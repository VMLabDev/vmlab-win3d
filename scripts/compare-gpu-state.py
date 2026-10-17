#!/usr/bin/env python3
"""Compare two Get-GpuState.ps1 reports and list every difference.

Used to check that uninstall returns a guest to its baseline: display device
driver and registry, Vulkan registry, system graphics DLL hashes and boot
policy. The collection time and the video mode are ignored.

usage: compare-gpu-state.py BEFORE.json AFTER.json   (exit 1 when they differ)
"""
import json
import sys

IGNORE = {"collected", "current_mode"}


def load(p):
    with open(p, encoding="utf-8-sig") as f:
        return json.load(f)


def walk(a, b, path, out):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in sorted(set(a) | set(b)):
            if k in IGNORE:
                continue
            walk(a.get(k), b.get(k), f"{path}.{k}" if path else k, out)
    elif isinstance(a, list) and isinstance(b, list) and len(a) == len(b):
        for i, (x, y) in enumerate(zip(a, b)):
            walk(x, y, f"{path}[{i}]", out)
    elif a != b:
        out.append(f"{path}: {json.dumps(a)} -> {json.dumps(b)}")


before, after = load(sys.argv[1]), load(sys.argv[2])
diffs = []
walk(before, after, "", diffs)
for d in diffs:
    print(d)
print("identical to the baseline" if not diffs else f"{len(diffs)} difference(s)")
sys.exit(1 if diffs else 0)
