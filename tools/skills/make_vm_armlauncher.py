# vm_armlauncher.gvm: Batou's ARM LAUNCHER first-person viewmodel (weapon\Armluncher\1st\Armluncher_1rd.nif: the launcher
# pod, Batou's right hand, the forearm skin that opens) with the game's own 1st-person clips (GIS_base_1st.nmb, family
# 'Armluncher', rig 38), decoded with our own decoders (/root/anim/py) and baked in camera space exactly as the weapon
# viewmodels (vm/make_vm_weapon.py: same rig->camera transform, eye = rig (0,0,156.93); the engine draws it with the
# hip projection of the weapons: fovy 49.75, offset (-2.37, 0.08, 0)).
# Meshes: 0 = the launcher (pod, rotor, armour shell), 1 = Batou's gloved hand, 2 = his forearm skin. The original dissolves the
# arm into the launcher (OpticamoDissolve.dds): the engine draws meshes 1-2 before the switch and mesh 0 after it (skills.h
# sk_draw_vm: swap_in 0.65 s, swap_out 0.35 s, with a short chrome flash), as in the footage (Electronics Center 37.3-38.6 s,
# 43.4-44.7 s).
# Clips: swap_in (= swap_end: the arm comes up and unfolds into the launcher, 1.37 s), idle, walk, run, fire, swap_out
# (= swap_start: the launcher folds back into the arm and drops, 1.03 s), jump, fall, land. Attachment 'muzzle' = the front
# of the pod in camera space at idle frame 0.
# b71: the PC's glow map (Armlancher_g.dds on uv set 1: the red lens, the pod's lights) baked into the pod's colour map (vm/gvmlib.py
# emissive_image; engine model.h MDF_EMIS), as the screenshots' glowing red lens.
# b78 (the user's screenshot + the Batou video's launcher footage, 2:25-2:37, 3:25-3:37, 12:13-12:21): only the sight's lens (the top
# barrel, under the visor) glows, and only while the launcher is up: it lights with a red ring flash as the sight locks in place,
# stays lit, and goes dark when the missiles leave (the other barrels' lenses stay dark glass). The colour map keeps the dark iris
# (the PC glow map's strips only); the lens is drawn by the engine (skills.h sk_draw_vm) as an additive disc on the sight's glass:
# attachments 'lens_c' / 'lens_n' (the glass's centre and normal in its bone's frame), 'lens_b' (bone index, glass radius) and
# three textures after the meshes' (lens_glow: the lit ring, additive; lens_ring: the power-on ring; lens_dark: the glass
# darkened, alpha-blended black), made here.
# b109: the lens from the game's effect files (armlauncher_load_1 / _load_2: see LENS_Q below): lens_glow = the ring at 1.26 glass
# radii with its bars and flares, lens_ring = the flash, lens_dark = the lit glass's dark red veil (no black disc any more);
# lens_b[2] = the glass's radius (the engine tells this model from b78's by it); 'lens2_c' / 'lens2_n' / 'lens2_b' = the second
# barrel's glass (Bone24, the game's Fire02), lit at Tier 2.
# usage: python3 make_vm_armlauncher.py [outdir]
import sys,os,io,json,contextlib
HERE=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,os.path.join(HERE,'..','..','vm'))
from vmw import *
OUT=sys.argv[1] if len(sys.argv)>1 else os.path.join(HERE,'..','..','build','data')
import gitspaths
A=gitspaths.dirp('ANIMBIN','GIS_base_1st_runtimeBinary');NMB=A+'GIS_base_1st.nmb'
D=gitspaths.dirp('SKRAW','weapon','Armluncher','1st');NIF=D+'Armluncher_1rd.nif'
CAM=np.array([0,0,156.93]);RC=np.array([[-1,0,0],[0,0,1],[0,1,0]],float)
CLIPS=[('swap_in','swap_end_',0),('idle','stand_idl',1),('walk','walk_forw',1),('run','stand_run',1),('fire','fire_base',0),
       ('swap_out','swap_star',0),('jump','jump_up_b',0),('fall','jump_fall',1),('land','jump_land',0)]
