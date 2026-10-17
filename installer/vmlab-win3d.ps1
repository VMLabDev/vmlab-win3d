<#
.SYNOPSIS
Installs, repairs or removes the vmlab-win3d graphics package.

.DESCRIPTION
Install.cmd and Uninstall.cmd call this script. It runs elevated (as an
administrator or SYSTEM) in 64-bit Windows PowerShell 5.1.

Modes:
  Install    Install, or upgrade in place when a package is already
             installed. Reboots once to turn on test signing when it is off,
             and once at the end when anything changed.
  Uninstall  Restore what Install changed, then reboot.
  Resume     Continue an install or uninstall after a reboot. Run by the
             vmlab-win3d-resume startup task.
  Repair     Put the D3D12 bridge back if Windows servicing replaced
             d3d12.dll. Run by the vmlab-win3d-repair startup task.
  Status     Print state.json.

State lives in C:\ProgramData\vmlab-win3d:
  state.json     status (Installing, RebootPending, Installed, Uninstalling,
                 Removed, Failed), the next step and the last error
  baseline.json  what the machine looked like before the first install
  journal.log    every action, timestamped
  backup\        the original files the package replaced
  bundle\        the staged package that the startup tasks run from
#>
[CmdletBinding()]
param(
    [ValidateSet('Install', 'Uninstall', 'Resume', 'Repair', 'Status')]
    [string]$Mode = 'Install',
    # Leave the reboot to the caller instead of restarting at the end of a step.
    [switch]$NoReboot
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$Root = Join-Path $env:ProgramData 'vmlab-win3d'
$StatePath = Join-Path $Root 'state.json'
$BaselinePath = Join-Path $Root 'baseline.json'
$JournalPath = Join-Path $Root 'journal.log'
$BackupDir = Join-Path $Root 'backup'
$BundleDir = Join-Path $Root 'bundle'
$TrashDir = Join-Path $Root 'trash'
$ScriptCopy = Join-Path $Root 'vmlab-win3d.ps1'
$ResumeTask = 'vmlab-win3d-resume'
$RepairTask = 'vmlab-win3d-repair'
$Sys32 = Join-Path $env:SystemRoot 'System32'
$SysWow = Join-Path $env:SystemRoot 'SysWOW64'
$Arches = @(
    @{ Name = 'x64'; Dir = $Sys32; Vulkan = 'HKLM:\SOFTWARE\Khronos\Vulkan\Drivers'; Icd = 'virtio_icd.x86_64.json' },
    @{ Name = 'x86'; Dir = $SysWow; Vulkan = 'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan\Drivers'; Icd = 'virtio_icd.x86.json' }
)
# Files the INF copies into System32 (x64) and SysWOW64 (x86).
$InfSystemFiles = @('viogpu_d3d10.dll', 'viogpu_wgl.dll', 'libvulkan_virtio.dll')
# Files under runtime\<arch>\ in the bundle, and where they go.
$PrivateFiles = @('vkd3dcore.dll', 'dxgi-vulkan.dll')
$LoaderFiles = @('vulkan-1.dll', 'vulkaninfo.exe')
# The bridge names its private native copy; no Microsoft d3d12.dll does.
$BridgeMarker = [Text.Encoding]::Unicode.GetBytes('d3d12-native.dll')

# ---------------------------------------------------------------- utilities

function Write-Journal([string]$Message) {
    $line = '{0} [{1}] {2}' -f (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss.fffzzz'), $Mode, $Message
    Write-Host $line
    if (Test-Path -LiteralPath $Root) {
        Add-Content -LiteralPath $JournalPath -Value $line -Encoding UTF8
    }
}

function Read-Json([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
}

function Write-Json([string]$Path, $Object) {
    $tmp = $Path + '.tmp'
    ConvertTo-Json -InputObject $Object -Depth 8 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $Path -Force
}

function Get-State {
    $s = Read-Json $StatePath
    $h = [ordered]@{}
    if ($s) { foreach ($p in $s.PSObject.Properties) { $h[$p.Name] = $p.Value } }
    return $h
}

function Set-State([hashtable]$Changes) {
    $s = Get-State
    foreach ($k in $Changes.Keys) { $s[$k] = $Changes[$k] }
    $s['schema'] = 1
    $s['updated'] = (Get-Date).ToString('o')
    Write-Json $StatePath $s
}

function Get-Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Get-BootId {
    return (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
}

function Invoke-Native([string]$File, [string[]]$Arguments, [int[]]$Ok = @(0)) {
    # Native stderr must not become a terminating error under 'Stop'.
    $ErrorActionPreference = 'Continue'
    $out = & $File @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Write-Journal ("{0} {1} -> {2}" -f $File, ($Arguments -join ' '), $code)
    foreach ($l in ($out -split "`r?`n")) { if ($l.Trim()) { Write-Journal ('  ' + $l.TrimEnd()) } }
    if ($Ok -notcontains $code) { throw ("{0} {1} failed with exit code {2}" -f $File, ($Arguments -join ' '), $code) }
    return [pscustomobject]@{ Code = $code; Output = $out }
}

# Move a file that may be loaded by a running process out of the way. A
# mapped DLL cannot be deleted but can be renamed on the same volume.
function Move-ToTrash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    New-Item -ItemType Directory -Path $TrashDir -Force | Out-Null
    $dest = Join-Path $TrashDir ([guid]::NewGuid().ToString('N') + '-' + (Split-Path -Leaf $Path))
    Move-Item -LiteralPath $Path -Destination $dest -Force
    Remove-Item -LiteralPath $dest -Force -Recurse -ErrorAction SilentlyContinue
}

function Clear-Trash {
    if (Test-Path -LiteralPath $TrashDir) {
        Get-ChildItem -LiteralPath $TrashDir -Force | Remove-Item -Force -Recurse -ErrorAction SilentlyContinue
    }
}

# ------------------------------------------------------- file security (ACL)

function Get-Sddl([string]$Path) { return (Get-Acl -LiteralPath $Path).Sddl }

# Take ownership of a protected system file (owned by TrustedInstaller) so
# Administrators and SYSTEM can rename and replace it.
function Unlock-SystemFile([string]$Path) {
    Invoke-Native 'takeown.exe' @('/F', $Path, '/A') | Out-Null
    Invoke-Native 'icacls.exe' @($Path, '/grant', '*S-1-5-32-544:F', '*S-1-5-18:F') | Out-Null
}

# The DACL part of an SDDL string without the auto-inherit control flags (AI,
# AR), which Set-Acl adds and which change no access.
function Get-AccessKey([string]$Access) {
    $m = [regex]::Match($Access, '^D:([A-Z]*)(.*)$')
    if (-not $m.Success) { return $Access }
    return 'D:' + ($m.Groups[1].Value -replace 'AI|AR', '') + $m.Groups[2].Value
}

function Test-SddlMatch([string]$Path, [string]$Sddl) {
    $a = New-Object Security.AccessControl.FileSecurity
    $a.SetSecurityDescriptorSddlForm($Sddl)
    $b = New-Object Security.AccessControl.FileSecurity
    $b.SetSecurityDescriptorSddlForm((Get-Sddl $Path))
    return ($a.GetSecurityDescriptorSddlForm('Owner') -eq $b.GetSecurityDescriptorSddlForm('Owner')) -and
        ((Get-AccessKey $a.GetSecurityDescriptorSddlForm('Access')) -eq (Get-AccessKey $b.GetSecurityDescriptorSddlForm('Access')))
}

# Put back an owner and ACL recorded with Get-Sddl, in one Set-Acl call:
# Windows PowerShell enables SeRestorePrivilege to set an owner such as
# TrustedInstaller. Setting the DACL first would drop our own WRITE_OWNER.
function Restore-Sddl([string]$Path, [string]$Sddl) {
    $want = New-Object Security.AccessControl.FileSecurity
    $want.SetSecurityDescriptorSddlForm($Sddl)
    $owner = $want.GetOwner([Security.Principal.SecurityIdentifier]).Value
    $wantAccess = $want.GetSecurityDescriptorSddlForm('Access')
    $acl = Get-Acl -LiteralPath $Path
    $acl.SetSecurityDescriptorSddlForm($Sddl, 'Owner, Group, Access')
    Set-Acl -LiteralPath $Path -AclObject $acl
    $got = New-Object Security.AccessControl.FileSecurity
    $got.SetSecurityDescriptorSddlForm((Get-Sddl $Path))
    $gotOwner = $got.GetOwner([Security.Principal.SecurityIdentifier]).Value
    $gotAccess = $got.GetSecurityDescriptorSddlForm('Access')
    if ($gotOwner -ne $owner -or (Get-AccessKey $gotAccess) -ne (Get-AccessKey $wantAccess)) {
        throw "Could not restore the owner and ACL of $Path (owner $gotOwner, want $owner; ACL $gotAccess, want $wantAccess)"
    }
}

# Replace a system file with $Source, keeping the security descriptor $Sddl.
function Set-SystemFile([string]$Path, [string]$Source, [string]$Sddl) {
    if (Test-Path -LiteralPath $Path) {
        Unlock-SystemFile $Path
        Move-ToTrash $Path
    }
    Copy-Item -LiteralPath $Source -Destination $Path
    if ($Sddl) { Restore-Sddl $Path $Sddl }
    if ((Get-Sha256 $Path) -ne (Get-Sha256 $Source)) { throw "Copy to $Path did not verify" }
    Write-Journal "installed $Path ($(Get-Sha256 $Path))"
}

function Remove-SystemFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    Unlock-SystemFile $Path
    Move-ToTrash $Path
    Write-Journal "removed $Path"
}

# --------------------------------------------------------- machine queries

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object Security.Principal.WindowsPrincipal $id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# $true, $false, or $null when the firmware has no Secure Boot variable
# (a vmlab VM with secure_boot = false reports "Variable is currently undefined").
function Get-SecureBoot {
    try { return [bool](Confirm-SecureBootUEFI) } catch { return $null }
}

function Get-TestSigningSetting {
    $text = (& bcdedit.exe /enum '{current}') -join "`n"
    if ($LASTEXITCODE -ne 0) { throw 'bcdedit /enum {current} failed' }
    if ($text -match '(?m)^testsigning\s+(\S+)') { return $matches[1] }
    return 'Absent'
}

# Whether the running kernel accepts test-signed code, which can differ from
# the BCD setting until the next boot.
function Test-TestSigningActive {
    if (-not ('VmlabWin3d.CodeIntegrity' -as [type])) {
        Add-Type -Namespace VmlabWin3d -Name CodeIntegrity -MemberDefinition @'
[DllImport("ntdll.dll")]
public static extern int NtQuerySystemInformation(int cls, byte[] info, int len, out int retLen);
public static int Options() {
    byte[] b = new byte[8];
    System.BitConverter.GetBytes(8).CopyTo(b, 0);
    int r;
    int st = NtQuerySystemInformation(103, b, 8, out r); // SystemCodeIntegrityInformation
    if (st != 0) throw new System.ComponentModel.Win32Exception("NtQuerySystemInformation " + st);
    return System.BitConverter.ToInt32(b, 4);
}
'@
    }
    return ([VmlabWin3d.CodeIntegrity]::Options() -band 0x2) -ne 0 # CODEINTEGRITY_OPTION_TESTSIGN
}

function Get-HibernateState {
    $p = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Power' -Name HibernateEnabled -ErrorAction SilentlyContinue
    $b = Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -ErrorAction SilentlyContinue
    return [ordered]@{
        hibernate_enabled = $(if ($p) { [int]$p.HibernateEnabled } else { $null })
        hiberboot_enabled = $(if ($b) { [int]$b.HiberbootEnabled } else { $null })
    }
}

function Test-CertInStore([string]$Store, [string]$Thumbprint) {
    $s = New-Object Security.Cryptography.X509Certificates.X509Store($Store, 'LocalMachine')
    try {
        $s.Open('ReadOnly')
        return $s.Certificates.Find('FindByThumbprint', $Thumbprint, $false).Count -gt 0
    } finally { $s.Close() }
}

function Get-Device([string]$HardwareId) {
    foreach ($d in @(Get-PnpDevice -Class Display -PresentOnly -ErrorAction SilentlyContinue)) {
        $ids = @((Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_HardwareIds).Data)
        if ($ids -contains $HardwareId) { return $d }
    }
    return $null
}

function Get-DeviceDriver([string]$InstanceId) {
    $p = @{}
    foreach ($k in 'DEVPKEY_Device_DriverInfPath', 'DEVPKEY_Device_DriverVersion', 'DEVPKEY_Device_DriverProvider',
        'DEVPKEY_Device_ProblemCode', 'DEVPKEY_Device_Driver') {
        $p[$k] = (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $k -ErrorAction SilentlyContinue).Data
    }
    return [ordered]@{
        inf = $p['DEVPKEY_Device_DriverInfPath']
        version = $p['DEVPKEY_Device_DriverVersion']
        provider = $p['DEVPKEY_Device_DriverProvider']
        problem_code = $p['DEVPKEY_Device_ProblemCode']
        driver_key = $p['DEVPKEY_Device_Driver']
    }
}

# Our driver packages in the driver store: published name, store directory,
# and whether its catalog is the one in this bundle.
function Get-OurPackages([string]$CatHash) {
    @(Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match '\\viogpu3d\.inf$' } | ForEach-Object {
        $dir = Split-Path -Parent $_.OriginalFileName
        [pscustomobject]@{
            Published = $_.Driver
            Directory = $dir
            Current = ((Get-Sha256 (Join-Path $dir 'viogpu3d.cat')) -eq $CatHash)
        }
    })
}

function Test-IsBridge([string]$Path, [string[]]$KnownHashes) {
    if (-not (Test-Path -LiteralPath $Path)) { return $false }
    if ($KnownHashes -contains (Get-Sha256 $Path)) { return $true }
    # Byte-for-byte search through a Latin-1 view of the file, which maps
    # each byte to one char.
    $latin1 = [Text.Encoding]::GetEncoding(28591)
    $text = $latin1.GetString([IO.File]::ReadAllBytes($Path))
    if ($text.IndexOf($latin1.GetString($BridgeMarker), [StringComparison]::Ordinal) -ge 0) { return $true }
    return $false
}

# ------------------------------------------------------------------ bundle

function Get-Bundle([string]$Dir) {
    $m = Read-Json (Join-Path $Dir 'manifest.json')
    if (-not $m -or $m.schema -ne 1) { throw "No valid manifest.json in $Dir" }
    foreach ($f in $m.files) {
        $p = Join-Path $Dir ($f.path -replace '/', '\')
        if ((Get-Sha256 $p) -ne $f.sha256.ToUpper()) { throw "Bundle file is missing or changed: $($f.path)" }
    }
    $cer = Join-Path $Dir 'vmlab-win3d-test.cer'
    $cert = New-Object Security.Cryptography.X509Certificates.X509Certificate2($cer)
    return [pscustomobject]@{
        Dir = $Dir
        Manifest = $m
        Version = $m.version
        Hash = Get-Sha256 (Join-Path $Dir 'manifest.json')
        Cer = $cer
        Thumbprint = $cert.Thumbprint
        Inf = Join-Path $Dir 'driver\viogpu3d.inf'
        CatHash = Get-Sha256 (Join-Path $Dir 'driver\viogpu3d.cat')
        HardwareId = $m.hardware_id
        DriverVersion = $m.driver_version
    }
}

function Copy-BundleToRoot([string]$Source) {
    if ((Resolve-Path -LiteralPath $Source).Path.TrimEnd('\') -ieq $BundleDir) { return }
    if (Test-Path -LiteralPath $BundleDir) { Remove-Item -LiteralPath $BundleDir -Recurse -Force }
    Copy-Item -LiteralPath $Source -Destination $BundleDir -Recurse
    Copy-Item -LiteralPath (Join-Path $BundleDir 'vmlab-win3d.ps1') -Destination $ScriptCopy -Force
    Write-Journal "staged bundle $Source -> $BundleDir"
}

# ---------------------------------------------------------------- baseline

function Get-ManagedFiles {
    foreach ($a in $Arches) {
        foreach ($n in @('d3d12.dll') + $LoaderFiles + $InfSystemFiles + @($a.Icd)) {
            [pscustomobject]@{ Arch = $a.Name; Name = $n; Path = Join-Path $a.Dir $n }
        }
    }
}

function New-Baseline($Bundle, $Device) {
    New-Item -ItemType Directory -Path $BackupDir -Force | Out-Null
    $files = foreach ($f in Get-ManagedFiles) {
        $exists = Test-Path -LiteralPath $f.Path
        $backup = $null
        if ($exists -and $f.Name -in @('d3d12.dll') + $LoaderFiles) {
            $backup = Join-Path $BackupDir ($f.Arch + '-' + $f.Name)
            Copy-Item -LiteralPath $f.Path -Destination $backup -Force
        }
        [ordered]@{
            arch = $f.Arch; name = $f.Name; path = $f.Path; exists = $exists
            sha256 = Get-Sha256 $f.Path
            version = $(if ($exists) { [Diagnostics.FileVersionInfo]::GetVersionInfo($f.Path).FileVersion } else { $null })
            sddl = $(if ($exists) { Get-Sddl $f.Path } else { $null })
            backup = $backup
        }
    }
    $vulkan = foreach ($a in $Arches) {
        $name = Join-Path $a.Dir $a.Icd
        $key = Get-Item -LiteralPath $a.Vulkan -ErrorAction SilentlyContinue
        [ordered]@{ key = $a.Vulkan; name = $name; existed = [bool]($key -and ($key.GetValueNames() -contains $name)); key_existed = [bool]$key }
    }
    $services = foreach ($s in 'VioGpu3D', 'vgpusrv') {
        [ordered]@{ name = $s; existed = [bool](Get-Service -Name $s -ErrorAction SilentlyContinue) }
    }
    $b = [ordered]@{
        schema = 1
        captured = (Get-Date).ToString('o')
        os = (Get-CimInstance Win32_OperatingSystem).Version
        testsigning = Get-TestSigningSetting
        power = Get-HibernateState
        certificates = [ordered]@{
            thumbprint = $Bundle.Thumbprint
            root = Test-CertInStore 'Root' $Bundle.Thumbprint
            trusted_publisher = Test-CertInStore 'TrustedPublisher' $Bundle.Thumbprint
        }
        device = [ordered]@{ instance_id = $Device.InstanceId; driver = Get-DeviceDriver $Device.InstanceId }
        class_values = @(Get-RegValues (Get-DeviceKeys $Device.InstanceId).class $ClassValues)
        msi_values = @(Get-RegValues (Get-DeviceKeys $Device.InstanceId).msi $MsiValues)
        files = @($files)
        vulkan_registry = @($vulkan)
        services = @($services)
    }
    Write-Json $BaselinePath $b
    Write-Journal ("baseline captured: testsigning={0} hibernate={1} driver={2} {3}" -f $b.testsigning,
        $b.power.hibernate_enabled, $b.device.driver.inf, $b.device.driver.version)
    return Read-Json $BaselinePath
}

function Get-BaselineFile($Baseline, [string]$Arch, [string]$Name) {
    return @($Baseline.files | Where-Object { $_.arch -eq $Arch -and $_.name -eq $Name })[0]
}

# Registry values the INF writes into keys that outlive the driver: the
# display class key (PnP keeps it for the next driver of the device) and the
# device's hardware key. Uninstall puts each back as it was.
$ClassValues = @('UserModeDriverName', 'UserModeDriverNameWow', 'OpenGLDriverName', 'OpenGLDriverNameWow',
    'OpenGLFlags', 'OpenGLFlagsWow', 'OpenGLVersion', 'OpenGLVersionWow', 'InstalledDisplayDrivers',
    'HWCursor', 'FlexResolution', 'UsePhysicalMemory')
$MsiValues = @('MSISupported', 'MessageNumberLimit')

function Get-DeviceKeys([string]$InstanceId) {
    $drv = (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName DEVPKEY_Device_Driver).Data
    return [ordered]@{
        class = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$drv"
        msi = "HKLM:\SYSTEM\CurrentControlSet\Enum\$InstanceId\Device Parameters\Interrupt Management\MessageSignaledInterruptProperties"
    }
}

function Get-RegValues([string]$Key, [string[]]$Names) {
    $item = Get-Item -LiteralPath $Key -ErrorAction SilentlyContinue
    foreach ($n in $Names) {
        $exists = [bool]($item -and ($item.GetValueNames() -contains $n))
        [ordered]@{
            name = $n; exists = $exists
            kind = $(if ($exists) { $item.GetValueKind($n).ToString() } else { $null })
            value = $(if ($exists) { $item.GetValue($n, $null, 'DoNotExpandEnvironmentNames') } else { $null })
        }
    }
}

function Restore-RegValues([string]$Key, $Saved) {
    if (-not (Test-Path -LiteralPath $Key)) { return }
    $item = Get-Item -LiteralPath $Key
    foreach ($v in $Saved) {
        $has = $item.GetValueNames() -contains $v.name
        if ($v.exists) {
            $cur = $(if ($has) { ConvertTo-Json -Compress -InputObject $item.GetValue($v.name, $null, 'DoNotExpandEnvironmentNames') } else { $null })
            if (-not $has -or $cur -ne (ConvertTo-Json -Compress -InputObject $v.value) -or $item.GetValueKind($v.name).ToString() -ne $v.kind) {
                $data = $v.value
                if ($v.kind -eq 'MultiString') { $data = [string[]]@($v.value) }
                New-ItemProperty -LiteralPath $Key -Name $v.name -PropertyType $v.kind -Value $data -Force | Out-Null
                Write-Journal "restored $Key\$($v.name)"
            }
        } elseif ($has) {
            Remove-ItemProperty -LiteralPath $Key -Name $v.name
            Write-Journal "removed $Key\$($v.name)"
        }
    }
}

function Test-RegValues([string]$Key, $Saved) {
    $now = @(Get-RegValues $Key @($Saved | ForEach-Object { $_.name }))
    $bad = @()
    for ($i = 0; $i -lt $now.Count; $i++) {
        if ((ConvertTo-Json -Compress -InputObject $now[$i]) -ne (ConvertTo-Json -Compress -InputObject ([ordered]@{
            name = $Saved[$i].name; exists = $Saved[$i].exists; kind = $Saved[$i].kind; value = $Saved[$i].value }))) {
            $bad += "$Key\$($Saved[$i].name)"
        }
    }
    return $bad
}

# ----------------------------------------------------------- startup tasks

function Register-StartupTask([string]$Name, [string]$TaskMode, [string]$Delay) {
    $ps = Join-Path $Sys32 'WindowsPowerShell\v1.0\powershell.exe'
    $action = New-ScheduledTaskAction -Execute $ps -Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "{0}" -Mode {1}' -f $ScriptCopy, $TaskMode)
    $trigger = New-ScheduledTaskTrigger -AtStartup
    $trigger.Delay = $Delay
    $principal = New-ScheduledTaskPrincipal -UserId 'S-1-5-18' -LogonType ServiceAccount -RunLevel Highest
    $settings = New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Minutes 30) -MultipleInstances IgnoreNew -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
    Register-ScheduledTask -TaskName $Name -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Force | Out-Null
    Write-Journal "registered startup task $Name (-Mode $TaskMode)"
}

function Unregister-Task([string]$Name) {
    if (Get-ScheduledTask -TaskName $Name -ErrorAction SilentlyContinue) {
        Unregister-ScheduledTask -TaskName $Name -Confirm:$false
        Write-Journal "removed task $Name"
    }
}

function Request-Reboot([string]$Status, [string]$Step) {
    Register-StartupTask $ResumeTask 'Resume' 'PT20S'
    Set-State @{ status = $Status; step = $Step; boot_id = (Get-BootId); last_error = $null }
    if ($NoReboot) {
        Write-Journal "reboot required before step $Step (-NoReboot given)"
    } else {
        Write-Journal "rebooting; step $Step resumes after the restart"
        & shutdown.exe /r /t 5 /f /d p:2:4 /c 'vmlab-win3d setup continues after this restart.' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "shutdown /r failed with $LASTEXITCODE" }
    }
}

# ------------------------------------------------------------ install steps

function Step-Trust($Bundle) {
    foreach ($store in 'Root', 'TrustedPublisher') {
        if (-not (Test-CertInStore $store $Bundle.Thumbprint)) {
            # -f creates TrustedPublisher, which the template lacks, without a prompt.
            Invoke-Native 'certutil.exe' @('-f', '-addstore', $store, $Bundle.Cer) | Out-Null
        }
        if (-not (Test-CertInStore $store $Bundle.Thumbprint)) { throw "Certificate is not in LocalMachine\$store after import" }
    }
    # Fast Startup resumes a hibernated kernel session with stale graphics
    # state (docs/decisions/0001-signing.md). Turning hibernation off disables it.
    $h = Get-HibernateState
    if ($h.hibernate_enabled -ne 0) { Invoke-Native 'powercfg.exe' @('/hibernate', 'off') | Out-Null }
    if ((Get-TestSigningSetting) -ne 'Yes') {
        Invoke-Native 'bcdedit.exe' @('/set', '{current}', 'testsigning', 'on') | Out-Null
    }
}

# Returns $true when the device's driver changed.
function Step-Driver($Bundle, $Device) {
    $changed = $false
    foreach ($p in Get-OurPackages $Bundle.CatHash) {
        if (-not $p.Current) {
            Write-Journal "removing older package $($p.Published) ($($p.Directory))"
            Invoke-Native 'pnputil.exe' @('/delete-driver', $p.Published, '/uninstall', '/force') @(0, 3010) | Out-Null
            $changed = $true
        }
    }
    $ours = @(Get-OurPackages $Bundle.CatHash | Where-Object Current)
    $bound = (Get-DeviceDriver $Device.InstanceId).inf
    if ($ours.Count -eq 1 -and $bound -eq $ours[0].Published) {
        Write-Journal "driver $bound already installed on $($Device.InstanceId)"
        return $changed
    }
    # 259: the package is in the store already and no device needed it.
    Invoke-Native 'pnputil.exe' @('/add-driver', $Bundle.Inf, '/install') @(0, 259, 3010) | Out-Null
    $ours = @(Get-OurPackages $Bundle.CatHash | Where-Object Current)
    if ($ours.Count -ne 1) { throw "pnputil did not publish the package (found $($ours.Count))" }
    $drv = Get-DeviceDriver $Device.InstanceId
    if ($drv.inf -ne $ours[0].Published) {
        throw ("The display device uses {0} {1}, not this package's {2}. A better-ranked driver for {3} is in the driver store; remove it with 'pnputil /delete-driver {0} /uninstall' and run Install.cmd again." -f
            $drv.inf, $drv.version, $ours[0].Published, $Bundle.HardwareId)
    }
    Write-Journal "driver $($drv.inf) $($drv.version) installed on $($Device.InstanceId)"
    return $true
}

function Get-KnownBridgeHashes($Bundle) {
    $s = Get-State
    $known = @()
    if ($s.Contains('bridge_hashes') -and $s['bridge_hashes']) { $known += @($s['bridge_hashes']) }
    foreach ($a in $Arches) { $known += Get-Sha256 (Join-Path $Bundle.Dir "runtime\$($a.Name)\d3d12.dll") }
    return @($known | Where-Object { $_ } | Select-Object -Unique)
}

# Install the bridge for one architecture. A d3d12.dll that is not a bridge
# is the machine's native one (the original, or one Windows servicing put
# back); it becomes the private d3d12-native.dll first. Returns $true when
# anything changed.
function Install-Bridge($Bundle, $Baseline, $Arch) {
    $changed = $false
    $sys = Join-Path $Arch.Dir 'd3d12.dll'
    $private = Join-Path $Arch.Dir 'vmlab-win3d'
    $src = Join-Path $Bundle.Dir "runtime\$($Arch.Name)"
    $known = Get-KnownBridgeHashes $Bundle
    New-Item -ItemType Directory -Path $private -Force | Out-Null
    $native = Join-Path $private 'd3d12-native.dll'
    if (-not (Test-Path -LiteralPath $sys)) { throw "$sys is missing" }
    if (-not (Test-IsBridge $sys $known)) {
        $hash = Get-Sha256 $sys
        if ((Get-Sha256 $native) -ne $hash) {
            Copy-Item -LiteralPath $sys -Destination $native -Force
            Write-Journal "native d3d12.dll $hash copied to $native"
        }
        # Keep a backup of the newest native file and its security descriptor,
        # which uninstall restores.
        $backup = Join-Path $BackupDir "$($Arch.Name)-d3d12.dll"
        if ((Get-Sha256 $backup) -ne $hash) { Copy-Item -LiteralPath $sys -Destination $backup -Force; $changed = $true }
        Set-State @{ ("native_d3d12_" + $Arch.Name) = [ordered]@{ sha256 = $hash; sddl = (Get-Sddl $sys); backup = $backup } }
    }
    foreach ($n in $PrivateFiles) {
        $dst = Join-Path $private $n
        if ((Get-Sha256 $dst) -ne (Get-Sha256 (Join-Path $src $n))) {
            if (Test-Path -LiteralPath $dst) { Move-ToTrash $dst }
            Copy-Item -LiteralPath (Join-Path $src $n) -Destination $dst
            Write-Journal "installed $dst"
            $changed = $true
        }
    }
    if (-not (Test-Path -LiteralPath $native)) { throw "$native is missing and $sys is already a bridge" }
    $bridge = Join-Path $src 'd3d12.dll'
    $s = Get-State
    $sddl = $(if ($s[("native_d3d12_" + $Arch.Name)]) { $s[("native_d3d12_" + $Arch.Name)].sddl } else { $null })
    if ((Get-Sha256 $sys) -ne (Get-Sha256 $bridge)) {
        Set-SystemFile $sys $bridge $sddl
        $changed = $true
    } elseif ($sddl -and -not (Test-SddlMatch $sys $sddl)) {
        Write-Journal "$sys has the wrong owner or ACL; restoring"
        Restore-Sddl $sys $sddl
    }
    $hashes = @(Get-KnownBridgeHashes $Bundle)
    Set-State @{ bridge_hashes = $hashes }
    return $changed
}

# The file version from the binary VS_FIXEDFILEINFO, or $null when the file
# has none.
function Get-FileVersion([string]$Path) {
    $vi = [Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
    $v = New-Object Version($vi.FileMajorPart, $vi.FileMinorPart, $vi.FileBuildPart, $vi.FilePrivatePart)
    if ($v -eq (New-Object Version(0, 0, 0, 0))) { return $null }
    return $v
}

# vulkan-1.dll goes in when the machine has none, or an older one, or the one
# a previous run of this package installed. vulkaninfo.exe follows the
# loader's decision, and is also installed where none exists.
function Install-Loader($Bundle, $Baseline, $Arch) {
    $changed = $false
    $s = Get-State
    $installed = @{}
    if ($s.Contains('loader_installed') -and $s['loader_installed']) {
        foreach ($p in $s['loader_installed'].PSObject.Properties) { $installed[$p.Name] = $p.Value }
    }
    $src = Join-Path $Bundle.Dir "runtime\$($Arch.Name)"
    $loader = Join-Path $Arch.Dir 'vulkan-1.dll'
    $cur = Get-Sha256 $loader
    $key = "$($Arch.Name)/vulkan-1.dll"
    $take = $true
    if ($cur -and $cur -ne (Get-Sha256 (Join-Path $src 'vulkan-1.dll')) -and -not ($installed.ContainsKey($key) -and $installed[$key] -eq $cur)) {
        $have = Get-FileVersion $loader
        $want = Get-FileVersion (Join-Path $src 'vulkan-1.dll')
        if ($null -eq $want) { throw "The bundle's $($Arch.Name) vulkan-1.dll has no file version" }
        if ($null -ne $have -and $have -ge $want) {
            Write-Journal "keeping $loader $have (not older than the package's $want)"
            $take = $false
        }
    }
    foreach ($n in $LoaderFiles) {
        $dst = Join-Path $Arch.Dir $n
        $new = Get-Sha256 (Join-Path $src $n)
        $k = "$($Arch.Name)/$n"
        $now = Get-Sha256 $dst
        if ($now -eq $new) { $installed[$k] = $new; continue }
        if (-not $take -and $now) { continue }
        $bf = Get-BaselineFile $Baseline $Arch.Name $n
        Set-SystemFile $dst (Join-Path $src $n) $(if ($bf -and $bf.sddl) { $bf.sddl } else { $null })
        $installed[$k] = $new
        $changed = $true
    }
    Set-State @{ loader_installed = $installed }
    return $changed
}

function Step-Runtime($Bundle, $Baseline) {
    $changed = $false
    foreach ($a in $Arches) {
        if (Install-Loader $Bundle $Baseline $a) { $changed = $true }
        if (Install-Bridge $Bundle $Baseline $a) { $changed = $true }
    }
    Register-StartupTask $RepairTask 'Repair' 'PT10S'
    return $changed
}

function Test-Installed($Bundle, $Device) {
    $problems = @()
    $ours = @(Get-OurPackages $Bundle.CatHash | Where-Object Current)
    $drv = Get-DeviceDriver $Device.InstanceId
    if ($ours.Count -ne 1 -or $drv.inf -ne $ours[0].Published) { $problems += "device driver is $($drv.inf), not this package" }
    if ($drv.problem_code -ne 0) { $problems += "device problem code $($drv.problem_code)" }
    if ($drv.driver_key) {
        $k = Get-Item -LiteralPath ("HKLM:\SYSTEM\CurrentControlSet\Control\Class\" + $drv.driver_key)
        $umd = @($k.GetValue('UserModeDriverName'))
        if ($umd.Count -ne 3 -or ($umd | Where-Object { $_ -notmatch '\\viogpu_d3d10\.dll$' })) { $problems += "UserModeDriverName is '$($umd -join ',')'" }
        if (@($k.GetValue('OpenGLDriverName')) -notmatch 'viogpu_wgl\.dll$') { $problems += 'OpenGLDriverName is not set' }
    }
    foreach ($a in $Arches) {
        $sys = Join-Path $a.Dir 'd3d12.dll'
        if ((Get-Sha256 $sys) -ne (Get-Sha256 (Join-Path $Bundle.Dir "runtime\$($a.Name)\d3d12.dll"))) { $problems += "$sys is not the bridge" }
        if (-not (Test-Path -LiteralPath (Join-Path $a.Dir 'vmlab-win3d\d3d12-native.dll'))) { $problems += "$($a.Name) d3d12-native.dll is missing" }
        $icd = Join-Path $a.Dir $a.Icd
        $key = Get-Item -LiteralPath $a.Vulkan -ErrorAction SilentlyContinue
        if (-not $key -or $key.GetValue($icd) -ne 0) { $problems += "Vulkan ICD $icd is not registered" }
        if (-not (Test-Path -LiteralPath $icd)) { $problems += "$icd is missing" }
    }
    if (-not (Get-ScheduledTask -TaskName $RepairTask -ErrorAction SilentlyContinue)) { $problems += "task $RepairTask is missing" }
    return $problems
}

function Wait-DeviceStarted($Device) {
    for ($i = 0; $i -lt 24; $i++) {
        if ((Get-DeviceDriver $Device.InstanceId).problem_code -eq 0) { return }
        Start-Sleep -Seconds 5
    }
}

# The whole install from wherever the state says it is. Idempotent: each
# step checks before it changes anything.
function Invoke-Install($Bundle, [string]$Step) {
    $device = Get-Device $Bundle.HardwareId
    if (-not $device) { throw "No display device with hardware ID $($Bundle.HardwareId). The VM needs a virtio-gpu GL device (docs/install.md)." }
    $baseline = Read-Json $BaselinePath

    if ($Step -eq 'Prepare') {
        Step-Trust $Bundle
        if (-not (Test-TestSigningActive)) {
            Write-Journal 'test signing is set but not active in this boot'
            Request-Reboot 'RebootPending' 'Driver'
            return
        }
        $Step = 'Driver'
    }
    if ($Step -eq 'Driver') {
        if (-not (Test-TestSigningActive)) { throw 'Test signing is still off after the reboot. Is Secure Boot on?' }
        Set-State @{ status = 'Installing'; step = 'Driver' }
        $changed = Step-Driver $Bundle $device
        Set-State @{ step = 'Runtime' }
        if (Step-Runtime $Bundle $baseline) { $changed = $true }
        Clear-Trash
        if ($changed) {
            Request-Reboot 'RebootPending' 'Verify'
            return
        }
        Write-Journal 'nothing changed; no reboot needed'
        $Step = 'Verify'
    }
    if ($Step -eq 'Verify') {
        Wait-DeviceStarted $device
        $problems = @(Test-Installed $Bundle $device)
        Unregister-Task $ResumeTask
        if ($problems.Count) {
            Set-State @{ status = 'Failed'; step = 'Verify'; last_error = ($problems -join '; ') }
            throw ('Install verification failed: ' + ($problems -join '; '))
        }
        $drv = Get-DeviceDriver $device.InstanceId
        Set-State @{ status = 'Installed'; step = $null; last_error = $null; installed_inf = $drv.inf; driver_version = $drv.version; installed = (Get-Date).ToString('o') }
        Write-Journal "installed: $($drv.inf) $($drv.version), problem code 0"
    }
}

# ---------------------------------------------------------- uninstall steps

function Invoke-Remove($Baseline) {
    Unregister-Task $RepairTask
    $state = Get-State
    $bundle = Get-Bundle $BundleDir

    # D3D12 bridge: put back the newest native d3d12.dll (which the repair
    # task refreshes) with the original owner and ACL.
    foreach ($a in $Arches) {
        $sys = Join-Path $a.Dir 'd3d12.dll'
        $private = Join-Path $a.Dir 'vmlab-win3d'
        $nat = if ($state.Contains("native_d3d12_$($a.Name)")) { $state["native_d3d12_$($a.Name)"] } else { $null }
        $bf = Get-BaselineFile $Baseline $a.Name 'd3d12.dll'
        if (Test-IsBridge $sys (Get-KnownBridgeHashes $bundle)) {
            $src = Join-Path $private 'd3d12-native.dll'
            if (-not (Test-Path -LiteralPath $src)) { $src = $(if ($nat) { $nat.backup } else { $bf.backup }) }
            if (-not $src -or -not (Test-Path -LiteralPath $src)) { throw "No native d3d12.dll to restore for $($a.Name)" }
            $sddl = $(if ($nat) { $nat.sddl } else { $bf.sddl })
            Set-SystemFile $sys $src $sddl
        } else {
            Write-Journal "$sys is not the bridge; left alone"
        }
        if (Test-Path -LiteralPath $private) {
            Get-ChildItem -LiteralPath $private -Force | ForEach-Object { Move-ToTrash $_.FullName }
            Remove-Item -LiteralPath $private -Recurse -Force -ErrorAction SilentlyContinue
            Write-Journal "removed $private"
        }
    }

    # Vulkan loader and vulkaninfo: restore only what the package installed
    # and nobody changed since.
    $installed = @{}
    if ($state.Contains('loader_installed') -and $state['loader_installed']) {
        foreach ($p in $state['loader_installed'].PSObject.Properties) { $installed[$p.Name] = $p.Value }
    }
    foreach ($a in $Arches) {
        foreach ($n in $LoaderFiles) {
            $dst = Join-Path $a.Dir $n
            $key = "$($a.Name)/$n"
            $bf = Get-BaselineFile $Baseline $a.Name $n
            if (-not $installed.ContainsKey($key)) { continue }
            $cur = Get-Sha256 $dst
            if ($cur -ne $installed[$key]) { Write-Journal "$dst changed since install; left alone"; continue }
            if ($bf.sha256 -eq $cur) { continue }
            if ($bf.exists) { Set-SystemFile $dst $bf.backup $bf.sddl } else { Remove-SystemFile $dst }
        }
    }

    # The driver package. Deleting it with /uninstall puts the next best
    # driver (the template's viogpudo) back on the device.
    foreach ($p in Get-OurPackages '-') {
        Invoke-Native 'pnputil.exe' @('/delete-driver', $p.Published, '/uninstall', '/force') @(0, 3010) | Out-Null
    }
    $device = Get-PnpDevice -InstanceId $Baseline.device.instance_id -ErrorAction SilentlyContinue
    if ($device -and (Get-DeviceDriver $device.InstanceId).inf -ne $Baseline.device.driver.inf) {
        Invoke-Native 'pnputil.exe' @('/scan-devices') | Out-Null
    }
    if ($device -and (Get-DeviceDriver $device.InstanceId).inf -ne $Baseline.device.driver.inf) {
        $prev = Join-Path $env:SystemRoot ('INF\' + $Baseline.device.driver.inf)
        $store = @(Get-WindowsDriver -Online | Where-Object Driver -eq $Baseline.device.driver.inf)
        if ($store.Count) { $prev = $store[0].OriginalFileName }
        Invoke-Native 'pnputil.exe' @('/add-driver', $prev, '/install') @(0, 259, 3010) | Out-Null
    }
    # Values the INF wrote into the class and hardware keys, which the
    # restored driver reuses.
    if ($device) {
        $keys = Get-DeviceKeys $device.InstanceId
        Restore-RegValues $keys.class @($Baseline.class_values)
        Restore-RegValues $keys.msi @($Baseline.msi_values)
    }
    # The services the INF added and pnputil leaves behind.
    foreach ($svc in $Baseline.services) {
        if (-not $svc.existed -and (Get-Service -Name $svc.name -ErrorAction SilentlyContinue)) {
            try { Invoke-Native 'sc.exe' @('stop', $svc.name) | Out-Null } catch { Write-Journal "could not stop $($svc.name): $($_.Exception.Message)" }
            Invoke-Native 'sc.exe' @('delete', $svc.name) @(0, 1072) | Out-Null
        }
    }
    # Files the INF copied into System32 and SysWOW64, and its Vulkan values.
    foreach ($a in $Arches) {
        foreach ($n in $InfSystemFiles + @($a.Icd)) {
            $bf = Get-BaselineFile $Baseline $a.Name $n
            if ($bf -and -not $bf.exists) { Remove-SystemFile (Join-Path $a.Dir $n) }
        }
    }
    foreach ($v in $Baseline.vulkan_registry) {
        if (-not $v.existed -and (Test-Path -LiteralPath $v.key)) {
            Remove-ItemProperty -LiteralPath $v.key -Name $v.name -ErrorAction SilentlyContinue
            Write-Journal "removed registry value $($v.key)\$($v.name)"
            if (-not $v.key_existed -and -not (Get-Item -LiteralPath $v.key).GetValueNames()) {
                Remove-Item -LiteralPath $v.key -ErrorAction SilentlyContinue
            }
        }
    }

    # Certificate and boot policy.
    $tp = $Baseline.certificates.thumbprint
    foreach ($store in @(@('Root', $Baseline.certificates.root), @('TrustedPublisher', $Baseline.certificates.trusted_publisher))) {
        if (-not $store[1] -and (Test-CertInStore $store[0] $tp)) {
            Invoke-Native 'certutil.exe' @('-delstore', $store[0], $tp) | Out-Null
        }
    }
    $ts = Get-TestSigningSetting
    if ($Baseline.testsigning -eq 'Absent' -and $ts -ne 'Absent') {
        Invoke-Native 'bcdedit.exe' @('/deletevalue', '{current}', 'testsigning') | Out-Null
    } elseif ($Baseline.testsigning -in 'Yes', 'No' -and $ts -ne $Baseline.testsigning) {
        Invoke-Native 'bcdedit.exe' @('/set', '{current}', 'testsigning', $(if ($Baseline.testsigning -eq 'Yes') { 'on' } else { 'off' })) | Out-Null
    }
    if ($Baseline.power.hibernate_enabled -eq 1 -and (Get-HibernateState).hibernate_enabled -ne 1) {
        Invoke-Native 'powercfg.exe' @('/hibernate', 'on') | Out-Null
    }
    if ($null -ne $Baseline.power.hiberboot_enabled -and (Get-HibernateState).hiberboot_enabled -ne $Baseline.power.hiberboot_enabled) {
        Set-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -Value ([int]$Baseline.power.hiberboot_enabled) -Type DWord
        Write-Journal "HiberbootEnabled restored to $($Baseline.power.hiberboot_enabled)"
    }
    Clear-Trash
}

function Test-Removed($Baseline) {
    $problems = @()
    $drv = Get-DeviceDriver $Baseline.device.instance_id
    if ($drv.inf -ne $Baseline.device.driver.inf) { $problems += "device driver is $($drv.inf), baseline $($Baseline.device.driver.inf)" }
    if ($drv.problem_code -ne $Baseline.device.driver.problem_code) { $problems += "device problem code $($drv.problem_code), baseline $($Baseline.device.driver.problem_code)" }
    foreach ($f in $Baseline.files) {
        $h = Get-Sha256 $f.path
        if ($h -ne $f.sha256) { $problems += "$($f.path) is $h, baseline $($f.sha256)" }
    }
    $keys = Get-DeviceKeys $Baseline.device.instance_id
    foreach ($b in (Test-RegValues $keys.class @($Baseline.class_values)) + (Test-RegValues $keys.msi @($Baseline.msi_values))) {
        $problems += "$b differs from the baseline"
    }
    if ((Get-TestSigningSetting) -ne $Baseline.testsigning) { $problems += "testsigning is $(Get-TestSigningSetting), baseline $($Baseline.testsigning)" }
    if ((Test-CertInStore 'Root' $Baseline.certificates.thumbprint) -ne $Baseline.certificates.root) { $problems += 'Root certificate not restored' }
    if ((Test-CertInStore 'TrustedPublisher' $Baseline.certificates.thumbprint) -ne $Baseline.certificates.trusted_publisher) { $problems += 'TrustedPublisher certificate not restored' }
    if ((Get-HibernateState).hibernate_enabled -ne $Baseline.power.hibernate_enabled) { $problems += 'hibernation not restored' }
    foreach ($a in $Arches) { if (Test-Path -LiteralPath (Join-Path $a.Dir 'vmlab-win3d')) { $problems += "$($a.Dir)\vmlab-win3d remains" } }
    if (@(Get-OurPackages '-').Count) { $problems += 'a viogpu3d package remains in the driver store' }
    return $problems
}

function Invoke-Uninstall([string]$Step) {
    $baseline = Read-Json $BaselinePath
    if (-not $baseline) { throw 'No baseline.json; nothing to restore' }
    if ($Step -ne 'VerifyRemoval') {
        Set-State @{ status = 'Uninstalling'; step = 'Remove'; last_error = $null }
        Invoke-Remove $baseline
        Request-Reboot 'Uninstalling' 'VerifyRemoval'
        return
    }
    Start-Sleep -Seconds 15 # let PnP start the restored driver
    $problems = @(Test-Removed $baseline)
    Unregister-Task $ResumeTask
    $history = Join-Path $Root 'history'
    New-Item -ItemType Directory -Path $history -Force | Out-Null
    $stamp = (Get-Date).ToString('yyyyMMddTHHmmss')
    Move-Item -LiteralPath $BaselinePath -Destination (Join-Path $history "baseline-$stamp.json")
    foreach ($d in $BackupDir, $BundleDir, $TrashDir) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force } }
    if ($problems.Count) {
        Set-State @{ status = 'Failed'; step = 'VerifyRemoval'; last_error = ($problems -join '; ') }
        throw ('Uninstall verification failed: ' + ($problems -join '; '))
    }
    Set-State @{ status = 'Removed'; step = $null; last_error = $null; removed = (Get-Date).ToString('o');
        installed_inf = $null; bridge_hashes = $null; loader_installed = $null; native_d3d12_x64 = $null; native_d3d12_x86 = $null }
    Write-Journal 'removed; the machine matches its baseline'
}

# -------------------------------------------------------------------- main

if ($Mode -eq 'Status') {
    $s = Get-State
    if (-not $s.Count) { $s = [ordered]@{ status = 'NotInstalled' } }
    ConvertTo-Json -InputObject $s -Depth 8
    return
}
if (-not [Environment]::Is64BitProcess) { throw 'Run this script in 64-bit PowerShell (Install.cmd does).' }
if (-not (Test-Admin)) { throw 'Run this from an elevated (administrator) prompt.' }

New-Item -ItemType Directory -Path $Root -Force | Out-Null
$mutex = New-Object Threading.Mutex($false, 'Global\vmlab-win3d-setup')
[void]$mutex.WaitOne()
try {
    $state = Get-State
    $status = if ($state.Contains('status')) { $state['status'] } else { 'NotInstalled' }
    switch ($Mode) {
        'Install' {
            Write-Journal "Install from $PSScriptRoot (state: $status)"
            if ($status -eq 'Uninstalling') { throw 'An uninstall is waiting for its reboot. Restart Windows, wait for it to finish, then install.' }
            $sb = Get-SecureBoot
            if ($sb -eq $true) {
                throw 'Secure Boot is on. Windows ignores test signing while Secure Boot is on, so the driver cannot load. Turn Secure Boot off (vmlab: secure_boot = false) and run Install.cmd again.'
            }
            $bundle = Get-Bundle $PSScriptRoot
            $device = Get-Device $bundle.HardwareId
            if (-not $device) { throw "No display device with hardware ID $($bundle.HardwareId). The VM needs a virtio-gpu GL device (docs/install.md)." }
            Copy-BundleToRoot $PSScriptRoot
            $bundle = Get-Bundle $BundleDir
            if (-not (Test-Path -LiteralPath $BaselinePath)) { New-Baseline $bundle $device | Out-Null }
            Set-State @{ status = 'Installing'; operation = 'Install'; step = 'Prepare'; version = $bundle.Version;
                bundle_sha256 = $bundle.Hash; last_error = $null }
            Invoke-Install $bundle 'Prepare'
        }
        'Uninstall' {
            Write-Journal "Uninstall (state: $status)"
            if ($status -in 'NotInstalled', 'Removed') { Write-Journal 'nothing is installed'; break }
            Set-State @{ operation = 'Uninstall' }
            Invoke-Uninstall 'Remove'
        }
        'Resume' {
            $step = $state['step']
            if ($state.Contains('boot_id') -and $state['boot_id'] -eq (Get-BootId)) {
                Write-Journal "resume: still in the boot that requested the restart; waiting for it"
                break
            }
            Write-Journal "resume: status $status, step $step"
            if ($status -eq 'Uninstalling') { Invoke-Uninstall $step }
            elseif ($status -eq 'RebootPending') { Invoke-Install (Get-Bundle $BundleDir) $step }
            else { Unregister-Task $ResumeTask }
        }
        'Repair' {
            if ($status -ne 'Installed') { break }
            $bundle = Get-Bundle $BundleDir
            $baseline = Read-Json $BaselinePath
            foreach ($a in $Arches) {
                if (Install-Bridge $bundle $baseline $a) { Write-Journal "repair: re-applied the D3D12 bridge for $($a.Name)" }
            }
            Clear-Trash
        }
    }
} catch {
    $msg = $_.Exception.Message
    Write-Journal "ERROR: $msg"
    Write-Journal ($_.ScriptStackTrace -replace "`r?`n", ' | ')
    if (Test-Path -LiteralPath $Root) {
        $s = Get-State
        $keep = @{ last_error = $msg }
        if (-not $s.Contains('status') -or $s['status'] -notin 'Installed', 'Removed') { $keep['status'] = 'Failed' }
        Set-State $keep
    }
    if ($Mode -eq 'Resume') { Unregister-Task $ResumeTask }
    [Console]::Error.WriteLine("vmlab-win3d: $msg")
    exit 1
} finally {
    $mutex.ReleaseMutex()
}
