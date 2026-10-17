# Prior art: the `/home/wil/dev/virtio` Windows 3D experiments

This review covers the codex session's work in `/home/wil/dev/virtio` between
2026-10-15 and 2026-10-17. It is written for vmlab-win3d. It records what that
work proved, what it got wrong, and what can be reused. It does not continue
any of that work.

## Path abbreviations

Citations use these prefixes:

| Prefix | Path |
| --- | --- |
| `V/` | `/home/wil/dev/virtio/` |
| `P/` | `/home/wil/dev/virtio/.scratch/prototype/` |
| `R/` | `/home/wil/dev/virtio/.scratch/prototype/driver-integrated-3d/20261016-001605-8bac/` |
| `NW` | `R/NATIVE-WDDM20.md` (finding numbers F1–F237, line numbers given) |
| `HO` | `V/.scratch/handoff/20261017-021318-windows-3d-paused.md` |

Status markers:

- **proven**: the notes cite evidence, such as a screenshot, log, archive or
  hash, and that evidence exists on disk.
- **claimed**: the notes state it, but this review found no evidence for it.
- **overturned**: a later finding contradicts it.

"Original lab" means vmlab lab `virtio-pr943`. "Clean lab" means
`virtio-clean-install`. Both are paused, and neither is ours.

---

## Summary

1. **The prior stack is not this repo's target stack.** The session never
   built "KMD → Venus ICD → DXVK, vkd3d-proton and Zink" system-wide. It used
   **Yttrium**, an alpha third-party stack by Åke Rehnman. Yttrium has a
   virtio-gpu WDDM KMD (`arehnman/yttrium-virtio-gpu`, a fork of
   virtio-win/kvm-guest-drivers-windows). It also has a Mesa fork
   (`arehnman/virtio-win-mesa`, branch `yttrium-experimental`). In that fork:
   - D3D9, D3D10 and D3D11 are a **native Gallium UMD** (`viogpu_d3d10.dll`).
     It translates shaders to SPIR-V and carries the work over its own
     embedded Venus transport.
   - OpenGL is the Mesa WGL ICD with **Zink** (`viogpu_wgl.dll`).
   - Vulkan is **Venus for Windows** (`libvulkan_virtio.dll`). It talks to the
     KMD through D3DKMT escapes.

   Only D3D12 went through vkd3d-proton, by a system32 `d3d12.dll`
   replacement. The session made no DXVK-for-D3D9/10/11 system-wide stack in
   its final state. (proven: `R/VERDICT.md` "Result" and "Sources";
   `NW` F204 line 5235)
2. **What rendered.** In a **fresh clean lab from the vmlab `windows-11`
   template (26100.1742.5)**, a packaged installer produced visible rendering
   with no app-local DLLs. It covered GravityMark D3D11, D3D12 and OpenGL, the
   D3D12 triangles (x86 and x64), and Godot D3D12. D3D9 (Heaven), D3D10
   (triangle) and Vulkan (vkcube and vulkaninfo) were proven only in the
   heavily modified original lab. **dxdiag was never run.** No Windows 10 test
   exists. (proven: `NW` F203–F204 lines 5208–5262, `HO` lines 88–95)
3. **Signing.** The session created a self-signed code-signing cert inside the
   Windows guest (`New-SelfSignedCertificate`, key non-exportable) and signed
   with EWDK `signtool` and `Inf2Cat`, also in the guest. The cert was trusted
   through `certutil -f -addstore Root|TrustedPublisher`. It set
   `bcdedit /set {current} testsigning on`, and Secure Boot was off through
   vmlab `secure_boot = false`. The signing key lives only in the paused
   original-lab VM, so it is **not reusable**. (proven: `R/sign-kmd-r10.ps1`,
   `R/windows-installer.Vjgbml/Set-YttriumTestSigning.ps1`, `NW` F92 line 2211)
4. **Install.** `Install.cmd` runs `Setup-Yttrium.ps1`. That script backs up
   state, enables test signing, and reboots. A SYSTEM startup task then runs
   `pnputil /add-driver viogpu3d.inf /install`, deploys the UMDs, writes the
   registry and replaces `System32\d3d12.dll`, then reboots again and
   verifies. Two reboots, no host commands. Uninstall and reinstall were also
   proven. (proven: `NW` F207–F214 lines 5302–5484)
5. **Biggest trap for the target stack.** Mesa's Win32 Vulkan WSI presents
   through the **system `d3d11.dll` and `dxgi.dll`**, and therefore through
   the KMD-registered D3D11 UMD. When `System32\dxgi.dll` was replaced
   globally, even vkcube hung. A global DXVK `dxgi.dll` or `d3d11.dll` would
   therefore break Vulkan presentation unless that WSI changes. (proven:
   `P/windows-global-3d/20261015-222344-d503/VERDICT.md` lines 64–90; source
   `R/mesa/src/vulkan/wsi/wsi_common_win32.cpp:1477` and `:1761`)
6. **DXVK on this stack.** App-local DXVK 3.1.1 rendered D3D9/10/11 triangles,
   Heaven D3D9 and D3D11, and GravityMark D3D11. It has a reproducible
   **clear-only black frame** in DXVK 3.0.2 and 3.1.1, which does not occur in
   2.7.1. The cause is deferred-clear materialisation in DXVK's Windows
   presentation path. DXVK also mishandled one window resize. System-wide
   DXVK D3D9, by replacing System32 and SysWOW64 `d3d9.dll`, rendered Heaven.
   (proven: `P/dxvk-vulkan-capture/20261015-204409-3a72/VERDICT.md` lines
   18–100; `P/windows-global-3d/…/VERDICT.md` lines 45–62)
7. **Host requirements.** It works on the **AMD iGPU (RADV, `renderD129`)**
   only. On the RTX 5090 (NVIDIA 610.57.04), the guest desktop failed before
   login. The QEMU line was `virtio-vga-gl,hostmem=4G,blob=on,venus=on` plus a
   shared memfd RAM backend plus `-display egl-headless,rendernode=…`. This
   was passed through the raw vmlab `display` and `qemu_args` fields, not
   `gpu { mode = "vulkan" }`. The supervisor also needed
   `VK_DRIVER_FILES=…/radeon_icd.json`. (proven: `R/clean-install-lab.WlATCI/
   vmlab.wcl`, `vmlab-clean.sh`; `P/dxvk-vulkan-capture/…/VERDICT.md` lines
   132–160)
8. **Still open at pause.**
   - Presentation is poor. D3D11 spends about 33 ms per Present on a composed
     GPU-GDI copy, for about 19 FPS. D3D12 drops about 40 % of its presents.
   - Host-console delivery was never measured.
   - The QEMU 11.1.1 libgallium SIGSEGV at shutdown is fixed only in a
     lab-local QEMU build.
   - The exported patches are stale relative to the working trees.

   (`HO` lines 163–245; `NW` F233–F237)

---

## 1. Component map

Target-stack position uses this repo's stack: (1) KMD → (2) Venus Vulkan ICD →
(3) D3D/GL translation (DXVK, vkd3d-proton, Zink).

