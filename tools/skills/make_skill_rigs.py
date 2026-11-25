# Rigged skill deployables with the game's OWN animation clips (decoded from GIS_turret.nmb / GIS_smalltachi.nmb with our
# /root/anim/py decoders; never NaturalMotion code). They replace the rigid sk_turret.gvm / sk_minitachi.gvm of
# make_skill_models.py (same file names, same attachments), so the engine plays deploy / idle / fire / damage / destroy:
#   sk_turret.gvm     TURRET.nif skinned to the turret rig (19 bones) + the Tier-2 grenade pod (LAST mesh, rigid on the head).
#                     Clips: deploy (turret_enable: unfolds), idle (turret_enable_idle: standing), undeploy (turret_enable_stop:
#                     folds up), folded (turret_idle), turn, fire_start,
#                     fire (turret_fire_loop), fire_end, damage, destroy. The engine adds its own aim on top: yaw turns the
#                     subtree of Bip01_body02, pitch the subtree of Bip01_head (skills.h sk_turret_aim).
#   sk_minitachi.gvm  CB_TACHIKOMA_MINI.nif skinned to the smalltachi rig (19 bones). Clips: deploy (smalltachi_enable, 4.7 s:
#                     pops up 68 cm at 0.5-1.6 s, lands, unfolds), idle (smalltachi_enable_idle: standing), folded (smalltachi_idle),
#                     walk (run_forward), damage, destroy, jump, land.
# Space: the rig's own (z up, cm, the object faces -y), as the characters (gvmlib.Builder.bake without a transform).
# Input: the anim sets copied from Data/anim (GIS_turret_runtimeBinary, GIS_smalltachi_runtimeBinary) into ANIM.
# usage: python3 make_skill_rigs.py [outdir] [anim dir]
import sys,os,io,json,contextlib
HERE=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,os.path.join(HERE,'..','..','vm'));sys.path.insert(0,HERE)
from vmw import *
from sklib import RAW,node_tree,tex_png,static_parts
OUT=sys.argv[1] if len(sys.argv)>1 else os.path.join(HERE,'..','..','build','data')
import gitspaths
ANIM=sys.argv[2] if len(sys.argv)>2 else gitspaths.dirp('SKANIM')
N=RAW+'model/Npc/'

