# Appends the map's trailers to its .glgt (engine/src/light.h reads them; older engines stop after the light columns):
#   'ACTR', u32 1, f32 ambient[3], diffuse[3], specular[3] (0..1), f32 dimmer   the actor light: light.ini [ACTOR]
#                                                                               (map.dat map\FixedMap\Fix_<Map>\light.ini;
#                                                                               tutorial / tutorial2 have none)
#   'FOGP', u32 1, f32 colour[3], f32 start (cm), density (1/cm), amount        databuilder: the map's fog, World.ini
#        [NIFOG] / [HEIGHTFOG] (tools/databuilder/mapini.py), at the precision of the engine's former MAPS[] table (colour
#        3 decimals, start 1, density 6, amount 2), so the engine's fog is bit-identical to b46's compiled-in values
# usage: glgt_actor.py IN_DIR OUT_DIR [stem ...]
import sys, os, struct
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'databuilder'))
import mapini   # databuilder: the values are read from each map's light.ini / World.ini

def columns_end(d):
    """the end of the light columns (where the trailers start), as light.h walks them"""
    w, h = struct.unpack_from('<II', d, 20); o = 28
    for _ in range(w * h):
        if o >= len(d): break
        o += 1 + 5 * d[o]
    return o

def fog_values(stem):
    (r, g, b), (start, dens, amount) = mapini.fog(stem)
    return [round(r, 3), round(g, 3), round(b, 3), round(start, 1), round(dens, 6), round(amount, 2)]

def add(src, dst, stem):
    d = open(src, 'rb').read(); d = d[:columns_end(d)]   # (replaces older trailers)
    act = mapini.actor(stem)
    if act:
        a, df, sp, dim = act
        d += b'ACTR' + struct.pack('<I10f', 1, *[v / 255.0 for v in a + df + sp], dim)
    d += b'FOGP' + struct.pack('<I6f', 1, *fog_values(stem))
    open(dst, 'wb').write(d); return len(d)

if __name__ == '__main__':
    ind, outd = sys.argv[1:3]; stems = sys.argv[3:] or sorted(mapini.FIX)
    for s in stems: print(s, add('%s/%s.glgt' % (ind, s), '%s/%s.glgt' % (outd, s), s))
