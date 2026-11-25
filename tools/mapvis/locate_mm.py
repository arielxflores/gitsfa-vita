# Finds where a footage frame was taken from the game's own minimap (top-left HUD): the radar turns with the player
# (facing up) and sits on a tilted panel, so the panel is unwarped (hud.ghud node mm.view homography, scaled to the
# 1280x720 footage) and matched against our copy of the radar image (build/data/minimap/*.png, minimap.txt) over all
# headings -> player x, y, yaw. Then the floor height comes from the navigation mesh and pitch / position are refined
# against a render (edges only). usage: locate_mm.py STEM OUT.json FRAME.jpg [...]
import sys, os, math, json, struct, numpy as np, cv2
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
DATA = os.environ.get('GITS_DATA', '/root/work/render/build/data'); MAPS = '/root/gits/build/maps'
CX, CY, K, VW, VH = 102.0, 85.0, 0.036, 204.0, 170.0    # hud_game.h minimap viewport (plane units), player spot; plane units per cm as in the footage
# (best match on Ghost Storage frames: 0.036; hud_game.h draws 0.07, i.e. twice as zoomed in as the original)
HV = np.array([[0.6865, -0.06153, 24.95], [0.1148, 0.6685, 12.22], [0.0004491, 7.416e-05, 0.9476]])   # hud.ghud mm.view: plane -> 960x544 screen
FIT = json.loads(os.environ.get('MM_FIT', '[1.155, 1.155, -4.0, -2.0]'))   # 960-screen -> footage pixels: sx, sy, tx, ty (calibrated on Ghost Storage frames)
PS = 2.0   # plane pixels per unit
def plane_to_frame():
    A = np.array([[FIT[0], 0, FIT[2]], [0, FIT[1], FIT[3]], [0, 0, 1]]); return A @ HV
def unwarp(frame):
    im = np.asarray(Image.open(frame).convert('RGB')).astype(np.float32)
    S = np.diag([1 / PS, 1 / PS, 1]); M = plane_to_frame() @ S   # plane pixel -> frame pixel
    return cv2.warpPerspective(im, M, (int(VW * PS), int(VH * PS)), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP)
def grad(g):
    g = cv2.GaussianBlur(g, (0, 0), 1.0); gx = cv2.Sobel(g, cv2.CV_32F, 1, 0); gy = cv2.Sobel(g, cv2.CV_32F, 0, 1); return np.sqrt(gx * gx + gy * gy)
def radar(stem):
    for line in open(DATA + '/minimap.txt'):
        p = line.split()
        if p and p[0] == stem and p[1] != 'none':
            im = Image.open(DATA + '/minimap/%s.png' % p[1]).convert('RGBA'); k = im.size[0] / 600.0
            a = np.array(list(map(float, p[2:8]))) * k; return np.asarray(im).astype(np.float32), a.reshape(2, 3)
    return None, None
def locate_xy(stem, frame, yaw_step=2.0):
    rim, aff = radar(stem)
    if rim is None: return None
    F = unwarp(frame); fl = F[..., :3].mean(-1)
    # mask: the player marker / blips (saturated colours) and the viewport border
    sat = F[..., :3].max(-1) - F[..., :3].min(-1); m = (sat < 60).astype(np.float32)
    m[:6] = 0; m[-6:] = 0; m[:, :6] = 0; m[:, -6:] = 0
    cy0, cx0 = int(CY * PS), int(CX * PS); m[cy0 - 22:cy0 + 22, cx0 - 22:cx0 + 22] = 0
    Fg = grad(fl) * m; FF2 = float((Fg * Fg).sum())
    rl = rim[..., :3].mean(-1) * (rim[..., 3] / 255.0)
    Ainv = np.linalg.inv(np.vstack([aff, [0, 0, 1]]))   # image px -> world
    best = (-1, None)
    for yd in np.arange(0, 360, yaw_step):
        th = math.radians(yd); f = np.array([math.cos(th), math.sin(th)]); r = np.array([math.sin(th), -math.cos(th)])
        # world -> Q px:  q = PS*K*(w.r, -(w.f)) + off
        W2Q = PS * K * np.array([[r[0], r[1]], [-f[0], -f[1]]])
        # corners of the radar image in Q space to size the canvas
        cs = np.array([[0, 0, 1], [rim.shape[1], 0, 1], [rim.shape[1], rim.shape[0], 1], [0, rim.shape[0], 1]]) @ Ainv.T
        qc = cs[:, :2] @ W2Q.T; off = -qc.min(0) + 4; size = (int(np.ptp(qc[:, 0]) + 8), int(np.ptp(qc[:, 1]) + 8))
        M = np.hstack([W2Q @ Ainv[:2, :2], (W2Q @ Ainv[:2, 2] + off)[:, None]])   # image px -> Q px
        Q = cv2.warpAffine(rl, M, size, flags=cv2.INTER_LINEAR)
        if Q.shape[0] < Fg.shape[0] or Q.shape[1] < Fg.shape[1]: continue
        Qg = grad(Q); num = cv2.matchTemplate(Qg, Fg, cv2.TM_CCORR); den = cv2.matchTemplate(Qg * Qg, m, cv2.TM_CCORR)
        res = num / np.sqrt(np.maximum(den, 0.25 * FF2) * FF2)
        _, mv, _, ml = cv2.minMaxLoc(res)
        if mv > best[0]:
            # template top-left ml -> the player spot in Q px -> world
            qp = np.array([ml[0] + cx0, ml[1] + cy0]) - off; w = np.linalg.solve(W2Q, qp); best = (mv, (float(w[0]), float(w[1]), yd))
    return best
