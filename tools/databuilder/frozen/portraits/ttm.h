// Terminal Conquest (MODE_TTM): a chain of terminals (A - B - C on Underground, A..E on Berutalube / Ruined Tokyo); one is
// active at a time; capturing it scores a point and moves the fight one terminal towards the losers' end; capturing the
// other team's last terminal wins outright. Think Tanks (thinktank.h) are the capture reward.
// Rules measured in the footage (Terminal Conquest Underground Base - Sitara gameplay, 969 s; notes: tools/tank/TTM_NOTES.md):
//   - 15:00 match, first to 4 captures (the "4" between the scores), or the enemy's final terminal ("FINAL TERMINAL!").
//   - "Capture system rebooting..." 35 s before a terminal can be captured (match start and after every capture; the grey
//     bar shrinks). Then a tug-of-war bar from 50 %: only operators of ONE team inside the capture area move it towards
//     that team (any number of them), at 50 % per ~37 s; contested or empty: it stays. "<name> capturing" = the first of
//     the moving team in the area. The speed multiplier steps x1 -> x2 -> x3 every 60 s the terminal is online (shown
//     in the bar). 100 % (or 0 %): +1 for that team, "Terminal secured! Move on to the next terminal." /
//     "Terminal takeover failed! Regroup and secure the next terminal!", the next terminal reboots.
//   - The capturer gets "TACHIKOMA READY" (in-game tip: "TACHIKOMA will spawn when you capture the terminal."); so does
//     whoever destroys an enemy Think Tank (footage: both times a new tank appeared seconds after that). One per team.
//   - Time up with equal scores: OVERTIME (3:00, the HUD shows both sides' %), the next capture decides; when overtime
//     runs out the side ahead on the bar wins.
// Data: <stem>.gttm (tools/tank/make_ttm.py from SpawnSystem.ssf + area_check.nif), tk_term.gvm (the terminal).
// Spawns: each terminal carries 21 spawns per team used while it is active (the SSF lists).
//
// API: ttm_load(&map, path); ttm_start(&m, &map, to_win, match_s); per frame ttm_update(&m, dt) -> TEV_* bits;
//      ttm_frame(dt, eye...) drives bots / rewards / tank call-ins (see the game hooks in main.c "TTM glue").
#pragma once
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef TTM_RULES_ONLY
#include "thinktank.h"
#else              // host test of the rules (tools/tank/host/ttm_sim.c)
typedef struct { float x, y, z; } V3;
#define NBOT_MAX 8
#endif

#define TTM_REBOOT 35.0f
#define TTM_RATE (1.0f / 74.0f)   // bar per second at x1 (footage: 50 % -> 100 % in ~37 s)
#define TTM_MULT_T 60.0f
#define TTM_MATCH_T 900.0f
#define TTM_OVERTIME 180.0f
#define TTM_TO_WIN 4
#define TTM_MAXT 8
#define TTM_NACT (1 + NBOT_MAX)

typedef struct { char name[16], letter; int next[2]; float dev[4], side[2][4]; int nsp[2]; float sp[2][24][4]; int ntri; float *tri, *tz; float lo[3], hi[3]; } TtmTerm;
typedef struct { int ok, n, start; TtmTerm t[TTM_MAXT]; } TtmMap;
enum { TS_REBOOT, TS_LIVE, TS_END };
// events (bits) from ttm_update
#define TEV_ONLINE    (1u << 0)    // the active terminal can be captured now ("Capture the terminal!")
#define TEV_CAPTURE   (1u << 1)    // last_team captured (last_actor did it); a new terminal reboots (or the match ends)
#define TEV_FINAL     (1u << 2)    // the active terminal is final_team's last one
#define TEV_MULT      (1u << 3)    // the multiplier stepped up
#define TEV_OVERTIME  (1u << 4)
#define TEV_END       (1u << 5)    // winner set (0 / 1, 2 = draw)
#define TEV_MOVING    (1u << 6)    // the bar started moving for moving_team
#define TEV_LAST60    (1u << 7)
#define TEV_LAST20    (1u << 8)
#define TEV_REBOOT    (1u << 9)
typedef struct {
	const TtmMap *map; int state; float t;
	int active; float bar, live_t; int mult, moving, moving_team;   // bar: 0..1, team 0 pushes it to 1
	int nin[2], capturer, cap_team;                                // operators in the area; the named capturer
	int score[2], to_win; float time_left, ot_left; int overtime, winner;
	int owner[TTM_MAXT];                                           // last team that captured each terminal (-1 none)
	int last_team, last_actor, final_team;                         // final_team: the team whose final terminal is active (-1 none)
	int inside[TTM_NACT];                                          // actor a is in the active area
	int ready[TTM_NACT]; float ready_t[TTM_NACT];                  // TACHIKOMA READY per actor (bots call theirs after ready_t)
	int first_in[2];                                               // first actor of each team to enter (the capturer name)
	unsigned pend;
	float speed;                                                   // 1; the test run (autoplay) uses more to see captures in a minute
} TtmMatch;

