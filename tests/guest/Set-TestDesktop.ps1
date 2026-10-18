<#
.SYNOPSIS
Creates a local test user and logs it on automatically at boot, so GPU tests
have an interactive desktop (session 1) to run in.

.DESCRIPTION
Vulkan does not enumerate devices from the SYSTEM session, and a probe window
is only visible on the console when it runs in the logged-on user's session.
This script prepares that session: it creates the user (a standard user unless
-Administrator), turns on Winlogon autologon for it, keeps the display from
blanking, and optionally reboots so the logon happens. Run it as SYSTEM or an
administrator. It is idempotent.

Before it writes the Winlogon values it waits, up to -OobeTimeoutSeconds, for
Windows' user OOBE to finish. On a fresh clone of a sysprep-generalized
template, user OOBE can still be running when vmlab reports the VM ready, and
when it exits it deletes AutoAdminLogon and DefaultUserName whoever wrote them
(vmlab handoff 20261018-092229). The check is the one vmlab gives there. It
prints one "oobe:" line saying how long it waited.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File Set-TestDesktop.ps1 -Reboot
#>
[CmdletBinding()]
param(
    [string]$UserName = 'win3d',
    [string]$Password = 'Win3d-probe-1!',
    [switch]$Administrator,
    [switch]$Reboot,
    [int]$OobeTimeoutSeconds = 900
)
$ErrorActionPreference = 'Stop'

function Test-OobeDone {
    $s = Get-ItemProperty HKLM:\SYSTEM\Setup
    $w = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
    $l = Select-String C:\Windows\Panther\UnattendGC\setupact.log `
        -Pattern 'fEnableAutologon = 1|Clearing Auto-logon values' -ErrorAction SilentlyContinue |
        Select-Object -Last 1
    -not ($s.SystemSetupInProgress -ne 0 -or $s.OOBEInProgress -ne 0 -or $w.SystemAutoLogon -or
        ($l -and $l.Line -notmatch 'Clearing'))
}
$oobeStart = Get-Date
while (-not (Test-OobeDone)) {
    if (((Get-Date) - $oobeStart).TotalSeconds -ge $OobeTimeoutSeconds) { break }
    Start-Sleep -Seconds 5
}
$waited = [int]((Get-Date) - $oobeStart).TotalSeconds
if (Test-OobeDone) { Write-Output "oobe: done after $waited s" }
else { Write-Output "oobe: still in progress after $waited s; setting autologon anyway" }

$secure = ConvertTo-SecureString $Password -AsPlainText -Force
$user = Get-LocalUser -Name $UserName -ErrorAction SilentlyContinue
if (-not $user) {
    $user = New-LocalUser -Name $UserName -Password $secure -PasswordNeverExpires `
        -AccountNeverExpires -Description 'vmlab-win3d interactive test user'
    Write-Output "created user $UserName"
} else {
    Set-LocalUser -Name $UserName -Password $secure -PasswordNeverExpires $true
}
# Groups by SID, so a localised Windows works too.
$groups = @('S-1-5-32-545')
if ($Administrator) { $groups += 'S-1-5-32-544' }
foreach ($sid in $groups) {
    $group = Get-LocalGroup -SID $sid
    $member = Get-LocalGroupMember -Group $group -ErrorAction SilentlyContinue |
        Where-Object { $_.SID -eq $user.SID }
    if (-not $member) { Add-LocalGroupMember -Group $group -Member $user }
}

$winlogon = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
Set-ItemProperty -Path $winlogon -Name AutoAdminLogon -Value '1'
Set-ItemProperty -Path $winlogon -Name DefaultUserName -Value $UserName
Set-ItemProperty -Path $winlogon -Name DefaultPassword -Value $Password
Set-ItemProperty -Path $winlogon -Name DefaultDomainName -Value $env:COMPUTERNAME
Remove-ItemProperty -Path $winlogon -Name AutoLogonCount -ErrorAction SilentlyContinue

# No first-logon animation, no display blanking, no lock after idle.
$system = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System'
Set-ItemProperty -Path $system -Name EnableFirstLogonAnimation -Value 0 -Type DWord
$personal = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\Personalization'
if (-not (Test-Path $personal)) { New-Item -Path $personal | Out-Null }
Set-ItemProperty -Path $personal -Name NoLockScreen -Value 1 -Type DWord
& powercfg.exe /change monitor-timeout-ac 0 | Out-Null
& powercfg.exe /change standby-timeout-ac 0 | Out-Null

Write-Output "autologon set for $env:COMPUTERNAME\$UserName"
if ($Reboot) {
    & shutdown.exe /r /t 5 /d p:4:1 /c 'vmlab-win3d: autologon for GPU tests'
    Write-Output 'rebooting in 5 s'
}
