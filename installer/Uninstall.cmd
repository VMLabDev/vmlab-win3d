@echo off
rem vmlab-win3d: Uninstall. Run from an elevated command prompt (or as SYSTEM).
rem Progress goes to C:\ProgramData\vmlab-win3d\journal.log and state.json.
setlocal
fltmc >nul 2>&1 || (echo Uninstall.cmd must run elevated: right-click, Run as administrator. & exit /b 1)
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0vmlab-win3d.ps1" -Mode Uninstall %*
exit /b %ERRORLEVEL%
