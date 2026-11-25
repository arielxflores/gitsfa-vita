# Where the converters find their inputs and put their outputs (databuilder). No machine paths in the tools themselves:
# every folder comes from an environment variable GITSFA_<KEY>, or from GITSFA_WORK + the key's place in the builder's
# work tree (tools/databuilder/build_data.py sets GITSFA_WORK). For the old container layout, source tools/devpaths.sh.
#   import gitspaths as GP;  GP.path('MAPSRC', 'Fix_Urban')  ->  <maps source folder>/Fix_Urban
import os
# key: (place under GITSFA_WORK, what it is)
LAYOUT = {
    'MAPSRC':   ('x/maps',            'map.dat map\\FixedMap\\Fix_<Map>\\ per map (+ tex/*.png at most 512 px, 1024 where the original is (b69), Lightmapping/*.png at the full size of the original (b69))'),
    'MAPVIS':   ('x/raw/mapvis',      'glow/ (map.dat glow maps), env/ + gloss/ (b67: the reflection pictures and gloss maps), wff/<Map>.wff, sky/{skydome,cybersky}, effect/{map,tutorial,texture}'),
    'DEMX':     ('x/raw/dem/dem_x',   'map.dat Fix_* spawn / safe-zone / area files, worldEnv.dat EnvBomb, model.dat Objects/DM'),
    'SKRAW':    ('x/raw/skills',      'skill models / effects / weapons (model.dat, weapon.dat, effect.dat, ui.dat) + png/'),
    'WORLD':    ('x/raw/world',       'world.dat world\\TutorialStep.txt, TutShootBot*.tsd'),
    'ANIMBIN':  ('anim',              'Data/anim/<GIS_*_runtimeBinary> (morpheme runtime binaries)'),
    'ANIMOUT':  ('x/anim/out',        'model.dat model\\..., weapon.dat weapon\\... (NIF + DDS)'),
    'W3':       ('x/w3',              'weapon.dat weapon\\<DIR>\\3rd, Parts, Textures'),
    'WRAW':     ('x/wraw',            'weapon.dat: out/weapon/AR_SEBURO_C30 + IronSight (c30.gmdl)'),
    'FX':       ('x/fx',              'png/ (effect.dat textures as PNG)'),
    'UI':       ('x/ui',              'ui.dat: ui/GFx_DA/*.swf, ui/GFx_DA/Data/*.dds, ui/map/*.dds'),
    'UI2':      ('x/ui2',             'ui.dat: ui/GFx_DA/*.swf (menus, Intro, ResourceTemplate, Localization)'),
    'MENURSC':  ('x/menursc',         'ui.dat: menu/ui/GFx_DA/Data/rsc/map/{loading/background,thumbnail}'),
    'TUTSRC':   ('x/tut/src',         'map.dat / effect.dat tutorial textures'),
    'USM':      ('x/usm',             'tutorial_1..6.m1v (ui.dat tutorial videos, demuxed)'),
    'CG':       ('x/cg',              'map.dat colour-grading LUTs (*.tga)'),
    'MINIMAP':  ('x/minimap',         'minimap/<img>.png radar images (512 px) + minimap.txt'),
    'AS':       ('x/as',              'ActionScript Matrix3D tables of InGameUI(_D).swf (ingame_as/, d_as/)'),
    'FMOD':     ('fmod',              'Data/audio/Fmod_Build (sac.fev, sac.h, *.fsb)'),
    'FSBDEC':   ('x/fsbdec',          'decrypted banks + pcm/<bank>/*.npy'),
    'SDF':      ('x/maps',            'map.dat Fix_<Map>\\SoundData.sdf'),
    'PARAMS':   (None,                'tools/databuilder/params (our own parameter files)'),
    'DATA':     ('out/data',          'output: data files'),
    'BMAPS':    ('out/maps',          'output: map files'),
    'AUDIOOUT': ('out/data',          'output: sound packs (+ audio/ streams)'),
    'TMP':      ('tmp',               'scratch'),
}
HERE = os.path.dirname(os.path.abspath(__file__))
def root(key):
    v = os.environ.get('GITSFA_' + key)
    if v: return v
    if key == 'PARAMS': return os.path.join(HERE, 'databuilder', 'params')
    w = os.environ.get('GITSFA_WORK')
    if w and LAYOUT.get(key, (None,))[0]: return os.path.join(w, LAYOUT[key][0])
    raise SystemExit('gitspaths: set GITSFA_%s (%s) or GITSFA_WORK (tools/databuilder/build_data.py does; '
                     'in the dev container: source tools/devpaths.sh)' % (key, LAYOUT.get(key, ('', '?'))[1]))
def path(key, *sub):
    """GITSFA_<key> joined with sub parts; a trailing '' gives a trailing separator"""
    return os.path.join(root(key), *sub)
def dirp(key, *sub):
    """the folder with a trailing '/', for the tools that build paths by string concatenation"""
    p = os.path.join(root(key), *sub)
    return p if p.endswith(('/', os.sep)) else p + '/'
def tmp(*sub):
    p = path('TMP', *sub); os.makedirs(os.path.dirname(p) if sub else p, exist_ok=True); return p
