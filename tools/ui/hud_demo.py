# Draws HUD states through hudsim (= the engine's hud.h logic) next to footage frames for checking.
import sys, numpy as np
sys.path.insert(0, '/root/gits/tools/ui')
import hudmv as M
from hudsim import Hud
from PIL import Image
CYAN = (0, 0, 0, 1); CYAN_ADD = (41 / 255, 211 / 255, 225 / 255, 0)
def T(dx, dy, s=1.0, r=0.0):
    c, sn = np.cos(r) * s, np.sin(r) * s
    return np.array([[c, -sn, dx], [sn, c, dy], [0, 0, 1.0]])
def draw_state(h, st):
    # ---- HP / AP
    hp, ap = st.get('hp', 100), st.get('ap', 100); low = hp <= 20
    for p in ('hp.arc31', 'hp.arc32', 'hp.disc'): h.draw(p, p)
    h.draw('hp.back', 'hp.back'); h.draw('hp.d1', 'hp.d1')
    h.draw('hp.ticks', 'hp.ticks')
    if ap > 0: h.draw('ap.gauge', 'ap.gauge', ramp='ap.gauge#r', thr=ap / 100)
    if hp > 0: h.draw('hp.gaugeR' if low else 'hp.gauge', 'hp.gauge', ramp='hp.gauge#r', thr=hp / 100)
    h.draw('hp.labelW' if low else 'hp.label', 'hp.labelW' if low else 'hp.label')
    pre = 'hpW' if low else 'hp'
    h.number(pre + '.n', hp, 3, zeros=pre + '.z')
    APC = (0.55, 0.55, 0.55, 0.6) if ap > 0 else (0.6, 0.1, 0.15, 0.5)
    h.draw('ap.label', 'ap.label', mul=APC); h.number('ap.n', ap, 3, zeros='ap.z', mul=APC)
    # ---- intel
    for p in ('in.fx', 'in.bg'): h.draw(p, p)
    v = st.get('intel', 300); lo, hi = st.get('tier_lo', 200), st.get('tier_hi', 400); tier = st.get('tier', 1)
    h.draw('in.gauge', 'in.gauge', ramp='in.gauge#r', thr=v / hi, mul=CYAN, add=CYAN_ADD)
    for p in ('in.a45', 'in.a46', 'in.t47', 'in.deco'): h.draw(p, p)
    if st.get('cool') is not None: h.draw('in.cool', 'in.cool', ramp='in.cool#r', thr=st['cool'], mul=(1, 1, 1, 1), add=(0, 0, 0, 0))
    ready = st.get('cool') is None
    h.draw('skill.%d' % st.get('skill', 8), 'in.skill', mul=(1, 1, 1, 1) if ready else (0.55, 0.55, 0.55, 1))
    h.draw('in.max', 'in.max'); h.number('in.m', hi, 4)
    GREY = (0.62, 0.62, 0.62, 0.8)
    h.text('in.t1', 0, -h.asc, 1, 'TIER %d' % tier, align=2, mul=GREY); h.number('in.lo', lo, 3, mul=GREY)
    h.text('in.tier', 0, -h.asc, 1, 'TIER', align=2)
    h.draw('in.btn', 'in.badge', mul=CYAN, add=CYAN_ADD); h.text('in.badgetxt', 0, -h.asc, 1, str(tier), align=2, mul=(0.05, 0.1, 0.15, 1))
    h.number('in.v', v, 4); h.draw('white', 'in.line', mul=(0.7, 0.7, 0.7, 0.7))
    h.draw('in.btn', 'in.btn'); h.field_text('in.btntxt', st.get('key', 'Q'))
    # ---- score / timer
    for p in ('sc.d1', 'sc.d11', 'sc.d301'): h.draw(p, p)
    h.number('scL.n', st.get('scoreL', 500), 5); h.number('scR.n', st.get('scoreR', 700), 5, left=True); h.number('scG.n', 10000, 5)
    t = st.get('time', 436); h.draw('tm.colon', 'tm.colon')
    h.number('tmM.n', t // 60, 2, lead_zero=True); h.number('tmS.n', t % 60, 2, lead_zero=True)
    # ---- crosshair
    if not st.get('ads'):
        spr = st.get('spread', 0.0)
        for s_, d in (('T', (0, -1)), ('B', (0, 1)), ('L', (-1, 0)), ('R', (1, 0))):
            h.draw('aim.' + s_, 'aim.' + s_, A=T(d[0] * spr, d[1] * spr))
    if st.get('hit'):
        for s_ in ('RT', 'LT', 'RB', 'LB'): h.draw('hm.' + s_, 'hm.' + s_)
    # ---- magazine at the gun anchor (screen px)
    if 'mag' in st:
        ax, ay = st['mag_at']; Hn = h.node['mg.anchor'][0]
        o = Hn @ np.array([0, 0, 1.0]); o = o[:2] / o[2]
        sh = np.array([[1, 0, ax - o[0]], [0, 1, ay - o[1]], [0, 0, 1.0]])
        saved = {k: v for k, v in h.node.items() if k.startswith('mg')}
        for k, (H, mu, ad) in saved.items(): h.node[k] = (sh @ H, mu, ad)
        mag, res, cap = st['mag'], st['reserve'], st.get('cap', 50)
        h.number('mgB.n', mag, 5, glyphs='n26'); h.number('mgA.n', res, 5, glyphs='n26', left=True)
        h.draw('mg.d173', 'mg.d173'); h.draw('mg.gauge', 'mg.gauge', ramp='mg.gauge#r', thr=mag / cap)
        h.draw('mg.d413', 'mg.d413'); h.draw('mg.d85', 'mg.d85')
        h.field_text('mg.name', st.get('wname', 'M4A1'))
        h.node.update(saved)
    for k, (kn, kc, wpn, vn, vc) in enumerate(st.get('feed', [])):
        nd = 'kr%d' % k
        h.text(nd, -190, 4 + 4 + h.asc * 12, 12, kn, align=1, mul=kc)
        h.draw('wp.%d' % wpn, nd, A=np.array([[-0.3, 0, -122], [0, 0.3, 4], [0, 0, 1.0]]), mul=(0, 0, 0, 1), add=(1, 1, 1, 0))
        h.text(nd, -117, 4 + 4 + h.asc * 12, 12, vn, align=0, mul=vc)
    for k, (ic, name, act) in enumerate(st.get('chips', [])):
        a = 1.0 if act else 0.45
        h.draw('chipi.%d' % ic, 'chip%d.icon' % (k + 1), mul=(1, 1, 1, a) if act else (0.7, 0.7, 0.7, a))
        h.text('chip%d.row' % (k + 1), 0, -h.asc * 15, 15, name, align=1, mul=(1, 1, 1, a) if act else (0.75, 0.75, 0.75, a))
    if st.get('kill'):
        h.field_text('kn.txt', st['kill']); h.field_text('kp.txt', '+100')
BOXES = {'feed': (900, 0, 1280, 80), 'chips': (1100, 420, 1280, 560), 'intel': (0, 540, 220, 720), 'hp': (1060, 540, 1280, 720), 'score': (480, 0, 800, 80), 'mag': (680, 460, 860, 540)}
def compare(h, out, rows, boxes, zoom=3):
    FOOT = '/mnt/user-data/uploads/vita-agent-use/gitsfa/work/footage/'
    tiles = []
    for f, st in rows:
        foot = Image.open(FOOT + f + '.jpg').convert('RGB')
        bgd = np.asarray(foot, np.float32) / 255 * 0.35
        h.clear(); draw_state(h, st); ours = h.image(bgd[:M.VH, :M.VW])
        if '--blend' in sys.argv: ours = Image.blend(foot, h.image(np.zeros((720, 1280, 3), np.float32)), 0.5)
        for b in boxes:
            tiles.append((foot.crop(BOXES[b]), ours.crop(BOXES[b])))
    W = max(a.size[0] for a, _ in tiles); Hh = sum(a.size[1] for a, _ in tiles)
    im = Image.new('RGB', (W * 2, Hh)); y = 0
    for a, b in tiles: im.paste(a, (0, y)); im.paste(b, (W, y)); y += a.size[1]
    im.resize((im.size[0] * zoom, im.size[1] * zoom)).save(out)
CHIPS = [(40, 'RAPID AIM', 0), (39, 'RAPID RELOAD', 0), (17, 'SILENT CAMO', 0), (18, 'EXTENDED MEMORY', 0), (7, 'ACTIVE CAMO', 0)]
FEED = [('Genroa', (1, 1, 1, 1), 2, 'Waifu Delta', (243/255, 43/255, 62/255, 1)), ('Yuzuru', (243/255, 43/255, 62/255, 1), 2, 'Gemma', (1, 1, 1, 1))]
ROWS = [('f_000200', dict(hp=100, ap=100, intel=300, scoreL=400, scoreR=400, time=486, mag=50, reserve=72, mag_at=(750, 500), chips=CHIPS, feed=FEED)),
        ('f_000250', dict(hp=14, ap=0, intel=300, cool=0.6, scoreL=500, scoreR=700, time=436, ads=1, mag=50, reserve=81, mag_at=(630, 650), kill='NICE KILL'))]
if __name__ == '__main__' and '--foot' in sys.argv:
    M.set_stage(1920, 1080, 2 / 3)
    h = Hud('/tmp/claude-0/hud/hud_foot.ghud')
    compare(h, '/tmp/claude-0/hud/cmp.png', ROWS, [b for b in sys.argv[2:] if not b.startswith('--')] or ['intel', 'hp'])
elif __name__ == '__main__':
    h = Hud(sys.argv[1] if len(sys.argv) > 1 else '/tmp/claude-0/hud/hud.ghud')
    FOOT = '/mnt/user-data/uploads/vita-agent-use/gitsfa/work/footage/'
    rows = []
    for f, st in (('f_000200', dict(hp=100, ap=100, intel=300, scoreL=400, scoreR=400, time=486, mag=50, reserve=72, mag_at=(565, 330))),
                  ('f_000250', dict(hp=14, ap=0, intel=300, cool=0.6, scoreL=500, scoreR=700, time=436, ads=1, mag=50, reserve=81, mag_at=(480, 430), kill='NICE KILL'))):
        foot = Image.open(FOOT + f + '.jpg').convert('RGB').resize((960, 540))
        bg = np.zeros((544, 960, 3), np.float32); bg[:540] = np.asarray(foot, np.float32) / 255
        h.clear(); draw_state(h, st); ours = h.image(bg * 0.8)
        im = Image.new('RGB', (1920, 544)); im.paste(foot, (0, 0)); im.paste(ours, (960, 0)); rows.append(im)
    out = Image.new('RGB', (1920, 544 * len(rows)))
    for i, r in enumerate(rows): out.paste(r, (0, 544 * i))
    out.save('/tmp/claude-0/hud/demo.png'); print('ok')
