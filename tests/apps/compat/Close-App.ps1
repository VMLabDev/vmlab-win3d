<#
.SYNOPSIS
Asks a program in the interactive user's session to close, the way clicking
its window's close button does (WM_CLOSE to its main window).

.DESCRIPTION
Window messages cannot cross from SYSTEM's session 0 to the user's desktop,
and Alt+F4 on the console goes to whichever window has the focus (with none,
it opens the Shut Down Windows dialog). This script, run as SYSTEM through
`vmlab exec`, starts a one-off scheduled task as the logged-on user that
calls Process.CloseMainWindow() on every process whose path is -Path, and
prints what it found. It does not wait for the program to exit.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string]$User
)
$ErrorActionPreference = 'Stop'
if (-not $User) {
    $User = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $User) { throw 'nobody is logged on to the console' }
}
$out = Join-Path $env:ProgramData ('win3d-close-' + [guid]::NewGuid().ToString('N').Substring(0, 8) + '.txt')
New-Item -ItemType File -Path $out -Force | Out-Null
& icacls.exe $out /grant "${User}:M" /Q | Out-Null
$cmd = "`$r = foreach (`$p in Get-Process | Where-Object { `$_.Path -eq '$($Path.Replace("'", "''"))' }) { '{0} window=0x{1:X} title=[{2}] closed={3}' -f `$p.Id, [int64]`$p.MainWindowHandle, `$p.MainWindowTitle, `$p.CloseMainWindow() }; if (-not `$r) { `$r = 'no such process' }; Set-Content -LiteralPath '$out' -Value `$r"
$enc = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($cmd))
$task = 'win3d-close-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand $enc"
$principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
Register-ScheduledTask -TaskName $task -Action $action -Principal $principal | Out-Null
try {
    Start-ScheduledTask -TaskName $task
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline -and -not (Get-Content -LiteralPath $out -Raw)) { Start-Sleep -Milliseconds 250 }
    $r = Get-Content -LiteralPath $out -Raw
    if ($r) { $r.Trim() } else { 'close task gave no answer in 30 s' }
} finally {
    Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
}
