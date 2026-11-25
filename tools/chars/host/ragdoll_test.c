// b82 host check of engine/src/ragdoll.h: an operator killed by a hit (death clip by its side, then the ragdoll: soft keyframing
// RD_SOFT s, then physics) on a flat floor with a wall behind, drawn from the side at a few moments into one strip, plus the
// numbers: when it comes to rest, bone length errors, floor penetration, how far it slid.
// usage: ragdoll_test <data dir> <chr> <side 0 front 1 back 2 left 3 right> <out.ppm> [hit cm/s] [wall cm behind, 0 none]
// Build: gcc -O2 -w -I. -Istub -o ragdoll_test ragdoll_test.c -lm -lpthread
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
#include "../../../engine/src/ragdoll.h"
static void mul4(float *o, const float *a, const float *b) {
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void view(float *m, const float *p, float yaw, float pitch) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0, -(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static const char *FEM[] = { "motoko", "maven", "reiko", "sitara", NULL };
static float wall_y = 0; static float pen_max = 0;
static int coll(float *c, float r, float *n) {   // the floor z = 0, a wall at y = wall_y (the body faces -y: the wall is behind it at +y)
	int hit = 0;
	if (c[2] < r) { if (r - c[2] > pen_max) pen_max = r - c[2]; c[2] = r; n[2] = 1; hit = 1; }
	if (wall_y > 0 && c[1] > wall_y - r) { c[1] = wall_y - r; n[1] = -1; hit = 1; }
	return hit;
}
int main(int argc, char **argv) {
	if (argc < 5) { fprintf(stderr, "usage: see ragdoll_test.c\n"); return 1; }
	model_shader(mk, lg); model_keep_cpu = 1;
	const char *dir = argv[1], *stem = argv[2]; int side = atoi(argv[3]); float hitv = argc > 5 ? atof(argv[5]) : 300; wall_y = argc > 6 ? atof(argv[6]) : 0;
	int fem = 0; for (int i = 0; FEM[i]; i++) if (!strncmp(FEM[i], stem, strlen(FEM[i]))) fem = 1;
	static CBank B; char p[512]; snprintf(p, sizeof(p), "%s/chr_anim_%c.gab", dir, fem ? 'f' : 'm'); if (cb_load(&B, p)) return 1;
	static Model M; snprintf(p, sizeof(p), "%s/chr_%s.gvm", dir, stem); if (model_load(&M, p) < 0) return 1; model_prepare_rigid(&M);
	CBind bind; cb_bind(&bind, &B, &M); int set = cb_find_set(&B, "m4a1");
	static CAnim A; ca_init(&A, &B, set < 0 ? 0 : set); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
	for (int f = 0; f < 6; f++) ca_update(&A, &in, 1 / 30.f);
	srand(7); ca_die(&A, 0, side, 0);
	// the hit's push in world (= model here: T identity): from the front pushes towards +y (backwards: the body faces -y)
	static const float HD[4][2] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { -1, 0 } };
	float hit[3] = { HD[side][0] * hitv, HD[side][1] * hitv, 0.45f * hitv }, T[5] = { 1, 0, 0, 0, 0 };
	static Ragdoll R; static MBone P0[CB_MAXB], P1[CB_MAXB], RP[CB_MAXB];
	const float dt = 1 / 60.f;
	MBone *P = ca_pose(&A, &in); memcpy(P1, P, sizeof(MBone) * B.nb); ca_update(&A, &in, dt); P = ca_pose(&A, &in); memcpy(P0, P, sizeof(MBone) * B.nb);
	if (!rd_start(&R, &B, P0, P1, dt, T, hit)) { printf("rd_start failed (bones)\n"); return 2; }
	R.collide = coll;
	static const float SNAP[6] = { 0.1f, 0.3f, 0.6f, 1.0f, 1.6f, 2.6f }; int si = 0;
	glViewport(0, 0, 960, 544); h_clear(0.25f, 0.25f, 0.28f);
	float t = 0, rest_t = -1, slide = 0, p0x = R.x[RD_PELVIS][0], p0y = R.x[RD_PELVIS][1]; double lerr = 0;
	static CSkin sk; cskin_init(&sk, &M); chr_gpu_skin = 0;
	float Pm[16], V[16], VP[16], eye[3] = { 430, 20, 130 }; persp(Pm, 50 * 3.14159f / 180, 960.f / 544.f, 10, 5000); view(V, eye, 3.14159f, -0.12f); mul4(VP, Pm, V);
	for (int f = 0; f < 60 * 3; f++) {
		t += dt; ca_update(&A, &in, dt); MBone *Pa = ca_pose(&A, &in);
		rd_update(&R, dt, Pa);
		if (R.asleep && rest_t < 0) rest_t = t;
		for (int k = 0; k < R.nc; k++) if (R.c[k].kind == 0 && R.c[k].lo == 1 && R.c[k].hi == 1) { float d[3]; rd_sub(R.x[R.c[k].a], R.x[R.c[k].b], d); double e = fabs(rd_len(d) - R.c[k].d); if (e > lerr) lerr = e; }
		if (getenv("RD_TRACE") && f % 6 == 5) printf("  %.2f s pelvis %.0f %.0f %.0f head %.0f %.0f %.0f knees z %.0f %.0f contacts %d\n", t, R.x[RD_PELVIS][0], R.x[RD_PELVIS][1], R.x[RD_PELVIS][2], R.x[RD_HEAD][0], R.x[RD_HEAD][1], R.x[RD_HEAD][2], R.x[RD_LKN][2], R.x[RD_RKN][2], R.contacts);
		if (si < 6 && t >= SNAP[si] - 1e-4f) {
			rd_pose(&R, RP); static MBone mp[64]; cb_to_model(&bind, RP, mp);
			float off = (si - 2.5f) * 125, Mm[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, off, 0, 1 }, MVP[16]; mul4(MVP, VP, Mm);   // side by side along y
			if (getenv("RD_CLOSE")) { if (si != 5) { si++; continue; } float e2[3] = { R.x[RD_PELVIS][0] + 170, R.x[RD_PELVIS][1] + 60, 190 }, V2[16], VP2[16]; view(V2, e2, atan2f(R.x[RD_PELVIS][1] - e2[1], R.x[RD_PELVIS][0] - e2[0]), -0.78f); mul4(VP2, Pm, V2); memcpy(MVP, VP2, 64); }
			const float tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 }, ld[4] = { 0.3f, -0.7f, 0.65f, 0 };
			sk.cur = -1; chr_draw(&M, &sk, 0, MVP, mp, ld, tint, nofog, 1);
			printf("t %.1f: pelvis %.0f %.0f %.0f, head z %.0f, contacts %d%s\n", t, R.x[RD_PELVIS][0], R.x[RD_PELVIS][1], R.x[RD_PELVIS][2], R.x[RD_HEAD][2], R.contacts, R.asleep ? " (at rest)" : "");
			si++;
		}
	}
	slide = sqrtf((R.x[RD_PELVIS][0] - p0x) * (R.x[RD_PELVIS][0] - p0x) + (R.x[RD_PELVIS][1] - p0y) * (R.x[RD_PELVIS][1] - p0y));
	printf("RESULT side %d hit %.0f: at rest after %.2f s, bone length error max %.2f cm, floor penetration max %.2f cm, pelvis travelled %.0f cm\n", side, hitv, rest_t, lerr, pen_max, slide);
	FILE *f = fopen(argv[4], "wb"); fprintf(f, "P6 %d %d 255\n", 960, 544);
	for (int y = 0; y < 544; y++) for (int x = 0; x < 960; x++) for (int k = 0; k < 3; k++) { float v = hfb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); }
	fclose(f); return 0;
}
