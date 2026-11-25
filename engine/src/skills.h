// Operator skills (GITS:SAC First Assault): the 12 operators' special abilities, for the player and the bots.
// Spec, numbers and sources: tools/skills/SKILLS.md (skill videos, gameplay footage, the game's effect/model/sound data).
// The economy is the FINAL build's (the one whose InGameUI.swf the port uses, SKILLS.md section 8):
//   Tier 1: always available, a cooldown counted from activation (Motoko / Batou 25 s, Reiko / Sitara 30 s).
//   Tier 2: needs the operator's Tier-2 intel (the big HUD ring: 400 / 500 / 600), spends it, stronger effect; while a Tier-2
//           skill or its deployable runs, both tiers are disabled. Same button: tap = Tier 1 (or Tier 2 when Tier 1 is not
//           available), hold 0.4 s = Tier 2 (a Tier 1 started by the press is upgraded, its cooldown refunded).
//   SkillSync: a shareable Tier-2 skill (camo, heat scan, metaboost, rush, plasma shield) links up to 3 nearby allies; an
//           ally who downloads it (interact near the sharer) gets the Tier-1 effect. Bots download and share too.
// Data: sk_*.gvm (deployables, every mesh rigid on one bone: one draw call per mesh, no CPU skinning), fx_sk.gfx
// (the SK_* effects and atlas cells, looked up BY NAME so base effect ids may shift), sk_ovl.gtx (screen overlays).
// The engine gives world access through SkWorld (actor 0 = the local player, 1 + i = bot i); nothing here reads main.c
// or bots.h. Never NaturalMotion, never GAME.exe: everything is reconstructed from the data files and footage.
// Cost on the Vita (Cortex-A9 444 MHz): logic ~0.05-0.15 ms/frame (fixed pools; map rays only at 5 Hz per deployable,
// per missile step and for the local placement preview), drawing: <= ~10 rigid draw calls, one or two sprite batches,
// at most ~3 full-screen overlay quads.
//
// State other modules may read (vision / HUD / bots): see "---- read-only state" and "---- vision accessors" below
// (sk_cloaked, sk_cloak_left, sk_cloak, sk_cloak_flags (SKCF_*), sk_cloak_shimmer, sk_cloak_break, sk_cloak_seen_by,
// sk_revealed, sk_reveal_left, sk_reveal_until, sk_revealed_by, sk_heat_pulse, sk_sync_link, sk_hud_info, sk_time).
// The SkillActor fields camo_close, heat_t[team], mark_t[team], hack_t, emp_t keep their meaning; heat_by[team] / mark_by[team]
// name the revealer.
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
#include "sk_shield_geo.h"   // Azuma's shield panels and break pieces (tools/skills/make_shield.py; databuilder: loaded from sk_shield.gsg)
#include "hud_game.h"

enum { SK_ARMLAUNCHER, SK_OPTICAL, SK_HEATSCAN, SK_SPECIALRECON, SK_SENTRYGUN, SK_METABOOST, SK_RUSH, SK_PLASMASHIELD,
	SK_EMP, SK_CYBERWALL, SK_CYBERDIVE, SK_DMDDRONE, SK_COUNT };                       // == CHARS[] / MENU_CHR order
enum { SKS_READY, SKS_AIM, SKS_ACTIVE, SKS_COOLDOWN };
enum { SKO_NONE, SKO_TURRET, SKO_WALL, SKO_DRONE, SKO_EMP, SKO_RECON, SKO_DECOY, SKO_SEEKER };   // (b84: Reiko's Tier 2 Net Jump hologram; b85: Togusa's Seeker Drones)
enum { SKD_BULLET, SKD_BLAST, SKD_MELEE, SKD_LASER, SKD_SHOCK };                        // damage kinds (SkWorld.damage)
enum { SKN_HIT, SKN_KILL, SKN_TEXT, SKN_POINTS };   // SkWorld.notify: hit marker, kill-style notice (+pts), plain notice, notice + points (no kill marker)
#define SK_MAXACT 9
#define SK_MAXOBJ 16
#define SK_MAXMS 16
#define SK_TGT_OBJ 100      // target encoding: 0..SK_MAXACT-1 an actor, SK_TGT_OBJ + o a deployable, -1 none
#define SK_TGT_RANGE 200    // b76: SK_TGT_RANGE + i = the shooting range's target i (the hooks below)
// b76 (user: Batou's arm cannon went through the shooting range's targets, nothing happened to them or round them): the
// range's targets for the launcher's locks, its missiles' contact and blast (main.c: range.h); NULL outside a game
static int (*sk_rg_n)(void); static int (*sk_rg_up)(int i); static V3 (*sk_rg_chest)(int i);
static int (*sk_rg_hit)(V3 o, V3 d, float maxd, float *t); static void (*sk_rg_hurt)(int i, float dmg, V3 hp, V3 d); static void (*sk_rg_blast)(V3 c, float r, float dmg);
#define SK_HOLD_T2 0.4f     // hold the skill button this long for Tier 2
#define SK_LINK_R 2000.f    // SkillSync: white link lines to allies within 20 m of the sharer
#define SK_SYNC_R 800.f     // SkillSync: download distance (the interact prompt)
#define SK_SYNC_MAX 3       // up to 3 allies can SkillSync one skill
#define SK_SYNC_PTS 20      // "SKILLSYNC COMPLETED +20" for the sharer
#define SK_CLOAK_IN 0.3f    // camo forms this fast after the press (footage, final build: no gesture, the haze and the chrome gun
                            // are there 0.1-0.3 s after the bar appears; Neon Ruins 12.1 / 49.5 s)
#define SK_CLOAK_OUT 0.6f   // ... and dissolves this slowly at the end (the white haze fades over ~0.6 s; Neon 17.2 -> 17.8 s)
#define SK_CLOAK_DEATH 0.25f// on death the body loses the cloak this fast (Neon 318.6 s: the kill cam shows her still chrome)
#define SK_SHIM_IN 0.67f    // the forming shimmer (effect camouflage_start.nif: 0.67 s)
#define SK_SHIM_OUT 0.5f    // the dissolving shimmer (camouflage_end.nif: 0.5 s)
// Batou's arm launcher, footage (Electronics Center 36.9-38.7 / 42.9-44.8 s at 10 fps) + the clip lengths of vm_armlauncher.gvm:
#define SK_AL_IN 1.37f      // press -> the gun drops, the arm comes up and unfolds into the launcher (clip swap_end, 41 frames)
#define SK_AL_SWITCH 0.65f  // ... the arm dissolves into the launcher this far into it (37.9-38.0 s: the chrome flash)
#define SK_AL_FIRE 0.3f     // the shot (fire clip) before the launcher folds back
#define SK_AL_OUT 0.95f     // the launcher folds back into the arm and drops (clip swap_start; the arm is back at 0.35 s, gone at 0.75)
#define SK_AL_BACK 0.35f    // ... the launcher dissolves back into the arm this far into it
#define SK_CLOAK_HIT 1.4f   // a hit while cloaked: the electric flicker (camouflage_attack.nif: 1.4 s)
#define SK_CLOAK_FIRE 0.3f  // a shot fired while cloaked shows the body this long (fading)

typedef struct {
	const char *id, *name, *desc;     // voice token ("using_<id>", "op_<id>_ready"), menu strings
	int hud_icon, menu_icon;          // hud.ghud skill.N, GameLoading skillIconMc frame or -1
	int placed;                       // 1: AIM shows a placement hologram, 2: a target point (drone)
	float cool1; int cost2, share;    // Tier-1 cooldown (s, counted from activation), Tier-2 intel cost (HUD ring max), Tier 2 SkillSync-able
	float duration[3];                // [0] Tier 1, [1] Tier 2, [2] a SkillSync download (0 = what is left of the sharer's Tier 2, >= Tier 1)
	float p1[3], p2[3];               // per-skill values, same indexing (see the table)
	float range;                      // placement range (cm)
} SkillDef;
// p1 / p2: launcher locks / missile damage; camo near-reveal distance; heat pulses / radius; recon radius / HP; sentry arc (deg) / HP;
// metaboost HP cap (x max) / heal total (HP); rush dash drain per s / idle drain; shield HP-equivalent / idle drain; EMP radius / shock s;
// wall width / height; dive hack s / dive range; drone laser dps / HP. [M] measured in footage, [E] estimates (DB.dat is encrypted).
static const SkillDef SK_DEF[SK_COUNT] = {
	{ "armlauncher", "ARM LAUNCHER", "Fire a missile from the launcher hidden in your arm.", 12, -1, 0,   // b84: v2.0 text (EP16)
	  25, 500, 0, { 8, 10, 0 }, { 1, 4, 0 }, { 150, 120, 0 }, 0 },                          // [M] 25 s, 500, one rocket at Tier 1
	{ "optical", "THERM-OPTIC CAMO", "An active camo layer that makes you nearly invisible.", 8, 0, 0,   // b84: v2.0 text (EP15)
	  25, 400, 1, { 5, 44, 30 }, { 400, 400, 400 }, { 0, 0, 0 }, 0 },                      // [M] 25 s, 400, 5 s / 44 s (21 + 9 cycles in 3 videos), shared: 30 s (Batou, Electronics Center 296-326)
	{ "heatscan", "HEAT SENSOR", "Uplink to a satellite and see enemies through walls.", 3, 1, 0,   // b83: v2.0 (Saito video EP20, its DETAILS text)
	  20, 400, 1, { 4.3f, 35.3f, 0 }, { 4, 5, 5 }, { 4000, 6000, 6000 }, 0 },              // [M EP20] T1 4 scans 1 s apart (3:08.9, 20 s cooldown), T2 5 scans 7 s apart (6:16.5-6:51, 9:35), shared = the rest of it
	{ "specialrecon", "SEEKER DRONE", "A hunter drone that explodes on contact with an enemy.", 13, -1, 0,   // b85: v2.0 (Togusa video EP17; was v1 SPECIAL RECON)
	  30, 500, 0, { 1.75f, 2.05f, 0 }, { 1, 3, 0 }, { 70, 70, 0 }, 0 },                     // [M EP17] cooldown 30 s (3:00 -> 28 at 3:02); p1 drones, p2 blast damage; (b117, the game's own skill video SUMMON.usm at 30 fps: the press at frame 21, the gesture 27-57, the drone on the floor at 69 = 1.6 s after the press; was ~0.5 s)
	
	{ "sentrygun", "CYBER SENTRY", "A sentry that targets any enemies in range.", 1, 7, 1,   // (b85: Tier 2 also sees farther, SK_SENT_RANGE)   // b84: v2.0 name (EP18)
	  30, 600, 0, { 20, 40, 0 }, { 180, 360, 0 }, { 300, 450, 0 }, 300 },
	{ "metaboost", "NANO-GEL ARMOR", "Repair gel that boosts and regenerates your HP and armor.", 2, 2, 0,   // b83: v2.0 (Borma video EP21)
	  24, 400, 1, { 3, 15, 0 }, { 1.0f, 1.91f, 1.91f }, { 1.0f, 2.21f, 2.21f }, 0 },       // [M EP21] p1 / p2 = HP / AP max x (220 -> 420, 165 -> 365 at 6:06-6:22)
	{ "rush", "HYPER SPRINT", "Boost your speed and jumps, and reach further with your knife.", 9, 3, 0,   // b79: v2.0's HYPER SPRINT (Paz video EP19 /
	  25, 400, 1, { 5.5f, 8.0f, 5.5f }, { 0.18f, 0.125f, 0.18f }, { 0.18f, 0.125f, 0.18f }, 0 },        // the game's DETAILS text): was v1's contact kill; a fixed 5.5 s / 8 s (EP19 5:09.3-5:14.8)
	{ "plasmashield", "TACTICAL SHIELD", "Raise an energy shield that stops attacks from the front.",   // b87: the v2.0 name, as the operative screen (EP24) - was v1 PLASMA SHIELD in game
	  17, -1, 0,
	  30, 500, 1, { 9, 22, 0 }, { 400, 700, 300 }, { 0, 0, 0 }, 0 },                         // [M] Shield.usm: T1 9 s (1 s bar steps), T2 ~22 s; HP [E]
	{ "emp", "EMP GENERATOR", "Disables the skills and electronics of nearby enemies.", 16, -1, 1,   // b85: v2.0 (Kuro video EP25 + the user's guides; was v1 EMP SHOCKWAVE)
	  25, 500, 0, { 4, 4, 0 }, { 500, 2500, 0 }, { 4, 7, 0 }, 700 },                        // [M EP25] cooldown 25 s (2:41.8 -> 24); duration = the fuse ("EMP Spark: -50 % (2 s)"); p1 blast radius (T2 "a large portion of the map"), p2 stun s
	{ "cyberwall", "THERM-OPTIC BARRIER", "A therm-optic wall that blocks sight and slows bullets.", 5, 6, 1,   // b83: v2.0 (Maven video EP22)
	  30, 500, 0, { 20, 30, 0 }, { 300, 500, 0 }, { 200, 300, 0 }, 700 },
	{ "cyberdive", "NET JUMP", "Hack the eyes watching you and move unseen.", 18, -1, 0,   // b84: v2.0 (Reiko video EP26)
	  30, 400, 0, { 3, 6, 0 }, { 1.2f, 1.6f, 0 }, { 40, 40, 0 }, 0 },                       // [M EP26] T1 3 s (4:43-4:45.5, 1 s bar steps), T2 ~6 s (7:16-7:22), cooldown 30 s; p1 = the watchers' blind s, p2 = their view cone (deg)
	{ "dmddrone", "SIPHON DRONE", "A drone that scorches nearby enemies and heals you a little.", 19, -1, 2,   // b83: v2.0 (Sitara video EP23)
	  30, 600, 0, { 20, 100, 0 }, { 20, 30, 0 }, { 150, 250, 0 }, 3000 },                  // [M] 30 s, 600, Tier-2 drone ~100 s; [M EP23] Tier 1 20 s (2:50.5-3:10.5, 1 s bar steps)
};
// operator stems (chr_<stem>.gvm, voice events voice/action/<stem>/...), same order
// b83 (v2.0, the Saito video EP20): seconds between Heat Sensor scans - Tier 1 four in 4 s (the bar steps each second), Tier 2
// (and a SkillSync download of it) five 7 s apart over 35 s (the remaining scans as dots under the bar)
static const float SK_HS_GAP[3] = { 1.0f, 7.0f, 7.0f };
// b83 (v2.0, the Borma video EP21): Nano-Gel Armor regeneration, HP / AP per second (of a full 100): Tier 1 3 s (4:51.5: HP 14 -> 153
// of 220 and AP 52 -> 165 of 165 in ~2.5 s under fire), Tier 2 faster ("increases the regeneration speed"), its higher maxima fill in ~3 s
static const float SK_NG_HP[3] = { 25, 30, 30 }, SK_NG_AP[3] = { 33, 40, 40 };
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
	const char *(*name)(int a);                      // display name ("Batou") for the SkillSync slots (NULL: the operator's name)
	V3    (*muzzle)(int a);                          // b52: the held gun's muzzle (world, last pose; bots), NULL: none
	void  (*armor)(int a, float add, float cap_mul); // b83 (Nano-Gel Armor): the same for AP (max 100); NULL: no armour (last: positional initialisers stay valid)
} SkWorld;

typedef struct {
	int on, skill, team, tier, state;          // tier: 1 / 2 of the current (or last) activation
	float t, cool, energy, bar;                // t: time in the state; cool: Tier-1 cooldown left (counts from activation)
	int obj, nlocks, locks[4]; float lock_t;   // deployable slot; arm-launcher locks
	int pulses; float pulse_t;                 // heat scan
	int toggled; V3 dome;                      // plasma shield (on/off)
	float sh_t, sh_hit; signed char sh_ph, sh_up, sh_big;   // shield look: phase (0 none, 1 raise, 2 up, 3 lowered, 4 broken) and its time, hit noise
	float sh_fwd;                              // b52: the panel's distance in front of the eye (third person: clear of the gun's muzzle)
	V3 cam; float dive_still;                  // (v1 cyber dive ghost camera; b84: cam = where the Net Jump started)
	unsigned linked; float link_t, tick_t;     // metaboost
	float fx_t, loop_t;                        // effect / sound loop timers
	int dashing;                               // rush (own or downloaded)
	// economy
	int intel, intel_in, t2_note;              // Tier-2 intel (capped at the cost), last engine total seen, ready notice given
	float btn_hold, pre_cool; int btn_up;      // hold-to-upgrade (Tier 1 -> Tier 2) of the press that started it
	float post_t;                              // arm launcher: the arm folds back after the shot
	// SkillSync: sharer side (while a shareable Tier 2 runs)
	unsigned synced; int nsync; signed char sync_who[SK_SYNC_MAX];
	// SkillSync: receiver side (a downloaded Tier-1 effect)
	int sx_skill, sx_from; float sx_t, sx_len, sx_age, sx_energy, sx_pulse_t, sx_tick_t, sync_wait; int sx_pulses;
	// received effects
	float hack_t, emp_t, shock_t, heat_t[2], mark_t[2], warn_t, laser_t, fire_t, hit_ago, camo_close;   // hit_ago: s since the last damage
	float stun_t;                              // b85: an EMP Generator's stun (slow walk, the view barely turns; emp_t keeps the skills off)
	float blind_t;                             // b117: blinded by a Seeker Drone's flash (a bot sees no enemy; the local player: SKV.blind)
	// cloak look state (vision accessors sk_cloak*): amount 0..1 ramped in / out, time since the last on / off change, hit flicker
	float cloak_k, cloak_t, cloak_hit_t; int cloak_was;
	signed char heat_by[2], mark_by[2];        // who revealed this actor to team k (-1)
	// bookkeeping
	float last_hp; int was_alive, warned, sh_alarm;   // sh_alarm (b63): the shield's running-out alarm played this activation
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
	int clip; float clip_t;                       // the model's own clip (deploy / idle / fire / damage) and its time
	unsigned char ser;                            // net: a serial per placement (the snapshot mirror tells a new one from the old)
	float off_t;                                  // b85: switched off by an enemy EMP Generator for this long (no firing / scanning)
} SkObj;
typedef struct { int on, owner, team, kind, target; V3 p, v; float t, delay, trail_t, dmg; unsigned char ser; V3 p0, v0; float delay0; } SkMissile;   // kind 0 missile, 1 turret grenade (ser, p0 / v0 / delay0: net, its launch)

static V3 sk_shield_origin(int a);
static SkWorld SKW_; static SkillActor SKA[SK_MAXACT]; static SkObj SKO[SK_MAXOBJ]; static SkMissile SKMS[SK_MAXMS];
static int sk_ok, sk_local = 0, sk_frame; static float sk_now;
static int sk_uses[SK_MAXACT];   // b58: skills used this match per actor (a Tier 2 counts 2): the results' TECHNICIAN award, the skill mission
static int sk_bots_use = 1;          // bots use their skills
static int sk_blur_req;              // b102: an ability of the local player started this frame (main.c hands it to ablur.h and clears it)
static int sk_net_view;              // b84: main.c - the world is drawn in the network view this frame (the Net Jump's own darkening steps back)
// b121 (the user: "make sure that reiko's net jump work properly"): the PC's own screen scenes of the jump (in effect.dat's
// screen folder: screen_dive_start.nif, 0.67 s: a fan of cyan light streaks and a white plane as she goes in - EP26 4:43.0; screen_dive.nif
// from its 1.0 s on, 1.2 s: the streaks for 0.57 s over the still dark world with the gun already back, then the white plane
// - 4:45.5-4:46.3, 7:22-7:23), baked by tools/fx/make_scene.py into sk_nj_in.gsc / sk_nj_out.gsc (flat scenes in their xz
// plane, no camera node: seen from 204.6 cm before them, the dive scenes' own distance and lens). sk_nj_hold: the network
// view kept on that long after the jump ended (main.c).
static FxScene sk_nj_sc[2]; static int sk_nj_play = -1; static float sk_nj_t, sk_nj_hold;
#define SK_NJ_HOLD 0.57f
// net (net_game.h): sk_manual = actors driven by a remote player's buttons (host: no bot brain); sk_mirror = this machine shows the
// host's skills (client): the actors' states, deployables and missiles come from the snapshots, so the local simulation only animates
// and sounds them (no damage, no endings of its own); sk_mirror_ok while the net code applies the host's state. All 0 offline.
static unsigned sk_manual; static int sk_mirror, sk_mirror_ok; static unsigned char sk_ser_n;
#define SK_MIRRORED (sk_mirror && !sk_mirror_ok)
enum { SK_DRAW_HAZE = 1, SK_DRAW_HEATGHOST = 2, SK_DRAW_MARKS = 4 };
static int sk_draw_mask = 7;         // what skills.h draws itself: the vision module clears the bits of what it takes over
                                     // (the camo haze overlay, the heat ghosts, the through-wall kal marks of revealed enemies)
static struct { float camo, glitch, glitch_len, hlines, flash, vig, vig_len, heat, laugh, warn_cd, t2n, sync_note, seek_t, blind, blind_len; float glitch_c[3], vig_c[3]; int sync_from; } SKV;   // (b85 seek_t: "SEEKER DRONE INCOMING")
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

#define SK_API static __attribute__((unused))   // read-only API other modules may or may not call in a given build
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
static int sk_synced(int a, int skill) { return sk_valid(a) && SKA[a].sx_skill == skill && SKA[a].sx_t > 0; }   // a downloaded (SkillSync) effect runs
static int sk_has(int a, int skill) { return sk_is_active(a, skill) || sk_synced(a, skill); }                       // own or downloaded
static const char *sk_name(int a) { const char *n = SKW_.name && sk_valid(a) ? SKW_.name(a) : NULL; return n ? n : sk_valid(a) ? SK_DEF[SKA[a].skill].name : "?"; }

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
static void sk_voice_line(int a, const char *pfx) {   // a RAM voice line ("d_<stem>_share") from actor a: panned, quieter with distance
	if (a == sk_local) { voice_play(pfx, 220, 0); return; }
	V3 p = sk_pos(a); float dx = p.x - snd_lis[0], dy = p.y - snd_lis[1], d = sqrtf(dx * dx + dy * dy); if (d > 3500) return;
	voice_play(pfx, (int)(220 * (1 - 0.7f * d / 3500)), (dx * sinf(snd_lis_yaw) - dy * cosf(snd_lis_yaw)) / (d + 1) * 0.8f);
}
static const char *const SK_ALARM[SK_COUNT] = { "armlauncher", "cloaking", "heat_scan", "mini_tachikoma", "turret", "metabolism", "rush", "shield", "emp", "screen", "cyberdive", "dmd" };
static const char *const SK_HUDON[SK_COUNT] = { "cloaking", "cloaking", "heat_scan", "noise_scan", "turret_hacking", "metabolism", "fast_run", "cloaking", "cloaking", "cloaking", "cyberdive", "cloaking" };
// ---- skill messages in the commander box (the "donut": portrait, name, text; top left). Late-2016 footage, every message of
// n2 / n6 / n7 / n8 read (SKILLS.md 8.3): an ally's use = his portrait + his name + "An ally activated Arm Launcher!" / "An ally
// deployed a Cyber Sentry!" / "An ally engaged Therm-optic Camo."; an enemy skill = the Operator (cm.face.12) + "Enemy Therm-optic
// Camo user detected!" / "Enemy Cyber Sentry detected!"; own Tier 2 ready = the Operator + "Therm-optic Camo ready."; SkillSync =
// the ally's portrait + "Share code transmission complete." (he downloaded yours) / "Transfer complete." (you downloaded his).
// The engine draws the head of the queue with menu_commander_draw (sk_msg). Shown 5.45 s (the box's own fade); a newer message
// replaces one shown >= 1.5 s (footage: two ally messages 3 s apart), else waits (at most 3 waiting).
static const unsigned char SK_FACE[SK_COUNT] = { 2, 1, 3, 6, 5, 7, 4, 15, 9, 8, 16, 17 };   // (b103: Azuma 15, Kuro 9, Maven 8, Reiko 16, Sitara 17 - menu.h's FACE_* note)   // cm.face.N: Paz = 4 (n2 340 s), Togusa = 6
static const char *const SK_MNAME[SK_COUNT] = { "Arm Launcher", "Therm-optic Camo", "Heat Sensor", "Seeker Drone", "Cyber Sentry",
	"Nano-Gel Armor", "Hyper Sprint", "Tactical Shield", "EMP Generator", "Therm-optic Barrier", "Net Jump", "Siphon Drone" };   // (b83 / b84: v2.0 names)   // footage names where seen
enum { SK_MSG_N = 4, SK_FACE_OPERATOR = 12 };
#define SK_MSG_SHOW 5.45f
typedef struct { int face; char name[24], text[64]; } SkMsg;
static SkMsg sk_mq[SK_MSG_N]; static int sk_mq_n; static float sk_mq_t;
static void sk_msg_post(int face, const char *name, const char *text) {
	if (sk_mq_n == 1 && sk_mq_t >= 1.5f) sk_mq_n = 0;
	if (sk_mq_n == 0) sk_mq_t = 0;
	if (sk_mq_n >= SK_MSG_N) return;
	SkMsg *m = &sk_mq[sk_mq_n++]; m->face = face; snprintf(m->name, sizeof(m->name), "%s", name ? name : ""); snprintf(m->text, sizeof(m->text), "%s", text);
}
static void sk_msg_step(float dt) {
	if (sk_mq_n <= 0) return; sk_mq_t += dt;
	if (sk_mq_t >= SK_MSG_SHOW || (sk_mq_n > 1 && sk_mq_t >= 2.5f)) { memmove(sk_mq, sk_mq + 1, (size_t)(sk_mq_n - 1) * sizeof(SkMsg)); sk_mq_n--; sk_mq_t = 0; }
}
static int (*sk_force_cloak)(int a);   // b63 (Ghost Assault, ga.h): 1 = actor a is cloaked by the mode (the ghosts), not by a skill
static int (*sk_grip_decloak)(int a);  // b70 (Ghost Assault, ga.h): 1 = actor a is in a cyborg's grip: its cloak drops at once (IGN 252.5 s)
static void sk_msg_ally(int a) {   // ally a (not the player) used his skill
	if (a == sk_local || !sk_valid(a) || !sk_valid(sk_local) || sk_hostile(a, sk_local)) return;
	if (SKA[a].skill == SK_OPTICAL && sk_force_cloak && sk_force_cloak(a)) return;   // b63 (Ghost Assault): the ghosts are cloaked anyway
	int s = SKA[a].skill; char t[64]; const char *nm = SK_MNAME[s];
	if (s == SK_OPTICAL) snprintf(t, sizeof(t), "An ally engaged %s.", nm);
	else if (SK_DEF[s].placed) snprintf(t, sizeof(t), "An ally deployed %s %s!", strchr("AEIOU", nm[0]) ? "an" : "a", nm);
	else snprintf(t, sizeof(t), "An ally activated %s!", nm);
	sk_msg_post(SK_FACE[s], sk_name(a), t);
}
static void sk_msg_enemy(int s) {   // an enemy skill concerns the player (with sk_warn's voice)
	char t[64]; if (s == SK_RUSH) snprintf(t, sizeof(t), "Enemy Hyper-Sprint user detected!");   // (hyphenated in this message)
	else snprintf(t, sizeof(t), SK_DEF[s].placed ? "Enemy %s detected!" : "Enemy %s user detected!", SK_MNAME[s]);
	sk_msg_post(SK_FACE_OPERATOR, "Operator", t);
}
static void sk_msg_ready(int a) { if (a == sk_local && sk_valid(a)) { char t[64]; snprintf(t, sizeof(t), "%s ready.", SK_MNAME[SKA[a].skill]); sk_msg_post(SK_FACE_OPERATOR, "Operator", t); } }
// the message to show now (face = cm.face.N, t = seconds it has been up): 0 = none
static int sk_msg(int *face, const char **name, const char **text, float *t) {
	if (!sk_ok || sk_mq_n <= 0) return 0;
	*face = sk_mq[0].face; *name = sk_mq[0].name; *text = sk_mq[0].text; *t = sk_mq_t; return 1;
}
static void sk_warn(int skill) {   // an enemy skill concerns the local player: intel_skill/<x>_alarm (is_use_alarm) + d_op_enemy_<skill>, at most one per 6 s
	if (SKV.warn_cd > 0) return; SKV.warn_cd = 6;
	char n[64]; snprintf(n, sizeof(n), "intel_skill/%s_alarm", SK_ALARM[skill]); ev_play(n, NULL);
	snprintf(n, sizeof(n), "voice/donut/common/op_%s_enemy", SK_DEF[skill].id); radio_play(n);
	sk_msg_enemy(skill);
}

// ---- models
enum { SKM_TURRET, SKM_DRONE, SKM_EMP, SKM_MINI, SKM_WALLPOD, SKM_WALL, SKM_SHIELD, SKM_DOME, SKM_MISSILE, SKM_GHOST, SKM_N };
static const char *const SKM_FILE[SKM_N] = { "sk_turret", "sk_drone", "sk_emp", "sk_minitachi", "sk_wallpod", "sk_wall", "sk_shield", "sk_dome", "sk_missile", "sk_heatghost" };
static Model sk_mdl[SKM_N]; static SkinCache sk_sc[SKM_N]; static int sk_mok[SKM_N], sk_emp_spin = -1;
static Model sk_vm; static int sk_vm_ok, sk_vmc[9]; static void sk_vm_load(const char *vdir);   // Batou's arm-launcher viewmodel (sk_draw_vm)
// the deployables' own clips (tools/skills/make_skill_models.py: decoded from GIS_turret / GIS_emp / GIS_smalltachi / ...)
enum { SKA_DEPLOY, SKA_IDLE, SKA_FIRE, SKA_DAMAGE, SKA_DESTROY, SKA_WALK, SKA_FIRE_START, SKA_FIRE_END, SKA_UNDEPLOY, SKA_N };
static const char *const SKA_NAME[SKA_N] = { "deploy", "idle", "fire", "damage", "destroy", "walk", "fire_start", "fire_end", "undeploy" };
static int sk_clip[SKM_N][SKA_N];
static int sk_mini_jump = -1;   // b117: the mini Tachikoma's "jump" clip (the Seeker Drone's leap)
static MBone sk_rest[SKM_N][48]; static int sk_rest_ok[SKM_N];   // skinned models: the standing pose (idle frame 0) for holograms
static signed char sk_tyaw[8], sk_tpitch[8]; static int sk_ntyaw, sk_ntpitch, sk_tb_yaw = -1, sk_tb_pitch = -1;   // rigged turret: aim subtrees
// deployables that are going away: the destroyed / folding-up model plays its own clip, then vanishes (no logic, no hits)
typedef struct { int on, mi, clip, tier, unbuild; float t, len, hold, rz, yaw, pitch; V3 pos; float lit[3]; } SkGhost;
static SkGhost SKG[4];
static MBone sk_ident[48];
static GLuint sk_ovl_tex, sk_sh_tex;   // full-screen overlays (sk_ovl.gtx); Azuma's shield (sk_shield.gtx, make_shield.py)
// screen overlay cells of sk_ovl.gtx (tools/skills/out/sk_ovl.txt): u0 v0 u1 v1, blend 1 add / 2 mul2x
enum { SKV_VIG, SKV_CAMO, SKV_META, SKV_SHIELD, SKV_RUSH, SKV_DIVE, SKV_GLITCH, SKV_HLINES, SKV_DIVE_MUL, SKV_HEAT_MUL, SKV_LAUGH, SKV_SHIELD_HIT, SKV_N };
static const float SKV_UV[SKV_N][5] = {
	{ 0.000488f, 0.000488f, 0.499512f, 0.249512f, 1 }, { 0.500488f, 0.000488f, 0.999512f, 0.249512f, 1 }, { 0.000488f, 0.250488f, 0.499512f, 0.499512f, 1 },
	{ 0.500488f, 0.250488f, 0.999512f, 0.499512f, 1 }, { 0.000488f, 0.500488f, 0.499512f, 0.749512f, 1 }, { 0.500488f, 0.500488f, 0.999512f, 0.749512f, 1 },
	{ 0.000488f, 0.750488f, 0.249512f, 0.874512f, 1 }, { 0.250488f, 0.750488f, 0.499512f, 0.874512f, 1 }, { 0.500488f, 0.750488f, 0.749512f, 0.874512f, 2 },
	{ 0.750488f, 0.750488f, 0.999512f, 0.874512f, 2 }, { 0.000488f, 0.875488f, 0.249512f, 0.999512f, 1 }, { 0.250488f, 0.875488f, 0.499512f, 0.999512f, 1 } };   // (b72: laugh 256 x 128)