| Component | What it is | Source, pin | Target-stack position | Status |
| --- | --- | --- | --- | --- |
| **PR 943 "community viogpu3d"** | The original virtio-win PR 943 driver package: viogpu3d KMD plus Mesa 23.3.0-devel virgl UMDs. It loads as "Red Hat VirtIO GPU 3D controller" 100.6.101.58000 and uses **virgl**, not Venus. | Community zip attached to PR 943 (comment 1761795174). SHA256 `24aa30b2…e0` (`P/pr943-win11/20261015-192630-eb3e/hashes.txt:1`). The exact KMD commit is unknown (`V/.scratch/research/pr943-win11/20261015-192630-78b8/findings/q01-prerequisites.md`). | Superseded: a virgl, not Venus, KMD | proven (D3D FL10.0 readback and Present pass; GL fails 887a002d) `P/pr943-win11/…/proof.txt` |
| **Yttrium KMD** (`viogpu3d.sys`, "Hardsoft VirtIO GPU 3D controller") | WDDM KMD with Venus and blob support, a 60 Hz VidPN timer, and present blits. Reports WDDM 2.0. Package includes `viogpuap.exe`, `vgpusrv.exe`/`viogpusc`. | `https://github.com/arehnman/yttrium-virtio-gpu`, tag `yttrium-experimental-2026-09-27` = `b99d951a1c97f6fb3a1adbc08a4d2ab9ce9f5a15` (worktree `R/yttrium-kmd-release`). Newer checkout `1d3e6800b84b7f827654bfdedd95c9d582d07e8b` (`R/yttrium-kmd`), README-only delta. BSD-3 upstream, MPL-2.0 for Yttrium parts. | (1) KMD | proven: running image hash matches the Sept release (`NW` F82 line 1918) |
| Yttrium KMD **r10** | The release KMD plus `R/kmd-r10-copy.patch` (10-bit → 8-bit CPU blit conversion). Built **Win11 Debug x64**. Signed hash `3AEF2AB1…C26C`. This is the kernel inside the v13–v18 installers. | base `b99d951a`, patch `R/kmd-r10-copy.patch` (91 lines) | (1) | proven `NW` F91–F93 lines 2177–2265 |
| **Yttrium D3D UMD** `viogpu_d3d10.dll` | Mesa `gallium/frontends/d3d10umd` (D3D10/11 DDI plus D3D9 DDI in `D3D9*.cpp`) on the `gallium/winsys/yttrium/gdi` driver. Shaders go TGSI → NIR → SPIR-V and are submitted through an *integrated* Venus transport (`yttrium_venus2_*`). It does **not** load the Vulkan ICD DLL. Registered three times in `UserModeDriverName` (D3D9, D3D10, D3D11). | `https://github.com/arehnman/virtio-win-mesa.git`, branch `yttrium-experimental` = `374c30222e0c08b04783c173c127abb3e465d028` (Mesa 26.1.0), plus local changes (43 files, +1793/−161) in `R/mesa`. Yttrium files MPL-2.0, Mesa MIT. | Outside target. The target uses DXVK for D3D9–11. **But** it is the D3D11 device Venus WSI presents through (section 4). | proven `NW` F204 line 5249 (source trace: `d3d10_create_screen` → `yttrium_gdi_screen_create` → `yttrium_venus_create`) |
| **Venus Vulkan ICD** `libvulkan_virtio.dll` | Mesa `src/virtio/vulkan` with a Windows renderer `vn_renderer_d3dkmt.c`, which submits through `D3DKMTEscape` (`VIRTGPU_SUBMIT_VIA_ESCAPE`). Win32 WSI in `src/vulkan/wsi/wsi_common_win32.cpp`. Reports Vulkan 1.4.334, `DRIVER_ID_MESA_VENUS`. | Same Mesa tree. Local patch adds opaque NT memory export/import and filters the guest Win32 extension (`NW` F70, `V/EXPERIMENTS.md` lines 402–416). | (2) Venus ICD | proven: vulkaninfo lists `Virtio-GPU Venus (AMD Ryzen 9 9950X … (RADV RAPHAEL_MENDOCINO))` (`P/windows-vulkan/20261015-195348-ee5d/vulkan-interactive-amd.txt`). vkcube visible (`…/cube-amd.png`). |
| **OpenGL ICD** `viogpu_wgl.dll` | Mesa WGL Gallium ICD, `-Dgallium-drivers=zink`. Zink runs on the Venus ICD through the Vulkan loader. | Same Mesa tree | (3) Zink: **matches the target** | proven: GravityMark GL log names Zink, Venus and RADV (`NW` F204). Blender EEVEE renders (`R/blender-eevee.png`). |
| `yttrium12.dll` | A D3D12 UMD slot in the release INF (4th `UserModeDriverName` entry). The KMD build only *stages* an externally supplied `YTTRIUM12_DLL_x64`. | `R/yttrium-kmd-release/viogpu/viogpu3d/viogpu3d.vcxproj:466` | Not a real D3D12 UMD | proven absent: no `OpenAdapter12`, DX12 UMD query returns `0xc000000d` (`NW` F133 line 3482; `R/umd-discovery-x86_64.txt:3`) |
| **vkd3d-proton** (`vkd3dcore.dll`) | vkd3d-proton 3.0.1 core, x64 and x86, with large local changes: YUY2/packed-RGB/XR formats, root-signature validation, shared NT handles. dxil-spirv bumped to `8434b0639fb2a48ce13aac08a420ecce5d23df0e` **with an extra 823-line local diff**. | `https://github.com/HansKristian-Work/vkd3d-proton` `3b10bd7a7ec6a7347e616cf8bea59333afec2255` (`R/vkd3d`). LGPL-2.1. | (3) vkd3d-proton: **matches the target** | proven (D3D12 apps render, `NW` F203–F204) |
| **"D3D12 bridge"** (`System32\d3d12.dll`, `SysWOW64\d3d12.dll`) | vkd3d-proton's `libs/d3d12` loader, patched. It loads the core from `System32\YttriumLab\vkd3dcore.dll` (`R/vkd3d/libs/d3d12/main.c:125`). `native_dxgi.cpp` hooks four methods on the **native** DXGI factory vtable (CreateSwapChain*). It routes only vkd3d queues to a private DXVK DXGI, `YttriumLab\dxgi-vulkan.dll`. It keeps the 19 native export ordinals and forwards the rest to a preserved `d3d12-native.dll`. | `R/vkd3d/libs/d3d12/main.c`, `R/vkd3d/libs/d3d12/native_dxgi.cpp` (158 lines, **untracked**) | (3) system-wide D3D12 registration mechanism | proven `R/VERDICT.md` lines 81–99. **Installed bridge binaries do not match rebuilds**, so their provenance is unverified (`NW` F187–F188 lines 4829–4875). |
| **DXVK** | (a) Private `dxgi-vulkan.dll`: DXVK 3.1.1 DXGI only, built with D3D8/9/10/11 disabled, plus a resize-validation patch. (b) Earlier app-local DXVK 3.1.1/3.0.2/2.7.1 release binaries for D3D9–11. (c) Earlier global DXVK `d3d9.dll`. | `https://github.com/doitsujin/dxvk` `b1a1c99ab52b687cf950d62c88bc2fa316b41663` (v3.1.1) (`R/dxvk`), patch `R/dxgi-validation.patch`. zlib licence. | (3) DXVK: **matches the target** | (a) proven (D3D12 presents through it), (b)(c) proven in sibling prototypes (section 2) |
| **QEMU** (host) | System QEMU 11.1.1 (Arch `qemu-full 11.1.1-1`). A lab-local QEMU v11.1.1 build with the upstream EGL/GBM teardown-order fix (`baca25172d8c195b9abbe6f8a3ab5915dba09a91`). | `R/qemu-cleanup.patch` on base `c3d48b7d1e89604920e5b81b91140c2ad39a1943`. Binary SHA256 in `R/qemu-cleanup-sha256.txt`. | Host | proven `NW` F61 line 1303 |
| virglrenderer / host Mesa | virglrenderer 1.3.0 (Arch `1.3.0-2.1`), Mesa/RADV 26.2.2 | Arch packages | Host | proven `R/VERDICT.md` lines 31–40 |
| Yttrium QEMU / Flatpak / virglrenderer forks | `arehnman/yttrium-qemu` (branch `yttrium`), `arehnman/yttrium-qemu-flatpak`, `arehnman/virglrenderer` (branch `yttrium`). QEMU carries a "display progress" patch so a hidden display does not stall the guest. | `R/yttrium-kmd-release/README.md` "Components" and "Linux host setup" | Host alternatives | claimed: **never used**; stock Arch packages were used |
| Godot 4.7.2 source | Application cleanup fixes only. Not a driver. | `R/godot-source` `ed1daf0b…`, `R/godot-cleanup.patch` | Not relevant | proven, but irrelevant (`NW` F75–F79) |

