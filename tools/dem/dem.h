// Demolition (bomb mode) - reference implementation proposal for the engine (header-only, no engine dependencies).
// Data: build/data/<stem>.gdem (tools/dem/make_gdem.py), rules: build/data/dem_rules.txt (values below match it).
// The engine owns players, bots, rendering and audio; this module owns the round/bomb state and tells the engine what
// happened through event bits returned by dem_update() (play sounds / show banners / respawn players on them).
//
//   DemMap map; dem_load(&map, VDIR "downtown.gdem");      // sites (A,B) + target meshes + attack/defense spawns
//   DemMatch m; dem_start(&m, &map, 0);                    // team 0 attacks first
//   each frame:
//     dem_set_alive(&m, alive_team0, alive_team1);
//     for the local player / each bot that holds the use button:
//        float p = dem_use(&m, id, team, eye, dir, held, dt);  // plant / defuse progress 0..1 (<0: nothing to do here)
//     on a death: dem_player_died(&m, id);
//     unsigned ev = dem_update(&m, dt);                     // DEV_* bits, see below
//   queries: dem_attacking(&m, team), dem_spawn(&map, side, i), m.bomb_pos, m.bomb_site, m.fuse_left, m.round_left,
//            m.score[2], m.round, dem_site_ray(&map, eye, dir, maxd, &t)
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define DEM_ROUND_TIME   150.0f
#define DEM_TO_WIN       8
#define DEM_SWITCH_AFTER 5        // sides switch after this round (footage: "ROUND 6 SWITCHING SIDES")
#define DEM_PLANT_TIME   5.0f
#define DEM_DEFUSE_TIME  7.0f
#define DEM_FUSE         45.0f
#define DEM_FREEZE       6.8f     // spawn -> START
#define DEM_BANNER_AT    2.9f     // round banner, seconds into the freeze
#define DEM_POST_ROUND   5.0f     // result band -> reset
#define DEM_HALFTIME     7.2f     // extra: switching sides band
#define DEM_MATCH_END    3.4f
#define DEM_REACH_PLANT  200.0f   // cm from the eye to the target mesh along the view ray (zioplant A lies under a floor grate: ~170 cm)
#define DEM_REACH_DEFUSE 200.0f   // cm from the eye to the planted bomb
#define DEM_BEEP1_UNTIL  15.0f    // ui_bomb_count_01 each second until 15 s after the plant
#define DEM_BEEP3_LEFT   22.34f   // ui_bomb_count_03 (22.34 s) starts so that it ends on the detonation

typedef struct {
	char name[16], letter; int add_col;
	float origin[3], yaw, bmin[3], bmax[3], marker[3], stand[3], aim[3];
	int nv, nt; float *v; unsigned short *t;
} DemSite;
typedef struct { float x, y, z, yaw; } DemSpawn;
typedef struct { int nsite, nsp[2]; DemSite site[2]; DemSpawn *sp[2]; int ok; } DemMap;   // sp[0] attack, sp[1] defense

static int dem_load(DemMap *M, const char *path) {
	memset(M, 0, sizeof(*M));
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned h[5];
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GDEM", 4) || fread(h, 4, 5, f) != 5 || h[0] != 1 || h[1] > 2) { fclose(f); return -2; }
	M->nsite = h[1]; M->nsp[0] = h[2]; M->nsp[1] = h[3];
	for (int i = 0; i < M->nsite; i++) {
		DemSite *s = &M->site[i]; unsigned char b[4]; unsigned n[2];
		fread(s->name, 16, 1, f); fread(b, 4, 1, f); s->letter = (char)b[0]; s->add_col = b[1];
		fread(s->origin, 4, 3, f); fread(&s->yaw, 4, 1, f); fread(s->bmin, 4, 3, f); fread(s->bmax, 4, 3, f);
		fread(s->marker, 4, 3, f); fread(s->stand, 4, 3, f); fread(s->aim, 4, 3, f); fread(n, 4, 2, f); s->nv = n[0]; s->nt = n[1];
		s->v = malloc(s->nv * 12); s->t = malloc(s->nt * 6 + 2);
		fread(s->v, 12, s->nv, f); fread(s->t, 6, s->nt, f); if ((s->nt * 3) & 1) { unsigned short pad; fread(&pad, 2, 1, f); }
	}
	for (int k = 0; k < 2; k++) { M->sp[k] = malloc(sizeof(DemSpawn) * (M->nsp[k] ? M->nsp[k] : 1)); fread(M->sp[k], sizeof(DemSpawn), M->nsp[k], f); }
	int ok = !ferror(f) && !feof(f); fclose(f); M->ok = ok; return ok ? 0 : -3;
}
static void dem_free(DemMap *M) { for (int i = 0; i < M->nsite; i++) { free(M->site[i].v); free(M->site[i].t); } free(M->sp[0]); free(M->sp[1]); memset(M, 0, sizeof(*M)); }

