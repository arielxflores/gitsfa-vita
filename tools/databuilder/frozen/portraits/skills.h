// Operator skills (GITS:SAC First Assault): the 12 operators' special abilities, for the player and the bots.
// Spec, numbers and sources: tools/skills/SKILLS.md (skill videos + footage + the game's effect/model/sound data).
// Data: sk_*.gvm (deployables, every mesh rigid on one bone: one draw call per mesh, no CPU skinning), fx_sk.gfx
// (the SK_* effects and atlas cells, looked up BY NAME so base effect ids may shift), sk_ovl.gtx (screen overlays).
// The engine gives world access through SkWorld (actor 0 = the local player, 1 + i = bot i); nothing here reads main.c
// or bots.h. Never NaturalMotion, never GAME.exe: everything is reconstructed from the data files and footage.
// Cost on the Vita (Cortex-A9 444 MHz): logic ~0.05-0.15 ms/frame (fixed pools; map rays only at 5 Hz per deployable,
// per missile step and for the local placement preview), drawing: <= ~10 rigid draw calls, one or two sprite batches,
// at most ~3 full-screen overlay quads.
#pragma once
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"

enum { SK_ARMLAUNCHER, SK_OPTICAL, SK_HEATSCAN, SK_SPECIALRECON, SK_SENTRYGUN, SK_METABOOST, SK_RUSH, SK_PLASMASHIELD,
	SK_EMP, SK_CYBERWALL, SK_CYBERDIVE, SK_DMDDRONE, SK_COUNT };                       // == CHARS[] / MENU_CHR order
enum { SKS_READY, SKS_AIM, SKS_ACTIVE, SKS_COOLDOWN };
enum { SKO_NONE, SKO_TURRET, SKO_WALL, SKO_DRONE, SKO_EMP, SKO_RECON };
enum { SKD_BULLET, SKD_BLAST, SKD_MELEE, SKD_LASER, SKD_SHOCK };                        // damage kinds (SkWorld.damage)
enum { SKN_HIT, SKN_KILL, SKN_TEXT };                                                   // SkWorld.notify: hit marker, kill-style notice (+pts), plain notice
#define SK_MAXACT 9
#define SK_MAXOBJ 16
#define SK_MAXMS 16
#define SK_TGT_OBJ 100      // target encoding: 0..SK_MAXACT-1 an actor, SK_TGT_OBJ + o a deployable, -1 none

typedef struct {
	const char *id, *name, *desc;     // voice token ("using_<id>", "op_<id>_ready"), menu strings
	int hud_icon, menu_icon;          // hud.ghud skill.N, GameLoading skillIconMc frame or -1
	int placed;                       // 1: AIM shows a placement hologram, 2: a target point (drone)
	float cooldown[3], duration[3];   // per tier (T1..T3)
	float p1[3], p2[3];               // per-skill tier values (see the table)
	float range;                      // placement range (cm)
} SkillDef;
// p1 / p2: launcher locks / missile damage; camo near-reveal distance; heat pulses / radius; recon radius / HP; sentry arc (deg) / HP;
// metaboost HP cap (x max) / heal total; rush dash drain per s / idle drain; shield HP-equivalent / idle drain; EMP radius / shock s;
// wall width / height; dive hack s / dive range; drone laser dps / HP. [E] = estimates (DB.dat is encrypted), see SKILLS.md 3.x
static const SkillDef SK_DEF[SK_COUNT] = {
	{ "armlauncher", "ARM LAUNCHER", "Turn your arm into a missile pod: lock on to enemies and fire homing missiles.", 12, -1, 0,
	  { 50, 50, 50 }, { 8, 8, 8 }, { 2, 3, 4 }, { 120, 120, 120 }, 0 },
	{ "optical", "THERM-OPTIC CAMO", "Bend light around your body and become nearly invisible.", 8, 0, 0,
	  { 60, 60, 60 }, { 15, 18, 22 }, { 400, 400, 400 }, { 0, 0, 0 }, 0 },
	{ "heatscan", "HEAT SCAN", "Pulse a heat scan: see enemies through walls.", 3, 1, 0,
	  { 45, 45, 45 }, { 5.3f, 7.8f, 10.3f }, { 4, 6, 8 }, { 4000, 4000, 4000 }, 0 },
	{ "specialrecon", "SPECIAL RECON", "Deploy a recon Tachikoma that tracks every noisy enemy around it.", 13, -1, 1,
	  { 45, 45, 45 }, { 16, 16, 16 }, { 1157, 1400, 1700 }, { 80, 80, 80 }, 700 },
	{ "sentrygun", "SENTRY GUN", "Deploy an automated sentry gun.", 1, 7, 1,
	  { 60, 60, 60 }, { 30, 30, 30 }, { 90, 180, 360 }, { 300, 300, 300 }, 300 },
	{ "metaboost", "METABOOST", "Boost your team's metabolism: fast healing for you and nearby allies.", 2, 2, 0,
	  { 50, 50, 50 }, { 17.5f, 17.5f, 17.5f }, { 1.0f, 1.4f, 1.8f }, { 80, 80, 80 }, 0 },
	{ "rush", "RUSH", "Charge at full speed: take down any enemy you reach.", 9, 3, 0,
	  { 40, 40, 40 }, { 2.2f, 2.8f, 3.4f }, { 0.45f, 0.36f, 0.30f }, { 0.048f, 0.048f, 0.048f }, 0 },
	{ "plasmashield", "PLASMA SHIELD", "Raise a plasma shield that stops bullets from the front.", 17, -1, 0,
	  { 45, 45, 45 }, { 30, 30, 30 }, { 400, 550, 700 }, { 0.033f, 0.033f, 0.033f }, 0 },
	{ "emp", "EMP SHOCKWAVE", "Plant an EMP that fries enemy devices and shocks enemy operators.", 16, -1, 1,
	  { 50, 50, 50 }, { 2, 2, 2 }, { 500, 650, 800 }, { 3, 3, 3 }, 700 },
	{ "cyberwall", "CYBER WALL", "Raise a holo wall: it stops enemy fire and hacks enemies who cross it.", 5, 6, 1,
	  { 50, 50, 50 }, { 20, 20, 20 }, { 300, 400, 500 }, { 200, 250, 300 }, 700 },
	{ "cyberdive", "CYBER DIVE", "Dive into the net and hack an enemy's cyberbrain.", 18, -1, 0,
	  { 55, 55, 55 }, { 5, 5, 5 }, { 3, 4, 5 }, { 400, 400, 400 }, 0 },
	{ "dmddrone", "DMD DRONE", "Call in a drone that burns enemies with its laser.", 19, -1, 2,
	  { 60, 60, 60 }, { 90, 90, 90 }, { 20, 26, 32 }, { 150, 150, 150 }, 3000 },
};
// operator stems (chr_<stem>.gvm, voice events voice/action/<stem>/...), same order
static const char *const SK_STEM[SK_COUNT] = { "bato", "motoko", "saito", "togusa", "ishikawa", "borma", "paz", "azuma", "kuro", "maven", "reiko", "sitara" };
static int sk_skill_of_stem(const char *s) { for (int i = 0; s && i < SK_COUNT; i++) if (!strcmp(SK_STEM[i], s)) return i; return -1; }

// ---- world access (the engine fills this; any pointer may be NULL where noted)
typedef struct {
	int   (*n_actors)(void);                         // 1 + bots
	V3    (*pos)(int a);                             // feet
	V3    (*vel)(int a);
	float (*yaw)(int a);                             // body / view yaw
	int   (*alive)(int a);
	V3    (*eye)(int a);
	V3    (*aim_dir)(int a);                         // unit view direction
	float (*hp)(int a);                              // current HP (max 100)
	int   (*los)(V3 a, V3 b);                        // map ray, 1 = clear
	int   (*damage)(int victim, float dmg, int attacker, int kind);   // returns 1 on a kill
	void  (*heal)(int a, float add, float cap_mul);  // add > 0: heal up to cap_mul x max HP; add <= 0: clamp to cap_mul x max
	int   (*firing)(int a);                          // 1 = fired this frame (NULL: never)
	int   (*bot_info)(int a, int *target, int *sees, float *lost_t, V3 *last_seen);   // bots only (target: actor or -1); 0 = no info
	void  (*shot_fx)(V3 from, V3 to);                // a muzzle flash + tracer (NULL: none)
	void  (*notify)(int a, int what, const char *text, int pts);      // HUD feedback for actor a (hit marker / "SENTRY DESTROYED" +50)
} SkWorld;

typedef struct {
	int on, skill, team, tier, state;
	float t, cool, energy, bar;                // t: time in the state; cool: cooldown left (counts from activation)
	int obj, nlocks, locks[4]; float lock_t;   // deployable slot; arm-launcher locks
	int pulses; float pulse_t;                 // heat scan
	int toggled; V3 dome;                      // plasma shield (on/off; T3 dome centre)
	V3 cam; float dive_still;                  // cyber dive ghost camera
	unsigned linked; float link_t, tick_t;     // metaboost
	float fx_t, loop_t;                        // effect / sound loop timers
	int dashing;                               // rush
	// received effects
	float hack_t, emp_t, shock_t, heat_t[2], mark_t[2], warn_t, laser_t, fire_t, hit_ago, camo_close;   // hit_ago: s since the last damage
	// bookkeeping
	float last_hp; int was_alive, warned;
	float bot_t, seen_t, nosee_t, strafe_t, otgt_t, oshot_t; int strafe, otgt;     // bot brain (otgt: deployable it shoots at)
	V3 aim_pt, aim_n; int aim_ok;              // placement preview
} SkillActor;

typedef struct {
	int type, team, owner, tier, alive, phase;    // phase 0 building / flying in, 1 working, 2 ending
	float t, life, hp, hpmax, energy;
	V3 pos, nrm, cur;                             // pos: base point (drone: hover point); nrm: wall normal / ground normal; cur: drone position
	float yaw0, yaw, pitch, w, h, r;
	int target; float scan_t, fire_t, spin_t, gren_t, fx_t, dmg_snd_t, loop_t, end_t, hit_t;
	V3 from;                                      // drone: where it flies in from
	unsigned marked; signed char side[SK_MAXACT]; float lit[3];
} SkObj;
typedef struct { int on, owner, team, kind, target; V3 p, v; float t, delay, trail_t; } SkMissile;   // kind 0 missile, 1 turret grenade

static SkWorld SKW_; static SkillActor SKA[SK_MAXACT]; static SkObj SKO[SK_MAXOBJ]; static SkMissile SKMS[SK_MAXMS];
static int sk_ok, sk_local = 0, sk_frame; static float sk_now;
static int sk_bots_use = 1;          // bots use their skills
static struct { float camo, glitch, glitch_len, hlines, shield_hit, flash, vig, vig_len, heat, laugh, warn_cd; float glitch_c[3], vig_c[3]; } SKV;
static int sk_stat_rays;             // map rays this frame (stats)

// ---- small maths
static float sk_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float sk_len(V3 a) { return sqrtf(vdot(a, a)); }
static float sk_hdist(V3 a, V3 b) { float dx = a.x - b.x, dy = a.y - b.y; return sqrtf(dx * dx + dy * dy); }
static V3 sk_norm(V3 a) { float l = sk_len(a); return l > 1e-6f ? vmul(a, 1 / l) : v3(0, 0, 1); }
static float sk_wrap(float a) { if (a > 1e4f || a < -1e4f) return 0; while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static unsigned sk_seed = 0x2545F491u;
static float sk_rnd(void) { sk_seed ^= sk_seed << 13; sk_seed ^= sk_seed >> 17; sk_seed ^= sk_seed << 5; return (sk_seed >> 8) * (1.0f / 16777216.0f); }
static void sk_mul4(float *o, const float *a, const float *b) {
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static void sk_qaxis(float *q, float ax, float ay, float az, float ang) { float s = sinf(ang * 0.5f); q[0] = ax * s; q[1] = ay * s; q[2] = az * s; q[3] = cosf(ang * 0.5f); }
static void sk_qmul(float *o, const float *a, const float *b) {
	float r[4] = { a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
		a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] }; memcpy(o, r, 16);
}
static float sk_ray_sphere(V3 o, V3 d, V3 c, float r) {   // first hit distance along unit d, -1 none (0 when o is inside)
	V3 m = vsub(o, c); float b = vdot(m, d), cc = vdot(m, m) - r * r; if (cc > 0 && b > 0) return -1;
	float disc = b * b - cc; if (disc < 0) return -1; float t = -b - sqrtf(disc); return t < 0 ? 0 : t;
}

// ---- world helpers
static int sk_n(void) { int n = SKW_.n_actors ? SKW_.n_actors() : 1; return n > SK_MAXACT ? SK_MAXACT : n < 0 ? 0 : n; }
static int sk_valid(int a) { return a >= 0 && a < SK_MAXACT && SKA[a].on; }
static int sk_alive(int a) { return sk_valid(a) && SKW_.alive && SKW_.alive(a); }
static V3 sk_pos(int a) { return SKW_.pos ? SKW_.pos(a) : v3(0, 0, 0); }
static V3 sk_chest(int a) { V3 p = sk_pos(a); p.z += 115; return p; }
static V3 sk_eye(int a) { if (SKW_.eye) return SKW_.eye(a); V3 p = sk_pos(a); p.z += P_EYE; return p; }
static V3 sk_dir(int a) { if (SKW_.aim_dir) return SKW_.aim_dir(a); float y = SKW_.yaw ? SKW_.yaw(a) : 0; return v3(cosf(y), sinf(y), 0); }
static float sk_yaw(int a) { return SKW_.yaw ? SKW_.yaw(a) : 0; }
static V3 sk_vel(int a) { return SKW_.vel ? SKW_.vel(a) : v3(0, 0, 0); }
static float sk_hp(int a) { return SKW_.hp ? SKW_.hp(a) : 100; }
static int sk_los(V3 a, V3 b) { sk_stat_rays++; return SKW_.los ? SKW_.los(a, b) : 1; }
static int sk_hostile(int a, int b) { return SKA[a].team != SKA[b].team; }
static int sk_obj_hostile(int o, int team) { return SKO[o].alive && SKO[o].team != team; }
static int sk_damage(int v, float dmg, int att, int kind) { return SKW_.damage && sk_alive(v) ? SKW_.damage(v, dmg, att, kind) : 0; }
static void sk_notify(int a, int what, const char *t, int pts) { if (SKW_.notify) SKW_.notify(a, what, t, pts); }
static int sk_is_active(int a, int skill) { return sk_valid(a) && SKA[a].skill == skill && SKA[a].state == SKS_ACTIVE; }

// ---- effects (ids by name: base effect ids may shift when build_fx.py grows)
enum { SKF_CAMO_ON, SKF_CAMO_OFF, SKF_CAMO_HIT, SKF_AL_FIRE, SKF_MS_TRAIL, SKF_MS_EXP, SKF_BUILD, SKF_UNBUILD, SKF_DESTROY, SKF_TU_LOCK,
	SKF_META_ON, SKF_META_TICK, SKF_RUSH, SKF_RUSH_HIT, SKF_SH_HIT, SKF_SH_BREAK, SKF_WALL_PASS, SKF_DL_HIT, SKF_DIVE_ON, SKF_DIVE_HIT,
	SKF_EMP_EXP, SKF_EMP_SHOCK, SKF_EMP_CHG, SKF_RE_PING, SKF_HS_FLASH, SKF_EXPLOSION, SKF_HIT_BLOOD, SKF_N };
static const char *const SKF_NAME[SKF_N] = { "SK_CAMO_ON", "SK_CAMO_OFF", "SK_CAMO_HIT", "SK_AL_FIRE", "SK_MS_TRAIL", "SK_MS_EXP", "SK_BUILD",
	"SK_UNBUILD", "SK_DESTROY", "SK_TU_LOCK", "SK_META_ON", "SK_META_TICK", "SK_RUSH", "SK_RUSH_HIT", "SK_SH_HIT", "SK_SH_BREAK", "SK_WALL_PASS",
	"SK_DL_HIT", "SK_DIVE_ON", "SK_DIVE_HIT", "SK_EMP_EXP", "SK_EMP_SHOCK", "SK_EMP_CHG", "SK_RE_PING", "SK_HS_FLASH", "EXPLOSION", "HIT_BLOOD" };
static int sk_fxid[SKF_N];
// atlas cells (SK_CELLS order of build_skills_fx.py), reached through the zero-count marker emitters of effects C_DIR0..3
enum { SKC_EXPL, SKC_SMOKE, SKC_FLARE, SKC_LENZ, SKC_RING, SKC_DRING, SKC_DISK, SKC_UPLINE, SKC_META, SKC_ELE, SKC_SHARD, SKC_BEAM, SKC_BOX,
	SKC_MOSAIC, SKC_SCAN, SKC_KAL, SKC_GLOW, SKC_SPRINT, SKC_SECTOR, SKC_PILLAR, SKC_HEAT, SKC_HEX, SKC_SPARK, SKC_N };
static int sk_cell[SKC_N];
static int sk_fx_find(const char *n) { for (int i = 0; i < fx_neff; i++) if (!strncmp(fx_eff[i].name, n, 16)) return i; return -1; }
static void sk_fx_resolve(void) {
	for (int k = 0; k < SKF_N; k++) sk_fxid[k] = fx_ok ? sk_fx_find(SKF_NAME[k]) : -1;
	if (sk_fxid[SKF_EXPLOSION] < 0 && fx_ok) sk_fxid[SKF_EXPLOSION] = sk_fx_find("GRENADE_EXP");   // the grenade explosion (build_fx.py), else the missile blast
	if (sk_fxid[SKF_EXPLOSION] < 0) sk_fxid[SKF_EXPLOSION] = sk_fxid[SKF_MS_EXP];
	for (int k = 0; k < SKC_N; k++) {
		char n[16]; snprintf(n, sizeof(n), "C_DIR%d", k / 6); int e = fx_ok ? sk_fx_find(n) : -1; sk_cell[k] = -1;
		if (e >= 0 && k % 6 < fx_eff[e].n) { int em = fx_eff[e].em[k % 6]; if (em >= 0 && em < fx_nem && fx_em[em].cell >= 0 && fx_em[em].cell < fx_ncell) sk_cell[k] = fx_em[em].cell; }
	}
}
static void sk_fx(int k, V3 p, V3 n, const V3 *d) { if (sk_fxid[k] >= 0) fx_spawn(sk_fxid[k], &p.x, &n.x, d ? &d->x : NULL); }
static void sk_fx_up(int k, V3 p) { sk_fx(k, p, v3(0, 0, 1), NULL); }

// ---- sounds: FMOD events (audio.h tables) where the game has them, RAM sounds by name otherwise
static void sk_sfx(const char *pfx, V3 p, int vol) {   // a RAM sound at a world point: distance volume, pan from the listener
	float dx = p.x - snd_lis[0], dy = p.y - snd_lis[1], dz = p.z - snd_lis[2], d = sqrtf(dx * dx + dy * dy + dz * dz); const float maxd = 4500;
	if (d > maxd) return; float att = 1 - d / maxd; att = 0.25f + 0.75f * att * att;
	float pan = (dx * sinf(snd_lis_yaw) - dy * cosf(snd_lis_yaw)) / (d + 1);
	play_pan(pfx, (int)(vol * att), pan * 0.85f);
}
static void sk_ev(int a, const char *name) { if (a == sk_local) ev_play(name, NULL); else { V3 p = sk_pos(a); p.z += 100; ev_play(name, &p.x); } }
static void sk_ev_at(const char *name, V3 p) { ev_play(name, &p.x); }
static void sk_voice_use(int a) {   // "using <skill>" line: the operator's own (action) voice
	char w[48]; snprintf(w, sizeof(w), "using_%s", SK_DEF[SKA[a].skill].id);
	if (a == sk_local) snd_voice(SK_STEM[SKA[a].skill], w, NULL); else { V3 p = sk_pos(a); p.z += 150; snd_voice(SK_STEM[SKA[a].skill], w, &p.x); }
}
static const char *const SK_ALARM[SK_COUNT] = { "armlauncher", "cloaking", "heat_scan", "mini_tachikoma", "turret", "metabolism", "rush", "shield", "emp", "screen", "cyberdive", "dmd" };
static void sk_warn(int skill) {   // an enemy skill concerns the local player: intel_skill/<x>_alarm (is_use_alarm) + d_op_enemy_<skill>, at most one per 6 s
	if (SKV.warn_cd > 0) return; SKV.warn_cd = 6;
	char n[64]; snprintf(n, sizeof(n), "intel_skill/%s_alarm", SK_ALARM[skill]); ev_play(n, NULL);
	snprintf(n, sizeof(n), "voice/donut/common/op_%s_enemy", SK_DEF[skill].id); radio_play(n);
}

// ---- models
enum { SKM_TURRET, SKM_DRONE, SKM_EMP, SKM_MINI, SKM_WALLPOD, SKM_WALL, SKM_SHIELD, SKM_DOME, SKM_MISSILE, SKM_GHOST, SKM_N };
static const char *const SKM_FILE[SKM_N] = { "sk_turret", "sk_drone", "sk_emp", "sk_minitachi", "sk_wallpod", "sk_wall", "sk_shield", "sk_dome", "sk_missile", "sk_heatghost" };
static Model sk_mdl[SKM_N]; static SkinCache sk_sc[SKM_N]; static int sk_mok[SKM_N], sk_emp_spin = -1;
static MBone sk_ident[8];
static GLuint sk_ovl_tex;
// screen overlay cells of sk_ovl.gtx (tools/skills/out/sk_ovl.txt): u0 v0 u1 v1, blend 1 add / 2 mul2x
enum { SKV_VIG, SKV_CAMO, SKV_META, SKV_SHIELD, SKV_RUSH, SKV_DIVE, SKV_GLITCH, SKV_HLINES, SKV_DIVE_MUL, SKV_HEAT_MUL, SKV_LAUGH, SKV_SHIELD_HIT, SKV_N };
static const float SKV_UV[SKV_N][5] = {
	{ 0.000488f, 0.000488f, 0.499512f, 0.249512f, 1 }, { 0.500488f, 0.000488f, 0.999512f, 0.249512f, 1 }, { 0.000488f, 0.250488f, 0.499512f, 0.499512f, 1 },
	{ 0.500488f, 0.250488f, 0.999512f, 0.499512f, 1 }, { 0.000488f, 0.500488f, 0.499512f, 0.749512f, 1 }, { 0.500488f, 0.500488f, 0.999512f, 0.749512f, 1 },
	{ 0.000488f, 0.750488f, 0.249512f, 0.874512f, 1 }, { 0.250488f, 0.750488f, 0.499512f, 0.874512f, 1 }, { 0.500488f, 0.750488f, 0.749512f, 0.874512f, 2 },
	{ 0.750488f, 0.750488f, 0.999512f, 0.874512f, 2 }, { 0.000488f, 0.875488f, 0.124512f, 0.999512f, 1 }, { 0.250488f, 0.875488f, 0.499512f, 0.999512f, 1 } };
static GLuint sk_tex_load(const char *path) {   // GTX1: 'GTX1' u16 w, u16 h, u8 fmt (1 = DXT1), u8 0, u16 0, u32 size, data (as menu.h)
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	unsigned char h[16]; if (fread(h, 1, 16, f) != 16 || memcmp(h, "GTX1", 4)) { fclose(f); return 0; }
	int w = h[4] | h[5] << 8, hh = h[6] | h[7] << 8, fmt = h[8]; unsigned size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
	if (size > (16u << 20)) { fclose(f); return 0; }
	void *buf = malloc(size); if (!buf || fread(buf, 1, size, f) != size) { free(buf); fclose(f); return 0; }
	fclose(f); GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	if (fmt == 1) glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, hh, 0, size, buf);
	else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
#ifdef __vita__
	sceGxmTransferFinish();
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free(buf); return t;
}

