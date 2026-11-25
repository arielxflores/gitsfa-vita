// Bots: third-person Batou models animated with the game's own 3rd-person clips, moving with the
// player physics, finding their way on the map's AI navigation mesh, and fighting the player.
#pragma once
#include "player.h"
#include "audio.h"
#include "model.h"
#include "nav.h"
#include "light.h"
#include "sprite.h"

#define NBOT_MAX 8
enum { BS_ROAM, BS_ENGAGE, BS_DEAD };
static const char *bot_state_name[3] = { "roam", "fight", "dead" };
typedef struct {
	Player pl; Anim an; SkinCache sc; MBone *pose; int state; float hp;
	float path[48][3]; int npath, ipath; float repath_t, prog_d, prog_t, jump_cd; float goal[3]; int stuck_n;
	float think_t, see_t, lost_t, react; int sees; V3 last_seen; float hurt_t;
	int burst; float shot_cd, pause_t, strafe, strafe_t, reload_t; int mag;
	float aim_pitch, dead_t, step_d, flash_t, air_t; int fmat;
	V3 head, chest, pelvis, muzzle;   // world-space points from the last pose
	int kills, deaths, shots, hits;
	int ct;   // character type (chrt index)
	int team, tgt;   // team (0 = the player's side, 1 = enemies); current target: -1 the player, >= 0 a bot, -2 none
	int hold; float hold_yaw, hold_pitch;   // set by the game (Demolition): stand still facing this way (planting / defusing) unless fighting
	float lit[3], floor_z;   // light under foot (smoothed), ground height for the shadow
} Bot;
typedef struct { V3 pos, vel; int alive; } BotTarget;   // the player

// character types: one model (with its baked clips) per playable character; each bot is drawn as one of them
typedef struct {
	Model m; char name[24];
	int idle, walk[4], run[4], sprint, jump, fall, fire, reload, draw, hit, die[3];   // clip indices
	int pelvis, head, spine, spine2, grip; const float *muzzle;                       // bone indices, muzzle attachment
} ChrType;
#define NCHR_MAX 16
static ChrType chrt[NCHR_MAX]; static int nchrt;
static Bot bots[NBOT_MAX]; static int nbot, bot_frame;
static V3 lis_pos; static float lis_yaw;                  // listener (camera) for 3D sound
static float bot_dmg_to_player, bot_dmg_dir;              // filled by bots_update: damage dealt this frame and the world yaw it came from
static void (*bot_on_impact)(V3 p, V3 n, int mat);        // a bot bullet hit the map near the player
static void (*bot_on_shot)(V3 from, V3 to);                // every bot shot: muzzle and where the bullet ends (flash, tracer)
static int bot_last_hitter = -1;                           // the last bot that hit the player
static int (*bot_spawn_pick)(int team);                    // team spawn point (set by the game); NULL: anywhere away from the player
static void (*bot_on_kill)(int killer, int victim, int head);   // a bot died (killer -1 = the player)
static int bot_total_shots, bot_total_hits, bot_stuck_respawns;
static int bot_freeze, bot_no_respawn;                     // round start freeze; rounds without respawns (Demolition)
static int (*bot_goal)(int i, float *g);                    // the game's objective for bot i (where to walk); 0 = none
// operator skills (skills.h; set by the game, all NULL = no skills, bots behave exactly as before)
#define BOT_SK_HOOKS 1
static int (*bot_sk_can_see)(int i, int target);              // target -1 = the player, >= 0 a bot; 0 = it can't see it (camo, hacked)
static int (*bot_sk_block)(int i, V3 from, V3 to, V3 *end);   // 1 = the shot is stopped (shield / cyber wall / deployable) at *end
static float (*bot_sk_aim)(int i);                            // hit-chance factor (EMP 0.5); <= 0: no firing (hacked, placing, diving, rushing)
static int (*bot_sk_goal)(int i, float *g);                   // a roam goal from skills (enemies revealed by heat scan / recon / wall)
static void (*bot_sk_move)(int i, float *mx, float *my, int *sprint, int *jump);   // movement override (rush, flee EMP / drone, hacked ...)
static float (*bot_sk_alpha)(int i);                          // draw: 1 normal, (0, 1) cloaked ghost (additive), 0 hidden
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
	float h = raycast(a, vmul(d, 1 / l), l, NULL, NULL); return h < 0 || h > l - 25;
}
static V3 bot_eye(const Bot *b) { return v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 150); }