// b81 (fxtrim.h, fx.h fx_rect_shaped): the additive overlays' shapes, found at load from sk_ovl.gtx itself. A vignette (the
// low-HP haze, the skill flashes, camo, metaboost, rush lines, shield hit) is drawn as the ring around its exactly-black
// middle, the Laughing Man as a polygon around the logo; what is left out added exactly nothing. Host check (the same
// pictures, 17-44 % fewer fragments per layer): tools/fx/host_test/ovlhole.c. Test switch notrim: whole quads.
typedef struct { const FxTrim *t; const FxHole *h; FxTrim trim; FxHole hole; } SkOvlShape;
static SkOvlShape sk_ovl_sh[SKV_N]; static int sk_ovl_sh_n, sk_ovl_sh_said; static volatile int sk_ovl_sh_ready;   // (built on a worker thread: quads until sk_draw_overlays has seen it ready)
static unsigned char *sk_ovl_sh_d; static int sk_ovl_sh_w, sk_ovl_sh_h; static char sk_ovl_sh_msg[200];
static void sk_ovl_shapes_build(void) {
	unsigned char *mk = fxt_mask(sk_ovl_sh_d, sk_ovl_sh_w, sk_ovl_sh_h, 1); free(sk_ovl_sh_d); sk_ovl_sh_d = NULL; if (!mk) return;
	const int w = sk_ovl_sh_w, hh = sk_ovl_sh_h;
#ifdef __vita__
	SceUInt64 t0 = sceKernelGetProcessTimeWide();
#endif
	char *msg = sk_ovl_sh_msg; int o = snprintf(msg, sizeof(sk_ovl_sh_msg), "skill overlays: shapes (b81)");
	static const char *const NM[SKV_N] = { "vig", "camo", "meta", "shield", "rush", "dive", "glitch", "hlines", "dive_mul", "heat_mul", "laugh", "shield_hit" };
	for (int c = 0; c < SKV_N; c++) {
		if (SKV_UV[c][4] != 1) continue;   // (multiply x2: grey is what changes nothing)
		SkOvlShape *S = &sk_ovl_sh[c]; double X0 = SKV_UV[c][0] * w, Y0 = SKV_UV[c][1] * hh, X1 = SKV_UV[c][2] * w, Y1 = SKV_UV[c][3] * hh;
		fxt_frame_ex(mk, w, hh, X0, Y0, X1, Y1, fxt_mtexel, 1, &S->trim); fxt_hole(mk, w, hh, X0, Y0, X1, Y1, 1, &S->hole);
		double st = S->trim.n == FXT_EMPTY ? 1 : S->trim.n ? 1 - S->trim.area : 0, sh = S->hole.n ? S->hole.area : 0;
		if (st >= sh && st >= 0.05) S->t = &S->trim; else if (sh > st && sh >= 0.05) S->h = &S->hole; else continue;
		sk_ovl_sh_n++; if (o < (int)sizeof(sk_ovl_sh_msg) - 24) o += snprintf(msg + o, sizeof(sk_ovl_sh_msg) - o, " %s -%.0f%%", NM[c], 100 * (st > sh ? st : sh));
	}
	free(mk);
#ifdef __vita__
	if (o < (int)sizeof(sk_ovl_sh_msg) - 32) o += snprintf(msg + o, sizeof(sk_ovl_sh_msg) - o, ", %.1f ms (worker thread)", (sceKernelGetProcessTimeWide() - t0) / 1000.0);
#endif
}
#ifdef __vita__
static int sk_ovl_shapes_thread(SceSize args, void *argp) { (void)args; (void)argp; sk_ovl_shapes_build(); __sync_synchronize(); sk_ovl_sh_ready = 1; sceKernelExitDeleteThread(0); return 0; }
#endif
static void sk_ovl_shapes(const char *path) {
	memset(sk_ovl_sh, 0, sizeof(sk_ovl_sh)); sk_ovl_sh_n = 0; sk_ovl_sh_ready = 0; sk_ovl_sh_said = 0;
	FILE *f = fopen(path, "rb"); if (!f) return;
	unsigned char h[16]; if (fread(h, 1, 16, f) != 16 || memcmp(h, "GTX1", 4) || h[8] != 1) { fclose(f); return; }   // (DXT1 only)
	int w = h[4] | h[5] << 8, hh = h[6] | h[7] << 8; unsigned size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
	if (w < 4 || hh < 4 || w > 2048 || hh > 2048 || size != (unsigned)(w / 4) * (hh / 4) * 8) { fclose(f); return; }
	unsigned char *d = malloc(size); if (!d || fread(d, 1, size, f) != size) { free(d); fclose(f); return; }
	fclose(f);
	sk_ovl_sh_d = d; sk_ovl_sh_w = w; sk_ovl_sh_h = hh;
#ifdef __vita__
	static const int mask[3] = { SCE_KERNEL_CPU_MASK_USER_0 << 3, SCE_KERNEL_CPU_MASK_USER_0 << 2, 0 };
	for (int k = 0; k < 3; k++) { SceUID t = sceKernelCreateThread("gitsfa_ovlshape", sk_ovl_shapes_thread, 0x10000100 + 20, 0x10000, 0, mask[k], NULL);
		if (t >= 0) { if (sceKernelStartThread(t, 0, NULL) >= 0) return; sceKernelDeleteThread(t); } }
#endif
	sk_ovl_shapes_build(); sk_ovl_sh_ready = 1;
}
static GLuint sk_tex_load(const char *path) {   // GTX1: 'GTX1' u16 w, u16 h, u8 fmt (1 = DXT1, 2 = DXT5, else RGBA8), u8 0, u16 0, u32 size, data (as menu.h)
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	unsigned char h[16]; if (fread(h, 1, 16, f) != 16 || memcmp(h, "GTX1", 4)) { fclose(f); return 0; }
	int w = h[4] | h[5] << 8, hh = h[6] | h[7] << 8, fmt = h[8]; unsigned size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
	if (size > (16u << 20) || w < 4 || hh < 4 || w > 2048 || hh > 2048) { fclose(f); return 0; }
	if (size != (fmt == 1 ? (unsigned)(w / 4) * (hh / 4) * 8 : fmt == 2 ? (unsigned)(w / 4) * (hh / 4) * 16 : (unsigned)w * hh * 4)) { fclose(f); return 0; }   // exactly one level (never let the driver read / copy past it)
	void *buf = malloc(size); if (!buf || fread(buf, 1, size, f) != size) { free(buf); fclose(f); return 0; }
	fclose(f); GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	if (fmt == 1) glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, hh, 0, size, buf);
	else if (fmt == 2) glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w, hh, 0, size, buf);
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
// test switch (emulator / device test runs; no file = no effect): ux0:data/gitsfa/skshield makes every operator Azuma (the
// player and every bot), bots raise the shield whenever they see an enemy (odd bots always at Tier 2), and the autoplay player
// raises it, lowers it after 2 s, raises it again 1 s later, Tier 1 and Tier 2 in turn - screenshots catch every phase
static int sk_test_shield;
static int sk_test_t2;   // b109 test switch sktier2 (with autoplay): the player's Tier 2 is always ready, the test run uses it
static int skills_init(const char *vdir, const SkWorld *w, int models) {
	if (!w || !w->pos || !w->alive) return -1;
	{ char p[192]; snprintf(p, sizeof(p), "%sskshield", vdir); FILE *tf = fopen(p, "rb"); sk_test_shield = tf != NULL; if (tf) fclose(tf); }
	{ char p[192]; snprintf(p, sizeof(p), "%ssktier2", vdir); FILE *tf = fopen(p, "rb"); sk_test_t2 = tf != NULL; if (tf) fclose(tf); }
	SKW_ = *w; memset(SKA, 0, sizeof(SKA)); memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS)); memset(&SKV, 0, sizeof(SKV));
	for (int k = 0; k < 48; k++) { memset(&sk_ident[k], 0, sizeof(MBone)); sk_ident[k].q[3] = 1; }
	sk_fx_resolve();
	if (!SKSH_OUT[0]) sksh_load(vdir);   // databuilder: the shield tables are data (sk_shield.gsg, sk_shield_geo.h)
	int n = 0;
	for (int i = 0; i < SKM_N; i++) for (int c = 0; c < SKA_N; c++) sk_clip[i][c] = -1;
	for (int i = 0; i < SKM_N && models; i++) {
		if (sk_mok[i]) { n++; continue; }
		char p[192]; snprintf(p, sizeof(p), "%s%s.gvm", vdir, SKM_FILE[i]);
		model_keep_cpu = 1; int r = model_load(&sk_mdl[i], p); model_keep_cpu = 0; if (r < 0) continue;
		if (sk_mdl[i].nb > 48) { model_free(&sk_mdl[i]); continue; }
		model_prepare_rigid(&sk_mdl[i]);
		for (int k = 0; k < sk_mdl[i].nmesh; k++) { MMesh *m = &sk_mdl[i].mesh[k]; if (m->cpu) { free(m->cpu); m->cpu = NULL; } }   // rigid: rvbo; skinned (turret, mini-Tachikoma): CPU skinning (model.h skin plans, chars b35)
		skincache_init(&sk_sc[i], &sk_mdl[i]); sk_mok[i] = 1; n++;
	}
	for (int i = 0; i < SKM_N; i++) if (sk_mok[i]) for (int c = 0; c < SKA_N; c++) sk_clip[i][c] = model_find_clip(&sk_mdl[i], SKA_NAME[c]);
	sk_mini_jump = sk_mok[SKM_MINI] ? model_find_clip(&sk_mdl[SKM_MINI], "jump") : -1;   // b117: the Seeker Drone's leap
	for (int i = 0; i < SKM_N; i++) { sk_rest_ok[i] = sk_mok[i] && sk_mdl[i].mesh[0].rigid < 0 && sk_clip[i][SKA_IDLE] >= 0 && sk_mdl[i].nb <= 48;
		if (sk_rest_ok[i]) model_sample(&sk_mdl[i], sk_clip[i][SKA_IDLE], 0, sk_rest[i]); }
	sk_ntyaw = sk_ntpitch = 0; sk_tb_yaw = sk_tb_pitch = -1;
	if (sk_rest_ok[SKM_TURRET]) {   // rig: Bip01_body02 (yaw) > Bip01_head (pitch) > gun01 > gun02, wing01, wing02
		static const char *const YB[6] = { "Bip01_body02", "Bip01_head", "gun01", "gun02", "wing01", "wing02" };
		for (int k = 0; k < 6; k++) { int b = model_find_bone(&sk_mdl[SKM_TURRET], YB[k]); if (b < 0) continue; sk_tyaw[sk_ntyaw++] = (signed char)b; if (k) sk_tpitch[sk_ntpitch++] = (signed char)b; }
		sk_tb_yaw = model_find_bone(&sk_mdl[SKM_TURRET], "Bip01_body02"); sk_tb_pitch = model_find_bone(&sk_mdl[SKM_TURRET], "Bip01_head"); }
	if (sk_mok[SKM_EMP]) sk_emp_spin = model_find_clip(&sk_mdl[SKM_EMP], "spin");
	if (!sk_ovl_tex) { char p[192]; snprintf(p, sizeof(p), "%ssk_ovl.gtx", vdir); sk_ovl_tex = sk_tex_load(p); if (sk_ovl_tex) sk_ovl_shapes(p); }
	if (sk_ovl_tex) n++;
	if (!sk_nj_sc[0].ok && !sk_nj_sc[1].ok) { char p[192]; snprintf(p, sizeof(p), "%ssk_nj_in.gsc", vdir); fxs_load(&sk_nj_sc[0], p); snprintf(p, sizeof(p), "%ssk_nj_out.gsc", vdir); fxs_load(&sk_nj_sc[1], p);
		logp("skills: the Net Jump's screen scenes %s (sk_nj_in.gsc %.2f s, sk_nj_out.gsc %.2f s - b121)\n", sk_nj_sc[0].ok && sk_nj_sc[1].ok ? "loaded" : "missing: the white flash instead", fxs_len(&sk_nj_sc[0]), fxs_len(&sk_nj_sc[1])); }
	if (!sk_sh_tex) { char p[192]; snprintf(p, sizeof(p), "%ssk_shield.gtx", vdir); sk_sh_tex = sk_tex_load(p); }
	if (sk_sh_tex) n++;
	if (models) { sk_vm_load(vdir); if (sk_vm_ok) n++; }   // Batou's arm-launcher viewmodel
	sk_ok = 1; return n;
}
static void sk_ms_born(SkMissile *M) { M->ser = ++sk_ser_n ? sk_ser_n : ++sk_ser_n; M->p0 = M->p; M->v0 = M->v; M->delay0 = M->delay; }   // net: the launch the snapshot carries
static void sk_obj_free(int o) { if (o >= 0 && o < SK_MAXOBJ) { SKO[o].alive = 0; for (int a = 0; a < SK_MAXACT; a++) if (SKA[a].obj == o) SKA[a].obj = -1; } }
static void sk_actor_clear_fx(SkillActor *A) {
	A->hack_t = A->emp_t = A->shock_t = A->warn_t = A->laser_t = A->camo_close = A->stun_t = A->blind_t = 0; A->hit_ago = 99; A->cloak_hit_t = 0; A->heat_t[0] = A->heat_t[1] = A->mark_t[0] = A->mark_t[1] = 0;
	A->heat_by[0] = A->heat_by[1] = A->mark_by[0] = A->mark_by[1] = -1;
	A->nlocks = 0; A->toggled = 0; A->linked = 0; A->otgt = -1; A->fire_t = 9; A->aim_ok = 0; A->dashing = 0; A->post_t = 0; A->btn_up = 0;
	A->synced = 0; A->nsync = 0; for (int k = 0; k < SK_SYNC_MAX; k++) A->sync_who[k] = -1;
	A->sx_skill = -1; A->sx_from = -1; A->sx_t = A->sx_len = A->sx_age = 0; A->sync_wait = 0;
}
// a new match: Tier 1 ready (footage: ready at t = 0), no Tier-2 intel, nothing deployed
static void skills_reset_match(void) {
	memset(sk_uses, 0, sizeof(sk_uses)); memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS)); memset(&SKV, 0, sizeof(SKV)); memset(SKG, 0, sizeof(SKG)); sk_mq_n = 0; sk_mq_t = 0;
	for (int a = 0; a < SK_MAXACT; a++) { SkillActor *A = &SKA[a]; A->state = SKS_READY; A->cool = 0; A->t = 0; A->obj = -1; A->bar = -1; A->tier = 1; sk_actor_clear_fx(A); A->bot_t = sk_test_shield ? 0.2f : 1 + a * 0.37f; A->last_hp = -1; A->was_alive = 0;
		A->intel = 0; A->intel_in = 0; A->t2_note = 0; }
	fade_others("cloaking_loop", -1, 100); fade_others("is_noise_scan_loop", -1, 100); fade_others("is_metabolism_loop", -1, 100);
}
// Demolition round reset: deployables, active and downloaded skills end; cooldowns keep running, intel is kept
static void sk_round_reset(void) {
	memset(SKO, 0, sizeof(SKO)); memset(SKMS, 0, sizeof(SKMS)); memset(SKG, 0, sizeof(SKG));
	for (int a = 0; a < SK_MAXACT; a++) { SkillActor *A = &SKA[a]; if (A->state == SKS_ACTIVE || A->state == SKS_AIM) A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->obj = -1; sk_actor_clear_fx(A); }
	SKV.camo = SKV.glitch = SKV.hlines = SKV.laugh = SKV.vig = SKV.heat = 0; sk_nj_play = -1; sk_nj_hold = 0;
}
// at spawn / operator change: actor a uses skill (operator index), team 0/1
static void sk_actor_set(int a, int skill, int team) {
	if (a < 0 || a >= SK_MAXACT) return; SkillActor *A = &SKA[a];
	if (sk_test_shield && skill >= 0) skill = SK_PLASMASHIELD;   // (test switch skshield)
	if (A->on && A->skill == skill && A->team == team) return;
	for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].owner == a) sk_obj_free(o);
	for (int m = 0; m < SK_MAXMS; m++) if (SKMS[m].owner == a) SKMS[m].on = 0;
	memset(A, 0, sizeof(*A)); A->on = skill >= 0 && skill < SK_COUNT; A->skill = A->on ? skill : 0; A->team = team; A->tier = 1; A->state = SKS_READY; A->obj = -1; A->bar = -1;
	sk_actor_clear_fx(A); A->last_hp = -1; A->bot_t = sk_test_shield ? 0.2f : 1.5f + a * 0.4f;   // (test switch skshield: at once)
}
static void sk_actor_off(int a) { if (a >= 0 && a < SK_MAXACT && SKA[a].on) { sk_actor_set(a, -1, 0); SKA[a].on = 0; } }
static int sk_ti(int a) { int t = SKA[a].tier - 1; return t < 0 ? 0 : t > 1 ? 1 : t; }   // SK_DEF index of the running activation (0 Tier 1, 1 Tier 2)
static float sk_p1(int a) { return SK_DEF[SKA[a].skill].p1[sk_ti(a)]; }
static float sk_p2(int a) { return SK_DEF[SKA[a].skill].p2[sk_ti(a)]; }
static int sk_cost2(int a) { return sk_valid(a) ? SK_DEF[SKA[a].skill].cost2 : 400; }

// ---- economy: Tier 1 = cooldown, Tier 2 = intel
// a Tier-2 skill (or the deployable it placed) still runs: both tiers are disabled (the HUD's "no" icons)
static int sk_t2_running(int a) {
	const SkillActor *A = &SKA[a];
	if (A->tier == 2 && (A->state == SKS_ACTIVE || A->state == SKS_AIM)) return 1;
	return A->obj >= 0 && A->obj < SK_MAXOBJ && SKO[A->obj].alive && SKO[A->obj].owner == a && SKO[A->obj].tier >= 2;
}
static int sk_blocked(int a) { return SKA[a].hack_t > 0 || SKA[a].emp_t > 0; }
static int sk_t1_ready(int a) { return sk_ok && sk_valid(a) && SKA[a].state == SKS_READY && SKA[a].cool <= 0 && !sk_t2_running(a) && !sk_blocked(a); }
static int sk_t2_ready(int a) { return sk_ok && sk_valid(a) && SKA[a].intel >= sk_cost2(a) && (SKA[a].state == SKS_READY || SKA[a].state == SKS_COOLDOWN) && !sk_t2_running(a) && !sk_blocked(a); }
SK_API int sk_intel(int a) { return sk_valid(a) ? SKA[a].intel : 0; }
static void sk_intel_changed(int a) {   // the Tier-2 ring just filled: "TIER 2 SKILL READY" + the Operator ("Therm-optic Camo ready.")
	if (a < 0 || a >= SK_MAXACT) return;
	SkillActor *A = &SKA[a]; int full = A->intel >= sk_cost2(a);
	if (full && !A->t2_note) { A->t2_note = 1;
		if (a == sk_local && sk_ok) { char n[64]; SKV.t2n = 3.0f; ev_play("intel_skill/alarm_tier2", NULL); snprintf(n, sizeof(n), "voice/donut/common/op_%s_ready", SK_DEF[A->skill].id); radio_play(n); sk_msg_ready(a); } }
	if (!full) A->t2_note = 0;
}
// the engine's running intel total of actor a (player: the HUD intel; bots: kills x 100). Intel gained fills the Tier-2 ring
// (capped at the operator's cost); spending it never lowers the engine's own number. (Name kept from the tier-by-intel scheme.)
static void sk_set_tier(int a, int intel_total) {
	if (!sk_valid(a)) return; SkillActor *A = &SKA[a];
	if (intel_total > A->intel_in) { A->intel += intel_total - A->intel_in; if (A->intel > sk_cost2(a)) A->intel = sk_cost2(a); }
	A->intel_in = intel_total; sk_intel_changed(a);
}
// intel from the skill code itself (SkillSync +20): the local player gets it through notify (which adds to the engine's intel)
static void sk_give_intel(int a, int n, const char *text) {
	if (!sk_valid(a)) return;
	if (a == sk_local && SKW_.notify) { sk_notify(a, SKN_POINTS, text, n); return; }
	SKA[a].intel += n; if (SKA[a].intel > sk_cost2(a)) SKA[a].intel = sk_cost2(a); sk_intel_changed(a);
}
static void sk_pay(int a) {   // at activation (placed skills: at placement): Tier 1 starts its cooldown, Tier 2 spends the intel
	SkillActor *A = &SKA[a]; A->pre_cool = A->cool; A->warned = 0; if (a >= 0 && a < SK_MAXACT) sk_uses[a] += A->tier == 2 ? 2 : 1;
	if (A->tier == 2) { A->intel -= sk_cost2(a); if (A->intel < 0) A->intel = 0; sk_intel_changed(a); if (a == sk_local) SKV.t2n = 0; }
	else A->cool = sk_test_shield ? 3 : SK_DEF[A->skill].cool1;   // (test switch skshield: back in 3 s)
}

// ---- state machine
static const char *sk_state_name(int a) {
	static const char *S[4] = { "ready", "aim", "active", "cooldown" };
	if (!sk_valid(a)) return "none"; static char b[64]; snprintf(b, sizeof(b), "%s %s (tier %d)", SK_DEF[SKA[a].skill].name, S[SKA[a].state & 3], SKA[a].tier); return b;
}
static void sk_to_cooldown(int a) { SkillActor *A = &SKA[a]; A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; A->nlocks = 0; A->btn_up = 0; }
static void sk_nanogel_end(int a);   // b83 (below)
static void sk_meta_release(int a) {   // metaboost over: overheal back to max
	SkillActor *A = &SKA[a];
	for (int b = 0; b < SK_MAXACT; b++) if ((A->linked >> b & 1) && sk_valid(b) && SKW_.heal) SKW_.heal(b, 0, 1.0f);
	A->linked = 0;
}
// ends a personal skill (camo, scan, metaboost, rush, shield, dive) early or on time
static void sk_end_active(int a, int broke) {
	SkillActor *A = &SKA[a]; if (A->state != SKS_ACTIVE || SK_MIRRORED) return;
	V3 p = sk_pos(a);
	switch (A->skill) {
	case SK_OPTICAL: sk_ev(a, "intel_skill/cloaking_end"); sk_fx_up(SKF_CAMO_OFF, p); if (a == sk_local) fade_others("cloaking_loop", -1, 250); break;
	case SK_METABOOST: sk_meta_release(a); sk_nanogel_end(a); sk_ev(a, "intel_skill/metabolism_end"); if (a == sk_local) fade_others("is_metabolism_loop", -1, 300); break;
	case SK_RUSH: sk_sfx("foley_co_rush_stop_001", p, 200); A->dashing = 0; break;
	case SK_PLASMASHIELD: {
		V3 c = sk_shield_origin(a);
		if (broke) { A->sh_ph = 4; A->sh_t = 0; }
		if (broke) { sk_fx_up(SKF_SH_BREAK, c); sk_ev_at("intel_skill/shield_exp", c); } else if (A->toggled) sk_ev(a, "intel_skill/shield_end");
		A->toggled = 0; } break;
	case SK_CYBERDIVE: if (a == sk_local) { ev_play("ui/ghost_dive_end", NULL);
			if (sk_nj_sc[1].ok && sk_alive(a)) { sk_nj_play = 1; sk_nj_t = 0; sk_nj_hold = SK_NJ_HOLD; }   // b121: the PC's scene - the streaks first, the white blocks as the world comes back (sk_update)
			else { SKV.flash = 0.3f; SKV.glitch = SKV.glitch_len = 0.6f; SKV.glitch_c[0] = 1; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 1; } }   // (b84: the light beams and white blocks of EP26 4:45.5)
		sk_ev(a, "intel_skill/cyberdive_end_tp"); sk_fx_up(SKF_DIVE_ON, sk_pos(a)); break;
	default: break;
	}
	sk_to_cooldown(a);
}
static void sk_sx_end(int a, int broke);
static void sk_cancel(int a) {   // EMP shock / hack / death: AIM is dropped, active and downloaded skills end
	SkillActor *A = &SKA[a]; if (SK_MIRRORED) return;
	if (A->state == SKS_AIM) { A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->nlocks = 0; A->btn_up = 0; if (A->skill == SK_ARMLAUNCHER) sk_ev(a, "intel_skill/armlauncher_off"); }
	else if (A->state == SKS_ACTIVE) sk_end_active(a, 0);
	if (A->sx_t > 0) sk_sx_end(a, 0);
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
	SkObj *O = &SKO[s]; memset(O, 0, sizeof(*O)); O->ser = ++sk_ser_n ? sk_ser_n : ++sk_ser_n; O->type = type; O->owner = owner; O->team = SKA[owner].team; O->tier = SKA[owner].tier; O->alive = 1; O->target = -1;
	for (int k = 0; k < SK_MAXACT; k++) O->side[k] = 0;
	O->scan_t = 0.05f * (s % 4); O->lit[0] = O->lit[1] = O->lit[2] = 1; O->clip = SKA_DEPLOY; O->clip_t = 0;
	return s;
}
static void sk_place(int a, V3 p, V3 n) {   // confirm: the deployable appears, the tier is paid
	SkillActor *A = &SKA[a]; int ti = sk_ti(a); float yaw = sk_yaw(a); V3 f = sk_pos(a);
	int type = A->skill == SK_SENTRYGUN ? SKO_TURRET : A->skill == SK_CYBERWALL ? SKO_WALL : A->skill == SK_DMDDRONE ? SKO_DRONE : A->skill == SK_EMP ? SKO_EMP : SKO_RECON;
	int o = sk_obj_new(type, a); SkObj *O = &SKO[o]; O->pos = p; O->nrm = n; O->yaw0 = O->yaw = yaw; light_at(p.x, p.y, p.z + 20, O->lit);
	A->obj = o; sk_pay(a); A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; A->btn_up = 0; sk_voice_use(a); sk_msg_ally(a);
	const SkillDef *D = &SK_DEF[A->skill];
	switch (type) {
	case SKO_TURRET: O->life = D->duration[ti]; O->hp = O->hpmax = D->p2[ti]; sk_ev_at("intel_skill/turret_hacking_deploy", p); break;
	case SKO_WALL: { V3 tn = sk_norm(v3(f.x - p.x, f.y - p.y, 0)); if (sk_len(v3(f.x - p.x, f.y - p.y, 0)) < 1) tn = v3(-cosf(yaw), -sinf(yaw), 0);
		O->nrm = tn; O->w = D->p1[ti]; O->h = D->p2[ti]; O->life = D->duration[ti]; O->hp = O->hpmax = ti ? 900 : 600;
		sk_ev_at("intel_skill/screen_start", p); sk_sfx("is_screen_set1", p, 200); } break;
	case SKO_DRONE: O->life = D->duration[ti] + 60; O->hp = O->hpmax = D->p2[ti]; O->energy = 1; O->cur = O->from = v3(f.x, f.y, f.z + 1500); O->pos = v3(p.x, p.y, p.z + 350);
		O->r = ti ? 1500 : 900; sk_ev_at("intel_skill/dmd_spawn", O->cur); sk_ev_at("intel_skill/dmd_start", O->pos); break;
	case SKO_EMP: O->r = D->p1[ti]; O->life = D->duration[ti]; O->hp = O->hpmax = ti ? 120 : 60; sk_ev_at(ti == 0 ? "intel_skill/emp_set_tier1" : "intel_skill/emp_set", p); sk_sfx("is_emp_beep", p, 220); break;   // (b85: the v2.0 generator - its fuse, Tier 2 tougher)
	case SKO_RECON: O->r = D->p1[ti]; O->life = D->duration[ti] + 1; O->hp = O->hpmax = D->p2[ti];
		sk_ev_at("intel_skill/mini_tachikoma_spawn", p); ev_play_later("intel_skill/mini_tachikoma_stand_up", &p.x, 0.6f); break;
	}
	sk_fx_up(SKF_BUILD, p);
}

// ---- SkillSync: a shareable Tier 2 links up to 3 allies; each can download the Tier-1 effect
static int sk_sharing(int s) { return sk_alive(s) && SKA[s].state == SKS_ACTIVE && SKA[s].tier == 2 && SK_DEF[SKA[s].skill].share && SKA[s].nsync < SK_SYNC_MAX; }
static int sk_sync_ok(int s, int r) {   // r may download s's skill now (same team, a different operator skill, nothing downloaded yet)
	return sk_valid(r) && r != s && sk_sharing(s) && sk_alive(r) && !sk_hostile(s, r) && !(SKA[s].synced >> r & 1) && SKA[r].skill != SKA[s].skill && SKA[r].sx_t <= 0 && !sk_blocked(r);
}
// the sharer within maxd that r is closest to (-1 none)
static int sk_sync_source(int r, float maxd) {
	if (!sk_ok || !sk_valid(r)) return -1;
	int best = -1; float bd = maxd; V3 p = sk_pos(r);
	for (int s = 0; s < SK_MAXACT; s++) if (sk_sync_ok(s, r)) { float d = sk_len(vsub(sk_pos(s), p)); if (d < bd) { bd = d; best = s; } }
	return best;
}
static float sk_share_left(int s) {   // what is left of the sharer's Tier 2
	const SkillActor *S = &SKA[s]; const SkillDef *D = &SK_DEF[S->skill];
	switch (S->skill) {
	case SK_HEATSCAN: return 0.3f + D->p1[1] * SK_HS_GAP[1] - S->t;
	case SK_RUSH: return S->energy * D->duration[1];
	default: return D->duration[1] - S->t;
	}
}
static void sk_sx_start(int r) {   // the downloaded effect begins on r
	SkillActor *R = &SKA[r]; V3 p = sk_pos(r);
	R->sx_age = 0; R->sx_energy = 1; R->sx_pulses = 0; R->sx_pulse_t = 9; R->sx_tick_t = 0; R->loop_t = 0;
	switch (R->sx_skill) {
	case SK_OPTICAL: sk_ev(r, "intel_skill/cloaking_start"); sk_fx_up(SKF_CAMO_ON, p); R->camo_close = 0; break;
	case SK_HEATSCAN: sk_ev(r, "intel_skill/heat_scan_start"); sk_fx_up(SKF_HS_FLASH, p); break;
	case SK_METABOOST: sk_ev(r, "intel_skill/metabolism_start"); sk_fx_up(SKF_META_ON, p); if (r == sk_local) play("is_metabolism_loop", 200); break;
	case SK_RUSH: sk_sfx("foley_co_dash_01", p, 230); R->dashing = 0; break;
	case SK_PLASMASHIELD: sk_ev(r, "intel_skill/shield_start"); break;
	}
}
static void sk_sx_end(int r, int broke) {
	SkillActor *R = &SKA[r]; V3 p = sk_pos(r); if (SK_MIRRORED) return;
	switch (R->sx_skill) {
	case SK_OPTICAL: sk_ev(r, "intel_skill/cloaking_end"); sk_fx_up(SKF_CAMO_OFF, p); if (r == sk_local) fade_others("cloaking_loop", -1, 250); break;
	case SK_METABOOST: sk_nanogel_end(r); sk_ev(r, "intel_skill/metabolism_end"); if (r == sk_local) fade_others("is_metabolism_loop", -1, 300); break;
	case SK_RUSH: sk_sfx("foley_co_rush_stop_001", p, 200); R->dashing = 0; break;
	case SK_PLASMASHIELD: { V3 c = sk_shield_origin(r); if (broke) { R->sh_ph = 4; R->sh_t = 0; }
		if (broke) { sk_fx_up(SKF_SH_BREAK, c); sk_ev_at("intel_skill/shield_exp", c); } else sk_ev(r, "intel_skill/shield_end"); } break;
	}
	R->sx_t = 0; R->sx_skill = -1;
}
// r downloads s's skill (the receiver gets the Tier-1 effect)
static void sk_sync_do(int s, int r) {
	if (!sk_sync_ok(s, r)) return;
	SkillActor *S = &SKA[s], *R = &SKA[r]; const SkillDef *D = &SK_DEF[S->skill];
	R->sx_skill = S->skill; R->sx_from = s;
	R->sx_len = R->sx_t = D->duration[2] > 0 ? D->duration[2] : fmaxf(D->duration[0], sk_share_left(s));
	if (S->skill == SK_RUSH) R->sx_len = R->sx_t = 30;   // rush: until its energy is gone
	R->sync_wait = 0; sk_sx_start(r);
	S->synced |= 1u << r; if (S->nsync < SK_SYNC_MAX) S->sync_who[S->nsync] = (signed char)r; S->nsync++;
	if (r == sk_local) { char n[64]; ev_play("intel_skill/intel_transfer_fp", NULL); ev_play("ui/transfer", NULL); snprintf(n, sizeof(n), "voice/donut/common/%s_transferintel", SK_STEM[S->skill]); radio_play(n);
		SKV.sync_note = 2.5f; SKV.sync_from = s; }
	else sk_ev(r, "intel_skill/intel_transfer_tp");
	if (s == sk_local) ev_play("intel_skill/intel_transfer_tp", NULL);
	if (r == sk_local) sk_msg_post(SK_FACE[S->skill], sk_name(s), "Transfer complete.");                 // footage n2 340 s
	if (s == sk_local) sk_msg_post(SK_FACE[R->skill], sk_name(r), "Share code transmission complete.");  // footage n6 104-115 s (one per ally)
	sk_give_intel(s, SK_SYNC_PTS, "SKILLSYNC COMPLETED");
}
// the local player's interact button: download from the nearest sharer within reach and in sight. 1 = done (the press is used)
static int sk_sync_try(int a) {
	if (!sk_ok || !sk_alive(a)) return 0;
	int s = sk_sync_source(a, SK_SYNC_R); if (s < 0 || !sk_los(sk_eye(a), sk_chest(s))) return 0;
	sk_sync_do(s, a); return 1;
}
static void sk_share_open(int s) {   // a shareable Tier 2 starts: the operator's "share" line, the slots are cleared
	SkillActor *S = &SKA[s]; S->synced = 0; S->nsync = 0; for (int k = 0; k < SK_SYNC_MAX; k++) S->sync_who[k] = -1;
	char n[32]; snprintf(n, sizeof(n), "d_%s_share", SK_STEM[S->skill]); sk_voice_line(s, n);
}

