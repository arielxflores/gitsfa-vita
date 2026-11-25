# Per-map sky -> <stem>.gsky
#   dome maps:  world.dat world\skydome\world_SKY.nif (one hemisphere, radius 135 m, polar texture) + the map's
#               world_SKY_<code>.dds, drawn centred on the camera behind everything
#   VR maps:    the cyber sky the map's WorldEffectFx.wff loads (effect.dat effect\tutorial\tutorial_shooting_sky.nif,
#               tutorial_bg.nif): additive cyan sphere + line cylinders, slowly scrolling / turning
# usage: make_sky.py SKYDIR EFFECTDIR OUTDIR [stem ...]
#   SKYDIR = extracted world\skydome (and ..\cybersky), EFFECTDIR = extracted effect\tutorial
#
# .gsky v1 (little endian):
#   'GSKY' u32 version=1, u32 nlayer, u32 ntex
#   ntex x { u16 w, u16 h, u8 fmt (1 DXT1, 5 DXT5), u8 nmips, nmips x { u32 size, data } }
#   nlayer x { i32 tex, u32 flags, f32 tint[4], f32 scroll_u, f32 scroll_v, f32 rot_z, f32 fog, u32 nv, u32 ni,
#              nv x (pos3, uv2) f32, ni x u16 (+u16 pad if ni odd) }
#   flags: 1 additive blend (src alpha, one), 2 alpha blend, 4 sky layer (centred on the camera, drawn at the far
#          plane after the opaque world), else a world-space layer (real depth, no depth writes)
#   tint multiplies the texture (rgb, alpha); fog = how much of the map's fog colour covers the layer;
#   scroll_* = texture scroll per second, rot_z = turn around the vertical axis in radians per second
import sys,os,struct,json,math,numpy as np
from PIL import Image
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import map_visual as mv
from nifmesh import load,S
# sky texture per map. The skydome textures carry the same codes as the maps' colour-grading LUTs (world.ini ColorCgInfo):
# certain by code: pss9 DM01, downtown DM02, zioplant DM03, ghost_storage TDM01, port TDM02, urban TDM03, ruinednight TDM05,
# ruinedtokyo TTM01, berutalube TTM02. ecenter / trainstation have no sky code of their own (E_Center0, DEM_Train CG):
# inferred, see SKYMAP_NOTES. headquarter, underground, tutorial, tutorial2: no sky is visible anywhere in the map.
SKYMAP={'pss9':'world_SKY_DM01.dds','downtown':'world_SKY_DM02.dds','zioplant':'world_SKY_DM03.dds',
        'ghost_storage':'world_SKY_tdm01.dds','port':'world_SKY_TDM02.dds','urban':'world_SKY_TDM03.dds',
        'ruinednight':'world_SKY_TDM05.dds','ruinedtokyo':'world_SKY_ttm01.dds','berutalube':'world_SKY_ttm02.dds',
        'ecenter':'world_SKY_TDM06.dds','trainstation':'world_SKY_ttm11.dds',
        'shooting':'fx:tutorial_shooting_sky.nif','tutorial3':'fx:tutorial_bg.nif'}
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)),'databuilder'));import mapini   # databuilder: the maps' fog, read from their World.ini
DOME_SCALE=0.4      # 135 m dome -> 54 m around the camera (inside the 120 m far plane; drawn at the far plane anyway)
FX_SCALE=0.2
SKY_FOG=float(os.environ.get('GITS_SKY_FOG','0.5'))   # share of the map's fog amount at the dome's distance
# b107 (user, a Train Station screenshot from the Vita: "the sky looks a little grey in train station, is it supposed to look
# like that?"): no. The PC's map runs with HDR and bloom on (its World.ini [NIHDR], [NINEWBLOOM], [NEWTONE]) and its sky is a
# bright, nearly even haze: 230 225 231 in the footage (the Train Station video 5:18-5:21, 6:42-6:43; darkest patches 218
# 210 217), where the port's plain texture + 29 % of the map's yellow fog + the grading came out a flat mid grey (170-178
# on the Vita; the sunlit floor, 213 208 191, matched the PC's 215 213 219 - the sky alone was off). The port has no HDR
# pass, so the look is put into the map's own sky texture: its colours are drawn in toward the colour that the map's
# grading (the affine fit the sky program uses, <stem>.glut) turns into the footage's 230 225 231, keeping 40 % of the
# texture's own variation (the clouds and the sun's patch stay, as faint as on the PC), and the layer takes none of the fog.
#   'to': the texture's new mean colour (before grading), 'contrast': the share of its own variation kept, 'fog': the layer's
SKY_LOOK={'trainstation':dict(to=(216,225,249),contrast=0.4,fog=0.0)}
def look(im,lk):
    a=np.asarray(im.convert('RGB')).astype(float)/255;h,w,_=a.shape
    yy,xx=np.mgrid[0:h,0:w];disc=np.hypot(xx-w/2,yy-h/2)<0.47*w   # (the dome's polar picture: the corners are not sky)
    mean=a[disc].mean(0);o=np.clip(np.array(lk['to'])/255.0+lk['contrast']*(a-mean),0,1)
    return Image.fromarray((o*255+0.5).astype('u1'),'RGB'),mean
