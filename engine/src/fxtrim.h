// b81: particle trimming - a particle is drawn as a tight convex polygon (<= FXT_K corners) around the part of its atlas frame
// that can change the picture, instead of the whole quad. Idea from the Burst Limit Vita chat (its full-screen shaders skip
// the work where their output is exactly zero; the Vita's GPU pays per fragment shaded, transparent ones too); here it is
// done on the geometry side, so no shader changes and no branches. A blood cloud 1 m away fills ~0.4 screens; most of its
// quad is transparent corners.
//
// Exact by construction: a texel can matter only if it is not exactly zero after the GPU's DXT5 decode (alpha blend: alpha
// != 0; additive: alpha != 0 and rgb != 0; decided per palette entry, conservatively: an interpolated entry counts as zero
// only when both of its endpoints are zero). Bilinear filtering reads a texel at sample points within one texel of its
// centre (open square), so the polygon holds every such square of every texel that can matter, clipped to the frame.
// Outside the polygon the full quad would have drawn exactly nothing (alpha blend: dst * (1 - 0) + src * 0; additive:
// dst + 0). It is used only where the GPU samples mip 0 alone (the particle magnified by 2x or more: fx.h FXT_GATE); the
// smaller mips blur the frame outwards, and small particles cost little fill anyway.
// Pure C (no GL): fx.h builds the table at load from fx_sk.gfx's mip 0 (fxt_frame per emitter frame); host tests in
// tools/fx/host_test/trim.c (pixel-identical pictures, fragment counts).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define FXT_K 8
#define FXT_EMPTY 255        // nothing in the frame can show
#define FXT_MAXAREA 0.92f
#define FXT_MARGIN (1.0 / 32)   // texels added around each texel's reach    // a polygon covering more of the quad than this is not worth the extra vertices: draw the quad
typedef struct { unsigned char n; float area; float p[FXT_K][2]; } FxTrim;   // n 0: draw the quad; corners in frame units [0,1]^2

