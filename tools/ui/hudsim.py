# Reader + software rasterizer for .ghud files: draws exactly what engine/src/hud.h draws (same data, same
# maths), used to check the HUD offline against the gameplay footage.
import numpy as np, struct
import hudmv as M
class Hud:
    def __init__(s, path):
        d = open(path, 'rb').read(); o = 8
        s.w0, s.h0, s.w1, s.h1 = struct.unpack_from('<IIII', d, o); o += 16
        n = struct.unpack_from('<I', d, o)[0]; o += 4; s.img = {}; s.imgl = []
        for i in range(n):
            nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32
            tex, _, _, _, x, y, w, h = struct.unpack_from('<BBBBHHHH', d, o); o += 12
            loc = struct.unpack_from('<4f', d, o); o += 16
            s.img[nm] = dict(tex=tex, x=x, y=y, w=w, h=h, loc=loc); s.imgl.append(nm)
        n = struct.unpack_from('<I', d, o)[0]; o += 4; s.node = {}
        for i in range(n):
            nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32
            H = np.array(struct.unpack_from('<9f', d, o), np.float64).reshape(3, 3); o += 36
            mul = np.array(struct.unpack_from('<4f', d, o)); o += 16; add = np.array(struct.unpack_from('<4f', d, o)); o += 16
            s.node[nm] = (H, mul, add)
        n = struct.unpack_from('<I', d, o)[0]; o += 4; s.field = {}
        for i in range(n):
            nm = d[o:o + 32].split(b'\0')[0].decode(); o += 32
            ni = struct.unpack_from('<i', d, o)[0]; o += 4; b = struct.unpack_from('<4f', d, o); o += 16
            size, align = struct.unpack_from('<fi', d, o); o += 8; col = list(d[o:o + 4]); o += 4
            s.field[nm] = dict(bounds=b, size=size, align=align, color=col)
        n, s.asc, s.desc = struct.unpack_from('<Iff', d, o); o += 12; s.glyph = {}
        for i in range(n):
            code, ix, adv = struct.unpack_from('<Hhf', d, o); o += 8
            s.glyph[code] = (s.imgl[ix] if ix >= 0 else None, adv)
        s.a0 = np.frombuffer(d, np.uint8, s.w0 * s.h0 * 4, o).reshape(s.h0, s.w0, 4).astype(np.float32) / 255; o += s.w0 * s.h0 * 4
        s.a1 = np.frombuffer(d, np.uint8, s.w1 * s.h1 * 4, o).reshape(s.h1, s.w1, 4).astype(np.float32) / 255
        s.cv = np.zeros((M.VH, M.VW, 4), np.float32)
    def clear(s, bg=None):
        s.cv[:] = 0
        if bg is not None: s.bg = bg
    # draw image 'name' in node plane; A = 3x3 local affine (image local -> node local)
    def draw(s, name, node, A=None, mul=None, add=None, ramp=None, thr=1.0):
        im = s.img[name]; H, nmul, nadd = s.node[node]
        if A is not None: H = H @ A
        mul = nmul if mul is None else np.array(mul); add = nadd if add is None else np.array(add)
        tex = s.a0 if im['tex'] == 0 else s.a1
        sub = tex[im['y']:im['y'] + im['h'], im['x']:im['x'] + im['w']]
        if ramp is not None:
            r = s.img[ramp]; rl = r['loc']; l = im['loc']
            rsub = s.a0[r['y']:r['y'] + r['h'], r['x']:r['x'] + r['w'], 0]
            ys, xs = np.mgrid[0:im['h'], 0:im['w']] + 0.5
            lx = l[0] + xs / im['w'] * (l[2] - l[0]); ly = l[1] + ys / im['h'] * (l[3] - l[1])
            u = np.clip(((lx - rl[0]) / (rl[2] - rl[0]) * r['w'] - 0.5).astype(int), 0, r['w'] - 1)
            v = np.clip(((ly - rl[1]) / (rl[3] - rl[1]) * r['h'] - 0.5).astype(int), 0, r['h'] - 1)
            rv = rsub[v, u]; k = np.clip((thr - rv) * 200 + 0.5, 0, 1)[..., None]
            sub = sub * k
        c = np.empty_like(sub)
        c[..., :3] = (sub[..., :3] * mul[None, None, :3] + add[None, None, :3] * sub[..., 3:4]) * mul[3]
        c[..., 3] = sub[..., 3] * mul[3]
        c = np.clip(c, 0, 1); c[..., :3] = np.minimum(c[..., :3], c[..., 3:4])
        M.warp(c, im['loc'], H, s.cv)
    def number(s, prefix, value, nslots, glyphs='n30', zeros=None, mul=None, add=None, lead_zero=False, left=False):
        txt = str(int(value))
        if left:      # left-aligned: first digit in the highest (leftmost) slot
            for j, ch in enumerate(txt[:nslots]): s.draw('%s.%s' % (glyphs, ch), '%s%d' % (prefix, nslots - j), mul=mul, add=add)
            return
        for i in range(nslots):
            nd = '%s%d' % (prefix, i + 1)
            if nd not in s.node: continue
            if i < len(txt): s.draw('%s.%s' % (glyphs, txt[-1 - i]), nd, mul=mul, add=add)
            elif lead_zero: s.draw('%s.0' % glyphs, nd, mul=mul, add=add)
            elif zeros and nd.replace(prefix, zeros) in s.node and ('%s.z' % glyphs) in s.img: s.draw('%s.z' % glyphs, nd.replace(prefix, zeros))
    def text(s, node, x, y, size, txt, align=0, mul=(1, 1, 1, 1), add=(0, 0, 0, 0), width=None, spacing=0.0):
        sc = size; w = s.text_width(txt, size) + spacing * size * max(len(txt) - 1, 0)
        if align == 1: x -= w
        elif align == 2: x -= w / 2
        base = y + s.asc * size
        for ch in txt:
            g = s.glyph.get(ord(ch))
            if g is None: x += size * 0.5; continue
            nm, adv = g
            if nm: s.draw(nm, node, A=np.array([[sc, 0, x], [0, sc, base], [0, 0, 1.0]]), mul=mul, add=add)
            x += adv * size + spacing * size
    def text_width(s, txt, size):
        return sum((s.glyph[ord(c)][1] if ord(c) in s.glyph else 0.5) * size for c in txt)
    def field_text(s, name, txt, mul=None, add=(0, 0, 0, 0)):
        f = s.field[name]; x0, x1, y0, y1 = f['bounds']; size = f['size']
        col = np.array(f['color'], np.float32) / 255
        mul = col if mul is None else mul
        w = s.text_width(txt, size); al = f['align']
        x = x0 + 2 if al == 0 else (x1 - 2 - w if al == 1 else (x0 + x1) / 2 - w / 2)
        s.text(name, x, y0 + 2, size, txt, 0, mul, add)
    def image(s, bg=None):
        from PIL import Image
        c = s.cv
        if bg is None: bg = np.zeros(c.shape[:2] + (3,), np.float32) + np.array([0.25, 0.27, 0.3])
        out = c[..., :3] + bg * (1 - c[..., 3:4])
        return Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8))
