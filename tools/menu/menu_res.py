# exec'd by make_menu.py: post-match screens. TOP OPERATIVE (InGameUI mvpTitleMC / mvpuserInfo) and the result screen
# (GameResult.swf resultMainMC: RESULTS = rewardMc, PROGRESS = hackingMc, STATISTICS = statsMc), laid out as in the footage.
print('results')
gr = movie('GameResult'); ig = movie('InGameUI')
R = 'resultMainMC'; RW = R + '.rewardMc'; FR = {RW: 178}
def no_text(mv, path, frames=None):
    """hide every text field under path (they are drawn as fields)"""
    ts = set(text_fields(mv, path, frames)); return lambda p: p in ts
# ---- TOP OPERATIVE
# (b99: the title's settled frame - the last of its 65: the line art left of the title faded to 30%, as the user's PC screenshots;
#  frame 0, drawn until b98, is the first of its sweep-in with those lines at full strength across the operative's face)
B.auto('mvp.title', ig, 'mvpTitleMC', scale_mul=0.5, frames={'mvpTitleMC': 64}, hide=hide_prefix('mvpTitleMC.d1', 'mvpTitleMC.d3', 'mvpTitleMC.d13', 'mvpTitleMC.title1r', 'mvpTitleMC.title1g', 'mvpTitleMC.title1b', 'mvpTitleMC.titleback1', 'mvpTitleMC.titleback2', 'mvpTitleMC.title1.txt', 'mvpTitleMC.title2.txt'))
B.mfield('mvp.t1', ig, 'mvpTitleMC.title1.txt'); B.mfield('mvp.t2', ig, 'mvpTitleMC.title2.txt')
B.auto('mvp.bar', ig, 'mvpuserInfo', scale_mul=0.6, hide=hide_prefix('mvpuserInfo.username', 'mvpuserInfo.userScore', 'mvpuserInfo.namecardicon', 'mvpuserInfo.emblemicon', 'mvpuserInfo.scoretitletxt.txt'))
B.mfield('mvp.name', ig, 'mvpuserInfo.username.txt'); B.mfield('mvp.score', ig, 'mvpuserInfo.userScore.txt'); B.mfield('mvp.stitle', ig, 'mvpuserInfo.scoretitletxt.txt')
# ---- result screen frame: hex backdrop, ring, top tabs, bottom bar
B.auto('res.hex', gr, R + '.baseBack', hide=ST(R + '.baseBack', 'd1', 'd3', 'hex2'), scale_mul=0.4)
B.auto('res.ring', gr, R + '.mainDonut', frames={R + '.mainDonut': 18})
TM = R + '.reusltTopMenuMc'
B.auto('res.top', gr, TM, hide=ST(TM, 'rewardBtn', 'progressBtn', 'statsBtn', 'backBtn.txt'))
B.mfield('res.back', gr, TM + '.backBtn.txt')
for f in (0, 1, 4):
    B.mpart('res.tab.%d' % f, gr, TM + '.rewardBtn', frames={TM + '.rewardBtn': f}, hide=ST(TM + '.rewardBtn', 'txt', 'd6'), node='res.tab0')
B.mpart('res.tabico', gr, TM + '.rewardBtn.d6', node='res.ti0')
for i, b in enumerate(('rewardBtn', 'progressBtn', 'statsBtn')): B.mnode('res.tab%d' % i, gr, TM + '.' + b); B.mnode('res.ti%d' % i, gr, TM + '.' + b + '.d6')
B.mfield('res.ttxt', gr, TM + '.rewardBtn.txt', align=2)
UB = R + '.resultUnderbarMc'
B.auto('res.under', gr, UB, hide=ST(UB, 'nextGameTime', 'autoNotice', 'skipTMC'))
B.mfield('res.utxt', gr, UB + '.autoNotice.txt')
# player name + level badge, bottom right as in the footage (GameResultUserInfoMC moved right)
UI = R + '.userInfoMc'
B.mnode('res.uname', gr, UI + '.unserName.txt'); B.mfield('res.uname', gr, UI + '.unserName.txt')
B.mnode('res.urank', gr, UI + '.rankMC.rankIcon'); B.mfield('res.ulv', gr, UI + '.rankMC.userLevel.txt', align=2)
# rank badges (RankBigMc frames = level - 1), drawn into any node
RK = R + '.userInfoMc.rankMC.rankIcon'
for f in range(20): B.mpart('rk.%d' % f, gr, RK, frames={RK: f}, node='rk.tmp', scale=0.6 * 0.75)
del B.nodes['rk.tmp']
# ---- RESULTS tab (rewardMc at its last frame)
RHIDE = both(no_text(gr, RW, FR), hide_prefix(*[RW + '.' + k for k in ('rewards', 'd195', 'titleEvent', 'nextUpIems1', 'nextUpIems2', 'titleFX2', 'titleFX1',
             'expMcFX1', 'expMcFX2', 'addExpMcFX1', 'addExpMcFX2', 'addGpMcFX1', 'addGpMcFX2', 'gpMcFX1', 'gpMcFX2', 'addExpMc', 'addGpMc', 'emblemMC',
             'expGMC.expAddFX', 'expGMC.expAdd', 'expGMC.expAdd2', 'expGMC.expBasic', 'expGMC.rankMC', 'expGMC.nextrankMC')]))
