// Host test of the in-match HUD: runs the engine's own hud_game.h / hud_round.h / menu.h overlay code with the recording
// GL stand-in (vitaGL.h here) and writes one capture per HUD state; raster_hud.py turns each into a PNG (over a footage
// frame). Build + run: tools/ui/host/run.sh  (data: build/data hud.ghud, ovl.ghud, <stem>.gmm)
#include "vitaGL.h"
#include <sys/stat.h>
#include <math.h>
#include <time.h>
#ifdef CG
#include <valgrind/callgrind.h>
#endif
#include <stdlib.h>
#define sceGxmTransferFinish() 0
#include "../../../engine/src/hud_game.h"
#include "../../../engine/src/menu.h"
#include "../../../engine/src/hud_round.h"
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static const char *OUT; static char cur[64];
static void shot(const char *name) { char p[256]; snprintf(cur, sizeof(cur), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); cap_begin(p); }
static void done(void) { char p[256]; cap_end(); snprintf(p, sizeof(p), "%s/%s.tex", OUT, cur); mkdir(p, 0755); cap_dump_textures(p); }
// ui b42: every state drawn twice - the first time unrecorded, so the hud.h claims of its banners / notices are there for the
// second (the game reads the previous frame's claims) - then the claims cleared for the next state
#define S(name, ...) do { __VA_ARGS__; hud_claims_frame(); shot(name); __VA_ARGS__; done(); hud_claims_frame(); hud_claims_frame(); } while (0)
static void base(HudState *H) {
	memset(H, 0, sizeof(*H)); H->hp = 100; H->ap = 100; H->intel = 300; H->tier = 2; H->tier_lo = 200; H->tier_hi = 400; H->cool = -1; H->skill_icon = 1;
	H->scoreL = 800; H->scoreR = 600; H->goal = 8000; H->time_s = 543; H->spread = 6; H->mag = 43; H->reserve = 69; H->cap = 50; H->wname = "UMP-45S1"; H->mag_x = 760; H->mag_y = 340;
	H->mm_tex = mm_tex; H->mm_w = mm_w; H->mm_h = mm_h; memcpy(H->mm_aff, mm_aff, sizeof(mm_aff)); H->px = 0; H->py = 0; H->pyaw = 1.2f; H->chr = 1; H->bomb_t = -1;
	static const int ci[5] = { 40, 39, 17, 18, 7 }; for (int k = 0; k < 5; k++) { H->chip[k].icon = ci[k]; H->chip[k].active = k < 2; } H->nchip = 5;
	H->ntag = 1; H->tag[0].x = 300; H->tag[0].y = 250; H->tag[0].a = 1; H->tag[0].name = "BATOU";
	H->nblip = 3; H->blip[0].x = 900; H->blip[0].y = 300; H->blip[0].type = 2; H->blip[1].x = -1200; H->blip[1].y = 800; H->blip[1].type = 0; H->blip[2].x = 600; H->blip[2].y = -1500; H->blip[2].type = 1;
}
static void feed3(HudState *H) {
	static const float Y[4] = { 1, 0.86f, 0.25f, 1 }, R[4] = { 0.95f, 0.17f, 0.24f, 1 }, W[4] = { 0.92f, 0.95f, 1, 1 };
	const char *k[3] = { "COM555", "ISHIKAWA", "MOTOKO" }, *v[3] = { "KURUTSU", "SAITO", "WOnderwoman" }; const float *kc[3] = { R, R, W }, *vc[3] = { W, W, R };
	for (int i = 0; i < 3; i++) { HudKill *f = &H->feed[i]; snprintf(f->killer, 24, "%s", k[i]); snprintf(f->victim, 24, "%s", v[i]); memcpy(f->kc, kc[i], 16); memcpy(f->vc, vc[i], 16); f->weapon = i ? 2 : 51; f->t = 2; }
	H->nfeed = 3; (void)Y;
}
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "build/data/"; OUT = argc > 2 ? argv[2] : "/tmp/hudcap"; char p[256];
	snprintf(p, sizeof(p), "%shud.ghud", data); int hr = hud_load(p, mk); int gr = hud_game_init(); int mr = menu_init(data, mk);
	snprintf(p, sizeof(p), "%surban_em.gmm", data); int mm = minimap_load(p);
	printf("hud %d widgets %d menus %d minimap %d\n", hr, gr, mr, mm);
