// The tutorial: the original's four training programs, rebuilt from the tutorial footage ("Ghost in The Shell First
// Assault - Tutorial Stuff", 1:04-8:36, and the Early Access intro play-through) on the three tutorial maps:
//   1 CONTROL TRAINING PROGRAM    (tutorial2) basic move, sprint, crouch, walk (sneak past watchers), jump, context movement
//   2 COMBAT TRAINING PROGRAM     (tutorial3) load the weapons program at the INTER GATE, primary, hip fire, aim, precision
//                                 fire, secondary, weapon pick-up (sniper rifle), throwing weapons (grenades) - the
//                                 targets are the original's (world\TutShootBot1.tsd groups, tutorial3.grange, range.h)
//   3 CYBERBRAIN TRAINING PROGRAM (tutorial) skill explanation screens with their videos, therm-optic camo, intel,
//                                 kill all enemies (tier 2), SkillSync sharing / downloading (heat sensor), melee
//                                 stealth kill
//   4 ENEMY THINKTANK             (tutorial, the arena at the end) hacking and destroying a Think Tank (TutTankApi)
// A program starts when a match starts with cfg.tut = 1..4 (menu.h MenuConfig; the front end's program chooser sets it).
// Each program: loading screen (tut_loading_draw), the cyber "connect" sequence, then its steps: the objective band,
// checklist, key guide, operator portrait + voice line, waypoint flag and ring, orange gates that open, SUCCESS, and at
// the end "End Program / Prosthetic Calibration Complete" and back to the lobby (or the next program when the whole
// tutorial is played through, as the original's first run does: control -> combat -> cyberbrain + think tank).
// The dummies (watchers, targets, the melee dummy, the stub Think Tank) are range.h practice targets (the cyan
// holographic cy_male dummy, chr_dummy.gvm), so the player's bullets, grenades and the HUD work on them unchanged;
// the SkillSync allies are real operators (bots.h chrt models) drawn here.
// Intro: tut_play_logos() at start-up and tut_play_intro() before the first program (H.264 movies, tut_video.h).
// main.c hooks: see the list at the end of this file.
#pragma once
#include "tut_ui.h"
#include "tut_video.h"
#ifdef __vita__
#include <psp2/touch.h>
#endif

// ------------------------------------------------------------------ Think Tank interface
// The ENEMY THINKTANK program (EA tutorial ~9:00-11:40): the tank stands in the arena's glass cylinder; "Hacking Think
// Tanks": one press of interact next to it starts the hack (SUCCESS; the objective moves on to "Destroying Think Tanks"),
// "Bypassing encryption..." reveals 10 digits (~18 s), then its defensive protocols are down for ~13 s and it takes far
// more damage; not destroyed in time: "You failed to destroy enemy Think Tank." (Batou: "Think Tank disabling failed.")
// and the objective goes back to "Hacking Think Tanks". The arena's glass stops bullets, not the player: walk in.
// tutorial.h only uses the calls below; it times the retry (TUT_OPEN_T after the shutdown -> rearm). With the tank
// workstream's thinktank.h compiled in (main.c includes ttm.h / thinktank.h before tutorial.h) they drive the real unit:
// its tutorial helpers when present (tk_tut_spawn: TKF_NOAI | TKF_NOREGEN | TKF_NORESTORE - it stands still, never
// shoots, keeps its defences down until tk_rearm - and the EA tutorial's HUD look), else tk_spawn directly; main's Think
// Tank glue (tk_update / tk_use with the interact button / tk_ray + tk_damage for bullets / tk_blast for grenades /
// tk_draw / tk_hud) does the rest, including its prompt, code panel and tag. Without it, a stand-in: a range.h dummy
// with a red ring, the same rules, and the tutorial's own prompt and code panel.
typedef struct {
	int  (*spawn)(const float *pos, float yaw, int team);   // standing at pos (cm, feet), facing yaw, team 1 (enemy) -> handle >= 0
	void (*remove)(int h);
	void (*use)(int h, int held, float dt);  // every frame of the hack / destroy steps: interact held (the stand-in starts its hack here)
	int  (*reach)(int h);                    // 1: the player can start a hack now (prompt "Hacking Think Tank - PRESS {s}")
	float (*hacking)(int h);                 // a hack is running: 0..1; -1 none
	int  (*open)(int h);                     // 1 while the defensive protocols are down
	int  (*destroyed)(int h);                // 1 once destroyed
	float (*health)(int h);                  // 0..1
	void (*pos)(int h, float *out);          // centre (cm), for the waypoint
	void (*hurt)(int h, float dmg);          // the test tour's autoplay shoots it
	void (*rearm)(int h);                    // defences up again (not destroyed in time)
	int own_hud;                             // 1: the unit draws its own prompt, code panel and tag (thinktank.h tk_hud)
} TutTankApi;
#define TUT_HACK_RUN 10.0f   // stand-in timings (thinktank.h: TK_HACK_RUN, TK_OPEN_T; b86: v2.0 10 s, was the EA tutorial's 18)
#define TUT_OPEN_T 13.0f
static int tts_spawn(const float *pos, float yaw, int team);
static void tts_remove(int h);
static void tts_use(int h, int held, float dt);
static int tts_reach(int h);
static float tts_hacking(int h);
static int tts_open(int h);
static int tts_destroyed(int h);
static float tts_health(int h);
static void tts_pos(int h, float *out);
static void tts_hurt(int h, float dmg);
static void tts_rearm(int h);
#if defined(TKF_NOAI) && defined(TK_HACK_RUN)   // thinktank.h (tank workstream) is in: the real unit
static int ttk_dead_[TK_MAX];
static int ttk_spawn(const float *p, float yaw, int team) {
	if (tk_load(VDIR) < 0) { logp("tutorial: think tank models missing (tk_blue.gvm / tk_red.gvm): stand-in\n"); return -1; }
#ifdef TKF_NORESTORE   // the tank workstream's tutorial helper (EA tutorial HUD look, defences kept down until rearmed)
	int i = tk_tut_spawn(p, yaw, team);
#else
	int i = tk_spawn(v3(p[0], p[1], p[2]), yaw, team, -1, "", "Think Tank", TKF_NOAI | TKF_NOREGEN);
#endif
	if (i >= 0) ttk_dead_[i] = 0; return i;
}
static int ttk_ok(int h) { return h >= 0 && h < TK_MAX; }
#ifdef TKF_NORESTORE
static void ttk_remove(int h) { if (ttk_ok(h)) tk_tut_remove(h); }
static void ttk_rearm(int h) { if (ttk_ok(h)) tk_tut_rearm(h); }
#else
static void ttk_remove(int h) { if (ttk_ok(h)) tk_remove(h); }
static void ttk_rearm(int h) { (void)h; }   // (this thinktank.h restores its defences by itself after TK_OPEN_T)
#endif
static void ttk_use(int h, int held, float dt) { (void)h; (void)held; (void)dt; }   // main's glue: tk_use(0, eye, dir, use_held, dt)
static int ttk_reach(int h) { return ttk_ok(h) && tk_use_prog >= 0 && tk[h].hacker < 0 && tk[h].def; }
static float ttk_hacking(int h) { return ttk_ok(h) && tk[h].state == TK_LIVE && tk[h].hacker == 0 ? fminf(1, tk[h].hack_t / TK_HACK_RUN) : -1; }
static int ttk_open(int h) { return ttk_ok(h) && tk[h].state == TK_LIVE && !tk[h].def; }
static int ttk_destroyed(int h) { if (!ttk_ok(h)) return 0; if (tk[h].state == TK_DEAD || tk[h].state == TK_OFF) ttk_dead_[h] = 1; return ttk_dead_[h]; }
static float ttk_health(int h) { return ttk_ok(h) && tk[h].hpmax > 0 && tk[h].state != TK_OFF ? fmaxf(0, tk[h].hp / tk[h].hpmax) : 0; }
static void ttk_pos(int h, float *o) { if (!ttk_ok(h)) { o[0] = o[1] = o[2] = 0; return; } o[0] = tk[h].pl.pos.x; o[1] = tk[h].pl.pos.y; o[2] = tk[h].pl.pos.z + 100; }
static void ttk_hurt(int h, float dmg) { if (ttk_ok(h) && tk[h].state == TK_LIVE) { V3 c = v3(tk[h].pl.pos.x, tk[h].pl.pos.y, tk[h].pl.pos.z + 110); tk_damage(h, dmg, 0, c, v3(1, 0, 0), SKD_BULLET); } }
static TutTankApi tut_tank = { ttk_spawn, ttk_remove, ttk_use, ttk_reach, ttk_hacking, ttk_open, ttk_destroyed, ttk_health, ttk_pos, ttk_hurt, ttk_rearm, 1 };
#define TUT_TANK_REAL 1
#else
static TutTankApi tut_tank = { tts_spawn, tts_remove, tts_use, tts_reach, tts_hacking, tts_open, tts_destroyed, tts_health, tts_pos, tts_hurt, tts_rearm, 0 };
#define TUT_TANK_REAL 0
#endif
static const TutTankApi tut_tank_stub = { tts_spawn, tts_remove, tts_use, tts_reach, tts_hacking, tts_open, tts_destroyed, tts_health, tts_pos, tts_hurt, tts_rearm, 0 };

// ------------------------------------------------------------------ data
enum { TUT_OFF, TUT_CONTROL, TUT_COMBAT, TUT_CYBER, TUT_TANK, TUT_NPROG };
enum { TUS_REACH, TUS_SPRINT, TUS_EXPLAIN, TUS_LOAD, TUS_SWITCH_PRI, TUS_KILLS, TUS_ADS, TUS_SWITCH_SEC, TUS_PICKUP, TUS_GRENADE, TUS_SKILL,
	TUS_SHARE, TUS_DOWNLOAD, TUS_KNIFE, TUS_STEALTH, TUS_HACK, TUS_DESTROY, TUS_END };
enum { TF_CROUCH = 1, TF_WALK = 2, TF_CAMO = 4, TF_LADDER = 8 };   // step flags: reach while crouched, watchers spot running, ... ; ladder climb here
enum { FC_OPERATOR = 12, FC_BATOU = 2, FC_SAITO = 3 };            // ovl pack cm.face.N (12: the tutorial's operator)
typedef struct { float x, y, z, hw, h; int kind; } TutGate;      // gate across a corridor (plane x = const), kind 0 medium, 1 small, 2 large
typedef struct { float x, y, z, yaw; int role; } TutActor;        // role 0 watcher, 1 target, 2 walker (melee), 3 tank
typedef struct {
	int kind; const char *title, *text, *text2;   // text2 replaces text 7 s into the step
	int page, item;                               // checklist page and item this step ticks (-1 none)
	float mx, my, mz, mr;                         // waypoint (mr: reach radius, 0 = none)
	int gate_open, gate_open_at_start;            // gate index opened on success / at the start (-1 none)
	int face; const char *who, *caption, *voice;  // radio line at the start
	int face2; const char *who2, *voice2;         // a second speaker 3.5 s later (same caption)
	unsigned groups; int goal;                    // target groups (bit per group) and how many to destroy
	unsigned keys; int flags; int explain;        // key guide highlight, TF_* flags, explanation screen index
} TutStep;
typedef struct { const char *label; int goal; } TutCheckDef;
typedef struct {
	int map; const char *stem;                    // MAPS[] index (main.c) and stem
	TuiLoading ld;
	float sx, sy, sz, syaw;                       // start
	const char *prim, *sec; int start_knife, chr; // loadout; start with the knife only; operator (CHARS order)
	const TutStep *st; int nst;
	const TutGate *gate; int ngate;
	const TutActor *act; int nact;                // dummies (range targets) of this map, groups = runs of the same role (see tut_groups)
	const TutCheckDef (*page)[6]; int npage;
	const char *end_voice, *end_caption;
} TutProg;

static const TuiExplain TUT_EX[6] = {
	{ "CYBERBRAIN SKILL", "- Each character has their own unique skill, with two tiers: the two rings at the bottom left of the screen.\n- Small ring, TIER 1: tap {right}. It counts down while it recharges.\n- Big ring, TIER 2: fills with Intel. When it turns orange, hold {right} for the stronger Tier 2.\n- While Tier 1 recharges, a tap uses Tier 2 if it is ready.", { 275, 400 } },
	{ "THERM-OPTIC CAMO", "- Motoko uses the Therm-optic Camo skill.\n- Therm-optic Camo cloaks your body for a short time.\n- Therm-optic Camo is excellent for sneaking around enemies, but you are not completely invisible.", { 275, 400 } },
	{ "INTEL", "- Intel is required to use skills.\n- Accrue Intel by completing mission objective or killing enemies to activate your skills.\n- You automatically acquire enough Intel to activate Tier 1 skills over time.", { 275, 400 } },
	{ "SKILLSYNC : SHARING", "- Many Tier 2 skills can be shared with allies.\n- A white line will link to nearby allies able to share your Tier 2 skill.\n- Up to 3 allies can SkillSync your skill, granting them similar effects.", { 275, 400 } },
	{ "SKILLSYNC : DOWNLOADING", "- A link line will appear when your teammate activates their Tier 2 skill\n- Approach your teammate and press {s} to download their skill.\n- You will receive the Tier 1 effect of the shared skill.", { 275, 400 } },
	{ "ENEMY THINKTANK", "- Capturing a terminal in TERMINAL CONQUEST will call in a ThinkTank to support its team.\n- ThinkTanks are equipped with high-tech armor, but can be taken down by hacking its defenses or attacking its ocular sensors.\n- Hacking a ThinkTank will expose your position to enemies.", { 275, 400 } },
};
static const char *TUT_EX_VIDEO[6] = { "tut_v1.mp4", "tut_v2.mp4", "tut_v3.mp4", "tut_v4.mp4", "tut_v5.mp4", "tut_v6.mp4" };   // ui.dat rsc\video\tutorial\tutorial_1..6.usm
#define K(k) TKB(TK_##k)
// ---- 1 CONTROL (tutorial2: the player walks towards -x)
static const TutCheckDef TUT_C_PAGE[1][6] = { { { "Reach Marker" }, { "Sprint" }, { "Crouch" }, { "Walk" }, { "Jump" }, { "Context Movement" } } };
static const TutGate TUT_C_GATE[] = { { 7800, 0, -262, 92, 330, 0 }, { 5400, 0, -262, 90, 330, 0 }, { 3550, 0, -262, 90, 330, 0 }, { 800, 0, -250, 418, 375, 2 } };
static const TutActor TUT_C_ACT[] = { { 3150, 250, -262, -1.5708f, 0 }, { 2650, -250, -262, 1.5708f, 0 }, { 2250, 240, -262, -1.5708f, 0 }, { 1450, -560, -250, 2.3f, 0 } };
static const TutStep TUT_C_ST[] = {
	{ TUS_REACH, "Basic Move", "Use {ls} to reach the marker.", NULL, 0, 0, 7960, 0, -262, 110, 0, -1, FC_OPERATOR, "Operator", "Starting Prosthetic Body Restoration Program", "m_tut_operator_loading", FC_BATOU, "Batou", "m_tut_batou_intro", 0, 0, K(MOVE) | K(LOOK), 0, -1 },
	{ TUS_SPRINT, "Sprint", "Tap {down} while moving forward to sprint.\nThe gate will open if you run for at least 1 second.", NULL, 0, 1, 5520, 0, -262, 1, 1, -1, FC_OPERATOR, "Operator", "Sprint", "m_tut_operator_sprint", 0, NULL, NULL, 0, 0, K(MOVE) | K(SPRINT), 0, -1 },
	{ TUS_REACH, "Crouching", "Press {o} to crouch.\nReach the marker while crouching.", NULL, 0, 2, 4150, 0, -262, 120, 2, -1, FC_OPERATOR, "Operator", "Crouching", "m_tut_operator_crouch", 0, NULL, NULL, 0, 0, K(MOVE) | K(CROUCH), TF_CROUCH, -1 },
	{ TUS_REACH, "Walk", "Push {ls} gently to sneak.\nSneak past the enemies and reach the marker.", NULL, 0, 3, 1000, 0, -250, 140, 3, -1, FC_OPERATOR, "Operator", "Walking", "m_tut_operator_walk", 0, NULL, NULL, 1, 0, K(WALK), TF_WALK, -1 },
	{ TUS_REACH, "Jump", "Press {x} while moving to jump.\nJump over the obstacles and reach the marker.", NULL, 0, 4, -150, 0, -249, 120, -1, -1, FC_OPERATOR, "Operator", "Jump", "m_tut_operator_jump", 0, NULL, NULL, 0, 0, K(MOVE) | K(JUMP), 0, -1 },
	{ TUS_REACH, "Context\nMovement", "Pressing {x} near an obstacle with a {x} icon on it will allow you to vault or climb. Reach the marker.", NULL, 0, 5, -2120, 0, 279, 130, -1, -1, FC_OPERATOR, "Operator", "Special Move", "m_tut_operator_context_movement", 0, NULL, NULL, 0, 0, K(MOVE) | K(JUMP), TF_LADDER, -1 },
	{ TUS_END, NULL, NULL, NULL, 0, 6, 0, 0, 0, 0, -1, -1, FC_BATOU, "Batou", "Ending Prosthetic Body Restoration Program", "m_tut_batou_complete", FC_OPERATOR, "Operator", "m_tut_operator_complete", 0, 0, 0, 0, -1 },
};
// ---- 2 COMBAT (tutorial3: the INTER GATE terminal and the weapon at the centre of the platform; targets x < 0, grenade boxes behind, x > 0)
static const TutCheckDef TUT_S_PAGE[2][6] = { { { "Primary Weapons" }, { "Eliminate target", 5 }, { "Precision Aiming" }, { "Precision Firing", 5 }, { "Secondary Weapons" }, { "Secondary Weapons Fire", 3 } },
	{ { "Weapon Pick Up" }, { "Sniper Rifles", 3 }, { "Throwing Weapons" }, { "Throwing Weapons Throw", 3 } } };
