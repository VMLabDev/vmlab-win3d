#!/usr/bin/env python3
"""Check that the repository paths, links and commits the documents name exist.

usage: scripts/check-docs.py [-v]

Reads README.md, docs/*.md, docs/decisions/*.md and build/*/README.md and checks:

- every backticked span that starts with a top-level directory of this
  repository (`build/...`, `docs/...`, `patches/mesa/0021` ...) names a path
  that exists. A span may be a prefix of a file name (`patches/mesa/0012`), a
  glob (`*`, `{a,b}`), or contain a `<placeholder>`, which matches anything.
  Result tables name files relative to their evidence directory
  (`probes/x64-d3d9_probe.run.json`), so a path that exists under some
  docs/evidence/ directory also passes. A path in HISTORICAL_PATHS passes if
  git history has it;
- every relative Markdown link [text](target) points at an existing file or
  directory, relative to the document (anchors are not checked);
- every backticked lowercase hex string of 7 to 40 characters that looks like
  a commit is a commit in this repository, or a prefix of an upstream commit
  pinned in a PINS file, or listed in EXTERNAL_COMMITS.

Paths under out/ and .scratch/ are build or run output and are not checked.
Exit 1 and one line per problem when anything is missing.
"""
import argparse
import itertools
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TOP = sorted(p.name for p in REPO.iterdir()
             if p.is_dir() and not p.name.startswith(".") and p.name not in ("out",))
SKIP_PREFIXES = ("out/", ".scratch/")

# Commits of other repositories that the documents cite and no PINS file pins.
EXTERNAL_COMMITS = {
    "baca25172d8c195b9abbe6f8a3ab5915dba09a91": "QEMU: egl-headless teardown fix (known-limits.md, prior-art.md)",
    "c3d48b7d1e89604920e5b81b91140c2ad39a1943": "QEMU: the prior art's patch base (prior-art.md)",
    "1d3e6800b84b7f827654bfdedd95c9d582d07e8b": "yttrium-virtio-gpu: the prior art's newer checkout (prior-art.md)",
    "42abca66ff": "Yttrium Mesa release binaries (prior-art.md)",
    "147274f": "vmlab: the 0.24 changes from handoff 20261017-142905 (host.md)",
    "cdf4ff9": "vmlab: the 0.24 changes from handoff 20261017-142905 (host.md)",
    "2645da6": "vmlab 0.23.0-alpha as probed (host.md)",
    "f1e4515": "vmlab: the template agent's version (host.md)",
    "fb0c538": "vmlab-templates: firstboot.ws waits for user OOBE, handoff 20261018-092229 (known-limits.md)",
    "f5832b34dd09a1be669af55e0c4c81b4a8d52a5f": "VMLabDev/qemu: the single-patch egl-headless fix, before the branch was rewritten (nvidia.md)",
    "78bc99a513c04b8c51bc6c211f5da4eaa21e2592": "VMLabDev/qemu: the series on v11.1.1, source of 0.1.0-alpha, tag vmlab-win3d-0.1.0-alpha (nvidia.md, release evidence)",
    "672843fce505d7aaeb11966269a22d34aefdef6a": "VMLabDev/qemu: the series on v11.1.2 with baca2517 backported, briefly the branch tip (nvidia.md)",
    "a19f59a72d96056d9a8f48cf6c77f5787491e414": "QEMU master: the base of the series on VMLabDev/qemu (nvidia.md, stack.md)",
    "8dae2d85": "dxil-spirv's dxbc-spirv gitlink at the pinned commit (build/d3d12/README.md)",
    "be71ee8c": "dxil-spirv's SPIRV-Cross gitlink (build/d3d12/README.md)",
    "199cb207": "dxil-spirv's SPIRV-Tools gitlink (build/d3d12/README.md)",
    "c63848ec": "dxil-spirv's spirv-headers gitlink (build/d3d12/README.md)",
}

# Paths that documents describe as removed; they must exist in git history.
HISTORICAL_PATHS = {
    "labs/probe/vmlab-amd.sh": "the vmlab 0.23 supervisor workaround, removed in badd5c2 (host.md)",
    "labs/nvidia/vmlab-nv.sh": "the isolated-supervisor NVIDIA wrapper, removed in 08ff884 when vmlab 0.26 added qemu_binary (host.md, nvidia.md)",
    "patches/host/qemu/0001": "the single QEMU patch, replaced by the VMLabDev/qemu branch pin in patches/host/PINS (nvidia.md, zeus.md, acceptance.md)",
}

