#!/usr/bin/env bash
# builds and runs match_sim (engine headers + the bot_sim host stubs): match_sim.sh <map stem> [seconds] [jobs 0/1] [seed] [nbots]
# env: OUT (build dir), CC (default gcc; arm-linux-gnueabihf-gcc + QEMU=qemu-arm-static for an ARM/NEON build), CFLAGS
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="${SRC:-$H/../../../engine/src}"; C="$H/../../chars/host"; T="${OUT:-/tmp/match_sim_build}"
mkdir -p "$T"; cp "$E"/player.h "$E"/model.h "$E"/skin.h "$E"/skin_gpu.h "$E"/nav.h "$E"/light.h "$E"/chr.h "$E"/bots.h "$E"/grade.h "$E"/jobs.h "$E"/cpuprof.h "$T"/; cp "$C"/stub/audio.h "$C"/stub/sprite.h "$C"/vitaGL.h "$T"/
cp "$H/match_sim.c" "$T"/; ${CC:-gcc} ${CFLAGS:--O2} -w -I"$T" -o "$T/match_sim" "$T/match_sim.c" -lm -lpthread
D="$H/../../../build/data"; M="${MAPS:-/root/gits/build/maps}"
${QEMU:-} "$T/match_sim" "$D" "$M" "$@"
