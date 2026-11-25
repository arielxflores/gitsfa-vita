# Builds hud.ghud for the Vita engine from the game's own Scaleform HUD (InGameUI.swf, InGameUI_D.swf).
# Layout: the movies' own (the user's game files are the final version, the one in the 2016 gameplay videos: HP / AP
# with the operator portrait, two-ring skill gauge, hexagon chips); the round-mode parts (score_DM with player slots,
# roundRe bands, plant bar, site markers: tools/dem/make_dem_hud.py) are merged in, so one pack serves every mode.
import sys, os, re, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', 'dem'))
import hudmv as M
from hudbuild import Builder
from render import CX0
from PIL import Image
args = [a for i, a in enumerate(sys.argv[1:]) if not a.startswith('--') and sys.argv[i] != '--atlas']
OUT = args[0] if args else os.path.join(HERE, '..', '..', 'build', 'data', 'hud.ghud')
mv = M.main(); dm = M.dmovie()
if '--foot' in sys.argv: M.set_stage(1920, 1080, 2 / 3)     # the footage's layout (1080p, shown at 720p) for 1:1 checks
B = Builder()
B.oversample = 1.0; B.ramp_scale = 0.6        # (legibility made the parts bigger: keep the atlas at 1024 x 1024)
B.dens_k.update({'mm.back': 0.3, 'dm.emc': 0.35, 'dm.set.bg': 0.5, 'sc.d1': 0.5, 'dm.sc.d1': 0.5, 'sc.d11': 0.5, 'dm.sc.d11': 0.5, 'rr.d7': 0.5, 'rr.d11': 0.5, 'dm.bt.bg': 0.5})   # soft bands, plain fills
def T(dx, dy, s=1.0): return np.array([[s, 0, dx], [0, s, dy], [0, 0, 1.0]])
def hide_prefix(*prefixes):
    return lambda p: any(p == x or p.startswith(x + '.') for x in prefixes)
def keep_icon(path, k, prefix='icon'):
    pat = re.compile(re.escape(path) + r'\.' + prefix + r'(\d+)(\.|$)')
    def h(p):
        m = pat.match(p); return bool(m) and int(m.group(1)) != k
    return h
def max_scale(nodes, loc):
    return max(M.jac_scale(B.nodes[n][0], loc) for n in nodes) * 1.1