BACKTICK = re.compile(r"`([^`\n]+)`")
LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
HEX = re.compile(r"^[0-9a-f]{7,40}$")
FENCE = re.compile(r"^\s*(```|~~~)")


def documents():
    yield REPO / "README.md"
    yield from sorted((REPO / "docs").glob("*.md"))
    yield from sorted((REPO / "docs" / "decisions").glob("*.md"))
    yield from sorted((REPO / "build").glob("*/README.md"))


def prose_lines(path):
    """(line number, text) outside fenced code blocks."""
    fenced = False
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            yield n, line


def expand_braces(s):
    m = re.search(r"\{([^{}]*,[^{}]*)\}", s)
    if not m:
        return [s]
    out = []
    for alt in m.group(1).split(","):
        out.extend(expand_braces(s[:m.start()] + alt + s[m.end():]))
    return out


def path_exists(span):
    """True when the span names something in the repository."""
    span = span.rstrip(".,;:)")
    span = re.sub(r":\d+(-\d+)?$", "", span)            # file.c:123
    span = re.sub(r"<[^>]*>", "*", span)                 # <component>
    for candidate in expand_braces(span):
        candidate = candidate.rstrip("/")
        pattern = candidate if any(c in candidate for c in "*?[") else candidate + "*"
        if (REPO / candidate).exists() or any(REPO.glob(pattern)):
            return True
        if any((REPO / "docs" / "evidence").glob("**/" + pattern)):
            return True
        if candidate in HISTORICAL_PATHS:
            return subprocess.run(["git", "-C", str(REPO), "log", "--all", "-1", "--format=%h", "--", f":(glob){candidate}*"],
                                  capture_output=True, text=True).stdout.strip() != ""
    return False


def path_spans(span):
    """The repository-path candidates in one backticked span."""
    out = []
    for word in span.split():
        word = word.strip("\"'(),;")
        if word.startswith("./"):
            word = word[2:]
        if word.startswith(SKIP_PREFIXES):
            continue
        first = word.split("/", 1)[0]
        if "/" in word and first in TOP:
            out.append(word)
        elif word in ("README.md",):
            out.append(word)
    return out


def pinned_commits():
    hashes = set(EXTERNAL_COMMITS)
    for pins in itertools.chain(REPO.glob("build/*/PINS"), REPO.glob("patches/*/PINS"), REPO.glob("tests/*/PINS")):
        hashes.update(re.findall(r"\b[0-9a-f]{40}\b", pins.read_text()))
    return hashes


def is_repo_commit(h):
    return subprocess.run(["git", "-C", str(REPO), "cat-file", "-e", f"{h}^{{commit}}"],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def looks_like_commit(s):
    # A run of decimal digits (a date, a build number) is not a commit.
    return HEX.match(s) and re.search(r"[a-f]", s) and re.search(r"[0-9]", s)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("-v", "--verbose", action="store_true", help="print every checked item")
    a = ap.parse_args()
    pinned = pinned_commits()
    problems, checked = [], 0
    for doc in documents():
        rel = doc.relative_to(REPO)
        for n, line in prose_lines(doc):
            for m in BACKTICK.finditer(line):
                span = m.group(1)
                for p in path_spans(span):
                    checked += 1
                    ok = path_exists(p)
                    if a.verbose:
                        print(f"{'ok  ' if ok else 'MISS'} {rel}:{n}: path {p}")
                    if not ok:
                        problems.append(f"{rel}:{n}: no such path: {p}")
                if looks_like_commit(span):
                    checked += 1
                    ok = is_repo_commit(span) or any(h.startswith(span) for h in pinned)
                    if a.verbose:
                        print(f"{'ok  ' if ok else 'MISS'} {rel}:{n}: commit {span}")
                    if not ok:
                        problems.append(f"{rel}:{n}: not a commit here or a pinned upstream commit: {span}")
            for m in LINK.finditer(line):
                target = m.group(1)
                if re.match(r"^[a-z][a-z0-9+.-]*:", target) or target.startswith("#"):
                    continue
                target = target.split("#", 1)[0]
                checked += 1
                ok = (doc.parent / target).exists()
                if a.verbose:
                    print(f"{'ok  ' if ok else 'MISS'} {rel}:{n}: link {target}")
                if not ok:
                    problems.append(f"{rel}:{n}: broken link: {target}")
    for p in problems:
        print(p)
    print(f"check-docs: {checked} references checked, {len(problems)} missing", file=sys.stderr)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
