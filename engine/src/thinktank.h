// Think Tanks (the Tachikoma-type spider tanks of Terminal Conquest and the tutorial's "Enemy ThinkTank" program).
// Sources: Terminal Conquest footage (Underground Base, Sitara), the two tutorial videos (Enemy ThinkTank program), the game's
// data: model/Npc/Tachikoma (Tachikoma_G.nif = the blue team-A tank, Tachikoma_K.nif = the black red-eyed team-B tank),
// GIS_tachikoma.nmb clips, sac.fev events (TACHIKOMA_*, tachi_* voice lines), InGameUI.swf (TC_TACHICOMA panel).
// Data: tk_blue.gvm, tk_red.gvm (tools/tank/make_tank.py; the red file has no clips: it shares the blue one's), tk_hud.gtx
// (panel pictures, tools/tank/make_tk_hud.py). Host test: tools/tank/host/run.sh.
//
// What the footage shows (and this implements):
//   - deployed by a player ("TACHIKOMA READY - PRESS [key]" -> a hologram where he looks, "Click to deploy" -> cyan flash,
//     it drops in: "TACHIKOMA DEPLOYED +200"); the HUD panel shows its picture, the owner, the tank's name ("My Think Tank",
//     "<name>Koma") and its HP (4000).
//   - it walks to the fight and shoots enemies with its gatling ("TARGETED BY THINK TANK" + a red aiming laser on the
//     target), now and then lobs a grenade; it is credited with assists ("TACHIKOMA ASSIST +30").
//   - bullets barely scratch it while its "defensive protocols" are up (a rifle: a few HP per hit) - except on its
//     "ocular sensors" (the red lenses in front: full damage from the front; intel screen: "can be taken down by hacking its
//     defenses or attacking its ocular sensors"). An enemy next to it can hack it (one press: "Hack Think Tank - PRESS [R]"
//     in the match, "Hacking Think Tank - PRESS [E]" in the tutorial): "SUCCESS", then "Bypassing encryption..." reveals 10
//     digits, one per ~1.8 s (18 s, three runs measured); the hacker may move about meanwhile and is exposed to the tank's
//     team ("Hacking a ThinkTank will expose your position to enemies"); its owners see "Hacker Detected". Then "Think Tank
//     defensive protocols shutting down!": it loses ~2000 HP at once (TTM footage), glows red and takes full damage - for
//     ~13 s; then the defences come back ("You failed to destroy enemy Think Tank."). A hack fails if the hacker dies or
//     walks away (18 m). Destroyed: "destroy" clip, explosion, the wreck stays a few seconds.
//   - HP creeps back (+11/s) when it has not been hit for a while (footage: 3445 -> 3510 at 11/s).
// The tutorial uses the same unit with a few switches (TKF_* flags: no AI / no gun, hackable or not, defences kept down
// until it rearms them, a hack that converts instead of opening it up).
//
// API (the game binds the world first: tk_bind):
//   tk_load(vdir)                      models (once; ~1.7 MB); <0 when missing (then tk_spawn fails)
//   tk_spawn(pos, yaw, team, owner, owner_name, tank_name, flags) -> index or -1 (team 0 = the player's side; TKF_* flags)
//   tk_remove(i), tk_clear(), tk_rearm(i) (defences up, full HP)
//   tk_update(dt)                      AI, animation, hacks, projectiles; call once per frame after bots_update
//   tk_ray(o, d, maxd, &t)             first living enemy tank hit by a ray (player bullets) -> index or -1 (then tk_damage)
//   tk_damage(i, dmg, attacker, p, dir, kind)  kind SKD_BULLET / SKD_BLAST / ...; attacker = actor (0 player, 1+i bot, -1 none)
//   tk_blast(c, r, dmg, attacker)      explosions
//   tk_use(actor, eye, dir, held, dt)  hacking: <0 nothing to hack here, 0 a hackable enemy tank is in reach, 1 started now
//   tk_draw(VP, fog)                   tanks + blob shadows + the red "defences down" glow + lasers + grenades
//   tk_hud(MVP, cam, now)              the local player's in-world HUD bits (hack prompt, "SUCCESS", "Bypassing encryption",
//                                      hit diamonds, enemy tags, "TARGETED BY THINK TANK"); call between hud_begin / hud_end;
//                                      tk_hud_style 0 = the match's look, 1 = the Early Access tutorial's
//   queries: tk[i].state (TK_*) / team / hp / hpmax / def / hack_t / hacker / owner, tk_count(team), tk_of_owner(actor), tk_alive_i(i)
//   events: tk_on_event(i, TKE_*, actor) callback (the game: kill feed, notices, rewards)
//   tutorial: tk_tut_* (tutorial.h's TutTankApi, see "the tutorial's Think Tank" below)
//   vision: tk[i].hl = TK_HL_* (enemy / ally / defences down); tk_draw_body(i, VP, col) draws tank i once more in a flat
//           additive colour (e.g. inside glDepthFunc(GL_GREATER) for a through-wall silhouette); tk_draw_info for anything else
#pragma once
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "player.h"
#include "audio.h"
#include "model.h"
#include "nav.h"
#include "light.h"
#include "sprite.h"
#include "fx.h"
#include "bots.h"
#include "skills.h"     // SkWorld (actors: 0 = the local player, 1 + i = bot i), SKD_* damage kinds
#include "hud_game.h"
#include "vita_prompt.h"   // ui agent: the Vita Square button in the hack prompt (BRIEF.md Vita prompts rule)

#define TK_MAX 4
#define TK_HP 4000.0f
#define TK_DEF_MUL 0.15f        // bullet damage factor while the defensive protocols are up (footage: a few HP per rifle hit)
#define TK_DEF_MUL_BLAST 1.5f   // explosives while up (footage: 3263 -> 2409 at one blast)
#define TK_OPEN_MUL 1.0f        // after the shutdown ("it will take far more damage")
#define TK_OPEN_MUL_BLAST 3.0f
#define TK_HACK_RUN 10.0f       // one press of use starts the hack, it runs this long (v2.0: 10 digits, ~1 s each - EP14 tutorial
                                // 7:04.5 plant -> 7:13.7 all ten -> 7:14.5 panel gone; EP15 Terminal Conquest 11:30.5-11:36 six
                                // digits; the EA tutorial had ~1.8 s each, 18 s) ...
#define TK_OPEN_T 13.0f         // the defences stay down this long, then come back (EA tutorial: 12-14 s, three runs)
#define TK_EYE_R 24.0f          // the ocular sensor (the red lenses in front): hits there ignore the armour
#define TK_EYE_MUL 1.0f
#define TK_HACK_COST 2000.0f    // ... then the shutdown costs it this much HP (footage: 3510 -> 1525, 2409 -> 229)
#define TK_HACK_REACH 1250.0f   // cm from the hacker's eye to the tank's body (b86 v2.0: from range - EP15 Terminal Conquest 11:29.6
                                // "Hack Think Tank" with the tank ~10 m off, EP14 tutorial 7:03.5-7:04.5 hacked from the arena's door,
                                // ~12 m from the glass cylinder's centre; the EA version: next to it, 3.2 m) ...
#define TK_HACK_NEAR 400.0f     // ... beyond this the hacker has to look at it (within ~15 degrees) and see it
#define TK_HACK_LEASH 1800.0f   // the hack fails when the hacker goes farther than this (or dies)
#define TK_REGEN 11.0f          // HP/s ...
#define TK_REGEN_AFTER 20.0f    // ... after this long without damage (defences up only)
#define TK_SIGHT 4500.0f        // engagement range (cm)
#define TK_GUN_DMG 8.0f         // per hit on an operator (100 HP + 100 AP): a burst hurts, it does not delete you
#define TK_GUN_RATE 11.0f       // rounds per second while the fire_loop plays
#define TK_GREN_CD 9.0f
#define TK_GREN_DMG 90.0f
#define TK_GREN_R 450.0f
#define TK_WRECK_T 8.0f         // the wreck stays this long
#define TK_DROP_T 1.1f          // call-in: falls from the sky this long

enum { TK_OFF, TK_DROP, TK_LIVE, TK_DEAD };
enum { TKH_DEFENSE, TKH_CONVERT };                                  // what a completed hack does
enum { TKE_DEPLOYED = 1, TKE_HACK_START, TKE_HACK_DONE, TKE_HACK_FAIL, TKE_DESTROYED, TKE_KILL, TKE_ASSIST, TKE_CONVERTED, TKE_RESTORED };   // RESTORED: the defences came back up (actor = the last hacker)
#define TKF_NOAI   1            // stands still, never shoots (tutorial dummy)
#define TKF_NOHACK 2            // cannot be hacked
#define TKF_CONVERT 4           // a finished hack switches its side (TKH_CONVERT) instead of dropping its defences
#define TKF_DROP   8            // arrives with the call-in drop (else appears in place)
#define TKF_NOREGEN 16
#define TKF_NORESTORE 32          // the defences stay down after a hack until tk_rearm (the tutorial times it)
#define TKF_LEASH  64           // b86: stays within TK_LEASH_R of where it was spawned (the tutorial's arena: v2.0 EP14 7:03-7:20,
                                // it walks about in the glass cylinder and shoots at the player)
#define TK_LEASH_R 450.0f
#define TK_HL_ENEMY 1           // vision: an enemy of the local player (red outline)
#define TK_HL_ALLY  2
#define TK_HL_OPEN  4           // defences down (the footage's red glow)

typedef struct {
	int state, team, owner, flags, kind;   // kind: 0 blue model, 1 red model (by team at spawn)
	char owner_name[24], name[24];
	Player pl; float t;                    // state time
	float hp, hpmax; int def;              // def: defensive protocols up
	int hacker; float hack_t, hack_hold, hack_hold_t; int hold_actor;   // hacker: actor running a hack (-1 none); hack_t: s into it
	float since_hit, regen_acc;
	// brain
	int tgt; float think_t, see_t, lost_t, gren_cd, burst_t, pause_t, laser; int firing, sees; V3 last_seen, aim_pt;
	float path[32][3]; int npath, ipath; float repath_t, prog_d, prog_t; float goal[3]; int has_goal; float voice_cd, noenemy_t;
	int nav_pend; unsigned nav_gen; long long nav_ask_f;   // perf (b35): a path search asked for (tk_nav_step), its ask number and frame
	// body
	Anim an; CSkin cs; MBone *pose; int base; float step_d, lit[3], floor_z, turn;   // cs: chr.h's NEON skinning buffers (as the bots)
	V3 muzzle, eye, sph[3];
	// effects / HUD
	float hit_flash, dmg_snd_cd, shot_cd; V3 hit_pt[4]; float hit_t[4]; int hit_n;
	float drop_z; int hl;
	int assist_by;                         // last actor it damaged (assists)
	unsigned code[2];                      // the hack's "encryption" digits (HUD)
	float open_t;                          // s since the defences went down (they come back at TK_OPEN_T)
	int last_hacker; float done_t, succ_t; // the last hack (HUD: full code shown a moment, the "SUCCESS" ring at the start)
	unsigned char ser; int killed_by;     // net: a serial per spawn, the actor that destroyed it
	int custom; float dmg_mul, gren_mul, spd_mul;   // b74: built from the chosen parts (tkp_*): the gun / rocket / speed factors (1 = the stock tank)
	V3 net_pos, net_vel; float net_yaw, net_age; int net_firing;   // net (client): the host's latest (tk_puppet follows it)
	V3 home; float strafe_t; int strafe;   // b86: TKF_LEASH (where it was spawned), the side step while it fights in the arena
	int boom2;                             // b86: the death's second, higher fireball has gone off
	int seen_frame;                        // b102: bot_frame of the last tk_draw that drew it (the HUD's live picture skins it itself otherwise)
} Tank;
typedef struct { int on, team, owner, tank; V3 p, v; float t; unsigned char ser; V3 p0, v0; } TkGren;   // (ser, p0 / v0: net, the launch)

static Tank tk[TK_MAX]; static TkGren tk_gren[6];
// net (net_game.h): tk_mirror on a client: the host's tanks are shown (positions, hacks, deaths from the snapshots); no brain, no
// damage here, the presentation runs on the host's transitions (net_game.h calls it). 0 offline.
static int tk_mirror; static unsigned char tk_ser_n;
static NavCtx tk_nav; static int tk_nav_cur = -1; static unsigned tk_nav_cur_gen, tk_nav_serial;   // perf (b35): the tanks' path searches (tk_nav_step)
static Model tk_model[2]; static int tk_ok, tk_cl[48];
static GLuint tk_white, tk_soft, tk_ringt;          // white 4 x 4 (glow / hologram passes, lasers), a soft white dot and a white ring (sprite.h's blob is black)
static const SkWorld *tk_w; static Player *tk_pl;   // world access (skills' SkWorld) + the local player (pushed out of the tanks)
static void (*tk_on_event)(int i, int ev, int actor);
// vision.h's looks for any model (main.c points this at vis_model after including vision.h): look 0 = hologram (translucent
// fill + bright fresnel rim; the hacked tank in red, the call-in placement), 1 = outline only where hidden (as teammates).
// Returns 0 when it did not draw (vision off): the tank code then uses its own additive white-texture pass.
static int (*tk_vis_model)(const Model *M, const float *VP, const float *Mw, const MBone *P, const float *col, int look);
static int tk_local_team = 0;                       // the local player's team (HUD colours, outlines)
static int tk_tut_mode;                             // b86: the tutorial's tank is out (its SUCCESS is the tutorial's own disc)
static float tk_slow_t;                             // b86 test runs (main.c "tkslow"): s of slow motion after a hack is planted / a tank blows up
static int tk_b_body = -1, tk_b_tail = -1;
static const float *tk_a_muzzle, *tk_a_gren, *tk_a_eye, *tk_a_tag, *tk_a_smoke;
static float tk_targeted_t;                         // > 0: an enemy tank aims at the local player
static int tk_stat_skinned;
enum { TC_IDLE, TC_IDLE2, TC_WALK, TC_WALK_B, TC_WALK_L, TC_WALK_R, TC_RUN, TC_TURN_L, TC_TURN_R, TC_FIRE_START, TC_FIRE_LOOP, TC_FIRE_END,
	TC_GREN, TC_DMG, TC_DMG_CRIT, TC_DESTROY, TC_STUN_START, TC_STUN_LOOP, TC_STUN_END, TC_FALL, TC_LAND, TC_DANCE, TC_N };
static const char *const TK_CLIP_NAME[TC_N] = { "idle", "idle2", "walk_f", "walk_b", "walk_l", "walk_r", "run", "turn_l", "turn_r", "fire_start",
	"fire_loop", "fire_end", "gren", "dmg", "dmg_crit", "destroy", "stun_start", "stun_loop", "stun_end", "fall", "land", "dance" };

