# 0001: Kernel driver signing — test signing with Secure Boot off

- Status: accepted
- Date: 2026-10-17
- Decides: plan.md D2

## Context

64-bit Windows loads a kernel driver only when Code Integrity (`ci.dll`)
accepts its signature, and PnP adds a driver package to the driver store only
when a signed catalog covers every file the INF installs. The package carries
the Yttrium viogpu3d KMD, built here from source (`build/kmd/`). Nothing
Microsoft signed is available for it.

The package must install on a stock vmlab `windows-11` template guest
(26100.1742.5) with one documented command, and the build must stay
scriptable on this Linux host.

## Options considered

1. **Test signing, Secure Boot off.** Sign with a self-made code-signing
   certificate. On the guest, trust that certificate (Root and
   TrustedPublisher) and turn on `testsigning` in the boot configuration.
   The kernel then accepts test-signed drivers. The prior art used this and
   proved it (prior-art §5).
2. **Attestation signing through the Partner Center hardware dashboard.**
   Microsoft signs the package, and it loads with Secure Boot on and no
   boot-policy change on Windows 10 and 11 client. It needs an EV
   code-signing certificate, a Partner Center (Hardware Dev Center) account
   tied to a verified company, and an upload round trip for every build. It
   is not valid for Windows Server, and it is a distribution decision for
   third-party code (Yttrium is not ours). None of those prerequisites exist
   here.
3. **WHQL (HLK) signing.** The same account and EV certificate, plus a passing
   Hardware Lab Kit run for a display driver. That cannot be done for an
   alpha WDDM driver.
4. **Secure Boot with custom keys.** Enrol our own PK, KEK and db in the VM's
   OVMF and sign with a key in db. This does not help. UEFI db decides which
   *boot loaders* firmware runs. Windows kernel Code Integrity does not
   consult UEFI db for kernel modules: it trusts only the Microsoft roots
   built into CI, plus the test roots when `testsigning` is on. And while
   Secure Boot is on, the Windows boot manager refuses the `testsigning` (and
   `nointegritychecks`) BCD options, so a db-signed driver still fails to
   load. Custom keys would only let us sign our own boot loader, which we do
   not need.
5. **Disable signature enforcement** (`F8` "Disable driver signature
   enforcement", or `nointegritychecks`). The F8 option lasts one boot and
   needs an interactive boot menu. `nointegritychecks` is ignored on Windows
   10 and later. Not usable for an unattended install.
6. **Legacy cross-signing.** Microsoft stopped accepting new cross-signed
   kernel drivers in 2021; Windows 10 1607+ rejects them with Secure Boot on.

## Decision

Use option 1: test signing with Secure Boot off.

- The certificate is created once per checkout by
  `build/signing/make-test-cert.sh` into `.signing/` at the repo root, which
  is never committed. It is RSA 3072, SHA-256, code-signing EKU, valid 10
  years, subject
  `CN=vmlab-win3d Test Driver Signing, O=vmlab-win3d local test (not for distribution)`.
  `.signing/CERT-INFO.txt` records subject, validity and thumbprints.
- `build/signing/sign-driver.sh <dir>` signs on Linux: osslsigncode embeds
  SHA-256 Authenticode signatures in every PE file, LINBIT
  `generate-cat-file` writes the catalog the INF names, and osslsigncode signs
  the catalog. Signing time and catalog GUID are fixed, so re-signing is
  byte-identical. No Windows tool (Inf2Cat, signtool) is needed.
- The catalog's member hashes are SHA-1 (what `generate-cat-file` writes);
  the signatures on it and on the PE files are SHA-256. Windows 11 26100
  accepts this for a test-signed package in `pnputil /add-driver /install`:
  setupapi logs "The INF was signed with an Authenticode(tm) catalog from a
  trusted publisher", signer score `0x0F000000 (Authenticode)` (evidence
  below). The first check against the WHQL policy fails with 0x800b0109,
  which is normal for any non-Microsoft root.
- No timestamp is applied (that needs a network TSA). The signatures stay
  valid while the certificate is, until 2036.

The VM side is a vmlab setting: `secure_boot = false` in the lab file. That
cannot be changed from inside the guest.

## What an installer must do on the guest

Run as Administrator (or as SYSTEM through `vmlab exec`) with the signed
package and `test-signing.cer` copied to the guest. Secure Boot must already
be off.

