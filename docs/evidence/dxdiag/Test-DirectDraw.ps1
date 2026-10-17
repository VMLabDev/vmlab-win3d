<#
.SYNOPSIS
Collects the DirectDraw evidence for docs/dxdiag.md in a guest with the
package installed.

.DESCRIPTION
Runs, in the interactive user's session (through tests\guest\Run-Probe.ps1):
ddcaps.exe x64 and x86 (HAL caps and a video-memory blit test, with a
window on the console for -Seconds), then dxdiag through Get-Dxdiag.ps1.
With -Trace, the D3D9 UMD trace (D3D10UMD_D3D9_TRACE in
C:\ProgramData\Yttrium\yttrium.ini) is on for the run, so every GetCaps
call DirectDraw and dxdiag make is logged to
C:\ProgramData\Yttrium\yttrium-user.log, which is copied to OutDir.

Expects ddcaps in C:\win3d\ddcaps\{x64,x86} and the guest scripts in
C:\win3d\guest.
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'C:\win3d\results\dxdiag',
    [int]$Seconds = 10,
    [switch]$Trace
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$ini = 'C:\ProgramData\Yttrium\yttrium.ini'
$log = 'C:\ProgramData\Yttrium\yttrium-user.log'
if ($Trace) {
    New-Item -ItemType Directory -Force (Split-Path $ini) | Out-Null
    Set-Content -LiteralPath $ini -Value 'D3D10UMD_D3D9_TRACE=1' -Encoding ASCII
    Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
}
try {
    foreach ($arch in 'x64', 'x86') {
        & C:\win3d\guest\Run-Probe.ps1 -Path "C:\win3d\ddcaps\$arch\ddcaps.exe" -Seconds $Seconds `
            -OutDir $OutDir -Name "ddcaps-$arch" | Out-Null
    }
    & C:\win3d\guest\Get-Dxdiag.ps1 -OutDir $OutDir -AsUser | Out-Null
} finally {
    if ($Trace) {
        Remove-Item -LiteralPath $ini -ErrorAction SilentlyContinue
        if (Test-Path $log) { Copy-Item $log (Join-Path $OutDir 'yttrium-user.log') }
    }
}
$summary = [ordered]@{}
foreach ($arch in 'x64', 'x86') {
    $f = Join-Path $OutDir "ddcaps-$arch.json"
    if (Test-Path $f) {
        $j = Get-Content $f -Raw | ConvertFrom-Json
        $summary["ddcaps_$arch"] = [ordered]@{ pass = $j.pass; hal_present = $j.hal_present;
            nonlocal_vidmem = $j.nonlocal_vidmem; hal_caps = $j.hal.dwCaps; hal_caps2 = $j.hal.dwCaps2 }
    }
}
$dx = Get-Content (Join-Path $OutDir 'dxdiag.json') -Raw | ConvertFrom-Json
$summary.dxdiag = $dx.summary
$summary | ConvertTo-Json -Depth 4
