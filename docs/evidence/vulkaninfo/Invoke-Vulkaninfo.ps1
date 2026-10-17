<#
.SYNOPSIS
Runs the installed vulkaninfo.exe (System32 and SysWOW64), with --summary and
with no arguments, in the logged-on user's session. Writes
vulkaninfo-<arch>-summary.txt and vulkaninfo-<arch>-full.txt to -OutDir, each
ending in an "exit <code>" line, plus files.txt (hash and version of each
vulkaninfo.exe and vulkan-1.dll) and crashes.txt (Application Error events).

.DESCRIPTION
Vulkan enumerates no device from SYSTEM (session 0), so the runs go through
tests/guest/Run-Probe.ps1, which starts cmd.exe in the console user's session.
#>
[CmdletBinding()]
param(
    [string]$GuestDir = 'C:\win3d\guest',
    [string]$OutDir = 'C:\win3d\results\vulkaninfo'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$start = Get-Date
$lines = @('@echo off')
foreach ($a in @(@('x64', 'System32'), @('x86', 'SysWOW64'))) {
    $exe = "%SystemRoot%\$($a[1])\vulkaninfo.exe"
    foreach ($m in @(@('summary', '--summary'), @('full', ''))) {
        $f = "$OutDir\vulkaninfo-$($a[0])-$($m[0]).txt"
        $lines += "`"$exe`" $($m[1]) > `"$f`" 2>&1"
        $lines += "echo exit %ERRORLEVEL% >> `"$f`""
    }
}
$cmd = Join-Path $OutDir 'vulkaninfo.cmd'
$lines | Set-Content -LiteralPath $cmd -Encoding ascii
& (Join-Path $GuestDir 'Run-Probe.ps1') -Path "$env:SystemRoot\System32\cmd.exe" -ArgumentList '/c', $cmd `
    -Raw -NoModules -Name vulkaninfo -OutDir $OutDir -Seconds 120
$files = foreach ($d in 'System32', 'SysWOW64') {
    foreach ($n in 'vulkaninfo.exe', 'vulkan-1.dll') {
        $p = Join-Path "$env:SystemRoot\$d" $n
        $v = (Get-Item $p).VersionInfo.FileVersion
        '{0}  {1}  {2}' -f (Get-FileHash -Algorithm SHA256 $p).Hash.ToLower(), $v, $p
    }
}
$files | Tee-Object -FilePath (Join-Path $OutDir 'files.txt')
Get-WinEvent -FilterHashtable @{ LogName = 'Application'; Id = 1000; StartTime = $start } -ErrorAction SilentlyContinue |
    Where-Object { $_.Message -match 'vulkaninfo' } | ForEach-Object { $_.TimeCreated.ToString('o'); $_.Message; '' } |
    Out-File -Encoding ascii (Join-Path $OutDir 'crashes.txt')
"crash events: $((Get-Content (Join-Path $OutDir 'crashes.txt') | Measure-Object -Line).Lines) lines"
