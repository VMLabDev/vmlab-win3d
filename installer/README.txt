vmlab-win3d: hardware-accelerated Direct3D 9-12, OpenGL and Vulkan for
Windows 10 and 11 guests on vmlab, rendered by the host's Vulkan driver over
virtio-gpu Venus.

Requirements: a virtio-gpu GL device with Venus, Secure Boot off
(vmlab: secure_boot = false). The package is test-signed; installing it turns
on Windows test signing.

Install:    run Install.cmd from an elevated command prompt. Windows restarts
            once or twice; setup continues by itself after each restart.
Uninstall:  run Uninstall.cmd from an elevated command prompt. Windows
            restarts once.
Status:     powershell -ExecutionPolicy Bypass -File vmlab-win3d.ps1 -Mode Status
Log:        C:\ProgramData\vmlab-win3d\journal.log

Full documentation: docs/install.md and docs/uninstall.md in the vmlab-win3d
repository.
