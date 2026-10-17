# d3d12: vkd3d-proton, the DXGI-only DXVK and the d3d12.dll bridge

This slice builds the system-wide Direct3D 12 path for Windows 10 and 11
guests, for x64 and x86:

| File | What it is |
| --- | --- |
| `d3d12.dll` | The **bridge**. It replaces `System32\d3d12.dll` (x64) and `SysWOW64\d3d12.dll` (x86). |
| `vkd3dcore.dll` | vkd3d-proton's core (upstream `d3d12core.dll`, renamed), with dxil-spirv built in. |
| `dxgi-vulkan.dll` | DXVK's `dxgi.dll` built with D3D8, D3D9, D3D10 and D3D11 disabled, renamed. |

## Build

```sh
build/d3d12/build.sh            # incremental after the first run
build/d3d12/build.sh --clean    # discard fetched sources and build trees first
```

The script runs in the shared toolchain image (`build/toolchain/`, llvm-mingw
20261006, LLVM 23.1.3, ucrt) as the calling user. It needs Docker and network
access, and no sudo. It writes only to `out/d3d12/`:

```
out/d3d12/x64/   d3d12.dll vkd3dcore.dll dxgi-vulkan.dll exports.txt imports.txt
out/d3d12/x86/   d3d12.dll vkd3dcore.dll dxgi-vulkan.dll exports.txt imports.txt
out/d3d12/SHA256SUMS
out/d3d12/.work/ fetched sources, build trees and the stamp
```

`container-build.sh` fetches each source by commit, applies the patches,
builds, and runs `check.sh`. A stamp over the pins, the patches, the build
scripts and the toolchain image tag decides whether the sources are still
valid. If the stamp matches, a rerun is an incremental `ninja` and changes
nothing. If it does not match, the sources are reset to their pins, the
patches are applied again and the build trees are recreated.

`check.sh` fails the build unless both conditions hold:

- the bridge exports every name and ordinal-only export in each recorded
  native table (`native-exports/`), with `D3D12CreateDevice` at ordinal 101
  and `D3D12GetDebugInterface` at 102;
- every DLL imports only Windows system DLLs (`api-ms-win-*`, `kernel32`,
  `user32`, `gdi32`, `advapi32`, `dxgi`, `setupapi`, `version` and similar).

`exports.txt` and `imports.txt` are its evidence. The C and C++ runtimes are
linked statically, so there is no `libc++.dll` or `libunwind.dll`. The UCRT
comes from the `api-ms-win-crt-*` API sets, which ship with Windows 10 and 11.

DXVK builds with llvm-mingw, so this slice needs no mingw-w64 GCC image. The
prior art used GCC 16.2.

## Pins

Every source is a git commit, so the commit hash is the content pin. Nested
submodules follow the superproject's gitlinks. The full list is in `PINS`.

| Component | Version | Commit | Why this one |
| --- | --- | --- | --- |
| vkd3d-proton | v3.0.1 | `3b10bd7a7ec6a7347e616cf8bea59333afec2255` | Newest release on 2026-10-17. It is also the prior-art base, so the patches port without change. |
| dxil-spirv | master | `8434b0639fb2a48ce13aac08a420ecce5d23df0e` | Fixes Godot D3D12's invalid SPIR-V (prior art §2.2). Its nested pins: dxbc-spirv `8dae2d85`, SPIRV-Cross `be71ee8c`, SPIRV-Tools `199cb207`, spirv-headers `c63848ec`. |
| DXVK | v3.1.1 | `b1a1c99ab52b687cf950d62c88bc2fa316b41663` | Newest release on 2026-10-17. It is the prior-art base. |

**DXVK clear-only black frame** (prior art §2.3): the 3.1 and 3.1.1 release
notes list no fix, and the prior art reproduced the bug on 3.1.1, so this pin
still has it. The bug is in the deferred clears of DXVK's own D3D9/D3D11 path.
This build contains only DXVK's DXGI. That DXGI presents images that
vkd3d-proton rendered, so the D3D12 path is not affected.

## Patches

The rule is to keep only the patches that the bridge, correctness or real
applications need. The prior-art vkd3d tree (`R/vkd3d`, 28 files,
+3916/−109) and its dxil-spirv diff were triaged hunk by hunk.

### Kept