# ------------------------------------------------------------------ legibility (BRIEF.md: >= 24 px labels, >= 20 px fine print,
# big numbers >= ~17 px cap on the Vita's 960x544): each widget keeps the movie's own layout and perspective, scaled about
# its screen anchor (ROOT_SCALE), and the small labels inside grow about their own centre (grow) or get a bigger font
# (FIELD_K, applied to the text fields at the end). tools/ui/host/run.sh + legib.py check the result.
HP = 'hpapMC.hpapMC'
IT = 'intelMC.intelMC'
LEGIBLE = '--orig' not in sys.argv          # --orig: the movies' own sizes (for 1:1 checks against the footage)
FIELD_K = {}; FIELD_D = {}                  # font factor, extra offset (field units) per text field
KR_PITCH, KR_TEXT = 37.55, 12               # kill feed: row pitch and name size (kill-plane units)
if LEGIBLE:
    # b93 (user, 2026-11-18, with a PC screenshot: "resize and adjust the hud elements to appear more like in this screenshot while
    # still being readable", the bottom-left sizes and spacing especially; the HP widget sat over the gun): the corner widgets
    # where the PC draws them - the screenshot's skill-ring and portrait centres, in Vita pixels (PC_AT) - at the PC's size
    # times PC_K (a little bigger, for the 5" screen), scaled about their screen corner; the small ring tucked against the big
    # one as on the PC (it was pushed 24 px right, b55); the labels inside grow to stay readable
    PC_AT = {'intelMC': ((57.6, 489.1), (0, M.VH), IT + '.t2disMC'), 'hpapMC': ((898.0, 492.1), (M.VW, M.VH), HP + '.chaicon')}   # (t2disMC: the inner disc's mark)
    PC_K = {'intelMC': 1.15, 'hpapMC': 1.05}
    PC_R1 = (48.7, 5.2, 15.6)               # the PC's small ring: centre from the big ring's, radius (screenshot, Vita px)
    R1_R = 25.0                             # ours (its "TIER 1" must stay readable): ~1.5x the PC's
    PC_COST = (39.1, -43.1)                 # the PC's "400" (Tier-2 cost): centre from the big ring's
    PC_BTN2 = (6.4, 41.5)                   # the PC's key badge under the big ring: centre from the big ring's
    if os.environ.get('HUD_PC1'): PC_K = {'intelMC': 1.0, 'hpapMC': 1.0}; R1_R = PC_R1[2]   # (check: the PC's own sizes over the screenshot)
    M.ROOT_SCALE.update({'score_TDM': 1.2, 'score_DM': 1.2, 'minimap': 1.05, 'bombset': 1.7, 'skillGauge': 1.1,
                         'installMSG': 1.8, 'roundRe': 1.25, 'magazineMC': 1.0, 'bomb': 1.3})
    PC_LIFT = 10.0                          # b95 (user, on the Vita: the corner widgets looked cut off at the screen's bottom edge): raised
    for nm_, (at_, cn_, path_) in PC_AT.items():
        k_ = PC_K[nm_]; M.ROOT_SCALE[nm_] = k_
        M.place_root(mv, nm_, path_, (cn_[0] + (at_[0] - cn_[0]) * k_, cn_[1] + (at_[1] - cn_[1]) * k_ - PC_LIFT))
    for sc_ in ('score_TDM', 'score_DM'): M.grow_group(mv, sc_, ('timeMc', 'timeMcWarning', 'd11'), 1.35, 'd11')
    M.grow(mv, 'score_TDM.ruleMCodd', 1.27); M.grow(mv, 'score_DM.ruleMC', 1.27)
    for k_ in range(1, 7):                  # score_DM: the player slots make room for the bigger timer
        M.ovr('score_DM.user.ally%d' % k_, -20, 0); M.ovr('score_DM.user.enemy%d' % k_, 20, 0)
    M.grow(mv, 'score_DM.bombtime', 1.15)
    for v_ in ('apde', 'apwarning'): M.grow(mv, HP + '.apMC.%s.defMC' % v_, 1.3); M.grow(mv, HP + '.apMC.%s.defIcon' % v_, 1.9, anchor='r')
    for v_ in ('hpde', 'hpwarning'): M.grow(mv, HP + '.hpMC.%s.hpIcon' % v_, 1.7, anchor='r')
    _b = M.screen_centre(mv, IT + '.t2disMC'); _k = PC_K['intelMC']; _d = np.array(PC_R1[:2]); _d = _d / np.linalg.norm(_d)
    _r1 = M.screen_centre(mv, IT + '.cooltBack'); _r1r = 0.5 * M.ROOT_SCALE['intelMC'] * 42.2   # (t1basic: 42 px across at scale 1)
    _at = _b + np.array(PC_R1[:2]) * _k + _d * (R1_R - PC_R1[2] * _k)   # the same overlap of the big ring's outer arc as the PC's
    M.move_group(mv, IT, ('t1basic', 'cooltBack', 't1disMC', 'coolTime', 'cooltimeCount', 'inteltxt', 'skillbtn1'), R1_R / _r1r, 'cooltBack', _at)
    M.move_group(mv, IT, ('intelGaugevalue',), 1.5, 'intelGaugevalue', _b + np.array(PC_COST) * _k)   # the Tier-2 cost by the gauge's arrow, as the PC
    M.move_group(mv, IT, ('skillbtn2',), 1.0, 'skillbtn2', _b + np.array(PC_BTN2) * _k)                # the big ring's key badge, clear of the screen edge as the PC's
    KR_PITCH, KR_TEXT = 58.0, 46
    M.ovr('roundRe.alMc', 0, 55)            # the bigger result band stays clear of the score panel and Demolition's bomb row under it (footage: a gap under them)
    FIELD_D.update({'kp.txt': (0, 24), 'mg.name': (0, -13)})   # '+100' under the bigger 'NICE KILL'; the weapon name above the reserve count
    FIELD_K.update({'in.sktxt': 1.73, 'in.r1txt': 1.85, 'kn.txt': 1.75, 'kp.txt': 1.65, 'mg.name': 2.8, 'dm.emctxt': 1.75,
                    'dm.set.txt': 1.2, 'dm.set.al': 1.2, 'dm.set.hold': 1.3, 'dm.set.keytxt': 1.1, 'dm.im.t1': 1.25, 'dm.im.t2': 1.25,
                    'rr.round': 1.25, 'rr.sub': 1.35, 'rr.typeA': 1.35, 'rr.typeE': 1.35, 'ch.round': 1.25, 'ch.sub': 1.35, 'wm.dist': 2.0})
