#!/usr/bin/env bash
# The whole game on the host under a sanitizer: builds (build.sh), makes a fresh output folder with the autoplay switch,
# runs the test tour headless (menus, all 17 maps with Elimination on Urban, Demolition on PSS9 / Downtown, Terminal
# Conquest with Think Tanks on Underground, the shooting range, the training programs and their movies, the end screens)
# and prints a summary of the findings.
#   run.sh [asan|asan-align|tsan|plain] [out folder] [tour step to start at (tourstart.txt, 1..16)] [max frames]
#   ASan full tour: ~8 min on 2 cores; TSan: ~15 min per 10000 frames (the menu tour alone is 3600 frames)
# Findings: AddressSanitizer / UBSan / ThreadSanitizer reports and "HOSTGL" lines (hostshim.c) in <out>/run.err; the
# game's own log in <out>/boot.log.
set -e
H="$(cd "$(dirname "$0")" && pwd)"; MODE="${1:-asan}"; O="${2:-${TMPDIR:-/tmp}/hostgame_$MODE}"; START="${3:-}"; MAXF="${4:-400000}"
nice -n "${HG_NICE:-15}" bash "$H/build.sh" "$MODE"   # (long jobs at nice 15: the emulators on this machine come first)
rm -rf "$O"; mkdir -p "$O"; touch "$O/autoplay"; [ -n "$START" ] && echo "$START" > "$O/tourstart.txt"; [ -n "${TOURCHR:-}" ] && echo "$TOURCHR" > "$O/tourchr.txt"   # (b76: TOURCHR=0 Batou on every map)
for s_ in ${HG_SWITCHES:-}; do touch "$O/$s_"; done   # (b80: test switch files, e.g. HG_SWITCHES="skinview decapdemo")
cd "$O"
export HG_OUT="$O" HG_MAX_FRAMES="$MAXF"
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=0:detect_stack_use_after_return=1}" UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0 report_signal_unsafe=0 history_size=4}"
set +e; nice -n "${HG_NICE:-15}" "$H/out/gitsfa_$MODE" > "$O/run.out" 2> "$O/run.err"; rc=$?; set -e
echo "exit $rc; $(grep -c 'ERROR: AddressSanitizer' "$O/run.err") ASan errors, $(grep -c 'runtime error' "$O/run.err") UBSan, $(grep -c 'WARNING: ThreadSanitizer' "$O/run.err") TSan, $(grep -c '^HOSTGL [A-Z]' "$O/run.err") HOSTGL"
grep -E "^HOSTGL summary|tour finished" "$O/run.err" "$O/boot.log" || true
