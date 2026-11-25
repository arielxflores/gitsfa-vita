# find the camera that best matches a footage frame (coarse grid search on edge images)
import sys,os,math,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from PIL import Image,ImageFilter
def prep(im):
    g=np.asarray(im.convert('L').resize((160,90),Image.BOX)).astype(float)
    gx=np.abs(np.diff(g,axis=1))[:-1,:];gy=np.abs(np.diff(g,axis=0))[:,:-1];e=gx+gy
    m=np.ones_like(e);m[:25,:40]=0;m[60:,:]=0;m[:8,55:110]=0   # HUD areas
    e=e*m;return (e-e.mean())/(e.std()+1e-6),m
def run(stem,gmap,gsky,frame,xs,ys,z,yaws,pitches,fov=59):
    R=Renderer(W=320,H=180,ss=1);R.load(stem,gmap=gmap,gsky=gsky)
    F,mask=prep(Image.open(frame));best=[]
    for x in xs:
        for y in ys:
            for yw in yaws:
                for p in pitches:
                    im=R.shot((x,y,z),math.radians(yw),math.radians(p),fov=fov)
                    E,_=prep(im);s=(E*F).mean()
                    best.append((s,x,y,yw,p))
    best.sort(reverse=True);return best[:8]
if __name__=='__main__':
    import json
    stem,gmap,gsky,frame=sys.argv[1:5];a=json.loads(sys.argv[5])
    for b in run(stem,gmap,gsky,frame,**a): print(b)
