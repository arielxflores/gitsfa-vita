#!/usr/bin/env bash
# builds bot_sim against the engine headers (with host stubs for audio.h / sprite.h): bot_sim.sh [outdir]
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="$H/../../../engine/src"; T="${1:-/tmp/bot_sim_build}"
mkdir -p "$T"; cp "$E"/player.h "$E"/model.h "$E"/skin.h "$E"/skin_gpu.h "$E"/jobs.h "$E"/cpuprof.h "$E"/nav.h "$E"/light.h "$E"/chr.h "$E"/bots.h "$E"/grade.h "$T"/; cp "$H"/stub/audio.h "$H"/stub/sprite.h "$H"/vitaGL.h "$T"/
cp "$H/bot_sim.c" "$T"/; gcc -O2 -w -I"$T" -o "$H/bot_sim" "$T/bot_sim.c" -lm -lpthread
echo built "$H/bot_sim"