// ---------------------------------------------------------------- small helpers
static float tk_frand(void) { return (rand() % 10001) / 10000.f; }
static float tk_wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }
static V3 tk_world(const Tank *k, const float *p) {   // model space (faces -y) -> world
	float th = k->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	return v3(k->pl.pos.x + c * p[0] - s * p[1], k->pl.pos.y + s * p[0] + c * p[1], k->pl.pos.z + p[2] + (k->state == TK_DROP ? k->drop_z : 0));
}
static int tk_actor_team(int a) { return a == 0 ? tk_local_team : (a - 1 < nbot ? bots[a - 1].team : -1); }
static const char *(*tk_name_fn)(int a);   // display name of an actor (the game sets it; NULL: "")
static const char *tk_actor_name(int a) { const char *n = tk_name_fn ? tk_name_fn(a) : NULL; return n ? n : ""; }
static int tk_alive_i(int i) { return i >= 0 && i < TK_MAX && (tk[i].state == TK_LIVE || tk[i].state == TK_DROP); }
static int tk_count(int team) { int n = 0; for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i) && tk[i].team == team) n++; return n; }
__attribute__((unused)) static int tk_of_owner(int a) { for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i) && tk[i].owner == a) return i; return -1; }
static void tk_event(int i, int ev, int actor) { if (tk_on_event) tk_on_event(i, ev, actor); }

// 3D sound of a RAM sample (prefix) with the event's design volume and distance range (sac.fev TACHIKOMA_*)
static void tk_snd(const char *pfx, V3 p, float vol, float maxd) {
	V3 d = vsub(p, lis_pos); float dist = sqrtf(vdot(d, d)); if (dist > maxd) return;
	float att = 1 - dist / maxd; att = 0.2f + 0.8f * att * att;
	float rx = sinf(lis_yaw), ry = -cosf(lis_yaw), pan = (d.x * rx + d.y * ry) / (dist + 1);
	play_pan(pfx, (int)(256 * vol * att), pan * 0.85f);
}
static void tk_voice(const char *pfx, V3 p, float vol) {   // the tank's own voice lines (tachi_*, sac.fev voice/tachikoma: v0.25-0.30, 0..80 m), 3D
	V3 d = vsub(p, lis_pos); float dist = sqrtf(vdot(d, d)); if (dist > 8000 || !nvline) return;
	const VEntry *e = find_vline(pfx); if (!e) return;
	float att = 1 - dist / 8000; float rx = sinf(lis_yaw), ry = -cosf(lis_yaw), pan = (d.x * rx + d.y * ry) / (dist + 1);
	voice_play_entry(e, (int)(0.32f * vol * (0.3f + 0.7f * att) * 65536), pan * 0.8f, 60);
}

// ---------------------------------------------------------------- loading
static int tk_load(const char *vdir) {
	if (tk_ok) return tk_ok > 0 ? 0 : -1;
	char p[128]; int r;
	model_keep_cpu = 1;
	snprintf(p, sizeof(p), "%stk_blue.gvm", vdir); r = model_load(&tk_model[0], p);
	if (r == 0) { snprintf(p, sizeof(p), "%stk_red.gvm", vdir); if (model_load(&tk_model[1], p) < 0) r = -5; }
	model_keep_cpu = 0;
	if (r < 0) { tk_ok = -1; return r; }
	Model *B = &tk_model[0], *R = &tk_model[1];
	if (R->nb != B->nb) { tk_ok = -1; return -6; }
	for (int b = 0; b < B->nb; b++) if (strcmp(B->bone_name[b], R->bone_name[b])) { tk_ok = -1; return -7; }
	free(R->clip); R->clip = B->clip; R->nclip = B->nclip;                 // one clip set for both (same bones)
	for (int m = 0; m < 2; m++) { Model *M = &tk_model[m]; model_prepare_rigid(M);
		for (int b = 0; b < M->nb; b++) M->upper[b] = strstr(M->bone_name[b], "leg") || strstr(M->bone_name[b], "foot") || !strcmp(M->bone_name[b], "Bip01") ? 0.f : 1.f; }
	for (int c = 0; c < TC_N; c++) tk_cl[c] = model_find_clip(B, TK_CLIP_NAME[c]);
	if (tk_cl[TC_IDLE] < 0 || tk_cl[TC_DESTROY] < 0) { tk_ok = -1; return -8; }
	for (int c = 0; c < TC_N; c++) if (tk_cl[c] < 0) tk_cl[c] = tk_cl[TC_IDLE];
	tk_b_body = model_find_bone(B, "Bip01_body"); tk_b_tail = model_find_bone(B, "Bip01_tail");
	tk_a_muzzle = model_attach(B, "muzzle"); tk_a_gren = model_attach(B, "gren"); tk_a_eye = model_attach(B, "eye"); tk_a_tag = model_attach(B, "tag"); tk_a_smoke = model_attach(B, "smoke");
	if (tk_b_body < 0 || !tk_a_muzzle) { tk_ok = -1; return -9; }
	{ static unsigned char px[3][32 * 32 * 4]; GLuint *ids[3] = { &tk_white, &tk_soft, &tk_ringt };
	  for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { float dx = (x + 0.5f) / 16 - 1, dy = (y + 0.5f) / 16 - 1, r = sqrtf(dx * dx + dy * dy);
		float soft = r >= 1 ? 0 : (1 - r) * (1 - r), ring = fmaxf(0, 1 - fabsf(r - 0.86f) / 0.1f); float a[3] = { 1, soft, ring };
		for (int t = 0; t < 3; t++) { unsigned char *q = px[t] + (y * 32 + x) * 4; q[0] = q[1] = q[2] = 255; q[3] = (unsigned char)(a[t] * 255); } }
	  for (int t = 0; t < 3; t++) { glGenTextures(1, ids[t]); glBindTexture(GL_TEXTURE_2D, *ids[t]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, px[t]); VGL_XFER_WAIT(); } }
	tk_ok = 1; return 0;
}
static void tk_bind(Player *local, const SkWorld *w) { tk_pl = local; tk_w = w; }
// b74: free the tank models (the front end loads them for the MY THINK TANK screen; a match that has no tanks gives the
// memory back). tk_load loads them again when needed.
static void tkp_unload(void); static void tk_clear(void); static void tk_holo_free(void);
static void tk_unload(void) {
	if (tk_ok <= 0) { tk_ok = 0; return; }
	tk_clear(); tkp_unload(); tk_holo_free();
	tk_model[1].clip = NULL; tk_model[1].nclip = 0;   // (shared with the blue one's)
	model_free(&tk_model[0]); model_free(&tk_model[1]);
	if (tk_white) glDeleteTextures(1, &tk_white); if (tk_soft) glDeleteTextures(1, &tk_soft); if (tk_ringt) glDeleteTextures(1, &tk_ringt);
	tk_white = tk_soft = tk_ringt = 0; tk_ok = 0;
}

// ---------------------------------------------------------------- b74: My Think Tank - the parts
// (user: the Fandom "Tachikoma" page + the PC's "My Think Tank / CUSTOMIZE THINK TANK" screenshots) The player's own Think
// Tank (Terminal Conquest call-in) is built from six parts: PROPULSION (the legs), HULL, CABIN, MAIN ARM, SUB ARM, SUPPORT,
// each a model of its own on the tank's bones (tools/tank/make_tank_parts.py: tkp_core.gvm + tkp_<slot>_<k>.gvm, the support
// on its cabin: tkp_supp_<k>_c<cabin>.gvm) posed with tk_blue's clips; stats per part from tk_parts.txt (the page's: weight,
// HP, the legs' loadage / max speed / acceleration, the main arm's DPS, the sub arm's rocket damage / spread). The tank's HP
// is the sum of the legs', the hull's and the cabin's (the screenshots: 3000 for the 001A set, 4800 for the 004A set); its
// gun hits harder by DPS / 105, its rockets by damage / 200, it walks at speed / 700 of the stock tank; the total weight must
// stay within the legs' loadage (menu_fe.h refuses to save more). Other tanks (bots', enemies', the tutorial's) stay stock.
#define TKP_NS 6
#define TKP_MAXK 4
static const char *const TKP_STEM[TKP_NS] = { "prop", "hull", "cabin", "main", "sub", "supp" };
typedef struct { char code[16]; int weight, hp, loadage, speed; float accel; int dps, rdmg; float rspread; int tris; } TkPart;
static TkPart tkp_info[TKP_NS][TKP_MAXK]; static int tkp_n[TKP_NS], tkp_info_ok;
static Model tkp_core, tkp_m[TKP_NS]; static CSkin tkp_cs[TKP_NS + 1];
static int tkp_core_ok, tkp_have[TKP_NS] = { -1, -1, -1, -1, -1, -1 }, tkp_have_cab = -1;
static unsigned char tkp_sel[TKP_NS];          // the player's parts (main.c: cfg.tk_part)
static int tkp_info_load(const char *vdir) {
	if (tkp_info_ok) return tkp_info_ok > 0 ? 0 : -1;
	char p[128]; snprintf(p, sizeof(p), "%stk_parts.txt", vdir); FILE *f = fopen(p, "r"); tkp_info_ok = -1; if (!f) return -1;
	char ln[200]; memset(tkp_n, 0, sizeof(tkp_n));
	while (fgets(ln, sizeof(ln), f)) {
		if (ln[0] == '#') continue; char st[16], code[16]; int k, w, hp, ld, sp, dps, rd, tr; float ac, rs;
		if (sscanf(ln, "%15s %d %15s %d %d %d %d %f %d %d %f %d", st, &k, code, &w, &hp, &ld, &sp, &ac, &dps, &rd, &rs, &tr) != 12 || k < 0 || k >= TKP_MAXK) continue;
		for (int s = 0; s < TKP_NS; s++) if (!strcmp(st, TKP_STEM[s])) { TkPart *P = &tkp_info[s][k]; snprintf(P->code, sizeof(P->code), "%s", code);
			P->weight = w; P->hp = hp; P->loadage = ld; P->speed = sp; P->accel = ac; P->dps = dps; P->rdmg = rd; P->rspread = rs; P->tris = tr; if (k + 1 > tkp_n[s]) tkp_n[s] = k + 1; }
	}
	fclose(f); int ok = 1; for (int s = 0; s < TKP_NS; s++) ok &= tkp_n[s] > 0;
	tkp_info_ok = ok ? 1 : -1; logp("think tank parts: %s (%d %d %d %d %d %d per slot) (b74)\n", ok ? "tk_parts.txt" : "tk_parts.txt incomplete", tkp_n[0], tkp_n[1], tkp_n[2], tkp_n[3], tkp_n[4], tkp_n[5]);
	return ok ? 0 : -1;
}
static int tkp_k(const unsigned char *sel, int s) { int k = sel ? sel[s] : 0; return k < tkp_n[s] ? k : 0; }
// the totals of a choice (the screen's numbers)
typedef struct { int weight, loadage, hp, speed, dps, rdmg; float accel, rspread; } TkStats;
static void tkp_stats(const unsigned char *sel, TkStats *o) {
	memset(o, 0, sizeof(*o)); if (tkp_info_ok <= 0) return;
	for (int s = 0; s < TKP_NS; s++) { const TkPart *P = &tkp_info[s][tkp_k(sel, s)]; o->weight += P->weight; o->hp += P->hp; }
	const TkPart *L = &tkp_info[0][tkp_k(sel, 0)], *A = &tkp_info[3][tkp_k(sel, 3)], *R = &tkp_info[4][tkp_k(sel, 4)];
	o->loadage = L->loadage; o->speed = L->speed; o->accel = L->accel; o->dps = A->dps; o->rdmg = R->rdmg; o->rspread = R->rspread;
}
static int tkp_load_one(Model *M, const char *vdir, const char *name) {
	char p[128]; snprintf(p, sizeof(p), "%s%s", vdir, name); model_keep_cpu = 1; int r = model_load(M, p); model_keep_cpu = 0;
	if (r < 0) return r;
	const Model *B = &tk_model[0]; if (M->nb != B->nb) { model_free(M); return -6; }
	for (int b = 0; b < M->nb; b++) if (strcmp(M->bone_name[b], B->bone_name[b])) { model_free(M); return -7; }
	model_prepare_rigid(M); return 0;
}
static void tkp_unload(void) {
	for (int s = 0; s < TKP_NS; s++) if (tkp_have[s] >= 0) { model_free(&tkp_m[s]); tkp_have[s] = -1; }
	if (tkp_core_ok > 0) model_free(&tkp_core);
	for (int s = 0; s <= TKP_NS; s++) if (tkp_cs[s].ok) cskin_free(&tkp_cs[s]);
	tkp_core_ok = 0; tkp_have_cab = -1;
}
// load the core and the chosen part of each slot (only what changed); 0 = ready
static int tkp_load(const char *vdir, const unsigned char *sel) {
	if (tk_load(vdir) < 0 || tkp_info_load(vdir) < 0) return -1;
	if (!tkp_core_ok) { tkp_core_ok = tkp_load_one(&tkp_core, vdir, "tkp_core.gvm") == 0 ? 1 : -1; if (tkp_core_ok > 0) cskin_init(&tkp_cs[TKP_NS], &tkp_core); }
	if (tkp_core_ok < 0) return -1;
	int cab = tkp_k(sel, 2), bad = 0;
	for (int s = 0; s < TKP_NS; s++) {
		int k = tkp_k(sel, s); if (tkp_have[s] == k && (s != 5 || tkp_have_cab == cab)) continue;
		if (tkp_have[s] >= 0) { model_free(&tkp_m[s]); if (tkp_cs[s].ok) cskin_free(&tkp_cs[s]); tkp_have[s] = -1; }
		char nm[40]; if (s == 5) snprintf(nm, sizeof(nm), "tkp_supp_%d_c%d.gvm", k, cab); else snprintf(nm, sizeof(nm), "tkp_%s_%d.gvm", TKP_STEM[s], k);
		int r = tkp_load_one(&tkp_m[s], vdir, nm); if (r < 0) { logp("think tank part %s: error %d\n", nm, r); bad = 1; continue; }
		cskin_init(&tkp_cs[s], &tkp_m[s]); tkp_have[s] = k; if (s == 5) tkp_have_cab = cab;
	}
	return bad ? -1 : 0;
}
static int tkp_ready(void) { if (tkp_core_ok <= 0) return 0; for (int s = 0; s < TKP_NS; s++) if (tkp_have[s] < 0) return 0; return 1; }
// the models a tank is drawn with: the stock one (its own skinning buffers), or the core and the parts
static int tk_models(Tank *k, Model **ms, CSkin **cs) {
	if (k->custom && tkp_ready()) { ms[0] = &tkp_core; cs[0] = &tkp_cs[TKP_NS]; for (int s = 0; s < TKP_NS; s++) { ms[1 + s] = &tkp_m[s]; cs[1 + s] = &tkp_cs[s]; } return 1 + TKP_NS; }
	ms[0] = &tk_model[k->kind]; cs[0] = &k->cs; return 1;
}
// the player's call-in: the parts' stats (ttm.h ttm_call_tank, actor 0, offline / the host)
static void tkp_apply(int i) {
	if (i < 0 || i >= TK_MAX || tkp_info_ok <= 0 || !tkp_ready() || tk_mirror) return;
	Tank *k = &tk[i]; TkStats S; tkp_stats(tkp_sel, &S); if (S.hp <= 0) return;
	k->custom = 1; k->hp = k->hpmax = (float)S.hp; k->dmg_mul = S.dps / 105.f; k->gren_mul = S.rdmg / 200.f; k->spd_mul = S.speed / 700.f;
	logp("  think tank parts: %s %s %s %s %s %s: %d HP, weight %d / %d, speed x%.2f, gun x%.2f, rockets x%.2f (b74)\n", tkp_info[0][tkp_k(tkp_sel, 0)].code,
	     tkp_info[1][tkp_k(tkp_sel, 1)].code, tkp_info[2][tkp_k(tkp_sel, 2)].code, tkp_info[3][tkp_k(tkp_sel, 3)].code, tkp_info[4][tkp_k(tkp_sel, 4)].code,
	     tkp_info[5][tkp_k(tkp_sel, 5)].code, S.hp, S.weight, S.loadage, k->spd_mul, k->dmg_mul, k->gren_mul);
}

