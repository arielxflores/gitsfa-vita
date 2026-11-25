#!/bin/bash
# legibility check of the tutorial captures (tools/menu/check_legibility.py), with tut_ui.gtx's atlas (also 512x512 RGBA,
# so the checker would take its rectangles and button glyphs for text) taken out of each capture's texture list
D=${1:-/tmp/claude-0/tut/cap}; R=$(cd "$(dirname "$0")/../../.." && pwd); T=$(mktemp -d)
python3 - "$D" "$T" "$R/build/data/tut_ui.gtx" <<'PY'
import sys, os, glob, shutil
D, T, gtx = sys.argv[1:4]; atlas = open(gtx, 'rb').read()[16:16 + 4096]
for cap in glob.glob(os.path.join(D, '*.cap')):
    nm = os.path.basename(cap)[:-4]; shutil.copy(cap, T); os.makedirs(os.path.join(T, nm + '.tex'), exist_ok=True)
    for p in glob.glob(os.path.join(D, nm + '.tex', 'tex*.bin')):
        if open(p, 'rb').read()[16:16 + 4096] != atlas: shutil.copy(p, os.path.join(T, nm + '.tex'))
PY
python3 $R/tools/menu/check_legibility.py $T; rm -rf $T