// ---- personal skills: activation
static __attribute__((unused)) void sk_meta_link(int a, float r) {   // Tier-2 metaboost: allies within r join (v1; b83: v2.0 Nano-Gel has no auto link - kept for reference)
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a);
	for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && !sk_hostile(a, b) && !(A->linked >> b & 1) && sk_len(vsub(sk_pos(b), p)) < r) {
		A->linked |= 1u << b; sk_fx_up(SKF_META_ON, sk_pos(b)); if (a == sk_local || b == sk_local) ev_play("intel_skill/metabolism_link", NULL); }
}
static void sk_t2_start(int a) {   // what a Tier 2 adds when it starts (or when a Tier 1 is upgraded)
	SkillActor *A = &SKA[a];
	// (b83: v2.0's Nano-Gel Armor links nobody by itself - allies take it by SkillSync)
	if (SK_DEF[A->skill].share) sk_share_open(a);
}
static int sk_activate(int a) {
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a);
	A->state = SKS_ACTIVE; A->t = 0; A->fx_t = 0; A->loop_t = 0; A->energy = 1; sk_pay(a); sk_voice_use(a); sk_msg_ally(a);
	if (a == sk_local) sk_blur_req = 1;
	switch (A->skill) {
	case SK_OPTICAL: sk_ev(a, "intel_skill/cloaking_start"); sk_fx_up(SKF_CAMO_ON, p); A->camo_close = 0; break;
	case SK_HEATSCAN: sk_ev(a, "intel_skill/heat_scan_start"); sk_fx_up(SKF_HS_FLASH, p); A->pulses = 0; A->pulse_t = 9;
		if (a == sk_local) { SKV.glitch = SKV.glitch_len = 0.3f; SKV.glitch_c[0] = 1; SKV.glitch_c[1] = 0.45f; SKV.glitch_c[2] = 0.25f; } break;
	case SK_METABOOST:
		A->linked = 1u << a; A->link_t = 1; A->tick_t = 0; sk_ev(a, "intel_skill/metabolism_start"); sk_fx_up(SKF_META_ON, p);
		if (a == sk_local) play("is_metabolism_loop", 200); break;
	case SK_RUSH: sk_sfx("foley_co_dash_01", p, 230); A->dashing = 0;
		if (a == sk_local) { SKV.vig = SKV.vig_len = 0.45f; SKV.vig_c[0] = 0.3f; SKV.vig_c[1] = 0.85f; SKV.vig_c[2] = 0.9f; }   // b80 (Paz video EP19 188.55 s: a teal flash as it starts; b95: 0.45 s, as EP19 307.0-307.5)
		break;
	case SK_PLASMASHIELD: A->toggled = 1; A->dome = p; A->sh_alarm = 0; sk_ev(a, "intel_skill/shield_start"); break;
	case SK_SPECIALRECON: A->nlocks = 0; sk_ev(a, "intel_skill/mini_tachikoma_ui_on"); sk_fx_up(SKF_BUILD, sk_chest(a)); break;   // b85: the Seeker Drone - the hand projects it (sk_tick_active releases)
	case SK_CYBERDIVE: {   // b84 NET JUMP (v2.0, EP26): into the network where she stands - the enemies looking at her lose their sight
		A->cam = p; A->dive_still = 0; sk_fx_up(SKF_DIVE_ON, p);
		if (a == sk_local) { ev_play("intel_skill/cyberdive_start_fp", NULL); ev_play("ui/ghost_dive_in", NULL); SKV.glitch = SKV.glitch_len = 0.35f; SKV.glitch_c[0] = 1; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 1; sk_nj_hold = 0;
			if (sk_nj_sc[0].ok) { sk_nj_play = 0; sk_nj_t = 0; } else SKV.flash = 0.25f; }   // (b121: the PC's own scene of it; the plain flash without the file)
		else sk_ev(a, "intel_skill/cyberdive_start_tp");
		const SkillDef *D = &SK_DEF[SK_CYBERDIVE]; int ti = A->tier == 2 ? 1 : 0; float cc = cosf(D->p2[ti] * 3.14159f / 180); V3 c = sk_chest(a);
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && SKA[b].hack_t <= 0) {   // "hacks the vision of enemies looking at Reiko"
			V3 e = sk_eye(b), v = vsub(c, e); float l = sk_len(v); if (l < 1 || l > 4000 || vdot(v, sk_dir(b)) / l < cc || !sk_los(e, c)) continue;
			SKA[b].hack_t = D->p1[ti]; sk_cancel(b); sk_fx_up(SKF_DIVE_HIT, sk_pos(b));
			if (b == sk_local) { SKV.glitch = SKV.glitch_len = D->p1[ti]; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; sk_warn(SK_CYBERDIVE); } }
		if (A->tier == 2) {   // Tier 2: "a holographic decoy in the place where she initiated the dive" (it cannot be destroyed)
			int o = sk_obj_new(SKO_DECOY, a); SkObj *O = &SKO[o]; O->pos = p; O->yaw0 = O->yaw = sk_yaw(a); O->life = D->duration[1] + 4; O->hp = O->hpmax = 1e9f; O->h = 170; O->phase = 1;
			light_at(p.x, p.y, p.z + 20, O->lit); }
	} break;
	default: break;
	}
	if (A->tier == 2) sk_t2_start(a);
	return 1;
}
// arm launcher: one missile per lock (none: one straight ahead), 0.08 s apart; Tier 1 holds one lock
static void sk_launcher_fire(int a) {
	SkillActor *A = &SKA[a]; V3 e = sk_eye(a), d = sk_dir(a), r = sk_norm(v3(d.y, -d.x, 0));
	V3 p0 = vadd(vadd(e, vmul(d, 45)), vadd(vmul(r, 22), v3(0, 0, -22)));
	int n = A->nlocks > 0 ? A->nlocks : 1;
	sk_ev(a, "intel_skill/armlauncher_fire"); sk_fx(SKF_AL_FIRE, p0, d, &d);
	for (int k = 0; k < n; k++) {
		int s = -1; for (int m = 0; m < SK_MAXMS; m++) if (!SKMS[m].on) { s = m; break; } if (s < 0) break;
		SkMissile *M = &SKMS[s]; memset(M, 0, sizeof(*M)); M->on = 1; M->owner = a; M->team = A->team; M->kind = 0; M->target = A->nlocks > 0 ? A->locks[k] : -1; M->dmg = sk_p2(a);
		V3 sp = n > 1 ? v3((sk_rnd() - 0.5f) * 0.25f, (sk_rnd() - 0.5f) * 0.25f, 0.12f + 0.1f * k) : v3(0, 0, 0);   // a salvo fans out, then homes in
		M->p = p0; M->v = vmul(sk_norm(vadd(d, sp)), 2500); M->delay = 0.08f * k + 1e-4f; sk_ms_born(M);
	}
	if (a == sk_local || A->nlocks) ev_play_later("intel_skill/armlauncher_off", a == sk_local ? NULL : &p0.x, 0.4f);
	sk_pay(a); A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; A->nlocks = 0; A->post_t = SK_AL_FIRE + SK_AL_OUT; A->btn_up = 0;   // footage: the shot, then the launcher folds back into the arm
}
// start tier (1 / 2) of actor a's skill. 1 = started (AIM for placed skills and the launcher; they pay at confirm)
static int sk_use(int a, int tier) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a];
	if (tier == 2 ? !sk_t2_ready(a) : !sk_t1_ready(a)) return 0;
	A->tier = tier; A->btn_up = 0;
	if (SK_DEF[A->skill].placed || A->skill == SK_ARMLAUNCHER) {
		A->state = SKS_AIM; A->t = 0; A->nlocks = 0; A->lock_t = 0.05f; A->aim_ok = 0;   // (the first lock as soon as the launcher is up)
		if (A->skill == SK_ARMLAUNCHER) { sk_ev(a, "intel_skill/armlauncher_on"); if (a == sk_local) ev_play("intel_skill/armlauncher_on_ring", NULL); sk_msg_ally(a); }
		else if (a == sk_local) ev_play(A->skill == SK_SENTRYGUN ? "intel_skill/turret_hacking_ui_on" : "intel_skill/mini_tachikoma_ui_on", NULL);
		return 1; }
	return sk_activate(a);
}
// the button was held: a Tier 1 that the same press started (< 0.6 s ago) becomes Tier 2; its cooldown is refunded
static int sk_upgrade(int a) {
	SkillActor *A = &SKA[a];
	if (A->tier != 1 || A->t > 0.6f || A->intel < sk_cost2(a) || sk_blocked(a) || (A->state != SKS_ACTIVE && A->state != SKS_AIM)) return 0;
	A->tier = 2;
	if (A->state == SKS_ACTIVE) { A->cool = A->pre_cool; A->intel -= sk_cost2(a); if (A->intel < 0) A->intel = 0; sk_intel_changed(a); A->energy = 1; sk_t2_start(a); if (a == sk_local) SKV.t2n = 0; }
	if (a == sk_local) ev_play("intel_skill/alarm_tier2", NULL);
	return 1;
}
// skill button pressed (bots, autoplay, tutorial scripts): AIM -> cancel / fire the locked launcher; ACTIVE -> shield toggle,
// end rush / dive; otherwise Tier 1, or Tier 2 when Tier 1 is not available. 1 = something happened.
static int sk_press(int a) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a];
	if (sk_blocked(a)) return 0;
	switch (A->state) {
	case SKS_AIM:
		if (A->skill == SK_ARMLAUNCHER && A->nlocks > 0) { sk_launcher_fire(a); return 1; }
		A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->nlocks = 0; A->btn_up = 0;   // cancel: nothing is paid
		if (A->skill == SK_ARMLAUNCHER) A->post_t = SK_AL_OUT * fminf(1, A->t / SK_AL_IN);   // the arm folds back (no shot)
		if (a == sk_local) ev_play(A->skill == SK_ARMLAUNCHER ? "intel_skill/armlauncher_off" : "intel_skill/mini_tachikoma_ui_off", NULL);
		return 1;
	case SKS_ACTIVE:
		if (A->skill == SK_PLASMASHIELD) {   // toggle: raise / lower the panel (its time runs on while it is down; footage)
			A->toggled = !A->toggled; if (A->toggled) A->dome = sk_pos(a);
			sk_ev(a, A->toggled ? "intel_skill/shield_start" : "intel_skill/shield_end"); return 1; }
		if (A->skill == SK_RUSH || A->skill == SK_CYBERDIVE) { sk_end_active(a, 0); return 1; }
		return 0;
	default:
		if (sk_t1_ready(a)) return sk_use(a, 1);
		return sk_use(a, 2);
	}
}
// the player's skill button: pressed = this frame, down = held. Tap = sk_press; when the Tier-2 ring is full, holding the
// press SK_HOLD_T2 s turns the Tier 1 it started into Tier 2. Returns 1 when something happened.
static int sk_button(int a, int pressed, int down, float dt) {
	if (!sk_ok || !sk_valid(a)) return 0; SkillActor *A = &SKA[a]; int r = 0;
	if (pressed) { int t2 = sk_t2_ready(a), was = A->state; r = sk_press(a);
		if (r && t2 && A->tier == 1 && was != SKS_ACTIVE && was != SKS_AIM && (A->state == SKS_ACTIVE || A->state == SKS_AIM)) { A->btn_up = 1; A->btn_hold = 0; } }
	else if (A->btn_up && down) { A->btn_hold += dt; if (A->btn_hold >= SK_HOLD_T2) { A->btn_up = 0; r = sk_upgrade(a); } }
	if (!down) A->btn_up = 0;
	return r;
}
static void sk_hack(int a, int b);
// fire pressed: 1 = the skill used it (place / launch / dive in)
static int sk_fire(int a) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a];
	if (A->state == SKS_AIM) {
		if (A->skill == SK_ARMLAUNCHER) { if (A->t >= SK_AL_IN - 0.1f) sk_launcher_fire(a); return 1; }
		V3 p, n; if (!sk_place_point(a, &p, &n)) { if (a == sk_local) ev_play("intel_skill/fake_set_alert", NULL); return 1; }
		sk_place(a, p, n); return 1;
	}
	if (A->post_t > 0) return 1;   // the launcher arm is folding back
	if (0 && A->state == SKS_ACTIVE && A->skill == SK_CYBERDIVE) {   // (v1: dive into the enemy closest to the view centre; b84 v2.0 Net Jump has no target)
		V3 d = sk_dir(a); int best = -1; float bc = cosf(15 * 3.14159f / 180);
		for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && sk_hostile(a, b)) {
			V3 v = vsub(sk_chest(b), A->cam); float l = sk_len(v); if (l > sk_p2(a) || l < 1) continue;
			float c = vdot(v, d) / l; if (c > bc) { bc = c; best = b; } }
		if (best >= 0) sk_hack(a, best);
		return 1;
	}
	return 0;
}
static void sk_hack(int a, int b) {   // Reiko's dive lands in b: hacked (blind, no fire, no skills)
	if (SK_MIRRORED) return;
	SkillActor *B = &SKA[b];
	B->hack_t = sk_p1(a); sk_cancel(b);
	sk_fx_up(SKF_DIVE_HIT, sk_pos(b));
	if (a == sk_local) { ev_play("intel_skill/cyberdive_end_fp", NULL); SKV.flash = 0.25f; sk_notify(a, SKN_HIT, NULL, 0); }
	if (b == sk_local) { SKV.laugh = B->hack_t; SKV.glitch = SKV.glitch_len = 0.6f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; sk_warn(SK_CYBERDIVE); }
	sk_end_active(a, 0);
}

// ---- hit tests: walls, shields, deployables (for bullets, bot shots, lasers)
static float sk_ray_wall(int o, V3 from, V3 d, float maxd) {
	const SkObj *O = &SKO[o]; if (O->phase == 0) return -1;
	float dn = vdot(d, O->nrm); if (fabsf(dn) < 1e-4f) return -1;
	float t = vdot(vsub(O->pos, from), O->nrm) / dn; if (t < 0 || t > maxd) return -1;
	V3 h = vadd(from, vmul(d, t)), rt = v3(-O->nrm.y, O->nrm.x, 0); float lat = vdot(vsub(h, O->pos), rt), up = h.z - O->pos.z;
	return fabsf(lat) <= O->w * 0.5f && up >= 0 && up <= O->h ? t : -1;
}
// the plasma shield actor a holds up: 0 none, 1 own (Tier 1 small panel / Tier 2 big panel), 2 a downloaded Tier-1 panel
static int sk_shield_kind(int a) {
	if (!sk_valid(a)) return 0; const SkillActor *A = &SKA[a];
	if (A->skill == SK_PLASMASHIELD && A->state == SKS_ACTIVE && A->toggled) return 1;
	if (A->sx_skill == SK_PLASMASHIELD && A->sx_t > 0 && A->sx_energy > 0) return 2;
	return 0;
}
static int sk_shield_big(int a) { return sk_shield_kind(a) == 1 && SKA[a].tier == 2; }
// where the panel is: the game's Shield video (first person, fitted to the rim, tools/skills/SKILLS.md 3.7) puts the NIF's origin
// 50.8 cm in front of the eye and 21.4 cm below it, centred on the view line, facing the body yaw (the view pitch does not tilt it)
#define SK_SH_FWD 50.8f
#define SK_SH_DOWN 21.4f
// b52 (user: "the tip of the gun seemed to clip through the shield in third person"): a bot / ally's panel sits 8 cm past its gun's
// muzzle when the gun reaches further than 50.8 cm (a rifle held at the shoulder does: 60-95 cm), smoothed (skills tick); the
// player's own panel keeps the first-person fit. Hits and the break use the same place.
#define SK_SH_MUZZLE_GAP 8.0f
#define SK_SH_FWD_MAX 110.0f
static float sk_shield_fwd_target(int a) {
	if (a == 0 || !SKW_.muzzle) return SK_SH_FWD;
	V3 e = sk_eye(a), m = SKW_.muzzle(a); float y = sk_yaw(a), d = (m.x - e.x) * cosf(y) + (m.y - e.y) * sinf(y) + SK_SH_MUZZLE_GAP;
	return d < SK_SH_FWD ? SK_SH_FWD : d > SK_SH_FWD_MAX ? SK_SH_FWD_MAX : d;
}
static V3 sk_shield_origin(int a) { V3 e = sk_eye(a); float y = sk_yaw(a), fw = a && SKA[a].sh_fwd > 0 ? SKA[a].sh_fwd : SK_SH_FWD; return v3(e.x + cosf(y) * fw, e.y + sinf(y) * fw, e.z - SK_SH_DOWN); }
static void sk_shield_frame(int a, V3 *c, V3 *f, V3 *r, float *hw, float *hh) {   // panel OBB, from the game's tier1 / tier2 panel meshes
	float y = sk_yaw(a); *f = v3(cosf(y), sinf(y), 0); *r = v3(sinf(y), -cosf(y), 0);
	*c = sk_shield_origin(a); int big = sk_shield_big(a); *hw = SKSH_DIM[big][0]; *hh = SKSH_DIM[big][1];   // tier1 81 x 101 cm, tier2 86 x 153 cm
}
static float sk_ray_shield(int a, V3 from, V3 d, float maxd) {
	if (!sk_shield_kind(a) || !sk_alive(a)) return -1;
	V3 c, f, r; float hw, hh; sk_shield_frame(a, &c, &f, &r, &hw, &hh);
	const V3 ax[3] = { r, f, v3(0, 0, 1) }; const float he[3] = { hw, 10, hh }; float t0 = 0, t1 = maxd; V3 o = vsub(from, c);
	for (int k = 0; k < 3; k++) { float e = vdot(o, ax[k]), g = vdot(d, ax[k]);
		if (fabsf(g) < 1e-6f) { if (fabsf(e) > he[k]) return -1; continue; }
		float ta = (-he[k] - e) / g, tb = (he[k] - e) / g; if (ta > tb) { float s = ta; ta = tb; tb = s; }
		if (ta > t0) t0 = ta; if (tb < t1) t1 = tb; if (t0 > t1) return -1; }
	return t0;
}
static V3 sk_obj_center(int o) { const SkObj *O = &SKO[o]; switch (O->type) { case SKO_TURRET: return vadd(O->pos, v3(0, 0, 60)); case SKO_DRONE: return vadd(O->cur, v3(0, 0, 30));
	case SKO_RECON: return vadd(O->pos, v3(0, 0, 20)); case SKO_EMP: return vadd(O->pos, v3(0, 0, 30)); case SKO_SEEKER: return vadd(O->cur, v3(0, 0, 20)); default: return vadd(O->pos, v3(0, 0, O->h * 0.5f)); } }
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
	for (int a = 0; a < SK_MAXACT; a++) if (a != self && sk_valid(a) && SKA[a].team != team && sk_shield_kind(a)) {
		float t = sk_ray_shield(a, from, d, best); if (t >= 0 && t < best) { best = t; *kind = 2; *idx = a; } }
	return *kind ? best : -1;
}
static void sk_shield_absorb(int a, V3 hp, V3 d, float dmg) {
	SkillActor *A = &SKA[a]; int k = sk_shield_kind(a); if (!k) return;
	const SkillDef *D = &SK_DEF[SK_PLASMASHIELD];
	if (k == 1) A->energy -= dmg / D->p1[sk_ti(a)]; else A->sx_energy -= dmg / D->p1[2];
	V3 n = vmul(d, -1); sk_fx(SKF_SH_HIT, hp, n, &d);
	if (A->fx_t <= 0) { sk_ev_at("intel_skill/shield_dmg", hp); A->fx_t = 0.12f; }
	A->sh_hit = 0.3f;   // the panel's noise lines ('damage')
	if (k == 1 && A->energy <= 0) { A->energy = 0; sk_end_active(a, 1); }
	if (k == 2 && A->sx_energy <= 0) { A->sx_energy = 0; sk_sx_end(a, 1); }
}
// applies a hit found by sk_trace
static void sk_apply(int kind, int idx, V3 hp, V3 d, float dmg, int att) {
	if (kind == 1) { SkObj *O = &SKO[idx]; V3 n = vdot(d, O->nrm) < 0 ? O->nrm : vmul(O->nrm, -1); sk_fx(SKF_SH_HIT, hp, n, &d);
		if (O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/shield_dmg", hp); O->dmg_snd_t = 0.15f; } sk_obj_damage(idx, dmg, att); }
	else if (kind == 2) sk_shield_absorb(idx, hp, d, dmg);
	else if (kind == 3) { V3 n = vmul(d, -1); fx_impact_nodecal(2, &hp.x, &n.x, &d.x); sk_obj_damage(idx, dmg, att); }   // (b87: no hole left in the air)
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
	SkObj *O = &SKO[o]; if (!O->alive || SK_MIRRORED) return; V3 c = sk_obj_center(o);
	if (destroyed) {
		sk_fx_up(SKF_DESTROY, c);
		static const char *DS[8] = { "", "intel_skill/turret_hacking_destroyed", "intel_skill/screen_exp", "intel_skill/dmd_destroyed", "intel_skill/mini_tachikoma_exp", "intel_skill/mini_tachikoma_destroyed", "", "intel_skill/mini_tachikoma_exp" };
		static const char *NT[8] = { "", "SENTRY DESTROYED", "BARRIER DESTROYED", "DRONE DESTROYED", "EMP DESTROYED", "RECON DESTROYED", "", "SEEKER DRONE DESTROYED" };   // (b85: 6 decoy, 7 seeker)
		sk_ev_at(DS[O->type], c);
		if (attacker >= 0) { if (attacker == sk_local) sk_notify(attacker, SKN_KILL, NT[O->type], 50); else sk_give_intel(attacker, 50, NT[O->type]); }
	}
	{ int mi = O->type == SKO_TURRET ? SKM_TURRET : O->type == SKO_RECON ? SKM_MINI : O->type == SKO_EMP ? SKM_EMP : -1, ca = destroyed ? SKA_DESTROY : SKA_UNDEPLOY, g = -1;
	  if (mi >= 0 && sk_rest_ok[mi] && sk_clip[mi][ca] >= 0 && O->phase >= 1) for (int k = 0; k < 4; k++) if (!SKG[k].on) { g = k; break; }
	  if (g >= 0) { SkGhost *G = &SKG[g]; memset(G, 0, sizeof(*G)); G->on = 1; G->mi = mi; G->clip = ca; G->tier = O->tier; G->len = model_clip_len(&sk_mdl[mi], sk_clip[mi][ca]);
		G->hold = destroyed ? 1.2f : 0; G->unbuild = 1; G->rz = O->yaw0 + 1.5707963f; G->yaw = sk_wrap(O->yaw - O->yaw0); G->pitch = O->pitch; G->pos = O->pos; memcpy(G->lit, O->lit, 12); }
	  else if (!destroyed) sk_fx_up(SKF_UNBUILD, O->pos); }
	if (O->type == SKO_RECON) { sk_ev_at("intel_skill/noise_scan_end", c); fade_others("is_noise_scan_loop", -1, 300); }
	if (O->type == SKO_WALL && !destroyed) sk_sfx("is_screen_off", c, 220);
	if (O->type == SKO_TURRET && !destroyed) sk_ev_at("intel_skill/turret_hacking_end", c);
	sk_obj_free(o);
}
static void sk_seeker_explode(int o, int kill_only);
static void sk_obj_damage(int o, float dmg, int att) {
	SkObj *O = &SKO[o]; if (!O->alive || dmg <= 0) return;
	if (O->type == SKO_SEEKER && (att < 0 || att >= SK_MAXACT || SKA[att].team != O->team) && !sk_mirror && (O->hp -= dmg) <= 0) { if (att >= 0 && att < SK_MAXACT) { if (att == sk_local) sk_notify(att, SKN_KILL, "SEEKER DRONE DESTROYED", 50); else sk_give_intel(att, 50, "SEEKER DRONE DESTROYED"); } sk_seeker_explode(o, 1); return; }   // b85
	if (O->type == SKO_DECOY) { O->hit_t = 0.3f; return; }   // b84: "Holograms cannot be destroyed"
	if (att >= 0 && att < SK_MAXACT && SKA[att].team == O->team) return;
	if (!sk_mirror) O->hp -= dmg; O->hit_t = 0.3f;
	if (O->type == SKO_TURRET && O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/turret_hacking_dmg", sk_obj_center(o)); O->dmg_snd_t = 0.25f; }
	if (O->type == SKO_DRONE && O->dmg_snd_t <= 0) { sk_ev_at("intel_skill/dmd_dmg", sk_obj_center(o)); O->dmg_snd_t = 0.25f; }
	if (O->phase == 1 && O->clip != SKA_DESTROY) { O->clip = SKA_DAMAGE; O->clip_t = 0; }
	if (O->hp <= 0) sk_obj_end(o, 1, att);
}
static int sk_cloaked(int a);
// what a deployable's sensor sees: camo hides beyond the near-reveal distance unless she just fired
static int sk_sensor_sees(V3 p, int b) {
	const SkillActor *B = &SKA[b];
	if (sk_cloaked(b) && B->fire_t > 1.0f && sk_len(vsub(sk_pos(b), p)) > SK_DEF[SK_OPTICAL].p1[0]) return 0;
	return 1;
}
static V3 sk_target_pos(int t) { return t >= SK_TGT_RANGE ? (sk_rg_chest ? sk_rg_chest(t - SK_TGT_RANGE) : v3(0, 0, 0)) : t >= SK_TGT_OBJ ? sk_obj_center(t - SK_TGT_OBJ) : sk_chest(t); }
static int sk_target_ok(int t, int team) { return t >= SK_TGT_RANGE ? sk_rg_up && sk_rg_up(t - SK_TGT_RANGE) : t >= SK_TGT_OBJ ? t - SK_TGT_OBJ < SK_MAXOBJ && sk_obj_hostile(t - SK_TGT_OBJ, team) : t >= 0 && sk_alive(t) && SKA[t].team != team; }
static void sk_hit_target(int t, float dmg, int att, int kind) { if (t >= SK_TGT_RANGE) { if (sk_rg_hurt) sk_rg_hurt(t - SK_TGT_RANGE, dmg, sk_target_pos(t), v3(0, 0, -1)); } else if (t >= SK_TGT_OBJ) sk_obj_damage(t - SK_TGT_OBJ, dmg, att); else if (t >= 0) sk_damage(t, dmg, att, kind); }
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
static void sk_grenade_launch(int o, V3 from, V3 to) {   // Tier-2 sentry: lobbed grenade
	if (SK_MIRRORED) return;
	int s = -1; for (int m = 0; m < SK_MAXMS; m++) if (!SKMS[m].on) { s = m; break; } if (s < 0) return;
	SkMissile *M = &SKMS[s]; memset(M, 0, sizeof(*M)); M->on = 1; M->owner = SKO[o].owner; M->team = SKO[o].team; M->kind = 1; M->target = -1; M->p = from; M->dmg = 60;
	float hd = sk_hdist(to, from), T = fmaxf(0.3f, hd / 1000); V3 h = sk_norm(v3(to.x - from.x, to.y - from.y, 0));
	M->v = v3(h.x * hd / T, h.y * hd / T, (to.z - from.z + 0.5f * 980 * T * T) / T); sk_ms_born(M);
	sk_ev_at("intel_skill/turret_rocket_fire", from);
}
static const float SK_SENT_RANGE[2] = { 2500, 3500 };   // b85 (v2.0 EP18 DETAILS: "Tier 2: Increases detection range and HP"): the sentry's reach
static void sk_update_turret(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.4f; sk_fx_up(SKF_BUILD, O->pos); }
		if (O->t >= 2) { O->phase = 1; O->clip = SKA_IDLE; O->clip_t = 0; sk_ev_at("intel_skill/turret_hacking_start", O->pos); } return; }
	float arc = SK_DEF[SK_SENTRYGUN].p1[O->tier >= 2 ? 1 : 0];
	V3 fwd = v3(cosf(O->yaw0), sinf(O->yaw0), 0), laser = sk_turret_att(o, "laser");
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.2f;   // 5 Hz target search
		int t = sk_obj_pick(o, laser, SK_SENT_RANGE[O->tier >= 2 ? 1 : 0], 0, arc >= 359 ? -2 : cosf(arc * 0.5f * 3.14159f / 180), fwd);
		if (t >= 0 && t != O->target) { O->spin_t = 0.4f; if (O->clip != SKA_DAMAGE) { O->clip = SKA_FIRE_START; O->clip_t = 0; } sk_ev_at("intel_skill/turret_hacking_alarm", O->pos); V3 tp = sk_target_pos(t); sk_fx(SKF_TU_LOCK, laser, sk_norm(vsub(tp, laser)), NULL);
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
			if (O->clip != SKA_FIRE && O->clip != SKA_DAMAGE) { O->clip = SKA_FIRE; O->clip_t = 0; }
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
			if (O->tier >= 2) { O->gren_t -= dt; if (O->gren_t <= 0) { O->gren_t = 6; V3 g = sk_turret_att(o, "gren"), tp = sk_target_pos(O->target);
				sk_grenade_launch(o, g, tp); sk_grenade_launch(o, g, vadd(tp, v3((sk_rnd() - 0.5f) * 150, (sk_rnd() - 0.5f) * 150, 0))); } }
		}
	} else { O->fire_t = 0; if (O->clip == SKA_FIRE || O->clip == SKA_FIRE_START) { O->clip = sk_clip[SKM_TURRET][SKA_FIRE_END] >= 0 ? SKA_FIRE_END : SKA_IDLE; O->clip_t = 0; } }
}
static void sk_update_drone(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { float k = sk_clamp(O->t / 1.5f, 0, 1); k = k * k * (3 - 2 * k);
		O->cur = vadd(O->from, vmul(vsub(O->pos, O->from), k)); if (O->t >= 1.5f) { O->phase = 1; O->loop_t = 0; O->clip = SKA_IDLE; O->clip_t = 0; } return; }
	if (O->phase == 2) { O->cur.z += 900 * dt; if (O->t - O->end_t > 1.0f && !sk_mirror) sk_obj_free(o); return; }
	O->cur = vadd(O->pos, v3(0, 0, sinf(O->t * 2.1f) * 15));
	int ti = O->tier >= 2 ? 1 : 0;
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.2f; int t = sk_obj_pick(o, vadd(O->cur, v3(0, 0, 10)), 0, O->r > 0 ? O->r : 900, -2, v3(1, 0, 0));
		if (t >= 0 && t != O->target) { O->fx_t = 0; O->loop_t = 0; if (t == sk_local) sk_warn(SK_DMDDRONE); }
		if (t < 0 && O->target >= 0) fade_others("is_dmd_heat_loop_01", -1, 200);
		O->target = t; }
	if (O->target >= 0 && !sk_target_ok(O->target, O->team)) O->target = -1;
	O->loop_t -= dt;
	float life = SK_DEF[SK_DMDDRONE].duration[ti];   // energy: life s (b83: idle or not)
	if (O->target >= 0) {
		V3 tp = sk_target_pos(O->target), v = vsub(tp, O->cur); O->yaw += sk_clamp(sk_wrap(atan2f(v.y, v.x) - O->yaw), -4 * dt, 4 * dt);
		O->energy -= 1 / life * dt;   // b83 (v2.0, EP23 2:50.5-3:10.5): a steady 1 s-step drain, fighting or not (was 4x while lasering)
		if (O->loop_t <= 0) { O->loop_t = 4.0f; sk_sfx("is_dmd_heat_loop_01", O->cur, 200); }
		O->fx_t -= dt;
		while (O->fx_t <= 0) { O->fx_t += 0.1f;   // the laser in 0.1 s ticks
			V3 from = vadd(O->cur, v3(0, 0, 11)), end = tp; float dmg = SK_DEF[SK_DMDDRONE].p1[ti] * 0.1f;
			if (!sk_shot_block(O->owner, O->team, from, tp, dmg, &end)) { sk_hit_target(O->target, dmg, O->owner, SKD_LASER);
				if (O->target < SK_TGT_OBJ) { SKA[O->target].laser_t = 0.3f; if (SKW_.heal && sk_alive(O->owner)) SKW_.heal(O->owner, dmg * 0.25f, 1.0f);   // b83 (v2.0 Siphon Drone): it heals Sitara a little
					if (O->target == sk_local) { SKV.vig = SKV.vig_len = 0.3f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.1f; SKV.vig_c[2] = 0.05f; } } }
			V3 bd = sk_norm(vsub(end, from)), n = vmul(bd, -1); sk_fx(SKF_DL_HIT, end, n, &bd);
			// b83 (v2.0, the game's DETAILS text: "attacks all enemies near it with high-intensity heat"): the other operators in its
			// range with a clear line from the drone burn too (every third tick, the same damage per second), each one heals Sitara
			static unsigned dr_tick; if (++dr_tick % 3 == 0) for (int b = 0; b < SK_MAXACT; b++) if (b != O->target && sk_alive(b) && SKA[b].team != O->team) {
				V3 cb = sk_chest(b); if (sk_len(vsub(cb, O->cur)) > (O->r > 0 ? O->r : 900) || (SKW_.los && !SKW_.los(from, cb))) continue;
				V3 e2 = cb; if (sk_shot_block(O->owner, O->team, from, cb, dmg * 3, &e2)) continue;
				sk_damage(b, dmg * 3, O->owner, SKD_LASER); SKA[b].laser_t = 0.3f; if (SKW_.heal && sk_alive(O->owner)) SKW_.heal(O->owner, dmg * 0.75f, 1.0f);
				V3 bd2 = sk_norm(vsub(cb, from)), n2 = vmul(bd2, -1); sk_fx(SKF_DL_HIT, cb, n2, &bd2);
				if (b == sk_local) { SKV.vig = SKV.vig_len = 0.3f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.1f; SKV.vig_c[2] = 0.05f; } }
		}
	} else { O->energy -= 1 / life * dt; O->yaw += 0.4f * dt; if (O->loop_t <= 0) { O->loop_t = 2.4f; sk_sfx(sk_rnd() < 0.5f ? "is_dmd_loop_03" : "is_dmd_loop_04", O->cur, 150); } }
	if (O->energy <= 0) { O->energy = 0; O->phase = 2; O->end_t = O->t; O->target = -1; sk_ev_at("intel_skill/dmd_end", O->cur); fade_others("is_dmd_heat_loop_01", -1, 200); }
}
// b85 EMP GENERATOR (Kuro, v2.0 EP25 DETAILS + the user's Steam guides "Guide on everything First Assault" / "A General Guide
// To FA 2.0"): placed like v1's device, it charges for 4 s with its blast radius shown on the floor ("slow detonation times
// mean players can escape the field"), then goes off: enemies inside are stunned (no skills, half aim, slow walk, the view
// barely turns - "only able to look one way, and walking slowly, but you can still fire") for 4 s (Tier 2: 7 s) and marked
// for Kuro's side; enemy barriers, drones, sentries, recon units and seekers in it are destroyed, missiles fall. Tier 1's
// radius is small (5 m: "negligible"), Tier 2's huge (25 m: "a large portion of the map"); the generator can be shot.
static void sk_seeker_explode(int o, int kill_only);
static void sk_update_emp(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) {
		O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.1f; sk_fx_up(SKF_EMP_CHG, vadd(O->pos, v3(0, 0, 45))); }
		O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = O->life - O->t < 1.5f ? 0.25f : 0.6f; sk_sfx("is_emp_beep", O->pos, 200); }   // (beeps faster at the end)
		if (O->t >= O->life) {   // the blast
			O->phase = 1; O->end_t = O->t; O->clip = SKA_IDLE; O->clip_t = 0; V3 c = vadd(O->pos, v3(0, 0, 60)); sk_fx_up(SKF_EMP_EXP, c); sk_ev_at("intel_skill/emp_exp", c);
			float stun = SK_DEF[SK_EMP].p2[O->tier >= 2 ? 1 : 0];
			for (int k = 0; k < SK_MAXOBJ; k++) if (k != o && sk_obj_hostile(k, O->team) && sk_len(vsub(sk_obj_center(k), c)) < O->r) { if (SKO[k].type == SKO_SEEKER) sk_seeker_explode(k, 1); else if (SKO[k].type != SKO_DECOY) sk_obj_end(k, 1, O->owner); }
			for (int m = 0; m < SK_MAXMS; m++) if (SKMS[m].on && SKMS[m].team != O->team && sk_len(vsub(SKMS[m].p, c)) < O->r) { sk_fx_up(SKF_DESTROY, SKMS[m].p); SKMS[m].on = 0; }
			for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team && sk_len(vsub(sk_chest(b), c)) < O->r && (O->tier >= 2 || sk_los(c, sk_chest(b)))) {
				SkillActor *B = &SKA[b]; sk_cancel(b); B->emp_t = fmaxf(B->emp_t, stun); B->stun_t = fmaxf(B->stun_t, stun); B->shock_t = 0; if (B->state == SKS_READY) B->state = SKS_COOLDOWN;
				int tm = O->team & 1; B->mark_t[tm] = fmaxf(B->mark_t[tm], stun); B->mark_by[tm] = (signed char)O->owner;   // ("marked with an icon only Kuro can see")
				sk_damage(b, 10, O->owner, SKD_SHOCK);
				if (b == sk_local) { SKV.glitch = SKV.glitch_len = stun; SKV.glitch_c[0] = 0.4f; SKV.glitch_c[1] = 0.6f; SKV.glitch_c[2] = 1; sk_warn(SK_EMP); } }
		}
	} else if (O->t - O->end_t >= 1.0f) sk_obj_end(o, 0, -1);
}
static void sk_update_wall(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { O->fx_t -= dt; if (O->fx_t <= 0) { O->fx_t = 0.4f; V3 rt = v3(-O->nrm.y, O->nrm.x, 0); for (int k = 0; k < 5; k++) sk_fx_up(SKF_BUILD, vadd(O->pos, vmul(rt, (k - 2) * O->w * 0.22f))); }
		if (O->t >= 2) { O->phase = 1; sk_sfx("is_screen_set2", O->pos, 200); O->loop_t = 0.5f; } }
	else { O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = 5.1f; sk_sfx("is_screen_loop", vadd(O->pos, v3(0, 0, 100)), 120); } }
	V3 rt = v3(-O->nrm.y, O->nrm.x, 0);
	for (int b = 0; b < SK_MAXACT; b++) {   // someone walking through: the barrier's shimmer on him (b83, v2.0 Therm-Optic Barrier: no hack, no reveal - v1's Cyber Wall did)
		if (!sk_alive(b)) { O->side[b] = 0; continue; }
		V3 p = vadd(sk_pos(b), v3(0, 0, 100)), q = vsub(p, O->pos); float sd = vdot(q, O->nrm); signed char s = sd > 0 ? 1 : -1;
		int inside = fabsf(vdot(q, rt)) <= O->w * 0.5f + 30 && q.z > -100 && q.z < O->h + 50;
		if (O->side[b] && s != O->side[b] && inside && O->phase == 1) { sk_fx_up(SKF_WALL_PASS, sk_pos(b)); sk_ev_at("intel_skill/screen_through", p);
			if (SKA[b].team != O->team) { SkillActor *B = &SKA[b]; if (B->emp_t <= 0) sk_cancel(b); B->emp_t = fmaxf(B->emp_t, 3); if (B->state == SKS_READY) B->state = SKS_COOLDOWN; }   // b85 (the guides: "any enemy who passes through it will be applied an EMP effect for a time")
			if (b == sk_local && SKA[b].team != O->team) { SKV.glitch = SKV.glitch_len = 0.4f; SKV.glitch_c[0] = 0.85f; SKV.glitch_c[1] = 0.25f; SKV.glitch_c[2] = 0.2f; } }   // (EP22 skill video 0:42-0:44: red squares crossing an enemy barrier)
		O->side[b] = s;
	}
}
static void sk_update_recon(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { if (O->t >= 1.0f) { O->phase = 1; if (!sk_rest_ok[SKM_MINI]) { O->clip = SKA_IDLE; O->clip_t = 0; } sk_ev_at("intel_skill/noise_scan_start", O->pos); sk_sfx("is_noise_scan_loop", O->pos, 160); O->scan_t = 0; } return; }
	O->scan_t -= dt;
	if (O->scan_t <= 0) { O->scan_t = 0.5f; sk_fx_up(SKF_RE_PING, vadd(O->pos, v3(0, 0, 30)));   // noise scan: running or shooting enemies get marked
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team) {
			SkillActor *B = &SKA[b]; V3 v = sk_vel(b);
			if (sk_hdist(sk_pos(b), O->pos) > O->r || (sqrtf(v.x * v.x + v.y * v.y) <= 250 && B->fire_t > 0.5f)) continue;
			B->mark_t[O->team & 1] = 3; B->mark_by[O->team & 1] = (signed char)O->owner;
			if (!(O->marked >> b & 1)) { O->marked |= 1u << b;
				if (O->owner == sk_local) ev_play("intel_skill/noise_scan_triangle", NULL);
				B->warn_t = 3;
				if (b == sk_local) { voice_play("d_op_warn_specialrecon", 230, 0); SKV.vig = SKV.vig_len = 0.6f; SKV.vig_c[0] = 1; SKV.vig_c[1] = 0.15f; SKV.vig_c[2] = 0.1f; } }
		}
	}
}
// ---- b85 SEEKER DRONE (Togusa, v2.0 EP17 3:00-3:03, DETAILS: "Releases a hunter drone that explodes on contact with an enemy.
// Drones can be destroyed by enemies. Tier 2: Releases 3 drones and adds a scrambler effect"): the hand projects it (~0.5 s),
// a small white drone with a cyan glow flies off at chest height after the nearest enemy (9 m/s, homing, sliding along walls)
// and shows him to Togusa's team while it hunts (3:01.9: the target outlined through the wall); on contact a cyan blast
// (3:02.9-3:03.1): 70 at the centre, 20 at 3 m; Tier 2 drones also scramble (no skills 4 s, half aim, the HUD broken up).
// 40 HP (bots shoot it; an EMP Generator kills it). After 7 s without contact it pops where it is. The target hears and sees
// "SEEKER DRONE INCOMING" (EP16 7:55.5).
// b117 (user: "make sure togusa's seeker drones function, look and play correctly with all of their effects ... seek out and
// explodes next to enemies. The explosion also produces a flash effect blinding enemies that are not killed"; the wiki's tips:
// "shoot the drones or try to bait their delayed detonation ... wait till it stops to play the detonate animation and sprint
// jump away"). Looked at again in the v2.0 footage (EP17 3:01.4-3:03.2, frame by frame) and in the game's own skill video
// (ui.dat video\skill\SUMMON.usm, played in v2.0's DETAILS window at EP17 0:25-0:57): b85's small flying hull with a line to
// the floor was a misreading. The drone is the game's MINI TACHIKOMA (model\Npc\CB_SPIDERMINE, the smalltachi rig and clips -
// sk_minitachi.gvm, in the data since b63): the arm device projects it onto the FLOOR at the aimed spot (a thin beam from the
// hand, a glowing cap, white blocks, a ring; 0.35 s), then it RUNS along the ground to the nearest enemy, stops beside it,
// leaps (its jump clip = the "detonate animation", 0.5 s: the delayed detonation one can run from) and bursts: the blast, and
// a flash that blinds who survives it in the blast's reach. Tier 2: three of them (the skill video's SEEKER KILL, SEEKER
// KILL, TRIPLE KILL; the DETAILS text "Releases 3 drones and adds a scrambler effect") - the wiki's "two" is an older version.
// Phases: 0 materialising, 1 hunting, 2 armed. O->from: the hand (the beam) / then the next waypoint; O->spin_t: until the
// next path; O->fire_t: the fuse; O->nrm: the velocity.
static int (*sk_nav_path)(const float *s, const float *g, float (*out)[3], int maxp, float margin);   // main.c: nav.h's (NULL: straight at the target)
#define SK_SEEK_SPEED 900.0f
#define SK_SEEK_FUSE 0.5f
#define SK_SEEK_BUILD 0.5f     /* (SUMMON.usm frames 69-84: red cap, white, the white blocks) */
#define SK_SEEK_BOOT 1.3f      /* (frames 84-123: it stands, its dish blinking white / cyan, then runs) */
#define SK_SEEK_RELEASE 1.6f   /* (frames 21-69: after the hand's gesture) */
#define SK_SEEK_BLIND 1.6f
static int sk_seeker_new(int owner) {   // (several at once: no one-per-operator rule)
	int s = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (!SKO[o].alive) { s = o; break; }
	if (s < 0) return -1;
	SkObj *O = &SKO[s]; memset(O, 0, sizeof(*O)); O->ser = ++sk_ser_n ? sk_ser_n : ++sk_ser_n; O->type = SKO_SEEKER; O->owner = owner; O->team = SKA[owner].team; O->tier = SKA[owner].tier; O->alive = 1; O->target = -1;
	O->lit[0] = O->lit[1] = O->lit[2] = 1; O->clip = SKA_IDLE; O->life = 7 + SK_SEEK_BUILD + SK_SEEK_BOOT; O->hp = O->hpmax = 40; O->phase = 0;   /* (7 s of hunting, as b85) */
	return s;
}
static void sk_seeker_explode(int o, int kill_only) {   // kill_only: shot down / EMP'd (a small pop, no damage)
	SkObj *O = &SKO[o]; if (!O->alive || SK_MIRRORED) return; V3 c = O->cur;
	c.z += 25;
	if (kill_only) { sk_fx_up(SKF_DESTROY, c); sk_ev_at("intel_skill/mini_tachikoma_destroyed", c);
		if (sk_rest_ok[SKM_MINI] && sk_clip[SKM_MINI][SKA_DESTROY] >= 0 && O->phase >= 1) for (int k = 0; k < 4; k++) if (!SKG[k].on) {   // it falls apart where it stood
			SkGhost *G = &SKG[k]; memset(G, 0, sizeof(*G)); G->on = 1; G->mi = SKM_MINI; G->clip = SKA_DESTROY; G->tier = O->tier; G->len = model_clip_len(&sk_mdl[SKM_MINI], sk_clip[SKM_MINI][SKA_DESTROY]);
			G->hold = 1.2f; G->unbuild = 1; G->rz = O->yaw + 1.5707963f; G->pos = O->cur; memcpy(G->lit, O->lit, 12); break; }
		sk_obj_free(o); return; }
	sk_fx_up(SKF_EMP_EXP, c); sk_fx_up(SKF_DESTROY, c); sk_ev_at("intel_skill/mini_tachikoma_exp", c);
	float dmax = SK_DEF[SK_SPECIALRECON].p2[O->tier >= 2 ? 1 : 0];
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team) { float d = sk_len(vsub(sk_chest(b), c)); if (d > 300 || !sk_los(c, sk_chest(b))) continue;
		sk_damage(b, dmax - (dmax - 20) * d / 300, O->owner, SKD_BLAST); sk_fx_up(SKF_EMP_SHOCK, sk_chest(b));
		if (O->tier >= 2) { SkillActor *B = &SKA[b]; if (B->emp_t <= 0) sk_cancel(b); B->emp_t = fmaxf(B->emp_t, 4); if (B->state == SKS_READY) B->state = SKS_COOLDOWN;   // the scrambler
			if (b == sk_local) { SKV.glitch = SKV.glitch_len = 4; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 0.85f; SKV.glitch_c[2] = 1; } }
		else if (b == sk_local) { SKV.glitch = SKV.glitch_len = 0.5f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 0.85f; SKV.glitch_c[2] = 1; }
		if (sk_alive(b)) { SKA[b].blind_t = fmaxf(SKA[b].blind_t, SK_SEEK_BLIND); if (b == sk_local) SKV.blind = SKV.blind_len = SK_SEEK_BLIND; } }   // b117: "a flash effect blinding enemies that are not killed"
	sk_obj_free(o);
}
static void sk_update_seeker(int o, float dt) {
	SkObj *O = &SKO[o];
	if (O->phase == 0) { if (O->t > SK_SEEK_BUILD) { O->phase = 1; O->clip = SKA_WALK; O->clip_t = 0; O->spin_t = 0; O->from = O->cur; sk_ev_at("intel_skill/mini_tachikoma_stand_up", O->cur); } return; }   // the hand projects it
	if (O->phase == 2) {   // armed: it has stopped and leaps - the delayed detonation
		O->fire_t += dt; O->clip_t += dt; O->cur.z += (O->fire_t < SK_SEEK_FUSE * 0.6f ? 90 : -20) * dt;
		O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = 0.12f; sk_sfx("is_emp_beep", O->cur, 220); }
		if (O->target >= 0 && O->target == sk_local) SKV.seek_t = fmaxf(SKV.seek_t, 0.4f);
		if (O->fire_t >= SK_SEEK_FUSE) sk_seeker_explode(o, 0);
		return; }
	if (O->t > O->life) { sk_seeker_explode(o, 0); return; }   // the tracking time is over
	O->scan_t -= dt; O->clip_t += dt; int boot = O->t < SK_SEEK_BUILD + SK_SEEK_BOOT;   // (b117) it stands where it was projected first, its dish blinking
	if (O->scan_t <= 0 || O->target < 0 || !sk_alive(O->target) || SKA[O->target].team == O->team) { O->scan_t = 0.4f; int best = -1; float bd = 1e9f;   /* (b117: the nearest enemy wherever it is - it seeks them out; its 7 s are what limits it. Marked and warned only inside 40 m, below) */
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != O->team && !sk_is_active(b, SK_CYBERDIVE)) { float d = sk_len(vsub(sk_chest(b), O->cur)); if (!sk_los(vadd(O->cur, v3(0, 0, 30)), sk_chest(b))) d *= 1.6f;
			for (int k = 0; k < SK_MAXOBJ; k++) if (k != o && SKO[k].alive && SKO[k].type == SKO_SEEKER && SKO[k].owner == O->owner && SKO[k].target == b) d *= 2.5f;   // Tier 2's three spread out (the skill video: SEEKER KILL, SEEKER KILL, TRIPLE KILL)
			if (d < bd) { bd = d; best = b; } }
		if (best != O->target) { O->spin_t = 0; O->energy = 0; }
		O->target = best; }
	V3 v = v3(0, 0, 0);
	if (O->target >= 0) { V3 tf = sk_pos(O->target), tc = sk_chest(O->target), d = vsub(tf, O->cur); float lh = sqrtf(d.x * d.x + d.y * d.y);
		SkillActor *T = &SKA[O->target]; int tm = O->team & 1;
		if (sk_len(vsub(tc, O->cur)) < 4000) {   // within 40 m: shown to Togusa's team while it hunts, and the hunted player is warned (once per drone and target)
			if (T->mark_t[tm] < 0.3f) { T->mark_t[tm] = 0.3f; T->mark_by[tm] = (signed char)O->owner; }
			if (O->target == sk_local) { if (O->energy == 0) { O->energy = 1; ev_play("intel_skill/mini_tachikoma_alarm", NULL); SKV.seek_t = 2.5f; } SKV.seek_t = fmaxf(SKV.seek_t, 0.4f); } }
		if (!boot && lh < 140 && fabsf(d.z) < 220 && sk_los(vadd(O->cur, v3(0, 0, 30)), tc)) {   // beside it: stop, leap, burst
			O->phase = 2; O->fire_t = 0; O->loop_t = 0; O->clip_t = 0; O->nrm = v3(0, 0, 0); if (lh > 1) O->yaw = atan2f(d.y, d.x); return; }
		O->spin_t -= dt;
		if (O->spin_t <= 0) { O->spin_t = 0.5f; O->from = tf;   // the next corner of the way there (the bots' navigation mesh), else straight at it
			if (sk_nav_path) { float sp[3] = { O->cur.x, O->cur.y, O->cur.z }, gp[3] = { tf.x, tf.y, tf.z }, path[8][3]; int n = sk_nav_path(sp, gp, path, 8, 30);
				for (int k = 0; k < n; k++) { float dx = path[k][0] - O->cur.x, dy = path[k][1] - O->cur.y; if (dx * dx + dy * dy > 60 * 60 || k == n - 1) { O->from = v3(path[k][0], path[k][1], path[k][2]); break; } } } }
		{ V3 w = vsub(O->from, O->cur); float wl = sqrtf(w.x * w.x + w.y * w.y); if (wl < 50) { w = d; wl = lh; O->spin_t = 0; }   // at the corner: straight on until the next path
		  if (wl > 1) v = v3(w.x / wl * SK_SEEK_SPEED, w.y / wl * SK_SEEK_SPEED, 0); } }
	if (boot) v = v3(0, 0, 0);
	float sp = sqrtf(v.x * v.x + v.y * v.y);
	if (sp > 1) { V3 dir = v3(v.x / sp, v.y / sp, 0), n; float h = raycast(vadd(O->cur, v3(0, 0, 28)), dir, sp * dt + 30, NULL, &n);
		if (h >= 0) { float vn = v.x * n.x + v.y * n.y; if (vn < 0) { v.x -= n.x * vn; v.y -= n.y * vn; } } }   // along the wall
	O->cur.x += v.x * dt; O->cur.y += v.y * dt;
	{ float g = raycast(vadd(O->cur, v3(0, 0, 60)), v3(0, 0, -1), 400, NULL, NULL);   // on the floor: up a step of 60 cm at once, down at a fall's speed
	  if (g >= 0) { float fz = O->cur.z + 60 - g; O->cur.z = fz >= O->cur.z ? fz : fmaxf(fz, O->cur.z - 1100 * dt); } else O->cur.z -= 1100 * dt; }
	if (O->cur.z < O->pos.z - 3000) { sk_seeker_explode(o, 1); return; }   // fell out of the map
	O->nrm = v; if (sp > 10) O->yaw = atan2f(v.y, v.x);
	O->loop_t -= dt; if (O->loop_t <= 0) { O->loop_t = 0.9f; sk_sfx("is_noise_scan_loop", O->cur, 110); }
}
static void sk_update_objs(float dt) {
	for (int o = 0; o < SK_MAXOBJ; o++) {
		SkObj *O = &SKO[o]; if (!O->alive) continue;
		if (!sk_valid(O->owner) || SKA[O->owner].team != O->team) { sk_obj_end(o, 0, -1); continue; }   // the owner left / changed sides
		O->t += dt; O->clip_t += dt; if (O->dmg_snd_t > 0) O->dmg_snd_t -= dt; if (O->hit_t > 0) O->hit_t -= dt; if (O->off_t > 0) O->off_t -= dt;
		if ((O->clip == SKA_DAMAGE && O->clip_t > 0.5f) || (O->clip == SKA_FIRE_END && O->clip_t > 1.0f)) { O->clip = SKA_IDLE; O->clip_t = 0; }
		switch (O->type) {
		case SKO_TURRET: if (O->off_t <= 0) sk_update_turret(o, dt); break;   // (b85: an enemy EMP Generator holds them)
		case SKO_DRONE: if (O->off_t <= 0 || O->phase != 1) sk_update_drone(o, dt); break;
		case SKO_EMP: sk_update_emp(o, dt); break;
		case SKO_WALL: sk_update_wall(o, dt); break;
		case SKO_RECON: if (O->off_t <= 0) sk_update_recon(o, dt); break;
		case SKO_SEEKER: sk_update_seeker(o, dt); break;
		}
		if (!O->alive) continue;
		if (O->type != SKO_EMP && O->type != SKO_DRONE && O->type != SKO_SEEKER && O->t >= O->life) sk_obj_end(o, 0, -1);
		else if (O->type == SKO_DRONE && O->t >= O->life && O->phase != 2) { O->phase = 2; O->end_t = O->t; sk_ev_at("intel_skill/dmd_end", O->cur); }
	}
}

