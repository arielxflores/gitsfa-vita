# vm_m4a1.gvm: first-person arms + M4A1 (the footage's default loadout: QuickPoint S1 holo sight, folded front sight,
# standard barrel and magazine) with the game's own M4A1 1st-person morpheme clips, baked in camera space.
# usage: python3 make_vm_m4a1.py [outdir] [cam_x cam_y cam_z]
import sys;sys.path.insert(0,'/root/gits/vm')
from gvmlib import *
OUT=sys.argv[1] if len(sys.argv)>1 else '/root/gits/build/data'
A='/root/anim/GIS_base_1st_runtimeBinary/';NMB=A+'GIS_base_1st.nmb'
R=add_parents(NMB,rigs(NMB));RIG=R[57];MAPS=find_maps(NMB);RN=RIG['names']
CAM=np.array([float(x) for x in sys.argv[2:5]]) if len(sys.argv)>4 else np.array([0,0,156.4])
RC=np.array([[-1,0,0],[0,0,1],[0,1,0]],float)   # rig (facing -y, z up) -> camera (x right, y up, z back)
W1='/root/anim/out/weapon/';P1=W1+'Parts/'
# each clip exists as two hashed exports; the usable one is the export whose Bip01_Head sits at (2.4,4.3,159)
CLIPS=[('idle','base_M4A1_1st_stand_idle_base_7a04bbe8',1),
 ('walk','base_M4A1_1st_stand_walk_forw_b515dd63',1),          # (aimed walk: the engine plays it while moving in ADS)
 ('run','base_M4A1_1st_stand_run_forwa_5bc78edf',1),
 ('sprint','base_M4A1_1st_sprint_base_M4A1_1335bd3',1),
 ('sprint_in','base_M4A1_1st_sprint_ready_ba_cb6854ed',0),('sprint_out','base_M4A1_1st_sprint_stop2_ba_c7c09808',0),
 ('fire','base_M4A1_1st_fire_base_M4A1__2532986d',0),
 ('fire_ads','base_M4A1_1st_fire_zoom_base__cec66364',0),
 ('ads_idle','base_M4A1_1st_stand_zoom_base_9f303e32',1),      # static aimed pose (61 frames)
 ('ads_in','base_M4A1_1st_stand_zoom_read_19c4bd2d',0),        # 2-frame raise: hip -> aimed
 ('reload','base_M4A1_1st_reload_fast_bas_f407a3c7',0),('reload_empty','base_M4A1_1st_reload_base_M4A_9150e9f5',0),
 ('draw','base_M4A1_1st_swap_end_base_M_24e884b4',0),('holster','base_M4A1_1st_swap_start_base_7c254202',0),
 ('jump','base_M4A1_1st_jump_up_base_M4_5fc58ebb',0),('fall','base_M4A1_1st_jump_fall_base__aa5e06b5',1),('land','base_M4A1_1st_land_base_M4A1__6445fa3f',0),
 ('slide','base_M4A1_1st_sliding_base_M4_19dd171e',1),('slide_fire','base_M4A1_1st_sliding_fire_ba_a3aabbc7',0)]
b=Builder(RIG)
hands=skinrun('/root/anim/out/model/ActorGhost/CommonHand/1stHand.nif')[0]
gun,Wg,ng,_=skinrun(W1+'AR_M4A1/1st/M4A1_Body.nif');gnode={v:k for k,v in ng.items()};gW=lambda n:Wg[gnode[n]]
H='/root/anim/out/model/ActorGhost/CommonHand/'
from PIL import Image
Image.fromarray((np.asarray(Image.open(H+'1sthand2_d.dds').convert('RGB'),float)*0.5).astype('u1')).save(H+'1sthand2_dark.png')   # the suit sleeve reads as dark cloth in game (cf. footage)
b.add_skinned(hands[0],H+'1sthand_d.dds',1024);b.add_skinned(hands[2],H+'1sthand2_dark.png')
# the rig's weapon slots carry the C-30 names; the M4A1 skin binds to the same slots
AL={'M4A1_Trigger':'AR_SEBRO_Trigger','M4A1_Breechblock':'AR_SEBRO_Breechblock','M4A1_Magazine':'AR_SEBRO_Magazine'}
for m in gun: b.add_skinned(m,W1+'AR_M4A1/1st/M4A1_D.dds',1024,alias=AL)
b.add_rigid(static_meshes(P1+'Magazine/AR_M4A1/1st/M4A1_MA.nif'),gW('wea_ma'),'AR_SEBRO_Magazine',np.linalg.inv(gW('M4A1_Magazine')),P1+'Magazine/AR_M4A1/1st/',
            texmap=lambda t:P1+'Magazine/AR_M4A1/1st/m4A1_MA_D.dds')
