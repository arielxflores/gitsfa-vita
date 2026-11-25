import struct
import numpy as np
def find_maps(path):
    """RigToAnimMap-like records: [..][u32 nRig][u32 nWords][bits][u32 nAnimCh][u16 n][u16 n2][u16 14][u16 0][u16 list n+1]"""
    d=open(path,'rb').read();out=[]
    i=0
    pat=struct.pack('<H',14)+b'\0\0'+struct.pack('<H',0)
    while True:
        i=d.find(pat,i+1)
        if i<0: break
        n,n2=struct.unpack_from('<HH',d,i-4)
        nch=struct.unpack_from('<I',d,i-8)[0]
        if not (n==n2 or abs(n-n2)<4) or not (10<n<120) or not (10<nch<200): continue
        nw=None
        for w in (1,2,3,4):
            nrig=struct.unpack_from('<I',d,i-8-4*w-8)[0];nw_=struct.unpack_from('<I',d,i-8-4*w-4)[0]
            if nw_==w and 10<nrig<200: nw=w;break
        if nw is None: continue
        bits=d[i-8-4*nw:i-8]
        lst=list(struct.unpack_from('<%dH'%(max(n,n2)+1),d,i+4))
        core=lst[:n+1]
        if lst[0]!=0 or len(set(core))!=len(core) or max(core)>=nrig: continue
        out.append(dict(off=i,nrig=nrig,nch=nch,n=n,n2=n2,list=lst[:n+1],extra=lst[n+1:],bits=bits))
    return out
def choose_map(h,maps,rig):
    """pick the map whose mapping of the clip's constant positions best matches the rig bind positions"""
    from qsa import const_tracks
    pc,qc=const_tracks(h)
    best=None
    for m in maps:
        if m['nch']!=h['nch'] or m['nrig']!=rig.get('nrig0',len(rig['names'])): continue   # (b92 nrig0: a rig with bones appended after its maps, vm/make_vm_weapon.py _add_head)
        err=0;cnt=0
        for c,v in pc.items():
            if c+1>=len(m['list']): continue
            b=m['list'][c+1];bp=rig['pos'][b]
            err+=min(np.linalg.norm(v-bp),50);cnt+=1
        if cnt==0: continue
        e=err/cnt
        if best is None or e<best[0]: best=(e,m)
    return best

def _ang(a,b):
    d=abs(float(np.dot(a,b)));return 2*np.degrees(np.arccos(min(1.0,d)))
def choose_map_ref(h,C,maps,rig,refL,refW,tqa):
    """pick the map that makes frame 0 closest to a reference pose (local refL / world refW per rig bone)"""
    best=None
    for m in maps:
        if m['nch']!=h['nch'] or m['nrig']!=rig.get('nrig0',len(rig['names'])): continue   # (b92 nrig0: a rig with bones appended after its maps, vm/make_vm_weapon.py _add_head)
        L=m['list'];err=[]
        for c,v in C['Q'].items():
            if c+1>=len(L): continue
            b=L[c+1];q=tqa(v[0])
            if C['mode']!=1 and c in C['anim']: err.append(_ang(q,refW[b]))
            else: err.append(_ang(q,refL[b]))
        if not err: continue
        e=float(np.median(err))
        if best is None or e<best[0]: best=(e,m)
    return best

def choose_map2(h,C,maps,rig,refL=None,wrot=0.1):
    """score = mean const-position error (cm) + wrot * median const-rotation error vs reference local pose (deg)"""
    from maps import _ang
    best=None
    for m in maps:
        if m['nch']!=h['nch'] or m['nrig']!=rig.get('nrig0',len(rig['names'])): continue   # (b92 nrig0: a rig with bones appended after its maps, vm/make_vm_weapon.py _add_head)
        L=m['list'];pe=[];re=[]
        for c,v in C['P'].items():
            if c not in h['L'][0]: continue
            b=L[c+1] if c+1<len(L) else None
            pe.append(50 if b is None else min(np.linalg.norm(v[0]-rig['pos'][b]),50))
        if refL is not None:
            for c,v in C['Q'].items():
                if c in C['anim']: continue
                b=L[c+1] if c+1<len(L) else None
                q=v[0];s=q@q;qq=np.r_[2*q,1-s]/(1+s)
                re.append(90 if b is None else _ang(qq,refL[b]))
        sc=(np.mean(pe) if pe else 0)+wrot*(np.median(re) if re else 0)
        if best is None or sc<best[0]: best=(sc,m)
    return best

def choose_map3(h,C,maps,rig,wrot=0.1,return_all=False):
    """no external reference: compare with the rig's bind pose (T-pose: legs/spine/fingers/weapon bones are close to it)"""
    from maps import _ang
    from rig import fk
    par=rig['parent']
    Pb,Wb=fk(par,[p.astype(float) for p in rig['pos']],[q.astype(float) for q in rig['quat']])
    bl=[q.astype(float) for q in rig['quat']]
    bad=set(i for i,p in enumerate(rig['pos']) if np.linalg.norm(p)>60 and 'Footsteps' not in rig['names'][i] and rig['names'][i]!='Bip01')
    res=[]
    for m in maps:
        if m['nch']!=h['nch'] or m['nrig']!=rig.get('nrig0',len(rig['names'])): continue   # (b92 nrig0: a rig with bones appended after its maps, vm/make_vm_weapon.py _add_head)
        L=m['list'];pe=[];re=[]
        for c,v in C['P'].items():
            b=L[c+1] if c+1<len(L) else None
            if c in h['L'][0] and b not in bad: pe.append(50 if b is None else min(np.linalg.norm(v[0]-rig['pos'][b]),50))
        for c,v in C['Q'].items():
            b=L[c+1] if c+1<len(L) else None
            q=v[0];s=q@q;qq=np.r_[2*q,1-s]/(1+s)
            if b is None: re.append(90);continue
            if b in bad: continue
            ref=Wb[b] if (C['mode']!=1 and c in C['anim']) else bl[b]
            re.append(_ang(qq,ref))
        sc=(np.mean(pe) if pe else 0)+wrot*(np.mean(np.minimum(re,120)) if re else 0)
        res.append((sc,m))
    res.sort(key=lambda x:x[0])
    return res if return_all else (res[0] if res else None)
