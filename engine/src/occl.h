// occl.h (b60): occlusion culling on the CPU for the world's chunks and its whole meshes (not the bots: their outlines and vision passes show through walls).
// Why: the SGX shades every vertex of a draw before it can drop a triangle (world.h, render b45), so what is submitted
// sets the vertex and tiling work; the device draws 250-480 thousand indices a frame (b58-7), 130-440 thousand of them
// world geometry that the frustum test alone lets through - much of it behind walls. The CPU has the time (3-7 ms of a
// 16.7 ms frame used, three more cores nearly idle).
// How: a small depth picture (OC_W x OC_H) of the map's big opaque triangles ("occluders": 1 m^2 and more, 7-25 thousand
// a map, 83-99 % of the opaque area; filed by 10 m cell at load) is drawn each frame, nearest cells first; a box is hidden
// when every pixel it may touch has an occluder in front of the box's nearest point.
// Built to err on the side of drawing (a thing that can be seen is not dropped):
//   - the occluders are drawn at twice the picture's resolution (OC_HW x OC_HH samples), each sample taking the depth of
//     the triangle's FARTHEST point over the sample's square (1/w is linear on screen: centre value minus half the
//     gradient) and keeping the nearest such depth; triangles sharing an edge leave no gap between them;
//   - a pixel of the picture is the FARTHEST of its four samples and the ring of samples around them (4 x 4): a pixel
//     counts as covered only when the cover reaches a sample beyond it on every side, so a wall's outline, and the step
//     in depth where a near wall ends in front of a far one, never hide what shows beside them. (First tried: only
//     pixels a triangle covers wholly. Every shared edge then leaves a seam of open pixels: walls of 1 m2 triangles hid
//     next to nothing.)
//   - a box is tested with its NEAREST corner over the whole pixel rectangle that contains its projection, and counts as
//     visible when any corner is behind the near plane;
//   - only front faces occlude (as the GPU culls back faces; two-sided meshes occlude both ways), and an occluder is
//     clipped at a near plane a little beyond the GPU's;
//   - blended, alpha-tested and no-depth-write meshes never occlude; the workers race on the samples without a lock: a
//     lost update leaves the farther value (less is hidden, never more).
// What it cannot see: a gap between two walls thinner than a sample (5 screen pixels). tools/occl/occl_host.c checks it
// against a 960x544 picture of every triangle on every map (200 views each, 3400 in all, b60): it dropped something the
// truth shows 58 times (at most 56 pixels of one chunk in one view): thin lines along seams of the map's own geometry, far geometry
// seen through the crack where a wall meets a floor (the ones looked at: 1-2 pixels high, up to 75 long; 5 of the 58
// reach a 2x2 block) - what fills the crack changes. It counts the saving too: world indices after the frustum test -29 % (Port) to -71 %
// (Trainstation), no change in the two small VR rooms. The picture size hardly matters (160x96, 128x72, 96x56: the same
// saving within 1 %), so it is the small one.
// Cost: the picture is drawn on all cores (oc_par = jobs.h); oc_clock times it and oc_far (how far occluders are taken
// from) shrinks while the average is over OC_BUDGET_US, grows back under it; over twice the budget at the shortest range it
// turns itself off for ~10 s (a slow stretch - a load, the card - must not cost it the whole map), then tries again. Switch: ux0:data/gitsfa/noocc (off).
#pragma once
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif
#ifndef OC_W
#define OC_W 96
#define OC_H 56
#endif
#define OC_HW (OC_W * 2)
#define OC_HH (OC_H * 2)
#define OC_TW (OC_W / 8)
#define OC_TH (OC_H / 8)
#define OC_CELL 1000.f        // cm: occluders are filed by the 10 m cell of their centre
#define OC_MIN_AREA 10000.f   // cm^2: triangles of 1 m^2 and more
#define OC_MAX_TRI 32768
#define OC_FAR_MAX 9000.f     // cm: cells farther than oc_far are not drawn (oc_far: OC_FAR_MIN .. OC_FAR_MAX)
#define OC_FAR_MIN 2500.f
#define OC_NEAR 12.f          // cm: occluders are clipped here (the world's near plane is 10)
#define OC_MAX_CELLS 512      // cells drawn a frame at most (nearest first)
#define OC_ROUND1 16          // the nearest cells are drawn first; the others are tested against their picture
#define OC_BUDGET_US 2400     // the picture's time a frame (oc_clock) that oc_far is steered to stay under (b61: 1800 -> 2400: device
                              // b60 took 1.9-3.0 ms at the shortest range on every big map, CPU busy 6-10 ms of 16.7)