B.auto('rw', gr, RW + '.d1', frames=FR, scale_mul=0.3); B.auto('rw', gr, RW + '.d199', frames=FR, scale_mul=0.3)
B.auto('rw', gr, RW, frames=FR, hide=both(RHIDE, ST(RW, 'd1', 'd199')))
emb = '#%d' % sym_id(gr, 'GameResult_fla.embelm_62')
for nm, f in (('victory', 0), ('defeat', 9), ('draw', 19)): B.mimage('rw.emb.' + nm, gr, emb, 0.6, frames={emb: f})
B.mnode('rw.emb', gr, RW + '.emblemMC', FR)
for nm, p in (('rw.title', 'titleMC.d1'), ('rw.ghost', 'titleFX1.d1'), ('rw.team', 'teamtxt.txt'), ('rw.head', 'd301.__id10_.txt'), ('rw.gph', '__id7_.txt'),
              ('rw.exph', '__id6_.txt'), ('rw.total', '__id5_.txt'), ('rw.gpt', 'gpMc.txt'), ('rw.ext', 'expMc.txt')):
    B.mfield(nm, gr, RW + '.' + p, FR)
for i in (1, 2, 3):
    for nm, p in (('rw.p%dt', 'pointTitle.__id9_.txt'), ('rw.p%dg', 'gpPoint.txt'), ('rw.p%de', 'expPoint.txt')): B.mfield(nm % i, gr, RW + '.point%d.' % i + p, FR)
EG = RW + '.expGMC'
B.mpart('rw.bar', gr, EG + '.expBasic', frames=FR)
for nm, p in (('rw.rank', 'rankMC'), ('rw.nrank', 'nextrankMC')): B.mnode(nm, gr, EG + '.' + p, FR)
for nm, p in (('rw.lv', 'levelTXT.txt'), ('rw.nlv', 'nextlevelTXT.txt'), ('rw.next', '__id11_.txt'), ('rw.exp', 'expPercentMc.txt')): B.mfield(nm, gr, EG + '.' + p, FR)
# ---- PROGRESS tab (hackingMc)
HK = R + '.hackingMc'; FH = {HK: 50}
B.auto('pg', gr, HK + '.d1', frames=FH, scale_mul=0.3)
B.auto('pg', gr, HK, frames=FH, hide=both(no_text(gr, HK, FH), hide_prefix(HK + '.d1', HK + '.hackingListMC', HK + '.titleFX2', HK + '.titleFX1', HK + '.titleEvent')))
for nm, p in (('pg.title', 'titleMC.txt'), ('pg.ghost', 'titleFX1.txt'), ('pg.fin', 'fintxt.txt'), ('pg.tip', 'titleTxtMc.txtMc.txt')): B.mfield(nm, gr, HK + '.' + p, FH)
B.mimage('pg.emb', gr, HK + '.d711', 0.5, frames=FH)   # b77: the PROGRESS tab's emblem (the hexagon with the keyhole; footage #49 0:09)
# ---- STATISTICS tab (statsMc): mode / map / time, two team tables of 6 rows
SM = R + '.statsMc'; FS = {SM: 59}
B.auto('st', gr, SM + '.d1', frames=FS, scale_mul=0.3)
B.auto('st', gr, SM, frames=FS, hide=both(no_text(gr, SM, FS), hide_prefix(*[SM + '.' + k for k in ('d1', 'teamList1', 'teamList2', 'rightMenu', 'modTXTFx2', 'modTXTFx1', 'emblemMC', 'mvpTileList', 'mvp1', 'mvp2', 'mvp3')])))
for nm, p in (('st.mode', 'modTXT.d1'), ('st.map', 'mapTXT.txt'), ('st.time', 'playTimeTXT.txt')): B.mfield(nm, gr, SM + '.' + p, FS)
B.mnode('st.emb', gr, SM + '.emblemMC', FS)
embm = '#%d' % sym_id(gr, 'GameResult_fla.embelm_mod_152')
cid_m = gr.node_world(SM + '.emblemMC', FS)[0]
print('  mode emblem frames', len(gr.ch[cid_m]['frames']), gr.ch[cid_m].get('labels'))
for nm, f in (('tdm', 0), ('dem', 10)): B.mimage('st.emb.' + nm, gr, embm, 0.6, frames={embm: f})
for t in (1, 2):
    TL = SM + '.teamList%d' % t
    B.auto('st.team%d' % t, gr, TL, hide=both(no_text(gr, TL), hide_prefix(TL + '.d3', TL + '.taTileList', *[TL + '.list%d' % k for k in range(1, 7)])))
    for nm, p in (('st.t%dname', 'teamTXT.txt'), ('st.t%dres', 'btResult.txt'), ('st.t%dscore', 'btScroe.txt')): B.mfield(nm % t, gr, TL + '.' + p)
    for k in range(1, 7): B.mnode('st.r%d_%d' % (t, k), gr, TL + '.list%d' % k)