static void bot_repath(Bot *b, const float *goal) {
	float s[3] = { b->pl.pos.x, b->pl.pos.y, b->pl.pos.z }; memcpy(b->goal, goal, 12);
	b->npath = nnav ? nav_path(s, goal, b->path, 48, 45) : 0; b->ipath = 0; b->repath_t = 4 + frand() * 2; b->prog_d = 1e9f; b->prog_t = 0;
	for (int k = 0; k < 4 && nnav && !b->npath && nspawn; k++) { // can't get there from here: wander to a spawn point we can reach
		int si = rand() % nspawn; float g2[3] = { spawns[si].x, spawns[si].y, spawns[si].z }; memcpy(b->goal, g2, 12);
		b->npath = nav_path(s, g2, b->path, 48, 45); }
	if (!b->npath) { memcpy(b->path[0], goal, 12); b->npath = 1; }   // no mesh: walk straight
}
// spawn far from (and out of sight of) the player
static void bot_spawn(Bot *b, const BotTarget *T) {
	ChrType *C = &chrt[b->ct];
	int cand[64], nc = 0; V3 pe = v3(T->pos.x, T->pos.y, T->pos.z + 140);
	for (int i = 0; i < nspawn && nc < 64; i++) {
		V3 s = v3(spawns[i].x, spawns[i].y, spawns[i].z); float dx = s.x - T->pos.x, dy = s.y - T->pos.y;
		if (dx * dx + dy * dy < 2500 * 2500) continue;
		if (bot_los(v3(s.x, s.y, s.z + 150), pe)) continue;
		cand[nc++] = i;
	}
	int si = nc ? cand[rand() % nc] : rand() % nspawn;
	if (bot_spawn_pick) { int s2 = bot_spawn_pick(b->team); if (s2 >= 0) si = s2; }
	memset(&b->pl, 0, sizeof(b->pl)); b->pl.pos = v3(spawns[si].x, spawns[si].y, spawns[si].z + 5); b->pl.yaw = spawns[si].yaw;
	b->hp = 100; b->state = BS_ROAM; b->mag = 50; b->reload_t = 0; b->see_t = 0; b->sees = 0; b->hurt_t = 0; b->npath = 0; b->burst = 0; b->pause_t = 0;
	b->aim_pitch = 0; b->air_t = 0; b->think_t = frand() * 0.2f; b->jump_cd = 0; b->stuck_n = 0; b->tgt = -2;
	anim_stop(&b->an); b->an.base = -1; anim_set_base(&b->an, C->idle, 0, 0); anim_play(&b->an, C->draw, 0.01f, 0.2f, 1);
	light_at(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 20, b->lit); b->floor_z = b->pl.pos.z;
	float g[3] = { T->pos.x, T->pos.y, T->pos.z };
	if (b->team == 0) { for (int k = 0; k < 8; k++) { int j = rand() % (nbot ? nbot : 1); if (j < nbot && bots[j].team != b->team) { g[0] = bots[j].pl.pos.x; g[1] = bots[j].pl.pos.y; g[2] = bots[j].pl.pos.z; break; } } }
	bot_repath(b, g);
}
// load one character model; returns its type index or < 0
static int chrt_load(const char *path, const char *name) {
	if (nchrt >= NCHR_MAX) return -3;
	ChrType *C = &chrt[nchrt]; memset(C, 0, sizeof(*C));
	model_keep_cpu = 1; int r = model_load(&C->m, path); model_keep_cpu = 0; if (r < 0) return r;
	model_prepare_rigid(&C->m); Model *M = &C->m;
	static const char *dirs[4] = { "f", "b", "l", "r" };
	for (int k = 0; k < 4; k++) { char nm[16]; sprintf(nm, "walk_%s", dirs[k]); C->walk[k] = model_find_clip(M, nm); sprintf(nm, "run_%s", dirs[k]); C->run[k] = model_find_clip(M, nm); }
	C->idle = model_find_clip(M, "idle"); C->sprint = model_find_clip(M, "sprint"); C->jump = model_find_clip(M, "jump"); C->fall = model_find_clip(M, "fall");
	C->fire = model_find_clip(M, "fire"); C->reload = model_find_clip(M, "reload"); C->draw = model_find_clip(M, "draw"); C->hit = model_find_clip(M, "hit");
	C->die[0] = model_find_clip(M, "die_head"); C->die[1] = model_find_clip(M, "die_body"); C->die[2] = model_find_clip(M, "die_body2");
	C->pelvis = model_find_bone(M, "Bip01_Pelvis"); C->head = model_find_bone(M, "Bip01_Head"); C->spine = model_find_bone(M, "Bip01_Spine");
	C->spine2 = model_find_bone(M, "Bip01_Spine2"); C->grip = model_find_bone(M, "Grip"); C->muzzle = model_attach(M, "muzzle");
	snprintf(C->name, sizeof(C->name), "%s", name);
	if (C->idle < 0 || C->pelvis < 0 || C->head < 0) return -2;
	return nchrt++;
}
static void bot_set_char(Bot *b, int ct) {
	if (b->an.m) anim_free(&b->an);
	if (b->sc.ok) skincache_free(&b->sc);
	b->ct = ct; anim_init(&b->an, &chrt[ct].m); skincache_init(&b->sc, &chrt[ct].m); b->pose = NULL;
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
static void bot_fire(Bot *b, const BotTarget *T) {
	ChrType *C = &chrt[b->ct];
	b->mag--; b->shots++; bot_total_shots++; b->flash_t = 0.05f;
	anim_play(&b->an, C->fire, 0.03f, 0.15f, 1);
	snd_wfire_tp("m4a1", &b->muzzle.x);   // near / distant layers and panning as the original's event
	if (b->tgt <= -3) {   // at an external target (a Think Tank): rifle bullets, the target applies its own armour
		V3 xp, pc; if (!bot_ext_get || !bot_ext_get(-3 - b->tgt, b->team, &xp, &pc)) return;
		V3 d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d)) + 1, p = 0.6f - dist / 8000; if (p < 0.15f) p = 0.15f; if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
		V3 end = vadd(b->muzzle, vmul(d, 1.6f));
		if (frand() < p) { end = pc; if (bot_ext_hit) bot_ext_hit(-3 - b->tgt, (int)(b - bots), 24, pc); }
		if (bot_on_shot) bot_on_shot(b->muzzle, end);
		return;
	}
	if (b->tgt >= 0) {   // at another bot
		Bot *v = &bots[b->tgt]; V3 pc = v->chest, d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d)) + 1, spd = sqrtf(v->pl.vel.x * v->pl.vel.x + v->pl.vel.y * v->pl.vel.y);
		float p = 0.45f - dist / 6000; if (p < 0.06f) p = 0.06f; if (spd > 300) p *= 0.6f; p *= fminf(1, 0.3f + b->see_t * 0.3f); if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
		V3 end = vadd(b->muzzle, vmul(d, 1.6f)); if (bot_sk_block && bot_sk_block((int)(b - bots), b->muzzle, pc, &end)) { if (bot_on_shot) bot_on_shot(b->muzzle, end); return; }
		if (frand() < p) { b->hits++; end = pc; bot_hurt(b->tgt, 9, frand() < 0.1f, (int)(b - bots)); }
		else { V3 side = v3(-d.y / dist, d.x / dist, 0); end = vadd(pc, vadd(vmul(side, (frand() - 0.5f) * 120), v3(0, 0, (frand() - 0.3f) * 100))); }
		if (bot_on_shot) bot_on_shot(b->muzzle, end);
		return;
	}
	V3 pc = v3(T->pos.x, T->pos.y, T->pos.z + 115); V3 d = vsub(pc, b->muzzle); float dist = sqrtf(vdot(d, d));
	float spd = sqrtf(T->vel.x * T->vel.x + T->vel.y * T->vel.y);
	float p = 0.45f - dist / 6000; if (p < 0.06f) p = 0.06f; if (spd > 300) p *= 0.6f; p *= fminf(1, 0.3f + b->see_t * 0.3f); if (bot_sk_aim) p *= bot_sk_aim((int)(b - bots));
	V3 end = vadd(b->muzzle, vmul(d, 1.6f)); if (bot_sk_block && bot_sk_block((int)(b - bots), b->muzzle, pc, &end)) { if (bot_on_shot) bot_on_shot(b->muzzle, end); return; }
	if (frand() < p) { b->hits++; bot_total_hits++; bot_dmg_to_player += 8; bot_dmg_dir = atan2f(b->pl.pos.y - T->pos.y, b->pl.pos.x - T->pos.x); bot_last_hitter = (int)(b - bots); end = pc; }
	else if (dist < 3000 && bot_on_impact) { // a near miss: the bullet hits something around the player
		if (frand() < 0.5f) ev_play(frand() < 0.5f ? "impact/bullet_fly_l" : "impact/bullet_fly_h", NULL);   // 2D in the original
		V3 side = v3(-d.y / dist, d.x / dist, 0); float o1 = (frand() - 0.5f) * 140, o2 = (frand() - 0.3f) * 120;
		V3 aim = vadd(pc, vadd(vmul(side, o1), v3(0, 0, o2))); V3 dd = vsub(aim, b->muzzle); float l = sqrtf(vdot(dd, dd)); dd = vmul(dd, 1 / l);
		int mat = 0; V3 n; float h = raycast(b->muzzle, dd, l + 1500, &mat, &n);
		if (h > 0) { V3 hp = vadd(b->muzzle, vmul(dd, h)); V3 q = vsub(hp, T->pos); if (vdot(q, q) < 900 * 900) bot_on_impact(hp, n, mat); end = hp; }
	}
	if (bot_on_shot) bot_on_shot(b->muzzle, end);
}
// damage (killer: -1 the player, >= 0 a bot); returns 1 on a kill
static int bot_hurt(int i, float dmg, int head, int killer) {
	Bot *b = &bots[i]; if (b->state == BS_DEAD) return 0;
	ChrType *C = &chrt[b->ct];
	b->hp -= dmg; b->hurt_t = 1.5f;
	if (b->hp <= 0) {
		b->state = BS_DEAD; b->dead_t = 0; b->deaths++; anim_stop(&b->an);
		int dc = head ? C->die[0] : C->die[1 + rand() % 2]; if (dc < 0) dc = C->die[0];
		anim_set_base(&b->an, dc, 0.08f, 0); b->an.rate = 1;
		static const char *de[3] = { "foley/common/death_back", "foley/common/death_knee", "foley/common/death_spin" };
		ev_play_ex(ev_find(de[rand() % 3]), &b->pl.pos.x, -1, 0, snd_chr_female(C->name) ? 0.4f : 0.1f, 1.f);
		snd_voice(C->name, head ? "death_large_3rd" : "death_3rd", &b->pl.pos.x);
		if (killer >= 0) bots[killer].kills++;
		if (bot_on_kill) bot_on_kill(killer, i, head);
		return 1;
	}
	if (b->state != BS_ENGAGE) { b->state = BS_ENGAGE; b->see_t = 0; b->react = 0.25f; }
	if (!(b->an.act == C->fire && b->an.act_w > 0.3f) && b->reload_t <= 0) anim_play(&b->an, C->hit, 0.04f, 0.15f, 1);
	if (frand() < 0.4f) snd_voice(C->name, "damage_3rd", &b->pl.pos.x);
	return 0;
}
static int bots_damage(int i, float dmg, int head) { return bot_hurt(i, dmg, head, -1); }

