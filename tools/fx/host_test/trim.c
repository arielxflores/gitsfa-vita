// b81 host check of fx.h's particle trimming (fxtrim.h): every effect drawn twice from the same particles, as quads and
// trimmed, by the software rasteriser (vitaGL.h): the pictures must be the same and the trimmed one shades fewer fragments.
// Part 1 checks every trim polygon of fx_sk.gfx directly: sample points of the frame outside the polygon must read only
// texels that cannot show (fxt_texel), on a 1/8-texel grid plus the texel-square corners.
//   build: gcc -std=gnu11 -O2 -w -I. -I../../../engine/src trim.c -o trim -lm && ./trim [fx_sk.gfx] [out dir]
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static float A[SW_H][SW_W][3];
static int find_eff(const char *nm) { for (int i = 0; i < fx_neff; i++) if (!strcmp(fx_eff[i].name, nm)) return i; return -1; }
static int inside(const FxTrim *T, double x, double y) {   // in the polygon (either orientation, boundary counts as inside)
	int pos = 0, neg = 0;
	for (int k = 0; k < T->n; k++) { const float *a = T->p[k], *b = T->p[(k + 1) % T->n]; double c = (b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]);
		if (c > 1e-7) pos = 1; else if (c < -1e-7) neg = 1; }
	return !(pos && neg);
}
static int check_polys(const unsigned char *mip0, int W, int H) {
	int bad = 0, nt = 0, ne = 0; double sa = 0;
	for (int i = 0; i < fx_nem; i++) { const FxEmit *e = &fx_em[i]; if (fx_trim_at[i] < 0) continue;
		for (int fr = 0; fr < e->nfr; fr++) { const FxTrim *T = &fx_trim[fx_trim_at[i] + fr]; if (!T->n) continue;
			float uv[4]; fx_uv(e->cell, e->fx, e->fy, fr, uv); double X0 = uv[0] * W, Y0 = uv[1] * H, X1 = uv[2] * W, Y1 = uv[3] * H;
			if (T->n == FXT_EMPTY) ne++; else { nt++; sa += T->area; }
			int fb = 0;
			for (double y = Y0; y <= Y1 + 1e-9 && fb < 3; y += 0.125) for (double x = X0; x <= X1 + 1e-9 && fb < 3; x += 0.125) {
				double u = (x - X0) / (X1 - X0), v = (y - Y0) / (Y1 - Y0);
				if (T->n != FXT_EMPTY && inside(T, u, v)) continue;
				// bilinear reads at x: texels floor(x - 0.5) and + 1 (a weight of exactly 0 still counted), clamped
				int tx = (int)floor(x - 0.5), ty = (int)floor(y - 0.5);
				for (int j = 0; j < 4; j++) { int xx = tx + (j & 1), yy = ty + (j >> 1); if (xx < 0) xx = 0; if (yy < 0) yy = 0; if (xx >= W) xx = W - 1; if (yy >= H) yy = H - 1;
					if (fxt_texel(mip0, W, xx, yy, e->blend == 1)) { if (!fb) printf("BAD em %d frame %d: sample %.3f %.3f outside the polygon reads texel %d %d\n", i, fr, x, y, xx, yy); fb++; bad++; break; } }
			}
		}
	}
	printf("polygons: %d trimmed frames (mean area %.3f of the quad), %d empty, %d sample-point failures\n", nt, nt ? sa / nt : 0, ne, bad);
	return bad;
}
// pixels changed by more than one 8-bit level (rounding of the interpolated uv moves values by under a level); a picture
// fails with more than 2 such pixels: a polygon corner on the quad's own edge snaps to the GPU's sub-pixel grid a hair
// differently from the quad's corner, which can move one pixel of that edge in or out (as a quad drawn with the other
// diagonal would)
static double cmp(long *ndiff) {
	double m = 0; long n = 0;
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { int c = 0; for (int k = 0; k < 3; k++) { double d = fabs(A[y][x][k] - sw_fb[y][x][k]); if (d > m) m = d; if (d > 1.0 / 255) c = 1; } n += c; }
	*ndiff = n; return m;
}
int main(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : "/root/gits/build/data/fx_sk.gfx", *outd = argc > 2 ? argv[2] : NULL;
	mkbg(); sprite_init(mk); int r = fx_init(path); if (r < 0) { printf("fx_init %d\n", r); return 1; }
	printf("fx: %d effects, %d emitters, %d trim entries\n", fx_neff, fx_nem, fx_ntrim);
	// part 1: the polygons themselves (mip 0 again, straight from the file)
	{ FILE *f = fopen(path, "rb"); unsigned char h[12]; fread(h, 1, 12, f); int W = h[4] | h[5] << 8, H = h[6] | h[7] << 8; unsigned sz; fread(&sz, 4, 1, f);
	  unsigned char *m0 = malloc(sz); fread(m0, 1, sz, f); fclose(f); if (check_polys(m0, W, H)) return 2; free(m0); }
	// part 2: pictures
	fx_trim_budget = 0;   // (the same thinning both times: this part checks the drawing; the budget's own effect: expl.c)
	struct { const char *eff; float dist, z, t[4]; } S[] = {
		{ "HIT_BLOOD", 60, 150, { 0.05f, 0.15f, 0.3f, 0.6f } }, { "HIT_BLOOD", 150, 150, { 0.05f, 0.15f, 0.3f, 0.6f } }, { "HIT_BLOOD", 400, 150, { 0.05f, 0.15f, 0.3f, 0.6f } },
		{ "DX_BURST", 80, 165, { 0.05f, 0.2f, 0.5f, 1.0f } }, { "DX_BURST", 250, 165, { 0.05f, 0.2f, 0.5f, 1.0f } },
		{ "HIT_CYBORG", 100, 150, { 0.03f, 0.1f, 0.25f, 0.5f } },
		{ "EXPLOSION", 150, 60, { 0.05f, 0.25f, 0.6f, 1.2f } }, { "EXPLOSION", 400, 60, { 0.05f, 0.25f, 0.6f, 1.2f } },
		{ "IMPACT_DEFAULT", 80, 120, { 0.05f, 0.2f, 0.5f, 1.0f } }, { "IMPACT_SAND", 120, 60, { 0.05f, 0.2f, 0.5f, 1.0f } }, { "IMPACT_METAL", 90, 140, { 0.03f, 0.1f, 0.2f, 0.4f } },
		{ "IMPACT_GLASS", 90, 140, { 0.03f, 0.1f, 0.2f, 0.4f } }, { "SK_EMP_EXP", 300, 100, { 0.05f, 0.2f, 0.5f, 0.9f } }, { "SK_MS_EXP", 300, 80, { 0.05f, 0.3f, 0.7f, 1.2f } },
		{ "SK_SH_BREAK", 120, 150, { 0.05f, 0.2f, 0.4f, 0.8f } }, { "DX_STUMP", 70, 160, { 0.1f, 0.4f, 0.8f, 1.5f } }, { "MUZZLE_3P", 120, 150, { 0.01f, 0.03f, 0.05f, 0.08f } },
	};
	long tq = 0, tt = 0, tpx = 0; int fail = 0, npic = 0; char nm[128];
	for (unsigned s = 0; s < sizeof(S) / sizeof(S[0]); s++) {
		int id = find_eff(S[s].eff); if (id < 0) { printf("no effect %s\n", S[s].eff); continue; }
		for (int rep = 0; rep < 3; rep++) {   // three random rolls / spins
			fx_clear(); fx_seed = 0x12345u + 977u * s + 31u * rep; setcam(0, 0, 160, rep * 0.07f, -0.02f * rep, 70); float t = 0;
			float p[3] = { S[s].dist, 8.0f * rep, S[s].z }, n[3] = { -1, 0, 0.15f }, d[3] = { 1, 0, 0 }; fx_norm(n);
			fx_spawn(id, p, n, d);
			for (int k = 0; k < 4; k++) { run_to(&t, S[s].t[k]);
				scene(VP, 2000); sw_pixels = 0; fx_trim_on = 0; fx_draw(VP, cam, R, U); long q = sw_pixels; memcpy(A, sw_fb, sizeof(A));
				scene(VP, 2000); sw_pixels = 0; fx_trim_on = 1; fx_draw(VP, cam, R, U); long tr = sw_pixels;
				long nd; double md = cmp(&nd); tq += q; tt += tr; npic++;
				if (nd > 2) fail++; tpx += nd;
				if (rep == 0) printf("%-14s %4.0f cm t %.2f: particles %3d, fragments %7ld quads -> %7ld trimmed (%4.1f%% fewer, %d trimmed), picture diff max %.2g, %ld pixels off by more than a level\n",
					S[s].eff, S[s].dist, t, fx_np, q, tr, q ? 100.0 * (q - tr) / q : 0, fx_stat_trim, md, nd);
				else if (nd) printf("  %s roll %d t %.2f: %ld pixels off by more than a level (max %.2g)\n", S[s].eff, rep, t, nd, md);
				if (outd && rep == 0 && k == 1) { snprintf(nm, sizeof(nm), "%s/trim_%s_%d", outd, S[s].eff, (int)S[s].dist); FILE *f = fopen(strcat(nm, ".ppm"), "wb"); fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
					for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int c = 0; c < 3; c++) fputc((int)(sw_fb[y][x][c] * 255 + 0.5f), f); fclose(f); }
			}
		}
	}
	// first-person muzzle flash (view space, the weapon's matrix = the projection)
	{ long q = 0, tr = 0, nd = 0; double md = 0;
	  for (int rep = 0; rep < 12; rep++) { fx_clear(); fx_seed = 777u + rep; float pc[3] = { 9, -7, -45 }, dc[3] = { 0.02f * rep - 0.1f, 0.01f, -1 }; fx_norm(dc); fx_muzzle_fp(pc, dc); float t = 0; run_to(&t, 0.01f + 0.004f * rep);
		float Pm[16]; persp(Pm, 55 * 3.14159f / 180, 960.0f / 544, 1, 1000);
		scene(VP, 2000); sw_depth = 0; sw_pixels = 0; fx_trim_on = 0; fx_draw_fp(Pm); q += sw_pixels; memcpy(A, sw_fb, sizeof(A));
		scene(VP, 2000); sw_depth = 0; sw_pixels = 0; fx_trim_on = 1; fx_draw_fp(Pm); tr += sw_pixels; long n1; double m1 = cmp(&n1); nd += n1; if (m1 > md) md = m1; }
	  printf("MUZZLE_FP (12 shots): fragments %ld -> %ld (%.1f%% fewer), %ld pixels off by more than a level (max %.2g)\n", q, tr, q ? 100.0 * (q - tr) / q : 0, nd, md); tq += q; tt += tr; tpx += nd; npic += 12; if (nd > 2 * 12) fail++; }
	printf("TOTAL fragments %ld -> %ld: %.1f%% fewer; %d pictures, %ld pixels off by more than a level in all, %d pictures failed\n", tq, tt, 100.0 * (tq - tt) / tq, npic, tpx, fail);
	return fail ? 3 : 0;
}