static int ttm_load(TtmMap *M, const char *path) {
	memset(M, 0, sizeof(*M)); FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned v, n; int st;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GTTM", 4) || fread(&v, 4, 1, f) != 1 || v != 1 || fread(&n, 4, 1, f) != 1 || fread(&st, 4, 1, f) != 1 || n < 1 || n > TTM_MAXT) { fclose(f); return -2; }
	M->n = n; M->start = st;
	for (unsigned i = 0; i < n; i++) {
		TtmTerm *t = &M->t[i]; char pad[3];
		fread(t->name, 16, 1, f); fread(&t->letter, 1, 1, f); fread(pad, 3, 1, f); fread(t->next, 4, 2, f);
		fread(t->dev, 4, 4, f); fread(t->side, 4, 8, f);
		for (int k = 0; k < 2; k++) { unsigned c; fread(&c, 4, 1, f); t->nsp[k] = c > 24 ? 24 : c; fread(t->sp[k], 16, t->nsp[k], f); if (c > 24) fseek(f, (c - 24) * 16, SEEK_CUR); }
		unsigned nt; fread(&nt, 4, 1, f); t->ntri = nt; t->tri = malloc(nt * 36 + 4); t->tz = malloc(nt * 8 + 4); fread(t->tri, 36, nt, f);
		for (unsigned k = 0; k < nt; k++) { float *q = t->tri + k * 9; t->tz[k * 2] = fminf(q[2], fminf(q[5], q[8])); t->tz[k * 2 + 1] = fmaxf(q[2], fmaxf(q[5], q[8])); }
		fread(t->lo, 4, 3, f); fread(t->hi, 4, 3, f);
	}
	int ok = !ferror(f) && !feof(f); fclose(f); M->ok = ok; if (M->start < 0 || M->start >= (int)n) M->start = 0; return ok ? 0 : -3;
}
static void ttm_free(TtmMap *M) { for (int i = 0; i < M->n; i++) { free(M->t[i].tri); free(M->t[i].tz); } memset(M, 0, sizeof(*M)); }
// a point (feet) inside terminal t's capture area: over one of its floor triangles, between 1.5 m below and 3.5 m above it
static int ttm_in_area(const TtmTerm *t, V3 p) {
	if (p.x < t->lo[0] || p.x > t->hi[0] || p.y < t->lo[1] || p.y > t->hi[1] || p.z < t->lo[2] - 150 || p.z > t->hi[2] + 350) return 0;
	for (int k = 0; k < t->ntri; k++) {
		if (p.z < t->tz[k * 2] - 150 || p.z > t->tz[k * 2 + 1] + 350) continue;
		const float *q = t->tri + k * 9;
		float d1 = (p.x - q[3]) * (q[1] - q[4]) - (q[0] - q[3]) * (p.y - q[4]), d2 = (p.x - q[6]) * (q[4] - q[7]) - (q[3] - q[6]) * (p.y - q[7]), d3 = (p.x - q[0]) * (q[7] - q[1]) - (q[6] - q[0]) * (p.y - q[1]);
		int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
		if (!(neg && pos)) return 1;
	}
	return 0;
}
// a random point of the area (area-weighted triangle, random barycentric); the device when there is no area
static void ttm_area_point(const TtmTerm *t, float *o) {
	if (!t->ntri) { o[0] = t->dev[0] + (rand() % 600 - 300); o[1] = t->dev[1] + (rand() % 600 - 300); o[2] = t->dev[2]; return; }
	float tot = 0; for (int k = 0; k < t->ntri; k++) { const float *q = t->tri + k * 9; tot += fabsf((q[3] - q[0]) * (q[7] - q[1]) - (q[6] - q[0]) * (q[4] - q[1])); }
	float r = (rand() % 10000) / 10000.f * tot; int k = 0;
	for (; k < t->ntri - 1; k++) { const float *q = t->tri + k * 9; r -= fabsf((q[3] - q[0]) * (q[7] - q[1]) - (q[6] - q[0]) * (q[4] - q[1])); if (r <= 0) break; }
	const float *q = t->tri + k * 9; float a = (rand() % 1000) / 1000.f, b = (rand() % 1000) / 1000.f; if (a + b > 1) { a = 1 - a; b = 1 - b; }
	for (int c = 0; c < 3; c++) o[c] = q[c] + (q[3 + c] - q[c]) * a + (q[6 + c] - q[c]) * b;
}
static int ttm_final_of(const TtmMatch *m) {   // the team whose final terminal the active one is (-1 none)
	const TtmTerm *t = &m->map->t[m->active]; return t->next[0] < 0 ? 1 : t->next[1] < 0 ? 0 : -1;
}
static void ttm_start(TtmMatch *m, const TtmMap *map, int to_win, float match_s) {
	memset(m, 0, sizeof(*m)); m->map = map; m->active = map->start; m->state = TS_REBOOT; m->t = 0; m->bar = 0.5f; m->mult = 1;
	m->to_win = to_win > 0 ? to_win : TTM_TO_WIN; m->time_left = match_s > 0 ? match_s : TTM_MATCH_T; m->winner = -1;
	m->capturer = -1; m->cap_team = -1; m->last_team = -1; m->last_actor = -1; m->final_team = -1; m->moving_team = -1;
	for (int i = 0; i < TTM_MAXT; i++) m->owner[i] = -1;
	m->first_in[0] = m->first_in[1] = -1; m->pend = TEV_REBOOT; m->speed = 1;
}
static void ttm_end(TtmMatch *m, int winner) { if (m->state == TS_END) return; m->state = TS_END; m->t = 0; m->winner = winner; m->pend |= TEV_END; }
// in_team[a] = team of actor a if it is alive and inside the area this frame, else -1; n = actors
static unsigned ttm_update(TtmMatch *m, const int *team_in, int n, float dt) {
	unsigned ev = m->pend; m->pend = 0;
	if (m->state == TS_END) { m->t += dt; return ev; }
	const TtmTerm *T = &m->map->t[m->active];
	// clock (overtime after a tied regulation time)
	float tl0 = m->time_left;
	if (!m->overtime) {
		m->time_left -= dt;
		if (tl0 > 60 && m->time_left <= 60) ev |= TEV_LAST60;
		if (tl0 > 20 && m->time_left <= 20) ev |= TEV_LAST20;
		if (m->time_left <= 0) { m->time_left = 0;
			if (m->score[0] != m->score[1]) { ttm_end(m, m->score[0] > m->score[1] ? 0 : 1); return ev | m->pend; }
			m->overtime = 1; m->ot_left = TTM_OVERTIME; ev |= TEV_OVERTIME; }
	} else {
		m->ot_left -= dt;
		if (m->ot_left <= 0) { m->ot_left = 0; ttm_end(m, m->bar > 0.501f ? 0 : m->bar < 0.499f ? 1 : 2); return ev | m->pend; }
	}
	m->nin[0] = m->nin[1] = 0;
	for (int a = 0; a < n && a < TTM_NACT; a++) { m->inside[a] = team_in[a] >= 0; if (team_in[a] == 0 || team_in[a] == 1) m->nin[team_in[a]]++; }
	for (int k = 0; k < 2; k++) if (m->first_in[k] >= 0 && (m->first_in[k] >= n || team_in[m->first_in[k]] != k)) m->first_in[k] = -1;
	for (int a = 0; a < n && a < TTM_NACT; a++) { int k = team_in[a]; if ((k == 0 || k == 1) && m->first_in[k] < 0) m->first_in[k] = a; }
	m->t += dt * m->speed;
	if (m->state == TS_REBOOT) {
		m->capturer = -1; m->moving = 0;
		if (m->t >= TTM_REBOOT) { m->state = TS_LIVE; m->t = 0; m->live_t = 0; m->mult = 1; m->bar = 0.5f; ev |= TEV_ONLINE;
			m->final_team = ttm_final_of(m); if (m->final_team >= 0) ev |= TEV_FINAL; }
		return ev;
	}
	// live: the tug of war
	m->live_t += dt * m->speed; { int mu = 1 + (int)(m->live_t / TTM_MULT_T); if (mu > 3) mu = 3; if (mu != m->mult) { m->mult = mu; ev |= TEV_MULT; } }
	int k = m->nin[0] > 0 && m->nin[1] == 0 ? 0 : m->nin[1] > 0 && m->nin[0] == 0 ? 1 : -1;
	if (k >= 0 && ((k == 0 && m->bar < 1) || (k == 1 && m->bar > 0))) {
		if (!m->moving || m->moving_team != k) { m->moving = 1; m->moving_team = k; ev |= TEV_MOVING; }
		m->bar += (k == 0 ? 1 : -1) * TTM_RATE * m->mult * dt * m->speed; m->capturer = m->first_in[k]; m->cap_team = k;
	} else { m->moving = 0; m->capturer = -1; }
	if (m->bar >= 1 || m->bar <= 0) {
		int w = m->bar >= 1 ? 0 : 1; m->bar = w == 0 ? 1.f : 0.f;
		m->score[w]++; m->owner[m->active] = w; m->last_team = w; m->last_actor = m->first_in[w]; ev |= TEV_CAPTURE;
		if (m->last_actor >= 0 && m->last_actor < TTM_NACT) { m->ready[m->last_actor] = 1; m->ready_t[m->last_actor] = 2 + (rand() % 30) / 10.f; }
		int nx = T->next[w];
		if (nx < 0 || m->score[w] >= m->to_win || m->overtime) { ttm_end(m, w); return ev | m->pend; }
		m->active = nx; m->state = TS_REBOOT; m->t = 0; m->bar = 0.5f; m->live_t = 0; m->mult = 1; m->moving = 0; m->capturer = -1; m->final_team = -1;
		m->first_in[0] = m->first_in[1] = -1; ev |= TEV_REBOOT;
	}
	return ev;
}

