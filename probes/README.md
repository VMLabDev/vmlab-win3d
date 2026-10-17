# Probes

Small Windows test applications that show whether a graphics API renders, on
which adapter, and through which driver. There is one probe per API, built for
x64 and x86:

| Probe | API path | Adapter selection |
| --- | --- | --- |
| `d3d9_probe.exe` | `Direct3DCreate9Ex` (falls back to `Direct3DCreate9`), HAL device, `vs_2_0`/`ps_2_0` | Default adapter |
| `d3d11_probe.exe` | `D3D11CreateDevice`, feature level 11_0, 10_1 or 10_0, DXGI swap chain (flip discard, else discard) | Default adapter (`D3D_DRIVER_TYPE_HARDWARE`); WARP with `--warp` |
| `d3d12_probe.exe` | `D3D12CreateDevice` at 11_0 minimum, flip-discard swap chain | Default adapter (`NULL`); WARP with `--warp` |
| `opengl_probe.exe` | WGL compatibility context from `wglCreateContext`, GL 1.1 vertex arrays | Whatever `opengl32.dll` loads: the display driver's ICD, else GDI generic |
| `vulkan_probe.exe` | `vulkan-1.dll` loaded at run time, `VK_KHR_win32_surface` swap chain, FIFO | First device that can present, GPUs before CPU devices |

Each probe opens a 1024×640 window titled `vmlab-win3d probe: <API> (<arch>)`
that stays on top. It draws a turning pinwheel, an orbiting square, the API
name in large letters, a line with the architecture and adapter name, and
three marker squares in fixed colours: red at the top left, blue at the top
right and green at the bottom left. The scene is identical in every probe, so
a console screenshot shows at a glance which API is on screen and whether it
is animating.

After the last frame is drawn, and before it is presented, the probe copies
the back buffer to CPU memory with the API's own readback path. It then
compares the centre pixel of each marker with the expected colour.

| API | Readback path |
| --- | --- |
| D3D9 | `GetRenderTargetData` into a system-memory surface |
| D3D11 | `CopyResource` into a staging texture |
| D3D12 | `CopyTextureRegion` into a readback-heap buffer, then a fence wait |
| OpenGL | `glReadPixels` from the back buffer |
| Vulkan | `vkCmdCopyImageToBuffer` from the swap chain image |

The probes import only Windows system DLLs. The C++ runtime is linked
statically, and the build fails when a probe imports anything else (see
`build/probes/check-imports.sh`). HLSL is compiled at run time with
`System32\d3dcompiler_47.dll`, which ships with Windows 10 and 11, so no
DXBC blob is committed. The Vulkan SPIR-V is compiled at build time by the
pinned glslang.

## Command line

```
<api>_probe.exe [--seconds N] [--out FILE] [--width W] [--height H] [--warp]
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--seconds N` | 30 | Render for N seconds, then read back and exit. `0` renders one frame. |
| `--out FILE` | `<probe>.json` in the current directory | Where the JSON result goes. |
| `--width W`, `--height H` | 1024, 640 | Client area size in pixels. At least 320×240. |
| `--warp` | off | D3D11 and D3D12 only: use the WARP software adapter instead of the default adapter. |
| `--help` | | Print usage to the parent console (the probes are GUI programs) and exit 3. |

Frames are paced by vsync (present interval 1, FIFO, `wglSwapIntervalEXT(1)`
when the ICD has it). GDI generic OpenGL has no swap control, so that run is
unthrottled and reports a very high `avg_fps`.

Closing the window or pressing Esc ends the run early. The current frame is
still read back, and `window_closed_early` is set.

| Exit code | Meaning |
| --- | --- |
| 0 | Pass: no API error and every marker matched |
| 1 | API failure: anything in `errors`, including a missing loader or ICD |
| 2 | Readback mismatch: no API error, but a marker differs by more than 2 per channel |
| 3 | Usage error |

## JSON result

Schema version 1. Example (abridged) from `d3d11_probe.exe` on WARP:

```json
{
  "schema": 1,
  "probe": "d3d11",
  "api": "Direct3D 11",
  "arch": "x64",
  "result": "pass",
  "exit_code": 0,
  "exe": "C:\\win3d\\probes\\x64\\d3d11_probe.exe",
  "pid": 4242,
  "session_id": 1,
  "os_version": "10.0.26100",
  "finished_utc": "2026-10-17T10:00:00Z",
  "args": {"seconds": 30, "warp": false, "width": 1024, "height": 640},
  "adapter": {
    "name": "Microsoft Basic Render Driver",
    "vendor": "Microsoft",
    "vendor_id": "0x1414",
    "device_id": "0x008c",
    "software": true,
    "software_reason": "name contains \"Microsoft Basic Render\""
  },
  "device": {"driver_type": "HARDWARE", "feature_level": "11_0", "swap_effect": "FLIP_DISCARD"},
  "frames_presented": 1790,
  "elapsed_seconds": 30.004,
  "avg_fps": 59.66,
  "window_closed_early": false,
  "readback": {
    "ok": true,
    "tolerance": 2,
    "pixels": [
      {"name": "red_top_left", "x": 48, "y": 48, "expected": [255, 0, 0], "actual": [255, 0, 0], "match": true}
    ]
  },
  "errors": []
}
```

### Top-level fields

