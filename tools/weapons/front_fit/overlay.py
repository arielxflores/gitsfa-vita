import sys;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import side,fit,sheet_map as SM,numpy as np
from PIL import Image,ImageDraw
D=sys.argv[1];out=sys.argv[2];guns=sys.argv[3:]
S=Image.new('RGB',(384*3,150*3*len(guns)))
for i,g in enumerate(guns):
    r=fit.fit(g,D);tris=side.gun_side(D+'/vm_%s.gvm'%g);m,mxa,mxb=fit.port_mask(tris,0,2,r['s'])
    pc=Image.open('/tmp/claude-0/pcui/%s_BASIC_01.png'%SM.MAP[g]).convert('RGBA');bg=Image.new('RGBA',pc.size,(52,44,46,255));bg.alpha_composite(pc);a=np.asarray(bg.convert('RGB')).copy()
    # port silhouette edge in green
    full=np.zeros(a.shape[:2],bool);oy,ox=r['oy'],r['ox']
    ys,xs=np.nonzero(m);ys=ys+oy;xs=xs+ox;ok=(ys>=0)&(ys<a.shape[0])&(xs>=0)&(xs<a.shape[1]);full[ys[ok],xs[ok]]=True
    edge=full&~(np.roll(full,1,0)&np.roll(full,-1,0)&np.roll(full,1,1)&np.roll(full,-1,1))
    a[edge]=(60,255,60)
    im=Image.fromarray(a).resize((384*3,150*3),Image.NEAREST);d=ImageDraw.Draw(im);d.text((6,4),'%s  PC tip %.1f  rear %.1f  scale %.2f'%(g,r['pc_tip'],r['pc_rear'],r['s']),fill=(255,255,0))
    # ruler: ticks every 5 cm along the bottom
    for x in range(-40,110,5):
        px=(ox+4+(mxa-x)*r['s'])*3
        if 0<=px<384*3: d.line([(px,440),(px,450 if x%10 else 436)],fill=(255,255,255)); 
        if x%10==0 and 0<=px<384*3: d.text((px+2,424),str(x),fill=(255,255,255))
    S.paste(im,(0,150*3*i))
S.save(out,quality=90)
