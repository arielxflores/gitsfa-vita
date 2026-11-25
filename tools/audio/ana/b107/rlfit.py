import pickle,re,collections,sys
import numpy as np
res=pickle.load(open('tl_showcase.pkl','rb'))
# event -> samples
ev2s={};cur=None
for l in open('dump_weapon.txt'):
    if l.startswith('weapon/'): cur=l.split()[0];ev2s[cur]=set()
    elif cur and l.startswith('    k'):
        s=l.split()[4];ev2s[cur].add(re.sub(r'(_?\d+)?\*?$','',s) if s.endswith('*') else s)
def evname(ref): return None
# refs are named weapon_<cls>_<group>_<event>; rebuild a map file-stem -> event path
stem2ev={e.replace('/','_'):e for e in ev2s}
SND={}
for l in open('/root/gits/engine/src/audio.h'):
    for m in re.finditer(r'\{ "(\w+)", "(weapon/[^"]+)"',l): SND[m.group(1)]=m.group(2)
SEC=[(5,'m4a1'),(27,'c30'),(45,'x95r'),(63,'hk417'),(83,'acr'),(101,'famas'),(117,'ak47'),(137,'k2'),(154,'f2000'),(172,'scar'),(195,'arx160'),(213,'mp5'),(232,'c26a'),(251,'evo'),(270,'mpx'),(288,'ump45'),(307,'p90'),(330,'kriss'),(348,'mp5k'),(365,'kacpdw'),(385,'pp2000'),(402,None),(421,'seburom5'),(444,'kcz85'),(469,'b93se'),(492,'mag44'),(515,'mateba'),(540,'sn57'),(569,'seburomg'),(594,'l86'),(620,'m249'),(658,'mg33'),(696,'mg36'),(724,'ksg'),(765,'uss12'),(783,'870'),(816,'sta12'),(868,'seburosr'),(895,'cm2010'),(926,'seo90'),(955,'srv97'),(981,'mm200'),(1010,'m99d'),(1046,'l118'),(1070,'sa50'),(1128,None)]
ROWS=collections.defaultdict(dict)
for l in open('/root/gits/build/data/wpn_snd.txt'):
    if l.startswith('#') or not l.strip(): continue
    sh,cl,ev=l.rstrip('\n').split('|');ROWS[sh][cl]=[(float(a.split(':')[0]),a.split(':')[1]) for a in ev.split()]
LEN={}
for l in open('/root/gits/build/data/weapons.txt'):
    if l.startswith('#'): continue
    f=l.split('|');LEN[f[0]]=(float(f[13]),float(f[14]))
out={}
for (a,sh),(b,_) in zip(SEC,SEC[1:]):
    if sh is None or sh not in ROWS: continue
    base=SND.get(sh)
    M=[]   # (time, samples of the matched events (top 3 with score > 0.6))
    for t,c in res:
        if a<=t<b:
            for n,sc,t0 in c:
                if sc<0.6: continue
                e=stem2ev.get(n)
                if e: M.append((t0,ev2s[e],sc))
    for ci,cl in enumerate(('reload','reload_empty')):
        if cl not in ROWS[sh]: continue
        E=[(f,ev2s.get(base+'/'+n,set())) for f,n in ROWS[sh][cl]];E=[e for e in E if e[1]]
        if len(E)<2: continue
        best=[]
        for T in np.arange(1.0,9.0,0.02):
            for (t0,S,sc) in M:
                for k,(fk,Sk) in enumerate(E):
                    if not (S&Sk): continue
                    start=t0-fk*T;hit=0;err=0
                    for (f2,S2) in E:
                        d=[abs(t2-(start+f2*T)) for (t2,S3,_) in M if S3&S2]
                        if d and min(d)<0.07: hit+=1;err+=min(d)
                    best.append((hit,-err,round(float(T),2),round(start,2)))
        best.sort(reverse=True)
        # the best fits at distinct start times
        seen=[];top=[]
        for h,e,T,s in best:
            if h<min(3,len(E)): break
            if any(abs(s-x)<1.5 for x in seen): continue
            seen.append(s);top.append((h,len(E),T,s,round(-e/h,3)))
            if len(top)>=2: break
        print('%-9s %-12s clip %5.2f s  fits (events hit/of, T, start, mean err): %s'%(sh,cl,LEN[sh][ci],top))
        out[(sh,cl)]=top
pickle.dump(out,open('rlfit.pkl','wb'))
