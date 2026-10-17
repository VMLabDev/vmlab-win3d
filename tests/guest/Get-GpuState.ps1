<#
.SYNOPSIS
Reports the guest's display driver state: PnP status, driver package, the
user-mode driver registration, Vulkan ICDs and the boot signing policy.

.DESCRIPTION
For each display-class device: status, problem code, hardware IDs, driver INF,
version, provider and date (from the DEVPKEY properties, which update at once
after pnputil, unlike Win32_PnPSignedDriver), and the driver's registry key
under Control\Class with UserModeDriverName*, OpenGLDriverName*, OpenGLFlags*,
OpenGLVersion* and InstalledDisplayDrivers. Then the Khronos Vulkan driver and
layer registrations in both registry views, the system graphics DLL versions in
System32 and SysWOW64, and the testsigning, Secure Boot and hibernation state.
Prints JSON, and writes it to -OutFile when given. Run it elevated.

.EXAMPLE
Get-GpuState.ps1 -OutFile C:\win3d\results\gpu-state.json
#>
[CmdletBinding()]
param([string]$OutFile)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Get-DevProp($dev, [string]$key) {
    try {
        $p = Get-PnpDeviceProperty -InputObject $dev -KeyName $key -ErrorAction Stop
        return $p.Data
    } catch { return $null }
}

function Get-Values([string]$path, [string]$pattern) {
    $out = [ordered]@{}
    if (-not (Test-Path -LiteralPath $path)) { return $out }
    $item = Get-Item -LiteralPath $path
    foreach ($name in $item.GetValueNames()) {
        if ($name -match $pattern) {
            $v = $item.GetValue($name, $null, 'DoNotExpandEnvironmentNames')
            if ($v -is [array]) { $v = @($v) }
            $out[$name] = $v
        }
    }
    return $out
}

$displayClass = '{4d36e968-e325-11ce-bfc1-08002be10318}'
$devices = foreach ($dev in @(Get-PnpDevice -Class Display -ErrorAction SilentlyContinue)) {
    $driverKey = Get-DevProp $dev 'DEVPKEY_Device_Driver'
    $regPath = $null
    $registry = [ordered]@{}
    if ($driverKey) {
        $regPath = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\$driverKey"
        $registry = Get-Values $regPath '^(UserModeDriverName|UserModeDriverNameWow|UserModeDriverGUID|OpenGLDriverName|OpenGLDriverNameWow|OpenGLFlags|OpenGLFlagsWow|OpenGLVersion|OpenGLVersionWow|InstalledDisplayDrivers|DriverDesc|ProviderName|DriverVersion|InfPath|InfSection|FeatureScore|EnablePreemption|TdrDebugMode)$'
    }
    [ordered]@{
        name             = $dev.FriendlyName
        instance_id      = $dev.InstanceId
        status           = "$($dev.Status)"
        present          = $dev.Present
        problem_code     = Get-DevProp $dev 'DEVPKEY_Device_ProblemCode'
        problem_status   = Get-DevProp $dev 'DEVPKEY_Device_ProblemStatus'
        hardware_ids     = @(Get-DevProp $dev 'DEVPKEY_Device_HardwareIds')
        driver_inf       = Get-DevProp $dev 'DEVPKEY_Device_DriverInfPath'
        driver_inf_section = Get-DevProp $dev 'DEVPKEY_Device_DriverInfSection'
        driver_version   = Get-DevProp $dev 'DEVPKEY_Device_DriverVersion'
        driver_provider  = Get-DevProp $dev 'DEVPKEY_Device_DriverProvider'
        driver_date      = "$(Get-DevProp $dev 'DEVPKEY_Device_DriverDate')"
        driver_desc      = Get-DevProp $dev 'DEVPKEY_Device_DriverDesc'
        driver_key       = $driverKey
        service          = Get-DevProp $dev 'DEVPKEY_Device_Service'
        registry_path    = $regPath
        registry         = $registry
    }
}

