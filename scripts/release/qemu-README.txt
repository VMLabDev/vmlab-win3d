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

Only the x86_64 system emulator (bin/qemu-system-x86_64) is included. It
has no firmware or ROMs (share/qemu): vmlab passes -L with the data
directory of the host's own QEMU, so the host also needs its distribution's
QEMU package installed.

Which hosts it suits
--------------------
The binary links the host's shared libraries; none are bundled. It was
built on @BUILD_HOST@, against:

@BUILDINFO@

BUILDINFO lists every library it needs (NEEDED). It runs on hosts that have
all of them at the same or newer versions, which in practice means a
current Arch Linux or CachyOS (any repository level: v3, v4 or znver4)
with the distribution's qemu-full package installed.

The CPU needs x86-64-v2 (SSE4.2 and POPCNT, any x86-64 CPU since about
2009). The binary is compiled with -march=x86-64-v2 -mtune=generic, and its
ELF note says "x86 ISA needed: x86-64-baseline, x86-64-v2" (readelf -n).
QEMU uses AVX2 and AVX-512 only where the CPU has them, chosen at run time.
The build host's shared libraries are not included and do not matter: the
binary loads the target host's own.

install.sh checks all of this before it installs anything: ldd for missing
or too-old libraries, the CPU's ISA level against the binary's (from
/lib64/ld-linux-x86-64.so.2 --help), and that --version runs. It names
what is wrong if a check fails. On any other host, rebuild it with
patches/host/build-qemu.sh from the vmlab-win3d repository, which builds
the same commit against the host's own libraries.

Install and use with vmlab
--------------------------
You need vmlab 0.26.0-alpha or later, for the host config's qemu_binary.

  tar --zstd -xf qemu-vmlab-egl-@VERSION@-x86_64-linux.tar.zst
  cd qemu-vmlab-egl-@VERSION@
  ./install.sh --check          # only the checks
  ./install.sh                  # into ~/.local/opt/qemu-vmlab-egl
  ./install.sh /some/prefix     # or anywhere else; no root needed

install.sh prints the line to add to every NVIDIA GPU block in
~/.config/vmlab/config.wcl:

  gpu "Nvidia1" {
    address     = "0000:01:00.0"
    qemu_binary = "/home/<you>/.local/opt/qemu-vmlab-egl/bin/qemu-system-x86_64"
  }

Every VM whose gpu {} resolves to that GPU then runs this QEMU. Restart a
running VM for it to take effect.

Licence and source
------------------
QEMU is free software under the GNU GPL version 2 (licenses/COPYING; some
parts are under other compatible licences, see licenses/LICENSE). The
corresponding source is the fork commit above; QEMU's configure fetches the
Meson subprojects at the revisions its subprojects/*.wrap files pin.
patches/host/build-qemu.sh in https://github.com/VMLabDev/vmlab-win3d at
commit @COMMIT@ built this binary from it. BUILDINFO records the build.