def nav_floors(stem, x, y):
    d = open('%s/%s.gnav' % (MAPS, stem), 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]
    V = np.frombuffer(d, '<f4', n * 12, 8).reshape(n, 12)[:, :9].reshape(n, 3, 3); zs = []
    for v in V:
        a, b, c = v; den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(den) < 1e-6: continue
        l1 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / den; l2 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / den; l3 = 1 - l1 - l2
        if min(l1, l2, l3) > -0.15: zs.append(float(l1 * a[2] + l2 * b[2] + l3 * c[2]))
    zs = sorted(set(round(z / 50) * 50 for z in zs))
    if not zs:   # nearest triangle
        cen = V.mean(1); i = int(np.argmin(np.hypot(cen[:, 0] - x, cen[:, 1] - y))); zs = [float(cen[i, 2])]
    return zs
def radar_view(stem, x, y, yd):   # what the HUD shows for player (x, y, yaw deg): plane image (PS px / unit)
    rim, aff = radar(stem); th = math.radians(yd); f = np.array([math.cos(th), math.sin(th)]); r = np.array([math.sin(th), -math.cos(th)])
    pv, pu = np.mgrid[0:int(VH * PS), 0:int(VW * PS)] / PS
    d = ((pu - CX) / K)[..., None] * r + ((CY - pv) / K)[..., None] * f; w = d + np.array([x, y])
    u = aff[0, 0] * w[..., 0] + aff[0, 1] * w[..., 1] + aff[0, 2]; v = aff[1, 0] * w[..., 0] + aff[1, 1] * w[..., 1] + aff[1, 2]
    return cv2.remap(rim, u.astype(np.float32), v.astype(np.float32), cv2.INTER_LINEAR)
def local_search(R, frame, x, y, z, yd, rad=320.0, step=80.0, dyaw=27, ystep=3):
    """cube faces at a grid of points around (x, y) at eye height above z, views at yaws within +-dyaw of yd"""
    from locate import mask, norm, frame_feat, luma, edges, render_cam, lookups, persp, TW, TH
    m = mask(TW, TH); f = norm(frame_feat(frame, TW, TH), m)
    yaws = [math.radians(yd + a) for a in range(-dyaw, dyaw + 1, ystep)]; pitches = [math.radians(a) for a in (-16, -8, 0, 8)]
    LK = lookups(yaws, pitches); Pc = persp(math.radians(90), 1.0, 10, 12000); out = []
    for dx in np.arange(-rad, rad + 1, step):
        for dy in np.arange(-rad, rad + 1, step):
            eye = (x + dx, y + dy, z + 152.6)
            faces = np.stack([luma(render_cam(R, eye, k * math.pi / 2, 0, Pc).astype(np.float32)) for k in range(4)])
            V = np.stack([edges(faces[fi, r, c]) for fi, r, c in LK]); sc = norm(V, m) @ f
            for vi in np.argsort(-sc)[:2]: out.append((float(sc[vi]), eye, yaws[vi // len(pitches)], pitches[vi % len(pitches)]))
    return sorted(out, key=lambda o: -o[0])
if __name__ == '__main__':
    from locate import refine, Renderer, S
    stem, outp, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
    R = Renderer(W=S, H=S, ss=1); R.load(stem, lut=False, gsky=None)
    R2 = Renderer(W=224, H=126, ss=1); R2.load(stem, lut=False, gsky=None); res = {}
    if os.environ.get('MM_K'): K = float(os.environ['MM_K'])
    else:   # the radar zoom differs between videos (0.036 in the 3v3 TDM video, ~0.07 in Geofront): pick it on a few frames
        best = None
        for k in (0.030, 0.036, 0.044, 0.054, 0.066, 0.080):
            K = k; sc = np.mean([locate_xy(stem, f, yaw_step=6)[0] for f in frames[1:4]])
            print('radar zoom %.3f: %.3f' % (k, sc), flush=True)
            if best is None or sc > best[0]: best = (sc, k)
        K = best[1]
    for f in frames:
        b = locate_xy(stem, f)
        if not b or b[1] is None: print(os.path.basename(f), 'no radar match'); continue
        sc, (x, y, yd) = b; cands = []
        for z in nav_floors(stem, x, y): cands += local_search(R, f, x, y, z, yd)
        cands = sorted(cands, key=lambda c: -c[0]); sel = []
        for c in cands:
            if all(np.hypot(c[1][0] - q[1][0], c[1][1] - q[1][1]) > 120 or abs(c[2] - q[2]) > 0.15 or abs(c[1][2] - q[1][2]) > 100 for q in sel): sel.append(c)
            if len(sel) == 4: break
        rr = sorted([refine(R2, f, c) for c in sel], key=lambda r: -r[0]); s_, X = rr[0]
        res[f] = dict(score=s_, radar=sc, radar_zoom=K, pos=[float(v) for v in X[:3]], yaw=math.degrees(X[3]), pitch=math.degrees(X[4]), radar_xy=[x, y, float(yd)])
        print('%s radar %.3f (%.0f %.0f yaw %.0f) -> render %.3f pos %.0f %.0f %.0f yaw %.1f pitch %.1f' % (os.path.basename(f), sc, x, y, yd, s_, *X[:3], math.degrees(X[3]), math.degrees(X[4])), flush=True)
        json.dump(res, open(outp, 'w'), indent=1)
