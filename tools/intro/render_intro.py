#!/usr/bin/env python3
"""b119: the game's intro cut scene as a video, drawn on the player's own PC from the player's own install.
The PC game plays the scene live (world.dat world\\IntroMovie: MovieMaker.txt - one room, one character file with every
actor's animation on one timeline, ten shots = a camera file + a time range + meshes to hide + a sound); the Vita plays a
video (tut_video.h, ux0:data/gitsfa/tut_intro.mp4). This script walks the shots at 30 fps, hands every frame's geometry
to tools/intro/introrender.c (a software rasteriser, no GPU needed) and finishes each picture (exposure, glow,
colour grading, the fades).
b120: the picture is the PC's own arithmetic now. The actors are drawn with the PC's material shaders (WeaponMaterial,
WGStandardMaterial: normal map, shader map, gloss, cube map, Fresnel and rim light, the room's light probes), the room with
its CustomMaterial terms (relief, highlight, sphere map), from the numbers each mesh carries; the light is the room's own
(light.ini [ACTOR], the SUN light's direction, LightProbeCloud.lpf); the post-processing follows the PC's passes. What
the files do not state is measured against a recording of the PC's intro and marked MEASURED below.
usage: render_intro.py <IntroMovie folder> <tut_intro.mp4> [--jobs n] [--fmod <Data/audio/Fmod_Build>] [--tmp dir]
           the film: frames on n processes, the scene's music and voice tracks from the install's sound banks, H.264 + AAC
       render_intro.py <IntroMovie folder> <folder> [--frames a:b] [--every n] [--list f,f,...] [--sheet file.jpg] [--post 0]
           development: pictures of single frames
       (GITSFA_EXE_INTRORENDER: the compiled introrender; GITSFA_FFMPEG, GITSFA_FMOD as the other builder tools)
"""
import os, sys, struct, subprocess, math, numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H); sys.path.insert(0, os.path.join(H, '..'))
from introlib import *   # (Nif, movie, Baked, camera_track, FPS; make_scene's keyval, ctime)
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
MF_UV1, MF_VCOL, MF_STATIC, MF_ATEST, MF_ADD, MF_ALPHA, MF_UNLIT, MF_CULL, MF_NOZ, MF_LMADD, MF_DARKUV1, MF_CLAMPA, MF_CLAMPB, MF_NOFLIP = 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192

NP = 31; ZSH = bytes(27 * 4); ID9 = np.eye(3, dtype='<f4').tobytes()   # (introrender.c: the material's numbers P[], the probe)
P_DIFV, P_AO, P_HLP, P_LSMIN, P_LSOVER, P_BLPOW, P_BLEXP, P_FRFALL, P_FRVAL, P_FRC, P_IBL, P_CLOD, P_CBRIGHT, P_RFALL, P_RVAL, P_RIMEXP, P_RIMPOW, P_MDIF, P_MSPEC, P_MAMB, P_MEMI, P_GLOWV, P_MPOW = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 19, 20, 23, 26, 29, 30
# ---------------------------------------------------------------- the rasteriser's pipe
class Raster:
    def __init__(s, exe, w, h, ss):
        import tempfile
        kw = {'creationflags': 0x08000000} if os.name == 'nt' else {}
        s.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE, **kw); s.w = w; s.h = h; s.ntex = 0; s.nmesh = 0; s.texid = {}; s.nv = {}; s.cubeid = {}; s.kind = {}
        # the pictures come back through a file (a pipe hands 8 MB over in small pieces); through the pipe where the folder's
        # name cannot be given to the C program (Windows: letters outside the system's code page)
        try:
            d = os.environ.get('GITSFA_INTRO_TMP') or os.environ.get('GITSFA_TMP') or None
            if d: os.makedirs(d, exist_ok=True)
            fd, s.ans = tempfile.mkstemp(prefix='introframe_', suffix='.bin', dir=d); os.close(fd)
            pb = s.ans.encode('mbcs', 'strict') if os.name == 'nt' else os.fsencode(s.ans)
            if len(pb) > 1000 or (os.name == 'nt' and pb.decode('mbcs') != s.ans): raise ValueError
        except Exception: s.ans = None; pb = b''
        s.p.stdin.write(b'I' + struct.pack('<4i', w, h, ss, len(pb)) + pb)
    def tex(s, key, loader):
        """a texture by its key (a path, or any name with a loader returning an RGBA uint8 array); -1 when it cannot be had"""
        if key in s.texid: return s.texid[key]
        try: a = loader()
        except Exception as e: print('  texture %s: %s' % (key, e)); a = None
        if a is None: s.texid[key] = -1; return -1
        a = np.ascontiguousarray(a, 'u1'); i = s.ntex; s.ntex += 1; s.texid[key] = i
        s.p.stdin.write(b'T' + struct.pack('<3i', i, a.shape[1], a.shape[0])); s.p.stdin.write(a.tobytes()); return i
    def cube(s, key, loader):
        """a cube map by its key (loader -> six (n, n, 3) float faces, +X -X +Y -Y +Z -Z); -1 when it cannot be had"""
        if key in s.cubeid: return s.cubeid[key]
        try: fc = loader()
        except Exception as e: print('  cube map %s: %s' % (key, e)); fc = None
        if fc is None: s.cubeid[key] = -1; return -1
        i = len([v for v in s.cubeid.values() if v >= 0]); s.cubeid[key] = i; n = fc[0].shape[0]
        s.p.stdin.write(b'C' + struct.pack('<2i', i, n))
        for f in fc: s.p.stdin.write(np.ascontiguousarray(f[..., :3], '<f4').tobytes())
        return i
    def mesh(s, uv0, idx, flags=0, tex=-1, lm=-1, glow=-1, spec=-1, dark=-1, color=(1, 1, 1, 1), glow_gain=1.0, spec_gain=0.0, uv1=None, vcol=None, pos=None, nrm=None,
             kind=0, nmap=-1, cmap=-1, cube=-1, P=None, tan=None, bin=None):
        i = s.nmesh; s.nmesh += 1; nv = len(uv0); s.nv[i] = nv; s.kind[i] = kind
        if uv1 is not None: flags |= MF_UV1
        if vcol is not None: flags |= MF_VCOL
        if pos is not None: flags |= MF_STATIC
        w = s.p.stdin.write; w(b'M' + struct.pack('<9i', i, nv, len(idx), flags, tex, lm, glow, spec, dark) + struct.pack('<6f', *color, glow_gain, spec_gain) + struct.pack('<4i', kind, nmap, cmap, cube) + np.asarray(P if P is not None else np.zeros(NP), '<f4').tobytes())
        w(np.ascontiguousarray(uv0, '<f4').tobytes())
        if uv1 is not None: w(np.ascontiguousarray(uv1, '<f4').tobytes())
        if vcol is not None: w(np.ascontiguousarray(vcol, 'u1').tobytes())
        w(np.ascontiguousarray(idx, '<u4').tobytes())
        if pos is not None:
            w(np.ascontiguousarray(pos, '<f4').tobytes()); w(np.ascontiguousarray(nrm, '<f4').tobytes())
            if kind: w(np.ascontiguousarray(tan, '<f4').tobytes()); w(np.ascontiguousarray(bin, '<f4').tobytes())
        return i
    def frame(s, eye, dr, up, tanx, tany, items, amb=(0.3, 0.3, 0.3), sky=(0.3, 0.3, 0.3), lights=(), rim=(0, 0, 0, 0), zn=2.0, zf=100000.0, actor=None):
        """items: [(mesh, colour4, uva6, uvb6, base texture or -2, pos or None, nrm or None, tangents, binormals, probe (27) or None, glow map's uv6 or None)] in drawing order;
        actor: (direction the actors' light travels, diffuse, specular, ambient) -> (rgb float32 (h, w, 3), depth (h, w))"""
        w = s.p.stdin.write; w(b'F' + struct.pack('<13f', *eye, *dr, *up, tanx, tany, zn, zf) + struct.pack('<6fi', *amb, *sky, len(lights)))
        for d, c in lights: w(struct.pack('<6f', *d, *c))
        ac = actor or ((0, 0, -1), (1, 1, 1), (0, 0, 0), (0, 0, 0))
        w(struct.pack('<4f', *rim) + struct.pack('<12f', *ac[0], *ac[1], *ac[2], *ac[3]) + struct.pack('<i', len(items)))
        for m, col, ua, ub, tex, pos, nrm, tan, bin, sh, uc, em in items:
            w(struct.pack('<i22fi', m, *col, *ua, *ub, *(uc or ID6), tex) + (np.asarray(sh, '<f4').tobytes() if sh is not None else ZSH) + (np.asarray(em, '<f4').tobytes() if em is not None else ID9) + struct.pack('<i', 1 if pos is not None else 0))
            if pos is not None:
                w(np.ascontiguousarray(pos, '<f4').tobytes()); w(np.ascontiguousarray(nrm, '<f4').tobytes())
                if s.kind[m]: w(np.ascontiguousarray(tan, '<f4').tobytes()); w(np.ascontiguousarray(bin, '<f4').tobytes())
        s.p.stdin.flush(); n = s.w * s.h
        if s.ans:
            if s.p.stdout.read(1) != b'K': raise RuntimeError('introrender stopped (exit %s)' % s.p.poll())
            buf = np.fromfile(s.ans, '<f4')
        else: buf = np.frombuffer(s.p.stdout.read(n * 16), '<f4')
        if len(buf) != n * 4: raise RuntimeError('introrender stopped (exit %s)' % s.p.poll())
        return buf[:n * 3].reshape(s.h, s.w, 3), buf[n * 3:].reshape(s.h, s.w)
    def close(s):
        try: s.p.stdin.write(b'Q'); s.p.stdin.close(); s.p.wait(10)
        except Exception: pass
        try: os.remove(s.ans)
        except (OSError, TypeError): pass

