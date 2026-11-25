// b81 host check of the screen-overlay shapes (skills.h sk_ovl_quad -> fx.h fx_rect_shaped, fxtrim.h fxt_hole / fxt_ring /
// fxt_frame_ex with the DXT1 rule): every additive cell of sk_ovl.gtx drawn over a scene as the whole quad and through its
// shape (ring around the black middle, or polygon around the logo), full screen, scaled (the rush lines) and jittered (the
// glitch) - the pictures must match and the shape must shade fewer fragments.
//   build: gcc -std=gnu11 -O2 -w -I. -I../../../engine/src ovlhole.c -o ovlhole -lm && ./ovlhole [sk_ovl.gtx]
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static float A[SW_H][SW_W][3];
static const char *NM[12] = { "vig", "camo", "meta", "shield", "rush", "dive", "glitch", "hlines", "dive_mul", "heat_mul", "laugh", "shield_hit" };
static const float UV[12][5] = {   // skills.h SKV_UV
	{ 0.000488f, 0.000488f, 0.499512f, 0.249512f, 1 }, { 0.500488f, 0.000488f, 0.999512f, 0.249512f, 1 }, { 0.000488f, 0.250488f, 0.499512f, 0.499512f, 1 },
	{ 0.500488f, 0.250488f, 0.999512f, 0.499512f, 1 }, { 0.000488f, 0.500488f, 0.499512f, 0.749512f, 1 }, { 0.500488f, 0.500488f, 0.999512f, 0.749512f, 1 },
	{ 0.000488f, 0.750488f, 0.249512f, 0.874512f, 1 }, { 0.250488f, 0.750488f, 0.499512f, 0.874512f, 1 }, { 0.500488f, 0.750488f, 0.749512f, 0.874512f, 2 },
	{ 0.750488f, 0.750488f, 0.999512f, 0.874512f, 2 }, { 0.000488f, 0.875488f, 0.249512f, 0.999512f, 1 }, { 0.250488f, 0.875488f, 0.499512f, 0.999512f, 1 } };
static double cmp(long *nd) { double m = 0; long n = 0; for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { int c = 0; for (int k = 0; k < 3; k++) { double d = fabs(A[y][x][k] - sw_fb[y][x][k]); if (d > m) m = d; if (d > 1.0 / 255) c = 1; } n += c; } *nd = n; return m; }
int main(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : "/root/gits/build/data/sk_ovl.gtx";
	FILE *f = fopen(path, "rb"); unsigned char h[16]; if (!f || fread(h, 1, 16, f) != 16) return 1;
	int W = h[4] | h[5] << 8, H = h[6] | h[7] << 8; unsigned sz = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24; unsigned char *d = malloc(sz); fread(d, 1, sz, f); fclose(f); unsigned char *msk = fxt_mask(d, W, H, 1);
	unsigned char *px = malloc((size_t)W * H * 4);   // DXT1 -> RGBA (3-colour mode: index 3 transparent black)
	for (int by = 0; by < H / 4; by++) for (int bx = 0; bx < W / 4; bx++) { const unsigned char *b = d + ((size_t)by * (W / 4) + bx) * 8;
		unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8, idx = b[4] | b[5] << 8 | b[6] << 16 | (unsigned)b[7] << 24; int c[4][4];
		for (int e = 0; e < 2; e++) { unsigned v = e ? c1 : c0; c[e][0] = (v >> 11) * 255 / 31; c[e][1] = ((v >> 5) & 63) * 255 / 63; c[e][2] = (v & 31) * 255 / 31; c[e][3] = 255; }
		for (int k = 0; k < 3; k++) { if (c0 > c1) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; } else { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; } }
		c[2][3] = 255; c[3][3] = c0 > c1 ? 255 : 0;
		for (int i = 0; i < 16; i++) { int ci = (idx >> (2 * i)) & 3; unsigned char *o = px + (((size_t)(by * 4 + i / 4) * W) + bx * 4 + i % 4) * 4; for (int k = 0; k < 4; k++) o[k] = (unsigned char)c[ci][k]; } }
	mkbg(); sprite_init(mk); GLuint tex; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	setcam(0, 0, 160, 0.2f, -0.1f, 70);
	long tq = 0, ts = 0; int fail = 0;
	for (int c = 0; c < 12; c++) {
		if (UV[c][4] != 1) continue;
		FxTrim t; FxHole hl; double X0 = UV[c][0] * W, Y0 = UV[c][1] * H, X1 = UV[c][2] * W, Y1 = UV[c][3] * H;
		fxt_frame_ex(msk, W, H, X0, Y0, X1, Y1, fxt_mtexel, 1, &t); fxt_hole(msk, W, H, X0, Y0, X1, Y1, 1, &hl);
		double st = t.n == FXT_EMPTY ? 1 : t.n ? 1 - t.area : 0, sh = hl.n ? hl.area : 0; const FxTrim *ut = NULL; const FxHole *uh = NULL;
		if (st >= sh && st >= 0.05) ut = &t; else if (sh > st && sh >= 0.05) uh = &hl;
		printf("%-10s trim %s (saves %4.1f%%), hole %d corners (saves %4.1f%%, %d ring triangles) -> %s\n", NM[c], t.n == FXT_EMPTY ? "empty" : t.n ? "poly" : "none", 100 * st, hl.n, 100 * sh, hl.nt, ut ? "polygon" : uh ? "ring" : "quad");
		struct { float x0, y0, x1, y1; } R_[4] = { { -1, -1, 1, 1 }, { -1.15f, -1.15f, 1.15f, 1.15f }, { -1.03f, -0.98f, 1.03f, 1.02f }, { -0.45f, -0.8f, 0.45f, 0.8f } };
		for (int r = 0; r < 4; r++) {
			const float col[4] = { 0.9f, 0.6f, 0.5f, 0.8f };
			scene(VP, 2000); sw_depth = 0; sw_pixels = 0; float a[3] = { R_[r].x0, R_[r].y1, 0 }, b[3] = { R_[r].x1, R_[r].y1, 0 }, cc[3] = { R_[r].x1, R_[r].y0, 0 }, dd[3] = { R_[r].x0, R_[r].y0, 0 };
			sprite_quad(a, b, cc, dd, UV[c][0], UV[c][1], UV[c][2], UV[c][3], col); sprite_flush_mode(I, tex, 1); long q = sw_pixels; memcpy(A, sw_fb, sizeof(A));
			scene(VP, 2000); sw_depth = 0; sw_pixels = 0; fx_rect_shaped(R_[r].x0, R_[r].y0, R_[r].x1, R_[r].y1, UV[c][0], UV[c][1], UV[c][2], UV[c][3], col, ut, uh); sprite_flush_mode(I, tex, 1); long s = sw_pixels;
			long nd; double md = cmp(&nd); tq += q; ts += s; if (nd > 2) fail++;
			printf("   rect %d: fragments %6ld -> %6ld (%4.1f%% fewer), max diff %.2g, %ld pixels off by more than a level\n", r, q, s, q ? 100.0 * (q - s) / q : 0, md, nd);
		}
	}
	printf("TOTAL fragments %ld -> %ld (%.1f%% fewer), %d pictures failed\n", tq, ts, 100.0 * (tq - ts) / tq, fail);
	return fail ? 3 : 0;
}
