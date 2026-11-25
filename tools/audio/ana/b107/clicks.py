import numpy as np,sys
from scipy import signal
from prof import rd,K
V,sr=rd(sys.argv[1])
b=signal.butter(4,300,btype='high',fs=sr,output='sos');Vh=signal.sosfilt(b,V)
hop=240;m=len(Vh)//hop;env=10*np.log10((Vh[:m*hop].reshape(m,hop)**2).mean(1)+1e-12)
print('level pct',[round(float(np.percentile(env,p)),1) for p in (5,25,50,75,95)])
c=[]
for i in range(200,m-400):
    if env[i]==env[i-6:i+7].max() and env[i]-env[i-14:i-8].mean()>16 and env[i-100:i+160].max()<=env[i]+0.01 and env[i]-np.percentile(env[i-400:i+400],10)>30: c.append(i)
P=[]
for i in c:
    seg=env[i:i+200]-env[i];P.append([seg[k] for k in K])
    if len(P)<=int(sys.argv[2]) if len(sys.argv)>2 else 25: print('%7.2f s pk %5.1f floor %4.0f | %s'%(i*hop/sr,env[i],np.percentile(env[i-400:i+400],10)-env[i],' '.join('%4.0f'%v for v in P[-1])))
print(len(c),'clicks; median profile:',' '.join('%4.0f'%v for v in np.median(P,0)))