# ------------------------------------------------------------------ HP / AP widget (bottom right)
print('hp')
for nm, p in (('hp.d1', 'd1'), ('hp.disc', '__id30_'), ('hp.arc31', '__id31_'), ('hp.arc32', '__id32_'), ('hp.back', 'hpback'), ('hp.ticks', '__id33_')):
    B.part(nm, HP + '.' + p)
B.part('hp.deco', HP + '.indecoMC', hide=hide_prefix(HP + '.indecoMC.icon2'))
B.image('hp.decoW', HP + '.indecoMC', B.density(B.nodes['hp.deco'][0], HP + '.indecoMC', mv, None)[0], hide=hide_prefix(HP + '.indecoMC.icon1'))
B.node('hp.chr', HP + '.chaicon')                                   # operator portrait (icon1..12, game order)
csc = B.density(B.nodes['hp.chr'][0], HP + '.chaicon', mv, keep_icon(HP + '.chaicon', 1))[0]
for k in range(1, 13): B.image('hp.chr.%d' % k, HP + '.chaicon', csc, hide=keep_icon(HP + '.chaicon', k))
B.ramp('hp.gauge', HP + '.hpgauge', 'icon', 100)
sc = B.density(B.nodes['hp.gauge'][0], HP + '.hpgauge', mv, keep_icon(HP + '.hpgauge', 100))[0]
B.image('hp.gaugeR', HP + '.hpgauge', sc, hide=keep_icon(HP + '.hpgauge', 20))
B.ramp('ap.gauge', HP + '.apgauge', 'icon', 20)
B.part('hp.label', HP + '.hpMC.hpde.hpIcon'); B.part('hp.labelW', HP + '.hpMC.hpwarning.hpIcon')
B.part('ap.label', HP + '.apMC.apde.defIcon')
B.digits('hp.n', HP + '.hpMC.hpde.hpMC', ['no1', 'no2', 'no3']); B.digits('hp.z', HP + '.hpMC.hpde.hpMC', ['no1', 'zero2', 'zero3'])
B.digits('hpW.n', HP + '.hpMC.hpwarning.hpMC', ['no1', 'no2', 'no3']); B.digits('hpW.z', HP + '.hpMC.hpwarning.hpMC', ['no1', 'zero2', 'zero3'])
B.digits('ap.n', HP + '.apMC.apde.defMC', ['no1', 'no2', 'no3']); B.digits('ap.z', HP + '.apMC.apde.defMC', ['no1', 'zero2', 'zero3'])
# ------------------------------------------------------------------ intel / skill gauge (bottom left)
print('intel')
B.node('in.plane', IT)
for nm, p in (('in.fx', 'intelFX'), ('in.bg', '__id44_'), ('in.a45', '__id45_'), ('in.a46', '__id46_'), ('in.t47', '__id47_'), ('in.deco', 'insideDecoMC')):
    B.part(nm, IT + '.' + p)
B.ramp('in.gauge', IT + '.intelGauge.plusGauge', 'icon', 100)
B.ramp('in.cool', IT + '.coolTime', 'icon', 50)
B.node('in.skill', IT + '.skillMC.skillIcon')
sk_scale = None
for k in (1, 2, 3, 5, 8, 9, 11, 12, 13, 16, 17, 18, 19):
    pth = IT + '.skillMC.skillIcon'
    if sk_scale is None: sk_scale = B.density(B.nodes['in.skill'][0], pth, mv, keep_icon(pth, k))[0]
    B.image('skill.%d' % k, pth, sk_scale, hide=keep_icon(pth, k))
