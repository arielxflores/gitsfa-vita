# in-match overlays for ovl.ghud (exec'd by make_ovl.py with B, movie, ... in scope)
print('in-match overlays')
ig = movie('InGameUI'); gr = movie('GameResult'); rt = movie('ResourceTemplate')
STG = B.nodes['stage'][0]
def snode(name, x, y, s=1.0, cx=None):
    """node in stage units (1600 x 906.7): origin at (x, y), uniform scale s"""
    B.nodes[name] = (STG @ np.array([[s, 0, x], [0, s, y], [0, 0, 1.0]]), np.ones(4, np.float32), np.zeros(4, np.float32))
# ---- the HUD's skill glyphs (loading screen tip: the operator's skill in a circle, as the footage's operative select)
SKI = 'intelMC.intelMC.skillMC.skillIcon'
for k in (1, 2, 3, 5, 8, 9, 11, 12, 13, 16, 17, 18, 19): B.mimage('sk.%d' % k, ig, SKI, 1.0, hide=only(SKI, 'icon%d' % k))
# ---- digits (Font_Number_30), 33 units per digit
p30 = 'hpapMC.hpapMC.hpMC.hpde.hpMC.no1'
for k in range(10): B.mimage('n30.%d' % k, ig, p30 + '.no%d' % k, 1.6 * M.K)
# ---- match start splash: mode emblem + mode name, round / mission lines (modeAlMC)
for k, f in MODE_FR.items(): B.mpart('sp.mode.' + k, gl, LC + '.modeIconMc', frames={LC + '.modeIconMc': f}, node='sp.tmp', scale=0.33 * M.K * 1.2)
del B.nodes['sp.tmp']
snode('sp.emblem', 800, 222, 165 / 252.0)
B.mfield('sp.mode', ig, 'modeAlMC.mod.txt'); B.mfield('sp.round', ig, 'modeAlMC.roundMC.rtxt'); B.mfield('sp.rmission', ig, 'modeAlMC.roundMC.txt'); B.mfield('sp.mission', ig, 'modeAlMC.mission.txt')
# notices ("START", objectives): yellow line on a dark bar
B.mpart('nt.bar', ig, 'noti1txt.d1', scale=0.3); B.mfield('nt.txt', ig, 'noti1txt.txt')
B.mnode('ma.bar', ig, 'modAltxt.d1'); B.mfield('ma.txt', ig, 'modAltxt.roundTXT.txt')
# ---- commander message (donut: tilted portrait + name + text), 1.45x the original so its text is readable on the Vita
DN = 'donut.donutMC'
M.ovr(DN, sx=1.45)
B.auto('cm.frame', ig, DN, hide=hide_prefix(DN + '.donutCHA.chaMC.chaIcon', DN + '.donutTXT', DN + '.donutName', DN + '.donutCHA.c_out', DN + '.donutCHA.decoani'))
for k in (1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17):   # (b63: 13 / 14 for Ghost Assault's cyborg commander, which one the footage shows)
    B.mpart('cm.face.%d' % k, ig, DN + '.donutCHA.chaMC.chaIcon', hide=only(DN + '.donutCHA.chaMC.chaIcon', 'icon%d' % k), node='cm.face')
B.mfield('cm.name', ig, DN + '.donutName.txt'); B.mfield('cm.msg', ig, DN + '.donutTXT.txt')
# ---- respawn: killer bar (respawn.userInfo), tip, timer (respawnT)
UI = 'respawn.userInfo'
B.auto('rs.bar', ig, UI, scale_mul=0.6, hide=hide_prefix(UI + '.userName', UI + '.enemyName', UI + '.enemyPoint', UI + '.userPoint', UI + '.nameCardUser', UI + '.nameCardEnemy'))
B.mfield('rs.user', ig, UI + '.userName.txt'); B.mfield('rs.enemy', ig, UI + '.enemyName.txt')
for g, pth in (('rs.ep', UI + '.enemyPoint'), ('rs.up', UI + '.userPoint')):
    for i in (1, 2): B.mnode('%s%d' % (g, i), ig, pth + '.no%d' % i)
B.mnode('rs.wp', ig, UI + '.wpMC')
B.mfield('rs.tip', ig, 'respawn.resTip.txt')
RT = 'respawnT.resTime'
B.auto('rt.frame', ig, RT, hide=hide_prefix(RT + '.resTXT', RT + '.resTime.mSec', RT + '.resTime.sec'))
B.mfield('rt.txt', ig, RT + '.resTXT.txt')
for g, pth in (('rt.ms', RT + '.resTime.mSec'), ('rt.s', RT + '.resTime.sec')):
    for i in (1, 2): B.mnode('%s%d' % (g, i), ig, pth + '.no%d' % i)
WP = '#%d' % sym_id(rt, 'WpIconMC')
for k in (1, 2, 4, 5, 6, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 45, 46, 47, 48, 49, 50, 53, 56, 57, 58, 59, 69, 70, 71, 73, 74, 75, 76, 77, 78, 79, 81):
    B.mimage('wp.%d' % k, rt, WP + '.icon%d' % k, 0.83 * M.K * 0.5)
# ---- end of match splash: VICTORY / DEFEAT / DRAW band, emblem, team scores (footage layout)
B.mpart('es.band', ig, 'btResult.bgColor', scale=0.3)
emb = '#%d' % sym_id(gr, 'GameResult_fla.embelm_62')
for nm, f in (('victory', 0), ('defeat', 9), ('draw', 19)): B.mimage('es.emb.' + nm, gr, emb, 1.25 * M.K, frames={emb: f})
snode('es.emb', 800 - 100 * 1.2, 453 - 101 * 1.2, 1.2)
B.mfield('es.title', ig, 'btResult.resultMC.icon1.txt')
snode('es.left', 600, 418, 0.95); snode('es.right', 1000, 418, 0.95)
# ---- timer warning
B.auto('tw', ig, 'gmtimeAL', scale_mul=0.7, hide=hide_prefix('gmtimeAL.titleTXT', 'gmtimeAL.subTXT'))
B.mfield('tw.title', ig, 'gmtimeAL.titleTXT.txt'); B.mfield('tw.sub', ig, 'gmtimeAL.subTXT.txt')
# ---- pause menu and confirmation band: drawn procedurally by menu.h (menu_ui.h kit), no art needed