static const TutStep TUT_S_ST[] = {
	{ TUS_LOAD, "Loading\nCombat", "Move to the marker and load the weapons program.\nKeep pressing {s} until the gauge is full.", NULL, -1, -1, 0, 0, 6, 190, -1, -1, FC_BATOU, "Batou", "Firearms Testing Program Loaded", "sh_tut_batou_loading", 0, NULL, NULL, 0, 0, K(USE) | K(MOVE), 0, -1 },
	{ TUS_SWITCH_PRI, "Primary\nWeapons", "Switch to primary weapon to restore firing capability.\nPress {t} to switch to your primary weapon.", NULL, 0, 0, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Primary Weapons", "sh_tut_operator_choose_weapon", 0, NULL, NULL, 0, 0, K(SWAP), 0, -1 },
	{ TUS_KILLS, "Hip Firing", "Neutralize all targets with your primary weapon.\nPress {r} to fire.", NULL, 0, 1, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Primary Weapons", "sh_tut_operator_hip_firing", 0, NULL, NULL, 0x3, 5, K(FIRE), 0, -1 },
	{ TUS_ADS, "Precision\nAiming", "Switch to iron sights aiming for more precise shots.\nHold {l} to aim down the sights.", NULL, 0, 2, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, K(AIM), 0, -1 },
	{ TUS_KILLS, "Precision\nFiring", "Neutralize all targets with your primary weapon in iron sights mode.\nHold {l} to aim down the sights.", NULL, 0, 3, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Precision Firing", "sh_tut_operator_precision_firing", FC_BATOU, "Batou", "sh_tut_batou_precision_firing", 0x1c, 5, K(AIM) | K(FIRE), 0, -1 },
	{ TUS_SWITCH_SEC, "Secondary\nWeapons", "Secondary weapon functionality added.\nPress {t} to switch to your secondary weapon.", NULL, 0, 4, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Secondary Weapons", "sh_tut_operator_secondary", 0, NULL, NULL, 0, 0, K(SWAP), 0, -1 },
	{ TUS_KILLS, "Secondary\nWeapons Fire", "Neutralize all targets with your secondary weapon.\nPress {r} to fire.", NULL, 0, 5, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0x60, 3, K(FIRE), 0, -1 },
	{ TUS_PICKUP, "Weapon Pick\nUp", "Swap your weapon with the weapon at the center of ground.\nHold {s} near the weapon to swap.", NULL, 1, 0, 0, 0, 6, 160, -1, -1, FC_BATOU, "Batou", "Sniper Rifles", "sh_tut_batou_sniper_rifle", 0, NULL, NULL, 0, 0, K(USE) | K(MOVE), 0, -1 },
	{ TUS_KILLS, "Sniper Rifles", "Use the sniper rifle to neutralize all targets.\nHold {l} to bring up the scope.", NULL, 1, 1, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Sniper Rifles", "sh_tut_operator_sniper_rifle", 0, NULL, NULL, 0x180, 3, K(AIM) | K(FIRE), 0, -1 },
	{ TUS_GRENADE, "Throwing\nWeapons", "Throwing weapon functionality added.\nPress {up} to use your throwing weapon.", NULL, 1, 2, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Grenades", "sh_tut_operator_grenade", 0, NULL, NULL, 0, 0, K(GREN), 0, -1 },
	{ TUS_KILLS, "Grenades", "Neutralize all targets behind.\nHold {up} to prime your grenade, then let go to throw.", NULL, 1, 3, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0x200, 3, K(GREN), 0, -1 },
	{ TUS_END, NULL, NULL, NULL, 1, 4, 0, 0, 0, 0, -1, -1, FC_BATOU, "Batou", "End Program", "sh_tut_batou_complete", FC_OPERATOR, "Operator", "sh_tut_operator_complete", 0, 0, 0, 0, -1 },
};
// ---- 3 CYBERBRAIN + 4 THINK TANK (tutorial: the player walks towards -x; the arena with the glass cylinder is at the far end)
static const TutCheckDef TUT_K_PAGE[3][6] = { { { "Reach Marker" }, { "Use Skill" }, { "Therm-optic Camo Movement" }, { "Kill Enemies", 3 } },
	{ { "Reach Marker" }, { "SkillSync : Share" }, { "SkillSync : Download" }, { "Weapon Switching" }, { "Melee" } },
	{ { "Reach Marker" }, { "Hacking Think Tank" }, { "Destroying Think Tank" } } };
static const TutGate TUT_K_GATE[] = { { -2200, 0, -250, 154, 330, 0 }, { -3300, 0, -250, 409, 345, 2 }, { -6650, 0, -250, 88, 330, 0 }, { -7650, 0, -250, 154, 330, 0 },
	{ -10300, 0, -250, 154, 330, 0 }, { -11350, 0, -250, 88, 330, 0 } };
static const TutActor TUT_K_ACT[] = {
	{ -3950, 380, -250, -1.5708f, 0 }, { -4300, -380, -250, 1.5708f, 0 }, { -5300, 700, -250, -1.5708f, 0 }, { -5750, -700, -250, 1.5708f, 0 }, { -6200, 480, -250, -2.2f, 0 }, { -6200, -480, -250, 2.2f, 0 },   // group 0: camo room watchers
	{ -8300, -250, -250, 0, 1 }, { -8550, 200, -250, 0, 1 }, { -8800, -60, -250, 0, 1 },   // group 1: kill all enemies
	{ -10600, 0, -262, 3.14159f, 2 },                                                       // group 2: the melee dummy
	{ -12950, 0, -250, 0, 3 } };                                                             // group 3: the stub Think Tank
// b49: no voice line here names a PC key (audio.h VO_PC_ONLY): the Combat program's loading step keeps Batou's line only, the
// camo step plays Batou's "Turn on your therm-optic camo...", the SkillSync step the operator's SkillSync intro
static const TutStep TUT_K_ST[] = {
	{ TUS_REACH, "Basic\nMovement", "Use {ls} to approach the marker. Tap {down} to sprint.", NULL, 0, 0, -2080, 0, -262, 120, 0, -1, FC_BATOU, "Batou", "Program Loaded", "sk_tut_batou_loading", 0, NULL, NULL, 0, 0, K(MOVE) | K(SPRINT), 0, -1 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 0, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 0 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 0, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 1 },
	{ TUS_SKILL, "Use Skill", "Press {right} to turn on Therm-optic Camo.", NULL, 0, 1, 0, 0, 0, 0, 1, -1, FC_OPERATOR, "Operator", "Cyberbrain Skill Training Initiated", "sk_tut_operator_camo_loading", 0, NULL, NULL, 0, 0, K(SKILL), 0, -1 },
	{ TUS_REACH, "Therm-optic\nCamo", "Use Therm-optic Camo to reach the marker without being detected.", "Therm-optic Camo will not make you completely invisible, but you will be much harder to spot.",
		0, 2, -6480, 0, -250, 160, 2, -1, FC_BATOU, "Batou", "Therm-optic Camo", "sk_tut_batou_camo_start", 0, NULL, NULL, 1, 0, K(SKILL) | K(MOVE), TF_CAMO, -1 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 0, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 2 },
	{ TUS_KILLS, "Kill All\nEnemies", "Kill all enemies to get Intel for using Tier 2 Therm-optic Camo.", NULL, 0, 3, 0, 0, 0, 0, -1, 3, FC_OPERATOR, "Operator", "Intel acquired", "sk_tut_operator_intel_start", 0, NULL, NULL, 2, 3, K(FIRE) | K(AIM), 0, -1 },
	{ TUS_REACH, "Move", "Approach the next marker", NULL, 1, 0, -9350, 0, -250, 150, -1, -1, -1, NULL, NULL, "sk_tut_operator_intel_complete", 0, NULL, NULL, 0, 0, K(MOVE), 0, -1 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 1, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 3 },
	{ TUS_SHARE, "SkillSync", "Hold {right} to turn on Tier 2 Therm-optic Camo and share the skill with your allies.", NULL, 1, 1, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "SkillSync", "sk_tut_operator_skillsync_intro", 0, NULL, NULL, 0, 0, K(SKILL), 0, -1 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 1, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 4 },
	{ TUS_DOWNLOAD, "Heat Sensor", "Your ally activated Tier 2 Heat Sensor.\nPress {s} near your ally to download the ability via SkillSync.", NULL, 1, 2, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "SkillSync: {s}", "sk_tut_operator_skillsync", 0, NULL, NULL, 4, 0, K(USE), 0, -1 },
	{ TUS_KNIFE, "Change\nWeapon", "Hold {left} to take out your melee weapon.", NULL, 1, 3, 0, 0, 0, 0, 4, -1, FC_SAITO, "Saito", "SkillSync complete.", "sk_tut_saito_skillsync_complete", 0, NULL, NULL, 0, 0, K(MELEE), 0, -1 },
	{ TUS_STEALTH, "Melee", "Get close to the enemy without being spotted.\nPress {l} when prompted to perform a stealth kill.", NULL, 1, 4, 0, 0, 0, 0, -1, -1, FC_BATOU, "Batou", "Melee Combat Initiated", "sk_tut_batou_melee_loading_001", 0, NULL, NULL, 4, 1, K(MELEE) | K(MOVE), 0, -1 },
	{ TUS_END, NULL, NULL, NULL, 1, 5, 0, 0, 0, 0, -1, -1, FC_BATOU, "Batou", "End Program", "sk_tut_batou_complete", 0, NULL, NULL, 0, 0, 0, 0, -1 },
	// 4 ENEMY THINKTANK (index TUT_K_TANK0: also the continuation of program 3 in the full run)
	{ TUS_REACH, "Move", "Approach the next marker", NULL, 2, 0, -11750, 0, -250, 170, -1, 5, FC_BATOU, "Batou", "Think Tank", NULL, FC_OPERATOR, "Operator", NULL, 0, 0, K(MOVE), 0, -1 },
	{ TUS_EXPLAIN, NULL, NULL, NULL, 2, -1, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 0, 0, 0, 0, 5 },
	{ TUS_HACK, "Hacking Think\nTanks", "Press {s} when near an enemy Think Tank to hack its defenses.", NULL, 2, 1, 0, 0, 0, 0, -1, -1, FC_OPERATOR, "Operator", "Enemy Think Tank inbound!", NULL, 0, NULL, NULL, 8, 0, K(USE) | K(MOVE), 0, -1 },
	{ TUS_DESTROY, "Destroying\nThink Tanks", "Once the Think Tank's defenses have been hacked, it will take far more damage. Destroy it now.", NULL, 2, 2, 0, 0, 0, 0, -1, -1, -1, NULL, NULL, NULL, 0, NULL, NULL, 8, 0, K(FIRE) | K(AIM), 0, -1 },
	{ TUS_END, NULL, NULL, NULL, 2, 3, 0, 0, 0, 0, -1, -1, FC_BATOU, "Batou", "End Program", "sk_tut_batou_complete", 0, NULL, NULL, 0, 0, 0, 0, -1 },
};
#define TUT_K_TANK0 15
#undef K
static const TutProg TUT_PROG[TUT_NPROG] = {
	{ 0 },
	{ 15, "tutorial2", { "CONTROL TRAINING PROGRAM", NULL, "MOVEMENT CONTROL", "Tier 2 Arm Launcher will fire 2 missiles at once.", "tutorial" },
	  8450, 0, -262, 3.14159f, "", "", 1, 1, TUT_C_ST, sizeof(TUT_C_ST) / sizeof(TUT_C_ST[0]), TUT_C_GATE, 4, TUT_C_ACT, 4, TUT_C_PAGE, 1, NULL, NULL },
	{ 16, "tutorial3", { "COMBAT TRAINING PROGRAM", NULL, "SHOOTING RANGE", "Skills have two levels: Tier 1 & Tier 2. Tier 1 consumes less Intel, but provides less powerful benefits.", "tutorial2" },
	  560, 0, 6, 3.14159f, "m4a1", "seburom5", 1, 1, TUT_S_ST, sizeof(TUT_S_ST) / sizeof(TUT_S_ST[0]), NULL, 0, NULL, 0, TUT_S_PAGE, 2, NULL, NULL },
	{ 14, "tutorial", { "CYBERBRAIN TRAINING PROGRAM", NULL, "SKILL TRAINING", "Tier 2 Cyber Sentries have a much larger cone of detection.", "tutorial3" },
	  -1400, 0, -262, 3.14159f, "m4a1", "seburom5", 0, 1, TUT_K_ST, TUT_K_TANK0, TUT_K_GATE, 6, TUT_K_ACT, 11, TUT_K_PAGE, 2, NULL, NULL },
	{ 14, "tutorial", { "THINK TANK TRAINING PROGRAM", NULL, "ENEMY THINKTANK", "Hacking a ThinkTank will expose your position to enemies.", "tutorial3" },
	  -11050, 0, -262, 3.14159f, "m4a1", "seburom5", 0, 1, TUT_K_ST + TUT_K_TANK0, sizeof(TUT_K_ST) / sizeof(TUT_K_ST[0]) - TUT_K_TANK0, TUT_K_GATE, 6, TUT_K_ACT, 11, TUT_K_PAGE, 3, NULL, NULL },
};

// ------------------------------------------------------------------ state
#define TUT_MAXGRP 12
#define TUT_NALLY 3
enum { TP_NONE, TP_CONNECT, TP_STEP, TP_SUCCESS, TP_END, TP_RESET };
enum { TR_NONE, TR_NEXT, TR_EXIT };
static struct {
	int prog, chain, phase, step, nsteps, page, item_done; const TutStep *st; float t, step_t, phase_t, total_t;
	int gate_state[8]; float gate_t[8];          // 0 closed, 1 opening, 2 open
	int grp[TUT_MAXGRP][RG_MAX], ngrp[TUT_MAXGRP], grp_spawned; int act_rg[16]; int kills0, count, cur_grp; float grp_wait;
	float succ_t, msg_t, port_t, port2_t, obj_t; const char *msg; const float *msg_col;
	float sprint_t, ads_t, hold, hack_t, destroy_t; int use_was, knife_ok, laddering; float ladder_t; V3 ladder_from, ladder_to;
	V3 cp; float cp_yaw; int reset_pending; float reset_t;   // checkpoint
	int tank, tank_rg, tank_open_was, tank_fails; unsigned hack_seed; int pick_def;   // pick_def: the L118 lying on the platform (wpn.h drop), -1: the old stand-in pick-up
	int say_face; const char *say_who, *say_cap; float say_t;   // a radio line outside the step script (failures)
	int shared, downloaded; float heat_t;
	int ex_open; float ex_t; GLuint vtex;
	int walker; float walk_t, walk_pause; int walk_dir; float walk_yaw; int stealth_ok;
	int ledge, ledge_cd;
	int intro_done, ap_done, ap_use; float ap_t; int tour_k, tour_on; float tour_spent, tour_budget;
	// bound main() locals
	Player *pl; float *pitch, *php, *pap, *pdead; int *intel, *fly, *autoplay;
	// allies (skillsync): operator models
	struct { int ct; Anim an; SkinCache sc; int ok; float x, y, z, yaw; } ally[TUT_NALLY];
	GLuint fx; int ok;
} TU;
static int tut_on(void) { return TU.prog > 0; }
static void tut_bind(Player *pl, float *pitch, float *php, float *pap, float *pdead, int *intel, int *fly, int *autoplay) {
	TU.pl = pl; TU.pitch = pitch; TU.php = php; TU.pap = pap; TU.pdead = pdead; TU.intel = intel; TU.fly = fly; TU.autoplay = autoplay;
}
// the MAPS[] index a program runs on (main.c)
static int tut_map_of(int prog) { return prog > 0 && prog < TUT_NPROG ? TUT_PROG[prog].map : -1; }
static const TutProg *tut_p(void) { return &TUT_PROG[TU.prog]; }
static void tut_radio(int face, const char *who, const char *voice, int second) {
	if (voice) voice_play(voice, 230, 0);
	if (face >= 0) { if (second) TU.port2_t = 0; else TU.port_t = 0; }
	(void)who;
}
static void tut_message(const char *s, const float *col) { TU.msg = s; TU.msg_t = 0; TU.msg_col = col; }
static void tut_say(int face, const char *who, const char *caption, const char *voice) { TU.say_face = face; TU.say_who = who; TU.say_cap = caption; TU.say_t = 0; if (voice) voice_play(voice, 230, 0); }

// ------------------------------------------------------------------ init (once, after the HUD / menus / range are up)
static int tut_init(const char *dir) {
	int r = tui_init(dir);
	if (!TU.fx) { char p[200]; snprintf(p, sizeof(p), "%stut_fx.gtx", dir); TU.fx = tui_tex_load(p); }
	TU.ok = r == 0; TU.tank = -1; TU.tank_rg = -1;
	return r == 0 && TU.fx ? 0 : -1;
}

// ------------------------------------------------------------------ the dummies (range.h targets)
static int tut_rg_add(float x, float y, float z, float yaw) {
	if (RG.n >= RG_MAX) return -1; RgTarget *T = &RG.t[RG.n]; memset(T, 0, sizeof(*T));
	T->id = 9000 + RG.n; T->type = 0; T->nwp = 1; T->wp[0][0] = x; T->wp[0][1] = y; T->wp[0][2] = z; T->wp[0][3] = yaw;
	rg_path(T); T->state = RG_OFF; rg_place(T); return RG.n++;
}
static int tut_rg_setup(void) {
	if (!RG.ok) {   // maps without a .grange: the dummy model and an empty target list
		if (rg_model(VDIR) < 0) { logp("tutorial: no dummy model (chr_dummy.gvm)\n"); return -1; }
		rg_textures(); RG.ok = 1; RG.loop = 0; RG.reset = 1.5f; RG.hp = 100; RG.n = 0; RG.ngroup = 0; RG.event = 99; snprintf(RG.stem, sizeof(RG.stem), "%s", tut_p()->stem);
	}
	RG.done = 1; RG.group = 0; RG.np = 0; RG.aim = RG.plate = -1; RG.kills = RG.hits = 0;   // done: range.h's own group sequence stays off
	for (int i = 0; i < RG.n; i++) RG.t[i].state = RG_OFF;
	memset(TU.ngrp, 0, sizeof(TU.ngrp));
	if (TU.prog == TUT_COMBAT) {   // the original's target groups (TutorialStep.txt event 1)
		for (int g = 0; g < RG.ngroup && g < TUT_MAXGRP; g++) { TU.ngrp[g] = RG.g[g].n; memcpy(TU.grp[g], RG.g[g].idx, sizeof(int) * RG.g[g].n); }
	} else {
		const TutProg *P = tut_p(); int g = -1, last = -1;
		for (int i = 0; i < P->nact && i < 16; i++) { const TutActor *A = &P->act[i];
			if (A->role != last) { g++; last = A->role; }
			int k = tut_rg_add(A->x, A->y, A->z, A->yaw); TU.act_rg[i] = k;
			if (k >= 0 && g < TUT_MAXGRP) TU.grp[g][TU.ngrp[g]++] = k; }
	}
	return RG.n;
}
static void tut_group_spawn(int g) { if (g < 0 || g >= TUT_MAXGRP) return; for (int k = 0; k < TU.ngrp[g]; k++) rg_spawn(&RG.t[TU.grp[g][k]]); }
static void tut_group_off(int g) { if (g < 0 || g >= TUT_MAXGRP) return; for (int k = 0; k < TU.ngrp[g]; k++) RG.t[TU.grp[g][k]].state = RG_OFF; }
static int tut_group_dead(int g) { int n = 0; for (int k = 0; k < TU.ngrp[g]; k++) n += RG.t[TU.grp[g][k]].state == RG_DEAD; return n; }

// ------------------------------------------------------------------ Think Tank stand-in: a dummy with lots of armour, the real unit's rules
static struct { int h, hacking, open, was_held; float hack_t, open_t; } TTS = { -1, 0, 0, 0, 0, 0 };
static float tts_hp0 = 3000, tts_hp_open = 260;
static int tts_spawn(const float *pos, float yaw, int team) {
	(void)team; int k = -1;
	for (int i = 0; i < tut_p()->nact; i++) if (tut_p()->act[i].role == 3) k = TU.act_rg[i];
	if (k < 0) k = tut_rg_add(pos[0], pos[1], pos[2], yaw);
	if (k < 0) return -1; RgTarget *T = &RG.t[k]; T->wp[0][0] = pos[0]; T->wp[0][1] = pos[1]; T->wp[0][2] = pos[2]; T->wp[0][3] = yaw; rg_place(T);
	rg_spawn(T); T->hp = tts_hp0; TTS.h = k; TTS.hacking = TTS.open = 0; TTS.hack_t = TTS.open_t = 0; return k;
}
static int tts_ok(int h) { return h >= 0 && h < RG.n && h == TTS.h; }
static void tts_remove(int h) { if (h >= 0 && h < RG.n) RG.t[h].state = RG_OFF; TTS.h = -1; }
static int tts_reach(int h) {
	if (!tts_ok(h) || TTS.hacking || TTS.open || RG.t[h].state != RG_UP) return 0;
	V3 d = vsub(RG.t[h].pos, TU.pl->pos); return d.x * d.x + d.y * d.y < 330.f * 330.f && fabsf(d.z) < 200;
}
static void tts_use(int h, int held, float dt) {   // one press in reach plants the hack; it runs, then the defences are down a while
	if (!tts_ok(h)) return; RgTarget *T = &RG.t[h];
	if (held && !TTS.was_held && tts_reach(h)) { TTS.hacking = 1; TTS.hack_t = 0; play("ui_ghost_hacking_success", 230); }
	TTS.was_held = held;
	if (TTS.hacking && (TTS.hack_t += dt) >= TUT_HACK_RUN) { TTS.hacking = 0; TTS.open = 1; TTS.open_t = 0; if (T->state != RG_DEAD && T->hp > tts_hp_open) T->hp = tts_hp_open; }
}
static void tts_rearm(int h) { if (!tts_ok(h)) return; TTS.open = TTS.hacking = 0; if (RG.t[h].state != RG_DEAD) RG.t[h].hp = tts_hp0; }
static float tts_hacking(int h) { return tts_ok(h) && TTS.hacking ? fminf(1, TTS.hack_t / TUT_HACK_RUN) : -1; }
static int tts_open(int h) { return tts_ok(h) && TTS.open && RG.t[h].state != RG_DEAD; }
static int tts_destroyed(int h) { return h >= 0 && h < RG.n && RG.t[h].state == RG_DEAD; }
static float tts_health(int h) { if (h < 0 || h >= RG.n) return 0; return fmaxf(0, RG.t[h].hp / (TTS.open ? tts_hp_open : tts_hp0)); }
static void tts_pos(int h, float *o) { if (h < 0 || h >= RG.n) { o[0] = o[1] = o[2] = 0; return; } o[0] = RG.t[h].pos.x; o[1] = RG.t[h].pos.y; o[2] = RG.t[h].pos.z + 100; }
static void tts_hurt(int h, float dmg) { if (h >= 0 && h < RG.n && RG.t[h].state == RG_UP) range_damage(h, dmg, 0, RG.t[h].chest, v3(1, 0, 0)); }

// ------------------------------------------------------------------ program start / steps
static void tut_step_begin(int s);
static void tut_set_checkpoint(void) { TU.cp = TU.pl->pos; TU.cp_yaw = TU.pl->yaw; }
static void tut_teleport(V3 p, float yaw) {
	Player *P = TU.pl; P->pos = p; P->vel = v3(0, 0, 0); P->yaw = yaw; *TU.pitch = 0; P->mantle_t = 0; P->slide_t = 0; P->grounded = 0;
}
static void tut_allies_free(void) { for (int i = 0; i < TUT_NALLY; i++) if (TU.ally[i].ok) { anim_free(&TU.ally[i].an); skincache_free(&TU.ally[i].sc); TU.ally[i].ok = 0; } }
static void tut_allies_setup(void) {
	tut_allies_free(); if (TU.prog != TUT_CYBER) return;
	static const char *NM[TUT_NALLY] = { "saito", "ishikawa", "bato" }; static const float Y[TUT_NALLY] = { 320, 0, -320 };
	for (int i = 0; i < TUT_NALLY; i++) { int ct = -1; for (int c = 0; c < nchrt; c++) if (!strcmp(chrt[c].name, NM[i])) ct = c; if (ct < 0) continue;
		TU.ally[i].ct = ct; anim_init(&TU.ally[i].an, &chrt[ct].m); skincache_init(&TU.ally[i].sc, &chrt[ct].m); anim_set_base(&TU.ally[i].an, chrt[ct].idle, 0, 0);
		TU.ally[i].an.base_t = i * 0.37f; TU.ally[i].x = -9900; TU.ally[i].y = Y[i]; TU.ally[i].z = -250; TU.ally[i].yaw = 0; TU.ally[i].ok = 1; }
}
// called by main once the program's map is loaded (want_map block)
static void tut_begin(int prog) {
	TU.prog = prog; if (prog <= 0 || prog >= TUT_NPROG) { TU.prog = 0; return; }
	const TutProg *P = tut_p();
	logp("tutorial: program %d (%s) on %s, %d steps\n", prog, P->ld.program, P->stem, P->nst);
#if defined(TKF_NOAI) && defined(TK_HACK_RUN)
	if (prog == TUT_TANK && tk_load(VDIR) == 0) logp("tutorial: think tank models loaded with the map (b74: were loaded at its spawn, a 0.44 s hitch on the device)\n");
#endif
	TU.ap_done = 0;   // (b31: the previous program's End Program set it again in the frame the tour switched: programs 2 and 4 ran 0 s)
	TU.nsteps = P->nst; TU.step = -1; TU.phase = TP_CONNECT; TU.phase_t = 0; TU.total_t = 0; TU.msg = NULL; TU.msg_t = 9; TU.succ_t = 9; TU.port_t = TU.port2_t = 99;
	memset(TU.gate_state, 0, sizeof(TU.gate_state)); TU.tank = -1; TU.tank_open_was = 0; TU.tank_fails = 0; TU.say_t = 99; TU.shared = TU.downloaded = 0; TU.heat_t = 0; TU.ex_open = 0; TU.laddering = 0; TU.walker = -1;
	voice_stop_all(0);
	nbot = 0;                              // no TDM bots in the training programs
	tut_rg_setup();
	if (P->chr >= 0) { cfg.chr = P->chr; pvoice = op_voice(cfg.chr); skw_match_start(); }
	if (md_ok) {
		if (P->prim[0]) loadout_load(P->prim, P->sec, "knife"); else loadout_load("m4a1", "seburom5", "knife");
		if (P->start_knife && wsl[WS_KNIFE].ok) { WP.sw_to = WS_KNIFE; weapon_finish_switch(); }
	}
	TU.pick_def = -1;
	if (TU.prog == TUT_COMBAT && md_ok) {   // "Weapon Pick Up": the L118A-S1b lies at the centre of the platform (wpn.h drop: hold Square to swap); loaded with the map
		int d = wdef_find("l118"); if (d >= 0 && !(wspare.ok && wspare.def == d)) { wslot_free_ptr(&wspare); if (wslot_load_ptr(&wspare, d, VDIR) < 0) wspare.def = -1; }
		if (d >= 0 && wspare.ok && wspare.def == d) TU.pick_def = d; else logp("tutorial: l118 not loaded: stand-in pick-up\n"); }
	*TU.intel = TU.prog == TUT_CYBER ? 210 : TU.prog == TUT_TANK ? 0 : 0;
	tut_teleport(v3(P->sx, P->sy, P->sz + 5), P->syaw); tut_set_checkpoint();
	*TU.php = 100; *TU.pap = 100;
	tut_allies_setup();
	if (TU.prog == TUT_TANK) TU.gate_state[5] = 0;
	music_ev("bgm/tutorial_matrix", 300);
}
static int tut_page_of(const TutStep *s) { return s->page; }
static void tut_step_begin(int s) {
	const TutProg *P = tut_p(); TU.step = s; TU.st = &P->st[s]; TU.step_t = 0; TU.obj_t = 0; TU.phase = TP_STEP; TU.phase_t = 0;
	const TutStep *S = TU.st; TU.sprint_t = TU.ads_t = TU.hold = 0; TU.use_was = 0; TU.kills0 = 0; TU.count = 0; TU.cur_grp = -1; TU.grp_wait = 0; TU.stealth_ok = 0;
	TU.page = tut_page_of(S);
	if (S->face >= 0 || S->voice) tut_radio(S->face, S->who, S->voice, 0);
	if (S->gate_open_at_start >= 0 && TU.gate_state[S->gate_open_at_start] == 0) { TU.gate_state[S->gate_open_at_start] = 1; TU.gate_t[S->gate_open_at_start] = 0; voice_play("ui_tutorial_gate", 200, 0); }
	tut_set_checkpoint();
	switch (S->kind) {
	case TUS_EXPLAIN: { char p[160]; snprintf(p, sizeof(p), VDIR "%s", TUT_EX_VIDEO[S->explain]); int r = tv_open(p, 1, 0); TU.ex_open = 1; TU.ex_t = 0; TU.vtex = 0;
		if (r < 0) logp("tutorial: video %s not played (%d)\n", p, r); voice_play("ui_tutorial_message", 200, 0); break; }
	case TUS_REACH: if (S->groups && TU.prog != TUT_COMBAT) { for (int g = 0; g < TUT_MAXGRP; g++) if (S->groups >> g & 1) tut_group_spawn(g); } break;
	case TUS_KILLS:
		if (TU.prog == TUT_COMBAT) { TU.cur_grp = -1; for (int g = 0; g < TUT_MAXGRP; g++) if (S->groups >> g & 1) { tut_group_spawn(g); TU.cur_grp = g; break; } }
		else for (int g = 0; g < TUT_MAXGRP; g++) if (S->groups >> g & 1) tut_group_spawn(g);
		if (TU.prog == TUT_CYBER) *TU.intel = 0;   // the camo used it up (footage: 000 after the camo room)
		break;
	case TUS_SKILL: case TUS_SHARE:
		if (sk_valid(0)) { SKA[0].cool = 0; if (SKA[0].state == SKS_COOLDOWN) SKA[0].state = SKS_READY; }
		if (S->kind == TUS_SHARE) { if (*TU.intel < 400) *TU.intel = 400;   // Tier 2 must be ready (hold the skill button)
			if (sk_valid(0) && SKA[0].intel < sk_cost2(0)) { SKA[0].intel = sk_cost2(0); sk_intel_changed(0); } }
		break;
	case TUS_DOWNLOAD: case TUS_STEALTH:
		for (int g = 0; g < TUT_MAXGRP; g++) if ((S->groups >> g & 1) && TU.ngrp[g]) { int k = TU.grp[g][0]; if (RG.t[k].state == RG_OFF || RG.t[k].state == RG_DEAD) rg_spawn(&RG.t[k]); TU.walker = k; }
		if (S->kind == TUS_STEALTH) { TU.walk_t = 0; TU.walk_pause = 0; TU.walk_dir = -1; TU.walk_yaw = 3.14159f; }
		break;
	case TUS_HACK:   // the tank in the arena's glass cylinder (real unit, or the stand-in when thinktank.h / its models are missing)
		if (TU.tank < 0) { float p[3] = { -12950, 0, -250 }; TU.tank = tut_tank.spawn(p, 0, 1);
			if (TU.tank < 0 && tut_tank.own_hud) { tut_tank = tut_tank_stub; TU.tank = tut_tank.spawn(p, 0, 1); }
			logp("tutorial: think tank %d (%s)\n", TU.tank, tut_tank.own_hud ? "thinktank.h" : "stand-in"); }
		TU.hack_seed = (unsigned)(TU.total_t * 1000); TU.tank_open_was = 0; break;
	case TUS_DESTROY: TU.destroy_t = 0; break;
	case TUS_PICKUP: if (TU.pick_def >= 0) { const float c[3] = { S->mx, S->my, S->mz }; const WDef *D = &wdef[TU.pick_def]; wpn_drop_add(TU.pick_def, c, 0.6f, D->mag, D->res); } break;
	case TUS_END: TU.phase = TP_END; TU.phase_t = 0; voice_play("ui_tutorial_success", 200, 0); music_ev("bgm/win", 400); break;
	default: break;
	}
	if (S->face2 >= 0 && (S->voice2 || S->who2)) TU.port2_t = -3.5f; else TU.port2_t = 99;
	logp("tutorial: step %d/%d %s\n", s + 1, P->nst, S->title ? S->title : S->kind == TUS_EXPLAIN ? TUT_EX[S->explain].title : S->kind == TUS_END ? "End Program" : "-");
}
static void tut_success(void) {
	const TutStep *S = TU.st; TU.succ_t = 0; voice_play("ui_tutorial_success", 210, 0);
	if (S->gate_open >= 0 && TU.gate_state[S->gate_open] == 0) { TU.gate_state[S->gate_open] = 1; TU.gate_t[S->gate_open] = 0; voice_play(tut_p()->gate[S->gate_open].kind == 2 ? "ui_tutorial_big_gate" : "ui_tutorial_gate", 210, 0); }
	if (S->page >= 0 && S->item >= 0) TU.item_done = S->item + 1;
	TU.phase = TP_SUCCESS; TU.phase_t = 0;
	logp("tutorial: step %d done (%.1f s)\n", TU.step + 1, TU.step_t);
}
static void tut_next_step(void) {
	if (TU.step + 1 < TU.nsteps) tut_step_begin(TU.step + 1);
	else { TU.phase = TP_END; TU.phase_t = 0; }
}
// back to the step's checkpoint after being spotted / failing / falling
static void tut_fail(const char *msg, const char *voice) {
	if (TU.reset_pending) return;
	tut_message(msg, NULL); if (voice) voice_play(voice, 230, 0); voice_play("ui_tutorial_alarm", 170, 0);
	TU.reset_pending = 1; TU.reset_t = 1.4f; logp("tutorial: step %d failed: %s\n", TU.step + 1, msg ? msg : "-");
}
static void tut_do_reset(void) {
	TU.reset_pending = 0; tut_teleport(TU.cp, TU.cp_yaw); ev_play("ui/respawn", NULL);
	if (TU.st && TU.st->kind == TUS_STEALTH && TU.walker >= 0) { TU.walk_t = 0; TU.walk_pause = 0; TU.walk_dir = -1; TU.walk_yaw = 3.14159f; RgTarget *T = &RG.t[TU.walker]; T->wp[0][0] = -10600; T->wp[0][1] = 0; T->wp[0][3] = 3.14159f; rg_place(T); }
}

// ------------------------------------------------------------------ input filter (main: after its own input locks, before the movement)
// a new tap on the front touch screen (edge); x, y in screen pixels (960x544)
static int tut_tap(float *x, float *y) {
#ifdef __vita__
	static int on, was; if (!on) { sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START); on = 1; }
	SceTouchData td; int now = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) > 0 && td.reportNum > 0, r = now && !was; was = now;
	if (r) { *x = td.report[0].x * 0.5f; *y = td.report[0].y * 0.5f; } return r;
#else
	(void)x; (void)y; return 0;
#endif
}
static int tut_blocking(void) { return TU.phase == TP_CONNECT || TU.ex_open || TU.phase == TP_END || TU.laddering || TU.reset_pending; }
static void tut_autoplay(float dt, float *mx, float *my, int *sprint, int *jump, int *trig, int *aim, int *in_swap, int *in_knife, int *in_gren, int *gren_held, int *use_held, unsigned *pr);
static void tut_input(float dt, unsigned pr, unsigned held, float *mx, float *my, float *rx, float *ry, int *sprint, int *jump, int *trig, int *trig_pr, int *aim, int *want_reload,
	int *in_swap, int *in_knife, int *in_gren, int *gren_held, int *in_cycle, int *use_held) {
	if (!tut_on()) return;
	(void)held;
	if (*TU.autoplay) tut_autoplay(dt, mx, my, sprint, jump, trig, aim, in_swap, in_knife, in_gren, gren_held, use_held, &pr);
	*in_cycle = 0;
	const TutStep *S = TU.st; int k = S ? S->kind : -1;
	float tx = 0, ty = 0; int tap = (TU.ex_open || TU.phase == TP_CONNECT) && tut_tap(&tx, &ty);
	if (tap && TU.ex_open && tx >= TUI_NEXT_X0 - 6 && tx <= TUI_NEXT_X1 + 6 && ty >= TUI_NEXT_Y0 - 6) pr |= SCE_CTRL_CROSS;   // NEXT tapped
	if (tap && TU.phase == TP_CONNECT && tx > 790 && ty > 456) pr |= SCE_CTRL_CROSS;   // SKIP tapped
	if (TU.ex_open && TU.ex_t > 1.0f && (pr & SCE_CTRL_CROSS)) { TU.ex_open = 0; tv_close_soon();   /* perf b56: no 50 ms frame */ voice_play("ui_tutorial_message_e", 200, 0); TU.phase = TP_SUCCESS; TU.phase_t = 1.1f; }
	if (TU.phase == TP_CONNECT && TU.phase_t > 1.0f && (pr & SCE_CTRL_CROSS)) TU.phase_t = fmaxf(TU.phase_t, 4.4f);   // skip the connect sequence
	if (tut_blocking()) { *mx = *my = 0; *rx = *ry = 0; *sprint = *jump = *trig = *trig_pr = *aim = *want_reload = 0; *in_swap = *in_knife = *in_gren = *gren_held = *use_held = 0; return; }
	// weapon functions come back one at a time in the combat program (footage: knife only until "Primary Weapons")
	if (TU.prog == TUT_COMBAT && TU.phase != TP_NONE) {
		int s = TU.step;
		if (s < 1) { *in_swap = 0; *trig = *trig_pr = 0; }
		if (s < 5 && wcur == WS_PRIMARY) *in_swap = 0;       // no secondary yet
		if (s < 9) { *in_gren = 0; *gren_held = 0; }        // no grenades yet
	}
	if (TU.prog == TUT_CONTROL) { *trig = *trig_pr = 0; *in_gren = *gren_held = 0; *in_swap = 0; }
	// interact steps: Square works the terminal / weapon / ally / tank instead of reloading
	if (k == TUS_LOAD || (k == TUS_PICKUP && TU.pick_def < 0) || k == TUS_DOWNLOAD) { if (TU.hold > 0 || *use_held) *want_reload = 0; }
	if ((k == TUS_HACK || k == TUS_DESTROY) && TU.tank >= 0 && tut_tank.reach(TU.tank)) *want_reload = 0;
	// stealth kill
	// (weapons' mapping: a quick knife tap of D-pad left from behind, or L with the knife out; the dummy is a range target, not a bot)
	if (k == TUS_STEALTH && TU.stealth_ok && TU.walker >= 0 && (*in_knife || ((pr & SCE_CTRL_LTRIGGER) && wcur == WS_KNIFE && WP.quick_from < 0))) {
		RgTarget *T = &RG.t[TU.walker]; range_damage(TU.walker, 10000, 1, T->chest, v3(cosf(TU.pl->yaw), sinf(TU.pl->yaw), 0));
		ev_play("impact/knife/body", &T->chest.x); TU.count = 1; }
	// the ladder (context movement): Cross at its foot climbs it
	if (TU.st && (TU.st->flags & TF_LADDER) && *jump && !TU.laddering) {
		V3 p = TU.pl->pos; if (p.x < -1690 && p.x > -1910 && fabsf(p.y) < 80 && p.z < 60 && p.z > -160 && cosf(TU.pl->yaw) < -0.3f) {
			TU.laddering = 1; TU.ladder_t = 0; TU.ladder_from = p; TU.ladder_to = v3(-1985, p.y * 0.3f, 282); *jump = 0; snd_foley(0, "jump", 2, NULL); logp("tutorial: ladder\n"); }
	}
}
// after player_update: closed gates block the corridor, the ladder climb moves the player, falls into the pit reset
static void tut_after_move(float dt) {
	if (!tut_on()) return; Player *P = TU.pl; const TutProg *Pg = tut_p();
	if (TU.laddering) {
		TU.ladder_t += dt; float f = fminf(1, TU.ladder_t / 1.6f), up = fminf(1, f / 0.8f), fw = f < 0.8f ? 0.15f * f / 0.8f : 0.15f + 0.85f * (f - 0.8f) / 0.2f;
		P->pos = v3(TU.ladder_from.x + (TU.ladder_to.x - TU.ladder_from.x) * fw, TU.ladder_from.y + (TU.ladder_to.y - TU.ladder_from.y) * fw, TU.ladder_from.z + (TU.ladder_to.z - TU.ladder_from.z) * up);
		P->vel = v3(0, 0, 0); if (((int)(TU.ladder_t * 4)) != ((int)((TU.ladder_t - dt) * 4))) snd_foley(0, "foot_walk", 2, NULL);
		if (f >= 1) { TU.laddering = 0; P->grounded = 0; }
		return;
	}
	for (int g = 0; g < Pg->ngate; g++) {
		if (TU.gate_state[g] == 2) continue; const TutGate *G = &Pg->gate[g];
		if (fabsf(P->pos.y - G->y) > G->hw + 45 || P->pos.z < G->z - 80 || P->pos.z > G->z + G->h) continue;
		float side = TU.cp.x > G->x ? 1.f : -1.f, d = (P->pos.x - G->x) * side;
		if (d < 34 && d > -150) { P->pos.x = G->x + side * 34; if (P->vel.x * side < 0) P->vel.x = 0; }
	}
	if (P->pos.z < (TU.prog == TUT_CONTROL ? -700.f : -900.f) && !TU.reset_pending) tut_fail(NULL, NULL);
}

// ------------------------------------------------------------------ per frame (after range_update): steps, checks
static float tut_hspeed(void) { return sqrtf(TU.pl->vel.x * TU.pl->vel.x + TU.pl->vel.y * TU.pl->vel.y); }
static float tut_dist2(float x, float y, float z) { V3 p = TU.pl->pos; float dx = p.x - x, dy = p.y - y, dz = p.z - z; return dx * dx + dy * dy + dz * dz * 0.25f; }
// a watcher sees the player: in its 150 degree view, within range, line of sight
static int tut_seen_by(int rg, float range) {
	RgTarget *T = &RG.t[rg]; if (T->state != RG_UP) return 0;
	V3 e = vadd(T->pos, v3(0, 0, 150)), p = vadd(TU.pl->pos, v3(0, 0, 120)), d = vsub(p, e); float l = sqrtf(vdot(d, d)); if (l > range) return 0;
	float f = (d.x * cosf(T->yaw) + d.y * sinf(T->yaw)) / (l + 1e-3f); if (f < 0.26f) return 0;
	return bot_los(e, p);
}
static void tut_walker(float dt) {   // the melee dummy patrols the corridor, facing where it walks; turns at the ends
	if (TU.walker < 0) return; RgTarget *T = &RG.t[TU.walker]; if (T->state != RG_UP && T->state != RG_SPAWN) return;
	float x = T->wp[0][0], ax = -10560, bx = -11080, sp = 150;
	if (TU.walk_pause > 0) { TU.walk_pause -= dt; float tgt = TU.walk_dir < 0 ? 3.14159f : 0; TU.walk_yaw += wrap_pi(tgt - TU.walk_yaw) * fminf(1, 3 * dt); }
	else { x += TU.walk_dir * sp * dt; if (x < bx) { x = bx; TU.walk_dir = 1; TU.walk_pause = 2.2f; } if (x > ax) { x = ax; TU.walk_dir = -1; TU.walk_pause = 2.2f; } TU.walk_yaw = TU.walk_dir < 0 ? 3.14159f : 0; }
	T->wp[0][0] = x; T->wp[0][3] = TU.walk_yaw; rg_place(T);
	if (TU.walk_pause <= 0) { T->pose = 1; anim_update(&RG.an[1], dt); RG.pose[1] = anim_pose(&RG.an[1], -1); }
}
static int tut_update(float dt) {
	if (!tut_on()) return TR_NONE;
	TU.total_t += dt; TU.phase_t += dt; TU.step_t += dt; TU.obj_t += dt; TU.succ_t += dt; TU.msg_t += dt; TU.port_t += dt; TU.port2_t += dt; TU.say_t += dt; TU.heat_t -= dt;
	if (TU.ex_open) { TU.ex_t += dt; GLuint t = tv_frame(); if (t) TU.vtex = t; }
	tv_close_poll();   // perf b56: a movie closed on its own thread: its memory freed when done
	for (int g = 0; g < 8; g++) if (TU.gate_state[g] == 1) { TU.gate_t[g] += dt; if (TU.gate_t[g] > 0.9f) TU.gate_state[g] = 2; }
	if (TU.reset_pending) { TU.reset_t -= dt; if (TU.reset_t <= 0) tut_do_reset(); }
	if (TU.port2_t > 0 && TU.port2_t - dt <= 0 && TU.st && TU.st->voice2) voice_play(TU.st->voice2, 230, 0);
	if (TU.st && TU.st->kind == TUS_STEALTH) tut_walker(dt);
	for (int i = 0; i < TUT_NALLY; i++) if (TU.ally[i].ok) anim_update(&TU.ally[i].an, dt);
	if (*TU.php < 100) *TU.php = 100;
	switch (TU.phase) {
	case TP_CONNECT: if (TU.phase_t > 5.0f) { music_ev("bgm/tutorial", 800); tut_step_begin(0); } return TR_NONE;
	case TP_SUCCESS: if (TU.phase_t > 1.3f) tut_next_step(); return TR_NONE;
	case TP_END:
		if (TU.phase_t > 4.0f && *TU.autoplay && TU.tour_on) { if (!TU.ap_done && TU.prog == TU.tour_k) logp("tutorial: tour program %d done (%.0f s)\n", TU.prog, TU.total_t); TU.ap_done = 1; return TR_NONE; }   // the test tour moves on by itself
		if (TU.phase_t > 6.5f) {
			if (TU.chain && TU.prog == TUT_CYBER) { TU.prog = TUT_TANK; TU.nsteps = TUT_PROG[TUT_TANK].nst; logp("tutorial: on to the Think Tank program\n");
				TU.phase = TP_STEP; { const TutProg *P = &TUT_PROG[TUT_TANK]; (void)P; } tut_step_begin(0); return TR_NONE; }
			return TU.chain && TU.prog < TUT_CYBER ? TR_NEXT : TR_EXIT; }
		return TR_NONE;
	default: break;
	}
	const TutStep *S = TU.st; if (!S) return TR_NONE;
	float t = TU.step_t; int done = 0;
	switch (S->kind) {
	case TUS_REACH: {
		float r2 = S->mr * S->mr; int at = tut_dist2(S->mx, S->my, S->mz) < r2;
		if ((S->flags & TF_CROUCH) && !TU.pl->crouched) at = 0;
		if ((S->flags & TF_CAMO) && sk_valid(0)) {   // tutorial: Tier 1 camo (5 s, 25 s cooldown) holds for the whole room; tap again if it is off
			if (sk_is_active(0, SK_OPTICAL)) { float lim = SK_DEF[SK_OPTICAL].duration[sk_ti(0)] - 1.0f; if (SKA[0].t > lim) SKA[0].t = lim; }
			else if (SKA[0].state == SKS_COOLDOWN) { SKA[0].cool = 0; SKA[0].state = SKS_READY; } }
		if (S->flags & (TF_WALK | TF_CAMO)) {   // the watchers: running past them (walk step) or being seen without camo
			int camo = sk_camo_on(0); float hs = tut_hspeed();
			for (int g = 0; g < TUT_MAXGRP; g++) if (S->groups >> g & 1) for (int k = 0; k < TU.ngrp[g]; k++) {
				int rg = TU.grp[g][k];
				int seen = (S->flags & TF_WALK) ? (hs > 255 && tut_seen_by(rg, 950)) || tut_seen_by(rg, 170) : tut_seen_by(rg, camo ? 230 : 1300);
				if (seen) { tut_fail("You have been spotted!", TU.prog == TUT_CONTROL ? "m_tut_operator_spotted" : "sk_tut_operator_spotted_retry_001"); break; } }
		}
		if (at) done = 1; break; }
	case TUS_SPRINT: if (TU.pl->sprinting && tut_hspeed() > 400) TU.sprint_t += dt; else TU.sprint_t = 0; if (TU.sprint_t >= 1.0f) done = 1; break;
	case TUS_LOAD: {
		int near = tut_dist2(S->mx, S->my, S->mz) < S->mr * S->mr * 1.6f;   // footage: "keep pressing [E]": every press and holding both fill it
		int u = 0;
#ifdef __vita__
		{ SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); u = (c.buttons & SCE_CTRL_SQUARE) != 0; }
#endif
		if (*TU.autoplay) u = 1;
		if (near && u) { if (!TU.use_was) { TU.hold += 0.08f; voice_play("ui_tutorial_shooting_terminal_loading_on", 160, 0); } TU.hold += dt / 2.2f; }
		else TU.hold = fmaxf(0, TU.hold - dt * 0.15f);
		TU.use_was = near && u;
		if (TU.hold >= 1) { TU.hold = 1; done = 1; voice_play("ui_tutorial_shooting_terminal_loading_done", 220, 0); } break; }
	case TUS_SWITCH_PRI: if (wcur == WS_PRIMARY && WP.sw_to < 0) done = 1; break;
	case TUS_SWITCH_SEC: if (wcur == WS_SECONDARY && WP.sw_to < 0) done = 1; break;
	case TUS_ADS: { int a = 0;
#ifdef __vita__
		{ SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); a = (c.buttons & SCE_CTRL_LTRIGGER) != 0; }
#endif
		if (*TU.autoplay && t > 1) a = 1;
		if (a) TU.ads_t += dt; if (TU.ads_t > 0.6f) done = 1; break; }
	case TUS_KILLS: {
		int n = 0; for (int g = 0; g < TUT_MAXGRP; g++) if (S->groups >> g & 1) n += tut_group_dead(g);
		if (n > TU.count && TU.prog == TUT_CYBER) { *TU.intel += 135 * (n - TU.count); if (*TU.intel > 405) *TU.intel = 405; }
		TU.count = n;
		if (TU.prog == TUT_COMBAT && TU.cur_grp >= 0 && tut_group_dead(TU.cur_grp) == TU.ngrp[TU.cur_grp]) {   // next group of the step
			TU.grp_wait += dt; if (TU.grp_wait > 0.8f) { TU.grp_wait = 0; tut_group_off(TU.cur_grp); int g = TU.cur_grp; TU.cur_grp = -1;
				for (int h = g + 1; h < TUT_MAXGRP; h++) if (S->groups >> h & 1) { tut_group_spawn(h); TU.cur_grp = h; break; } }
		}
		if (TU.prog == TUT_COMBAT && wsl[WS_GRENADE].ok && wsl[WS_GRENADE].res < 2) wsl[WS_GRENADE].res = 9;   // grenades never run out here
		if (TU.count >= S->goal) done = 1; break; }
	case TUS_PICKUP: {
		if (TU.pick_def >= 0) {   // wpn.h: the drop in reach + Square held 0.35 s swaps the guns (main); keep it lying there
			if (wsl[WS_PRIMARY].ok && wsl[WS_PRIMARY].def == TU.pick_def) { ev_play("ui/ghost_primary_weapon_pickup", NULL); done = 1; break; }
			int there = 0; for (int i = 0; i < NDROP; i++) if (wdrop[i].on && wdrop[i].def == TU.pick_def) { wdrop[i].t = 0; there = 1; }
			if (!there) { const float c[3] = { S->mx, S->my, S->mz }; const WDef *D = &wdef[TU.pick_def]; wpn_drop_add(TU.pick_def, c, 0.6f, D->mag, D->res); }
			break; }
		int near = tut_dist2(S->mx, S->my, S->mz) < S->mr * S->mr, u = 0;
#ifdef __vita__
		{ SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); u = (c.buttons & SCE_CTRL_SQUARE) != 0; }
#endif
		if (*TU.autoplay) u = 1;
		if (near && u) TU.hold += dt / 0.6f; else TU.hold = 0;
		if (TU.hold >= 1) { if (md_ok) loadout_load("l118", wsl[WS_SECONDARY].ok ? wdef[wsl[WS_SECONDARY].def].sh : "seburom5", "knife"); ev_play("ui/ghost_primary_weapon_pickup", NULL); done = 1; } break; }
	case TUS_GRENADE: if (WP.gren_state != 0) done = 1; if (wsl[WS_GRENADE].ok && wsl[WS_GRENADE].res < 2) wsl[WS_GRENADE].res = 9; break;
	case TUS_SKILL: if (sk_camo_on(0)) done = 1; break;
	case TUS_SHARE: { float d2 = tut_dist2(-9900, 0, -250); if (sk_camo_on(0) && SKA[0].tier == 2 && d2 < 1400.f * 1400.f) { TU.shared = 1; done = 1; } break; }
	case TUS_DOWNLOAD: {
		int near = tut_dist2(TU.ally[0].x, TU.ally[0].y, TU.ally[0].z) < 420.f * 420.f, u = 0;
#ifdef __vita__
		{ SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); u = (c.buttons & SCE_CTRL_SQUARE) != 0; }
#endif
		if (*TU.autoplay) u = t > 1;
		if (near && u && !TU.use_was) {   // Square (skills.h: the SkillSync download button); the Tier 1 effect of the shared skill: the heat scan
			TU.downloaded = 1; TU.heat_t = SK_DEF[SK_HEATSCAN].duration[2] > 0 ? SK_DEF[SK_HEATSCAN].duration[2] : 5.3f; ev_play("intel_skill/heat_scan_start", NULL); done = 1; }
		TU.use_was = u; break; }
	case TUS_KNIFE: if (WP.quick_from >= 0 && wcur == WS_KNIFE) done = 1; if (wcur == WS_KNIFE && WP.quick_from < 0 && t > 0.3f) done = 1; break;
	case TUS_STEALTH: {
		if (TU.count >= 1 || (TU.walker >= 0 && RG.t[TU.walker].state == RG_DEAD)) { done = 1; voice_play("sk_tut_operator_melee_complete_001", 230, 0); break; }
		if (TU.walker < 0) break; RgTarget *T = &RG.t[TU.walker];
		V3 d = vsub(TU.pl->pos, T->pos); float l = sqrtf(d.x * d.x + d.y * d.y) + 1e-3f, f = (d.x * cosf(T->yaw) + d.y * sinf(T->yaw)) / l;
		TU.stealth_ok = l < WST_RANGE + 30 && f < -0.35f;
		if (TU.stealth_ok) wst_prompt = 1;   // wpn.h's prompt ([L] / [LEFT] STEALTH KILL under the crosshair)
		if (l < 750 && f > 0.5f && bot_los(vadd(T->pos, v3(0, 0, 150)), vadd(TU.pl->pos, v3(0, 0, 120)))) { if (!TU.reset_pending) wst_fail_t = 2.5f; tut_fail(NULL, "sk_tut_operator_melee_retry_001"); }
		break; }
	case TUS_HACK: case TUS_DESTROY: {   // one press next to it starts the hack (step done); then destroy it while its defences are down
		if (TU.tank < 0) break; int u = 0;
#ifdef __vita__
		{ SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); u = (c.buttons & SCE_CTRL_SQUARE) != 0; }
#endif
		if (*TU.autoplay && t > 1) u = TU.ap_use;
		tut_tank.use(TU.tank, u, dt);
		int open = tut_tank.open(TU.tank);
		if (S->kind == TUS_HACK) { if (tut_tank.hacking(TU.tank) >= 0 || open) done = 1; TU.tank_open_was = open; break; }
		if (tut_tank.destroyed(TU.tank)) { done = 1; break; }
		if (open) TU.destroy_t += dt;
		if (open && TU.destroy_t >= TUT_OPEN_T) { tut_tank.rearm(TU.tank); open = 0; }   // not destroyed in time (EA footage: ~13 s after the shutdown)
		if (TU.tank_open_was && !open) {   // the defences came back up before it was destroyed
			TU.tank_fails++; tut_message("You failed to destroy enemy Think Tank.", NULL); voice_play("ui_tutorial_alarm", 170, 0);
			tut_say(FC_BATOU, "Batou", "Think Tank disabling failed.", NULL); logp("tutorial: think tank not destroyed in time (%d): hack it again\n", TU.tank_fails);
			tut_step_begin(TU.step - 1); break; }   // EA footage: back to "Hacking Think Tanks"
		TU.tank_open_was = open;
		break; }
	default: break;
	}
	if (done && TU.phase == TP_STEP && !TU.reset_pending) tut_success();
	return TR_NONE;
}