// ---------------------------------------------------------------- spawning
static void tk_free_slot(Tank *k) { if (k->an.m) anim_free(&k->an); if (k->cs.ok) cskin_free(&k->cs); memset(k, 0, sizeof(*k)); }
__attribute__((unused)) static void tk_remove(int i) { if (i >= 0 && i < TK_MAX) tk_free_slot(&tk[i]); }
static void tk_clear(void) { for (int i = 0; i < TK_MAX; i++) tk_free_slot(&tk[i]); memset(tk_gren, 0, sizeof(tk_gren)); tk_targeted_t = 0; tk_nav_cur = -1; tk_tut_mode = 0; }
static int tk_spawn(V3 pos, float yaw, int team, int owner, const char *owner_name, const char *tank_name, int flags) {
	if (tk_ok <= 0) return -1;
	int i = -1; for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_OFF) { i = k; break; }
	if (i < 0) for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_DEAD) { i = k; break; }   // reuse a wreck
	if (i < 0) return -1;
	Tank *k = &tk[i]; tk_free_slot(k);
	k->team = team; k->owner = owner; k->flags = flags; k->kind = team == 1 ? 1 : 0;
	snprintf(k->owner_name, sizeof(k->owner_name), "%.*s", (int)sizeof(k->owner_name) - 1, owner_name ? owner_name : ""); snprintf(k->name, sizeof(k->name), "%.*s", (int)sizeof(k->name) - 1, tank_name ? tank_name : "Think Tank");
	k->pl.pos = pos; k->pl.yaw = yaw; k->pl.grounded = 1; k->pl.speed_mul = 1; k->home = pos; k->strafe = (i & 1) ? 1 : -1; k->strafe_t = 2 + tk_frand() * 3;
	k->hp = k->hpmax = TK_HP; k->def = 1; k->hacker = -1; k->hold_actor = -1; k->tgt = -1; k->assist_by = -1; k->last_hacker = -1; k->done_t = k->succ_t = 99;
	k->ser = ++tk_ser_n ? tk_ser_n : ++tk_ser_n; k->killed_by = -1; k->net_pos = pos; k->net_yaw = yaw; k->dmg_mul = k->gren_mul = k->spd_mul = 1;
	k->state = flags & TKF_DROP ? TK_DROP : TK_LIVE; k->t = 0; k->drop_z = flags & TKF_DROP ? 1400 : 0;
	k->code[0] = (unsigned)rand() * 2654435761u; k->code[1] = (unsigned)rand() * 40503u;
	Model *M = &tk_model[k->kind]; anim_init(&k->an, M); cskin_init(&k->cs, M);
	k->base = k->state == TK_DROP ? TC_FALL : TC_IDLE; anim_set_base(&k->an, tk_cl[k->base], 0, 0);
	light_at(pos.x, pos.y, pos.z + 30, k->lit); k->floor_z = pos.z; k->think_t = 0.5f; k->gren_cd = 4 + tk_frand() * 4; k->voice_cd = 2;
	if (k->state == TK_DROP) tk_snd("fp_tachi_spawn", pos, 0.9f, 10000); else tk_snd("fp_tachi_match_spawn", pos, 0.9f, 10000);
	tk_event(i, TKE_DEPLOYED, owner);
	return i;
}

// ---------------------------------------------------------------- damage
static int tk_ray_eye;   // set by tk_ray*: the ray hit the ocular sensor; the next SKD_BULLET tk_damage uses it
static void tk_die(int i, int attacker) {   // destroyed (the host's damage; a client: the host's state)
	Tank *k = &tk[i];
	k->hp = 0; k->state = TK_DEAD; k->t = 0; k->firing = 0; k->laser = 0; k->hacker = -1; k->killed_by = attacker;
	anim_stop(&k->an); anim_set_base(&k->an, tk_cl[TC_DESTROY], 0.15f, 0);
	V3 c = tk_world(k, (const float[3]){ 0, 0, 110 }); float up[3] = { 0, 0, 1 };
	// b86 (v2.0 EP14 7:20.4-7:20.9): a white flash with a horizontal light streak (tk_hud), then a fireball about twice a
	// grenade's that climbs (a second, higher one 0.12 s later: tk_update), dark debris and smoke; the wreck is left lying
	fx_scale = 1.7f; fx_explosion(&c.x, up); fx_scale = 1; { V3 g = v3(c.x, c.y, k->floor_z + 4); fx_scorch(&g.x, up); }
	k->boom2 = 0; tk_slow_t = 6.0f;
	tk_snd("tachi_exp", c, 0.65f, 12000); tk_voice("tachi_death_", c, 0.9f);
	tk_event(i, TKE_DESTROYED, attacker);
}
static int tk_damage(int i, float dmg, int attacker, V3 p, V3 dir, int kind) {
	if (!tk_alive_i(i) || dmg <= 0) return 0;
	Tank *k = &tk[i];
	if (attacker >= 0 && tk_actor_team(attacker) == k->team) return 0;            // no friendly fire
	int blast = kind == SKD_BLAST, eye = kind == SKD_BULLET && tk_ray_eye; tk_ray_eye = 0;
	float mul = k->def ? (blast ? TK_DEF_MUL_BLAST : TK_DEF_MUL) : (blast ? TK_OPEN_MUL_BLAST : TK_OPEN_MUL);
	if (eye && mul < TK_EYE_MUL) mul = TK_EYE_MUL;   // the ocular sensor
	if (!tk_mirror) k->hp -= dmg * mul; k->since_hit = 0; k->hit_flash = 1;
	int h = k->hit_n++ & 3; k->hit_pt[h] = p; k->hit_t[h] = 0.35f;
	if (k->dmg_snd_cd <= 0) { tk_snd("tachi_hit_", p, 0.45f, 6000); k->dmg_snd_cd = 0.12f; }
	(void)dir;
	if (attacker >= 0 && k->tgt < 0 && k->state == TK_LIVE) { k->tgt = attacker; k->see_t = 0; }   // turn on whoever shoots it
	if (k->hp <= 0 && !tk_mirror) { tk_die(i, attacker); return 1; }
	if (blast && tk_frand() < 0.5f) { anim_play(&k->an, tk_cl[TC_DMG_CRIT], 0.06f, 0.2f, 1); if (k->voice_cd <= 0) { tk_voice("tachi_bombdamage_", p, 0.8f); k->voice_cd = 4; } }
	else if (tk_frand() < 0.12f && !k->firing) anim_play(&k->an, tk_cl[TC_DMG], 0.06f, 0.2f, 1);
	if (k->voice_cd <= 0 && tk_frand() < 0.08f) { tk_voice("tachi_damage_", p, 0.7f); k->voice_cd = 5; }
	return 0;
}
static void tk_blast(V3 c, float r, float dmg, int attacker) {
	for (int i = 0; i < TK_MAX; i++) { if (!tk_alive_i(i)) continue; Tank *k = &tk[i];
		V3 e = vsub(k->sph[0], c); float d = sqrtf(vdot(e, e)) - 80; if (d > r) continue; if (d < 0) d = 0;
		if (!bot_los(c, k->sph[0]) && d > 100) continue;
		tk_damage(i, dmg * (1 - 0.7f * d / r), attacker, k->sph[0], vmul(e, 1 / (d + 81)), SKD_BLAST); }
}
// ray vs the tanks' body spheres (pod, body, face). team_skip: tanks of this team are ignored (-1 none)
static float tk_ray_sph(V3 o, V3 d, V3 c, float r) {
	V3 m = vsub(o, c); float b = vdot(m, d), cc = vdot(m, m) - r * r; if (cc > 0 && b > 0) return -1;
	float disc = b * b - cc; if (disc < 0) return -1; float t = -b - sqrtf(disc); return t < 0 ? 0 : t;
}
static int tk_ray_team(V3 o, V3 d, float maxd, float *t, int team_skip) {
	int hit = -1; float bt = maxd;
	for (int i = 0; i < TK_MAX; i++) {
		Tank *k = &tk[i]; if (k->state == TK_OFF || k->state == TK_DEAD || k->team == team_skip) continue;
		static const float R[3] = { 78, 82, 48 }; float tb = 1e30f;
		for (int s = 0; s < 3; s++) { float h = tk_ray_sph(o, d, k->sph[s], R[s]); if (h >= 0 && h < tb) tb = h; }
		if (tb >= 1e29f) continue;
		// the ocular sensor sits in the front of the body: the ray passes within TK_EYE_R of it, coming from the front
		V3 m = vsub(k->eye, o), fe = vsub(k->eye, k->sph[0]); float te = vdot(m, d), fl = sqrtf(fe.x * fe.x + fe.y * fe.y) + 1e-3f;
		V3 cp = vsub(m, vmul(d, te)); int eye = te > 0 && te < tb + 90 && vdot(cp, cp) < TK_EYE_R * TK_EYE_R && -(d.x * fe.x + d.y * fe.y) / fl > 0.35f;
		if (tb < bt) { bt = tb; hit = i; tk_ray_eye = eye; }
	}
	if (hit < 0) tk_ray_eye = 0;
	if (hit >= 0 && t) *t = bt;
	return hit;
}
static int tk_ray(V3 o, V3 d, float maxd, float *t) { return tk_ray_team(o, d, maxd, t, tk_local_team); }

// ---------------------------------------------------------------- hacking (player and bots: press use next to an enemy tank)
static void tk_hack_begin(int i, int actor) {   // the hack is planted ("SUCCESS"): it runs by itself now
	Tank *k = &tk[i];
	k->hack_hold = 0; k->hold_actor = -1; k->hacker = actor; k->hack_t = 0; k->last_hacker = actor; k->succ_t = 0; k->done_t = 99; tk_slow_t = 3.0f;
	play("ui_ghost_hacking_success", actor == 0 ? 230 : 0);
	if (k->team == tk_local_team) { play("is_turret_hacking_alarm", 170); }
	tk_voice("tachi_enemyaccess_", k->sph[0], 0.8f);
	tk_event(i, TKE_HACK_START, actor);
}
// rearm: defences up again, full HP (the tutorial's retry)
static void tk_rearm(int i) { if (!tk_alive_i(i)) return; Tank *k = &tk[i]; k->def = 1; k->hacker = -1; k->hack_t = 0; k->open_t = 0; k->hp = k->hpmax; k->done_t = 99; }
static float tk_use(int actor, V3 eye, V3 dir, int held, float dt) {
	int team = tk_actor_team(actor); int best = -1; float bd = TK_HACK_REACH;
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i];
		if (k->state != TK_LIVE || k->team == team || (k->flags & TKF_NOHACK) || !k->def || k->hacker >= 0) continue;
		V3 e = vsub(k->sph[0], eye); float d = sqrtf(vdot(e, e)) - 75; if (d > bd) continue;
		if (actor == 0 && d > 40 && vdot(e, dir) < 0.25f * (d + 75)) continue;     // the player has to look at it
		if (d > TK_HACK_NEAR && (vdot(e, dir) < 0.966f * (d + 75) || (!tk_tut_mode && !bot_los(eye, k->sph[0])))) continue;   // b86: from range: aimed at, in sight (the tutorial: through its glass)
		bd = d; best = i; }
	for (int i = 0; i < TK_MAX; i++) if (tk[i].hold_actor == actor && i != best) { tk[i].hold_actor = -1; tk[i].hack_hold = 0; }
	if (best < 0) return -1;
	Tank *k = &tk[best];
	if (!held) { if (k->hold_actor == actor) { k->hold_actor = -1; k->hack_hold = 0; } return 0; }
	(void)dt;
	tk_hack_begin(best, actor);   // one press plants it
	return 1;
}
static void tk_hack_step(int i, float dt) {
	Tank *k = &tk[i]; if (k->hacker < 0) return;
	int a = k->hacker; int ok = tk_w && tk_w->alive(a);
	if (ok) { V3 e = vsub(tk_w->pos(a), k->pl.pos); ok = vdot(e, e) < TK_HACK_LEASH * TK_HACK_LEASH; }
	if (!ok) { k->hacker = -1; k->hack_t = 0; tk_event(i, TKE_HACK_FAIL, a); return; }
	k->hack_t += dt;
	if (sk_valid(a)) { float *m = &SKA[a].mark_t[k->team & 1]; if (*m < 1.0f) *m = 1.0f; }   // "Hacking a ThinkTank will expose your position to enemies"
	if (k->hack_t >= TK_HACK_RUN) {
		k->hacker = -1; k->hack_t = 0; k->done_t = 0;
		if (k->flags & TKF_CONVERT) {   // the hack turns it: it fights for the hacker's side now
			k->team = tk_actor_team(a); k->owner = a; k->tgt = -1; k->firing = 0;
			snprintf(k->owner_name, sizeof(k->owner_name), "%s", tk_actor_name(a));
			tk_voice("tachi_teamsuccess_", k->sph[0], 0.8f); tk_event(i, TKE_CONVERTED, a);
		} else {
			k->def = 0; k->open_t = 0; k->hp -= TK_HACK_COST; if (k->hp < 1) k->hp = 1;
			anim_play(&k->an, tk_cl[TC_DMG_CRIT], 0.06f, 0.25f, 0); tk_snd("tachi_stun_start", k->sph[0], 0.25f, 8000);
			tk_voice("tachi_enemysuccess_", k->sph[0], 0.8f);
			tk_event(i, TKE_HACK_DONE, a);
		}
	}
}

