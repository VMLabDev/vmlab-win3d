# D3D12 applications: Microsoft's DirectX-Graphics-Samples

Until this work, the only D3D12 application tested here was our own probe.
Microsoft's DirectX-Graphics-Samples need the MSVC ABI, so llvm-mingw cannot
build them (`docs/known-limits.md`). This page covers three things:

- how five unmodified samples are now built, with clang-cl against the MSVC
  CRT and the Windows SDK;
- how they ran on AMD1 and Nvidia1 through the installed package;
- the two failures found, their causes, and what a fix needs.

No patch came out of this work. Neither failure could be fixed inside the
time box, so both are reported with their cause and evidence.

## Results

Each sample ran in a fresh Windows 11 lab with the package installed,
started in the autologon user's session, captured on the console, and then
closed with `WM_CLOSE`. No DLL sat next to the executables: there was no
`D3D12\` Agility folder. Every graphics module loaded from System32 or
`System32\vmlab-win3d\`: `d3d12.dll` (the bridge), `vkd3dcore.dll`,
`dxgi-vulkan.dll`, `vulkan-1.dll` and `libvulkan_virtio.dll`. The samples
loaded no app-local modules.

| Sample | AMD1 | Nvidia1, after one more reboot | Nvidia1, first boot after the install |
| --- | --- | --- | --- |
| `D3D12HelloTriangle` | renders, exit 0 | renders, exit 0 | exit 0, but dwm crashed while it ran |
| `D3D12HelloTexture` | renders, exit 0 | renders, exit 0 | fails, `0xC0000409` |
| `D3D12Bundles` | renders, exit 0 | renders, exit 0 | fails, `0xC0000409` |
| `D3D12nBodyGravity` (compute) | renders, exit 0 | renders, exit 0 | fails, `0xC0000409` |
| `D3D12ExecuteIndirect` | **clear colour only**, exit 0 | **clear colour only**, exit 0 | window not visible, exit 0 |

- **"Renders"** means the console capture shows the sample's own picture:
  - the triangle;
  - the checkerboard texture;
  - the city of bundles;
  - the moving particle cloud.
- **Exit 0** is the sample's own exit code after `WM_CLOSE`.
  `tests/apps/d3d12/run.sh` sends the close from the user's session with
  `taskkill /im` (no `/f`).
- **`0xC0000409`** is `ucrtbase.dll` failing fast: a `ThrowIfFailed` in the
  sample threw, and nothing caught the exception.

Two failures remain:

1. [D3D12ExecuteIndirect draws nothing](#executeindirect-draws-nothing) on
   both GPUs. The cause is known: Venus lacks `VK_EXT_device_generated_commands`.
2. [On NVIDIA, dwm.exe crashes](#nvidia-dwm-crashes-in-the-first-boot-after-the-install)
   during the first D3D12 sample in the first boot after the install. The
   cause is open.

The first D3D12 process in a fresh lab also starts slowly. On AMD1,
`D3D12HelloTriangle` showed its window after about 15 s in one lab, and
after more than 45 s in another. After that first process, a sample's window
appears within 4 to 8 s.

## Building the samples

`build/d3d12-samples/build.sh` builds the samples in Docker, using the shared
toolchain image. `build/d3d12-samples/README.md` describes the build in
detail. In short:

| Input | Pin |
| --- | --- |
| Samples | `microsoft/DirectX-Graphics-Samples` `e5975f9b` |
| Compiler | clang 23.1.3 as clang-cl, and lld-link (llvm-mingw 20261006) |
| MSVC CRT, Windows SDK | 14.44.17.14 and 10.0.26100, fetched by xwin 0.10.0 from the Visual Studio 17.14.42 package manifest (pinned by sha256), checked by a hash over the whole tree |
| Shaders | DXC v1.8.2505.1 for Linux, running each project's own `dxc.exe` command line (SM 6.0) |
| Headers | the projects' NuGet packages: `Microsoft.Direct3D.D3D12` 1.618.3 and `WinPixEventRuntime` 1.0.240308001 |

`build-samples.py` reads each `.vcxproj` and carries out its `Release|x64`
configuration. No sample source is changed. The Agility SDK's
`D3D12Core.dll` is not deployed.

Two builds from the same pins gave identical files
(`build/SHA256SUMS-run-a.txt` and `-run-b.txt`).

The executables import `VCRUNTIME140.dll` and `MSVCP140.dll`. The guest gets
the Visual C++ 2015-2022 runtime 14.44.35211 from the pinned redistributable
(`tests/apps/d3d12/PINS`), installed system-wide as an application installer
would install it.

## Running them

```sh
tests/apps/d3d12/lab-up.sh .scratch/labs/amd1 win3d-<you>-amd1 AMD1   # fresh lab, desktop, install
tests/apps/d3d12/run.sh .scratch/labs/amd1 win11 <evidence-dir>       # all five samples
```

`lab-up.sh` writes the lab from `labs/probe/vmlab.wcl` with a NAT NIC added.
It then sets up the autologon desktop and installs `out/package/` with
`scripts/install-into-vm.sh`. `run.sh` writes, for each sample:

- `samples.json` (pass means exit 0, not killed, no app-local module, and a
  console capture exists);
- `<sample>.run.json` and `<sample>.modules.txt`;
- `console-<sample>.png`, a frame two thirds into the run;
- `<sample>_dxgi.log`, DXVK's log;
- `app-errors.txt`, the Application Error events from the run.

The script cannot tell whether a sample rendered correctly. Judge that from
the captures.

## ExecuteIndirect draws nothing

> **Fixed later** by `patches/vkd3d-proton/0006`, which emulates
> state-changing command signatures without device-generated commands
> ([ei.md](ei.md)). The rest of this section is the original finding.

`D3D12ExecuteIndirect` builds a command signature with two arguments: a root
constant-buffer view (`D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW`)
followed by a draw. Each of its 1024 triangles takes its offset, velocity,
colour and projection from its own constant buffer, chosen by that argument.
The window shows the clear colour and no triangles. The result is the same on
AMD1 and on Nvidia1 (`amd1/console-D3D12ExecuteIndirect.png`,
`nvidia1/console-D3D12ExecuteIndirect.png`).

### Cause

vkd3d-proton implements any command signature that changes state (root
constants, root descriptors, vertex or index buffer views) with
`VK_EXT_device_generated_commands`. At device creation it logs this
(`executeindirect/vkd3d-amd1-D3D12ExecuteIndirect.log`):

```
info:vkd3d_init_device_caps: Not all relevant pipeline stages are supported by EXT_dgc. Skipping.
```

Without the extension, `d3d12_command_signature_create`
(`libs/vkd3d/command.c`) still succeeds. It drops the state argument
("Device generated commands is not supported by implementation"), and
`ExecuteIndirect` then issues only the draws, pointed at the draw arguments.
The root CBV is never set, so every triangle reads an unbound constant
buffer. Its projection is zero and the triangle is degenerate. No error
reaches the application.

The host drivers have the extension. Venus does not pass it through:

| Where | `VK_EXT_device_generated_commands` | Evidence |
| --- | --- | --- |
| Host, RADV (Mesa 26.2.2), AMD1 | yes | `executeindirect/host-vulkaninfo-dgc.txt` |
| Host, NVIDIA 610.57.04, RTX 5090 | yes | same file |
| Guest, Venus (Mesa 26.1.0) on the RTX 5090 | **no** (165 device extensions, none of them DGC) | `executeindirect/guest-vulkaninfo-nvidia1.txt` |

Our Mesa tree (`arehnman/virtio-win-mesa` `374c3022`, Mesa 26.1.0) has no
device-generated-commands support anywhere under `src/virtio/`. Venus needs
protocol support for an extension in both the guest driver and the host
renderer, virglrenderer.

### What a fix needs

Either of these would fix it. Each is larger than this slice:

1. **DGC in Venus.** Add `VK_EXT_device_generated_commands` to the Venus
   protocol, the guest driver (Mesa) and the host renderer (virglrenderer
   1.3.0). Changing the host renderer is a host stack change, which D3
   rules out for now.
2. **An ExecuteIndirect path in vkd3d-proton that needs no DGC.** In this
   path, root parameters live in vkd3d-proton's push-UBO block
   (`VKD3D_ROOT_SIGNATURE_USE_PUSH_CONSTANT_UNIFORM_BLOCK`), with root
   descriptors as raw VAs. For each command, the existing patch shader
   (`cs_execute_indirect_patch.comp`) copies that command's argument words
   from the application's argument buffer into a per-command UBO slot, and
   copies its draw arguments into the same slot. It also honours the count
   buffer. The recording loop then binds each slot and issues
   `vkCmdDrawIndirect` per command.

   This path must change the shader interface for every root signature on
   a device without DGC, because the root signature is created before
   anything shows it will be used for ExecuteIndirect. It therefore needs a
   regression run across all D3D12 applications.

Until one of these lands, any D3D12 application whose command signatures
change root constants, root descriptors or vertex or index buffers will draw
wrongly, with no error.

## NVIDIA: dwm crashes in the first boot after the install

In a fresh Nvidia1 lab, in the session right after the install's final
reboot, `dwm.exe` crashed with heap corruption (`0xc0000374` in `ntdll.dll`)
while the first sample, `D3D12HelloTriangle`, was running; its window never
showed. In the same boot, the next three samples failed with `0xC0000409`,
and `D3D12ExecuteIndirect` never showed a window. The same thing happened in two separate fresh
Nvidia1 labs (`nvidia1-first-boot/app-errors.txt`; the first lab's run is
summarised in the table above).

After one more reboot, all five samples ran with no Application Error event
(`nvidia1/`). The two fresh AMD1 labs showed no dwm crash.

The crashed dwm had our D3D10/11 UMD `viogpu_d3d10.dll` loaded, along with
`d3d11.dll`, `dxgi.dll` and `dcomp.dll`. The WER report is
`nvidia1-dwm/dwm-Report.wer`. Its fault bucket is a stack hash, so it gives
no module.

What this points to, without proof:

- a heap overrun in the UMD's dwm-side handling of a surface first
  presented through the DXVK swap chain;
- or state left over from the driver installation that the extra reboot
  clears.

The next steps would be a full dwm dump (WER `LocalDumps` for `dwm.exe`) and
a run of `D3D12HelloTriangle` alone as the first program after a fresh
install. A user who hits this can reboot once more.

## Evidence

All under `docs/evidence/d3d12app/`:

| Path | What |
| --- | --- |
| `build/` | Build log (`case shim` lines), and the SHA256SUMS of two builds, identical |
| `amd1/` | Fresh AMD1 lab: the five samples, captures, `samples.json`, run and module lists, DXVK logs, `app-errors.txt` (none) |
| `nvidia1/` | Nvidia1, after one more reboot: the same set, all five run, no Application Error events |
| `nvidia1-first-boot/` | Nvidia1, first boot after the install: dwm crash, then three `0xC0000409` failures |
| `nvidia1-dwm/dwm-Report.wer` | WER report of that dwm crash, with its loaded modules |
| `executeindirect/` | vkd3d-proton log of `D3D12ExecuteIndirect` on AMD1, guest `vulkaninfo` (Nvidia1), host `vulkaninfo` lines for both drivers |

These runs used the bundle built from `82d8e51` (package 0.1.0) plus this
branch. The labs were `win3d-d3d12app-amd1` and `win3d-d3d12app-nv1`, on
template `x86_64/windows-11@26100.1742.5`. The NVIDIA card was Nvidia1,
because Nvidia2 already had three labs.

## What remains

- **ExecuteIndirect with state arguments**, as above. This is also an entry
  in `docs/known-limits.md`.
- **The NVIDIA first-boot dwm crash**, as above.
- **x86 builds.** The samples ship x64 and ARM64 configurations only, so
  this slice builds only x64.
- **The full `tests/e2e/run.sh` was not run for this branch.** The branch
  changes no package component, so there is no fix for it to prove.
