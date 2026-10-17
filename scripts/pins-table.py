#!/usr/bin/env python3
"""Generate the pin and patch tables in docs/stack.md from build/*/PINS and patches/.

usage: scripts/pins-table.py           print both tables
       scripts/pins-table.py --write   rewrite the generated blocks in docs/stack.md
       scripts/pins-table.py --check   exit 1 if docs/stack.md is out of date

Each build/<component>/PINS has its own line format (see the header of each
file). The parsers below read every format in the repository and fail on a
line they do not understand, so a new PINS format cannot be skipped silently.
"""
import argparse
import difflib
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
STACK = REPO / "docs" / "stack.md"
HEX40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")

# Patches whose file has no "Subject:" header.
SUBJECT_FALLBACK = {
    "kmd/0001-kmd-r10-copy.patch":
        "Convert R10G10B10A2 sources to 8-bit RGBA or BGRA in the KMD's CPU present blit (the prior art's r10 KMD)",
}


def split_comment(line):
    code, _, comment = line.partition("#")
    return code.split(), comment.strip()


def row(component, name, version="", commit="", sha="", url=""):
    return {"component": component, "name": name, "version": version,
            "commit": commit, "sha256": sha, "url": url}


def version_or_commit(value):
    """A version field may hold a version, a commit, or tag@commit."""
    if "@" in value:
        tag, _, commit = value.partition("@")
        return tag, commit
    if HEX40.match(value):
        return "", value
    return value, ""


def parse_kind_lines(component, lines):
    """name kind url ref sha256 [# comment]  (build/kmd, build/signing)."""
    out = []
    for f, comment in lines:
        name, kind, url, ref, sha = f
        sha = "" if sha == "-" else sha
        if kind == "git":
            m = re.search(r"\btag (\S+)", comment)
            out.append(row(component, name, m.group(1) if m else "", ref, sha, url))
        elif kind == "file":
            out.append(row(component, name, ref, "", sha, url))
        elif kind == "image":
            m = re.match(r"(\S+:\S+)", comment)
            out.append(row(component, name, m.group(1) if m else "", ref, sha, url))
        else:
            raise ValueError(f"{component}: unknown kind {kind!r}")
    return out


def parse_d3d12(component, lines):
    """name url ref commit."""
    return [row(component, name, ref, commit, "", url) for (name, url, ref, commit), _ in lines]


def parse_mesa(component, lines, comments):
    """src name version-or-commit sha256 url."""
    out = []
    for f, _ in lines:
        if f[0] != "src":
            continue
        _, name, ver, sha, url = f
        version, commit = version_or_commit(ver)
        if not commit:
            # "# DirectX-Headers v1.619.1 is commit 9e39..." in the file's comments.
            m = re.search(rf"\b{re.escape(name)} {re.escape(ver)} is commit ([0-9a-f]{{40}})", comments)
            commit = m.group(1) if m else ""
        out.append(row(component, name, version, commit, sha, url))
    return out


def parse_vulkan_sdk(component, lines):
    """src name tag commit sha256 url."""
    return [row(component, name, tag, commit, sha, url)
            for (kind, name, tag, commit, sha, url), _ in lines if kind == "src"]


def parse_plain(component, lines):
    """name version sha256 url  (build/probes, build/toolchain); toolchain also has apt lines."""
    out = []
    for f, _ in lines:
        if f[0] == "apt":
            _, name, version = f
            out.append(row(component, f"{name} (apt)", version))
            continue
        name, ver, sha, url = f
        version, commit = version_or_commit(ver)
        sha = "" if sha == "-" else sha
        if sha.startswith("sha256:"):  # an image digest, not a file hash
            commit, sha = sha, ""
        out.append(row(component, name, version, commit, sha, url))
    return out


SKIP_FIRST = {"epoch", "toolchain"}  # SOURCE_DATE_EPOCH and the toolchain pointer


