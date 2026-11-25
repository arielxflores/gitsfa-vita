# Terminal Conquest data for the Vita engine (engine/src/ttm.h):
#   <stem>.gttm   per map: the terminal chain, the terminal devices, the capture areas and the spawn lists, from the map's
#                 SpawnSystem.ssf (TTM block at 0x1c4c, tools/tank/ssf_ttm.py) and area_check.nif (one mesh per terminal,
#                 under a node named like the terminal: "terminal&B")
#   tk_term.gvm   the terminal ("resource guider", model/Npc/Tachikoma/ResGuider.nif): base + device (opaque, meshes 0..1),
#                 the floor glow planes (res_base / res_3, additive, meshes 2..3) and the hologram screen (terminal_alpha /
#                 terminal_g_line, additive, meshes 4..5); the engine tints the additive meshes with the owner's colour.
# Maps with TTM data (the only ones whose SSF has the block): underground (3 terminals), berutalube (5), ruinedtokyo (5).
#
# .gttm (little endian):
#   "GTTM" u32 version=1, u32 nterm, i32 start (index of the first active terminal)
#   per terminal: char name[16] ("terminal&B"), char letter, u8 pad[3], i32 next[2] (index of the terminal that becomes
#     active when team k captures this one; -1 = this is the other team's final terminal: capturing it wins),
#     f32 dev[4] (x, y, z, yaw: the device), f32 side[2][4] (the two side points of the SSF record, purpose unknown;
#     used as defence spots), u32 nsp[2], f32 sp[2][nsp][4] (team k's spawns while this terminal is active: x, y, z, yaw
#     in the engine convention: forward = (cos yaw, sin yaw); the SSF yaw is used as it is - the lists face the terminal),
#     u32 ntri, f32 tri[ntri][9] (the capture area, world cm), f32 bmin[3], bmax[3]
# usage: python3 make_ttm.py [outdir]
import sys, os, struct, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'skills')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'vm')); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')); import gitspaths
import ssf_ttm, sklib
from PIL import Image
OUT = sys.argv[1] if len(sys.argv) > 1 else gitspaths.root('DATA')
FIX = gitspaths.dirp('DEMX', 'map', 'FixedMap')
SRC = gitspaths.dirp('ANIMOUT', 'model', 'Npc', 'Tachikoma')
TMP = gitspaths.dirp('TMP', 'ttm'); os.makedirs(TMP, exist_ok=True)
MAPS = [('underground', 'Fix_Underground'), ('berutalube', 'Fix_Berutalube'), ('ruinedtokyo', 'Fix_RuinedTokyo')]

def areas(fix):
    out = {}
    for g in sklib.static_parts(FIX + fix + '/area_check.nif', with_skinned=True):
        nm = (g['node'] or g['name'] or '').lower()
        if 'terminal' in nm and len(g['I']): out.setdefault(nm, []).append(g['P'][g['I'].reshape(-1, 3)])
    return {k: np.concatenate(v) for k, v in out.items()}

