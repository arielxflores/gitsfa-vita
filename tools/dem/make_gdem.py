# Demolition data per map -> build/data/<stem>.gdem (+ one readable summary dem_sites.txt)
# Source: the map's own SpawnSystem.ssf (DEM section, see ssf_dem.py) + worldEnv/EnvBomb/BOMB_*.nif (target collision
# meshes), checked against our build/maps/<stem>.gcol / .gnav.
#
# GDEM v1, little endian
#   char magic[4] "GDEM"; u32 version = 1
#   u32 nsite; u32 natt; u32 ndef; u32 reserved
#   site[nsite] (A first, then B):
#     char name[16]          target object, e.g. "BOMB_DEM03_A"
#     char letter            'A' / 'B'
#     u8   add_col           1 = the map collision does not contain this target: add its triangles to player collision
#     u16  pad
#     f32  origin[3], yaw    the object transform from the SSF (yaw: radians about +z, applied as rotz(yaw) to the mesh)
#     f32  bmin[3], bmax[3]  world AABB of the target mesh
#     f32  marker[3]         where to draw the in-world A/B marker (top centre of the AABB + 60 cm)
#     f32  stand[3]          a walkable nav-mesh point next to the target (for bots planting / defusing)
#     f32  aim[3]            the point on the target to look at from stand + eye height (152.6 cm): ray hits it < 190 cm
#     u32  nv, nt; f32 v[nv][3] (world space); u16 tri[nt][3]; u16 pad if nt*3 is odd
#   spawn att[natt]: f32 x, y, z, yaw   attackers (yaw in the ENGINE convention: forward = (cos yaw, sin yaw);
#   spawn def[ndef]: f32 x, y, z, yaw   defenders  the SSF stores the opposite: engine yaw = ssf yaw + pi)
import sys, os, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); sys.path.insert(0, os.path.join(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'), 'minimap'))
import ssf_dem, export_col, gcol, gitspaths
RAW = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DEMX')          # extracted map.dat / worldEnv.dat files
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
BMAPS = gitspaths.root('BMAPS')
MAPS = {'downtown': 'Fix_Downtown', 'ecenter': 'Fix_Ecenter', 'headquarter': 'Fix_Headquarter', 'pss9': 'Fix_PSS9', 'trainstation': 'Fix_Trainstation', 'zioplant': 'Fix_Zioplant'}

def rotz(a):
    c, s = np.cos(a), np.sin(a); return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1.0]])
def target(name, v):
    P = []; T = []; o = 0
    for n, p, i in export_col.run(RAW + '/worldEnv/EnvBomb/%s.nif' % name):
        P.append(p); T.append(i.reshape(-1, 3) + o); o += len(p)
    P = np.concatenate(P) @ rotz(v[5]).T + np.array(v[:3]); T = np.concatenate(T)
    # weld duplicate vertices (NIF streams repeat them per normal/colour)
    key = np.round(P, 2); uq, inv = np.unique(key, axis=0, return_inverse=True)
    T = inv.reshape(-1)[T]; T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
    return uq.astype(np.float32), T.astype(np.int64)
def ray_tris(o, d, T):
    """nearest hit distance of the ray o + t d (t > 0) with triangles T (n,3,3), both faces, or None"""
    if len(T) == 0: return None
    e1 = T[:, 1] - T[:, 0]; e2 = T[:, 2] - T[:, 0]; p = np.cross(d, e2); det = (e1 * p).sum(1)
    ok = np.abs(det) > 1e-6; inv = np.where(ok, 1 / np.where(ok, det, 1), 0); tv = o - T[:, 0]
    u = (tv * p).sum(1) * inv; q = np.cross(tv, e1); v = (q * d).sum(1) * inv; t = (e2 * q).sum(1) * inv
    hit = ok & (u >= 0) & (u <= 1) & (v >= 0) & (u + v <= 1) & (t > 1e-3)
    return float(t[hit].min()) if hit.any() else None
