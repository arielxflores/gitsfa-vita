# vm_<short>.gvm for the other first-person weapons: arms + weapon (body skin + rigid parts + sight) with the game's own
# 1st-person morpheme clips, baked in camera space exactly like make_vm_m4a1.py (same rig->camera transform, same eye).
# Differences from the M4A1 script:
#  - each weapon is decoded with its OWN rig from GIS_base_1st.nmb (the skin's bone names match it, unanimated
#    weapon slots fall back to the right bind; the 71-bone M249 rig needs vmw.fix_rigs)
#  - each clip is checked (Bip01_Head at an eye rest position, Grip near the right hand) and the first valid
#    hash variant/bone map is used
#  - clips borrowed from another weapon family get their weapon parts re-seated at this weapon's bind
# usage: python3 make_vm_weapon.py <short|all> [outdir]      (CAM eye = rig (0,0,156.93) as for vm_m4a1)
import sys,os,json;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from vmw import *
from PIL import Image
OUT=sys.argv[2] if len(sys.argv)>2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','build','data')
import gitspaths
A=gitspaths.dirp('ANIMBIN','GIS_base_1st_runtimeBinary');NMB=A+'GIS_base_1st.nmb'
R=fix_rigs(NMB,add_parents(NMB,rigs(NMB)));MAPS=find_maps(NMB)
# b109 (user: "the magnum reload animation also looks a bit wrong"): the 44 Magnum's own reload clips have 62 channels; their map on
# its 61-bone rig is in the file, but with TWO lengths (60 mapped, 62 listed: the list is as long as the second, a 0 after the
# first entry = a channel without a bone - the clip's two extra channels). find_maps reads the first length and drops the
# record for its repeated zeros, so b92 fell back to the Mateba's reloads retargeted by bone name (another revolver's motion:
# its cylinder swings up over the frame, this one's out to the side). Read here for that rig only: every other gun's choice of
# maps stays what it was.
def _maps_two_lengths(path,nrig_want,nch_want):
    import struct as _s
    d=open(path,'rb').read();out=[];pat=_s.pack('<H',14)+b'\0\0'+_s.pack('<H',0);i=0
    while True:
        i=d.find(pat,i+1)
        if i<0: break
        n,n2=_s.unpack_from('<HH',d,i-4);nch=_s.unpack_from('<I',d,i-8)[0]
        if nch!=nch_want or n2<=n or n2!=nch: continue
        for w in (1,2,3,4):
            nrig=_s.unpack_from('<I',d,i-8-4*w-8)[0]
            if _s.unpack_from('<I',d,i-8-4*w-4)[0]==w: break
        else: continue
        if nrig!=nrig_want: continue
        lst=list(_s.unpack_from('<%dH'%(n2+1),d,i+4))
        if lst[0]!=0 or max(lst)>=nrig: continue
        lst=[0]+[v if v else None for v in lst[1:]]
        if len([v for v in lst if v is not None])!=n+1 or len(set(v for v in lst if v is not None))!=n+1: continue
        out.append(dict(off=i,nrig=nrig,nch=nch,n=n2,n2=n2,list=lst,extra=[],bits=d[i-8-4*w:i-8]))
    return out
if not any(m_['nrig']==61 and m_['nch']==62 for m_ in MAPS): MAPS=MAPS+_maps_two_lengths(NMB,61,62)
# b92: the 44 Magnum's rig (12: the 44MG set, also the Mateba 2008M's bones) has no Bip01_Head: the head added under the neck
# with the Mateba rig's bind (8), so its clips can be checked and the camera follows the head as for every other gun
def _add_head(dst,src):
    if 'Bip01_Head' in dst['names'] or 'Bip01_Head' not in src['names']: return
    i=src['names'].index('Bip01_Head');pn=src['names'][src['parent'][i]]
    if pn not in dst['names']: return
    dst['nrig0']=len(dst['names']);dst['names']=list(dst['names'])+['Bip01_Head'];dst['parent']=list(dst['parent'])+[dst['names'].index(pn)]
    for k in ('pos','quat'):
        v=dst[k];dst[k]=(np.concatenate([v,np.asarray(src[k][i])[None]]) if isinstance(v,np.ndarray) else list(v)+[src[k][i]])
_add_head(R[12],R[8])
CAM=np.array([0,0,156.93]);RC=np.array([[-1,0,0],[0,0,1],[0,1,0]],float)
W1=gitspaths.dirp('ANIMOUT','weapon');P1=W1+'Parts/';SS=P1+'ScopeSight/';IS=P1+'IronSight/'
H=gitspaths.dirp('ANIMOUT','model','ActorGhost','CommonHand')
TEXD=[W1+'Textures/']
def pre(fam,key):   # clip file prefixes of a family whose name starts with key (all hash variants); '@fam' = no 'base_' (bato_knife ...)
    st=('%s_1st_%s'%(fam[1:],key)) if fam.startswith('@') else ('base_%s_1st_%s'%(fam,key))
    return sorted(x.rsplit('.',1)[0] for x in os.listdir(A) if x.startswith(st))
XNAME={'Weapon_R':'Weapon_R_forearm','AR_SEBRO_Magazine':'M4A1_Magazine','AR_SEBRO_Breechblock':'M4A1_Breechblock','AR_SEBRO_Trigger':'M4A1_Trigger'}   # source-rig bone -> target rig bone (cross-rig clips)
# b92: the Mateba's reloads on the 44 Magnum / Mateba 2008M rig (the 44MG set's own reloads have no bone map for it): same revolver parts
XNAME.update({'Mateba_'+k:'44MG_'+k for k in ('Cap','Trigger','Hammer','Release','Axis','Cylinder','Bullet','Rod')})
def decode_cross(p,src,rig,headfix=True,head=None,local=False):
    """a clip of another anim set (its own rig src, e.g. the 'bato_knife' stealth kill): decoded with that rig's best bone
    map, retargeted by bone name onto rig (world transforms), and moved so that the head sits at the 1st-person eye rest
    position in every frame (the camera follows the head) -> frames [(P,W)] in rig space"""
    rs=R[src];f=[x for x in os.listdir(A) if x.startswith(p)][0];h=hdr(A+f);C=decode_clip(h)
    with contextlib.redirect_stdout(io.StringIO()):
        cands=choose_map3(h,C,MAPS,rs,return_all=True)
    if not cands: return None
    C['map']=cands[0][1]['list'];RS=rs['names'];RT=rig['names'];Pb,Wb=bind_world(rig)
    tn=lambda n:XNAME[n] if n in XNAME and XNAME[n] in RT else n
    idx=[(RS.index(n),RT.index(tn(n))) for n in RS if tn(n) in RT]
    iH=RT.index('Bip01_Head');out=[]
    if local:
        import gvmlib as gl
        KID_T=sorted(descendants(rig['parent'],RT.index('Grip')));XL={b_:a for a,b_ in idx if RS[a]!=RT[b_] and b_ in KID_T}
    for i in range(C['nf']):
        if C['mode']==1: lp,lq=pose(rs,C,i);P,W=fk(rs['parent'],lp,lq)
        else: P,W=pose_world(rs,C,i)
        Pt=np.array(Pb,float).copy();Wt=np.array(Wb,float).copy()
        for a,b_ in idx: Pt[b_]=P[a];Wt[b_]=W[a]
        if local:   # b92 (local): the gun's own parts move as the source's renamed parts do relative to their bind (a revolver
            # cylinder swinging out about its own crane), placed on this gun's bones; its other parts follow their parents
            for b_ in KID_T:
                pb=rig['parent'][b_];Rp=gl.qmat(Wt[pb]);Bt=gl.qmat(rig['quat'][b_])
                if b_ in XL:
                    a=XL[b_];pa=rs['parent'][a];D=gl.qmat(rs['quat'][a]).T@(gl.qmat(W[pa]).T@gl.qmat(W[a]));Wt[b_]=gl.mat2q(Rp@Bt@D)
                else: Wt[b_]=gl.mat2q(Rp@Bt)
                Pt[b_]=Pt[pb]+Rp@np.asarray(rig['pos'][b_],float)
        if headfix: Pt+=(HEAD if head is None else head)-Pt[iH]
        out.append((Pt,Wt))
    return out
# ---- class base clip sets: engine name -> (loop, [family, key] candidates in order of preference)
# b122 (user: "when running and jumping as batou the screen seems to glitch with his character model" - he carried the SEBURO C-30):
# the M4A1 set's jump decoded with seven guns' own rigs (57 C-30 / C-26A, 14 F2000, 24 UMP-45, 41 MG36, 36 SEBURO MG, 42 KSG) threw
# the Grip 98 cm out of place (-55, +73, +34: gun and arms swung out of the picture and back with every jump); as on the SCAR
# and the 870 it is now decoded with the M4A1's rig (22) and retargeted. Check: the Grip's farthest distance from its idle place
# in every gun's jump clip is 1-3 cm (these seven: 98 before).
# (AR jump: the 39ec8d98 export keys the Grip -> no re-grip, the left hand stays on the gun. Sprint: 'sprint_base' = the
#  original (PSS9 TDM footage 169-171 s: the gun lowered across the bottom of the screen, pointing left, no hands in
#  view); b31's 'sprint_running' 812cdcfc kept the gun up at the right with the left hand dropped away: a floating gun)
AR=dict(idle=(1,[('M4A1','stand_idle')]),walk=(1,[('M4A1','stand_walk')]),run=(1,[('M4A1','stand_run')]),
        sprint=(1,[('M4A1','sprint_base')]),sprint_in=(0,[('M4A1','sprint_ready')]),sprint_out=(0,[('M4A1','sprint_stop2')]),
        fire=(0,[('M4A1','fire_base')]),fire_ads=(0,[('M4A1','fire_zoom')]),ads_idle=(1,[('M4A1','stand_zoom_base')]),ads_in=(0,[('M4A1','stand_zoom_read')]),
        reload=(0,[('M4A1','reload_fast')]),reload_empty=(0,[('M4A1','reload_base')]),draw=(0,[('M4A1','swap_end')]),holster=(0,[('M4A1','swap_start')]),
        jump=(0,[('M4A1','jump_up_base_M4_39ec8d98'),('M4A1','jump_up_base')]),fall=(1,[('M4A1','jump_fall')]),land=(0,[('M4A1','land_base')]),
        slide=(1,[('M4A1','sliding_base')]),slide_fire=(0,[('M4A1','sliding_fire')]))
SMG=dict(AR,idle=(1,[('Kriss','stand_idle')]),walk=(1,[('Kriss','stand_walk')]),run=(1,[('Kriss','stand_run')]),
         sprint=(1,[('M4A1','sprint_base_M4A_268bb5d8',22)]),   # (the KRISS set's own sprint_base holds the gun under the screen)
sprint_in=(0,[('Kriss','sprint_ready')]),sprint_out=(0,[('Kriss','sprint_stop2')]),
         fire=(0,[('Kriss','fire_Grip')]),fire_ads=(0,[('Kriss','fire_zoom_Grip')]),ads_idle=(1,[('Kriss','stand_zoom_Gri')]),ads_in=(0,[('Kriss','stand_zoom_rea')]),
         reload=(0,[('Kriss','reload_fast')]),reload_empty=(0,[('Kriss','reload_base')]),draw=(0,[('Kriss','swap_end_base')]),holster=(0,[('Kriss','swap_start')]),
         jump=(0,[('Kriss','jump_up')]),fall=(1,[('Kriss','jump_fall')]),land=(0,[('Kriss','land_base')]))
HG=dict(idle=(1,[('P99','stand_idle')]),walk=(1,[('P99','stand_walk')]),run=(1,[('P99','stand_run')]),
        sprint=(1,[('P99','sprint_base'),('p99','stand_sprint_run')]),sprint_in=(0,[('P99','sprint_ready')]),
        fire=(0,[('P99','fire_base')]),fire_ads=(0,[('P99','fire_zoom')]),ads_idle=(1,[('P99','stand_zoom_base')]),ads_in=(0,[('P99','stand_zoom_ready')]),
        reload=(0,[('P99','reload_fast')]),reload_empty=(0,[('P99','reload_base')]),draw=(0,[('p99','swap_end')]),holster=(0,[('p99','swap_start')]),
        jump=(0,[('P99','jump_up_base')]),fall=(1,[('P99','jump_fall')]),land=(0,[('P99','land_base')]),
        slide=(1,[('P99','sliding_base')]),slide_fire=(0,[('P99','sliding_fire')]))
def own(base,fam,**kw):   # base set with per-weapon overrides
    d=dict(base);d.update(kw);return d
