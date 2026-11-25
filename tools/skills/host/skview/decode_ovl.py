#!/usr/bin/env python3
# sk_ovl.gtx (GTX1 DXT1) -> raw RGBA (u32 w, u32 h, pixels) for the viewer's software GL, which only decodes DXT5
import struct, sys, numpy as np
b = open(sys.argv[1], 'rb').read(); w, h = struct.unpack_from('<HH', b, 4); fmt = b[8]; sz, = struct.unpack_from('<I', b, 12); d = np.frombuffer(b[16:16 + sz], np.uint8)
assert fmt == 1
blk = d.reshape(-1, 8); c0 = blk[:, 0].astype(np.uint32) | blk[:, 1].astype(np.uint32) << 8; c1 = blk[:, 2].astype(np.uint32) | blk[:, 3].astype(np.uint32) << 8
idx = blk[:, 4].astype(np.uint32) | blk[:, 5].astype(np.uint32) << 8 | blk[:, 6].astype(np.uint32) << 16 | blk[:, 7].astype(np.uint32) << 24
def rgb(c): return np.stack([(c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31], 1).astype(np.int32)
A, B = rgb(c0), rgb(c1); four = (c0 > c1)[:, None]
C2 = np.where(four, (2 * A + B) // 3, (A + B) // 2); C3 = np.where(four, (A + 2 * B) // 3, 0)
pal = np.stack([A, B, C2, C3], 1)   # (n, 4, 3)
out = np.zeros((h // 4, w // 4, 4, 4, 4), np.uint8); n = 0
sel = np.stack([(idx >> (2 * i)) & 3 for i in range(16)], 1)   # (n, 16)
px = np.take_along_axis(pal, sel[:, :, None].astype(np.int64), 1)   # (n, 16, 3)
alpha = np.where((~four[:, 0])[:, None] & (sel == 3), 0, 255)
px = np.concatenate([px, alpha[:, :, None]], 2).reshape(h // 4, w // 4, 4, 4, 4)
img = px.transpose(0, 2, 1, 3, 4).reshape(h, w, 4).astype(np.uint8)
open(sys.argv[2], 'wb').write(struct.pack('<II', w, h) + img.tobytes())