B.part('in.btn', IT + '.skillbtn1.keyTxtFirst', hide=hide_prefix(IT + '.skillbtn1.keyTxtFirst.keyTXT'))
B.field('in.btntxt', IT + '.skillbtn1.keyTxtFirst.keyTXT.txt')
B.part('in.btn2', IT + '.skillbtn2.keyTxtSecond', hide=hide_prefix(IT + '.skillbtn2.keyTxtSecond.keyTXT'))
B.field('in.btn2txt', IT + '.skillbtn2.keyTxtSecond.keyTXT.txt')
# the second (small) ring: tier-1 / cooldown state - ring, back, cooldown count, disabled mark
B.part('in.t2dis', IT + '.t2disMC'); B.part('in.r1', IT + '.t1basic'); B.part('in.r1back', IT + '.cooltBack', hide=hide_prefix(IT + '.cooltBack.icon50')); B.part('in.r1dis', IT + '.t1disMC')
B.digits('in.cc', IT + '.cooltimeCount', ['no1', 'no2'])
B.field('in.sktxt', IT + '.skillMCTXT.icon1.txt'); B.field('in.r1txt', IT + '.inteltxt.icon1.txt')   # "TIER 2" over the skill icon, "TIER 1" in the small ring
# Vita D-pad glyph for the skill badges (the skill is on D-pad right; drawn in the badge text field's plane)
# b107 (the user: "the dpad prompts look really small on the bottom left hud elements ... just use the rectangular hexagon
# right button with a triangle inside part of the dpad as a prompt instead"): ONE arm of the D-pad, large - the right arm's
# own shape (pointed towards the pad's centre, rounded outside) with the hollow triangle the Vita prints on it. It takes
# the place of the PC's small key square (in.btn): 'btn.arm' = the dark body, 'btn.dpr' = its rim + triangle (tinted by
# the engine: cyan / yellow when the skill can be used, dim otherwise). Before: a whole 22 px D-pad inside the square, the
# lit arm 3 px wide on the Vita. BTN_ARM_W x BTN_ARM_H are movie units (x 0.5 on the Vita's screen: 23 x 17 px there).
# (b107 shipped it 80 x 58 = 41 x 33 px; the user, the same day, with the host picture of the first try: "please decrease the
# size of these button prompts to look more like you have them in this picture, they do not need to be as big" -> this size,
# centred on the PC's key square again.)
from PIL import Image as _I, ImageDraw as _D
BTN_ARM_W, BTN_ARM_H, _SS = 46, 34, 8
def _arm(kind):
    W_, H_ = BTN_ARM_W * _SS, BTN_ARM_H * _SS; t = _I.new('L', (W_, H_), 0); d = _D.Draw(t); u = _SS
    r = 7 * u; x0, x1, y0, y1, px = 2 * u, W_ - 2 * u, 2 * u, H_ - 2 * u, 13 * u   # the point's depth
    import math as _m
    arc = lambda cx_, cy_, a0, a1: [(cx_ + r * _m.cos(_m.radians(a)), cy_ + r * _m.sin(_m.radians(a))) for a in range(a0, a1 + 1, 6)]
    body = [(x0, H_ // 2), (x0 + px, y0)] + arc(x1 - r, y0 + r, -90, 0) + arc(x1 - r, y1 - r, 0, 90) + [(x0 + px, y1)]
    if kind == 'body': d.polygon(body, fill=255)
    else:
        w = int(2.2 * u); d.line(body + [body[0], body[1]], fill=255, width=w, joint='curve')
        cx, cy, th, tw = int(W_ * 0.63), H_ // 2, 8.5 * u, 11 * u   # the triangle: hollow, pointing out
        tri = [(cx - tw * 0.45, cy - th), (cx + tw * 0.55, cy), (cx - tw * 0.45, cy + th)]
        d.line(tri + [tri[0], tri[1]], fill=255, width=int(2.4 * u), joint='curve')
    return np.asarray(t.resize((BTN_ARM_W * 2, BTN_ARM_H * 2), _I.LANCZOS), np.float32) / 255
_ab, _ag = _arm('body'), _arm('glyph')
_abody = np.zeros(_ab.shape + (4,), np.float32); _abody[..., 3] = _ab * 0.9; _abody[..., 0], _abody[..., 1], _abody[..., 2] = 0.055 * _abody[..., 3], 0.065 * _abody[..., 3], 0.085 * _abody[..., 3]
for _nm, _an, _f, _dy in (('btn.dpr', 'btn.arm', 'in.btntxt', 0), ('btn.dpr2', 'btn.arm2', 'in.btn2txt', 0)):
    _fb = B.fields[_f]['bounds']; _cx, _cy = (_fb[0] + _fb[1]) / 2, (_fb[2] + _fb[3]) / 2 + _dy
    _loc = (_cx - BTN_ARM_W / 2, _cy - BTN_ARM_H / 2, _cx + BTN_ARM_W / 2, _cy + BTN_ARM_H / 2)
    B.add_image(_an, _abody, _loc); B.add_image(_nm, np.repeat(_ag[..., None], 4, 2), _loc)
B.part('in.max', IT + '.intelGaugevalue.lv2rule.maxvalue')
B.digits('in.m', IT + '.intelGaugevalue.lv2rule.intelValue', ['no1', 'no2', 'no3', 'no4'])
# ------------------------------------------------------------------ the centre skill gauge (b95: the PC's skillGauge - the running skill's
# icon over a framed bar that drains, under the crosshair; the Paz video EP19 5:09-5:15, a Hyper Sprint; was a plain 49 x 5 bar)
print('skill gauge')
SG = 'skillGauge'
B.part('sg.bg', SG + '.gauge.d1')
B.part('sg.fill', SG + '.gauge.skillG.gauge', hide=hide_prefix(SG + '.gauge.skillG.gauge.icon2'))
B.node('sg.icon', SG + '.skillIcon')
_sgs = B.density(B.nodes['sg.icon'][0], SG + '.skillIcon', mv, keep_icon(SG + '.skillIcon', 1))[0]
for k in (1, 2, 3, 5, 8, 9, 11, 12, 13, 14, 16, 17, 18, 19):
    B.image('sgi.%d' % k, SG + '.skillIcon', _sgs, hide=keep_icon(SG + '.skillIcon', k))
# ------------------------------------------------------------------ score + timer (top centre)
print('score')
SC = 'score_TDM'
for p in ('d1', 'd3', 'd7', 'd11', 'd301'): B.part('sc.' + p, SC + '.' + p)
B.digits('scL.n', SC + '.lefrScoreMc.scoreMc', ['no%d' % i for i in range(1, 6)])
B.digits('scLB.n', SC + '.lefrScoreMc.scoreMcB', ['no%d' % i for i in range(1, 6)])
B.digits('scR.n', SC + '.rightScoreMc.scoreMc', ['no%d' % i for i in range(1, 6)])
B.digits('scG.n', SC + '.ruleMCodd', ['no%d' % i for i in range(1, 6)])
for t in ('timeMc', 'timeMcWarning'):
    pre = 'tm' if t == 'timeMc' else 'tmW'
    B.part(pre + '.colon', SC + '.%s.d1' % t)
    B.digits(pre + 'M.n', SC + '.%s.minMC' % t, ['no1', 'no2']); B.digits(pre + 'S.n', SC + '.%s.secMC' % t, ['no1', 'no2'])
# ------------------------------------------------------------------ crosshair, hit marker, damage direction, kill notify
print('aim')
for s_ in 'TBLR':
    B.part('aim.' + s_, 'aim.aim' + s_, mv=dm, hide=hide_prefix('aim.aim%s.icon2' % s_))
    sc = B.density(B.nodes['aim.' + s_][0], 'aim.aim' + s_, dm, None)[0]
    B.image('aim.%s2' % s_, 'aim.aim' + s_, sc, mv=dm, hide=hide_prefix('aim.aim%s.icon1' % s_))
for s_ in ('RT', 'LT', 'RB', 'LB'): B.part('hm.' + s_, 'hitMarker.hitMarkerMC.hitMarker' + s_)
B.part('bs', 'beshot.beshotArrow')
B.field('kn.txt', 'killNotify.killnotifyTXT.txt'); B.field('kp.txt', 'killPoint.pointSum.txt')
# ------------------------------------------------------------------ magazine (floats at the gun's hud1 point; node origin = anchor)
print('magazine')
MG = 'magazineMC.magazineB'
for p in ('d85', 'd173', 'd413'): B.part('mg.' + p, MG + '.' + p, mv=dm)
B.ramp('mg.gauge', MG + '.gaugeMC', 'icon', 10, mv=dm)
sc = B.density(B.nodes['mg.gauge'][0], MG + '.gaugeMCW', dm, None)[0]
B.node('mg.gaugeW', MG + '.gaugeMCW', mv=dm)
B.digits('mgB.n', MG + '.blMC.basicB', ['no%d' % i for i in range(1, 6)], mv=dm)
B.digits('mgBW.n', MG + '.blMC.warnB', ['no%d' % i for i in range(1, 6)], mv=dm)
B.digits('mgA.n', MG + '.allCountMC.basicB', ['no%d' % i for i in range(1, 6)], mv=dm)
B.digits('mgAW.n', MG + '.allCountMC.warnB', ['no%d' % i for i in range(1, 6)], mv=dm)
B.field('mg.name', MG + '.wpName.txt', mv=dm)
B.node('mg.anchor', 'magazineMC', mv=dm)
# ------------------------------------------------------------------ vignette (dark corners), screen-space node
print('vignette, kill feed, chips')
B.part('vin', 'vinetRT', scale=0.3); B.node('vin.RT', 'vinetRT')
# fill: each vignette corner is drawn as a fan over its visible part only (63 % of the quad is fully transparent): the polygon,
# star-shaped from the image's opaque corner, goes into nodes vin.poly0..3 (4 points each in H[0..7], count in vin.poly0 H[8])
def _vin_poly(nm, t=2, nband=12):
    """visible part of a corner gradient: rows scanned for the first texel over t from the transparent side; each band of rows
    gets the leftmost boundary of the band at both ends (the polygon never cuts a visible texel); fan from the opaque corner"""
    im = B.images[nm]; a = im['img'][..., 3] * 255; h, w = a.shape; l = im['loc']
    top = a[: h // 2].sum() >= a[h // 2:].sum(); right = a[:, w // 2:].sum() >= a[:, : w // 2].sum()
    b = a[:: 1 if top else -1, :: 1 if right else -1]   # flipped so the opaque corner is the top-right
    xl = np.full(h, float(w))
    for y in range(h):
        c = np.nonzero(b[y] > t)[0]
        if len(c): xl[y] = c.min()
    rows = np.nonzero(xl < w)[0]; y1 = rows.max() + 1 if len(rows) else h
    ys = np.linspace(0, y1, nband + 1); pts = [(w, 0.0)]
    for k in range(nband + 1):
        y0, y2 = (int(np.floor(ys[k - 1])) if k else 0), min(h, int(np.ceil(ys[k + 1])) if k < nband else h)
        x = max(0.0, xl[max(0, y0):max(y0 + 1, y2)].min() - 1); pts.append((x, ys[k] if k else 0.0))
    pts.append((w, float(y1)))
    if not top: pts = [(x, h - y) for x, y in pts]
    if not right: pts = [(w - x, y) for x, y in pts]
    return [(l[0] + x / w * (l[2] - l[0]), l[1] + y / h * (l[3] - l[1])) for x, y in pts]
_vp = _vin_poly('vin'); print('  vignette fan: %d points' % len(_vp))
for _k in range(4):
    _q = (_vp[_k * 4:_k * 4 + 4] + [(0, 0)] * 4)[:4]
    B.nodes['vin.poly%d' % _k] = (np.array([[_q[0][0], _q[0][1], _q[1][0]], [_q[1][1], _q[2][0], _q[2][1]], [_q[3][0], _q[3][1], float(len(_vp)) if _k == 0 else 0]]), np.ones(4, np.float32), np.zeros(4, np.float32))
for c_ in ('LT', 'RD', 'LD'): B.node('vin.' + c_, 'vinet' + c_)
B.nodes['screen'] = (np.eye(3), np.ones(4, np.float32), np.zeros(4, np.float32))
# ------------------------------------------------------------------ kill feed: 5 rows (each row: the tilted 'kill' plane of a killrecord)
KR = 'killrecord.krMC.krecord'
for k in range(5):
    M.ovr(KR, -46, -5.1 + KR_PITCH * k)    # rows as laid out in the footage (legible: a wider pitch for the bigger names)
    B.node('kr%d' % k, KR + '.kill')
del M.OVR[KR]
rm = M.rmovie(); inv = {v: k for k, v in rm.sym.items()}
WP = '#%d' % inv['WpIconMC']
KR_ICON = 0.3 * KR_TEXT / 12 * 0.55 if LEGIBLE else 0.3     # weapon icon scale in the row (icon units -> plane units)
B.vnode('kr.style', 'screen', [[KR_TEXT, KR_ICON, 0], [0, 1, 0], [0, 0, 1]])   # read by hud_game.h: name size, icon scale
wscale = M.jac_scale(B.nodes['kr0'][0], (-200, -10, 0, 30)) * KR_ICON * 1.3
WP_USED = {2, 41, 51}                     # kill-feed icons the game can show: the weapons table's wpicon column + defaults
for ln_ in open(os.environ.get('GITSFA_WEAPONS_TXT') or os.path.join(HERE, '..', 'databuilder', 'params', 'weapons.txt')):
    f_ = ln_.split('|')
    if len(f_) > 3 and not ln_.startswith('#') and f_[3].strip().isdigit(): WP_USED.add(int(f_[3]))
for nm_, _ in rm.children(WP):
    if nm_[4:].isdigit() and int(nm_[4:]) in WP_USED: B.image('wp.' + nm_[4:], WP + '.' + nm_, wscale, mv=rm)
# ------------------------------------------------------------------ chips: hexagon icons above the HP widget (model.chipAL)
CH = 'model.chipAL'
# the footage's (2016) layout: two rows of hexagons (720p centres), upper row = the chips in effect, lower row the others
CHIP_AT = [(1205, 512), (1240, 512), (1180, 540), (1215, 540), (1250, 540)]
for k, (x7, y7) in enumerate(CHIP_AT):
    X = (x7 * 1.5 + (M.SW - 1920)) * M.K; Y = (y7 * 1.5 + (M.SH - 1080)) * M.K; s_ = 30 / 64 * 1.5 * M.K   # ~30 px (720p) hexagons
    g_ = M.ROOT_SCALE.get('hpapMC', 1.0); X = M.VW - 5 + (X - M.VW + 5) * g_; Y = M.VH - 15 + (Y - M.VH + 15) * g_; s_ *= g_   # follow the HP widget's scale
    o_ = M.ROOT_OFF.get('hpapMC', (0, 0)); X += o_[0] * M.K; Y += o_[1] * M.K                                                  # and its move (b93)
    B.nodes['chip%d.icon' % (k + 1)] = (np.array([[s_, 0, X - 32 * s_], [0, s_, Y - 32 * s_], [0, 0, 1.0]]), np.ones(4, np.float32), np.zeros(4, np.float32))
    B.nodes['chip%d.row' % (k + 1)] = B.nodes['chip%d.icon' % (k + 1)]
CI = '#%d' % inv['Chipicons']
cscale = M.jac_scale(B.nodes['chip1.icon'][0], (0, 0, 64, 64)) * 1.3
for k in (7, 17, 39, 40, 41, 18, 32, 2, 52, 62, 64, 68, 75, 79, 26, 46, 48, 50):
    B.image('chipi.%d' % k, CI + '.icon%d' % k, cscale, mv=rm)
# ------------------------------------------------------------------ minimap frame (the map image itself is per map: <stem>.gmm)
MM = 'minimap.minimapMC'
B.part('mm.back', MM + '.mapback'); B.part('mm.line', MM + '.mline')
B.node('mm.view', MM + '.minimap')          # viewport (0,0)-(204,170) in this plane; the player sits at (102,85)
import gitspaths; _ic = np.asarray(Image.open(gitspaths.path('MINIMAP', 'minimap', 'Icons.png')).convert('RGBA'), np.float32) / 255
def _icon(name, col, row):
    t = _ic[row * 36:(row + 1) * 36, col * 36:(col + 1) * 36].copy(); t[..., :3] *= t[..., 3:4]   # premultiply
    B.add_image(name, t, (-18, -18, 18, 18))
for nm_, c_, r_ in (('mmi.player', 0, 1), ('mmi.ally', 13, 2), ('mmi.enemy', 11, 2), ('mmi.ping', 12, 2), ('mmi.A', 0, 2), ('mmi.B', 1, 2), ('mmi.Ar', 2, 2), ('mmi.Br', 3, 2)):
    _icon(nm_, c_, r_)
# ------------------------------------------------------------------ digit glyphs (sized for their largest use)
print('glyphs')
p30 = HP + '.hpMC.hpde.hpMC.no1'
loc = mv.render_flat(p30 + '.no8', 1.0, cx=CX0)[1]
n30 = [n for n in B.nodes if re.search(r'\.[nmz]\d$', n) and not n.startswith('mg')]
B.glyphset('n30', p30, max_scale(n30, loc))
B.image('n30.z', HP + '.hpMC.hpde.hpMC.zero2', max_scale(n30, loc), mv=mv)
p26 = MG + '.blMC.basicB.no1'
loc = dm.render_flat(p26 + '.no8', 1.0, cx=CX0)[1]
B.glyphset('n26', p26, max_scale([n for n in B.nodes if n.startswith('mg') and '.n' in n], loc), mv=dm)
B.add_image('white', np.ones((4, 4, 4), np.float32), (0, 0, 1, 1))
pcc = IT + '.cooltimeCount.no1'                                      # cooldown seconds (Font_Number_cooltime)
loc = mv.render_flat(pcc + '.no8', 1.0, cx=CX0)[1]
B.glyphset('nc', pcc, max_scale(['in.cc1', 'in.cc2'], loc))
# ------------------------------------------------------------------ round modes (Demolition, Elimination): score_DM with player slots,
# bomb timer, plant bar, round result / switching-sides bands, in-world site markers (tools/dem/make_dem_hud.py)
if '--no-round' not in sys.argv:
    print('round modes')
    import make_dem_hud
    make_dem_hud.add_dem(B, with_glyphs=False)
    # Elimination's zone tag under the round timer (score_DM.emcaution: "SAFE ZONE" / "ZONE LOCKED" in the footage)
    B.part('dm.emc', 'score_DM.emcaution.d1'); B.field('dm.emctxt', 'score_DM.emcaution.txt')
    # the Vita's Square button (plant / defuse) for the bar's key box: pink square in a light ring
    _t = _I.new('L', (96, 96), 0); _d = _D.Draw(_t); _d.ellipse((4, 4, 92, 92), outline=200, width=6); _d.rectangle((30, 30, 66, 66), outline=255, width=7)
    _a = np.asarray(_t.resize((48, 48), _I.LANCZOS), np.float32) / 255; _g = np.stack([_a * 0.93, _a * 0.62, _a * 0.85, _a], 2)
    _fb = B.fields['dm.set.keytxt']['bounds']; _cx, _cy = (_fb[0] + _fb[1]) / 2, (_fb[2] + _fb[3]) / 2; _r = (_fb[3] - _fb[2]) * 0.42
    B.add_image('btn.sq', _g.astype(np.float32), (_cx - _r, _cy - _r, _cx + _r, _cy + _r))
    # the Vita's L shoulder button (spectating: previous / next ally; mirrored for R): the button's outline - the outer top corner
    # rounded wide - over a dark fill, drawn at any size in screen pixels (hud_round.h rh_lr_glyph). Premultiplied.
    _W, _H, _R0, _r1 = 128, 80, 44, 12; _y, _x = np.mgrid[0:_H, 0:_W].astype(np.float32) + 0.5
    def _inside(m):   # m: inset in px
        x0, y0, x1, y1 = m, m, _W - m, _H - m
        ok = (_x >= x0) & (_x <= x1) & (_y >= y0) & (_y <= y1)
        for cx_, cy_, rr_ in ((x0 + _R0 - m, y0 + _R0 - m, _R0 - m), (x1 - _r1, y0 + _r1, _r1), (x1 - _r1, y1 - _r1, _r1), (x0 + _r1, y1 - _r1, _r1)):
            sx_ = (_x < cx_) if cx_ < _W / 2 else (_x > cx_); sy_ = (_y < cy_) if cy_ < _H / 2 else (_y > cy_)
            ok &= ~(sx_ & sy_ & ((_x - cx_) ** 2 + (_y - cy_) ** 2 > rr_ ** 2))
        return ok.astype(np.float32)
    _o, _i = _inside(1.0), _inside(7.0); _ring = _o - _i; _fa = _i * 0.55
    _g = np.stack([_ring + _fa * 0.05, _ring + _fa * 0.06, _ring + _fa * 0.08, _ring + _fa], 2)
    _g = np.stack([np.asarray(_I.fromarray(_g[..., c], 'F').resize((64, 40), _I.LANCZOS)) for c in range(4)], 2).clip(0, 1)
    _g[..., :3] = np.minimum(_g[..., :3], _g[..., 3:])   # (the filter's overshoot: keep it premultiplied)
    B.add_image('btn.lsh', _g.astype(np.float32), (0, 0, 64, 40))
for f_, k_ in FIELD_K.items():             # bigger fonts, centred where the original text sat
    F_ = B.fields[f_]; s0 = F_['size']; F_['size'] = s0 * k_; b_ = list(F_['bounds'])
    d_ = (M.FONT['asc'] + M.FONT.get('desc', 0)) / 1024.0 * (F_['size'] - s0) / 2; b_[2] -= d_; b_[3] += d_
    ox_, oy_ = FIELD_D.get(f_, (0, 0)); b_[0] += ox_; b_[1] += ox_; b_[2] += oy_; b_[3] += oy_; F_['bounds'] = tuple(b_)
B.font(44)
a0, a1 = B.write(OUT)
print('wrote', OUT, 'atlas', a0.shape, 'font', a1.shape, 'images', len(B.images), 'nodes', len(B.nodes), 'fields', len(B.fields))
from PIL import Image
if '--atlas' in sys.argv:
    AD = sys.argv[sys.argv.index('--atlas') + 1]; os.makedirs(AD, exist_ok=True); Image.fromarray((np.clip(a0, 0, 1) * 255).astype(np.uint8)).save(AD + '/atlas0.png')
    Image.fromarray((np.clip(a1, 0, 1) * 255).astype(np.uint8)).save(AD + '/atlas1.png')
