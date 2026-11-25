# b68: the original's material values for the models' highlights and reflections, read from the user's own NIFs
# (weapon.dat / model.dat, ANIMOUT) at build time - nothing of them is kept in the tools.
# The PC draws weapons, hands and operators with WeaponMaterial / WeaponColorMaterial (shaders/data/generated *.cache,
# ps_3_0; same terms in both):
#   reflection = cube(lod (1 - C.a) x g_CubeMapLodLevelCount) x g_CubeMapBrightness x Gloss(_S) x C.b x Reflact_Value
#                x (1 - (1 - n.v)^3)^Reflact_Falloff
#   highlight  = (n.h)^(C.r x 255) x light specular x Specular_Value x NiMaterialProperty specular x Gloss(_S)
# (C = the 'Shader' map, the material's shader map 0, *_C.dds: r = highlight power / 255, g = ambient occlusion, b = how
# much it reflects (metal; 0 on rubber / plastic), a = smoothness.) Per mesh this module gives the files and numbers;
# b112: WGStandardMaterial (the Ghost Assault cyborg's body, cy_male.nif - the only mesh of the user's files with that shader,
# a gloss map and an environment NiTextureEffect; WGStandardMaterial_ps_3_0 variant 0504631F-10000000-00000000-00A90010-00005000):
#   colour = base x (n.l x light x MaterialDiffuse + MaterialAmbient x ambient + emissive) + Glow x Glow_Value
#            + Gloss(_S) x (cube(reflection) + (n.h)^MaterialPower x light specular x MaterialSpecular)
# i.e. the reflection's weight is the gloss map itself (no Reflact_Value, no shader map, no Fresnel fade, no cube lod):
# 'wgenv' in the record (gvmlib.mask_images). The cube is the NIF's own (cybogCUBE.dds: gvmlib.Builder.envmap).
# gvmlib.mask_sample turns them into the two per-vertex weights the engine reads (model.h MD_VS_MASK).
# b115: 'rim' = g_RimPower, 'fres' = Fresnel_Value, 'fresc' = Fresnel_Color. WeaponMaterial ps (variant 050CA31F-...-00000470):
#   light = g_DirDiffuse x (g_LightSpectrumMin + w x (1 + g_LightSpectrumOverflow - g_LightSpectrumMin)), w = half Lambert
#           (0.5 n.l + 0.5)^g_HalfLambertPower, lifted to 1 where the normal map leaves the surface normal (BackLight_Normal_*)
#   light = lerp(light, Fresnel_Color x F, saturate(F)), F = Fresnel_Value x (1 - |n.v|)^(8 x Fresnel_Falloff)
#   colour = (MaterialDiffuse x light + ...) x base x Diffuse_Value (x AO)  +  rim  + reflection + highlight
#   rim = (1 - |N.v|)^g_RimExp x g_DirDiffuse x g_RimPower x max(0, light direction . to the eye)   (N: the surface normal; ADDED)
# Of 577 colour maps: g_RimPower 0 on 513 (no number), 0.1-0.5 on 61, 0.8 on 2 (Sitara), 2.0 on 1 (Reiko_Hair_D - with
# Diffuse_Value 0.5 and MaterialDiffuse 0.59 where everything else has ~1.0 / 0.99). gvmlib: 'sheen' materials (SHEEN_RIM_MIN).
import os, sys, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools', 'mapvis'))
import gitspaths
_DB = None
_TIES = None   # (tests: every candidate per colour map)

def _index(root):
    """lower-case file name -> every path under root"""
    ix = {}
    for dp, dn, fn in os.walk(root):
        dn.sort()   # (walk order decides ties in _resolve: the same on every machine)
        for f in sorted(fn): ix.setdefault(f.lower(), []).append(os.path.join(dp, f))
    return ix

def _resolve(ix, nifdir, name):
    """the texture a NIF names: of the files with that name, the one whose folder shares the most with the NIF's"""
    c = ix.get(os.path.basename(name or '').lower())
    if not c: return None
    nd = os.path.normcase(os.path.abspath(nifdir))
    return max(c, key=lambda p: (len(os.path.commonpath([nd, os.path.normcase(os.path.abspath(p))])), -len(p)))

def _extra(h, k):
    import nifmat
    from nifmesh import S
    d = h['d']; t, o, s = h['blocks'][k]
    if t == 'NiColorExtraData': return S(h, struct.unpack_from('<I', d, o)[0]), None
    return nifmat.extra(h, k)

def _fresc(h, m):
    """the mesh's Fresnel_Color (a NiColorExtraData of the mesh; the first of the NIF otherwise) or None"""
    try:
        from nifmesh import S
        d = h['d']; first = None
        for k, (t, o, s) in enumerate(h['blocks']):
            if t == 'NiColorExtraData' and S(h, struct.unpack_from('<I', d, o)[0]) == 'Fresnel_Color':
                c = tuple(round(x, 4) for x in struct.unpack_from('<3f', d, o + 4))
                if k in (m.get('extra_blocks') or ()): return c
                if first is None: first = c
        return first
    except Exception: return None

