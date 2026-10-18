# Evidence: the vmlab render server (hostvirgl)

Deployment of the N5 fix ([`docs/n5.md`](../../n5.md)): virglrenderer
1.3.0's `virgl_render_server` with `patches/host/virglrenderer/0001` and
`0002`, built by `patches/host/build-virglrenderer.sh` and selected per GPU
with `qemu_env = { RENDER_SERVER_EXEC_PATH: ... }`.

helios, 2026-10-19: vmlab 0.28.0-alpha, the vmlab QEMU `d8a6048aae`,
virglrenderer 1.3.0-2.1 (host library), NVIDIA 610.57.04, Mesa 26.2.2
(RADV). The guest package was the one built at `1c73461` (tests/e2e with
the lab NIC and `slmgr /ato`), `windows-11` template 26100.1742.5. Every
run used an isolated vmlab supervisor (own `XDG_CONFIG_HOME`,
`XDG_STATE_HOME`, `XDG_RUNTIME_DIR`) whose host config was helios's plus
the `qemu_env` line, so the user's host config stayed unchanged while
other workers' labs ran. The server under test was the one installed in
`~/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server` (sha256
`69d6fc6afbe88c8dafc02324df414fd29791073fbefbeb60eeb4d450fda2cff1`).

Other workers' VMs shared every GPU in every run, and from about 04:00 the
RTX 5090s' 256 MiB BAR1 was exhausted at times (kernel: `NVRM:
dmaAllocMapping_GM107: can't alloc VA space for mapping`; worker nvva owns
that). Absolute frame rates therefore vary from run to run.

## `nvidia1-ab/`: stock and patched server in one lab

One kept `windows-11` lab on `Nvidia1`. Between phases the isolated host
config gained or lost the `qemu_env` line and the VM was restarted
(`vmlab vm restart`). `qemu-env-lines.txt` holds the `vmlab: env:` line of
each QEMU start in order: the e2e boot (two lines, the first from an
aborted attempt), the stock phase (no `RENDER_SERVER_EXEC_PATH`), the
second patched phase. `docs/evidence/present/run-pace.sh` ran the present
repro; the package probes ran through `tests/guest/Run-Probe.ps1`.

| Phase | `pace d3d9` x64 | frame p50, p95 | `pace d3d9-imm` | D3D9 probe | D3D12 probe | OpenGL probe |
| --- | --- | --- | --- | --- | --- | --- |
| `1-patched` | 55.8/s | 16.67, 19.48 ms | 235.2/s | — | 52.0 fps | — |
| `2-stock` | 21.6/s | 42.62, 43.70 ms | 74.4/s | 22.4 fps | failed (fence timeout) | failed (`SwapBuffers`) |
| `3-patched` | 55.4/s | 16.67, 19.18 ms | 55.1/s | 37.2 fps | 43.2 fps | 41.2 fps |

The stock D3D12 and OpenGL failures coincided with BAR1 errors in the
kernel log (04:41). They are not evidence against the stock server, only
that the card was overcommitted. The `3-patched` `d3d9-imm` run lost frames
the same way (p95 37.8 ms).

## `amd1-ab/`: AMD1 does not need it

The same procedure on `AMD1` (RADV), stock, patched, stock:

| Phase | `pace d3d9` x64 | `pace d3d9-imm` | D3D9 probe | D3D12 probe |
| --- | --- | --- | --- | --- |
| `1-stock` | 43.3/s | 149.5/s | 59.4 fps | — |
| `2-patched` | 59.4/s | 551.9/s | 59.4 fps | 57.7 fps |
| `3-stock` | 59.4/s | 534.7/s | 59.3 fps | 58.7 fps |

`1-stock` ran with the host's load average near 30; `3-stock`, at about 16,
equals `2-patched`. RADV wakes plain and exportable fences alike
(`docs/evidence/n5/host/vkfence.txt`), so `AMD1` keeps the stock server.

## `e2e/`: `tests/e2e/run.sh` with the render server

Each directory holds `results.txt`, `run.log`, `host.txt`, `status-v.txt`,
`activation.txt` and `qemu-argv-env.txt` from one `tests/e2e/run.sh
--template <t> --host-gpu <GPU>` run. `qemu-argv-env.txt` accumulates
every QEMU start under that lab name in the isolated supervisor's state;
the last `vmlab: env:` line is the run's own. NVIDIA runs had
`RENDER_SERVER_EXEC_PATH` on it, AMD runs did not (the deployed profile).

