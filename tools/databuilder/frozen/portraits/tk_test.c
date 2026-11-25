// Host test of engine/src/thinktank.h + ttm.h (Think Tanks, Terminal Conquest) on the real data: tk_blue/tk_red.gvm, tk_term.gvm,
// <map>.gttm, the maps' collision / navigation / light grids (build/maps), hud.ghud and the real sound names.
//   1. the models (meshes, rigid / CPU-skinned split, clips, attach points)
//   2. damage model (armour, ocular sensor, blasts, regeneration), the hack (18 s run, 2000 HP, 13 s open, restore, failure),
//      destruction and the wreck, the AI (turning, bursts, grenades, laser, "TARGETED") against a scripted operator
//   3. the Terminal Conquest rules on every .gttm (chain, reboot, capture time, multiplier, final terminal, overtime, time up)
//      and the data (devices / spawns on the floor, spawns outside the areas)
//   4. a scripted 3 v 3 Terminal Conquest match on Underground through the game layer (ttm_frame: captures, rewards, bot
//      call-ins, tanks walking to the terminal, music / radio / voice names)
//   5. cost: tk_update per tank and frame, CPU-skinned vertices, draw calls / triangles of tk_draw
//   6. renders (PPM) of the tanks on the map's collision mesh + HUD captures (tools/menu/raster.py) for the side-by-sides
// run: tools/tank/host/run.sh
#include <stdio.h>
#include <time.h>
#include <stdarg.h>
#include "vitaGL.h"
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"
#include "skills.h"
#include "bots.h"
#include "ttm.h"

// databuilder (frozen copy of tools/tank/host/tk_test.c @5ffb4dd, used for tk_hud.gtx): the data folder at run time (TK_DATA)
static char tk_data[512] = "./";
#define DATA tk_data
static const char *dpath(const char *f) { static char b[4][600]; static int k; k = (k + 1) & 3; snprintf(b[k], sizeof(b[k]), "%s%s", tk_data, f); return b[k]; }
#ifndef MAPS
#define MAPS "/root/gits/build/maps/"
#endif
static const char *OUT = "/tmp/tk_test";
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))
static void logp(const char *f, ...) { (void)f; }
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }

// ---------------------------------------------------------------- actors: 0 = the player (pl), 1 + i = "bot" i (bots[i].pl, not simulated)
static Player pl; static float a_hp[1 + NBOT_MAX]; static int a_alive[1 + NBOT_MAX], n_act = 1; static float a_pitch[1 + NBOT_MAX];
static Player *apl(int a) { return a == 0 ? &pl : &bots[a - 1].pl; }
static int w_n(void) { return n_act; }
static V3 w_pos(int a) { return apl(a)->pos; }
static V3 w_vel(int a) { return apl(a)->vel; }
static float w_yaw(int a) { return apl(a)->yaw; }
static int w_alive(int a) { return a >= 0 && a < n_act && a_alive[a]; }
static V3 w_eye(int a) { V3 p = apl(a)->pos; return v3(p.x, p.y, p.z + P_EYE); }
static V3 w_dir(int a) { float y = apl(a)->yaw, p = a_pitch[a]; return v3(cosf(y) * cosf(p), sinf(y) * cosf(p), sinf(p)); }
static float w_hp(int a) { return a_hp[a]; }
static int w_los(V3 a, V3 b) { return bot_los(a, b); }
typedef struct { int v, att, kind; float dmg; } DmgLog; static DmgLog dlog[8192]; static int ndlog;
static int w_damage(int v, float dmg, int att, int kind) {
	if (ndlog < 8192) { dlog[ndlog].v = v; dlog[ndlog].att = att; dlog[ndlog].kind = kind; dlog[ndlog].dmg = dmg; ndlog++; }
	if (!a_alive[v]) return 0; a_hp[v] -= dmg; if (a_hp[v] <= 0) { a_hp[v] = 0; a_alive[v] = 0; if (v > 0) bots[v - 1].state = BS_DEAD; return 1; } return 0; }
static void w_heal(int a, float add, float cap) { (void)a; (void)add; (void)cap; }
static int w_firing(int a) { (void)a; return 0; }
static int w_bot_info(int a, int *t, int *s, float *l, V3 *ls) { (void)a; (void)t; (void)s; (void)l; (void)ls; return 0; }
static void w_shot(V3 a, V3 b) { (void)a; (void)b; }
static void w_notify(int a, int what, const char *t, int pts) { (void)a; (void)what; (void)t; (void)pts; }
static const SkWorld W = { w_n, w_pos, w_vel, w_yaw, w_alive, w_eye, w_dir, w_hp, w_los, w_damage, w_heal, w_firing, w_bot_info, w_shot, w_notify };
static const char *NAMES[1 + NBOT_MAX] = { "Sitara", "Batou", "Saito", "Motoko", "Paz", "Borma", "Togusa", "Ishikawa", "Azuma" };
static const char *w_name(int a) { return a >= 0 && a <= NBOT_MAX ? NAMES[a] : ""; }
static void actors(int n, const int *team, const V3 *pos) {   // n actors; team[a], pos[a] (feet)
	n_act = n; nbot = n - 1; memset(bots, 0, sizeof(bots));
	for (int a = 0; a < n; a++) { Player *p = apl(a); memset(p, 0, sizeof(*p)); p->pos = pos[a]; p->grounded = 1; p->speed_mul = 1; a_hp[a] = 100; a_alive[a] = 1; a_pitch[a] = 0;
		if (a > 0) { bots[a - 1].team = team[a]; bots[a - 1].state = BS_ROAM; } }
	tk_local_team = team[0];
}
static void face(int a, V3 at) { V3 e = vsub(at, w_eye(a)); apl(a)->yaw = atan2f(e.y, e.x); a_pitch[a] = atan2f(e.z, sqrtf(e.x * e.x + e.y * e.y)); }
// a floor point near (x, y) (ray down from z0)
static int floor_at(float x, float y, float z0, V3 *out) { V3 n; float h = raycast(v3(x, y, z0), v3(0, 0, -1), 3000, NULL, &n); if (h < 0) return 0; *out = v3(x, y, z0 - h); return 1; }

// ---------------------------------------------------------------- events
static int ev_n[16], ev_last_actor[16]; static float ev_t[16], T_now;
static void on_ev(int i, int ev, int a) { (void)i; if (ev > 0 && ev < 16) { ev_n[ev]++; ev_t[ev] = T_now; ev_last_actor[ev] = a; } }
static void ev_reset(void) { memset(ev_n, 0, sizeof(ev_n)); for (int k = 0; k < 16; k++) ev_t[k] = -1; }

