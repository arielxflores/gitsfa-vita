// Bots: the operators in third person, moving with the player physics, finding their way on the map's AI navigation
// mesh, and fighting the player. Their bodies are animated by chr.h from the original network's clips (shared banks
// chr_anim_m.gab / chr_anim_f.gab) and carry a third-person weapon (w3_<weapon>.gvm) on the Grip bone.
#pragma once
#include "player.h"
#include "audio.h"
#include "model.h"
#include "nav.h"
#include "light.h"
#include "sprite.h"
#define CHR_CLOCK_US() ((unsigned long long)sceKernelGetProcessTimeWide())
#include "chr.h"
#include "cpuprof.h"   // perf (b35): CP_T0 / CP_ADD accumulators for the per-second cpu line
#include "jobs.h"      // perf (b35): physics, poses, path searches and spawn sight tests on the worker cores

#define NBOT_MAX 8
enum { BS_ROAM, BS_ENGAGE, BS_DEAD };
static const char *bot_state_name[3] = { "roam", "fight", "dead" };
typedef struct {
	Player pl; CAnim ca; CSkin sk; MBone *pose; MBone mpose[64]; int state; float hp;   // ca: animation (chr.h), pose: model-order bones
	float path[48][3]; int npath, ipath; float repath_t, prog_d, prog_t, jump_cd; float goal[3]; int stuck_n;
	float think_t, see_t, lost_t, react; int sees; V3 last_seen; float hurt_t;
	int burst; float shot_cd, pause_t, strafe, strafe_t, reload_t; int mag;
	float aim_pitch, dead_t, step_d, flash_t, air_t; int fmat;
	V3 head, chest, pelvis, muzzle;   // world-space points from the last pose
	int kills, deaths, shots, hits, assists;   // (b77: assists - damage within 6 s of a kill someone else made, bot_assist_*)
	int ct;   // character type (chrt index)
	int team, tgt;   // team (0 = the player's side, 1 = enemies); current target: -1 the player, >= 0 a bot, -2 none
	int hold; float hold_yaw, hold_pitch;   // set by the game (Demolition): stand still facing this way (planting / defusing) unless fighting
	float lit[3], floor_z;   // light under foot (smoothed), ground height for the shadow
	int wpn, lod; float prev_yaw, yaw_rate, zoom;          // third-person weapon (BOT_WPN), drawn LOD, turn rate, aiming
	float death_drop, death_vz;                            // death: drop to the floor below the corpse
	int hold_was, hold_kind; float wpn_hide;               // plant / defuse animation state; seconds the weapon stays hidden
	int grabbed;                                           // held by a stealth kill from behind (bot_grab): no moving or shooting
	int grab_front;                                        // b70: grabbed from the front (an execution: its front cqc death, kind 4)
	int ex_on, ex_tgt, ex_front, ex_killed, ex_stab; float ex_t, ex_rdy, ex_len;   // b70: this bot executes ex_tgt (-1 the player, >= 0 a bot): bot_exec_start
	int melee_hit; float melee_t, melee_cd, knife_t, gren_t;   // knife slash: time since it started (< 0 none), damage dealt; knife / grenade shown in the hand (s)
	float crouch_t, slide_cd, ledge_t, sk_t;               // stance timers; next ledge probe; the skill's active time last frame (gesture on activation)
	int pose_new;      // the pose was rebuilt since the last skinning
	int seen_frame;    // bot_frame of the last bots_draw that drew it (off screen: the pose is rebuilt every 4th frame)
	int pose_static;   // a corpse whose death clip has ended: the pose and its skinning are kept (no animation or skinning cost)
	SkinCache sc;   // unused (never initialised, ok = 0): kept so code written against the old bots (vision.h) still builds; bots skin with sk
	// perf (b35): path searches spread over frames (bot_repath asks, bnav_step searches, bot_nav_apply takes the result)
	int nav_pend, nav_gen, nav_tries, nav_ready, nav_res_n, nav_res_gen, nav_fail, nav_ask_frame; float nav_goal[3], nav_goal0[3], nav_from[3], nav_res[48][3];
	// net (net_game.h): netc = a remote player on the host (no AI, no physics here: the net code moves it with the player's commands;
	// armour and regeneration as the player's); netp = a puppet on a client (no AI, no physics: the snapshots place it). The
	// counters tell the other machines what to play (shots: bot_fire, hurts, reloads, acts = knife / throw, spawns, death_info)
	int netc, netp, net_ads, net_score, net_use, net_skill; float ap, regen_cd; int hurts, reloads, acts, act_kind, spawns, death_info;
	int xoff;   // b80: the part blown off in a destruction death (chr.h ca_xlast: 1 head, 2-5 arms / legs; 0 none) - bparts.h
} Bot;
typedef struct { V3 pos, vel; int alive; } BotTarget;   // the player

// character types: one model per playable character; each bot is drawn as one of them
typedef struct {
	Model m; char name[24];
	int idle, pelvis, head, spine, spine2;   // the model's idle clip and bones (menu preview, shooting range fallback)
	int female; CBind bind;                  // animation bank (chr_bank[female]) and the model's bones in it
	const float *mask_at;                    // b71: where the enemy mask sits (attachment 'mask', head bone space; NULL: no mask)
	int skin;                                // b71: the skin loaded (chrt_reskin: k-th of the operator's chr_skins.txt rows, 0 = chr_<op>.gvm)
} ChrType;
// the animation banks (male / female skeletons) and the third-person weapons bots carry
static CBank chr_bank[2];
static CBank chr_og[2];   // b63: the out-of-game poses (chr_og_m.gab / chr_og_f.gab: operator-select "choice" poses, results)
typedef struct { const char *sh; float rpm; int mag; int weight; int kind; } BotWpn;   // kind: 0 automatic, 1 pump shotgun, 2 bolt-action rifle
// damage per hit is scaled by the shot interval so every weapon deals the same damage per second as the rifle
// (shotgun: falls off over ~15 m; sniper: capped below one-shot kills); weight = how often a bot carries it
// b89: rate and magazine as the player's gun (weapons.txt: the v2.0 armory FIRE RATE x 10, AMMO), so a bot's M4A1 sounds and
// reloads like yours; the rifles of the user's AR video added (appended: net messages carry the index)
static const BotWpn BOT_WPN[] = { { "m4a1", 690, 30, 4, 0 }, { "scar", 620, 30, 2, 0 }, { "acr", 630, 30, 2, 0 }, { "c30", 710, 30, 2, 0 },
	{ "mp5", 830, 30, 2, 0 }, { "kriss", 880, 28, 2, 0 }, { "m249", 750, 100, 1, 0 }, { "870", 60, 8, 1, 1 }, { "l118", 40, 5, 1, 2 },
	{ "seburom5", 600, 20, 0, 0 }, { "mateba", 210, 6, 0, 0 },   // net: the handguns (weight 0: bots never carry them; remote players do)
	{ "ak47", 580, 30, 2, 0 }, { "k2", 680, 30, 2, 0 }, { "arx160", 750, 30, 2, 0 }, { "hk417", 600, 30, 2, 0 }, { "f2000", 800, 30, 2, 0 }, { "famas", 880, 25, 2, 0 }, { "x95r", 850, 30, 2, 0 },
	// b90: the SMGs and the PP-2200
	{ "c26a", 850, 30, 2, 0 }, { "evo", 880, 30, 2, 0 }, { "kacpdw", 700, 30, 2, 0 }, { "mp5k", 850, 30, 2, 0 }, { "mpx", 830, 30, 2, 0 }, { "p90", 880, 50, 2, 0 }, { "pp2000", 850, 20, 2, 0 }, { "ump45", 600, 25, 2, 0 },
	// b91: the sniper rifles (kind 2: a sniper's range and its damage cap; the rate of the player's gun, weapons.txt)
	{ "cm2010", 22.5f, 5, 1, 2 }, { "mm200", 22.5f, 5, 1, 2 }, { "m99d", 22.5f, 10, 1, 2 }, { "sa50", 70, 5, 1, 2 }, { "seburosr", 22.5f, 5, 1, 2 },
	{ "seo90", 330, 10, 1, 2 }, { "srv97", 30, 10, 1, 2 }, { "ws2088", 120, 6, 1, 2 },
	// b92: the light machine guns (Assault class, as the M249)
	{ "l86", 660, 50, 1, 0 }, { "mg33", 850, 100, 1, 0 }, { "mg36", 730, 50, 1, 0 }, { "seburomg", 680, 50, 1, 0 },
	// b92: the handguns (weight 0 as the SEBURO M5: net only, remote players carry them)
	{ "mag44", 200, 6, 0, 0 }, { "b93se", 850, 20, 0, 0 }, { "kcz85", 500, 17, 0, 0 }, { "sn57", 600, 20, 0, 0 }, { "p226", 500, 12, 0, 0 },
	{ "m2008", 210, 6, 0, 0 },
	// b97: the shotguns (close-range class, as the 870-ICS)
	{ "ksg", 69, 14, 1, 1 }, { "sta12", 69, 15, 1, 1 }, { "uss12", 200, 6, 1, 1 } };
#define NBOT_WPN ((int)(sizeof(BOT_WPN) / sizeof(BOT_WPN[0])))
static Model bot_w3[NBOT_WPN]; static int bot_w3_ok[NBOT_WPN]; static const float *bot_w3_mz[NBOT_WPN];
static Model bot_w3_knife, bot_w3_gren; static int bot_w3_knife_ok, bot_w3_gren_ok;   // the knife (melee) and the grenade (throws) in the hand
static Model bot_w3_axe; static int bot_w3_axe_ok;   // b70: the Ghost Assault cyborgs' arm blade (w3_axe.gvm; the knife without it)
// b71: the enemy team's masks (vm/make_masks.py: mask_m.gvm / mask_f.gvm, the PC's EnemyMask - a black helmet with glowing red
// visor strips, as the other team wears in the PC footage); bot_mask_on: main.c per match (OPTIONS > GAME > ENEMY MASKS, not in
// Ghost Assault); bot_masked(b): this bot wears one
#ifndef WICON_MELEE
#define WICON_MELEE(i) ((i) == 51 || (i) == 80)   // b98: the knife's or the Double Katar's icon (weapon.h)
#endif
static Model bot_mask[2]; static int bot_mask_ok[2], bot_mask_on = 1;

static long long bot_anim_us; static int bot_anim_n;   // animation time (stats)
static int bot_hit_icon = -1; static V3 bot_hit_dir;  // optional context of the next bot_hurt (main.c: the player's hit; 51 knife, 41 grenade)
#define NCHR_MAX 16
#define BOT_RIM 0.22f   // b62: rim light strength on the operators (bots, menu figures)
static ChrType chrt[NCHR_MAX]; static int nchrt;
static Bot bots[NBOT_MAX]; static int nbot, bot_frame;
static char bot_label[NBOT_MAX][24];   // net: a remote player's name for bot slot i (empty: the operator's name)
CHR_API const char *bot_weapon(int i) { return i >= 0 && i < NBOT_MAX ? BOT_WPN[bots[i].wpn].sh : "m4a1"; }   // the gun bot i carries (weapon.h short name: drops, kill feed)
static V3 lis_pos; static float lis_yaw;                  // listener (camera) for 3D sound
static V3 bot_player_pos;                                 // the player (direction of the player's shots for death clips)
static float bot_dmg_to_player, bot_dmg_dir;              // filled by bots_update: damage dealt this frame and the world yaw it came from
static void (*bot_on_impact)(V3 p, V3 n, int mat);        // a bot bullet hit the map near the player
static void (*bot_on_shot)(V3 from, V3 to);                // every bot shot: muzzle and where the bullet ends (flash, tracer)
static void (*bot_on_blood)(V3 p, V3 dir);                 // b59: a bot's bullet hit another bot at p (blood)
static void (*bot_on_destruct)(int i, int part, V3 dir);    // b80: bot i died a destruction death that blew off part (ca_xlast) - bparts.h
static int bot_last_hitter = -1;                           // the last bot that hit the player
static int bot_last_icon = -1, bot_kill_icon = -1;          // b89: how it hit the player (51 knife; -1 its gun) / how the last bot_on_kill victim died (51 knife, 41 grenade, -1 the killer's gun): the kill feed's and KILLED BY's icon
// b77 (the user: the statistics tab as the footage's: ASSISTS): who hurt whom when - victim slot (0 the player, 1 + i bot i) x
// attacker slot; a kill credits an assist to every other attacker that hurt the victim in the last 6 s (bot_now: the bots' clock)
#define BOT_ASSIST_T 6.0f
static float bot_now, bot_dmg_t[NBOT_MAX + 1][NBOT_MAX + 1]; static int bot_player_assists;
static void bot_assist_hit(int victim, int attacker) { if (victim >= 0 && victim <= NBOT_MAX && attacker >= 0 && attacker <= NBOT_MAX && victim != attacker) bot_dmg_t[victim][attacker] = bot_now + 1e-3f; }
static void bot_assist_kill(int victim, int killer) {   // slots as bot_assist_hit; killer < 0: no one in particular
	if (victim < 0 || victim > NBOT_MAX) return;
	for (int a = 0; a <= NBOT_MAX; a++) { float t = bot_dmg_t[victim][a]; bot_dmg_t[victim][a] = 0;
		if (a == killer || a == victim || t <= 0 || bot_now - t > BOT_ASSIST_T) continue;
		if (a == 0) bot_player_assists++; else if (a - 1 < NBOT_MAX) bots[a - 1].assists++; }
}
static void bot_assist_reset(void) { memset(bot_dmg_t, 0, sizeof(bot_dmg_t)); bot_player_assists = 0; for (int i = 0; i < NBOT_MAX; i++) bots[i].assists = 0; }
static int (*bot_spawn_pick)(int team);                    // team spawn point (set by the game); NULL: anywhere away from the player
static void (*bot_on_kill)(int killer, int victim, int head);   // a bot died (killer -1 = the player)
static int bot_total_shots, bot_total_hits, bot_stuck_respawns;
static int bot_freeze, bot_no_respawn;                     // round start freeze; rounds without respawns (Demolition)
static int (*bot_goal)(int i, float *g);                    // the game's objective for bot i (where to walk); 0 = none
static int (*bot_hold_defuse)(int i);                         // Demolition: 1 = bot i's hold is a defuse (else a plant); NULL: plant
// operator skills (skills.h; set by the game, all NULL = no skills, bots behave exactly as before)
#define BOT_SK_HOOKS 1
static int (*bot_sk_can_see)(int i, int target);              // target -1 = the player, >= 0 a bot; 0 = it can't see it (camo, hacked)
static int (*bot_sk_block)(int i, V3 from, V3 to, V3 *end);   // 1 = the shot is stopped (shield / cyber wall / deployable) at *end
static float (*bot_sk_aim)(int i);                            // hit-chance factor (EMP 0.5); <= 0: no firing (hacked, placing, diving, rushing)
static int (*bot_sk_goal)(int i, float *g);                   // a roam goal from skills (enemies revealed by heat scan / recon / wall)
static void (*bot_sk_move)(int i, float *mx, float *my, int *sprint, int *jump);   // movement override (rush, flee EMP / drone, hacked ...)
static float (*bot_sk_alpha)(int i);                          // draw: 1 normal, (0, 1) cloaked ghost (additive), 0 hidden
static float (*bot_sk_melee_mul)(int i);                      // b79: the knife's reach factor (1; HYPER SPRINT 2 / 2.4: its slash is a takedown)
static float (*bot_sk_active_t)(int i);                       // seconds since bot i's personal skill went active (< 0: not active): the activation gesture
static int (*bot_vis_body)(int i, const float *MVP, const float *M, const float *ld, const float *tint, const float *fogc, int reskin);   // vision.h: draws the body its own way (camo glass, flashes); 0 = draw normally
// b63 Ghost Assault (ga.h; NULL = off): melee-only bots (the cyborgs: no gun, they run at their target and slash it), bots
// without a reserve (the ghosts' pistol: an empty magazine stays empty), the slash's damage
static int (*bot_melee_only)(int i);
static int (*bot_no_reserve)(int i);
static float bot_slash_dmg = 55;
static float (*bot_max_range)(int i);   // b63 (Ghost Assault): > 0 = bot i holds its fire beyond this (the ghosts: each shot shows them)
// b70 (Ghost Assault): the cyborgs' blow that would kill is an execution (wpn.h has the player's side of it):
// bot_exec_ok -> 1 front, 2 from behind, 0 a plain slash; bot_exec_player: the player (target -1) is grabbed by bot i
static int (*bot_exec_ok)(int i, int tgt, float dmg);
static void (*bot_exec_player)(int i, int front);
#define BEX_D 62.f          // the attacker's feet this far from the victim's (third person: the original's clip pair)
#define BEX_STAB_F 0.55f    // the stab, seconds into the strike (front: Saito_CQC) ...
#define BEX_STAB_B 0.25f    // ... (behind: Bato_CQC_Back)
// external targets (Think Tanks, thinktank.h; all NULL = none). A bot's tgt <= -3 means external slot -3 - tgt.
static int (*bot_ext_n)(void);                                // number of slots
static int (*bot_ext_get)(int k, int team, V3 *pos, V3 *aim); // 1 = slot k is a living enemy of this team: feet pos, aim point
static void (*bot_ext_hit)(int k, int shooter, float dmg, V3 at);   // a bot's bullet hit it

