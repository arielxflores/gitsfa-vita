import numpy as np,glob,os,sys,pickle
from scipy import signal
from prof import rd
V,sr=rd(sys.argv[1]);out=sys.argv[2]
b=signal.butter(4,300,btype='high',fs=sr,output='sos');Vh=signal.sosfilt(b,V).astype(np.float32)
NF=32768;names=[];R=[];off=[]
for p in sorted(glob.glob('evs/*.wav')):
    x,_=rd(p);x=signal.sosfilt(b,x)
    if abs(x).max()<1e-4: continue
    e=np.convolve(x**2,np.ones(240)/240,'same');i=int(np.argmax(e));a=max(0,i-1200);seg=x[a:i+3600];seg=seg/np.linalg.norm(seg)
    names.append(os.path.basename(p)[:-4]);R.append(np.fft.rfft(seg,NF).conj());off.append((i-a,i/sr,len(seg)))
R=np.array(R).astype(np.complex64);L=4800
hop=240;m=len(Vh)//hop;env=10*np.log10((Vh[:m*hop].reshape(m,hop)**2).mean(1)+1e-12)
c=[i for i in range(60,m-120) if env[i]==env[i-12:i+13].max() and env[i]>-34 and env[i]-env[i-14:i-6].mean()>9]
print(len(names),'refs',len(c),'windows',flush=True)
res=[]
W=int(0.45*sr)
for n,i in enumerate(c):
    t=i*hop/sr;s0=int((t-0.2)*sr);s=Vh[s0:s0+W]
    if len(s)<W: continue
    S=np.fft.rfft(s,NF);cc=np.fft.irfft(S[None,:]*R,NF)[:,:W-L]
    cs=np.cumsum(np.concatenate([[0],s.astype(np.float64)**2]));en=np.sqrt(cs[L:L+W-L]-cs[:W-L])+1e-9
    cc=np.abs(cc)/en[None,:];j=np.argmax(cc,1);sc=cc[np.arange(len(names)),j];k=np.argsort(sc)[::-1][:3]
    if sc[k[0]]>0.6: res.append((t,[(names[q],float(sc[q]),(s0+j[q]+off[q][0])/sr-off[q][1]) for q in k]))   # (name, score, the time the EVENT started)
    if n%300==0: print(n,len(res),flush=True)
pickle.dump(res,open(out,'wb'));print('done',len(res))