---

## 2. What was proven to work

Unless noted, everything ran on **Windows 11 Enterprise Evaluation build 26100
x64**. The template was `ghcr.io/vmlabdev/vmlab-templates/windows-11:26100.1742.5`,
the same version as the local `~/.local/share/vmlab/templates/x86_64/windows-11/26100.1742.5`.
The VM had 4 vCPUs and 8 GiB, `secure_boot = false`, and test signing on. The
GPU line was `display = "virtio-vga-gl,hostmem=4G,blob=on,venus=on"` with
`qemu_args = ["-object","memory-backend-memfd,id=venusmem,size=8G,share=on","-machine","memory-backend=venusmem","-display","egl-headless,rendernode=/dev/dri/renderD129"]`.
The host was an AMD Ryzen 9 9950X iGPU on RADV. Sources:
`R/clean-install-lab.WlATCI/vmlab.wcl`, `P/pr943-win11/20261015-192630-eb3e/vmlab.wcl`.
**Windows 10 was never tested.** **vmlab `gpu { mode = "vulkan" }` was never used.**

### 2.1 Clean lab, packaged installer (the strongest evidence)

The installer was v13 for the matrix and v16–v18 for the later reruns. It ran
in a fresh guest from the template under a standard user, `yttrium-test`. App
directories had no local graphics DLLs, and graphics environment variables
were absent.

| API / app | Path | Result | Best evidence | Status |
| --- | --- | --- | --- | --- |
| D3D12 triangle x64 and x86 | bridge → vkd3d → Venus | Visible, exit 0 | `R/clean-retry-frames.Moeywc/clean-installed-r13-vcrt-d3d12-2.png`, `…-d3d12-32-8.png`; `NW` F204 line 5235 | proven |
| GravityMark 1.89 D3D12 | same | Visible, 25.5 FPS overlay, exit 0 | `R/clean-gravity12-live.png`; `NW` F203 line 5208 | proven |
| GravityMark D3D11 | Yttrium native D3D11 UMD | Visible. 1.5 FPS at v13, 18 FPS at v18. PresentMon at v18: 19.11 presents/s, 33 ms in Present. | `R/clean-gravity11-live.png`; `NW` F232–F233 lines 5929–6000 | proven (slow) |
| GravityMark OpenGL | WGL → Zink → Venus | Visible, 60.1 FPS. Shutdown hang fixed in v15/v16. | `R/clean-gravitygl-live.png`; `NW` F204, F219, F222 | proven |
| Godot 4.7.2 D3D12 | bridge | Visible, exit 0. Leaks are app bugs (F223). | `R/clean-retry-frames.Moeywc/clean-installed-r13-vcrt-godot12-2.png` | proven |
| Module load is system-wide | — | Module lists show System32/SysWOW64 bridge, YttriumLab cores, DXGI, Venus | `R/clean-installed-r13-vcrt.zip`; `NW` F202 line 5172 | proven |
| Install, reinstall, uninstall, upgrade | `Install.cmd` / `Uninstall.cmd` | Pass after reboots | `R/packaged-install-verification.json`, `R/packaged-uninstall-verification.json`, `R/active-upgrade-verification.json`, `R/indirect18-upgrade-verification.json` | proven |

The clean lab did **not** test D3D9, D3D10, Vulkan apps, vulkaninfo or dxdiag.
`HO` lines 233–245 lists what remained unproven.

### 2.2 Original lab (`virtio-pr943`, accumulated experimental state)

| API / app | Path | Result | Best evidence | Status |
| --- | --- | --- | --- | --- |
| vulkaninfo, vkcube | Venus ICD | Venus device listed, cube visible | `P/windows-vulkan/20261015-195348-ee5d/vulkan-interactive-amd.txt`, `cube-amd.png`, `vulkan-repeat.txt` | proven |
| D3D9, D3D10, D3D11 triangles, x86 and x64 | native Yttrium | All six visible | `P/dxvk-vulkan-capture/20261015-204409-3a72/VERDICT.md` line 23; `R/native-results/` | proven |
| Heaven 4.0 D3D9 | native Yttrium | Initially crashed (exit 3, `P/dxvk-vulkan-capture/…/VERDICT.md` line 52), then **fixed**: blend max_rt, sampler, RGBA4, depth bias, staged-draw. Full menu and scene by build staged-draw-0212. | `R/xr-heaven-late-pause.zip` (`NW` F102 line 2488); `V/EXPERIMENTS.md` lines 76–85 | proven (earlier failure **overturned**) |
| Heaven D3D11 | native Yttrium | Visible | `R/final-heaven11.png`, `R/final3-results/` | proven |
| Heaven OpenGL | Zink | Visible **without** the GLSL midshader override once profile matching was fixed (F2) | `R/native-heaven-opengl.png`; `R/VERDICT.md` line 111 | proven (earlier "needs env override" **overturned**) |
| Heaven dark frames | — | A WARP reference shows the same dark transition. It is application behaviour. | `R/heaven-warp-layout.zip`; `NW` F85 line 2014 | proven (an earlier "driver defect" suspicion was **overturned**) |
| Blender 4.5.9 EEVEE | Zink | Render completes | `R/blender-eevee.png`, `R/final-blender.txt` | proven |
| Godot Vulkan | Venus | Visible, exit 0 | `R/hook-results/final-godot-vulkan-5.png` | proven |
| Godot D3D12 Forward+ / mobile | bridge | First failed 0xc0000409 (Venus ring watchdog after a host RADV crash on invalid SPIR-V). Rendered after the dxil-spirv bump. | `R/compat-final.zip`; `R/COMPATIBILITY.md` "Godot D3D12 investigation" | proven (failure **overturned**) |
| GravityMark D3D11 | native | Black and host GPU resets (F18–F35), then rendering after the GPU-written constant-buffer fix and max-RT fix | `V/EXPERIMENTS.md` lines 55–73; `R/constant-regression-gravity11-live.png` | proven (black **overturned**) |
| x86 D3D12 (gears, triangle) | SysWOW64 bridge | Visible | `R/bridge32-installed.zip`, `R/bridge32-modules-live.txt` (`NW` F25–F27 line 138) | proven |
| D3D12 conformance subset | vkd3d tests | 41,012 to 59,572 assertions pass across 62 processes. Remaining failures were confined to YUY2. | `NW` F94 line 2266; `V/EXPERIMENTS.md` lines 615–619 | proven, but not full conformance |
| 10-bit redirected present | r10 KMD | Fixed | `R/xr-kernel-r10.zip`; `NW` F93 line 2242 | proven |
| dxdiag | — | **Never run** | grep over all notes finds nothing | — |

### 2.3 Sibling prototypes: DXVK, vkd3d and Zink as translation layers

