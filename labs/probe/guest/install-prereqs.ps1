# Stage 1, as SYSTEM: Yttrium README prerequisites (test cert, test signing,
# hibernate off), LunarG Vulkan runtime + SDK (for vkcube), autologon user "probe".
# Expects the payload files in C:\hp. Reboot afterwards.
$ErrorActionPreference = 'Stop'
$log = 'C:\hp\setup1.txt'
Start-Transcript -Path $log -Force
Expand-Archive -Force C:\hp\yttrium-viogpu3d-experimental-2026-09-27.zip C:\hp\yt
certutil -f -addstore Root C:\hp\yt\Install_Debug\VirtIOTestCert.cer
certutil -f -addstore TrustedPublisher C:\hp\yt\Install_Debug\VirtIOTestCert.cer
bcdedit /set testsigning on
if ($LASTEXITCODE -ne 0) { throw "bcdedit failed $LASTEXITCODE" }
powercfg /hibernate off
$p = Start-Process C:\hp\VulkanRT-X64-1.4.363.0-Installer.exe -ArgumentList '/S' -Wait -PassThru
"VulkanRT exit $($p.ExitCode)"
$p = Start-Process C:\hp\vulkansdk-windows-X64-1.4.363.0.exe -ArgumentList '--root','C:\VulkanSDK','--accept-licenses','--default-answer','--confirm-command','install' -Wait -PassThru
"VulkanSDK exit $($p.ExitCode)"
Get-ChildItem C:\Windows\System32\vulkan-1.dll, C:\Windows\System32\vulkaninfo.exe -ErrorAction SilentlyContinue | Select FullName, @{n='Version';e={$_.VersionInfo.FileVersion}}
Get-ChildItem -Recurse C:\VulkanSDK -Filter vkcube.exe | Select FullName
$pw = 'Probe-pass-123!'
if (-not (Get-LocalUser probe -ErrorAction SilentlyContinue)) {
  New-LocalUser -Name probe -Password (ConvertTo-SecureString $pw -AsPlainText -Force) -PasswordNeverExpires
  Add-LocalGroupMember -Group Administrators -Member probe
}
$w = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon'
Set-ItemProperty $w AutoAdminLogon '1'
Set-ItemProperty $w DefaultUserName 'probe'
Set-ItemProperty $w DefaultPassword $pw
Set-ItemProperty $w DefaultDomainName '.'
bcdedit /enum '{current}' | Select-String testsigning
Stop-Transcript
