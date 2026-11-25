# Shared builder for GVM1 files (skinned models + baked animation clips), used for the
# first-person viewmodel (camera space) and for third-person characters (model space).
import sys,os,struct,numpy as np,etcpak
from PIL import Image
sys.path.insert(0,os.path.join(os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'),'lib'));sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'))
from anim import *
from maps import find_maps,choose_map3
from skin import run as skinrun
import nifmesh

def qmat(q):
    x,y,z,w=q
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
def mat2q(m):
    t=np.trace(m)
    if t>0: s=np.sqrt(t+1)*2;q=[(m[2,1]-m[1,2])/s,(m[0,2]-m[2,0])/s,(m[1,0]-m[0,1])/s,0.25*s]
    elif m[0,0]>m[1,1] and m[0,0]>m[2,2]: s=np.sqrt(1+m[0,0]-m[1,1]-m[2,2])*2;q=[0.25*s,(m[0,1]+m[1,0])/s,(m[0,2]+m[2,0])/s,(m[2,1]-m[1,2])/s]
    elif m[1,1]>m[2,2]: s=np.sqrt(1+m[1,1]-m[0,0]-m[2,2])*2;q=[(m[0,1]+m[1,0])/s,0.25*s,(m[1,2]+m[2,1])/s,(m[0,2]-m[2,0])/s]
    else: s=np.sqrt(1+m[2,2]-m[0,0]-m[1,1])*2;q=[(m[0,2]+m[2,0])/s,(m[1,2]+m[2,1])/s,0.25*s,(m[1,0]-m[0,1])/s]
    q=np.array(q);return q/np.linalg.norm(q)

def static_meshes(path):
    h=nifmesh.load(path);d=h['d'];B=h['blocks'];out=[]
    sizes={0x30437:12,0x20436:8,0x10215:2,0x10425:4,0x40110:4,0x40401:4}
    def walk(k,M):
        t,o,s=B[k]
        a=nifmesh.node(d,o) if t=='NiNode' else nifmesh.mesh(d,o) if t=='NiMesh' else None
        if a is None: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];Wk=M@L
        if t=='NiNode':
            for c in a['children']: walk(c,Wk)
            return
        tex=None
        for p in a['props']:
            if B[p][0]=='NiTexturingProperty':
                raw=d[B[p][1]:B[p][1]+B[p][2]];v=struct.unpack_from('<I',raw,19)[0]
                if v<len(B) and B[v][0]=='NiSourceTexture': tex=nifmesh.S(h,struct.unpack_from('<I',d,B[v][1]+13)[0])
        P=N=UV=UV1=I=None
        for st in a['streams']:
            ds=nifmesh.stream(d,B[st['ref']][1])
            stride=sum(sizes.get(f,12) for f in ds['fmt'])
            raw=np.frombuffer(ds['data'],'u1').reshape(-1,stride);off=0
            for (sem,idx),f in zip(st['comps'],ds['fmt']):
                nm=nifmesh.S(h,sem);sz=sizes.get(f,12);col=raw[:,off:off+sz].copy()
                if nm=='POSITION' and P is None: P=col.view('<f4').reshape(-1,3)
                if nm=='NORMAL' and N is None: N=col.view('<f4').reshape(-1,3)
                if nm=='TEXCOORD' and idx==0 and UV is None: UV=col.view('<f4').reshape(-1,2)
                if nm=='TEXCOORD' and idx==1 and UV1 is None: UV1=col.view('<f4').reshape(-1,2)
                if nm=='INDEX': I=col.view('<u2' if sz==2 else '<u4').ravel().astype(np.int64)
                off+=sz
        if a.get('prim')==1 and I is not None and len(I)>=3:   # triangle strip -> list (alternate winding, drop degenerates)
            T=np.stack([I[:-2],I[1:-1],I[2:]],1);T[1::2]=T[1::2][:,[1,0,2]]
            I=T[(T[:,0]!=T[:,1])&(T[:,1]!=T[:,2])&(T[:,0]!=T[:,2])].ravel()
        P=(np.c_[P,np.ones(len(P))]@Wk.T)[:,:3];N=N@Wk[:3,:3].T if N is not None else np.zeros_like(P)
        out.append(dict(name=nifmesh.S(h,a['name']),P=P,N=N,UV=UV,UV1=UV1,I=I,tex=tex))
    walk(0,np.eye(4));return out