L1 = SM + '.teamList1.list1'
B.mpart('st.row', gr, L1, hide=hide_prefix(*[L1 + '.' + k for k in ('yourSelfBg', 'clanicon', 'clanmark', 'pointtxt', 'killtxt', 'assitxt', 'userTXT', 'partyIocn', 'kdrIconMc', 'kdrPointMc')]), node='st.r1_1')
B.mpart('st.rowme', gr, L1 + '.yourSelfBg', node='st.rme')
for nm, p in (('st.cpt', 'pointtxt.txt'), ('st.ckill', 'killtxt.txt'), ('st.cassi', 'assitxt.txt'), ('st.cname', 'userTXT.txt'), ('st.ckdr', 'kdrPointMc.kdrpoint.txt')):
    B.mfield(nm, gr, L1 + '.' + p)
# per row and column: the column field's plane moved onto that row (rows of both teams share list1's layout)
H1 = B.nodes['st.r1_1'][0]; iH1 = np.linalg.inv(H1)
for c in ('st.cpt', 'st.ckill', 'st.cassi', 'st.cname', 'st.ckdr', 'st.rme'):
    Hc, mul, add = B.nodes[c]
    for t in (1, 2):
        for k in range(1, 7): B.nodes['%s%d_%d' % (c, t, k)] = (B.nodes['st.r%d_%d' % (t, k)][0] @ iH1 @ Hc, mul.copy(), add.copy())
HD = SM + '.d333'
for i, t in enumerate(text_fields(gr, HD)): B.mfield('st.h%d' % i, gr, t)
print('  stats header fields', len(text_fields(gr, HD)))
# ---- b58 (footage: the statistics tab's CONTROLLER / DESTROYER / TECHNICIAN awards; MISSION +1 / ACHIEVEMENT +1 beside the
# player's level on every result tab). The award pictures are the reward icons the original's result screen loads at run
# time (ui.dat rsc/item/rewards: GameResult.swf's mvpIcon is only a placeholder); the medals are AchievePopup's amIcon.
RWD = gitspaths.dirp('MENURSC', 'menu', 'ui', 'GFx_DA', 'Data', 'rsc', 'item', 'rewards')
for nm, fn in (('aw.ctrl', 'Objective_Controler.png'), ('aw.dest', 'Combat_Master.png'), ('aw.tech', 'CyberBrain_Expert.png')):
    im = Image.open(RWD + fn).convert('RGBA'); a = np.asarray(im)[..., 3] > 4; ys, xs = np.nonzero(a)
    im = im.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)); im = im.resize((max(1, round(im.size[0] * 104 / im.size[1])), 104), Image.LANCZOS)
    B.pil_image(nm, np.asarray(im), (0, 0, im.size[0], im.size[1]))
# b102: TOP OPERATIVE's name card and emblem (mvpuserInfo.namecardicon: class nameCardMC at (238,-152) scaled 0.93;
# emblemicon: class embicon at (498,-155) scaled 0.75 - both loaded at run time). The six name cards the client carries are
# ui.dat rsc/item/Namecard/Achievement_BG_01..06.png (843x102). Demolition video 576 s (1 stage unit = 1 px there): the card
# is a 357x88 window from the card's left end (49% of it), its left quarter fading in; the emblem 80x80 at its right end.
# Here (0.6 px a unit): the card 214x53 px at (622.8,452.8), the emblem 48x48 at (778.8,451).
NCD = gitspaths.dirp('MENURSC', 'menu', 'ui', 'GFx_DA', 'Data', 'rsc', 'item', 'Namecard')
for k in range(6):
    im = Image.open(NCD + 'Achievement_BG_%02d.png' % (k + 1)).convert('RGBA'); im = im.crop((0, 0, round(im.size[1] * 357 / 88), im.size[1])).resize((260, 64), Image.LANCZOS)
    a = np.asarray(im).copy(); a[..., 3] = (a[..., 3] * np.clip(np.arange(260) / 65.0, 0, 1)[None, :]).astype(np.uint8)
    B.pil_image('nc.%d' % k, a, (0, 0, 260, 64))
print('  name cards 6')
ap = movie('AchievePopup'); AMI = '#%d' % sym_id(ap, 'AchievePopup_fla.amIcon_6')
for nm, f in (('md.mission', 0), ('md.achieve', 1)): B.mimage(nm, ap, AMI, 0.42, frames={AMI: f})
print('  awards + medals')
# the team line-up before TOP OPERATIVE (footage: the winning side posed, a name over each operator): MVP_NameTag's backing
NT = '#%d' % sym_id(ig, 'InGameUI_fla.MVP_NameTag_1794')
print('  name tag', B.mimage('lu.tag', ig, NT, 1.0, frames={NT: 12}, hide=hide_prefix(NT + '.nameTXT')))
