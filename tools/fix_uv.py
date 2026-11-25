# rewrite a .gmap with rebased texture coordinates (textures untouched)
import sys,struct,numpy as np,glob,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from uvrebase import rebase,split16
def run(src,dst):
    b=open(src,'rb').read()
    mg,ver,nt,nm=struct.unpack_from('<4sIII',b,0); o=16
    for i in range(nt):
        w,h,fmt,n=struct.unpack_from('<HHBB',b,o); o+=6
        for k in range(n): s=struct.unpack_from('<I',b,o)[0]; o+=4+s
    texblob=b[16:o]; recs=[]; before=after=0
    for i in range(nm):
        t,l,a,nv,ni=struct.unpack_from('<iiIII',b,o); o+=20+24
        V=np.frombuffer(b,np.float32,nv*7,o).reshape(-1,7).copy(); o+=nv*28; I=np.frombuffer(b,np.uint16,ni,o).astype(np.int64); o+=ni*2+(2 if ni%2 else 0)
        V2,I2=rebase(V,I); before+=nv; after+=len(V2)
        for Vp,Ip in split16(V2,I2): recs.append((t,l,a,Vp.astype('<f4'),Ip.astype('<u2')))
    with open(dst,'wb') as f:
        f.write(b'GMAP'+struct.pack('<III',ver,nt,len(recs))); f.write(texblob)
        for t,l,a,V,I in recs:
            P=V[:,:3]; f.write(struct.pack('<iiIII',t,l,a,len(V),len(I))); f.write(P.min(0).astype('<f4').tobytes()+P.max(0).astype('<f4').tobytes())
            f.write(V.tobytes()); f.write(I.tobytes())
            if len(I)%2: f.write(b'\0\0')
    mx=max(float(np.abs(r[3][:,3:5]).max()) for r in recs)
    print(os.path.basename(dst),'meshes',nm,'->',len(recs),'vertices',before,'->',after,'max |uv| now %.1f'%mx)
if __name__=='__main__': run(sys.argv[1],sys.argv[2])
