# Builder for the engine's HUD file (.ghud): images packed in an atlas (premultiplied RGBA8), nodes
# (local plane -> screen homography + Flash colour transform), gauge ramps, text fields and a glyph font.
import numpy as np, struct, re
from PIL import Image
import hudmv as M, render
from render import CX0, cxcompose
NAMELEN = 32
def _np8(img): return np.clip(np.round(img * 255), 0, 255).astype(np.uint8)
class Builder:
    def __init__(s):
        s.images = {}      # name -> dict(img float premult, loc, tex)
        s.order = []
        s.nodes = {}       # name -> (H 3x3, mul4, add4)
        s.fields = {}      # name -> dict(node, bounds, size, align, color)
        s.glyphs = {}      # code -> (img name, adv em)
        s.font_asc = s.font_desc = 0
        s.oversample = 1.15  # texels per screen pixel for parts (x the plane's scale)
        s.ramp_scale = 1.0   # gauge ramps (threshold maps) can be stored smaller than their image
        s.dens_k = {}        # per part: density factor (soft bands / plain fills can be stored small and stretched)
    # ---------------------------------------------------------------- nodes
    def node(s, name, path, mv=None, A=None, cx=None):
        mv = mv or M.main(); cid, W, cxw = mv.node_world(path)
        H = M.SOUT @ (mv.R0.P @ W)[:, [0, 1, 3]]
        if A is not None: H = H @ np.asarray(A, np.float64)
        cx = cxw if cx is None else cx
        s.nodes[name] = (H, np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0)
        return H
    def vnode(s, name, base, A, cx=None):
        """virtual node: an existing node's plane with an extra local affine (3x3)"""
        H, mul, add = s.nodes[base]
        if cx is not None: mul, add = np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0
        s.nodes[name] = (H @ np.asarray(A, np.float64), mul, add)
    # ---------------------------------------------------------------- images
    def add_image(s, name, img, loc, tex=0):
        s.images[name] = dict(img=img.astype(np.float32), loc=tuple(float(v) for v in loc), tex=tex); s.order.append(name)
    def density(s, H, path, mv, hide):
        """texels per local unit so the image lands ~1:1 (x1.15) on the Vita screen"""
        img, loc = mv.render_flat(path, 1.0, hide=hide, cx=CX0)
        if img is None: return None, None
        return M.jac_scale(H, loc) * s.oversample, loc
    def part(s, name, path, hide=None, mv=None, node=True, scale=None, texts=None, cx=None):
        """static part: node + flat image of the subtree (raw colours; the node's colour transform applies at draw)"""
        mv = mv or M.main()
        H = s.node(name, path, mv, cx=cx) if node else s.nodes[name][0]
        if scale is None:
            scale, _ = s.density(H, path, mv, hide)
            if scale is None: print('  empty part', name, path); return None
            scale *= s.dens_k.get(name, 1.0)
        img, loc = mv.render_flat(path, scale, hide=hide, cx=CX0, texts=texts)
        if img is None: print('  empty part', name, path); return None
        s.add_image(name, img, loc); return loc
    def image(s, name, path, scale, hide=None, mv=None, texts=None):
        mv = mv or M.main(); img, loc = mv.render_flat(path, scale, hide=hide, cx=CX0, texts=texts)
        if img is None: print('  empty image', name, path); return None
        s.add_image(name, img, loc); return loc
    def ramp(s, name, path, prefix, n, full_k=None, mv=None, scale=None, other_hide=None, node=True):
        """gauge made of n cumulative state children prefix1..prefixN: image of the full state + ramp image
        (per texel: the fraction at which it lights up). Draw with the ramp and a threshold = gauge value."""
        mv = mv or M.main(); full_k = full_k or n
        H = s.node(name, path, mv) if node else s.nodes[name][0]
        def only_k(k):
            pat = re.compile(re.escape(path) + r'\.' + prefix + r'(\d+)(\.|$)')
            def h(p):
                if other_hide and other_hide(p): return True
                m = pat.match(p)
                return bool(m) and int(m.group(1)) != k
            return h
        if scale is None: scale, _ = s.density(H, path, mv, only_k(full_k))
        full, loc = mv.render_flat(path, scale, hide=only_k(full_k), cx=CX0)
        h, w = full.shape[:2]; bounds = (*loc, w, h)
        a_full = full[..., 3]; ramp = np.full((h, w), 2.0, np.float32)
        for k in range(1, n + 1):
            img, _ = mv.render_flat(path, scale, hide=only_k(k), cx=CX0, bounds=bounds)
            lit = (img[..., 3] >= 0.5 * np.maximum(a_full, 1e-3)) & (a_full > 0.004)
            ramp[lit & (ramp > 1.5)] = (k - 0.5) / n
        ramp[(ramp > 1.5) & (a_full > 0.004)] = 1.0
        # dilate valid values into empty texels so bilinear filtering at edges stays correct
        valid = ramp < 1.5
        for _ in range(4):
            if valid.all(): break
            r2 = ramp.copy(); v2 = valid.copy()
            for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
                sh = np.roll(np.roll(ramp, dy, 0), dx, 1); sv = np.roll(np.roll(valid, dy, 0), dx, 1)
                take = (~v2) & sv; r2[take] = sh[take]; v2 |= take
            ramp, valid = r2, v2
        ramp[~valid] = 1.0
        s.add_image(name, full, loc)
        if s.ramp_scale < 1:
            from PIL import Image as _Im
            rw, rh = max(4, int(round(w * s.ramp_scale))), max(4, int(round(h * s.ramp_scale)))
            ramp = np.asarray(_Im.fromarray(ramp.astype(np.float32), 'F').resize((rw, rh), _Im.BILINEAR), np.float32)
        r = np.repeat(ramp[..., None], 4, 2); s.add_image(name + '#r', r, loc)
        return loc
    def digits(s, prefix, group_path, slots, mv=None):
        """digit slot nodes prefix.1 (ones) .. prefix.n"""
        for i, sl in enumerate(slots): s.node('%s%d' % (prefix, i + 1), group_path + '.' + sl, mv)
    def glyphset(s, name, any_path, scale, mv=None):
        """10 digit images name.0..9 from a Font_Number instance (children no1..no9,no0)"""
        mv = mv or M.main()
        for k in range(10):
            s.image('%s.%d' % (name, k), any_path + '.no%d' % k, scale, mv=mv)
    def field(s, name, path, mv=None, size=None, align=None, color=None):
        """dynamic text field: node of its plane + bounds/size/alignment/colour from the SWF"""
        mv = mv or M.main(); cid = mv.node_world(path)[0]; t = mv.ch[cid]; assert t['kind'] == 'text', (path, t['kind'])
        s.node(name, path, mv)
        txt = t.get('text', ''); col = t.get('color', [255, 255, 255, 255]); sz = t.get('size', 12)
        if t.get('html'):
            m = re.search(r'color="#([0-9A-Fa-f]{6})"', txt)
            if m: col = [int(m.group(1)[i:i + 2], 16) for i in (0, 2, 4)] + [255]
            m = re.search(r'size="(\d+)"', txt)
            if m: sz = float(m.group(1))
        s.fields[name] = dict(bounds=t['bounds'], size=size or sz, align=t.get('align', 0) if align is None else align, color=color or col)
    def font(s, em_px=44, chars=None):
        f = M.FONT; sc = em_px / 1024.0
        chars = chars or [chr(c) for c in range(32, 127)] + list('·×←→↑↓')
        s.font_asc = f['asc'] / 1024.0; s.font_desc = f.get('desc', 0) / 1024.0
        for chh in chars:
            g = f['codes'].get(ord(chh))
            if g is None: continue
            adv = f['adv'][g] / 1024.0; E = f['glyphs'][g]
            if not E: s.glyphs[ord(chh)] = (None, adv); continue
            P = np.array([[e[0], e[1], e[2], e[3]] for e in E], np.float64) * sc
            x0 = int(np.floor(min(P[:, 0].min(), P[:, 2].min()))) - 3; y0 = int(np.floor(min(P[:, 1].min(), P[:, 3].min()))) - 3
            x1 = int(np.ceil(max(P[:, 0].max(), P[:, 2].max()))) + 3; y1 = int(np.ceil(max(P[:, 1].max(), P[:, 3].max()))) + 3
            cov = render.coverage(P, x0, y0, x1 - x0, y1 - y0)
            img = np.repeat(cov[..., None], 4, 2).astype(np.float32)
            nm = 'g%04x' % ord(chh)
            s.add_image(nm, img, (x0 / em_px, y0 / em_px, x1 / em_px, y1 / em_px), tex=1)
            s.glyphs[ord(chh)] = (nm, adv)
    # ---------------------------------------------------------------- output
    def pack(s, tex, width):
        names = [n for n in s.order if s.images[n]['tex'] == tex]
        seen = {}; alias = {}
        for n in names:
            im = s.images[n]['img']; key = (im.shape, hash(np.round(im * 255).astype(np.uint8).tobytes()))
            if key in seen and np.array_equal(np.round(s.images[seen[key]]['img'] * 255), np.round(im * 255)): alias[n] = seen[key]
            else: seen[key] = n
        names = [n for n in names if n not in alias]
        names.sort(key=lambda n: -s.images[n]['img'].shape[0])
        pad = 2 if tex == 0 else 4
        x = 8; y = shelf = 0; pos = {}      # 4x4 zero block reserved at (0,0): 'no ramp' texel
        for n in names:
            h, w = s.images[n]['img'].shape[:2]
            if x + w + pad > width: x = 0; y += shelf; shelf = 0
            pos[n] = (x + pad // 2, y + pad // 2); x += w + pad; shelf = max(shelf, h + pad)
        H = 1
        while H < y + shelf: H *= 2
        atlas = np.zeros((H, width, 4), np.float32)
        for n in names:
            img = s.images[n]['img']; h, w = img.shape[:2]; px, py = pos[n]
            atlas[py:py + h, px:px + w] = img
        for n in names: s.images[n]['pos'] = pos[n]
        for n, a in alias.items(): s.images[n]['pos'] = pos[a]
        return atlas
    def write(s, path, width=1024):
        a0 = s.pack(0, width); a1 = s.pack(1, 512) if any(v['tex'] == 1 for v in s.images.values()) else np.zeros((8, 8, 4), np.float32)
        a0[0:4, 0:4] = 0; a1[0:4, 0:4] = 0
        # b92: a third page (tex 2: the menu's weapon icons, which overflowed the 1024 x 2048 atlas) -> version 2, its size in the
        # header after the other two and its texels after theirs. Packs without one are written as before (version 1)
        a2 = s.pack(2, 1024) if any(v['tex'] == 2 for v in s.images.values()) else None
        if a2 is not None: a2[0:4, 0:4] = 0
        imgs = list(s.order); name_ix = {n: i for i, n in enumerate(imgs)}
        with open(path, 'wb') as f:
            f.write(b'GHUD' + struct.pack('<I', 1 if a2 is None else 2))
            f.write(struct.pack('<IIII', a0.shape[1], a0.shape[0], a1.shape[1], a1.shape[0]))
            if a2 is not None: f.write(struct.pack('<II', a2.shape[1], a2.shape[0]))
            f.write(struct.pack('<I', len(imgs)))
            for n in imgs:
                im = s.images[n]; h, w = im['img'].shape[:2]; x, y = im['pos']
                f.write(n.encode()[:NAMELEN - 1].ljust(NAMELEN, b'\0'))
                f.write(struct.pack('<BBBBHHHH', im['tex'], 0, 0, 0, x, y, w, h)); f.write(struct.pack('<4f', *im['loc']))
            f.write(struct.pack('<I', len(s.nodes)))
            for n, (H, mul, add) in s.nodes.items():
                f.write(n.encode()[:NAMELEN - 1].ljust(NAMELEN, b'\0')); f.write(np.asarray(H, '<f4').ravel().tobytes())
                f.write(np.asarray(mul, '<f4').tobytes()); f.write(np.asarray(add, '<f4').tobytes())
            f.write(struct.pack('<I', len(s.fields)))
            nodes = list(s.nodes.keys())
            for n, fl in s.fields.items():
                f.write(n.encode()[:NAMELEN - 1].ljust(NAMELEN, b'\0'))
                f.write(struct.pack('<i', nodes.index(n))); f.write(struct.pack('<4f', *fl['bounds'])); f.write(struct.pack('<fi', fl['size'], fl['align']))
                f.write(bytes(int(c) for c in fl['color'][:4]))
            f.write(struct.pack('<Iff', len(s.glyphs), s.font_asc, s.font_desc))
            for code, (nm, adv) in sorted(s.glyphs.items()):
                f.write(struct.pack('<Hhf', code, name_ix[nm] if nm else -1, adv))
            f.write(_np8(a0).tobytes()); f.write(_np8(a1).tobytes())
            if a2 is not None: f.write(_np8(a2).tobytes())
        return a0, a1
