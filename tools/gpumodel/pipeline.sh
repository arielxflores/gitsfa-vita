#!/usr/bin/env bash
# The GPU cost model end to end (render b38; b39: the build label N, default 39). Heavy steps run at nice 15 (shared machine).
#   [N=39] pipeline.sh <work dir> <shader cache dir of a run of this build> "<its build string>" [<b32 boot.log>]
# The shader cache dir: ux0:data/gitsfa/shadercache/<8 hex> of an emulator run (copy it right after the run: the next
# build that runs on that emulator instance empties it). The build string: __DATE__ " " __TIME__ of that eboot's main.c
# (the strings in the decompressed eboot.bin; the folder name is FNV-64 of it folded to 32 bits, as shcache.h does).
# Steps: GXP -> USSE disassembly (gxpdsm, built from Vita3K's translator) -> per-program cycles (gxpcost.py) -> the world's
# programs and their cache keys (progsrc.c on the host GL stand-in) -> which program draws each mesh / sky layer
# (assign.c) -> fragments per program per view (fragcount.py) -> fit to the b32 device probe and predict (model.py).
# b32 / b37 reference files (prog32.json, prog37.json, cost37.json, assign/*_32|37.json, frag/*.json) are made the same
# way from those builds' world.h (git show b32:engine/src/world.h) and b37's cache; see model.py's header.
set -euo pipefail
N=${N:-39}; W=$1; CACHE=$2; BUILD=$3; BOOT=${4:-/mnt/user-data/uploads/vita-agent-use/gitsfa/results/b32-1/boot.log}
T=$(cd "$(dirname "$0")" && pwd); V3K=/root/work/emu/v3k
mkdir -p "$W/gxp$N" "$W/dsm$N" "$W/assign" "$W/frag"
cp "$CACHE"/*.bin "$W/gxp$N/"
if [[ ! -x "$W/gxpdsm" ]]; then   # thin-LTO static libs of the Vita3K build: clang-18 + lld
	B=$V3K/build; S=$V3K/src
	nice -n 15 clang++-18 -O1 -std=c++20 -flto=thin -fuse-ld=lld -DBOOST_ATOMIC_NO_LIB -DBOOST_FILESYSTEM_NO_LIB -DSPDLOG_COMPILED_LIB -DSPDLOG_FMT_EXTERNAL -DSPDLOG_NO_THREAD_ID \
		-I$B/vita3k/config -I$S/vita3k/shader/include -I$S/vita3k/features/include -I$S/vita3k/gxm/include -I$S/vita3k/util/include -I$S/external/fmt/include \
		-I$S/external/spdlog/include -I$S/vita3k/mem/include -I$S/external/glslang -I$B/include -I$S/external/SPIRV-Cross -isystem $S/external/boost "$T/gxpdsm.cpp" \
		$B/vita3k/shader/libshader.a $B/vita3k/gxm/libgxm.a $B/external/glslang/SPIRV/libSPIRV.a $B/external/glslang/glslang/libglslang.a \
		$B/external/SPIRV-Cross/libspirv-cross-glsl.a $B/external/SPIRV-Cross/libspirv-cross-core.a $B/vita3k/util/libutil.a $B/vita3k/mem/libmem.a \
		$B/vita3k/util/libutil.a $B/external/spdlog/libspdlog.a $B/external/fmt/libfmt.a $B/external/boost/lib/libboost_filesystem.a $B/external/boost/lib/libboost_atomic.a -lpthread -o "$W/gxpdsm"
fi
nice -n 15 "$W/gxpdsm" "$W/dsm$N" "$W"/gxp$N/*.bin
python3 "$T/gxpcost.py" "$W/gxp$N" "$W/dsm$N" > "$W/cost$N.json"
nice -n 15 gcc -O1 -w -I"$T/../world_host" -DWORLDH="\"$T/../../engine/src/world.h\"" "$T/progsrc.c" -o "$W/progsrc$N" -lm
"$W/progsrc$N" "$BUILD" > "$W/prog$N.json"
nice -n 15 gcc -O1 -w -I"$T/../world_host" -DWORLDH="\"$T/../../engine/src/world.h\"" "$T/assign.c" -o "$W/assign$N" -lm
python3 - "$W" "$N" <<'EOF'
import json, re, subprocess, sys
W = sys.argv[1]; t = open('/root/work/render/engine/src/main.c').read()
for m in re.finditer(r'\{\s*"(\w+)",\s*"[^"]*",\s*\{[^}]*\},\s*\{\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?,\s*([-0-9.e]+)f?\s*\}', t):
    r = subprocess.run([W + '/assign' + sys.argv[2], '/root/gits/build/maps/', m.group(1)] + list(m.groups()[1:]), capture_output=True, text=True, check=True)
    open('%s/assign/%s_%s.json' % (W, m.group(1), sys.argv[2]), 'w').write(r.stdout)
EOF
nice -n 15 python3 "$T/model.py" "$W" "$BOOT" fit
nice -n 15 python3 "$T/model.py" "$W" - table 32 37 38 $N
HEADER_BUILD=$N nice -n 15 python3 "$T/model.py" "$W" - header "$T/../../engine/src/gpumodel.h"
