#!/usr/bin/env python3
"""b63: the operative screen's DETAILS window - the game's own operator videos for the Vita (sceAvPlayer, H.264 .mp4).
Sources (ui.dat ui\\GFx_DA\\Data\\rsc\\video\\skill\\*.usm, demuxed by extract.py: 'usm' = the MPEG-1 video stream,
'usmadx' = the CRI ADX audio stream):
  mv_sk_<stem>.mp4   the skill video (gameplay, no sound): ARMLAUNCHER, opticalCamo, heatSpotted, SENTRYGUN, endocoverFusion
                     (Borma's metaboost), SPRINTACC (Paz), Shield (Azuma), EMP (Kurutsu), CYBERWALL, CyberDive, DMDDrone,
                     SUMMON (b117: Togusa's Seeker Drone - the mini Tachikomas projected onto the floor; v2.0's DETAILS
                     window plays it, EP17 0:25-0:57. Left out until b117 as 'a cut skill': wrong).
  mv_op_<stem>.mp4   the operator's anime intro (<op>_ani.usm, with its sound): Batou, Borma, Ishikawa, Kurutsu, Maven,
                     Motoko, Paz, Saito, Togusa (Azuma, Reiko and Sitara have none in the game)
Framing as the tutorial clips (tools/tutorial/make_tut_clips.py): 512 x 288 (the decoder's pitch = the width, a multiple
of 64), the sources' ~16:9 scaled whole (712 x 400 .. 800 x 456: at most a few rows cropped to 16:9), their own frame rate,
H.264 Baseline, no B-frames, 1 reference frame, yuv420p, +faststart; sound: AAC LC 48 kHz stereo.
The skill videos show the 2015 build's HUD at 712 px: its key letters are 1-2 px tall at 512 px (nothing readable).
usage: make_skill_videos.py <folder with the .m1v / .adx> <out folder>
"""
import os, subprocess, sys
FFMPEG = os.environ.get('GITSFA_FFMPEG', 'ffmpeg')
X264_THREADS = os.environ.get('GITSFA_X264_THREADS', '3')
OW, OH = 512, 288
SKILL = [('bato', 'ARMLAUNCHER'), ('motoko', 'opticalCamo'), ('saito', 'heatSpotted'), ('ishikawa', 'SENTRYGUN'), ('borma', 'endocoverFusion'),
         ('paz', 'SPRINTACC'), ('azuma', 'Shield'), ('kuro', 'EMP'), ('maven', 'CYBERWALL'), ('reiko', 'CyberDive'), ('sitara', 'DMDDrone'), ('togusa', 'SUMMON')]
ANI = ['bato', 'borma', 'ishikawa', 'kuro', 'maven', 'motoko', 'paz', 'saito', 'togusa']
ANI_SRC = {'bato': 'batou_ani'}

def find(folder, name):
    for f in os.listdir(folder):
        if f.lower() == name.lower(): return os.path.join(folder, f)
    return None

def encode(src, out, audio=None, crf=25):
    """src -> out (written as out.part.mp4, renamed when complete: an interrupted run leaves no half file under the name)"""
    if os.path.exists(out) and os.path.getsize(out) > 0: return
    part = out[:-4] + '.part.mp4'
    vf = 'crop=w=min(iw\\,trunc(ih*16/9/2)*2):h=min(ih\\,trunc(iw*9/16/2)*2),scale=%d:%d:flags=lanczos,format=yuv420p' % (OW, OH)   # (the centre 16:9)
    cmd = [FFMPEG, '-v', 'error', '-y', '-i', src]
    if audio: cmd += ['-f', 'adx', '-i', audio]
    cmd += ['-map', '0:v:0'] + (['-map', '1:a:0'] if audio else []) + ['-vf', vf, '-c:v', 'libx264', '-threads', X264_THREADS, '-profile:v', 'baseline',
            '-level', '3.0', '-refs', '1', '-bf', '0', '-preset', 'slow', '-crf', str(crf), '-g', '48']
    cmd += (['-c:a', 'aac', '-b:a', '96k', '-ar', '48000', '-ac', '2'] if audio else ['-an']) + ['-movflags', '+faststart', part]
    subprocess.run(cmd, check=True); os.replace(part, out)

def main():
    src, out = sys.argv[1], sys.argv[2]; os.makedirs(out, exist_ok=True); n = 0
    for stem, name in SKILL:
        s = find(src, name + '.m1v')
        if not s: print('skill video %s: missing' % name); continue
        encode(s, os.path.join(out, 'mv_sk_%s.mp4' % stem)); n += 1
        print('mv_sk_%s.mp4 (%s) %d bytes' % (stem, name, os.path.getsize(os.path.join(out, 'mv_sk_%s.mp4' % stem))), flush=True)
    for stem in ANI:
        b = ANI_SRC.get(stem, stem + '_ani'); s = find(src, b + '.m1v'); a = find(src, b + '.adx')
        if not s: print('intro %s: missing' % b); continue
        encode(s, os.path.join(out, 'mv_op_%s.mp4' % stem), a, crf=24); n += 1
        print('mv_op_%s.mp4 (%s%s) %d bytes' % (stem, b, '' if a else ', no sound', os.path.getsize(os.path.join(out, 'mv_op_%s.mp4' % stem))), flush=True)
    print('%d videos' % n)

if __name__ == '__main__': main()
