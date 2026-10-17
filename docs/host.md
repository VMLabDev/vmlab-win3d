# Host probe: Venus for a Windows 11 guest on this host

This page records what this host (helios) and vmlab v0.23.0-alpha need before a
Windows 11 guest from the local `windows-11` template gets a working Venus
device and a visible console. Every claim cites a file under
`docs/evidence/host/`. Probe date: 2026-10-17.

## Update: vmlab 0.24 (2026-10-17)

vmlab 0.24.0-alpha supersedes the workaround this page describes. It came
from handoff `20261017-142905`, which carried these findings to vmlab
(vmlab commits `147274f` and `cdf4ff9`). With 0.24:

| Workaround on 0.23 | What vmlab 0.24 does instead |
| --- | --- |
| `qemu_args`: `-global virtio-gpu-gl-device.blob=true` and `-vga none` | `gpu { mode = "vulkan" }` emits one adapter, `virtio-vga-gl,venus=true,blob=true,hostmem=4G` |
| `qemu_args`: `-display egl-headless,rendernode=/dev/dri/renderD129` | `gpu.address = "0000:72:00.0"` names the host GPU; vmlab resolves it through sysfs to its render node for egl-headless |
| An isolated supervisor started with `VK_DRIVER_FILES=…/radeon_icd.json` (`labs/probe/vmlab-amd.sh`) | vmlab clears the Vulkan loader variables for every QEMU and sets `VK_LOADER_DRIVERS_SELECT` from the driver of `gpu.address` in QEMU's own environment. `gpu.vulkan_icd = "<json>"` sets `VK_DRIVER_FILES` instead. Any supervisor works. |
| VNC capture with `labs/probe/tools/vnccap.py` | `vmlab vm screenshot`, `ocr` and `find-image` capture GL displays over VNC |
| Reading `/proc/<pid>/cmdline` and `environ` to see QEMU's line | `qemu.log` starts with `vmlab: argv:` and `vmlab: env:` lines |

`secure_boot = false` is still required, for test signing.

`labs/probe/vmlab.wcl` is now the plain form: `secure_boot = false` and
`gpu { mode = "vulkan" address = "0000:72:00.0" }`, with no `qemu_args`.
`labs/probe/vmlab-amd.sh` was removed, because it ran the isolated supervisor
on whatever vmlab binary is installed, and only the 0.23 binary needed it.

The measurements below were made on vmlab 0.23 and were not repeated on
0.24. They still explain why each setting matters, and the evidence
directories stay as they were. To repeat one of the environment variants
now, set `gpu.vulkan_icd` or leave out `gpu.address`, and read QEMU's
environment from `qemu.log`.

## Result

vmlab's plain `gpu { mode = "vulkan" }` does not work on this host. Five
changes make it work:

| Change | Why | Evidence |
| --- | --- | --- |
| `blob=true` on the GPU device | QEMU 11.1.1 refuses to start without it | `up-plain.txt` |
| No second display adapter (`-vga none`, or `virtio-vga-gl` instead of `virtio-gpu-gl`) | With q35's default std VGA also in the guest, Venus enumerates no device | `gpu-blob-memfd-amd/`, `gpu-gl-amd/`, `gpu-gl-novga-amd/`, `min-amd/` |
| `-display egl-headless,rendernode=/dev/dri/renderD129` | Without it EGL runs on an RTX 5090 and the console is black | `no-rn-amd/` |
| `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json` (or `VK_LOADER_DRIVERS_SELECT=*radeon*`) in QEMU's environment | Without it Venus exposes the RTX 5090s first. vkcube then fails to present and the console is black | `good-none/`, `min-none/`, `min-select/` |
| `secure_boot = false` | The template boots with Secure Boot on, which blocks test signing | `q1-guest-secureboot-default.txt` |

The memfd `share=on` RAM backend was **not** needed for vulkaninfo, vkcube or
the desktop (`no-memfd-amd/`, `min-amd/`).