These are closest to this repo's target. All used the Sept Yttrium release
binaries (KMD 100.2026.9.1, Mesa git `42abca66ff`), DXVK 3.1.1 and
vkd3d-proton 3.0.1.

| Case | Result | Evidence | Status |
| --- | --- | --- | --- |
| App-local DXVK 3.1.1 D3D9/10/11 triangles, x86 and x64 | All visible | `P/dxvk-vulkan-capture/…/VERDICT.md` line 24 | proven |
| App-local DXVK D3D11 60 s cube | Renders; resize visual check **fails** (old dimensions kept) | same, line 28 | proven |
| App-local DXVK Heaven D3D9 and D3D11 | Visible | same, lines 51–53 | proven |
| App-local DXVK GravityMark D3D11 | Visible (native D3D11 failed at that time) | `P/windows-global-3d/20261015-222344-d503/VERDICT.md` line 39 | proven |
| **System-wide DXVK D3D9**: DXVK `d3d9.dll` copied over `System32` and `SysWOW64` `d3d9.dll` | Heaven D3D9 renders with no app-local DLL | same, lines 33 and 45–62 | proven. Later reverted in favour of Yttrium D3D9 (`R/VERDICT.md` line 211). |
| App-local vkd3d-proton + DXVK DXGI, D3D12 triangle and GravityMark | Visible | `P/dxvk-vulkan-capture/…/VERDICT.md` line 26; `P/windows-global-3d/…/VERDICT.md` line 38 | proven |
| Zink OpenGL x86/x64 cube | Pass | `P/dxvk-vulkan-capture/…/VERDICT.md` line 29 | proven |
| DXVK clear-only frame | Black in 3.0.2 and 3.1.1, green in 2.7.1 and native. GFXReconstruct shows the green clear is never submitted. | `P/dxvk-vulkan-capture/…/VERDICT.md` lines 72–100 | proven |

### 2.4 Performance observations (frame counters, not benchmarks)

- D3D12 GravityMark: about 50 FPS (`V/EXPERIMENTS.md` lines 62–64), 54.1 FPS
  after the cache fix (lines 87–92). Present cadence and dropped frames are in
  section 3. Status: proven.
- D3D11 GravityMark: 1–19 FPS (`NW` F233). Status: proven.
- OpenGL GravityMark: 60.1 FPS (`NW` F204). Status: proven.
- None of these was measured under controlled host load. The host ran
  unrelated loads of about 27–43 (`HO` lines 204–206).

---

## 3. What failed or stayed unresolved

| Item | Last known state | Suspected cause | Source | Status |
| --- | --- | --- | --- | --- |
| **Global DXGI/D3D12Core replacement** | Replacing System32 `dxgi.dll`, `d3d12.dll` and `D3D12Core.dll` with vkd3d/DXVK made vkcube hang. Restored. | Venus WSI loads system `dxgi.dll` and `d3d11.dll` to present | `P/windows-global-3d/…/VERDICT.md` lines 64–90; `R/VERDICT.md` R1 line 144 | proven |
| vkd3d with native DXGI swapchain | `CreateSwapChain` returns E_NOTIMPL, so the bridge hooks the factory | Native DXGI cannot present a vkd3d queue | `P/windows-global-3d/…/VERDICT.md` line 66 | proven |
| **D3D11 presentation cost** | About 33 ms in Present, composed GPU-GDI copy, windowed and fullscreen, vsync on and off | Unknown. Yttrium D3D11 present path (`gdikmt_d3dddi.cpp` `yttrium_prepare_present` before `pfnPresentCb`). | `HO` lines 163–171 and 228–231; `NW` F233–F235 | proven symptom, unknown cause |
| **D3D12 dropped presents** | 667/1628 windowed, 533/1463 fullscreen. Display latency about 105–109 ms. Composed flip. | Venus reports no `present_wait`, so vkd3d logs "Latency will increase". Mesa WSI sets no `MaximumFrameLatency`. Neither is established as the cause. | `HO` lines 172–175 and 208–219; `NW` F235–F236 | proven symptom |
| Idle refresh | WaitForVBlank about 16.6 ms; the KMD has a 60 Hz timer that does the Flip | — | `NW` F237 line 6077; `HO` lines 220–226 | proven |
| F238 refresh under load | Archive `R/measure-d3d12-refresh238.zip` exists but was **never analysed** | — | `HO` lines 41–57 and 97–127 | open |
| **Host QEMU crash at shutdown** | SIGSEGV in libgallium during `egl_cleanup`. `vmlab stop` still returns 0. Fixed by a lab-local QEMU build with the upstream teardown order. System QEMU is unpatched. | QEMU 11.1.1 destroys GBM before EGL | `NW` F60–F61 lines 1257–1359; `V/EXPERIMENTS.md` lines 266–299 | proven |
| NVIDIA host | dwm crash (0x889800c0). Host renderer `vkGetImageMemoryRequirements` decoder errors. No login. | Unknown; Venus on NVIDIA 610.57.04 | `P/dxvk-vulkan-capture/…/VERDICT.md` lines 132–160 | proven failure, unknown cause |
| Code 43 after kernel-only install | Device Code 43 until the runtime was also deployed and the guest rebooted | Not isolated | `NW` F196, F199 lines 5029 and 5111 | proven symptom |
| GravityMark needs VC++ runtime | 0xC0000135 until `vc_redist.x64.exe` was installed | App prerequisite, not graphics | `NW` F202 line 5172 | proven |
| GravityMark D3D12 cache write | Permission error under a non-admin user. Fixed with a user-wide cache directory under LocalAppData. | vkd3d cache path | `V/EXPERIMENTS.md` lines 87–94 | proven |
| GL unload deadlock and leaks | Fixed in v15 (loader-lock join) and v16 (current-context leak) | `u_queue` join under loader lock | `NW` F215–F221; `R/gl-unload-repeat-queue16.zip` | proven fixed |
| Shared YUY2 implicit-read sampling (R3) | Many partial fixes in candidates. Not fully closed. | vkd3d packed-format emulation | `V/EXPERIMENTS.md` lines 389–1206 | open. Irrelevant to the goal (codex side thread). |
| Native D3D12 DDI (R2) | Absent. Out of scope for us. | — | `NW` F133 | proven absent |
| Windowed XR present | Rejected by Windows DXGI itself, before the driver | — | `NW` F97 line 2338 | proven, irrelevant |
| Installer limits | Pinned to build 26100, the native D3D12 hashes, the hardware ID, the lab cert, and a **pre-existing Vulkan loader** in the template. No bare-Windows loader install. No interrupted-upgrade test. | Design choices | `NW` F190 line 4900, F192 line 4937, F214; `HO` lines 240–243 | proven |
| Exported patches stale | `R/mesa-driver.patch` and `R/vkd3d-api.patch` are dated 2026-10-16 22:39. The working-tree diffs are larger (Mesa 149 KB vs 131 KB, vkd3d 267 KB vs 193 KB). The dxil-spirv submodule's 823-line diff is **not exported at all**. | `R/export-results.sh` does not recurse into submodules | this review: `git -C R/mesa diff \| wc -c`, `git -C R/vkd3d/subprojects/dxil-spirv diff --stat` | proven |
| Formatting | Mesa and KMD clang-format checks fail on untouched upstream files | Upstream state | `R/VERDICT.md` lines 200–204 | proven, cosmetic |

---

## 4. System-wide registration

### 4.1 Vulkan ICD

