# Registers minimap radar images (ui/map/*.dds) to our maps: world (x,y) cm -> image pixel (u,v)
import numpy as np, sys, cv2, json
sys.path.insert(0, '/root/gits/tools/minimap')
import gcol, topdown
from PIL import Image
from scipy.optimize import minimize
MAPDIR = '/root/gits/ui/ui/map/'
PAIRS = {'downtown': 'dem003', 'berutalube': 'TTM_002', 'ecenter': 'ecenter', 'ghost_storage': 'TDM01', 'headquarter': 'SM001', 'port': 'TDM_Port',
         'pss9': 'DEM001bg', 'ruinednight': 'TTM02bg', 'ruinedtokyo': 'TTM01bg', 'trainstation': 'station', 'underground': 'TTM03bg',
         'urban': 'urbancity', 'zioplant': 'Dem005'}
CLIP = {'urban': 900}
ORI = {'urban': [1]}
ORIENT = []
for sw in (0, 1):
    for sx in (1, -1):
        for sy in (1, -1):
            O = np.array([[sx, 0], [0, sy]], float)
            if sw: O = O[:, ::-1]
            ORIENT.append(O)
def img_mask(name):
    a = np.asarray(Image.open(MAPDIR + name + '.dds').convert('RGBA')).astype(np.float32)
    m = (a[..., 3] > 120).astype(np.float32)
    return m, a
def world_mask(tris, A, size=600):
    """tris (n,3,3); A 2x3 world->pixel. filled mask size x size"""
    P = tris[:, :, :2].reshape(-1, 2) @ A[:, :2].T + A[:, 2]
    P = P.reshape(-1, 3, 2)
    m = np.zeros((size, size), np.uint8)
    cv2.fillPoly(m, [np.round(p * 4).astype(np.int32) for p in P], 1, lineType=cv2.LINE_8, shift=2)
    return m.astype(np.float32)
def dice(a, b): return 2 * (a * b).sum() / (a.sum() + b.sum() + 1e-6)
def search(tris, im, nsc=48, orients=None, srange=(0.55, 1.6)):
    """coarse: orientation x scale x translation (FFT correlation)"""
    P = tris[:, :, :2].reshape(-1, 2); c = (P.min(0) + P.max(0)) / 2; ext = (P.max(0) - P.min(0)).max()
    ys, xs = np.nonzero(im); iext = max(xs.max() - xs.min(), ys.max() - ys.min())
    s0 = iext / ext; best = (-1, None)
    F_im = np.fft.rfft2(im, s=(1200, 1200))
    for oi, O in enumerate(ORIENT):
        if orients is not None and oi not in orients: continue
        for s in s0 * np.exp(np.linspace(np.log(srange[0]), np.log(srange[1]), nsc)):
            A = np.zeros((2, 3)); A[:, :2] = s * O; A[:, 2] = 300 - (s * O) @ c
            wm = world_mask(tris, A, 1200 // 2 * 2)[:600, :600] if False else world_mask(tris, np.c_[s * O, 600 - (s * O) @ c], 1200)
            # correlation: shift that maps wm onto im
            F_w = np.fft.rfft2(wm, s=(1200, 1200))
            cc = np.fft.irfft2(F_im * np.conj(F_w), s=(1200, 1200))
            k = np.argmax(cc); dy, dx = np.unravel_index(k, cc.shape); dy = dy - 1200 if dy > 600 else dy; dx = dx - 1200 if dx > 600 else dx
            score = 2 * cc.flat[k] / (im.sum() + wm.sum() + 1e-6)
            if score > best[0]:
                At = np.c_[s * O, 600 - (s * O) @ c + np.array([dx, dy])]
                best = (score, At, oi, s)
    return best
def refine(tris, im, A, oi):
    O = ORIENT[oi]; ims = cv2.GaussianBlur(im, (0, 0), 2)
    s0 = abs(np.linalg.det(A[:, :2])) ** 0.5; t0 = A[:, 2].copy()
    def f(p):
        s, th, tx, ty = s0 * np.exp(p[0]), p[1], t0[0] + p[2], t0[1] + p[3]
        R = np.array([[np.cos(th), -np.sin(th)], [np.sin(th), np.cos(th)]])
        A2 = np.c_[s * R @ O, [tx, ty]]
        wm = cv2.GaussianBlur(world_mask(tris, A2), (0, 0), 2)
        return -dice(wm, ims)
    r = minimize(f, [0, 0, 0, 0], method='Nelder-Mead', options=dict(xatol=1e-3, fatol=1e-5, maxiter=400, initial_simplex=[[0, 0, 0, 0], [0.03, 0, 0, 0], [0, 0.02, 0, 0], [0, 0, 3, 0], [0, 0, 0, 3]]))
    p = r.x; s, th = s0 * np.exp(p[0]), p[1]; R = np.array([[np.cos(th), -np.sin(th)], [np.sin(th), np.cos(th)]])
    return np.c_[s * R @ O, [t0[0] + p[2], t0[1] + p[3]]], -r.fun, th
if __name__ == '__main__':
    D = topdown.D; res = {}
    stems = sys.argv[1:] or list(PAIRS)
    for st in stems:
        V, I, S, M = gcol.load(D + st + '.gcol'); N = topdown.load_nav(D + st + '.gnav')
        im, rgba = img_mask(PAIRS[st])
        if st in CLIP:   # keep only the play area around the spawn points
            m = CLIP[st]; c = N[:, :, :2].mean(1)
            keep = (c[:, 0] > S['x'].min() - m) & (c[:, 0] < S['x'].max() + m) & (c[:, 1] > S['y'].min() - m) & (c[:, 1] < S['y'].max() + m); N = N[keep]
        sc, A, oi, s = search(N, im, orients=ORI.get(st))
        A2, sc2, th = refine(N, im, A, oi)
        # spawn check
        P = np.c_[S['x'], S['y']] @ A2[:, :2].T + A2[:, 2]
        inside = np.mean([im[int(np.clip(v, 0, 599)), int(np.clip(u, 0, 599))] > 0 for u, v in P])
        print('%-14s %-9s orient %d scale %.5f coarse %.3f refined %.3f rot %.2f deg spawns inside %.2f' % (st, PAIRS[st], oi, s, sc, sc2, np.degrees(th), inside), flush=True)
        res[st] = dict(image=PAIRS[st], A=A2.tolist(), score=float(sc2), coarse=float(sc), inside=float(inside), orient=oi, rot=float(np.degrees(th)))
        json.dump(res, open('/tmp/claude-0/minimap/reg_%s.json' % ('all' if not sys.argv[1:] else '_'.join(stems)), 'w'), indent=1)
