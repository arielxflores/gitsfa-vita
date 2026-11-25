# Contact strip of viewmodel poses: python3 vmstrip.py <gvm> <out.png> clip:t[:ads] ...  (t in seconds; ads 0 = hip
# projection, 1 = ADS projection with the weapons.txt ADS offset of that weapon)
import sys,os;H=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,H+'/../../vm')
import vmgl,numpy as np
from PIL import Image,ImageDraw
def wtab(sh):
    for l in open(H+'/../../build/data/weapons.txt'):
        f=l.rstrip('\n').split('|')
        if f[0]==sh:
            num=lambda x,d:float(x) if x not in ('','-') else d
            vec=lambda x,d:[float(y) for y in x.split(',')] if x.count(',')==2 else d
            hf=num(f[17],49.75);ho=vec(f[18],[-2.37,0.08,0]);return dict(hip_fov=hf,hip_off=ho,ads_fov=num(f[19],hf),ads_off=vec(f[20],ho))
    return dict(hip_fov=49.75,hip_off=[-2.37,0.08,0],ads_fov=11.5,ads_off=[0,0,-15.6])
def proj(T,a):
    th=np.tan(np.radians(T['hip_fov'])/2)*(1-a)+np.tan(np.radians(T['ads_fov'])/2)*a
    return np.degrees(2*np.arctan(th)),[T['hip_off'][k]*(1-a)+T['ads_off'][k]*a for k in range(3)]
if __name__=='__main__':
    G=vmgl.load(sys.argv[1]);sh=os.path.basename(sys.argv[1])[3:-4];T=wtab(sh);ims=[]
    for spec in sys.argv[3:]:
        c,t,*r=spec.split(':');a=float(r[0]) if r else 0.0
        if c not in G['clips']: continue
        fov,off=proj(T,a);im=vmgl.render(G,(c,float(t)),fov,off,W=480,H=272,ads=a);d=ImageDraw.Draw(im);d.text((4,4),'%s %s t=%s ads=%g'%(sh,c,t,a),fill=(255,255,0))
        d.line([(234,136),(246,136)],fill=(255,255,0));d.line([(240,130),(240,142)],fill=(255,255,0));ims.append(im)
    n=len(ims);cols=min(4,n);rows=(n+cols-1)//cols;o=Image.new('RGB',(480*cols,272*rows))
    for i,im in enumerate(ims): o.paste(im,((i%cols)*480,(i//cols)*272))
    o.save(sys.argv[2]);print(sys.argv[2])
