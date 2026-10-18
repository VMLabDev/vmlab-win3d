#!/usr/bin/env python3
"""Compare two console captures of D3D12ExecuteIndirect: the GPU run and WARP.

usage: compare-sample.py <gpu.png> <warp.png>

The sample animates 1024 triangles whose start positions come from rand(),
and the two captures are taken at different frames, so the pictures cannot
match pixel for pixel. This compares what they must share: the window's
clear colour (0, 0.2, 0.4), the share of the client area the triangles
cover, and how that coverage spreads across eight vertical bands (the
compute pass culls triangles outside the middle of the window). It reads
the pixels with ImageMagick (magick ... rgb:-).

Exit 0 when the GPU frame has triangles, its coverage is within a factor
of 1.5 of WARP's, and each band's share of the coverage is within 0.1 of
WARP's; 1 otherwise.
"""
import subprocess
import sys

CLEAR = (0, 51, 102)


def load(path):
    size = subprocess.run(["magick", "identify", "-format", "%w %h", path], check=True,
                          capture_output=True, text=True).stdout.split()
    w, h = int(size[0]), int(size[1])
    raw = subprocess.run(["magick", path, "-depth", "8", "rgb:-"], check=True, capture_output=True).stdout
    return w, h, raw


def stats(path):
    w, h, raw = load(path)
    def px(x, y):
        i = 3 * (y * w + x)
        return raw[i], raw[i + 1], raw[i + 2]
    def is_clear(p):
        return all(abs(a - b) <= 3 for a, b in zip(p, CLEAR))
    xs, ys = [], []
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            if is_clear(px(x, y)):
                xs.append(x)
                ys.append(y)
    if len(xs) < 1000:
        return None
    # The client area: the bounding box of clear-colour pixels.
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    bands = [0] * 8
    lit = total = 0
    for y in range(y0, y1 + 1, 2):
        for x in range(x0, x1 + 1, 2):
            total += 1
            if not is_clear(px(x, y)):
                lit += 1
                bands[min(7, (x - x0) * 8 // (x1 - x0 + 1))] += 1
    return {"client": (x0, y0, x1, y1), "coverage": lit / total,
            "bands": [b / lit if lit else 0 for b in bands]}


def main():
    gpu, warp = stats(sys.argv[1]), stats(sys.argv[2])
    ok = True
    for name, s in (("gpu", gpu), ("warp", warp)):
        if s is None:
            print(f"FAIL {name}: no window with the sample's clear colour")
            ok = False
        else:
            print(f"{name}: client {s['client']}, triangle coverage {s['coverage']:.3f}, "
                  f"bands {' '.join(f'{b:.2f}' for b in s['bands'])}")
    if not ok:
        return 1
    if gpu["coverage"] < 0.005:
        print("FAIL gpu: no triangles (clear colour only)")
        ok = False
    ratio = gpu["coverage"] / warp["coverage"] if warp["coverage"] else float("inf")
    if not 1 / 1.5 <= ratio <= 1.5:
        print(f"FAIL coverage ratio gpu/warp {ratio:.2f}, outside 0.67..1.5")
        ok = False
    worst = max(abs(a - b) for a, b in zip(gpu["bands"], warp["bands"]))
    if worst > 0.1:
        print(f"FAIL band shares differ by up to {worst:.2f}")
        ok = False
    print(f"{'PASS' if ok else 'FAIL'}: coverage ratio gpu/warp {ratio:.2f}, largest band difference {worst:.2f}")
    return 0 if ok else 1


sys.exit(main())