#define OC_RETRY_FRAMES 600   // over twice the budget at the shortest range: off for this many frames (~10 s), then tried again
typedef struct { float p[3][3]; int two; float n[3], d, r2; } OcTri;   // its plane (n . x = d, n not unit: front where n . x > d), its longest edge squared
typedef struct { int first, count; float mn[3], mx[3]; } OcCell;
typedef struct { float d2; int i; } OcSel;
static OcTri *oc_tri; static int oc_ntri, oc_cap; static OcCell *oc_cell; static int oc_ncell; static OcSel *oc_sel; static int oc_nsel;
static float oc_hr[OC_HH][OC_HW];                         // samples: 1/w of the nearest occluder's farthest point there (0: nothing)
static float oc_buf[OC_H][OC_W], oc_tile[OC_TH][OC_TW];   // the picture: the least of each pixel's 4 x 4 samples; the least of each 8 x 8 block of pixels
static float oc_m[16], oc_cam[3], oc_min_size = 0.02f, oc_far = OC_FAR_MAX; static int oc_on = 1, oc_ready, oc_gave_up, oc_retry, oc_n_gave_up;
static long oc_st_tris, oc_st_tests, oc_st_hidden, oc_st_frames, oc_st_cells, oc_st_us; static unsigned oc_avg_us, oc_worst_us;   // counters for the log
static void (*oc_par)(void (*fn)(void *ctx, int i), void *ctx, int n);   // run fn(ctx, i) for i in 0..n-1 on the workers (main.c: jobs.h); none: here
static unsigned long long (*oc_clock)(void);                             // microseconds (main.c); none: no timing, no steering
static int oc_job0, oc_skip_test; static unsigned char *oc_drawn;        // (the workers' jobs: the round's first index; cells are tested against the picture so far)
static int oc_rect_hidden(int ix0, int iy0, int ix1, int iy1, float qn);
#ifdef OC_COUNT
static long oc_cnt_fill, oc_cnt_box, oc_cnt_in;   // (host study: triangles filled, samples in their rectangles, samples inside)
#endif

