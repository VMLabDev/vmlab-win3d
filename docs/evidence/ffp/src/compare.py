#!/usr/bin/env python3
"""Compare d3d9_ffp.exe logs against a reference log.

    compare.py REF.txt NAME=LOG.txt [NAME=LOG.txt ...] [--markdown]

For every case in the reference, each log's 64 sampled pixels are compared
channel by channel with the case's tolerance (the tol= field, in 8-bit
steps). A cell is:
  pass             every channel of every sample within tolerance
  FAIL d=<n> k=<m> the largest channel difference and how many of the 64
                   samples are outside tolerance
  HR <hex>         an HRESULT failure while the case ran
  missing          the case is not in the log (the program stopped early)
Prints one row per case and a summary line per log. Exit 0 when every log
passes every case.
"""
import sys


def parse(path):
    cases = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("CASE "):
                continue
            fields = dict(p.split("=", 1) for p in line.split()[2:] if "=" in p)
            name = line.split()[1]
            px = fields.get("px", "")
            vals = [int(v, 16) for v in px.split(",")] if px and px[0] in "0123456789abcdef" else []
            cases[name] = (int(fields.get("tol", "2")), int(fields.get("hr", "0"), 16), vals)
    return cases


def judge(ref, got):
    tol, _, rpx = ref
    if got is None:
        return "missing", False
    _, hr, gpx = got
    if hr:
        return "HR 0x%08x" % hr, False
    if len(gpx) != len(rpx) or not rpx:
        return "no pixels", False
    worst, bad = 0, 0
    for a, b in zip(rpx, gpx):
        d = max(abs(((a >> s) & 255) - ((b >> s) & 255)) for s in (0, 8, 16, 24))
        worst = max(worst, d)
        bad += d > tol
    if bad:
        return "FAIL d=%d k=%d" % (worst, bad), False
    return "pass", True


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    md = "--markdown" in sys.argv
    if len(args) < 2:
        print(__doc__)
        return 2
    ref = parse(args[0])
    logs = []
    for a in args[1:]:
        name, _, path = a.partition("=")
        logs.append((name, parse(path)))
    ok_all = True
    counts = {n: 0 for n, _ in logs}
    rows = []
    for case, r in ref.items():
        cells = []
        for name, cases in logs:
            text, ok = judge(r, cases.get(case))
            if r[1]:
                text += " (ref HR 0x%08x)" % r[1]
            counts[name] += ok
            ok_all &= ok
            cells.append(text)
        rows.append((case, cells))
    names = [n for n, _ in logs]
    if md:
        print("| Case | " + " | ".join(names) + " |")
        print("| --- |" + " --- |" * len(names))
        for case, cells in rows:
            print("| `%s` | %s |" % (case, " | ".join(cells)))
    else:
        w = max(len(c) for c, _ in rows)
        print("%-*s  %s" % (w, "case", "  ".join("%-22s" % n for n in names)))
        for case, cells in rows:
            print("%-*s  %s" % (w, case, "  ".join("%-22s" % c for c in cells)))
    for n in names:
        print("%s: %d of %d pass" % (n, counts[n], len(rows)))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
