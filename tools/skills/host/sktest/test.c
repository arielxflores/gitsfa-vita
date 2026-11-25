// Host test of engine/src/skills.h: every skill's state machine through a scripted scenario (activate, active, effect on a
// target, cooldown, tier change), the bot rules and hooks, the bullet / shot blocking, the drawing paths (GL stubbed, draw calls
// counted) and the cost of skills_update. Real data: fx_sk.gfx, sk_*.gvm, sk_ovl.gtx, hud.ghud; real event / sound names.
// run: ./run_test.sh   (copies the engine headers next to the stubs, builds with -fsanitize=address,undefined, runs)
#include <stdio.h>
#include <time.h>
#include "vitaGL.h"
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"
#include <stdarg.h>
static void logp(const char *f, ...) { (void)f; }   // (b83: skills.h logs the overlay shapes through it)
#include "fxscene.h"   // (b121: the Net Jump's entry / exit screen scenes)
#include "skills.h"

#ifndef DATA
#define DATA "/root/gits/build/data/"
#endif
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

// ---- a tiny world: a 200 x 200 m floor at z 0 and a wall (x 2000..2100, y -1000..1000, 400 high)
static void make_col(const char *path) {
	float V[][3] = { { -10000, -10000, 0 }, { 10000, -10000, 0 }, { 10000, 10000, 0 }, { -10000, 10000, 0 },
		{ 2000, -1000, 0 }, { 2000, 1000, 0 }, { 2000, 1000, 400 }, { 2000, -1000, 400 }, { 2100, -1000, 0 }, { 2100, 1000, 0 }, { 2100, 1000, 400 }, { 2100, -1000, 400 } };
	unsigned I[][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 4, 5, 6 }, { 4, 6, 7 }, { 8, 10, 9 }, { 8, 11, 10 }, { 4, 8, 9 }, { 4, 9, 5 }, { 7, 6, 10 }, { 7, 10, 11 } };
	unsigned nv = 12, nt = 10, ver = 3; int ns = 2; FILE *f = fopen(path, "wb"); fwrite("GCOL", 4, 1, f); fwrite(&ver, 4, 1, f); fwrite(&nv, 4, 1, f); fwrite(&nt, 4, 1, f);
	fwrite(V, 12, nv, f); fwrite(I, 12, nt, f); fwrite(&ns, 4, 1, f);
	Spawn s[2] = { { 0, 0, -500, 0, 0, 0 }, { 1, 0, 500, 0, 0, 3.14f } }; fwrite(s, sizeof(Spawn), 2, f);
	for (unsigned i = 0; i < nt; i++) { unsigned char m = 8 | (TF_PLAYER | TF_BULLET) << 4; fwrite(&m, 1, 1, f); }
	fclose(f);
}
// ---- actors (0 = the player)
typedef struct { V3 pos, vel; float yaw, pitch, hp, ap; int alive, team, firing, tgt, sees; float lost; V3 last; int kills; } TA;
static TA ta[SK_MAXACT]; static int n_act = SK_MAXACT;
typedef struct { int v, att, kind; float dmg; } DmgLog; static DmgLog dlog[4096]; static int ndlog;
static int w_n(void) { return n_act; }
static V3 w_pos(int a) { return ta[a].pos; }
static V3 w_vel(int a) { return ta[a].vel; }
static float w_yaw(int a) { return ta[a].yaw; }
static int w_alive(int a) { return ta[a].alive; }
static V3 w_eye(int a) { return v3(ta[a].pos.x, ta[a].pos.y, ta[a].pos.z + P_EYE); }
static V3 w_dir(int a) { return v3(cosf(ta[a].yaw) * cosf(ta[a].pitch), sinf(ta[a].yaw) * cosf(ta[a].pitch), sinf(ta[a].pitch)); }
static float w_hp(int a) { return ta[a].hp; }
static int w_los(V3 a, V3 b) { V3 d = vsub(b, a); float l = sqrtf(vdot(d, d)); if (l < 1) return 1; float h = raycast(a, vmul(d, 1 / l), l, NULL, NULL); return h < 0 || h > l - 25; }
static int w_damage(int v, float dmg, int att, int kind) {
	if (ndlog < 4096) { dlog[ndlog].v = v; dlog[ndlog].att = att; dlog[ndlog].kind = kind; dlog[ndlog].dmg = dmg; ndlog++; }
	if (!ta[v].alive) return 0; ta[v].hp -= dmg; if (ta[v].hp <= 0) { ta[v].hp = 0; ta[v].alive = 0; if (att >= 0) ta[att].kills++; return 1; } return 0; }
static void w_heal(int a, float add, float cap) { float mx = 100 * cap; if (add > 0) { float nh = ta[a].hp + add; if (nh > fmaxf(ta[a].hp, mx)) nh = fmaxf(ta[a].hp, mx); ta[a].hp = nh; } else if (ta[a].hp > mx) ta[a].hp = mx; }
static int w_firing(int a) { return ta[a].firing; }
static int w_bot_info(int a, int *t, int *s, float *l, V3 *ls) { if (a == 0) return 0; *t = ta[a].tgt; *s = ta[a].sees; *l = ta[a].lost; *ls = ta[a].last; return 1; }
static int nshots; static void w_shot(V3 a, V3 b) { (void)a; (void)b; nshots++; }
static int nnotify; static char last_notify[64]; static void w_notify(int a, int what, const char *t, int pts) { (void)pts; if (a == 0 && what == SKN_KILL && t) { nnotify++; snprintf(last_notify, 64, "%s", t); } }
static int npoints; static void w_notify2(int a, int what, const char *t, int pts) { if (a == 0 && what == SKN_POINTS) npoints += pts; w_notify(a, what, t, pts); }
static const char *w_name(int a) { static const char *N[SK_MAXACT] = { "Player", "Ishikawa", "Saito", "E3", "E4", "E5", "E6", "E7", "E8" }; return N[a]; }
static void w_armor(int a, float add, float cap) { float mx = 100 * cap; if (add > 0) { float n = ta[a].ap + add; if (n > fmaxf(ta[a].ap, mx)) n = fmaxf(ta[a].ap, mx); ta[a].ap = n; } else if (ta[a].ap > mx) ta[a].ap = mx; }   // b83
static const SkWorld W = { w_n, w_pos, w_vel, w_yaw, w_alive, w_eye, w_dir, w_hp, w_los, w_damage, w_heal, w_firing, w_bot_info, w_shot, w_notify2, w_name, .armor = w_armor };

static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static int heard(const char *name) { for (int i = 0; i < aud_nlog; i++) if (strstr(aud_log[i], name)) return 1; return 0; }
static float dmg_to(int v, int kind) { float s = 0; for (int i = 0; i < ndlog; i++) if (dlog[i].v == v && (kind < 0 || dlog[i].kind == kind)) s += dlog[i].dmg; return s; }
static void step(float secs) { int n = (int)(secs * 60 + 0.5f); for (int i = 0; i < n; i++) { for (int a = 0; a < n_act; a++) if (ta[a].alive) ta[a].pos = vadd(ta[a].pos, vmul(ta[a].vel, 1 / 60.f)); skills_update(1 / 60.f); } }
// a fresh scene: everyone alive at 100 HP, standing; skills set per actor (-1 = keep), all ready
static void scene(const int *skills) {
	memset(ta, 0, sizeof(ta)); ndlog = 0; aud_nlog = 0; nshots = 0; nnotify = 0; npoints = 0;
	for (int a = 0; a < SK_MAXACT; a++) { ta[a].alive = 1; ta[a].hp = 100; ta[a].ap = 100; ta[a].team = a < 3 ? 0 : 1; ta[a].tgt = -1; ta[a].pos = v3(-5000 - a * 300, -6000, 0); }
	for (int a = 0; a < SK_MAXACT; a++) { SKA[a].on = 0; sk_actor_set(a, skills[a] >= 0 ? skills[a] : (a * 5) % SK_COUNT, ta[a].team); }
	skills_reset_match(); sk_bots_use = 0;   // bots keep their skills to themselves unless a test turns them on
}
static void bots_on(void) { sk_bots_use = 1; for (int a = 1; a < SK_MAXACT; a++) SKA[a].bot_t = 0.5f; }
static void all(int s, int *k) { for (int i = 0; i < SK_MAXACT; i++) k[i] = s; }

