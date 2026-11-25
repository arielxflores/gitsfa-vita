#!/bin/sh
# skills.h viewer: builds view.c against the software rasterizer, renders, makes out/world.png and out/overlays.png
set -e
HERE=$(cd "$(dirname "$0")" && pwd); SRC=${1:-/root/gits/engine/src}; B=${TMPDIR:-/tmp}/skview_build
rm -rf "$B"; mkdir -p "$B/out"
for h in player.h sprite.h fx.h model.h skin.h jobs.h light.h hud.h hud_game.h grade.h skills.h; do cp "$SRC/$h" "$B/"; done
cp "$HERE/vitaGL.h" "$HERE/vitaGL_sw.h" "$HERE/view.c" "$HERE/../sktest/audio.h" "$B/"
python3 "$HERE/../sktest/gen_names.py" "$SRC/audio.h" /root/gits/build/data/audio_manifest.txt "$B/names_gen.h" > /dev/null
python3 -I "$HERE/decode_ovl.py" /root/gits/build/data/sk_ovl.gtx /tmp/sk_ovl.rgba
cc -std=gnu11 -O2 -w -I"$B" "$B/view.c" -o "$B/view" -lm
(cd "$B" && ./view)
mkdir -p "$HERE/out"
python3 - "$B/out" "$HERE/out" <<'PY'
import sys, glob, os
from PIL import Image, ImageDraw
src, dst = sys.argv[1], sys.argv[2]
def sheet(files, name, cols, scale):
    ims = [Image.open(f) for f in files]; w, h = int(ims[0].width * scale), int(ims[0].height * scale)
    S = Image.new('RGB', (cols * w, ((len(ims) + cols - 1) // cols) * (h + 16)), (20, 20, 20)); d = ImageDraw.Draw(S)
    for i, (f, im) in enumerate(zip(files, ims)):
        x, y = (i % cols) * w, (i // cols) * (h + 16); S.paste(im.resize((w, h)), (x, y + 16)); d.text((x + 4, y + 2), os.path.basename(f)[:-4], fill=(255, 255, 0))
    S.save(os.path.join(dst, name)); print(name, S.size)
sheet(sorted(glob.glob(src + '/world_*.ppm')), 'world.png', 3, 0.5)
sheet(sorted(glob.glob(src + '/ovl_*.ppm')), 'overlays.png', 4, 0.33)
PY
