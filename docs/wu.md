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
| No driver (Microsoft Basic Display, `viogpudo` deleted) | The same four | None |
| This package (`viogpu3d`, 2026-09-27, rank `00E00000`, pinned) | The same four | None |

Windows Update offers a driver only when it outranks the installed one.
The Microsoft Update Catalog lists 25 Red Hat "Display" driver entries for
`VEN_1AF4&DEV_1050` (`catalog-VEN_1AF4-DEV_1050.txt`). All but one are
version 100.6.101.58000 and one is 18.14.55.988, last updated between
August 2023 and December 2024, filed under "Servicing Drivers". None is
newer than the template's `viogpudo`, and the search with no driver on the
device shows that Windows Update does not offer any of them to this VM.
The entries are most likely targeted at particular machines.

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

`checks.txt` in each run directory judges the reports.

| Step | Nvidia1 (`windows-11-nvidia1/`) | AMD1 (`windows-11-amd1/`) |
| --- | --- | --- |
| 1 Activation | Notification, then `Licensed`, ends 1/16/2027 | Same, ends 1/16/2027 |
| 2 Baseline | `oem7.inf` `viogpudo` 2025-07-09, rank `00F90000` | Same |
| 4 Search, template driver | 4 updates, no driver; 953 s | **No result**: the search was still syncing when `vmlab exec` hit its 3600 s limit. The guest's log has transient `0x8024401C` timeouts against `fe2cr.update.microsoft.com` throughout; an earlier attempt ended `0x80240438` (endpoint unreachable) |
| 5 Search, no driver | `display.inf`, rank `00FB2006`; 4 updates, no driver; 13 s | Same; the search picked up the interrupted sync: 4 updates, no driver, 423 s |
| 6 Install the bundle (0.1.1) | `viogpu3d` 2026-09-27, rank `00E00000`, pinned | Same |
| 7 Search, download, install drivers | 4 updates, 0 drivers, nothing to install; `viogpu3d`, pinned | Same |
| 8 UsoClient | `StartScan`, `StartInteractiveScan` exit 0; `StartDownload`, `StartInstall` and the made-up verb `0x80070057`; Windows Update logged "found 2 updates"; `viogpu3d`, pinned | Same; "found 2 updates" twice |
| 9 Reboot | `viogpu3d`, pinned; still `Licensed` | Same |
| Checks | 16 of 16 | 15 of 16: step 4, for the network reason above |

An earlier Nvidia1 run, before step 5 existed and while the UsoClient check
still expected the download and install verbs to work, had the same driver
results with bundle 0.1.0 (git history of `windows-11-nvidia1/`). The
first AMD1 run was stopped after the same network failure at step 4; the
search then had no retry. `repro.sh` now allows each first search two hours,
and `Test-WindowsUpdate.ps1` retries a search that ends in a transport error.
The AMD1 step 4 was not run again: the step does not depend on the GPU, it
passed on Nvidia1, and the step-5 search in the same AMD1 guest returned the
same offer.

The full acceptance run, `tests/e2e/run.sh --template windows-11`, with
the NIC and the activation step, passes 12 of 12 on Nvidia2
(`docs/evidence/wu/e2e/windows-11-nvidia2/`, bundle 0.1.0 at `54355df`)
and on AMD1 (`docs/evidence/wu/e2e/windows-11-amd1/`, bundle 0.1.1 built
at `1c73461`). Each run's `activation.txt` shows `License Status: Licensed`.
The first AMD1 run passed 11 of 12
(`docs/evidence/wu/e2e/windows-11-amd1-run1/`). Its x86 D3D12 probe passed
its own readback but presented one frame in 40 seconds, so its window never
reached a console capture. The x64 D3D12 probe in the same run ran at 49
fps, the host's load average was about 28, and worker `dd16` saw the same
11 of 12 on AMD1 without this change. The rerun presented 569 frames at 28
fps.

## What remains

- **No real newer `viogpudo` exists to be offered.** Windows Update offered
  no driver for the device in any state, so the installer's guards were
  not exercised by a real offer, only by the `pnputil` repro in
  `docs/installer.md`.
- **AMD1's search on the template's driver has no result** (above). Each
  first Windows Update search took 15 to 60 minutes or more on helios's degraded
  internet link on 2026-10-19. The first search in a clone syncs the whole
  catalogue at background priority.
- Activation needs `slmgr /ato` once per clone. Labs brought up by hand
  need it too (`docs/known-limits.md`). The evaluation ends 90 days after
  activation, and the rearm count is 1.
- The NIC gives the guest internet egress, and lets guests in one lab
  reach each other.
- On AMD1 the x86 D3D12 probe once stalled for 40 seconds after its first
  present (above). The cause was not investigated here; it is not related
  to the lab network.
