import struct,numpy as np
def u16list(d,o):
    n=struct.unpack_from('<H',d,o)[0]
    return list(struct.unpack_from('<%dH'%n,d,o+2))
def hdr(path):
    d=open(path,'rb').read()
    h=dict(d=d)
    h['h1'],h['x8'],h['xc']=struct.unpack_from('<III',d,4)
    h['dur'],h['fps']=struct.unpack_from('<ff',d,0x18)
    h['nch'],h['a24'],h['a28'],h['a2c']=struct.unpack_from('<IIII',d,0x20)
    h['offs']=struct.unpack_from('<10I',d,0x30)
    h['L']=[u16list(d,h['offs'][i]) for i in range(6)]
    o7=h['offs'][7]
    h['ident']=struct.unpack_from('<4f',d,o7)
    h['cnt']=struct.unpack_from('<6H',d,o7+16)
    h['pf']=struct.unpack_from('<6f',d,o7+32)
    h['qf']=struct.unpack_from('<6f',d,o7+56)
    return h

def getbits(buf,pos,n):
    if n==0: return 0
    v=int.from_bytes(buf[pos>>3:(pos>>3)+((n+(pos&7)+7)>>3)],'little')
    return (v>>(pos&7))&((1<<n)-1)

def r4(n): return (n+3)&~3

def const_tracks(h):
    d=h['d'];o7=h['offs'][7]
    fl=np.frombuffer(d,'<f4',12,o7+28)
    pmin,pmax,qmin,qmax=fl[0:3],fl[3:6],fl[6:9],fl[9:12]
    L0,L1=h['L'][0],h['L'][1]
    o=o7+28+48+8; o=(o+15)&~15
    P=np.frombuffer(d,'<u2',3*len(L0),o).reshape(-1,3);o+=6*len(L0);o=(o+3)&~3
    Q=np.frombuffer(d,'<u2',3*len(L1),o).reshape(-1,3)
    pos={c:pmin+u/65535*(pmax-pmin) for c,u in zip(L0,P)}
    quat={c:qmin+u/65535*(qmax-qmin) for c,u in zip(L1,Q)}
    return pos,quat

def block_offsets(h):
    d=h['d'];o6=h['offs'][6];o8=h['offs'][8]
    n=(o8-o6)//8
    return [(struct.unpack_from('<I',d,o6+8*k)[0],struct.unpack_from('<I',d,o6+8*k+4)[0],struct.unpack_from('<I',d,o8+4*k)[0]) for k in range(n)]

def decode_block(h,b,qmode='m11'):
    d=h['d']
    H=struct.unpack_from('<20H',d,b)
    size,nch,start,ns,nP,nQ=H[:6];pB,qB=H[9],H[10];nPS,nQS=H[15],H[16]
    gp=np.frombuffer(d,'<f4',6,b+0x28)
    o=b+0x9c
    PS=np.frombuffer(d,'<f4',6*nPS,o).reshape(-1,6);o+=24*nPS
    PR=[d[o+9*i:o+9*i+9] for i in range(nP)];o+=9*r4(nP)
    pdata=d[o:o+pB*ns+8];o+=pB*ns;o=(o+3)&~3
    QS=np.frombuffer(d,'<f4',6*nQS,o).reshape(-1,6);o+=24*nQS
    QR=[d[o+9*i:o+9*i+9] for i in range(nQ)];o+=9*r4(nQ)
    qdata=d[o:o+qB*ns+8]
    def dec(R,S,data,stride,gmin,gmax,n):
        out=np.zeros((ns,n,3))
        for f in range(ns):
            pos=f*stride*8
            for c,r in enumerate(R):
                for k in range(3):
                    bits=r[k];m=r[3+k];si=r[6+k]
                    s=getbits(data,pos,bits);pos+=bits
                    t=s/((1<<bits)-1) if bits else 0.0
                    smin,smax=S[si][k],S[si][3+k]
                    out[f,c,k]=gmin[k]+m/255*(gmax[k]-gmin[k])+smin+t*(smax-smin)
        return out
    P=dec(PR,PS,pdata,pB,gp[:3],gp[3:],nP) if nP else np.zeros((ns,0,3))
    gq=np.array([-1,-1,-1.]),np.array([1,1,1.])
    Q=dec(QR,QS,qdata,qB,gq[0],gq[1],nQ) if nQ else np.zeros((ns,0,3))
    return dict(start=start,ns=ns,P=P,Q=Q,H=H)
