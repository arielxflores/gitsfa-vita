#!/usr/bin/env python3
# b89 (user: "make sure nothing is mismatched ... the operatives ... anywhere during gameplay or in the menus"): cross-checks
# every table that names, pictures, classes or tunes a weapon or an operative, in the engine source and in the data folder,
# and fails on the first disagreement of each kind. No game data is read beyond names inside the packs.
#   python3 tools/audit/consistency.py [DATA_DIR]      (default build/data)
import os, re, sys
H = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.normpath(os.path.join(H, '..', '..')); SRC = os.path.join(ROOT, 'engine', 'src')
DATA = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'data')
src = lambda f: open(os.path.join(SRC, f)).read()
bad = []; notes = []
def check(ok, msg):
    if not ok: bad.append(msg)
def cstr(s): return None if s.strip() == 'NULL' else s.strip().strip('"')
def split_args(body):   # top-level commas of a C initializer body
    out, d, q, cur = [], 0, False, ''
    for ch in body:
        if ch == '"': q = not q
        if not q and ch in '{(': d += 1
        if not q and ch in '})': d -= 1
        if ch == ',' and d == 0 and not q: out.append(cur); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur)
    return [x.strip() for x in out]
def table(text, start):
    i = text.index(start); i = text.index('{', i) + 1; d = 1; j = i
    while d: d += {'{': 1, '}': -1}.get(text[j], 0); j += 1
    body = re.sub(r'//[^\n]*', '', text[i:j - 1]); body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    out, d, q, st = [], 0, False, 0   # the top-level { ... } entries (nested braces stay inside their entry)
    for k, ch in enumerate(body):
        if ch == '"': q = not q
        if q: continue
        if ch == '{':
            if d == 0: st = k + 1
            d += 1
        elif ch == '}':
            d -= 1
            if d == 0: out.append(split_args(body[st:k]))
    return out
menu_h, menu_fe, bots_h, audio_h, hud_m, skills_h, main_c = (src(f) for f in ('menu.h', 'menu_fe.h', 'bots.h', 'audio.h', 'hud_match.h', 'skills.h', 'main.c'))
# ------------------------------------------------------------------ weapons
W = {}   # weapons.txt rows
for l in open(os.path.join(DATA, 'weapons.txt')):
    if l.startswith('#') or not l.strip(): continue
    f = l.rstrip('\n').split('|'); W[f[0]] = f
MW = [dict(name=cstr(a[0]), cls=cstr(a[1]), icon=int(a[2]), slot=int(a[3]), dmg=float(a[4].rstrip('f')), acc=float(a[5].rstrip('f')), stab=float(a[6].rstrip('f')),
           rate=float(a[7].rstrip('f')), rng=float(a[8].rstrip('f')), mag=int(a[9]), res=int(a[10]), sh=cstr(a[11]), vm=cstr(a[12]), desc=cstr(a[13]))
      for a in table(menu_h, 'static const MenuWpn MENU_WPN[]')]
order = [int(x) for x in re.search(r'MENU_WPN_ORDER\[\] = \{([^}]*)\}', menu_h).group(1).split(',')]
check(sorted(order) == list(range(len(MW))), 'MENU_WPN_ORDER is not a permutation of MENU_WPN')
CLS = {'ar': 'ASSAULT RIFLE', 'pdw': 'PERSONAL DEFENSE WEAPON', 'smg': 'SUBMACHINE GUN', 'sr': 'SNIPER RIFLE', 'sg': 'SHOTGUN', 'lmg': 'LIGHT MACHINE GUN', 'hg': 'PISTOL'}
gvm_has = lambda n: os.path.exists(os.path.join(DATA, n))
pack = lambda n: set(x.decode() for x in re.findall(rb'([a-z][a-z.]*\.\d+)\x00', open(os.path.join(DATA, n), 'rb').read()))
HUDI, MENUI = pack('hud.ghud'), pack('menu.ghud')
insp = {l.split()[0] for l in open(os.path.join(DATA, 'wpn_inspect.txt')) if l.strip() and not l.startswith('#')} if gvm_has('wpn_inspect.txt') else set()
for m in MW:
    v = m['vm'] or m['sh']; r = W.get(v); tag = 'MENU_WPN %s' % m['name']
    check(r is not None, '%s: no weapons.txt row %s' % (tag, v))
    if not r: continue
    check(m['name'] == r[1] or (m['vm'] and m['name'].startswith(r[1] + ' ')), '%s: weapons.txt calls it %s' % (tag, r[1]))
    check(int(r[3]) == m['icon'], '%s: icon %d, weapons.txt %s' % (tag, m['icon'], r[3]))
    mel = m['slot'] == 2   # b98: a melee weapon (the panel's DAMAGE / FIRE RATE are shown, not played: a melee hit kills, the rate is its clips')
    check((CLS.get(r[2]) == m['cls']) if not mel else (r[2] == 'knife' and m['cls'] == 'MELEE'), '%s: class %s, weapons.txt %s' % (tag, m['cls'], r[2]))
    check((m['slot'] == 1) == (r[2] == 'hg') and (m['slot'] == 2) == (r[2] == 'knife'), '%s: slot %d for a %s' % (tag, m['slot'], r[2]))
    check(int(r[11]) == m['mag'] and int(r[12]) == m['res'], '%s: ammo %d / %d, weapons.txt %s / %s' % (tag, m['mag'], m['res'], r[11], r[12]))
    if len(r) > 30:   # the v2.0 panel columns
        v2 = [float(x) for x in r[26:31]]
        check(v2 == [m['dmg'], m['acc'], m['stab'], m['rate'], m['rng']], '%s: panel %s, weapons.txt %s' % (tag, [m['dmg'], m['acc'], m['stab'], m['rate'], m['rng']], v2))
        pel = 8 if r[2] == 'sg' else 1
        if not mel: check(abs(float(r[9]) * pel - m['dmg']) < 0.6, '%s: DAMAGE %g, plays %s x %d' % (tag, m['dmg'], r[9], pel))
        if r[2] not in ('sr', 'sg', 'knife'): check(abs(float(r[8]) - m['rate'] * 10) < 1, '%s: FIRE RATE %g, plays %s rpm' % (tag, m['rate'], r[8]))
    else: bad.append('%s: weapons.txt has no v2.0 panel columns' % tag)
    for f in ('vm_%s.gvm' % v, 'w3_%s.gvm' % m['sh']): check(gvm_has(f), '%s: %s missing' % (tag, f))
    check('wp.%d' % m['icon'] in HUDI, '%s: hud.ghud has no wp.%d (kill feed, pick-up)' % (tag, m['icon']))
    check('wpi.%d' % m['icon'] in MENUI, '%s: menu.ghud has no wpi.%d (armory)' % (tag, m['icon']))
    check(v in insp, '%s: no wpn_inspect.txt row (INSPECT)' % tag)
    check(m['desc'] and len(m['desc']) <= 56, '%s: description missing or too long for the inspect panel' % tag)
