# databuilder: our C tools. On Linux / macOS they are built here with the system C compiler (cc); on Windows the package
# carries them prebuilt (bin/win64, made by win/make_package.sh with mingw-w64 from the same sources).
#   ctools.py gchk MAPS_DIR OUT_DIR          <map>.gchk for every .gmap (tools/gchk/gchk.c + engine/src/world.h)
#   ctools.py glz c IN OUT                  pack one file (GLZ1, engine/src/glz.h)
#   ctools.py portraits DATA_DIR OUT_DIR     the Think Tank pictures for tk_hud.gtx (frozen/portraits: tk_test.c @5ffb4dd)
#   ctools.py vmvis DATA_DIR OUT_DIR         b60: which triangles of each first-person model can be seen (tools/lod/vmvis.c):
#                                            vm_<gun>.vis for every gun of DATA_DIR/weapons.txt, a few guns at a time
#   ctools.py intro INTRO_DIR OUT.mp4 [jobs] b119: the intro cut scene drawn to a video (tools/intro/render_intro.py with the
#                                            software rasteriser tools/intro/introrender.c)
#   ctools.py build-all OUT_DIR [cc]         (packaging) every tool into OUT_DIR
import os, sys, subprocess
H = os.path.dirname(os.path.abspath(__file__)); T = os.path.join(H, '..'); E = os.path.join(T, '..', 'engine', 'src')
WIN = os.name == 'nt'; EXE = '.exe' if WIN else ''
def tmpdir():
    d = os.path.join(os.environ.get('GITSFA_TMP') or os.environ.get('TMPDIR') or '.', 'ctools'); os.makedirs(d, exist_ok=True); return d
SRC = {   # name: (sources, flags)
    'gchk': ([os.path.join(T, 'gchk', 'gchk.c')], ['-O2', '-w', '-I' + os.path.join(T, 'world_host'), '-DWORLDH="%s"' % os.path.join(E, 'world.h').replace('\\', '/')]),
    'portraits': ([os.path.join(H, 'frozen', 'portraits', 'tk_test.c')], ['-std=gnu11', '-O1', '-w', '-I' + os.path.join(H, 'frozen', 'portraits')]),
    'navedges': ([os.path.join(T, 'navedges.c')], ['-O2', '-w', '-I' + E]),
    'navgen': ([os.path.join(T, 'navgen.c')], ['-O2', '-w', '-I' + E]),
    'glz': ([os.path.join(T, 'glz', 'glz.c')], ['-O2', '-w']),   # perf b56: packs the big data files (engine/src/glz.h)
    'introrender': ([os.path.join(T, 'intro', 'introrender.c')], ['-O2', '-w']),   # b119: the intro video's frames (tools/intro)
    'vmvis': ([os.path.join(T, 'lod', 'vmvis.c')], ['-O2', '-w', '-I' + os.path.join(T, 'weapons', 'sgl')]),   # b60: light first-person meshes (tools/lod)
}
def build(name, out, cc='cc', extra=()):
    srcs, fl = SRC[name]
    subprocess.run([cc] + fl + list(extra) + ['-o', out] + srcs + ['-lm'], check=True); return out
def tool(name):
    if WIN:
        p = os.path.join(H, 'bin', 'win64', name + '.exe')
        if not os.path.exists(p): raise SystemExit('missing ' + p)
        return p
    p = os.path.join(tmpdir(), name)
    srcs = SRC[name][0]
    deps = list(srcs) + ([os.path.join(E, f) for f in ('model.h', 'weapon.h', 'wpn_pose.h')] if name == 'vmvis' else [])   # (b90: vmvis compiles the engine's weapon.h in: a stale binary kept WDEF_MAX 16 and skipped every gun past the 16th row)
    if not os.path.exists(p) or any(os.path.getmtime(s) > os.path.getmtime(p) for s in deps if os.path.exists(s)):
        tmp = p + '.%d' % os.getpid(); build(name, tmp); os.replace(tmp, p)   # (steps run side by side: rename when complete)
    return p
if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'gchk':
        m, o = sys.argv[2], sys.argv[3]
        subprocess.run([tool('gchk'), m.rstrip('/\\') + '/', o.rstrip('/\\') + '/'], check=True)
    elif cmd == 'portraits':
        d, o = sys.argv[2], sys.argv[3]; os.makedirs(o, exist_ok=True)
        env = dict(os.environ); env['TK_DATA'] = d.rstrip('/\\') + '/'
        subprocess.run([tool('portraits'), o, 'portraits'], check=True, env=env, stdout=subprocess.DEVNULL)
    elif cmd == 'vmvis':
        d, o = sys.argv[2].rstrip('/\\') + '/', sys.argv[3]; os.makedirs(o, exist_ok=True); exe = tool('vmvis')
        guns = [l.split('|')[0].strip() for l in open(os.path.join(d, 'weapons.txt')) if l.strip() and not l.startswith('#')]
        guns = [g for g in guns if os.path.exists(os.path.join(d, 'vm_%s.gvm' % g))]
        n = max(1, min(4, (os.cpu_count() or 2) - 1)); run = []; todo = list(guns); bad = 0
        while todo or run:
            while todo and len(run) < n: g = todo.pop(0); run.append((g, subprocess.Popen([exe, d, o, g], stdout=subprocess.PIPE, text=True)))
            g, pr = run.pop(0); out, _ = pr.communicate(); print(out.strip()[:160], flush=True); bad += pr.returncode != 0
        if bad: raise SystemExit('vmvis failed for %d guns' % bad)
    elif cmd == 'intro':
        src, out = sys.argv[2], sys.argv[3]; jobs = sys.argv[4] if len(sys.argv) > 4 else '2'
        env = dict(os.environ); env['GITSFA_EXE_INTRORENDER'] = tool('introrender')
        sys.exit(subprocess.run([sys.executable, '-u', os.path.join(T, 'intro', 'render_intro.py'), src, out, '--jobs', jobs, '--tmp', os.path.join(tmpdir(), 'intro')], env=env).returncode)
    elif cmd == 'build-all':
        o = sys.argv[2]; cc = sys.argv[3] if len(sys.argv) > 3 else 'cc'; os.makedirs(o, exist_ok=True)
        ext = '.exe' if 'mingw' in cc else ''
        for n in SRC: build(n, os.path.join(o, n + ext), cc, ['-static'] if 'mingw' in cc else []); print('built', n)
