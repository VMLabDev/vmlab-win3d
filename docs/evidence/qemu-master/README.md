# The vmlab QEMU on upstream master

Evidence for moving `patches/host/PINS` from `78bc99a513` (the series on
v11.1.1) to `d8a6048aae`: the same six commits on upstream QEMU master
`a19f59a72d` (`v11.1.0-2513-ga19f59a72d`, version 11.1.50), on the
[VMLabDev/qemu](https://github.com/VMLabDev/qemu/tree/vmlab/egl-headless-external-dmabuf)
branch `vmlab/egl-headless-external-dmabuf`. helios, 2026-10-18: NVIDIA
610.57.04, Mesa 26.2.2, virglrenderer 1.3.0, vmlab 0.28.0-alpha.

## Build

- The series needed no changes after the rebase. Each of the six commits
  builds on its own (`git rebase --exec`, ninja after every commit) with the
  egl-headless, GTK (3.24.52) and SDL (2.32.70) displays all enabled, against
  the host's own libraries and QEMU's vendored Meson 1.12.0. No newer host
  library was needed: master asks for glib 2.66 or later, and the host has
  2.88.3.
- `patches/host/build-qemu.sh` builds the pinned commit from GitHub.
  `BUILDINFO` is that build: `QEMU emulator version 11.1.50 (vmlab d8a6048aae11)`,
  the base, the series and the host libraries.

## Display: 12 of 12 on every host GPU

`tests/e2e/run.sh --template windows-11 --host-gpu <GPU>` with the test
build as `qemu_binary`. The runs used an isolated vmlab supervisor whose own
`XDG_CONFIG_HOME` host config gave all three GPU blocks that binary, so the
user's host config was not touched. Each directory's `qemu-argv-env.txt`
shows `vmlab: argv:` starting with that binary, and `host.txt` shows its
`--version`.

| Run | Result |
| --- | --- |
| `windows-11-nvidia1/` | 12 of 12, every probe visible and animating on the console |
| `windows-11-nvidia2/` | 12 of 12, every probe visible and animating on the console |
| `windows-11-amd1/` | 12 of 12. This is the stock display path (`GL_TEXTURE_2D` import), now through the patched binary |

## Shutdown crash: gone

The stock QEMU 11.1.1 crash is a SIGSEGV in `libgallium` under
`egl_cleanup()` → `qemu_display_cleanup()` (`stock-qemu-shutdown-cores.txt`:
three cores on helios, all from labs whose egl-headless ran on the AMD
render node). Master has `baca25172d` ("ui/egl: fix render node cleanup
order").

`cycles.sh` stops and starts the VM of an installed e2e lab 10 times. Before
each stop it waits for the autologon desktop and runs the x64 D3D12 and D3D11
probes for 15 s each. After each stop it reads the lab's `vm.stopped` event
and `coredumpctl`.

| Run | Stops | QEMU exit | Cores | Probes |
| --- | --- | --- | --- | --- |
| `cycles-amd1/cycles.log` | 10 | `exit status: 0` every time | 0 | 20 of 20 pass |
| `cycles-nvidia2/cycles.log` | 10 | `exit status: 0` every time | 0 | 20 of 20 pass |

`console-cycle-10.png` in each is the console on the tenth boot, so the
console still shows the guest after repeated restarts.

The labs were destroyed and the isolated supervisor stopped afterwards.
