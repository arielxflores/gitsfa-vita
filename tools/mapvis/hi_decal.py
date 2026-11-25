# hi_decal.py (b105): a map's blended ground decals at the PC's own texture size.
# tools/map_visual.py gives a map's most stretched base textures 512 / 1024 px (hi_plan, b61 / b69) - but only those of
# opaque and alpha-tested surfaces: every BLENDED decal stayed at 256 px. Urban's leaf litter lies 4.3 m to a tile: 0.6
# texels a centimetre, blobs at walking distance (the user's screenshot: "the blurry textures i mentioned in urban"); the
# PC's Leaf_Decal_01_D.dds is 512 px. This puts named decal textures into a built .gmap at the original's size, made as
# map_visual.py's TexStore makes them (the same mip filter, the same DXT5 encoder), every other byte of the map kept.
# Each replacement is checked first: the original scaled to the map's size must BE the texture it replaces.
#   hi_decal.py IN.gmap OUT.gmap INDEX=ORIGINAL.dds [...]        (IN / OUT unpacked: tools/glz)
import sys, os, struct, numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H); sys.path.insert(0, os.path.join(H, '..'))
from PIL import Image
import gmapio, map_visual as mv
def main():
    src, dst = sys.argv[1], sys.argv[2]; rep = dict((int(a.split('=')[0]), a.split('=', 1)[1]) for a in sys.argv[3:])
    d = open(src, 'rb').read(); mg, ver, nt, nm = struct.unpack_from('<4sIII', d, 0); o = 16; out = [d[:16]]
    for i in range(nt):
        w, h, fmt, n = struct.unpack_from('<HHBB', d, o); p = o + 6; lv = []
        for k in range(n): s, = struct.unpack_from('<I', d, p); lv.append(d[p + 4:p + 4 + s]); p += 4 + s
        if i in rep:
            im = Image.open(rep[i]).convert('RGBA'); assert im.size[0] == im.size[1] and im.size[0] > w, (rep[i], im.size, w)
            old = gmapio.dds_decode(w, h, lv[0], fmt).astype(float); new = np.asarray(im.resize((w, h), Image.LANCZOS)).astype(float)
            al = old[:, :, 3:4] / 255; err = float((np.abs(old[:, :, :3] - new[:, :, :3]) * al).mean()); aerr = float(np.abs(old[:, :, 3] - new[:, :, 3]).mean())
            assert err < 6 and aerr < 6, '%s is not texture %d (colour %.1f, alpha %.1f off)' % (rep[i], i, err, aerr)
            f2 = 5 if np.asarray(im)[:, :, 3].min() < 250 else 1; assert f2 == fmt, (f2, fmt)
            mips = mv.TexStore._mips(im, fmt); out.append(struct.pack('<HHBB', im.size[0], im.size[1], fmt, len(mips)) + b''.join(struct.pack('<I', len(m)) + m for m in mips))
            print('texture %d: %dx%d -> %dx%d, %d levels (%s; it matched within %.1f / %.1f of 255)' % (i, w, h, im.size[0], im.size[1], len(mips), os.path.basename(rep[i]), err, aerr))
        else: out.append(d[o:p])
        o = p
    out.append(d[o:]); open(dst, 'wb').write(b''.join(out)); print(dst, sum(len(x) for x in out), 'bytes (was %d)' % len(d))
if __name__ == '__main__': main()
