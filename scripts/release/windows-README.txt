vmlab-win3d @VERSION@ for Windows 10 and 11 (x64)
==================================================

What this is
------------
A driver package for Windows guests running under vmlab (QEMU). After one
install, unmodified Direct3D 9, 10, 11 and 12, OpenGL and Vulkan
applications render on the host's GPU. The guest reaches the host's Vulkan
driver through virtio-gpu Venus. The package contains:

  - the Yttrium viogpu3d WDDM kernel driver, test-signed;
  - Mesa's Venus Vulkan driver, the Mesa Direct3D 9-11 user-mode driver and
    OpenGL through Mesa Zink;
  - Direct3D 12 through vkd3d-proton, behind a system d3d12.dll bridge;
  - the Khronos Vulkan loader and vulkaninfo.

This is an alpha release, for test VMs only.

Built from https://github.com/VMLabDev/vmlab-win3d at commit
@COMMIT@.

Prerequisites
-------------
  - Windows 10 or 11, x64.
  - Secure Boot off. In vmlab: secure_boot = false on the VM.
  - A virtio-gpu GPU with Venus. In vmlab: gpu { mode = "vulkan" }, with
    host_gpu naming an AMD, Intel or NVIDIA GPU from the host config. An
    NVIDIA host GPU also needs the patched QEMU (the qemu-vmlab-egl
    release asset) as that GPU's qemu_binary, or the console stays black.
  - Administrator rights in the guest.

WARNING: test signing
---------------------
The kernel driver is signed with a self-made test certificate, not by
Microsoft. Install.cmd turns on Windows test signing (Test Mode) and imports
the certificate into the machine's Trusted Root and Trusted Publisher
stores. That certificate is then trusted machine-wide, for code that anyone
holding its private key signs. Install this package only in test VMs that
you can throw away. Never install it on a computer you use for anything
else.

  Certificate subject:     @CERT_SUBJECT@
  SHA-1 thumbprint:        @CERT_SHA1@

Install
-------
1. Copy this zip into the guest and extract it, for example to
   C:\vmlab-win3d-@VERSION@.
2. Open the folder @BUNDLE@ inside it. Right-click Install.cmd and choose
   "Run as administrator" (or run it from an elevated command prompt).
3. Windows restarts once or twice. Setup continues by itself after each
   restart. Do not log off, shut down or run Install.cmd again until setup
   is done.
4. Setup is done when
     powershell -ExecutionPolicy Bypass -File C:\ProgramData\vmlab-win3d\vmlab-win3d.ps1 -Mode Status
   reports "Installed". The log is C:\ProgramData\vmlab-win3d\journal.log.

To check: run vulkaninfo --summary in a command prompt. It lists a
"Virtio-GPU Venus (<host GPU>)" device.

Uninstall
---------
Run C:\ProgramData\vmlab-win3d\bundle\Uninstall.cmd as administrator.
Windows restarts once. Uninstall removes the driver, restores d3d12.dll, removes the test
certificate and sets test signing back to how it was.

More information
----------------
  Install and what it changes: https://github.com/VMLabDev/vmlab-win3d/blob/@COMMIT@/docs/install.md
  Uninstall:                   https://github.com/VMLabDev/vmlab-win3d/blob/@COMMIT@/docs/uninstall.md
  Known limits and host GPUs:  https://github.com/VMLabDev/vmlab-win3d/blob/@COMMIT@/docs/known-limits.md
  Signing decision:            https://github.com/VMLabDev/vmlab-win3d/blob/@COMMIT@/docs/decisions/0001-signing.md
  Components and licences:     https://github.com/VMLabDev/vmlab-win3d/blob/@COMMIT@/docs/stack.md
