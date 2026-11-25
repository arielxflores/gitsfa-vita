# Check renders of a vm_<short>.gvm with the engine's projection (preview.py rasterizer): hip idle, ADS (aimed idle +
# the weapon's ADS offset, viewmodel fovy 11.5), and a reload mid-frame -> one strip PNG. A cross marks the view axis.
# usage: python3 preview_weapon.py <short> [out.png]
import sys,os,json;HERE=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,HERE)
import os,numpy as np,preview
from PIL import Image,ImageDraw
HIP_FOV,ADS_FOV=49.75,11.5;HIP_OFF=(-2.37,0.08,0)
def frame(G,clip,t):
    nf=G['clips'][clip][2].shape[0];return G['clips'][clip][2][min(nf-1,int(round(t*(nf-1))))]
def shot(G,clip,t,fov,off,W=480,H=272,label=''):
    p='/tmp/claude-0/weapons/_tmp%d.png'%os.getpid();preview.render(G,frame(G,clip,t),p,fovy=fov,W=W,H=H,offset=off)
    im=Image.open(p).convert('RGB');d=ImageDraw.Draw(im)
    d.line([(W/2-6,H/2),(W/2+6,H/2)],fill=(255,255,0));d.line([(W/2,H/2-6),(W/2,H/2+6)],fill=(255,255,0))
    d.text((4,4),label,fill=(255,255,255));return im
if __name__=='__main__':
    s=sys.argv[1];out=sys.argv[2] if len(sys.argv)>2 else '/tmp/claude-0/weapons/%s.png'%s
    G=preview.read(os.path.join(HERE,'..','build','data','vm_%s.gvm'%s));info=json.load(open('/tmp/claude-0/weapons/vminfo.json')).get(s,{})
    ims=[shot(G,'idle',0,HIP_FOV,HIP_OFF,label='%s hip idle'%s)]
    if 'ads_idle' in G['clips']:
        off=tuple(info.get('ads_offset',(0,0,-15.6)));ims.append(shot(G,'ads_idle',0,ADS_FOV,off,label='ADS off=(%.2f,%.2f,%.1f)'%off))
    for c,t in (('reload',0.5),('reload_loop',0.5),('cook',0.6),('fire',0.35)):
        if c in G['clips']: ims.append(shot(G,c,t,HIP_FOV,HIP_OFF,label='%s %.0f%%'%(c,t*100)));break
    W=sum(i.size[0] for i in ims);im=Image.new('RGB',(W,ims[0].size[1]));x=0
    for i in ims: im.paste(i,(x,0));x+=i.size[0]
    im.save(out);print(out)