def tex_block(im,fmt,maxs):
    w,h=im.size;sc=min(1,maxs/max(w,h));w=1<<(int(w*sc).bit_length()-1);h=1<<(int(h*sc).bit_length()-1)
    im=im.convert('RGBA').resize((w,h),Image.LANCZOS);mm=mv.TexStore._mips(im,fmt)
    return struct.pack('<HHBB',w,h,fmt,len(mm))+b''.join(struct.pack('<I',len(b))+b for b in mm)
def layer(tex,flags,tint,P,uv,I,scroll=(0,0),rot=0.0,fog=0.0):
    P=np.asarray(P,'<f4');uv=np.asarray(uv,'<f4');I=np.asarray(I,'<u2').ravel()
    b=struct.pack('<iI4f4fII',tex,flags,*tint,scroll[0],scroll[1],rot,fog,len(P),len(I))
    b+=np.c_[P,uv].astype('<f4').tobytes()+I.tobytes()
    if len(I)%2: b+=b'\0\0'
    return b
def dome(skydir,texname,stem):
    sc=mv.scene(os.path.join(skydir,'skydome','world_SKY.nif'));m=sc[0]
    P=m['streams'][('POSITION',0)][0];W=m['W'];P=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]*DOME_SCALE
    uv=m['streams'][('TEXCOORD',0)][0];I=m['streams'][('INDEX',0)][0].ravel()
    # fog: the map's exponential fog (world.ini) at the dome's radius, scaled by SKY_FOG (the sky keeps most of its own colour)
    fc,fp=mapini.fog(stem);R=13530.0;fog=fp[2]*(1-math.exp(-max(R-fp[0],0)*fp[1]))*SKY_FOG
    im=Image.open(os.path.join(skydir,'skydome',texname)).convert('RGB')
    lk=SKY_LOOK.get(stem)
    if lk: im,mean=look(im,lk);fog=lk['fog'];print('   %s: sky look - the texture\'s mean %s drawn to %s, contrast %.2f, fog %.2f'%(stem,np.round(mean*255),lk['to'],lk['contrast'],fog))
    return [tex_block(im,1,1024)],[layer(0,4,(1,1,1,1),P,uv,I,fog=fog)],fog
def _ctrl_rate(h,nodeblk):
    """turn rate (rad/s around z) and texture u-scroll (1/s) from the transform / texture-transform controllers of a block"""
    d=h['d'];B=h['blocks'];t,o,s=B[nodeblk]
    ne=struct.unpack_from('<I',d,o+4)[0];c=struct.unpack_from('<i',d,o+8+4*ne)[0];rot=0.0
    while 0<=c<len(B):
        ct,co,cs=B[c];nxt,fl,fr,ph,t0,t1,tg,ip=struct.unpack_from('<iHffffii',d,co)
        if ct=='NiTransformController' and 0<=ip<len(B):
            dref=struct.unpack_from('<i',d,B[ip][1]+32)[0]
            if 0<=dref<len(B):
                do=B[dref][1];nk,kt=struct.unpack_from('<II',d,do);q=do+8
                if nk and kt==4:
                    vals=[]
                    for axis in range(3):
                        n,it=struct.unpack_from('<II',d,q);q+=8;ks=[]
                        for k in range(n):
                            tk,v=struct.unpack_from('<ff',d,q);q+=8+(8 if it==2 else 12 if it==3 else 0);ks.append((tk,v))
                        vals.append(ks)
                    z=vals[2]
                    if len(z)>=2 and z[-1][0]>z[0][0]: rot=(z[-1][1]-z[0][1])/(z[-1][0]-z[0][0])
        c=nxt
    return rot
