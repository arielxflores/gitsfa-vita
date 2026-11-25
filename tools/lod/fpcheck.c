// fpcheck.c (b60): is the light first-person mesh (vm_<gun>.gfp, make_fp.py) the same picture as the full model whenever the
// engine would draw it? For each gun: (A) every frame of every everyday clip at 60 per second (the frames between the ones
// vmvis.c looked at) under 5 aim amounts, (B) N random states as the game makes them (wpn_vm_pose: a base clip, another one
// fading in, an action at any weight, any aim amount) with the stick sway and recoil kick of main.c at their limits. Both
// meshes are drawn into depth buffers at the Vita's resolution with main.c's projection; a pixel differs when one covers
// it and the other does not, or the depths differ by more than 0.02 cm.
//   fpcheck DATA_DIR GFP_DIR [N [gun]]      exit 1 when any pixel differs
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
typedef struct { char name[48]; } Snd; static Snd snds[1]; static int nsnd;
#include "../../engine/src/model.h"
#include "../../engine/src/weapon.h"
#include "../../engine/src/wpn_pose.h"
#define W 960
#define H 544
static float zb[2][H][W];
static unsigned rs = 12345; static float frand(void) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f; }
static void draw(const Model *M, int light, const float *pal, float ads, const float *K, float sx, float sy, float (*z)[W], long *tris) {
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) z[y][x] = 1e30f;
	static float cs[70000][3];
	for (int i = 0; i < M->nmesh; i++) { const MMesh *m = &M->mesh[i]; int b = m->blend & 127; if (!(b == 0 || b > 3 || (b == 3 && ads < 0.5f))) continue;
		const unsigned char *V = m->cpu; const unsigned short *ix = sgl_buf[m->ibo].d; int nv = m->nv, ni = m->ni;
		if (light && M->fp && M->fp[i].ni) { V = sgl_buf[M->fp[i].vbo].d; ix = sgl_buf[M->fp[i].ibo].d; nv = M->fp[i].nv; ni = M->fp[i].ni; }
		for (int k = 0; k < nv; k++) { const unsigned char *v = V + k * 32; float pos[3], s[3] = { 0, 0, 0 }; memcpy(pos, v, 12);
			if (m->rigid >= 0) { const float *R = pal + m->rigid * 12; for (int r = 0; r < 3; r++) s[r] = R[r * 4] * pos[0] + R[r * 4 + 1] * pos[1] + R[r * 4 + 2] * pos[2] + R[r * 4 + 3]; }
			else for (int j = 0; j < 4; j++) { float w = v[28 + j] / 255.f; if (w <= 0) continue; int e = m->pbase + v[24 + j]; if (e >= M->npal) e = M->npal - 1; const float *R = pal + e * 12;
				for (int r = 0; r < 3; r++) s[r] += w * (R[r * 4] * pos[0] + R[r * 4 + 1] * pos[1] + R[r * 4 + 2] * pos[2] + R[r * 4 + 3]); }
			cs[k][0] = (K[0] * s[0] + K[4] * s[1] + K[8] * s[2] + K[12]) * sx; cs[k][1] = (K[1] * s[0] + K[5] * s[1] + K[9] * s[2] + K[13]) * sy; cs[k][2] = -(K[2] * s[0] + K[6] * s[1] + K[10] * s[2] + K[14]); }
		*tris += ni / 3;
		for (int t = 0; t < ni; t += 3) { const float *A = cs[ix[t]], *B = cs[ix[t + 1]], *C = cs[ix[t + 2]];
			if (A[2] < 2 || B[2] < 2 || C[2] < 2) continue;   // (near-plane triangles: both meshes keep them, vmvis.c)
			float X[3] = { (A[0] / A[2] + 1) * W / 2, (B[0] / B[2] + 1) * W / 2, (C[0] / C[2] + 1) * W / 2 }, Y[3] = { (1 - A[1] / A[2]) * H / 2, (1 - B[1] / B[2]) * H / 2, (1 - C[1] / C[2]) * H / 2 }, Z[3] = { A[2], B[2], C[2] };
			float fx0 = fminf(X[0], fminf(X[1], X[2])), fx1 = fmaxf(X[0], fmaxf(X[1], X[2])), fy0 = fminf(Y[0], fminf(Y[1], Y[2])), fy1 = fmaxf(Y[0], fmaxf(Y[1], Y[2]));
			if (fx1 < 0 || fy1 < 0 || fx0 > W || fy0 > H) continue;
			int x0 = fx0 < 0 ? 0 : (int)fx0, x1 = fx1 > W - 1 ? W - 1 : (int)fx1, y0 = fy0 < 0 ? 0 : (int)fy0, y1 = fy1 > H - 1 ? H - 1 : (int)fy1;
			float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]); if (den == 0) continue; float id = 1 / den;
			float ax = (Y[1] - Y[2]) * id, bx = (Y[2] - Y[0]) * id, zx = ax * Z[0] + bx * Z[1] - (ax + bx) * Z[2];
			for (int y = y0; y <= y1; y++) {
				float px = x0 + 0.5f, py = y + 0.5f, a_ = ((Y[1] - Y[2]) * (px - X[2]) + (X[2] - X[1]) * (py - Y[2])) * id, b_ = ((Y[2] - Y[0]) * (px - X[2]) + (X[0] - X[2]) * (py - Y[2])) * id, zz = a_ * Z[0] + b_ * Z[1] + (1 - a_ - b_) * Z[2];
				float *zr = z[y];
				for (int x = x0; x <= x1; x++, a_ += ax, b_ += bx, zz += zx) { if (a_ < 0 || b_ < 0 || a_ + b_ > 1) continue; if (zz < zr[x]) zr[x] = zz; } } } }
}
static long cmp(void) { long n = 0; for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { float a = zb[0][y][x], b = zb[1][y][x]; if ((a > 1e29f) != (b > 1e29f) || (a < 1e29f && fabsf(a - b) > 0.02f)) n++; } return n; }
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "fpcheck DATA_DIR GFP_DIR [N [gun]]\n"); return 2; }
	const char *dir = argv[1], *gdir = argv[2]; int N = argc > 3 ? atoi(argv[3]) : 2000; char p[512]; snprintf(p, sizeof(p), "%sweapons.txt", dir); weapons_load_table(p);
	model_keep_cpu = 1; wvm_fp_off = 1; long bad_all = 0;
	for (int d = 0; d < nwdef; d++) {
		const WDef *D = &wdef[d]; if (argc > 4 && strcmp(argv[4], D->sh)) continue;
		WSlot S; memset(&S, 0, sizeof(S)); if (wslot_load_ptr(&S, d, dir) < 0) continue;
		Model *M = &S.m; snprintf(p, sizeof(p), "%s/vm_%s.gfp", gdir, D->sh); int fr = model_fp_load(M, p); if (fr < 0) { printf("%-10s no light mesh (%d)\n", D->sh, fr); continue; }
		static float pal[64 * 12]; long views = 0, bad = 0, worst = 0, tf = 0, tl = 0; int cl[64], nc = 0, loops[64], nl = 0, acts[64], na = 0;
		for (int c = 0; c < M->nclip && c < 64; c++) if ((M->fp_clips >> c) & 1) { cl[nc++] = c; if (M->clip[c].loop) loops[nl++] = c; else acts[na++] = c; }
		int aim = D->kind != WK_KNIFE && D->kind != WK_GRENADE && S.c[CL_ADS_IDLE] >= 0;
		for (int pass = 0; pass < 2; pass++) {
			long n = pass ? N : 0; if (!pass) for (int k = 0; k < nc; k++) n += ((long)(model_clip_len(M, cl[k]) * 60) + 2) * (aim ? 5 : 1);
			int k = 0, f = 0, a = 0;
			for (long it = 0; it < n; it++) {
				Anim *A = &S.a; float ads, sway_x = 0, sway_y = 0, kick = 0; A->act = -1; A->act_w = 0; A->base_fade = 1; A->base_prev = -1;
				if (!pass) {   // (A) one clip alone, 60 per second; an action clip plays over the idle
					int c = cl[k]; float len = model_clip_len(M, c), t = f / 60.f; if (t > len) t = len; ads = aim ? a * 0.25f : 0;
					if (M->clip[c].loop) { A->base = c; A->base_t = t; } else { A->base = S.c[CL_IDLE]; A->base_t = 0; A->act = c; A->act_t = t; A->act_w = 1; }
					if (++a >= (aim ? 5 : 1)) { a = 0; if (++f > (int)(len * 60) + 1) { f = 0; k++; } }
				} else {     // (B) as the game mixes them
					int c = loops[(int)(frand() * nl) % nl]; A->base = c; A->base_t = frand() * 4; ads = aim ? (frand() < 0.3f ? 0 : frand() < 0.4f ? 1 : frand()) : 0;
					if (frand() < 0.5f) { A->base_prev = loops[(int)(frand() * nl) % nl]; A->base_prev_t = frand() * 4; A->base_fade = frand(); }
					if (na && frand() < 0.6f) { int ac = acts[(int)(frand() * na) % na]; A->act = ac; A->act_t = frand() * model_clip_len(M, ac); A->act_w = frand() < 0.5f ? 1 : frand(); }
					sway_x = (frand() * 2 - 1) * 1.2f; sway_y = (frand() * 2 - 1) * 0.8f; kick = frand() < 0.5f ? frand() : frand() * 2.5f;
				}
				if (!wvm_fp_state(&S)) { printf("%s: state not everyday?\n", D->sh); continue; }
				MBone *P = wpn_vm_pose(&S, ads); model_palette(M, P, pal);
				float th = tanf(D->hip_vfov * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(D->ads_vfov * 0.5f * 3.14159f / 180) * ads, sy = 1 / th, sx = sy * 544.f / 960.f;
				float ka = kick * 0.015f, kc = cosf(ka), ks = sinf(ka), ox = D->hip_off[0] * (1 - ads) + D->ads_off[0] * ads, oy = D->hip_off[1] * (1 - ads) + D->ads_off[1] * ads - 0.3f * kick * ads, oz = D->hip_off[2] * (1 - ads) + D->ads_off[2] * ads + 3.0f * kick * ads;
				float K[16] = { 1, 0, 0, 0, 0, kc, ks, 0, 0, -ks, kc, 0, sway_x * (1 - ads) + ox, sway_y * (1 - ads) + oy, kick * 0.8f * (1 - ads) + oz, 1 };
				draw(M, 0, pal, ads, K, sx, sy, zb[0], &tf); draw(M, 1, pal, ads, K, sx, sy, zb[1], &tl);
				long c = cmp(); views++; if (c) { bad++; if (c > worst) worst = c;
					if (getenv("FPV")) printf("  %ld px: base %s t %.2f | prev %s fade %.2f | act %s t %.2f w %.2f | aim %.2f sway %.2f %.2f kick %.2f\n", c, M->clip[A->base].name, A->base_t, A->base_prev >= 0 ? M->clip[A->base_prev].name : "-", A->base_fade,
						A->act >= 0 ? M->clip[A->act].name : "-", A->act_t, A->act_w, ads, sway_x, sway_y, kick); }
			}
		}
		printf("%-10s %ld views: %ld with a different pixel (worst %ld pixels) | triangles a view: full %ld, light %ld\n", D->sh, views, bad, worst, tf / views, tl / views); fflush(stdout);
		bad_all += bad; wslot_free_ptr(&S);
	}
	return bad_all ? 1 : 0;
}
