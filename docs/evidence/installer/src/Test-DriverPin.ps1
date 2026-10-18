<#
.SYNOPSIS
Guest half of the driver-pinning repro (docs/installer.md, "Evidence").
Runs as SYSTEM through vmlab exec, in 64-bit Windows PowerShell 5.1.

.DESCRIPTION
  -Action Report -Label L   Print one JSON object: the display device's
                            driver, the device installation policy, the
                            viogpudo and viogpu3d packages in the driver
                            store, the installer's state, and pnputil's
                            ranked driver list for the device.
  -Action Source            Print the driver store directory of the
                            template's viogpudo package and its files.
  -Action Stage -Inf P      pnputil /add-driver P /install, as Windows Update
                            does when it delivers a driver: stage it and
                            install it on every device it ranks best for.
  -Action Scan              pnputil /scan-devices, then UsoClient StartScan
                            (Windows Update's scan, drivers included), then
                            wait -Seconds for anything to change the driver.
#>
param(
    [ValidateSet('Report', 'Source', 'Stage', 'Scan')]
    [string]$Action = 'Report',
    [string]$Label = '',
    [string]$Inf,
    [int]$Seconds = 180
)
$ErrorActionPreference = 'Stop'
$HardwareId = 'PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01'

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

function Get-Policy {
    $k = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions'
    $r = Get-Item -LiteralPath $k -ErrorAction SilentlyContinue
    $l = Get-Item -LiteralPath "$k\DenyInstanceIDs" -ErrorAction SilentlyContinue
    [ordered]@{
        device_install_key = Test-Path -LiteralPath 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall'
        restrictions_key = [bool]$r
        values = $(if ($r) { $o = [ordered]@{}; foreach ($n in $r.GetValueNames()) { $o[$n] = $r.GetValue($n) }; $o } else { $null })
        deny_instance_ids = $(if ($l) { @($l.GetValueNames() | ForEach-Object { '{0}={1}' -f $_, $l.GetValue($_) }) } else { $null })
    }
}

function Invoke-Pnputil([string[]]$Arguments) {
    $ErrorActionPreference = 'Continue'
    $out = & pnputil.exe @Arguments 2>&1 | ForEach-Object { "$_" } | Out-String
    [pscustomobject]@{ args = ($Arguments -join ' '); exit = $LASTEXITCODE; output = $out.Trim() }
}

$gpu = Get-Gpu
switch ($Action) {
    'Report' {
        $state = 'C:\ProgramData\vmlab-win3d\state.json'
        [ordered]@{
            label = $Label
            time = (Get-Date).ToString('o')
            instance_id = $gpu.InstanceId
            bound = Get-Bound $gpu.InstanceId
            policy = Get-Policy
            store = @(Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match '\\(viogpudo|viogpu3d)\.inf$' } | ForEach-Object {
                    '{0} {1} {2} {3}' -f $_.Driver, (Split-Path -Leaf $_.OriginalFileName), $_.Date.ToString('yyyy-MM-dd'), $_.Version })
            installer = $(if (Test-Path $state) { $s = Get-Content $state -Raw | ConvertFrom-Json; '{0} {1}' -f $s.status, $s.last_error } else { 'NotInstalled' })
            ranks = (Invoke-Pnputil @('/enum-devices', '/instanceid', $gpu.InstanceId, '/drivers')).output -split "`r?`n" | Where-Object { $_.Trim() }
        } | ConvertTo-Json -Depth 6
    }
    'Source' {
        $d = @(Get-WindowsDriver -Online | Where-Object { $_.OriginalFileName -match '\\viogpudo\.inf$' } | Sort-Object Date)
        if (-not $d.Count) { throw 'no viogpudo package in the driver store' }
        $dir = Split-Path -Parent $d[0].OriginalFileName
        $dir
        Get-ChildItem -LiteralPath $dir -File | ForEach-Object { $_.Name }
    }
    'Stage' {
        $before = Get-Bound $gpu.InstanceId
        $r = Invoke-Pnputil @('/add-driver', $Inf, '/install')
        [ordered]@{ action = "stage $Inf"; pnputil = $r; before = $before; after = (Get-Bound $gpu.InstanceId) } | ConvertTo-Json -Depth 6
    }
    'Scan' {
        $before = Get-Bound $gpu.InstanceId
        $scan = Invoke-Pnputil @('/scan-devices')
        $ErrorActionPreference = 'Continue'
        & "$env:SystemRoot\System32\UsoClient.exe" StartScan 2>&1 | Out-Null
        $uso = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $changes = @()
        $deadline = (Get-Date).AddSeconds($Seconds)
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Seconds 10
            $now = Get-Bound $gpu.InstanceId
            if ($now.inf -ne $before.inf) { $changes += ('{0} {1}' -f (Get-Date).ToString('T'), $now.inf) }
        }
        [ordered]@{ action = 'scan'; pnputil = $scan; usoclient_exit = $uso; seconds = $Seconds
            wu_events = @(Get-WinEvent -LogName 'Microsoft-Windows-WindowsUpdateClient/Operational' -MaxEvents 5 -ErrorAction SilentlyContinue |
                ForEach-Object { '{0} {1} {2}' -f $_.TimeCreated.ToString('s'), $_.Id, ($_.Message -split "`r?`n")[0] })
            before = $before; after = (Get-Bound $gpu.InstanceId); changes = $changes } | ConvertTo-Json -Depth 6
    }
}