# ---- weapons. parts: (nif, attach node in the body, bone, texture dirs); sight: (kind, ...) -> ADS aim point
B_D=W1+'Textures/B_D.dds'
CFG={
 'm4a1':dict(rig=22,fam='M4A1',dir='AR_M4A1',body='M4A1_Body.nif',   # the default M4A1 (tutorial + armory 'SIGHT: empty'): IS_M4 carry-handle rear sight + front post
   parts=[('Magazine/AR_M4A1/1st/M4A1_MA.nif','wea_ma','M4A1_Magazine'),('Barrel/AR_M4A1/1st/M4A1_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'M4A1',reload=(0,[('M4A1','reload_fast'),('M4A1','reload_fast',57)]),reload_empty=(0,[('M4A1','reload_base'),('M4A1','reload_base',57)]))),
 'scar':dict(rig=23,fam='Scar',dir='AR_SCAR',body='SCAR_BODY.nif',
   parts=[('Magazine/AR_SCAR/1st/SCAR_MA.nif','wea_ma','Scar_Magazine'),('Barrel/AR_SCAR/1st/SCAR_BA.nif','wea_ba','Grip')],
   sight=('scope','TRICO_GOA','Trijicon_CROSS'),
   clips=own(AR,'Scar',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('Scar','fire_base')]),fire_ads=(0,[('Scar','fire_zoom')]),reload=(0,[('Scar','reload_fast')]),
             reload_empty=(0,[('Scar','reload_base')]),draw=(0,[('Scar','swap_end')]))),
 'acr':dict(rig=29,fam='ACR',dir='AR_ACR',body='ACR_Body.nif',
   parts=[('Magazine/AR_ACR/1st/ACR_MA.nif','wea_ma','ACR_Magazine'),('Barrel/AR_ACR/1st/ACR_BA.nif','wea_ba','Grip')],
   sight=('scope','GRAFT_N3','DOT_02'),
   clips=own(AR,'ACR',fire=(0,[('ACR','fire_base')]),fire_ads=(0,[('ACR','fire_zoom')]),reload=(0,[('ACR','reload_fast')]),
             reload_empty=(0,[('ACR','reload_base')]),draw=(0,[('ACR','swap_end')]))),
 # b89 (the user's "Assault Rifles of GitS SAC - First Assault Online" video, v2.0 armory: every rifle with no sight fitted): the
 # rifles the port was missing, each on the rig its body is skinned to (AK47 / K22 / KHX-160 on the AN94 bones and the AN94
 # set, KH-417 on the M4A1's) and its own clip family where the game has one (F2000, FAMAS, X95R)
 'ak47':dict(rig=9,fam='AN94',dir='AR_AK47',body='AK47_Body.nif',
   parts=[('Magazine/AR_AK47/1st/AK47_MA.nif','wea_ma','AN94_Magazine'),('Barrel/AR_AK47/1st/AK47_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_AK47','IS_AK47_E1_U.nif','IS_AK47_E2_U.nif',None),
   clips=own(AR,'AN94',fire=(0,[('AN94','fire_base')]),fire_ads=(0,[('AN94','fire_zoom')]),reload=(0,[('AN94','reload_fast')]),
             reload_empty=(0,[('AN94','reload_base')]),draw=(0,[('AN94','swap_end')]))),
 'k2':dict(rig=9,fam='AN94',dir='AR_K2',body='K2_body.nif',
   parts=[('Magazine/AR_K2/1st/K2_MA.nif','wea_ma','AN94_Magazine')],
   sight=('iron','IS_K2','IS_K2_E1_U.nif','IS_K2_E2_U.nif',None),
   clips=own(AR,'AN94',fire=(0,[('AN94','fire_base')]),fire_ads=(0,[('AN94','fire_zoom')]),reload=(0,[('AN94','reload_fast')]),
             reload_empty=(0,[('AN94','reload_base')]),draw=(0,[('AN94','swap_end')]))),
 'arx160':dict(rig=9,fam='AN94',dir='AR_ARX_160',body='ARX_160_Body.nif',
   parts=[('Magazine/AR_ARX_160/1st/ARX_160_MA.nif','wea_ma','AN94_Magazine')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'AN94',fire=(0,[('AN94','fire_base')]),fire_ads=(0,[('AN94','fire_zoom')]),reload=(0,[('AN94','reload_fast')]),
             reload_empty=(0,[('AN94','reload_base')]),draw=(0,[('AN94','swap_end')]))),
 'hk417':dict(rig=22,fam='M4A1',dir='AR_HK417',body='HK417_Body.nif',
   parts=[('Magazine/AR_HK417/1st/HK417_MA.nif','wea_ma','M4A1_Magazine')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'M4A1',reload=(0,[('M4A1','reload_fast'),('M4A1','reload_fast',57)]),reload_empty=(0,[('M4A1','reload_base'),('M4A1','reload_base',57)]))),
 'f2000':dict(rig=14,fam='F2000',dir='AR_F2000',body='F2000_Body.nif',
   parts=[('Magazine/AR_F2000/1st/F2000_MA.nif','wea_ma','F2000_Magazine')],
   sight=('iron','IS_F2000','IS_F2000_E1_U.nif','IS_F2000_E2_U.nif',None),
   clips=own(AR,'F2000',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('F2000','fire_base')]),fire_ads=(0,[('F2000','fire_zoom')]),reload=(0,[('F2000','reload_fast')]),
             reload_empty=(0,[('F2000','reload_base')]),draw=(0,[('F2000','swap_end')]))),
 'famas':dict(rig=19,fam='FAMAS',dir='AR_FAMAS',body='FAMAS_Body.nif',
   parts=[('Magazine/AR_FAMAS/1st/FAMAS_MA.nif','wea_ma','FAMAS_Magazine')],
   sight=('builtin',2.25),   # the carry handle's own sights (the body has no E1 / E2 nodes)
   clips=own(AR,'FAMAS',fire=(0,[('FAMAS','Fire_base')]),fire_ads=(0,[('FAMAS','Fire_Zoom')]),reload=(0,[('FAMAS','Reload_fast')]),
             reload_empty=(0,[('FAMAS','Reload_base')]),draw=(0,[('FAMAS','Swap_End_base')]))),
 'x95r':dict(rig=35,fam='X95R',dir='AR_X95_R',body='X95_R_Body.nif',
   parts=[('Magazine/AR_X95R/1st/X95R_MA.nif','wea_ma','X95R_Magazine'),('Barrel/AR_X95_R/1st/X95_R_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'X95R',fire=(0,[('X95R','fire_base')]),fire_ads=(0,[('X95R','fire_zoom')]),reload=(0,[('X95R','reload_fast')]),
             reload_empty=(0,[('X95R','reload_base')]),draw=(0,[('X95R','swap_end')]),sprint=(1,[('X95R','sprint_base')]))),
 # b89: the SEBURO C-30 (the rifle bots already carried) on the AR_SEBRO rig and its own AR_C30 set (fire, reloads, draw,
 # sprint); its one-piece IS_SEBURO_C30 sight (E1 only: rear notch + front post on one rail), aimed at the post (make_vm.py's)
 'c30':dict(rig=57,fam='AR_C30',dir='AR_SEBURO_C30',body='SEBURO_C30_body.nif',
   parts=[('Magazine/AR_SEBURO_C30/1st/SEBURO_C30_MA.nif','wea_ma','AR_SEBRO_Magazine')],
   sight=('iron','IS_SEBURO_C30','IS_SEBURO_C30_E1_U.nif',None,(-4.582,0,1.494)),
   clips=own(AR,'AR_C30',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('AR_C30','Fire_base')]),reload=(0,[('AR_C30','reload_fast')]),reload_empty=(0,[('AR_C30','reload_base')]),
             draw=(0,[('AR_C30','swap_end')]),sprint=(1,[('AR_C30','sprint_base')]),sprint_in=(0,[('AR_C30','sprint_ready')]))),
 # b90: the SMGs of the user's SMG video (and the PP-2200 PDW), each on the rig its body is skinned to and its own clip family
 # (fire / reloads / draw from the family, the M4A1 everyday set around them, as the MP-V5A); sights as the video's ADS shows
 'c26a':dict(rig=57,fam='SM_C26A',dir='SM_SEBURO_C26A',body='SEBURO_C26A_body.nif',
   parts=[('Magazine/SM_SEBURO_C26A/1st/SEBURO_C26A_MA.nif','wea_ma','AR_SEBRO_Magazine')],
   sight=('iron','IS_SEBURO_C26A','IS_SEBURO_C26A_E1_U.nif',None,(14.6,0,3.4)),   # (one-piece sight: rear lens ring + front dot at 3.4 cm)
   clips=own(AR,'SM_C26A',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('SM_C26A','fire_base')]),fire_ads=(0,[('A26','fire_zoom'),('M4A1','fire_zoom')]),reload=(0,[('SM_C26A','reload_fast')]),
             reload_empty=(0,[('SM_C26A','reload_base')]),draw=(0,[('SM_C26A','swap_end')]),sprint=(1,[('SM_C26A','sprint_base')]),sprint_in=(0,[('SM_C26A','sprint_ready')]))),
 'evo':dict(rig=28,fam='Scolpion',dir='SM_EVO',body='EVO_Body.nif',
   parts=[('Magazine/SM_EVO/1st/EVO_MA.nif','wea_ma','Scolpion_evo_Magazine'),('Barrel/SM_EVO/1st/EVO_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'Scolpion',fire=(0,[('Scolpion','fire_base')]),reload=(0,[('Scolpion','reload_fast')]),reload_empty=(0,[('Scolpion','reload_base')]),draw=(0,[('Scolpion','swap_end')]))),
 'kacpdw':dict(rig=22,fam='PDW',dir='SM_KAC_PDW',body='KAC_PDW_Body.nif',
   parts=[('Magazine/SM_KAC_PDW/1st/KAC_PDW_MA.nif','wea_ma','M4A1_Magazine')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'PDW',reload=(0,[('PDW','reload_fast')]),reload_empty=(0,[('PDW','reload_base')]),draw=(0,[('PDW','swap_end')]))),
 'mp5k':dict(rig=31,fam='MP5K',dir='SM_MP5K',body='MP5K_Body.nif',
   parts=[('Magazine/SM_MP5K/1st/MP5K_MA.nif','wea_ma','MP5K_Magazine'),('Barrel/SM_MP5K/1st/MP5K_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_MP5','IS_MP5_E1_U.nif','IS_MP5_E2_U.nif','Dot'),
   clips=own(AR,'MP5K',fire=(0,[('MP5K','fire_base')]),fire_ads=(0,[('MP5K','fire_zoom')]),reload=(0,[('MP5K','reload_fast')]),reload_empty=(0,[('MP5K','reload_base')]),draw=(0,[('MP5K','swap_end')]))),
 'mpx':dict(rig=22,fam='MPX',dir='SM_MPX',body='MPX_body.nif',alias={'MPX_Ring':'Grip'},
   parts=[('Magazine/SM_MPX/1st/MPX_MA.nif','wea_ma','M4A1_Magazine'),('Barrel/SM_MPX/1st/MPX_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),
   clips=own(AR,'MPX',reload=(0,[('MPX','reload_fast')]),reload_empty=(0,[('MPX','reload_base')]),draw=(0,[('MPX','swap_end')]))),
 'p90':dict(rig=18,fam='P90',dir='SM_P90',body='P90_body.nif',parts=[],alias={'p90_Trigger':'Grip'},
   sight=('scope','QuickPoint_S1','bsa_combo_DOT'),   # (v2.0's ring sight is not in the install: the closest part it has, a ring reflex sight, on the SC_DUMMY rail)
   clips=own(AR,'P90',fire=(0,[('P90','fire_base')]),reload=(0,[('P90','reload_fast')]),reload_empty=(0,[('P90','reload_base')]),draw=(0,[('P90','swap_end')]))),
 # b116 (user: "playing as togusa using the pp-2200 and it doesn't look like he's holding it right while carrying it normally"): its set has no
 # stand_idle and leaves the left arm unanimated, so the left hand took the M4A1 idle's place on the M4A1's handguard - 26 to 35 cm ahead of
 # the grip, past this gun's muzzle (22 cm): the hand closed on air in front of the gun. lhand_shift moves it back onto the gun (palm under the
 # front of the body, thumb on the left side), in every clip that holds it.
 'pp2000':dict(rig=52,fam='PP2000',dir='SM_PP2000',body='PP2000_Body.nif',lhand_shift=(-13,0.5,-1),
   parts=[('Magazine/SM_PP2000/1st/PP2000_MA.nif','wea_ma','PP2000_Magazine')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),   # (v2.0's ring sight is not in the install: the M4's ring rear + post on its E1 / E2 rail)
   clips=own(AR,'PP2000',fire=(0,[('PP2000','fire_base')]),fire_ads=(0,[('PP2000','fire_zoom')]),reload=(0,[('PP2000','reload_fast')]),reload_empty=(0,[('PP2000','reload_base')]),
             draw=(0,[('PP2000','swap_end')]),walk=(1,[('PP2000','stand_walk')]),ads_idle=(1,[('PP2000','stand_zoom_ba')]),ads_in=(0,[('PP2000','stand_zoom_re')]),
             sprint=(1,[('PP2000','sprint_base')]),land=(0,[('PP2000','land1'),('M4A1','land_base')]))),
 'ump45':dict(rig=24,fam='UMP45',dir='SM_UMP45',body='UMP45_body.nif',skip=('sticker',),   # (b105: the body's 'sticker' patch - the PC's weapon-sticker decal, 12 vertices - sits 20 cm ABOVE this gun in the file: it showed over the gun in INSPECT; on the other guns it lies on the receiver)
   parts=[('Magazine/SM_UMP45/1st/UMP45_MA.nif','wea_ma','UMP45_Magazine')],
   sight=('iron','IS_UMP45','IS_UMP45_E1_U.nif','IS_UMP45_E2_U.nif',None),
   clips=own(AR,'UMP45',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('UMP45','fire_base')]),fire_ads=(0,[('UMP45','fire_zoom')]),reload=(0,[('UMP45','reload_fast')]),reload_empty=(0,[('UMP45','reload_base')]),draw=(0,[('UMP45','swap_end')]))),
 'mp5':dict(rig=1,fam='MP5',dir='SM_MP5',body='MP5_Body.nif',   # (MP5 clips have the left hand on the handguard as the M4A1 set, not on the KRISS grip)
   parts=[('Magazine/SM_MP5/1st/MP5_MA.nif','wea_ma','MP5_Magazine'),('Barrel/SM_MP5/1st/MP5_BA.nif','wea_ba','Grip')],
   sight=('iron','IS_MP5','IS_MP5_E1_U.nif','IS_MP5_E2_U.nif','Dot'),
   clips=own(AR,'MP5',fire=(0,[('MP5','fire_base')]),fire_ads=(0,[('MP5','fire_zoom')]),reload=(0,[('MP5','reload_fast')]),
             reload_empty=(0,[('MP5','reload_base')]),draw=(0,[('MP5','swap_end')]))),
 'kriss':dict(rig=11,fam='Kriss',dir='SM_KRISS',body='KRISS_Body.nif',   # (b32: no sprint lift here any more: weapons.txt lifts the whole KRISS view 4 cm, which puts the borrowed M4A1 sprint where the SMG sprint footage has it, Sitara TC 269 s)
  
   parts=[('Magazine/SM_KRISS/1st/KRISS_MA.nif','wea_ma','Kriss_Magazine'),('Barrel/SM_KRISS/1st/KRISS_BA.nif','wea_ba','Grip')],
   sight=('scope','DIOTECH660','Dot'),
   clips=own(SMG,'Kriss',fire=(0,[('Kriss','fire_Grip')]),ads_idle=(1,[('Kriss','stand_zoom_Gri')]),draw=(0,[('Kriss','swap_end_Grip')]))),   # KRISS: the '_Grip' variants hold its front grip
 'l118':dict(rig=3,fam='L118A',dir='SR_L118',body='L118A_Body.nif',
   parts=[('Magazine/SR_L118/1st/L118A_MA.nif','wea_ma','L118A_Magazine'),('Barrel/SR_L118/1st/L118A_BA.nif','wea_ba','Grip')],
   sight=('scope','RIFLESCOPE','R_SC_CROSS'),skip=('BODY_HIDE',),overlay='ScopeSight/RIFLESCOPE/1st/R_SC_CROSS.dds',
   clips=own(AR,'L118A',idle=(1,[('M200','stand_idle')]),run=(1,[('M200','stand_run')]),walk=None,ads_in=None,
             fire=(0,[('L118A','fire_base')]),fire_ads=(0,[('L118A','fire_zoom')]),ads_idle=(1,[('M200','stand_zoom')]),bolt=(0,[('L118A','boltaction')]),
             reload=(0,[('L118A','reload_fast')]),reload_empty=(0,[('L118A','reload_base')]),draw=(0,[('L118A','swap_end')]),
             jump=(0,[('M200','jump_up_base')]),fall=(1,[('M200','jump_fall')]),land=(0,[('M200','jump_land')]))),
 '870':dict(rig=13,fam='M870',dir='SG_870MCS',body='870MCS_Body.nif',parts=[],lhand='charging_handle',   # (the left hand holds the pump)
   sight=('iron','IS_SF_COM_01','IS_SF_COM_01_E1_U.nif','IS_SF_COM_01_E2_U.nif',None),muzzle='Fire',
   clips=own(AR,'M870',idle=(1,[('M870','stand_idle')]),run=(1,[('M870','stand_run')]),walk=None,ads_in=None,ads_idle=('fire_ads0',),
             fire=(0,[('M870','fire_base')]),fire_ads=(0,[('M870','fire_zoom')]),reload=None,reload_empty=None,
             reload_start=(0,[('M870','ReloadStart')]),reload_loop=(0,[('M870','reload_base')]),reload_end=(0,[('M870','ReloadEnd')]),
             draw=(0,[('M870','swap_end')]),jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]))),   # (the M4A1 jump decodes badly with the SCAR / 870 rigs: decoded with the M4A1's, retargeted)
 'm249':dict(rig=45,fam='M249',dir='MG_M249',body='M249_Body.nif',
   parts=[('Barrel/MG_M249/1st/M249_BA.nif','wea_ba','Grip'),('Bipod/MG_M249/1st/M249_BI.nif','wea_bi','Grip')],
   sight=('iron','IS_M249','IS_M249_E1_U.nif','IS_M249_E2_U.nif',None),
   merge={'Dummy02':'Dummy01','Dummy04':'Dummy03','Dummy06':'Dummy05','Dummy07':'Dummy05','Dummy09':'Dummy10','Dummy11':'Dummy10','Dummy12':'Dummy10','Dummy15':'Dummy14'},
   clips=own(AR,'M249',idle=(1,[('M249','stand_idle')]),walk=(1,[('M249','stand_walk')]),run=(1,[('M249','stand_run')]),ads_in=None,
             fire=(0,[('M249','fire_base')]),fire_ads=(0,[('M249','fire_zoom')]),ads_idle=(1,[('M249','stand_zoom')]),
             reload=(0,[('M249','reload_base')]),reload_empty=(0,[('M249','reload_base')]),draw=(0,[('M249','swap_end')]))),
 'seburom5':dict(rig=4,fam='P99',dir='PL_SEBUROM5',body='SEBUROM5.nif',parts=[],sight=('pistol',),muzzle='Fire',
   clips=own(HG,'P99')),
 'mateba':dict(rig=8,fam='Mateba',dir='PL_MATEBA',body='MATEBA.nif',parts=[],sight=('pistol',),muzzle='Fire',
   clips=own(HG,'Mateba',fire=(0,[('Mateba','fire_base')]),fire_ads=(0,[('Mateba','fire_zoom')]),reload=(0,[('Mateba','reload_fast')]),
             reload_empty=(0,[('Mateba','reload_base')]),draw=(0,[('Mateba','swap_end')]))),
 # b105 (user: "the megatech knife still seems to glitch when you press the L trigger button to attack with it, it looks like it
 # comes out of place"): 'fire_right' exists only for the anim sets with 50 / 58 channels, which this rig (58 bones) has no
 # bone map for - it is decoded with the KRISS rig's map (11: the same Bip01 skeleton, so the arms are right) and retargeted by
 # name. That left the knife where rig 11 holds a gun (Weapon_R -> Grip in the HAND, 7.7 cm from the wrist, at the gun's
 # angle) instead of on this rig's Weapon_R_forearm (the knife's Grip sits on the forearm bone, constant in every clip of its
 # own): the blade jumped out of the fist for the whole slash. 'seat': in that clip the Grip and the blade's bones ride the
 # named bone exactly as the idle's first frame holds them.
 # 'nohf': with that map its first frame already has the head where the knife's idle has it (0, 4.9, 159.9); the head fix of
 # the other sets' clips moved the whole slash 2.4 cm sideways and ~10 cm away from the eye (a jump when it started).
 # 'turn': in its hook (frames 10-19, a third of a second) the elbow comes up 18 cm before the eye, 14 cm to the right - the
 # arms' mesh ends 5 cm above the elbow (the upper arm is not modelled), so its open end showed at the right of the view
 # ("you can see the back of the arm disconnected"). The pose is turned 16 deg to the right about the eye for those frames
 # (eased in while the hand is winding up off the view, out as it leaves): the open end stays outside the picture, the
 # blade still crosses the middle.
 'knife':dict(rig=7,fam='Knife',dir='CN_CW',body='CW.nif',parts=[],sight=None,seat=dict(fire2='Bip01_R_Forearm',cqc_ready='Bip01_R_Forearm',cqc='Bip01_R_Forearm'),turn=dict(fire2=(-16,5,9,19,23)),
   clips=dict(idle=(1,[('Knife','stand_idle')]),run=(1,[('Knife','stand_run')]),sprint=(1,[('Knife','sprint_base')]),sprint_in=(0,[('Knife','sprint_ready')]),
              fire=(0,[('Knife','fire_base')]),draw=(0,[('Knife','swap_end')]),holster=(0,[('Knife','swap_start')]),
              fire2=(0,[('Knife','fire_right',11,'nohf')]),   # the second slash (right-click without a stealth-kill prompt)
              cqc_ready=(0,[('@saito_knife','cqc_ready',11)]),cqc=(0,[('@saito_knife','cqc_saito',6)]),   # stealth kill (the 'saito' set: grab, stab down; the 'bato' set decodes badly)
              jump=(0,[('Knife','jump_up_base')]),fall=(1,[('Knife','jump_fall')]),land=(0,[('Knife','land_base')]),slide=(1,[('Knife','sliding_base')]))),
 # b70 (Ghost Assault: the user "make sure that the blade kill animations are included in the game like how they function in the
 # ghost assault gameplay footage"): the cyborg's arm blade, CN_AXE/1st/AXE.nif (B_Hand = the blade housing on the right
 # forearm, B_Device_A/B + B_Blade_01/02 = the two folding blades), with the game's own 1st-person 'Axe' set (rig 50):
 # its slashes and the two executions, front (cqc_ready + cqc_base: the left hand grabs the head, the blade goes up under
 # the chin - IGN "9 Minutes of Ghost Assault" 252.5-254 s) and from behind (back_ready + back_base). Only the '_cy'
 # variant is built (axe_cy: the cyborg's chrome arms). 'v01_3rd' is the third-person mesh kept in the same NIF (skipped);
 # 'AxeMain_Dummy' (not a rig bone; the skin was bound with it on the Grip: its skin -> bone matrices carry none of the
 # node's 10 degree tilt) rides on the Grip.
 # The set's head rests at (2.1, 18.2, 156.8), 15 cm behind its camera bone Camera01 (1.23, 2.84, 157.35, never animated):
 # the eye is that camera (the other sets: CAM).
 'axe':dict(rig=50,fam='Axe',dir='CN_AXE',body='AXE.nif',parts=[],sight=None,skip=('v01_3rd',),alias={'AxeMain_Dummy':'Grip'},
   head=(2.1,18.2,156.8),cam=(1.23,2.84,157.35),   # (b98: built with the operators' arms too - the selectable DOUBLE KATAR)
   clips=dict(idle=(1,[('Axe','stand_idle')]),run=(1,[('Axe','stand_run_forwar')]),sprint=(1,[('Axe','sprint_base')]),sprint_in=(0,[('Axe','sprint_ready')]),
              sprint_out=(0,[('Axe','sprint_stop2')]),fire=(0,[('Axe','fire_base')]),fire2=(0,[('Axe','fire_right')]),draw=(0,[('Axe','swap_end')]),
              # the front execution: the 'saito' cqc pair (the left hand holds the victim's head in front at eye height, the blade
              # comes up under it: the alpha footage's choreography; the set's own cqc_base decodes with the left hand low at the side)
              cqc_ready=(0,[('@saito_knife','cqc_ready',11)]),cqc=(0,[('@saito_knife','cqc_saito',6)]),
              cqcb_ready=(0,[('Axe','back_ready')]),cqcb=(0,[('Axe','back_base')]),
              jump=(0,[('Axe','jump_up_base')]),fall=(1,[('Axe','jump_fall_base')]),land=(0,[('Axe','land_base')]),
              climb=('concat',[[('Axe','climb_catch')],[('Axe','climb_up')],[('Axe','climb_land')]],0.8),
              vault=('concat',[[('Axe','window_up_A')],[('Axe','window_land_A')]],0.6))),
 'm67':dict(rig=5,fam='M67',dir='GR_M67',body='M67.nif',parts=[],sight=None,
   clips=dict(idle=(1,[('M67','stand_idle')]),run=(1,[('M67','stand_run')]),sprint=(1,[('M67','sprint_base')]),sprint_in=(0,[('M67','sprint_ready')]),
              sprint_out=(0,[('M67','sprint_stop2')]),cook=(0,[('M67','cooking')]),fire=(0,[('M67','fire_base')]),draw=(0,[('M67','swap_end')]),
              holster=(0,[('M67','swap_start')]),jump=(0,[('M67','jump_up_base')]),fall=(1,[('M67','jump_fall')]),land=(0,[('M67','land_base')]),
              slide=(1,[('M67','sliding_base')]))),
}
# b91: the sniper rifles of the user's SR video (v2.0 armory: every one with the 8x RIFLESCOPE, x12 variable zoom, aim 0.25 s).
# As the L118A: the M200 set's idle / run / aimed idle / jumps; each gun's own reloads and draw; its own fire / bolt where the
# install has them (the XM2010, M82 and SEBURO SR sets have no fire clip: the M200's). Semi-auto (no bolt): SA50, SEO90, WS2088.
def sr_clips(fam,fire,fire_ads=None,bolt=None,rl=None,draw=None):
    kw=dict(idle=(1,[('M200','stand_idle')]),run=(1,[('M200','stand_run')]),walk=None,ads_in=None,
            fire=(0,fire),fire_ads=(0,fire_ads or [('L118A','fire_zoom')]),ads_idle=(1,[('M200','stand_zoom')]),
            reload=(0,[(rl or fam,'reload_fast')]),reload_empty=(0,[(rl or fam,'reload_base')]),draw=(0,[(draw or rl or fam,'swap_end')]),
            jump=(0,[('M200','jump_up_base')]),fall=(1,[('M200','jump_fall')]),land=(0,[('M200','jump_land')]))
    if bolt: kw['bolt']=(0,bolt)
    return own(AR,fam,**kw)
def sr_cfg(rig,fam,d,body,mag,mbone,ba,bipod=None,**kw):
    parts=[('Magazine/%s/1st/%s'%(d,mag),'wea_ma',mbone),('Barrel/%s/1st/%s'%(d,ba),'wea_ba','Grip')]
    if bipod: parts.append(('Bipod/%s/1st/%s'%(d,bipod),'wea_bi','Grip'))
    return dict(rig=rig,fam=fam,dir=d,body=body,parts=parts,sight=('scope','RIFLESCOPE','R_SC_CROSS'),
                overlay='ScopeSight/RIFLESCOPE/1st/R_SC_CROSS.dds',**kw)
M200F=[('M200','fire_base'),('L118A','fire_base')]
CFG.update(
 cm2010=sr_cfg(49,'XM2010','SR_XM2010','XM2010_body.nif','XM2010_MA.nif','XM2010_Magazine','XM2010_BA.nif','XM2010_BI.nif',
   clips=sr_clips('XM2010',M200F,bolt=[('XM2010','boltaction')])),
 mm200=sr_cfg(16,'M200','SR_M200','M200_Body.nif','M200_MA.nif','M200_Magazine','M200_BA.nif',
   clips=sr_clips('M200',[('M200','fire_base')],bolt=[('M200','boltaction')])),
 m99d=sr_cfg(6,'M82','SR_M82','M82_Body.nif','M82_MA.nif','M98B_Magazine','M82_BA.nif',
   clips=sr_clips('M82',M200F,bolt=[('M82','boltaction')])),
 sa50=sr_cfg(40,'AS50','SR_AS50','AS50_Body.nif','AS50_MA.nif','AS50_Magazine','AS50_BA.nif','AS50_BI.nif',
   clips=sr_clips('AS50',[('AS50','fire_base')],[('AS50','fire_zoom')])),
 seburosr=sr_cfg(47,'SR_C30','SR_SEBURO_SR','SEBURO_SR_Body.nif','SEBURO_SR_MA.nif','SR_C30_Magazine','SEBURO_SR_BA.nif',
   clips=sr_clips('SR_C30',M200F,bolt=[('SR_C30','boltaction')])),
 seo90=sr_cfg(21,'MSG90','SR_MSG90','MSG90_Body.nif','MSG90_MA.nif','MSG90_Magazine','MSG90_BA.nif',
   clips=sr_clips('MSG90',[('MSG90','fire_base')],[('MSG90','fire_zoom')])),
 srv97=sr_cfg(20,'SV98','SR_SV98','SV98_Body.nif','SV98_MA.nif','SV98_Magazine','SV98_BA.nif',
   clips=sr_clips('SV98',[('SV98','fire_base')],bolt=[('SV98','boltaction')])),
 ws2088=sr_cfg(10,'WA2000','SR_WA2000','WA2000_Body.nif','WA2000_MA.nif','SVU_AS_Magazine','WA2000_BA.nif',   # (the beta WA2000: skinned to the SVU-AS bones)
   clips=sr_clips('WA2000',[('SVU_AS','fire_base')],[('SVU_AS','fire_zoom')])),
)
# b92: the pistols of the user's handgun video and the removed Mateba 2008M (its body is skinned to the 44 Magnum's bones),
# each on the rig its body is skinned to: the revolvers with the 44MG set's fire / reloads / draw, the 93-SE with its own
# full set, the P99-boned pistols (KCZ-85, SN 5-7, SEO-P226) with the P99 set as the SEBURO M5
def rev_clips(fam):   # (the reloads: its own when they decode, else the Mateba's from its rig 8, retargeted by bone name)
    return own(HG,fam,fire=(0,[(fam,'fire_base')]),fire_ads=(0,[(fam,'fire_zoom')]),reload=(0,[(fam,'reload_fast'),('Mateba','reload_fast',8,'local')]),
               reload_empty=(0,[(fam,'reload_base'),('Mateba','reload_base',8,'local')]),draw=(0,[(fam,'swap_end')]))
CFG.update(
 mag44=dict(rig=12,fam='44MG',dir='PL_44MG',body='44MG.nif',parts=[],sight=('pistol',),muzzle='Fire',clips=rev_clips('44MG')),
 m2008=dict(rig=12,fam='44MG',dir='PL_2008M',body='2008M.nif',parts=[],sight=('pistol',),muzzle='Fire',parts_rigid=1,
   clips=own(rev_clips('44MG'),'44MG',reload=(0,[('Mateba','reload_fast',8)]),reload_empty=(0,[('Mateba','reload_base',8)]))),
 b93se=dict(rig=17,fam='93R',dir='PL_93R',body='93R.nif',parts=[],sight=('pistol',),muzzle='Fire',
   clips=own(HG,'93R',idle=(1,[('93R','stand_idle')]),walk=(1,[('93R','stand_walk')]),run=(1,[('93R','stand_run')]),
             sprint=(1,[('93R','sprint_base')]),sprint_in=(0,[('93R','sprint_ready')]),fire=(0,[('93R','fire_base')]),fire_ads=(0,[('93R','fire_zoom')]),
             ads_idle=(1,[('93R','stand_zoom')]),ads_in=(0,[('93R','zoom_ready')]),reload=(0,[('93R','reload_fast')]),reload_empty=(0,[('93R','reload_base')]),
             draw=(0,[('93R','swap_end')]),jump=(0,[('93R','jump_up_base')]),fall=(1,[('93R','jump_fall')]),land=(0,[('93R','jump_land')]))),
 kcz85=dict(rig=4,fam='P99',dir='PL_BULStorm',body='BULStorm.nif',parts=[],sight=('pistol',),muzzle='Fire',clips=own(HG,'P99')),
 sn57=dict(rig=4,fam='P99',dir='PL_FN57',body='FN57.nif',parts=[],sight=('pistol',),muzzle='Fire',clips=own(HG,'P99')),
 p226=dict(rig=4,fam='P99',dir='PL_P226',body='P226.nif',parts=[],sight=('pistol',),muzzle='Fire',clips=own(HG,'P99')),
)
# b92: the light machine guns of the user's LMG video. MG33-A: the M249's bones and set (its own reload), its barrel and bipod;
# L86-SEO, MG36-A, SEBURO MG: the M4A1 set with their own reloads / draw (and the SEBURO MG's fire / sprint), magazines, bipods
CFG.update(
 mg33=dict(CFG['m249'],dir='MG_MG3',body='MG3_Body.nif',fam='MG3',
   parts=[('Barrel/MG_MG3/1st/MG3_BA.nif','wea_ba','Grip'),('Bipod/MG_MG3/1st/MG3_BI.nif','wea_bi','Grip')],
   sight=('iron','IS_M249','IS_M249_E1_U.nif','IS_M249_E2_U.nif',None),nodes={'E2':('E1',(39.76,0,0))},   # (the user's LMG video: the M249-style sights; the
   # MG3 body has no front-sight node: placed as far ahead of E1 as the M249's E2 is)
   clips=own(CFG['m249']['clips'],'MG3',reload=(0,[('MG3','reload_base'),('M249','reload_base')]),reload_empty=(0,[('MG3','reload_base'),('M249','reload_base')]))),
 l86=dict(rig=44,fam='LS86LSW',dir='MG_L86LSW',body='L86LSW_body.nif',   # (the drum magazine: the v2.0 armory's L86-SEO)

   parts=[('Magazine/MG_L86LSW/1st/L86LSW_2X_MA.nif','wea_ma','LS86LSW_Magazine'),('Bipod/MG_L86LSW/1st/L86LSW_BI.nif','wea_bi','Grip')],
   sight=('iron','IS_L86LSW','IS_L86LSW_E1_U.nif','IS_L86LSW_E2_U.nif',None),
   clips=own(AR,'LS86LSW',fire_ads=(0,[('LS86LSW','fire_zoom')]),reload=(0,[('LS86LSW','reload_fast')]),reload_empty=(0,[('LS86LSW','reload_base')]),
             draw=(0,[('LS86LSW','swap_end')]))),
 mg36=dict(rig=41,fam='MG36',dir='MG_MG36',body='MG36_body.nif',alias={'MG36_Trigger':'Grip'},   # (rig 41 has no trigger bone)
   parts=[('Magazine/MG_MG36/1st/MG36_2X_MA.nif','wea_ma','MG36_Magazine'),('Barrel/MG_MG36/1st/MG36_BA.nif','wea_ba','Grip'),
          ('Bipod/MG_MG36/1st/MG36_BI.nif','wea_bi','Grip')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),   # (the user's LMG video: a ring rear sight and post on the body's E1 / E2)
   clips=own(AR,'MG36',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),reload=(0,[('MG36','reload_fast')]),reload_empty=(0,[('MG36','reload_base')]),draw=(0,[('MG36','swap_end')]))),
 seburomg=dict(rig=36,fam='SeburoLMG',dir='MG_SEBURO_MG',body='SEBURO_MG_body.nif',
   parts=[('Magazine/MG_SEBURO_MG/1st/SEBURO_MG_MA.nif','wea_ma','AR_SEBRO_Magazine')],
   sight=('iron','IS_SEBURO_MG','IS_SEBURO_MG_E1_U.nif','IS_SEBURO_MG_E2_U.nif',None),
   clips=own(AR,'SeburoLMG',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('SeburoLMG','fire_base')]),fire_ads=(0,[('SeburoLMG','fire_zoom')]),reload=(0,[('SeburoLMG','reload_fas')]),
             reload_empty=(0,[('SeburoLMG','reload_bas')]),draw=(0,[('SeburoLMG','swap_end')]),sprint=(1,[('SeburoLMG','sprint_bas'),('M4A1','sprint_base')]),
             sprint_in=(0,[('SeburoLMG','sprint_rea'),('M4A1','sprint_ready')]))),   # (this set's file names stop at 'reload_bas' / 'sprint_rea')
)
# b97: the shotguns of the user's shotgun video (v2.0 armory). KSG-G1 and STA-12S2 (the UTS-15: the KSG's bones plus its two
# loading-port covers) pump and load shell by shell as the 870-ICS (ReloadStart / Reload / ReloadEnd; the STA's second tube: Reload_Right);
# USS 12 (USAS-12) is semi-automatic with a box magazine. The video shows the ring rear sight and post (IS_M4) on all three.
CFG.update(
 ksg=dict(rig=42,fam='KSG',dir='SG_KSG',body='KSG_Body.nif',parts=[],sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),muzzle='Fire',
   clips=own(AR,'KSG',jump=(0,[('M4A1','jump_up_base_M4_39ec8d98',22)]),fire=(0,[('KSG','fire_base')]),fire_ads=(0,[('KSG','fire_base')]),reload=None,reload_empty=None,
             reload_start=(0,[('KSG','ReloadStart')]),reload_loop=(0,[('KSG','Reload_base')]),reload_end=(0,[('KSG','ReloadEnd')]),draw=(0,[('KSG','swap_end')]))),
 sta12=dict(rig=51,fam='UTS15',dir='SG_UTS15',body='UTS15_Body.nif',parts=[],park=('KSG_bullet',(-0.1,-20.7,5.6),('reload_start','reload_loop','reload_end')),   # (b109: into the bolt, rig space cm)
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),muzzle='Fire',
   clips=own(AR,'UTS15',fire=(0,[('UTS15','fire_base')]),fire_ads=(0,[('UTS15','fire_zoom')]),reload=None,reload_empty=None,
             reload_start=(0,[('UTS15','ReloadStart')]),reload_loop=(0,[('UTS15','Reload_base')]),   # (its Reload_Right - the second tube - is not used: one loading clip)
             reload_end=(0,[('UTS15','ReloadEnd')]),draw=(0,[('UTS15','swap_end')]))),
 uss12=dict(rig=2,fam='USAS12',dir='SG_USAS12',body='USAS12_Body.nif',parts=[('Magazine/SG_USAS12/1st/USAS_MA.nif','wea_ma','USAS12_Magazine')],
   sight=('iron','IS_M4','IS_M4_E1_U.nif','IS_M4_E2_U.nif','Dot'),muzzle='Fire',
   clips=own(AR,'USAS12',fire=(0,[('USAS12','fire_base')]),fire_ads=(0,[('USAS12','fire_zoom')]),reload=(0,[('USAS12','reload_fast')]),
             reload_empty=(0,[('USAS12','reload_base')]),draw=(0,[('USAS12','swap_end')]))),
)
# b99 (user: the skill activation gesture, Demolition video 7:23.5-7:25; the bomb in the hands while planting, 4:45-4:52): the
# operator's arms alone on the M4A1's rig (the only set with these clips; the game plays them whatever weapon is out) -
# 'skill' = skill_start (1.37 s: the right forearm comes up, the left hand taps it), install_start 4.63 s + install_end 0.43 s
# (planting: the engine's 5.0 s), clear_start 2.53 s + clear_end 0.27 s (defusing: the gun stays in the right hand there).
# No gun meshes ('hands_only'). Each clip exists twice: for rig 22 (49-50 channels, no Grip key) and for rig 0 (52 channels,
# the Grip keyed in the right palm) - the rig-0 ones are taken, by name onto rig 22 (decode_cross). The camera is fixed in the
# character's space (CAM), so no head fix ('headfix': the head sinks 16 cm and tilts in them; with it the hands sat a third of
# the screen too high against the footage). install_end: the rig-22 export (the hands let go and drop; the rig-0 one decodes static).
FORE_SET=(1,2)   # the meshes of WP_CN_CW_NoActive.nif: 1 'CW' the housing round the forearm, 2 'Object01' the folded blade's linkage (0 'L3' is a coarse stand-in, not drawn)
FORE_Q=(0.41475516,0.0,0.0,0.90993305);FORE_T=(0.0,-0.95377103,0.0)   # the knife's Grip in the right forearm's space (constant in every clip of the knife's own set)
CFG['hands']=dict(rig=22,fam='M4A1',dir='AR_M4A1',body='M4A1_Body.nif',parts=[],sight=None,hands_only=True,headfix=False,fore=FORE_SET,
      clips=dict(idle=(1,[('M4A1','stand_idle')]),skill=(0,[('M4A1','skill_start_bas_bc7ee2c6',0)]),install_start=(0,[('M4A1','install_start_b_605dc654',0)]),
                 install_end=(0,[('M4A1','install_end_bas_796a0379',22)]),clear_start=(0,[('M4A1','clear_start_bas_18288401',0)]),clear_end=(0,[('M4A1','clear_end_base__3b69c8cc',0)]),
                 # b121 (user: "make sure that reiko's net jump work properly"; EP26 4:43-4:46, 7:16-7:22): the open hands of the Net Jump -
                 # the 'overload' set (rig 62: the hand-held charge's own clips; the PC shows them with nothing in the hand): the right
                 # hand low on the right, fingers spread; the left one swings into the picture in the sprint
                 nj_idle=(1,[('overload','stand_idle',62)]),nj_run=(1,[('overload','stand_run_f',62)]),nj_sprint=(1,[('overload','sprint_base',62)]),
                 nj_in=(0,[('overload','swap_end',62)]),nj_fall=(1,[('overload','jump_fall',62)])))
