# Map visuals: world_lmap.nif -> <name>.gmap version 2 (textures + meshes with per-mesh material records).
# usage: map_visual.py MAPDIR OUTDIR NAME [TEXMAX]      (make_map.py calls build() for the visual part)
#
# What the NIF materials say and how it is kept (Gamebryo 30.2 data as written by the game's tools):
#  - properties are inherited from parent nodes (NiAlphaProperty, NiZBufferProperty, NiStencilProperty, ...)
#  - NiTexturingProperty slots: base = diffuse, detail = Beast lightmap (its own UV set, usually 1, sometimes 2),
#    glow = emissive map (added on top, scaled by the mesh's Glow_Value), gloss/normal are not used on the Vita
#  - NiAlphaProperty: blend -> blended pass (src alpha / one-minus-src-alpha, or additive), test -> alpha test with
#    the material's own threshold, neither -> opaque even when the texture has an alpha channel
#  - NiStencilProperty draw mode BOTH -> two-sided; NiZBufferProperty without write -> no depth writes
#  - meshes without a lightmap get a neutral lightmap (value 0.5, x2 = 1.0) so every mesh runs the same shader path
#  - meshes without a base texture use a white texture tinted with the material colour
#  - UmbraAlpha_* meshes without a real base texture (none, or the Base_D placeholder) are occlusion-culling helper
#    volumes (big low-poly boxes) that the game never draws: skipped
#  - glossy floors (blended at a uniform alpha >= 0.7 over a planar reflection) are drawn opaque at colour x alpha
#  - the map's world effects (WorldEffectFx.wff: screens, signs, billboards, range markers) are added as static meshes
#    posed at t = 0 (world_fx.py); blended ones with 0..1 UVs share one atlas and carry their colour per vertex
#  - shooting / tutorial3: EXPOSURE calibrated on the Shooting Range footage
#  - b67 environment maps (user: "reflective floors and windows ... without making things worse"): the game hangs an
#    NiTextureEffect (environment map, sphere-map coordinates) on the nodes above most meshes of the indoor / city maps --
#    per area reflection pictures from map.dat map\FixedMap\Texture (re_*: soft blobs of the area's lights, a few photos)
#    -- modulated by the material's gloss map (_S). Kept where it shows: mean gloss x the picture's 99th percentile >=
#    ENV_CMIN (the rest adds < ~6 / 255 at its brightest); envk = mean gloss x ENV_K (the gloss map's detail is not kept:
#    one texture read more a pixel, not two). The normals (the NIF's NORMAL stream) only on these meshes. Opaque, blended
#    (glass) and opaque + glow meshes; not alpha-tested ones (leaves, grates) nor any with vertex colour. The engine draws
#    them with the env programs (world.h W_ENV) near the camera and drops back to the plain ones when frames run late; it
#    weights the reflection per pixel by a Fresnel term (F0 0.1) and the lightmap (shade reflects less), so ENV_K 2.25.
#    Host A/B (mapvis/render.py, the engine shader's twin) at the views each map changes most: without those weights the
#    soft pictures washed shaded shutters, screens and whole floors in one even tint (Ruined Tokyo, Train Station's kiosk,
#    E-Center's library, PSS9's deck); with them the floors keep their colour underfoot and take a sheen toward grazing.
#
# .gmap v2 layout (little endian):
#   'GMAP' u32 version=2, u32 ntex, u32 nmesh
#   ntex x { u16 w, u16 h, u8 fmt (0 = RGBA8, 1 = DXT1, 5 = DXT5), u8 nmips, nmips x { u32 size, data } }
#   nmesh x { i32 tex, i32 lm, i32 glow, u32 mode (0 opaque, 1 alpha test, 2 blend), u32 flags, u32 nv, u32 ni,
#             f32 ref, f32 glowk, f32 tint[4], f32 bbmin[3], f32 bbmax[3],
#             vertices nv x (pos3, uv0 2, uv1 2 [, rgba8 when flags&4]) f32, indices ni x u16 (+u16 pad if ni odd) }
#   flags: 1 two-sided, 2 no depth write, 4 vertex colour, 8 additive blend, 16 texture coordinates already rebased
#   b67: flags 64 = environment map (the NIF's NiTextureEffect, below): the record carries i32 env, f32 envk after tint[4]
#        (before bbmin) and every vertex one more 4-byte slot after the others: the world-space normal as 3 x int8 (/127) + 0
import sys,os,struct,numpy as np
from PIL import Image
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import gitspaths
from nifmesh import load,S,node,mesh,stream,avobj
from uvrebase import rebase,split16
GLOWDIR=os.environ.get('GITS_GLOW') or gitspaths.path('MAPVIS','glow')   # glow (_G / _E) maps from map.dat map\FixedMap\Texture
ENVDIR=gitspaths.path('MAPVIS','env');GLOSSDIR=gitspaths.path('MAPVIS','gloss')   # b67: reflection pictures, gloss (_S) maps (lower-case names)
ENV_ON=int(os.environ.get('GITS_ENV','1'));ENV_CMIN=float(os.environ.get('GITS_ENV_CMIN','0.06'));ENV_K=float(os.environ.get('GITS_ENV_K','2.25'));ENVMAX=128
F_ENV=64   # (32 is the engine's own load-time WFLAG_FLAT)
SLOTS=['base','dark','detail','gloss','glow','bump','normal','parallax','decal0','decal1','decal2','decal3']
F_TWO,F_NOZW,F_VCOL,F_ADD,F_REB=1,2,4,8,16
# The VR shooting range maps come out far brighter than the original (footage: 'Shooting Range' video, floor ~(2,5,38),
# target boards dark navy): their tutorial materials are darker in the game than base x lightmap x 2. Calibrated on the footage.
# Tutorial3 (the VR combat-training space of the tutorial): 0.6 -- the 'Early Access Game Intro tutorial' footage shows
# its floor about twice as bright as the Shooting Range's (median luma 25 vs 13 on 24 frames; 0.33 gave 13).
EXPOSURE={'shooting':0.33,'tutorial3':0.6}
# Decal textures whose meshes carry no NiAlphaProperty but whose texture is a cut-out on a black background (alpha 0
# around the shape): drawn opaque they leave black blobs on the roads and walls (b28 Ruined Tokyo). Alpha-tested at 0.5.
CUTOUT=('rubble_jm_d_','conalpha_','gp_walldecal')
_CUT={}
def _has_cutout(MAPDIR,base):   # only textures with a real transparent area (rubble_jm_d_01 is fully opaque: no alpha test for it)
    p=MAPDIR+'/tex/'+os.path.splitext(os.path.basename(base.replace('\\','/')))[0].lower()+'.png'
    if p not in _CUT:
        try: _CUT[p]=float((np.asarray(Image.open(p).convert('RGBA'))[:,:,3]<10).mean())>0.05
        except Exception: _CUT[p]=False
    return _CUT[p]