- **Release INF method** (Sept Yttrium). Files are copied to `%11%`
  (System32) and `%16425%` (SysWOW64). The INF writes
  `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` value `%11%\virtio_icd.x86_64.json`
  = DWORD 0, and the same under `WOW6432Node` for `virtio_icd.x86.json`. It
  also registers lavapipe (`lvp_icd*.json`). Source:
  `R/yttrium-kmd-release/viogpu/viogpu3d/viogpu3d_x64_wow64.inx` lines
  138–141. Status: proven, since the source was read and the release was
  installed.
- **Final installer method** (v13–v18). Files go into private directories
  `C:\Windows\System32\YttriumLab\` and `C:\Windows\SysWOW64\YttriumLab\`. Each
  holds `virtio.json` with
  `{"file_format_version":"1.0.0","ICD":{"library_path":".\\libvulkan_virtio.dll","api_version":"1.4.334"}}`.
  The installer writes `HKLM\SOFTWARE\Khronos\Vulkan\Drivers\<private>\virtio.json`
  = DWORD 0 and the WOW6432Node equivalent. No lavapipe. Source:
  `R/windows-installer.Vjgbml/Set-YttriumRuntime.ps1` (the `$vulkan` change
  entries) and `R/windows-installer.Vjgbml/payload/runtime/64/virtio.json`.
  Status: proven.
- The Vulkan **loader** (`vulkan-1.dll`) was **already in the template**. The
  installer requires it and does not install it (`NW` F192 line 4937). The
  Yttrium README tells users to install the LunarG runtime
  (`R/yttrium-kmd-release/README.md` "Vulkan runtime installer"). Status:
  proven.
- Vulkan enumeration **fails in the SYSTEM session**. Tests must run as an
  interactive user, for example through a scheduled task in session 1.
  (proven: `P/windows-vulkan/…/VERDICT.md` line 66)

### 4.2 D3D9, D3D10, D3D11 (KMD-registered UMD)

This is the display-class device key,
`HKLM\SYSTEM\CurrentControlSet\Control\Class\{4d36e968…}\<driverKey>`. The
installer resolves it from the PnP instance property `DEVPKEY_Device_Driver`
and does not assume slot 0000 (`NW` F187 line 4829).

- Release INF (`viogpu3d_x64_wow64.inx` lines 127–135):
  - `UserModeDriverName` = `%11%\viogpu_d3d10.dll` ×3 + `%11%\yttrium12.dll`
  - `UserModeDriverNameWow` = `%16425%\viogpu_d3d10.dll` ×4
  - `InstalledDisplayDrivers` = `viogpu_d3d10` ×3, `yttrium12`
  - It also copies Mesa `opengl32.dll` **over System32/SysWOW64
    `opengl32.dll`** (lines 72 and 79), and writes `EnablePreemption=1` and
    `TdrDebugMode=1` (lines 142–143).

  Status: proven, source read.
- Final installer (`Set-YttriumRuntime.ps1`):
  - `UserModeDriverName` = `C:\Windows\System32\YttriumLab\viogpu_d3d10.dll` ×3
  - `UserModeDriverNameWow` = `…SysWOW64\YttriumLab\viogpu_d3d10.dll` ×3
  - `InstalledDisplayDrivers` = `viogpu_d3d10` ×3
  - It deliberately drops the INF's `opengl32.dll` copy, Preemption/TDR
    settings and `yttrium12` entry (`NW` F188–F189 lines 4855–4898).
  - Registry writes create keys only when absent. The earlier
    `New-Item -Force` wiped the existing values (`NW` F197 line 5056).

  Status: proven.

### 4.3 OpenGL (`OpenGLDriverName`)

Same device key:

- `OpenGLDriverName` / `OpenGLDriverNameWow` = path to `viogpu_wgl.dll`
- `OpenGLFlags` / `OpenGLFlagsWow` = 3
- `OpenGLVersion` / `OpenGLVersionWow` = 4096

The release INF uses `%11%` paths. The final installer uses YttriumLab paths.
Windows' own `opengl32.dll` then loads the ICD. Status: proven
(`Set-YttriumRuntime.ps1`; inx lines 128–134; GL renders in the clean lab).

### 4.4 D3D12 (system DLL replacement)

The installer runs `takeown /F <path> /A` and `icacls <path> /grant *S-1-5-18:F *S-1-5-32-544:F`.
It moves native `System32\d3d12.dll` (and SysWOW64) into the backup and copies
the bridge in. It restores the original SDDL with `Set-Acl`. It also copies
the native DLL to `YttriumLab\d3d12-native.dll` for forwarding. `D3D12Core.dll`
and `dxgi.dll` are untouched. Preflight refuses to run unless the native
d3d12.dll hashes match (64: `6B321815…15E4`, 32: `A43885B0…00AF`). Sources:
`Set-YttriumRuntime.ps1`; `R/windows-installer.Vjgbml/bundle.json`.
Status: proven.

The session itself says Windows servicing can replace these files and that it
is not a supported deployment (`R/VERDICT.md` lines 24–27; `HO` lines 240–242).

### 4.5 DXVK system-wide (sibling prototype only)

DXVK `d3d9.dll` was copied over System32 and SysWOW64 `d3d9.dll`, with
backups and ACLs under `C:/venus/global-runtime/`. A machine environment
variable `allow_glsl_extension_directive_midshader=true` was also set. That
variable was later made unnecessary by fix F2. (proven:
`P/windows-global-3d/…/VERDICT.md` lines 45–62)

Replacing `dxgi.dll` globally broke presentation (section 3). The source shows
why. `wsi_win32_load_system_module(L"d3d11.dll")` at
`R/mesa/src/vulkan/wsi/wsi_common_win32.cpp:1477` and `…(L"DXGI.DLL")` at
`:1761` load from System32 with `LOAD_LIBRARY_SEARCH_SYSTEM32`. The resulting
`DXGI_SHARED` path creates a D3D11 device and a `FLIP_SEQUENTIAL` composition
swapchain, then copies to buffer 0 and calls Present1 (`HO` lines 210–214).
**Every Venus/Zink/DXVK/vkd3d frame therefore goes through native DXGI and the
KMD's D3D11 UMD.** Status: proven by source plus the vkcube hang. It was
inferred from source rather than debugged
(`P/windows-global-3d/…/VERDICT.md` lines 83–86).

---

## 5. Signing and installation

### 5.1 Signing (proven: `R/sign-kmd-r10.ps1`, `R/kmd-r10-signing.json`, `NW` F92 line 2211)

Signing ran **on Windows, inside the original-lab guest**, using EWDK tools
mounted at `D:` (`D:/Program Files/Windows Kits/10/bin/10.0.26100.0`):

```powershell
$certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Yttrium Lab Kernel R10' -CertStoreLocation 'Cert:/LocalMachine/My' -KeyExportPolicy NonExportable -HashAlgorithm SHA256
Export-Certificate -Cert $certificate -FilePath ($root + '/lab-kernel.cer')
Import-Certificate -FilePath ($root + '/lab-kernel.cer') -CertStoreLocation 'Cert:/LocalMachine/Root'
Import-Certificate -FilePath ($root + '/lab-kernel.cer') -CertStoreLocation 'Cert:/LocalMachine/TrustedPublisher'
& $signtool sign /v /fd SHA256 /sm /s My /sha1 $certificate.Thumbprint ($root + '/viogpu3d.sys')
& $inf2cat ('/driver:' + $root) /os:10_X64,Server10_X64 /uselocaltime
& $signtool sign /v /fd SHA256 /sm /s My /sha1 $certificate.Thumbprint ($root + '/viogpu3d.cat')
& $signtool verify /pa /v ($root + '/viogpu3d.sys')
& $signtool verify /pa /v /c ($root + '/viogpu3d.cat') ($root + '/viogpu3d.sys')
```

- Cert thumbprint `61351DAE775154870DABE22749A83CEFBFEE6491`. It expires
  October 2027 and has **no timestamp** (`NW` F92).
- The private key is **non-exportable** and lives only in the paused
  `virtio-pr943` guest. The installer kernel package (INF version
  100.6.101.58001, kernel binary unchanged) was re-catalogued and re-signed
  with the same key by `R/sign-installer-kernel.ps1` (`NW` F189 line 4877).
- No Linux-side signing (for example osslsigncode) was attempted. Status:
  claimed absent; no evidence of any attempt was found.
- The upstream Sept release instead ships `VirtIOTestCert.cer`. It was
  installed with
  `certutil -f -addstore Root …\VirtIOTestCert.cer` and
  `certutil -f -addstore TrustedPublisher …` (proven:
  `P/windows-vulkan/20261015-195348-ee5d/install-yttrium.ps1`; README
  "Windows 10 Yttrium driver installation"). Without `-f`, `addstore
  TrustedPublisher` fails on this template because the store does not exist
  (proven: `P/pr943-win11/…/baseline-driver.txt` lines 5–7; `NW` F195 line
  5003).

### 5.2 Boot policy

- **Secure Boot off**: vmlab `secure_boot = false` in both labs' `vmlab.wcl`.
  Status: proven.
- **Test signing**: the installer runs `bcdedit.exe /set '{current}' testsigning on`
  and records the original value. Restore uses `/deletevalue` when the value
  was originally absent (`Set-YttriumTestSigning.ps1`). The clean template has
  **no testsigning entry** at baseline (`NW` F194 line 4983). Status: proven.
- The README also requires `powercfg /hibernate off`, because Fast Startup
  restores stale graphics state (`R/yttrium-kmd-release/README.md`). The
  release install script ran it. The final installer does **not** set it.
  Status: claimed hazard (README only); no failure was reproduced.

### 5.3 Install mechanics

- **Early method** (release and PR 943): P/Invoke
  `newdev.dll!UpdateDriverForPlugAndPlayDevices(0, 'PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01', <inf>, 1, …)`,
  then reboot (`P/windows-vulkan/…/install-yttrium.ps1`;
  `P/pr943-win11/…/install.ps1`). Status: proven.
- **Final packaged method** (`R/windows-installer.Vjgbml/`, v18 bundle
  `R/windows-installer.Vjgbml.zip`, SHA256
  `889bd59823bf2668fc086372e5267df6c27a086e6681278e026fb48e84c01ff3`; `HO`
  lines 129–135). The installer is unattended and runs in five steps:
  1. `Install.cmd` runs
     `powershell.exe -NoProfile -ExecutionPolicy Bypass -File Setup-Yttrium.ps1 -Mode Install`.
  2. `Setup-Yttrium.ps1` stages the bundle to `C:\ProgramData\YttriumSetup`
     (SYSTEM and Administrators only) and keeps a journal.
     `Test-YttriumPayload.ps1` runs preflight: build, hardware ID, hashes,
     Vulkan loader, cert. `Backup-YttriumState.ps1` exports the old driver
     with `pnputil`, and saves native DLLs and ACLs, registry, certs and boot
     settings.
  3. `Set-YttriumTestSigning.ps1 -Mode Enable` imports the cert into Root and
     TrustedPublisher and runs `bcdedit` testsigning on. The script registers
     the SYSTEM startup task `YttriumSetupResume` and runs
     `shutdown.exe /r /t 10 /d p:2:4`.
  4. After reboot, `Set-YttriumKernel.ps1` runs
     `pnputil.exe /add-driver viogpu3d.inf /install` (exit codes 0 and 3010
     accepted). It verifies the selection through
     `DEVPKEY_Device_DriverInfPath` and `DEVPKEY_Device_DriverVersion`, not
     `Win32_PnPSignedDriver`, which lags (`NW` F196). `Set-YttriumRuntime.ps1`
     then runs (section 4), followed by a second reboot.
  5. After the second boot, both components are verified and the task is
     removed. State becomes `Installed` (`NW` F207–F208 lines 5302–5345).

  Status: proven.
- **Uninstall**: `Uninstall.cmd` restores the runtime, runs
  `pnputil /delete-driver <oemNN.inf> /uninstall`, re-adds the exported old
  `viogpudo.inf` only if it is not already selected, restores the boot value
  and certs, reboots, and verifies (`NW` F205, F206, F209). Status: proven.
- **Reboots**: two for install and one for uninstall.
- **Template hook**: none. The bundle was copied in with `vmlab cp` and run
  through `vmlab exec`.
- **Interactive desktop for tests**: `prepare-clean-desktop.ps1` created the
  standard user `yttrium-test` with a one-time autologon. That is test setup,
  not part of the installer (`NW` F199 line 5111).

---

## 6. Build

| Component | How | Host or guest | Pins | Source | Status |
| --- | --- | --- | --- | --- | --- |
| Mesa (Yttrium UMDs, Venus ICD, WGL/Zink), x64 and x86 | `R/build.sh` and `R/build32.sh`: Meson cross build with the options listed below | **Linux host**, `/opt/llvm-mingw` (clang 22.1.7, from the meson log) | Meson 1.9.1, mako 1.3.10, packaging 25.0, pyyaml 6.0.3 (uv venv). Windows SDK and WDK **NuGet 10.0.26100.1**: `R/prepare-build.py` extracts 13 DDK headers into `mesa/include/winddk`. | `R/build.sh`, `R/cross-win64.ini`, `R/cross-win32.ini`, `R/build-win64/meson-logs/meson-log.txt` | proven (builds and runs) |
| vkd3d-proton core and bridge, x64 and x86 | `meson setup build-vkd3d{,32} vkd3d --cross-file cross-win{64,32}.ini --buildtype=release` | Linux host, llvm-mingw | vkd3d `3b10bd7a`, dxil-spirv `8434b063` (+ local diff), nested pins in `R/translator-source-pins.txt` | `R/build-vkd3d32.sh`, `R/build-vkd3d/meson-logs/` | proven |
| DXVK DXGI-only, x64 and x86 | `meson setup build-dxgi$arch dxvk --cross-file dxvk/build-win$arch.txt --buildtype release -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false -Denable_d3d11=false` | Linux host, **system mingw-w64 GCC 16.2.0** (Arch), not llvm-mingw | DXVK `b1a1c99a` (v3.1.1) | `R/build-dxgi.sh`, `R/build-dxgi64/meson-logs/meson-log.txt` | proven |
| Yttrium KMD | `R/build-kmd.ps1`: in the guest, with an **EWDK ISO attached as a CD-ROM** (`D:`), it runs `BuildEnv\SetupBuildEnv.cmd`, then msbuild on `VirtIO/VirtioLib`, `viogpu/viogpuap`, `viogpu/viogpusc`, `viogpu/viogpu3d` as **`Win11 Debug|x64`**. `MESA_PREFIX_x64` and `MESA_PREFIX_x86` point at staged Mesa DLLs (`R/stage-kmd-mesa.ps1`). | **Windows guest**, MSVC 14.44.35207 from the EWDK | EWDK 26100 ISO, 20,002,537,472 bytes, SHA256 `9f48251dd24ad31aac206d8256e95bda5f90a9783982c45a8aafeb9054562379` (`R/ewdk-full-hash.txt`, `NW` F91). The download URL is not recorded in the notes found. | `R/build-kmd.ps1`, `R/kmd-r10-environment.json`, `R/download-ewdk-ranges.sh` | proven |
| Installer payload and bundle | `R/build-installer-payload.py`, `R/build-windows-installer.py` (host Python). They hash every file into `manifest.json` and `bundle.json`. | Linux host | — | `NW` F190, F207 | proven |
| QEMU fix | `R/build-qemu-cleanup.sh`: v11.1.1 plus `qemu-cleanup.patch`. Needs `--enable-pixman` alongside `--without-default-features`. | Linux host | base `c3d48b7d…` | `NW` F61 | proven |

The Mesa `meson setup` options from `R/build.sh` (the x86 build differs only
in the cross file and prefix):

```
-Dplatforms=windows -Dgallium-drivers=zink -Dgallium-yttrium=true -Dgallium-d3d10umd=true
-Dgallium-wgl-dll-name=viogpu_wgl -Dgallium-d3d10-dll-name=viogpu_d3d10
-Dvulkan-drivers=virtio -Dllvm=disabled -Dzlib=disabled -Dzstd=disabled -Dexpat=disabled
-Dbuild-tests=false -Dc_link_args=-static -Dcpp_link_args=-static
```

Build caveats:

- `build.sh` is not idempotent; the session notes say so (`R/VERDICT.md` line
  196).
- `/opt/llvm-mingw` is a host install of unrecorded provenance. Its release tag
  is not recorded.
- No build runs in a container.
- The **KMD needs Windows**: either the EWDK in a VM, or the upstream EWDK
  instructions referenced in the README. Yttrium's own `BUILDING.md` says
  "Only mingw w64 clang is supported when building Vulkan driver"
  (`R/yttrium-kmd-release/viogpu/viogpu3d/BUILDING.md`).
- The installed **D3D12 bridge binaries** cannot be reproduced from the
  current tree (`NW` F188).
- The KMD is a **Debug** build.

---

## 7. Reusable artefacts

### 7.1 Sources, patches and scripts (reusable)

| Path | What | Provenance | Licence |
| --- | --- | --- | --- |
| upstream `https://github.com/arehnman/yttrium-virtio-gpu` @ `b99d951a` (`R/yttrium-kmd-release`) | Venus-capable virtio-gpu WDDM KMD source and INX templates | Yttrium tag `yttrium-experimental-2026-09-27` | BSD-3, MPL-2.0 (file headers) |
| `R/kmd-r10-copy.patch` (+ `R/kmd-r10-copy-base.txt`) | KMD 10-bit → 8-bit present-blit conversion | Codex, on `b99d951a` | as KMD |
| upstream `https://github.com/arehnman/virtio-win-mesa` @ `374c3022` | Venus Windows ICD (`vn_renderer_d3dkmt.c`), Win32 WSI, WGL/Zink, Yttrium UMD | Yttrium | MIT / MPL-2.0 |
| `R/mesa` working tree (re-export it; `R/mesa-driver.patch` is stale) | Build fixes (F1), case-insensitive drirc exe match (F2), D3D9/D3D11 fixes, `u_queue` loader-lock fix (v15/v16), NT memory export in Venus, WSI tweaks | Codex | as Mesa |
| `R/build.sh`, `R/build32.sh`, `R/prepare-build.py`, `R/cross-win64.ini`, `R/cross-win32.ini` | Linux cross-build recipe for Mesa on Windows, with SDK/WDK NuGet header extraction | Codex | — |
| upstream vkd3d-proton @ `3b10bd7a`, dxil-spirv @ `8434b063` | D3D12 → Vulkan | upstream | LGPL-2.1 / MIT |
| `R/vkd3d/libs/d3d12/main.c` diff + `R/vkd3d/libs/d3d12/native_dxgi.cpp` (untracked) | **System-wide D3D12 bridge**: private core path plus DXGI factory hook that routes swapchains to a private DXVK DXGI | Codex | LGPL-2.1 |
| `R/vkd3d-api.patch` (stale) and the dxil-spirv diff (unexported) | Format and validation fixes; mostly the YUY2 side thread | Codex | LGPL-2.1 |
| `R/dxgi-validation.patch` on DXVK `b1a1c99a` | Keeps the swap-chain desc when a resize is rejected | Codex | zlib |
| `R/build-dxgi.sh`, `R/build-vkd3d32.sh` | DXGI-only DXVK build and vkd3d cross build | Codex | — |
| `R/build-kmd.ps1`, `R/stage-kmd-mesa.ps1`, `R/download-ewdk-ranges.sh` | EWDK-in-guest KMD build and ranged EWDK download with verification | Codex | — |
| `R/sign-kmd-r10.ps1`, `R/sign-installer-kernel.ps1` | Self-signed cert plus signtool/Inf2Cat recipe (generate a **new** cert) | Codex | — |
| `R/windows-installer.Vjgbml/*.ps1`, `Install.cmd`, `Uninstall.cmd`, `R/build-installer-payload.py`, `R/build-windows-installer.py` | Journaled two-reboot installer with backup, restore and upgrade, ACL-preserving system-DLL swap, registry and PnP handling | Codex | — |
| `R/inspect-install-state.ps1`, `Test-YttriumRestored.ps1`, `R/check-restored-permissions.ps1` | Install-state inventory and restore verifiers | Codex | — |
| `R/qemu-cleanup.patch` | Backport of QEMU `baca2517` (EGL/GBM teardown order) to v11.1.1 | upstream QEMU | GPL-2.0 |
| `P/does-qemu-egl-gbm-teardown-order-cause-the-host-/20261016-171816-205f/run.sh` | Standalone host EGL/GBM crash probe | Codex | — |
| `R/analyze-frame-times.py`, `R/test-measure-gravity.sh`, `R/measure-gravity.ps1` (guest), `R/refresh-timing.cpp` | PresentMon/ETW frame analysis and refresh probe | Codex | — |
| `P/dxvk-vulkan-capture/…/analyze_capture.py`, probe C sources (`d3dprobe.c`, `glprobe.c`, `triangle.c`, `probe.c`) | Minimal D3D/GL present probes and the DXVK clear-only reproducer | Codex | — |
| `R/clean-install-lab.WlATCI/vmlab.wcl`, `vmlab-clean.sh` | Working vmlab lab definition for Venus on the AMD iGPU | Codex | — |

