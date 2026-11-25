# Elimination data per map -> build/data/<stem>.gelim (+ readable summary elim_maps.txt) and the mode's own radar
# image <stem>_em.gmm (the EM_* minimap with the lockable zones hatched in red).
# Sources (the game's own files, see BRIEF.md):
#   <map>/SpawnSystem.ssf   two spawn lists at 0x0c and 0x208 (21 slots x (x,y,z, rx,ry, yaw) f32, empty slots zero),
#                           the Elimination start points of the two teams (only the EM maps also have SafeZone meshes)
#   <map>/SafeZone_out_Area.nif  floor polygons of the zones that lock (the outer areas; the middle stays open)
#   <map>/SafeZone_out.nif       the same areas + small upright quads across the zone entrances (the lock barriers)
#   ui/map/EM_<name>.dds         radar image of the mode (same framing as the map's TDM radar image, checked: dice 1.0)
# Maps: urban, port, ghost_storage (the three maps that carry SafeZone meshes and an EM_ radar image).
#
# GELM v2, little endian
#   char magic[4] "GELM"; u32 version = 2
#   u32 nsp[2]; u32 nzone; u32 nbar
#   spawn sp[0][nsp0], sp[1][nsp1]: f32 x, y, z, yaw   (engine yaw: forward = (cos, sin); = SSF yaw + pi)
#   zone[nzone]: f32 bmin[3], bmax[3]; u32 nv, nt; f32 v[nv][3]; u16 t[nt][3]; u16 pad if nt*3 is odd
#   barrier[nbar]: f32 p[4][3]   (upright quads, corners in order)
#   u32 nsafe; f32 safe[nsafe][3]  walkable points outside every lockable zone (nav mesh triangle centres, spread out):
#                                  where bots go when the zones are about to lock
import sys, os, struct, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..')); sys.path.insert(0, os.path.join(HERE, '..', 'minimap'))
import export_col, gcol, gitspaths
RAW = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DEMX')
OUT = sys.argv[2] if len(sys.argv) > 2 else gitspaths.root('DATA')
UIMAP = gitspaths.dirp('UI', 'ui', 'map')
BMAPS = gitspaths.root('BMAPS')
MAPS = {'urban': ('Fix_Urban', 'EM_urbancity'), 'port': ('Fix_Port', 'EM_Port'), 'ghost_storage': ('Fix_Ghost_Storage', 'EM_01')}

def ssf_lists(path):
    d = open(path, 'rb').read(); out = []
    for base in (0x0c, 0x208):   # (a 4-byte field sits between the two lists)
        L = [struct.unpack_from('<6f', d, base + 24 * j) for j in range(21)]
        out.append([r for r in L if any(abs(v) > 1e-6 for v in r)])
    return out
def floor_z(V, I, x, y, z, up=60, down=200):
    T = V[I]; a, b, c = T[:, 0], T[:, 1], T[:, 2]
    d = (b[:, 0] - a[:, 0]) * (c[:, 1] - a[:, 1]) - (b[:, 1] - a[:, 1]) * (c[:, 0] - a[:, 0]); ok = np.abs(d) > 1e-6
    u = np.where(ok, ((x - a[:, 0]) * (c[:, 1] - a[:, 1]) - (y - a[:, 1]) * (c[:, 0] - a[:, 0])) / np.where(ok, d, 1), -1)
    w = np.where(ok, ((b[:, 0] - a[:, 0]) * (y - a[:, 1]) - (b[:, 1] - a[:, 1]) * (x - a[:, 0])) / np.where(ok, d, 1), -1)
    ins = ok & (u >= 0) & (w >= 0) & (u + w <= 1); zz = a[:, 2] + u * (b[:, 2] - a[:, 2]) + w * (c[:, 2] - a[:, 2])
    sel = ins & (zz > z - down) & (zz < z + up)
    return float(zz[sel].max()) if sel.any() else None
