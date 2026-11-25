# GVM1 summary: textures (size, format, bytes), bones / palette, meshes (texture, blend mode, LOD mask, vertices, indices), clips.
# usage: python3 gvminfo.py file.gvm ...
import sys, struct
def info(path):
    d = open(path, 'rb').read(); o = 4
    ntex, nb, npal, nmesh, nclip = struct.unpack_from('<5I', d, o); o += 20
    tex = []
    for i in range(ntex):
        w, h, fmt, nmp = struct.unpack_from('<HHBB', d, o); o += 6; tb = 0
        for m in range(nmp): sz, = struct.unpack_from('<I', d, o); o += 4 + sz; tb += sz
        tex.append((w, h, 'DXT1' if fmt == 1 else 'DXT5', nmp, tb))
    o += 32 * nb + 52 * npal
    meshes = []
    for i in range(nmesh):
        t, bl, nv, ni = struct.unpack_from('<4i', d, o); o += 16 + 32 * nv + 2 * ni + (2 if ni & 1 else 0)
        meshes.append((t, bl & 15, (bl >> 4) & 7 or 7, nv, ni))
    clips = []
    for i in range(nclip):
        nm = d[o:o + 16].split(b'\0')[0].decode(); nf, lp = struct.unpack_from('<2i', d, o + 16); o += 28 + nf * nb * 28; clips.append((nm, nf))
    return dict(size=len(d), ntex=ntex, nb=nb, npal=npal, tex=tex, meshes=meshes, clips=clips)
if __name__ == '__main__':
    for p in sys.argv[1:]:
        I = info(p); lod = [0, 0, 0]; tri = [0, 0, 0]
        for t, bl, lm, nv, ni in I['meshes']:
            for l in range(3):
                if lm & (1 << l): lod[l] += nv; tri[l] += ni // 3
        tb = sum(t[4] for t in I['tex'])
        print('%-22s %8d B  bones %2d pal %2d  tex %d (%d B: %s)  meshes %d  LOD verts %s tris %s  clips %s' % (p.split('/')[-1], I['size'], I['nb'], I['npal'], I['ntex'], tb,
              ' '.join('%dx%d%s' % (t[0], t[1], t[2][-1]) for t in I['tex']), len(I['meshes']), lod, tri, [c[0] for c in I['clips']]))
