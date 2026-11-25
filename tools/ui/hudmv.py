# HUD movies (InGameUI.swf / InGameUI_D.swf) laid out for the Vita screen, with helpers that render any
# display-list subtree flat in its own local plane and give the local->screen homography (Flash 3D
# perspective, fov 50) of any node. The engine draws every part as one perspective-correct quad.
import numpy as np, swf, render3, as3d, abc3d, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
from render import CX0, cxcompose
K = 0.6                                  # Vita pixels per stage unit (stage 1600 x 906.7)
VW, VH = 960, 544
SW, SH = VW / K, VH / K
SOUT = np.diag([K, K, 1.0])
def set_stage(sw, sh, k):
    global SW, SH, K, SOUT, VW, VH
    SW, SH, K = sw, sh, k; SOUT = np.diag([K, K, 1.0]); VW, VH = int(round(sw * k)), int(round(sh * k))
    for mv in _cache.values(): mv.R0 = mv.renderer()
FONT_SWF = gitspaths.path('UI2', 'ui', 'GFx_DA', 'Data', 'Localization', 'fonts_en.swf')
_F = swf.load(FONT_SWF)
FONT = [v for v in swf.characters(_F).values() if isinstance(v, dict) and v.get('kind') == 'font'][0]
OVR = render3.OVR
def ovr(path, dx=0, dy=0, sx=1, sy=None, dz=0):
    """layout fix: move a child by (dx,dy,dz) in its parent's coords and scale it about its own origin"""
    sy = sx if sy is None else sy
    OVR[path] = (np.array([[1, 0, 0, dx], [0, 1, 0, dy], [0, 0, 1, dz], [0, 0, 0, 1]], np.float64), np.diag([sx, sy, 1, 1.0]))
# Legibility (the Vita's 5" screen): whole widgets (root items) scaled about their screen anchor, and single children
# grown about their own visual centre (grow); the perspective planes come along, so the look stays the original's.
ROOT_SCALE = {}
ROOT_OFF = {}    # b93: whole widgets moved (stage units, after the anchoring), e.g. into the screen corner as the PC draws them
def child_centre(mv, path):
    """centre of a child's drawn bounds in its parent's coordinates"""
    parent, nm = path.rsplit('.', 1); pcid = mv.node_world(parent)[0]
    cm = mv.R0.child_matrix(pcid, mv.find_child(pcid, nm)); old = OVR.pop(path, None); loc = mv.render_flat(path, 1.0)[1]
    if old is not None: OVR[path] = old
    return (cm @ np.array([(loc[0] + loc[2]) / 2, (loc[1] + loc[3]) / 2, 0, 1.0]))[:2]
def screen_pt(mv, parent, xy):
    """parent-plane point -> Vita screen pixels"""
    p = mv.H(parent) @ np.array([xy[0], xy[1], 1.0]); return p[:2] / p[2]
def plane_pt(mv, parent, sxy):
    """Vita screen pixels -> the parent plane's point under it"""
    p = np.linalg.solve(mv.H(parent), np.array([sxy[0], sxy[1], 1.0])); return p[:2] / p[2]
def screen_centre(mv, path):
    """centre of a child's drawn bounds on the Vita screen"""
    return screen_pt(mv, path.rsplit('.', 1)[0], child_centre(mv, path))
def place_root(mv, name, path, at):
    """b93: move a root widget (ROOT_OFF) so the centre of the child at path lands on screen point at"""
    ROOT_OFF[name] = (0.0, 0.0); c = screen_centre(mv, path)
    ROOT_OFF[name] = ((at[0] - c[0]) / K, (at[1] - c[1]) / K)
def move_group(mv, parent, names, s, ref, at):
    """b93: grow several children of parent by s about the centre of child ref and move them so that centre lands on screen
    point at (the plane's perspective kept)"""
    c = child_centre(mv, parent + '.' + ref); t = plane_pt(mv, parent, at)
    for n in names:
        grow(mv, parent + '.' + n, s, ppivot=c); T, S = OVR[parent + '.' + n]; T = T.copy(); T[0, 3] += t[0] - c[0]; T[1, 3] += t[1] - c[1]
        OVR[parent + '.' + n] = (T, S)
def grow_group(mv, parent, names, s, ref):
    """grow several children of parent by s about one point: the centre of child ref (parent coordinates)"""
    c = child_centre(mv, parent + '.' + ref)
    for n in names: grow(mv, parent + '.' + n, s, ppivot=c)