#ifndef TTM_RULES_ONLY
// ==================================================================================================================
// The mode in the engine: rewards and call-ins, bots, audio, terminals in the world, HUD. The game (main.c "TTM glue")
// fills the hooks below and calls ttm_begin / ttm_frame / ttm_draw / ttm_hud / ttm_overlay.
static TtmMap ttm_map; static TtmMatch tm; static int ttm_on;
static Model ttm_term; static int ttm_term_ok;
static float ttm_now, ttm_banner_t = 99, ttm_flash_t = 99, ttm_start_t, ttm_notice_t = 99, ttm_ready_t = 99, ttm_sd_t = 99;
static char ttm_banner[2][96], ttm_notice[48]; static int ttm_banner_col;   // 0 yellow title
static const char *ttm_music_cur;
static int ttm_hacker_bot[2] = { -1, -1 }; static float ttm_hack_pick_t;
static float ttm_say_t[4]; static char ttm_say_q[4][40];
static V3 ttm_call_pt; static float ttm_call_t = 99;
// the call-in placement (footage: "TACHIKOMA READY - PRESS [key]" -> a hologram of the tank where the player looks, "Click to deploy"
// -> fire deploys it): ttm_placing is on between the call button and the deploy press; the game keeps the gun quiet meanwhile
static int ttm_placing, ttm_place_ok, ttm_auto_deploy; static float ttm_place_t; static V3 ttm_place_pt; static float ttm_place_yaw;
static GLuint ttm_hud_tex;   // tk_hud.gtx: the Think Tank panels' pictures (tools/tank/make_tk_hud.py), 0 = plain panels
// hooks the game sets
static void (*ttm_feed)(const char *killer, int kside, const char *victim, int vside, int icon);   // kill feed line
static void (*ttm_points)(int actor, const char *text, int pts);                                    // "+200" style notice for the local player
static const char *ttm_voice_stem = "motoko";   // the local player's operator voice set (k_<op>_killtachikoma)

// ---- audio helpers: Aramaki over the radio (click, line, click), music cues (data/audio/<name>.gstr, flat on the Vita)
static void ttm_say(const char *prefix, float delay) {
	for (int i = 0; i < 4; i++) if (ttm_say_t[i] <= 0 && !ttm_say_q[i][0]) { ttm_say_t[i] = delay + 0.14f; snprintf(ttm_say_q[i], sizeof(ttm_say_q[i]), "%s", prefix);
		if (delay > 0) ev_play_later("ui/doughnut_on", NULL, delay); else ev_play("ui/doughnut_on", NULL); return; }
}
static void ttm_say_update(float dt) {
	for (int i = 0; i < 4; i++) { if (!ttm_say_q[i][0]) continue; ttm_say_t[i] -= dt;
		if (ttm_say_t[i] <= 0) { const VEntry *e = find_vline(ttm_say_q[i]); if (e) { voice_play_entry(e, (int)(0.43f * 65536 / (voice_gain > 0.05f ? voice_gain : 0.05f)), 0, 0); ev_play_later("ui/doughnut_off", NULL, vline_secs(e) + 0.1f); }
			ttm_say_q[i][0] = 0; } }
}
static void ttm_music(const char *name, int loop, int fade_ms) {
	if (ttm_music_cur == name) return; ttm_music_cur = name;
	char p[112]; snprintf(p, sizeof(p), "%.56s%.48s.gstr", snd_dir, name);
	music_play(p, loop, (int)(0.5f / (music_gain > 0.05f ? music_gain : 0.05f) * 256), fade_ms);
}
static void ttm_show(const char *title, const char *sub, int col) { snprintf(ttm_banner[0], 96, "%s", title); snprintf(ttm_banner[1], 96, "%s", sub ? sub : ""); ttm_banner_col = col; ttm_banner_t = 0; }
static void ttm_note(const char *txt) { snprintf(ttm_notice, sizeof(ttm_notice), "%s", txt); ttm_notice_t = 0; }

// ---- spawns: the active terminal's list for the team
static int ttm_spawn_point(int team, float *xyz, float *yaw) {
	if (!ttm_on || !ttm_map.ok) return 0;
	const TtmTerm *t = &ttm_map.t[tm.active]; int n = t->nsp[team & 1]; if (!n) return 0;
	const float *s = t->sp[team & 1][rand() % n]; xyz[0] = s[0]; xyz[1] = s[1]; xyz[2] = s[2]; if (yaw) *yaw = s[3]; return 1;
}
static int ttm_spawn_pick_bot(int team) {   // bots.h bot_spawn_pick wants a spawns[] index: the map spawn nearest to a list point
	float p[3]; if (!ttm_spawn_point(team, p, NULL) || !nspawn) return -1;
	int best = -1; float bd = 1e18f; for (int i = 0; i < nspawn; i++) { float dx = spawns[i].x - p[0], dy = spawns[i].y - p[1], dz = spawns[i].z - p[2], d = dx * dx + dy * dy + dz * dz * 4; if (d < bd) { bd = d; best = i; } }
	return best;
}
static void ttm_place_bot(int i) {   // after bot_spawn: move it onto the TTM list point exactly
	float p[3], y; if (!ttm_spawn_point(bots[i].team, p, &y)) return;
	bots[i].pl.pos = v3(p[0], p[1], p[2] + 5); bots[i].pl.yaw = y; bots[i].floor_z = p[2]; bots[i].npath = 0; bots[i].repath_t = 0;
}

// ---- Think Tank call-ins
static const char *ttm_tank_name(int a, char *buf, int n) { if (a == 0) return "My Think Tank"; snprintf(buf, n, "%sKoma", tk_actor_name(a)); return buf; }
static int ttm_call_tank(int a, V3 at, float yaw) {
	int team = tk_actor_team(a); if (team < 0 || tk_count(team) > 0) return -1;
	char nb[32]; int i = tk_spawn(at, yaw, team, a, tk_actor_name(a), ttm_tank_name(a, nb, sizeof(nb)), TKF_DROP);
	if (i < 0) return -1;
	tm.ready[a] = 0;
	if (a == 0) { ttm_flash_t = 0; tk_snd("fp_tachi_deploy", at, 0.8f, 10000); if (ttm_points) ttm_points(0, "TACHIKOMA DEPLOYED", 200); }
	if (team == tk_local_team) { if (a != 0) ttm_say("d_tachi_spawn_", 0.5f); }
	else { const VEntry *e = find_vline("d_op_warn_tachi_"); if (e) voice_play_entry(e, (int)(0.3f * 65536 / (voice_gain > 0.05f ? voice_gain : 0.05f)), 0, 40); }
	return i;
}
// where the local player's call-in lands: the floor along the view (<= 15 m), else 4 m ahead
static int ttm_call_point(V3 eye, V3 dir, V3 *out) {
	int mat; V3 n; float h = raycast(eye, dir, 1500, &mat, &n); V3 p;
	if (h > 0 && n.z > 0.6f) p = vadd(eye, vmul(dir, h)); else { V3 f = v3(dir.x, dir.y, 0); float l = sqrtf(f.x * f.x + f.y * f.y) + 1e-3f; f = vmul(f, 1 / l);
		float k = h > 0 ? fminf(400, h * 0.8f) : 400; p = vadd(eye, vmul(f, k)); float d2 = raycast(p, v3(0, 0, -1), 600, NULL, &n); if (d2 < 0) return 0; p.z -= d2; }
	*out = p; return 1;
}