ORDER=['idle','walk','run','sprint','sprint_in','sprint_out','fire','fire2','cqc_ready','cqc','cqcb_ready','cqcb','fire_ads','ads_idle','ads_in','bolt','cook','reload','reload_empty',
       'reload_start','reload_loop','reload_end','draw','holster','jump','fall','land','slide','slide_fire','climb','vault',
       'skill','install_start','install_end','clear_start','clear_end',   # (b99: the hands' clips, CFG 'hands')
       'nj_idle','nj_run','nj_sprint','nj_in','nj_fall']   # (b121: the Net Jump's open hands)
# climbing onto a ledge / vaulting over cover (player.h mantle, 0.45-0.9 s): the game's 1st-person climb_* / window_*
# clips joined and re-timed to the engine's mantle ('concat', [parts], seconds); the left hand is on the obstacle there,
# so no left-hand IK. The M4A1 set's clips serve every gun (other families have only parts of the sequence).
NO_LIK=('climb','vault','skill','install_start','install_end','clear_start','clear_end','nj_idle','nj_run','nj_sprint','nj_in','nj_fall')   # (b99: + the hands' clips: no gun for the left hand to hold)
CLIMB=('concat',[[('M4A1','climb_start',22)],[('M4A1','climb_catch',22)],[('M4A1','climb_up',22)],[('M4A1','climb_land'),('M4A1','climb_land',22)]],0.8)
VAULT=('concat',[[('M4A1','window_up_A'),('M4A1','window_up_A',22)],[('M4A1','window_land_A'),('M4A1','window_land_A',22)]],0.6)
for _k,_c in CFG.items():
    if _c['sight'] is not None: _c['clips'].setdefault('climb',CLIMB);_c['clips'].setdefault('vault',VAULT)
