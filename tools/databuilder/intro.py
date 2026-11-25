# databuilder: tut_intro.mp4 from the player's OWN recording of the game's intro (optional; the original game renders the
# intro in-engine, there is no video file of it in the install). Same cut and encoding as tools/tutorial/enc_clips.sh:
# 41.2 s from the start time, 960x544 30 fps H.264 Baseline (as tut_video.h plays), AAC 48 kHz stereo, 0.4 s fade out.
# (The corner-logo removal of the original cut is left out: it was for one recorder's mark.)
# usage: intro.py VIDEO START_SECONDS OUT.mp4
import os, sys, subprocess
v, start, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
ff = os.environ.get('GITSFA_FFMPEG', 'ffmpeg')
subprocess.run([ff, '-v', 'error', '-y', '-ss', '%.3f' % start, '-i', v, '-t', '41.2',
                '-vf', 'fps=30,scale=960:544:flags=lanczos,fade=t=out:st=40.8:d=0.4,format=yuv420p',
                '-c:v', 'libx264', '-threads', os.environ.get('GITSFA_X264_THREADS', '3'), '-profile:v', 'baseline', '-level', '3.1', '-refs', '1', '-bf', '0',
                '-preset', 'medium', '-crf', '21', '-g', '60', '-af', 'afade=t=out:st=40.6:d=0.6,aresample=48000', '-c:a', 'aac', '-b:a', '128k', '-ac', '2',
                '-movflags', '+faststart', out], check=True)
print(out, os.path.getsize(out), 'bytes')
