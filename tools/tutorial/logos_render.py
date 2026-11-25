#!/usr/bin/env python3
"""databuilder: the start-up logo frames of Intro.swf drawn with our own SWF renderer (tools/ui/swf.py + render.py), in place
of JPEXS ffdec's sprite export (which needed a Java runtime). make_tut_logos.py composes and encodes them as before.

What the logo sprites use beyond tools/ui/render.py: nested sprites that play their own timelines (each instance's frame is
its age since it was placed, looping), Glow and Blur filters, the Add and Layer blend modes, and DefineShape4 shapes.
Filters, as Flash draws them: box blurs (blurX x blurY, `passes` times) in stage pixels; a glow is the object's blurred
alpha times its strength, in the glow colour, under the object.

  frames(swf) -> iterator of (kind, index, 1600x900 RGB uint8 array) in the order make_tut_logos.py encodes them
  python3 logos_render.py <Intro.swf> <out dir> [first last]   writes the frames as PNG (for checking)
"""
import os, sys, struct
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'ui'))
import swf, render
from render import Canvas, CX0, I2, mcompose, cxcompose, mapply
from scipy.ndimage import uniform_filter1d

W, H = 1600, 900
STEPS = [12, 21, 32]          # step1MC..step3MC (sprites _01_2, _02_9, _04_15)
STEP5 = 144                   # step5MC (_05_24)
DIVE_END = 57                 # step5 frames 2..DIVE_END (1-based, as JPEXS numbered them)
STEP_CY = 460                 # the step sprites are centred at x 800, y 460 (as make_tut_logos.py placed JPEXS's frames)


# ------------------------------------------------------------------ timeline with filters, blend modes and instance ages
def fixed(d, o): return struct.unpack_from('<i', d, o)[0] / 65536.0
def filters(d, o):
    n = d[o]; o += 1; out = []
    for _ in range(n):
        t = d[o]; o += 1
        if t == 0:   # DropShadow
            out.append(dict(t='shadow', c=list(d[o:o + 4]), bx=fixed(d, o + 4), by=fixed(d, o + 8), ang=fixed(d, o + 12), dist=fixed(d, o + 16),
                            k=struct.unpack_from('<h', d, o + 20)[0] / 256.0, f=d[o + 22])); o += 23
        elif t == 1:   # Blur
            out.append(dict(t='blur', bx=fixed(d, o), by=fixed(d, o + 4), passes=d[o + 8] >> 3)); o += 9
        elif t == 2:   # Glow
            out.append(dict(t='glow', c=list(d[o:o + 4]), bx=fixed(d, o + 4), by=fixed(d, o + 8), k=struct.unpack_from('<h', d, o + 12)[0] / 256.0,
                            inner=bool(d[o + 14] & 0x80), knockout=bool(d[o + 14] & 0x40), passes=d[o + 14] & 31)); o += 15
        elif t == 3: out.append(dict(t='?')); o += 27                                   # Bevel
        elif t in (4, 7): nc = d[o]; o += 1 + nc * 5 + 19; out.append(dict(t='?'))      # gradient glow / bevel
        elif t == 5: mx, my = d[o], d[o + 1]; o += 2 + 8 + 4 * mx * my + 4 + 1; out.append(dict(t='?'))
        elif t == 6: out.append(dict(t='cmat', m=struct.unpack_from('<20f', d, o))); o += 80
        else: raise ValueError('filter %d' % t)
    return out, o

def place(code, d):
    """PlaceObject2/3 -> dict (as swf.place, with the filters' parameters and a correct read of what follows them)"""
    f = d[0]; f2 = d[1] if code == 70 else 0; o = 2 if code == 70 else 1
    r = dict(depth=struct.unpack_from('<H', d, o)[0], move=bool(f & 1)); o += 2
    if code == 70 and (f2 & 8 or (f2 & 16 and f & 2)): r['cls'], o = swf.cstr(d, o)
    if f & 2: r['id'] = struct.unpack_from('<H', d, o)[0]; o += 2
    b = swf.Bits(d, o)
    if f & 4: r['m'] = swf.matrix(b)
    if f & 8: r['cx'] = swf.cxform(b, True)
    o = b.byte()
    if f & 16: r['ratio'] = struct.unpack_from('<H', d, o)[0]; o += 2
    if f & 32: r['name'], o = swf.cstr(d, o)
    if f & 64: r['clip'] = struct.unpack_from('<H', d, o)[0]; o += 2
    if code == 70:
        if f2 & 1: r['filters'], o = filters(d, o)
        if f2 & 2: r['blend'] = d[o]; o += 1
        if f2 & 4: o += 1
        if f2 & 32: r['visible'] = d[o]; o += 1
    return r