# ---------------------------------------------------------------- files
class Files:
    """the IntroMovie folder's pictures by their bare name, any case"""
    def __init__(s, root):
        s.by = {}
        for d, _, fs in os.walk(root):
            for f in fs: s.by.setdefault(f.lower(), os.path.join(d, f))
    def find(s, name):
        if not name: return None
        b = os.path.basename(name.replace('\\', '/')).lower()
        if b in s.by: return s.by[b]
        st = os.path.splitext(b)[0]
        for e in ('.dds', '.tga', '.png'):
            if st + e in s.by: return s.by[st + e]
        return None
def load_rgba(path, maxs=2048):
    im = Image.open(path); im.load(); im = im.convert('RGBA')
    if max(im.size) > maxs: im.thumbnail((maxs, maxs), Image.LANCZOS)
    return np.asarray(im)

def vnormals(P, I):
    N = np.zeros_like(P, dtype='f8'); t = I.reshape(-1, 3); fn = np.cross(P[t[:, 1]] - P[t[:, 0]], P[t[:, 2]] - P[t[:, 0]])
    for k in range(3): np.add.at(N, t[:, k], fn)
    return (N / (np.linalg.norm(N, axis=1, keepdims=True) + 1e-12)).astype('f4')

# ---------------------------------------------------------------- the room (static, lightmapped)
ROOM_RELIEF = os.environ.get('INTRO_RELIEF', '1') != '0'; ROOM_ENV = os.environ.get('INTRO_RENV', '1') != '0'; ROOM_SPEC = float(os.environ.get('INTRO_RSPEC', '1.0'))   # (tests)
def room_items(R, F, mapnif):
    import map_visual as MV
    out = []
    for m in MV.scene(mapnif):
        P = m['props']; Sx = m['streams']; tp = P.get('NiTexturingProperty', {}); base = tp.get('base', {}).get('file')
        if ('POSITION', 0) not in Sx or ('INDEX', 0) not in Sx: continue
        if 'umbraalpha' in m['path'].lower() and (not base or base.lower().startswith('base_d')): continue
        Pos = Sx[('POSITION', 0)][0]; I = Sx[('INDEX', 0)][0].ravel().astype(np.int64); W = m['W']; Pw = (np.c_[Pos, np.ones(len(Pos))] @ W.T)[:, :3].astype('f4'); n = len(Pw)
        if ('NORMAL', 0) in Sx and Sx[('NORMAL', 0)][0].shape[1] == 3: Nw = Sx[('NORMAL', 0)][0] @ W[:3, :3].T; Nw = (Nw / (np.linalg.norm(Nw, axis=1, keepdims=True) + 1e-12)).astype('f4')
        else: Nw = vnormals(Pw, I)
        uv0 = Sx.get(('TEXCOORD', tp.get('base', {}).get('uvset', 0)), Sx.get(('TEXCOORD', 0), (None,)))[0]
        if uv0 is None: uv0 = np.zeros((n, 2), 'f4')
        tt = tp.get('base', {}).get('tt')
        if tt is not None: c = np.array(tt[6:8]); uv0 = ((uv0 - c) * np.array(tt[2:4]) + c + np.array(tt[0:2])).astype('f4')
        al = P.get('NiAlphaProperty'); mat = P.get('NiMaterialProperty', dict(dif=(1, 1, 1), emi=(0, 0, 0), alpha=1.0)); flags = 0; col = [1.0, 1.0, 1.0, 1.0]
        if al and al['blend']: flags |= MF_ADD if al['dst'] == 0 else MF_ALPHA; col[3] = mat['alpha']
        elif al and al['test']: flags |= MF_ATEST
        if P.get('NiStencilProperty', {}).get('draw') != 3: flags |= MF_CULL   # (back faces are not drawn: the camera looks into the room through its walls)
        tex = R.tex(F.find(base), lambda: load_rgba(F.find(base))) if base and F.find(base) else -1
        if tex < 0: col[:3] = [min(1.0, mat['dif'][i] * 0.5 + mat['emi'][i]) for i in range(3)]
        else: col[:3] = [float(mat['dif'][i]) for i in range(3)]   # (b120: the material's colour - the bed's dark lettering is a grey texture x (0.13, 0.11, 0.11))
        lm = -1; uv1 = None
        for sl in ('detail', 'dark'):
            e = tp.get(sl)
            if e and e['file'] and 'lightmapping' in e['file'].lower():
                p = F.find(e['file'])
                if p and ('TEXCOORD', e['uvset']) in Sx: uv1 = Sx[('TEXCOORD', e['uvset'])][0]; lm = R.tex(p, lambda: load_rgba(p, 2048))
                break
        g = tp.get('glow', {}).get('file'); glow = R.tex(F.find(g), lambda: load_rgba(F.find(g))) if g and F.find(g) else -1
        if lm < 0: flags |= MF_UNLIT; uv1 = None
        else: col[:3] = [c * 2.0 for c in col[:3]]   # (a Beast lightmap of 0.5 is full light)
        # b120: a lightmapped, solid mesh with a normal map is drawn as the PC's CustomMaterial does (shader cache, the variant with base x detail:
        # colour = base x lightmap x 2 x material diffuse x relief + gloss x (highlight x material specular [+ cube])), relief = 1 - | |N.L| - |n.L| |
        # (g_Normal_Value / g_Normal_Falloff 1), highlight = (n.h)^gloss power where the mesh has an NiSpecularProperty; L = the room's sun
        kw = {}; nf = F.find(tp.get('normal', {}).get('file')); un = lambda V: (V / (np.linalg.norm(V, axis=1, keepdims=True) + 1e-12)).astype('f4')
        if lm >= 0 and nf and not (flags & (MF_ADD | MF_ALPHA)) and ('TANGENT', 0) in Sx and ('BINORMAL', 0) in Sx and Sx[('TANGENT', 0)][0].shape[1] == 3 and ROOM_RELIEF:
            Pm = np.zeros(NP, 'f4'); Pm[P_DIFV] = 1.0; Pm[P_BLPOW] = 1.0; Pm[P_BLEXP] = 1.0; Pm[P_MPOW] = float(mat.get('gloss', 10.0)); Pm[P_GLOWV] = 1.0
            if 'NiSpecularProperty' in P: Pm[P_MSPEC:P_MSPEC + 3] = np.array(mat.get('spec', (0, 0, 0)), float) * ROOM_SPEC
            gf = F.find(tp.get('gloss', {}).get('file')); R3 = W[:3, :3].T; ef = F.find(P.get('_env')) if ROOM_ENV else None   # (the node's environment picture: re_cyborg.tga on the android shells)
            kw = dict(kind=4, nmap=R.tex(nf, lambda: load_rgba(nf)), cmap=R.tex(ef, lambda: load_rgba(ef)) if ef else -1, P=Pm, tan=un(Sx[('TANGENT', 0)][0] @ R3), bin=un(Sx[('BINORMAL', 0)][0] @ R3))
            mid = R.mesh(uv0[:n], I, flags, tex, lm, glow, R.tex(gf, lambda: load_rgba(gf)) if gf else -1, -1, col, 1.0, 0.0, uv1=uv1[:n], pos=Pw, nrm=Nw, **kw)
        else: mid = R.mesh(uv0[:n], I, flags, tex, lm, glow, -1, -1, col, 1.0, 0.0, uv1=uv1[:n] if uv1 is not None else None, pos=Pw, nrm=Nw)
        out.append(dict(mesh=mid, path=m['path'], blend=bool(flags & (MF_ADD | MF_ALPHA)), cen=Pw.mean(0)))
    return out