// ---------------------------------------------------------------- matrices (main.c conventions, column-major)
static void mul4(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
static void persp(float *m, float fovy, float asp, float n, float f) { float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64); m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); }
static void view(float *m, const float *p, float yaw, float pitch) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0, -(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static float VPm[16], CAM[3];
static void camera(V3 eye, V3 at, float vfov_deg) {
	float P[16], V[16], e[3] = { eye.x, eye.y, eye.z }; V3 d = vsub(at, eye);
	persp(P, vfov_deg * 3.14159f / 180, 960.0f / 544, 10, 12000); view(V, e, atan2f(d.y, d.x), atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y))); mul4(VPm, P, V);
	memcpy(CAM, e, 12); lis_pos = eye; lis_yaw = atan2f(d.y, d.x);
}
// the map around the camera: collision triangles flat-shaded (light grid colour), as tools/chars/host/bot_sim.c
static void draw_world(V3 c, float r) {
	g_bound = 0; int prog = g_prog; g_prog = 0; g_blend = 0; g_dmask = 1; g_depth = 1;
	for (int i = 0; i < nctri; i++) {
		Tri *t = &ctris[i]; if (!(t->flags & (TF_PLAYER | TF_BULLET))) continue;
		V3 m = vmul(vadd(t->a, vadd(t->b, t->c)), 1 / 3.f), d = vsub(m, c); if (vdot(d, d) > r * r) continue;
		float L[3]; light_at(m.x, m.y, m.z + 30, L);
		float sh = 0.45f + 0.35f * fabsf(t->n.z) + 0.2f * fabsf(t->n.x); GV v[3]; const V3 *p[3] = { &t->a, &t->b, &t->c };
		for (int k = 0; k < 3; k++) { float q[4] = { p[k]->x, p[k]->y, p[k]->z, 1 }; for (int rr = 0; rr < 4; rr++) v[k].pos[rr] = VPm[rr] * q[0] + VPm[4 + rr] * q[1] + VPm[8 + rr] * q[2] + VPm[12 + rr];
			v[k].uv[0] = v[k].uv[1] = 0; for (int cc = 0; cc < 3; cc++) v[k].col[cc] = sh * fminf(1.2f, 0.25f + L[cc] * 0.6f); v[k].col[3] = 1; v[k].fog = 0; }
		g_tri(&v[0], &v[1], &v[2], NULL, 0);
	}
	g_prog = prog;
}
static const float FOG0[4] = { 0.5f, 0.55f, 0.6f, 0 };
static void shot3d(const char *name, V3 c, float r) {
	char p[256]; g_clear(0.08f, 0.09f, 0.1f); draw_world(c, r); tk_draw(VPm, FOG0); ttm_draw(VPm, FOG0); snprintf(p, sizeof(p), "%s/%s.ppm", OUT, name); g_save(p);
}
static void marker(V3 p, float r, const float *col) {   // a round marker (debug renders)
	V3 a = v3(p.x - r, p.y, p.z - r), b = v3(p.x + r, p.y, p.z - r), c = v3(p.x + r, p.y, p.z + r), d = v3(p.x - r, p.y, p.z + r); sprite_quad(&a.x, &b.x, &c.x, &d.x, 0, 0, 1, 1, col);
	V3 a2 = v3(p.x, p.y - r, p.z - r), b2 = v3(p.x, p.y + r, p.z - r), c2 = v3(p.x, p.y + r, p.z + r), d2 = v3(p.x, p.y - r, p.z + r); sprite_quad(&a2.x, &b2.x, &c2.x, &d2.x, 0, 0, 1, 1, col);
}
static char cap_name[64];
static void hud_shot_begin(const char *name) { char p[256]; snprintf(cap_name, sizeof(cap_name), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); cap_begin(p); }
static void hud_shot_end(void) { char p[256]; cap_end(); snprintf(p, sizeof(p), "mkdir -p %s/%s.tex", OUT, cap_name); if (system(p)) {} snprintf(p, sizeof(p), "%s/%s.tex", OUT, cap_name); cap_dump_textures(p); }
static void hud_base(int ttm_score_l, int ttm_score_r) {   // the game's HUD under ours (score / clock, HP, ammo) as in main.c
	HudState H; memset(&H, 0, sizeof(H)); H.hp = 100; H.ap = 100; H.intel = 300; H.tier = 2; H.tier_lo = 200; H.tier_hi = 400; H.cool = -1; H.skill_icon = 1;
	H.scoreL = ttm_score_l; H.scoreR = ttm_score_r; H.goal = 4; H.time_s = 715; H.spread = 6; H.mag = 30; H.reserve = 90; H.cap = 30; H.wname = "M4A1"; H.mag_x = 760; H.mag_y = 340;
	hud_use(0); hud_game_draw(&H, 3.3f);
}

// ---------------------------------------------------------------- 1. models
static void test_models(void) {
	printf("== models\n");
	int r = tk_load(DATA); CHECK(r == 0, "tk_load %d", r); if (r) return;
	for (int m = 0; m < 2; m++) { const Model *M = &tk_model[m]; int nv = 0, nvs = 0, ntri = 0, nrig = 0;
		for (int i = 0; i < M->nmesh; i++) { nv += M->mesh[i].nv; ntri += M->mesh[i].ni / 3; if (M->mesh[i].rigid >= 0) nrig++; else nvs += M->mesh[i].nv; }
		printf("  %s: %d bones, %d meshes (%d rigid), %d verts (%d CPU-skinned), %d tris, %d clips, %d attach points\n", m ? "tk_red" : "tk_blue", M->nb, M->nmesh, nrig, nv, nvs, ntri, M->nclip, M->natt);
		CHECK(M->nmesh <= 8, "more than 8 meshes: the skin cache only covers 8"); }
	const Model *B = &tk_model[0]; printf("  clips:"); for (int c = 0; c < TC_N; c++) printf(" %s%s(%.2fs)", TK_CLIP_NAME[c], tk_cl[c] == tk_cl[TC_IDLE] && c != TC_IDLE ? "=idle" : "", model_clip_len(B, tk_cl[c])); printf("\n");
	for (int c = 0; c < TC_N; c++) CHECK(model_find_clip(B, TK_CLIP_NAME[c]) >= 0, "clip %s missing", TK_CLIP_NAME[c]);
	printf("  attach: muzzle %s gren %s eye %s tag %s smoke %s\n", tk_a_muzzle ? "ok" : "-", tk_a_gren ? "ok" : "-", tk_a_eye ? "ok" : "-", tk_a_tag ? "ok" : "-", tk_a_smoke ? "ok" : "-");
	CHECK(tk_a_muzzle && tk_a_eye && tk_a_gren, "attach points");
}

