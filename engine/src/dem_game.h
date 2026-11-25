// Demolition on screen and in the world (dem.h rules; the round flow stays in main.c): the original's HUD parts for the plant /
// defuse bar and the bomb-site markers, the round screens (banner, START, result band with the sides, switching sides), the
// result reasons from the player's side, and the bomb-site target meshes added to the player collision where the map lacks
// them (make_gdem.py add_col: downtown B, headquarter B, zioplant A + B). Included by main.c after the Demolition statics.
// main.c hooks: dem_add_site_col (match setup), dem_game_frame (after dem_update), dem_hud_draw (HUD), dem_overlays_draw (in-match
// screens). Spectating after death: elim_game.h (shared with Elimination).
#pragma once
#include "hud_round.h"
// the result line from the player's point of view (footage: "Mission Successful: Bomb Defused", "Mission Failed: ...")
static const char *dem_reason_text(int reason, int won) {
	static const char *W[6] = { "", "Mission Successful: Team Eliminated", "Mission Successful: Team Eliminated", "Mission Successful: Detonation", "Mission Successful: Bomb Defused", "Mission Successful: Time Over" };
	static const char *L[6] = { "", "Mission Failed: Team Eliminated", "Mission Failed: Team Eliminated", "Mission Failed: Detonation", "Mission Failed: Bomb Defused", "Mission Failed: Time Over" };
	reason = reason < 0 || reason > 5 ? 0 : reason; return won ? W[reason] : L[reason];
}
// target meshes of the sites the map collision does not have: appended to the collision triangles (player + bullets) and the
// cell lists rebuilt (same grid: the sites lie inside the map's bounds)
static void dem_add_site_col(const DemMap *M) {
	int add = 0; for (int i = 0; i < M->nsite; i++) if (M->site[i].add_col) add += M->site[i].nt;
	if (!add || !ctris) return;
	Tri *nt = realloc(ctris, sizeof(Tri) * (nctri + add + 1)); if (!nt) return; ctris = nt;
	int n0 = nctri;
	for (int i = 0; i < M->nsite; i++) { const DemSite *s = &M->site[i]; if (!s->add_col) continue;
		for (int k = 0; k < s->nt; k++) { const float *A = s->v + 3 * s->t[3 * k], *B = s->v + 3 * s->t[3 * k + 1], *C = s->v + 3 * s->t[3 * k + 2];
			Tri *t = &ctris[nctri]; t->a = v3(A[0], A[1], A[2]); t->b = v3(B[0], B[1], B[2]); t->c = v3(C[0], C[1], C[2]);
			V3 n = vcross(vsub(t->b, t->a), vsub(t->c, t->a)); float l = sqrtf(vdot(n, n)); if (l < 1e-4f) continue;
			t->n = vmul(n, 1.0f / l); t->zmin = fminf(A[2], fminf(B[2], C[2])); t->zmax = fmaxf(A[2], fmaxf(B[2], C[2])); t->mat = 0; t->flags = TF_PLAYER | TF_BULLET; nctri++; } }
	// rebuild the per-cell lists (both grids) for all triangles
	int *cnt = calloc(gw * gh, sizeof(int)), *pcnt = calloc(gw * gh, sizeof(int)); if (!cnt || !pcnt) { free(cnt); free(pcnt); nctri = n0; return; }
	free(cstart); free(clist); free(pstart); free(plist);
	for (int pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			cstart = malloc(sizeof(int) * (gw * gh + 1)); pstart = malloc(sizeof(int) * (gw * gh + 1)); cstart[0] = pstart[0] = 0;
			for (int i = 0; i < gw * gh; i++) { cstart[i + 1] = cstart[i] + cnt[i]; pstart[i + 1] = pstart[i] + pcnt[i]; }
			clist = malloc(sizeof(int) * (cstart[gw * gh] + 1)); plist = malloc(sizeof(int) * (pstart[gw * gh] + 1));
			memset(cnt, 0, sizeof(int) * gw * gh); memset(pcnt, 0, sizeof(int) * gw * gh);
		}
		for (int i = 0; i < nctri; i++) {
			Tri *t = &ctris[i];
			int x0 = (int)((fminf(t->a.x, fminf(t->b.x, t->c.x)) - gx0) / CELL), x1 = (int)((fmaxf(t->a.x, fmaxf(t->b.x, t->c.x)) - gx0) / CELL);
			int y0 = (int)((fminf(t->a.y, fminf(t->b.y, t->c.y)) - gy0) / CELL), y1 = (int)((fmaxf(t->a.y, fmaxf(t->b.y, t->c.y)) - gy0) / CELL);
			if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= gw) x1 = gw - 1; if (y1 >= gh) y1 = gh - 1;
			for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { int c = y * gw + x;
				if (t->flags & TF_BULLET) { if (pass) clist[cstart[c] + cnt[c]] = i; cnt[c]++; }
				if (t->flags & TF_PLAYER) { if (pass) plist[pstart[c] + pcnt[c]] = i; pcnt[c]++; } }
		}
	}
	free(cnt); free(pcnt); free(ray_stamp); ray_stamp = NULL;
	col_build_zr();   // perf (b35): the lists' height ranges (player.h)
	logp("demolition: %d site triangles added to the collision\n", nctri - n0);
}
// objectives as the round banner words them (footage: Downtown, Train Station - "Destroy all hostiles" / "Defend the targets")
#define DEM_GOAL(att) ((att) ? "Destroy all hostiles" : "Defend the targets")
// ---- each frame after dem_update (ev: its events): Aramaki's lines and the timer warning (footage, Train Station: "Begin
// defensive operations." at START after switching to defense, "One Enemy Remaining", at 0:20 the "Timer Warning" band with
// "Time is running out!" and the 20 s voice). The line for the attackers' start is not in the footage: none shown.
static float dem_tw_t = 99; static int dem_tw_done;
static void dem_game_frame(unsigned ev, float dt) {
	if (!dem_on) return;
	int att = dem_attacking(&dm, 0);
	if (ev & DEV_RESET) { dem_tw_t = 99; dem_tw_done = 0; em_t_cmd = 99; }
	if ((ev & DEV_START) && !att && (dm.round == 1 || dm.round == dm.switch_after + 1)) em_commander("Begin defensive operations.", 0.3f);
	if ((ev & DEV_ONE_LEFT_0) && elim_player_alive()) em_commander("Lone Survivor", 0);
	if (ev & DEV_ONE_LEFT_1) em_commander("One Enemy Remaining", 0);
	if (dm.state == DS_LIVE && !dem_tw_done && dm.round_left <= 20) { dem_tw_done = 1; dem_tw_t = 0; radio_play("voice/donut/dm/20sec"); ev_play("ui/crisis", NULL); em_commander("Time is running out!", 0.3f); }
	if (dm.state != DS_LIVE && dem_tw_t < 5) dem_tw_t = 5;   // planted / round over: the band goes
	dem_tw_t += scoreboard_hold_dt(dt);   // (perf b38: stands still under the scoreboard, elim_game.h)
}
// HUD (hud pack current, between hud_begin / hud_end): plant / defuse bar, the two site markers (on the screen edge when off-screen)
static void dem_hud_draw(const float *MVP, const float *eye, float now, int dead) {
	if (!dem_on) return;
	if (dem_prog >= 0 && !dead) rh_plant_bar(dem_prog, dm.state == DS_PLANTED, now);
	if (dm.state == DS_FREEZE || dm.state >= DS_SWITCH) return;
	int att = dem_attacking(&dm, 0);
	for (int k = 0; k < dmap.nsite; k++) {
		const DemSite *st = &dmap.site[k]; int bomb = dm.bomb_site == k && (dm.state == DS_PLANTED || dm.state == DS_ROUND_END);
		if (dm.bomb_site >= 0 && !bomb && dm.state == DS_PLANTED) continue;   // once planted only the bomb's site is marked
		const float *mp = bomb ? dm.bomb_pos : st->marker; float mk[3] = { mp[0], mp[1], mp[2] + (bomb ? 45 : 0) };
		float q[4]; for (int r = 0; r < 4; r++) q[r] = MVP[r] * mk[0] + MVP[4 + r] * mk[1] + MVP[8 + r] * mk[2] + MVP[12 + r];
		float sx, sy, dir = -1; const float L = 46, R = 914, T = 110, B = 400;   // clamp box (clear of the score panel and the gauges)
		if (q[3] > 10) { sx = (q[0] / q[3] + 1) * 480; sy = (1 - q[1] / q[3]) * 272; }
		else { sx = 480 - q[0] * 1e4f; sy = 272 + q[1] * 1e4f; }   // behind the camera: push it to the edge on the mirrored side
		if (q[3] <= 10 || sx < L || sx > R || sy < T || sy > B) {
			float dx = sx - 480, dy = sy - 272, s = 1e9f; if (fabsf(dx) > 1e-3f) s = fminf(s, (dx > 0 ? R - 480 : 480 - L) / fabsf(dx)); if (fabsf(dy) > 1e-3f) s = fminf(s, (dy > 0 ? B - 272 : 272 - T) / fabsf(dy));
			sx = 480 + dx * s; sy = 272 + dy * s; dir = atan2f(dx, -dy); if (dir < 0) dir += 6.2831853f; }
		float ddx = mk[0] - eye[0], ddy = mk[1] - eye[1], ddz = mk[2] - eye[2]; int dist = (int)(sqrtf(ddx * ddx + ddy * ddy + ddz * ddz) / 100);
		rh_site_marker(sx, sy, st->letter == 'B' ? 1 : st->letter == 'A' ? 0 : k, att, bomb, dist, dir, now);
	}
}
// in-match screens (call outside hud_begin / hud_end): round banner + START, the result band, switching sides
static void dem_overlays_draw(void) {
	if (!dem_on || scoreboard_shown()) return;   // (not over the held scoreboard: qa, b37)
	rh_legible_ovl();
	int att = dem_attacking(&dm, 0);
	if (dem_banner_t < 6) rh_round_banner(MODE_DEM, dm.round, DEM_GOAL(att), dem_banner_t, dm.round == 1, att ? RH_EMB_ATT : RH_EMB_DEF);
	menu_notice_draw("START", dem_start_t);
	if (em_t_cmd < 6) menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", em_cmd_msg, em_t_cmd);
	rh_timewarn("Timer Warning", "Time is running out!", dem_tw_t, 0);
	if (dem_band_t < DEM_POST_ROUND) { int won = dm.last_winner == 0; rh_result_band(dm.round, won ? 0 : 1, dem_reason_text(dm.last_reason, won), dm.score[0], dm.score[1], dem_band_t, att); }
	if (dem_sw_t < DEM_HALFTIME) rh_switch_band(dm.round + 1, DEM_GOAL(att), dem_sw_t, att);   // (the sides have switched: the new objective, the new side's emblem)
}
