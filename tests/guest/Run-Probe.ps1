<#
.SYNOPSIS
Runs a probe (or any program) in the interactive user's desktop session and
collects its JSON result and the modules it loaded.

.DESCRIPTION
Graphics tests must run in the logged-on user's session: Vulkan does not
enumerate devices from SYSTEM (session 0), and only session 1 is on the
console. This script registers a one-off scheduled task with an interactive
logon for that user, starts it, and while the process runs it samples the
process's loaded modules with Toolhelp (TH32CS_SNAPMODULE | SNAPMODULE32, so a
32-bit probe's WOW64 modules are listed too). When the process exits it writes:

  <OutDir>\<Name>.json          the probe's own result (probes only)
  <Name>.modules.json           every module seen, with path, version and flags
  <Name>.run.json               this run: user, session, pid, exit code, timing,
                                and the graphics modules and where they came from

and prints the run summary as JSON. Run it elevated (SYSTEM or an admin), for
example through `vmlab exec`. Exit code: the program's exit code, or 124 on a
timeout, or 125 when the program could not be started.

.EXAMPLE
Run-Probe.ps1 -Path C:\win3d\probes\x64\d3d11_probe.exe -Seconds 20 -OutDir C:\win3d\results

.EXAMPLE
Run-Probe.ps1 -Path C:\Windows\System32\dxdiag.exe -Raw -ArgumentList '/t','C:\win3d\results\dxdiag.txt' -Name dxdiag
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string[]]$ArgumentList = @(),
    [string]$OutDir = 'C:\win3d\results',
    [string]$Name,
    [int]$Seconds = 30,
    [int]$TimeoutSec = 0,
    [string]$User,
    # Run the program as is: do not add --seconds and --out.
    [switch]$Raw,
    # Skip module sampling (for programs that are not probes).
    [switch]$NoModules
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

$Path = (Resolve-Path -LiteralPath $Path).ProviderPath
if (-not $Name) {
    # x64\d3d11_probe.exe -> x64-d3d11_probe, so x86 and x64 runs do not collide.
    $Name = (Split-Path -Leaf (Split-Path -Parent $Path)) + '-' + [IO.Path]::GetFileNameWithoutExtension($Path)
}
if ($TimeoutSec -le 0) { $TimeoutSec = $Seconds + 300 }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).ProviderPath

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class Win3dToolhelp {
    const uint TH32CS_SNAPMODULE = 0x8, TH32CS_SNAPMODULE32 = 0x10;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct MODULEENTRY32W {
        public uint dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage;
        public IntPtr modBaseAddr;
        public uint modBaseSize;
        public IntPtr hModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szModule;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExePath;
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool Module32FirstW(IntPtr snap, ref MODULEENTRY32W me);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool Module32NextW(IntPtr snap, ref MODULEENTRY32W me);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")]
    static extern bool IsWow64Process(IntPtr h, out bool wow64);

    // Returns "base|size|path" per module, or null when the snapshot fails
    // (the process is starting or gone).
    public static string[] Modules(int pid) {
        IntPtr snap = IntPtr.Zero;
        for (int attempt = 0; attempt < 5; attempt++) {
            snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, (uint)pid);
            if (snap != new IntPtr(-1)) break;
            if (Marshal.GetLastWin32Error() != 24) return null;  // ERROR_BAD_LENGTH: retry
        }
        if (snap == new IntPtr(-1)) return null;
        var list = new List<string>();
        try {
            var me = new MODULEENTRY32W();
            me.dwSize = (uint)Marshal.SizeOf(typeof(MODULEENTRY32W));
            if (!Module32FirstW(snap, ref me)) return null;
            do {
                list.Add(string.Format("0x{0:x}|{1}|{2}", me.modBaseAddr.ToInt64(), me.modBaseSize, me.szExePath));
            } while (Module32NextW(snap, ref me));
        } finally { CloseHandle(snap); }
        return list.ToArray();
    }

    public static bool IsWow64(IntPtr handle) {
        bool w; return IsWow64Process(handle, out w) && w;
    }
}
'@

function Get-InteractiveUser {
    $name = (Get-CimInstance Win32_ComputerSystem).UserName
    if ($name) { return $name }
    # Fall back to the owner of explorer.exe in a non-zero session.
    $explorer = Get-CimInstance Win32_Process -Filter "Name='explorer.exe'" |
        Where-Object { $_.SessionId -ne 0 } | Select-Object -First 1
    if ($explorer) {
        $owner = Invoke-CimMethod -InputObject $explorer -MethodName GetOwner
        if ($owner.User) { return "$($owner.Domain)\$($owner.User)" }
    }
    return $null
}