| Patch | Source in prior art | Reason |
| --- | --- | --- |
| `vkd3d-proton/0001` root-signature overlap validation | `state.c`, F50 | Correctness. Native D3D12 rejects overlapping registers with shared visibility. The prior art fixed six conformance assertions per architecture. |
| `vkd3d-proton/0002` FORMAT_INFO plane counts | `device.c`, F51 | Correctness. Reports the DXGI plane layout without needing a Vulkan format. Fixed 60 assertions and advertises no new capability. |
| `vkd3d-proton/0003` depth in CPU-visible heaps | `resource.c`, F52 | Correctness. D3D12 allows depth textures in GPU-upload heaps; vkd3d-proton returned E_NOTIMPL. Only the depth hunks were kept; the 420/YUY2 conditions mixed into them were removed. |
| `vkd3d-proton/0004` LocalAppData shader cache | F45 (proven through a user-wide `VKD3D_SHADER_CACHE_PATH`) | Real apps. Under Program Files, standard users cannot write a cache to the working directory. Making `%LOCALAPPDATA%\vkd3d-proton` the default removes the need for an environment variable. `VKD3D_SHADER_CACHE_PATH` still overrides it. |
| `vkd3d-proton/0005` the bridge | `libs/d3d12/main.c`, `native_dxgi.cpp` | Rewritten; see "The bridge" below. |
| dxil-spirv bump to `8434b063` | submodule pin | Real apps (Godot D3D12). This is a pin, not a patch. |
| `dxvk/0001` keep the swap-chain description when a resize fails | `R/dxgi-validation.patch` | Correctness. When the presenter rejected `ResizeBuffers`, DXVK kept the rejected size and format in the swap chain. vkd3d-proton rejects a resize while back buffers are still referenced, so real apps can hit this. Native DXGI leaves the swap chain unchanged. This patch is not on the brief's list of keepers; it is kept as a correctness fix. |

### Dropped

| Prior-art change | Files | Reason |
| --- | --- | --- |
| YUY2, packed RGB (`R8G8_B8G8`, `G8R8_G8B8`) and opaque-420 emulation | `utils.c` format table, `device.c` format support and multisample checks, `resource.c`, `command.c`, `memory.c`, `meta.c`, `shaders/cs_packed_rgb.comp`, `vkd3d_private.h` | Side thread (brief) |
| XR-bias format and its swap-chain rejection | `utils.c`, `device.c`, `swapchain.c`, `resource.c` | Side thread (brief) |
| Sampler metadata and static-sampler plumbing for YUY2 direct reads | `state.c`, `dxil.c`, `raytracing_pipeline.c`, `workgraphs.c`, `resource.c` descriptor code, `include/vkd3d_shader.h`, and the 823-line dxil-spirv local diff | Side thread (YUY2). It needs the dxil-spirv local API (major version 3). |
| Sparse-feedback load changes | dxil-spirv local diff, `dxil.c` | Part of the same unexported diff; not on the keep list. |
| Yttrium private shared-memory contract and NT-handle open rework | `d3dkmt.c`, `resource.c` (`yttrium_memory_runtime_data_info`, `sType 1000001006`) | Tied to the Yttrium KMD's private ABI, for shared YUY2 resources |
| Copy-queue fallback and per-layer image-to-buffer copies | `command.c` | Serve the packed and opaque formats |
| `VKD3D_SHADER_DUMP_UNIQUE` and the shader quirk-hash bump | `vkd3d_shader_main.c` | Debugging aids |
| Demo and test changes | `demos/`, `tests/` | Not built here. Tests are disabled with `-Denable_tests=false`. |
| `VKD3D_TEST_CORE` and hard-coded `C:\Windows\System32\YttriumLab` paths | `libs/d3d12` | Replaced by the module-relative private directory |

## The bridge

### Runtime layout