def _is_cut(P,base,MAPDIR):
    return not P.get('NiAlphaProperty') and bool(base) and any(k in base.lower() for k in CUTOUT) and _has_cutout(MAPDIR,base)
# Cut-out decals only show what is behind them where something IS behind them: parts of the Ruined Tokyo rubble meshes
# are the only floor there (cut out, the sky shows through the street). Per triangle: rays from the centre, the three
# corners and the three edge midpoints (pulled in by 5 %) go back along the face normal; if all seven hit another opaque mesh within COVER_CM the
# triangle is alpha-tested, else it stays opaque with the transparent area filled with the surrounding colour (FILL).
COVER_CM=30.0
class _Occ:
    def __init__(s,tris,cell=300.0):
        s.T=tris.astype(np.float64);s.cell=cell;s.grid={}
        mn=s.T.min(1);mx=s.T.max(1)
        x0=np.floor(mn[:,0]/cell).astype(int);x1=np.floor(mx[:,0]/cell).astype(int);y0=np.floor(mn[:,1]/cell).astype(int);y1=np.floor(mx[:,1]/cell).astype(int)
        big=[]
        for k in range(len(s.T)):
            if (x1[k]-x0[k]+1)*(y1[k]-y0[k]+1)>400: big.append(k);continue
            for gx in range(x0[k],x1[k]+1):
                for gy in range(y0[k],y1[k]+1): s.grid.setdefault((gx,gy),[]).append(k)
        s.big=np.array(big,np.int64)
    def hit(s,o,d,L):
        """any triangle hit by the segment o + t d, 0 < t < L (d unit)"""
        e=o+d*L;ks=set()
        for q in (o,e):
            ks.update(s.grid.get((int(np.floor(q[0]/s.cell)),int(np.floor(q[1]/s.cell))),()))
        idx=np.array(sorted(ks),np.int64)
        if len(s.big): idx=np.r_[idx,s.big]
        if not len(idx): return False
        T=s.T[idx];a=T[:,0];e1=T[:,1]-a;e2=T[:,2]-a
        pv=np.cross(d,e2);det=(e1*pv).sum(1);ok=np.abs(det)>1e-9;inv=np.where(ok,1.0/np.where(ok,det,1),0)
        tv=o-a;u=(tv*pv).sum(1)*inv;qv=np.cross(tv,e1);v=(qv*d).sum(1)*inv;t=(qv*e2).sum(1)*inv
        return bool((ok&(u>=0)&(v>=0)&(u+v<=1)&(t>0)&(t<L)).any())
def _covered(occ,Pw,I):
    tri=Pw[I.reshape(-1,3)].astype(np.float64);out=np.zeros(len(tri),bool)
    for k,t in enumerate(tri):
        n=np.cross(t[1]-t[0],t[2]-t[0]);ln=np.linalg.norm(n)
        if ln<1e-6: out[k]=True;continue
        n/=ln;c=t.mean(0);out[k]=all(occ.hit(q+n*2.0,-n,COVER_CM+2.0) for q in [c]+[c+(v-c)*0.95 for v in t]+[c+((t[i]+t[(i+1)%3])*0.5-c)*0.95 for i in range(3)])
    return out
