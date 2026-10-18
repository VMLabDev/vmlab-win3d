#!/usr/bin/env python3
"""Build the tables of docs/ffp2.md from run-repro.sh output directories.

    make-table.py EVIDENCE-DIR

EVIDENCE-DIR is docs/evidence/ffp2; it holds <state>/<config>/ with state
before or after and config w11-nvidia2, w11-amd1 or w10-nvidia2. Prints:

1. for every d3d9_ffp2 case, whether the x64 hardware run matched the
   reference, per config and state, with hardware vertex processing (HW),
   software (SW) and a mixed device in either mode (MHW, MSW), and whether
   the reference itself gives the same result with the runtime's software
   vertex processing as with its own;
2. the totals of every comparison, for d3d9_ffp2 and d3d9_ffp.

A comparison uses the reference run of the same directory, else the one of
the first directory of the same Windows version that has it (the reference
does not depend on the GPU).
"""
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../ffp/src"))
from compare import judge, parse  # noqa: E402

STATES = ["before", "after"]
CONFIGS = ["w11-nvidia2", "w11-amd1", "w10-nvidia2"]
# (label, hardware run, reference run)
RUNS = [("HW", "x64-hw", "x64-ref"), ("SW", "x64-swvp", "x64-refswvp"),
        ("MHW", "x64-mixhw", "x64-ref"), ("MSW", "x64-mixsw", "x64-refswvp")]
TOTALS = [("x64-hw", "x64-ref"), ("x86-hw", "x86-ref"), ("x64-swvp", "x64-refswvp"),
          ("x64-mixhw", "x64-ref"), ("x64-mixsw", "x64-refswvp"), ("x64-hw", "x64-refswvp")]


def load(ev, state, cfg, prog, run):
    p = os.path.join(ev, state, cfg, "%s-%s.txt" % (prog, run))
    return parse(p) if os.path.exists(p) else None


def ref_for(ev, state, cfg, prog, run):
    r = load(ev, state, cfg, prog, run)
    if r:
        return r
    win = cfg.split("-")[0]
    for s in STATES:
        for c in CONFIGS:
            if c.startswith(win):
                r = load(ev, s, c, prog, run)
                if r:
                    return r
    return None


def cell(ref, got, case):
    if not ref or case not in ref or got is None:
        return "-"
    text, ok = judge(ref[case], got.get(case))
    return "pass" if ok else text.replace("FAIL ", "fail ")


def present(ev):
    return [(s, c) for c in CONFIGS for s in STATES if os.path.isdir(os.path.join(ev, s, c))]


def main():
    ev = sys.argv[1]
    cols = present(ev)
    prog = "d3d9_ffp2"
    ref = ref_for(ev, "after", "w11-nvidia2", prog, "x64-ref")
    refsw = ref_for(ev, "after", "w11-nvidia2", prog, "x64-refswvp")
    head = ["Case"]
    for s, c in cols:
        head.append("%s %s (HW, SW, MHW, MSW)" % (c, s))
    head.append("REF: SWVP = HWVP")
    print("| " + " | ".join(head) + " |")
    print("|" + " --- |" * len(head))
    for case in ref:
        row = ["`%s`" % case]
        for s, c in cols:
            cells = []
            for _, run, rrun in RUNS:
                cells.append(cell(ref_for(ev, s, c, prog, rrun), load(ev, s, c, prog, run), case))
            if all(x == "pass" for x in cells):
                row.append("pass")
            else:
                row.append(", ".join(cells))
        row.append("yes" if refsw and judge(ref[case], refsw.get(case))[1] else "differs")
        print("| " + " | ".join(row) + " |")
    print()
    for prog in ("d3d9_ffp2", "d3d9_ffp"):
        print("| `%s` run | %s |" % (prog, " | ".join("%s %s" % (c, s) for s, c in cols)))
        print("|" + " --- |" * (1 + len(cols)))
        for run, rrun in TOTALS:
            cells = []
            for s, c in cols:
                r, h = ref_for(ev, s, c, prog, rrun), load(ev, s, c, prog, run)
                if r and h is not None:
                    cells.append("%d of %d" % (sum(judge(r[k], h.get(k))[1] for k in r), len(r)))
                else:
                    cells.append("-")
            print("| %s against %s | %s |" % (run, rrun, " | ".join(cells)))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