wmax = int(re.search(r'#define WDEF_MAX (\d+)', src('weapon.h')).group(1))
check(len(W) <= wmax, 'weapon.h WDEF_MAX %d: weapons.txt has %d rows (the last ones would be dropped)' % (wmax, len(W)))
for k, r in W.items():   # every first-person gun: its sound group, its light mesh, its kill-feed icon
    check(gvm_has('vm_%s.gvm' % k), 'weapons.txt %s: vm_%s.gvm missing' % (k, k))
    check('wp.%s' % r[3] in HUDI, 'weapons.txt %s: hud.ghud has no wp.%s' % (k, r[3]))
SW = {a[0].strip('"'): a[1].strip('"') for a in table(audio_h, 'static const SndWeapon SND_WEAPON[]')}
gev = open(os.path.join(DATA, 'events.gev'), 'rb').read()
for k in W:
    base = SW.get(k)
    check(base is not None, 'audio.h SND_WEAPON: no entry for %s (it would sound like the M4A1)' % k)
    if base and W[k][2] == 'knife':   # b98: the blades - swing and equip
        for e in ('swing', 'swing_tp', 'equip_on'):
            check((base + '/' + e).encode() in gev, 'events.gev: no %s/%s (%s)' % (base, e, k))
    if base and k not in ('knife', 'axe', 'm67'):
        for e in ('fire_fp', 'fire_tp', 'equip_on'):
            check((base + '/' + e).encode() in gev, 'events.gev: no %s/%s (%s)' % (base, e, k))
BW = table(bots_h, 'static const BotWpn BOT_WPN[]')
BWD = {a[0].strip('"'): (float(a[1].rstrip('f')), int(a[2])) for a in BW}   # (b91: float literals like 22.5f)
for m in MW:
    if m['vm'] or m['slot'] == 2: continue   # (b98: the melee weapons are not bot guns: bots.h bot_w3_knife / bot_w3_axe)
    check(m['sh'] in BWD, 'bots.h BOT_WPN: no %s (bots / remote players with it)' % m['sh'])
    r = W.get(m['sh'])
    if m['sh'] in BWD and r:
        rpm, mag = BWD[m['sh']]
        if r[2] not in ('sr', 'sg'): check(abs(rpm - float(r[8])) < 1, 'BOT_WPN %s: %g rpm, the player\'s %s' % (m['sh'], rpm, r[8]))
        check(mag == int(r[11]), 'BOT_WPN %s: magazine %d, the player\'s %s' % (m['sh'], mag, r[11]))
for k in BWD: check(gvm_has('w3_%s.gvm' % k), 'BOT_WPN %s: w3_%s.gvm missing' % (k, k))
check('R.weapon_icon = 2;' not in main_c, 'main.c: KILLED BY shows the M4A1 icon for every killer')
# shipped files: everything the engine loads is in the ship list
ship = {l.strip() for l in open(os.path.join(ROOT, 'tools', 'databuilder', 'shiplist.txt')) if l.strip() and not l.startswith('#')}
for k in W:
    for f in ('vm_%s.gvm' % k,): check(f in ship, 'shiplist.txt: %s not listed' % f)