def _fill(path):
    """the texture with its transparent area filled from the surrounding colour (push-pull), alpha opaque"""
    im=np.asarray(Image.open(path).convert('RGBA')).astype(np.float32)/255;w=(im[:,:,3]>0.5).astype(np.float32)
    lv=[(im[:,:,:3]*w[...,None],w)]
    while min(lv[-1][1].shape)>1:
        c,ww=lv[-1];h2,w2=max(1,c.shape[0]//2),max(1,c.shape[1]//2)
        c=c[:h2*2,:w2*2].reshape(h2,2,w2,2,3).sum((1,3));ww=ww[:h2*2,:w2*2].reshape(h2,2,w2,2).sum((1,3));lv.append((c,ww))
    c,ww=lv[-1];col=c/np.maximum(ww,1e-6)[...,None]
    for c,ww in reversed(lv[:-1]):
        up=np.repeat(np.repeat(col,2,0),2,1);up=np.pad(up,((0,c.shape[0]-up.shape[0]),(0,c.shape[1]-up.shape[1]),(0,0)),mode='edge')[:c.shape[0],:c.shape[1]]
        a=np.clip(ww,0,1)[...,None];col=np.where(ww[...,None]>0,c/np.maximum(ww,1e-6)[...,None],0)*a+up*(1-a)
    out=np.dstack([np.where(w[...,None]>0,im[:,:,:3],col),np.ones_like(w)])
    return Image.fromarray((np.clip(out,0,1)*255+0.5).astype(np.uint8),'RGBA')
WORLD_FX=int(os.environ.get('GITS_WORLD_FX','1'))   # bake the map's world effects (world_fx.py) into the .gmap
def _texprop(h,po):
    d=h['d'];B=h['blocks']
    ne=struct.unpack_from('<I',d,po+4)[0];o=po+8+4*ne+4
    flags,count=struct.unpack_from('<HI',d,o);o+=6;out={}
    for i in range(count):
        has=d[o];o+=1
        if not has: continue
        src,mf,an,ht=struct.unpack_from('<iHHB',d,o);o+=9
        tt=None
        if ht: tt=struct.unpack_from('<5fI2f',d,o);o+=32
        fn=S(h,struct.unpack_from('<I',d,B[src][1]+13)[0]) if 0<=src<len(B) and B[src][0]=='NiSourceTexture' else None
        out[SLOTS[i] if i<len(SLOTS) else 'slot%d'%i]=dict(file=fn,uvset=mf&0xff,tt=tt)
    return out
def _prop(h,k):
    d=h['d'];t,o,s=h['blocks'][k]
    if t=='NiTexturingProperty': return t,_texprop(h,o)
    ne=struct.unpack_from('<I',d,o+4)[0];q=o+8+4*ne+4
    if t=='NiAlphaProperty':
        f,th=struct.unpack_from('<HB',d,q);return t,dict(blend=f&1,src=(f>>1)&15,dst=(f>>5)&15,test=(f>>9)&1,func=(f>>10)&7,thr=th)
    if t in('NiZBufferProperty','NiVertexColorProperty','NiStencilProperty','NiSpecularProperty'):
        f=struct.unpack_from('<H',d,q)[0]
        if t=='NiZBufferProperty': return t,dict(test=f&1,write=(f>>1)&1)
        if t=='NiVertexColorProperty': return t,dict(src=(f>>4)&3)
        if t=='NiStencilProperty': return t,dict(draw=(f>>10)&3)
        return t,dict(raw=f)
    if t=='NiMaterialProperty':
        v=struct.unpack_from('<14f',d,q);return t,dict(amb=v[0:3],dif=v[3:6],spec=v[6:9],emi=v[9:12],gloss=v[12],alpha=v[13])
    return t,{}
def _extra(h,k):
    d=h['d'];t,o,s=h['blocks'][k];nm=S(h,struct.unpack_from('<I',d,o)[0])
    if t=='NiFloatExtraData': return nm,struct.unpack_from('<f',d,o+4)[0]
    if t=='NiStringExtraData': return nm,S(h,struct.unpack_from('<I',d,o+4)[0])
    if t in('NiBooleanExtraData',): return nm,d[o+4]
    if t=='NiIntegerExtraData': return nm,struct.unpack_from('<i',d,o+4)[0]
    return nm,None
def _decode(h,a):
    """all vertex streams of a NiMesh (handles interleaved streams, u16/u32 indices, several regions)"""
    d=h['d'];B=h['blocks'];out={}
    for st in a['streams']:
        ds=stream(d,B[st['ref']][1]);data=ds['data']
        elems=[];off=0
        for f in ds['fmt']:
            cnt,sz,ty=(f>>16)&0xff,(f>>8)&0xff,f&0xff
            dt='<f4' if sz==4 and ty in(0x34,0x35,0x36,0x37,0x38) else '<u4' if sz==4 else '<u2' if sz==2 else 'u1'
            elems.append((cnt,sz,dt,off));off+=cnt*sz
        stride=off;n=len(data)//stride if stride else 0
        raw=np.frombuffer(data,np.uint8)[:n*stride].reshape(n,stride)
        for (sem,idx),(cnt,sz,dt,eo) in zip(st['comps'],elems):
            col=np.ascontiguousarray(raw[:,eo:eo+cnt*sz]).view(dt).reshape(n,cnt)
            if dt=='u1': col=col.astype('f4')/255.0
            out[(S(h,sem),idx)]=(col,ds['regs'],st['rmap'])
    return out
def _texeffect(h,k):
    """b67: NiTextureEffect (30.2): NiAVObject, switch state, affected node count (0 here), projection 3x3 + translation,
    filtering u32, max anisotropy u16, clamping, type (2 = environment map), coordinate generation (2 = sphere map), source"""
    d=h['d'];B=h['blocks'];t,o,s=B[k];a,p=avobj(d,o);p+=1+4+36+12
    filt,aniso,clamp,ty,cg,src=struct.unpack_from('<IHIIIi',d,p)
    fn=S(h,struct.unpack_from('<I',d,B[src][1]+13)[0]) if 0<=src<len(B) and B[src][0]=='NiSourceTexture' else None
    return dict(type=ty,cg=cg,file=fn)
_envstat={}
def _imgstat(path):
    """mean luminance and 99th percentile of a picture (None if it can't be read)"""
    if path in _envstat: return _envstat[path]
    try: a=np.asarray(Image.open(path).convert('RGB'),np.float32)/255;l=a.mean(2);r=(float(l.mean()),float(np.percentile(l,99)))
    except Exception: r=None
    _envstat[path]=r;return r
def _vispath(d,fn):
    """a file of MAPVIS env / gloss by its NIF name (any case)"""
    if not fn or not os.path.isdir(d): return None
    b=os.path.basename(fn.replace('\\','/')).lower();p=os.path.join(d,b)
    return p if os.path.exists(p) else None
def scene(path):
    """[(name, path, props, extra, streams, world matrix)] for every NiMesh, properties inherited down the graph"""
    h=load(path);d=h['d'];B=h['blocks'];res=[]
    def walk(k,M,props,pth):
        t,o,s=B[k]
        if t=='NiNode': a=node(d,o)
        elif t=='NiMesh': a=mesh(d,o)
        else: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];W=M@L
        P=dict(props)
        for p in a['props']:
            if 0<=p<len(B): pt,pv=_prop(h,p);P[pt]=pv
        nm=S(h,a['name']) or ''
        if t=='NiNode':
            env=P.get('_env')   # b67: an environment map hung on this node reaches every mesh below it
            _,q=avobj(d,o);nc=struct.unpack_from('<I',d,q)[0];q+=4+4*nc;ne=struct.unpack_from('<I',d,q)[0]
            for r in struct.unpack_from('<%dI'%ne,d,q+4):
                if 0<=r<len(B) and B[r][0]=='NiTextureEffect':
                    te=_texeffect(h,r)
                    if te['type']==2 and te['file']: env=te['file']
            P['_env']=env
            for c in a['children']: walk(c,W,P,pth+'/'+nm)
            return
        X={}
        for e in a['extra']:
            if 0<=e<len(B): k2,v=_extra(h,e);X[k2]=v
        res.append(dict(name=nm,path=pth+'/'+nm,props=P,extra=X,streams=_decode(h,a),W=W,nsub=a['nsub'],env=P.get('_env')))
    walk(0,np.eye(4),{},'')
    return res
