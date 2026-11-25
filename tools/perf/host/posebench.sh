#!/usr/bin/env bash
# pose evaluation micro-benchmark (chr.h ca_pose: a bot running, aiming and firing) on the host and as a Cortex-A9 / NEON build
# under qemu, with instructions and estimated issue cycles per function (armprof.py). usage: posebench.sh [poses]
# The cost of one pose = (count with N poses - count with 0) / N.
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="${SRC:-$H/../../../engine/src}"; T="${OUT:-/tmp/posebench_build}"; N="${1:-100}"
mkdir -p "$T"; cp "$H/../../chars/host/vitaGL.h" "$T"/
D="$H/../../../build/data/chr_anim_m.gab"
gcc -O2 -w -I"$T" -I"$E" -o "$T/pb_x86" "$H/posebench.c" -lm && "$T/pb_x86" "$D" 1000
arm-linux-gnueabihf-gcc -O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -ffast-math -static -w -I"$T" -I"$E" -o "$T/pb_arm" "$H/posebench.c" -lm
python3 "$H/armprof.py" "$T/pb_arm" "$D" 0 | sed -n 2p; python3 "$H/armprof.py" "$T/pb_arm" "$D" "$N"