// ------------------------------------------------------------------ world drawing (after range_draw): gates, waypoint ring, INTER GATE, weapon, allies, x-ray
static void tfx_quad(int id, const float *A, const float *B, const float *C, const float *D, const float *col) {
	const unsigned short *r = TFX_RECT[id]; sprite_quad(A, B, C, D, (r[0] + 0.5f) / TFX_W, (r[1] + 0.5f) / TFX_H, (r[0] + r[2] - 0.5f) / TFX_W, (r[1] + r[3] - 0.5f) / TFX_H, col);
}
static void tfx_bill(int id, V3 c, float rx, float ry, const float *right, const float *up, float rot, const float *col) {   // camera-facing (right/up) quad, rotated
	float cr = cosf(rot), sr = sinf(rot), a[3], b[3]; for (int k = 0; k < 3; k++) { a[k] = (cr * right[k] + sr * up[k]) * rx; b[k] = (-sr * right[k] + cr * up[k]) * ry; }
	float p0[3] = { c.x - a[0] + b[0], c.y - a[1] + b[1], c.z - a[2] + b[2] }, p1[3] = { c.x + a[0] + b[0], c.y + a[1] + b[1], c.z + a[2] + b[2] },
		p2[3] = { c.x + a[0] - b[0], c.y + a[1] - b[1], c.z + a[2] - b[2] }, p3[3] = { c.x - a[0] - b[0], c.y - a[1] - b[1], c.z - a[2] - b[2] };
	tfx_quad(id, p0, p1, p2, p3, col);
}
static void tfx_upright(int id, V3 c, float rx, float ry, float yaw, float rot, const float *col) {   // a vertical quad facing yaw, rotated in its plane
	float rgt[3] = { -sinf(yaw), cosf(yaw), 0 }, up[3] = { 0, 0, 1 }; tfx_bill(id, c, rx, ry, rgt, up, rot, col);
}
static void tut_draw_world(const float *VP, const float *cam, const float *right, const float *up) {
	if (!tut_on() || !TU.fx || !sp_ok) return;
	const TutProg *P = tut_p(); float tm = TU.total_t; V3 C = v3(cam[0], cam[1], cam[2]);
	// allies (opaque operators; translucent while sharing the camo). chars b35: their skinning is queued first and done in one
	// go on all cores (model.h / skin.h), then each is drawn
	for (int i = 0; i < TUT_NALLY; i++) if (TU.ally[i].ok) {
		float dx = TU.ally[i].x - cam[0], dy = TU.ally[i].y - cam[1]; if (dx * dx + dy * dy > 3500.f * 3500.f) continue;
		model_skin_queue(&chrt[TU.ally[i].ct].m, &TU.ally[i].sc, anim_pose(&TU.ally[i].an, chrt[TU.ally[i].ct].pelvis)); }
	skin_run();
	for (int i = 0; i < TUT_NALLY; i++) if (TU.ally[i].ok) {
		float dx = TU.ally[i].x - cam[0], dy = TU.ally[i].y - cam[1]; if (dx * dx + dy * dy > 3500.f * 3500.f) continue;
		Model *M = &chrt[TU.ally[i].ct].m; MBone *pose = anim_pose(&TU.ally[i].an, chrt[TU.ally[i].ct].pelvis);
		float th = TU.ally[i].yaw + 1.5707963f, c = cosf(th), s = sinf(th), Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, TU.ally[i].x, TU.ally[i].y, TU.ally[i].z, 1 }, MVP[16];
		rg_mul4(MVP, VP, Mw);
		static const float ld[4] = { 0.30f, -0.70f, 0.65f, 0 }, nofog[4] = { 0, 0, 0, 0 }; float tint[4] = { 0.95f, 0.95f, 0.95f, TU.shared ? 0.35f : 1 };
		glDisable(GL_CULL_FACE); if (TU.shared) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
		model_draw_cpu(M, &TU.ally[i].sc, MVP, pose, ld, tint, nofog, 1);
		glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
	}
	// heat sensor download: the dummy beyond the door, red, through the walls
	if (TU.heat_t > 0 && TU.walker >= 0 && RG.model_ok && (RG.t[TU.walker].state == RG_UP || RG.t[TU.walker].state == RG_SPAWN)) {
		RgTarget *T = &RG.t[TU.walker]; float th = T->yaw + 1.5707963f, c = cosf(th), s = sinf(th), Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, T->pos.x, T->pos.y, T->pos.z, 1 }, MVP[16];
		rg_mul4(MVP, VP, Mw); static const float ld[4] = { 0.25f, -0.55f, 0.80f, 0 }, nofog[4] = { 0, 0, 0, 0 };
		float a = fminf(1, TU.heat_t / 1.5f) * (0.75f + 0.25f * sinf(tm * 6)), tint[4] = { 6.0f, 0.5f, 0.35f, a };
		int k = T->pose; MBone *Pz = RG.pose[k] ? RG.pose[k] : RG.pose[0]; if (!RG.pose[k]) k = 0;
		glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
		model_draw_cpu(RG.M, &RG.sc[k], MVP, Pz, ld, tint, nofog, 0);   // reskin 0: range_draw skinned this pose this frame (a second reskin would cycle the triple-buffered skin VBOs too fast)
		glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
	}
	// gates: the door-shaped orange panel, a hexagon layer and rising scan lines; opening: they fade and lift
	for (int g = 0; g < P->ngate; g++) {
		if (TU.gate_state[g] == 2) continue; const TutGate *G = &P->gate[g];
		float dx = G->x - cam[0]; if (fabsf(dx) > 6000) continue;
		float o = TU.gate_state[g] == 1 ? TU.gate_t[g] / 0.9f : 0, a = 1 - o * o, lift = o * o * G->h * 0.3f;
		float y0 = G->y - G->hw, y1 = G->y + G->hw, z0 = G->z + lift, z1 = G->z + G->h, x = G->x;
		float A[3] = { x, y1, z1 }, B[3] = { x, y0, z1 }, Cc[3] = { x, y0, z0 }, D[3] = { x, y1, z0 };
		float col[4] = { 1, 1, 1, 0.82f * a }; tfx_quad(G->kind == 2 ? TFX_GATE_L : G->kind == 1 ? TFX_GATE_S : TFX_GATE_M, A, B, Cc, D, col);
	}
	sprite_flush(VP, TU.fx, 0);
	for (int g = 0; g < P->ngate; g++) {
		if (TU.gate_state[g] == 2) continue; const TutGate *G = &P->gate[g]; float dx = G->x - cam[0]; if (fabsf(dx) > 6000) continue;
		float o = TU.gate_state[g] == 1 ? TU.gate_t[g] / 0.9f : 0, a = (1 - o) * (0.55f + 0.15f * sinf(tm * 2 + g)), cell = 70;
		int nu = (int)(2 * G->hw / cell + 0.5f), nv = (int)(G->h / cell + 0.5f); if (nu < 1) nu = 1; if (nu > 12) nu = 12; if (nv > 7) nv = 7;
		float cu = 2 * G->hw / nu, cv = G->h / nv, x = G->x + (cam[0] > G->x ? 1.f : -1.f);
		for (int i = 0; i < nu; i++) for (int j = 0; j < nv; j++) {   // hexagons, brightest near the bottom and the middle
			float yy = G->y - G->hw + i * cu, zz = G->z + j * cv, w = (1 - 0.6f * (float)j / nv) * (1 - 0.5f * fabsf((i + 0.5f) / nu - 0.5f) * 2);
			float A[3] = { x, yy + cu, zz + cv }, B[3] = { x, yy, zz + cv }, Cc[3] = { x, yy, zz }, D[3] = { x, yy + cu, zz }, col[4] = { 1.0f, 0.42f, 0.08f, a * 0.55f * w };
			tfx_quad(TFX_HEX, A, B, Cc, D, col); }
		float sc = fmodf(tm * 0.45f, 1.f), zs = G->z + sc * G->h, A[3] = { x, G->y + G->hw, zs + 40 }, B[3] = { x, G->y - G->hw, zs + 40 }, Cc[3] = { x, G->y - G->hw, zs - 40 }, D[3] = { x, G->y + G->hw, zs - 40 };
		float col[4] = { 1, 0.75f, 0.4f, 0.5f * (1 - o) * (1 - sc) }; tfx_quad(TFX_SCAN, A, B, Cc, D, col);
		if (o > 0) { float f = TU.gate_t[g] / 0.9f, ze = G->z + f * G->h, A2[3] = { x, G->y + G->hw, ze + 12 }, B2[3] = { x, G->y - G->hw, ze + 12 }, C2[3] = { x, G->y - G->hw, ze - 12 }, D2[3] = { x, G->y + G->hw, ze - 12 }, w[4] = { 1, 0.9f, 0.7f, 1 - f };
			tfx_quad(TFX_GLOW, A2, B2, C2, D2, w); }
		sprite_flush(VP, TU.fx, 1);
	}
	// the waypoint ring (yellow-orange, two counter-rotating rings, a soft disc on the floor)
	const TutStep *S = TU.st;
	if (S && TU.phase == TP_STEP && S->mr > 0 && S->kind != TUS_LOAD) {
		V3 m = v3(S->mx, S->my, S->mz + 80); float pulse = 0.85f + 0.15f * sinf(tm * 5), c1[4] = { 1, 0.70f, 0.12f, 0.9f * pulse }, c2[4] = { 1, 0.82f, 0.30f, 0.7f };
		tfx_bill(TFX_RING, m, 58, 58, right, up, tm * 0.8f, c1); tfx_bill(TFX_INTER5, m, 48, 48, right, up, -tm * 1.3f, c2);
		float gx = S->mx, gy = S->my, gz = S->mz + 3, r = 70, A[3] = { gx - r, gy + r, gz }, B[3] = { gx + r, gy + r, gz }, Cc[3] = { gx + r, gy - r, gz }, D[3] = { gx - r, gy - r, gz }, gc[4] = { 1, 0.6f, 0.1f, 0.35f * pulse };
		tfx_quad(TFX_GLOW, A, B, Cc, D, gc);
	}
	// the INTER GATE terminal (combat: load the weapons program): stacked rotating rings, the label, the gauge arc
	if (TU.prog == TUT_COMBAT && S && (S->kind == TUS_LOAD || (TU.step == 0 && TU.phase == TP_SUCCESS))) {
		V3 c = v3(0, 0, 6 + 125); float yaw = atan2f(cam[1] - c.y, cam[0] - c.x), k = TU.hold, cy[4] = { 0.35f + 0.6f * k, 0.9f - 0.45f * k, 1 - 0.8f * k, 0.8f };
		static const int L[6] = { TFX_INTER1, TFX_INTER2, TFX_INTER3, TFX_INTER4, TFX_INTER5, TFX_INTER6 }; static const float SPD[6] = { 0.6f, -0.4f, 0.25f, -0.15f, 0.9f, -0.7f }, RAD[6] = { 70, 90, 115, 125, 105, 130 };
		for (int i = 0; i < 6; i++) { V3 p = vadd(c, v3(cosf(yaw) * (i * 4.f), sinf(yaw) * (i * 4.f), 0)); tfx_upright(L[i], p, RAD[i], RAD[i], yaw, tm * SPD[i], cy); }
		float w[4] = { 1, 1, 1, 0.9f }; tfx_upright(TFX_INTERFONT, vadd(c, v3(cosf(yaw) * 26, sinf(yaw) * 26, 0)), 55, 55, yaw, 0, w);
		if (k > 0) { float o[4] = { 1, 0.45f, 0.1f, 0.9f }; tfx_upright(TFX_INTERARC, vadd(c, v3(cosf(yaw) * 28, sinf(yaw) * 28, 0)), 80 * (0.6f + 0.4f * k), 80 * (0.6f + 0.4f * k), yaw, k * 3.14159f, o); }
	}
	// the weapon at the centre of the platform (combat: weapon pick-up): a pin and a glowing disc
	if (TU.prog == TUT_COMBAT && S && S->kind == TUS_PICKUP) {
		float pc[4] = { 0.4f, 0.95f, 1, 0.9f }, gc[4] = { 0.3f, 0.8f, 1, 0.45f };
		tfx_bill(TFX_PIN, v3(0, 0, 6 + 120 + 10 * sinf(tm * 2)), 30, 30, right, up, 0, pc);
		float r = 110, A[3] = { -r, r, 9 }, B[3] = { r, r, 9 }, Cc[3] = { r, -r, 9 }, D[3] = { -r, -r, 9 }; tfx_quad(TFX_DISC, A, B, Cc, D, gc);
	}
	// SkillSync link lines (white) from the player to the allies / from the ally to the player
	if (TU.prog == TUT_CYBER && S && (S->kind == TUS_SHARE || S->kind == TUS_DOWNLOAD || (TU.shared && TU.step_t < 3)) && TU.pl) {
		for (int i = 0; i < TUT_NALLY; i++) if (TU.ally[i].ok && (S->kind != TUS_DOWNLOAD || i == 0)) {
			V3 a = vadd(TU.pl->pos, v3(0, 0, 60)), b = v3(TU.ally[i].x, TU.ally[i].y, TU.ally[i].z + 60), d = vsub(b, a); float l = sqrtf(vdot(d, d)); if (l < 1) continue;
			V3 n = vmul(vcross(d, vsub(C, a)), 1); float nl = sqrtf(vdot(n, n)) + 1e-3f; n = vmul(n, 2.5f / nl);
			float A[3] = { a.x + n.x, a.y + n.y, a.z + n.z }, B[3] = { b.x + n.x, b.y + n.y, b.z + n.z }, Cc[3] = { b.x - n.x, b.y - n.y, b.z - n.z }, D[3] = { a.x - n.x, a.y - n.y, a.z - n.z };
			float w[4] = { 0.9f, 0.97f, 1, (S->kind == TUS_SHARE && !sk_camo_on(0)) ? 0.25f : 0.8f }; tfx_quad(TFX_GLOW, A, B, Cc, D, w);
		}
	}
	// the stub Think Tank: a red ring on the floor around it
	if (TU.tank >= 0 && !tut_tank.own_hud && !tut_tank.destroyed(TU.tank)) {
		float tp[3]; tut_tank.pos(TU.tank, tp); float r = 150, z = tp[2] - 97, A[3] = { tp[0] - r, tp[1] + r, z }, B[3] = { tp[0] + r, tp[1] + r, z }, Cc[3] = { tp[0] + r, tp[1] - r, z }, D[3] = { tp[0] - r, tp[1] - r, z };
		float rc[4] = { 1, 0.15f, 0.12f, 0.8f }; tfx_quad(TFX_RING, A, B, Cc, D, rc);
	}
	sprite_flush(VP, TU.fx, 1);
	glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
}

