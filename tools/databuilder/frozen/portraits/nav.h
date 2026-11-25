// AI navigation on a triangle navigation mesh (*.gnav: triangles + neighbour links + optional blocked-edge bits).
// Most maps use the game's own NaviMesh.Nmf; maps without a ground mesh get a generated grid mesh (tools/navgen.c).
// A* over triangles (binary heap), then a funnel pass over the shared edges gives a short corner-to-corner path.
#pragma once
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { float v[3][3]; int nb[3]; float c[3]; unsigned char blocked; } NavTri;   // nb[k]: neighbour across edge v[k] -> v[k+1]; blocked bit k: can't walk/climb out that way (ladders)
static NavTri *navt; static int nnav; static unsigned char *nav_swp;   // nav_swp[i]: triangle stored with v1/v2 swapped (tools)
// spatial hash: 500 cm buckets over the mesh bounds
#define NAV_BUCKET 500.0f
static float nav_x0, nav_y0; static int nav_bw, nav_bh; static int *nav_bstart, *nav_blist;

static void nav_free(void) {
	free(navt); free(nav_bstart); free(nav_blist); free(nav_swp); navt = NULL; nav_bstart = nav_blist = NULL; nav_swp = NULL; nnav = 0;
}
static int nav_load(const char *path) {
	nav_free();
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned n; if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GNAV", 4) || fread(&n, 4, 1, f) != 1) { fclose(f); return -2; }
	navt = calloc(n, sizeof(NavTri)); nnav = n; nav_swp = calloc(n, 1);
	for (unsigned i = 0; i < n; i++) {
		NavTri *t = &navt[i]; fread(t->v, 4, 9, f); fread(t->nb, 4, 3, f);
		// store counter-clockwise (seen from above) so portal sides are consistent
		float cr = (t->v[1][0] - t->v[0][0]) * (t->v[2][1] - t->v[0][1]) - (t->v[1][1] - t->v[0][1]) * (t->v[2][0] - t->v[0][0]);
		if (cr < 0) { // swap v1 and v2: edges 0 (v0v1) and 2 (v2v0) swap too
			float tmp[3]; memcpy(tmp, t->v[1], 12); memcpy(t->v[1], t->v[2], 12); memcpy(t->v[2], tmp, 12);
			int e0 = t->nb[0]; t->nb[0] = t->nb[2]; t->nb[2] = e0;
			t->blocked = 0x80;   // remember the swap for the flags below
		}
		for (int k = 0; k < 3; k++) t->c[k] = (t->v[0][k] + t->v[1][k] + t->v[2][k]) / 3;
	}
	// optional per-triangle blocked-edge bits, stored in file edge order: remap for swapped triangles
	int haveflags = fread(mg, 4, 1, f) == 1 && !memcmp(mg, "FLG1", 4);
	for (unsigned i = 0; i < n; i++) {
		int swapped = navt[i].blocked & 0x80, c = haveflags ? fgetc(f) : 0; if (c < 0) c = 0; nav_swp[i] = swapped != 0;
		if (swapped) c = (c & 2) | ((c & 1) << 2) | ((c & 4) >> 2);
		navt[i].blocked = c;
	}
	fclose(f);
	// buckets
	float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
	for (int i = 0; i < nnav; i++) for (int k = 0; k < 3; k++) { mnx = fminf(mnx, navt[i].v[k][0]); mxx = fmaxf(mxx, navt[i].v[k][0]); mny = fminf(mny, navt[i].v[k][1]); mxy = fmaxf(mxy, navt[i].v[k][1]); }
	nav_x0 = mnx; nav_y0 = mny; nav_bw = (int)((mxx - mnx) / NAV_BUCKET) + 1; nav_bh = (int)((mxy - mny) / NAV_BUCKET) + 1;
	int nb = nav_bw * nav_bh, *cnt = calloc(nb, sizeof(int));
	nav_bstart = malloc(sizeof(int) * (nb + 1));
	for (int pass = 0; pass < 2; pass++) {
		if (pass) { nav_bstart[0] = 0; for (int i = 0; i < nb; i++) nav_bstart[i + 1] = nav_bstart[i] + cnt[i]; nav_blist = malloc(sizeof(int) * (nav_bstart[nb] + 1)); memset(cnt, 0, sizeof(int) * nb); }
		for (int i = 0; i < nnav; i++) {
			const NavTri *t = &navt[i];
			int x0 = (int)((fminf(t->v[0][0], fminf(t->v[1][0], t->v[2][0])) - nav_x0) / NAV_BUCKET), x1 = (int)((fmaxf(t->v[0][0], fmaxf(t->v[1][0], t->v[2][0])) - nav_x0) / NAV_BUCKET);
			int y0 = (int)((fminf(t->v[0][1], fminf(t->v[1][1], t->v[2][1])) - nav_y0) / NAV_BUCKET), y1 = (int)((fmaxf(t->v[0][1], fmaxf(t->v[1][1], t->v[2][1])) - nav_y0) / NAV_BUCKET);
			for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { int b = y * nav_bw + x; if (pass) nav_blist[nav_bstart[b] + cnt[b]] = i; cnt[b]++; }
		}
	}
	free(cnt);
	return 0;
}
// height of the triangle's plane at (x, y)
static float nav_tri_z(const NavTri *t, float x, float y) {
	const float *a = t->v[0], *b = t->v[1], *c = t->v[2];
	float d = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); if (fabsf(d) < 1e-6f) return t->c[2];
	float u = ((x - a[0]) * (c[1] - a[1]) - (y - a[1]) * (c[0] - a[0])) / d, v = ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])) / d;
	return a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]);
}
static int nav_inside(const NavTri *t, float x, float y) {
	for (int k = 0; k < 3; k++) { const float *a = t->v[k], *b = t->v[(k + 1) % 3];
		if ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0]) < -0.01f) return 0; }
	return 1;
}
// triangle under a point (feet position); falls back to the nearest triangle in the surrounding buckets
static int nav_find(float x, float y, float z) {
	if (!nnav) return -1;
	int bx = (int)((x - nav_x0) / NAV_BUCKET), by = (int)((y - nav_y0) / NAV_BUCKET);
	int best = -1; float bd = 1e30f;
	if (bx >= 0 && by >= 0 && bx < nav_bw && by < nav_bh) {
		int b = by * nav_bw + bx;
		for (int k = nav_bstart[b]; k < nav_bstart[b + 1]; k++) { int i = nav_blist[k];
			if (!nav_inside(&navt[i], x, y)) continue;
			float dz = z - nav_tri_z(&navt[i], x, y); float cost = dz > -60 ? fabsf(dz) : -dz * 3;  // prefer the floor below the feet
			if (cost < bd) { bd = cost; best = i; } }
	}
	if (best >= 0 && bd < 400) return best;
	best = -1; bd = 1e30f;
	for (int r = 0; r < 64 && best < 0; r += 2) {   // widen the search ring until something turns up
		for (int yy = by - r - 1; yy <= by + r + 1; yy++) for (int xx = bx - r - 1; xx <= bx + r + 1; xx++) {
			if (xx < 0 || yy < 0 || xx >= nav_bw || yy >= nav_bh) continue;
			int b = yy * nav_bw + xx;
			for (int k = nav_bstart[b]; k < nav_bstart[b + 1]; k++) { int i = nav_blist[k];
				float dx = navt[i].c[0] - x, dy = navt[i].c[1] - y, dz = navt[i].c[2] - z, d = dx * dx + dy * dy + 4 * dz * dz;
				if (d < bd) { bd = d; best = i; } }
		}
	}
	return best;
}