// ---- tanks: objective = the active terminal; events from thinktank.h
static int ttm_tank_goal(int i, float *g) {
	if (!ttm_on) return 0; const TtmTerm *t = &ttm_map.t[tm.active];
	if (tk[i].has_goal) { float dx = tk[i].goal[0] - t->dev[0], dy = tk[i].goal[1] - t->dev[1]; if (dx * dx + dy * dy < 3000.f * 3000.f && (rand() % 4)) return 0; }
	ttm_area_point(t, g); return 1;
}
static void ttm_tank_event(int i, int ev, int a) {
	Tank *k = &tk[i]; int mine = k->team == tk_local_team;
	switch (ev) {
	case TKE_DESTROYED: {
		const char *kn = a >= 0 ? tk_actor_name(a) : "";
		if (ttm_feed) ttm_feed(a >= 0 ? kn : "", a == 0 ? 1 : (a >= 0 && tk_actor_team(a) == tk_local_team) ? 0 : -1, k->name, mine ? (k->owner == 0 ? 1 : 0) : -1, 2);
		if (a >= 0 && tk_actor_team(a) != k->team && a < TTM_NACT) { tm.ready[a] = 1; tm.ready_t[a] = 3 + (rand() % 30) / 10.f; }   // the destroyer earns one
		if (a == 0) { if (ttm_points) ttm_points(0, "TACHIKOMA DESTROYED", 300); snd_killvoice(ttm_voice_stem, "killtachikoma"); }
		break; }
	case TKE_HACK_START: if (mine) { ev_play("ui/ghost_team_hacked", NULL); } break;
	case TKE_HACK_DONE: ttm_show("Think Tank defensive protocols shutting down!", "Hack the enemy Think Tank to disable its defenses.", 0); if (!mine) ev_play("ui/ghost_brain_hacking_success", NULL); break;
	case TKE_HACK_FAIL: if (a == 0) ttm_note("HACKING FAILED"); break;
	case TKE_KILL: if (k->owner == 0 && a != 0) { ev_play("ui/assist", NULL); } break;
	case TKE_RESTORED: if (a == 0) ttm_note("You failed to destroy enemy Think Tank."); break;   // EA tutorial wording
	}
}

// ---- bots: go capture / defend the active terminal; one bot per team goes to hack an enemy tank
static int ttm_bot_goal(int i, float *g) {
	if (!ttm_on || tm.state == TS_END) return 0;
	Bot *b = &bots[i]; const TtmTerm *t = &ttm_map.t[tm.active];
	for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].hacker == 1 + i) {   // running a hack: stay within reach of it (the leash)
		g[0] = tk[k].pl.pos.x + 500 * cosf(i * 1.7f); g[1] = tk[k].pl.pos.y + 500 * sinf(i * 1.7f); g[2] = tk[k].pl.pos.z; return 1; }
	if (ttm_hacker_bot[b->team] == i) { for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].team != b->team && tk[k].def && tk[k].hacker < 0) {
		g[0] = tk[k].pl.pos.x; g[1] = tk[k].pl.pos.y; g[2] = tk[k].pl.pos.z; return 1; } }
	if (i % 4 == 3 && t->side[b->team][0] != 0) { const float *s = t->side[(i / 4 + b->team) & 1]; g[0] = s[0]; g[1] = s[1]; g[2] = s[2]; return 1; }   // one in four watches from a side point
	ttm_area_point(t, g); return 1;
}
static void ttm_bots(float dt) {
	ttm_hack_pick_t -= dt;
	if (ttm_hack_pick_t <= 0) { ttm_hack_pick_t = 2;   // the bot of each team nearest an enemy tank with its defences up (within 30 m) goes to hack it
		for (int team = 0; team < 2; team++) { int best = -1; float bd = 3000.f * 3000.f;
			for (int k = 0; k < TK_MAX; k++) { if (tk[k].state != TK_LIVE || tk[k].team == team || !tk[k].def || tk[k].hacker >= 0) continue;
				for (int i = 0; i < nbot; i++) { if (bots[i].team != team || bots[i].state == BS_DEAD) continue; V3 e = vsub(bots[i].pl.pos, tk[k].pl.pos); float d = vdot(e, e); if (d < bd) { bd = d; best = i; } } }
			if (best != ttm_hacker_bot[team] && best >= 0) bots[best].repath_t = 0;
			ttm_hacker_bot[team] = best; } }
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (b->state == BS_DEAD) continue;
		// the hacker: hold "use" next to the tank (stands still facing it, like planting in Demolition)
		if (ttm_hacker_bot[b->team] == i && b->state != BS_ENGAGE) {
			V3 eye = bot_eye(b); int best = -1; float bd = TK_HACK_REACH + 60;
			for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].team != b->team && tk[k].def && tk[k].hacker < 0) { V3 e = vsub(tk[k].sph[0], eye); float d = sqrtf(vdot(e, e)) - 75; if (d < bd) { bd = d; best = k; } }
			if (best >= 0) { V3 e = vsub(tk[best].sph[0], eye); float l = sqrtf(vdot(e, e)) + 1e-3f; V3 d = vmul(e, 1 / l);
				if (tk_use(1 + i, eye, d, 1, dt) >= 0) { b->hold = 1; b->hold_yaw = atan2f(d.y, d.x); b->hold_pitch = asinf(d.z); } }
		}
		// a bot's Tachikoma: called in a few seconds after it earned it, next to itself
		int a = 1 + i; if (a < TTM_NACT && tm.ready[a] && b->state != BS_ENGAGE) { tm.ready_t[a] -= dt;
			if (tm.ready_t[a] <= 0 && tk_count(b->team) == 0) { V3 f = v3(cosf(b->pl.yaw), sinf(b->pl.yaw), 0), p = vadd(b->pl.pos, vmul(f, 300));
				V3 n; float h = raycast(v3(p.x, p.y, p.z + 150), v3(0, 0, -1), 500, NULL, &n); if (h > 0) p.z = p.z + 150 - h; else p = b->pl.pos;
				ttm_call_tank(a, p, b->pl.yaw); } }
	}
}

// ---- start / stop
static int ttm_begin(const char *vdir, const char *stem, int to_win, float match_s) {
	char p[128]; ttm_on = 0; if (ttm_map.ok) ttm_free(&ttm_map);
	snprintf(p, sizeof(p), "%s%s.gttm", vdir, stem); int r = ttm_load(&ttm_map, p); if (r < 0) return r;
	if (tk_load(vdir) < 0) { ttm_free(&ttm_map); return -10; }
	if (!ttm_term_ok) { snprintf(p, sizeof(p), "%stk_term.gvm", vdir); ttm_term_ok = model_load(&ttm_term, p) == 0 ? 1 : -1; }
	if (!ttm_hud_tex) { snprintf(p, sizeof(p), "%stk_hud.gtx", vdir); ttm_hud_tex = sk_tex_load(p); }
	tk_clear(); tk_goal_fn = ttm_tank_goal; tk_on_event = ttm_tank_event; tk_hud_style = 0; ttm_placing = 0;
	if (to_win > 9) to_win = TTM_TO_WIN;   // a kill limit left over from another mode: the footage's first to 4
	ttm_start(&tm, &ttm_map, to_win, match_s); ttm_on = 1; ttm_start_t = 0; ttm_banner_t = ttm_flash_t = ttm_notice_t = ttm_ready_t = ttm_call_t = 99; ttm_music_cur = NULL;
	ttm_hacker_bot[0] = ttm_hacker_bot[1] = -1; memset(ttm_say_q, 0, sizeof(ttm_say_q)); memset(ttm_say_t, 0, sizeof(ttm_say_t));
	ev_play("ui/game_start_ttm_text", NULL); ttm_music("bgm_in_game_start_ttm_01", 0, 300); ttm_say("d_aramaki_ttm_startgame_", 1.4f);
	return 0;
}
static void ttm_stop(void) { if (ttm_on) { tk_clear(); tk_goal_fn = NULL; tk_on_event = NULL; } ttm_on = 0; ttm_placing = 0; }