// ------------------------------------------------------------------ HUD (in main's hud_begin/hud_end block)
// the game HUD minus what the tutorial does not show (score / timer, HP / AP, minimap; the skill gauge only in the
// cyberbrain programs): parts are switched off by clearing their indices in a copy of hud_game.h's HG for one call
static void tut_hud_game(HudState *s, float now) {
	if (!tut_on()) { hud_game_draw(s, now); return; }
	if (TU.ex_open) return;   // b61 (user: the HUD's 30 / TIER rings showed through the CYBERBRAIN SKILL page, over its own rings): none under a page
	typeof(HG) keep = HG; int skill = TU.prog == TUT_CYBER || TU.prog == TUT_TANK;
	HG.sc_d1 = HG.sc_d11 = HG.sc_d301 = HG.tm_colon = HG.tmW_colon = -1;
	for (int i = 0; i < 5; i++) HG.scL[i] = HG.scR[i] = HG.scG[i] = -1; for (int i = 0; i < 2; i++) HG.tmM[i] = HG.tmS[i] = HG.tmWM[i] = HG.tmWS[i] = -1;
	HG.hp_arc31 = HG.hp_arc32 = HG.hp_disc = HG.hp_back = HG.hp_d1 = HG.hp_ticks = HG.hp_label = HG.hp_labelW = HG.ap_label = -1;
	for (int i = 0; i < 3; i++) HG.hp_n[i] = HG.hp_z[i] = HG.hpW_n[i] = HG.hpW_z[i] = HG.ap_n[i] = HG.ap_z[i] = -1;
	s->hp = 0; s->ap = 0; HG.mm_view = -1; s->nfeed = 0; s->nchip = 0; s->nblip = 0; s->notify = NULL;
	// b61 (user: the bottom-left circle went missing, its TIER labels stayed; footage: no skill gauge in the movement and
	// weapons programs, and no portrait / HP ring at the bottom right in any program): the whole gauge off, the portrait
	// and its HP decoration off (with hp 0 the HP ring took the low-health red pulse)
	HG.hp_chr = HG.hp_deco = HG.hp_decoW = -1; hg_skill_hidden = !skill;
	if (!skill) { HG.in_fx = HG.in_bg = HG.in_a45 = HG.in_a46 = HG.in_t47 = HG.in_deco = HG.in_max = HG.in_btn = -1; HG.in_skill = HG.in_t1 = HG.in_tier = HG.in_badge = HG.in_badgetxt = HG.in_line = HG.in_btntxt = -1;
		for (int i = 0; i < 4; i++) HG.in_m[i] = HG.in_v[i] = -1; for (int i = 0; i < 3; i++) HG.in_lo[i] = -1; s->tier_hi = 0; s->cool = -1; }
	hud_game_draw(s, now);
	HG = keep; hg_skill_hidden = 0;
}
static int tut_project(const float *VP, V3 p, float *sx, float *sy) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = VP[r] * p.x + VP[4 + r] * p.y + VP[8 + r] * p.z + VP[12 + r];
	if (q[3] < 5) { *sx = 480 - q[0] * 1000; *sy = 272; return 0; } *sx = (q[0] / q[3] + 1) * 480; *sy = (1 - q[1] / q[3]) * 272; return 1;
}
static void tut_hud(const float *VP, float now) {
	if (!tut_on() || !TU.ok) return;
	(void)now; hud_flush();
	if (!tui_begin()) { hud_begin(); return; }
	const TutStep *S = TU.st; const TutProg *P = tut_p();
	int show = TU.phase == TP_STEP || TU.phase == TP_SUCCESS || TU.phase == TP_END;
	if (show && !TU.ex_open) {
		// objective band (the last titled step's), or End Program; under it the step's button prompts
		float by = 0;
		if (TU.phase == TP_END) { tui_endprog(NULL, NULL, TU.phase_t); by = TUI.band_y1; }
		else { const TutStep *O = NULL; for (int s = TU.step; s >= 0; s--) if (P->st[s].title) { O = &P->st[s]; break; }
			if (O) { const char *txt = O->text2 && TU.step_t > 7 && O == S ? O->text2 : O->text; by = tui_objective(O->title, txt, O == S ? TU.obj_t : 2, 1); } }
		if (S && TU.phase == TP_STEP && S->keys) tui_keys(S->keys, by + 8, 1);   // ui b42: 8 px under the band (was 6: the plates' outline came within 4 px)
		float y2 = fmaxf(by + 48, 96);   // top of the side widgets (checklist right, portrait left)
		// checklist
		if (S && S->page >= 0 && S->page < P->npage) {
			TuiCheck it[6]; int n = 0; const TutCheckDef *D = P->page[S->page];
			for (int i = 0; i < 6 && D[i].label; i++) { it[i].label = D[i].label; it[i].goal = D[i].goal; it[i].count = 0; n++; }
			int cur = S->item >= 0 ? S->item : 0; if (TU.phase != TP_STEP && S->item >= 0) cur = S->item + 1; if (S->kind == TUS_END) cur = n;
			if (S->kind == TUS_EXPLAIN) { cur = 0; for (int s = TU.step + 1; s < TU.nsteps; s++) if (P->st[s].item >= 0) { cur = P->st[s].item; break; } }
			for (int i = 0; i < n; i++) if (it[i].goal) it[i].count = i < cur ? it[i].goal : i == cur && S->kind == TUS_KILLS ? TU.count : 0;
			tui_checklist(it, n, cur, y2, TU.step_t + 1);
		}
		// operator portrait + caption (the radio widget): the step's speaker, then the second one
		if (TU.say_t < 6) tui_portrait(TU.say_face, TU.say_who, TU.say_cap, y2, TU.say_t);
		else if (S) {
			if (TU.port2_t >= 0 && TU.port2_t < 6 && S->face2 >= 0 && S->who2) tui_portrait(S->face2, S->who2, S->caption, y2, fminf(TU.port2_t, 5.0f));
			else if (S->face >= 0 && S->who && TU.port_t < 6) tui_portrait(S->face, S->who, S->caption, y2, TU.phase == TP_END ? fminf(TU.port_t, 2.0f) : TU.port_t);
		}
		// waypoint flag
		if (S && TU.phase == TP_STEP && (S->mr > 0 || (S->kind == TUS_HACK && !tut_tank.own_hud) || S->kind == TUS_DOWNLOAD)) {   // (the real tank carries its own tag)
			V3 m = v3(S->mx, S->my, S->mz + 175);
			if (S->kind == TUS_HACK && TU.tank >= 0) { float tp[3]; tut_tank.pos(TU.tank, tp); m = v3(tp[0], tp[1], tp[2] + 120); }
			if (S->kind == TUS_DOWNLOAD && TU.ally[0].ok) m = v3(TU.ally[0].x, TU.ally[0].y, TU.ally[0].z + 210);
			float sx, sy, top = by + 70; int on = tut_project(VP, m, &sx, &sy), edge = !on || sx < 30 || sx > 930 || sy < top || sy > 480;
			if (sx < 30) sx = 30; if (sx > 930) sx = 930; if (sy < top) sy = top; if (sy > 480) sy = 480;
			V3 d = vsub(m, TU.pl->pos); tui_marker(sx, sy, sqrtf(vdot(d, d)) / 100, edge, 1);
		}
		// prompts (centre, lower half)
		if (S && TU.phase == TP_STEP) {
			if (S->kind == TUS_LOAD) { int near = tut_dist2(S->mx, S->my, S->mz) < S->mr * S->mr * 1.6f; if (near || TU.hold > 0) tui_prompt(TU.hold > 0 ? "Loading..." : "Load Program", TU.hold, "HOLD {s}", 360, 1); }
			if (S->kind == TUS_PICKUP && TU.pick_def < 0 && tut_dist2(S->mx, S->my, S->mz) < S->mr * S->mr) tui_prompt("L118A-S1b", TU.hold > 0 ? TU.hold : -1, "HOLD {s}  SWAP", 360, 1);
			if (S->kind == TUS_DOWNLOAD && tut_dist2(TU.ally[0].x, TU.ally[0].y, TU.ally[0].z) < 420.f * 420.f) tui_prompt("SkillSync: Heat Sensor", TU.hold > 0 ? TU.hold : -1, "HOLD {s}", 360, 1);
			if ((S->kind == TUS_HACK || S->kind == TUS_DESTROY) && TU.tank >= 0 && !tut_tank.own_hud) {   // the stand-in: the real unit's prompt / code panel (thinktank.h tk_hud)
				float hp = tut_tank.hacking(TU.tank);
				if (hp >= 0) tui_hack(hp, TU.hack_seed, 1); else if (tut_tank.reach(TU.tank)) tui_prompt("Hacking Think Tank", -1, "PRESS {s}", 360, 1); }
			if ((S->flags & TF_LADDER) && !TU.laddering) {   // the original's "W + SPACE" spots: here {x} over the obstacle in front
				if (--TU.ledge_cd <= 0) { TU.ledge_cd = 6; TU.ledge = player_ledge_ahead(TU.pl); V3 p = TU.pl->pos; if (p.x < -1690 && p.x > -1910 && fabsf(p.y) < 80 && p.z < 60) TU.ledge = 1; }
				if (TU.ledge) tui_prompt(TU.ledge == 2 ? "Vault" : "Climb", -1, "{x}", 340, 0.9f);
			}
		}
		// the aimed dummy's plate (as range.h): red flag, ENEMY / TARGET, health bar
		if (RG.plate >= 0 && RG.plate_t > 0 && RG.plate < RG.n) { RgTarget *T = &RG.t[RG.plate];
			if (T->state != RG_OFF) { float sx, sy; if (tut_project(VP, vadd(T->head, v3(0, 0, 26)), &sx, &sy)) {
				float fade = RG.plate_t > 0.2f ? 1 : RG.plate_t / 0.2f, red[4] = { 0.95f, 0.22f, 0.27f, fade }, dark[4] = { 0.05f, 0, 0, 0.55f * fade };
				tui_rect(sx - 12, sy - 26, sx - 2, sy - 18, red); tui_rect(sx - 12, sy - 26, sx - 10, sy - 6, red);
				int tank = !tut_tank.own_hud && TU.tank == RG.plate;
				tui_text(sx + 2, sy - 10, TUI_FS, 0, red, TU.prog == TUT_COMBAT ? "TARGET" : tank ? "THINK TANK" : "ENEMY");
				float fr = tank ? tut_tank.health(TU.tank) : T->show_hp / RG.hp; fr = fr < 0 ? 0 : fr > 1 ? 1 : fr;
				tui_rect(sx + 2, sy - 5, sx + 102, sy - 1, dark); if (fr > 0) tui_rect(sx + 2, sy - 5, sx + 2 + 100 * fr, sy - 1, red); } } }
		if (TU.phase == TP_SUCCESS || TU.succ_t < 2.2f) tui_success(TU.succ_t);
		if (TU.msg) tui_message(TU.msg, TU.msg_t, TU.msg_col);
	}
	tui_end();
	hud_begin();   // back to main's HUD state (the ovl pack calls ended it)
}
// full-screen layers (in main's overlay section): connect sequence, explanation screens
static void tut_overlay(float now) {
	if (!tut_on() || !TU.ok) return; (void)now;
	if (TU.phase == TP_CONNECT) { if (tui_begin()) { tui_connect(TU.phase_t, 5.0f); tui_end(); } }
	if (TU.ex_open && TU.st) { if (tui_begin()) { tui_explain(&TUT_EX[TU.st->explain], TU.vtex, TU.ex_t, TU.ex_t > 1.0f); tui_end(); } }
}
// loading screen hook (main.c loading_progress): returns 1 when it drew the tutorial's loading page
static int tut_loading_draw(int prog, float progress) {
	if (prog <= 0 || prog >= TUT_NPROG || !TUI.ok) return 0;
	tui_loading(&TUT_PROG[prog].ld, progress, 0); return 1;
}

