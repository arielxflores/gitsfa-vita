# shared helpers of the vision sheets (make_sheets.py, camo_sheet.py)
import os, numpy as np
from PIL import Image, ImageDraw
REF = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ref')
def label(im, text, h=22):
    W = Image.new('RGB', (im.width, im.height + h), (12, 14, 18)); W.paste(im, (0, h)); ImageDraw.Draw(W).text((6, 5), text, fill=(230, 235, 240)); return W
def fit(im, H): return im.resize((max(1, round(im.width * H / im.height)), H), Image.LANCZOS)
def row(items, H=300, title=None):
    ims = [label(fit(im, H), t) for im, t in items]; W = sum(i.width for i in ims) + 8 * (len(ims) - 1); out = Image.new('RGB', (W, ims[0].height), (30, 30, 30)); x = 0
    for i in ims: out.paste(i, (x, 0)); x += i.width + 8
    return label(out, title, 26) if title else out
def stack(rows):
    W = max(r.width for r in rows); H = sum(r.height for r in rows) + 10 * (len(rows) - 1); out = Image.new('RGB', (W, H), (30, 30, 30)); y = 0
    for r in rows: out.paste(r, (0, y)); y += r.height + 10
    return out
def ref(n): return Image.open(os.path.join(REF, n)).convert('RGB')
def crop_around(im, A, H, w=360, h=300):
    p = H.VP @ np.r_[A['chest'], 1]; x = (p[0] / p[3] + 1) * 480; y = (1 - p[1] / p[3]) * 272
    x0 = int(min(max(x - w / 2, 0), 960 - w)); y0 = int(min(max(y - h / 2, 0), 544 - h)); return im.crop((x0, y0, x0 + w, y0 + h))

