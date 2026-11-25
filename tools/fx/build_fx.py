# Builds fx.gfx: one DXT5 effects atlas (with mips) + the emitter / effect / decal tables read by engine/src/fx.h.
# Parameters come from the game's own particle NIFs (effect.dat: muzzle/, impact/, character/, weapon/, explosion/), parsed
# by nifps.py / summ.py; values that the NIFs do not pin down (spawn scale, counts trimmed for the Vita) are noted inline.
# Tables are imported by /root/gits/tools/skills/build_skills_fx.py (CELLS / EM / EFFECTS / DECALS / E / drag_k / pack /
# load_cell): only append to them, keep the names.
import sys,struct,os,math,numpy as np,etcpak
from PIL import Image
# databuilder: this is /root/fx/build_fx.py (with the grenade explosion), the version both shipped files were made with:
# fx.gfx (this script) and fx_sk.gfx (tools/skills/build_skills_fx.py imports these tables). It replaces the older copy
# that was here. Inputs: GITSFA_FX/png (effect.dat textures as PNG).
sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'));import gitspaths
PNG=gitspaths.dirp('FX','png')
OUT=sys.argv[1] if len(sys.argv)>1 else gitspaths.path('DATA','fx.gfx')
AW=AH=1024; PAD=8; GRID=16; NMIP=3

