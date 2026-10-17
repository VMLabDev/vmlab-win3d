#!/usr/bin/env python3
"""Grab one frame of a vmlab VM's console over VNC and look for a probe window.

    vnccap.py <unix-socket|host:port> <out.png>

QMP screendump fails under egl-headless (docs/host.md Q4), so this speaks
RFB directly, like labs/probe/tools/vnccap.py. It writes the full frame as a
PNG and prints one JSON line:

    {"png": ..., "width": W, "height": H, "probe_window": {...} | null}

probe_window is set when the frame holds the three marker squares every
probe draws (probes/README.md): red at client (48,48), blue at (W-48,48) and
green at (48,H-48), 64x64 each, for the default 1024x640 client. It gives
the client origin and a CRC of the client area, so two frames can show
whether the window is animating.
"""
import json, socket, struct, sys, zlib

CLIENT_W, CLIENT_H, M = 1024, 640, 48


def rx(s, n):
    b = bytearray()
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c:
            raise EOFError('vnc closed')
        b += c
    return bytes(b)


def grab(target):
    if ':' in target and not target.startswith('/'):
        h, p = target.rsplit(':', 1)
        s = socket.create_connection((h, int(p)), timeout=30)
    else:
        s = socket.socket(socket.AF_UNIX)
        s.settimeout(30)
        s.connect(target)
    rx(s, 12)
    s.sendall(b'RFB 003.008\n')
    n = rx(s, 1)[0]
    types = rx(s, n)
    if 1 not in types:
        sys.exit(f'no None security: {list(types)}')
    s.sendall(b'\x01')
    if struct.unpack('>I', rx(s, 4))[0]:
        sys.exit('security failed')
    s.sendall(b'\x01')
    w, h = struct.unpack('>HH', rx(s, 4))
    rx(s, 16)
    rx(s, struct.unpack('>I', rx(s, 4))[0])
    # 32 bpp, depth 24, little endian, true colour, R<<16 G<<8 B: bytes B,G,R,X.
    s.sendall(struct.pack('>BxxxBBBBHHHBBBxxx', 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
    s.sendall(struct.pack('>BxHi', 2, 1, 0))  # Raw encoding only
    s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, w, h))
    fb = bytearray(w * h * 4)
    while True:
        t = rx(s, 1)[0]
        if t == 0:
            rx(s, 1)
            for _ in range(struct.unpack('>H', rx(s, 2))[0]):
                x, y, rw, rh, enc = struct.unpack('>HHHHi', rx(s, 12))
                if enc != 0:
                    sys.exit(f'unexpected encoding {enc}')
                data = rx(s, rw * rh * 4)
                for r in range(rh):
                    o = ((y + r) * w + x) * 4
                    fb[o:o + rw * 4] = data[r * rw * 4:(r + 1) * rw * 4]
            break
        elif t == 2:
            continue
        elif t == 3:
            rx(s, 3)
            rx(s, struct.unpack('>I', rx(s, 4))[0])
        elif t == 1:
            rx(s, 3)
            rx(s, struct.unpack('>H', rx(s, 2))[0] * 6)
    s.close()
    return w, h, fb


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


def write_png(path, w, h, fb):
    raw = bytearray()
    for r in range(h):
        raw.append(0)
        row = fb[r * w * 4:(r + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = row[2::4], row[1::4], row[0::4]
        raw += rgb

    def chunk(k, d):
        return struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    w, h, fb = grab(sys.argv[1])
    write_png(sys.argv[2], w, h, fb)
    print(json.dumps({'png': sys.argv[2], 'width': w, 'height': h, 'probe_window': find_probe(w, h, fb)}))


if __name__ == '__main__':
    main()
