// Host simulation of engine/src/bots.h on a real map (collision + navigation mesh): the bots' AI, physics and chr.h
// animation run as in the game; the host rasterizer draws chase-camera frames of one bot (flat-shaded collision mesh
// around it), and the bots' animation states and sounds are logged. Built by bot_sim.sh.
// Usage: bot_sim <data dir> <maps dir> <map stem> <seconds> <focus bot> <out prefix> [frame times... | auto]
// "auto": frames of whichever bot starts a slide, vault, climb, knife slash or death (the first 3 of each kind), 0.1 / 0.35 /
// 0.7 s after it starts, named <out prefix>_<kind>_<n>_<k>.ppm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "player.h"
static int bot_cam_hidden;
#include "bots.h"
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
static void draw_world(const float *VP, V3 c) {
	htex_bound = 0; float tint0[4]; memcpy(tint0, hu_tint, 16); float fog0[4]; memcpy(fog0, hu_fog, 16);
	hu_tint[0] = hu_tint[1] = hu_tint[2] = hu_tint[3] = 1; hu_fog[3] = 0; hblend = 0; hdmask = 1;
	for (int i = 0; i < nctri; i++) {
		Tri *t = &ctris[i]; if (!(t->flags & (TF_PLAYER | TF_BULLET))) continue;
		V3 m = vmul(vadd(t->a, vadd(t->b, t->c)), 1 / 3.f), d = vsub(m, c); if (vdot(d, d) > 2600.f * 2600.f) continue;
		HV v[3]; const V3 *p[3] = { &t->a, &t->b, &t->c };
		float sh = 0.35f + 0.5f * fabsf(t->n.z) + 0.15f * fabsf(t->n.x);
		for (int k = 0; k < 3; k++) { float q[4] = { p[k]->x, p[k]->y, p[k]->z, 1 }; for (int r = 0; r < 4; r++) v[k].pos[r] = VP[r] * q[0] + VP[4 + r] * q[1] + VP[8 + r] * q[2] + VP[12 + r]; v[k].uv[0] = v[k].uv[1] = 0; v[k].sh = sh * 0.55f; v[k].fog = 0; }
		h_tri(&v[0], &v[1], &v[2]);
	}
	memcpy(hu_tint, tint0, 16); memcpy(hu_fog, fog0, 16);
}
// a stand-in for skills.h: every bot's personal skill goes active every 20 s (tests the activation gesture)
static float sim_t; static float sim_sk_t(int i) { float k = fmodf(sim_t + i * 4.f, 20.f); return k < 6 ? k : -1; }
int main(int argc, char **argv) {
	if (argc < 7) { fprintf(stderr, "usage\n"); return 1; }
	const char *data = argv[1], *mdir = argv[2], *stem = argv[3]; float secs = atof(argv[4]); int focus = atoi(argv[5]); const char *out = argv[6];
	char p[256];
	snprintf(p, sizeof(p), "%s/%s.gcol", mdir, stem); if (load_col(p) < 0) { fprintf(stderr, "no col %s\n", p); return 1; }
	snprintf(p, sizeof(p), "%s/%s.gnav", mdir, stem); if (nav_load(p) < 0) fprintf(stderr, "no nav\n");
	snprintf(p, sizeof(p), "%s/%s.glgt", mdir, stem); light_load(p);
	model_shader(mk, lg);
	static const char *CHARS[] = { "bato", "motoko", "saito", "togusa", "ishikawa", "borma", "paz", "azuma", "kuro", "maven", "reiko", "sitara" };
	for (int i = 0; i < 12; i++) { snprintf(p, sizeof(p), "%s/chr_%s.gvm", data, CHARS[i]); int r = chrt_load(p, CHARS[i]); if (r < 0) printf("character %s: %d\n", CHARS[i], r); }
	snprintf(p, sizeof(p), "%s.sounds.txt", out); snd_log = fopen(p, "w");
	srand(7);
	BotTarget T = { v3(spawns[0].x, spawns[0].y, spawns[0].z + 5), v3(0, 0, 0), 1 };
	if (bots_init(5, &T) < 0) { fprintf(stderr, "bots_init failed (%d characters)\n", nchrt); return 1; }
	for (int i = 0; i < nbot; i++) { bots[i].team = i < 2 ? 0 : 1; bot_spawn(&bots[i], &T); }
	for (int i = 0; i < nbot; i++) printf("bot %d %s weapon %s set %s\n", i, chrt[bots[i].ct].name, BOT_WPN[bots[i].wpn].sh, bots[i].ca.B->setname[bots[i].ca.set]);
	float dt = 1 / 30.f; int nf = (int)(secs / dt), next_shot = 7; long long anim_us = 0; int nframes = 0;
	int n_slide = 0, n_vault = 0, n_climb = 0, n_melee = 0, n_crouch = 0, n_jump = 0, n_skill = 0; bot_sk_active_t = sim_sk_t;
	int autocap = argc > 7 && !strcmp(argv[7], "auto"); struct { int bot, kind, n; float t0; } cap[64]; int ncap = 0, kcount[5] = { 0 };
	static const char *KN[5] = { "slide", "vault", "climb", "knife", "death" }; int was_dead[NBOT_MAX] = { 0 };
	for (int f = 0; f < nf; f++) {
		float t = f * dt; snd_t = t;
		// the "player": walks a slow circle around the first spawn
		T.pos = v3(spawns[0].x + 300 * cosf(t * 0.2f), spawns[0].y + 300 * sinf(t * 0.2f), spawns[0].z + 5);
		Bot *fb = &bots[focus % nbot];
		lis_pos = vsub(fb->pl.pos, v3(cosf(fb->pl.yaw) * 350, sinf(fb->pl.yaw) * 350, -180)); lis_yaw = fb->pl.yaw;
		if (getenv("STEALTH")) {   // the weapons agent's stealth kill as wpn.h does it: hold the victim, the player right behind it, the stab 0.55 s later
			Bot *v = &bots[2]; if (t >= 5.0f && t < 5.0f + dt) { v->hold = 1; v->hold_yaw = v->pl.yaw; v->hold_pitch = 0; }
			if (t >= 5.0f && v->state != BS_DEAD) T.pos = v3(v->pl.pos.x - cosf(v->pl.yaw) * 120, v->pl.pos.y - sinf(v->pl.yaw) * 120, v->pl.pos.z);
			if (t >= 5.55f && t < 5.55f + dt) { v->hold = 0; bot_hit_info(51, v3(cosf(v->pl.yaw), sinf(v->pl.yaw), 0)); bots_damage(2, 999, 0); }
			if (t >= 4.9f && t < 8.0f && f % 3 == 0) printf("stealth t %.2f: bot 2 %s grabbed %d hold %d anim %s\n", t, bot_state_name[v->state], v->grabbed, v->hold, bot_anim_name(v));
		}
		sim_t = t; bot_anim_us = 0; bots_update(dt, &T); anim_us += bot_anim_us; nframes++;
		for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; int e = b->pl.events; n_slide += !!(e & PE_SLIDE); n_vault += !!(e & PE_VAULT); n_climb += !!(e & PE_CLIMB); n_crouch += !!(e & PE_CROUCH); n_jump += !!(e & PE_JUMP);
			n_melee += b->melee_t >= 0 && b->melee_t < dt * 1.5f; n_skill += b->ca.up.clip >= 0 && b->ca.up.clip == ca_c(&b->ca, S_SKILL) && b->ca.up.t < dt * 1.5f;
			if (autocap) { int k = (e & PE_SLIDE) ? 0 : (e & PE_VAULT) ? 1 : (e & PE_CLIMB) ? 2 : (b->melee_t >= 0 && b->melee_t < dt * 1.5f) ? 3 : (b->state == BS_DEAD && !was_dead[i]) ? 4 : -1;
				if (k >= 0 && kcount[k] < 3 && ncap < 64) { cap[ncap].bot = i; cap[ncap].kind = k; cap[ncap].n = kcount[k]++; cap[ncap].t0 = t; ncap++; } was_dead[i] = b->state == BS_DEAD; } }
		for (int c = 0; c < ncap; c++) for (int k = 0; k < 3; k++) { static const float DT[3] = { 0.1f, 0.35f, 0.7f };
			if (fabsf(t - cap[c].t0 - DT[k]) >= dt * 0.5f) continue;
			Bot *cb = &bots[cap[c].bot]; V3 cc = cb->pl.pos; float yaw = cb->pl.yaw + 2.2f;
			float eye[3] = { cc.x + cosf(yaw) * 300, cc.y + sinf(yaw) * 300, cc.z + 170 }, ctr[3] = { cc.x, cc.y, cc.z + 80 };
			float P[16], V[16], VP[16]; persp(P, 60 * 3.14159f / 180, 1, 10, 9000); lookat(V, eye, ctr); mul4(VP, P, V);
			V3 lp = lis_pos; lis_pos = v3(eye[0], eye[1], eye[2]); h_clear(0.30f, 0.33f, 0.37f); draw_world(VP, cc);
			const float fogc[4] = { 0, 0, 0, 0 }; bots_draw(VP, fogc, mul4); lis_pos = lp;
			snprintf(p, sizeof(p), "%s_%s_%d_%d.ppm", out, KN[cap[c].kind], cap[c].n, k); h_save(p); }
		if (f % 15 == 0) { printf("t %5.1f", t); for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; float hs = sqrtf(b->pl.vel.x * b->pl.vel.x + b->pl.vel.y * b->pl.vel.y);
			printf(" | %d %s %s %3.0f %s r%.2f m%.2f d%.0f", i, bot_state_name[b->state], b->pl.crouched ? "C" : "", hs, bot_anim_name(b), b->ca.run_w, b->ca.move_w, b->ca.dir_ang * 57.3f); } printf("\n"); }
		int shoot = 0; for (int k = next_shot; k < argc; k++) if (fabsf(atof(argv[k]) - t) < dt * 0.5f) shoot = 1;
		if (shoot) {
			V3 c = fb->pl.pos; float yaw = fb->pl.yaw + 2.4f;   // camera behind-left of the bot
			float eye[3] = { c.x + cosf(yaw) * 330, c.y + sinf(yaw) * 330, c.z + 175 }, ctr[3] = { c.x, c.y, c.z + 95 };
			float P[16], V[16], VP[16]; persp(P, 60 * 3.14159f / 180, 1, 10, 9000); lookat(V, eye, ctr); mul4(VP, P, V);
			lis_pos = v3(eye[0], eye[1], eye[2]);
			h_clear(0.30f, 0.33f, 0.37f); draw_world(VP, c);
			const float fogc[4] = { 0, 0, 0, 0 };
			bots_draw(VP, fogc, mul4);
			snprintf(p, sizeof(p), "%s_%05.2f.ppm", out, t); h_save(p);
		}
	}
	printf("animation: %.3f ms/frame for %d bots (host)\n", anim_us / 1000.0 / nframes, nbot);
	printf("events: %d slides, %d vaults, %d climbs, %d crouches, %d jumps, %d knife slashes, %d skill gestures\n", n_slide, n_vault, n_climb, n_crouch, n_jump, n_melee, n_skill);
	return 0;
}
