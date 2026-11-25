# make_fxobj.py (b105; user: "on the urban map the helicopter sitting outside the map seems frozen ... like it's supposed
# to be animated"): the turning parts of a map's effect scene as an animated object for engine/src/wobj.h.
# A map's WorldEffectFx.wff places effect\map\<map>.nif; tools/world_fx.py bakes it into the .gmap as it stands at t = 0.
# Urban's has the police helicopter parked behind the barrier (node 'heli'): on the PC its two rotor discs turn (nodes
# 'Plangfe02' - the main rotor, about z - and 'asd' - the tail rotor, about x; one turn in 3 s, looping; hel_p.DDS shows
# six blurred blades). This writes those meshes - every mesh under a node with a looping NiTransformController below ROOT -
# as wobj_<map>_<root>.gvm (one bone a node, the loop baked at 30 fps in map space; the first and last frame are the same
# pose) and adds the object to wobj_<map>.txt: BEGIN 0, END = RESET-less loop, and a 'hide' line with the box of the still
# copies in the map (world.h w_hide_box leaves the map's blended meshes inside it out while the object is loaded).
# usage: make_fxobj.py <effect scene .nif> <texture dir> <out dir> <map stem> <root node>
import sys, os, struct, numpy as np
H = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, H); sys.path.insert(0, os.path.join(H, '..')); sys.path.insert(0, os.path.join(H, '..', '..', 'vm'))
import importlib.util
_s = importlib.util.spec_from_file_location('make_wobj', os.path.join(H, 'make_wobj.py')); mw = importlib.util.module_from_spec(_s); _s.loader.exec_module(mw)
from nifhdr import hdr
import nifmesh
from gvmlib import Builder, mat2q
def main():
    nif, tdir, out, stem, root = sys.argv[1:6]
    d = open(nif, 'rb').read(); h = hdr(d); h['d'] = d; B = h['blocks']; S = lambda i: nifmesh.S(h, i)
    par = {}; loc = {}; nm = {}; ctl = {}
    for k, (t, o, sz) in enumerate(B):
        if t in ('NiNode', 'NiMesh', 'NiBillboardNode'):
            a = nifmesh.node(d, o) if t != 'NiMesh' else nifmesh.mesh(d, o)
            L = np.eye(4); L[:3, :3] = np.array(a['r']).reshape(3, 3) * a['s']; L[:3, 3] = a['t']; loc[k] = L; nm[k] = S(a['name']) if a['name'] != 0xffffffff else ''
            for c in a.get('children', []): par[c] = k
            c = struct.unpack_from('<i', d, o + 8 + 4 * len(a['extra']))[0]
            while 0 <= c < len(B) and B[c][0].endswith('Controller'):
                co = B[c][1]; nxt, fl, fr, ph, st, sp, tgt, ip = struct.unpack_from('<iHffffii', d, co)
                if B[c][0] == 'NiTransformController' and ip >= 0 and B[ip][0] == 'NiTransformInterpolator' and ((fl >> 1) & 3) == 0 and sp > st:
                    dref = struct.unpack_from('<i', d, B[ip][1] + 32)[0]
                    if dref >= 0: ctl[k] = (mw.tdata(d, B[dref][1]), st, sp)
                c = nxt
    def below(k, r):
        while k is not None:
            if k == r: return True
            k = par.get(k)
        return False
    rk = [k for k in nm if nm[k] == root and B[k][0] == 'NiNode'][0]
    anim = sorted(k for k in ctl if below(k, rk)); assert anim, 'no looping node under ' + root
    def world_at(k, t):
        M = np.eye(4)
        while k is not None:
            L = loc[k]
            if k in ctl:
                td, st, sp = ctl[k]; tt = st + (t - st) % (sp - st); L = np.eye(4); L[:3, :3] = mw.node_rot(td['rot'], tt, loc[k][:3, :3]) * np.linalg.norm(loc[k][:3, 0]); L[:3, 3] = mw.ev(td['trans'], tt, loc[k][:3, 3])
            M = L @ M; k = par.get(k)
        return M
    def world_rest(k):
        M = np.eye(4)
        while k is not None: M = loc[k] @ M; k = par.get(k)
        return M
    bones = [nm[k] or 'node%d' % k for k in anim]; b = Builder(dict(names=bones)); b.spec = True
    for n in bones: b.bone(n)
    tmp = []; still = []
    for m in [x for x in range(len(B)) if B[x][0] == 'NiMesh']:
        A = par.get(m)
        while A is not None and A not in anim: A = par.get(A)
        if A is None: continue
        mb = mw.mesh_block(h, m)
        if mb is None or not mb['tex']: continue
        X = np.linalg.inv(world_rest(A)) @ world_rest(m)   # the mesh's own space -> its node's
        tp = [os.path.join(tdir, f) for f in os.listdir(tdir) if f.lower() == os.path.basename(mb['tex'].replace('\\', '/')).lower()]
        assert tp, 'no texture ' + mb['tex']; tp = tp[0]; bl = mb['blend']; al = float(mb['alpha'])
        if bl and 0 < al < 0.99:   # the material's alpha in a copy of the texture, as make_wobj.py
            from PIL import Image
            im = Image.open(tp).convert('RGBA'); a_ = np.asarray(im).astype(np.float64)
            if bl == 2: a_[:, :, :3] *= al
            else: a_[:, :, 3] *= al
            tq = os.path.join(out, '_fxobj_%s_a%02d.png' % (os.path.splitext(os.path.basename(tp))[0], int(round(al * 100))))
            Image.fromarray(np.clip(a_ + 0.5, 0, 255).astype(np.uint8), 'RGBA').save(tq); tp = tq; tmp.append(tq)
        b.add_rigid([mb], X, nm[A] or 'node%d' % A, np.eye(4), '', lambda t, bl=bl: bl, 256, texmap=lambda t, tp=tp: tp)
        Wm = world_rest(m); still.append((np.c_[mb['P'], np.ones(len(mb['P']))] @ Wm.T)[:, :3])
    for f in set(tmp): os.remove(f)
    T0 = max(ctl[k][2] for k in anim); nf = int(round(T0 * 30)) + 1; frames = []
    for f in range(nf):
        P = np.zeros((len(bones), 3)); W = np.zeros((len(bones), 4))
        for s, k in enumerate(anim): M = world_at(k, f / 30.0); P[s] = M[:3, 3]; W[s] = mat2q(M[:3, :3] / np.linalg.norm(M[:3, 0]))
        frames.append((P, W))
    b.bake('anim', frames, 0)
    gvm = 'wobj_%s_%s.gvm' % (stem, root.lower()); sz = b.write(os.path.join(out, gvm)); St = np.concatenate(still)
    print('%s: %d bytes, bones %s, %d frames (%.2f s), meshes %s' % (gvm, sz, bones, nf, T0, [(len(x['P']), len(x['I']) // 3) for x in b.meshes]))
    r = float(np.abs(np.concatenate([x['P'] for x in b.meshes])).max()) * 1.05
    lines = ['obj %s 0.000 %.3f 0.000 %.3f' % (gvm, T0, T0), 'box %.0f %.0f %.0f %.0f %.0f %.0f' % (-r - 1500, -r - 1500, -r - 1500, r + 1500, r + 1500, r + 1500),   # (bone 0's space: the whole machine, however the disc stands)
             'hide %.1f %.1f %.1f %.1f %.1f %.1f' % (*St.min(0), *St.max(0))]
    p = os.path.join(out, 'wobj_%s.txt' % stem); old = open(p).read().split('\n') if os.path.exists(p) else ['# wobj.h: the map\'s animated objects (tools/wobj/make_wobj.py)']
    if any(l.startswith('obj ' + gvm) for l in old):   # run again: its lines replaced
        i = [k for k, l in enumerate(old) if l.startswith('obj ' + gvm)][0]; j = i + 1
        while j < len(old) and old[j] and not old[j].startswith('obj '): j += 1
        old = old[:i] + old[j:]
    old = [l for l in old if l.strip()]; open(p, 'w').write('\n'.join(old + lines) + '\n'); print('\n'.join(lines))
if __name__ == '__main__': main()