// ---------------------------------------------------------------- 2. the unit
static V3 G0;   // a floor point on Underground (open area)
static void step(float secs, float dt) { for (float t = 0; t < secs - 1e-4f; t += dt) { T_now += dt; tk_update(dt); fx_update(dt); } }
static float shoot(int i, V3 from, V3 at, float dmg, int n) {   // n player bullets from -> at; returns the HP lost
	float hp0 = tk[i].hp; for (int s = 0; s < n; s++) { V3 d = vsub(at, from); float l = sqrtf(vdot(d, d)); d = vmul(d, 1 / l); float t;
		int h = tk_ray(from, d, l + 500, &t); if (h >= 0) tk_damage(h, dmg, 0, vadd(from, vmul(d, t)), d, SKD_BULLET); }
	return hp0 - tk[i].hp;
}
static V3 free_dir(V3 c, float *len) {   // the horizontal direction from c (at 1.3 m) with the most free space
	float best = -1; V3 bd = v3(1, 0, 0);
	for (int q = 0; q < 32; q++) { V3 d = v3(cosf(q * 0.19635f), sinf(q * 0.19635f), 0); float h = raycast(v3(c.x, c.y, c.z + 130), d, 4000, NULL, NULL); if (h < 0) h = 4000; if (h > best) { best = h; bd = d; } }
	if (len) *len = best; return bd;
}
static void test_unit(void) {
	printf("== unit\n");
	tk_on_event = on_ev; tk_goal_fn = NULL;
	int team[2] = { 0, 1 }; V3 pos[2] = { v3(G0.x, G0.y - 600, G0.z + 2), v3(G0.x - 2000, G0.y + 1500, G0.z + 2) };
	actors(2, team, pos); ev_reset(); tk_clear();
	int i = tk_spawn(G0, 0, 1, -1, "Batou", "BatouKoma", TKF_NOAI); CHECK(i >= 0, "spawn"); if (i < 0) return;   // faces +x
	CHECK(ev_n[TKE_DEPLOYED] == 1, "deployed event");
	step(0.5f, 1 / 30.f);
	Tank *k = &tk[i];
	printf("  body spheres z+%.0f %.0f %.0f, eye (%.0f %.0f) z+%.0f, muzzle z+%.0f\n", k->sph[0].z - k->pl.pos.z, k->sph[1].z - k->pl.pos.z, k->sph[2].z - k->pl.pos.z, k->eye.x - k->pl.pos.x, k->eye.y - k->pl.pos.y, k->eye.z - k->pl.pos.z, k->muzzle.z - k->pl.pos.z);
	CHECK(k->eye.x - k->pl.pos.x > 30, "the eye is in front (+x)");
	{ // debug render: the hit spheres (blue), the eye (red), the muzzle (yellow), from the side and from the front
	  static const float R[3] = { 78, 82, 48 }; const float cb[4] = { 0.2f, 0.5f, 1, 0.35f }, cr[4] = { 1, 0.1f, 0.1f, 0.9f }, cy[4] = { 1, 0.9f, 0.1f, 0.9f };
	  for (int view_ = 0; view_ < 2; view_++) {
		if (view_ == 0) camera(v3(k->pl.pos.x + 60, k->pl.pos.y - 420, k->pl.pos.z + 140), v3(k->pl.pos.x + 30, k->pl.pos.y, k->pl.pos.z + 110), 59);
		else camera(v3(k->pl.pos.x + 420, k->pl.pos.y + 40, k->pl.pos.z + 150), v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 110), 59);
		g_clear(0.08f, 0.09f, 0.1f); draw_world(G0, 3000); tk_draw(VPm, FOG0); glDisable(GL_DEPTH_TEST);
		for (int q = 0; q < 3; q++) marker(k->sph[q], R[q], cb); sprite_flush(VPm, sp_blob, 0);
		marker(k->eye, TK_EYE_R, cr); marker(k->muzzle, 10, cy); sprite_flush(VPm, sp_blob, 0); glEnable(GL_DEPTH_TEST);
		char pp[256]; snprintf(pp, sizeof(pp), "%s/dbg_spheres%d.ppm", OUT, view_); g_save(pp); } }
	// armour: 30 rifle hits on the body from the side ~ 30 x 30 x 0.15
	face(0, k->sph[0]); V3 eye = w_eye(0); float lost = shoot(i, eye, k->sph[0], 30, 30);
	printf("  30 rifle hits (30 dmg) on the body from the side: -%.0f HP (%.1f per hit)\n", lost, lost / 30);
	CHECK(NEAR(lost, 30 * 30 * TK_DEF_MUL, 1), "armour: lost %.1f", lost);
	// ocular sensor: hits on it from the front do full damage; the body below it from the front is armour
	pl.pos = v3(G0.x + 700, G0.y, G0.z + 2); eye = w_eye(0);
	lost = shoot(i, eye, k->eye, 30, 5); printf("  5 hits on the ocular sensor from the front: -%.0f HP\n", lost); CHECK(lost > 5 * 30 * 0.9f, "eye: lost %.1f", lost);
	lost = shoot(i, eye, v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 70), 30, 5); printf("  5 hits from the front at the belly (70 cm): -%.0f HP\n", lost); CHECK(lost < 5 * 30 * 0.5f, "belly is armour: %.1f", lost);
	pl.pos = v3(G0.x - 700, G0.y, G0.z + 2); lost = shoot(i, w_eye(0), k->eye, 30, 5); printf("  5 hits aimed at the eye from behind: -%.0f HP\n", lost); CHECK(lost < 5 * 30 * 0.5f, "eye from behind %.1f", lost);
	// blast at 2 m
	float hp0 = k->hp; tk_blast(vadd(k->sph[0], v3(200, 0, 0)), 650, 160, 0); printf("  grenade at 2 m: -%.0f HP\n", hp0 - k->hp);
	CHECK(hp0 - k->hp > 100 && hp0 - k->hp < 260, "blast %.1f", hp0 - k->hp);
	// regeneration after 20 s
	hp0 = k->hp; step(25, 1 / 30.f); printf("  25 s untouched: +%.0f HP (regen from 20 s at %.0f/s)\n", k->hp - hp0, TK_REGEN);
	CHECK(NEAR(k->hp - hp0, 5 * TK_REGEN, 4), "regen %.1f", k->hp - hp0);
	// hack: one press of use next to it; 18 s; -2000 HP; open 13 s; restored
	pl.pos = vadd(k->pl.pos, v3(-200, -170, 0)); face(0, k->sph[0]); eye = w_eye(0);
	float r0 = tk_use(0, eye, w_dir(0), 0, 1 / 30.f); CHECK(r0 == 0, "in reach, not pressed: %g", r0);
	tk_use_prog = r0;
	camera(eye, k->sph[0], 59);
	shot3d("u_prompt", k->pl.pos, 3000); hud_shot_begin("u_prompt"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 3.3f); hud_end(); hud_shot_end();
	shot3d("u_prompt_ea", k->pl.pos, 3000); tk_hud_style = 1; hud_shot_begin("u_prompt_ea"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 3.3f); hud_end(); hud_shot_end(); tk_hud_style = 0;
	ev_reset(); T_now = 0;
	float r1 = tk_use(0, eye, w_dir(0), 1, 1 / 30.f); CHECK(r1 == 1, "press starts the hack: %g", r1); CHECK(k->hacker == 0, "hacker");
	CHECK(ev_n[TKE_HACK_START] == 1, "hack start event");
	CHECK(tk_use(0, eye, w_dir(0), 1, 1 / 30.f) < 0, "a tank being hacked is not offered again");
	step(0.6f, 1 / 30.f); tk_use_prog = -1;
	shot3d("u_success", k->pl.pos, 3000); hud_shot_begin("u_success"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 3.3f); hud_end(); hud_shot_end();
	step(6, 1 / 30.f); pl.pos = vadd(pl.pos, v3(-400, -300, 0));   // the hacker may move about (leash 18 m)
	face(0, k->sph[0]); eye = w_eye(0); camera(eye, k->sph[0], 59);
	shot3d("u_code", k->pl.pos, 3000); hud_shot_begin("u_code"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 9.0f); hud_end(); hud_shot_end();
	shot3d("u_code_ea", k->pl.pos, 3000); tk_hud_style = 1; hud_shot_begin("u_code_ea"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 9.0f); hud_end(); hud_shot_end(); tk_hud_style = 0;
	step(TK_HACK_RUN - 6.6f - 0.1f, 1 / 30.f); hp0 = k->hp; step(0.3f, 1 / 30.f);
	printf("  hack: done at %.2f s (TK_HACK_RUN %.0f), HP %.0f -> %.0f, defences %s\n", ev_t[TKE_HACK_DONE], TK_HACK_RUN, hp0, k->hp, k->def ? "UP" : "down");
	CHECK(ev_n[TKE_HACK_DONE] == 1 && NEAR(ev_t[TKE_HACK_DONE], TK_HACK_RUN, 0.1f), "hack done at %.2f", ev_t[TKE_HACK_DONE]);
	CHECK(NEAR(hp0 - k->hp, TK_HACK_COST, 1), "hack cost %.0f", hp0 - k->hp); CHECK(!k->def, "defences down");
	// the full code a moment after the end (HUD)
	hud_shot_begin("u_code_done"); hud_begin(); hud_base(0, 0); tk_hud(VPm, eye, 9.0f); hud_end(); hud_shot_end();
	// defences down: the red look (render), full damage
	camera(v3(k->pl.pos.x + 480, k->pl.pos.y - 260, k->pl.pos.z + 170), v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 80), 59);
	shot3d("u_open", k->pl.pos, 3000);
	lost = shoot(i, w_eye(0), k->sph[1], 30, 3); CHECK(NEAR(lost, 90, 1), "open: full damage %.1f", lost);
	ev_reset(); T_now = 0; step(TK_OPEN_T + 0.3f, 1 / 30.f);
	printf("  defences back up after %.2f s (TK_OPEN_T %.0f)\n", ev_t[TKE_RESTORED], TK_OPEN_T);
	CHECK(ev_n[TKE_RESTORED] == 1 && k->def, "restored");
	// a failed hack: the hacker walks away
	pl.pos = vadd(k->pl.pos, v3(-200, -170, 0)); face(0, k->sph[0]); ev_reset(); tk_use(0, w_eye(0), w_dir(0), 1, 1 / 30.f); step(3, 1 / 30.f);
	pl.pos = vadd(k->pl.pos, v3(-2500, 0, 0)); step(0.2f, 1 / 30.f); CHECK(ev_n[TKE_HACK_FAIL] == 1 && k->hacker < 0, "hack fails when the hacker leaves");
	// full HP, hack again, then destroy it with rifle fire on the body (from the side)
	k->hp = k->hpmax; pl.pos = vadd(k->pl.pos, v3(-200, -170, 0)); face(0, k->sph[0]); tk_use(0, w_eye(0), w_dir(0), 1, 1 / 30.f); step(TK_HACK_RUN + 0.1f, 1 / 30.f);
	CHECK(!k->def, "second hack");
	pl.pos = v3(G0.x, G0.y - 900, G0.z + 2); ev_reset(); T_now = 0; int shots = 0; while (tk[i].state == TK_LIVE && k->def == 0 && shots < 400) { shoot(i, w_eye(0), k->sph[0], 30, 1); shots++; step(0.1f, 1 / 30.f); }
	printf("  after the hack (HP %.0f -> 2000): %d rifle hits (30 dmg, 600 rpm = %.1f s) %s\n", k->hpmax, shots, shots * 0.1f, tk[i].state == TK_DEAD ? "destroy it" : "do not destroy it before the defences return");
	CHECK(tk[i].state == TK_DEAD && ev_n[TKE_DESTROYED] == 1 && ev_last_actor[TKE_DESTROYED] == 0, "destroyed by the player");
	camera(v3(k->pl.pos.x + 480, k->pl.pos.y - 260, k->pl.pos.z + 170), v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 60), 59);
	step(2.5f, 1 / 30.f); shot3d("u_wreck", k->pl.pos, 3000);
	step(TK_WRECK_T, 1 / 30.f); CHECK(tk[i].state == TK_OFF, "wreck removed");

	// ---- the AI: an enemy tank (team 1, no owner) and the player 15 m away in the most open direction
	printf("== AI\n");
	float fl; V3 fd = free_dir(G0, &fl); float dist = fminf(1500, fl - 200);
	tk_clear(); pos[0] = vadd(G0, vmul(fd, dist)); pos[0].z = G0.z + 2; { V3 f; if (floor_at(pos[0].x, pos[0].y, G0.z + 200, &f)) pos[0] = v3(f.x, f.y, f.z + 2); }
	actors(2, team, pos); ndlog = 0; ev_reset(); T_now = 0; int nl0 = aud_nlog;
	i = tk_spawn(G0, atan2f(-fd.y, -fd.x), 1, -1, "", "Think Tank", 0); k = &tk[i]; float targeted = 0, first_fire = -1; int fired = 0;
	for (int f = 0; f < 30 * 20; f++) { T_now += 1 / 30.f; tk_update(1 / 30.f); if (tk_targeted_t > 0) targeted += 1 / 30.f; if (k->firing && first_fire < 0) first_fire = T_now; fired |= k->firing; a_hp[0] = 100; a_alive[0] = 1; }
	float dmg = 0; int hits = 0; for (int q = 0; q < ndlog; q++) if (dlog[q].v == 0) { dmg += dlog[q].dmg; hits++; }
	int grens = 0, rounds = 0; for (int q = nl0; q < aud_nlog; q++) { if (strstr(aud_log[q], "tachi_gl_fire")) grens++; }
	(void)rounds;
	printf("  20 s vs the player at %.0f m (los %d): yaw %.2f (to the player %.2f), first burst at %.1f s, %d hits (%.0f dmg, %.1f dps), 'TARGETED' %.1f s, grenades %d\n",
	       dist / 100, bot_los(k->eye, v3(pl.pos.x, pl.pos.y, pl.pos.z + 110)), k->pl.yaw, atan2f(pl.pos.y - k->pl.pos.y, pl.pos.x - k->pl.pos.x), first_fire, hits, dmg, dmg / 20, targeted, grens);
	CHECK(fired && hits > 10, "the tank shoots the player"); CHECK(targeted > 5, "TARGETED shown");
	// a firing frame for the render (from behind the player's shoulder)
	for (int f = 0; f < 120 && !(k->firing && k->base == TC_FIRE_LOOP); f++) { tk_update(1 / 30.f); a_hp[0] = 100; a_alive[0] = 1; }
	face(0, k->sph[0]); camera(w_eye(0), k->sph[0], 59); tk_targeted_t = 0.3f;
	shot3d("ai_fire", k->pl.pos, 3500); hud_shot_begin("ai_fire"); hud_begin(); hud_base(0, 0); tk_hud(VPm, w_eye(0), 3.25f); hud_end(); hud_shot_end();
	// both models side by side
	tk_clear(); V3 sd = v3(-fd.y, fd.x, 0); int b0 = tk_spawn(vadd(G0, vmul(sd, -260)), atan2f(-fd.y, -fd.x) + 0.5f, 0, 0, "Sitara", "My Think Tank", TKF_NOAI), r0i = tk_spawn(vadd(G0, vmul(sd, 260)), atan2f(-fd.y, -fd.x) - 0.5f, 1, 1, "Batou", "BatouKoma", TKF_NOAI);
	step(0.3f, 1 / 30.f); CHECK(b0 >= 0 && r0i >= 0, "two tanks");
	camera(vadd(v3(G0.x, G0.y, G0.z + 230), vmul(fd, 850)), v3(G0.x, G0.y, G0.z + 90), 59); shot3d("both", G0, 3000);
	tk_clear();
}

