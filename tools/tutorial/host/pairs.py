#!/usr/bin/env python3
"""footage | our tutorial UI side by side.
usage: pairs.py bg <footage dir> <bg out dir>      -> blurred, darkened footage frames named after the host-test screens
       pairs.py sheet <footage dir> <png dir> <out dir>  -> <name>_pair.jpg (left: footage frame, right: our render over it)
Run run.sh with BG=<bg out dir> between the two steps so the renders sit on the (blurred) scene they belong to."""
import os, sys
from PIL import Image, ImageFilter, ImageEnhance, ImageDraw
PAIRS = {   # host-test screen -> footage frame (ts_: "Tutorial Stuff" video, ea_: Early Access intro tutorial; the number is seconds)
    'c_basic': 'ts_100', 'c_crouch': 'ts_112', 's_loading': 'ts_168', 's_hip': 'ea_236', 'l_control': 'ea_60', 'x_connect': 'ea_90',
    'k_explain': 'ea_408', 't_explain': 'ea_558', 't_hack': 'ea_564', 't_destroy': 'ea_570', 't_failed': 'ea_600'}
def load(p): return Image.open(p).convert('RGB').resize((960, 540), Image.LANCZOS)
def main():
    mode = sys.argv[1]
    if mode == 'bg':
        src, out = sys.argv[2], sys.argv[3]; os.makedirs(out, exist_ok=True)
        for n, f in PAIRS.items():
            p = os.path.join(src, f + '.jpg')
            if not os.path.exists(p): continue
            im = load(p).filter(ImageFilter.GaussianBlur(6)); im = ImageEnhance.Brightness(im).enhance(0.6)
            cv = Image.new('RGB', (960, 544)); cv.paste(im, (0, 2)); cv.save(os.path.join(out, n + '.jpg'), quality=92)
    else:
        src, png, out = sys.argv[2], sys.argv[3], sys.argv[4]; os.makedirs(out, exist_ok=True)
        for n, f in PAIRS.items():
            p, q = os.path.join(src, f + '.jpg'), os.path.join(png, n + '.png')
            if not (os.path.exists(p) and os.path.exists(q)): continue
            cv = Image.new('RGB', (1940, 580), (24, 24, 28)); cv.paste(load(p), (0, 36)); cv.paste(Image.open(q).convert('RGB'), (980, 34))
            d = ImageDraw.Draw(cv); d.text((8, 8), 'footage %s (720p, scaled to 960 wide)' % f, fill=(230, 230, 230)); d.text((988, 8), 'Vita 960x544: %s' % n, fill=(230, 230, 230))
            cv.save(os.path.join(out, n + '_pair.jpg'), quality=88)
if __name__ == '__main__': main()
