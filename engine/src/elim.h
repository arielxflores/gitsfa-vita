// Elimination (mode EM): rounds without respawns; a team wins a round by wiping out the other, time-out is a draw
// (both teams score). Late in each round the outer zones of the map lock: anyone still inside takes damage.
// Header-only like dem.h: owns the round state and the map's zone data, tells the engine what happened through event
// bits returned by elim_update() (sounds, banners, resets). The engine owns players, bots, rendering and audio.
// Data: build/data/<stem>.gelim (tools/elim/make_gelim.py: spawns + SafeZone_out_Area / SafeZone_out meshes of the
// map) and <stem>_em.gmm (the mode's radar image with the lockable zones hatched).
// Rules and timings measured from "Elimination Urban City - Motoko gameplay.mp4" (per-second frames + the soundtrack
// matched against the game's own sounds):
//   ROUND n / "Eliminate all enemy reinforcements" banner at the reset (ui_game_start_text), START 5.9 s later
//   (ui_game_start_start); round timer 01:30 (shown during the freeze, 00:00 before round 1)
//   01:00 "30 SECONDS TO ZONE LOCK" (ui_elim_zone_start), 00:40 "MOVE TO A SAFE ZONE!" + 10..1 countdown for players in
//   a lockable zone (ui_elim_zone_start), 00:30 "ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED." (ui_elim_zone_start) and a
//   red "ZONE LOCKED" tag under the timer, 00:20 "Timer Warning / Time is running out!" (d_aramaki_tdm_20sec)
//   round end: "ROUND n / VICTORY|DEFEAT / Mission Successful|Failed: Team Eliminated" or "DRAW / Draw: Time Up" band
//   (ui_result) 4.1 s, then the next reset; after round 5 (first to 6) an extra 8.1 s "ROUND 6 / SWITCHING SIDES" band;
//   one player left: d_aramaki_dm_oneleft_self / _enemy; match won: d_aramaki_dm_common_win (6 - 5 in the footage)
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define EM_ROUND_TIME   90.0f
#define EM_TO_WIN       6
#define EM_SWITCH_AFTER 5       // "ROUND 6 / SWITCHING SIDES" after round 5
#define EM_FREEZE       5.9f    // reset (banner) -> START
#define EM_POST_ROUND   4.1f    // result band -> next reset
#define EM_SWITCH_BAND  8.1f    // switching sides band -> reset
#define EM_MATCH_END    3.4f    // last round result -> end of match screen
#define EM_WARN_AT      60.0f   // round time left: "30 SECONDS TO ZONE LOCK"
#define EM_EVAC_AT      40.0f   // "MOVE TO A SAFE ZONE!" + countdown (players inside a lockable zone)
#define EM_LOCK_AT      30.0f   // the zones lock
#define EM_TIMEWARN_AT  20.0f   // "Timer Warning"
#define EM_ZONE_DPS     100.0f  // damage per second inside a locked zone (b83: "lethal damage" - the game's rules; was a 25 proposal)

typedef struct { float bmin[3], bmax[3]; int nv, nt; float *v; unsigned short *t; float (*tb)[4]; } ElimZone;   // tb: each triangle's x/y box (perf)
typedef struct { float x, y, z, yaw; } ElimSpawn;
typedef struct { int nsp[2], nzone, nbar, nsafe; ElimSpawn *sp[2]; ElimZone *zone; float (*bar)[4][3]; float (*safe)[3]; int ok; } ElimMap;

