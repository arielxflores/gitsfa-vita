# Builds the FMOD-event table for audio.h from the game's own sac.fev (FMOD Designer 4.44 project):
# event volume, 3D mode, rolloff, min/max distance, max playbacks, priority, pitch/volume randomisation, and per layer the
# sound definition (sample names in our banks), its volume, the parameter range it plays on (distance / indoor / aim)
# and its envelopes (volume and 3D pan level against distance; indoor / aim multipliers).
#   python3 make_events.py missing         -> lists samples the selected events need that are not in our packs (extras.txt)
#   python3 make_events.py gev OUT.gev     -> the table as data: events.gev (engine/src/ev_table.h reads it; databuilder)
#   python3 make_events.py c OUT.h         -> the same table as the C block audio.h used to compile in (checks)
# Inputs (tools/gitspaths.py): GITSFA_FMOD (sac.fev, sac.h), GITSFA_AUDIOOUT/audio_manifest.txt (build_audio.py +
# build_extras.py), GITSFA_SDF/Fix_<Map>/SoundData.sdf (map.dat), GITSFA_FSBDEC (missing mode only)
import sys,os,re,pickle,struct
HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE+'/fevx');sys.path.insert(0,os.path.join(HERE,'..'))
import gitspaths
SEL=[r'impact/.*',
     r'foley/female_1st/.*', r'foley/female/.*', r'foley/enemy/.*', r'foley/common/[^/]+', r'foley/dm/.*',
     r'weapon/(ar/(m4a1|seburo_c-30|scar|acr|ak47|k2|arx-160|hk417|f2000|famas|x95-r)|smg/(mp5|kriss_super_v|ump45|sig_mpx|seburo_c-26a|scorpion_evo|kac-pdw|mp5k|p90|pp2000)|sr/(l118a|xm2010|m200|m82|as50|seburo_sr|msg90|sv98|wa2000)|sg/(870mcs|ksg|usas-12|uts15)|mg/(m249|mg36|l86_lsw|mg3|seburo_mg)|hg/(m-5|mateba|p99|44magnum|93r|bul_storm|fn_5-7|sig-p226|mateba_2008m))/[^/]+',
     r'weapon/(knife|doubleblade|grenade/common|grenade/m67|grenade/m67/roll|grenade/m67/bounce|shelldrop|common)/[^/]+',
     r'ui/[^/]+', r'voice/donut/(tdm|dm|common)/[^/]+', r'voice/action/(common|kuro|maven|azuma|motoko|reiko|sitara|bato|saito|paz|borma|togusa|ishikawa)/[^/]+',
     r'bgm/[^/]+', r'ambience/object/[^/]+', r'intel_skill/[^/]+']
DROP=re.compile(r'foley/.*_(stone|metal|dirt|wood|metallight|paper)_(glass|leaf|water)$|foley/.*_metal_car_|silencer|_loop($|_)|_ng$|snow|ui/(ach_|card_|emblem_|custom|char_pose|character_(click|info|select_pop|upgrade)|click_(buy|dive|socket|unavailable|weapon|medium_0[23])|socket|window_(close_big|close_new|open_big|open_buy|open_new|open_weapon)|logo_(end|nick)|tutorial|typing|match_cancel|result_rank|event_card|level_up|pve_|dive_|gacha|tckm|spray|sticker|superrare|result_random|result_additional|daily|clan|boost|mouse_over_(?!basic|medium)|friend|party|mail|shop|buy|myinfo)|intel_skill/(disassemble|turret_ammo)')