// ---- init / reset
// vdir: data folder; w: world access (copied); models: 1 when the model shader is up. Call after fx_init (fx_sk.gfx) and hud_load.
// Returns the number of parts loaded (models + overlay texture), < 0 when the world access is missing.
static int skills_init(const char *vdir, const SkWorld *w, int models) {
	if (!w || !w->pos || !w->alive) return -1;
	SKW_ = *w; memset(SKA, 0, sizeof(SKA)); memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS)); memset(&SKV, 0, sizeof(SKV));
	for (int k = 0; k < 8; k++) { memset(&sk_ident[k], 0, sizeof(MBone)); sk_ident[k].q[3] = 1; }
	sk_fx_resolve();
	int n = 0;
	for (int i = 0; i < SKM_N && models; i++) {
		if (sk_mok[i]) { n++; continue; }
		char p[192]; snprintf(p, sizeof(p), "%s%s.gvm", vdir, SKM_FILE[i]);
		model_keep_cpu = 1; int r = model_load(&sk_mdl[i], p); model_keep_cpu = 0; if (r < 0) continue;
		model_prepare_rigid(&sk_mdl[i]);
		for (int k = 0; k < sk_mdl[i].nmesh; k++) { MMesh *m = &sk_mdl[i].mesh[k]; if (m->rigid >= 0 && m->cpu) { free(m->cpu); m->cpu = NULL; } }   // rigid: the CPU copy is not needed
		skincache_init(&sk_sc[i], &sk_mdl[i]); sk_mok[i] = 1; n++;
	}
	if (sk_mok[SKM_EMP]) sk_emp_spin = model_find_clip(&sk_mdl[SKM_EMP], "spin");
	if (!sk_ovl_tex) { char p[192]; snprintf(p, sizeof(p), "%ssk_ovl.gtx", vdir); sk_ovl_tex = sk_tex_load(p); }
	if (sk_ovl_tex) n++;
	sk_ok = 1; return n;
}
static void sk_obj_free(int o) { if (o >= 0 && o < SK_MAXOBJ) { SKO[o].alive = 0; for (int a = 0; a < SK_MAXACT; a++) if (SKA[a].obj == o) SKA[a].obj = -1; } }
static void sk_actor_clear_fx(SkillActor *A) {
	A->hack_t = A->emp_t = A->shock_t = A->warn_t = A->laser_t = A->camo_close = 0; A->hit_ago = 99; A->heat_t[0] = A->heat_t[1] = A->mark_t[0] = A->mark_t[1] = 0;
	A->nlocks = 0; A->toggled = 0; A->linked = 0; A->otgt = -1; A->fire_t = 9; A->aim_ok = 0;
}
// a new match: every skill ready (footage: ready at t = 0), nothing deployed
static void skills_reset_match(void) {
	memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS)); memset(&SKV, 0, sizeof(SKV));
	for (int a = 0; a < SK_MAXACT; a++) { SkillActor *A = &SKA[a]; A->state = SKS_READY; A->cool = 0; A->t = 0; A->obj = -1; A->bar = -1; sk_actor_clear_fx(A); A->bot_t = 1 + a * 0.37f; A->last_hp = -1; A->was_alive = 0; }
	fade_others("cloaking_loop", -1, 100); fade_others("is_noise_scan_loop", -1, 100); fade_others("is_metabolism_loop", -1, 100);
}
// Demolition round reset: deployables and active skills end, cooldowns keep running
static void sk_round_reset(void) {
	memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS));
	for (int a = 0; a < SK_MAXACT; a++) { SkillActor *A = &SKA[a]; if (A->state == SKS_ACTIVE || A->state == SKS_AIM) A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->obj = -1; sk_actor_clear_fx(A); }
	SKV.camo = SKV.glitch = SKV.hlines = SKV.laugh = SKV.vig = SKV.heat = 0;
}
// at spawn / operator change: actor a uses skill (operator index), team 0/1
static void sk_actor_set(int a, int skill, int team) {
	if (a < 0 || a >= SK_MAXACT) return; SkillActor *A = &SKA[a];
	if (A->on && A->skill == skill && A->team == team) return;
	for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].owner == a) sk_obj_free(o);
	for (int m = 0; m < SK_MAXMS; m++) if (SKMS[m].owner == a) SKMS[m].on = 0;
	memset(A, 0, sizeof(*A)); A->on = skill >= 0 && skill < SK_COUNT; A->skill = A->on ? skill : 0; A->team = team; A->tier = 1; A->state = SKS_READY; A->obj = -1; A->bar = -1;
	sk_actor_clear_fx(A); A->last_hp = -1; A->bot_t = 1.5f + a * 0.4f;
}
static void sk_actor_off(int a) { if (a >= 0 && a < SK_MAXACT && SKA[a].on) { sk_actor_set(a, -1, 0); SKA[a].on = 0; } }
// tier = 1 + (intel >= 400) + (intel >= 800) (TDM HUD: TIER 1 from 0 to 399)
static void sk_set_tier(int a, int intel) {
	if (!sk_valid(a)) return; int t = 1 + (intel >= 400) + (intel >= 800);
	if (a == sk_local && t > SKA[a].tier && sk_ok) ev_play(t == 2 ? "intel_skill/alarm_tier1" : "intel_skill/alarm_tier2", NULL);   // tier up (is_use_alarm5 / 4)
	SKA[a].tier = t;
}
static int sk_ti(int a) { int t = SKA[a].tier - 1; return t < 0 ? 0 : t > 2 ? 2 : t; }
static float sk_p1(int a) { return SK_DEF[SKA[a].skill].p1[sk_ti(a)]; }
static float sk_p2(int a) { return SK_DEF[SKA[a].skill].p2[sk_ti(a)]; }

// ---- state machine
static const char *sk_state_name(int a) {
	static const char *S[4] = { "ready", "aim", "active", "cooldown" };
	if (!sk_valid(a)) return "none"; static char b[64]; snprintf(b, sizeof(b), "%s %s (tier %d)", SK_DEF[SKA[a].skill].name, S[SKA[a].state & 3], SKA[a].tier); return b;
}
static void sk_to_cooldown(int a) { SkillActor *A = &SKA[a]; A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; A->nlocks = 0; }
static void sk_start_cool(int a) { SKA[a].cool = SK_DEF[SKA[a].skill].cooldown[sk_ti(a)]; SKA[a].warned = 0; }
static void sk_meta_release(int a) {   // metaboost over: overheal back to max
	SkillActor *A = &SKA[a];
	for (int b = 0; b < SK_MAXACT; b++) if ((A->linked >> b & 1) && sk_valid(b) && SKW_.heal) SKW_.heal(b, 0, 1.0f);
	A->linked = 0;
}
// ends a personal skill (camo, scan, metaboost, rush, shield, dive) early or on time
static void sk_end_active(int a, int broke) {
	SkillActor *A = &SKA[a]; if (A->state != SKS_ACTIVE) return;
	V3 p = sk_pos(a);
	switch (A->skill) {
	case SK_OPTICAL: sk_ev(a, "intel_skill/cloaking_end"); sk_fx_up(SKF_CAMO_OFF, p); if (a == sk_local) fade_others("cloaking_loop", -1, 250); break;
	case SK_METABOOST: sk_meta_release(a); sk_ev(a, "intel_skill/metabolism_end"); if (a == sk_local) fade_others("is_metabolism_loop", -1, 300); break;
	case SK_RUSH: sk_sfx("foley_co_rush_stop_001", p, 200); break;
	case SK_PLASMASHIELD: {
		V3 c = vadd(p, v3(cosf(sk_yaw(a)) * 60, sinf(sk_yaw(a)) * 60, 110)); if (A->toggled && sk_ti(a) == 2) c = vadd(A->dome, v3(0, 0, 100));
		if (broke) { sk_fx_up(SKF_SH_BREAK, c); sk_ev_at("intel_skill/shield_exp", c); } else if (A->toggled) sk_ev(a, sk_ti(a) == 2 ? "intel_skill/shield_dome_end" : "intel_skill/shield_end");
		A->toggled = 0; } break;
	case SK_CYBERDIVE: if (a == sk_local) { ev_play("ui/ghost_dive_end", NULL); SKV.flash = 0.2f; } sk_ev(a, "intel_skill/cyberdive_end_tp"); break;
	default: break;
	}
	sk_to_cooldown(a);
}
static void sk_cancel(int a) {   // EMP shock / death: AIM is dropped, active skills end
	SkillActor *A = &SKA[a];
	if (A->state == SKS_AIM) { A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->nlocks = 0; if (A->skill == SK_ARMLAUNCHER) sk_ev(a, "intel_skill/armlauncher_off"); }
	else if (A->state == SKS_ACTIVE) sk_end_active(a, 0);
}

// ---- placement (sentry, wall, EMP, recon, drone target): preview point on the floor in front of the eye
static int sk_place_point(int a, V3 *out, V3 *nrm) {
	const SkillDef *D = &SK_DEF[SKA[a].skill]; V3 e = sk_eye(a), d = sk_dir(a), p;
	float maxr = D->range > 0 ? D->range : 700;
	float h = raycast(e, d, maxr, NULL, NULL); sk_stat_rays++;
	if (D->placed == 2) p = h > 0 ? vadd(e, vmul(d, h - 10)) : vadd(e, vmul(d, maxr));            // drone: where the view ray ends
	else { V3 dh = sk_norm(v3(d.x, d.y, 0)); float far_ = h > 0 ? h - 30 : fminf(maxr, 300);
		p = vadd(e, vmul(d, far_)); if (sk_hdist(p, e) < 120) p = vadd(e, vmul(dh, 120)); }
	if (D->placed != 2) { V3 f = sk_pos(a); float hd = sk_hdist(p, f); if (hd > maxr) { V3 dd = sk_norm(v3(p.x - f.x, p.y - f.y, 0)); p = v3(f.x + dd.x * maxr, f.y + dd.y * maxr, p.z); } }
	V3 n; float dn = raycast(v3(p.x, p.y, p.z + 60), v3(0, 0, -1), D->placed == 2 ? 2500 : 450, NULL, &n); sk_stat_rays++;
	if (dn < 0) { *out = p; *nrm = v3(0, 0, 1); return 0; }
	p.z = p.z + 60 - dn; *out = p; *nrm = n;
	if (n.z < 0.7f) return 0;
	float fr = SKA[a].skill == SK_SENTRYGUN ? 70 : SKA[a].skill == SK_CYBERWALL ? 40 : 30;   // footprint clear (4 short rays at knee height)
	if (D->placed == 1) for (int k = 0; k < 4; k++) { float an = k * 1.5707963f + 0.4f; if (raycast(v3(p.x, p.y, p.z + 40), v3(cosf(an), sinf(an), 0), fr, NULL, NULL) >= 0) return 0; }
	if (D->placed == 1) sk_stat_rays += 4;
	return 1;
}
static void sk_obj_end(int o, int destroyed, int attacker);
static int sk_obj_new(int type, int owner) {
	for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].owner == owner && SKO[o].type == type) { sk_obj_end(o, 0, -1); break; }   // one per operator: the old one folds up
	int s = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (!SKO[o].alive) { s = o; break; }
	if (s < 0) { float best = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].t > best) { best = SKO[o].t; s = o; } }   // full: the oldest goes
	SkObj *O = &SKO[s]; memset(O, 0, sizeof(*O)); O->type = type; O->owner = owner; O->team = SKA[owner].team; O->tier = SKA[owner].tier; O->alive = 1; O->target = -1;
	for (int k = 0; k < SK_MAXACT; k++) O->side[k] = 0;
	O->scan_t = 0.05f * (s % 4); O->lit[0] = O->lit[1] = O->lit[2] = 1;
	return s;
}
static void sk_place(int a, V3 p, V3 n) {   // confirm: the deployable appears, the cooldown starts
	SkillActor *A = &SKA[a]; int ti = sk_ti(a); float yaw = sk_yaw(a); V3 f = sk_pos(a);
	int type = A->skill == SK_SENTRYGUN ? SKO_TURRET : A->skill == SK_CYBERWALL ? SKO_WALL : A->skill == SK_DMDDRONE ? SKO_DRONE : A->skill == SK_EMP ? SKO_EMP : SKO_RECON;
	int o = sk_obj_new(type, a); SkObj *O = &SKO[o]; O->pos = p; O->nrm = n; O->yaw0 = O->yaw = yaw; light_at(p.x, p.y, p.z + 20, O->lit);
	A->obj = o; sk_start_cool(a); A->state = SKS_COOLDOWN; A->t = 0; sk_voice_use(a);
	switch (type) {
	case SKO_TURRET: O->life = SK_DEF[A->skill].duration[ti]; O->hp = O->hpmax = SK_DEF[A->skill].p2[ti]; sk_ev_at("intel_skill/turret_hacking_deploy", p); break;
	case SKO_WALL: { V3 tn = sk_norm(v3(f.x - p.x, f.y - p.y, 0)); if (sk_len(v3(f.x - p.x, f.y - p.y, 0)) < 1) tn = v3(-cosf(yaw), -sinf(yaw), 0);
		O->nrm = tn; O->w = SK_DEF[A->skill].p1[ti]; O->h = SK_DEF[A->skill].p2[ti]; O->life = SK_DEF[A->skill].duration[ti]; O->hp = O->hpmax = 600;
		for (int k = 0; k < SK_MAXACT; k++) O->side[k] = 0;
		sk_ev_at("intel_skill/screen_start", p); sk_sfx("is_screen_set1", p, 200); } break;
	case SKO_DRONE: O->life = 120; O->hp = O->hpmax = SK_DEF[A->skill].p2[ti]; O->energy = 1; O->cur = O->from = v3(f.x, f.y, f.z + 1500); O->pos = v3(p.x, p.y, p.z + 350);
		sk_ev_at("intel_skill/dmd_spawn", O->cur); sk_ev_at("intel_skill/dmd_start", O->pos); break;
	case SKO_EMP: O->r = SK_DEF[A->skill].p1[ti]; O->life = 3.0f; O->hp = O->hpmax = 60; sk_ev_at(ti == 0 ? "intel_skill/emp_set_tier1" : "intel_skill/emp_set", p); sk_sfx("is_emp_beep", p, 220); break;
	case SKO_RECON: O->r = SK_DEF[A->skill].p1[ti]; O->life = SK_DEF[A->skill].duration[ti] + 1; O->hp = O->hpmax = SK_DEF[A->skill].p2[ti];
		sk_ev_at("intel_skill/mini_tachikoma_spawn", p); ev_play_later("intel_skill/mini_tachikoma_stand_up", &p.x, 0.6f); break;
	}
	sk_fx_up(SKF_BUILD, p);
}

