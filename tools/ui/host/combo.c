// ui b42: HUD de-clutter states - the in-match HUD with every overlay that can share the screen with it (kill feed with up to
// five long rows, commander box with 1-3 lines, timer / zone warnings, centre notices on top of each other, the skills' notices),
// so tools/ui/host/overlap.py can measure the gaps between the elements. The engine's hud_game.h + skills.h + menu.h +
// hud_round.h with the recording GL stand-in (vitaGL.h here) and the element tags (hudtag.h). Build + run: tools/ui/host/overlap.sh
#include "vitaGL.h"
#include <sys/stat.h>
#include <math.h>
#define sceGxmTransferFinish() 0
static void logp(const char *fmt, ...);   // (defined below; skills.h logs before menu_ui.h declares it)
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"
#include "skills.h"
#include "menu.h"
#include "hud_round.h"
#include "wpn_hud.h"
#include "ga_hud.h"   // b63: Ghost Assault's HUD pieces
#include <stdarg.h>
static void logp(const char *fmt, ...) { va_list a; va_start(a, fmt); vfprintf(stderr, fmt, a); va_end(a); }   // (main.c on the Vita; menu_ui.h logs labels that do not fit)
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static const char *OUT; static char cur[64];
static void shot(const char *name) { char p[256]; snprintf(cur, sizeof(cur), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); cap_begin(p); }
static void done(void) { char p[256]; cap_end(); snprintf(p, sizeof(p), "%s/%s.tex", OUT, cur); mkdir(p, 0755); cap_dump_textures(p); }
// ui b42: every state drawn twice - the first time unrecorded, so the hud.h claims of its banners / notices are there for the
// second (the game reads the previous frame's claims) - then the claims cleared for the next state
#define S(name, ...) do { __VA_ARGS__; hud_claims_frame(); shot(name); __VA_ARGS__; done(); hud_claims_frame(); hud_claims_frame(); } while (0)
static V3 w_pos(int a) { return v3(a * 300.f, 0, 0); } static int w_alive(int a) { (void)a; return 1; } static int w_n(void) { return 4; }
static const char *w_name(int a) { static const char *N[4] = { "OPERATOR", "ISHIKAWA", "SAITO", "E" }; return N[a & 3]; }
static const float Y_[4] = { 1, 0.86f, 0.25f, 1 }, R_[4] = { 0.95f, 0.17f, 0.24f, 1 }, W_[4] = { 0.92f, 0.95f, 1, 1 };
static HudState H; static const float NOW = 3.3f;
static void base(void) {
	memset(&H, 0, sizeof(H)); H.hp = 100; H.ap = 100; H.intel = 300; H.tier = 2; H.tier_lo = 200; H.tier_hi = 400; H.cool = -1; H.skill_icon = 1; H.cool_s = 0;
	H.scoreL = 3600; H.scoreR = 1300; H.goal = 10000; H.time_s = 403; H.spread = 6; H.mag = 30; H.reserve = 90; H.cap = 30; H.wname = "M4A1"; H.mag_x = 760; H.mag_y = 340;
	H.mm_tex = mm_tex; H.mm_w = mm_w; H.mm_h = mm_h; memcpy(H.mm_aff, mm_aff, sizeof(mm_aff)); H.pyaw = 1.2f; H.chr = 1; H.bomb_t = -1;
	static const int ci[5] = { 40, 39, 17, 18, 7 }; for (int k = 0; k < 5; k++) { H.chip[k].icon = ci[k]; H.chip[k].active = k < 2; } H.nchip = 5;
	H.nblip = 2; H.blip[0].x = 900; H.blip[0].y = 300; H.blip[0].type = 2; H.blip[1].x = -1200; H.blip[1].y = 800; H.blip[1].type = 0;
}
static void round_base(int sl, int sr) {   // Elimination / Demolition: score_DM with 5 v 5 slots
	base(); H.scoreL = sl; H.scoreR = sr; H.goal = 6; H.round = 1; H.time_s = 97; H.nslot[0] = 5; H.nslot[1] = 5; H.slot[0][0].me = 1;
	for (int i = 0; i < 5; i++) { H.slot[0][i].icon = 1 + i; H.slot[0][i].alive = i != 3; H.slot[1][i].icon = 3 + i; H.slot[1][i].alive = i < 4; }
}
// the longest rows the game makes: the player's name (OPERATOR, yellow), operators' and Tachikomas' names, CHIEF ARAMAKI as a stress name
static void feed(int n) {
	static const char *K[5] = { "OPERATOR", "CHIEF ARAMAKI", "OPERATOR", "BORMAKoma", "ISHIKAWA" }, *V[5] = { "ISHIKAWA", "OPERATOR", "BORMAKoma", "TOGUSA", "CHIEF ARAMAKI" };
	const float *KC[5] = { Y_, R_, Y_, R_, W_ }, *VC[5] = { R_, Y_, R_, W_, R_ }; static const int WP[5] = { 2, 51, 2, 7, 2 };
	for (int i = 0; i < n && i < 5; i++) { HudKill *f = &H.feed[i]; snprintf(f->killer, 24, "%s", K[i]); snprintf(f->victim, 24, "%s", V[i]); memcpy(f->kc, KC[i], 16); memcpy(f->vc, VC[i], 16); f->weapon = WP[i]; f->t = 2; }
	H.nfeed = n;
}
static void hud(void) { hud_use(0); hud_begin(); hud_game_draw(&H, NOW); }   // (b63: no pause button in the match)
static void hud_sk(float t2n) {   // + skills.h's HUD (the TIER 2 SKILL READY notice when t2n > 0)
	sk_actor_set(0, SK_OPTICAL, 0); skills_reset_match(); SKV.t2n = t2n; SKA[0].intel = 420;
	hud_use(0); hud_begin(); hud_game_draw(&H, NOW); float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; skills_hud(0, I, NOW);
}
static const char *CM1 = "Capture the terminal!", *CM2 = "All units: eliminate the enemy team. Good luck.",
	*CM3 = "Enemy Think Tank defenses down! Hack it before they can redeploy";   // (63 chars: the longest a radio line gets, ttm_cm_msg[64])
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "build/data/"; OUT = argc > 2 ? argv[2] : "/tmp/combo"; char p[256];
	snprintf(p, sizeof(p), "%shud.ghud", data); int hr = hud_load(p, mk); int gr = hud_game_init(); int mr = menu_init(data, mk);
	snprintf(p, sizeof(p), "%surban_em.gmm", data); int mm = minimap_load(p);
	static const SkWorld W = { .n_actors = w_n, .pos = w_pos, .alive = w_alive, .name = w_name };
	int sr = skills_init(data, &W, 0); printf("hud %d widgets %d menus %d minimap %d skills %d\n", hr, gr, mr, mm, sr);
	// ---- kill feed, 1..5 rows of long names, beside the score panel, the pause button and the minimap (TDM)
	for (int n = 1; n <= 5; n++) { char b[16]; snprintf(b, 16, "x_feed%d", n); S(b, base(); feed(n); hud(); hud_end();); }
	S("x_pick_hip", base(); hud(); { WHud wh; memset(&wh, 0, sizeof(wh)); wh.pick = 1; wh.pick_name = "MATEBA 2006SM"; wh.pick_ammo = 18; wh.pick_icon = 2; wh.pick_hold = 0.3f; wpn_hud_draw(&wh); } hud_end(););   // b62: the pick-up box with the counter at the gun
	S("x_pick_ads", base(); H.mag_x = 475; H.mag_y = 490; H.wname = "MATEBA 2006SM"; hud(); { WHud wh; memset(&wh, 0, sizeof(wh)); wh.pick = 1; wh.pick_name = "M4A1"; wh.pick_ammo = 90; wh.pick_icon = 2; wh.pick_hold = 0.3f; wpn_hud_draw(&wh); } hud_end(););   // b62 (device b61-2): aiming / sprinting: the counter under the sight
	S("x_feed5_em", round_base(3, 2); feed(5); hud(); hud_end(););
	S("x_feed5_ttm", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.scoreL = 2; H.scoreR = 1; H.goal = 4; H.time_s = 715; feed(5); hud(); hud_end(););   // (Terminal Conquest: main.c's 2 + TTM_PH + 6; overlap.sh draws it over the tank harness's frames)
	S("x_feed5_ttm_band", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.scoreL = 2; H.scoreR = 1; H.goal = 4; H.time_s = 715; feed(5); hud_claim(222, 164, 738, 250, HUD_P_BANNER); hud(); hud_end(););   // (+ ttm.h's two-line band claim)
	S("x_feed5_dem", round_base(2, 1); H.bomb_t = 41.05f; feed(5); hud(); hud_end(););
	// ---- commander box (1, 2, 3 lines) over the skill gauge and its "400" (with the kill feed)
	S("x_cm1", base(); feed(2); hud(); hud_end(); menu_commander_draw(FACE_ARAMAKI, "ARAMAKI", CM1, 1.0f););
	S("x_cm2", base(); feed(2); hud(); hud_end(); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", CM2, 1.0f););
	S("x_cm3", base(); feed(2); hud(); hud_end(); menu_commander_draw(FACE_PAZ, "PAZ", CM3, 1.0f););
	S("x_cm2_cool", base(); H.cool = 0.4f; H.cool_s = 14; H.tier_hi = 600; feed(1); hud(); hud_end(); menu_commander_draw(FACE_TOGUSA, "TOGUSA", "Share code transmission complete.", 1.0f););
	// ---- match start (TDM / TTM: splash + Aramaki; Elimination: round banner + Aramaki)
	S("x_start_tdm", base(); H.time_s = 600; hud(); hud_end(); menu_splash_draw(MODE_TDM, 0, MENU_MODE_GOAL[MODE_TDM], 2.8f); menu_commander_draw(FACE_ARAMAKI, "ARAMAKI", CM2, 1.6f););
	S("x_start_tdm_mode", base(); H.time_s = 600; hud(); hud_end(); menu_splash_draw(MODE_TDM, 0, MENU_MODE_GOAL[MODE_TDM], 1.0f););
	// ui b44: the Terminal Conquest splash (soft band) with the mode's kill feed layout and ttm.h's bar claim (live, capturing); overlap.sh
	// also lays it over the tank harness's frame
	S("x_start_ttm", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.time_s = 900; feed(3); hud(); hud_end(); hud_claim(368, 86, 592, 140, HUD_P_BANNER); menu_splash_draw(MODE_TTM, 0, MENU_MODE_GOAL[MODE_TTM], 1.0f););
	S("x_start_ttm_goal", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.time_s = 900; feed(3); hud(); hud_end(); hud_claim(368, 86, 592, 140, HUD_P_BANNER); menu_splash_draw(MODE_TTM, 0, MENU_MODE_GOAL[MODE_TTM], 2.8f););
	S("x_start_dem", round_base(0, 0); hud(); hud_end(); rh_round_banner(MODE_DEM, 1, "Destroy all hostiles", 1.0f, 1, NULL););
	S("x_round_em", round_base(2, 1); feed(2); hud(); hud_end(); rh_round_banner(MODE_EM, 4, "Eliminate all enemy reinforcements", 1.0f, 0, "rr.emb.emA"); menu_notice_draw("START", 0.6f););
	S("x_start_em", round_base(0, 0); hud(); hud_end(); rh_round_banner(MODE_EM, 1, "Eliminate all enemy reinforcements", 3.2f, 1, "rr.emb.emA"); menu_notice_draw("START", 0.6f);
	menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "All units: eliminate the enemy team. Good luck.", 1.0f););
	// ---- timer / zone warnings beside the minimap
	S("x_tw_tdm", base(); feed(3); H.time_s = 59; H.time_warn = 1; hud(); hud_end(); menu_timewarn_draw(1.0f););
	S("x_tw_em", round_base(3, 2); feed(3); H.time_s = 19; H.time_warn = 1; hud(); hud_end(); rh_timewarn("Timer Warning", "Time is running out!", 1.0f, 0););
	S("x_tw_em_locked", round_base(3, 2); feed(3); H.time_s = 19; H.time_warn = 1; H.zone_tag = "ZONE LOCKED"; hud(); hud_end(); rh_timewarn("Timer Warning", "Time is running out!", 1.0f, 30););
	S("x_zone30", round_base(3, 2); feed(3); H.time_s = 61; hud(); hud_end(); menu_notice_draw("30 SECONDS TO ZONE LOCK", 1.0f););
	S("x_zone_lock", round_base(3, 2); feed(2); H.time_s = 31; H.zone_tag = "ZONE LOCKED"; hud(); hud_end(); menu_notice_draw("ACCESS TO CERTAIN ZONES HAS BEEN RESTRICTED.", 1.0f););
	S("x_zone_evac", round_base(3, 2); feed(2); H.time_s = 39; H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); rh_countdown(9, 0); hud_end(); menu_notice_draw("MOVE TO A SAFE ZONE!", 1.0f););
	// ---- centre notices together
	S("x_kill_t2", base(); feed(2); H.notify = "DOUBLE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; H.skill_ready = 1; H.skill2 = 1; H.intel = 420; hud_sk(1.0f); hud_end(););
	S("x_kill_t2_cm", base(); feed(2); H.notify = "DOUBLE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; H.skill_ready = 1; H.skill2 = 1; H.intel = 420; hud_sk(1.0f); hud_end();
	menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "One Enemy Remaining", 1.0f););
	// (Terminal Conquest, overtime with the FINAL TERMINAL band: ttm.h's claims for the bar and the two-line band, as it makes them -
	// overlap.sh draws this state's notices over the tank harness's frame)
	S("x_kill_t2_ttm", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.notify = "DOUBLE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; H.skill_ready = 1; H.skill2 = 1; H.intel = 420;
		hud_claim(396 - 72, 86, 564 + 72, 140, HUD_P_BANNER); hud_claim(222, 164, 738, 250, HUD_P_BANNER); hud_sk(1.0f); hud_end(););
	S("x_kill_ttm_live", base(); H.feed_top = 92; H.feed_rows = 3; H.score_narrow = 1; H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100;
		hud_claim(396 - 28, 86, 564 + 28, 140, HUD_P_BANNER); hud(); hud_end(););
	S("x_round_kill", round_base(2, 1); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end(); rh_round_banner(MODE_EM, 3, "Eliminate all enemy reinforcements", 1.5f, 0, "rr.emb.emA"););
	S("x_result_kill", round_base(3, 1); feed(2); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end();
	rh_result_band(3, 0, "Mission Successful: Team Eliminated", 3, 1, 1.2f, -1); menu_commander_draw(FACE_TOGUSA, "TOGUSA", "Share code transmission complete.", 1.0f););
	S("x_round_result", round_base(3, 1); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end();
	rh_round_banner(MODE_EM, 3, "Eliminate all enemy reinforcements", 5.5f, 0, "rr.emb.emA"); rh_result_band(3, 0, "Mission Successful: Team Eliminated", 3, 1, 0.5f, -1););
	S("x_switch_kill", round_base(3, 3); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end(); rh_switch_band(7, "Defend the targets", 1.2f, 0););
	S("x_victory_kill", base(); feed(3); H.notify = "DOUBLE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end(); menu_endsplash_draw(0, 10000, 6200, 1.0f););
	S("x_kill_sync", base(); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud_sk(0); SKV.sync_note = 1; { float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; skills_hud(0, I, NOW); } hud_end(););
	S("x_kill_notice", round_base(3, 2); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; hud(); hud_end(); menu_notice_draw("30 SECONDS TO ZONE LOCK", 1.0f););
	S("x_kill_tw", base(); feed(1); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; H.time_s = 59; H.time_warn = 1; hud(); hud_end(); menu_timewarn_draw(1.0f););
	// ---- dead: kill cam + respawn bar with the feed; spectating
	S("x_killcam", base(); feed(5); H.dead = 1; H.killcam = 1; H.hp = 0; H.ap = 0; hud(); hud_end();
	{ MenuRespawn R; memset(&R, 0, sizeof(R)); R.killer = "CHIEF ARAMAKI"; R.you = "OPERATOR"; R.killer_kills = 1; R.your_kills = 0; R.weapon_icon = 2; R.respawn_in = 1.28f; R.tip = MENU_TIPS[1]; menu_respawn_draw(&R, 0.8f); });
	// ---- b63 Ghost Assault (ga_hud.h): the cyborg's compass ring with pings and the seeker drone, the corpse hack, the cyber dive;
	// the ghost's DANGER / DETECTED, an ally restored - with the round HUD (3 v 3, first to 4), the cyborg's 200 HP and no counter,
	// the ghost's pistol counter under the sight (aiming / sprinting: its lowest spot), the commanders and the mode's notices
	{ GaHudView V; memset(&V, 0, sizeof(V)); V.now = NOW; V.compass = 1; V.yaw = 1.2f; V.nping = 3;
	  V.ping_rel[0] = -0.3f; V.ping_rel[1] = 0.5f; V.ping_rel[2] = 2.0f; for (int k = 0; k < 3; k++) V.ping_show[k] = 1.2f; V.ping_close[1] = 1;
	  V.drone = 1; V.drone_x = 780; V.drone_y = 200; V.drone_m = 17;
	#define GA_CY() round_base(1, 2); H.goal = 4; H.nslot[0] = H.nslot[1] = 3; H.hp = 189; H.hp_max = 200; H.ap = 0; H.cap = 0; H.wname = NULL; H.mag = H.reserve = 0
	#define GA_GH() round_base(2, 1); H.goal = 4; H.nslot[0] = H.nslot[1] = 3; H.hp = 100; H.hp_max = 100; H.ap = 0; H.mag = 20; H.reserve = 0; H.cap = 20; H.wname = "SEBURO M5"; H.mag_x = 475; H.mag_y = 490
	  S("x_ga_cyborg", GA_CY(); feed(2); hud(); ga_hud_paint(&V); hud_end(); menu_commander_draw(14, "TERRORIST LEADER", "Find the Ghosts and eliminate them!", 1.0f); menu_notice_draw("Seeker Drones report enemy locations.", 1.0f););
	  GaHudView Vh = V; Vh.drone = 0; Vh.hack = 1; Vh.hack_pr = 0.6f;
	  S("x_ga_hack", GA_CY(); H.notify = "NICE KILL"; H.notify_t = 0.5f; H.notify_pts = 100; feed(3); hud(); ga_hud_paint(&Vh); hud_end(); menu_notice_draw("SEEKER DRONE AVAILABLE", 1.0f););
	  GaHudView Vd = V; Vd.drone = 0; Vd.nping = 0; Vd.dive_k = 1; Vd.ndia = 2; Vd.dia[0][0] = 300; Vd.dia[0][1] = 250; Vd.dia[1][0] = 820; Vd.dia[1][1] = 300;
	  S("x_ga_dive", GA_CY(); H.notify = "ENEMY CYBERBRAIN HACKED"; H.notify_t = 0.5f; H.notify_pts = 300; hud(); ga_hud_paint(&Vd); hud_end(); menu_commander_draw(14, "TERRORIST LEADER", "Cyborg has hacked a Ghost body. Enemy positions revealed.", 1.0f););
	  GaHudView Vg; memset(&Vg, 0, sizeof(Vg)); Vg.now = NOW; Vg.detected_k = 1; Vg.danger_k = 1; Vg.hack = 1; Vg.hack_ally = 1; Vg.hack_pr = 0.4f;
	  S("x_ga_ghost", GA_GH(); feed(2); hud(); ga_hud_paint(&Vg); hud_end(); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "Stay hidden. Survive until the time runs out!", 1.0f););
	  S("x_ga_ghost_tw", GA_GH(); H.time_s = 29; H.time_warn = 1; hud(); ga_hud_paint(&Vg); hud_end(); rh_timewarn("Timer Warning", "Time is running out!", 1.0f, 0); menu_notice_draw("Enemy Seeker Drones report your team's positions.", 1.0f););
	  rh_lbl_a = "CYBORG"; rh_lbl_e = "GHOST";
	  S("x_ga_result", GA_CY(); hud(); hud_end(); rh_result_band(3, 1, "Mission Failed: Time's Up", 1, 2, 1.2f, -1););
	  rh_lbl_a = rh_lbl_e = NULL;
	  S("x_ga_round", GA_GH(); hud(); hud_end(); rh_round_banner(MODE_GA, 4, "Stay hidden and survive until time runs out", 1.0f, 0, NULL); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", "Stay hidden. Survive until the time runs out!", 1.0f););
	}
	S("x_spec", round_base(1, 3); feed(4); H.dead = 1; H.spectating = 1; H.hp = 0; H.ap = 0; hud(); rh_spectate4("ISHIKAWA", "1 Kill / 2 Death", "SAITO", "BATOU"); hud_end(););
	{ // ui b42: the claims at work where two harnesses' parts meet (ttm.h's terminal letters vs the commander box): a frame with the box
	  base(); hud(); hud_end(); menu_commander_draw(FACE_ARAMAKI, "CHIEF ARAMAKI", CM2, 1.0f); hud_claims_frame(); int hid = 0;
	  for (int o = 0; o < 5; o++) { float x = 40 + o * 32, y = 198; hid += hud_blocked(x - 14, y - 24, x + 14, y + 6, HUD_P_FEED); }
	  printf("claims: %d of 5 terminal letters under the commander box%s\n", hid, hid ? " (b61: so the whole strip waits)" : ""); hud_claims_frame(); hud_claims_frame(); }
	return 0;
}
