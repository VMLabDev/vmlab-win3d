<#
.SYNOPSIS
Guest half of the Windows Update driver test (docs/installer.md, "Windows
Update"). Runs as SYSTEM through vmlab exec, in 64-bit Windows PowerShell 5.1.
Every action prints one JSON object.

.DESCRIPTION
  -Action Report -Label L   The display device's driver and rank, the device
                            installation policy, the Windows Update services
                            and policies, and pnputil's ranked driver list.
  -Action Enable            Turn Windows Update back on in this VM only: drop
                            the template's DoNotConnectToWindowsUpdateInternet
                            Locations and NoAutoUpdate policies, set UsoSvc
                            (disabled by the template) and wuauserv to manual
                            and start them. Prints what it changed.
  -Action Search            Windows Update scan through the
                            Microsoft.Update.Session COM API against Windows
                            Update itself (ServerSelection 2), driver updates
                            included: every update not installed, with the
                            driver fields of each.
  -Action Install           Search, then download and install every driver
                            update offered, as Windows Update does. Prints the
                            result code and HRESULT of each.
  -Action Uso               UsoClient StartScan and StartInteractiveScan (the
                            orchestrator's own scans), StartDownload,
                            StartInstall and a made-up verb as a control;
                            waits -Seconds, then prints the
                            WindowsUpdateClient events since.
#>
param(
    [ValidateSet('Report', 'Enable', 'Search', 'Install', 'Uso')]
    [string]$Action = 'Report',
    [string]$Label = '',
    [int]$Seconds = 300
)
$ErrorActionPreference = 'Stop'
$HardwareId = 'PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01'
$WuPolicy = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate'

function Get-Gpu {
    foreach ($d in @(Get-PnpDevice -Class Display -PresentOnly)) {
        if (@((Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName DEVPKEY_Device_HardwareIds).Data) -contains $HardwareId) { return $d }
    }
    throw "no display device with $HardwareId"
}

function Get-Bound([string]$Id) {
    $p = @{}
    foreach ($k in 'DriverInfPath', 'DriverVersion', 'DriverDate', 'DriverProvider', 'ProblemCode', 'MatchingDeviceId', 'DriverRank') {
        $p[$k] = (Get-PnpDeviceProperty -InstanceId $Id -KeyName "DEVPKEY_Device_$k" -ErrorAction SilentlyContinue).Data
    }
    $store = @(Get-WindowsDriver -Online | Where-Object Driver -eq $p['DriverInfPath'])
    [ordered]@{
        inf = $p['DriverInfPath']
        original = $(if ($store.Count) { Split-Path -Leaf $store[0].OriginalFileName } else { $null })
        provider = $p['DriverProvider']
        date = $(if ($p['DriverDate']) { ([datetime]$p['DriverDate']).ToUniversalTime().ToString('yyyy-MM-dd') } else { $null })
        version = $p['DriverVersion']
        rank = $(if ($null -ne $p['DriverRank']) { '{0:X8}' -f [uint32]$p['DriverRank'] } else { $null })
        matching_id = $p['MatchingDeviceId']
        problem_code = $p['ProblemCode']
    }
}

function Get-Values([string]$Key) {
    $k = Get-Item -LiteralPath $Key -ErrorAction SilentlyContinue
    if (-not $k) { return $null }
    $o = [ordered]@{}
    foreach ($n in $k.GetValueNames()) { $o[$n] = $k.GetValue($n) }
    $o
}

function Get-Policy {
    $k = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions'
    $l = Get-Item -LiteralPath "$k\DenyInstanceIDs" -ErrorAction SilentlyContinue
    [ordered]@{
        restrictions = Get-Values $k
        deny_instance_ids = $(if ($l) { @($l.GetValueNames() | ForEach-Object { '{0}={1}' -f $_, $l.GetValue($_) }) } else { $null })
    }
}

function Get-WuState {
    [ordered]@{
        services = @(Get-Service wuauserv, UsoSvc, BITS, CryptSvc, TrustedInstaller, WaaSMedicSvc -ErrorAction SilentlyContinue |
            ForEach-Object { [ordered]@{ name = $_.Name; status = "$($_.Status)"; start = "$($_.StartType)" } })
        policy = Get-Values $WuPolicy
        policy_au = Get-Values "$WuPolicy\AU"
        driver_searching = Get-Values 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\DriverSearching'
        driver_searching_policy = Get-Values 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DriverSearching'
    }
}

function Get-Ranked([string]$Id) {
    $ErrorActionPreference = 'Continue'
    (& pnputil.exe /enum-devices /instanceid $Id /drivers 2>&1 | ForEach-Object { "$_" } | Out-String).Trim()
}

function ConvertTo-Update($u) {
    [ordered]@{
        title = $u.Title
        update_id = $u.Identity.UpdateID
        revision = $u.Identity.RevisionNumber
        type = $u.Type
        categories = @($u.Categories | ForEach-Object { $_.Name })
        driver_class = $u.DriverClass
        driver_hardware_id = $u.DriverHardwareID
        driver_manufacturer = $u.DriverManufacturer
        driver_model = $u.DriverModel
        driver_provider = $u.DriverProvider
        driver_ver_date = $(if ($u.DriverVerDate) { ([datetime]$u.DriverVerDate).ToString('yyyy-MM-dd') } else { $null })
        size = $u.MaxDownloadSize
        for_this_device = ($u.Type -eq 2 -and $u.DriverHardwareID -and $u.DriverHardwareID -like 'PCI\VEN_1AF4&DEV_1050*')
    }
}

function Invoke-Search {
    $s = New-Object -ComObject Microsoft.Update.Session
    $s.ClientApplicationID = 'vmlab-win3d wu test'
    $q = $s.CreateUpdateSearcher()
    $q.ServerSelection = 2   # ssWindowsUpdate
    $q.Online = $true
    $t0 = Get-Date
    $r = $q.Search("IsInstalled=0 and IsHidden=0")
    [pscustomobject]@{
        session = $s
        result = $r
        report = [ordered]@{
            criteria = 'IsInstalled=0 and IsHidden=0'
            server_selection = 'ssWindowsUpdate'
            result_code = $r.ResultCode
            seconds = [int]((Get-Date) - $t0).TotalSeconds
            warnings = @($r.Warnings | ForEach-Object { '0x{0:X8} {1}' -f $_.HResult, $_.Message })
            updates = @($r.Updates | ForEach-Object { ConvertTo-Update $_ })
        }
    }
}

$gpu = Get-Gpu
switch ($Action) {
    'Report' {
        $state = 'C:\ProgramData\vmlab-win3d\state.json'
        [ordered]@{
            label = $Label
            time = (Get-Date).ToString('o')
            instance_id = $gpu.InstanceId
            status = "$($gpu.Status)"
            bound = Get-Bound $gpu.InstanceId
            policy = Get-Policy
            installer_state = $(if (Test-Path $state) { (Get-Content -Raw $state | ConvertFrom-Json).status } else { $null })
            windows_update = Get-WuState
            ranked = Get-Ranked $gpu.InstanceId
        } | ConvertTo-Json -Depth 6
    }
    'Enable' {
        $before = Get-WuState
        foreach ($n in 'DoNotConnectToWindowsUpdateInternetLocations', 'DisableWindowsUpdateAccess', 'WUServer', 'WUStatusServer', 'ExcludeWUDriversInQualityUpdate') {
            Remove-ItemProperty -LiteralPath $WuPolicy -Name $n -ErrorAction SilentlyContinue
        }
        foreach ($n in 'NoAutoUpdate', 'AUOptions', 'UseWUServer') {
            Remove-ItemProperty -LiteralPath "$WuPolicy\AU" -Name $n -ErrorAction SilentlyContinue
        }
        foreach ($n in 'UsoSvc', 'wuauserv') {
            Set-Service -Name $n -StartupType Manual
            Start-Service -Name $n
        }
        [ordered]@{ label = $Label; time = (Get-Date).ToString('o'); before = $before; after = Get-WuState } | ConvertTo-Json -Depth 6
    }
    'Search' {
        $o = (Invoke-Search).report
        $o['label'] = $Label
        $o['bound'] = Get-Bound $gpu.InstanceId
        $o | ConvertTo-Json -Depth 6
    }
    'Install' {
        $sr = Invoke-Search
        $o = $sr.report
        $o['label'] = $Label
        $o['bound_before'] = Get-Bound $gpu.InstanceId
        $drivers = New-Object -ComObject Microsoft.Update.UpdateColl
        foreach ($u in $sr.result.Updates) { if ($u.Type -eq 2) { [void]$drivers.Add($u) } }
        $o['driver_updates'] = $drivers.Count
        $o['download'] = $null
        $o['install'] = $null
        if ($drivers.Count) {
            $d = $sr.session.CreateUpdateDownloader()
            $d.Updates = $drivers
            $dr = $d.Download()
            $o['download'] = [ordered]@{
                result_code = $dr.ResultCode
                hresult = '0x{0:X8}' -f $dr.HResult
                updates = @(for ($i = 0; $i -lt $drivers.Count; $i++) {
                    $x = $dr.GetUpdateResult($i)
                    [ordered]@{ title = $drivers.Item($i).Title; result_code = $x.ResultCode; hresult = '0x{0:X8}' -f $x.HResult; downloaded = $drivers.Item($i).IsDownloaded }
                })
            }
            $inst = $sr.session.CreateUpdateInstaller()
            $inst.Updates = $drivers
            $ir = $inst.Install()
            $o['install'] = [ordered]@{
                result_code = $ir.ResultCode
                hresult = '0x{0:X8}' -f $ir.HResult
                reboot_required = $ir.RebootRequired
                updates = @(for ($i = 0; $i -lt $drivers.Count; $i++) {
                    $x = $ir.GetUpdateResult($i)
                    [ordered]@{ title = $drivers.Item($i).Title; driver_hardware_id = $drivers.Item($i).DriverHardwareID; result_code = $x.ResultCode; hresult = '0x{0:X8}' -f $x.HResult }
                })
            }
        }
        $o['bound_after'] = Get-Bound $gpu.InstanceId
        $o['policy'] = Get-Policy
        $o | ConvertTo-Json -Depth 6
    }
    'Uso' {
        $t0 = Get-Date
        # Build 26100 accepts StartScan and StartInteractiveScan; StartDownload
        # and StartInstall exit 0x80070057, as the made-up verb does.
        $runs = foreach ($c in 'StartScan', 'StartInteractiveScan', 'StartDownload', 'StartInstall', 'NoSuchVerb') {
            $p = Start-Process -FilePath "$env:SystemRoot\System32\UsoClient.exe" -ArgumentList $c -Wait -PassThru
            [ordered]@{ command = "UsoClient $c"; exit = '0x{0:X8}' -f $p.ExitCode }
            if ($p.ExitCode -eq 0) { Start-Sleep -Seconds ([int]($Seconds / 2)) }
        }
        $ErrorActionPreference = 'Continue'
        $ev = @(Get-WinEvent -FilterHashtable @{ LogName = 'Microsoft-Windows-WindowsUpdateClient/Operational'; StartTime = $t0 } -ErrorAction SilentlyContinue |
            Sort-Object TimeCreated | ForEach-Object { '{0:o} {1} {2}' -f $_.TimeCreated, $_.Id, ($_.Message -replace '\s+', ' ') })
        [ordered]@{
            label = $Label
            runs = @($runs)
            events = $ev
            bound = Get-Bound $gpu.InstanceId
            policy = Get-Policy
        } | ConvertTo-Json -Depth 6
    }
}