# ---------------------------------------------------------------- a keyed NIF: actors, cards, flares, the effect files
ID6 = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0)
# world -> the environment cube's frame: the matrix every NiTextureEffect of the character file carries (y and z change places: the cube is painted
# y up). With it the two light spots painted into eye_CUBE.dds fall on Motoko's eye where the PC's picture has them (tried: the mesh's own frame, the other axis orders)
ENVT = np.array([[1, 0, 0], [0, 0, 1], [0, 1, 0]], float)
if os.environ.get('INTRO_ENVT'): ENVT = np.array([float(x) for x in os.environ['INTRO_ENVT'].split(',')]).reshape(3, 3)   # (test)
def FLICK(t):
    """0 .. 1, fixed for a time: mostly late in the loop (the PC's picture shows the body faint in most frames, bright in few)"""
    h = math.sin(t * 91.17 + 1.3) * 43758.5453; h -= math.floor(h); return 1.0 - h * h
def inherited_props(N, k):
    import map_visual as MV
    chain = []; q = k; P = {}
    while q is not None: chain.append(q); q = N.par.get(q)
    for q in chain[::-1]:
        for p in N.B[q].get('props', []):
            t, v = MV._prop(N.h, p); P[t] = v; P['#' + t] = p
    return P
class SMesh:
    pass