def load_nav(path):
    d = open(path, 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    return np.frombuffer(d, dtype=[('v', '<f4', (3, 3)), ('nb', '<i4', 3)], count=n, offset=8)['v'].copy()
def floor_z(V, I, x, y, z, up=60, down=200):
    """highest upward-facing collision triangle under (x,y) within [z-down, z+up]"""
    T = V[I]; best = None
    a, b, c = T[:, 0], T[:, 1], T[:, 2]
    d = (b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0])
    ok = np.abs(d) > 1e-6
    u = np.where(ok, ((x - a[:, 0]) * (c[:, 1] - a[:, 1]) - (y - a[:, 1]) * (c[:, 0] - a[:, 0])) / np.where(ok, d, 1), -1)
    w = np.where(ok, ((b[:, 0] - a[:, 0]) * (y - a[:, 1]) - (b[:, 1] - a[:, 1]) * (x - a[:, 0])) / np.where(ok, d, 1), -1)
    ins = ok & (u >= 0) & (w >= 0) & (u + w <= 1)
    zz = a[:, 2] + u * (b[:, 2] - a[:, 2]) + w * (c[:, 2] - a[:, 2])
    nz = np.cross(b - a, c - a)[:, 2]
    sel = ins & (zz > z - down) & (zz < z + up)
    return float(zz[sel].max()) if sel.any() else None