def load_nav(path):
    d = open(path, 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    return np.frombuffer(d, dtype=[('v', '<f4', (3, 3)), ('nb', '<i4', 3)], count=n, offset=8)['v'].copy()
def weld(P, I):
    key = np.round(P, 1); uq, inv = np.unique(key, axis=0, return_inverse=True)
    T = inv.reshape(-1)[I.reshape(-1, 3)]; T = T[(T[:, 0] != T[:, 1]) & (T[:, 1] != T[:, 2]) & (T[:, 0] != T[:, 2])]
    return uq.astype(np.float32), T.astype(np.int64)
def in_zones(zones, x, y, z):
    for P, T in zones:
        for t in T:
            a, b, c = P[t]
            d = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if abs(d) < 1e-6: continue
            u = ((x - a[0]) * (c[1] - a[1]) - (y - a[1]) * (c[0] - a[0])) / d; w = ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])) / d
            if u >= 0 and w >= 0 and u + w <= 1 and abs(z - (a[2] + u * (b[2] - a[2]) + w * (c[2] - a[2]))) < 300: return True
    return False
def area2d(P, T):
    A = P[T]; e1 = A[:, 1, :2] - A[:, 0, :2]; e2 = A[:, 2, :2] - A[:, 0, :2]
    return 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]).sum()
def gmm(img, stem, aff_line):
    """<stem>_em.gmm from the EM radar image with the TDM image's world->texel affine (same framing)"""
    import etcpak
    from PIL import Image
    a, b, c, d, e, f = aff_line
    im = Image.open(UIMAP + img + '.dds').convert('RGBA').resize((512, 512), Image.LANCZOS); W, H = im.size; k = W / 600.0
    aff = [a * k, b * k, c * k, d * k, e * k, f * k]; mips = []; cur = im; w, h = W, H
    while w >= 64:
        px = np.asarray(cur, np.uint8).copy(); px[..., :3] = (px[..., :3].astype(np.float32) * px[..., 3:4] / 255).astype(np.uint8)
        mips.append(etcpak.compress_to_dxt5(np.ascontiguousarray(px).tobytes(), w, h)); w //= 2; h //= 2; cur = cur.resize((w, h), Image.BOX)
    p = os.path.join(OUT, stem + '_em.gmm')
    with open(p, 'wb') as fo:
        fo.write(b'GMM1' + struct.pack('<III', W, H, len(mips)) + struct.pack('<6f', *aff))
        for m in mips: fo.write(struct.pack('<I', len(m))); fo.write(m)
    return os.path.getsize(p)
