import numpy as np, struct, sys
sys.path.insert(0,'/root/gits/tools/minimap')
import gcol
from PIL import Image, ImageDraw
D='/mnt/user-data/uploads/vita-agent-use/gitsfa/copy-to-vita/'
STEMS='downtown berutalube ecenter ghost_storage headquarter port pss9 ruinednight ruinedtokyo trainstation underground urban zioplant shooting tutorial tutorial2 tutorial3'.split()
def load_nav(path):
    d=open(path,'rb').read(); n=struct.unpack_from('<I',d,4)[0]
    rec=np.frombuffer(d,dtype=[('v','<f4',(3,3)),('nb','<i4',3)],count=n,offset=8)
    return rec['v'].copy()
def floor_tris(V,I,S,zpad=(250,500)):
    T=V[I]; n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]); l=np.linalg.norm(n,axis=1)+1e-9; nz=n[:,2]/l
    zc=T[:,:,2].mean(1)
    zlo=S['z'].min()-zpad[0]; zhi=S['z'].max()+zpad[1]
    return T[(np.abs(nz)>0.6)&(zc>zlo)&(zc<zhi)]
def wall_tris(V,I,S):
    T=V[I]; n=np.cross(T[:,1]-T[:,0],T[:,2]-T[:,0]); l=np.linalg.norm(n,axis=1)+1e-9; nz=n[:,2]/l
    zmin=T[:,:,2].min(1); zmax=T[:,:,2].max(1)
    zlo=S['z'].min()-50; zhi=S['z'].max()+250
    return T[(np.abs(nz)<0.3)&(zmax>zlo)&(zmin<zhi)]
def raster(tris, x0, y0, res, W, H, fill=True):
    """tris (n,3,3) world -> image W x H, pixel = ((x-x0)/res, (y0... y up -> v down))"""
    im=Image.new('L',(W,H),0); d=ImageDraw.Draw(im)
    for t in tris:
        pts=[((p[0]-x0)/res,(y0-p[1])/res) for p in t]
        if fill: d.polygon(pts,fill=255)
        else: d.line(pts+[pts[0]],fill=255)
    return np.asarray(im)
if __name__=='__main__':
    tiles=[]
    for st in STEMS:
        V,I,S,M=gcol.load(D+st+'.gcol'); F=floor_tris(V,I,S); N=load_nav(D+st+'.gnav')
        P=N.reshape(-1,3); x0,x1=P[:,0].min()-500,P[:,0].max()+500; y0,y1=P[:,1].min()-500,P[:,1].max()+500
        res=max(x1-x0,y1-y0)/300
        W=int((x1-x0)/res)+1; H=int((y1-y0)/res)+1
        a=raster(F,x0,y1,res,W,H); b=raster(N,x0,y1,res,W,H)
        rgb=np.zeros((H,W,3),np.uint8); rgb[...,0]=a*0.5; rgb[...,1]=b; rgb[...,2]=a*0.5
        im=Image.fromarray(rgb); d=ImageDraw.Draw(im)
        for s in S: u,v=(s['x']-x0)/res,(y1-s['y'])/res; d.ellipse([u-2,v-2,u+2,v+2],fill=(255,80,80))
        t=Image.new('RGB',(310,330),(30,30,30)); t.paste(im,(0,20)); ImageDraw.Draw(t).text((3,3),st,fill=(255,255,0)); tiles.append(t)
    cols=6; rows=(len(tiles)+cols-1)//cols; sh=Image.new('RGB',(310*cols,330*rows))
    for i,t in enumerate(tiles): sh.paste(t,((i%cols)*310,(i//cols)*330))
    sh.save('/tmp/claude-0/minimap/topdown_sheet.png')