// ---------------------------------------------------------------- brain
static int tk_target_alive(int a) { return a >= 0 && tk_w && a < tk_w->n_actors() && tk_w->alive(a); }
static V3 tk_target_chest(int a) { V3 p = tk_w->pos(a); return v3(p.x, p.y, p.z + 110); }
// ---- paths. perf (b35): a full search in one frame was up to ~1400 expansions on Underground and ~2100 on Berutalube (~1-2 ms
// on the Vita). tk_repath now asks for one and the tank drives on (its old path, or straight at the goal) while tk_nav_step
// runs the searches, at most TK_NAV_BUDGET expansions per frame in their own nav.h context (as bots.h's bnav_step), the
// longest-waiting ask first. TK_NAV_ASYNC 0: the old search in tk_repath.
#ifndef TK_NAV_ASYNC
#define TK_NAV_ASYNC 1
#endif
#define TK_NAV_BUDGET 300
static long long tk_nav_frame, tk_nav_paths, tk_nav_wait_sum; static int tk_nav_wait_max;   // stats: frames a path was pending
static void tk_path_set(Tank *k, int n) {   // the new path (n points in k->path; none: straight at the goal, as before)
	k->npath = n; k->ipath = 0; k->prog_d = 1e9f; k->prog_t = 0;
	if (!k->npath) { memcpy(k->path[0], k->goal, 12); k->npath = 1; }
}
static void tk_repath(Tank *k, const float *g) {
	memcpy(k->goal, g, 12); k->has_goal = 1;
#if TK_NAV_ASYNC
	k->repath_t = 5 + tk_frand() * 2;
	if (!nnav) { tk_path_set(k, 0); k->nav_pend = 0; return; }
	if (!k->nav_pend) k->nav_ask_f = tk_nav_frame;
	k->nav_pend = 1; k->nav_gen = ++tk_nav_serial;   // (a newer ask replaces one in flight)
#else
	float s[3] = { k->pl.pos.x, k->pl.pos.y, k->pl.pos.z };
	int n = nnav ? nav_path(s, g, k->path, 32, 90) : 0; k->repath_t = 5 + tk_frand() * 2; tk_path_set(k, n);
#endif
}
static void tk_nav_done(Tank *k, int n) {
	int w = (int)(tk_nav_frame - k->nav_ask_f); tk_nav_paths++; tk_nav_wait_sum += w; if (w > tk_nav_wait_max) tk_nav_wait_max = w;
	k->nav_pend = 0; tk_path_set(k, n);
}
static void tk_nav_step(void) {   // once per tk_update, before the tanks move
	tk_nav_frame++;
	int budget = TK_NAV_BUDGET, starts = 0;
	for (;;) {
		if (tk_nav_cur >= 0) { Tank *k = &tk[tk_nav_cur]; if (!k->nav_pend || k->nav_gen != tk_nav_cur_gen) tk_nav_cur = -1; }   // asked again / gone: start over
		if (tk_nav_cur < 0) {
			if (starts >= 2 || budget <= 0) return;
			int c = -1; for (int i = 0; i < TK_MAX; i++) if (tk[i].nav_pend && (c < 0 || tk[i].nav_gen < tk[c].nav_gen)) c = i;
			if (c < 0) return;
			Tank *k = &tk[c]; float s[3] = { k->pl.pos.x, k->pl.pos.y, k->pl.pos.z }; starts++;
			if (!nav_search_begin(&tk_nav, s, k->goal)) { tk_nav_done(k, 0); budget--; continue; }
			tk_nav_cur = c; tk_nav_cur_gen = k->nav_gen;
		}
		int e0 = tk_nav.expanded, st = nav_search_run(&tk_nav, e0 + budget); budget -= tk_nav.expanded - e0;   // (budget > 0 here)
		if (st == 1) return;   // spent: on with it next frame
		Tank *k = &tk[tk_nav_cur]; tk_nav_cur = -1;
		tk_nav_done(k, nav_search_path(&tk_nav, k->path, 32, 90)); budget--;
	}
}
static int (*tk_goal_fn)(int i, float *g);   // the mode's objective for tank i (Terminal Conquest: the active terminal); 0 = none
static void tk_think(int i) {
	Tank *k = &tk[i]; if (!tk_w) return;
	// targets: the nearest visible enemy actor (at most two sight rays)
	int n = tk_w->n_actors(), cand[NBOT_MAX + 1], nc = 0; float cd[NBOT_MAX + 1];
	for (int a = 0; a < n && nc < NBOT_MAX + 1; a++) { if (!tk_w->alive(a) || tk_actor_team(a) == k->team) continue;
		V3 q = vsub(tk_w->pos(a), k->pl.pos); float d = sqrtf(q.x * q.x + q.y * q.y); if (d > TK_SIGHT) continue;
		if (sk_camo_on(a) && d > 400) continue;   // therm-optic camo: tanks don't see a cloaked operator beyond 4 m (as bots)
		cd[nc] = d; cand[nc++] = a; }
	int best = -1, tests = 0;
	if (k->tgt >= 0 && tk_target_alive(k->tgt) && tk_actor_team(k->tgt) != k->team && bot_los(k->eye, tk_target_chest(k->tgt))) best = k->tgt;
	while (best < 0 && tests < 2) { int c = -1; float m = 1e9f; for (int j = 0; j < nc; j++) if (cd[j] < m) { m = cd[j]; c = j; } if (c < 0) break;
		int a = cand[c]; cd[c] = 1e9f; tests++; if (bot_los(k->eye, tk_target_chest(a))) best = a; }
	k->sees = best >= 0;
	if (best >= 0) { if (best != k->tgt) { k->see_t = 0; if (k->voice_cd <= 0) { tk_voice("tachi_detectenemy_", k->eye, 0.8f); k->voice_cd = 6; } } k->tgt = best; k->last_seen = tk_w->pos(best); k->lost_t = 0; k->noenemy_t = 0; }
	else if (k->tgt >= 0 && !tk_target_alive(k->tgt)) k->tgt = -1;
	// where to go: the mode's objective, else stay near the owner
	k->repath_t -= 0.2f;
	if (!k->sees && (k->repath_t <= 0 || (k->ipath >= k->npath && !(TK_NAV_ASYNC && k->nav_pend)))) {   // (perf: not while a path is pending)
		float g[3]; int have = tk_goal_fn && tk_goal_fn(i, g);
		if (!have && k->owner >= 0 && tk_w->alive(k->owner)) { V3 o = tk_w->pos(k->owner); V3 e = vsub(o, k->pl.pos);
			if (vdot(e, e) > 900.f * 900.f) { g[0] = o.x; g[1] = o.y; g[2] = o.z; have = 1; } }
		if (have) tk_repath(k, g); else { k->npath = 0; k->has_goal = 0; k->nav_pend = 0; }
	}
}

