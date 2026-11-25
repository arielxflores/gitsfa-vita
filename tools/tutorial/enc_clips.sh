#!/bin/bash
# Tutorial movies for the Vita (all H.264 Baseline, no B-frames, 1 reference frame, yuv420p, width a multiple of 64 and
# height of 16: tut_video.h maps the decoder's NV12 frames straight onto a linear texture of that size).
#   tut_v1..6.mp4  the skill explanation clips (640x272, no audio):  make_tut_clips.py <usm .m1v folder> <out> [N ...]
#   tut_logos.mp4  the start-up logos from Intro.swf (960x544):     make_tut_logos.py <Intro.swf> <work> <out.mp4>
#   tut_intro.mp4  the intro cutscene (960x544, AAC 48 kHz stereo): this script
# usage: enc_clips.sh "<Ghost in The Shell First Assault - Tutorial Stuff.mp4>" <out folder>
# (the footage's 23.7..64.9 s; delogo hides the recorder's corner mark; 0.4 s fade out)
TS=$1; OUT=${2:-.}
ffmpeg -v error -y -ss 23.7 -i "$TS" -t 41.2 \
  -vf "delogo=x=1:y=4:w=42:h=14,fps=30,scale=960:544:flags=lanczos,fade=t=out:st=40.8:d=0.4,format=yuv420p" \
  -c:v libx264 -profile:v baseline -level 3.1 -refs 1 -bf 0 -preset medium -crf 21 -g 60 \
  -af "afade=t=out:st=40.6:d=0.6,aresample=48000" -c:a aac -b:a 128k -ac 2 -movflags +faststart "$OUT/tut_intro.mp4"
ls -la "$OUT/tut_intro.mp4"
