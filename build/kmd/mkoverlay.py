#!/usr/bin/env python3
"""Write a clang VFS overlay that makes the given directory trees case-insensitive.

Windows SDK, WDK and driver sources #include headers with whatever case the
author typed. On a case-sensitive filesystem those lookups fail, so clang is
given an overlay that lists every file under each root with
'case-sensitive': false.

usage: mkoverlay.py OUT.yaml ROOT...
"""
import json
import os
import sys


def tree(path):
    entries = []
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if os.path.isdir(full):
            entries.append({"type": "directory", "name": name, "contents": tree(full)})
        elif os.path.isfile(full):
            entries.append({"type": "file", "name": name, "external-contents": full})
    return entries


def main():
    out, roots = sys.argv[1], sys.argv[2:]
    overlay = {
        "version": 0,
        "case-sensitive": "false",
        "overlay-relative": "false",
        "roots": [
            {"type": "directory", "name": os.path.abspath(r), "contents": tree(os.path.abspath(r))}
            for r in roots
        ],
    }
    with open(out, "w") as f:
        json.dump(overlay, f)


if __name__ == "__main__":
    main()