static void bots_update(float dt, const BotTarget *T) {
	bot_dmg_to_player = 0;
	int save_floor = floor_mat;
	V3 pe = v3(T->pos.x, T->pos.y, T->pos.z + 140);
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; float mx = 0, my = 0; int jump = 0, sprint = 0; ChrType *C = &chrt[b->ct];
		if (b->hurt_t > 0) b->hurt_t -= dt; if (b->flash_t > 0) b->flash_t -= dt; if (b->jump_cd > 0) b->jump_cd -= dt;
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
					if (bot_los(bot_eye(b), ce) || (t == -1 && bot_los(bot_eye(b), v3(T->pos.x, T->pos.y, T->pos.z + 100)))) { best = t; bd = m; break; }
				}
				if (best != -2 && best != b->tgt) { b->tgt = best; b->see_t = 0; }
				b->sees = best != -2;
				if (b->tgt <= -3) bot_ext_get(-3 - b->tgt, b->team, &xp_, &xa_);
				tpos = b->tgt >= 0 ? bots[b->tgt].pl.pos : b->tgt <= -3 ? xp_ : T->pos; talive = b->tgt >= 0 || b->tgt <= -3 ? 1 : b->tgt == -1 ? T->alive : 0; tp = vsub(tpos, b->pl.pos); tdist = sqrtf(tp.x * tp.x + tp.y * tp.y);
				if (b->sees) { b->last_seen = tpos; b->lost_t = 0;
					if (b->state != BS_ENGAGE) { b->state = BS_ENGAGE; b->see_t = 0; b->react = 0.5f + frand() * 0.4f; b->burst = 0; b->pause_t = 0; b->strafe = frand() < 0.5f ? -1 : 1; b->strafe_t = 0.6f + frand(); } }
			}
			if (b->state == BS_ENGAGE) {
				if (b->sees) b->see_t += dt; else b->lost_t += dt;
				if (b->lost_t > 2.5f || !talive) { b->state = BS_ROAM; float g[3] = { b->last_seen.x, b->last_seen.y, b->last_seen.z }; bot_repath(b, g); }
			}
			// ---- movement
			if (b->state == BS_ENGAGE) {
				float want = atan2f(tp.y, tp.x); b->pl.yaw += wrap_pi(want - b->pl.yaw) * fminf(1, 7 * dt);
				b->strafe_t -= dt; if (b->strafe_t <= 0) { b->strafe = frand() < 0.5f ? -1 : 1; b->strafe_t = 0.5f + frand() * 1.5f; if (frand() < 0.25f) b->strafe = 0; }
				mx = b->strafe * 0.42f; my = tdist > 2600 ? 0.6f : tdist < 450 ? -0.4f : 0;
				if (!b->sees) { mx = 0; my = 0.5f; }   // lost sight: step toward the last seen spot
				float dz = (tpos.z + 115) - (b->pl.pos.z + 120); float wp = atan2f(dz, tdist + 1); b->aim_pitch += (wp - b->aim_pitch) * fminf(1, 8 * dt);
				// ---- shooting
				if (b->reload_t > 0) { b->reload_t -= dt; if (b->reload_t <= 0) b->mag = 50; }
				else if (b->sees && b->see_t > b->react && tdist < 4000 && !bot_freeze && !(bot_sk_aim && bot_sk_aim(i) <= 0)) {
					if (b->pause_t > 0) b->pause_t -= dt;
					else {
						if (b->burst <= 0) b->burst = 2 + rand() % 4;
						b->shot_cd -= dt;
						while (b->shot_cd <= 0 && b->burst > 0 && b->mag > 0) { b->shot_cd += 0.08f; b->burst--; bot_fire(b, T); }
						if (b->burst <= 0) { b->pause_t = 0.5f + frand() * 0.7f; b->shot_cd = 0; }
						if (b->mag <= 0) { b->reload_t = model_clip_len(&C->m, C->reload); anim_play(&b->an, C->reload, 0.15f, 0.25f, 1); snd_weapon("weapon/ar/m4a1", "reload_01", &b->pl.pos.x, 0); if (frand() < 0.3f) snd_voice(C->name, "reload_3rd", &b->pl.pos.x); }
					}
				}
			} else {
				b->aim_pitch *= 1 - fminf(1, 4 * dt);
				b->repath_t -= dt;
				if (b->hold) { b->pl.yaw += wrap_pi(b->hold_yaw - b->pl.yaw) * fminf(1, 6 * dt); b->aim_pitch += (b->hold_pitch - b->aim_pitch) * fminf(1, 6 * dt); b->npath = 0; b->repath_t = 0.5f; }
				else if (b->repath_t <= 0 || b->ipath >= b->npath) { float g[3] = { T->pos.x, T->pos.y, T->pos.z };
					if (!(bot_goal && bot_goal(i, g)) && !(bot_sk_goal && bot_sk_goal(i, g))) {
					{ int j0 = rand() % (nbot ? nbot : 1); for (int k = 0; k < nbot; k++) { int j = (j0 + k) % nbot; if (bots[j].team != b->team && bots[j].state != BS_DEAD && (b->team == 0 || frand() < 0.4f)) { g[0] = bots[j].pl.pos.x; g[1] = bots[j].pl.pos.y; g[2] = bots[j].pl.pos.z; break; } } }
					if (frand() < 0.3f && nspawn) { int s = rand() % nspawn; g[0] = spawns[s].x; g[1] = spawns[s].y; g[2] = spawns[s].z; } }
					bot_repath(b, g); }
				if (!b->hold && b->ipath < b->npath) {
					float *w = b->path[b->ipath]; float dx = w[0] - b->pl.pos.x, dy = w[1] - b->pl.pos.y, d = sqrtf(dx * dx + dy * dy);
					if (d < 70) { b->ipath++; b->prog_d = 1e9f; b->prog_t = 0; b->stuck_n = 0; }
					else {
						float want = atan2f(dy, dx), dyaw = wrap_pi(want - b->pl.yaw); b->pl.yaw += dyaw * fminf(1, 6 * dt);
						my = fabsf(dyaw) < 1.0f ? 1 : 0.25f; sprint = d > 600 && fabsf(dyaw) < 0.3f && (tdist > 1500 || b->tgt == -2);
						b->prog_t += dt; if (d < b->prog_d - 25) { b->prog_d = d; b->prog_t = 0; }
						if (b->prog_t > 0.5f && b->jump_cd <= 0 && b->pl.grounded) { jump = 1; b->jump_cd = 1.0f; }
						if (b->prog_t > 3.5f && ++b->stuck_n >= 3) { bot_stuck_respawns++; bot_spawn(b, T); continue; }   // hopelessly stuck: start over somewhere else
						if (b->prog_t > 3.5f) { b->ipath++; b->prog_d = 1e9f; b->prog_t = 0; if (b->ipath >= b->npath) b->repath_t = 0; }
					}
				}
			}
		}
		if (bot_sk_move && b->state != BS_DEAD) bot_sk_move(i, &mx, &my, &sprint, &jump);   // skills: rush, flee, hacked ...
		// ---- physics (the player's own movement code)
		int was_ground = b->pl.grounded;
		if (b->state == BS_DEAD || bot_freeze) { mx = my = 0; jump = sprint = 0; }
		player_update(&b->pl, mx, my, sprint, jump, dt); b->fmat = floor_mat;
		if (b->pl.pos.z < col_zmin - 500 && b->state != BS_DEAD) { b->state = BS_DEAD; b->dead_t = 4.9f; }
		b->air_t = b->pl.grounded ? 0 : b->air_t + dt;
		{ float L[3]; light_at(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 20, L); float k = fminf(1, 4 * dt); for (int c = 0; c < 3; c++) b->lit[c] += (L[c] - b->lit[c]) * k;
		  if (b->pl.grounded) b->floor_z = b->pl.pos.z; else if ((i + bot_frame) % 4 == 0) { float h = raycast(v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 40), v3(0, 0, -1), 600, NULL, NULL); b->floor_z = h > 0 ? b->pl.pos.z + 40 - h : b->pl.pos.z - 600; } }
		if (jump && was_ground && C->jump >= 0) anim_play(&b->an, C->jump, 0.08f, 0.2f, 0);
		// ---- animation choice from the velocity in the bot's own frame
		if (b->state != BS_DEAD) {
			float cy = cosf(b->pl.yaw), sy = sinf(b->pl.yaw);
			float vf = b->pl.vel.x * cy + b->pl.vel.y * sy, vr = b->pl.vel.x * sy - b->pl.vel.y * cy, hs = sqrtf(vf * vf + vr * vr);
			if (b->air_t > 0.35f && C->fall >= 0) { anim_set_base(&b->an, C->fall, 0.25f, 0); b->an.rate = 1; }
			else if (hs < 50) { anim_set_base(&b->an, C->idle, 0.25f, 0); b->an.rate = 1; }
			else {
				int dir = fabsf(vf) >= fabsf(vr) ? (vf >= 0 ? 0 : 1) : (vr >= 0 ? 3 : 2);
				int c; float ref;
				// reference speeds: how fast the feet move in each clip (walk ~190, run ~330, sprint ~500 cm/s)
				if (hs > 450 && dir == 0 && C->sprint >= 0) { c = C->sprint; ref = 500; }
				else if (hs > 300) { c = C->run[dir]; ref = dir == 2 ? 280 : dir == 3 ? 300 : 340; } else { c = C->walk[dir]; ref = dir == 3 ? 150 : 190; }
				anim_set_base(&b->an, c, 0.2f, 0); b->an.rate = fminf(1.8f, fmaxf(0.8f, hs / ref));
			}
			// footsteps
			if (b->pl.grounded && hs > 60) { b->step_d += hs * dt; if (b->step_d > (hs > 450 ? 135 : 112)) { b->step_d = 0; snd_foley(snd_chr_female(C->name) ? 2 : 1, hs > 450 ? "foot_fastrun" : "foot_run", b->fmat, &b->pl.pos.x); } }
		}
		anim_update(&b->an, dt);
		b->pose = anim_pose(&b->an, C->pelvis);
		if (b->state != BS_DEAD) pose_aim_pitch(&C->m, b->pose, C->spine, b->aim_pitch);
		// world points for hit tests and effects
		float hp[3] = { b->pose[C->head].p[0], b->pose[C->head].p[1], b->pose[C->head].p[2] + 8 };
		b->head = bot_world(b, hp); b->pelvis = bot_world(b, b->pose[C->pelvis].p); b->chest = bot_world(b, b->pose[C->spine2 >= 0 ? C->spine2 : C->pelvis].p);
		if (C->grip >= 0 && C->muzzle) { float m[3]; model_bone_xform(&b->pose[C->grip], C->muzzle, m); b->muzzle = bot_world(b, m); } else b->muzzle = b->chest;
	}
	// keep bots and the player apart (simple push)
	for (int i = 0; i < nbot; i++) for (int j = -1; j < nbot; j++) {
		if (j == i) continue; Bot *a = &bots[i]; if (a->state == BS_DEAD) continue;
		V3 o = j < 0 ? T->pos : bots[j].pl.pos; if (j >= 0 && bots[j].state == BS_DEAD) continue;
		float dx = a->pl.pos.x - o.x, dy = a->pl.pos.y - o.y, d2 = dx * dx + dy * dy;
		if (d2 < 65 * 65 && fabsf(a->pl.pos.z - o.z) < 150 && d2 > 1e-2f) { float d = sqrtf(d2), push = (65 - d) * (j < 0 ? 1.0f : 0.5f); a->pl.pos.x += dx / d * push; a->pl.pos.y += dy / d * push; }
	}
	floor_mat = save_floor;
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