// ---- personal skills: activation
static int sk_activate(int a) {
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a); int ti = sk_ti(a);
	A->state = SKS_ACTIVE; A->t = 0; A->fx_t = 0; A->loop_t = 0; A->energy = 1; sk_start_cool(a); sk_voice_use(a);
	switch (A->skill) {
	case SK_OPTICAL: sk_ev(a, "intel_skill/cloaking_start"); sk_fx_up(SKF_CAMO_ON, p); A->camo_close = 0; break;
	case SK_HEATSCAN: sk_ev(a, "intel_skill/heat_scan_start"); sk_fx_up(SKF_HS_FLASH, p); A->pulses = 0; A->pulse_t = 9;
		if (a == sk_local) { SKV.glitch = SKV.glitch_len = 0.3f; SKV.glitch_c[0] = 1; SKV.glitch_c[1] = 0.45f; SKV.glitch_c[2] = 0.25f; } break;
	case SK_METABOOST: {
		A->linked = 1u << a; A->link_t = 1; A->tick_t = 0; sk_ev(a, "intel_skill/metabolism_start"); sk_fx_up(SKF_META_ON, p);
		for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && !sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < 1000) {
			A->linked |= 1u << b; sk_fx_up(SKF_META_ON, sk_pos(b)); if (a == sk_local || b == sk_local) ev_play("intel_skill/metabolism_link", NULL); }
		if (A->linked >> sk_local & 1) play("is_metabolism_loop", 200); } break;
	case SK_RUSH: sk_sfx("foley_co_dash_01", p, 230); A->dashing = 0; break;
	case SK_PLASMASHIELD: A->toggled = 1; A->dome = p; sk_ev(a, ti == 2 ? "intel_skill/shield_dome_start" : "intel_skill/shield_start"); break;
	case SK_CYBERDIVE: A->cam = sk_eye(a); A->dive_still = 0; sk_fx_up(SKF_DIVE_ON, p);
		if (a == sk_local) { ev_play("intel_skill/cyberdive_start_fp", NULL); ev_play("ui/ghost_dive_in", NULL); SKV.flash = 0.25f; } else sk_ev(a, "intel_skill/cyberdive_start_tp"); break;
	default: break;
	}
	return 1;
}
// arm launcher: one missile per lock (none: one straight ahead), 0.08 s apart
static void sk_launcher_fire(int a) {
	SkillActor *A = &SKA[a]; V3 e = sk_eye(a), d = sk_dir(a), r = sk_norm(v3(d.y, -d.x, 0));
	V3 p0 = vadd(vadd(e, vmul(d, 45)), vadd(vmul(r, 22), v3(0, 0, -22)));
	int n = A->nlocks > 0 ? A->nlocks : 1;
	sk_ev(a, "intel_skill/armlauncher_fire"); sk_fx(SKF_AL_FIRE, p0, d, &d);
	for (int k = 0; k < n; k++) {
		int s = -1; for (int m = 0; m < SK_MAXMS; m++) if (!SKMS[m].on) { s = m; break; } if (s < 0) break;
		SkMissile *M = &SKMS[s]; memset(M, 0, sizeof(*M)); M->on = 1; M->owner = a; M->team = A->team; M->kind = 0; M->target = A->nlocks > 0 ? A->locks[k] : -1;
		V3 sp = v3((sk_rnd() - 0.5f) * 0.25f, (sk_rnd() - 0.5f) * 0.25f, 0.12f + 0.1f * k);   // fan out, then home in
		M->p = p0; M->v = vmul(sk_norm(vadd(d, sp)), 2500); M->delay = 0.08f * k + 1e-4f;
	}
	if (a == sk_local || A->nlocks) ev_play_later("intel_skill/armlauncher_off", a == sk_local ? NULL : &p0.x, 0.4f);
	sk_start_cool(a); A->state = SKS_COOLDOWN; A->t = 0; A->nlocks = 0;
}

// skill button. Returns 1 when something happened.
static int sk_press(int a) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a];
	if (A->hack_t > 0 || A->emp_t > 0) return 0;
	switch (A->state) {
	case SKS_READY:
		if (SK_DEF[A->skill].placed || A->skill == SK_ARMLAUNCHER) {
			A->state = SKS_AIM; A->t = 0; A->nlocks = 0; A->lock_t = 0.35f; A->aim_ok = 0;
			if (A->skill == SK_ARMLAUNCHER) { sk_ev(a, "intel_skill/armlauncher_on"); if (a == sk_local) ev_play("intel_skill/armlauncher_on_ring", NULL); }
			else if (a == sk_local) ev_play(A->skill == SK_SENTRYGUN ? "intel_skill/turret_hacking_ui_on" : "intel_skill/mini_tachikoma_ui_on", NULL);
			return 1; }
		return sk_activate(a);
	case SKS_AIM:
		if (A->skill == SK_ARMLAUNCHER && A->nlocks > 0) { sk_launcher_fire(a); return 1; }
		A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->nlocks = 0;   // cancel: no cooldown
		if (a == sk_local) ev_play(A->skill == SK_ARMLAUNCHER ? "intel_skill/armlauncher_off" : "intel_skill/mini_tachikoma_ui_off", NULL);
		return 1;
	case SKS_ACTIVE:
		if (A->skill == SK_PLASMASHIELD) {   // toggle (energy is kept while off)
			A->toggled = !A->toggled; if (A->toggled) A->dome = sk_pos(a);
			sk_ev(a, A->toggled ? (sk_ti(a) == 2 ? "intel_skill/shield_dome_start" : "intel_skill/shield_start") : (sk_ti(a) == 2 ? "intel_skill/shield_dome_end" : "intel_skill/shield_end")); return 1; }
		if (A->skill == SK_RUSH || A->skill == SK_CYBERDIVE) { sk_end_active(a, 0); return 1; }
		return 0;
	default: return 0;
	}
}
static void sk_hack(int a, int b);
// fire pressed: 1 = the skill used it (place / launch / dive in)
static int sk_fire(int a) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a];
	if (A->state == SKS_AIM) {
		if (A->skill == SK_ARMLAUNCHER) { if (A->t >= 0.5f) sk_launcher_fire(a); return 1; }
		V3 p, n; if (!sk_place_point(a, &p, &n)) { if (a == sk_local) ev_play("intel_skill/fake_set_alert", NULL); return 1; }
		sk_place(a, p, n); return 1;
	}
	if (A->state == SKS_ACTIVE && A->skill == SK_CYBERDIVE) {   // dive into the enemy closest to the view centre within 400 cm, 15 deg
		V3 d = sk_dir(a); int best = -1; float bc = cosf(15 * 3.14159f / 180);
		for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && sk_hostile(a, b)) {
			V3 v = vsub(sk_chest(b), A->cam); float l = sk_len(v); if (l > SK_DEF[SK_CYBERDIVE].p2[0] || l < 1) continue;
			float c = vdot(v, d) / l; if (c > bc) { bc = c; best = b; } }
		if (best >= 0) sk_hack(a, best);
		return 1;
	}
	return 0;
}
static void sk_hack(int a, int b) {   // Reiko's dive lands in b: hacked (blind, no fire, no skills)
	SkillActor *A = &SKA[a], *B = &SKA[b];
	B->hack_t = sk_p1(a); sk_cancel(b);
	sk_fx_up(SKF_DIVE_HIT, sk_pos(b));
	if (a == sk_local) { ev_play("intel_skill/cyberdive_end_fp", NULL); SKV.flash = 0.25f; sk_notify(a, SKN_HIT, NULL, 0); }
	if (b == sk_local) { SKV.laugh = B->hack_t; SKV.glitch = SKV.glitch_len = 0.6f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; sk_warn(SK_CYBERDIVE); }
	(void)A; sk_end_active(a, 0);
}

// ---- hit tests: walls, shields, deployables (for bullets, bot shots, lasers)
static float sk_ray_wall(int o, V3 from, V3 d, float maxd) {
	const SkObj *O = &SKO[o]; if (O->phase == 0) return -1;
	float dn = vdot(d, O->nrm); if (fabsf(dn) < 1e-4f) return -1;
	float t = vdot(vsub(O->pos, from), O->nrm) / dn; if (t < 0 || t > maxd) return -1;
	V3 h = vadd(from, vmul(d, t)), rt = v3(-O->nrm.y, O->nrm.x, 0); float lat = vdot(vsub(h, O->pos), rt), up = h.z - O->pos.z;
	return fabsf(lat) <= O->w * 0.5f && up >= 0 && up <= O->h ? t : -1;
}
static void sk_shield_frame(int a, V3 *c, V3 *f, V3 *r, float *hw, float *hh) {   // panel OBB: centre 60 cm in front of the chest, facing the view yaw
	float y = sk_yaw(a); V3 p = sk_pos(a); *f = v3(cosf(y), sinf(y), 0); *r = v3(sinf(y), -cosf(y), 0);
	*c = vadd(p, v3(f->x * 60, f->y * 60, 110)); int ti = sk_ti(a); *hw = ti == 0 ? 42.5f : 45; *hh = ti == 0 ? 50 : 75;
}
static float sk_ray_shield(int a, V3 from, V3 d, float maxd) {
	SkillActor *A = &SKA[a]; if (!sk_is_active(a, SK_PLASMASHIELD) || !A->toggled || !sk_alive(a)) return -1;
	if (sk_ti(a) == 2) {   // dome: bullets from outside crossing the sphere
		V3 c = A->dome; if (sk_len(vsub(from, c)) < 290) return -1; float t = sk_ray_sphere(from, d, c, 290); return t >= 0 && t <= maxd ? t : -1; }
	V3 c, f, r; float hw, hh; sk_shield_frame(a, &c, &f, &r, &hw, &hh);
	const V3 ax[3] = { r, f, v3(0, 0, 1) }; const float he[3] = { hw, 10, hh }; float t0 = 0, t1 = maxd; V3 o = vsub(from, c);
	for (int k = 0; k < 3; k++) { float e = vdot(o, ax[k]), g = vdot(d, ax[k]);
		if (fabsf(g) < 1e-6f) { if (fabsf(e) > he[k]) return -1; continue; }
		float ta = (-he[k] - e) / g, tb = (he[k] - e) / g; if (ta > tb) { float s = ta; ta = tb; tb = s; }
		if (ta > t0) t0 = ta; if (tb < t1) t1 = tb; if (t0 > t1) return -1; }
	return t0;
}
static V3 sk_obj_center(int o) { const SkObj *O = &SKO[o]; switch (O->type) { case SKO_TURRET: return vadd(O->pos, v3(0, 0, 60)); case SKO_DRONE: return vadd(O->cur, v3(0, 0, 30));
	case SKO_RECON: return vadd(O->pos, v3(0, 0, 20)); case SKO_EMP: return vadd(O->pos, v3(0, 0, 30)); default: return vadd(O->pos, v3(0, 0, O->h * 0.5f)); } }
static float sk_obj_radius(int o) { int t = SKO[o].type; return t == SKO_TURRET || t == SKO_DRONE ? 45 : 30; }
static void sk_obj_damage(int o, float dmg, int att);
// nearest thing of a team other than `team` along the ray: kind 1 wall, 2 shield (idx = actor), 3 deployable sphere; -1 = nothing
static float sk_trace(V3 from, V3 d, float len, int team, int self, int *kind, int *idx) {
	float best = len; *kind = 0; *idx = -1;
	for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, team)) {
		float t = -1;
		if (SKO[o].type == SKO_WALL) t = sk_ray_wall(o, from, d, best);
		else if (SKO[o].type != SKO_EMP) t = sk_ray_sphere(from, d, sk_obj_center(o), sk_obj_radius(o));
		if (t >= 0 && t < best) { best = t; *kind = SKO[o].type == SKO_WALL ? 1 : 3; *idx = o; }
	}
	for (int a = 0; a < SK_MAXACT; a++) if (a != self && sk_valid(a) && SKA[a].team != team && SKA[a].skill == SK_PLASMASHIELD && SKA[a].state == SKS_ACTIVE) {
		float t = sk_ray_shield(a, from, d, best); if (t >= 0 && t < best) { best = t; *kind = 2; *idx = a; } }
	return *kind ? best : -1;
}
static void sk_shield_absorb(int a, V3 hp, V3 d, float dmg) {
	SkillActor *A = &SKA[a]; A->energy -= dmg / sk_p1(a);
	V3 n = vmul(d, -1); sk_fx(SKF_SH_HIT, hp, n, &d);
	if (A->fx_t <= 0) { sk_ev_at(sk_ti(a) == 2 ? "intel_skill/shield_dome_dmg_out" : "intel_skill/shield_dmg", hp); A->fx_t = 0.12f; }
	if (a == sk_local) SKV.shield_hit = 0.2f;
	if (A->energy <= 0) { A->energy = 0; sk_end_active(a, 1); }
}
// applies a hit found by sk_trace
static void sk_apply(int kind, int idx, V3 hp, V3 d, float dmg, int att) {
	if (kind == 1) { SkObj *O = &SKO[idx]; V3 n = vdot(d, O->nrm) < 0 ? O->nrm : vmul(O->nrm, -1); sk_fx(SKF_SH_HIT, hp, n, &d);
		if (O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/shield_dmg", hp); O->dmg_snd_t = 0.15f; } sk_obj_damage(idx, dmg, att); }
	else if (kind == 2) sk_shield_absorb(idx, hp, d, dmg);
	else if (kind == 3) { V3 n = vmul(d, -1); fx_impact(2, &hp.x, &n.x, &d.x); sk_obj_damage(idx, dmg, att); }
}
// the player's (or any actor's) hitscan shot: returns the distance where a shield / wall / deployable stopped it (and applies the
// damage, effects and hit marker), or len when nothing did. Call it with the distance to the first character / map hit.
static float sk_bullet(V3 from, V3 dir, float len, int shooter, float dmg) {
	if (!sk_ok || !sk_valid(shooter)) return len;
	int kind, idx; float t = sk_trace(from, dir, len, SKA[shooter].team, shooter, &kind, &idx); if (t < 0) return len;
	V3 hp = vadd(from, vmul(dir, t)); sk_apply(kind, idx, hp, dir, dmg, shooter); sk_notify(shooter, SKN_HIT, NULL, 0);
	return t;
}
// a shot from -> to (bots, turrets, lasers): 1 = stopped by a hostile shield / wall / deployable (damage applied, *end = impact)
static int sk_shot_block(int shooter, int team, V3 from, V3 to, float dmg, V3 *end) {
	V3 d = vsub(to, from); float L = sk_len(d); if (L < 1) return 0; d = vmul(d, 1 / L);
	int kind, idx; float t = sk_trace(from, d, L, team, shooter, &kind, &idx); if (t < 0) return 0;
	V3 hp = vadd(from, vmul(d, t)); sk_apply(kind, idx, hp, d, dmg, shooter); if (end) *end = hp; return 1;
}
static int sk_bullet_block(int shooter, V3 from, V3 to, V3 *end) { return sk_ok && sk_valid(shooter) ? sk_shot_block(shooter, SKA[shooter].team, from, to, 9, end) : 0; }
// explosions (grenades) vs deployables
static void sk_blast(V3 c, float r, float dmg, int attacker) {
	if (!sk_ok) return; int team = sk_valid(attacker) ? SKA[attacker].team : -1;
	for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].team != team) { float d = sk_len(vsub(sk_obj_center(o), c)); if (d < r) sk_obj_damage(o, dmg * (1 - 0.6f * d / r), attacker); }
}

