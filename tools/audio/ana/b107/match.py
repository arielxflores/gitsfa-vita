import numpy as np,glob,sys,pickle,os
from scipy import signal
from ana import rd,V,sr
b=signal.butter(4,300,btype='high',fs=sr,output='sos')
refs={}
for p in sorted(glob.glob('evs/*.wav')):
    x,_=rd(p);x=signal.sosfilt(b,x)
    if abs(x).max()<1e-4: continue
    e=np.convolve(x**2,np.ones(240)/240,'same');i=int(np.argmax(e));a=max(0,i-1200);seg=x[a:i+3600]   # 25 ms before .. 75 ms after the main peak
    refs[os.path.basename(p)[:-4]]=(seg/np.linalg.norm(seg),i-a)
names=list(refs)
def best(t):
    s=signal.sosfilt(b,V[int((t-0.25)*sr):int((t+0.35)*sr)]);out=[]
    for n,(r,o) in refs.items():
        c=signal.correlate(s,r,'valid');en=np.sqrt(np.convolve(s**2,np.ones(len(r)),'valid'))+1e-9;cc=np.abs(c)/en;j=int(np.argmax(cc));out.append((float(cc[j]),n))
    return sorted(out)[::-1][:3]
for t in [float(a) for a in sys.argv[1].split(',')]:
    print(t,' | '.join('%s %.2f'%(n[7:],c) for c,n in best(t)))