# b69: each mip level from the top one with Lanczos (b1-b68: a box of the level above, which blurs every level a little
# more than the last: the floors and walls a few metres out went soft sooner); the alpha channel stays an area average
# (box from the top: the alpha-tested grates and leaves keep their coverage). GITSFA_MIPF=box: the old mips.
MIPF=os.environ.get('GITSFA_MIPF','lanczos')
# b69: the lightmaps at the original's size (2048; b1-b68 1024: shadow edges and contact shading on the floors went soft)
LMMAX=int(os.environ.get('GITSFA_LMMAX','2048'))
def _mip_down(top,prev,w,h):
    if MIPF=='box': return prev.resize((w,h),Image.BOX)
    t=top.convert('RGBA');rgb=t.convert('RGB').resize((w,h),Image.LANCZOS,reducing_gap=None);a=t.getchannel('A').resize((w,h),Image.BOX)
    rgb.putalpha(a);return rgb
class TexStore:
    def __init__(s,texmax): s.texs=[];s.ids={};s.texmax=texmax
    @staticmethod
    def _mips(im,fmt):
        import etcpak
        out=[];w,h=im.size;top=im
        while True:
            a=np.ascontiguousarray(np.asarray(im.convert('RGBA')))
            out.append(etcpak.compress_to_dxt1(a.tobytes(),w,h) if fmt==1 else etcpak.compress_to_dxt5(a.tobytes(),w,h))
            if w<=4 or h<=4: break
            w//=2;h//=2;im=_mip_down(top,im,w,h)
        return out
    def raw(s,key,rgba):
        if key in s.ids: return s.ids[key]
        s.texs.append((4,4,0,[bytes(rgba)*16]));s.ids[key]=len(s.texs)-1;return s.ids[key]
    def add(s,key,path,maxs,alpha=False,drop_black=False,img=None):
        k=(key,alpha)
        if k in s.ids: return s.ids[k]
        try: im=img if img is not None else Image.open(path).convert('RGBA')
        except Exception: s.ids[k]=-1;return -1
        if drop_black and np.asarray(im)[:,:,:3].max()<12: s.ids[k]=-1;return -1
        w,h=im.size;sc=min(1,maxs/max(w,h));w=max(4,int(w*sc));h=max(4,int(h*sc));w=1<<(w.bit_length()-1);h=1<<(h.bit_length()-1)
        im=im.resize((w,h),Image.LANCZOS)
        fmt=5 if alpha and np.asarray(im)[:,:,3].min()<250 else 1
        s.texs.append((w,h,fmt,s._mips(im,fmt)));s.ids[k]=len(s.texs)-1;return s.ids[k]
# b61 (user: textures as good as the Vita allows): the base textures that need it most come at HIMAX (512, the original's
# size) instead of TEXMAX (256), within HIBUDGET bytes of DXT more per map. Need: a texture's texel density at TEXMAX on
# the map's own surfaces (texels per metre from the triangles' world and UV areas, opaque and alpha-tested meshes) against
# HIDENS (the pixels per metre of a surface 1.5 m away at 960 x 544, 70 deg: ~260), weighted by the area it covers.
HIMAX=int(os.environ.get('GITSFA_TEXHI','512'));HIBUDGET=int(float(os.environ.get('GITSFA_TEXHI_MB','10'))*1048576);HIDENS=260.0
def hi_plan(MAPDIR,meshes,TEXMAX,verbose=True):
    def tex_path(b): return MAPDIR+'/tex/'+os.path.splitext(os.path.basename(b.replace('\\','/')))[0].lower()+'.png'
    acc={}
    for m in meshes:
        P=m['props'];Sx=m['streams'];tp=P.get('NiTexturingProperty',{});b=tp.get('base',{}).get('file')
        if not b or ('POSITION',0) not in Sx or ('INDEX',0) not in Sx or 'umbraalpha' in m['path'].lower(): continue
        al=P.get('NiAlphaProperty')
        if al and al['blend']: continue
        uv=Sx.get(('TEXCOORD',tp.get('base',{}).get('uvset',0)),Sx.get(('TEXCOORD',0),(None,)))[0]
        if uv is None: continue
        I=Sx[('INDEX',0)][0].ravel().astype(np.int64).reshape(-1,3);Pos=Sx[('POSITION',0)][0]
        Pw=(np.c_[Pos,np.ones(len(Pos))]@m['W'].T)[:,:3]/100.0   # metres
        tri=Pw[I];wa=0.5*np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
        tt=tp.get('base',{}).get('tt');u=uv[:,:2].astype('f8')
        if tt is not None: u=u*np.array(tt[2:4])
        tu=u[I];ua=0.5*np.abs((tu[:,1,0]-tu[:,0,0])*(tu[:,2,1]-tu[:,0,1])-(tu[:,2,0]-tu[:,0,0])*(tu[:,1,1]-tu[:,0,1]))
        k=b.lower();a=acc.setdefault(k,[0.0,0.0,bool(al and al['test'])]);a[0]+=wa.sum();a[1]+=ua.sum()
    cand=[]
    for k,(wa,ua,test) in acc.items():
        if wa<0.5 or ua<=0: continue
        p=tex_path(k)
        try: w,h=Image.open(p).size
        except Exception: continue
        if max(w,h)<=TEXMAX: continue   # (nothing bigger to give)
        d=TEXMAX*np.sqrt(ua/wa)          # texels per metre at TEXMAX
        if d>=HIDENS: continue
        need=wa*min(4.0,HIDENS/d-1)      # area x how far short it falls
        extra=(min(HIMAX,max(w,h))**2-TEXMAX**2)*(1.0 if test else 0.5)*4/3
        cand.append((need/extra,need,extra,k,d,wa))
    cand.sort(reverse=True);pick=set();used=0
    for r,need,extra,k,d,wa in cand:
        if used+extra>HIBUDGET: continue
        pick.add(k);used+=extra
    if verbose: print('  textures at %d px: %d of %d candidates (%d base textures on opaque / alpha-tested surfaces), %.1f MB more'%(HIMAX,len(pick),len(cand),len(acc),used/1048576))
    return pick