The bridge finds its private directory **relative to its own file**. It calls
`GetModuleHandleExW(FROM_ADDRESS)` and `GetModuleFileNameW` on itself, takes
the directory, and appends `vmlab-win3d\`. It reads no registry value and no
environment variable, and hard-codes no Windows directory. Under WOW64, the
x86 bridge is loaded from `SysWOW64`, so it finds the x86 private directory.

| Path (x64; x86 uses `SysWOW64`) | Content | Comes from |
| --- | --- | --- |
| `C:\Windows\System32\d3d12.dll` | the bridge | `out/d3d12/x64/d3d12.dll` |
| `C:\Windows\System32\vmlab-win3d\vkd3dcore.dll` | vkd3d-proton core | `out/d3d12/x64/vkd3dcore.dll` |
| `C:\Windows\System32\vmlab-win3d\dxgi-vulkan.dll` | DXGI-only DXVK | `out/d3d12/x64/dxgi-vulkan.dll` |
| `C:\Windows\System32\vmlab-win3d\d3d12-native.dll` | the machine's own original `d3d12.dll` | copied by the installer **on the target machine** before it replaces `d3d12.dll` |

The same layout works app-locally for testing: put `d3d12.dll` next to an
executable and the `vmlab-win3d\` directory beside it. `test/run.sh` does
this.

Runtime dependencies outside this slice:

- `vulkan-1.dll`, the Khronos loader in `System32`/`SysWOW64` (vulkan-sdk
  slice). vkd3d-proton and DXVK load it at run time; they do not import it.
- A Vulkan ICD (the Venus ICD from the mesa slice).
- The system `dxgi.dll`, which `vkd3dcore.dll` imports. The system
  `D3D12Core.dll` is not used.

Notes for the installer:

- Preserve the native DLL as `vmlab-win3d\d3d12-native.dll` from the machine
  being installed. Never ship a Microsoft `d3d12.dll`. Uninstall restores it.
- The bridge works with any Windows 10 or 11 `d3d12.dll`, so a native-hash
  preflight is not needed. The prior-art installer checked hashes.
- `System32\d3d12.dll` is owned by TrustedInstaller. The prior art's
  take-ownership, ACL-grant and SDDL-restore sequence (prior art §4.4) applies.
  Windows servicing can replace the file, so the package needs a repair path.
- Leave `dxgi.dll` and `D3D12Core.dll` alone. Replacing `dxgi.dll` breaks
  Venus WSI presentation (prior art §3, §4.5).

### Exports

`libs/d3d12/native_exports.txt` (added by patch 0005) is the export table.
At build time, `gen_exports.py` turns it into the module definition, the
forwarding thunks (`bridge_exports.S`) and the import table that the thunks
use. Each export is one of three kinds:

- **vkd3d**: implemented by vkd3d-proton. These are `D3D12CreateDevice`,
  `D3D12GetDebugInterface`, `D3D12SerializeRootSignature`,
  `D3D12SerializeVersionedRootSignature`,
  `D3D12CreateRootSignatureDeserializer`,
  `D3D12CreateVersionedRootSignatureDeserializer`,
  `D3D12EnableExperimentalFeatures` and `D3D12GetInterface`.
- **forward**: ordinal 99 (no name), `SetAppCompatStringPointer`,
  `D3D12CoreCreateLayeredDevice`, `D3D12CoreGetLayeredDeviceSize`,
  `D3D12CoreRegisterLayers`, `D3D12PIXEventsReplaceBlock`,
  `D3D12PIXGetThreadInfo`, `D3D12PIXNotifyWakeFromFenceSignal`,
  `D3D12PIXReportCounter` and `GetBehaviorValue`.
- **data**: `D3D12DeviceRemovedExtendedData`. The bridge defines it as a
  zeroed `D3D12_VERSIONED_DEVICE_REMOVED_EXTENDED_DATA`, because the device
  is vkd3d-proton's and the native DRED block would never be filled.

A forwarded export is a `jmp` through a pointer table. On the first call, the
pointer resolves the export **by name** in `vmlab-win3d\d3d12-native.dll`
(ordinal 99 resolves by ordinal), stores the result and jumps there with the
caller's registers and stack untouched. Later calls go straight through.

The prior art used static PE forwarders to `C:\Windows\System32\YttriumLab\d3d12-native.*`.
Thunks replace them for three reasons:

- A static forwarder needs a path that can be resolved at load time. The
  private directory is next to the bridge, not on the DLL search path, and
  hard-coding `C:\Windows` breaks other Windows directories.
- A static forwarder to a missing export makes the importing module fail to
  load. With a thunk, a missing export returns the fallback value listed in
  `native_exports.txt` (`E_NOTIMPL` for HRESULT functions, 0 otherwise). On
  x86 the thunk also pops the right stdcall argument bytes. The bridge
  therefore loads on any Windows 10 or 11 build, including older ones that
  lack the PIX or layering exports.
- The native DLL loads only when a forwarded export is first called.

So the bridge is not tied to one `d3d12.dll` hash or one Windows build. It
depends only on the export **names**, which are stable. Run-time resolution by
name takes the place of reading the native export table during the build. A
Windows build needs no rebuild of the bridge; a new export name needs one line
in `native_exports.txt`.

**Ordinals.** The native ordinals differ between Windows builds. Only 99, 101
and 102 match in Win10 19045 and Win11 26100; in between, 100 is
`GetBehaviorValue` on Win10 and `SetAppCompatStringPointer` on Win11. A scan
of every DLL and EXE in `System32` and `SysWOW64` of both templates found
that Windows imports `d3d12.dll` by ordinal only for 101 and 102
(`d3d11on12`, `d3d9on12`, `dxdiagn`, `Taskmgr`, `DHolographicDisplay`,
`Windows.Mirage*` and others). Everything else is imported by name. The
bridge keeps 99, 101 and 102, and uses the Win11 26100 layout for the rest.

**Native tables compared.** `native-exports/` holds the export tables that
`check.sh` compares against. Each file records its source path, sha256 and
file version. The tables were read with `guestfish --ro` from the local
vmlab templates:

| File | Template | `d3d12.dll` version | sha256 |
| --- | --- | --- | --- |
| `win11-26100.1742.5-x64.txt` | `windows-11/26100.1742.5`, `System32` | 10.0.26100.9278 | `6b321815…15e4` |
| `win11-26100.1742.5-x86.txt` | `windows-11/26100.1742.5`, `SysWOW64` | 10.0.26100.9278 | `a43885b0…00af` |
| `win10-19045.2006.8-x64.txt` | `windows-10/19045.2006.8`, `System32` | 10.0.19041.5794 | `97879150…4f8f` |
| `win10-19045.2006.8-x86.txt` | `windows-10/19045.2006.8`, `SysWOW64` | 10.0.19041.5794 | `65ff1160…f1f0` |

Win11 26100 and Win10 19045 export the same 19 entries: ordinal 99 (no
name), the 17 functions above and `D3D12DeviceRemovedExtendedData`. The
Win11 hashes match the ones the prior-art installer pinned (prior art §4.4).
The x86 stdcall argument sizes in `native_exports.txt` come from the `ret N`
of each export in the x86 DLLs. Both builds agree.

### Core loading

The bridge loads `vmlab-win3d\vkd3dcore.dll` with
`LOAD_WITH_ALTERED_SEARCH_PATH`, so the core's dependencies resolve from the
private directory first and then from System32. Upstream also falls back to
`System32\D3D12Core.dll`. The bridge drops that fallback, because on Windows
that file is Microsoft's runtime.

If the core cannot be loaded, the vkd3d-proton exports call the same export
in `d3d12-native.dll`. The bridge then behaves like stock Windows rather than
failing every D3D12 call. The bridge logs this through vkd3d-proton's error
channel.

### DXGI routing

Applications create swap chains through the system `dxgi.dll`, and native
DXGI cannot present a vkd3d-proton queue (prior art §3: `E_NOTIMPL`). On the
first successful core load, the bridge creates a native DXGI factory and
patches four methods in the vtable that every native factory shares:
`CreateSwapChain`, `CreateSwapChainForHwnd`, `CreateSwapChainForCoreWindow`
and `CreateSwapChainForComposition`. It addresses them by field in
`IDXGIFactory2Vtbl`, not by raw index.

Each hook asks the device for `IDXGIVkSwapChainFactory`, which only
vkd3d-proton queues implement. If the device has it, the hook creates the
swap chain through `vmlab-win3d\dxgi-vulkan.dll`, loaded once on first use;
DXVK presents through vkd3d-proton's swap-chain factory. Any other device,
such as the KMD's D3D11 UMD or Venus WSI's own `DXGI_SHARED` swap chains,
calls the original native method. The bridge pins itself and `dxgi.dll` in
memory, because hooked factories outlive any caller.

Limits:

- Only the factory class returned by `CreateDXGIFactory2(0)` is hooked. A
  factory with a different vtable, if one exists (for example with
  `DXGI_CREATE_FACTORY_DEBUG`), is not.
- `D3D12DeviceRemovedExtendedData` stays empty, so the native debugger DRED
  extension shows nothing for vkd3d-proton devices.

## Verification

| What | How | Result |
| --- | --- | --- |
| Builds x64 and x86, imports, exports | `build/d3d12/build.sh` (runs `check.sh`) | Pass. Evidence: `out/d3d12/{x64,x86}/exports.txt`, `imports.txt`, `SHA256SUMS` |
| Forwarding thunks | `build/d3d12/test/run.sh`: `forward_test.exe` loads the bridge with a fake native DLL (`test/fake_native.c`) under Wine 11.17 | 11/11 checks pass on x64 and x86: ordinal 99 by ordinal, pointer, float and 64-bit return arguments, resolved-pointer reuse, `E_NOTIMPL` and 0 fallbacks with x86 stack balance, the data export, and native fallback without a core |
| Bridge, core and DXGI routing | Smoke run of the probes slice's `d3d12_probe.exe` under Wine and Xvfb on host RADV (scratch only, not committed) | The device was created by the private `vkd3dcore.dll` (FL 12_2, SM 6.6). `CreateSwapChainForHwnd` went through the hook into DXVK 3.1.1's DXGI, which created a vkd3d-proton swap chain. The shader cache went to `AppData\Local\vkd3d-proton\vkd3d-proton.d3d12_probe.exe.cache`. Xvfb has no DRI3, so presentation itself is not tested there. The Venus guest test belongs to the e2e slice. |