def read_pins(component):
    path = REPO / "build" / component / "PINS"
    lines, comments = [], []
    for raw in path.read_text().splitlines():
        if raw.lstrip().startswith("#"):
            comments.append(raw.lstrip("# "))
            continue
        fields, comment = split_comment(raw)
        if not fields or fields[0] in SKIP_FIRST:
            continue
        lines.append((fields, comment))
    comments = " ".join(comments)
    try:
        if component in ("kmd", "signing"):
            rows = parse_kind_lines(component, lines)
        elif component == "d3d12":
            rows = parse_d3d12(component, lines)
        elif component == "mesa":
            rows = parse_mesa(component, lines, comments)
        elif component == "vulkan-sdk":
            rows = parse_vulkan_sdk(component, lines)
        elif component in ("probes", "toolchain"):
            rows = parse_plain(component, lines)
        else:
            raise ValueError("no parser for this PINS file")
    except ValueError as e:
        sys.exit(f"{path.relative_to(REPO)}: {e}")
    for r in rows:
        for key in ("commit", "sha256"):
            v = r[key]
            if v and not (HEX40.match(v) or SHA256.match(v) or v.startswith("sha256:")):
                sys.exit(f"{path.relative_to(REPO)}: {r['name']}: {key} {v!r} is not a hash")
    return rows


def code(v):
    return f"`{v}`" if v else "—"


def pins_table():
    components = sorted(p.parent.name for p in (REPO / "build").glob("*/PINS"))
    out = ["| Build | Input | Version or tag | Commit or digest | sha256 of the download | Source |",
           "| --- | --- | --- | --- | --- | --- |"]
    for c in components:
        for r in read_pins(c):
            out.append(f"| [`{c}`](../build/{c}/PINS) | {r['name']} | {code(r['version'])} | "
                       f"{code(r['commit'])} | {code(r['sha256'])} | {r['url'] or '—'} |")
    return out


def subject(path):
    text = path.read_text(errors="replace").splitlines()
    for i, line in enumerate(text):
        if line.startswith("Subject:"):
            s = line[len("Subject:"):]
            for cont in text[i + 1:]:
                if not cont.startswith(" "):
                    break
                s += cont
            return re.sub(r"^\s*\[PATCH[^\]]*\]\s*", "", s).strip()
        if line.startswith("diff --git"):
            break
    key = f"{path.parent.name}/{path.name}"
    if key not in SUBJECT_FALLBACK:
        sys.exit(f"{path.relative_to(REPO)}: no Subject: header and no fallback in pins-table.py")
    return SUBJECT_FALLBACK[key]


def patches_table():
    out = ["| Upstream | Patches | Patch | What it does |", "| --- | --- | --- | --- |"]
    for d in sorted(p for p in (REPO / "patches").iterdir() if p.is_dir()):
        patches = sorted(d.glob("*.patch"))
        if not patches:
            out.append(f"| {d.name} | 0 | — | None; see [`patches/{d.name}/`](../patches/{d.name}/) |")
            continue
        for i, p in enumerate(patches):
            first = f"| {d.name} | {len(patches)} |" if i == 0 else "| | |"
            desc = subject(p).replace("|", "\\|")
            out.append(f"{first} [`{p.name[:4]}`](../patches/{d.name}/{p.name}) | {desc} |")
    return out


BLOCKS = {"pins": pins_table, "patches": patches_table}


def render(text):
    for name, fn in BLOCKS.items():
        begin = f"<!-- BEGIN generated {name} table: scripts/pins-table.py --write -->"
        end = f"<!-- END generated {name} table -->"
        pattern = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.S)
        if not pattern.search(text):
            sys.exit(f"{STACK.relative_to(REPO)}: missing marker {begin!r}")
        block = "\n".join([begin, *fn(), end])
        text = pattern.sub(lambda _: block, text)
    return text


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--write", action="store_true", help="rewrite docs/stack.md")
    g.add_argument("--check", action="store_true", help="fail if docs/stack.md is out of date")
    a = ap.parse_args()
    if not (a.write or a.check):
        for fn in BLOCKS.values():
            print("\n".join(fn()) + "\n")
        return
    old = STACK.read_text()
    new = render(old)
    if a.write:
        if new != old:
            STACK.write_text(new)
        return
    if new != old:
        sys.stdout.writelines(difflib.unified_diff(
            old.splitlines(True), new.splitlines(True), "docs/stack.md", "generated"))
        sys.exit("docs/stack.md is out of date; run scripts/pins-table.py --write")


if __name__ == "__main__":
    main()
