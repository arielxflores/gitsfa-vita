# b71 (the user: "could you implement all skins for characters ... if possible?"): every other outfit the PC keeps for the
# operators (model.dat model\ActorGhost\<Op>\<Op>_<skin>\: Goldblack, New, UNIFORM, ordinary, Valentine, Phoenix, camo ...)
# -> chr_<op>_<skin>.gvm (vm/make_chr.py: own meshes, or a texture-only skin on the folder whose body wears a map of the same
# name) and chr_skins.txt, the list the engine reads (bots.h chr_skins_load): "<op> <skin> <NAME>", the default (_A,
# chr_<op>.gvm) first. The names are this port's (the PC's are in its encrypted string table): the folder's word.
# Usage: python3 make_skins.py [outdir] [op,op,...]
import sys, os, subprocess
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import gitspaths
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
OPS = ['Bato', 'Motoko', 'Saito', 'Togusa', 'Ishikawa', 'Borma', 'Paz', 'Azuma', 'Kuro', 'Maven', 'Reiko', 'Sitara']
if len(sys.argv) > 2: OPS = [o for o in OPS if o.lower() in sys.argv[2].lower().split(',')]
NAMES = {'a': 'STANDARD', 'a_fire': 'FIRE', 'a_camo': 'CAMO', 'goldblack': 'GOLD BLACK', 'new': 'NEW LOOK', 'uniform': 'UNIFORM',
         'ordinary': 'CASUAL', 'ordin': 'CASUAL', 'ordinary02': 'CASUAL II', 'ordin02': 'CASUAL II', 'ordinary03': 'CASUAL III',
         'valentine': 'VALENTINE', 'phoenix': 'PHOENIX', 'sig': 'SIGNATURE'}
AG = gitspaths.path('ANIMOUT', 'model', 'ActorGhost', '')
rows = []; his = []
for op in OPS:
    opd = [d for d in os.listdir(AG) if d.lower() == op.lower()][0]
    folds = sorted(f for f in os.listdir(AG + opd) if os.path.isdir(AG + opd + '/' + f) and '_' in f)
    pre = [f for f in folds if f.lower().endswith('_a')][0][:-2]   # (the files' spelling: ReiKo -> Reiko)
    rows.append((op.lower(), 'a', NAMES['a'])); his.append((op.lower(), 'a', 'chr_%s_hi' % op.lower()))   # (the 'chr' step's make_chr writes it)
    for f in folds:
        if not f.lower().startswith(pre.lower() + '_'): continue
        sk = f[len(pre) + 1:]
        if sk.lower() == 'a': continue
        r = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'make_chr.py'), OUT, op, sk], capture_output=True, text=True)
        line = [l for l in r.stdout.splitlines() if l.startswith('chr ')]
        if r.returncode or not line: print('skin %s %s FAILED\n%s' % (op, sk, r.stderr[-1500:])); continue
        print(line[0][:160])
        rows.append((op.lower(), sk.lower(), NAMES.get(sk.lower(), sk.upper().replace('_', ' '))))
        hi = [l.split()[1] for l in r.stdout.splitlines() if l.startswith('hi ')]   # b76: its menu model (make_chr.py)
        if hi: his.append((op.lower(), sk.lower(), hi[0]))
with open(OUT + '/chr_skins.txt', 'w') as f:
    f.write('# b71: operator skins (vm/make_skins.py): <op> <skin> <NAME>; a = chr_<op>.gvm, else chr_<op>_<skin>.gvm\n')
    for r in rows: f.write('%s %s %s\n' % r)
his = [h for h in his if os.path.exists(OUT + '/' + h[2] + '.gvm')]   # (a mesh folder with no LOD heavier than the game's has none: the in-game model then)
with open(OUT + '/chr_hi.txt', 'w') as f:   # b76: the menus' high-detail operator of each skin (main.c chr_preview; mesh only)
    f.write('# b76: menu operator models (vm/make_chr.py, mesh only - the in-game model lends its textures): <op> <skin> <file stem>\n')
    for r in his: f.write('%s %s %s\n' % r)
print('skins:', len(rows) - len(OPS), 'besides the defaults ->', OUT + '/chr_skins.txt;', len(his), 'menu models listed -> chr_hi.txt')
