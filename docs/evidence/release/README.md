# Release evidence

One directory per release version, with the checks from
[`docs/release.md`](../../release.md), "Check it before publishing".

## 0.1.0-alpha

Built on 2026-10-18 on helios by
`WIN3D_SIGNING_DIR=/home/wil/dev/vmlab-win3d/.signing scripts/release.sh 0.1.0-alpha`
in a fresh clone of `work/release` at `9569489`, from empty `out/` and
download caches (exit 0, 14:55 to 15:58).

| Asset | sha256 |
| --- | --- |
| `vmlab-win3d-0.1.0-alpha-windows-x64.zip` | `f05051683ea342526ff175ed6441b7587227dc0646f66cfe32676a8259897d1f` |
| `qemu-11.1.1-vmlab-egl-0.1.0-alpha-x86_64-linux.tar.zst` | `7ddfd3fd2a2c6ea29daec6f530ac70342b028d4524abe9dd257cd04ac407fa5a` |
| `RELEASE-NOTES.md` | `f5dcc3c387bc12dc9d38c0286d2f70c308421a9843926387d8c61cc681e08319` |

The package is signed with the shared test certificate, SHA-1 thumbprint
`BEC7EE0705AFDB5E0BF72809F4E00F4D611F356A`. The QEMU is VMLabDev/qemu
commit `78bc99a513c04b8c51bc6c211f5da4eaa21e2592` (`qemu-nvidia2/BUILDINFO`).

### Driver zip on AMD1: pass, 10 of 10

[`0.1.0-alpha/zip-amd1/`](0.1.0-alpha/zip-amd1/), made by its `zip-test.sh`.

- A fresh lab from `labs/probe/vmlab.wcl` (renamed `win3d-release-zip`;
  `host_gpu = "AMD1"`, vmlab 0.28.0-alpha), with the autologon test desktop
  from `tests/guest/Set-TestDesktop.ps1`.
- The zip was copied in and extracted to `C:\vmlab-win3d-0.1.0-alpha`
  (`extracted.txt`). Then, as its `README.txt` says,
  `vmlab-win3d-0.1.0\Install.cmd` ran elevated (as SYSTEM through
  `vmlab exec`; `install-cmd.txt`). Windows restarted twice, and the
  `-Mode Status` command from the README reported `Installed`
  (`status.txt`, `journal.log`: `oem15.inf`, problem code 0).
- `tests/guest/Run-AllProbes.ps1` with the probes from the same build:
  D3D9, D3D11, D3D12, OpenGL and Vulkan, x64 and x86, all `pass`, exit 0
  (`run-allprobes.txt`, `summary.json`). `console-after-probes.jpg` is the
  console afterwards, with the Test Mode watermark.
- The README's `vulkaninfo --summary` check was run as SYSTEM by the script
  and found no GPU there. That is expected: Vulkan does not enumerate devices
  from session 0 (`tests/guest/Set-TestDesktop.ps1`). The Vulkan probes in the
  user's session list `Virtio-GPU Venus (AMD Ryzen 9 9950X ... (RADV
  RAPHAEL_MENDOCINO))`.
- The lab was destroyed afterwards.

### QEMU tarball on Nvidia2: pass, 12 of 12

[`0.1.0-alpha/qemu-nvidia2/`](0.1.0-alpha/qemu-nvidia2/).

- The tarball was extracted and `install.sh --check` and
  `install.sh <worktree>/.scratch/qemu-test/prefix` run (`install.txt`,
  `ldd-check.txt`, `version.txt`: QEMU 11.1.1).
- `tests/e2e/run.sh --template windows-11 --host-gpu Nvidia2 --lab
  win3d-release-nv2` ran in the release clone, with the package from the
  release build, through an isolated supervisor whose own
  `XDG_CONFIG_HOME` host config (`host-config.wcl`) gives Nvidia2 that
  prefix as `qemu_binary`. The user's host config was not touched.
  `e2e/qemu-argv-env.txt` shows vmlab started the temporary prefix's QEMU
  on `0000:05:00.0`; `e2e/host.txt` has the host stack.
- Result: 12 of 12 Done checks pass, every probe visible and animating on
  the console (`e2e/results.txt`, `e2e/console-*.png`). The lab was
  destroyed and the isolated supervisor stopped.
