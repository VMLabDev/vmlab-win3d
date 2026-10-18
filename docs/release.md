# Releases

A release has five files, all written by `scripts/release.sh` to
`out/release/<version>/`:

| File | What it is |
| --- | --- |
| `vmlab-win3d-<version>-windows-x64.zip` | The signed guest package (`out/package/vmlab-win3d-<x.y.z>/`) and a top-level `README.txt` for the person installing it |
| `qemu-vmlab-egl-<version>-x86_64-linux.tar.zst` | The vmlab QEMU for NVIDIA hosts: `bin/qemu-system-x86_64`, `install.sh`, `README.txt`, `BUILDINFO` and QEMU's licence files |
| `virglrenderer-vmlab-<version>-x86_64-linux.tar.zst` | The vmlab render server for NVIDIA hosts: `libexec/virgl_render_server` (virglrenderer 1.3.0 with `patches/host/virglrenderer/`), `install.sh`, `README.txt`, `BUILDINFO` and the licence |
| `RELEASE-NOTES.md` | Version, repository commit, the assets and their sha256, the test certificate, the host GPU matrix, the known limits in brief, and every upstream pin |
| `SHA256SUMS` | sha256 of the other four files |

Releases up to 0.1.0-alpha shipped QEMU 11.1.1 as
`qemu-11.1.1-vmlab-egl-<version>-x86_64-linux.tar.zst`, which installed into
`~/.local/opt/qemu-11.1.1-vmlab-egl`. Later releases build the series on
upstream QEMU master, so the asset and the default prefix no longer name a
QEMU version; `BUILDINFO` and `--version` do.

The first part of this page is for the person who cuts a release; the
second is for the person who installs one.

## Cut a release

### Prerequisites

- A host that can run `scripts/build.sh`: Docker without sudo, and about an
  hour for a build with empty caches.
- For the QEMU asset, the native build dependencies of
  `patches/host/build-qemu.sh` (git, gcc, make, ninja, pkg-config, and the
  development files of glib, pixman, epoxy, gbm, virglrenderer and the other
  libraries QEMU links). An Arch or CachyOS host with the `qemu-full`
  package has them. The binary links this host's libraries, so it suits
  hosts like this one; `BUILDINFO` in the tarball records them.
- The shared test-signing directory, made once by
  `build/signing/make-test-cert.sh` and kept outside every checkout. Every
  release signs with the same certificate, so a guest that trusts one
  release's certificate trusts the next one's. On helios it is
  `/home/wil/dev/vmlab-win3d/.signing/`.
- A clean checkout of the commit to release. `installer/VERSION` gives the
  package version; the release version must start with it (`0.1.0-alpha`
  for `0.1.0`).
- The QEMU commit to ship, pinned on the `qemu` line of
  `patches/host/PINS`. To move it to the tip of the fork branch, run
  `patches/host/bump-qemu.sh`, which rewrites the pin and prints the commits
  between the old and new pin. Then rebuild, test it on an NVIDIA GPU
  ([`nvidia.md`](nvidia.md)), run `scripts/pins-table.py --write`, and
  commit before releasing.

### Run it

Run the script from a fresh clone, so nothing left over from earlier
builds ends up in the assets:

```sh
git clone <repository> vmlab-win3d-release
cd vmlab-win3d-release
git checkout <commit or tag>
WIN3D_SIGNING_DIR=/home/wil/dev/vmlab-win3d/.signing scripts/release.sh 0.1.0-alpha
```

The script does the following, and stops at the first failure:

1. Refuses to start if `git status` shows any change or untracked file.
2. Links `.signing` in the checkout to `WIN3D_SIGNING_DIR`, after checking
   that the key, both certificate forms, `SIGN_TIME` and `CERT-INFO.txt`
   are there. The link is removed when the script exits; the key is never
   copied. Without `WIN3D_SIGNING_DIR` the checkout's own `.signing/` is
   used, and `make-test-cert.sh` creates a new certificate if it has none.
   Do not do that for a public release.
3. Runs `scripts/build.sh`: every component, then the signed package in
   `out/package/`.
4. Runs `patches/host/build-qemu.sh`, which fetches exactly the pinned fork
   commit and builds `out/host/qemu/`, then
   `patches/host/build-virglrenderer.sh`, which builds the render server
   from the pinned virglrenderer release and `patches/host/virglrenderer/`
   into `out/host/virglrenderer/`. Both are native builds for the
   x86-64-v2 baseline.
5. Checks that the tree is still clean, that `manifest.json` names the
   shared certificate's thumbprint, and that `out/host/qemu/BUILDINFO`
   names the pinned commit, and that `out/host/virglrenderer/BUILDINFO`
   names the pinned virglrenderer archive and no `workaround/` patch.
6. Writes the zip, the two tarballs, `RELEASE-NOTES.md` and `SHA256SUMS`.
   The zip and the tarballs carry the release commit's date as every
   timestamp.

The result:

```
out/release/<version>/
  vmlab-win3d-<version>-windows-x64.zip
  qemu-vmlab-egl-<version>-x86_64-linux.tar.zst
  virglrenderer-vmlab-<version>-x86_64-linux.tar.zst
  RELEASE-NOTES.md
  SHA256SUMS
```

### Check it before publishing

