# Helpers for the per-weapon viewmodel builder (make_vm_weapon.py). Kept apart from gvmlib.py so that
# vm_c30.gvm / vm_m4a1.gvm keep building byte-identically.
import sys,os,io,struct,contextlib;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from gvmlib import *

def fix_rigs(path,R):
    """rig.rigs() reads the bind-pose pointers at a fixed offset, which assumes a 2-word bone mask (<=64 bones);
    rigs with more bones (M249: 71) carry a longer mask -> re-read their bind pose"""
    d=open(path,'rb').read()
    for r in R:
        n=len(r['names'])
        if n<=64 or r.get('base') is None: continue
        nw=(n+31)//32;po,qo=struct.unpack_from('<II',d,r['base']+0x48+4*(nw-2))
        r['pos']=np.frombuffer(d,'<f4',n*4,r['base']+po).reshape(-1,4)[:,:3];r['quat']=np.frombuffer(d,'<f4',n*4,r['base']+qo).reshape(-1,4)
    return R

HEAD=np.array([2.4,4.3,159.0]);HEAD2=np.array([0.0,4.9,159.9])   # 1st-person eye rest positions (gun sets / knife+grenade sets)
def decode_valid(adir,prefix,rig,maps,tol=6.0,strict=True,heads=None):
    """decode a clip with the first candidate bone map (best score first) whose frame 0 has Bip01_Head at a 1st-person
    eye rest position and the Grip within reach of the right hand (rejects 'garbage mapping' decodes).
    -> (frames [(P,W)...], header, candidate index) or (None, header, -1)"""
    f=[x for x in os.listdir(adir) if x.startswith(prefix)][0]
    h=hdr(adir+f);C=decode_clip(h);RN=rig['names'];iH=RN.index('Bip01_Head');iG=RN.index('Grip');iR=RN.index('Bip01_R_Hand')
    def pz(i):
        if C['mode']==1: lp,lq=pose(rig,C,i);return fk(rig['parent'],lp,lq)
        return pose_world(rig,C,i)
    with contextlib.redirect_stdout(io.StringIO()):
        cands=choose_map3(h,C,maps,rig,return_all=True)
    for k,(sc,m) in enumerate(cands):
        C['map']=m['list'];P,W=pz(0)
        if min(np.linalg.norm(P[iH]-h_) for h_ in [HEAD,HEAD2]+list(heads or []))<tol and np.linalg.norm(P[iR]-P[iG])<30 \
           and (not strict or (P[iR][1]<0 and P[iG][1]<0 and P[iR][2]<P[iH][2])):   # hand and gun in front of the body, below the eyes
            return [pz(i) for i in range(C['nf'])],h,k
    return None,h,-1

def descendants(parent,root):
    out=[];ch={}
    for i,p in enumerate(parent): ch.setdefault(p,[]).append(i)
    st=list(ch.get(root,[]))
    while st:
        i=st.pop();out.append(i);st+=ch.get(i,[])
    return out

def bind_world(rig):
    return fk(rig['parent'],[p.astype(float) for p in rig['pos']],[q.astype(float) for q in rig['quat']])

def find_tex(name,dirs):
    """case-insensitive lookup of a texture file name in a list of directories"""
    if not name: return None
    base=os.path.splitext(name)[0].lower()
    for d in dirs:
        if not os.path.isdir(d): continue
        for f in os.listdir(d):
            if os.path.splitext(f)[0].lower()==base and f.lower().endswith(('.dds','.png')): return d+f
    return None

def skin_tex(path,block):
    """texture file name of a skinned NiMesh block (skin.run() does not report it)"""
    h=nifmesh.load(path);d=h['d'];B=h['blocks'];t,o,s=B[block];a=nifmesh.mesh(d,o)
    for p in a['props']:
        if B[p][0]=='NiTexturingProperty':
            raw=d[B[p][1]:B[p][1]+B[p][2]];v=struct.unpack_from('<I',raw,19)[0]
            if v<len(B) and B[v][0]=='NiSourceTexture': return nifmesh.S(h,struct.unpack_from('<I',d,B[v][1]+13)[0])
    return None

def skinrun_fixed(path):
    """skin.run(), but with the right NiSkinningMeshModifier per mesh: skin.run takes the first u32 in the mesh block
    that happens to index such a modifier, which for some bodies (L118A) is another mesh's modifier"""
    import skin
    gun,Wg,ng,x=skinrun(path)
    h=nifmesh.load(path);d=h['d'];B=h['blocks']
    for m in gun:
        need=int(m['PAL'][0].view('<u2').max())+1 if m['PAL'] is not None else 1
        if len(m['sm']['bones'])>=need: continue
        t,o,s=B[m['block']];raw=d[o:o+s];refs=[]
        for i in range(0,len(raw)-3):
            v=struct.unpack_from('<I',raw,i)[0]
            if v<len(B) and B[v][0]=='NiSkinningMeshModifier': refs.append(v)
        for v in reversed(refs):
            sm=skin.skinmod(d,B[v][1])
            if len(sm['bones'])>=need:
                m['sm']=sm;m['bone_names']=[ng.get(b) for b in sm['bones']];m['bone_world']=[Wg.get(b) for b in sm['bones']];break
    return gun,Wg,ng,x

