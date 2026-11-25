# The L118A-S1b scoped view as one screen-space texture (premultiplied RGBA), rendered from the game's own RIFLESCOPE
# (1st-person NIF): the eyepiece (rubber eyecup with its four marks, the turret caps around it), the tube interior,
# the lens vignette (LENS_02_D, black with a radial alpha ramp) and the reticle (R_SC_CROSS: black cross, thick
# outer posts, thin centre lines, red centre dot). The glass (LENS_01 dome + the RifeScope disc behind it) and the
# caps inside the tube are left out, so the world shows through, as in the original.
# Calibrated on the tutorial footage (Tutorial Stuff.mp4, 255.3 s / 261.55 s, 1280x720): ring centre = screen centre,
# eyecup outer edge r = 390 px, thick reticle posts start 92 px from the centre -> eye 12.85 cm behind the eyecup,
# f = 2386 px at 720 lines. The texture covers +-HALF screen heights around the centre (HALF = 400/720).
# usage: python3 make_scope_overlay.py [out.png] [size]
import sys,os,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from gvmlib import static_meshes
from PIL import Image
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..','tools'));import gitspaths
SD=gitspaths.dirp('ANIMOUT','weapon','Parts','ScopeSight','RIFLESCOPE','1st');TEXD=gitspaths.dirp('ANIMOUT','weapon','Textures')
AXIS_Z=4.78;EYECUP_X=-14.7;EYE_D=12.85;F720=2386.0;HALF=400/720.0
# eyecup brightness: the footage's ring (Machina Media TC 364 / 370 s, daylight) averages ~50/255, the plain bake ~22: x2.2 on
# the outer rubber ring; the inner face of the eyecup (inside 0.79 of the texture's half size) stays in shadow, as there (x0.7)
BODY_GAIN=2.2
VS='''#version 330
in vec3 pos; in vec3 nrm; in vec2 uv; uniform mat4 mvp; uniform mat3 nm; uniform vec3 ldir; out vec2 t0; out float sh; out vec3 lp; out vec2 sp;
void main(){ gl_Position=mvp*vec4(pos,1.0); t0=uv; lp=pos; sp=gl_Position.xy/gl_Position.w; sh=0.40+0.75*clamp(dot(normalize(nm*nrm+vec3(0,0,0.0001)),ldir),0.0,1.0); }'''
FS='''#version 330
in vec2 t0; in float sh; in vec3 lp; in vec2 sp; uniform sampler2D dif; uniform int lit; uniform float amul; uniform float rin; uniform float gain; out vec4 o;
void main(){ if(lit==1 && length(sp)<rin && lp.x>-13.35) discard;   // through the eyecup opening only the first part of the tube wall shows
 vec4 c=texture(dif,t0); float g=mix(0.7,gain,smoothstep(0.76,0.82,length(sp))); o=vec4(lit==1?min(c.rgb*sh*g,vec3(1.0)):lit==2?vec3(0.0):c.rgb, lit==1?1.0:c.a*amul); }'''
def tex(c,path):
    import moderngl
    im=Image.open(path).convert('RGBA');t=c.texture(im.size,4,im.tobytes());t.build_mipmaps();t.filter=(moderngl.LINEAR_MIPMAP_LINEAR,moderngl.LINEAR);return t
def build(size=1024,ss=2):
    """databuilder: drawn on the CPU (scope_cpu.py: the same scene, shaders and blending, deterministic on every PC); the
    original GPU drawing (moderngl + EGL, whose result depends on the GPU and driver) with GITSFA_SCOPE_GPU=1"""
    if os.environ.get('GITSFA_SCOPE_GPU')!='1':
        import scope_cpu; return scope_cpu.build(size,ss)
    return build_gpu(size,ss)