def scene_meshes(R, F, N):
    """every drawn mesh of a keyed NIF (the character file or an effect file), registered with the rasteriser"""
    out = []; ENV = N.env_cubes()
    for k in N.meshes():
        path = N.path_of(k); pl = '/'.join(path)
        if 'NDLCD' in pl or 'UmbraAlpha' in pl or 'Biped Object' in pl: continue
        if os.environ.get('INTRO_SKIP') and any(x in pl for x in os.environ['INTRO_SKIP'].split(',')): continue   # (test)
        g = N.geom(k); P = inherited_props(N, k); tp = P.get('NiTexturingProperty', {}); sk = N.skin(k); a = SMesh(); a.k = k; a.path = path; a.name = pl; a.N = N
        a.group = path[2] if len(path) > 2 and path[1] == 'PD_01' else path[1] if len(path) > 1 else path[0]
        n = len(g['P']); uv = g['UV'] if g['UV'] is not None else np.zeros((n, 2), 'f4')
        if sk and g.get('PAL'):   # several vertex regions = several bone palettes over one vertex buffer
            palr, pregs = g['PAL']; BI = np.zeros((n, 4), int); I = []
            for (vs, vc), (ps, pc), (is_, ic) in zip(g['regs'], pregs, g['iregs']):
                BI[vs:vs + vc] = palr[ps + g['BI'][vs:vs + vc]]; I.append(g['I'][is_:is_ + ic] + vs)
            a.I = np.concatenate(I); a.BI = BI; a.BW = g['BW'].astype('f8'); a.bones = sk['bones']; a.s2b = np.array(sk['s2b'])
        elif sk: a.I = g['I']; a.BI = g['BI']; a.BW = g['BW'].astype('f8'); a.bones = sk['bones']; a.s2b = np.array(sk['s2b'])
        else: a.I = g['I']; a.bones = None
        a.P = g['P'].astype('f8'); a.N0 = g['N'].astype('f8') if g['N'] is not None else vnormals(g['P'], a.I).astype('f8')
        a.T0 = g['T'].astype('f8') if g.get('T') is not None and g.get('B') is not None else None; a.B0 = g['B'].astype('f8') if a.T0 is not None else None
        al = P.get('NiAlphaProperty'); mat = P.get('NiMaterialProperty', dict(dif=(1, 1, 1), emi=(0, 0, 0), alpha=1.0)); flags = 0
        if al and al['blend']: flags |= MF_ADD if al['dst'] == 0 else MF_ALPHA
        elif al and al['test']: flags |= MF_ATEST
        def T(slot, maxs=2048):
            f = tp.get(slot, {}).get('file'); p_ = F.find(f)
            return R.tex(p_, lambda: load_rgba(p_, maxs)) if p_ else -1
        tex = T('base'); glow = T('glow'); spec = T('gloss'); dark = T('dark'); a.base = tp.get('base', {}).get('file')
        a.card = sk is None or n <= 8   # planes: background cards, flares, the blinds, the eyes' glow, the effect files
        a.matprop = P.get('#NiMaterialProperty'); a.texprop = P.get('#NiTexturingProperty'); a.additive = bool(flags & MF_ADD); a.blend = bool(flags & (MF_ADD | MF_ALPHA)); a.notex = tex < 0
        a.emi0 = np.array(mat['emi'], float); a.alpha0 = float(mat['alpha'])
        # what moves on the material / the texturing property
        a.matanim = a.matprop is not None and any(True for _ in N.chain(N.B[a.matprop].get('ctrl')))
        a.uvs = [None, None]; a.uvanim = [False, False]; a.glowanim = False; a.flip = None; uv1 = None; a.missing = bool(a.base) and tex < 0
        # a one-frame loop on the material (the hologram panel's body picture: alpha 1 -> 0 in 1 / 30 s, looping) is a flicker: what a frame
        # shows depends on where in the loop its time falls, on the PC as it comes; here a fixed pseudo-random place per frame (FLICK)
        mc = [cb for c, cb, ct in N.chain(N.B[a.matprop].get('ctrl'))] if a.matprop is not None else []
        a.flick = max(cb['stop'] - cb['start'] for cb in mc) if mc and all(((cb['flags'] >> 1) & 3) == 0 and 0 < cb['stop'] - cb['start'] <= 1.0 / FPS + 1e-4 for cb in mc) else 0.0
        if a.missing: print('  picture %s not in the install: %s is not drawn' % (a.base, pl))
        if not a.card and not a.base: a.missing = True   # (a skinned mesh without any texture - Batou's 'HG' - is a helper: the PC's picture does not show it)
        if a.texprop is not None:
            td = N.texdesc(a.texprop)
            for sl in (0, 1):
                if sl < len(td) and td[sl]:
                    a.uvs[sl] = td[sl][2]
                    if not td[sl][1]: flags |= (MF_CLAMPA, MF_CLAMPB)[sl]
            ds = tp.get('dark', {}).get('uvset', 0)
            if dark >= 0 and ds and g.get('UV%d' % ds) is not None: uv1 = g['UV%d' % ds]; flags |= MF_DARKUV1
            for c, cb, ct in N.chain(N.B[a.texprop].get('ctrl')):
                if ct == 'NiTextureTransformController' and cb.get('slot') in (0, 1): a.uvanim[cb['slot']] = True
                elif ct == 'NiTextureTransformController' and cb.get('slot') == 4: a.glowanim = True   # (the scan lines sliding over the hologram panel)
                elif ct == 'NiFlipController' and cb.get('slot', 0) == 0:
                    ids = []
                    for sref in cb['sources']:
                        f = N.B[sref].get('file') if sref not in (None, NONE) else None; p_ = F.find(f); ids.append(R.tex(p_, (lambda p_=p_: load_rgba(p_))) if p_ else -1)
                    a.flip = (cb, ids)
        if a.card: flags |= MF_UNLIT
        elif P.get('NiStencilProperty', {}).get('draw') != 3: flags |= MF_CULL
        else: flags |= MF_NOFLIP   # (drawn from both sides and lit by its own normals on both: the inside of Batou's hair, seen through its see-through fringe, is as light as the outside - the PC's picture has no dark patches there)   # (see-through meshes too: the back faces of Batou's hair cards are dark blocks on his head otherwise)
        # b120: the actors' own materials. WeaponMaterial (a 'shader' map beside the colour map) and WGStandardMaterial (the android shell) are
        # drawn with the PC's own arithmetic (introrender.c), from the numbers the mesh carries
        a.kind = 0; kw = {}
        if not a.card and a.texprop is not None and a.T0 is not None and not a.missing:
            ts = N.texslots(a.texprop); X = N.extras(k); fx = lambda nm, dv: float(X[nm]) if isinstance(X.get(nm), (int, float)) else dv
            def T2(slot):
                p_ = F.find(ts.get(slot, {}).get('file'))
                return R.tex(p_, lambda: load_rgba(p_)) if p_ else -1
            cb = None; q_ = k
            while q_ is not None and cb is None: cb = F.find(ENV.get(q_)); q_ = N.par.get(q_)   # (the environment cube of the nearest node that names one)
            if 'normal' in ts and ('shader0' in ts or 'gloss' in ts or cb):
                a.kind = 1 if 'shader0' in ts else 2 if 'gloss' in ts else 3; Pm = np.zeros(NP, 'f4'); sv = fx('Specular_Value', 1.0) if 'NiSpecularProperty' in P else 0.0
                Pm[P_DIFV] = fx('Diffuse_Value', 1.0); Pm[P_AO] = fx('g_AOPower', 0.0); Pm[P_HLP] = fx('g_HalfLambertPower', 1.0); Pm[P_LSMIN] = fx('g_LightSpectrumMin', 0.0); Pm[P_LSOVER] = fx('g_LightSpectrumOverflow', 0.0)
                Pm[P_BLPOW] = fx('BackLight_Normal_Power', 0.0); Pm[P_BLEXP] = fx('BackLight_Normal_Exp', 1.0); Pm[P_FRFALL] = fx('Fresnel_Falloff', 1.0); Pm[P_FRVAL] = fx('Fresnel_Value', 0.0)
                Pm[P_FRC:P_FRC + 3] = X['Fresnel_Color'][:3] if isinstance(X.get('Fresnel_Color'), tuple) else (1, 1, 1); Pm[P_IBL] = ACTOR['ibl']; Pm[P_CLOD] = fx('g_CubeMapLodLevelCount', 0.0); Pm[P_CBRIGHT] = fx('g_CubeMapBrightness', 1.0)
                Pm[P_RFALL] = fx('Reflact_Falloff', 1.0); Pm[P_RVAL] = fx('Reflact_Value', 0.0); Pm[P_RIMEXP] = fx('g_RimExp', 0.0); Pm[P_RIMPOW] = fx('g_RimPower', 0.0)
                Pm[P_MDIF] = float(np.mean(mat['dif'])); Pm[P_MSPEC:P_MSPEC + 3] = np.array(mat['spec']) * sv; Pm[P_MAMB:P_MAMB + 3] = mat['amb']; Pm[P_MEMI:P_MEMI + 3] = mat['emi']; Pm[P_GLOWV] = fx('Glow_Value', 1.0); Pm[P_MPOW] = float(mat['gloss'])
                kw = dict(kind=a.kind, nmap=T2('normal'), cmap=T2('shader0') if a.kind == 1 else -1, cube=R.cube(cb, lambda: cube_faces(cb)) if cb else -1, P=Pm)
        k_ = HAIR if (not a.card and not a.kind and 'hair' in pl.lower()) else 1.0   # (the PC's hair comes out darker than this light makes it: measured on both heads)
        a.mesh = R.mesh(uv, a.I, flags, tex, -1, glow, spec, dark, (k_, k_, k_, 1), 1.0, SPEC if spec >= 0 else 0.0, uv1=uv1, **kw)
        out.append(a)
    return out