def build(stem, fx, log):
    r = ssf_dem.load(RAW + '/map/FixedMap/%s/SpawnSystem.ssf' % fx)
    V, I, S, Mt = gcol.load('%s/%s.gcol' % (BMAPS, stem)); I = I.astype(np.int64)
    NAV = load_nav('%s/%s.gnav' % (BMAPS, stem)) if os.path.exists('%s/%s.gnav' % (BMAPS, stem)) else np.zeros((0, 3, 3), np.float32)
    navpts = np.concatenate([NAV.reshape(-1, 3), NAV.mean(1)]) if len(NAV) else np.zeros((0, 3))
    cen = V[I].mean(1)
    sites = []
    for name, v in r['targets']:
        P, T = target(name, v); lo, hi = P.min(0), P.max(0)
        inner = np.all((cen > lo + 15) & (cen < hi - 15), axis=1).sum()     # map collision triangles inside the target volume
        add_col = 1 if inner < 4 else 0
        marker = np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, hi[2] + 60])
        # stand: walkable nav point from which the eye (152.6 cm up) sees a target triangle within 190 cm, unobstructed by
        # the map collision; prefer ~100 cm. aim = that triangle's centre.
        dx = np.maximum(np.maximum(lo[0] - navpts[:, 0], navpts[:, 0] - hi[0]), 0); dy = np.maximum(np.maximum(lo[1] - navpts[:, 1], navpts[:, 1] - hi[1]), 0)
        dh = np.hypot(dx, dy); cand = np.where((np.abs(navpts[:, 2] - lo[2]) < 220) & (dh < 220))[0]
        # aim candidates: points spread over every target triangle (barycentric grid), not just the centroids
        bw = np.array([(a / 6, b / 6) for a in range(7) for b in range(7 - a)]); TT = P[T]
        tc = (TT[:, None, 0] * (1 - bw[None, :, 0:1] - bw[None, :, 1:2]) + TT[:, None, 1] * bw[None, :, 0:1] + TT[:, None, 2] * bw[None, :, 1:2]).reshape(-1, 3)
        tc = tc + (tc - tc.mean(0)) * -0.002; best = None
        near = np.all((cen > lo - 400) & (cen < hi + 400), axis=1) & ~np.all((cen > lo - 30) & (cen < hi + 30), axis=1); MT = V[I[near]]   # occluders (not the target's own map collision)
        for ci in cand:
            eye = navpts[ci] + np.array([0, 0, 152.6])
            for k in np.argsort(np.linalg.norm(tc - eye, axis=1))[:8]:
                d = tc[k] - eye; L = np.linalg.norm(d); d = d / L
                h = ray_tris(eye, d, P[T]); 
                if h is None or h > 190 or h < 40: continue
                w = ray_tris(eye, d, MT)
                if w is not None and w < h - 12: continue                 # a wall in between (zioplant A lies 5 cm under the floor grate)
                sc = abs(h - 100)
                if best is None or sc < best[0]: best = (sc, navpts[ci], eye + d * h)
        stand, aim = (best[1], best[2]) if best else (np.array([(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, lo[2]]), (lo + hi) / 2)
        if best is None: log.append('  !! %s: no stand point found' % name)
        sites.append(dict(name=name, letter=name[-1], add_col=add_col, v=v, lo=lo, hi=hi, marker=marker, stand=stand, aim=aim, P=P, T=T, inner=inner))
    def spawns(L):
        out = []
        for s in L:
            fz = floor_z(V, I, s[0], s[1], s[2])
            yaw = (s[5] + np.pi + np.pi) % (2 * np.pi) - np.pi
            out.append((s[0], s[1], s[2], yaw, fz))
        return out
    att, dfn = spawns(r['attack']), spawns(r['defense'])
    with open('%s/%s.gdem' % (OUT, stem), 'wb') as f:
        f.write(b'GDEM' + struct.pack('<I', 1) + struct.pack('<4I', len(sites), len(att), len(dfn), 0))
        for s in sites:
            f.write(s['name'].encode()[:15].ljust(16, b'\0')); f.write(struct.pack('<cBH', s['letter'].encode(), s['add_col'], 0))
            f.write(struct.pack('<4f', *s['v'][:3], s['v'][5])); f.write(struct.pack('<6f', *s['lo'], *s['hi']))
            f.write(struct.pack('<3f', *s['marker'])); f.write(struct.pack('<3f', *s['stand'])); f.write(struct.pack('<3f', *s['aim']))
            f.write(struct.pack('<II', len(s['P']), len(s['T']))); f.write(s['P'].astype('<f4').tobytes()); f.write(s['T'].astype('<u2').tobytes())
            if (len(s['T']) * 3) % 2: f.write(b'\0\0')
        for L in (att, dfn):
            for x, y, z, yaw, fz in L: f.write(struct.pack('<4f', x, y, z, yaw))
    log.append('[%s]  (%s/SpawnSystem.ssf)' % (stem, fx))
    for s in sites:
        log.append('  site %s  %-15s origin (%.1f, %.1f, %.1f) yaw %.4f  box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)  marker (%.0f %.0f %.0f)  stand (%.0f %.0f %.0f) aim (%.0f %.0f %.0f)  tris %d  add_col %d (map tris inside: %d)'
                   % (s['letter'], s['name'], *s['v'][:3], s['v'][5], *s['lo'], *s['hi'], *s['marker'], *s['stand'], *s['aim'], len(s['T']), s['add_col'], s['inner']))
    for nm, L in (('attack', att), ('defense', dfn)):
        bad = sum(1 for p in L if p[4] is None or abs(p[4] - p[2]) > 40)
        log.append('  %-7s spawns %d (off-floor: %d): ' % (nm, len(L), bad) + ' '.join('(%.0f,%.0f,%.0f y%.2f)' % p[:4] for p in L))
    return os.path.getsize('%s/%s.gdem' % (OUT, stem))

if __name__ == '__main__':
    log = ['# Demolition sites and spawns (make_gdem.py). Source: <map>/SpawnSystem.ssf DEM section + worldEnv/EnvBomb/BOMB_*.nif',
           '# spawn yaw here = engine convention (forward = cos,sin) = SSF yaw + pi']
    for st, fx in MAPS.items():
        n = build(st, fx, log); print(st, n, 'bytes')
    open(OUT + '/dem_sites.txt', 'w').write('\n'.join(log) + '\n'); print('\n'.join(log))
