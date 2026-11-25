# Helpers for building the menu packs (.ghud) from the game's Scaleform menus, on top of the HUD tools
# (tools/ui: hudmv.Movie, hudbuild.Builder) which are used read-only.
import sys, re, os, numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ui'))   # read-only HUD tools
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
import hudmv as M, render3
from hudbuild import Builder
from render import CX0
SWF = gitspaths.path('UI2', 'ui', 'GFx_DA', '%s.swf')
AS = None   # databuilder: the menu packs are made without the menus' decompiled scripts (as the shipped menu.ghud was)
_mv = {}
def unhide_alpha(mv):
    """menus rest at animation start states: placements with alpha 0 become visible (hide what must stay hidden)"""
    n = 0
    for c in mv.ch.values():
        if not isinstance(c, dict) or c.get('kind') != 'sprite': continue
        for fr in c['frames']:
            for d, p in fr.items():
                cx = p.get('cx')
                if cx and cx[0][3] < 0.05 and cx[1][3] <= 0:
                    p['cx'] = ([cx[0][0], cx[0][1], cx[0][2], 1.0], list(cx[1])); n += 1
    return n
def movie(name, scripts=False):
    if name not in _mv:
        if name == 'InGameUI': _mv[name] = M.main()
        elif name == 'InGameUI_D': _mv[name] = M.dmovie()
        elif name == 'ResourceTemplate': _mv[name] = M.rmovie()
        else: _mv[name] = M.Movie(SWF % name, (AS % name) if scripts and AS and os.path.isdir(AS % name) else None)
        unhide_alpha(_mv[name])
        for d, pl in _mv[name].ROOT[0].items():      # unnamed root items become addressable as d<depth>
            if not pl.get('name'): pl['name'] = 'd%d' % d
    return _mv[name]
def sym_id(mv, cls):
    for k, v in mv.sym.items():
        if v == cls: return k
    raise KeyError(cls)
def hide_prefix(*prefixes):
    pre = [p for p in prefixes]
    return lambda p: any(p == x or p.startswith(x + '.') for x in pre)
def only(base, *keep):
    """inside base: draw only the listed child subtrees"""
    ks = [base + '.' + k for k in keep]
    def h(p):
        if not p.startswith(base + '.'): return False
        return not any(p == k or p.startswith(k + '.') or k.startswith(p + '.') for k in ks)
    return h
def both(*hs):
    hs = [h for h in hs if h]
    return lambda p: any(h(p) for h in hs)
def text_fields(mv, path, frames=None, out=None, depth=0):
    """all text field paths under a node"""
    out = [] if out is None else out
    try: kids = mv.children(path)
    except Exception: return out
    for nm, cid in kids:
        c = mv.ch.get(cid, {}); p = path + '.' + nm
        if c.get('kind') == 'text': out.append(p)
        elif c.get('kind') == 'sprite' and depth < 12: text_fields(mv, p, frames, out, depth + 1)
    return out
def _vis(cx):
    """menus often rest at an animation start state with alpha 0: show such nodes fully"""
    mul, add = list(cx[0]), list(cx[1])
    if mul[3] < 0.05 and add[3] <= 0: mul[3] = 1.0
    return (mul, add)