for k in BWD: check('w3_%s.gvm' % k in ship, 'shiplist.txt: w3_%s.gvm not listed' % k)
for f in ('wpn_inspect.txt',): check(f in ship, 'shiplist.txt: %s not listed' % f)
for f in sorted(os.listdir(DATA)):
    if f.endswith('.gfp'): check(f in ship, 'shiplist.txt: %s not listed' % f)
# ------------------------------------------------------------------ operatives
MC = table(menu_h, 'static const MenuChr MENU_CHR[12]'); stems = [cstr(a[0]) for a in MC]; names = [cstr(a[1]) for a in MC]
check(len(stems) == 12, 'MENU_CHR: %d operators' % len(stems))
HP_ORDER = ['motoko', 'bato', 'saito', 'paz', 'ishikawa', 'togusa', 'borma', 'maven', 'kuro', 'azuma', 'reiko', 'sitara']   # hp.chr frames 1..12 (in-game HUD portraits; b87)
hm = [int(x) for x in re.search(r'HM_PORTRAIT\[12\] = \{([^}]*)\}', hud_m).group(1).split(',')]
for i, s in enumerate(stems): check(HP_ORDER[hm[i] - 1] == s, 'hud_match.h HM_PORTRAIT: %s shows %s\'s portrait' % (s, HP_ORDER[hm[i] - 1]))
face = dict((k, int(v)) for k, v in re.findall(r'FACE_(\w+) = (\d+)', menu_h))
FACE_OF = {'bato': 'BATOU', 'motoko': 'MOTOKO', 'saito': 'SAITO', 'togusa': 'TOGUSA', 'ishikawa': 'ISHIKAWA', 'borma': 'BORMA', 'paz': 'PAZ', 'azuma': 'AZUMA', 'kuro': 'KURUTSU', 'maven': 'MAVEN', 'reiko': 'REIKO', 'sitara': 'SITARA'}
mf = re.search(r'MENU_CHR_FACE\[12\] = \{([^}]*)\}', menu_h).group(1).split(',')
for i, s in enumerate(stems): check(mf[i].strip() == 'FACE_' + FACE_OF[s], 'menu.h MENU_CHR_FACE: %s has %s' % (s, mf[i].strip()))
for s in stems: check('op.face.%d' % face[FACE_OF[s]] in MENUI, 'menu.ghud has no op.face.%d (%s)' % (face[FACE_OF[s]], s))
sk_stem = re.search(r'SK_STEM\[SK_COUNT\] = \{([^}]*)\}', skills_h).group(1).replace('"', '').replace(' ', '').split(',')
skd = table(skills_h, 'static const SkillDef SK_DEF[SK_COUNT]')
skico = [int(x) for x in re.search(r'MENU_CHR_SKICO\[12\] = \{([^}]*)\}', menu_h).group(1).split(',')]
for i, s in enumerate(stems):
    k = sk_stem.index(s); d = skd[k]
    check(cstr(MC[i][2]) == cstr(d[1]), 'operator %s: menu skill %s, game skill %s' % (s, cstr(MC[i][2]), cstr(d[1])))
    check(cstr(MC[i][3]) == cstr(d[2]), 'operator %s: menu skill text differs from the game\'s' % s)
    check(skico[i] == int(d[3]), 'operator %s: menu skill icon sk.%d, HUD icon %s' % (s, skico[i], d[3]))
    check('sk.%d' % skico[i] in MENUI, 'menu.ghud has no sk.%d (%s)' % (skico[i], s))
cls = [int(x) for x in re.search(r'MENU_CHR_CLASS\[12\] = \{([^}]*)\}', menu_h).group(1).split(',')]
bc = [x.split() for x in re.search(r'C\[3\] = \{ "([^"]*)", "([^"]*)", "([^"]*)" \}', bots_h).groups()]
slots = [int(x) for x in re.search(r'FE_OP_SLOT\[14\] = \{([^}]*)\}', menu_fe).group(1).split(',')]
grp = [[x for i, x in enumerate(slots) if x >= 0 and (0 if i < 5 else 1 if i < 10 else 2) == k] for k in range(3)]   # (menu_fe.h fe_op_class: slots 0-4, 5-9, 10-13)
for i, s in enumerate(stems):
    check(s in bc[cls[i]], 'operator %s: class %d in the menus, the bots put it elsewhere' % (s, cls[i]))
    check(i in grp[cls[i]], 'operator %s: listed under another class on the OPERATIVES screen' % s)
disp = dict(re.findall(r'\{ "(\w+)", "([A-Z ]+)" \}', main_c[main_c.index('static const char *chr_display(const char *n) {'):][:900]))
for i, s in enumerate(stems): check(disp.get(s) == names[i].split()[0], 'operator %s: called %s in game, %s in the menus' % (s, disp.get(s), names[i]))
for s in stems: check(gvm_has('chr_%s.gvm' % s), 'chr_%s.gvm missing' % s)
# ------------------------------------------------------------------
print('consistency: %d weapons in the armory, %d in weapons.txt, %d bot guns, %d operators' % (len(MW), len(W), len(BWD), len(stems)))
for b in bad: print('  MISMATCH', b)
print('OK' if not bad else '%d mismatches' % len(bad)); sys.exit(1 if bad else 0)