// ---- deployables
static void sk_obj_end(int o, int destroyed, int attacker) {
	SkObj *O = &SKO[o]; if (!O->alive) return; V3 c = sk_obj_center(o);
	if (destroyed) {
		sk_fx_up(SKF_DESTROY, c);
		static const char *DS[6] = { "", "intel_skill/turret_hacking_destroyed", "intel_skill/screen_exp", "intel_skill/dmd_destroyed", "intel_skill/mini_tachikoma_exp", "intel_skill/mini_tachikoma_destroyed" };
		static const char *NT[6] = { "", "SENTRY DESTROYED", "BARRIER DESTROYED", "DRONE DESTROYED", "EMP DESTROYED", "RECON DESTROYED" };
		sk_ev_at(DS[O->type], c);
		if (attacker >= 0) sk_notify(attacker, SKN_KILL, NT[O->type], 50);
	} else sk_fx_up(SKF_UNBUILD, O->pos);
	if (O->type == SKO_RECON) { sk_ev_at("intel_skill/noise_scan_end", c); fade_others("is_noise_scan_loop", -1, 300); }
	if (O->type == SKO_WALL && !destroyed) sk_sfx("is_screen_off", c, 220);
	if (O->type == SKO_TURRET && !destroyed) sk_ev_at("intel_skill/turret_hacking_end", c);
	sk_obj_free(o);
}
static void sk_obj_damage(int o, float dmg, int att) {
	SkObj *O = &SKO[o]; if (!O->alive || dmg <= 0) return;
	if (att >= 0 && att < SK_MAXACT && SKA[att].team == O->team) return;
	O->hp -= dmg; O->hit_t = 0.3f;
	if (O->type == SKO_TURRET && O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/turret_hacking_dmg", sk_obj_center(o)); O->dmg_snd_t = 0.25f; }
	if (O->type == SKO_DRONE && O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/dmd_dmg", sk_obj_center(o)); O->dmg_snd_t = 0.25f; }
	if (O->hp <= 0) sk_obj_end(o, 1, att);
}
// what a deployable's sensor sees: camo hides beyond the near-reveal distance unless she just fired
static int sk_sensor_sees(V3 p, int b) {
	const SkillActor *B = &SKA[b];
	if (B->skill == SK_OPTICAL && B->state == SKS_ACTIVE && B->fire_t > 1.0f && sk_len(vsub(sk_pos(b), p)) > SK_DEF[SK_OPTICAL].p1[0]) return 0;
	return 1;
}
static V3 sk_target_pos(int t) { return t >= SK_TGT_OBJ ? sk_obj_center(t - SK_TGT_OBJ) : sk_chest(t); }
static int sk_target_ok(int t, int team) { return t >= SK_TGT_OBJ ? t - SK_TGT_OBJ < SK_MAXOBJ && sk_obj_hostile(t - SK_TGT_OBJ, team) : t >= 0 && sk_alive(t) && SKA[t].team != team; }
static void sk_hit_target(int t, float dmg, int att, int kind) { if (t >= SK_TGT_OBJ) sk_obj_damage(t - SK_TGT_OBJ, dmg, att); else if (t >= 0) sk_damage(t, dmg, att, kind); }
// nearest hostile target of a deployable: actors and other deployables (turret, drone, recon) in range / arc, in sight from eye
static int sk_obj_pick(int o, V3 eye, float range, float hrange, float arc_cos, V3 fwd) {
	SkObj *O = &SKO[o]; int cand[SK_MAXACT + SK_MAXOBJ]; float cd[SK_MAXACT + SK_MAXOBJ]; int nc = 0;
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team && sk_sensor_sees(eye, b)) {
		V3 v = vsub(sk_chest(b), eye); float l = hrange > 0 ? sk_hdist(sk_pos(b), O->pos) : sk_len(v); if (l > (hrange > 0 ? hrange : range)) continue;
		if (arc_cos > -1 && l > 1) { V3 hv = sk_norm(v3(v.x, v.y, 0)); if (vdot(hv, fwd) < arc_cos) continue; }
		cand[nc] = b; cd[nc++] = l; }
	for (int k = 0; k < SK_MAXOBJ; k++) if (k != o && sk_obj_hostile(k, O->team) && (SKO[k].type == SKO_TURRET || SKO[k].type == SKO_DRONE || SKO[k].type == SKO_RECON)) {
		V3 v = vsub(sk_obj_center(k), eye); float l = hrange > 0 ? sk_hdist(sk_obj_center(k), O->pos) : sk_len(v); if (l > (hrange > 0 ? hrange : range)) continue;
		if (arc_cos > -1 && l > 1) { V3 hv = sk_norm(v3(v.x, v.y, 0)); if (vdot(hv, fwd) < arc_cos) continue; }
		cand[nc] = SK_TGT_OBJ + k; cd[nc++] = l + 200; }   // operators first
	for (int tries = 0; tries < 3 && nc > 0; tries++) {   // nearest first, at most three sight rays
		int k = 0; for (int i = 1; i < nc; i++) if (cd[i] < cd[k]) k = i;
		int t = cand[k]; cand[k] = cand[nc - 1]; cd[k] = cd[nc - 1]; nc--;
		if (sk_los(eye, sk_target_pos(t))) return t;
	}
	return -1;
}
static V3 sk_turret_att(int o, const char *att) {   // a turret attachment point in the world (its yaw / pitch applied as drawn)
	SkObj *O = &SKO[o]; const float *A = sk_mok[SKM_TURRET] ? model_attach(&sk_mdl[SKM_TURRET], att) : NULL; float a3[3] = { 0, -40, 70 };
	if (A) memcpy(a3, A, 12);
	if (strcmp(att, "yaw")) {   // pitch about the pitch pivot (model x axis; the gun faces -y: nose up = rotate -pitch)
		const float *pv = sk_mok[SKM_TURRET] ? model_attach(&sk_mdl[SKM_TURRET], "pitch") : NULL; float c[3] = { 0, 1.5f, 53.9f }; if (pv) memcpy(c, pv, 12);
		float cp = cosf(-O->pitch), sp = sinf(-O->pitch), y = a3[1] - c[1], z = a3[2] - c[2]; a3[1] = c[1] + y * cp - z * sp; a3[2] = c[2] + y * sp + z * cp; }
	float th = O->yaw + 1.5707963f, c = cosf(th), s = sinf(th);   // model faces -y: world yaw y -> rotate by y + 90 deg
	return v3(O->pos.x + c * a3[0] - s * a3[1], O->pos.y + s * a3[0] + c * a3[1], O->pos.z + a3[2]);
}
static void sk_grenade_launch(int o, V3 from, V3 to) {   // T3 sentry: lobbed grenade
	int s = -1; for (int m = 0; m < SK_MAXMS; m++) if (!SKMS[m].on) { s = m; break; } if (s < 0) return;
	SkMissile *M = &SKMS[s]; memset(M, 0, sizeof(*M)); M->on = 1; M->owner = SKO[o].owner; M->team = SKO[o].team; M->kind = 1; M->target = -1; M->p = from;
	float hd = sk_hdist(to, from), T = fmaxf(0.3f, hd / 1000); V3 h = sk_norm(v3(to.x - from.x, to.y - from.y, 0));
	M->v = v3(h.x * hd / T, h.y * hd / T, (to.z - from.z + 0.5f * 980 * T * T) / T);
	sk_ev_at("intel_skill/turret_rocket_fire", from);
}
static void sk_update_turret(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.4f; sk_fx_up(SKF_BUILD, O->pos); }
		if (O->t >= 2) { O->phase = 1; sk_ev_at("intel_skill/turret_hacking_start", O->pos); } return; }
	float arc = SK_DEF[SK_SENTRYGUN].p1[O->tier - 1 < 0 ? 0 : O->tier - 1 > 2 ? 2 : O->tier - 1];
	V3 fwd = v3(cosf(O->yaw0), sinf(O->yaw0), 0), laser = sk_turret_att(o, "laser");
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.2f;   // 5 Hz target search
		int t = sk_obj_pick(o, laser, 2500, 0, arc >= 359 ? -2 : cosf(arc * 0.5f * 3.14159f / 180), fwd);
		if (t >= 0 && t != O->target) { O->spin_t = 0.4f; sk_ev_at("intel_skill/turret_hacking_alarm", O->pos); V3 tp = sk_target_pos(t); sk_fx(SKF_TU_LOCK, laser, sk_norm(vsub(tp, laser)), NULL);
			if (t == sk_local) sk_warn(SK_SENTRYGUN); }
		if (t < 0 && O->target >= 0) ev_play("intel_skill/turret_hacking_fire_end", &O->pos.x);
		O->target = t; }
	if (O->target >= 0 && !sk_target_ok(O->target, O->team)) O->target = -1;
	float want_y, want_p;
	if (O->target >= 0) { V3 v = vsub(sk_target_pos(O->target), laser); want_y = atan2f(v.y, v.x); want_p = atan2f(v.z, sqrtf(v.x * v.x + v.y * v.y)); }
	else { want_y = O->yaw0 + sinf(O->t * 0.8f) * (arc >= 359 ? 1.5f : fminf(0.785f, arc * 0.5f * 3.14159f / 180)); want_p = 0; }
	float mx = 240 * 3.14159f / 180 * dt, dy = sk_wrap(want_y - O->yaw); O->yaw += sk_clamp(dy, -mx, mx); O->pitch += sk_clamp(want_p - O->pitch, -mx, mx);
	if (O->target >= 0) {
		if (O->spin_t > 0) { O->spin_t -= dt; if (O->spin_t <= 0) O->loop_t = 0; }
		else if (fabsf(sk_wrap(want_y - O->yaw)) < 0.17f) {
			O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = 1.3f; sk_sfx("is_turret_hacking_fire_loop3", O->pos, 210); }
			O->fire_t -= dt;
			while (O->fire_t <= 0) { O->fire_t += 0.1f;   // 10 shots/s, 9 damage, spread 1.5 deg
				V3 mz = sk_turret_att(o, "muzzle"), tp = sk_target_pos(O->target), end = tp; float d = sk_len(vsub(tp, mz));
				float p = sk_clamp(1.1f - d / 2500, 0.3f, 0.9f); int moving = O->target < SK_TGT_OBJ && sk_hdist(sk_vel(O->target), v3(0, 0, 0)) > 300; if (moving) p *= 0.7f;
				if (sk_shot_block(O->owner, O->team, mz, tp, 9, &end)) {}
				else if (sk_rnd() < p) sk_hit_target(O->target, 9, O->owner, SKD_BULLET);
				else end = vadd(tp, v3((sk_rnd() - 0.5f) * 90, (sk_rnd() - 0.5f) * 90, (sk_rnd() - 0.3f) * 70));
				if (SKW_.shot_fx) SKW_.shot_fx(mz, end);
			}
			if (O->tier >= 3) { O->gren_t -= dt; if (O->gren_t <= 0) { O->gren_t = 6; V3 g = sk_turret_att(o, "gren"), tp = sk_target_pos(O->target);
				sk_grenade_launch(o, g, tp); sk_grenade_launch(o, g, vadd(tp, v3((sk_rnd() - 0.5f) * 150, (sk_rnd() - 0.5f) * 150, 0))); } }
		}
	} else O->fire_t = 0;
}
static void sk_update_drone(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { float k = sk_clamp(O->t / 1.5f, 0, 1); k = k * k * (3 - 2 * k);
		O->cur = vadd(O->from, vmul(vsub(O->pos, O->from), k)); if (O->t >= 1.5f) { O->phase = 1; O->loop_t = 0; } return; }
	if (O->phase == 2) { O->cur.z += 900 * dt; if (O->t - O->end_t > 1.0f) sk_obj_free(o); return; }
	O->cur = vadd(O->pos, v3(0, 0, sinf(O->t * 2.1f) * 15));
	int ti = O->tier - 1 < 0 ? 0 : O->tier - 1 > 2 ? 2 : O->tier - 1;
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.2f; int t = sk_obj_pick(o, vadd(O->cur, v3(0, 0, 10)), 0, 900, -2, v3(1, 0, 0));
		if (t >= 0 && t != O->target) { O->fx_t = 0; O->loop_t = 0; if (t == sk_local) sk_warn(SK_DMDDRONE); }
		if (t < 0 && O->target >= 0) fade_others("is_dmd_heat_loop_01", -1, 200);
		O->target = t; }
	if (O->target >= 0 && !sk_target_ok(O->target, O->team)) O->target = -1;
	O->loop_t -= dt;
	if (O->target >= 0) {
		V3 tp = sk_target_pos(O->target), v = vsub(tp, O->cur); O->yaw += sk_clamp(sk_wrap(atan2f(v.y, v.x) - O->yaw), -4 * dt, 4 * dt);
		O->energy -= 0.05f * dt;
		if (O->loop_t <= 0) { O->loop_t = 4.0f; sk_sfx("is_dmd_heat_loop_01", O->cur, 200); }
		O->fx_t -= dt;
		while (O->fx_t <= 0) { O->fx_t += 0.1f;   // the laser in 0.1 s ticks
			V3 from = vadd(O->cur, v3(0, 0, 11)), end = tp; float dmg = SK_DEF[SK_DMDDRONE].p1[ti] * 0.1f;
			if (!sk_shot_block(O->owner, O->team, from, tp, dmg, &end)) { sk_hit_target(O->target, dmg, O->owner, SKD_LASER);
				if (O->target < SK_TGT_OBJ) { SKA[O->target].laser_t = 0.3f; if (O->target == sk_local) { SKV.vig = SKV.vig_len = 0.3f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.1f; SKV.vig_c[2] = 0.05f; } } }
			V3 bd = sk_norm(vsub(end, from)), n = vmul(bd, -1); sk_fx(SKF_DL_HIT, end, n, &bd);
		}
	} else { O->energy -= 0.011f * dt; O->yaw += 0.4f * dt; if (O->loop_t <= 0) { O->loop_t = 2.4f; sk_sfx(sk_rnd() < 0.5f ? "is_dmd_loop_03" : "is_dmd_loop_04", O->cur, 150); } }
	if (O->energy <= 0) { O->energy = 0; O->phase = 2; O->end_t = O->t; O->target = -1; sk_ev_at("intel_skill/dmd_end", O->cur); fade_others("is_dmd_heat_loop_01", -1, 200); }
}
static void sk_update_emp(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) {
		O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.1f; sk_fx_up(SKF_EMP_CHG, vadd(O->pos, v3(0, 0, 45))); }
		if (O->t >= 2.0f) {   // the blast
			O->phase = 1; V3 c = vadd(O->pos, v3(0, 0, 60)); sk_fx_up(SKF_EMP_EXP, c); sk_ev_at("intel_skill/emp_exp", c);
			for (int k = 0; k < SK_MAXOBJ; k++) if (k != o && sk_obj_hostile(k, O->team) && sk_len(vsub(sk_obj_center(k), c)) < O->r) sk_obj_end(k, 1, O->owner);
			for (int m = 0; m < SK_MAXMS; m++) if (SKMS[m].on && SKMS[m].team != O->team && sk_len(vsub(SKMS[m].p, c)) < O->r) { sk_fx_up(SKF_DESTROY, SKMS[m].p); SKMS[m].on = 0; }
			for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team && sk_len(vsub(sk_chest(b), c)) < O->r && sk_los(c, sk_chest(b))) {
				SkillActor *B = &SKA[b]; sk_cancel(b); B->emp_t = SK_DEF[SK_EMP].p2[0]; B->shock_t = 0; if (B->state == SKS_COOLDOWN || B->cool > 0) B->cool += 5; else { B->cool = 5; B->state = SKS_COOLDOWN; }
				sk_damage(b, 10, O->owner, SKD_SHOCK);
				if (b == sk_local) { SKV.glitch = SKV.glitch_len = 0.8f; SKV.glitch_c[0] = 0.4f; SKV.glitch_c[1] = 0.6f; SKV.glitch_c[2] = 1; sk_warn(SK_EMP); } }
		}
	} else if (O->t >= 3.0f) sk_obj_end(o, 0, -1);
}
static void sk_update_wall(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.4f; V3 rt = v3(-O->nrm.y, O->nrm.x, 0); for (int k = 0; k < 5; k++) sk_fx_up(SKF_BUILD, vadd(O->pos, vmul(rt, (k - 2) * O->w * 0.22f))); }
		if (O->t >= 2) { O->phase = 1; sk_sfx("is_screen_set2", O->pos, 200); O->loop_t = 0.5f; } }
	else { O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = 5.1f; sk_sfx("is_screen_loop", vadd(O->pos, v3(0, 0, 100)), 120); } }
	V3 rt = v3(-O->nrm.y, O->nrm.x, 0);
	for (int b = 0; b < SK_MAXACT; b++) {   // an enemy walking through: hacked screen + revealed to the wall's team for 5 s
		if (!sk_alive(b)) { O->side[b] = 0; continue; }
		V3 p = vadd(sk_pos(b), v3(0, 0, 100)), q = vsub(p, O->pos); float sd = vdot(q, O->nrm); signed char s = sd > 0 ? 1 : -1;
		int inside = fabsf(vdot(q, rt)) <= O->w * 0.5f + 30 && q.z > -100 && q.z < O->h + 50;
		if (O->side[b] && s != O->side[b] && inside && O->phase == 1 && SKA[b].team != O->team) {
			SkillActor *B = &SKA[b]; B->mark_t[O->team] = 5; sk_fx_up(SKF_WALL_PASS, sk_pos(b)); sk_ev_at("intel_skill/screen_through", p);
			if (b == sk_local) { SKV.glitch = SKV.glitch_len = 0.6f; SKV.glitch_c[0] = 0.2f; SKV.glitch_c[1] = 0.8f; SKV.glitch_c[2] = 0.3f; SKV.hlines = 1.5f; sk_warn(SK_CYBERWALL); }
		}
		O->side[b] = s;
	}
}
static void sk_update_recon(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { if (O->t >= 1.0f) { O->phase = 1; sk_ev_at("intel_skill/noise_scan_start", O->pos); sk_sfx("is_noise_scan_loop", O->pos, 160); O->scan_t = 0; } return; }
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.5f; sk_fx_up(SKF_RE_PING, vadd(O->pos, v3(0, 0, 30)));   // noise scan: running or shooting enemies get marked
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team) {
			SkillActor *B = &SKA[b]; V3 v = sk_vel(b);
			if (sk_hdist(sk_pos(b), O->pos) > O->r || (sqrtf(v.x * v.x + v.y * v.y) <= 250 && B->fire_t > 0.5f)) continue;
			B->mark_t[O->team] = 3;
			if (!(O->marked >> b & 1)) { O->marked |= 1u << b;
				if (O->owner == sk_local) ev_play("intel_skill/noise_scan_triangle", NULL);
				B->warn_t = 3;
				if (b == sk_local) { voice_play("d_op_warn_specialrecon", 230, 0); SKV.vig = SKV.vig_len = 0.6f; SKV.vig_c[0] = 1; SKV.vig_c[1] = 0.15f; SKV.vig_c[2] = 0.1f; } }
		}
	}
}
static void sk_update_objs(float dt) {
	for (int o = 0; o < SK_MAXOBJ; o++) {
		SkObj *O = &SKO[o]; if (!O->alive) continue;
		if (!sk_valid(O->owner) || SKA[O->owner].team != O->team) { sk_obj_end(o, 0, -1); continue; }   // the owner left / changed sides
		O->t += dt; if (O->dmg_snd_t > 0) O->dmg_snd_t -= dt; if (O->hit_t > 0) O->hit_t -= dt;
		switch (O->type) {
		case SKO_TURRET: sk_update_turret(o, dt); break;
		case SKO_DRONE: sk_update_drone(o, dt); break;
		case SKO_EMP: sk_update_emp(o, dt); break;
		case SKO_WALL: sk_update_wall(o, dt); break;
		case SKO_RECON: sk_update_recon(o, dt); break;
		}
		if (!O->alive) continue;
		if (O->type != SKO_EMP && O->type != SKO_DRONE && O->t >= O->life) sk_obj_end(o, 0, -1);
		else if (O->type == SKO_DRONE && O->t >= O->life && O->phase != 2) { O->phase = 2; O->end_t = O->t; sk_ev_at("intel_skill/dmd_end", O->cur); }
	}
}