static void elim_free(ElimMap *M) {
	for (int i = 0; i < M->nzone; i++) { free(M->zone[i].v); free(M->zone[i].t); free(M->zone[i].tb); }
	free(M->zone); free(M->sp[0]); free(M->sp[1]); free(M->bar); free(M->safe); memset(M, 0, sizeof(*M));
}
static int elim_load(ElimMap *M, const char *path) {
	memset(M, 0, sizeof(*M));
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned ver, h[4];
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GELM", 4) || fread(&ver, 4, 1, f) != 1 || ver < 1 || ver > 2 || fread(h, 4, 4, f) != 4 || h[0] > 64 || h[1] > 64 || h[2] > 32 || h[3] > 64) { fclose(f); return -2; }
	M->nsp[0] = h[0]; M->nsp[1] = h[1]; M->nzone = h[2]; M->nbar = h[3];
	for (int k = 0; k < 2; k++) { M->sp[k] = malloc(sizeof(ElimSpawn) * (M->nsp[k] + 1)); if (fread(M->sp[k], sizeof(ElimSpawn), M->nsp[k], f) != (size_t)M->nsp[k]) { fclose(f); elim_free(M); return -3; } }
	M->zone = calloc(M->nzone + 1, sizeof(ElimZone));
	for (int i = 0; i < M->nzone; i++) {
		ElimZone *z = &M->zone[i]; unsigned n[2];
		if (fread(z->bmin, 4, 3, f) != 3 || fread(z->bmax, 4, 3, f) != 3 || fread(n, 4, 2, f) != 2 || n[0] > 65535 || n[1] > 65535) { fclose(f); elim_free(M); return -4; }
		z->nv = n[0]; z->nt = n[1]; z->v = malloc(z->nv * 12 + 12); z->t = malloc(z->nt * 6 + 2);
		if (fread(z->v, 12, z->nv, f) != (size_t)z->nv || fread(z->t, 6, z->nt, f) != (size_t)z->nt) { fclose(f); elim_free(M); return -5; }
		if ((z->nt * 3) & 1) { unsigned short pad; if (fread(&pad, 2, 1, f) != 1) { fclose(f); elim_free(M); return -5; } }
		for (int k = 0; k < 3 * z->nt; k++) if (z->t[k] >= z->nv) { fclose(f); elim_free(M); return -5; }
		z->tb = malloc(sizeof(float) * 4 * (z->nt + 1));   // perf (b35): elim_zone_at skips the triangles whose box (+ 2 cm) misses the point
		if (z->tb) for (int k = 0; k < z->nt; k++) { float *r = z->tb[k]; r[0] = r[1] = 1e30f; r[2] = r[3] = -1e30f;
			for (int j = 0; j < 3; j++) { const float *a = z->v + 3 * z->t[3 * k + j]; r[0] = fminf(r[0], a[0]); r[1] = fminf(r[1], a[1]); r[2] = fmaxf(r[2], a[0]); r[3] = fmaxf(r[3], a[1]); }
			r[0] -= 2; r[1] -= 2; r[2] += 2; r[3] += 2; }
	}
	M->bar = malloc(sizeof(float) * 12 * (M->nbar + 1)); if (fread(M->bar, 48, M->nbar, f) != (size_t)M->nbar) { fclose(f); elim_free(M); return -6; }
	if (ver >= 2) { unsigned ns = 0; if (fread(&ns, 4, 1, f) == 1 && ns <= 256) { M->nsafe = ns; M->safe = malloc(12 * (ns + 1)); if (fread(M->safe, 12, ns, f) != ns) M->nsafe = 0; } }
	fclose(f); M->ok = 1; return 0;
}
// the lockable zone under a point (feet), or -1: the zone floor triangle under (x, y) within 150 cm below / 250 cm above
static int elim_zone_at(const ElimMap *M, float x, float y, float z) {
	for (int i = 0; i < M->nzone; i++) {
		const ElimZone *Z = &M->zone[i];
		if (x < Z->bmin[0] || x > Z->bmax[0] || y < Z->bmin[1] || y > Z->bmax[1] || z < Z->bmin[2] - 250 || z > Z->bmax[2] + 150) continue;
		for (int k = 0; k < Z->nt; k++) {
			if (Z->tb) { const float *r = Z->tb[k]; if (x < r[0] || x > r[2] || y < r[1] || y > r[3]) continue; }   // (a point 2 cm outside a triangle's box fails the test below too)
			const float *a = Z->v + 3 * Z->t[3 * k], *b = Z->v + 3 * Z->t[3 * k + 1], *c = Z->v + 3 * Z->t[3 * k + 2];
			float d = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]); if (fabsf(d) < 1e-3f) continue;
			float u = ((x - a[0]) * (c[1] - a[1]) - (y - a[1]) * (c[0] - a[0])) / d; if (u < 0 || u > 1) continue;
			float w = ((b[0] - a[0]) * (y - a[1]) - (b[1] - a[1]) * (x - a[0])) / d; if (w < 0 || u + w > 1) continue;
			float zz = a[2] + u * (b[2] - a[2]) + w * (c[2] - a[2]);
			if (z > zz - 150 && z < zz + 250) return i;
		}
	}
	return -1;
}
// the nearest safe point (a walkable spot outside every lockable zone) to (x, y, z); NULL when the map has none
static const float *elim_safe_near(const ElimMap *M, float x, float y, float z, int pick_random) {
	if (!M->nsafe) return NULL;
	int best = 0; float bd = 1e30f;
	for (int i = 0; i < M->nsafe; i++) { float dx = M->safe[i][0] - x, dy = M->safe[i][1] - y, dz = (M->safe[i][2] - z) * 2; float d = dx * dx + dy * dy + dz * dz; if (pick_random) d *= 0.6f + (rand() % 1000) / 1000.f; if (d < bd) { bd = d; best = i; } }
	return M->safe[best];
}