// ---------------------------------------------------------------- 2b. the tutorial's interface (tutorial.h TutTankApi semantics)
static void test_tutorial(void) {
	printf("== tutorial adapter (tk_tut_*)\n");
	int team[1] = { 0 }; V3 pos[1] = { v3(G0.x - 230, G0.y - 150, G0.z + 2) }; actors(1, team, pos); tk_clear(); tk_on_event = NULL; tk_hud_style = 0;
	float p0[3] = { G0.x, G0.y, G0.z }; int h = tk_tut_spawn(p0, 0, 1); CHECK(h >= 0 && tk[h].flags == (TKF_NOAI | TKF_NOREGEN | TKF_NORESTORE) && tk_hud_style == 1, "spawn %d", h);
	float c[3]; tk_tut_pos(h, c); CHECK(NEAR(c[2] - G0.z, 100, 1), "pos is the centre");
	CHECK(tk_tut_progress(h) == 0 && !tk_tut_destroyed(h) && NEAR(tk_tut_health(h), 1, 1e-4f), "fresh");
	step(0.3f, 1 / 30.f); face(0, tk[h].sph[0]);
	// TS_HACK as tutorial.h runs it: near && Square -> hack(h, dt) every frame; done when progress >= 1
	float T = 0, t_done = -1; for (int f = 0; f < 30 * 25 && t_done < 0; f++) { tk_tut_hack(h, 1 / 30.f); step(1 / 30.f, 1 / 30.f); T += 1 / 30.f; if (tk_tut_progress(h) >= 1) t_done = T; }
	printf("  hack(h) each frame: progress 1 after %.2f s, HP %.0f%%\n", t_done, tk_tut_health(h) * 100); CHECK(NEAR(t_done, TK_HACK_RUN, 0.1f), "tutorial hack time %.2f", t_done);
	step(30, 1 / 30.f); CHECK(!tk[h].def, "TKF_NORESTORE: still open after 30 s");
	tk_tut_rearm(h); CHECK(tk[h].def && tk_tut_progress(h) == 0 && NEAR(tk_tut_health(h), 1, 1e-4f), "rearm");
	for (int f = 0; f < 30 * 19; f++) { tk_tut_hack(h, 1 / 30.f); step(1 / 30.f, 1 / 30.f); }
	int shots = 0; while (!tk_tut_destroyed(h) && shots < 300) { shoot(h, w_eye(0), tk[h].sph[0], 30, 1); shots++; step(0.1f, 1 / 30.f); }
	printf("  destroyed with %d rifle hits after the hack; destroyed() %d\n", shots, tk_tut_destroyed(h)); CHECK(tk_tut_destroyed(h), "destroyed");
	step(TK_WRECK_T + 0.5f, 1 / 30.f); CHECK(tk_tut_destroyed(h), "destroyed() stays 1 after the wreck is gone");
	tk_tut_remove(h); CHECK(tk_hud_style == 0, "style back"); tk_clear();
}

