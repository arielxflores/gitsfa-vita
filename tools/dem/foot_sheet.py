# Contact sheets of a crop of every Nth footage frame, labelled with the time (s).
# usage: foot_sheet.py framedir outprefix x0 y0 x1 y1 cols rows [step] [start] [scale]
import sys,glob,os
from PIL import Image,ImageDraw
fd,out=sys.argv[1],sys.argv[2];x0,y0,x1,y1,cols,rows=map(int,sys.argv[3:9])
step=int(sys.argv[9]) if len(sys.argv)>9 else 1;start=int(sys.argv[10]) if len(sys.argv)>10 else 1
sc=float(sys.argv[11]) if len(sys.argv)>11 else 1.0
fr=sorted(glob.glob(fd+'/f_*.jpg'))
idx=[i for i in range(start,len(fr)+1,step)]
w,h=int((x1-x0)*sc),int((y1-y0)*sc);per=cols*rows
for s in range(0,len(idx),per):
    im=Image.new('RGB',(cols*w,rows*(h+12)),(40,0,40));dr=ImageDraw.Draw(im)
    for k,i in enumerate(idx[s:s+per]):
        f=Image.open(fd+'/f_%04d.jpg'%i).crop((x0,y0,x1,y1)).resize((w,h))
        cx,cy=(k%cols)*w,(k//cols)*(h+12);im.paste(f,(cx,cy+12));dr.text((cx+2,cy),'%d:%02d (%d)'%((i-1)//60,(i-1)%60,i-1),fill=(255,255,0))
    im.save('%s_%02d.jpg'%(out,s//per),quality=90)
