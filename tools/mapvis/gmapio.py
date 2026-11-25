# .gmap reader (v1: 7-float vertices; v2: per-mesh material record, see make_gmap2.py)
import struct,io,numpy as np
from PIL import Image
def dds_decode(w,h,data,fmt):
    if fmt==0: return np.frombuffer(data,np.uint8).reshape(h,w,4).copy()
    four=b'DXT1' if fmt==1 else b'DXT5'
    hdr=b'DDS '+struct.pack('<IIIIIII',124,0x1007,h,w,len(data),0,1)+b'\0'*44+struct.pack('<II4sIIIII',32,4,four,0,0,0,0,0)+struct.pack('<IIIII',0x1000,0,0,0,0)
    return np.asarray(Image.open(io.BytesIO(hdr+data)).convert('RGBA'))
def read(path,decode=True):
    d=open(path,'rb').read()
    mg,ver,nt,nm=struct.unpack_from('<4sIII',d,0);o=16
    T=[]
    for i in range(nt):
        w,h,fmt,n=struct.unpack_from('<HHBB',d,o);o+=6;lv=[]
        for k in range(n):
            s=struct.unpack_from('<I',d,o)[0];o+=4;lv.append(d[o:o+s]);o+=s
        T.append(dict(w=w,h=h,fmt=fmt,levels=lv))
    M=[]
    for i in range(nm):
        if ver==1:
            t,l,a,nv,ni=struct.unpack_from('<iiIII',d,o);o+=20
            mat=dict(tex=t,lm=l,mode=1 if a else 0,ref=0.5,two=1 if a else 0,zwrite=1,glow=-1,glowk=0.0,tint=(1,1,1,1),flags=0,stride=7)
        else:
            t,l,g,mode,flags,nv,ni=struct.unpack_from('<iiiIIII',d,o);o+=28
            ref,glowk=struct.unpack_from('<ff',d,o);o+=8
            tint=struct.unpack_from('<4f',d,o);o+=16
            mat=dict(tex=t,lm=l,glow=g,mode=mode&3,two=(flags>>0)&1,zwrite=0 if (flags>>1)&1 else 1,vcol=(flags>>2)&1,flags=flags,ref=ref,glowk=glowk,tint=tint,stride=(8 if (flags>>2)&1 else 7)+(1 if flags&64 else 0),env=-1,envk=0.0)
            if flags&64: mat['env'],mat['envk']=struct.unpack_from('<if',d,o);o+=8   # b67: environment map (map_visual.py)
        mn=struct.unpack_from('<3f',d,o);mx=struct.unpack_from('<3f',d,o+12);o+=24
        st=mat['stride']
        V=np.frombuffer(d,'<f4',nv*st,o).reshape(nv,st).copy();o+=nv*st*4
        I=np.frombuffer(d,'<u2',ni,o).copy();o+=ni*2+(2 if ni%2 else 0)
        M.append(dict(mat,nv=nv,ni=ni,mn=mn,mx=mx,V=V,I=I))
    return dict(ver=ver,tex=T,mesh=M)
