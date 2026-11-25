#!/usr/bin/env python3
"""tut_logos.mp4: the start-up logos, rendered from the game's own Intro.swf (ui.dat ui\\GFx_DA\\Intro.swf).

The original plays Intro.swf's IntroMovie: step1MC (KODANSHA | Production I.G), step2MC (neople | NEXON), step3MC
(Geeky Studio | Team Waffle), step4MC (a 3-frame pause), then step5MC: the Ghost-emblem "dive" (grey flash, rings,
emblem) that turns into the FIRST ASSAULT title (the front end's FE_TITLE takes over from there). 30 fps, stage 1600x900.
The UI sounds the script asks for (onPlayUISound) are played by the engine at the same times (tutorial.h tut_play_logos):
  logo_company step1 frame 51, logo_team step2 frame 52, logo_dive step3 frame 50.

usage: make_tut_logos.py <Intro.swf> <work dir> <out.mp4> [jobs]
  The frames are drawn by our own SWF renderer (logos_render.py; databuilder: it replaces JPEXS ffdec's sprite export and
  the Java runtime it needed), on `jobs` processes (default: the CPU count, up to 8), then encoded with ffmpeg + libx264 as
  every Vita movie here (tut_video.h): H.264 Baseline, no B-frames, 1 reference frame, 960x544 yuv420p.
  The work dir is not used any more (kept in the usage for the builder's step list).
"""
import os, subprocess, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logos_render

FFMPEG = os.environ.get('GITSFA_FFMPEG', 'ffmpeg')
X264_THREADS = os.environ.get('GITSFA_X264_THREADS', '3')   # libx264's output depends on its thread count (3 = the reference)
W, H = logos_render.W, logos_render.H

_logos = None
def _init(swf):
    global _logos; _logos = logos_render.Logos(swf)
def _frame(i): return _logos.frame(i).tobytes()


def main():
    swf, work, out = sys.argv[1], sys.argv[2], sys.argv[3]
    jobs = int(sys.argv[4]) if len(sys.argv) > 4 else max(1, min(8, os.cpu_count() or 1))
    n = logos_render.count(swf)
    enc = subprocess.Popen([FFMPEG, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (W, H), '-r', '30', '-i', '-',
                            '-vf', 'scale=960:544:flags=lanczos,format=yuv420p', '-c:v', 'libx264', '-threads', X264_THREADS, '-profile:v', 'baseline', '-level', '3.1',
                            '-refs', '1', '-bf', '0', '-preset', 'slow', '-crf', '18', '-g', '60', '-an', '-movflags', '+faststart', out], stdin=subprocess.PIPE)
    with Pool(jobs, _init, (swf,)) as pool:
        for k, raw in enumerate(pool.imap(_frame, range(n))):   # in order
            enc.stdin.write(raw)
            if k % 25 == 24: print('logos: %d of %d frames' % (k + 1, n), flush=True)
    enc.stdin.close(); enc.wait()
    if enc.returncode: raise SystemExit('ffmpeg failed (exit %d)' % enc.returncode)
    print('%s: %d frames (%.1f s)' % (out, n, n / 30))


if __name__ == '__main__':
    main()
