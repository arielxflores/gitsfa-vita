#!/usr/bin/env python3
"""tut_v1..6.mp4: the skill explanation clips (ui.dat rsc\\video\\tutorial\\tutorial_1..6.usm, demuxed to MPEG-1 .m1v by
the usm folder's demux.py on the PC) for the Vita.

- Framing (b59, user: the clips looked stretched wide; make them closer to the Vita's shape): 16:9 at 512x288 (width a
  multiple of 64, height of 16: the decoder's frame pitch is the width), the explanation panel 16:9 too. The 16:9 sources
  are scaled as they are; the 1064x456 ones (3, 6) lose an even strip at each side (810x456 kept, then scaled). Before b59
  every clip was squashed to 640x272, the original panel's 2.33 shape.
- Vita prompts rule: the clips show the PC build's key badges; each is replaced by the Vita control, drawn in the
  badge's own colours (measured per frame, so fades and the white -> red "active" change stay as they were):
    tutorial_1  "TIER1 > [E]"  -> D-pad right            "TIER2 > [Q]" -> D-pad right + HOLD   (skill: tap / hold)
    tutorial_4  "[Q]" (SkillSync: share, Tier 2)          -> D-pad right + HOLD
    tutorial_5  "[F]" (SkillSync: download)               -> Square
    tutorial_6  "[E] HACKING THINK TANK" key cap          -> Square
  (tutorial_2 / 3 show no keys.)
  The key cap (tutorial_6) fades in and out over the moving scene: its letter is located once in a frame where it is
  fully shown, inpainted away in every frame of the window, and the Square drawn with the letter's opacity in that frame
  (how much brighter the letter pixels are than the inpainted background, relative to the reference frame).
- Encoding: H.264 Baseline (no B-frames, 1 reference frame), yuv420p, no audio, +faststart.

usage: make_tut_clips.py <folder with tutorial_N.m1v> <out folder> [N ...]
"""
import os, sys, subprocess, numpy as np, cv2
from PIL import Image, ImageDraw, ImageFont

OW, OH = 512, 288
FONT = os.environ.get('GITSFA_FONT_BOLD') or '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'   # DejaVu Sans Bold (shipped with the builder)
FFMPEG = os.environ.get('GITSFA_FFMPEG', 'ffmpeg')
# libx264's output depends on its thread count: the shipped clips were encoded with 3 threads (its default on 2 cores)
X264_THREADS = os.environ.get('GITSFA_X264_THREADS', '3')
# per clip: source file, size, fps, and badges: (box x0 y0 x1 y1 in source pixels, kind, symbol, hold, time window -
# measured: where each badge is on screen, outside the white transition flashes; a key cap's window starts with its fade-in
# and ends with its fade-out, its reference frame is KEYCAP_REF s)
CLIPS = {
    1: ('tutorial_1', 1280, 720, 24, [((652, 581, 698, 621), 'badge', 'dpad', 0, (1.9, 10.7)), ((652, 581, 698, 621), 'badge', 'dpad', 1, (13.3, 99))]),
    2: ('tutorial_2', 1280, 720, 24, []),
    3: ('tutorial_3', 1064, 456, 25, []),
    4: ('tutorial_4', 1280, 720, 24, [((619, 139, 663, 180), 'badge', 'dpad', 1, (2.4, 7.9))]),
    5: ('tutorial_5', 1280, 720, 24, [((619, 139, 663, 180), 'badge', 'square', 0, (2.6, 12.9))]),
    6: ('Tutorial_6', 1064, 456, 25, [((381, 201, 445, 259), 'keycap', 'square', 0, (17.08, 19.6))]),
}
KEYCAP_REF = 18.5


