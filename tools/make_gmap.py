import sys,os,struct,numpy as np,etcpak
from PIL import Image
sys.path.insert(0,os.path.dirname(__file__))
import export_map
RAW=sys.argv[1]; OUT=sys.argv[2]; TEXMAX=int(sys.argv[3]) if len(sys.argv)>3 else 256
m=export_map.run(RAW+'/out/map/FixedMap/Fix_Downtown/world_lmap.nif','/tmp/dt.pkl')
texs=[];tid={}
def mips(im,fmt):
    out=[];w,h=im.size
    while True:
        a=np.ascontiguousarray(np.asarray(im.convert('RGBA')))
        if w<4 or h<4: break
        out.append(etcpak.compress_to_dxt1(a.tobytes(),w,h) if fmt==1 else etcpak.compress_to_dxt5(a.tobytes(),w,h))
        if w==4 or h==4: break
        w//=2;h//=2;im=im.resize((w,h),Image.BOX)
    return out
def addtex(key,loader,maxs):
    if key in tid: return tid[key]
    try: im=loader()
    except Exception as e: print('tex fail',key,e); tid[key]=-1; return -1
    w,h=im.size
    s=min(1,maxs/max(w,h));w=max(4,int(w*s));h=max(4,int(h*s))
    w=1<<(w.bit_length()-1);h=1<<(h.bit_length()-1)
    im=im.convert('RGBA').resize((w,h),Image.LANCZOS)
    alpha=np.asarray(im)[:,:,3].min()<200
    fmt=5 if alpha else 1
    texs.append((w,h,fmt,mips(im,fmt)));tid[key]=len(texs)-1;return tid[key]
recs=[]
for x in m:
    t=addtex(x['tex'].lower(),lambda:Image.open(RAW+'/tex/'+os.path.basename(x['tex']).lower()),TEXMAX) if x['tex'] else -1
    l=addtex(x['lm'],lambda:Image.open(RAW+'/out/map/FixedMap/Fix_Downtown/'+x['lm'].replace('\\','/')),1024) if x['lm'] else -1
    n=len(x['P'])
    uv0=x['UV0'] if x['UV0'] is not None else np.zeros((n,2),'f4')
    uv1=x['UV1'] if x['UV1'] is not None else np.full((n,2),-1,'f4')
    V=np.c_[x['P'],uv0,uv1].astype('<f4')
    I=x['I'].astype('<u4')
    assert n<65536
    I=I.astype('<u2')
    alpha=t>=0 and texs[t][2]==5
    recs.append((t,l,alpha,V,I))
# sort: opaque first, then by texture
recs.sort(key=lambda r:(r[2],r[0],r[1]))
with open(OUT,'wb') as f:
    f.write(b'GMAP'+struct.pack('<III',1,len(texs),len(recs)))
    for w,h,fmt,mm in texs:
        f.write(struct.pack('<HHBB',w,h,fmt,len(mm)))
        for b in mm: f.write(struct.pack('<I',len(b)));f.write(b)
    for t,l,a,V,I in recs:
        P=V[:,:3];f.write(struct.pack('<iiIII',t,l,int(a),len(V),len(I)));f.write(P.min(0).astype('<f4').tobytes()+P.max(0).astype('<f4').tobytes())
        f.write(V.tobytes());f.write(I.tobytes())
        if len(I)%2: f.write(b'\0\0')
print('textures',len(texs),'meshes',len(recs),'size MB',os.path.getsize(OUT)/1e6)
