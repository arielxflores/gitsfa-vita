import sys,struct,numpy as np;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from nifmesh import *
SZ={0x30437:12,0x40110:4,0x20436:8,0x10425:4,0x10215:2,0x40438:16,0x40401:4,0x40406:4,0x40409:4,0x4040a:4,0x20422:4,0x40424:8,0x10413:2,0x10414:2,0x30423:6,0x40108:4,0x40102:4}
def run(path,with_nodes=False):
    h=load(path);d=h['d'];B=h['blocks'];out=[];nodes={}
    def src(k): return S(h,struct.unpack_from('<I',d,B[k][1]+13)[0]) if 0<=k<len(B) and B[k][0]=='NiSourceTexture' else None
    def walk(k,M):
        t,o,s=B[k]
        a=node(d,o) if t=='NiNode' else mesh(d,o) if t=='NiMesh' else None
        if a is None: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];W=M@L
        nodes[S(h,a['name'])]=W
        if t=='NiNode':
            for c in a['children']: walk(c,W)
            return
        tex=None
        for p in a['props']:
            if B[p][0]=='NiTexturingProperty':
                raw=d[B[p][1]:B[p][1]+B[p][2]]
                for i in range(len(raw)-3):
                    v=struct.unpack_from('<I',raw,i)[0]
                    if v<len(B) and B[v][0]=='NiSourceTexture':
                        tex=src(v);break
        P=UV=I=None
        for st in a['streams']:
            ds=stream(d,B[st['ref']][1])
            try: stride=sum(SZ[f] for f in ds['fmt'])
            except KeyError as e: print('unknown fmt',[hex(f) for f in ds['fmt']]);continue
            raw=np.frombuffer(ds['data'],'u1').reshape(-1,stride);off=0
            for (sem,idx),f in zip(st['comps'],ds['fmt']):
                nm=S(h,sem)
                if nm in('POSITION','POSITION_BP') and P is None: P=raw[:,off:off+12].copy().view('<f4').reshape(-1,3)
                if nm=='TEXCOORD' and idx==0: UV=raw[:,off:off+8].copy().view('<f4').reshape(-1,2)
                if nm=='INDEX': I=raw[:,off:off+SZ[f]].copy().view('<u2' if SZ[f]==2 else '<u4').ravel()
                off+=SZ[f]
        if P is None or I is None: return
        P=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        out.append(dict(name=S(h,a['name']),P=P.astype('f4'),UV=UV if UV is not None else np.zeros((len(P),2),'f4'),I=I.astype('u4'),tex=tex))
    walk(0,np.eye(4))
    return (out,nodes) if with_nodes else out
if __name__=='__main__':
    for m in run(sys.argv[1]): print(m['name'],len(m['P']),len(m['I'])//3,m['tex'],m['P'].min(0).round(1),m['P'].max(0).round(1))
