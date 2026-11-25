# Finds where a footage frame was taken in one of our maps: coarse search over points sampled on the navigation mesh
# (4 cube faces rendered per point, reprojected to 24 yaws x 3 pitches, compared on edge images), then a local
# refinement with real renders. Colours are ignored (grading / lighting differ), only edges count; HUD and the
# first-person weapon areas are masked out.
# usage: locate.py STEM OUT.json FRAME.jpg [FRAME.jpg ...]
import sys, os, math, json, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render import Renderer, persp, view
from PIL import Image
MAPS = '/root/gits/build/maps'
TW, TH = 96, 54; S = 128; VFOV = 59.0; SPACING = float(os.environ.get('LOC_SPACING', 240))
def mask(W, H):
    m = np.ones((H, W), bool); y, x = np.mgrid[0:H, 0:W] / np.array([H, W])[:, None, None]
    m[y > 0.80] = 0; m[(x > 0.52) & (y > 0.42)] = 0; m[(x < 0.20) & (y < 0.32)] = 0; m[y < 0.07] = 0
    m[(abs(x - 0.5) < 0.04) & (abs(y - 0.5) < 0.06)] = 0
    return m
def edges(g):
    g = g.astype(np.float32); gx = np.zeros_like(g); gy = np.zeros_like(g)
    gx[:, 1:-1] = g[:, 2:] - g[:, :-2]; gy[1:-1] = g[2:] - g[:-2]
    e = np.sqrt(gx * gx + gy * gy)
    e = (e + np.roll(e, 1, 0) + np.roll(e, -1, 0) + np.roll(e, 1, 1) + np.roll(e, -1, 1)) / 5   # a little blur
    return np.sqrt(e)   # compress strong edges
def norm(f, m):
    v = f[..., m]; v = v - v.mean(-1, keepdims=True); return v / (np.linalg.norm(v, axis=-1, keepdims=True) + 1e-6)
def luma(a): return a[..., 0] * 0.3 + a[..., 1] * 0.59 + a[..., 2] * 0.11
def frame_feat(path, W, H):
    im = Image.open(path).convert('RGB').resize((W, H), Image.BOX); return edges(luma(np.asarray(im).astype(np.float32)))
