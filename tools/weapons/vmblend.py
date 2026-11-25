# Hands-on-gun check of a vm_<short>.gvm under the engine's blending (model.h: per-bone lerp of camera-space positions,
# nlerp of rotations): for every pair of clips the engine crossfades (base layer) or layers (action over base), the
# worst distance of the left / right hand from where the gun holds it (its idle relation), over blend weights and frames.
# usage: python3 vmblend.py vm_a.gvm [vm_b.gvm ...]
import sys,os,itertools;H=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,H+'/../../vm')
import preview,numpy as np
def qm(q): return preview.qmat(q/np.linalg.norm(q))
def blend(A,B,w):
    d=(A[:,:4]*B[:,:4]).sum(1,keepdims=True);Bq=np.where(d<0,-B[:,:4],B[:,:4]);q=A[:,:4]*(1-w)+Bq*w;q/=np.linalg.norm(q,axis=1,keepdims=True)
    return np.c_[q,A[:,4:]*(1-w)+B[:,4:]*w]
BASE=['idle','walk','run','sprint','ads_idle','fall','slide']
ACT=['fire','fire_ads','reload','reload_empty','draw','holster','jump','land','bolt','slide_fire','fire2']
def check(path,tb='Grip'):
    G=preview.read(path);B=G['bones'];C=G['clips'];g=B.index(tb if tb in B else 'Grip');L=B.index('Bip01_L_Hand');R=B.index('Bip01_R_Hand')
    def rel(f,h): return qm(f[g,:4]).T@(f[h,4:]-f[g,4:])
    ref={h:rel(C['idle'][2][0],h) for h in (L,R)}
    def err(f,h): return float(np.linalg.norm(rel(f,h)-ref[h]))
    worst=[]
    pairs=[(a,b) for a,b in itertools.permutations([c for c in BASE if c in C],2)]+[(a,b) for a in BASE for b in ACT if a in C and b in C]
    for a,b in pairs:
        A=C[a][2];Bc=C[b][2]
        for fa in (0,A.shape[0]//2):
            for fb in range(0,Bc.shape[0],max(1,Bc.shape[0]//8)):
                ends=max(err(A[fa],L),err(Bc[fb],L)),max(err(A[fa],R),err(Bc[fb],R))   # the end poses themselves
                for w in (0.25,0.5,0.75):
                    P=blend(A[fa],Bc[fb],w);worst.append((err(P,L)-ends[0],err(P,R)-ends[1],a,b,fa,fb,w,ends))
    wl=max(worst,key=lambda x:x[0]);wr=max(worst,key=lambda x:x[1])
    print('%-14s blend adds at most: left hand %.1f cm (%s->%s f%d/f%d w%.2f)   right hand %.1f cm (%s->%s)'%(os.path.basename(path),wl[0],wl[2],wl[3],wl[4],wl[5],wl[6],wr[1],wr[2],wr[3]))
    return worst
if __name__=='__main__':
    for p in sys.argv[1:]: check(p,'charging_handle' if '870' in p else 'Grip')
