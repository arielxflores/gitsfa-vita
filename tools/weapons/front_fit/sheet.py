import sys,os;sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import side
from PIL import Image,ImageDraw
MAP=dict(m4a1='M4A1',scar='SCAR',acr='ACR',ak47='AK47',k2='K2',arx160='ARX_160',hk417='HK417',f2000='F2000',famas='FAMAS',c30='SEBURO_C30',x95r='X95-R',mp5='MP5',kriss='KRISS',c26a='SEBURO_C26A',evo='SCORPIONEVO',kacpdw='KAC_PDW',mp5k='MP5K',mpx='MPX',p90='P90',pp2000='PP2000',ump45='UMP45',l118='L118A',cm2010='XM2010',mm200='M200',m99d='M82',sa50='AS50',seburosr='SEBURO_SR',seo90='MSG90',srv97='SV98',ws2088='WA2000',mag44='44MAGNUM',b93se='93R',kcz85='BULSTORM',sn57='FN57',p226='SIGP226',m2008='2008M',l86='L86LSW',mg33='MG3',mg36='MG36',seburomg='SEBURO_MG',ksg='KSG',sta12='UTS15',uss12='USAS',**{'870':'870MCS'},m249='M249',seburom5='SEBUROM5',mateba='MATEBA')
D=sys.argv[1];guns=sys.argv[3:] or list(MAP);out=sys.argv[2]
W,H=384,150;cols=2;rows=(len(guns)+cols-1)//cols
S=Image.new('RGB',(W*2*cols,H*rows),(20,20,20));dr=ImageDraw.Draw(S)
for i,g in enumerate(guns):
    x=(i%cols)*W*2;y=(i//cols)*H
    try: im,info=side.render(D+'/vm_%s.gvm'%g);S.paste(im,(x,y))
    except Exception as e: dr.text((x+4,y+60),'%s: %s'%(g,e),fill=(255,80,80))
    pc=Image.open('/tmp/claude-0/pcui/%s_BASIC_01.png'%MAP[g]).convert('RGBA');bg=Image.new('RGBA',pc.size,(52,44,46,255));bg.alpha_composite(pc);S.paste(bg.convert('RGB'),(x+W,y))
    dr.text((x+4,y+2),g,fill=(255,255,0))
S.save(out,quality=90)
