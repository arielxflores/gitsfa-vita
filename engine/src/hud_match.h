// HUD state the match adds on top of what main.c fills (hud_game.h HudState): the operator portrait in the HP ring, the
// skill gauge's flags, and in the round modes (Demolition, Elimination) score_DM's player slots and the bomb timer.
// Included by main.c after dem_game.h. main.c hook: hud_match_state(&H_, cfg.chr, pdead_t > 0) after elim_hud(&H_).
#pragma once
// MENU_CHR order (bato, motoko, saito, togusa, ishikawa, borma, paz, azuma, kuro, maven, reiko, sitara) -> the HUD movie's
// portrait frames hp.chr.N (game order: 1 Motoko, 2 Batou, 3 Saito, 4 Paz, 5 Ishikawa, 6 Togusa, 7 Borma, 8 Maven, 9 Kuro,
// 10 Azuma, 11 Reiko, 12 Sitara; b87: checked against the user's footage, below)
// b87 (user: "some of the operatives and their portraits didn't match in the operatives screen and in game"): the HP ring's
// frames re-checked against the user's v2.0 footage (EP19-EP26: the HP ring of each video's operator, masked NCC inside the
// ring + the armory's skin cards "TOGUSA - DEALER" / "KURO - NIGHT OPS"): 4 is Paz and 6 Togusa (as cm.face), 8 Maven, 9 Kuro,
// 10 Azuma - until b86 Togusa / Paz and Azuma / Kuro / Maven showed each other's faces in game
static const unsigned char HM_PORTRAIT[12] = { 2, 1, 3, 6, 5, 7, 4, 10, 9, 8, 11, 12 };
static int hm_skill_icon(int actor) { int ic = 8; sk_hud(actor, &ic, NULL, NULL); return ic; }
static void hud_match_state(HudState *H, int chr, int dead) {
	H->chr = chr >= 0 && chr < 12 ? HM_PORTRAIT[chr] : 0;
	H->bomb_t = -1; H->killcam = killcam_active();   // (killcam.h: no gauges while the camera shows the killer)
	// skill gauge (skills.h sk_hud_info): Tier 1 = the small ring (countdown seconds / ready / 'no' while disabled), Tier 2 = the big
	// ring (intel / cost, orange when ready, dark while it runs, 'no' while disabled)
	if (sk_ok && sk_valid(0)) { SkHudInfo hi; sk_hud_info(0, &hi);
		H->cool_s = hi.t1_left > 0 ? hi.t1_left : 0; H->skill_off = hi.t1_state == 2; if (hi.t1_state == 0) H->cool = -1; else if (hi.t1_state == 1 && H->cool < 0) H->cool = 1;
		H->skill2 = hi.t2_state; H->skill_ready = hi.t2_state == 1; if (hi.cost2 > 0) { H->tier_hi = hi.cost2; H->intel = hi.intel; } }
	else H->skill_ready = H->tier_hi > 0 && H->intel >= H->tier_hi;
	if (!dem_on && !elim_on) return;
	// round modes: score_DM with a slot per player (the operator's skill icon; the fallen are taken off), the player first
	H->round = 1; H->nslot[0] = H->nslot[1] = 0;
	H->slot[0][0].icon = H->skill_icon; H->slot[0][0].alive = !dead; H->slot[0][0].me = 1; H->nslot[0] = 1;
	for (int i = 0; i < nbot; i++) { int sd = bots[i].team ? 1 : 0; if (H->nslot[sd] >= 6) continue;
		int k = H->nslot[sd]++; H->slot[sd][k].icon = hm_skill_icon(1 + i); H->slot[sd][k].alive = bots[i].state != BS_DEAD; H->slot[sd][k].me = 0; }
	if (dem_on) {   // Demolition: the round timer stops at the plant; the bomb row counts the fuse down (seconds : hundredths)
		float tl = dm.state == DS_FREEZE ? DEM_ROUND_TIME : dm.round_left; H->time_s = (int)(tl + 0.999f);
		H->time_warn = dm.state == DS_LIVE && H->time_s <= 20;   // footage (Train Station): red from 0:20, with the Timer Warning band
		if (dm.state == DS_PLANTED || (dm.state == DS_ROUND_END && dm.bomb_site >= 0 && dm.last_reason == DR_DEFUSED)) H->bomb_t = dm.fuse_left;
	}
}
// ---- in-match scoreboard (the original's TAB board): shown while the front touch screen is held on the score panel at the
// top (the Vita has no TAB; Select stays the developer fly toggle), or when the test run asks for it (force); + the pause button
#ifdef __vita__
#include <psp2/touch.h>
#endif
// front touch in the match (sampled once a frame from scoreboard_draw): the score panel held = scoreboard. (b55-b62: a pause
// button at the right edge; removed in b63 at the user's request: Start opens the pause menu)
static struct { int n, was_in, tap, polls, tap_poll; float x[4], y[4]; } HMT;
#define HM_PAUSE_X0 880
#define HM_PAUSE_Y0 205
#define HM_PAUSE_S  80
static int hm_in_pause(float x, float y) { return x >= HM_PAUSE_X0 && x <= HM_PAUSE_X0 + HM_PAUSE_S && y >= HM_PAUSE_Y0 && y <= HM_PAUSE_Y0 + HM_PAUSE_S; }
static void hm_touch_poll(void) {
	HMT.n = 0; HMT.polls++;
#ifdef __vita__
	static int init; if (!init) { sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START); init = 1; }
	SceTouchData td; if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) >= 0) for (int i = 0; i < td.reportNum && HMT.n < 4; i++) { HMT.x[HMT.n] = td.report[i].x * 960.0f / 1920.0f; HMT.y[HMT.n] = td.report[i].y * 544.0f / 1088.0f; HMT.n++; }