enum { ES_FREEZE, ES_LIVE, ES_ROUND_END, ES_SWITCH, ES_MATCH_END, ES_DONE };
enum { ER_NONE, ER_ELIM, ER_TIME };   // round end reasons: a team eliminated (winner in last_winner), time up (draw, both score)
// event bits returned by elim_update
#define EEV_RESET      (1u << 0)    // place everyone at their side's spawns, revive, freeze; ROUND n banner (ui_game_start_text)
#define EEV_START      (1u << 1)    // "START": unfreeze, the round timer runs (ui_game_start_start)
#define EEV_ZONE_WARN  (1u << 2)    // "30 SECONDS TO ZONE LOCK" (ui_elim_zone_start)
#define EEV_EVAC       (1u << 3)    // "MOVE TO A SAFE ZONE!" countdown starts (ui_elim_zone_start for players in a zone)
#define EEV_COUNT      (1u << 4)    // a countdown second (10..1) passed (ui_elim_countdown for players in a zone)
#define EEV_LOCK       (1u << 5)    // zones locked: "ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED." (ui_elim_zone_start)
#define EEV_TIMEWARN   (1u << 6)    // "Timer Warning / Time is running out!" (d_aramaki_tdm_20sec)
#define EEV_ROUND_END  (1u << 7)    // result band (last_winner: 0/1, -1 draw; last_reason) (ui_result, bgm win/lose/even)
#define EEV_SWITCH     (1u << 8)    // switching sides band
#define EEV_MATCH_END  (1u << 9)    // d_aramaki_dm_common_win|lose|draw, end screen
#define EEV_ONE_LEFT_0 (1u << 10)   // team 0 down to its last player (d_aramaki_dm_oneleft_self for team 0)
#define EEV_ONE_LEFT_1 (1u << 11)
#define EEV_RESPAWN_SND (1u << 12)  // ui_respawn (1.4 s after the reset in the footage)

typedef struct {
	const ElimMap *map;
	int state, round, score[2], first_side, switched, last_winner, last_reason;
	float t, round_left;
	int alive[2], one_left_said[2], warned, evac, locked, timewarn, count_left, respawn_snd;
	int to_win, switch_after; float round_time;
	float warn_at, evac_at, lock_at, timewarn_at;   // zone / timer timeline (round time left); EM_* by default
	int time_win_side, no_zones;   // b63 (Ghost Assault, ga.h): the side that wins a round on time (-1: a draw, Elimination); no zone timeline
} ElimMatch;

