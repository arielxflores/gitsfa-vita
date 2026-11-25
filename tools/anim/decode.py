"""Decode one clip of an animation set with the channel map the original network uses for it (nmb.py), into rig-space
bone transforms (P: positions cm, W: world quaternions x,y,z,w) for every rig bone. Rig space = character space of the
engine: z up, centimetres, the character faces -y."""
import sys, numpy as np
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
from anim import hdr, decode_clip, pose, pose_world
from rig import fk

def decode(N, adir, rec, aset):
    rig = N.rigs[aset]; m = N.mapof(rec)
    h = hdr(adir + rec['file']); C = decode_clip(h); C['map'] = m['list']
    nf = C['nf']
    f0 = int(round(rec['start'] * (nf - 1))); f1 = int(round(rec['end'] * (nf - 1)))
    if f1 <= f0: f1 = min(nf - 1, f0 + 1) if rec['end'] > rec['start'] else f0
    out = []
    for i in range(f0, f1 + 1):
        if C['mode'] == 1: lp, lq = pose(rig, C, i); P, W = fk(rig['parent'], lp, lq)
        else: P, W = pose_world(rig, C, i)
        out.append((np.array(P, float), np.array(W, float)))
    return out, rig, h