def build_gpu(size=1024,ss=2):
    import moderngl
    c=moderngl.create_standalone_context(backend='egl');N=size*ss
    fbo=c.framebuffer(c.texture((N,N),4),c.depth_renderbuffer((N,N)));fbo.use();fbo.clear(0,0,0,0,depth=1.0)
    ms=static_meshes(SD+'RIFLESCOPE.nif')
    # camera: eye on the scope axis behind the eyecup, looking down +x, z up. camera space: x_c=-y, y_c=z-AXIS_Z, z_c=-(x-eye)
    ex=EYECUP_X-EYE_D;V=np.array([[0,-1,0,0],[0,0,1,-AXIS_Z],[-1,0,0,ex],[0,0,0,1]],float)
    t=HALF*720/F720;n_,f_=1.0,100.0;P=np.zeros((4,4));P[0,0]=P[1,1]=1/t;P[2,2]=(f_+n_)/(n_-f_);P[2,3]=2*f_*n_/(n_-f_);P[3,2]=-1
    MVP=(P@V).astype('f4');pr=c.program(vertex_shader=VS,fragment_shader=FS)
    pr['mvp'].write(MVP.T.tobytes());pr['nm'].write(V[:3,:3].astype('f4').T.tobytes());pr['ldir'].value=(-0.35,0.75,0.56);pr['rin'].value=1.72*F720/EYE_D/(HALF*720);pr['gain'].value=BODY_GAIN
    c.enable(moderngl.DEPTH_TEST);c.disable(moderngl.CULL_FACE)
    def draw(m,T,keep=None,lit=1,amul=1.0):
        I=m['I'].reshape(-1,3)
        if keep is not None: I=I[keep]
        vb=c.buffer(np.c_[m['P'],m['N'],m['UV']].astype('f4').tobytes());ib=c.buffer(I.astype('u4').tobytes())
        va=c.vertex_array(pr,[(vb,'3f 3f 2f','pos','nrm','uv')],ib);pr['lit'].value=lit;pr['amul'].value=amul;T.use(0);va.render(moderngl.TRIANGLES)
    body=tex(c,SD+'RifeScope_D.dds')
    for k,m in enumerate(ms):
        if (m['tex'] or '')!='RifeScope_D.dds' or k==1: continue   # mesh 1: the opaque disc behind the eyepiece glass
        P_=m['P'];I=m['I'].reshape(-1,3);rv=np.hypot(P_[:,1],P_[:,2]-AXIS_Z)
        C=P_[I].mean(1);rc=np.hypot(C[:,1],C[:,2]-AXIS_Z)
        draw(m,body,keep=~(rc<1.6))   # the tube interior and the caps across it are left out (and the shader drops whatever lies behind the eyecup opening)
    # blended layers, farthest first: reticle (x=-8.15), vignette (x=-8.30). Premultiplied output over transparent black.
    c.enable(moderngl.BLEND);c.blend_func=(moderngl.SRC_ALPHA,moderngl.ONE_MINUS_SRC_ALPHA,moderngl.ONE,moderngl.ONE_MINUS_SRC_ALPHA)
    c.depth_mask=False
    # the eyepiece glass (LENS_01 dome, fills the eyecup opening) and the inner vignette (LENS_02) darken the edge of the view:
    # both drawn as black with their texture's alpha (lit=2; LENS_02 at 0.75 to match the footage's band 225-320 px), the reticle
    # with its own colours (black cross, red dot)
    for name,path,lit,am in (('R_SC_CROSS.dds',SD+'R_SC_CROSS.dds',0,1.0),('LENS_02_D.dds',TEXD+'LENS_02_D.dds',2,0.75),('LENS_01_D.dds',TEXD+'LENS_01_D.dds',2,1.0)):
        for m in ms:
            if (m['tex'] or '').lower()==name.lower(): draw(m,tex(c,path),lit=lit,amul=am)
    c.depth_mask=True
    a=np.frombuffer(fbo.color_attachments[0].read(),'u1').reshape(N,N,4)[::-1]
    im=Image.fromarray(a.copy(),'RGBA').resize((size,size),Image.LANCZOS)
    return im
if __name__=='__main__':
    out=sys.argv[1] if len(sys.argv)>1 else gitspaths.tmp('wpn','scope_l118.png');size=int(sys.argv[2]) if len(sys.argv)>2 else 1024
    im=build(size);im.save(out);A=np.asarray(im);print(out,im.size,'alpha>0: %.1f%%'%(100*(A[...,3]>0).mean()))