# Graphics-relevant modules, by file name.
$graphicsPattern = '^(d3d|dxgi|dxcore|ddraw|opengl32|glu32|vulkan|libvulkan|vk|viogpu|yttrium|libgallium|' +
    'dxil|d3dcompiler|vkd3d|dxvk|wined3d|nvd3d|nvwgf|nvogl|nvldumd|amdxx|atiu|atig|aticfx|amdvlk|' +
    'igd|igc|ig\d|lvp|swiftshader|mesa|zink|basicrender|msvproxy|dwmapi|windows\.graphics)'

if (-not $User) { $User = Get-InteractiveUser }
if (-not $User) {
    throw 'nobody is logged on to the console; run Set-TestDesktop.ps1 -Reboot first'
}

$args2 = @($ArgumentList)
$probeJson = Join-Path $OutDir "$Name.json"
if (-not $Raw) {
    Remove-Item -LiteralPath $probeJson -ErrorAction SilentlyContinue
    $args2 += @('--seconds', "$Seconds", '--out', $probeJson)
}
$argString = ($args2 | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '

# Let the user write results.
& icacls.exe $OutDir /grant "${User}:(OI)(CI)M" /Q | Out-Null

$taskName = "win3d-run-$Name-" + [guid]::NewGuid().ToString('N').Substring(0, 8)
# New-ScheduledTaskAction rejects an empty -Argument, so pass it only when
# there is one (a -Raw run with no -ArgumentList has none).
$actionArgs = @{ Execute = $Path; WorkingDirectory = $OutDir }
if ($argString) { $actionArgs.Argument = $argString }
$action = New-ScheduledTaskAction @actionArgs
$principal = New-ScheduledTaskPrincipal -UserId $User -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Seconds ($TimeoutSec + 60)) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Settings $settings | Out-Null

