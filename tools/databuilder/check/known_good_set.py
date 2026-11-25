# databuilder (dev container only): sets the known_good.sha256 lines of data files just rebuilt here (build/data) - the
# hashes the builder's output is checked against on the user's PC. Names only; a name not yet listed is added at the end.
#   usage: python3 known_good_set.py <file name in build/data> [...]
import hashlib, os, sys
H = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(H, '..', '..', '..')
p = os.path.join(H, '..', 'known_good.sha256'); d = {}; order = []
for l in open(p).read().split('\n'):
    if not l.strip(): continue
    h, n = l.split(None, 1); d[n.strip()] = h; order.append(n.strip())
for n in sys.argv[1:]:
    h = hashlib.sha256(open(os.path.join(ROOT, 'build', 'data', n), 'rb').read()).hexdigest()
    if n not in d: order.append(n)
    print(n, 'changed' if d.get(n) != h else 'same'); d[n] = h
open(p, 'w').write('\n'.join('%s  %s' % (d[n], n) for n in order) + '\n')
