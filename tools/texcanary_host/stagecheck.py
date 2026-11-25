#!/usr/bin/env python3
"""render b41: which compressed uploads of a map reach vitaGL's transfer engine from a staging copy that is not 16-byte
aligned (without texcanary.h's padding), and how that matches a device log's BAD UPLOAD lines.

vitaGL (this libvitaGL.a) copies each DXT level into its frame data pool (vgl_reserve_data_pool, a bump allocator that does
not align) and transfers it into the swizzled texture: RAW128 for DXT3/5, RAW64 for DXT1. A map's textures are uploaded in
.gmap order, then the .gsky's. Each level advances the pool by its size, so the staging address of a texture's levels, mod
16, is the running sum of all earlier level sizes (the pool starts aligned; loading-screen frames in between moved it by
multiples of 16 in b40-2). The 4x4 level of a DXT1 chain is 8 bytes: it flips the alignment.
usage: stagecheck.py <stem> [device boot.log]   (BUILD /root/gits/build) -> one line per DXT5 texture
"""
import sys, os, re, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis'))
import gmapio
B = '/root/gits/build/maps'


def sky_textures(path):
    if not os.path.exists(path): return []
    d = open(path, 'rb').read(); mg, ver, nl, nt = struct.unpack_from('<4sIII', d, 0); o = 16; out = []
    for i in range(nt):
        w, h, fmt, n = struct.unpack_from('<HHBB', d, o); o += 6; lv = []
        for k in range(n):
            s = struct.unpack_from('<I', d, o)[0]; o += 4 + s; lv.append(s)
        out.append(dict(w=w, h=h, fmt=fmt, sizes=lv))
    return out


def main():
    stem = sys.argv[1]; g = gmapio.read('%s/%s.gmap' % (B, stem), decode=False)
    tex = [dict(w=t['w'], h=t['h'], fmt=t['fmt'], sizes=[len(L) for L in t['levels']], where='gmap %d' % i) for i, t in enumerate(g['tex'])]
    tex += [dict(t, where='sky %d' % i) for i, t in enumerate(sky_textures('%s/%s.gsky' % (B, stem)))]
    bad = set()
    if len(sys.argv) > 2:   # the device's BAD UPLOAD lines of this map (GL id = .gmap index + 3: true on the first map of a run only)
        txt = open(sys.argv[2], errors='replace').read(); m = re.search(r'loading map %s\n(.*?)(?:loading map |\Z)' % stem, txt, re.S)
        if m: bad = {int(x) for x in re.findall(r'BAD UPLOAD: texture (\d+) \(world\.h', m.group(1))}
    cum = 0
    hit = miss = 0
    for i, t in enumerate(tex):
        if t['fmt'] == 5:
            mis = cum % 16; line = '%-9s %4dx%-4d DXT5: staging %s' % (t['where'], t['w'], t['h'], 'at 8 mod 16 (RAW128 misaligned)' if mis else 'aligned')
            if bad and t['where'].startswith('gmap'):
                gid = int(t['where'].split()[1]) + 3; dev = gid in bad; line += ' | device: %s' % ('BAD' if dev else 'ok'); hit += dev == bool(mis); miss += dev != bool(mis)
            print(line)
        cum += sum(t['sizes'])
    if bad: print('agreement with the device: %d of %d DXT5 textures' % (hit, hit + miss))


if __name__ == '__main__':
    main()