def node_worlds(path):
    """b103: every NiNode / NiMesh of a NIF -> its world matrix in the file's space (as static_meshes walks it), by name"""
    h=nifmesh.load(path);d=h['d'];B=h['blocks'];out={}
    def walk(k,M):
        t,o,s=B[k]
        a=nifmesh.node(d,o) if t=='NiNode' else nifmesh.mesh(d,o) if t=='NiMesh' else None
        if a is None: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];Wk=M@L
        nm=nifmesh.S(h,a['name'])
        if nm and nm not in out: out[nm]=Wk
        for c in a.get('children',()): walk(c,Wk)
    walk(0,np.eye(4));return out

# b62: per-vertex specular masks (the spare 4th byte of the vertex normal; engine model.h MD_VS: 0 = no highlight).
# The mask is the texture's own specular map (<name>_S / _s next to <name>_D / _d) where the game has one (operators Bato and
# Motoko, the first-person arms and guns), else a guess from the colour map (bright, grey parts: metal, visors; r = 0.3-0.76
# with the real maps); both normalised so their 95th percentile is 0.8 (guess: 0.5), sampled at the vertices' UVs.
SPEC_SIDE = 256
def spec_map_path(texpath):
    d, b = os.path.split(texpath); stem, ext = os.path.splitext(b)
    for suf in ('_D', '_d'):
        if stem.endswith(suf):
            want = (stem[:-2] + '_s').lower()
            try: names = sorted(os.listdir(d or '.'))
            except OSError: return None
            for n in names:
                if os.path.splitext(n)[0].lower() == want and os.path.splitext(n)[1].lower() in ('.dds', '.png'): return os.path.join(d, n)
    return None
_SPEC_CACHE = {}
def spec_image(texpath):
    if texpath in _SPEC_CACHE: return _SPEC_CACHE[texpath]
    sp = spec_map_path(texpath)
    if sp:
        a = np.asarray(Image.open(sp).convert('L').resize((SPEC_SIDE, SPEC_SIDE), Image.BOX)).astype(np.float64) / 255; top = 0.8
    else:
        D = np.asarray(Image.open(texpath).convert('RGB').resize((SPEC_SIDE, SPEC_SIDE), Image.BOX)).astype(np.float64) / 255
        mx = D.max(2); mn = D.min(2); a = (D @ np.array([0.3, 0.59, 0.11])) * (1 - (mx - mn) / np.maximum(mx, 1e-3)); top = 0.5
    a = np.clip(a / max(float(np.percentile(a, 95)), 0.25) * top, 0, 1)
    _SPEC_CACHE[texpath] = (a, sp); return _SPEC_CACHE[texpath]
def _bilinear(a, UV):
    h, w = a.shape; uv = np.asarray(UV, np.float64)
    u = np.mod(uv[:, 0], 1.0) * w - 0.5; v = np.mod(uv[:, 1], 1.0) * h - 0.5
    x0 = np.floor(u).astype(int); y0 = np.floor(v).astype(int); fx = u - x0; fy = v - y0
    g = lambda x, y: a[np.mod(y, h), np.mod(x, w)]
    return g(x0, y0) * (1 - fx) * (1 - fy) + g(x0 + 1, y0) * fx * (1 - fy) + g(x0, y0 + 1) * (1 - fx) * fy + g(x0 + 1, y0 + 1) * fx * fy
def spec_sample_legacy(texpath, UV):
    a, _ = spec_image(texpath); return _bilinear(a, UV)

# b68 (the user: "make sure that nothing looks too shiny or too matte ... scan any values from the pc game's data files"):
# two per-vertex weights instead of one normalised mask - the highlight's and the reflection's, in the PC material's own
# units (vm/matdb.py: the WeaponMaterial / WeaponColorMaterial terms, the NIF's numbers, the _S and _C maps):
#   reflection weight = Gloss(_S) x C.b x Reflact_Value x g_CubeMapBrightness   (metal reflects, rubber / plastic: C.b ~ 0)
#   highlight weight  = Gloss(_S) x Specular_Value x 18 / (C.r x 255 + 2)       (the PC's (n.h)^(C.r x 255) highlight with the
#                       energy of the engine's (n.h)^16 lobe: a tight PC highlight is a faint broad one here, not a broad bright one)
# b62-b67's mask (_S over its own 95th percentile x 0.8) made a black polymer gun as reflective as polished steel: the KRISS
# x46, the 870 x38, the operators x43-89 the PC's reflection next to the M4A1's x9, the Raptor axe x1.2.
# Models whose NIFs carry no such numbers (the Think Tanks' Tachi_S) keep b62-b67's mask, scaled to look as before
# (LEGACY_*: the engine's gains changed, model.h MD_GAIN_*).
MASK_CACHE = {}
MAT_ALIAS = {}   # a builder's own colour map (a PNG it made) -> the game's colour map whose material it carries
GLOW_ALIAS = {}  # b71: a colour map -> the glow map to use instead of its material's (a texture-only skin's own _E)
LEGACY_SPEC, LEGACY_ENV = 1.0, 0.6
_LEGACY_SAID = set()
def material_record(texpath):
    """the PC material (vm/matdb.py) of a colour map, or None"""
    import matdb
    rec = matdb.lookup(MAT_ALIAS.get(texpath, texpath))
    if rec is None:
        st, ext = os.path.splitext(texpath)
        for alt in (st + '.dds', st + '.DDS', st.replace('_dark', '_d') + '.dds'):   # (make_vm_weapon's 1sthand2_dark.png: 1sthand2_d.dds at half brightness)
            if alt != texpath and os.path.exists(alt): rec = matdb.lookup(alt)
            if rec: break
    return rec