def skin_positions(a, Wb):
    """Wb: {bone block: 4x4 world} -> world positions, normals, tangents, binormals (the last two None without a normal map's frame)"""
    M = np.array([Wb[b] @ s for b, s in zip(a.bones, a.s2b)])   # (nb, 4, 4)
    n = len(a.P); Mv = np.zeros((n, 3, 4))   # every vertex's own blended matrix
    for j in range(a.BI.shape[1]):
        w = a.BW[:, j]
        if (w > 0).any(): Mv += w[:, None, None] * M[a.BI[:, j]][:, :3, :]
    P = np.einsum('nij,nj->ni', Mv, np.c_[a.P, np.ones(n)]); R3 = Mv[:, :, :3]; un = lambda V: (V / (np.linalg.norm(V, axis=1, keepdims=True) + 1e-12)).astype('f4')
    Nn = un(np.einsum('nij,nj->ni', R3, a.N0))
    if a.T0 is None: return P.astype('f4'), Nn, None, None
    return P.astype('f4'), Nn, un(np.einsum('nij,nj->ni', R3, a.T0)), un(np.einsum('nij,nj->ni', R3, a.B0))
def scene_item(a, B, j, t, hide=()):
    """one mesh at frame j of the baked times (time t) -> (blended?, depth point, item) or None when it is not shown"""
    N = a.N
    if a.missing or a.group in hide or not B.vis(a.k)[j]: return None
    Tn = Bn = None
    if a.bones is not None: P, Nn, Tn, Bn = skin_positions(a, {b: B.world(b)[j] for b in a.bones})
    else:
        Wm = B.world(a.k)[j]; P = (np.c_[a.P, np.ones(len(a.P))] @ Wm.T)[:, :3].astype('f4'); Nn = (a.N0 @ Wm[:3, :3].T).astype('f4')
        if a.kind: Tn = (a.T0 @ Wm[:3, :3].T).astype('f4'); Bn = (a.B0 @ Wm[:3, :3].T).astype('f4')
    col = (1.0, 1.0, 1.0, 1.0)
    if a.card:
        emi, alp = N.material(a.k, FLICK(t) * a.flick if a.flick else t) if a.matanim else (a.emi0, a.alpha0)
        if alp <= 0.002 and a.blend: return None
        col = (float(emi[0]) * CARD, float(emi[1]) * CARD, float(emi[2]) * CARD, float(alp))   # (a card is lit by its material's own light alone: the painted backgrounds have 0.39, and measure so against the PC's picture)
    ua = ID6; ub = ID6; uc = None; tex = -2
    if a.texprop is not None:
        if a.glowanim: uc = tuple(float(x) for x in N.uvx(a.texprop, 4, t, None)[0])
        if a.uvs[0] is not None or a.uvanim[0]: ua = tuple(float(x) for x in N.uvx(a.texprop, 0, t, a.uvs[0])[0])
        if a.uvs[1] is not None or a.uvanim[1]: ub = tuple(float(x) for x in N.uvx(a.texprop, 1, t, a.uvs[1])[0])
        if a.flip:
            cb, ids = a.flip; ib = N.B[cb['interp']] if cb.get('interp') not in (None, NONE) else None; d = N.B[ib['data']] if ib and ib.get('data') not in (None, NONE) else None
            ct_ = ctime(cb, t); v = keyval(d, ct_) if d is not None and d.get('keys') else (ct_ - cb['start']) / max(1e-9, cb['stop'] - cb['start']) * len(ids)
            tex = ids[int(min(len(ids) - 1, max(0, np.floor(v + 1e-4))))]
            if tex < 0: return None
    cen = P.mean(0)
    return a.blend, cen, (a.mesh, col, ua, ub, tex, P, Nn, Tn, Bn, PROBES.at(cen) if a.kind == 1 and PROBES else None, uc, ENVT if a.kind else None)

# ---------------------------------------------------------------- finishing a picture
class Post:
    """a drawn picture -> the PC's picture, in the PC's order (shaders\\FX\\PostHdrScene.fxo, PostEffects.fxo):
      exposure  HDRScene_FinalScenePass: scene x clamp(middle grey / adapted light, min, final); the room's World.ini [NIHDR] has 0.8 and 1.0
                there. MEASURED (EXPO): 0.774, on the painted background cards of a recording of the PC's intro - the same pixels, drawn here
                without any post-processing, against the PC's finished ones (four shots)
      bloom     DownScaleThresholdPass / GaussianBlur ([NINEWBLOOM] 0.7 1.0): colour x saturate(max(r, g, b) - 0.7), blurred, added
      grading   PostColorGradingPass with map01\\intromovie_colorgrading.tga (256 x 16 = 16 blue slices of 16 x 16 red x green), read as
                that shader reads it: between two blue slices by 16 b - slice (not 15 b - slice: blue runs 1 / 15 ahead, past the last slice)
      gamma     MEASURED on the same cards (GAMMA): the PC's finished picture is the graded one ^ (0.78, 0.82, 0.88) - black stays black, the
                darker half is lifted (the PC's brightness setting / PostTonemapPass; no file states the numbers). Without it the film is
                harder in contrast than the PC's: the cards' mean error 7.5 of 255 without, 2.2 with.
    No focus blur: the shots' DOF_CONTROL blocks have FAR_BLUR 0 and the PC's picture shows none (the soft backgrounds are painted so)."""
    EXPO = float(os.environ.get('INTRO_EXPO', '0.774')); GAMMA = tuple(float(x) for x in os.environ.get('INTRO_GAMMA', '0.782,0.823,0.879').split(','))
    def __init__(s, lutpath, bloom=1.0, bloom_thr=0.7):
        import cv2; s.cv = cv2; s.bloom = bloom; s.thr = bloom_thr; s.lut = None
        if lutpath and os.path.exists(lutpath):
            a = np.asarray(Image.open(lutpath).convert('RGB'), np.float32) / 255
            if a.shape == (16, 256, 3):   # [green][blue slice * 16 + red] -> [blue][green][red], spread to 256 steps a channel once: a picture is then one look-up
                L = a.reshape(16, 16, 16, 3).transpose(1, 0, 2, 3); x = np.arange(256, dtype=np.float32) * (15.0 / 255.0); i0 = np.minimum(x.astype(np.int32), 14); f = (x - i0)[:, None]
                L = L[:, :, i0] * (1 - f) + L[:, :, i0 + 1] * f                                  # red   -> (16, 16, 256, 3)
                L = L[:, i0] * (1 - f[:, None]) + L[:, i0 + 1] * f[:, None]                      # green -> (16, 256, 256, 3)
                s.lut = np.empty((256, 256, 256, 3), np.uint8); g = np.array(s.GAMMA, np.float32)
                for b in range(256):
                    v = b / 255.0; sl = min(14, int(v * 14.9999)); t = v * 16.0 - sl
                    s.lut[b] = (np.power(np.clip(L[sl] + (L[sl + 1] - L[sl]) * t, 0, 1), g) * 255 + 0.5).astype(np.uint8)
    def glow(s, rgb):
        cv = s.cv; w = np.clip(rgb.max(2, keepdims=True) - s.thr, 0, 1); sc = rgb.shape[1] / 960.0
        if not (w > 0).any(): return rgb
        return rgb + cv.GaussianBlur(np.minimum(rgb, 4.0) * w, (0, 0), 8 * sc) * s.bloom
    def grade(s, rgb):
        """-> the finished picture, uint8"""
        v = (np.clip(rgb, 0, 1) * 255 + 0.5).astype(np.uint8)
        return v if s.lut is None else s.lut[v[..., 2], v[..., 1], v[..., 0]]
    def finish(s, rgb):
        return s.grade(s.glow(rgb * s.EXPO))