def timeline(tags):
    """-> per frame {depth: placement}, each placement with 'born' (the frame its instance was placed); 'keep' marks the
    instances that are still the same at the last frame (they survive the loop back to frame 1)"""
    disp = {}; out = []; f = 0
    for code, d in tags:
        if code in (26, 70):
            p = place(code, d); dep = p['depth']
            if p['move'] and dep in disp:
                q = dict(disp[dep]); new = 'id' in p and p['id'] != q.get('id')
                q.update({k: v for k, v in p.items() if k != 'move'})
                if new: q['born'] = f
                disp[dep] = q
            else: p['born'] = f; disp[dep] = p
        elif code == 28: disp.pop(struct.unpack_from('<H', d, 0)[0], None)
        elif code == 5: disp.pop(struct.unpack_from('<H', d, 2)[0], None)
        elif code == 1: out.append({k: dict(v) for k, v in disp.items()}); f += 1
    if out:
        last = out[-1]
        for fr in out:
            for dep, p in fr.items():
                q = last.get(dep); p['keep'] = bool(q and q['born'] == p['born'] and q.get('id') == p.get('id'))
    return out


def jpeg_bitmap(code, d):
    """DefineBitsJPEG2/3 -> (id, bitmap): the JPEG (or PNG / GIF) data, plus the zlib alpha plane of JPEG3"""
    import io, zlib
    from PIL import Image
    cid = struct.unpack_from('<H', d, 0)[0]
    if code == 35: n = struct.unpack_from('<I', d, 2)[0]; img, alpha = d[6:6 + n], d[6 + n:]
    else: img, alpha = d[2:], b''
    if img[:4] == b'\xff\xd9\xff\xd8': img = img[4:]   # (an old encoder's leading EOI/SOI pair)
    im = Image.open(io.BytesIO(img)); im.load()
    rgba = np.array(im.convert('RGBA'))
    if alpha and im.format == 'JPEG':
        a = np.frombuffer(zlib.decompress(alpha), np.uint8)[:rgba.shape[0] * rgba.shape[1]]
        a = a.reshape(rgba.shape[:2]); rgba[..., 3] = a
        af = a[..., None].astype(np.float32)   # the JPEG colours are premultiplied by that alpha
        rgba[..., :3] = np.where(af > 0, np.minimum(rgba[..., :3] * 255.0 / np.maximum(af, 1), 255), 0).astype(np.uint8)
    return cid, dict(kind='bitmap', img=rgba)

def load(path):
    swf.SHAPE4_EDGE_RECT_ALIGNED = True
    S = swf.load(path); ch = swf.characters(S)
    for code, d in S['tags']:   # sprites again, with the full placement read and instance ages; JPEG bitmaps
        if code == 39:
            sid, nf, tags = swf.sprite(d); ch[sid]['tl'] = timeline(tags)
        elif code in (21, 35):
            k, v = jpeg_bitmap(code, d); ch[k] = v
    return ch


# ------------------------------------------------------------------ renderer
def box(a, r, axis, passes):
    """Flash's box blur: `passes` box filters of width r (pixels) along one axis"""
    w = int(round(r))
    if w <= 1 or passes <= 0: return a
    for _ in range(passes): a = uniform_filter1d(a, w, axis=axis, mode='constant')
    return a