// ---- missiles (arm launcher) and turret grenades
static void sk_explode(int m, V3 p, V3 n) {
	SkMissile *M = &SKMS[m]; M->on = 0;
	float R = 300, dmax = M->dmg > 0 ? M->dmg : M->kind ? 60 : 120, dmin = dmax * 0.25f;
	sk_fx(M->kind ? SKF_EXPLOSION : SKF_MS_EXP, p, n, NULL); sk_ev_at(M->kind ? "intel_skill/turret_rocket_exp" : "intel_skill/armlauncher_rocket_exp", p);
	V3 c = vadd(p, vmul(n, 10));
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != M->team) {
		float d = sk_len(vsub(sk_chest(b), p)); if (d > R || !sk_los(c, sk_chest(b))) continue;
		sk_damage(b, dmax - (dmax - dmin) * d / R, M->owner, SKD_BLAST); }
	for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, M->team) && sk_len(vsub(sk_obj_center(o), p)) < R) sk_obj_damage(o, M->kind ? 100 : 200, M->owner);
	if (sk_rg_blast) sk_rg_blast(c, R, dmax);   // b76: the shooting range's targets round the blast
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
		if (sk_rg_hit) { float rt; int ri = sk_rg_hit(M->p, d, h >= 0 ? fminf(h, step) : step, &rt);   // b76: a shooting range target in the way
			if (ri >= 0) { V3 hp = vadd(M->p, vmul(d, rt)); if (sk_rg_hurt) sk_rg_hurt(ri, M->dmg > 0 ? M->dmg : 120, hp, d); sk_explode(m, hp, vmul(d, -1)); continue; } }
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
		if (A->t > SK_DEF[SK_ARMLAUNCHER].duration[sk_ti(a)] && !sk_mirror) { sk_pay(a); A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; A->nlocks = 0; A->post_t = SK_AL_OUT; sk_ev(a, "intel_skill/armlauncher_off"); return; }
		if (A->t < SK_AL_IN - 0.1f) return;   // the launcher is still unfolding (footage: the red lens lights up at ~1.3 s)
		A->lock_t -= dt; int maxl = (int)sk_p1(a); if (maxl > 4) maxl = 4; if (maxl < 1) maxl = 1;
		for (int k = 0; k < A->nlocks; k++) if (!sk_target_ok(A->locks[k], A->team)) { A->locks[k] = A->locks[--A->nlocks]; k--; }
		if (A->lock_t > 0 || A->nlocks >= maxl) return;
		A->lock_t = 0.35f;   // a lock: the enemy nearest the view axis inside 12 deg, <= 40 m, in sight
		V3 e = sk_eye(a), d = sk_dir(a); int cand[SK_MAXACT + SK_MAXOBJ + 64]; float ca[SK_MAXACT + SK_MAXOBJ + 64]; int nc = 0; const float cmin = cosf(12 * 3.14159f / 180);
		int nrg = sk_rg_n ? sk_rg_n() : 0; if (nrg > 64) nrg = 64;   // b76: + the shooting range's targets
		for (int t = 0; t < SK_MAXACT + SK_MAXOBJ + nrg; t++) {
			int id = t < SK_MAXACT ? t : t < SK_MAXACT + SK_MAXOBJ ? SK_TGT_OBJ + t - SK_MAXACT : SK_TGT_RANGE + t - SK_MAXACT - SK_MAXOBJ; if (!sk_target_ok(id, A->team)) continue;
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
	if (A->t > 15 && !sk_mirror) { A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->t = 0; }   // forgotten preview: back to ready
}
// a heat-scan pulse from actor a: every enemy within r shows through walls to a's team for 1.6 s
static void sk_heat_pulse_do(int a, float r) {
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a); int found = 0;
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < r) {
		SKA[b].heat_t[A->team & 1] = 1.6f; SKA[b].heat_by[A->team & 1] = (signed char)a; found = 1;
		if (b == sk_local) { sk_warn(SK_HEATSCAN); ev_play("intel_skill/heat_scan", NULL); } }
	if (a == sk_local) { ev_play("intel_skill/heat_scan_woosh", NULL); if (found) ev_play("intel_skill/heat_scan_detect", NULL);
		SKV.heat = 0.4f; SKV.vig = SKV.vig_len = 0.4f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.35f; SKV.vig_c[2] = 0.15f; }
}
// rush (own or downloaded): energy drains fast while dashing; contact with an enemy while dashing = takedown. 0 = it ended
static int sk_rush_step(int a, float *energy, float dash_drain, float idle_drain, float dt) {
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a), v = sk_vel(a); float hs = sqrtf(v.x * v.x + v.y * v.y);
	A->dashing = hs > 700;   // sprinting at 2x
	*energy -= (A->dashing ? dash_drain : idle_drain) * dt;
	if (A->dashing && (sk_frame & 1) == 0) { V3 dd = v3(v.x / hs, v.y / hs, 0); sk_fx(SKF_RUSH, p, v3(0, 0, 1), &dd); }
	// b79 (v2.0, the Paz video EP19 and the game's DETAILS text: "boost speed, jump capability, and extend the range of melee
	// combat"): no takedown on contact any more (v1's Rush) - the knife reaches further instead (sk_hs_melee_mul)
	return *energy > 0;
}
// b83: one frame of Nano-Gel Armor on actor a (tier index ti: 0 / 1 own, 2 downloaded): HP and AP up to the tier's maxima
static void sk_nanogel_step(int a, int ti, float dt) {
	const SkillDef *D = &SK_DEF[SK_METABOOST]; SkillActor *A = &SKA[a];
	if (!sk_alive(a)) return;
	if (SKW_.heal) SKW_.heal(a, SK_NG_HP[ti] * dt, D->p1[ti]);
	if (SKW_.armor) SKW_.armor(a, SK_NG_AP[ti] * dt, D->p2[ti]);
	A->tick_t -= dt; if (A->tick_t <= 0) { A->tick_t = 0.5f; sk_fx_up(SKF_META_TICK, sk_pos(a)); }
}
static void sk_nanogel_end(int a) { if (!sk_valid(a)) return; if (SKW_.heal) SKW_.heal(a, 0, 1.0f); if (SKW_.armor) SKW_.armor(a, 0, 1.0f); }   // back to the normal maxima
static void sk_tick_active(int a, float dt) {
	SkillActor *A = &SKA[a]; const SkillDef *D = &SK_DEF[A->skill]; int ti = sk_ti(a); V3 p = sk_pos(a);
	if (A->fx_t > 0) A->fx_t -= dt;
	switch (A->skill) {
	case SK_SPECIALRECON: {   // b85: release the drones (Tier 1 one; Tier 2 three, 0.15 s apart), then the cooldown (b117: 1.6 s after the press, was 0.45)
		int want = (int)D->p1[ti], due = A->t >= SK_SEEK_RELEASE ? 1 + (int)((A->t - SK_SEEK_RELEASE) / 0.15f) : 0; if (due > want) due = want;
		while (A->nlocks < due) { int o = sk_seeker_new(a); A->nlocks++; if (o < 0) continue; SkObj *O = &SKO[o];
			V3 d = sk_dir(a), f = sk_norm(v3(d.x, d.y, 0)), r = v3(f.y, -f.x, 0), e = sk_eye(a), n; int kk = A->nlocks - 1;
			// where the hand points, on the floor: the view's ray (at most 12 m), else 3.5 m ahead; never through a wall
			float h = raycast(e, d, 1200, NULL, &n); V3 at = h >= 0 && n.z > 0.6f ? vadd(e, vmul(d, h)) : vadd(e, vmul(f, h >= 0 ? fminf(350, fmaxf(60, h * sqrtf(fmaxf(0.01f, 1 - d.z * d.z)) - 40)) : 350));
			if (want > 1 && kk > 0) { float back = sk_clamp(0.45f * sqrtf((at.x - e.x) * (at.x - e.x) + (at.y - e.y) * (at.y - e.y)), 100, 300);   // (b117, SUMMON.usm 14.0-14.4 s: the first where the hand points, the second beyond it to the right, the third nearer the operative)
				at = kk == 1 ? vadd(at, vadd(vmul(r, 150), vmul(f, 80))) : vadd(at, vadd(vmul(r, 30), vmul(f, -back))); }
			{ V3 e2 = vsub(at, e); float l2 = sk_len(e2), hw = l2 > 1 ? raycast(e, vmul(e2, 1 / l2), l2, NULL, NULL) : -1; if (hw >= 0 && hw < l2 - 45) at = vadd(e, vmul(e2, fmaxf(0, hw - 40) / l2)); }
			{ float g = raycast(vadd(at, v3(0, 0, 70)), v3(0, 0, -1), 600, NULL, NULL); if (g >= 0) at.z = at.z + 70 - g; }
			O->cur = O->pos = at; O->from = vadd(vadd(e, vmul(d, 45)), vadd(vmul(r, 14), v3(0, 0, -22))); O->nrm = v3(0, 0, 0); O->yaw = sk_yaw(a);
			sk_ev_at("intel_skill/mini_tachikoma_spawn", O->cur); sk_fx_up(SKF_RE_PING, O->cur); }
		A->bar = sk_clamp(1 - A->t / D->duration[ti], 0, 1); (void)p;
		if (A->t >= D->duration[ti]) sk_end_active(a, 0);
	} break;
	case SK_OPTICAL: {
		float dur = D->duration[ti];   // footage: Tier 1 = a 5-step bar (one step per second), Tier 2 = a smooth 44 s drain
		A->bar = ti == 0 ? ceilf(dur - A->t - 1e-3f) / dur : 1 - A->t / dur; if (A->bar < 0) A->bar = 0;
		int near_ = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < D->p1[0]) near_ = 1;
		A->camo_close = near_ ? A->camo_close + dt : 0;
		if (a == sk_local) { A->loop_t -= dt; if (A->loop_t <= 0) { A->loop_t = 3.9f; play("cloaking_loop", 120); } }
		if (A->t >= dur) sk_end_active(a, 0);
	} break;
	case SK_HEATSCAN: {
		int np = (int)D->p1[ti]; float gap = SK_HS_GAP[ti], dur = 0.3f + np * gap; A->pulse_t += dt;
		if (A->pulses < np && A->t >= 0.3f + A->pulses * gap) { A->pulses++; A->pulse_t = 0; sk_heat_pulse_do(a, D->p2[ti]); }
		A->bar = ti == 0 ? (float)(np - A->pulses + (A->pulses < np ? 1 : 0)) / np : 1 - A->t / dur; if (A->t > dur) A->bar = 0;   // T1 steps, T2 drains (EP20)
		if (A->t >= dur) sk_end_active(a, 0);
	} break;
	case SK_METABOOST: {   // b83: v2.0 Nano-Gel Armor (EP21): HP and AP regenerate (Tier 2: faster, up to raised maxima), the owner only
		float dur = D->duration[ti];
		A->bar = ti == 0 ? sk_clamp(ceilf(dur - A->t - 1e-3f) / dur, 0, 1) : sk_clamp(1 - A->t / dur, 0, 1);   // T1 3 one-second steps, T2 a 15 s drain (EP21)
		sk_nanogel_step(a, ti, dt);
		A->energy = 1;
		if (A->t >= dur) sk_end_active(a, 0);
	} break;
	case SK_RUSH:
		if (!sk_rush_step(a, &A->energy, D->p1[ti], D->p2[ti], dt)) { if (A->energy < 0) A->energy = 0; sk_end_active(a, 0); return; }
		A->bar = A->energy;
		break;
	case SK_PLASMASHIELD:   // the game's Shield video: the bar is the time left (Tier 1 9 s in 1 s steps, Tier 2 ~22 s, smooth) and keeps
		// running while the shield is lowered; hits wear the shield's own HP (energy, not shown) and break it at 0
		A->bar = ti == 0 ? sk_clamp((D->duration[0] - floorf(A->t)) / D->duration[0], 0, 1) : sk_clamp(1 - A->t / D->duration[ti], 0, 1);
		if (A->energy <= 0) { A->energy = 0; sk_end_active(a, 1); }
		else if (A->t >= D->duration[ti]) sk_end_active(a, 0);
		else if (!A->sh_alarm && (D->duration[ti] - A->t < 3.0f || A->energy < 0.25f)) { A->sh_alarm = 1; if (a == sk_local) ev_play("intel_skill/shield_alarm", NULL); }   // b63: the game's warning beep (3 s left, or a quarter of its strength)
		break;
	case SK_CYBERDIVE:   // b84 NET JUMP: Tier 1 the bar in 1 s steps (EP26 4:43.5 / 4:44.5 / 4:45.5), Tier 2 smooth
		A->bar = ti == 0 ? sk_clamp((D->duration[0] - floorf(A->t)) / D->duration[0], 0, 1) : sk_clamp(1 - A->t / D->duration[ti], 0, 1); (void)p;
		if (A->t >= D->duration[ti]) sk_end_active(a, 0);
		break;
	default: if (A->t >= D->duration[ti]) sk_end_active(a, 0); break;
	}
}
// a downloaded (SkillSync) Tier-1 effect
static void sk_tick_sync(int a, float dt) {
	SkillActor *A = &SKA[a]; const SkillDef *D = &SK_DEF[A->sx_skill]; V3 p = sk_pos(a);
	float age0 = A->sx_age; A->sx_age += dt; A->sx_t -= dt;
	switch (A->sx_skill) {
	case SK_OPTICAL: {
		int near_ = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < D->p1[2]) near_ = 1;
		A->camo_close = near_ ? A->camo_close + dt : 0;
		if (a == sk_local) { A->loop_t -= dt; if (A->loop_t <= 0) { A->loop_t = 3.9f; play("cloaking_loop", 120); } }
	} break;
	case SK_HEATSCAN:
		A->sx_pulse_t += dt;
		if (A->sx_pulses < (int)D->p1[2] && A->sx_age >= 0.3f + A->sx_pulses * SK_HS_GAP[2]) { A->sx_pulses++; A->sx_pulse_t = 0; sk_heat_pulse_do(a, D->p2[2]); }
		break;
	case SK_METABOOST: (void)age0; sk_nanogel_step(a, 2, dt); break;   // b83: the downloaded Tier 2 Nano-Gel Armor
	case SK_RUSH:
		if (!sk_rush_step(a, &A->sx_energy, D->p1[2], D->p2[2], dt)) { A->sx_energy = 0; sk_sx_end(a, 0); return; }
		break;
	case SK_PLASMASHIELD:
		A->sx_energy -= D->p2[2] * dt; if (A->sx_energy <= 0) { A->sx_energy = 0; sk_sx_end(a, 1); return; }
		break;
	}
	if (A->sx_t <= 0) sk_sx_end(a, 0);
}

// ---- bots: when to use which tier (SKILLS.md 3.x "bot use", section 8) and how it changes their moves
static int sk_count_enemies(int a, V3 c, float r) { int n = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), c)) < r) n++; return n; }
SK_API int sk_count_allies(int a, float r) { int n = 0; V3 c = sk_pos(a); for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && !sk_hostile(a, b) && sk_len(vsub(sk_pos(b), c)) < r) n++; return n; }
static int sk_bot_place_at(int a, V3 want) {   // bots place without a preview: on the floor at want
	SkillActor *A = &SKA[a]; V3 n; float dn = raycast(v3(want.x, want.y, want.z + 100), v3(0, 0, -1), SK_DEF[A->skill].placed == 2 ? 2500 : 400, NULL, &n); sk_stat_rays++;
	if (dn < 0 || n.z < 0.7f) return 0;
	sk_place(a, v3(want.x, want.y, want.z + 100 - dn), n); return 1;
}
// bots download a teammate's shared Tier 2 (walk over, "press interact" after a short reaction)
static void sk_bot_sync(int a, float dt) {
	SkillActor *A = &SKA[a]; if (!sk_bots_use || A->sx_t > 0) { A->sync_wait = 0; return; }
	int s = sk_sync_source(a, SK_SYNC_R);
	if (s < 0) { A->sync_wait = 0; return; }
	A->sync_wait += dt; if (A->sync_wait > 0.7f + (a % 3) * 0.6f) sk_sync_do(s, a);
}
static void sk_bot_think(int a, float dt) {
	SkillActor *A = &SKA[a];
	int tg = -1, sees = 0; float lost = 99; V3 ls = v3(0, 0, 0);
	int info = SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &lost, &ls);
	if (info && sees && tg >= 0) { A->seen_t += dt; A->nosee_t = 0; } else { A->seen_t = 0; A->nosee_t += dt; }
	if (!info || !sk_alive(a) || sk_blocked(a)) return;
	sk_bot_sync(a, dt);
	V3 p = sk_pos(a); float td = tg >= 0 && sk_valid(tg) ? sk_len(vsub(sk_pos(tg), p)) : 1e9f; float hp = sk_hp(a);
	if (A->state == SKS_AIM && A->skill == SK_ARMLAUNCHER) { int want = (int)sk_p1(a); float r = SK_AL_IN; if ((A->nlocks >= want && A->t > r + 0.4f) || (A->nlocks > 0 && A->t > r + 1.6f) || A->t > r + 3.0f) sk_launcher_fire(a); return; }
	A->bot_t -= dt; if (A->bot_t > 0 || !sk_bots_use) return;
	A->bot_t = 2;
	int t1 = sk_t1_ready(a), t2 = sk_t2_ready(a); if (!t1 && !t2) return;
	int go = 0, go2 = 0; V3 at = p; float fy = sk_yaw(a); V3 fwd = v3(cosf(fy), sinf(fy), 0);
	switch (A->skill) {
	case SK_ARMLAUNCHER: go = sees && td > 800 && td < 4000 && A->seen_t > 1; go2 = go && sk_count_enemies(a, sk_pos(tg), 1500) >= 2; break;
	case SK_OPTICAL:   // footage: Tier 1 almost every time it is back, going into a fight (5 s at a time); Tier 2 within seconds of the ring filling
		go = (sees && td > 800 && A->seen_t > 0.4f) || (hp < 60 && A->hit_ago < 1.5f) || (!sees && A->nosee_t > 8 && sk_rnd() < 0.25f);
		go2 = !sees || A->hit_ago > 3 || hp < 60; break;   // Tier 2: soon after the ring fills (with allies near, the SkillSync follows)
	case SK_HEATSCAN: { int ne = 0; for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b)) ne++; go = !sees && A->nosee_t > 4 && ne > 1; go2 = go; } break;
	case SK_SPECIALRECON: go = (sees && td < 3000) || (!sees && lost < 4 && tg >= 0); go2 = go; (void)fwd; break;   // b85: seekers at a target in sight or just lost
	case SK_SENTRYGUN: go = sees && td > 1000; go2 = go; at = vadd(p, vmul(fwd, 200)); break;
	case SK_METABOOST: { int low = 0; for (int b = 0; b < SK_MAXACT; b++) if (b != a && sk_alive(b) && !sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < 1000 && sk_hp(b) < 70) low++; go = hp < 50; go2 = hp < 50 || low >= 1; } break;
	case SK_RUSH: go = sees && td > 600 && td < 2000; go2 = go; break;
	case SK_PLASMASHIELD: go = sees && A->hit_ago < 2.5f && hp < 70; go2 = go;
		if (sk_test_shield) { go = go2 = 1; if (a & 1) A->intel = sk_cost2(a); } break;   // (test switch skshield: always, odd bots Tier 2)
	case SK_EMP: {
		int seen_obj = -1; for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, A->team) && sk_len(vsub(SKO[o].pos, p)) < 1500) { seen_obj = o; break; }
		if (seen_obj >= 0 && sk_los(sk_eye(a), sk_obj_center(seen_obj))) { go = 1; at = SKO[seen_obj].pos; }
		else if (sees && tg >= 0 && td < 1500 && sk_count_enemies(a, sk_pos(tg), 800) >= 2) { go = 1; at = sk_pos(tg); }
		if (go) { V3 d = vsub(at, p); float l = sk_hdist(at, p); if (l > 650) at = vadd(p, vmul(sk_norm(v3(d.x, d.y, 0)), 650)); } go2 = go; } break;
	case SK_CYBERWALL: go = sees && td > 1200 && hp < 60; if (go && tg >= 0) at = vadd(p, vmul(sk_norm(v3(sk_pos(tg).x - p.x, sk_pos(tg).y - p.y, 0)), 300)); go2 = go; break;
	case SK_CYBERDIVE: go = sees && td < 2500; go2 = go; break;
	case SK_DMDDRONE: go = (!sees && lost < 3 && tg >= 0) || (sees && td > 1200); at = sees && tg >= 0 ? sk_pos(tg) : ls; go2 = go; break;
	}
	int tier = t2 && go2 && sk_rnd() < 0.85f ? 2 : t1 && go ? 1 : 0;
	if (!tier) return;
	if (sk_use(a, tier) && A->state == SKS_AIM && SK_DEF[A->skill].placed) { if (!sk_bot_place_at(a, at)) { A->state = A->cool > 0 ? SKS_COOLDOWN : SKS_READY; A->bot_t = 3; } }
}
static int sk_rushing(int a) { return sk_has(a, SK_RUSH); }
// bot movement override (called by the bots' movement through the engine): rush charges, hacked bots drift, EMP / drone escape,
// warned bots stop sprinting, shield strafing, shooting at deployables, stopping by a sharer. yaw may be changed. 1 = changed.
static int sk_bot_move(int a, float *mx, float *my, int *sprint, int *jump, float *yaw) {
	if (!sk_ok || !sk_alive(a)) return 0;
	SkillActor *A = &SKA[a]; V3 p = sk_pos(a);
	if (A->hack_t > 0) { *mx = *my = 0; *sprint = 0; *jump = 0; *yaw += sinf(sk_now * 2.3f + a) * 1.8f * 0.016f; return 1; }
	if (A->state == SKS_AIM) { *mx = *my = 0; *sprint = 0; *jump = 0; return 1; }   // (b84: a bot in a Net Jump walks on)
	if (A->stun_t > 0) { *mx *= 0.45f; *my *= 0.45f; *sprint = 0; *jump = 0; return 1; }   // b85: EMP stun
	if (sk_rushing(a)) { int tg = -1, sees = 0; float l; V3 ls; if (SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &l, &ls) && sees) { *mx = 0; *my = 1; *sprint = 1; return 1; } }
	for (int o = 0; o < SK_MAXOBJ; o++) {   // run from a charging enemy EMP / out of an enemy drone's laser
		SkObj *O = &SKO[o]; if (!O->alive || O->team == A->team) continue;
		V3 c = O->pos; float r = O->type == SKO_EMP && A->otgt != SK_TGT_OBJ + o ? O->r + 100 : O->type == SKO_DRONE && O->target == a && A->otgt != SK_TGT_OBJ + o ? O->r + 50 : 0;   // (b85: out of the generator's field unless shooting it)
		if (r <= 0) continue; float d = sk_hdist(p, c); if (d > r) continue;
		*yaw = atan2f(p.y - c.y, p.x - c.x); *mx = 0; *my = 1; *sprint = 1; return 1;
	}
	if (A->otgt >= 0) { V3 v = vsub(sk_target_pos(A->otgt), p); *yaw = atan2f(v.y, v.x); *mx = *my = 0; *sprint = 0; return 1; }
	int ch = 0;
	if (A->sync_wait > 0) { *mx = *my = 0; *sprint = 0; ch = 1; }   // stopping by a sharer to download
	if (A->warn_t > 0 && *sprint) { *sprint = 0; ch = 1; }
	{ int tg = -1, sees = 0; float l; V3 ls;   // a target behind a shield panel facing us: strafe round it
		if (SKW_.bot_info && SKW_.bot_info(a, &tg, &sees, &l, &ls) && sees && tg >= 0 && sk_shield_kind(tg)) {
			V3 q = sk_norm(vsub(p, sk_pos(tg))); float ty = sk_yaw(tg); if (q.x * cosf(ty) + q.y * sinf(ty) > 0.5f) { A->strafe_t -= 0.016f; if (A->strafe_t <= 0) { A->strafe_t = 1.2f; A->strafe = sk_rnd() < 0.5f ? -1 : 1; } *mx = (float)A->strafe; *my = 0.3f; ch = 1; } } }
	return ch;
}
// a roam goal from skills for bot a: a teammate sharing a Tier 2 within link range (go download it), else the nearest enemy
// its team knows about (heat scan, recon mark, wall pass)
static int sk_bot_goal(int a, float *g) {
	if (!sk_ok || !sk_valid(a)) return 0; int tm = SKA[a].team & 1, best = -1; float bd = 1e9f; V3 p = sk_pos(a);
	if (sk_bots_use && SKA[a].sx_t <= 0) { int s = sk_sync_source(a, SK_LINK_R); if (s >= 0) { V3 q = sk_pos(s); g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1; } }
	for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && (SKA[b].heat_t[tm] > 0 || SKA[b].mark_t[tm] > 0)) { float d = sk_len(vsub(sk_pos(b), p)); if (d < bd) { bd = d; best = b; } }
	if (best < 0) return 0; V3 q = sk_pos(best); g[0] = q.x; g[1] = q.y; g[2] = q.z; return 1;
}
// bots shooting at hostile deployables when no operator is in sight (turrets / drones / recon units in 15 m)
static void sk_bot_objects(int a, float dt) {
	SkillActor *A = &SKA[a]; int tg = -1, sees = 0; float l; V3 ls;
	if (!sk_bots_use || !SKW_.bot_info || !SKW_.bot_info(a, &tg, &sees, &l, &ls) || sees || A->hack_t > 0 || A->state == SKS_AIM || sk_is_active(a, SK_CYBERDIVE) || sk_rushing(a)) { A->otgt = -1; return; }
	A->otgt_t -= dt;
	if (A->otgt_t <= 0) { A->otgt_t = 0.5f; A->otgt = -1; V3 e = sk_eye(a); float bd = 1500;
		for (int o = 0; o < SK_MAXOBJ; o++) if (sk_obj_hostile(o, A->team) && SKO[o].type != SKO_WALL) { float d = sk_len(vsub(sk_obj_center(o), e)); if (d < bd) { bd = d; A->otgt = SK_TGT_OBJ + o; } }   // (b85: the EMP Generator, seekers and decoys get shot too)
		if (A->otgt >= 0 && !sk_los(e, sk_target_pos(A->otgt))) A->otgt = -1; }
	if (A->otgt < 0 || !sk_target_ok(A->otgt, A->team)) { A->otgt = -1; return; }
	A->oshot_t -= dt;
	while (A->oshot_t <= 0) { A->oshot_t += 0.12f; if (sk_rnd() < 0.3f) { A->oshot_t += 0.5f; break; }   // bursts
		V3 e = sk_eye(a), tp = sk_target_pos(A->otgt), end = tp; int hit = sk_rnd() < 0.4f * (A->emp_t > 0 ? 0.5f : 1);
		if (hit) sk_obj_damage(A->otgt - SK_TGT_OBJ, 9, a); else end = vadd(tp, v3((sk_rnd() - 0.5f) * 100, (sk_rnd() - 0.5f) * 100, (sk_rnd() - 0.4f) * 80));
		if (SKW_.shot_fx) SKW_.shot_fx(vadd(e, v3(0, 0, -25)), end); snd_wfire_tp("m4a1", &e.x);
		if (A->otgt < 0 || !SKO[A->otgt - SK_TGT_OBJ].alive) { A->otgt = -1; break; } }
}