// ---------------------------------------------------------------- per frame
static void tk_fire_round(Tank *k, int i) {
	V3 from = k->muzzle, to = k->aim_pt; V3 d = vsub(to, from); float L = sqrtf(vdot(d, d)) + 1; d = vmul(d, 1 / L);
	float spread = (k->tgt == 0 ? 0.035f : 0.03f) + 0.05f * fmaxf(0, 1 - k->see_t);
	V3 side = v3(-d.y, d.x, 0); V3 dd = vadd(d, vadd(vmul(side, (tk_frand() - 0.5f) * 2 * spread), v3(0, 0, (tk_frand() - 0.5f) * spread * 1.4f)));
	float l2 = sqrtf(vdot(dd, dd)); dd = vmul(dd, 1 / l2);
	float hit_p = 0.55f - L / 9000; if (hit_p < 0.12f) hit_p = 0.12f;
	int mat = 0; V3 n; float wall = raycast(from, dd, L + 1500, &mat, &n);
	V3 end = vadd(from, vmul(dd, wall > 0 ? wall : L + 1500));
	if (tk_target_alive(k->tgt) && (wall < 0 || wall > L - 40) && tk_frand() < hit_p && !(tk_tut_mode && k->tgt == 0)) {   // (b86: the tutorial's tank only shoots near the player - EP14 7:11.7-7:12.5, sparks round him)
		end = tk_target_chest(k->tgt);
		if (tk_w->damage(k->tgt, TK_GUN_DMG * k->dmg_mul, k->owner >= 0 && tk_actor_team(k->owner) == k->team ? k->owner : -1, SKD_MELEE)) {   // (SKD_MELEE: the owner's notice reads "NICE KILL")
			tk_event(i, TKE_KILL, k->tgt); if (k->voice_cd <= 0) { tk_voice("tachi_killenemy_", k->eye, 0.8f); k->voice_cd = 5; } }
		else k->assist_by = k->tgt;
	} else if (wall > 0) { V3 hp = vadd(from, vmul(dd, wall)); fx_impact(mat, &hp.x, &n.x, &dd.x); }
	fx_muzzle_3p(&from.x, &dd.x); if (k->shot_cd <= 0) { fx_tracer(&from.x, &end.x); k->shot_cd = 0.05f; }
	if (k->tgt == 0) { V3 e = vsub(end, tk_pl ? v3(tk_pl->pos.x, tk_pl->pos.y, tk_pl->pos.z + 110) : end); if (vdot(e, e) < 220.f * 220.f && tk_frand() < 0.3f) ev_play(tk_frand() < 0.5f ? "impact/bullet_fly_h_tachi" : "impact/bullet_fly_l_tachi", NULL); }
}
static void tk_gren_fire(Tank *k, int i) {
	int g = -1; for (int j = 0; j < 6; j++) if (!tk_gren[j].on) { g = j; break; } if (g < 0) return;
	V3 from = k->muzzle; if (k->pose && tk_a_gren) { float m3[3]; model_bone_xform(&k->pose[tk_b_body], tk_a_gren, m3); from = tk_world(k, m3); }
	V3 to = k->last_seen; V3 d = vsub(to, from); float hd = sqrtf(d.x * d.x + d.y * d.y) + 1;
	float T = fmaxf(0.6f, hd / 1600.f); TkGren *G = &tk_gren[g]; memset(G, 0, sizeof(*G));
	G->on = 1; G->team = k->team; G->owner = k->owner; G->tank = i; G->p = from; G->v = v3(d.x / T, d.y / T, (d.z + 0.5f * 980 * T * T) / T);
	G->ser = ++tk_ser_n ? tk_ser_n : ++tk_ser_n; G->p0 = G->p; G->v0 = G->v;
	tk_snd("tachi_gl_fire", from, 0.55f, 12000); if (k->voice_cd <= 0) { tk_voice("tachi_missile_", k->eye, 0.7f); k->voice_cd = 6; }
	fx_muzzle_3p(&from.x, &d.x);
}
static void tk_gren_update(float dt) {
	for (int j = 0; j < 6; j++) { TkGren *G = &tk_gren[j]; if (!G->on) continue;
		G->t += dt; G->v.z -= 980 * dt; V3 st = vmul(G->v, dt); float L = sqrtf(vdot(st, st));
		int boom = G->t > 4; V3 hitp = G->p;
		if (L > 0.01f) { V3 d = vmul(st, 1 / L), n; float h = raycast(G->p, d, L + 4, NULL, &n); if (h > 0) { boom = 1; hitp = vadd(G->p, vmul(d, fmaxf(0, h - 4))); } else G->p = vadd(G->p, st); }
		if (!boom && tk_w) for (int a = 0; a < tk_w->n_actors(); a++) if (tk_w->alive(a) && tk_actor_team(a) != G->team) { V3 e = vsub(tk_target_chest(a), G->p); if (vdot(e, e) < 90.f * 90.f) { boom = 1; hitp = G->p; break; } }
		if (!boom) continue;
		G->on = 0; float up[3] = { 0, 0, 1 }; fx_explosion(&hitp.x, up); snd_gren_exp(&hitp.x);
		if (tk_w) for (int a = 0; a < tk_w->n_actors(); a++) { if (!tk_w->alive(a) || tk_actor_team(a) == G->team) continue;
			V3 e = vsub(tk_target_chest(a), hitp); float d = sqrtf(vdot(e, e)); if (d > TK_GREN_R || !bot_los(hitp, tk_target_chest(a))) continue;
			int own = G->owner >= 0 && tk_actor_team(G->owner) == G->team ? G->owner : -1;
			if (tk_w->damage(a, TK_GREN_DMG * (G->tank >= 0 && G->tank < TK_MAX ? tk[G->tank].gren_mul : 1) * (1 - 0.8f * d / TK_GREN_R), own, SKD_BLAST)) tk_event(G->tank, TKE_KILL, a); }
		tk_blast(hitp, TK_GREN_R, 120, G->owner);   // (other team's tanks only: tk_damage skips allies)
	}
}
// net (client): a tank of the host's, following its latest position / yaw, the gun's bursts as the host fires them (looks only)
static void tk_puppet(Tank *k, Model *M, float dt) {
	if (k->state != TK_LIVE) return;
	k->net_age += dt; V3 tg = vadd(k->net_pos, vmul(k->net_vel, fminf(k->net_age, 0.2f))), e = vsub(tg, k->pl.pos);
	if (vdot(e, e) > 300.f * 300.f) k->pl.pos = tg; else k->pl.pos = vadd(k->pl.pos, vmul(e, fminf(1, 10 * dt)));
	k->pl.vel = k->net_vel; { float ey = tk_wrap(k->net_yaw - k->pl.yaw); k->turn = ey > 0.03f ? 1 : ey < -0.03f ? -1 : 0; k->pl.yaw += ey * fminf(1, 10 * dt); }
	k->since_hit += dt; if (!k->def) k->open_t += dt;
	if (k->net_firing && !k->firing) { k->firing = 1; k->burst_t = 0; anim_play(&k->an, tk_cl[TC_FIRE_START], 0.1f, 0.05f, 0); tk_snd("tachi_fire_ready", k->muzzle, 0.35f, 9000); }
	else if (!k->net_firing && k->firing) { k->firing = 0; anim_play(&k->an, tk_cl[TC_FIRE_END], 0.1f, 0.2f, 0); k->base = TC_IDLE; anim_set_base(&k->an, tk_cl[TC_IDLE], 0.2f, 0); tk_snd("tachi_fire_end", k->muzzle, 0.38f, 9000); }
	float hs = sqrtf(k->pl.vel.x * k->pl.vel.x + k->pl.vel.y * k->pl.vel.y);
	if (k->firing) { k->an.rate = 1; k->burst_t += dt; float start = model_clip_len(M, tk_cl[TC_FIRE_START]) * 0.6f;
		if (k->burst_t > start) { if (k->base != TC_FIRE_LOOP) { k->base = TC_FIRE_LOOP; anim_set_base(&k->an, tk_cl[TC_FIRE_LOOP], 0.1f, 0); }
			float r0 = (k->burst_t - dt - start) * TK_GUN_RATE, r1 = (k->burst_t - start) * TK_GUN_RATE; if (r0 < 0) r0 = 0;
			for (int s = (int)r0; s < (int)r1; s++) { V3 from = k->muzzle, d = vsub(k->aim_pt, from); float L = sqrtf(vdot(d, d)) + 1; d = vmul(d, 1 / L);
				V3 side = v3(-d.y, d.x, 0), dd = vadd(d, vadd(vmul(side, (tk_frand() - 0.5f) * 0.07f), v3(0, 0, (tk_frand() - 0.5f) * 0.05f))); V3 n; int mat = 0;
				float w = raycast(from, dd, L + 1500, &mat, &n); V3 end = vadd(from, vmul(dd, w > 0 ? w : L)); if (w > 0 && w < L - 40) fx_impact(mat, &end.x, &n.x, &dd.x);
				fx_muzzle_3p(&from.x, &dd.x); if (k->shot_cd <= 0) { fx_tracer(&from.x, &end.x); k->shot_cd = 0.05f; } }
			if ((int)(k->burst_t * 2) != (int)((k->burst_t - dt) * 2)) tk_snd("tachi_fire_loop", k->muzzle, 0.32f, 14000); } }
	else { int b = hs > 420 ? TC_RUN : hs > 60 ? TC_WALK : k->turn ? (k->turn > 0 ? TC_TURN_L : TC_TURN_R) : TC_IDLE;
		if (b != k->base) { k->base = b; anim_set_base(&k->an, tk_cl[b], 0.25f, 0); }
		k->an.rate = b == TC_RUN ? fminf(1.4f, fmaxf(0.8f, hs / 560)) : b == TC_WALK ? fminf(1.5f, fmaxf(0.7f, hs / 260)) : 1; }
	if (hs > 60) { k->step_d += hs * dt; if (k->step_d > (hs > 420 ? 170 : 130)) { k->step_d = 0; tk_snd("tachi_foot_0", k->pl.pos, 0.2f, 5000); } }
	k->floor_z = k->pl.pos.z;
}
static void tk_update(float dt) {
	if (tk_ok <= 0) return;
	if (tk_targeted_t > 0) tk_targeted_t -= dt;
	tk_gren_update(dt);
	if (TK_NAV_ASYNC) tk_nav_step();   // perf (b35)
	for (int i = 0; i < TK_MAX; i++) {
		Tank *k = &tk[i]; if (k->state == TK_OFF) continue;
		Model *M = &tk_model[k->kind]; k->t += dt;
		if (k->hit_flash > 0) k->hit_flash -= dt * 7; if (k->dmg_snd_cd > 0) k->dmg_snd_cd -= dt; if (k->voice_cd > 0) k->voice_cd -= dt; if (k->shot_cd > 0) k->shot_cd -= dt;
		for (int h = 0; h < 4; h++) if (k->hit_t[h] > 0) k->hit_t[h] -= dt;
		k->done_t += dt; k->succ_t += dt;
		if (k->hack_hold_t > 0) { k->hack_hold_t -= dt; if (k->hack_hold_t <= 0) { k->hack_hold = 0; k->hold_actor = -1; } }
		float mx = 0, my = 0; int sprint = 0; float want_yaw = k->pl.yaw; int turning = 0;
		if (k->state == TK_DROP) {   // the call-in: falls, lands with a thud
			float f = k->t / TK_DROP_T; k->drop_z = 1400 * (1 - f * f); if (k->drop_z < 0) k->drop_z = 0;
			if (k->t >= TK_DROP_T) { k->state = TK_LIVE; k->t = 0; k->drop_z = 0; anim_play(&k->an, tk_cl[TC_LAND], 0.02f, 0.25f, 0); k->base = TC_IDLE; anim_set_base(&k->an, tk_cl[TC_IDLE], 0.2f, 0);
				tk_snd("tachi_land", k->pl.pos, 0.4f, 9000); { float up[3] = { 0, 0, 1 }; V3 g = v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 10); fx_impact_nodecal(3, &g.x, up, NULL); }
				tk_voice("d_tachi_spawn_", k->pl.pos, 0.9f); }
		} else if (k->state == TK_DEAD) {
			if (k->t > TK_WRECK_T) { tk_free_slot(k); continue; }
			if (!k->boom2 && k->t > 0.12f) { k->boom2 = 1; V3 c = tk_world(k, (const float[3]){ 0, 0, 200 }); float up[3] = { 0, 0, 1 }; fx_scale = 1.35f; fx_explosion(&c.x, up); fx_scale = 1; }
			if (tk_a_smoke && ((int)(k->t * 4) != (int)((k->t - dt) * 4))) { V3 s = tk_world(k, (const float[3]){ 0, 0, 120 }); float up[3] = { 0, 0, 1 }; fx_impact_nodecal(0, &s.x, up, NULL); }
		} else if (tk_mirror) tk_puppet(k, M, dt);   // net: a client shows the host's tank
		else {   // ---- alive
			tk_hack_step(i, dt);
			if (k->state != TK_LIVE) continue;
			k->since_hit += dt;
			if (!k->def && !(k->flags & TKF_NORESTORE) && (k->open_t += dt) >= TK_OPEN_T) {   // not destroyed in time: the defences come back
				k->def = 1; k->open_t = 0; tk_snd("tachi_stun_end", k->sph[0], 0.3f, 8000); tk_voice("tachi_teamsuccess_", k->sph[0], 0.6f); tk_event(i, TKE_RESTORED, k->last_hacker); }
			if (k->def && !(k->flags & TKF_NOREGEN) && k->since_hit > TK_REGEN_AFTER && k->hp < k->hpmax) { k->hp += TK_REGEN * dt; if (k->hp > k->hpmax) k->hp = k->hpmax; }
			if (!(k->flags & TKF_NOAI)) {
				k->think_t -= dt; if (k->think_t <= 0) { k->think_t = 0.2f + tk_frand() * 0.1f; tk_think(i); }
				int engage = k->tgt >= 0 && tk_target_alive(k->tgt) && tk_actor_team(k->tgt) != k->team;
				if (engage) {
					V3 tp = k->sees ? tk_target_chest(k->tgt) : v3(k->last_seen.x, k->last_seen.y, k->last_seen.z + 110);
					if (k->sees) { k->see_t += dt; k->aim_pt = tp; } else k->lost_t += dt;
					V3 e = vsub(tp, k->pl.pos); want_yaw = atan2f(e.y, e.x); float ey = tk_wrap(want_yaw - k->pl.yaw), hd = sqrtf(e.x * e.x + e.y * e.y);
					turning = fabsf(ey) > 0.15f;
					if (!k->sees && k->lost_t > 4) { k->tgt = -1; engage = 0; }
					else if (!k->sees || hd > TK_SIGHT * 0.8f) my = 0.55f;          // close in on where it was last seen
					// gatling: fire_start (spin-up), bursts in fire_loop, fire_end; the red aiming laser while engaged
					k->laser = fminf(1, k->laser + dt * 3);
					if (k->tgt == 0 && k->sees && !tk_tut_mode) tk_targeted_t = 0.4f;   // (b86: not in the tutorial - EP14 7:03-7:20 shows no TARGETED line)
					int can = k->sees && fabsf(ey) < 0.35f && k->see_t > 0.6f;
					if (k->pause_t > 0) k->pause_t -= dt;
					if (can && !k->firing && k->pause_t <= 0) { k->firing = 1; k->burst_t = 0; anim_play(&k->an, tk_cl[TC_FIRE_START], 0.1f, 0.05f, 0); tk_snd("tachi_fire_ready", k->muzzle, 0.35f, 9000);
						if (k->voice_cd <= 0 && tk_frand() < 0.4f) { tk_voice("tachi_gunshot_", k->eye, 0.7f); k->voice_cd = 6; } }
					if (k->firing) {
						k->burst_t += dt; float start = model_clip_len(M, tk_cl[TC_FIRE_START]) * 0.6f;
						if (k->burst_t > start) {
							if (k->base != TC_FIRE_LOOP) { k->base = TC_FIRE_LOOP; anim_set_base(&k->an, tk_cl[TC_FIRE_LOOP], 0.1f, 0); }
							float r0 = (k->burst_t - dt - start) * TK_GUN_RATE, r1 = (k->burst_t - start) * TK_GUN_RATE; if (r0 < 0) r0 = 0;
							for (int s = (int)r0; s < (int)r1; s++) tk_fire_round(k, i);
							if ((int)(k->burst_t * 2) != (int)((k->burst_t - dt) * 2)) tk_snd("tachi_fire_loop", k->muzzle, 0.32f, 14000);
						}
						if (k->burst_t > start + 1.6f + (i & 1) * 0.6f || !k->sees) { k->firing = 0; k->pause_t = 0.8f + tk_frand(); anim_play(&k->an, tk_cl[TC_FIRE_END], 0.1f, 0.2f, 0); k->base = TC_IDLE; anim_set_base(&k->an, tk_cl[TC_IDLE], 0.2f, 0); tk_snd("tachi_fire_end", k->muzzle, 0.38f, 9000); }
					}
					k->gren_cd -= dt;
					if (k->gren_cd <= 0 && k->sees && !k->firing && hd > 800 && hd < 3500) { k->gren_cd = TK_GREN_CD + tk_frand() * 4; anim_play(&k->an, tk_cl[TC_GREN], 0.1f, 0.2f, 0); tk_gren_fire(k, i); }
				}
				if (!engage) {
					k->laser = fmaxf(0, k->laser - dt * 3); if (k->firing) { k->firing = 0; k->base = TC_IDLE; anim_set_base(&k->an, tk_cl[TC_IDLE], 0.2f, 0); }
					k->noenemy_t += dt; if (k->noenemy_t > 25 && k->voice_cd <= 0) { tk_voice("tachi_noenemy_", k->eye, 0.6f); k->voice_cd = 30; k->noenemy_t = 0; }
					if (k->ipath < k->npath || (TK_NAV_ASYNC && k->nav_pend)) {
						int virt = k->ipath >= k->npath;   // perf (b35): the path is still being searched: straight at the goal meanwhile
						float *w = virt ? k->goal : k->path[k->ipath]; float dx = w[0] - k->pl.pos.x, dy = w[1] - k->pl.pos.y, d = sqrtf(dx * dx + dy * dy);
						if (d < 110) { if (!virt) k->ipath++; k->prog_d = 1e9f; k->prog_t = 0; }
						else { want_yaw = atan2f(dy, dx); float ey = tk_wrap(want_yaw - k->pl.yaw); turning = fabsf(ey) > 0.6f;
							my = fabsf(ey) < 0.6f ? 1 : 0; sprint = !virt && d > 900 && fabsf(ey) < 0.25f && k->ipath + 1 < k->npath;
							if (!virt) { k->prog_t += dt; if (d < k->prog_d - 25) { k->prog_d = d; k->prog_t = 0; }
								if (k->prog_t > 3) { k->ipath++; k->prog_d = 1e9f; k->prog_t = 0; } } }
						if (!virt && k->ipath >= k->npath) k->has_goal = 0;
					}
				}
			}
			// b86 (TKF_LEASH: the tutorial's arena, v2.0 EP14 7:06-7:16): between bursts it side-steps while it fights, and turns
			// round at the edge of the arena; out of a fight it walks back towards where it was spawned
			if ((k->flags & TKF_LEASH) && !(k->flags & TKF_NOAI)) {
				V3 hv = vsub(k->pl.pos, k->home); float hd2 = hv.x * hv.x + hv.y * hv.y;
				if (k->tgt >= 0 && k->sees) {
					if ((k->strafe_t -= dt) <= 0) { k->strafe = -k->strafe; k->strafe_t = 2.5f + tk_frand() * 3; }
					my = 0; mx = k->firing ? 0 : 0.5f * k->strafe;
				} else if (hd2 > 120.f * 120.f) { want_yaw = atan2f(-hv.y, -hv.x); turning = fabsf(tk_wrap(want_yaw - k->pl.yaw)) > 0.6f; my = turning ? 0 : 0.6f; mx = 0; }
				else { mx = my = 0; sprint = 0; }
				float cy = cosf(k->pl.yaw), sy = sinf(k->pl.yaw), wx = cy * my + sy * mx, wy = sy * my - cy * mx;   // this frame's step (player.h: rgt = (sin, -cos))
				if (hd2 > TK_LEASH_R * TK_LEASH_R && wx * hv.x + wy * hv.y > 0) { if (mx != 0) { k->strafe = -k->strafe; k->strafe_t = 2 + tk_frand() * 2; mx = -mx; } else my = 0; }
			}
			// turning: ~110 deg/s
			{ float ey = tk_wrap(want_yaw - k->pl.yaw), st = 1.9f * dt; k->turn = ey > st ? 1 : ey < -st ? -1 : 0; k->pl.yaw += ey > st ? st : ey < -st ? -st : ey; }
			k->pl.speed_mul = (sprint ? 1.05f : 0.78f) * k->spd_mul;   // (b74: the legs' MAX SPEED / 700)
			player_update(&k->pl, mx, my, sprint, 0, dt);
			if (k->pl.pos.z < col_zmin - 500) { k->hp = 1; tk_damage(i, 1e6f, -1, k->pl.pos, v3(0, 0, 1), SKD_BLAST); continue; }
			// animation: idle / walk / run / turning in place; the gun's loop while firing
			float hs = sqrtf(k->pl.vel.x * k->pl.vel.x + k->pl.vel.y * k->pl.vel.y);
			if (!k->firing) {
				int b = hs > 420 ? TC_RUN : hs > 60 ? TC_WALK : (turning && k->turn) ? (k->turn > 0 ? TC_TURN_L : TC_TURN_R) : TC_IDLE;
				if (b == TC_WALK && fabsf(mx) > 0.2f && fabsf(my) < 0.2f) b = mx > 0 ? TC_WALK_R : TC_WALK_L;   // b86: the side step
				if (b != k->base) { k->base = b; anim_set_base(&k->an, tk_cl[b], 0.25f, 0); }
				k->an.rate = b == TC_RUN ? fminf(1.4f, fmaxf(0.8f, hs / 560)) : (b == TC_WALK || b == TC_WALK_L || b == TC_WALK_R) ? fminf(1.5f, fmaxf(0.7f, hs / 260)) : 1;
			} else k->an.rate = 1;
			if (k->pl.grounded && hs > 60) { k->step_d += hs * dt; if (k->step_d > (hs > 420 ? 170 : 130)) { k->step_d = 0; tk_snd("tachi_foot_0", k->pl.pos, 0.2f, 5000); } }
			if (k->pl.grounded) k->floor_z = k->pl.pos.z;
		}
		anim_update(&k->an, dt);
		k->pose = anim_pose(&k->an, -1);
		// world points (body spheres for hits, muzzle, eye)
		const MBone *P = k->pose; float c0[3], c1[3], c2[3];
		for (int q = 0; q < 3; q++) { c0[q] = P[tk_b_body].p[q]; c1[q] = P[tk_b_tail].p[q]; }
		c0[1] -= 10; c0[2] += 30; c1[1] += 45; c1[2] += 38; memcpy(c2, P[tk_b_body].p, 12); c2[1] -= 78; c2[2] += 25;
		k->sph[0] = tk_world(k, c0); k->sph[1] = tk_world(k, c1); k->sph[2] = tk_world(k, c2);
		{ float m3[3]; model_bone_xform(&P[tk_b_body], tk_a_muzzle, m3); k->muzzle = tk_world(k, m3); }
		{ float m3[3]; model_bone_xform(&P[tk_b_body], tk_a_eye ? tk_a_eye : (const float[3]){ 0, -40, 60 }, m3); k->eye = tk_world(k, m3); }
		{ float L[3]; light_at(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 40, L); float a = fminf(1, 3 * dt); for (int c = 0; c < 3; c++) k->lit[c] += (L[c] - k->lit[c]) * a; }
		// highlight state for the outline pass (vision): enemy / ally, defences down
		k->hl = (k->state == TK_LIVE || k->state == TK_DROP) ? ((k->team == tk_local_team ? TK_HL_ALLY : TK_HL_ENEMY) | (k->def ? 0 : TK_HL_OPEN)) : 0;
	}
	// keep operators out of the tanks' bodies (bots and the local player)
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state != TK_LIVE) continue;
		for (int j = -1; j < nbot; j++) { Player *p = j < 0 ? tk_pl : &bots[j].pl; if (!p || (j >= 0 && bots[j].state == BS_DEAD)) continue;
			float dx = p->pos.x - k->pl.pos.x, dy = p->pos.y - k->pl.pos.y, d2 = dx * dx + dy * dy;
			if (d2 < 150.f * 150.f && d2 > 1 && fabsf(p->pos.z - k->pl.pos.z) < 200) { float d = sqrtf(d2), push = (150 - d) * 0.5f; V3 np = v3(p->pos.x + dx / d * push, p->pos.y + dy / d * push, p->pos.z);
				if (!body_blocked(np, P_HEIGHT * 0.9f, 1.0f)) p->pos = np; } } }
}