// ---- per frame. eye/dir: the local player's view; use_held: Square; call_pr: the call-in button pressed this frame (starts /
// cancels the placement); deploy_pr: fire pressed this frame (deploys while placing); player_alive: 0 while dead.
// Returns the TEV_* bits (the game ends the match on TEV_END: tm.winner).
static unsigned ttm_frame(float dt, V3 eye, V3 dir, int use_held, int call_pr, int deploy_pr, int player_alive) {
	if (!ttm_on) return 0;
	ttm_now += dt; ttm_start_t += dt; ttm_banner_t += dt; ttm_flash_t += dt; ttm_notice_t += dt; ttm_ready_t += dt; ttm_call_t += dt; ttm_sd_t += dt;
	ttm_say_update(dt);
	// who is in the active area
	int team_in[TTM_NACT], n = tk_w ? tk_w->n_actors() : 0; if (n > TTM_NACT) n = TTM_NACT;
	const TtmTerm *T = &ttm_map.t[tm.active];
	for (int a = 0; a < n; a++) { team_in[a] = -1; if (!tk_w->alive(a)) continue; if (ttm_in_area(T, tk_w->pos(a))) team_in[a] = tk_actor_team(a); }
	int was_state = tm.state, was_active = tm.active;
	unsigned ev = ttm_update(&tm, team_in, n, dt);
	int me = tk_local_team;
	if (ev & TEV_ONLINE) { ttm_say("d_aramaki_ttm_termactive_", 0); ev_play("ui/intel_alarm", NULL); ttm_music((rand() & 1) ? "bgm_ttm_01_terminal_01" : "bgm_ttm_01_terminal_02", 0, 600); }
	if (ev & TEV_MOVING) { ttm_music("bgm_ttm_01_hacking", 1, 800); }
	if (ev & TEV_CAPTURE) {
		int w = tm.last_team; const char *who = tm.last_actor >= 0 ? tk_actor_name(tm.last_actor) : "";
		ev_play(w == me ? "ui/intel_capture_fellow" : "ui/intel_capture_enemy", NULL);
		if (tm.state != TS_END) {
			if (w == me) { ttm_show("Terminal secured!", "Move on to the next terminal.", 0); ttm_say(tm.last_actor == 0 ? "d_aramaki_ttm_teamsuccess_theperson_" : "d_aramaki_ttm_teamsuccess_bestoccupy_", 0.6f); }
			else { ttm_show("Terminal takeover failed!", "Regroup and secure the next terminal!", 0); ttm_say("d_aramaki_ttm_enemysuccess_", 0.6f); }
			ttm_music("bgm_ttm_01_start", 1, 1500);
		}
		if (ttm_feed) { char b[64]; snprintf(b, sizeof(b), "captured terminal %c", ttm_map.t[was_active].letter); ttm_feed(who, tm.last_actor == 0 ? 1 : w == me ? 0 : -1, b, -2, -1); }
		if (tm.last_actor == 0 && ttm_points) ttm_points(0, "TERMINAL CAPTURED", 500);
		for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].team == w) { tk_voice("tachi_teamsuccess_", tk[k].eye, 0.7f); break; }
		if (tm.last_actor == 0) { ttm_ready_t = -0.8f; }   // "TACHIKOMA READY" + the operator's summon line 0.8 s later
	}
	if (ev & TEV_FINAL) { int fin = tm.final_team;
		ttm_show("FINAL TERMINAL!", fin == me ? "Letting the enemy capture the final terminal will mean defeat regardless of score." : "Capturing the final terminal will secure victory regardless of score.", 0);
		ttm_music("bgm_ttm_01_final_score", 1, 1000); }
	if (ev & TEV_MULT) ev_play("ui/ghost_scan_triangle", NULL);
	if (ev & TEV_OVERTIME) { ttm_show("OVERTIME", "The next capture decides the match.", 0); ev_play("ui/round_overtime", NULL); }
	if (ev & TEV_LAST60) ev_play("ui/crisis", NULL);
	if (ev & TEV_LAST20) { ev_play("ui/ghost_time_warning", NULL); radio_play("voice/donut/tdm/20sec"); }
	if (ev & TEV_END) { int w = tm.winner; music_stop(300); ttm_music_cur = NULL;
		ttm_say(w == 2 ? "d_aramaki_ttm_draw_" : w == me ? "d_aramaki_ttm_win_" : "d_aramaki_ttm_lose_", 1.0f); }
	(void)was_state;
	// the local player: TACHIKOMA READY -> call it in where he looks
	if (tm.ready[0] && ttm_ready_t > 90) ttm_ready_t = 0;
	if (tm.ready[0] && tm.state != TS_END && player_alive) {
		if (ttm_ready_t > 0 && ttm_ready_t - dt <= 0) { const VEntry *e = find_vline("d_op_tachi_summon_"); if (e) voice_play_entry(e, (int)(0.3f * 65536 / (voice_gain > 0.05f ? voice_gain : 0.05f)), 0, 40); ev_play("ui/ready_on", NULL); }
		if (call_pr) { if (tk_count(me) > 0) { ttm_note("THINK TANK ALREADY DEPLOYED"); ttm_placing = 0; } else { ttm_placing = !ttm_placing; ttm_place_t = 0; ev_play(ttm_placing ? "ui/tab_on" : "ui/tab_off", NULL); } }
		if (ttm_placing) { ttm_place_t += dt; ttm_place_ok = ttm_call_point(eye, dir, &ttm_place_pt); ttm_place_yaw = atan2f(dir.y, dir.x);
			if ((deploy_pr || (ttm_auto_deploy && ttm_place_t > 1.5f)) && ttm_place_ok && ttm_call_tank(0, ttm_place_pt, ttm_place_yaw) >= 0) { ttm_call_pt = ttm_place_pt; ttm_call_t = 0; ttm_placing = 0; } }
	} else ttm_placing = 0;
	ttm_bots(dt);
	// the local player's hack (Square next to an enemy tank)
	tk_use_prog = player_alive ? tk_use(0, eye, dir, use_held, dt) : -1;
	// keep everyone out of the terminal devices (radius 95 cm)
	for (int t = 0; t < ttm_map.n; t++) { const float *d = ttm_map.t[t].dev;
		for (int j = -1; j < nbot; j++) { Player *p = j < 0 ? tk_pl : &bots[j].pl; if (!p) continue; float dx = p->pos.x - d[0], dy = p->pos.y - d[1], d2 = dx * dx + dy * dy;
			if (d2 < 95.f * 95.f && d2 > 1 && p->pos.z < d[2] + 240 && p->pos.z > d[2] - 100) { float l = sqrtf(d2); p->pos.x = d[0] + dx / l * 95; p->pos.y = d[1] + dy / l * 95; } } }
	return ev;
}

