import numpy as np,subprocess,os,sys
from prof import rd,prof,K
V,sr=rd('/tmp/claude-0/sr/showcase.wav')
C=[('ar/m4a1/reload_02',[15.22,25.02]),('ar/m4a1/reload_03',[16.13]),('ar/acr/reload_03',[88.99]),('ar/ak47/reload_02',[133.45]),('ar/scar/reload_03',[209.02]),('smg/sig_mpx/reload_03',[275.81,371.61]),('hg/mateba/reload_04',[525.96,535.82]),('sg/usas-12/reload_02',[779.52])]
tag=sys.argv[1] if len(sys.argv)>1 else 'x'
D=[]
print('ms after the click:            '+' '.join('%4d'%(k*5) for k in K))
for ev,ts in C:
    f='fit_%s_%s.wav'%(ev.replace('/','_'),tag)
    subprocess.run(['./scene','one','weapon/'+ev,'1','3','0','shooting','0'],capture_output=True);os.replace('one.wav',f)
    x,s2=rd(f);pk,t,sp=prof(x,s2)
    vs=[]
    for tt in ts:
        seg=V[int((tt-0.5)*sr):int((tt+1.6)*sr)];vp=prof(seg,sr,at=0.5)[2];vs.append(vp)
    vm=np.mean(vs,0)
    print('%-24s video %s'%(ev,' '.join('%4.0f'%v for v in vm)));print('%-24s port  %s'%('',' '.join('%4.0f'%v for v in sp)))
    D.append(np.array(vm[0:8])-np.array(sp[0:8]))
D=np.array(D);print('video - port, 20..400 ms, mean per time:',np.round(D.mean(0),1),' overall %.1f dB'%D.mean())
