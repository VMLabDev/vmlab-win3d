#!/usr/bin/env python3
"""Minimal RFB client: grab one full framebuffer as PNG. Usage: vnccap.py <host:port|unix-socket> <out.png>"""
import socket, struct, sys, zlib

def rx(s, n):
    b = b''
    while len(b) < n:
        c = s.recv(n - len(b))
        if not c: raise EOFError('vnc closed')
        b += c
    return b

target, out = sys.argv[1], sys.argv[2]
if ':' in target and not target.startswith('/'):
    h, p = target.rsplit(':', 1); s = socket.create_connection((h, int(p)), timeout=30)
else:
    s = socket.socket(socket.AF_UNIX); s.settimeout(30); s.connect(target)
ver = rx(s, 12); s.sendall(b'RFB 003.008\n')
n = rx(s, 1)[0]; types = rx(s, n)
if 1 not in types: sys.exit(f'no None security: {list(types)}')
s.sendall(b'\x01'); res = struct.unpack('>I', rx(s, 4))[0]
if res: sys.exit('security failed')
s.sendall(b'\x01')  # shared
w, h = struct.unpack('>HH', rx(s, 4)); rx(s, 16); nl = struct.unpack('>I', rx(s, 4))[0]; name = rx(s, nl)
s.sendall(struct.pack('>BxxxBBBBHHHBBBxxx', 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
s.sendall(struct.pack('>BxH i', 2, 1, 0))
s.sendall(struct.pack('>BBHHHH', 3, 0, 0, 0, w, h))
fb = bytearray(w * h * 4)
while True:
    t = rx(s, 1)[0]
    if t == 0:
        rx(s, 1); nr = struct.unpack('>H', rx(s, 2))[0]
        for _ in range(nr):
            x, y, rw, rh, enc = struct.unpack('>HHHHi', rx(s, 12))
            if enc != 0: sys.exit(f'unexpected encoding {enc}')
            data = rx(s, rw * rh * 4)
            for r in range(rh):
                o = ((y + r) * w + x) * 4
                fb[o:o + rw * 4] = data[r * rw * 4:(r + 1) * rw * 4]
        break
    elif t == 2: continue
    elif t == 3: rx(s, 3); ln = struct.unpack('>I', rx(s, 4))[0]; rx(s, ln)
    elif t == 1: rx(s, 3); nc = struct.unpack('>H', rx(s, 2))[0]; rx(s, nc * 6)
raw = bytearray()
for r in range(h):
    raw.append(0)
    row = fb[r * w * 4:(r + 1) * w * 4]
    for i in range(0, len(row), 4):
        raw += bytes((row[i + 2], row[i + 1], row[i]))
def chunk(k, d): return struct.pack('>I', len(d)) + k + d + struct.pack('>I', zlib.crc32(k + d))
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
open(out, 'wb').write(png)
print(f'{out}: {w}x{h} name={name.decode(errors="replace")}')