On vmlab 0.23 the minimal working lab was `labs/probe/vmlab.wcl` with the
`min-amd` line in `qemu_args`, run through `labs/probe/vmlab-amd.sh`. That
script started an isolated supervisor with the ICD variable set, because a
0.23 lab file could not set QEMU's environment. See the update above for
the 0.24 form.

Screenshots: `vmlab vm screenshot`, `vmlab vm ocr`, `vmlab vm find-image` and
the wscript `screenshot()`/`ocr()` all fail with `no surface` once the console
shows the GL scanout. A VNC capture works, through either the VM's `vnc.sock`
or `vmlab console --tcp`. vkcube is visible in it (`min-amd/vnc-console-tcp-capture.png`).

## Host

| Item | Value |
| --- | --- |
| QEMU | 11.1.1 (`qemu-full 11.1.1-1`) |
| virglrenderer | 1.3.0 (`1.3.0-2.1`). Venus runs in `/usr/lib/virgl_render_server`, one forked `virgl-N-gpu_render` process per guest context |
| Mesa / RADV | 26.2.2 (`mesa 3:26.2.2-2`, `vulkan-radeon 3:26.2.2-2`) |
| NVIDIA | 610.57.04 |
| vmlab | 0.23.0-alpha (`/home/wil/dev/vmlab` at `2645da6`) |

Render nodes and ICDs (`/sys/class/drm/renderD*/device`, `nvidia-smi`):

| Node | PCI | GPU |
| --- | --- | --- |
| `renderD128` | 0000:01:00.0 | NVIDIA GeForce RTX 5090 (`/dev/nvidia0`) |
| `renderD129` | 0000:72:00.0 | AMD Granite Ridge iGPU (RADV RAPHAEL_MENDOCINO) |
| `renderD130` | 0000:05:00.0 | NVIDIA GeForce RTX 5090 (`/dev/nvidia1`) |

`/usr/share/vulkan/icd.d/` holds `nvidia_icd.json` and `radeon_icd.json`.

## Method

All probe labs ran on an **isolated vmlab supervisor**. `XDG_RUNTIME_DIR` was
`/tmp/win3d-hp-run` and `XDG_STATE_HOME` was this worktree's
`.scratch/state`. The user's supervisor was never touched. To change QEMU's
environment, the isolated supervisor was stopped and respawned with the
variable set. vmlab daemons are not dumpable, so their `/proc/<pid>/environ`
cannot be read. The isolated supervisor was therefore identified as the parent
of the probe lab's daemon. QEMU's own environment is readable and is recorded
per run in `qemu-env.txt`.

One lab, `win3d-host-probe-a`, was cloned once from
`x86_64/windows-11@26100.1742.5`, with no NIC. The guest was prepared once,
then the lab file was rewritten and the lab re-upped for each variant. The
scripts are in `labs/probe/guest/`:

1. `install-prereqs.ps1` (as SYSTEM) follows the Yttrium README:
   - `certutil -f -addstore Root` and `certutil -f -addstore TrustedPublisher`
     with `VirtIOTestCert.cer`;
   - `bcdedit /set testsigning on` and `powercfg /hibernate off`;
   - LunarG Vulkan runtime and SDK installs, both silent;
   - creates a local user `probe` with autologon.
2. Reboot.
3. `install-yttrium.ps1` runs
   `pnputil /add-driver …\Install_Debug\Win11\amd64\viogpu3d.inf /install`.
   Result: "Hardsoft VirtIO GPU 3D controller" 100.2026.9.1, Code 0. Then
   reboot.
4. Per variant, `probe.ps1` runs as `probe` in the interactive session
   (session 1) through a scheduled task registered with `schtasks /ru probe /it`.
   It runs `vulkaninfo --summary`, starts vkcube, waits six seconds and saves a
   guest-side `CopyFromScreen` PNG. The host then tries every capture path
   (Q4) while vkcube runs.