# (as the weapons: swap_end = the draw, swap_start = the holster)
MERGE={'Bone06':'Bone05','Bone09':'Bone08'}   # (lightest-weighted fingertips, see the build log)
TEX={'Armlancher_d.dds':1024,'1sthand_d.dds':512,'Armlancher_tran_d.dds':512}

def frames(rig,maps,key):
    f=[x for x in os.listdir(A) if x.startswith('base_Armluncher_1st_'+key)][0]
    h=hdr(A+f);C=decode_clip(h)
    with contextlib.redirect_stdout(io.StringIO()): best=choose_map3(h,C,maps,rig)
    if best: C['map']=best[1]['list']
    out=[]
    for i in range(C['nf']):
        if C['mode']==1: lp,lq=pose(rig,C,i);out.append(fk(rig['parent'],lp,lq))
        else: out.append(pose_world(rig,C,i))
    return out,f

# b71 (the user: "improve the appearance ... of batou's arm cannon trying to make it look closer to the screenshots"): the PC
# screenshots show both lenses of the cannon (the pod's front and the barrel's) glowing red: red glass over dark iris blades,
# a bright rim at the glass's edge. The PC data has no glow there (Armlancher_g lights only the yellow strips): the red is
# drawn from the screenshots, the blades from the colour map (its dark lines stay dark).
# b72 (the user: the red "looks to be rendered more like an oval ... instead of blending in seamlessly with the more circular
# geometry"): b71 drew an ellipse guessed in the texture (887, 192, 73 x 107 texels) - the glass is mapped non-uniformly
# (its half disc spans 815-887 x 50-337) and only its middle glowed, an oval inside the round lens. Now from the geometry:
# the glass = the flat discs of the pod mesh on the iris region of Armlancher_d (u 0.79-0.87, v 0.04-0.34), each disc's
# centre / plane / radius from its own vertices; every texel of those triangles gets its distance from the disc's centre in
# 3D (r = 0 centre .. 1 edge), so the glow is round on the lens whatever the texture mapping.
LENS_UV=(0.79,0.87,0.04,0.34)
def lens_discs(m):
    """the lens glass triangles of the pod mesh: [(tri indices (n,3) into P / UV, centre, normal, radius)]"""
    T=np.concatenate([m['I'][is_:is_+ic]+vs for (vs,vc),(is_,ic) in zip(m['vregs'],m['idxregs'])]).reshape(-1,3)
    P=np.asarray(m['P'],np.float64);c=np.mod(np.asarray(m['UV'])[T].mean(1),1.0)
    sel=np.nonzero((c[:,0]>LENS_UV[0])&(c[:,0]<LENS_UV[1])&(c[:,1]>LENS_UV[2])&(c[:,1]<LENS_UV[3]))[0]
    par=list(range(len(sel)));key={}
    def find(a):
        while par[a]!=a: par[a]=par[par[a]];a=par[a]
        return a
    for k,t in enumerate(sel):
        for v in T[t]:
            q=tuple(np.round(P[v],2))
            if q in key:
                a,b=find(k),find(key[q])
                if a!=b: par[a]=b
            else: key[q]=k
    comps={}
    for k in range(len(sel)): comps.setdefault(find(k),[]).append(sel[k])
    out=[]
    for ts in comps.values():
        if len(ts)<8: continue   # (2-triangle details next to the iris)
        V=P[T[ts].ravel()];ce=V.mean(0);_,S,Vt=np.linalg.svd(V-ce);n=Vt[2]
        if S[2]>0.01*S[0]: continue   # not flat: not a lens face
        d=V-ce;R=np.linalg.norm(d-np.outer(d@n,n),axis=1).max()
        out.append((T[ts],ce,n,R))
    return out,P,np.asarray(m['UV'],np.float64)