def c_map(path, size, how):
    """the material's shader map (*_C.dds) at a size, 0..255 (RGBA). b103 (user: 'saito's standard skin seems to render
    strangely in the operatives screen', 'motoko's hair and face ... with her phoenix skin'): Pillow resizes RGBA premultiplied
    by alpha, so where the map's alpha (the smoothness) is 0 - the skin and hair of Saito_A, Motoko_A / _sig, Kuro_A, Bato_ordinary,
    all of Maven_A_Camo - the other channels came out 0: the occlusion bake (green) darkened those parts by its whole power
    (Saito's face x 0.4) and the highlight / reflection weights were read from 0 (b68-b102). Where the resized alpha is under
    32 / 255 (the premultiplied 8-bit colour holds under 5 bits there) each channel is resized on its own; elsewhere the filter
    is the one every model was built with (alpha-weighted: the same picture to within 1.6% of full scale, and those models
    come out byte for byte as before)."""
    im = Image.open(path).convert('RGBA')
    old = np.asarray(im.resize(size, how)).astype(np.float64)
    own = np.stack([np.asarray(im.getchannel(ch).resize(size, how)) for ch in 'RGBA'], 2).astype(np.float64)
    return np.where((old[:, :, 3] < 32)[:, :, None], own, old)   # (0..255, whole numbers: the callers divide in their own precision, as before)
def material_shade(texpath, rgba):
    """b68: the PC's own shading of the colour map that does not depend on the light, baked in (no cost on the Vita):
    the material's ambient occlusion (the _C map's green: base x lerp(1, C.g, g_AOPower) - WeaponMaterial ps_3_0: 'mad r0.xyz,
    c{g_AOPower}, r6, r0' with r6 = base x C.g - base) and Diffuse_Value (base x Diffuse_Value). g_AOPower 0.6 on the weapons
    and hands, 0.3 on the operators; the M4A1's C.g averages 0.62: its recesses go dark as on the PC."""
    rec = material_record(texpath)
    if rec is None: return rgba
    dv = rec.get('dval', 1.0)
    # b115: a sheen material (SHEEN_RIM_MIN: Reiko's hair - Diffuse_Value 0.5 with g_RimPower 2.0 and Fresnel_Value 2.5) keeps its
    # colour map's own level: on the PC the rim and Fresnel light fall over the whole of the hair's cards (per pixel, on the
    # normal map) and make it silver-white - the user's TOP OPERATIVE picture, her portrait; the port's per-vertex rim
    # (model.h md_mesh_sheen) reaches the outline only. Tried 0.5 / 0.8 / 1.0 against that picture on the OPERATIVE page.
    if rec.get('rim', 0) >= SHEEN_RIM_MIN: dv = max(dv, 1.0)
    a = np.asarray(rgba).astype(np.float32); f = np.full(a.shape[:2], dv, np.float32)
    if rec.get('C') and rec.get('ao', 0) > 0:
        g = c_map(rec['C'], rgba.size, Image.BILINEAR).astype(np.float32)[:, :, 1] / 255
        f *= 1 - rec['ao'] * (1 - g)
    if np.allclose(f, 1): return rgba
    a[:, :, :3] = np.clip(a[:, :, :3] * f[:, :, None], 0, 255)
    return Image.fromarray((a + 0.5).astype(np.uint8), 'RGBA')