def symbol(kind, size, col, lw):
    """the Vita control as an RGBA image size x size (4x supersampled): 'dpad' = the D-pad with its right arm filled,
    'square' = the Square button's outline"""
    S = size * 4; im = Image.new('RGBA', (S, S), (0, 0, 0, 0)); d = ImageDraw.Draw(im); c = tuple(col) + (255,); w = max(4, int(lw * 4))
    if kind == 'square':
        m = S * 0.2; d.rectangle([m, m, S - m, S - m], outline=c, width=w)
    else:   # D-pad: a plus of 5 cells; outline, the right cell filled
        u = S / 3.4; cx = cy = S / 2
        cells = [(cx - u / 2, cy - u * 1.5), (cx - u / 2, cy + u / 2), (cx - u * 1.5, cy - u / 2), (cx + u / 2, cy - u / 2)]   # up, down, left, right
        for i, (x, y) in enumerate(cells):
            if i == 3: d.rectangle([x, y, x + u, y + u], fill=c)
            else: d.rectangle([x, y, x + u, y + u], outline=c, width=max(3, w * 2 // 3))
    return im.resize((size, size), Image.LANCZOS)


def keycap_ref(src, W, H, fps, box):
    """the key cap's letter in its reference frame: mask (dilated), colour, contrast over the cap"""
    n = int(round(KEYCAP_REF * fps)); x0, y0, x1, y1 = box
    b = subprocess.run([FFMPEG, '-v', 'error', '-i', src, '-vf', 'select=eq(n\\,%d)' % n, '-frames:v', '1', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'],
                       stdout=subprocess.PIPE, check=True).stdout
    r = np.frombuffer(b[:W * H * 3], np.uint8).reshape(H, W, 3)[y0:y1, x0:x1].astype(np.float32); lum = r.mean(2)
    inner = np.zeros(lum.shape, bool); inner[8:-8, 8:-8] = True
    bg = np.median(lum[inner & (lum < np.percentile(lum[inner], 55))]); fg = np.percentile(lum[inner], 97)
    m = inner & (lum > bg + 0.5 * (fg - bg)); col = np.median(r[m], 0)
    m = cv2.dilate(m.astype(np.uint8), np.ones((5, 5), np.uint8)); mb = m.astype(bool)
    inp = cv2.inpaint(np.ascontiguousarray(r.astype(np.uint8)), m, 3, cv2.INPAINT_TELEA).astype(np.float32).mean(2)
    return m, col, float((lum[mb] - inp[mb]).mean())   # (the same measure as patch's, so a fully shown letter = 1)


def patch(frame, box, kind, sym, hold, log, ref=None):
    x0, y0, x1, y1 = box; r = frame[y0:y1, x0:x1].astype(np.float32); lum = r.mean(2); alpha = 1.0
    if kind == 'badge':   # a filled key badge (white, or red when active) with a dark letter
        red = (r[..., 0] > 140) & (r[..., 1] < 90) & (r[..., 2] < 90); wht = r.min(2) > 170
        if lum.min() > 200: return False   # a full-white flash frame, no badge
        if red.mean() > 0.3: bgm = red
        elif wht.mean() > 0.3: bgm = wht
        else: return False
        bg = np.median(r[bgm], 0); dark = lum <= np.percentile(lum, 6); fg = np.median(r[dark], 0)
        if bg.mean() - fg.mean() < 40 or fg.mean() > 90: return False
        ins = 6
        # erase the letter with the badge colour
        frame[y0 + ins:y1 - ins, x0 + ins:x1 - ins] = bg.astype(np.uint8)
    else:   # a translucent key cap with a light letter, fading in / out over the scene: inpaint the letter away
        m, fg, c = ref; reg = np.ascontiguousarray(frame[y0:y1, x0:x1]); inp = cv2.inpaint(reg, m, 3, cv2.INPAINT_TELEA)
        mb = m.astype(bool); alpha = float(np.clip((lum[mb].mean() - inp.astype(np.float32).mean(2)[mb].mean()) / c, 0, 1))
        frame[y0:y1, x0:x1] = inp
        if alpha < 0.08: log[0] += 1; return True   # (letter erased; too faint to draw the button)
        ins = 10
    # draw the Vita control in the letter's colour (and opacity)
    sz = min(x1 - x0, y1 - y0) - 2 * ins + (4 if kind == 'badge' else 8)
    im = Image.fromarray(frame); s = symbol(sym, sz, np.asarray(fg).astype(int), max(2.0, sz / 9))
    if alpha < 1: s.putalpha(s.getchannel('A').point(lambda v: int(v * alpha)))
    base = im.convert('RGBA'); base.alpha_composite(s, (int((x0 + x1 - sz) / 2), int((y0 + y1 - sz) / 2)))
    if hold:   # "HOLD" after the badge, white as the TIER captions
        d = ImageDraw.Draw(base); f = ImageFont.truetype(FONT, int((y1 - y0) * 0.62)); d.text((x1 + 8, (y0 + y1) / 2), 'HOLD', font=f, fill=(235, 235, 235, 255), anchor='lm')
    frame[:] = np.asarray(base.convert('RGB'))
    log[0] += 1; return True


def build(n, src_dir, out_dir):
    name, W, H, fps, badges = CLIPS[n]; src = os.path.join(src_dir, name + '.m1v'); out = os.path.join(out_dir, 'tut_v%d.mp4' % n)
    dec = subprocess.Popen([FFMPEG, '-v', 'error', '-i', src, '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-'], stdout=subprocess.PIPE)
    enc = subprocess.Popen([FFMPEG, '-v', 'error', '-y', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', '%dx%d' % (W, H), '-r', str(fps), '-i', '-',
                            '-vf', ('' if W * 9 == H * 16 else 'crop=%d:%d:%d:0,' % (H * 16 // 9 // 2 * 2, H, (W - H * 16 // 9 // 2 * 2) // 2)) + 'scale=%d:%d:flags=lanczos,format=yuv420p' % (OW, OH), '-c:v', 'libx264', '-threads', X264_THREADS, '-profile:v', 'baseline', '-level', '3.0',
                            '-refs', '1', '-bf', '0', '-preset', 'slow', '-crf', '22', '-g', str(fps * 2), '-an', '-movflags', '+faststart', out], stdin=subprocess.PIPE)
    refs = [keycap_ref(src, W, H, fps, b[0]) if b[1] == 'keycap' else None for b in badges]
    i = 0; log = [0]; first = last = None
    while True:
        b = dec.stdout.read(W * H * 3)
        if len(b) < W * H * 3: break
        f = np.frombuffer(b, np.uint8).reshape(H, W, 3).copy(); t = i / fps
        for (box, kind, sym, hold, (ta, tb)), ref in zip(badges, refs):
            if ta <= t < tb and patch(f, box, kind, sym, hold, log, ref): first = t if first is None else first; last = t
        enc.stdin.write(f.tobytes()); i += 1
    enc.stdin.close(); enc.wait(); dec.wait()
    print('%s: %d frames, %d badge frames replaced%s, %d bytes' % (out, i, log[0], ' (%.2f-%.2f s)' % (first, last) if first is not None else '', os.path.getsize(out)))


if __name__ == '__main__':
    src_dir, out_dir = sys.argv[1], sys.argv[2]; os.makedirs(out_dir, exist_ok=True)
    for n in (map(int, sys.argv[3:]) if len(sys.argv) > 3 else sorted(CLIPS)): build(n, src_dir, out_dir)
