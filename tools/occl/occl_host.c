// tools/occl/occl_host.c (b60): checks engine/src/occl.h against the truth and counts what it saves.
// For each map: world.h's own world_load (tools/world_host's stub GL) with the chunk split of <map>.gchk; then N camera
// views (eye 165 cm above random floor triangles, random yaw, pitch within +-25 degrees; main.c's projection). Per view:
//   truth    every depth-writing triangle of the map drawn into a 960x544 depth + "which chunk" picture (opaque triangles
//            write depth, back faces culled as the GPU does; alpha-tested ones are drawn as see-through: they pass the test
//            and mark their chunk, hide nothing); blended meshes likewise. A chunk with one pixel is VISIBLE.
//   engine   oc_begin + the frustum and oc_test calls world_draw makes.
// A visible chunk / mesh the engine would not draw is an ERROR (must be 0). Counts: indices in the frustum, after
// occlusion, truly visible.
//   occl_host MAPS_DIR [N [map ...]]
#include <stdarg.h>
#include <dirent.h>
#include <time.h>
#include "vitaGL.h"
typedef struct { float *p; unsigned short *ix; int nv, ni; } HMesh; static HMesh *hm; static int hcap;
#define W_MESH_HOOK(i, m, v, stride, ix) hook_mesh(i, (m)->nv, (m)->ni, v, stride, ix)
static void hook_mesh(int i, int nv, int ni, const float *v, int stride, const unsigned short *ix) {
	if (i >= hcap) { int c = hcap ? hcap * 2 : 1024; while (c <= i) c *= 2; hm = realloc(hm, sizeof(HMesh) * c); memset(hm + hcap, 0, sizeof(HMesh) * (c - hcap)); hcap = c; }
	HMesh *h = &hm[i]; free(h->p); free(h->ix); h->nv = nv; h->ni = ni; h->p = malloc(nv * 12); h->ix = malloc(ni * 2);
	for (int k = 0; k < nv; k++) memcpy(h->p + k * 3, v + k * stride, 12); memcpy(h->ix, ix, ni * 2);
}
#include WORLDH
#define TW 960
#define TH 544
static float tz[TH][TW]; static int tid[TH][TW]; static int *seen_chunk;   // per chunk: pixels of it in the truth picture
static void logq(const char *fmt, ...) { (void)fmt; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static unsigned rs = 99; static float frand(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 16777216.f; }
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void view(float *m, const float *p, float yaw, float pitch) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0, -(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
// truth: one triangle (clip x y w), near-clipped at w = 10 (the world's near plane); write: it hides what is behind
static void t_fill(const float (*v)[3], int n, int id, int write) {
	float s[5][3]; for (int i = 0; i < n; i++) { float q = 1 / v[i][2]; s[i][0] = (v[i][0] * q * 0.5f + 0.5f) * TW; s[i][1] = (0.5f - v[i][1] * q * 0.5f) * TH; s[i][2] = q; }
	for (int k = 1; k + 1 < n; k++) { const float *A = s[0], *B = s[k], *C = s[k + 1];
		float den = (B[0] - A[0]) * (C[1] - A[1]) - (C[0] - A[0]) * (B[1] - A[1]); if (den == 0) continue; float id_ = 1 / den;
		float fx0 = fminf(A[0], fminf(B[0], C[0])), fx1 = fmaxf(A[0], fmaxf(B[0], C[0])), fy0 = fminf(A[1], fminf(B[1], C[1])), fy1 = fmaxf(A[1], fmaxf(B[1], C[1]));
		if (fx1 < 0 || fy1 < 0 || fx0 > TW || fy0 > TH) continue;
		int x0 = fx0 < 0 ? 0 : (int)fx0, x1 = fx1 > TW - 1 ? TW - 1 : (int)fx1, y0 = fy0 < 0 ? 0 : (int)fy0, y1 = fy1 > TH - 1 ? TH - 1 : (int)fy1;
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f, py = y + 0.5f, b = ((px - A[0]) * (C[1] - A[1]) - (C[0] - A[0]) * (py - A[1])) * id_, c = ((B[0] - A[0]) * (py - A[1]) - (px - A[0]) * (B[1] - A[1])) * id_, a = 1 - b - c;
			if (a < -1e-4f || b < -1e-4f || c < -1e-4f) continue;   // (a hair generous: the truth must not miss a pixel the GPU fills)
			float q = a * A[2] + b * B[2] + c * C[2]; if (q < 1 / 12000.f) continue;   // (beyond the far plane)
			if (write) { if (q > tz[y][x]) { tz[y][x] = q; tid[y][x] = id; } }
			else if (q >= tz[y][x] * 0.9999f) seen_chunk[id]++; } }
}
static void t_tri(const float *M, const float *a, const float *b, const float *c, int id, int write, int two) {
	float cl[3][3]; const float *P[3] = { a, b, c };
	for (int i = 0; i < 3; i++) { cl[i][0] = M[0] * P[i][0] + M[4] * P[i][1] + M[8] * P[i][2] + M[12]; cl[i][1] = M[1] * P[i][0] + M[5] * P[i][1] + M[9] * P[i][2] + M[13]; cl[i][2] = M[3] * P[i][0] + M[7] * P[i][1] + M[11] * P[i][2] + M[15]; }
	if (cl[0][2] < 10 && cl[1][2] < 10 && cl[2][2] < 10) return;
	if (!two) { float det = cl[0][0] * (cl[1][1] * cl[2][2] - cl[2][1] * cl[1][2]) - cl[0][1] * (cl[1][0] * cl[2][2] - cl[2][0] * cl[1][2]) + cl[0][2] * (cl[1][0] * cl[2][1] - cl[2][0] * cl[1][1]); if (det <= 0) return; }
	float v[5][3]; int n = 0;
	for (int i = 0; i < 3; i++) { const float *p = cl[i], *q = cl[(i + 1) % 3]; int ip = p[2] >= 10, iq = q[2] >= 10;
		if (ip) memcpy(v[n++], p, 12);
		if (ip != iq) { float k = (10 - p[2]) / (q[2] - p[2]); v[n][0] = p[0] + (q[0] - p[0]) * k; v[n][1] = p[1] + (q[1] - p[1]) * k; v[n][2] = 10; n++; } }
	if (n >= 3) t_fill((const float (*)[3])v, n, id, write);
}
int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "occl_host MAPS_DIR/ [N [map ...]]\n"); return 2; }
	const char *Md = argv[1]; int N = argc > 2 ? atoi(argv[2]) : 200; char *stems[64]; int ns = 0;
	if (argc > 3) for (int i = 3; i < argc && ns < 64; i++) stems[ns++] = argv[i];
	else { DIR *d = opendir(Md); struct dirent *e; while (d && (e = readdir(d)) && ns < 64) { size_t l = strlen(e->d_name); if (l > 5 && !strcmp(e->d_name + l - 5, ".gmap")) { stems[ns] = strdup(e->d_name); stems[ns][l - 5] = 0; ns++; } } if (d) closedir(d); }
	if (getenv("OC_MIN_SIZE")) oc_min_size = atof(getenv("OC_MIN_SIZE"));
	w_log = logq; long errs_all = 0; float P[16]; persp(P, 59 * 3.14159f / 180, 960.0f / 544, 10, 12000);
	for (int si = 0; si < ns; si++) {
		if (world_load(Md, stems[si]) < 0) { printf("%s: load failed\n", stems[si]); continue; }
		// floors to stand on: opaque triangles facing up, 1 m2 and more (the occluder list has them)
		int nfl = 0, *fl = malloc(sizeof(int) * (oc_ntri + 1));
		for (int i = 0; i < oc_ntri; i++) { const OcTri *t = &oc_tri[i]; float e1[3], e2[3]; for (int k = 0; k < 3; k++) { e1[k] = t->p[1][k] - t->p[0][k]; e2[k] = t->p[2][k] - t->p[0][k]; }
			float nz = e1[0] * e2[1] - e1[1] * e2[0], nx = e1[1] * e2[2] - e1[2] * e2[1], ny = e1[2] * e2[0] - e1[0] * e2[2]; if (nz > 0.9f * sqrtf(nx * nx + ny * ny + nz * nz)) fl[nfl++] = i; }
		seen_chunk = calloc(w_nchunk + 1, sizeof(int));
		long i_fr = 0, i_oc = 0, i_true = 0, errs = 0, err_px = 0, tests = 0, hidden = 0, octris = 0; int worst = 0, blobs = 0, hair = 0, thin_st = 0; double t_oc = 0, t_test = 0; rs = 1000; for (const char *c = stems[si]; *c; c++) rs = rs * 31 + (unsigned char)*c;
		for (int vi = 0; vi < N && nfl; vi++) {
			const OcTri *t = &oc_tri[fl[(int)(frand() * nfl) % nfl]]; float u = frand(), w = frand(); if (u + w > 1) { u = 1 - u; w = 1 - w; }
			float eye[3]; for (int k = 0; k < 3; k++) eye[k] = t->p[0][k] + (t->p[1][k] - t->p[0][k]) * u + (t->p[2][k] - t->p[0][k]) * w; eye[2] += frand() < 0.2f ? 95 : 165;
			float V[16], MVP[16]; view(V, eye, frand() * 6.2832f, (frand() - 0.5f) * 0.87f); mul4(MVP, P, V);
			// ---- truth
			for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) { tz[y][x] = 0; tid[y][x] = -1; }
			memset(seen_chunk, 0, (w_nchunk + 1) * sizeof(int));
			for (int pass = 0; pass < 2; pass++) for (int i = 0; i < world_nmesh; i++) {   // the solid ones first, then what only passes the test
				const WMeshW *m = &w_mesh[i]; const HMesh *h = &hm[i]; int solid = m->mode == WMODE_OPAQUE && !(m->flags & WFLAG_NOZW);
				if ((pass == 0) != solid) continue;
				for (int c = m->c0; c < m->c1; c++) { int f = w_chunk[c].first, n = w_chunk[c].count;
					for (int k = f; k + 2 < f + n; k += 3) t_tri(MVP, h->p + h->ix[k] * 3, h->p + h->ix[k + 1] * 3, h->p + h->ix[k + 2] * 3, c, solid, (m->flags & WFLAG_TWO) != 0 || !solid); }
			}
			for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) if (tid[y][x] >= 0) seen_chunk[tid[y][x]]++;
			// ---- engine (world_draw's tests)
			float wrong[3] = { eye[0] + 300, eye[1] - 200, eye[2] + 50 };   // (the caller's position is not trusted: oc_begin takes the eye from the matrix)
