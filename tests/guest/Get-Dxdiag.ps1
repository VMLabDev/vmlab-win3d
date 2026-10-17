<#
.SYNOPSIS
Runs dxdiag, waits for its report, and extracts what the display device
reports: driver, version, acceleration and DDI/feature levels.

.DESCRIPTION
`dxdiag /t <file>` returns before the report is written on some builds, so this
script starts it, waits for the process to exit and for the file to stop
growing, then parses the text report. It writes:

  <OutDir>\dxdiag.txt     the full text report
  <OutDir>\dxdiag.xml     the XML report (with -Xml)
  <OutDir>\dxdiag.json    the extract below

and prints the extract as JSON. The extract has one entry per display device
with Card name, Manufacturer, Driver Name, Driver File Version, Driver Version,
Driver Model, DDI Version, Feature Levels, DirectDraw/Direct3D/AGP Texture
Acceleration, DirectX 12 Ultimate, Device Problem Code and Driver Problem Code,
plus a summary: hardware_acceleration is true when Direct3D and DirectDraw
acceleration are both Enabled on the first device.

With -AsUser, dxdiag runs in the interactive user's session through
Run-Probe.ps1 (beside this script) rather than in the caller's session.
With -Parse, no dxdiag runs; an existing report is parsed instead.

.EXAMPLE
Get-Dxdiag.ps1 -OutDir C:\win3d\results
#>
[CmdletBinding()]
param(
    [string]$OutDir = 'C:\win3d\results',
    [switch]$Xml,
    [switch]$AsUser,
    [string]$Parse,
    [int]$TimeoutSec = 300
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Wait-Report([string]$file, [datetime]$deadline) {
    $last = -1
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $file) {
            $len = (Get-Item -LiteralPath $file).Length
            if ($len -gt 0 -and $len -eq $last) { return $true }
            $last = $len
        }
        Start-Sleep -Seconds 2
    }
    return $false
}

