// Host check of the results' team line-up (engine/src/main.c chr_lineup, b58) - Vita3K draws no menu figures. Poses up to 5
// operators as chr_lineup does (slots, facing, spin, each idle started 0.83 s apart) and renders them full screen with this
// folder's host rasterizer; prints the name-tag points (as chr_lineup gives menu.h).
// usage: lineup <data dir> <t> <spin deg> <eye height> <distance> <look-at height> <fov> <out.ppm> <chr:gun> [chr:gun ...]
//   chr: model stem (motoko, bato ...), gun: weapon.h short name or - ; writes out.ppm (colour) + out.ppm.a (coverage)
// Build: gcc -O2 -w -I. -Istub -o lineup lineup.c -lm -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
static void mul4(float *o, const float *a, const float *b) {
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
		float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static void persp(float *m, float fovy, float asp, float n, float f) {
	float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64);
	m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}
static void view(float *m, const float *p, float yaw, float pitch, float *fwd) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	memcpy(fwd, F, 12);
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0,
		-(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static const char *FEM[] = { "motoko", "maven", "reiko", "sitara", NULL };
int main(int argc, char **argv) {
	if (argc < 10) { fprintf(stderr, "usage: see lineup.c\n"); return 1; }
	model_shader(mk, lg); model_keep_cpu = 1;
	const char *dir = argv[1]; float T = atof(argv[2]), spin = atof(argv[3]), cam[4] = { atof(argv[4]), atof(argv[5]), atof(argv[6]), atof(argv[7]) };
	static const float SLOT[5][2] = { { 0, 0 }, { -95, 50 }, { 95, 50 }, { -190, 100 }, { 190, 100 } };
	int n = argc - 9; if (n > 5) n = 5;
	glViewport(0, 0, 960, 544); h_clear(0, 0, 0);
	float Pm[16], V[16], VP[16], fwd[3], d = cam[1], eye[3] = { 0, -d, cam[0] };
	persp(Pm, cam[3] * 3.14159f / 180, 960.f / 544.f, 10, 4000); view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(cam[2] - cam[0], d), fwd); mul4(VP, Pm, V);
	static CBank B[2]; char p[512]; snprintf(p, sizeof(p), "%s/chr_anim_m.gab", dir); if (cb_load(&B[0], p)) return 1; snprintf(p, sizeof(p), "%s/chr_anim_f.gab", dir); if (cb_load(&B[1], p)) return 1;
	const float tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	for (int k = 0; k < n; k++) {
		char stem[64], gun[32]; const char *a = argv[9 + k], *c = strchr(a, ':'); snprintf(stem, sizeof(stem), "%.*s", c ? (int)(c - a) : (int)strlen(a), a); snprintf(gun, sizeof(gun), "%s", c ? c + 1 : "-");
		int fem = 0; for (int i = 0; FEM[i]; i++) if (!strncmp(FEM[i], stem, strlen(FEM[i]))) fem = 1;   // (b71: skins chr_<op>_<skin>)
		static Model M[5], W[5]; snprintf(p, sizeof(p), "%s/chr_%s.gvm", dir, stem); if (model_load(&M[k], p) < 0) { fprintf(stderr, "%s\n", p); return 1; } model_prepare_rigid(&M[k]);
		snprintf(p, sizeof(p), "%s/w3_%s.gvm", dir, gun); int haveW = strcmp(gun, "-") && model_load(&W[k], p) == 0;
		CBind bind; cb_bind(&bind, &B[fem], &M[k]); int set = cb_find_set(&B[fem], gun);
		static CAnim A; ca_init(&A, &B[fem], set < 0 ? 0 : set); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
		ca_update(&A, &in, 0.83f * k); for (float t = 0; t < T; t += 1 / 30.f) ca_update(&A, &in, 1 / 30.f);
		MBone *P = ca_pose(&A, &in), pose[64]; cb_to_model(&bind, P, pose);
		float x = SLOT[k][0], y = SLOT[k][1], r = 0.7f * atan2f(-x, d + y) + spin * 3.14159f / 180, cr = cosf(r), sr = sinf(r);
		float Mm[16] = { cr, sr, 0, 0, -sr, cr, 0, 0, 0, 0, 1, 0, x, y, 0, 1 }, MVP[16]; mul4(MVP, VP, Mm);
		float ry = -r, cy_ = cosf(ry), sy_ = sinf(ry), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 };
		int mk_ = getenv("LINEUP_MASK") && atoi(getenv("LINEUP_MASK")); static Model MK[2]; static int mk_ok[2];   // b71: the enemy mask (bots.h bot_mask_draw)
		if (mk_ && !mk_ok[fem]) { snprintf(p, sizeof(p), "%s/mask_%c.gvm", dir, fem ? 'f' : 'm'); mk_ok[fem] = model_load(&MK[fem], p) == 0 ? 1 : -1; }
		const float *mat = model_attach(&M[k], "mask"); int hb = model_find_bone(&M[k], "Bip01_Head"); md_masked = mk_ && mk_ok[fem] > 0 && mat && hb >= 0;
		chr_gpu_skin = 0; static CSkin cs[5]; cskin_init(&cs[k], &M[k]); chr_draw(&M[k], &cs[k], 0, MVP, pose, ld, tint, nofog, 1);
		if (md_masked) { MBone Bh = pose[hb]; float o[3]; quat_rotate(Bh.q, mat, o); for (int j = 0; j < 3; j++) Bh.p[j] += o[j]; chr_draw_rigid(&MK[fem], MVP, &Bh, ld, tint, nofog); }
		md_masked = 0;
		if (haveW) chr_draw_rigid(&W[k], MVP, &P[B[fem].grip], ld, tint, nofog);
		float hp[4] = { x, y, 182, 1 }, cc[4]; for (int i = 0; i < 4; i++) cc[i] = VP[i] * hp[0] + VP[4 + i] * hp[1] + VP[8 + i] * hp[2] + VP[12 + i];
		printf("tag %d %s: %.0f %.0f\n", k, stem, (cc[0] / cc[3] * 0.5f + 0.5f) * 960, (0.5f - cc[1] / cc[3] * 0.5f) * 544);
	}
	char pa[512]; snprintf(pa, sizeof(pa), "%s.a", argv[8]); FILE *f = fopen(argv[8], "wb"), *g = fopen(pa, "wb");
	fprintf(f, "P6 %d %d 255\n", 960, 544); fprintf(g, "P5 %d %d 255\n", 960, 544);
	for (int y = 0; y < 544; y++) for (int x = 0; x < 960; x++) { for (int k = 0; k < 3; k++) { float v = hfb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); } fputc(hcov[y][x] ? 255 : 0, g); }
	fclose(f); fclose(g); printf("%ld pixels\n", hpix);
	return 0;
}
