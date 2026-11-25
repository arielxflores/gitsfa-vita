#!/usr/bin/env python3
"""World visibility table (PVS) of one map (render b45): <stem>.gpvs, read by world.h at map load.

Camera cells: an XY x XY x Z grid; a cell gets a table entry when a camera can be in it: a point of the navigation mesh
(where players and bots stand) raised by 40-340 cm (crouched, standing and jumping eyes, the third-person and kill
cameras). The other cells have none, and a camera there draws every chunk in the frustum, as before.
A cell's set = every chunk seen from its 8 corners and its centre (cube maps of chunk-ID renders, RES^2 a face): opaque
surfaces that write depth hide, alpha-tested / blended / no-depth-write ones only show. The corners are shared lattice
points, rendered once. pvscheck.py verifies the table from other camera positions.
File (little endian): "GPV1", u32 version 1, u32 nmesh, u32 nchunk; f32 origin[3], f32 cell[3], i32 dims[3]; u32 nsets,
u32 setbytes; nmesh x u32 chunks of the mesh; nchunk x { u32 key, u32 triangles } (world.h checks them);
dims[0]*dims[1]*dims[2] x u16 set (0xffff: no entry), x fastest; nsets x setbytes bit sets (bit c = chunk c).
usage: pvs.py <stem> <out.gpvs> [xy cm (800)] [z cm (400)] [res (256)]
"""
import sys, os, time, struct, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import pvslib as L
FACES = [((1, 0, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1)), ((0, 1, 0), (0, 0, 1)), ((0, -1, 0), (0, 0, 1)), ((0, 0, 1), (0, 1, 0)), ((0, 0, -1), (0, 1, 0))]
HEIGHTS = np.arange(40.0, 341.0, 50.0)

def cube_vis(S, P, eye, n):
    v = np.zeros(n, bool)
    for f, u in FACES:
        for idv in S.render(P @ L.look(eye, f, u)): v[idv[idv >= 0]] = True
    return v

def main():
    stem, out = sys.argv[1:3]; cxy = float(sys.argv[3]) if len(sys.argv) > 3 else 800.0; cz = float(sys.argv[4]) if len(sys.argv) > 4 else 400.0
    res = int(sys.argv[5]) if len(sys.argv) > 5 else 256; t0 = time.time()
    meshes, chunks = L.load(stem); n = len(chunks); S = L.Scene(meshes, chunks, res, res); P = L.persp(math.radians(90), 1, 5, 20000)
    pts = L.nav_points(stem); cs = np.array([cxy, cxy, cz])
    org = np.floor((pts.min(0) - (0, 0, 0)) / cs) * cs; cells = set()
    for h in HEIGHTS: cells |= set(map(tuple, np.floor((pts + (0, 0, h) - org) / cs).astype(int)))
    C = np.array(sorted(cells)); dims = C.max(0) + 1
    lat = {}
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                for c in map(tuple, C + (dx, dy, dz)): lat[c] = None
    for k in lat: lat[k] = cube_vis(S, P, org + np.array(k) * cs, n)
    sets = {}; table = np.full(int(dims.prod()), 0xffff, np.uint16); setlist = []
    for c in map(tuple, C):
        v = cube_vis(S, P, org + (np.array(c) + 0.5) * cs, n)
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1): v |= lat[(c[0] + dx, c[1] + dy, c[2] + dz)]
        b = np.packbits(v, bitorder='little').tobytes()
        if b not in sets: sets[b] = len(setlist); setlist.append(b)
        table[c[0] + dims[0] * (c[1] + dims[1] * c[2])] = sets[b]
    sb = (n + 7) // 8
    with open(out, 'wb') as f:
        f.write(b'GPV1' + struct.pack('<III', 1, len(meshes), n) + struct.pack('<3f', *org) + struct.pack('<3f', *cs) + struct.pack('<3i', *dims) + struct.pack('<II', len(setlist), sb))
        f.write(np.array([m['c1'] - m['c0'] for m in meshes], '<u4').tobytes())
        f.write(np.array([(c['key'], len(c['tris'])) for c in chunks], '<u4').tobytes())
        f.write(table.astype('<u2').tobytes()); f.write(b''.join(setlist))
    frac = np.mean([np.frombuffer(s, np.uint8).view(np.uint8) is not None and np.unpackbits(np.frombuffer(s, np.uint8), bitorder='little')[:n].mean() for s in setlist])
    tri_all = sum(len(c['tris']) for c in chunks)
    tri_in = np.mean([sum(len(chunks[i]['tris']) for i in np.nonzero(np.unpackbits(np.frombuffer(setlist[table[c[0] + dims[0] * (c[1] + dims[1] * c[2])]], np.uint8), bitorder='little')[:n])[0]) for c in map(tuple, C)])
    print('%-13s %d chunks, %d cells (%d lattice points), %d sets, %s bytes; a cell keeps %.0f%% of the chunks, %.0f%% of the triangles; %.0f s' % (
        stem, n, len(C), len(lat), len(setlist), os.path.getsize(out), 100 * frac, 100 * tri_in / tri_all, time.time() - t0), flush=True)

if __name__ == '__main__':
    main()
