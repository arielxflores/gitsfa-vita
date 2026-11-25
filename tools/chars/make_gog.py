# chr_gog.txt (b103; user: "in the operatives screen please include the option to toggle masks from operatives skins by pressing
# square to remove or equip them where present"): which meshes of each operator model are the skin's mask / goggles - the
# game's own head part model\ActorGhost\<Op>\<Op>_<skin>\Default_Goggle.nif (Goggle / Gogle / Goggles.nif), which vm/make_chr.py
# builds into chr_<op>[_<skin>].gvm and the menus' chr_<..>_hi.gvm as rigid meshes on the head, next to the hair.
# The models are not rebuilt for this: a head-part mesh (flag MESH_UNMASK) is matched to its NIF by its vertex count - the
# skin's own folder first, then the operator's other folders (a texture-only skin wears another folder's meshes).
# Line: "<file stem> <mesh index> [<mesh index> ...]"; a model without such a part has no line (the engine: no toggle there).
# Engine: bots.h chr_gog_load / chr_gog_apply (sets MDF_GOGGLE on those meshes), model.h md_nogog hides them.
# usage: python3 make_gog.py <data dir> [out file]      (needs GITSFA_ANIMOUT or GITSFA_WORK: tools/gitspaths.py)
import sys, os, struct
H = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(H, '..')); sys.path.insert(0, os.path.join(H, '..', '..', 'vm'))
import gitspaths
from gvmlib import static_meshes
D = sys.argv[1].rstrip('/') + '/'; OUT = sys.argv[2] if len(sys.argv) > 2 else D + 'chr_gog.txt'
AG = gitspaths.path('ANIMOUT', 'model', 'ActorGhost', '')
GOG = ('default_goggle.nif', 'goggle.nif', 'gogle.nif', 'goggles.nif'); HAIR = ('hair.nif',)
def meshes(path):
    d = open(path, 'rb').read(); nt, nb, npl, nm, nc = struct.unpack_from('<5I', d, 4); o = 24
    for i in range(nt):
        w, h, fmt, nmm = struct.unpack_from('<HHBB', d, o); o += 6
        for k in range(nmm): o += 4 + struct.unpack_from('<I', d, o)[0]
    o += 32 * nb + 52 * npl; ms = []
    for i in range(nm):
        t, bl, n, ni = struct.unpack_from('<iiII', d, o); o += 16 + n * 32 + ni * 2 + (2 if ni % 2 else 0); ms.append((bl, n))
    return ms
def parts(op):
    """every head part of the operator: folder (lower) -> {vertex count: 'gog' | 'hair'}"""
    opd = [x for x in os.listdir(AG) if x.lower() == op][0]; out = {}
    for f in sorted(os.listdir(AG + opd)):
        p = AG + opd + '/' + f + '/'
        if not os.path.isdir(p): continue
        t = {}
        for nif in os.listdir(p):
            kind = 'gog' if nif.lower() in GOG else 'hair' if nif.lower() in HAIR else None
            if kind:
                for m in static_meshes(p + nif): t.setdefault(len(m['P']), set()).add(kind)
        out[f.lower()] = t
    return out
rows = [l.split()[:2] for l in open(D + 'chr_skins.txt') if l.strip() and l[0] != '#']
hi = {tuple(l.split()[:2]): l.split()[2] for l in open(D + 'chr_hi.txt') if l.strip() and l[0] != '#'}
lines = {}; cache = {}; nskin = 0
for op, sk in rows:
    P = cache.setdefault(op, parts(op)); own = [k for k in P if k.split('_', 1)[1] == sk] if any('_' in k for k in P) else []
    order = own + [k for k in sorted(P, key=lambda k: (not k.endswith('_a'), k)) if k not in own]
    stems = ['chr_%s' % op if sk == 'a' else 'chr_%s_%s' % (op, sk)] + ([hi[(op, sk)]] if (op, sk) in hi else [])
    for st in stems:
        if st in lines or not os.path.exists(D + st + '.gvm'): continue
        idx = []
        for i, (bl, n) in enumerate(meshes(D + st + '.gvm')):
            if not bl & (1 << 18): continue   # MESH_UNMASK: the head parts
            kind = None
            for k in order:
                if n in P[k]: assert len(P[k][n]) == 1, (st, i, n, P[k][n]); kind = next(iter(P[k][n])); break
            assert kind, 'no head part of %d vertices for %s mesh %d' % (n, st, i)
            if kind == 'gog': idx.append(i)
        lines[st] = idx
    nskin += bool(lines.get(stems[0]))
with open(OUT, 'w') as f:
    f.write('# b103: the mask / goggle meshes of the operator models (tools/chars/make_gog.py): <file stem> <mesh index> ...\n')
    for st in sorted(lines):
        if lines[st]: f.write('%s %s\n' % (st, ' '.join(str(i) for i in lines[st])))
print('chr_gog.txt: %d of %d models have a mask / goggle part; %d of %d skins' % (sum(bool(v) for v in lines.values()), len(lines), nskin, len(rows)))
