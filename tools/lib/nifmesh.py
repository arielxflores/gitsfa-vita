import struct,sys
sys.path.insert(0,__import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from nifhdr import hdr
FMT={0x00030437:('f',3),0x00020436:('f',2),0x00010425:('I',1),0x00010413:('H',1),0x00040438:('f',4),0x00020422:('e',2),0x00040424:('e',4),0x00030423:('e',3),0x00040401:('B',4),0x00040406:('B',4)}
def S(h,i): return None if i==0xffffffff else h['strs'][i]
def avobj(d,o):
    name,ne=struct.unpack_from('<II',d,o);o+=8
    extra=struct.unpack_from('<%dI'%ne,d,o);o+=4*ne
    ctrl,flags=struct.unpack_from('<iH',d,o);o+=6
    t=struct.unpack_from('<3f',d,o);o+=12
    r=struct.unpack_from('<9f',d,o);o+=36
    s=struct.unpack_from('<f',d,o)[0];o+=4
    np_=struct.unpack_from('<I',d,o)[0];o+=4
    props=struct.unpack_from('<%dI'%np_,d,o);o+=4*np_
    o+=4
    return dict(name=name,extra=extra,t=t,r=r,s=s,props=props),o
def node(d,o):
    a,o=avobj(d,o)
    nc=struct.unpack_from('<I',d,o)[0];o+=4
    a['children']=[c for c in struct.unpack_from('<%dI'%nc,d,o) if c!=0xffffffff];return a
def stream(d,o):
    size,clone,nr=struct.unpack_from('<III',d,o);o+=12
    regs=[struct.unpack_from('<II',d,o+8*k) for k in range(nr)];o+=8*nr
    nc=struct.unpack_from('<I',d,o)[0];o+=4
    fm=struct.unpack_from('<%dI'%nc,d,o);o+=4*nc
    return dict(regs=regs,fmt=fm,data=d[o:o+size])
def mesh(d,o):
    a,o=avobj(d,o)
    nm=struct.unpack_from('<I',d,o)[0];o+=4+8*nm
    o+=5
    prim,nsub,inst=struct.unpack_from('<IHB',d,o);o+=7
    o+=16
    ns=struct.unpack_from('<I',d,o)[0];o+=4
    st=[]
    for _ in range(ns):
        ref,pi,nsm=struct.unpack_from('<IBH',d,o);o+=7
        rmap=struct.unpack_from('<%dH'%nsm,d,o);o+=2*nsm
        nc=struct.unpack_from('<I',d,o)[0];o+=4
        comps=[struct.unpack_from('<II',d,o+8*k) for k in range(nc)];o+=8*nc
        st.append(dict(ref=ref,rmap=rmap,comps=comps))
    a.update(prim=prim,nsub=nsub,streams=st);return a
def load(path):
    d=open(path,'rb').read();h=hdr(d);h['d']=d;return h