int main(void) {
	make_col("/tmp/sktest.gcol"); if (load_col("/tmp/sktest.gcol") < 0) { printf("collision failed\n"); return 1; }
	sprite_init(mk); model_shader(mk, lg);
	int fr = fx_init(DATA "fx_sk.gfx"); printf("fx_sk.gfx: %d effects\n", fr);
	int hr = hud_load(DATA "hud.ghud", mk); int gr = hr >= 0 ? hud_game_init() : -1; printf("hud: %d / widgets %d\n", hr, gr);
	fxs_ok = 1;   // (b121: the Net Jump's scenes load - the GL is the stub's)
	int sr = skills_init(DATA, &W, 1); printf("skills_init: %d parts\n", sr);
	CHECK(sr == SKM_N + 3, "models + overlay texture + shield texture + launcher viewmodel loaded (%d)", sr); CHECK(sk_vm_ok && sk_vm.nmesh == 3, "vm_armlauncher.gvm: 3 meshes, clips");
	for (int k = 0; k < SKF_N; k++) CHECK(sk_fxid[k] >= 0, "effect %s resolved", SKF_NAME[k]);
	printf("  explosion effect: %s (id %d)\n", sk_fx_find("EXPLOSION") >= 0 ? "EXPLOSION (build_fx.py grenade)" : "fallback SK_MS_EXP", sk_fxid[SKF_EXPLOSION]);
	for (int k = 0; k < SKC_N; k++) CHECK(sk_cell[k] >= 0, "cell %d resolved", k);
	CHECK(sk_cell[SKC_BEAM] >= 0 && fabsf(fx_cell[sk_cell[SKC_BEAM]][2] - fx_cell[sk_cell[SKC_BEAM]][0]) > fabsf(fx_cell[sk_cell[SKC_BEAM]][3] - fx_cell[sk_cell[SKC_BEAM]][1]), "beam cell runs along u");
	CHECK(sk_mok[SKM_TURRET] && model_attach(&sk_mdl[SKM_TURRET], "muzzle") && model_attach(&sk_mdl[SKM_TURRET], "laser") && sk_mdl[SKM_TURRET].nmesh == 2, "turret model (rigged: body + T2 pod) + attachments");
	CHECK(sk_rest_ok[SKM_TURRET] && sk_rest_ok[SKM_MINI] && sk_ntyaw == 6 && sk_ntpitch == 5, "rigged turret / recon: rest poses, aim subtrees");
	for (int c = 0; c < SKA_N; c++) if (c != SKA_WALK) CHECK(sk_clip[SKM_TURRET][c] >= 0, "turret clip %s", SKA_NAME[c]);
	{ const Model *M = &sk_mdl[SKM_TURRET]; int g2 = model_find_bone(M, "gun02"), l1 = model_find_bone(M, "Bone01"); MBone P[48], Q[48]; memcpy(P, sk_rest[SKM_TURRET], sizeof(P)); memcpy(Q, P, sizeof(P));
	  sk_turret_aim(Q, 1.5707963f, 0); float *pv = P[sk_tb_yaw].p, dx = P[g2].p[0] - pv[0], dy = P[g2].p[1] - pv[1];
	  CHECK(g2 >= 0 && NEAR(Q[g2].p[0] - pv[0], -dy, 0.5f) && NEAR(Q[g2].p[1] - pv[1], dx, 0.5f) && NEAR(Q[l1].p[0], P[l1].p[0], 1e-4f), "aim yaw 90: the gun turns about the body (%.1f %.1f), the legs stay", Q[g2].p[0] - pv[0], Q[g2].p[1] - pv[1]);
	  memcpy(Q, P, sizeof(P)); sk_turret_aim(Q, 0, 0.4f); CHECK(Q[g2].p[2] > P[g2].p[2] + 5 && P[g2].p[1] < P[sk_tb_pitch].p[1], "aim pitch up: the barrel (in front, -y) rises (%.1f -> %.1f)", P[g2].p[2], Q[g2].p[2]); }
	CHECK(sk_clip[SKM_MINI][SKA_DEPLOY] >= 0 && sk_clip[SKM_MINI][SKA_IDLE] >= 0 && sk_clip[SKM_MINI][SKA_DESTROY] >= 0, "recon clips");
	CHECK(sk_emp_spin >= 0, "EMP spin clip");
	{ // the firing-arc wedges: apex at the turret, the arc centred on its facing (sprite corners captured from sp_v)
	  float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, col[4] = { 1, 1, 1, 1 }; sp_n = 0; sk_arc(I, v3(100, 200, 0), 250, 0.5f, 90, col);
	  float cx = 0, cy = 0; for (int k = 0; k < 6; k++) { cx += sp_v[k * 9]; cy += sp_v[k * 9 + 1]; } cx = cx / 6 - 100; cy = cy / 6 - 200;
	  CHECK(sp_n == 6 && fabsf(atan2f(cy, cx) - 0.5f) < 0.01f, "90 deg wedge centred on the facing (%.2f)", atan2f(cy, cx));
	  int apex = 0; for (int k = 0; k < 6; k++) if (fabsf(sp_v[k * 9] - 100) < 0.01f && fabsf(sp_v[k * 9 + 1] - 200) < 0.01f && fabsf(sp_v[k * 9 + 3] - fx_cell[sk_cell[SKC_SECTOR]][0]) < 1e-6f && fabsf(sp_v[k * 9 + 4] - fx_cell[sk_cell[SKC_SECTOR]][3]) < 1e-6f) apex++;
	  CHECK(apex >= 1, "apex (u0, v1) at the turret"); sp_n = 0; sk_arc(I, v3(0, 0, 0), 250, 0, 360, col); CHECK(sp_n == 24, "360: four wedges"); sp_n = 0; }
	for (int i = 0; i < SKM_N; i++) for (int k = 0; k < sk_mdl[i].nmesh; k++) CHECK(sk_mdl[i].mesh[k].rigid >= 0 || (sk_rest_ok[i] && sk_mdl[i].npal <= 48), "%s mesh %d rigid (or a skinned model on the GPU palette)", SKM_FILE[i], k);
	for (int i = 0; i < SK_COUNT; i++) { char b[16]; snprintf(b, 16, "skill.%d", SK_DEF[i].hud_icon); CHECK(hud_find_img(b) >= 0, "hud icon %s", b); }
	int K[SK_MAXACT];

	// ---- economy (final build): Tier 1 = 25-30 s cooldown, Tier 2 = the operator's intel cost; one button, hold for Tier 2
	printf("economy\n"); all(SK_OPTICAL, K); scene(K);
	sk_set_tier(0, 0); CHECK(sk_t1_ready(0) && !sk_t2_ready(0), "start: Tier 1 ready, no Tier-2 intel");
	sk_set_tier(0, 399); CHECK(!sk_t2_ready(0) && SKV.t2n <= 0, "399 of 400: not yet");
	sk_set_tier(0, 400); CHECK(sk_t2_ready(0) && SKV.t2n > 0 && heard("intel_skill/alarm_tier2") && heard("voice/donut/common/op_optical_ready"), "400: TIER 2 SKILL READY + Operator line");
	sk_set_tier(0, 900); CHECK(SKA[0].intel == 400, "the ring caps at the cost (%d)", SKA[0].intel);
	{ HudState H; memset(&H, 0, sizeof(H)); sk_hud_state(0, &H); CHECK(H.intel == 400 && H.tier_hi == 400 && H.tier == 2 && H.cool < 0 && H.skill_icon == 8, "HUD gauge: ring full, TIER 2, Tier 1 ready"); }
	CHECK(sk_button(0, 1, 1, 1 / 60.f) && SKA[0].state == SKS_ACTIVE && SKA[0].tier == 1 && NEAR(SKA[0].cool, 25, 0.01f), "tap: Tier 1 at once");
	for (int i = 0; i < 20; i++) { sk_button(0, 0, 1, 1 / 60.f); skills_update(1 / 60.f); } CHECK(SKA[0].tier == 1, "held 0.33 s: still Tier 1");
	for (int i = 0; i < 6; i++) { sk_button(0, 0, 1, 1 / 60.f); skills_update(1 / 60.f); }
	CHECK(SKA[0].tier == 2 && SKA[0].intel == 0 && SKA[0].cool <= 0 && SKA[0].state == SKS_ACTIVE, "held 0.4 s: upgraded to Tier 2, intel spent, Tier-1 cooldown refunded");
	CHECK(!sk_t1_ready(0) && !sk_t2_ready(0), "while Tier 2 runs both tiers are disabled");
	{ HudState H; memset(&H, 0, sizeof(H)); sk_hud_state(0, &H); SkHudInfo hi; sk_hud_info(0, &hi); CHECK(H.cool == 1 && hi.t1_state == 2 && hi.t2_state == 3, "HUD: Tier 1 'no' icon, Tier 2 running"); }
	scene(K); sk_set_tier(0, 0); sk_press(0); step(5.1f); sk_set_tier(0, 400); CHECK(SKA[0].cool > 19 && sk_t2_ready(0), "Tier 1 cooling, Tier 2 ready");
	CHECK(sk_button(0, 1, 0, 1 / 60.f) && SKA[0].tier == 2 && SKA[0].state == SKS_ACTIVE, "a tap while Tier 1 cools uses Tier 2");
	scene(K); sk_set_tier(0, 0); sk_press(0); sk_set_tier(0, 400); step(0.1f); CHECK(!sk_t2_ready(0), "Tier 2 waits while Tier 1 runs"); step(5.0f); CHECK(sk_t2_ready(0), "...and is ready after it");

	// ---- 1 Motoko: therm-optic camo
	printf("camo\n"); all(SK_OPTICAL, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(1000, 0, 0); ta[4].pos = v3(300, 0, 0);
	CHECK(sk_ready(0) && sk_can_see(3, 0) == 1, "visible before");
	CHECK(sk_press(0) && SKA[0].state == SKS_ACTIVE, "press -> active"); CHECK(heard("intel_skill/cloaking_start") && heard("motoko_using_optical"), "start sound + voice");
	CHECK(NEAR(SKA[0].cool, 25, 0.01f), "Tier-1 cooldown (25 s) starts at activation (%.2f)", SKA[0].cool);
	step(0.1f); CHECK(sk_can_see(3, 0) == 0, "hidden at 10 m"); CHECK(sk_can_see(4, 0) == 0, "near: hidden for the first 0.8 s");
	step(1.0f); CHECK(sk_can_see(4, 0) == 1, "near: seen after 0.8 s within 4 m"); CHECK(sk_can_see(1, 0) == 1, "allies always see her");
	ta[0].firing = 1; step(1 / 60.f); ta[0].firing = 0; CHECK(sk_can_see(3, 0) == 1, "a shot gives her away"); step(1.1f); CHECK(sk_can_see(3, 0) == 0, "hidden again 1 s after the shot");
	CHECK(sk_camo_on(0) && SKV.camo > 0.99f, "overlay faded in");
	{ int ic; float co, ba; sk_hud(0, &ic, &co, &ba); CHECK(ic == 8 && co > 0.9f && ba > 0.3f && ba < 1, "HUD: icon 8, cooling, bar %.2f", ba); }
	step(5 - SKA[0].t + 0.05f); CHECK(SKA[0].state == SKS_COOLDOWN && heard("intel_skill/cloaking_end"), "ends after 5 s (Tier 1, footage)");
	CHECK(NEAR(SKA[0].cool, 20, 0.1f), "20 s of cooldown left (%.2f)", SKA[0].cool); CHECK(!sk_press(0), "can't press while cooling (no intel)");
	step(20.1f); CHECK(SKA[0].state == SKS_READY && heard("intel_skill/cloaking_hud_on"), "ready 25 s after activation");
	sk_set_tier(0, 400); CHECK(sk_use(0, 2), "Tier 2"); step(43.9f); CHECK(SKA[0].state == SKS_ACTIVE, "Tier 2 lasts 44 s"); step(0.2f); CHECK(SKA[0].state == SKS_READY, "Tier 2 over (Tier 1 untouched: ready)");
	{ scene(K); sk_set_tier(0, 0); sk_press(0); step(0.5f); float b0 = SKA[0].bar; step(1.0f); float b1 = SKA[0].bar; step(3.4f); float b4 = SKA[0].bar;
	  CHECK(NEAR(b0, 1, 1e-3f) && NEAR(b1, 0.8f, 1e-3f) && NEAR(b4, 0.2f, 1e-3f), "Tier-1 bar: 5 steps of 1 s (%.2f %.2f %.2f)", b0, b1, b4); }
	{ scene(K); sk_set_tier(0, 400); sk_use(0, 2); step(22); CHECK(NEAR(SKA[0].bar, 0.5f, 0.01f), "Tier-2 bar: smooth (%.2f at 22 s)", SKA[0].bar);
	  SkHudInfo hi; sk_hud_info(0, &hi); CHECK(hi.t1_state == 2, "Tier 1 'no' while Tier 2 runs (no countdown)"); }
	{ scene(K); sk_set_tier(0, 0); sk_press(0); step(5.5f); sk_set_tier(0, 400); sk_use(0, 2); step(1); SkHudInfo hi; sk_hud_info(0, &hi);
	  CHECK(hi.t1_state == 1 && hi.t2_state == 3 && NEAR(hi.t1_left, 18.5f, 0.05f), "Tier 2 started while Tier 1 cools: the countdown keeps running (%.1f)", hi.t1_left);
	  step(19); sk_hud_info(0, &hi); CHECK(hi.t1_state == 2, "...then the 'no' icon until Tier 2 ends"); step(25); sk_hud_info(0, &hi); CHECK(hi.t1_state == 0, "Tier 1 ready after Tier 2"); }
	{ scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); sk_press(0); step(1); for (int i = 0; i < 40; i++) { ta[0].firing = i & 1; ta[0].hp -= 1; step(0.1f); } ta[0].firing = 0;
	  CHECK(!sk_cloaked(0) && SKA[0].state == SKS_COOLDOWN && NEAR(SKA[0].t, 0, 0.2f), "firing and taking hits neither end nor shorten the camo (footage: always 5.0 s)"); }
	// vision accessors: cloak amount ramps, flags, shimmer, break, seen-by
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(1000, 0, 0);
	CHECK(sk_cloak(0) == 0 && sk_cloak_flags(0) == 0 && sk_cloak_shimmer(0) == 0, "no camo: amount 0, no flags");
	sk_press(0); step(0.15f); CHECK(sk_cloak(0) > 0.3f && sk_cloak(0) < 0.7f && (sk_cloak_flags(0) & (SKCF_ON | SKCF_FADE_IN)) == (SKCF_ON | SKCF_FADE_IN) && sk_cloak_shimmer(0) > 0.4f,
		"forming: amount %.2f, ON|FADE_IN, shimmer %.2f", sk_cloak(0), sk_cloak_shimmer(0));
	step(0.5f); CHECK(sk_cloak(0) == 1 && sk_cloak_flags(0) == SKCF_ON && NEAR(sk_cloak_shimmer(0), 0.15f, 0.01f) && sk_cloak_seen_by(0, 3) == 0, "formed: amount 1, steady shimmer, unseen at 10 m");
	ta[0].firing = 1; step(1 / 60.f); ta[0].firing = 0; CHECK((sk_cloak_flags(0) & (SKCF_FIRED | SKCF_EXPOSED)) == (SKCF_FIRED | SKCF_EXPOSED) && sk_cloak_break(0) > 0.9f && sk_cloak_seen_by(0, 3) == 1, "fired: FIRED|EXPOSED, break 1, seen");
	step(1.1f); CHECK(!(sk_cloak_flags(0) & (SKCF_FIRED | SKCF_EXPOSED)) && sk_cloak_break(0) == 0, "1 s later: hidden again");
	ta[0].hp -= 10; step(1 / 60.f); CHECK((sk_cloak_flags(0) & SKCF_HIT) && sk_cloak_shimmer(0) > 0.5f && sk_cloak_break(0) > 0.4f, "hit: HIT flicker");
	ta[4].pos = v3(200, 0, 0); step(0.1f); CHECK((sk_cloak_flags(0) & SKCF_NEAR) && !(sk_cloak_flags(0) & SKCF_EXPOSED), "enemy at 2 m: NEAR, not yet exposed"); step(0.8f); CHECK(sk_cloak_flags(0) & SKCF_EXPOSED, "...exposed after 0.8 s");
	ta[4].pos = v3(-5000, 0, 0); step(5 - SKA[0].t + 0.05f); CHECK(!sk_cloaked(0) && (sk_cloak_flags(0) & SKCF_FADE_OUT) && sk_cloak(0) > 0.8f && sk_cloak_shimmer(0) > 0.8f, "ended: FADE_OUT, amount %.2f", sk_cloak(0));
	step(0.6f); CHECK(sk_cloak(0) == 0 && sk_cloak_flags(0) == 0, "dissolved after 0.6 s");
	sk_set_tier(0, 400); sk_use(0, 2); step(1); CHECK(sk_cloak_flags(0) & SKCF_TIER2, "Tier 2 flag"); ta[0].alive = 0; step(1 / 60.f); CHECK(!sk_cloaked(0) && sk_cloak(0) > 0.8f, "death: the cloak ends, the body keeps it a moment"); step(0.3f); CHECK(sk_cloak(0) == 0, "...0.25 s"); ta[0].alive = 1;
	SKA[3].heat_t[0] = 1.2f; SKA[3].heat_by[0] = 2; CHECK(NEAR(sk_reveal_until(3, 0) - sk_time(), 1.2f, 1e-3f) && sk_revealed_by(3, 0) == 2 && sk_reveal_until(3, 1) == 0, "reveal until / by");
	// a cloaked enemy bot as the player sees it
	SKA[3].tier = 1; ta[3].pos = v3(3000, 0, 0); sk_press(3); step(0.05f);
	CHECK(sk_draw_alpha(3, v3(0, 0, 150)) == 0, "far cloaked enemy: invisible"); ta[3].pos = v3(300, 0, 0); CHECK(sk_draw_alpha(3, v3(0, 0, 150)) > 0.05f, "near: faint ghost");
	CHECK(sk_draw_alpha(1, v3(0, 0, 150)) == 1, "others drawn normally");
	// death ends it
	ta[3].alive = 0; step(0.05f); CHECK(SKA[3].state == SKS_COOLDOWN, "death ends the camo");

	// ---- 2 Saito: heat scan
	printf("heat scan\n"); all(SK_HEATSCAN, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(3000, 0, 0); ta[4].pos = v3(5000, 0, 0); ta[1].pos = v3(100, 0, 0);
	sk_press(0); step(0.25f); CHECK(SKA[3].heat_t[0] <= 0, "no reveal before the first pulse (0.3 s)");
	step(0.1f); CHECK(SKA[3].heat_t[0] > 1.4f && SKA[4].heat_t[0] <= 0, "pulse 1: 30 m revealed, 50 m not");
	CHECK(sk_can_see(1, 3) == 2, "ally bots know the revealed enemy"); CHECK(sk_can_see(5, 0) == 1, "enemies are not told");
	{ float g[3]; CHECK(sk_bot_goal(1, g) && NEAR(g[0], 3000, 1), "ally roam goal = the revealed enemy"); }
	CHECK(sk_known(3) && !sk_known(4) && !sk_known(1), "minimap: the revealed enemy only");
	CHECK(heard("intel_skill/heat_scan_detect") && heard("intel_skill/heat_scan_woosh"), "pulse + detect sounds");
	step(3.0f); CHECK(SKA[0].pulses == 4, "T1: 4 scans 1 s apart (%d)", SKA[0].pulses); step(1.0f); CHECK(SKA[0].state == SKS_COOLDOWN, "over after 4.3 s (b83, EP20)");
	CHECK(NEAR(SKA[0].cool, 20 - 4.35f, 0.1f), "Tier-1 cooldown 20 s from activation (%.2f)", SKA[0].cool);
	step(2.0f); CHECK(SKA[3].heat_t[0] <= 0, "reveal fades 1.6 s after the last pulse");
	sk_set_tier(0, 400); sk_use(0, 2); step(7.0f); CHECK(SKA[0].pulses == 1 && SKA[0].bar > 0.75f && SKA[0].bar < 0.82f, "Tier 2: one scan in the first 7 s, the bar drains (%d %.2f)", SKA[0].pulses, SKA[0].bar);
	step(28.0f); CHECK(SKA[0].pulses == 5 && SKA[0].state == SKS_ACTIVE, "Tier 2: five scans 7 s apart (%d)", SKA[0].pulses); step(0.5f); CHECK(SKA[0].state == SKS_COOLDOWN || SKA[0].state == SKS_READY, "Tier 2 over at 35.3 s");

	// ---- 3 Togusa: Seeker Drone (b85, v2.0 EP17: a hunter drone that explodes on contact; Tier 2 three + the scrambler)
	printf("seeker drone\n"); all(SK_SPECIALRECON, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = 0; ta[3].pos = v3(1500, 0, 0); ta[4].pos = v3(-3500, 0, 0);
	{ CHECK(sk_press(0) && SKA[0].state == SKS_ACTIVE, "no placement: the hand projects it"); int ns = 0; for (int o = 0; o < SK_MAXOBJ; o++) ns += SKO[o].alive && SKO[o].type == SKO_SEEKER; CHECK(ns == 0, "not out yet");
	  step(1.5f); for (int o = 0; o < SK_MAXOBJ; o++) ns += SKO[o].alive && SKO[o].type == SKO_SEEKER; CHECK(ns == 0, "during the hand's gesture: still not out (b117: 1.6 s, the game's skill video)");
	  step(0.15f); ns = 0; int so = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_SEEKER) { ns++; so = o; } CHECK(ns == 1, "Tier 1: one drone 1.6 s after the press");
	  step(0.15f); CHECK(SKA[0].state == SKS_COOLDOWN && NEAR(SKA[0].cool, 30 - 1.8f, 0.1f), "cooldown 30 s from the press (%.2f)", SKA[0].cool);
	  step(0.6f); CHECK(so >= 0 && SKO[so].target == 3, "it goes after the nearest enemy"); CHECK(SKA[3].mark_t[0] > 0, "the target shown to Togusa's team while it hunts");
	  { V3 c0 = SKO[so].cur; step(0.8f); CHECK(SKO[so].alive && SKO[so].phase == 1 && fabsf(SKO[so].cur.x - c0.x) < 1, "it stands 1.3 s where it was projected before it runs"); }
	  float h0 = ta[3].hp; step(2.6f); CHECK(!SKO[so].alive && ta[3].hp < h0 - 40 && heard("intel_skill/mini_tachikoma_exp"), "contact: blast (%.0f dmg)", h0 - ta[3].hp); CHECK(SKA[3].emp_t <= 0, "Tier 1: no scrambler"); }
	// b117: the mini Tachikoma - projected onto the floor, runs, stops beside the enemy and leaps (the delayed detonation), blinds who survives
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = 0; ta[3].pos = v3(1500, 0, 0); ta[4].pos = v3(-3500, 0, 0); sk_press(0); step(1.7f);
	{ int so = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_SEEKER) so = o;
	  CHECK(so >= 0 && SKO[so].phase == 0 && fabsf(SKO[so].cur.z) < 2 && SKO[so].cur.x > 200, "projected onto the floor ahead (%.0f %.0f %.0f)", so >= 0 ? SKO[so].cur.x : 0, so >= 0 ? SKO[so].cur.y : 0, so >= 0 ? SKO[so].cur.z : 0);
	  float h0 = ta[3].hp, tarm = -1; for (int k = 0; k < 80 && so >= 0 && SKO[so].alive && SKO[so].phase != 2; k++) { step(0.05f); if (SKO[so].alive && SKO[so].phase == 1) CHECK(fabsf(SKO[so].cur.z) < 2, "it runs on the floor (z %.1f)", SKO[so].cur.z); }
	  CHECK(so >= 0 && SKO[so].alive && SKO[so].phase == 2 && ta[3].hp == h0, "beside the enemy it stops armed: no blast yet"); if (so >= 0) tarm = SKO[so].t;
	  V3 p2 = so >= 0 ? SKO[so].cur : v3(0, 0, 0); step(0.3f); CHECK(so >= 0 && SKO[so].alive && fabsf(SKO[so].cur.x - p2.x) < 1 && ta[3].hp == h0, "the fuse: 0.3 s later still there, still no blast");
	  step(0.3f); CHECK(so >= 0 && !SKO[so].alive && ta[3].hp < h0 - 20, "then the blast (%.0f dmg)", h0 - ta[3].hp); (void)tarm;
	  CHECK(SKA[3].blind_t > 1.0f && !sk_can_see(3, 0), "the survivor is blinded (%.2f s): sees no enemy", SKA[3].blind_t); step(1.8f); CHECK(SKA[3].blind_t <= 0 && sk_can_see(3, 0), "and sees again after it"); }
	// running clear of the fuse: the target sprints away when the drone arms
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = 0; ta[3].pos = v3(1500, 0, 0); ta[4].pos = v3(-3500, 0, 0); sk_press(0); step(1.7f);
	{ int so = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_SEEKER) so = o;
	  for (int k = 0; k < 80 && so >= 0 && SKO[so].alive && SKO[so].phase != 2; k++) step(0.05f);
	  float h0 = ta[3].hp; for (int k = 0; k < 12; k++) { ta[3].pos.y += 650 * 0.05f; step(0.05f); }   // 6.5 m/s for 0.6 s
	  CHECK(so >= 0 && !SKO[so].alive && ta[3].hp == h0 && SKA[3].blind_t <= 0, "sprinting clear of the fuse: no damage, no flash (%.0f)", h0 - ta[3].hp); }
	// the player caught by an enemy's drone: the white-out
	scene(K); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(1200, 0, 0); ta[3].yaw = 3.14159f; ta[3].pitch = 0; sk_press(3); step(5.2f);
	CHECK(SKV.blind > 0 && SKV.blind_len > 0, "the player's view whites out (%.2f)", SKV.blind);
	scene(K); sk_set_tier(0, 500); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[3].pos = v3(1200, 0, 0); ta[4].pos = v3(1300, 300, 0); sk_use(0, 2); step(2.0f);
	{ int ns = 0; for (int o = 0; o < SK_MAXOBJ; o++) ns += SKO[o].alive && SKO[o].type == SKO_SEEKER; CHECK(ns == 3, "Tier 2: three drones (%d)", ns); }
	{ step(0.5f); int t3 = 0, t4 = 0; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_SEEKER) { t3 += SKO[o].target == 3; t4 += SKO[o].target == 4; } CHECK(t3 >= 1 && t4 >= 1, "Tier 2: the three spread over the enemies (%d / %d)", t3, t4); }
	step(3.6f); CHECK(SKA[3].emp_t > 0 || SKA[4].emp_t > 0, "Tier 2: the scrambler (no skills)");
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(3000, 0, 0); sk_press(0); step(1.75f);
	{ int so = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_SEEKER) so = o;
	  if (so >= 0) { sk_obj_damage(so, 50, 3); CHECK(!SKO[so].alive, "40 HP: shot down"); } else CHECK(0, "no drone to shoot"); }
	// an enemy seeker after the player
	scene(K); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(1500, 0, 0); ta[3].yaw = 3.14159f; ta[3].pitch = 0; sk_press(3); step(2.3f); CHECK(SKV.seek_t > 0, "the player sees SEEKER DRONE INCOMING");

	// ---- 4 Ishikawa: sentry gun
	printf("sentry gun\n"); all(SK_SENTRYGUN, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.5f; sk_press(0); step(0.1f); sk_fire(0); int to = SKA[0].obj;
	CHECK(to >= 0 && SKO[to].type == SKO_TURRET && sk_hdist(SKO[to].pos, ta[0].pos) <= 300.5f, "placed within 3 m");
	V3 tp = SKO[to].pos; ta[3].pos = vadd(tp, v3(1500, 300, 0)); ta[4].pos = vadd(tp, v3(-1500, 0, 0));
	step(1.0f); CHECK(SKO[to].phase == 0 && dmg_to(3, -1) == 0, "building (2 s): no fire");
	step(1.6f); CHECK(SKO[to].phase == 1 && SKO[to].target == 3, "targets the enemy in its 90 deg arc");
	step(2.0f); CHECK(dmg_to(3, SKD_BULLET) > 30 && dmg_to(4, -1) == 0, "shoots it (%.0f dmg), not the one behind (T1 arc)", dmg_to(3, SKD_BULLET));
	CHECK(nshots > 10, "muzzle / tracer effects (%d)", nshots);
	{ // the drawn pose and the firing point agree: muzzle through the pose (bone 2) + model->world, vs sk_turret_att
	  SkObj *O = &SKO[to]; float sy = O->yaw, sp = O->pitch; O->yaw0 = 0.3f; O->yaw = 0.9f; O->pitch = 0.25f;
	  MBone P[3]; sk_turret_pose(to, P); const float *mz = model_attach(&sk_mdl[SKM_TURRET], "muzzle"); float m3[3]; model_bone_xform(&P[2], mz, m3);
	  float th = O->yaw0 + 1.5707963f; V3 wpose = v3(O->pos.x + cosf(th) * m3[0] - sinf(th) * m3[1], O->pos.y + sinf(th) * m3[0] + cosf(th) * m3[1], O->pos.z + m3[2]);
	  V3 watt = sk_turret_att(to, "muzzle"), d = vsub(watt, wpose);
	  CHECK(sk_len(d) < 3, "pose and firing point agree (%.1f cm)", sk_len(d));
	  V3 piv = sk_turret_att(to, "yaw"), f = vsub(watt, piv); CHECK(fabsf(sk_wrap(atan2f(f.y, f.x) - 0.9f)) < 0.05f && f.z > 25, "muzzle points along the aim yaw, raised by the pitch");
	  O->yaw = sy; O->pitch = sp; O->yaw0 = 0; }
	ta[3].pos = v3(2500, 0, 0); ta[3].hp = 100; ta[3].alive = 1; SKO[to].pos = v3(1500, 0, 0); SKO[to].yaw0 = SKO[to].yaw = 0; SKO[to].target = -1; ndlog = 0; step(1.0f);
	CHECK(dmg_to(3, -1) == 0, "no shots through a wall");
	{ int k7[SK_MAXACT]; all(SK_SENTRYGUN, k7); scene(k7); ta[3].pos = v3(0, 0, 0); ta[3].pitch = -0.5f; sk_press(3); sk_bot_place_at(3, v3(200, 0, 0)); ta[0].pos = v3(1200, 100, 0); step(2.6f);
	  CHECK(heard("intel_skill/turret_alarm") && heard("voice/donut/common/op_sentrygun_enemy") && dmg_to(0, SKD_BULLET) > 0, "an enemy sentry on the player: alarm + warning voice, damage"); }
	all(SK_SENTRYGUN, K); scene(K); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.5f; sk_press(0); step(0.1f); sk_fire(0); to = SKA[0].obj;
	{ float rem = SKO[to].life - SKO[to].t; CHECK(NEAR(SKO[to].life, 20, 0.01f), "Tier 1: 20 s"); step(rem + 0.1f); CHECK(!SKO[to].alive && heard("intel_skill/turret_hacking_end"), "gone after its life");
	  int gh = 0; for (int k = 0; k < 4; k++) gh += SKG[k].on && SKG[k].clip == SKA_UNDEPLOY; CHECK(gh == 1, "...folding up with its own clip (undeploy)"); step(1.2f); gh = 0; for (int k = 0; k < 4; k++) gh += SKG[k].on; CHECK(!gh, "...then gone"); }
	scene(K); sk_set_tier(0, 600); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.5f; sk_use(0, 2); step(0.1f); sk_fire(0); to = SKA[0].obj; tp = SKO[to].pos;
	CHECK(SKO[to].tier == 2 && SKA[0].intel == 0 && !sk_t1_ready(0), "Tier-2 sentry: intel spent, Tier 1 disabled while it stands");
	ta[4].pos = vadd(tp, v3(-1500, 0, 0)); step(2.6f); CHECK(SKO[to].target == 4, "Tier 2: 360 deg"); step(6.5f); CHECK(heard("intel_skill/turret_rocket_fire"), "Tier-2 grenade pod fires");
	CHECK(dmg_to(4, SKD_BLAST) > 0 || !ta[4].alive, "grenades hurt (%.0f)", dmg_to(4, SKD_BLAST));
	scene(K); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.5f; sk_press(0); step(0.1f); sk_fire(0); to = SKA[0].obj; step(2.1f);
	{ V3 c = sk_obj_center(to); for (int i = 0; i < 40 && SKO[to].alive; i++) sk_bullet(v3(c.x + 800, c.y, c.z), v3(-1, 0, 0), 2000, 3, 9);
	  CHECK(!SKO[to].alive, "300 HP: shot down"); int gh = 0; for (int k = 0; k < 4; k++) gh += SKG[k].on && SKG[k].clip == SKA_DESTROY; CHECK(gh == 1, "the wreck plays its destroy clip"); }
	sk_press(0); CHECK(SKA[0].state == SKS_COOLDOWN, "one turret per cooldown");

	// ---- 5 Borma: metaboost
	printf("metaboost\n"); all(SK_METABOOST, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].hp = 40; ta[1].pos = v3(800, 0, 0); ta[1].hp = 50; ta[2].pos = v3(1500, 0, 0); ta[2].hp = 50; ta[3].pos = v3(300, 0, 0); ta[3].hp = 50;
	ta[0].ap = 20; sk_press(0); step(1.0f); CHECK(NEAR(ta[0].hp, 65, 1.5f) && NEAR(ta[0].ap, 53, 1.5f), "Nano-Gel T1: HP +25/s, AP +33/s (%.1f %.1f)", ta[0].hp, ta[0].ap);
	step(2.1f); CHECK(NEAR(ta[0].hp, 100, 0.5f) && NEAR(ta[1].hp, 50, 0.01f) && NEAR(ta[0].ap, 100, 0.5f), "Tier 1: self only, normal maxima (%.1f %.1f %.1f)", ta[0].hp, ta[1].hp, ta[0].ap);
	CHECK(NEAR(ta[2].hp, 50, 0.01f) && NEAR(ta[3].hp, 50, 0.01f), "nobody else: ally at 15 m, enemy");
	CHECK(SKA[0].state == SKS_COOLDOWN && heard("intel_skill/metabolism_end"), "Tier 1: 3 s (b83, EP21)");
	scene(K); sk_set_tier(0, 400); ta[0].pos = v3(0, 0, 0); ta[0].hp = 100; ta[0].ap = 100; ta[1].pos = v3(500, 0, 0); ta[1].hp = 100;
	sk_use(0, 2); step(4.0f); CHECK(NEAR(ta[0].hp, 191, 1) && NEAR(ta[0].ap, 221, 1) && NEAR(ta[1].hp, 100, 0.01f), "Tier 2: HP / AP maxima x1.91 / x2.21, filled in ~3 s, no auto-link (%.0f %.0f %.0f)", ta[0].hp, ta[0].ap, ta[1].hp);
	step(10); CHECK(ta[0].hp > 190, "bonus kept"); step(1.2f); CHECK(NEAR(ta[0].hp, 100, 0.01f) && NEAR(ta[0].ap, 100, 0.01f), "back to the normal maxima after 15 s");

	// ---- 6 Paz: rush
	printf("rush\n"); all(SK_RUSH, K); scene(K); sk_set_tier(0, 0);
	// (b83: the b79 v2.0 Hyper Sprint - run x1.3 / sprint x2.0, the gun stays, a fixed 5.5 s, no contact takedown)
	ta[0].pos = v3(0, 0, 0); sk_press(0); CHECK(sk_speed_mul(0, 1) == 2 && NEAR(sk_speed_mul(0, 0), 1.3f, 0.001f), "x2 sprint, x1.3 run"); CHECK(sk_can_fire(0), "the gun stays (v2.0)");
	ta[0].vel = v3(1070, 0, 0); step(1.0f); CHECK(sk_fov_add(0) == 8 && SKA[0].state == SKS_ACTIVE, "dashing: FOV +8");
	step(4.6f); CHECK(SKA[0].state == SKS_COOLDOWN, "Tier 1: a fixed 5.5 s");
	scene(K); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(600, 30, 0); sk_press(0); ta[0].vel = v3(1070, 0, 0); step(0.6f);
	CHECK(ta[3].alive && SKA[0].state == SKS_ACTIVE, "contact while dashing: no takedown in v2.0 (the knife reaches further instead)");
	scene(K); ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(-150, 0, 0); sk_press(0); ta[0].vel = v3(1070, 0, 0); step(0.2f); CHECK(ta[3].alive, "behind: no takedown");
	// a rushing bot
	scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(1200, 0, 0); step(0.6f);
	CHECK(SKA[3].state == SKS_ACTIVE, "bot rushes an enemy at 12 m");
	{ float mx = 0.4f, my = 0, yw = 0; int sp = 0, jp = 0; sk_bot_move(3, &mx, &my, &sp, &jp, &yw); CHECK(my == 1 && sp == 1 && mx == 0 && sk_aim_factor(3) == 0, "charges, no firing"); }

	// ---- 7 Azuma: plasma shield
	printf("plasma shield\n"); all(SK_PLASMASHIELD, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; sk_press(0); step(0.05f);
	{ V3 e; V3 from = v3(1500, 0, 115), to = v3(0, 0, 115); CHECK(sk_bullet_block(3, from, to, &e) && NEAR(e.x, SK_SH_FWD + 10, 1), "front shot absorbed at the panel (x %.0f)", e.x);
	  CHECK(NEAR(SKA[0].energy, 1 - 9.f / 400, 0.002f), "a hit wears the shield's HP: dmg / 400 (%.4f)", SKA[0].energy);
	  CHECK(SKA[0].sh_ph == 1 && SKA[0].sh_hit > 0, "raise animation, hit noise on");
	  step(0.2f); CHECK(SKA[0].sh_ph == 2, "up after 0.17 s");
	  CHECK(!sk_bullet_block(3, v3(-1500, 0, 115), to, &e), "behind: not absorbed"); CHECK(!sk_bullet_block(1, from, to, &e), "allied shots pass");
	  sk_press(0); CHECK(!SKA[0].toggled && !sk_bullet_block(3, from, to, &e), "toggled off: no block"); float en = SKA[0].energy, t0 = SKA[0].t; step(0.05f); CHECK(SKA[0].sh_ph == 3, "lowered: the lowering flash");
	  step(1.95f); CHECK(NEAR(SKA[0].energy, en, 1e-4f) && SKA[0].t > t0 + 1.9f && SKA[0].sh_ph == 0, "its HP is kept while off, its time runs on (footage)");
	  sk_press(0); CHECK(SKA[0].toggled, "toggled on again"); for (int i = 0; i < 60 && SKA[0].state == SKS_ACTIVE; i++) sk_bullet_block(3, from, to, &e);
	  CHECK(SKA[0].state == SKS_COOLDOWN && heard("intel_skill/shield_exp") && SKA[0].sh_ph == 4, "breaks when its HP is gone (the break pieces)"); step(0.4f); CHECK(SKA[0].sh_ph == 0, "pieces gone after 0.33 s"); }
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); sk_press(0); step(1.05f); CHECK(NEAR(SKA[0].bar, 8.f / 9, 0.01f), "Tier-1 bar: 1 s steps of 1/9 (%.3f)", SKA[0].bar);
	step(7.8f); CHECK(SKA[0].state == SKS_ACTIVE, "Tier 1 still up at 8.85 s"); step(0.3f); CHECK(SKA[0].state == SKS_COOLDOWN, "Tier 1: 9 s (footage)");
	scene(K); sk_set_tier(0, 500); ta[0].pos = v3(0, 0, 0); sk_use(0, 2); step(11); CHECK(NEAR(SKA[0].bar, 0.5f, 0.01f), "Tier-2 bar: smooth, half at 11 s (%.3f)", SKA[0].bar);
	step(11.1f); CHECK(SKA[0].state != SKS_ACTIVE, "Tier 2: 22 s (footage)");
	{ V3 e; scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); sk_press(0); step(0.05f); CHECK(!sk_bullet_block(3, v3(1500, 0, 190), v3(0, 0, 190), &e), "Tier-1 panel (81 x 101): over it passes");
	  CHECK(sk_bullet_block(3, v3(1500, 0, 175), v3(0, 0, 175), &e) && sk_bullet_block(3, v3(1500, 0, 85), v3(0, 0, 85), &e), "it covers z 81 .. 182");
	  scene(K); sk_set_tier(0, 500); ta[0].pos = v3(0, 0, 0); sk_use(0, 2); step(0.05f); CHECK(sk_bullet_block(3, v3(1500, 0, 190), v3(0, 0, 190), &e), "Tier-2 panel (86 x 153): blocks it"); }
	scene(K); sk_set_tier(3, 0); ta[3].pos = v3(1000, 0, 0); ta[3].yaw = 3.14159f; sk_press(3); step(0.05f);
	{ float t = sk_bullet(v3(0, 0, 115), v3(1, 0, 0), 1000, 0, 25); CHECK(t > 900 && t < 960, "the player's shot stops on an enemy panel (%.0f)", t); }
	{ float mx = 0, my = 0, yw = 0; int sp = 0, jp = 0; ta[4].pos = v3(0, 0, 0); ta[4].tgt = 3; ta[4].sees = 1; SKA[4].team = 0; ta[4].team = 0;
	  int ch = sk_bot_move(4, &mx, &my, &sp, &jp, &yw); CHECK(ch && fabsf(mx) == 1, "a bot facing a shield strafes"); SKA[4].team = 1; }

	// ---- 8 Kuro: EMP Generator (b85, v2.0 EP25 + the guides: 4 s fuse, then the blast stuns, destroys deployables; T2 huge)
	printf("EMP generator\n"); all(SK_EMP, K); K[4] = SK_SENTRYGUN; K[5] = SK_OPTICAL; K[6] = SK_CYBERWALL; scene(K); sk_set_tier(0, 0);
	ta[4].pos = v3(400, 0, 0); ta[4].pitch = -0.6f; ta[4].yaw = 0; sk_press(4); sk_bot_place_at(4, v3(600, 0, 0)); int et = SKA[4].obj;
	ta[5].pos = v3(500, 200, 0); sk_press(5); ta[6].pos = v3(300, -700, 0); sk_press(6); sk_bot_place_at(6, v3(300, -400, 0)); int ew = SKA[6].obj;
	ta[7].pos = v3(1600, 0, 0); step(2.5f); CHECK(SKO[et].alive && SKO[ew].alive && SKA[5].state == SKS_ACTIVE, "enemy turret, wall, camo up");
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.35f; sk_press(0); step(0.05f); CHECK(SKA[0].aim_ok, "generator preview ok");
	sk_fire(0); int eo = SKA[0].obj; CHECK(eo >= 0 && SKO[eo].type == SKO_EMP && sk_hdist(SKO[eo].pos, ta[0].pos) <= 700.5f, "placed within 7 m (%.0f)", sk_hdist(SKO[eo].pos, ta[0].pos));
	CHECK(NEAR(SKA[0].cool, 25, 0.2f), "cooldown 25 s (EP25)"); SKO[eo].pos = v3(450, -100, 0);
	step(3.9f); CHECK(SKO[et].alive && SKO[ew].alive, "4 s fuse (time to run)"); step(0.2f);
	CHECK(!SKO[et].alive && !SKO[ew].alive, "blast: enemy sentry + barrier destroyed"); CHECK(nnotify >= 2, "SENTRY / BARRIER DESTROYED notices (%s)", last_notify);
	CHECK(SKA[5].state == SKS_COOLDOWN && SKA[5].emp_t > 3.5f && SKA[5].stun_t > 3.5f && SKA[5].mark_t[0] > 0, "camo ended, stunned 4 s, marked for Kuro's side");
	CHECK(!sk_press(5) && sk_aim_factor(5) == 0.5f && NEAR(sk_speed_mul(5, 0), 0.45f, 0.01f) && sk_look_mul(5) < 0.2f, "stunned: no skills, half aim, slow walk, the view barely turns");
	CHECK(SKA[7].emp_t <= 0, "out of range (5 m at T1): untouched");
	step(1.0f); CHECK(!SKO[eo].alive, "device gone 1 s after the blast"); step(3.0f); CHECK(SKA[5].stun_t <= 0, "the stun wears off");
	scene(K); sk_set_tier(0, 500); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.35f; ta[7].pos = v3(2000, 0, 0); sk_use(0, 2); step(0.05f); sk_fire(0); eo = SKA[0].obj;
	step(4.1f); CHECK(SKA[7].stun_t > 6.5f, "Tier 2: 20 m away still stunned, 7 s");

	// ---- 9 Maven: cyber wall
	printf("cyber wall\n"); all(SK_CYBERWALL, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.25f; sk_press(0); step(0.05f); CHECK(SKA[0].aim_ok, "wall preview"); sk_fire(0); int wo = SKA[0].obj;
	CHECK(SKO[wo].type == SKO_WALL && NEAR(SKO[wo].w, 300, 0.1f) && NEAR(SKO[wo].h, 200, 0.1f) && SKO[wo].nrm.x < -0.99f, "3 x 2 m facing Maven");
	{ V3 e; float wx = SKO[wo].pos.x; CHECK(!sk_bullet_block(3, v3(wx + 1500, 0, 100), v3(0, 0, 100), &e), "no block while building");
	  step(2.1f); CHECK(sk_bullet_block(3, v3(wx + 1500, 0, 100), v3(0, 0, 100), &e) && NEAR(e.x, wx, 1), "enemy bullets stop at the wall");
	  CHECK(!sk_bullet_block(1, v3(0, 0, 100), v3(wx + 1500, 0, 100), &e), "ally bullets pass");
	  CHECK(!sk_bullet_block(3, v3(wx + 1500, 0, 300), v3(0, 0, 300), &e), "over the top (2 m): passes");
	  ta[0].pos = v3(0, 0, 0); ta[3].pos = v3(wx + 600, 0, 0); CHECK(sk_can_see(3, 0) == 0 && sk_can_see(1, 3) == 1, "b83: the barrier hides Maven from enemies (allies see through)");
	  ta[3].pos = v3(wx + 100, 0, 0); ta[3].vel = v3(-400, 0, 0); step(0.6f); CHECK(SKA[3].mark_t[0] <= 0 && heard("intel_skill/screen_through"), "walking through: the shimmer, no reveal (v2.0)");
	  ta[1].pos = v3(wx - 100, 0, 0); ta[1].vel = v3(400, 0, 0); step(0.6f); CHECK(SKA[1].mark_t[1] <= 0, "allies pass freely");
	  for (int i = 0; i < 70 && SKO[wo].alive; i++) sk_bullet_block(3, v3(wx + 1500, 0, 100), v3(0, 0, 100), &e); CHECK(!SKO[wo].alive, "600 HP: shot down"); }
	scene(K); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.25f; sk_press(0); step(0.05f); sk_fire(0); wo = SKA[0].obj; step(20.1f); CHECK(!SKO[wo].alive, "20 s life");
	// the player walks through an enemy wall
	scene(K); ta[3].pos = v3(1000, 0, 0); ta[3].yaw = 3.14159f; ta[3].pitch = -0.25f; sk_press(3); sk_bot_place_at(3, v3(700, 0, 0)); step(2.1f);
	ta[0].pos = v3(400, 0, 0); ta[0].vel = v3(500, 0, 0); step(0.7f); CHECK(SKV.glitch > 0, "player: the barrier's red squares"); CHECK(SKA[0].mark_t[1] <= 0, "not revealed (v2.0)");

	// ---- 10 Reiko: Net Jump (b84, v2.0 EP26: the enemies looking at her lose their sight, she walks unseen, Tier 2 leaves a decoy)
	printf("net jump\n"); all(SK_CYBERDIVE, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = 0; ta[3].pos = v3(1200, 0, 0); ta[3].yaw = 3.14159f; ta[3].pitch = 0; ta[4].pos = v3(0, 1200, 0); ta[4].yaw = 1.5708f; ta[4].pitch = 0;
	sk_press(0);
	{ V3 c; CHECK(!sk_camera(0, &c), "no ghost camera: the body itself goes"); CHECK(sk_hide_viewmodel(0) && !sk_can_fire(0), "no gun");
	  float mx = 0, my = 1; int sp = 1, jp = 1; CHECK(!sk_move_input(0, &mx, &my, &sp, &jp, 1 / 60.f) && my == 1, "she walks"); }
	CHECK(SKA[3].hack_t > 1.1f && SKA[4].hack_t <= 0, "the enemy looking at her loses his sight, the one looking away not");
	CHECK(sk_can_see(4, 0) == 0 && sk_aim_factor(3) == 0, "unseen in the network; the hacked one cannot fire");
	step(0.95f); CHECK(NEAR(SKA[0].bar, 1, 0.01f), "Tier 1 bar full the first second"); step(0.1f); CHECK(NEAR(SKA[0].bar, 2 / 3.f, 0.01f), "1 s steps");
	step(2.0f); CHECK(SKA[0].state == SKS_COOLDOWN && sk_can_see(4, 0) == 1, "3 s, then back in sight");
	// b121: the exit scene - the network view kept on 0.57 s under the streaks, then the white blocks; the plain flash without the file
	if (sk_nj_sc[1].ok) { CHECK(sk_nj_play == 1 && sk_nj_hold > 0.4f && sk_nj_hold <= SK_NJ_HOLD, "b121: the exit scene runs, the dark world held (%.2f s left)", sk_nj_hold);
		step(0.6f); CHECK(sk_nj_hold == 0 && SKV.glitch > 0.2f, "b121: the world back under the white blocks"); step(0.7f); CHECK(sk_nj_play < 0, "b121: the scene over"); }
	else CHECK(SKV.flash > 0 || SKV.glitch > 0, "b121: no scene file - the white flash");
	printf("  net jump scenes: in %s (%.2f s), out %s (%.2f s)\n", sk_nj_sc[0].ok ? "loaded" : "not loaded", fxs_len(&sk_nj_sc[0]), sk_nj_sc[1].ok ? "loaded" : "not loaded", fxs_len(&sk_nj_sc[1]));
	scene(K); sk_set_tier(0, 0); ta[0].pos = v3(0, 0, 0); sk_press(0); step(1); ta[0].hp -= 10; step(0.05f); CHECK(SKA[0].state == SKS_COOLDOWN, "a hit ends the jump");
	scene(K); sk_set_tier(0, 400); ta[0].pos = v3(0, 0, 0); sk_use(0, 2);
	{ int dc = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_DECOY && SKO[o].owner == 0) dc = o;
	  CHECK(dc >= 0 && NEAR(SKO[dc].pos.x, 0, 1), "Tier 2: a decoy where she started");
	  step(5.9f); CHECK(SKA[0].state == SKS_ACTIVE, "Tier 2 longer"); step(0.2f); CHECK(SKA[0].state != SKS_ACTIVE, "about 6 s");
	  if (dc >= 0) { sk_obj_damage(dc, 1e6f, 3); CHECK(SKO[dc].alive, "holograms cannot be destroyed"); step(4.5f); CHECK(!SKO[dc].alive, "the decoy fades after it"); } }
	// a bot jumping while the player looks at it
	scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(1500, 0, 0); ta[0].yaw = 3.14159f; ta[0].pitch = 0; step(0.6f); CHECK(SKA[3].state == SKS_ACTIVE, "bot jumps");
	CHECK(SKA[0].hack_t > 0 && SKV.glitch > 0 && !sk_can_fire(0), "player watching it: sight hacked, no fire");

	// ---- 11 Sitara: DMD drone
	printf("DMD drone\n"); all(SK_DMDDRONE, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.2f; sk_press(0); step(0.05f); CHECK(SKA[0].aim_ok, "drone target point"); sk_fire(0); int dr = SKA[0].obj;
	CHECK(SKO[dr].type == SKO_DRONE && NEAR(SKO[dr].cur.z, 1500, 1) && NEAR(SKO[dr].pos.z, 350, 1), "flies in from 15 m up to hover 3.5 m");
	ta[3].pos = vadd(v3(SKO[dr].pos.x, SKO[dr].pos.y, 0), v3(600, 0, 0)); ta[4].pos = vadd(v3(SKO[dr].pos.x, SKO[dr].pos.y, 0), v3(1200, 0, 0));
	step(1.55f); CHECK(SKO[dr].phase == 1, "hovering after 1.5 s"); float e0 = SKO[dr].energy; ndlog = 0; step(2.0f);
	CHECK(NEAR(dmg_to(3, SKD_LASER), 38, 5) && dmg_to(4, -1) == 0, "lasers the enemy in 9 m at 20 dps (%.1f), not at 12 m", dmg_to(3, SKD_LASER));
	CHECK(NEAR(e0 - SKO[dr].energy, 2 / 20.f, 0.02f), "Tier 1: 1/20 per s, lasering or not (b83, %.3f)", e0 - SKO[dr].energy);
	{ int ic; float co, ba; sk_hud(0, &ic, &co, &ba); CHECK(ic == 19 && NEAR(ba, SKO[dr].energy, 0.002f), "HUD bar = drone energy"); }
	ta[3].alive = 0; e0 = SKO[dr].energy; step(10); CHECK(NEAR(e0 - SKO[dr].energy, 10 / 20.f, 0.02f), "idle 1/20 per s");
	{ V3 c = sk_obj_center(dr); for (int i = 0; i < 20 && SKO[dr].alive; i++) sk_bullet(v3(c.x - 600, c.y, c.z), v3(1, 0, 0), 2000, 3, 9); CHECK(!SKO[dr].alive, "150 HP: shot down"); }
	// a bot sends its drone to where it lost the player
	scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 0; ta[3].lost = 1; ta[3].last = v3(800, 0, 0); step(0.6f); int bd = SKA[3].obj;
	CHECK(bd >= 0 && SKO[bd].type == SKO_DRONE && NEAR(SKO[bd].pos.x, 800, 1), "bot drone at the last seen spot");
	ta[0].pos = v3(800, 300, 0); step(2.0f); CHECK(dmg_to(0, SKD_LASER) > 10 && SKV.vig > 0, "it lasers the player (red edge)");

	// ---- 0 Batou: arm launcher
	printf("arm launcher\n"); all(SK_ARMLAUNCHER, K); scene(K); sk_set_tier(0, 0);
	ta[0].pos = v3(0, 0, 0); ta[0].yaw = 1.5708f; ta[0].pitch = 0; ta[3].pos = v3(-50, 1500, 0); ta[4].pos = v3(100, 2500, 0); ta[5].pos = v3(1500, 1500, 0);   // facing +y (the test wall is at +x)
	sk_press(0); CHECK(SKA[0].state == SKS_AIM && sk_hide_viewmodel(0) && !sk_can_fire(0), "launcher mode");
	{ int ph; float pt; CHECK(sk_launcher_vm(0, &ph, &pt) && ph == 0, "viewmodel: arm swap-in"); }
	step(1.2f); CHECK(SKA[0].nlocks == 0 && sk_fire(0) && SKA[0].state == SKS_AIM, "1.37 s swap: no locks, R does nothing yet"); step(0.3f);
	{ int ph; float pt; CHECK(sk_launcher_vm(0, &ph, &pt) && ph == 1, "viewmodel: aiming"); } CHECK(SKA[0].nlocks == 1 && SKA[0].locks[0] == 3, "first lock: nearest to the view axis");
	step(1.0f); CHECK(SKA[0].nlocks == 1, "Tier 1: one lock (footage: one rocket)");
	sk_fire(0); CHECK(SKA[0].state == SKS_COOLDOWN && NEAR(SKA[0].cool, 25, 0.01f), "fire -> cooldown 25");
	{ int ph; float pt; CHECK(sk_launcher_vm(0, &ph, &pt) && ph == 2 && sk_hide_viewmodel(0) && !sk_can_fire(0), "viewmodel: shot, then the arm folds back (no gun yet)"); }
	int nm = 0; for (int m = 0; m < SK_MAXMS; m++) nm += SKMS[m].on; CHECK(nm == 1, "one missile");
	step(1.5f); CHECK(dmg_to(3, SKD_BLAST) >= 100 && dmg_to(5, -1) == 0, "it hits (%.0f)", dmg_to(3, SKD_BLAST));
	CHECK(!sk_hide_viewmodel(0) && sk_can_fire(0), "gun back 1.25 s after the shot (footage 43.4 -> 44.65 s)");
	{ scene(K); ta[0].pos = v3(0, 0, 0); sk_press(0); step(2); sk_press(0); int ph; float pt; CHECK(SKA[0].state != SKS_AIM && sk_launcher_vm(0, &ph, &pt) && ph == 3 && sk_hide_viewmodel(0), "cancel: the launcher folds back (no shot)"); step(1); CHECK(!sk_hide_viewmodel(0), "...then the gun"); }
	nm = 0; for (int m = 0; m < SK_MAXMS; m++) nm += SKMS[m].on; CHECK(nm == 0, "missiles spent");
	scene(K); sk_set_tier(0, 500); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 1.5708f; for (int k = 3; k < 8; k++) ta[k].pos = v3((k - 5) * 60.f, 1500 + k * 100, 0);
	sk_use(0, 2); step(3); CHECK(SKA[0].nlocks == 4, "Tier 2: 4 locks"); sk_press(0); CHECK(SKA[0].state != SKS_AIM && SKA[0].intel == 0, "the skill button fires too (intel spent)");
	scene(K); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 1.5708f; ta[3].pos = v3(0, 1500, 0); sk_press(0); step(8.1f); CHECK(SKA[0].state == SKS_COOLDOWN, "AIM times out after 8 s (cooldown)");
	// bot launcher
	scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].yaw = 1.5708f; ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(0, 2000, 0); step(5.5f);
	CHECK(SKA[3].state == SKS_COOLDOWN && heard("intel_skill/armlauncher_rocket_fire"), "bot fires its missiles"); step(2); CHECK(dmg_to(0, SKD_BLAST) > 0, "they hit the player");

	// ---- SkillSync (tutorial 6:14-6:48, Neon Ruins 1:40, Electronics Center 4:55)
	printf("skillsync\n");
	{ all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_SENTRYGUN; K[2] = SK_HEATSCAN; scene(K); bots_on();
	  ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(500, 0, 0); ta[2].pos = v3(1500, 0, 0); for (int k = 3; k < 9; k++) ta[k].pos = v3(-6000, 4000 + k * 300.f, 0);
	  for (int a = 1; a < 9; a++) SKA[a].bot_t = 99;   // nobody uses skills on their own here
	  sk_set_tier(0, 400); sk_use(0, 2); CHECK(heard("d_motoko_share"), "sharer's share line");
	  CHECK(sk_sync_link(0, 1) == 1 && sk_sync_link(0, 2) == 1 && sk_sync_link(0, 3) == 0, "white link lines to the two allies, none to enemies");
	  { float g[3]; CHECK(sk_bot_goal(2, g) && NEAR(g[0], 0, 1), "the far ally walks over to download"); }
	  step(2.5f); CHECK(SKA[1].sx_skill == SK_OPTICAL && sk_cloaked(1) && SKA[1].sx_t > 28 && SKA[1].sx_t <= 30, "the near ally downloaded the camo (%.1f s of 30)", SKA[1].sx_t);
	  CHECK(SKA[2].sx_t <= 0, "the far one not yet"); CHECK(sk_sync_link(0, 1) == 2 && SKA[0].nsync == 1 && npoints == SK_SYNC_PTS, "slot filled, SKILLSYNC COMPLETED +20");
	  { SkHudInfo hi; sk_hud_info(0, &hi); CHECK(hi.nsync == 1 && hi.sync_name[0] && !strcmp(hi.sync_name[0], "Ishikawa"), "HUD slot: Ishikawa"); }
	  ta[3].pos = v3(1200, 0, 0); CHECK(sk_can_see(3, 1) == 0, "enemies can't see the synced ally beyond 4 m");
	  ta[2].pos = v3(600, 300, 0); step(2.5f); CHECK(SKA[2].sx_skill == SK_OPTICAL && SKA[0].nsync == 2, "second ally synced");
	  ta[0].alive = 0; step(0.1f); CHECK(sk_cloaked(1) && !sk_cloaked(0), "the sharer's death doesn't end the downloads"); ta[0].alive = 1;
	  float l1 = SKA[1].sx_t; step(l1 + 0.1f); CHECK(!sk_cloaked(1) && heard("intel_skill/cloaking_end"), "download over"); }
	{ // the player downloads a bot's heat scan
	  all(SK_SENTRYGUN, K); K[0] = SK_RUSH; K[1] = SK_HEATSCAN; scene(K); ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(600, 0, 0); ta[3].pos = v3(3000, 0, 0);
	  sk_set_tier(1, 400); sk_use(1, 2); CHECK(sk_sync_source(0, SK_SYNC_R) == 1, "prompt: Saito's heat sensor in reach");
	  aud_nlog = 0; CHECK(sk_sync_try(0) && SKA[0].sx_skill == SK_HEATSCAN && heard("intel_skill/intel_transfer_fp") && heard("saito_transferintel"), "Square: downloaded (+ transfer sound, Operator)");
	  CHECK(!sk_sync_try(0), "only once"); step(0.4f); CHECK(SKA[3].heat_t[0] > 0 && sk_heat_pulse(0) > 0, "my downloaded scan pulses and reveals"); step(7); CHECK(SKA[0].sx_pulses == 2 && SKA[0].sx_t > 0, "b83: the sharer's Tier 2 schedule - a scan every 7 s (%d)", SKA[0].sx_pulses); step(29); CHECK(SKA[0].sx_t <= 0, "...over with the sharer's 35 s"); }
	{ // shield and rush downloads; the same skill can't be downloaded; at most 3
	  all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_PLASMASHIELD; K[2] = SK_PLASMASHIELD; scene(K); ta[0].pos = v3(0, 0, 0); ta[0].yaw = 0; ta[1].pos = v3(-300, 0, 0); ta[2].pos = v3(-300, 300, 0);
	  sk_set_tier(1, 500); sk_use(1, 2); CHECK(sk_sync_try(0) && sk_shield_kind(0) == 2, "downloaded shield panel");
	  { V3 e; CHECK(sk_bullet_block(3, v3(1500, 0, 115), v3(0, 0, 115), &e) && SKA[0].sx_energy < 1, "it absorbs enemy shots"); }
	  CHECK(sk_sync_link(1, 2) == 0, "another Azuma can't download a shield");
	  all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_RUSH; scene(K); ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(300, 0, 0); sk_set_tier(1, 400); sk_use(1, 2); sk_sync_try(0);
	  CHECK(sk_speed_mul(0, 1) == 2 && sk_can_fire(0), "downloaded rush: x2 sprint, the gun stays (v2.0)");
	  all(SK_SENTRYGUN, K); K[1] = SK_OPTICAL; for (int k = 3; k < 9; k++) K[k] = SK_SENTRYGUN; scene(K); for (int a = 0; a < 9; a++) { ta[a].team = 0; SKA[a].team = 0; ta[a].pos = v3(a * 50.f, 0, 0); }
	  sk_set_tier(1, 400); sk_use(1, 2); int ns = 0; for (int a = 0; a < 9; a++) if (a != 1) { if (sk_sync_source(a, SK_SYNC_R) == 1) { sk_sync_do(1, a); ns++; } }
	  CHECK(ns == 3 && SKA[1].nsync == 3, "at most 3 allies (%d)", ns); }
	{ // commander-box messages (late-2016 footage n2 / n6 / n7 / n8)
	  int f = 0; const char *n = "", *m = ""; float t = 0;
	  all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_RUSH; K[2] = SK_SENTRYGUN; K[3] = SK_OPTICAL; scene(K);
	  ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(300, 0, 0); ta[2].pos = v3(0, 300, 0); ta[3].pos = v3(-9000, 9000, 0); ta[3].yaw = 3.14f;
	  CHECK(!sk_msg(&f, &n, &m, &t), "no message at the start");
	  sk_use(1, 1); CHECK(sk_msg(&f, &n, &m, &t) && f == 4 && !strcmp(n, "Ishikawa") && !strcmp(m, "An ally activated Hyper Sprint!"), "ally use: face %d %s: %s", f, n, m);
	  sk_use(3, 1); CHECK(sk_msg(&f, &n, &m, &t) && !strcmp(m, "An ally activated Hyper Sprint!"), "an enemy's use is not an ally message");
	  step(1.6f); sk_use(2, 1); sk_place(2, v3(0, 400, 0), v3(0, 0, 1));
	  CHECK(sk_msg(&f, &n, &m, &t) && f == 5 && !strcmp(m, "An ally deployed a Cyber Sentry!") && t < 0.1f, "a newer message replaces one shown 1.5 s: %s", m);
	  sk_use(0, 1); CHECK(sk_msg(&f, &n, &m, &t) && !strcmp(m, "An ally deployed a Cyber Sentry!"), "own use: no ally message");
	  ta[3].pos = v3(1500, 0, 0); step(2.6f); CHECK(sk_msg(&f, &n, &m, &t) && f == SK_FACE_OPERATOR && !strcmp(n, "Operator") && !strcmp(m, "Enemy Therm-optic Camo user detected!"), "enemy camo in view: %s", m);
	  step(6); CHECK(!sk_msg(&f, &n, &m, &t), "gone after 5.45 s (%d %s)", sk_mq_n, sk_mq_n ? sk_mq[0].text : "");
	  for (int i = 0; i < 60 && !sk_mq_n; i++) step(0.5f); CHECK(!sk_msg(&f, &n, &m, &t), "no message when Tier 1 is back (Neon footage)");
	  sk_set_tier(0, 400); CHECK(sk_msg(&f, &n, &m, &t) && f == SK_FACE_OPERATOR && !strcmp(m, "Therm-optic Camo ready."), "own Tier 2 ready: %s", m);
	  all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_RUSH; scene(K); ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(300, 0, 0);
	  sk_set_tier(1, 400); sk_use(1, 2); step(3); sk_sync_try(0);
	  CHECK(sk_msg(&f, &n, &m, &t) && f == 4 && !strcmp(m, "Transfer complete."), "downloaded an ally's Tier 2: %s", m);
	  all(SK_SENTRYGUN, K); K[0] = SK_OPTICAL; K[1] = SK_HEATSCAN; scene(K); ta[0].pos = v3(0, 0, 0); ta[1].pos = v3(300, 0, 0);
	  sk_set_tier(0, 400); sk_use(0, 2); step(3); sk_sync_do(0, 1);
	  CHECK(sk_msg(&f, &n, &m, &t) && f == 3 && !strcmp(n, "Ishikawa") && !strcmp(m, "Share code transmission complete."), "an ally downloaded mine: %d %s", f, m);
	  scene(K); sk_msg_enemy(SK_RUSH); sk_msg(&f, &n, &m, &t); CHECK(!strcmp(m, "Enemy Hyper-Sprint user detected!"), "rush: %s", m);
	  scene(K); sk_msg_enemy(SK_SENTRYGUN); sk_msg(&f, &n, &m, &t); CHECK(!strcmp(m, "Enemy Cyber Sentry detected!"), "sentry: %s", m);
	  scene(K); SKA[2].skill = SK_EMP; sk_msg_ally(2); sk_msg(&f, &n, &m, &t); CHECK(!strcmp(m, "An ally deployed an EMP Generator!"), "article: %s", m); }

	// ---- bot rules for the rest
	printf("bot rules\n");
	{ all(SK_OPTICAL, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(2000, 0, 0); step(2.1f); CHECK(SKA[3].state == SKS_ACTIVE, "camo: enemy at 20 m"); }
	{ all(SK_HEATSCAN, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].sees = 0; step(6.1f); CHECK(SKA[3].state != SKS_READY, "heat scan: nobody seen for 4 s"); }
	{ all(SK_SENTRYGUN, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(1500, 0, 0); step(2.1f); CHECK(SKA[3].obj >= 0, "sentry: engaged beyond 10 m"); }
	{ all(SK_METABOOST, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].hp = 40; step(2.1f); CHECK(SKA[3].state == SKS_ACTIVE, "metaboost: HP < 50 %%"); }
	{ all(SK_PLASMASHIELD, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; step(0.5f); ta[3].hp = 60; step(1.7f); CHECK(SKA[3].state == SKS_ACTIVE, "shield: under fire, HP < 70 %%"); }
	{ all(SK_CYBERWALL, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[3].hp = 50; ta[0].pos = v3(1500, 0, 0); step(2.1f); CHECK(SKA[3].obj >= 0, "wall: engaged far, HP < 60 %%"); }
	{ all(SK_EMP, K); K[0] = SK_SENTRYGUN; scene(K); bots_on(); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.5f; sk_press(0); step(0.05f); sk_fire(0); ta[3].pos = v3(900, 0, 0); step(2.1f);
	  CHECK(SKA[3].obj >= 0 && SKO[SKA[3].obj].type == SKO_EMP, "EMP: a hostile sentry in sight"); step(4.5f); CHECK(SKA[0].obj < 0 || !SKO[SKA[0].obj].alive, "...and fries it"); }
	{ all(SK_SPECIALRECON, K); scene(K); bots_on(); ta[3].pos = v3(0, 0, 0); ta[3].tgt = 0; ta[3].sees = 1; ta[0].pos = v3(2000, 0, 0); step(2.1f); CHECK(SKA[3].state != SKS_READY, "seeker: an enemy in sight within 30 m"); }
	{ int k2[SK_MAXACT]; all(SK_SENTRYGUN, k2); k2[3] = SK_RUSH; scene(k2); bots_on(); ta[0].pos = v3(0, 0, 0); ta[0].pitch = -0.5f; sk_press(0); step(0.05f); sk_fire(0); int tt = SKA[0].obj; step(2.1f);
	  ta[3].pos = vadd(SKO[tt].pos, v3(-1000, 0, 0)); ta[3].sees = 0; SKA[3].bot_t = 99; step(2.0f);
	  CHECK(SKA[3].otgt == SK_TGT_OBJ + tt && SKO[tt].hp < SKO[tt].hpmax, "no operator in sight: bots shoot the sentry (%.0f HP)", SKO[tt].hp); }

	// ---- player hooks: bullets vs objects, blast, hit notify; HUD / drawing paths and GL state
	printf("drawing\n"); { int k3[SK_MAXACT] = { SK_SENTRYGUN, SK_PLASMASHIELD, SK_CYBERWALL, SK_DMDDRONE, SK_EMP, SK_SPECIALRECON, SK_OPTICAL, SK_HEATSCAN, SK_ARMLAUNCHER }; scene(k3); }
	for (int a = 0; a < SK_MAXACT; a++) { ta[a].pos = v3(-1200 + a * 400.f, 300, 0); ta[a].pitch = -0.4f; ta[a].tgt = 0; ta[a].sees = 1; }
	for (int a = 0; a < SK_MAXACT; a++) { sk_set_tier(a, 800); sk_press(a); if (SKA[a].state == SKS_AIM && SK_DEF[SKA[a].skill].placed) sk_bot_place_at(a, vadd(ta[a].pos, v3(0, 250, 0))); }
	sk_press(1); sk_press(1);   // shield: off and on again
	step(2.5f); sk_press(0); step(0.1f);   // the player previews a second sentry
	{ float VP[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -0.001f, 1, 0, 0, 0, 1 }, cam[3] = { 0, -1500, 200 }, R[3] = { 1, 0, 0 }, U[3] = { 0, 0, 1 }, fog[4] = { 0.5f, 0.6f, 0.7f, 1 };
	  SKA[6].state = SKS_ACTIVE; SKA[6].skill = SK_OPTICAL; SKA[8].heat_t[0] = 1; SKA[4].mark_t[0] = 2;
	  gls_draws = gls_tris = 0; sk_draw_world(VP, cam, R, U, fog); sk_draw_world_late(VP, cam, R, U, fog); long dw = gls_draws, tw = gls_tris;
	  CHECK(gls_depth && gls_cull && !gls_blend && gls_dmask, "GL state restored after the world pass");
	  gls_draws = gls_tris = 0; SKV.glitch = SKV.glitch_len = 0.5f; SKV.vig = SKV.vig_len = 0.5f; SKV.camo = 1; SKV.heat = 0.3f; sk_draw_overlays(0); long dov = gls_draws;
	  CHECK(gls_depth, "depth test back on after the overlays");
	  hud_begin(); gls_draws = 0; HudState H; memset(&H, 0, sizeof(H)); sk_hud_state(0, &H); SKV.t2n = 2; SKV.sync_note = 1; skills_hud(0, VP, 1.0f); hud_end();
	  printf("  world pass: %ld draw calls, %ld triangles (%d objects, %d actors); overlays: %ld draws\n", dw, tw, 8, SK_MAXACT, dov);
	  CHECK(dw < 40, "few draw calls (%ld)", dw); }
	{ int k4[SK_MAXACT]; all(SK_SENTRYGUN, k4); scene(k4); ta[3].pos = v3(0, 0, 0); ta[3].pitch = -0.5f; sk_press(3); sk_bot_place_at(3, v3(200, 0, 0)); int o = SKA[3].obj; step(2.1f);
	  sk_blast(v3(200, 0, 50), 650, 160, 0); CHECK(SKO[o].hp < 300 && SKO[o].hp > 0, "grenade blast damages a sentry (%.0f HP left)", SKO[o].hp);
	  nnotify = 0; for (int i = 0; i < 3 && SKO[o].alive; i++) sk_blast(v3(200, 0, 50), 650, 160, 0); CHECK(!SKO[o].alive && nnotify == 1 && !strcmp(last_notify, "SENTRY DESTROYED"), "destroyed: notice to the player"); }

	// ---- cost: a busy fight, 9 actors, every deployable kind, missiles
	printf("cost\n"); { int k5[SK_MAXACT] = { SK_ARMLAUNCHER, SK_SENTRYGUN, SK_DMDDRONE, SK_SENTRYGUN, SK_CYBERWALL, SK_SPECIALRECON, SK_DMDDRONE, SK_EMP, SK_OPTICAL }; scene(k5); }
	for (int a = 0; a < SK_MAXACT; a++) { ta[a].pos = v3((a % 3) * 700.f - 700, (a / 3) * 700.f - 700, 0); ta[a].pitch = -0.4f; ta[a].tgt = a < 3 ? 3 : 0; ta[a].sees = 1; ta[a].hp = 1e6f; sk_set_tier(a, 800); }
	for (int a = 1; a < SK_MAXACT; a++) { sk_press(a); if (SKA[a].state == SKS_AIM && SK_DEF[SKA[a].skill].placed) sk_bot_place_at(a, vadd(ta[a].pos, v3(0, 250, 0))); }
	step(3); sk_press(0); step(1.5f); sk_fire(0);
	{ struct timespec t0, t1; long rays = 0; clock_gettime(CLOCK_MONOTONIC, &t0);
	  for (int i = 0; i < 600; i++) { for (int a = 0; a < n_act; a++) ta[a].yaw += 0.01f; skills_update(1 / 60.f); rays += sk_stat_rays; }
	  clock_gettime(CLOCK_MONOTONIC, &t1); double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / 600;
	  int no = 0; for (int o = 0; o < SK_MAXOBJ; o++) no += SKO[o].alive;
	  printf("  skills_update: %.1f us/frame on this host (ASan build), %.1f map rays/frame, %d deployables\n", us, rays / 600.0, no); }

	printf("audio: %d events, %d RAM sounds, %d voice lines played; unknown names: %d\n", aud_events, aud_ram, aud_voice, aud_bad);
	CHECK(aud_bad == 0, "every event / sound name exists");
	// ---- shield captures for the host renders (tools/skills/host/skshield/render.py): SKSH_CAP=<dir>
	if (getenv("SKSH_CAP")) {
		const char *dir = getenv("SKSH_CAP");
		struct { const char *name; int tier, ph_wait_ms, enemy, tp; float hit, t_after; int brk, lower; } SC[] = {
			{ "fp_t1_up", 1, 0, 0, 0, 0, 0.5f, 0, 0 }, { "fp_t1_start", 1, 0, 0, 0, 0, 0.12f, 0, 0 }, { "fp_t1_hit", 1, 0, 0, 0, 0.25f, 0.5f, 0, 0 },
			{ "fp_t1_lower", 1, 0, 0, 0, 0, 0.5f, 0, 1 }, { "fp_t1_break", 1, 0, 0, 0, 0, 0.5f, 1, 0 }, { "fp_t2_up", 2, 0, 0, 0, 0, 2.5f, 0, 0 },
			{ "tp_ally", 1, 0, 0, 1, 0, 0.5f, 0, 0 }, { "tp_ally_t2", 2, 0, 0, 1, 0, 2.5f, 0, 0 }, { "tp_enemy", 1, 0, 1, 2, 0.2f, 0.5f, 0, 0 } };
		for (unsigned k = 0; k < sizeof(SC) / sizeof(SC[0]); k++) {
			int K6[SK_MAXACT]; all(SK_SENTRYGUN, K6); K6[0] = SK_PLASMASHIELD; K6[1] = SK_PLASMASHIELD; K6[3] = SK_PLASMASHIELD; scene(K6);
			for (int a = 0; a < SK_MAXACT; a++) ta[a].pos = v3(-9000 - a * 300.f, 9000, 0);
			int h = SC[k].tp == 0 ? 0 : SC[k].tp == 1 ? 1 : 3;   // the shield holder
			ta[h].pos = v3(0, 0, 0); ta[h].yaw = SC[k].tp == 2 ? 3.14159f : 0;
			sk_set_tier(h, SC[k].tier == 2 ? 500 : 0); sk_use(h, SC[k].tier);
			step(SC[k].t_after);
			if (SC[k].hit > 0) SKA[h].sh_hit = SC[k].hit;
			if (SC[k].lower) { sk_press(h); step(0.06f); }
			if (SC[k].brk) { SKA[h].energy = 0.001f; sk_shield_absorb(h, v3(60, 0, 140), v3(-1, 0, 0), 10); step(0.12f); }
			float eye[3], yaw = 0, pitch = 0;
			if (SC[k].tp == 0) { eye[0] = 0; eye[1] = 0; eye[2] = P_EYE; }
			else if (SC[k].tp == 1) { eye[0] = -260; eye[1] = 230; eye[2] = 210; yaw = atan2f(-230, 260); pitch = -0.12f; }   // behind-left of the ally, 3.5 m
			else { eye[0] = -800; eye[1] = 0; eye[2] = 160; yaw = 0; }   // facing the enemy, 8 m
			float P[16], V[16], VP[16], F[3]; float t = 1.0f / tanf(59 * 0.5f * 3.14159f / 180), asp = 960.0f / 544, n_ = 10, f_ = 12000;
			memset(P, 0, 64); P[0] = t / asp; P[5] = t; P[10] = (f_ + n_) / (n_ - f_); P[11] = -1; P[14] = 2 * f_ * n_ / (n_ - f_);
			{ float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw); float Fv[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * Fv[2] - R[2] * Fv[1], R[2] * Fv[0] - R[0] * Fv[2], R[0] * Fv[1] - R[1] * Fv[0] };
			  memcpy(F, Fv, 12); float v[16] = { R[0], U[0], -Fv[0], 0, R[1], U[1], -Fv[1], 0, R[2], U[2], -Fv[2], 0, -(R[0] * eye[0] + R[1] * eye[1] + R[2] * eye[2]), -(U[0] * eye[0] + U[1] * eye[1] + U[2] * eye[2]), (Fv[0] * eye[0] + Fv[1] * eye[1] + Fv[2] * eye[2]), 1 }; memcpy(V, v, 64);
			  sk_mul4(VP, P, V);
			  char fn[256]; snprintf(fn, sizeof(fn), "%s/%s.cap", dir, SC[k].name); gls_cap = fopen(fn, "wb");
			  float Rr[3] = { R[0], R[1], R[2] }, Ur[3] = { U[0], U[1], U[2] };
			  if (SC[k].tp == 0) { ta[0].pos = v3(0, 0, 0); }   // the eye: w_eye = pos + P_EYE
			  sk_draw_world_late(VP, eye, Rr, Ur, NULL);
			  fclose(gls_cap); gls_cap = NULL; printf("  capture %s (holder %d, phase %d, t %.2f)\n", fn, h, SKA[h].sh_ph, SKA[h].sh_t); }
		}
		for (int en = 0; en < 2; en++) {   // cyber walls: an ally's (T1) and an enemy's, 6 m ahead, seen from eye height
			int K7[SK_MAXACT]; all(SK_SENTRYGUN, K7); K7[1] = SK_CYBERWALL; K7[3] = SK_CYBERWALL; scene(K7);
			for (int a = 0; a < SK_MAXACT; a++) ta[a].pos = v3(-9000 - a * 300.f, 9000, 0);
			int h = en ? 3 : 1; ta[h].pos = v3(900, 0, 0); ta[h].yaw = 3.14159f; sk_set_tier(h, 0); sk_use(h, 1); sk_place(h, v3(600, 0, 0), v3(0, 0, 1)); step(2.5f);
			float eye[3] = { 0, 60, P_EYE }, yaw = 0, P[16], V[16], VP[16]; float t = 1.0f / tanf(59 * 0.5f * 3.14159f / 180), asp = 960.0f / 544, n_ = 10, f_ = 12000;
			memset(P, 0, 64); P[0] = t / asp; P[5] = t; P[10] = (f_ + n_) / (n_ - f_); P[11] = -1; P[14] = 2 * f_ * n_ / (n_ - f_);
			float cy = cosf(yaw), sy = sinf(yaw), Fv[3] = { cy, sy, 0 }, R[3] = { sy, -cy, 0 }, U[3] = { 0, 0, 1 };
			float v[16] = { R[0], U[0], -Fv[0], 0, R[1], U[1], -Fv[1], 0, R[2], U[2], -Fv[2], 0, -(R[0] * eye[0] + R[1] * eye[1]), -(U[2] * eye[2]), (Fv[0] * eye[0] + Fv[1] * eye[1]), 1 }; memcpy(V, v, 64); sk_mul4(VP, P, V);
			char fn[256]; snprintf(fn, sizeof(fn), "%s/%s.cap", dir, en ? "wall_enemy" : "wall_ally"); gls_cap = fopen(fn, "wb");
			sk_draw_world_late(VP, eye, R, U, NULL); fclose(gls_cap); gls_cap = NULL; printf("  capture %s (wall %d)\n", fn, SKA[h].obj);
		}
	}
	printf("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