$started = Get-Date
$proc = $null
$sessionId = $null
$procId = $null
$modules = @{}
$order = New-Object System.Collections.ArrayList
$timedOut = $false
$exitCode = $null
$taskResult = $null
try {
    Start-ScheduledTask -TaskName $taskName
    # Find the process the task started. A task stays Queued until the user's
    # logon has finished, so allow for that.
    $deadline = $started.AddSeconds(180)
    $escaped = $Path.Replace('\', '\\').Replace("'", "\'")
    while (-not $proc -and (Get-Date) -lt $deadline) {
        $cim = Get-CimInstance Win32_Process -Filter "ExecutablePath='$escaped'" |
            Where-Object { $_.CreationDate -ge $started.AddSeconds(-2) } | Select-Object -First 1
        if ($cim) {
            try {
                $proc = [Diagnostics.Process]::GetProcessById([int]$cim.ProcessId)
                $null = $proc.Handle  # keep a handle so the exit code survives the exit
                $sessionId = $proc.SessionId
                $procId = $proc.Id
            } catch { $proc = $null }
        }
        if (-not $proc) {
            $state = "$((Get-ScheduledTask -TaskName $taskName).State)"
            if ($state -ne 'Running' -and $state -ne 'Queued' -and ((Get-Date) - $started).TotalSeconds -gt 5) {
                break  # the task already ended
            }
            Start-Sleep -Milliseconds 200
        }
    }

    if ($proc) {
        $wow64 = [Win3dToolhelp]::IsWow64($proc.Handle)
        $end = $started.AddSeconds($TimeoutSec)
        while (-not $proc.HasExited) {
            if (-not $NoModules) {
                $snap = [Win3dToolhelp]::Modules($proc.Id)
                if ($snap) {
                    $t = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
                    foreach ($m in $snap) {
                        $parts = $m.Split('|', 3)
                        $key = $parts[2].ToLowerInvariant()
                        if (-not $modules.ContainsKey($key)) {
                            $modules[$key] = @{ base = $parts[0]; size = [int64]$parts[1]; path = $parts[2]; first_seen_s = $t }
                            [void]$order.Add($key)
                        }
                    }
                }
            }
            if ((Get-Date) -gt $end) {
                $timedOut = $true
                try { $proc.Kill() } catch {}
                break
            }
            [void]$proc.WaitForExit(1000)
        }
        [void]$proc.WaitForExit(5000)
        if (-not $timedOut) { $exitCode = $proc.ExitCode }
    }
    # The task result is the exit code too; wait for the scheduler to record it.
    for ($i = 0; $i -lt 20; $i++) {
        if ("$((Get-ScheduledTask -TaskName $taskName).State)" -notin @('Running', 'Queued')) { break }
        Start-Sleep -Milliseconds 250
    }
    $taskResult = (Get-ScheduledTaskInfo -TaskName $taskName).LastTaskResult
} finally {
    Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
}
$ended = Get-Date
if ($null -eq $exitCode -and -not $timedOut -and $null -ne $taskResult -and $taskResult -ne 267009) {
    # LastTaskResult is unsigned: an NTSTATUS such as 0xC0000135 (missing DLL)
    # does not fit [int], so reinterpret its bits as Process.ExitCode does.
    $exitCode = [BitConverter]::ToInt32([BitConverter]::GetBytes([uint32]([int64]$taskResult -band [int64]4294967295)), 0)
}

$exeDir = [IO.Path]::GetDirectoryName($Path).ToLowerInvariant()
$sys32 = [Environment]::SystemDirectory.ToLowerInvariant()
$wow = (Join-Path $env:windir 'SysWOW64').ToLowerInvariant()
$moduleList = foreach ($key in $order) {
    $m = $modules[$key]
    $file = [IO.Path]::GetFileName($m.path)
    $dir = [IO.Path]::GetDirectoryName($m.path).ToLowerInvariant()
    $vi = $null
    try { $vi = [Diagnostics.FileVersionInfo]::GetVersionInfo($m.path) } catch {}
    $location = 'other'
    if ($dir -eq $exeDir) { $location = 'app_local' }
    elseif ($dir.StartsWith($sys32 + '\driverstore\')) { $location = 'driverstore' }
    elseif ($dir -eq $sys32) { $location = 'system32' }
    elseif ($dir -eq $wow) { $location = 'syswow64' }
    elseif ($dir.StartsWith($sys32 + '\') -or $dir.StartsWith($wow + '\')) { $location = 'system_subdir' }
    [ordered]@{
        path         = $m.path
        name         = $file
        base         = $m.base
        size         = $m.size
        file_version = $(if ($vi) { $vi.FileVersion } else { $null })
        company      = $(if ($vi) { $vi.CompanyName } else { $null })
        location     = $location
        graphics     = [bool]($file -match $graphicsPattern -and $m.path -ne $Path)
        first_seen_s = $m.first_seen_s
    }
}
$moduleList = @($moduleList | Where-Object { $null -ne $_ })
$graphics = @($moduleList | Where-Object { $_.graphics } | ForEach-Object { "$($_.path) [$($_.location)] $($_.file_version)" })
$appLocal = @($moduleList | Where-Object { $_.location -eq 'app_local' -and $_.name -ne [IO.Path]::GetFileName($Path) } | ForEach-Object { $_.path })

# Reads a nested field of a ConvertFrom-Json object, or $null when absent
# (a probe that failed early leaves "adapter" empty).
function Get-JsonField($obj, [string[]]$names) {
    foreach ($n in $names) {
        if ($null -eq $obj) { return $null }
        $p = $obj.PSObject.Properties[$n]
        if (-not $p) { return $null }
        $obj = $p.Value
    }
    return $obj
}

$probeResult = $null
if (-not $Raw -and (Test-Path -LiteralPath $probeJson)) {
    $probeResult = Get-Content -LiteralPath $probeJson -Raw | ConvertFrom-Json
}

# A probe that fails at once can exit before the process is found; its own
# JSON still names the session and pid.
if ($null -eq $sessionId) { $sessionId = Get-JsonField $probeResult 'session_id' }
if ($null -eq $procId) { $procId = Get-JsonField $probeResult 'pid' }

$run = [ordered]@{
    name               = $Name
    exe                = $Path
    arguments          = $argString
    user               = $User
    session_id         = $sessionId
    pid                = $procId
    wow64              = $(if ($proc) { $wow64 } else { $null })
    started            = $started.ToString('o')
    ended              = $ended.ToString('o')
    duration_s         = [math]::Round(($ended - $started).TotalSeconds, 1)
    timed_out          = $timedOut
    exit_code          = $exitCode
    exit_code_hex      = $(if ($null -ne $exitCode) { '0x{0:X8}' -f $exitCode } else { $null })
    task_result        = $taskResult
    probe_json         = $(if ($probeResult) { $probeJson } else { $null })
    probe_result       = $(if ($probeResult) { $probeResult.result } else { $null })
    adapter            = Get-JsonField $probeResult 'adapter', 'name'
    software           = Get-JsonField $probeResult 'adapter', 'software'
    readback_ok        = Get-JsonField $probeResult 'readback', 'ok'
    module_count       = $moduleList.Count
    graphics_modules   = $graphics
    app_local_modules  = $appLocal
}
if (-not $NoModules) {
    ConvertTo-Json -InputObject $moduleList -Depth 4 | Set-Content -LiteralPath (Join-Path $OutDir "$Name.modules.json") -Encoding UTF8
}
$runJson = ConvertTo-Json -InputObject $run -Depth 4
$runJson | Set-Content -LiteralPath (Join-Path $OutDir "$Name.run.json") -Encoding UTF8
Write-Output $runJson

if ($timedOut) { exit 124 }
if ($null -eq $proc -and $null -eq $exitCode) { exit 125 }
if ($null -eq $exitCode) { exit 125 }
exit $exitCode
