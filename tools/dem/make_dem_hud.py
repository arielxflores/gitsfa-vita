# Demolition HUD pack (dem_hud.ghud) from the game's own Scaleform movies:
#   InGameUI.swf   score_DM (score panel with the bomb timer and the 6+6 player slots), bombset (plant/defuse bar),
#                  installMSG (can/can't plant prompt), roundRe (round result band + switching sides band)
#   InGameUI_D.swf bomb (in-world A/B site markers, attack and defense variants, direction arrow, distance)
# Same GHUD v1 format as hud.ghud (tools/ui/hudbuild.py), so the engine can load it as an extra pack (hud.h holds up to
# HUD_MAXPACK packs; hud_use(k) switches) - or call add_dem(B) from make_hud.py before B.font()/B.write() to merge it.
# usage: make_dem_hud.py [out.ghud] [--sheet preview.png]
import sys, re, numpy as np
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ui')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
import hudmv as M
from hudbuild import Builder
from render import CX0
from PIL import Image, ImageDraw

def hide_prefix(*prefixes):
    return lambda p: any(p == x or p.startswith(x + '.') for x in prefixes)
def only_icon(parent, k, prefix='icon'):
    pat = re.compile(re.escape(parent) + r'\.' + prefix + r'(\d+)(\.|$)')
    def h(p):
        m = pat.match(p); return bool(m) and int(m.group(1)) != k
    return h

