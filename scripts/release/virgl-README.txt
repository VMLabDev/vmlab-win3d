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

Only the render server (libexec/virgl_render_server) is included. QEMU
keeps using the host's own libvirglrenderer, which starts the server named
by RENDER_SERVER_EXEC_PATH instead of /usr/lib/virgl_render_server. The two
talk over a private protocol, so the host's virglrenderer must be the same
version, @VIRGL_VERSION@.

Which hosts it suits
--------------------
The server links the host's libdrm, gbm, expat and glibc; none are bundled.
It loads the host's Vulkan loader at run time. It was built on
@BUILD_HOST@, for the x86-64-v2 CPU baseline, against:

@BUILDINFO@

It runs on hosts with virglrenderer @VIRGL_VERSION@ and those libraries at
the same or newer versions, which in practice means a current Arch Linux or
CachyOS. install.sh checks all of this before it installs anything. On any
other host, build it with patches/host/build-virglrenderer.sh from the
vmlab-win3d repository, which builds the same source against the host's
own libraries.

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
