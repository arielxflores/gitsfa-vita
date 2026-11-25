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
// perf (b35): bucket counts summed over rectangles (nav_find's widening ring tests a whole square in O(1) instead of scanning it
// again for every ring), and the mesh's connected pieces (edges taken both ways: a goal in another piece has no path, so the
// search can stop at once instead of exhausting every triangle it can reach)
static int *nav_bsum, *nav_comp;
// ... and which triangle can reach which, the way the search walks (links out of a triangle, blocked edges one-way): the strongly
// connected pieces (nav_scc) and, per piece, the bit set of the pieces reachable from it (nav_reach, nav_nscc^2 bits; built
// when there are at most NAV_MAXSCC pieces). A goal that can't be reached is answered at once: before, the search expanded
// every triangle it could reach first (up to ~6700 on RuinedNight), and a bot's fallback tries could do that 5 times in a frame
#define NAV_MAXSCC 2048
static int *nav_scc, nav_nscc; static unsigned *nav_reach; static int nav_reach_w;

static void nav_free(void) {
	free(navt); free(nav_bstart); free(nav_blist); free(nav_swp); free(nav_bsum); free(nav_comp); navt = NULL; nav_bstart = nav_blist = NULL; nav_swp = NULL; nav_bsum = nav_comp = NULL; nnav = 0;
	free(nav_scc); free(nav_reach); nav_scc = NULL; nav_reach = NULL; nav_nscc = 0;
}
static int nav_bcount(int x0, int y0, int x1, int y1) {   // triangles listed in the buckets [x0, x1] x [y0, y1] (clipped to the grid)
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= nav_bw) x1 = nav_bw - 1; if (y1 >= nav_bh) y1 = nav_bh - 1; if (x0 > x1 || y0 > y1) return 0;
	int W = nav_bw + 1; return nav_bsum[(y1 + 1) * W + x1 + 1] - nav_bsum[y0 * W + x1 + 1] - nav_bsum[(y1 + 1) * W + x0] + nav_bsum[y0 * W + x0];
}
static void nav_index(void) {
	int W = nav_bw + 1; nav_bsum = calloc((size_t)W * (nav_bh + 1), sizeof(int));
	if (nav_bsum) for (int y = 0; y < nav_bh; y++) for (int x = 0; x < nav_bw; x++) { int b = y * nav_bw + x;
		nav_bsum[(y + 1) * W + x + 1] = nav_bstart[b + 1] - nav_bstart[b] + nav_bsum[y * W + x + 1] + nav_bsum[(y + 1) * W + x] - nav_bsum[y * W + x]; }
	nav_comp = malloc(sizeof(int) * (nnav + 1)); int *stk = malloc(sizeof(int) * (nnav + 1));
	if (!nav_comp || !stk) { free(nav_comp); nav_comp = NULL; free(stk); return; }
	for (int i = 0; i < nnav; i++) nav_comp[i] = -1;
	int nc = 0;
	for (int i = 0; i < nnav; i++) { if (nav_comp[i] >= 0) continue; int sp = 0; stk[sp++] = i; nav_comp[i] = nc;
		while (sp) { int t = stk[--sp]; for (int k = 0; k < 3; k++) { int n = navt[t].nb[k]; if (n >= 0 && n < nnav && nav_comp[n] < 0) { nav_comp[n] = nc; stk[sp++] = n; } } }
		nc++; }
	// (a link listed on one side only still joins the pieces: the search below follows links one way, components cover both)
	for (int pass = 0; pass < 8; pass++) { int ch = 0;
		for (int t = 0; t < nnav; t++) for (int k = 0; k < 3; k++) { int n = navt[t].nb[k]; if (n < 0 || n >= nnav || nav_comp[n] == nav_comp[t]) continue;
			int lo = nav_comp[n] < nav_comp[t] ? nav_comp[n] : nav_comp[t], hi = nav_comp[n] + nav_comp[t] - lo; for (int j = 0; j < nnav; j++) if (nav_comp[j] == hi) nav_comp[j] = lo; ch = 1; }
		if (!ch) break; }
	free(stk);
	// strongly connected pieces (iterative Tarjan over the walkable links), completed sinks first
	{ int n = nnav, *idx = malloc(sizeof(int) * n), *low = malloc(sizeof(int) * n), *st = malloc(sizeof(int) * n), *cs = malloc(sizeof(int) * n), *ck = malloc(sizeof(int) * n);
	  unsigned char *on = calloc(n, 1); nav_scc = malloc(sizeof(int) * (n + 1));
	  if (!idx || !low || !st || !cs || !ck || !on || !nav_scc) { free(nav_scc); nav_scc = NULL; }
	  else {
		for (int i = 0; i < n; i++) idx[i] = -1;
		int counter = 0, sp = 0, nscc = 0;
		for (int r = 0; r < n; r++) { if (idx[r] >= 0) continue;
			int cp = 0; cs[cp] = r; ck[cp] = 0; cp++; idx[r] = low[r] = counter++; st[sp++] = r; on[r] = 1;
			while (cp) { int v = cs[cp - 1];
				if (ck[cp - 1] < 3) { int k = ck[cp - 1]++; int w = navt[v].nb[k]; if (w < 0 || w >= n || (navt[v].blocked & (1 << k))) continue;
					if (idx[w] < 0) { idx[w] = low[w] = counter++; st[sp++] = w; on[w] = 1; cs[cp] = w; ck[cp] = 0; cp++; }
					else if (on[w] && idx[w] < low[v]) low[v] = idx[w];
					continue; }
				if (low[v] == idx[v]) { int w; do { w = st[--sp]; on[w] = 0; nav_scc[w] = nscc; } while (w != v); nscc++; }
				cp--; if (cp) { int u = cs[cp - 1]; if (low[v] < low[u]) low[u] = low[v]; } } }
		nav_nscc = nscc;
		if (nscc <= NAV_MAXSCC) {   // reachability: sinks were completed first, so a piece's successors are done before it
			nav_reach_w = (nscc + 31) / 32; nav_reach = calloc((size_t)nscc * nav_reach_w, 4);
			int *first = malloc(sizeof(int) * (nscc + 1)), *mem = malloc(sizeof(int) * n);
			if (nav_reach && first && mem) {
				for (int c = 0; c <= nscc; c++) first[c] = 0; for (int i = 0; i < n; i++) first[nav_scc[i] + 1]++; for (int c = 0; c < nscc; c++) first[c + 1] += first[c];
				{ int *fill = malloc(sizeof(int) * (nscc + 1)); if (fill) { memcpy(fill, first, sizeof(int) * (nscc + 1)); for (int i = 0; i < n; i++) mem[fill[nav_scc[i]]++] = i; free(fill); } }
				for (int c = 0; c < nscc; c++) { unsigned *R = nav_reach + (size_t)c * nav_reach_w; R[c >> 5] |= 1u << (c & 31);
					for (int m = first[c]; m < first[c + 1]; m++) { int v = mem[m];
						for (int k = 0; k < 3; k++) { int w = navt[v].nb[k]; if (w < 0 || w >= n || (navt[v].blocked & (1 << k))) continue; int d = nav_scc[w]; if (d == c) continue;
							const unsigned *D = nav_reach + (size_t)d * nav_reach_w; for (int j = 0; j < nav_reach_w; j++) R[j] |= D[j]; } } }
			} else { free(nav_reach); nav_reach = NULL; }
			free(first); free(mem);
		}
	  }
	  free(idx); free(low); free(st); free(cs); free(ck); free(on); }
}
static int nav_reachable(int a, int b) {   // 1 = the search can get from triangle a to b (or unknown), 0 = it certainly can't
	if (!nav_scc || !nav_reach) return 1; int ca = nav_scc[a], cb = nav_scc[b];
	return (nav_reach[(size_t)ca * nav_reach_w + (cb >> 5)] >> (cb & 31)) & 1;
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
	nav_index();
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
	int r0 = 0;   // perf (b35): the first ring square that holds any triangle, found with the summed counts (the empty squares
	if (nav_bsum) { while (r0 < 64 && !nav_bcount(bx - r0 - 1, by - r0 - 1, bx + r0 + 1, by + r0 + 1)) r0 += 2; if (r0 >= 64) return -1; }   // gave nothing)
	for (int r = r0; r < 64 && best < 0; r += 2) {   // widen the search ring until something turns up
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

// ---- path search (perf, b35): A* over the triangles in a search context (its own scratch), in three steps so that a search
// can be spread over several frames: nav_search_begin, nav_search_run(budget: node expansions, <= 0 unlimited) until it is no
// longer pending, nav_search_path (corridor + funnel). nav_path() runs the three at once in the shared context, with the same
// arithmetic in the same order as before (identical paths); the bots' searches run in their own context (bots.h).
typedef struct {
	int cap; unsigned seq; int *prev, *heap, *corr; float *g, *f; unsigned *stamp; unsigned char *closed; float (*L)[3], (*R)[3];
	int a, b, hn, found, state; float s[3], gl[3], gx, gy; int expanded;   // state: 0 idle, 1 pending, 2 found, 3 no path
	const NavTri *navt_at; int nnav_at;
} NavCtx;
static NavCtx nav_ctx0;   // nav_path's
static long long nav_exp_count;   // triangle expansions so far, every search (stats: tools/perf/host/match_sim)
static int nav_ctx_fit(NavCtx *C) {
	if (C->cap >= nnav && C->prev) return 1;
	free(C->prev); free(C->g); free(C->f); free(C->stamp); free(C->closed); free(C->heap); free(C->corr); free(C->L); free(C->R);
	C->cap = nnav; C->prev = malloc(sizeof(int) * nnav); C->g = malloc(sizeof(float) * nnav); C->f = malloc(sizeof(float) * nnav); C->stamp = calloc(nnav, sizeof(unsigned)); C->closed = malloc(nnav);
	C->heap = malloc(sizeof(int) * (nnav * 3 + 8)); C->corr = malloc(sizeof(int) * nnav); C->L = malloc(12 * (nnav + 2)); C->R = malloc(12 * (nnav + 2)); C->seq = 0;
	if (!C->prev || !C->g || !C->f || !C->stamp || !C->closed || !C->heap || !C->corr || !C->L || !C->R) { C->cap = 0; return 0; }
	return 1;
}
static void heap_push_c(NavCtx *C, int v) { int *H = C->heap; const float *F = C->f; int i = C->hn++; H[i] = v; while (i > 0) { int p = (i - 1) / 2; if (F[H[p]] <= F[H[i]]) break; int t = H[p]; H[p] = H[i]; H[i] = t; i = p; } }
static int heap_pop_c(NavCtx *C) { int *H = C->heap; const float *F = C->f; int r = H[0]; H[0] = H[--C->hn]; int i = 0, n = C->hn;
	for (;;) { int l = 2 * i + 1, rr = l + 1, m = i; if (l < n && F[H[l]] < F[H[m]]) m = l; if (rr < n && F[H[rr]] < F[H[m]]) m = rr; if (m == i) break; int t = H[m]; H[m] = H[i]; H[i] = t; i = m; }
	return r; }
#define NAV_TOUCH(C, i) if ((C)->stamp[i] != (C)->seq) { (C)->stamp[i] = (C)->seq; (C)->g[i] = 1e30f; (C)->prev[i] = -1; (C)->closed[i] = 0; }
// returns 1 pending, 0 nothing to do (no mesh / start or goal off the mesh: state 3)
static int nav_search_begin(NavCtx *C, const float *s, const float *g) {
	C->state = 3; C->expanded = 0; C->navt_at = navt; C->nnav_at = nnav;
	int a = nav_find(s[0], s[1], s[2]), b = nav_find(g[0], g[1], g[2]);
	if (a < 0 || b < 0 || !nav_ctx_fit(C)) return 0;
	if ((nav_comp && nav_comp[a] != nav_comp[b]) || !nav_reachable(a, b)) return 0;   // perf (b35): it can't get there: no search
	C->a = a; C->b = b; memcpy(C->s, s, 12); memcpy(C->gl, g, 12);
	C->seq++;
	NAV_TOUCH(C, a); C->g[a] = 0; C->f[a] = 0; C->hn = 0; heap_push_c(C, a); C->found = a == b;
	C->gx = navt[b].c[0]; C->gy = navt[b].c[1]; C->state = 1;
	return 1;
}
// expands up to budget nodes (<= 0: until done); returns the state (1 still pending, 2 found, 3 no path)
static int nav_search_run(NavCtx *C, int budget) {
	if (C->state != 1) return C->state;
	if (C->navt_at != navt || C->nnav_at != nnav) { C->state = 3; return 3; }   // the map changed under it
	int b = C->b; float gx = C->gx, gy = C->gy;
	while (!C->found && C->hn > 0) {
		if (budget > 0 && C->expanded >= budget) return 1;
		int cur = heap_pop_c(C); if (C->closed[cur]) continue; C->closed[cur] = 1; C->expanded++; nav_exp_count++;
		if (cur == b) { C->found = 1; break; }
		for (int k = 0; k < 3; k++) {
			int n = navt[cur].nb[k]; if (n < 0 || (navt[cur].blocked & (1 << k))) continue;
			NAV_TOUCH(C, n); if (C->closed[n]) continue;
			float dx = navt[n].c[0] - navt[cur].c[0], dy = navt[n].c[1] - navt[cur].c[1], dz = navt[n].c[2] - navt[cur].c[2];
			float ng = C->g[cur] + sqrtf(dx * dx + dy * dy + dz * dz);
			if (ng < C->g[n]) { C->g[n] = ng; C->prev[n] = cur; float hx = navt[n].c[0] - gx, hy = navt[n].c[1] - gy; C->f[n] = ng + sqrtf(hx * hx + hy * hy); if (C->hn < nnav * 3) heap_push_c(C, n); }
		}
	}
	C->state = C->found ? 2 : 3; return C->state;
}
// the path of a found search: up to maxp points (x,y,z) excluding the start, ending at the goal; 0 if none
static int nav_search_path(NavCtx *C, float (*out)[3], int maxp, float margin) {
	if (C->state != 2) return 0;
	const float *s = C->s, *g = C->gl; int a = C->a, b = C->b;
	// corridor (start .. goal)
	int nc = 0; for (int t = b; t >= 0 && nc < nnav; t = C->prev[t]) { C->corr[nc++] = t; if (t == a) break; }
	for (int i = 0; i < nc / 2; i++) { int t = C->corr[i]; C->corr[i] = C->corr[nc - 1 - i]; C->corr[nc - 1 - i] = t; }
	// portals: left/right as seen walking from corr[i] into corr[i+1], shrunk by the margin
	float (*L)[3] = C->L, (*R)[3] = C->R; int np = 0;
	memcpy(L[np], s, 12); memcpy(R[np], s, 12); np++;
	for (int i = 0; i + 1 < nc; i++) {
		const NavTri *t = &navt[C->corr[i]]; int k = 0; while (k < 3 && t->nb[k] != C->corr[i + 1]) k++;
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
// path from s to g, all at once (tanks, the test walker). Writes up to maxp points (x,y,z) excluding the start, ending at the goal.
// Returns count, 0 if none.
static int nav_path(const float *s, const float *g, float (*out)[3], int maxp, float margin) {
	if (!nav_search_begin(&nav_ctx0, s, g)) return 0;
	nav_search_run(&nav_ctx0, 0);
	return nav_search_path(&nav_ctx0, out, maxp, margin);
}