def grow(mv, path, s, pivot=None, ppivot=None, anchor='c'):
    """scale the child at path by s about the centre of its drawn bounds (anchor 'r' / 'l': the middle of its right / left
    edge), or about a local / parent-coordinate pivot, keeping that point in place"""
    parent, nm = path.rsplit('.', 1)
    pcid = mv.node_world(parent)[0]
    p = mv.find_child(pcid, nm); cm = mv.R0.child_matrix(pcid, p)
    if ppivot is not None:
        A = cm[:2, :2]; pivot = np.linalg.solve(A, np.asarray(ppivot, np.float64) - cm[:2, 3])
    if pivot is None:
        old = OVR.pop(path, None); loc = mv.render_flat(path, 1.0)[1]
        if old is not None: OVR[path] = old
        if loc is None: return
        pivot = (loc[2] if anchor == 'r' else loc[0] if anchor == 'l' else (loc[0] + loc[2]) / 2, (loc[1] + loc[3]) / 2)
    c = np.array([pivot[0], pivot[1], 0, 0.0]); d = cm @ (c * (1 - s))
    OVR[path] = (np.array([[1, 0, 0, d[0]], [0, 1, 0, d[1]], [0, 0, 1, d[2]], [0, 0, 0, 1]], np.float64), np.diag([s, s, 1, 1.0]))
class Movie:
    def __init__(s, path, scripts=None, xs=None):
        s.S = swf.load(path); s.ch = swf.characters(s.S); s.sym = swf.symbols(s.S)
        # databuilder: scripts = the SWF itself -> its Matrix3D tables straight from the bytecode (abc3d.py, same numbers as
        # as3d.py from JPEXS's decompiled .as files, which a folder still selects)
        s.m3 = (abc3d.load(scripts) if scripts.lower().endswith('.swf') else as3d.load(scripts)) if scripts else {}
        s.ROOT, _ = swf.frames(s.S['tags']); s.xs = xs or {}
        s.R0 = s.renderer()
    def renderer(s, hide=None, frames=None, texts=None):
        R = render3.R3(s.ch, FONT, s.sym, s.m3, hide=hide, center=(SW / 2, SH / 2), stage_w=1600)
        R.frame_of = frames or {}; R.text_of = texts or {}; R.out_scale_m = SOUT.copy(); return R
    def root_item(s, name):
        for dep in sorted(s.ROOT[0]):
            p = s.ROOT[0][dep]
            if p.get('name') == name:
                m = list(p.get('m', (1, 0, 0, 1, 0, 0)))
                if name in s.xs: m[4] = s.xs[name](SW, SH)
                elif abs(m[4] - 800) < 1: m[4] += (SW - 1600) / 2   # centred
                elif m[4] > 1100: m[4] += SW - 1600                 # right-anchored
                if abs(m[5] - 450) < 1: m[5] += (SH - 900) / 2      # centred
                elif m[5] > 450: m[5] += SH - 900                   # bottom-anchored
                if name in ROOT_OFF: m[4] += ROOT_OFF[name][0]; m[5] += ROOT_OFF[name][1]   # b93: moved (stage units)
                s = ROOT_SCALE.get(name, 1.0)
                return p['id'], render3.m2(m) @ np.diag([s, s, s, 1.0])
        raise KeyError(name)
    def find_child(s, cid, nm, frame=0):
        c = s.ch[cid]; dl = c['frames'][min(frame, len(c['frames']) - 1)]
        for d in sorted(dl):
            p = dl[d]
            if (p.get('name') or 'd%d' % d) == nm: return p
        raise KeyError((cid, nm))
    def children(s, path):
        cid = s.node_world(path)[0]; c = s.ch[cid]
        if c['kind'] != 'sprite': return []
        dl = c['frames'][0]; return [((dl[d].get('name') or 'd%d' % d), dl[d].get('id')) for d in sorted(dl)]
    def node_world(s, path, frames=None):
        """-> (cid, W 4x4 in stage units, colour transform) of the node at a display path"""
        frames = frames or {}
        parts = path.split('.'); cx = CX0; cur = parts[0]
        if parts[0].startswith('#'): cid, W = int(parts[0][1:]), np.eye(4)      # a library symbol by id, at the origin
        else: cid, W = s.root_item(parts[0])
        for nm in parts[1:]:
            p = s.find_child(cid, nm, frames.get(cur, 0)); cm = s.R0.child_matrix(cid, p)
            nxt = cur + '.' + nm
            W = W @ (OVR[nxt][0] @ cm @ OVR[nxt][1] if nxt in OVR else cm)
            if 'cx' in p: cx = cxcompose(cx, p['cx'])
            cid = p.get('id'); cur = nxt
        return cid, W, cx
    def H(s, path, frames=None):
        W = s.node_world(path, frames)[1]
        return SOUT @ (s.R0.P @ W)[:, [0, 1, 3]]
    def render_flat(s, path, scale, hide=None, frames=None, texts=None, cx=None, pad=3, bounds=None):
        """render a node subtree in its own local coordinates -> (premultiplied float RGBA, (lx0,ly0,lx1,ly1))"""
        cid, _, cxw = s.node_world(path, frames)
        cx = cxw if cx is None else cx
        R = s.renderer(hide, frames, texts)
        R.P = np.array([[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]], np.float64)
        if bounds is not None:   # fixed local rect, pixel-exact: w x h image
            lx0, ly0, lx1, ly1, w, h = bounds
            R.out_scale_m = np.array([[w / (lx1 - lx0), 0, -lx0 * w / (lx1 - lx0)], [0, h / (ly1 - ly0), -ly0 * h / (ly1 - ly0)], [0, 0, 1]])
            cv = render3.Canvas(w, h); R.draw(cid, np.eye(4), cx, cv, path); return cv.px, (lx0, ly0, lx1, ly1)
        sc0 = 0.25; N = 2048
        R.out_scale_m = np.array([[sc0, 0, N / 2], [0, sc0, N / 2], [0, 0, 1]])
        cv = render3.Canvas(N, N); R.draw(cid, np.eye(4), cx, cv, path)
        a = cv.px[..., 3]; ys, xs = np.nonzero(a > 1e-3)
        if len(xs) == 0: return None, None
        lx0 = (xs.min() - N / 2) / sc0 - 4; lx1 = (xs.max() + 1 - N / 2) / sc0 + 4
        ly0 = (ys.min() - N / 2) / sc0 - 4; ly1 = (ys.max() + 1 - N / 2) / sc0 + 4
        w = int(np.ceil((lx1 - lx0) * scale)) + 2 * pad; h = int(np.ceil((ly1 - ly0) * scale)) + 2 * pad
        R.out_scale_m = np.array([[scale, 0, -lx0 * scale + pad], [0, scale, -ly0 * scale + pad], [0, 0, 1]])
        cv = render3.Canvas(w, h); R.draw(cid, np.eye(4), cx, cv, path)
        img = cv.px; a = img[..., 3]; ys, xs = np.nonzero(a > 0.5 / 255)
        if len(xs) == 0: return None, None
        x0, x1, y0, y1 = max(xs.min() - 1, 0), min(xs.max() + 2, w), max(ys.min() - 1, 0), min(ys.max() + 2, h)
        img = img[y0:y1, x0:x1]
        loc = (lx0 + (x0 - pad) / scale, ly0 + (y0 - pad) / scale, lx0 + (x1 - pad) / scale, ly0 + (y1 - pad) / scale)
        return img, loc
    def draw_screen(s, names, hide=None, frames=None, texts=None, scale=K):
        """direct perspective render of whole root items at a given scale (for checks)"""
        R = s.renderer(hide, frames, texts); R.out_scale_m = np.diag([scale, scale, 1.0])
        cv = render3.Canvas(int(round(SW * scale)), int(round(SH * scale)))
        for n in names:
            cid, M = s.root_item(n); R.draw(cid, M, CX0, cv, n)
        return cv.px