#endif
	int in = HMT.n > 0 && hm_in_pause(HMT.x[0], HMT.y[0]);
	if (HMT.was_in && HMT.n == 0) { HMT.tap = 1; HMT.tap_poll = HMT.polls; }   // released after a touch that stayed on the button
	HMT.was_in = in || (HMT.was_in && HMT.n > 0 && hm_in_pause(HMT.x[0], HMT.y[0]));
}
static __attribute__((unused)) int hud_pause_tap(void) { int t = HMT.tap && HMT.polls - HMT.tap_poll <= 2; HMT.tap = 0; return t; }   // (a fresh tap only)
static int sb_touch_top(void) { for (int i = 0; i < HMT.n; i++) if (HMT.y[i] < 120 && HMT.x[i] > 270 && HMT.x[i] < 690) return 1; return 0; }
static __attribute__((unused)) void hm_pause_button_draw(void) { rh_pause_button(HM_PAUSE_X0, HM_PAUSE_Y0, HM_PAUSE_S, HMT.was_in); }
// b37 (qa's fix, perf b38): while the scoreboard is up the round-mode screens (Demolition / Elimination round banners, START,
// zone notices, result and switch bands, Aramaki's lines; Terminal Conquest's banners) are not drawn over it, and their
// display clocks stand still (scoreboard_hold_dt), so a banner that came up under the board plays when it closes. A board
// held longer than SB_HOLD_MAX lets the clocks run again: what is that old by then has passed (a newer event replaces it).
#define SB_HOLD_MAX 8.0f
static float hm_sb_a, hm_sb_held;   // the board's fade; how long it has been up
static int scoreboard_shown(void) { return hm_sb_a > 0.01f; }
static float scoreboard_hold_dt(float dt) { return scoreboard_shown() && hm_sb_held < SB_HOLD_MAX ? 0 : dt; }
static void scoreboard_draw(const char *my_name, int my_kills, int my_deaths, int my_score, int dead, const float *pos, float yaw, const char *map_name, int force, float dt) {
	hm_touch_poll();   // b63 (user: the pause button was visual clutter - Start opens the pause menu): no button, no touch target
	float a = hm_sb_a; int on = force || sb_touch_top(); a += (on ? 10.f : -10.f) * dt; a = a < 0 ? 0 : a > 1 ? 1 : a; hm_sb_a = a;
	hm_sb_held = a > 0.01f ? hm_sb_held + dt : 0;
	if (a <= 0.01f) return;
	RhRow R[NBOT_MAX + 1]; int n = 0;
	snprintf(R[n].name, 24, "%s", my_name ? my_name : "PLAYER"); R[n].portrait = cfg.chr >= 0 && cfg.chr < 12 ? HM_PORTRAIT[cfg.chr] : 1; R[n].team = 0;
	R[n].kills = my_kills; R[n].deaths = my_deaths; R[n].score = my_score; R[n].alive = !dead; R[n].me = 1; n++;
	for (int pass = 0; pass < 2; pass++) for (int i = 0; i < nbot && n < NBOT_MAX + 1; i++) if (bots[i].team == pass) {   // allies, then enemies; each by score
		RhRow *r = &R[n++]; snprintf(r->name, 24, "%s", bot_name(i)); int op = op_of_chrt(bots[i].ct); r->portrait = op >= 0 ? HM_PORTRAIT[op] : 1;
		r->team = bots[i].team ? 1 : 0; r->kills = bots[i].kills; r->deaths = bots[i].deaths; r->score = bots[i].kills * 100; r->alive = bots[i].state != BS_DEAD; r->me = 0; }
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && R[j].team == R[j - 1].team && R[j].score > R[j - 1].score; j--) { RhRow t = R[j]; R[j] = R[j - 1]; R[j - 1] = t; }
	RhMap M; memset(&M, 0, sizeof(M)); M.tex = mm_tex; M.w = mm_w; M.h = mm_h; memcpy(M.aff, mm_aff, sizeof(M.aff));
	if (!dead) { M.dot[M.ndot].x = pos[0]; M.dot[M.ndot].y = pos[1]; M.dot[M.ndot].yaw = yaw; M.dot[M.ndot].type = 0; M.ndot++; }
	for (int i = 0; i < nbot && M.ndot < 13; i++) if (bots[i].team == 0 && bots[i].state != BS_DEAD) { M.dot[M.ndot].x = bots[i].pl.pos.x; M.dot[M.ndot].y = bots[i].pl.pos.y; M.dot[M.ndot].yaw = bots[i].pl.yaw; M.dot[M.ndot].type = 1; M.ndot++; }
	const char *mode = ga_on ? "GHOST ASSAULT" : elim_on ? "ELIMINATION" : dem_on ? "DEMOLITION" : MENU_MODE_NAME[cfg.mode >= 0 && cfg.mode < MODE_COUNT ? cfg.mode : 0];
	const char *goal = ga_on ? ga_goal_text() : elim_on ? "Eliminate all enemy reinforcements" : dem_on ? DEM_GOAL(dem_attacking(&dm, 0)) : MENU_MODE_GOAL[cfg.mode >= 0 && cfg.mode < MODE_COUNT ? cfg.mode : 0];
	int sA = elim_on ? em.score[0] : dem_on ? dm.score[0] : score_l, sE = elim_on ? em.score[1] : dem_on ? dm.score[1] : score_r;
	char mn[40]; snprintf(mn, sizeof(mn), "%s", map_name ? map_name : ""); for (char *c = mn; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
	rh_scoreboard(mode, goal, mn, R, n, sA, sE, &M, a);
}