def build(roots=None):
    """{os.path.normcase(abspath of the base texture): record}; record = dict(S, C (paths or None), refl, spv, bright, gloss, spec,
    ao = g_AOPower, dval = Diffuse_Value, b71: G = the glow (emissive) map or None, guv = its uv set, glow = Glow_Value)"""
    import nifmat
    from nifmesh import mesh
    out = {}
    roots = roots or [gitspaths.dirp('ANIMOUT', 'weapon'), gitspaths.dirp('ANIMOUT', 'model'), gitspaths.dirp('SKRAW', 'weapon')]   # (SKRAW: the arm launcher)
    for root in roots:
        if not os.path.isdir(root): continue
        ix = _index(root)
        for dp, dn, fn in os.walk(root):
            dn.sort()   # (the same order on every machine: ties keep the first)
            for f in sorted(fn):
                if not f.lower().endswith('.nif'): continue
                try: h, ms = nifmat.walk_all(os.path.join(dp, f))
                except Exception: continue
                for m in ms:
                    P = m['props']; tp = P.get('NiTexturingProperty', (0, {}))[1]
                    base = tp.get('base', {}).get('file') if isinstance(tp.get('base'), dict) else None
                    if not base: continue
                    X = {}
                    t, o, s = h['blocks'][m['k']]
                    me = mesh(h['d'], o)
                    msz = sum(h['blocks'][q['ref']][2] for q in me['streams'] if 0 <= q['ref'] < len(h['blocks']))   # (vertex + index bytes)
                    for e in me['extra']:
                        if 0 <= e < len(h['blocks']):
                            try: nm, v = _extra(h, e); X[str(nm)] = v
                            except Exception: pass
                    bp = _resolve(ix, dp, base)
                    if not bp: continue
                    g = tp.get('gloss', {}).get('file') if isinstance(tp.get('gloss'), dict) else None
                    gw = tp.get('glow') if isinstance(tp.get('glow'), dict) else None   # b71: the emissive map (gvmlib.emissive_image)
                    c = tp.get('shader0', {}).get('file') if isinstance(tp.get('shader0'), dict) else None
                    mat = P.get('NiMaterialProperty', (0, {}))[1].get('v')
                    # b112: the mesh's shader (its first material name) and whether the NIF carries an environment NiTextureEffect
                    try:
                        from nifmesh import avobj, S
                        _a, _o = avobj(h['d'], o); _n = struct.unpack_from('<I', h['d'], _o)[0]
                        shader = S(h, struct.unpack_from('<I', h['d'], _o + 4)[0]) if _n else ''
                    except Exception: shader = ''
                    fx = any(b[0] == 'NiTextureEffect' for b in h['blocks'])
                    rec = dict(S=_resolve(ix, dp, g) if g else None, C=_resolve(ix, dp, c) if c else None,
                               refl=float(X['Reflact_Value']) if 'Reflact_Value' in X else 0.0,
                               spv=float(X.get('Specular_Value', 1.0)) if isinstance(X.get('Specular_Value', 1.0), (int, float)) else 1.0,
                               bright=float(X.get('g_CubeMapBrightness', 1.0)) if isinstance(X.get('g_CubeMapBrightness', 1.0), (int, float)) else 1.0,
                               gloss=float(mat[12]) if mat else 0.0, spec='NiSpecularProperty' in P, nif=os.path.join(dp, f),
                               ao=float(X['g_AOPower']) if isinstance(X.get('g_AOPower'), (int, float)) else 0.0,
                               dval=float(X['Diffuse_Value']) if isinstance(X.get('Diffuse_Value'), (int, float)) else 1.0,
                               G=_resolve(ix, dp, gw.get('file')) if gw and gw.get('file') else None, guv=int(gw.get('uvset', 0)) if gw else 0,
                               glow=float(X['Glow_Value']) if isinstance(X.get('Glow_Value'), (int, float)) else 1.0,
                               wgenv=bool(shader == 'WGStandardMaterial' and g and fx and 'Reflact_Value' not in X),   # b112 (below)
                               # b115: the rim light and the Fresnel light of the material (see 'sheen' in the header)
                               rim=float(X['g_RimPower']) if isinstance(X.get('g_RimPower'), (int, float)) else 0.0,
                               fres=float(X['Fresnel_Value']) if isinstance(X.get('Fresnel_Value'), (int, float)) else 0.0,
                               fresc=_fresc(h, m))
                    k = os.path.normcase(os.path.abspath(bp))
                    old = out.get(k)
                    # several meshes / NIFs can share a colour map: keep the one that has the material's numbers, preferring 1st-person NIFs;
                    # equal scores keep the first (sorted walk: the same pick on every machine)
                    score = (rec['S'] is not None, 'Reflact_Value' in X, '1st' in dp.lower() or 'commonhand' in dp.lower(), '_tps' in f.lower() or 'body' in f.lower(), msz)
                    # (the body NIF over a goggle's / hair's; in one NIF the biggest mesh - the gun's body over its sticker)
                    if old is None or score > old[0]: out[k] = (score, rec)
                    if _TIES is not None: _TIES.setdefault(k, []).append((score, rec))
    return {k: v[1] for k, v in out.items()}

def lookup(texpath):
    global _DB
    if _DB is None: _DB = build()
    return _DB.get(os.path.normcase(os.path.abspath(texpath)))