The host-side driver is `.scratch/variant.sh` → `.scratch/runprobe.sh`. It is
not committed, and its logic is described above.

### Probe material (downloaded, not committed)

| Asset | Source | sha256 |
| --- | --- | --- |
| `yttrium-viogpu3d-experimental-2026-09-27.zip` | `gh release download yttrium-experimental-2026-09-27 -R arehnman/yttrium-virtio-gpu -p yttrium-viogpu3d-experimental-2026-09-27.zip` | `215b4aafe459f3bb12b88dc5e4cd5b01853345f9760a1b3f65dd27b30ace098f` (matches GitHub's asset digest and the prior art's `yttrium.zip`) |
| `VulkanRT-X64-1.4.363.0-Installer.exe` | `https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/VulkanRT-X64-1.4.363.0-Installer.exe` (LunarG, the source the Yttrium README names; 1.4.363.0 was `latest` on 2026-10-17) | `3f03bda2522d1d10d05461ce8a47364f9af6f5dc8642b7c45e43943dba164303` |
| `vulkansdk-windows-X64-1.4.363.0.exe` | `https://sdk.lunarg.com/sdk/download/1.4.363.0/windows/vulkansdk-windows-X64-1.4.363.0.exe` (for `vkcube.exe`; the runtime ships only `vulkan-1.dll` and `vulkaninfo.exe`) | `94a82d378f7a5e3e54c9db7d2fb7016af136e14ac0a18dbf0f2f67a36352d141` |
| `VulkanRT-X64-1.4.363.0-Components.zip` (downloaded, not used) | same directory | `a25a927aa8b9f0371048f1861cf88ac3b9bc9b1fb332c42d897c8ab32695769a` |

The runtime installed `C:\Windows\System32\vulkan-1.dll` and `vulkaninfo.exe`,
both 1.4.363.0. The SDK installed `C:\VulkanSDK\Bin\vkcube.exe`. The SDK
install command was `--root C:\VulkanSDK --accept-licenses --default-answer --confirm-command install`.

### Where the QEMU command lines come from

vmlab writes only QEMU's stderr to `…/vms/<vm>/qemu.log`. It does not log the
argument vector of a QEMU that started. Each `qemu-cmdline.txt` is therefore
`/proc/<qemu pid>/cmdline`, read while the VM ran. The plain-mode line, which
never started, is quoted from QEMU's error in vmlab's `up` output and from
`src/qemu/cmdline.rs:579-586`.

## Q1: firmware, Secure Boot and TPM defaults

The template records only `profile = "windows-11"` and no firmware fields
(`~/.local/share/vmlab/templates/x86_64/windows-11/26100.1742.5/template.wcl`).
Those fields therefore come from the shipped `windows-11` profile: q35, `ovmf`,
`secure_boot = true`, `tpm = true`, `virtio-vga`, 4 vCPUs, 8 GiB.

A lab with no `secure_boot` line boots with Secure Boot and a TPM:

- QEMU runs `OVMF_CODE_4M.secboot.fd`, `-global driver=cfi.pflash01,property=secure,value=on`
  and `-device tpm-tis` (`qemu-cmdline-gpu-blob-securebootdefault.txt`).
- In the guest, `Confirm-SecureBootUEFI` returns `True` and `Get-Tpm` reports
  present and ready.
- `bcdedit /set testsigning on` fails with "The value is protected by Secure
  Boot policy" (`q1-guest-secureboot-default.txt`).

**A lab can set `secure_boot = false`.** `vmlab validate` accepts it. The same
clone, switched from on to off, then booted `OVMF_CODE_4M.fd` with no
`secure=on` (`qemu-cmdline-gpu-blob-secureboot-off.txt`), and `bcdedit` set
`testsigning Yes`. The TPM stays on, which is harmless.