# b69 (user, after the b68 device run: floors "really blurry"; "use better textures for everything you can"): a second
# tier: of the textures at 512, those whose original is 1024 (333 base textures over the 15 big maps; extract tex1024 rows)
# come at 1024 where their density at 512 still falls short of HIDENS, the most short first, within HI2BUDGET more per map.
HI2MAX=int(os.environ.get('GITSFA_TEXHI2','1024'));HI2BUDGET=int(float(os.environ.get('GITSFA_TEXHI2_MB','8'))*1048576)
def hi2_plan(MAPDIR,meshes,HI,verbose=True):
    if HI2MAX<=HIMAX or HI2BUDGET<=0 or not HI: return set()
    def tex_path(b): return MAPDIR+'/tex/'+os.path.splitext(os.path.basename(b.replace('\\','/')))[0].lower()+'.png'
    acc={}
    for m in meshes:
        P=m['props'];Sx=m['streams'];tp=P.get('NiTexturingProperty',{});b=tp.get('base',{}).get('file')
        if not b or b.lower() not in HI or ('POSITION',0) not in Sx or ('INDEX',0) not in Sx: continue
        uv=Sx.get(('TEXCOORD',tp.get('base',{}).get('uvset',0)),Sx.get(('TEXCOORD',0),(None,)))[0]
        if uv is None: continue
        I=Sx[('INDEX',0)][0].ravel().astype(np.int64).reshape(-1,3);Pos=Sx[('POSITION',0)][0]
        Pw=(np.c_[Pos,np.ones(len(Pos))]@m['W'].T)[:,:3]/100.0
        tri=Pw[I];wa=0.5*np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
        tt=tp.get('base',{}).get('tt');u=uv[:,:2].astype('f8')
        if tt is not None: u=u*np.array(tt[2:4])
        tu=u[I];ua=0.5*np.abs((tu[:,1,0]-tu[:,0,0])*(tu[:,2,1]-tu[:,0,1])-(tu[:,2,0]-tu[:,0,0])*(tu[:,1,1]-tu[:,0,1]))
        al=P.get('NiAlphaProperty');k=b.lower();a=acc.setdefault(k,[0.0,0.0,bool(al and al['test'])]);a[0]+=wa.sum();a[1]+=ua.sum()
    cand=[]
    for k,(wa,ua,test) in acc.items():
        if wa<0.5 or ua<=0: continue
        try: w,h=Image.open(tex_path(k)).size
        except Exception: continue
        if max(w,h)<=HIMAX: continue
        d=HIMAX*np.sqrt(ua/wa)
        if d>=HIDENS: continue
        need=wa*min(4.0,HIDENS/d-1);extra=(min(HI2MAX,max(w,h))**2-HIMAX**2)*(w*h/max(w,h)**2)*(1.0 if test else 0.5)*4/3
        cand.append((need/extra,need,extra,k))
    cand.sort(reverse=True);pick=set();used=0
    for r,need,extra,k in cand:
        if used+extra>HI2BUDGET: continue
        pick.add(k);used+=extra
    if verbose: print('  textures at %d px: %d of %d candidates (originals 1024 px, short of %.0f texels/m at 512), %.1f MB more'%(HI2MAX,len(pick),len(cand),HIDENS,used/1048576))
    return pick