// ---- missiles (arm launcher) and turret grenades
static void sk_explode(int m, V3 p, V3 n) {
	SkMissile *M = &SKMS[m]; M->on = 0;
	float R = 300, dmax = M->kind ? 60 : 120, dmin = M->kind ? 10 : 30;
	sk_fx(M->kind ? SKF_EXPLOSION : SKF_MS_EXP, p, n, NULL); sk_ev_at(M->kind ? "intel_skill/turret_rocket_exp" : "intel_skill/armlauncher_rocket_exp", p);
	V3 c = vadd(p, vmul(n, 10));
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != M->team) {
		float d = sk_len(vsub(sk_chest(b), p)); if (d > R || !sk_los(c, sk_chest(b))) continue;
		sk_damage(b, dmax - (dmax - dmin) * d / R, M->owner, SKD_BLAST); }
	for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, M->team) && sk_len(vsub(sk_obj_center(o), p)) < R) sk_obj_damage(o, M->kind ? 100 : 200, M->owner);
}
static void sk_update_missiles(float dt) {
	for (int m = 0; m < SK_MAXMS; m++) {
		SkMissile *M = &SKMS[m]; if (!M->on) continue;
		if (M->delay > 0) { M->delay -= dt; if (M->delay <= 0) sk_ev_at("intel_skill/armlauncher_rocket_fire", M->p); continue; }
		M->t += dt;
		if (M->kind == 1) M->v.z -= 980 * dt;
		else if (M->target >= 0) {   // homing: turn up to 180 deg/s toward the target
			if (!sk_target_ok(M->target, M->team)) M->target = -1;
			else { V3 want = sk_norm(vsub(sk_target_pos(M->target), M->p)), cur = sk_norm(M->v); float c = sk_clamp(vdot(cur, want), -1, 1), ang = acosf(c), mx = 3.14159f * dt;
				V3 nd = ang <= mx || ang < 1e-4f ? want : sk_norm(vadd(vmul(cur, sinf(ang - mx)), vmul(want, sinf(mx)))); M->v = vmul(nd, 2500); }
		}
		float sp = sk_len(M->v); V3 d = vmul(M->v, 1 / (sp + 1e-6f)); float step = sp * dt;
		V3 n; float h = raycast(M->p, d, step + 5, NULL, &n); sk_stat_rays++;
		if (M->t > 3 || h >= 0) { V3 hp = h >= 0 ? vadd(M->p, vmul(d, fmaxf(0, h - 5))) : M->p; sk_explode(m, hp, h >= 0 ? n : v3(0, 0, 1)); continue; }
		V3 np = vadd(M->p, vmul(d, step)), ep = np; int hit = 0;
		for (int b = 0; b < SK_MAXACT && !hit; b++) if (sk_alive(b) && SKA[b].team != M->team) {   // contact: within 40 cm of the body (or the shield in front)
			V3 c = sk_chest(b), q = vsub(c, M->p); float t = sk_clamp(vdot(q, d), 0, step); V3 cl = vadd(M->p, vmul(d, t)); V3 e = vsub(c, cl);
			if (fabsf(e.z) < 80 && e.x * e.x + e.y * e.y < 40 * 40) hit = 1; }
		for (int o = 0; o < SK_MAXOBJ && !hit; o++) if (sk_obj_hostile(o, M->team)) { float t = sk_ray_sphere(M->p, d, sk_obj_center(o), sk_obj_radius(o) + 10); if (t >= 0 && t <= step) hit = 1; }
		{ int kind, idx; float t = sk_trace(M->p, d, step, M->team, M->owner, &kind, &idx); if (t >= 0 && kind != 3) { V3 hp = vadd(M->p, vmul(d, t)); if (kind == 2) sk_shield_absorb(idx, hp, d, 60); else sk_obj_damage(idx, 120, M->owner); ep = hp; hit = 1; } }
		if (hit) { sk_explode(m, ep, vmul(d, -1)); continue; }
		M->p = np;
		M->trail_t -= dt; if (M->trail_t <= 0 && M->kind == 0) { M->trail_t = 1 / 30.f; V3 nb = vmul(d, -1); sk_fx(SKF_MS_TRAIL, vadd(M->p, vmul(d, -10)), nb, NULL); }
	}
}

// ---- per-actor update
static void sk_tick_aim(int a, float dt) {
	SkillActor *A = &SKA[a];
	if (A->skill == SK_ARMLAUNCHER) {
		if (A->t > SK_DEF[SK_ARMLAUNCHER].duration[0]) { sk_start_cool(a); A->state = SKS_COOLDOWN; A->t = 0; A->nlocks = 0; sk_ev(a, "intel_skill/armlauncher_off"); return; }
		if (A->t < 0.5f) return;
		A->lock_t -= dt; int maxl = (int)sk_p1(a); if (maxl > 4) maxl = 4;
		for (int k = 0; k < A->nlocks; k++) if (!sk_target_ok(A->locks[k], A->team)) { A->locks[k] = A->locks[--A->nlocks]; k--; }
		if (A->lock_t > 0 || A->nlocks >= maxl) return;
		A->lock_t = 0.35f;   // a lock: the enemy nearest the view axis inside 12 deg, <= 40 m, in sight
		V3 e = sk_eye(a), d = sk_dir(a); int cand[SK_MAXACT + SK_MAXOBJ]; float ca[SK_MAXACT + SK_MAXOBJ]; int nc = 0; const float cmin = cosf(12 * 3.14159f / 180);
		for (int t = 0; t < SK_MAXACT + SK_MAXOBJ; t++) {
			int id = t < SK_MAXACT ? t : SK_TGT_OBJ + t - SK_MAXACT; if (!sk_target_ok(id, A->team)) continue;
			if (id < SK_MAXACT && !sk_sensor_sees(e, id)) continue;
			int dup = 0; for (int k = 0; k < A->nlocks; k++) if (A->locks[k] == id) dup = 1; if (dup) continue;
			V3 v = vsub(sk_target_pos(id), e); float l = sk_len(v); if (l > 4000 || l < 50) continue; float c = vdot(v, d) / l; if (c < cmin) continue;
			cand[nc] = id; ca[nc++] = c; }
		for (int tries = 0; tries < 2 && nc > 0; tries++) {
			int k = 0; for (int i = 1; i < nc; i++) if (ca[i] > ca[k]) k = i;
			int id = cand[k]; cand[k] = cand[nc - 1]; ca[k] = ca[nc - 1]; nc--;
			if (!sk_los(e, sk_target_pos(id))) continue;
			A->locks[A->nlocks++] = id;
			if (a == sk_local) ev_play("intel_skill/armlauncher_on_ring", NULL);
			if (id == sk_local) sk_warn(SK_ARMLAUNCHER);
			break;
		}
		return;
	}
	if (a == sk_local && ((sk_frame & 1) || A->t < 0.05f)) { V3 p, n; A->aim_ok = sk_place_point(a, &p, &n); A->aim_pt = p; A->aim_n = n; }   // the hologram follows the view (30 Hz: ~6 short rays)
	if (A->t > 15) { A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; }   // forgotten preview: back to ready
}
static void sk_tick_active(int a, float dt) {
	SkillActor *A = &SKA[a]; const SkillDef *D = &SK_DEF[A->skill]; int ti = sk_ti(a); V3 p = sk_pos(a);
	if (A->fx_t > 0) A->fx_t -= dt;
	switch (A->skill) {
	case SK_OPTICAL: {
		float dur = D->duration[ti]; A->bar = 1 - 0.7f * A->t / dur;   // footage: 100 % -> 30 % over the duration
		int near_ = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < D->p1[0]) near_ = 1;
		A->camo_close = near_ ? A->camo_close + dt : 0;
		if (a == sk_local) { A->loop_t -= dt; if (A->loop_t <= 0) { A->loop_t = 3.9f; play("cloaking_loop", 120); } }
		if (A->t >= dur) sk_end_active(a, 0);
	} break;
	case SK_HEATSCAN: {
		int np = (int)D->p1[ti]; A->pulse_t += dt;
		float next = 0.3f + A->pulses * 1.25f;
		if (A->pulses < np && A->t >= next) {   // a pulse: every enemy within 40 m shows through walls for 1.6 s (shared with the team)
			A->pulses++; A->pulse_t = 0; int found = 0;
			for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < D->p2[ti]) {
				SKA[b].heat_t[A->team & 1] = 1.6f; found = 1;
				if (b == sk_local) { sk_warn(SK_HEATSCAN); ev_play("intel_skill/heat_scan", NULL); } }
			if (a == sk_local) { ev_play("intel_skill/heat_scan_woosh", NULL); if (found) ev_play("intel_skill/heat_scan_detect", NULL); }
			if (a == sk_local) { SKV.heat = 0.4f; SKV.vig = SKV.vig_len = 0.4f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.35f; SKV.vig_c[2] = 0.15f; }
		}
		A->bar = (float)(np - A->pulses + (A->pulses < np ? 1 : 0)) / np; if (A->t > 0.3f + np * 1.25f) A->bar = 0;
		if (A->t >= 0.3f + np * 1.25f) sk_end_active(a, 0);
	} break;
	case SK_METABOOST: {
		const float heal_len = 7.5f, total = D->p2[ti], cap = D->p1[ti]; float t0 = A->t - dt, t1 = A->t; if (t0 < 0) t0 = 0;
		A->bar = 1 - A->t / D->duration[ti];
		if (t0 < heal_len) {
			if (t1 > heal_len) t1 = heal_len;
			float inc = total * ((t1 / heal_len) * (t1 / heal_len) - (t0 / heal_len) * (t0 / heal_len));   // accelerating heal
			A->link_t -= dt;
			if (A->link_t <= 0) { A->link_t = 1;   // re-link every second: new allies within 10 m, lose those beyond 12 m
				for (int b = 0; b < SK_MAXACT; b++) { if (b == a || !sk_valid(b) || sk_hostile(a, b)) continue; float d = sk_len(vsub(sk_pos(b), p));
					if (!sk_alive(b) || d > 1200) A->linked &= ~(1u << b); else if (d < 1000 && !(A->linked >> b & 1)) { A->linked |= 1u << b; sk_fx_up(SKF_META_ON, sk_pos(b)); if (b == sk_local) ev_play("intel_skill/metabolism_link", NULL); } } }
			A->tick_t -= dt; int tick = A->tick_t <= 0; if (tick) A->tick_t = 0.5f;
			for (int b = 0; b < SK_MAXACT; b++) if ((A->linked >> b & 1) && sk_alive(b)) { if (SKW_.heal) SKW_.heal(b, inc, cap); if (tick) sk_fx_up(SKF_META_TICK, sk_pos(b)); }
			if ((A->linked >> sk_local & 1) && sk_alive(sk_local)) { SKV.vig = SKV.vig_len = 0.15f; SKV.vig_c[0] = 0.1f; SKV.vig_c[1] = 0.5f; SKV.vig_c[2] = 0.6f; }
		}
		A->energy = A->t < heal_len ? 1 : 0;
		if (A->t >= D->duration[ti]) sk_end_active(a, 0);
	} break;
	case SK_RUSH: {
		V3 v = sk_vel(a); float hs = sqrtf(v.x * v.x + v.y * v.y);
		A->dashing = hs > 700;   // sprinting at 2x
		A->energy -= (A->dashing ? D->p1[ti] : D->p2[ti]) * dt; A->bar = A->energy;
		if (A->dashing) {
			if ((sk_frame & 1) == 0) { V3 dd = v3(v.x / hs, v.y / hs, 0); sk_fx(SKF_RUSH, p, v3(0, 0, 1), &dd); }
			V3 rd = v3(v.x / hs, v.y / hs, 0);
			for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && sk_hostile(a, b)) {   // contact while dashing = takedown
				V3 q = vsub(sk_pos(b), p); float hd = sqrtf(q.x * q.x + q.y * q.y); if (hd > 110 || fabsf(q.z) > 120 || hd < 1) continue;
				if ((q.x * rd.x + q.y * rd.y) / hd < 0.707f) continue;
				char n[96]; V3 vc = sk_chest(b), nb = vmul(rd, -1);
				snprintf(n, sizeof(n), "voice/action/%s/cqc_split_%s_attack_f%d", SK_STEM[SK_RUSH], SK_STEM[SK_RUSH], 1 + (sk_rnd() < 0.5f)); if (a == sk_local) ev_play(n, NULL); else ev_play(n, &p.x);
				snprintf(n, sizeof(n), "voice/action/%s/cqc_split_%s_death_f%d", SK_STEM[SKA[b].skill], SK_STEM[SKA[b].skill], 1 + (sk_rnd() < 0.5f)); ev_play(n, b == sk_local ? NULL : &vc.x);
				sk_fx(SKF_RUSH_HIT, vc, nb, &rd); sk_fx(SKF_HIT_BLOOD, vc, nb, &rd);
				sk_damage(b, 999, a, SKD_MELEE); sk_end_active(a, 0); return;
			}
		}
		if (A->energy <= 0) { A->energy = 0; sk_end_active(a, 0); }
	} break;
	case SK_PLASMASHIELD:
		if (A->toggled) A->energy -= D->p2[ti] * dt;
		A->bar = A->energy;
		if (A->energy <= 0) { A->energy = 0; sk_end_active(a, 1); }
		else if (A->t >= D->duration[ti]) sk_end_active(a, 0);
		break;
	case SK_CYBERDIVE:
		A->bar = 1 - A->t / D->duration[ti];
		if (a != sk_local) {   // bots: the body stands still 1.5 s, then dives into its target if it is still there
			A->dive_still += dt; int tg = -1, sees = 0; float lost = 9; V3 ls;
			if (A->dive_still > 1.5f && SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &lost, &ls) && tg >= 0 && sees && sk_alive(tg) && sk_hostile(a, tg) && sk_len(vsub(sk_pos(tg), p)) < 2500) { sk_hack(a, tg); return; }
		}
		if (A->t >= D->duration[ti]) sk_end_active(a, 0);
		break;
	default: if (A->t >= D->duration[ti]) sk_end_active(a, 0); break;
	}
}