| Field | Type | Meaning |
| --- | --- | --- |
| `schema` | int | 1 |
| `probe` | string | `d3d9`, `d3d11`, `d3d12`, `opengl` or `vulkan` |
| `api` | string | Human name of the API |
| `arch` | string | `x64` or `x86` |
| `result` | string | `pass`, `api_failure` or `readback_mismatch` |
| `exit_code` | int | The process exit code, as in the table above |
| `exe`, `pid`, `session_id` | | The process. `session_id` 0 means it ran as a service, where Vulkan does not enumerate |
| `os_version` | string | `major.minor.build` from `RtlGetVersion` |
| `finished_utc` | string | When the result was written |
| `args` | object | The options in effect |
| `adapter` | object | Which device rendered (below) |
| `device` | object | API-specific facts (below) |
| `frames_presented` | int | Successful presents (occluded presents are not counted) |
| `elapsed_seconds` | number | From start-up to the last present |
| `avg_fps` | number | `frames_presented / elapsed_seconds`. It includes start-up, so it understates the steady rate on short runs |
| `window_closed_early` | bool | The window was closed before `--seconds` elapsed |
| `readback.ok` | bool | Every marker was read and matched within `tolerance` |
| `readback.pixels[]` | array | `name`, `x`, `y` (client pixels, origin top left), `expected` and `actual` as `[r, g, b]` (`actual` is `null` if no readback happened), `match` |
| `errors` | array of string | Every failure, in order. The first is usually the cause |

### `adapter`

| Field | Probes | Source |
| --- | --- | --- |
| `name` | all | D3D9 `D3DADAPTER_IDENTIFIER9.Description`, DXGI `DXGI_ADAPTER_DESC1.Description`, `GL_RENDERER`, Vulkan `deviceName` |
| `vendor` | all | Vendor name from the PCI vendor ID; for OpenGL, `GL_VENDOR` |
| `vendor_id`, `device_id` | D3D, Vulkan | PCI IDs as `"0x1234"` |
| `subsys_id`, `dedicated_video_memory_mb`, `shared_system_memory_mb`, `dxgi_flag_software` | D3D11, D3D12 | `DXGI_ADAPTER_DESC1` |
| `driver` | D3D9 | `D3DADAPTER_IDENTIFIER9.Driver`, the D3D9 UMD file name |
| `driver_version` | D3D | D3D9 `DriverVersion`, DXGI `CheckInterfaceSupport(IDXGIDevice)`, as `a.b.c.d` |
| `driver_version_raw` | Vulkan | `VkPhysicalDeviceProperties.driverVersion` (encoding is vendor-specific) |
| `software` | all | True for a software renderer |
| `software_reason` | all | Why: `--warp requested`, `DXGI_ADAPTER_FLAG_SOFTWARE`, PCI vendor 0x1414 (Microsoft), a GDI generic pixel format, `VK_PHYSICAL_DEVICE_TYPE_CPU`, or a name containing Microsoft Basic Render/Display, WARP, GDI Generic, llvmpipe, lavapipe, softpipe or SwiftShader |

### `device`

| Probe | Fields |
| --- | --- |
| D3D9 | `create_function` (`Direct3DCreate9Ex` or `Direct3DCreate9`), `adapter_count`, `vertex_shader_version`, `pixel_shader_version`, `hw_transform_and_light`, `vertex_processing`, `device_type` |
| D3D11 | `driver_type` (`HARDWARE` or `WARP`), `feature_level`, `swap_effect` |
| D3D12 | `adapter_selection`, `max_feature_level`, `highest_shader_model`, `swap_effect` |
| OpenGL | `pixel_format`, `pfd_generic_format`, `pfd_generic_accelerated`, `context`, `gl_renderer`, `gl_vendor`, `gl_version`, `gl_shading_language_version`, `icd_loaded`, `icd_modules` (full paths of loaded modules that export `DrvSetPixelFormat`), `wgl_arb_create_context`, `swap_interval_1` |
| Vulkan | `loader` (path of `vulkan-1.dll`), `instance_version`, `physical_device_count`, `physical_devices` (one line per device), `device_type`, `device_api_version`, `driver_name`, `driver_info`, `driver_id` (Vulkan 1.2 devices), `swapchain_format`, `swapchain_images` |

A field is absent when the probe failed before it could learn it.

## Building

```sh
build/probes/build.sh          # all probes, x64 and x86, into out/probes/{x64,x86}/
build/probes/build.sh clean    # remove them
```

The build runs in the shared toolchain image (`build/toolchain/`: llvm-mingw
20261006 and glslang from the pinned Ubuntu snapshot). The only extra input is
Vulkan-Headers, pinned with a sha256 in `build/probes/PINS`. `build.sh` fetches
it into `out/probes/deps/` once and checks the hash. `make` rebuilds only what
changed. Binaries are stripped and linked without a timestamp, so a clean
rebuild gives the same bytes; this was checked on 2026-10-17 (hashes in
`docs/evidence/probes/README.md`). After every build, `check-imports.sh` fails
the build if a probe imports a DLL that is not a Windows system DLL.

## Running in a guest

Use `tests/guest/Run-Probe.ps1`. It runs the probe in the logged-on user's
desktop session, where Vulkan works and the window is on the console, and it
records the modules the process loaded. `tests/guest/Set-TestDesktop.ps1`
creates an autologon user for that session. See `tests/guest/README.md`.
