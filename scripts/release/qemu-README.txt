QEMU @QEMU_VERSION@ with the vmlab egl-headless fix (vmlab-win3d @VERSION@)
======================================================================

What it fixes
-------------
On an NVIDIA host GPU, a Venus guest (vmlab gpu { mode = "vulkan" }) renders,
but stock QEMU shows a black console, and vmlab vm screenshot is black.
NVIDIA's EGL imports a LINEAR dma-buf only as an external texture, and
QEMU's egl-headless display binds the guest's scanout as GL_TEXTURE_2D. This
build imports such a scanout as GL_TEXTURE_EXTERNAL_OES and blits it into a
normal texture before each read-back, so the console shows the guest. AMD
and Intel hosts do not need it.

It is upstream QEMU master at @QEMU_BASE@ (version @QEMU_VERSION@) plus a
series kept on a fork for upstreaming:

  @QEMU_URL@
  branch @QEMU_BRANCH@
  commit @QEMU_COMMIT@

@QEMU_SERIES@

What is in the tarball
----------------------
  bin/qemu-system-x86_64     the x86_64 system emulator, and nothing else
  lib/libvirglrenderer.so.1  virglrenderer 1.3.0 with the vmlab patches, which
                             the binary loads from here (RUNPATH $ORIGIN/../lib)
  install.sh, README.txt, BUILDINFO, licenses/

QEMU is configured with --without-default-features and only what vmlab's
command line uses: KVM and TCG, the egl-headless display (OpenGL), VNC,
virtio-gpu Venus, the TPM emulator back end, USB host passthrough, vhost-user
(virtiofs shares) and qcow2 with zlib and zstd clusters. There is no SDL,
GTK, SPICE, audio, slirp, 9p, iSCSI, NFS, curl or ssh block driver, and no
guest agent or qemu-img (vmlab uses the host's). No firmware or ROMs
(share/qemu) either: vmlab passes -L with the data directory of the host's
own QEMU, so the host also needs its distribution's QEMU package installed.

The bundled virglrenderer is why this QEMU does not use the host's
libvirglrenderer: QEMU's library and the render server it forks talk a
protocol private to a virglrenderer version, distributions ship versions
from 0.10 to 1.3, and the vmlab render server (the other tarball) is 1.3.0.
With the library bundled, the pair always matches whatever the host has.
Give every GPU block that has this qemu_binary the render server's qemu_env
line too; without it the library falls back to /usr/libexec/virgl_render_server,
the host's own server, which may be another version.

Which hosts it suits
--------------------
Any x86-64 Linux with glibc 2.36 or newer and the shared libraries below:
Debian 12 and later, Ubuntu 24.04 and later, Fedora 37 and later, RHEL 10,
Arch Linux and CachyOS, openSUSE Tumbleweed. Ubuntu 22.04 (glibc 2.35) and
RHEL 9 (2.34) are below the floor; install.sh refuses them.

The binary was built in a Debian 12 container (@BUILD_HOST@), so every
glibc symbol it needs exists in glibc 2.36:

@BUILDINFO@

It needs these libraries from the host, all with sonames that have been
stable for many years:

  library            Debian, Ubuntu     Fedora, RHEL     Arch, CachyOS   openSUSE
  libglib-2.0.so.0   libglib2.0-0       glib2            glib2           libglib-2_0-0
  libpixman-1.so.0   libpixman-1-0      pixman           pixman          libpixman-1-0-0
  libepoxy.so.0      libepoxy0          libepoxy         libepoxy        libepoxy0
  libgbm.so.1        libgbm1            mesa-libgbm      mesa            libgbm1
  libz.so.1          zlib1g             zlib-ng-compat   zlib-ng         libz1
  libzstd.so.1       libzstd1           libzstd          zstd            libzstd1
  libusb-1.0.so.0    libusb-1.0-0       libusb1          libusb          libusb-1_0-0

(glibc's libc and libm are on every host.) The graphics stack, epoxy, gbm,
the Vulkan loader and the GPU drivers, comes from the host on purpose: it
must match the host's kernel driver.

The CPU needs x86-64-v2 (SSE4.2 and POPCNT, any x86-64 CPU since about
2009). The binary is compiled with -march=x86-64-v2 -mtune=generic, and its
ELF note says "x86 ISA needed: x86-64-baseline, x86-64-v2" (readelf -n).
QEMU uses AVX2 and AVX-512 only where the CPU has them, chosen at run time.

install.sh checks all of this before it installs anything: the host's glibc
against the floor, ldd for missing or too-old libraries, that the bundled
libvirglrenderer is the one the binary loads, the CPU's ISA level against
the binary's (from /lib64/ld-linux-x86-64.so.2 --help), and that --version
runs. It names what is wrong if a check fails. To check a host without
installing:

  ./install.sh --check

On a host below the floor, or with another need, build QEMU on that host
with patches/host/build-qemu.sh from the vmlab-win3d repository: it builds
the same commit in the same Debian 12 container (Docker, no root), so the
result is this tarball's content, or set WIN3D_ALLOW_OLDER_GLIBC=1 to try
the binary anyway (its own symbols need GLIBC_2.34; unsupported).

Install and use with vmlab
--------------------------
You need vmlab 0.26.0-alpha or later, for the host config's qemu_binary.

  tar --zstd -xf qemu-vmlab-egl-@VERSION@-x86_64-linux.tar.zst
  cd qemu-vmlab-egl-@VERSION@
  ./install.sh --check          # only the checks
  ./install.sh                  # into ~/.local/opt/qemu-vmlab-egl
  ./install.sh /some/prefix     # or anywhere else; no root needed

install.sh copies bin/ and lib/ to the prefix and prints the line to add to
every NVIDIA GPU block in ~/.config/vmlab/config.wcl, next to the render
server's qemu_env:

  gpu "Nvidia1" {
    address     = "0000:01:00.0"
    qemu_binary = "/home/<you>/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
    qemu_env    = { RENDER_SERVER_EXEC_PATH: "/home/<you>/.local/opt/virglrenderer-vmlab/libexec/virgl_render_server" }
  }

Every VM whose gpu {} resolves to that GPU then runs this QEMU. Restart a
running VM for it to take effect.

Licence and source
------------------
QEMU is free software under the GNU GPL version 2 (licenses/COPYING; some
parts are under other compatible licences, see licenses/LICENSE). The
corresponding source is the fork commit above; QEMU's configure fetches the
Meson subprojects at the revisions its subprojects/*.wrap files pin.
virglrenderer (lib/libvirglrenderer.so.1) is under the MIT licence; its
source is the 1.3.0 release archive and the patches named in BUILDINFO.
patches/host/build-qemu.sh in https://github.com/VMLabDev/vmlab-win3d at
commit @COMMIT@ built this tarball's content. BUILDINFO records the build:
the container image, the Debian packages linked, and the NEEDED list.