// ---- bots: when to use the skill (SKILLS.md 3.x "bot use") and how it changes their moves
static int sk_count_enemies(int a, V3 c, float r) { int n = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), c)) < r) n++; return n; }
static int sk_bot_place_at(int a, V3 want) {   // bots place without a preview: on the floor at want
	SkillActor *A = &SKA[a]; V3 n; float dn = raycast(v3(want.x, want.y, want.z + 100), v3(0, 0, -1), SK_DEF[A->skill].placed == 2 ? 2500 : 400, NULL, &n); sk_stat_rays++;
	if (dn < 0 || n.z < 0.7f) return 0;
	sk_place(a, v3(want.x, want.y, want.z + 100 - dn), n); return 1;
}
static void sk_bot_think(int a, float dt) {
	SkillActor *A = &SKA[a];
	int tg = -1, sees = 0; float lost = 99; V3 ls = v3(0, 0, 0);
	int info = SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &lost, &ls);
	if (info && sees && tg >= 0) { A->seen_t += dt; A->nosee_t = 0; } else { A->seen_t = 0; A->nosee_t += dt; }
	if (!info || !sk_alive(a) || A->hack_t > 0 || A->emp_t > 0) return;
	V3 p = sk_pos(a); float td = tg >= 0 && sk_valid(tg) ? sk_len(vsub(sk_pos(tg), p)) : 1e9f; float hp = sk_hp(a);
	if (A->state == SKS_AIM && A->skill == SK_ARMLAUNCHER) { if ((A->nlocks > 0 && A->t > 1.2f) || A->t > 3) sk_launcher_fire(a); return; }
	A->bot_t -= dt; if (A->state != SKS_READY || A->bot_t > 0 || !sk_bots_use) return;
	A->bot_t = 2;
	int go = 0; V3 at = p; float fy = sk_yaw(a); V3 fwd = v3(cosf(fy), sinf(fy), 0);
	switch (A->skill) {
	case SK_ARMLAUNCHER: go = sees && td > 800 && td < 4000 && A->seen_t > 1; break;
	case SK_OPTICAL: go = (sees && td > 1500) || (hp < 50 && A->hit_ago < 2.5f); break;
	case SK_HEATSCAN: { int ne = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b)) ne++; go = !sees && A->nosee_t > 4 && ne > 2; } break;
	case SK_SPECIALRECON: go = !sees && A->nosee_t > 3 && sk_rnd() < 0.35f; at = vadd(p, vmul(fwd, 250)); break;
	case SK_SENTRYGUN: go = sees && td > 1000; at = vadd(p, vmul(fwd, 200)); break;
	case SK_METABOOST: { int low = 0; for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && !sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < 1000 && sk_hp(b) < 70) low++; go = hp < 50 || low >= 2; } break;
	case SK_RUSH: go = sees && td > 600 && td < 2000; break;
	case SK_PLASMASHIELD: go = sees && A->hit_ago < 2.5f && hp < 70; break;
	case SK_EMP: {
		int seen_obj = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, A->team) && sk_len(vsub(SKO[o].pos, p)) < 1500) { seen_obj = o; break; }
		if (seen_obj >= 0 && sk_los(sk_eye(a), sk_obj_center(seen_obj))) { go = 1; at = SKO[seen_obj].pos; }
		else if (sees && tg >= 0 && td < 1500 && sk_count_enemies(a, sk_pos(tg), 800) >= 2) { go = 1; at = sk_pos(tg); }
		if (go) { V3 d = vsub(at, p); float l = sk_hdist(at, p); if (l > 650) at = vadd(p, vmul(sk_norm(v3(d.x, d.y, 0)), 650)); } } break;
	case SK_CYBERWALL: go = sees && td > 1200 && hp < 60; if (go && tg >= 0) at = vadd(p, vmul(sk_norm(v3(sk_pos(tg).x - p.x, sk_pos(tg).y - p.y, 0)), 300)); break;
	case SK_CYBERDIVE: go = sees && td < 2500; break;
	case SK_DMDDRONE: go = !sees && lost < 3 && tg >= 0; at = ls; break;
	}
	if (!go) return;
	if (sk_press(a) && A->state == SKS_AIM && SK_DEF[A->skill].placed) { if (!sk_bot_place_at(a, at)) { A->state = SKS_READY; A->bot_t = 3; } }
}
// bot movement override (called by the bots' movement through the engine): rush charges, hacked bots drift, EMP / drone escape,
// warned bots stop sprinting, shield strafing, shooting at deployables. yaw may be changed. Returns 1 when it changed something.
static int sk_bot_move(int a, float *mx, float *my, int *sprint, int *jump, float *yaw) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a);
	if (A->hack_t > 0) { *mx = *my = 0; *sprint = 0; *jump = 0; *yaw += sinf(sk_now * 2.3f + a) * 1.8f * 0.016f; return 1; }
	if (A->state == SKS_AIM || sk_is_active(a, SK_CYBERDIVE)) { *mx = *my = 0; *sprint = 0; *jump = 0; return 1; }
	if (sk_is_active(a, SK_RUSH)) { int tg = -1, sees = 0; float l; V3 ls; if (SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &l, &ls) && sees) { *mx = 0; *my = 1; *sprint = 1; return 1; } }
	for (int o = 0; o < SK_MAXOBJ; o++) {   // run from a charging enemy EMP / out of an enemy drone's laser
		SkObj *O = &SKO[o]; if (!O->alive || O->team == A->team) continue;
		V3 c = O->type == SKO_DRONE ? O->pos : O->pos; float r = O->type == SKO_EMP && O->phase == 0 ? O->r + 200 : O->type == SKO_DRONE && O->target == a && A->otgt != SK_TGT_OBJ + o ? 950 : 0;
		if (r <= 0) continue; float d = sk_hdist(p, c); if (d > r) continue;
		*yaw = atan2f(p.y - c.y, p.x - c.x); *mx = 0; *my = 1; *sprint = 1; return 1;
	}
	if (A->otgt >= 0) { V3 v = vsub(sk_target_pos(A->otgt), p); *yaw = atan2f(v.y, v.x); *mx = *my = 0; *sprint = 0; return 1; }
	int ch = 0;
	if (A->warn_t > 0 && *sprint) { *sprint = 0; ch = 1; }
	{ int tg = -1, sees = 0; float l; V3 ls;   // a target behind a shield panel facing us: strafe round it
		if (SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &l, &ls) && sees && tg >= 0 && sk_is_active(tg, SK_PLASMASHIELD) && SKA[tg].toggled && sk_ti(tg) < 2) {
			V3 q = sk_norm(vsub(p, sk_pos(tg))); float ty = sk_yaw(tg); if (q.x * cosf(ty) + q.y * sinf(ty) > 0.5f) { A->strafe_t -= 0.016f; if (A->strafe_t <= 0) { A->strafe_t = 1.2f; A->strafe = sk_rnd() < 0.5f ? -1 : 1; } *mx = (float)A->strafe; *my = 0.3f; ch = 1; } } }
	return ch;
}
// a roam goal from skills for bot a: the nearest enemy its team knows about (heat scan, recon mark, wall pass)
static int sk_bot_goal(int a, float *g) {
	if (!sk_ok || !sk_valid(a)) return 0; int tm = SKA[a].team & 1, best = -1; float bd = 1e9f; V3 p = sk_pos(a);
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && (SKA[b].heat_t[tm] > 0 || SKA[b].mark_t[tm] > 0)) { float d = sk_len(vsub(sk_pos(b), p)); if (d < bd) { bd = d; best = b; } }
	if (best < 0) return 0; V3 q = sk_pos(best); g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1;
}
// bots shooting at hostile deployables when no operator is in sight (turrets / drones / recon units in 15 m)
static void sk_bot_objects(int a, float dt) {
	SkillActor *A = &SKA[a]; int tg = -1, sees = 0; float l; V3 ls;
	if (!sk_bots_use || !SKW_.bot_info || !SKW_.bot_info(a, &tg, &sees, &l, &ls) || sees || A->hack_t > 0 || A->state == SKS_AIM || sk_is_active(a, SK_CYBERDIVE) || sk_is_active(a, SK_RUSH)) { A->otgt = -1; return; }
	A->otgt_t -= dt;
	if (A->otgt_t <= 0) { A->otgt_t = 0.5f; A->otgt = -1; V3 e = sk_eye(a); float bd = 1500;
		for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, A->team) && SKO[o].type != SKO_WALL && SKO[o].type != SKO_EMP) { float d = sk_len(vsub(sk_obj_center(o), e)); if (d < bd) { bd = d; A->otgt = SK_TGT_OBJ + o; } }
		if (A->otgt >= 0 && !sk_los(e, sk_target_pos(A->otgt))) A->otgt = -1; }
	if (A->otgt < 0 || !sk_target_ok(A->otgt, A->team)) { A->otgt = -1; return; }
	A->oshot_t -= dt;
	while (A->oshot_t <= 0) { A->oshot_t += 0.12f; if (sk_rnd() < 0.3f) { A->oshot_t += 0.5f; break; }   // bursts
		V3 e = sk_eye(a), tp = sk_target_pos(A->otgt), end = tp; int hit = sk_rnd() < 0.4f * (A->emp_t > 0 ? 0.5f : 1);
		if (hit) sk_obj_damage(A->otgt - SK_TGT_OBJ, 9, a); else end = vadd(tp, v3((sk_rnd() - 0.5f) * 100, (sk_rnd() - 0.5f) * 100, (sk_rnd() - 0.4f) * 80));
		if (SKW_.shot_fx) SKW_.shot_fx(vadd(e, v3(0, 0, -25)), end); snd_wfire_tp("m4a1", &e.x);
		if (A->otgt < 0 || !SKO[A->otgt - SK_TGT_OBJ].alive) { A->otgt = -1; break; } }
}

static void sk_update_actor(int a, float dt) {
	SkillActor *A = &SKA[a]; int alive = sk_alive(a);
	if (A->was_alive && !alive) { sk_cancel(a); A->hack_t = A->emp_t = 0; if (a == sk_local) { SKV.camo = SKV.laugh = 0; fade_others("cloaking_loop", -1, 200); } }   // death ends personal effects
	A->was_alive = alive;
	float hp = sk_hp(a);
	if (alive && A->last_hp >= 0 && hp < A->last_hp - 0.01f) {   // took damage
		A->hit_ago = 0; if (A->bot_t > 0.3f) A->bot_t = 0.3f;   // bots react to being shot (shield, camo)
		if (sk_is_active(a, SK_OPTICAL)) sk_fx_up(SKF_CAMO_HIT, sk_pos(a));
		if (sk_is_active(a, SK_CYBERDIVE)) { sk_ev(a, "intel_skill/cyberdive_dmg_tp"); sk_end_active(a, 0); }   // the body was hit: back at once
	}
	A->last_hp = alive ? hp : -1;
	if (SKW_.firing && SKW_.firing(a)) A->fire_t = 0; else A->fire_t += dt;
	A->hit_ago += dt;
	if (A->hack_t > 0) { A->hack_t -= dt; if (a == sk_local) { SKV.laugh = A->hack_t; if (SKV.glitch < 0.1f) { SKV.glitch = SKV.glitch_len = 0.25f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; } } }
	if (A->emp_t > 0) { A->emp_t -= dt; A->shock_t -= dt; if (A->shock_t <= 0 && alive) { A->shock_t = 0.3f; sk_fx_up(SKF_EMP_SHOCK, sk_pos(a)); } }
	for (int k = 0; k < 2; k++) { if (A->heat_t[k] > 0) A->heat_t[k] -= dt; if (A->mark_t[k] > 0) A->mark_t[k] -= dt; }
	if (A->warn_t > 0) A->warn_t -= dt; if (A->laser_t > 0) A->laser_t -= dt;
	if (A->cool > 0) { A->cool -= dt; if (A->cool <= 0) { A->cool = 0;
		if (A->state == SKS_COOLDOWN) { A->state = SKS_READY; A->t = 0;
			if (a == sk_local) { char n[64]; snprintf(n, sizeof(n), "voice/donut/common/op_%s_ready", SK_DEF[A->skill].id); radio_play(n);
				ev_play("intel_skill/cloaking_hud_on", NULL); sk_notify(a, SKN_TEXT, "SKILL READY", 0); } } } }
	else if (A->state == SKS_COOLDOWN) A->state = SKS_READY;
	A->t += dt;
	if (!alive) { A->bar = -1; return; }
	if (A->state == SKS_AIM) sk_tick_aim(a, dt);
	else if (A->state == SKS_ACTIVE) sk_tick_active(a, dt);
	// HUD bar: the active skill's energy / time, or the deployable's life
	if (A->state == SKS_AIM) A->bar = A->skill == SK_ARMLAUNCHER ? 1 - A->t / SK_DEF[SK_ARMLAUNCHER].duration[0] : -1;
	else if (A->state != SKS_ACTIVE) {
		A->bar = -1; int o = A->obj;
		if (o >= 0 && o < SK_MAXOBJ && SKO[o].alive && SKO[o].owner == a) { const SkObj *O = &SKO[o];
			A->bar = O->type == SKO_DRONE ? O->energy : O->type == SKO_EMP ? (O->phase == 0 ? 1 - (float)(int)(O->t / 0.667f) / 3 : 0) : fminf(1 - O->t / O->life, O->hp / O->hpmax); if (A->bar < 0) A->bar = 0; }
	}
	if (a != sk_local) { sk_bot_think(a, dt); sk_bot_objects(a, dt); }
}
// local player: the enemy-skill voice warning when an enemy skill shows up in view (at 2 Hz, one sight ray per check)
static void sk_local_checks(float dt) {
	if (SKV.warn_cd > 0) SKV.warn_cd -= dt;
	static float t; t -= dt; if (t > 0 || !sk_alive(sk_local)) return; t = 0.5f;
	static int rr; V3 e = sk_eye(sk_local);
	for (int k = 0; k < SK_MAXACT; k++) { int b = (rr + k) % SK_MAXACT; if (b == sk_local || !sk_alive(b) || !sk_hostile(b, sk_local)) continue;
		SkillActor *B = &SKA[b]; if (B->warned) continue;
		V3 at; int rel = 0;
		if (B->state == SKS_ACTIVE && (B->skill == SK_OPTICAL || B->skill == SK_RUSH || B->skill == SK_PLASMASHIELD || B->skill == SK_METABOOST)) { at = sk_chest(b); rel = 1; }
		else if (B->obj >= 0 && SKO[B->obj].alive && SKO[B->obj].owner == b) { at = sk_obj_center(B->obj); rel = 1; }
		if (!rel || sk_len(vsub(at, e)) > 3000) continue;
		rr = b + 1; if (sk_los(e, at)) { B->warned = 1; sk_warn(B->skill); } break; }
}
// once per frame, after the player's input and before the bots
static void skills_update(float dt) {
	if (!sk_ok) return; sk_frame++; sk_now += dt; sk_stat_rays = 0;
	int n = sk_n(); for (int a = n; a < SK_MAXACT; a++) if (SKA[a].on) sk_actor_off(a);
	if (dt <= 0) return;
	for (int a = 0; a < SK_MAXACT; a++) if (SKA[a].on) sk_update_actor(a, dt);
	sk_update_objs(dt); sk_update_missiles(dt); sk_local_checks(dt);
	float k = dt; SKV.glitch -= k; SKV.hlines -= k; SKV.shield_hit -= k; SKV.flash -= k; SKV.vig -= k; SKV.heat -= k; if (SKV.laugh > 0) SKV.laugh -= k;
	{ int c = sk_is_active(sk_local, SK_OPTICAL); SKV.camo += (c ? 1 : -1) * dt / 0.3f; SKV.camo = sk_clamp(SKV.camo, 0, 1); }
}

// ---- hooks the engine calls
// 0 hidden (camo far away, the observer is hacked), 1 normal (still needs a sight line), 2 known through walls (revealed to its team)
static int sk_can_see(int obs, int tgt) {
	if (!sk_ok || !sk_valid(tgt) || !sk_valid(obs)) return 1;
	const SkillActor *O = &SKA[obs], *T = &SKA[tgt];
	if (O->hack_t > 0) return 0;
	if (T->team == O->team) return 1;
	if (T->skill == SK_OPTICAL && T->state == SKS_ACTIVE) {
		float d = sk_len(vsub(sk_pos(obs), sk_pos(tgt)));
		if (T->fire_t < 1.0f && d < 2500) return 1;   // shots give her away for a second
		if (d > SK_DEF[SK_OPTICAL].p1[0]) return 0;
		return T->camo_close > 0.8f;
	}
	if (T->heat_t[O->team & 1] > 0 || T->mark_t[O->team & 1] > 0) return 2;
	return 1;
}
static float sk_aim_factor(int a) {   // bots: hit chance factor; 0 = no firing (hacked, placing, diving, rushing)
	if (!sk_ok || !sk_valid(a)) return 1; const SkillActor *A = &SKA[a];
	if (A->hack_t > 0 || A->state == SKS_AIM || sk_is_active(a, SK_CYBERDIVE) || sk_is_active(a, SK_RUSH)) return 0;
	return A->emp_t > 0 ? 0.5f : 1;
}
static int sk_can_fire(int a) { return !sk_ok || !sk_valid(a) || sk_aim_factor(a) > 0; }
static float sk_speed_mul(int a, int sprinting) { return sk_ok && sk_is_active(a, SK_RUSH) && sprinting ? 2.0f : 1.0f; }
static float sk_fov_add(int a) { return sk_ok && sk_is_active(a, SK_RUSH) && SKA[a].dashing ? 8 : 0; }
static int sk_hide_viewmodel(int a) { return sk_ok && sk_valid(a) && ((SKA[a].state == SKS_AIM && SKA[a].skill == SK_ARMLAUNCHER) || sk_is_active(a, SK_CYBERDIVE)); }
static int sk_camo_on(int a) { return sk_ok && sk_is_active(a, SK_OPTICAL); }
// actor b is an enemy the local player's team knows about through walls (heat scan, recon mark, wall pass): minimap dot
static int sk_known(int b) { if (!sk_ok || !sk_valid(b) || !sk_valid(sk_local) || !sk_hostile(b, sk_local)) return 0; int t = SKA[sk_local].team & 1; return SKA[b].heat_t[t] > 0 || SKA[b].mark_t[t] > 0; }
static int sk_ready(int a) { return sk_ok && sk_valid(a) && SKA[a].state == SKS_READY && SKA[a].hack_t <= 0 && SKA[a].emp_t <= 0; }
// the player's movement input while a skill owns it: the dive moves the ghost camera (noclip, 12 m/s, <= 15 m from the body) and
// the body stands still. Returns 1 when the inputs were taken.
static int sk_move_input(int a, float *mx, float *my, int *sprint, int *jump, float dt) {
	if (!sk_ok || !sk_is_active(a, SK_CYBERDIVE)) return 0;
	SkillActor *A = &SKA[a]; V3 d = sk_dir(a), r = sk_norm(v3(d.y, -d.x, 0)), mv = vadd(vmul(d, *my), vmul(r, *mx));
	A->cam = vadd(A->cam, vmul(mv, 1200 * dt));
	V3 body = sk_eye(a), off = vsub(A->cam, body); float l = sk_len(off); if (l > 1500) A->cam = vadd(body, vmul(off, 1500 / l));
	if (gw > 0) { float x0 = gx0, y0 = gy0, x1 = gx0 + gw * CELL, y1 = gy0 + gh * CELL; A->cam.x = sk_clamp(A->cam.x, x0, x1); A->cam.y = sk_clamp(A->cam.y, y0, y1); }
	*mx = *my = 0; *sprint = 0; *jump = 0; return 1;
}
static int sk_camera(int a, V3 *eye) { if (!sk_ok || !sk_is_active(a, SK_CYBERDIVE)) return 0; *eye = SKA[a].cam; return 1; }
// draw alpha of actor a's body as seen from cam: 1 normal, (0..1) camo ghost, 0 hidden
static float sk_draw_alpha(int a, V3 cam) {
	if (!sk_ok || !sk_is_active(a, SK_OPTICAL)) return 1;
	const SkillActor *A = &SKA[a]; if (sk_valid(sk_local) && !sk_hostile(a, sk_local)) return 0.4f;
	if (A->fire_t < 0.3f) return 0.35f;
	float d = sk_len(vsub(sk_pos(a), cam)); float al = 0.12f * sk_clamp(1 - d / 1500, 0, 1); return al < 0.004f ? 0 : al;
}
// HUD: skill icon, cooldown (0..1, -1 ready), active bar (0..1, -1 none)
static void sk_hud(int a, int *icon, float *cool, float *bar) {
	if (!sk_ok || !sk_valid(a)) { if (icon) *icon = 8; if (cool) *cool = -1; if (bar) *bar = -1; return; }
	const SkillActor *A = &SKA[a]; float cd = SK_DEF[A->skill].cooldown[sk_ti(a)];
	if (icon) *icon = SK_DEF[A->skill].hud_icon;
	if (cool) { if (A->state == SKS_READY || A->state == SKS_AIM) *cool = A->hack_t > 0 || A->emp_t > 0 ? 1 : -1; else *cool = sk_clamp(A->cool / cd, 0, 1); }
	if (bar) *bar = A->bar;
}
// autoplay (test runs): use the skill when it would make sense (the bot rules from the player's view) and confirm placements
static void sk_autoplay(int a, int fighting, float dt, int *press, int *fire) {
	if (!sk_ok || !sk_alive(a)) return; SkillActor *A = &SKA[a];
	static float wait = 2; wait -= dt;
	if (sk_ready(a) && wait <= 0 && (fighting || sk_rnd() < 0.01f)) { *press = 1; wait = 3; }
	if (A->state == SKS_AIM && A->t > 0.6f && (A->skill != SK_ARMLAUNCHER || A->nlocks > 0 || A->t > 2.5f)) *fire = 1;
	if (sk_is_active(a, SK_CYBERDIVE) && A->t > 1) *fire = 1;
}

