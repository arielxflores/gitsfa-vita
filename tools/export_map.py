import sys,os,struct,numpy as np,pickle;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from nifmesh import *
def run(path,out):
    h=load(path);d=h['d'];B=h['blocks']
    def src(k): return S(h,struct.unpack_from('<I',d,B[k][1]+13)[0]) if 0<=k<len(B) and B[k][0]=='NiSourceTexture' else None
    meshes=[]
    def walk(k,M):
        t,o,s=B[k]
        if t=='NiNode': a=node(d,o)
        elif t=='NiMesh': a=mesh(d,o)
        else: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t']
        W=M@L
        if t=='NiNode':
            for c in a['children']: walk(c,W)
            return
        tex=lm=None
        for p in a['props']:
            if B[p][0]=='NiTexturingProperty':
                po=B[p][1];tex=src(struct.unpack_from('<I',d,po+19)[0]);lm=src(struct.unpack_from('<I',d,po+30)[0])
        S_={}
        for st in a['streams']:
            ds=stream(d,B[st['ref']][1]);name=(S(h,st['comps'][0][0]),st['comps'][0][1])
            f=ds['fmt'][0];dt={0x30437:('<f4',3),0x20436:('<f4',2),0x10425:('<u4',1),0x40438:('<f4',4)}.get(f)
            if dt: S_[name]=np.frombuffer(ds['data'],dt[0]).reshape(-1,dt[1])
        P=S_[('POSITION',0)];P=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        N=(S_[('NORMAL',0)]@W[:3,:3].T) if ('NORMAL',0) in S_ else np.zeros_like(P)
        meshes.append(dict(name=S(h,a['name']),P=P.astype('f4'),N=N.astype('f4'),I=S_[('INDEX',0)].ravel(),
            UV0=S_.get(('TEXCOORD',0)),UV1=S_.get(('TEXCOORD',1)),tex=tex,lm=lm))
    walk(0,np.eye(4))
    pickle.dump(meshes,open(out,'wb'))
    return meshes
if __name__=='__main__':
    m=run(sys.argv[1],sys.argv[2])
    allP=np.concatenate([x['P'] for x in m])
    print(len(m),'meshes',sum(len(x['I']) for x in m)//3,'tris',len(allP),'verts');print('bbox',allP.min(0),allP.max(0))
    from collections import Counter;print(Counter(x['lm'] for x in m))
