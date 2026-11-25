// Host check of the menus' 3D operator (engine/src/main.c chr_preview) - Vita3K does not draw the menu figures, so the
// emulator cannot show them. Poses the figure the way chr_preview does and renders it with this folder's host rasterizer
// (vitaGL.h) at a screen's preview rectangle and camera (menu_fe.h menu_fe_preview / menu.h menu_results_preview):
//   new (ui b50): the bank's idle of the weapon's animation set (CAnim, as the bots), the w3 gun on the Grip bone
//   old (b49):    the model's own "idle" clip, no gun
// usage: menufig <chr.gvm> <bank.gab> <set> <w3.gvm|-> <old|new> <t> <rx> <ry> <rw> <rh> <cam0..cam4> <out.ppm>
//   writes a rw x rh RGBA PPM pair: out.ppm (colour) and out.ppm.a (coverage), for tools/chars/host/menufig.sh to lay
//   over a host menu capture. Build: gcc -O2 -w -I. -Istub -o menufig menufig.c -lm -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
// main.c's camera helpers (column-major, z up), verbatim
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
int main(int argc, char **argv) {
	if (argc < 17) { fprintf(stderr, "usage: see menufig.c\n"); return 1; }
	model_shader(mk, lg);
	CBank B; if (cb_load(&B, argv[2])) { fprintf(stderr, "bank\n"); return 1; }
	Model M; model_keep_cpu = 1; if (model_load(&M, argv[1]) < 0) { fprintf(stderr, "gvm\n"); return 1; } model_prepare_rigid(&M);
	Model W; int haveW = strcmp(argv[4], "-") && model_load(&W, argv[4]) == 0;
	int newp = !strcmp(argv[5], "new"); float T = atof(argv[6]);
	float rect[4], cam[5]; for (int k = 0; k < 4; k++) rect[k] = atof(argv[7 + k]); for (int k = 0; k < 5; k++) cam[k] = atof(argv[11 + k]);
	MBone pose[64]; MBone *P = NULL;
	if (!strncmp(argv[5], "og", 2)) {   // b63: the out-of-game bank (chr_og_*.gab): set argv[3] ("op.azuma"), the choice pose then its idle
		CBind bind; cb_bind(&bind, &B, &M); int set = cb_find_set(&B, argv[3]); if (set < 0) { fprintf(stderr, "no set %s\n", argv[3]); return 1; }
		int res = !strcmp(argv[3], "result");
		const char *s0 = getenv("OG_C0"), *s1 = getenv("OG_C1");   // b77: any two slots of the set (results: res_win -> res_win_idle by default)
		int c0 = cb_clip(&B, set, cb_find_slot(&B, s0 ? s0 : res ? "res_win" : "choice")), c1 = cb_clip(&B, set, cb_find_slot(&B, s1 ? s1 : res ? "res_win_idle" : "idle"));
		static CLoc L[CB_MAXB], Tt[CB_MAXB]; static MBone Pm_[CB_MAXB]; cb_pose_seq(&B, c0, c1, T, 0.3f, L, Tt, Pm_); P = Pm_; cb_to_model(&bind, P, pose);
	} else if (newp) {   // chr_preview (ui b50)
		CBind bind; cb_bind(&bind, &B, &M); int set = cb_find_set(&B, argv[3]);
		CAnim A; ca_init(&A, &B, set < 0 ? 0 : set); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
		for (float t = 0; t < T; t += 1 / 30.f) ca_update(&A, &in, 1 / 30.f);
		P = ca_pose(&A, &in); cb_to_model(&bind, P, pose);
	} else {      // chr_preview (b49): the model's own idle clip
		int ci = model_find_clip(&M, "idle"); if (ci < 0) { fprintf(stderr, "no idle\n"); return 1; } model_sample(&M, ci, T, pose); haveW = 0;
	}
	int w = (int)rect[2], h = (int)rect[3];
	glViewport(0, 0, w, h); h_clear(0, 0, 0);
	float Pm[16], V[16], VP[16], fwd[3], yaw = cam[4] * 3.14159f / 180, d = cam[1], eye[3] = { sinf(yaw) * d, -cosf(yaw) * d, cam[0] };
	persp(Pm, cam[3] * 3.14159f / 180, (float)w / h, 10, 3000); view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(cam[2] - cam[0], d), fwd); mul4(VP, Pm, V);
	const float cy_ = cosf(yaw), sy_ = sinf(yaw), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 },   /* b57: as main.c chr_preview */ tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	chr_gpu_skin = 0; CSkin cs; cskin_init(&cs, &M); chr_draw(&M, &cs, 0, VP, pose, ld, tint, nofog, 1);   // (the device's CPU skinning)
	if (haveW) chr_draw_rigid(&W, VP, &P[B.grip], ld, tint, nofog);
	// out: the viewport's pixels (colour + coverage), top row first
	char pa[512]; snprintf(pa, sizeof(pa), "%s.a", argv[16]); FILE *f = fopen(argv[16], "wb"), *g = fopen(pa, "wb");
	fprintf(f, "P6 %d %d 255\n", w, h); fprintf(g, "P5 %d %d 255\n", w, h);
	for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { for (int k = 0; k < 3; k++) { float v = hfb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); } fputc(hcov[y][x] ? 255 : 0, g); }
	fclose(f); fclose(g);
	printf("%s %s: %ld pixels%s\n", argv[1], newp ? "new" : "old", hpix, haveW ? ", gun drawn" : "");
	return 0;
}