class MB(Builder):
    """Builder with frames/texts aware parts and 1:1 density for 2D menus"""
    def mpart(s, name, mv, path, hide=None, frames=None, texts=None, scale=None, node=True, cx_node=None, pad=3):
        cid, W, cxw = mv.node_world(path, frames)
        H = M.SOUT @ (mv.R0.P @ W)[:, [0, 1, 3]]
        if node is True or node == name:
            cx = _vis(cxw) if cx_node is None else cx_node
            s.nodes[name] = (H, np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0)
        elif isinstance(node, str) and node not in s.nodes:
            cx = _vis(cxw) if cx_node is None else cx_node
            s.nodes[node] = (H, np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0)
        if scale is None:
            img, loc = mv.render_flat(path, 1.0, hide=hide, frames=frames, texts=texts, cx=CX0)
            if img is None: print('  empty', name, path); return None
            scale = M.jac_scale(H, loc) * 1.0
        img, loc = mv.render_flat(path, scale, hide=hide, frames=frames, texts=texts, cx=CX0, pad=pad)
        if img is None: print('  empty', name, path); return None
        if max(img.shape[:2]) > 1000:
            scale *= 1000.0 / max(img.shape[:2]); img, loc = mv.render_flat(path, scale, hide=hide, frames=frames, texts=texts, cx=CX0, pad=pad)
        s.add_image(name, img, loc); return loc
    def mimage(s, name, mv, path, scale, hide=None, frames=None, texts=None, tex=0):
        img, loc = mv.render_flat(path, scale, hide=hide, frames=frames, texts=texts, cx=CX0)
        if img is None: print('  empty image', name, path); return None
        s.add_image(name, img, loc, tex=tex); return loc
    def mnode(s, name, mv, path, frames=None, A=None, cx=None):
        cid, W, cxw = mv.node_world(path, frames)
        H = M.SOUT @ (mv.R0.P @ W)[:, [0, 1, 3]]
        if A is not None: H = H @ np.asarray(A, np.float64)
        cx = _vis(cxw) if cx is None else cx
        s.nodes[name] = (H, np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0)
    def mfield(s, name, mv, path, frames=None, size=None, align=None, color=None):
        cid = mv.node_world(path, frames)[0]; t = mv.ch[cid]; assert t['kind'] == 'text', (path, t['kind'])
        s.mnode(name, mv, path, frames)
        txt = t.get('text', ''); col = t.get('color', [255, 255, 255, 255]); sz = t.get('size', 12)
        if t.get('html'):
            m = re.search(r'color="#([0-9A-Fa-f]{6})"', txt)
            if m: col = [int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4)] + [255]
            m = re.search(r'size="(\d+)"', txt)
            if m: sz = float(m.group(1))
        s.fields[name] = dict(bounds=t['bounds'], size=size or sz, align=t.get('align', 0) if align is None else align, color=color or col)
    def screen_node(s, name='screen'):
        s.nodes[name] = (np.eye(3), np.ones(4, np.float32), np.zeros(4, np.float32))
    def stage_node(s, name='stage'):
        """1600x906.7 stage units -> Vita pixels"""
        s.nodes[name] = (M.SOUT.copy(), np.ones(4, np.float32), np.zeros(4, np.float32))
    def pil_image(s, name, rgba, loc, tex=0):
        a = np.asarray(rgba, np.float32) / 255.0
        a[..., :3] *= a[..., 3:4]
        s.add_image(name, a, loc, tex)

def _placements(mv, cid, frame=0):
    c = mv.ch.get(cid)
    if not c or c.get('kind') != 'sprite' or not c['frames']: return []
    dl = c['frames'][min(frame, len(c['frames']) - 1)]
    return [(d, dl[d]) for d in sorted(dl)]
