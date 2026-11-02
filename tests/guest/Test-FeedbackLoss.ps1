<#
.SYNOPSIS
Checks live and revoked graphics-feedback mappings in the production Mesa code.
Red requires an access violation from each revoked mapping. Green requires
device-lost propagation without terminating the process.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][ValidateSet('Red', 'Green')][string]$Phase,
    [Parameter(Mandatory = $true)][string]$OutDir
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$results = @()
foreach ($arch in 'x64', 'x86') {
    $binary = (Resolve-Path -LiteralPath (Join-Path $Root "$arch\feedback_loss_probe.exe")).ProviderPath
    foreach ($mode in 'control', 'read-revoked', 'write-revoked', 'read-unmapped', 'write-unmapped') {
        $start = New-Object System.Diagnostics.ProcessStartInfo
        $start.FileName = $binary
        $start.Arguments = "--$mode"
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = New-Object System.Diagnostics.Process
        $process.StartInfo = $start
        [void]$process.Start()
        $finished = $process.WaitForExit(30000)
        if (-not $finished) {
            $process.Kill()
            $process.WaitForExit()
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $exitCode = $process.ExitCode
        $exitHex = '0x{0:x8}' -f $exitCode
        $process.Dispose()
        $stdout | Set-Content -LiteralPath (Join-Path $OutDir "$arch-$mode.stdout") -Encoding UTF8
        $stderr | Set-Content -LiteralPath (Join-Path $OutDir "$arch-$mode.stderr") -Encoding UTF8
        $records = @($stdout -split '\r?\n' | Where-Object { $_.StartsWith('{') } | ForEach-Object { $_ | ConvertFrom-Json })
        $final = $records | Where-Object { $_.PSObject.Properties.Name -contains 'vk_result' } | Select-Object -Last 1
        $startedAccess = @($records | Where-Object { $_.PSObject.Properties.Name -contains 'phase' -and $_.phase -eq 'access' }).Count -gt 0
        $expectCrash = $Phase -eq 'Red' -and $mode -ne 'control'
        if ($expectCrash) {
            $passed = $finished -and $startedAccess -and $exitHex -eq '0xc0000005' -and $null -eq $final
        } else {
            $passed = $finished -and $exitCode -eq 0 -and $null -ne $final -and $final.result -eq 'pass'
            if ($mode -ne 'control' -and $null -ne $final) {
                $passed = $passed -and $final.vk_result -eq -4 -and $final.backend_failed -and
                    $final.reset_callbacks -eq 1 -and $final.reported_reset -eq $final.reset_status
            }
        }
        $results += [pscustomobject][ordered]@{
            arch = $arch
            mode = $mode
            finished = $finished
            exit_code = $exitCode
            exit_hex = $exitHex
            expected_access_violation = $expectCrash
            passed = [bool]$passed
            report = $final
        }
    }
}
$failed = @($results | Where-Object { -not $_.passed }).Count
$summary = [ordered]@{
    schema = 1
    phase = $Phase
    powershell = $PSVersionTable.PSVersion.ToString()
    checks = $results
    failed = $failed
}
$json = ConvertTo-Json -InputObject $summary -Depth 8
$json | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json') -Encoding UTF8
Write-Output $json
if ($results.Count -ne 10 -or $failed) { exit 1 }
exit 0