# The stand-in light (b119) - since b120 only for a skinned mesh that lacks the PC material's maps (none in the user's files):
# a key light from the upper left of the camera, the room's teal as the fill, a cyan rim, set by eye.
HAIR = float(os.environ.get('INTRO_HAIR', '0.76'))
SPEC = float(os.environ.get('INTRO_SPEC', '0.6'))   # the highlight's strength (times the gloss map)
CARD = float(os.environ.get('INTRO_CARD', '1.0'))   # (test: a factor on the cards' own light)
LIGHT = dict(amb=(0.12, 0.20, 0.22), sky=(0.75, 0.90, 0.85), key=(0.35, 0.38, 0.33), key_dir=(-0.45, 0.70, 0.60), rim=(0.10, 0.26, 0.30, 3.0))
# per shot (the PC's light differs from shot to shot with where the actors stand; one light for all cannot match both the
# profile of shot 3 and the faces of shots 8 and 9): the key light's strength and its direction from the camera (right, up, toward it)
LIGHT_SHOT = {6: dict(key=(0.43, 0.47, 0.40), key_dir=(-0.35, 0.45, 0.82)), 8: dict(key=(1.15, 1.25, 1.10), key_dir=(-0.10, 0.45, 0.89)), 9: dict(key=(0.62, 0.67, 0.57), key_dir=(-0.40, 0.35, 0.85))}
for k_ in ('amb', 'sky', 'key', 'key_dir', 'rim'):
    if os.environ.get('INTRO_' + k_.upper()): LIGHT[k_] = tuple(float(x) for x in os.environ['INTRO_' + k_.upper()].split(','))
if os.environ.get('INTRO_SHOT'):   # (test) "shot:r,g,b:x,y,z;..."
    for e_ in os.environ['INTRO_SHOT'].split(';'):
        i_, c_, d_ = e_.split(':'); LIGHT_SHOT[int(i_)] = dict(key=tuple(float(x) for x in c_.split(',')), key_dir=tuple(float(x) for x in d_.split(',')))
# b120: THE ACTORS' LIGHT AS THE PC HAS IT. The actors' meshes carry the PC's material (WeaponMaterial; the android shell
# WGStandardMaterial) and all its numbers; the arithmetic is the shaders' own (introrender.c). The light:
#   dir   the direction the light travels (world; z up): the room file's SUN light. MEASURED: of 36 directions round the sphere the
#         PC's picture fits best between two neighbours of the SUN's own (the room's other lights and the camera-fixed lights do not fit)
#   dif / spec / amb   light.ini [ACTOR] diffuse 247 232 233, specular 255 252 208, ambient 10 10 10, colours x dimmer 1.22
#   ibl   g_IBL_LightBlendingValue, the share of (light x probe) against the plain light: LightProbeCloud.lpf's first line, 0.5
# With these (nothing tuned) the actors' pixels differ from the PC's by 7 of 255 on average over six shots (b119's film by the same measure: 34).
# The values below are the user's install's, used when a file cannot be read; actor_light() and Probes read the install's own.
ACTOR = dict(dir=(-0.813, -0.313, -0.491), dif=(247 / 255 * 1.22, 232 / 255 * 1.22, 233 / 255 * 1.22), spec=(255 / 255 * 1.22, 252 / 255 * 1.22, 208 / 255 * 1.22), amb=(10 / 255, 10 / 255, 10 / 255), ibl=0.5)
ACTOR_SHOT = {}
for k_ in ('dir', 'dif', 'spec', 'amb'):
    if os.environ.get('INTRO_A' + k_.upper()): ACTOR[k_] = tuple(float(x) for x in os.environ['INTRO_A' + k_.upper()].split(','))
if os.environ.get('INTRO_IBL'): ACTOR['ibl'] = float(os.environ['INTRO_IBL'])
PROBES = None
class Probes:
    """map01\\LightProbeCloud.lpf (text): a blend value, the count, then per probe its place and nine RGB coefficients of the light
    around it (spherical harmonics, the order the PC's shader reads them in). The light at a point: the four nearest probes by 1 / distance^2."""
    def __init__(s, path):
        L = [l for l in open(path, encoding='latin1').read().split('\n') if l.strip()]; n = int(L[1]); s.blend = float(L[0])
        v = np.array([[float(x) for x in l.split(',')] for l in L[2:2 + n * 10]], 'f8').reshape(n, 10, 3); s.pos = v[:, 0]; s.sh = v[:, 1:].reshape(n, 27)
    def at(s, p):
        d2 = ((s.pos - np.asarray(p, 'f8')) ** 2).sum(1); i = np.argsort(d2)[:4]; w = 1.0 / np.maximum(d2[i], 1.0); w /= w.sum()
        return (s.sh[i] * w[:, None]).sum(0).astype('f4')
def actor_light(mapdir):
    """ACTOR's colours from the room's light.ini [ACTOR] (x its dimmer) and its direction from the room file's SUN light, where the install has them"""
    try:
        sec = None; a = {}
        for ln in open(os.path.join(mapdir, 'light.ini'), encoding='latin1'):
            ln = ln.strip()
            if ln.startswith('['): sec = ln.strip('[]').upper()
            elif '=' in ln and sec == 'ACTOR': k, v = ln.split('=', 1); a[k.strip().lower()] = v.strip()
        dm = float(a['actor_dimmer']); rgb = lambda k: tuple(int(x) / 255.0 * dm for x in a['actor_' + k].split('.'))
        for k, e in (('dif', 'diffuse'), ('spec', 'specular')):
            if not os.environ.get('INTRO_A' + k.upper()): ACTOR[k] = rgb(e)
        if not os.environ.get('INTRO_AAMB'): ACTOR['amb'] = tuple(int(x) / 255.0 for x in a['actor_ambient'].split('.'))
    except Exception as e: print('  intro: light.ini not read (%s): the built-in actor light' % e)
    try:
        if not os.environ.get('INTRO_ADIR'):
            N = Nif(os.path.join(mapdir, 'world_lmap.nif')); d = N.d
            for k, t in enumerate(N.T):
                p = N.par.get(k)
                if t != 'NiDirectionalLight' or p is None or not str(N.name(p) or '').upper().startswith('SUN:'): continue
                o = N.h['blocks'][k][1]; ne = struct.unpack_from('<I', d, o + 4)[0]; q = o + 8 + 4 * ne + 4 + 2 + 12   # (name, extra data, controller, flags, translation)
                R = np.array(N.B[p]['r'], float).reshape(3, 3) @ np.array(struct.unpack_from('<9f', d, q)).reshape(3, 3)
                ACTOR['dir'] = tuple(float(x) for x in R[:, 0]); break   # (a light shines along its own x axis)
    except Exception as e: print('  intro: the room\'s sun not read (%s): the built-in direction' % e)
