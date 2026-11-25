// Terminal Conquest (MODE_TTM): a chain of terminals (A - B - C on Underground, A..E on Berutalube / Ruined Tokyo); one is
// active at a time; capturing it scores a point and moves the fight one terminal towards the losers' end; capturing the
// other team's last terminal wins outright. Think Tanks (thinktank.h) are the capture reward.
// Rules measured in the footage (Terminal Conquest Underground Base - Sitara gameplay, 969 s):
//   - 15:00 match, first to 4 captures (the "4" between the scores), or the enemy's final terminal ("FINAL TERMINAL!").
//   - "Capture system rebooting..." 35 s before a terminal can be captured (match start and after every capture; the grey
//     bar shrinks). Then a tug-of-war bar from 50 %: only operators of ONE team inside the capture area move it towards
//     that team (any number of them), at 50 % per ~37 s; contested or empty: it stays. "<name> capturing" = the first of
//     the moving team in the area. The speed multiplier steps x1 -> x2 -> x3 every 60 s the terminal is online (shown
//     in the bar). 100 % (or 0 %): +1 for that team, "Terminal secured! Move on to the next terminal." /
//     "Terminal takeover failed! Regroup and secure the next terminal!", the next terminal reboots.
//   - Each operator has a Tachikoma call-in gauge (the HUD's "[E] <hexagon> NN" badge next to the HP ring; later footage:
//     Berutalube / Ruined Tokyo matches, 2016-17): 50 % at the match start, +2 % per second while he is in the area and his
//     side moves the bar, +28 % for the named capturer when the terminal falls, 100 % at once for destroying an enemy Think
//     Tank ("THINK TANK DESTROYED +200"); kills do not fill it. At 100 %: "TACHIKOMA READY" ("Your Tachikoma is ready to call
//     in."); the call-in empties it. One Think Tank per team at a time; a call-in right after a capture turns the capture
//     banner's line into "Deploying Tachikoma." / "Enemy Think Tank inbound!". Call-ins: 4000 HP (the player's), 3000 (bots').
//   - 15:00, first to 5 on the five-terminal maps (Berutalube, Ruined Tokyo: "0 5 0", VICTORY 5 - 4 / 4 - 1 by the final
//     terminal), first to 4 on Underground; both later matches start at C. "Timer Warning / Time is running out!" at 0:20.
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
#else              // the rules alone (a tool / test without the engine headers)
typedef struct { float x, y, z; } V3;
#define NBOT_MAX 8
#endif

