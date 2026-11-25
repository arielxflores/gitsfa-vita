# chr_anim_*.gab summary: clips (frames, length, flags, speed, events), sets (slot -> clip), sizes.
# usage: python3 gabinfo.py bank.gab [set] [slotprefix]
import sys, struct
def load(path):
    d = open(path, 'rb').read(); assert d[:4] == b'GAB1'
    ver, nb, nclip, nset, nslot = struct.unpack_from('<5I', d, 4); o = 24
    bones = []
    for i in range(nb): bones.append((d[o:o + 32].split(b'\0')[0].decode(), struct.unpack_from('<i', d, o + 32)[0])); o += 36
    slots = [d[o + 16 * i:o + 16 * i + 16].split(b'\0')[0].decode() for i in range(nslot)]; o += 16 * nslot
    rec = (16 + 2 * nslot + 3) & ~3; sets = {}
    for s in range(nset): sets[d[o:o + 16].split(b'\0')[0].decode()] = struct.unpack_from('<%dh' % nslot, d, o + 16); o += rec
    hsz = 68 + 8 * nb; clips = []
    for c in range(nclip):
        nm = d[o:o + 32].split(b'\0')[0].decode(); nf, fl = struct.unpack_from('<HH', d, o + 32); fps, sp, ph, dx, dy = struct.unpack_from('<5f', d, o + 36)
        off, ln, nq = struct.unpack_from('<3I', d, o + 56); clips.append(dict(name=nm, nf=nf, flags=fl, fps=fps, speed=sp, phase=ph, dir=(dx, dy), off=off, len=ln, ev=[])); o += hsz
    data = o; last = max(((c['off'] + c['len'] + 3) & ~3) for c in clips); q = data + last
    nev, = struct.unpack_from('<I', d, q); q += 4; er = q; q += 12 * nev; ns, = struct.unpack_from('<I', d, q); strs = d[q + 4:q + 4 + ns]
    for i in range(nev):
        t, ci, kind, _, so, _ = struct.unpack_from('<fHBBHH', d, er + 12 * i); clips[ci]['ev'].append((t, kind, strs[so:].split(b'\0')[0].decode()))
    return dict(size=len(d), bones=bones, slots=slots, sets=sets, clips=clips, data=last)
if __name__ == '__main__':
    G = load(sys.argv[1]); st = sys.argv[2] if len(sys.argv) > 2 else None; pre = sys.argv[3] if len(sys.argv) > 3 else ''
    print('%s: %d bytes, %d bones, %d clips (%d bytes of tracks), %d sets, %d slots' % (sys.argv[1], G['size'], len(G['bones']), len(G['clips']), G['data'], len(G['sets']), len(G['slots'])))
    if st:
        tab = G['sets'][st]
        for s, c in zip(G['slots'], tab):
            if not s.startswith(pre) or c < 0: continue
            C = G['clips'][c]; L = (C['nf'] - 1) / C['fps']
            print('  %-11s %3d %-22s %3d f %5.2f s fl %d sp %4.0f ph %.2f  %s' % (s, c, C['name'], C['nf'], L, C['flags'], C['speed'], C['phase'], ' '.join('%.2f:%d%s' % (t, k, (':' + n) if n else '') for t, k, n in C['ev'][:6])))
    else:
        print(' sets:', ' '.join(G['sets'].keys()))