def add_dem(B, with_glyphs=True):
    mv = M.main(); dm = M.dmovie()
    def part(name, path, hide=None, m=mv, node=True):
        return B.part(name, path, hide=hide, mv=m, node=node)
    def field(name, path, m=mv, **kw):
        B.field(name, path, mv=m, **kw)
    # ------------------------------------------------------------ score panel (top centre): replaces score_TDM in DEM
    SC = 'score_DM'
    for p in ('d1', 'd11', 'd81'): part('dm.sc.' + p, SC + '.' + p)        # d3/d7 (thin lines) render empty
    B.digits('dmL.n', SC + '.lefrScoreMc.scoreMc', ['no1']); B.digits('dmLB.n', SC + '.lefrScoreMc.scoreMcB', ['no1'])
    B.digits('dmR.n', SC + '.rightScoreMc.scoreMc', ['no1']); B.digits('dmRB.n', SC + '.rightScoreMc.scoreMcB', ['no1'])
    B.digits('dmG.n', SC + '.ruleMC', ['no1'])                       # rounds needed to win (the "8")
    for t, pre in (('timeMc', 'dmT'), ('timeMcWarning', 'dmTW')):     # round timer (normal / last seconds)
        part(pre + '.colon', SC + '.%s.d1' % t)
        B.digits(pre + 'M.n', SC + '.%s.minMC' % t, ['no1', 'no2']); B.digits(pre + 'S.n', SC + '.%s.secMC' % t, ['no1', 'no2'])
    # bomb timer row under the round timer (shown while the bomb is planted)
    BT = SC + '.bombtime'
    part('dm.bt.bg', BT + '.d1'); part('dm.bt.icon', BT + '.d7'); part('dm.bt.colon', BT + '.timeMc.d1')
    B.digits('dmBM.n', BT + '.timeMc.minMC', ['no1', 'no2']); B.digits('dmBS.n', BT + '.timeMc.secMC', ['no1', 'no2'])
    # player slots: 6 ally (left of the timer) + 6 enemy (right); per slot nodes .ring/.back/.icon, shared images
    for side, nm in (('A', 'ally'), ('E', 'enemy')):
        for k in range(1, 7):
            base = '%s.user.%s%d' % (SC, nm, k)
            B.node('dm.u%s%d.ring' % (side, k), base + '.scbUserIcon', mv)
            B.node('dm.u%s%d.back' % (side, k), base + '.iconback', mv)
            B.node('dm.u%s%d.icon' % (side, k), base + '.skillIcon', mv)
    U = SC + '.user.ally1'
    ring_scale = B.density(B.nodes['dm.uA1.ring'][0], U + '.scbUserIcon', mv, None)[0]
    B.image('dm.u.ring', U + '.scbUserIcon', ring_scale, mv=mv)
    back_scale = B.density(B.nodes['dm.uA1.back'][0], U + '.iconback', mv, only_icon(U + '.iconback', 1))[0]
    for k in (1, 2, 3): B.image('dm.u.back%d' % k, U + '.iconback', back_scale, mv=mv, hide=only_icon(U + '.iconback', k))
    # alive / dead glyphs as the footage shows them (triangle, X), white: tint cyan (ally) / red (enemy) / grey (dead)
    sk_img, sk_loc = mv.render_flat(U + '.skillIcon', 1.0, hide=only_icon(U + '.skillIcon', 8))
    cx, cy = (sk_loc[0] + sk_loc[2]) / 2, (sk_loc[1] + sk_loc[3]) / 2; r = 0.42 * max(sk_loc[2] - sk_loc[0], sk_loc[3] - sk_loc[1])
    def glyph(draw_fn):
        t = Image.new('L', (128, 128), 0); d = ImageDraw.Draw(t); draw_fn(d)
        a = np.asarray(t.resize((40, 40), Image.LANCZOS), np.float32) / 255
        return np.repeat(a[..., None], 4, 2)
    B.add_image('dm.u.alive', glyph(lambda d: d.polygon([(64, 22), (106, 98), (22, 98)], fill=255)), (cx - r, cy - r, cx + r, cy + r))
    B.add_image('dm.u.dead', glyph(lambda d: (d.line([(30, 30), (98, 98)], fill=255, width=14), d.line([(98, 30), (30, 98)], fill=255, width=14))),
                (cx - r, cy - r, cx + r, cy + r))
    # ------------------------------------------------------------ plant / defuse bar (bombset): title, gauge, HOLD + key
    BS = 'bombset'
    part('dm.set.bg', BS + '.d1'); B.part('dm.set.gauge', BS + '.bombgauge.d1', mv=mv, scale=2.0); part('dm.set.bar', BS + '.bombgauge.dmgauge')
    field('dm.set.txt', BS + '.bombgauge.txt')                         # "Plant Bomb" / "Planting" / "Defuse Bomb" / "Defusing"
    field('dm.set.al', BS + '.bombAL.txt')                             # alert line above the bar
    NS = BS + '.notiset'
    field('dm.set.hold', NS + '.downKey.txt')                          # "HOLD"
    part('dm.set.d3', NS + '.d3'); part('dm.set.key', NS + '.keybtn')
    field('dm.set.keytxt', NS + '.inputTXT.txt')                       # key label (PC: "E"; Vita: button name)
    # ------------------------------------------------------------ can / can't plant here prompt (installMSG)
    IM = 'installMSG'
    B.part('dm.im.bg', IM + '.d1', mv=mv, scale=2.0)              # plain 4x4-unit box (solid fill, colour from the node)
    B.node('dm.im.icon', IM + '.iconMc', mv)
    sc_ = B.density(B.nodes['dm.im.icon'][0], IM + '.iconMc', mv, only_icon(IM + '.iconMc', 1))[0]
    B.image('dm.im.no', IM + '.iconMc', sc_, mv=mv, hide=only_icon(IM + '.iconMc', 1))
    # (iconMc frame 2 is a PC mouse: left out - Vita prompts rule)
    field('dm.im.t1', IM + '.messageMc.icon1.txt'); field('dm.im.t2', IM + '.messageMc.icon2.txt')
    # ------------------------------------------------------------ round result band (roundRe.alMc.resultMC)
    RR = 'roundRe.alMc.resultMC'
    B.node('rr.bg', RR + '.bgColor', mv)                               # band (soft gradient: stored at 0.3 scale, shared with ch.bg)
    B.image('rr.band', RR + '.bgColor', 0.3 * M.K, mv=mv, hide=only_icon(RR + '.bgColor', 1))
    for p in ('d7', 'd11', 'd53'): part('rr.' + p, RR + '.' + p)      # d7/d11 score plates, d53 "VS"
    B.node('rr.campa', RR + '.campa', mv); B.node('rr.campb', RR + '.campb', mv)
    esc = max(B.density(B.nodes['rr.campa'][0], RR + '.campa', mv, only_icon(RR + '.campa', 1))[0],
              B.density(B.nodes['rr.campb'][0], RR + '.campb', mv, only_icon(RR + '.campb', 1))[0])
    # campa / campb frames: 5 attack, 6 defense in the ally (blue) / enemy (red) colours (footage HQ: DEFENSE blue hexagon vs ATTACK
    # red tower); 8 / 7 the team emblems Elimination shows (footage Urban: the player's GHOST B shield vs GHOST A triangle)
    for nm, side, k in (('attA', 'campa', 5), ('defA', 'campa', 6), ('attE', 'campb', 5), ('defE', 'campb', 6), ('emA', 'campa', 8), ('emE', 'campb', 7)):
        B.image('rr.emb.' + nm, RR + '.' + side, esc, mv=mv, hide=only_icon(RR + '.' + side, k))
    B.digits('rrA.n', RR + '.allyPoint', ['no1']); B.digits('rrAB.n', RR + '.allyPointB', ['no1']); B.digits('rrE.n', RR + '.enemyPoint', ['no1'])
    field('rr.round', RR + '.roundMC.txt'); field('rr.title', RR + '.resultMC.icon1.txt'); field('rr.sub', RR + '.miTXT.txt')
    field('rr.typeA', RR + '.btType.icon1.txt'); field('rr.typeE', RR + '.btTypeenemy.icon1.txt')
    # switching sides band (roundRe.alMc.changeMC): band, title, subtitle, round; emblem + side name below (footage layout)
    CH = 'roundRe.alMc.changeMC'
    B.node('ch.bg', CH + '.bgColor', mv)
    field('ch.round', CH + '.roundMC.txt'); field('ch.title', CH + '.chcamp.txt'); field('ch.sub', CH + '.miContent.txt'); field('ch.type', CH + '.btType.icon1.txt')
    B.nodes['screen'] = (np.eye(3), np.ones(4, np.float32), np.zeros(4, np.float32))
    eh = B.images['rr.emb.attA']['loc']; s = 98.0 / (eh[3] - eh[1])     # emblem ~98 px tall at Vita (480, 264), as in the footage
    for nm in ('ch.emb', 'sp.emb'):
        y = 264 if nm == 'ch.emb' else 130                               # sp.emb: round-start banner emblem (footage: above "ROUND n")
        k = 1.0 if nm == 'ch.emb' else 76.0 / 98                          # (clear of the bigger timer plate above and ROUND n below)
        B.vnode(nm, 'screen', [[s * k, 0, 480 - (eh[0] + eh[2]) / 2 * s * k], [0, s * k, y - (eh[1] + eh[3]) / 2 * s * k], [0, 0, 1]])
    # ------------------------------------------------------------ in-world site markers (InGameUI_D 'bomb'): anchor = node wm.anchor
    WB = 'bomb.bombMC.boomS'
    B.node('wm.anchor', 'bomb', dm); B.node('wm.icon', WB + '.iconMc', dm); B.node('wm.arrow', WB + '.arrowMc', dm)
    isc = None
    for nm, p in (('wm.atkA', 'attackMc.dmsetAreaA'), ('wm.atkB', 'attackMc.dmsetAreaB'), ('wm.atkBombA', 'attackMc.dmsetBombA'), ('wm.atkBombB', 'attackMc.dmsetBombB'),
                  ('wm.defA', 'defencdMc.dmdefAreaA'), ('wm.defB', 'defencdMc.dmdefAreaB'), ('wm.defBombA', 'defencdMc.dminstallA'), ('wm.defBombB', 'defencdMc.dminstallB')):
        full = WB + '.iconMc.' + p; leaf = p.split('.')[1]
        side = p.split('.')[0]
        keep = lambda q, full=full, side=side: q.startswith(WB + '.iconMc.') and not (q == full or q.startswith(full + '.') or q == WB + '.iconMc.' + side)
        if isc is None: isc = B.density(B.nodes['wm.icon'][0], WB + '.iconMc', dm, keep)[0] * 1.5
        B.image(nm, WB + '.iconMc', isc, mv=dm, hide=keep)
    for nm, p in (('wm.arrowW', 'arrowReady'), ('wm.arrowR', 'arrowInstall')):
        full = WB + '.arrowMc.' + p
        B.image(nm, WB + '.arrowMc', isc, mv=dm, hide=lambda q, full=full: q.startswith(WB + '.arrowMc.') and not (q == full or q.startswith(full + '.')))
    field('wm.dist', WB + '.iconMc.attackMc.distanceMC.distance.txt', m=dm)
    # ------------------------------------------------------------ digits used by this pack (Font_Number_30)
    if with_glyphs:
        p30 = 'hpapMC.hpapMC.hpMC.hpde.hpMC.no1'
        loc = mv.render_flat(p30 + '.no8', 1.0, cx=CX0)[1]
        dn = [n for n in B.nodes if re.match(r'(dm[LRGB]|dmLB|dmRB|dmT|dmTW|dmB[MS]|rr[AE]|rrAB)', n) and re.search(r'\.n\d$', n)]
        sc = max(M.jac_scale(B.nodes[n][0], loc) for n in dn) * 1.15
        B.glyphset('n30', p30, sc)
        B.add_image('white', np.ones((4, 4, 4), np.float32), (0, 0, 1, 1))
    return B

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    OUT = args[0] if args else gitspaths.path('DATA', 'dem_hud.ghud')
    B = add_dem(Builder())
    B.font(44)
    a0, a1 = B.write(OUT)
    print('wrote', OUT, 'atlas', a0.shape, 'font', a1.shape, 'images', len(B.images), 'nodes', len(B.nodes), 'fields', len(B.fields))
    if '--sheet' in sys.argv:
        Image.fromarray((np.clip(a0, 0, 1) * 255).astype(np.uint8)).save(sys.argv[sys.argv.index('--sheet') + 1])