def mask_images(texpath):
    """(highlight weight image, reflection weight image) for a colour map, or None (no material numbers: legacy mask)"""
    if texpath in MASK_CACHE: return MASK_CACHE[texpath]
    rec = material_record(texpath)
    out = None
    if rec and rec['S']:
        S = np.asarray(Image.open(rec['S']).convert('L').resize((SPEC_SIDE, SPEC_SIDE), Image.BOX)).astype(np.float64) / 255
        if rec['C']:
            C = c_map(rec['C'], (SPEC_SIDE, SPEC_SIDE), Image.BOX).astype(np.float64) / 255
            cb, n = C[:, :, 2], np.clip(C[:, :, 0] * 255, 1, 255)
        else:
            cb, n = np.zeros_like(S), np.full_like(S, rec['gloss'] if rec['gloss'] > 0 else 20.0)
        hi = S * rec['spv'] * 18.0 / (n + 2.0) if rec['spec'] else np.zeros_like(S)
        out = (hi, S * cb * rec['refl'] * rec['bright'], rec)
        if rec.get('wgenv'): out = (hi, S.copy(), rec)   # b112: WGStandardMaterial - Gloss x cube (vm/matdb.py)
    MASK_CACHE[texpath] = out; return out
def spec_sample(texpath, UV):
    """(n, 2): the highlight and reflection weights at the vertices (b68)"""
    m = mask_images(texpath)
    if m is None:
        if texpath not in _LEGACY_SAID: _LEGACY_SAID.add(texpath); print('masks: no material numbers for %s - the b62 mask (legacy)' % os.path.basename(texpath), file=sys.stderr)
        a = spec_sample_legacy(texpath, UV); return np.c_[a * LEGACY_SPEC, a * LEGACY_ENV]
    return np.c_[_bilinear(m[0], UV), _bilinear(m[1], UV)]
def pack_masks(SP):
    """the normal's 4th byte: 11 x highlight level + reflection level, each level = round(cbrt(weight) x 10) (weights 0-1:
    finer steps for the small ones; engine model.h MD_VS_MASK decodes level^3 / 1000). A 1-D SP (an older caller): both."""
    SP = np.asarray(SP, np.float64)
    if SP.ndim == 1: SP = np.c_[SP, SP]
    q = np.round(np.cbrt(np.clip(SP, 0, 1)) * 10).astype(int)
    return 11 * q[:, 0] + q[:, 1]