// ---- drawing
static int sk_room(const float *VP) { if (sp_n + 6 > SP_MAXQ * 6) { sprite_flush_mode(VP, fx_tex, 1); return 1; } return 0; }
static void sk_cuv(int c, float *uv) { if (c < 0) { uv[0] = uv[1] = 0; uv[2] = uv[3] = 1; return; } const float *r = fx_cell[c]; memcpy(uv, r, 16); }
static void sk_quad(const float *VP, V3 a, V3 b, V3 c, V3 d, int cell, const float *col) {
	if (cell < 0) return; float uv[4]; sk_cuv(cell, uv); sk_room(VP); sprite_quad(&a.x, &b.x, &c.x, &d.x, uv[0], uv[1], uv[2], uv[3], col);
}
static void sk_ground(const float *VP, V3 c, float r, float rot, int cell, const float *col) {   // flat square on the floor
	float cs = cosf(rot) * r, sn = sinf(rot) * r; V3 ax = v3(cs, sn, 0), ay = v3(-sn, cs, 0);
	sk_quad(VP, vadd(c, vsub(vmul(ay, -1), ax)), vadd(c, vsub(ax, ay)), vadd(c, vadd(ax, ay)), vadd(c, vsub(ay, ax)), cell, col);
}
static void sk_sector(const float *VP, V3 apex, float r, float th, const float *col) {   // the 90 deg wedge cell (apex at u0,v1) from angle th to th + 90 deg
	V3 e1 = v3(cosf(th) * r, sinf(th) * r, 0), e2 = v3(-sinf(th) * r, cosf(th) * r, 0);
	sk_quad(VP, vadd(apex, e2), vadd(apex, vadd(e1, e2)), vadd(apex, e1), apex, sk_cell[SKC_SECTOR], col);
}
static void sk_arc(const float *VP, V3 apex, float r, float yaw, float arc_deg, const float *col) {   // the sentry's firing arc on the floor: 1, 2 or 4 wedges
	int n = (int)(arc_deg / 90 + 0.5f); if (n < 1) n = 1; if (n > 4) n = 4; float a0 = yaw - n * 0.785398f;
	for (int k = 0; k < n; k++) sk_sector(VP, apex, r, a0 + k * 1.5707963f, col);
}
static void sk_marker(const float *VP, V3 p, float w, float h, const float *cam, const float *R, const float *U, const float *col) {   // the kal bar, mirrored (the cell is its left half)
	if (sk_cell[SKC_KAL] < 0) return; float d = sk_len(vsub(p, v3(cam[0], cam[1], cam[2]))), k = fmaxf(1, d / 1500); w *= k; h *= k;   // keeps its screen size beyond 15 m
	float uv[4]; sk_cuv(sk_cell[SKC_KAL], uv); V3 r = v3(R[0] * w, R[1] * w, R[2] * w), u = v3(U[0] * h, U[1] * h, U[2] * h);
	V3 tl = vadd(vsub(p, r), u), tc = vadd(p, u), tr = vadd(vadd(p, r), u), bl = vsub(vsub(p, r), u), bc = vsub(p, u), br = vsub(vadd(p, r), u);
	sk_room(VP); sprite_quad(&tl.x, &tc.x, &bc.x, &bl.x, uv[0], uv[1], uv[2], uv[3], col); sk_room(VP); sprite_quad(&tc.x, &tr.x, &br.x, &bc.x, uv[2], uv[1], uv[0], uv[3], col);
}
static void sk_band(const float *VP, V3 c, float r, float h, int segs, int cell, const float *col) {   // a vertical band (cylinder wall) round c: the heat-scan front
	float uv[4]; if (cell < 0) return; sk_cuv(cell, uv);
	for (int k = 0; k < segs; k++) { float a0 = k * 6.2831853f / segs, a1 = (k + 1) * 6.2831853f / segs;
		V3 p0 = v3(c.x + cosf(a0) * r, c.y + sinf(a0) * r, c.z), p1 = v3(c.x + cosf(a1) * r, c.y + sinf(a1) * r, c.z), q0 = vadd(p0, v3(0, 0, h)), q1 = vadd(p1, v3(0, 0, h));
		sk_room(VP); sprite_quad(&q0.x, &q1.x, &p1.x, &p0.x, uv[0], uv[1], uv[2], uv[3], col); }
}
static void sk_bill(const float *VP, V3 p, float w, float h, const float *R, const float *U, int cell, const float *col) {
	V3 r = v3(R[0] * w, R[1] * w, R[2] * w), u = v3(U[0] * h, U[1] * h, U[2] * h);
	sk_quad(VP, vadd(vsub(p, r), u), vadd(vadd(p, r), u), vsub(vadd(p, r), u), vsub(vsub(p, r), u), cell, col);
}
static void sk_beam_q(const float *VP, V3 a, V3 b, float w, const float *cam, int cell, const float *col) {   // camera-facing strip a -> b (the beam cell runs along u, core at mid v)
	V3 d = vsub(b, a); float L = sk_len(d); if (L < 1) return; d = vmul(d, 1 / L);
	V3 m = vmul(vadd(a, b), 0.5f), toc = vsub(v3(cam[0], cam[1], cam[2]), m), sd = vcross(d, toc); float sl = sk_len(sd); if (sl < 1e-4f) return;
	w = fmaxf(w, sk_len(toc) * 0.009f);   // at least ~6 px wide at 960x544 / 70 deg, so the 1-texel core stays visible
	sd = vmul(sd, w * 0.5f / sl);
	sk_quad(VP, vsub(a, sd), vsub(b, sd), vadd(b, sd), vadd(a, sd), cell, col);
}
static void sk_pillar_q(const float *VP, V3 base, float h, float w, const float *cam, const float *col) {   // vertical light pillar (bright at v1 = the floor)
	V3 toc = v3(cam[0] - base.x, cam[1] - base.y, 0), sd = sk_norm(v3(-toc.y, toc.x, 0)); sd = vmul(sd, w * 0.5f);
	V3 top = vadd(base, v3(0, 0, h)); sk_quad(VP, vsub(top, sd), vadd(top, sd), vadd(base, sd), vsub(base, sd), sk_cell[SKC_PILLAR], col);
}
// a model at p turned by rz (model faces -y: pass world yaw + 90 deg), scaled S, posed P; add: every mesh additive (hologram / build)
static void sk_mdraw(int mi, const float *VP, V3 p, float rz, const float *S, const MBone *P, const float *tint, int add, int m0, int m1, const float *fogc, const float *lit) {
	if (!sk_mok[mi]) return; Model *M = &sk_mdl[mi];
	float c = cosf(rz), s = sinf(rz), sx = S ? S[0] : 1, sy = S ? S[1] : 1, sz = S ? S[2] : 1;
	float W[16] = { c * sx, s * sx, 0, 0, -s * sy, c * sy, 0, 0, 0, 0, sz, 0, p.x, p.y, p.z, 1 }, MVP[16]; sk_mul4(MVP, VP, W);
	static const float Lw[3] = { 0.33f, 0.42f, 0.84f }; float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
	int n = M->nmesh < 8 ? M->nmesh : 8; if (m1 > n) m1 = n; if (m0 < 0) m0 = 0; if (m0 >= m1) return;
	MMesh mm[8]; memcpy(mm, M->mesh + m0, sizeof(MMesh) * (m1 - m0)); if (add) for (int k = 0; k < m1 - m0; k++) mm[k].blend = 2;
	Model T = *M; T.mesh = mm; T.nmesh = m1 - m0;
	float tn[4] = { tint[0], tint[1], tint[2], tint[3] }; if (lit && !add) for (int k = 0; k < 3; k++) tn[k] *= sk_clamp(lit[k] * 1.1f, 0.25f, 1.4f);
	static const float nofog[4] = { 0, 0, 0, 0 };
	if (mm[0].rigid >= 0) model_draw_cpu(&T, &sk_sc[mi], MVP, P ? P : sk_ident, ld, tn, add ? nofog : fogc, 0);
	else model_draw(&T, MVP, P ? P : sk_ident, ld, tn, add ? nofog : fogc);   // debug "cpuall" mode: the palette path
}
static void sk_turret_pose(int o, MBone *P) {   // base fixed; yaw about the yaw pivot; pitch about the pitch pivot (gun faces -y)
	const SkObj *O = &SKO[o]; for (int k = 0; k < 3; k++) P[k] = sk_ident[k];
	const float *cy = sk_mok[SKM_TURRET] ? model_attach(&sk_mdl[SKM_TURRET], "yaw") : NULL, *cp = sk_mok[SKM_TURRET] ? model_attach(&sk_mdl[SKM_TURRET], "pitch") : NULL;
	float c1[3] = { 0, 1.5f, 40.5f }, c2[3] = { 0, 1.5f, 53.9f }; if (cy) memcpy(c1, cy, 12); if (cp) memcpy(c2, cp, 12);
	float qz[4], qx[4], q[4], v[3], o3[3]; sk_qaxis(qz, 0, 0, 1, sk_wrap(O->yaw - O->yaw0)); sk_qaxis(qx, 1, 0, 0, -O->pitch);
	quat_rotate(qz, c1, o3); memcpy(P[1].q, qz, 16); for (int k = 0; k < 3; k++) P[1].p[k] = c1[k] - o3[k];
	sk_qmul(q, qz, qx); memcpy(P[2].q, q, 16); quat_rotate(qx, c2, v); for (int k = 0; k < 3; k++) v[k] = c2[k] - v[k] - c1[k];
	quat_rotate(qz, v, o3); for (int k = 0; k < 3; k++) P[2].p[k] = o3[k] + c1[k];
}
// world pass, after the opaque scene and the characters, before fx_draw. fogc: fog colour (a = 1) as the map / characters
static void sk_draw_world(const float *VP, const float *cam, const float *R, const float *U, const float *fogc) {
	if (!sk_ok) return;
	if (sp_n) sprite_flush(VP, sp_blob, 0);
	V3 C = v3(cam[0], cam[1], cam[2]); int lt = sk_valid(sk_local) ? SKA[sk_local].team : 0; float now = sk_now;
	glDisable(GL_CULL_FACE);
	static const float WHITE[4] = { 1, 1, 1, 1 }, HOLO[4] = { 0.3f, 0.8f, 1, 0.6f }, HOLO_BAD[4] = { 1, 0.25f, 0.2f, 0.6f };
	// deployables
	for (int o = 0; o < SK_MAXOBJ; o++) {
		SkObj *O = &SKO[o]; if (!O->alive) continue;
		V3 d = vsub(O->type == SKO_DRONE ? O->cur : O->pos, C); if (vdot(d, d) > 6000.f * 6000.f) continue;
		float bk = O->phase == 0 && O->type != SKO_DRONE ? sk_clamp(O->t / (O->type == SKO_RECON ? 1.0f : 2.0f), 0, 1) : 1;
		float S[3] = { 1, 1, 0.1f + 0.9f * bk }; const float *sc = O->phase == 0 && O->type != SKO_DRONE ? S : NULL; int add = O->phase == 0 && O->type != SKO_DRONE && O->type != SKO_EMP;
		float hc[4] = { HOLO[0], HOLO[1], HOLO[2], 0.4f + 0.4f * bk }; const float *tn = add ? hc : WHITE;
		switch (O->type) {
		case SKO_TURRET: { MBone P[3]; sk_turret_pose(o, P); sk_mdraw(SKM_TURRET, VP, O->pos, O->yaw0 + 1.5707963f, sc, P, tn, add, 0, O->tier >= 3 ? 4 : 3, fogc, O->lit); } break;
		case SKO_DRONE: sk_mdraw(SKM_DRONE, VP, O->cur, O->yaw + 1.5707963f, NULL, NULL, WHITE, 0, 0, 8, fogc, O->lit); break;
		case SKO_EMP: { MBone P[3]; for (int k = 0; k < 3; k++) P[k] = sk_ident[k];
			if (sk_emp_spin >= 0 && O->phase == 0 && sk_mdl[SKM_EMP].nb <= 3) model_sample(&sk_mdl[SKM_EMP], sk_emp_spin, O->t * (1 + O->t), P);
			sk_mdraw(SKM_EMP, VP, O->pos, O->yaw0 + 1.5707963f, NULL, P, WHITE, 0, 0, 8, fogc, O->lit); } break;
		case SKO_RECON: sk_mdraw(SKM_MINI, VP, O->pos, O->yaw0 + 1.5707963f, sc, NULL, tn, add, 0, 8, fogc, O->lit); break;
		case SKO_WALL: sk_mdraw(SKM_WALLPOD, VP, O->pos, atan2f(O->nrm.y, O->nrm.x) - 1.5707963f, NULL, NULL, WHITE, 0, 0, 8, fogc, O->lit); break;
		}
	}
	// the local player's placement hologram
	if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && SK_DEF[SKA[sk_local].skill].placed == 1 && sk_alive(sk_local)) {
		SkillActor *A = &SKA[sk_local]; const float *hc = A->aim_ok ? HOLO : HOLO_BAD; float y = sk_yaw(sk_local) + 1.5707963f;
		switch (A->skill) {
		case SK_SENTRYGUN: sk_mdraw(SKM_TURRET, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, A->tier >= 3 ? 4 : 3, fogc, NULL); break;
		case SK_EMP: sk_mdraw(SKM_EMP, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, 8, fogc, NULL); break;
		case SK_SPECIALRECON: sk_mdraw(SKM_MINI, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, 8, fogc, NULL); break;
		case SK_CYBERWALL: { V3 f = sk_pos(sk_local), n = sk_norm(v3(f.x - A->aim_pt.x, f.y - A->aim_pt.y, 0)); float S[3] = { sk_p1(sk_local), 1, sk_p2(sk_local) };
			float h2[4] = { hc[0], hc[1], hc[2], 0.35f }; sk_mdraw(SKM_WALL, VP, A->aim_pt, atan2f(n.y, n.x) - 1.5707963f, S, NULL, h2, 1, 0, 8, fogc, NULL);
			sk_mdraw(SKM_WALLPOD, VP, A->aim_pt, atan2f(n.y, n.x) - 1.5707963f, NULL, NULL, hc, 1, 0, 8, fogc, NULL); } break;
		}
	}
	// additive: walls, shields, domes, EMP spheres
	for (int o = 0; o < SK_MAXOBJ; o++) {
		SkObj *O = &SKO[o]; if (!O->alive) continue; int ally = O->team == lt;
		if (O->type == SKO_WALL) { float k = O->phase == 0 ? sk_clamp(O->t / 2, 0.05f, 1) : 1, left = O->life - O->t;
			float al = 0.8f * (left < 2 ? 0.6f + 0.4f * (sinf(now * 50) > 0) : 1) * (O->hit_t > 0 ? 1.25f : 1);
			float tn[4] = { ally ? 0.4f : 1, ally ? 0.9f : 0.3f, ally ? 1 : 0.25f, al }, S[3] = { O->w, 1, O->h * k };
			sk_mdraw(SKM_WALL, VP, O->pos, atan2f(O->nrm.y, O->nrm.x) - 1.5707963f, S, NULL, tn, 1, 0, 8, fogc, NULL); }
		else if (O->type == SKO_EMP && O->phase == 0) { float r = O->r * sk_clamp(O->t / 1.5f, 0.02f, 1) / 100; float S[3] = { r, r, r }, tn[4] = { 0.3f, 0.5f, 1, 0.25f };
			sk_mdraw(SKM_DOME, VP, O->pos, now, S, NULL, tn, 1, 0, 8, fogc, NULL); }
	}
	for (int a = 0; a < SK_MAXACT; a++) if (sk_is_active(a, SK_PLASMASHIELD) && SKA[a].toggled && sk_alive(a)) {
		SkillActor *A = &SKA[a];
		if (sk_ti(a) == 2) { float S[3] = { 2.9f, 2.9f, 2.9f }, tn[4] = { 0.35f, 0.85f, 1, 0.35f }; sk_mdraw(SKM_DOME, VP, A->dome, 0, S, NULL, tn, 1, 0, 8, fogc, NULL); }
		else if (a != sk_local) { V3 c, f, r; float hw, hh; sk_shield_frame(a, &c, &f, &r, &hw, &hh); float S[3] = { sk_ti(a) == 0 ? 0.95f : 1, 1, sk_ti(a) == 0 ? 0.66f : 1 }, tn[4] = { 0.4f, 0.9f, 1, 0.5f };
			sk_mdraw(SKM_SHIELD, VP, c, sk_yaw(a) + 1.5707963f, S, NULL, tn, 1, 0, 8, fogc, NULL); }
	}
	// missiles
	for (int m = 0; m < SK_MAXMS; m++) { SkMissile *M = &SKMS[m]; if (!M->on || M->delay > 0) continue; V3 d = sk_norm(M->v);
		float rz = atan2f(d.y, d.x) + 1.5707963f, S[3] = { M->kind ? 2.5f : 1, M->kind ? 0.6f : 1, M->kind ? 2.5f : 1 };
		V3 p = vsub(M->p, vmul(d, 20.5f * S[1]));   // the mesh lies at y -29..-12 (model -y = forward): centre it on the missile
		sk_mdraw(SKM_MISSILE, VP, p, rz, S, NULL, WHITE, 0, 0, 8, fogc, NULL); }
	// heat ghosts of enemies revealed to our team (through walls)
	if (sk_mok[SKM_GHOST]) { glDisable(GL_DEPTH_TEST);
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != lt && SKA[b].heat_t[lt & 1] > 0) {
			V3 v = sk_vel(b); int mv = v.x * v.x + v.y * v.y > 50 * 50; float al = sk_clamp(SKA[b].heat_t[lt & 1] / 1.6f, 0, 1), tn[4] = { 1, 0.32f, 0.12f, al };
			sk_mdraw(SKM_GHOST, VP, sk_pos(b), sk_yaw(b) + 1.5707963f, NULL, NULL, tn, 1, mv ? 0 : 1, mv ? 1 : 2, fogc, NULL); }
		glEnable(GL_DEPTH_TEST); }
	// sprites (fx atlas cells, additive): rings, beams, pillars, sectors (depth tested)
	if (fx_ok && fx_tex) {
		for (int o = 0; o < SK_MAXOBJ; o++) { SkObj *O = &SKO[o]; if (!O->alive) continue; int ally = O->team == lt;
			if (O->type == SKO_TURRET && O->phase == 1 && O->target >= 0) { const float col[4] = { ally ? 0.3f : 1, ally ? 0.9f : 0.2f, ally ? 1 : 0.15f, 0.8f };
				sk_beam_q(VP, sk_turret_att(o, "laser"), sk_target_pos(O->target), 4, cam, sk_cell[SKC_BEAM], col); }
			if (O->type == SKO_TURRET && (O->phase == 0 || O->t < 4)) { const float col[4] = { 0.3f, 0.8f, 1, 0.35f };
				sk_arc(VP, vadd(O->pos, v3(0, 0, 3)), 250, O->yaw0, SK_DEF[SK_SENTRYGUN].p1[O->tier < 1 ? 0 : O->tier > 3 ? 2 : O->tier - 1], col); }
			if (O->type == SKO_DRONE && O->target >= 0 && O->phase == 1) { const float col[4] = { 1, ally ? 0.45f : 0.15f, ally ? 0.3f : 0.1f, 0.95f };
				sk_beam_q(VP, vadd(O->cur, v3(0, 0, 11)), sk_target_pos(O->target), 10, cam, sk_cell[SKC_BEAM], col); }
			if (O->type == SKO_RECON && O->phase == 1) { float k = fmodf(O->t, 2.4f) / 2.4f; const float col[4] = { 0.3f, 0.85f, 1, 0.5f * (1 - k) };
				sk_ground(VP, vadd(O->pos, v3(0, 0, 4)), O->r * k, 0, sk_cell[SKC_RING], col); }
			if (O->type == SKO_EMP && O->phase == 0) { const float col[4] = { 0.3f, 0.7f, 1, 0.45f }; sk_ground(VP, vadd(O->pos, v3(0, 0, 4)), O->r, now, sk_cell[SKC_RING], col); }
		}
		for (int a = 0; a < SK_MAXACT; a++) { if (!sk_alive(a)) continue; SkillActor *A = &SKA[a];
			if (sk_is_active(a, SK_HEATSCAN) && A->pulse_t < 1 && !sk_hostile(a, sk_local)) { const float col[4] = { 1, 0.45f, 0.2f, 0.7f * (1 - A->pulse_t) };   // the scan front: a band sweeping out to 40 m
				sk_band(VP, vadd(sk_pos(a), v3(0, 0, -50)), 4000 * A->pulse_t + 50, 350, 32, sk_cell[SKC_HEAT], col); }
			if (sk_is_active(a, SK_METABOOST) && A->t < 1.2f) { const float col[4] = { 0.85f, 1, 1, 0.8f * (1 - A->t / 1.2f) };
				for (int b = 0; b < SK_MAXACT; b++) if (b != a && (A->linked >> b & 1) && sk_alive(b)) sk_beam_q(VP, sk_chest(a), sk_chest(b), 4, cam, sk_cell[SKC_BEAM], col);
				const float rc[4] = { 0.3f, 0.9f, 1, 0.6f * (1 - A->t / 1.2f) }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 5)), 1000 * A->t / 1.2f + 10, 0, sk_cell[SKC_RING], rc); }
			if (sk_is_active(a, SK_CYBERDIVE)) { const float col[4] = { 0.3f, 0.9f, 1, 0.7f }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 4)), 70, now * 1.5f, sk_cell[SKC_DRING], col);
				const float c2[4] = { 0.2f, 0.6f, 0.8f, 0.3f }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 3)), 60, 0, sk_cell[SKC_DISK], c2); }
		}
		if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && sk_alive(sk_local)) { SkillActor *A = &SKA[sk_local]; const float *hc = A->aim_ok ? HOLO : HOLO_BAD;
			float col[4] = { hc[0], hc[1], hc[2], 0.7f };
			if (A->skill == SK_DMDDRONE) { sk_pillar_q(VP, A->aim_pt, 600, 40, cam, col); sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 3)), 110, now, sk_cell[SKC_DISK], col); }
			else if (A->skill != SK_ARMLAUNCHER) { float fr = A->skill == SK_SENTRYGUN ? 70 : A->skill == SK_EMP ? sk_p1(sk_local) : A->skill == SK_SPECIALRECON ? 60 : 40;
				sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 3)), fr, now, sk_cell[A->skill == SK_EMP ? SKC_RING : SKC_DRING], col);
				if (A->skill == SK_SENTRYGUN) { const float c2[4] = { 0.3f, 0.8f, 1, 0.3f }; sk_arc(VP, vadd(A->aim_pt, v3(0, 0, 4)), 250, sk_yaw(sk_local), sk_p1(sk_local), c2); }
				if (A->skill == SK_SPECIALRECON) { const float c2[4] = { 0.3f, 0.85f, 1, 0.25f }; sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 4)), sk_p1(sk_local), 0, sk_cell[SKC_RING], c2); } } }
		sprite_flush_mode(VP, fx_tex, 1);
		// markers through walls: recon / wall marks (kal), lock rings, dive targets
		glDisable(GL_DEPTH_TEST);
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != lt) {
			if (SKA[b].mark_t[lt & 1] > 0) { const float col[4] = { 1, 0.25f, 0.2f, sk_clamp(SKA[b].mark_t[lt & 1], 0, 1) }; sk_marker(VP, vadd(sk_pos(b), v3(0, 0, 220)), 8, 25, cam, R, U, col); }
			if (sk_is_active(sk_local, SK_CYBERDIVE) && sk_len(vsub(sk_chest(b), SKA[sk_local].cam)) < 2000) { const float col[4] = { 1, 0.15f, 0.15f, 0.9f }; sk_bill(VP, sk_chest(b), 30, 30, R, U, sk_cell[SKC_RING], col); }
		}
		if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && SKA[sk_local].skill == SK_ARMLAUNCHER)
			for (int k = 0; k < SKA[sk_local].nlocks; k++) { const float col[4] = { 1, 0.2f, 0.15f, 0.95f }; float s = 32 + 6 * sinf(now * 9 + k);
				sk_bill(VP, sk_target_pos(SKA[sk_local].locks[k]), s, s, R, U, sk_cell[SKC_DRING], col); }
		sprite_flush_mode(VP, fx_tex, 1);
		glEnable(GL_DEPTH_TEST);
	}
	glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
