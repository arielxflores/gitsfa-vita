// vmvis.c (b60): which triangles of a first-person model (vm_<gun>.gvm: arms + gun) can ever be seen.
// The viewmodel is 23-31 thousand triangles (the arms alone 18,878, up to the shoulders) drawn every frame, two-sided;
// most of the arms never enter the picture. This tool poses the model with EVERY frame of EVERY clip it has (30 per
// second), under the hip, half-aimed and aimed projections of main.c (viewmodel fov and offset from weapons.txt) widened
// by a guard band (GUARD: the view 30 % larger each way, for sway, recoil and blends between clips), draws triangle
// numbers with a depth buffer at 2x the Vita's resolution, and marks what wins a pixel. Triangles crossing the near plane
// count as seen. tools/lod/make_fp.py turns the result into vm_<gun>.gfp (the part that is seen kept as it is or
// simplified within a fraction of a pixel, the rest collapsed to a coarse closed surface, never removed).
//   vmvis DATA_DIR OUT_DIR [gun]        -> OUT_DIR/vm_<gun>.vis: 'VMV3', nmesh, nclip, u64 everyday mask (bit per clip, bit
//                                          63), nclip x 16-byte clip names, then per mesh: ntri, ntri x u64 (bit c: seen
//                                          in a frame of clip c; bit 63: seen in a mix of everyday clips)
// Deterministic across compilers: no libm transcendentals on the way (own tan; sqrtf is exact).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"   // tools/weapons/sgl: buffers kept in RAM (the rasteriser here is our own)
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../engine/src/model.h"
#include "../../engine/src/weapon.h"
#include "../../engine/src/wpn_pose.h"
#define W 1920
#define H 1088
#define GUARD 1.30
static float zb[H][W]; static unsigned idb[H][W];
static double dtan(double x) { double s = 0, c = 0, t = x, x2 = x * x; s = x; c = 1; t = x; double u = 1;   // Taylor series, x < 1
	for (int k = 1; k < 12; k++) { u = -u * x2 / ((2 * k - 1) * (2 * k)); c += u; t = -t * x2 / ((2 * k) * (2 * k + 1)); s += t; } return s / c; }