def rigs_any(path):
    """rig.rigs(), but the bind-pose pointers follow a bone mask of (n+31)//32 words (rig.rigs assumes 2: <= 64 bones;
    the EMP rig has 20 bones -> 1 word, the M249 rig 71 -> 3; vmw.fix_rigs handles only the latter)"""
    import re,struct
    d=open(path,'rb').read();out=[]
    for m in re.finditer(b'CharacterWorldSpaceTM\x00',d):
        o=m.start();e=d.index(b'\xcd\xcd',o);names=[n.decode() for n in d[o:e].split(b'\x00') if n];e=(e+3)&~3
        for k in range(e,e+64,4):
            sz,al,n=struct.unpack_from('<III',d,k)
            if al==16 and n==len(names): break
        else: continue
        n=len(names);po,qo=struct.unpack_from('<II',d,k+0x48+4*((n+31)//32-2))
        out.append(dict(names=names,off=o,base=k,pos=np.frombuffer(d,'<f4',n*4,k+po).reshape(-1,4)[:,:3],quat=np.frombuffer(d,'<f4',n*4,k+qo).reshape(-1,4)))
    return out

def decode(adir,nmb,clips):
    R=add_parents(adir+nmb,rigs_any(adir+nmb));rig=R[0];maps=find_maps(adir+nmb);out=[]
    for name,key,loop in clips:
        f=[x for x in os.listdir(adir) if x.startswith(key)][0]
        h=hdr(adir+f);C=decode_clip(h)
        with contextlib.redirect_stdout(io.StringIO()): best=choose_map3(h,C,maps,rig)
        if best: C['map']=best[1]['list']
        fr=[]
        for i in range(C['nf']):
            if C['mode']==1: lp,lq=pose(rig,C,i);fr.append(fk(rig['parent'],lp,lq))
            else: fr.append(pose_world(rig,C,i))
        out.append((name,fr,loop,f))
    return rig,out

def glow_map(path):   # b71: a NIF glow map on uv set 0 at the colour map's size (gvmlib.Builder.tex bakes it into alpha)
    def f(W,H):
        E=np.asarray(Image.open(path).convert('RGB').resize((W,H),Image.BILINEAR)).astype(np.float64)/255
        print('glow: %s, %.1f %% of the texels lit'%(os.path.basename(path),100*(E.max(2)>0.05).mean()));return E if E.max()>0.02 else None
    return f
def build(name,nif,adir,nmb,clips,tex,texsize,extra=None,att=None,glow=None):
    rig,dec=decode(adir,nmb,clips);b=Builder(rig)
    ms,Wn,nn,_=skinrun(nif)
    for m in ms: b.add_skinned(m,tex,texsize,emis=glow_map(glow) if glow and os.path.exists(glow) else None)
    if extra: extra(b,rig)
    info={}
    for cn,fr,loop,f in dec: b.bake(cn,fr,loop);info[cn]=dict(src=f,nf=len(fr),loop=loop)
    b.att=att or {}
    assert len(b.palette)<=48,'palette %d'%len(b.palette)
    pal_windows(b)   # per-mesh palette windows (model.h pbase; the Vita's skinning shader reaches only ~42 entries)
    path=os.path.join(OUT,name);sz=b.write(path)
    print(name,sz,'bytes; bones',len(b.bones),'palette',len(b.palette),'meshes',[(len(x['P']),len(x['I'])//3) for x in b.meshes]);print(json.dumps(info))
    return path

def turret():
    nif=N+'CB_Turret/TURRET.nif';W,par,h=node_tree(nif)
    def pod(b,rig):   # the Tier-2 grenade pod: static meshes under the Grenade node, rigid on the head (pitch part) -> LAST mesh
        g=[x for x in static_parts(nif) if x['tex'] and 'grenade' in x['tex'].lower()]
        S2B=np.linalg.inv(W['Bip01_head']) if 'Bip01_head' in W else np.eye(4)
        for x in g: b.add_rigid([dict(P=x['P'],N=x['N'],UV=x['UV'],I=x['I'],tex=x['tex'])],np.eye(4),'Bip01_head',S2B,'',texmap=lambda t:tex_png('CB_Turret_Grenade_D'),maxs=256)
    att={'yaw':W['turret_yaw'][:3,3],'pitch':W['turret_pitch'][:3,3],'muzzle':W['Turret_Fire'][:3,3],'laser':W['Turret_Laser'][:3,3],
         'gren':W['Grenade_Fire01'][:3,3],'tag':W['name_tag'][:3,3],'fx':W['Effect_turret'][:3,3]}
    A=ANIM+'GIS_turret_runtimeBinary/'
    clips=[('deploy','turret_enable_Turret_enable',0),('idle','turret_enable_idle',1),('undeploy','turret_enable_stop',0),
           ('folded','turret_idle',1),('turn','turret_turn',1),('fire_start','turret_fire_start',0),('fire','turret_fire_loop',1),
           ('fire_end','turret_fire_end',0),('damage','turret_damage',0),('destroy','turret_destroy',0)]
    return build('sk_turret.gvm',nif,A,'GIS_turret.nmb',clips,tex_png('CB_TURRET_D'),512,pod,att)

def minitachi():
    nif=N+'CB_SPIDERMINE/CB_TACHIKOMA_MINI.nif';W,par,h=node_tree(nif)
    att={'eye':W['Bip01 body'][:3,3] if 'Bip01 body' in W else np.array([0,-10,20.0])}
    A=ANIM+'GIS_smalltachi_runtimeBinary/'
    clips=[('deploy','smalltachi_enable_smalltachi',0),('idle','smalltachi_enable_idle',1),('folded','smalltachi_idle',1),
           ('walk','smalltachi_run_forward',1),('damage','smalltachi_damage',0),('destroy','smalltachi_destroy',0),
           ('jump','smalltachi_jump_up',0),('land','smalltachi_jump_land',0)]
    ms,_,_,_=skinrun(nif);t=skin_tex(nif,ms[0]['block']);tp=find_tex(t,[N+'CB_SPIDERMINE/',N+'CB_Tachikoma_mini/']) or tex_png('CB_TACHIKOMA_MINI_B_D')
    return build('sk_minitachi.gvm',nif,A,'GIS_smalltachi.nmb',clips,tp,256,None,att,glow=N+'CB_SPIDERMINE/B_glow.dds' if os.path.basename(tp).lower().startswith('cb_tachikoma_mini_b_d') else None)   # (b71: its eyes / lights glow)

def emp():
    nif=N+'CB_EMP/CB_EMP.nif';W,par,h=node_tree(nif)
    att={'body':W['Bip01_body'][:3,3],'top':W['Bip01_upperbody'][:3,3],'head':W['Bip01_Head'][:3,3]}
    A=ANIM+'GIS_emp_runtimeBinary/'
    clips=[('deploy','EMP_Operate_EMP_operate',0),('spin_start','EMP_Operate_Turn_Start',0),('spin','EMP_Operate_Turn_Roop',1),
           ('undeploy','EMP_Operate_End',0),('folded','EMP_Idle',1),('idle','EMP_Operate_Turn_Roop',1)]
    ms,_,_,_=skinrun(nif);t=skin_tex(nif,ms[0]['block']);tp=find_tex(t,[N+'CB_EMP/']) or tex_png('EMP_D')
    return build('sk_emp.gvm',nif,A,'GIS_EMP.nmb',clips,tp,512,None,att)

if __name__=='__main__':
    only=sys.argv[3:] if len(sys.argv)>3 else ['turret','minitachi','emp']
    for f in only: globals()[f]()
