# HUD part generator: flat renders of SWF display-list subtrees in their own local plane, plus the
# local->screen homography (Flash 3D perspective) of any node, so the engine can draw each part as one
# perspective-correct quad.
import numpy as np, render3, hudlib
from render import CX0, cxcompose
ch, sym, m3, font = hudlib.ch, hudlib.sym, hudlib.m3, hudlib.font
VW, VH = 960, 544
SOUT = np.diag([VW / 1600.0, VH / 900.0, 1.0])
_R = render3.R3(ch, font, sym, m3)
def find_child(cid, nm, frame=0):
    c = ch[cid]; dl = c['frames'][min(frame, len(c['frames']) - 1)]
    for d in sorted(dl):
        p = dl[d]
        if (p.get('name') or 'd%d' % d) == nm: return p
    raise KeyError((cid, nm))
def node_world(path, frames=None):
    """-> (cid, W 4x4 in stage units, colour transform) of the node at a display path"""
    frames = frames or {}
    parts = path.split('.'); cid, W = hudlib.root_item(parts[0]); cx = CX0; cur = parts[0]
    for nm in parts[1:]:
        p = find_child(cid, nm, frames.get(cur, 0)); W = W @ _R.child_matrix(cid, p)
        if cur + '.' + nm in render3.OVR: o = render3.OVR[cur + '.' + nm]; W = W @ np.linalg.inv(_R.child_matrix(cid, p)) @ o[0] @ _R.child_matrix(cid, p) @ o[1]
        if 'cx' in p: cx = cxcompose(cx, p['cx'])
        cid = p['id']; cur += '.' + nm
    return cid, W, cx
def ovr(path, dx=0, dy=0, sx=1, sy=None, dz=0):
    sy = sx if sy is None else sy
    render3.OVR[path] = (np.array([[1, 0, 0, dx], [0, 1, 0, dy], [0, 0, 1, dz], [0, 0, 0, 1]], np.float64), np.diag([sx, sy, 1, 1.0]))
def H_of(W):
    """local plane (x,y,1) -> Vita screen homogeneous"""
    return SOUT @ (_R.P @ W)[:, [0, 1, 3]]
def proj(H, x, y):
    v = H @ np.array([x, y, 1.0]); return v[:2] / v[2]
def jac_scale(H, x, y, e=0.5):
    a = proj(H, x, y); b = proj(H, x + e, y); c = proj(H, x, y + e)
    return max(np.linalg.norm(b - a), np.linalg.norm(c - a)) / e
def flat_renderer(hide=None, frames=None, texts=None):
    R = render3.R3(ch, font, sym, m3, hide=hide)
    R.P = np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]], np.float64)
    R.frame_of = frames or {}; R.text_of = texts or {}
    return R
def render_flat(path, scale, hide=None, frames=None, texts=None, cx=None, pad=3, bounds=None):
    """render node subtree in its own local coordinates. -> (premultiplied float RGBA image, (lx0,ly0,lx1,ly1))"""
    cid, _, cxw = node_world(path, frames)
    cx = cxw if cx is None else cx
    R = flat_renderer(hide, frames, texts)
    if bounds is None:
        sc0 = 0.25; N = 2048
        R.out_scale_m = np.array([[sc0, 0, N / 2], [0, sc0, N / 2], [0, 0, 1]])
        cv = render3.Canvas(N, N); R.draw(cid, np.eye(4), cx, cv, path)
        a = cv.px[..., 3]; ys, xs = np.nonzero(a > 1e-3)
        if len(xs) == 0: return None, None
        lx0 = (xs.min() - N / 2) / sc0 - 4; lx1 = (xs.max() + 1 - N / 2) / sc0 + 4
        ly0 = (ys.min() - N / 2) / sc0 - 4; ly1 = (ys.max() + 1 - N / 2) / sc0 + 4
    else: lx0, ly0, lx1, ly1 = bounds
    w = int(np.ceil((lx1 - lx0) * scale)) + 2 * pad; h = int(np.ceil((ly1 - ly0) * scale)) + 2 * pad
    R.out_scale_m = np.array([[scale, 0, -lx0 * scale + pad], [0, scale, -ly0 * scale + pad], [0, 0, 1]])
    cv = render3.Canvas(w, h); R.draw(cid, np.eye(4), cx, cv, path)
    img = cv.px
    # tight crop
    a = img[..., 3]; ys, xs = np.nonzero(a > 1.0 / 255)
    if len(xs) == 0: return None, None
    x0, x1, y0, y1 = max(xs.min() - 1, 0), min(xs.max() + 2, w), max(ys.min() - 1, 0), min(ys.max() + 2, h)
    img = img[y0:y1, x0:x1]
    loc = (lx0 + (x0 - pad) / scale, ly0 + (y0 - pad) / scale, lx0 + (x1 - pad) / scale, ly0 + (y1 - pad) / scale)
    return img, loc
