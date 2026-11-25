#!/usr/bin/env python3
"""Compare two folders of HUD captures (render b40, the HUD split): the recording GL stand-ins (tools/ui/host, tools/menu/host,
tools/tank/host) write every HUD draw as (texture, blend flag, vertex count) + 17 floats a vertex. The split may cut a batch
into more draws, but the quads, their order, textures, blend and every vertex value must stay the same: then the frame is
the same pixel for pixel (the lean program equals HUD_FS at threshold 2). Per state: draws before / after, identical or not.
usage: capdiff.py <before dir> <after dir>
"""
import sys, os, struct


def stream(path):
    d = open(path, 'rb').read(); o = 0; draws = 0; out = bytearray()
    while o + 12 <= len(d):
        tex, pm, n = struct.unpack_from('<3i', d, o); o += 12; v = d[o:o + n * 68]; o += n * 68; draws += 1
        hdr = struct.pack('<2i', tex, pm)
        for i in range(n): out += hdr + v[i * 68:(i + 1) * 68]
    return draws, bytes(out), len(out) // 76


def main():
    a, b = sys.argv[1], sys.argv[2]; names = sorted(f for f in os.listdir(a) if f.endswith('.cap') and os.path.exists(os.path.join(b, f)))
    bad = 0; da = db = 0
    for f in names:
        na, sa, va = stream(os.path.join(a, f)); nb, sb, vb = stream(os.path.join(b, f)); same = sa == sb; bad += not same; da += na; db += nb
        print('%-28s draws %3d -> %3d, %5d vertices: %s' % (f[:-4], na, nb, va, 'identical' if same else 'DIFFERENT (%d vs %d vertices)' % (va, vb)))
    print('%d states: %d identical, %d different; draws %d -> %d' % (len(names), len(names) - bad, bad, da, db))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