// ------------------------------------------------------------------ program start / end (front end)
// called when the front end starts a match with cfg.tut > 0 (main's start block): map, team, limits; the intro cut
// scene before the first program of a full run
static void tut_play_intro(void);
#define TUT_FULL_RUN 5   // cfg.tut = 5: the whole tutorial in a row (control, combat, cyberbrain + think tank), as the original's first run
static void tut_match_setup(MenuConfig *c, int full_run) {
	if (c->tut == TUT_FULL_RUN) { c->tut = TUT_CONTROL; full_run = 1; }
	if (c->tut <= 0 || c->tut >= TUT_NPROG) { c->tut = 0; return; }
	c->map = tut_map_of(c->tut); c->mode = MODE_TDM; c->team_size = 1; c->time_limit = 0;
	TU.chain = full_run;
	if (c->tut == TUT_CONTROL && !TU.intro_done) { TU.intro_done = 1; tut_play_intro(); }
}
// a finished program: the next one of a full run (returns its map) or back to the lobby
static int tut_next_program(MenuConfig *c) { if (!TU.chain || c->tut >= TUT_CYBER) return -1; c->tut++; return tut_map_of(c->tut); }
static void tut_end(void) { if (TU.prog) logp("tutorial: program %d over (%.0f s)\n", TU.prog, TU.total_t); TU.prog = 0; TU.st = NULL; tv_close(); TU.ex_open = 0; tut_allies_free(); if (TU.tank >= 0) tut_tank.remove(TU.tank); TU.tank = -1; }
static void tut_exit_to_lobby(void) {
	tut_end(); cfg.tut = 0; amb_map_start(""); voice_stop_all(200);
	menu_fe_load(); menu_fe_open(FE_LOBBY); phase = PH_FRONT; grade_identity(); menu_music("bgm/lobby", 800);
}