## Q2: plain `gpu { mode = "vulkan" }`

**It does not start.** vmlab emits
`-device virtio-gpu-gl,venus=true,hostmem=4G -display egl-headless`, and QEMU
11.1.1 exits with:

```
qemu-system-x86_64: -device virtio-gpu-gl,venus=true,hostmem=4G: venus requires enabled blob and hostmem options
```

(`up-plain.txt`)

With only `blob=true` added (variant `gpu-blob`, `qemu_args = ["-global", "virtio-gpu-gl-device.blob=true"]`),
under a supervisor environment with no Vulkan variables (`gpu-blob-noenv/`):

- **Host GPU and ICD.** QEMU's EGL (egl-headless with no `rendernode`) opened
  `renderD130`, the second RTX 5090, plus `/dev/nvidia*`, `renderD128` and the
  NVIDIA GL cache (`qemu-fds.txt`). The Vulkan loader saw both ICDs. Without a
  filter, Venus contexts enumerate the RTX 5090s first: in `good-none/`, where
  vulkaninfo succeeded, GPU0 and GPU1 are "Virtio-GPU Venus (NVIDIA GeForce RTX
  5090)" and GPU2 is the AMD iGPU. The render server repeatedly logged
  `vkr: vkGetImageMemoryRequirements resulted in CS error`
  (`good-none/render-server-journal-summary.txt`). This is the same
  NVIDIA decoder failure the prior art recorded.
- **Guest.** Running as `probe` in session 1, vulkaninfo fails:
  `vkEnumeratePhysicalDevices failed with ERROR_INITIALIZATION_FAILED`.
  vkcube shows "vkEnumeratePhysicalDevices reported zero accessible devices"
  (`gpu-blob-amd/guest-capture.png`).
- **Why.** Two separate faults:
  1. **There are two display adapters.** `virtio-gpu-gl` has no VGA function,
     and vmlab does not pass `-vga none`, so q35 adds its default std VGA. The
     guest has "Microsoft Basic Display Adapter" (`PCI\VEN_1234&DEV_1111`)
     next to the Yttrium adapter (`videocontrollers.txt`). With it present,
     Venus enumerates nothing even when every Venus context runs on AMD
     (`gpu-blob-amd/`, `gpu-blob-memfd-amd/`, `gpu-gl-amd/`, all with
     `VK_DRIVER_FILES` set and render workers on `renderD129`). Removing it
     fixes enumeration (`gpu-gl-novga-amd/`, `min-amd/`).
  2. **The host GPU defaults to NVIDIA.** Venus on the RTX 5090 fails, as in
     the prior art. With the std VGA gone but no ICD filter (`min-none/`),
     vulkaninfo lists the NVIDIA devices first. vkcube picks GPU0 and fails:
     `wsi/win32: DXGI_SHARED CreateSwapChainForComposition failed (HRESULT 0x887a0005)`.
     The console is black.

## Q3: which additions are each necessary

The baseline was the prior art's known-good line (prior-art §2):
`display = "virtio-vga-gl,hostmem=4G,blob=on,venus=on"`, a memfd `share=on`
backend, `-display egl-headless,rendernode=/dev/dri/renderD129`, and
`VK_DRIVER_FILES=…/radeon_icd.json` in the isolated supervisor. Each variant
removed one item. "Venus" means vulkaninfo, as the interactive user, lists
"Virtio-GPU Venus (AMD Ryzen 9 9950X … (RADV RAPHAEL_MENDOCINO))".