def lens_glow(tp,W,H,E,meshes):
    D=np.asarray(Image.open(tp).convert('RGB').resize((W,H),Image.BILINEAR)).astype(np.float64)/255;L=D@np.array([0.3,0.59,0.11])
    rr=np.full((H,W),-1.0)
    nd=0
    for m in meshes:
        discs,P,UV=lens_discs(m)
        for TT,ce,n,R in discs:
            nd+=1
            for t in TT:
                a=UV[t];c0=np.floor(a.mean(0));a=(a-c0)*[W,H]
                x0,y0=np.floor(a.min(0)).astype(int);x1,y1=np.ceil(a.max(0)).astype(int)
                x0=max(x0,0);y0=max(y0,0);x1=min(x1,W);y1=min(y1,H)
                if x1<=x0 or y1<=y0: continue
                gx,gy=np.meshgrid(np.arange(x0,x1)+0.5,np.arange(y0,y1)+0.5)
                dd=(a[1,1]-a[2,1])*(a[0,0]-a[2,0])+(a[2,0]-a[1,0])*(a[0,1]-a[2,1])
                if abs(dd)<1e-9: continue
                l0=((a[1,1]-a[2,1])*(gx-a[2,0])+(a[2,0]-a[1,0])*(gy-a[2,1]))/dd;l1=((a[2,1]-a[0,1])*(gx-a[2,0])+(a[0,0]-a[2,0])*(gy-a[2,1]))/dd;l2=1-l0-l1
                e=-0.75/max(abs(dd),1.0);ins=(l0>=e)&(l1>=e)&(l2>=e)
                if not ins.any(): continue
                q=l0[ins,None]*P[t[0]]+l1[ins,None]*P[t[1]]+l2[ins,None]*P[t[2]]-ce
                r=np.clip(np.linalg.norm(q-np.outer(q@n,n),axis=1)/R,0,1)
                yy,xx=(gy[ins]-0.5).astype(int),(gx[ins]-0.5).astype(int)
                rr[yy,xx]=np.where(rr[yy,xx]<0,r,np.minimum(rr[yy,xx],r))
    for _ in range(2):   # (the glass's own texels + 2 around it: mip and filter bleed stays red, not dark)
        cov=rr>=0;nb=rr.copy()
        for dy,dx in ((0,1),(0,-1),(1,0),(-1,0)):
            sh=np.roll(np.roll(rr,dy,0),dx,1);ok=(~cov)&(sh>=0)&(nb<0);nb[ok]=sh[ok]
        rr=nb
    cov=rr>=0;r=np.where(cov,rr,1)
    blades=np.clip(L/0.2,0.3,1)   # the iris blades' dark lines
    inner=(0.5+0.3*(1-r))*blades
    rim=np.clip((r-0.80)/0.12,0,1)*0.95
    e=np.where(cov,np.maximum(inner,rim),0)
    t=np.clip((r-0.80)/0.12,0,1)[:,:,None]
    hue=np.array([1.0,0.12,0.10])*(1-t)+np.array([1.0,0.40,0.38])*t
    G=hue*e[:,:,None]
    print('glow: %s + the screenshots\' red lenses: %d glass discs, %.2f %% of the texels'%(os.path.basename(tp),nd,100*cov.mean()),file=sys.stderr)
    return G if E is None else np.maximum(E,G)

