#!/usr/bin/env python3
# transcribe the decoded voice samples (our own decoded PCM, /root/fsbdec/pcm/<bank>/<name>.npy) to find lines that mention
# PC controls. usage: asr.py <bank> <out.tsv> [name filter regex]
import sys, os, re, pickle, numpy as np
from faster_whisper import WhisperModel
bank, out = sys.argv[1], sys.argv[2]; flt = re.compile(sys.argv[3]) if len(sys.argv) > 3 else None
info = pickle.load(open('/root/fsbdec/info.pkl', 'rb'))
fsb = [k for k in info if k.startswith(bank)][0]; meta = {n: (r, c) for n, r, c, _ in info[fsb][1]}
done = set()
if os.path.exists(out): done = {l.split('\t')[0] for l in open(out)}
m = WhisperModel('base.en', device='cpu', compute_type='int8', cpu_threads=2)
names = sorted(f[:-4] for f in os.listdir('/root/fsbdec/pcm/' + bank) if f.endswith('.npy'))
if flt: names = [n for n in names if flt.search(n)]
f = open(out, 'a')
for i, n in enumerate(names):
    if n in done: continue
    a = np.load('/root/fsbdec/pcm/%s/%s.npy' % (bank, n)); r, c = meta.get(n, (44100, 1))
    a = a.astype(np.float32); a /= 32768.0 if np.abs(a).max() > 2 else 1.0
    if a.ndim == 2: a = a.mean(axis=1) if a.shape[1] <= 2 else a.mean(axis=0)
    if r != 16000:
        t = np.arange(0, len(a) / r, 1 / 16000.0); a = np.interp(t, np.arange(len(a)) / r, a).astype(np.float32)
    segs, _ = m.transcribe(a, language='en', beam_size=3, vad_filter=False)
    txt = ' '.join(s.text.strip() for s in segs)
    f.write('%s\t%.1f\t%s\n' % (n, len(a) / 16000.0, txt)); f.flush()
    if i % 50 == 0: print(i, len(names), n, txt[:80], flush=True)