```bat
rem 0. Check Secure Boot is off; testsigning is ignored when it is on.
rem    Must print False, or fail with "Variable is currently undefined"
rem    (what a vmlab secure_boot = false VM reports).
powershell -NoProfile -Command "Confirm-SecureBootUEFI"

rem 1. Trust the test certificate. -f creates TrustedPublisher, which the
rem    template does not have, and makes the import non-interactive.
certutil -f -addstore Root             test-signing.cer
certutil -f -addstore TrustedPublisher test-signing.cer

rem 2. Allow test-signed kernel code from the next boot.
bcdedit /set {current} testsigning on

rem 3. Disable hibernation, and with it Fast Startup (see below).
powercfg /hibernate off

rem 4. Reboot so the boot policy takes effect.
shutdown /r /t 0
```

After the reboot:

```bat
rem 5. Add the package to the driver store and install it on the device.
rem    Exit 0 = done, 3010 = done, reboot required.
pnputil /add-driver viogpu3d.inf /install

rem 6. Reboot so dxgkrnl starts the new display driver cleanly.
shutdown /r /t 0
```

That is two reboots. The desktop then shows the "Test Mode" watermark.

The template already carries Red Hat's WHQL-signed `viogpudo.inf` for the
same hardware ID. Both packages get driver rank `00F90000` for the exact
hardware ID, so PnP breaks the tie on driver date and version, and ours
(09/27/2026, 100.6.101.58100) wins over viogpudo (07/09/2025,
100.101.104.28500). pnputil installed it with no removal step. Keep the
package's DriverVer date newer than any viogpudo the guest may receive; if
a newer viogpudo arrives (Windows Update), it would take the device back,
and the installer would then have to remove it with
`pnputil /delete-driver <oemNN.inf> /uninstall` before installing.

Verify:

```bat
pnputil /enum-devices /class Display /drivers
powershell -NoProfile -Command "Get-PnpDevice -Class Display | Get-PnpDeviceProperty DEVPKEY_Device_ProblemCode,DEVPKEY_Device_DriverInfPath"
```

The device's driver INF is the package's `oemNN.inf` and its problem code is
0. Code 52 means the signature was rejected (certificate not trusted, or
testsigning not active); check `bcdedit /enum {current}` and the
`Microsoft-Windows-CodeIntegrity/Operational` log.

Uninstall reverses it:

```bat
pnputil /delete-driver oemNN.inf /uninstall
bcdedit /deletevalue {current} testsigning
certutil -delstore TrustedPublisher <sha1 thumbprint>
certutil -delstore Root             <sha1 thumbprint>
shutdown /r /t 0
```

Restore `testsigning` with `/deletevalue` only when it was absent before the
install (the template has no entry), and re-enable hibernation only if it was
on before.

## Fast Startup and hibernation hazard

With Fast Startup on (the Windows default whenever hibernation is enabled),
"Shut down" does not end the kernel session: Windows hibernates it and the
next power-on resumes it. A driver installed, updated or removed before such
a shutdown, and graphics state generally, comes back stale on the next start
instead of reinitialising. Yttrium's README requires `powercfg /hibernate off`
for this reason, and the prior art's final installer did not set it (prior-art
§5.2). `vmlab vm stop` and `vmlab down` send an ACPI power-off, which Windows
treats as a Fast Startup shutdown. "Restart" (`shutdown /r`) always does a
full boot, so the installer's own reboots are safe, but a later stop and
start would not be. The installer must therefore run
`powercfg /hibernate off`. The load test in this slice reboots only with
`shutdown /r`.

## Consequences

- Every guest that runs the package has test signing on and Secure Boot
  off. That lowers the guest's protection against boot kits and unsigned
  kernel code; acceptable for lab VMs, not for anything else.
- Each checkout has its own key, so packages signed in different checkouts
  carry different certificates. A guest must trust the certificate of the
  checkout that signed its package.
- Moving to attestation signing later changes only `sign-driver.sh` and
  drops installer steps 1, 2 and 4; the build does not change.

## Evidence

`docs/evidence/kmd/README.md` indexes the load test on a fresh `windows-11`
lab (`build/signing/load-test.sh`). In short: `pnputil /add-driver
/install` accepted the Linux-made catalog as an Authenticode catalog from a
trusted publisher, installed it on the virtio-gpu device, the kernel loaded
`viogpu3d.sys` and started the device (Kernel-PnP event 410), and Code
Integrity logged no rejection. The device then reports Code 43 because the
kernel-only package has no UMDs (prior-art §3), not Code 52.