# b71 (the user: "make it look closer to these in game screenshots from the pc version"): the PC's glow (emissive) maps - the
# NiTexturingProperty glow slot, *_E / *_g.dds, added on top of the lit colour x Glow_Value (the operators' cyan suit lights,
# the enemy masks' red visors, the arm launcher's lens). The operators' and masks' glow maps are laid out on the meshes'
# second uv set (TEXCOORD 1, a small atlas), so the glow is baked into the colour map's own layout: every triangle drawn in
# uv set 0 at the colour map's size, the glow map read at the matching uv set 1 point. No new vertex data, no second texture:
# the colour map becomes DXT5 with alpha = 1 - e (e = the glow's strength) and its colour leans to the glow's hue by e; the
# engine adds colour x (1 - alpha) x the mesh's emissive gain (model.h MD_FS: meshes flagged MESH_EMIS only).
MESH_CUT = 4   # b116: blend mode 4 (model.h MD_BM_CUT): a cut-out part, blended over the body by its alpha with depth written
MESH_EMIS, MESH_MASK, MESH_UNMASK = 1 << 16, 1 << 17, 1 << 18   # blend word bits (model.h MDF_*): glow / enemy mask part / hidden under the mask
def _tri_uvs(m):
    """(n, 3, 2) uv set 0 and uv set 1 corners of a mesh's triangles (skin.run output or a static_meshes part), or None"""
    UV, UV1 = m.get('UV'), m.get('UV1')
    if UV is None or UV1 is None: return None
    if 'idxregs' in m:   # skinned: indices per region, relative to the region's first vertex
        T = np.concatenate([m['I'][is_:is_ + ic] + vs for (vs, vc), (is_, ic) in zip(m['vregs'], m['idxregs'])])
    else: T = np.asarray(m['I'])
    T = T[:len(T) // 3 * 3].reshape(-1, 3)
    return np.asarray(UV, np.float64)[T], np.asarray(UV1, np.float64)[T]
def _sample(img, uv):
    """bilinear, wrapping: img (h, w, c) float, uv (n, 2) -> (n, c)"""
    h, w = img.shape[:2]; u = np.mod(uv[:, 0], 1.0) * w - 0.5; v = np.mod(uv[:, 1], 1.0) * h - 0.5
    x0 = np.floor(u).astype(int); y0 = np.floor(v).astype(int); fx = (u - x0)[:, None]; fy = (v - y0)[:, None]
    g = lambda x, y: img[np.mod(y, h), np.mod(x, w)]
    return g(x0, y0) * (1 - fx) * (1 - fy) + g(x0 + 1, y0) * fx * (1 - fy) + g(x0, y0 + 1) * (1 - fx) * fy + g(x0 + 1, y0 + 1) * fx * fy
def bake_uv1(meshes, E, W, H):
    """the glow map E (h, w, 3 float) read through uv set 1, laid out in uv set 0 at W x H; covered: (H, W) bool"""
    out = np.zeros((H, W, 3)); cov = np.zeros((H, W), bool)
    for m in meshes:
        r = _tri_uvs(m)
        if r is None: continue
        A, B = r
        for t in range(len(A)):
            a = A[t]; c0 = np.floor(a.mean(0)); a = (a - c0) * [W, H]   # (tiling uvs: the triangle's own tile)
            x0, y0 = np.floor(a.min(0)).astype(int); x1, y1 = np.ceil(a.max(0)).astype(int)
            x0 = max(x0, 0); y0 = max(y0, 0); x1 = min(x1, W); y1 = min(y1, H)
            if x1 <= x0 or y1 <= y0: continue
            gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            d = (a[1, 1] - a[2, 1]) * (a[0, 0] - a[2, 0]) + (a[2, 0] - a[1, 0]) * (a[0, 1] - a[2, 1])
            if abs(d) < 1e-9: continue
            l0 = ((a[1, 1] - a[2, 1]) * (gx - a[2, 0]) + (a[2, 0] - a[1, 0]) * (gy - a[2, 1])) / d
            l1 = ((a[2, 1] - a[0, 1]) * (gx - a[2, 0]) + (a[0, 0] - a[2, 0]) * (gy - a[2, 1])) / d
            l2 = 1 - l0 - l1; e = -0.5 / max(abs(d), 1.0)   # (half a pixel's slack: thin slivers still get their pixels)
            ins = (l0 >= e) & (l1 >= e) & (l2 >= e)
            if not ins.any(): continue
            b = B[t]; q = l0[ins, None] * b[0] + l1[ins, None] * b[1] + l2[ins, None] * b[2]
            yy, xx = (gy[ins] - 0.5).astype(int), (gx[ins] - 0.5).astype(int)
            out[yy, xx] = np.maximum(out[yy, xx], _sample(E, q)); cov[yy, xx] = True
    for _ in range(4):   # bleed into the gutters around the islands (mips, filtering)
        nb = np.zeros_like(out); k = np.zeros((H, W), bool)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            sh = np.roll(np.roll(out, dy, 0), dx, 1); sc = np.roll(np.roll(cov, dy, 0), dx, 1)
            nb = np.where((sc & ~cov)[:, :, None], np.maximum(nb, sh), nb); k |= sc & ~cov
        out = np.where(k[:, :, None], nb, out); cov = cov | k
    return out, cov
EMIS_SAID = set()
def emissive_image(texpath, meshes, W, H, synth_red=False):
    """b71: the glow at W x H in the colour map's layout (H, W, 3 float 0..1, Glow_Value applied), or None. The glow map from
    the PC material (vm/matdb.py: slot, uv set, Glow_Value); synth_red: no glow map - the colour map's own painted red lights
    (the male enemy masks: their red visor strips are only in _D)"""
    rec = material_record(texpath); E = None; gv = 1.0
    if rec and rec.get('G'):
        if texpath in GLOW_ALIAS: rec = dict(rec, G=GLOW_ALIAS[texpath])
        E = np.asarray(Image.open(rec['G']).convert('RGB')).astype(np.float64) / 255; gv = rec.get('glow', 1.0)
        if rec.get('guv', 0) == 1: E, cov = bake_uv1(meshes, E, W, H)
        else: E = np.asarray(Image.fromarray((E * 255 + 0.5).astype(np.uint8)).resize((W, H), Image.BILINEAR)).astype(np.float64) / 255
        what = '%s (uv set %d) x %.2f' % (os.path.basename(rec['G']), rec.get('guv', 0), gv)
    elif synth_red:
        D = np.asarray(Image.open(texpath).convert('RGB').resize((W, H), Image.BILINEAR)).astype(np.float64) / 255
        r, g, b = D[:, :, 0], D[:, :, 1], D[:, :, 2]; k = np.clip((r - 0.45) * 4, 0, 1) * np.clip((r - 1.6 * np.maximum(g, b)) * 5, 0, 1)
        E = D * k[:, :, None]; what = 'red lights of %s' % os.path.basename(texpath)
    if E is None: return None
    E = np.clip(E * gv, 0, 1)
    if texpath not in EMIS_SAID: EMIS_SAID.add(texpath); print('glow: %s <- %s, %.1f %% of the texels lit' % (os.path.basename(texpath), what, 100 * (E.max(2) > 0.05).mean()), file=sys.stderr)
    return E if E.max() > 0.02 else None

# b69: each mip level from the top one with Lanczos (b1-b68: a box of the level above: every level a little softer than
# the last), the alpha channel an area average from the top (alpha-tested parts keep their coverage); GITSFA_MIPF=box: as before
MIPF=os.environ.get('GITSFA_MIPF','lanczos')
def mip_down(top,prev,w,h):
    if MIPF=='box': return prev.resize((w,h),Image.BOX)
    t=top.convert('RGBA');rgb=t.convert('RGB').resize((w,h),Image.LANCZOS,reducing_gap=None);rgb.putalpha(t.getchannel('A').resize((w,h),Image.BOX));return rgb
SHEEN_RIM_MIN=1.0
class Builder:
    spec = False   # b62: True = write specular masks (make_chr.py, make_vm_weapon.py, make_vm.py)
    def __init__(self,rig):
        self.rig=rig;self.RN=rig['names'];self.bones=[];self.palette=[];self.texlist=[];self.TEX={};self.EMIS=set();self.meshes=[];self.clips=[];self.att={}
    def bone(self,nm):
        i=self.RN.index(nm.replace(' ','_'))
        if i not in self.bones: self.bones.append(i)
        return self.bones.index(i)
    def pal(self,slot,S):
        for k,(b,s) in enumerate(self.palette):
            if b==slot and np.allclose(s,S,atol=1e-4): return k
        self.palette.append((slot,S));return len(self.palette)-1
    def tex(self,path,maxs=512,emis=None):   # (b69: mips from the top level, tools/map_visual.py _mip_down)
        """emis (b71): fn(W, H) -> the glow in this texture's layout (gvmlib.emissive_image) or None"""
        key=(path,'E') if emis else path
        if key in self.TEX: return self.TEX[key]
        im=Image.open(path).convert('RGBA')
        if self.spec: im=material_shade(path,im)   # b68: the PC material's ambient occlusion and Diffuse_Value
        W,H=im.size;sc=min(1,maxs/max(W,H))
        im=im.resize((max(4,int(W*sc)),max(4,int(H*sc))),Image.LANCZOS)
        fmt=5 if np.asarray(im)[:,:,3].min()<250 else 1
        E=emis(*im.size) if emis else None
        if E is not None and fmt==5: print('glow: %s has its own alpha - no glow'%os.path.basename(path),file=sys.stderr);E=None
        if E is not None:   # b71: alpha = 1 - e, the colour towards the glow's hue by e (engine: + colour x (1 - alpha) x gain)
            a=np.asarray(im).astype(np.float64);e=E.max(2);hue=E/np.maximum(e,1e-3)[:,:,None]
            a[:,:,:3]=a[:,:,:3]*(1-e[:,:,None])+hue*255*e[:,:,None];a[:,:,3]=255*(1-e)
            im=Image.fromarray(np.clip(a+0.5,0,255).astype(np.uint8),'RGBA');fmt=5
        mm=[];w_,h_=im.size;cur=im
        while w_>=4 and h_>=4:
            a=np.ascontiguousarray(np.asarray(cur.convert('RGBA'))).tobytes()
            mm.append(etcpak.compress_to_dxt1(a,w_,h_) if fmt==1 else etcpak.compress_to_dxt5(a,w_,h_))
            if w_==4 or h_==4: break
            w_//=2;h_//=2;cur=mip_down(im,cur,w_,h_)
        self.texlist.append((im.size[0],im.size[1],fmt,mm));self.TEX[key]=len(self.texlist)-1
        # b115: a material whose rim light is far above the operators' usual 0.2-0.5 (Reiko's hair 2.0, Sitara's 0.8 - with
        # Diffuse_Value 0.5 / 0.7: the PC gives the light back as rim and Fresnel light) tells the engine so: attachment
        # 'mat<texture index>' = (g_RimPower, Fresnel_Value x the Fresnel colour's brightness, 0) - model.h md_mesh_sheen
        if self.spec:
            import matdb
            rec=matdb.lookup(path)
            if rec and rec.get('rim',0)>=SHEEN_RIM_MIN:
                fc=rec.get('fresc') or (0.62,0.62,0.62)
                self.att['mat%d'%self.TEX[key]]=np.array([rec['rim'],rec.get('fres',0)*(0.299*fc[0]+0.587*fc[1]+0.114*fc[2]),0],float)
        if E is not None: self.EMIS.add(self.TEX[key])
        return self.TEX[key]
    def envmap(self,cube,lobe=64.0):
        """b112: the model's own reflection picture (a NiTextureEffect cube that is not the weapons' CUBE.dds - the cyborg's
        cybogCUBE.dds): the cube as the engine's sphere map (vm/make_env.py sphere_map), kept as one more texture of the model;
        attachment 'envmap' = (texture index, 1, 0). model.h binds it in place of env_ref.gtx for this model's draws."""
        import make_env,tempfile
        a=make_env.sphere_map(cube,lobe);p=os.path.join(tempfile.mkdtemp(prefix='envmap_'),'envmap.png');Image.fromarray(a,'RGBA').save(p)
        sp=self.spec;self.spec=False   # (no material shading on a reflection picture)
        try: t=self.tex(p,128)
        finally: self.spec=sp
        W_,H_,fmt,mm=self.texlist[t];self.texlist[t]=(W_,H_,fmt,mm[:1])   # (one level: looked up per vertex, never minified)
        self.att['envmap']=np.array([t,1,0],float);return t
    def add_skinned(self,m,texpath,maxs=512,alias=None,emis=None,flags=0,alias_s2b=False):
        alias=alias or {}
        pal_map=[]
        # b77 (user: faces caved in on the operative screen - the b76 menu models, and Reiko / Maven_ordinary / Saito_ordinary /
        # Togusa_New in game): a bone outside the rig (the face rig's B_Brow / B_Eyelid_Up / B_Eyelid_Down) drawn by its alias
        # takes the ALIAS's skin-to-bone transform - with its own, its vertices were placed in the head bone's frame by the
        # brow's / eyelid's bind (the brow and eyelids sank into the face). Bind poses agree, so the head's own s2b is the one.
        s2b_of={bn:s2b for bn,s2b in zip(m['bone_names'],m['sm']['s2b'])}
        for bn,s2b in zip(m['bone_names'],m['sm']['s2b']):
            nm=alias.get(bn,bn)
            if nm!=bn and alias_s2b:   # (make_chr only: the other builders' aliased models are as shipped)
                if nm in s2b_of: s2b=s2b_of[nm]
                else: print('gvmlib: alias %s -> %s: the target is not in the skin; its own bind kept'%(bn,nm),file=sys.stderr)
            pal_map.append(self.pal(self.bone(nm),s2b))
        BIr=(m['BI'][0].view('<u2').reshape(-1,4) if m['BI'][0].shape[1]==8 else m['BI'][0]).astype(int)
        BW=m['BW'][0].view('<f4').reshape(len(BIr),-1)
        BW=np.c_[BW,1-BW.sum(1)] if BW.shape[1]==3 else BW/np.maximum(BW.sum(1,keepdims=True),1e-6)
        palr=m['PAL'][0].view('<u2').ravel();V=[];I=[]
        for (vs,vc),(ps,pc),(is_,ic) in zip(m['vregs'],m['PAL'][2],m['idxregs']):
            bi=np.array([[pal_map[palr[ps+j]] for j in row] for row in BIr[vs:vs+vc]])
            V.append((m['P'][vs:vs+vc],m['UV'][vs:vs+vc],bi,BW[vs:vs+vc]));I.append(m['I'][is_:is_+ic]+vs)
        P=np.concatenate([v[0] for v in V]);UV=np.concatenate([v[1] for v in V]);BI=np.concatenate([v[2] for v in V]);BWt=np.concatenate([v[3] for v in V]);I=np.concatenate(I)
        Nn=np.zeros_like(P);tri=I.reshape(-1,3);fn=np.cross(P[tri[:,1]]-P[tri[:,0]],P[tri[:,2]]-P[tri[:,0]])
        for k in range(3): np.add.at(Nn,tri[:,k],fn)
        Nn/=np.linalg.norm(Nn,axis=1,keepdims=True)+1e-9
        self.meshes.append(dict(tex=self.tex(texpath,maxs,emis),blend=flags,P=P,N=Nn,UV=UV,BI=BI,BW=BWt,I=I,SP=spec_sample(texpath,UV) if self.spec else None))
    def add_rigid(self,parts,attachW,bone_name,S2B,texdir,blend_for=lambda t:0,maxs=512,texmap=None,emis_for=None,flags=0):
        """emis_for (b71): fn(part, texture path) -> the tex() emis function or None; flags: blend bits 16+ (MESH_*)"""
        pe=self.pal(self.bone(bone_name),S2B)
        for m in parts:
            if np.ndim(m.get('UV'))!=2 or len(m['UV'])!=len(m['P']): continue   # (b92: a part mesh without UVs - the SEBURO MG sight's lens marker - is not drawn)
            P=(np.c_[m['P'],np.ones(len(m['P']))]@attachW.T)[:,:3];N=m['N']@attachW[:3,:3].T
            tp=texmap(m['tex']) if texmap else texdir+m['tex'];bl=blend_for(m['tex'])|flags
            self.meshes.append(dict(tex=self.tex(tp,maxs,emis_for(m,tp) if emis_for else None),blend=bl,P=P,N=N,UV=m['UV'],BI=np.tile([pe,pe,pe,pe],(len(P),1)),BW=np.tile([1.,0,0,0],(len(P),1)),I=m['I'],SP=spec_sample(tp,m['UV']) if self.spec and not (bl&15) else None))
    def bake(self,name,frames,loop,space_R=None,space_t=None):
        """frames: list of (P,W) per frame in rig space. space: p' = R @ (p - t), q' = R*q"""
        R=np.eye(3) if space_R is None else space_R;t=np.zeros(3) if space_t is None else space_t
        data=np.zeros((len(frames),len(self.bones),7),'f4')
        for i,(P,W) in enumerate(frames):
            for s,b in enumerate(self.bones):
                data[i,s,:4]=mat2q(R@qmat(W[b]));data[i,s,4:]=R@(P[b]-t)
        for s in range(len(self.bones)):
            for i in range(1,len(frames)):
                if np.dot(data[i,s,:4],data[i-1,s,:4])<0: data[i,s,:4]*=-1
        self.clips.append((name,loop,30.0,data))
    def write(self,path):
        with open(path,'wb') as f:
            f.write(b'GVM1'+struct.pack('<IIIII',len(self.texlist),len(self.bones),len(self.palette),len(self.meshes),len(self.clips)))
            for W_,H_,fmt,mm in self.texlist:
                f.write(struct.pack('<HHBB',W_,H_,fmt,len(mm)))
                for b in mm: f.write(struct.pack('<I',len(b)));f.write(b)
            for b in self.bones: f.write(self.RN[b].encode()[:31].ljust(32,b'\0'))
            for slot,S in self.palette: f.write(struct.pack('<i',slot));f.write(S[:3,:].astype('<f4').tobytes())
            for m in self.meshes:
                n=len(m['P']);ni=len(m['I']);assert n<65536
                v=np.zeros(n,dtype=[('p','<f4',3),('n','i1',4),('uv','<f4',2),('bi','u1',4),('bw','u1',4)])
                v['p']=m['P'];v['n'][:,:3]=np.clip(np.round(m['N']*127),-127,127);v['uv']=m['UV'];v['bi']=m['BI']
                if m.get('SP') is not None and not (m['blend']&15): v['n'][:,3]=pack_masks(m['SP'])   # b62: specular mask; b68: highlight + reflection weights
                bw=np.round(m['BW']*255).astype(int);bw[:,0]+=255-bw.sum(1);v['bw']=np.clip(bw,0,255)
                f.write(struct.pack('<iiII',m['tex'],m['blend']|(MESH_EMIS if m['tex'] in self.EMIS else 0),n,ni));f.write(v.tobytes());f.write(m['I'].astype('<u2').tobytes())   # (b71: + the glow flag)
                if ni%2: f.write(b'\0\0')
            for name,loop,fps,data in self.clips:
                f.write(name.encode()[:15].ljust(16,b'\0'));f.write(struct.pack('<IIf',data.shape[0],loop,fps));f.write(data.astype('<f4').tobytes())
            f.write(struct.pack('<I',len(self.att)))
            for k,v in self.att.items(): f.write(k.encode()[:15].ljust(16,b'\0'));f.write(np.asarray(v,'<f4').tobytes())
        return os.path.getsize(path)

def clip_frames(adir,prefix,rig,maps):
    f=[x for x in os.listdir(adir) if x.startswith(prefix)][0]
    h=hdr(adir+f);C=decode_clip(h)
    if h['L'][4] or h['L'][5]: print('  WARNING spline channels in',f)
    best=choose_map3(h,C,maps,rig)
    if best: C['map']=best[1]['list']
    fr=[]
    for i in range(C['nf']):
        if C['mode']==1: lp,lq=pose(rig,C,i);P,W=fk(rig['parent'],lp,lq)
        else: P,W=pose_world(rig,C,i)
        fr.append((P,W))
    return fr,h