| Variant (`docs/evidence/host/<dir>`) | Change from known-good | QEMU | Venus in guest | Console (VNC) | Verdict |
| --- | --- | --- | --- | --- | --- |
| `good-amd` | none | runs | yes, AMD | desktop + vkcube | works |
| `no-blob-amd` (no dir; QEMU never ran) | `blob` removed | **refuses**: "venus requires enabled blob and hostmem options" | — | — | **blob required** |
| `no-memfd-amd` | memfd backend removed | runs | yes, AMD | desktop + vkcube | memfd **not required** |
| `gpu-gl-amd` | `virtio-gpu-gl` instead of `virtio-vga-gl` | runs, std VGA added | **no** (INIT_FAILED) | std VGA head only | fails, but because of the extra VGA: |
| `gpu-gl-novga-amd` | `virtio-gpu-gl` + `-vga none` | runs | yes, AMD | desktop + vkcube | `virtio-gpu-gl` **is fine** without the std VGA |
| `no-rn-amd` | `rendernode=` removed | runs, EGL on `renderD130` (NVIDIA) | yes, AMD; vkcube renders (`guest-capture.png`) | **black** | rendernode **required for the console** |
| `good-none` | `VK_DRIVER_FILES` removed | runs | 3 devices, NVIDIA first; vkcube fails to present | **black** | **ICD choice required** |

Then the vmlab `gpu {}` block itself, with `qemu_args` additions:

| Variant | Line | Env | Venus | Console | Verdict |
| --- | --- | --- | --- | --- | --- |
| `gpu-blob-noenv` | gpu{} + `blob=true` | none | no | std VGA head | fails |
| `gpu-blob-amd` | gpu{} + `blob=true` | `VK_DRIVER_FILES` | no | std VGA head | fails (extra VGA) |
| `gpu-blob-memfd-amd` | + memfd | `VK_DRIVER_FILES` | no | std VGA head | fails (extra VGA) |
| **`min-amd`** | gpu{} + `blob=true` + `-vga none` + `egl-headless,rendernode=renderD129` | `VK_DRIVER_FILES` | **yes, AMD** | **desktop + vkcube** | **minimal working line** |
| `min-memfd-amd` | `min` + memfd | `VK_DRIVER_FILES` | yes | desktop + vkcube | memfd adds nothing visible |
| `min-none` | `min` | none | NVIDIA first | black | fails |
| `min-devsel` | `min` | `MESA_VK_DEVICE_SELECT=1002:13c0!` | **no**: host logs `vkr: venus driver is required to call vkEnumeratePhysicalDevices first` | — | device-select layer breaks Venus; not an alternative |
| `min-select` | `min` | `VK_LOADER_DRIVERS_SELECT=*radeon*` | yes, AMD | desktop + vkcube | works; an alternative to `VK_DRIVER_FILES` |

In every AMD-ICD run, `render-worker-fds.txt` shows each `virgl-N-gpu_render`
process holding only `/dev/dri/renderD129`. In the no-env runs, the workers
hold `renderD128`, `renderD130` and `/dev/nvidia*`.

The exact QEMU command line of the minimal working run
(`min-amd/qemu-cmdline.txt`) is:

```
qemu-system-x86_64 -name vmlab:win3d-host-probe-a/win11 -machine q35 -accel kvm -cpu host,vmx=off,svm=off -smp 4 -m 8192M -display none -vnc unix:/tmp/win3d-hp-run/vmlab/labs/win3d-host-probe-a/vms/win11/vnc.sock,share=ignore -qmp unix:/tmp/win3d-hp-run/vmlab/labs/win3d-host-probe-a/vms/win11/qmp.sock,server=on,wait=off -monitor none -serial file:/home/wil/dev/vmlab-win3d-wt/host-probe/.scratch/state/vmlab/labs/win3d-host-probe-a/vms/win11/serial.log -drive if=pflash,format=raw,readonly=on,file=/home/wil/.local/share/vmlab/guest/firmware/x86_64/OVMF_CODE_4M.fd -drive if=pflash,format=raw,file=/home/wil/dev/vmlab-win3d-wt/host-probe/.scratch/labs/a/.vmlab/vms/win11/OVMF_VARS.fd -chardev socket,id=vagent0,path=/tmp/win3d-hp-run/vmlab/labs/win3d-host-probe-a/vms/win11/agent.sock,server=on,wait=off -device virtio-serial-pci -device virtserialport,chardev=vagent0,name=vmlab.agent.0 -blockdev driver=qcow2,node-name=disk0,file.driver=file,file.filename=/home/wil/dev/vmlab-win3d-wt/host-probe/.scratch/labs/a/.vmlab/vms/win11/disk0.qcow2 -device virtio-blk-pci,drive=disk0 -nic none -chardev socket,id=chrtpm,path=/tmp/win3d-hp-run/vmlab/labs/win3d-host-probe-a/vms/win11/tpm.sock -tpmdev emulator,id=tpm0,chardev=chrtpm -device tpm-tis,tpmdev=tpm0 -device virtio-gpu-gl,venus=true,hostmem=4G -display egl-headless -usb -device usb-tablet -S -global virtio-gpu-gl-device.blob=true -vga none -display egl-headless,rendernode=/dev/dri/renderD129
```