// ---------------------------------------------------------------- drawing
// the model once more, additive, with the white texture (its own textures are near black, a tint alone would not show);
// reuses the skinning of the last chr_draw of this CSkin
// b86: mul = 1 multiplies what is behind it by the colour (a see-through tinted body: the hacked look), reskin = 1 skins it
// this frame (when the opaque draw is skipped)
static void tk_draw_flat_ex(Model *M, CSkin *cs, const float *MVP, const MBone *pose, const float *ld, const float *col, int mul, int reskin) {
	const float nofog[4] = { 0, 0, 0, 0 }; GLuint keep[8]; int nt = M->ntex < 8 ? M->ntex : 8;
	for (int t = 0; t < nt; t++) { keep[t] = M->tex[t]; if (tk_white) M->tex[t] = tk_white; }
	glEnable(GL_BLEND); if (mul) glBlendFunc(GL_DST_COLOR, GL_ZERO); else glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
	chr_draw(M, cs, 0, MVP, pose, ld, col, nofog, reskin);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for (int t = 0; t < nt; t++) M->tex[t] = keep[t];
}
static void tk_draw_flat(Model *M, CSkin *cs, const float *MVP, const MBone *pose, const float *ld, const float *col) { tk_draw_flat_ex(M, cs, MVP, pose, ld, col, 0, 0); }
// a hologram of a tank (the call-in placement): kind 0 blue / 1 red model, at pos facing yaw, idle pose; col = colour (alpha = strength)
static MBone *tk_holo_pose; static Anim tk_holo_an; static CSkin tk_holo_cs; static int tk_holo_kind = -1;
static void tk_holo_free(void) { if (tk_holo_an.m) anim_free(&tk_holo_an); if (tk_holo_cs.ok) cskin_free(&tk_holo_cs); tk_holo_kind = -1; }
static void tk_draw_holo(const float *VP, int kind, V3 pos, float yaw, const float *col) {
	if (tk_ok <= 0) return; Model *M = &tk_model[kind & 1];
	if (tk_holo_kind != (kind & 1)) { if (tk_holo_an.m) anim_free(&tk_holo_an); if (tk_holo_cs.ok) cskin_free(&tk_holo_cs);
		anim_init(&tk_holo_an, M); cskin_init(&tk_holo_cs, M); anim_set_base(&tk_holo_an, tk_cl[TC_IDLE], 0, 0); tk_holo_kind = kind & 1; }
	anim_update(&tk_holo_an, 1 / 60.f); tk_holo_pose = anim_pose(&tk_holo_an, -1);
	float th = yaw + 1.5707963f, c = cosf(th), s = sinf(th), Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, pos.x, pos.y, pos.z, 1 }, MVP[16];
	for (int cc = 0; cc < 4; cc++) for (int r = 0; r < 4; r++) { float a = 0; for (int q = 0; q < 4; q++) a += VP[q * 4 + r] * Mw[cc * 4 + q]; MVP[cc * 4 + r] = a; }
	{ const float hc[4] = { col[0], col[1], col[2], fminf(1, col[3] * 2.2f) }; if (tk_vis_model && tk_vis_model(M, VP, Mw, tk_holo_pose, hc, 0)) { glEnable(GL_CULL_FACE); return; } }   // vision.h's hologram look (GPU skinned)
	const float ld[4] = { 0.3f, -0.6f, 0.75f, 0 }, nofog[4] = { 0, 0, 0, 0 }; GLuint keep[8]; int nt = M->ntex < 8 ? M->ntex : 8;
	for (int t = 0; t < nt; t++) { keep[t] = M->tex[t]; if (tk_white) M->tex[t] = tk_white; }
	glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE);
	chr_draw(M, &tk_holo_cs, 0, MVP, tk_holo_pose, ld, col, nofog, 1);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
	for (int t = 0; t < nt; t++) M->tex[t] = keep[t];
}
static void tk_world_mat(const Tank *k, float *M) {   // model -> world (column-major)
	float th = k->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	const float W[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + (k->state == TK_DROP ? k->drop_z : 0), 1 }; memcpy(M, W, 64);
}
static int tk_draw_info(int i, const float *VP, float *mvp, const Model **m, CSkin **sc, const MBone **pose) {
	Tank *k = &tk[i]; if (k->state == TK_OFF || !k->pose) return 0;
	float M[16]; tk_world_mat(k, M);
	for (int col = 0; col < 4; col++) for (int r = 0; r < 4; r++) { float a = 0; for (int q = 0; q < 4; q++) a += VP[q * 4 + r] * M[col * 4 + q]; mvp[col * 4 + r] = a; }
	if (m) *m = &tk_model[k->kind]; if (sc) *sc = &k->cs; if (pose) *pose = k->pose; return 1;
}
// vision: tank i once more in a flat additive colour (col: rgb + strength), with the current depth test (a through-wall
// silhouette: glDepthFunc(GL_GREATER) around the call); call after tk_draw (it reuses that frame's skinning)
__attribute__((unused)) static void tk_draw_body(int i, const float *VP, const float *col) {
	float MVP[16]; const Model *M; CSkin *cs; const MBone *pose; if (tk_ok <= 0 || !tk_draw_info(i, VP, MVP, &M, &cs, &pose)) return;   // (b74: each model's skinning checked below)
	const float ld[4] = { 0, 0, 1, 0 }; glDisable(GL_CULL_FACE); Model *ms[1 + TKP_NS]; CSkin *css[1 + TKP_NS]; int nm = tk_models(&tk[i], ms, css); (void)M; (void)cs;
	for (int q = 0; q < nm; q++) if (css[q]->ok && css[q]->cur >= 0) tk_draw_flat(ms[q], css[q], MVP, pose, ld, col); glEnable(GL_CULL_FACE);
}
// a camera-facing ribbon a -> b about w0 * 1.5 px wide on screen at any distance (sprite.h batch; flush with tk_white)
static void tk_line3(V3 a, V3 b, float w0, const float *col) {
	V3 d = vsub(b, a); float L = sqrtf(vdot(d, d)); if (L < 10) return; d = vmul(d, 1 / L);
	V3 cam = lis_pos, mid = vmul(vadd(a, b), 0.5f), tv = vsub(cam, mid), w = vcross(d, tv); float wl = sqrtf(vdot(w, w)); if (wl < 1e-3f) return;
	V3 ea = vsub(a, cam), eb = vsub(b, cam); w = vmul(w, 1 / wl);
	V3 wa = vmul(w, w0 * fmaxf(0.5f, 0.0011f * sqrtf(vdot(ea, ea)))), wb = vmul(w, w0 * fmaxf(0.5f, 0.0011f * sqrtf(vdot(eb, eb))));
	V3 p0 = vsub(a, wa), p1 = vadd(a, wa), p2 = vadd(b, wb), p3 = vsub(b, wb);
	sprite_quad(&p0.x, &p1.x, &p2.x, &p3.x, 0, 0, 1, 1, col);
}
// b86: the hacked hologram's drop-outs (EP14 7:10.9-7:11.3: ~0.4 s every few seconds, flickering in and out): 1 on, ~0.1 off
static float tk_glitch(float t) {
	float u = t * 0.27f, f = u - floorf(u);
	if (f > 0.885f) return 0.1f;
	if (f > 0.86f || f < 0.012f) return ((int)(t * 28) & 1) ? 1 : 0.25f;
	return 1;
}
static void tk_draw(const float *VP, const float *fog) {
	if (tk_ok <= 0) return;
	// blob shadows
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state == TK_OFF || !k->pose) continue;
		V3 d = vsub(k->pl.pos, lis_pos); if (vdot(d, d) > 5000.f * 5000.f) continue;
		float lum = (k->lit[0] + k->lit[1] + k->lit[2]) / 3, a = 0.55f * fminf(1, 0.35f + lum) * (k->state == TK_DROP ? fmaxf(0.2f, 1 - k->drop_z / 1400) : 1);
		const float col[4] = { 1, 1, 1, a }; sprite_ground(k->pl.pos.x, k->pl.pos.y, k->floor_z + 3, 175, 165, k->pl.yaw, col); }
	sprite_flush(VP, sp_blob, 0);
	// b86: a red mark on the floor under a tank being hacked (EP14 7:06-7:13: a glowing red disc, a ring round it)
	{ int any = 0; for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state != TK_LIVE || k->hacker < 0 || !k->pose) continue;
		float a = fminf(1, k->hack_t / 0.6f) * (0.8f + 0.2f * sinf(k->t * 6)); const float c[4] = { 1, 0.12f, 0.08f, 0.85f * a };
		sprite_ground(k->pl.pos.x, k->pl.pos.y, k->floor_z + 4, 120, 120, 0, c); any = 1; }
	  if (any) sprite_flush(VP, tk_ringt, 1);
	  for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state != TK_LIVE || k->hacker < 0 || !k->pose) continue;
		float a = fminf(1, k->hack_t / 0.6f); const float c[4] = { 1, 0.15f, 0.1f, 0.7f * a }; sprite_ground(k->pl.pos.x, k->pl.pos.y, k->floor_z + 4, 70, 50, k->pl.yaw, c); }
	  if (any) sprite_flush(VP, tk_soft, 1); }
	glDisable(GL_CULL_FACE);
	for (int i = 0; i < TK_MAX; i++) {
		Tank *k = &tk[i]; float MVP[16]; const Model *M; CSkin *sc; const MBone *pose;
		if (!tk_draw_info(i, VP, MVP, &M, &sc, &pose)) continue;
		V3 d = vsub(k->pl.pos, lis_pos); float d2 = vdot(d, d); if (d2 > 9000.f * 9000.f) continue;
		float cx = k->pl.pos.x, cy = k->pl.pos.y, cz = k->pl.pos.z + 100 + (k->state == TK_DROP ? k->drop_z : 0), q[4];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * cx + VP[4 + r] * cy + VP[8 + r] * cz + VP[12 + r];
		if (q[3] < -250 || q[0] > q[3] + 400 || q[0] < -q[3] - 400 || q[1] > q[3] + 450 || q[1] < -q[3] - 450) continue;
		float th = k->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th); const float Lw[3] = { 0.33f, 0.42f, 0.84f };
		float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
		float tint[4] = { 1, 1, 1, 1 }; for (int cc = 0; cc < 3; cc++) tint[cc] = fminf(1.5f, fmaxf(0.2f, k->lit[cc] * 1.15f)) * (k->state == TK_DEAD ? 0.55f : 1);
		// b86 (v2.0 EP14): while it is being hacked it shows as a red, see-through hologram (7:06-7:14, with short drop-outs,
		// 7:10.9-7:11.3); every hit flashes the whole body bright red for a moment (7:16-7:20: one frame per pistol shot);
		// with its defences down it looks as usual (7:14.5-7:20.4: dark, red eyes) - before: a steady red glow while open
		float hf = k->state == TK_LIVE && k->hit_flash > 0 ? fminf(1, k->hit_flash) : 0, hk = 0;
		if (k->state == TK_LIVE && k->hacker >= 0) hk = fminf(1, k->hack_t / 1.2f) * tk_glitch(k->t + i * 1.7f);
		int see = hk > 0.5f && hf < 0.3f;   // the hacked body: no opaque draw - it tints what is behind it a deep red (footage 7:07.5: the floor lines show through)
		if (hf > 0) { tint[0] *= 1 - 0.2f * hf; tint[1] *= 1 - 0.85f * hf; tint[2] *= 1 - 0.85f * hf; }   // a hit: the body under the red, red-dark (not pink)
		int reskin = d2 < 3000.f * 3000.f || ((bot_frame + i) & 1);
		k->seen_frame = bot_frame;
		Model *ms[1 + TKP_NS]; CSkin *css[1 + TKP_NS]; int nm = tk_models(k, ms, css); (void)M; (void)sc;   // b74: the player's own tank: the core + its parts
		if (see) {   // b87 (device b86-3: 25.7 ms GPU / 39 fps up close - every layer of the body, both faces, blended twice): its depth
			         // first (no colour), then the red multiplies what is behind its nearest surface only, the hologram over that one
			         // surface - the scene still shows through, a deep red (one layer: x0.62 / 0.2 / 0.18)
			const float mulc[4] = { 0.62f, 0.2f, 0.18f, 1 };
			glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
			for (int q = 0; q < nm; q++) chr_draw(ms[q], css[q], 0, MVP, pose, ld, tint, fog, reskin);
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_TRUE);
			reskin = 0;   // (emu b86-1: 0.95 / 0.42 / 0.4 + 0.16 glow read flat bright red over the arena's light floor; b86-1 re-run: 0.75 / 0.32 / 0.3 see-through but brighter than the footage's maroon)
			for (int q = 0; q < nm; q++) { tk_draw_flat_ex(ms[q], css[q], MVP, pose, ld, mulc, 1, reskin);
				tk_stat_skinned += reskin ? ms[q]->mesh[0].nv : 0; }
		} else {
			// b86: the black (red-eyed) tank keeps dark under bright lights - EP14 7:11.1 / 7:21 (the tutorial's arena): near black
			// with a soft sheen; the model reflection at full strength made it silver there (emu b86-1)
			float env0 = md_env, spec0 = md_spec; if (k->kind == 1) { md_env = 0.25f; md_spec = 0.6f; }
			for (int q = 0; q < nm; q++) { chr_draw(ms[q], css[q], 0, MVP, pose, ld, tint, fog, reskin);   // chr.h: NEON skinning into 16-byte vertices (or the GPU when chrskin.txt says so), as the bots
				tk_stat_skinned += reskin ? ms[q]->mesh[0].nv : 0; }
			md_env = env0; md_spec = spec0; }
		float ra = see ? 0.25f * hk : 0.5f * hk;   // (emu b86-1: the hologram's fill at full strength over the multiplied body read flat bright red; footage 7:07.5: a deep, see-through red)
		if (ra > 0.02f) {   // the red hologram (vision.h: translucent fill + bright rim; flat additive red without it) - depth-tested: its nearest surface
			float Mw[16]; const float red[4] = { 1.0f, 0.1f, 0.08f, 0.7f * ra }, red_h[4] = { 1.0f, 0.1f, 0.07f, ra };
			tk_world_mat(k, Mw);
			for (int q = 0; q < nm; q++) if (!tk_vis_model || !tk_vis_model(ms[q], VP, Mw, pose, red_h, 0)) tk_draw_flat(ms[q], css[q], MVP, pose, ld, red);
			glDisable(GL_CULL_FACE);
		}
		if (hf > 0.02f) {   // a hit: one red wash over the darkened body (EP14 7:16-7:20; b87: the wash alone - no hologram pass for hits)
			const float wash[4] = { 1.0f, 0.16f, 0.1f, 0.7f * hf };
			for (int q = 0; q < nm; q++) tk_draw_flat(ms[q], css[q], MVP, pose, ld, wash); }
		if (k->state == TK_LIVE && k->team == tk_local_team && tk_vis_model) { float Mw[16]; const float teal[4] = { 0.47f, 0.86f, 0.90f, 1 }; tk_world_mat(k, Mw);
			for (int q = 0; q < nm; q++) tk_vis_model(ms[q], VP, Mw, pose, teal, 1); glDisable(GL_CULL_FACE); }   // ours behind cover: the teammates' outline
	}
	glEnable(GL_CULL_FACE);
	// the red aiming lasers (eye -> target; ~1.5 px wide at any distance) and grenades in flight (a hot dot), additive
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state != TK_LIVE || k->laser <= 0.02f || !k->sees) continue;
		const float col[4] = { 1, 0.1f, 0.08f, 0.7f * k->laser }; tk_line3(k->eye, k->aim_pt, 1.0f, col); }
	sprite_flush(VP, tk_white, 1);
	for (int j = 0; j < 6; j++) { TkGren *G = &tk_gren[j]; if (!G->on) continue; const float col[4] = { 1, 0.75f, 0.45f, 1 };
		V3 p0 = v3(G->p.x - 14, G->p.y, G->p.z - 14), p1 = v3(G->p.x + 14, G->p.y, G->p.z - 14), p2 = v3(G->p.x + 14, G->p.y, G->p.z + 14), p3 = v3(G->p.x - 14, G->p.y, G->p.z + 14);
		sprite_quad(&p0.x, &p1.x, &p2.x, &p3.x, 0, 0, 1, 1, col); p0 = v3(G->p.x, G->p.y - 14, G->p.z - 14); p1 = v3(G->p.x, G->p.y + 14, G->p.z - 14); p2 = v3(G->p.x, G->p.y + 14, G->p.z + 14); p3 = v3(G->p.x, G->p.y - 14, G->p.z + 14);
		sprite_quad(&p0.x, &p1.x, &p2.x, &p3.x, 0, 0, 1, 1, col); }
	sprite_flush(VP, tk_soft, 1);
}