#define TTM_REBOOT 35.0f
#define TTM_RATE (1.0f / 74.0f)   // bar per second at x1 (footage: 50 % -> 100 % in ~37 s)
#define TTM_MULT_T 60.0f
#define TTM_MATCH_T 900.0f
#define TTM_OVERTIME 180.0f
#define TTM_TO_WIN 4
#define TTM_GAUGE0 0.5f       // call-in gauge at the start (footage: 50 at 0:05 in both later matches)
#define TTM_GAUGE_RATE 0.02f  // per second capturing (footage: 52 -> 100 in 24 s, 4 -> 40 in 18 s)
#define TTM_GAUGE_CAP 0.28f   // the capturer's bonus (footage: 40 -> 68 when ADMAN314's capture completed)
#define TTM_WAVE 12.0f        // redeploy waves: the dead come back together (footage, 4 deaths: back at match time 120, 276, 312, 372 s,
#define TTM_WAVE_MIN 7.0f     // all multiples of 12 s; dead 5.8 s before a wave was too short, 8.8 s enough)
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
	float gauge[TTM_NACT];                                         // the call-in gauge 0..1 (ready at 1)
	int first_in[2];                                               // first actor of each team to enter (the capturer name)
	unsigned pend;
	float speed;                                                   // 1; the test run (autoplay) uses more to see captures in a minute
	unsigned char obj_n[TTM_NACT];                                 // b58: captures each actor took part in (inside the area when it fell)
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
	for (int a = 0; a < TTM_NACT; a++) m->gauge[a] = TTM_GAUGE0;
}
static void ttm_gauge_add(TtmMatch *m, int a, float d) {   // fill actor a's call-in gauge; full = TACHIKOMA READY
	if (a < 0 || a >= TTM_NACT) return; m->gauge[a] = fminf(1, m->gauge[a] + d);
	if (m->gauge[a] >= 1 && !m->ready[a]) { m->ready[a] = 1; m->ready_t[a] = 2 + (rand() % 30) / 10.f; }
}
static int ttm_map_to_win(const TtmMap *M) { return M->n >= 5 ? 5 : TTM_TO_WIN; }   // footage: first to 5 with five terminals, 4 on Underground
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
		for (int a = 0; a < n && a < TTM_NACT; a++) if (team_in[a] == k) ttm_gauge_add(m, a, TTM_GAUGE_RATE * dt * m->speed);
	} else { m->moving = 0; m->capturer = -1; }
	if (m->bar >= 1 || m->bar <= 0) {
		int w = m->bar >= 1 ? 0 : 1; m->bar = w == 0 ? 1.f : 0.f;
		m->score[w]++; m->owner[m->active] = w; m->last_team = w; m->last_actor = m->first_in[w]; ev |= TEV_CAPTURE;
		for (int a = 0; a < n && a < TTM_NACT; a++) if (team_in[a] == w && m->obj_n[a] < 255) m->obj_n[a]++;
		ttm_gauge_add(m, m->last_actor, TTM_GAUGE_CAP);
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
static GLuint ttm_hud_tex;
static float ttm_cp_r[TTM_MAXT];   // the "CAPTURE POINT" ring's radius per terminal (the area's equivalent circle, 7..15 m)
#define TTM_CP_H 330.0f            // its height over the device (footage: a dashed ring above head height)
static float ttm_match_t, ttm_dead_wave = -1, ttm_respawn_in;   // s since the match start; the local player's redeploy wave (match time) and the wait left   // tk_hud.gtx: the Think Tank panels' pictures (tools/tank/make_tk_hud.py), 0 = plain panels
// hooks the game sets
static void (*ttm_feed)(const char *killer, int kside, const char *victim, int vside, int icon);   // kill feed line
static void (*ttm_points)(int actor, const char *text, int pts);                                    // "+200" style notice for the local player
static const char *ttm_voice_stem = "motoko";
static float (*ttm_hold_dt)(float dt); static int (*ttm_board_up)(void);   // perf (b38): main.c's scoreboard (hud_match.h): display clocks held / screens hidden while it is up
// net (net_game.h, a client): ttm_ev_fn gives the host's TEV_* events (tm is the host's, from the snapshots) instead of the rules;
// ttm_call_fn sends the local player's call-in to the host. Both NULL offline and on the host.
static unsigned (*ttm_ev_fn)(void); static int (*ttm_call_fn)(V3 at, float yaw);
static void (*ttm_logf)(const char *fmt, ...);   // the game's log (boot.log): tank events, call-ins   // the local player's operator voice set (k_<op>_killtachikoma)
// the radio (the original's commander message: portrait, name, line; the game draws it with menu.h's menu_commander_draw while
// ttm_cm_t < 6): "Your Tachikoma is ready to call in." (Operator), "Terminal support inbound!", "Time is running out!" (Aramaki)
#define TTM_FACE_ARAMAKI 11
#define TTM_FACE_OPERATOR 12   // the ovl pack's cm.face.12 (the tutorial's operator)
#define TTM_FACE_TACHIKOMA 0   // the pack has no Tachikoma face yet (tools/menu/ovl_ingame.py's cm.face list): the ring stays empty
static int ttm_cm_face; static float ttm_cm_t = 99; static char ttm_cm_name[24], ttm_cm_msg[64];
static void ttm_cm(int face, const char *name, const char *msg) { ttm_cm_face = face; snprintf(ttm_cm_name, sizeof(ttm_cm_name), "%s", name); snprintf(ttm_cm_msg, sizeof(ttm_cm_msg), "%s", msg); ttm_cm_t = 0; }
static int ttm_cap_banner = -1;   // the capture banner on screen is for this team (-1: another banner): a call-in rewrites its line
static int ttm_ready_prev;

// ---- audio helpers: Aramaki over the radio (click, line, click), music cues (data/audio/<name>.gstr, flat on the Vita)
static void ttm_say(const char *prefix, float delay) {
	for (int i = 0; i < 4; i++) if (ttm_say_t[i] <= 0 && !ttm_say_q[i][0]) { ttm_say_t[i] = delay + 0.14f; snprintf(ttm_say_q[i], sizeof(ttm_say_q[i]), "%s", prefix);
		if (delay > 0) ev_play_later("ui/doughnut_on", NULL, delay); else ev_play("ui/doughnut_on", NULL); return; }
}
static void ttm_say_update(float dt) {
	for (int i = 0; i < 4; i++) { if (!ttm_say_q[i][0]) continue; ttm_say_t[i] -= dt;
		if (ttm_say_t[i] <= 0) { const VEntry *e = find_vline(ttm_say_q[i]); if (e) { voice_play_entry(e, (int)(0.43f * 65536), 0, 0); ev_play_later("ui/doughnut_off", NULL, vline_secs(e) + 0.1f); }
			ttm_say_q[i][0] = 0; } }
}
static void ttm_music(const char *name, int loop, int fade_ms) {
	if (ttm_music_cur == name) return; ttm_music_cur = name;
	char p[112]; snprintf(p, sizeof(p), "%.56s%.48s.gstr", snd_dir, name);
	music_play(p, loop, (int)(0.5f * 256), fade_ms);
}
static void ttm_show(const char *title, const char *sub, int col) { snprintf(ttm_banner[0], 96, "%s", title); snprintf(ttm_banner[1], 96, "%s", sub ? sub : ""); ttm_banner_col = col; ttm_banner_t = 0; ttm_cap_banner = -1; }
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
	tk[i].hp = tk[i].hpmax = a == 0 ? 4000.f : 3000.f;   // footage: the recorders' own Tachikomas 4000, most others 3000
	if (a == 0) tkp_apply(i);   // b74: My Think Tank - the parts the player chose (MY THINK TANK screen): their HP, gun, rockets, speed
	tm.ready[a] = 0; if (a < TTM_NACT) tm.gauge[a] = 0;
	if (ttm_cap_banner == team && ttm_banner_t < 4.0f) snprintf(ttm_banner[1], 96, "%s", team == tk_local_team ? "Deploying Tachikoma." : "Enemy Think Tank inbound!");
	if (a == 0) { ttm_flash_t = 0; tk_snd("fp_tachi_deploy", at, 0.8f, 10000); if (ttm_points) ttm_points(0, "TACHIKOMA DEPLOYED", 200); }
	if (team == tk_local_team) { ttm_say("d_tachi_spawn_", 0.5f); ttm_cm(TTM_FACE_TACHIKOMA, "TACHIKOMA", "Terminal support inbound!"); }
	else { const VEntry *e = find_vline("d_op_warn_tachi_"); if (e) voice_play_entry(e, (int)(0.3f * 65536), 0, 40); }
	if (ttm_logf) ttm_logf("  think tank call-in: %s by actor %d (team %d), %.0f HP\n", tk[i].name, a, team, tk[i].hpmax);
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
	if (ttm_logf) { static const char *const EN[] = { "", "deployed", "hack started", "hack done (defences down)", "hack failed", "destroyed", "kill", "assist", "converted", "defences back up" };
		if (ev != TKE_KILL && ev != TKE_ASSIST && ev > 0 && ev <= TKE_RESTORED) ttm_logf("  think tank %d (%s, team %d): %s by actor %d, HP %.0f\n", i, k->name, k->team, EN[ev], a, k->hp); }
	switch (ev) {
	case TKE_DESTROYED: {
		const char *kn = a >= 0 ? tk_actor_name(a) : "";
		if (ttm_feed) ttm_feed(a >= 0 ? kn : "", a == 0 ? 1 : (a >= 0 && tk_actor_team(a) == tk_local_team) ? 0 : -1, k->name, mine ? (k->owner == 0 ? 1 : 0) : -1, 2);
		if (a >= 0 && tk_actor_team(a) != k->team && a < TTM_NACT) ttm_gauge_add(&tm, a, 1);   // the destroyer's gauge fills (footage: 62 -> 100, 68 -> 100)
		if (a == 0) { if (ttm_points) ttm_points(0, "THINK TANK DESTROYED", 200); snd_killvoice(ttm_voice_stem, "killtachikoma"); }
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
				for (int i = 0; i < nbot; i++) { if (bots[i].team != team || bots[i].state == BS_DEAD || bots[i].netc) continue; V3 e = vsub(bots[i].pl.pos, tk[k].pl.pos); float d = vdot(e, e); if (d < bd) { bd = d; best = i; } } }
			if (best != ttm_hacker_bot[team] && best >= 0) bots[best].repath_t = 0;
			ttm_hacker_bot[team] = best; } }
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (b->state == BS_DEAD || b->netc) continue;   // (net: remote players hack and call in themselves)
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
	if (tkp_load(vdir, tkp_sel) < 0) logp("think tank parts: not loaded - the player's tank stays stock (b74)\n");   // b74: My Think Tank (at the map load: no hitch at the call-in)
	if (!ttm_term_ok) { snprintf(p, sizeof(p), "%stk_term.gvm", vdir); ttm_term_ok = model_load(&ttm_term, p) == 0 ? 1 : -1; }
	if (!ttm_hud_tex) { snprintf(p, sizeof(p), "%stk_hud.gtx", vdir); ttm_hud_tex = sk_tex_load(p); }
	tk_clear(); tk_goal_fn = ttm_tank_goal; tk_on_event = ttm_tank_event; tk_hud_style = 0; ttm_placing = 0;
	for (int i = 0; i < ttm_map.n; i++) { const TtmTerm *t = &ttm_map.t[i]; float area = 0;
		for (int q = 0; q < t->ntri; q++) { const float *A = t->tri + q * 9; area += 0.5f * fabsf((A[3] - A[0]) * (A[7] - A[1]) - (A[6] - A[0]) * (A[4] - A[1])); }
		ttm_cp_r[i] = fminf(1500, fmaxf(700, sqrtf(area / 3.14159f))); }
	if (to_win <= 0 || to_win > 9) to_win = ttm_map_to_win(&ttm_map);   // 0 (or a kill limit left over from another mode): the map's own
	ttm_start(&tm, &ttm_map, to_win, match_s); ttm_on = 1; ttm_start_t = 0; ttm_banner_t = ttm_flash_t = ttm_notice_t = ttm_ready_t = ttm_call_t = 99; ttm_music_cur = NULL;
	ttm_match_t = 0; ttm_dead_wave = -1; ttm_respawn_in = 0; ttm_cm_t = 99; ttm_cap_banner = -1; ttm_ready_prev = 0;
	ttm_hacker_bot[0] = ttm_hacker_bot[1] = -1; memset(ttm_say_q, 0, sizeof(ttm_say_q)); memset(ttm_say_t, 0, sizeof(ttm_say_t));
	ev_play("ui/game_start_ttm_text", NULL); ttm_music("bgm_in_game_start_ttm_01", 0, 300); ttm_say("d_aramaki_ttm_startgame_", 1.4f);
	return 0;
}
static void ttm_stop(void) { if (ttm_on) { tk_clear(); tk_goal_fn = NULL; tk_on_event = NULL; bot_no_respawn = 0; } ttm_on = 0; ttm_placing = 0; }
// the local player's respawn: the game's countdown (*pdead_t, death cam included) is held until his wave, the first one at
// least TTM_WAVE_MIN after the death; ttm_respawn_in = the wait left (the respawn screen's timer). Call every frame before the
// game counts *pdead_t down by dt.
static void ttm_respawn_hold(float *pdead_t, float dt) {
	if (!ttm_on || *pdead_t <= 0) { ttm_dead_wave = -1; ttm_respawn_in = 0; return; }
	if (ttm_dead_wave < 0) ttm_dead_wave = ceilf((ttm_match_t + TTM_WAVE_MIN) / TTM_WAVE) * TTM_WAVE;
	ttm_respawn_in = fmaxf(0, ttm_dead_wave - ttm_match_t);
	if (ttm_respawn_in > dt && tm.state != TS_END && *pdead_t < dt * 2 + 0.002f) *pdead_t = dt * 2 + 0.002f;   // stays down until the wave
}

