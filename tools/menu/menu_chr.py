# exec'd by make_menu.py: pictures for the operative and armory screens (layout and panels are menu_fe.h's)
print('operators / armory')
cs = movie('CharacterSelect'); rt = movie('ResourceTemplate'); ig = movie('InGameUI')
CM = 'characterMc'; AT = CM + '.armyType'
# class icons (ASSAULT / RECON / SUPPORT): CharacterSelect armyTypeMc frames
for f in range(3): B.mimage('op.cls.%d' % f, cs, AT + '.armyTypeMc', 1.2, frames={AT + '.armyTypeMc': f})
# skill icons (GameLoading tipInfoMc.skillIconMc frames, as on the loading screen)
gl = movie('GameLoading'); SK = 'loadingContainer.tipInfoMc.skillIconMc'
# (operators' skill glyphs: sk.N below)
# portraits (the commander-message faces), large enough for the operator list rows and the info panel
DN = 'donut.donutMC.donutCHA.chaMC.chaIcon'
for k in (1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17):   # (b103: 15 = Azuma; 12 - the Operator - is no operative's, engine menu.h FACE_*)
    B.mimage('op.face.%d' % k, ig, DN, 0.75, hide=only(DN, 'icon%d' % k))
# weapon icons (ResourceTemplate WpIconMC), for the armory list and the weapon info panel
WPI = '#%d' % sym_id(rt, 'WpIconMC')
# (b89: and every gun of the weapons table: its wpicon column, as make_hud.py's kill-feed icons)
WPI_USED = {13, 2, 6, 5, 21, 27, 31, 32, 18, 38, 1, 4}
WPI_SCALE = float(os.environ.get('GITSFA_WPI_SCALE', '0.7'))   # (b89: 20 guns at 1.0 overflowed the menu atlas into a second 4096 x 512 strip; the list draws them <= 270 px wide)
for ln_ in open(os.environ.get('GITSFA_WEAPONS_TXT') or os.path.join(HERE, '..', 'databuilder', 'params', 'weapons.txt')):
    f_ = ln_.split('|')
    if len(f_) > 3 and not ln_.startswith('#') and f_[3].strip().isdigit(): WPI_USED.add(int(f_[3]))
WPI0 = (13, 2, 6, 5, 21, 27, 31, 32, 18, 38, 1, 4)   # (the b88 pack's order first: its atlas layout is kept)
for k in sorted(WPI_USED, key=lambda k: (WPI0.index(k), 0) if k in WPI0 else (99, k)): B.mimage('wpi.%d' % k, rt, WPI + '.icon%d' % k, WPI_SCALE, tex=2)   # (b92: their own page)
# the HUD's skill glyphs (InGameUI intelMC skillMC.skillIcon.iconN): every operator has one; the footage shows them in a circle
# next to the operator's name (operative select) - menu_fe.h MENU_CHR_SKICO
SKI = 'intelMC.intelMC.skillMC.skillIcon'
for k in (1, 2, 3, 5, 8, 9, 11, 12, 13, 16, 17, 18, 19): B.mimage('sk.%d' % k, ig, SKI, 1.3, hide=only(SKI, 'icon%d' % k))