- Driver zip: in a fresh lab from `labs/probe/vmlab.wcl`, extract the zip in
  the guest, follow its `README.txt`, then run
  `tests/guest/Run-AllProbes.ps1` with the probes from `out/probes/`. Expect
  10 of 10.
- QEMU tarball: extract it, run `./install.sh <temporary prefix>`, and
  boot a Venus guest on an NVIDIA GPU whose `qemu_binary` points at that
  prefix. The console must not be black.
- Render server tarball: extract it, run `./install.sh <temporary prefix>`,
  and give the same NVIDIA GPU's block the `qemu_env` line it prints. The
  VM's `qemu.log` must show `RENDER_SERVER_EXEC_PATH` on its `vmlab: env:`
  line, `ps` must show that `virgl_render_server` running, and
  `tests/e2e/run.sh --host-gpu <that GPU>` must pass 12 of 12.

`docs/evidence/release/` has the record of these checks for each release.
Publish the five files as the assets of a release tagged with the version,
and use `RELEASE-NOTES.md` as its description.

## Install the driver package

You need a Windows 10 or 11 x64 guest with Secure Boot off and a virtio-gpu
Venus GPU. In vmlab, that is a VM with `secure_boot = false` and
`gpu { mode = "vulkan" host_gpu = "<GPU>" }`, as in `labs/probe/vmlab.wcl`.
On an NVIDIA host GPU, install the QEMU asset first (next section), or the
console stays black.

1. Copy `vmlab-win3d-<version>-windows-x64.zip` into the guest, for example
   with `vmlab cp`, and extract it.
2. Open the `vmlab-win3d-<x.y.z>` folder inside and run `Install.cmd` as
   Administrator.
3. Windows restarts once or twice, and setup continues by itself after each
   restart. It is done when
   `powershell -ExecutionPolicy Bypass -File C:\ProgramData\vmlab-win3d\vmlab-win3d.ps1 -Mode Status`
   reports `Installed`.

The zip's `README.txt` has the same steps, the certificate's thumbprint,
and the uninstall command. [`install.md`](install.md) explains what the
installer changes, and [`uninstall.md`](uninstall.md) how to remove it.

> **Warning:** the package is test-signed. Installing it turns on Windows
> test signing and trusts the test certificate machine-wide. Install it only
> in test VMs ([decision 0001](decisions/0001-signing.md)).

## Install the QEMU for NVIDIA hosts

Only hosts whose Venus labs use an NVIDIA GPU need this
([`nvidia.md`](nvidia.md)). You need vmlab 0.26.0-alpha or later and the
distribution's own QEMU package, whose firmware vmlab passes with `-L`.

```sh
tar --zstd -xf qemu-vmlab-egl-<version>-x86_64-linux.tar.zst
cd qemu-vmlab-egl-<version>
./install.sh --check      # ldd check only
./install.sh              # installs into ~/.local/opt/qemu-vmlab-egl
```

`install.sh` needs no root. It takes an optional prefix instead of the
default. It first checks the binary's shared libraries with `ldd`. If any is
missing or too old, it names them and stops; then install those libraries,
or build QEMU for this host with `patches/host/build-qemu.sh`. Otherwise it
copies the binary to `<prefix>/bin`, puts the notes and licences in
`<prefix>/share/doc/qemu-vmlab-egl`, runs `--version`, and prints the line
to add to each NVIDIA GPU block in `~/.config/vmlab/config.wcl`:

```wcl
gpu "Nvidia1" {
  address     = "0000:01:00.0"
  qemu_binary = "/home/<you>/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
}
```

The binary is replaced by a rename, so a QEMU that is already running keeps
its old file. Restart a VM to give it the new one.

## Install the render server for NVIDIA hosts

Only hosts whose Venus labs use an NVIDIA GPU need it. Without it, every
Venus fence on NVIDIA takes at least 10 ms, and windowed D3D9 runs at about
40 frames per second instead of the refresh rate ([`n5.md`](n5.md)). You
need vmlab 0.26.0-alpha or later and the distribution's virglrenderer 1.3.0,
whose `libvirglrenderer` QEMU keeps using; only the render server it starts
is replaced.

```sh
tar --zstd -xf virglrenderer-vmlab-<version>-x86_64-linux.tar.zst
cd virglrenderer-vmlab-<version>
./install.sh --check      # checks only
./install.sh              # installs into ~/.local/opt/virglrenderer-vmlab
```

`install.sh` needs no root, and takes an optional prefix instead of the
default. It checks that the host's virglrenderer is 1.3.0 (the render-server
protocol is private to a version), that `ldd` finds every library the server
needs, that the CPU meets the binary's x86-64 ISA level (x86-64-v2), and
that the server starts. If a check fails, it says which; then build the
server on that host with `patches/host/build-virglrenderer.sh --install`.
Otherwise it copies the server to `<prefix>/libexec`, puts the notes and
licence in `<prefix>/share/doc/virglrenderer-vmlab`, and prints the line to
add to each NVIDIA GPU block in `~/.config/vmlab/config.wcl`, next to its
`qemu_binary`:

```wcl
gpu "Nvidia1" {
  address     = "0000:01:00.0"
  qemu_binary = "/home/<you>/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
  qemu_env    = { RENDER_SERVER_EXEC_PATH: "/home/<you>/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server" }
}
```

A VM picks it up when it next starts. [`nvidia.md`](nvidia.md#the-patched-render-server-on-the-gpus-profile)
says how to check that a VM runs it.