static void sk_cloak_step(int a, float dt) {   // the cloak's look state: ramps, change time, hit flicker
	SkillActor *A = &SKA[a]; int c = sk_cloaked(a);
	if (c != A->cloak_was) { A->cloak_was = c; A->cloak_t = 0; } else A->cloak_t += dt;
	A->cloak_k = sk_clamp(A->cloak_k + (c ? dt / SK_CLOAK_IN : -dt / (sk_alive(a) ? SK_CLOAK_OUT : SK_CLOAK_DEATH)), 0, 1);
	if (sk_grip_decloak && sk_grip_decloak(a)) A->cloak_k = sk_clamp(A->cloak_k - dt / 0.06f, 0, 1);
	if (A->cloak_hit_t > 0) A->cloak_hit_t -= dt;
}
static void sk_update_actor(int a, float dt) {
	SkillActor *A = &SKA[a]; int alive = sk_alive(a);
	if (A->was_alive && !alive) { sk_cancel(a); A->hack_t = A->emp_t = 0; A->post_t = 0; if (a == sk_local) { SKV.camo = SKV.laugh = 0; fade_others("cloaking_loop", -1, 200); } }   // death ends personal effects
	A->was_alive = alive;
	float hp = sk_hp(a);
	if (alive && A->last_hp >= 0 && hp < A->last_hp - 0.01f) {   // took damage
		A->hit_ago = 0; if (A->bot_t > 0.3f) A->bot_t = 0.3f;   // bots react to being shot (shield, camo)
		if (sk_cloaked(a)) { sk_fx_up(SKF_CAMO_HIT, sk_pos(a)); A->cloak_hit_t = SK_CLOAK_HIT; }
		if (sk_is_active(a, SK_CYBERDIVE)) { sk_ev(a, "intel_skill/cyberdive_dmg_tp"); sk_end_active(a, 0); }   // the body was hit: back at once
	}
	A->last_hp = alive ? hp : -1;
	{ int up = alive && sk_shield_kind(a) != 0;   // the shield's look: raise 0.17 s, up, lower 0.17 s (broken: set where it breaks, 0.33 s)
	  if (up) { float tf = sk_shield_fwd_target(a); A->sh_fwd = !A->sh_up || A->sh_fwd <= 0 ? tf : A->sh_fwd + (tf - A->sh_fwd) * fminf(1, dt * 8); }   // b52
	  if (up && !A->sh_up) { A->sh_ph = 1; A->sh_t = 0; A->sh_big = (signed char)sk_shield_big(a); }
	  else if (!up && A->sh_up && A->sh_ph != 4) { A->sh_ph = 3; A->sh_t = 0; }
	  A->sh_up = (signed char)up; A->sh_t += dt; if (A->sh_hit > 0) A->sh_hit -= dt;
	  if (A->sh_ph == 1 && A->sh_t >= 0.17f) A->sh_ph = 2;
	  if ((A->sh_ph == 3 && A->sh_t >= 0.17f) || (A->sh_ph == 4 && A->sh_t >= 0.34f)) A->sh_ph = 0; }
	if (SKW_.firing && SKW_.firing(a)) A->fire_t = 0; else A->fire_t += dt;
	A->hit_ago += dt;
	if (A->post_t > 0) A->post_t -= dt;
	if (A->hack_t > 0) { A->hack_t -= dt; if (a == sk_local) { SKV.laugh = A->hack_t; if (SKV.glitch < 0.1f) { SKV.glitch = SKV.glitch_len = 0.25f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; } } }
	if (A->stun_t > 0) A->stun_t -= dt;
	if (A->blind_t > 0) A->blind_t -= dt;
	if (A->emp_t > 0) { A->emp_t -= dt; A->shock_t -= dt; if (A->shock_t <= 0 && alive) { A->shock_t = 0.3f; sk_fx_up(SKF_EMP_SHOCK, sk_pos(a)); } }
	for (int k = 0; k < 2; k++) { if (A->heat_t[k] > 0) A->heat_t[k] -= dt; if (A->mark_t[k] > 0) A->mark_t[k] -= dt; }
	if (A->warn_t > 0) A->warn_t -= dt; if (A->laser_t > 0) A->laser_t -= dt;
	if (A->cool > 0) { A->cool -= dt; if (A->cool <= 0) { A->cool = 0;
		if (A->state == SKS_COOLDOWN) { A->state = SKS_READY; A->t = 0;
			if (a == sk_local) { char n[64]; snprintf(n, sizeof(n), "intel_skill/%s_hud_on", SK_HUDON[A->skill]); ev_play(n, NULL); } } } }   // footage: no notice when Tier 1 is back (the Operator's "<skill> ready." is the Tier-2 one)
	else if (A->state == SKS_COOLDOWN) A->state = SKS_READY;
	A->t += dt;
	if (!alive) { A->bar = -1; A->cloak_hit_t = 0; sk_cloak_step(a, dt); return; }
	if (A->state == SKS_AIM) sk_tick_aim(a, dt);
	else if (A->state == SKS_ACTIVE) sk_tick_active(a, dt);
	if (A->sx_t > 0 && A->sx_skill >= 0) sk_tick_sync(a, dt);
	sk_cloak_step(a, dt);
	if (sk_force_cloak && sk_force_cloak(a) && !sk_has(a, SK_OPTICAL)) {   // b63: a mode's cloak (ga.h) gives itself away up close as the skill's does
		int near_ = 0; V3 p = sk_pos(a); for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && sk_hostile(a, b) && sk_len(vsub(sk_pos(b), p)) < SK_DEF[SK_OPTICAL].p1[0]) near_ = 1;
		A->camo_close = near_ ? A->camo_close + dt : 0; }
	// HUD bar: the active skill's energy / time, the deployable's life, else a downloaded effect's time
	if (A->state == SKS_AIM) A->bar = A->skill == SK_ARMLAUNCHER ? 1 - A->t / SK_DEF[SK_ARMLAUNCHER].duration[sk_ti(a)] : -1;
	else if (A->state != SKS_ACTIVE) {
		A->bar = -1; int o = A->obj;
		if (o >= 0 && o < SK_MAXOBJ && SKO[o].alive && SKO[o].owner == a) { const SkObj *O = &SKO[o];
			A->bar = O->type == SKO_DRONE ? O->energy : O->type == SKO_EMP ? (O->phase == 0 ? 1 - (float)(int)(O->t / 0.667f) / 3 : 0) : fminf(1 - O->t / O->life, O->hp / O->hpmax); if (A->bar < 0) A->bar = 0; }
		else if (A->sx_t > 0 && A->sx_len > 0) A->bar = A->sx_skill == SK_RUSH ? A->sx_energy : A->sx_t / A->sx_len;
	}
	if (a != sk_local && !sk_mirror && !(sk_manual >> a & 1)) { sk_bot_think(a, dt); sk_bot_objects(a, dt); }
}
// local player: the enemy-skill voice warning when an enemy skill shows up in view (at 2 Hz, one sight ray per check)
static void sk_local_checks(float dt) {
	if (SKV.warn_cd > 0) SKV.warn_cd -= dt;
	static float t; t -= dt; if (t > 0 || !sk_alive(sk_local)) return; t = 0.5f;
	static int rr; V3 e = sk_eye(sk_local);
	for (int k = 0; k < SK_MAXACT; k++) { int b = (rr + k) % SK_MAXACT; if (b == sk_local || !sk_alive(b) || !sk_hostile(b, sk_local)) continue;
		SkillActor *B = &SKA[b]; if (B->warned) continue;
		if (B->skill == SK_OPTICAL && sk_force_cloak && sk_force_cloak(b)) continue;   // b63 (Ghost Assault): the ghosts are always cloaked: no "camo user detected" each round
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
	for (int k = 0; k < 4; k++) { SkGhost *G = &SKG[k]; if (!G->on) continue; G->t += dt; G->yaw *= 1 - fminf(1, 3 * dt); G->pitch *= 1 - fminf(1, 3 * dt);
		if (G->t > G->len + G->hold) { G->on = 0; if (G->unbuild) sk_fx_up(SKF_UNBUILD, G->pos); } }
	if (sk_nj_play >= 0) { sk_nj_t += dt; if (sk_nj_t >= fxs_len(&sk_nj_sc[sk_nj_play])) sk_nj_play = -1; }   // b121
	if (sk_nj_hold > 0) { sk_nj_hold -= dt; if (!sk_alive(sk_local)) { sk_nj_hold = 0; sk_nj_play = -1; }
		else if (sk_nj_hold <= 0) { sk_nj_hold = 0; SKV.glitch = SKV.glitch_len = 0.4f; SKV.glitch_c[0] = 1; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 1; } }   // the world comes back under the white blocks (EP26 4:46.0)
	float k = dt; SKV.glitch -= k; SKV.hlines -= k; SKV.flash -= k; SKV.vig -= k; SKV.heat -= k; if (SKV.laugh > 0) SKV.laugh -= k; if (SKV.seek_t > 0) SKV.seek_t -= k; if (SKV.blind > 0) SKV.blind -= k;
	if (SKV.t2n > 0) SKV.t2n -= k; if (SKV.sync_note > 0) SKV.sync_note -= k;
	sk_msg_step(dt);
	{ int c = sk_cloaked(sk_local); SKV.camo += c ? dt / SK_CLOAK_IN : -dt / SK_CLOAK_OUT; SKV.camo = sk_clamp(SKV.camo, 0, 1); }   // the white haze
}

// ---- read-only state (vision: how cloak / outlines / heat look; HUD; bots; Think Tanks)
// 1 while a is cloaked: its own therm-optic camo (either tier) or a camo downloaded through SkillSync
static int sk_cloaked(int a) { return sk_ok && !(sk_grip_decloak && sk_grip_decloak(a)) && (sk_has(a, SK_OPTICAL) || (sk_force_cloak && sk_valid(a) && sk_force_cloak(a))); }   // (b70: not in a cyborg's grip)
static int sk_camo_on(int a) { return sk_cloaked(a); }   // (old name, used by thinktank.h and the HUD chips)
// seconds of camo left (0 = not cloaked)
SK_API float sk_cloak_left(int a) {
	if (!sk_ok || !sk_valid(a)) return 0; const SkillActor *A = &SKA[a]; float l = 0;
	if (sk_is_active(a, SK_OPTICAL)) l = SK_DEF[SK_OPTICAL].duration[sk_ti(a)] - A->t;
	if (sk_synced(a, SK_OPTICAL) && A->sx_t > l) l = A->sx_t;
	return l > 0 ? l : 0;
}
// b shows through walls to `team`: bits 1 heat scan, 2 recon / cyber-wall mark (0 = no)
SK_API int sk_revealed(int b, int team) { if (!sk_ok || !sk_valid(b)) return 0; const SkillActor *B = &SKA[b]; return (B->heat_t[team & 1] > 0) | (B->mark_t[team & 1] > 0) << 1; }
static float sk_reveal_left(int b, int team) { if (!sk_ok || !sk_valid(b)) return 0; const SkillActor *B = &SKA[b]; return fmaxf(fmaxf(B->heat_t[team & 1], B->mark_t[team & 1]), 0); }
// the actor whose skill revealed b to team (-1): the heat scanner, or the owner of the recon unit / cyber wall
SK_API int sk_revealed_by(int b, int team) { if (!sk_ok || !sk_valid(b)) return -1; const SkillActor *B = &SKA[b]; return B->heat_t[team & 1] > 0 ? B->heat_by[team & 1] : B->mark_t[team & 1] > 0 ? B->mark_by[team & 1] : -1; }
// a heat scan of a (own or downloaded) just pulsed: 1 at the pulse, fading to 0 over 1 s (-1 = no scan running)
static float sk_heat_pulse(int a) {
	if (!sk_ok || !sk_valid(a)) return -1; const SkillActor *A = &SKA[a];
	if (sk_is_active(a, SK_HEATSCAN)) return sk_clamp(1 - A->pulse_t, 0, 1);
	if (sk_synced(a, SK_HEATSCAN)) return sk_clamp(1 - A->sx_pulse_t, 0, 1);
	return -1;
}
// SkillSync between sharer s and r: 0 none, 1 r can download (white line / white highlight on s's side), 2 r downloaded it
static int sk_sync_link(int s, int r) {
	if (!sk_ok || !sk_valid(s) || !sk_valid(r)) return 0;
	if ((SKA[s].synced >> r & 1) && SKA[s].tier == 2 && SKA[s].state == SKS_ACTIVE) return 2;
	return sk_sync_ok(s, r) && sk_len(vsub(sk_pos(s), sk_pos(r))) < SK_LINK_R ? 1 : 0;
}

// ---- vision accessors: WHAT state an actor's camo / reveal is in (HOW it looks belongs to the vision workstream).
// Any actor a (0 = the local player, 1 + i = bot i). Cheap, no side effects, safe before skills_init (they return "nothing").
// Numbers: SKILLS.md 3.1 + section 8 (the final build's camo) and the SK_CLOAK_* constants above.
enum {
	SKCF_ON       = 1,     // logically cloaked now (own camo of either tier, or one downloaded through SkillSync)
	SKCF_FADE_IN  = 2,     // the cloak is still forming (activation: sk_cloak < 1 and rising)
	SKCF_FADE_OUT = 4,     // the cloak is dissolving (it ended < SK_CLOAK_OUT s ago: sk_cloak > 0 and falling)
	SKCF_HIT      = 8,     // took damage while cloaked < SK_CLOAK_HIT s ago (the electric flicker, camouflage_attack)
	SKCF_FIRED    = 16,    // fired while cloaked < SK_CLOAK_FIRE s ago (the body flashes into view)
	SKCF_NEAR     = 32,    // an enemy is within the near-reveal distance (4 m): enemies that close make her out (sk_can_see)
	SKCF_SYNCED   = 64,    // the camo is a SkillSync download (the Tier-1 effect of a teammate's Tier 2)
	SKCF_TIER2    = 128,   // own Tier-2 camo
	SKCF_EXPOSED  = 256,   // enemies can see her right now despite the camo (fired < 1 s ago, or an enemy near for > 0.8 s)
};
SK_API float sk_time(void) { return sk_now; }   // the skills clock (s since start; the clock of sk_reveal_until)
// cloak amount: 0 fully visible .. 1 fully cloaked. Ramps 0 -> 1 over SK_CLOAK_IN s at activation, 1 -> 0 over SK_CLOAK_OUT s
// at the end (also after an EMP / hack / round reset cancels it), over SK_CLOAK_DEATH s on death. The same for every viewer.
static float sk_cloak(int a) { return sk_ok && sk_valid(a) ? SKA[a].cloak_k : 0; }
static int sk_cloak_flags(int a) {
	if (!sk_ok || !sk_valid(a)) return 0; const SkillActor *A = &SKA[a]; int f = 0, on = sk_cloaked(a);
	if (on) { f |= SKCF_ON; if (A->cloak_k < 1) f |= SKCF_FADE_IN; if (sk_synced(a, SK_OPTICAL) && !sk_is_active(a, SK_OPTICAL)) f |= SKCF_SYNCED;
		if (sk_is_active(a, SK_OPTICAL) && A->tier == 2) f |= SKCF_TIER2; }
	else if (A->cloak_k > 0) f |= SKCF_FADE_OUT;
	if (A->cloak_k > 0) {
		if (A->cloak_hit_t > 0) f |= SKCF_HIT;
		if (A->fire_t < SK_CLOAK_FIRE) f |= SKCF_FIRED;
		if (on && A->camo_close > 0) f |= SKCF_NEAR;
		if (on && (A->fire_t < 1.0f || A->camo_close > 0.8f)) f |= SKCF_EXPOSED; }
	return f;
}
// shimmer 0..1: how strongly the camo's light-bending edge / sparkle should show: 1 when it starts forming or dissolving, fading
// over that ramp; a hit flickers it for SK_CLOAK_HIT s; a low steady 0.15 while it simply holds; 0 when there is no cloak.
static float sk_cloak_shimmer(int a) {
	if (!sk_ok || !sk_valid(a)) return 0; const SkillActor *A = &SKA[a]; if (A->cloak_k <= 0) return 0;
	float s = 0.15f, ramp = A->cloak_was ? SK_SHIM_IN : SK_SHIM_OUT;
	if (A->cloak_t < ramp) s = fmaxf(s, 1 - A->cloak_t / ramp);
	if (A->cloak_hit_t > 0) { float k = A->cloak_hit_t / SK_CLOAK_HIT; s = fmaxf(s, k * (0.6f + 0.4f * (sinf(sk_now * 47) > 0))); }   // electric flicker
	return sk_clamp(s, 0, 1);
}
// momentary break 0..1: how far the cloak is pierced right now (a shot: 1 fading to 0 over SK_CLOAK_FIRE s; a hit: 0.5 fading)
static float sk_cloak_break(int a) {
	if (!sk_ok || !sk_valid(a) || SKA[a].cloak_k <= 0) return 0; const SkillActor *A = &SKA[a]; float b = 0;
	if (A->fire_t < SK_CLOAK_FIRE) b = 1 - A->fire_t / SK_CLOAK_FIRE;
	if (A->cloak_hit_t > 0) b = fmaxf(b, 0.5f * A->cloak_hit_t / SK_CLOAK_HIT);
	return b;
}
// can observer o make a out despite the camo? 1 yes (or a is not cloaked / not an enemy), 0 no. The bots' rule (sk_can_see) minus
// walls and hacks: shots give her away for 1 s within 25 m; beyond 4 m she is invisible; within 4 m after 0.8 s
static int sk_cloak_seen_by(int a, int o) {
	if (!sk_ok || !sk_valid(a) || !sk_valid(o) || !sk_cloaked(a) || !sk_hostile(a, o)) return 1; const SkillActor *A = &SKA[a];
	float d = sk_len(vsub(sk_pos(o), sk_pos(a))); if (A->fire_t < 1.0f && d < 2500) return 1; if (d > SK_DEF[SK_OPTICAL].p1[0]) return 0; return A->camo_close > 0.8f;
}
// reveal through walls: the sk_time() at which b stops showing to `team` (0 = not revealed now); what revealed it: sk_revealed
SK_API float sk_reveal_until(int b, int team) { float l = sk_reveal_left(b, team); return l > 0 ? sk_now + l : 0; }

// ---- hooks the engine calls
// 0 hidden (camo far away, the observer is hacked), 1 normal (still needs a sight line), 2 known through walls (revealed to its team)
static int sk_can_see(int obs, int tgt) {
	if (!sk_ok || !sk_valid(tgt) || !sk_valid(obs)) return 1;
	const SkillActor *O = &SKA[obs], *T = &SKA[tgt];
	if (O->hack_t > 0) return 0;
	if (T->team == O->team) return 1;
	if (O->blind_t > 0) return 0;   // b117: a Seeker Drone's flash
	if (sk_cloaked(tgt)) return sk_cloak_seen_by(tgt, obs);   // shots give her away for a second; near (4 m) after 0.8 s
	if (sk_is_active(tgt, SK_CYBERDIVE)) return 0;   // b84: in the network (Net Jump): unseen
	if (T->heat_t[O->team & 1] > 0 || T->mark_t[O->team & 1] > 0) return 2;
	// b83 (v2.0 Therm-Optic Barrier, "capable of obscuring sight"): an enemy barrier between the eyes and the target hides it
	{ V3 e = SKW_.eye ? SKW_.eye(obs) : vadd(sk_pos(obs), v3(0, 0, 160)), c = sk_chest(tgt), d = vsub(c, e); float l = sk_len(d);
	  if (l > 1) { d = vmul(d, 1 / l); for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_WALL && SKO[o].phase == 1 && SKO[o].team != O->team && sk_ray_wall(o, e, d, l) >= 0) return 0; } }
	return 1;
}
// b79 HYPER SPRINT (v2.0): 0 off, 1 Tier 1 or a SkillSync download, 2 Tier 2 ("increases overall skill effectiveness")
static int sk_hs_tier(int a) { if (!sk_ok || !sk_valid(a) || !sk_rushing(a)) return 0; return sk_is_active(a, SK_RUSH) && SKA[a].tier == 2 ? 2 : 1; }
static float sk_hs_melee_mul(int a) { int t = sk_hs_tier(a); return t == 2 ? 2.4f : t ? 2.0f : 1.0f; }   // the knife's reach
static float sk_jump_mul(int a) { int t = sk_hs_tier(a); return t == 2 ? 1.42f : t ? 1.32f : 1.0f; }      // take-off speed: apex x 1.74 / x 2.0
static float sk_aim_factor(int a) {   // bots: hit chance factor; 0 = no firing (hacked, placing, diving, rushing)
	if (!sk_ok || !sk_valid(a)) return 1; const SkillActor *A = &SKA[a];
	if (A->hack_t > 0 || A->state == SKS_AIM || A->post_t > 0 || sk_is_active(a, SK_CYBERDIVE) || sk_rushing(a)) return 0;
	return A->emp_t > 0 ? 0.5f : 1;
}
static int sk_can_fire(int a) { return !sk_ok || !sk_valid(a) || sk_aim_factor(a) > 0 || sk_hs_tier(a); }   // (b79: the knife works in a hyper sprint)
static float sk_speed_mul(int a, int sprinting) { if (sk_ok && sk_valid(a) && SKA[a].stun_t > 0) return 0.45f; int t = sk_hs_tier(a); return !t ? 1.0f : sprinting ? (t == 2 ? 2.2f : 2.0f) : (t == 2 ? 1.4f : 1.3f); }   // (b85: EMP stun: slow)
static float sk_look_mul(int a) { return sk_ok && sk_valid(a) && SKA[a].stun_t > 0 ? 0.15f : 1.0f; }   // b85: EMP stun - "only able to look one way"
static float sk_fov_add(int a) { return sk_ok && sk_rushing(a) && SKA[a].dashing ? 8 : 0; }
static int sk_hide_viewmodel(int a) { return sk_ok && sk_valid(a) && (((SKA[a].state == SKS_AIM || SKA[a].post_t > 0) && SKA[a].skill == SK_ARMLAUNCHER) || sk_is_active(a, SK_CYBERDIVE)); }
// actor b is an enemy the local player's team knows about through walls (heat scan, recon mark, wall pass): minimap dot
static int sk_known(int b) { if (!sk_ok || !sk_valid(b) || !sk_valid(sk_local) || !sk_hostile(b, sk_local)) return 0; int t = SKA[sk_local].team & 1; return SKA[b].heat_t[t] > 0 || SKA[b].mark_t[t] > 0; }
SK_API int sk_ready(int a) { return sk_t1_ready(a); }
// the arm-launcher viewmodel: 1 = draw it instead of the gun. phase 0 swap-in (the arm comes up and turns into the launcher,
// SK_AL_IN s), 1 aiming, 2 the shot (SK_AL_FIRE s), 3 swap-out (the launcher folds back into the arm, SK_AL_OUT s);
// t = seconds into the phase. (A cancel or a timeout goes straight to phase 3.)
static int sk_launcher_vm(int a, int *phase, float *t) {
	if (!sk_ok || !sk_valid(a) || SKA[a].skill != SK_ARMLAUNCHER) return 0; const SkillActor *A = &SKA[a];
	if (A->state == SKS_AIM) { if (A->t < SK_AL_IN) { *phase = 0; *t = A->t; } else { *phase = 1; *t = A->t - SK_AL_IN; } return 1; }
	if (A->post_t > 0) { if (A->post_t > SK_AL_OUT) { *phase = 2; *t = SK_AL_FIRE + SK_AL_OUT - A->post_t; } else { *phase = 3; *t = SK_AL_OUT - A->post_t; } return 1; }
	return 0;
}
// the launcher viewmodel (vm_armlauncher.gvm: mesh 0 the launcher, 1-2 Batou's hand and forearm; clips in camera space as the
// weapon viewmodels). Drawn by the engine instead of the gun while sk_hide_viewmodel(a) && sk_launcher_vm(a) (after its depth clear).
// lit: the light at the player's spot (as the gun's tint, may be NULL). Projection: the weapons' hip lens (fovy 49.75, offset
// (-2.37, 0.08, 0)), near 2 / far 400. The arm <-> launcher switch shows a 0.12 s chrome flash (the original dissolves them).
enum { SKVC_IN, SKVC_IDLE, SKVC_WALK, SKVC_RUN, SKVC_FIRE, SKVC_OUT, SKVC_JUMP, SKVC_FALL, SKVC_LAND, SKVC_N };
static float sk_vm_jt = 99, sk_vm_lt = 99, sk_vm_at;   // main.c, per frame: seconds since the jump / since a hard landing, seconds in the air (b109)
static void sk_vm_load(const char *vdir) {
	static const char *const N[SKVC_N] = { "swap_in", "idle", "walk", "run", "fire", "swap_out", "jump", "fall", "land" };   // (b109: jump / fall / land were in the model since b78 and never played)
	char p[192]; snprintf(p, sizeof(p), "%svm_armlauncher.gvm", vdir); if (sk_vm_ok || model_load(&sk_vm, p) < 0) return;
	if (sk_vm.nb > 64 || sk_vm.nmesh < 3) { model_free(&sk_vm); return; }
	for (int k = 0; k < SKVC_N; k++) sk_vmc[k] = model_find_clip(&sk_vm, N[k]);
	sk_vm_ok = sk_vmc[SKVC_IN] >= 0 && sk_vmc[SKVC_IDLE] >= 0 && sk_vmc[SKVC_FIRE] >= 0 && sk_vmc[SKVC_OUT] >= 0;
}
// b78 (user: Batou's arm cannon "as much like the footage as possible" - the Batou video 2:25-2:37, 3:25-3:37, 12:13-12:21 and
// the user's screenshot): the PC draws the launcher larger than the guns - fitted to the footage frame by frame, the same
// model under a 34.35 deg lens shifted 0.232 left / 0.138 up (screen halves) lands on it within 6 px (the guns' 49.75 deg drew it
// 2/3 size and low); the guns' material (the highlight from overhead, the Fresnel sheen: md_sld_dir, fogp.w 2) and their sway
// with the look (main.c sk_vm_sw). The sight's lens (vm_armlauncher.gvm 'lens_*'): dark glass under a lit red ring with hot
// spots at the top and bottom and a short bloom; it powers on with a red ring that grows past the visor as the sight locks in
// place, brightens a moment at each lock-on, and goes dark as the missiles leave (the footage: dark from the shot to the fold).
#define SK_VM_FOV 34.35f
static float sk_vm_sw[2];   // main.c, per frame: the guns' sway (camera space x / y, cm)
static int sk_lens_ok = -1, sk_lens_lk; static float sk_lens_lt;
// b109 (user: "add all glow effects and animations possible to batou's arm cannon and make sure that inside the circular elements
// isn't too dark but properly lit and reflective in accordance to ... the game's data files and footage"). The lens light is the
// game's effect\cyberbrain\armlauncher_load_1.nif (Tier 2: + _load_2), read with tools/fx/nifps.py:
//   - a ring (red_ring.dds on an annulus of 1.13 - 1.42 cm round the 1.0 cm glass: the line at 1.26, white-hot bars where the two
//     halves meet) and two flares on it (eye_line.dds, 1.28 cm from the middle), all on one alpha track: 0 at 1/30 s, then 1, 0, 1,
//     0, 1 a thirtieth of a second apart - the light flickers three times and stays (sk_lens_flick);
//   - a flash (blood_dot.dds, a 5 cm plane, additive): alpha 0.5 at 1/30 s fading to 0 at 0.2 s while it grows 3.4 times.
// b78 had fitted these by eye to older footage: the ring at 1.47 radii, a 0.35 s flash, a smooth fade in - and a black disc over the
// glass. Measured on EP16 (3:13, 8:30; 1280 x 720): inside the lit ring the glass reads (76..95, 35..41, 37..42) - dark red, its
// rings still showing - where the port showed (14..25, 17..29, 18..31). The disc is gone: the unlit glass is the model's own
// (its colour map, the PC material's reflection and highlight weights), the lit glass a red veil over it.
static float sk_lens_flick(float t) { if (t < 1.0f / 30) return 0; if (t >= 0.2f) return 1; float x = (t - 1.0f / 30) * 30; int i = (int)x; float f = x - i; return (i & 1) ? 1 - f : f; }
static void sk_lens_quad(const float *c, const float *r, const float *u, float q, const float *col) {
	float a[3], b[3], cc[3], d[3]; for (int k = 0; k < 3; k++) { a[k] = c[k] - r[k] * q + u[k] * q; b[k] = c[k] + r[k] * q + u[k] * q; cc[k] = c[k] + r[k] * q - u[k] * q; d[k] = c[k] - r[k] * q - u[k] * q; }
	sprite_quad(a, b, cc, d, 0, 0, 1, 1, col);
}
static void sk_draw_vm(int a, const float *lit) {
	int ph; float t; if (!sk_vm_ok || !sk_launcher_vm(a, &ph, &t)) return;
	static MBone P[64], Q[64]; const Model *M = &sk_vm; int arm = 0; float flash = 0;   // arm: draw Batou's arm (meshes 1-2) instead of the launcher
	switch (ph) {
	case 0: model_sample(M, sk_vmc[SKVC_IN], t, P); arm = t < SK_AL_SWITCH; flash = 1 - fabsf(t - SK_AL_SWITCH) / 0.18f; break;
	case 1: { V3 v = sk_vel(a); float hs = sqrtf(v.x * v.x + v.y * v.y);   // idle / walk / run by speed (crossfaded)
		model_sample(M, sk_vmc[SKVC_IDLE], t, P);
		int mc = hs > 450 && sk_vmc[SKVC_RUN] >= 0 ? SKVC_RUN : sk_vmc[SKVC_WALK] >= 0 ? SKVC_WALK : -1;
		if (mc >= 0 && hs > 30) { model_sample(M, sk_vmc[mc], t, Q); model_blend(M->nb, P, Q, sk_clamp((hs - 30) / 200, 0, 1), NULL); }
		// b109 (user: "add all glow effects and animations possible to batou's arm cannon"): the launcher's own jump, fall and landing
		// clips (GIS_base_1st: base_Armluncher_1st_jump_up / jump_fall / jump_land), played as the guns play theirs (main.c: the jump at
		// take-off, the fall after 0.45 s in the air, the landing after a fall faster than 3 m/s)
		if (a == sk_local) {
			if (sk_vmc[SKVC_JUMP] >= 0 && sk_vm_jt < model_clip_len(M, sk_vmc[SKVC_JUMP])) { float L = model_clip_len(M, sk_vmc[SKVC_JUMP]); model_sample(M, sk_vmc[SKVC_JUMP], sk_vm_jt, Q); model_blend(M->nb, P, Q, sk_clamp(sk_vm_jt / 0.1f, 0, 1) * sk_clamp((L - sk_vm_jt) / 0.25f, 0, 1), NULL); }
			if (sk_vmc[SKVC_FALL] >= 0 && sk_vm_at > 0.45f) { model_sample(M, sk_vmc[SKVC_FALL], sk_vm_at, Q); model_blend(M->nb, P, Q, sk_clamp((sk_vm_at - 0.45f) / 0.3f, 0, 1), NULL); }
			if (sk_vmc[SKVC_LAND] >= 0 && sk_vm_lt < model_clip_len(M, sk_vmc[SKVC_LAND])) { float L = model_clip_len(M, sk_vmc[SKVC_LAND]); model_sample(M, sk_vmc[SKVC_LAND], sk_vm_lt, Q); model_blend(M->nb, P, Q, sk_clamp(sk_vm_lt / 0.06f, 0, 1) * sk_clamp((L - sk_vm_lt) / 0.2f, 0, 1), NULL); } }
		if (t < 0.15f) { model_sample(M, sk_vmc[SKVC_IN], model_clip_len(M, sk_vmc[SKVC_IN]), Q); model_blend(M->nb, P, Q, 1 - t / 0.15f, NULL); } } break;
	case 2: model_sample(M, sk_vmc[SKVC_FIRE], t, P); break;
	default: model_sample(M, sk_vmc[SKVC_OUT], t, P); arm = t >= SK_AL_BACK; flash = 1 - fabsf(t - SK_AL_BACK) / 0.18f; break;
	}
	float PV[16], th = tanf(SK_VM_FOV * 0.5f * 3.14159265f / 180), as = 960.0f / 544, n = 2, f = 400, VP[16];
	memset(PV, 0, sizeof(PV)); PV[0] = 1 / (th * as); PV[5] = 1 / th; PV[10] = (f + n) / (n - f); PV[11] = -1; PV[14] = 2 * f * n / (n - f);
	PV[12] = PV[0] * -2.37f; PV[13] = PV[5] * 0.08f;   // (x the projection of the hip offset translation)
	PV[8] = 0.2316f; PV[9] = -0.1382f;                 // b78: the lens shift (ndc -0.232, +0.138: x -PV[8], y -PV[9])
	memcpy(VP, PV, sizeof(VP)); for (int r = 0; r < 4; r++) VP[12 + r] += PV[r] * sk_vm_sw[0] + PV[4 + r] * sk_vm_sw[1];   // the guns' sway (PV x a translation)
	float tint[4] = { 1, 1, 1, 1 }; if (lit) for (int c = 0; c < 3; c++) tint[c] = fminf(1.4f, fmaxf(0.25f, lit[c] * 1.1f));
	static const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }, nofog[4] = { 0, 0, 0, 0 }, sld[3] = { 0.15f, 1.0f, -0.25f };
	glDisable(GL_CULL_FACE); md_sld_dir = sld; md_fogp[3] = 2.0f;   // (the guns' highlight and sheen, main.c's first-person draw)
	md_mesh0 = arm ? 1 : 0; md_mesh1 = arm ? 3 : 1; model_draw_cpu(M, NULL, VP, P, ld, tint, nofog, 1);   // (chars b35: CPU skinning unless the GPU path is verified)
	if (flash > 0) {   // the dissolve: the other part as a fading chrome-white additive ghost
		MMesh mm[3]; memcpy(mm, M->mesh, sizeof(mm)); for (int k = 0; k < 3; k++) mm[k].blend = 2; Model T = *M; T.mesh = mm;
		float fc[4] = { 0.85f, 0.95f, 1, 0.85f * flash }; md_mesh0 = arm ? 0 : 1; md_mesh1 = arm ? 1 : 3; model_draw_cpu(&T, NULL, VP, P, ld, fc, nofog, 1); }
	md_mesh0 = 0; md_mesh1 = 1 << 30; md_sld_dir = NULL; md_fogp[3] = 0;
	// the sight's lens (b78)
	if (sk_lens_ok < 0) { const float *lb = model_attach(M, "lens_b"); sk_lens_ok = lb && model_attach(M, "lens_c") && model_attach(M, "lens_n") && M->ntex >= 6 && (int)lb[0] >= 0 && (int)lb[0] < M->nb;
		logp("arm launcher: the sight's lens %s (b78)\n", sk_lens_ok ? "drawn by the engine" : "missing from vm_armlauncher.gvm (an older model)"); }
	if (!arm && sk_lens_ok > 0 && sp_ok) {
		const SkillActor *A = &SKA[a]; float g = 0, rt = -1;
		if (A->nlocks > sk_lens_lk) sk_lens_lt = 0.15f; sk_lens_lk = A->nlocks; if (sk_lens_lt > 0) sk_lens_lt -= 1.0f / 30;   // (a lock-on: a brief brightening)
		const float *lb0 = model_attach(M, "lens_b"); const int nv = lb0[2] > 0.5f;   // (b109's model: lens_b[2] = the glass's radius)
		if (ph == 0) { rt = t - 1.05f; g = nv ? sk_lens_flick(rt) : sk_clamp(rt / 0.12f, 0, 1); }   // lights as the sight locks in place (swap_end ~1.0 s)
		else if (ph == 1) g = 1 + 0.05f * sinf(t * 5.65f) + 0.5f * fmaxf(0, sk_lens_lt) / 0.15f;
		else if (ph == 2) g = t < 0.04f ? 1 : sk_clamp(1 - (t - 0.04f) / 0.12f, 0, 1);   // dark as the missiles leave
		g *= 1.4f;   // (b78-2 on screen: the ring dimmer than the footage's)
		// b109 (user: "once batou's arm cannon reaches tier 2 the other circular elements ... are supposed to light up red too"; the
		// game's files: effect\cyberbrain\armlauncher_load_1.nif lights the launcher's Fire01 node - the sight, Bone28 - with a red
		// ring, two hot spots above and below and a flash that grows 3.4 times in 0.2 s; armlauncher_load_2.nif is the same on Fire02 -
		// the barrel left of and below the sight, Bone24 - with its hot spots to the sides. There is none for Fire03 / Fire04. EP16
		// 8:29-8:31, "TIER 2 SKILL READY" then the launcher: the sight's ring and that barrel's light together; Tier 1, 3:14 and
		// 5:46: that barrel dark): at Tier 2 the second lens (lens2_*, vm_armlauncher.gvm of b109) is lit as the sight's, a quarter turn round
		const int nlens = A->tier == 2 && model_attach(M, "lens2_c") && model_attach(M, "lens2_n") && model_attach(M, "lens2_b") ? 2 : 1;
		for (int li = 0; li < nlens; li++) {
		const float *lc = model_attach(M, li ? "lens2_c" : "lens_c"), *ln = model_attach(M, li ? "lens2_n" : "lens_n"), *lb = model_attach(M, li ? "lens2_b" : "lens_b"); if ((int)lb[0] < 0 || (int)lb[0] >= M->nb) continue; const MBone *B = &P[(int)lb[0]];
		float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3], R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		float c[3], nn[3], u[3], r[3], q = lb[1];
		for (int k = 0; k < 3; k++) { c[k] = R[k * 3] * lc[0] + R[k * 3 + 1] * lc[1] + R[k * 3 + 2] * lc[2] + B->p[k]; nn[k] = R[k * 3] * ln[0] + R[k * 3 + 1] * ln[1] + R[k * 3 + 2] * ln[2]; }
		float nl = sqrtf(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]); if (nl > 1e-4f) for (int k = 0; k < 3; k++) nn[k] /= nl;
		for (int k = 0; k < 3; k++) c[k] += nn[k] * (nv ? 0.5f : 0.15f);   // (in front of the glass. b109: 0.5 cm, where the effect's ring, flares and flash lie in the launcher's own frame - on Fire01 / Fire02, 11.86 cm back from the node, the glass 12.36 - clear of the collar round the glass, which hid a ring at 1.26 radii)
		u[0] = -nn[1] * nn[0]; u[1] = 1 - nn[1] * nn[1]; u[2] = -nn[1] * nn[2]; float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);   // the camera's up in the glass's plane
		{ static int said; if (sp_n && !said) { said = 1; logp("arm launcher: %d sprite vertices still queued at the lens - the lens skipped (b78)\n", sp_n); } }
		if (ul > 1e-3f && sp_n == 0) {
			for (int k = 0; k < 3; k++) u[k] /= ul; r[0] = u[1] * nn[2] - u[2] * nn[1]; r[1] = u[2] * nn[0] - u[0] * nn[2]; r[2] = u[0] * nn[1] - u[1] * nn[0];
			if (li) { float t3[3]; memcpy(t3, u, sizeof(t3)); memcpy(u, r, sizeof(u)); for (int k = 0; k < 3; k++) r[k] = -t3[k]; }   // (the second lens: the hot spots to the sides)
			if (nv) {   // b109: as the effect files draw it (above sk_lens_flick)
				float fa = fminf(1, g / 1.4f); if (fa > 0.003f) { float fc[4] = { 1, 1, 1, fa }; sk_lens_quad(c, r, u, q, fc); sprite_flush(VP, M->tex[5], 0); }   // the lit glass: dark red
				for (int pass = 0; pass < 2; pass++) { float k = fminf(1, g - pass); if (k <= 0.003f) break; float gc[4] = { k, k, k, 1 }; sk_lens_quad(c, r, u, q, gc); sprite_flush(VP, M->tex[3], 1); }
				if (rt >= 0 && rt < 0.2f) { const float k = sk_clamp((rt - 1.0f / 30) / (0.2f - 1.0f / 30), 0, 1), ra = rt < 1.0f / 30 ? rt * 15 : 0.5f * (1 - k), rc[4] = { ra, ra, ra, 1 }; sk_lens_quad(c, r, u, 2.5f * (1 + 2.4f * k), rc); sprite_flush(VP, M->tex[4], 1); }
			} else {   // (a model older than b109: b78's lens)
				static const float dk[4] = { 1, 1, 1, 1 }; sk_lens_quad(c, r, u, q, dk); sprite_flush(VP, M->tex[5], 0);   // the dark glass
				for (int pass = 0; pass < 2; pass++) { float k = fminf(1, g - pass); if (k <= 0.003f) break; float gc[4] = { k, k, k, 1 }; sk_lens_quad(c, r, u, q, gc); sprite_flush(VP, M->tex[3], 1); }   // (twice past 1: the sprites' colour is clamped)
				if (rt >= 0 && rt < 0.35f) { float k = rt / 0.35f, s = 1 + 1.4f * powf(k, 0.7f), ra = 0.9f * powf(1 - k, 1.5f), rc[4] = { ra, ra, ra, 1 }; sk_lens_quad(c, r, u, q * s, rc); sprite_flush(VP, M->tex[4], 1); }
			}
		}
		}
	}
	glEnable(GL_CULL_FACE);
}
// the player's movement input while a skill owns it: the dive moves the ghost camera (noclip, 12 m/s, <= 15 m from the body) and
// the body stands still. Returns 1 when the inputs were taken.
static int sk_move_input(int a, float *mx, float *my, int *sprint, int *jump, float dt) {
	if (1 || !sk_ok || !sk_is_active(a, SK_CYBERDIVE)) return 0;   // (b84: the v2.0 Net Jump moves the body itself; v1's ghost camera kept below)
	SkillActor *A = &SKA[a]; V3 d = sk_dir(a), r = sk_norm(v3(d.y, -d.x, 0)), mv = vadd(vmul(d, *my), vmul(r, *mx));
	A->cam = vadd(A->cam, vmul(mv, 1200 * dt));
	V3 body = sk_eye(a), off = vsub(A->cam, body); float l = sk_len(off); if (l > 1500) A->cam = vadd(body, vmul(off, 1500 / l));
	if (gw > 0) { float x0 = gx0, y0 = gy0, x1 = gx0 + gw * CELL, y1 = gy0 + gh * CELL; A->cam.x = sk_clamp(A->cam.x, x0, x1); A->cam.y = sk_clamp(A->cam.y, y0, y1); }
	*mx = *my = 0; *sprint = 0; *jump = 0; return 1;
}
static int sk_camera(int a, V3 *eye) { (void)a; (void)eye; return 0; }   // (b84: no ghost camera)
// draw alpha of actor a's body as seen from cam: 1 normal, (0..1) camo ghost, 0 hidden
static float sk_draw_alpha(int a, V3 cam) {
	if (sk_ok && sk_is_active(a, SK_CYBERDIVE)) return sk_valid(sk_local) && !sk_hostile(a, sk_local) ? 0.35f : 0;   // b84: Net Jump - gone from the enemies' eyes
	if (!sk_ok || !sk_cloaked(a)) return 1;
	const SkillActor *A = &SKA[a]; if (sk_valid(sk_local) && !sk_hostile(a, sk_local)) return 0.4f;
	if (A->fire_t < 0.3f) return 0.35f;
	float d = sk_len(vsub(sk_pos(a), cam)); float al = 0.12f * sk_clamp(1 - d / 1500, 0, 1); return al < 0.004f ? 0 : al;
}
// HUD: skill icon, Tier-1 cooldown (0..1; -1 ready; 1 while disabled: hacked, EMP'd, a Tier 2 running), active bar (0..1, -1 none)
static void sk_hud(int a, int *icon, float *cool, float *bar) {
	if (!sk_ok || !sk_valid(a)) { if (icon) *icon = 8; if (cool) *cool = -1; if (bar) *bar = -1; return; }
	const SkillActor *A = &SKA[a]; float cd = SK_DEF[A->skill].cool1;
	if (icon) *icon = SK_DEF[A->skill].hud_icon;
	if (cool) { if (sk_blocked(a) || (sk_t2_running(a) && A->cool <= 0)) *cool = 1; else if (A->cool <= 0 && (A->state == SKS_READY || A->state == SKS_AIM)) *cool = -1; else *cool = A->cool > 0 ? sk_clamp(A->cool / cd, 0, 1) : 1; }
	if (bar) *bar = A->bar;
}
// the whole skill gauge for the HUD (the final build's: Tier-2 intel ring, Tier-1 countdown, SkillSync slots)
typedef struct {
	int skill, icon;                  // operator skill, hud.ghud skill.N
	int t1_state; float t1_left;      // Tier 1: 0 ready, 1 cooling (t1_left s; the HUD shows ceil), 2 disabled ("no" icon)
	int intel, cost2, t2_state;       // Tier 2 ring: intel / cost2; t2_state 0 filling, 1 ready ("TIER 2" orange), 2 disabled, 3 running
	int tier;                         // tier of the running (or last) activation
	float bar;                        // the bar under the crosshair (0..1, -1 none)
	int nsync; const char *sync_name[SK_SYNC_MAX];   // sharer: allies who downloaded the running Tier 2 (-> the 3 slots); nsync < 0: not sharing
	int sx_icon; float sx_left, sx_frac;             // receiver: downloaded skill's icon (-1 none), seconds left, 0..1
	float t2_notice;                  // > 0: "TIER 2 SKILL READY" is showing (s left)
} SkHudInfo;
static void sk_hud_info(int a, SkHudInfo *o) {
	memset(o, 0, sizeof(*o)); o->sx_icon = -1; o->nsync = -1; o->bar = -1;
	if (!sk_ok || !sk_valid(a)) return; const SkillActor *A = &SKA[a];
	o->skill = A->skill; o->icon = SK_DEF[A->skill].hud_icon; o->tier = A->tier; o->bar = A->bar;
	int dead = !sk_alive(a), dis = dead || sk_blocked(a) || sk_t2_running(a);
	o->t1_state = dead || sk_blocked(a) ? 2 : A->cool > 0 ? 1 : dis ? 2 : 0; o->t1_left = A->cool;   // footage: the countdown runs on under a Tier 2, then the 'no' icon
	o->intel = A->intel; o->cost2 = sk_cost2(a);
	o->t2_state = A->tier == 2 && A->state == SKS_ACTIVE ? 3 : dis ? 2 : A->intel >= o->cost2 ? 1 : 0;
	if (A->tier == 2 && A->state == SKS_ACTIVE && SK_DEF[A->skill].share) { o->nsync = A->nsync < SK_SYNC_MAX ? A->nsync : SK_SYNC_MAX; for (int k = 0; k < o->nsync; k++) o->sync_name[k] = sk_name(A->sync_who[k]); }
	if (A->sx_t > 0 && A->sx_skill >= 0) { o->sx_icon = SK_DEF[A->sx_skill].hud_icon; o->sx_left = A->sx_t; o->sx_frac = A->sx_len > 0 ? A->sx_t / A->sx_len : 0; }
	o->t2_notice = a == sk_local ? SKV.t2n : 0;
}
// fills the HUD gauge fields of the engine's HudState: ring = intel / Tier-2 cost, TIER 2 when the ring is full, the red arc =
// the Tier-1 cooldown. (Replaces the old intel thresholds 200 / 400 / 800.)
static void sk_hud_state(int a, HudState *H) {
	sk_hud(a, &H->skill_icon, &H->cool, NULL);
	if (!sk_ok || !sk_valid(a)) return;
	H->intel = SKA[a].intel; H->tier_hi = sk_cost2(a); H->tier_lo = 0; H->tier = SKA[a].intel >= H->tier_hi ? 2 : 1;
}
// autoplay (test runs): use the skill when it would make sense (the bot rules from the player's view) and confirm placements;
// Tier 2 whenever the ring is full; download a teammate's shared skill when one is in reach
static void sk_autoplay(int a, int fighting, float dt, int *press, int *fire) {
	if (!sk_ok || !sk_alive(a)) return; SkillActor *A = &SKA[a];
	static float wait = 2; wait -= dt;
	if (sk_test_shield && A->skill == SK_PLASMASHIELD) {   // (test switch skshield) every phase in turn: up 2 s, lowered 1 s, up ...
		static float tg; static int n; tg += dt;
		if (A->state == SKS_ACTIVE) { if (tg > (A->toggled ? 2.0f : 1.0f)) { *press = 1; tg = 0; } return; }
		if (A->state == SKS_READY && wait <= 0) { n++; if (n & 1) A->intel = sk_cost2(a); if (sk_t2_ready(a) && (n & 1)) sk_use(a, 2); else *press = 1; wait = 1; tg = 0; }
		return; }
	if (sk_test_t2 && a == sk_local && A->state == SKS_READY) { A->intel = sk_cost2(a); fighting = 1; }
	if (wait <= 0 && (fighting || sk_rnd() < 0.01f)) {
		if (sk_t2_ready(a)) { if (sk_mirror) { *press = 1; wait = 3; } else if (sk_use(a, 2)) { wait = 3; if (sk_test_t2 && a == sk_local) logp("  skill: Tier 2 used (test switch sktier2)\n"); } }
		else if (sk_t1_ready(a)) { *press = 1; wait = 3; } }
	if (A->state == SKS_AIM && (A->skill == SK_ARMLAUNCHER ? (sk_test_t2 && a == sk_local ? A->t > SK_AL_IN + 5 : A->t > SK_AL_IN && (A->nlocks >= (int)sk_p1(a) || A->t > SK_AL_IN + 2)) : A->t > 0.6f)) *fire = 1;   // (sktier2: the launcher held up 5 s for the pictures)
	(void)0;   // (b84: the Net Jump has nothing to fire)
	{ static float sw; sw += dt; if (sw > 1.0f && sk_sync_source(a, SK_SYNC_R) >= 0) { sw = 0; sk_sync_try(a); } }
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
static void sk_floor_line(const float *VP, V3 a, V3 b, float w, int cell, const float *col) {   // flat strip on the floor a -> b (SkillSync link lines)
	V3 d = v3(b.x - a.x, b.y - a.y, 0); float L = sk_len(d); if (L < 1) return; V3 s = v3(-d.y / L * w * 0.5f, d.x / L * w * 0.5f, 0);
	a.z += 4; b.z += 4; sk_quad(VP, vsub(a, s), vsub(b, s), vadd(b, s), vadd(a, s), cell, col);
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
	const MBone *PP = P ? P : sk_rest_ok[mi] ? sk_rest[mi] : sk_ident;
	model_draw_cpu(&T, &sk_sc[mi], MVP, PP, ld, tn, add ? nofog : fogc, 0);   // rigid pieces: one-entry palettes; skinned (turret, mini-Tachikoma): CPU skinning (chars b35)
}
// a deployable's pose from its model's own clip (deploy / idle / fire / damage), or NULL when the model has none
static const MBone *sk_obj_pose(int mi, const SkObj *O, MBone *P) {
	if (!sk_mok[mi]) return NULL; const Model *M = &sk_mdl[mi]; int c = sk_clip[mi][O->clip];
	if (c < 0 && O->clip != SKA_IDLE) c = sk_clip[mi][SKA_IDLE];
	if (c < 0 || M->nb > 48) return NULL;
	float t = O->clip_t;
	if (O->type == SKO_EMP && sk_rest_ok[mi]) {   // the device unfolds (0.97 s), spins up until the blast (operate_turn_start), spins on (loop)
		static int cs = -2; if (cs == -2) cs = model_find_clip(M, "spin_start");
		float ld = model_clip_len(M, sk_clip[mi][SKA_DEPLOY] >= 0 ? sk_clip[mi][SKA_DEPLOY] : c); t = O->t;
		if (sk_clip[mi][SKA_DEPLOY] >= 0 && t < ld) c = sk_clip[mi][SKA_DEPLOY]; else if (O->phase == 0 && cs >= 0) { c = cs; t -= ld; } else c = sk_clip[mi][SKA_IDLE];
		model_sample(M, c, t, P); return P; }
	if (O->clip == SKA_DEPLOY) { float len = model_clip_len(M, c);
		if (sk_rest_ok[mi]) {   // the game's own unfold: sentry in the 2nd second of its 2 s build, recon in real time (4.7 s: pops up, lands)
			t = O->type == SKO_TURRET ? O->t - 1.0f : O->t; if (t < 0) t = 0;
			if (t > len && O->phase >= 1 && sk_clip[mi][SKA_IDLE] >= 0) { c = sk_clip[mi][SKA_IDLE]; t -= len; } }
		else { float build = O->type == SKO_RECON ? 1.0f : 2.0f; t = len > 0 ? O->t / build * len : 0; } }
	model_sample(M, c, t, P); return P;
}
// rigged turret: the engine's aim on top of the clip: yaw turns the Bip01_body02 subtree, pitch the Bip01_head subtree
static void sk_turret_aim(MBone *P, float yaw_rel, float pitch) {
	if (sk_tb_yaw < 0 || sk_tb_pitch < 0) return;
	float qz[4], qx[4], ax[3] = { 1, 0, 0 }, a2[3], py[3], pp[3]; sk_qaxis(qz, 0, 0, 1, yaw_rel); memcpy(py, P[sk_tb_yaw].p, 12);
	for (int k = 0; k < sk_ntyaw; k++) { MBone *B = &P[(int)sk_tyaw[k]]; float v[3] = { B->p[0] - py[0], B->p[1] - py[1], B->p[2] - py[2] }, r[3], q[4];
		quat_rotate(qz, v, r); for (int j = 0; j < 3; j++) B->p[j] = py[j] + r[j]; sk_qmul(q, qz, B->q); memcpy(B->q, q, 16); }
	quat_rotate(qz, ax, a2); sk_qaxis(qx, a2[0], a2[1], a2[2], -pitch); memcpy(pp, P[sk_tb_pitch].p, 12);   // (the gun faces -y: nose up = -pitch about x)
	for (int k = 0; k < sk_ntpitch; k++) { MBone *B = &P[(int)sk_tpitch[k]]; float v[3] = { B->p[0] - pp[0], B->p[1] - pp[1], B->p[2] - pp[2] }, r[3], q[4];
		quat_rotate(qx, v, r); for (int j = 0; j < 3; j++) B->p[j] = pp[j] + r[j]; sk_qmul(q, qx, B->q); memcpy(B->q, q, 16); }
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
// ---- Azuma's plasma shield, drawn the way the game builds it (azuma_ally / enemy_shield_tier1 / tier2.nif; make_shield.py):
// a solid rim round the panel; the panel's own fill (as_solid, additive 0.25: dark middle, brighter sides); its hex surface lit
// at grazing angles (as_out with the outline shader - the panel is curved round the eye, so in first person it is face-on and
// clear: only the rim and a faint haze at the sides show, as in the game's Shield video); Tier 2's rising scan band ('upup',
// every 2 s); noise lines when it is hit ('damage', R/G/B flicker); the raise / lower flashes (rim and hex surface scale 3x <-> 1x
// in 0.17 s, a round glow) and the break (rim grows away, the pieces fly forward and out, 0.33 s). Ally cyan, enemy red (the
// game's enemy NIFs are red). One texture (sk_shield.gtx), two batched draws for all shields in view.
typedef struct { V3 o, f, l; float s; int t; } SkShX;   // origin, forward, left, scale about the origin, tier (0 / 1)
static V3 sksh_w(const SkShX *X, float x, float y, float z) { return vadd(X->o, vmul(vadd(vadd(vmul(X->f, x), vmul(X->l, y)), v3(0, 0, z)), X->s)); }
static float sksh_x(int t, float y) { return SKSH_CURV[t][0] + SKSH_CURV[t][1] * y * y; }
static int sksh_span(int t, float z, float *y0, float *y1) {   // the panel's width at height z (from its outline loop)
	const float (*O)[2] = SKSH_OUT[t]; int n = SKSH_NOUT[t]; float a0 = 1e9f, a1 = -1e9f;
	for (int i = 0; i < n; i++) { const float *a = O[i], *b = O[(i + 1) % n]; if ((a[1] - z) * (b[1] - z) > 0 || a[1] == b[1]) continue;
		float y = a[0] + (b[0] - a[0]) * (z - a[1]) / (b[1] - a[1]); if (y < a0) a0 = y; if (y > a1) a1 = y; }
	*y0 = a0; *y1 = a1; return a0 < a1;
}
// texture regions of sk_shield.gtx (256 x 512, make_shield.py): A as_out hex cells, B noise lines, C glow, D rim profile,
// E as_solid, F white, H as_dark_ally hex lines (the same grid as A)
#define SKSH_TH 512.0f
enum { SKSH_A, SKSH_B, SKSH_C, SKSH_E, SKSH_F, SKSH_H, SKSH_B2 };
static void sksh_tc(int reg, float u, float v, float *tu, float *tv) {
	u = sk_clamp(u, 0, 1); v = sk_clamp(v, 0, 1);
	switch (reg) {
	case SKSH_A: *tu = (0.5f + u * 127) / 256; *tv = (0.5f + v * 255) / SKSH_TH; break;
	case SKSH_H: *tu = (0.5f + u * 127) / 256; *tv = (256.5f + v * 255) / SKSH_TH; break;
	case SKSH_B: *tu = (128.5f + u * 127) / 256; *tv = (0.5f + v * 127) / SKSH_TH; break;
	case SKSH_B2: *tu = (128.5f + u * 127) / 256; *tv = (256.5f + v * 255) / SKSH_TH; break;
	case SKSH_C: *tu = (128.5f + u * 63) / 256; *tv = (128.5f + v * 63) / SKSH_TH; break;
	case SKSH_F: *tu = (194.0f + u * 12) / 256; *tv = (146.0f + v * 12) / SKSH_TH; break;
	default: *tu = (192.5f + u * 63) / 256; *tv = (160.5f + v * 95) / SKSH_TH; break;
	}
}
#define SKSH_RIM_TU (224.0f / 256)
#define SKSH_RIM_TV0 (128.5f / SKSH_TH)
#define SKSH_RIM_TV1 (143.5f / SKSH_TH)
static int sksh_mode;   // blend of the quads queued in the sprite batch (0 alpha, 1 additive)
static void sksh_room(const float *VP, int n) { if (sp_n + n > SP_MAXQ * 6) sprite_flush_mode(VP, sk_sh_tex, sksh_mode); }
// one layer over the panel: rows of the outline x `nc` columns, texture region reg mapped by the NIF's UVs (+ dv), per-vertex
// alpha = a0 x (kind 1: the outline shader's grazing term; kind 2: a band round z = bz, half height bh). Quads that would add
// nothing are skipped (in first person the grazing layer is skipped whole).
static float sksh_noise(float u, float v) {   // the dark map (turret_laser_noise, 32 x 32, tiled), bilinear, / its mean
	static float inv = 0; if (inv == 0) { unsigned s = 0; for (int i = 0; i < 32 * 32; i++) s += SKSH_NZ[i]; inv = s ? 32.0f * 32.0f / (float)s : 1; }
	u = (u - floorf(u)) * 32 - 0.5f; v = (v - floorf(v)) * 32 - 0.5f; int x0 = (int)floorf(u), y0 = (int)floorf(v); float fx = u - x0, fy = v - y0;
	int x1 = (x0 + 1) & 31, y1 = (y0 + 1) & 31; x0 &= 31; y0 &= 31;
	float a = SKSH_NZ[y0 * 32 + x0] + (SKSH_NZ[y0 * 32 + x1] - SKSH_NZ[y0 * 32 + x0]) * fx, b = SKSH_NZ[y1 * 32 + x0] + (SKSH_NZ[y1 * 32 + x1] - SKSH_NZ[y1 * 32 + x0]) * fx;
	return (a + (b - a) * fy) * inv;
}
static float sksh_now;   // the shield clock (shimmer scroll), set by sk_draw_shields
static void sksh_layer(const float *VP, const SkShX *X, V3 cam, int reg, float dv, float dvs, const float *col, float a0, int kind, float bz, float bh) {
	int t = X->t, nr = kind == 2 ? 12 : t ? 14 : 10, nc = reg == SKSH_E || kind == 1 ? 10 : 6; float zt = SKSH_DIM[t][1] * 0.999f, z0 = -zt, z1 = zt;
	if (kind == 2) {   // Tier 2's band: texture v (panel v + bz) 0.50 .. 0.72 is where upup.dds is lit
		float za = (SKSH_UVMAP[t][2] + bz - 0.50f) / -SKSH_UVMAP[t][3], zb = (SKSH_UVMAP[t][2] + bz - 0.72f) / -SKSH_UVMAP[t][3];
		z0 = fmaxf(-zt, fminf(za, zb)); z1 = fminf(zt, fmaxf(za, zb)); if (z1 <= z0) return; }
	// b105 (user: "two small gaps at the top left and right of azuma's shield in his shield animation on the operatives screen"):
	// the rows were evenly spaced (11.8 cm apart on the Tier-2 panel) and joined by straight edges - across the panel's rounded
	// upper and lower corners that chord ran up to 3.4 cm inside the outline, a wedge of nothing between the fill and the rim at
	// each corner. A row is added at the height of every outline point of those corners (beyond 0.6 of the half height), so
	// the fill follows the outline there to the rim.
	enum { SKSH_ROWS = 30 }; static float pv[SKSH_ROWS][11][10]; int ok[SKSH_ROWS]; float zr[SKSH_ROWS];   // per row / column: world xyz, tu, tv, rgba, u (the NIF's)
	for (int r = 0; r < nr; r++) zr[r] = z0 + (z1 - z0) * r / (nr - 1);
	for (int i = 0; i < SKSH_NOUT[t] && nr < SKSH_ROWS; i++) { float z = SKSH_OUT[t][i][1]; int k = 0;
		if (fabsf(z) < 0.6f * zt || z <= z0 || z >= z1) continue;
		while (k < nr && zr[k] < z) k++;
		if ((k < nr && zr[k] - z < 0.35f) || (k > 0 && z - zr[k - 1] < 0.35f)) continue;
		for (int j = nr; j > k; j--) zr[j] = zr[j - 1];
		zr[k] = z; nr++; }
	for (int r = 0; r < nr; r++) {
		float z = zr[r], ya, yb; ok[r] = sksh_span(t, z, &ya, &yb);
		for (int c = 0; c <= nc && ok[r]; c++) {
			float y = ya + (yb - ya) * c / nc, x = sksh_x(t, y); V3 P = sksh_w(X, x, y, z); float *o = pv[r][c];
			o[0] = P.x; o[1] = P.y; o[2] = P.z;
			float u = SKSH_UVMAP[t][0] + SKSH_UVMAP[t][1] * y, v = SKSH_UVMAP[t][2] + SKSH_UVMAP[t][3] * z;
			sksh_tc(reg, u, dv + v * dvs, &o[3], &o[4]); o[9] = u;
			float a = a0;
			if (kind == 1) { float g = 2 * SKSH_CURV[t][1] * y, il = 1 / sqrtf(1 + g * g); V3 n = vadd(vmul(X->f, il), vmul(X->l, -g * il));
				V3 e = vsub(cam, P); e.z = 0; e = sk_norm(e);   // grazing across the panel only: in first person its top and bottom stay clear (footage)
				float d = 1 - fabsf(vdot(n, e)); a *= sk_clamp(2 * powf(d > 0 ? d : 0, 0.75f), 0, 1);
				// the dark map: the NIF multiplies this surface by turret_laser_noise (turned 90 degrees) scrolling u +1 / v -1 every
				// 8 s - a slow drifting shimmer; sampled per vertex (the noise is a soft cloud), kept at its mean brightness
				if (SKSH_HAVE_X) a *= sk_clamp(sksh_noise(v - sksh_now * 0.125f, u + sksh_now * 0.125f), 0.3f, 1.8f); }
			else if (kind == 2) { float vt = (v + bz - 0.50f) / 0.22f * 32 - 0.5f; int i0 = (int)floorf(vt); float fr = vt - i0;   // upup.dds (linear)
				float p0 = i0 >= 0 && i0 < 32 ? SKSH_UP[i0] : 0, p1 = i0 + 1 >= 0 && i0 + 1 < 32 ? SKSH_UP[i0 + 1] : 0; a *= (p0 + (p1 - p0) * fr) / 255; }
			o[5] = col[0]; o[6] = col[1]; o[7] = col[2]; o[8] = a;
		}
	}
	float cm = fmaxf(col[0], fmaxf(col[1], col[2]));
	for (int r = 0; r + 1 < nr; r++) if (ok[r] && ok[r + 1]) for (int c = 0; c < nc; c++) {
		const float *q[4] = { pv[r][c], pv[r][c + 1], pv[r + 1][c + 1], pv[r + 1][c] };
		float am = fmaxf(fmaxf(q[0][8], q[1][8]), fmaxf(q[2][8], q[3][8]));
		if (am * cm < 3.0f / 255) continue;   // adds under 3 / 255 anywhere in the quad: not drawn (b50: most of a first-person panel)
		if (reg == SKSH_E && SKSH_HAVE_X) {   // the fill's dark middle adds nothing: skip quads whose brightest texel x alpha < 4 / 255
			float ua = fminf(fminf(q[0][9], q[1][9]), fminf(q[2][9], q[3][9])), ub = fmaxf(fmaxf(q[0][9], q[1][9]), fmaxf(q[2][9], q[3][9]));
			int b0 = (int)(sk_clamp(ua, 0, 0.999f) * 16), b1 = (int)(sk_clamp(ub, 0, 0.999f) * 16), mx = 0; for (int b = b0; b <= b1; b++) if (SKSH_PROF[b] > mx) mx = SKSH_PROF[b];
			if (am * cm * mx < 6.0f) continue; }   // (a smooth ramp: the step where it stops is under 6 / 255)
		sksh_room(VP, 6); static const int T6[6] = { 0, 1, 2, 0, 2, 3 };
		for (int k = 0; k < 6; k++) { const float *v = q[T6[k]]; sp_vert(v, v[3], v[4], v + 5); }
	}
}
// b105 (user: "two small gaps at the top left and right of azuma's shield in his shield animation on the operatives screen"):
// the ribbon was one rectangle per edge of the outline, each a little longer than its edge - where the outline turns sharply
// (the panel's two upper corners) the rectangles overlapped on the inside and left a notch open on the outside. The corners
// are mitred now: both edges meeting at a point share the same two corner vertices (the bisector, as long as the corner
// needs, at most twice the ribbon's half width), so the rim is one closed band - no notch, and no brighter patch where two
// rectangles used to lie over each other.
static void sksh_rim(const float *VP, const SkShX *X, V3 cam, const float *col) {   // the solid rim: a ribbon facing the eye
	int t = X->t, n = SKSH_NOUT[t]; const float (*O)[2] = SKSH_OUT[t];
	float tu = SKSH_RIM_TU, tv0 = SKSH_RIM_TV0, tv1 = SKSH_RIM_TV1;
	enum { RIM_MAX = 192 }; static V3 PI[RIM_MAX], PO[RIM_MAX]; static unsigned char okv[RIM_MAX];
	if (n < 3 || n > RIM_MAX) return;
	for (int i = 0; i < n; i++) {
		const float *c = O[i]; V3 C = sksh_w(X, sksh_x(t, c[0]), c[0], c[1]), din = v3(0, 0, 0), dout = v3(0, 0, 0); int gi = 0, go = 0;
		for (int k = 1; k < n && !gi; k++) { const float *a = O[(i - k + n) % n]; V3 d = vsub(C, sksh_w(X, sksh_x(t, a[0]), a[0], a[1])); float L = sk_len(d); if (L >= 0.01f) { din = vmul(d, 1 / L); gi = 1; } }
		for (int k = 1; k < n && !go; k++) { const float *b = O[(i + k) % n]; V3 d = vsub(sksh_w(X, sksh_x(t, b[0]), b[0], b[1]), C); float L = sk_len(d); if (L >= 0.01f) { dout = vmul(d, 1 / L); go = 1; } }
		V3 e = vsub(cam, C); float dist = sk_len(e), w = fmaxf(1.5f * X->s, dist * 0.004f) * 0.5f;   // as thick as the game's first-person rim (7-8 px); at least ~2 px far away
		V3 si = vcross(din, e), so = vcross(dout, e); float li = sk_len(si), lo = sk_len(so); okv[i] = 0;
		if (!gi || !go || li < 1e-4f || lo < 1e-4f) continue;
		si = vmul(si, 1 / li); so = vmul(so, 1 / lo); V3 m = vadd(si, so); float ml = sk_len(m); if (ml < 0.05f) { m = so; ml = 1; }
		m = vmul(m, 1 / ml); float cs = vdot(m, so); if (cs < 0.5f) cs = 0.5f;   // (the mitre's reach: w / cos(half the turn), at most 2 w)
		m = vmul(m, w / cs); PI[i] = vsub(C, m); PO[i] = vadd(C, m); okv[i] = 1;
	}
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n; if (!okv[i] || !okv[j]) continue;
		if (sk_len(vsub(PI[j], PI[i])) < 0.005f && sk_len(vsub(PO[j], PO[i])) < 0.005f) continue;
		sksh_room(VP, 6);
		sp_vert(&PI[i].x, tu, tv0, col); sp_vert(&PI[j].x, tu, tv0, col); sp_vert(&PO[j].x, tu, tv1, col);
		sp_vert(&PI[i].x, tu, tv0, col); sp_vert(&PO[j].x, tu, tv1, col); sp_vert(&PO[i].x, tu, tv1, col);
	}
}
static void sksh_frags(const float *VP, const SkShX *X, float tt, const float *col, float a, int noise, float now) {   // the break
	int t = X->t; float k = sk_clamp(tt / 0.17f, 0, 1);
	for (int i = 0; i < SKSH_NFR[t]; i++) {
		const SkShFrag *F = &SKSH_FR[t][i]; float ox = F->org[0] + F->d[0] * k, oy = F->org[1] + F->d[1] * k, oz = F->org[2] + F->d[2] * k;
		float rx = F->rot[0] * k, ry = F->rot[1] * k, rz = F->rot[2] * k, cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
		for (int j = 0; j + 2 < F->ni; j += 3) {
			sksh_room(VP, 3);
			for (int m = 0; m < 3; m++) {
				const float *q = SKSH_FV[t][SKSH_FI[t][F->i0 + j + m]]; float x = q[0], y = q[1], z = q[2], y2, z2, x2;
				y2 = y * cx - z * sx; z2 = y * sx + z * cx; y = y2; z = z2;   // spin about x, y, z (the NIF's euler keys, linear)
				x2 = x * cy + z * sy; z2 = -x * sy + z * cy; x = x2; z = z2;
				x2 = x * cz - y * sz; y2 = x * sz + y * cz; x = x2; y = y2;
				V3 P = sksh_w(X, ox + x, oy + y, oz + z); float tu, tv;
				float u = SKSH_UVMAP[t][0] + SKSH_UVMAP[t][1] * (F->org[1] + q[1]), v = SKSH_UVMAP[t][2] + SKSH_UVMAP[t][3] * (F->org[2] + q[2]);
				sksh_tc(noise ? SKSH_B : SKSH_E, u, noise ? fmodf(v + now * 2, 1) : v, &tu, &tv);
				float c[4] = { col[0], col[1], col[2], a }; sp_vert(&P.x, tu, tv, c);
			}
		}
	}
}
// the map's fog at distance d (the world / model shaders' formula, md_fogp = start, density, amount): 0 = none
static float sksh_fog(float d) { return md_fogp[2] * (1 - expf(-fmaxf(d - md_fogp[0], 0) * md_fogp[1])); }
// which shields: SKSH_WORLD = everyone's but the player's own first-person panel (sk_draw_world, before the particles);
// SKSH_OWN = only that one (sk_draw_world_late, after the particles: it is in front of everything but the gun)
enum { SKSH_WORLD, SKSH_OWN };
static int sksh_own_fp(int a, V3 cam) { return a == sk_local && sk_len(vsub(cam, sk_eye(a))) < 40; }
static void sk_draw_shields(const float *VP, V3 cam, const float *R, const float *U, const float *fogc, int who) {
	if (!sk_sh_tex) return;
	int lt = sk_valid(sk_local) ? SKA[sk_local].team : 0; float now = sk_now; sksh_now = now;
	static const float ACY[3] = { 0.40f, 1.00f, 0.92f }, ARIM[3] = { 0.44f, 0.84f, 0.74f }, ERED[3] = { 1.0f, 0.28f, 0.22f }, ERIM[3] = { 0.92f, 0.30f, 0.25f };
	static const float nofog[4] = { 0, 0, 0, 0 }; if (!fogc) fogc = nofog;
	// what to draw, far to near (the rims are alpha blended: a near rim must cover a far one); id < SK_MAXACT a shield, else a wall
	int id[SK_MAXACT + SK_MAXOBJ]; float dd[SK_MAXACT + SK_MAXOBJ]; int n = 0;
	for (int a = 0; a < SK_MAXACT; a++) {
		if (!sk_valid(a) || !SKA[a].sh_ph || sksh_own_fp(a, cam) != (who == SKSH_OWN)) continue;
		float d = sk_len(vsub(sk_shield_origin(a), cam)); if (d > 6000) continue; id[n] = a; dd[n] = d; n++; }
	if (who == SKSH_WORLD) for (int o = 0; o < SK_MAXOBJ; o++) if (SKO[o].alive && SKO[o].type == SKO_WALL) {
		float d = sk_len(vsub(vadd(SKO[o].pos, v3(0, 0, SKO[o].h * 0.5f)), cam)); if (d > 8000) continue; id[n] = SK_MAXACT + o; dd[n] = d; n++; }
	if (!n) return;
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && dd[j] > dd[j - 1]; j--) { int ti = id[j]; id[j] = id[j - 1]; id[j - 1] = ti; float tf = dd[j]; dd[j] = dd[j - 1]; dd[j - 1] = tf; }
	if (sp_n) sprite_flush(VP, sp_blob, 0);
	if (who == SKSH_OWN) glDisable(GL_DEPTH_TEST); else glEnable(GL_DEPTH_TEST);
	for (int pass = 0; pass < 2; pass++) {   // 0: the additive layers (order-free), 1: the rims / wall edges (alpha blended, far to near)
		sksh_mode = pass ? 0 : 1;
		for (int k = 0; k < n; k++) {
			// fog: an additive layer fades by (1 - fog), a rim turns to the fog colour - as the world and the models do
			float fk = sksh_fog(dd[k]) * fogc[3], fa = 1 - fk;
			if (id[k] >= SK_MAXACT) {   // Maven's cyber walls, as the game's CYBERWALL video shows them: a nearly clear pane (faint fill)
				// with a thin bright edge, cyan for allies, red for enemies (an enemy pane a little stronger); it grows up while it builds
				// and flickers before it goes
				const SkObj *O = &SKO[id[k] - SK_MAXACT];
				int ally = O->team == lt; float kb = O->phase == 0 ? sk_clamp(O->t / 2, 0.05f, 1) : 1, left = O->life - O->t;
				float al = (left < 2 ? 0.6f + 0.4f * (sinf(now * 50) > 0) : 1) * (O->hit_t > 0 ? 1.4f : 1);
				V3 r = v3(-O->nrm.y, O->nrm.x, 0); float hw = O->w * 0.5f, h = O->h * kb;
				V3 c0 = vsub(O->pos, vmul(r, hw)), c1 = vadd(O->pos, vmul(r, hw)), c2 = vadd(c1, v3(0, 0, h)), c3 = vadd(c0, v3(0, 0, h));
				const float *cc = ally ? ACY : ERED, *ec = ally ? ARIM : ERIM;
				if (pass == 0) { float f[4] = { cc[0], cc[1], cc[2], (ally ? 0.07f : 0.11f) * al * fa }, u0, v0, u1, v1; sksh_tc(SKSH_F, 0, 0, &u0, &v0); sksh_tc(SKSH_F, 1, 1, &u1, &v1);
					sksh_room(VP, 6); sprite_quad(&c0.x, &c1.x, &c2.x, &c3.x, u0, v0, u1, v1, f); }
				else { float e[4] = { ec[0] + (fogc[0] - ec[0]) * fk, ec[1] + (fogc[1] - ec[1]) * fk, ec[2] + (fogc[2] - ec[2]) * fk, 0.85f * fminf(al, 1) };
					V3 q[5] = { c0, c1, c2, c3, c0 }; float tu = SKSH_RIM_TU, tv0 = SKSH_RIM_TV0, tv1 = SKSH_RIM_TV1;
					for (int i = 0; i < 4; i++) { V3 A = q[i], B = q[i + 1], d = vsub(B, A); float L = sk_len(d); if (L < 0.01f) continue; d = vmul(d, 1 / L);
						V3 m = vmul(vadd(A, B), 0.5f), ev = vsub(cam, m); float w = fmaxf(1.2f, sk_len(ev) * 0.003f) * 0.5f; V3 sd = vcross(d, ev); float sl = sk_len(sd); if (sl < 1e-4f) continue;
						sd = vmul(sd, w / sl); A = vsub(A, vmul(d, w)); B = vadd(B, vmul(d, w)); V3 p0 = vsub(A, sd), p1 = vsub(B, sd), p2 = vadd(B, sd), p3 = vadd(A, sd);
						sksh_room(VP, 6); sp_vert(&p0.x, tu, tv0, e); sp_vert(&p1.x, tu, tv0, e); sp_vert(&p2.x, tu, tv1, e); sp_vert(&p0.x, tu, tv0, e); sp_vert(&p2.x, tu, tv1, e); sp_vert(&p3.x, tu, tv1, e); } }
				continue;
			}
			int a = id[k]; const SkillActor *A = &SKA[a];
			SkShX X; X.o = sk_shield_origin(a); float y = sk_yaw(a); X.f = v3(cosf(y), sinf(y), 0); X.l = v3(-sinf(y), cosf(y), 0); X.s = 1; X.t = A->sh_big ? 1 : 0;
			int ally = A->team == lt; const float *add = ally ? ACY : ERED, *rimc = ally ? ARIM : ERIM;
			float tt = A->sh_t, ra = 1, rs = 1;   // rim alpha / scale
			if (A->sh_ph == 1) { ra = sk_clamp(tt / 0.17f, 0, 1); rs = 3 - 2 * ra; }
			else if (A->sh_ph >= 3) { float kk = sk_clamp(tt / 0.17f, 0, 1); ra = 1 - kk; rs = 1 + 2 * kk; }
			if (pass == 1) { if (ra > 0.01f) { SkShX Y = X; Y.s = rs; float c[4] = { rimc[0] + (fogc[0] - rimc[0]) * fk, rimc[1] + (fogc[1] - rimc[1]) * fk, rimc[2] + (fogc[2] - rimc[2]) * fk, ra }; sksh_rim(VP, &Y, cam, c); } continue; }
			if (fa < 0.02f) continue;
			if (A->sh_ph == 1 || A->sh_ph == 2) {   // up: the fill, the hex surface at grazing angles, Tier 2's scan band, hit noise
				float c[4] = { add[0], add[1], add[2], 1 };
				sksh_layer(VP, &X, cam, SKSH_E, 0, 1, c, 0.25f * ra * fa, 0, 0, 0);   // T08 as_solid, additive 0.25 (NIF)
				// the hex surface (CollMesh, the outline shader: only where the panel is seen edge-on). Not in first person: the panel is
				// curved round the eye, so it is face-on there and the game's Shield video shows no hex pattern - only the rim and the
				// haze at the sides (the fill above; measured against the video: cyan excess +12..+20 inside the rim, ours +14..+18)
				if (who != SKSH_OWN) sksh_layer(VP, &X, cam, SKSH_A, 0, 1, c, 0.5f * ra * fa, 1, 0, 0);
				if (X.t && A->sh_ph == 2 && SKSH_HAVE_X) {   // T03: upup.dds scrolled v -0.4 -> 0.6 in 1 s, then 1 s off (NIF), masking the hex LINES
					// (the game's Shield video, frames 342-350: a band of glowing hex edges rising up the panel, brightest at its top)
					float ph = fmodf(tt, 2.0f); if (ph < 1) sksh_layer(VP, &X, cam, SKSH_H, 0, 1, c, 1.0f * fa, 2, -0.4f + ph, 0); }
				if (A->sh_hit > 0) {   // 'damage': fine horizontal lines jumping over the panel (its texture scrolls 2 / s, emissive flickering
					// at 15 Hz); colour and strength measured on the game's Shield video (frame 186 - 138: +18 / +32 / +20 over the panel, rows +-20)
					float fl = fmodf(now, 0.067f) < 0.0335f ? 1.15f : 0.85f, hk = sk_clamp(A->sh_hit / 0.3f, 0, 1) * fa;
					float hc[4] = { 0.6f * fl, 1.0f * fl, 0.65f * fl, 1 };
					sksh_layer(VP, &X, cam, SKSH_B2, fmodf(now * 37.0f, 0.05f), 0.95f, hc, 0.35f * hk, 0, 0, 0); }   // (all its rows over the panel: fine lines; one layer)
				if (A->sh_ph == 1) { float c2[4] = { add[0], add[1], add[2], 1 }; sksh_layer(VP, &X, cam, SKSH_A, 0, 1, c2, (tt < 0.13f ? 0 : 0.5f * (tt - 0.13f) / 0.04f) * fa, 0, 0, 0); }
			} else if (A->sh_ph == 3) {   // lowered / timed out: the hex surface grows 3x and fades (the cyan hex wash in first person)
				SkShX Y = X; Y.s = rs; float c[4] = { add[0], add[1], add[2], 1 }; sksh_layer(VP, &Y, cam, SKSH_A, 0, 1, c, 0.5f * ra * fa, 0, 0, 0);
			} else if (A->sh_ph == 4) {   // broken: the pieces - translucent light cyan plates (Shield.usm frame 194), the noise lines over them
				float fb = (tt < 0.17f ? 1 : sk_clamp(1 - (tt - 0.17f) / 0.16f, 0, 1)) * fa, c[4] = { add[0], add[1], add[2], 1 }, w[4] = { 0.55f + 0.45f * add[0], 1, 0.55f + 0.45f * add[2], 1 };
				sksh_frags(VP, &X, tt, c, 0.45f * fb, 0, now); sksh_frags(VP, &X, tt, w, 0.22f * fb, 1, now);
			}
			// the round glow of the raise (0.10 - 0.17 s) and of the lowering / break (0 - 0.10 s)
			float ga = A->sh_ph == 1 ? (tt < 0.10f ? 0 : tt < 0.13f ? (tt - 0.10f) / 0.03f : sk_clamp((0.17f - tt) / 0.04f, 0, 1)) : A->sh_ph >= 3 ? sk_clamp(1 - tt / 0.10f, 0, 1) : 0;
			ga *= 0.8f * fa;   // (NIF 0.5; the game's raise frame is a near-white wash: its glow blooms)
			if (ga > 0.005f) { float gw = 100, gh = X.t ? 200 : 100, u0, v0, u1, v1; sksh_tc(SKSH_C, 0, 0, &u0, &v0); sksh_tc(SKSH_C, 1, 1, &u1, &v1);
				V3 r = v3(R[0] * gw, R[1] * gw, R[2] * gw), u = v3(U[0] * gh, U[1] * gh, U[2] * gh), o = X.o;
				V3 p0 = vsub(vsub(o, r), u), p1 = vsub(vadd(o, r), u), p2 = vadd(vadd(o, r), u), p3 = vadd(vsub(o, r), u); float c[4] = { add[0], add[1], add[2], ga };
				sksh_room(VP, 6); sprite_quad(&p0.x, &p1.x, &p2.x, &p3.x, u0, v1, u1, v0, c); }
		}
		sprite_flush_mode(VP, sk_sh_tex, sksh_mode);
	}
	glEnable(GL_DEPTH_TEST);
}
// b63: the operative screen's shield (footage "EP. 24 Azuma" 20.4 - 21.4 s; the game's model\Object\shield\OUTGAME_SHIELD.nif is
// its Tier-2 panel set up at body size in front of the operator): as Azuma's choice pose (chr_og_m.gab) touches the wrist the
// panel forms round him - a bright cyan haze, the hex lines across it and the solid rim - holds for about 0.7 s, then fades.
// t = seconds since the pose started; feet: the figure's feet, fwd: its facing (towards the camera); R / U: the camera's
// right / up (the forming glow). Drawn by main.c chr_preview after the figure, in its viewport; costs two sprite batches.
static float sk_menu_shield_k(float t) { return t < 0.40f ? 0 : t < 0.55f ? (t - 0.40f) / 0.15f : t < 1.20f ? 1 : t < 1.45f ? 1 - (t - 1.20f) / 0.25f : 0; }
static void sk_draw_menu_shield(const float *VP, V3 cam, V3 feet, V3 fwd, const float *R, const float *U, float t, float dist) {
	float k = sk_menu_shield_k(t); if (k <= 0.003f || !sk_sh_tex || !SKSH_NOUT[1] || SKSH_DIM[1][1] <= 0) return;
	static const float ACY[3] = { 0.36f, 1.00f, 0.95f }, ARIM[3] = { 0.55f, 0.95f, 0.92f };
	// b103 (user: "azuma's shield appears during his animation in the operatives screen and his forearm is just clipping through
	// it"): dist = how far in front of the feet the panel stands - main.c measures the pose (the left arm is held 34-50 cm out
	// while the panel shows; b63-b102 stood it at 22 cm, through the forearm). Nearer the camera it would look larger: the size
	// is taken back by the same ratio, so it covers him as it did (from just above the head to the ankles, as the footage).
	if (dist < 22) dist = 22;
	float cd = sk_len(vsub(cam, feet)), near_k = cd > dist + 60 ? (cd - dist) / (cd - 22) : 1;
	SkShX X; X.t = 1; X.f = fwd; X.l = v3(-fwd.y, fwd.x, 0); X.s = 162.0f / (2 * SKSH_DIM[1][1]) * near_k * (1 + 0.08f * (1 - sk_clamp((t - 0.40f) / 0.15f, 0, 1)));   // forms from a little larger
	X.o = vadd(feet, v3(fwd.x * dist, fwd.y * dist, 99));   // (seen from the menu camera: from just above the head to the ankles, as the footage) sksh_now = t;
	if (sp_n) sprite_flush(VP, sp_blob, 0);
	glEnable(GL_DEPTH_TEST);
	for (int pass = 0; pass < 2; pass++) {
		sksh_mode = pass ? 0 : 1; float c[4] = { ACY[0], ACY[1], ACY[2], 1 };
		if (pass == 0) {
			sksh_layer(VP, &X, cam, SKSH_E, 0, 1, c, 0.62f * k, 0, 0, 0);    // the cyan haze, strongest at the sides (as_solid)
			sksh_layer(VP, &X, cam, SKSH_H, 0, 1, c, 0.42f * k, 0, 0, 0);    // the hex lines over the whole face
			if (SKSH_HAVE_X) { float ph = sk_clamp((t - 0.40f) / 0.8f, 0, 1); sksh_layer(VP, &X, cam, SKSH_H, 0, 1, c, 1.0f * k, 2, -0.4f + ph, 0); }   // the rising band
			float ga = t < 0.40f ? 0 : t < 0.47f ? (t - 0.40f) / 0.07f : sk_clamp((0.62f - t) / 0.15f, 0, 1);   // the forming flash
			if (ga > 0.005f) { float gw = 110, gh = 150, u0, v0, u1, v1; sksh_tc(SKSH_C, 0, 0, &u0, &v0); sksh_tc(SKSH_C, 1, 1, &u1, &v1);
				V3 r = v3(R[0] * gw, R[1] * gw, R[2] * gw), u = v3(U[0] * gh, U[1] * gh, U[2] * gh), o = X.o;
				V3 p0 = vsub(vsub(o, r), u), p1 = vsub(vadd(o, r), u), p2 = vadd(vadd(o, r), u), p3 = vadd(vsub(o, r), u); float gc[4] = { ACY[0], ACY[1], ACY[2], 0.7f * ga };
				sksh_room(VP, 6); sprite_quad(&p0.x, &p1.x, &p2.x, &p3.x, u0, v1, u1, v0, gc); }
		} else { float rc[4] = { ARIM[0], ARIM[1], ARIM[2], k }; sksh_rim(VP, &X, cam, rc); }
		sprite_flush_mode(VP, sk_sh_tex, sksh_mode);
	}
}
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
		float S[3] = { 1, 1, 0.1f + 0.9f * bk }; int add = O->phase == 0 && O->type != SKO_DRONE && O->type != SKO_EMP;
		float hc[4] = { HOLO[0], HOLO[1], HOLO[2], 0.4f + 0.4f * bk }; const float *tn = add ? hc : WHITE;
		MBone P[48];
		switch (O->type) {
		case SKO_TURRET: if (sk_mdl[SKM_TURRET].nb <= 3) { sk_turret_pose(o, P); sk_mdraw(SKM_TURRET, VP, O->pos, O->yaw0 + 1.5707963f, add ? S : NULL, P, tn, add, 0, O->tier >= 2 ? 8 : sk_mdl[SKM_TURRET].nmesh - 1, fogc, O->lit); }
			else { const MBone *pp = sk_obj_pose(SKM_TURRET, O, P); if (pp && sk_rest_ok[SKM_TURRET]) sk_turret_aim(P, sk_wrap(O->yaw - O->yaw0), O->pitch);   // rigged: its own clips + our aim
				sk_mdraw(SKM_TURRET, VP, O->pos, O->yaw0 + 1.5707963f, add ? S : NULL, pp, tn, add, 0, O->tier >= 2 ? 8 : sk_mdl[SKM_TURRET].nmesh - 1, fogc, O->lit); }
			break;
		case SKO_DRONE: sk_mdraw(SKM_DRONE, VP, O->cur, O->yaw + 1.5707963f, NULL, sk_obj_pose(SKM_DRONE, O, P), WHITE, 0, 0, 8, fogc, O->lit); break;
		case SKO_EMP: { const MBone *pp = sk_obj_pose(SKM_EMP, O, P);
			if (!pp && sk_emp_spin >= 0 && O->phase == 0) { model_sample(&sk_mdl[SKM_EMP], sk_emp_spin, O->t * (1 + O->t), P); pp = P; }
			sk_mdraw(SKM_EMP, VP, O->pos, O->yaw0 + 1.5707963f, NULL, pp, WHITE, 0, 0, 8, fogc, O->lit); } break;
		case SKO_RECON: { const MBone *pp = sk_obj_pose(SKM_MINI, O, P); sk_mdraw(SKM_MINI, VP, O->pos, O->yaw0 + 1.5707963f, pp ? NULL : add ? S : NULL, pp, tn, add, 0, 8, fogc, O->lit); } break;
		case SKO_WALL: sk_mdraw(SKM_WALLPOD, VP, O->pos, atan2f(O->nrm.y, O->nrm.x) - 1.5707963f, NULL, NULL, WHITE, 0, 0, 8, fogc, O->lit); break;
		case SKO_SEEKER: if (sk_mok[SKM_MINI]) {   // b117: the mini Tachikoma (was b85's flying hull): a hologram while it is projected, standing up, running, then its leap
			const Model *M = &sk_mdl[SKM_MINI]; const MBone *pp = NULL; float tb = O->t - SK_SEEK_BUILD, len; int boot = O->phase == 1 && tb < SK_SEEK_BOOT, cd = sk_clip[SKM_MINI][SKA_DEPLOY];
			int c = O->phase == 2 && sk_mini_jump >= 0 ? sk_mini_jump : boot && cd >= 0 && model_clip_len(M, cd) > SK_SEEK_BOOT ? cd : O->phase == 1 && O->nrm.x * O->nrm.x + O->nrm.y * O->nrm.y > 100 ? sk_clip[SKM_MINI][SKA_WALK] : sk_clip[SKM_MINI][SKA_IDLE];   /* (standing with nobody to hunt: its idle) */
			if (c >= 0 && M->nb <= 48) { len = model_clip_len(M, c); float t = O->phase == 0 ? 0 : O->phase == 2 ? fminf(O->fire_t / SK_SEEK_FUSE * len, len) : c == cd ? len - SK_SEEK_BOOT + tb : len > 0 ? fmodf(O->clip_t * 1.6f, len) : 0;   /* (standing up: the end of its deploy clip, the unfolding) */
				model_sample(M, c, t, P); pp = P; }
			if (O->phase == 0) { float k = sk_clamp(O->t / SK_SEEK_BUILD, 0, 1), hs[3] = { 1, 1, 0.15f + 0.85f * k }; const float bc[4] = { 0.75f, 1, 1, 0.5f + 0.4f * k }, rc[4] = { 1, 0.25f, 0.2f, 0.8f }; sk_mdraw(SKM_MINI, VP, O->cur, O->yaw + 1.5707963f, hs, pp, O->t < 0.07f ? rc : bc, 1, 0, 8, fogc, NULL); }   /* (its first frame: a red cap) */
			else { static const float CY[4] = { 0.45f, 0.95f, 1, 1 }, RD[4] = { 1, 0.35f, 0.3f, 1 };   // its dish blinks white / cyan (SUMMON.usm: one frame in three white); armed, white / red and faster
				const float *tc = O->phase == 2 ? (fmodf(O->fire_t, 0.12f) < 0.06f ? RD : WHITE) : fmodf(O->t, 0.3f) < 0.1f ? WHITE : CY;
				sk_mdraw(SKM_MINI, VP, O->cur, O->yaw + 1.5707963f, NULL, pp, tc, 0, 0, 8, fogc, O->lit); } } break;
		case SKO_DECOY: if (sk_mok[SKM_GHOST]) {   // b84: Reiko's Tier 2 decoy - a standing figure of light, flickering (more when shot)
			float fl = 0.75f + 0.25f * sinf(sk_now * 23 + o) - (O->hit_t > 0 ? 0.35f * sk_rnd() : 0), fa = sk_clamp(O->t / 0.4f, 0, 1) * sk_clamp((O->life - O->t) / 0.6f, 0, 1)
				* sk_clamp((sk_hdist(O->pos, v3(cam[0], cam[1], cam[2])) - 70) / 110, 0, 1);   // b121: gone from 70 cm in (seen from inside it was a cyan slab over half the picture: emu b120-2)
			const float dc[4] = { 0.35f * fl, 0.95f * fl, 1.0f * fl, 0.85f * fa }; sk_mdraw(SKM_GHOST, VP, O->pos, O->yaw + 1.5707963f, NULL, NULL, dc, 1, 1, 2, fogc, NULL); } break;
		}
	}
	for (int k = 0; k < 4; k++) { SkGhost *G = &SKG[k]; if (!G->on) continue; MBone P[48]; const Model *M = &sk_mdl[G->mi];   // destroyed / folding deployables
		model_sample(M, sk_clip[G->mi][G->clip], G->t, P); if (G->mi == SKM_TURRET) sk_turret_aim(P, G->yaw, G->pitch);
		sk_mdraw(G->mi, VP, G->pos, G->rz, NULL, P, WHITE, 0, 0, G->mi == SKM_TURRET && G->tier < 2 ? M->nmesh - 1 : 8, fogc, G->lit); }
	// the local player's placement hologram
	if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && SK_DEF[SKA[sk_local].skill].placed == 1 && sk_alive(sk_local)) {
		SkillActor *A = &SKA[sk_local]; const float *hc = A->aim_ok ? HOLO : HOLO_BAD; float y = sk_yaw(sk_local) + 1.5707963f;
		switch (A->skill) {
		case SK_SENTRYGUN: sk_mdraw(SKM_TURRET, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, A->tier >= 2 ? 8 : sk_mdl[SKM_TURRET].nmesh - 1, fogc, NULL); break;
		case SK_EMP: sk_mdraw(SKM_EMP, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, 8, fogc, NULL); break;
		case SK_SPECIALRECON: sk_mdraw(SKM_MINI, VP, A->aim_pt, y, NULL, NULL, hc, 1, 0, 8, fogc, NULL); break;
		case SK_CYBERWALL: { V3 f = sk_pos(sk_local), n = sk_norm(v3(f.x - A->aim_pt.x, f.y - A->aim_pt.y, 0)); float S[3] = { sk_p1(sk_local), 1, sk_p2(sk_local) };
			float h2[4] = { hc[0], hc[1], hc[2], 0.35f }; sk_mdraw(SKM_WALL, VP, A->aim_pt, atan2f(n.y, n.x) - 1.5707963f, S, NULL, h2, 1, 0, 8, fogc, NULL);
			sk_mdraw(SKM_WALLPOD, VP, A->aim_pt, atan2f(n.y, n.x) - 1.5707963f, NULL, NULL, hc, 1, 0, 8, fogc, NULL); } break;
		}
	}
	// additive: walls, shields, EMP spheres
	for (int o = 0; o < SK_MAXOBJ; o++) {
		SkObj *O = &SKO[o]; if (!O->alive) continue; int ally = O->team == lt;
		if (O->type == SKO_WALL && !sk_sh_tex) { float k = O->phase == 0 ? sk_clamp(O->t / 2, 0.05f, 1) : 1, left = O->life - O->t;   // (fallback without sk_shield.gtx)
			float al = 0.8f * (left < 2 ? 0.6f + 0.4f * (sinf(now * 50) > 0) : 1) * (O->hit_t > 0 ? 1.25f : 1);
			float tn[4] = { ally ? 0.4f : 1, ally ? 0.9f : 0.3f, ally ? 1 : 0.25f, al }, S[3] = { O->w, 1, O->h * k };
			sk_mdraw(SKM_WALL, VP, O->pos, atan2f(O->nrm.y, O->nrm.x) - 1.5707963f, S, NULL, tn, 1, 0, 8, fogc, NULL); }
		else if (O->type == SKO_EMP && O->phase == 0) { float r = O->r * sk_clamp(O->t / 1.5f, 0.02f, 1) / 100; float S[3] = { r, r, r }, tn[4] = { 0.3f, 0.5f, 1, 0.25f * (1 - sksh_fog(sk_len(vsub(O->pos, C))) * fogc[3]) };   // (additive: faded by the fog)
			sk_mdraw(SKM_DOME, VP, O->pos, now, S, NULL, tn, 1, 0, 8, fogc, NULL); }
	}
	// (Azuma's plasma shields and Maven's walls: sk_draw_world_late, after the particles)
	glDisable(GL_CULL_FACE);
	// missiles
	for (int m = 0; m < SK_MAXMS; m++) { SkMissile *M = &SKMS[m]; if (!M->on || M->delay > 0) continue; V3 d = sk_norm(M->v);
		float rz = atan2f(d.y, d.x) + 1.5707963f, S[3] = { M->kind ? 2.5f : 1, M->kind ? 0.6f : 1, M->kind ? 2.5f : 1 };
		V3 p = vsub(M->p, vmul(d, 20.5f * S[1]));   // the mesh lies at y -29..-12 (model -y = forward): centre it on the missile
		sk_mdraw(SKM_MISSILE, VP, p, rz, S, NULL, WHITE, 0, 0, 8, fogc, NULL); }
	// heat ghosts of enemies revealed to our team (through walls)
	if (sk_mok[SKM_GHOST] && (sk_draw_mask & SK_DRAW_HEATGHOST)) { glDisable(GL_DEPTH_TEST);
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != lt && SKA[b].heat_t[lt & 1] > 0) {
			V3 v = sk_vel(b); int mv = v.x * v.x + v.y * v.y > 50 * 50; float al = sk_clamp(SKA[b].heat_t[lt & 1] / 1.6f, 0, 1), tn[4] = { 1, 0.32f, 0.12f, al };
			sk_mdraw(SKM_GHOST, VP, sk_pos(b), sk_yaw(b) + 1.5707963f, NULL, NULL, tn, 1, mv ? 0 : 1, mv ? 1 : 2, fogc, NULL); }
		glEnable(GL_DEPTH_TEST); }
	// sprites (fx atlas cells, additive): rings, beams, pillars, sectors, SkillSync lines (depth tested)
	if (fx_ok && fx_tex) {
		for (int o = 0; o < SK_MAXOBJ; o++) { SkObj *O = &SKO[o]; if (!O->alive) continue; int ally = O->team == lt;
			if (O->type == SKO_TURRET && O->phase == 1 && O->target >= 0) { const float col[4] = { ally ? 0.3f : 1, ally ? 0.9f : 0.2f, ally ? 1 : 0.15f, 0.8f };
				sk_beam_q(VP, sk_turret_att(o, "laser"), sk_target_pos(O->target), 4, cam, sk_cell[SKC_BEAM], col); }
			if (O->type == SKO_TURRET && (O->phase == 0 || O->t < 4)) { const float col[4] = { 0.3f, 0.8f, 1, 0.35f };
				sk_arc(VP, vadd(O->pos, v3(0, 0, 3)), 250, O->yaw0, SK_DEF[SK_SENTRYGUN].p1[O->tier >= 2 ? 1 : 0], col); }
			if (O->type == SKO_DRONE && O->target >= 0 && O->phase == 1) { const float col[4] = { 1, ally ? 0.45f : 0.15f, ally ? 0.3f : 0.1f, 0.95f };
				sk_beam_q(VP, vadd(O->cur, v3(0, 0, 11)), sk_target_pos(O->target), 10, cam, sk_cell[SKC_BEAM], col); }
			if (O->type == SKO_DRONE && O->tier >= 2 && O->phase == 1) { const float col[4] = { 1, 0.25f, 0.3f, 0.25f }; V3 gp = v3(O->pos.x, O->pos.y, O->pos.z - 346);   // Tier 2: the drone's red floor area
				sk_ground(VP, gp, O->r, 0, sk_cell[SKC_HEX], col); sk_ground(VP, vadd(gp, v3(0, 0, 1)), O->r, now * 0.2f, sk_cell[SKC_RING], col); }
			if (O->type == SKO_RECON && O->phase == 1) { float k = fmodf(O->t, 2.4f) / 2.4f; const float col[4] = { 0.3f, 0.85f, 1, 0.5f * (1 - k) };
				sk_ground(VP, vadd(O->pos, v3(0, 0, 4)), O->r * k, 0, sk_cell[SKC_RING], col); }
			if (O->type == SKO_EMP && O->phase == 0) { const float col[4] = { 0.3f, 0.7f, 1, 0.45f }; sk_ground(VP, vadd(O->pos, v3(0, 0, 4)), O->r, now, sk_cell[SKC_RING], col);   // (b85: the blast radius shown while it charges)
				float q = O->t / (O->life > 0 ? O->life : 1); const float c2[4] = { 0.4f, 0.8f, 1, 0.4f }; sk_ground(VP, vadd(O->pos, v3(0, 0, 5)), O->r * q, 0, sk_cell[SKC_RING], c2); }
			if (O->type == SKO_SEEKER) {   // b117 (the game's own skill video SUMMON.usm): while it forms a thin line above it, the glowing cap, the ring on the floor and the white blocks; a burst of light when it starts to run; its eye; the leap's glare
				V3 top = vadd(O->cur, v3(0, 0, 34)); float tb = O->t - SK_SEEK_BUILD - SK_SEEK_BOOT;
				if (O->phase == 0) { float k = sk_clamp(O->t / SK_SEEK_BUILD, 0, 1); const float ln[4] = { 0.45f, 0.95f, 1, 0.7f * (1 - 0.5f * k) }, cap[4] = { 0.8f, 1, 1, 0.95f }, rg[4] = { 0.35f, 0.95f, 1, 0.7f }, bx[4] = { 1, 1, 1, 0.95f };
					sk_beam_q(VP, top, vadd(top, v3(0, 0, 260)), 1.2f, cam, sk_cell[SKC_BEAM], ln); sk_bill(VP, vadd(top, v3(0, 0, 6)), 64, 26, R, U, sk_cell[SKC_FLARE], cap);
					sk_ground(VP, vadd(O->cur, v3(0, 0, 3)), 26 + 22 * k, 0, sk_cell[SKC_RING], rg);
					if (k > 0.25f && sk_cell[SKC_UPLINE] >= 0) { int fr = (int)(O->t * 30); for (int q = 0; q < 9; q++) if ((fr * 7 + q * 5 + o * 3) % 4) {   // the 3 x 3 white blocks over the cap, flickering
						float bx_ = (q % 3 - 1) * 9.0f, bz_ = (q / 3 - 1) * 9.0f + 16; V3 bp = v3(top.x + R[0] * bx_, top.y + R[1] * bx_, top.z + bz_); sk_bill(VP, bp, 10, 10, R, U, sk_cell[SKC_UPLINE], bx); sk_bill(VP, bp, 10, 10, R, U, sk_cell[SKC_UPLINE], bx); } } }   /* (twice: the cell is a faint square with a bright core) */
				else if (O->phase == 1) { const float gl[4] = { 0.35f, 0.95f, 1, 0.55f }; sk_bill(VP, vadd(O->cur, v3(0, 0, 22)), 16, 16, R, U, sk_cell[SKC_FLARE], gl);
					if (tb >= 0 && tb < 0.4f) { float f = 1 - tb / 0.4f; const float ry[4] = { 0.6f, 1, 1, 0.55f * f }, fl[4] = { 0.7f, 1, 1, 0.8f * f };   // off it goes: rays of light up from the dish
						sk_beam_q(VP, top, vadd(top, v3(0, 0, 220 + 200 * tb)), 26 * f + 4, cam, sk_cell[SKC_BEAM], ry); sk_bill(VP, vadd(top, v3(0, 0, 6)), 90 * f + 20, 60 * f + 14, R, U, sk_cell[SKC_FLARE], fl); } }
				else { float k = sk_clamp(O->fire_t / SK_SEEK_FUSE, 0, 1), pu = 0.6f + 0.4f * sinf(O->fire_t * 50); const float gl[4] = { 0.6f, 1, 1, 0.9f * pu }; sk_bill(VP, vadd(O->cur, v3(0, 0, 22)), 26 + 60 * k, 26 + 60 * k, R, U, sk_cell[SKC_FLARE], gl); } }
		}
		for (int a = 0; a < SK_MAXACT; a++) { if (!sk_alive(a)) continue; SkillActor *A = &SKA[a];
			float hp_ = sk_heat_pulse(a);
			if (hp_ > 0 && !sk_hostile(a, sk_local)) { float pt = 1 - hp_; const float col[4] = { 1, 0.45f, 0.2f, 0.7f * hp_ };   // the scan front: a band sweeping out to 40 m
				sk_band(VP, vadd(sk_pos(a), v3(0, 0, -50)), 4000 * pt + 50, 350, 32, sk_cell[SKC_HEAT], col); }
			if (sk_is_active(a, SK_METABOOST) && A->t < 1.2f) { const float col[4] = { 0.85f, 1, 1, 0.8f * (1 - A->t / 1.2f) };
				for (int b = 0; b < SK_MAXACT; b++) if (b != a && (A->linked >> b & 1) && sk_alive(b)) sk_beam_q(VP, sk_chest(a), sk_chest(b), 4, cam, sk_cell[SKC_BEAM], col);
				const float rc[4] = { 0.3f, 0.9f, 1, 0.6f * (1 - A->t / 1.2f) }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 5)), (A->tier == 2 ? 1000 : 150) * A->t / 1.2f + 10, 0, sk_cell[SKC_RING], rc); }
			if (sk_is_active(a, SK_CYBERDIVE)) { const float col[4] = { 0.3f, 0.9f, 1, 0.7f }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 4)), 70, now * 1.5f, sk_cell[SKC_DRING], col);
				const float c2[4] = { 0.2f, 0.6f, 0.8f, 0.3f }; sk_ground(VP, vadd(sk_pos(a), v3(0, 0, 3)), 60, 0, sk_cell[SKC_DISK], c2); }
		}
		// SkillSync from the local player's side: white floor lines to the allies who can download our Tier 2; a cyan line + rings
		// to a teammate whose Tier 2 we can download (footage: tutorial 6:24-6:45, Neon Ruins 1:42)
		if (sk_valid(sk_local) && sk_alive(sk_local)) { V3 me = sk_pos(sk_local);
			for (int b = 0; b < SK_MAXACT; b++) {
				if (sk_sync_link(sk_local, b) == 1) { const float col[4] = { 0.9f, 0.95f, 1, 0.75f }; sk_floor_line(VP, me, sk_pos(b), 6, sk_cell[SKC_BEAM], col); }
				if (sk_sync_link(b, sk_local) == 1) { V3 sp = sk_pos(b); const float col[4] = { 0.25f, 0.85f, 1, 0.85f }, c2[4] = { 0.25f, 0.8f, 1, 0.5f };
					sk_floor_line(VP, me, sp, 8, sk_cell[SKC_BEAM], col); sk_ground(VP, vadd(sp, v3(0, 0, 3)), 55, 0, sk_cell[SKC_DISK], c2); sk_ground(VP, vadd(sp, v3(0, 0, 4)), 65, now, sk_cell[SKC_DRING], col); }
			}
			for (int b = 0; b < SK_MAXACT; b++) if (b != sk_local && sk_alive(b) && !sk_hostile(b, sk_local) && SKA[b].sx_t > 0) {   // allies carrying a downloaded skill: a small cyan ring
				const float c3[4] = { 0.25f, 0.8f, 1, 0.35f }; sk_ground(VP, vadd(sk_pos(b), v3(0, 0, 3)), 45, -now, sk_cell[SKC_DRING], c3); }
		}
		if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && sk_alive(sk_local)) { SkillActor *A = &SKA[sk_local]; const float *hc = A->aim_ok ? HOLO : HOLO_BAD;
			float col[4] = { hc[0], hc[1], hc[2], 0.7f };
			if (A->skill == SK_DMDDRONE) { sk_pillar_q(VP, A->aim_pt, 600, 40, cam, col); sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 3)), A->tier == 2 ? 1500 : 110, now, sk_cell[A->tier == 2 ? SKC_RING : SKC_DISK], col); }
			else if (A->skill != SK_ARMLAUNCHER) { float fr = A->skill == SK_SENTRYGUN ? 70 : A->skill == SK_EMP ? sk_p1(sk_local) : A->skill == SK_SPECIALRECON ? 60 : 40;
				sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 3)), fr, now, sk_cell[A->skill == SK_EMP ? SKC_RING : SKC_DRING], col);
				if (A->skill == SK_SENTRYGUN) { const float c2[4] = { 0.3f, 0.8f, 1, 0.3f }; sk_arc(VP, vadd(A->aim_pt, v3(0, 0, 4)), 250, sk_yaw(sk_local), sk_p1(sk_local), c2); }
				if (A->skill == SK_SPECIALRECON) { const float c2[4] = { 0.3f, 0.85f, 1, 0.25f }; sk_ground(VP, vadd(A->aim_pt, v3(0, 0, 4)), sk_p1(sk_local), 0, sk_cell[SKC_RING], c2); } } }
		sprite_flush_mode(VP, fx_tex, 1);
		// markers through walls: recon / wall marks (kal), lock rings, dive targets
		glDisable(GL_DEPTH_TEST);
		for (int b = 0; b < SK_MAXACT; b++) if (sk_alive(b) && SKA[b].team != lt) {
			if (SKA[b].mark_t[lt & 1] > 0 && (sk_draw_mask & SK_DRAW_MARKS)) { const float col[4] = { 1, 0.25f, 0.2f, sk_clamp(SKA[b].mark_t[lt & 1], 0, 1) }; sk_marker(VP, vadd(sk_pos(b), v3(0, 0, 220)), 8, 25, cam, R, U, col); }
			if (sk_is_active(sk_local, SK_CYBERDIVE)) { float dl = sk_len(vsub(sk_chest(b), sk_eye(sk_local))); if (dl < 4000) { float s = dl < 300 ? 50 + (300 - dl) * 0.6f : 50; const float col[4] = { 1, 0.15f, 0.15f, 0.9f };   /* b121: 1 m across (EP26 7:17: 45 of 720 px at 15 m; was 60 cm) */   // b84: every enemy a red ring through the walls (EP26 7:17-7:21; up close a big one, 7:18.5)
				sk_bill(VP, sk_chest(b), s, s, R, U, sk_cell[SKC_RING], col); const float c2[4] = { 1, 0.2f, 0.15f, 0.5f }; sk_ground(VP, vadd(sk_pos(b), v3(0, 0, 4)), 40, 0, sk_cell[SKC_DISK], c2); } }
		}
		if (sk_valid(sk_local) && SKA[sk_local].state == SKS_AIM && SKA[sk_local].skill == SK_ARMLAUNCHER)
			for (int k = 0; k < SKA[sk_local].nlocks; k++) { const float col[4] = { 1, 0.2f, 0.15f, 0.95f }; float s = 32 + 6 * sinf(now * 9 + k);
				sk_bill(VP, sk_target_pos(SKA[sk_local].locks[k]), s, s, R, U, sk_cell[SKC_DRING], col); }
		sprite_flush_mode(VP, fx_tex, 1);
		glEnable(GL_DEPTH_TEST);
	}
	glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
