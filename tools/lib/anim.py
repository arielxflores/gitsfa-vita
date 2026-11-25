from qsa import *
from qsa_fixed import decode_block   # rotations: tqa(mean) * tqa(residual), see notes/qsa-format.md
from rig import *
def tqa(v):
    v=np.asarray(v,float);s=v@v;return np.r_[2*v,1-s]/(1+s)
def decode_clip(h):
    """returns dict: frames F, pos[F][ch] or None, quat (as tqa vectors) per channel; mode"""
    pc,qc=const_tracks(h)
    bo=block_offsets(h)
    nf=int(round(h['dur']*h['fps']))+1
    P={c:np.repeat(v[None],nf,0) for c,v in pc.items()}
    Q={c:np.repeat(v[None],nf,0) for c,v in qc.items()}
    for c in h['L'][2]: P[c]=np.zeros((nf,3))
    for c in h['L'][3]: Q[c]=np.zeros((nf,3))
    for st,sz,b in bo:
        B=decode_block(h,b)
        n=B['ns']
        for k,c in enumerate(h['L'][2]): P[c][st:st+n]=B['P'][:n,k][:max(0,min(n,nf-st))]
        for k,c in enumerate(h['L'][3]): Q[c][st:st+n]=B['Q'][:n,k][:max(0,min(n,nf-st))]
    return dict(nf=nf,P=P,Q=Q,mode=h['a28'],root=h['cnt'][0],rq=h['ident'],anim=set(h['L'][3]))
def bone_of(clip,c):
    m=clip.get('map')
    if m is None: return c+1
    return m[c+1] if c+1<len(m) else None
def pose(rig,clip,fr,world_mode=None):
    n=len(rig['names']);lp=[p.astype(float).copy() for p in rig['pos']];lq=[q.astype(float).copy() for q in rig['quat']]
    for c,v in clip['P'].items():
        b=bone_of(clip,c)
        if b is not None: lp[b]=v[fr]
    for c,v in clip['Q'].items():
        b=bone_of(clip,c)
        if b is not None: lq[b]=tqa(v[fr])
    return lp,lq

def conj(q): return np.r_[-q[:3],q[3]]
def pose_world(rig,clip,fr,consts_world=False,root_mode='traj'):
    """world-mode: animated quats are in a common frame"""
    par=rig['parent'];n=len(par)
    lp=[p.astype(float).copy() for p in rig['pos']];lq=[q.astype(float).copy() for q in rig['quat']]
    for c,v in clip['P'].items():
        b=bone_of(clip,c)
        if b is not None: lp[b]=v[fr]
    worldset={}
    for c,v in clip['Q'].items():
        b=bone_of(clip,c)
        if b is None: continue
        if c in clip['anim'] or consts_world: worldset[b]=tqa(v[fr])
        else: lq[b]=tqa(v[fr])
    # FK with override
    Wq=[None]*n;P=[None]*n
    for i in range(n):
        p=par[i]
        if p<0: Wq[i]=lq[i];P[i]=lp[i];continue
        if i in worldset:
            Wq[i]=worldset[i]
        else:
            Wq[i]=qmul(Wq[p],lq[i])
        P[i]=P[p]+qrot(Wq[p],lp[i])
    return np.array(P),np.array(Wq)