SUB = 4   # sub-rows per pixel; across a row the coverage is exact (area of the span in each pixel)
def coverage(segs, x0, y0, W, Hh):
    """coverage (Hh, W) of the region bounded by directed segments (n, 5: xa ya xb yb dir), nonzero winding with |w| capped at
    1 (each fill style's edges are turned so the fill is on the same side: inside winds +-1, holes 0)"""
    if len(segs) == 0: return np.zeros((Hh, W), np.float32)
    xa, ya, xb, yb, dr = segs.T
    up = yb < ya; s = np.where(up, -dr, dr)
    lo = np.where(up, yb, ya); hi = np.where(up, ya, yb); xl = np.where(up, xb, xa); xh = np.where(up, xa, xb)
    j0 = np.clip(np.ceil((lo - y0) * SUB - 0.5).astype(np.int64), 0, Hh * SUB); j1 = np.clip(np.ceil((hi - y0) * SUB - 0.5).astype(np.int64), 0, Hh * SUB)
    n = j1 - j0; keep = n > 0
    if not keep.any(): return np.zeros((Hh, W), np.float32)
    n = n[keep]; e = np.repeat(np.nonzero(keep)[0], n)
    j = np.repeat(j0[keep], n) + (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n))
    ys = y0 + (j + 0.5) / SUB; t = (ys - lo[e]) / (hi[e] - lo[e]); xs = xl[e] + t * (xh[e] - xl[e]) - x0
    xs = np.clip(xs, 0, W); xi = np.floor(xs).astype(np.int64); fr = xs - xi; w = s[e]
    idx = j * (W + 1) + xi
    acc = np.bincount(idx, w * (1 - fr), Hh * SUB * (W + 1)) + np.bincount(np.minimum(idx + 1, Hh * SUB * (W + 1) - 1), w * fr, Hh * SUB * (W + 1))
    wnd = np.cumsum(acc.reshape(Hh * SUB, W + 1)[:, :W], axis=1)
    return np.minimum(np.abs(wnd), 1.0).reshape(Hh, SUB, W).mean(1).astype(np.float32)

def coverage_rgba(segs, cols, x0, y0, W, Hh):
    """premultiplied colour (Hh, W, 4) of several solid fills at once: segs (n, 5) as coverage(), cols (n, 4) the fill
    colour of each segment's fill. The fills of one style set do not overlap and wind the same way, so the summed
    winding-weighted colours give each fill's own colour inside it (and the exact mix along shared edges)."""
    out = np.zeros((Hh, W, 4), np.float32)
    if len(segs) == 0: return out
    xa, ya, xb, yb, dr = segs.T
    up = yb < ya; s = np.where(up, -dr, dr)
    lo = np.where(up, yb, ya); hi = np.where(up, ya, yb); xl = np.where(up, xb, xa); xh = np.where(up, xa, xb)
    j0 = np.clip(np.ceil((lo - y0) * SUB - 0.5).astype(np.int64), 0, Hh * SUB); j1 = np.clip(np.ceil((hi - y0) * SUB - 0.5).astype(np.int64), 0, Hh * SUB)
    n = j1 - j0; keep = n > 0
    if not keep.any(): return out
    n = n[keep]; e = np.repeat(np.nonzero(keep)[0], n)
    j = np.repeat(j0[keep], n) + (np.arange(n.sum()) - np.repeat(np.cumsum(n) - n, n))
    ys = y0 + (j + 0.5) / SUB; t = (ys - lo[e]) / (hi[e] - lo[e]); xs = xl[e] + t * (xh[e] - xl[e]) - x0
    xs = np.clip(xs, 0, W); xi = np.floor(xs).astype(np.int64); fr = xs - xi; w = s[e]
    idx = j * (W + 1) + xi; idx1 = np.minimum(idx + 1, Hh * SUB * (W + 1) - 1); N = Hh * SUB * (W + 1)
    for ch in range(4):
        wc = w * cols[e, ch]
        acc = np.bincount(idx, wc * (1 - fr), N) + np.bincount(idx1, wc * fr, N)
        out[..., ch] = np.cumsum(acc.reshape(Hh * SUB, W + 1)[:, :W], axis=1).reshape(Hh, SUB, W).mean(1)
    out *= np.where(out[..., 3:4] < 0, -1.0, 1.0)   # (the shape's winding sign)
    return np.clip(out, 0.0, 1.0)

