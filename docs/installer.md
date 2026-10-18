# Keeping the display device on this package

A newer Red Hat `viogpudo` from Windows Update could take the virtio-gpu
device away from this package. This page explains why, describes the fix,
and gives the evidence. The fix has three parts: the INF outranks
`viogpudo`, the installer forces this package onto the device when PnP
picks another driver, and a device installation policy pins the one device
instance so that Windows installs no other driver on it. Uninstall removes
the pin. Investigated on 2026-10-18 on helios with vmlab 0.28.0-alpha, the
`windows-11@26100.1742.5` template and bundle 0.1.0. Evidence is in
`docs/evidence/installer/`.

## Result

| Item | Result |
| --- | --- |
| Cause | PnP ranks this package and Red Hat's WHQL `viogpudo` the same, `00F90000`. Both match the full hardware ID `PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01`, both say `FeatureScore=F9`, and the signer byte is `00` for both, so the later DriverVer date wins. A `viogpudo` dated after 09/27/2026 therefore takes the device |
| Fix, INF | `FeatureScore=E0` (`build/package/make-inf.py`). The package ranks `00E00000` and beats any driver for this device that keeps the usual display score, whatever its date |
| Fix, installer | When PnP still binds another driver, `vmlab-win3d.ps1` installs this package with `UpdateDriverForPlugAndPlayDevices` and `INSTALLFLAG_FORCE`. It then pins the device instance with the device installation policy `DenyInstanceIDs`, which blocks every later driver install on that device, including from Windows Update |
| Uninstall | Removes the pin before it deletes the package, forces the baseline driver back if PnP picks another one, and puts the policy keys back as the baseline recorded them |
| Proof | See [Evidence](#evidence) |

## Ranking

PnP gives each candidate driver a rank, `0xSSGGTHHH`, and installs the one
with the lowest rank:

| Field | Meaning | This package | Template `viogpudo` |
| --- | --- | --- | --- |
| `SS` | Signer score. Windows client editions count every trusted signature the same ("all signing equal"), so a test signature and WHQL both give `00` | `00` | `00` |
| `GG` | The INF's `FeatureScore` (`FF` when absent) | `E0` (was `F9`) | `F9` |
| `T` | Hardware ID (`0`) or compatible ID (`1`–`3`) | `0` | `0` |
| `HHH` | Position of the matched ID in the device's list; `000` is its first and most specific hardware ID | `000` | `000` |

When ranks tie, PnP takes the newer DriverVer date, then the higher
version. `docs/evidence/kmd/06-setupapi-dev-log.txt` shows the tie before the
fix: `Driver Rank - 00F90000` for this package, signed with
`Signer Score - Authenticode (0F000000)`, and for `viogpudo`, signed with
`Signer Score - WHQL (0D000005)`. The signer score is recorded but does
not reach the rank.

The options for winning, and why the fix uses each of them, are:

| Option | Assessment |
| --- | --- |
| A more specific hardware ID | Not possible. The INF already matches the device's first hardware ID, with `SUBSYS` and `REV`, and `viogpudo` matches the same ID. |
| A later DriverVer date | Not robust. A date beats only drivers dated earlier, so it is a race with every future `viogpudo` release. A far-future date would also show a false date in Device Manager and `dxdiag`. DriverVer stays at the upstream commit date (`build/kmd/inner.sh`). |
| A lower `FeatureScore` | Used. It decides before the date does. `E0` is well below `viogpudo`'s `F9`, but a driver that sets a lower score of its own still wins. |
| Forced install | Used. `UpdateDriverForPlugAndPlayDevices(…, INSTALLFLAG_FORCE)` installs the named INF on the device even when PnP ranks it lower. It holds only until PnP next picks a driver for the device. |
| Device installation policy | Used. It holds afterwards. "Prevent installation of devices that match any of these device instance IDs" stops PnP from installing any driver on the listed device, which covers Windows Update and `pnputil /add-driver /install` alike. |

### The pin

The policy is set in the registry, where the Group Policy setting puts it.
PnP reads it each time it installs a driver, so it takes effect without
`gpupdate` or a reboot:

```
HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions
    DenyInstanceIDs   REG_DWORD  1
HKLM\SOFTWARE\Policies\Microsoft\Windows\DeviceInstall\Restrictions\DenyInstanceIDs
    <n>               REG_SZ     PCI\VEN_1AF4&DEV_1050&SUBSYS_11001AF4&REV_01\<instance>
```

The pin lists one device **instance**, not the hardware ID. A new device
instance, for example after the VM's PCI layout changes, installs normally,
and `FeatureScore` makes this package win there too. A hardware ID entry
would have blocked every driver, Microsoft Basic Display included, on any
such new instance. `DenyInstanceIDsRetroactive` is not set, so the policy
never removes the driver that is installed. The setting is supported from
Windows 10 version 1903.

The trade-off is that nothing else can change the driver of that one device
while the package is installed: not Windows Update, not Device Manager's
"Update driver", and not `pnputil`. Windows Update can still download a
newer `viogpudo`, but its install fails ("prohibited by policy") and can be
retried and reported in the update history. This is acceptable for lab VMs.
It changes nothing for other devices, and `Uninstall.cmd` removes it.

### What the installer does

`installer/vmlab-win3d.ps1`, "driver pinning":

| When | Action |
| --- | --- |
| First install | `baseline.json` records `device_policy`: whether the policy keys existed, the `DenyInstanceIDs` switch, and whether the device was already listed. A baseline from an older package gets `device_policy` added before the first pin. |
| Driver step, nothing to change | Ensures the pin. |
| Driver step, upgrade or new install | Removes the pin first (the policy would block the step's own driver changes too), removes older builds, runs `pnputil /add-driver /install`, and forces the package if PnP bound another driver. Then pins the device and records the ranked driver list (`pnputil /enum-devices /drivers`) in the journal. |
| Verify | Also fails when the device is not pinned. |
| Every boot (`vmlab-win3d-repair`) | If the device uses another driver, for example because a domain policy refresh dropped the pin, it lifts the pin, forces this package back, and pins it again. If the pin is gone, it puts it back. |
| Uninstall | Removes the pin and puts the switch and keys back as the baseline had them. Other entries in the list are left alone. It then deletes the package. If the best remaining driver is not the baseline one (a newer `viogpudo` that arrived during the install, say), it forces the baseline INF back. Verify compares the policy with the baseline. |

## Evidence

`docs/evidence/installer/src/` holds the repro:

| File | Role |
| --- | --- |
| `repro.sh` | Host driver. Brings up a fresh `windows-11` lab on the named GPU and runs every step below. |
| `build.sh` | Builds the inputs into `out/installer-repro/`: two copies of the template's own `viogpudo` package and a control bundle. |
| `Test-DriverPin.ps1` | Guest half. Reports the bound driver, its rank, the policy and the driver store, stages a package, and runs a scan. |
| `check.py` | Judges the reports. Writes `checks.txt`. |
| `VIOGPUDO.sha256` | The template's `viogpudo` files, pinned. |

The simulated update is the template's own Red Hat `viogpudo` (07/09/2025,
100.101.104.28500). `build.sh` changes its DriverVer to `12/31/2026`, later
than this package's 09/27/2026, and re-signs it with this checkout's test
certificate. Because the signer byte of the rank is `00` for both
signatures, PnP sees what it would see for a WHQL `viogpudo` released after
this package. A second copy, "hostile", also has `FeatureScore=00`, so it
outranks even the fixed INF and only the forced install and the pin hold it
off. The control bundle is this checkout's bundle with the installer and
INF of commit `b4b8c3f`, from before the fix.

The steps (`repro.sh`):

1. Baseline: the template's `viogpudo` drives the device.
2. Control: install the control bundle, then stage the newer `viogpudo` with
   `pnputil /add-driver /install`, which is how Windows Update installs a
   driver, and reboot. The device moves to `viogpudo`. This is the bug.
3. Install this bundle over it, as an upgrade.
4. Stage the newer and the hostile `viogpudo` again.
5. `pnputil /scan-devices` and `UsoClient StartScan`, then wait four minutes.
6. Reboot.
7. Uninstall.
8. Reinstall with both copies still in the driver store, then rerun.
9. Uninstall again.

### Results

`checks.txt` in each run directory judges the reports. Each row also gives
the bound driver from the numbered JSON reports.

| Step | Nvidia2 (`windows-11-nvidia2/`) | AMD1 (`windows-11-amd1/`) |
| --- | --- | --- |
| 1 Baseline | `oem7.inf` `viogpudo` 2025-07-09, rank `00F90000` | Same |
| 2 Control installed | `viogpu3d` 2026-09-27, rank `00F90000` | Same |
| 2 Control, after the newer `viogpudo` and a reboot | **`viogpudo` 2026-12-31, rank `00F90000`: the bug** | **Same: the bug** |
| 3 Fix installed (upgrade) | `viogpu3d`, rank `00E00000`, pinned | Same |
| 4 Stage the newer `viogpudo` | pnputil exit 259 (no device needed it): ranked out by `FeatureScore` | Same, exit 259 |
| 4 Stage the hostile `viogpudo` | pnputil exit `0xE0000248`, "The installation of this device is forbidden by system policy": blocked by the pin | Same, exit `0xE0000248` |
| 5 Scans, 4 minutes | `viogpu3d`, no change | Same |
| 6 Reboot | `viogpu3d`, no change | Same |
| 7 Uninstall | The hostile `viogpudo` took the device once the package was deleted. The installer forced `oem7.inf` back. Policy keys gone, as in the baseline | Same (`oem17.inf`, the hostile copy, forced back to `oem7.inf`) |
| 8 Reinstall | `pnputil /add-driver /install` left the device on `oem7.inf`, because the hostile copy outranks the package. The forced install bound `viogpu3d`, rank `00E00000`, pinned | Same |
| 8 Rerun | "already installed", "nothing changed; no reboot needed" | Same |
| 9 Uninstall again | `oem7.inf`, no policy keys, `Removed` | Same |
| Checks | 20 of 20 | 20 of 20 |

The journals show each mechanism at work. On Nvidia2:
`journal-removed.log` has the pin coming off
(`policy: removed DenyInstanceIDs\1`, then the three keys the install
created), then
`device uses oem17.inf 2026-12-31 100.101.104.29901, baseline oem7.inf; restoring the baseline driver`.
`journal-reinstalled.log` has
`PnP chose oem7.inf (Red Hat, Inc. 2025-07-09 100.101.104.28500) over oem15.inf; installing oem15.inf with INSTALLFLAG_FORCE`
and the ranked list: `viogpu3d` `00E00000`, both `viogpudo` copies with a
full-ID match at `00F90000` and `00000000`, and Microsoft Basic Display at
`00FB2006`.

The full acceptance run (`tests/e2e/run.sh --template windows-11`) passes
with the fixed bundle, built at `43a4b1e`: 12 of 12 on Nvidia1
(`e2e/windows-11-nvidia1/`) and 12 of 12 on AMD1 (`e2e/windows-11/`). Both
runs install, verify and destroy their own lab.

The AMD1 run used the fixed checker. The Nvidia2 run's own `check.py` call failed on two checker bugs: a
one-element list that PowerShell's JSON writes as a string, and a
`stage-newer` match that also caught `control-stage-newer`. The checker was
fixed and run again on the same reports; `checks.txt` is that second run.

## Windows Update

The repro above could not run a real Windows Update scan: the template
disables the service, and the lab had no network. With the lab NIC
(`docs/wu.md`), `docs/evidence/wu/src/repro.sh` turns Windows Update on in
a test VM and scans with the Windows Update Agent COM API
(`Microsoft.Update.Session`, against Windows Update itself, driver updates
included). It downloads and installs every driver update offered, runs
UsoClient's scans and reboots. It searches in three device states:

| Device state | Drivers Windows Update offers | Driver bound afterwards |
| --- | --- | --- |
| Template `viogpudo` (2025-07-09) | None | `viogpudo` |
| No driver (Microsoft Basic Display, `viogpudo` deleted) | None | Microsoft Basic Display |
| This package, pinned | None | `viogpu3d`, rank `00E00000`, pinned, after the driver install, UsoClient's scans and a reboot |

Windows Update offers no driver for `PCI\VEN_1AF4&DEV_1050`, and no
driver for any other device in the VM. It offers the 2026-10 cumulative
update, a .NET update, MSRT and a Defender definition update. The Microsoft
Update Catalog does list Red Hat display drivers for the device, but all
are older than the template's `viogpudo` (`docs/wu.md`). `UsoClient` on build
26100 accepts only its scan verbs. `StartDownload` and `StartInstall` exit
`0x80070057`, as a made-up verb does, so the COM API does the install.

Results: `docs/evidence/wu/windows-11-nvidia1/checks.txt` and
`docs/evidence/wu/windows-11-amd1/checks.txt`, 16 of 16 checks each.

## What remains

- **No real newer `viogpudo` exists to be offered.** A real Windows Update
  scan offers no driver for the device ([Windows Update](#windows-update)),
  so the guards have been exercised only by the simulated update above.
  Windows Update installs a driver through the same PnP device install that
  `pnputil /add-driver /install` uses, and the device installation
  restrictions apply to every PnP device install, whatever started it.
- **The simulated update is test-signed, not WHQL-signed.** Only Microsoft
  can sign a modified `viogpudo` with WHQL. The rank lists show that PnP
  gives both signatures the same signer byte.
- A driver that sets an even lower `FeatureScore` still outranks the INF,
  as the hostile copy shows. Against such a driver, only the pin and the
  forced install hold the device.
- A domain Group Policy that manages `DeviceInstall\Restrictions` can
  overwrite the pin at refresh. The repair task restores it at the next
  boot, but not before.
- Windows 10 was not run. `DenyInstanceIDs` needs Windows 10 version 1903 or
  later.
