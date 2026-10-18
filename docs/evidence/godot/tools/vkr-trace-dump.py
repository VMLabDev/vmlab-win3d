#!/usr/bin/env python3
"""Print a Venus command trace written by the diagnostic render server.

    vkr-trace-dump.py TRACE.bin VIRGLRENDERER_SRC [--hex N] [--from I] [--to I]

TRACE.bin is <VKR_TRACE>/vkr-<pid>.bin from a render server built with
patches/host/virglrenderer/0040 (docs/godot.md). Each record is
u32 size, u32 command type, u64 dispatch tag, u64 CLOCK_MONOTONIC ns, then the
command as the guest encoded it (u32 type, u32 flags, arguments). Type
0xffffffff is a text note (VKR_SYNC_SUBMIT results). Command names come from
the protocol header in VIRGLRENDERER_SRC. For vkCmd* commands the first
argument, the guest's command buffer id, is printed as cb=.
"""
import argparse
import re
import struct
import sys


def names(src):
    text = open(f"{src}/src/venus/venus-protocol/vn_protocol_renderer_defines.h").read()
    return {int(v): k for k, v in re.findall(r"VK_COMMAND_TYPE_(\w+)_EXT = (\d+),", text)}


def records(path):
    with open(path, "rb") as f:
        data = f.read()
    off = 0
    while off + 24 <= len(data):
        size, ctype, tag, ns = struct.unpack_from("<IIQQ", data, off)
        off += 24
        if off + size > len(data):
            break
        yield ctype, tag, ns, data[off:off + size]
        off += size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("src")
    ap.add_argument("--hex", type=int, default=48, help="argument bytes to print")
    ap.add_argument("--from", dest="start", type=int, default=0)
    ap.add_argument("--to", dest="stop", type=int, default=1 << 62)
    a = ap.parse_args()
    nm = names(a.src)
    t0 = None
    out = sys.stdout
    for i, (ctype, tag, ns, body) in enumerate(records(a.trace)):
        if t0 is None:
            t0 = ns
        if i < a.start:
            continue
        if i > a.stop:
            break
        ms = (ns - t0) / 1e6
        if ctype == 0xFFFFFFFF:
            out.write(f"{i:8d} {ms:10.3f} NOTE {body.decode(errors='replace')}\n")
            continue
        name = nm.get(ctype, f"type{ctype}")
        args = body[8:]
        extra = ""
        if name.startswith("vkCmd") and len(args) >= 8:
            extra = f" cb={struct.unpack_from('<Q', args)[0]}"
            args = args[8:]
        out.write(f"{i:8d} {ms:10.3f} {tag & 0xffff:04x} {name}{extra} [{len(body)}] {args[:a.hex].hex()}\n")


if __name__ == "__main__":
    main()