// can texel (x, y) of a DXT5 image (W texels wide) change the picture? add 0: alpha blend (alpha != 0), 1: additive (also rgb)
static int fxt_texel(const unsigned char *dxt, int W, int x, int y, int add) {
	const unsigned char *b = dxt + ((size_t)(y >> 2) * (W >> 2) + (x >> 2)) * 16; int i = (y & 3) * 4 + (x & 3);
	unsigned a0 = b[0], a1 = b[1]; uint64_t ab = 0; for (int k = 0; k < 6; k++) ab |= (uint64_t)b[2 + k] << (8 * k);
	int ai = (int)((ab >> (3 * i)) & 7), az;
	if (ai == 0) az = a0 == 0; else if (ai == 1) az = a1 == 0;
	else if (a0 <= a1 && ai == 6) az = 1; else if (a0 <= a1 && ai == 7) az = 0;   // 6-alpha mode: explicit 0 and 255
	else az = a0 == 0 && a1 == 0;                                                  // interpolated
	if (az) return 0;
	if (!add) return 1;
	unsigned c0 = b[8] | b[9] << 8, c1 = b[10] | b[11] << 8, ci = ((unsigned)(b[12] | b[13] << 8 | b[14] << 16 | (unsigned)b[15] << 24) >> (2 * i)) & 3;
	int cz = ci == 0 ? c0 == 0 : ci == 1 ? c1 == 0 : (c0 == 0 && c1 == 0);   // (3-colour mode's black index 3 also reads as "may matter": conservative)
	return !cz;
}
typedef struct { double x, y; } FxtPt;
static int fxt_cmp(const void *a, const void *b) { const FxtPt *p = a, *q = b; return p->x < q->x ? -1 : p->x > q->x ? 1 : p->y < q->y ? -1 : p->y > q->y; }
static double fxt_cr(FxtPt o, FxtPt a, FxtPt b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }
static int fxt_hull(FxtPt *P, int n, FxtPt *H) {   // monotone chain, counter-clockwise (math orientation), collinear points dropped
	if (n < 3) { memcpy(H, P, sizeof(FxtPt) * n); return n; }
	qsort(P, n, sizeof(FxtPt), fxt_cmp); int k = 0;
	for (int i = 0; i < n; i++) { while (k >= 2 && fxt_cr(H[k - 2], H[k - 1], P[i]) <= 0) k--; H[k++] = P[i]; }
	for (int i = n - 2, t = k + 1; i >= 0; i--) { while (k >= t && fxt_cr(H[k - 2], H[k - 1], P[i]) <= 0) k--; H[k++] = P[i]; }
	return k - 1;
}
static int fxt_clip(const FxtPt *P, int n, FxtPt *O) {   // convex polygon against [0,1]^2 (Sutherland-Hodgman), O: room for n + 4
	FxtPt A[260], B[260]; if (n > 250) return 0; memcpy(A, P, sizeof(FxtPt) * n);
	for (int s = 0; s < 4; s++) {
		int m = 0;
		for (int i = 0; i < n; i++) { FxtPt a = A[i], b = A[(i + 1) % n];
			double va = s == 0 ? a.x : s == 1 ? 1 - a.x : s == 2 ? a.y : 1 - a.y, vb = s == 0 ? b.x : s == 1 ? 1 - b.x : s == 2 ? b.y : 1 - b.y;
			if (va >= 0) B[m++] = a;
			if ((va >= 0) != (vb >= 0)) { double t = va / (va - vb); B[m++] = (FxtPt){ a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) }; } }
		n = m; memcpy(A, B, sizeof(FxtPt) * n); if (!n) return 0;
	}
	memcpy(O, A, sizeof(FxtPt) * n); return n;
}
static double fxt_area(const FxtPt *P, int n) { double a = 0; for (int i = 0; i < n; i++) a += P[i].x * P[(i + 1) % n].y - P[(i + 1) % n].x * P[i].y; return 0.5 * a; }
// fewer corners: an edge goes, its two neighbours extended until they meet (the polygon only grows), least added area first;
// the meeting point must stay inside the frame. Returns the new count (> k when it cannot get there). The cost of removing
// each edge is cached and only the edges next to a removal are worked out again.
static int fxt_edge_cost(const FxtPt *H, int n, int i, FxtPt *pp, double *cost) {   // 0: edge i cannot go
	const double eps = 1e-9;
	FxtPt a = H[(i + n - 1) % n], b = H[i], c = H[(i + 1) % n], d = H[(i + 2) % n];
	double den = (a.x - b.x) * (c.y - d.y) - (a.y - b.y) * (c.x - d.x); if (fabs(den) < 1e-14) return 0;
	double t = ((a.x - c.x) * (c.y - d.y) - (a.y - c.y) * (c.x - d.x)) / den; FxtPt p = { a.x + t * (b.x - a.x), a.y + t * (b.y - a.y) };
	if (p.x < -eps || p.x > 1 + eps || p.y < -eps || p.y > 1 + eps) return 0;
	if (fxt_cr(b, c, p) > eps) return 0;                                                  // must lie outside the edge b -> c
	if ((p.x - b.x) * (b.x - a.x) + (p.y - b.y) * (b.y - a.y) < -eps) return 0;           // forward along a -> b
	if ((p.x - c.x) * (c.x - d.x) + (p.y - c.y) * (c.y - d.y) < -eps) return 0;           // backward along d -> c
	*pp = p; *cost = fabs(fxt_cr(b, p, c)) * 0.5; return 1;
}
static int fxt_reduce(FxtPt *H, int n, int k) {
	if (n <= k) return n;
	if (n > 512) return n;
	double cost[512]; FxtPt cp[512]; unsigned char ok[512];
	for (int i = 0; i < n; i++) ok[i] = (unsigned char)fxt_edge_cost(H, n, i, &cp[i], &cost[i]);
	while (n > k) {
		int bi = -1; for (int i = 0; i < n; i++) if (ok[i] && (bi < 0 || cost[i] < cost[bi])) bi = i;
		if (bi < 0) return n;
		FxtPt bp = cp[bi]; if (bp.x < 0) bp.x = 0; if (bp.x > 1) bp.x = 1; if (bp.y < 0) bp.y = 0; if (bp.y > 1) bp.y = 1;
		int del = (bi + 1) % n; H[bi] = bp;
		memmove(&H[del], &H[del + 1], sizeof(FxtPt) * (n - 1 - del)); memmove(&cost[del], &cost[del + 1], sizeof(double) * (n - 1 - del));
		memmove(&cp[del], &cp[del + 1], sizeof(FxtPt) * (n - 1 - del)); memmove(&ok[del], &ok[del + 1], n - 1 - del);
		n--; int at = del < bi ? bi - 1 : bi;   // the moved corner's index now
		for (int j = -2; j <= 1; j++) { int e = ((at + j) % n + n) % n; ok[e] = (unsigned char)fxt_edge_cost(H, n, e, &cp[e], &cost[e]); }
	}
	return n;
}
// DXT1 (skills.h's screen overlays, additive): can the texel's rgb be non-zero? (3-colour mode's index 3 is black)
static __attribute__((unused)) int fxt_texel1(const unsigned char *dxt, int W, int x, int y, int add) {
	(void)add; const unsigned char *b = dxt + ((size_t)(y >> 2) * (W >> 2) + (x >> 2)) * 8; int i = (y & 3) * 4 + (x & 3);
	unsigned c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8, ci = ((unsigned)(b[4] | b[5] << 8 | b[6] << 16 | (unsigned)b[7] << 24) >> (2 * i)) & 3;
	if (ci == 0) return c0 != 0; if (ci == 1) return c1 != 0;
	if (ci == 3 && c0 <= c1) return 0;
	return !(c0 == 0 && c1 == 0);
}
typedef int (*fxt_nz_fn)(const unsigned char *img, int W, int x, int y, int add);
// The same answers for a whole image at once (one decode per block; the trims of fx_sk.gfx read ~1.5 M texels): a W x H
// byte mask, bit 0 = can matter under alpha blending, bit 1 = under additive; fxt_mtexel reads it. DXT1: both bits from rgb.
static int fxt_mtexel(const unsigned char *m, int W, int x, int y, int add) { return m[(size_t)y * W + x] >> add & 1; }
static unsigned char *fxt_mask(const unsigned char *dxt, int W, int H, int dxt1) {
	if (W < 4 || H < 4 || (W & 3) || (H & 3)) return NULL;
	unsigned char *m = malloc((size_t)W * H); if (!m) return NULL;
	const int bs = dxt1 ? 8 : 16;
	for (int by = 0; by < H / 4; by++) for (int bx = 0; bx < W / 4; bx++) {
		const unsigned char *b = dxt + ((size_t)by * (W / 4) + bx) * bs, *cb = dxt1 ? b : b + 8;
		unsigned char az[8] = { 0 }; uint64_t ab = 0;   // alpha entry exactly zero?
		unsigned c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8, ci = cb[4] | cb[5] << 8 | cb[6] << 16 | (unsigned)cb[7] << 24;
		unsigned char *row = m + (size_t)by * 4 * W + bx * 4;
		if (!dxt1) { unsigned a0 = b[0], a1 = b[1]; for (int k = 0; k < 6; k++) ab |= (uint64_t)b[2 + k] << (8 * k);
			if (a0 == 0 && a1 == 0 && !(ab & (ab >> 1) & (ab >> 2) & 0x249249249249ull)) { for (int y = 0; y < 4; y++) memset(row + (size_t)y * W, 0, 4); continue; }   // all alpha 0 (no explicit 255)
			if (a0 > a1 && a1 > 0 && c0 && c1) { for (int y = 0; y < 4; y++) memset(row + (size_t)y * W, 3, 4); continue; }                               // every entry non-zero
			az[0] = a0 == 0; az[1] = a1 == 0; for (int k = 2; k < 8; k++) az[k] = a0 == 0 && a1 == 0; if (a0 <= a1) { az[6] = 1; az[7] = 0; } }
		else if (c0 == 0 && c1 == 0) { for (int y = 0; y < 4; y++) memset(row + (size_t)y * W, 0, 4); continue; }
		unsigned char cz[4] = { c0 == 0, c1 == 0, c0 == 0 && c1 == 0, (c0 == 0 && c1 == 0) || (dxt1 && c0 <= c1) };   // (DXT5's colour block: 3-colour mode reads as "may matter")
		for (int i = 0; i < 16; i++) {
			int a = dxt1 ? 1 : !az[(ab >> (3 * i)) & 7], c = !cz[(ci >> (2 * i)) & 3];
			row[(size_t)(i >> 2) * W + (i & 3)] = (unsigned char)(dxt1 ? (c ? 3 : 0) : (a | (a && c) << 1));
		}
	}
	return m;
}
// the frame's sample area in texel units [X0, X1] x [Y0, Y1] of a W x H DXT5 image (mip 0)
static void fxt_frame_ex(const unsigned char *dxt, int W, int H, double X0, double Y0, double X1, double Y1, fxt_nz_fn nz, int add, FxTrim *out);
static __attribute__((unused)) void fxt_frame(const unsigned char *dxt, int W, int H, double X0, double Y0, double X1, double Y1, int add, FxTrim *out) { fxt_frame_ex(dxt, W, H, X0, Y0, X1, Y1, fxt_texel, add, out); }
static void fxt_frame_ex(const unsigned char *dxt, int W, int H, double X0, double Y0, double X1, double Y1, fxt_nz_fn nz, int add, FxTrim *out) {
	memset(out, 0, sizeof(*out)); out->area = 1;
	if (!(X1 > X0) || !(Y1 > Y0)) return;
	// texels whose centre (k + 0.5) lies within one texel (+ the margin) of the sample area (clamped reads: the edge texels)
	int kx0 = (int)floor(X0 - 1.5 - FXT_MARGIN), kx1 = (int)ceil(X1 + 0.5 + FXT_MARGIN), ky0 = (int)floor(Y0 - 1.5 - FXT_MARGIN), ky1 = (int)ceil(Y1 + 0.5 + FXT_MARGIN);
	if (kx0 < 0) kx0 = 0; if (ky0 < 0) ky0 = 0; if (kx1 > W - 1) kx1 = W - 1; if (ky1 > H - 1) ky1 = H - 1;
	int nw = kx1 - kx0 + 1, nh = ky1 - ky0 + 1; if (nw <= 0 || nh <= 0 || nw > 1024 || nh > 1024) return;
	int *cmin = malloc(sizeof(int) * nw * 2), *rmin = malloc(sizeof(int) * nh * 2); FxtPt *P = malloc(sizeof(FxtPt) * ((nw + nh) * 8 + 64)), *Hh = malloc(sizeof(FxtPt) * ((nw + nh) * 16 + 64));   // (a hull's work space: twice the points)
	if (!cmin || !rmin || !P || !Hh) { free(cmin); free(rmin); free(P); free(Hh); return; }
	int *cmax = cmin + nw, *rmax = rmin + nh, any = 0;
	for (int i = 0; i < nw; i++) { cmin[i] = 1 << 30; cmax[i] = -1; }
	for (int j = 0; j < nh; j++) { rmin[j] = 1 << 30; rmax[j] = -1; }
	if (nz == fxt_mtexel) {   // a mask (fxt_mask): rows read straight, each from both ends inwards (the middle is not needed for the rows)
		const unsigned char bit = (unsigned char)(1 << add);
		for (int j = 0; j < nh; j++) { const unsigned char *r = dxt + (size_t)(ky0 + j) * W + kx0; int i0 = 0, i1 = nw - 1;
			while (i0 < nw && !(r[i0] & bit)) i0++;
			if (i0 == nw) continue;
			while (!(r[i1] & bit)) i1--;
			any = 1; rmin[j] = i0; rmax[j] = i1;
			for (int i = i0; i <= i1; i++) if (r[i] & bit) { if (j < cmin[i]) cmin[i] = j; cmax[i] = j; } }
	} else
	for (int j = 0; j < nh; j++) for (int i = 0; i < nw; i++) if (nz(dxt, W, kx0 + i, ky0 + j, add)) {
		any = 1; if (i < rmin[j]) rmin[j] = i; if (i > rmax[j]) rmax[j] = i; if (j < cmin[i]) cmin[i] = j; if (j > cmax[i]) cmax[i] = j; }
	if (!any) { out->n = FXT_EMPTY; out->area = 0; free(cmin); free(rmin); free(P); free(Hh); return; }
	int n = 0; const double sx = 1.0 / (X1 - X0), sy = 1.0 / (Y1 - Y0);
	// (the square grows by FXT_MARGIN texels: the GPU's filter weights and interpolated uv are not exact)
	// (only the outer corners can be on the hull: a row's leftmost texel gives its two left corners, and so on; and only where
	// the extreme moves - a row whose left end matches both neighbours' adds nothing the hull of theirs lacks)
#define FXT_C(i_, j_, cx_, cy_) do { P[n].x = ((kx0 + (i_) + ((cx_) ? 1.5 + FXT_MARGIN : -0.5 - FXT_MARGIN)) - X0) * sx; P[n].y = ((ky0 + (j_) + ((cy_) ? 1.5 + FXT_MARGIN : -0.5 - FXT_MARGIN)) - Y0) * sy; n++; } while (0)
	for (int j = 0; j < nh; j++) if (rmax[j] >= 0) {
		int pl = j > 0 && rmax[j - 1] >= 0 ? rmin[j - 1] : -1, nl = j + 1 < nh && rmax[j + 1] >= 0 ? rmin[j + 1] : -1;
		int pr = j > 0 && rmax[j - 1] >= 0 ? rmax[j - 1] : -1, nr = j + 1 < nh && rmax[j + 1] >= 0 ? rmax[j + 1] : -1;
		if (!(pl == rmin[j] && nl == rmin[j])) { FXT_C(rmin[j], j, 0, 0); FXT_C(rmin[j], j, 0, 1); }
		if (!(pr == rmax[j] && nr == rmax[j])) { FXT_C(rmax[j], j, 1, 0); FXT_C(rmax[j], j, 1, 1); } }
	for (int i = 0; i < nw; i++) if (cmax[i] >= 0) {
		int pt = i > 0 && cmax[i - 1] >= 0 ? cmin[i - 1] : -1, nt = i + 1 < nw && cmax[i + 1] >= 0 ? cmin[i + 1] : -1;
		int pb = i > 0 && cmax[i - 1] >= 0 ? cmax[i - 1] : -1, nb = i + 1 < nw && cmax[i + 1] >= 0 ? cmax[i + 1] : -1;
		if (!(pt == cmin[i] && nt == cmin[i])) { FXT_C(i, cmin[i], 0, 0); FXT_C(i, cmin[i], 1, 0); }
		if (!(pb == cmax[i] && nb == cmax[i])) { FXT_C(i, cmax[i], 0, 1); FXT_C(i, cmax[i], 1, 1); } }
#undef FXT_C
	int h = fxt_hull(P, n, Hh); FxtPt C[520];
	int m = h >= 3 && h <= 250 ? fxt_clip(Hh, h, C) : 0;
	if (m >= 3) { memcpy(P, C, sizeof(FxtPt) * m); m = fxt_hull(P, m, C); }
	if (m >= 3) m = fxt_reduce(C, m, FXT_K);
	if (m >= 3 && m <= FXT_K) { double a = fabs(fxt_area(C, m));
		if (a <= FXT_MAXAREA) { out->n = (unsigned char)m; out->area = (float)a; for (int k = 0; k < m; k++) { out->p[k][0] = (float)C[k].x; out->p[k][1] = (float)C[k].y; } } }
	free(cmin); free(rmin); free(P); free(Hh);
}