# ---- per-map ambience: 2D beds (outdoor / indoor, crossfaded by the indoor probe), 3D emitters from the map's
# SoundData.sdf (event, position, inner/outer radius) and timed objects from WorldObjFx.txt (train, helicopter)
MAPBEDS={  # map stem: (outdoor bed event, indoor bed event)  (map codes from the texture names: hk = downtown, dm01 = pss9, tdm02 = port, tdm03 = urban)
 'downtown':('hk_outside_01','hk_inside_01'), 'berutalube':('berutalube_outside_01','berutalube_hall'),
 'ecenter':('e_center_outside','e_center_room'), 'ghost_storage':('ghost_mode','ghost_mode'),
 'headquarter':('dm01_outside_01','dm01_hallway'), 'port':('port_outside','port_hall'), 'pss9':('dm01_outside_01','dm01_hallway'),
 'ruinednight':('ttm01_outside_01','ttm01_small_room_01'), 'ruinedtokyo':('ttm01_outside_01','ttm01_small_room_01'),
 'trainstation':('train_station_outside','train_station_hallway'), 'underground':('underground_hall','underground_room'),
 'urban':('urban_outside','urban_inside'), 'zioplant':('zio_circle_tower','zio_room'), 'shooting':('shooting_range','shooting_range'),
 'tutorial':('tutorial_hall','tutorial_hallway'), 'tutorial2':('tutorial_hall','tutorial_hallway'), 'tutorial3':('tutorial_hall','tutorial_hallway'),
}
# b107: the maps' reverb definitions (sac.fev's "Reverbs", fevlib.reverb_defs): (outdoor zone, indoor zone). The project names
# them like the ambience beds above; where a bed has no definition of its own name, the map's nearest one stands in.
MAPREV={
 'downtown':('hk_outside_01','hk_inside_01'), 'berutalube':('berutalube_outside_01','berutalube_middle_room'),
 'ecenter':('e_center_outside_01','e_center_middle_room'), 'ghost_storage':('ghost_mode','ghost_mode'),
 'headquarter':('dm01_outside_01','dm01_hallway_01'), 'port':('port_outside_01','port_inside_01'), 'pss9':('dm01_outside_01','dm01_hallway_01'),
 'ruinednight':('ttm01_outside_01','ttm01_small_room_01'), 'ruinedtokyo':('ttm01_outside_01','ttm01_small_room_01'),
 'trainstation':('train_station_outside','train_station_mall'), 'underground':('underground_hall','underground_middle_room'),
 'urban':('urban_outside','urban_inside'), 'zioplant':('zio_hall','zio_room'), 'shooting':('shooting_range','shooting_range'),
 'tutorial':('tutorial_01','tutorial_01'), 'tutorial2':('tutorial_01','tutorial_01'), 'tutorial3':('tutorial_01','tutorial_01'),
}
def grv_chunk(wets):
    """events.gev's reverb chunk (engine/src/ev_table.h): per event its wet level, the definitions the maps use, per map its two"""
    import fevlib
    defs=dict(fevlib.reverb_defs()); names=[]
    for m in MAPBEDS:
        for n in MAPREV[m]:
            assert n in defs, n
            if n not in names: names.append(n)
    w=bytes(255 if x<=-59 else max(0,min(240,int(round(-x*4)))) for x in wets); w+=b'\0'*(-len(w)%4)
    K=('room','room_hf','room_lf','decay','decay_hf','refl','refl_delay','reverb','reverb_delay','diffusion','density','hf_ref','lf_ref')
    rd=b''.join(n.encode()[:27].ljust(28,b'\0')+struct.pack('<13f',*[float(defs[n][k]) for k in K]) for n in names)
    mr=b''.join(bytes([names.index(MAPREV[m][0]),names.index(MAPREV[m][1]),0,0]) for m in MAPBEDS)
    print(len(names),'reverb definitions for',len(MAPBEDS),'maps;',sum(1 for x in w[:len(wets)] if x<255),'of',len(wets),'events send to the reverb')
    return b'GRV1'+struct.pack('<3I',len(wets),len(names),len(MAPBEDS))+w+rd+mr
SDFMAP={'Fix_Downtown':'downtown','Fix_Ecenter':'ecenter','Fix_PSS9':'pss9','Fix_Port':'port','Fix_RuinedNight':'ruinednight','Fix_RuinedTokyo':'ruinedtokyo',
        'Fix_Trainstation':'trainstation','Fix_Underground':'underground','Fix_Urban':'urban','Fix_Zioplant':'zioplant'}
TIMED=[  # map, event, period s, first at s, assumed distance (the moving object's path is not known)
 ('trainstation','train_bell',25.0,0.1,3000.0), ('trainstation','train_passing_by_station',25.0,0.5,2500.0),
 ('urban','urban_helicopter_flyby',30.0,0.1,3000.0),
]
def sdf_emitters():
    out=[]
    for d,stem in SDFMAP.items():
        f=gitspaths.path('SDF',d,'SoundData.sdf')
        if not os.path.exists(f): continue
        b=open(f,'rb').read(); n=struct.unpack_from('<I',b,4)[0]
        for i in range(n):
            r=b[8+92*i:8+92*(i+1)]
            ev=r[32:64].split(b'\0')[0].decode().lower()
            rout,rin,x,y,z,on=struct.unpack_from('<fffffI',r,64)
            if on: out.append((stem,ev,x,y,z,rin,rout))
    return out