### 7.2 Binaries: **not reusable** (unknown or unreproducible provenance)

| Path | Why |
| --- | --- |
| `P/windows-vulkan/20261015-195348-ee5d/yttrium.zip` (`215b4aaf…`), `Install_Debug/` | Upstream release binary. The download URL was not recorded. It is test-signed with `VirtIOTestCert`. |
| `P/pr943-win11/…/community-viogpu3d.zip`, `viogpu3d.iso`, `driver/` | PR 943 community build; the KMD commit is unknown |
| `R/windows-installer.*/payload/runtime/*/d3d12.dll` (installed bridges) | Differ from rebuilds beyond the timestamp (`NW` F188) |
| `R/windows-installer.*/payload/kernel/*` | Signed with a key locked in a paused VM; Debug build |
| `R/windows-installer.*/payload/runtime/*` other DLLs | Built from a dirty tree whose patches were not re-exported |
| DXVK 2.7.1/3.0.2/3.1.1 release tarballs in `P/dxvk-vulkan-capture/` and `P/windows-vulkan/…/dxvk.tar.gz` | Fine as upstream releases, but rebuild from tag instead |
| `R/ewdk-*.iso` | Microsoft EWDK. Licence-restricted; download it again |
| `R/qemu-cleanup-build.FCJLcO/qemu-system-x86_64` | Local build; rebuild from the patch |
| Godot executables, GravityMark, Heaven, Blender | Test apps only |
| `R/before-*-fix-*.dll`, `R/*.exe` probes | Intermediate binaries |

