#!/usr/bin/env python3
"""Check that a driver package directory holds exactly the files its INF lists.

Every [SourceDisksFiles] entry must exist in its subdirectory, and every file
in the directory must be listed, apart from the INF itself and its catalog.

usage: check-inf.py PACKAGE/viogpu3d.inf
"""
import os
import re
import sys

inf = sys.argv[1]
root = os.path.dirname(os.path.abspath(inf))
section = None
listed = {os.path.basename(inf).lower()}
problems = []
with open(inf, encoding="ascii") as f:
    for raw in f:
        line = raw.split(";", 1)[0].strip()
        m = re.match(r"^\[([^\]]+)\]$", line)
        if m:
            section = m.group(1).lower()
            continue
        if section == "version":
            m = re.match(r"^CatalogFile\s*=\s*(\S+)", line, re.I)
            if m:
                listed.add(m.group(1).lower())
        if section != "sourcedisksfiles" or "=" not in line:
            continue
        name, rest = (p.strip() for p in line.split("=", 1))
        fields = [p.strip() for p in rest.split(",")]
        sub = fields[1] if len(fields) > 1 else ""
        rel = os.path.join(sub, name) if sub else name
        listed.add(rel.lower())
        if not os.path.isfile(os.path.join(root, rel)):
            problems.append(f"listed but missing: {rel}")
for dirpath, _, files in os.walk(root):
    for n in files:
        rel = os.path.relpath(os.path.join(dirpath, n), root)
        if rel.lower() not in listed:
            problems.append(f"present but not in the INF: {rel}")
if problems:
    sys.exit("\n".join(problems))
print(f"INF lists {len(listed) - 2} files; all present")
