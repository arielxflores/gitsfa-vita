# Extra sounds the original game's events use that the main packs (build_audio.py) leave out:
# the indoor first-person fire variants, the real KRISS / MP5 / M249 / MG36 sounds (named *_test / *_loop_end in the
# banks), the C-30 distant layer, the indoor grenade blast, and the time-up sting.
# Writes data/sfx_x.gsnd (RAM) and data/voice_x.gvp (streamed) and appends them to audio_manifest.txt.
#   python3 build_extras.py [OUTDIR]
import sys,os,re,struct,pickle,numpy as np
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
import adpcm
from build_audio import process,BANKS,BANKSRC
import pcmsrc
import gitspaths
OUT=(sys.argv[1].rstrip('/\\')+'/') if len(sys.argv)>1 else gitspaths.dirp('AUDIOOUT')
GUNS='m4a1|ar_seburoc30|scar|acr|ump45|sig-mpx|l118a|870mcs|m249|mg36|m-5|mateba'
GUNS+='|k2|f2000|famas|hk417|x95-r|arx160'
GUNS+='|kac|pp2000|mp7|smg_seburoc26a'   # b90: the SMGs' indoor shots (the DEATHSTALKER EVO plays the MP7's)   # b89: the rifles added from the user's AR video (their indoor shots; the AK47's are in the main packs)
RULES=[  # regex on the sample name -> (kind, rate, ch, truncate s, fade s)
    (r'wp_(%s)_fire_fp_indoor_\d+$'%GUNS,          ('ram',32000,1,2.2,0.6)),
    (r'wp_(as50|m200|m98b|msg90|sv98|c30|sr_wa2000)_fire_(fp_)?indoor_?\d+$',('ram',32000,1,2.2,0.6)),   # b91: the sniper rifles' indoor shots (the SEBURO SR plays wp_c30_*, the CM2010 the SV98's)
    (r'wp_kriss_fire_fp_\d+_test$',                  ('ram',32000,1,2.2,0.6)),
    (r'wp_p90_fire_(distance|fp_indoor|fp_outdoor)_loop_end$', ('ram',32000,1,2.2,0.6)),   # b90: the P-90S1's shot tails
    (r'wp_mp5_fire_fp_(outdoor|indoor)_loop_end$',   ('ram',32000,1,2.2,0.6)),
    (r'wp_m249_fire_mech_loop_end$',                 ('ram',32000,1,0,0)),
    (r'wp_(m249|mg36|mp7|l86)_fire_tp_distance_loop_end$',('ram',24000,1,1.6,0.7)),   # (b92: + the L86-SEO)
    (r'wp_(44magnum|93r|p226|hg_bulstorm|hg_fn57|l86|mg_seburomg)_fire_fp_indoor_\d+$',('ram',32000,1,2.2,0.6)),   # b92: the pistols' and machine guns' indoor shots
    (r'wp_(sg_ksg|uts15|usas-12)_fire_fp_indoor_\d+$',('ram',32000,1,2.2,0.6)),   # b97: the shotguns' indoor shots
    (r'wp_seburo-a26_fire_fp_distance_loop_end$',    ('ram',24000,1,1.6,0.7)),
    (r'grenade_exp_indoor_\d+$',                     ('ram',32000,1,4.5,1.2)),
    (r'ui_result_test$',                             ('voice',32000,2,0,0)),
]
def main():
    # (b89: re-runnable: the extras of an earlier run are dropped from the manifest and made again)
    base=[l for l in open(OUT+'audio_manifest.txt') if not l.rstrip().endswith(' extra')]
    man=set(l.split()[0] for l in base if l.strip() and not l.startswith('#'))
    ram=[];voice=[];lines=[]
    for b in range(10):
        if os.environ.get('GITSFA_EXTRAS_PARTIAL') and not os.path.exists(os.path.join(BANKSRC.dir,pcmsrc.NAMES[b])): continue   # (a dev run with only some banks at hand)
        meta=BANKSRC.meta(b)
        for nm,(fr,ch0,ns) in meta.items():
            n=nm.lower().replace(' ','_').replace('.wav','')
            c=next((c for r,c in RULES if re.match(r,n)),None)
            if c is None or n in man: continue
            a=BANKSRC.pcm(b,nm)
            y,rate=process(a,fr,c); data,nblk=adpcm.encode(y)
            rec=(n,rate,y.shape[1],len(y),nblk,data)
            (ram if c[0]=='ram' else voice).append(rec)
            lines.append('%-48s %-6s %5d %d %7.2f  bank%02d  extra'%(n,c[0],rate,y.shape[1],len(y)/rate,b))
    ram.sort(); voice.sort()
    off=0; ents=b''; blob=[]
    for n,rate,ch,fr,nb,data in ram:
        ents+=n.encode()[:47].ljust(48,b'\0')+struct.pack('<IHHIII',rate,ch,0,fr,nb,off); blob.append(data); off+=len(data)
    with open(OUT+'sfx_x.gsnd','wb') as f: f.write(b'GSN2'+struct.pack('<III',len(ram),adpcm.BF,off)); f.write(ents); f.write(b''.join(blob))
    hdr=16+68*len(voice); off=hdr; ents=b''; blob=[]
    for n,rate,ch,fr,nb,data in voice:
        ents+=n.encode()[:47].ljust(48,b'\0')+struct.pack('<IHHIII',rate,ch,0,fr,nb,off); blob.append(data); off+=len(data)
    with open(OUT+'voice_x.gvp','wb') as f: f.write(b'GVP1'+struct.pack('<III',len(voice),adpcm.BF,off-hdr)); f.write(ents); f.write(b''.join(blob))
    with open(OUT+'audio_manifest.txt','w') as f: f.write(''.join(base)+('\n'.join(sorted(lines))+'\n' if lines else ''))
    print(len(ram),'ram sounds',os.path.getsize(OUT+'sfx_x.gsnd'),'bytes;',len(voice),'voice lines',os.path.getsize(OUT+'voice_x.gvp'),'bytes')
if __name__=='__main__': main()
