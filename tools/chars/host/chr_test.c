// Host preview of engine/src/chr.h: animates a character with the engine's own code and renders it with the host
// rasterizer (vitaGL.h here). Usage:
//   chr_test <chr.gvm> <bank.gab> <set> <scenario> <t> <cam_yaw_deg> <cam_dist> <cam_h> <out.ppm> [lod] [weapon.gvm]
// scenario: idle, zoom, walk_<dir>, run_<dir>, cwalk_<dir>, sprint, sprint_l, crouch, turn, fire, reload, hit, jump, die_<slot>, slot:<name>,
//           melee (knife slash), grab (stealth-kill victim, killed at 0.85 s), cqc (stealth-kill attacker), sgreload (pump shotgun,
//           4 shells), throw (grenade), slide, vault, climb, skill, fall (walked off an edge), aimfire (zoom + fire while walking)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "../../../engine/src/model.h"
#include "../../../engine/src/chr.h"
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void lookat(float *m, const float *e, const float *c) {
	float f[3] = { c[0] - e[0], c[1] - e[1], c[2] - e[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); for (int k = 0; k < 3; k++) f[k] /= l;
	float up[3] = { 0, 0, 1 }, s[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] }; l = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]); for (int k = 0; k < 3; k++) s[k] /= l;
	float u[3] = { s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0] };
	float v[16] = { s[0], u[0], -f[0], 0, s[1], u[1], -f[1], 0, s[2], u[2], -f[2], 0, -(s[0] * e[0] + s[1] * e[1] + s[2] * e[2]), -(u[0] * e[0] + u[1] * e[1] + u[2] * e[2]), f[0] * e[0] + f[1] * e[1] + f[2] * e[2], 1 };
	memcpy(m, v, 64);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static void on_ev(void *u, int kind, const char *str, int foot) { (void)u; printf("  event kind %d %s foot %d\n", kind, str, foot); }
