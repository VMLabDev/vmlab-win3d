<#
.SYNOPSIS
Forces a guest virtio-gpu TDR, proves both KMD recovery callbacks ran, and
checks hardware rendering, the boot identity, and new Windows crash reports.

.DESCRIPTION
Run elevated in a disposable Windows VM with an interactive console user.
TdrProbe selects only PCI 1AF4:1050 and temporarily enables the documented
TdrTestMode test escape, restoring the previous registry value afterwards.
RenderProbe is one of the existing graphical probes, run through Run-Probe.ps1.
Writes tdr-recovery.json and the probe evidence to OutDir. Exit 0 requires
positive reset and restart telemetry, hardware pixel readback before and after,
an unchanged boot time, and no new BlueScreen or BugCheck event records.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$TdrProbe,
    [Parameter(Mandatory = $true)][string]$RenderProbe,
    [string]$RunProbe = (Join-Path $PSScriptRoot 'Run-Probe.ps1'),
    [string]$OutDir = 'C:\win3d\tdr-results',
    [ValidateRange(1, 120)][int]$Seconds = 5,
    [ValidateRange(1, 120)][int]$RecoveryTimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Get-LastRecord([string]$Log) {
    [long](Get-WinEvent -LogName $Log -MaxEvents 1).RecordId
}

function Get-NewCrashes([string]$Log, [long]$After) {
    $query = "*[System[(EventID=1001) and (EventRecordID > $After)]]"
    try {
        $events = @(Get-WinEvent -LogName $Log -FilterXPath $query -ErrorAction Stop)
    } catch {
        if ($_.FullyQualifiedErrorId -like 'NoMatchingEventsFound*') { return }
        throw
    }
    foreach ($event in $events) {
        $crash = $Log -eq 'System' -and $event.ProviderName -in @('Microsoft-Windows-WER-SystemErrorReporting', 'BugCheck')
        if ($Log -eq 'Application' -and $event.ProviderName -eq 'Windows Error Reporting') {
            [xml]$xml = $event.ToXml()
            foreach ($data in $xml.SelectNodes('//*[local-name()="EventData"]/*[local-name()="Data"]')) {
                if ($data.GetAttribute('Name') -eq 'EventName' -and $data.InnerText -eq 'BlueScreen') { $crash = $true }
            }
        }
        if ($crash) {
            [pscustomobject][ordered]@{
                log = $Log
                record_id = $event.RecordId
                provider = $event.ProviderName
                utc = $event.TimeCreated.ToUniversalTime().ToString('o')
                message = $event.Message
            }
        }
    }
}

function Invoke-Rendering([string]$Name) {
    $null = & $RunProbe -Path $RenderProbe -Seconds $Seconds -OutDir $OutDir -Name $Name
    $code = $LASTEXITCODE
    $path = Join-Path $OutDir "$Name.json"
    if ($code -ne 0 -or -not (Test-Path -LiteralPath $path)) { throw "rendering $Name failed with exit $code" }
    $probe = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    if ($probe.result -ne 'pass' -or $probe.adapter.software -or -not $probe.readback.ok -or $probe.frames_presented -lt 1) {
        throw "rendering $Name did not prove hardware pixel readback"
    }
    return $probe
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).ProviderPath
$TdrProbe = (Resolve-Path -LiteralPath $TdrProbe).ProviderPath
$RenderProbe = (Resolve-Path -LiteralPath $RenderProbe).ProviderPath
$RunProbe = (Resolve-Path -LiteralPath $RunProbe).ProviderPath
$summary = [ordered]@{
    schema = 1
    result = 'fail'
    force_exit_code = $null
    force = $null
    render_before = $null
    render_after = $null
    boot_before = $null
    boot_after = $null
    event_baselines = [ordered]@{}
    new_crashes = @()
    errors = @()
}
try {
    $summary.boot_before = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
    foreach ($log in 'System', 'Application') { $summary.event_baselines[$log] = Get-LastRecord $log }
    $summary.render_before = Invoke-Rendering 'tdr-render-before'
    $forcePath = Join-Path $OutDir 'tdr-request.json'
    $forceOutput = (& $TdrProbe --force-tdr --wait-ms ($RecoveryTimeoutSeconds * 1000) --out $forcePath | Out-String)
    $summary.force_exit_code = $LASTEXITCODE
    $forceOutput | Set-Content -LiteralPath (Join-Path $OutDir 'tdr-request.stdout.json') -Encoding UTF8
    if (-not (Test-Path -LiteralPath $forcePath)) { throw 'forced-TDR probe did not write its result' }
    $summary.force = Get-Content -LiteralPath $forcePath -Raw | ConvertFrom-Json
    $force = $summary.force
    if ($summary.force_exit_code -ne 0 -or $force.result -ne 'request_accepted' -or -not $force.request_completed) {
        throw "forced-TDR request failed with exit $($summary.force_exit_code)"
    }
    if ($force.adapter.vendor_id -ne '0x1af4' -or $force.adapter.device_id -ne '0x1050') { throw 'forced-TDR target was not virtio-gpu' }
    if (-not $force.recovery_observed -or $null -eq $force.reset_before -or $null -eq $force.restart_before -or
        $force.reset_after -le $force.reset_before -or $force.restart_after -le $force.restart_before -or
        $force.last_reset_status -ne '0x00000000' -or $force.last_restart_status -ne '0x00000000' -or $force.recovery_state -ne 0) {
        throw 'reset and restart callbacks were not both observed completing successfully'
    }
    if ($force.test_mode_restore_status -ne 0) { throw 'forced-TDR probe did not restore TdrTestMode' }
    $summary.render_after = Invoke-Rendering 'tdr-render-after'
} catch {
    $summary.errors += $_.Exception.Message
} finally {
    try {
        $summary.boot_after = (Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
        if ($summary.boot_before -ne $summary.boot_after) { $summary.errors += 'Windows rebooted during the TDR test' }
        foreach ($log in 'System', 'Application') {
            if ($summary.event_baselines.Contains($log)) {
                if ((Get-LastRecord $log) -lt $summary.event_baselines[$log]) { throw "$log event log was cleared during the test" }
                $summary.new_crashes += @(Get-NewCrashes $log $summary.event_baselines[$log])
            }
        }
        if ($summary.new_crashes.Count) { $summary.errors += 'Windows recorded new BlueScreen or BugCheck reports' }
    } catch {
        $summary.errors += $_.Exception.Message
    }
    if (-not $summary.errors.Count) { $summary.result = 'pass' }
    $json = ConvertTo-Json -InputObject $summary -Depth 12
    $json | Set-Content -LiteralPath (Join-Path $OutDir 'tdr-recovery.json') -Encoding UTF8
    Write-Output $json
}
if ($summary.result -ne 'pass') { exit 1 }
exit 0
