#!/bin/sh
# Host test of engine/src/thinktank.h + ttm.h (tk_test.c): copies the engine headers next to the GL / audio stubs, generates the
# sound-name tables, builds with AddressSanitizer + UBSan, runs, then rasterises the HUD captures over the 3D renders and makes
# the side-by-side sheets. usage: run.sh [engine src dir] [data dir] [out dir]
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=${1:-$HERE/../../../engine/src}
DATA=${2:-$HERE/../../../build/data/}
OUT=${3:-${TMPDIR:-/tmp}/tk_test}
B=${TMPDIR:-/tmp}/tk_test_build
rm -rf "$B" "$OUT"; mkdir -p "$B" "$OUT"
for h in player.h sprite.h fx.h model.h skin.h skin_gpu.h jobs.h cpuprof.h grade.h light.h nav.h hud.h hud_game.h skills.h chr.h bots.h thinktank.h ttm.h vita_prompt.h sk_shield_geo.h; do   # vita_prompt.h: ui agent (Vita button glyphs); sk_shield_geo.h (skills.h includes it): perf b38
   cp "$SRC/$h" "$B/"; done
cp "$HERE/vitaGL.h" "$HERE/audio.h" "$HERE/tk_test.c" "$B/"
python3 "$HERE/gen_names.py" "$SRC/audio.h" "$DATA/audio_manifest.txt" "$B/names_gen.h"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers -Wno-sign-compare -Wno-misleading-indentation -Wno-unused-result -Wno-format-truncation -Wno-unused-variable -Wno-unused-but-set-variable \
   -fsanitize=address,undefined -fno-sanitize-recover=undefined -DDATA="\"$DATA\"" -I"$B" "$B/tk_test.c" -o "$B/tk_test" -lm
ASAN_OPTIONS=detect_leaks=0 "$B/tk_test" "$OUT" $MODE
[ -n "$NORENDER" ] && exit 0
python3 "$HERE/compose.py" "$OUT" "$HERE/../../menu/raster.py"
