#!/usr/bin/env python3
"""Build the case table of docs/ffp.md from run-repro.sh output directories.

    make-table.py EVIDENCE-DIR

EVIDENCE-DIR is docs/evidence/ffp; it holds {before,after}/{nvidia2,amd1}/.
For every case, prints whether the x64 hardware run with hardware vertex
processing (HWVP) and with software vertex processing (SWVP) matched the
reference rasterizer, per GPU, before and after the fix, and whether the
reference itself gives the same result with the runtime's software vertex
processing as with its own (a "differs" there means the case depends on
which Microsoft vertex pipeline runs, not on the driver).
Then prints the totals per run, x64 and x86.
"""
import os
import sys

sys.dont_write_bytecode = True

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from compare import judge, parse  # noqa: E402

GPUS = ["nvidia2", "amd1"]
STATES = ["before", "after"]


def load(ev, state, gpu, run):
    p = os.path.join(ev, state, gpu, "d3d9_ffp-%s.txt" % run)
    return parse(p) if os.path.exists(p) else {}


def cell(ref, got, case):
    if case not in ref:
        return "?"
    text, ok = judge(ref[case], got.get(case))
    return "pass" if ok else text.replace("FAIL ", "fail ")


def main():
    ev = sys.argv[1]
    data = {}
    for s in STATES:
        for g in GPUS:
            data[s, g] = {r: load(ev, s, g, r) for r in
                          ("x64-hw", "x86-hw", "x64-swvp", "x64-ref", "x86-ref", "x64-refswvp")}
    ref = data["after", "nvidia2"]["x64-ref"] or data["before", "nvidia2"]["x64-ref"]
    refsw = data["after", "nvidia2"]["x64-refswvp"] or data["before", "nvidia2"]["x64-refswvp"]
    head = ["Case"]
    for g in GPUS:
        for s in STATES:
            head += ["%s %s HWVP" % (g, s), "%s %s SWVP" % (g, s)]
    head += ["REF: SWVP = HWVP"]
    print("| " + " | ".join(head) + " |")
    print("|" + " --- |" * len(head))
    for case in ref:
        row = ["`%s`" % case]
        for g in GPUS:
            for s in STATES:
                d = data[s, g]
                row.append(cell(d["x64-ref"], d["x64-hw"], case))
                row.append(cell(d["x64-refswvp"], d["x64-swvp"], case))
        row.append("yes" if judge(ref[case], refsw.get(case))[1] else "differs")
        print("| " + " | ".join(row) + " |")
    print()
    print("| Run | " + " | ".join("%s %s" % (g, s) for g in GPUS for s in STATES) + " |")
    print("|" + " --- |" * (1 + len(GPUS) * len(STATES)))
    for run, refrun in (("x64-hw", "x64-ref"), ("x86-hw", "x86-ref"), ("x64-swvp", "x64-refswvp")):
        cells = []
        for g in GPUS:
            for s in STATES:
                d = data[s, g]
                r, h = d[refrun], d[run]
                n = sum(judge(r[c], h.get(c))[1] for c in r)
                cells.append("%d of %d" % (n, len(r)) if r else "-")
        print("| %s against %s | %s |" % (run, refrun, " | ".join(cells)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
