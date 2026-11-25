import re,struct,numpy as np
def rigs(path):
    d=open(path,'rb').read()
    out=[]
    for m in re.finditer(b'CharacterWorldSpaceTM\x00',d):
        o=m.start()
        # name offsets table precedes: find count by reading names until 0xcd pad
        e=d.index(b'\xcd\xcd',o)
        names=[n.decode() for n in d[o:e].split(b'\x00') if n]
        # bind pose: search for header "size,16,n" after names
        e=(e+3)&~3
        k=e
        while k<e+64:
            sz,al,n=struct.unpack_from('<III',d,k)
            if al==16 and n==len(names): break
            k+=4
        else:
            out.append(dict(names=names,off=o,pos=None,quat=None));continue
        base=k
        po,qo=struct.unpack_from('<II',d,base+0x48)
        P=np.frombuffer(d,'<f4',len(names)*4,base+po).reshape(-1,4)[:,:3]
        Q=np.frombuffer(d,'<f4',len(names)*4,base+qo).reshape(-1,4)
        out.append(dict(names=names,off=o,base=base,pos=P,quat=Q))
    return out

def add_parents(path,R):
    d=open(path,'rb').read()
    for r in R:
        n=len(r['names']);o=r['off']
        k=d.rfind(struct.pack('<IIi',n,0,-1),0,o)+4
        r['parent']=list(struct.unpack_from('<%di'%n,d,k+4))
    return R

def qmul(a,b):
    ax,ay,az,aw=a;bx,by,bz,bw=b
    return np.array([aw*bx+ax*bw+ay*bz-az*by, aw*by-ax*bz+ay*bw+az*bx, aw*bz+ax*by-ay*bx+az*bw, aw*bw-ax*bx-ay*by-az*bz])
def qrot(q,v):
    u=np.array(q[:3]);w=q[3]
    return v+2*np.cross(u,np.cross(u,v)+w*v)
def fk(parent,lpos,lquat):
    n=len(parent);W=[None]*n;P=[None]*n
    for i in range(n):
        p=parent[i]
        if p<0: W[i]=np.array(lquat[i]);P[i]=np.array(lpos[i])
        else:
            W[i]=qmul(W[p],lquat[i]);P[i]=P[p]+qrot(W[p],lpos[i])
    return np.array(P),np.array(W)
