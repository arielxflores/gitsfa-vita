// b80 host check of the destruction deaths (engine/src/bparts.h, chr.h chr_part_mask / chr_palette): an operator dies a
// destruction death (ca_die kind 2: d_xhead or a limb clip) and is drawn with the part collapsed onto its root joint; the part
// itself (every other palette entry collapsed) is drawn lifted off by (dx, dz) cm, with the enemy mask when masked.
// usage: decap_test <data dir> <chr> <part 1 head .. 5 right leg> <t> <out.ppm> [masked 0/1] [lift cm] [view yaw deg]
// Build: gcc -O2 -w -I. -Istub -o decap_test decap_test.c -lm -lpthread
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
static void view(float *m, const float *p, float yaw, float pitch) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0,
		-(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static const char *FEM[] = { "motoko", "maven", "reiko", "sitara", NULL };
int main(int argc, char **argv) {
	if (argc < 6) { fprintf(stderr, "usage: see decap_test.c\n"); return 1; }
	model_shader(mk, lg); model_keep_cpu = 1;
	const char *dir = argv[1], *stem = argv[2]; int part = atoi(argv[3]); float T = atof(argv[4]); int masked = argc > 6 ? atoi(argv[6]) : 1; float lift = argc > 7 ? atof(argv[7]) : 45, vy = argc > 8 ? atof(argv[8]) : 0;
	int fem = 0; for (int i = 0; FEM[i]; i++) if (!strncmp(FEM[i], stem, strlen(FEM[i]))) fem = 1;
	static CBank B; char p[512]; snprintf(p, sizeof(p), "%s/chr_anim_%c.gab", dir, fem ? 'f' : 'm'); if (cb_load(&B, p)) return 1;
	static Model M, MK; snprintf(p, sizeof(p), "%s/chr_%s.gvm", dir, stem); if (model_load(&M, p) < 0) return 1; model_prepare_rigid(&M);
	snprintf(p, sizeof(p), "%s/mask_%c.gvm", dir, fem ? 'f' : 'm'); int mk_ok = model_load(&MK, p) == 0;
	CBind bind; cb_bind(&bind, &B, &M); int set = cb_find_set(&B, "m4a1");
	static CAnim A; ca_init(&A, &B, set < 0 ? 0 : set); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
	ca_update(&A, &in, 0.5f);
	static const int X[6] = { 0, 1, 0, 0, 0, 0 };
	if (part >= 1) { srand(part == 1 ? 0 : 1000 + part); ca_die(&A, X[part], 0, 2); }
	for (float t = 0; t < T; t += 1 / 30.f) ca_update(&A, &in, 1 / 30.f);
	MBone *P = ca_pose(&A, &in), pose[64]; cb_to_model(&bind, P, pose);
	static const char *N[6] = { NULL, "Bip01_Head", "Bip01_L_UpperArm", "Bip01_R_UpperArm", "Bip01_L_Thigh", "Bip01_R_Thigh" };
	int broot = part >= 1 ? cb_find_bone(&B, N[ca_xlast ? ca_xlast : part]) : -1, root = -1;
	unsigned long long cm = chr_part_mask(&M, &bind, &B, broot, &root);
	printf("clip part %d (ca_xlast %d) root bank %d model %d, %d of %d palette entries\n", part, ca_xlast, broot, root, __builtin_popcountll(cm), M.npal);
	glViewport(0, 0, 960, 544); h_clear(0.25f, 0.25f, 0.28f);
	float Pm[16], V[16], VP[16], eye[3] = { 260 * cosf(vy * 3.14159f / 180), -260 * sinf(vy * 3.14159f / 180), 140 };
	persp(Pm, 50 * 3.14159f / 180, 960.f / 544.f, 10, 4000); view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(95 - eye[2], 260)); mul4(VP, Pm, V);
	const float tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 }, ld[4] = { 0.3f, -0.7f, 0.65f, 0 };
	float Mm[16] = { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, MVP[16]; mul4(MVP, VP, Mm);   // facing -y (towards the camera at yaw 0: +x)
	chr_gpu_skin = 0;
	static CSkin body, prt; cskin_init(&body, &M); body.cmask = cm; body.croot = root; body.cinv = 0;
	chr_draw(&M, &body, 0, MVP, pose, ld, tint, nofog, 1);
	if (cm) {
		cskin_init(&prt, &M); prt.cmask = cm; prt.croot = root; prt.cinv = 1;
		float Ml[16] = { 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, -lift * 0.6f, lift, 1 }, MVP2[16]; mul4(MVP2, VP, Ml);
		chr_draw(&M, &prt, 0, MVP2, pose, ld, tint, nofog, 1);
		const float *mat = model_attach(&M, "mask"); int hb = model_find_bone(&M, "Bip01_Head");
		if (part == 1 && masked && mk_ok && mat && hb >= 0) { MBone Bh = pose[hb]; float o[3]; quat_rotate(Bh.q, mat, o); for (int j = 0; j < 3; j++) Bh.p[j] += o[j]; chr_draw_rigid(&MK, MVP2, &Bh, ld, tint, nofog);
			printf("mask at %.1f %.1f %.1f (head bone space)\n", mat[0], mat[1], mat[2]); }
	}
	FILE *f = fopen(argv[5], "wb"); fprintf(f, "P6 %d %d 255\n", 960, 544);
	for (int y = 0; y < 544; y++) for (int x = 0; x < 960; x++) for (int k = 0; k < 3; k++) { float v = hfb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); }
	fclose(f); printf("%ld pixels\n", hpix);
	return 0;
}