static float frand(void) { return (rand() % 10001) / 10000.f; }
static float wrap_pi(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

// 3D-ish sound: volume falls off with distance, pan from the listener's view
static void bot_sound(const char *pfx, V3 p, int vol, float maxd) {
	V3 d = vsub(p, lis_pos); float dist = sqrtf(vdot(d, d)); if (dist > maxd) return;
	float att = 1 - dist / maxd; att = 0.25f + 0.75f * att * att;
	float rx = sinf(lis_yaw), ry = -cosf(lis_yaw), pan = (d.x * rx + d.y * ry) / (dist + 1);
	play_pan(pfx, (int)(vol * att), pan * 0.85f);
}
// model space (character faces -y) -> world
static V3 bot_world(const Bot *b, const float *p) {
	float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	return v3(b->pl.pos.x + c * p[0] - s * p[1], b->pl.pos.y + s * p[0] + c * p[1], b->pl.pos.z + p[2]);
}
static int bot_los(V3 a, V3 b) {
	V3 d = vsub(b, a); float l = sqrtf(vdot(d, d)); if (l < 1) return 1;
	return ray_clear(a, vmul(d, 1 / l), l, 25);   // (= raycast()'s hit beyond l - 25, stopping at the first hit that settles it)
}
static V3 bot_eye(const Bot *b) { return v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 150); }

