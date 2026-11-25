// In-match HUD: the original widgets (see hud.h) driven by the game state. Layout = the movies' own (the final game
// version, as in the 2016 gameplay videos): HP / AP around the operator portrait, the two-ring skill gauge, hexagon
// chips, score_TDM or (round modes) score_DM with the player slots. tools/ui/host/run.sh renders it offline.
#pragma once
#ifndef VGL_XFER_WAIT
#ifdef __vita__
#define VGL_XFER_WAIT() sceGxmTransferFinish()   // vitaGL uploads copy asynchronously: wait before the source buffer is reused
#else
#define VGL_XFER_WAIT() ((void)0)
#endif
#endif
#include "hud.h"
#define HUD_FEED 5
#define HUD_CHIPS 5
#define HUD_DMG 4
#define HUD_FEED_ROWS 4    // ui b42: kill feed rows shown (the newest; main.c keeps HUD_FEED)
#define HUD_FEED_EM 22.0f  // ui b42: kill feed name size (px; was ~30) and row pitch: names >= 20 px (legibility), even rows
#define HUD_FEED_PITCH 26.0f
typedef struct { char killer[24], victim[24]; float kc[4], vc[4]; int weapon; float t; } HudKill;
typedef struct {
	float hp, ap;                         // 0..100 (hp: 0..hp_max)
	float hp_max;                         // b63: full health (0 = 100; Ghost Assault's cyborgs 200)
	int intel, tier, tier_lo, tier_hi;    // intel points, tier and its thresholds (gauge = intel / tier_hi)
	float cool;                           // skill cooldown 0..1 (<0: ready)
	int skill_icon; const char *key;      // skill icon (skill.N) and the key shown on its badge
	int scoreL, scoreR, goal, time_s, time_warn;
	float ads, spread; int aim_red;       // crosshair
	float hit_t, kill_t;                  // hit marker timers (s left)
	int mag, reserve, cap; const char *wname; float mag_x, mag_y; // ammo counter at a screen point
	const char *notify; float notify_t; int notify_pts;            // "NICE KILL" +100 (notify_t counts up from 0)
	float dmg_ang[HUD_DMG], dmg_t[HUD_DMG];                        // damage direction (rad, 0 = ahead), seconds left
	float hurt;                                                    // red edge 0..1
	float spawn_glow;                                              // b72: cyan edge glow 0..1 after a respawn (PC footage v49 448 s)
	HudKill feed[HUD_FEED]; int nfeed;
	struct { int icon; const char *name; int active; } chip[HUD_CHIPS]; int nchip;
	int dead, killcam, spectating;                                             // killcam: the camera shows the killer (killcam.h; footage: no gauges then)
	int chr;                                                       // operator portrait in the HP ring (game order 1..12; 0 none)
	float cool_s; int skill_ready, skill_off;                      // Tier-1 cooldown seconds left; Tier 2 ready (orange ring); Tier 1 disabled (EMP, hacked, a Tier 2 running)
	int skill2;                                                    // Tier 2 ring: 0 filling, 1 ready, 2 disabled, 3 running (skills.h SkHudInfo.t2_state)
	int round; struct { int icon, alive, me; } slot[2][6]; int nslot[2];   // round modes: score_DM with player slots (skill icons), [0] allies
	float bomb_t;                                                  // Demolition: bomb timer row (seconds left, <0 hidden; shown SS:cc as in the footage)
	const char *zone_tag;                                          // Elimination: "ZONE LOCKED" under the round timer
	int no_score;                                                  // perf (b38): no top-centre score + timer (the shooting range; under the held scoreboard)
	float feed_top; int feed_rows, score_narrow;                   // ui b42: kill feed top (px; 0 = the top right), rows shown (0 = HUD_FEED_ROWS); score_TDM's plate narrowed (Terminal Conquest: between the Think Tank panels)
	// minimap: map image (texture + world->texel affine), the player, and blips (type 0 enemy, 1 enemy firing ping, 2 ally, 3/4 bomb site A/B, 5/6 site A/B with the bomb)
	GLuint mm_tex; int mm_w, mm_h; float mm_aff[6]; float px, py, pyaw;
	struct { float x, y, yaw; int type; } blip[16]; int nblip;
	struct { float x, y, a; const char *name; int enemy; } tag[8]; int ntag;   // ally name tags (screen px); b121 enemy: an enemy's, red, in the Net Jump
} HudState;
// ---- per-map minimap image (<stem>.gmm: 'GMM1', w, h, nmip, world->texel affine, DXT5 mips)
static GLuint mm_tex; static int mm_w, mm_h; static float mm_aff[6];
static int minimap_load(const char *path) {
	if (mm_tex) { glDeleteTextures(1, &mm_tex); mm_tex = 0; }
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned w, h, nm; if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "GMM1", 4) || fread(&w, 4, 1, f) != 1 || fread(&h, 4, 1, f) != 1 || fread(&nm, 4, 1, f) != 1 || fread(mm_aff, 4, 6, f) != 6) { fclose(f); return -2; }
	glGenTextures(1, &mm_tex); glBindTexture(GL_TEXTURE_2D, mm_tex);
	for (unsigned l = 0; l < nm; l++) { unsigned sz; if (fread(&sz, 4, 1, f) != 1) break; void *d = malloc(sz); if (!d || fread(d, 1, sz, f) != sz) { free(d); break; }
		glCompressedTexImage2D(GL_TEXTURE_2D, l, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w >> l, h >> l, 0, sz, d); VGL_XFER_WAIT(); free(d); }   // the upload copies asynchronously: wait before the buffer is reused
	fclose(f);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nm > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	mm_w = w; mm_h = h; return 0;
}
static struct {
	int ok;
	int hp_arc31, hp_arc32, hp_disc, hp_back, hp_d1, hp_ticks, ap_gauge, ap_gauge_r, hp_gauge, hp_gauge_r, hp_gaugeR, hp_label, hp_labelW, ap_label;
	int hp_n[3], hp_z[3], hpW_n[3], hpW_z[3], ap_n[3], ap_z[3];
	int in_fx, in_bg, in_gauge, in_gauge_r, in_a45, in_a46, in_t47, in_deco, in_cool, in_cool_r, in_skill, in_max, in_btn, in_btntxt, in_m[4], in_lo[3], in_v[4], in_t1, in_tier, in_badge, in_badgetxt, in_line;
	int sc_d1, sc_d11, sc_d301, scL[5], scR[5], scG[5], tm_colon, tmW_colon, tmM[2], tmS[2], tmWM[2], tmWS[2];
	int aim[4], aim2[4], hm[4], bs, kn, kp;
	int mg_d85, mg_d173, mg_d413, mg_gauge, mg_gauge_r, mg_gaugeW, mgB[5], mgBW[5], mgA[5], mgAW[5], mg_name, mg_anchor;
	int vin, vinn[4], screen, kr[HUD_FEED], chip_row[HUD_CHIPS], chip_icon[HUD_CHIPS];
	int n30, n30z, n26, nc, white, btn_tri, btn_dpr, btn_dpr2, btn_arm, btn_arm2;   // (b107: btn_arm - the D-pad arm's body, hud.ghud btn.arm)
	int hp_chr, hp_deco, hp_decoW, in_btn2, in_btn2txt, in_r1, in_r1back, in_r1dis, in_cc[2], in_sktxt, in_r1txt;
	int round_ok, dm_sc[3], dmL, dmR, dmG, dmT_colon, dmTW_colon, dmTM[2], dmTS[2], dmTWM[2], dmTWS[2], dm_bt[3], dmBM[2], dmBS[2], dm_emc, dm_emctxt;
	int u_ring, u_back[3], u_node[2][6][3], in_t2dis;
	int mm_back, mm_line, mm_view, mmi_player, mmi_ally, mmi_enemy, mmi_ping, mmi_site[4];
	float kr_ts, kr_is;
	int vin_n; float vin_xy[16][2], vin_uv[16][2];   // the vignette's visible part (fan, image-local) and its atlas uv                // kill feed: name size, weapon icon scale (kill-plane units; node kr.style)
} HG;
static int hg_i(const char *n) { int i = hud_find_img(n); return i; }
static int hg_n(const char *n) { int i = hud_find_node(n); return i; }
static void hg_slots(int *o, const char *pre, int n) { char b[40]; for (int i = 0; i < n; i++) { snprintf(b, sizeof(b), "%s%d", pre, i + 1); o[i] = hg_n(b); } }
static int hud_game_init(void) {
	if (!hud_ok) return -1;
	HG.hp_arc31 = hg_i("hp.arc31"); HG.hp_arc32 = hg_i("hp.arc32"); HG.hp_disc = hg_i("hp.disc"); HG.hp_back = hg_i("hp.back"); HG.hp_d1 = hg_i("hp.d1"); HG.hp_ticks = hg_i("hp.ticks");
	HG.ap_gauge = hg_i("ap.gauge"); HG.ap_gauge_r = hg_i("ap.gauge#r"); HG.hp_gauge = hg_i("hp.gauge"); HG.hp_gauge_r = hg_i("hp.gauge#r"); HG.hp_gaugeR = hg_i("hp.gaugeR");
	HG.hp_label = hg_i("hp.label"); HG.hp_labelW = hg_i("hp.labelW"); HG.ap_label = hg_i("ap.label");
	hg_slots(HG.hp_n, "hp.n", 3); hg_slots(HG.hp_z, "hp.z", 3); hg_slots(HG.hpW_n, "hpW.n", 3); hg_slots(HG.hpW_z, "hpW.z", 3); hg_slots(HG.ap_n, "ap.n", 3); hg_slots(HG.ap_z, "ap.z", 3);
	HG.hp_z[0] = HG.hpW_z[0] = HG.ap_z[0] = -1;
	HG.in_fx = hg_i("in.fx"); HG.in_bg = hg_i("in.bg"); HG.in_gauge = hg_i("in.gauge"); HG.in_gauge_r = hg_i("in.gauge#r"); HG.in_a45 = hg_i("in.a45"); HG.in_a46 = hg_i("in.a46");
	HG.in_t47 = hg_i("in.t47"); HG.in_deco = hg_i("in.deco"); HG.in_cool = hg_i("in.cool"); HG.in_cool_r = hg_i("in.cool#r"); HG.in_skill = hg_n("in.skill"); HG.in_max = hg_i("in.max");
	HG.in_btn = hg_i("in.btn"); HG.in_btntxt = hud_find_field("in.btntxt"); hg_slots(HG.in_m, "in.m", 4); hg_slots(HG.in_lo, "in.lo", 3); hg_slots(HG.in_v, "in.v", 4);
	HG.in_t1 = hg_n("in.t1"); HG.in_tier = hg_n("in.tier"); HG.in_badge = hg_n("in.badge"); HG.in_badgetxt = hg_n("in.badgetxt"); HG.in_line = hg_n("in.line");
	HG.hp_chr = hg_n("hp.chr"); HG.hp_deco = hg_i("hp.deco"); HG.hp_decoW = hg_i("hp.decoW");
	HG.in_t2dis = hg_i("in.t2dis"); HG.in_btn2 = hg_i("in.btn2"); HG.in_btn2txt = hud_find_field("in.btn2txt"); HG.in_r1 = hg_i("in.r1"); HG.in_r1back = hg_i("in.r1back"); HG.in_r1dis = hg_i("in.r1dis");
	hg_slots(HG.in_cc, "in.cc", 2); HG.in_sktxt = hud_find_field("in.sktxt"); HG.in_r1txt = hud_find_field("in.r1txt");
	HG.btn_dpr = hg_i("btn.dpr"); HG.btn_dpr2 = hg_i("btn.dpr2"); HG.btn_arm = hud_find_img("btn.arm"); HG.btn_arm2 = hud_find_img("btn.arm2"); HG.nc = hg_i("nc.0");
	// round-mode score panel (score_DM, merged from tools/dem/make_dem_hud.py)
	HG.dm_sc[0] = hg_i("dm.sc.d1"); HG.dm_sc[1] = hg_i("dm.sc.d11"); HG.dm_sc[2] = hg_i("dm.sc.d81");
	HG.dmL = hg_n("dmL.n1"); HG.dmR = hg_n("dmR.n1"); HG.dmG = hg_n("dmG.n1"); HG.dmT_colon = hg_i("dmT.colon"); HG.dmTW_colon = hg_i("dmTW.colon");
	hg_slots(HG.dmTM, "dmTM.n", 2); hg_slots(HG.dmTS, "dmTS.n", 2); hg_slots(HG.dmTWM, "dmTWM.n", 2); hg_slots(HG.dmTWS, "dmTWS.n", 2);
	HG.dm_bt[0] = hg_i("dm.bt.bg"); HG.dm_bt[1] = hg_i("dm.bt.icon"); HG.dm_bt[2] = hg_i("dm.bt.colon"); hg_slots(HG.dmBM, "dmBM.n", 2); hg_slots(HG.dmBS, "dmBS.n", 2);
	HG.dm_emc = hg_i("dm.emc"); HG.dm_emctxt = hud_find_field("dm.emctxt"); HG.u_ring = hg_i("dm.u.ring");
	{ char b[24]; for (int k = 0; k < 3; k++) { snprintf(b, 24, "dm.u.back%d", k + 1); HG.u_back[k] = hg_i(b); }
	  for (int sd = 0; sd < 2; sd++) for (int k = 0; k < 6; k++) { static const char *pt[3] = { "ring", "back", "icon" };
		for (int q = 0; q < 3; q++) { snprintf(b, 24, "dm.u%c%d.%s", sd ? 'E' : 'A', k + 1, pt[q]); HG.u_node[sd][k][q] = hg_n(b); } } }
	HG.round_ok = HG.dm_sc[0] >= 0 && HG.dmL >= 0 && HG.u_node[0][0][2] >= 0;
	HG.sc_d1 = hg_i("sc.d1"); HG.sc_d11 = hg_i("sc.d11"); HG.sc_d301 = hg_i("sc.d301"); hg_slots(HG.scL, "scL.n", 5); hg_slots(HG.scR, "scR.n", 5); hg_slots(HG.scG, "scG.n", 5);
	HG.tm_colon = hg_i("tm.colon"); HG.tmW_colon = hg_i("tmW.colon"); hg_slots(HG.tmM, "tmM.n", 2); hg_slots(HG.tmS, "tmS.n", 2); hg_slots(HG.tmWM, "tmWM.n", 2); hg_slots(HG.tmWS, "tmWS.n", 2);
	{ const char *a[4] = { "T", "B", "L", "R" }, *h[4] = { "RT", "LT", "RB", "LB" }; char b[16];
		for (int k = 0; k < 4; k++) { snprintf(b, 16, "aim.%s", a[k]); HG.aim[k] = hg_i(b); snprintf(b, 16, "aim.%s2", a[k]); HG.aim2[k] = hg_i(b); snprintf(b, 16, "hm.%s", h[k]); HG.hm[k] = hg_i(b); } }
	HG.bs = hg_i("bs"); HG.kn = hud_find_field("kn.txt"); HG.kp = hud_find_field("kp.txt");
	HG.mg_d85 = hg_i("mg.d85"); HG.mg_d173 = hg_i("mg.d173"); HG.mg_d413 = hg_i("mg.d413"); HG.mg_gauge = hg_i("mg.gauge"); HG.mg_gauge_r = hg_i("mg.gauge#r"); HG.mg_gaugeW = hg_n("mg.gaugeW");
	hg_slots(HG.mgB, "mgB.n", 5); hg_slots(HG.mgBW, "mgBW.n", 5); hg_slots(HG.mgA, "mgA.n", 5); hg_slots(HG.mgAW, "mgAW.n", 5); HG.mg_name = hud_find_field("mg.name"); HG.mg_anchor = hg_n("mg.anchor");
	HG.vin = hg_i("vin"); HG.vinn[0] = hg_n("vin.RT"); HG.vinn[1] = hg_n("vin.LT"); HG.vinn[2] = hg_n("vin.RD"); HG.vinn[3] = hg_n("vin.LD"); HG.screen = hg_n("screen");
	{ char b[16]; for (int k = 0; k < HUD_FEED; k++) { snprintf(b, 16, "kr%d", k); HG.kr[k] = hg_n(b); }
		for (int k = 0; k < HUD_CHIPS; k++) { snprintf(b, 16, "chip%d.row", k + 1); HG.chip_row[k] = hg_n(b); snprintf(b, 16, "chip%d.icon", k + 1); HG.chip_icon[k] = hg_n(b); } }
	HG.n30 = hg_i("n30.0"); HG.n30z = hg_i("n30.z"); HG.n26 = hg_i("n26.0"); HG.white = hg_i("white"); HG.btn_tri = hg_i("btn.tri");
	HG.mm_back = hg_i("mm.back"); HG.mm_line = hg_i("mm.line"); HG.mm_view = hg_n("mm.view");
	HG.mmi_player = hg_i("mmi.player"); HG.mmi_ally = hg_i("mmi.ally"); HG.mmi_enemy = hg_i("mmi.enemy"); HG.mmi_ping = hg_i("mmi.ping");
	HG.mmi_site[0] = hg_i("mmi.A"); HG.mmi_site[1] = hg_i("mmi.B"); HG.mmi_site[2] = hg_i("mmi.Ar"); HG.mmi_site[3] = hg_i("mmi.Br");
	{ int p0 = hg_n("vin.poly0"); HG.vin_n = 0;   // vignette fan (tools/ui/make_hud.py): 4 points per node, the count in vin.poly0
	  if (p0 >= 0 && HG.vin >= 0) { int n = (int)hnode[p0].H[8]; if (n > 16) n = 16; const HImg *I = &himg[HG.vin]; const float *l = I->loc;
		for (int k = 0; k < n; k++) { char b[16]; snprintf(b, 16, "vin.poly%d", k / 4); int nd = hg_n(b); if (nd < 0) { n = 0; break; } const float *H = hnode[nd].H; int j = (k % 4) * 2;
			HG.vin_xy[k][0] = H[j]; HG.vin_xy[k][1] = H[j + 1];
			HG.vin_uv[k][0] = (I->x + (H[j] - l[0]) / (l[2] - l[0]) * I->w) / htw[I->tex]; HG.vin_uv[k][1] = (I->y + (H[j + 1] - l[1]) / (l[3] - l[1]) * I->h) / hth[I->tex]; }
		HG.vin_n = n >= 3 ? n : 0; } }
	{ int st = hg_n("kr.style"); HG.kr_ts = st >= 0 ? hnode[st].H[0] : 12; HG.kr_is = st >= 0 ? hnode[st].H[1] : 0.3f; }
	HG.ok = HG.hp_gauge >= 0 && HG.n30 >= 0 && HG.n26 >= 0;
	return HG.ok ? 0 : -1;
}
// image by node of the same name ("hp.disc" in node "hp.disc")
static __attribute__((unused)) void hg_part(int im, const float *mul) { if (im >= 0) hud_img(im, hud_find_node(himg[im].name), NULL, mul, NULL, -1, 1); }
static __attribute__((unused)) int hg_nodeof(int im) { return im >= 0 ? hud_find_node(himg[im].name) : -1; }
// ui b42: an affine J (2x3: plane -> screen px) applied after A (2x3: image -> plane)
static void hg_aff(const float *J, const float *A, float *o) {
	o[0] = J[0] * A[0] + J[1] * A[3]; o[1] = J[0] * A[1] + J[1] * A[4]; o[2] = J[0] * A[2] + J[1] * A[5] + J[2];
	o[3] = J[3] * A[0] + J[4] * A[3]; o[4] = J[3] * A[1] + J[4] * A[4]; o[5] = J[3] * A[2] + J[4] * A[5] + J[5];
}
// hud_text's glyphs (left-aligned at plane x, baseline y, size ts) in the plane J, drawn in the screen node
static void hg_feed_text(const float *J, float x, float y, float ts, const float *mul, const char *s) {
	for (; *s; s++) { int c = (unsigned char)*s, g = hglyph[c];
		if (g >= 0 && HG.screen >= 0) { float A[6] = { ts, 0, x, 0, ts, y }, A2[6]; hg_aff(J, A, A2); hud_img(g, HG.screen, A2, mul, HUD_Z4, -1, 1); }
		x += hadv[c] * ts; }
}
// qa (b42 fix, marked; QA b42 claims check): a kill notify with no free spot (under the ROUND n banner, a result band, the VICTORY
// band) waits: main.c advances its clock with hud_notify_hold_dt, which stands still while it is hidden (at most 6 s), so
// "DOUBLE KILL +100" pops in when the band has gone instead of running out unseen.
static int hud_notify_waiting; static float hud_notify_held;
static float hud_notify_hold_dt(float dt) { if (hud_notify_waiting && hud_notify_held < 6.0f) { hud_notify_held += dt; return 0; } if (!hud_notify_waiting) hud_notify_held = 0; return dt; }
static int hg_nd[512], hg_cached;   // node of each image (hud_find_node by name), filled once
static void hg_cache(void) { if (!hg_cached) { for (int i = 0; i < nhimg && i < 512; i++) hg_nd[i] = hud_find_node(himg[i].name); hg_cached = 1; } }
// ---- skill gauge (bottom left), also drawn by the tutorial's CYBERBRAIN SKILL page as a demonstration (b55)
static int hg_skill_hidden;   // b61: the tutorial's movement / weapons programs show no skill gauge at all (footage; the TIER labels and the small ring were left)
static void hg_skill_draw(const HudState *s) {
	if (hg_skill_hidden) return;
	hg_cache(); int *nd_cache = hg_nd; float dx0 = hud_dx;   // (the tutorial demo draws it moved)
#define PART(im, mul) do { if ((im) >= 0) hud_img((im), nd_cache[(im)], NULL, (mul), NULL, -1, 1); } while (0)
#ifndef HG_R1_DX
#define HG_R1_DX 0.0f    // b55 (user: the bottom-left rings overlapped): the Tier-1 ring and its badge, this far right of the layout (b93: 0 - the pack places it, tucked against the big ring as the PC: tools/ui/make_hud.py PC_R1)
#endif
	// ---- skill gauge (bottom left): big ring = the operator skill (intel gauge to the next tier, orange when the tier-2
	// skill is ready), small ring = its cooldown (seconds) / "TIER 1" when usable / crossed out when blocked
	HUD_TAG("skill");
	{ PART(HG.in_fx, NULL); PART(HG.in_bg, NULL);
		float g = s->tier_hi > 0 ? (float)s->intel / s->tier_hi : 0; if (g > 1) g = 1;
		static const float GR_M[4] = { 0, 0, 0, 1 }, GR_A[4] = { 0.72f, 0.74f, 0.76f, 0 }, OR_A[4] = { 0.97f, 0.66f, 0.12f, 0 };
		static const float DK_A[4] = { 0.1f, 0.11f, 0.13f, 0 };   // footage: the ring goes dark while the Tier 2 runs
		if (s->skill2 == 3) hud_img(HG.in_gauge, nd_cache[HG.in_gauge], NULL, GR_M, DK_A, HG.in_gauge_r, 1);
		else if (s->skill_ready) hud_img(HG.in_gauge, nd_cache[HG.in_gauge], NULL, GR_M, OR_A, HG.in_gauge_r, 1);
		else if (g > 0) hud_img(HG.in_gauge, nd_cache[HG.in_gauge], NULL, GR_M, GR_A, HG.in_gauge_r, g);
		PART(HG.in_a45, NULL); PART(HG.in_a46, NULL); PART(HG.in_t47, NULL); PART(HG.in_deco, NULL);
		{ char b[16]; snprintf(b, 16, "skill.%d", s->skill_icon); int im = hud_find_img(b); float m[4] = { 1, 1, 1, 1 };
			if (s->skill2 == 2 || s->dead) { m[0] = m[1] = m[2] = 0.45f; } if (im >= 0) hud_img(im, HG.in_skill, NULL, m, HUD_Z4, -1, 1);
			if (s->skill2 == 2 && !s->dead) PART(HG.in_t2dis, NULL); }   // the 'no' mark over the skill icon (EMP, hacked)
		{ static const float TG[4] = { 0.75f, 0.77f, 0.8f, 1 }; hud_field(HG.in_sktxt, "TIER 2", TG); }   // the big ring is always the Tier-2 ring (footage)
		HUD_TAG("skill.cost"); PART(HG.in_max, NULL); hud_number(HG.in_m, 3, HG.n30, s->tier_hi, 0, NULL, NULL, NULL, -1); HUD_TAG("skill");   // Tier-2 cost ("400"; in.m4 is hidden in the movie)
		// key badges (footage: the PC key in a dark square; yellow under the big ring when Tier 2 is ready, cyan under the small one
		// when Tier 1 is): the Vita's D-pad right glyph, dimmed while not usable
		{ int t1 = s->cool < 0 && !s->skill_off && !s->dead, t2 = s->skill_ready && !s->skill_off && !s->dead; static const float CYb[4] = { 0.35f, 0.9f, 0.95f, 1 }, YLb[4] = { 1, 0.82f, 0.2f, 1 }, DIMb[4] = { 0.62f, 0.66f, 0.7f, 0.8f };   // (b107: the dim state a little brighter - the large arm must still read)
		  hud_dx = dx0 + HG_R1_DX;   // b55: the small ring and its badge sit clear of the big ring (they overlapped it)
		  // b107 (the user: the D-pad prompts "look really small"): one large D-pad arm - its dark body (btn.arm) in place of the
		  // PC's key square, the rim and triangle (btn.dpr) in the state's colour; an older hud.ghud keeps the square and its glyph
		  if (s->key) { PART(HG.in_btn, NULL); hud_field(HG.in_btntxt, s->key, NULL); }
		  else if (HG.btn_dpr >= 0 && HG.in_btntxt >= 0) { if (HG.btn_arm >= 0) hud_img(HG.btn_arm, hfield[HG.in_btntxt].node, NULL, HUD_W1, HUD_Z4, -1, 1); else PART(HG.in_btn, NULL);
			hud_img(HG.btn_dpr, hfield[HG.in_btntxt].node, NULL, t1 ? CYb : DIMb, HUD_Z4, -1, 1); }
		  else PART(HG.in_btn, NULL);
		  hud_dx = dx0;
		  if (HG.btn_dpr2 >= 0 && HG.in_btn2txt >= 0) { if (HG.btn_arm2 >= 0) hud_img(HG.btn_arm2, hfield[HG.in_btn2txt].node, NULL, HUD_W1, HUD_Z4, -1, 1); else PART(HG.in_btn2, NULL);
			hud_img(HG.btn_dpr2, hfield[HG.in_btn2txt].node, NULL, t2 ? YLb : DIMb, HUD_Z4, -1, 1); }
		  else PART(HG.in_btn2, NULL); }
		// small ring (b55: moved HG_R1_DX px right, clear of the big ring)
		hud_dx = dx0 + HG_R1_DX;
		PART(HG.in_r1back, NULL);
		if (s->skill_off) PART(HG.in_r1dis, NULL);
		else if (s->cool >= 0) {
			hud_img(HG.in_cool, nd_cache[HG.in_cool], NULL, HUD_W1, HUD_Z4, HG.in_cool_r, 1 - s->cool);
			int sec = (int)ceilf(s->cool_s); if (sec > 99) sec = 99; if (sec < 0) sec = 0;
			if (HG.nc >= 0 && HG.in_cc[0] >= 0) { hud_img(HG.nc + sec % 10, HG.in_cc[0], NULL, HUD_W1, HUD_Z4, -1, 1); hud_img(HG.nc + sec / 10, HG.in_cc[1], NULL, HUD_W1, HUD_Z4, -1, 1); }
		} else { PART(HG.in_r1, NULL); static const float TC[4] = { 0.92f, 0.95f, 1, 1 }; hud_field_fit(HG.in_r1txt, "TIER 1", TC, 29); }   // (b93: 29 - inside the smaller dark disc)
		hud_dx = dx0;
	}
#undef PART
}
// b61 (emulator b61-2: an ally's name tag over the commander box and the skill gauge's cost): a tag that would sit on a
// fixed HUD widget (radar, score, kill feed, skill gauge, chips, HP / AP, the ammo counter) or under a claimed box (commander,
// banners: hud.h claims) is not drawn there
static int hg_tag_covered(const HudState *s, float x0, float y0, float x1, float y1) {
	static const float R[][4] = { { 0, 0, 190, 170 }, { 270, 0, 690, 86 }, { 660, 0, 960, 120 }, { 830, 220, 960, 368 }, { 730, 390, 960, 544 } };
	for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++) if (x1 > R[i][0] && x0 < R[i][2] && y1 > R[i][1] && y0 < R[i][3]) return 1;
	if (!hg_skill_hidden && !s->spectating && x1 > 8 && x0 < 168 && y1 > 390 && y0 < 544) return 1;   // the skill gauge (b93: the PC-sized layout; b95 10 px higher)
	if (s->mag_x > 0 && x1 > s->mag_x - 56 && x0 < s->mag_x + 118 && y1 > s->mag_y - 28 && y0 < s->mag_y + 31) return 1;   // the ammo counter (as its banner test below)
	return hud_blocked(x0, y0, x1, y1, HUD_P_FEED);
}
static void hud_game_draw(const HudState *s, float now) {
	if (!HG.ok) return;
	static const float __attribute__((unused)) CYAN_M[4] = { 0, 0, 0, 1 }, CYAN_A[4] = { 41 / 255.f, 211 / 255.f, 225 / 255.f, 0 }, WHITE_A[4] = { 1, 1, 1, 0 };
	hg_cache(); int *nd_cache = hg_nd;
#define PART(im, mul) do { if ((im) >= 0) hud_img((im), nd_cache[(im)], NULL, (mul), NULL, -1, 1); } while (0)
	HUD_TAG("~vignette");   // ui b42: element names for the host overlap checker (tools/ui/host/overlap.py; no-op on the Vita)
	// ---- vignette, damage edge: one pass (dark corners, reddened when hurt) over the corners' visible part only (fill: the full
	// quads covered a whole screen, twice when hurt)
	// b72: a respawn lights the edges cyan for ~1.3 s (PC footage v49 448-449 s: an electric cyan glow round the frame)
	if (HG.vin >= 0) { float h = s->killcam ? 0 : fminf(1, fmaxf(0, s->hurt)), g = fminf(1, fmaxf(0, s->spawn_glow)), al = fminf(1, 0.55f + 0.75f * fmaxf(h, g)), vm[4] = { 0, 0, 0, al },
		va[4] = { (0.75f * h + 0.22f * g) / al, (0.05f * h + 0.86f * g) / al, (0.08f * h + 0.95f * g) / al, 0 };
		if (g > 0.01f && HG.white >= 0) { float A[6] = { 960, 0, 0, 0, 544, 0 }, wm[4] = { 0, 0, 0, 0.16f * g }, wa[4] = { 0.30f, 0.85f, 0.95f, 0 }; hud_img(HG.white, HG.screen, A, wm, wa, -1, 1); }
		for (int k = 0; k < 4; k++) {
			if (HG.vin_n) hud_poly_add(htex[himg[HG.vin].tex], HG.vinn[k], (const float (*)[2])HG.vin_xy, (const float (*)[2])HG.vin_uv, HG.vin_n, vm, va);
			else hud_img(HG.vin, HG.vinn[k], NULL, vm, va, -1, 1); } }
	// ---- damage direction arcs around the centre
	HUD_TAG("~dmg");
	for (int k = 0; k < HUD_DMG; k++) if (s->dmg_t[k] > 0 && HG.bs >= 0 && !s->killcam) {
		float a = s->dmg_ang[k], c = cosf(a), sn = sinf(a), A[6] = { c, -sn, 0, sn, c, 0 }, m[4] = { 1, 1, 1, fminf(1, s->dmg_t[k] / 0.6f) };
		hud_img(HG.bs, nd_cache[HG.bs], A, m, NULL, -1, 1); }
	HUD_TAG("~nametag");
	for (int k = 0; k < s->ntag; k++) { if (HG.screen < 0 || !s->tag[k].name) continue;   // ally name tags (footage: a small blue chat-bubble icon, then the name in white)
		const float ts = 20;   // Legibility rule: >= 20 px on the Vita (the original's tag is ~11 px at 720p)
		float ta = s->tag[k].a, m[4] = { 0.90f, 0.95f, 1, ta }, ic[4] = { 0.22f, 0.52f, 0.92f, ta }, w = hud_text_w(s->tag[k].name) * ts, x0 = s->tag[k].x - w / 2 + 9;
		if (hg_tag_covered(s, x0 - 22, s->tag[k].y - 18, x0 + w + 4, s->tag[k].y + 6)) continue;   // b61 (user: nothing over what it should not cover)
		if (s->tag[k].enemy) {   // b121: the Net Jump shows the enemies' names in red over their marks, a small triangle under each (EP26 7:17-7:21)
			const float rd[4] = { 1, 0.30f, 0.24f, ta }; float cx = s->tag[k].x, y = s->tag[k].y;
			hud_text(HG.screen, cx - w / 2, y, ts, 0, rd, HUD_Z4, s->tag[k].name);
			if (HG.white >= 0) for (int r = 0; r < 3; r++) { float hw = 5 - r * 2, A[6] = { hw * 2, 0, cx - hw, 0, 2, y + 5 + r * 2 }; hud_img(HG.white, HG.screen, A, rd, HUD_Z4, -1, 1); }
			continue; }
		if (HG.white >= 0) { float A[6] = { 13, 0, x0 - 18, 0, 11, s->tag[k].y - 14 }, B[6] = { 4, 0, x0 - 16, 0, 4, s->tag[k].y - 3 }; hud_img(HG.white, HG.screen, A, ic, HUD_Z4, -1, 1); hud_img(HG.white, HG.screen, B, ic, HUD_Z4, -1, 1); }
		hud_text(HG.screen, x0, s->tag[k].y, ts, 0, m, HUD_Z4, s->tag[k].name); }
	if (!s->dead) {
		// ---- crosshair (spread in Vita px) and hit marker
		HUD_TAG("~crosshair");
		if (s->ads < 0.5f) { float g = s->spread / 0.6f; const float d[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
			for (int k = 0; k < 4; k++) { int im = s->aim_red && HG.aim2[k] >= 0 ? HG.aim2[k] : HG.aim[k]; float A[6] = { 1, 0, d[k][0] * g, 0, 1, d[k][1] * g };
				if (im >= 0) hud_img(im, nd_cache[HG.aim[k]], A, NULL, NULL, -1, 1); } }
		if (s->hit_t > 0 || s->kill_t > 0) { float a = fminf(1, fmaxf(s->hit_t, s->kill_t) / 0.15f), m[4] = { 1, 1, 1, a }, rm[4] = { 0, 0, 0, a }, ra[4] = { 0.95f, 0.17f, 0.24f, 0 };
			for (int k = 0; k < 4; k++) if (HG.hm[k] >= 0) hud_img(HG.hm[k], nd_cache[HG.hm[k]], NULL, s->kill_t > 0 ? rm : m, s->kill_t > 0 ? ra : NULL, -1, 1); }
		// ---- ammo counter (follows the gun at hip, fixed below the sight when aiming)
		HUD_TAG("ammo");
		if (s->cap > 0 && HG.mg_anchor >= 0) {
			float ax, ay; hud_node_screen(HG.mg_anchor, 0, 0, &ax, &ay); hud_dx = s->mag_x - ax; hud_dy = s->mag_y - ay;
			// ui b42 (HUD de-clutter): at hip the counter follows the gun into the chips' column; a long weapon name (UMP-45S1,
			// DEATHSTALKER EVO) ran under them - the counter keeps 7 px left of the chips instead
			if (s->wname && HG.mg_name >= 0 && s->nchip > 0 && HG.chip_icon[0] >= 0) {
				static float cx0 = -1, cy0, cy1; if (cx0 < 0) { cx0 = 9999; cy0 = 9999; cy1 = -9999; int im = hud_find_img("chipi.40");
					for (int k = 0; k < HUD_CHIPS && im >= 0; k++) if (HG.chip_icon[k] >= 0) { const float *l = himg[im].loc; float x, y;
						hud_node_screen(HG.chip_icon[k], l[0], l[1], &x, &y); cx0 = fminf(cx0, x); cy0 = fminf(cy0, y); hud_node_screen(HG.chip_icon[k], l[2], l[3], &x, &y); cx0 = fminf(cx0, x); cy1 = fmaxf(cy1, y); } }
				float x0, y0, x1, y1; hud_field_box(HG.mg_name, s->wname, &x0, &y0, &x1, &y1);
				if (x1 + hud_dx > cx0 - 7 && y1 + hud_dy > cy0 - 7 && y0 + hud_dy < cy1 + 7) hud_dx -= x1 + hud_dx - (cx0 - 7); }
			// b61 (device b61-1, the range with the Mateba: "MATEBA 2006SM" ran off the right edge): the name stays on screen,
			// and the counter clear of the HP / AP ring
			if (s->wname && HG.mg_name >= 0) { float x0, y0, x1, y1; hud_field_box(HG.mg_name, s->wname, &x0, &y0, &x1, &y1);
				if (x1 + hud_dx > 950) hud_dx -= x1 + hud_dx - 950;
				if (x1 + hud_dx > 722 && s->mag_y + hud_dy - (s->mag_y - ay) + 27 > 392) hud_dx -= x1 + hud_dx - 722; }
			// ui b42: not under a round result / end-of-match band (hud.h claims; the round is over)
			{ float sx = hud_dx - (s->mag_x - ax); if (hud_blocked(s->mag_x - 52 + sx, s->mag_y - 24, s->mag_x + 114 + sx, s->mag_y + 27, HUD_P_NOTICE)) { hud_dx = hud_dy = 0; goto ammo_done; }   // (b63: NOTICE: last frame's banners too - thinktank.h's hack panel, drawn after it)
			  hud_claim(s->mag_x - 52 + sx, s->mag_y - 24, s->mag_x + 114 + sx, s->mag_y + 27, HUD_P_FEED); }   // b61: its box (thinktank.h's TARGETED line keeps clear of it)
			int low = s->mag * 10 < s->cap * 3;   // footage: red at 8 of 30
			hud_number(low ? HG.mgBW : HG.mgB, 5, HG.n26, s->mag, 0, NULL, NULL, NULL, -1);
			hud_number(HG.mgA, 5, HG.n26, s->reserve, 1, NULL, NULL, NULL, -1);
			PART(HG.mg_d173, NULL);
			if (s->mag > 0) { if (low && HG.mg_gaugeW >= 0) hud_img(HG.mg_gauge, HG.mg_gaugeW, NULL, NULL, NULL, HG.mg_gauge_r, (float)s->mag / s->cap); else hud_img(HG.mg_gauge, nd_cache[HG.mg_gauge], NULL, NULL, NULL, HG.mg_gauge_r, (float)s->mag / s->cap); }
			PART(HG.mg_d413, NULL); PART(HG.mg_d85, NULL);
			if (s->wname) hud_field(HG.mg_name, s->wname, NULL);
			hud_dx = hud_dy = 0;
		}
		ammo_done:;
	}
	HUD_TAG("hp");
	// ---- HP / AP (bottom right): portrait in a dark disc, HP gauge (cyan, red when low) and AP gauge (white) around it
	if (!s->killcam) { float hpm = s->hp_max > 0 ? s->hp_max : 100; int low = s->hp <= 0.205f * hpm; int hp = (int)(s->hp + 0.5f), ap = (int)(s->ap + 0.5f);
		PART(HG.hp_disc, NULL); PART(HG.hp_arc31, NULL); PART(HG.hp_arc32, NULL); PART(HG.hp_ticks, NULL);
		if (s->chr > 0 && HG.hp_chr >= 0) { char b[24]; snprintf(b, sizeof(b), "hp.chr.%d", s->chr); int im = hud_find_img(b); if (im >= 0) hud_img(im, HG.hp_chr, NULL, s->dead ? (const float[4]){ 0.45f, 0.45f, 0.45f, 1 } : NULL, NULL, -1, 1); }
		if (ap > 0) hud_img(HG.ap_gauge, nd_cache[HG.ap_gauge], NULL, NULL, NULL, HG.ap_gauge_r, fminf(1, ap / 100.f));   // (b83: Nano-Gel Armor lifts AP over 100)
		if (hp > 0) hud_img(low ? HG.hp_gaugeR : HG.hp_gauge, nd_cache[HG.hp_gauge], NULL, NULL, NULL, HG.hp_gauge_r, fminf(1, hp / hpm));
		if (low && HG.hp_decoW >= 0) { float pulse = 0.6f + 0.4f * sinf(now * 9); float m[4] = { 1, 1, 1, pulse }; hud_img(HG.hp_decoW, nd_cache[HG.hp_deco], NULL, m, NULL, -1, 1); }
		else PART(HG.hp_deco, NULL);
		PART(HG.hp_back, NULL); PART(HG.hp_d1, NULL);
		static const float ZD[4] = { 0.6f, 0.6f, 0.6f, 0.5f };   // leading zeros (footage: "088", "071" with dim zeros)
		if (low) { float pulse = 0.75f + 0.25f * sinf(now * 9); float m[4] = { 1, 1, 1, pulse }; PART(HG.hp_labelW, m); hud_number(HG.hpW_n, 3, HG.n30, hp, 0, NULL, NULL, NULL, -1); }
		else { PART(HG.hp_label, NULL); hud_number(HG.hp_n, 3, HG.n30, hp, 0, NULL, NULL, NULL, -1); }
		for (int i = hp >= 100 ? 3 : hp >= 10 ? 2 : 1; i < 3; i++) { const int *sl = low ? HG.hpW_n : HG.hp_n; if (sl[i] >= 0) hud_img(HG.n30, sl[i], NULL, ZD, HUD_Z4, -1, 1); }
		float apc[4] = { 1, 1, 1, 1 }; if (ap <= 0) { apc[0] = 0.75f; apc[1] = 0.2f; apc[2] = 0.25f; apc[3] = 0.85f; }
		PART(HG.ap_label, apc); hud_number(HG.ap_n, 3, HG.n30, ap, 0, apc, NULL, NULL, -1);
		for (int i = ap >= 100 ? 3 : ap >= 10 ? 2 : 1; i < 3; i++) if (HG.ap_n[i] >= 0) hud_img(HG.n30, HG.ap_n[i], NULL, ZD, HUD_Z4, -1, 1);
	}
	HUD_TAG("skill");
	if (!s->killcam && !s->spectating) hg_skill_draw(s);   // b61: none while spectating (the spectate band covers the bottom; overlap check)
	HUD_TAG("score");
	// ---- score + timer (top centre): score_TDM, or score_DM with the player slots in the round modes
	if (s->no_score) { }   // perf (b38, QA b37): hidden in the range (it covered the leave prompt) and under the held scoreboard
	else if (s->round && HG.round_ok) {
		int t = s->time_s < 0 ? 0 : s->time_s, w = s->time_warn;
		for (int k = 0; k < 3; k++) PART(HG.dm_sc[k], NULL);
		static const float SL[4] = { 1, 1, 1, 1 }, SR_[4] = { 0.98f, 0.33f, 0.35f, 1 }, SG[4] = { 0.62f, 0.64f, 0.66f, 0.85f };
		{ char b[8]; int v; v = s->scoreL % 10; hud_img(HG.n30 + v, HG.dmL, NULL, SL, HUD_Z4, -1, 1); v = s->scoreR % 10; hud_img(HG.n30 + v, HG.dmR, NULL, SR_, HUD_Z4, -1, 1); v = s->goal % 10; hud_img(HG.n30 + v, HG.dmG, NULL, SG, HUD_Z4, -1, 1); (void)b; }
		PART(w ? HG.dmTW_colon : HG.dmT_colon, NULL);
		const int *M_ = w ? HG.dmTWM : HG.dmTM, *S_ = w ? HG.dmTWS : HG.dmTS;
		hud_img(HG.n30 + (t / 60) % 10, M_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t / 600) % 10, M_[1], NULL, NULL, NULL, -1, 1);
		hud_img(HG.n30 + (t % 60) % 10, S_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t % 60) / 10, S_[1], NULL, NULL, NULL, -1, 1);
		if (s->bomb_t >= 0) { for (int k = 0; k < 3; k++) PART(HG.dm_bt[k], NULL);   // footage (HQ): "41:05" = seconds : hundredths, red
			int sec = (int)s->bomb_t, cs = (int)((s->bomb_t - sec) * 100); if (sec > 99) sec = 99; if (cs > 99) cs = 99;
			hud_img(HG.n30 + sec % 10, HG.dmBM[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + sec / 10, HG.dmBM[1], NULL, NULL, NULL, -1, 1);
			hud_img(HG.n30 + cs % 10, HG.dmBS[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + cs / 10, HG.dmBS[1], NULL, NULL, NULL, -1, 1); }
		// slots: allies left of the timer (the local player nearest it, ringed), enemies right; the fallen are removed
		static const float SC_[2][4] = { { 0.32f, 0.88f, 0.95f, 1 }, { 0.98f, 0.32f, 0.35f, 1 } }, SB[4] = { 1, 1, 1, 0.9f };
		for (int sd = 0; sd < 2; sd++) for (int k = 0; k < s->nslot[sd] && k < 6; k++) {
			const int *nd = HG.u_node[sd][k]; if (nd[2] < 0) continue;
			if (s->slot[sd][k].me) { if (HG.u_ring >= 0) hud_img(HG.u_ring, nd[0], NULL, s->slot[sd][k].alive ? HUD_W1 : (const float[4]){ 0.85f, 0.88f, 0.9f, 0.9f }, NULL, -1, 1); }
			if (!s->slot[sd][k].alive) continue;
			if (HG.u_back[0] >= 0) hud_img(HG.u_back[0], nd[1], NULL, SB, NULL, -1, 1);
			char b[16]; snprintf(b, 16, "skill.%d", s->slot[sd][k].icon); int im = hud_find_img(b);
			if (im >= 0) hud_img(im, nd[2], NULL, (const float[4]){ 0, 0, 0, 1 }, (const float[4]){ SC_[sd][0], SC_[sd][1], SC_[sd][2], 0 }, -1, 1);
		}
		if (s->zone_tag && HG.dm_emctxt >= 0) { float a = 0.8f + 0.2f * sinf(now * 5), R[4] = { 1, 1, 1, a };
			if (HG.dm_emc >= 0) { const float *l = himg[HG.dm_emc].loc; float k = 0.62f, cx = (l[0] + l[2]) / 2, A[6] = { k, 0, cx * (1 - k), 0, 1, 0 }; hud_img(HG.dm_emc, nd_cache[HG.dm_emc], A, R, NULL, -1, 1); }   // ui b42: 62 % wide (its ends ran under the kill feed)
			static const float RT[4] = { 0.95f, 0.22f, 0.25f, 1 }; hud_field(HG.dm_emctxt, s->zone_tag, RT); }
	} else {
		if (s->score_narrow && HG.sc_d1 >= 0) { const float *l = himg[HG.sc_d1].loc; float k = 0.76f, cx = (l[0] + l[2]) / 2, A[6] = { k, 0, cx * (1 - k), 0, 1, 0 };   // ui b42: between
			hud_img(HG.sc_d1, nd_cache[HG.sc_d1], A, NULL, NULL, -1, 1); } else PART(HG.sc_d1, NULL);                                                     // the Think Tank panels
		PART(HG.sc_d11, NULL); PART(HG.sc_d301, NULL);
		hud_number(HG.scL, 5, HG.n30, s->scoreL, 0, NULL, NULL, NULL, -1); hud_number(HG.scR, 5, HG.n30, s->scoreR, 1, NULL, NULL, NULL, -1);
		hud_number(HG.scG, 5, HG.n30, s->goal, 0, NULL, NULL, NULL, -1);
		int t = s->time_s < 0 ? 0 : s->time_s, w = s->time_warn;
		PART(w ? HG.tmW_colon : HG.tm_colon, NULL);
		const int *M_ = w ? HG.tmWM : HG.tmM, *S_ = w ? HG.tmWS : HG.tmS;
		hud_img(HG.n30 + (t / 60) % 10, M_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t / 600) % 10, M_[1], NULL, NULL, NULL, -1, 1);
		hud_img(HG.n30 + (t % 60) % 10, S_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t % 60) / 10, S_[1], NULL, NULL, NULL, -1, 1);
	}
	// ---- kill feed (top right): killer, weapon, victim. ui b42 (HUD de-clutter, user report b41): smaller names (HUD_FEED_EM, were
	// ~30 px) on even rows - every row in the plane of the middle row (kr2; the movie's rows fanned out) moved down the screen by
	// HUD_FEED_PITCH; the newest HUD_FEED_ROWS; beside the score panel a long row is narrowed (down to 20 px names) to stay clear of
	// it (score_DM's enemy slots); under the Think Tank panels (HudState.feed_top, main.c) it starts lower; a row a banner or notice
	// covers (hud.h claims) is hidden meanwhile
	if (s->nfeed > 0 && HG.kr[2] >= 0) {
		// the middle row's plane flattened to its affine at the rows' right end (J: plane units -> screen px), so a name keeps its
		// size along the row (the perspective made the left end's names ~12 % smaller: under 20 px)
		const int nd = HG.kr[2]; float ax, ay, bx, by, cx, cy; hud_node_screen(nd, -6, 0, &ax, &ay); hud_node_screen(nd, -106, 0, &bx, &by); hud_node_screen(nd, -6, 100, &cx, &cy);
		float J[6] = { (ax - bx) / 100, (cx - ax) / 100, 0, (ay - by) / 100, (cy - ay) / 100, 0 }; J[2] = ax + 6 * J[0]; J[5] = ay + 6 * J[3];
		float pxu = hypotf(J[1], J[4]), top = s->feed_top > 0 ? s->feed_top : 8;   // screen px per plane unit along the glyphs' height
		float plate_x1 = s->round ? 666 : s->score_narrow ? 641 : 642, plate_y1 = s->no_score ? -99 : s->score_narrow ? 146 : 80;   // the score panel's right edge + gap, its bottom (Terminal Conquest: ttm.h's bar under it, the overtime percentages beside it)
		int rows = s->feed_rows > 0 ? s->feed_rows : HUD_FEED_ROWS, k0 = s->nfeed > rows ? s->nfeed - rows : 0;
		for (int k = k0; k < s->nfeed && k < HUD_FEED; k++) {
			const HudKill *f = &s->feed[k]; int r = k - k0; float y0 = top + r * HUD_FEED_PITCH;
			char b[16]; snprintf(b, 16, "wp.%d", f->weapon); int im = hud_find_img(b); const float *l = im >= 0 ? himg[im].loc : NULL;
			float em = HUD_FEED_EM, ts = 0, is = 0, w = 0, lim = y0 < plate_y1 + 6 ? plate_x1 : 0; char kn[24]; snprintf(kn, sizeof(kn), "%s", f->killer);
			for (int pass = 0; pass < 2; pass++) {   // the row's width (plane units) at em; narrowed once beside the score panel
				ts = em / pxu; is = HG.kr_is * ts / HG.kr_ts * 0.85f;
				w = (hud_text_w(f->victim) + hud_text_w(kn) + 0.6f) * ts + (l ? is * (l[2] - l[0]) + 0.3f * ts : 0);
				float lx = ax - w * J[0];
				if (pass || lx >= lim || em <= 20) break;
				em = fmaxf(20, em * (ax - lim) / (ax - lx)); }
			for (int kl = (int)strlen(kn); kl > 3 && ax - w * J[0] < lim; ) {   // still too long at 20 px: the killer's name cut ("CHIEF ARAM..")
				kn[--kl] = 0; kn[kl - 1] = '.'; kn[kl - 2] = '.'; w = (hud_text_w(f->victim) + hud_text_w(kn) + 0.6f) * ts + (l ? is * (l[2] - l[0]) + 0.3f * ts : 0); }
			if (hud_blocked(ax - w * J[0], y0, ax, y0 + em + fmaxf(0, -w * J[3]) + 2, HUD_P_FEED)) continue;   // (the row's tilt: its left end is lower)
			HUD_TAG(r == 0 ? "feed0" : r == 1 ? "feed1" : r == 2 ? "feed2" : r == 3 ? "feed3" : "feed4");
			float a = f->t < 0.25f ? f->t / 0.25f : 1; float kc[4] = { f->kc[0], f->kc[1], f->kc[2], f->kc[3] * a }, vc[4] = { f->vc[0], f->vc[1], f->vc[2], f->vc[3] * a };
			// right-aligned (the row plane's x = 0 is the screen edge): victim, the killer's weapon pointing at it, killer; the ink top at y0
			float base = hasc * ts, capc = base - 0.33f * ts, x = -6, gap = 0.3f * ts;
			hud_dx = 0; hud_dy = y0 - ay - (hasc - 0.72f) * em;
			hg_feed_text(J, x - hud_text_w(f->victim) * ts, base, ts, vc, f->victim); x -= hud_text_w(f->victim) * ts + gap;
			if (l) { float m[4] = { 0, 0, 0, a }, A[6] = { -is, 0, x + is * l[0], 0, is, capc - is * (l[1] + l[3]) / 2 }, A2[6]; hg_aff(J, A, A2); hud_img(im, HG.screen, A2, m, WHITE_A, -1, 1); x -= is * (l[2] - l[0]) + gap; }
			hg_feed_text(J, x - hud_text_w(kn) * ts, base, ts, kc, kn);
			hud_dy = 0;
		}
	}
	HUD_TAG("chips");
	// ---- chips: hexagon icons above the HP ring, full colour while in effect, dim otherwise
	for (int k = 0; k < s->nchip && k < HUD_CHIPS && !s->killcam; k++) {
		char b[16]; snprintf(b, 16, "chipi.%d", s->chip[k].icon); int im = hud_find_img(b); if (im < 0) continue;
		static const float DIM[4] = { 0.55f, 0.55f, 0.5f, 0.4f }; hud_img(im, HG.chip_icon[k], NULL, s->chip[k].active ? HUD_W1 : DIM, HUD_Z4, -1, 1);
	}
	HUD_TAG("minimap");
	// ---- minimap (top left): the map turns with the player (facing up), clipped to the tilted viewport
	if (HG.mm_view >= 0) {
		PART(HG.mm_back, NULL);
		const float VW_ = 204, VH_ = 170, CX_ = 102, CY_ = 85, K_ = 0.034f;  // viewport (plane units), player spot, plane units per cm (footage: Urban EM 0.032, Ghost Storage 0.036)
		float fx = cosf(s->pyaw), fy = sinf(s->pyaw), rxv = fy, ryv = -fx;   // forward / right in the world
		if (s->mm_tex) {
			const float *A = s->mm_aff; float det = A[0] * A[4] - A[1] * A[3]; if (fabsf(det) < 1e-12f) det = 1e-12f;
			float P[16][2], U[16][2]; int n = 4; const float cu[4] = { 0, (float)s->mm_w, (float)s->mm_w, 0 }, cv[4] = { 0, 0, (float)s->mm_h, (float)s->mm_h };
			for (int k = 0; k < 4; k++) { float du = cu[k] - A[2], dv = cv[k] - A[5]; float wx = (A[4] * du - A[1] * dv) / det, wy = (-A[3] * du + A[0] * dv) / det;
				float dx = wx - s->px, dy = wy - s->py; P[k][0] = CX_ + K_ * (dx * rxv + dy * ryv); P[k][1] = CY_ - K_ * (dx * fx + dy * fy); U[k][0] = cu[k] / s->mm_w; U[k][1] = cv[k] / s->mm_h; }
			// clip against the viewport rectangle (Sutherland-Hodgman, four edges)
			for (int e = 0; e < 4 && n >= 3; e++) {
				float Q[16][2], V[16][2]; int m = 0; int ax = e & 1; float lim = e == 0 ? 0 : e == 1 ? 0 : e == 2 ? VW_ : VH_; int keep_ge = e < 2;
				if (e == 0) { ax = 0; } else if (e == 1) { ax = 1; } else if (e == 2) { ax = 0; } else ax = 1;
				for (int i = 0; i < n && m < 15; i++) { int j = (i + 1) % n; float a = P[i][ax] - lim, b = P[j][ax] - lim; if (!keep_ge) { a = -a; b = -b; }
					if (a >= 0) { Q[m][0] = P[i][0]; Q[m][1] = P[i][1]; V[m][0] = U[i][0]; V[m][1] = U[i][1]; m++; }
					if ((a >= 0) != (b >= 0) && m < 15) { float t = a / (a - b); Q[m][0] = P[i][0] + (P[j][0] - P[i][0]) * t; Q[m][1] = P[i][1] + (P[j][1] - P[i][1]) * t;
						V[m][0] = U[i][0] + (U[j][0] - U[i][0]) * t; V[m][1] = U[i][1] + (U[j][1] - U[i][1]) * t; m++; } }
				n = m; memcpy(P, Q, sizeof(float) * 2 * m); memcpy(U, V, sizeof(float) * 2 * m);
			}
			static const float MMUL[4] = { 2.1f, 1.8f, 1.7f, 0.92f };   // the game shows the radar image brightened (footage, Urban EM: fill ~ 64,77,89)
			if (n >= 3) hud_poly(s->mm_tex, HG.mm_view, (const float (*)[2])P, (const float (*)[2])U, n, MMUL);
		}
		for (int k = 0; k < s->nblip; k++) {
			float dx = s->blip[k].x - s->px, dy = s->blip[k].y - s->py, lx = CX_ + K_ * (dx * rxv + dy * ryv), ly = CY_ - K_ * (dx * fx + dy * fy);
			if (lx < 6 || lx > VW_ - 6 || ly < 6 || ly > VH_ - 6) continue;
			int ty = s->blip[k].type, im = ty >= 3 && ty <= 6 ? HG.mmi_site[ty - 3] : ty == 2 ? HG.mmi_ally : ty == 1 ? HG.mmi_ping : HG.mmi_enemy; if (im < 0) continue;
			float th = -(s->blip[k].yaw - s->pyaw), c = cosf(th) * 0.38f, sn = sinf(th) * 0.38f; if (ty == 1) { c = 0.32f; sn = 0; } if (ty >= 3) { c = 0.42f; sn = 0; }
			float A2[6] = { c, -sn, lx, sn, c, ly }; hud_img(im, HG.mm_view, A2, HUD_W1, HUD_Z4, -1, 1);
		}
		if (HG.mmi_player >= 0) { float A2[6] = { 0.85f, 0, CX_, 0, 0.85f, CY_ }; hud_img(HG.mmi_player, HG.mm_view, A2, HUD_W1, HUD_Z4, -1, 1); }
		PART(HG.mm_line, NULL);
	}
	// ---- kill notify ("NICE KILL" +100): pops in, holds, fades
	hud_notify_waiting = 0;   // qa (b42 fix, marked): set below when the notify has no free spot this frame
	HUD_TAG("notify");
	if (s->notify && s->notify_t >= 0 && s->notify_t < 2.4f) {
		float t = s->notify_t, a = t < 0.12f ? t / 0.12f : t > 1.9f ? (2.4f - t) / 0.5f : 1, m[4] = { 1, 1, 1, a };
		// ui b42 (HUD de-clutter, user report b41: DOUBLE KILL +100 over TIER 2 SKILL READY, over the round result band): placed clear
		// of the banners, notices and skill notices (hud.h claims) - just above or below them - else not shown while they are
		char b[16]; snprintf(b, 16, "+%d", s->notify_pts); float x0, y0, x1, y1, px0, py0, px1, py1;
		if (HG.kn >= 0) { hud_field_box(HG.kn, s->notify, &x0, &y0, &x1, &y1);
			if (s->notify_pts && HG.kp >= 0) { hud_field_box(HG.kp, b, &px0, &py0, &px1, &py1); x0 = fminf(x0, px0); y0 = fminf(y0, py0); x1 = fmaxf(x1, px1); y1 = fmaxf(y1, py1); }
			float d = hud_place(x0, y0, x1, y1, HUD_P_NOTIFY, 88, 262);
			if (d < HUD_HIDE) { hud_dy = d; hud_field(HG.kn, s->notify, m); if (s->notify_pts) hud_field(HG.kp, b, m); hud_dy = 0; if (a > 0.02f) hud_claim(x0, y0 + d, x1, y1 + d, HUD_P_NOTIFY); } else hud_notify_waiting = 1; }
	}
	HUD_TAG("?");
#undef PART
}
