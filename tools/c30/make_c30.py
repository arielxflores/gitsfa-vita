# c30.gmdl: the first weapon model of the port (Seburo C-30, first person, static), still loaded at start-up (main.c load_weapon).
# Moved here from the old /root/gits/wraw/make_assets.py (its sounds.gsnd part is gone: the sound packs replaced it).
# usage: make_c30.py OUTDIR      inputs: GITSFA_ANIMOUT/weapon (weapon.dat weapon\AR_SEBURO_C30\1st, Parts\IronSight\IS_SEBURO_C30\1st)
import sys,os,struct,numpy as np,etcpak
from PIL import Image
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','lib'));sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
import export_skin,gitspaths
OUT=sys.argv[1]
W=gitspaths.dirp('ANIMOUT','weapon')
# ---------- weapon model ----------
w,nodes=export_skin.run(W+'AR_SEBURO_C30/1st/SEBURO_C30_body.nif',True)
s=export_skin.run(W+'Parts/IronSight/IS_SEBURO_C30/1st/IS_SEBURO_C30_E1_U.nif')
E1=nodes['E1']
for m in s: m['P']=(np.c_[m['P'],np.ones(len(m['P']))]@E1.T)[:,:3]
def texpath(n):
    for d in [W+'AR_SEBURO_C30/1st/',W+'Parts/IronSight/IS_SEBURO_C30/1st/']:
        if os.path.exists(d+n): return d+n
def mips(im,fmt):
    out=[];w_,h_=im.size
    while w_>=4 and h_>=4:
        a=np.ascontiguousarray(np.asarray(im.convert('RGBA'))).tobytes()
        out.append(etcpak.compress_to_dxt1(a,w_,h_) if fmt==1 else etcpak.compress_to_dxt5(a,w_,h_))
        if w_==4 or h_==4: break
        w_//=2;h_//=2;im=im.resize((w_,h_),Image.BOX)
    return out
texs=[];tid={}
def addtex(n,maxs=512):
    if n in tid: return tid[n]
    im=Image.open(texpath(n)).convert('RGBA');W,H=im.size;sc=min(1,maxs/max(W,H));im=im.resize((max(4,int(W*sc)),max(4,int(H*sc))),Image.LANCZOS)
    fmt=5 if np.asarray(im)[:,:,3].min()<250 else 1
    texs.append((im.size[0],im.size[1],fmt,mips(im,fmt)));tid[n]=len(texs)-1;return tid[n]
meshes=[]
for m in w+s:
    blend=2 if m['tex'] and m['tex'].upper().startswith('DOT') else 0
    meshes.append((addtex(m['tex']),blend,np.c_[m['P'],m['UV']].astype('<f4'),m['I'].astype('<u2')))
meshes.sort(key=lambda x:x[1])
with open(OUT+'/c30.gmdl','wb') as f:
    f.write(b'GMDL'+struct.pack('<III',1,len(texs),len(meshes)))
    for W_,H_,fmt,mm in texs:
        f.write(struct.pack('<HHBB',W_,H_,fmt,len(mm)))
        for b in mm: f.write(struct.pack('<I',len(b)));f.write(b)
    for t,bl,V,I in meshes:
        f.write(struct.pack('<iiII',t,bl,len(V),len(I)));f.write(V.tobytes());f.write(I.tobytes())
        if len(I)%2: f.write(b'\0\0')
    # attachment points (muzzle = wea_bi) in weapon space
    for nm in ['wea_bi','E1','SC_DUMMY']:
        f.write(nm.encode().ljust(16,b'\0'));f.write(nodes[nm][:3,3].astype('<f4').tobytes())
print('c30.gmdl',os.path.getsize(OUT+'/c30.gmdl'),'tris',sum(len(m[3])//3 for m in meshes))