// full-screen layers of the local player's skill state: after the 3D (and the viewmodel), before the HUD
static void sk_ovl_quad(int cell, float x0, float y0, float x1, float y1, const float *col, float du, float dv) {   // clip-space rectangle
	const float *u = SKV_UV[cell]; float a[3] = { x0, y1, 0 }, b[3] = { x1, y1, 0 }, c[3] = { x1, y0, 0 }, d[3] = { x0, y0, 0 };
	sprite_quad(a, b, c, d, u[0] + du, u[1] + dv, u[2] + du, u[3] + dv, col);
}
static void sk_draw_overlays(int a) {
	if (!sk_ok || !sk_ovl_tex || !sk_valid(a)) return;
	sp_n = 0;   // (nothing is queued at this point; never draw stale quads with the overlay matrix)
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	SkillActor *A = &SKA[a]; float now = sk_now; int alive = sk_alive(a);
	glDisable(GL_DEPTH_TEST);
	// mul2x layers first
	int nmul = 0;
	if (alive && sk_is_active(a, SK_CYBERDIVE)) { const float w[4] = { 1, 1, 1, 1 }; sk_ovl_quad(SKV_DIVE_MUL, -1, -1, 1, 1, w, 0, 0); nmul++; }
	if (SKV.heat > 0) { const float w[4] = { 1, 1, 1, 1 }; sk_ovl_quad(SKV_HEAT_MUL, -1, -1, 1, 1, w, 0, 0); nmul++; }
	if (nmul) sprite_flush_mode(I, sk_ovl_tex, 2);
	// additive layers
	if (SKV.camo > 0) { const float c[4] = { 1, 1, 1, SKV.camo * (0.85f + 0.15f * sinf(now * 3)) }; sk_ovl_quad(SKV_CAMO, -1, -1, 1, 1, c, 0, 0); }
	{ int lk = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_is_active(b, SK_METABOOST) && SKA[b].t < 7.5f && (SKA[b].linked >> a & 1)) lk = 1;   // linked to any Borma
	  if (alive && lk) { float pu = 0.6f + 0.4f * (0.5f + 0.5f * sinf(now * 12.566f)); const float c[4] = { 1, 1, 1, pu }; sk_ovl_quad(SKV_META, -1, -1, 1, 1, c, 0, 0); } }
	if (alive && sk_is_active(a, SK_PLASMASHIELD) && A->toggled && sk_ti(a) < 2) { const float c[4] = { 1, 1, 1, 0.55f + 0.45f * A->energy }; sk_ovl_quad(SKV_SHIELD, -1, -1, 1, 1, c, 0, 0); }
	if (SKV.shield_hit > 0) { const float c[4] = { 1, 1, 1, SKV.shield_hit / 0.2f }; sk_ovl_quad(SKV_SHIELD_HIT, -1, -1, 1, 1, c, 0, 0); }
	if (alive && sk_is_active(a, SK_RUSH) && A->dashing) { float s = 1 + 0.15f * (1 - A->energy); const float c[4] = { 1, 1, 1, 0.9f }; sk_ovl_quad(SKV_RUSH, -s, -s, s, s, c, 0, 0); }
	if (alive && sk_is_active(a, SK_CYBERDIVE)) { const float c[4] = { 1, 1, 1, 0.85f }; sk_ovl_quad(SKV_DIVE, -1, -1, 1, 1, c, 0, 0); }
	if (SKV.vig > 0) { float k = SKV.vig_len > 0 ? sk_clamp(SKV.vig / SKV.vig_len, 0, 1) : 0; const float c[4] = { SKV.vig_c[0], SKV.vig_c[1], SKV.vig_c[2], 0.3f + 0.7f * k }; sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); }
	if (SKV.glitch > 0) { float k = SKV.glitch_len > 0 ? sk_clamp(SKV.glitch / SKV.glitch_len, 0, 1) : 0; const float c[4] = { SKV.glitch_c[0], SKV.glitch_c[1], SKV.glitch_c[2], 0.5f + 0.5f * k };
		float j = (sk_rnd() - 0.5f) * 0.08f, jv = (sk_rnd() - 0.5f) * 0.04f; sk_ovl_quad(SKV_GLITCH, -1 + j, -1 + jv, 1 + j, 1 + jv, c, 0, 0); }
	if (SKV.hlines > 0 && SKV.glitch <= 0.05f) { const float c[4] = { 1, 0.35f, 0.3f, sk_clamp(SKV.hlines / 0.9f, 0, 1) }; float s = fmodf(now * 0.6f, 1) * 0.1f;
		sk_ovl_quad(SKV_HLINES, -1, -1 + s, 1, 1 + s, c, 0, 0); }
	if (SKV.laugh > 0 && alive) { float hy = 0.8f, hx = hy * 544 / 960; /* the logo fills ~45 % of its cell: ~35-40 % of the screen height */ float al = sk_clamp(SKV.laugh / 0.3f, 0, 1); const float c[4] = { 1, 1, 1, al }; sk_ovl_quad(SKV_LAUGH, -hx, -hy, hx, hy, c, 0, 0); }
	if (SKV.flash > 0) { const float c[4] = { 1, 1, 1, sk_clamp(SKV.flash / 0.2f, 0, 1) }; sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); }
	sprite_flush_mode(I, sk_ovl_tex, 1);
	glEnable(GL_DEPTH_TEST);
}
// HUD additions between hud_begin and hud_end: the active-skill bar under the crosshair with a small copy of the icon above it
// (footage 1280x720 bar 608..673 x 593..600 -> Vita 456..505 x 445..450), the AIM prompt, the "EMP" / "HACKED" state
static void skills_hud(int a, const float *VP, float now) {
	(void)VP; if (!sk_ok || !sk_valid(a) || !HG.ok || HG.white < 0 || HG.screen < 0 || !sk_alive(a)) return;
	SkillActor *A = &SKA[a];
	if (A->bar >= 0) {
		float B0[6] = { 49, 0, 456, 0, 5, 445 }, B1[6] = { 49 * sk_clamp(A->bar, 0, 1), 0, 456, 0, 5, 445 };
		static const float bg[4] = { 0, 0, 0, 0.35f }, fill[4] = { 0.33f, 0.78f, 0.95f, 0.95f };
		hud_img(HG.white, HG.screen, B0, bg, HUD_Z4, -1, 1); if (A->bar > 0) hud_img(HG.white, HG.screen, B1, fill, HUD_Z4, -1, 1);
		char b[16]; snprintf(b, 16, "skill.%d", SK_DEF[A->skill].hud_icon); int im = hud_find_img(b);
		if (im >= 0) { const HImg *I = &himg[im]; float w = I->loc[2] - I->loc[0], h = I->loc[3] - I->loc[1], s = 15 / fmaxf(1, fmaxf(w, h)), cx = (I->loc[0] + I->loc[2]) * 0.5f, cy = (I->loc[1] + I->loc[3]) * 0.5f;
			float A2[6] = { s, 0, 480.5f - cx * s, 0, s, 434.5f - cy * s }; static const float ic[4] = { 0.55f, 0.92f, 1, 0.95f }; hud_img(im, HG.screen, A2, ic, HUD_Z4, -1, 1); }
	}
	static const float wm[4] = { 1, 1, 1, 0.9f }, rd[4] = { 1, 0.35f, 0.3f, 0.95f };
	const char *msg = NULL; char buf[48];
	if (A->state == SKS_AIM) {
		if (A->skill == SK_ARMLAUNCHER) { snprintf(buf, sizeof(buf), "R: FIRE  %d LOCK%s", A->nlocks, A->nlocks == 1 ? "" : "S"); msg = buf; }
		else msg = A->aim_ok ? "R: DEPLOY   RIGHT: CANCEL" : "CAN'T DEPLOY HERE";
	} else if (sk_is_active(a, SK_CYBERDIVE)) msg = "R: DIVE IN   RIGHT: CANCEL";
	else if (sk_is_active(a, SK_PLASMASHIELD)) msg = A->toggled ? "RIGHT: DEACTIVATE SHIELD" : "RIGHT: REACTIVATE SHIELD";
	if (msg) hud_text(HG.screen, 480, 470, 11, 2, A->state == SKS_AIM && !A->aim_ok && A->skill != SK_ARMLAUNCHER ? rd : wm, HUD_Z4, msg);
	if (A->emp_t > 0 && fmodf(now, 0.5f) < 0.3f) hud_text(HG.screen, 480, 412, 13, 2, rd, HUD_Z4, "EMP");
	if (A->hack_t > 0) hud_text(HG.screen, 480, 412, 13, 2, rd, HUD_Z4, "HACKED");
}