# ---------------------------------------------------------------- atlas cells
# name: (png, crop box or None, target w,h, pad mode)   pad: 'clear' (transparent black) or 'grey' (mul2x decals)
CELLS=[
 ('xmuzzle',   'xmuzzle_a', None,(256,128),'clear'),     # 1P flash variants, 3 frames across (SpriteXCount 3, 1/30 s)
 ('6muzzle',   '6muzzle_a', None,(256,128),'clear'),
 ('1muzzle',   '1muzzle_a', None,(256,128),'clear'),
 ('mz_cen',    'muzzle_cen_a',None,(256,64),'clear'),    # centre blob, 3 frames
 ('mz_hor',    'muzzle_hor_a',None,(256,128),'clear'),   # 3P side flame, 3 frames
 ('bullet',    'bullet',(48,0,80,256),(16,128),'clear'), # tracer streak (centre strip of Bullet.dds)
 ('smoke',     'cen_smoke', None,(256,128),'clear'),     # impact puff, 8x4 flipbook
 ('plume',     'impact_base_up',None,(128,256),'clear'), # rising dust plume, 4x4 flipbook (billboardUp)
 ('cement',    'cement_a',  None,(128,128),'clear'),     # 4x4 chunks
 ('wood',      'wood_a',    None,(128,128),'clear'),     # 4x4 splinters
 ('glass',     'glass_8x8', (0,0,512,256),(256,128),'clear'),  # 8x4 shards (top half of the 8x8 sheet)
 ('dirt',      'dirt_brown',None,(128,128),'clear'),     # dirt spray (white, tinted)
 ('dust',      'exp_dust',  None,(128,128),'clear'),     # dust grains
 ('spark_star','spark_sprite2',None,(256,256),'clear'),  # metal hit star, 3x2 frames
 ('spark',     'space_particle2',None,(64,64),'clear'),  # stretched sparks, 2x2
 ('blood',     'blood_a',   None,(256,256),'clear'),     # blood spray, 5x5 flipbook
 ('blood_boom','cen_blood', None,(128,128),'clear'),     # 2x2
 ('blood_smoke','blood_smoke2',None,(256,256),'clear'),  # 8x8 flipbook
 ('soft',      't_soft_smoke',None,(64,64),'clear'),
 ('spark_blue','spark_sprite_blue',None,(192,128),'clear'),  # cyborg hit, 3x2 frames
 ('light_blue','light_blue',None,(64,64),'clear'),
 ('shell',     'sprite_rifle_shell_a',None,(256,128),'clear'),  # 6x2 rotation frames
 ('d_cement',  'cement_decal',None,(128,128),'grey'),    # bullet holes, 2x2 each, drawn DST_COLOR*SRC_COLOR (x2)
 ('d_metal',   'metal_decal', None,(128,128),'grey'),
 ('d_wood',    'wood_decal',  None,(128,128),'grey'),
 ('d_dirt',    'dirt_decal',  None,(128,128),'grey'),
 ('d_blood',   'blood_decal_a',None,(128,128),'grey'),
 ('white',     None,None,(16,16),'clear'),
 # grenade explosion (effect/explosion/explosion_grenade.nif). Its two big flipbooks are thinned out to fit the atlas gaps:
 # 'FR:<png>:<gx>x<gy>:<frames>:<ox>x<oy>' = those frames of a gx x gy sheet re-packed into an ox x oy grid (frames()).
 ('ex_fire',   'FR:a:4x4:0,2,4,6,8,10,12,14:4x2',None,(256,128),'clear'),            # A.dds fireball: 8 of 16 frames, 64 px
 ('ex_smoke',  'FR:explosion_smoke:6x6:0,5,10,15,20,25,30,35:8x1',None,(512,64),'clear'),  # 8 of 36 frames, 64 px
 ('ex_flash',  'exp_start_glow',None,(64,64),'clear'),   # first-frame glow
 ('ex_line',   'exp_line_smoke',None,(16,128),'clear'),  # smoke streak (64 x 512 source)
 ('ex_spark',  'exp_spark',(64,0,192,256),(32,64),'clear'),  # fire streak, head at the top (centre strip of the 256 sheet)
 ('ex_pt',     'exp_pt',    None,(64,64),'clear'),       # 4x4 burning debris
 ('d_scorch',  'b_down',    None,(128,128),'grey'),      # B_Down.DDS ground mark (explosion_camouflage.nif), mul2x
]
def frames(spec):
    """'FR:<png>:<gx>x<gy>:<f0,f1,..>:<ox>x<oy>' -> the listed frames of a gx x gy flipbook, re-packed row-major into ox x oy"""
    _,png,g,fl,o=spec.split(':');gx,gy=map(int,g.split('x'));ox,oy=map(int,o.split('x'));fl=[int(v) for v in fl.split(',')]
    im=Image.open(PNG+png+'.png').convert('RGBA');fw=im.size[0]/gx;fh=im.size[1]/gy;cw,ch=int(round(fw)),int(round(fh))
    out=Image.new('RGBA',(cw*ox,ch*oy))
    for k,f in enumerate(fl):
        box=((f%gx)*fw,(f//gx)*fh,(f%gx+1)*fw,(f//gx+1)*fh);out.paste(im.resize((cw,ch),Image.LANCZOS,box=box),((k%ox)*cw,(k//ox)*ch))
    return out
def load_cell(png,crop,size):
    if png is None:
        a=np.zeros((size[1],size[0],4),np.uint8);a[:]=255
        yy,xx=np.mgrid[0:size[1],0:size[0]];r=np.hypot((xx+.5)/size[0]*2-1,(yy+.5)/size[1]*2-1);a[:,:,3]=(np.clip(1-r,0,1)**1.5*255).astype(np.uint8)
        return Image.fromarray(a)
    im=frames(png) if png.startswith('FR:') else Image.open(PNG+png+'.png').convert('RGBA')
    if crop: im=im.crop(crop)
    return im.resize(size,Image.LANCZOS)

NSHELF=28   # CELLS[:NSHELF] (the original set, up to 'white') keep the shelf layout they always had (same rects as before)
def pack():
    items=[];
    for name,png,crop,size,pm in CELLS:
        w=(size[0]+2*PAD+GRID-1)//GRID*GRID;h=(size[1]+2*PAD+GRID-1)//GRID*GRID;items.append((name,png,crop,size,pm,w,h))
    nb=min(NSHELF,len(items))
    order=sorted(range(nb),key=lambda i:-items[i][6])
    x=y=sh=0;pos={}
    for i in order:
        w,h=items[i][5],items[i][6]
        if x+w>AW: x=0;y+=sh;sh=0
        pos[i]=(x,y);x+=w;sh=max(sh,h)
    assert y+sh<=AH,('atlas overflow',y+sh)
    used=y+sh
    # cells added later go into the gaps: first fit (top-down, left-right) on a GRID occupancy map, tallest first
    occ=np.zeros((AH//GRID,AW//GRID),bool)
    for i,(x0,y0) in pos.items(): occ[y0//GRID:(y0+items[i][6])//GRID,x0//GRID:(x0+items[i][5])//GRID]=True
    for i in sorted(range(nb,len(items)),key=lambda i:(-items[i][6],-items[i][5],i)):
        gw,gh=items[i][5]//GRID,items[i][6]//GRID
        free=~occ;ok=np.zeros_like(occ)   # ok[gy,gx]: the gw x gh block at (gx,gy) is free (2D sliding-window test)
        if gh<=occ.shape[0] and gw<=occ.shape[1]:
            c=np.pad(np.cumsum(np.cumsum(free.astype(np.int32),0),1),((1,0),(1,0)))
            s=c[gh:,gw:]-c[:-gh,gw:]-c[gh:,:-gw]+c[:-gh,:-gw];ok[:s.shape[0],:s.shape[1]]=s==gw*gh
        ys,xs=np.nonzero(ok)
        assert len(ys),('atlas overflow',items[i][0])
        gy,gx=ys[0],xs[0];occ[gy:gy+gh,gx:gx+gw]=True;pos[i]=(int(gx)*GRID,int(gy)*GRID);used=max(used,(int(gy)+gh)*GRID)
    atlas=np.zeros((AH,AW,4),np.uint8);rects={}
    for i,(name,png,crop,size,pm,w,h) in enumerate(items):
        x0,y0=pos[i];im=np.asarray(load_cell(png,crop,size))
        if pm=='grey': blk=np.zeros((h,w,4),np.uint8);blk[:]=(128,128,128,255)
        else: blk=np.zeros((h,w,4),np.uint8)
        ox=(w-size[0])//2;oy=(h-size[1])//2;blk[oy:oy+size[1],ox:ox+size[0]]=im
        if pm=='grey':   # extend the edges outwards so mips never pull in a non-grey border
            blk[:oy,ox:ox+size[0]]=im[0];blk[oy+size[1]:,ox:ox+size[0]]=im[-1]
            blk[:,:ox]=blk[:,ox:ox+1];blk[:,ox+size[0]:]=blk[:,ox+size[0]-1:ox+size[0]]
        atlas[y0:y0+h,x0:x0+w]=blk
        # half-texel inset so bilinear never samples the neighbour
        rects[name]=((x0+ox+0.5)/AW,(y0+oy+0.5)/AH,(x0+ox+size[0]-0.5)/AW,(y0+oy+size[1]-0.5)/AH)
    return atlas,rects,used

# ---------------------------------------------------------------- emitters
KIND={'billboard':0,'stretch':1,'axis':2,'flat':3}
BLEND={'alpha':0,'add':1,'mul2x':2}
DIR={'normal':0,'shot':1,'reflect':2,'sphere':3,'up':4,'ring':5}   # ring: around the normal, elevation within +-spread of its plane
# anchor (kind 'axis'): 0 centred, 1 base at the particle, 2 base at the particle and axis = its own direction (radial streaks)
# jitter < 0: radial start offset |jitter| along the particle's own direction (in addition to `offset` along the normal)
ANIM={'life':0,'random':1,'fps':2,'loop':3}
def E(cell,kind='billboard',blend='alpha',grid=(1,1),nfr=1,anim='life',fps=30,count=(1,1),delay=0,life=(1,0),size=(10,0),aspect=1,
      grow=0,size_end=1,speed=(0,0),spread=0,dir='normal',offset=0,jitter=0,gravity=0,drag=0,rot=(0,0),spin=0,stretch=0,
      colors=((0,(255,255,255,255)),(1,(255,255,255,255))),bright=1.0,cull=0,anchor=0):
    return dict(cell=cell,kind=KIND[kind],blend=BLEND[blend],fx=grid[0],fy=grid[1],nfr=nfr,anim=ANIM[anim],nmin=count[0],nmax=count[1],
        fps=fps,delay=delay,life=life[0],life_var=life[1],size=size[0],size_var=size[1],aspect=aspect,grow=grow,size_end=size_end,
        speed=speed[0],speed_var=speed[1],spread=spread,dir=DIR[dir],offset=offset,jitter=jitter,gravity=gravity,drag=drag,
        rot=rot[0],rot_var=rot[1],spin=spin,stretch=stretch,colors=colors,bright=bright,cull=cull,anchor=anchor)
def drag_k(pct): return -math.log(1-pct)*30   # NiPSDragForce percentage, read as a per-frame (1/30 s) velocity loss
GREY=lambda a0,a1,a2:((0,(253,184,129,a0)),(0.2,(185,180,172,a1)),(0.6,(170,168,165,a2)),(1,(166,166,166,0)))
# Spawn scale: the impact / blood NIFs are authored large (cen_smoke radius 78.7, plume 118 tall). Smoke, blood and
# plumes are drawn at about half the authored size (S); debris keeps 1:1 (its speeds / gravity are physical:
# 354 cm/s, 900 cm/s^2). Smoke alpha is raised over the NIF keys (75 -> 160 at 0.2) so hits read on the small screen.
S=0.5
EM={
 # impact_stage_base / cement / snowbase: cen_smoke.DDS 8x4 over life, SRC_ALPHA/INV_SRC_ALPHA, speed 708+-708,
 # decl var 25 deg, size 78.7+-19.7, life 1.33+-0.33, grow 0.067 s, gravity 450, drag 0.5/0.5/0.28, colour keys as below
 'puff':      E('smoke',grid=(8,4),nfr=32,count=(2,2),life=(1.1,0.4),size=(78.7*S,19.7*S),grow=0.067,size_end=1.35,speed=(520,520),
                spread=0.436,drag=drag_k(0.45),gravity=200,rot=(-0.785,0.785),spin=0.3,colors=GREY(255,160,70),bright=1.2,cull=6000),
 'puff_wood': E('smoke',grid=(8,4),nfr=32,count=(1,2),life=(0.9,0.3),size=(60*S,15*S),grow=0.067,size_end=1.3,speed=(450,450),
                spread=0.436,drag=drag_k(0.45),gravity=200,rot=(-0.785,0.785),spin=0.3,
                colors=((0,(240,190,140,240)),(0.2,(165,135,105,150)),(0.6,(150,125,100,60)),(1,(150,120,90,0))),cull=5000),
 'puff_small':E('smoke',grid=(8,4),nfr=32,count=(1,1),life=(0.7,0.2),size=(50*S,10*S),grow=0.067,size_end=1.3,speed=(300,300),
                spread=0.436,drag=drag_k(0.45),gravity=150,rot=(-0.785,0.785),colors=GREY(220,130,50),cull=4000),
 # impact_stage_base_up: impact_base_up.DDS 4x4 at 1/30 s, billboardUp plane 59x118 standing on the hit point,
 # alpha 1 -> 0 between 0.333 and 0.533 s, emissive 0.624
 'plume':     E('plume',kind='axis',grid=(4,4),nfr=16,anim='fps',fps=30,life=(0.533,0),size=(29.5*0.55,0),aspect=2.0,anchor=1,dir='up',offset=5,
                colors=((0,(185,172,158,255)),(0.625,(185,172,158,255)),(1,(185,172,158,0))),bright=1.25,cull=5000),
 'plume_dirt':E('plume',kind='axis',grid=(4,4),nfr=16,anim='fps',fps=30,life=(0.533,0),size=(29.5*0.5,0),aspect=2.0,anchor=1,dir='up',offset=5,
                colors=((0,(165,140,100,255)),(0.625,(165,140,100,255)),(1,(165,140,100,0))),bright=1.2,cull=5000),
 # impact_stage_cement_pt_A: cement_A 4x4 random, 15 max (trimmed), speed 354+-181, gravity 900, life 0.667, size 1.18+-0.59
 'chunks':    E('cement',grid=(4,4),nfr=16,anim='random',count=(3,4),life=(0.667,0.15),size=(1.6,0.6),speed=(354,361),spread=1.0,
                gravity=900,rot=(-0.785,0.785),spin=12,colors=((0,(200,200,200,255)),(0.8,(200,200,200,255)),(1,(200,200,200,0))),cull=2500),
 # impact_stage_wood_pt_A: wood_A 4x4, size 3.15, same motion
 'splinters': E('wood',grid=(4,4),nfr=16,anim='random',count=(4,5),life=(0.667,0.15),size=(2.6,0.9),speed=(354,361),spread=0.9,
                gravity=900,rot=(-0.785,0.785),spin=14,colors=((0,(255,255,255,255)),(0.8,(255,255,255,255)),(1,(255,255,255,0))),cull=2500),
 # impact_stage_glass_pt_A: glass_8x8 random frame, additive (SRC_ALPHA/ONE), size 3.15, speed 354+-181, gravity 900
 'shards':    E('glass',blend='add',grid=(8,4),nfr=32,anim='random',count=(5,6),life=(0.667,0.15),size=(2.6,0.9),speed=(354,361),
                spread=0.9,gravity=900,rot=(-0.785,0.785),spin=10,cull=2500),
 # impact_stage_metal_pt_A: spark_sprite2 3x2 over life, additive, size 39.4, life 0.1, random roll
 'star':      E('spark_star',blend='add',grid=(3,2),nfr=6,life=(0.1,0),size=(39.4*0.45,0),rot=(-3.14,3.14),offset=1.5,bright=1.3),
 # impact_stage_metal_pt_B/C: space_particle2 2x2 random, additive, speed 1181+-354, life 0.133, size 2.4-4.7,
 # quads stretched along velocity (Scale Amount V 1, limit 25), shrink over 0.167 s
 'sparks':    E('spark',kind='stretch',blend='add',grid=(2,2),nfr=4,anim='random',count=(6,8),life=(0.133,0.06),size=(1.3,0.4),
                size_end=0.3,speed=(1181,708),spread=1.1,gravity=600,stretch=0.03,bright=1.3,cull=4000),
 # impact_stage_dirt: dirt_brown facing quads tinted (114,98,59), alpha 255/203/126/0, speed 708+-708, size 78.7+-19.7
 'dirt':      E('dirt',count=(2,2),life=(1.1,0.4),size=(78.7*S,19.7*S),grow=0.067,size_end=1.3,speed=(520,520),spread=0.436,
                drag=drag_k(0.45),gravity=250,rot=(-2.98,2.98),spin=0.3,
                colors=((0,(114,98,59,255)),(0.2,(114,98,59,203)),(0.5,(114,98,59,126)),(1,(114,98,59,0))),bright=1.4,cull=6000),
 # impact_stage_dirt_pt: exp_dust grains, speed 750+-375, gravity 900, size 100+-50 (x0.2), grow 1.33 s
 'grains':    E('dust',count=(2,2),life=(0.9,0.4),size=(100*0.3,25*0.3),grow=0.25,size_end=1.2,speed=(500,375),spread=0.5,gravity=900,
                rot=(-2.98,2.98),colors=((0,(159,159,159,255)),(0.5,(159,159,159,127)),(1,(159,159,159,0))),cull=4000),
 'fabric':    E('smoke',grid=(8,4),nfr=32,count=(1,2),life=(0.8,0.3),size=(55*S,12*S),grow=0.067,size_end=1.4,speed=(300,300),
                spread=0.5,drag=drag_k(0.45),gravity=100,rot=(-0.785,0.785),
                colors=((0,(215,210,200,255)),(0.2,(190,185,175,190)),(0.6,(175,170,160,80)),(1,(170,165,155,0))),bright=1.15,cull=4000),
 # blood_sprite_A: blood_A 5x5 over 0.833 s, alpha, size 80, speed 240, gravity 675, starts 0.1 s after the hit
 # b59 (user: blood must not be cut down too far): 0.3 -> 0.45 of the authored size, 2 -> 2..3 sprays (BS: the blood scale)
 # b80 (user: blood as the Paz video EP19 - red clouds ~2 m wide on hits, 271-272 / 283 / 410 s): spray 0.7, boom 0.75, smoke 0.8,
 # base flash 0.35 of the authored size (the NIF sizes are quad half-widths: 1:1 would be the PC's; the fill budget thins them)
 'blood':     E('blood',grid=(5,5),nfr=25,count=(2,3),delay=0.05,life=(0.833,0.1),size=(80*0.7,10*0.7),speed=(260,120),spread=1.2,
                dir='reflect',gravity=675,rot=(-0.5,0.5),colors=((0,(255,255,255,255)),(0.5,(255,255,255,128)),(1,(255,255,255,0))),cull=6000),
 # blood_boom: cen_blood 2x2 over 0.133 s, additive, size 100
 'blood_boom':E('blood_boom',blend='add',grid=(2,2),nfr=4,delay=0.05,life=(0.133,0),size=(100*0.75,0),rot=(-3.14,3.14),bright=1.6,
                colors=((0,(255,255,255,255)),(0.5,(255,255,255,128)),(1,(255,255,255,0)))),
 # blood_smoke: blood_smoke2 8x8 over 1.07 s, additive, size 120, colour 255 -> 128 -> 0
 'blood_smoke':E('blood_smoke',blend='add',grid=(8,8),nfr=64,delay=0.08,life=(1.067,0),size=(120*0.8,0),size_end=1.3,rot=(-3.14,3.14),
                speed=(40,20),dir='reflect',colors=((0,(200,200,200,200)),(0.5,(100,100,100,100)),(1,(0,0,0,0))),cull=6000),
 # blood_base: T_Soft_Smoke additive flash, size 130, life 0.2, grow 0.2
 'blood_base':E('soft',blend='add',delay=0.05,life=(0.2,0),size=(130*0.35,0),grow=0.1,rot=(-3.14,3.14),
                colors=((0,(255,255,255,255)),(0.5,(255,255,255,255)),(1,(0,0,0,0)))),
 # body_arm_break_A: spark_sprite_blue 3x2 over 0.1 s (size 50) + light_blue flash (size 100, alpha 128, 0.033 s)
 'cy_star':   E('spark_blue',blend='add',grid=(3,2),nfr=6,life=(0.1,0),size=(50*0.3,0),rot=(-3.14,3.14),bright=1.3),
 'cy_flash':  E('light_blue',blend='add',life=(0.066,0),size=(100*0.3,0),colors=((0,(255,255,255,128)),(1,(255,255,255,90)))),
 'cy_sparks': E('spark',kind='stretch',blend='add',grid=(2,2),nfr=4,anim='random',count=(4,6),life=(0.133,0.06),size=(1.2,0.4),
                size_end=0.3,speed=(1000,600),spread=1.2,gravity=600,stretch=0.03,dir='reflect',
                colors=((0,(150,200,255,255)),(1,(150,200,255,255))),bright=1.3,cull=4000),
 # muzzleflame_A (1P centre): muzzle_cen_A, 27.6 cm billboard, 3 frames at 1/30 s, alpha 1 -> 0 over 0.067-0.1 s, additive
 'fp_cen':    E('mz_cen',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(13.8,0),rot=(-0.25,0.25),offset=2,
                colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0))),bright=0.9),
 # EFFECT_HeavyMuzzleFlame_A / _2 / _3 (1P): xmuzzle (39.4 cm), 6muzzle (27.6), 1muzzle (39.4); one per shot
 'fp_x':      E('xmuzzle',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(19.7,0),rot=(-0.2,0.2),offset=3.8,
                colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0))),bright=1.6),
 'fp_6':      E('6muzzle',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(13.8,0),rot=(-0.3,0.3),offset=3.8,
                colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0))),bright=1.6),
 'fp_1':      E('1muzzle',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(19.7,0),rot=(-0.1,0.1),offset=3.8,
                colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0))),bright=1.6),
 # EFFECT_3P_HeavyMuzzleFlame_A: muzzle_cen_A rigid-centre billboard (44 cm) + muzzle_hor_A billboardUp plane along the
 # barrel (53 x 106 cm, starting at the muzzle); both 3 frames at 1/30 s, fade 0.067-0.1 s. Drawn at 0.7x.
 'tp_cen':    E('mz_cen',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(14.8*1.5*0.7,0),rot=(-0.3,0.3),
                colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0)))),
 'tp_hor':    E('mz_hor',kind='axis',blend='add',grid=(3,1),nfr=3,anim='fps',fps=30,life=(0.1,0),size=(17.7*1.5*0.7,0),aspect=2.0,anchor=1,
                dir='shot',colors=((0,(255,255,255,255)),(0.67,(255,255,255,255)),(1,(255,255,255,0)))),
 # rifle_bullet_shell_1p_A / 3p_A: sprite_rifle_shell_A 6x2 cycling, speed 120+-36, decl var 15 deg, gravity 225,
 # size 2 (1P, weapon space) / 3 (3P). Life trimmed from 2 s.
 'shell_fp':  E('shell',grid=(6,2),nfr=12,anim='loop',fps=40,life=(0.55,0.1),size=(2.0,0),speed=(130,40),spread=0.26,gravity=500,
                colors=((0,(230,230,230,255)),(1,(230,230,230,255)))),
 'shell_tp':  E('shell',grid=(6,2),nfr=12,anim='loop',fps=40,life=(0.7,0.1),size=(3.0,0),speed=(160,40),spread=0.26,gravity=900,
                colors=((0,(230,230,230,255)),(1,(230,230,230,255))),cull=1500),
 # tracer: Bullet.dds streak, additive (fx_tracer sets speed / life from the shot)
 'tracer':    E('bullet',kind='stretch',blend='add',life=(1,0),size=(1.6,0),stretch=0.022,colors=((0,(255,230,170,255)),(1,(255,230,170,255))),bright=1.2),
}
# ---------------------------------------------------------------- grenade explosion (M030 / M67 frag; also the Demolition bomb)
# effect/explosion/explosion_grenade.nif (Gamebryo 30.2, dump: /root/fx/expl/grenade_pp.txt). Unless marked 'chosen', values are
# the NIF's: emitter speed / size / life / angle as base +- half the NIF variance (E() takes the full range like the entries
# above), sizes are particle radii, the scene is authored around the ground point (z up, cm). Bursts: every system is an
# NiPSEmitParticlesCtlr pulse (rate x one frame >= maxp), i.e. it emits its maxp at once. The NIF spawns ~370 particles; here
# ~50 (chosen). Spatial values (sizes, offsets, speeds, gravity) are scaled by GX (chosen) so the blast is ~3.5 m across at its
# peak instead of ~5 m; times are kept. Not used: Plane10 explosion_real_ring (a 0.067 s, nearly black additive ring), Plane03
# flare2 and Plane04 (hidden meshes), the NiMorphWeightsController bends of the streak planes.
GX=0.7
W255=(255,255,255)
EM.update({
 # Plane01: billboard exp_start_glow.DDS, 700 x 700 cm centred at z 90 (lifted to 0.5 x radius, chosen), SRC_ALPHA/ONE,
 # alpha 1 -> 0 and scale 1 -> 0.711 over 0..0.067 s
 'ex_flash':  E('ex_flash',blend='add',life=(0.067,0),size=(350*GX,0),size_end=0.711,offset=0.5*350*GX,colors=((0,W255+(255,)),(1,W255+(0,)))),
 # PArray01: A.dds 4x4 over life (8 of the 16 frames kept), SRC_ALPHA/ONE, radius 300, life 0.267, no motion or roll, born at
 # 0.033 s on the tiny Sphere01 (z 99); white, alpha 255 -> 255 (0.5) -> 0. The NIF stacks 30 identical quads (maxp 30) into
 # an over-bright core: here 2 quads rolled +-0.35 rad (chosen). Centre lifted from z 99 to 0.65 x radius (chosen):
 # without soft particles the NIF's low centre would cut the quad flat along the floor (same for the flash and the smoke)
 'ex_fire':   E('ex_fire',blend='add',grid=(4,2),nfr=8,count=(2,2),delay=0.033,life=(0.267,0),size=(300*GX,0),offset=0.65*300*GX,rot=(-0.35,0.35),
                colors=((0,W255+(255,)),(0.5,W255+(255,)),(1,W255+(0,)))),
 # SuperSpray01 / 02: explosion_smoke.DDS 6x6 over life (8 of 36 frames kept), SRC_ALPHA/INV_SRC_ALPHA, 30 + 30 born at 0.1 s
 # from points at z 85 / 64, speed 105+-31 / 90+-27 in a 25 deg cone, radius 200+-30 / 180+-27, life 1.07+-1.0 / 1.0+-1.0,
 # roll +-0.785, white, alpha 255 -> 161 (0.5) -> 0. Merged into 5 puffs (chosen); life 1.45+-0.55 (chosen: the NIF's +-1 s
 # would thin a 5-puff cloud unevenly); centre z 75 -> 0.7 x radius (chosen, see ex_fire)
 'ex_smoke':  E('ex_smoke',grid=(8,1),nfr=8,count=(5,5),delay=0.1,life=(1.45,1.1),size=(190*GX,57*GX),speed=(97*GX,58*GX),spread=0.436,
                offset=0.7*190*GX,rot=(-0.785,0.785),colors=((0,W255+(255,)),(0.5,W255+(161,)),(1,W255+(0,))),cull=12000),
 # PArray02 / 07: explosion_smoke.DDS emitted from the faces of Torus01 (outer radius ~150, tube ~58, centre z 110) along their
 # normals at 60+-18 / 75+-22, radius 145+-22, life 0.27+-1.0 / 0.53+-1.0, roll +-1.29, 30 + 30 at 0.1 s. Here 4 puffs
 # (chosen) leaving a ring (dir 'ring', elevation +-45 deg) 110 cm out (radial jitter) at z 110; life 0.7+-0.4 (chosen)
 'ex_rsmoke': E('ex_smoke',grid=(8,1),nfr=8,count=(4,4),delay=0.1,life=(0.7,0.8),size=(145*GX,43*GX),speed=(67*GX,40*GX),dir='ring',spread=0.785,
                offset=110*GX,jitter=-110*GX,rot=(-1.29,1.29),colors=((0,W255+(255,)),(0.5,W255+(161,)),(1,W255+(0,))),cull=12000),
 # Plane02 / 05 / 06 / 07 / 08 / 09: billboardUp planes of exp_line_smoke.DDS (SRC_ALPHA/INV_SRC_ALPHA; lit material, emissive
 # 0.318 + diffuse 1 -> grey 130 here, chosen): 40 x 326 cm strips (mesh scale 0.4..0.6, ~0.47) based 240 cm out from the centre
 # along their own axis (elevations 10..79 deg), node scale 1 -> 2 (by 0.067 s) -> 2.4 (1.6 s), alpha 0 -> 1 (0.033..0.1 s) -> 0
 # (1.6 s); three start at 0.033 s, three at 0.1 s. 'axis' quads with anchor 2 (axis = own direction, base at the radial start):
 # half width 20 x 0.47 x 2 = 18.8 at scale 2, reached by grow 0.067 s, then size_end 1.2 (-> 2.4); aspect 326 / 40 = 8.15
 'ex_lines':  E('ex_line',kind='axis',count=(6,6),delay=0.033,life=(1.57,0.1),size=(18.8*GX,4*GX),grow=0.067,size_end=1.2,aspect=8.15,anchor=2,
                spread=1.38,jitter=-240*GX,colors=((0,(130,128,125,0)),(0.045,(130,128,125,255)),(1,(130,128,125,0))),cull=12000),
 # PArray05: exp_spark.dds, SRC_ALPHA/ONE, quads aligned to the velocity (scale amount 1, limit 5: up to 5x long), 50 born in
 # 0..0.033 s (1500/s) on the base_spark dome (100 x 100 x 31 cm at z 102) along its normals +-34 deg, speed 3000+-900,
 # radius 12+-3, life 0.13+-0.17, shrink 0.167 s. Here 14 (chosen), a 70 deg cone (dome normals + 34 deg)
 'ex_spark':  E('ex_spark',kind='stretch',blend='add',count=(14,14),life=(0.17,0.2),size=(12*GX,6*GX),size_end=0.3,speed=(3000*GX,1800*GX),
                spread=1.2,offset=102*GX,stretch=0.036,bright=1.2,cull=8000),
 # PCloud01: exp_pt.dds 4x4 over life (burning debris), SRC_ALPHA/ONE, 100 born at 0.167..0.2 s in a 300 x 300 x 441 cm box
 # centred at z 236, speed 60+-60 any direction, radius 2+-0.3, life 1.17+-1.0, shrink 0.167 s (-> alpha fade). Here 12 (chosen),
 # radius x2 (chosen: 4 cm pieces are 1-2 px on the Vita), the box as a +-105 cm cube (jitter)
 'ex_embers': E('ex_pt',blend='add',grid=(4,4),nfr=16,count=(12,12),delay=0.167,life=(1.17,1.2),size=(4*GX,1.2*GX),speed=(60*GX,120*GX),
                dir='sphere',offset=236*GX,jitter=150*GX,colors=((0,W255+(255,)),(0.85,W255+(255,)),(1,W255+(0,))),bright=1.3,cull=6000),
 # SuperSpray03: exp_dust.DDS grains (cell 'dust'), SRC_ALPHA/INV_SRC_ALPHA, 10 born at 0 s from z 196, speed 750+-375 in a
 # 15 deg cone, gravity 900, radius 200+-30 growing from 0 over 1.33 s, life 1.33+-1.0, (75,60,0) alpha 255 -> 127 -> 0.
 # Here 3 (chosen), life 1.33+-0.5 (chosen)
 'ex_dirt':   E('dust',count=(3,3),life=(1.33,1.0),size=(200*GX,60*GX),grow=1.333,speed=(750*GX,750*GX),spread=0.262,offset=196*GX,
                gravity=900*GX,rot=(-1.24,1.24),colors=((0,(75,60,0,255)),(0.5,(75,60,0,127)),(1,(75,60,0,0))),cull=8000),
})
EFFECTS=[ # id, name, emitters (first `pick` of them: one chosen at random; pick < 0: also spawn effect -pick)
 ('IMPACT_DEFAULT', ['puff','plume','chunks'],0),
 ('IMPACT_WOOD',    ['puff_wood','splinters'],0),
 ('IMPACT_METAL',   ['star','sparks','puff_small'],0),
 ('IMPACT_SAND',    ['dirt','grains','plume_dirt'],0),
 ('IMPACT_GLASS',   ['shards','star','puff_small'],0),
 ('IMPACT_CAR',     ['star','sparks','puff_small'],0),
 ('IMPACT_BOX',     ['puff_wood','splinters'],0),
 ('IMPACT_SOFT',    ['fabric'],0),
 ('IMPACT_CONCRETE',['puff','plume','chunks'],0),
 ('HIT_BLOOD',      ['blood','blood_boom','blood_smoke','blood_base'],0),
 ('HIT_CYBORG',     ['cy_star','cy_flash','cy_sparks'],0),
 ('MUZZLE_3P',      ['tp_cen','tp_hor'],0),
 ('MUZZLE_FP',      ['fp_x','fp_6','fp_1','fp_cen'],3),
 ('SHELL_FP',       ['shell_fp'],0),
 ('SHELL_3P',       ['shell_tp'],0),
 ('TRACER',         ['tracer'],0),
 # 16: grenade explosion (fx_explosion); more than 6 emitters, so it chains to EXPL_DEBRIS (resolved to -id below)
 ('EXPLOSION',      ['ex_flash','ex_fire','ex_smoke','ex_rsmoke','ex_lines','ex_spark'],'EXPL_DEBRIS'),
 ('EXPL_DEBRIS',    ['ex_embers','ex_dirt'],0),
]
EFFECTS=[(n,ems,-[x[0] for x in EFFECTS].index(p) if isinstance(p,str) else p) for n,ems,p in EFFECTS]
# per material (mat_hit index 0..9): decal cell, grid, half size (cm), life (s); 10: explosion scorch (fx_scorch)
DECALS=[('d_cement',2,8.0,25),('d_wood',2,7.5,25),('d_metal',2,7.0,25),('d_dirt',2,8.5,20),('d_metal',2,6.0,25),
        ('d_metal',2,7.0,25),('d_wood',2,7.5,25),('d_dirt',2,7.0,15),('d_cement',2,8.0,25),('d_blood',1,22.0,20),
        ('d_scorch',1,100*GX,6)]   # explosion_camouflage.nif: B_Down plane 200 x 200 cm, DST_COLOR/SRC_COLOR, hidden at 6 s

