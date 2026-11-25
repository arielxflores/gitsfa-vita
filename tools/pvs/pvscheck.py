#!/usr/bin/env python3
"""Checks a map's visibility table (pvs.py) from camera positions it was not built from, and counts what it saves.

Views: the map's spawn points (eye height, 4 yaws, level) and N random cameras on the navigation mesh (40-340 cm up,
any yaw, pitch -50..50 degrees, 59 / 70 degree fov), plus each random camera's third-person orbit point 360 cm out.
Per view the chunks a camera there can see (pvslib.Scene.render: occluders hide, see-through surfaces only show) are
compared with the cell's set: a chunk seen but not in the set = a pop-in (its pixels counted). Indices and draws: as
world_draw submits them today (whole meshes in the frustum) and with the table (meshes split by chunk where world.h
reorders them - opaque / tested meshes that write depth - with contiguous visible chunks merged into one draw; other
meshes whole when any of their chunks is in the set).
usage: pvscheck.py <stem> <table.gpvs> [random views (300)] [width (960)]   -> one line, and the worst views
"""
import sys, os, struct, math
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import pvslib as L
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mapvis')); from gcol import spawns

def read_table(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GPV1'
    ver, nm, nc = struct.unpack_from('<III', d, 4); org = np.array(struct.unpack_from('<3f', d, 16)); cs = np.array(struct.unpack_from('<3f', d, 28))
    dims = np.array(struct.unpack_from('<3i', d, 40)); ns, sb = struct.unpack_from('<II', d, 52); o = 60 + 4 * nm + 8 * nc
    tab = np.frombuffer(d, '<u2', int(dims.prod()), o); o += 2 * int(dims.prod())
    sets = [np.unpackbits(np.frombuffer(d, np.uint8, sb, o + i * sb), bitorder='little')[:nc].astype(bool) for i in range(ns)]
    return org, cs, dims, tab, sets

def main():
    stem, tpath = sys.argv[1:3]; nr = int(sys.argv[3]) if len(sys.argv) > 3 else 300; W = int(sys.argv[4]) if len(sys.argv) > 4 else 960; H = W * 544 // 960
    meshes, chunks = L.load(stem); n = len(chunks); S = L.Scene(meshes, chunks, W, H)
    org, cs, dims, tab, sets = read_table(tpath)
    if os.environ.get('DIL'):   # each cell's set grown by its 26 neighbours' sets
        T3 = tab.reshape(dims[2], dims[1], dims[0]); nsets = []; nt = np.full_like(tab, 0xffff)
        for z in range(dims[2]):
            for y in range(dims[1]):
                for x in range(dims[0]):
                    if T3[z, y, x] == 0xffff: continue
                    u = np.zeros(len(sets[0]), bool)
                    for dz in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            for dx in (-1, 0, 1):
                                zz, yy, xx = z + dz, y + dy, x + dx
                                if 0 <= zz < dims[2] and 0 <= yy < dims[1] and 0 <= xx < dims[0] and T3[zz, yy, xx] != 0xffff: u |= sets[T3[zz, yy, xx]]
                    nt[x + dims[0] * (y + dims[1] * z)] = len(nsets); nsets.append(u)
        tab, sets = nt, nsets
    def setof(e):
        c = np.floor((e - org) / cs).astype(int)
        if (c < 0).any() or (c >= dims).any(): return None
        s = tab[c[0] + dims[0] * (c[1] + dims[1] * c[2])]; return None if s == 0xffff else sets[s]
    views = [(np.array((x, y, z + 152.0)), yw + k * math.pi / 2, 0.0, 59.0) for gi, t, x, y, z, yw in spawns(os.path.join(L.MAPS, stem + '.gcol')) for k in range(4)]
    rng = np.random.default_rng(11); nav = L.nav_points(stem, 150.0)
    for i in range(nr):
        p = nav[rng.integers(len(nav))] + (0, 0, rng.uniform(40, 340)); yaw = rng.uniform(0, 2 * math.pi); pit = rng.uniform(-50, 50)
        views.append((p, yaw, pit, 59.0 if i % 3 else 70.0))
        q = nav[rng.integers(len(nav))]; a = rng.uniform(0, 2 * math.pi)   # a third-person orbit camera: 360 cm out, 180 cm up, not pulled in by walls (worst case)
        views.append((q + (math.cos(a) * 360, math.sin(a) * 360, 180), a + math.pi, -10.0, 70.0))
    split = np.array([m['occ'] or m['mode'] == 1 for m in meshes])
    ntri = np.array([len(c['tris']) for c in chunks]); cm = np.array([c['mesh'] for c in chunks])
    bad_views = []; tot = dict(views=0, nodata=0, pop=0, poppx=0, idx_now=0, idx_new=0, d_now=0, d_new=0)
    for (e, yaw, pit, fov) in views:
        f = (math.cos(yaw) * math.cos(math.radians(pit)), math.sin(yaw) * math.cos(math.radians(pit)), math.sin(math.radians(pit)))
        mvp = L.persp(math.radians(fov), W / H, 10, 12000) @ L.look(e, f, (0, 0, 1))
        st = setof(e); tot['views'] += 1
        inb = S.boxes_in(mvp)
        meshin = np.array([L.box_in(mvp, m['mn'], m['mx']) for m in meshes])
        tot['idx_now'] += 3 * sum(len(m['I']) for i, m in enumerate(meshes) if meshin[i]); tot['d_now'] += int(meshin.sum())
        if st is None:
            tot['nodata'] += 1; tot['idx_new'] += 3 * sum(len(m['I']) for i, m in enumerate(meshes) if meshin[i]); tot['d_new'] += int(meshin.sum()); continue
        occ, see = S.render(mvp); seen = np.zeros(n, bool); seen[occ[occ >= 0]] = True; seen[see[see >= 0]] = True
        miss = seen & ~st
        if miss.any():
            px = int(np.isin(occ, np.nonzero(miss)[0]).sum() + np.isin(see, np.nonzero(miss)[0]).sum()); tot['pop'] += 1; tot['poppx'] += px
            bad_views.append((px, e.round().tolist(), round(math.degrees(yaw)), round(pit), int(miss.sum())))
        for i, m in enumerate(meshes):
            if not meshin[i]: continue
            cs_ = np.arange(m['c0'], m['c1'])
            if split[i]:
                v = st[cs_] & inb[cs_]; tot['idx_new'] += 3 * int(ntri[cs_][v].sum())
                tot['d_new'] += int(np.sum(np.diff(np.r_[0, v.astype(int)]) == 1))   # contiguous runs
            elif st[cs_].any(): tot['idx_new'] += 3 * len(m['I']); tot['d_new'] += 1
    v = tot['views']
    print('%-13s %4d views (%d without a table entry) | pop-in: %d views, %d pixels | world indices/view %.0fk -> %.0fk (%.0f%%), draws %.0f -> %.0f' % (
        stem, v, tot['nodata'], tot['pop'], tot['poppx'], tot['idx_now'] / v / 1e3, tot['idx_new'] / v / 1e3, 100 * (tot['idx_new'] / max(1, tot['idx_now']) - 1), tot['d_now'] / v, tot['d_new'] / v), flush=True)
    for b in sorted(bad_views, reverse=True)[:5]: print('   pop-in %d px at %s yaw %d pitch %d (%d chunks)' % b)

if __name__ == '__main__':
    main()