// after the particles (main.c, after fx_draw; before the depth clear of the gun): every shield and cyber wall, far to near -
// emissive glass over the smoke and sparks behind it - then the player's own first-person shield with no depth test: it is in
// front of everything in the world (nothing can cover it, and a wall within 50 cm no longer cuts it; the gun is drawn after)
static void sk_draw_world_late(const float *VP, const float *cam, const float *R, const float *U, const float *fogc) {
	if (!sk_ok) return;
	V3 C = v3(cam[0], cam[1], cam[2]); glDisable(GL_CULL_FACE);
	sk_draw_shields(VP, C, R, U, fogc, SKSH_WORLD);
	if (sk_valid(sk_local) && SKA[sk_local].sh_ph) sk_draw_shields(VP, C, R, U, fogc, SKSH_OWN);
	glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
// full-screen layers of the local player's skill state: after the 3D (and the viewmodel), before the HUD
static void sk_ovl_quad(int cell, float x0, float y0, float x1, float y1, const float *col, float du, float dv) {   // clip-space rectangle
	const float *u = SKV_UV[cell];
	if (fx_trim_on && sk_ovl_sh_said && du == 0 && dv == 0 && (sk_ovl_sh[cell].t || sk_ovl_sh[cell].h)) {   // b81: through its shape (sk_ovl_shapes)
		static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
		if (sp_n + 3 * FXT_RING_T > SP_MAXQ * 6) sprite_flush_mode(I, sk_ovl_tex, (int)u[4]);
		fx_rect_shaped(x0, y0, x1, y1, u[0], u[1], u[2], u[3], col, sk_ovl_sh[cell].t, sk_ovl_sh[cell].h); return; }
	float a[3] = { x0, y1, 0 }, b[3] = { x1, y1, 0 }, c[3] = { x1, y0, 0 }, d[3] = { x0, y0, 0 };
	if (sp_n + 6 > SP_MAXQ * 6) { static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; sprite_flush_mode(I, sk_ovl_tex, (int)u[4]); }
	sprite_quad(a, b, c, d, u[0] + du, u[1] + dv, u[2] + du, u[3] + dv, col);
}
static float sk_low_hp;   // b79 (main.c, per frame): 0 .. 1 how near death the local player is (the footage's white-hazed edges at critical HP)
static void sk_draw_overlays(int a) {
	if (!sk_ok || !sk_ovl_tex || !sk_valid(a)) return;
	sp_n = 0;   // (nothing is queued at this point; never draw stale quads with the overlay matrix)
	if (sk_ovl_sh_ready && !sk_ovl_sh_said) { __sync_synchronize(); sk_ovl_sh_said = 1; logp("%s\n", sk_ovl_sh_msg); }   // b81: the shapes are in (the barrier: the worker's writes seen first)
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	SkillActor *A = &SKA[a]; float now = sk_now; int alive = sk_alive(a);
	glDisable(GL_DEPTH_TEST);
	// mul2x layers first
	int nmul = 0;
	if (alive && sk_is_active(a, SK_CYBERDIVE) && !sk_net_view) { const float w[4] = { 1, 1, 1, 1 }; sk_ovl_quad(SKV_DIVE_MUL, -1, -1, 1, 1, w, 0, 0); nmul++; }   // (b84: the world's network view makes the dark itself)
	if (SKV.heat > 0) { const float w[4] = { 1, 1, 1, 1 }; sk_ovl_quad(SKV_HEAT_MUL, -1, -1, 1, 1, w, 0, 0); nmul++; }
	if (nmul) sprite_flush_mode(I, sk_ovl_tex, 2);
	// additive layers
	if (SKV.camo > 0 && (sk_draw_mask & SK_DRAW_HAZE)) { const float c[4] = { 1, 1, 1, SKV.camo * (0.85f + 0.15f * sinf(now * 3)) }; sk_ovl_quad(SKV_CAMO, -1, -1, 1, 1, c, 0, 0); }
	{ float k = sk_is_active(a, SK_METABOOST) ? 1 - A->t / 1.5f : sk_synced(a, SK_METABOOST) ? 1 - A->sx_age / 1.5f : 0;   // b83: the gel's cyan wash for 1.5 s at the start (EP21 5:45.5 / 6:05.5)
	  if (alive && k > 0) { float pu = (0.6f + 0.4f * (0.5f + 0.5f * sinf(now * 12.566f))) * sk_clamp(k, 0, 1); const float c[4] = { 1, 1, 1, pu }; sk_ovl_quad(SKV_META, -1, -1, 1, 1, c, 0, 0); } }
	// (Azuma's shield: no screen layer - the game draws the panel itself in front of the eye, sk_draw_shields)
	if (alive && sk_rushing(a) && A->dashing) { float e = sk_is_active(a, SK_RUSH) ? A->energy : A->sx_energy, s = 1 + 0.15f * (1 - e); const float c[4] = { 1, 1, 1, 0.9f }; sk_ovl_quad(SKV_RUSH, -s, -s, s, s, c, 0, 0); }
	if (alive && sk_is_active(a, SK_CYBERDIVE) && !sk_net_view) { const float c[4] = { 1, 1, 1, 0.85f }; sk_ovl_quad(SKV_DIVE, -1, -1, 1, 1, c, 0, 0); }   // (b121: none over the network view - the PC's picture is the dark world alone, EP26 4:43-4:45; b84 laid it on at 0.45: a cyan wash over everything)
	if (SKV.vig > 0) { float k = SKV.vig_len > 0 ? sk_clamp(SKV.vig / SKV.vig_len, 0, 1) : 0; const float c[4] = { SKV.vig_c[0], SKV.vig_c[1], SKV.vig_c[2], 0.3f + 0.7f * k }; sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); }
	if (SKV.glitch > 0) { float k = SKV.glitch_len > 0 ? sk_clamp(SKV.glitch / SKV.glitch_len, 0, 1) : 0; const float c[4] = { SKV.glitch_c[0], SKV.glitch_c[1], SKV.glitch_c[2], 0.5f + 0.5f * k };
		float j = (sk_rnd() - 0.5f) * 0.08f, jv = (sk_rnd() - 0.5f) * 0.04f; sk_ovl_quad(SKV_GLITCH, -1 + j, -1 + jv, 1 + j, 1 + jv, c, 0, 0); }
	if (SKV.hlines > 0 && SKV.glitch <= 0.05f) { const float c[4] = { 1, 0.35f, 0.3f, sk_clamp(SKV.hlines / 0.9f, 0, 1) }; float s = fmodf(now * 0.6f, 1) * 0.1f;
		sk_ovl_quad(SKV_HLINES, -1, -1 + s, 1, 1 + s, c, 0, 0); }
	if (SKV.laugh > 0 && alive) { float hy = 0.8f, hx = hy * 2 * 544 / 960;   /* b72: a 2:1 quad as the game's (the ring round; was square: an upright oval) */ /* the logo fills ~45 % of its cell: ~35-40 % of the screen height */ float al = sk_clamp(SKV.laugh / 0.3f, 0, 1); const float c[4] = { 1, 1, 1, al }; sk_ovl_quad(SKV_LAUGH, -hx, -hy, hx, hy, c, 0, 0); }
	if (SKV.blind > 0 && SKV.blind_len > 0) { float k = sk_clamp(SKV.blind / (SKV.blind_len * 0.6f), 0, 1); const float c[4] = { 0.86f, 1, 1, k * k * (3 - 2 * k) };   // b117: a Seeker Drone's flash - white-out, clearing over its last 60 %
		for (int q = 0; q < 2; q++) { sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); sk_ovl_quad(SKV_VIG, -5, -5, 5, 5, c, 0, 0); sk_ovl_quad(SKV_VIG, -1, -9, 1, 9, c, 0, 0); sk_ovl_quad(SKV_VIG, -9, -1, 9, 1, c, 0, 0); } }
	if (SKV.flash > 0) { const float c[4] = { 1, 1, 1, sk_clamp(SKV.flash / 0.2f, 0, 1) }; sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); }
	if (alive && sk_low_hp > 0) { float hb = 0.78f + 0.22f * sinf(now * 7.5f); const float c[4] = { 0.95f, 0.86f, 0.86f, 0.8f * sk_low_hp * hb }; sk_ovl_quad(SKV_VIG, -1, -1, 1, 1, c, 0, 0); }   // b79 (Paz video EP19 5:20-5:36 at 20 HP: the edges wash out white, pulsing)
	sprite_flush_mode(I, sk_ovl_tex, 1);
	if (sk_nj_play >= 0 && a == sk_local) { fxs_draw(&sk_nj_sc[sk_nj_play], sk_nj_t, NULL, 1); glDisable(GL_CULL_FACE); }   // b121: the Net Jump's entry / exit scene
	glEnable(GL_DEPTH_TEST);
}
// ---- HUD (between hud_begin and hud_end)
static void sk_hud_rect(float x0, float y0, float x1, float y1, const float *col) {   // a flat screen rectangle (Vita px)
	float A[6] = { x1 - x0, 0, x0, 0, y1 - y0, y0 }; hud_img(HG.white, HG.screen, A, col, HUD_Z4, -1, 1);
}
static void sk_hud_frame(float x0, float y0, float x1, float y1, float w, const float *col) {   // a rectangle outline
	sk_hud_rect(x0, y0, x1, y0 + w, col); sk_hud_rect(x0, y1 - w, x1, y1, col); sk_hud_rect(x0, y0, x0 + w, y1, col); sk_hud_rect(x1 - w, y0, x1, y1, col);
}
static void sk_hud_icon(int icon, float cx, float cy, float size, const float *col) {   // hud.ghud skill.N centred at a screen point
	char b[16]; snprintf(b, 16, "skill.%d", icon); int im = hud_find_img(b); if (im < 0) return;
	const HImg *I = &himg[im]; float w = I->loc[2] - I->loc[0], h = I->loc[3] - I->loc[1], s = size / fmaxf(1, fmaxf(w, h)), ix = (I->loc[0] + I->loc[2]) * 0.5f, iy = (I->loc[1] + I->loc[3]) * 0.5f;
	float A[6] = { s, 0, cx - ix * s, 0, s, cy - iy * s }; hud_img(im, HG.screen, A, col, HUD_Z4, -1, 1);
}
static void sk_hud_digits(int v, float cx, float cy, float h, const float *col) {   // two digits ("24", "05") in the HUD's number font, centred
	if (HG.n30 < 0) return; const HImg *G = &himg[HG.n30 + 8]; float gh = G->loc[3] - G->loc[1], s = h / fmaxf(1, gh), adv = (G->loc[2] - G->loc[0]) * s * 1.05f;
	int d[2] = { (v / 10) % 10, v % 10 };
	for (int k = 0; k < 2; k++) { const HImg *I = &himg[HG.n30 + d[k]]; float gx = (I->loc[0] + I->loc[2]) * 0.5f, gy = (I->loc[1] + I->loc[3]) * 0.5f, x = cx + (k - 0.5f) * adv;
		float A[6] = { s, 0, x - gx * s, 0, s, cy - gy * s }; hud_img(HG.n30 + d[k], HG.screen, A, col, HUD_Z4, -1, 1); }
}
// ---- Vita button glyphs in skill prompts (Vita prompts rule: no PC keys). Markup: {R} {L} shoulder plates, {DR} D-pad right
// (hud.ghud btn.dpr: the skill button), {SQ} Square (btn.sq: SkillSync), {X} Cross, {O} Circle. Letters inside plates >= 20 px.
enum { SKG_R, SKG_L, SKG_DR, SKG_SQ, SKG_X, SKG_O, SKG_N };
static float sk_glyph_w(int g, float size) { return g == SKG_R || g == SKG_L ? fmaxf(size * 1.25f, 34) : size * 1.15f; }
static void sk_glyph(int g, float x, float cy, float size, const float *col) {   // x: left edge, cy: centre line (Vita px)
	float w = sk_glyph_w(g, size), r = size * 0.55f, cx = x + w * 0.5f;
	if (g == SKG_R || g == SKG_L) {   // shoulder button: a plate (dark fill, outline) with the letter
		float ls = fmaxf(20, size * 0.8f), h = fmaxf(size * 0.95f, ls * 1.15f); static const float bg[4] = { 0.02f, 0.05f, 0.07f, 0.7f };
		float b4[4] = { bg[0], bg[1], bg[2], bg[3] * col[3] }; sk_hud_rect(x, cy - h * 0.5f, x + w, cy + h * 0.5f, b4); sk_hud_frame(x, cy - h * 0.5f, x + w, cy + h * 0.5f, 2, col);
		hud_text(HG.screen, cx, cy + ls * 0.36f, ls, 2, col, HUD_Z4, g == SKG_R ? "R" : "L"); return; }
	int im = g == SKG_DR ? HG.btn_dpr : g == SKG_SQ ? hud_find_img("btn.sq") : -1;
	if (im >= 0) { const HImg *I = &himg[im]; float iw = I->loc[2] - I->loc[0], ih = I->loc[3] - I->loc[1], sc = 2 * r / fmaxf(1, fmaxf(iw, ih)), ix = (I->loc[0] + I->loc[2]) * 0.5f, iy = (I->loc[1] + I->loc[3]) * 0.5f;
		float A[6] = { sc, 0, cx - ix * sc, 0, sc, cy - iy * sc }; static const float W1[4] = { 1, 1, 1, 1 }; float wa[4] = { 1, 1, 1, col[3] };
		hud_img(im, HG.screen, A, g == SKG_SQ ? (col[3] < 1 ? wa : W1) : col, HUD_Z4, -1, 1); return; }
	// drawn fallbacks (Cross / Circle, or a missing image): thin strokes of white quads
	float t = fmaxf(2, size * 0.09f), q = r * 0.7f;
	if (g == SKG_SQ) { sk_hud_frame(cx - q, cy - q, cx + q, cy + q, t, col); return; }
	if (g == SKG_DR) { sk_hud_rect(cx - q * 0.3f, cy - q, cx + q * 0.3f, cy + q, col); sk_hud_rect(cx - q, cy - q * 0.3f, cx + q, cy + q * 0.3f, col); return; }
	if (g == SKG_O) { for (int k = 0; k < 12; k++) { float a0 = k * 0.5236f; sk_hud_rect(cx + cosf(a0) * q - t * 0.5f, cy + sinf(a0) * q - t * 0.5f, cx + cosf(a0) * q + t * 0.5f, cy + sinf(a0) * q + t * 0.5f, col); } return; }
	for (int k = -4; k <= 4; k++) { float o = k * q / 4; sk_hud_rect(cx + o - t * 0.5f, cy + o - t * 0.5f, cx + o + t * 0.5f, cy + o + t * 0.5f, col); sk_hud_rect(cx + o - t * 0.5f, cy - o - t * 0.5f, cx + o + t * 0.5f, cy - o + t * 0.5f, col); }
}
static int sk_glyph_tok(const char *m, int *len) {
	static const char *const T[SKG_N] = { "{R}", "{L}", "{DR}", "{SQ}", "{X}", "{O}" };
	for (int k = 0; k < SKG_N; k++) { int l = (int)strlen(T[k]); if (!strncmp(m, T[k], l)) { *len = l; return k; } }
	return -1;
}
static float sk_prompt_w(const char *m, float size) {
	float w = 0; char seg[2] = { 0, 0 };
	while (*m) { int l, g = sk_glyph_tok(m, &l); if (g >= 0) { w += sk_glyph_w(g, size) + size * 0.25f; m += l; continue; } seg[0] = *m++; w += hud_text_w(seg) * size; }
	return w;
}
// a prompt line with button glyphs, centred on cx, text baseline y, text size (>= 24 px for prompts, Legibility rule)
static void sk_prompt(float cx, float y, float size, const float *col, const char *m) {
	float x = cx - sk_prompt_w(m, size) * 0.5f, cy = y - size * 0.36f; char buf[96]; int n = 0;
	while (1) {
		int l = 0, g = *m ? sk_glyph_tok(m, &l) : -1;
		if (!*m || g >= 0) { if (n) { buf[n] = 0; hud_text(HG.screen, x, y, size, 0, col, HUD_Z4, buf); x += hud_text_w(buf) * size; n = 0; }
			if (!*m) break; sk_glyph(g, x + size * 0.125f, cy, size, col); x += sk_glyph_w(g, size) + size * 0.25f; m += l; continue; }
		if (n < 95) buf[n++] = *m; m++; }
}
static int sk_project(const float *VP, V3 p, float *sx, float *sy) {   // world -> Vita screen px; 0 behind the camera / far off screen
	float x = VP[0] * p.x + VP[4] * p.y + VP[8] * p.z + VP[12], y = VP[1] * p.x + VP[5] * p.y + VP[9] * p.z + VP[13], w = VP[3] * p.x + VP[7] * p.y + VP[11] * p.z + VP[15];
	if (w < 1) return 0; *sx = (x / w * 0.5f + 0.5f) * 960; *sy = (0.5f - y / w * 0.5f) * 544; return *sx > -50 && *sx < 1010 && *sy > -50 && *sy < 594;
}
// the HUD's own skill gauge centre (where it draws the skill icon), Vita px
#ifndef HG_R1_DX
#define HG_R1_DX 0.0f    // (hud_game.h)
#endif
static void sk_gauge_xy(float *x, float *y) {   // (modes: the TIER 1 circle - the small ring of hud.ghud's skill gauge, between its two countdown digits)
	*x = 46; *y = 503;
	if (HG.in_cc[0] >= 0 && HG.in_cc[1] >= 0 && HG.nc >= 0) { const HImg *G = &himg[HG.nc + 8]; float cx = (G->loc[0] + G->loc[2]) * 0.5f, cy = (G->loc[1] + G->loc[3]) * 0.5f, x1, y1, x2, y2;
		hud_node_screen(HG.in_cc[0], cx, cy, &x1, &y1); hud_node_screen(HG.in_cc[1], cx, cy, &x2, &y2); *x = (x1 + x2) * 0.5f + HG_R1_DX; *y = (y1 + y2) * 0.5f; return; }   // (b55: the small ring sits HG_R1_DX right of its layout)
	if (HG.in_skill < 0) return;
	int im = hud_find_img("skill.8"); if (im < 0) return; const HImg *I = &himg[im]; hud_node_screen(HG.in_skill, (I->loc[0] + I->loc[2]) * 0.5f, (I->loc[1] + I->loc[3]) * 0.5f, x, y);
}
// HUD additions (text sizes follow the legibility rule: labels >= 24 px, fine print >= 20 px, headings 32 px): the active-skill bar under the crosshair with a small copy of the icon above it (footage 1280x720 bar 608..673 x 593..600
// -> Vita 456..505 x 445..450), the Tier-1 countdown on the gauge, "TIER 2 SKILL READY", SkillSync slots / prompt / downloaded icon,
// the AIM prompt, "EMP" / "HACKED". VP: the world view-projection (for the prompt above a sharer; NULL = none).
static void skills_hud(int a, const float *VP, float now) {
	if (!sk_ok || !sk_valid(a) || !HG.ok || HG.white < 0 || HG.screen < 0) return;
	SkillActor *A = &SKA[a]; int alive = sk_alive(a);
	static const float CYAN[4] = { 0.55f, 0.92f, 1, 0.95f }, YEL[4] = { 1, 0.78f, 0.2f, 1 }, wm[4] = { 1, 1, 1, 0.9f }, rd[4] = { 1, 0.35f, 0.3f, 0.95f };
	float gx, gy; sk_gauge_xy(&gx, &gy);
	HUD_TAG("sk.bar");   // ui b42: element names for the host overlap checker (no-op on the Vita)
	if (alive && A->bar >= 0) {
		int own = A->state == SKS_ACTIVE || A->state == SKS_AIM || (A->obj >= 0 && SKO[A->obj].alive) || A->sx_t <= 0, icon = own ? SK_DEF[A->skill].hud_icon : SK_DEF[A->sx_skill].hud_icon;
		// b95 (user: the speed boost as the footage; the Paz video EP19 5:09-5:15): the PC's centre skill gauge (hud.ghud sg.*, the
		// movie's skillGauge) - the skill's white icon over a framed cyan bar that drains; the plain bar + small icon without it
		static int sg_ok = -1, sg_bg, sg_fill, sg_nbg, sg_nfill, sg_nicon;
		if (sg_ok < 0) { sg_bg = hud_find_img("sg.bg"); sg_fill = hud_find_img("sg.fill"); sg_nbg = hud_find_node("sg.bg"); sg_nfill = hud_find_node("sg.fill"); sg_nicon = hud_find_node("sg.icon");
			sg_ok = sg_bg >= 0 && sg_fill >= 0 && sg_nbg >= 0 && sg_nfill >= 0 && sg_nicon >= 0; }
		char sgn[16]; snprintf(sgn, sizeof(sgn), "sgi.%d", icon); int sgi = sg_ok ? hud_find_img(sgn) : -1;
		if (sg_ok && sgi >= 0) {
			float f = sk_clamp(A->bar, 0, 1), x0 = himg[sg_fill].loc[0], Af[6] = { f, 0, x0 * (1 - f), 0, 1, 0 };   // (the fill shrunk toward its left end)
			hud_img(sg_bg, sg_nbg, NULL, HUD_W1, HUD_Z4, -1, 1); if (f > 0) hud_img(sg_fill, sg_nfill, Af, HUD_W1, HUD_Z4, -1, 1);
			hud_img(sgi, sg_nicon, NULL, HUD_W1, HUD_Z4, -1, 1);
		} else {
			float B0[6] = { 49, 0, 456, 0, 5, 445 }, B1[6] = { 49 * sk_clamp(A->bar, 0, 1), 0, 456, 0, 5, 445 };
			static const float bg[4] = { 0, 0, 0, 0.35f }, fill[4] = { 0.33f, 0.78f, 0.95f, 0.95f };
			hud_img(HG.white, HG.screen, B0, bg, HUD_Z4, -1, 1); if (A->bar > 0) hud_img(HG.white, HG.screen, B1, fill, HUD_Z4, -1, 1);
			sk_hud_icon(icon, 480.5f, 434.5f, 15, CYAN); }
		// b83 (EP20 6:17, 9:35): a Tier 2 Heat Sensor (own or downloaded) shows its scans left as white dots under the bar
		int dots = sk_is_active(a, SK_HEATSCAN) && A->tier == 2 ? (int)SK_DEF[SK_HEATSCAN].p1[1] - A->pulses
		         : !own && A->sx_skill == SK_HEATSCAN ? (int)SK_DEF[SK_HEATSCAN].p1[2] - A->sx_pulses : 0;
		static const float dw[4] = { 1, 1, 1, 0.92f };
		for (int k = 0; k < dots && k < 8; k++) { float x = 480 - (dots - 1) * 5.0f + k * 10, y = sg_ok && sgi >= 0 ? 8 : 0; sk_hud_rect(x - 2, 454 + y, x + 2, 458 + y, dw); }   // (b95: under the PC gauge's bar)
	}
	// Tier-1 countdown on the gauge (final build: the TIER 1 circle counts 24..00): hud_game.h draws it in the small ring with the
	// movie's cooldown digits (HudState.cool_s, filled by hud_match.h); without that pack, here
	if (A->cool > 0 && alive && !sk_blocked(a) && (HG.nc < 0 || HG.in_cc[0] < 0)) { static const float dg[4] = { 1, 1, 1, 0.95f }; sk_hud_digits((int)ceilf(A->cool), gx, gy, 18, dg); }
	// "TIER 2 SKILL READY" (yellow icon + text + line at the screen centre, 3 s)
	HUD_TAG("sk.t2ready");
	// ui b42 (HUD de-clutter, user report b41): the line within the centre column (it ran across the screen, under the minimap and the
	// kill feed), and the notice moved clear of the banners / notices (hud.h claims; DOUBLE KILL +100 keeps clear of it in turn)
	if (SKV.t2n > 0 && a == sk_local) { float al = sk_clamp(SKV.t2n / 0.4f, 0, 1) * sk_clamp((3.0f - SKV.t2n) / 0.15f, 0, 1); float y4[4] = { YEL[0], YEL[1], YEL[2], al }, ln[4] = { YEL[0], YEL[1], YEL[2], 0.6f * al };
	  float dy = hud_place(196, 149, 764, 260, HUD_P_SKILL, 88, 320);
	  if (dy < HUD_HIDE) {
		float lw = 284 * sk_clamp((3.0f - SKV.t2n) / 0.5f, 0, 1); sk_hud_rect(480 - lw, 168 + dy, 480 + lw, 170 + dy, ln);   // (legibility rule: heading 32 px, fine print 20 px)
		sk_hud_icon(SK_DEF[A->skill].hud_icon, 480, 169 + dy, 40, y4); hud_text(HG.screen, 480, 222 + dy, 32, 2, y4, HUD_Z4, "TIER 2 SKILL READY");
		float g2[4] = { 0.85f, 0.85f, 0.85f, 0.85f * al }; sk_prompt(480, 254 + dy, 24, g2, "HOLD {DR}");
		if (al > 0.02f) hud_claim(196, 149 + dy, 764, 260 + dy, HUD_P_SKILL); } }
	HUD_TAG("sk.sync");
	// SkillSync, sharer: three slots right of the gauge, filled with the names of the allies who downloaded (footage "CCC")
	SkHudInfo hi; sk_hud_info(a, &hi);
	if (alive && hi.nsync >= 0) {
		static const float sl[4] = { 0.75f, 0.8f, 0.85f, 0.85f }, sf[4] = { 0.33f, 0.78f, 0.95f, 0.9f };
		for (int k = 0; k < SK_SYNC_MAX; k++) { float x0 = gx + 46 + k * 22, y0 = gy + 12; if (k < hi.nsync) sk_hud_rect(x0, y0, x0 + 18, y0 + 14, sf); sk_hud_frame(x0, y0, x0 + 18, y0 + 14, 2, sl); }
		for (int k = 0; k < hi.nsync; k++) { float y = gy - 12 - k * 22; sk_hud_rect(gx + 40, y - 5, gx + 54, y - 3, sl); hud_text(HG.screen, gx + 58, y + 2, 20, 0, wm, HUD_Z4, hi.sync_name[k] ? hi.sync_name[k] : "?"); }
	}
	// SkillSync, receiver: the downloaded skill's small icon + time bar above the gauge
	if (alive && hi.sx_icon >= 0) { sk_hud_icon(hi.sx_icon, gx + 36, gy - 38, 22, CYAN); static const float bg[4] = { 0, 0, 0, 0.4f };
		sk_hud_rect(gx + 22, gy - 24, gx + 50, gy - 21, bg); sk_hud_rect(gx + 22, gy - 24, gx + 22 + 28 * hi.sx_frac, gy - 21, CYAN); }
	HUD_TAG("sk.syncnote");
	if (SKV.sync_note > 0 && a == sk_local) { float al = sk_clamp(SKV.sync_note / 0.4f, 0, 1); float c[4] = { CYAN[0], CYAN[1], CYAN[2], al };
		float dy = hud_place(352, 282, 608, 302, HUD_P_NOTIFY, 88, 340);   // ui b42 (hud.h claims)
		if (dy < HUD_HIDE) { hud_text(HG.screen, 480, 300 + dy, 24, 2, c, HUD_Z4, "SKILLSYNC COMPLETE"); if (al > 0.02f) hud_claim(352, 282 + dy, 608, 302 + dy, HUD_P_NOTIFY); } }
	HUD_TAG("~sk.syncprompt");
	// SkillSync prompt above a teammate whose Tier 2 we can download: "[] <icon> SkillSync" (the interact button is Square)
	if (alive && VP && a == sk_local) { int s = sk_sync_source(a, SK_LINK_R);
		if (s >= 0) { float sx, sy; V3 hd = sk_pos(s); hd.z += 215;
			if (sk_project(VP, hd, &sx, &sy)) { int nearby = sk_len(vsub(sk_pos(s), sk_pos(a))) < SK_SYNC_R; static const float bx[4] = { 0.02f, 0.08f, 0.12f, 0.75f };
				float c[4] = { CYAN[0], CYAN[1], CYAN[2], nearby ? 1 : 0.6f }; sk_hud_rect(sx - 96, sy - 18, sx + 96, sy + 16, bx); sk_hud_frame(sx - 96, sy - 18, sx + 96, sy + 16, 2, c);
				sk_glyph(SKG_SQ, sx - 92, sy - 1, 24, c); sk_hud_icon(SK_DEF[SKA[s].skill].hud_icon, sx - 48, sy - 1, 24, c); hud_text(HG.screen, sx - 30, sy + 8, 24, 0, c, HUD_Z4, "SkillSync"); } } }   // (original: "[E] <icon> SkillSync")
	if (!alive) return;
	if (SKV.seek_t > 0 && HG.screen >= 0) {   // b85: an enemy Seeker Drone after the local player (EP16 7:55.5: red, under the sight)
		float pu = 0.65f + 0.35f * sinf(sk_now * 12.6f), k = sk_clamp(SKV.seek_t / 0.3f, 0, 1); const float c[4] = { 1, 0.22f * pu + 0.1f, 0.18f, k };
		hud_text(HG.screen, 480, 334, 24, 2, c, HUD_Z4, "SEEKER DRONE INCOMING"); }
	HUD_TAG("sk.prompt");
	const char *msg = NULL; char buf[48];
	if (A->state == SKS_AIM) {
		if (A->skill == SK_ARMLAUNCHER) { snprintf(buf, sizeof(buf), "{R} FIRE   %d LOCK%s", A->nlocks, A->nlocks == 1 ? "" : "S"); msg = buf; }
		else msg = A->aim_ok ? (A->tier == 2 ? "TIER 2   {R} DEPLOY   {DR} CANCEL" : "{R} DEPLOY   {DR} CANCEL") : "CAN'T DEPLOY HERE";
	}
	else if (sk_is_active(a, SK_PLASMASHIELD)) msg = A->toggled ? "{DR} - DEACTIVATE SHIELD" : "{DR} - REACTIVATE SHIELD";   // (original: "[Q] - DEACTIVE SHIELD", yellow: Azuma footage b63)
	else if (sk_sync_source(a, SK_SYNC_R) >= 0) msg = "{SQ} SKILLSYNC";
	// bottom centre, under the ammo counter (which sits at y 470-516 when aiming / sprinting, main.c mag_x / mag_y 475 / 490): the
	// original's spot (under the skill bar, 0.85 of the height) is taken by our counter, so the prompt goes to the free strip below it
	{ static const float yl[4] = { 0.98f, 0.9f, 0.3f, 1 };
	  if (msg) sk_prompt(480, 536, 24, A->state == SKS_AIM && !A->aim_ok && A->skill != SK_ARMLAUNCHER ? rd : A->state != SKS_AIM && sk_is_active(a, SK_PLASMASHIELD) ? yl : wm, msg); }
	HUD_TAG("sk.emp");
	if (A->emp_t > 0 && fmodf(now, 0.5f) < 0.3f) hud_text(HG.screen, 480, 405, 32, 2, rd, HUD_Z4, "EMP");
	if (A->hack_t > 0) hud_text(HG.screen, 480, 405, 32, 2, rd, HUD_Z4, "HACKED");
}