def norm(p): return p.lower().replace(' ','_')
def load():
    import fevlib   # (was the pickle ref/fev_paths.pkl = (fevlib.walk_groups()[0], fevlib.sounddefs()[0]))
    evs=fevlib.walk_groups()[0];defs=fevlib.sounddefs()[0]
    sel=[(norm(p),e) for p,e in evs if any(re.fullmatch(r,norm(p)) for r in SEL) and not DROP.search(norm(p))]
    return sel,defs
def wname(w): return w[2].split('/')[-1].lower().replace('.wav','').replace(' ','_')
def manifest():
    m={}
    for l in open(gitspaths.path('AUDIOOUT','audio_manifest.txt')):
        if l.startswith('#'): continue
        p=l.split(); m[p[0]]=(p[1],float(p[4]))
    return m
def fsb_names():
    s={}
    for b in range(10):
        for k,(fr,ch,n) in pickle.load(open(gitspaths.path('FSBDEC','pcm','sac_bank%02d'%b,'meta.pkl'),'rb')).items(): s[k.lower().replace(' ','_').replace('.wav','')]=(b,n/fr)
    return s
def env_at(pts,x):
    if x<=pts[0][0]: return pts[0][1]
    for i in range(1,len(pts)):
        if x<=pts[i][0]:
            x0,y0,_=pts[i-1]; x1,y1,_=pts[i]; t=(x-x0)/max(1e-9,x1-x0); return y0+t*(y1-y0)
    return pts[-1][1]
def pkind(name):
    n=name.lower()
    if n in ('distance','(distance)'): return 1
    if n=='sustain': return 2
    if n=='aim': return 3
    return 4
# ---- databuilder: the C block above, as data (engine/src/ev_table.h). Parsed from the C text so that every number is the
# float the compiler made of it before (decimal -> nearest float32, ties to even)
def f32(s):
    import numpy as np
    from fractions import Fraction
    q=Fraction(s.rstrip('f'));x=np.float32(float(q));best=None
    for c in (np.nextafter(x,np.float32(-np.inf)),x,np.nextafter(x,np.float32(np.inf))):
        dd=abs(Fraction(float(c))-q);ev=(int(np.float32(c).view(np.uint32))&1)==0
        if best is None or dd<best[0] or (dd==best[0] and ev): best=(dd,c)
    if q==0 and s.lstrip().startswith('-'): return struct.pack('<f',-0.0)   # '-0.00f' is -0.0f to the compiler
    return struct.pack('<f',best[1])
def gev_from_c(gen):
    tok=re.compile(r'"((?:[^"\\]|\\.)*)"|(-?[0-9.]+f?)')
    sec={};cur=None
    for ln in gen.split('\n'):
        m=re.match(r'static const (?:unsigned char|EvLayer|EvDef|AmbMap|AmbEmit|AmbTimed) (\w+)\[',ln)
        if m: cur=m.group(1);sec[cur]=[];continue
        if cur and ln.startswith('\t{'):
            row=[]
            for a,b in tok.findall(ln):
                if b: row.append(('n',b))
                else: row.append(('s',re.sub(r'\\(.)',r'\1',a)))
            sec[cur].append(row)
        elif ln.startswith('};'): cur=None
    strs=bytearray();soff={}
    def S(t):
        assert t[0]=='s'
        if t[1] not in soff: soff[t[1]]=len(strs);strs.extend(t[1].encode('latin1')+b'\0')
        return struct.pack('<I',soff[t[1]])
    I=lambda t,f: struct.pack(f,int(t[1]))
    F=lambda t: f32(t[1])
    cur_=b''.join(bytes(int(t[1]) for t in r) for r in sec['EV_CURVE']);cur_+=b'\0'*(-len(cur_)%4)
    ly=b''.join(S(r[0])+b''.join(F(t) for t in r[1:7])+bytes(int(t[1]) for t in r[7:12])+b'\0\0\0'+F(r[12]) for r in sec['EV_LAYER'])
    ev=b''.join(S(r[0])+I(r[1],'<H')+bytes([int(r[2][1]),int(r[3][1])])+b''.join(F(t) for t in r[4:11])+bytes([int(r[11][1]),int(r[12][1])])+I(r[13],'<H') for r in sec['EV_DEF'])
    am=b''.join(S(r[0])+S(r[1])+S(r[2]) for r in sec['AMB_MAP'])
    em=b''.join(S(r[0])+S(r[1])+b''.join(F(t) for t in r[2:7]) for r in sec['AMB_EMIT'])
    tm=b''.join(S(r[0])+S(r[1])+b''.join(F(t) for t in r[2:5]) for r in sec['AMB_TIMED'])
    strs.extend(b'\0'*(-len(strs)%4))
    hdr=b'GEV1'+struct.pack('<7I',len(sec['EV_CURVE']),len(sec['EV_LAYER']),len(sec['EV_DEF']),len(sec['AMB_MAP']),len(sec['AMB_EMIT']),len(sec['AMB_TIMED']),len(strs))
    assert len(ly)==40*len(sec['EV_LAYER']) and len(ev)==40*len(sec['EV_DEF'])
    return hdr+cur_+bytes(strs)+ly+ev+am+em+tm
