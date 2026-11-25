# databuilder: the per-map colour grading <stem>.glut = the game's LUT (tools/make_lut.py, map.dat *_CG_*.tga)
#   -> for 11 maps, our footage calibration (params/grade_calib.json: luma tone curve + white balance, as calib_dist.py made;
#      b80: + per-channel tone curves where a map has 'curves', as calib.py made - ecenter, from the Paz video EP19)
#   -> + the GFIT trailer (tools/gfit_cpu.py: polynomial / affine fit weighted by the map's spawn views, drawn on the CPU by
#        tools/gfit_view.py; deterministic)
# usage: grading.py CG_DIR MAPS_DIR OUT_DIR [stem ...]
import sys, os, json, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, os.path.join(HERE, '..'))
import make_lut, gfit_cpu
CAL = json.load(open(os.path.join(HERE, 'params', 'grade_calib.json')))
import mapini   # the maps' fog, read from their World.ini
def calibrate(raw, p):   # mapvis/calib_dist.py's apply(), on the LUT entries
    cx, cy = np.array(p['luma'][0]), np.array(p['luma'][1]); wb = np.array(p['wb'])
    v = np.frombuffer(raw, np.uint8).reshape(16, 256, 3).astype(np.float32) / 255
    l = v.mean(-1, keepdims=True); g = np.interp(l, cx, cy) / np.maximum(l, 1e-4)
    o = v * g * wb; m = o.max(-1, keepdims=True)
    o = np.clip(np.where(m > 1, o / np.maximum(m, 1e-6), o), 0, 1)
    o = (o * 255 + 0.5).astype(np.uint8)
    if 'curves' in p:   # b80: per-channel tone curves on top (mapvis/calib.py, fitted to located footage frames: ecenter <- the Paz video EP19)
        v = o.astype(np.float32) / 255
        o = (np.stack([np.clip(np.interp(v[..., c], p['curves'][c][0], p['curves'][c][1]), 0, 1) for c in range(3)], -1) * 255 + 0.5).astype(np.uint8)
    return o.tobytes()
def glut(cg, maps, stem):
    raw = make_lut.lut(cg, stem)
    if stem in CAL: raw = calibrate(raw, CAL[stem])
    sky = os.path.join(maps, stem + '.gsky')
    return gfit_cpu.gfit(raw, os.path.join(maps, stem + '.gmap'), os.path.join(maps, stem + '.gcol'), sky if os.path.exists(sky) else None, mapini.fog(stem))
if __name__ == '__main__':
    cg, maps, out = sys.argv[1:4]; stems = sys.argv[4:] or list(make_lut.MAP); os.makedirs(out, exist_ok=True)
    for st in stems:
        data, (n, ep, ea) = glut(cg, maps, st); open(os.path.join(out, st + '.glut'), 'wb').write(data)
        print('%-13s %s  %6d view samples  fit mean poly %.2f affine %.2f /255' % (st, 'calibrated' if st in CAL else 'game LUT  ', n, ep, ea), flush=True)