# b109: the lens textures, from the game's own effect (effect\cyberbrain\armlauncher_load_1.nif / _load_2.nif, read with
# tools/fx/nifps.py; EP16 3:13 and 8:30 for the look of the lit glass). b78 had fitted them by eye to older footage: the ring at
# 1.47 x the glass radius, a black disc inside it. The effect's parts, in cm (the glass disc is 1.0 cm in radius):
#   glow     an annulus 1.13 - 1.42 round the glass with red_ring.dds (a thin red line - at 1.26 - with a white-hot bar where its
#            two halves meet: above and below on the sight, to the sides on the second barrel)
#   Plane01/02 (03/04)  two flares, eye_line.dds (a point with a faint streak), 1.28 from the middle, on those bars
#   Plane03 (s)         a flash, blood_dot.dds (a red disc, see-through in the middle, a brighter rim), 2.5 in radius, growing 3.4 x
# The engine's quad spans LENS_Q x the glass radius around the glass's centre, in its plane, its v axis toward the camera's up
# (the second barrel's turned a quarter). Drawn here (the effect's own textures are not among the builder's inputs).
LENS_Q=2.0
def _rho(N=128):
    y,x=np.mgrid[0:N,0:N]+0.5;return np.hypot(x-N/2,y-N/2)/(N/2),np.arctan2(N/2-y,x-N/2)
def lens_tex(N=256):
    """additive: the ring, its bars and flares, a short bloom"""
    r,a=_rho(N);g=1.26/LENS_Q;y,x=np.mgrid[0:N,0:N]+0.5;X=(x-N/2)/(N/2)*LENS_Q;Y=(N/2-y)/(N/2)*LENS_Q   # (cm / glass radii)
    ring=np.exp(-((r-g)/0.016)**2)
    da=np.abs(np.abs(a)-np.pi/2);bar=np.exp(-(da/0.16)**2)*np.exp(-((r-g)/0.034)**2)   # the bars: thicker, +-9 degrees
    fl=0
    for s_ in (1,-1):   # the flares: a point, a halo, a streak along the ring's tangent
        dx,dy=X,Y-s_*1.28;d2=dx*dx+dy*dy
        fl=fl+np.exp(-d2/0.012)+0.35*np.exp(-d2/0.10)+0.22*np.exp(-(dy/0.035)**2)*np.exp(-(dx/0.75)**2)
    bloom=0.30*np.exp(-np.abs(r-g)/0.055)*(1+0.9*np.exp(-(da/0.5)**2))
    red=np.array([1.0,0.13,0.10]);core=np.array([1.0,0.80,0.76])
    k=ring*0.95+bar*1.3+fl*0.9
    c=np.clip(k,0,1.6)[...,None]*red+(np.clip(k-0.55,0,1)*1.6)[...,None]*(core-red)*0.75+bloom[...,None]*np.array([0.95,0.10,0.08])
    c*=np.clip((1-r)/0.05,0,1)[...,None]
    return (np.clip(c,0,1)*255+0.5).astype(np.uint8)
def dark_tex(N=128):
    """alpha-blended (the lit glass): a dark red veil inside the ring - EP16: (76..95, 35..41, 37..42) there, the glass's own
    rings still showing through; a little denser over the collar between the glass and the ring"""
    r,_=_rho(N);g=1.26/LENS_Q;rc=r*LENS_Q
    al=(0.64+0.16*np.clip((rc-0.85)/0.25,0,1))*np.clip((g-r)/0.035,0,1)
    out=np.zeros((N,N,4));out[...,0]=0.38;out[...,1]=0.13;out[...,2]=0.14;out[...,3]=al
    return (out*255+0.5).astype(np.uint8)
def ring_tex(N=128):
    """additive: the flash (blood_dot.dds: red 0.67, a full red rim, its alpha rising from nothing in the middle to the rim)"""
    r,_=_rho(N);rim=np.exp(-((r-0.93)/0.04)**2);al=np.clip(r,0,1)**1.5*np.clip((0.99-r)/0.03,0,1)
    c=((0.67+0.33*rim)*al)[...,None]*np.array([1.0,0.02,0.02])
    return (np.clip(c,0,1)*255+0.5).astype(np.uint8)

class WinBuilder(Builder):   # palette entries are never shared between meshes: each mesh gets a contiguous window (vmw.pal_windows)
    win0=0
    def pal(self,slot,S):
        for k in range(self.win0,len(self.palette)):
            b,s=self.palette[k]
            if b==slot and np.allclose(s,S,atol=1e-4): return k
        self.palette.append((slot,S));return len(self.palette)-1