def nav_points(stem, spacing=280.0):
    d = open('%s/%s.gnav' % (MAPS, stem), 'rb').read(); n = struct.unpack_from('<I', d, 4)[0]; pts = []
    for i in range(n):
        v = np.frombuffer(d, '<f4', 9, 8 + i * 48).reshape(3, 3)
        a = 0.5 * abs(np.cross(v[1] - v[0], v[2] - v[0])[2]); k = max(1, int(round(a / spacing ** 2)))
        rng = np.random.default_rng(i)
        for _ in range(k):
            r1, r2 = rng.random(2)
            if r1 + r2 > 1: r1, r2 = 1 - r1, 1 - r2
            pts.append(v[0] + r1 * (v[1] - v[0]) + r2 * (v[2] - v[0]))
    P = np.array(pts); keep = []   # thin out to the spacing
    grid = {}
    for i, p in enumerate(P):
        key = (int(p[0] // spacing), int(p[1] // spacing), int(p[2] // 200))
        if key not in grid: grid[key] = i; keep.append(i)
    return P[keep]
def render_cam(R, pos, yaw, pitch, P):
    ctx = R.ctx; R.fbo.use(); ctx.viewport = (0, 0, R.W * R.ss, R.H * R.ss); R.fbo.clear(*R.fog[:3], 1.0, depth=1.0)
    import moderngl
    ctx.enable(moderngl.DEPTH_TEST); ctx.depth_func = '<='; ctx.front_face = 'ccw'; ctx.cull_face = 'back'
    V = view(np.array(pos, float), yaw, pitch); R.draw(P @ V, True, cam=np.array(pos, float), P=P, Vm=V)
    a = np.frombuffer(R.fbo.read(components=3), np.uint8).reshape(R.H * R.ss, R.W * R.ss, 3)[::-1]
    return a
def lookups(yaws, pitches):
    hf = math.tan(math.radians(VFOV) / 2) * TW / TH; vf = math.tan(math.radians(VFOV) / 2)
    xs = (np.arange(TW) + 0.5) / TW * 2 - 1; ys = 1 - (np.arange(TH) + 0.5) / TH * 2
    X, Y = np.meshgrid(xs * hf, ys * vf); L = []
    for yw in yaws:
        for pt in pitches:
            cp, sp, cy, sy = math.cos(pt), math.sin(pt), math.cos(yw), math.sin(yw)
            F = np.array([cy * cp, sy * cp, sp]); Rr = np.array([sy, -cy, 0.0]); U = np.cross(Rr, F)
            D = F[None, None] + X[..., None] * Rr + Y[..., None] * U
            ang = np.arctan2(D[..., 1], D[..., 0]); face = np.round(ang / (math.pi / 2)).astype(int) % 4
            fy = face * math.pi / 2; Fk = np.stack([np.cos(fy), np.sin(fy), np.zeros_like(fy)], -1); Rk = np.stack([np.sin(fy), -np.cos(fy), np.zeros_like(fy)], -1)
            dF = (D * Fk).sum(-1); u = (D * Rk).sum(-1) / dF; v = D[..., 2] / dF
            col = np.clip(((u + 1) / 2 * S).astype(int), 0, S - 1); row = np.clip(((1 - v) / 2 * S).astype(int), 0, S - 1)
            L.append((face, row, col))
    return L
def coarse(stem, frames, R, topk=8):
    m = mask(TW, TH); FF = np.stack([norm(frame_feat(f, TW, TH), m) for f in frames])
    pts = nav_points(stem, SPACING); yaws = [math.radians(a) for a in range(0, 360, 8)]; pitches = [math.radians(a) for a in (-14, -6, 2)]
    LK = lookups(yaws, pitches); Pc = persp(math.radians(90), 1.0, 10, 12000)
    best = [[] for _ in frames]
    for pi_, p in enumerate(pts):
        eye = (p[0], p[1], p[2] + 152.6)
        faces = np.stack([luma(render_cam(R, eye, k * math.pi / 2, 0, Pc).astype(np.float32)) for k in range(4)])
        V = np.stack([edges(faces[f, r, c]) for f, r, c in LK]); Vn = norm(V, m)
        sc = Vn @ FF.T   # views x frames
        for j in range(len(frames)):
            for vi in np.argsort(-sc[:, j])[:2]:
                best[j].append((float(sc[vi, j]), eye, yaws[vi // len(pitches)], pitches[vi % len(pitches)]))
            best[j] = sorted(best[j], key=lambda b: -b[0])[:topk * 3]
    out = []
    for b in best:   # distinct places
        sel = []
        for c in b:
            if all(np.hypot(c[1][0] - s[1][0], c[1][1] - s[1][1]) > 300 or abs(math.remainder(c[2] - s[2], 2 * math.pi)) > 0.6 for s in sel): sel.append(c)
            if len(sel) == topk: break
        out.append(sel)
    return out, len(pts)
def lowl(g, W, H):   # 32x18 luma, locally normalised
    im = Image.fromarray(np.clip(g, 0, 255).astype(np.uint8)).resize((32, 18), Image.BOX); return np.asarray(im).astype(np.float32)
def refine(R2, frame, cand, iters=3):
    W, H = R2.W, R2.H; m = mask(W, H); m2 = mask(32, 18); fr = luma(np.asarray(Image.open(frame).convert('RGB').resize((W, H), Image.BOX)).astype(np.float32))
    f = norm(edges(fr), m); fl = norm(lowl(fr, W, H), m2); P = persp(math.radians(VFOV), W / H, 10, 12000)
    def score(x):
        g = luma(render_cam(R2, x[:3], x[3], x[4], P).astype(np.float32)); return float(norm(edges(g), m) @ f) * 0.7 + 0.3 * float(norm(lowl(g, W, H), m2) @ fl)
    x = np.array([cand[1][0], cand[1][1], cand[1][2], cand[2], cand[3]], float); s = score(x)
    steps = [80.0, 80.0, 25.0, math.radians(3), math.radians(3)]
    for it in range(iters):
        for k in range(5):
            for sgn in (-1, 1, -2, 2):
                y = x.copy(); y[k] += sgn * steps[k]; t = score(y)
                if t > s: s, x = t, y
        steps = [v * 0.5 for v in steps]
    return s, x
if __name__ == '__main__':
    stem, outp, frames = sys.argv[1], sys.argv[2], sys.argv[3:]
    gs = '%s/%s.gsky' % (MAPS, stem)
    R = Renderer(W=S, H=S, ss=1); R.load(stem, lut=False, gsky=None)
    cands, npts = coarse(stem, frames, R)
    R2 = Renderer(W=224, H=126, ss=1); R2.load(stem, lut=False, gsky=None)
    res = {}
    for f, cl in zip(frames, cands):
        rr = sorted([refine(R2, f, c) for c in cl[:8]], key=lambda r: -r[0])
        s, x = rr[0]; res[f] = dict(score=s, pos=[float(v) for v in x[:3]], yaw=math.degrees(x[3]), pitch=math.degrees(x[4]), coarse=cl[0][0],
                                     alts=[dict(score=r[0], pos=[float(v) for v in r[1][:3]], yaw=math.degrees(r[1][3]), pitch=math.degrees(r[1][4])) for r in rr[1:]])
        print('%s  %.3f  pos %.0f %.0f %.0f yaw %.1f pitch %.1f  (2nd %.3f)' % (os.path.basename(f), s, *x[:3], math.degrees(x[3]), math.degrees(x[4]), rr[1][0] if len(rr) > 1 else 0), flush=True)
    json.dump(res, open(outp, 'w'), indent=1); print(npts, 'points searched')