def main():
    mode=sys.argv[1]
    sel,defs=load(); man=manifest()
    if mode=='missing':
        fsb=fsb_names(); need={}
        for p,e in sel:
            for l in e['layers']:
                for s in l['sis']:
                    for w in defs[s['sd']][2]:
                        if w[0]!='wav': continue
                        n=wname(w)
                        if n not in man and n in fsb: need.setdefault(n,[]).append(p)
        tot=0
        for n in sorted(need):
            b,sec=fsb[n]; tot+=sec
            print('%-50s bank%02d %5.2fs  %s'%(n,b,sec,need[n][0]))
        print(len(need),'samples, %.1f s'%tot)
        return
    # ---- generate
    curves=[(255,)*17]; cidx={curves[0]:0}
    def curve(vals):
        q=tuple(max(0,min(255,int(round(v*255)))) for v in vals)
        if q not in cidx: cidx[q]=len(curves); curves.append(q)
        return cidx[q]
    names={}
    for n in man: names[n]=1
    allnames=sorted(man)
    def layer_snd(sd):
        ws=[wname(w) for w in defs[sd][2] if w[0]=='wav']; nul=sum(w[1] for w in defs[sd][2] if w[0]=='null'); wt=sum(w[1] for w in defs[sd][2] if w[0]=='wav')
        ws=[w for w in ws if w in man]
        if not ws: return None,0,None
        kinds=set(man[w][0] for w in ws); kind=sorted(kinds)[0]
        pn=int(round(255*nul/(nul+wt))) if nul else 0
        if len(ws)==1: return ws[0],pn,kind
        if kind in ('music','amb'): return '|'.join(sorted(ws)),pn,kind
        # common prefix that matches exactly this set in the packs of that kind
        pre=os.path.commonprefix(ws)
        hit=[n for n in allnames if n.startswith(pre) and man[n][0]==kind]
        if pre and set(hit)==set(ws): return pre+'*',pn,kind
        return '|'.join(sorted(ws)),pn,kind
    KIND={'ram':0,'voice':1,'music':2,'amb':3}
    out_ev=[]; out_ly=[]; skipped=[]; out_wet=[]
    for p,e in sorted(sel):
        params=e['params']; dpi=next((i for i,q in enumerate(params) if pkind(q['name'])==1),-1)
        dmax=params[dpi]['max'] if dpi>=0 else 0
        layers=[]
        for l in e['layers']:
            cp=params[l['cparam']] if l['cparam']<len(params) else None
            spk=pkind(cp['name']) if cp else 0
            # envelopes: volume (0x804 index 0, 0xc, 0x24) and 3D pan level (0x404) by parameter
            vd=[1.0]*17; pd=None; gm=[[1.0,1.0],[1.0,1.0]]; pconst=None
            for en in l['envs']:
                if en['dsp']: continue
                fl=en['flags']; pi=en['t'][0]; q=params[pi] if pi<len(params) else None
                k=pkind(q['name']) if q else 4
                isvol=(fl==0x804 and en['a2']==0) or fl in (0xc,0x24)
                ispan=fl==0x404
                if not (isvol or ispan): continue
                if k==1:
                    vals=[env_at(en['pts'],i/16) for i in range(17)]
                    if isvol: vd=[a*b for a,b in zip(vd,vals)]
                    else: pd=vals
                elif isvol:
                    for ind in (0,1):
                        for aim in (0,1):
                            x=(0.75 if ind else 0.2) if k==2 else (float(aim) if k==3 else 0.0)
                            gm[ind][aim]*=env_at(en['pts'],x)
                elif ispan: pconst=env_at(en['pts'],0.0)
            for s in l['sis']:
                snd,pn,kind=layer_snd(s['sd'])
                if snd is None: skipped.append((p,defs[s['sd']][0])); continue
                s0,s1=s['start'],s['start']+s['len']
                sp=spk   # 0 none, 1 distance, 2 indoor ('sustain'), 3 aim, 4 the event's other parameter (caller's value, default 0)
                if sp==4 and s0<=0.0001 and s1>=0.999: sp=0
                g=[s['vol']*gm[i][a] for i in (0,1) for a in (0,1)]
                vc=curve(vd) if dpi>=0 and any(v<0.999 for v in vd) else 0
                pc=curve(pd)+1 if pd is not None else 0
                layers.append((snd,g,s0,s1,sp,KIND[kind]|(16 if s['lmode']==0 else 0),vc,pc,pn,pconst if pconst is not None else -1.0))
        if not layers: continue
        mode_=e['mode']; fl=0
        if mode_&0x10: fl|=1
        if mode_&0x04000000: fl|=2
        elif mode_&0x00200000: fl|=8
        elif mode_&0x00100000: fl|=4
        out_wet.append(e['rev'][1])
        out_ev.append((p,len(out_ly),len(layers),fl,e['vol'],e['vol_rand'],e['pitch'],e['pitch_rand'],e['f56'],e['f60'],dmax,e['w44'],e['w40'],e['fade'][1]))
        out_ly+=layers
    # ---- C
    def cs(s): return '"'+s.replace('\\','\\\\').replace('"','\\"')+'"'
    L=['// ---- BEGIN GENERATED by tools/audio/make_events.py from sac.fev (do not edit by hand)',
       '// %d events, %d layers, %d curves'%(len(out_ev),len(out_ly),len(curves)),
       'static const unsigned char EV_CURVE[%d][17] = {'%len(curves)]
    for c in curves: L.append('\t{'+','.join(map(str,c))+'},')
    L.append('};')
    L.append('static const EvLayer EV_LAYER[%d] = {'%len(out_ly))
    for (snd,g,s0,s1,sp,kind,vc,pc,pn,pk) in out_ly:
        L.append('\t{%s,{%.3ff,%.3ff,%.3ff,%.3ff},%.3ff,%.3ff,%d,%d,%d,%d,%d,%.2ff},'%(cs(snd),g[0],g[1],g[2],g[3],s0,s1,sp,kind,vc,pc,pn,pk))
    L.append('};')
    L.append('static const EvDef EV_DEF[%d] = {'%len(out_ev))
    for (p,l0,nl,fl,vol,vr,pit,pr,mind,maxd,dmax,mp,pri,fo) in out_ev:
        L.append('\t{%s,%d,%d,%d,%.3ff,%.3ff,%.4ff,%.4ff,%.1ff,%.1ff,%.1ff,%d,%d,%d},'%(cs(p),l0,nl,fl,vol,vr,pit,pr,mind,maxd,dmax,min(mp,255),min(pri,255),fo))
    L.append('};')
    evnames=set(p for p,*_ in out_ev)
    L.append('static const AmbMap AMB_MAP[] = {')
    for m,(o,i) in MAPBEDS.items():
        for x in (o,i): assert 'ambience/object/'+x in evnames, x
        L.append('\t{"%s","ambience/object/%s","ambience/object/%s"},'%(m,o,i))
    L.append('};')
    em=[e for e in sdf_emitters() if 'ambience/object/'+e[1] in evnames]
    L.append('static const AmbEmit AMB_EMIT[] = {')
    for (m,ev,x,y,z,rin,rout) in em: L.append('\t{"%s","ambience/object/%s",%.1ff,%.1ff,%.1ff,%.1ff,%.1ff},'%(m,ev,x,y,z,rin,rout))
    L.append('};')
    L.append('static const AmbTimed AMB_TIMED[] = {')
    for (m,ev,per,at,dist) in TIMED:
        assert 'ambience/object/'+ev in evnames, ev
        L.append('\t{"%s","ambience/object/%s",%.1ff,%.1ff,%.1ff},'%(m,ev,per,at,dist))
    L.append('};')
    print(len(em),'emitters')
    L.append('#define EV_COUNT %d'%len(out_ev))
    L.append('// ---- END GENERATED')
    gen='\n'.join(L)+'\n'
    out=sys.argv[2]
    if mode=='c': open(out,'w').write(gen)
    else: open(out,'wb').write(gev_from_c(gen)+grv_chunk(out_wet))   # (b107: + the reverb chunk)
    print(len(out_ev),'events',len(out_ly),'layers',len(curves),'curves;',len(skipped),'layers without samples')
    with open(gitspaths.tmp('skipped_layers.txt'),'w') as f:
        for p,d in skipped: f.write('%s %s\n'%(p,d))
if __name__=='__main__': main()
