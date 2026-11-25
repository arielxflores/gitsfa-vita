import struct
def hdr(d):
    i=d.index(b'\n')+1
    ver,end,uv,nb,meta=struct.unpack_from('<IBIII',d,i);i+=17
    nt=struct.unpack_from('<H',d,i)[0];i+=2
    types=[]
    for _ in range(nt):
        l=struct.unpack_from('<I',d,i)[0];i+=4;types.append(d[i:i+l].decode());i+=l
    idx=struct.unpack_from('<%dH'%nb,d,i);i+=2*nb
    sizes=struct.unpack_from('<%dI'%nb,d,i);i+=4*nb
    ns,ml=struct.unpack_from('<II',d,i);i+=8
    strs=[]
    for _ in range(ns):
        l=struct.unpack_from('<I',d,i)[0];i+=4;strs.append(d[i:i+l].decode('latin1'));i+=l
    ng=struct.unpack_from('<I',d,i)[0];i+=4+4*ng
    blocks=[];o=i
    for k in range(nb):
        blocks.append((types[idx[k]&0x7fff],o,sizes[k]));o+=sizes[k]
    return dict(meta=meta,types=types,strs=strs,blocks=blocks,end=o,len=len(d))
