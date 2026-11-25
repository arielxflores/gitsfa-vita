#!/usr/bin/env bash
# Think Tank path searches, synchronous (TK_NAV_ASYNC=0) vs time-sliced (1): builds tk_nav_drive.c on top of the tank host
# harness (tools/tank/host/tk_test.c, its stubs and sound names), runs N seeds x 3 TTM maps of tanks driving to the terminal
# and M scripted matches, prints the means with the difference in standard errors. usage: tk_nav_stats.sh [N=40] [M=20]
set -e
H="$(cd "$(dirname "$0")" && pwd)"; E="${SRC:-$H/../../../engine/src}"; K="$H/../../tank/host"; D="${DATA:-$H/../../../build/data/}"
T="${OUT:-/tmp/tk_nav_stats}"; mkdir -p "$T"
for h in player.h sprite.h fx.h model.h skin.h skin_gpu.h jobs.h cpuprof.h grade.h light.h nav.h hud.h hud_game.h skills.h chr.h bots.h thinktank.h ttm.h vita_prompt.h; do cp "$E/$h" "$T/"; done
cp "$K/vitaGL.h" "$K/audio.h" "$K/tk_test.c" "$H/tk_nav_drive.c" "$T/"; python3 "$K/gen_names.py" "$E/audio.h" "$D/audio_manifest.txt" "$T/names_gen.h"
for v in 0 1; do gcc -std=gnu11 -O2 -w -DTK_NAV_ASYNC=$v -DDATA="\"$D\"" -I"$T" "$T/tk_nav_drive.c" -o "$T/drive$v" -lm -lpthread; (cd "$T" && ./drive$v "${1:-40}" "${2:-20}" > "out$v.txt"); done
python3 - "$T/out0.txt" "$T/out1.txt" <<'PY'
import re, sys, statistics as st
def load(f):
    R, M = {}, []
    for l in open(f):
        if l.startswith('RUN'): p = l.split(); R.setdefault(p[1], []).append(dict(zip(p[4::2], map(float, p[5::2]))))
        m = re.search(r'walked (\d+) -> (\d+) m \(team 0\) / (\d+) -> (\d+) m \(team 1\).*, (\d+) bullets', l)
        if m: M.append(dict(zip(['t0_end_m', 't1_end_m', 'bullets'], map(float, m.groups()[1::2] + (m.group(5),)))))
    if M: R['match'] = M
    return R
def cmp(name, x, y):
    se = ((st.pvariance(x) + st.pvariance(y)) / len(x)) ** 0.5; d = st.mean(y) - st.mean(x)
    print(f"  {name:9s} sync {st.mean(x):8.2f}  async {st.mean(y):8.2f}  ({d / se if se else 0:+.1f} SE)")
a, b = load(sys.argv[1]), load(sys.argv[2])
for m in a:
    print(m)
    for k in a[m][0]: cmp(k, [r[k] for r in a[m]], [r[k] for r in b[m]])
print(open(sys.argv[2]).read().strip().splitlines()[-1])
PY