MASK_CX = ([1, 1, 1, 0], [0, 0, 0, 255])
def visible(cx):
    mul, add = cx; return not (mul[3] <= 0 and add[3] <= 0)

class LR(render.R):
    """render.R with instance ages, filters and blend modes; draw(cid, M, cx, canvas, time). zoom: the scale the stage is
    drawn at (filters are in stage pixels)"""
    zoom = 1.0
    def draw(s, cid, M, cx, cv, time=0):
        c = s.ch.get(cid)
        if c is None: return
        k = c['kind']
        if not visible(cx): return   # alpha x 0 (+ <= 0): nothing of it shows
        if k == 'shape': s.shape(c, M, cx, cv)
        elif k == 'sprite': s.sprite_t(c, M, cx, cv, time)
    def shape(s, c, M, cx, cv):
        """render.R.shape with the exact-span coverage above (fills by fill style, then the strokes)"""
        E = c['edges']
        if not E: return
        a, b, cc, d, tx, ty = M
        pts = np.array([e[:4] for e in E], np.float64)
        T = np.stack([a * pts[:, 0] + cc * pts[:, 1] + tx, b * pts[:, 0] + d * pts[:, 1] + ty,
                      a * pts[:, 2] + cc * pts[:, 3] + tx, b * pts[:, 2] + d * pts[:, 3] + ty], 1)
        keys = {}
        for i, e in enumerate(E):
            if e[4] and e[4] != e[5]: keys.setdefault(e[4], []).append((i, -1.0))
            if e[5] and e[4] != e[5]: keys.setdefault(e[5], []).append((i, 1.0))
        mul, add = np.array(cx[0], np.float32), np.array(cx[1], np.float32) / 255.0
        for ss in sorted(set(k[0] for k in keys)):   # solid fills: one pass per style set
            sk = [k for k in keys if k[0] == ss and c['fills'][k[0]][k[1] - 1]['t'] == 'solid']
            if not sk: continue
            idx = []; dr = []; cols = []
            for k in sk:
                col = np.clip(np.array(c['fills'][k[0]][k[1] - 1]['c'], np.float32) / 255.0 * mul + add, 0, 1); col = np.r_[col[:3] * col[3], col[3]]
                for i, x in keys[k]: idx.append(i); dr.append(x); cols.append(col)
            idx = np.array(idx); segs = np.concatenate([T[idx], np.array(dr)[:, None]], 1); P = np.concatenate([segs[:, :2], segs[:, 2:4]])
            x0, y0, x1, y1 = render.bbox_of(P, cv)
            if x1 <= x0 or y1 <= y0: continue
            px = coverage_rgba(segs, np.array(cols), x0, y0, x1 - x0, y1 - y0)
            if px[..., 3].max() > 0: cv.comp(x0, y0, px)
        for key in sorted(keys, key=lambda k: (k[0], k[1])):
            if c['fills'][key[0]][key[1] - 1]['t'] == 'solid': continue
            idx = np.array([i for i, _ in keys[key]]); dr = np.array([x for _, x in keys[key]])
            segs = np.concatenate([T[idx], dr[:, None]], 1); P = np.concatenate([segs[:, :2], segs[:, 2:4]])
            x0, y0, x1, y1 = render.bbox_of(P, cv)
            if x1 <= x0 or y1 <= y0: continue
            cov = coverage(segs, x0, y0, x1 - x0, y1 - y0)
            if cov.max() <= 0: continue
            st = c['fills'][key[0]][key[1] - 1]
            col = s.fillpix(st, M, cx, x0, y0, x1 - x0, y1 - y0); al = col[..., 3:4] * cov[..., None]
            cv.comp(x0, y0, np.concatenate([col[..., :3] * al, al], 2))
        lk = {}
        for i, e in enumerate(E):
            if e[6]: lk.setdefault(e[6], []).append(i)
        sc = np.sqrt(abs(a * d - b * cc))
        for key, idx in lk.items():
            ls = c['lines'][key[0]][key[1] - 1]; w = max(ls['w'] * sc, 1.0) / 2
            quads = []
            for i in idx:
                xa, ya, xb, yb = T[i]; dx, dy = xb - xa, yb - ya; l = np.hypot(dx, dy)
                if l < 1e-6: continue
                nx, ny = -dy / l * w, dx / l * w; ex, ey = dx / l * w * 0.5, dy / l * w * 0.5
                quads.append([(xa - ex + nx, ya - ey + ny), (xb + ex + nx, yb + ey + ny), (xb + ex - nx, yb + ey - ny), (xa - ex - nx, ya - ey - ny)])
            if not quads: continue
            P = np.array([q for qq in quads for q in qq]); x0, y0, x1, y1 = render.bbox_of(P, cv)
            if x1 <= x0 or y1 <= y0: continue
            cov = np.zeros((y1 - y0, x1 - x0), np.float32)
            for q in quads:
                segs = np.array([[q[k][0], q[k][1], q[(k + 1) % 4][0], q[(k + 1) % 4][1], 1.0] for k in range(4)])
                cov = np.maximum(cov, coverage(segs, x0, y0, x1 - x0, y1 - y0))
            mul, add = cx; col = np.clip(np.array(ls['c'], np.float32) / 255.0 * np.array(mul, np.float32) + np.array(add, np.float32) / 255.0, 0, 1)
            al = col[3] * cov[..., None]; cv.comp(x0, y0, np.concatenate([np.broadcast_to(col[:3], al.shape[:2] + (3,)) * al, al], 2))
    def sprite_t(s, c, M, cx, cv, time):
        tl = c.get('tl') or []
        if not tl: return
        fi = time % len(tl); dl = tl[fi]; clips = []
        for dep in sorted(dl):
            p = dl[dep]
            if p.get('visible', 1) == 0: continue
            clips = [x for x in clips if dep <= x[0]]
            age = (time - p['born']) if (p['keep'] and p['born'] == 0) else (fi - p['born'])   # survives the loop: keeps ageing
            Mm = mcompose(M, p.get('m', I2)); cc = cxcompose(cx, p['cx']) if 'cx' in p else cx
            if 'clip' in p:
                # a mask counts where it is filled, whatever its colours' alpha (as Flash)
                mc = Canvas(cv.W, cv.H); s.draw(p.get('id'), Mm, MASK_CX, mc, age); clips.append((p['clip'], mc.px[..., 3:4].copy())); continue
            fl = [x for x in p.get('filters', []) if x['t'] in ('glow', 'blur')]; blend = p.get('blend', 0)
            if clips or fl or blend > 2:
                tmp = Canvas(cv.W, cv.H); s.draw(p.get('id'), Mm, cc, tmp, age); px = tmp.px
                for f in fl: px = s.filter(px, f)
                if clips:
                    m = np.ones_like(clips[0][1])
                    for _, mk in clips: m = m * mk
                    px = px * m
                if blend == 8: cv.px[:] = np.minimum(cv.px + px, 1.0)              # add
                elif blend == 9: cv.px[..., :3] = np.maximum(cv.px[..., :3] - px[..., :3], 0)   # subtract
                elif blend == 3:                                                  # multiply
                    cv.px[:] = px * cv.px + px * (1 - cv.px[..., 3:4]) + cv.px * (1 - px[..., 3:4])
                elif blend == 4:                                                  # screen
                    cv.px[:] = px + cv.px - px * cv.px
                else: cv.px[:] = px + cv.px * (1 - px[..., 3:4])
            else: s.draw(p.get('id'), Mm, cc, cv, age)
    def filter(s, px, f):
        if f['t'] == 'blur':
            return np.clip(box(box(px, f['bx'] * s.zoom, 1, f['passes']), f['by'] * s.zoom, 0, f['passes']), 0, 1)
        a = px[..., 3]
        if f['inner']: a = 1 - a
        g = np.clip(box(box(a, f['bx'] * s.zoom, 1, f['passes']), f['by'] * s.zoom, 0, f['passes']) * f['k'], 0, 1)
        col = np.array(f['c'], np.float32) / 255.0; ga = g * col[3]
        glow = np.concatenate([col[:3] * ga[..., None], ga[..., None]], 2)
        if f['inner']:
            glow = glow * px[..., 3:4]
            return glow if f['knockout'] else glow + px * (1 - glow[..., 3:4])
        if f['knockout']: return glow * (1 - px[..., 3:4])
        return px + glow * (1 - px[..., 3:4])