def cube_faces(path):
    sys.path.insert(0, os.path.join(H, '..', '..', 'vm')); import make_env
    return make_env.cube_faces(path)
# ---------------------------------------------------------------- the light
def setup(root, w, h, ss):
    exe = os.environ.get('GITSFA_EXE_INTRORENDER') or os.environ.get('GITSFA_INTRORENDER') or os.path.join(H, '..', 'databuilder', 'ctools', 'introrender' + ('.exe' if os.name == 'nt' else ''))
    M = movie(os.path.join(root, 'MovieMaker.txt')); F = Files(os.path.join(root, 'model')); shots = []; t0 = 0.0
    # b120: the film's frame f shows the PC's scene at the time f / 30 - a shot starts at the sum of the shots before it (2.85 s and
    # 2.72 s long shots: not on a frame), and its first frame is already that much into it, as on the PC
    first = lambda t: int(math.ceil(t * FPS - 1e-6))
    for i, sc in enumerate(M['scenes']):
        dur = sc['end'] - sc['begin']; shots.append(dict(i=i, sc=sc, n=first(t0 + dur) - first(t0), f0=first(t0), t0=t0)); t0 += dur
    return exe, M, F, shots, first(t0)

def render(root, frames, emit, w=960, h=544, ss=2, post=True, quiet=False, raw=None):
    """draws the frames (a sorted list of film frame numbers) and hands each finished picture (uint8 h x w x 3) to emit(frame, picture)"""
    exe, M, F, shots, total = setup(root, w, h, ss); want = set(frames)
    R = Raster(exe, w, h, ss); global PROBES
    lp = os.path.join(root, 'model', 'map', M['level'], 'LightProbeCloud.lpf'); PROBES = Probes(lp) if os.path.exists(lp) else None
    if PROBES and not os.environ.get('INTRO_IBL'): ACTOR['ibl'] = PROBES.blend   # (the file's first line: 0.5)
    actor_light(os.path.join(root, 'model', 'map', M['level']))
    room = room_items(R, F, os.path.join(root, 'model', 'map', M['level'], 'world_lmap.nif'))
    N = Nif(os.path.join(root, 'model', 'character', M['char'])); acts = scene_meshes(R, F, N)
    if not quiet: print('  intro: room %d meshes, character file %d meshes (%d cards), %d frames to draw' % (len(room), len(acts), sum(a.card for a in acts), len(want))); sys.stdout.flush()
    PP = Post(F.find('intromovie_colorgrading.tga')) if post else None
    RI = lambda r: (r['mesh'], (1, 1, 1, 1), ID6, ID6, -2, None, None, None, None, None, None, None)
    fade0 = total - int(round(FADE_OUT * FPS))
    for sh in shots:
        fr = [f for f in range(sh['f0'], sh['f0'] + sh['n']) if f in want]
        if not fr: continue
        sc = sh['sc']; lt = np.maximum(np.array(fr) / FPS - sh['t0'], 0.0); tt = sc['begin'] + lt; B = Baked(N, tt); hide = set(sc['hide'])
        trk, tx, ty = camera_track(os.path.join(root, 'model', 'character', sc['cam']), lt)
        ritems = [r for r in room if not any(('/' + nm + '/') in r['path'] + '/' for nm in hide)] if sc['map'] else []
        eff = None; LS = dict(LIGHT); LS.update(LIGHT_SHOT.get(sh['i'], {})); AS = dict(ACTOR); AS.update(ACTOR_SHOT.get(sh['i'], {}))
        if sc['eff']:   # the shot's effect file runs on the shot's own clock
            EN = Nif(os.path.join(root, 'model', 'effect', sc['eff'])); eff = (scene_meshes(R, F, EN), Baked(EN, lt))
        for j, f in enumerate(fr):
            cam = trk[j]; eye, dr, up = cam[0:3], cam[3:6], cam[6:9]; rt = np.cross(dr, up)
            op = [(float((r['cen'] - eye) @ dr), RI(r)) for r in ritems if not r['blend']]; bl = [(float((r['cen'] - eye) @ dr), RI(r)) for r in ritems if r['blend']]
            for a_, B_, t_, hd in [(a, B, tt[j], hide) for a in acts] + ([(a, eff[1], lt[j], ()) for a in eff[0]] if eff else []):
                it = scene_item(a_, B_, j, t_, hd)
                if it is None: continue
                if it[0]: bl.append((float((it[1] - eye) @ dr), it[2]))
                else: op.append((float((it[1] - eye) @ dr), it[2]))
            op.sort(key=lambda x: x[0]); bl.sort(key=lambda x: -x[0])   # solid things near to far (what is hidden is not shaded), see-through ones far to near
            rgb, dep = R.frame(eye, dr, up, tx, ty, [o[1] for o in op] + [b[1] for b in bl], amb=LIGHT['amb'], sky=LIGHT['sky'], lights=[(tuple(-dr * LS['key_dir'][2] + up * LS['key_dir'][1] + rt * LS['key_dir'][0]), LS['key'])], rim=LIGHT['rim'], actor=(AS['dir'], AS['dif'], AS['spec'], AS['amb']))
            if raw: raw(f, rgb, dep)   # (development: the picture before the post-processing, and its depth)
            if PP: img = PP.finish(rgb)
            else: img = (np.clip(rgb, 0, 1) * 255 + 0.5).astype('u1')
            if f >= fade0: img = (img.astype(np.float32) * max(0.0, (total - 1 - f) / max(1, total - 1 - fade0)) + 0.5).astype('u1')
            emit(f, img)
        if not quiet: print('  intro: shot %d of %d drawn (%d frames)' % (sh['i'] + 1, len(shots), len(fr))); sys.stdout.flush()
    R.close()

