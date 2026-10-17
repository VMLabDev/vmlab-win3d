<#
.SYNOPSIS
Runs vulkaninfo --summary (System32 and SysWOW64) in the logged-on user's
session and writes vulkaninfo-x64.txt and vulkaninfo-x86.txt to -OutDir.

.DESCRIPTION
Vulkan enumerates no device from SYSTEM (session 0), so this goes through
tests/guest/Run-Probe.ps1, which starts a program in the console user's
session. vulkaninfo is a console program, so it runs under cmd.exe with its
output redirected to a file.
#>
[CmdletBinding()]
param(
    [string]$GuestDir = 'C:\win3d\guest',
    [string]$OutDir = 'C:\win3d\results\vulkaninfo'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$cmd = Join-Path $OutDir 'vulkaninfo.cmd'
@"
@echo off
"%SystemRoot%\System32\vulkaninfo.exe" --summary > "$OutDir\vulkaninfo-x64.txt" 2>&1
echo exit %ERRORLEVEL% >> "$OutDir\vulkaninfo-x64.txt"
"%SystemRoot%\SysWOW64\vulkaninfo.exe" --summary > "$OutDir\vulkaninfo-x86.txt" 2>&1
echo exit %ERRORLEVEL% >> "$OutDir\vulkaninfo-x86.txt"
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
& (Join-Path $GuestDir 'Run-Probe.ps1') -Path "$env:SystemRoot\System32\cmd.exe" -ArgumentList '/c', $cmd `
    -Raw -NoModules -Name vulkaninfo -OutDir $OutDir -Seconds 60
foreach ($a in 'x64', 'x86') {
    $f = Join-Path $OutDir "vulkaninfo-$a.txt"
    "## $f"
    if (Test-Path $f) { Get-Content $f } else { '(missing)' }
}
# A crash leaves an Application Error (event 1000) naming the faulting module.
"## Application Error events for vulkaninfo.exe"
Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = (Get-Date).AddMinutes(-10) } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'vulkaninfo' } | ForEach-Object { $_.TimeCreated.ToString('o'); $_.Message; '' } |
    Tee-Object -FilePath (Join-Path $OutDir 'vulkaninfo-crashes.txt')
