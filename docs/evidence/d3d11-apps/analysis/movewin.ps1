param([string]$Proc = 'BasicHLSL11', [int]$W = 816, [int]$H = 639)
# Moves the process's top-level window to (40,40) at W x H, from outside the
# process (the application is not modified). Used to see a window that the
# application placed off-screen.
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class M {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
}
'@
$p = Get-Process $Proc | Select-Object -First 1
"{0} hwnd 0x{1:x}: SetWindowPos -> {2}" -f $Proc, [int64]$p.MainWindowHandle, [M]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, 40, 40, $W, $H, 0x4)