---

## 8. Lessons and traps

### 8.1 Host and QEMU

- **Use the AMD iGPU.** Venus on the RTX 5090 (NVIDIA 610.57.04) killed dwm
  before login (proven: `P/dxvk-vulkan-capture/…/VERDICT.md` lines 132–160).
  The host currently exposes render nodes `renderD128` (NVIDIA
  0000:01:00.0), `renderD129` (AMD 0000:72:00.0) and `renderD130` (NVIDIA
  0000:05:00.0). The ICDs are `nvidia_icd.json` and `radeon_icd.json`
  (observed by this review in `/sys/class/drm`).
- **The vmlab supervisor needs `VK_DRIVER_FILES=/usr/share/vulkan/icd.d/radeon_icd.json`
  in its own environment.** A running supervisor does not pick up the
  caller's environment, so the session ran an isolated supervisor with its own
  `XDG_RUNTIME_DIR` and `XDG_STATE_HOME`
  (`P/windows-vulkan/…/vmlab-amd.sh`; `P/windows-vulkan/…/VERDICT.md` lines
  37–40). Status: proven.
- **vmlab `gpu { mode = "vulkan" }` differs from the tested line.** vmlab emits
  `virtio-gpu-gl,venus=true,hostmem=4G` plus `-display egl-headless`
  (`/home/wil/dev/vmlab/src/qemu/cmdline.rs` lines 579–586). The tested line
  was `virtio-vga-gl,…,blob=on,venus=on`, a **memfd `share=on` RAM backend**
  and `egl-headless,rendernode=/dev/dri/renderD129`. vmlab adds the memfd
  backend only for virtiofs VMs (`cmdline.rs` lines 260–282). Whether `blob`,
  memfd, `virtio-vga` vs `virtio-gpu` and the rendernode are each required was
  **not tested**. The Yttrium README lists blob, venus, nonzero hostmem and a
  shared memory backend as essential
  (`R/yttrium-kmd-release/README.md` "QEMU setup"). These are candidate vmlab
  handoffs.