// ---- paths (perf, b35). A search on a big navigation mesh costs ~1-2 ms on the Vita (RuinedNight: ~1800 triangle expansions on
// average, up to ~6700) and several bots can ask in the same frame. bot_repath now only asks: the searches run one after the
// other in their own context, at most BNAV_BUDGET expansions and BNAV_STARTS new searches per frame (bnav_step, on a worker
// next to the bots' physics), first asked first served; the path is taken at the bot's next decision (bot_nav_apply). Meanwhile
// the bot keeps to its old path, or walks straight at the goal when that is used up, so nobody stands and waits. A result for
// a bot that has been moved since (respawn, round reset) is thrown away and asked again from where it stands.
// BOT_NAV_ASYNC 0: the old synchronous search (tools/perf/host/match_sim comparisons).
#ifndef BOT_NAV_ASYNC
#define BOT_NAV_ASYNC 1
#endif
#define BNAV_BUDGET 300
#define BNAV_STARTS 2
static NavCtx bnav_ctx; static short bnav_q[NBOT_MAX]; static int bnav_nq, bnav_cur = -1, bnav_cur_gen;
static long long bnav_expanded; static int bnav_searches, bnav_frames_waited, bnav_wait_n, bnav_wait_max; static long long bnav_wait_sum;   // (stats)
static void bot_nav_ask(Bot *b, const float *goal) {
	int i = (int)(b - bots); memcpy(b->nav_goal, goal, 12); b->nav_from[0] = b->pl.pos.x; b->nav_from[1] = b->pl.pos.y; b->nav_from[2] = b->pl.pos.z;
	b->nav_gen++; if (!b->nav_pend) b->nav_ask_frame = bot_frame; b->nav_pend = 1; b->nav_ready = 0; b->nav_fail = 0;
	int k = 0; while (k < bnav_nq && bnav_q[k] != i) k++; if (k == bnav_nq && bnav_nq < NBOT_MAX) bnav_q[bnav_nq++] = (short)i;   // (once in the queue)
}
static void bot_repath(Bot *b, const float *goal) {
	CP_T0(tn_);
	float s[3] = { b->pl.pos.x, b->pl.pos.y, b->pl.pos.z }; memcpy(b->goal, goal, 12);
#if BOT_NAV_ASYNC
	b->repath_t = 4 + frand() * 2; b->prog_d = 1e9f; b->prog_t = 0; b->nav_tries = 0;
	if (!nnav) { memcpy(b->path[0], goal, 12); b->npath = 1; b->ipath = 0; b->nav_pend = 0; }   // no mesh: walk straight
	else { memcpy(b->nav_goal0, goal, 12); bot_nav_ask(b, goal); if (b->ipath >= b->npath) { b->npath = 0; b->ipath = 0; } }   // (nothing left of the old path: straight at the goal meanwhile)
	(void)s;
#else
	b->npath = nnav ? nav_path(s, goal, b->path, 48, 45) : 0; b->ipath = 0; b->repath_t = 4 + frand() * 2; b->prog_d = 1e9f; b->prog_t = 0;
	for (int k = 0; k < 4 && nnav && !b->npath && nspawn; k++) { // can't get there from here: wander to a spawn point we can reach
		int si = rand() % nspawn; float g2[3] = { spawns[si].x, spawns[si].y, spawns[si].z }; memcpy(b->goal, g2, 12);
		b->npath = nav_path(s, g2, b->path, 48, 45); }
	if (!b->npath) { memcpy(b->path[0], goal, 12); b->npath = 1; }   // no mesh: walk straight
#endif
	CP_ADD(CP_BOT_NAV, tn_);
}
// a worker's share of a frame (bots_update phase 2, next to the physics): no rand, no logp; touches the search context, the
// queue (only phase 1 adds to it) and the asking bot's nav_res* fields (nothing else reads them during phase 2)
static void bnav_step(void) {
	int budget = BNAV_BUDGET, starts = 0;
	for (;;) {
		if (bnav_cur < 0) {
			if (!bnav_nq || starts >= BNAV_STARTS || budget <= 0) return;
			int i = bnav_q[0]; bnav_nq--; memmove(bnav_q, bnav_q + 1, sizeof(short) * (size_t)bnav_nq); Bot *b = &bots[i]; starts++;
			if (!b->nav_pend) continue;
			bnav_cur = i; bnav_cur_gen = b->nav_gen; bnav_searches++;
			if (!nav_search_begin(&bnav_ctx, b->nav_from, b->nav_goal)) { b->nav_res_n = 0; b->nav_res_gen = bnav_cur_gen; b->nav_ready = 1; bnav_cur = -1; budget--; continue; }
		}
		int e0 = bnav_ctx.expanded, st = nav_search_run(&bnav_ctx, e0 + budget), used = bnav_ctx.expanded - e0;   // (budget > 0 here)
		budget -= used; bnav_expanded += used;
		if (st == 1) { bnav_frames_waited++; return; }   // the budget is spent: on with it next frame
		Bot *b = &bots[bnav_cur];
		b->nav_res_n = nav_search_path(&bnav_ctx, b->nav_res, 48, 45); b->nav_res_gen = bnav_cur_gen; b->nav_ready = 1; bnav_cur = -1; budget--;
	}
}
// phase 1, before the bot decides: take a finished search (or try the fallbacks of the old bot_repath when there was no path)
static void bot_nav_apply(Bot *b) {
	if (!b->nav_ready) return;
	b->nav_ready = 0;
	if (b->nav_res_gen != b->nav_gen || !b->nav_pend) return;   // asked again since: wait for that one
	float dx = b->pl.pos.x - b->nav_from[0], dy = b->pl.pos.y - b->nav_from[1], dz = b->pl.pos.z - b->nav_from[2];
	if (dx * dx + dy * dy + dz * dz > 1500.f * 1500.f) { bot_nav_ask(b, b->nav_goal); return; }   // carried off (not walked): ask from here
	if (b->nav_res_n > 0) { memcpy(b->path, b->nav_res, 12 * (size_t)b->nav_res_n); b->npath = b->nav_res_n; b->ipath = 0; b->prog_d = 1e9f; b->prog_t = 0; b->nav_pend = 0;
		{ float bd = 1e30f; for (int k = 0; k < 4 && k < b->npath; k++) { float ex = b->path[k][0] - b->pl.pos.x, ey = b->path[k][1] - b->pl.pos.y, e2 = ex * ex + ey * ey; if (e2 < bd) { bd = e2; b->ipath = k; } } }   // (it walked on meanwhile: from the nearest corner)
		int w = bot_frame - b->nav_ask_frame; bnav_wait_sum += w; bnav_wait_n++; if (w > bnav_wait_max) bnav_wait_max = w; return; }
	if (b->nav_tries < 4 && nspawn) {   // can't get there from here: wander to a spawn point we can reach (as before: up to 4 tries)
		b->nav_tries++; int si = rand() % nspawn; float g2[3] = { spawns[si].x, spawns[si].y, spawns[si].z }; memcpy(b->goal, g2, 12); bot_nav_ask(b, g2); return; }
	memcpy(b->path[0], b->nav_goal0, 12); b->npath = 1; b->ipath = 0; b->nav_pend = 0;   // nothing found: walk straight at the goal it was given (as before)
}
// perf (b35): the spawn points out of the player's sight (at least 25 m away, no line of sight), the first 64. Each test is a long
// ray: a respawn cost ~0.22 M instructions on Urban and ~2.2 M on E-Center (55 tests; host counts), about 0.6 / 6 ms on the Vita,
// and a round start (Elimination, Demolition: every bot in one frame) eight times that. The list depends only on the player's
// position, the spawn points and the collision, so it is kept until one of them changes (a round start tests once for all bots);
// the tests run on the jobs.h workers (each spawn point writes its own flag). The same list, in the same order, as the old loop.
static struct { float pe[3]; int gen, nsp, n; const Spawn *sp; short cand[64]; unsigned char *vis; int cap; } bspc = { .gen = -1 };
static void bsp_job(void *ctx, int i) {
	const float *pe = ctx; const Spawn *s = &spawns[i]; float dx = s->x - (pe[0]), dy = s->y - (pe[1]);
	bspc.vis[i] = dx * dx + dy * dy < 2500 * 2500 || bot_los(v3(s->x, s->y, s->z + 150), v3(pe[0], pe[1], pe[2])) ? 0 : 1;
}
static int bot_spawn_cands(const BotTarget *T, const short **cand) {
	float pe[3] = { T->pos.x, T->pos.y, T->pos.z + 140 }; *cand = bspc.cand;
	if (bspc.gen == col_gen && bspc.sp == spawns && bspc.nsp == nspawn && !memcmp(bspc.pe, pe, sizeof(pe))) return bspc.n;
	if (nspawn > bspc.cap) { unsigned char *v = realloc(bspc.vis, nspawn); if (!v) return 0; bspc.vis = v; bspc.cap = nspawn; }
	jobs_parallel_for(bsp_job, pe, nspawn);   // (the old loop stopped after 64 candidates: the extra tests change nothing)
	bspc.n = 0; for (int i = 0; i < nspawn && bspc.n < 64; i++) if (bspc.vis[i]) bspc.cand[bspc.n++] = (short)i;
	memcpy(bspc.pe, pe, sizeof(pe)); bspc.gen = col_gen; bspc.sp = spawns; bspc.nsp = nspawn;
	return bspc.n;
}
// spawn far from (and out of sight of) the player
static void bot_spawn(Bot *b, const BotTarget *T) {
	// the spawn point: one rand() as before, then the game's own pick (it may use rand() too); the sight tests only when needed
	int r = rand(), si = -1;
	if (bot_spawn_pick) si = bot_spawn_pick(b->team);
	if (si < 0) { const short *cand; int nc = bot_spawn_cands(T, &cand); si = nc ? cand[r % nc] : r % nspawn; }
	memset(&b->pl, 0, sizeof(b->pl)); b->pl.pos = v3(spawns[si].x, spawns[si].y, spawns[si].z + 5); b->pl.yaw = spawns[si].yaw;
	b->hp = 100; b->ap = 100; b->regen_cd = 0; b->spawns++; b->state = BS_ROAM; b->mag = 50; b->reload_t = 0; b->see_t = 0; b->sees = 0; b->hurt_t = 0; b->npath = 0; b->burst = 0; b->pause_t = 0;
	b->aim_pitch = 0; b->air_t = 0; b->think_t = frand() * 0.2f; b->jump_cd = 0; b->stuck_n = 0; b->tgt = -2;
	b->hold_was = 0; b->wpn_hide = 0; b->mag = BOT_WPN[b->wpn].mag; b->prev_yaw = b->pl.yaw; b->yaw_rate = 0; b->zoom = 0; b->death_drop = 0; b->death_vz = 0;
	b->grabbed = 0; b->melee_t = -1; b->melee_cd = 0; b->knife_t = 0; b->gren_t = 0; b->crouch_t = 0; b->slide_cd = 2 + frand() * 4; b->ledge_t = frand() * 0.25f; b->sk_t = -1;
	b->grab_front = 0; b->ex_on = 0; b->ex_tgt = -2;
	ca_revive(&b->ca); b->sk.cmask = 0; b->sk.cinv = 0; b->xoff = 0; b->ca.phase = frand(); b->ca.idle_t = frand() * 2; ca_upper(&b->ca, S_SWAPEND, 0.01f, 0.2f, 1);   // spawns with the weapon coming up
	{ char e[64]; snprintf(e, sizeof(e), "%s/equip_on_tp", snd_wpn(BOT_WPN[b->wpn].sh)->base); ev_play(e, &b->pl.pos.x); }
	light_at(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 20, b->lit); b->floor_z = b->pl.pos.z;
	float g[3] = { T->pos.x, T->pos.y, T->pos.z };
	if (b->team == 0) { for (int k = 0; k < 8; k++) { int j = rand() % (nbot ? nbot : 1); if (j < nbot && bots[j].team != b->team) { g[0] = bots[j].pl.pos.x; g[1] = bots[j].pl.pos.y; g[2] = bots[j].pl.pos.z; break; } } }
	bot_repath(b, g);
}
// third-person weapons: w3_<sh>.gvm next to the characters (missing ones fall back to the M4A1, then to none)
static void bot_weapons_load(const char *dir) {
	for (int w = 0; w < NBOT_WPN; w++) {
		if (bot_w3_ok[w]) continue;
		char p[160]; snprintf(p, sizeof(p), "%sw3_%s.gvm", dir, BOT_WPN[w].sh);
		if (model_load(&bot_w3[w], p) == 0) { bot_w3_ok[w] = 1; bot_w3_mz[w] = model_attach(&bot_w3[w], "muzzle"); }
	}
	char p[160];
	if (!bot_w3_knife_ok) { snprintf(p, sizeof(p), "%sw3_knife.gvm", dir); bot_w3_knife_ok = model_load(&bot_w3_knife, p) == 0 ? 1 : -1; }
	if (!bot_w3_axe_ok) { snprintf(p, sizeof(p), "%sw3_axe.gvm", dir); bot_w3_axe_ok = model_load(&bot_w3_axe, p) == 0 ? 1 : -1; }
	for (int g = 0; g < 2; g++) if (!bot_mask_ok[g]) { snprintf(p, sizeof(p), "%smask_%c.gvm", dir, g ? 'f' : 'm'); bot_mask_ok[g] = model_load(&bot_mask[g], p) == 0 && bot_mask[g].npal >= 1 ? 1 : -1; }
	if (!bot_w3_gren_ok) { snprintf(p, sizeof(p), "%sw3_m67.gvm", dir); bot_w3_gren_ok = model_load(&bot_w3_gren, p) == 0 ? 1 : -1; }
}
static void bot_event(void *user, int kind, const char *str, int foot);
// load one character model (+ its animation bank and the bots' weapons the first time); returns its type index or < 0
// b103 (user: "in the operatives screen please include the option to toggle masks from operatives skins by pressing square to
// remove or equip them where present"): chr_gog.txt (tools/chars/make_gog.py) "<file stem> <mesh index> ..." = the meshes of an
// operator model that are the skin's mask / goggles (the game's Default_Goggle / Goggle part). chr_gog_apply marks them
// MDF_GOGGLE after a load; model.h md_nogog leaves them out of a draw. A model without a line has no such part.
typedef struct { char stem[40]; unsigned bits; } ChrGog;
#define CHR_GOG_MAX 256
static ChrGog chr_gog_row[CHR_GOG_MAX]; static int nchr_gog, chr_gog_read;
static void chr_gog_load(const char *dir) {   // (read with the first character: chrt_load marks its meshes)
	nchr_gog = 0; chr_gog_read = 1; char p[192]; snprintf(p, sizeof(p), "%schr_gog.txt", dir); FILE *f = fopen(p, "rb");
	if (f) { char ln[160];
		while (fgets(ln, sizeof(ln), f) && nchr_gog < CHR_GOG_MAX) { ChrGog *g = &chr_gog_row[nchr_gog]; memset(g, 0, sizeof(*g)); int o = 0;
			if (ln[0] == '#' || sscanf(ln, "%39s%n", g->stem, &o) < 1) continue;
			for (char *q = ln + o; *q; ) { char *e; long v = strtol(q, &e, 10); if (e == q) break; if (v >= 0 && v < 32) g->bits |= 1u << v; q = e; }
			if (g->bits) nchr_gog++; }
		fclose(f); }
}
static char chr_dir[128];
static unsigned chr_gog_bits(const char *stem) { if (!chr_gog_read) chr_gog_load(chr_dir); for (int i = 0; i < nchr_gog; i++) if (!strcmp(chr_gog_row[i].stem, stem)) return chr_gog_row[i].bits; return 0; }
static void chr_gog_apply(Model *M, const char *stem) { unsigned b = chr_gog_bits(stem); for (int i = 0; i < M->nmesh && i < 32; i++) if ((b >> i) & 1) M->mesh[i].flags |= MDF_GOGGLE; }
static int chrt_load(const char *path, const char *name) {
	if (nchrt >= NCHR_MAX) return -3;
	char dir[128]; snprintf(dir, sizeof(dir), "%s", path); { char *e = strrchr(dir, '/'); if (!e) e = strrchr(dir, ':'); if (e) e[1] = 0; else dir[0] = 0; }
	if (!chr_dir[0]) snprintf(chr_dir, sizeof(chr_dir), "%s", dir);
	int fem = !strcmp(name, "motoko") || !strcmp(name, "maven") || !strcmp(name, "reiko") || !strcmp(name, "sitara");
	if (!chr_bank[fem].ok) { char p[160]; snprintf(p, sizeof(p), "%schr_anim_%c.gab", dir, fem ? 'f' : 'm'); if (cb_load(&chr_bank[fem], p) < 0) return -4;
		// b63: the menus' out-of-game poses (chr.h cb_pose_seq), optional: kept only when its skeleton is the bank's, bone for bone
		snprintf(p, sizeof(p), "%schr_og_%c.gab", dir, fem ? 'f' : 'm');
		if (cb_load(&chr_og[fem], p) == 0) { int same = chr_og[fem].nb == chr_bank[fem].nb; for (int b = 0; same && b < chr_og[fem].nb; b++) same = !strcmp(chr_og[fem].bone[b], chr_bank[fem].bone[b]) && chr_og[fem].parent[b] == chr_bank[fem].parent[b];
			if (!same) cb_free(&chr_og[fem]); }
	}
	bot_weapons_load(dir);
	ChrType *C = &chrt[nchrt]; memset(C, 0, sizeof(*C));
	int r = model_load(&C->m, path); if (r < 0) return r;   // (chars b35: model_load builds the skin plans; no CPU vertex copy is kept)
	{ char st_[48]; snprintf(st_, sizeof(st_), "chr_%s", name); chr_gog_apply(&C->m, st_); }   // b103: its mask / goggle meshes (chr_gog.txt, loaded before the characters)
	Model *M = &C->m;
	C->idle = model_find_clip(M, "idle");
	C->pelvis = model_find_bone(M, "Bip01_Pelvis"); C->head = model_find_bone(M, "Bip01_Head"); C->spine = model_find_bone(M, "Bip01_Spine");
	C->spine2 = model_find_bone(M, "Bip01_Spine2");
	snprintf(C->name, sizeof(C->name), "%s", name);
	C->female = fem; cb_bind(&C->bind, &chr_bank[fem], M);
	C->mask_at = model_attach(M, "mask");   // (b71)
	if (C->idle < 0 || C->pelvis < 0 || C->head < 0 || M->nb > 64) { model_free(M); return -2; }
	return nchrt++;
}
// ---- b71 (the user: "could you implement all skins for characters"): the PC's other outfits for each operator (vm/make_skins.py:
// chr_<op>_<skin>.gvm and chr_skins.txt "<op> <skin> <NAME>", the default "a" = chr_<op>.gvm first). One skin of each operator is
// loaded at a time (memory as before): main.c picks them per match (the player's choice for the player's operator, OPTIONS >
// GAME > BOT SKINS random or standard for the rest) and the operative screen loads the one it shows.
#define CHR_SKIN_MAX 128
typedef struct { char op[12], key[16], name[24]; int bad; } ChrSkin;
static ChrSkin chr_skin[CHR_SKIN_MAX]; static int nchr_skin;
static void chr_skins_load(const char *dir, void (*log)(const char *, ...)) {
	nchr_skin = 0; char p[192]; snprintf(p, sizeof(p), "%schr_skins.txt", dir); FILE *f = fopen(p, "rb");
	if (f) { char ln[160];
		while (fgets(ln, sizeof(ln), f) && nchr_skin < CHR_SKIN_MAX) {
			ChrSkin *k = &chr_skin[nchr_skin]; char nm[64] = { 0 }; memset(k, 0, sizeof(*k));
			if (ln[0] == '#' || sscanf(ln, "%11s %15s %63[^\r\n]", k->op, k->key, nm) < 2) continue;
			snprintf(k->name, sizeof(k->name), "%s", nm[0] ? nm : k->key); nchr_skin++; }
		fclose(f); }
	if (log) log("operator skins: %d rows in chr_skins.txt%s (b71)\n", nchr_skin, f ? "" : " (missing: the default skins only)");
}
// b76 (user: the operatives' fidelity when they are turned and looked at): the menus' high-detail operators (vm/make_skins.py
// chr_hi.txt "<op> <skin> <file stem>": the TPS body's top LOD, mesh only - main.c chr_preview lends it the in-game model's
// textures)
typedef struct { char op[12], key[16], stem[40]; } ChrHi;
static ChrHi chr_hi_row[CHR_SKIN_MAX]; static int nchr_hi;
static void chr_hi_load(const char *dir, void (*log)(const char *, ...)) {
	nchr_hi = 0; char p[192]; snprintf(p, sizeof(p), "%schr_hi.txt", dir); FILE *f = fopen(p, "rb");
	if (f) { char ln[160];
		while (fgets(ln, sizeof(ln), f) && nchr_hi < CHR_SKIN_MAX) { ChrHi *h = &chr_hi_row[nchr_hi]; memset(h, 0, sizeof(*h));
			if (ln[0] != '#' && sscanf(ln, "%11s %15s %39s", h->op, h->key, h->stem) == 3) nchr_hi++; }
		fclose(f); }
	if (log) log("menu operators: %d high-detail rows in chr_hi.txt%s (b76)\n", nchr_hi, f ? "" : " (missing: the in-game models)");
}
static int chr_skin_n(const char *op) { int n = 0; for (int i = 0; i < nchr_skin; i++) n += !strcmp(chr_skin[i].op, op); return n ? n : 1; }
static ChrSkin *chr_skin_at(const char *op, int k) { for (int i = 0; i < nchr_skin; i++) if (!strcmp(chr_skin[i].op, op) && !k--) return &chr_skin[i]; return NULL; }
static const char *chr_skin_name(const char *op, int k) { const ChrSkin *s = chr_skin_at(op, k); return s ? s->name : "STANDARD"; }
static void chr_skin_stem(const char *op, int k, char *out, int n) { const ChrSkin *s = chr_skin_at(op, k); if (!s || !strcmp(s->key, "a")) snprintf(out, n, "chr_%s", op); else snprintf(out, n, "chr_%s_%s", op, s->key); }   // (the in-game model's file stem)
static int chr_skin_has_gog(const char *op, int k) { char st[48]; chr_skin_stem(op, k, st, sizeof(st)); return chr_gog_bits(st) != 0; }   // b103: that skin has a mask / goggle part
static const char *chr_hi_stem(const char *op, int k) { const ChrSkin *s = chr_skin_at(op, k); const char *key = s ? s->key : "a";
	for (int i = 0; i < nchr_hi; i++) if (!strcmp(chr_hi_row[i].op, op) && !strcmp(chr_hi_row[i].key, key)) return chr_hi_row[i].stem; return NULL; }
// load skin k of character type ct in place of the one it has (bots of that type get new skinning buffers); 1 loaded, 0 already
// that one, < 0 failed (the type keeps its skin)
static void (*chr_reskin_log)(const char *, ...);
static int chrt_reskin(int ct, int k) {
	if (ct < 0 || ct >= nchrt) return -1; ChrType *C = &chrt[ct]; if (k < 0 || k >= chr_skin_n(C->name)) k = 0;
	if (k == C->skin) return 0;
	ChrSkin *s = chr_skin_at(C->name, k); if (s && s->bad) return -1;
	char p[192]; if (!s || !strcmp(s->key, "a")) snprintf(p, sizeof(p), "%schr_%s.gvm", chr_dir, C->name); else snprintf(p, sizeof(p), "%schr_%s_%s.gvm", chr_dir, C->name, s->key);
	Model M; memset(&M, 0, sizeof(M)); int r = model_load(&M, p);
	int idle = r < 0 ? -1 : model_find_clip(&M, "idle"), pel = r < 0 ? -1 : model_find_bone(&M, "Bip01_Pelvis"), hd = r < 0 ? -1 : model_find_bone(&M, "Bip01_Head");
	if (r < 0 || idle < 0 || pel < 0 || hd < 0 || M.nb > 64) { if (r >= 0) model_free(&M); if (s) s->bad = 1; if (chr_reskin_log) chr_reskin_log("skin %s: %s FAILED (%d): keeps its skin\n", C->name, p, r); return -2; }
	for (int i = 0; i < nbot; i++) if (bots[i].ct == ct && bots[i].sk.ok) cskin_free(&bots[i].sk);   // (the old meshes' buffers)
	model_free(&C->m); C->m = M; C->idle = idle; C->pelvis = pel; C->head = hd;
	{ char st_[48]; chr_skin_stem(C->name, k, st_, sizeof(st_)); chr_gog_apply(&C->m, st_); }   // b103
	C->spine = model_find_bone(&C->m, "Bip01_Spine"); C->spine2 = model_find_bone(&C->m, "Bip01_Spine2");
	cb_bind(&C->bind, &chr_bank[C->female], &C->m); C->mask_at = model_attach(&C->m, "mask"); C->skin = k;
	for (int i = 0; i < nbot; i++) if (bots[i].ct == ct) { cskin_init(&bots[i].sk, &C->m); bots[i].pose = NULL; }
	if (chr_reskin_log) chr_reskin_log("skin %s: %s (%s, %d meshes)\n", C->name, s ? s->name : "STANDARD", p, C->m.nmesh);
	return 1;
}
// b88 (v2.0 classes, menu.h MENU_CHR_CLASS): a bot carries a primary of its operator's class - assault rifles / machine guns
// (Batou, Ishikawa, Borma, Azuma), submachine guns / shotguns (Motoko, Paz, Maven, Kuro, Reiko), sniper rifles (Saito,
// Togusa, Sitara); an operator not in the list (the Ghost Assault cyborg, the dummy) carries any
static int bot_wpn_cls(int w) {   // the class that carries it (menu.h MENU_CHR_CLASS): 0 assault (AR, LMG), 1 infiltrator (SMG, SG), 2 specialist (SR, PDW)
	static const char *const C1 = " mp5 kriss 870 ksg sta12 uss12 c26a evo kacpdw mp5k mpx p90 ump45 ", *const C2 = " l118 pp2000 cm2010 mm200 m99d sa50 seburosr seo90 srv97 ws2088 ";
	char k[24]; snprintf(k, sizeof(k), " %s ", BOT_WPN[w].sh); return strstr(C1, k) ? 1 : strstr(C2, k) ? 2 : 0;
}
static int bot_chr_cls(const char *stem) {
	static const char *const C[3] = { "bato ishikawa borma azuma", "motoko paz maven kuro reiko", "saito togusa sitara" };
	if (!stem || !stem[0]) return -1; size_t n = strlen(stem);
	for (int k = 0; k < 3; k++) for (const char *p = strstr(C[k], stem); p; p = strstr(p + 1, stem)) if ((p == C[k] || p[-1] == ' ') && (p[n] == ' ' || !p[n])) return k;
	return -1;
}
static int bot_pick_weapon_cls(int cls) {
	int tot = 0; for (int w = 0; w < NBOT_WPN; w++) if (bot_w3_ok[w] && (cls < 0 || bot_wpn_cls(w) == cls)) tot += BOT_WPN[w].weight;
	if (!tot) { if (cls >= 0) return bot_pick_weapon_cls(-1); return 0; }
	int k = rand() % tot; for (int w = 0; w < NBOT_WPN; w++) if (bot_w3_ok[w] && (cls < 0 || bot_wpn_cls(w) == cls)) { k -= BOT_WPN[w].weight; if (k < 0) return w; }
	return 0;
}

