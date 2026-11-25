# fit each port gun's side silhouette to the PC's item picture (scale + offset) and read where the PC gun's front tip is, in the gun's own space (cm)
import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import side,sheet_map as SM
import numpy as np
from PIL import Image,ImageDraw
from scipy.signal import fftconvolve
def port_mask(tris,a,b,s,flip=(-1,-1)):
    allp=np.concatenate([t[0].reshape(-1,3) for t in tris if not t[3]]);mn=allp.min(0);mx=allp.max(0)
    W=int((mx[a]-mn[a])*s)+8;H=int((mx[b]-mn[b])*s)+8;im=Image.new('L',(W,H),0);dr=ImageDraw.Draw(im)
    for X,N,col,bl in tris:
        if bl: continue
        sx=(mx[a]-X[:,:,a])*s+4;sy=(mx[b]-X[:,:,b])*s+4
        for i in range(len(X)): dr.polygon([(sx[i,0],sy[i,0]),(sx[i,1],sy[i,1]),(sx[i,2],sy[i,2])],fill=255)
    return np.asarray(im)>0,mx[a],mx[b]     # pixel (px,py) -> x = mx_a-(px-4)/s, z = mx_b-(py-4)/s
def fit(g,D):
    tris=side.gun_side(D+'/vm_%s.gvm'%g)
    pc=np.asarray(Image.open('/tmp/claude-0/pcui/%s_BASIC_01.png'%SM.MAP[g]).convert('RGBA'))[:,:,3]>100
    best=None
    for s in np.arange(2.2,7.6,0.05):
        m,mxa,mxb=port_mask(tris,0,2,s)
        if m.shape[0]>pc.shape[0]+60 or m.shape[1]>pc.shape[1]+80: continue
        # ignore the port's front 25 % (barrel region) and top 20 % (sights) when scoring
        w=m.astype(float);cut=int(m.shape[1]*0.25);w[:,:cut]=0
        box=np.zeros_like(w);box[:,cut:]=1
        A=fftconvolve(pc.astype(float),w[::-1,::-1],mode='full');C=fftconvolve(pc.astype(float),box[::-1,::-1],mode='full')
        sc=A/(w.sum()+C-A+1);k=np.unravel_index(np.argmax(sc),sc.shape);v=sc[k]
        if best is None or v>best[0]: best=(v,s,k[0]-(m.shape[0]-1),k[1]-(m.shape[1]-1),mxa,mxb)
    v,s,oy,ox,mxa,mxb=best
    cols=np.nonzero(pc.any(0))[0];tip_px=cols.min();rear_px=cols.max()
    x_of=lambda px:mxa-((px-ox)-4)/s
    return dict(score=round(float(v),3),s=round(float(s),2),pc_tip=round(float(x_of(tip_px)),1),pc_rear=round(float(x_of(rear_px+1)),1),ox=ox,oy=oy,mxa=mxa,mxb=mxb)
if __name__=='__main__':
    D=sys.argv[1]
    for g in sys.argv[2:]:
        tris=side.gun_side(D+'/vm_%s.gvm'%g);allp=np.concatenate([t[0].reshape(-1,3) for t in tris if not t[3]])
        r=fit(g,D);print('%-9s fit %.3f scale %.2f px/cm | port x %.1f..%.1f | PC rear %.1f tip %.1f -> PC front is %.1f cm beyond the port model'%(g,r['score'],r['s'],allp[:,0].min(),allp[:,0].max(),r['pc_rear'],r['pc_tip'],r['pc_tip']-allp[:,0].max()),flush=True)