with `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json` in its
environment (`min-amd/qemu-env.txt`). The second `-display` overrides vmlab's
own: QEMU's EGL then holds `renderD129` (`min-amd/qemu-fds.txt`).

On memfd: the Yttrium README calls the shared memory backend essential. Its
absence did not change anything observable here (vulkaninfo, vkcube, desktop
composition through the Yttrium D3D UMD). D3D9-12 and OpenGL workloads were
not run in this probe. If a later slice finds a workload that needs it, the
`min-memfd-amd` line is known to work.

## Q4: console screenshots with Venus active

Every vmlab screenshot path goes through QMP `screendump`. All of them fail
when the console shows the virtio-gpu GL scanout:

| Path | Result with `min-amd` (and every working variant) |
| --- | --- |
| `vmlab vm screenshot win11 <png>` | `qmp command failed: GenericError: no surface`, exit 1 (`min-amd/vmlab-screenshot.txt`) |
| `vmlab vm ocr win11` | same error |
| `vmlab vm find-image win11 <ref.png>` | same error |
| wscript `m.screenshot("")` and `m.ocr()` (`labs/probe/scripts/shot.ws` via `vmlab script`) | `screenshot failed: qmp command failed: GenericError: no surface`, same for OCR |
| `vmlab console --tcp win11` + an RFB client (`labs/probe/tools/vnccap.py 127.0.0.1:5900 out.png`) | **works**: `vnc-console-tcp-capture.png` |
| RFB client straight on `$XDG_RUNTIME_DIR/vmlab/labs/<lab>/vms/<vm>/vnc.sock` | **works**: `vnc-capture.png` |
| `vmlab console win11` with a viewer | not run; no VNC viewer is installed on this host. It uses the same socket |

`vmlab vm screenshot` "succeeds" only in the broken variants. There, q35's std
VGA is console 0, and the capture shows that second head, with no taskbar and
no vkcube (`gpu-blob-noenv/vnc-capture.png` against `gpu-blob-amd/guest-capture.png`).

egl-headless renders the scanout as a GL texture and serves VNC from a
read-back copy. QMP `screendump` asks the console for a 2D surface, which a GL
scanout console does not have. So vmlab's display verbs need a VNC-based
capture whenever `gpu {}` is `virgl` or `vulkan`.

## Q5: does vkcube render visibly in the console