barrel=static_meshes(P1+'Barrel/AR_M4A1/1st/M4A1_BA.nif')
b.add_rigid(barrel,gW('wea_ba'),'Grip',np.linalg.inv(gW('Grip')),'',texmap=lambda t:W1+'Textures/B_D.dds')
b.add_rigid(static_meshes(P1+'IronSight/IS_M4/1st/IS_M4_E2_D.nif'),gW('E2'),'Grip',np.linalg.inv(gW('Grip')),P1+'IronSight/IS_M4/1st/',
            lambda t:2 if t and t.lower().startswith('dot') else 0)
# holo sight: diffuse with its emissive (the two cyan status lights) baked in; reticle/glow/lens quads additive
QP=P1+'ScopeSight/QuickPoint_S1/1st/'
from PIL import Image
sight=[m for m in static_meshes(QP+'QuickPoint_S1.nif') if m['tex']!='DOT_Blur.dds']   # (the big blur quad reads as a red haze over the whole window; footage shows only reticle + ring glow)
def bake_emissive(D,E,m,gain=1.6):
    """add the glow map (sampled through the mesh's 2nd UV set) into the diffuse (1st UV set), texture-space bake"""
    H,W=D.shape[:2];eh,ew=E.shape[:2];out=D.copy();lit=E.sum(2)>30
    for t in m['I'].reshape(-1,3):
        u1=np.mod(m['UV1'][t],1);ex=(u1[:,0]*ew).astype(int)%ew;ey=(u1[:,1]*eh).astype(int)%eh
        x0,x1=int(max(0,np.floor(min(u1[:,0])*ew))),int(min(ew-1,np.ceil(max(u1[:,0])*ew)));y0,y1=int(max(0,np.floor(min(u1[:,1])*eh))),int(min(eh-1,np.ceil(max(u1[:,1])*eh)))
        if not lit[y0:y1+1,x0:x1+1].any(): continue
        p=np.mod(m['UV'][t],1)*[W,H]
        X0,X1=int(np.floor(p[:,0].min())),int(np.ceil(p[:,0].max()));Y0,Y1=int(np.floor(p[:,1].min())),int(np.ceil(p[:,1].max()))
        xs,ys=np.meshgrid(np.arange(X0,X1+1)+0.5,np.arange(Y0,Y1+1)+0.5)
        a,b,c=p;d=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
        if abs(d)<1e-9: continue
        l0=((b[1]-c[1])*(xs-c[0])+(c[0]-b[0])*(ys-c[1]))/d;l1=((c[1]-a[1])*(xs-c[0])+(a[0]-c[0])*(ys-c[1]))/d;l2=1-l0-l1
        ins=(l0>=-0.02)&(l1>=-0.02)&(l2>=-0.02)
        uv=l0[ins,None]*m['UV1'][t[0]]+l1[ins,None]*m['UV1'][t[1]]+l2[ins,None]*m['UV1'][t[2]]
        col=E[(np.mod(uv[:,1],1)*eh).astype(int)%eh,(np.mod(uv[:,0],1)*ew).astype(int)%ew]
        yy=(ys[ins]-0.5).astype(int)%H;xx=(xs[ins]-0.5).astype(int)%W
        out[yy,xx]=np.clip(np.maximum(out[yy,xx],D[yy,xx]+col*gain),0,255)
    return out