static void bot_set_char(Bot *b, int ct) {
	if (b->sk.ok) cskin_free(&b->sk);
	b->ct = ct; b->wpn = bot_pick_weapon_cls(ct >= 0 && ct < nchrt ? bot_chr_cls(chrt[ct].name) : -1);   // (b88: the operator's class)
	const CBank *B = &chr_bank[chrt[ct].female]; int set = cb_find_set(B, BOT_WPN[b->wpn].sh);
	ca_init(&b->ca, B, set < 0 ? 0 : set); b->ca.on_event = bot_event; b->ca.user = b;
	cskin_init(&b->sk, &chrt[ct].m); b->pose = NULL;
}
// b63 (Ghost Assault, ga.h): bot b drawn as character type ct, carrying BOT_WPN[wpn], its body animated with animation set
// aset (NULL: the weapon's own set) - a cyborg is the cyborg body with the knife set, a ghost its operator with the pistol
static void bot_restyle(Bot *b, int ct, int wpn, const char *aset) {
	if (ct < 0 || ct >= NCHR_MAX || !chrt[ct].m.nmesh) ct = b->ct;
	if (ct != b->ct || !b->sk.ok) { if (b->sk.ok) cskin_free(&b->sk); b->ct = ct; cskin_init(&b->sk, &chrt[ct].m); b->pose = NULL; }
	if (wpn >= 0 && wpn < NBOT_WPN) b->wpn = wpn;
	const CBank *B = &chr_bank[chrt[b->ct].female]; int set = cb_find_set(B, aset ? aset : BOT_WPN[b->wpn].sh);
	ca_init(&b->ca, B, set < 0 ? 0 : set); b->ca.on_event = bot_event; b->ca.user = b;
}
// b63 (Ghost Assault): a dead bot back on its feet where it lies (an ally hacked its body), full health, as a fresh spawn
static void bot_revive_here(int i, const BotTarget *T) {
	if (i < 0 || i >= nbot || bots[i].state != BS_DEAD) return;
	Bot *b = &bots[i]; V3 p = b->pl.pos; float y = b->pl.yaw, fz = b->floor_z - b->death_drop;
	bot_spawn(b, T); b->pl.pos = v3(p.x, p.y, fz + 5); b->pl.yaw = y; b->floor_z = fz; b->npath = 0; b->repath_t = 0; b->tgt = -2; b->prev_yaw = y;
}
// a different character for each bot where possible (shuffled)
static void bots_pick_chars(void) {
	int order[NCHR_MAX]; for (int i = 0; i < nchrt; i++) order[i] = i;
	for (int i = nchrt - 1; i > 0; i--) { int j = rand() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
	for (int i = 0; i < nbot; i++) bot_set_char(&bots[i], order[i % nchrt]);
}
static int bots_init(int n, const BotTarget *T) {
	if (!nchrt) return -1;
	nbot = n > NBOT_MAX ? NBOT_MAX : n;
	for (int i = 0; i < nbot; i++) memset(&bots[i], 0, sizeof(Bot));
	bots_pick_chars();
	for (int i = 0; i < nbot; i++) bot_spawn(&bots[i], T);
	return 0;
}

static int bot_hurt(int i, float dmg, int head, int killer);
static void bot_hit_info(int icon, V3 dir);
CHR_API void bot_release(int i);
// damage of one hit at distance dist (cm): the weapon's share of the rifle's damage per second (base = the rifle's per-hit damage)
static float (*bot_falloff_hook)(const char *sh, float dist);   // b90: main.c - the bot's gun's damage drop-off with distance (weapons.txt RANGE)
static float bot_wpn_dmg(const BotWpn *W, float base, float dist) {
	float d = base * (60.f / W->rpm) / 0.08f;
	if (W->kind != 1 && bot_falloff_hook) d *= bot_falloff_hook(W->sh, dist);   // (the shotgun has its own spread drop-off below)
	if (W->kind == 1) { float k = 1.3f - dist / 1500; d *= k < 0.15f ? 0.15f : k > 1 ? 1 : k; }   // pellets spread out
	if (W->kind == 2 && d > 95) d = 95;                                                     // a body shot never kills from full health
	return d;
}
static float bot_wpn_acc(const BotWpn *W, float dist) { float p = 0.45f - dist / (W->kind == 2 ? 15000 : 6000); return p < 0.06f ? 0.06f : p; }
static void bot_fire(Bot *b, const BotTarget *T) {
	const BotWpn *W = &BOT_WPN[b->wpn];
	b->mag--; b->shots++; bot_total_shots++; b->flash_t = 0.05f;
	if (W->kind) ca_play(&b->ca.fire, ca_c(&b->ca, S_FIRE), 0.03f, 0.15f, CA_UPPER, 1);   // pump / bolt: the whole fire clip (the pump or the bolt is in it)
	else ca_fire(&b->ca);
	snd_wfire_tp(W->sh, &b->muzzle.x);   // near / distant layers and panning as the original's event
	if (b->tgt <= -3) {   // at an external target (a Think Tank): the weapon's bullets, the target applies its own armour
		V3 xp, pc; if (!bot_ext_get || !bot_ext_get(-3 - b->tgt, b->team, &xp, &pc)) return;
		V3 d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d)) + 1, p = 0.6f - dist / 8000; if (p < 0.15f) p = 0.15f; if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
		V3 end = vadd(b->muzzle, vmul(d, 1.6f));
		if (frand() < p) { end = pc; if (bot_ext_hit) bot_ext_hit(-3 - b->tgt, (int)(b - bots), bot_wpn_dmg(W, 24, dist), pc); }
		if (bot_on_shot) bot_on_shot(b->muzzle, end);
		return;
	}
	if (b->tgt >= 0) {   // at another bot
		Bot *v = &bots[b->tgt]; V3 pc = v->chest, d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d)) + 1, spd = sqrtf(v->pl.vel.x * v->pl.vel.x + v->pl.vel.y * v->pl.vel.y);
		float p = bot_wpn_acc(W, dist); if (spd > 300) p *= 0.6f; p *= fminf(1, 0.3f + b->see_t * 0.3f); if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
		V3 end = vadd(b->muzzle, vmul(d, 1.6f)); if (bot_sk_block && bot_sk_block((int)(b - bots), b->muzzle, pc, &end)) { if (bot_on_shot) bot_on_shot(b->muzzle, end); return; }
		if (frand() < p) { b->hits++; end = pc; bot_hit_info(-1, vsub(pc, b->muzzle)); bot_hurt(b->tgt, bot_wpn_dmg(W, 9, dist), frand() < 0.1f, (int)(b - bots)); if (bot_on_blood) bot_on_blood(pc, vmul(d, 1 / dist)); }
		else { V3 side = v3(-d.y / dist, d.x / dist, 0); end = vadd(pc, vadd(vmul(side, (frand() - 0.5f) * 120), v3(0, 0, (frand() - 0.3f) * 100))); }
		if (bot_on_shot) bot_on_shot(b->muzzle, end);
		return;
	}
	V3 pc = v3(T->pos.x, T->pos.y, T->pos.z + 115); V3 d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d));
	float spd = sqrtf(T->vel.x * T->vel.x + T->vel.y * T->vel.y);
	float p = bot_wpn_acc(W, dist); if (spd > 300) p *= 0.6f; p *= fminf(1, 0.3f + b->see_t * 0.3f); if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
	V3 end = vadd(b->muzzle, vmul(d, 1.6f)); if (bot_sk_block && bot_sk_block((int)(b - bots), b->muzzle, pc, &end)) { if (bot_on_shot) bot_on_shot(b->muzzle, end); return; }
	if (frand() < p) { b->hits++; bot_total_hits++; bot_dmg_to_player += bot_wpn_dmg(W, 8, dist); bot_dmg_dir = atan2f(b->pl.pos.y - T->pos.y, b->pl.pos.x - T->pos.x); bot_last_hitter = (int)(b - bots); bot_last_icon = -1; bot_assist_hit(0, 1 + bot_last_hitter); end = pc; }
	else if (dist < 3000 && bot_on_impact) { // a near miss: the bullet hits something around the player
		if (frand() < 0.5f) ev_play(frand() < 0.5f ? "impact/bullet_fly_l" : "impact/bullet_fly_h", NULL);   // 2D in the original
		V3 side = v3(-d.y / dist, d.x / dist, 0); float o1 = (frand() - 0.5f) * 140, o2 = (frand() - 0.3f) * 120;
		V3 aim = vadd(pc, vadd(vmul(side, o1), v3(0, 0, o2))); V3 dd = vsub(aim, b->muzzle); float l = sqrtf(vdot(dd, dd)); dd = vmul(dd, 1 / l);
		int mat = 0; V3 n; float h = raycast(b->muzzle, dd, l + 1500, &mat, &n);
		if (h > 0) { V3 hp = vadd(b->muzzle, vmul(dd, h)); V3 q = vsub(hp, T->pos); if (vdot(q, q) < 900 * 900) bot_on_impact(hp, n, mat); end = hp; }
	}
	if (bot_on_shot) bot_on_shot(b->muzzle, end);
}
// the side a hit came from, relative to the bot's facing: 0 front, 1 back, 2 left, 3 right. d: direction of travel of the hit
static int bot_hit_side(const Bot *b, V3 d) {
	float l = sqrtf(d.x * d.x + d.y * d.y); if (l < 1e-3f) return 0;
	float fx = cosf(b->pl.yaw), fy = sinf(b->pl.yaw), sx = -d.x / l, sy = -d.y / l;   // s: towards the shooter
	float fw = sx * fx + sy * fy, rt = sx * fy - sy * fx;                              // right of the facing = (sin y, -cos y)
	if (fw > 0.707f) return 0; if (fw < -0.707f) return 1; return rt > 0 ? 3 : 2;
}
// a corpse's travel (the death clip moves the body up to ~2.5 m) is shortened when a wall is in the way
static void bot_death_clear(Bot *b) {
	CAnim *A = &b->ca; A->death_scale = 1; if (A->death.clip < 0) return;
	const CClip *Cl = &A->B->clip[A->death.clip]; CLoc L0[CB_MAXB], L1[CB_MAXB];
	cb_sample(A->B, A->death.clip, 0, 0, L0); cb_sample(A->B, A->death.clip, Cl->len, 0, L1);
	int r = A->B->root; float dv[3] = { L1[r].p[0] - L0[r].p[0], L1[r].p[1] - L0[r].p[1], 0 };
	V3 w = vsub(bot_world(b, dv), b->pl.pos); float l = sqrtf(w.x * w.x + w.y * w.y);
	A->death_p0[0] = L0[r].p[0]; A->death_p0[1] = L0[r].p[1];
	if (l < 20) return;
	V3 o = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 40), d = vmul(w, 1 / l);
	float h = raycast(o, d, l + 45, NULL, NULL);
	if (h >= 0) A->death_scale = fmaxf(0, (h - 45) / l);
}
// damage (killer: -1 the player, >= 0 a bot, -2 the world); returns 1 on a kill
static int bot_hurt(int i, float dmg, int head, int killer) {
	Bot *b = &bots[i]; if (b->state == BS_DEAD) { bot_hit_icon = -1; return 0; }
	ChrType *C = &chrt[b->ct];
	// where the hit came from (for the hit reaction and the death clip)
	V3 dir; int icon = bot_hit_icon; bot_hit_icon = -1;
	if (icon >= 0) dir = bot_hit_dir;
	else if (killer >= 0) dir = vsub(b->pl.pos, bots[killer].pl.pos);
	else if (killer == -1) dir = vsub(b->pl.pos, bot_player_pos);
	else dir = v3(-cosf(b->pl.yaw), -sinf(b->pl.yaw), 0);
	if (b->netc) { float ab = fminf(b->ap, dmg * 0.6f); b->ap -= ab; dmg -= ab; b->regen_cd = 4; }   // net: a remote player has the player's armour
	b->hp -= dmg; b->hurt_t = 1.5f;
	if (killer >= -1) bot_assist_hit(1 + i, killer + 1);   // b77
	if (b->hp <= 0) {
		bot_assist_kill(1 + i, killer >= -1 ? killer + 1 : -1);   // b77
		b->state = BS_DEAD; b->dead_t = 0; b->deaths++;
		int side = bot_hit_side(b, dir), kind = 0;
		if (b->grabbed) kind = b->grab_front ? 4 : 3;                            // the stealth kill from behind / b70 an execution from the front (the victim's cqc deaths)
		else if (WICON_MELEE(icon) && dmg >= 500) kind = 3;
		else if (icon == 41 || (dmg >= 300 && !WICON_MELEE(icon))) kind = 1;             // grenade / bomb
		else if (dmg >= 100 && !WICON_MELEE(icon)) kind = 2;                             // a heavy hit: the destruction deaths
		else if (head && !WICON_MELEE(icon)) kind = 2;                                   // b80 (Paz video EP19 271.7 / 336.5 / 410.6 s): every headshot kill pops the head off
		if (b->ex_on && !b->ex_killed && b->ex_tgt >= 0) bot_release(b->ex_tgt);   // b70: killed while executing: its victim is let go
		b->grabbed = 0; b->grab_front = 0; b->ex_on = 0; b->melee_t = -1; b->knife_t = 0; b->gren_t = 0; b->pl.in_crouch = 0;
		ca_die(&b->ca, head, side, kind); bot_death_clear(b); b->death_info = kind | side << 2 | (head ? 16 : 0);   // (net: the puppets die the same way)
		b->xoff = ca_xlast; if (b->xoff && bot_on_destruct) bot_on_destruct(i, b->xoff, dir);   // b80: the part flies off (bparts.h)
		static const char *de[3] = { "foley/common/death_back", "foley/common/death_knee", "foley/common/death_spin" };
		const char *fe = kind == 2 ? (head ? "foley/common/destruct_head" : b->xoff == 2 || b->xoff == 3 ? "foley/common/destruct_arm" : b->xoff >= 4 ? "foley/common/destruct_leg" : "foley/common/destruct_fullbody") : side == 1 ? de[1] : de[rand() % 3];
		if (kind == 2 && ev_find(fe) < 0) fe = "foley/common/destruct_fullbody";   // (b80: the arm / leg sounds where the bank has them)
		ev_play_ex(ev_find(fe), &b->pl.pos.x, -1, 0, snd_chr_female(C->name) ? 0.4f : 0.1f, 1.f);
		snd_voice(C->name, head ? "death_large_3rd" : "death_3rd", &b->pl.pos.x);
		if (killer >= 0) bots[killer].kills++;
		bot_kill_icon = icon; if (bot_on_kill) bot_on_kill(killer, i, head);
		return 1;
	}
	b->hurts++;
	if (b->state != BS_ENGAGE && !b->netc) { b->state = BS_ENGAGE; b->see_t = 0; b->react = 0.25f; }
	if (b->reload_t <= 0) ca_hit(&b->ca, icon == 41 ? 2 : 0);
	if (frand() < 0.4f) snd_voice(C->name, "damage_3rd", &b->pl.pos.x);
	return 0;
}
// body animation hooks for the skills (skills.h) and future AI: the original's skill activation gesture
// (base_*_3rd_skill_start), a grenade throw (cook + throw from the M67 set, the weapon hidden meanwhile)
CHR_API void bot_anim_skill(int i) { if (i < 0 || i >= nbot || bots[i].state == BS_DEAD) return; ca_upper(&bots[i].ca, S_SKILL, 0.1f, 0.2f, 1); }
CHR_API float bot_anim_throw(int i) {   // returns the time (s) at which the grenade leaves the hand
	if (i < 0 || i >= nbot || bots[i].state == BS_DEAD) return 0;
	Bot *b = &bots[i]; float L = ca_play_from(&b->ca, &b->ca.up, "m67", S_THROW, 0.1f, 0.25f, CA_UPPER); if (L <= 0) return 0;
	b->wpn_hide = L; b->gren_t = 0.2f; ca_stop(&b->ca.fire); if (!b->netp && !b->netc) { b->acts++; b->act_kind = 2; } return 0.2f;   // the M67 in the hand until it is released
}
// the stealth kill from behind (weapons: the player's knife): bot i is grabbed (ready pose, no moving or shooting) until
// the kill (bots_damage with the knife icon: its cqc death) or bot_release. bots_update also grabs a bot when the game
// holds it (b->hold) with the player right behind it.
static void bot_grab_ex(int i, int front) {   // b70: front = an execution from the front (Death_CQC_Ready_Saito held)
	if (i < 0 || i >= nbot || bots[i].state == BS_DEAD) return;
	Bot *b = &bots[i]; if (!b->grabbed) ca_grabbed_ex(&b->ca, front); b->grabbed = 1; b->grab_front = front; b->hold = 1; b->hold_yaw = b->pl.yaw; b->hold_pitch = 0; b->hold_was = 1; b->hold_kind = 3;
	b->melee_t = -1; b->pl.in_crouch = 0; b->ex_on = 0; b->knife_t = 0; b->reload_t = 0;
}
static void bot_grab(int i) { bot_grab_ex(i, 0); }
CHR_API void bot_release(int i) { if (i < 0 || i >= nbot) return; Bot *b = &bots[i]; if (b->grabbed) { b->grabbed = 0; b->grab_front = 0; b->hold = 0; ca_released(&b->ca); } }
// b70: the execution's voices (voice/action/<op>/cqc_split_<op>_attack_f1 ... _death_b2: the operators' own lines; none for
// the cyborg body)
static void bot_cqc_voice(const char *op, int attack, int front, const float *pos) {
	char n[96]; snprintf(n, sizeof(n), "voice/action/%s/cqc_split_%s_%s_%c%d", op, op, attack ? "attack" : "death", front ? 'f' : 'b', 1 + (rand() & 1));
	int e = ev_find(n); if (e >= 0) ev_play_ex(e, pos, -1, 0, 0.f, 1.f);
}
// b70: bot i executes tgt (-1 the player, >= 0 a bot): the grab (Saito_CQC_Ready / Bato_CQC_Back_Ready) and the strike
// (Saito_CQC / Bato_CQC_Back); a bot victim is held in its grabbed pose and dies as the strike begins (its death clip is the
// strike's partner), the player at the stab (main.c holds the view on the attacker meanwhile)
static void bot_exec_start(int i, int tgt, int front) {
	Bot *b = &bots[i]; float rdy = 0, len = ca_cqc_attack_ex(&b->ca, front, &rdy);
	if (len <= 0) { len = 1.8f; rdy = 0.37f; }
	b->ex_on = 1; b->ex_tgt = tgt; b->ex_front = front; b->ex_t = 0; b->ex_rdy = rdy; b->ex_len = len; b->ex_killed = 0; b->ex_stab = 0;
	b->melee_t = -1; b->knife_t = len; b->wpn_hide = 0; b->melee_cd = len + 0.5f; b->acts++; b->act_kind = 1;
	if (tgt >= 0) { Bot *v = &bots[tgt]; if (!front) v->pl.yaw = atan2f(v->pl.pos.y - b->pl.pos.y, v->pl.pos.x - b->pl.pos.x);   // (from behind: it keeps looking away)
		else v->pl.yaw = atan2f(b->pl.pos.y - v->pl.pos.y, b->pl.pos.x - v->pl.pos.x);
		v->pl.vel = v3(0, 0, 0); v->prev_yaw = v->pl.yaw; bot_grab_ex(tgt, front); }
	else if (bot_exec_player) bot_exec_player(i, front);
	ev_play("weapon/knife/swing_tp", &b->pl.pos.x);
}
// per frame while bot i executes (its movement and shooting are off): it closes in on the victim during the grab, faces it,
// deals the kill and the stab
static void bot_exec_update(int i, float dt, const BotTarget *T) {
	Bot *b = &bots[i]; b->ex_t += dt; int t = b->ex_tgt;
	int alive = t >= 0 ? (t < nbot && bots[t].state != BS_DEAD) : T->alive;
	if (!alive && !b->ex_killed) { b->ex_on = 0; if (t >= 0) bot_release(t); ca_stop(&b->ca.full); return; }   // (the victim died some other way)
	V3 vp = t >= 0 ? bots[t].pl.pos : T->pos, d = vsub(b->pl.pos, vp); float hd = sqrtf(d.x * d.x + d.y * d.y);
	if (b->ex_t < b->ex_rdy + 0.1f && hd > 1) {   // to the clip pair's distance (from the victim's back when from behind: it faces away)
		float k = fminf(1, dt * 14); V3 want = v3(vp.x + d.x / hd * BEX_D, vp.y + d.y / hd * BEX_D, b->pl.pos.z);
		b->pl.pos.x += (want.x - b->pl.pos.x) * k; b->pl.pos.y += (want.y - b->pl.pos.y) * k; b->pl.vel = v3(0, 0, 0); }
	b->pl.yaw = atan2f(vp.y - b->pl.pos.y, vp.x - b->pl.pos.x);
	float stab = b->ex_rdy + (b->ex_front ? BEX_STAB_F : BEX_STAB_B);
	if (!b->ex_killed && b->ex_t >= (t >= 0 ? b->ex_rdy : stab)) {
		b->ex_killed = 1; b->hits++;
		if (t >= 0) { bot_hit_info(51, vsub(bots[t].pl.pos, b->pl.pos)); bot_hurt(t, 999, 0, i); }
		else { bot_dmg_to_player += 999; bot_dmg_dir = atan2f(b->pl.pos.y - T->pos.y, b->pl.pos.x - T->pos.x); bot_last_hitter = i; bot_last_icon = 51; bot_assist_hit(0, 1 + i); bot_total_hits++; }
		bot_cqc_voice(chrt[b->ct].name, 1, b->ex_front, &b->pl.pos.x);
	}
	if (!b->ex_stab && b->ex_t >= stab) { b->ex_stab = 1;
		V3 hp = t >= 0 ? bots[t].head : v3(T->pos.x, T->pos.y, T->pos.z + 150); hp.z -= 14;   // (the neck)
		ev_play("impact/killknife", &hp.x); if (bot_on_blood) bot_on_blood(hp, v3(-d.x / (hd + 1), -d.y / (hd + 1), 0.4f));
		if (t >= 0) bot_cqc_voice(chrt[bots[t].ct].name, 0, b->ex_front, &hp.x); }
	if (b->ex_t >= b->ex_len) b->ex_on = 0;
}
// a knife slash (the original's quick knife: the knife set's slash with the knife in the hand, then the gun comes back)
static void bot_melee_start(Bot *b) {
	float L = ca_play_from(&b->ca, &b->ca.up, "knife", S_FIRE, 0.06f, 0.15f, CA_UPPER); if (L <= 0) L = 0.5f;
	b->melee_t = 0; b->melee_hit = 0; b->knife_t = L; b->wpn_hide = L; b->melee_cd = 1.2f + frand() * 0.6f; ca_stop(&b->ca.fire); b->acts++; b->act_kind = 1;
	ev_play("weapon/knife/swing_tp", &b->pl.pos.x);
}
// optional context for the next bots_damage / bot_hurt: the hit's weapon icon (51 knife, 41 grenade, -1 bullet) and its direction
static void bot_hit_info(int icon, V3 dir) { bot_hit_icon = icon; bot_hit_dir = dir; }
static int bots_damage(int i, float dmg, int head) { return bot_hurt(i, dmg, head, -1); }