// ---------------------------------------------------------------- 3. rules + data
static const char *STEMS[3] = { "underground", "berutalube", "ruinedtokyo" };
static void test_rules(void) {
	printf("== Terminal Conquest rules and data\n");
	for (int mi = 0; mi < 3; mi++) {
		char p[256]; TtmMap M; snprintf(p, sizeof(p), "%s%s.gttm", DATA, STEMS[mi]); int r = ttm_load(&M, p); CHECK(r == 0, "%s load %d", STEMS[mi], r); if (r) continue;
		// the chain: from the start, team 0 capturing moves along next[0] to team 1's final terminal (next[0] < 0) and the other way
		char chain[2][16] = { "", "" };
		for (int team = 0; team < 2; team++) { int t = M.start, n = 0; char *c = chain[team]; c[n++] = M.t[t].letter; while (M.t[t].next[team] >= 0 && n < 10) { t = M.t[t].next[team]; c[n++] = M.t[t].letter; } c[n] = 0; }
		printf("  %-11s %d terminals, start %c; team 0 pushes %s, team 1 pushes %s\n", STEMS[mi], M.n, M.t[M.start].letter, chain[0], chain[1]);
		CHECK((int)strlen(chain[0]) + (int)strlen(chain[1]) - 1 == M.n, "%s: the chain covers every terminal", STEMS[mi]);
		// the data on the map
		snprintf(p, sizeof(p), "%s%s.gcol", MAPS, STEMS[mi]); if (load_col(p) < 0) { CHECK(0, "gcol %s", p); ttm_free(&M); continue; }
		for (int t = 0; t < M.n; t++) { const TtmTerm *T = &M.t[t]; V3 f; int on = floor_at(T->dev[0], T->dev[1], T->dev[2] + 100, &f);
			int in = ttm_in_area(T, v3(T->dev[0] + 150, T->dev[1], T->dev[2])) || ttm_in_area(T, v3(T->dev[0] - 150, T->dev[1], T->dev[2])) || ttm_in_area(T, v3(T->dev[0], T->dev[1] + 150, T->dev[2])) || ttm_in_area(T, v3(T->dev[0], T->dev[1] - 150, T->dev[2]));
			int bad_sp = 0, sp_in = 0; for (int s = 0; s < 2; s++) for (int q = 0; q < T->nsp[s]; q++) { V3 g; const float *S = T->sp[s][q]; if (!floor_at(S[0], S[1], S[2] + 60, &g) || fabsf(g.z - S[2]) > 60) bad_sp++; if (ttm_in_area(T, v3(S[0], S[1], S[2]))) sp_in++; }
			float area = 0; for (int q = 0; q < T->ntri; q++) { const float *A = T->tri + q * 9; area += 0.5f * fabsf((A[3] - A[0]) * (A[7] - A[1]) - (A[6] - A[0]) * (A[4] - A[1])); }
			printf("    %c: device on the floor %s (dz %.0f), next to it in the area %s, area %.0f m2 (%d tris), spawns %d/%d (%d off the floor, %d inside the area)\n",
			       T->letter, on ? "yes" : "NO", on ? T->dev[2] - f.z : 0, in ? "yes" : "NO", area / 1e4f, T->ntri, T->nsp[0], T->nsp[1], bad_sp, sp_in);
			CHECK(on && fabsf(T->dev[2] - f.z) < 40, "%s %c device floor", STEMS[mi], T->letter); CHECK(in, "%s %c area next to the device", STEMS[mi], T->letter);
			CHECK(T->nsp[0] >= 8 && T->nsp[1] >= 8, "%s %c spawns", STEMS[mi], T->letter); CHECK(bad_sp <= 2, "%s %c spawns on the floor (%d bad)", STEMS[mi], T->letter, bad_sp); }
		// a match: team 0 alone in the area each time -> captures every ~37 s (x1); time per capture and the end
		TtmMatch m; ttm_start(&m, &M, 4, 900); int team_in[4] = { -1, -1, -1, -1 }; float T = 0, dt = 1 / 30.f, t_online = -1, t_cap = -1; int caps = 0; unsigned all = 0;
		while (m.state != TS_END && T < 1200) { team_in[0] = m.state == TS_LIVE ? 0 : -1; unsigned ev = ttm_update(&m, team_in, 4, dt); all |= ev; T += dt;
			if (ev & TEV_ONLINE) t_online = T; if (ev & TEV_CAPTURE) { if (!caps) t_cap = T - t_online; caps++; } }
		printf("    team 0 alone on each terminal: first online at %.1f s, first capture %.1f s later, %d captures, winner %d at %.0f s%s\n", 35.0f, t_cap, caps, m.winner, T, (all & TEV_FINAL) ? ", FINAL announced" : "");
		CHECK(NEAR(t_cap, 37, 1), "capture time %.1f", t_cap); CHECK(m.winner == 0, "team 0 wins");
		CHECK(caps == ((int)strlen(chain[0]) < 4 ? (int)strlen(chain[0]) : 4), "captures %d", caps);
		ttm_free(&M);
	}
	// multiplier, contest, overtime, time up (rules only, Underground)
	{ TtmMap M; char p[256]; snprintf(p, sizeof(p), "%sunderground.gttm", DATA); ttm_load(&M, p); TtmMatch m; ttm_start(&m, &M, 4, 900);
	  int ti[4] = { -1, -1, -1, -1 }; float T = 0, dt = 1 / 30.f; int mult_at[4] = { 0 };
	  while (m.state == TS_REBOOT) { ttm_update(&m, ti, 4, dt); T += dt; }
	  for (float t = 0; t < 200; t += dt) { ti[0] = 0; ti[1] = 1; unsigned ev = ttm_update(&m, ti, 4, dt); if (ev & TEV_MULT) mult_at[m.mult] = (int)(t + 0.5f); }   // contested: no movement
	  printf("  contested 200 s: bar %.3f, multiplier x2 at %d s, x3 at %d s\n", m.bar, mult_at[2], mult_at[3]);
	  CHECK(NEAR(m.bar, 0.5f, 1e-4f), "contested bar moves"); CHECK(mult_at[2] == 60 && mult_at[3] == 120, "multiplier steps");
	  ti[1] = -1; float t0 = 0; while (m.state == TS_LIVE && t0 < 60) { ttm_update(&m, ti, 4, dt); t0 += dt; }
	  printf("  then team 0 alone at x3: captured in %.1f s (37 / 3 = 12.3)\n", t0); CHECK(NEAR(t0, 74 * 0.5f / 3, 0.6f), "x3 capture %.1f", t0);
	  // overtime: tie at time up, then the next capture decides
	  ttm_start(&m, &M, 4, 60); memset(ti, -1, sizeof(ti)); unsigned all = 0; T = 0;
	  while (m.state != TS_END && T < 400) { unsigned ev = ttm_update(&m, ti, 4, dt); all |= ev; T += dt; if (m.overtime && m.state == TS_LIVE) ti[2] = 1; }
	  printf("  60 s match, nobody captures: overtime %s, ends at %.0f s, winner %d\n", (all & TEV_OVERTIME) ? "yes" : "no", T, m.winner);
	  CHECK((all & TEV_OVERTIME) && m.winner == 1, "overtime capture decides");
	  ttm_start(&m, &M, 4, 30); memset(ti, -1, sizeof(ti)); T = 0; while (m.state != TS_END && T < 400) { ttm_update(&m, ti, 4, dt); T += dt; }
	  printf("  30 s match, nobody ever in the area: ends at %.0f s (overtime %d s), result %d (2 = draw)\n", T, (int)TTM_OVERTIME, m.winner); CHECK(m.winner == 2, "draw");
	  ttm_free(&M); }
}

