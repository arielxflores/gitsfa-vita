#!/usr/bin/env bash
# Builds the whole game for the host (engine/src/main.c + every header) against the shims in tools/hostgame/shim, with the
# device code paths (__vita__: jobs.h / alog.h / skin.h / tut_video.h run as on the Vita, on pthreads).
#   build.sh [asan|asan-align|tsan|msan|plain|leaks] [out binary]      env: SRC (engine/src), CC (default gcc), EXTRA (more flags)
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="${SRC:-$H/../../engine/src}"; MODE="${1:-asan}"; OUT="${2:-$H/out/gitsfa_$MODE}"
mkdir -p "$(dirname "$OUT")"
case "$MODE" in
  asan) SAN="-fsanitize=address,undefined -fsanitize-recover=all -fno-omit-frame-pointer -fno-sanitize=alignment" ;;
  asan-align) SAN="-fsanitize=address,undefined,alignment -fsanitize-recover=all -fno-omit-frame-pointer" ;;
  tsan) SAN="-fsanitize=thread -fno-omit-frame-pointer" ;;
  msan) SAN="-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer"; CC="${CC:-clang}" ;;   # (clang + its compiler-rt msan runtime; not installed here: valgrind memcheck on the plain build instead)
  plain) SAN="" ;;
  leaks) SAN="-DHG_LEAKS -fno-omit-frame-pointer -no-pie -fno-pie"; LDX="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=posix_memalign,--wrap=memalign,--wrap=aligned_alloc" ;;   # (leakdiff.py)
esac
# -O1 -g: sanitizer-friendly; the game's own warnings are not the point here (the Vita build checks those)
FL="-std=gnu11 -O1 -g -D__vita__ -DGITSFA_PAD=16 -DHG_HOST -D_GNU_SOURCE -I$H/shim -I$E -w $SAN ${EXTRA:-}"   # HG_HOST: ldprof.h times fread / fopen (wrapped below)
${CC:-gcc} $FL -c "$H/hostshim.c" -o "$OUT.shim.o" -Wall -Wextra -Wno-unused-parameter
${CC:-gcc} $FL -c "$E/main.c" -o "$OUT.main.o"
${CC:-gcc} $FL -c "$H/hostnet.c" -o "$OUT.net.o" -Wall -Wextra -Wno-unused-parameter   # net (stage 1): sockets + the PDP shim
${CC:-gcc} $FL "$OUT.main.o" "$OUT.shim.o" "$OUT.net.o" -o "$OUT" ${LDX:-} -Wl,--wrap=fread,--wrap=fopen -lm -lpthread
echo "built $OUT"