// clip events (chr.h): foot plants -> footsteps, sound events -> the bot's weapon sounds (the clips name the original's
// FMOD events; "common" ones are the weapon class's shared set, played here with the bot's own weapon)
static void bot_event(void *user, int kind, const char *str, int foot) {
	Bot *b = (Bot *)user; (void)foot; if (b < bots || b >= bots + NBOT_MAX) return;
	const ChrType *C = &chrt[b->ct];
	if (kind == CE_LFOOT || kind == CE_RFOOT) {
		if (!b->pl.grounded) return;
		float hs = sqrtf(b->pl.vel.x * b->pl.vel.x + b->pl.vel.y * b->pl.vel.y); if (hs < 40) return;
		float cy = cosf(b->pl.yaw), sy = sinf(b->pl.yaw), vf = b->pl.vel.x * cy + b->pl.vel.y * sy;
		const char *act = b->pl.crouched ? "foot_slowwalk" : b->pl.sprinting || hs > 450 ? "foot_fastrun" : hs > 280 ? "foot_run" : fabsf(vf) < hs * 0.5f ? "foot_walk_s" : "foot_walk";
		b->fmat = player_floor_mat(&b->pl);   // perf (b35): the material ray when a step sounds, not every update
		snd_foley(snd_chr_female(C->name) ? 2 : 1, act, b->fmat, &b->pl.pos.x);
	} else if (kind == CE_SOUND && str && *str) {
		char n[96]; int k = 0; for (const char *p = str; *p && k < 95; p++) n[k++] = *p == ' ' ? '_' : *p; n[k] = 0;
		int e = ev_find(n);
		if (!strncmp(n, "weapon/", 7)) {   // the bot's own weapon's version of a class event
			const char *last = strrchr(n, '/'); char w[96]; snprintf(w, sizeof(w), "%s%s", snd_wpn(BOT_WPN[b->wpn].sh)->base, last ? last : "");
			int e2 = ev_find(w); if (e2 >= 0) e = e2;
		}
		if (e >= 0) ev_play_ex(e, &b->pl.pos.x, -1, 0, 0.f, 1.f);
	}
}
// ---- perf (b35): the update runs in phases so that the per-bot physics and pose evaluation can use the jobs.h workers:
// 1 (serial, in bot order) timers, perception, decisions, shooting, paths; 2 (workers) physics, light under foot, floor height;
// 3 (serial) animation state and its events (sounds); 4 (workers) pose evaluation and the world points; 5 (serial) corpses
// settling, push-apart. Every bot now decides on the same snapshot of the others (where they stood at the start of the frame,
// last frame's poses) instead of seeing the bots before it in the list already moved: the outcome no longer depends on the list
// order, and is bit for bit the same with or without workers (tools/perf/host/match_sim compares state hashes).
// Jobs: no GL, no logp, no sounds, no rand(); each touches only its own bot (raycasts are thread-safe since b35, player.h).
typedef struct { float mx, my; int sprint, jump, skip, pose; CInput in; } BotStep;
static BotStep bot_step[NBOT_MAX]; static float bot_step_dt;
static void bnav_step(void);
static void bot_phys_job(void *ctx, int i) {
	if (BOT_NAV_ASYNC) { if (i == 0) { bnav_step(); return; } i--; }   // item 0: the path searches (handed out first: the longest item)
	(void)ctx; Bot *b = &bots[i]; const BotStep *S = &bot_step[i]; float dt = bot_step_dt; if (S->skip) return;
	b->pl.local_floor = 1;   // (the floor_mat global is the player's)
	if (!b->netc && !b->netp) player_update(&b->pl, S->mx, S->my, S->sprint, S->jump, dt);   // (b->fmat: phase 3, when a sound needs it; net: netc / netp are moved by net_game.h)
	b->air_t = b->pl.grounded ? 0 : b->air_t + dt;
	{ float L[3]; light_at(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 20, L); float k = fminf(1, 4 * dt); for (int c = 0; c < 3; c++) b->lit[c] += (L[c] - b->lit[c]) * k;
	  if (b->pl.grounded) b->floor_z = b->pl.pos.z; else if ((i + bot_frame) % 4 == 0) { float h = raycast(v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 40), v3(0, 0, -1), 600, NULL, NULL); b->floor_z = h > 0 ? b->pl.pos.z + 40 - h : b->pl.pos.z - 600; } }
}
static void bot_pose_job(void *ctx, int i) {
	(void)ctx; Bot *b = &bots[i]; const BotStep *S = &bot_step[i]; if (S->skip || !S->pose) return; ChrType *C = &chrt[b->ct];
	MBone *P = ca_pose(&b->ca, &S->in); const CBank *B = b->ca.B;
	cb_to_model(&C->bind, P, b->mpose); b->pose = b->mpose;
	// world points for hit tests and effects (bank bones)
	float hp[3] = { P[B->head].p[0], P[B->head].p[1], P[B->head].p[2] + 8 };
	b->head = bot_world(b, hp); b->pelvis = bot_world(b, P[B->pelvis].p); b->chest = bot_world(b, P[B->spine[2]].p);
	const float *mz = bot_w3_ok[b->wpn] ? bot_w3_mz[b->wpn] : NULL;
	if (mz) { float m[3]; model_bone_xform(&P[B->grip], mz, m); b->muzzle = bot_world(b, m); } else b->muzzle = b->chest;
	b->pose_new = 1;
}
static void bots_update(float dt, const BotTarget *T) {
	CP_T0(tb_); bot_now += dt;   // (b77: the assists' clock)
	bot_player_pos = T->pos;
	bot_dmg_to_player = 0;
	V3 pe = v3(T->pos.x, T->pos.y, T->pos.z + 140);
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; float mx = 0, my = 0; int jump = 0, sprint = 0; ChrType *C = &chrt[b->ct]; BotStep *S = &bot_step[i]; S->skip = 0;
		if (BOT_NAV_ASYNC) bot_nav_apply(b);
		if (b->hurt_t > 0) b->hurt_t -= dt; if (b->flash_t > 0) b->flash_t -= dt; if (b->jump_cd > 0) b->jump_cd -= dt;
		const BotWpn *W = &BOT_WPN[b->wpn];
		if (b->melee_cd > 0) b->melee_cd -= dt; if (b->knife_t > 0) b->knife_t -= dt; if (b->gren_t > 0) b->gren_t -= dt; if (b->slide_cd > 0) b->slide_cd -= dt; b->crouch_t += dt;
		if (b->netc || b->netp) {   // net: no AI, no physics here (net_game.h moves them); a remote player respawns like the player (3.5 s)
			if (b->melee_t >= 0) { b->melee_t += dt; if (b->knife_t <= 0) b->melee_t = -1; }
			if (b->state == BS_DEAD) { b->dead_t += dt; if (b->netc && b->dead_t > 3.5f && !bot_no_respawn) bot_spawn(b, T); }
			S->mx = S->my = 0; S->sprint = S->jump = 0; continue; }
		// the current target: the player or a bot of the other team
		if (b->tgt >= 0 && bots[b->tgt].state == BS_DEAD) { b->tgt = -2; if (b->state == BS_ENGAGE) b->lost_t = 9; }
		if (b->tgt == -1 && (!T->alive || b->team == 0)) b->tgt = -2;
		V3 xp_ = T->pos, xa_ = T->pos; if (b->tgt <= -3 && !(bot_ext_get && bot_ext_get(-3 - b->tgt, b->team, &xp_, &xa_))) { b->tgt = -2; if (b->state == BS_ENGAGE) b->lost_t = 9; }
		V3 tpos = b->tgt >= 0 ? bots[b->tgt].pl.pos : b->tgt <= -3 ? xp_ : T->pos; int talive = b->tgt >= 0 || b->tgt <= -3 ? 1 : b->tgt == -1 ? T->alive : 0;
		V3 tp = vsub(tpos, b->pl.pos); float tdist = sqrtf(tp.x * tp.x + tp.y * tp.y);
		if (b->state == BS_DEAD) {
			b->dead_t += dt;
			if (b->dead_t > 5 && !bot_no_respawn) bot_spawn(b, T);
		} else {
			// ---- perception (a few times a second)
			b->think_t -= dt;
			if (b->think_t <= 0) {
				b->think_t = 0.15f + frand() * 0.1f;
				// pick the nearest enemy in view (the player only for the enemy team), keeping the current one when it is still seen
				int cand[NBOT_MAX + 9], nc = 0; float cd[NBOT_MAX + 9];
				if (b->team == 1 && T->alive) { V3 q = vsub(T->pos, b->pl.pos); cd[nc] = sqrtf(q.x * q.x + q.y * q.y); cand[nc++] = -1; }
				for (int j = 0; j < nbot; j++) if (bots[j].team != b->team && bots[j].state != BS_DEAD) { V3 q = vsub(bots[j].pl.pos, b->pl.pos); cd[nc] = sqrtf(q.x * q.x + q.y * q.y); cand[nc++] = j; }
				for (int k = 0; bot_ext_get && bot_ext_n && k < bot_ext_n() && nc < NBOT_MAX + 9; k++) { V3 xp, xa; if (!bot_ext_get(k, b->team, &xp, &xa)) continue; V3 q = vsub(xp, b->pl.pos); cd[nc] = sqrtf(q.x * q.x + q.y * q.y) + 300; cand[nc++] = -3 - k; }   // Think Tanks (a little less urgent than operators)
				int best = -2, tests = 0; float bd = 5000;
				for (int pass = 0; pass < nc && tests < 2; pass++) {   // nearest first, at most two line-of-sight rays
					int k = -1; float m = bd; for (int c2 = 0; c2 < nc; c2++) if (cd[c2] < m) { m = cd[c2]; k = c2; }
					if (k < 0) break; int t = cand[k]; cd[k] = 1e9f; if (t > -3 && bot_sk_can_see && !bot_sk_can_see(i, t)) continue;
					V3 xp = T->pos, xa = T->pos; if (t <= -3) bot_ext_get(-3 - t, b->team, &xp, &xa);
					V3 cp = t >= 0 ? bots[t].pl.pos : t <= -3 ? xp : T->pos; V3 q = vsub(cp, b->pl.pos);
					float facing = fabsf(wrap_pi(atan2f(q.y, q.x) - b->pl.yaw));
					if (!(facing < 1.4f || m < 900 || b->hurt_t > 0 || (b->state == BS_ENGAGE && t == b->tgt))) continue;
					tests++; V3 ce = t >= 0 ? bots[t].chest : t <= -3 ? xa : pe;
					CP_T0(tr_); int los_ = bot_los(bot_eye(b), ce) || (t == -1 && bot_los(bot_eye(b), v3(T->pos.x, T->pos.y, T->pos.z + 100))); CP_ADD(CP_BOT_RAYS, tr_);
					if (los_) { best = t; bd = m; break; }
				}
				if (best != -2 && best != b->tgt) { b->tgt = best; b->see_t = 0; }
				b->sees = best != -2;
				if (b->tgt <= -3) bot_ext_get(-3 - b->tgt, b->team, &xp_, &xa_);
				tpos = b->tgt >= 0 ? bots[b->tgt].pl.pos : b->tgt <= -3 ? xp_ : T->pos; talive = b->tgt >= 0 || b->tgt <= -3 ? 1 : b->tgt == -1 ? T->alive : 0; tp = vsub(tpos, b->pl.pos); tdist = sqrtf(tp.x * tp.x + tp.y * tp.y);
				if (b->sees) { b->last_seen = tpos; b->lost_t = 0;
					if (b->state != BS_ENGAGE) { b->state = BS_ENGAGE; b->see_t = 0; b->react = 0.5f + frand() * 0.4f; b->burst = 0; b->pause_t = 0; b->strafe = frand() < 0.5f ? -1 : 1; b->strafe_t = 0.6f + frand();
						if (b->pl.sprinting && b->slide_cd <= 0 && frand() < 0.4f) { b->pl.in_crouch = 1; b->crouch_t = 0; b->slide_cd = 6 + frand() * 4; } } }
			}
			if (b->state == BS_ENGAGE) {
				if (b->sees) b->see_t += dt; else b->lost_t += dt;
				if (b->lost_t > 2.5f || !talive) { b->state = BS_ROAM; float g[3] = { b->last_seen.x, b->last_seen.y, b->last_seen.z }; bot_repath(b, g); }
			}
			// ---- movement
			if (b->grabbed) { b->pl.yaw = b->hold_yaw; b->pl.in_crouch = 0; }   // held by a stealth kill: no moving, turning or shooting
			else if (b->ex_on) { bot_exec_update(i, dt, T); b->pl.in_crouch = 0; }   // b70: executing its victim (no moving or shooting)
			else if (b->state == BS_ENGAGE) {
				float want = atan2f(tp.y, tp.x); b->pl.yaw += wrap_pi(want - b->pl.yaw) * fminf(1, 7 * dt);
				b->strafe_t -= dt; if (b->strafe_t <= 0) { b->strafe = frand() < 0.5f ? -1 : 1; b->strafe_t = 0.5f + frand() * 1.5f; if (frand() < 0.25f) b->strafe = 0;
					// stance (as players in the footage): crouch to fire from a standing spot, now and then while strafing; snipers more often
					if (b->pl.slide_t <= 0 && b->crouch_t > 1.2f) { int c = (b->strafe == 0 && tdist > 600 && frand() < 0.6f) || (b->strafe != 0 && frand() < 0.15f) || (W->kind == 2 && frand() < 0.5f);
						if (c != b->pl.in_crouch) { b->pl.in_crouch = c; b->crouch_t = 0; } } }
				float far_d = W->kind == 1 ? 900 : W->kind == 2 ? 5000 : 2600, near_d = W->kind == 1 ? 250 : W->kind == 2 ? 1500 : 450;   // shotguns close in, snipers keep away
				mx = b->strafe * 0.42f; my = tdist > far_d ? 0.6f : tdist < near_d ? -0.4f : 0;
				if (!b->sees) { mx = 0; my = 0.5f; }   // lost sight: step toward the last seen spot
				int mel_ = bot_melee_only && bot_melee_only(i), dry_ = !mel_ && b->mag <= 0 && b->reload_t <= 0 && bot_no_reserve && bot_no_reserve(i);
				if (mel_) { mx = 0; my = tdist > 95 ? 1 : 0; sprint = tdist > 320 && b->pl.grounded; if (b->pl.in_crouch) { b->pl.in_crouch = 0; b->crouch_t = 0; } }   // b63 cyborgs: straight at it
				else if (dry_) { mx = b->strafe * 0.6f; my = -1; sprint = 1; }   // b63 an empty pistol and no reserve: back off and run
				else if (bot_no_reserve && bot_no_reserve(i)) { mx = b->strafe * 0.6f; my = tdist < 900 ? -0.6f : 0; }   // b63 (the ghosts): keep their distance while they shoot
				float dz = (tpos.z + 115) - (b->pl.pos.z + 120); float wp = atan2f(dz, tdist + 1); b->aim_pitch += (wp - b->aim_pitch) * fminf(1, 8 * dt);
				// ---- shooting
				// knife: a target within reach in front of it gets a slash (the original's quick knife)
				float mm_ = bot_sk_melee_mul ? bot_sk_melee_mul(i) : 1;   // b79: a hyper sprint's knife reaches further (and works while the gun can't)
				if (b->melee_t < 0 && b->melee_cd <= 0 && b->reload_t <= 0 && b->sees && talive && !bot_freeze && !(bot_sk_aim && bot_sk_aim(i) <= 0 && mm_ <= 1)) {
					float dz = tpos.z - b->pl.pos.z, fa = fabsf(wrap_pi(atan2f(tp.y, tp.x) - b->pl.yaw));
					if (tdist < (mel_ ? 160 : 125) * mm_ && fabsf(dz) < 70 && fa < 0.6f && b->pl.grounded && b->pl.slide_t <= 0) {
						int ex_ = mel_ && bot_exec_ok ? bot_exec_ok(i, b->tgt, bot_slash_dmg) : 0;   // b70: the blow that would kill: an execution
						if (ex_) bot_exec_start(i, b->tgt, ex_ == 1);
						else { bot_melee_start(b); if (mel_) b->melee_cd = 0.55f + frand() * 0.35f; } }
				}
				if (b->melee_t >= 0) {   // the blade passes ~0.22 s in: 55 damage when the target is still in reach
					b->melee_t += dt;
					if (!b->melee_hit && b->melee_t >= 0.22f) { b->melee_hit = 1;
						float fa = fabsf(wrap_pi(atan2f(tp.y, tp.x) - b->pl.yaw));
						if (talive && tdist < 165 * mm_ && fa < 0.9f) {
							ev_play("impact/knife/body", &tpos.x);
							float sd_ = mel_ ? bot_slash_dmg : mm_ > 1 ? 100 : 55;   // (b63: the cyborgs' blade; b79: a hyper sprint's slash takes the target down)
							if (b->tgt >= 0) { bot_hit_info(51, vsub(bots[b->tgt].pl.pos, b->pl.pos)); bot_hurt(b->tgt, sd_, 0, i); }
							else if (b->tgt == -1) { bot_dmg_to_player += sd_; bot_dmg_dir = atan2f(b->pl.pos.y - T->pos.y, b->pl.pos.x - T->pos.x); bot_last_hitter = i; bot_last_icon = 51; bot_assist_hit(0, 1 + i); b->hits++; bot_total_hits++; } } }
					if (b->knife_t <= 0) b->melee_t = -1;
				}
				else if (mel_ || dry_) { }   // b63: the cyborgs' blade only; an empty pistol with no reserve
				else if (b->reload_t > 0) { b->reload_t -= dt; if (b->reload_t <= 0) { int m = b->mag > 0 ? b->mag : 0; b->mag = W->kind == 1 ? (m + 4 < W->mag ? m + 4 : W->mag) : W->mag; } }   // the shotgun: four shells
				else if (b->sees && b->see_t > b->react && tdist < (W->kind == 2 ? 7000 : 4000) && !(bot_max_range && bot_max_range(i) > 0 && tdist > bot_max_range(i)) && !bot_freeze && !(bot_sk_aim && bot_sk_aim(i) <= 0)) {
					if (b->pause_t > 0) b->pause_t -= dt;
					else {
						float iv = 60.f / W->rpm;
						if (b->burst <= 0) b->burst = W->kind == 0 ? 2 + rand() % 4 : W->kind == 1 ? 1 + rand() % 2 : 1;
						b->shot_cd -= dt;
						while (b->shot_cd <= 0 && b->burst > 0 && b->mag > 0) { b->shot_cd += iv; b->burst--; bot_fire(b, T); }
						if (b->burst <= 0) { b->pause_t = W->kind == 0 ? 0.5f + frand() * 0.7f : W->kind == 1 ? 0.3f + frand() * 0.5f : 0.6f + frand() * 0.9f; b->shot_cd = W->kind ? iv : 0; }
						if (b->mag <= 0 && !(bot_no_reserve && bot_no_reserve(i))) { b->reload_t = W->kind == 1 ? ca_reload_shells(&b->ca, 4) : ca_reload(&b->ca, 0); if (b->reload_t <= 0) b->reload_t = 2.5f; if (frand() < 0.3f) snd_voice(C->name, "reload_3rd", &b->pl.pos.x); }   // the reload sounds come from the clip's own events (bot_event)
					}
				}
			} else {
				b->aim_pitch *= 1 - fminf(1, 4 * dt);
				if (b->pl.in_crouch && b->pl.slide_t <= 0 && b->crouch_t > 0.2f && !b->hold) { b->pl.in_crouch = 0; b->crouch_t = 0; }   // up and running again
				b->repath_t -= dt;
				if (b->hold) { b->pl.yaw += wrap_pi(b->hold_yaw - b->pl.yaw) * fminf(1, 6 * dt); b->aim_pitch += (b->hold_pitch - b->aim_pitch) * fminf(1, 6 * dt); b->npath = 0; b->repath_t = 0.5f; }
				else if (b->repath_t <= 0 || (b->ipath >= b->npath && !(BOT_NAV_ASYNC && b->nav_pend))) { float g[3] = { T->pos.x, T->pos.y, T->pos.z };
					if (!(bot_goal && bot_goal(i, g)) && !(bot_sk_goal && bot_sk_goal(i, g))) {
					{ int j0 = rand() % (nbot ? nbot : 1); for (int k = 0; k < nbot; k++) { int j = (j0 + k) % nbot; if (bots[j].team != b->team && bots[j].state != BS_DEAD && (b->team == 0 || frand() < 0.4f)) { g[0] = bots[j].pl.pos.x; g[1] = bots[j].pl.pos.y; g[2] = bots[j].pl.pos.z; break; } } }
					if (frand() < 0.3f && nspawn) { int s = rand() % nspawn; g[0] = spawns[s].x; g[1] = spawns[s].y; g[2] = spawns[s].z; } }
					bot_repath(b, g); }
				const float *w = b->ipath < b->npath ? b->path[b->ipath] : BOT_NAV_ASYNC && b->nav_pend ? b->nav_goal0 : NULL;   // (a path on its way: straight at the goal)
				if (!b->hold && w) {
					int virt = b->ipath >= b->npath;   // waiting for a path: walk at the goal, no sprinting, vaulting or unsticking (it would climb onto things)
					float dx = w[0] - b->pl.pos.x, dy = w[1] - b->pl.pos.y, d = sqrtf(dx * dx + dy * dy);
					if (d < 70) { b->ipath++; b->prog_d = 1e9f; b->prog_t = 0; b->stuck_n = 0; }
					else {
						float want = atan2f(dy, dx), dyaw = wrap_pi(want - b->pl.yaw); b->pl.yaw += dyaw * fminf(1, 6 * dt);
						my = fabsf(dyaw) < 1.0f ? 1 : 0.25f; sprint = !virt && d > 600 && fabsf(dyaw) < 0.3f && (tdist > 1500 || b->tgt == -2);
						if (sprint && b->pl.sprinting && d > 900 && b->slide_cd <= 0 && frand() < dt * 0.12f) { b->pl.in_crouch = 1; b->crouch_t = 0; b->slide_cd = 8 + frand() * 6; }   // a slide on a long straight run
						// low cover or a ledge in the way, with the waypoint beyond it: vault / climb (the physics' jump does it)
						if (!virt && b->pl.grounded && my > 0.9f && b->pl.slide_t <= 0 && (b->ledge_t -= dt) <= 0) {
							V3 ld = vsub(b->pl.pos, lis_pos); b->ledge_t = vdot(ld, ld) < 5000.f * 5000.f ? 0.25f : 1.0f;
							V3 fd = v3(cosf(b->pl.yaw), sinf(b->pl.yaw), 0);
							if (raycast_mask(v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 50), fd, P_RADIUS + 60, TF_PLAYER, NULL) >= 0) {
								V3 dest; float peak; int k = player_ledge(&b->pl, fd, &dest, &peak);
								float ex = w[0] - dest.x, ey = w[1] - dest.y;
								if (k && sqrtf(ex * ex + ey * ey) < d - 60) { jump = 1; b->jump_cd = 1.0f; }
							}
						}
						if (!virt) { b->prog_t += dt; if (d < b->prog_d - 25) { b->prog_d = d; b->prog_t = 0; } }
						if (!virt && b->prog_t > 0.5f && b->jump_cd <= 0 && b->pl.grounded) { jump = 1; b->jump_cd = 1.0f; }
						if (!virt && b->prog_t > 3.5f && ++b->stuck_n >= 3) { bot_stuck_respawns++;
#ifdef BOT_DEBUG_STUCK
						printf("stuck: bot %d frame %d at %.0f %.0f %.0f pend %d npath %d ipath %d waited %d goal %.0f %.0f %.0f wp %.0f %.0f\n", i, bot_frame, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z, b->nav_pend, b->npath, b->ipath, bot_frame - b->nav_ask_frame, b->goal[0], b->goal[1], b->goal[2], w[0], w[1]);
#endif
						bot_spawn(b, T); S->skip = 1; continue; }   // hopelessly stuck: start over somewhere else
						if (!virt && b->prog_t > 3.5f) { b->ipath++; b->prog_d = 1e9f; b->prog_t = 0; if (b->ipath >= b->npath) b->repath_t = 0; }
					}
				}
			}
		}
		if (bot_sk_move && b->state != BS_DEAD) bot_sk_move(i, &mx, &my, &sprint, &jump);   // skills: rush, flee, hacked ...
		if (b->state == BS_DEAD || bot_freeze || b->grabbed || b->ex_on) { mx = my = 0; jump = sprint = 0; }
		S->mx = mx; S->my = my; S->sprint = sprint; S->jump = jump;
	}
	// ---- 2: physics (the player's own movement code), the light under foot and the floor height: on the workers
	{ CP_T0(tp_); bot_step_dt = dt; jobs_parallel_for(bot_phys_job, NULL, nbot + (BOT_NAV_ASYNC ? 1 : 0)); CP_ADD(CP_BOT_PHYS, tp_); }   // (item 0: the path searches)
	// ---- 3: animation state (chr.h) and its events (sounds), in bot order
	SceUInt64 ta0 = sceKernelGetProcessTimeWide();
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; BotStep *S = &bot_step[i]; ChrType *C = &chrt[b->ct]; S->pose = 0; if (S->skip) continue;
		if (b->pl.pos.z < col_zmin - 500 && b->state != BS_DEAD && !b->netp) { b->state = BS_DEAD; b->dead_t = 4.9f; }
		// ---- animation (chr.h): the movement state of the physics drives the original's locomotion / action layers
		{
			float cy = cosf(b->pl.yaw), sy = sinf(b->pl.yaw);
			CInput in; memset(&in, 0, sizeof(in));
			in.vf = b->pl.vel.x * cy + b->pl.vel.y * sy; in.vr = b->pl.vel.x * sy - b->pl.vel.y * cy;
			float yr = wrap_pi(b->pl.yaw - b->prev_yaw) / (dt > 1e-4f ? dt : 1e-4f); b->prev_yaw = b->pl.yaw;
			b->yaw_rate += (yr - b->yaw_rate) * fminf(1, 12 * dt); in.yaw_rate = b->yaw_rate;
			in.grounded = b->pl.grounded || b->pl.mantle_t > 0; in.crouch = b->pl.crouched && b->pl.slide_t <= 0; in.sprint = b->pl.sprinting; in.slide = b->pl.slide_t > 0;
			in.jump = (b->pl.events & PE_JUMP) != 0; in.air_t = b->air_t;
			if (b->pl.mantle_t > 0) { in.mantle = b->pl.mantle_kind; in.mantle_k = 1 - b->pl.mantle_t / (b->pl.mantle_len > 0 ? b->pl.mantle_len : 1); }
			float zt = b->netc || b->netp ? (b->net_ads ? 1.f : 0.f) : b->state == BS_ENGAGE && b->sees ? 1.f : 0.f; b->zoom += (zt - b->zoom) * fminf(1, 5 * dt); in.zoom = b->zoom;
			in.aim_pitch = b->aim_pitch;
			// Demolition: planting (install) / defusing (clear) while the game holds the bot on the site; the weapon is put away.
			// A hold that starts with the player right behind the bot is a stealth kill (weapons: the knife): the victim's grab.
			if (b->hold && !b->hold_was && b->state != BS_DEAD) {
				V3 q = vsub(bot_player_pos, b->pl.pos); float qd = sqrtf(q.x * q.x + q.y * q.y);
				if (b->grabbed || (qd < 230 && q.x * cosf(b->pl.yaw) + q.y * sinf(b->pl.yaw) < -0.3f * qd)) bot_grab(i);
				else { b->hold_kind = bot_hold_defuse && bot_hold_defuse(i) ? 2 : 1;
					ca_play(&b->ca.up, ca_c(&b->ca, b->hold_kind == 2 ? S_DEFUSEST : S_PLANTST), 0.2f, 0.2f, CA_UPPER, 1); b->ca.up.hold = 1; b->wpn_hide = 1e9f; } }
			else if (!b->hold && b->hold_was && b->state != BS_DEAD) {
				if (b->hold_kind == 3) bot_release(i);
				else { ca_play(&b->ca.up, ca_c(&b->ca, b->hold_kind == 2 ? S_DEFUSEEND : S_PLANTEND), 0.05f, 0.2f, CA_UPPER, 1);
					b->wpn_hide = ca_len(&b->ca, b->hold_kind == 2 ? S_DEFUSEEND : S_PLANTEND); } }
			// skills (skills.h via main.c): the activation gesture when the bot's personal skill goes active
			if (bot_sk_active_t && b->state != BS_DEAD) { float st = bot_sk_active_t(i); if (st >= 0 && (b->sk_t < 0 || st < b->sk_t) && !b->grabbed && b->melee_t < 0) bot_anim_skill(i); b->sk_t = st; }
			b->hold_was = b->hold && b->state != BS_DEAD; if (b->wpn_hide > 0 && b->wpn_hide < 1e8f) { b->wpn_hide -= dt; if (b->wpn_hide <= 0) ca_upper(&b->ca, S_SWAPEND, 0.05f, 0.2f, 1); }
			if (b->pl.events & (PE_JUMP | PE_LAND | PE_SLIDE)) b->fmat = player_floor_mat(&b->pl);   // the material ray only when a foley sound plays
			if (b->pl.events & PE_JUMP || (b->pl.events & PE_LAND && b->pl.land_vz < -250)) {   // take-off / landing foley (the original's 3rd-person sets)
				float hs = sqrtf(in.vf * in.vf + in.vr * in.vr); const char *k = b->pl.events & PE_JUMP ? (hs > 450 ? "fastrun_jump" : hs > 280 ? "run_jump" : "jump") : (hs > 450 ? "fastrun_land" : hs > 280 ? "run_land" : "land");
				snd_foley(snd_chr_female(C->name) ? 2 : 1, k, b->fmat, &b->pl.pos.x); }
			if (b->pl.events & PE_VAULT) ev_play("foley/common/s_jump_3rd", &b->pl.pos.x);
			if (b->pl.events & PE_SLIDE) snd_foley(snd_chr_female(C->name) ? 2 : 1, "slide_dash", b->fmat, &b->pl.pos.x);
			// far bots: the animation advances every frame, the pose is rebuilt every other frame (as their skinning)
			V3 dl = vsub(b->pl.pos, lis_pos); int far_ = vdot(dl, dl) > 2500.f * 2500.f;
			ca_update(&b->ca, &in, dt);
			b->pose_static = b->pose && b->state == BS_DEAD && b->ca.death.clip >= 0 && b->ca.death.w >= 0.999f && b->ca.death.t >= b->ca.B->clip[b->ca.death.clip].len + 0.1f;
			int offscreen = bot_frame - b->seen_frame > 2;   // not drawn lately: only the hit-test points need the pose (every 4th frame)
			S->pose = !b->pose_static && (!b->pose || (offscreen ? ((bot_frame + i) & 3) == 0 : !far_ || ((bot_frame + i) & 1)));   // (evaluated in phase 4)
			S->in = in;
			bot_anim_n++;
		}
	}
	CP_ADD(CP_BOT_ANIM, ta0);
	// ---- 4: pose evaluation and the world points (hit tests, effects): on the workers
	{ CP_T0(tq_); jobs_parallel_for(bot_pose_job, NULL, nbot); CP_ADD(CP_BOT_POSE, tq_); }
	bot_anim_us += (long long)(sceKernelGetProcessTimeWide() - ta0);
	// ---- 5: corpses
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; if (bot_step[i].skip) continue;
		// a corpse that slid off an edge drops to the floor under its pelvis (a cheap stand-in for the original's ragdoll)
		if (b->state == BS_DEAD && b->pose && ((i + bot_frame) % 4 == 0 || b->death_vz != 0)) {
			float h = raycast(v3(b->pelvis.x, b->pelvis.y, b->pl.pos.z + 60), v3(0, 0, -1), 800, NULL, NULL);
			float below = h > 0 ? h - 60 : 0;   // floor under the pelvis, below the feet level
			if (below > b->death_drop + 3) { b->death_vz += 1400 * dt; b->death_drop = fminf(below, b->death_drop + b->death_vz * dt); } else b->death_vz = 0;
		}
	}
	// keep bots and the player apart (simple push)
	for (int i = 0; i < nbot; i++) for (int j = -1; j < nbot; j++) {
		if (j == i) continue; Bot *a = &bots[i]; if (a->state == BS_DEAD || a->netc || a->netp || a->grabbed || a->ex_on) continue;   // (net: the net code places them; b70: an execution's pair stays put)
		V3 o = j < 0 ? T->pos : bots[j].pl.pos; if (j >= 0 && (bots[j].state == BS_DEAD || bots[j].grabbed || bots[j].ex_on)) continue;
		float dx = a->pl.pos.x - o.x, dy = a->pl.pos.y - o.y, d2 = dx * dx + dy * dy;
		if (d2 < 65 * 65 && fabsf(a->pl.pos.z - o.z) < 150 && d2 > 1e-2f) { float d = sqrtf(d2), push = (65 - d) * (j < 0 ? 1.0f : 0.5f); a->pl.pos.x += dx / d * push; a->pl.pos.y += dy / d * push; }
	}
	CP_ADD(CP_BOTS, tb_);
}

