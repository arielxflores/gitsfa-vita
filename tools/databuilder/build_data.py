#!/usr/bin/env python3
"""GITS: SAC First Assault (PS Vita port) - game data builder.

Makes the Vita port's data (the files of ux0:data/gitsfa/) on your own PC from your own copy of
Ghost in the Shell: Stand Alone Complex - First Assault Online (its "Game Files" folder).

usage: build_data.py --game "<install>/Game Files" --out <folder> [--work <folder>] [--jobs N]
                     [--intro-video <your own recording of the intro, instead of the drawn one> [--intro-start 23.7]] [--only group,...]
                     [--keep-work] [--check-only]

The result is <out>/gitsfa/ (copy that folder to ux0:data/ on the Vita) with manifest.sha256, checked
against the hashes of the reference build (known_good.sha256). Running it again resumes: finished
steps are skipped. Needs Python 3.10+ with numpy, Pillow, scipy, etcpak and opencv-python-headless,
ffmpeg with libx264 and, on Linux / macOS, a C and C++ compiler (our C tools and meshoptimizer 0.23).
The Windows package bundles all of them.

Dev only (this repository's container): --dev uses the existing intermediate extracts instead of the
install (tools/devpaths.sh), to check the converters.
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, threading, time
HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.abspath(os.path.join(HERE, '..'))
ROOT = os.path.abspath(os.path.join(TOOLS, '..'))
VM = os.path.join(ROOT, 'vm')
ENGINE = os.path.join(ROOT, 'engine', 'src')
WIN = os.name == 'nt'
BIN = os.path.join(HERE, 'bin', 'win64')        # prebuilt C tools for Windows (made by win/make_package.sh)
MAPS = [('downtown', 'Fix_Downtown'), ('berutalube', 'Fix_Berutalube'), ('ecenter', 'Fix_Ecenter'), ('ghost_storage', 'Fix_Ghost_Storage'),
        ('headquarter', 'Fix_Headquarter'), ('port', 'Fix_Port'), ('pss9', 'Fix_PSS9'), ('ruinednight', 'Fix_RuinedNight'),
        ('ruinedtokyo', 'Fix_RuinedTokyo'), ('trainstation', 'Fix_Trainstation'), ('underground', 'Fix_Underground'), ('urban', 'Fix_Urban'),
        ('zioplant', 'Fix_Zioplant'), ('shooting', 'Fix_Shooting'), ('tutorial', 'Fix_Tutorial'), ('tutorial2', 'Fix_Tutorial2'),
        ('tutorial3', 'Fix_Tutorial3')]
OPERATORS = ['Bato', 'Motoko', 'Saito', 'Togusa', 'Ishikawa', 'Borma', 'Paz', 'Azuma', 'Kuro', 'Maven', 'Reiko', 'Sitara']
# b71: the operators' other outfits (vm/make_skins.py: chr_<op>_<skin>.gvm, listed in chr_skins.txt)
SKINS = ('bato_a_fire', 'bato_goldblack', 'bato_new', 'bato_uniform', 'bato_ordinary', 'bato_ordinary02', 'motoko_a_camo', 'motoko_goldblack', 'motoko_new', 'motoko_phoenix', 'motoko_uniform', 'motoko_valentine', 'motoko_ordinary', 'motoko_ordinary02', 'motoko_ordinary03', 'motoko_sig', 'saito_a_camo', 'saito_goldblack', 'saito_phoenix', 'saito_ordinary', 'saito_ordinary02', 'togusa_a_camo', 'togusa_goldblack', 'togusa_new', 'togusa_ordinary', 'togusa_ordinary02', 'togusa_ordinary03', 'ishikawa_a_camo', 'ishikawa_goldblack', 'ishikawa_ordin', 'ishikawa_ordin02', 'borma_a_camo', 'borma_goldblack', 'borma_ordinary', 'borma_ordinary02', 'paz_a_camo', 'paz_goldblack', 'paz_ordinary', 'paz_ordinary02', 'paz_ordinary03', 'kuro_a_camo', 'kuro_goldblack', 'kuro_ordinary', 'kuro_ordinary02', 'kuro_ordinary03', 'maven_a_camo', 'maven_goldblack', 'maven_uniform', 'maven_valentine', 'maven_ordinary', 'maven_ordinary02', 'reiko_a_camo')
SKINS_HI = ('bato_goldblack', 'bato_new', 'bato_ordinary', 'bato_uniform', 'borma_goldblack', 'borma_ordinary02', 'borma_ordinary', 'ishikawa_goldblack', 'ishikawa_ordin02', 'ishikawa_ordin', 'kuro_goldblack', 'kuro_ordinary', 'maven_a_camo', 'maven_ordinary', 'maven_uniform', 'maven_valentine', 'motoko_goldblack', 'motoko_new', 'motoko_ordinary03', 'motoko_ordinary', 'motoko_sig', 'motoko_uniform', 'motoko_valentine', 'paz_goldblack', 'paz_ordinary', 'saito_a_camo', 'saito_goldblack', 'togusa_goldblack', 'togusa_new', 'togusa_ordinary03', 'togusa_ordinary')   # b76: the skins with their own meshes and a body LOD heavier than the game's (vm/make_chr.py chr_<..>_hi.gvm)
OPVID_SK = ('bato', 'motoko', 'saito', 'ishikawa', 'borma', 'paz', 'azuma', 'kuro', 'maven', 'reiko', 'sitara', 'togusa')   # tools/menu/make_skill_videos.py
OPVID_OP = ('bato', 'borma', 'ishikawa', 'kuro', 'maven', 'motoko', 'paz', 'saito', 'togusa')
SPLIT = {'ecenter.gmap': 10000000, 'ruinedtokyo.gmap': 15000000}   # the Vita copy tool's file size limit: byte pieces .000 / .001 of the (packed) file
PACK = ('.gmap', '.gcol', '.gvm', '.ghud', '.gfp')   # perf b56: shipped packed (GLZ1 containers, engine/src/glz.h): half the card reads per load

def log(msg): print('[%s] %s' % (time.strftime('%H:%M:%S'), msg), flush=True)
def sha256(path, bs=1 << 20):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(bs), b''): h.update(b)
    return h.hexdigest()

class Step:
    def __init__(s, name, group, cmds, outputs, deps=(), weight=1, cwd=None, title=None, optional=False):
        s.name, s.group, s.cmds, s.outputs, s.deps, s.weight, s.cwd = name, group, cmds, outputs, list(deps), weight, cwd
        s.title = title or name; s.optional = optional

def ship_list():
    return [l.strip() for l in open(os.path.join(HERE, 'shiplist.txt')) if l.strip() and not l.startswith('#')]

# ---------------------------------------------------------------- the work tree and the converters' environment
def environment(a):
    W = a.work; e = {}
    e['GITSFA_WORK'] = W
    e['GITSFA_TMP'] = os.path.join(W, 'tmp'); e['GITSFA_DATA'] = os.path.join(W, 'out', 'data'); e['GITSFA_BMAPS'] = os.path.join(W, 'out', 'maps')
    e['GITSFA_AUDIOOUT'] = os.path.join(W, 'out', 'audio')
    if a.dev:   # the container's intermediates (tools/devpaths.sh)
        for ln in open(os.path.join(TOOLS, 'devpaths.sh')):
            if not ln.startswith('export '): continue
            import shlex
            for kv in shlex.split(ln[7:].replace('$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)', TOOLS)):
                k, v = kv.split('=', 1)
                if k.startswith('GITSFA_') and k not in ('GITSFA_DATA', 'GITSFA_BMAPS', 'GITSFA_AUDIOOUT', 'GITSFA_TMP'): e[k] = v
    else:
        D = os.path.join(a.game, 'Data')
        e['GITSFA_FMOD'] = os.path.join(D, 'audio', 'Fmod_Build'); e['GITSFA_ANIMBIN'] = os.path.join(D, 'anim'); e['GITSFA_SKANIM'] = os.path.join(D, 'anim')
    if not a.dev: e['GITSFA_MINIMAP'] = os.path.join(W, 'x', 'minimap')
    e['GITSFA_WEAPONS_TXT'] = os.path.join(HERE, 'params', 'weapons.txt')
    e['PYTHONHASHSEED'] = '0'; e['PYTHONDONTWRITEBYTECODE'] = '1'; e['PYTHONUTF8'] = '1'
    e['GITSFA_X264_THREADS'] = '3'
    if WIN:
        e['GITSFA_EXE_NAVEDGES'] = os.path.join(BIN, 'navedges.exe'); e['GITSFA_EXE_NAVGEN'] = os.path.join(BIN, 'navgen.exe')
        e['GITSFA_ADPCM_LIB'] = os.path.join(BIN, 'adpcm.dll')
    for k in ('GITSFA_FFMPEG', 'GITSFA_FONT_BOLD'):
        if os.environ.get(k): e[k] = os.environ[k]
    return e

# ---------------------------------------------------------------- the steps (inputs: the work tree; outputs: work/out)
def steps(a):
    P = sys.executable; T = TOOLS; D = '{DATA}'; M = '{BMAPS}'; S = []
    def py(*args): return [P, '-u'] + list(args)
    if not a.dev:
        S.append(Step('extract', 'extract', [py(os.path.join(HERE, 'extract.py'), '--game', a.game, '--work', a.work)],
                      ['{WORK}/x/.complete'], weight=2, title='unpack the game archives'))
    X = [] if a.dev else ['extract']
    for stem, fix in MAPS:
        S.append(Step('map_' + stem, 'maps', [py(os.path.join(T, 'make_map.py'), '{MAPSRC}/' + fix, M, stem)],
                      [M + '/%s.gmap' % stem, M + '/%s.gcol' % stem, M + '/%s.gnav' % stem], X, title='map ' + stem))
    S.append(Step('light', 'maps', [py(os.path.join(HERE, 'light.py'), M)], [M + '/%s.glgt' % s for s, _ in MAPS], X, title='map light'))
    S.append(Step('sky', 'maps', [py(os.path.join(T, 'make_sky.py'), '{MAPVIS}/sky', '{MAPVIS}/effect/tutorial', M)],
                  [M + '/%s.gsky' % s for s in ('berutalube', 'downtown', 'ecenter', 'ghost_storage', 'port', 'pss9', 'ruinednight', 'ruinedtokyo',
                                                 'shooting', 'trainstation', 'tutorial3', 'urban', 'zioplant')], X, title='skies'))
    S.append(Step('gchk', 'maps', [py(os.path.join(HERE, 'ctools.py'), 'gchk', M, M)], [M + '/%s.gchk' % s for s, _ in MAPS],
                  ['map_' + s for s, _ in MAPS], title='map chunk split'))
    for stem, _ in MAPS:   # the grading fit draws 32 views of the map (CPU), so one step per map
        S.append(Step('grading_' + stem, 'maps', [py(os.path.join(HERE, 'grading.py'), '{CG}', M, D, stem)], [D + '/%s.glut' % stem],
                      X + ['map_' + stem, 'sky'], title='colour grading ' + stem))
    S.append(Step('radar', 'maps', [py(os.path.join(HERE, 'radar.py'), D)], [D + '/%s.gmm' % s for s, _ in MAPS[:13]], X, title='radar images'))
    # ---- interface
    S.append(Step('hud', 'ui', [py(os.path.join(T, 'ui', 'make_hud.py'), D + '/hud.ghud')], [D + '/hud.ghud'], X + ['radar'], title='HUD'))
    S.append(Step('menu', 'ui', [py(os.path.join(T, 'menu', 'make_menu.py'), D + '/menu.ghud')], [D + '/menu.ghud'], X + ['tank'], title='menus'))   # b76: after 'tank' (the TACHIKOMA tile's picture renders tk_blue.gvm)
    S.append(Step('ovl', 'ui', [py(os.path.join(T, 'menu', 'make_ovl.py'), D + '/ovl.ghud')], [D + '/ovl.ghud'], X, title='loading screen + overlays'))
    S.append(Step('bgs', 'ui', [py(os.path.join(T, 'menu', 'make_bgs.py'), D + '/')], [D + '/mload_%s.gtx' % s for s, _ in MAPS] + [D + '/mload_title.gtx'], X, title='loading pictures'))
    S.append(Step('fe_tex', 'ui', [py(os.path.join(T, 'menu', 'make_fe_tex.py'), D + '/')], [D + '/mload_lobby.gtx', D + '/mthumb.gtx'], X + ['bgs'], title='front-end pictures'))
    S.append(Step('skovl', 'ui', [py(os.path.join(T, 'skills', 'make_skill_overlays.py'), D)], [D + '/sk_ovl.gtx'], X, title='skill overlays'))
    S.append(Step('shield', 'ui', [py(os.path.join(T, 'skills', 'make_shield.py'), D)], [D + '/sk_shield.gtx', D + '/sk_shield.gsg'], X, title='plasma shield'))
    S.append(Step('tutassets', 'ui', [py(os.path.join(T, 'tutorial', 'make_tut_assets.py'), '{TUTSRC}', D, '{TMP}')], [D + '/tut_ui.gtx', D + '/tut_fx.gtx'], X, title='tutorial pictures'))
    # ---- models and animation
    S.append(Step('vm', 'models', [py(os.path.join(VM, 'make_vm_weapon.py'), 'all', D)],
                  [D + '/vm_%s.gvm' % w for w in ('hands', 'm4a1', 'm4a1rd', '870', 'acr', 'knife', 'kriss', 'l118', 'm249', 'm67', 'mateba', 'mp5', 'scar', 'seburom5', 'ak47', 'k2', 'arx160', 'hk417', 'f2000', 'famas', 'x95r', 'c30', 'c26a', 'evo', 'kacpdw', 'mp5k', 'mpx', 'p90', 'pp2000', 'ump45', 'cm2010', 'mm200', 'm99d', 'sa50', 'seburosr', 'seo90', 'srv97', 'ws2088', 'mag44', 'm2008', 'b93se', 'kcz85', 'sn57', 'p226', 'mg33', 'l86', 'mg36', 'seburomg', 'ksg', 'sta12', 'uss12', 'axe')], X, weight=3, title='first-person weapons'))   # b89: + the rifles of the user's AR video, the C-30 on its own set
    # b60: the light first-person meshes (what can be on screen while only the everyday clips play; tools/lod)
    S.append(Step('fp', 'models', [py(os.path.join(HERE, 'ctools.py'), 'vmvis', D, '{TMP}/vmvis'), py(os.path.join(T, 'lod', 'make_fp.py'), D, '{TMP}/vmvis', D)],
                  [D + '/vm_%s.gfp' % w for w in ('m4a1', 'm4a1rd', '870', 'acr', 'knife', 'kriss', 'l118', 'm249', 'm67', 'mateba', 'mp5', 'scar', 'seburom5', 'ak47', 'k2', 'arx160', 'hk417', 'f2000', 'famas', 'x95r', 'c30', 'c26a', 'evo', 'kacpdw', 'mp5k', 'mpx', 'p90', 'pp2000', 'ump45', 'cm2010', 'mm200', 'm99d', 'sa50', 'seburosr', 'seo90', 'srv97', 'ws2088', 'mag44', 'm2008', 'b93se', 'kcz85', 'sn57', 'p226', 'mg33', 'l86', 'mg36', 'seburomg', 'ksg', 'sta12', 'uss12', 'axe')], ['vm', 'weapons'], weight=3, title='light first-person meshes'))
    # (b89: vm_c30.gvm comes from make_vm_weapon.py now - its own AR_C30 set, aim, climb, slide - not make_vm.py's first model)
    S.append(Step('inspect', 'models', [py(os.path.join(T, 'menu', 'make_inspect.py'), D)], [D + '/wpn_inspect.txt'], ['vm', 'weapons'], title='armory inspect framing'))   # b89
    S.append(Step('wpnsnd', 'models', [py(os.path.join(VM, 'make_wpn_snd.py'), D)], [D + '/wpn_snd.txt'], X, title='weapon sound schedule'))   # b107: the reload / bolt / pump sounds' times (the animation network's event tracks)
    S.append(Step('w3', 'models', [py(os.path.join(VM, 'make_w3.py'), D, 'all')],
                  [D + '/w3_%s.gvm' % w for w in ('870', 'acr', 'axe', 'c30', 'knife', 'kriss', 'l118', 'm249', 'm4a1', 'm67', 'mateba', 'mp5', 'scar', 'seburom5', 'ak47', 'k2', 'arx160', 'hk417', 'f2000', 'famas', 'x95r', 'c26a', 'evo', 'kacpdw', 'mp5k', 'mpx', 'p90', 'pp2000', 'ump45', 'cm2010', 'mm200', 'm99d', 'sa50', 'seburosr', 'seo90', 'srv97', 'ws2088', 'mag44', 'm2008', 'b93se', 'kcz85', 'sn57', 'p226', 'mg33', 'l86', 'mg36', 'seburomg', 'ksg', 'sta12', 'uss12')], X, title='third-person weapons'))
    S.append(Step('chr', 'models', [py(os.path.join(VM, 'make_chr.py'), D, o) for o in OPERATORS], [D + '/chr_%s.gvm' % o.lower() for o in OPERATORS] + [D + '/chr_%s_hi.gvm' % o.lower() for o in OPERATORS], X, title='operators'))   # b76: + the menus' high-detail models
    S.append(Step('masks', 'models', [py(os.path.join(VM, 'make_masks.py'), D)], [D + '/mask_m.gvm', D + '/mask_f.gvm'], X, title='enemy masks'))   # b71
    S.append(Step('skins', 'models', [py(os.path.join(VM, 'make_skins.py'), D)], [D + '/chr_%s.gvm' % k for k in SKINS] + [D + '/chr_%s_hi.gvm' % k for k in SKINS_HI] + [D + '/chr_skins.txt', D + '/chr_hi.txt'], X + ['chr'], weight=3, title='operator skins'))   # b71; b76 the menu models of the skins with their own meshes + chr_hi.txt
    S.append(Step('gog', 'models', [py(os.path.join(T, 'chars', 'make_gog.py'), D)], [D + '/chr_gog.txt'], ['chr', 'skins'], title='operator masks'))   # b103: the mask / goggle meshes (Square on the operative screen)
    for g in 'mf': S.append(Step('gab_' + g, 'models', [py(os.path.join(T, 'anim', 'make_bank.py'), g, D)], [D + '/chr_anim_%s.gab' % g], X, title='animation bank ' + g))
    for g in 'mf': S.append(Step('og_' + g, 'models', [py(os.path.join(T, 'anim', 'make_og_bank.py'), g, D)], [D + '/chr_og_%s.gab' % g], X, title='menu poses ' + g))   # b63: the operators' select poses
    S.append(Step('skmodels', 'models', [py(os.path.join(T, 'skills', 'make_skill_models.py'), D), py(os.path.join(T, 'skills', 'make_skill_rigs.py'), D)],
                  [D + '/sk_%s.gvm' % k for k in ('dome', 'drone', 'emp', 'heatghost', 'minitachi', 'missile', 'shield', 'turret', 'wall', 'wallpod')], X, title='skill models'))
    S.append(Step('armlauncher', 'models', [py(os.path.join(T, 'skills', 'make_vm_armlauncher.py'), D)], [D + '/vm_armlauncher.gvm'], X, title='arm launcher'))
    S.append(Step('tank', 'models', [py(os.path.join(T, 'tank', 'make_tank.py'), D)], [D + '/tk_blue.gvm', D + '/tk_red.gvm'], X, title='Think Tanks'))
    S.append(Step('tankparts', 'models', [py(os.path.join(T, 'tank', 'make_tank_parts.py'), D)], [D + '/' + f for f in ['tkp_core.gvm'] + ['tkp_%s_%d.gvm' % (sl, k) for sl in ('hull', 'cabin', 'prop', 'main', 'sub') for k in (0, 1)] + ['tkp_supp_%d_c%d.gvm' % (k, c) for k in (0, 1) for c in (0, 1)] + ['tk_parts.txt']], X, weight=2, title='Think Tank parts'))   # b74: My Think Tank
    S.append(Step('tk_hud', 'models', [py(os.path.join(HERE, 'ctools.py'), 'portraits', D, '{TMP}/portraits'), py(os.path.join(T, 'tank', 'make_tk_hud.py'), '{TMP}/portraits', D)],
                  [D + '/tk_hud.gtx'], ['tank', 'audio'], title='Think Tank pictures'))
    S.append(Step('dem_models', 'models', [py(os.path.join(T, 'dem', 'make_dem_models.py'), '{DEMX}', D)], [D + '/dem_bomb.gvm'], X, title='bomb'))
    S.append(Step('dem_fx', 'ui', [py(os.path.join(T, 'dem', 'make_bomb_fx.py'), '{DEMX}', D)], [D + '/dem_fx.gtx', D + '/dem_dev.gtx'], X, title='bomb device effects'))   # b100: the device's display filling red, its keys' flashes (hands.h)
    FXD = '{SKRAW}/effect'; SC = os.path.join(T, 'fx', 'make_scene.py')   # b102: the match start's dive - the game's own scenes (fxscene.h)
    S.append(Step('dive', 'ui', [py(SC, FXD + '/cyber_world/start_screen.nif', FXD + '/texture', D + '/dive_a.gsc', '8', '30', '--fov', '73'),
                                 py(SC, FXD + '/cyber_world/start_bg_deco.nif', FXD + '/texture', D + '/dive_b.gsc', '8', '30', '--camera', FXD + '/camera/cam_cyber_world.nif', '--maxdim', '1024'),
                                 py(SC, FXD + '/cyber_world/start_screen_end.nif', FXD + '/texture', D + '/dive_c.gsc', '1', '30', '--fov', '73')],
                  [D + '/dive_a.gsc', D + '/dive_b.gsc', D + '/dive_c.gsc'], X, title='match start dive'))
    S.append(Step('netjump', 'ui', [py(SC, FXD + '/screen/screen_dive_start.nif', FXD + '/texture', D + '/sk_nj_in.gsc', '0.67', '30', '--fov', '73', '--eye', '0,-204.6,0', '--maxdim', '256'),   # b121: the Net Jump's entry / exit screen scenes (skills.h)
                                   py(SC, FXD + '/screen/screen_dive.nif', FXD + '/texture', D + '/sk_nj_out.gsc', '1.2', '30', '--fov', '73', '--eye', '0,-204.6,0', '--t0', '1.0', '--maxdim', '256')],
                  [D + '/sk_nj_in.gsc', D + '/sk_nj_out.gsc'], X, title='Net Jump screen scenes'))
    SKD = [('motoko', 'op_camouflage'), ('saito', 'heat'), ('togusa', 'm_tachikoma'), ('ishikawa', 'turret'), ('borma', 'endocoverflow'), ('paz', 'sprint')]   # b102: the skill gesture's hologram (hands.h)
    S.append(Step('skilldev', 'ui', [py(SC, FXD + '/cyberbrain/device_%s.nif' % f, FXD + '/texture', D + '/sk_dev_%s.gsc' % o, '2', '30', '--maxdim', '128') for o, f in SKD],
                  [D + '/sk_dev_%s.gsc' % o for o, f in SKD], X, title='skill gesture holograms'))
    S.append(Step('dummy', 'models', [py(os.path.join(T, 'range', 'make_dummy.py'), D)], [D + '/chr_dummy.gvm'], X, title='practice dummy'))
    S.append(Step('cyborg', 'models', [py(os.path.join(VM, 'make_cyborg.py'), D)], [D + '/chr_cyborg.gvm'], X, title='Ghost Assault cyborg'))   # b63
    S.append(Step('vm_cy', 'models', [py(os.path.join(VM, 'make_vm_weapon.py'), 'knife_cy,axe_cy', D)], [D + '/vm_knife_cy.gvm', D + '/vm_axe_cy.gvm'], X, title='Ghost Assault cyborg arms'))   # b63: the knife in the cyborg's arms; b70: the arm blade (the engine prefers it)
    S.append(Step('env', 'models', [py(os.path.join(VM, 'make_env.py'), D)], [D + '/env_ref.gtx'], X, title='model reflections'))   # b65: the game's CUBE.dds as a sphere map
    S.append(Step('c30', 'models', [py(os.path.join(T, 'c30', 'make_c30.py'), D)], [D + '/c30.gmdl'], X, title='C-30 model'))
    # ---- effects and game modes
    S.append(Step('fx', 'fx', [py(os.path.join(T, 'fx', 'build_fx.py'), D + '/fx.gfx')], [D + '/fx.gfx'], X, title='effects'))
    S.append(Step('fx_sk', 'fx', [py(os.path.join(T, 'skills', 'build_skills_fx.py'), D + '/fx_sk.gfx')], [D + '/fx_sk.gfx'], X, title='skill effects'))
    S.append(Step('dem', 'modes', [py(os.path.join(T, 'dem', 'make_gdem.py'), '{DEMX}', D)],
                  [D + '/%s.gdem' % s for s in ('downtown', 'ecenter', 'headquarter', 'pss9', 'trainstation', 'zioplant')], X + ['map_' + s for s, _ in MAPS], title='Demolition'))
    S.append(Step('elim', 'modes', [py(os.path.join(T, 'elim', 'make_gelim.py'), '{DEMX}', D)],
                  [D + '/%s.gelim' % s for s in ('urban', 'port', 'ghost_storage')] + [D + '/%s_em.gmm' % s for s in ('urban', 'port', 'ghost_storage')],
                  X + ['map_' + s for s, _ in MAPS] + ['radar'], title='Elimination'))
    S.append(Step('range', 'modes', [py(os.path.join(T, 'range', 'make_grange.py'), '{WORLD}', D)], [D + '/shooting.grange', D + '/tutorial3.grange'], X, title='shooting range'))
    S.append(Step('smf', 'maps', [py(os.path.join(T, 'smf', 'make_smf.py'), '{DEMX}/map/FixedMap', D)], [D + '/smf.txt'], X, title='special maneuvers'))   # b87: vaults, climbs, leaps (player.h)
    S.append(Step('wobj', 'models', [py(os.path.join(T, 'wobj', 'make_wobj.py'), '{WORLD}', '{MAPSRC}', D, 'Trainstation', 'Urban'), py(os.path.join(T, 'wobj', 'make_fxobj.py'), '{MAPVIS}/effect/map/urban.nif', '{MAPVIS}/effect/map', D, 'urban', 'heli')], [D + '/wobj_train_ani.gvm', D + '/wobj_trainstation.txt', D + '/wobj_hel_ani.gvm', D + '/wobj_urban.txt', D + '/wobj_urban_heli.gvm'], X, title='Train Station train, Urban helicopter'))   # b87 / b88 (wobj.h)
    S.append(Step('ttm', 'modes', [py(os.path.join(T, 'tank', 'make_ttm.py'), D)], [D + '/%s.gttm' % s for s in ('underground', 'berutalube', 'ruinedtokyo')] + [D + '/tk_term.gvm'], X, title='Terminal Conquest'))
    # ---- sound
    A = '{AUDIOOUT}'
    S.append(Step('audio', 'audio', [py(os.path.join(T, 'audio', 'build_audio.py'), A), py(os.path.join(T, 'audio', 'build_extras.py'), A)],
                  [A + '/sfx_0.gsnd', A + '/sfx_1.gsnd', A + '/sfx_x.gsnd', A + '/voice_0.gvp', A + '/voice_1.gvp', A + '/voice_2.gvp', A + '/voice_x.gvp', A + '/audio_manifest.txt'],
                  X, weight=2, title='sounds, voices, music'))
    S.append(Step('events', 'audio', [py(os.path.join(T, 'audio', 'make_events.py'), 'gev', D + '/events.gev')], [D + '/events.gev'], X + ['audio'], title='sound events'))
    # ---- movies
    S.append(Step('clips', 'movies', [py(os.path.join(T, 'tutorial', 'make_tut_clips.py'), '{USM}', D)], [D + '/tut_v%d.mp4' % i for i in range(1, 7)], X, title='tutorial clips'))
    S.append(Step('opvideos', 'movies', [py(os.path.join(T, 'menu', 'make_skill_videos.py'), '{USM}', D)],   # b63: the operative screen's DETAILS
                  [D + '/mv_sk_%s.mp4' % o for o in OPVID_SK] + [D + '/mv_op_%s.mp4' % o for o in OPVID_OP], X, weight=2, title='operator videos'))
    lj = max(1, min(2, a.jobs))   # the logo frames are drawn on lj processes
    S.append(Step('logos', 'movies', [py(os.path.join(T, 'tutorial', 'make_tut_logos.py'), '{UI2}/ui/GFx_DA/Intro.swf', '{TMP}/logos', D + '/tut_logos.mp4', str(lj))],
                  [D + '/tut_logos.mp4'], X, weight=lj, title='start-up logos'))
    if a.intro_video:
        S.append(Step('intro', 'movies', [py(os.path.join(HERE, 'intro.py'), a.intro_video, str(a.intro_start), D + '/tut_intro.mp4')],
                      [D + '/tut_intro.mp4'], title='intro movie (your recording)', optional=True))
    else:   # b119: the intro cut scene drawn from the install's own scene files (the PC plays it live; tools/intro)
        ij = max(1, min(4, a.jobs))
        S.append(Step('intro', 'movies', [py(os.path.join(HERE, 'ctools.py'), 'intro', '{WORLD}/IntroMovie', D + '/tut_intro.mp4', str(ij))],
                      [D + '/tut_intro.mp4'], X, weight=ij, title='intro movie', optional=True))
    S.append(Step('weapons', 'data', [py(os.path.join(HERE, 'copy_file.py'), os.path.join(HERE, 'params', 'weapons.txt'), D + '/weapons.txt')], [D + '/weapons.txt'], title='weapon table'))
    return S

# ---------------------------------------------------------------- running (resumable, in parallel)
# b122 (the user's first Windows run: "FAILED Think Tank parts" - the parts' textures were not in extract_list.txt): the lists
# that only the extraction reads (what to unpack, the hashes to tell entries apart) are left out of the tools' digest and go
# into the extract step's own recipe instead, so that a fixed list re-runs the extraction (resumed: only the new files) and
# the steps that failed, and keeps every other finished step. shiplist.txt is read only by the final collection.
EXTRACT_LISTS = (os.path.join(HERE, 'extract_list.txt'), os.path.join(HERE, 'check', 'extract_ref.txt'))
NOT_IN_DIGEST = ('tools/databuilder/extract_list.txt', 'tools/databuilder/shiplist.txt')
def tool_digest():
    h = hashlib.sha256()
    for top in (TOOLS, VM, ENGINE):
        for r, ds, fs in os.walk(top):
            ds[:] = sorted(d for d in ds if d not in ('__pycache__', 'host', 'out', 'bin', 'win', 'check'))
            for f in sorted(fs):
                if f.endswith(('.py', '.c', '.h', '.json', '.txt', '.sh')) and not (top == ENGINE and not f.endswith('.h')):
                    p = os.path.join(r, f); rel = os.path.relpath(p, ROOT).replace(os.sep, '/')
                    if rel in NOT_IN_DIGEST: continue
                    h.update(rel.encode()); h.update(open(p, 'rb').read())
    return h.hexdigest()[:16]

class Runner:
    def __init__(s, a, env, steps_):
        s.a, s.env, s.steps = a, env, {x.name: x for x in steps_}; s.order = [x.name for x in steps_]
        s.sub = {'WORK': a.work, 'TMP': env['GITSFA_TMP'], 'DATA': env['GITSFA_DATA'], 'BMAPS': env['GITSFA_BMAPS'], 'AUDIOOUT': env['GITSFA_AUDIOOUT']}
        # every work folder of tools/gitspaths.py (b122, the user's first Windows run: '{SKRAW}' was missing from a fixed
        # list here, and the dive / Net Jump / skill-device scenes failed with the placeholder as their path)
        sys.path.insert(0, TOOLS); import gitspaths
        for k, (place, _) in gitspaths.LAYOUT.items():
            if place and k not in s.sub: s.sub[k] = env.get('GITSFA_' + k) or os.path.join(a.work, *place.split('/'))
        s.stamps = os.path.join(a.work, 'stamps'); s.logs = os.path.join(a.work, 'logs'); os.makedirs(s.stamps, exist_ok=True); os.makedirs(s.logs, exist_ok=True)
        s.digest = tool_digest(); s.done = {}; s.failed = []; s.notrun = []; s.skipped_optional = []; s.lock = threading.Lock()
    def x(s, t):
        for k, v in s.sub.items(): t = t.replace('{%s}' % k, v)
        return t
    def recipe(s, st):
        h = hashlib.sha256(); h.update(s.digest.encode()); h.update(json.dumps([[s.x(c) for c in cmd] for cmd in st.cmds]).encode())
        if st.name == 'extract':
            for p in EXTRACT_LISTS: h.update(sha256(p).encode())
        for d in st.deps:
            if d in s.steps: h.update(json.dumps(s.done.get(d, {})).encode())
        return h.hexdigest()[:16]
    def is_done(s, st, rec):
        p = os.path.join(s.stamps, st.name + '.json')
        try: j = json.load(open(p))
        except Exception: return None
        if j.get('recipe') != rec: return None
        for o, (size, mt, h) in j['outputs'].items():
            try: stt = os.stat(o)
            except OSError: return None
            if stt.st_size != size or stt.st_mtime_ns != mt: return None
        return j['outputs']
    def run_step(s, st):
        rec = s.recipe(st); ok = s.is_done(st, rec)
        if ok is not None: return ok, 0.0, True
        for o in st.outputs: os.makedirs(os.path.dirname(s.x(o)), exist_ok=True)
        lp = os.path.join(s.logs, st.name + '.log'); t0 = time.time()
        env = dict(os.environ); env.update(s.env)
        with open(lp, 'w') as lf:
            for cmd in st.cmds:
                c = [s.x(t) for t in cmd]; lf.write('$ ' + ' '.join(c) + '\n'); lf.flush()
                kw = {'creationflags': 0x00004000} if WIN else {'preexec_fn': lambda: os.nice(int(s.a.nice))} if s.a.nice else {}   # (lower priority)
                r = subprocess.run(c, stdout=lf, stderr=subprocess.STDOUT, env=env, cwd=st.cwd or s.env['GITSFA_TMP'], **kw)
                if r.returncode: raise RuntimeError('exit %d' % r.returncode)
        outs = {}
        for o in st.outputs:
            p = s.x(o)
            if not os.path.exists(p): raise RuntimeError('did not make ' + os.path.basename(p))
            stt = os.stat(p); outs[p] = (stt.st_size, stt.st_mtime_ns, sha256(p))
        json.dump(dict(recipe=rec, outputs=outs, seconds=round(time.time() - t0, 1)), open(os.path.join(s.stamps, st.name + '.json'), 'w'), indent=0)
        return outs, time.time() - t0, False
    def run(s, jobs):
        todo = list(s.order); running = {}; n = len(todo); k = 0; cap = max(1, jobs)
        def finish(name, res):
            nonlocal k
            k += 1; st = s.steps[name]
            if isinstance(res, Exception):
                lp = os.path.join(s.logs, name + '.log')
                tail = open(lp, errors='replace').read().strip().split('\n')[-12:] if os.path.exists(lp) else []
                if st.optional:   # b119: the game runs without it (the intro movie): said, and the build goes on
                    s.done[name] = {}; s.skipped_optional.append(name)
                    log('[%2d/%d] LEFT OUT %s: %s (the data works without it)\n        log: %s\n          %s' % (k, n, st.title, res, lp, '\n          '.join(tail)))
                else:
                    s.failed.append(name)
                    log('[%2d/%d] FAILED %s: %s\n        log: %s\n          %s' % (k, n, st.title, res, lp, '\n          '.join(tail)))
            else:
                outs, dt, skipped = res; s.done[name] = {os.path.basename(p): v[2] for p, v in outs.items()}
                log('[%2d/%d] %s %s%s' % (k, n, 'skip' if skipped else 'done', st.title, '' if skipped else ' (%.0f s)' % dt))
        while todo or running:
            ready = [x for x in todo if all(d in s.done for d in s.steps[x].deps if d in s.steps) and not any(d in s.failed for d in s.steps[x].deps)]
            blocked = [x for x in todo if any(d in s.failed for d in s.steps[x].deps)]
            for x in blocked: todo.remove(x); s.failed.append(x); s.notrun.append(x); k += 1; log('[%2d/%d] not run (an earlier step failed): %s' % (k, n, s.steps[x].title))
            used = sum(s.steps[x].weight for x in running)
            for x in ready:
                w = min(cap, s.steps[x].weight)
                if used + w > cap and running: continue
                todo.remove(x); used += w; res = {}
                def go(x=x, res=res):
                    try: res['r'] = s.run_step(s.steps[x])
                    except Exception as e: res['r'] = e
                th = threading.Thread(target=go, daemon=True); th.start(); running[x] = (th, res)
                if s.steps[x].weight > 1 or used >= cap: break
            if not running and not ready and todo: break
            time.sleep(0.2)
            for x in list(running):
                th, res = running[x]
                if not th.is_alive(): del running[x]; finish(x, res['r'])
        return not s.failed

# ---------------------------------------------------------------- output: flat folder, split maps, manifest, check
def packed(p, work):
    """perf b56: the GLZ1-packed copy of p (made once, again when p is newer), in <work>/packed"""
    d = os.path.join(work, 'packed'); os.makedirs(d, exist_ok=True); q = os.path.join(d, os.path.basename(p))
    if not os.path.exists(q) or os.path.getmtime(q) < os.path.getmtime(p):
        import ctools
        subprocess.run([ctools.tool('glz'), 'c', p, q + '.tmp'], check=True, stdout=subprocess.DEVNULL); os.replace(q + '.tmp', q)
    return q

def collect(a, env):
    out = os.path.join(a.out, 'gitsfa'); os.makedirs(out, exist_ok=True)
    src = [env['GITSFA_BMAPS'], env['GITSFA_DATA'], env['GITSFA_AUDIOOUT'], os.path.join(env['GITSFA_AUDIOOUT'], 'audio')]
    names = ship_list(); missing = []; made = []
    for nm in names:
        base = nm[:-4] if nm.endswith(('.000', '.001')) else nm
        p = next((os.path.join(d, base) for d in src if os.path.exists(os.path.join(d, base))), None)
        if p is None:
            if nm == 'tut_intro.mp4': continue   # optional (b119: drawn from the install; or your own recording, --intro-video)
            missing.append(nm); continue
        dst = os.path.join(out, nm)
        if base.endswith(PACK): p = packed(p, a.work)
        if base in SPLIT:
            data = open(p, 'rb').read(); cut = SPLIT[base]
            piece = data[:cut] if nm.endswith('.000') else data[cut:]
            if not os.path.exists(dst) or os.path.getsize(dst) != len(piece) or open(dst, 'rb').read() != piece: open(dst, 'wb').write(piece)
        elif not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(p) or os.path.getmtime(dst) < os.path.getmtime(p):
            shutil.copyfile(p, dst)
        made.append(nm)
    for f in os.listdir(out):   # nothing else in the data folder (an old tut_intro.mp4 stays only when asked for)
        if f not in made and f != 'manifest.sha256' and f.endswith(('.mp4', '.gmap', '.000', '.001')) and f not in names: os.remove(os.path.join(out, f))
    man = {nm: sha256(os.path.join(out, nm)) for nm in sorted(made)}
    with open(os.path.join(out, 'manifest.sha256'), 'w') as f:
        for nm, h in man.items(): f.write('%s  %s\n' % (h, nm))
    return out, man, missing

def known_good():
    """(hashes, variable): known_good.sha256's '<sha256>  <name>' lines, and its '~ <name> video <WxH> <frames>' lines: files whose
    bytes can vary with the PC (video encodes: x264 / swscale take other code paths on CPUs without SSSE3, OpenCV's inpainting
    rounds differently in its Windows build), checked by decoding them when their hash differs; '~ <name> gfp <meshes> <indices>'
    lines (b60): the light first-person meshes, read and compared with those figures when their hash differs"""
    kg = {}; var = {}
    for ln in open(os.path.join(HERE, 'known_good.sha256')):
        if ln.startswith('~'): f = ln[1:].split(); var[f[0]] = (f[1], f[2], int(f[3]))   # (b122: also '~ <name> size <magic hex> <bytes>')
        elif ln.strip() and not ln.startswith('#'): h, nm = ln.split(None, 1); kg[nm.strip()] = h
    return kg, var

def decodes(path, spec):
    """a video file decodes completely, at the expected size and frame count"""
    kind, size, n = spec
    ff = os.environ.get('GITSFA_FFMPEG') or 'ffmpeg'
    try: r = subprocess.run([ff, '-v', 'error', '-i', path, '-map', '0:v:0', '-f', 'framemd5', '-'], capture_output=True, text=True)
    except OSError: return False
    lines = r.stdout.splitlines()
    return (r.returncode == 0 and not r.stderr.strip() and '#dimensions 0: ' + size in lines
            and sum(1 for l in lines if l and not l.startswith('#')) == n)

def light_mesh_ok(path, spec):
    """b60: a vm_<gun>.gfp (tools/lod/make_fp.py) whose bytes differ from the reference build's is well formed: it unpacks,
    has the reference's mesh count and its index total within 5 %"""
    kind, nmesh, nidx = spec; import ctools, struct, tempfile
    try:
        with tempfile.TemporaryDirectory() as td:
            q = os.path.join(td, 'x'); subprocess.run([ctools.tool('glz'), 'd', path, q], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            d = open(q, 'rb').read()
        magic, nm, nc = struct.unpack_from('<4sII', d, 0); o = 12 + 16 * nc; tot = 0
        if magic != b'GFP1' or nm != int(nmesh): return False
        for i in range(nm):
            nv, ni = struct.unpack_from('<II', d, o); o += 8 + nv * 32 + ni * 2 + (ni * 2) % 4; tot += ni
        return o == len(d) and abs(tot - nidx) <= 0.05 * nidx
    except Exception: return False

def same_shape(path, spec):
    """b122: a file of a kind whose numbers come out of floating-point maths (light samples, colour grading fits, skies, the
    animation banks' quantised quaternions, the radar pictures, navigation edges, effect tables): the C library's sin / exp /
    pow and numpy's SIMD paths round the last bit differently on Windows, Linux and other CPUs. When its hash differs it is
    checked to be the same kind of file (first 4 bytes) and exactly the reference's size."""
    kind, magic, size = spec
    try:
        with open(path, 'rb') as f: m = f.read(4)
        return m.hex() == magic and os.path.getsize(path) == size
    except OSError: return False

def check(man, missing, out=None, dev=False, own_intro=False):
    kg, var = known_good()
    if own_intro: kg.pop('tut_intro.mp4', None)   # (--intro-video: your own recording is not compared with the drawn intro)
    ok = [n for n in man if kg.get(n) == man[n]]; diff = [n for n in man if n in kg and kg[n] != man[n]]
    new = [n for n in man if n not in kg and not (own_intro and n == 'tut_intro.mp4')]
    vok = [n for n in diff if n in var and var[n][0] == 'video' and out and decodes(os.path.join(out, n), var[n])]
    lok = [n for n in diff if n in var and var[n][0] == 'gfp' and out and light_mesh_ok(os.path.join(out, n), var[n])]   # b60
    sok = [n for n in diff if n in var and var[n][0] == 'size' and out and same_shape(os.path.join(out, n), var[n])]   # b122
    dv = [n for n in diff if dev and n.endswith('.gmm') and not n.endswith('_em.gmm')]   # --dev: the radar step reuses old PNGs
    real = [n for n in diff if n not in vok and n not in dv and n not in lok and n not in sok]
    log('check against known_good.sha256: %d of %d files ok (%d identical to the reference build%s%s)' % (
        len(ok) + len(vok) + len(dv) + len(lok) + len(sok), len(man), len(ok), ', %d video(s) encoded with slightly different rounding on this PC, checked by '
        'decoding them' % len(vok) if vok else '', ', %d radar file(s) from the dev path' % len(dv) if dv else '')
        + (', %d light first-person mesh(es) rounded differently on this PC, checked by reading them' % len(lok) if lok else '')
        + (', %d file(s) of computed numbers rounded differently on this PC, checked for kind and size' % len(sok) if sok else ''))
    if real: log('  DIFFERENT from the reference build: ' + ', '.join(real) + ' (the data may still work; please report it)')
    if new: log('  not in the reference list: ' + ', '.join(new))
    if missing: log('  MISSING: ' + ', '.join(missing))
    return len(missing) == 0

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', help='the install\'s "Game Files" folder (the one with Data\\map.dat)')
    ap.add_argument('--out', required=True, help='where to put the result (OUT/gitsfa/)')
    ap.add_argument('--work', help='work folder (default: OUT/work)')
    ap.add_argument('--jobs', type=int, default=min(4, os.cpu_count() or 2), help='steps run at once (default: up to 4)')
    ap.add_argument('--intro-video', help='your own recording of the game\'s intro movie, used instead of the one the builder draws from the install (optional)')
    ap.add_argument('--intro-start', type=float, default=23.7, help='where the intro starts in that video, seconds (default 23.7)')
    ap.add_argument('--only', help='only these steps / groups (comma separated), and what they need')
    ap.add_argument('--nice', default='0' if WIN else '10', help=argparse.SUPPRESS)
    ap.add_argument('--keep-work', action='store_true', help='keep the work folder (default: kept; delete it yourself when done)')
    ap.add_argument('--check-only', action='store_true', help='only re-check OUT/gitsfa against the reference hashes')
    ap.add_argument('--dev', action='store_true', help=argparse.SUPPRESS)
    a = ap.parse_args()
    a.out = os.path.abspath(a.out); a.work = os.path.abspath(a.work or os.path.join(a.out, 'work'))
    if a.check_only:
        out = os.path.join(a.out, 'gitsfa'); man = {}
        for ln in open(os.path.join(out, 'manifest.sha256')): h, nm = ln.split(None, 1); man[nm.strip()] = h
        return 0 if check(man, [], out, a.dev) else 1
    if not a.dev:
        if not a.game: ap.error('--game is needed: the "Game Files" folder of your install')
        a.game = os.path.abspath(a.game)
        need = ['map.dat', 'model.dat', 'weapon.dat', 'effect.dat', 'ui.dat', 'world.dat', 'worldEnv.dat', os.path.join('audio', 'Fmod_Build', 'sac.fev'), 'anim']
        gone = [n for n in need if not os.path.exists(os.path.join(a.game, 'Data', n))]
        if gone: log('These are missing from %s: %s' % (os.path.join(a.game, 'Data'), ', '.join(gone))); return 2
    if a.intro_video and not os.path.exists(a.intro_video): log('--intro-video: no such file: ' + a.intro_video); return 2
    env = environment(a); S = steps(a)
    if a.only:
        want = set(x.strip() for x in a.only.split(',')); keep = set()
        def add(n):
            if n in keep: return
            keep.add(n); [add(d) for d in by[n].deps if d in by]
        by = {x.name: x for x in S}
        for x in S:
            if x.name in want or x.group in want: add(x.name)
        S = [x for x in S if x.name in keep]
    for k in ('GITSFA_TMP', 'GITSFA_DATA', 'GITSFA_BMAPS', 'GITSFA_AUDIOOUT'): os.makedirs(env[k], exist_ok=True)
    log('GITS:FA Vita data builder: %d steps, %d at a time; work folder %s' % (len(S), a.jobs, a.work))
    t0 = time.time(); R = Runner(a, env, S); ok = R.run(a.jobs)
    if not ok:
        log('Stopped: %d step(s) failed%s (see above). Fix the cause and run again: finished steps are kept.'
            % (len(R.failed) - len(R.notrun), ', %d more not run because of that' % len(R.notrun) if R.notrun else '')); return 1
    if a.only: log('done (%s only, %.0f s)' % (a.only, time.time() - t0)); return 0
    out, man, missing = collect(a, env)
    good = check(man, missing, out, a.dev, bool(a.intro_video))
    if R.skipped_optional: log('  left out (the game works without): ' + ', '.join(R.steps[x].title for x in R.skipped_optional))
    log('%s: %d files, %.0f MB, in %.0f min. Copy the folder "%s" to ux0:data/ on the Vita.' % ('Done' if good else 'Finished with problems', len(man),
        sum(os.path.getsize(os.path.join(out, n)) for n in man) / 1e6, (time.time() - t0) / 60, out))
    return 0 if good else 1

if __name__ == '__main__':
    sys.exit(main())