- **QEMU 11.1.1 SIGSEGV at shutdown** in libgallium. `vmlab stop` returns 0
  anyway; check the lab daemon log. The fix is upstream QEMU `baca2517`
  (proven: `NW` F60–F61).
- Host stack that worked: QEMU 11.1.1, virglrenderer 1.3.0, Mesa/RADV 26.2.2
  (Arch packages). Yttrium's Flatpak and QEMU forks were not needed. Status:
  proven.

### 8.2 vmlab gaps hit

These are candidate vmlab handoffs:

- **Online snapshots fail** ("virgl is not migratable"); only offline
  snapshots work (`NW` F192 line 4937).
- **QMP screenshot fails with `no surface`** under egl-headless. Evidence came
  from guest-side `CopyFromScreen` or a local VNC capture, `R/capture-vnc.py`
  (`P/dxvk-vulkan-capture/…/VERDICT.md` line 187; `NW` F98 line 2365). That
  matters for "visible in a vmlab console screenshot".
- `vmlab up` returns before the guest agent is ready, and jobs failed on that
  (`NW` F198 line 5086).
- `vmlab cp` cannot pull a directory
  (`P/dxvk-black-frame/20261015-201354-f801/VERDICT.md` "Experiment limits").
- Descriptor exhaustion in the lab daemon and supervisor; soft limits were
  raised from 2048 to 4096 (`V/EXPERIMENTS.md` lines 1212–1214; `R/VERDICT.md`
  lines 297–302).
- The standalone `wcl check` cannot resolve `<vmlab.wcl>`; use
  `vmlab validate` (`NW` F91).
- Low guest disk space broke `Compress-Archive` in the original VM, which then
  had 0 bytes free (`NW` F200).

### 8.3 Guest and Windows

- Test signing needs `certutil -f` for TrustedPublisher, because the store does
  not exist on the template. After import, check from a **fresh process** or
  with an `X509Store` (`NW` F195).
- `Win32_PnPSignedDriver` lags after pnputil; use `DEVPKEY_Device_DriverInfPath`
  (`NW` F196).
- `New-Item -Force` on an existing registry key **erases its values** (`NW`
  F197).
- `takeown` rejects forward-slash paths (`NW` F25–F27 line 159).
- Vulkan does not enumerate from the SYSTEM session. GPU tests need an
  interactive user session (section 4.1).
- An API exit code of 0 is not rendering evidence. Several "passes" were black
  frames, for example the DXVK clear-only frame and GravityMark `Visible: 0`
  (`NW` F32 line 209).
- Global `GALLIUM_DRIVER=zink` and `MESA_LOADER_DRIVER_OVERRIDE=zink` broke
  native D3D11 shared presentation (`P/windows-vulkan/…/VERDICT.md` lines
  96–99).
- PowerShell 64-bit module enumeration misses WOW64 modules; use Toolhelp
  `TH32CS_SNAPMODULE32` (`NW` F26).
- Fast Startup and hibernation are unsupported by the KMD (README claim,
  section 5.2).
- Apps may need the VC++ runtime. That is not a graphics fault (`NW` F202).

### 8.4 Time sinks to avoid

The session spent most of F40–F184 and about 1,000 lines of `V/EXPERIMENTS.md`
on these, none of which this repo needs:

- vkd3d YUY2, packed-RGB, opaque-420 and XR format emulation and sampler
  conformance
- the native D3D12 DDI probe
- a custom Godot build
- 10-bit and XR presentation

The brief is right that these were side threads.

---

## 9. Open questions

1. Does Venus work with vmlab's own `gpu { mode = "vulkan" }` line, which has
   no `blob=on`, no memfd backend and no rendernode? Which of those options
   are required? (Never tested.)
2. How does vmlab choose the host render node and Vulkan ICD for egl-headless
   and virglrenderer on this multi-GPU host? Can a lab request the AMD iGPU?
3. Can Venus WSI present without native DXGI and the KMD's D3D11 UMD? If not,
   a system-wide DXVK `d3d11.dll` and `dxgi.dll` collides with the WSI's own
   use. How would system-wide DXVK D3D10/11 then be registered: as the
   KMD-registered UMD names, or as System32 replacements? Untested. Only
   global DXVK D3D9 was tried, and it worked.
4. What causes the DXVK ≥3.0 clear-only black frame? Is it fixed in a DXVK
   newer than 3.1.1? (Not bisected.)
5. What causes the D3D11 present cost of about 33 ms and the D3D12 drops? Does
   the host VNC console see the same cadence? (F238 not analysed; host-side
   delivery never measured.)
6. Does dxdiag report hardware acceleration with this KMD? (Never run.)
7. Does the stack work on **Windows 10**? Yttrium upstream qualifies on
   Windows 10 with an Intel Arc A580 (README "Project status"); the session
   tested only Windows 11.
8. Can the KMD be built off-Windows, or must the EWDK run in a VM? Can kernel
   signing be done on Linux, for example with osslsigncode, and will Windows
   accept that catalog?
9. What exact URL and release asset did `yttrium.zip` come from? It is not
   recorded.
10. Is the Venus ICD usable without the Yttrium KMD's private escape protocol?
    `vn_renderer_d3dkmt.c` submits through `D3DKMTEscape`. The KMD/ICD ABI
    pairing was never documented.
11. Does the local `windows-11` template ship with Secure Boot on by default?
    The labs forced `secure_boot = false`.
12. What does the Vulkan loader look like on a template that lacks it? The
    installer assumed it was present.
13. Does `powercfg /hibernate off` matter in practice? The README says yes; no
    failure was reproduced.