for _k in ('seburom5','mateba'):   # pistols: the P99 set's vault and climb landing
    CFG[_k]['clips'].update(climb=('concat',CLIMB[1][:3]+[[('P99','climb_land')]],0.8),vault=('concat',[[('P99','window_up_A')],[('P99','window_land_A')]],0.6))
# b82 (the user's Batou footage, 0:10-3:10 and the screenshot at ~1:40: Batou's M4A1 carries a short tube red dot - the
# game's UH3 (GT1_RedDot: knurled turrets on top and at the right, tall mount, amber LENS_Blur2 glass) - with the front
# sight folded down): 'm4a1rd' = the M4A1 with that sight (the armory offers it next to the iron-sight M4A1; the tutorial
# keeps the irons, as its footage)
CFG['m4a1rd']=dict(CFG['m4a1'],parts=CFG['m4a1']['parts']+[('IronSight/IS_M4/1st/IS_M4_E2_D.nif','E2','Grip')],sight=('scope','UH3','DOT_02'),
                   clips=dict(CFG['m4a1']['clips']))
CFG['knife']['clips'].update(climb=('concat',[[('Knife','climb_catch',7)],[('Knife','climb_up',7)],[('Knife','climb_land')]],0.8),
                             vault=('concat',[[('Knife','window_up_A')],[('Knife','window_land_A')]],0.6))

