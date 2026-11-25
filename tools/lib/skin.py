import sys,struct,numpy as np;sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from nifmesh import *
SZ={0x40214:8,0x30437:12,0x40110:4,0x20436:8,0x10425:4,0x10215:2,0x40438:16,0x40401:4,0x40406:4,0x40409:4,0x4040a:4,0x20422:4,0x40424:8,0x10413:2,0x10414:2,0x30423:6,0x40108:4,0x40102:4,0x10213:2,0x40404:4,0x40405:4}
def xform(d,o):
    r=np.array(struct.unpack_from('<9f',d,o)).reshape(3,3);t=np.array(struct.unpack_from('<3f',d,o+36));s=struct.unpack_from('<f',d,o+48)[0]
    M=np.eye(4);M[:3,:3]=r*s;M[:3,3]=t;return M
def skinmod(d,o):
    ns=struct.unpack_from('<I',d,o)[0];o+=4+2*ns
    nc=struct.unpack_from('<I',d,o)[0];o+=4+2*nc
    flags,root=struct.unpack_from('<HI',d,o);o+=6
    rp2s=xform(d,o);o+=52
    nb=struct.unpack_from('<I',d,o)[0];o+=4
    bones=struct.unpack_from('<%dI'%nb,d,o);o+=4*nb
    s2b=[xform(d,o+52*i) for i in range(nb)];o+=52*nb
    return dict(flags=flags,root=root,rp2s=rp2s,bones=bones,s2b=s2b)
def avobj_full(d,o):
    name,ne=struct.unpack_from('<II',d,o);o+=8
    o+=4*ne
    ctrl,flags=struct.unpack_from('<iH',d,o);o+=6
    o+=12+36+4
    np_=struct.unpack_from('<I',d,o)[0];o+=4+4*np_
    o+=4
    return o
def mesh_mods(d,o):
    o=avobj_full(d,o)
    nm=struct.unpack_from('<I',d,o)[0]
    return [struct.unpack_from('<I',d,o+4+4*i)[0] for i in range(nm)]  # first list = materials? keep raw
def run(path):
    h=load(path);d=h['d'];B=h['blocks']
    # world transforms of nodes
    W={};names={}
    def walk(k,M):
        t,o,s=B[k]
        a=node(d,o) if t=='NiNode' else mesh(d,o) if t=='NiMesh' else None
        if a is None: return
        L=np.eye(4);L[:3,:3]=np.array(a['r']).reshape(3,3)*a['s'];L[:3,3]=a['t'];Wk=M@L
        W[k]=Wk;names[k]=S(h,a['name'])
        if t=='NiNode':
            for c in a['children']: walk(c,Wk)
    walk(0,np.eye(4))
    out=[]
    for k,(t,o,s) in enumerate(B):
        if t!='NiMesh': continue
        a=mesh(d,o)
        if not any(S(h,c[0])=='BLENDINDICES' for st in a['streams'] for c in st['comps']): continue
        # find modifier block: scan raw block for refs to NiSkinningMeshModifier
        raw=d[o:o+s];mod=None
        for i in range(0,len(raw)-3):
            v=struct.unpack_from('<I',raw,i)[0]
            if v<len(B) and B[v][0]=='NiSkinningMeshModifier': mod=v;break
        sm=skinmod(d,B[mod][1])
        streams={}
        for st in a['streams']:
            ds=stream(d,B[st['ref']][1])
            stride=sum(SZ[f] for f in ds['fmt'])
            rawd=np.frombuffer(ds['data'],'u1').reshape(-1,stride);off=0
            for (sem,idx),f in zip(st['comps'],ds['fmt']):
                nm=S(h,sem);sz=SZ[f]
                col=rawd[:,off:off+sz].copy()
                streams[(nm,idx)]=(col,f,ds['regs']);off+=sz
        P=streams[('POSITION_BP',0)][0].view('<f4').reshape(-1,3)
        UV=streams[('TEXCOORD',0)][0].view('<f4').reshape(-1,2)
        UV1=streams[('TEXCOORD',1)][0].view('<f4').reshape(-1,2) if ('TEXCOORD',1) in streams and SZ[streams[('TEXCOORD',1)][1]]==8 else None   # b71: the glow map's uv set (gvmlib.emissive_image)
        I=streams[('INDEX',0)][0];f=streams[('INDEX',0)][1]
        I=I.view('<u2' if SZ[f]==2 else '<u4').ravel().astype(np.int64)
        BI=streams[('BLENDINDICES',0)];BW=streams[('BLENDWEIGHT',0)]
        bw_f=BW[1];bi_f=BI[1]
        out.append(dict(vregs=streams[('POSITION_BP',0)][2],block=k,name=names.get(k),P=P,UV=UV,UV1=UV1,I=I,BI=BI,BW=BW,PAL=streams.get(('BONE_PALETTE',0)),
            idxregs=streams[('INDEX',0)][2],sm=sm,bone_names=[names.get(b) for b in sm['bones']],bone_world=[W.get(b) for b in sm['bones']],meshW=W.get(k)))
    return out,W,names,h
