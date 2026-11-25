# databuilder: per-map numbers read from the game's own map ini files (map.dat map\FixedMap\Fix_<Map>\), so that no table of
# them has to ship with the tools:
#   fog(stem)   -> ([r, g, b], [start cm, density 1/cm, amount])   from World.ini [NIFOG] FogInfo (colour) and
#                  [HEIGHTFOG] HeightFogInfo (start, density, amount): the values the engine's MAPS[] table carries
#   actor(stem) -> ((amb r, g, b), (dif ...), (spec ...), dimmer) or None   from light.ini [ACTOR] (tutorial / tutorial2: none)
# The ini values are hex-encoded little-endian records with a trailing checksum byte (sum of the bytes, mod 256):
#   FogInfo        u8 on, 3 bytes, f32, u8 r, g, b, a                                   (12 bytes + checksum)
#   HeightFogInfo  f32 x 8: band bottom, ?, band top, start, ?, density, amount, ?       (32 bytes + checksum)
#                  (checked on all 17 maps against the engine's former MAPS[] table; amount 0 when the band is empty)
# Where: <MAPSRC>/Fix_<Map>/ (the extracted map folders: extract.py puts every map's World.ini there) or
# <DEMX>/map/FixedMap/Fix_<Map>/ (light.ini).
import os, sys, struct
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(H, '..'))
import gitspaths

FIX = {'downtown': 'Fix_Downtown', 'berutalube': 'Fix_Berutalube', 'ecenter': 'Fix_Ecenter', 'ghost_storage': 'Fix_Ghost_Storage',
       'headquarter': 'Fix_Headquarter', 'port': 'Fix_Port', 'pss9': 'Fix_PSS9', 'ruinednight': 'Fix_RuinedNight',
       'ruinedtokyo': 'Fix_RuinedTokyo', 'trainstation': 'Fix_Trainstation', 'underground': 'Fix_Underground', 'urban': 'Fix_Urban',
       'zioplant': 'Fix_Zioplant', 'shooting': 'Fix_Shooting', 'tutorial': 'Fix_Tutorial', 'tutorial2': 'Fix_Tutorial2', 'tutorial3': 'Fix_Tutorial3'}

def _dirs(stem):
    fix = FIX[stem]; out = []
    for key, sub in (('MAPSRC', (fix,)), ('DEMX', ('map', 'FixedMap', fix))):
        try: out.append(os.path.join(gitspaths.root(key), *sub))
        except SystemExit: pass
    return out

def find(stem, name):
    """the map's <name> (any letter case), or None"""
    for d in _dirs(stem):
        if os.path.isdir(d):
            for f in sorted(os.listdir(d)):
                if f.lower() == name.lower(): return os.path.join(d, f)
    return None

def ini(path):
    """{section: {key: value}} (keys lower case)"""
    out = {}; sec = None
    for ln in open(path, encoding='latin-1'):
        ln = ln.strip()
        if ln.startswith('[') and ln.endswith(']'): sec = ln[1:-1].strip().upper(); out.setdefault(sec, {})
        elif '=' in ln and sec: k, v = ln.split('=', 1); out[sec][k.strip().lower()] = v.strip()
    return out

def record(hexstr, what):
    b = bytes.fromhex(hexstr)
    if sum(b[:-1]) & 255 != b[-1]: raise ValueError('%s: checksum mismatch' % what)
    return b[:-1]

def fog_from_world_ini(path):
    s = ini(path)
    f = record(s['NIFOG']['foginfo'], path + ' FogInfo'); h = record(s['HEIGHTFOG']['heightfoginfo'], path + ' HeightFogInfo')
    col = [f[8] / 255, f[9] / 255, f[10] / 255]
    v = struct.unpack('<8f', h[:32])
    # the height band is v[0]..v[2] (cm); the VR rooms (shooting, tutorial3) have it upside down (1500 .. 80): no fog there
    return [col, [float(v[3]), float(v[5]), float(v[6]) if v[0] < v[2] else 0.0]]

def fog(stem):
    p = find(stem, 'World.ini')
    if p: return fog_from_world_ini(p)
    raise SystemExit('%s: World.ini not found in the extracted map folders (%s)' % (stem, ', '.join(_dirs(stem))))

def actor(stem):
    p = find(stem, 'light.ini')
    if not p: return None
    a = ini(p).get('ACTOR', {})
    if not a or a.get('use', '1').strip() == '0': return None
    rgb = lambda k: tuple(int(x) for x in a['actor_' + k].split('.'))
    return (rgb('ambient'), rgb('diffuse'), rgb('specular'), float(a['actor_dimmer']))

if __name__ == '__main__':   # print what the install gives (dev: compare with the old tables)
    for st in sys.argv[1:] or sorted(FIX):
        print(st, 'World.ini' if find(st, 'World.ini') else '-', fog(st), 'light.ini' if find(st, 'light.ini') else '-', actor(st))