def rigid(b,ms,W,bone,S2B,texmap,addl=lambda t:0):
    """rigid part meshes: opaque ones with textures up to 512, blended (reticle/lens/glow) ones up to 256"""
    op=[m for m in ms if not addl(m['tex'])];ad=[m for m in ms if addl(m['tex'])]
    if op: b.add_rigid(op,W,bone,S2B,'',addl,512,texmap)
    if ad: b.add_rigid(ad,W,bone,S2B,'',addl,256,texmap)
# Sight meshes, by texture (engine weapon.h wvm_draw: 0 opaque, 1 alpha blend, 2 additive, 3 opaque in hip only):
#  LENS_01 / LENS_02 = the glass vignettes (black with the texture's radial alpha: the dark edge of a scope's view),
#  LENS_03 / *_Lens / *CROSS* = tinted glass and reticles (alpha), Dot / DOT_glow / DOT_Blur = red dots (additive),
#  LENS_Blur* = the 'closed' lens look from outside (hip only; gone when aiming, as the lens caps inside the tube).
# Lens glass (b37, weapons agent): the eyepiece / holo glass is drawn by the engine's lens program (wpn_vm.h): blend bit 7.
#  LENS_01 (the frontmost dome of a tube sight) carries a baked texture: RGB = the closed lens at hip (the game's own
#  LENS_Blur*_D tint + LENS_Blur_S baked lens reflections + LENS_Blur_N1 dome shading), A = the aimed vignette (LENS_01's
#  alpha). The engine fades it from closed (hip, slightly see-through) to the vignette (aimed) with `ads` and adds a moving reflection of
#  lensenv.png (the game's CUBE.dds street, mirrored into a seamless panorama) tinted by the sight's coating colour.
#  The LENS_Blur* caps (and the L118A's eyepiece cap) are not built any more: the closed LENS_01 covers them.
LENS_BIT=128
LENS=dict(   # base: closed-lens tint (0..1), spec: LENS_Blur_S gain, coat: reflection colour x strength, kind: a tube lens's
            # opacity when closed (it dims the reticle behind it) or 1 = open glass, mid: reflection weight in the middle of
            # a closed lens (1 at the rim)
  TRICO_GOA=dict(base=(0.20,0.14,0.06),spec=0.35,coat=(0.95,0.50,0.25),kind=0.78,mid=0.3),     # ACOG: LENS_Blur2 (amber) + LENS_Blur3 (teal haze)
  GRAFT_N3=dict(base=(0.05,0.10,0.24),spec=0.35,coat=(0.45,0.55,1.0),kind=0.8,mid=0.3),       # CompM2 tube: LENS_Blur1 (blue)
  RIFLESCOPE=dict(base=(0.30,0.20,0.13),spec=0.30,coat=(0.62,0.40,0.27),kind=0.96,mid=0.45),   # L118A: copper all over (tutorial 257-266 s)
  DIOTECH660=dict(base=None,spec=0.0,coat=(0.30,0.42,0.45),kind=1,mid=0.3),                   # EOTech holo window: clear glass
  UH3=dict(base=(0.20,0.14,0.06),spec=0.35,coat=(0.95,0.50,0.25),kind=0.78,mid=0.3,open_tube=True))   # b82 M4A1 red dot (GT1): LENS_Blur2 (amber), as the ACOG;
            # open_tube: its caps go (split_optical clear) - the short tube shows the world at its far end through the glass, as the footage
            # (the other tubes look the same either way at hip: kept as they were)
def lens_glass_tex(sight):
    """256x256 RGBA: RGB = the closed lens (hip), A = LENS_01's vignette alpha (aimed); disc UV as LENS_01's"""
    L=LENS[sight];os.makedirs(TEXC,exist_ok=True);o=TEXC+'lensglass_%s.png'%sight
    T=W1+'Textures/';S=256
    A=np.asarray(Image.open(T+'LENS_01_D.dds').convert('RGBA').resize((S,S),Image.LANCZOS)).astype(float)[...,3]/255
    sp=np.asarray(Image.open(T+'LENS_Blur_S.dds').convert('RGB').resize((S,S),Image.LANCZOS)).astype(float)/255
    nm=np.asarray(Image.open(T+'LENS_Blur_N1.dds').convert('RGB').resize((S,S),Image.LANCZOS)).astype(float)/255*2-1
    nm/=np.maximum(np.linalg.norm(nm,axis=2,keepdims=True),1e-6)
    l=np.array([-0.45,0.55,0.70]);l/=np.linalg.norm(l)   # light from the upper left (u right, v down: y flipped below)
    sh=0.55+0.45*np.clip(nm[...,0]*l[0]-nm[...,1]*l[1]+nm[...,2]*l[2],0,1)
    yy,xx=np.mgrid[0:S,0:S];r=np.hypot((xx+0.5)/S*2-1,(yy+0.5)/S*2-1)
    rim=1-0.55*np.clip((r-0.72)/0.26,0,1)**1.5
    rgb=np.array(L['base'])[None,None]*sh[...,None]+sp*L['spec']*np.array([1.0,0.97,0.92])
    if sight=='TRICO_GOA': rgb+=np.array([0.025,0.066,0.08])*np.clip(1-r,0,1)[...,None]   # LENS_Blur3's teal haze
    rgb=np.clip(rgb*rim[...,None],0,1)
    # A is stored as 0.5 + 0.5 x vignette: never 0, so the mip levels keep the closed-lens colour (Pillow premultiplies
    # RGBA when it downsizes: where A = 0 the smaller mips went black - Vita3K showed a dark centre ringed by DXT noise)
    Image.fromarray(np.dstack([rgb*255,(0.5+0.5*A)*255]).round().astype('u1'),'RGBA').save(o);return o
def lens_env_tex():
    """the game's CUBE.dds (a street) and its mirror side by side: a horizontally seamless 256x128 reflection panorama"""
    o=TEXC+'lensenv.png';os.makedirs(TEXC,exist_ok=True)
    from PIL import ImageFilter
    c=Image.open(W1+'Textures/CUBE.dds').convert('RGB').resize((128,128),Image.LANCZOS)
    e=Image.new('RGB',(256,128));e.paste(c,(0,0));e.paste(c.transpose(Image.FLIP_LEFT_RIGHT),(128,0))
    # a lens reflection is soft: blurred (the picture's black doorway would read as a black hole in the lens), shadows lifted
    a=np.asarray(e.filter(ImageFilter.GaussianBlur(4)),float)/255;a=0.32+0.45*a**0.85
    Image.fromarray((a*255).round().astype('u1')).save(o);return o
def sight_blend(t):
    t=(t or '').lower()
    if t.startswith('gt1_reddot'): return 0   # b82: the UH3 body texture ('GT1_RedDot_D') is not a red dot
    if t.startswith('lensglass:') or t.startswith('lens_03'): return 1|LENS_BIT
    if t.startswith('lens_blur'): return 3
    if t.startswith(('lens_01','lens_02','lens_03')) or t.endswith('_lens.dds') or 'cross' in t: return 1
    if any(k in t for k in ('dot','glow','blur')): return 2
    return 0
TEXC=gitspaths.dirp('TMP','weapons','texcache')
def black_tex(path):
    """copy of a vignette texture with black colour and its own alpha"""
    os.makedirs(TEXC,exist_ok=True);o=TEXC+os.path.splitext(os.path.basename(path))[0]+'_black.png'
    if not os.path.exists(o):
        a=np.asarray(Image.open(path).convert('RGBA')).copy();a[...,:3]=0;Image.fromarray(a).save(o)
    return o
def sight_tex(t,dirs):
    if (t or '').lower().startswith('lensglass:'): return lens_glass_tex(t.split(':',1)[1])
    p=find_tex(t,dirs) or B_D
    return black_tex(p) if (t or '').lower().startswith(('lens_01','lens_02')) else p
