# Viewmodel poses from outside (third-person look at the arms + gun) to check that the hands stay on the gun:
# python3 vmside.py <gvm> <out.png> clip:t ...   (views: from the right side and from below-front, 2 rows)
import sys,os;H=os.path.dirname(os.path.abspath(__file__));sys.path.insert(0,H+'/../../vm')
import vmgl,numpy as np,moderngl
from PIL import Image,ImageDraw
def look(eye,at,up=(0,1,0)):
    eye=np.array(eye,float);f=np.array(at,float)-eye;f/=np.linalg.norm(f);s=np.cross(f,up);s/=np.linalg.norm(s);u=np.cross(s,f)
    M=np.eye(4);M[0,:3]=s;M[1,:3]=u;M[2,:3]=-f;M[:3,3]=-M[:3,:3]@eye;return M
def render(G,fr,MVP,W=480,H_=360):
    c=vmgl.ctx();fbo=c.simple_framebuffer((W,H_),samples=0);fbo.use();fbo.clear(0.45,0.5,0.56,1.0,depth=1.0)
    pr=G['prog'];pr['mvp'].write(MVP.astype('f4').T.tobytes());pr['ldir'].value=(-0.35,0.75,0.56);pr['tint'].value=(1,1,1,1)
    c.enable(moderngl.DEPTH_TEST);c.disable(moderngl.CULL_FACE)
    for i,m in enumerate(G['meshes']):
        if m['blend'] not in (0,3) or len(m['I'])==0: continue
        X,N=vmgl.skin(G,fr,i);arm=i<2
        vb=c.buffer(np.c_[X,N,m['v']['uv']].astype('f4').tobytes());ib=c.buffer(m['I'].astype('u4').tobytes())
        va=c.vertex_array(pr,[(vb,'3f 3f 2f','pos','nrm','uv')],ib);pr['mode'].value=0
        pr['tint'].value=(1.6,1.2,0.9,1) if arm else (0.8,0.95,1.3,1)   # arms warm, gun cool
        G['gltex'][m['tex']].use(0);va.render(moderngl.TRIANGLES);vb.release();ib.release();va.release()
    img=Image.frombytes('RGBA',(W,H_),fbo.read(components=4)).transpose(Image.FLIP_TOP_BOTTOM).convert('RGB');fbo.release();return img
if __name__=='__main__':
    G=vmgl.load(sys.argv[1]);sh=os.path.basename(sys.argv[1])[3:-4];ims=[]
    P=vmgl.persp(40,480/360,2,600)
    views=[look((70,-5,-25),(5,-15,-30)),look((10,-70,-20),(5,-15,-30),(0,0,-1))]
    specs=sys.argv[3:]
    for vi,V in enumerate(views):
        for spec in specs:
            c_,t=spec.split(':')[:2]
            im=render(G,vmgl.pose(G,c_,float(t)),P@V);d=ImageDraw.Draw(im);d.text((4,4),'%s %s t=%s %s'%(sh,c_,t,'side' if vi==0 else 'below'),fill=(255,255,0));ims.append(im)
    n=len(specs);o=Image.new('RGB',(480*n,360*2))
    for i,im in enumerate(ims): o.paste(im,((i%n)*480,(i//n)*360))
    o.save(sys.argv[2]);print(sys.argv[2])