#ifdef OC_CALLER_EYE
			memcpy(wrong, eye, 12);
#endif
			double t0 = now(); oc_begin(MVP, wrong); double t1 = now();
			if (fabsf(oc_cam[0] - eye[0]) + fabsf(oc_cam[1] - eye[1]) + fabsf(oc_cam[2] - eye[2]) > 0.5f) { printf("  eye from the matrix off: %.2f %.2f %.2f vs %.2f %.2f %.2f\n", oc_cam[0], oc_cam[1], oc_cam[2], eye[0], eye[1], eye[2]); errs++; } t_oc += t1 - t0; for (int k = 0; k < oc_nsel; k++) if (oc_drawn[k]) octris += oc_cell[oc_sel[k].i].count;
			for (int i = 0; i < world_nmesh; i++) {
				const WMeshW *m = &w_mesh[i]; int chunked = m->c1 - m->c0 > 1 && m->mode != WMODE_BLEND;
				int mesh_true = 0; for (int c = m->c0; c < m->c1; c++) mesh_true |= seen_chunk[c];
				int fr = w_visible(MVP, m->mn, m->mx), oc = fr && (tests++, oc_test(m->mn, m->mx));
				if (fr && !oc) hidden++;
				if (!fr) { if (mesh_true) { errs++; printf("  %s view %d: mesh %d visible but frustum-culled\n", stems[si], vi, i); } continue; }
				if (!chunked) { if (mesh_true && !oc) { int px = 0; for (int c = m->c0; c < m->c1; c++) px += seen_chunk[c]; errs++; err_px += px; if (px > worst) worst = px;
					if (getenv("OCV")) printf("  %s view %d: mesh %d (mode %d flags %d) has %d pixels but is occlusion-culled\n", stems[si], vi, i, m->mode, m->flags, px); } i_fr += m->ni; if (oc) i_oc += m->ni; if (mesh_true) i_true += m->ni; continue; }
				for (int c = m->c0; c < m->c1; c++) { const WChunk *C = &w_chunk[c]; int cf = w_visible(MVP, C->mn, C->mx);
					if (!cf) { if (seen_chunk[c]) { errs++; if (errs <= 5) printf("  %s view %d: chunk %d visible but frustum-culled\n", stems[si], vi, c); } continue; }
					i_fr += C->count; int co = oc && (tests++, oc_test(C->mn, C->mx)); if (oc && !co) hidden++;
					if (co) i_oc += C->count;
					if (seen_chunk[c]) { i_true += C->count; if (!co) { errs++; err_px += seen_chunk[c]; if (seen_chunk[c] > worst) worst = seen_chunk[c];
						{ int blk = 0; for (int y = 0; y + 1 < TH && !blk; y++) for (int x = 0; x + 1 < TW; x++) if (tid[y][x] == c && tid[y][x + 1] == c && tid[y + 1][x] == c && tid[y + 1][x + 1] == c) { blk = 1; break; }
						  int solid = m->mode == WMODE_OPAQUE && !(m->flags & WFLAG_NOZW); if (blk) { blobs++; printf("  BLOCK: %s view %d chunk %d (%d px)\n", stems[si], vi, c, seen_chunk[c]); } else if (solid) hair++; else thin_st++; }
						if (getenv("OCV")) printf("  %s view %d (eye %.0f %.0f %.0f): chunk %d of mesh %d (mode %d flags %d) has %d pixels but is %s\n", stems[si], vi, eye[0], eye[1], eye[2], c, i, m->mode, m->flags, seen_chunk[c], oc ? "occlusion-culled" : "culled with its mesh"); } } }
			}
			if (getenv("OCDUMP") && atoi(getenv("OCDUMP")) == vi) {   // pictures of this view: truth depth (grey), the chunk OCCHUNK's pixels (red), where the occlusion picture is nearer than the chunk (blue tint)
				int ck = getenv("OCCHUNK") ? atoi(getenv("OCCHUNK")) : -1; FILE *f = fopen("/tmp/claude-0/ocdump.ppm", "wb"); fprintf(f, "P6\n%d %d\n255\n", TW, TH);
				// redo the see-through pass to find the chunk's pixels
				static unsigned char mask[TH][TW]; memset(mask, 0, sizeof(mask));
				float wn = 1e30f; if (ck >= 0) { const WChunk *C = &w_chunk[ck]; for (int k = 0; k < 8; k++) { float p[3] = { k & 1 ? C->mx[0] : C->mn[0], k & 2 ? C->mx[1] : C->mn[1], k & 4 ? C->mx[2] : C->mn[2] }, c[3]; oc_xf(p, c); if (c[2] < wn) wn = c[2]; } }
				for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) { float q = tz[y][x]; int g = q > 0 ? (int)(255 * fminf(1, 300 * q)) : 0; unsigned char c[3] = { g, g, g };
					if (tid[y][x] == ck) { c[0] = 255; c[1] = 0; c[2] = 0; }
					else if (oc_buf[y * OC_H / TH][x * OC_W / TW] > 1.002f / wn) c[2] = 255;
					fwrite(c, 1, 3, f); }
				fclose(f); printf("dumped view %d, chunk %d nearest w %.0f\n", vi, ck, wn); }
			t_test += now() - t1;
		}
		printf("%-14s %d views | indices a view: frustum %6ldk, after occlusion %6ldk (-%2.0f%%), truly visible %6ldk | %ld dropped with pixels in the truth (%ld pixels in all, worst %d; %d with a 2x2 block, %d solid hairlines, %d see-through) | occluders %d (%ld drawn a view) | host: picture %.3f ms, %ld tests %.3f ms (%ld hidden)\n",
			stems[si], N, i_fr / N / 1000, i_oc / N / 1000, i_fr ? 100.0 * (i_fr - i_oc) / i_fr : 0.0, i_true / N / 1000, errs, err_px, worst, blobs, hair, thin_st, oc_ntri, octris / N, t_oc / N * 1e3, tests / N, t_test / N * 1e3, hidden / N);
#ifdef OC_COUNT
		printf("    fills %ld a view, %ld rectangle samples, %ld inside, %d cells in all\n", oc_cnt_fill / N, oc_cnt_box / N, oc_cnt_in / N, oc_ncell); oc_cnt_fill = oc_cnt_box = oc_cnt_in = 0;
#endif
		fflush(stdout); errs_all += errs; free(fl); free(seen_chunk); (void)err_px;
	}
	return errs_all ? 1 : 0;
}
