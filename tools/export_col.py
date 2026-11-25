import sys,struct,numpy as np;sys.path.insert(0,__import__('os').path.dirname(__file__))
from nifmesh import *
SZ={0x30437:12,0x40110:4,0x20436:8,0x10425:4,0x10215:2,0x40438:16}
def run(path):
    h=load(path);d=h['d'];B=h['blocks'];out=[]
    def walk(k,M):
        t,o,s=B[k]
        a=node(d,o) if t=='NiNode' else mesh(d,o) if t=='NiMesh' else None
        if a is None: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];W=M@L
        if t=='NiNode':
            for c in a['children']: walk(c,W)
            return
        P=I=None
        for st in a['streams']:
            ds=stream(d,B[st['ref']][1]);stride=sum(SZ[f] for f in ds['fmt']);raw=np.frombuffer(ds['data'],'u1').reshape(-1,stride)
            off=0
            for (sem,idx),f in zip(st['comps'],ds['fmt']):
                nm=S(h,sem)
                if nm=='POSITION': P=raw[:,off:off+12].copy().view('<f4').reshape(-1,3)
                if nm=='INDEX': I=raw[:,off:off+SZ[f]].copy().view('<u2' if SZ[f]==2 else '<u4').ravel()
                off+=SZ[f]
        P=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        out.append((S(h,a['name']),P.astype('f4'),I.astype('u4')))
    walk(0,np.eye(4));return out
if __name__=='__main__':
    for n,P,I in run(sys.argv[1]): print(n,len(P),len(I)//3,P.min(0).round(),P.max(0).round())
