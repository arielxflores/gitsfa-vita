# Builds the Vita audio data from the decoded FMOD banks (decode_banks.py):
#   sfx.gsnd        GSN2 pack held in RAM (block IMA ADPCM, mono except first-person sounds)
#   voice.gvp       GVP1 pack of voice lines streamed from disk (dialogue, radio, announcer)
#   audio/*.gstr    music and ambience beds streamed from disk
#   audio_manifest.txt  name, kind, rate, channels, seconds, FMOD sound definition
import sys,os,re,struct,pickle,numpy as np
from fractions import Fraction
from scipy.signal import resample_poly
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)));sys.path.insert(0,os.path.join(os.path.dirname(os.path.abspath(__file__)),'..'))
import adpcm,gitspaths,pcmsrc
# databuilder: the samples come decoded from the install's banks (pcmsrc.py), not from a .npy cache
BANKSRC=pcmsrc.Banks(gitspaths.root('FMOD')) if __name__=='__main__' or os.environ.get('GITSFA_FMOD') or os.environ.get('GITSFA_WORK') else None
OUT=(sys.argv[1].rstrip('/\\')+'/') if len(sys.argv)>1 else gitspaths.dirp('AUDIOOUT')
BANKS=['sac_bank%02d'%i for i in range(10)]
CHARS='azuma|bato|borma|ishikawa|maven|motoko|paz|reiko|saito|sitara|togusa'
UI_STEREO=re.compile(r'ui_(kill|dead|respawn|ingame_start|game_start|ready_game_start|ready_count|result|logo|mvp|match_found|lobby_on|log_in_on|intergate_open)')
UI_MENU=re.compile(r'ui_(result|pose|tutorial|logo|lobby|log_in|server|room|party|levelup|mission|achievement|cha_select|character|intergate|match_found|autobalance|mvp|ready_on|mouse_over_weapon|scaned)')
UI_DROP=re.compile(r'ui_(gacha|myinfo|tckm|weapon_customize|daily_rewards|superrare|sticker|spray|result_random_box|result_test|mission_bar_loop)')
LONG={}
WEAP_DROP=re.compile(r'(_indoor|_silencer|_loop(_end)?$|_loop_end|_test$|_test_|fp_distant)')
def classify(b,n):
    """-> None or (kind, rate, ch, trunc_s, fade_s); kind ram|voice|music|amb"""
    if b==2: return ('music',44100,2,0,0)
    if b==1: return ('amb',32000,0,0,0)
    if b==6:
        if re.match(r'(android|bodyguard|terrorist|enemy)_',n) or n.startswith(('cqc_split_enemy','cqc_split_android')): return None
        return ('voice',24000,1,0,0)          # every voice line is streamed (barks included)
    if b in (0,8,9):
        if WEAP_DROP.search(n): return None
        if 'fire_fp' in n or re.search(r'fire_(outdoor|fp)?_?\d+$',n) and 'tp' not in n: return ('ram',32000,1,2.2,0.6)
        if re.search(r'fire_(tp|distance)|_distance_\d+$|fire_distance',n): return ('ram',24000,1,1.6,0.7)
        if 'oneshot_tail' in n: return ('ram',32000,2,2.0,0.8)
        if re.search(r'exp|explosion|debris',n): return ('ram',32000,1,4.5,1.2)
        return ('ram',32000,1,0,0)
    if b==3:
        if UI_DROP.search(n): return None
        if (LONG.get(n,0)>4.0 or UI_MENU.match(n)) and n not in ('ui_dead','ui_respawn','ui_kill'): return ('voice',32000,2,0,0)   # long jingles and menu sounds: streamed
        return ('ram',32000,2,0,0) if UI_STEREO.match(n) else ('ram',24000,1,0,0)
    if b in (4,5): return ('ram',32000,1,0,0)
    if b==7:
        if n.startswith(('cyborg_','foley_cyborg','cqc_cyborg')) or 'snow' in n: return None   # cyborg-only modes, snow maps
        return ('ram',24000,1,0,0)
    return None
def resample(a,fr,to,loop=False):
    if fr==to: return a.astype('f4')
    f=Fraction(to,fr).limit_denominator(2000); up,dn=f.numerator,f.denominator
    if loop:
        pad=min(len(a),4096); x=np.concatenate([a[-pad:],a,a[:pad]]).astype('f4')
        y=resample_poly(x,up,dn,axis=0); s=int(round(pad*to/fr)); n=int(round(len(a)*to/fr)); return y[s:s+n]
    return resample_poly(a.astype('f4'),up,dn,axis=0)