| Directory | Render server | Result | Failures |
| --- | --- | --- | --- |
| `windows-11-amd1/` | stock (the deployed `AMD1` profile) | 12 of 12 | — |
| `windows-10-amd1/` | stock | 12 of 12 | — |
| `windows-11-nvidia1-run1/` (03:56) | patched | 10 of 12 | D3D12 x64 and x86: 1 frame in 29 s; NVRM BAR1 errors at 04:11:31, the second the D3D12 probe ran |
| `windows-11-nvidia2-run2/` (04:59) | patched | 10 of 12 | D3D12 x64 and x86, the same way (4 VMs on `Nvidia2`, 365 NVRM BAR1 errors after 05:00) |
| `windows-11-nvidia1-run3/` (06:20) | patched | 4 of 12 | no probe window on the console; the server logged `vkAllocateMemory failed: ret -2` (out of device memory) for memory type 4, `DEVICE_LOCAL`, `HOST_VISIBLE`, `HOST_COHERENT` (the 256 MiB BAR1 heap), then `failed to look up object` CS errors (`render-server-errors.txt`, from patch 0001's logging); NVRM BAR1 errors 06:25 to 06:35; the card had 3 VMs |

No NVIDIA run reached 12 of 12. Every NVIDIA failure coincided with BAR1
exhaustion, the failure the orchestrator's 06:00 rule attributes to the
small-BAR heap (worker nvva, mesa 0070 and 0071), and the stock server
failed the same probes the same way in `nvidia1-ab/2-stock`. In the A/B lab
the D3D12, D3D9 and OpenGL probes passed with the patched server once the
card had room. A 12-of-12 NVIDIA run with the render server still needs a
quiet card or nvva's fix; `Nvidia2` is reserved for nvva.

Earlier runs that did not get as far as the probes are not kept: the
first `AMD1` and `Nvidia2` attempts at 04:05 stopped in Windows OOBE ("Why
did my PC restart?") with the host's load average at 26 and four VMs on
`AMD1`, and one `AMD1` run (05:30) passed 11 of 12, losing the x86 D3D9
probe (17 frames in 20 s) under load; the same probe ran at 59.3 fps in
that lab right after.

## `gl_alpha/`: the Windows 10 translucency was not reproduced

`docs/evidence/present/src/gl_alpha.c` now has GDI paint the GL window's
own background magenta too, so the composition would show magenta under
any alpha that reached DWM. The A/B swapped the WGL DLL in a kept lab with
`docs/evidence/present/swap-umd.sh`: the package's, with mesa 0027
(`viogpu_wgl.dll` x64 sha256 `6d1e68af…`), and one built from the same
tree without 0027 (`759c38ea…`, `build/mesa/build.sh` in a checkout of
`1c73461` with the patch removed). Each run sampled the console every 3 s
(`console-pixels.txt`: the window centre and the backdrop corner). The
centre was the clear colour (204, 51, 51) whenever the window was up, with
either DLL:

| Run | WGL | Console centre while the window was up | Probe |
| --- | --- | --- | --- |
| `w11-nv1-0027/` | 0027 | 204, 51, 51 | PASS |
| `w11-nv1-no0027/` | without 0027 | 204, 51, 51 | PASS |
| `w10-amd1-0027/` | 0027 | 204, 51, 51 | PASS |
| `w10-amd1-no0027/` | without 0027 | 204, 51, 51 (`console-t9.png`) | PASS |
| `w10-amd1-no0027-alpha05.txt` | without 0027, alpha 0.5 | 204, 51, 51 | PASS |

Two things looked like a reproduction and were not. Console captures taken
before the first GL frame reached DWM show the magenta redirection surface
(pure 255, 0, 255, not a blend). And a version of the probe that read the
screen through DXGI Desktop Duplication got `DXGI_ERROR_ACCESS_LOST` on
every frame on this display stack, and slowed the GL loop enough that the
window stayed magenta for most of the run. Neither is translucency.

## Build and release asset

`out/host/virglrenderer/BUILDINFO` of the server under test:

```
component:      virgl_render_server (vmlab render server)
virglrenderer:  1.3.0 https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.3.0/virglrenderer-1.3.0.tar.gz
sha256:         065bc56e89e6f631f96101cd62eba0748e48eb888b434edc86e89d05395e76f3
patch:          0001-vkr-log-image-creation.patch 1391a6b58249e2b7
patch:          0002-vkr-do-not-create-queue-sync-fences-exportable.patch 3b6aa0716822559b
build host:     CachyOS, x86_64, built 2026-10-18
compiler:       cc (GCC) 16.2.1 20260810
compiler flags: -march=x86-64-v2 -mtune=generic (link: -z x86-64-v2)
needs ISA:      x86-64-v2
meson:          1.12.1, PyYAML 6.0.3
glibc:          2.44
needs GLIBC:    GLIBC_2.38
lib libdrm: 2.4.134
lib gbm: 26.2.2-arch3.2
lib vulkan: 1.4.357
host libvirglrenderer: 1.3.0 (must be 1.3.0: same render-server protocol)
```

`zeus-install-check.txt`: the release tarball's `install.sh --check` and
install on Zeus, an x86-64-v3 CPU with the same virglrenderer package.
