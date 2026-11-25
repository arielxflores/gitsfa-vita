#!/usr/bin/env python3
"""Compare the device's "texsrc" lines (boot.log, written by texcanary.h's tc_srclog for every map load) with the
.gmap files that shipped: a mismatch means the bytes the Vita read from the memory card are not the file's bytes
(file reading gone wrong), before vitaGL ever saw them.

usage: texsrc_check.py <boot.log> <data dir with the .gmap files (or their .000/.001 pieces)>
       texsrc_check.py --selftest      (the hash of tools/texcanary_host/test.c's texture a: must print 9d69c800)
"""
import os, re, struct, sys
import numpy as np

M32 = 0xFFFFFFFF


def srchash(b):
    """tc_srchash: sum of (w_i ^ (i * 0x9E3779B1)) * (2i + 1) over the little-endian 32-bit words, then ^ n."""
    n = len(b)
    w = np.frombuffer(b[: n // 4 * 4], dtype='<u4').astype(np.uint64)
    i = np.arange(len(w), dtype=np.uint64)
    x = (w ^ ((i * 0x9E3779B1) & M32)) * ((2 * i + 1) & M32)
    return (int((x & M32).sum()) & M32) ^ n


def combine(levels):
    s = 0
    for b in levels:
        s = (s * 16777619 + srchash(b)) & M32
    return s


class Pieces:
    """a .gmap, or its .000, .001, ... pieces read as one stream (world.h's w_read)"""
    def __init__(self, path):
        self.files = []
        if os.path.exists(path + '.000'):
            k = 0
            while os.path.exists('%s.%03d' % (path, k)):
                self.files.append('%s.%03d' % (path, k)); k += 1
        elif os.path.exists(path):
            self.files = [path]
        self.data = b''.join(open(f, 'rb').read() for f in self.files)
        self.pos = 0

    def read(self, n):
        b = self.data[self.pos:self.pos + n]; self.pos += n
        if len(b) != n:
            raise EOFError
        return b


def gmap_hashes(path):
    f = Pieces(path)
    if not f.files:
        return None
    mg, ver, nt, nm = struct.unpack('<4sIII', f.read(16))
    assert mg == b'GMAP', path
    out = []
    for _ in range(nt):
        w, h, fmt, nmip = struct.unpack('<HHBB', f.read(6))
        levels = []
        for _m in range(nmip):
            sz, = struct.unpack('<I', f.read(4)); levels.append(f.read(sz))
        out.append(combine(levels))
    return out


def main():
    if sys.argv[1:] == ['--selftest']:
        src = bytes(((i * 7 + (i >> 8)) & 255) for i in range(4096 + 1024))
        print('%08x' % combine([src[:4096], src[4096:]]))
        return 0
    log, data = sys.argv[1], sys.argv[2]
    dev = {}   # map -> {index: hash}
    for line in open(log, errors='replace'):
        m = re.match(r'texsrc (\S+) (\d+):((?: [0-9a-f]{8})+)\s*$', line)
        if m:
            d = dev.setdefault(m.group(1), {})
            for k, hx in enumerate(m.group(3).split()):
                d[int(m.group(2)) + k] = int(hx, 16)
    if not dev:
        print('no texsrc lines in', log); return 1
    bad = 0
    for stem, d in dev.items():
        ref = gmap_hashes(os.path.join(data, stem + '.gmap'))
        if ref is None:
            print('%-14s no .gmap in %s' % (stem, data)); continue
        diff = [i for i, hv in sorted(d.items()) if i >= len(ref) or ref[i] != hv]
        print('%-14s %3d textures: %s' % (stem, len(d), 'all match the file' if not diff else '%d DIFFER: %s' % (len(diff), diff[:20])))
        bad += len(diff)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