d=np.asarray(Image.open(QP+'IS_SF_SMG_01_D.dds').convert('RGB'),float);e=np.asarray(Image.open(QP+'IS_SF_SMG_01_E.dds').convert('RGB'),float)
Image.fromarray(bake_emissive(d,e,sight[0]).astype('u1')).save(QP+'IS_SF_SMG_01_DE.png')
def qptex(t):
    if t.upper().startswith('IS_SF_SMG_01_D'): return QP+'IS_SF_SMG_01_DE.png'
    if 'Lens' in t: return W1+'Textures/'+t
    return QP+t
b.add_rigid(sight,gW('SC_DUMMY'),'Grip',np.linalg.inv(gW('Grip')),QP,lambda t:2 if t and ('dot' in t.lower() or 'lens' in t.lower()) else 0,texmap=qptex)
# weapon slots the clip does not animate fall back to the rig's C-30 bind: re-seat them at the M4A1 bind (relative to Grip)
iG=RN.index('Grip');FIX=[(RN.index('AR_SEBRO_'+s),gW(g)[:3,3]) for s,g in [('Magazine','M4A1_Magazine'),('Breechblock','M4A1_Breechblock'),('Trigger','M4A1_Trigger')]]
C30BIND={RN.index('AR_SEBRO_Magazine'):np.array([-12.836,0,-4.77]),RN.index('AR_SEBRO_Breechblock'):np.array([4.254,0.642,8.757]),RN.index('AR_SEBRO_Trigger'):np.array([5.171,0,3.944])}
iW=RIG['parent'][iG];KIDS=[i for i,_ in FIX]
def _ang(R): return np.degrees(np.arccos(np.clip((np.trace(R)-1)/2,-1,1)))
_P,_W=clip_frames(A,'base_M4A1_1st_stand_idle_base_7a04bbe8',RIG,MAPS)[0][0];GLOC=qmat(_W[iW]).T@qmat(_W[iG])   # Grip rotation in Weapon_R space (constant in every clip that keys it)
def fixup(fr):
    out=[];P0,W0=fr[0];regrip=_ang(GLOC.T@(qmat(W0[iW]).T@qmat(W0[iG])))>30   # clip without a Grip rotation key: the gun would hang at the rig's bind angle
    for P,W in fr:
        P=P.copy();W=W.copy()
        if regrip:   # re-seat the gun in the right hand as in idle, carrying the weapon parts (magazine etc.) along
            Ro=qmat(W[iG]);Rn=qmat(W[iW])@GLOC
            for i in KIDS: P[i]=P[iG]+Rn@(Ro.T@(P[i]-P[iG]));W[i]=mat2q(Rn@(Ro.T@qmat(W[i])))
            W[iG]=mat2q(Rn)
        RG=qmat(W[iG])
        for i,m4 in FIX:
            if np.linalg.norm(RG.T@(P[i]-P[iG])-C30BIND[i])<0.05: P[i]=P[iG]+RG@m4;W[i]=W[iG]
        out.append((P,W))
    return out
for ent in CLIPS:
    fr,h=clip_frames(A,ent[1],RIG,MAPS);fr=fixup(fr)
    b.bake(ent[0],fr,ent[2],RC,CAM);print('clip',ent[0],len(fr),'frames')
tip=max(barrel[0]['P'][:,0]);mz=(gW('wea_ba')@np.array([tip,0,0,1.0]))[:3]
dot=[m for m in sight if m['tex'] and m['tex'].lower().startswith('bsa_combo_dot')][0]['P'].mean(0)
b.att={'mz_grip':(np.linalg.inv(gW('Grip'))@np.r_[mz,1.0])[:3],'muzzle':mz,'sight':(gW('SC_DUMMY')@np.r_[dot,1.0])[:3],'hud1':(np.linalg.inv(gW('Grip'))@gW('hud1'))[:3,3]}
print('attachments',{k:np.round(v,2).tolist() for k,v in b.att.items()})
print('vm_m4a1.gvm',b.write(OUT+'/vm_m4a1.gvm'),'bytes, bones',len(b.bones),'palette',len(b.palette),'meshes',len(b.meshes),'textures',len(b.texlist))
