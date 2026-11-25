# Host preview of dem_hud.ghud: draws the Demolition HUD states the way the engine would (image through its node's
# homography, colour = image * node mul + node add, premultiplied), on a 960x544 frame (optionally over a footage frame).
# usage: dem_hud_preview.py dem_hud.ghud out.png [background.jpg]
import sys, struct, numpy as np
sys.path.insert(0, '/root/gits/tools/ui')
from hudmv import warp, proj
from PIL import Image, ImageDraw

def load(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GHUD'; o = 8
    w0, h0, w1, h1, n = struct.unpack_from('<IIIII', d, o); o += 20
    imgs = {}; order = []
    for _ in range(n):
        nm = d[o:o + 32].split(b'\0')[0].decode(); t, _, _, _, x, y, w, h = struct.unpack_from('<BBBBHHHH', d, o + 32)
        loc = struct.unpack_from('<4f', d, o + 44); o += 60; imgs[nm] = dict(tex=t, x=x, y=y, w=w, h=h, loc=loc); order.append(nm)
    nn = struct.unpack_from('<I', d, o)[0]; o += 4; nodes = {}; nlist = []
    for _ in range(nn):
        nm = d[o:o + 32].split(b'\0')[0].decode(); H = np.array(struct.unpack_from('<9f', d, o + 32)).reshape(3, 3)
        mul = np.array(struct.unpack_from('<4f', d, o + 68)); add = np.array(struct.unpack_from('<4f', d, o + 84)); o += 100
        nodes[nm] = (H, mul, add); nlist.append(nm)
    nf = struct.unpack_from('<I', d, o)[0]; o += 4; fields = {}
    for _ in range(nf):
        nm = d[o:o + 32].split(b'\0')[0].decode(); ni = struct.unpack_from('<i', d, o + 32)[0]; b = struct.unpack_from('<4f', d, o + 36)
        size, align = struct.unpack_from('<fi', d, o + 52); col = d[o + 60:o + 64]; o += 64; fields[nm] = dict(node=nlist[ni], b=b, size=size, align=align, col=col)
    ng, asc, desc = struct.unpack_from('<Iff', d, o); o += 12 + ng * 8
    a0 = np.frombuffer(d, np.uint8, w0 * h0 * 4, o).reshape(h0, w0, 4).astype(np.float32) / 255; o += w0 * h0 * 4
    return imgs, nodes, fields, a0

class Canvas:
    def __init__(s, path, bg=None):
        s.imgs, s.nodes, s.fields, s.a0 = load(path)
        s.c = np.zeros((544, 960, 4), np.float32)
        if bg: s.c[..., :3] = np.asarray(Image.open(bg).convert('RGB').resize((960, 544)), np.float32) / 255 * 0.8; s.c[..., 3] = 1
        else: s.c[..., :3] = (0.22, 0.26, 0.28); s.c[..., 3] = 1
        s.texts = []
    def img(s, name, node, A=None, mul=None, add=None, dx=0, dy=0):
        im = s.imgs[name]; H, nm, na = s.nodes[node]
        if A is not None: H = H @ np.asarray(A, np.float64)
        if dx or dy: H = np.array([[1, 0, dx], [0, 1, dy], [0, 0, 1.0]]) @ H
        px = s.a0[im['y']:im['y'] + im['h'], im['x']:im['x'] + im['w']].copy()
        m = nm * (np.asarray(mul) if mul is not None else 1); a = na if add is None else np.asarray(add)
        px = px * m + a * px[..., 3:4]                                     # Flash colour transform on premultiplied colour
        px = np.clip(px, 0, 1); px[..., :3] = np.minimum(px[..., :3], px[..., 3:4])
        warp(px, im['loc'], H, s.c)
    def digits(s, prefix, n, v, mul=None):
        for i in range(n):
            if i > 0 and v < 10 ** i: break
            s.img('n30.%d' % (v // 10 ** i % 10), '%s%d' % (prefix, i + 1), mul=mul)
    def text(s, field, txt, color=None):
        f = s.fields[field]; H = s.nodes[f['node']][0]; b = f['b']
        x = {0: b[0], 1: b[1], 2: (b[0] + b[1]) / 2}.get(f['align'], b[0]); p = proj(H, x, (b[2] + b[3]) / 2)
        s.texts.append((p, txt, tuple(color or tuple(f['col'][:3])), f['align'], f['size'] * abs(H[0, 0])))
    def save(s, out):
        im = Image.fromarray((np.clip(s.c[..., :3], 0, 1) * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
        for (x, y), t, col, al, sz in s.texts:
            w = d.textlength(t); x0 = x if al == 0 else x - w if al == 1 else x - w / 2
            d.text((x0, y - 6), t, fill=col)
        im.save(out)

if __name__ == '__main__':
    C = Canvas(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else None)
    CY, RD, GR = (0.35, 0.85, 0.95, 1), (1, 0.35, 0.4, 1), (0.5, 0.5, 0.5, 1)
    # score panel: 1 : 8 : 0, timer 01:54, bomb timer 00:41, slots (ally X ▲ ▲, enemy ▲ X)
    for p in ('d1', 'd11', 'd81'): C.img('dm.sc.' + p, 'dm.sc.' + p)
    C.digits('dmL.n', 1, 1); C.digits('dmR.n', 1, 0); C.digits('dmG.n', 1, 8)
    C.img('dmT.colon', 'dmT.colon'); C.digits('dmTM.n', 2, 1); C.digits('dmTS.n', 2, 54)
    C.img('dm.bt.bg', 'dm.bt.bg'); C.img('dm.bt.icon', 'dm.bt.icon'); C.img('dm.bt.colon', 'dm.bt.colon'); C.digits('dmBM.n', 2, 0); C.digits('dmBS.n', 2, 41)
    for side, alive, col in (('A', (0, 1, 1, 0, 0, 0), CY), ('E', (1, 0, 0, 0, 0, 0), RD)):
        for k in range(1, 4):
            C.img('dm.u.back1', 'dm.u%s%d.back' % (side, k)); C.img('dm.u.ring', 'dm.u%s%d.ring' % (side, k))
            C.img('dm.u.alive' if alive[k - 1] else 'dm.u.dead', 'dm.u%s%d.icon' % (side, k), mul=col if alive[k - 1] else GR)
    # plant bar at 60 %
    C.img('dm.set.bg', 'dm.set.bg'); C.img('dm.set.gauge', 'dm.set.gauge')
    l = C.imgs['dm.set.bar']['loc']; p = 0.6
    C.img('dm.set.bar', 'dm.set.bar', A=[[p, 0, l[0] * (1 - p)], [0, 1, 0], [0, 0, 1]])
    C.text('dm.set.txt', 'Planting'); C.text('dm.set.hold', 'HOLD'); C.img('dm.set.key', 'dm.set.key'); C.text('dm.set.keytxt', 'O')
    # in-world markers at two projected points (anchor = wm.anchor origin on screen)
    ax, ay = proj(C.nodes['wm.anchor'][0], 0, 0)
    for nm, (x, y) in (('wm.atkA', (250, 330)), ('wm.atkBombB', (700, 300))):
        C.img(nm, 'wm.icon', dx=x - ax, dy=y - ay)
    C.save(sys.argv[2])
    # second frame: round result + switching sides emblem preview
    C2 = Canvas(sys.argv[1], sys.argv[3] if len(sys.argv) > 3 else None)
    C2.img('rr.band', 'rr.bg'); C2.img('rr.d53', 'rr.d53'); C2.img('rr.d7', 'rr.d7'); C2.img('rr.d11', 'rr.d11')
    C2.img('rr.emb.attA', 'rr.campa'); C2.img('rr.emb.defE', 'rr.campb'); C2.digits('rrA.n', 1, 2); C2.digits('rrE.n', 1, 1)
    C2.text('rr.round', 'ROUND 3'); C2.text('rr.title', 'VICTORY', (255, 255, 255)); C2.text('rr.sub', 'Mission Success : Detonation')
    C2.text('rr.typeA', 'ATTACK'); C2.text('rr.typeE', 'DEFENSE')
    C2.save(sys.argv[2].replace('.png', '_round.png'))