def only(prefix_keep, base):
    """hide predicate: inside node 'base', show only children whose path starts with one of prefix_keep"""
    keep = [base + '.' + k for k in prefix_keep]
    def h(p):
        if not p.startswith(base + '.'): return False
        return not any(p == k or p.startswith(k + '.') or k.startswith(p + '.') for k in keep)
    return h
def warp_to_screen(img, loc, H, canvas):
    """composite a flat image onto a screen canvas (H x W x 4 premult) with homography (inverse mapping, bilinear)"""
    Hh, Ww = canvas.shape[:2]; x0, y0, x1, y1 = loc; ih, iw = img.shape[:2]
    cs = np.array([proj(H, x, y) for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))])
    bx0, by0 = np.floor(cs.min(0)).astype(int); bx1, by1 = np.ceil(cs.max(0)).astype(int)
    bx0, by0 = max(bx0, 0), max(by0, 0); bx1, by1 = min(bx1, Ww), min(by1, Hh)
    if bx1 <= bx0 or by1 <= by0: return
    ys, xs = np.mgrid[by0:by1, bx0:bx1] + 0.5
    Hi = np.linalg.inv(H); X = Hi[0, 0] * xs + Hi[0, 1] * ys + Hi[0, 2]; Y = Hi[1, 0] * xs + Hi[1, 1] * ys + Hi[1, 2]; Z = Hi[2, 0] * xs + Hi[2, 1] * ys + Hi[2, 2]
    lx, ly = X / Z, Y / Z
    u = (lx - x0) / (x1 - x0) * iw - 0.5; v = (ly - y0) / (y1 - y0) * ih - 0.5
    inside = (u > -1) & (u < iw) & (v > -1) & (v < ih)
    uc = np.clip(u, 0, iw - 1); vc = np.clip(v, 0, ih - 1)
    xa = np.floor(uc).astype(int); ya = np.floor(vc).astype(int); xb = np.minimum(xa + 1, iw - 1); yb = np.minimum(ya + 1, ih - 1)
    fx = (uc - xa)[..., None]; fy = (vc - ya)[..., None]
    c = img[ya, xa] * (1 - fx) * (1 - fy) + img[ya, xb] * fx * (1 - fy) + img[yb, xa] * (1 - fx) * fy + img[yb, xb] * fx * fy
    c = c * inside[..., None]
    d = canvas[by0:by1, bx0:bx1]; d[:] = c + d * (1 - c[..., 3:4])
def bbox_in(path, ancestor, hide=None):
    """bbox of node subtree expressed in an ancestor's local coords (ignores perspective; uses x,y of the 4x4 chain)"""
    img, loc = render_flat(path, 2.0, hide=hide)
    if img is None: return None
    _, Wa, _ = node_world(ancestor); _, Wn, _ = node_world(path)
    M = np.linalg.inv(Wa) @ Wn
    x0, y0, x1, y1 = loc; pts = np.array([[x, y, 0, 1] for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]) @ M.T
    return pts[:, 0].min(), pts[:, 1].min(), pts[:, 0].max(), pts[:, 1].max()
