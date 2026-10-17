#!/usr/bin/env python3
"""Write manifest.json for an install bundle, then zip the bundle.

manifest.json records the version, the source commit, the driver version and
hardware ID from the INF, the test certificate's SHA-1 thumbprint, and the
sha256 and size of every other file. vmlab-win3d.ps1 refuses a bundle whose
files do not match it. The zip has fixed timestamps and sorted entries, so
the same bundle gives the same zip.

usage: manifest.py BUNDLE_DIR VERSION COMMIT OUT.zip
"""
import hashlib
import json
import os
import re
import sys
import zipfile

bundle, version, commit, out_zip = sys.argv[1:5]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


inf = open(os.path.join(bundle, "driver", "viogpu3d.inf"), encoding="ascii").read()
driver_ver = re.search(r"^DriverVer\s*=\s*([^,\r\n]+),\s*([0-9.]+)", inf, re.M)
hwids = sorted(set(re.findall(r"PCI\\VEN_[0-9A-F]{4}&DEV_[0-9A-F]{4}[^\s,;]*", inf, re.I)))
if len(hwids) != 1:
    sys.exit(f"expected one hardware ID in the INF, found {hwids}")
with open(os.path.join(bundle, "vmlab-win3d-test.cer"), "rb") as f:
    thumb = hashlib.sha1(f.read()).hexdigest().upper()

files = []
for dirpath, _, names in os.walk(bundle):
    for n in names:
        p = os.path.join(dirpath, n)
        rel = os.path.relpath(p, bundle).replace(os.sep, "/")
        if rel == "manifest.json":
            continue
        files.append({"path": rel, "sha256": sha256(p), "size": os.path.getsize(p)})
files.sort(key=lambda e: e["path"])
manifest = {
    "schema": 1,
    "name": "vmlab-win3d",
    "version": version,
    "commit": commit,
    "driver_date": driver_ver.group(1).strip(),
    "driver_version": driver_ver.group(2),
    "hardware_id": hwids[0],
    "certificate_thumbprint": thumb,
    "files": files,
}
with open(os.path.join(bundle, "manifest.json"), "w", newline="\n") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")

top = os.path.basename(os.path.normpath(bundle))
entries = sorted(e["path"] for e in files) + ["manifest.json"]
with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for rel in sorted(entries):
        info = zipfile.ZipInfo(f"{top}/{rel}", date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.external_attr = 0o644 << 16
        with open(os.path.join(bundle, rel), "rb") as f:
            z.writestr(info, f.read())
print(f"manifest: {len(files)} files, driver {manifest['driver_version']}, {manifest['hardware_id']}")