def build(stem, fx, emimg, log, affs):
    root = RAW + '/map/FixedMap/' + fx
    lists = ssf_lists(root + '/SpawnSystem.ssf')
    V, I, S, Mt = gcol.load('%s/%s.gcol' % (BMAPS, stem)); I = I.astype(np.int64)
    zones = []
    for n, P, Ix in export_col.run(root + '/SafeZone_out_Area.nif'):
        Pw, T = weld(P, Ix); zones.append((Pw, T))
    bars = []
    for n, P, Ix in export_col.run(root + '/SafeZone_out.nif'):
        if len(P) != 4: continue
        # order the four corners around the quad: bottom pair then top pair, consistent winding
        P = P[np.argsort(P[:, 2])]; lo, hi = P[:2], P[2:]
        if np.linalg.norm(lo[0, :2] - hi[0, :2]) > np.linalg.norm(lo[0, :2] - hi[1, :2]): hi = hi[::-1]
        bars.append(np.array([lo[0], lo[1], hi[1], hi[0]], np.float32))
    nav = load_nav('%s/%s.gnav' % (BMAPS, stem)); cen = nav.mean(1)
    ok = np.array([not in_zones(zones, c[0], c[1], c[2]) for c in cen])
    area = 0.5 * np.linalg.norm(np.cross(nav[:, 1] - nav[:, 0], nav[:, 2] - nav[:, 0]), axis=1)
    cand = cen[ok & (area > 2000)]                     # skip slivers (< 0.2 m2)
    safe = [cand[np.argmin(np.linalg.norm(cand[:, :2] - cand[:, :2].mean(0), axis=1))]]   # farthest-point sampling from the middle
    dmin = np.linalg.norm(cand - safe[0], axis=1)
    while len(safe) < min(64, len(cand)):
        k = int(np.argmax(dmin)); safe.append(cand[k]); dmin = np.minimum(dmin, np.linalg.norm(cand - cand[k], axis=1))
    safe = np.array(safe, np.float32)
    sp = []
    for L in lists:
        out = []
        for s in L:
            fz = floor_z(V, I, s[0], s[1], s[2]); z = fz if fz is not None else s[2]
            yaw = (s[5] + np.pi + np.pi) % (2 * np.pi) - np.pi
            out.append((s[0], s[1], z, yaw, fz is None, in_zones(zones, s[0], s[1], z)))
        sp.append(out)
    with open(os.path.join(OUT, stem + '.gelim'), 'wb') as f:
        f.write(b'GELM' + struct.pack('<I', 2) + struct.pack('<4I', len(sp[0]), len(sp[1]), len(zones), len(bars)))
        for L in sp:
            for x, y, z, yaw, _, _ in L: f.write(struct.pack('<4f', x, y, z, yaw))
        for P, T in zones:
            f.write(struct.pack('<6f', *P.min(0), *P.max(0))); f.write(struct.pack('<II', len(P), len(T)))
            f.write(P.astype('<f4').tobytes()); f.write(T.astype('<u2').tobytes())
            if (len(T) * 3) % 2: f.write(b'\0\0')
        for q in bars: f.write(q.astype('<f4').tobytes())
        f.write(struct.pack('<I', len(safe))); f.write(safe.astype('<f4').tobytes())
    gm = gmm(emimg, stem, affs[stem])
    log.append('[%s]  (%s: SpawnSystem.ssf lists 0x0c / 0x208, SafeZone_out_Area.nif, SafeZone_out.nif; radar %s -> %s_em.gmm %d bytes)' % (stem, fx, emimg, stem, gm))
    for k, L in enumerate(sp):
        log.append('  team list %d: %d spawns (off-floor %d, inside a lockable zone %d): ' % (k, len(L), sum(p[4] for p in L), sum(p[5] for p in L)) +
                   ' '.join('(%.0f,%.0f,%.0f y%.2f)' % p[:4] for p in L))
    for k, (P, T) in enumerate(zones):
        log.append('  zone %d: %d tris, area %.0f m2, box (%.0f %.0f %.0f)-(%.0f %.0f %.0f)' % (k, len(T), area2d(P, T) / 1e4, *P.min(0), *P.max(0)))
    log.append('  %d safe points (of %d nav triangles outside the zones)' % (len(safe), int(ok.sum())))
    log.append('  %d barriers: ' % len(bars) + ' '.join('(%.0f,%.0f)-(%.0f,%.0f) z%.0f..%.0f' % (q[0, 0], q[0, 1], q[1, 0], q[1, 1], q[0, 2], q[2, 2]) for q in bars))
    return os.path.getsize(os.path.join(OUT, stem + '.gelim'))
if __name__ == '__main__':
    affs = {}
    for line in open(gitspaths.path('MINIMAP', 'minimap.txt')):
        p = line.split()
        if len(p) >= 8 and not line.startswith('#'): affs[p[0]] = list(map(float, p[2:8]))
    log = ['# Elimination spawns, lockable zones and barriers (tools/elim/make_gelim.py); spawn yaw = engine convention (SSF yaw + pi)']
    for st, (fx, em) in MAPS.items():
        n = build(st, fx, em, log, affs); print(st, n, 'bytes')
    open(os.path.join(OUT, 'elim_maps.txt'), 'w').write('\n'.join(log) + '\n'); print('\n'.join(log))