def process(a,fr,c):
    kind,rate,ch,trunc,fade=c
    a=a.astype('f4')
    if ch==1 and a.shape[1]==2: a=a.mean(1,keepdims=True)
    if ch==0: ch=a.shape[1]
    if kind in ('ram','voice'):
        if trunc and len(a)>trunc*fr:
            a=a[:int(trunc*fr)].copy(); nf=min(int(fade*fr),len(a)); a[-nf:]*=np.linspace(1,0,nf)[:,None]
        # trailing silence
        loud=np.nonzero(np.abs(a).max(1)>40)[0]
        if len(loud): end=min(len(a),loud[-1]+int(0.02*fr)); a=a[:end]
        else: a=a[:max(1,int(0.01*fr))]
    to=min(rate,fr) if kind!='music' else 44100
    if kind=='amb': to=min(32000,fr)
    y=resample(a,fr,to,loop=kind in ('amb','music'))
    if kind in ('ram','voice') and len(y)>64:   # 2 ms fade-in guard against a start click, 3 ms out
        y[:min(96,len(y))]*=np.linspace(0,1,min(96,len(y)))[:,None]**0.5 if y[0].any() and abs(y[0]).max()>2000 else 1
        n=min(96,len(y)); y[-n:]*=np.linspace(1,0,n)[:,None]
    return np.clip(np.round(y),-32768,32767).astype('<i2'),to
def main():
    import fev; sd=fev.sounddefs()[0]; defs={}   # (was /root/fsbdec/sounddefs.pkl, fev.py's dump of the same)
    for name,ws in sd:
        for w,b,i,ms in ws: defs.setdefault((b,i),name)
    ram=[];voice=[];streams=[];man=[];seen=set()
    os.makedirs(OUT+'audio',exist_ok=True)
    stats={}
    for b in (9,0,8,3,4,5,7,6,2,1):
        meta=BANKSRC.meta(b)
        for nm,(fr,ch0,ns) in meta.items(): LONG[nm.lower().replace(' ','_').replace('.wav','')]=ns/fr
        todo=[]
        for idx,(nm,(fr,ch0,ns)) in enumerate(meta.items()):
            n=nm.lower().replace(' ','_').replace('.wav','')
            c=classify(b,n)
            if c is None or n in seen: continue
            seen.add(n); todo.append((idx,nm,fr,n,c))
        for (idx,nm,fr,n,c),(_,a) in zip(todo,BANKSRC.iter_pcm(b,[t[1] for t in todo])):
            if len(a)==0: continue
            y,rate=process(a,fr,c); kind=c[0]
            data,nblk=adpcm.encode(y)
            rec=(n,rate,y.shape[1],len(y),nblk,data)
            (ram if kind=='ram' else voice if kind=='voice' else streams).append(rec+(kind,))
            st=stats.setdefault(kind,[0,0,0]); st[0]+=1; st[1]+=len(data); st[2]+=len(y)/rate
            man.append('%-48s %-6s %5d %d %7.2f  bank%02d  %s'%(n,kind,rate,y.shape[1],len(y)/rate,b,defs.get((b,idx),'')))
        print('bank',b,'done',{k:(v[0],round(v[1]/2**20,1),round(v[2])) for k,v in stats.items()},flush=True)
    # packs are split below MAXF bytes per file (file transfer limit); the engine loads sfx_0.., voice_0..
    MAXF=24<<20
    def split(recs):
        parts=[[]];sz=0
        for r in recs:
            if sz+len(r[5])>MAXF and parts[-1]: parts.append([]);sz=0
            parts[-1].append(r);sz+=len(r[5])
        return parts
    for f in os.listdir(OUT):
        if re.match(r'(sfx_\d+\.gsnd|voice_\d+\.gvp)$',f): os.remove(OUT+f)
    ram.sort(key=lambda r:r[0])
    for pi,part in enumerate(split(ram)):
        off=0; ents=b''; blob=[]
        for n,rate,ch,fr,nb,data,_ in part:
            ents+=n.encode()[:47].ljust(48,b'\0')+struct.pack('<IHHIII',rate,ch,0,fr,nb,off); blob.append(data); off+=len(data)
        with open(OUT+'sfx_%d.gsnd'%pi,'wb') as f: f.write(b'GSN2'+struct.pack('<III',len(part),adpcm.BF,off)); f.write(ents); f.write(b''.join(blob))
    voice.sort(key=lambda r:r[0])
    for pi,part in enumerate(split(voice)):
        hdr=16+68*len(part); off=hdr; ents=b''; blob=[]
        for n,rate,ch,fr,nb,data,_ in part:
            ents+=n.encode()[:47].ljust(48,b'\0')+struct.pack('<IHHIII',rate,ch,0,fr,nb,off); blob.append(data); off+=len(data)
        with open(OUT+'voice_%d.gvp'%pi,'wb') as f: f.write(b'GVP1'+struct.pack('<III',len(part),adpcm.BF,off-hdr)); f.write(ents); f.write(b''.join(blob))
    for n,rate,ch,fr,nb,data,kind in streams:
        with open(OUT+'audio/%s.gstr'%n,'wb') as f:
            f.write(b'GSTR'+struct.pack('<IHHIIIII',rate,ch,0,fr,nb,adpcm.BF,0,32)); f.write(data)
    open(OUT+'audio_manifest.txt','w').write('# name kind rate ch seconds bank fmod_sound_definition\n'+'\n'.join(sorted(man))+'\n')
    for k,v in stats.items(): print(k,'%d sounds %.1f MB %.0f s'%(v[0],v[1]/2**20,v[2]))
    for f in sorted(os.listdir(OUT)):
        if f.endswith(('.gsnd','.gvp')): print(f,os.path.getsize(OUT+f))
if __name__=='__main__': main()
