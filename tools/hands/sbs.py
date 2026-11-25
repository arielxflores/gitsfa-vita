import sys;sys.path.insert(0,'/tmp/claude-0/hv')
from ovl import *
f=int(sys.argv[1]);ref=Image.open(sys.argv[2]).convert('RGB').resize((960,540));out=sys.argv[3]
box=tuple(int(v) for v in __import__("os").environ.get("BOX","560,240,960,480").split(","));Z=2
def grid(im):
    im=im.crop(box).resize(((box[2]-box[0])*Z,(box[3]-box[1])*Z),Image.LANCZOS);d=ImageDraw.Draw(im)
    for x in range(box[0],box[2],40): d.line([((x-box[0])*Z,0),((x-box[0])*Z,im.size[1])],fill=(255,255,0,60));d.text(((x-box[0])*Z+2,2),str(x),fill=(255,255,0))
    for y in range(box[1],box[3],40): d.line([(0,(y-box[1])*Z),(im.size[0],(y-box[1])*Z)],fill=(255,255,0));d.text((2,(y-box[1])*Z+2),str(y),fill=(255,255,0))
    return im
ims=[grid(ref)]
for c in sys.argv[4:]:
    _,im=overlay(f,ref,**eval('dict(%s)'%c));im=grid(im.point(lambda v:int(255*(v/255)**0.5)));ImageDraw.Draw(im).text((8,20),c,fill=(255,60,60));ims.append(im)
w,h=ims[0].size;cv=Image.new('RGB',(w,h*len(ims)))
for i,x in enumerate(ims): cv.paste(x,(0,i*h))
cv.save(out,quality=88)