// ray vs. bots (player bullets). Returns bot index or -1; *t distance, *head 1 for a head hit.
static float ray_sphere(V3 o, V3 d, V3 c, float r) {
	V3 m = vsub(o, c); float b = vdot(m, d), cc = vdot(m, m) - r * r; if (cc > 0 && b > 0) return -1;
	float disc = b * b - cc; if (disc < 0) return -1; float t = -b - sqrtf(disc); return t < 0 ? 0 : t;
}
static float ray_capsule(V3 o, V3 d, V3 a, V3 b, float r) {  // sampled along the segment: good enough for hit tests
	float best = -1; for (int k = 0; k <= 6; k++) { V3 c = vadd(a, vmul(vsub(b, a), k / 6.f)); float t = ray_sphere(o, d, c, r); if (t >= 0 && (best < 0 || t < best)) best = t; }
	return best;
}
static int bots_hit_test(V3 o, V3 d, float maxd, float *t, int *head) {
	int hit = -1; float bt = maxd;
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; if (b->state == BS_DEAD || b->team == 0) continue;
		V3 c = vsub(b->chest, o); if (vdot(c, d) < -100 || vdot(c, c) > (bt + 200) * (bt + 200)) continue;
		float th = ray_sphere(o, d, b->head, 15);
		V3 low = v3(b->pelvis.x, b->pelvis.y, b->pl.pos.z + 22);
		float tb = ray_capsule(o, d, low, b->chest, 26);
		float tt = -1; int hh = 0;
		if (th >= 0 && (tb < 0 || th <= tb + 10)) { tt = th; hh = 1; } else if (tb >= 0) tt = tb;
		if (tt >= 0 && tt < bt) { bt = tt; hit = i; *head = hh; }
	}
	if (hit >= 0) *t = bt;
	return hit;
}
// nearest living bot the eye can see (autoplay aiming)
static int bots_visible_nearest(V3 eye, float maxd) {
	int best = -1; float bd = maxd;
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (b->state == BS_DEAD || b->team == 0) continue;
		V3 d = vsub(b->chest, eye); float l = sqrtf(vdot(d, d)); if (l < bd && bot_los(eye, b->chest)) { bd = l; best = i; } }
	return best;
}