// ray (eye, unit dir) vs a site's target mesh: nearest hit distance or -1 (Moller-Trumbore, both faces)
static float dem_ray_site(const DemSite *s, const float *o, const float *d, float maxd) {
	float best = -1;
	for (int i = 0; i < s->nt; i++) {
		const float *a = s->v + 3 * s->t[3 * i], *b = s->v + 3 * s->t[3 * i + 1], *c = s->v + 3 * s->t[3 * i + 2];
		float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
		float p[3] = { d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0] };
		float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2]; if (fabsf(det) < 1e-6f) continue;
		float inv = 1 / det, tv[3] = { o[0] - a[0], o[1] - a[1], o[2] - a[2] };
		float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv; if (u < 0 || u > 1) continue;
		float q[3] = { tv[1] * e1[2] - tv[2] * e1[1], tv[2] * e1[0] - tv[0] * e1[2], tv[0] * e1[1] - tv[1] * e1[0] };
		float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv; if (v < 0 || u + v > 1) continue;
		float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
		if (t > 0 && t <= maxd && (best < 0 || t < best)) best = t;
	}
	return best;
}
// which site the view ray hits within maxd (-1 none); *t = distance
static int dem_site_ray(const DemMap *M, const float *o, const float *d, float maxd, float *t) {
	int hit = -1; float bt = maxd;
	for (int i = 0; i < M->nsite; i++) {
		const DemSite *s = &M->site[i];   // cheap reject: eye farther than maxd from the box
		float dx = fmaxf(fmaxf(s->bmin[0] - o[0], o[0] - s->bmax[0]), 0), dy = fmaxf(fmaxf(s->bmin[1] - o[1], o[1] - s->bmax[1]), 0), dz = fmaxf(fmaxf(s->bmin[2] - o[2], o[2] - s->bmax[2]), 0);
		if (dx * dx + dy * dy + dz * dz > maxd * maxd) continue;
		float r = dem_ray_site(s, o, d, bt); if (r >= 0 && r <= bt) { bt = r; hit = i; }
	}
	if (t) *t = bt;
	return hit;
}
static const DemSpawn *dem_spawn(const DemMap *M, int side, int i) { return M->nsp[side] ? &M->sp[side][i % M->nsp[side]] : NULL; }

enum { DS_FREEZE, DS_LIVE, DS_PLANTED, DS_ROUND_END, DS_SWITCH, DS_MATCH_END, DS_DONE };
enum { DR_NONE, DR_ELIM_ATT, DR_ELIM_DEF, DR_DETONATION, DR_DEFUSED, DR_TIME };   // round end reasons
// events (bits) returned by dem_update / raised by dem_use
#define DEV_RESET        (1u << 0)   // place everyone at spawns (dem_attacking() tells which list), revive, freeze
#define DEV_BANNER       (1u << 1)   // "ROUND n" + objective banner; round 1 / after switch: Aramaki start line
#define DEV_START        (1u << 2)   // "START": unfreeze, round timer runs
#define DEV_PLANT_BEGIN  (1u << 3)   // someone started planting: ui_plant_bomb (+ plant_bomb_start)
#define DEV_PLANTED      (1u << 4)   // bomb armed: plant_bomb_done, d_aramaki_dm_plantbomb_team|enemy, bgm_bomb_count, spawn bomb model
#define DEV_DEFUSE_BEGIN (1u << 5)   // ui_deactivate_bomb (+ deactivate_bomb_start)
#define DEV_DEFUSED      (1u << 6)   // deactivate_bomb_done
#define DEV_EXPLODE      (1u << 7)   // bomb_exp_01, DM_Explode fx, cam_shake_explode_bomb, damage around bomb_pos
#define DEV_BEEP1        (1u << 8)   // ui_bomb_count_01
#define DEV_BEEP2        (1u << 9)   // ui_bomb_count_02
#define DEV_BEEP3        (1u << 10)  // ui_bomb_count_03 (once, 22.34 s)
#define DEV_ROUND_END    (1u << 11)  // result band (winner, reason in m->last_reason); bgm_win/lose
#define DEV_SWITCH       (1u << 12)  // switching sides band; d_aramaki_dm_attack_todefense / _defense_toattack at the next banner
#define DEV_MATCH_END    (1u << 13)  // d_aramaki_dm_common_win|lose|draw, end screen
#define DEV_ONE_LEFT_0   (1u << 14)  // team 0 down to its last player (d_aramaki_dm_oneleft_self for team 0's players)
#define DEV_ONE_LEFT_1   (1u << 15)
#define DEV_INTRO_MUSIC  (1u << 16)  // bgm_in_game_dm_round (4 s after a round ends, 14.7 s long)
#define DEV_DEFUSE_STOP  (1u << 17)  // defuse interrupted
#define DEV_PLANT_STOP   (1u << 18)

