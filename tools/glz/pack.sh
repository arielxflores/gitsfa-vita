#!/bin/bash
# perf b56: packs the data files the engine reads through glz.h (maps .gmap, collision .gcol, models .gvm, HUD packs
# .ghud) into GLZ1 containers. The two big maps are cut into <name>.000 / .001 as the data builder ships them
# (ecenter at 10,000,000 bytes, ruinedtokyo at 15,000,000). Other files are left out (copy them as they are).
#   pack.sh OUT_DIR FILE...      e.g. pack.sh /tmp/packed /root/gits/build/maps/*.gmap /root/gits/build/data/*.gvm
set -e
H=$(cd "$(dirname "$0")" && pwd); O=$1; shift; mkdir -p "$O"
G=${GLZ:-/tmp/claude-0/glz}; [[ -x $G && $G -nt $H/glz.c && $G -nt $H/../../engine/src/glz.h ]] || cc -O2 -o "$G" "$H/glz.c"
for f in "$@"; do
  b=$(basename "$f"); case $b in *.gmap|*.gcol|*.gvm|*.ghud|*.gfp) ;; *) continue;; esac
  "$G" c "$f" "$O/$b" ${LEVEL:-6} >/dev/null
  s=$(stat -c %s "$O/$b")
  case $b in ecenter.gmap) cut=10000000;; ruinedtokyo.gmap) cut=15000000;; *) cut=0;; esac   # as the data builder (SPLIT)
  if (( cut > 0 )); then
    python3 - "$O/$b" $cut <<'PY'
import sys; p = sys.argv[1]; d = open(p, 'rb').read(); n = int(sys.argv[2]); k = 0
for i in range(0, len(d), n): open('%s.%03d' % (p, k), 'wb').write(d[i:i + n]); k += 1
import os; os.remove(p)
PY
  fi
done
