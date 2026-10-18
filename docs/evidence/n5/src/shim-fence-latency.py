#!/usr/bin/env python3
# Summarise a shim.so log (shim.c): virtio fence latency from creation to
# virglrenderer's callback, for plain (context 0) fences and per ring of the
# context fences.  Usage: shim-fence-latency.py shim.log
import collections, statistics as st, sys
pend, lat = {}, collections.defaultdict(list)
for line in open(sys.argv[1]):
    f = line.split()
    t, ev = float(f[0]), f[1]
    if ev == 'create_fence':
        pend[('plain', f[2])] = t
    elif ev == 'cb_fence' and ('plain', f[2]) in pend:
        lat['plain (context 0)'].append(t - pend.pop(('plain', f[2])))
    elif ev == 'ctx_create_fence':
        pend[(f[2], f[3], f[4])] = t
    elif ev == 'cb_ctx_fence' and (f[2], f[3], f[4]) in pend:
        lat['context fence, ' + f[3]].append(t - pend.pop((f[2], f[3], f[4])))
    elif ev == 'init':
        print('virgl_renderer_init', ' '.join(f[2:]))
for k, v in sorted(lat.items()):
    q = st.quantiles(v, n=10) if len(v) > 2 else [min(v)] * 9
    print(f"{k}: n={len(v)} p10={q[0]:.2f} p50={st.median(v):.2f} p90={q[8]:.2f} ms, {sum(x >= 9 for x in v)} at 9 ms or more")