typedef struct { int nv, nt, pbase, rigid, blend; const unsigned char *v; const unsigned short *ix; unsigned long long *vis; float (*cs)[4]; } VM;
// the everyday clips: the light mesh is drawn only while nothing else plays (engine: wvm_fp_state). sprint_out, reload,
// draw, holster, climb, vault and the knife's / grenade's long actions bring far more of the arms into view (vm_m4a1:
// everyday 2,600 of the arms' 18,878 triangles; + sprint_out 3,300, + reload_empty 6,800, + climb 8,100).
static const char *EVERYDAY[] = { "idle", "walk", "run", "sprint", "sprint_in", "fire", "fire_ads", "ads_idle", "ads_in", "jump", "fall", "land", "slide", "slide_fire", "bolt", "fire2", "cook", NULL };
#define NMIX 3000    // mixed everyday states per gun (bit 63 of a triangle's mask)
static unsigned rs; static float frand(void) { rs = rs * 1664525u + 1013904223u; return (float)(rs >> 8) / 16777216.f; }
static int dx0, dy0, dx1, dy1;   // the part of the buffers drawn into since the last clear
// one view: palette pal, aim amount, the viewmodel's place K (3 columns of rotation, translation), marks bit `bit`
static void view(const Model *M, VM *vm, const float *pal, const WDef *D, float ads, const float *K, int bit) {
	double th = dtan(D->hip_vfov * 0.5 * 3.14159265358979 / 180) * (1 - ads) + dtan(D->ads_vfov * 0.5 * 3.14159265358979 / 180) * ads; th *= GUARD;
	float sy = (float)(1 / th), sx = sy * 544.f / 960.f;
	for (int y = dy0; y <= dy1; y++) for (int x = dx0; x <= dx1; x++) { zb[y][x] = 1e30f; idb[y][x] = 0; }
	dx0 = W; dy0 = H; dx1 = -1; dy1 = -1;
	for (int i = 0; i < M->nmesh; i++) { VM *q = &vm[i];
		int opaque = q->blend == 0 || q->blend > 3 || (q->blend == 3 && ads < 0.5f); if (!opaque) continue;
		for (int k = 0; k < q->nv; k++) {   // MD_VS: palette entries pbase + index, 3 rows each; camera space x right, y up, -z forward
			const unsigned char *v = q->v + k * 32; float pos[3], s[3] = { 0, 0, 0 }; memcpy(pos, v, 12);
			if (q->rigid >= 0) { const float *R = pal + q->rigid * 12; for (int r = 0; r < 3; r++) s[r] = R[r * 4] * pos[0] + R[r * 4 + 1] * pos[1] + R[r * 4 + 2] * pos[2] + R[r * 4 + 3]; }
			else for (int j = 0; j < 4; j++) { float w = v[28 + j] / 255.f; if (w <= 0) continue; int e = q->pbase + v[24 + j]; if (e >= M->npal) e = M->npal - 1; const float *R = pal + e * 12;
				for (int r = 0; r < 3; r++) s[r] += w * (R[r * 4] * pos[0] + R[r * 4 + 1] * pos[1] + R[r * 4 + 2] * pos[2] + R[r * 4 + 3]); }
			float cx = K[0] * s[0] + K[4] * s[1] + K[8] * s[2] + K[12], cy = K[1] * s[0] + K[5] * s[1] + K[9] * s[2] + K[13], wv = -(K[2] * s[0] + K[6] * s[1] + K[10] * s[2] + K[14]);   // w = distance in front of the eye
			q->cs[k][0] = cx * sx; q->cs[k][1] = cy * sy; q->cs[k][2] = wv; q->cs[k][3] = wv; }
		for (int t = 0; t < q->nt; t++) {
			const float *A = q->cs[q->ix[t * 3]], *B = q->cs[q->ix[t * 3 + 1]], *C = q->cs[q->ix[t * 3 + 2]];
			int nf_ = (A[3] < 2) + (B[3] < 2) + (C[3] < 2); if (nf_ == 3) continue; if (nf_) { q->vis[t] |= 1ull << bit; continue; }   // behind the near plane (2 cm) / crossing it
			float X[3] = { (A[0] / A[3] + 1) * W / 2, (B[0] / B[3] + 1) * W / 2, (C[0] / C[3] + 1) * W / 2 }, Y[3] = { (1 - A[1] / A[3]) * H / 2, (1 - B[1] / B[3]) * H / 2, (1 - C[1] / C[3]) * H / 2 }, Z[3] = { A[3], B[3], C[3] };
			float fx0 = fminf(X[0], fminf(X[1], X[2])), fx1 = fmaxf(X[0], fmaxf(X[1], X[2])), fy0 = fminf(Y[0], fminf(Y[1], Y[2])), fy1 = fmaxf(Y[0], fmaxf(Y[1], Y[2]));
			if (fx1 < 0 || fy1 < 0 || fx0 > W || fy0 > H) continue;
			int x0 = fx0 < 0 ? 0 : (int)fx0, x1 = fx1 > W - 1 ? W - 1 : (int)fx1, y0 = fy0 < 0 ? 0 : (int)fy0, y1 = fy1 > H - 1 ? H - 1 : (int)fy1;
			float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]); if (den == 0) continue; float id = 1 / den; unsigned tag = ((unsigned)(i + 1) << 24) | (unsigned)t;
			if (x0 < dx0) dx0 = x0; if (x1 > dx1) dx1 = x1; if (y0 < dy0) dy0 = y0; if (y1 > dy1) dy1 = y1;
			float ax = (Y[1] - Y[2]) * id, bx = (Y[2] - Y[0]) * id, zx = ax * Z[0] + bx * Z[1] - (ax + bx) * Z[2], e = 1e-5f;   // per pixel along a row (e: no cracks between neighbours from the rounding)
			for (int y = y0; y <= y1; y++) {
				float px = x0 + 0.5f, py = y + 0.5f, a_ = ((Y[1] - Y[2]) * (px - X[2]) + (X[2] - X[1]) * (py - Y[2])) * id, b_ = ((Y[2] - Y[0]) * (px - X[2]) + (X[0] - X[2]) * (py - Y[2])) * id, z = a_ * Z[0] + b_ * Z[1] + (1 - a_ - b_) * Z[2];
				float *zr = zb[y]; unsigned *ir = idb[y];
				for (int x = x0; x <= x1; x++, a_ += ax, b_ += bx, z += zx) { if (a_ < -e || b_ < -e || a_ + b_ > 1 + e) continue; if (z >= zr[x]) continue; zr[x] = z; ir[x] = tag; } }
		} }
	for (int y = dy0; y <= dy1; y++) for (int x = dx0; x <= dx1; x++) { unsigned g = idb[y][x]; if (g) vm[(g >> 24) - 1].vis[g & 0xffffff] |= 1ull << bit; }
}
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "vmvis DATA_DIR OUT_DIR [gun]\n"); return 1; }
	const char *dir = argv[1], *out = argv[2]; char p[512]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	model_keep_cpu = 1; wvm_fp_off = 1; dx0 = dy0 = 0; dx1 = W - 1; dy1 = H - 1;
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; if (argc > 3 && strcmp(argv[3], D->sh)) continue;
		WSlot S; memset(&S, 0, sizeof(S)); if (wslot_load_ptr(&S, d, dir) < 0) { printf("%s: no model\n", D->sh); continue; }
		const Model *M = &S.m; VM *vm = calloc(M->nmesh, sizeof(VM)); static float pal[64 * 12]; MBone *P = malloc(M->nb * sizeof(MBone));
		if (M->nclip > 63) { printf("%s: %d clips (63 at most)\n", D->sh, M->nclip); return 3; }
		for (int i = 0; i < M->nmesh; i++) { const MMesh *m = &M->mesh[i]; VM *q = &vm[i]; q->nv = m->nv; q->nt = m->ni / 3; q->pbase = m->pbase; q->rigid = m->rigid; q->blend = m->blend & 127;
			q->v = m->cpu; q->ix = sgl_buf[m->ibo].d; q->vis = calloc(q->nt + 1, 8); q->cs = malloc((q->nv + 1) * 16); }
		long renders = 0; int aim = D->kind == WK_AUTO || D->kind == WK_SEMI || D->kind == WK_BOLT || D->kind == WK_PUMP;   // knife, grenade: no aiming
		unsigned long long every = 0; int ev[64], nev = 0, loops[64], nl = 0, acts[64], na = 0;
		for (int c = 0; c < M->nclip; c++) for (int k = 0; EVERYDAY[k]; k++) if (!strcmp(M->clip[c].name, EVERYDAY[k])) { every |= 1ull << c; ev[nev++] = c; if (M->clip[c].loop) loops[nl++] = c; else acts[na++] = c; }
		// (1) every frame of every clip, alone
		for (int ci = 0; ci < M->nclip; ci++) {
			float len = model_clip_len(M, ci); int nf = (int)(len * 30) + 1;
			for (int f = 0; f <= nf; f++) {
				model_sample(M, ci, f < nf ? f / 30.f : len, P); model_palette(M, P, pal);
				for (int a = 0; a < (aim ? 3 : 1); a++) { float ads = a * 0.5f;
					float K[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, D->hip_off[0] * (1 - ads) + D->ads_off[0] * ads, D->hip_off[1] * (1 - ads) + D->ads_off[1] * ads, D->hip_off[2] * (1 - ads) + D->ads_off[2] * ads, 1 };
					view(M, vm, pal, D, ads, K, ci); renders++; } } }
		// (2) the everyday clips as the game mixes them (wpn_vm_pose: a base clip, one fading out, an action at any weight, any
		// aim amount), with main.c's stick sway and recoil kick up to their limits
		rs = 20261111u + (unsigned)d * 7919u;
		for (int it = 0; it < NMIX && nev; it++) {
			Anim *A = &S.a; A->act = -1; A->act_w = 0; A->base_fade = 1; A->base_prev = -1; int nb = nl ? nl : nev; const int *bs = nl ? loops : ev;
			A->base = bs[(int)(frand() * nb) % nb]; A->base_t = frand() * 4; float ads = aim ? (frand() < 0.3f ? 0 : frand() < 0.4f ? 1 : frand()) : 0;
			if (frand() < 0.6f) { A->base_prev = ev[(int)(frand() * nev) % nev]; A->base_prev_t = frand() * 4; A->base_fade = frand(); }
			if (na && frand() < 0.7f) { int ac = acts[(int)(frand() * na) % na]; A->act = ac; A->act_t = frand() * model_clip_len(M, ac); A->act_w = frand() < 0.4f ? 1 : frand(); }
			float sway_x = (frand() * 2 - 1) * 1.2f, sway_y = (frand() * 2 - 1) * 0.8f, kick = frand() < 0.5f ? frand() : frand() * 2.5f;
			if (frand() < 0.25f) { sway_x = sway_x < 0 ? -1.2f : 1.2f; sway_y = sway_y < 0 ? -0.8f : 0.8f; }
			MBone *Q = wpn_vm_pose(&S, ads); model_palette(M, Q, pal);
			float ka = kick * 0.015f, ks = ka - ka * ka * ka / 6, kc = 1 - ka * ka / 2 + ka * ka * ka * ka / 24;   // (sin, cos of at most 0.04)
			float ox = D->hip_off[0] * (1 - ads) + D->ads_off[0] * ads, oy = D->hip_off[1] * (1 - ads) + D->ads_off[1] * ads - 0.3f * kick * ads, oz = D->hip_off[2] * (1 - ads) + D->ads_off[2] * ads + 3.0f * kick * ads;
			float K[16] = { 1, 0, 0, 0, 0, kc, ks, 0, 0, -ks, kc, 0, sway_x * (1 - ads) + ox, sway_y * (1 - ads) + oy, kick * 0.8f * (1 - ads) + oz, 1 };
			view(M, vm, pal, D, ads, K, 63); renders++;
		}
		snprintf(p, sizeof(p), "%s/vm_%s.vis", out, D->sh); FILE *f = fopen(p, "wb"); if (!f) { printf("cannot write %s\n", p); return 2; }
		fwrite("VMV3", 4, 1, f); fwrite(&M->nmesh, 4, 1, f); fwrite(&M->nclip, 4, 1, f); every |= 1ull << 63; fwrite(&every, 8, 1, f); for (int c = 0; c < M->nclip; c++) fwrite(M->clip[c].name, 16, 1, f);
		printf("%-10s %ld views:", D->sh, renders);
		for (int i = 0; i < M->nmesh; i++) { int n = 0, ne = 0; for (int t = 0; t < vm[i].nt; t++) { n += vm[i].vis[t] != 0; ne += (vm[i].vis[t] & every) != 0; } fwrite(&vm[i].nt, 4, 1, f); fwrite(vm[i].vis, 8, vm[i].nt, f);
			if (vm[i].nt >= 200) printf(" mesh %d (blend %d%s) %d of %d seen, %d in the everyday clips;", i, vm[i].blend, vm[i].rigid >= 0 ? ", rigid" : "", n, vm[i].nt, ne); }
		printf("\n"); fclose(f); fflush(stdout); wslot_free_ptr(&S);
	}
	return 0;
}
