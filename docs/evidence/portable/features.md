# The vmlab QEMU's feature set

How `patches/host/build-qemu.sh` decides what to enable. The QEMU asset is
configured with `--without-default-features`, which turns every `auto`
feature off, and then enables only what vmlab's command line and QMP use.
This page maps each vmlab feature to the QEMU option it emits and to the
configure switch (or the fact that it needs none). The source read is vmlab
`04f68fd` (`chore: release`, the 0.31 line) at `/home/wil/dev/vmlab/src`,
read-only: `qemu/cmdline.rs` (`build_args`, the one argv builder for labs
and template builds), `qemu/container.rs` (micro-VMs), `qemu/hostgpu.rs`,
`qemu/process.rs` (swtpm), `qemu/virtiofsd.rs`, `qmp/client.rs`,
`usb/mod.rs`, `labd/vm.rs`. One real command line is in
`e2e-nvidia1/qemu-argv-env.txt` next to this page.

The result (`abi.txt`): `NEEDED` is `libpixman-1.so.0 libz.so.1
libepoxy.so.0 libzstd.so.1 libusb-1.0.so.0 libgbm.so.1 libvirglrenderer.so.1
libglib-2.0.so.0 libm.so.6 libc.so.6`, ten libraries, of which
`libvirglrenderer.so.1` is bundled. helios's native build of the same commit
needed 55.

## Configure line

```
./configure --prefix=/usr --target-list=x86_64-softmmu
    --with-pkgversion="vmlab <commit>"
    --without-default-features
    --enable-system --disable-user --disable-tools --disable-guest-agent --disable-docs
    --enable-kvm --enable-tcg
    --enable-pixman --enable-opengl --enable-virglrenderer --enable-vnc
    --enable-tpm --enable-libusb --enable-vhost-user --enable-zstd
    --enable-hmp --enable-malloc-trim --enable-stack-protector
    --disable-werror --x86-version=2
    --extra-cflags="-march=x86-64-v2 -mtune=generic" --extra-cxxflags=... --extra-ldflags="-Wl,-z,x86-64-v2"
```

`config-host.txt` is the resulting `config-host.h`, filtered to the
features below.

## Options vmlab emits (`qemu/cmdline.rs`, `qemu/container.rs`)