#ifdef VIN_FULL
	HG.vin_n = 0;   // the full vignette quads (fill comparison)
#endif
	HudState H; float now = 3.3f;
	if (getenv("HUD_BENCH")) {   // perf (b35): the CPU cost of a HUD frame (no capture): HUD_BENCH=<frames>; under callgrind: instructions per frame
		int nfr = atoi(getenv("HUD_BENCH")); struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
#ifdef CG
		CALLGRIND_TOGGLE_COLLECT;
#endif
		for (int f = 0; f < nfr; f++) { base(&H); feed3(&H); H.ntag = 2; H.tag[1] = H.tag[0]; H.tag[1].x = 500; hud_use(0); hud_begin(); hud_game_draw(&H, now + f / 60.f); hud_end(); }
#ifdef CG
		CALLGRIND_TOGGLE_COLLECT;
#endif
		clock_gettime(CLOCK_MONOTONIC, &t1); printf("HUD frame (TDM, hip): %.1f us host\n", ((t1.tv_sec - t0.tv_sec) * 1e6 + (t1.tv_nsec - t0.tv_nsec) / 1e3) / nfr); return 0; }
	// ---- TDM, hip fire (footage: TDM Neon Ruins t=60)
	S("tdm_hip", base(&H); feed3(&H); hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(););
	// ---- Tier 2 ready (orange ring, yellow badge), Tier 1 ready
	S("tdm_t2", base(&H); H.intel = 420; H.tier_hi = 400; H.skill_ready = 1; H.skill2 = 1; H.wname = "DEATHSTALKER EVO"; H.mag_x = 574; H.mag_y = 377; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(););
	// ---- low health, AP gone, skill cooling down, aiming
	S("tdm_low", base(&H); H.hp = 14; H.ap = 0; H.cool = 0.55f; H.cool_s = 14; H.skill2 = 3; H.ads = 1; H.mag = 8; H.mag_x = 475; H.mag_y = 490; H.hurt = 0.4f; H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100;
	hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(););
	// ---- Elimination: zone countdown + spectator line, round score
	S("em_evac", base(&H); H.scoreL = 3; H.scoreR = 2; H.goal = 6; H.time_s = 39; H.round = 1; H.nslot[0] = 5; H.nslot[1] = 5; H.slot[0][4].me = 1;
	for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = i != 3; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = i < 2; }
	hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_countdown(9, 0); hud_end();
	menu_notice_draw("MOVE TO A SAFE ZONE!", 1.0f););
	S("em_locked", base(&H); H.scoreL = 3; H.scoreR = 2; H.goal = 6; H.time_s = 25; H.round = 1; H.nslot[0] = 5; H.nslot[1] = 5;
	for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = i < 2; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = i < 1; }
	hud_use(0); hud_begin(); H.zone_tag = "ZONE LOCKED"; hud_game_draw(&H, now); hud_end(); menu_notice_draw("ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED.", 1.0f););
	S("em_banner", base(&H); H.scoreL = 1; H.scoreR = 1; H.goal = 6; H.time_s = 90; H.round = 1; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_round_banner(MODE_EM, 3, "Eliminate all enemy reinforcements", 1.5f, 0, "rr.emb.emA"););
	S("em_notice", base(&H); H.round = 1; H.goal = 6; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_legible_ovl(); menu_notice_draw("ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED.", 1.0f););
	S("em_result", base(&H); H.scoreL = 2; H.scoreR = 1; H.goal = 6; H.time_s = 37; H.round = 1; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_result_band(2, 0, "Mission Successful: Team Eliminated", 2, 1, 1.2f, -1););
	S("em_draw", base(&H); H.scoreL = 4; H.scoreR = 3; H.goal = 6; H.time_s = 0; H.round = 1; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_result_band(6, 2, "Draw: Time Up", 4, 3, 1.2f, -1););
	S("em_switch", base(&H); H.scoreL = 3; H.scoreR = 2; H.goal = 6; H.time_s = 50; H.round = 1; H.dead = 1; H.hp = 0; H.ap = 0; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_switch_band(6, "Eliminate all enemy reinforcements", 1.2f, -1););
	S("em_timewarn", base(&H); H.scoreL = 3; H.scoreR = 2; H.goal = 6; H.time_s = 19; H.time_warn = 1; H.round = 1; H.zone_tag = "ZONE LOCKED"; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_timewarn("Timer Warning", "Time is running out!", 1.0f, 30););
	S("em_spec", base(&H); H.spectating = 1; H.scoreL = 3; H.scoreR = 1; H.goal = 6; H.time_s = 52; H.round = 1; H.dead = 1; H.hp = 75; hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_spectate4("REIKO", "1 Kill / 2 Death", "SAITO", "BATOU"); hud_end(););
	// ---- Demolition: planted bomb row (SS:cc), slots, plant bar, site markers (on screen + on the edge), result band with sides
	S("dem_plant", base(&H); H.scoreL = 2; H.scoreR = 1; H.goal = 8; H.time_s = 100; H.round = 1; H.nslot[0] = 5; H.nslot[1] = 5; H.slot[0][0].me = 1;
	for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = i != 3; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = i < 4; }
	hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_plant_bar(0.6f, 0, now); rh_site_marker(600, 250, 0, 1, 0, 12, -1, now); rh_site_marker(914, 300, 1, 1, 0, 48, 1.4f, now); hud_end(););
	S("dem_bomb", base(&H); H.scoreL = 2; H.scoreR = 1; H.goal = 8; H.time_s = 100; H.round = 1; H.bomb_t = 41.05f; H.nslot[0] = 5; H.nslot[1] = 5; H.slot[0][0].me = 1;
	for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = i < 2; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = i < 4; }
	hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_plant_bar(0, 1, now); rh_site_marker(380, 230, 1, 0, 1, 7, -1, now); hud_end(););
	S("dem_result", base(&H); H.scoreL = 3; H.scoreR = 0; H.goal = 6; H.time_s = 100; H.round = 1; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_result_band(3, 0, "Mission Successful: Bomb Defused", 3, 0, 1.2f, 0););
	// ---- Demolition round flow (footage: Train Station - Andu, Downtown - Objective Rush Mid)
	#define DEM_BASE(sl, sr) do { base(&H); H.scoreL = sl; H.scoreR = sr; H.goal = 6; H.round = 1; H.nslot[0] = 5; H.nslot[1] = 5; H.slot[0][0].me = 1; \
		for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = 1; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = 1; } } while (0)
	S("dem_banner", DEM_BASE(1, 0); H.time_s = 150; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_round_banner(MODE_DEM, 2, "Destroy all hostiles", 1.5f, 0, RH_EMB_ATT););
	S("dem_result_att", DEM_BASE(1, 0); H.time_s = 69; H.bomb_t = 14.57f; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_result_band(1, 0, "Mission Successful: Team Eliminated", 1, 0, 1.2f, 1););
	S("dem_switch", DEM_BASE(3, 2); H.time_s = 87; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_switch_band(6, "Defend the targets", 2.0f, 0););
	S("dem_switch_flip", DEM_BASE(3, 2); H.time_s = 87; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_switch_band(6, "Defend the targets", 0.85f, 0););
	S("dem_start_cmd", DEM_BASE(3, 2); H.time_s = 150; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); menu_notice_draw("START", 0.5f); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "Begin defensive operations.", 1.0f););
	S("dem_spec", DEM_BASE(0, 0); H.spectating = 1; H.time_s = 112; H.dead = 1; H.hp = 13; H.ap = 0; H.slot[0][0].alive = 0; H.slot[0][3].alive = 0; H.slot[1][1].alive = 0;
	hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_spectate4("SAITO", "1 Kill / 1 Death", "BATOU", "PAZ"); hud_end(););
	S("dem_timewarn", DEM_BASE(4, 3); H.time_s = 19; H.time_warn = 1; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_timewarn("Timer Warning", "Time is running out!", 1.0f, 0); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "Time is running out!", 1.0f););
	// ---- Team Deathmatch kill cam (footage: PSS9 94 s, Port 344 s): no gauges, the respawn bar with its timer
	S("tdm_killcam", base(&H); feed3(&H); H.scoreL = 600; H.scoreR = 400; H.goal = 10000; H.time_s = 550; H.dead = 1; H.killcam = 1; H.hp = 0; H.ap = 0; hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end();
	{ MenuRespawn R; memset(&R, 0, sizeof(R)); R.killer = "SAITO"; R.you = "COM555"; R.killer_kills = 1; R.your_kills = 0; R.weapon_icon = 2; R.respawn_in = 1.28f; R.tip = MENU_TIPS[1]; menu_respawn_draw(&R, 0.8f); });
	// ---- in-match scoreboard (hold the score panel)
	S("scoreboard", base(&H); H.scoreL = 2; H.scoreR = 1; H.goal = 6; H.time_s = 52; H.round = 1;
	H.no_score = 1; rh_sb_type = "CUSTOM MATCH"; rh_sb_time = H.time_s;   // perf (b38): as main.c: the HUD timer hidden under the board, the time and match type in its header
	hud_use(0); hud_begin(); hud_game_draw(&H, now);
	{ RhRow R[10]; const char *nm[9] = { "COM555", "BATOU", "SAITO", "PAZ", "MOTOKO", "ISHIKAWA", "REIKO", "SITARA", "AZUMA" }; int pt[9] = { 1, 2, 3, 6, 1, 5, 11, 12, 9 };
	  for (int i = 0; i < 9; i++) { snprintf(R[i].name, 24, "%s", nm[i]); R[i].portrait = pt[i]; R[i].team = i >= 4; R[i].kills = 9 - i; R[i].deaths = i % 3; R[i].score = (9 - i) * 100; R[i].alive = i != 2 && i != 6; R[i].me = i == 0; }
	  RhMap M; memset(&M, 0, sizeof(M)); M.tex = mm_tex; M.w = mm_w; M.h = mm_h; memcpy(M.aff, mm_aff, sizeof(M.aff)); M.dot[0].x = 0; M.dot[0].y = 0; M.dot[0].yaw = 0.5f; M.dot[0].type = 0; M.dot[1].x = 1500; M.dot[1].y = -900; M.dot[1].type = 1; M.ndot = 2;
	  rh_scoreboard("ELIMINATION", "Eliminate all enemy reinforcements", "URBAN CITY", R, 9, 2, 1, &M, 1); }
	hud_end(););
	// ---- states matching footage frames (tools/ui/host/compare.sh renders them over the frames for side-by-side sheets)
	{ static const float R_[4] = { 0.95f, 0.17f, 0.24f, 1 }, W_[4] = { 0.92f, 0.95f, 1, 1 }, Y_[4] = { 1, 0.86f, 0.25f, 1 };
	S("cmp_tdm", base(&H); H.scoreL = 3600; H.scoreR = 1300; H.goal = 8000; H.time_s = 403; H.ap = 66; H.intel = 420; H.tier_hi = 400; H.skill_ready = 1; H.skill2 = 1; H.mag = 50; H.reserve = 24; H.cap = 50; H.wname = "DEATHSTALKER EVO"; H.mag_x = 492; H.mag_y = 487; H.ntag = 1; H.tag[0].x = 612; H.tag[0].y = 203; H.tag[0].name = "MIOKYZOHE";
	H.nfeed = 2; snprintf(H.feed[0].killer, 24, "WOnderwoman"); snprintf(H.feed[0].victim, 24, "Marrius"); memcpy(H.feed[0].kc, W_, 16); memcpy(H.feed[0].vc, R_, 16); H.feed[0].weapon = 2; H.feed[0].t = 2;
	snprintf(H.feed[1].killer, 24, "Jii7"); snprintf(H.feed[1].victim, 24, "COM555"); memcpy(H.feed[1].kc, Y_, 16); memcpy(H.feed[1].vc, R_, 16); H.feed[1].weapon = 2; H.feed[1].t = 2;
	hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(););
	S("cmp_em", base(&H); H.scoreL = 1; H.scoreR = 1; H.goal = 6; H.time_s = 37; H.round = 1; H.hp = 150; H.intel = 260; H.tier_hi = 400; H.mag = 43; H.reserve = 100; H.cap = 50; H.mag_x = 492; H.mag_y = 487; H.ntag = 0;
	H.nslot[0] = 1; H.nslot[1] = 1; H.slot[0][0].me = 1; H.slot[0][0].alive = 1; H.slot[0][0].icon = 1; H.slot[1][0].alive = 0;
	H.nfeed = 2; snprintf(H.feed[0].killer, 24, "Jii7"); snprintf(H.feed[0].victim, 24, "Sokuzani"); memcpy(H.feed[0].kc, Y_, 16); memcpy(H.feed[0].vc, R_, 16); H.feed[0].weapon = 2; H.feed[0].t = 2;
	snprintf(H.feed[1].killer, 24, "Sokuzani"); snprintf(H.feed[1].victim, 24, "SetOnAction"); memcpy(H.feed[1].kc, R_, 16); memcpy(H.feed[1].vc, W_, 16); H.feed[1].weapon = 2; H.feed[1].t = 2;
	hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); rh_result_band(2, 0, "Mission Successful: Team Eliminated", 1, 1, 1.2f, -1););
	S("cmp_user", base(&H); H.scoreL = 0; H.scoreR = 0; H.goal = 8000; H.time_s = 0; H.hp = 150; H.ap = 100; H.intel = 0; H.chr = 2; H.mag = 50; H.reserve = 100; H.cap = 50; H.wname = "UMP-45S1"; H.mag_x = 560; H.mag_y = 372; H.ntag = 0; H.nfeed = 0; H.nchip = 0;   // b93: the user's PC screenshot (2026-11-18, TDM start)
	hud_use(0); hud_begin(); hud_game_draw(&H, now); hud_end(); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "Eliminate all enemy agents!", 1.0f););
	S("cmp_dem", base(&H); H.scoreL = 2; H.scoreR = 0; H.goal = 6; H.time_s = 100; H.round = 1; H.bomb_t = 41.05f; H.hp = 150; H.cool = 0.1f; H.cool_s = 21.2f; H.mag = 50; H.reserve = 78; H.cap = 50; H.mag_x = 492; H.mag_y = 487; H.ntag = 0;
	H.nslot[0] = 3; H.nslot[1] = 3; H.slot[0][0].me = 1; H.slot[0][0].alive = 1; H.slot[0][0].icon = 1; for (int i = 0; i < 3; i++) { H.slot[1][i].alive = i < 2; H.slot[1][i].icon = 3 + i; }
	hud_use(0); hud_begin(); hud_game_draw(&H, now); rh_site_marker(739, 253, 1, 0, 0, 22, -1, now); rh_site_marker(806, 262, 0, 0, 0, 31, -1, now); hud_end();); }
	return 0;
}

#include <stdarg.h>
void logp(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); }   // (main.c on the Vita)