// ------------------------------------------------------------------ movies: start-up logos, intro cut scene
// plays a movie full screen until it ends or the player skips it (Cross / Circle / Start / a tap); max_s: give up after
typedef struct { float t; const char *ev; } TutCue;   // a sound event at t seconds into a movie
// b55 (user: "movies fill the Vita screen if full screen"): a full-screen movie covers all of 960x544, never letterboxed.
// The picture's true shape: the skill clips are squashed to 640x272 for the explanation panel (make_tut_clips.py), from
// 16:9 sources (tutorial 1, 2, 4, 5) or 1064x456 ones (3, 6); the logos and the intro are 960x544 as encoded. Full
// screen, the frame is scaled to fill and the overhang of its true shape cropped evenly (a 2.33 clip loses a 12 % strip
// at each side, a 16:9 one 0.4 %).
static float tv_true_aspect(const char *file) {
	const char *b = strrchr(file, '/'); b = b ? b + 1 : file;
	if (!strncmp(b, "tut_v", 5)) return 16.f / 9.f;   // b59: all six are encoded 16:9 (512x288)
	return TV.w > 0 && TV.h > 0 ? (float)TV.w / TV.h : 960.f / 544.f;
}
static void tv_draw_full(GLuint tx, const char *file) {
	float A = tv_true_aspect(file), S = 960.f / 544.f, u0 = 0, u1 = 1, v0 = 0, v1 = 1;
	if (A > S) { float k = S / A; u0 = (1 - k) / 2; u1 = 1 - u0; } else if (A < S) { float k = A / S; v0 = (1 - k) / 2; v1 = 1 - v0; }
	float xy[4][2] = { { 0, 0 }, { 960, 0 }, { 960, 544 }, { 0, 544 } }, uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
	hud_poly(tx, TUI.nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, HUD_W1);
}
static int tut_movie(const char *file, float max_s, int skip_after_s, const TutCue *cue, int ncue) {
	char p[200]; snprintf(p, sizeof(p), VDIR "%s", file);
	int r = tv_open(p, 0, 1); if (r < 0) { logp("movie %s: not played (%d)\n", file, r); return r; }
	logp("movie %s: playing\n", file);
	SceUInt64 t0 = sceKernelGetProcessTimeWide(), tf0 = 0; unsigned oldb = ~0u; int skipped = 0; float tl = -1;
#ifdef __vita__
	sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
#endif
	for (;;) {
		SceUInt64 now = sceKernelGetProcessTimeWide(); float t = (now - t0) / 1e6f; if (t > max_s) break;
		SceCtrlData c; sceCtrlPeekBufferPositive(0, &c, 1); unsigned pr = c.buttons & ~oldb; oldb = c.buttons; int tap = 0;
#ifdef __vita__
		{ SceTouchData td; if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) > 0 && td.reportNum > 0) tap = 1; }