def mesh_parents(path):
    """NiMesh block -> name of its parent NiNode"""
    h=nifmesh.load(path);d=h['d'];B=h['blocks'];out={}
    for k,(t,o,s) in enumerate(B):
        if t=='NiNode':
            a=nifmesh.node(d,o)
            for c in a['children']: out[c]=nifmesh.S(h,a['name'])
    return out

# ---- left hand on the gun (two-bone IK), see make_vm_weapon.py build(): many of the game's 1st-person clips (the aimed
# idle / aimed fire / aimed walk / slides) do not animate the left arm at all - it stays in the idle arm pose while the
# gun moves to the eye, so the hand floats ~10 cm beside the handguard (the original game puts it back with IK).
def _rot_between(a,b):
    a=a/np.linalg.norm(a);b=b/np.linalg.norm(b);v=np.cross(a,b);s=np.linalg.norm(v);c=float(np.dot(a,b))
    if s<1e-9: return np.eye(3)
    K=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]])/s;th=np.arctan2(s,c)
    return np.eye(3)+np.sin(th)*K+(1-np.cos(th))*K@K
def _axis_rot(u,th):
    u=u/np.linalg.norm(u);K=np.array([[0,-u[2],u[1]],[u[2],0,-u[0]],[-u[1],u[0],0]])
    return np.eye(3)+np.sin(th)*K+(1-np.cos(th))*K@K
def lhand_ik(P,W,parent,names,Pt,Rt,twist=0.5):
    """moves the left arm of one frame (world P (n,3), W quats (n,4)) so that Bip01_L_Hand sits at Pt with rotation Rt:
    upper arm + forearm solved in the plane of the current elbow, the hand set to Rt, its fingers carried along;
    Bip01_L_ForeTwist (a child of the upper arm placed on the forearm) follows the forearm and takes `twist` of the
    hand's roll about the forearm. Returns new (P, W) arrays."""
    P=np.array(P,float).copy();W=np.array(W,float).copy();N=list(names)
    iU,iF,iH=N.index('Bip01_L_UpperArm'),N.index('Bip01_L_Forearm'),N.index('Bip01_L_Hand');iT=N.index('Bip01_L_ForeTwist') if 'Bip01_L_ForeTwist' in N else -1
    under=lambda i:[i]+descendants(parent,i)
    def apply(ids,Rm,o):
        for i in ids: P[i]=o+Rm@(P[i]-o);W[i]=mat2q(Rm@qmat(W[i]))
    a,b,c=P[iU].copy(),P[iF].copy(),P[iH].copy();l1=np.linalg.norm(b-a);l2=np.linalg.norm(c-b)
    dv=Pt-a;d=np.linalg.norm(dv);n=dv/d;d=min(max(d,abs(l1-l2)+1e-3),l1+l2-1e-3)
    pole=(b-a)-np.dot(b-a,n)*n
    if np.linalg.norm(pole)<1e-6: pole=np.array([0,0,-1.0])-np.dot([0,0,-1.0],n)*n
    pole/=np.linalg.norm(pole);ca=(l1*l1+d*d-l2*l2)/(2*l1*d);e=a+n*l1*ca+pole*l1*np.sqrt(max(0,1-ca*ca))
    fore=under(iF)+([iT] if iT>=0 else [])
    apply(under(iU),_rot_between(b-a,e-a),a)   # (ForeTwist is under the upper arm: it moves with it)
    apply(fore,_rot_between(P[iH]-e,a+n*d-e),e)
    # hand: target rotation, fingers carried; ForeTwist rolls by a part of the hand's roll about the forearm axis
    R3=Rt@qmat(W[iH]).T;u=(P[iH]-e)/np.linalg.norm(P[iH]-e)
    if iT>=0:
        q3=mat2q(R3);ang=2*np.arctan2(np.dot(q3[:3],u),q3[3])   # twist about u (swing-twist decomposition)
        Rt_=_axis_rot(u,twist*ang);W[iT]=mat2q(Rt_@qmat(W[iT]))
    hp=P[iH].copy();apply(under(iH),R3,hp)
    off=Pt-P[iH]
    for i in under(iH): P[i]+=off   # (unreachable target: the stretched arm stops short, the hand still lands on the gun)
    return P,W,float(np.linalg.norm(off))

# ---- per-mesh palette windows. The Vita's runtime Cg compiler does not give the skinning shader all 144 rows of
# 'pal[144]': the arms (palette entries 0-35, rows 0-107) are always right, the M249 body (entries 44-47, rows 132-143)
# never draws (b28 device shots 33 / 46-47), and the 3rd-person characters with > 42 entries showed spikes. So each
# mesh gets the window of the palette it uses: its vertices index the palette from the window start, which is stored
# in bits 8-15 of the mesh's blend word (engine model.h / wpn.h upload pal[base ...] for that mesh).
PAL_WIN=36   # entries per window (108 rows, the most the arms need and known to work on the device)
def pal_windows(b):
    for m in b.meshes:
        used=sorted(set(int(x) for x in np.asarray(m['BI'])[np.asarray(m['BW'])>0].ravel()))
        base=used[0] if used else 0
        assert used[-1]-base<PAL_WIN,'mesh uses palette entries %d..%d (> %d)'%(base,used[-1],PAL_WIN)
        BI=np.asarray(m['BI']).astype(int)-base;BI[(np.asarray(m['BW'])<=0)|(BI<0)]=0   # (unweighted slots: entry 0 of the window)
        m['BI']=BI;m['blend']=(m['blend']&0xff)|(base<<8)