int main(int argc, char **argv) {
	if (argc < 10) { fprintf(stderr, "usage\n"); return 1; }
	model_shader(mk, lg);
	CBank B; int r = cb_load(&B, argv[2]); if (r) { fprintf(stderr, "bank %d\n", r); return 1; }
	Model M; model_keep_cpu = 1; if (model_load(&M, argv[1]) < 0) { fprintf(stderr, "gvm\n"); return 1; } model_prepare_rigid(&M);
	Model W; int haveW = argc > 11 && model_load(&W, argv[11]) == 0;
	CBind bind; int miss = cb_bind(&bind, &B, &M); if (miss) printf("unbound bones: %d\n", miss);
	int set = cb_find_set(&B, argv[3]); if (set < 0) set = 0;
	const char *sc = argv[4]; float T = atof(argv[5]);
	CAnim A; ca_init(&A, &B, set); A.on_event = on_ev;
	CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
	float dirs[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }; const char *dn = "frbl";
	float spd = 0; int dir = -1;
	if (!strncmp(sc, "walk_", 5)) { spd = 220; dir = strchr(dn, sc[5]) - dn; }
	if (!strncmp(sc, "run_", 4)) { spd = 335; dir = strchr(dn, sc[4]) - dn; }
	if (!strncmp(sc, "cwalk_", 6)) { spd = 175; dir = strchr(dn, sc[6]) - dn; in.crouch = 1; }
	if (!strncmp(sc, "diag_", 5)) { spd = 335; dir = 9; }
	if (!strncmp(sc, "sprint", 6)) { spd = 535; dir = 0; in.sprint = 1; if (sc[6] == '_') in.yaw_rate = sc[7] == 'l' ? 2.5f : -2.5f; }
	if (!strcmp(sc, "crouch")) in.crouch = 1;
	if (!strcmp(sc, "zoom")) in.zoom = 1;
	if (!strcmp(sc, "turn")) in.yaw_rate = 3;
	if (!strcmp(sc, "aimup")) { in.aim_pitch = 0.6f; in.zoom = 1; }
	if (!strcmp(sc, "aimdown")) { in.aim_pitch = -0.6f; in.zoom = 1; }
	if (!strcmp(sc, "slide")) { spd = 600; dir = 0; in.slide = 1; in.crouch = 1; }
	if (!strcmp(sc, "aimfire")) { spd = 220; dir = 1; in.zoom = 1; }
	float dt = 1 / 30.f; int fired = 0, jumped = 0;
	for (float t = 0; t < T; t += dt) {
		if (dir >= 0) { if (dir == 9) { in.vf = spd * 0.7071f; in.vr = spd * 0.7071f; } else { in.vf = dirs[dir][0] * spd; in.vr = dirs[dir][1] * spd; } }
		in.jump = 0;
		if (!strcmp(sc, "jump")) { if (!jumped && t >= 0.5f) { jumped = 1; in.jump = 1; in.grounded = 0; } if (jumped && t > 1.3f) in.grounded = 1; in.air_t = in.grounded ? 0 : in.air_t + dt; }
		if (!strcmp(sc, "fire") && t >= 0.5f && fmodf(t, 0.1f) < dt) { ca_fire(&A); fired++; }
		if (!strcmp(sc, "fire") ) in.zoom = 1;
		if (!strcmp(sc, "aimfire") && t >= 0.5f && fmodf(t, 0.1f) < dt) ca_fire(&A);
		if (!strcmp(sc, "melee") && !fired && t >= 0.3f) { ca_play_from(&A, &A.up, "knife", S_FIRE, 0.06f, 0.15f, CA_UPPER); fired = 1; }
		if (!strcmp(sc, "grab") && !fired && t >= 0.3f) { ca_grabbed(&A); fired = 1; }
		if (!strcmp(sc, "grab") && fired == 1 && t >= 0.85f) { ca_die(&A, 0, 1, 3); fired = 2; }
		if (!strcmp(sc, "cqc") && !fired && t >= 0.3f) { ca_cqc_attack(&A); fired = 1; }
		if (!strcmp(sc, "sgreload") && !fired && t >= 0.3f) { printf("shotgun reload %.2f s\n", ca_reload_shells(&A, 4)); fired = 1; }
		if (!strcmp(sc, "throw") && !fired && t >= 0.3f) { ca_play_from(&A, &A.up, "m67", S_THROW, 0.1f, 0.25f, CA_UPPER); fired = 1; }
		if (!strcmp(sc, "skill") && !fired && t >= 0.3f) { ca_upper(&A, S_SKILL, 0.1f, 0.2f, 1); fired = 1; }
		if ((!strcmp(sc, "vault") || !strcmp(sc, "climb")) && t >= 0.3f) { float L = !strcmp(sc, "vault") ? 0.6f : 0.75f, k = (t - 0.3f) / L; in.mantle = k < 1 ? (sc[0] == 'v' ? 2 : 1) : 0; in.mantle_k = k > 1 ? 1 : k; in.grounded = k >= 1; }
		if (!strcmp(sc, "sprintstop")) { int on = t < 1.0f; in.sprint = on; in.vf = on ? 535 : t < 1.1f ? 535 * (1.1f - t) / 0.1f : 0; in.vr = 0; }
		if (!strcmp(sc, "runstop")) { float v = t < 1.0f ? 335 : 335 * expf(-(t - 1.0f) * 12); in.vf = v < 5 ? 0 : v; in.vr = 0; }   // P_DECEL-like stop
		if (!strcmp(sc, "runstart")) { float v = t < 0.5f ? 0 : 335 * (1 - expf(-(t - 0.5f) * 10)); in.vf = v; in.vr = 0; }
		if (!strcmp(sc, "strafeswitch")) { float side = fmodf(t, 1.6f) < 0.8f ? 1 : -1, k = fmodf(t, 0.8f); float v = 141 * (1 - expf(-k * 10)); in.vr = side * v; in.vf = 0; in.zoom = 1; }
		if (!strcmp(sc, "fall")) { in.grounded = t < 0.3f || t > 1.4f; in.air_t = in.grounded ? 0 : in.air_t + dt; spd = 335; dir = 0; }
		if (!strcmp(sc, "reload") && !fired && t >= 0.3f) { ca_reload(&A, 0); fired = 1; }
		if (!strcmp(sc, "hit") && !fired && t >= 0.3f) { ca_hit(&A, 0); fired = 1; }
		if (!strncmp(sc, "slot:", 5) && !fired && t >= 0.3f) { int s = cb_find_slot(&B, sc + 5); ca_play(&A.up, cb_clip(&B, set, s), 0.1f, 0.2f, CA_FULL, 1); A.up.hold = 1; fired = 1; }
		if (!strncmp(sc, "die_", 4) && !fired && t >= 0.3f) { int s = cb_find_slot(&B, sc + 4); ca_play(&A.death, cb_clip(&B, set, s), 0.1f, 0, CA_FULL, 1); A.death.hold = 1; A.dead = 1; fired = 1; }
		ca_update(&A, &in, dt);
	}
	MBone *P = ca_pose(&A, &in); MBone pm[64]; cb_to_model(&bind, P, pm);
	if (getenv("DUMP")) for (int b = 0; b < B.nb; b++) printf("%2d %-18s q %6.3f %6.3f %6.3f %6.3f p %7.1f %7.1f %7.1f\n", b, B.bone[b], P[b].q[0], P[b].q[1], P[b].q[2], P[b].q[3], P[b].p[0], P[b].p[1], P[b].p[2]);
	if (getenv("NODRAW")) return 0;
	// camera around the character (the character faces -y): yaw 0 = in front of it
	float yaw = atof(argv[6]) * 3.14159f / 180, d = atof(argv[7]), h = atof(argv[8]);
	float eye[3] = { sinf(yaw) * d, -cosf(yaw) * d, h }, ctr[3] = { 0, 0, h > 150 ? 90 : h * 0.75f };
	if (getenv("CTR")) { sscanf(getenv("CTR"), "%f,%f,%f", &ctr[0], &ctr[1], &ctr[2]); for (int k = 0; k < 3; k++) eye[k] += ctr[k] - (k == 2 ? (h > 150 ? 90 : h * 0.75f) : 0); }
	float Pm[16], V[16], VP[16]; persp(Pm, 40 * 3.14159f / 180, 1, 10, 5000); lookat(V, eye, ctr); mul4(VP, Pm, V);
	h_clear(0.40f, 0.45f, 0.50f);
	// floor grid
	const float ld[4] = { 0.3f, -0.7f, 0.65f, 0 }, tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	chr_gpu_skin = getenv("CPUSKIN") ? 0 : 1; CSkin cs; cskin_init(&cs, &M);   // (CPUSKIN=1: the NEON / scalar software path)
	int lod = argc > 10 ? atoi(argv[10]) : 0;
	chr_draw(&M, &cs, lod, VP, pm, ld, tint, nofog, 1);
	if (haveW) chr_draw_rigid(&W, VP, &P[B.grip], ld, tint, nofog);
	h_save(argv[9]);
	printf("%s set %s: %d skinned verts, phase %.2f move %.2f run %.2f dom %d, pixels %ld\n", sc, B.setname[set], skin_verts, A.phase, A.move_w, A.run_w, A.dom_clip, hpix);
	printf("root %.1f %.1f %.1f head %.1f %.1f %.1f grip %.1f %.1f %.1f lfoot %.1f %.1f %.1f\n", P[B.root].p[0], P[B.root].p[1], P[B.root].p[2], P[B.head].p[0], P[B.head].p[1], P[B.head].p[2], P[B.grip].p[0], P[B.grip].p[1], P[B.grip].p[2], P[B.lfoot].p[0], P[B.lfoot].p[1], P[B.lfoot].p[2]);
	return 0;
}
