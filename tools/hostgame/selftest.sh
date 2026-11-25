#!/usr/bin/env bash
# the shim's own checks must fire: each case of selftest/selftest.c makes one mistake and must be reported
set -e
H="$(cd "$(dirname "$0")" && pwd)"; O="$H/out"; mkdir -p "$O"
SAN="-fsanitize=address,undefined -fsanitize-recover=all -fno-omit-frame-pointer"
gcc -std=gnu11 -O1 -g -D__vita__ -I"$H/shim" $SAN "$H/selftest/selftest.c" "$H/hostshim.c" -o "$O/selftest" -lm -lpthread -w
want=( "" "GL-DRAW-PAST-BUFFER" "GL-UNIFORM-OVERFLOW" "heap-use-after-free" "heap-buffer-overflow" "GL-OFF-MAIN-THREAD" "stack-buffer-overflow" "GL-COMPRESSED-SIZE" "heap-use-after-free" "UPLOAD-SOURCE-CHANGED" )
fail=0
for i in 1 2 3 4 5 6 7 8 9; do
  r=$(ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 "$O/selftest" $i 2>&1 || true)
  if echo "$r" | grep -q "${want[$i]}"; then echo "case $i: ${want[$i]} reported"; else echo "case $i: ${want[$i]} NOT reported"; fail=1; fi
done
exit $fail