// what bot b holds in the right hand now: the knife (slash), a grenade (throw), its gun, or nothing (put away, dropped)
static const Model *bot_hand_model(const Bot *b) {
	if (bot_melee_only && bot_melee_only((int)(b - bots)) && bot_w3_knife_ok > 0) return &bot_w3_knife;   // b107: the cyborgs' blade is the knife's, as the player's (weapon.h; b70 drew the katar) - on the arm, the corpse keeps it
	if (bot_melee_only && bot_melee_only((int)(b - bots)) && bot_w3_axe_ok > 0) return &bot_w3_axe;
	if ((b->knife_t > 0 && b->melee_t >= 0) || (bot_melee_only && bot_melee_only((int)(b - bots)) && b->state != BS_DEAD)) return bot_w3_knife_ok > 0 ? &bot_w3_knife : NULL;   // (b63: the cyborgs' blade)
	if (b->gren_t > 0) return bot_w3_gren_ok > 0 ? &bot_w3_gren : NULL;
	if (bot_w3_ok[b->wpn] && b->wpn_hide <= 0 && !(b->state == BS_DEAD && b->ca.death.t > 0.4f)) return &bot_w3[b->wpn];
	return NULL;
}
// b71: bot b wears the enemy mask (the other team, the option on, a mask for its skeleton and an offset on its model)
static int bot_masked(const Bot *b) {
	const ChrType *C = &chrt[b->ct]; return bot_mask_on && b->team == 1 && b->pose && C->mask_at && C->head >= 0 && bot_mask_ok[C->female & 1] > 0 && b->xoff != 1;   // (b80: not once the head is blown off: it flew with it)
}
// the mask on its head bone at the operator's Head_Attach offset (vm/make_masks.py); draw with md_masked set (its parts)
static void bot_mask_draw(const Bot *b, const float *mvp, const float *ld, const float *tint, const float *fogc) {
	const ChrType *C = &chrt[b->ct]; MBone B = b->pose[C->head]; float o[3]; quat_rotate(B.q, C->mask_at, o);
	for (int k = 0; k < 3; k++) B.p[k] += o[k];
	chr_draw_rigid(&bot_mask[C->female & 1], mvp, &B, ld, tint, fogc);
}
// soft round shadows under the bots (drawn before them, depth-tested, no depth write)
static void bots_shadows(const float *VP) {
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (!b->pose) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 4000.f * 4000.f) continue;
		float hgt = b->pl.pos.z - b->floor_z; if (hgt > 250) continue; float a = 0.5f * (1 - hgt / 250);
		float lum = (b->lit[0] + b->lit[1] + b->lit[2]) / 3; a *= fminf(1, 0.35f + lum);   // fainter in the dark, where there is little light to block
		const float col[4] = { 1, 1, 1, a };
		if (b->state == BS_DEAD) sprite_ground(b->pelvis.x, b->pelvis.y, b->floor_z - b->death_drop + 3, 95, 50, b->pl.yaw, col); else sprite_ground(b->pl.pos.x, b->pl.pos.y, b->floor_z + 3, 48, 42, b->pl.yaw, col); }
	sprite_flush(VP, sp_blob, 0);
}
// character shadows: near bots are drawn once more, flattened onto the floor under them along the light,
// in a flat dark colour; the stencil keeps each bot's shadow from darkening the same pixel twice
// (uses the skinning bots_draw did this frame: call it after bots_draw)
static void bots_planar_shadows(const float *VP, void (*mul)(float *, const float *, const float *)) {
	static const float L[3] = { 0.30f, 0.22f, 1.0f };   // towards the light (overcast sky: mostly from above)
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); md_shadow = 1;
	int ref = 0;
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; if (!b->pose || !b->sk.ok || b->sk.cur < 0) continue; ChrType *C = &chrt[b->ct];
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 2200.f * 2200.f) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;
		float hgt = b->pl.pos.z - b->death_drop - b->floor_z; if (hgt > 200) continue;
		float lum = (b->lit[0] + b->lit[1] + b->lit[2]) / 3, a = 0.42f * (1 - hgt / 200) * fminf(1, 0.35f + lum);
		float cx = b->pl.pos.x, cy_ = b->pl.pos.y, cz = b->floor_z, q[4];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * cx + VP[4 + r] * cy_ + VP[8 + r] * cz + VP[12 + r];
		if (q[3] < -150 || q[0] > q[3] + 300 || q[0] < -q[3] - 300 || q[1] > q[3] + 300 || q[1] < -q[3] - 300) continue;
		float h = b->floor_z + 1.5f, kx = L[0] / L[2], ky = L[1] / L[2];
		// world -> floor plane along L (column-major)
		float S[16] = { 1, 0, 0, 0, 0, 1, 0, 0, -kx, -ky, 0, 0, kx * h, ky * h, h, 1 };
		float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
		float Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z - b->death_drop, 1 }, SM[16], MVP[16];
		mul(SM, S, Mw); mul(MVP, VP, SM);
		ref = (ref + 1) & 255; if (!ref) { glClear(GL_STENCIL_BUFFER_BIT); ref = 1; }
		glStencilFunc(GL_GREATER, ref, 0xFF);   // draw where this bot's number > stencil (not yet shadowed by this bot), then mark it
		const float tint[4] = { 0, 0, 0, a }, nofog[4] = { 0, 0, 0, 0 }, ld[4] = { 0, 0, 1, 0 };
		md_masked = bot_masked(b);   // (b71: the mask's outline, the hair it covers left out)
		chr_draw(&C->m, &b->sk, chr_gpu_skin ? 2 : b->lod, MVP, b->pose, ld, tint, nofog, 0);   // (GPU skinning: the flat shadow uses the lightest LOD)
		if (md_masked) bot_mask_draw(b, MVP, ld, tint, nofog);
		md_masked = 0;
		{ const Model *hm = b->state != BS_DEAD ? bot_hand_model(b) : NULL; if (hm) chr_draw_rigid(hm, MVP, &b->ca.pose[b->ca.B->grip], ld, tint, nofog); }
	}
	md_shadow = 0; glDisable(GL_STENCIL_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
}
// draw all bots. VP: column-major view-projection. LOD by on-screen height: the TPS model's LODs nearest 6000 vertices
// above ~150 px (within ~4.7 m at the 70 deg view), 3500 above ~70 px (~10 m), 1300 below (chr.h); far bots are re-skinned
// every other frame. Two passes (b35): every visible bot that needs skinning is queued first and skinned in one go on all
// cores (skin.h), then all are drawn from their buffers.
static void bots_draw(const float *VP, const float *fogc, void (*mul)(float *, const float *, const float *)) {
	bot_frame++;
	const float Lw[3] = { 0.33f, 0.42f, 0.84f };
	float vpy = sqrtf(VP[1] * VP[1] + VP[5] * VP[5] + VP[9] * VP[9]);   // the projection's vertical scale (zoom aware)
	static float Mb[NBOT_MAX][16]; static unsigned char vis[NBOT_MAX];
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; vis[i] = 0; if (!b->pose || !b->sk.ok) continue; ChrType *C = &chrt[b->ct];
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 7000.f * 7000.f) continue;
		// rough frustum test on a sphere around the body
		float cx = b->pl.pos.x, cy_ = b->pl.pos.y, cz = b->pl.pos.z + 90, q[4];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * cx + VP[4 + r] * cy_ + VP[8 + r] * cz + VP[12 + r];
		if (q[3] < -150 || q[0] > q[3] + 260 || q[0] < -q[3] - 260 || q[1] > q[3] + 400 || q[1] < -q[3] - 400) continue;
		float px = q[3] > 1 ? 180 * vpy / q[3] * 272 : 1e9f;
		int lod = px > 150 ? 0 : px > 70 ? 1 : 2;   // host renders: LOD 1 matches LOD 0 below ~200 px, LOD 2 matches LOD 1 below ~100 px
		if (lod != b->lod) b->sk.cur = -1;   // a new LOD: skin now
		b->lod = lod; b->seen_frame = bot_frame; vis[i] = 1;
		float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
		float M[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z - b->death_drop, 1 }; memcpy(Mb[i], M, sizeof(M));
		// far bots rebuild their pose every other frame (bots_update): skinned then; a new LOD re-skins anyway (sk.cur = -1)
		if (b->pose_new || b->sk.cur < 0) chr_skin_queue(&C->m, &b->sk, lod, b->pose);
		b->pose_new = 0;
	}
	skin_run();   // (nothing queued: returns at once)
	for (int i = 0; i < nbot; i++) {
		if (!vis[i]) continue; Bot *b = &bots[i]; ChrType *C = &chrt[b->ct]; const float *M = Mb[i];
		float MVP[16]; mul(MVP, VP, M);
		float c = M[0], s = M[1], ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
		float tint[4] = { 1, 1, 1, 1 }; for (int k = 0; k < 3; k++) tint[k] = fminf(1.5f, fmaxf(0.18f, b->lit[k] * 1.15f)) * (b->state == BS_DEAD ? 0.8f : 1);   // lit like the floor under them
		if (bot_vis_body && bot_vis_body(i, MVP, M, ld, tint, fogc, 0)) continue;   // vision.h: thermoptic camo looks (it draws the body itself)
		float ga_ = bot_sk_alpha ? bot_sk_alpha(i) : 1.f; if (ga_ <= 0) continue;   // cloaked: a faint additive ghost (skills.h)
		if (ga_ < 1) { tint[0] = 0.55f; tint[1] = 0.65f; tint[2] = 0.75f; tint[3] = ga_; glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		md_rim = BOT_RIM;   // b62: the rim light (model.h MD_VS_SPEC): the operators' outlines catch the light, as in the footage
		md_emis_k = ga_ < 1 ? 0.f : 1.f;   // b71: no glow on a cloaked ghost
		md_masked = bot_masked(b);         // b71: the enemy team's mask (hair / goggles under it hidden)
		chr_draw(&C->m, &b->sk, b->lod, MVP, b->pose, ld, tint, fogc, 0);
		if (md_masked) bot_mask_draw(b, MVP, ld, tint, fogc);
		md_masked = 0; md_emis_k = 1;
		// the weapon in the right hand (dropped from the hand in the death clips: hidden once the bot is down)
		{ const Model *hm = bot_hand_model(b); if (hm) chr_draw_rigid(hm, MVP, &b->ca.pose[b->ca.B->grip], ld, tint, fogc); }
		md_rim = 0;
		if (ga_ < 1) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	}
}
// a short description of a bot's animation for the logs: the heaviest locomotion clip, + the upper-body action
static const char *bot_anim_name(const Bot *b) {
	static char s[64]; const CAnim *A = &b->ca; if (!A->B) return "-";
	const char *base = A->dead && A->death.clip >= 0 ? A->B->clip[A->death.clip].name : A->full.clip >= 0 && A->full.w > 0.3f ? A->B->clip[A->full.clip].name :
		A->mantle_w > 0.5f ? (A->mantle_kind == 2 ? "vault" : "climb") : A->slide_w > 0.5f ? "slide" : A->air_state ? (A->air_state == 1 ? "jump" : "fall") : A->move_w > 0.5f && A->dom_clip >= 0 ? A->B->clip[A->dom_clip].name : A->crouch_w > 0.5f ? "c_idle" : "idle";
	snprintf(s, sizeof(s), "%s%s%s", base, A->up.clip >= 0 ? "+" : "", A->up.clip >= 0 ? A->B->clip[A->up.clip].name : "");
	return s;
}