static float tri2(const float *a, const float *b, const float *c) { return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); } // >0: c left of a->b
static int vclose(const float *a, const float *b) { return fabsf(a[0] - b[0]) < 0.5f && fabsf(a[1] - b[1]) < 0.5f; }

// path from s to g. Writes up to maxp points (x,y,z) excluding the start, ending at the goal. Returns count, 0 if none.
static int *nav_prev, *nav_heap, nav_cap; static float *nav_g, *nav_f; static unsigned *nav_stamp, nav_seq; static unsigned char *nav_closed;
static int *nav_corr; static float (*nav_L)[3], (*nav_R)[3];
static void heap_push(int *n, int v) { int i = (*n)++; nav_heap[i] = v; while (i > 0) { int p = (i - 1) / 2; if (nav_f[nav_heap[p]] <= nav_f[nav_heap[i]]) break; int t = nav_heap[p]; nav_heap[p] = nav_heap[i]; nav_heap[i] = t; i = p; } }
static int heap_pop(int *n) { int r = nav_heap[0]; nav_heap[0] = nav_heap[--(*n)]; int i = 0;
	for (;;) { int l = 2 * i + 1, rr = l + 1, m = i; if (l < *n && nav_f[nav_heap[l]] < nav_f[nav_heap[m]]) m = l; if (rr < *n && nav_f[nav_heap[rr]] < nav_f[nav_heap[m]]) m = rr; if (m == i) break; int t = nav_heap[m]; nav_heap[m] = nav_heap[i]; nav_heap[i] = t; i = m; }
	return r; }