#endif
		if (t > skip_after_s && ((pr & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_START)) || tap)) { skipped = 1; break; }
		if (*TU.autoplay && t > 4) { skipped = 1; break; }   // test run: a few seconds of each movie
		GLuint tx = tv_frame(); if (tv_done()) break;
		if (tx && !tf0) tf0 = now;   // cues run on the movie's clock (from its first frame)
		if (tf0) { float tv = (now - tf0) / 1e6f; for (int i = 0; i < ncue; i++) if (tv >= cue[i].t && tl < cue[i].t) ui_sfx(cue[i].ev); tl = tv; }
		glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		if (tui_begin()) {
			if (tx) tv_draw_full(tx, file);
			if (t > skip_after_s && t < skip_after_s + 4) { float a = fminf(1, (t - skip_after_s) / 0.5f) * fminf(1, (skip_after_s + 4 - t) / 0.5f), w[4] = { 0.85f, 0.88f, 0.9f, 0.8f * a };
				tui_text(940, 516, TUI_FB, 1, w, "{x} SKIP"); }
			tui_end();
		}
		glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
		vglSwapBuffers(GL_FALSE);
	}
	logp("movie %s: %d frames%s\n", file, TV.frames, skipped ? ", skipped" : "");
	tv_close();
	return skipped;
}
// start-up: the publishers' / developers' logos and the title (footage 0:00-0:18)
// tut_logos.mp4 is rendered from the game's Intro.swf (tools/tutorial/make_tut_logos.py); its script's UI sounds as cues
// test runs only (autoplay): right after the logos, every other movie for TUT_MOVIE_TEST_S, opened the way the game opens
// it (the intro full screen with audio, the clips looping and silent as in the explanation panel), so the device log shows
// each one's frame memory and frame count at boot (b40: the log ring dropped everything after map 8). ~15 s.
// b42: the clips v2..v5 each try another frame memory mode (tut_vmem.h), so one device run tells which one the player
// accepts: v1 / v6 / the intro use the game's own mode (TVM_OWN, or moviemem.txt's).
#define TUT_MOVIE_TEST_S 2.0f
static void tut_movie_test(void) {
	static const char *const F[7] = { "tut_intro.mp4", "tut_v1.mp4", "tut_v2.mp4", "tut_v3.mp4", "tut_v4.mp4", "tut_v5.mp4", "tut_v6.mp4" };
	const int M[7] = { 0, 0, TVM_OWN | TVM_NOMAP, TVM_OWN | TVM_CACHED, TVM_ARENA, TVM_ARENA | TVM_OWN, 0 };   // 0: the game's mode
	int ok = 0, okm = 0;
	for (int i = 0; i < 7; i++) {
		char p[200]; snprintf(p, sizeof(p), VDIR "%s", F[i]); int clip = i > 0, mode = M[i] ? M[i] : tv_mode_default();
		tv_next_mode = mode;
		if (tv_open(p, clip, !clip) < 0) { logp("movie test %s [mode %d]: not played (%d)\n", F[i], mode, TV.err); continue; }
		SceUInt64 t0 = sceKernelGetProcessTimeWide(); GLuint tx = 0;
		while ((sceKernelGetProcessTimeWide() - t0) / 1e6f < TUT_MOVIE_TEST_S && !tv_done()) {
			GLuint t = tv_frame(); if (t) tx = t;
			glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			if (tui_begin()) {
				if (tx) tv_draw_full(tx, F[i]);
				float w[4] = { 0.85f, 0.88f, 0.9f, 0.9f }; tui_text(20, 30, TUI_FB, 0, w, F[i]);
				tui_end();
			}
			glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
			vglSwapBuffers(GL_FALSE);
		}
		ok += TV.frames > 0; if (TV.frames > 0) okm |= 1 << i;
		logp("movie test %s [mode %d: %s]: %d frames (%dx%d) in %.1f s\n", F[i], mode, tvm_mode_name(mode), TV.frames, TV.w, TV.h, TUT_MOVIE_TEST_S); tv_close();
	}
	logp("movie test: %d of 7 movies showed frames (game's mode %d: %s; other modes: %d %s, %d %s, %d %s, %d %s)\n", ok, tv_mode_default(), (okm & 0x43) == 0x43 ? "ok" : (okm & 0x43) ? "PARTLY" : "NO FRAMES",
		M[2], (okm & 4) ? "ok" : "no", M[3], (okm & 8) ? "ok" : "no", M[4], (okm & 16) ? "ok" : "no", M[5], (okm & 32) ? "ok" : "no");
}
static void tut_play_logos(void) {
	static const TutCue C[3] = { { 1.63f, "ui/logo_company" }, { 3.77f, "ui/logo_team" }, { 5.83f, "ui/logo_dive" } };
	menu_music("bgm/logo", 0);   // b59: the logos' music (the original's Intro.swf plays bgm_logo), on into the title screen
	TV.vol = cfg.vol_master; tut_movie("tut_logos.mp4", 30, 1, C, 3);
	if (TU.autoplay && *TU.autoplay) tut_movie_test();
}
// the test tour's time budget (see tut_tour_busy)
#define TUT_TOUR_BUDGET 280.f
#define TUT_TOUR_MIN 140.f
#define TUT_TOUR_END 975.f
#define TUT_TOUR_INTRO 20.f
// before the first program: "INTRO MOVIE" loading page, then "This is an age of information..." and the cut scene (0:22-1:03)
static void tut_play_intro(void) {
	FILE *f = fopen(VDIR "tut_intro.mp4", "rb"); if (!f) return; fclose(f);
	static const TuiLoading L = { "", "", NULL, NULL, "title" };   // (b109: the picture and the bar only)
	for (int i = 0; i < 40; i++) { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); tui_loading(&L, i / 39.f, 0);
		glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2); vglSwapBuffers(GL_FALSE); }
	music_stop(300); TV.vol = cfg.vol_master; tut_movie("tut_intro.mp4", TU.autoplay && *TU.autoplay ? TUT_TOUR_INTRO : 90, 1, NULL, 0);   // (the test tour: its first 20 s)
}

