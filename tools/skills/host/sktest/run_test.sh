#!/bin/sh
# Host test of engine/src/skills.h: copies the current engine headers next to the GL / audio stubs, regenerates the audio
# name tables, builds with AddressSanitizer + UBSan and runs. Usage: run_test.sh [engine src dir] [data dir]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-/root/gits/engine/src}
DATA=${2:-/root/gits/build/data/}
B=${TMPDIR:-/tmp}/sktest_build
rm -rf "$B"; mkdir -p "$B"
for h in "$SRC"/*.h; do case "$(basename "$h")" in audio.h|vitaGL.h) ;; *) cp "$h" "$B/";; esac; done   # (b83: every engine header - the list kept missing new ones)
cp "$HERE/vitaGL.h" "$HERE/audio.h" "$HERE/test.c" "$B/"
python3 "$HERE/gen_names.py" "$SRC/audio.h" "$DATA/audio_manifest.txt" "$B/names_gen.h"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-misleading-indentation -Wno-unused-result -Wno-format-truncation \
   -fsanitize=address,undefined -fno-sanitize-recover=undefined -DDATA="\"$DATA\"" -I"$B" "$B/test.c" -o "$B/sktest" -lm
ASAN_OPTIONS=detect_leaks=0 "$B/sktest"