// ---- chars b35: is the GPU palette path right on this device? (skin_gpu.h). Call once after the characters (and the
// first viewmodel) are loaded and before any bot / tank gets its skinning buffers (bots_init): logs the model shader's
// uniform layout, draws two characters in two poses (and the viewmodel in two clips) both ways and picks each class's path.
#include "skin_gpu.h"
static void skin_gpu_select(const char *dir, const Model *vm, void (*log)(const char *, ...)) {
	skin_gpu_read_cfg(dir); skin_gpu_dump_program(md_vs, log);
	if (skin_gpu_force < 0) {
		int done_f[2] = { 0, 0 };
		for (int ct = 0; ct < nchrt; ct++) {
			ChrType *C = &chrt[ct]; if (done_f[C->female]) continue; done_f[C->female] = 1;
			for (int k = 0; k < 2; k++) {
				CAnim A; ca_init(&A, &chr_bank[C->female], 0); CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
				if (k == 0) { in.vf = 335; in.vr = 120; in.zoom = 1; in.aim_pitch = 0.35f; } else { in.vf = -175; in.crouch = 1; in.zoom = 1; in.aim_pitch = -0.3f; }
				for (int f = 0; f < 24; f++) ca_update(&A, &in, 1 / 30.f);
				MBone pm[64]; cb_to_model(&C->bind, ca_pose(&A, &in), pm);
				float cd = 0, mis = skin_gpu_test(&C->m, pm, 0, &cd); char what[64]; snprintf(what, sizeof(what), "%s, %s", C->name, k ? "crouch-walking back" : "running, aiming up");
				skin_gpu_note(SKN_CHR, what, mis, cd, log); skin_gpu_note(SKN_MODEL, what, mis, cd, log);   // (the same models and shader)
			}
		}
		if (vm && vm->ok && vm->nb <= 64) {
			static const char *const cl[2][3] = { { "sprint", "run", "idle" }, { "reload", "fire", "idle" } };
			for (int k = 0; k < 2; k++) {
				int ci = -1; for (int j = 0; j < 3 && ci < 0; j++) ci = model_find_clip(vm, cl[k][j]); if (ci < 0) continue;
				MBone pm[64]; model_sample(vm, ci, model_clip_len(vm, ci) * 0.4f, pm);
				float cd = 0, mis = skin_gpu_test(vm, pm, 1, &cd); char what[64]; snprintf(what, sizeof(what), "clip %s", vm->clip[ci].name);
				skin_gpu_note(SKN_VM, what, mis, cd, log);
			}
		}
	}
	skin_gpu_decide(log);
}