// ------------------------------------------------------------------ autoplay (device test tour): each program a few seconds per step
static void tut_ap_walk(float dt, float tx, float ty, float *mx, float *my, int *sprint) {
	Player *P = TU.pl; float dx = tx - P->pos.x, dy = ty - P->pos.y, d = sqrtf(dx * dx + dy * dy); if (d < 30) { *mx = *my = 0; return; }
	float want = atan2f(dy, dx), e = wrap_pi(want - P->yaw); P->yaw += e * fminf(1, 6 * dt); *TU.pitch *= 0.9f;
	*mx = 0; *my = fabsf(e) < 0.8f ? 1 : 0.3f; *sprint = d > 600 && fabsf(e) < 0.3f;
}
static void tut_autoplay(float dt, float *mx, float *my, int *sprint, int *jump, int *trig, int *aim, int *in_swap, int *in_knife, int *in_gren, int *gren_held, int *use_held, unsigned *pr) {
	TU.ap_t += dt; const TutStep *S = TU.st;
	if (TU.phase == TP_CONNECT && TU.phase_t > 2) *pr |= SCE_CTRL_CROSS;
	if (TU.ex_open) { if (TU.ex_t > 2.5f) *pr |= SCE_CTRL_CROSS; return; }
#if TUT_TANK_REAL
	if (TU.prog == TUT_TANK && TU.tank >= 0 && TU.tank < TK_MAX && tk[TU.tank].state == TK_DEAD) {   // b86: keep looking at the wreck (its explosion, for the test's shots)
		float tp[3]; tut_tank.pos(TU.tank, tp); float dx = tp[0] - TU.pl->pos.x, dy = tp[1] - TU.pl->pos.y, d = sqrtf(dx * dx + dy * dy) + 1;
		TU.pl->yaw = atan2f(dy, dx); *TU.pitch = atan2f(tp[2] + 40 - (TU.pl->pos.z + TU.pl->eye), d); }
#endif
	if (!S || TU.phase != TP_STEP || TU.reset_pending) return;
	float t = TU.step_t; int force = t > 7.0f;   // the tour shows each step a few seconds, then completes it
	switch (S->kind) {
	case TUS_REACH:
		if (S->flags & TF_CROUCH) TU.pl->in_crouch = 1;
		if (force || t > 2.5f) { tut_teleport(v3(S->mx, S->my, S->mz + 5), TU.pl->yaw); TU.pl->crouched = (S->flags & TF_CROUCH) ? 1 : 0; logp("  tutorial autoplay: step %d moved to the marker\n", TU.step + 1); TU.step_t = 0; }
		else { tut_ap_walk(dt, S->mx, S->my, mx, my, sprint); if (S->flags & TF_WALK) { *sprint = 0; *my *= 0.5f; } }
		break;
	case TUS_SPRINT: TU.pl->yaw = 3.14159f; *my = 1; *sprint = 1; break;
	case TUS_LOAD: case TUS_PICKUP: if (tut_dist2(S->mx, S->my, S->mz) > 140.f * 140.f) tut_ap_walk(dt, S->mx + 120, S->my, mx, my, sprint); *use_held = 1; break;
	case TUS_SWITCH_PRI: case TUS_SWITCH_SEC: if (((int)(t * 2)) != ((int)((t - dt) * 2))) *in_swap = 1; break;
	case TUS_ADS: *aim = 1; break;
	case TUS_GRENADE: if (t > 1) *in_gren = 1; break;
	case TUS_KILLS:   // main's autoplay aims at and shoots the visible targets; the grenade step throws; stragglers are removed after a while
		if (S->groups & 0x200) { TU.pl->yaw += wrap_pi(0 - TU.pl->yaw) * fminf(1, 4 * dt); *TU.pitch = 0.05f; if (((int)(t)) != ((int)(t - dt)) && WP.gren_state == 0) { *in_gren = 1; } *gren_held = (fmodf(t, 1.f) < 0.5f); }
		if (t > 3.5f && ((int)(t * 3.3f)) != ((int)((t - dt) * 3.3f))) {   // stragglers: one every 0.3 s
			int hit = 0; for (int g = 0; g < TUT_MAXGRP && !hit; g++) if (S->groups >> g & 1) for (int k = 0; k < TU.ngrp[g]; k++) { RgTarget *T = &RG.t[TU.grp[g][k]]; if (T->state == RG_UP) { range_damage(TU.grp[g][k], 1000, 0, T->chest, v3(1, 0, 0)); hit = 1; break; } } }
		(void)trig; break;
	case TUS_SKILL: case TUS_SHARE: if (t > 1 && !sk_camo_on(0)) { if (S->kind == TUS_SHARE) tut_teleport(v3(-9500, 0, -245), 3.14159f); sk_press(0); if (S->kind == TUS_SHARE) sk_upgrade(0); } break;   // share: Tier 2
	case TUS_DOWNLOAD: if (t < 0.2f) tut_teleport(v3(TU.ally[0].x + 250, TU.ally[0].y, TU.ally[0].z + 5), 3.14159f); *use_held = 1; break;
	case TUS_KNIFE: if (t > 1 && ((int)(t)) != ((int)(t - dt))) *in_knife = 1; break;
	case TUS_STEALTH:
		if (t > 2 && TU.walker >= 0) { RgTarget *T = &RG.t[TU.walker]; tut_teleport(v3(T->pos.x - cosf(T->yaw) * 120, T->pos.y - sinf(T->yaw) * 120, T->pos.z + 5), T->yaw); TU.stealth_ok = 1; *in_knife = 1; }
		break;
	case TUS_HACK: case TUS_DESTROY:   // face it, press interact (tapped, so the stand-in sees presses), shoot it while its defences are down
		if (TU.tank >= 0) { float tp[3]; tut_tank.pos(TU.tank, tp);
#if TUT_TANK_REAL
			// b86 (v2.0 EP14 7:03-7:20): the hack from the arena's door (~10 m, through the glass), then in under the glass, at it
			float want = S->kind == TUS_HACK ? 1000 : 550;
			if (t < 0.2f || tut_dist2(tp[0], tp[1], tp[2] - 100) > (want + 400) * (want + 400)) { tut_teleport(v3(tp[0] + want, tp[1], tp[2] - 95), 3.14159f); }
			{ float dx = tp[0] - TU.pl->pos.x, dy = tp[1] - TU.pl->pos.y, d = sqrtf(dx * dx + dy * dy) + 1; TU.pl->yaw = atan2f(dy, dx);   // (set, not eased: main's autoplay turns to the range's dummies)
			  *TU.pitch = atan2f(tp[2] - 10 - (TU.pl->pos.z + TU.pl->eye), d); }
			TU.ap_use = !TU.ap_use && tut_tank.hacking(TU.tank) < 0 && !tut_tank.open(TU.tank) && t > 1.0f; *use_held = TU.ap_use;
			if (tut_tank.open(TU.tank) && ((int)(t * 4)) != ((int)((t - dt) * 4))) tut_tank.hurt(TU.tank, 130);
#else
			if ((S->kind == TUS_HACK && t < 0.2f) || tut_dist2(tp[0], tp[1], tp[2] - 100) > 300.f * 300.f) { tut_teleport(v3(tp[0] + 230, tp[1], tp[2] - 95), 3.14159f); *TU.pitch = -0.1f; }
			TU.ap_use = !TU.ap_use && tut_tank.hacking(TU.tank) < 0 && !tut_tank.open(TU.tank); *use_held = TU.ap_use;
			if (tut_tank.open(TU.tank) && ((int)(t * 4)) != ((int)((t - dt) * 4))) tut_tank.hurt(TU.tank, 260);
#endif
		}
		break;
	default: break;
	}
	(void)jump; (void)force;
}
// the test tour: after the maps, each program until its End Program screen (at most 80 s)
// tut_tour_busy: 1 while a program of the tour should keep running (main skips its end-of-map step meanwhile).
// Time budget: main's maps tour runs until TOUR_MAX (760 s since start, main.c; b31: the maps ended at ~765 s). The
// programs then share what is left until TUT_TOUR_END (s since start; the end-of-match screens follow, ~15 s, so the whole
// test stays under ~17 min): between TUT_TOUR_MIN and TUT_TOUR_BUDGET s, set when the first program starts (after the
// intro, which the tour cuts to TUT_TOUR_INTRO s). Each program runs to its End Program screen (about 30 / 60 / 50 / 40 s
// under autoplay) unless it reaches its share of what is left (never less than 25 s): time a program does not use goes
// to the next ones. b31: programs 1 and 3 ran 29 / 51 s, 2 and 4 0 s (fixed in tut_begin).
static int tut_tour_busy(float map_t) {
	if (!(TU.tour_on && tut_on() && !TU.ap_done)) return 0;
	int left = TUT_NPROG - TU.tour_k;   // programs still to run, this one included
	float share = (TU.tour_budget - TU.tour_spent) / (left > 0 ? left : 1), cap = fminf(90.f, fmaxf(25.f, share));
	if (map_t >= cap) { logp("tutorial: tour program %d cut at %.0f s (its share of the budget)\n", TU.prog, map_t); return 0; }
	return 1;
}
static int tut_tour_next(MenuConfig *c, int *want_map) {
	if (TU.tour_k >= TUT_NPROG - 1) { if (TU.tour_on) { logp("tutorial: tour done, %.0f of %.0f s used\n", TU.tour_spent + TU.total_t, TU.tour_budget); tut_end(); TU.tour_on = 0; c->tut = 0; } return 0; }
	if (TU.tour_on) TU.tour_spent += TU.total_t;
	TU.tour_k++; c->tut = TU.tour_k; TU.chain = 0; TU.tour_on = 1; TU.ap_done = 0;
	if (TU.tour_k == TUT_CONTROL && !TU.intro_done) { TU.intro_done = 1; tut_play_intro(); }
	if (TU.tour_k == 1) {
		float now = sceKernelGetProcessTimeWide() / 1e6f; TU.tour_spent = 0;
		TU.tour_budget = fminf(TUT_TOUR_BUDGET, fmaxf(TUT_TOUR_MIN, TUT_TOUR_END - now));
		logp("tutorial: tour of the %d programs starts at %.0f s, budget %.0f s\n", TUT_NPROG - 1, now, TU.tour_budget);
	}
	*want_map = tut_map_of(c->tut); logp("autoplay: tutorial program %d\n", c->tut); return 1;
}
// ------------------------------------------------------------------ main.c hooks (all guarded by cfg.tut > 0 / tut_on()):
//  H1  #include "tutorial.h" before int main()
//  H2  boot: vglInitWithCustomThreshold(..., TV_PHYCONT_FREE ...) instead of vglInitExtended (phycont for the video decoder)
//  H3  after menu_init: tut_init(VDIR); tut_bind(...); then tut_play_logos() before the front end opens
//  H4  front end FE_START: keep cfg.tut through cfg_check, tut_match_setup(&cfg, full_run)
//  H5  loading_progress(): tut_loading_draw(cfg.tut, p) instead of menu_loading_draw
//  H6  end of the want_map block: if (cfg.tut > 0) tut_begin(cfg.tut)
//  H7  input: tut_input(...) after the death / Demolition input locks
//  H8  after player_update: tut_after_move(dt)
//  H9  after range_update: tut_update(dt) -> TR_NEXT (next program's map) / TR_EXIT (lobby)
//  H10 world: tut_draw_world() after range_draw
//  H11 HUD: tut_hud_game() for hud_game_draw, tut_hud() instead of range_hud while a program runs
//  H12 overlays: tut_overlay() before the pause menu
//  H13 pause menu QUIT / results: tut_exit_to_lobby() instead of the results screens
//  H14 test tour: tut_tour_next() after the map tour
