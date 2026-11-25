#!/usr/bin/env bash
# render b45: build check (build.sh): every shader source this build can compile (enum.sh) has an entry in the offline
# shader bank engine/shbank.bin. Fails with the missing sources listed: run tools/shbank/gen.sh <vpk> to remake the bank.
# SHBANK_CHECK=0 skips it (a development build that will compile at run time; it says so).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd); B="$H/../../engine/shbank.bin"
[[ ${SHBANK_CHECK:-1} == 0 ]] && { echo "shader bank check SKIPPED (SHBANK_CHECK=0): this build compiles what the bank lacks at run time"; exit 0; }
T=$(mktemp -d "${TMPDIR:-/tmp}/shbchk.XXXXXX"); trap 'rm -rf "$T"' EXIT
bash "$H/enum.sh" "$T" > "$T/enum.out" || { cat "$T/enum.out"; exit 1; }
python3 -I - "$B" "$T/shbank_keys.txt" <<'PY'
import sys, struct
b, keys = sys.argv[1:3]
try: d = open(b, 'rb').read()
except OSError: d = b''
have = set()
if d[:4] == b'SHB1':
    n = struct.unpack_from('<I', d, 4)[0]
    have = {struct.unpack_from('<Q', d, 16 + 16 * i)[0] for i in range(n)}
lines = open(keys).read().splitlines(); miss = [l for l in lines if int(l.split()[0], 16) not in have]
if miss:
    print('shader bank check FAILED: %d of %d sources have no entry in %s (run tools/shbank/gen.sh <vpk>):' % (len(miss), len(lines), b))
    for l in miss[:12]: print('  ' + l)
    sys.exit(1)
print('shader bank check: all %d sources of this build are in the bank (%d entries)' % (len(lines), len(have)))
PY