$vulkan = [ordered]@{}
foreach ($view in @(
        @{ name = 'native'; root = 'HKLM:\SOFTWARE\Khronos\Vulkan' },
        @{ name = 'wow6432'; root = 'HKLM:\SOFTWARE\WOW6432Node\Khronos\Vulkan' })) {
    $entry = [ordered]@{}
    foreach ($kind in @('Drivers', 'ImplicitLayers', 'ExplicitLayers')) {
        $entry[$kind] = Get-Values (Join-Path $view.root $kind) '.'
    }
    $vulkan[$view.name] = $entry
}
# Per-adapter ICDs that WDDM drivers register in their device key.
$vulkanAdapter = @($devices | ForEach-Object {
        if ($_.registry_path) {
            $v = Get-Values $_.registry_path '^(VulkanDriverName|VulkanDriverNameWow|VulkanImplicitLayers|VulkanExplicitLayers)$'
            if ($v.Count) { $v['registry_path'] = $_.registry_path; $v }
        }
    })

$dlls = foreach ($dir in @([Environment]::SystemDirectory, (Join-Path $env:windir 'SysWOW64'))) {
    foreach ($name in @('d3d9.dll', 'd3d10.dll', 'd3d10_1.dll', 'd3d11.dll', 'd3d12.dll', 'D3D12Core.dll',
            'dxgi.dll', 'opengl32.dll', 'vulkan-1.dll', 'd3dcompiler_47.dll')) {
        $p = Join-Path $dir $name
        if (Test-Path -LiteralPath $p) {
            $vi = [Diagnostics.FileVersionInfo]::GetVersionInfo($p)
            [ordered]@{
                path    = $p
                version = $vi.FileVersion
                company = $vi.CompanyName
                sha256  = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash
            }
        } else {
            [ordered]@{ path = $p; version = $null; company = $null; sha256 = $null }
        }
    }
}

$bcd = (& bcdedit.exe /enum '{current}' 2>&1) -join "`n"
$testsigning = $null
if ($bcd -match '(?m)^testsigning\s+(\S+)') { $testsigning = $matches[1] }
$nointegrity = $null
if ($bcd -match '(?m)^nointegritychecks\s+(\S+)') { $nointegrity = $matches[1] }
$secureBoot = $null
try { $secureBoot = Confirm-SecureBootUEFI } catch { $secureBoot = "unknown: $($_.Exception.Message)" }
$hiberboot = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Power' -Name HiberbootEnabled -ErrorAction SilentlyContinue)
$os = Get-CimInstance Win32_OperatingSystem

$state = [ordered]@{
    computer        = $env:COMPUTERNAME
    os              = "$($os.Caption) $($os.Version)"
    collected       = (Get-Date).ToString('o')
    display_devices = @($devices)
    vulkan_registry = $vulkan
    vulkan_adapter_registry = $vulkanAdapter
    system_dlls     = @($dlls)
    boot            = [ordered]@{
        testsigning        = $testsigning
        nointegritychecks  = $nointegrity
        secure_boot        = $secureBoot
        hiberboot_enabled  = $(if ($hiberboot) { $hiberboot.HiberbootEnabled } else { $null })
    }
    video_controllers = @(Get-CimInstance Win32_VideoController | ForEach-Object {
            [ordered]@{
                name                = $_.Name
                driver_version      = $_.DriverVersion
                installed_display_drivers = $_.InstalledDisplayDrivers
                status              = $_.Status
                current_mode        = "$($_.CurrentHorizontalResolution)x$($_.CurrentVerticalResolution)x$($_.CurrentBitsPerPixel)"
            }
        })
}
$json = ConvertTo-Json -InputObject $state -Depth 6
if ($OutFile) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $OutFile) -Force | Out-Null
    $json | Set-Content -LiteralPath $OutFile -Encoding UTF8
}
Write-Output $json