def proj(H, x, y):
    v = H @ np.array([x, y, 1.0]); return v[:2] / v[2]
def jac_scale(H, loc):
    best = 0
    for x, y in ((loc[0], loc[1]), (loc[2], loc[1]), (loc[2], loc[3]), (loc[0], loc[3]), ((loc[0] + loc[2]) / 2, (loc[1] + loc[3]) / 2)):
        a = proj(H, x, y); b = proj(H, x + 0.5, y); c = proj(H, x, y + 0.5)
        best = max(best, np.linalg.norm(b - a) / 0.5, np.linalg.norm(c - a) / 0.5)
    return best
def warp(img, loc, H, canvas, tint=None):
    """composite a flat premultiplied image onto a screen canvas through homography H (bilinear) - the engine's draw, in numpy"""
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
    if tint is not None: c = c * np.array(tint, np.float32)
    d = canvas[by0:by1, bx0:bx1]; d[:] = c + d * (1 - c[..., 3:4])
XS_MAIN = {'killrecord': lambda W, H: W - 6, 'watchList': lambda W, H: W - 17, 'hpapMC': lambda W, H: W - 8, 'tkHPMC': lambda W, H: W - 83,
           'model': lambda W, H: W - 36, 'tutMIMC': lambda W, H: W - 12, 'minimap': lambda W, H: 26, 'donut': lambda W, H: 19, 'intelMC': lambda W, H: 10,
           'transfer': lambda W, H: 240, 'rmMC': lambda W, H: W // 2 - 18, 'tkskillMC': lambda W, H: 85, 'tccomaMC': lambda W, H: W // 2}
_cache = {}
def main():
    if 'main' not in _cache: p = gitspaths.path('UI', 'ui', 'GFx_DA', 'InGameUI.swf'); _cache['main'] = Movie(p, p, XS_MAIN)
    return _cache['main']
def rmovie():
    if 'r' not in _cache: _cache['r'] = Movie(gitspaths.path('UI2', 'ui', 'GFx_DA', 'ResourceTemplate.swf'))
    return _cache['r']
def dmovie():
    if 'd' not in _cache: p = gitspaths.path('UI', 'ui', 'GFx_DA', 'InGameUI_D.swf'); _cache['d'] = Movie(p, p)
    return _cache['d']