// ---- the terminals in the world: device + floor glow in the holder's colour (white neutral, cyan ours, red theirs);
// the active one pulses while it is being captured
static void ttm_team_col(int team, float *c) { if (team < 0) { c[0] = 0.85f; c[1] = 0.9f; c[2] = 0.95f; } else if (team == tk_local_team) { c[0] = 0.2f; c[1] = 0.75f; c[2] = 1.0f; } else { c[0] = 1.0f; c[1] = 0.22f; c[2] = 0.2f; } }
static void ttm_draw(const float *VP, const float *fog) {
	if (!ttm_on || ttm_term_ok <= 0) return;
	static MBone one[2]; one[0].q[3] = 1; one[1].q[3] = 1;
	glDisable(GL_CULL_FACE);
	for (int i = 0; i < ttm_map.n; i++) {
		const TtmTerm *t = &ttm_map.t[i]; V3 d = vsub(v3(t->dev[0], t->dev[1], t->dev[2]), lis_pos); if (vdot(d, d) > 9000.f * 9000.f) continue;
		float c = cosf(t->dev[3]), s = sinf(t->dev[3]);
		float M[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, t->dev[0], t->dev[1], t->dev[2], 1 }, MVP[16];
		for (int col = 0; col < 4; col++) for (int r = 0; r < 4; r++) { float a = 0; for (int q = 0; q < 4; q++) a += VP[q * 4 + r] * M[col * 4 + q]; MVP[col * 4 + r] = a; }
		float L[3]; light_at(t->dev[0], t->dev[1], t->dev[2] + 60, L); float tint[4] = { fminf(1.4f, L[0] * 1.15f + 0.1f), fminf(1.4f, L[1] * 1.15f + 0.1f), fminf(1.4f, L[2] * 1.15f + 0.1f), 1 };
		const float ld[4] = { 0.3f, -0.6f, 0.75f, 0 };
		md_mesh0 = 0; md_mesh1 = 2; model_draw(&ttm_term, MVP, one, ld, tint, fog);
		// glow: the active terminal shows who is pushing (or white), the others their holder; rebooting = dim
		int team = i == tm.active ? (tm.state == TS_LIVE && tm.moving ? tm.moving_team : -1) : tm.owner[i];
		float g[4]; ttm_team_col(team, g); float a = i == tm.active ? (tm.state == TS_LIVE ? 0.85f + 0.15f * sinf(ttm_now * 6) : 0.35f) : 0.3f; g[3] = a;
		const float nofog[4] = { 0, 0, 0, 0 };
		md_mesh0 = 2; md_mesh1 = 6; model_draw(&ttm_term, MVP, one, ld, g, nofog);
		md_mesh0 = 0; md_mesh1 = 1 << 30;
	}
	glEnable(GL_CULL_FACE);
	// the placement: a white hologram of the tank where it will land + a ring on the floor (footage: "Click to deploy")
	if (ttm_placing && ttm_place_ok) { const float hc[4] = { 0.75f, 0.92f, 1.0f, 0.42f + 0.08f * sinf(ttm_now * 6) }, rc[4] = { 0.3f, 0.85f, 1, 0.75f };
		tk_draw_holo(VP, tk_local_team == 1, ttm_place_pt, ttm_place_yaw, hc);
		sprite_ground(ttm_place_pt.x, ttm_place_pt.y, ttm_place_pt.z + 4, 150, 150, 0, rc); sprite_flush(VP, tk_ringt, 1); }
	// the call-in landing marker (a cyan ring for a moment)
	if (ttm_call_t < 1.2f) { float a = 1 - ttm_call_t / 1.2f; const float col[4] = { 0.3f, 0.85f, 1, 0.8f * a }; float r = 120 + 60 * ttm_call_t;
		sprite_ground(ttm_call_pt.x, ttm_call_pt.y, ttm_call_pt.z + 4, r, r, 0, col); sprite_flush(VP, tk_ringt, 1); }
}