// ---------------------------------------------------------------- 4. a scripted match through the game layer
static int feed_n; static char feed_last[128]; static int pts_n; static char pts_last[64];
static void feed(const char *k, int ks, const char *v, int vs, int icon) { (void)ks; (void)vs; (void)icon; feed_n++; snprintf(feed_last, sizeof(feed_last), "%s > %s", k, v); }
static void points(int a, const char *t, int p) { (void)a; pts_n++; snprintf(pts_last, sizeof(pts_last), "%s +%d", t, p); }
static void test_match(void) {
	printf("== scripted Terminal Conquest match on Underground (game layer)\n");
	char p[256]; snprintf(p, sizeof(p), "%sunderground.gcol", MAPS); load_col(p); snprintf(p, sizeof(p), "%sunderground.gnav", MAPS); int nr = nav_load(p); snprintf(p, sizeof(p), "%sunderground.glgt", MAPS); light_load(p);
	printf("  nav %d (%d tris)\n", nr, nnav);
	ttm_feed = feed; ttm_points = points; tk_name_fn = w_name;
	int team[6] = { 0, 0, 0, 1, 1, 1 }; V3 pos[6]; for (int a = 0; a < 6; a++) pos[a] = G0; actors(6, team, pos);
	int r = ttm_begin(DATA, "underground", 4, 900); CHECK(r == 0, "ttm_begin %d", r); if (r) return;
	for (int a = 0; a < 6; a++) { float s[3], y; ttm_spawn_point(team[a], s, &y); apl(a)->pos = v3(s[0], s[1], s[2] + 2); apl(a)->yaw = y; }
	float dt = 1 / 30.f, T = 0; int caps = 0, calls = 0, tanks_seen = 0, placed_shot = 0; float tank_d0[2] = { -1, -1 }, tank_d1[2] = { -1, -1 }; unsigned all = 0; int shots = 0;
	// script: the team that "owns" the phase walks into the area (teleported in steps); the other team stays at its spawns.
	// phase k: team (k & 1 ? 1 : 0) pushes, the other side's tank gets hacked once by the player (actor 0) when it is near.
	for (int f = 0; f < 30 * 900 && tm.state != TS_END; f++) {
		T += dt; T_now = T;
		int push = (caps & 1) ? 1 : 0; const TtmTerm *A = &ttm_map.t[tm.active];
		for (int a = 0; a < 6; a++) { if (!a_alive[a]) { a_alive[a] = 1; a_hp[a] = 100; if (a) bots[a - 1].state = BS_ROAM; }
			Player *P = apl(a); float tgt[3];
			if (team[a] == push && tm.state == TS_LIVE) { if (f % 90 == a) { ttm_area_point(A, tgt); P->pos = v3(tgt[0], tgt[1], tgt[2] + 2); } }
			else if (f % 300 == a * 7 && tm.state != TS_LIVE) { float s[3], y; ttm_spawn_point(team[a], s, &y); P->pos = v3(s[0], s[1], s[2] + 2); } }
		if (tm.ready[0]) a_pitch[0] = -0.38f; else a_pitch[0] = 0;
		V3 e = w_eye(0), d = w_dir(0); int use = 0, call = tm.ready[0] && (f % 60 == 0);
		for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].team != team[0] && tk[k].def && tk[k].hacker < 0) { V3 q = vsub(tk[k].sph[0], e); if (vdot(q, q) < 600.f * 600.f) { face(0, tk[k].sph[0]); d = w_dir(0); use = 1; } }
		// everyone shoots the enemy tanks a little (bullets 30 dmg, 3/s per actor in range)
		for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE) for (int a = 0; a < 6; a++) if (team[a] != tk[k].team && f % 10 == a) { V3 q = vsub(tk[k].sph[1], w_eye(a)); float l = sqrtf(vdot(q, q));
			if (l < 2500 && bot_los(w_eye(a), tk[k].sph[1])) { tk_damage(k, 30, a, tk[k].sph[1], vmul(q, 1 / l), SKD_BULLET); shots++; } }
		tk_update(dt); fx_update(dt);
		unsigned ev = ttm_frame(dt, e, d, use, call, tm.ready[0] && ttm_placing && (f % 60 == 30), 1); all |= ev;
		if (ev & TEV_CAPTURE) { caps++; printf("    %5.1f s: team %d captured (%d - %d) by %s%s\n", T, tm.last_team, tm.score[0], tm.score[1], w_name(tm.last_actor), tm.state == TS_END ? " - match over" : ""); }
		if (ev & TEV_ONLINE) printf("    %5.1f s: terminal %c online%s\n", T, ttm_map.t[tm.active].letter, tm.final_team >= 0 ? " (FINAL)" : "");
		for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE) { int tt = tk[k].team; float dd = sqrtf((tk[k].pl.pos.x - A->dev[0]) * (tk[k].pl.pos.x - A->dev[0]) + (tk[k].pl.pos.y - A->dev[1]) * (tk[k].pl.pos.y - A->dev[1]));
			if (tank_d0[tt] < 0) { tank_d0[tt] = dd; tanks_seen++; } tank_d1[tt] = dd; }
		static int last_tk; int ntk = tk_count(0) + tk_count(1); if (ntk > last_tk) { calls++; for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_DROP) printf("    %5.1f s: %s called in by %s (team %d)\n", T, tk[k].name, tk[k].owner_name, tk[k].team); } last_tk = ntk;
		if (ttm_placing && ttm_place_ok && ttm_place_t > 0.4f && !placed_shot) { placed_shot = 1; V3 ce = w_eye(0); camera(ce, ttm_place_pt, 59);
			printf("    %5.1f s: placing the Tachikoma at (%.0f %.0f %.0f), %.0f m ahead\n", T, ttm_place_pt.x, ttm_place_pt.y, ttm_place_pt.z, sqrtf(vdot(vsub(ttm_place_pt, ce), vsub(ttm_place_pt, ce))) / 100);
			shot3d("m_place", ce, 3500); hud_shot_begin("m_place"); hud_begin(); hud_base(tm.score[0], tm.score[1]); tk_hud(VPm, ce, T); float c3[3] = { ce.x, ce.y, ce.z }; ttm_hud(VPm, c3, T); hud_end(); ttm_overlay(); hud_shot_end(); }
		if (f == 30 * 120) { // a frame for the HUD capture: the live bar with a capturer + panels
			V3 ce = w_eye(0); camera(ce, v3(A->dev[0], A->dev[1], A->dev[2] + 100), 59); shot3d("m_live", ce, 3500);
			hud_shot_begin("m_live"); hud_begin(); hud_base(tm.score[0], tm.score[1]); tk_hud(VPm, ce, T); float c3[3] = { ce.x, ce.y, ce.z }; ttm_hud(VPm, c3, T); hud_end(); hud_shot_end(); }
	}
	printf("  %.0f s: score %d - %d, winner %d, %d captures, %d tank call-ins, tanks walked %.0f -> %.0f m (team 0) / %.0f -> %.0f m (team 1) from the terminal, %d bullets on tanks\n",
	       T, tm.score[0], tm.score[1], tm.winner, caps, calls, tank_d0[0] / 100, tank_d1[0] / 100, tank_d0[1] / 100, tank_d1[1] / 100, shots);
	printf("  events seen: %s%s%s%s%s; feed %d (last '%s'), points %d (last '%s'); hack events: start %d done %d fail %d destroyed %d restored %d\n",
	       (all & TEV_ONLINE) ? "online " : "", (all & TEV_CAPTURE) ? "capture " : "", (all & TEV_FINAL) ? "final " : "", (all & TEV_MULT) ? "mult " : "", (all & TEV_END) ? "end" : "",
	       feed_n, feed_last, pts_n, pts_last, ev_n[TKE_HACK_START], ev_n[TKE_HACK_DONE], ev_n[TKE_HACK_FAIL], ev_n[TKE_DESTROYED], ev_n[TKE_RESTORED]);
	CHECK(tm.state == TS_END && caps >= 4, "the match ends by captures");
	CHECK(calls >= 2, "tanks called in (%d)", calls);
	ttm_stop();
}