class MB2(MB):
    def __init__(s):
        super().__init__(); s.groups = {}; s.rects = set()
    def _add_part(s, group, mv, path, hide, frames, texts, img, loc, H, cx):
        k = s.groups.setdefault(group, 0); name = '%s/%d' % (group, k); s.groups[group] = k + 1
        cx = _vis(cx); s.nodes[name] = (H, np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0)
        a = img[..., 3]; nz = a > 0.01
        inner = a > 0.99
        if inner.mean() > 0.85:
            col = img[inner].reshape(-1, 4); sd = col.std(0).max()
            if sd < 0.01:      # a plain rectangle: 4x4 texels stretched over its rect
                c = col.mean(0); s.add_image(name, np.tile(c, (4, 4, 1)).astype(np.float32), loc); s.rects.add(name); return name
        s.add_image(name, img, loc); return name
    def auto(s, group, mv, path, hide=None, frames=None, texts=None, min_cov=0.3, depth=0, max_depth=8, scale_mul=1.0):
        """render a subtree as tight parts: split nodes whose image is mostly empty into their children"""
        cid, W, cxw = mv.node_world(path, frames)
        H = M.SOUT @ (mv.R0.P @ W)[:, [0, 1, 3]]
        img, loc = mv.render_flat(path, 0.25, hide=hide, frames=frames, texts=texts, cx=CX0)
        if img is None: return
        sc = M.jac_scale(H, loc) * scale_mul
        img, loc = mv.render_flat(path, sc, hide=hide, frames=frames, texts=texts, cx=CX0)
        if img is None: return
        if max(img.shape[:2]) > 1000:     # wider than the screen: fit the atlas
            sc *= 1000.0 / max(img.shape[:2]); img, loc = mv.render_flat(path, sc, hide=hide, frames=frames, texts=texts, cx=CX0)
        cov = (img[..., 3] > 0.02).mean(); area = img.shape[0] * img.shape[1]
        kids = _placements(mv, cid, (frames or {}).get(path, 0))
        visible = [(d, p) for d, p in kids if not (hide and hide(path + '.' + (p.get('name') or 'd%d' % d))) and p.get('visible', 1) != 0 and 'clip' not in p]
        ranges = [(d, p['clip']) for d, p in kids if 'clip' in p]
        has_clip = any(a < d <= b for d, _ in visible for a, b in ranges)
        if cov < min_cov and area > 48 * 48 and len(visible) > 1 and not has_clip and depth < max_depth:
            for d, p in visible:
                s.auto(group, mv, path + '.' + (p.get('name') or 'd%d' % d), hide, frames, texts, min_cov, depth + 1, max_depth, scale_mul)
            return
        if cov < min_cov and area > 48 * 48 and len(visible) == 1 and not has_clip and depth < max_depth and mv.ch.get(visible[0][1].get('id'), {}).get('kind') == 'sprite':
            d, p = visible[0]; s.auto(group, mv, path + '.' + (p.get('name') or 'd%d' % d), hide, frames, texts, min_cov, depth + 1, max_depth, scale_mul); return
        s._add_part(group, mv, path, hide, frames, texts, img, loc, H, cxw)
    def pack(s, tex, width):
        """as Builder.pack, but each image's border texels are replicated into its padding (clean bilinear edges, solid rectangles)"""
        names = [n for n in s.order if s.images[n]['tex'] == tex]
        names.sort(key=lambda n: (-s.images[n]['img'].shape[0], -s.images[n]['img'].shape[1]))
        pad = 2 if tex != 1 else 4   # (b92: the weapon-icon page 2 as the atlas)
        def layout(names):
          sky = [[8, 0, width - 8], [0, 8, 8]]   # skyline segments [x, y, w]; the 8x8 corner at (0,0) stays empty
          sky.sort(); pos = {}
          def place(w, h):
              best = None
              for i in range(len(sky)):
                  x = sky[i][0]
                  if x + w > width: break
                  y = 0; j = i; rem = w
                  while rem > 0 and j < len(sky):
                      y = max(y, sky[j][1]); rem -= sky[j][2]; j += 1
                  if rem > 0: continue
                  if best is None or y + h < best[1] + h or (y + h == best[1] + h and x < best[0]): best = (x, y)
              return best
          for n in names:
              h, w = s.images[n]['img'].shape[:2]; W2, H2 = w + 2 * pad, h + 2 * pad
              x, y = place(W2, H2); pos[n] = (x + pad, y + pad)
              # raise the skyline over [x, x + W2)
              out = []; 
              for sx, sy, sw in sky:
                  a, b = sx, sx + sw
                  if b <= x or a >= x + W2: out.append([sx, sy, sw]); continue
                  if a < x: out.append([a, sy, x - a])
                  if b > x + W2: out.append([x + W2, sy, b - x - W2])
              out.append([x, y + H2, W2]); out.sort()
              m = [out[0]]
              for seg in out[1:]:
                  if seg[1] == m[-1][1] and seg[0] == m[-1][0] + m[-1][2]: m[-1][2] += seg[2]
                  else: m.append(seg)
              sky[:] = m
          return pos, (max(p[1] + s.images[n]['img'].shape[0] + pad for n, p in pos.items()) if pos else 8)
        # b91 (the sniper rifles' icons overflowed 1024 x 2048 with the tallest-first order): other orders tried when it does not
        # fit the power of two the content needs; the first order's layout is kept when it is as small (packs as before)
        pow2 = lambda v: 1 << max(0, int(v - 1).bit_length())
        pos, top = layout(names)
        for key in (lambda n: -s.images[n]['img'].shape[0] * s.images[n]['img'].shape[1], lambda n: (-s.images[n]['img'].shape[1], -s.images[n]['img'].shape[0]),
                    lambda n: -max(s.images[n]['img'].shape[:2])):
            if pow2(top) <= 1024 if tex else pow2(top) <= pow2(sum(s.images[n]['img'].shape[0] * s.images[n]['img'].shape[1] for n in names) / width): break
            p2, t2 = layout(sorted(names, key=key))
            if pow2(t2) < pow2(top): pos, top = p2, t2
        y = 0; shelf = top
        Hh = 1
        while Hh < y + shelf: Hh *= 2
        atlas = np.zeros((Hh, width, 4), np.float32)
        for n in names:
            img = s.images[n]['img']; h, w = img.shape[:2]; px, py = pos[n]
            if n in s.rects or n.endswith('#r'):
                e = np.pad(img, ((pad, pad), (pad, pad), (0, 0)), mode='edge'); atlas[py - pad:py + h + pad, px - pad:px + w + pad] = e
            else: atlas[py:py + h, px:px + w] = img
        for n in names: s.images[n]['pos'] = pos[n]
        return atlas