// the spawn list team plays from this round (0/1): sides swap after switch_after rounds
static int elim_side(const ElimMatch *m, int team) { return (team ^ m->first_side ^ (m->switched != 0)) & 1; }
static const ElimSpawn *elim_spawn(const ElimMap *M, int side, int i) { return M->nsp[side] ? &M->sp[side][i % M->nsp[side]] : NULL; }
static void elim_reset_round(ElimMatch *m) {
	m->state = ES_FREEZE; m->t = 0; m->round++; m->round_left = m->round_time;
	m->one_left_said[0] = m->one_left_said[1] = 0; m->warned = m->evac = m->locked = m->timewarn = 0; m->count_left = 0; m->respawn_snd = 0;
	m->alive[0] = m->alive[1] = 99;
}
static void elim_start(ElimMatch *m, const ElimMap *map, int first_side) {
	memset(m, 0, sizeof(*m)); m->map = map; m->first_side = first_side; m->last_winner = -1;
	m->to_win = EM_TO_WIN; m->switch_after = EM_SWITCH_AFTER; m->round_time = EM_ROUND_TIME; m->time_win_side = -1;
	m->warn_at = EM_WARN_AT; m->evac_at = EM_EVAC_AT; m->lock_at = EM_LOCK_AT; m->timewarn_at = EM_TIMEWARN_AT;
	elim_reset_round(m);
}
// a shorter round (test runs): the zone timeline keeps its proportions
static void elim_set_round_time(ElimMatch *m, float rt) {
	float k = rt / EM_ROUND_TIME; m->round_time = rt; m->round_left = rt;
	m->warn_at = EM_WARN_AT * k; m->evac_at = EM_EVAC_AT * k; m->lock_at = EM_LOCK_AT * k; m->timewarn_at = EM_TIMEWARN_AT * k;
}
static void elim_set_alive(ElimMatch *m, int a0, int a1) { m->alive[0] = a0; m->alive[1] = a1; }
static void elim_end_round(ElimMatch *m, int winner, int reason) {
	m->state = ES_ROUND_END; m->t = 0; m->last_winner = winner; m->last_reason = reason;
	if (winner >= 0) m->score[winner]++; else { m->score[0]++; m->score[1]++; }   // footage: "ROUND 6 DRAW / Draw: Time Up" took 3-2 to 4-3
}
// zones are locked (damage inside) / about to lock (the evacuation countdown runs)
static int elim_locked(const ElimMatch *m) { return m->state == ES_LIVE && m->locked; }
static int elim_evacuating(const ElimMatch *m) { return m->state == ES_LIVE && m->evac && !m->locked; }
// the round timer as shown: 01:30 while frozen (00:00 before the first round), counting during the round
static float elim_timer(const ElimMatch *m) { if (m->state == ES_FREEZE) return m->round == 1 ? 0 : m->round_time; return m->round_left; }
static unsigned elim_update(ElimMatch *m, float dt) {
	unsigned ev = 0; float t0 = m->t; m->t += dt;
	switch (m->state) {
	case ES_FREEZE:
		if (m->round == 1 && t0 == 0) ev |= EEV_RESET;
		if (!m->respawn_snd && m->t >= 1.4f) { m->respawn_snd = 1; ev |= EEV_RESPAWN_SND; }
		if (m->t >= EM_FREEZE) { m->state = ES_LIVE; m->t = 0; ev |= EEV_START; }
		break;
	case ES_LIVE: {
		m->round_left -= dt; if (m->round_left < 0) m->round_left = 0;
		float now = m->round_left;
		for (int k = 0; k < 2; k++) if (m->alive[k] == 1 && !m->one_left_said[k]) { m->one_left_said[k] = 1; ev |= k ? EEV_ONE_LEFT_1 : EEV_ONE_LEFT_0; }
		if (m->map && m->map->nzone && !m->no_zones) {
			if (!m->warned && now <= m->warn_at) { m->warned = 1; ev |= EEV_ZONE_WARN; }
			if (!m->evac && now <= m->evac_at) { m->evac = 1; m->count_left = (int)ceilf(now - m->lock_at); ev |= EEV_EVAC; }
			else if (m->evac && !m->locked && now > m->lock_at && (int)ceilf(now - m->lock_at) != m->count_left) { m->count_left = (int)ceilf(now - m->lock_at); ev |= EEV_COUNT; }   // 10 .. 1 (seconds to the lock)
			if (!m->locked && now <= m->lock_at) { m->locked = 1; m->count_left = 0; ev |= EEV_LOCK; }
		}
		if (!m->timewarn && now <= m->timewarn_at) { m->timewarn = 1; ev |= EEV_TIMEWARN; }
		if (m->alive[0] <= 0 && m->alive[1] <= 0) { elim_end_round(m, -1, ER_ELIM); ev |= EEV_ROUND_END; }
		else if (m->alive[1] <= 0) { elim_end_round(m, 0, ER_ELIM); ev |= EEV_ROUND_END; }
		else if (m->alive[0] <= 0) { elim_end_round(m, 1, ER_ELIM); ev |= EEV_ROUND_END; }
		else if (now <= 0) { elim_end_round(m, m->time_win_side >= 0 ? (m->time_win_side ^ m->first_side ^ (m->switched != 0)) & 1 : -1, ER_TIME); ev |= EEV_ROUND_END; }   // (the team on that side)
		break; }
	case ES_ROUND_END:
		if (m->score[0] >= m->to_win || m->score[1] >= m->to_win) { if (m->t >= EM_MATCH_END) { m->state = ES_MATCH_END; m->t = 0; ev |= EEV_MATCH_END; } }
		else if (m->t >= EM_POST_ROUND) {
			if (m->round == m->switch_after && !m->switched) { m->state = ES_SWITCH; m->t = 0; m->switched = 1; ev |= EEV_SWITCH; }
			else { elim_reset_round(m); ev |= EEV_RESET; }
		}
		break;
	case ES_SWITCH:
		if (m->t >= EM_SWITCH_BAND) { elim_reset_round(m); ev |= EEV_RESET; }
		break;
	}
	return ev;
}
