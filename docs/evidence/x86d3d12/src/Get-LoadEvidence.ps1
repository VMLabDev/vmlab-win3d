<#
.SYNOPSIS
Collects what can hold an image load in a fresh guest: the Smart App Control
(Code Integrity "verified and reputable") state, Code Integrity and Defender
events, and the Defender settings. Run as SYSTEM after the D3D12 probes.
#>
param([int]$Minutes = 90)
$since = (Get-Date).AddMinutes(-$Minutes)

"date {0}" -f (Get-Date -Format o)
$ci = 'HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy'
$p = Get-ItemProperty $ci -ErrorAction SilentlyContinue
"smart app control VerifiedAndReputablePolicyState={0} (0 off, 1 on, 2 evaluation)" -f $p.VerifiedAndReputablePolicyState
$m = Get-MpPreference
"defender MAPSReporting={0} SubmitSamplesConsent={1} CloudBlockLevel={2} CloudExtendedTimeout={3}" -f `
    $m.MAPSReporting, $m.SubmitSamplesConsent, $m.CloudBlockLevel, $m.CloudExtendedTimeout

foreach ($log in 'Microsoft-Windows-CodeIntegrity/Operational', 'Microsoft-Windows-Windows Defender/Operational',
                 'Microsoft-Windows-AppLocker/EXE and DLL', 'Microsoft-Windows-SmartScreen/Debug') {
    "== $log"
    try {
        Get-WinEvent -FilterHashtable @{ LogName = $log; StartTime = $since } -ErrorAction Stop |
            Sort-Object TimeCreated | ForEach-Object {
                $msg = ($_.Message -replace '\s+', ' ')
                if ($msg.Length -gt 400) { $msg = $msg.Substring(0, 400) }
                "{0:o} id={1} {2}" -f $_.TimeCreated, $_.Id, $msg
            }
    } catch { "  ($($_.Exception.Message))" }
}