typedef struct {
	const DemMap *map;
	int state, round, score[2], first_attack, switched, last_winner, last_reason;
	float t, round_left, fuse_left, beep_t;
	int alive[2], one_left_said[2], beep3_done, intro_music_done;
	int bomb_site; float bomb_pos[3];               // planted bomb (site index or -1)
	int planter, plant_site; float plant_prog;       // current planter (player id, -1 none) and progress 0..1
	int defuser; float defuse_prog;
	unsigned pend;                                  // events raised by dem_use, returned by the next dem_update
} DemMatch;

static int dem_attacking(const DemMatch *m, int team) { return (team == m->first_attack) != (m->switched != 0); }
static void dem_reset_round(DemMatch *m) {
	m->state = DS_FREEZE; m->t = 0; m->round++; m->round_left = DEM_ROUND_TIME; m->fuse_left = 0; m->bomb_site = -1;
	m->planter = m->defuser = -1; m->plant_prog = m->defuse_prog = 0; m->one_left_said[0] = m->one_left_said[1] = 0; m->beep3_done = 0;
	m->intro_music_done = 1; m->alive[0] = m->alive[1] = 99;
}
static void dem_start(DemMatch *m, const DemMap *map, int first_attack) {
	memset(m, 0, sizeof(*m)); m->map = map; m->first_attack = first_attack; m->last_winner = -1;
	dem_reset_round(m); m->pend = DEV_RESET;
}
static void dem_set_alive(DemMatch *m, int a0, int a1) { m->alive[0] = a0; m->alive[1] = a1; }
static void dem_end_round(DemMatch *m, int winner, int reason) {
	m->state = DS_ROUND_END; m->t = 0; m->last_winner = winner; m->last_reason = reason; m->score[winner]++;
	m->pend |= DEV_ROUND_END; m->intro_music_done = 0; m->planter = m->defuser = -1;
}
// one player's use button (player: any id >= 0, team 0/1, eye position, unit view direction, held, dt)
// -> this player's plant/defuse progress 0..1, 0 = a target is in reach (show "Plant Bomb"/"Defuse Bomb" + HOLD), -1 = nothing here.
// The engine should stop the player's movement while the progress is > 0 (the game shows the 1P device during it).
static float dem_use(DemMatch *m, int player, int team, const float *eye, const float *dir, int held, float dt) {
	if (m->state == DS_LIVE && dem_attacking(m, team)) {
		float t; int s = dem_site_ray(m->map, eye, dir, DEM_REACH_PLANT, &t);
		if (s < 0 || !held) {                                   // released or looked away: progress is lost
			if (m->planter == player) { m->planter = -1; m->plant_prog = 0; m->pend |= DEV_PLANT_STOP; }
			return s < 0 ? -1 : 0;
		}
		if (m->planter >= 0 && m->planter != player) return 0;   // someone else of the team is planting
		if (m->planter != player || m->plant_site != s) { m->planter = player; m->plant_site = s; m->plant_prog = 0; m->pend |= DEV_PLANT_BEGIN; }
		m->plant_prog += dt / DEM_PLANT_TIME;
		if (m->plant_prog >= 1) {
			m->state = DS_PLANTED; m->fuse_left = DEM_FUSE; m->bomb_site = s; m->planter = -1; m->pend |= DEV_PLANTED | DEV_BEEP1;
			for (int k = 0; k < 3; k++) m->bomb_pos[k] = eye[k] + dir[k] * (t - 2);   // on the target surface, 2 cm in front
			return 1;
		}
		return m->plant_prog;
	}
	if (m->state == DS_PLANTED && !dem_attacking(m, team)) {
		float dx = m->bomb_pos[0] - eye[0], dy = m->bomb_pos[1] - eye[1], dz = m->bomb_pos[2] - eye[2], d = sqrtf(dx * dx + dy * dy + dz * dz);
		int ok = d <= DEM_REACH_DEFUSE && (d < 1 || (dx * dir[0] + dy * dir[1] + dz * dir[2]) / d > 0.6f);
		if (!ok || !held) {
			if (m->defuser == player) { m->defuser = -1; m->defuse_prog = 0; m->pend |= DEV_DEFUSE_STOP; }
			return ok ? 0 : -1;
		}
		if (m->defuser >= 0 && m->defuser != player) return 0;
		if (m->defuser != player) { m->defuser = player; m->defuse_prog = 0; m->pend |= DEV_DEFUSE_BEGIN; }
		m->defuse_prog += dt / DEM_DEFUSE_TIME;
		if (m->defuse_prog >= 1) { m->pend |= DEV_DEFUSED; dem_end_round(m, team, DR_DEFUSED); return 1; }
		return m->defuse_prog;
	}
	return -1;
}
// a planter / defuser died: drop their progress
static void dem_player_died(DemMatch *m, int player) {
	if (m->planter == player) { m->planter = -1; m->plant_prog = 0; m->pend |= DEV_PLANT_STOP; }
	if (m->defuser == player) { m->defuser = -1; m->defuse_prog = 0; m->pend |= DEV_DEFUSE_STOP; }
}
static unsigned dem_update(DemMatch *m, float dt) {
	unsigned ev = m->pend; m->pend = 0; float t0 = m->t; m->t += dt;
	int att = dem_attacking(m, 0) ? 0 : 1, def = 1 - att;
	switch (m->state) {
	case DS_FREEZE:
		if (t0 < DEM_BANNER_AT && m->t >= DEM_BANNER_AT) ev |= DEV_BANNER;
		if (m->t >= DEM_FREEZE) { m->state = DS_LIVE; m->t = 0; ev |= DEV_START; }
		break;
	case DS_LIVE:
		m->round_left -= dt;
		for (int k = 0; k < 2; k++) if (m->alive[k] == 1 && !m->one_left_said[k]) { m->one_left_said[k] = 1; ev |= k ? DEV_ONE_LEFT_1 : DEV_ONE_LEFT_0; }
		if (m->alive[def] <= 0) dem_end_round(m, att, DR_ELIM_DEF);
		else if (m->alive[att] <= 0) dem_end_round(m, def, DR_ELIM_ATT);
		else if (m->round_left <= 0) { m->round_left = 0; dem_end_round(m, def, DR_TIME); }
		break;
	case DS_PLANTED: {                                       // round timer stops; attackers dying no longer ends the round
		float before = DEM_FUSE - m->fuse_left; m->fuse_left -= dt; float after = DEM_FUSE - m->fuse_left;
		for (int k = 0; k < 2; k++) if (m->alive[k] == 1 && !m->one_left_said[k]) { m->one_left_said[k] = 1; ev |= k ? DEV_ONE_LEFT_1 : DEV_ONE_LEFT_0; }
		if (m->fuse_left > DEM_BEEP3_LEFT && floorf(after) > floorf(before)) ev |= after < DEM_BEEP1_UNTIL ? DEV_BEEP1 : DEV_BEEP2;
		if (!m->beep3_done && m->fuse_left <= DEM_BEEP3_LEFT) { m->beep3_done = 1; ev |= DEV_BEEP3; }
		if (m->fuse_left <= 0) { m->fuse_left = 0; ev |= DEV_EXPLODE; dem_end_round(m, att, DR_DETONATION); }
		else if (m->alive[def] <= 0) dem_end_round(m, att, DR_ELIM_DEF);
		break; }
	case DS_ROUND_END:
		if (!m->intro_music_done && m->t >= 4.0f && m->score[0] < DEM_TO_WIN && m->score[1] < DEM_TO_WIN) { m->intro_music_done = 1; ev |= DEV_INTRO_MUSIC; }
		if (m->score[0] >= DEM_TO_WIN || m->score[1] >= DEM_TO_WIN) { if (m->t >= DEM_MATCH_END) { m->state = DS_MATCH_END; m->t = 0; ev |= DEV_MATCH_END; } }
		else if (m->t >= DEM_POST_ROUND) {
			if (m->round == DEM_SWITCH_AFTER && !m->switched) { m->state = DS_SWITCH; m->t = 0; m->switched = 1; ev |= DEV_SWITCH; }
			else { dem_reset_round(m); ev |= DEV_RESET; }
		}
		break;
	case DS_SWITCH:
		if (m->t >= DEM_HALFTIME) { dem_reset_round(m); ev |= DEV_RESET; }
		break;
	}
	return ev;
}