def build():
    R=fix_rigs(NMB,add_parents(NMB,rigs(NMB)));maps=find_maps(NMB)
    rig=[r for r in R if 'Bone32' in r['names'] and 'Dummy21' in r['names']][0];RN=rig['names']
    b=WinBuilder(rig);b.spec=True;meshes,Wg,ng,_=skinrun_fixed(NIF);info=dict(rig=R.index(rig),clips={})   # (b65: specular masks, as the guns: the reflection and the highlight)
    for m in meshes:
        if 'Bone06' in m['bone_names']:   # the hand: two fingertip joints ride on their middle joints (palette <= 48 for the shader)
            nb=list(m['bone_names']);s2b=list(m['sm']['s2b']);idx={n:i for i,n in enumerate(nb)}
            for a_,t_ in MERGE.items(): nb[idx[a_]]=t_;s2b[idx[a_]]=s2b[idx[t_]]
            m=dict(m,bone_names=nb,sm=dict(m['sm'],s2b=s2b))
        t=skin_tex(NIF,m['block']);b.win0=len(b.palette);tp=find_tex(t,[D])
        users=[x for x in meshes if skin_tex(NIF,x['block'])==t]   # b71: the PC's glow map (Armlancher_g: the pod's red lens and lights; gvmlib.emissive_image)
        b.add_skinned(m,tp,TEX.get(t,512),emis=lambda W,H,tp=tp,users=users:emissive_image(tp,users,W,H))   # (b78: the red lens is the engine's, on the sight only)
    for name,key,loop in CLIPS:
        fr,f=frames(rig,maps,key);b.bake(name,fr,loop,RC,CAM);info['clips'][name]=dict(src=f,nf=len(fr),loop=loop)
    # attachments (camera space, idle frame 0): the muzzle = the front of the launcher pod along the view, the red lens
    gnode={v:k for k,v in ng.items()};idle,_=frames(rig,maps,'stand_idl');P,W=idle[0]
    pod=[m for m in meshes if skin_tex(NIF,m['block'])=='Armlancher_d.dds'][0]
    # skin the pod's vertices at idle frame 0 (rigid approximation: each vertex follows its heaviest bone)
    BIr=(pod['BI'][0].view('<u2').reshape(-1,4) if pod['BI'][0].shape[1]==8 else pod['BI'][0]).astype(int)
    BW=pod['BW'][0].view('<f4').reshape(len(BIr),-1);palr=pod['PAL'][0].view('<u2').ravel();V=[]
    for (vs,vc),(ps,pc),_ in zip(pod['vregs'],pod['PAL'][2],pod['idxregs']):
        for j in range(vs,vs+vc):
            k=int(np.argmax(BW[j]));s=palr[ps+BIr[j][k]];bn=pod['bone_names'][s];bi=RN.index(bn)
            v=pod['sm']['s2b'][s]@np.r_[pod['P'][j],1.0];V.append(P[bi]+qmat(W[bi])@v[:3])
    V=np.array([RC@(v-CAM) for v in V])   # camera space: x right, y up, -z forward
    fwd=-V[:,2];tip=V[fwd>np.percentile(fwd,99.5)].mean(0);info['muzzle_cam_idle']=np.round(tip,2).tolist()
    b.att={'muzzle':tip}
    # b78: the sight's lens: of the four barrels' glass discs, the one highest on screen at idle (the top barrel, under the visor)
    discs,_,_=lens_discs(pod);best=None;byb={}
    for TT,ce,n,R in discs:
        vs=np.unique(TT.ravel());bn={}
        for (vs0,vc0),(ps0,pc0),_ in zip(pod['vregs'],pod['PAL'][2],pod['idxregs']):
            for j in vs[(vs>=vs0)&(vs<vs0+vc0)]: s_=palr[ps0+BIr[j][int(np.argmax(BW[j]))]];bn[s_]=bn.get(s_,0)+1
        s_=max(bn,key=bn.get);name=pod['bone_names'][s_];bi=RN.index(name);S=pod['sm']['s2b'][s_]
        lc=(S@np.r_[ce,1.0])[:3];ln=S[:3,:3]@n;ln/=np.linalg.norm(ln)
        ax=np.cross(n,[0,0,1.0]) if abs(n[2])<0.9 else np.cross(n,[1.0,0,0]);ax/=np.linalg.norm(ax);lr=np.linalg.norm(S[:3,:3]@(ax*R))
        cam=RC@(P[bi]+qmat(W[bi])@lc-CAM)
        print('lens disc on %s: camera space %s, radius %.2f'%(name,np.round(cam,1),lr),file=sys.stderr)
        if best is None or cam[1]>best[0]: best=(cam[1],name,lc,ln,lr,cam)
        byb[name]=(lc,ln,lr,cam)
    _,name,lc,ln,lr,cam=best
    # the glass faces the camera at idle (its normal toward +z in camera space): the engine offsets the disc along it
    if (RC@(qmat(W[RN.index(name)])@ln))[2]<0: ln=-ln
    b.att['lens_c']=lc.astype(np.float32);b.att['lens_n']=ln.astype(np.float32);b.att['lens_b']=np.array([b.bone(name),lr*LENS_Q,lr],np.float32)   # (the quad's half size; b109: the glass's radius - the engine knows the model by it)
    info['lens']=dict(bone=name,cam=np.round(cam,2).tolist(),radius=round(float(lr),3))
    # b109: the Tier 2 lens. The game lights two of the four barrels: effect\cyberbrain\armlauncher_load_1.nif sits on the launcher's
    # node Fire01 (child of Bone28: the sight above) and armlauncher_load_2.nif on Fire02 (child of Bone24: the barrel left of and
    # below it); Fire03 (Bone32) and Fire04 (Bone20) have no such effect. EP16 8:29-8:31 (Tier 2): both lit. The same three
    # attachments for Bone24's glass disc, 'lens2_*'; the engine turns the ring a quarter (load_2's hot spots are at the sides).
    if 'Bone24' in byb:
        lc2,ln2,lr2,cam2=byb['Bone24']
        if (RC@(qmat(W[RN.index('Bone24')])@ln2))[2]<0: ln2=-ln2
        b.att['lens2_c']=lc2.astype(np.float32);b.att['lens2_n']=ln2.astype(np.float32);b.att['lens2_b']=np.array([b.bone('Bone24'),lr2*LENS_Q,lr2],np.float32)
        info['lens2']=dict(bone='Bone24',cam=np.round(cam2,2).tolist(),radius=round(float(lr2),3))
    spec=b.spec;b.spec=False
    for nm_,img in (('lens_glow',lens_tex()),('lens_ring',ring_tex()),('lens_dark',dark_tex())):
        tp=os.path.join(OUT,'.%s.png'%nm_);Image.fromarray(img,'RGBA' if img.shape[2]==4 else 'RGB').save(tp);k=b.tex(tp,img.shape[0]);os.remove(tp);info[nm_]=k
    b.spec=spec
    assert len(b.palette)<=48,'palette too large (%d)'%len(b.palette)
    pal_windows(b)   # the Vita's skinning shader only reaches ~42 palette entries from the window start (weapons agent, model.h pbase)
    info['windows']=[(m['blend']>>8,int(np.asarray(m['BI']).max())+1) for m in b.meshes]
    os.makedirs(OUT,exist_ok=True);path=os.path.join(OUT,'vm_armlauncher.gvm');sz=b.write(path)
    info.update(bytes=sz,bones=len(b.bones),palette=len(b.palette),meshes=len(b.meshes),textures=len(b.texlist))
    print(path,json.dumps(info,indent=1));return info

if __name__=='__main__': build()
