import pickle,sys,re
allev,defs=pickle.load(open('/root/gits/tools/audio/ref/fev.pkl','rb'))
def show(nm):
    e=allev[nm]
    print('==',nm,'vol %.3f'%e['vol'],'mode',hex(e['mode']),'min',e['f56'],'max',e['f60'],'mp',e['w44'],'w40',e['w40'],'prand %.3f vrand %.3f'%(e['pitch_rand'],e['vol_rand']),'fade',e['fade'],'cat',e['cat'],'props',e['props'])
    print('   params',[(p['name'],p['min'],p['max'],p['flags'],p['nenv'],p['sustain']) for p in e['params']])
    for li,l in enumerate(e['layers']):
        print('   layer',li,'cparam',l['cparam'],'flags',l['flags'],'prio',l['prio'])
        for s in l['sis']:
            d=defs[s['sd']]; ws=[w[2].split('/')[-1].lower().replace('.wav','') for w in d[2] if w[0]=='wav']; nn=sum(1 for w in d[2] if w[0]=='null')
            print('      si %-45s [%.3f..%.3f] vol %.2f lmode %d smode %d  waves %d null %d e.g. %s'%(d[0],s['start'],s['start']+s['len'],s['vol'],s['lmode'],s['smode'],len(ws),nn,ws[:2]))
        for en in l['envs']: print('      env %s %s a2=%d a0=%d t=%s'%(en['dsp'] or '-',hex(en['flags']),en['a2'],en['a0'],en['t']),[(round(p[0],3),round(p[1],3)) for p in en['pts']])
for pat in sys.argv[1:]:
    for nm in allev:
        if re.fullmatch(pat,nm): show(nm)
