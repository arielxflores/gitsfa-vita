# databuilder: the decoded FMOD samples straight from the install's banks, in memory (it replaces scan.py + decode_banks.py
# and their 1.7 GB cache of .npy files; the samples are the same int16 arrays, checked sample for sample).
#   banks = Banks(<Data/audio/Fmod_Build>);  banks.meta(b) -> {name: (rate, channels, frames)} in bank order (as meta.pkl)
#   for name, pcm in banks.iter_pcm(b, names): ...   (decodes ahead on worker threads; MP3 through ffmpeg, IMA in Python)
# Decryption: the banks' public FSB5 key (fsb5.py; the same key is in vgmstream's fsb_keys.h).
import os, sys, subprocess, numpy as np
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib'))
import fsb5
FFMPEG = os.environ.get('GITSFA_FFMPEG', 'ffmpeg')
NAMES = ['sac_bank00_weapon_eng_jpn_kor.fsb', 'sac_bank01_ambience.fsb', 'sac_bank02_bgm_eng_jpn_kor.fsb', 'sac_bank03_ui_eng_jpn_kor.fsb',
         'sac_bank04_tachikoma_eng_jpn_kor.fsb', 'sac_bank05_intel_eng_jpn_kor.fsb', 'sac_bank06_voice_eng.fsb', 'sac_bank07_foley_eng_jpn_kor.fsb',
         'sac_bank08_weapon_02_eng_jpn_kor.fsb', 'sac_bank09_weapon_03_eng_jpn_kor.fsb']

def dec_mpeg(data, freq, ch, n):
    """as decode_banks.py: ffmpeg's default MP3 decoder, int16 at the bank's own rate (the reference output depends on
    ffmpeg's float decoder and its SIMD path: the builder ships one ffmpeg build)"""
    kw = {'creationflags': 0x08000000} if os.name == 'nt' else {}   # (no console window per sample on Windows)
    r = subprocess.run([FFMPEG, '-v', 'quiet', '-f', 'mp3', '-i', 'pipe:0', '-f', 's16le', '-ac', str(ch), '-ar', str(freq), '-'],
                       input=data, capture_output=True, **kw)
    if r.returncode: raise RuntimeError('ffmpeg could not decode an MP3 sample (exit %d)' % r.returncode)
    return np.frombuffer(r.stdout, '<i2').reshape(-1, ch)[:n]

class Banks:
    def __init__(s, fmod_dir, jobs=None):
        s.dir = fmod_dir; s.jobs = jobs or max(2, min(8, os.cpu_count() or 2)); s._b = None; s._k = None
    def _load(s, b):
        if s._k == b: return s._b
        p = os.path.join(s.dir, NAMES[b]); d = fsb5.load(p)
        if d[:4] != b'FSB5': d = open(p, 'rb').read()
        mode, smp = fsb5.parse(d); s._k = b; s._b = (mode, {x['name'].replace('/', '_'): x for x in smp}, smp); return s._b
    def meta(s, b):
        mode, by, smp = s._load(b)
        return {x['name'].replace('/', '_'): (x['freq'], x['ch'], x['n']) for x in smp}
    def pcm(s, b, name):
        mode, by, smp = s._load(b); x = by[name]
        return fsb5.ima_xbox(x['data'], x['ch'], x['n']) if mode == 7 else dec_mpeg(x['data'], x['freq'], x['ch'], x['n'])
    def iter_pcm(s, b, names, ahead=48):
        """(name, int16 array) for names, in order, decoded up to `ahead` samples in advance"""
        mode, by, smp = s._load(b); names = list(names)
        if mode == 7:
            for nm in names: yield nm, s.pcm(b, nm)
            return
        with ThreadPoolExecutor(s.jobs) as ex:
            fut = {}
            for i, nm in enumerate(names):
                for j in range(i, min(len(names), i + ahead)):
                    if j not in fut: x = by[names[j]]; fut[j] = ex.submit(dec_mpeg, x['data'], x['freq'], x['ch'], x['n'])
                yield nm, fut.pop(i).result()