def gttm(stem, fix):
    r = ssf_ttm.load(FIX + fix + '/SpawnSystem.ssf'); A = areas(fix); T = r['term']
    ix = {t['id']: k for k, t in enumerate(T)}
    start = ix[r['start']]; log = []
    with open(os.path.join(OUT, stem + '.gttm'), 'wb') as f:
        f.write(b'GTTM' + struct.pack('<IIi', 1, len(T), start))
        for t in T:
            nm = t['name']; letter = nm[-1]
            f.write(nm.encode()[:15].ljust(16, b'\0')); f.write(struct.pack('<c3x', letter.encode()))
            nx = [ix[l] if l in ix else -1 for l in t['link']]; f.write(struct.pack('<2i', *nx))
            for p in t['pts']: f.write(struct.pack('<4f', p[0], p[1], p[2], p[5]))
            for s in (0, 1):
                f.write(struct.pack('<I', len(t['sp'][s])))
                for q in t['sp'][s]: f.write(struct.pack('<4f', q[0], q[1], q[2], q[5]))
            tri = A.get(nm.lower())
            if tri is None: tri = np.zeros((0, 3, 3)); log.append('  !! no capture area for ' + nm)
            f.write(struct.pack('<I', len(tri))); f.write(tri.astype('<f4').tobytes())
            lo = tri.reshape(-1, 3).min(0) if len(tri) else np.array(t['pts'][0][:3]) - 500; hi = tri.reshape(-1, 3).max(0) if len(tri) else np.array(t['pts'][0][:3]) + 500
            f.write(struct.pack('<6f', *lo, *hi))
            log.append('  %s next(team0 %s, team1 %s) dev (%.0f %.0f %.0f) spawns %d/%d area %d tris (%.0f %.0f %.0f)-(%.0f %.0f %.0f)' % (
                letter, T[nx[0]]['name'][-1] if nx[0] >= 0 else 'WIN', T[nx[1]]['name'][-1] if nx[1] >= 0 else 'WIN', *t['pts'][0][:3],
                len(t['sp'][0]), len(t['sp'][1]), len(tri), *lo, *hi))
    print('%s.gttm: %d terminals, start %s, %d bytes' % (stem, len(T), T[start]['name'][-1], os.path.getsize(os.path.join(OUT, stem + '.gttm'))))
    print('\n'.join(log))

def png(name, size, alpha_add=False):
    im = Image.open(SRC + name).convert('RGBA'); w, h = im.size; sc = min(1, size / max(w, h))
    im = im.resize((max(4, int(w * sc)), max(4, int(h * sc))), Image.LANCZOS)
    if alpha_add:   # additive glow: colour = alpha (the textures are white with the shape in alpha); alpha kept for the blend
        a = np.asarray(im, np.float32); a[..., :3] = a[..., 3:4]; im = Image.fromarray(a.astype(np.uint8), 'RGBA')
    p = TMP + os.path.splitext(name)[0].replace(' ', '_') + '.png'; im.save(p); return p

def terminal():
    parts = {g['node']: g for g in sklib.static_parts(SRC + 'ResGuider.nif')}
    b = sklib.Rigid()
    t02 = b.texture(png('CBB_Terminal02_D.dds', 256), 256); t01 = b.texture(png('CBB_Terminal01_D.dds', 512), 512)
    tb = b.texture(png('res_base.dds', 256, True), 256, force_alpha=True); t3 = b.texture(png('res_3.dds', 256, True), 256, force_alpha=True)
    ta = b.texture(png('terminal_alpha.dds', 128, True), 128, force_alpha=True); tl = b.texture(png('terminal_g_line.DDS', 128), 128)
    for node, tex, blend in (('cacacc', t02, 0), ('Object93', t01, 0), ('Plane14', tb, 2), ('Plane13', t3, 2), ('Plane01', ta, 2), ('Plane02', tl, 2)):
        g = parts[node]; I = g['I']
        if blend: I = np.r_[I, I.reshape(-1, 3)[:, [0, 2, 1]].ravel()]   # two-sided glow
        b.mesh(g['P'], g['N'], g['UV'], I, 'term', tex, blend)
    b.att = {'screen': (-0.1, -45.0, 120.0), 'top': (-0.1, -4.4, 250.0)}
    sz = b.write(os.path.join(OUT, 'tk_term.gvm'))
    print('tk_term.gvm %d bytes, %d meshes, %d tris' % (sz, len(b.meshes), sum(len(m['I']) // 3 for m in b.meshes)))
    try:
        import preview_gvm, preview as PV
        preview_gvm.render(PV.read(os.path.join(OUT, 'tk_term.gvm')), TMP + 'prev_term.png', 30, 15, 700, 'rest', 0, 480, 360, target=(0, 0, 110))
    except Exception as e: print('preview failed', e)

if __name__ == '__main__':
    for stem, fix in MAPS: gttm(stem, fix)
    terminal()
