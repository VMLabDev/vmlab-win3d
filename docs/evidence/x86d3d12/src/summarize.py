#!/usr/bin/env python3
"""Summarise a loop.sh output directory: per architecture, runs, passes,
frames, fps, console windows seen, and startup_seconds (from the probe JSONs).
usage: summarize.py <loop-dir>"""
import csv, glob, json, os, sys
d = sys.argv[1]
rows = list(csv.DictReader(open(os.path.join(d, 'runs.tsv')), delimiter='\t'))
for arch in sorted({r['arch'] for r in rows}):
    rs = [r for r in rows if r['arch'] == arch]
    st = []
    for r in rs:
        f = os.path.join(d, f"{arch}-{int(r['iter']):03d}.json")
        try:
            v = json.load(open(f, encoding='utf-8-sig')).get('startup_seconds')
            if v is not None:
                st.append(v)
        except Exception:
            pass
    num = lambda k: [float(r[k]) for r in rs if r[k] not in ('-', '')]
    fps, fr, win, run = num('fps'), num('frames'), num('window_frames'), num('run_s')
    print(f"{arch}: {len(rs)} runs, {sum(r['result'] == 'pass' for r in rs)} pass, "
          f"frames min {min(fr):.0f}, fps mean {sum(fps)/len(fps):.1f} min {min(fps):.1f}, "
          f"console windows min {min(win):.0f}, run_s max {max(run):.1f}"
          + (f", startup_seconds mean {sum(st)/len(st):.2f} max {max(st):.2f}" if st else ""))
