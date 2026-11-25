# Legibility check of the host captures (tools/menu/host): for every capture, the smallest text actually drawn and the touch
# targets. Text: each glyph quad drawn from a font texture (512x512, the packs' second texture) -> its on-screen em size
# (quad height / glyph texels * 44 px, the size the font atlas is rendered at). Targets: <name>.hits (id x0 y0 x1 y1) from
# mu_dump_hits; containers (list drag areas, the window's tap-swallowing background) are skipped.
# usage: check_legibility.py CAPDIR [min_em=20] [min_tap=72] [min_gap=12]
import sys, os, struct, glob, numpy as np
D = sys.argv[1]; MIN_EM = float(sys.argv[2]) if len(sys.argv) > 2 else 20; MIN_TAP = float(sys.argv[3]) if len(sys.argv) > 3 else 72; MIN_GAP = float(sys.argv[4]) if len(sys.argv) > 4 else 12
K_LIST, K_BG = 9, 14     # menu.h MU_K_LIST / MU_K_BG
def fonts(texdir):
    out = set()
    for p in glob.glob(os.path.join(texdir, 'tex*.bin')):
        w, h, fmt, size = struct.unpack_from('<4i', open(p, 'rb').read(16))
        if w == 512 and h == 512 and fmt == 4: out.add(int(os.path.basename(p)[3:-4]))
    return out
bad = 0; rows = []
for cap in sorted(glob.glob(os.path.join(D, '*.cap'))):
    nm = os.path.basename(cap)[:-4]; F = fonts(os.path.join(D, nm + '.tex')); d = open(cap, 'rb').read(); o = 0; ems = []
    while o < len(d):
        tid, prem, n = struct.unpack_from('<3i', d, o); o += 12
        if tid == -1: o += (n + 3) // 4 * 4; continue   # ui b42: an element tag (tools/ui/host/hudtag.h)
        V = np.frombuffer(d, np.float32, n * 17, o).reshape(n, 17); o += n * 68
        if tid not in F: continue
        for t in range(0, n, 6):             # two triangles per glyph quad: vertices 0,1,2 / 0,2,3
            q = V[t:t + 6]; w = q[:, 3]; a = q[:, 11] if q.shape[1] > 11 else None
            if np.any(w <= 0) or (a is not None and a.max() < 0.25): continue    # invisible (faded out) glyphs don't count
            sy = (1 - q[:, 1] / w) * 0.5 * 544; vv = q[:, 5] * 512
            hs, ht = sy.max() - sy.min(), vv.max() - vv.min()
            if ht > 2: ems.append(hs / ht * 44)
    hits = []
    hp = os.path.join(D, nm + '.hits')
    if os.path.exists(hp):
        for l in open(hp):
            i, x0, y0, x1, y1 = map(int, l.split())
            if (i >> 8) in (K_LIST, K_BG): continue
            hits.append((i, x0, y0, x1, y1))
    small = [h for h in hits if h[4] - h[2] < MIN_TAP - 0.5]
    close = []
    for a in range(len(hits)):
        for b in range(a + 1, len(hits)):
            A, B = hits[a], hits[b]
            ox = min(A[3], B[3]) - max(A[1], B[1]); oy = min(A[4], B[4]) - max(A[2], B[2])
            if ox > 0 and oy > 0: continue                       # nested / overlapping (e.g. an arrow inside its row) - not a gap
            gx = max(A[1], B[1]) - min(A[3], B[3]); gy = max(A[2], B[2]) - min(A[4], B[4])
            g = max(gx, gy)
            if g < MIN_GAP - 0.5: close.append((A[0], B[0], g))
    mn = min(ems) if ems else 0; nsm = sum(1 for e in ems if e < MIN_EM - 0.25); nsec = sum(1 for e in ems if e < 24 - 0.25)
    ok = nsm == 0 and not small and not close
    bad += not ok
    rows.append('%-20s glyphs %4d  min em %5.1f (cap %4.1f px)  <%g: %3d  <24: %3d   targets %2d  short %d  close %d  %s' % (nm, len(ems), mn, mn * 0.68, MIN_EM, nsm, nsec, len(hits), len(small), len(close), 'ok' if ok else 'FAIL'))
    for h in small[:4]: rows.append('      short target id %d kind %d: %d x %d at %d,%d' % (h[0], h[0] >> 8, h[3] - h[1], h[4] - h[2], h[1], h[2]))
    for c in close[:4]: rows.append('      targets %d / %d only %d px apart' % c)
print('\n'.join(rows)); print('%d of %d captures fail' % (bad, len(rows) and len(glob.glob(os.path.join(D, '*.cap')))))
