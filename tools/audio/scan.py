# decrypt every FMOD bank once and list codec + samples
import sys,os,pickle;sys.path.insert(0,'/root/gits')
import fsb5
SRC='/mnt/user-data/uploads/Ghost_in_the_Shell_Stand_Alone_Complex_First_Assault_Online_Win_Files_EN/Game Files/Data/audio/Fmod_Build/'
OUT='/root/fsbdec/'
info={}
for f in sorted(os.listdir(SRC)):
    if not f.endswith('.fsb'): continue
    dst=OUT+f
    if not os.path.exists(dst):
        d=fsb5.load(SRC+f)
        if d[:4]!=b'FSB5': d=open(SRC+f,'rb').read()
        open(dst,'wb').write(d)
    d=open(dst,'rb').read(); mode,smp=fsb5.parse(d)
    tot=sum(s['n']/s['freq'] for s in smp)
    print(f,'mode',mode,'samples',len(smp),'%.0f s'%tot, 'freqs',sorted(set(s['freq'] for s in smp)),'ch',sorted(set(s['ch'] for s in smp)))
    info[f]=(mode,[(s['name'],s['freq'],s['ch'],s['n']) for s in smp])
pickle.dump(info,open(OUT+'info.pkl','wb'))
