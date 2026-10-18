# Lab network and the Windows Update driver scan

This page covers two things. First, the lab files now give the VM a NAT
NIC, because the Windows evaluation templates must activate online or they
shut down. Second, with that NIC, a real Windows Update scan and driver
install leaves this package bound to the display device. It also shows
which driver Windows Update offers for the device. Investigated on
2026-10-19 on helios with vmlab 0.28.0-alpha, the
`windows-11@26100.1742.5` and `windows-10@19045.2006.8` templates, and
bundles 0.1.0 and 0.1.1. Evidence is in `docs/evidence/wu/`.

## Result

| Item | Result |
| --- | --- |
| Cause, shutdowns | The templates are Enterprise Evaluation editions. Clones boot in notification mode (`0xC004F009`, grace time expired), and `wlms.exe` shuts them down an hour after each boot. With no NIC they cannot activate |
| Fix, lab | `nic { nat = true }` in `labs/acceptance/vmlab.wcl` and `labs/probe/vmlab.wcl`. `tests/e2e/run.sh` copies the acceptance lab, so its labs get the NIC too |
| Fix, activation | The NIC is not enough: Windows did not activate a clone by itself, and `wlms.exe` shut it down after 60 minutes. `slmgr /ato` activates it in about a minute (`Licensed`, end date 1/16/2027), on `windows-11` and `windows-10`. `tests/e2e/run.sh` runs it after bring-up and keeps `activation.txt` |
| Windows Update offer | None. A real search against Windows Update, drivers included, offers no driver for `PCI\VEN_1AF4&DEV_1050`, and no driver for any other device in the VM. This holds with the template's `viogpudo`, with no driver on the device, and with this package |
| Driver stays bound | Yes. After the search, a download and install of every driver update offered, UsoClient's scans and a reboot, `viogpu3d` still drives the device at rank `00E00000`, pinned |
| Proof | See [Evidence](#evidence) |

## Activation

`docs/known-limits.md` ("Evaluation templates need the lab NIC and
`slmgr /ato`") has the user-facing entry. The evidence:

| File (`docs/evidence/wu/activation/`) | What it shows |
| --- | --- |
| `lab-vmlab.wcl` | The clone: `windows-11`, NAT NIC, no GPU, nothing run in it after `vmlab up` |
| `no-ato-shutdown.txt` | That clone was not activated by Windows. It was still in notification mode 63 minutes after it was reported ready, and then stopped. Its event log has `wlms.exe` starting the shutdown 60 minutes after the last boot: "The license period for this installation of Windows has expired. The operating system is shutting down." |
| `ato.txt` | `slmgr /ato` on the same clone: "Product activated successfully", `License Status: Licensed`. Still licensed after a reboot, and still running 72 minutes after it, with no further `wlms` event |
| `windows-10-ato.txt` | The same before and after on a `windows-10` clone |

## Windows Update

The template turns Windows Update off. It disables `UsoSvc` and sets the
policies `DoNotConnectToWindowsUpdateInternetLocations` and
`AU\NoAutoUpdate`. `docs/evidence/wu/src/Test-WindowsUpdate.ps1 -Action
Enable` undoes that in the test VM only: it removes those policy values,
sets `UsoSvc` and `wuauserv` to manual and starts them. `SearchOrderConfig`
is already 1, so device installation may search Windows Update for drivers.
The package and the lab files leave Windows Update as the template set it.

The scan uses the Windows Update Agent COM API,
`Microsoft.Update.Session`. The searcher is set to Windows Update itself
(`ServerSelection = 2`), with `Online = true`, and the criteria
`IsInstalled=0 and IsHidden=0`, which include driver updates
(`Type = 2`). The install step downloads and installs every driver update
the search returns, the way Windows Update does. `UsoClient` on build 26100
accepts only the scan verbs, `StartScan` and `StartInteractiveScan`.
`StartDownload`, `StartInstall` and `ScanInstallWait` exit `0x80070057`,
the same as a made-up verb, so the COM API is the install path.

What Windows Update offers this VM (`checks.txt` of each run):

| Device state | Updates offered | Drivers offered |
| --- | --- | --- |
| Template `viogpudo` (2025-07-09, 100.101.104.28500) | MSRT KB890830, a Defender definition update KB2267602, the 2026-10 cumulative update KB5127648 (26100.9711), the 2026-10 .NET Framework update KB5127690 | None |
| No driver (Microsoft Basic Display, `viogpudo` deleted) | RESULT-BASIC | RESULT-BASIC-DRIVERS |
| This package (`viogpu3d`, 2026-09-27, rank `00E00000`, pinned) | The same four | None |

Windows Update offers a driver only when it outranks the installed one.
The Microsoft Update Catalog lists 25 Red Hat "Display" driver entries for
`VEN_1AF4&DEV_1050` (`catalog-VEN_1AF4-DEV_1050.txt`). All but one are
version 100.6.101.58000 and one is 18.14.55.988, last updated between
August 2023 and December 2024, filed under "Servicing Drivers". None is
newer than the template's `viogpudo`, and the search with no driver on the
device shows whether Windows Update offers any of them to a general device.

So on 2026-10-19 the case that `docs/installer.md` guards against, a newer
`viogpudo` arriving through Windows Update, does not arise. The guards were
proven against a simulated update with `pnputil` (`docs/installer.md`,
"Evidence"). Windows Update installs a driver through the same PnP device
install, and the device installation policy applies to every such install.

## Evidence

`docs/evidence/wu/src/` holds the repro:

| File | Role |
| --- | --- |
| `repro.sh` | Host driver. Brings up a fresh lab from `labs/acceptance/vmlab.wcl` on the named GPU and runs every step below |
| `Test-WindowsUpdate.ps1` | Guest half. Reports the bound driver, the pin and the Windows Update state; turns Windows Update on; searches, downloads and installs through the COM API; runs UsoClient |
| `check.py` | Judges the reports. Writes `checks.txt` |

The steps (`repro.sh`):

1. Activation: `slmgr /dlv`, `slmgr /ato`, `slmgr /dlv`, `slmgr /xpr`.
2. Baseline: the template's `viogpudo` drives the device.
3. Turn Windows Update on in this VM.
4. Search on the template's driver.
5. Delete the template's `viogpudo` (`pnputil /delete-driver /uninstall`).
   The device falls back to Microsoft Basic Display. Search again.
6. Install the bundle with `scripts/install-into-vm.sh`.
7. Search, download and install every driver update offered.
8. UsoClient `StartScan` and `StartInteractiveScan`, five minutes.
9. Reboot, then check activation again.

### Results

RESULTS-TABLE

The full acceptance run, `tests/e2e/run.sh --template windows-11`, with
the NIC and the activation step, passes 12 of 12 on Nvidia2
(`docs/evidence/wu/e2e/windows-11-nvidia2/`, bundle 0.1.0 at `54355df`)
and AMD1-E2E. Each run's `activation.txt` shows `License Status: Licensed`.

## What remains

- **No real newer `viogpudo` exists to be offered.** Windows Update offered
  no driver for the device in any state, so the installer's guards were
  not exercised by a real offer, only by the `pnputil` repro in
  `docs/installer.md`.
- Each Windows Update search took 15 to 48 minutes on helios's degraded
  internet link on 2026-10-19. The first search in a clone syncs the whole
  catalogue at background priority.
- Activation needs `slmgr /ato` once per clone. Labs brought up by hand
  need it too (`docs/known-limits.md`). The evaluation ends 90 days after
  activation, and the rearm count is 1.
- The NIC gives the guest internet egress, and lets guests in one lab
  reach each other.