HI_DECAL={'urban':('leaf_decal_01_d','sewer_decal_01_d','skidmarka_jm_d','crosswalk')}   # b105: blended decals at HIMAX (build)
def build(MAPDIR,OUTD,NAME,TEXMAX=256,GLOWMAX=128,verbose=True):
    meshes=scene(MAPDIR+'/world_lmap.nif');EXP=EXPOSURE.get(NAME,1.0)
    T=TexStore(TEXMAX);HI=hi_plan(MAPDIR,meshes,TEXMAX,verbose) if HIMAX>TEXMAX and HIBUDGET>0 else set()
    HI2=hi2_plan(MAPDIR,meshes,HI,verbose)
    # b105 (user: "the blurry textures i mentioned in urban"): hi_plan only counts opaque and alpha-tested surfaces, so every
    # BLENDED decal came at TEXMAX. Urban's ground decals - the leaf litter (4.3 m to a tile: 0.6 texels a centimetre at 256),
    # the sewer lids, the tyre marks, the crossing's stripes - at the original's 512 (+1.0 MB of DXT5 in that map).
    # (tools/mapvis/hi_decal.py put the same four into the map built before this rule: the same pixels, mips and encoder.)
    HI|=set(k for k in (_b.lower() for _b in (m['props'].get('NiTexturingProperty',{}).get('base',{}).get('file') for m in meshes) if _b) if os.path.splitext(os.path.basename(k.replace('\\','/')))[0] in HI_DECAL.get(NAME,()))
    def _tmax(k): return HI2MAX if k in HI2 else HIMAX if k in HI else TEXMAX
    NEUTRAL=T.raw('neutral',(128,128,128,255));WHITE=T.raw('white',(255,255,255,255))
    recs=[];skipped=[];stats=dict(blend=0,test=0,two=0,nozw=0,glow=0,vcol=0,nolm=0,notex=0,lmfix=0)
    def _base(m): return m['props'].get('NiTexturingProperty',{}).get('base',{}).get('file')
    cutm=[_is_cut(m['props'],_base(m),MAPDIR) for m in meshes];occ=None
    if any(cutm):   # what can be seen behind the cut-out decals: every other opaque / alpha-tested mesh
        ot=[]
        for m,cm in zip(meshes,cutm):
            al=m['props'].get('NiAlphaProperty');Sx=m['streams']
            if cm or (al and al['blend']) or ('POSITION',0) not in Sx or ('INDEX',0) not in Sx or 'umbraalpha' in m['path'].lower(): continue
            Pp=(np.c_[Sx[('POSITION',0)][0],np.ones(len(Sx[('POSITION',0)][0]))]@m['W'].T)[:,:3];ot.append(Pp[Sx[('INDEX',0)][0].ravel().astype(np.int64).reshape(-1,3)])
        occ=_Occ(np.concatenate(ot))
    for m,cm in zip(meshes,cutm):
        P=m['props'];X=m['extra'];Sx=m['streams'];tp=P.get('NiTexturingProperty',{})
        b0=(tp.get('base',{}).get('file') or '').lower()
        if 'umbraalpha' in m['path'].lower() and (not b0 or b0.startswith('base_d')): skipped.append(m['name']);continue
        if ('POSITION',0) not in Sx or ('INDEX',0) not in Sx: continue
        Pos,_,_=Sx[('POSITION',0)];I=Sx[('INDEX',0)][0].ravel().astype(np.int64)
        if m['nsub']>1 and verbose: print('  note: %s has %d submeshes'%(m['path'],m['nsub']))
        W=m['W'];Pw=(np.c_[Pos,np.ones(len(Pos))]@W.T)[:,:3].astype('f4');n=len(Pw)
        # material
        al=P.get('NiAlphaProperty');mat=P.get('NiMaterialProperty',dict(dif=(1,1,1),emi=(0,0,0),alpha=1.0,amb=(1,1,1)))
        mode=0;ref=0.0;flags=F_REB
        if al and al['blend']:
            mode=2;ref=al['thr']/255.0 if al['test'] else 0.0
            if al['dst']==0: flags|=F_ADD
        elif al and al['test']: mode=1;ref=max(al['thr'],1)/255.0
        if P.get('NiStencilProperty',{}).get('draw')==3: flags|=F_TWO
        if not P.get('NiZBufferProperty',{'write':1})['write']: flags|=F_NOZW
        base=tp.get('base',{}).get('file')
        if cm: mode=1;ref=0.5;stats['cutout']=stats.get('cutout',0)+1
        uv0=Sx.get(('TEXCOORD',tp.get('base',{}).get('uvset',0)),Sx.get(('TEXCOORD',0),(None,)))[0]
        if uv0 is None: uv0=np.zeros((n,2),'f4')
        tt=tp.get('base',{}).get('tt')
        if tt is not None:   # (translation u v, scale u v, rotation, method, centre u v): about the centre, as 3ds Max writes it
            c=np.array(tt[6:8]);uv0=((uv0-c)*np.array(tt[2:4])+c+np.array(tt[0:2])).astype('f4')
        tint=[1.0,1.0,1.0,1.0]
        if base:
            tex=T.add(base.lower(),MAPDIR+'/tex/'+os.path.splitext(os.path.basename(base.replace('\\','/')))[0].lower()+'.png',_tmax(base.lower()),alpha=mode!=0)
            if tex<0: print('  missing texture',base,'on',m['path']);tex=WHITE
        else:
            tex=WHITE;stats['notex']+=1;tint=[min(1.0,mat['dif'][i]*0.5+mat['emi'][i]) for i in range(3)]+[1.0]
        if mode==2: tint[3]=mat['alpha']
        if not (mode==2 and flags&F_NOZW):   # holographic lines / glows (blended, no depth writes) keep their brightness
            tint=[tint[0]*EXP,tint[1]*EXP,tint[2]*EXP,tint[3]]
        # lightmap (Beast): the detail (or dark) slot, with its own UV set
        lm=-1;uv1=None
        for sl in ('detail','dark'):
            e=tp.get(sl)
            if e and e['file'] and 'lightmapping' in e['file'].lower():
                if ('TEXCOORD',e['uvset']) in Sx:
                    uv1=Sx[('TEXCOORD',e['uvset'])][0];lm=T.add('LM:'+e['file'],MAPDIR+'/'+os.path.splitext(e['file'].replace('\\','/'))[0]+'.png',LMMAX)
                    if e['uvset']!=1: stats['lmfix']+=1
                break
        if lm<0 or uv1 is None: lm=NEUTRAL;uv1=None;stats['nolm']+=1
        # glow (emissive) map
        glow=-1;glowk=0.0;g=tp.get('glow',{}).get('file')
        if g:
            gp=os.path.join(GLOWDIR,os.path.basename(g.replace('\\','/')))
            if not os.path.exists(gp):
                cand=[f for f in os.listdir(GLOWDIR) if f.lower()==os.path.basename(g.replace('\\','/')).lower()] if os.path.isdir(GLOWDIR) else []
                gp=os.path.join(GLOWDIR,cand[0]) if cand else None
            if gp:
                glow=T.add('G:'+g.lower(),gp,GLOWMAX,drop_black=True)
                if glow>=0: glowk=float(X.get('Glow_Value',1.0));stats['glow']+=1
        # b67: environment map (the node's NiTextureEffect x the gloss map's mean), kept where it shows (ENV_CMIN)
        env=-1;envk=0.0;Nw=None
        # (not on alpha-tested meshes: leaves, grates and fences took a sheen the PC's footage doesn't show)
        if ENV_ON and m.get('env') and ('NORMAL',0) in Sx and mode!=1 and not (glow>=0 and mode!=0):
            ep=_vispath(ENVDIR,m['env']);gfile=tp.get('gloss',{}).get('file');gp=_vispath(GLOSSDIR,gfile) if gfile else None
            es=_imgstat(ep) if ep else None;gs=_imgstat(gp) if gp else ((1.0,1.0) if not gfile else None)
            if es and gs and gs[0]*es[1]>=ENV_CMIN:
                env=T.add('ENV:'+os.path.basename(ep),ep,ENVMAX);envk=gs[0]*ENV_K
                if os.environ.get('GITS_ENV_DEBUG'): print('ENVDBG\t%d\t%s\t%s\t%s\t%.3f\t%.3f\t%.3f'%(len(I)//3,base,os.path.basename(ep),gfile,gs[0],es[0],es[1]))
                if env>=0:
                    Nw=Sx[('NORMAL',0)][0][:,:3]@W[:3,:3].T;Nw=(Nw/np.maximum(np.linalg.norm(Nw,axis=1,keepdims=True),1e-6)).astype('f4')
                    stats['env']=stats.get('env',0)+1;stats['env_tris']=stats.get('env_tris',0)+len(I)//3
                else: env=-1;envk=0.0
            elif es and gs: stats['env_weak']=stats.get('env_weak',0)+1
        # vertex colours (only when the material uses them and they are not all white)
        vc=None
        if P.get('NiVertexColorProperty',{}).get('src',0)>=1 and ('COLOR',0) in Sx:
            c=Sx[('COLOR',0)][0][:,:4]
            if c.min()<0.99: vc=np.clip(c,0,1);flags|=F_VCOL;stats['vcol']+=1
        if vc is not None and env>=0: env=-1;envk=0.0;Nw=None;stats['env']-=1   # (b67: no env program with vertex colour)
        if env>=0: flags|=F_ENV
        parts=[(mode,tex,ref,I)]
        if cm:   # alpha-tested where something is behind the decal, opaque with the filled texture elsewhere
            cov=_covered(occ,Pw,I);Ic=I.reshape(-1,3)
            ftex=T.add('FILL:'+base.lower(),None,_tmax(base.lower()),img=_fill(MAPDIR+'/tex/'+os.path.splitext(os.path.basename(base.replace('\\','/')))[0].lower()+'.png')) if (~cov).any() else tex
            parts=[(1,tex,0.5,Ic[cov].ravel()),(0,ftex,0.0,Ic[~cov].ravel())]
            stats['cut_tris']=stats.get('cut_tris',0)+int(cov.sum());stats['cut_opaque_tris']=stats.get('cut_opaque_tris',0)+int((~cov).sum())
        for mode,tex,ref,I in parts:
            if not len(I): continue
            V=np.c_[Pw,uv0[:n],(uv1[:n] if uv1 is not None else uv0[:n])].astype('<f4')
            if Nw is not None: V=np.c_[V,Nw[:n]].astype('<f4')   # b67: the normals ride along through the rebase
            V,I2=rebase(V,I)                      # small texture coordinates (see uvrebase.py)
            if uv1 is None: V[:,5:7]=V[:,3:5]     # neutral lightmap: any small coordinate will do
            if Nw is not None:                    # b67: packed into one 4-byte slot (int8 x 3 + 0)
                q=np.zeros((len(V),4),'i1');q[:,:3]=np.clip(np.round(V[:,7:10]*127),-127,127);V=np.c_[V[:,:7],q.view('<f4').reshape(-1,1)]
            if vc is not None:
                # rebase may duplicate vertices: carry the colours along by matching positions
                from scipy.spatial import cKDTree
                idx=cKDTree(Pw).query(V[:,:3])[1];col=(vc[idx]*255+0.5).astype('u1')
                V=np.c_[V,col.view('<f4').reshape(-1,1)]
            # glossy floors: blended at a uniform alpha >= 0.7 over a planar reflection the Vita doesn't have; drawn opaque at
            # colour x alpha instead of over the fog colour (Tutorial: Object557, floor_01_diffuse at alpha 0.74)
            if mode==2 and tex!=WHITE and T.texs[tex][2]==1 and tint[3]>=0.7 and not (flags&F_NOZW):
                tri=Pw[I.reshape(-1,3)];nz=np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]);ar=np.linalg.norm(nz,axis=1)
                if ar.sum()>0 and (np.abs(nz[:,2])).sum()/ar.sum()>0.95 and ar.sum()*0.5>500*500:
                    tint=[tint[0]*tint[3],tint[1]*tint[3],tint[2]*tint[3],1.0];mode=0;stats['floor']=stats.get('floor',0)+1
            if mode==1: stats['test']+=1
            if mode==2: stats['blend']+=1
            if flags&F_TWO: stats['two']+=1
            if flags&F_NOZW: stats['nozw']+=1
            for Vp,Ip in split16(V,I2): recs.append((mode,tex,lm,glow,flags,ref,glowk,tint,Vp.astype('<f4'),Ip.astype('<u2'),m['path'],env,envk))
    # world effects placed by the map's WorldEffectFx.wff (screens, signs, markers), posed as at t = 0 -- see world_fx.py
    nfx=0;fxb={};fxi=[]
    if WORLD_FX:
        import world_fx
        for m,off,src in world_fx.effect_meshes(os.path.basename(MAPDIR.rstrip('/'))[4:],verbose=verbose):
            P=m['props'];Sx=m['streams'];tp=P.get('NiTexturingProperty',{})
            if ('INDEX',0) not in Sx: continue
            Pos=Sx[('POSITION',0)][0];I=Sx[('INDEX',0)][0].ravel().astype(np.int64)
            Pw=((np.c_[Pos,np.ones(len(Pos))]@m['W'].T)[:,:3]+off).astype('f4');n=len(Pw)
            al=P.get('NiAlphaProperty');mat=P.get('NiMaterialProperty',dict(dif=(1,1,1),emi=(0,0,0),alpha=1.0,amb=(1,1,1)))
            mode=0;ref=0.0;flags=F_REB
            if al and al['blend']:
                mode=2;ref=al['thr']/255.0 if al['test'] else 0.0
                if al['dst']==0: flags|=F_ADD
            elif al and al['test']: mode=1;ref=max(al['thr'],1)/255.0
            if P.get('NiStencilProperty',{}).get('draw')==3: flags|=F_TWO
            if not P.get('NiZBufferProperty',{'write':1})['write']: flags|=F_NOZW
            emi=np.array(mat['emi']);col=emi if emi.max()>0.01 else np.array(mat['dif'])
            tint=[float(min(1.0,c)) for c in col]+[float(mat['alpha']) if mode==2 else 1.0]
            if mode==2 and tint[3]<=0.004: continue          # fully faded out at t = 0
            b=tp.get('base',{});fn=b.get('file');uvs=Sx.get(('TEXCOORD',b.get('uvset',0)),Sx.get(('TEXCOORD',0),(None,)))[0]
            uv0=np.zeros((n,2),'f4') if uvs is None else uvs[:n].astype('f4')
            tt=b.get('tt')
            if tt is not None: c=np.array(tt[6:8]);uv0=((uv0-c)*np.array(tt[2:4])+c+np.array(tt[0:2])).astype('f4')
            if b.get('off'): uv0=uv0+np.array(b['off'],'f4')
            tpath=world_fx.find_tex(fn) if fn else None
            if fn and not tpath: print('  world fx texture missing',fn);continue
            vc=None
            if P.get('NiVertexColorProperty',{}).get('src',0)>=1 and ('COLOR',0) in Sx:
                c=np.clip(Sx[('COLOR',0)][0][:n,:4],0,1)
                if c.min()<0.99:
                    if mode==2: vc=c
                    else: tint=[tint[i]*float(c[:,i].mean()) for i in range(3)]+[tint[3]]
            fxi.append(dict(mode=mode,flags=flags,ref=ref,tint=tint,tpath=tpath,P=Pw,uv=uv0,I=I,vc=vc,src='fx:'+src+m['path']));nfx+=1
    # Atlas: blended effect quads with plain 0..1 texture coordinates share one 1024x1024 texture and carry their colour
    # as vertex colour, so a room full of screens is a handful of draws instead of hundreds.
    PAD=4;cand={}
    for it in fxi:
        if it['mode']==2 and it['tpath'] and it['uv'].min()>=-0.02 and it['uv'].max()<=1.02: cand.setdefault(it['tpath'],[]).append(it)
    def fit(t):
        w0,h0=Image.open(t).size;sc=min(1.0,128.0/max(w0,h0))
        return max(4,1<<(int(w0*sc).bit_length()-1)),max(4,1<<(int(h0*sc).bit_length()-1))
    order=sorted(cand,key=lambda t:(-fit(t)[1],-fit(t)[0]))
    for AT in (256,512,1024):                          # the smallest square atlas that holds them all
        rect={};x=y=rowh=0
        for tpath in order:
            w,h=fit(tpath)
            if x+w+2*PAD>AT: x=0;y+=rowh;rowh=0
            if y+h+2*PAD>AT: continue
            rect[tpath]=(x,y,w,h);x+=w+2*PAD;rowh=max(rowh,h+2*PAD)
        if len(rect)==len(order): break
    atlas=Image.new('RGBA',(AT,AT),(0,0,0,0))
    for tpath,(x,y,w,h) in list(rect.items()):
        im=Image.open(tpath).convert('RGBA').resize((w,h),Image.LANCZOS)
        atlas.paste(Image.fromarray(np.pad(np.asarray(im),((PAD,PAD),(PAD,PAD),(0,0)),mode='edge')),(x,y))
        rect[tpath]=((x+PAD)/AT,(y+PAD)/AT,w/AT,h/AT)
    atex=-1
    if rect:
        T.texs.append((AT,AT,5,T._mips(atlas,5)));atex=len(T.texs)-1;stats['atlas']=AT
    for it in fxi:
        Pw,uv0,I,vc,tint,mode,flags=it['P'],it['uv'],it['I'],it['vc'],list(it['tint']),it['mode'],it['flags'];n=len(Pw)
        if it['tpath'] in rect and it['mode']==2 and uv0.min()>=-0.02 and uv0.max()<=1.02:
            rx,ry,rw,rh=rect[it['tpath']];uv0=np.c_[rx+np.clip(uv0[:,0],0,1)*rw,ry+np.clip(uv0[:,1],0,1)*rh].astype('f4')
            col=np.tile(np.array(tint,'f4'),(n,1))
            if vc is not None: col=col*vc
            vc=col;tint=[1.0,1.0,1.0,1.0];tex=atex
        elif it['tpath']:
            tex=T.add('FX:'+os.path.basename(it['tpath']).lower(),it['tpath'],TEXMAX,alpha=mode!=0)
        else: tex=WHITE
        if vc is not None: flags|=F_VCOL
        V=np.c_[Pw,uv0,uv0].astype('<f4')
        if tex!=atex: V,I2=rebase(V,I);V[:,5:7]=V[:,3:5]
        else: I2=I
        if vc is not None:
            from scipy.spatial import cKDTree
            idx=cKDTree(Pw).query(V[:,:3])[1];V=np.c_[V,(np.clip(vc[idx],0,1)*255+0.5).astype('u1').view('<f4').reshape(-1,1)]
        # batched: same render state and the same 20 m cell -> one mesh
        c=V[:,:3].mean(0);key=(mode,tex,flags,round(it['ref'],4),tuple(round(v,4) for v in tint),int(c[0]//2000),int(c[1]//2000),int(c[2]//1000))
        fxb.setdefault(key,[]).append((V,I2,it['src']))
    nb=0
    for key,parts in fxb.items():
        mode,tex,flags,ref,tint=key[0],key[1],key[2],key[3],list(key[4]);Vs=[];Is=[];o=0
        for V,I2,_ in parts: Vs.append(V);Is.append(np.asarray(I2).ravel()+o);o+=len(V)
        Vc=np.concatenate(Vs);Ic=np.concatenate(Is)
        for Vp,Ip in split16(Vc,Ic): recs.append((mode,tex,NEUTRAL,-1,flags,ref,0.0,tint,Vp.astype('<f4'),Ip.astype('<u2'),parts[0][2]+' (+%d)'%(len(parts)-1),-1,0.0));nb+=1
    stats['fx']='%d meshes -> %d batches (%d textures in the atlas)'%(nfx,nb,len(rect)) if WORLD_FX else 0
    recs.sort(key=lambda r:(r[0],r[3]>=0,r[4]&F_VCOL,r[4]&F_ENV,r[1],r[2]))
    with open(OUTD+'/%s.gmap'%NAME,'wb') as f:
        f.write(b'GMAP'+struct.pack('<III',2,len(T.texs),len(recs)))
        for w,h,fmt,mm in T.texs:
            f.write(struct.pack('<HHBB',w,h,fmt,len(mm)))
            for b in mm: f.write(struct.pack('<I',len(b)));f.write(b)
        for mode,tex,lm,glow,flags,ref,glowk,tint,V,I,_,env,envk in recs:
            Pp=V[:,:3];f.write(struct.pack('<iiiIIII',tex,lm,glow,mode,flags,len(V),len(I)));f.write(struct.pack('<6f',ref,glowk,*tint))
            if flags&F_ENV: f.write(struct.pack('<if',env,envk))
            f.write(Pp.min(0).astype('<f4').tobytes()+Pp.max(0).astype('<f4').tobytes());f.write(V.tobytes());f.write(I.tobytes())
            if len(I)%2: f.write(b'\0\0')
    if verbose:
        print(NAME,'gmap v2: textures',len(T.texs),'meshes',len(recs),'tris',sum(len(r[9])//3 for r in recs),'MB %.1f'%(os.path.getsize(OUTD+'/%s.gmap'%NAME)/1e6),
              '| skipped %d occluder meshes'%len(skipped),'|',stats)
    return recs
if __name__=='__main__':
    MAPDIR,OUTD,NAME=sys.argv[1],sys.argv[2],sys.argv[3];TEXMAX=int(sys.argv[4]) if len(sys.argv)>4 else 256
    build(MAPDIR,OUTD,NAME,TEXMAX)