// a bot next to an enemy Think Tank hacks it (ttm_bots), keeps near it while the hack runs (ttm_bot_goal), its owners are told
static void test_bot_hack(void) {
	printf("== bots hack tanks (Terminal Conquest)\n");
	int team[3] = { 0, 1, 0 }; V3 pos[3] = { v3(G0.x - 2500, G0.y, G0.z + 2), v3(G0.x - 220, G0.y + 120, G0.z + 2), v3(G0.x - 2500, G0.y + 300, G0.z + 2) }; actors(3, team, pos);
	ttm_feed = feed; ttm_points = points; tk_name_fn = w_name; ttm_begin(DATA, "underground", 4, 900);
	int k = tk_spawn(G0, 0, 0, 0, "Sitara", "My Think Tank", TKF_NOAI); step(0.3f, 1 / 30.f);
	int nl0 = aud_nlog; float T = 0, t_start = -1; float g[3] = { 0 }; int goal = 0;
	for (int f = 0; f < 30 * 25; f++) { T += 1 / 30.f; tk_update(1 / 30.f); ttm_frame(1 / 30.f, w_eye(0), w_dir(0), 0, 0, 0, 1);
		if (tk[k].hacker == 1 && t_start < 0) { t_start = T; goal = ttm_bot_goal(0, g); } }
	int alarm = 0; for (int q = nl0; q < aud_nlog; q++) if (strstr(aud_log[q], "ghost_team_hacked") || strstr(aud_log[q], "is_turret_hacking_alarm")) alarm++;
	printf("  bot 0 (team 1) next to our tank: hack started at %.1f s, its goal while hacking %s (%.0f m from the tank), our alarm sounds %d, after 25 s: defences %s, banner '%s'\n",
	       t_start, goal ? "set" : "none", goal ? sqrtf((g[0] - tk[k].pl.pos.x) * (g[0] - tk[k].pl.pos.x) + (g[1] - tk[k].pl.pos.y) * (g[1] - tk[k].pl.pos.y)) / 100 : 0, alarm, tk[k].def ? "up" : "DOWN", ttm_banner[0]);
	CHECK(t_start >= 0 && t_start < 3, "the bot hacks"); CHECK(!tk[k].def, "the bot's hack completes"); CHECK(alarm >= 1, "owners hear the alarm");
	ttm_stop();
}

// ---------------------------------------------------------------- 5. cost
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static void test_cost(void) {
	printf("== cost\n");
	int team[3] = { 0, 1, 1 }; V3 pos[3] = { v3(G0.x - 1500, G0.y, G0.z + 2), v3(G0.x - 1500, G0.y + 400, G0.z + 2), v3(G0.x + 1500, G0.y, G0.z + 2) }; actors(3, team, pos);
	tk_clear(); tk_on_event = on_ev; tk_spawn(G0, 0, 1, 1, "Batou", "BatouKoma", 0); tk_spawn(v3(G0.x + 300, G0.y + 900, G0.z), 0, 0, 0, "Sitara", "My Think Tank", 0);
	for (int f = 0; f < 60; f++) { tk_update(1 / 30.f); a_hp[0] = 100; a_alive[0] = 1; }
	int N = 1800; skin_verts = 0; double t0 = now_s(); for (int f = 0; f < N; f++) { tk_update(1 / 30.f); for (int a = 0; a < 3; a++) { a_hp[a] = 100; a_alive[a] = 1; } } double t1 = now_s();
	printf("  tk_update, 2 tanks fighting: %.1f us per frame on this PC (the Vita's A9 core is ~10-15x slower: ~%.2f ms)\n", (t1 - t0) / N * 1e6, (t1 - t0) / N * 1e3 * 12);
	camera(v3(G0.x - 900, G0.y - 250, G0.z + 260), v3(G0.x, G0.y, G0.z + 90), 59);
	g_raster = 0; g_draws = g_tris = 0; skin_verts = 0; tk_stat_skinned = 0; tk_draw(VPm, FOG0); g_raster = 1;
	printf("  tk_draw, 2 tanks near: %ld draw calls, %ld triangles, %d vertices CPU-skinned\n", g_draws, g_tris, skin_verts);
	tk_clear();
}