CYBER_BG=(1/255.0,0.0,14/255.0)   # the VR maps' sky behind the additive layers is near-black navy in the footage (darker than their fog colour)
CYBER_GAIN=0.6                     # additive layers: the footage shows them dimmer than emissive x texture
def fx_layers(skydir,effdir,nif,stem):
    h=load(os.path.join(effdir,nif));sc=mv.scene(os.path.join(effdir,nif))
    # node blocks by name, for the controllers
    names={}
    for k,(t,o,s) in enumerate(h['blocks']):
        if t in('NiNode','NiMesh'): names.setdefault(S(h,struct.unpack_from('<I',h['d'],o)[0]),k)
    texs=[];tid={};layers=[]
    # opaque background: the skydome mesh in a flat colour
    d=mv.scene(os.path.join(skydir,'skydome','world_SKY.nif'))[0];DP=d['streams'][('POSITION',0)][0];DW=d['W']
    DP=(np.c_[DP,np.ones(len(DP))]@DW.T)[:,:3]*DOME_SCALE
    texs.append(tex_block(Image.new('RGB',(4,4),(255,255,255)),1,4))
    # extend the hemisphere down so the floor's edge never shows the clear colour: mirror it below the horizon
    DI=d['streams'][('INDEX',0)][0].ravel();DPm=DP.copy();DPm[:,2]=-DPm[:,2]
    P2=np.r_[DP,DPm];I2=np.r_[DI,DI.reshape(-1,3)[:,::-1].ravel()+len(DP)]
    layers.append(layer(0,4,CYBER_BG+(1.0,),P2,np.zeros((len(P2),2)),I2))
    for m in sc:
        P=m['streams'][('POSITION',0)][0];W=m['W'];Pw=(np.c_[P,np.ones(len(P))]@W.T)[:,:3]
        rad=np.hypot(Pw[:,0],Pw[:,1]).max()
        if rad<2000: continue                      # tutorial markers / icons are gameplay effects, not scenery
        tp=m['props'].get('NiTexturingProperty',{});b=tp.get('base',{});fn=b.get('file')
        if not fn: continue
        path=None
        for dd in (effdir,os.path.join(skydir,'cybersky'),os.path.join(effdir,'..','texture'),os.path.join(effdir,'..','tutorial','texture')):
            for f in (os.listdir(dd) if os.path.isdir(dd) else []):
                if f.lower()==fn.lower(): path=os.path.join(dd,f)
        if path is None:
            for root,_,fs in os.walk(os.path.join(effdir,'..','..')):
                for f in fs:
                    if f.lower()==fn.lower(): path=os.path.join(root,f)
        if path is None: print('  sky texture missing',fn);continue
        if fn.lower() not in tid:
            tid[fn.lower()]=len(texs);texs.append(tex_block(Image.open(path),5 if Image.open(path).mode=='RGBA' else 1,512))
        mat=m['props'].get('NiMaterialProperty',dict(emi=(1,1,1),alpha=1.0));al=m['props'].get('NiAlphaProperty')
        flags=(1 if al and al['dst']==0 else 2 if al and al['blend'] else 0)
        uv=m['streams'][('TEXCOORD',0)][0].copy();tt=b.get('tt');scroll=(0.0,0.0)
        if tt is not None:
            c=np.array(tt[6:8]);uv=(uv-c)*np.array(tt[2:4])+c+np.array(tt[0:2]);scroll=(-1/200.0,0.0)   # cyber_sky1: u -1 over 200 s
        rot=0.0
        for part in m['path'].split('/')[::-1]:
            if part in names: rot=_ctrl_rate(h,names[part])
            if rot: break
        far=rad>6000
        if far: Pw=Pw*FX_SCALE;flags|=4
        tint=tuple(np.array(mat['emi'])*CYBER_GAIN)+(mat['alpha'],)
        layers.append(layer(tid[fn.lower()],flags,tint,Pw,uv,m['streams'][('INDEX',0)][0].ravel(),scroll,rot,0.0))
        print('   layer %-40s %-18s r %6.0f flags %d tint %s rot %.4f'%(m['path'][-40:],fn,rad,flags,np.round(tint,2),rot))
    return texs,layers
def build(skydir,effdir,outdir,stem):
    src=SKYMAP.get(stem)
    if not src: return None
    if src.startswith('fx:'): texs,layers=fx_layers(skydir,effdir,src[3:],stem);info='%d cyber layers'%len(layers)
    else: texs,layers,fog=dome(skydir,src,stem);info='%s fog %.2f'%(src,fog)
    with open(os.path.join(outdir,stem+'.gsky'),'wb') as f:
        f.write(b'GSKY'+struct.pack('<III',1,len(layers),len(texs)));[f.write(t) for t in texs];[f.write(l) for l in layers]
    print(stem,'sky:',info,'%.0f KB'%(os.path.getsize(os.path.join(outdir,stem+'.gsky'))/1024))
    return info
if __name__=='__main__':
    skydir,effdir,outdir=sys.argv[1:4];stems=sys.argv[4:] or sorted(SKYMAP)
    for s in stems: build(skydir,effdir,outdir,s)