| vmlab feature | QEMU option emitted | Condition in vmlab | Needs | Configure switch |
| --- | --- | --- | --- | --- |
| VM identity | `-name vmlab:<lab>/<vm>` | always | core | none |
| ROMs of the host QEMU | `-L <stock prefix>/share/qemu` | GPU block has `qemu_binary` and the stock QEMU on `PATH` has `share/qemu` | core (`--prefix=/usr` sets the fallback) | none |
| Machine | `-machine q35` or `pc` (profile) | always | x86_64 default devices | none (`--target-list=x86_64-softmmu`) |
| Machine with virtiofs shares | `-machine ...,memory-backend=mem0` and `-object memory-backend-memfd,id=mem0,size=<N>M,share=on` | VM has virtiofs shares | core (memfd) | none |
| Acceleration | `-accel kvm` | `/dev/kvm` opens read-write and the guest arch is the host's | KVM | `--enable-kvm` |
| Acceleration | `-accel tcg` | `accel = "tcg"` or no usable KVM | TCG | `--enable-tcg` (default `enabled`, passed anyway) |
| CPU | `-cpu host,vmx=off,svm=off`, `-cpu host`, `-cpu max`, `-cpu <cpu_model>` | by accelerator and `nested`/`cpu_model` | core | none |
| CPUs and memory | `-smp <n>`, `-m <n>M` | always | core | none |
| Fixed clock | `-rtc base=<iso>` | `clock = "..."` | core | none |
| Display off | `-display none` | always | core | none |
| Console | `-vnc unix:<run>/vnc.sock,share=ignore` | always (no password, no TLS, no SASL, no websocket) | VNC with pixman | `--enable-vnc --enable-pixman`; `--disable-vnc-jpeg`, `--disable-vnc-sasl`, no gnutls (all off by `--without-default-features`) |
| GL display | `-display egl-headless[,rendernode=/dev/dri/renderDN]` | `gpu.mode` `virgl` or `vulkan` | OpenGL through epoxy, gbm for the render node | `--enable-opengl` (finds epoxy; gbm found by pkg-config, `CONFIG_GBM`) |
| Control | `-qmp unix:<qmp.sock>,server=on,wait=off` | always | core | none |
| No HMP monitor | `-monitor none` | always | none (HMP is enabled anyway, it needs no library) | `--enable-hmp` |
| Serial log | `-serial file:<serial.log>` | always, unless the agent uses `isa-serial`; then `-chardev file,id=serlog,...` and `-device isa-serial,chardev=serlog,index=1` | chardev `file`, `isa-serial` | none |
| UEFI | `-drive if=pflash,format=raw,readonly=on,file=OVMF_CODE` and `-drive if=pflash,format=raw,file=OVMF_VARS` | `firmware = ovmf` | block `raw` + `file` | none (always built) |
| Secure Boot flash | `-global driver=cfi.pflash01,property=secure,value=on` | OVMF + `secure_boot` | core | none |
| Agent channel | `-chardev socket,id=vagent0,path=<agent.sock>,server=on,wait=off`, `-device virtio-serial-pci`, `-device virtserialport,chardev=vagent0,name=vmlab.agent.0` | default `agent_transport` | chardev `socket` (unix), virtio-serial | none |
| Agent channel, old guests | `-device isa-serial,chardev=vagent0,index=0` | `agent_transport = "isa-serial"` (windows-xp, 9x, templeos profiles) | `isa-serial` | none |
| Disks | `-blockdev driver=qcow2,node-name=diskN,file.driver=file,file.filename=<qcow2>` and `-device virtio-blk-pci,drive=diskN[,bootindex=]` | always (linked clones); `disk_bus = "virtio"` | qcow2 (zlib clusters; zstd clusters readable too) | none for qcow2 and `file`; zlib is required by QEMU; `--enable-zstd` for zstd-compressed qcow2 |
| Disks on IDE or SATA | `-device ide-hd,drive=diskN,bus=ide.B,unit=U` | `disk_bus` `ide` or `sata` | AHCI (q35), PIIX IDE (pc) | none |
| CD-ROMs and ISO media | `-blockdev driver=raw,node-name=cdN,read-only=on,file.driver=file,file.filename=<iso>` and `-device ide-cd,drive=cdN,bus=ide.B,unit=U[,bootindex][,id=install-cd]` | `cdrom =` or ISO `media {}` | `raw`, `ide-cd` | none |
| Floppies | `-drive if=floppy,format=raw,file=<img>` | `floppy =` or floppy media | floppy controller | none |
| Network, default | `-netdev stream,id=netN,server=off,addr.type=unix,addr.path=<sock>` and `-device virtio-net-pci,netdev=netN,mac=<mac>[,host_mtu=M]` | every NIC block; the userspace switch does NAT, DHCP and DNS in vmlab | netdev `stream` (QEMU 7.2+) | none (no slirp: `--disable-slirp`, vmlab never emits `-netdev user`) |
| Network, AF_XDP fast path | `-netdev tap,id=netN,fd=<10+i>` with the tap fd mapped into the child | fast-path tier `AfXdp` | netdev `tap` with a pre-opened fd; no `vhost=on`, no multiqueue | none (`vhost-net`/`vhost-kernel` not needed, left off) |
| Other NIC models | `-device e1000,...`, `-device pcnet,...`, or any name | profile `nic_model` | x86 default devices | none |
| No NIC | `-nic none` | no NIC blocks | core | none |
| TPM | `-chardev socket,id=chrtpm,path=<swtpm.sock>`, `-tpmdev emulator,id=tpm0,chardev=chrtpm`, `-device tpm-tis,tpmdev=tpm0` | `tpm = true`; vmlab runs `swtpm socket --tpm2 ...` itself | TPM emulator back end | `--enable-tpm` (no library; swtpm is the host's) |
| GPU, Venus | `-device virtio-vga-gl,venus=true,blob=true,hostmem=4G` (q35/pc) or `virtio-gpu-gl` (`virt`) | `gpu.mode = "vulkan"` | virglrenderer with Venus | `--enable-virglrenderer`, against the bundled virglrenderer 1.3.0 (`-Dvenus=true`) |
| GPU, virgl | `-device virtio-vga-gl` | `gpu.mode = "virgl"` | virglrenderer | same |
| GPU passthrough | `-device vfio-pci,host=<addr>` | `gpu.mode = "passthrough"` | VFIO_PCI (Kconfig, Linux) | none |
| Display device without a GPU block | `-device virtio-vga`, `virtio-gpu-pci`, `VGA`, `qxl-vga`, `cirrus-vga` (windows-9x), or any name | `display_device` | x86 default devices; `qxl-vga` builds without SPICE | none (checked: all five answer `-device X,help`) |
| `-vga none` | `-vga none` | GL mode on a `virt` machine | core | none |
| USB | `-usb`, `-device usb-tablet` | every non-`virt` machine | USB core, UHCI/EHCI on q35 | none |
| USB controller for passthrough | `-device qemu-xhci,id=usbx` | `usb = true` | xHCI | none |
| USB passthrough (`vmlab usb`) | QMP `device_add {driver:"usb-host", bus:"usbx.0", hostbus, hostaddr, hostport}` and `device_del` | at run time | `usb-host` needs libusb | `--enable-libusb` |
| Shared folders (virtiofs) | `-chardev socket,id=vfsN,path=<virtiofsN.sock>` and `-device vhost-user-fs-pci,chardev=vfsN,tag=<tag>` | each virtiofs share; vmlab runs `virtiofsd` itself | vhost-user | `--enable-vhost-user` (no library) |
| Shared folders (SMB) | nothing: `smbd` serves them over the vmlab network | SMB shares | none | none |
| Start paused | `-S`, then QMP `cont` | always | core | none |
| Extra arguments | `qemu_args` verbatim; in the repository's labs `-smbios type=1,serial=...`, `-trace monitor_qmp_cmd_in_band`, `-fw_cfg` | lab or template | core (`-trace` uses the `log` back end, the default) | none |
| Container micro-VMs | `-kernel`, `-initrd`, `-append`, `-blockdev driver=raw` (squashfs) and `qcow2`, `virtio-serial-pci`, `virtserialport`, `virtio-blk-pci`, `virtio-net-pci`, `vhost-user-fs-pci`, `-vga none` | containers; always the stock `qemu-system-<arch>`, never a GPU's `qemu_binary` | core | none (the asset is not used for them) |

Never emitted, so left off with everything they would link: `-spice`
(spice-server), `-audiodev` (ALSA, PulseAudio, PipeWire, JACK, sndio),
`-display gtk|sdl|dbus|curses` (GTK, SDL, gio, ncurses), `-netdev user`
(libslirp), `-netdev vde|netmap|af-xdp` (libvdeplug, libxdp, libbpf),
`-virtfs`/`-fsdev` (libcap-ng, libattr), `-sandbox` (libseccomp), `-numa`
(libnuma), block drivers `iscsi`, `nfs`, `ssh`, `curl`, `rbd`, `gluster`,
`vvfat`, `vmdk`, `vdi`, `vhdx`, `vpc`, `qcow1`, `dmg` (bzip2, lzfse),
`parallels`, `bochs`, `cloop`, `replication`; migration compression (lzo,
snappy, qpl, uadk, qatzip); `aio=native` and `aio=io_uring` (libaio,
liburing; vmlab leaves the default, `threads`); the disassembler
(capstone); `keyring` secrets (libkeyutils); `u2f`, `canokey`, `usb-redir`,
`smartcard` (libcacard), `brlapi`, `rutabaga`, `fuse` exports, `libdw`,
`libdaxctl`, `libpmem`, `libudev` (only `u2f-passthru` and `qemu-pr-helper`
use it); `rdma`; the Rust devices; the guest agent, `qemu-img` and the
other tools (vmlab runs the host's `qemu-img` and `swtpm`); TLS and crypto
libraries (gnutls, nettle, gcrypt: VNC runs without authentication on a
unix socket, no LUKS images). `fdt` is built from the `dtc` subproject into
the binary (`CONFIG_FDT`), not linked from the host.

Library-free features kept on although vmlab does not need them: HMP (for
`human-monitor-command` by hand), `malloc-trim`, the stack protector, AVX2
and AVX-512BW code paths chosen at run time (`CONFIG_AVX2_OPT`,
`CONFIG_AVX512BW_OPT`, detected by meson, not options).

## QMP commands vmlab issues (`qmp/client.rs`)

All present in the asset (`query-commands` returned 238 commands; checked
on helios under TCG, `-nodefaults -S -qmp stdio`):

`qmp_capabilities`, `system_powerdown`, `system_wakeup`, `quit`, `stop`,
`cont`, `query-status`, `eject` and `blockdev-close-tray` (the `install-cd`
drive), `screendump` (no `format`, so PPM; vmlab converts to PNG itself and
falls back to an RFB capture when the GL display answers "no surface", so
`--enable-png` is not needed), `send-key`, `input-send-event`, `device_add`
and `device_del` (USB), `snapshot-save`, `snapshot-load`,
`snapshot-delete`, `query-jobs`, `job-dismiss`. Events read: `SHUTDOWN`,
`SUSPEND`, `SUSPEND_DISK`, `WAKEUP`, `RESET`, `RESUME`, `STOP`,
`DEVICE_DELETED`. Nothing in vmlab uses migration, `savevm`,
`dump-guest-memory`, VNC passwords or the guest agent.

## Checked on the built binary (helios, `out/host/qemu/bin/qemu-system-x86_64`)

- `-device X,help` answers for `virtio-vga-gl virtio-gpu-gl virtio-vga
  virtio-gpu-pci VGA cirrus-vga qxl-vga usb-host usb-tablet qemu-xhci
  vhost-user-fs-pci tpm-tis ide-hd ide-cd virtio-blk-pci virtio-net-pci
  e1000 pcnet isa-serial virtio-serial-pci virtserialport vfio-pci`.
- `-display help`: `none`, `egl-headless`. `-accel help`: `tcg kvm`.
- `-netdev help`: `socket stream dgram hubport tap bridge vhost-user`.
- `-chardev help` includes `socket file`. `-tpmdev help`: `passthrough
  emulator`. `-object help` includes `memory-backend-memfd`.
- `-drive format=help` includes `qcow2 raw file`.
- `-machine q35 -accel kvm -display egl-headless,rendernode=/dev/dri/renderD129
  -device virtio-vga-gl,venus=true,blob=true,hostmem=4G -nodefaults -S`
  starts and answers QMP (the bundled virglrenderer forked the render
  server named by `RENDER_SERVER_EXEC_PATH`; the real run is
  `e2e-nvidia1/`).

Done when every option above maps to an enabled feature or to one that
needs no library: it does. The three features that cost a host library
beyond glib, pixman, zlib and the GL stack are USB passthrough (libusb),
zstd qcow2 clusters (libzstd) and Venus (the bundled virglrenderer).
