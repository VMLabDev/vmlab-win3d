#!/usr/bin/env python3
"""Look for a probe window in a console screenshot.

    findprobe.py <screenshot.png>

The screenshot comes from `vmlab vm screenshot` (VNC on GL displays since
vmlab 0.24). ImageMagick decodes it. Prints one JSON line:

    {"png": ..., "width": W, "height": H, "probe_window": {...} | null}

probe_window is set when the frame holds the three marker squares every
probe draws (probes/README.md): red at client (48,48), blue at (W-48,48) and
green at (48,H-48), 64x64 each, for the default 1024x640 client. It gives
the client origin and a CRC of the client area, so two frames can show
whether the window is animating.
"""
import json, subprocess, sys, zlib

CLIENT_W, CLIENT_H, M = 1024, 640, 48


def px(fb, w, x, y):
    o = (y * w + x) * 4
    return fb[o + 2], fb[o + 1], fb[o]


def near(c, want, tol=24):
    return all(abs(a - b) <= tol for a, b in zip(c, want))


def find_probe(w, h, fb):
    dx, dy = CLIENT_W - 2 * M, CLIENT_H - 2 * M
    for y in range(0, h - dy, 4):
        for x in range(0, w - dx, 4):
            if not near(px(fb, w, x, y), (255, 0, 0)):
                continue
            if not (near(px(fb, w, x + dx, y), (0, 0, 255)) and near(px(fb, w, x, y + dy), (0, 255, 0))):
                continue
            # Walk to the red square's top-left corner; the client origin is 16 px above and left of it.
            x0, y0 = x, y
            while x0 > 0 and near(px(fb, w, x0 - 1, y0), (255, 0, 0)):
                x0 -= 1
            while y0 > 0 and near(px(fb, w, x0, y0 - 1), (255, 0, 0)):
                y0 -= 1
            cx, cy = x0 - (M - 32), y0 - (M - 32)
            crc = 0
            for r in range(max(cy, 0), min(cy + CLIENT_H, h)):
                o = (r * w + max(cx, 0)) * 4
                crc = zlib.crc32(fb[o:o + (min(cx + CLIENT_W, w) - max(cx, 0)) * 4], crc)
            return {'client_x': cx, 'client_y': cy, 'markers': ['red', 'blue', 'green'], 'crc': f'{crc:08x}'}
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    png = sys.argv[1]
    w, h = map(int, subprocess.run(['magick', 'identify', '-format', '%w %h', png], check=True,
                                   capture_output=True, text=True).stdout.split())
    fb = bytearray(subprocess.run(['magick', png, '-depth', '8', 'BGRA:-'], check=True, capture_output=True).stdout)
    print(json.dumps({'png': png, 'width': w, 'height': h, 'probe_window': find_probe(w, h, fb)}))


if __name__ == '__main__':
    main()