// ---- b81: holes. A full-screen layer whose middle is black (skills.h's vignettes: the low-HP haze, skill flashes, the camo
// and dive views) is drawn as the ring between its rectangle and a convex polygon inside which no texel can matter (the
// same reach rule as above), instead of the whole rectangle. fxt_hole finds the polygon: FXT_HK rays from the frame's
// centre out to the last safe point, their convex hull, shrunk towards the centre until no texel's reach (+ margin) touches
// it (checked exactly: no texel centre inside the polygon grown by the reach square); then fxt_ring triangulates the ring.
#define FXT_HK 16
#define FXT_RING_T (2 * FXT_HK + 4)   // triangles of a ring: two per hole edge + one per rectangle corner
typedef struct { unsigned char n; float area; float p[FXT_HK][2]; int nt; float t[FXT_RING_T][3][2]; } FxHole;   // n 0: no hole; t: the ring's triangles (frame units)
// is the polygon (texel units, about the centre c scaled by s) clear of every texel that can matter? The polygon grown by the
// reach square is scanned row by row over the mask (m: fxt_mask, bit add): any such texel centre strictly inside fails it.
static int fxt_hole_ok(const FxtPt *P, int n, FxtPt c, double s, const unsigned char *m, int W, int H, int add) {
	const double r = 1 + FXT_MARGIN; FxtPt G[4 * FXT_HK + 4], D[8 * FXT_HK + 8]; int g = 0;
	for (int k = 0; k < n; k++) { double x = c.x + (P[k].x - c.x) * s, y = c.y + (P[k].y - c.y) * s;
		for (int q = 0; q < 4; q++) G[g++] = (FxtPt){ x + ((q & 1) ? r : -r), y + ((q & 2) ? r : -r) }; }
	int nd = fxt_hull(G, g, D); if (nd < 3) return 1;
	double y0 = D[0].y, y1 = D[0].y; for (int k = 1; k < nd; k++) { if (D[k].y < y0) y0 = D[k].y; if (D[k].y > y1) y1 = D[k].y; }
	int j0 = (int)floor(y0 - 0.5), j1 = (int)ceil(y1 - 0.5); if (j0 < 0) j0 = 0; if (j1 > H - 1) j1 = H - 1;
	const unsigned char bit = (unsigned char)(1 << add);
	for (int j = j0; j <= j1; j++) {
		double y = j + 0.5, xl = 1e30, xr = -1e30;   // where the row's centre line crosses the polygon
		for (int k = 0; k < nd; k++) { FxtPt a = D[k], b = D[(k + 1) % nd];
			if ((a.y <= y && b.y >= y) || (a.y >= y && b.y <= y)) { double x = fabs(b.y - a.y) < 1e-12 ? (y == a.y ? a.x : 1e30) : a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
				if (fabs(b.y - a.y) < 1e-12) { if (y == a.y) { xl = fmin(xl, fmin(a.x, b.x)); xr = fmax(xr, fmax(a.x, b.x)); } } else { if (x < xl) xl = x; if (x > xr) xr = x; } } }
		if (xl > xr) continue;
		int i0 = (int)floor(xl - 0.5) , i1 = (int)ceil(xr - 0.5); if (i0 < 0) i0 = 0; if (i1 > W - 1) i1 = W - 1;
		const unsigned char *row = m + (size_t)j * W;
		for (int i = i0; i <= i1; i++) if ((row[i] & bit) && i + 0.5 > xl - 1e-9 && i + 0.5 < xr + 1e-9) return 0;   // (on the boundary counts as touching: conservative)
	}
	return 1;
}
static void fxt_ring(FxHole *h) {   // the ring between [0,1]^2 and the hole: per hole edge, the convex piece of its wedge, fanned
	h->nt = 0; if (h->n < 3) return;
	double cx = 0, cy = 0; for (int k = 0; k < h->n; k++) { cx += h->p[k][0]; cy += h->p[k][1]; } cx /= h->n; cy /= h->n;
	for (int k = 0; k < h->n; k++) {
		const float *a = h->p[k], *b = h->p[(k + 1) % h->n];
		double ta = atan2(a[1] - cy, a[0] - cx), tb = atan2(b[1] - cy, b[0] - cx); if (tb < ta) tb += 2 * M_PI;   // counter-clockwise from a to b
		FxtPt chain[8]; int nch = 0;
		for (int e = 0; e < 2; e++) {   // the rays through a and b hit the rectangle at
			double t = e ? tb : ta, dx = cos(t), dy = sin(t), best = 1e9;
			if (dx > 1e-12) best = fmin(best, (1 - cx) / dx); if (dx < -1e-12) best = fmin(best, -cx / dx);
			if (dy > 1e-12) best = fmin(best, (1 - cy) / dy); if (dy < -1e-12) best = fmin(best, -cy / dy);
			FxtPt o = { cx + dx * best, cy + dy * best };
			if (fabs(o.x) < 1e-6) o.x = 0; if (fabs(o.x - 1) < 1e-6) o.x = 1; if (fabs(o.y) < 1e-6) o.y = 0; if (fabs(o.y - 1) < 1e-6) o.y = 1;   // (exactly on the rectangle)
			if (!e) chain[nch++] = o;
			else {   // corners strictly between the two rays, in angle order, then o
				static const double CX[4] = { 1, 0, 0, 1 }, CY[4] = { 1, 1, 0, 0 }; double ca[4]; int ci[4], nc = 0;
				for (int q = 0; q < 4; q++) { double t2 = atan2(CY[q] - cy, CX[q] - cx); while (t2 < ta) t2 += 2 * M_PI; if (t2 > ta + 1e-12 && t2 < tb - 1e-12) { ca[nc] = t2; ci[nc++] = q; } }
				for (int i = 0; i < nc; i++) for (int j = i + 1; j < nc; j++) if (ca[j] < ca[i]) { double t3 = ca[i]; ca[i] = ca[j]; ca[j] = t3; int t4 = ci[i]; ci[i] = ci[j]; ci[j] = t4; }
				for (int i = 0; i < nc; i++) chain[nch++] = (FxtPt){ CX[ci[i]], CY[ci[i]] };
				chain[nch++] = o;
			}
		}
		// the piece (a, chain..., b) is convex: fan from a
		FxtPt poly[10]; int np = 0; poly[np++] = (FxtPt){ a[0], a[1] }; for (int i = 0; i < nch; i++) poly[np++] = chain[i]; poly[np++] = (FxtPt){ b[0], b[1] };
		for (int i = 1; i + 1 < np && h->nt < FXT_RING_T; i++) { float (*T)[2] = h->t[h->nt++];
			T[0][0] = (float)poly[0].x; T[0][1] = (float)poly[0].y; T[1][0] = (float)poly[i].x; T[1][1] = (float)poly[i].y; T[2][0] = (float)poly[i + 1].x; T[2][1] = (float)poly[i + 1].y; }
	}
}
// m: fxt_mask of the image (bit add); the frame's sample area in texel units [X0, X1] x [Y0, Y1]
static void fxt_hole(const unsigned char *m, int W, int H, double X0, double Y0, double X1, double Y1, int add, FxHole *out) {
	memset(out, 0, sizeof(*out));
	FxtPt c = { (X0 + X1) * 0.5, (Y0 + Y1) * 0.5 };
	const double r = 1 + FXT_MARGIN; const unsigned char bit = (unsigned char)(1 << add);
#define FXT_SAFE(x_, y_) ({ int ok_ = 1; for (int jj = (int)floor((y_) - r - 0.5); jj <= (int)ceil((y_) + r - 0.5) && ok_; jj++) for (int ii = (int)floor((x_) - r - 0.5); ii <= (int)ceil((x_) + r - 0.5) && ok_; ii++) { \
		int ci_ = ii < 0 ? 0 : ii > W - 1 ? W - 1 : ii, cj_ = jj < 0 ? 0 : jj > H - 1 ? H - 1 : jj; \
		if ((m[(size_t)cj_ * W + ci_] & bit) && fabs(ii + 0.5 - (x_)) < r && fabs(jj + 0.5 - (y_)) < r) ok_ = 0; } ok_; })
	if (!FXT_SAFE(c.x, c.y)) return;
	FxtPt P[FXT_HK], Hh[2 * FXT_HK + 2];
	for (int k = 0; k < FXT_HK; k++) {   // march out (quarter texels) to the last safe point
		double t = 2 * M_PI * (k + 0.5) / FXT_HK, dx = cos(t), dy = sin(t), d = 0, lim = 4 * (X1 - X0 + Y1 - Y0);
		while (d < lim) { double x = c.x + dx * (d + 0.25), y = c.y + dy * (d + 0.25); if (x < X0 || x > X1 || y < Y0 || y > Y1 || !FXT_SAFE(x, y)) break; d += 0.25; }
		P[k] = (FxtPt){ c.x + dx * d, c.y + dy * d };
	}
#undef FXT_SAFE
	int n = fxt_hull(P, FXT_HK, Hh);
	if (n < 3 || n > FXT_HK) return;
	double lo = 0, hi = 1;
	if (fxt_hole_ok(Hh, n, c, 1, m, W, H, add)) lo = 1;
	else for (int it = 0; it < 14; it++) { double mid = (lo + hi) * 0.5; if (fxt_hole_ok(Hh, n, c, mid, m, W, H, add)) lo = mid; else hi = mid; }
	FxtPt S[2 * FXT_HK + 2];
	for (int k = 0; k < n; k++) S[k] = (FxtPt){ (c.x + (Hh[k].x - c.x) * lo - X0) / (X1 - X0), (c.y + (Hh[k].y - c.y) * lo - Y0) / (Y1 - Y0) };
	double area = fabs(fxt_area(S, n));
	if (lo > 0 && area >= 0.04) { out->n = (unsigned char)n; out->area = (float)area; for (int k = 0; k < n; k++) { out->p[k][0] = (float)S[k].x; out->p[k][1] = (float)S[k].y; } fxt_ring(out); }
}
