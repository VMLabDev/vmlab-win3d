# Run as the interactive user (scheduled task with /IT; Vulkan does not enumerate
# from SYSTEM): vulkaninfo --summary, then start vkcube and leave it running.
param([string]$Tag = 'run')
$out = "C:\hp\out\$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
"user: $(whoami)  session: $((Get-Process -Id $PID).SessionId)" | Out-File -Encoding ascii "$out\whoami.txt"
cmd /c "vulkaninfo --summary > $out\vulkaninfo-summary.txt 2>&1"
"vulkaninfo exit $LASTEXITCODE" | Out-File -Encoding ascii -Append "$out\whoami.txt"
$cube = Get-ChildItem -Recurse C:\VulkanSDK -Filter vkcube.exe | Select -First 1
$p = Start-Process $cube.FullName -PassThru -RedirectStandardOutput "$out\vkcube-out.txt" -RedirectStandardError "$out\vkcube-err.txt"
Start-Sleep -Seconds 6
try {
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save("$out\guest-screen.png")
} catch { "screen capture failed: $_" | Out-File -Encoding ascii -Append "$out\whoami.txt" }
"vkcube alive after 6s: $(-not $p.HasExited)" | Out-File -Encoding ascii -Append "$out\whoami.txt"