def tri_circle_dist(T):
    """distance from the origin to each 2D triangle (n,3,2); 0 when the origin is inside"""
    d=np.full(len(T),np.inf)
    for k in range(3):
        a=T[:,k];b=T[:,(k+1)%3];ab=b-a;t=np.clip(-(a*ab).sum(1)/np.maximum((ab*ab).sum(1),1e-12),0,1);q=a+ab*t[:,None];d=np.minimum(d,np.hypot(q[:,0],q[:,1]))
    cr=lambda u,v:u[:,0]*v[:,1]-u[:,1]*v[:,0]
    s0=cr(T[:,1]-T[:,0],-T[:,0]);s1=cr(T[:,2]-T[:,1],-T[:,1]);s2=cr(T[:,0]-T[:,2],-T[:,2])
    ins=((s0>=0)&(s1>=0)&(s2>=0))|((s0<=0)&(s1<=0)&(s2<=0));d[ins]=0;return d
def split_optical(ms,ret,rlens,clear=False,anyv=False):
    """scope bodies: the triangles inside the optical path (lens caps, the inner tube) go into separate meshes that the
    engine draws only in hip view, so the world shows through the scope when aiming.
    b82 (clear=True, LENS open_tube; the user's Batou footage: a tube sight at hip is see-through glass - the dark inner
    tube, its lens rings, the reticle and the world through the far end): the solid discs across the tube (faces along the
    optical axis fanned from a vertex near the axis: the caps that closed the tube) are dropped; the walls and the annular
    lens rings stay hip-only as before. anyv (UH3): every triangle overlapping the aperture is in the optical path."""
    out=[];dropped=0
    for m in ms:
        if sight_blend(m['tex']): out.append(m);continue
        I=m['I'].reshape(-1,3);T=m['P'][I];C=T.mean(1);r=np.hypot(C[:,1]-ret[1],C[:,2]-ret[2]);cap=r<rlens
        if anyv: cap=tri_circle_dist(T[...,1:3]-ret[1:3])<rlens   # b82 (UH3): every triangle that overlaps the aperture (its big lens faces, fanned from the rim, left a square window by their centres)
        if not cap.any(): out.append(m);continue
        if (~cap).any(): out.append(dict(m,I=I[~cap].ravel()))
        if clear:
            n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]);nx=np.abs(n[:,0])/(np.linalg.norm(n,axis=1)+1e-12)
            rv=np.hypot(T[...,1]-ret[1],T[...,2]-ret[2]).min(1);disc=cap&(nx>0.7)&(rv<0.3*rlens);dropped+=int(disc.sum());cap=cap&~disc
        if cap.any(): out.append(dict(m,I=I[cap].ravel(),hip_only=1))
    if clear: print('   see-through tube: %d cap triangles dropped'%dropped)
    return out
# b63 (Ghost Assault): '<short>_cy' = the same weapon in the hands of the mode's cyborg (footage: white chrome arms with the
# blade): the arms are cut from the game's cyborg body (model/Cyborg/Cyborg/cy_male.nif, as vm/make_cyborg.py): every
# triangle whose corners hang (on average) at least 60 % on the upper arms, forearms, hands and fingers (no clavicles: the
# shoulders would sit at the camera), skinned to the same named bones, so the 1st-person clips move them as the hands
CY=gitspaths.dirp('ANIMOUT','model','Cyborg','Cyborg')
ARMB=('UpperArm','Forearm','ForeTwist','Hand','Finger')
CYFP_GAMMA=0.5;CYFP_HI=0.82
def cyborg_tex():
    import tempfile;d=tempfile.mkdtemp(prefix='vmcy_')
    D=np.asarray(Image.open(CY+'cyborgbody_D.dds').convert('RGBA')).astype(np.float32)
    E=np.asarray(Image.open(CY+'cyborgbody_e.dds').convert('RGB').resize((D.shape[1],D.shape[0]),Image.LANCZOS)).astype(np.float32)
    o=D.copy();o[...,:3]=np.clip(D[...,:3]+E*0.85,0,255)   # (b112: no x 1.22 - the reflection is there now, as make_cyborg.py)
    # b113 (the user, on b112's side by side with the footage: "the pale white parts are covering the hands ... make it match the
    # footage"): in the footage (the ALPHA build) the first-person arms are light silver all over, fingers included; cy_male's colour
    # map has light plates on a near-black hand, so only the plates showed. First person only: the map's tones lifted (out =
    # in^CYFP_GAMMA) - the fingers and the suit come up to the plates' family of greys, their drawn detail kept, the plates
    # barely move (0.80 -> 0.88). The third-person body (make_cyborg.py) keeps the map as it is.
    # b114 (the user: "remove the forearm armour completely ... the way the hands look in the footage without anything obstructing
    # them"): what reads as armour in first person is the hand piece's own white plating (back of the hand, thumb base, cuff - one
    # connected mesh with the palm: nothing to take away without holes; the forearm itself is out of the picture). So the whites
    # come DOWN as well (out = CYFP_HI x in^CYFP_GAMMA): plating 0.88 -> 0.74, fingers as b113 - one silver, no white parts.
    o[...,:3]=np.clip(255*CYFP_HI*(np.clip(D[...,:3],0,255)/255)**CYFP_GAMMA+E*0.85,0,255)
    p=os.path.join(d,'cyborg_D.png');Image.fromarray(o.astype(np.uint8)).save(p)
    Image.open(CY+'cyborgbody_s.dds').convert('RGBA').save(os.path.join(d,'cyborg_s.png'));MAT_ALIAS[p]=CY+'cyborgbody_D.dds';return p   # (b68: its material numbers)
