#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Helpers for scripts/release.sh.

usage: assemble.py fill TEMPLATE OUT [--crlf] KEY=VALUE...
           Replace @KEY@ in TEMPLATE; fail if any @KEY@ is left.
       assemble.py zip SRC_DIR OUT.zip EPOCH
           Zip the contents of SRC_DIR with sorted entries and every
           timestamp set to EPOCH, so the same files give the same zip.
       assemble.py notes OUT.md VERSION COMMIT EPOCH CERT_INFO BUILDINFO ASSET...
           Write RELEASE-NOTES.md.
"""
import hashlib
import os
import posixpath
import re
import subprocess
import sys
import time
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REPO_URL = "https://github.com/VMLabDev/vmlab-win3d"


def fill(template, out, *args):
    crlf = "--crlf" in args
    text = Path(template).read_text()
    for kv in (a for a in args if a != "--crlf"):
        key, _, value = kv.partition("=")
        text = text.replace(f"@{key}@", value)
    left = sorted(set(re.findall(r"@[A-Z0-9_]+@", text)))
    if left:
        sys.exit(f"{template}: unfilled {', '.join(left)}")
    if crlf:
        text = text.replace("\r\n", "\n").replace("\n", "\r\n")
    Path(out).write_bytes(text.encode())


def make_zip(src, out, epoch):
    stamp = time.gmtime(int(epoch))[:6]
    src = Path(src)
    paths = sorted(p for p in src.rglob("*") if p.is_file())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p in paths:
            info = zipfile.ZipInfo(p.relative_to(src).as_posix(), stamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, p.read_bytes(), compresslevel=9)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def relink(text, base):
    """Point relative markdown links (relative to docs/ at `base`) at the repo at the commit."""
    def sub(m):
        target = m.group(2)
        if re.match(r"^(https?:|mailto:|#)", target):
            return m.group(0)
        path, _, frag = target.partition("#")
        url = posixpath.normpath(posixpath.join("docs", path)) if path else "docs/known-limits.md"
        return f"{m.group(1)}({base}/{url}{'#' + frag if frag else ''})"
    return re.sub(r"(\[[^\]]*\])\(([^)\s]+)\)", sub, text)


def section(md, heading):
    """The body of the markdown section titled `heading`, up to the next heading of any level."""
    lines = md.splitlines()
    start = next(i for i, l in enumerate(lines) if l.strip() == heading) + 1
    end = next((i for i in range(start, len(lines)) if lines[i].startswith("#")), len(lines))
    return "\n".join(lines[start:end]).strip()


def notes(out, version, commit, epoch, cert_info, buildinfo, *assets):
    base = f"{REPO_URL}/blob/{commit}"
    cert = dict(
        (k.strip(), v.strip())
        for k, _, v in (l.partition(":") for l in Path(cert_info).read_text().splitlines()) if v
    )
    limits = (REPO / "docs/known-limits.md").read_text()
    host_gpu = section(limits, "### Host GPU support")
    matrix = "\n".join(l for l in host_gpu.splitlines() if l.startswith("|"))
    headings = []
    part = None
    for l in limits.splitlines():
        if l.startswith("## "):
            part = l[3:].strip()
        elif l.startswith("### ") and l[4:].strip() != "Host GPU support":
            title = l[4:].strip()
            anchor = re.sub(r"[^a-z0-9 -]", "", title.lower()).replace(" ", "-")
            headings.append(f"- {part}: [{title}]({base}/docs/known-limits.md#{anchor})")
    pins = subprocess.run([sys.executable, str(REPO / "scripts/pins-table.py")],
                          check=True, capture_output=True, text=True).stdout.strip()
    date = time.strftime("%Y-%m-%d", time.gmtime(int(epoch)))
    rows = []
    for a in assets:
        p = Path(a)
        what = "Guest driver package for Windows 10 and 11 x64" if p.suffix == ".zip" else \
            "Patched virglrenderer render server for NVIDIA hosts (x86_64 Linux)" \
            if p.name.startswith("virglrenderer-") else "Patched QEMU for NVIDIA hosts (x86_64 Linux)"
        rows.append(f"| `{p.name}` | {what} | {p.stat().st_size} | `{sha256(p)}` |")

    text = f"""# vmlab-win3d {version}

Alpha, for test VMs only. Built from [`{commit[:12]}`]({REPO_URL}/commit/{commit})
(commit date {date}) by `scripts/release.sh {version}`.

## Assets

| File | What | Bytes | sha256 |
| --- | --- | --- | --- |
{chr(10).join(rows)}

`SHA256SUMS` lists the same hashes. [docs/release.md]({base}/docs/release.md)
explains how to install each asset.

- **Driver package.** Unzip in the guest, then run `Install.cmd` as
  Administrator from the `vmlab-win3d-*` folder; the zip's `README.txt` has
  the steps. Needs Secure Boot off and a Venus GPU (vmlab
  `gpu {{ mode = "vulkan" }}`).
- **QEMU.** Only NVIDIA host GPUs need it. Extract the tarball and run
  `./install.sh`, then put the `qemu_binary` line it prints into the GPU's
  block in the vmlab host config (vmlab 0.26.0-alpha or later).
- **Render server.** Only NVIDIA host GPUs need it: it removes a 10 ms
  floor from every Venus fence there, which held windowed D3D9 to about
  40 frames per second. Extract the tarball and run `./install.sh`, then
  put the `qemu_env` line it prints into the same GPU block.

## Test-signing certificate

The kernel driver is test-signed. `Install.cmd` turns on Windows test
signing and trusts this certificate machine-wide (Trusted Root and Trusted
Publisher), so install the package only in throwaway test VMs.

| Field | Value |
| --- | --- |
| Subject | `{cert['subject']}` |
| SHA-1 thumbprint | `{cert['sha1 thumbprint']}` |
| SHA-256 | `{cert['sha256']}` |
| Valid | {cert['notBefore']} to {cert['notAfter']} |

## Host GPUs

From [docs/known-limits.md]({base}/docs/known-limits.md#host-gpu-support):

{relink(matrix, base)}

## Known limits in brief

Each item is a section of [docs/known-limits.md]({base}/docs/known-limits.md).

{chr(10).join(headings)}

## Upstream pins

Every upstream input, from the `PINS` files (`scripts/pins-table.py`), and
every patch this repository applies.

{relink(pins, base)}

## QEMU build

```
{Path(buildinfo).read_text().strip()}
```
"""
    virgl = REPO / "out/host/virglrenderer/BUILDINFO"
    if any(Path(a).name.startswith("virglrenderer-") for a in assets):
        text += f"""
## Render server build

```
{virgl.read_text().strip()}
```
"""
    Path(out).write_text(text)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd, args = sys.argv[1], sys.argv[2:]
    {"fill": fill, "zip": make_zip, "notes": notes}[cmd](*args)


if __name__ == "__main__":
    main()
