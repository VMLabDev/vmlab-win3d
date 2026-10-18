#!/usr/bin/env python3
"""Judge a driver-pinning repro run (repro.sh). usage: check.py EVIDENCE_DIR

Prints one PASS or FAIL line per check and exits 1 when any fails.
"""
import glob
import json
import os
import re
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


def journal(name):
    p = os.path.join(ev, f"journal-{name}.log")
    return open(p, encoding="utf-8-sig", errors="replace").read() if os.path.exists(p) else ""


failed = 0


def check(name, ok, detail=""):
    global failed
    failed += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""))


def bound(label):
    return reports.get(label, {}).get("bound") or {}


def ours(label):
    b = bound(label)
    return b.get("original") == "viogpu3d.inf" and b.get("problem_code") == 0


def pinned(label):
    r = reports.get(label, {})
    p = r.get("policy") or {}
    ids = [x.split("=", 1)[1] for x in (p.get("deny_instance_ids") or [])]
    return (p.get("values") or {}).get("DenyInstanceIDs") == 1 and r.get("instance_id") in ids


def policy(label):
    return json.dumps((reports.get(label) or {}).get("policy"), sort_keys=True)


def desc(label):
    b = bound(label)
    return f"{b.get('inf')} {b.get('original')} {b.get('date')} rank {b.get('rank')} problem {b.get('problem_code')}"


base = bound("baseline")
check("baseline: the template's viogpudo drives the device", base.get("original") == "viogpudo.inf", desc("baseline"))
check("control: the old installer binds viogpu3d", ours("control-installed"), desc("control-installed"))
b = bound("control-after-newer")
check("control: a newer-dated viogpudo then takes the device (the bug)",
      b.get("original") == "viogpudo.inf" and b.get("date") == "2026-12-31", desc("control-after-newer"))
check("fix: the upgrade binds viogpu3d again", ours("fixed-installed") and reports["fixed-installed"]["installer"].startswith("Installed"),
      desc("fixed-installed"))
check("fix: the rank carries FeatureScore E0", (bound("fixed-installed").get("rank") or "").startswith("00E0"), bound("fixed-installed").get("rank"))
check("fix: the device is pinned (DenyInstanceIDs)", pinned("fixed-installed"), policy("fixed-installed"))
inf = bound("fixed-installed").get("inf")
for label in ("after-stage", "after-scan", "after-reboot"):
    check(f"{label}: still viogpu3d ({inf})", ours(label) and bound(label).get("inf") == inf and pinned(label), desc(label))
for name in ("stage-newer", "stage-hostile"):
    p = os.path.join(ev, next(iter(sorted(f for f in os.listdir(ev) if f.endswith(f"-{name}.json"))), ""))
    try:
        s = json.load(open(p, encoding="utf-8-sig"))
        check(f"{name}: pnputil staged it and the device kept viogpu3d",
              s["after"]["original"] == "viogpu3d.inf", f"pnputil exit {s['pnputil']['exit']}, after {s['after']['inf']}")
    except (OSError, ValueError, KeyError) as e:
        check(f"{name}: report readable", False, str(e))
for label in ("removed", "removed-again"):
    b = bound(label)
    check(f"{label}: the baseline driver is back", b.get("inf") == base.get("inf") and b.get("date") == base.get("date")
          and b.get("problem_code") == base.get("problem_code"), desc(label))
    check(f"{label}: the device installation policy matches the baseline", policy(label) == policy("baseline"), policy(label))
    check(f"{label}: the installer says Removed", (reports.get(label) or {}).get("installer", "").startswith("Removed"),
          (reports.get(label) or {}).get("installer"))
check("reinstall: viogpu3d and pinned", ours("reinstalled") and pinned("reinstalled"), desc("reinstalled"))
j = journal("reinstalled")
last = j[j.rfind("Install from"):] if "Install from" in j else ""
reinstall = j[: j.rfind("Install from")]
reinstall = reinstall[reinstall.rfind("Install from"):]
check("reinstall: PnP preferred the hostile viogpudo and the installer forced viogpu3d",
      "INSTALLFLAG_FORCE" in reinstall and "UpdateDriverForPlugAndPlayDevices" in reinstall)
check("rerun: changed nothing", ours("rerun") and "nothing changed; no reboot needed" in last and "already installed" in last)
sys.exit(1 if failed else 0)
