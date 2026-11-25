import summ,json,sys
def show(f):
    print('#####',f)
    for r in summ.summ('raw/effect/%s.nif'%f):
        d=r.pop('depth')
        if r['type']=='NiNode' and 'ctl' not in r: continue
        if r.get('hidden'): continue
        o={}
        o['T']=r['type'][2:6];o['n']=r['name'];o['pos']=r['pos'];o['sc']=r['scale']
        for k in('tex','blend','bb','z'):
            if k in r: o[k]=r[k]
        if 'extra' in r:
            e={a:b for a,b in r['extra'].items() if a in('SpriteXCount','SpriteYCount','SpriteSPF','UserPropBuffer')}
            if e: o['x']=e
        if 'mat' in r: o['mat']=(r['mat']['emis'],r['mat']['alpha'])
        if 'mat_ctl' in r: o['alpha_k']=[c.get('keys') for c in r['mat_ctl']]
        if 'flip' in r: o['flip']=[(c['n'],c['start'],c['stop']) for c in r['flip']]
        if 'ext_lo' in r: o['ext']=(r['ext_lo'],r['ext_hi'])
        if 'ctl' in r and r['ctl']:
            for c in r['ctl']:
                if c['t']=='NiPSEmitParticlesCtlr': o['birth']=(c['ival'],c.get('active'),c['stop'])
                elif c['t']=='NiTransformController': o['xf']=c['keys']
                elif c['t']!='NiPSForceActiveCtlr': o.setdefault('ctl',[]).append(c)
        for k in('maxp','world','quad','gen','forces','colliders'):
            if k in r: o[k]=r[k]
        if 'quad' in o and isinstance(o['quad'],dict): q=o['quad'];o['quad']=dict(fx=q['frames_x'],fy=q['frames_y'],f0=q['init_frame'],f0v=q['init_frame_var'],nf=q['nframes'],nfv=q['nframes_var'],t=(q['t0'],q['t1']),su=(q['scale_amt_u'],q['scale_lim_u'],q['scale_rest_u']),sv=(q['scale_amt_v'],q['scale_lim_v'],q['scale_rest_v']),c=(q['center_u'],q['center_v']))
        if 'forces' in o: o['forces']=[(f['type'][4:9],f.get('axis'),f.get('strength',f.get('pct')),f['active']) for f in o['forces']]
        if 'emitters' in r:
            o['em']=[]
            for e in r['emitters']:
                o['em'].append(dict(t=e['type'][4:],spd=(e['speed'],e['speed_var'],e['speed_flip']),decl=(e['decl'],e['decl_var']),plan=(e['planar'],e['planar_var']),size=(e['size'],e['size_var']),life=(e['life'],e['life_var']),rot=(e['rot'],e['rot_var'],e['rot_speed'],e['rot_speed_var'],e['rand_rot_sign']),
                    **({'mesh':e['mesh_ext'],'from':e['emit_from'],'vel':e['vel_type']} if 'mesh_ext' in e else {k:e[k] for k in('radius','dims','height') if k in e})))
        print('  '*d+json.dumps(summ.fmt(o),default=str))
for f in sys.argv[1:]: show(f)
