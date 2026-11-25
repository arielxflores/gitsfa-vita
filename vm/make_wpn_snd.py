# b107: when each gun's handling sounds play - the game's own schedule.
# The first-person animation network (Data/anim/GIS_base_1st_runtimeBinary/GIS_base_1st.nmb) carries, per clip, discrete
# event tracks NAMED after the FMOD events they fire ('weapon/ar/common/reload_01' ... 'weapon/sg/870MCS/shot_reload'):
# the magazine out / in / the bolt of every reload, the pump after a shotgun's shot, the bolt of a bolt-action rifle, each
# at its fraction of the clip. Until b107 the engine spread reload_01..09 evenly over the clip (audio.h snd_wreload), so the
# sounds came up to 0.3 s off the hands, and a tactical reload also played the bolt of the empty one.
#   python3 make_wpn_snd.py OUTDIR      -> OUTDIR/wpn_snd.txt
# Lines:  short|clip|fraction:event fraction:event ...|seconds     (clip as weapon.h CL_NAME; seconds: RELOAD_PC below; event = the track's last path part:
# the engine plays it from the weapon's own event group, as the game maps 'common' to the gun in hand)
# Layout notes (our own reading of the file, as tools/anim/nmb.py): in the network definition every animation source has a
# block marked 00000000 ffff 1500 (+0x60 the clip's index in the library) followed by a block ... ffff 1700 that lists the
# guids of its event tracks (bundle objects of type 3); a track = u32 count, name offset at +8, events offset at +0x14,
# events = { f32 start fraction, f32 duration fraction, u32 user data }.
import sys, os, re, struct, io, contextlib
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tools', 'anim')); sys.path.insert(0, os.path.join(HERE, '..', 'tools'))

def clip_events(nmb_path):
    """{clip file name: [(fraction, track name)]} for the tracks named weapon/..."""
    from nmb import objects
    d = open(nmb_path, 'rb').read(); O = objects(d); objs = {o['guid'][:8]: o for o in O}
    files = [m.group().decode() for m in re.finditer(rb'[A-Za-z0-9_]+\.qsa', d)]
    net = [o for o in O if o['type'] == 10][0]; blob = d[net['data']:net['data'] + net['size']]
    cache = {}
    def track(g):
        if g not in cache:
            o = objs[g]; p = o['data']; n = struct.unpack_from('<I', d, p)[0]
            no = struct.unpack_from('<I', d, p + 8)[0]; eo = struct.unpack_from('<I', d, p + 0x14)[0]
            name = d[p + no:d.index(b'\0', p + no)].decode('latin1')
            cache[g] = (name, [struct.unpack_from('<ffI', d, p + eo + 12 * i)[0] for i in range(n)])
        return cache[g]
    out = {}
    for m in re.finditer(re.escape(b'\x00\x00\x00\x00\xff\xff\x15\x00'), blob):
        b = net['data'] + m.start(); aid = struct.unpack_from('<I', d, b + 0x60)[0]
        if aid >= len(files) or files[aid] in out: continue
        e = d.find(b'\x00\x00\x00\x00\xff\xff\x17\x00', b + 8, b + 0x200); nxt = d.find(b'\x00\x00\x00\x00\xff\xff\x15\x00', b + 8, b + 0x400); ev = []
        if e >= 0 and (nxt < 0 or e < nxt):
            for q in range(e + 8, e + 0x80, 4):
                g = d[q:q + 4].hex()
                if g in objs and objs[g]['type'] == 3:
                    name, fr = track(g)
                    if name.lower().startswith('weapon/'): ev += [(f, name) for f in fr]
        out[files[aid]] = sorted(ev)
    return out

CLIPS = ('reload', 'reload_empty', 'reload_start', 'reload_loop', 'reload_end', 'bolt', 'fire')
# The seconds the PC plays a reload clip in, where the user's showcase video shows it differs from the clip's own length
# (the game fits the clip to the weapon's reload time; that number is in the encrypted DB.dat, so it is MEASURED: the times
# of the reload's sounds in the video, fitted to the schedule above - tools/audio/ana/reload_fit.py; only fits that hit
# three or more of the clip's events within 12 ms on average, and differ from the clip by more than 5 %):
#   short: (tactical reload, empty reload)   0 = the clip's own length
RELOAD_PC = {
 'm4a1': (1.70, 0), 'm4a1rd': (1.70, 0), 'x95r': (1.66, 2.42), 'famas': (1.52, 2.18), 'scar': (1.82, 2.60),
 'c26a': (0, 2.20), 'evo': (0, 2.23), 'mpx': (1.68, 2.40), 'ump45': (1.58, 2.20), 'p90': (0, 2.42), 'kriss': (0, 1.92), 'pp2000': (1.22, 2.50),
 'b93se': (1.74, 0), 'seburomg': (2.50, 3.60), 'l86': (0, 3.00), 'uss12': (0, 2.60),
 'seburosr': (0, 3.80), 'cm2010': (0, 4.82), 'seo90': (2.46, 3.52), 'srv97': (2.44, 3.50), 'mm200': (3.04, 0), 'm99d': (0, 4.20), 'l118': (3.10, 4.30), 'sa50': (2.32, 3.74),
}
def rows():
    sys.argv = [sys.argv[0]]
    with contextlib.redirect_stdout(io.StringIO()):
        import make_vm_weapon as m
    EV = clip_events(m.NMB); out = []
    for sh, cfg in m.CFG.items():
        if cfg.get('cy_only') or cfg.get('hands_only'): continue
        for cl in CLIPS:
            ent = cfg['clips'].get(cl)
            if not ent or ent[0] == 'concat': continue
            ev = None
            for c in ent[1]:   # the clip the model was built from: the first candidate with a file (make_vm_weapon's order)
                fs = [f for p in m.pre(c[0], c[1]) for f in EV if f.startswith(p)]
                if fs: ev = EV[fs[0]]; break
            if ev is None or (not ev and not cl.startswith('reload')): continue
            out.append((sh, cl, [(max(0.0, min(1.0, f)), n.split('/')[-1].lower().replace(' ', '_')) for f, n in ev]))
    return out

if __name__ == '__main__':
    outdir = sys.argv[1]; R = rows(); o = os.path.join(outdir, 'wpn_snd.txt')
    with open(o, 'w', newline='\n') as f:
        f.write('# wpn_snd.txt - when each first-person clip fires its sound events (vm/make_wpn_snd.py, from the game\'s animation event tracks)\n')
        f.write('# short|clip|fraction of the clip:event (played from the weapon\'s own event group) ...|seconds the PC plays the clip in (0 = its own length)\n')
        for sh, cl, ev in R:
            secs = RELOAD_PC.get(sh, (0, 0))[('reload', 'reload_empty').index(cl)] if cl in ('reload', 'reload_empty') else 0
            f.write('%s|%s|%s|%g\n' % (sh, cl, ' '.join('%.4f:%s' % e for e in ev), secs))
    print('wpn_snd.txt: %d clips of %d weapons, %d events' % (len(R), len(set(r[0] for r in R)), sum(len(r[2]) for r in R)))