// ---------------------------------------------------------------- the local player's in-world HUD bits
static float tk_use_prog = -1;   // the local player's tk_use result this frame (set by the game)
// two looks, as the two sources show them: 0 = Terminal Conquest (match footage: "Hack Think Tank" on a grey band under the
// crosshair, the encryption code under the ammo at the bottom), 1 = the Early Access tutorial ("Hacking Think Tank" on a red
// band, "Bypassing encryption..." mid-screen with "Complete hacking to shut down defensive protocols.")
static int tk_hud_style;
// HUD primitives (hud.h, the main pack's white image). Text sizes follow the Vita legibility rule (BRIEF.md): labels >= 24 px,
// fine print >= 20 px at 960x544; positions from the 720p footage x 0.75, re-flowed where the bigger text needs room.
static void tk_rect(float x0, float y0, float x1, float y1, const float *c) { float A[6] = { x1 - x0, 0, x0, 0, y1 - y0, y0 }; hud_img(HG.white, HG.screen, A, c, HUD_Z4, -1, 1); }
static void tk_quad(const float (*q)[2], const float *c) {
	const HImg *I = &himg[HG.white]; float u = (I->x + 2) / (float)htw[I->tex], v = (I->y + 2) / (float)hth[I->tex], uv[4][2] = { { u, v }, { u, v }, { u, v }, { u, v } };
	hud_poly(htex[I->tex], HG.screen, q, (const float (*)[2])uv, 4, c);
}
static void tk_ring(float cx, float cy, float r0, float r1, const float *c) {
	for (int k = 0; k < 24; k++) { float a0 = k * 6.2831853f / 24, a1 = (k + 1) * 6.2831853f / 24;
		float q[4][2] = { { cx + r1 * cosf(a0), cy + r1 * sinf(a0) }, { cx + r1 * cosf(a1), cy + r1 * sinf(a1) }, { cx + r0 * cosf(a1), cy + r0 * sinf(a1) }, { cx + r0 * cosf(a0), cy + r0 * sinf(a0) } };
		tk_quad((const float (*)[2])q, c); }
}
static void tk_frame_box(float x0, float y0, float x1, float y1, float w, const float *c) { tk_rect(x0, y0, x1, y0 + w, c); tk_rect(x0, y1 - w, x1, y1, c); tk_rect(x0, y0, x0 + w, y1, c); tk_rect(x1 - w, y0, x1, y1, c); }
static void tk_btn_square(float x0, float y0, float s) {   // the Square button (the original shows its key, [E] / [R])
	vp_glyph(VP_SQUARE, x0 + s * 0.5f, y0 + s * 0.5f, s / 1.2f, 1);   // ui agent: the round Vita button (vita_prompt.h), not a key cap
}
// a Vita button as a labelled plate (Vita prompts rule: SELECT, R, L, START): dark plate, light rim, the label at >= 20 px;
// x0 = left edge, cy = centre line; returns the width. R / L get the shoulder button's rounded outer corner (a cut).
static float tk_btn_plate(float x0, float cy, const char *label, float ts) {
	const float bk[4] = { 0.02f, 0.05f, 0.06f, 0.88f }, rim[4] = { 0.70f, 0.80f, 0.83f, 1 }, wm[4] = { 1, 1, 1, 0.97f };
	float h = ts * 1.3f, w = hud_text_w(label) * ts + ts * 0.9f, y0 = cy - h / 2, y1 = cy + h / 2, cut = (label[0] == 'R' || label[0] == 'L') && !label[1] ? h * 0.45f : 0;
	int rgt = label[0] == 'R';
	if (cut > 0) { float q[4][2] = { { x0 + (rgt ? 0 : cut), y0 }, { x0 + w - (rgt ? cut : 0), y0 }, { x0 + w, y0 + (rgt ? cut : 0) }, { x0, y0 + (rgt ? 0 : cut) } }; tk_quad((const float (*)[2])q, bk);
		tk_rect(x0, y0 + cut, x0 + w, y1, bk);
		float e[4][2] = { { x0 + (rgt ? w - cut : cut), y0 }, { x0 + (rgt ? w : 0), y0 + cut }, { x0 + (rgt ? w : 0) + (rgt ? -1.5f : 1.5f), y0 + cut + 1.2f }, { x0 + (rgt ? w - cut : cut) + (rgt ? -1 : 1), y0 + 1.5f } }; tk_quad((const float (*)[2])e, rim);
		tk_rect(rgt ? x0 : x0 + cut, y0, rgt ? x0 + w - cut : x0 + w, y0 + 1.5f, rim); tk_rect(x0, y1 - 1.5f, x0 + w, y1, rim);
		tk_rect(x0, rgt ? y0 : y0 + cut, x0 + 1.5f, y1, rim); tk_rect(x0 + w - 1.5f, rgt ? y0 + cut : y0, x0 + w, y1, rim); }
	else { tk_rect(x0, y0, x0 + w, y1, bk); tk_frame_box(x0, y0, x0 + w, y1, 1.5f, rim); }
	hud_text(HG.screen, x0 + w / 2, cy + ts * 0.36f, ts, 2, wm, HUD_Z4, label);
	return w;
}
// a band that fades out at both ends (the original's text bands): x0..x1 solid, fade px on each side, 10 steps
static void tk_band(float x0, float x1, float y0, float y1, float fade, const float *c) {
	tk_rect(x0, y0, x1, y1, c);
	for (int e = 0; e < 10; e++) { float a = c[3] * (1 - (e + 0.5f) / 10.f), cc[4] = { c[0], c[1], c[2], a }, w = fade / 10;
		tk_rect(x0 - w * (e + 1), y0, x0 - w * e, y1, cc); tk_rect(x1 + w * e, y0, x1 + w * (e + 1), y1, cc); }
}
static int tk_screen(const float *M, V3 p, float *sx, float *sy) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = M[r] * p.x + M[4 + r] * p.y + M[8 + r] * p.z + M[12 + r];
	if (q[3] < 10) return 0; *sx = (q[0] / q[3] + 1) * 480; *sy = (1 - q[1] / q[3]) * 272; return 1;
}
// the hack's 10 digits: the revealed ones settle (the newest two still roll), the rest are "?"; cell w x h at x0, y0 (text size ts)
static void tk_code_digits(const Tank *k, float now, float x0, float y0, float cw, float ch, float ts) {
	const float wm[4] = { 1, 1, 1, 0.97f }, qm[4] = { 0.62f, 0.62f, 0.64f, 0.95f };
	float f = k->hacker >= 0 ? k->hack_t / TK_HACK_RUN : 1; int shown = f >= 1 ? 10 : 1 + (int)(f * 10); if (shown > 10) shown = 10;
	const float gap = 2; tk_rect(x0 - 3, y0 - 3, x0 + 10 * cw + 9 * gap + 3, y0 + ch + 3, (const float[4]){ 0, 0, 0, 0.85f });
	unsigned v = k->code[0] ^ (k->code[1] << 7), roll = (unsigned)(now * 14) * 2654435761u;
	for (int d = 0; d < 10; d++) {
		float cx = x0 + d * (cw + gap); int lock = d < shown - 2 || f >= 1, vis = d < shown;
		char c[2] = { vis ? (char)('0' + ((lock ? v : roll >> (d * 3)) % 10)) : '?', 0 }; v = v / 10 + (unsigned)d * 7919u;
		tk_rect(cx, y0, cx + cw, y0 + ch, vis ? (const float[4]){ 0.1f, 0.1f, 0.11f, 0.95f } : (const float[4]){ 0.06f, 0.06f, 0.07f, 0.95f });
		hud_text(HG.screen, cx + cw / 2, y0 + ch * 0.5f + ts * 0.36f, ts, 2, vis ? wm : qm, HUD_Z4, c); }
}
// b86: the v2.0 digits (EP14 7:05-7:14 zoomed, EP15 11:31-11:36): the revealed ones bold white on the panel's dark ground, the
// newest two still rolling; the rest "?" in grey on darker cells
static void tk_code_digits_v2(const Tank *k, float now, float x0, float y0, float cw, float ch, float ts) {
	const float wm[4] = { 1, 1, 1, 1 }, qm[4] = { 0.58f, 0.6f, 0.62f, 0.95f }, cell[4] = { 0.15f, 0.16f, 0.17f, 0.96f };
	float f = k->hacker >= 0 ? k->hack_t / TK_HACK_RUN : 1; int shown = f >= 1 ? 10 : 1 + (int)(f * 10); if (shown > 10) shown = 10;
	const float gap = 2; unsigned v = k->code[0] ^ (k->code[1] << 7), roll = (unsigned)(now * 14) * 2654435761u;
	for (int d = 0; d < 10; d++) {
		float cx = x0 + d * (cw + gap); int lock = d < shown - 2 || (f >= 1 && d < 9), vis = d < shown;   // (the last one rolls until the panel goes: 7:13.7-7:14.4)
		char c[2] = { vis ? (char)('0' + ((lock ? v : roll >> (d * 3)) % 10)) : '?', 0 }; v = v / 10 + (unsigned)d * 7919u;
		if (!vis) tk_rect(cx, y0, cx + cw, y0 + ch, cell);
		hud_text(HG.screen, cx + cw / 2, y0 + ch * 0.5f + ts * 0.36f, ts, 2, vis ? wm : qm, HUD_Z4, c); }
}
static void tk_code_panel(const Tank *k, float now) {
	const float wm[4] = { 1, 1, 1, 0.97f }, pk[4] = { 1, 0.72f, 0.7f, 0.97f }, red[4] = { 0.55f, 0.04f, 0.06f, 0.82f };
	if (tk_hud_style == 1) {   // EA tutorial: mid-screen, "Bypassing encryption..." over the digits, the line under them
		// b63 (device b62-2, tutorial: the panel over the ammo counter at the gun): it claims its box (BANNER): the counter
		// (hud_game.h) gives way to it from the next frame
		hud_claim(300, 330, 660, 432, HUD_P_BANNER);
		tk_band(330, 630, 330, 398, 60, red); tk_rect(330, 330, 630, 336, (const float[4]){ 0.85f, 0.25f, 0.25f, 0.5f });
		hud_text(HG.screen, 480, 355, 24, 2, pk, HUD_Z4, "Bypassing encryption...");
		tk_code_digits(k, now, 351, 361, 24, 32, 28);
		hud_text(HG.screen, 480, 424, 22, 2, wm, HUD_Z4, "Complete hacking to shut down defensive protocols.");
		return;
	}
	// b86 v2.0 (the tutorial EP14 7:04.6-7:14.5 and Terminal Conquest EP15 11:30.5-11:36, both alike): at the bottom centre a
	// dark framed box with the 10 digits over an orange-red band "Bypassing encryption..." - nothing else (no "HACK DETECTED",
	// no line under it; before: the 2016-17 Berutalube look under the crosshair). 720p 558-720 x 666-708 x 1.6 for the Vita's
	// legibility (digits 30 px, the band's text 20 px); the ammo counter under the sight (aiming) gives way to it (claims)
	const float fr[4] = { 0.05f, 0.06f, 0.07f, 0.9f }, rim[4] = { 0.34f, 0.4f, 0.43f, 0.95f }, band[4] = { 0.68f, 0.25f, 0.15f, 0.95f }, sal[4] = { 1, 0.7f, 0.58f, 1 };
	const float cw = 23, gap = 2, dw = 10 * cw + 9 * gap; float tw = hud_text_w("Bypassing encryption...") * 20, w = fmaxf(dw + 16, tw + 28), x0 = 480 - w / 2, x1 = 480 + w / 2, y0 = 474, y1 = 538;
	hud_claim(x0 - 4, y0 - 4, x1 + 4, y1 + 2, HUD_P_BANNER);
	tk_rect(x0, y0, x1, y1, fr); tk_frame_box(x0, y0, x1, y1, 1.5f, rim);
	tk_code_digits_v2(k, now, 480 - dw / 2, y0 + 5, cw, 30, 30);
	tk_rect(x0 + 3, 513, x1 - 3, 535, band); tk_rect(x0 + 3, 513, x1 - 3, 514, (const float[4]){ 0.95f, 0.5f, 0.35f, 0.6f });
	hud_text(HG.screen, 480, 530, 20, 2, sal, HUD_Z4, "Bypassing encryption...");
}
// b86: the flash with a horizontal light streak through it (v2.0 EP14: the hack planted 7:04.5, the tank destroyed 7:20.4):
// a white core where it happens and a thin cyan streak across the whole screen, gone within a few tenths of a second.
// a = strength 0..1; death: the core is pinker and bigger (an explosion's flash)
static void tk_flare(float sx, float sy, float a, int death) {
	if (a <= 0.01f) return;
	const float r0 = death ? 1.4f : 1.0f;
	for (int k = 0; k < 5; k++) { static const float R[5] = { 170, 115, 75, 45, 24 }, A[5] = { 0.16f, 0.24f, 0.38f, 0.6f, 0.95f };   // (emu b86-1: half this was lost over the arena's glass)
		float c[4] = { 1, death ? 0.86f : 0.97f, death ? 0.9f : 1, A[k] * a }; tk_ring(sx, sy, 0, R[k] * r0, c); }
	const float cs[4] = { 0.85f, 1, 1, a }, cg[4] = { 0.3f, 0.85f, 1, 0.6f * a }, cw[4] = { 0.1f, 0.6f, 0.9f, 0.28f * a };
	tk_band(sx - 200, sx + 200, sy - 24, sy + 24, 560, cw); tk_band(sx - 260, sx + 260, sy - 8, sy + 8, 600, cg); tk_band(sx - 320, sx + 320, sy - 2, sy + 2, 640, cs);
}
static void tk_hud(const float *MVP, V3 cam, float now) {
	if (tk_ok <= 0 || HG.white < 0 || HG.screen < 0) return;
	const float wm[4] = { 1, 1, 1, 0.97f }, rd[4] = { 1, 0.24f, 0.22f, 0.97f };
	const Tank *code = NULL;
	HUD_TAG("~tk.world");   // ui b42: element names for the host overlap checker (no-op on the Vita)
	HUD_TAG("tk.flare");   // b86: the flash + light streak (the hacker's planted hack; anyone who sees a tank blow up)
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; float sx, sy, a = 0; int death = k->state == TK_DEAD;
		if (death && k->t < 0.4f) a = k->t < 0.05f ? k->t / 0.05f : (0.4f - k->t) / 0.35f;
		else if (k->state == TK_LIVE && k->last_hacker == 0 && k->succ_t < 0.45f) a = k->succ_t < 0.04f ? k->succ_t / 0.04f : k->succ_t < 0.25f ? 1 - (k->succ_t - 0.04f) / 0.21f * 0.9f : 0.1f + 0.25f * sinf((k->succ_t - 0.25f) / 0.2f * 3.14159f);
		if (a <= 0.01f) continue;
		V3 c = death ? tk_world(k, (const float[3]){ 0, 0, 110 }) : k->sph[0];
		if (!tk_screen(MVP, c, &sx, &sy) || sx < -200 || sx > 1160 || sy < -100 || sy > 644) continue;
		if (death && !bot_los(cam, c)) continue;   // (not through walls)
		tk_flare(sx, sy, fminf(1, a), death); }
	for (int i = 0; i < TK_MAX; i++) { Tank *k = &tk[i]; if (k->state != TK_LIVE) continue;
		// red diamonds where bullets hit it (footage)
		for (int h = 0; h < 4; h++) { if (k->hit_t[h] <= 0) continue; float sx, sy; if (!tk_screen(MVP, k->hit_pt[h], &sx, &sy)) continue;
			float s = 9 + 10 * (0.35f - k->hit_t[h]), a = fminf(1, k->hit_t[h] * 4); const float c[4] = { 1, 0.15f, 0.15f, 0.9f * a };
			float xy[4][2] = { { sx, sy - s }, { sx + s, sy }, { sx, sy + s }, { sx - s, sy } }, xi[4][2] = { { sx, sy - s * 0.6f }, { sx + s * 0.6f, sy }, { sx, sy + s * 0.6f }, { sx - s * 0.6f, sy } };
			for (int e = 0; e < 4; e++) { int f = (e + 1) & 3; float q[4][2] = { { xy[e][0], xy[e][1] }, { xy[f][0], xy[f][1] }, { xi[f][0], xi[f][1] }, { xi[e][0], xi[e][1] } }; tk_quad((const float (*)[2])q, c); } }
		// an enemy tank's tag above it (red square + name, underlined; footage: "Enemy / Think Tank")
		if (k->team != tk_local_team) { V3 top = v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 255), e = vsub(top, cam); float sx, sy;
			if (vdot(e, e) < 4500.f * 4500.f && tk_screen(MVP, top, &sx, &sy) && sx > -50 && sx < 1010 && sy > 120 && sy < 544) {   // (not over the score / panels)
				float w = hud_text_w(k->name) * 20, x0 = sx - (w + 20) / 2;
				tk_rect(x0, sy - 16, x0 + 14, sy - 2, rd); hud_text(HG.screen, x0 + 20, sy - 1, 20, 0, rd, HUD_Z4, k->name); tk_rect(x0, sy + 3, x0 + 20 + w, sy + 4.5f, rd); } }
		// our tank's tag above it (footage: "WORTHYONE's" / "My Think Tank" and a cyan HP bar), within 45 m
		if (k->team == tk_local_team && tk_hud_style == 0) { V3 top = v3(k->pl.pos.x, k->pl.pos.y, k->pl.pos.z + 255), e = vsub(top, cam); float sx, sy;
			if (vdot(e, e) < 4500.f * 4500.f && vdot(e, e) > 250.f * 250.f && tk_screen(MVP, top, &sx, &sy) && sx > 40 && sx < 920 && sy > 130 && sy < 470) {
				const float cy[4] = { 0.45f, 0.86f, 1, 0.95f }, bk[4] = { 0.02f, 0.05f, 0.07f, 0.6f }; char b[48]; snprintf(b, sizeof(b), "%s's", k->owner_name[0] ? k->owner_name : "Team");
				float w = fmaxf(hud_text_w(b), hud_text_w(k->name)) * 20 + 8; if (w < 110) w = 110;
				hud_text(HG.screen, sx - w / 2 + 4, sy - 30, 20, 0, cy, HUD_Z4, b); hud_text(HG.screen, sx - w / 2 + 4, sy - 9, 20, 0, cy, HUD_Z4, k->name);
				tk_rect(sx - w / 2, sy - 3, sx + w / 2, sy + 3, bk); tk_rect(sx - w / 2, sy - 3, sx - w / 2 + w * fmaxf(0, k->hp / k->hpmax), sy + 3, cy); } }
		// the hacker's "SUCCESS" ring over the tank when the hack is planted (b86: not in the tutorial, whose own SUCCESS disc
		// shows it - v2.0 EP14 7:04.8: one ring, at the screen's centre)
		if (k->last_hacker == 0 && k->succ_t < 1.3f && !tk_tut_mode) { float sx, sy; V3 c = v3(k->sph[0].x, k->sph[0].y, k->sph[0].z + 70);
			if (tk_screen(MVP, c, &sx, &sy)) { float a = k->succ_t < 1 ? 1 : (1.3f - k->succ_t) / 0.3f, r = 58 + 6 * fmaxf(0, 0.15f - k->succ_t) / 0.15f;
				tk_ring(sx, sy, 0, r - 3, (const float[4]){ 0.03f, 0.16f, 0.2f, 0.78f * a }); tk_ring(sx, sy, r - 3, r, (const float[4]){ 0.85f, 0.95f, 1, 0.9f * a });
				hud_text(HG.screen, sx, sy + 8, 24, 2, (const float[4]){ 1, 1, 1, a }, HUD_Z4, "SUCCESS"); } }
		if (k->hacker == 0 || (k->last_hacker == 0 && k->done_t < 1.0f)) code = k;
	}
	HUD_TAG("tk.code");
	if (code) tk_code_panel(code, now);
	HUD_TAG("tk.prompt");
	if (tk_use_prog >= 0 && !code) {   // the hack prompt (an enemy tank in reach)
		if (tk_hud_style == 1) {   // EA tutorial: "Hacking Think Tank / PRESS [E]" on a red band
			tk_rect(350, 334, 610, 366, (const float[4]){ 0.5f, 0.06f, 0.08f, 0.85f }); tk_rect(350, 366, 610, 402, (const float[4]){ 0.02f, 0.03f, 0.05f, 0.62f });
			hud_text(HG.screen, 480, 358, 24, 2, wm, HUD_Z4, "Hacking Think Tank");
			hud_text(HG.screen, 494, 393, 22, 1, wm, HUD_Z4, "PRESS"); tk_btn_square(504, 370, 28);
		} else {   // Terminal Conquest: "Hack Think Tank / PRESS [R]" on a grey band under the crosshair
			tk_rect(366, 352, 594, 384, (const float[4]){ 0.42f, 0.44f, 0.47f, 0.82f }); tk_rect(366, 384, 594, 420, (const float[4]){ 0.03f, 0.04f, 0.06f, 0.7f });
			hud_text(HG.screen, 480, 376, 24, 2, wm, HUD_Z4, "Hack Think Tank");
			hud_text(HG.screen, 494, 411, 22, 1, wm, HUD_Z4, "PRESS"); tk_btn_square(504, 388, 28);
		}
	}
	// an enemy tank aims at the player: a red lock sign under the crosshair and "TARGETED BY THINK TANK" (footage: steady, red)
	HUD_TAG("tk.targeted");
	if (tk_targeted_t > 0) { float cx = 480, cy = code && tk_hud_style == 1 ? 456 : 318, r = 13, w = 2.5f; const float rs[4] = { 1, 0.24f, 0.22f, 0.9f };   // (under the EA hack block while hacking; b86: the v2.0 code panel is at the bottom)
		float q[4][2] = { { cx, cy - r }, { cx + r, cy }, { cx, cy + r }, { cx - r, cy } };
		for (int e = 0; e < 4; e++) { int f = (e + 1) & 3; float dx = q[f][0] - q[e][0], dy = q[f][1] - q[e][1], l = sqrtf(dx * dx + dy * dy), nx = -dy / l * w, ny = dx / l * w;
			float s4[4][2] = { { q[e][0], q[e][1] }, { q[f][0], q[f][1] }, { q[f][0] + nx, q[f][1] + ny }, { q[e][0] + nx, q[e][1] + ny } }; tk_quad((const float (*)[2])s4, rs); }
		tk_ring(cx, cy, 4, 6.5f, rs);
		// b61 (device b61-1: the line ran over the ammo counter at hip): above the crosshair when the counter is under it
		float tw = hud_text_w("TARGETED BY THINK TANK") * 24 / 2 + 4, ty = cy + 34; if (hud_blocked(480 - tw, ty - 22, 480 + tw, ty + 5, HUD_P_FEED)) ty = 272 - 52;
		hud_text(HG.screen, 480, ty, 24, 2, rd, HUD_Z4, "TARGETED BY THINK TANK"); }
}