def cyborg_arms():
    m=skinrun(CY+'cy_male.nif')[0][0]
    BIr=(m['BI'][0].view('<u2').reshape(-1,4) if m['BI'][0].shape[1]==8 else m['BI'][0]).astype(int)
    BW=m['BW'][0].view('<f4').reshape(len(BIr),-1);BW=np.c_[BW,1-BW.sum(1)] if BW.shape[1]==3 else BW/np.maximum(BW.sum(1,keepdims=True),1e-6)
    palr=m['PAL'][0].view('<u2').ravel();isarm=np.array([any(k in n.replace(' ','_') for k in ARMB) for n in m['bone_names']])
    aw=np.zeros(len(m['P']))
    for (vs,vc),(ps,pc) in zip(m['vregs'],m['PAL'][2]):
        for j in range(4): aw[vs:vs+vc]+=BW[vs:vs+vc,j]*isarm[palr[ps+BIr[vs:vs+vc,j]]]
    I2=[];reg=[];n0=0
    for (vs,vc),(is_,ic) in zip(m['vregs'],m['idxregs']):
        t=m['I'][is_:is_+ic].reshape(-1,3);keep=aw[t+vs].mean(1)>=0.6;t=t[keep].ravel();reg.append((n0,len(t)));I2.append(t);n0+=len(t)
    out=dict(m);out['I']=np.concatenate(I2).astype(m['I'].dtype);out['idxregs']=reg
    print('  cyborg arms: %d of %d triangles'%(n0//3,len(m['I'])//3));return out
LASTFR={}   # the last build's clips: name -> (rig-space frames, loop, the rig's bone names, the eye) - vm/reclip.py
def build(short):
    cy=short.endswith('_cy');base=short[:-3] if cy else short
    cfg=CFG[base];rig=R[cfg['rig']];RN=rig['names'];WD=W1+cfg['dir']+'/1st/'
    b=Builder(rig);b.spec=True;info=dict(short=short,rig=cfg['rig'],clips={})   # (b62: specular masks)
    if cy:   # b63: the cyborg's arms (Ghost Assault); body bones the 1st-person rig lacks (spine, neck, legs: only the cut's edge
        # vertices lean on them) follow the nearest spine bone it has
        ca=cyborg_arms();fb=next((n for n in ('Bip01_Spine2','Bip01_Spine1','Bip01_Spine','Bip01') if n in RN),RN[0])
        b.add_skinned(ca,cyborg_tex(),1024,{n:fb for n in ca['bone_names'] if n.replace(' ','_') not in RN})
        M_=b.meshes[-1];used=np.unique(M_['I']);rm=np.full(len(M_['P']),-1,np.int64);rm[used]=np.arange(len(used))   # (only the kept triangles' corners)
        for k in list(M_.keys()):
            if k!='I' and isinstance(M_[k],np.ndarray) and M_[k].ndim>=1 and len(M_[k])==len(rm): M_[k]=M_[k][used]
        M_['I']=rm[M_['I']]
        b.envmap(CY+'cybogCUBE.dds')   # b112: the cyborg body's own reflection picture (vm/matdb.py 'wgenv')
    else:   # ---- arms (as vm_m4a1: hand + darkened sleeve)
      hands=skinrun(H+'1stHand.nif')[0]
      if not os.path.exists(H+'1sthand2_dark.png'):
        Image.fromarray((np.asarray(Image.open(H+'1sthand2_d.dds').convert('RGB'),float)*0.5).astype('u1')).save(H+'1sthand2_dark.png')
      b.add_skinned(hands[0],H+'1sthand_d.dds',1024);b.add_skinned(hands[2],H+'1sthand2_dark.png')
      if cfg.get('hands_only'):   # b99: the bones the engine hangs the bomb on (no mesh uses them here)
        for _n in ('Weapon_R','Grip'): b.bone(_n)
      if cfg.get('fore'):   # b105: the folded blade's housing on the right forearm (CFG 'hands')
        _G=np.eye(4);_G[:3,:3]=qmat(np.array(FORE_Q));_G[:3,3]=FORE_T
        _fm=[m for i_,m in enumerate(static_meshes(W1+'CW_CW/1st/WP_CN_CW_NoActive.nif')) if i_ in cfg['fore']]
        rigid(b,_fm,_G,'Bip01_R_Forearm',np.eye(4),lambda t:find_tex(t,[W1+'CN_CW/1st/']+TEXD) or B_D)
    # ---- weapon body (skinned to this weapon's own rig bones)
    body=WD+cfg['body'];gun,Wg,ng,_=skinrun_fixed(body);gnode={v:k for k,v in ng.items()}
    def gW(n):   # (b92 cfg 'nodes': a node the body lacks, as another node moved in body space - the MG33-A's front sight E2)
        if n not in gnode and n in cfg.get('nodes',{}):
            src,off=cfg['nodes'][n];M=Wg[gnode[src]].copy();M[:3,3]+=np.array(off,float);return M
        return Wg[gnode[n]]
    iGrip=gW('Grip');iGi=np.linalg.inv(iGrip)
    mpar=mesh_parents(body);TEXMAX={}   # body textures: 1024 when a big mesh uses them (the first user decides the size in Builder.tex)
    for m in gun:
        tp=find_tex(skin_tex(body,m['block']),[WD]+[W1+d for d in cfg.get('texdirs',())]+TEXD)
        if mpar.get(m['block']) not in cfg.get('skip',()): TEXMAX[tp]=max(TEXMAX.get(tp,0),cfg.get('body_tex',1024) if len(m['P'])>1000 else 512)
    for m in gun:
        if mpar.get(m['block']) in cfg.get('skip',()) or cfg.get('hands_only'): continue   # e.g. L118A 'BODY_HIDE' (integrated bipod block, hidden in game); b99 hands_only: no gun
        if cfg.get('merge'):   # fold some belt links onto a neighbour (keeps the shader palette <= 48 entries)
            nb=list(m['bone_names']);s2b=list(m['sm']['s2b']);idx={n:i for i,n in enumerate(nb)}
            for a_,t_ in cfg['merge'].items():
                if a_ in idx and t_ in idx: nb[idx[a_]]=t_;s2b[idx[a_]]=s2b[idx[t_]]
            m=dict(m,bone_names=nb,sm=dict(m['sm'],s2b=s2b))
        t=skin_tex(body,m['block']);tp=find_tex(t,[WD]+[W1+d for d in cfg.get('texdirs',())]+TEXD)
        b.add_skinned(m,tp,TEXMAX.get(tp,512),cfg.get('alias'))
    # ---- rigid parts
    mu_fire=None
    import barrels;parts_,fr_=barrels.parts_for(base,cfg['parts'])   # b105: the barrel and muzzle piece the PC shows the gun with (vm/barrels.py)
    for nif,node,bone in parts_:
        ms=static_meshes(P1+nif);pd=os.path.dirname(P1+nif)+'/'
        tm=lambda t,pd=pd:find_tex(t,[pd]+TEXD) or B_D
        Wn_=gW(node);isba_=nif.startswith('Barrel/') and bool(fr_)
        if isba_ and 'mu_x' in fr_ and not fr_.get('barrel'):   # its own barrel, moved along its axis so that its 'we_mu' is where the muzzle piece is
            bw_=node_worlds(P1+nif);Wn_=Wn_.copy();Wn_[:3,3]+=Wn_[:3,0]/np.linalg.norm(Wn_[:3,0])*(fr_['mu_x']-(Wn_@bw_['we_mu'])[0,3])
        rigid(b,ms,Wn_,bone,np.linalg.inv(gW(bone)),tm)
        if isba_ and not cfg.get('no_mu'):
            bw=node_worlds(P1+nif);mud=P1+'Silencer/'+cfg['dir']+'/1st/'
            mus=[f for f in (os.listdir(mud) if os.path.isdir(mud) else []) if f.lower().endswith('_mu.nif')]
            Wm=None
            if mus and 'mu_x' in fr_: Wm=gW('wea_ba').copy();Wm[0,3]=fr_['mu_x']
            elif mus and 'we_mu' in bw: Wm=Wn_@bw['we_mu']
            elif mus and fr_.get('mu_end'): Wm=Wn_.copy();Wm[:3,3]+=Wm[:3,0]/np.linalg.norm(Wm[:3,0])*max(m['P'][:,0].max() for m in ms)
            if Wm is not None:
                mms=static_meshes(mud+mus[0]);rigid(b,mms,Wm,bone,np.linalg.inv(gW(bone)),lambda t,pd=mud:find_tex(t,[pd]+TEXD) or B_D)
                mw=node_worlds(mud+mus[0])
                mu_fire=(Wm@mw['Fire'])[:3,3] if 'Fire' in mw else (Wm@np.array([max(m['P'][:,0].max() for m in mms),0,0,1.0]))[:3]
                info['muzzle_part']=mus[0]
            continue
        # b103 (user: "the tip of the kriss vector is not showing up"): the gun's own muzzle piece (weapon\Parts\Silencer\<dir>\
        # 1st\<GUN>_MU.nif - the PC's default part in that slot) on its barrel's 'we_mu' node; none of b1-b102's guns had it.
        # Guns without a barrel part have no such node (their bodies run to the muzzle): left as they were.
        if node=='wea_ba' and not cfg.get('no_mu'):
            bw=node_worlds(P1+nif);mud=os.path.dirname(P1+nif).replace('/Barrel/','/Silencer/')+'/'
            mus=[f for f in (os.listdir(mud) if os.path.isdir(mud) else []) if f.lower().endswith('_mu.nif')]
            if 'we_mu' in bw and mus:
                Wm=gW(node)@bw['we_mu'];mms=static_meshes(mud+mus[0])
                rigid(b,mms,Wm,bone,np.linalg.inv(gW(bone)),lambda t,pd=mud:find_tex(t,[pd]+TEXD) or B_D)
                mw=node_worlds(mud+mus[0])
                mu_fire=(Wm@mw['Fire'])[:3,3] if 'Fire' in mw else (Wm@np.array([max(m['P'][:,0].max() for m in mms),0,0,1.0]))[:3]
                info['muzzle_part']=mus[0]
    # ---- sight -> aim point (body NIF space)
    sight=cfg['sight'];aim=None;info['sight']=sight[0] if sight else 'none';lensatt={}
    addl=sight_blend
    if sight and sight[0]=='scope':
        sd=SS+sight[1]+'/1st/';ms=[m for m in static_meshes(sd+sight[1]+'.nif') if not (m['tex'] or '').lower().startswith('dot_blur')]
        ret=[m for m in ms if (m['tex'] or '').lower().startswith(sight[2].lower())][0]['P'].mean(0)
        lens=[m for m in ms if (m['tex'] or '').lower().startswith(('lens_01','lens_02'))]
        if lens:   # a tube sight (scope / tube red dot): open the optical path when aiming
            rl=max(np.hypot(m['P'][:,1]-ret[1],m['P'][:,2]-ret[2]).max() for m in lens);ms=split_optical(ms,ret,rl*0.98,clear=LENS.get(sight[1],{}).get('open_tube',False),anyv=sight[1]=='UH3');info['lens_r']=round(float(rl),2)
        if sight[1] in LENS:   # lens glass (engine lens program): LENS_01 gets the baked closed-lens texture; the caps go
            ms=[m for m in ms if not (m['tex'] or '').lower().startswith('lens_blur') and not (m.get('hip_only') and m['tex'].lower().startswith('rifescope_d') and len(m['P'])<600 and np.ptp(m['P'][m['I']][:,0])<0.6)]
            ms=[dict(m,tex='LENSGLASS:'+sight[1]) if (m['tex'] or '').lower().startswith('lens_01') else m for m in ms]
            lensatt=dict(lensenv=np.array([b.tex(lens_env_tex(),256),LENS[sight[1]]['kind'],LENS[sight[1]]['mid']],float),lenscoat=np.array(LENS[sight[1]]['coat'],float))
        hip=[m for m in ms if m.get('hip_only')];ms=[m for m in ms if not m.get('hip_only')]
        rigid(b,ms,gW('SC_DUMMY'),'Grip',iGi,lambda t:sight_tex(t,[sd]+TEXD),addl)
        if hip: b.add_rigid(hip,gW('SC_DUMMY'),'Grip',iGi,'',lambda t:3,512,lambda t:sight_tex(t,[sd]+TEXD))
        aim=(gW('SC_DUMMY')@np.r_[ret,1.0])[:3];info['sight_part']=sight[1]
    elif sight and sight[0]=='iron':
        sd=IS+sight[1]+'/1st/';front=None
        for k,(f,node) in enumerate(((sight[2],'E1'),(sight[3],'E2'))):
            if f is None:   # (b89: a one-piece sight on E1: sight[4] = its aim point in the E1 node's space)
                aim=(gW('E1')@np.r_[np.array(sight[4],float),1.0])[:3];continue
            ms=static_meshes(sd+f);rigid(b,ms,gW(node),'Grip',iGi,lambda t:sight_tex(t,[sd]+TEXD),addl)
            if k==1:
                if sight[4]: front=[m for m in ms if (m['tex'] or '').lower().startswith(sight[4].lower())][0]['P'].mean(0)
                else:   # front post tip: highest vertex near the centre line
                    P=np.concatenate([m['P'] for m in ms if not addl(m['tex'])]);c=P[np.abs(P[:,1])<0.35];front=c[np.argmax(c[:,2])]
                aim=(gW(node)@np.r_[front,1.0])[:3]
        info['sight_part']=sight[1]
    elif sight and sight[0]=='builtin':   # b89: sights moulded into the body (FAMAS carry handle): the front post = the highest point near the centre line in the front half
        Pb=np.concatenate([m['P'] for m in gun]);xm=Pb[:,0].min()+0.5*np.ptp(Pb[:,0]);c=Pb[(np.abs(Pb[:,1])<0.4)&(Pb[:,0]>xm)]
        aim=c[np.argmax(c[:,2])].copy();aim[2]-=sight[1] if len(sight)>1 else 0;info['sight_part']='builtin'   # (sight[1]: the sight line below the handle's top: FAMAS 2.25 cm, fitted to the user's video 6:33-6:48, the post in the ring)
    elif sight and sight[0]=='node':   # b90: sights moulded into the body, the sight line marked by its own node (PP-2000 / P90: E2, the front sight)
        aim=gW(sight[1])[:3,3].copy();aim[2]+=sight[2] if len(sight)>2 else 0;info['sight_part']='node:'+sight[1]
    elif sight and sight[0]=='pistol':   # sights are part of the slide: aim line at aim1 height, front post near the muzzle
        z=gW('aim1')[2,3];Pb=np.concatenate([m['P'] for m in gun]);c=Pb[(np.abs(Pb[:,1])<0.5)&(Pb[:,2]>z-1.5)&(Pb[:,0]>Pb[:,0].max()*0.6)]
        fx=c[np.argmax(c[:,2]),0] if len(c) else gW('Fire')[0,3]-4;aim=np.array([fx,0,z])
    # ---- clips
    iG=RN.index('Grip');iWp=rig['parent'][iG];KIDS=descendants(rig['parent'],iG)
    Pb,Wb=bind_world(rig);BLOC={i:(qmat(Wb[iG]).T@(Pb[i]-Pb[iG]),qmat(Wb[iG]).T@qmat(Wb[i])) for i in KIDS}   # parts relative to Grip at bind
    if cfg.get('parts_rigid'):   # b92 (the Mateba 2008M: skinned to bones of the 44 Magnum's rig that sit elsewhere on that gun): its parts
        GiB=np.linalg.inv(gW('Grip'))   # held where its own body places them, in every clip
        for i in KIDS:
            if RN[i] in gnode: M_=GiB@gW(RN[i]);BLOC[i]=(M_[:3,3].copy(),M_[:3,:3]/np.linalg.norm(M_[:3,0]))
    def ang(Rm): return np.degrees(np.arccos(np.clip((np.trace(Rm)-1)/2,-1,1)))
    def load(cands):   # first family/key with a valid decode; among its hash variants the one decoded with the best-scoring bone map
        for c_ in cands:
            if len(c_)>=3:   # (family, key, source rig[, 'local']): another anim set's clip, retargeted (decode_cross)
                lo=len(c_)>3 and c_[3]=='local'
                for p in pre(c_[0],c_[1]):
                    fr=decode_cross(p,c_[2],rig,head=HEAD+np.array(cfg['cam'],float)-CAM if cfg.get('cam') else None,local=lo,headfix=cfg.get('headfix',True) and not (len(c_)>3 and c_[3]=='nohf'))   # (b70: the same place before this set's eye)
                    if fr: return fr,p,(cfg['fam'] if lo else '@cross' if c_[0].startswith('@') else c_[0])   # another weapon's set: foreign (parts re-seated at this weapon's Grip); 'local': its parts moved on this gun's bones (b92)
                continue
            fam,key=c_
            for strict in (True,False):   # (lenient pass: throws/falls start with the hand raised behind the head)
                best=None
                for p in pre(fam,key):
                    fr,h,k=decode_valid(A,p,rig,MAPS,strict=strict,heads=[np.array(cfg['head'])] if cfg.get('head') else None)
                    if fr is not None and (best is None or k<best[2]): best=(fr,p,k)
                if best: return best[0],best[1],fam
        return None,None,None
    idle,ip,_=load(cfg['clips']['idle'][1]);P0,W0=idle[0];GLOC=qmat(W0[iWp]).T@qmat(W0[iG]);GB=qmat(Wb[iWp]).T@qmat(Wb[iG])
    def fixup(fr,foreign):
        # no Grip rotation key (Grip-in-Weapon_R stays at the rig bind, which differs from the held angle): the gun would hang
        # at the bind angle -> re-seat it at the idle's held angle. (Draws that key a changing Grip angle are left alone.)
        GI=[qmat(W[iWp]).T@qmat(W[iG]) for P,W in fr]
        regrip=all(ang(GB.T@g)<2 for g in GI) and ang(GB.T@GLOC)>30 and not cfg.get('hands_only')   # (b99 hands_only: no gun to re-seat)
        out=[]
        for P,W in fr:
            P=P.copy();W=W.copy()
            if regrip:
                Ro=qmat(W[iG]);Rn=qmat(W[iWp])@GLOC
                for i in KIDS: P[i]=P[iG]+Rn@(Ro.T@(P[i]-P[iG]));W[i]=mat2q(Rn@(Ro.T@qmat(W[i])))
                W[iG]=mat2q(Rn)
            if foreign:   # parts animated for another weapon's geometry: hold them at this weapon's bind
                RG=qmat(W[iG])
                for i in KIDS: P[i]=P[iG]+RG@BLOC[i][0];W[i]=mat2q(RG@BLOC[i][1])
            out.append((P,W))
        return out,regrip
    # left hand on the gun: reference = where the idle's frame 0 holds it, relative to the gun bone it grips (Grip, or the
    # 870's pump); clips that leave the left arm unanimated, or hold the hand at a fixed wrong spot (borrowed clips,
    # re-gripped guns), get it put back there by two-bone IK (vmw.lhand_ik). Knife / grenade: no two-handed hold.
    iL=RN.index('Bip01_L_Hand');iTB=RN.index(cfg.get('lhand','Grip'));LREF=[];PARK=[];LREF0=[]
    LARM=[RN.index(n) for n in ('Bip01_L_UpperArm','Bip01_L_Forearm','Bip01_L_Hand')]
    def lrel(P,W): R_=qmat(W[iTB]);return R_.T@(P[iL]-P[iTB]),R_.T@qmat(W[iL])
    def lfix(fr):
        if not LREF or cfg['sight'] is None: return fr,None
        dev=[np.linalg.norm(lrel(P,W)[0]-LREF[0]) for P,W in fr]
        if max(dev)<0.5: return fr,None
        still=max(max(ang((qmat(fr[0][1][rig['parent'][i]]).T@qmat(fr[0][1][i])).T@(qmat(W[rig['parent'][i]]).T@qmat(W[i]))) for P,W in fr) for i in LARM)<0.5
        dv0=[np.linalg.norm(lrel(P,W)[0]-LREF0[0]) for P,W in fr] if LREF0 else dev   # (b116 lhand_shift: "holds the gun" is judged against the borrowed idle's own hold)
        if not (still or (max(dv0)-min(dv0)<2.0 and max(dv0)<6.0)): return fr,None   # (the hand leaves the gun: reloads, draws, one-handed sprints)
        out=[];gap=0
        for P,W in fr:
            R_=qmat(W[iTB]);P2,W2,g=lhand_ik(P,W,rig['parent'],RN,P[iTB]+R_@LREF[0],R_@LREF[1]);out.append((P2,W2));gap=max(gap,g)
        return out,dict(dev=round(max(dev),1),still=bool(still),gap=round(gap,1))
    baked={}
    for name in ORDER:
        ent=cfg['clips'].get(name)
        if ent is None: continue
        if ent[0]=='fire_ads0':   # no aimed-idle clip: hold the first frame of the aimed fire clip
            fr=[baked['fire_ads'][0]]*2;b.bake(name,fr,1,RC,CAM);baked[name]=fr;info['clips'][name]=dict(src='fire_ads frame 0',nf=2,loop=1);continue
        if ent[0]=='concat':   # parts joined, re-timed to ent[2] seconds (nearest frames at 30 fps)
            parts=[];srcs=[];rg=False;foreign=False
            for cands in ent[1]:
                fr_,p_,fam_=load(cands)
                if fr_ is None: parts=None;break
                fg=(fam_.lower()!=cfg['fam'].lower() and not (cfg['fam']=='P99' and fam_=='p99')) or bool(cfg.get('parts_rigid'));fr_,r_=fixup(fr_,fg);parts+=fr_;srcs.append(p_[5:]);rg|=r_;foreign|=fg
            if not parts: print('  ! no valid clip for',name,ent[1]);continue
            n=max(2,int(round(ent[2]*30))+1);fr=[parts[min(len(parts)-1,int(round(i*(len(parts)-1)/(n-1))))] for i in range(n)];loop=0;p='     '+'+'.join(srcs)
        else:
            loop,cands=ent;fr,p,fam=load(cands)
            if fr is None: print('  ! no valid clip for',name,cands);continue
            foreign=(fam.lower()!=cfg['fam'].lower() and not (cfg['fam']=='P99' and fam=='p99')) or bool(cfg.get('parts_rigid'))
            fr,rg=fixup(fr,foreign)
        if name in cfg.get('seat',{}):   # b105: the weapon's bones held on a body bone as in the idle's first frame (the knife's retargeted second slash)
            iS=RN.index(cfg['seat'][name]);Rs0=qmat(W0[iS]);rel={i:(Rs0.T@(P0[i]-P0[iS]),Rs0.T@qmat(W0[i])) for i in [iWp,iG]+list(KIDS)};out_=[]
            for P,W in fr:
                P=P.copy();W=W.copy();Rs=qmat(W[iS])
                for i,(p_,r_) in rel.items(): P[i]=P[iS]+Rs@p_;W[i]=mat2q(Rs@r_)
                out_.append((P,W))
            fr=out_
        if os.environ.get('VM_SEATDBG'):   # (test: how far the weapon's Grip moves against the forearm / the hand, per clip)
            for bn in ('Bip01_R_Forearm','Bip01_R_Hand'):
                if bn in RN:
                    iS_=RN.index(bn);r0=qmat(W0[iS_]).T@(P0[iG]-P0[iS_]);dv=[float(np.linalg.norm(qmat(W[iS_]).T@(P[iG]-P[iS_])-r0)) for P,W in fr]
                    print('   seatdbg %-10s Grip vs %-16s: first %.1f max %.1f cm (frame %d of %d)'%(name,bn,dv[0],max(dv),int(np.argmax(dv)),len(dv)))
        if name in cfg.get('turn',{}):   # b105: the pose turned about the eye's vertical axis for a part of the clip (deg about the rig's z - negative = to the right of the view; ramp in f0-f1, out f2-f3)
            dg,f0_,f1_,f2_,f3_=cfg['turn'][name];cam_=np.array(cfg.get('cam',CAM),float);out_=[]
            for k_,(P,W) in enumerate(fr):
                e_=min(1.0,max(0.0,(k_-f0_)/float(f1_-f0_)),max(0.0,(f3_-k_)/float(f3_-f2_)));e_=e_*e_*(3-2*e_);a_=np.radians(dg*e_)
                Rz=np.array([[np.cos(a_),-np.sin(a_),0],[np.sin(a_),np.cos(a_),0],[0,0,1.0]])
                out_.append((cam_+(np.asarray(P,float)-cam_)@Rz.T,np.array([mat2q(Rz@qmat(q)) for q in W])))
            fr=out_
        if cfg.get('park') and name not in cfg['park'][2]:   # b109: a loose part kept at one place in the gun outside the clips that move it (the STA-12S2's shell: its node rests UNDER the receiver, in the open - the armory's INSPECT showed it floating there; the KSG's rests inside the stock)
            iPk=RN.index(cfg['park'][0]);dl_=np.array(cfg['park'][1],float)
            if name=='idle': PARK[:]=[qmat(fr[0][1][iG]).T@(fr[0][0][iPk]+dl_-fr[0][0][iG]),qmat(fr[0][1][iG]).T@qmat(fr[0][1][iPk])]
            if PARK:
                out_=[]
                for P,W in fr:
                    P=np.array(P,float).copy();W=np.array(W,float).copy();Rg=qmat(W[iG]);P[iPk]=P[iG]+Rg@PARK[0];W[iPk]=mat2q(Rg@PARK[1]);out_.append((P,W))
                fr=out_
        if name=='idle': LREF[:]=lrel(*fr[0])
        if name=='idle' and (cfg.get('lhand_shift') or os.environ.get('GITSFA_LSHIFT')):   # b116: the left hand's place on THIS gun (the idle is another set's: its hand holds that gun's handguard)
            sh_=np.array([float(x) for x in os.environ['GITSFA_LSHIFT'].split(',')] if os.environ.get('GITSFA_LSHIFT') else cfg['lhand_shift'],float)
            print('   left hand (in the grip bone\'s frame): %s -> %s'%(np.round(LREF[0],1),np.round(LREF[0]+sh_,1)))
            LREF0[:]=[LREF[0].copy()];LREF[0]=LREF[0]+sh_;out_=[]
            for P,W in fr: R_=qmat(W[iTB]);P2,W2,g=lhand_ik(P,W,rig['parent'],RN,P[iTB]+R_@LREF[0],R_@LREF[1]);out_.append((P2,W2))
            fr=out_
        fr,lk=lfix(fr) if name not in NO_LIK else (fr,None)
        if name in cfg.get('clip_off',{}):   # a borrowed pose moved for this gun's size (rig space, cm, z up)
            o_=np.array(cfg['clip_off'][name],float);fr=[(P+o_,W) for P,W in fr]
        b.bake(name,fr,loop,RC,np.array(cfg.get('cam',CAM),float));baked[name]=fr
        LASTFR[name]=(fr,loop,RN,np.array(cfg.get('cam',CAM),float))   # (b105: for vm/reclip.py)
        info['clips'][name]=dict(src=p[5:],nf=len(fr),loop=loop,regrip=bool(rg),foreign=bool(foreign))
        if lk: info['clips'][name]['lhand_ik']=lk
    if 'reload_start' in baked:   # shotgun: one-shell reload as a single clip for the generic reload path
        for nm in ('reload','reload_empty'):
            fr=baked['reload_start']+baked['reload_loop']+baked['reload_end'];b.bake(nm,fr,0,RC,CAM);info['clips'][nm]=dict(src='reload_start+reload_loop+reload_end',nf=len(fr),loop=0)
    if 'walk' not in baked and 'ads_idle' in baked:   # aimed walk falls back to the aimed idle
        b.bake('walk',baked['ads_idle'],1,RC,CAM);info['clips']['walk']=dict(src='= ads_idle',nf=len(baked['ads_idle']),loop=1)
    # ---- attachments (Grip space unless noted)
    if cfg.get('muzzle'): mz=gW(cfg['muzzle'])[:3,3]
    elif mu_fire is not None: mz=mu_fire   # b103: the muzzle piece's own 'Fire' point (its flash point in the game)
    elif any(n=='wea_ba' for n,_,_ in [(p[1],0,0) for p in cfg['parts']]):
        bar=[p for p in cfg['parts'] if p[1]=='wea_ba'][0];ms=static_meshes(P1+bar[0]);tip=max(m['P'][:,0].max() for m in ms)
        mz=(gW('wea_ba')@np.array([tip,0,0,1.0]))[:3]
    else:   # (b90: the front-most body vertex near the centre line at the barrel's height - wea_ba - not the body's median height)
        Pa=np.concatenate([m['P'] for m in gun]);zb=gW('wea_ba')[2,3] if 'wea_ba' in gnode else np.median(Pa[:,2])
        c=Pa[(np.abs(Pa[:,1])<1.5)&(np.abs(Pa[:,2]-zb)<3)];c=c if len(c) else Pa;mz=np.array([c[:,0].max(),0,zb])
    att={'mz_grip':(iGi@np.r_[mz,1.0])[:3],'muzzle':mz} if cfg['sight'] is not None else {}   # (knife / grenade: no muzzle)
    if aim is not None: att['sight']=(iGi@np.r_[aim,1.0])[:3]
    if base=='knife':   # b63: the blade's tip (the body vertex farthest from the Grip) and a point 30 % of the way out (Grip space): main.c's slash trail
        Pa=np.concatenate([m['P'] for m in gun]);g_=iGrip[:3,3];dd=np.linalg.norm(Pa-g_,axis=1);tip=Pa[dd.argmax()]
        att['blade_tip']=(iGi@np.r_[tip,1.0])[:3];att['blade_base']=(iGi@np.r_[g_+(tip-g_)*0.3,1.0])[:3];info['blade_cm']=round(float(dd.max()),1)
    if base=='axe' and 'idle' in baked:   # b70: the blades fold out on their own bones: the tip where the idle holds it (Grip space)
        P0_,W0_=baked['idle'][0];GW_=np.eye(4);GW_[:3,:3]=qmat(W0_[iG]);GW_[:3,3]=P0_[iG];iGW_=np.linalg.inv(GW_);pts=[]
        for M_ in b.meshes[1:] if cy else b.meshes:
            if M_['blend']: continue
            dom=M_['BI'][np.arange(len(M_['BI'])),np.argmax(M_['BW'],1)]
            for k_ in np.unique(dom):
                sl,S_=b.palette[k_];ri=b.bones[sl];BW_=np.eye(4);BW_[:3,:3]=qmat(W0_[ri]);BW_[:3,3]=P0_[ri]
                v_=M_['P'][dom==k_];pts.append(((iGW_@BW_@np.r_[S_[:3],[[0,0,0,1]]])@np.c_[v_,np.ones(len(v_))].T).T[:,:3])
        Pa=np.concatenate(pts);dd=np.linalg.norm(Pa,axis=1);tip=Pa[dd.argmax()]
        att['blade_tip']=tip;att['blade_base']=tip*0.3;info['blade_cm']=round(float(dd.max()),1)
    if 'hud1' in gnode: att['hud1']=(iGi@gW('hud1'))[:3,3]
    if cfg.get('overlay'):   # sniper: the scoped view (vm/make_scope_overlay.py, premultiplied RGBA) as an extra texture;
        # attachment 'scope' = (texture index, half size in screen heights, 2 = overlay version)
        import make_scope_overlay as SO
        op=gitspaths.tmp('weapons','scope_%s.png'%short);SO.build(1024).save(op)
        att['scope']=np.array([b.tex(op,1024),SO.HALF,2],float)
    att.update(lensatt)
    b.att=att
    # ADS: where the aim point sits (camera space) in the aimed idle -> viewmodel offset that puts it on the view axis
    if aim is not None and 'ads_idle' in baked:
        P,W=baked['ads_idle'][0];sg=RC@(P[iG]-CAM)+RC@qmat(W[iG])@att['sight']
        info['ads_aim_cam']=np.round(sg,3).tolist();info['ads_offset']=[round(-sg[0],2),round(-sg[1],2),-15.6]
    if 'hud1' in att and 'idle' in baked:
        P,W=baked['idle'][0];info['hud1_cam_idle']=np.round(RC@(P[iG]-CAM)+RC@qmat(W[iG])@att['hud1'],2).tolist()
    info['att']={k:np.round(v,3).tolist() for k,v in att.items()}
    assert len(b.palette)<=48,'palette too large (%d)'%len(b.palette)
    pal_windows(b)   # (each mesh indexes its own window of the palette: the Vita shader cannot reach rows >= ~128)
    path=OUT+'/vm_%s.gvm'%short;sz=b.write(path)
    info.update(bytes=sz,bones=len(b.bones),palette=len(b.palette),meshes=len(b.meshes),textures=len(b.texlist))
    print(short,'->',path,sz,'bytes; bones',len(b.bones),'palette',len(b.palette),'meshes',len(b.meshes),'tex',len(b.texlist))
    return info

if __name__=='__main__':
    which=[k for k in CFG if not CFG[k].get('cy_only')] if sys.argv[1]=='all' else sys.argv[1].split(',')   # (b70: 'axe' only as axe_cy)
    allinfo={}
    jp=gitspaths.tmp('weapons','vminfo.json')
    if os.path.exists(jp): allinfo=json.load(open(jp))
    for s in which:
        allinfo[s]=build(s);json.dump(allinfo,open(jp,'w'),indent=1)
        for k,v in allinfo[s]['clips'].items(): print('   %-12s %s'%(k,v))
        print('   ads',allinfo[s].get('ads_offset'),'hud1',allinfo[s].get('hud1_cam_idle'),'att',allinfo[s]['att'])
