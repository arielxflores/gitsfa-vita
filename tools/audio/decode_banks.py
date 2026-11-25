# Decode the FMOD banks (decrypted copies in /root/fsbdec, see scan.py) to int16 .npy at native rate: /root/fsbdec/pcm/<bank>/<name>.npy
import sys,os,re,pickle,subprocess,numpy as np
sys.path.insert(0,'/root/gits')
import fsb5
from multiprocessing import Pool
SRC='/root/fsbdec/'; OUT='/root/fsbdec/pcm/'
def dec_mpeg(args):
    bank,name,freq,ch,n,data=args
    dst=OUT+bank+'/'+name+'.npy'
    if os.path.exists(dst): return name
    r=subprocess.run(['ffmpeg','-v','quiet','-f','mp3','-i','pipe:0','-f','s16le','-ac',str(ch),'-ar',str(freq),'-'],input=data,capture_output=True)
    a=np.frombuffer(r.stdout,'<i2').reshape(-1,ch)[:n]
    np.save(dst,a); return name
def main():
    jobs=[]
    for f in sorted(os.listdir(SRC)):
        if not f.endswith('.fsb'): continue
        bank=f[:10]; os.makedirs(OUT+bank,exist_ok=True)
        mode,smp=fsb5.parse(open(SRC+f,'rb').read())
        meta={}
        for s in smp:
            nm=s['name'].replace('/','_')
            meta[nm]=(s['freq'],s['ch'],s['n'])
            if mode==7:
                dst=OUT+bank+'/'+nm+'.npy'
                if not os.path.exists(dst): np.save(dst,fsb5.ima_xbox(s['data'],s['ch'],s['n']))
            else: jobs.append((bank,nm,s['freq'],s['ch'],s['n'],s['data']))
        pickle.dump(meta,open(OUT+bank+'/meta.pkl','wb'))
        print(f,mode,len(smp),flush=True)
    with Pool(2) as p:
        for i,_ in enumerate(p.imap_unordered(dec_mpeg,jobs,chunksize=8)):
            if i%500==0: print('mpeg',i,'/',len(jobs),flush=True)
if __name__=='__main__': main()
