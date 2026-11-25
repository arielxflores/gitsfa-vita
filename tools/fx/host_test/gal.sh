set -e
cd /root/fx/host && gcc -std=gnu11 -O2 -w -I/root/fx/host -I/root/gits/engine/src gallery.c -o gallery -lm && ./gallery && cd out && python3 - <<'PY'
from PIL import Image,ImageDraw
rows=[(0,m) for m in range(11)]+[(1,m) for m in range(9)]
names=['default','wood','metal','sand','glass','car','box','soft','concrete','blood','cyborg']
cw,ch=300,200
for part,rr in (('a',rows[:11]),('b',rows[11:])):
    S=Image.new('RGB',(cw*6+90,ch*len(rr)+14),(0,0,0));d=ImageDraw.Draw(S)
    for r,(fl,m) in enumerate(rr):
        d.text((4,14+r*ch+ch//2),('floor ' if fl else 'wall ')+names[m],fill=(255,255,0))
        for i in range(6):
            im=Image.open(f'g{fl}_{m}_{i}.ppm'); cy=272 if not fl else 272
            c=im.crop((480-150,cy-110,480+150,cy+90))
            S.paste(c,(90+i*cw,14+r*ch))
    for i,t in enumerate(['0.03','0.1','0.25','0.5','0.85','3.0']): d.text((90+i*cw+4,2),'t='+t,fill=(0,255,255))
    S.save(f'/root/fx/host/gallery_{part}.png')
PY
