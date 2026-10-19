#!/usr/bin/env python3
"""Judge a lab-network and Windows Update run (repro.sh). usage: check.py EVIDENCE_DIR

Prints one PASS or FAIL line per check, then what Windows Update offered,
and exits 1 when any check fails.
"""
import glob
import json
import os
import sys

ev = sys.argv[1]
reports = {}
for f in sorted(glob.glob(os.path.join(ev, "[0-9][0-9]-*.json"))):
    try:
        r = json.load(open(f, encoding="utf-8-sig"))
    except ValueError:
        continue
    if "label" in r:
        reports[r["label"]] = r


def text(name):
    p = glob.glob(os.path.join(ev, f"[0-9][0-9]-{name}.txt"))
    return open(p[0], encoding="utf-8-sig", errors="replace").read() if p else ""


failed = 0


def check(name, ok, detail=""):
    global failed
    failed += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""))


def as_list(x):
    if x is None:
        return []
    return [x] if isinstance(x, (str, dict)) else list(x)  # ConvertTo-Json writes a one-element list as its element


def bound(label, key="bound"):
    return (reports.get(label) or {}).get(key) or {}


def desc(b):
    return f"{b.get('inf')} {b.get('original')} {b.get('date')} rank {b.get('rank')} problem {b.get('problem_code')}" if b else "no report"


def ours(b):
    return b.get("original") == "viogpu3d.inf" and b.get("problem_code") == 0


def pinned(label):
    r = reports.get(label) or {}
    p = r.get("policy") or {}
    ids = [x.split("=", 1)[1] for x in as_list(p.get("deny_instance_ids"))]
    return (p.get("restrictions") or {}).get("DenyInstanceIDs") == 1 and r.get("instance_id") in ids


def searched(label):
    r = reports.get(label) or {}
    return r.get("result_code") == 2, f"result code {r.get('result_code')}, {len(as_list(r.get('updates')))} updates in {r.get('seconds')} s"


act = text("activation")
after_ato = act.split("## slmgr /ato", 1)[-1]
check("1 the clone has a NAT address", "IPv4" in act and "Default Gateway" in act)
check("1 slmgr /ato activates", "Product activated successfully" in after_ato and "License Status: Licensed" in after_ato)
check("1 the evaluation has an end date", "Timebased activation will expire" in after_ato)
check("1 still licensed at the end", "License Status: Licensed" in text("activation-final"))

b = bound("baseline")
check("2 baseline: the template's viogpudo", b.get("original") == "viogpudo.inf", desc(b))
e = reports.get("enable") or {}
svc = {s["name"]: s for s in as_list((e.get("after") or {}).get("services"))}
check("3 Windows Update on: wuauserv and UsoSvc running, WU policies gone",
      all(svc.get(n, {}).get("status") == "Running" for n in ("wuauserv", "UsoSvc"))
      and not (e.get("after") or {}).get("policy") and not (e.get("after") or {}).get("policy_au"))
ok, d = searched("search-template")
check("4 Windows Update search succeeded on the template's driver", ok, d)

b = bound("basic-display")
check("4b viogpudo removed: Microsoft Basic Display drives the device", b.get("inf") == "display.inf", desc(b))
ok, d = searched("search-basic-display")
check("4b Windows Update search succeeded with no driver on the device", ok, d)

for label in ("installed", "after-wu-install", "after-uso", "after-reboot"):
    b = bound(label)
    check(f"{label}: this package drives the device and it is pinned", ours(b) and pinned(label), desc(b))

w = reports.get("wu-install") or {}
check("6 Windows Update search succeeded with the package installed", w.get("result_code") == 2,
      f"result code {w.get('result_code')}, {w.get('driver_updates')} driver updates")
check("6 the package survives the driver download and install", ours(bound("wu-install", "bound_after")),
      desc(bound("wu-install", "bound_after")))
u = reports.get("uso") or {}
runs = {r.get("command"): r.get("exit") for r in as_list(u.get("runs"))}
check("7 UsoClient scans ran", runs.get("UsoClient StartScan") == "0x00000000" and runs.get("UsoClient StartInteractiveScan") == "0x00000000",
      ", ".join(f"{r.get('command')} -> {r.get('exit')}" for r in as_list(u.get("runs"))))

print()
for label in ("search-template", "search-basic-display", "wu-install"):
    r = reports.get(label) or {}
    ups = as_list(r.get("updates"))
    print(f"{label}: {len(ups)} updates offered, {sum(x.get('type') == 2 for x in ups)} drivers, "
          f"{sum(bool(x.get('for_this_device')) for x in ups)} for the display device (PCI\\VEN_1AF4&DEV_1050)")
    for x in ups:
        drv = f" [{x.get('driver_hardware_id')}, {x.get('driver_provider')}, {x.get('driver_ver_date')}]" if x.get("type") == 2 else ""
        print(f"  type {x.get('type')}: {x.get('title')}{drv}")
    for x in as_list((r.get("install") or {}).get("updates")):
        print(f"  installed {x.get('title')}: result {x.get('result_code')} {x.get('hresult')}")
for x in as_list(u.get("events")):
    print(f"uso event: {x}")

print(f"\n{'ALL PASS' if not failed else f'{failed} FAILED'}")
sys.exit(1 if failed else 0)
