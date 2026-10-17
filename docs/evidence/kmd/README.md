# Evidence: viogpu3d KMD build and test signing

Run on 2026-10-17 from commit `e166465` by:

```sh
build/kmd/build.sh                                   # Docker, no network
cp out/kmd/x64/{viogpu3d.sys,viogpu3d.inf,viogpuap.exe,vgpusrv.exe} <pkg>/
build/signing/sign-driver.sh <pkg>                   # osslsigncode + generate-cat-file
build/signing/load-test.sh <pkg> docs/evidence/kmd   # fresh windows-11 lab, destroyed after
```

The guest was a fresh clone of the local `windows-11` template (Windows 11
24H2, build 26100.9457), `secure_boot = false`,
`display = "virtio-vga-gl"` with egl-headless on the AMD iGPU
(`/dev/dri/renderD129`).

| File | What it shows |
| --- | --- |
| `00-signing-host.txt` | Host side: `sign-driver.sh` run twice gives identical bytes; catalog members and hardware ID; `osslsigncode verify` of every file; build pins and output hashes (`BUILDINFO`); certificate subject, validity and thumbprints |
| `01-baseline.txt` | The lab file; OS build; Secure Boot off (`Confirm-SecureBootUEFI`: variable undefined); no `testsigning` in BCD; device on Red Hat `viogpudo` (oem7.inf) |
| `02-trust.txt` | `certutil -f -addstore Root` and `TrustedPublisher`, `bcdedit /set {current} testsigning on`, `powercfg /hibernate off` |
| `03-install.txt` | BCD after reboot shows `testsigning Yes`; package hashes on the guest; `Get-AuthenticodeSignature` is `Valid` for `.sys`, `.exe` and `.cat`; `pnputil /add-driver viogpu3d.inf /install` adds `oem15.inf` and installs it on the device; ranking: ours and viogpudo both `00F90000`, ours wins on date/version |
| `04-after-install.txt` | Device bound to `oem15.inf`, version 100.6.101.58100 |
| `05-after-reboot.txt` | After a reboot: still bound to `oem15.inf`; Kernel-PnP event 410 "Device … was started. Driver Name: oem15.inf … Service: VioGpu3D"; driver-store copies `Valid`; **no Code Integrity events** for any package file; `vgpusrv` service running; problem code 43 |
| `06-setupapi-dev-log.txt` | setupapi from the `pnputil` section on: "The INF was signed with an Authenticode(tm) catalog from a trusted publisher", signer score `0x0F000000 (Authenticode)`, "Installing best driver (oem15.inf)", exit status SUCCESS |
| `07-console-ocr.txt` | OCR of the console (lock screen clock only; not meaningful) |

## Reading it

- **Signature accepted.** PnP accepted the catalog (setupapi), the kernel
  loaded `viogpu3d.sys` and started the device (Kernel-PnP 410, Service
  VioGpu3D), and Code Integrity logged no rejection. There is no Code 52.
- **Code 43 remains** (`CM_PROB_FAILED_POST_START`). The package installs the
  kernel driver and helpers only, with no D3D or OpenGL UMDs and no
  `UserModeDriverName`; prior-art §3 records Code 43 after a kernel-only
  install until the runtime was deployed. This slice did not isolate the
  cause further. On plain `virtio-vga` (no virgl) the KMD fails to start for
  a different reason: `viogpu_adapter.cpp` requires `VIRTIO_GPU_F_VIRGL`.
- `sc query VioGpu3D` shows `STOPPED` with exit 1077: dxgkrnl starts display
  miniports itself, not through the SCM, and the miniport is unloaded again
  after the post-start failure.

## Earlier runs (not in these files)

Two defects were found in the guest before this run and fixed in `e166465`:

1. The stamped INF said `NTamd64...16299`, which puts 16299 in the
   ProductType field, so the package matched no device (setupapi "Unable to
   find any matching devices"). Upstream stamps `amd64.10.0`.
2. `lld-link /SECTION:INIT,d` replaced INIT's attributes, leaving
   `DriverEntry` non-executable. The guest bugchecked 0x1A
   (0x101b, INIT page address, …, 0xc0000005) the moment the driver loaded,
   which also showed the test signature had been accepted. Now `INIT,ERD`.