// ---------------------------------------------------------------- 6. HUD states of the mode
static void test_hud(void) {
	printf("== HUD captures\n");
	char p[256]; snprintf(p, sizeof(p), "%sunderground.gcol", MAPS); load_col(p);
	int team[6] = { 0, 0, 0, 1, 1, 1 }; V3 pos[6]; for (int a = 0; a < 6; a++) pos[a] = G0; actors(6, team, pos);
	ttm_feed = feed; ttm_points = points; tk_name_fn = w_name;
	ttm_begin(DATA, "underground", 4, 900);
	const TtmTerm *A = &ttm_map.t[tm.active]; V3 eye = v3(A->dev[0] - 900, A->dev[1] + 300, A->dev[2] + 150); camera(eye, v3(A->dev[0], A->dev[1], A->dev[2] + 120), 59);
	float c3[3] = { eye.x, eye.y, eye.z };
	tk_spawn(v3(A->dev[0] - 300, A->dev[1] - 300, A->dev[2]), 0, 0, 0, "Sitara", "My Think Tank", TKF_NOAI); tk_spawn(v3(A->dev[0] + 400, A->dev[1] + 200, A->dev[2]), 3.1f, 1, 4, "Paz", "PazKoma", TKF_NOAI);
	tk[1].hacker = 2; tk[1].hack_t = 4; tk[1].hp = 2409; tk[0].def = 0; tk[0].hp = 1525;
	step(0.2f, 1 / 30.f); tm.t = 20; ttm_banner_t = 99;
	shot3d("t_reboot", eye, 4000); hud_shot_begin("t_reboot"); hud_begin(); hud_base(1, 0); ttm_hud(VPm, c3, 3); hud_end(); ttm_overlay(); hud_shot_end();
	tm.state = TS_LIVE; tm.bar = 0.68f; tm.mult = 2; tm.moving = 1; tm.moving_team = 0; tm.capturer = 0; tm.cap_team = 0; tm.nin[0] = 2; tm.nin[1] = 0;
	hud_shot_begin("t_live"); hud_begin(); hud_base(1, 0); ttm_hud(VPm, c3, 3); hud_end(); ttm_overlay(); hud_shot_end();
	tm.overtime = 1; tm.ot_left = 143; tm.bar = 0.41f; tm.moving_team = 1; tm.capturer = 4; tm.cap_team = 1; tm.nin[0] = 0; tm.nin[1] = 3; tm.final_team = 0;
	ttm_show("FINAL TERMINAL!", "Letting the enemy capture the final terminal will mean defeat regardless of score.", 0); ttm_banner_t = 1;
	hud_shot_begin("t_overtime_final"); hud_begin(); hud_base(2, 2); ttm_hud(VPm, c3, 3); hud_end(); ttm_overlay(); hud_shot_end();
	tm.overtime = 0; tm.final_team = -1; ttm_banner_t = 99; tm.ready[0] = 1; ttm_ready_t = 1;
	hud_shot_begin("t_ready"); hud_begin(); hud_base(2, 1); ttm_hud(VPm, c3, 3); hud_end(); ttm_overlay(); hud_shot_end();
	tm.ready[0] = 0; ttm_note("THINK TANK ALREADY DEPLOYED"); ttm_show("Think Tank defensive protocols shutting down!", "Hack the enemy Think Tank to disable its defenses.", 0); ttm_banner_t = 1;
	hud_shot_begin("t_shutdown"); hud_begin(); hud_base(2, 1); ttm_hud(VPm, c3, 3); hud_end(); ttm_overlay(); hud_shot_end();
	// the call-in placement in the open: hologram + ring where the player looks, the prompt
	{ float fl; V3 fd = free_dir(G0, &fl); tk_clear(); tm.state = TS_LIVE; tm.ready[0] = 1; ttm_placing = 1; ttm_place_ok = 1; ttm_place_t = 1; ttm_banner_t = ttm_notice_t = 99;
	  ttm_place_pt = G0; ttm_place_yaw = atan2f(-fd.y, -fd.x) + 0.5f; V3 ce = vadd(v3(G0.x, G0.y, G0.z + 160), vmul(fd, 520)); camera(ce, v3(G0.x, G0.y, G0.z + 40), 59);
	  float c4[3] = { ce.x, ce.y, ce.z }; shot3d("t_place", G0, 3000); hud_shot_begin("t_place"); hud_begin(); hud_base(1, 0); ttm_hud(VPm, c4, 3); hud_end(); ttm_overlay(); hud_shot_end(); ttm_placing = 0; }
	ttm_stop();
}

// portraits for the HUD panels (tools/tank/make_tk_hud.py): each model, idle, 3/4 front view, on black and on white (-> alpha)
static void portraits(void) {
	tk_clear(); int team[1] = { 0 }; V3 pos[1] = { v3(G0.x - 3000, G0.y, G0.z) }; actors(1, team, pos);
	for (int kind = 0; kind < 2; kind++) {
		tk_clear(); int i = tk_spawn(G0, 0, kind, -1, "", "", TKF_NOAI); step(0.4f, 1 / 30.f); Tank *k = &tk[i];
		const float *cam = kind ? (const float[]){ 400, 210, 235, 0, 0, 100 } : (const float[]){ 400, -210, 235, 0, 0, 100 };   // eye, target (tank space: +x front)
		camera(v3(G0.x + cam[0], G0.y + cam[1], G0.z + cam[2]), v3(G0.x + cam[3], G0.y + cam[4], G0.z + cam[5]), 36);
		float MVP[16]; const Model *M; SkinCache *sc; const MBone *pose; tk_draw_info(i, VPm, MVP, &M, &sc, &pose);
		float th = k->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th); const float Lw[3] = { 0.45f, -0.35f, 0.82f }; float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
		const float tint[4] = { 1.35f, 1.35f, 1.4f, 1 }, nofog[4] = { 0, 0, 0, 0 };
		for (int bg = 0; bg < 2; bg++) { g_clear(bg, bg, bg); model_draw_cpu(M, sc, MVP, pose, ld, tint, nofog, 1); char p[256]; snprintf(p, sizeof(p), "%s/port%d_%s.ppm", OUT, kind, bg ? "w" : "b"); g_save(p); }
	}
	tk_clear();
}
int main(int argc, char **argv) {
	if (getenv("TK_DATA")) snprintf(tk_data, sizeof(tk_data), "%s", getenv("TK_DATA"));   // databuilder

	if (argc > 1) OUT = argv[1];
	int only_portraits = argc > 2 && !strcmp(argv[2], "portraits");
	{ char c[300]; snprintf(c, sizeof(c), "mkdir -p %s", OUT); if (system(c)) {} }
	aud_init(); srand(7);
	int r = model_shader(mk, lg), rs = sprite_init(mk), rf = fx_init(dpath("fx.gfx")), rh = hud_load(dpath("hud.ghud"), mk), rg = hud_game_init();
	printf("model shader %d, sprite %d, fx %d, hud %d / %d (white %d, screen %d)\n", r, rs, rf, rh, rg, HG.white, HG.screen); CHECK(rh >= 0 && rg == 0 && HG.white >= 0 && HG.screen >= 0, "HUD pack");
	char p[256]; snprintf(p, sizeof(p), "%sunderground.gcol", MAPS); r = load_col(p); CHECK(r >= 0, "gcol %d", r);
	snprintf(p, sizeof(p), "%sunderground.glgt", MAPS); light_load(p); snprintf(p, sizeof(p), "%sunderground.gnav", MAPS); nav_load(p);
	{ TtmMap M; snprintf(p, sizeof(p), "%sunderground.gttm", DATA); ttm_load(&M, p); const TtmTerm *T = &M.t[M.start]; float best = -1;
	  for (int gy = -12; gy <= 12; gy++) for (int gx = -12; gx <= 12; gx++) { V3 f; if (!floor_at(T->dev[0] + gx * 200, T->dev[1] + gy * 200, T->dev[2] + 250, &f) || fabsf(f.z - T->dev[2]) > 120) continue;
		float sc = 0; for (int q = 0; q < 16; q++) { V3 d = v3(cosf(q * 0.3927f), sinf(q * 0.3927f), 0); float h = raycast(v3(f.x, f.y, f.z + 130), d, 2500, NULL, NULL); sc += h < 0 ? 2500 : h; }
		if (sc > best) { best = sc; G0 = f; } }
	  printf("  open floor point: mean free distance %.0f cm\n", best / 16); ttm_free(&M); }
	printf("  test floor point (%.0f %.0f %.0f)\n", G0.x, G0.y, G0.z);
	tk_bind(&pl, &W); tk_hook_bots();
	if (only_portraits) { tk_load(DATA); portraits(); return 0; }
	test_models();
	if (tk_ok > 0) { test_unit(); test_tutorial(); test_cost(); }
	test_rules();
	if (tk_ok > 0) { test_match(); test_bot_hack(); test_hud(); }
	printf("audio: %d events, %d RAM sounds, %d voice lines, %d music cues, %d unknown names\n", aud_events, aud_ram, aud_voice, aud_music, aud_bad);
	CHECK(aud_bad == 0, "unknown sound names");
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
