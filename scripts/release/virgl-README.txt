vmlab render server: virglrenderer @VIRGL_VERSION@ with the vmlab fence fix (vmlab-win3d @VERSION@)
==========================================================================================

What it fixes
-------------
On an NVIDIA host GPU, every Venus fence of a guest took at least 10 ms to
come back, so a Windows guest's windowed D3D9 ran at about 40 frames per
second instead of the refresh rate, and DWM composed below it.
virglrenderer's Venus renderer creates the fences its queue threads wait on
as exportable SYNC_FD fences, although it never exports them, and NVIDIA's
driver (610.57) wakes vkWaitForFences on such a fence up to about 10 ms
late. This server creates them as plain fences: the fence floor drops from
10.2 ms to about 1.2 ms. AMD and Intel hosts do not need it, and it changes
nothing else.

It is virglrenderer @VIRGL_VERSION@ (@VIRGL_URL@,
sha256 @VIRGL_SHA@) with these patches from
patches/host/virglrenderer/ in the vmlab-win3d repository:

@PATCHES@

Only the render server (libexec/virgl_render_server) is included. The vmlab
QEMU (the other tarball, qemu-vmlab-egl) bundles the libvirglrenderer of
the same build in its lib/, and that library forks the server named by
RENDER_SERVER_EXEC_PATH. The two talk over a protocol private to a
virglrenderer version, and since both are this build's @VIRGL_VERSION@, the
host's own virglrenderer version does not matter. Use this server only with
that QEMU: a stock QEMU with another virglrenderer version cannot pair with it.

Which hosts it suits
--------------------
Any x86-64 Linux with glibc 2.36 or newer and libgbm: Debian 12 and later,
Ubuntu 24.04 and later, Fedora 37 and later, RHEL 10, Arch Linux and
CachyOS, openSUSE Tumbleweed. Ubuntu 22.04 (glibc 2.35) and RHEL 9 (2.34)
are below the floor; install.sh refuses them.

The server was built in a Debian 12 container (@BUILD_HOST@), for the
x86-64-v2 CPU baseline:

@BUILDINFO@

From the host it needs only libgbm.so.1 (Debian and Ubuntu: libgbm1;
Fedora: mesa-libgbm; Arch: mesa; openSUSE: libgbm1) and glibc, and at run
time it dlopens the host's Vulkan loader (libvulkan.so.1) and, through it,
the host GPU's Vulkan driver. Those must be the host's own.

install.sh checks all of this before it installs anything: the host's glibc
against the floor, ldd, the CPU's ISA level against the server's, and that
the server starts. To check a host without installing:

  ./install.sh --check

On a host below the floor, build the server there with
patches/host/build-virglrenderer.sh --install from the vmlab-win3d
repository (it builds in the same Debian 12 container; Docker, no root),
or set WIN3D_ALLOW_OLDER_GLIBC=1 to try the binary anyway (unsupported).

Install and use with vmlab
--------------------------
You need vmlab 0.26.0-alpha or later, for the host config's qemu_env.

  tar --zstd -xf virglrenderer-vmlab-@VERSION@-x86_64-linux.tar.zst
  cd virglrenderer-vmlab-@VERSION@
  ./install.sh --check          # only the checks
  ./install.sh                  # into ~/.local/opt/virglrenderer-vmlab
  ./install.sh /some/prefix     # or anywhere else; no root needed

install.sh prints the line to add to every NVIDIA GPU block in
~/.config/vmlab/config.wcl, next to the qemu_binary of the vmlab QEMU:

  gpu "Nvidia1" {
    address     = "0000:01:00.0"
    qemu_binary = "/home/<you>/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
    qemu_env    = { RENDER_SERVER_EXEC_PATH: "/home/<you>/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server" }
  }

Every VM whose gpu {} resolves to that GPU then starts this server. Restart
a running VM for it to take effect. vmlab status -v and the vmlab: env: line
at the top of the VM's qemu.log show the variable.

Licence and source
------------------
virglrenderer is under the MIT licence (licenses/COPYING). The source is
the release archive above plus the patches listed, and
patches/host/build-virglrenderer.sh in https://github.com/VMLabDev/vmlab-win3d
at commit @COMMIT@ built this server from them. BUILDINFO records the build.
