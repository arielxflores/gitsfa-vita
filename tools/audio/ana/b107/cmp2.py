import numpy as np
from ana import *
sets={'l118a':[1042.2,1044.2,1046.1,1052.7,1055.7,1058.9],'m200':[983.2,995.9],'sv98':[968.3],'seburo_sr':[878.8,881.2]}
T=[0,5,10,15,20,30,40,50,60,70,80,90]
for g,ts in sets.items():
    E=[edc(shot(t)) for t in ts];n=min(len(e) for e in E);v=np.mean([e[:n] for e in E],0)
    def r(p):
        x,_=rd(p);o=onset(x);return edc(x[o:])
    d=r('sr_%s_0.wav'%g);w=r('srv_%s.wav'%g)
    print('%-9s video    '%g,' '.join('%5.0f'%(v[t]-v[:3].max()) for t in T));print('%-9s dry      '%'',' '.join('%5.0f'%(d[t]-d[:3].max()) for t in T));print('%-9s +reverb  '%'',' '.join('%5.0f'%(w[t]-w[:3].max()) for t in T))
print('ms             ',' '.join('%5d'%(t*10) for t in T))
