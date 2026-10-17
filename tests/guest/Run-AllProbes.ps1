<#
.SYNOPSIS
Runs every probe under a probes directory through Run-Probe.ps1 and writes a
one-line-per-run summary.

.DESCRIPTION
Runs <ProbeDir>\x64\*_probe.exe and <ProbeDir>\x86\*_probe.exe one at a time,
each in the interactive user's session. With -Warp, the D3D11 and D3D12 probes
also run a second time with --warp. Results go to OutDir as Run-Probe.ps1
writes them, plus summary.json:

  [{ name, exit_code, result, adapter, software, readback_ok, avg_fps,
     frames, first_error, graphics_modules }]

Prints the summary as a table. Exit code 0 when every run passed, else 1; a
failure that is expected (Vulkan with no ICD) still counts, so read the
summary.

.EXAMPLE
Run-AllProbes.ps1 -ProbeDir C:\win3d\probes -OutDir C:\win3d\results -Seconds 15 -Warp
#>
[CmdletBinding()]
param(
    [string]$ProbeDir = 'C:\win3d\probes',
    [string]$OutDir = 'C:\win3d\results',
    [int]$Seconds = 15,
    [switch]$Warp,
    [string[]]$Arch = @('x64', 'x86')
)
$ErrorActionPreference = 'Stop'
$runProbe = Join-Path $PSScriptRoot 'Run-Probe.ps1'

$runs = @()
foreach ($a in $Arch) {
    foreach ($exe in @(Get-ChildItem -Path (Join-Path $ProbeDir $a) -Filter '*_probe.exe' | Sort-Object Name)) {
        $base = "$a-$($exe.BaseName)"
        $runs += , @($exe.FullName, $base, @())
        if ($Warp -and $exe.BaseName -in @('d3d11_probe', 'd3d12_probe')) {
            $runs += , @($exe.FullName, "$base-warp", @('--warp'))
        }
    }
}

$summary = foreach ($r in $runs) {
    $null = & $runProbe -Path $r[0] -Name $r[1] -ArgumentList $r[2] -Seconds $Seconds -OutDir $OutDir
    $code = $LASTEXITCODE
    $run = Get-Content -LiteralPath (Join-Path $OutDir "$($r[1]).run.json") -Raw | ConvertFrom-Json
    $probe = $null
    $json = Join-Path $OutDir "$($r[1]).json"
    if (Test-Path -LiteralPath $json) { $probe = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json }
    [pscustomobject][ordered]@{
        name             = $r[1]
        exit_code        = $code
        result           = $(if ($probe) { $probe.result } else { $null })
        adapter          = $(if ($probe -and $probe.adapter.PSObject.Properties['name']) { $probe.adapter.name } else { $null })
        software         = $(if ($probe -and $probe.adapter.PSObject.Properties['software']) { $probe.adapter.software } else { $null })
        readback_ok      = $(if ($probe) { $probe.readback.ok } else { $null })
        avg_fps          = $(if ($probe) { $probe.avg_fps } else { $null })
        frames           = $(if ($probe) { $probe.frames_presented } else { $null })
        session_id       = $run.session_id
        first_error      = $(if ($probe -and @($probe.errors).Count) { @($probe.errors)[0] } else { $null })
        graphics_modules = $run.graphics_modules
    }
}
$summary = @($summary)
ConvertTo-Json -InputObject $summary -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir 'summary.json') -Encoding UTF8
$summary | Format-Table name, exit_code, result, adapter, software, readback_ok, avg_fps, first_error -AutoSize |
    Out-String -Width 300 | Write-Output
if (@($summary | Where-Object { $_.exit_code -ne 0 }).Count) { exit 1 }
exit 0
