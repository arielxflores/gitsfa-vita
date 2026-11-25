# contact sheet: 8 yaw directions (+ optional pitch) from one point
import sys,math,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from render import Renderer
from PIL import Image,ImageDraw
def sheet(R,pos,out,pitch=-10,yaws=range(0,360,45),cols=4,label=''):
    ims=[]
    for y in yaws:
        im=R.shot(pos,math.radians(y),math.radians(pitch)).resize((480,272),Image.BOX)
        ImageDraw.Draw(im).text((4,4),'%s yaw %d pitch %d'%(label,y,pitch),fill=(255,255,0));ims.append(im)
    rows=(len(ims)+cols-1)//cols;S=Image.new('RGB',(480*cols,272*rows))
    for i,im in enumerate(ims): S.paste(im,((i%cols)*480,(i//cols)*272))
    S.save(out);return S
if __name__=='__main__':
    stem=sys.argv[1];x,y,z=map(float,sys.argv[2:5]);out=sys.argv[5];pitch=float(sys.argv[6]) if len(sys.argv)>6 else -10
    R=Renderer();R.load(stem);sheet(R,(x,y,z),out,pitch,label=stem)