// ---- per frame. eye/dir: the local player's view; use_held: Square; call_pr: the call-in button pressed this frame (starts /
// cancels the placement); deploy_pr: fire pressed this frame (deploys while placing); player_alive: 0 while dead.
// Returns the TEV_* bits (the game ends the match on TEV_END: tm.winner).
static unsigned ttm_frame(float dt, V3 eye, V3 dir, int use_held, int call_pr, int deploy_pr, int player_alive) {
	if (!ttm_on) return 0;
	{ int w0 = (int)(ttm_match_t / TTM_WAVE); ttm_match_t += dt; int wave = (int)(ttm_match_t / TTM_WAVE) != w0;
	  bot_no_respawn = tm.state != TS_END && !wave; }   // bots dead for 5 s or more come back with the wave (bots.h)
	float od = ttm_hold_dt ? ttm_hold_dt(dt) : dt;   // perf (b38): the banners, notices and radio lines wait under the held scoreboard (hud_match.h)
	ttm_now += dt; ttm_start_t += dt; ttm_banner_t += od; ttm_flash_t += dt; ttm_notice_t += od; ttm_ready_t += dt; ttm_call_t += dt; ttm_sd_t += dt; ttm_cm_t += od;
	ttm_say_update(dt);
	// who is in the active area
	int team_in[TTM_NACT], n = tk_w ? tk_w->n_actors() : 0; if (n > TTM_NACT) n = TTM_NACT;
	const TtmTerm *T = &ttm_map.t[tm.active];
	for (int a = 0; a < n; a++) { team_in[a] = -1; if (!tk_w->alive(a)) continue; if (ttm_in_area(T, tk_w->pos(a))) team_in[a] = tk_actor_team(a); }
	int was_state = tm.state, was_active = tm.active;
	unsigned ev = ttm_ev_fn ? ttm_ev_fn() : ttm_update(&tm, team_in, n, dt);
	int me = tk_local_team;
	if (ev & TEV_ONLINE) { ttm_say("d_aramaki_ttm_termactive_", 0); ev_play("ui/intel_alarm", NULL); ttm_music((rand() & 1) ? "bgm_ttm_01_terminal_01" : "bgm_ttm_01_terminal_02", 0, 600); }
	if (ev & TEV_MOVING) { ttm_music("bgm_ttm_01_hacking", 1, 800); }
	if (ev & TEV_CAPTURE) {
		int w = tm.last_team; const char *who = tm.last_actor >= 0 ? tk_actor_name(tm.last_actor) : "";
		ev_play(w == me ? "ui/intel_capture_fellow" : "ui/intel_capture_enemy", NULL);
		if (tm.state != TS_END) {
			if (w == me) { ttm_show("Terminal secured!", "Move to the next terminal!", 0); ttm_say(tm.last_actor == 0 ? "d_aramaki_ttm_teamsuccess_theperson_" : "d_aramaki_ttm_teamsuccess_bestoccupy_", 0.6f); }
			else { ttm_show("Terminal takeover failed!", "Regroup and secure the next terminal!", 0); ttm_say("d_aramaki_ttm_enemysuccess_", 0.6f); }
			ttm_cap_banner = w;   // a call-in in the next seconds: "Deploying Tachikoma." / "Enemy Think Tank inbound!" (footage)
			ttm_music("bgm_ttm_01_start", 1, 1500);
		}
		if (ttm_feed) { char b[64]; snprintf(b, sizeof(b), "captured terminal %c", ttm_map.t[was_active].letter); ttm_feed(who, tm.last_actor == 0 ? 1 : w == me ? 0 : -1, b, -2, -1); }
		if (tm.last_actor == 0 && ttm_points) ttm_points(0, "TERMINAL CAPTURED", 500);
		for (int k = 0; k < TK_MAX; k++) if (tk[k].state == TK_LIVE && tk[k].team == w) { tk_voice("tachi_teamsuccess_", tk[k].eye, 0.7f); break; }
	}
	if (ev & TEV_FINAL) { int fin = tm.final_team;
		ttm_show("FINAL TERMINAL!", fin == me ? "Letting the enemy capture the final terminal will mean defeat regardless of score." : "Capturing the final terminal will secure victory regardless of score.", 0);
		ttm_music("bgm_ttm_01_final_score", 1, 1000); }
	if (ev & TEV_MULT) ev_play("ui/ghost_scan_triangle", NULL);
	if (ev & TEV_OVERTIME) { ttm_show("OVERTIME", "The next capture decides the match.", 0); ev_play("ui/round_overtime", NULL); }
	if (ev & TEV_LAST60) ev_play("ui/crisis", NULL);
	if (ev & TEV_LAST20) { ev_play("ui/ghost_time_warning", NULL); radio_play("voice/donut/tdm/20sec"); ttm_show("Timer Warning", "Time is running out!", 0); ttm_cm(TTM_FACE_ARAMAKI, "CHIEF ARAMAKI", "Time is running out!"); }
	if (ev & TEV_END) { int w = tm.winner; music_stop(300); ttm_music_cur = NULL;
		ttm_say(w == 2 ? "d_aramaki_ttm_draw_" : w == me ? "d_aramaki_ttm_win_" : "d_aramaki_ttm_lose_", 1.0f); }
	(void)was_state;
	// the local player: TACHIKOMA READY (the gauge is full) -> call it in where he looks
	if (tm.ready[0] && !ttm_ready_prev && tm.state != TS_END) { ttm_ready_t = 0; ev_play("ui/ready_on", NULL);
		const VEntry *e = find_vline("d_op_tachi_summon_"); if (e) voice_play_entry(e, (int)(0.3f * 65536), 0, 40);
		ttm_cm(TTM_FACE_OPERATOR, "OPERATOR", "Your Tachikoma is ready to call in."); if (ttm_logf) ttm_logf("  tachikoma ready (local player)\n"); }
	ttm_ready_prev = tm.ready[0];
	if (tm.ready[0] && tm.state != TS_END && player_alive) {
		if (call_pr) { if (tk_count(me) > 0) { ttm_note("THINK TANK ALREADY DEPLOYED"); ttm_placing = 0; } else { ttm_placing = !ttm_placing; ttm_place_t = 0; ev_play(ttm_placing ? "ui/tab_on" : "ui/tab_off", NULL); } }
		if (ttm_placing) { ttm_place_t += dt; ttm_place_ok = ttm_call_point(eye, dir, &ttm_place_pt); ttm_place_yaw = atan2f(dir.y, dir.x);
			if ((deploy_pr || (ttm_auto_deploy && ttm_place_t > 1.5f)) && ttm_place_ok && (ttm_call_fn ? ttm_call_fn(ttm_place_pt, ttm_place_yaw) : ttm_call_tank(0, ttm_place_pt, ttm_place_yaw)) >= 0) { ttm_call_pt = ttm_place_pt; ttm_call_t = 0; ttm_placing = 0; } }
	} else ttm_placing = 0;
	if (!ttm_ev_fn) ttm_bots(dt);   // (a client: the host runs the bots)
	// the local player's hack (Square next to an enemy tank; a client: the host starts it, here only the prompt)
	tk_use_prog = player_alive ? tk_use(0, eye, dir, ttm_ev_fn ? 0 : use_held, dt) : -1;
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
	// "CAPTURE POINT": a thin dashed cyan ring around the active terminal above head height while it is online (footage)
	if (tm.state != TS_REBOOT) { const TtmTerm *t = &ttm_map.t[tm.active]; V3 d = vsub(v3(t->dev[0], t->dev[1], t->dev[2]), lis_pos);
		if (vdot(d, d) < 7000.f * 7000.f) { float R = ttm_cp_r[tm.active], z = t->dev[2] + TTM_CP_H; const float col[4] = { 0.5f, 0.88f, 1, 0.75f };
			for (int k = 0; k < 48; k++) { float a0 = k * 6.2831853f / 48, a1 = a0 + 6.2831853f / 48 * 0.55f;
				tk_line3(v3(t->dev[0] + R * cosf(a0), t->dev[1] + R * sinf(a0), z), v3(t->dev[0] + R * cosf(a1), t->dev[1] + R * sinf(a1), z), 1.4f, col); }
			sprite_flush(VP, tk_white, 1); } }
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
static int ttm_panel_frame[TK_MAX], ttm_live_frame[TK_MAX];   // b102: bot_frame + 1 of the panel's last draw / of its last live picture (main.c ttm_portraits)
#define TTM_PW 160   // ui b42 (HUD de-clutter): was 186 - the left panel covered the minimap's right edge (the picture cropped, not squeezed)
#define TTM_PH 84
static void ttm_panel(int i, float x0, float y, int right) {
	Tank *k = &tk[i]; float c[4]; ttm_team_col(k->team, c); c[3] = 0.95f;
	const float wm[4] = { 1, 1, 1, 0.97f }, rd[4] = { 1, 0.27f, 0.24f, 0.97f }, dim[4] = { c[0] * 0.3f, c[1] * 0.3f, c[2] * 0.3f, 0.75f }, sub[4] = { 0.8f, 0.88f, 0.93f, 0.95f };
	const float w = TTM_PW, h = TTM_PH;
	ttm_panel_frame[i] = bot_frame + 1;
	if (ttm_live_frame[i] == bot_frame + 1) ttm_rect(x0, y, x0 + w, y + h, (const float[4]){ c[0], c[1], c[2], 0.16f });   // b102: the live picture is under the HUD already - the team's wash over it
	else if (ttm_hud_tex) {   // the picture fills the panel (atlas: blue model at v 0..112/256, red at 128..240)
		float v0 = k->kind ? 128 / 256.f : 0, v1 = v0 + 112 / 256.f, cw = 248.f * (TTM_PW / 186.f), u0 = (248 - cw) / 2 / 256.f, u1 = u0 + cw / 256.f;
		float q[4][2] = { { x0, y }, { x0 + w, y }, { x0 + w, y + h }, { x0, y + h } }, uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
		hud_poly(ttm_hud_tex, HG.screen, (const float (*)[2])q, (const float (*)[2])uv, 4, (const float[4]){ 0.9f, 0.9f, 0.9f, 0.92f });
	} else ttm_rect(x0, y, x0 + w, y + h, (const float[4]){ c[0] * 0.15f, c[1] * 0.15f, c[2] * 0.15f, 0.7f });
	ttm_rect(x0, y, x0 + w, y + 50, (const float[4]){ 0, 0, 0, 0.3f });                 // under the names
	ttm_rect(x0, y + 54, x0 + w, y + h, (const float[4]){ 0, 0, 0, 0.35f });             // under HP
	ttm_rect(x0, y, x0 + w, y + 2, c); ttm_rect(right ? x0 + w - 3 : x0, y, right ? x0 + w : x0 + 3, y + h, c);
	float nx = right ? x0 + w - 9 : x0 + 9; int al = right ? 1 : 0; char b[40];
	ttm_clip(b, sizeof(b), k->owner_name, 20, w - 18 - (!k->def ? 30 : 0)); hud_text(HG.screen, nx, y + 21, 20, al, wm, HUD_Z4, b);
	if (k->hacker >= 0) { ttm_rect(x0 + 3, y + 28, x0 + w - 3, y + 50, (const float[4]){ 0.62f, 0.05f, 0.07f, 0.88f }); hud_text(HG.screen, x0 + w / 2, y + 46, 20, 2, wm, HUD_Z4, "Hacker Detected"); }
	else { ttm_clip(b, sizeof(b), k->name, 20, w - 18); hud_text(HG.screen, nx, y + 45, 20, al, sub, HUD_Z4, b); }
	// HP: "HP" tag on the inner side, the number on the outer side, ten segments under it
	snprintf(b, sizeof(b), "%d", (int)(k->hp + 0.999f)); hud_text(HG.screen, right ? x0 + w - 9 : x0 + 9, y + 77, 28, al, k->def ? wm : rd, HUD_Z4, b);
	{ float tx = right ? x0 + 8 : x0 + w - 36; ttm_rect(tx, y + 58, tx + 28, y + 77, (const float[4]){ 0.62f, 0.12f, 0.12f, 0.9f }); hud_text(HG.screen, tx + 14, y + 75, 20, 2, wm, HUD_Z4, "HP"); }
	for (int s = 0; s < 10; s++) { float f = k->hp / k->hpmax * 10 - s, sx = right ? x0 + w - 12 - (s + 1) * 9.5f : x0 + 9 + s * 9.5f; const float *cc = f > 0.5f ? (k->def ? c : rd) : dim;
		float q[4][2] = { { sx + 2, y + 79 }, { sx + 8, y + 79 }, { sx + 6, y + h - 1 }, { sx, y + h - 1 } }; ttm_quad((const float (*)[2])q, cc); }
	if (!k->def) { float cx = right ? x0 + 22 : x0 + w - 22, cy = y + 15; ttm_ring(cx, cy, 9, 12, rd); float q[4][2] = { { cx - 8, cy - 9 }, { cx - 6, cy - 10.5f }, { cx + 8, cy + 9 }, { cx + 6, cy + 10.5f } }; ttm_quad((const float (*)[2])q, rd); }   // ui b42: the shutdown sign on the owner's line (the narrower panel left the tank's name no room beside it)
}
// a world point on screen; off screen (or behind the camera) it sticks to the edge in its direction, clear of the radar (top left)
static int ttm_screen_edge(const float *M, const float *p, float *sx, float *sy) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r];
	float x, y; if (q[3] > 10) { x = q[0] / q[3]; y = q[1] / q[3]; } else { float w = fmaxf(fabsf(q[0]), fabsf(q[1])) + 1e-3f; x = -q[0] / w * 2; y = -q[1] / w * 2; if (fabsf(x) < 1 && fabsf(y) < 1) { y = -2; } }
	float X = (x + 1) * 480, Y = (1 - y) * 272;
	int edge = X < 60 || X > 900 || Y < 150 || Y > 470;
	if (edge) { float cx = X - 480, cy = Y - 310, s = fminf(cx != 0 ? 420 / fabsf(cx) : 1e9f, cy != 0 ? 160 / fabsf(cy) : 1e9f); if (s < 1) { X = 480 + cx * s; Y = 310 + cy * s; } }
	*sx = fminf(900, fmaxf(60, X)); *sy = fminf(470, fmaxf(150, Y)); return edge ? 2 : 1;   // ui b42: 2 = held at the edge
}
static void ttm_hud(const float *MVP, const float *cam, float now) {
	if (!ttm_on || HG.white < 0 || HG.screen < 0) return;
	(void)now; int me = tk_local_team;
	const float wm[4] = { 1, 1, 1, 0.97f }, bl[4] = { 0.33f, 0.78f, 0.95f, 0.97f }, rd[4] = { 0.95f, 0.27f, 0.3f, 0.97f }, dk[4] = { 0.02f, 0.05f, 0.07f, 0.62f }, gr[4] = { 0.62f, 0.64f, 0.66f, 0.92f };
	const float *cm = me == 0 ? bl : rd, *ce = me == 0 ? rd : bl;   // our colour on the left, theirs on the right
	float my = me == 0 ? tm.bar : 1 - tm.bar;                        // our share of the bar
	HUD_TAG("ttm.bar");   // ui b42: element names for the host overlap checker (no-op on the Vita)
	const float BX0 = 396, BX1 = 564, BY0 = 88, BY1 = 108;   // ui b42: 4 px lower (it touched the clock box)           // under the score / clock (hud_game.h's legible clock box ends at y 80), between the panels (footage: 405..551 x 56..68), the counts at its ends
	if (tm.state == TS_REBOOT) {   // "Capture system rebooting..." (the grey part shrinks as the reboot ends)
		ttm_rect(BX0 - 40, BY0, BX1 + 40, BY1, dk); float f = 1 - tm.t / TTM_REBOOT; ttm_rect(BX0 - 40, BY0, BX0 - 40 + (BX1 - BX0 + 80) * f, BY1, (const float[4]){ 0.5f, 0.52f, 0.55f, 0.8f });   // ui b42: +-40 (was 56: under the panels)
		ttm_rect(300, BY1 + 4, 660, BY1 + 36, dk); hud_text(HG.screen, 480, BY1 + 28, 24, 2, wm, HUD_Z4, "Capture system rebooting...");
	} else if (tm.state == TS_LIVE) {
		ttm_rect(BX0, BY0, BX1, BY1, dk);
		float mid = BX0 + (BX1 - BX0) * my; ttm_rect(BX0, BY0, mid, BY1, cm); ttm_rect(mid, BY0, BX1, BY1, ce);
		if (tm.moving) { int up = (tm.moving_team == me); float q[4][2] = { { mid + (up ? -6 : 6), BY0 + 2 }, { mid + (up ? 4 : -4), (BY0 + BY1) / 2 }, { mid + (up ? -6 : 6), BY1 - 2 }, { mid + (up ? -3 : 3), (BY0 + BY1) / 2 } }; ttm_quad((const float (*)[2])q, wm); }
		if (tm.mult > 1) { char m[16]; snprintf(m, sizeof(m), "X %d", tm.mult);   // footage: a white "X 2" over the middle of the bar
			hud_text(HG.screen, 481.5f, BY1 + 1.5f, 24, 2, (const float[4]){ 0, 0, 0, 0.75f }, HUD_Z4, m); hud_text(HG.screen, 480, BY1, 24, 2, wm, HUD_Z4, m); }
		// the ends (later footage): a ring in each side's colour with a white arrow pointing into the bar, ours left
		for (int e = 0; e < 2; e++) { float cx = e ? BX1 + 15 : BX0 - 15, cy = (BY0 + BY1) / 2, sg = e ? -1 : 1; const float *c = e ? ce : cm;
			ttm_ring(cx, cy, 0, 11, (const float[4]){ 0.02f, 0.05f, 0.07f, 0.8f }); ttm_ring(cx, cy, 9, 12, c);
			float q[4][2] = { { cx - 4 * sg, cy - 5.5f }, { cx + 5.5f * sg, cy }, { cx - 4 * sg, cy + 5.5f }, { cx - 4 * sg, cy } }; ttm_quad((const float (*)[2])q, wm); }
		if (tm.capturer >= 0) { char b[48], nm[32]; ttm_clip(nm, sizeof(nm), tk_actor_name(tm.capturer), 24, tm.overtime ? 88 : 180); snprintf(b, sizeof(b), "%s capturing", nm); hud_text(HG.screen, 480, BY1 + 27, 24, 2, tm.cap_team == me ? bl : rd, HUD_Z4, b); }
		if (tm.overtime) { char p[8]; snprintf(p, sizeof(p), "%d%%", (int)(my * 100 + 0.5f)); hud_text(HG.screen, BX0 - 46, BY1 + 27, 24, 2, cm, HUD_Z4, p); snprintf(p, sizeof(p), "%d%%", (int)((1 - my) * 100 + 0.5f)); hud_text(HG.screen, BX1 + 46, BY1 + 27, 24, 2, ce, HUD_Z4, p);   // ui b42: 30 px nearer the bar (clear of the kill feed; the capturer's name cut at 88 px)
			if (fabsf(my - 0.5f) > 0.004f && !hud_blocked(420, BY1 + 36, 540, BY1 + 58, HUD_P_FEED)) hud_text(HG.screen, 480, BY1 + 53, 22, 2, my > 0.5f ? cm : ce, HUD_Z4, my > 0.5f ? "Winning" : "Losing"); }   // ui b42: not under a banner (hud.h claims)
	}
	// terminal letters under the minimap (footage): rebooting - all plain; online - the active one is a yellow box with "!",
	// a box in the pushing side's colour (with its letter) while one side moves the bar; the others in their holder's colour
	{ int ot = tm.state == TS_LIVE && tm.overtime; float cy1 = tm.state == TS_REBOOT ? BY1 + 36 : ot && fabsf(my - 0.5f) > 0.004f && !hud_blocked(420, BY1 + 36, 540, BY1 + 58, HUD_P_FEED) ? BY1 + 58 : tm.state == TS_LIVE && (tm.capturer >= 0 || ot) ? BY1 + 32 : BY1 + 2;
	  if (tm.state == TS_REBOOT || tm.state == TS_LIVE) hud_claim(ot ? BX0 - 72 : tm.state == TS_REBOOT ? 300 : BX0 - 28, BY0 - 2, ot ? BX1 + 72 : tm.state == TS_REBOOT ? 660 : BX1 + 28, cy1, HUD_P_BANNER); }   // ui b42: notices keep clear of the bar (hud.h claims)
	HUD_TAG("ttm.letters");
	const float ye[4] = { 0.95f, 0.82f, 0.2f, 0.95f }, ink[4] = { 0.05f, 0.05f, 0.05f, 1 };
	int live = tm.state == TS_LIVE, idle = live && !tm.moving, strip_hidden = 0;
	// b61 (overlap check: the commander box's claims hid 4 of the 5 letters, the 5th stood beside its portrait ring): the
	// letters are one strip - when any of them is under a claim, all wait (back when it is gone)
	for (int o = 0; o < ttm_map.n; o++) { float x = 40 + o * 32, y = 198; if (hud_blocked(x - 14, y - 24, x + 14, y + 6, HUD_P_FEED)) strip_hidden = 1; }
	for (int i = 0; i < ttm_map.n && !strip_hidden; i++) {
		int ord = 0; for (int j = 0; j < ttm_map.n; j++) if (ttm_map.t[j].letter < ttm_map.t[i].letter) ord++;
		float x = 40 + ord * 32, y = 198; char l[2] = { ttm_map.t[i].letter, 0 };   // (under hud_game.h's radar, which ends at y ~166; ui b42: 32 px apart, five within its width)
		if (i == tm.active && live) { ttm_rect(x - 14, y - 24, x + 14, y + 6, idle ? ye : tm.moving_team == me ? cm : ce); hud_text(HG.screen, x, y + 1, 24, 2, ink, HUD_Z4, idle ? "!" : l); }
		else { ttm_rect(x - 14, y - 24, x + 14, y + 6, dk); hud_text(HG.screen, x, y + 1, 24, 2, tm.owner[i] < 0 || i == tm.active ? wm : tm.owner[i] == me ? cm : ce, HUD_Z4, l); }
	}
	// world marker of the active terminal (footage): rebooting - its letter in a white ring; online - a yellow ring with "!"
	// (the pushing side's colour with the letter while the bar moves); the distance under it; "FINAL" above a team's last one
	HUD_TAG("~ttm.marker");
	{ const TtmTerm *t = &ttm_map.t[tm.active]; float p[3] = { t->dev[0], t->dev[1], t->dev[2] + 200 }, sx, sy;
		int at_edge = ttm_screen_edge(MVP, p, &sx, &sy) == 2;
		if (at_edge) {   // ui b42 (HUD de-clutter): held at the left edge it stays above the skill gauge and right of the commander box (hud.h claims)
			if (sx < 230 && sy > 318) sy = 318;
			for (int g = 0; g < 60 && hud_blocked(sx - 24, sy - 44, sx + 24, sy + 48, HUD_P_FEED); g++) sx += 8; }
		if (!at_edge && hud_blocked(sx - 24, sy - 44, sx + 24, sy + 48, HUD_P_NOTICE)) { }   // b61: not under a banner / notice (back when it is gone)
		else {
			const float *c = !live ? wm : idle ? ye : tm.moving_team == me ? cm : ce;
			ttm_ring(sx, sy, 0, 19, (const float[4]){ 0, 0, 0, 0.4f }); ttm_ring(sx, sy, 18, 21.5f, c);
			char l[2] = { t->letter, 0 }; hud_text(HG.screen, sx, sy + 9, 26, 2, c, HUD_Z4, idle ? "!" : l);
			float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2]; char dm[16]; snprintf(dm, sizeof(dm), "%dm", (int)(sqrtf(dx * dx + dy * dy + dz * dz) / 100)); hud_text(HG.screen, sx, sy + 43, 20, 2, idle ? ye : wm, HUD_Z4, dm);
			if (ttm_final_of(&tm) >= 0) hud_text(HG.screen, sx, sy - 27, 20, 2, idle ? ye : wm, HUD_Z4, "FINAL"); } }
	// Think Tank panels: ours left of the score, theirs right of it
	{ int mine = -1, theirs = -1; for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i)) { if (tk[i].team == me) { if (mine < 0) mine = i; } else if (theirs < 0) theirs = i; }
	  HUD_TAG("ttm.panelL"); if (mine >= 0) ttm_panel(mine, 190, 2, 0); HUD_TAG("ttm.panelR"); if (theirs >= 0) ttm_panel(theirs, 770 - TTM_PW, 2, 1); }   // ui b42: right of the minimap (was 150 / 810); the score plate narrows between them (main.c score_narrow)
	// "CAPTURE POINT" tags on the ring (footage: cyan text in a bracket, in the world), the ones in front within 40 m
	HUD_TAG("~ttm.cptag");
	if (tm.state != TS_REBOOT) { const TtmTerm *t = &ttm_map.t[tm.active]; float R = ttm_cp_r[tm.active], z = t->dev[2] + TTM_CP_H + 25;
		for (int k = 0; k < 4; k++) { float a = 0.785398f + k * 1.5707963f, p[3] = { t->dev[0] + R * cosf(a), t->dev[1] + R * sinf(a), z }, q[4];
			for (int r = 0; r < 4; r++) q[r] = MVP[r] * p[0] + MVP[4 + r] * p[1] + MVP[8 + r] * p[2] + MVP[12 + r];
			float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2], dd = sqrtf(dx * dx + dy * dy + dz * dz); if (q[3] < 50 || dd > 4000 || dd < 300) continue;
			float sx = (q[0] / q[3] + 1) * 480, sy = (1 - q[1] / q[3]) * 272; if (sx < 90 || sx > 870 || sy < 130 || sy > 460) continue;
			float al = fminf(1, (4000 - dd) / 1200) * 0.9f, c[4] = { 0.5f, 0.88f, 1, al }, w = hud_text_w("CAPTURE POINT") * 20 + 16;
			if (hud_blocked(sx - w / 2, sy - 18, sx + w / 2, sy + 6, HUD_P_FEED)) continue;   // b61 (device b61-1: under the FINAL TERMINAL! band's text): not under a claimed box
			ttm_rect(sx - w / 2, sy - 18, sx + w / 2, sy + 6, (const float[4]){ 0.02f, 0.08f, 0.1f, 0.35f * al });
			ttm_rect(sx - w / 2, sy - 18, sx - w / 2 + 2, sy + 6, c); ttm_rect(sx + w / 2 - 2, sy - 18, sx + w / 2, sy + 6, c);
			ttm_rect(sx - w / 2, sy - 18, sx - w / 2 + 8, sy - 16.5f, c); ttm_rect(sx + w / 2 - 8, sy + 4.5f, sx + w / 2, sy + 6, c);
			hud_text(HG.screen, sx, sy, 20, 2, c, HUD_Z4, "CAPTURE POINT"); } }
	// the Tachikoma call-in gauge (footage: "[E] <hexagon> NN" left of the HP ring; cyan at 100 = TACHIKOMA READY): SELECT + hexagon + %
	HUD_TAG("ttm.gauge");
	if (tm.state != TS_END) { float g = tm.gauge[0], cx = 708, cy = 502, r = 24; int full = tm.ready[0];
		float sw = hud_text_w("SELECT") * 20 + 18; tk_btn_plate(cx - r - 8 - sw, cy, "SELECT", 20);
		const float fill[4] = { full ? 0.2f : 0.36f, full ? 0.78f : 0.4f, full ? 0.9f : 0.43f, full ? 0.92f : 0.6f }, rim[4] = { 0.85f, 0.9f, 0.92f, 0.95f };
		for (int e = 0; e < 6; e++) { float a0 = e * 1.0471976f, a1 = a0 + 1.0471976f, v0[2] = { cx + r * cosf(a0), cy + r * sinf(a0) }, v1[2] = { cx + r * cosf(a1), cy + r * sinf(a1) };
			float q[4][2] = { { cx, cy }, { v0[0], v0[1] }, { v1[0], v1[1] }, { cx, cy } }; ttm_quad((const float (*)[2])q, fill);
			float n0[2] = { cx + (r - 2) * cosf(a0), cy + (r - 2) * sinf(a0) }, n1[2] = { cx + (r - 2) * cosf(a1), cy + (r - 2) * sinf(a1) };
			float qr[4][2] = { { v0[0], v0[1] }, { v1[0], v1[1] }, { n1[0], n1[1] }, { n0[0], n0[1] } }; ttm_quad((const float (*)[2])qr, rim); }
		char b[8]; snprintf(b, sizeof(b), "%d", (int)(g * 100 + 0.001f)); hud_text(HG.screen, cx, cy + 8, 22, 2, wm, HUD_Z4, b); }
	// the call-in placement: "(R) to deploy" (footage: "Click to deploy" under the crosshair), "(SELECT) cancel"
	HUD_TAG("ttm.place");
	if (ttm_placing) { const float bk[4] = { 0.02f, 0.03f, 0.05f, 0.62f };
		ttm_rect(330, 386, 630, 456, bk); ttm_rect(330, 386, 630, 388, (const float[4]){ 0.3f, 0.85f, 1, 0.8f });
		if (ttm_place_ok) { float w1 = hud_text_w("R") * 22 + 22 * 0.9f, w2 = hud_text_w(" to deploy") * 24, x = 480 - (w1 + w2) / 2;
			tk_btn_plate(x, 408, "R", 22); hud_text(HG.screen, x + w1, 416, 24, 0, wm, HUD_Z4, " to deploy"); }
		else hud_text(HG.screen, 480, 416, 24, 2, rd, HUD_Z4, "No room to deploy here");
		{ float w1 = hud_text_w("SELECT") * 20 + 18, w2 = hud_text_w(" cancel") * 20, x = 480 - (w1 + w2) / 2;
		  tk_btn_plate(x, 437, "SELECT", 20); hud_text(HG.screen, x + w1, 444, 20, 0, gr, HUD_Z4, " cancel"); } }
}
// banners / flashes over the HUD (drawn after the HUD, before the menus' overlays)
static void ttm_overlay(void) {
	if (!ttm_on || HG.white < 0 || HG.screen < 0 || (ttm_board_up && ttm_board_up())) return;   // perf (b38): not over the held scoreboard
	hud_begin();
	HUD_TAG("~ttm.flash");
	if (ttm_flash_t < 0.6f) { float a = 0.6f * (1 - ttm_flash_t / 0.6f);   // the call-in flash (screen_tactical_resource; footage: a cyan glow at the screen edges):
		for (int e = 0; e < 8; e++) { float w = 9, i0 = e * w, i1 = i0 + w, c[4] = { 0.25f, 0.85f, 0.95f, a * (1 - e / 8.f) };   // edge bands fading inwards, a third of a full-screen fill
			ttm_rect(i0, i0, 960 - i0, i1, c); ttm_rect(i0, 544 - i1, 960 - i0, 544 - i0, c); ttm_rect(i0, i1, i1, 544 - i1, c); ttm_rect(960 - i1, i1, 960 - i0, 544 - i1, c); } }
	const float ye[4] = { 1, 0.86f, 0.25f, 1 };
	const char *t0 = NULL, *t1 = NULL; float bt = 99;
	if (ttm_banner_t < 4.5f) { t0 = ttm_banner[0]; t1 = ttm_banner[1]; bt = ttm_banner_t; }
	else if (tm.ready[0] && !ttm_placing && tm.state != TS_END && ttm_ready_t >= 0 && fmodf(ttm_ready_t, 12) < 4.5f) { t0 = "TACHIKOMA READY"; t1 = "\x01"; bt = fmodf(ttm_ready_t, 12); }   // "PRESS [E] KEY" -> PRESS (SELECT)
	HUD_TAG("ttm.banner");
	// ui b42 (HUD de-clutter): the band within the centre column (222..738, was the full width: over the minimap, the terminal letters,
	// the kill feed, and the commander's name plate beside it), 164..250 at most (clear of the crosshair; the overtime "Winning" line
	// and a kill feed row it would cover wait under it: hud.h claims), the title narrowed to fit (24 px at least), the subtitle
	// wrapped at 500 px (20 px on two lines)
	if (t0) { float a = bt < 0.2f ? bt / 0.2f : bt > 4 ? (4.5f - bt) / 0.5f : 1, m[4] = { ye[0], ye[1], ye[2], a }, w2[4] = { 1, 1, 1, a };
		const float Y0 = 164, X0 = 222, X1 = 738; int two = t1 && *t1 && t1[0] != 1 && hud_text_w(t1) * 22 > 500; float y1 = Y0 + (two ? 86 : 70);
		float bk[4] = { 0.02f, 0.04f, 0.06f, 0.6f * a }; for (int e = 0; e < 4; e++) { float f = (e + 1) / 5.f, c[4] = { bk[0] * f, bk[1] * f, bk[2] * f, bk[3] * f };   // soft ends
			ttm_rect(X0 + e * 9, Y0, X0 + 9 + e * 9, y1, c); ttm_rect(X1 - 9 - e * 9, Y0, X1 - e * 9, y1, c); }
		ttm_rect(X0 + 36, Y0, X1 - 36, y1, bk); ttm_rect(X0 + 18, Y0 + 1, X1 - 18, Y0 + 2.5f, (const float[4]){ 1, 1, 1, 0.3f * a }); ttm_rect(X0 + 18, y1 - 2.5f, X1 - 18, y1 - 1, (const float[4]){ 1, 1, 1, 0.3f * a });
		float tsz = fmaxf(24, fminf(34, 34 * 500 / fmaxf(1, hud_text_w(t0) * 34))); hud_text(HG.screen, 480, Y0 + 34, tsz, 2, m, HUD_Z4, t0);
		if (t1 && t1[0] == 1) { float w1 = hud_text_w("PRESS ") * 22, wg = hud_text_w("SELECT") * 20 + 18, x = 480 - (w1 + wg) / 2;
			hud_text(HG.screen, x, Y0 + 60, 22, 0, w2, HUD_Z4, "PRESS "); tk_btn_plate(x + w1, Y0 + 52, "SELECT", 20); }
		else if (two) ttm_text_wrap(480, Y0 + 58, 20, 22, 500, w2, t1);
		else if (t1 && *t1) hud_text(HG.screen, 480, Y0 + 60, 22, 2, w2, HUD_Z4, t1);
		if (a > 0.02f) hud_claim(X0, Y0, X1, y1, HUD_P_BANNER); }
	HUD_TAG("ttm.notice");
	if (ttm_notice_t < 2.5f) { float a = ttm_notice_t > 2 ? (2.5f - ttm_notice_t) * 2 : 1, w = fminf(900, hud_text_w(ttm_notice) * 24) / 2 + 4;
		float d = hud_place(480 - w, 282, 480 + w, 307, HUD_P_NOTICE, 88, 360);   // ui b42: clear of the band (hud.h claims)
		if (d < HUD_HIDE) { ttm_text_wrap(480, 300 + d, 24, 28, 900, (const float[4]){ 1, 0.88f, 0.3f, a }, ttm_notice); if (a > 0.02f) hud_claim(480 - w, 282 + d, 480 + w, 307 + d, HUD_P_NOTICE); } }
	hud_end();
}
#endif