# ---------------------------------------------------------------- the sound
# The PC starts the music with the scene and three voice / effect tracks with shots 0, 3 and 5 (MovieMaker.txt SOUND_BG,
# SOUND_SCENE). Levels and start delays are measured on a recording of the PC's intro (power spectra, least squares:
# music 0.71, the three tracks 0.81 +- 0.01; envelope correlation for the delays).
SND_GAIN = {'bgm': 0.71, 'voice': 0.81}; SND_DELAY = {'bgm_tutorial_intro_cut_scene': 0.32, 'intro_cut_scene_01': 0.08, 'intro_cut_scene_02': 0.38, 'intro_cut_scene_03': 0.09}
def mix_sound(root, fmod_dir, wav_path):
    import wave
    sys.path.insert(0, os.path.join(H, '..', 'audio')); from pcmsrc import Banks
    exe, M, F, shots, total = setup(root, 1, 1, 1); rate = 44100; n = int(total / FPS * rate); mix = np.zeros((n, 2), np.float32); B = Banks(fmod_dir)
    want = {2: [('bgm_' + M['bgm'], 0.0, SND_GAIN['bgm'])], 6: []}
    for sh in shots:
        snd = sh['sc']['sound']
        if snd: want[6].append((os.path.basename(snd[1]), sh['t0'], SND_GAIN['voice']))
    for bank, lst in want.items():
        names = [x[0] for x in lst]; meta = B.meta(bank)
        for nm in names:
            if nm not in meta: raise SystemExit('intro: sound %s is not in the install\'s bank %d' % (nm, bank))
        for nm, pcm in B.iter_pcm(bank, names):
            st, g = [(x[1], x[2]) for x in lst if x[0] == nm][0]; r, ch, _ = meta[nm]; x = pcm.astype(np.float32) / 32768
            if x.ndim == 1 or x.shape[1] == 1: x = np.repeat(x.reshape(-1, 1), 2, 1)
            if r != rate: k = int(len(x) * rate / r); x = np.stack([np.interp(np.arange(k) * r / rate, np.arange(len(x)), x[:, c]) for c in (0, 1)], 1).astype(np.float32)
            o = int(round((st + SND_DELAY.get(nm, 0.1)) * rate)); e = min(n, o + len(x))
            if e > o: mix[o:e] += x[:e - o] * g
    k = int(FADE_OUT_SOUND * rate); mix[n - k:] *= np.linspace(1, 0, k, dtype=np.float32)[:, None]
    pk = float(np.abs(mix).max())
    if pk > 0.98: mix *= 0.98 / pk
    w = wave.open(wav_path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate); w.writeframes((mix * 32767).astype('<i2').tobytes()); w.close()
    return pk

# ---------------------------------------------------------------- the film
FADE_OUT = 0.4; FADE_OUT_SOUND = 0.6   # (seconds of fade at the end, as the recorded intro's cut has)
def main():
    args = sys.argv[1:]
    if len(args) < 2: print(__doc__); sys.exit(2)
    root, out = args[0], args[1]; opt = dict(zip(args[2::2], args[3::2]))
    w, h = (int(x) for x in opt.get('--size', '960x544').split('x')); ss = int(opt.get('--ss', '2')); post = opt.get('--post', '1') != '0'
    exe, M, F, shots, total = setup(root, w, h, ss)
    if '--raw' in opt:   # a worker: frames a:b as raw RGB into one file
        a, b = (int(x) for x in opt['--frames'].split(':'))
        with open(opt['--raw'], 'wb') as f: render(root, list(range(a, b)), lambda fn, img: f.write(img.tobytes()), w, h, ss, post, quiet=True)
        return
    if not out.lower().endswith('.mp4'):   # development: pictures (and a contact sheet) into a folder
        os.makedirs(out, exist_ok=True); every = int(opt.get('--every', '1')); fa, fb_ = (int(x) for x in opt.get('--frames', '0:%d' % total).split(':'))
        fr = sorted(int(x) for x in opt['--list'].split(',')) if '--list' in opt else list(range(fa, fb_, every)); sheet = []
        def emit(f, img):
            Image.fromarray(img).save(os.path.join(out, 'f_%05d.png' % f)); sheet.append((f, img))
        render(root, fr, emit, w, h, ss, post)
        if '--sheet' in opt:
            cols = 7; tw, th = 480, 272; rows = (len(sheet) + cols - 1) // cols; S = Image.new('RGB', (cols * tw, rows * th))
            for i, (f, img) in enumerate(sheet): S.paste(Image.fromarray(img).resize((tw, th), Image.LANCZOS), ((i % cols) * tw, (i // cols) * th))
            S.save(opt['--sheet'], quality=88)
        return
    # the film: the frames on `jobs` processes, the sound, one H.264 file as tut_video.h plays (as tools/databuilder/intro.py makes of a recording)
    import tempfile, shutil
    jobs = max(1, min(int(opt.get('--jobs', '2')), 8)); tmp = opt.get('--tmp') or tempfile.mkdtemp(prefix='gitsfa_intro_'); os.makedirs(tmp, exist_ok=True)
    os.environ['GITSFA_INTRO_TMP'] = tmp   # (the workers' answer files go there too)
    ff = os.environ.get('GITSFA_FFMPEG', 'ffmpeg'); fmod = opt.get('--fmod') or os.environ.get('GITSFA_FMOD')
    if not fmod or not os.path.isdir(fmod): raise SystemExit('intro: the sound banks folder (Data/audio/Fmod_Build) is not known: --fmod or GITSFA_FMOD')
    print('intro: %d shots, %d frames (%.1f s) at %dx%d, %d process%s' % (len(shots), total, total / FPS, w, h, jobs, '' if jobs == 1 else 'es')); sys.stdout.flush()
    cuts = [round(total * k / jobs) for k in range(jobs + 1)]; parts = [os.path.join(tmp, 'part%d.rgb' % k) for k in range(jobs)]; kw = {'creationflags': 0x08000000} if os.name == 'nt' else {}
    ps = [subprocess.Popen([sys.executable, '-u', os.path.abspath(__file__), root, 'x', '--raw', parts[k], '--frames', '%d:%d' % (cuts[k], cuts[k + 1]), '--size', '%dx%d' % (w, h), '--ss', str(ss), '--post', '1' if post else '0'], **kw) for k in range(jobs)]
    import time
    t0 = time.time(); last = -1
    while any(p.poll() is None for p in ps):
        time.sleep(2.0); done = sum(os.path.getsize(p_) for p_ in parts if os.path.exists(p_)) // (w * h * 3)
        if done * 10 // total != last: last = done * 10 // total; print('  intro: %d of %d frames drawn (%.0f s)' % (done, total, time.time() - t0)); sys.stdout.flush()
    if any(p.returncode for p in ps): raise SystemExit('intro: a drawing process failed (exit codes %s)' % [p.returncode for p in ps])
    for k in range(jobs):
        if os.path.getsize(parts[k]) != (cuts[k + 1] - cuts[k]) * w * h * 3: raise SystemExit('intro: part %d is incomplete' % k)
    wav = os.path.join(tmp, 'intro.wav'); pk = mix_sound(root, fmod, wav); print('  intro: sound mixed (peak %.2f)' % pk); sys.stdout.flush()
    enc = subprocess.Popen([ff, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (w, h), '-r', str(FPS), '-i', '-', '-i', wav,
                            '-vf', 'scale=960:544:flags=lanczos,format=yuv420p', '-c:v', 'libx264', '-threads', os.environ.get('GITSFA_X264_THREADS', '3'), '-profile:v', 'baseline', '-level', '3.1',
                            '-refs', '1', '-bf', '0', '-preset', 'medium', '-crf', '19', '-g', '60', '-af', 'aresample=48000', '-c:a', 'aac', '-b:a', '128k', '-ac', '2', '-shortest', '-movflags', '+faststart', out], stdin=subprocess.PIPE, **kw)
    for p_ in parts:
        with open(p_, 'rb') as f: shutil.copyfileobj(f, enc.stdin, 1 << 22)
    enc.stdin.close(); enc.wait()
    if enc.returncode: raise SystemExit('intro: ffmpeg failed (exit %d)' % enc.returncode)
    if not opt.get('--tmp'): shutil.rmtree(tmp, ignore_errors=True)
    print('%s %d bytes (%.0f s)' % (out, os.path.getsize(out), time.time() - t0))
if __name__ == '__main__': main()
