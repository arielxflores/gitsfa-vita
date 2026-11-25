import sys,math,numpy as np,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from gcol import spawns
R=Renderer(ss=1)
for stem in sys.argv[1:]:
    R.load(stem);R.fog=(1.0,0.0,1.0,1.0);R.fogp=(1e9,0,0,0)
    sp=spawns('/root/gits/build/maps/%s.gcol'%stem);sp=sp[::max(1,len(sp)//12)]
    fr=[]
    for gi,t,x,y,z,yw in sp:
        tot=0
        for pitch in (0,40):
            for yaw in range(0,360,90):
                a=np.asarray(R.shot((x,y,z+152),math.radians(yaw),math.radians(pitch)).resize((240,136)))
                tot+=((a[...,0]>250)&(a[...,1]<5)&(a[...,2]>250)).mean()
        fr.append(tot/8)
    print('%-14s sky fraction per spawn: mean %.3f max %.3f'%(stem,np.mean(fr),np.max(fr)),[round(f,2) for f in fr],flush=True)