Yes. `min-amd/vnc-console-tcp-capture.png` (through `vmlab console --tcp`) and
`min-amd/vnc-capture.png` (through `vnc.sock`) show the textured LunarG cube
spinning in its window on the Windows desktop. The same holds for `good-amd`,
`no-memfd-amd`, `gpu-gl-novga-amd`, `min-memfd-amd` and `min-select`.
vkcube's stderr names the device: `Selected GPU 0: Virtio-GPU Venus (AMD Ryzen
9 9950X 16-Core Processor (RADV RAPHAEL_MENDOCINO))` (`min-amd/vkcube-err.txt`).

## Q6: the QEMU 11.1.1 libgallium SIGSEGV at shutdown

**It still happens.** The lab stopped 18 times. Two of those stops ended in
`signal: 11 (SIGSEGV) (core dumped)`, at 14:00:48 and 14:13:15 AEST
(`q6-shutdown-crashes.txt`). The kernel log shows the same fault the prior art
recorded:

```
qemu-system-x86[997310]: segfault at 4 ip 00007fa8ec7c7eaa … in libgallium-26.2.2-arch3.2.so[7c7eaa,…]
```

The two crashes were the stops of `good-none` and `min-none`. In those runs
EGL ran on AMD radeonsi (`rendernode=renderD129`) while Venus ran on NVIDIA
and logged CS errors. No stop crashed among the seven runs with AMD on both
sides (`good-amd`, `no-memfd-amd`, `gpu-gl-amd`, `gpu-gl-novga-amd`,
`min-amd`, `min-memfd-amd`, `min-select`). That includes a `vmlab down` with
vkcube still running. The remaining stops had EGL on NVIDIA, which does not
load libgallium. The prior art saw the crash
after heavy D3D12 work on AMD alone (prior-art §3), so a clean AMD run here
does not prove AMD-only shutdowns are safe.

**It does not lose data:**

- In QEMU 11.1.1's `qemu_cleanup()` (`system/runstate.c`, read in the prior
  art's `qemu-cleanup-source` checkout of v11.1.1), `vm_shutdown()`,
  `bdrv_drain_all_begin()`, `bdrv_close_all()` and `tpm_cleanup()` all run
  before `qemu_display_cleanup()`. The prior art's stack shows the crash
  inside `qemu_display_cleanup()` → `egl_cleanup()`. By then the guest has
  stopped and all disks and pflash (OVMF vars) are flushed and closed.
- The guest had already shut down through the agent.
- The next boot was clean every time (the run after each crash booted and
  probed normally).

**It is more than a log line.** `vmlab down` prints success and exits 0 while
the daemon records `vm.stopped` with `status: signal: 11 (SIGSEGV)`.
systemd-coredump writes a core on each occurrence. Scripts that trust
`down`'s exit code will not notice. Upstream QEMU commit
`baca25172d8c195b9abbe6f8a3ab5915dba09a91` (EGL torn down before GBM) fixes
it, per prior-art §3 and F61. Per decision D3, the host stays on 11.1.1.

## Other things learned

- **Vulkan needs an interactive session.** The probe used an autologon local
  user and a scheduled task with `/it`. `vmlab exec --user` gives a minted
  network logon, not a desktop session.
- **`vmlab up` can return before the agent answers** after a reboot
  (`win11: the guest's vmlab-agent … has not answered yet`). Scripts should
  poll `vmlab exec … cmd /c ver` before copying files.
- **Agent update warning.** Every `up` warned that the template's agent
  (`f1e4515`) could not be updated to `2645da6` in place. It was harmless.
- **Probe-script pitfalls (Windows PowerShell 5.1).** `Out-File` defaults to
  UTF-16, so use `-Encoding ascii`. `query user` exits 1 even when it
  succeeds, which defeats `set -o pipefail` checks.
- **The PCI ID is the same either way.** `virtio-vga-gl` and `virtio-gpu-gl`
  both present `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4`, so the installed
  Yttrium driver binds to either without reinstalling.

## Teardown

The probe lab `win3d-host-probe-a` was destroyed with `vmlab destroy`. Its
isolated supervisor was stopped and `/tmp/win3d-hp-run` removed. The user's
supervisor and other labs were not touched. `labs/probe/vmlab.wcl` passes
`vmlab validate`. At the time, its line was the `min-amd` variant, which
was booted and probed. The committed lab itself was not booted, because its name and
directory differ from the probe lab.