static int nav_path(const float *s, const float *g, float (*out)[3], int maxp, float margin) {
	int a = nav_find(s[0], s[1], s[2]), b = nav_find(g[0], g[1], g[2]);
	if (a < 0 || b < 0) return 0;
	if (nav_cap < nnav) {
		nav_cap = nnav; free(nav_prev); free(nav_g); free(nav_f); free(nav_stamp); free(nav_closed); free(nav_heap); free(nav_corr); free(nav_L); free(nav_R);
		nav_prev = malloc(sizeof(int) * nnav); nav_g = malloc(sizeof(float) * nnav); nav_f = malloc(sizeof(float) * nnav); nav_stamp = calloc(nnav, sizeof(unsigned)); nav_closed = malloc(nnav);
		nav_heap = malloc(sizeof(int) * (nnav * 3 + 8)); nav_corr = malloc(sizeof(int) * nnav); nav_L = malloc(12 * (nnav + 2)); nav_R = malloc(12 * (nnav + 2)); nav_seq = 0;
	}
	nav_seq++;
	#define NAV_TOUCH(i) if (nav_stamp[i] != nav_seq) { nav_stamp[i] = nav_seq; nav_g[i] = 1e30f; nav_prev[i] = -1; nav_closed[i] = 0; }
	NAV_TOUCH(a); nav_g[a] = 0; nav_f[a] = 0; int hn = 0; heap_push(&hn, a); int found = a == b;
	float gx = navt[b].c[0], gy = navt[b].c[1];
	while (!found && hn > 0) {
		int cur = heap_pop(&hn); if (nav_closed[cur]) continue; nav_closed[cur] = 1;
		if (cur == b) { found = 1; break; }
		for (int k = 0; k < 3; k++) {
			int n = navt[cur].nb[k]; if (n < 0 || (navt[cur].blocked & (1 << k))) continue;
			NAV_TOUCH(n); if (nav_closed[n]) continue;
			float dx = navt[n].c[0] - navt[cur].c[0], dy = navt[n].c[1] - navt[cur].c[1], dz = navt[n].c[2] - navt[cur].c[2];
			float ng = nav_g[cur] + sqrtf(dx * dx + dy * dy + dz * dz);
			if (ng < nav_g[n]) { nav_g[n] = ng; nav_prev[n] = cur; float hx = navt[n].c[0] - gx, hy = navt[n].c[1] - gy; nav_f[n] = ng + sqrtf(hx * hx + hy * hy); if (hn < nnav * 3) heap_push(&hn, n); }
		}
	}
	#undef NAV_TOUCH
	if (!found) return 0;
	// corridor (start .. goal)
	int nc = 0; for (int t = b; t >= 0 && nc < nnav; t = nav_prev[t]) { nav_corr[nc++] = t; if (t == a) break; }
	for (int i = 0; i < nc / 2; i++) { int t = nav_corr[i]; nav_corr[i] = nav_corr[nc - 1 - i]; nav_corr[nc - 1 - i] = t; }
	// portals: left/right as seen walking from corr[i] into corr[i+1], shrunk by the margin
	float (*L)[3] = nav_L, (*R)[3] = nav_R; int np = 0;
	memcpy(L[np], s, 12); memcpy(R[np], s, 12); np++;
	for (int i = 0; i + 1 < nc; i++) {
		const NavTri *t = &navt[nav_corr[i]]; int k = 0; while (k < 3 && t->nb[k] != nav_corr[i + 1]) k++;
		if (k == 3) return 0;
		float r[3], l[3]; memcpy(r, t->v[k], 12); memcpy(l, t->v[(k + 1) % 3], 12);   // CCW triangle: leaving across v[k]->v[k+1], v[k] is on the right
		float dx = l[0] - r[0], dy = l[1] - r[1], len = sqrtf(dx * dx + dy * dy), m = margin;
		if (m > len * 0.45f) m = len * 0.45f;
		if (len > 1e-3f) { r[0] += dx / len * m; r[1] += dy / len * m; l[0] -= dx / len * m; l[1] -= dy / len * m; }
		memcpy(L[np], l, 12); memcpy(R[np], r, 12); np++;
	}
	memcpy(L[np], g, 12); memcpy(R[np], g, 12); np++;
	// funnel
	int n = 0; float apex[3], pl[3], pr[3]; memcpy(apex, s, 12); memcpy(pl, L[0], 12); memcpy(pr, R[0], 12);
	int ai = 0, li = 0, ri = 0;
	for (int i = 1; i < np && n < maxp; i++) {
		const float *l = L[i], *r = R[i];
		if (tri2(apex, pr, r) >= 0) {          // right side tightens (r is left of or on apex->pr)
			if (vclose(apex, pr) || tri2(apex, pl, r) < 0) { memcpy(pr, r, 12); ri = i; }
			else { memcpy(out[n++], pl, 12); memcpy(apex, pl, 12); ai = li; memcpy(pr, apex, 12); ri = ai; i = ai; continue; }
		}
		if (tri2(apex, pl, l) <= 0) {          // left side tightens
			if (vclose(apex, pl) || tri2(apex, pr, l) > 0) { memcpy(pl, l, 12); li = i; }
			else { memcpy(out[n++], pr, 12); memcpy(apex, pr, 12); ai = ri; memcpy(pl, apex, 12); li = ai; i = ai; continue; }
		}
	}
	if (n < maxp && (n == 0 || !vclose(out[n - 1], g))) memcpy(out[n++], g, 12);
	return n;
}