// soft round shadows under the bots (drawn before them, depth-tested, no depth write)
static void bots_shadows(const float *VP) {
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (!b->pose) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 4000.f * 4000.f) continue;
		float hgt = b->pl.pos.z - b->floor_z; if (hgt > 250) continue; float a = 0.5f * (1 - hgt / 250);
		float lum = (b->lit[0] + b->lit[1] + b->lit[2]) / 3; a *= fminf(1, 0.35f + lum);   // fainter in the dark, where there is little light to block
		const float col[4] = { 1, 1, 1, a };
		if (b->state == BS_DEAD) sprite_ground(b->pelvis.x, b->pelvis.y, b->floor_z + 3, 95, 50, b->pl.yaw, col); else sprite_ground(b->pl.pos.x, b->pl.pos.y, b->floor_z + 3, 48, 42, b->pl.yaw, col); }
	sprite_flush(VP, sp_blob, 0);
}
// draw all bots. VP: column-major view-projection
// character shadows: near bots are drawn once more, flattened onto the floor under them along the light,
// in a flat dark colour; the stencil keeps each bot's shadow from darkening the same pixel twice
static void bots_planar_shadows(const float *VP, void (*mul)(float *, const float *, const float *)) {
	static const float L[3] = { 0.30f, 0.22f, 1.0f };   // towards the light (overcast sky: mostly from above)
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnable(GL_STENCIL_TEST); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); md_shadow = 1;
	int ref = 0;
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; if (!b->pose) continue; ChrType *C = &chrt[b->ct];
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 2200.f * 2200.f) continue; if (bot_sk_alpha && bot_sk_alpha(i) < 1) continue;
		float hgt = b->pl.pos.z - b->floor_z; if (hgt > 200) continue;
		float lum = (b->lit[0] + b->lit[1] + b->lit[2]) / 3, a = 0.42f * (1 - hgt / 200) * fminf(1, 0.35f + lum);
		float cx = b->pl.pos.x, cy_ = b->pl.pos.y, cz = b->floor_z, q[4];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * cx + VP[4 + r] * cy_ + VP[8 + r] * cz + VP[12 + r];
		if (q[3] < -150 || q[0] > q[3] + 300 || q[0] < -q[3] - 300 || q[1] > q[3] + 300 || q[1] < -q[3] - 300) continue;
		float h = b->floor_z + 1.5f, kx = L[0] / L[2], ky = L[1] / L[2];
		// world -> floor plane along L (column-major)
		float S[16] = { 1, 0, 0, 0, 0, 1, 0, 0, -kx, -ky, 0, 0, kx * h, ky * h, h, 1 };
		float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
		float Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z, 1 }, SM[16], MVP[16];
		mul(SM, S, Mw); mul(MVP, VP, SM);
		ref = (ref + 1) & 255; if (!ref) { glClear(GL_STENCIL_BUFFER_BIT); ref = 1; }
		glStencilFunc(GL_GREATER, ref, 0xFF);   // draw where this bot's number > stencil (not yet shadowed by this bot), then mark it
		const float tint[4] = { 0, 0, 0, a }, nofog[4] = { 0, 0, 0, 0 }, ld[4] = { 0, 0, 1, 0 };
		if (b->sc.ok && C->m.mesh[0].cpu) model_draw_cpu(&C->m, &b->sc, MVP, b->pose, ld, tint, nofog, 0); else model_draw(&C->m, MVP, b->pose, ld, tint, nofog);
	}
	md_shadow = 0; glDisable(GL_STENCIL_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE);
}
static void bots_draw(const float *VP, const float *fogc, void (*mul)(float *, const float *, const float *)) {
	bot_frame++;
	const float Lw[3] = { 0.33f, 0.42f, 0.84f };
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; if (!b->pose) continue; ChrType *C = &chrt[b->ct];
		V3 d = vsub(b->pl.pos, lis_pos); if (vdot(d, d) > 7000.f * 7000.f) continue;
		// rough frustum test on a sphere around the body
		float cx = b->pl.pos.x, cy_ = b->pl.pos.y, cz = b->pl.pos.z + 90, q[4];
		for (int r = 0; r < 4; r++) q[r] = VP[r] * cx + VP[4 + r] * cy_ + VP[8 + r] * cz + VP[12 + r];
		if (q[3] < -150 || q[0] > q[3] + 260 || q[0] < -q[3] - 260 || q[1] > q[3] + 400 || q[1] < -q[3] - 400) continue;
		float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
		float M[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z, 1 }, MVP[16];
		mul(MVP, VP, M);
		float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 };
		float tint[4] = { 1, 1, 1, 1 }; for (int c = 0; c < 3; c++) tint[c] = fminf(1.5f, fmaxf(0.18f, b->lit[c] * 1.15f)) * (b->state == BS_DEAD ? 0.8f : 1);   // lit like the floor under them
		// far bots are re-skinned every other frame (alternating), near ones every frame
		int reskin = vdot(d, d) < 2500.f * 2500.f || ((bot_frame + i) & 1);
		float ga_ = bot_sk_alpha ? bot_sk_alpha(i) : 1.f; if (ga_ <= 0) continue;   // cloaked: a faint additive ghost (skills.h)
		if (ga_ < 1) { tint[0] = 0.55f; tint[1] = 0.65f; tint[2] = 0.75f; tint[3] = ga_; glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		if (b->sc.ok && C->m.mesh[0].cpu) model_draw_cpu(&C->m, &b->sc, MVP, b->pose, ld, tint, fogc, reskin); else model_draw(&C->m, MVP, b->pose, ld, tint, fogc);
		if (ga_ < 1) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	}
}
