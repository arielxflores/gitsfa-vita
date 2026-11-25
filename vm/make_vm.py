# vm_c30.gvm: first-person arms + Seburo C-30 with the game's own (decoded) morpheme clips, baked in camera space.
import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from gvmlib import *
import gitspaths
OUT=sys.argv[1] if len(sys.argv)>1 else gitspaths.root('DATA')
A=gitspaths.dirp('ANIMBIN','GIS_base_1st_runtimeBinary')
AO=gitspaths.dirp('ANIMOUT');NMB=A+'GIS_base_1st.nmb'
R=add_parents(NMB,rigs(NMB));RIG=R[57];MAPS=find_maps(NMB)
CAM=np.array([0,0,156.4])   # eye: Camera_dummy (152.6) + 3.8 cm, matches the zoom clips' sight line and gameplay footage
RC=np.array([[-1,0,0],[0,0,1],[0,1,0]],float)   # rig (facing -y, z up) -> camera (x right, y up, z back)
CLIPS=[('idle','base_M4A1_1st_stand_idle_base_7a04bbe8',1),('walk','base_M4A1_1st_stand_walk_forw_b515dd63',1),
 ('run','base_M4A1_1st_stand_run_forwa_5bc78edf',1),('sprint','base_AR_C30_1st_sprint_base_A_767c5191',1),
 ('sprint_in','base_AR_C30_1st_sprint_ready__59740a4a',0),('sprint_out','base_M4A1_1st_sprint_stop2_ba_c7c09808',0),
 ('fire','base_AR_C30_1st_Fire_base_AR__f5428530',0),('fire_ads','base_M4A1_1st_fire_zoom_base__cec66364',0),
 ('reload','base_AR_C30_1st_reload_fast_b_52558247',0),('reload_empty','base_AR_C30_1st_reload_base_A_6ddef698',0),
 ('draw','base_AR_C30_1st_swap_end_base_baabb679',0),('holster','base_M4A1_1st_swap_start_base_7c254202',0),
 ('jump','base_M4A1_1st_jump_up_base_M4_5fc58ebb',0),('fall','base_M4A1_1st_jump_fall_base__aa5e06b5',1),('land','base_M4A1_1st_land_base_M4A1__6445fa3f',0)]
b=Builder(RIG);b.spec=True   # (b62: specular masks)
hands=skinrun(AO+'model/ActorGhost/CommonHand/1stHand.nif')[0]
gun,Wg,ng,_=skinrun(AO+'weapon/AR_SEBURO_C30/1st/SEBURO_C30_body.nif');gnode={v:k for k,v in ng.items()};gW=lambda n:Wg[gnode[n]]
H=AO+'model/ActorGhost/CommonHand/'
b.add_skinned(hands[0],H+'1sthand_d.dds',1024);b.add_skinned(hands[2],H+'1sthand2_d.dds')
for m in gun: b.add_skinned(m,AO+'weapon/AR_SEBURO_C30/1st/AR_C30_D.dds')
b.add_rigid(static_meshes(AO+'weapon/Parts/Magazine/AR_SEBURO_C30/1st/SEBURO_C30_MA.nif'),gW('wea_ma'),'AR_SEBRO_Magazine',np.linalg.inv(gW('AR_SEBRO_Magazine')),AO+'weapon/Parts/Magazine/AR_SEBURO_C30/1st/')
b.add_rigid(static_meshes(AO+'weapon/Parts/IronSight/IS_SEBURO_C30/1st/IS_SEBURO_C30_E1_U.nif'),gW('E1'),'Grip',np.linalg.inv(gW('Grip')),AO+'weapon/Parts/IronSight/IS_SEBURO_C30/1st/',lambda t:2 if t and t.upper().startswith('DOT') else 0)
for ent in CLIPS:
    fr,h=clip_frames(A,ent[1],RIG,MAPS)
    if len(ent)>3: fr=fr[ent[3][0]:ent[3][1]+1]
    b.bake(ent[0],fr,ent[2],RC,CAM);print('clip',ent[0],len(fr),'frames mode',h['a28'])
b.att={'mz_grip':(np.linalg.inv(gW('Grip'))@np.r_[gW('wea_bi')[:3,3],1.0])[:3],'muzzle':gW('wea_bi')[:3,3],'sight':(gW('E1')@np.array([-4.582,0,1.494,1.0]))[:3],'e1':gW('E1')[:3,3]}
print('vm_c30.gvm',b.write(OUT+'/vm_c30.gvm'),'bytes, bones',len(b.bones),'palette',len(b.palette))
