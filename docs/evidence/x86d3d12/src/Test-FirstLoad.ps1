<#
.SYNOPSIS
Times the first and second image load of the package's D3D12 DLLs, to show
what Microsoft Defender's first-sight check costs in a fresh guest.

.DESCRIPTION
Maps each file as an image with LoadLibraryExW(DONT_RESOLVE_DLL_REFERENCES),
which creates the image section (where Defender's real-time scan, and its
cloud check for a file it has not seen, runs) without running DllMain, then
frees it. Each file is loaded twice; the second load shows the cached verdict.
The 32-bit list is run in a 32-bit PowerShell (SysWOW64) by this script.
Also prints the Defender settings that decide the cloud check.
Output: one line per load, "<bits> <attempt> <ms> <path>".
#>
param([switch]$Child, [string]$Paths)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FirstLoad {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")]
    public static extern bool FreeLibrary(IntPtr h);
}
'@

function Time-Loads([string[]]$list) {
    $bits = [IntPtr]::Size * 8
    foreach ($attempt in 1, 2) {
        foreach ($p in $list) {
            if (-not (Test-Path -LiteralPath $p)) { "{0} {1} missing {2}" -f $bits, $attempt, $p; continue }
            $sw = [Diagnostics.Stopwatch]::StartNew()
            $h = [FirstLoad]::LoadLibraryExW($p, [IntPtr]::Zero, 1)
            $ms = $sw.Elapsed.TotalMilliseconds
            $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
            if ($h -ne [IntPtr]::Zero) { [void][FirstLoad]::FreeLibrary($h) }
            "{0} {1} {2,9:F1} {3}{4}" -f $bits, $attempt, $ms, $p, $(if ($h -eq [IntPtr]::Zero) { " (error $err)" } else { "" })
        }
    }
}

if ($Child) { Time-Loads ($Paths -split '\|'); return }

"date {0}" -f (Get-Date -Format o)
$m = Get-MpPreference
"defender MAPSReporting={0} SubmitSamplesConsent={1} CloudBlockLevel={2} CloudExtendedTimeout={3} DisableRealtimeMonitoring={4}" -f `
    $m.MAPSReporting, $m.SubmitSamplesConsent, $m.CloudBlockLevel, $m.CloudExtendedTimeout, $m.DisableRealtimeMonitoring

$x64 = @("$env:windir\System32\d3d12.dll", "$env:windir\System32\vmlab-win3d\vkd3dcore.dll",
         "$env:windir\System32\vmlab-win3d\dxgi-vulkan.dll", "$env:windir\System32\vulkan-1.dll",
         "$env:windir\System32\libvulkan_virtio.dll", 'C:\win3d\probes\x64\d3d12_probe.exe')
$x86 = @("$env:windir\SysWOW64\d3d12.dll", "$env:windir\SysWOW64\vmlab-win3d\vkd3dcore.dll",
         "$env:windir\SysWOW64\vmlab-win3d\dxgi-vulkan.dll", "$env:windir\SysWOW64\vulkan-1.dll",
         "$env:windir\SysWOW64\libvulkan_virtio.dll", 'C:\win3d\probes\x86\d3d12_probe.exe')
Time-Loads $x64
& "$env:windir\SysWOW64\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -NonInteractive -ExecutionPolicy Bypass `
    -File $PSCommandPath -Child -Paths ($x86 -join '|')
