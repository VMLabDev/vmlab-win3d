# Stage 2, as SYSTEM, after the reboot that activates test signing: install the
# Yttrium KMD on the virtio GPU. Reboot afterwards.
Start-Transcript -Path C:\hp\setup2.txt -Force
bcdedit /enum '{current}' | Select-String testsigning
Get-CimInstance Win32_VideoController | Select Name, DriverVersion, PNPDeviceID, ConfigManagerErrorCode | Format-List
pnputil /add-driver C:\hp\yt\Install_Debug\Win11\amd64\viogpu3d.inf /install
"pnputil exit $LASTEXITCODE"
Get-CimInstance Win32_VideoController | Select Name, DriverVersion, ConfigManagerErrorCode | Format-List
Stop-Transcript