static unsigned oc_st_late;   // b61: cells left out of pictures for the time limit (oc_cell_job)
static void oc_reset(void) { free(oc_tri); free(oc_cell); free(oc_sel); free(oc_drawn); oc_tri = NULL; oc_cell = NULL; oc_sel = NULL; oc_drawn = NULL; oc_ntri = oc_cap = oc_ncell = 0; oc_ready = 0; oc_gave_up = 0; oc_retry = 0; oc_n_gave_up = 0; oc_far = OC_FAR_MAX; oc_avg_us = oc_worst_us = 0; oc_st_late = 0; }
// at load: one opaque, depth-writing mesh's triangles (positions at v + index * stride floats)
static void oc_add_mesh(const float *v, int stride, const unsigned short *ix, int ni, int two_sided) {
	for (int t = 0; t + 2 < ni; t += 3) {
		const float *a = v + ix[t] * stride, *b = v + ix[t + 1] * stride, *c = v + ix[t + 2] * stride;
		float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
		if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] < 4 * OC_MIN_AREA * OC_MIN_AREA) continue;
		if (oc_ntri >= OC_MAX_TRI) return;
		if (oc_ntri >= oc_cap) { int nc = oc_cap ? oc_cap * 2 : 4096; OcTri *nt = realloc(oc_tri, sizeof(OcTri) * nc); if (!nt) return; oc_tri = nt; oc_cap = nc; }
		OcTri *o = &oc_tri[oc_ntri++]; memcpy(o->p[0], a, 12); memcpy(o->p[1], b, 12); memcpy(o->p[2], c, 12); o->two = two_sided;
		memcpy(o->n, n, 12); o->d = n[0] * a[0] + n[1] * a[1] + n[2] * a[2];
		float e3[3] = { c[0] - b[0], c[1] - b[1], c[2] - b[2] }, l1 = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2], l2 = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2], l3 = e3[0] * e3[0] + e3[1] * e3[1] + e3[2] * e3[2];
		o->r2 = l1 > l2 ? (l1 > l3 ? l1 : l3) : (l2 > l3 ? l2 : l3);
	}
}
static int oc_key(const OcTri *t) {
	float cx = (t->p[0][0] + t->p[1][0] + t->p[2][0]) * (1 / 3.f), cy = (t->p[0][1] + t->p[1][1] + t->p[2][1]) * (1 / 3.f);
	return (((int)floorf(cx / OC_CELL) + 1024) & 2047) << 11 | (((int)floorf(cy / OC_CELL) + 1024) & 2047);
}
static int oc_tri_cmp(const void *a, const void *b) { int ka = oc_key(a), kb = oc_key(b); return ka < kb ? -1 : ka > kb; }
static int oc_sel_cmp(const void *a, const void *b) { float x = ((const OcSel *)a)->d2, y = ((const OcSel *)b)->d2; return x < y ? -1 : x > y; }
// at load, after the last mesh: file the occluders by cell
static void oc_finish(void) {
	free(oc_cell); free(oc_sel); free(oc_drawn); oc_cell = NULL; oc_sel = NULL; oc_drawn = NULL; oc_ncell = 0; oc_ready = 0; if (!oc_ntri) return;
	qsort(oc_tri, oc_ntri, sizeof(OcTri), oc_tri_cmp);
	int nc = 1; for (int i = 1; i < oc_ntri; i++) nc += oc_key(&oc_tri[i]) != oc_key(&oc_tri[i - 1]);
	oc_cell = malloc(sizeof(OcCell) * nc); oc_sel = malloc(sizeof(OcSel) * nc); oc_drawn = malloc(nc);
	if (!oc_cell || !oc_sel || !oc_drawn) { free(oc_cell); free(oc_sel); free(oc_drawn); oc_cell = NULL; oc_sel = NULL; oc_drawn = NULL; oc_ntri = 0; return; }
	OcCell *c = NULL;
	for (int i = 0; i < oc_ntri; i++) {
		if (!i || oc_key(&oc_tri[i]) != oc_key(&oc_tri[i - 1])) { c = &oc_cell[oc_ncell++]; c->first = i; c->count = 0; for (int k = 0; k < 3; k++) { c->mn[k] = 1e30f; c->mx[k] = -1e30f; } }
		c->count++;
		for (int j = 0; j < 3; j++) for (int k = 0; k < 3; k++) { float x = oc_tri[i].p[j][k]; if (x < c->mn[k]) c->mn[k] = x; if (x > c->mx[k]) c->mx[k] = x; }
	}
}
// clip space: x, y, w of a world point
static inline void oc_xf(const float *p, float *o) {
	const float *m = oc_m;
	o[0] = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]; o[1] = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]; o[2] = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
}
// one screen triangle (sample coordinates x, y; q = 1/w), any winding: the samples whose centre it covers
static void oc_fill(const float *A, const float *B, const float *C) {
	float area = (B[0] - A[0]) * (C[1] - A[1]) - (C[0] - A[0]) * (B[1] - A[1]);
	if (area < 0) { const float *t = B; B = C; C = t; area = -area; }
	if (area < 0.5f) return;
	float fx0 = fminf(A[0], fminf(B[0], C[0])), fx1 = fmaxf(A[0], fmaxf(B[0], C[0])), fy0 = fminf(A[1], fminf(B[1], C[1])), fy1 = fmaxf(A[1], fmaxf(B[1], C[1]));
	if (fx1 < 0.5f || fy1 < 0.5f || fx0 > OC_HW - 0.5f || fy0 > OC_HH - 0.5f) return;
	int x0 = fx0 < 0.5f ? 0 : (int)(fx0 - 0.5f), x1 = fx1 > OC_HW - 0.5f ? OC_HW - 1 : (int)(fx1 + 0.5f), y0 = fy0 < 0.5f ? 0 : (int)(fy0 - 0.5f), y1 = fy1 > OC_HH - 0.5f ? OC_HH - 1 : (int)(fy1 + 0.5f);
	if (x1 > OC_HW - 1) x1 = OC_HW - 1; if (y1 > OC_HH - 1) y1 = OC_HH - 1;
	// edge i: E(x, y) = a x + b y + c >= 0 inside (area > 0 after the swap). A sample on a shared edge may be claimed by
	// both neighbours (e: the two sides' rounding differs), never by neither.
	float a0 = A[1] - B[1], b0 = B[0] - A[0], a1 = B[1] - C[1], b1 = C[0] - B[0], a2 = C[1] - A[1], b2 = A[0] - C[0];
	float e = (fabsf(a0) + fabsf(b0) + fabsf(a1) + fabsf(b1) + fabsf(a2) + fabsf(b2)) * 2e-4f + 1e-5f;
	float c0 = -(a0 * A[0] + b0 * A[1]) + e, c1 = -(a1 * B[0] + b1 * B[1]) + e, c2 = -(a2 * C[0] + b2 * C[1]) + e;
	// 1/w over the screen: q = q(A) + gx (x - Ax) + gy (y - Ay); the farthest over a sample's square: its centre's minus half the gradient
	float ia = 1 / area, gx = ((B[2] - A[2]) * (C[1] - A[1]) - (C[2] - A[2]) * (B[1] - A[1])) * ia, gy = ((C[2] - A[2]) * (B[0] - A[0]) - (B[2] - A[2]) * (C[0] - A[0])) * ia;
	float qm = fminf(A[2], fminf(B[2], C[2])) * 0.9995f, qb = A[2] - gx * A[0] - gy * A[1] - (fabsf(gx) + fabsf(gy)) * 0.5f;
#ifdef OC_COUNT
	oc_cnt_fill++; oc_cnt_box += (long)(x1 - x0 + 1) * (y1 - y0 + 1);
#endif
	// row by row: where the three edges allow x (a x + k >= 0), no test per sample (a triangle across the screen has a
	// rectangle of 20 thousand samples and covers a thousand)
	float r0 = a0 != 0 ? -1 / a0 : 0, r1 = a1 != 0 ? -1 / a1 : 0, r2 = a2 != 0 ? -1 / a2 : 0, gq = gx * 0.9995f;
	for (int y = y0; y <= y1; y++) {
		float py = y + 0.5f, k0 = b0 * py + c0, k1 = b1 * py + c1, k2 = b2 * py + c2, lo = x0 + 0.5f, hi = x1 + 0.5f;
		if (a0 > 0) { float t = k0 * r0; if (t > lo) lo = t; } else if (a0 < 0) { float t = k0 * r0; if (t < hi) hi = t; } else if (k0 < 0) continue;
		if (a1 > 0) { float t = k1 * r1; if (t > lo) lo = t; } else if (a1 < 0) { float t = k1 * r1; if (t < hi) hi = t; } else if (k1 < 0) continue;
		if (a2 > 0) { float t = k2 * r2; if (t > lo) lo = t; } else if (a2 < 0) { float t = k2 * r2; if (t < hi) hi = t; } else if (k2 < 0) continue;
		if (hi < lo) continue;
		int xa = (int)(lo + 0.4999f), xb = (int)(hi - 0.5f); if (xa < x0) xa = x0; if (xb > x1) xb = x1;   // sample centres xa + 0.5 .. xb + 0.5 within [lo, hi]
		float q = (qb + gx * (xa + 0.5f) + gy * py) * 0.9995f, *row = oc_hr[y];
		for (int x = xa; x <= xb; x++, q += gq) {
			float qq = q < qm ? qm : q;   // (never beyond the triangle's own farthest corner; 0.9995: rounding, a little farther)
#ifdef OC_COUNT
			oc_cnt_in++;
#endif
			if (qq > row[x]) row[x] = qq;
		}
	}
}
static void oc_draw_tri(const OcTri *t) {
	// cheap ways out first: facing away (its plane against the eye: the GPU culls it, it hides nothing), or too small in
	// the picture to cover a pixel's samples (its longest edge under oc_min_size of its distance)
	float dx = oc_cam[0] - t->p[0][0], dy = oc_cam[1] - t->p[0][1], dz = oc_cam[2] - t->p[0][2];
	if (!t->two && t->n[0] * dx + t->n[1] * dy + t->n[2] * dz <= 0) return;
	if (t->r2 < oc_min_size * oc_min_size * (dx * dx + dy * dy + dz * dz)) return;
	float c[3][3]; oc_xf(t->p[0], c[0]); oc_xf(t->p[1], c[1]); oc_xf(t->p[2], c[2]);
	if (c[0][2] < OC_NEAR && c[1][2] < OC_NEAR && c[2][2] < OC_NEAR) return;
	float v[5][3]; int n = 0;   // clipped at w = OC_NEAR, then to samples
	for (int i = 0; i < 3; i++) {
		const float *a = c[i], *b = c[(i + 1) % 3]; int ia = a[2] >= OC_NEAR, ib = b[2] >= OC_NEAR;
		if (ia) { memcpy(v[n++], a, 12); }
		if (ia != ib) { float k = (OC_NEAR - a[2]) / (b[2] - a[2]); v[n][0] = a[0] + (b[0] - a[0]) * k; v[n][1] = a[1] + (b[1] - a[1]) * k; v[n][2] = OC_NEAR; n++; }
	}
	if (n < 3) return;
	for (int i = 0; i < n; i++) { float q = 1 / v[i][2]; v[i][0] = (v[i][0] * q * 0.5f + 0.5f) * OC_HW; v[i][1] = (0.5f - v[i][1] * q * 0.5f) * OC_HH; v[i][2] = q; }
	float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, qx = 0;
	for (int i = 0; i < n; i++) { if (v[i][0] < x0) x0 = v[i][0]; if (v[i][0] > x1) x1 = v[i][0]; if (v[i][1] < y0) y0 = v[i][1]; if (v[i][1] > y1) y1 = v[i][1]; if (v[i][2] > qx) qx = v[i][2]; }
	if (x1 < 0 || y1 < 0 || x0 > OC_HW || y0 > OC_HH) return;   // off the picture
	if (oc_skip_test) {   // behind what the nearer round drew, over its whole rectangle: adds nothing
		int ix0 = x0 < 0 ? 0 : (int)(x0 * 0.5f), ix1 = x1 >= OC_HW ? OC_W - 1 : (int)(x1 * 0.5f), iy0 = y0 < 0 ? 0 : (int)(y0 * 0.5f), iy1 = y1 >= OC_HH ? OC_H - 1 : (int)(y1 * 0.5f);
		if (oc_rect_hidden(ix0, iy0, ix1, iy1, qx)) return;
	}
	for (int i = 1; i + 1 < n; i++) oc_fill(v[0], v[i], v[i + 1]);
}
// 1: every pixel of the rectangle (inclusive, inside the picture) has an occluder nearer than 1/w = qn
static int oc_rect_hidden(int ix0, int iy0, int ix1, int iy1, float qn) {
	for (int ty = iy0 >> 3; ty <= iy1 >> 3; ty++) for (int tx = ix0 >> 3; tx <= ix1 >> 3; tx++) {
		if (oc_tile[ty][tx] > qn) continue;   // the whole block is covered nearer
		int ax = tx << 3, ay = ty << 3, bx = ax + 7, by = ay + 7; if (ax < ix0) ax = ix0; if (bx > ix1) bx = ix1; if (ay < iy0) ay = iy0; if (by > iy1) by = iy1;
		for (int y = ay; y <= by; y++) { const float *row = oc_buf[y]; for (int x = ax; x <= bx; x++) if (row[x] <= qn) return 0; }
	}
	return 1;
}
// 1: the box may be seen (or the picture is not ready); 0: hidden behind occluders. Reads the picture only: any thread.
static int oc_box_visible(const float *mn, const float *mx) {
	if (!oc_ready) return 1;
	// the 8 corners: the least corner's clip coordinates plus the box's three edges
	float c0[3], ex[3], ey[3], ez[3]; const float *m = oc_m; oc_xf(mn, c0);
	{ float sx = mx[0] - mn[0], sy = mx[1] - mn[1], sz = mx[2] - mn[2];
	  ex[0] = m[0] * sx; ex[1] = m[1] * sx; ex[2] = m[3] * sx; ey[0] = m[4] * sy; ey[1] = m[5] * sy; ey[2] = m[7] * sy; ez[0] = m[8] * sz; ez[1] = m[9] * sz; ez[2] = m[11] * sz; }
	float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, wn = 1e30f;
	for (int k = 0; k < 8; k++) {
		float c[3] = { c0[0], c0[1], c0[2] };
		if (k & 1) { c[0] += ex[0]; c[1] += ex[1]; c[2] += ex[2]; } if (k & 2) { c[0] += ey[0]; c[1] += ey[1]; c[2] += ey[2]; } if (k & 4) { c[0] += ez[0]; c[1] += ez[1]; c[2] += ez[2]; }
		if (c[2] < OC_NEAR) return 1;
		float q = 1 / c[2], sx = (c[0] * q * 0.5f + 0.5f) * OC_W, sy = (0.5f - c[1] * q * 0.5f) * OC_H;
		if (sx < x0) x0 = sx; if (sx > x1) x1 = sx; if (sy < y0) y0 = sy; if (sy > y1) y1 = sy; if (c[2] < wn) wn = c[2];
	}
	x0 -= 0.01f; y0 -= 0.01f; x1 += 0.01f; y1 += 0.01f;   // (the corners' rounding)
	if (x1 < 0 || y1 < 0 || x0 >= OC_W || y0 >= OC_H) return 1;   // off the picture: the frustum test's business
	int ix0 = x0 < 0 ? 0 : (int)x0, ix1 = x1 >= OC_W ? OC_W - 1 : (int)x1, iy0 = y0 < 0 ? 0 : (int)y0, iy1 = y1 >= OC_H ? OC_H - 1 : (int)y1;
	return !oc_rect_hidden(ix0, iy0, ix1, iy1, 1.002f / wn);   // hidden only where an occluder's farthest point is nearer than the box's nearest
}
static int oc_frustum(const float *mn, const float *mx) {   // (as world.h w_visible)
	const float *mvp = oc_m;
	for (int p = 0; p < 6; p++) {
		int r = p >> 1; float s = (p & 1) ? -1.f : 1.f, pl[4];
		for (int i = 0; i < 4; i++) pl[i] = mvp[i * 4 + 3] + s * mvp[i * 4 + r];
		float x = pl[0] >= 0 ? mx[0] : mn[0], y = pl[1] >= 0 ? mx[1] : mn[1], z = pl[2] >= 0 ? mx[2] : mn[2];
		if (pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0) return 0;
	}
	return 1;
}
static void oc_down_job(void *ctx, int y) {   // one row of the picture: each pixel the least of its 4 x 4 samples (its own 2 x 2 and the ring around)
	(void)ctx; int ya = y * 2 - 1, yb = y * 2 + 2; if (ya < 0) ya = 0; if (yb > OC_HH - 1) yb = OC_HH - 1;
	float col[OC_HW + 4];   // the least of each sample column over the rows ya..yb
	int x = 0;
#ifdef __ARM_NEON
	for (; x + 4 <= OC_HW; x += 4) { float32x4_t m = vld1q_f32(&oc_hr[ya][x]); for (int k = ya + 1; k <= yb; k++) m = vminq_f32(m, vld1q_f32(&oc_hr[k][x])); vst1q_f32(&col[x], m); }
#endif
	for (; x < OC_HW; x++) { float m = oc_hr[ya][x]; for (int k = ya + 1; k <= yb; k++) if (oc_hr[k][x] < m) m = oc_hr[k][x]; col[x] = m; }
	float *row = oc_buf[y];
	{ float m = col[0]; if (col[1] < m) m = col[1]; if (col[2] < m) m = col[2]; row[0] = m; }
	for (x = 1; x < OC_W - 1; x++) { const float *c = col + x * 2 - 1; float a = c[0] < c[1] ? c[0] : c[1], b = c[2] < c[3] ? c[2] : c[3]; row[x] = a < b ? a : b; }
	{ const float *c = col + OC_HW - 3; float m = c[0]; if (c[1] < m) m = c[1]; if (c[2] < m) m = c[2]; row[OC_W - 1] = m; }
}
static void oc_tiles(void) {
	for (int ty = 0; ty < OC_TH; ty++) for (int tx = 0; tx < OC_TW; tx++) {
		float m = 1e30f; for (int y = 0; y < 8; y++) { const float *row = oc_buf[ty * 8 + y] + tx * 8; for (int x = 0; x < 8; x++) if (row[x] < m) m = row[x]; }
		oc_tile[ty][tx] = m; }
}
static unsigned long long oc_t_end;   // b61: when this frame's picture must stop taking cells (0: no limit)
static void oc_cell_job(void *ctx, int i) {
	(void)ctx; i += oc_job0; const OcCell *c = &oc_cell[oc_sel[i].i];
	if (oc_t_end && oc_clock && oc_clock() > oc_t_end) { oc_drawn[i] = 0; __sync_fetch_and_add(&oc_st_late, 1); return; }   // late: fewer occluders (hides less, never wrongly)
	if (oc_skip_test && !oc_box_visible(c->mn, c->mx)) { oc_drawn[i] = 0; return; }   // the whole cell is behind what the nearest ones drew
	oc_drawn[i] = 1;
	for (int k = 0; k < c->count; k++) oc_draw_tri(&oc_tri[c->first + k]);
}
static void oc_run(void (*fn)(void *, int), int n) { if (n <= 0) return; if (oc_par) oc_par(fn, NULL, n); else for (int i = 0; i < n; i++) fn(NULL, i); }
// each frame, before the world is drawn: the occluders near the camera into the picture
static void oc_begin(const float *MVP, const float *cam) {
	oc_ready = 0; if (!oc_on || !oc_ncell) return;
	if (oc_gave_up) { if (--oc_retry > 0) return; oc_gave_up = 0; oc_avg_us = 0; oc_far = OC_FAR_MIN; }   // (off for a while: then again, from the shortest range)
	unsigned long long t0 = oc_clock ? oc_clock() : 0; oc_t_end = t0 ? t0 + OC_BUDGET_US * 3 : 0;   // b61: a cell starts only within 3x the budget
	memcpy(oc_m, MVP, 64); memcpy(oc_cam, cam, 12); memset(oc_hr, 0, sizeof(oc_hr));
#ifndef OC_CALLER_EYE   // (tools/occl: -DOC_CALLER_EYE takes the caller's, to compare)
	{	// the eye of THIS projection (where clip x = y = w = 0), so that "facing away" is exactly what the GPU culls whatever
		// position the caller passes; the caller's is kept when the matrix has no such point (not a perspective)
		const float *m = MVP; float a0 = m[0], a1 = m[4], a2 = m[8], b0 = m[1], b1 = m[5], b2 = m[9], c0 = m[3], c1 = m[7], c2 = m[11], r0 = -m[12], r1 = -m[13], r2 = -m[15];
		float det = a0 * (b1 * c2 - b2 * c1) - a1 * (b0 * c2 - b2 * c0) + a2 * (b0 * c1 - b1 * c0);
		if (fabsf(det) > 1e-9f) { float id = 1 / det;
			oc_cam[0] = (r0 * (b1 * c2 - b2 * c1) - a1 * (r1 * c2 - b2 * r2) + a2 * (r1 * c1 - b1 * r2)) * id;
			oc_cam[1] = (a0 * (r1 * c2 - b2 * r2) - r0 * (b0 * c2 - b2 * c0) + a2 * (b0 * r2 - r1 * c0)) * id;
			oc_cam[2] = (a0 * (b1 * r2 - r1 * c1) - a1 * (b0 * r2 - r1 * c0) + r0 * (b0 * c1 - b1 * c0)) * id; } }
#endif
	cam = oc_cam;
	// the cells in the frustum within oc_far, nearest first
	oc_nsel = 0;
	for (int i = 0; i < oc_ncell; i++) {
		const OcCell *c = &oc_cell[i]; float d2 = 0;
		for (int k = 0; k < 3; k++) { float d = cam[k] < c->mn[k] ? c->mn[k] - cam[k] : cam[k] > c->mx[k] ? cam[k] - c->mx[k] : 0; d2 += d * d; }
		if (d2 > oc_far * oc_far || !oc_frustum(c->mn, c->mx)) continue;
		oc_sel[oc_nsel].d2 = d2; oc_sel[oc_nsel].i = i; oc_nsel++;
	}
	if (oc_nsel > 1) qsort(oc_sel, oc_nsel, sizeof(OcSel), oc_sel_cmp);
	if (oc_nsel > OC_MAX_CELLS) oc_nsel = OC_MAX_CELLS;
	// two rounds: the nearest cells, then the picture so far, then the rest - a cell or a triangle of the second round that
	// the first one's picture hides is not drawn (in a corridor the near walls hide nearly everything behind them)
	int a = oc_nsel < OC_ROUND1 ? oc_nsel : OC_ROUND1;
	oc_job0 = 0; oc_skip_test = 0; oc_run(oc_cell_job, a);
	if (a < oc_nsel) {
		oc_run(oc_down_job, OC_H); oc_tiles(); oc_ready = 1;
		oc_job0 = a; oc_skip_test = 1; oc_run(oc_cell_job, oc_nsel - a); oc_job0 = 0; oc_skip_test = 0; oc_ready = 0;
	}
	oc_run(oc_down_job, OC_H); oc_tiles();
	oc_ready = 1; oc_st_frames++;
	for (int i = 0; i < oc_nsel; i++) if (oc_drawn[i]) { oc_st_tris += oc_cell[oc_sel[i].i].count; oc_st_cells++; }
	if (oc_clock) {   // steer the range by the time it took (smoothed over ~16 frames)
		unsigned us = (unsigned)(oc_clock() - t0); oc_st_us += us; if (us > oc_worst_us) oc_worst_us = us;
		oc_avg_us = oc_avg_us ? (oc_avg_us * 15 + us) / 16 : us;
		if (oc_avg_us > OC_BUDGET_US) { if (oc_far > OC_FAR_MIN) { oc_far *= 0.93f; if (oc_far < OC_FAR_MIN) oc_far = OC_FAR_MIN; } else if (oc_avg_us > OC_BUDGET_US * 2) { oc_gave_up = 1; oc_retry = OC_RETRY_FRAMES; oc_n_gave_up++; } }
		else if (oc_avg_us < OC_BUDGET_US * 6 / 10 && oc_far < OC_FAR_MAX) { oc_far *= 1.03f; if (oc_far > OC_FAR_MAX) oc_far = OC_FAR_MAX; }
	}
}
// a box test with the counters (main thread: bots.h)
static __attribute__((unused)) int oc_test(const float *mn, const float *mx) { if (!oc_ready) return 1; int v = oc_box_visible(mn, mx); oc_st_tests++; oc_st_hidden += !v; return v; }