function Invoke-Dxdiag([string[]]$arguments, [string]$file) {
    Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    if ($AsUser) {
        $runProbe = Join-Path $PSScriptRoot 'Run-Probe.ps1'
        & $runProbe -Path (Join-Path ([Environment]::SystemDirectory) 'dxdiag.exe') -Raw -NoModules `
            -ArgumentList $arguments -OutDir $OutDir -Name ('dxdiag-' + [IO.Path]::GetExtension($file).TrimStart('.')) `
            -TimeoutSec $TimeoutSec | Out-Null
    } else {
        $p = Start-Process -FilePath (Join-Path ([Environment]::SystemDirectory) 'dxdiag.exe') `
            -ArgumentList $arguments -PassThru -WindowStyle Hidden
        if (-not $p.WaitForExit($TimeoutSec * 1000)) {
            try { $p.Kill() } catch {}
            throw "dxdiag did not finish within $TimeoutSec s"
        }
    }
    if (-not (Wait-Report $file $deadline)) { throw "dxdiag wrote no report to $file" }
}

# Parses "Key: Value" lines of the dxdiag text report into sections.
function ConvertFrom-DxdiagText([string[]]$lines) {
    $sections = [ordered]@{}
    $current = 'Header'
    $entries = New-Object System.Collections.ArrayList
    $sections[$current] = $entries
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        # A section title sits between two rules of dashes.
        if ($line -match '^-{3,}\s*$' -and $i + 2 -lt $lines.Count -and $lines[$i + 2] -match '^-{3,}\s*$') {
            $current = $lines[$i + 1].Trim()
            $entries = New-Object System.Collections.ArrayList
            $sections[$current] = $entries
            $i += 2
            continue
        }
        if ($line -match '^\s*([^:]+?):\s?(.*)$') {
            [void]$entries.Add(@($matches[1].Trim(), $matches[2].Trim()))
        }
    }
    return $sections
}

$keys = @('Card name', 'Manufacturer', 'Chip type', 'Device Type', 'Device Key', 'Device Status',
    'Device Problem Code', 'Driver Problem Code', 'Display Memory', 'Dedicated Memory', 'Shared Memory',
    'Current Mode', 'Driver Name', 'Driver File Version', 'Driver Version', 'Driver Language',
    'DDI Version', 'Feature Levels', 'Driver Model', 'WDDM Version', 'Hardware Scheduling',
    'Driver Attributes', 'Driver Date/Size', 'Vendor ID', 'Device ID', 'SubSys ID', 'Revision ID',
    'DirectDraw Acceleration', 'Direct3D Acceleration', 'AGP Texture Acceleration',
    'DirectX 12 Ultimate', 'Miracast', 'Hybrid Graphics GPU', 'Power P-states', 'Virtualization',
    'Block List', 'Catalog Attributes', 'D3D9 Overlay', 'User Mode Driver Version')

New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).ProviderPath
$txt = Join-Path $OutDir 'dxdiag.txt'
if ($Parse) {
    $txt = (Resolve-Path -LiteralPath $Parse).ProviderPath
} else {
    Invoke-Dxdiag @('/whql:off', '/t', $txt) $txt
    if ($Xml) {
        $xmlFile = Join-Path $OutDir 'dxdiag.xml'
        Invoke-Dxdiag @('/whql:off', '/x', $xmlFile) $xmlFile
    }
}

$sections = ConvertFrom-DxdiagText (Get-Content -LiteralPath $txt)

$system = [ordered]@{}
if ($sections.Contains('System Information')) {
    foreach ($kv in $sections['System Information']) {
        if ($kv[0] -in @('Operating System', 'DirectX Version', 'DxDiag Version', 'System Manufacturer',
                'System Model', 'Processor', 'Memory', 'Time of this report')) {
            $system[$kv[0]] = $kv[1]
        }
    }
}

$devices = New-Object System.Collections.ArrayList
$device = $null
if ($sections.Contains('Display Devices')) {
    foreach ($kv in $sections['Display Devices']) {
        if ($kv[0] -eq 'Card name') {
            $device = [ordered]@{}
            [void]$devices.Add($device)
        }
        if ($device -and $kv[0] -in $keys -and -not $device.Contains($kv[0])) {
            $device[$kv[0]] = $kv[1]
        }
    }
}

# "Display Tab 1: No problems found." and the like.
$notes = @(Get-Content -LiteralPath $txt | Where-Object { $_ -match '^\s*Display Tab \d+:' } |
    ForEach-Object { $_.Trim() })

$first = $null
if ($devices.Count -gt 0) { $first = $devices[0] }
function Get-Field($d, [string]$k) { if ($d -and $d.Contains($k)) { return $d[$k] } return $null }
$summary = [ordered]@{
    card_name               = Get-Field $first 'Card name'
    driver_name             = Get-Field $first 'Driver Name'
    driver_version          = Get-Field $first 'Driver Version'
    driver_model            = Get-Field $first 'Driver Model'
    ddi_version             = Get-Field $first 'DDI Version'
    feature_levels          = Get-Field $first 'Feature Levels'
    directdraw_acceleration = Get-Field $first 'DirectDraw Acceleration'
    direct3d_acceleration   = Get-Field $first 'Direct3D Acceleration'
    agp_texture_acceleration = Get-Field $first 'AGP Texture Acceleration'
    hardware_acceleration   = ((Get-Field $first 'Direct3D Acceleration') -eq 'Enabled' -and
                               (Get-Field $first 'DirectDraw Acceleration') -eq 'Enabled')
    display_notes           = $notes
}

$result = [ordered]@{
    report  = $txt
    system  = $system
    summary = $summary
    devices = $devices
}
$json = ConvertTo-Json -InputObject $result -Depth 5
if (-not $Parse) { $json | Set-Content -LiteralPath (Join-Path $OutDir 'dxdiag.json') -Encoding UTF8 }
Write-Output $json
