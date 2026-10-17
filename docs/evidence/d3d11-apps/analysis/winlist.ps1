param([string]$Out = 'C:\win3d\results\winlist.txt', [string]$Proc = 'BasicHLSL11')
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("dwmapi.dll")] public static extern int DwmGetWindowAttribute(IntPtr h, int a, out int v, int s);
}
'@
$pids = @(Get-Process $Proc -ErrorAction SilentlyContinue | ForEach-Object Id)
$lines = @("pids: $($pids -join ',')")
[W]::EnumWindows({ param($h, $l)
  $p = 0; [void][W]::GetWindowThreadProcessId($h, [ref]$p)
  if ($pids -contains $p) {
    $t = New-Object Text.StringBuilder 256; [void][W]::GetWindowText($h, $t, 256)
    $c = New-Object Text.StringBuilder 256; [void][W]::GetClassName($h, $c, 256)
    $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
    $cr = New-Object W+RECT; [void][W]::GetClientRect($h, [ref]$cr)
    $cl = 0; [void][W]::DwmGetWindowAttribute($h, 14, [ref]$cl, 4)
    $script:lines += ("hwnd 0x{0:x} title '{1}' class '{2}' visible {3} iconic {4} rect {5},{6}-{7},{8} client {9}x{10} style 0x{11:x} exstyle 0x{12:x} cloaked {13}" -f
      [int64]$h, $t, $c, [W]::IsWindowVisible($h), [W]::IsIconic($h), $r.l, $r.t, $r.r, $r.b, $cr.r, $cr.b, [W]::GetWindowLong($h, -16), [W]::GetWindowLong($h, -20), $cl)
  }
  $true }, [IntPtr]::Zero) | Out-Null
$lines | Set-Content $Out