def main():
    atlas,rects,used=pack()
    names=list(rects.keys());cid={n:i for i,n in enumerate(names)}
    emn=list(EM.keys());eid={n:i for i,n in enumerate(emn)}
    # mips: box-filtered whole atlas (cells are 16-px aligned with 8 px padding)
    levels=[atlas];cur=Image.fromarray(atlas)
    for m in range(1,NMIP): cur=cur.resize((AW>>m,AH>>m),Image.BOX);levels.append(np.asarray(cur))
    Image.fromarray(atlas).save(gitspaths.tmp('fx','atlas.png'))
    with open(OUT,'wb') as f:
        f.write(b'FXG1');f.write(struct.pack('<HHI',AW,AH,NMIP))
        for lv in levels:
            h_,w_=lv.shape[:2];b=etcpak.compress_to_dxt5(np.ascontiguousarray(lv).tobytes(),w_,h_);f.write(struct.pack('<I',len(b)));f.write(b)
        f.write(struct.pack('<I',len(names)))
        for n in names: f.write(struct.pack('<4f',*rects[n]))
        f.write(struct.pack('<I',len(emn)))
        for n in emn:
            e=EM[n];ck=list(e['colors'])[:4]
            while len(ck)<4: ck.append(ck[-1])
            f.write(struct.pack('<12i',cid[e['cell']],e['kind'],e['blend'],e['fx'],e['fy'],e['nfr'],e['anim'],e['nmin'],e['nmax'],e['dir'],len(e['colors'][:4]),e['anchor']))
            f.write(struct.pack('<24f',e['fps'],e['delay'],e['life'],e['life_var'],e['size'],e['size_var'],e['aspect'],e['grow'],e['size_end'],
                e['speed'],e['speed_var'],e['spread'],e['offset'],e['jitter'],e['gravity'],e['drag'],e['rot'],e['rot_var'],e['spin'],e['stretch'],
                e['bright'],e['cull'],0.,0.))
            f.write(struct.pack('<4f',*[k[0] for k in ck]))
            for k in ck: f.write(bytes(k[1]))
        f.write(struct.pack('<I',len(EFFECTS)))
        for name,ems,pick in EFFECTS:
            assert len(ems)<=6
            f.write(name.encode().ljust(16,b'\0'));f.write(struct.pack('<8i',len(ems),pick,*([eid[x] for x in ems]+[-1]*(6-len(ems)))))
        f.write(struct.pack('<I',len(DECALS)))
        for c,g,s,l in DECALS: f.write(struct.pack('<2i2f',cid[c],g,s,l))
    print(OUT,os.path.getsize(OUT),'bytes; atlas rows used',used,'cells',len(names),'emitters',len(emn),'effects',len(EFFECTS))
    with open(gitspaths.tmp('fx','fx_ids.txt'),'w') as f:
        for i,(n,_,_) in enumerate(EFFECTS): f.write('FX_%s=%d\n'%(n,i))
if __name__=='__main__': main()