// ---- HUD (hud.h primitives in the main HUD pack; Vita pixels, positions from the 720p footage x 0.75, re-flowed for the
// legibility rule in BRIEF.md: labels >= 24 px, fine print >= 20 px at 960x544)
#define ttm_rect tk_rect
#define ttm_quad tk_quad
#define ttm_ring tk_ring
static void ttm_person(float x, float y, float s, const float *c) {   // the little operator icon of the bar's counts
	ttm_ring(x, y - s * 0.55f, 0, s * 0.32f, c);
	float q[4][2] = { { x - s * 0.5f, y + s * 0.5f }, { x + s * 0.5f, y + s * 0.5f }, { x + s * 0.32f, y }, { x - s * 0.32f, y } }; ttm_quad((const float (*)[2])q, c);
}
// centred text, wrapped onto a second line (at the space nearest the middle) when wider than maxw; returns the lines used
static int ttm_text_wrap(float x, float y, float size, float lh, float maxw, const float *c, const char *s) {
	if (hud_text_w(s) * size <= maxw) { hud_text(HG.screen, x, y, size, 2, c, HUD_Z4, s); return 1; }
	int n = (int)strlen(s), best = -1; for (int i = 0; i < n; i++) if (s[i] == ' ' && (best < 0 || abs(i - n / 2) < abs(best - n / 2))) best = i;
	if (best < 0) { hud_text(HG.screen, x, y, size, 2, c, HUD_Z4, s); return 1; }
	char l0[128]; int k = best < 127 ? best : 127; memcpy(l0, s, k); l0[k] = 0;
	hud_text(HG.screen, x, y, size, 2, c, HUD_Z4, l0); hud_text(HG.screen, x, y + lh, size, 2, c, HUD_Z4, s + best + 1); return 2;
}
static void ttm_clip(char *o, int n, const char *s, float size, float maxw) {   // a name cut to fit maxw ("..")
	snprintf(o, n, "%s", s); int l = (int)strlen(o); while (l > 1 && hud_text_w(o) * size > maxw) { o[--l] = 0; if (l > 2) { o[l - 1] = '.'; o[l - 2] = '.'; } }
}
// a Think Tank's panel (TC_TACHICOMA; footage: ours left of the score, theirs right of it): the tank's picture (tk_hud.gtx, by
// model), owner and tank name (or a red "Hacker Detected" band), HP with ten segments, the shutdown sign while the defences are
// down. 186 x 84 Vita px (the footage's 98 x 71, widened for the 20 px names).
#define TTM_PW 186
#define TTM_PH 84
static void ttm_panel(int i, float x0, float y, int right) {
	Tank *k = &tk[i]; float c[4]; ttm_team_col(k->team, c); c[3] = 0.95f;
	const float wm[4] = { 1, 1, 1, 0.97f }, rd[4] = { 1, 0.27f, 0.24f, 0.97f }, dim[4] = { c[0] * 0.3f, c[1] * 0.3f, c[2] * 0.3f, 0.75f }, sub[4] = { 0.8f, 0.88f, 0.93f, 0.95f };
	const float w = TTM_PW, h = TTM_PH;
	if (ttm_hud_tex) {   // the picture fills the panel (atlas: blue model at v 0..112/256, red at 128..240)
		float v0 = k->kind ? 128 / 256.f : 0, v1 = v0 + 112 / 256.f, u0 = 0, u1 = 248 / 256.f;
		float q[4][2] = { { x0, y }, { x0 + w, y }, { x0 + w, y + h }, { x0, y + h } }, uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
		hud_poly(ttm_hud_tex, HG.screen, (const float (*)[2])q, (const float (*)[2])uv, 4, (const float[4]){ 0.9f, 0.9f, 0.9f, 0.92f });
	} else ttm_rect(x0, y, x0 + w, y + h, (const float[4]){ c[0] * 0.15f, c[1] * 0.15f, c[2] * 0.15f, 0.7f });
	ttm_rect(x0, y, x0 + w, y + 50, (const float[4]){ 0, 0, 0, 0.3f });                 // under the names
	ttm_rect(x0, y + 54, x0 + w, y + h, (const float[4]){ 0, 0, 0, 0.35f });             // under HP
	ttm_rect(x0, y, x0 + w, y + 2, c); ttm_rect(right ? x0 + w - 3 : x0, y, right ? x0 + w : x0 + 3, y + h, c);
	float nx = right ? x0 + w - 9 : x0 + 9; int al = right ? 1 : 0; char b[40];
	ttm_clip(b, sizeof(b), k->owner_name, 20, w - 18); hud_text(HG.screen, nx, y + 21, 20, al, wm, HUD_Z4, b);
	if (k->hacker >= 0) { ttm_rect(x0 + 3, y + 28, x0 + w - 3, y + 50, (const float[4]){ 0.62f, 0.05f, 0.07f, 0.88f }); hud_text(HG.screen, x0 + w / 2, y + 46, 20, 2, wm, HUD_Z4, "Hacker Detected"); }
	else { ttm_clip(b, sizeof(b), k->name, 20, w - 18 - (!k->def ? 30 : 0)); hud_text(HG.screen, nx, y + 45, 20, al, sub, HUD_Z4, b); }
	// HP: "HP" tag on the inner side, the number on the outer side, ten segments under it
	snprintf(b, sizeof(b), "%d", (int)(k->hp + 0.999f)); hud_text(HG.screen, right ? x0 + w - 9 : x0 + 9, y + 77, 28, al, k->def ? wm : rd, HUD_Z4, b);
	{ float tx = right ? x0 + 8 : x0 + w - 36; ttm_rect(tx, y + 58, tx + 28, y + 77, (const float[4]){ 0.62f, 0.12f, 0.12f, 0.9f }); hud_text(HG.screen, tx + 14, y + 75, 20, 2, wm, HUD_Z4, "HP"); }
	for (int s = 0; s < 10; s++) { float f = k->hp / k->hpmax * 10 - s, sx = right ? x0 + w - 12 - (s + 1) * 9.5f : x0 + 9 + s * 9.5f; const float *cc = f > 0.5f ? (k->def ? c : rd) : dim;
		float q[4][2] = { { sx + 2, y + 79 }, { sx + 8, y + 79 }, { sx + 6, y + h - 1 }, { sx, y + h - 1 } }; ttm_quad((const float (*)[2])q, cc); }
	if (!k->def) { float cx = right ? x0 + 22 : x0 + w - 22, cy = y + 39; ttm_ring(cx, cy, 9, 12, rd); float q[4][2] = { { cx - 8, cy - 9 }, { cx - 6, cy - 10.5f }, { cx + 8, cy + 9 }, { cx + 6, cy + 10.5f } }; ttm_quad((const float (*)[2])q, rd); }
}
// a world point on screen; off screen (or behind the camera) it sticks to the edge in its direction, clear of the radar (top left)
static int ttm_screen_edge(const float *M, const float *p, float *sx, float *sy) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r];
	float x, y; if (q[3] > 10) { x = q[0] / q[3]; y = q[1] / q[3]; } else { float w = fmaxf(fabsf(q[0]), fabsf(q[1])) + 1e-3f; x = -q[0] / w * 2; y = -q[1] / w * 2; if (fabsf(x) < 1 && fabsf(y) < 1) { y = -2; } }
	float X = (x + 1) * 480, Y = (1 - y) * 272;
	if (X < 60 || X > 900 || Y < 150 || Y > 470) { float cx = X - 480, cy = Y - 310, s = fminf(cx != 0 ? 420 / fabsf(cx) : 1e9f, cy != 0 ? 160 / fabsf(cy) : 1e9f); if (s < 1) { X = 480 + cx * s; Y = 310 + cy * s; } }
	*sx = fminf(900, fmaxf(60, X)); *sy = fminf(470, fmaxf(150, Y)); return 1;
}
static void ttm_hud(const float *MVP, const float *cam, float now) {
	if (!ttm_on || HG.white < 0 || HG.screen < 0) return;
	(void)now; int me = tk_local_team;
	const float wm[4] = { 1, 1, 1, 0.97f }, bl[4] = { 0.33f, 0.78f, 0.95f, 0.97f }, rd[4] = { 0.95f, 0.27f, 0.3f, 0.97f }, dk[4] = { 0.02f, 0.05f, 0.07f, 0.62f }, gr[4] = { 0.62f, 0.64f, 0.66f, 0.92f };
	const float *cm = me == 0 ? bl : rd, *ce = me == 0 ? rd : bl;   // our colour on the left, theirs on the right
	float my = me == 0 ? tm.bar : 1 - tm.bar;                        // our share of the bar
	const float BX0 = 396, BX1 = 564, BY0 = 66, BY1 = 88;            // under the score / clock, between the panels (footage: 405..551 x 56..68), the counts at its ends
	if (tm.state == TS_REBOOT) {   // "Capture system rebooting..." (the grey part shrinks as the reboot ends)
		ttm_rect(BX0 - 56, BY0, BX1 + 56, BY1, dk); float f = 1 - tm.t / TTM_REBOOT; ttm_rect(BX0 - 56, BY0, BX0 - 56 + (BX1 - BX0 + 112) * f, BY1, (const float[4]){ 0.5f, 0.52f, 0.55f, 0.8f });
		ttm_rect(BX0 - 56, BY1 + 4, BX1 + 56, BY1 + 32, dk); hud_text(HG.screen, 480, BY1 + 25, 20, 2, wm, HUD_Z4, "Capture system rebooting...");
	} else if (tm.state == TS_LIVE) {
		ttm_rect(BX0, BY0, BX1, BY1, dk);
		float mid = BX0 + (BX1 - BX0) * my; ttm_rect(BX0, BY0, mid, BY1, cm); ttm_rect(mid, BY0, BX1, BY1, ce);
		if (tm.mult > 1) { char m[16]; snprintf(m, sizeof(m), "x%d", tm.mult); ttm_rect(462, BY0 + 1, 498, BY1 - 1, (const float[4]){ 0.95f, 0.95f, 0.95f, 0.85f }); hud_text(HG.screen, 480, BY1 - 4, 20, 2, (const float[4]){ 0.05f, 0.08f, 0.12f, 1 }, HUD_Z4, m); }
		if (tm.moving) { int up = (tm.moving_team == me); float q[4][2] = { { mid + (up ? -6 : 6), BY0 + 2 }, { mid + (up ? 4 : -4), (BY0 + BY1) / 2 }, { mid + (up ? -6 : 6), BY1 - 2 }, { mid + (up ? -3 : 3), (BY0 + BY1) / 2 } }; ttm_quad((const float (*)[2])q, wm); }
		// the counts of operators in the area, left ours, right theirs
		ttm_rect(BX0 - 56, BY0, BX0 - 4, BY1, dk); ttm_rect(BX1 + 4, BY0, BX1 + 56, BY1, dk);
		ttm_person(BX0 - 42, (BY0 + BY1) / 2 + 2, 14, cm); ttm_person(BX1 + 18, (BY0 + BY1) / 2 + 2, 14, ce);
		char nb[8]; snprintf(nb, sizeof(nb), "%d", tm.nin[me]); hud_text(HG.screen, BX0 - 19, BY1 - 3, 22, 2, cm, HUD_Z4, nb);
		snprintf(nb, sizeof(nb), "%d", tm.nin[1 - me]); hud_text(HG.screen, BX1 + 41, BY1 - 3, 22, 2, ce, HUD_Z4, nb);
		if (tm.capturer >= 0) { char b[48], nm[32]; ttm_clip(nm, sizeof(nm), tk_actor_name(tm.capturer), 20, tm.overtime ? 110 : 180); snprintf(b, sizeof(b), "%s capturing", nm); hud_text(HG.screen, 480, BY1 + 25, 20, 2, tm.cap_team == me ? wm : rd, HUD_Z4, b); }
		if (tm.overtime) { char p[8]; snprintf(p, sizeof(p), "%d%%", (int)(my * 100 + 0.5f)); hud_text(HG.screen, BX0 - 30, BY1 + 25, 22, 2, cm, HUD_Z4, p); snprintf(p, sizeof(p), "%d%%", (int)((1 - my) * 100 + 0.5f)); hud_text(HG.screen, BX1 + 30, BY1 + 25, 22, 2, ce, HUD_Z4, p);
			if (fabsf(my - 0.5f) > 0.004f) hud_text(HG.screen, 480, BY1 + 49, 20, 2, my > 0.5f ? cm : ce, HUD_Z4, my > 0.5f ? "Winning" : "Losing"); }
	}
	// terminal letters under the minimap: the active one boxed (yellow, or the colour of the side pushing it)
	for (int i = 0; i < ttm_map.n; i++) {
		int ord = 0; for (int j = 0; j < ttm_map.n; j++) if (ttm_map.t[j].letter < ttm_map.t[i].letter) ord++;
		float x = 72 + ord * 36, y = 170; char l[2] = { ttm_map.t[i].letter, 0 };
		if (i == tm.active) { const float *bc = tm.state != TS_LIVE ? gr : tm.moving ? (tm.moving_team == me ? cm : ce) : (const float[4]){ 0.95f, 0.82f, 0.2f, 0.92f };
			ttm_rect(x - 15, y - 24, x + 15, y + 6, bc); hud_text(HG.screen, x, y + 1, 24, 2, (const float[4]){ 0.05f, 0.05f, 0.05f, 1 }, HUD_Z4, l); }
		else { ttm_rect(x - 15, y - 24, x + 15, y + 6, dk); hud_text(HG.screen, x, y + 1, 24, 2, tm.owner[i] < 0 ? wm : tm.owner[i] == me ? cm : ce, HUD_Z4, l); }
	}
	// world marker of the active terminal: letter in a ring, distance, "FINAL" above it when it is someone's last
	{ const TtmTerm *t = &ttm_map.t[tm.active]; float p[3] = { t->dev[0], t->dev[1], t->dev[2] + 200 }, sx, sy;
		if (ttm_screen_edge(MVP, p, &sx, &sy)) {
			const float *c = tm.state == TS_LIVE && tm.moving ? (tm.moving_team == me ? cm : ce) : wm;
			ttm_ring(sx, sy, 0, 19, (const float[4]){ 0, 0, 0, 0.4f }); ttm_ring(sx, sy, 18, 21.5f, c);
			char l[2] = { t->letter, 0 }; hud_text(HG.screen, sx, sy + 9, 26, 2, c, HUD_Z4, l);
			float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2]; char dm[16]; snprintf(dm, sizeof(dm), "%dm", (int)(sqrtf(dx * dx + dy * dy + dz * dz) / 100)); hud_text(HG.screen, sx, sy + 43, 20, 2, wm, HUD_Z4, dm);
			if (tm.final_team >= 0 && tm.state == TS_LIVE) hud_text(HG.screen, sx, sy - 27, 20, 2, wm, HUD_Z4, "FINAL"); } }
	// Think Tank panels: ours left of the score, theirs right of it
	{ int mine = -1, theirs = -1; for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i)) { if (tk[i].team == me) { if (mine < 0) mine = i; } else if (theirs < 0) theirs = i; }
	  if (mine >= 0) ttm_panel(mine, 150, 2, 0); if (theirs >= 0) ttm_panel(theirs, 810 - TTM_PW, 2, 1); }
	// the call-in placement: "Press R to deploy" (footage: "Click to deploy" under the crosshair)
	if (ttm_placing) { const float bk[4] = { 0.02f, 0.03f, 0.05f, 0.62f };
		ttm_rect(340, 392, 620, 448, bk); ttm_rect(340, 392, 620, 394, (const float[4]){ 0.3f, 0.85f, 1, 0.8f });
		hud_text(HG.screen, 480, 416, 24, 2, ttm_place_ok ? wm : rd, HUD_Z4, ttm_place_ok ? "Press R to deploy" : "No room to deploy here");
		hud_text(HG.screen, 480, 441, 20, 2, gr, HUD_Z4, "SELECT: cancel"); }
}
// banners / flashes over the HUD (drawn after the HUD, before the menus' overlays)
static void ttm_overlay(void) {
	if (!ttm_on || HG.white < 0 || HG.screen < 0) return;
	hud_begin();
	if (ttm_flash_t < 0.6f) { float a = 0.55f * (1 - ttm_flash_t / 0.6f); ttm_rect(0, 0, 960, 544, (const float[4]){ 0.25f, 0.85f, 0.95f, a }); }   // the call-in flash (screen_tactical_resource)
	const float ye[4] = { 1, 0.86f, 0.25f, 1 };
	const char *t0 = NULL, *t1 = NULL; float bt = 99;
	if (ttm_banner_t < 4.5f) { t0 = ttm_banner[0]; t1 = ttm_banner[1]; bt = ttm_banner_t; }
	else if (tm.ready[0] && !ttm_placing && tm.state != TS_END && ttm_ready_t >= 0 && fmodf(ttm_ready_t, 12) < 4.5f) { t0 = "TACHIKOMA READY"; t1 = "PRESS SELECT"; bt = fmodf(ttm_ready_t, 12); }
	if (t0) { float a = bt < 0.2f ? bt / 0.2f : bt > 4 ? (4.5f - bt) / 0.5f : 1, m[4] = { ye[0], ye[1], ye[2], a }, w2[4] = { 1, 1, 1, a };
		int two = t1 && *t1 && hud_text_w(t1) * 22 > 860; float y1 = two ? 258 : 232;
		ttm_rect(0, 150, 960, y1, (const float[4]){ 0.02f, 0.04f, 0.06f, 0.6f * a }); ttm_rect(160, 151, 800, 152.5f, (const float[4]){ 1, 1, 1, 0.3f * a }); ttm_rect(160, y1 - 2.5f, 800, y1 - 1, (const float[4]){ 1, 1, 1, 0.3f * a });
		hud_text(HG.screen, 480, 190, 34, 2, m, HUD_Z4, t0); if (t1 && *t1) ttm_text_wrap(480, 220, 22, 26, 860, w2, t1); }
	if (ttm_notice_t < 2.5f) { float a = ttm_notice_t > 2 ? (2.5f - ttm_notice_t) * 2 : 1; ttm_text_wrap(480, 300, 24, 28, 900, (const float[4]){ 1, 0.88f, 0.3f, a }, ttm_notice); }
	hud_end();
}
#endif