// ---------------------------------------------------------------- the tutorial's Think Tank (tutorial.h TutTankApi, branch tutorial)
// Once thinktank.h and tutorial.h are both in main.c, the game plugs these in (e.g. right after tut_bind):
//   tut_tank = (TutTankApi){ tk_tut_spawn, tk_tut_remove, tk_tut_hack, tk_tut_hack_stop, tk_tut_progress, tk_tut_rearm, tk_tut_destroyed, tk_tut_health, tk_tut_pos };
// The tank stands still and never shoots (TKF_NOAI), takes the player's bullets / grenades itself (main.c's tk_ray / tk_blast),
// and draws the hack prompt / "SUCCESS" / "Bypassing encryption" itself (tk_hud, EA tutorial look). One press of Square next
// to it starts the 18 s hack (tkg_frame's tk_use or tk_tut_hack); the defences stay down until tk_tut_rearm (TKF_NORESTORE:
// the tutorial times the retry - the EA video shows "You failed to destroy enemy Think Tank." ~13 s after the shutdown, TK_OPEN_T).
static const char *tk_vdir;   // data folder for loading the models on first use (main.c glue: VDIR)
__attribute__((unused)) static int tk_tut_spawn(const float *pos, float yaw, int team) {
	if (tk_ok == 0 && tk_vdir) tk_load(tk_vdir);
	// b86 (v2.0 EP14 7:03-7:20): it fights - aims its laser at the player, fires bursts, side-steps about the arena (TKF_LEASH) -
	// and the HUD is v2.0's (style 0: "Hack Think Tank" on the grey band, the code panel at the bottom); the tutorial keeps the
	// player's health up, its rounds only sting (half damage)
	int i = tk_spawn(v3(pos[0], pos[1], pos[2]), yaw, team, -1, "Enemy", "Think Tank", TKF_NOREGEN | TKF_NORESTORE | TKF_LEASH);
	if (i >= 0) { tk_hud_style = 0; tk_tut_mode = 1; tk[i].dmg_mul = 0.5f; tk[i].gren_cd = 1e9f; }
	return i;
}
__attribute__((unused)) static void tk_tut_remove(int h) { if (h >= 0 && h < TK_MAX) tk_free_slot(&tk[h]); tk_hud_style = 0; tk_tut_mode = 0; }
__attribute__((unused)) static void tk_tut_hack(int h, float dt) { (void)dt; if (tk_alive_i(h) && tk[h].state == TK_LIVE && tk[h].hacker < 0 && tk[h].def) tk_hack_begin(h, 0); }
__attribute__((unused)) static void tk_tut_hack_stop(int h) { (void)h; }   // the hack runs by itself once planted; it fails when the hacker dies or leaves (TK_HACK_LEASH)
__attribute__((unused)) static float tk_tut_progress(int h) { if (!tk_alive_i(h)) return 0; const Tank *k = &tk[h]; return !k->def ? 1 : k->hacker >= 0 ? fminf(0.999f, k->hack_t / TK_HACK_RUN) : 0; }
__attribute__((unused)) static void tk_tut_rearm(int h) { tk_rearm(h); }
__attribute__((unused)) static int tk_tut_destroyed(int h) { return h < 0 || h >= TK_MAX || tk[h].state == TK_DEAD || tk[h].state == TK_OFF; }
__attribute__((unused)) static float tk_tut_health(int h) { return tk_alive_i(h) ? tk[h].hp / tk[h].hpmax : 0; }
__attribute__((unused)) static void tk_tut_pos(int h, float *out) { if (h < 0 || h >= TK_MAX) { out[0] = out[1] = out[2] = 0; return; } out[0] = tk[h].pl.pos.x; out[1] = tk[h].pl.pos.y; out[2] = tk[h].pl.pos.z + 100; }

// ---------------------------------------------------------------- bots.h external targets: bots see, chase and shoot tanks
static int tk_bot_ext_n(void) { return TK_MAX; }
static int tk_bot_ext_get(int k, int team, V3 *pos, V3 *aim) {
	if (k < 0 || k >= TK_MAX || tk[k].state != TK_LIVE || tk[k].team == team) return 0;
	*pos = tk[k].pl.pos; *aim = tk[k].sph[0]; return 1;
}
static void tk_bot_ext_hit(int k, int shooter, float dmg, V3 at) {
	V3 n = vsub(at, tk[k].sph[0]); float l = sqrtf(vdot(n, n)) + 1e-3f; n = vmul(n, 1 / l);
	tk_ray_eye = 0; tk_damage(k, dmg, 1 + shooter, at, vmul(n, -1), SKD_BULLET); fx_impact_nodecal(2, &at.x, &n.x, NULL);   // (b87: no holes on a moving tank)
}
static void tk_hook_bots(void) { bot_ext_n = tk_bot_ext_n; bot_ext_get = tk_bot_ext_get; bot_ext_hit = tk_bot_ext_hit; }