# ------------------------------------------------------------------ the logo frames
def bounds(ch, cid, memo):
    """[xmin, xmax, ymin, ymax] of a character over all its frames, as JPEXS sizes a sprite export (child rects transformed)"""
    if cid in memo: return memo[cid]
    c = ch.get(cid); r = None
    if c is None: pass
    elif c['kind'] in ('shape', 'text'): r = list(c['bounds'])
    elif c['kind'] == 'sprite':
        for fr in c['tl']:
            for p in fr.values():
                b = bounds(ch, p.get('id'), memo)
                if b is None: continue
                P = np.array([mapply(p.get('m', I2), x, y) for x in b[:2] for y in b[2:]])
                b = [P[:, 0].min(), P[:, 0].max(), P[:, 1].min(), P[:, 1].max()]
                r = b if r is None else [min(r[0], b[0]), max(r[1], b[1]), min(r[2], b[2]), max(r[3], b[3])]
    memo[cid] = r; return r

def to_rgb(cv):
    """premultiplied canvas over black -> RGB uint8 (as the frames were composed over the black stage)"""
    return (np.clip(cv.px[..., :3], 0, 1) * 255.0 + 0.5).astype(np.uint8)

class Logos:
    """the frames in the encoder's order: steps 1-3 (their frames 2..n), 3 black (step4MC), step5 frames 2..DIVE_END, 6 black.
    Each step sprite sits as make_tut_logos.py placed JPEXS's whole-bounds export: centred at (800, STEP_CY); step5: the
    centre W x H of its whole-bounds render (8000 x 4500: its glows reach far out)."""
    def __init__(s, swf_path):
        s.ch = load(swf_path); s.R = LR(s.ch); memo = {}; s.seq = []
        for sid in STEPS:   # frame 1 of each step is the hidden / stopped state
            b = bounds(s.ch, sid, memo); iw, ih = int(b[1] - b[0]), int(b[3] - b[2])
            M = (1, 0, 0, 1, (W - iw) // 2 - b[0], STEP_CY - ih // 2 - b[2])
            s.seq += [('step%d' % sid, sid, M, t) for t in range(1, len(s.ch[sid]['tl']))]
        s.seq += [('black', None, None, 0)] * 3
        b = bounds(s.ch, STEP5, memo); iw, ih = int(b[1] - b[0]), int(b[3] - b[2])
        M5 = (1, 0, 0, 1, -((iw - W) // 2) - b[0], -((ih - H) // 2) - b[2])
        s.seq += [('step5', STEP5, M5, t) for t in range(1, DIVE_END)]
        s.seq += [('black', None, None, 0)] * 6
    def __len__(s): return len(s.seq)
    def frame(s, i):
        kind, sid, M, t = s.seq[i]; cv = Canvas(W, H)
        if sid is not None: s.R.draw(sid, M, CX0, cv, t)
        return to_rgb(cv)

def frames(swf_path, only=None):
    """(kind, index, 1600x900 RGB uint8) for every frame (or those in `only`)"""
    L = Logos(swf_path)
    for i in range(len(L)):
        if only is None or i in only: yield L.seq[i][0], i, L.frame(i)

def count(swf_path): return len(Logos(swf_path))


if __name__ == '__main__':
    from PIL import Image
    src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True)
    only = set(range(int(sys.argv[3]), int(sys.argv[4]) + 1)) if len(sys.argv) > 4 else None
    for kind, i, img in frames(src, only):
        Image.fromarray(img).save(os.path.join(out, '%03d.png' % i)); print(i, kind, flush=True)
