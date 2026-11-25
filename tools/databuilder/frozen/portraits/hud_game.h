// In-match HUD: the original widgets (see hud.h) driven by the game state. Layout and look follow the
// gameplay footage; tools/ui/hud_demo.py draws the same thing offline for checking.
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
typedef struct { char killer[24], victim[24]; float kc[4], vc[4]; int weapon; float t; } HudKill;
typedef struct {
	float hp, ap;                         // 0..100
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
	HudKill feed[HUD_FEED]; int nfeed;
	struct { int icon; const char *name; int active; } chip[HUD_CHIPS]; int nchip;
	int dead;
	// minimap: map image (texture + world->texel affine), the player, and blips (type 0 enemy, 1 enemy firing ping, 2 ally, 3/4 bomb site A/B, 5/6 site A/B with the bomb)
	GLuint mm_tex; int mm_w, mm_h; float mm_aff[6]; float px, py, pyaw;
	struct { float x, y, yaw; int type; } blip[16]; int nblip;
	struct { float x, y, a; const char *name; } tag[8]; int ntag;   // ally name tags (screen px)
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
	int n30, n30z, n26, white, btn_tri;
	int mm_back, mm_line, mm_view, mmi_player, mmi_ally, mmi_enemy, mmi_ping, mmi_site[4];
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
	HG.ok = HG.hp_gauge >= 0 && HG.n30 >= 0 && HG.n26 >= 0;
	return HG.ok ? 0 : -1;
}
// image by node of the same name ("hp.disc" in node "hp.disc")
static void hg_part(int im, const float *mul) { if (im >= 0) hud_img(im, hud_find_node(himg[im].name), NULL, mul, NULL, -1, 1); }
static int hg_nodeof(int im) { return im >= 0 ? hud_find_node(himg[im].name) : -1; }
static void hud_game_draw(const HudState *s, float now) {
	if (!HG.ok) return;
	static const float CYAN_M[4] = { 0, 0, 0, 1 }, CYAN_A[4] = { 41 / 255.f, 211 / 255.f, 225 / 255.f, 0 }, WHITE_A[4] = { 1, 1, 1, 0 };
	static int nd_cache[512]; static int cached = 0;
	if (!cached) { for (int i = 0; i < nhimg && i < 512; i++) nd_cache[i] = hud_find_node(himg[i].name); cached = 1; }
#define PART(im, mul) do { if ((im) >= 0) hud_img((im), nd_cache[(im)], NULL, (mul), NULL, -1, 1); } while (0)
	// ---- vignette, damage edge
	if (HG.vin >= 0) { float vm[4] = { 1, 1, 1, 0.55f };
		for (int k = 0; k < 4; k++) hud_img(HG.vin, HG.vinn[k], NULL, vm, NULL, -1, 1);
		if (s->hurt > 0.02f) { float rm[4] = { 0, 0, 0, fminf(1, s->hurt) }, ra[4] = { 0.75f, 0.05f, 0.08f, 0 };
			for (int k = 0; k < 4; k++) hud_img(HG.vin, HG.vinn[k], NULL, rm, ra, -1, 1); } }
	// ---- damage direction arcs around the centre
	for (int k = 0; k < HUD_DMG; k++) if (s->dmg_t[k] > 0 && HG.bs >= 0) {
		float a = s->dmg_ang[k], c = cosf(a), sn = sinf(a), A[6] = { c, -sn, 0, sn, c, 0 }, m[4] = { 1, 1, 1, fminf(1, s->dmg_t[k] / 0.6f) };
		hud_img(HG.bs, nd_cache[HG.bs], A, m, NULL, -1, 1); }
	for (int k = 0; k < s->ntag; k++) { float m[4] = { 0.45f, 0.95f, 0.5f, s->tag[k].a }; if (HG.screen >= 0 && s->tag[k].name) hud_text(HG.screen, s->tag[k].x, s->tag[k].y, 11, 2, m, HUD_Z4, s->tag[k].name); }
	if (!s->dead) {
		// ---- crosshair (spread in Vita px) and hit marker
		if (s->ads < 0.5f) { float g = s->spread / 0.6f; const float d[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };
			for (int k = 0; k < 4; k++) { int im = s->aim_red && HG.aim2[k] >= 0 ? HG.aim2[k] : HG.aim[k]; float A[6] = { 1, 0, d[k][0] * g, 0, 1, d[k][1] * g };
				if (im >= 0) hud_img(im, nd_cache[HG.aim[k]], A, NULL, NULL, -1, 1); } }
		if (s->hit_t > 0 || s->kill_t > 0) { float a = fminf(1, fmaxf(s->hit_t, s->kill_t) / 0.15f), m[4] = { 1, 1, 1, a }, rm[4] = { 0, 0, 0, a }, ra[4] = { 0.95f, 0.17f, 0.24f, 0 };
			for (int k = 0; k < 4; k++) if (HG.hm[k] >= 0) hud_img(HG.hm[k], nd_cache[HG.hm[k]], NULL, s->kill_t > 0 ? rm : m, s->kill_t > 0 ? ra : NULL, -1, 1); }
		// ---- ammo counter (follows the gun at hip, fixed below the sight when aiming)
		if (s->cap > 0 && HG.mg_anchor >= 0) {
			float ax, ay; hud_node_screen(HG.mg_anchor, 0, 0, &ax, &ay); hud_dx = s->mag_x - ax; hud_dy = s->mag_y - ay;
			int low = s->mag * 10 < s->cap * 3;   // footage: red at 8 of 30
			hud_number(low ? HG.mgBW : HG.mgB, 5, HG.n26, s->mag, 0, NULL, NULL, NULL, -1);
			hud_number(HG.mgA, 5, HG.n26, s->reserve, 1, NULL, NULL, NULL, -1);
			PART(HG.mg_d173, NULL);
			if (s->mag > 0) { if (low && HG.mg_gaugeW >= 0) hud_img(HG.mg_gauge, HG.mg_gaugeW, NULL, NULL, NULL, HG.mg_gauge_r, (float)s->mag / s->cap); else hud_img(HG.mg_gauge, nd_cache[HG.mg_gauge], NULL, NULL, NULL, HG.mg_gauge_r, (float)s->mag / s->cap); }
			PART(HG.mg_d413, NULL); PART(HG.mg_d85, NULL);
			if (s->wname) hud_field(HG.mg_name, s->wname, NULL);
			hud_dx = hud_dy = 0;
		}
	}
	// ---- HP / AP (bottom right)
	{ int low = s->hp <= 20.5f; int hp = (int)(s->hp + 0.5f), ap = (int)(s->ap + 0.5f);
		PART(HG.hp_arc31, NULL); PART(HG.hp_arc32, NULL); PART(HG.hp_disc, NULL); PART(HG.hp_back, NULL); PART(HG.hp_d1, NULL); PART(HG.hp_ticks, NULL);
		if (ap > 0) hud_img(HG.ap_gauge, nd_cache[HG.ap_gauge], NULL, NULL, NULL, HG.ap_gauge_r, ap / 100.f);
		if (hp > 0) hud_img(low ? HG.hp_gaugeR : HG.hp_gauge, nd_cache[HG.hp_gauge], NULL, NULL, NULL, HG.hp_gauge_r, hp / 100.f);
		if (low) { float pulse = 0.75f + 0.25f * sinf(now * 9); float m[4] = { 1, 1, 1, pulse }; PART(HG.hp_labelW, m); hud_number(HG.hpW_n, 3, HG.n30, hp, 0, NULL, NULL, HG.hpW_z, HG.n30z); }
		else { PART(HG.hp_label, NULL); hud_number(HG.hp_n, 3, HG.n30, hp, 0, NULL, NULL, HG.hp_z, HG.n30z); }
		float apc[4] = { 0.55f, 0.55f, 0.55f, 0.6f }; if (ap <= 0) { apc[0] = 0.6f; apc[1] = 0.1f; apc[2] = 0.15f; apc[3] = 0.5f; }
		PART(HG.ap_label, apc); hud_number(HG.ap_n, 3, HG.n30, ap, 0, apc, NULL, HG.ap_z, HG.n30z);
	}
	// ---- skill / intel gauge (bottom left)
	{ PART(HG.in_fx, NULL); PART(HG.in_bg, NULL);
		float g = s->tier_hi > 0 ? (float)s->intel / s->tier_hi : 0; if (g > 1) g = 1;
		if (g > 0) hud_img(HG.in_gauge, nd_cache[HG.in_gauge], NULL, CYAN_M, CYAN_A, HG.in_gauge_r, g);
		PART(HG.in_a45, NULL); PART(HG.in_a46, NULL); PART(HG.in_t47, NULL); PART(HG.in_deco, NULL);
		if (s->cool >= 0) hud_img(HG.in_cool, nd_cache[HG.in_cool], NULL, HUD_W1, HUD_Z4, HG.in_cool_r, s->cool);
		{ char b[16]; snprintf(b, 16, "skill.%d", s->skill_icon); int im = hud_find_img(b); float m[4] = { 1, 1, 1, 1 };
			if (s->cool >= 0) { m[0] = m[1] = m[2] = 0.55f; } if (im >= 0) hud_img(im, HG.in_skill, NULL, m, HUD_Z4, -1, 1); }
		PART(HG.in_max, NULL); hud_number(HG.in_m, 4, HG.n30, s->tier_hi, 0, NULL, NULL, NULL, -1);
		PART(HG.in_btn, NULL); if (s->key) hud_field(HG.in_btntxt, s->key, NULL);
		else if (HG.btn_tri >= 0 && HG.in_btntxt >= 0) { static const float TC[4] = { 0.36f, 0.86f, 0.62f, 1 }; hud_img(HG.btn_tri, hfield[HG.in_btntxt].node, NULL, TC, HUD_Z4, -1, 1); }
		static const float GREY[4] = { 0.62f, 0.62f, 0.62f, 0.8f }, DARK[4] = { 0.05f, 0.1f, 0.15f, 1 };
		char t[16]; snprintf(t, 16, "TIER %d", s->tier); hud_text(HG.in_t1, 0, 0, 1, 2, GREY, HUD_Z4, t);
		hud_number(HG.in_lo, 3, HG.n30, s->tier_lo, 0, GREY, HUD_Z4, NULL, -1);
		hud_text(HG.in_tier, 0, 0, 1, 2, HUD_W1, HUD_Z4, "TIER");
		if (HG.in_btn >= 0 && HG.in_badge >= 0) hud_img(HG.in_btn, HG.in_badge, NULL, CYAN_M, CYAN_A, -1, 1);
		snprintf(t, 16, "%d", s->tier); hud_text(HG.in_badgetxt, 0, 0, 1, 2, DARK, HUD_Z4, t);
		hud_number(HG.in_v, 4, HG.n30, s->intel, 0, NULL, NULL, NULL, -1);
		{ static const float LM[4] = { 0.7f, 0.7f, 0.7f, 0.7f }; if (HG.white >= 0 && HG.in_line >= 0) hud_img(HG.white, HG.in_line, NULL, LM, HUD_Z4, -1, 1); }
	}
	// ---- score + timer (top centre)
	{ PART(HG.sc_d1, NULL); PART(HG.sc_d11, NULL); PART(HG.sc_d301, NULL);
		hud_number(HG.scL, 5, HG.n30, s->scoreL, 0, NULL, NULL, NULL, -1); hud_number(HG.scR, 5, HG.n30, s->scoreR, 1, NULL, NULL, NULL, -1);
		hud_number(HG.scG, 5, HG.n30, s->goal, 0, NULL, NULL, NULL, -1);
		int t = s->time_s < 0 ? 0 : s->time_s, w = s->time_warn;
		PART(w ? HG.tmW_colon : HG.tm_colon, NULL);
		const int *M_ = w ? HG.tmWM : HG.tmM, *S_ = w ? HG.tmWS : HG.tmS;
		hud_img(HG.n30 + (t / 60) % 10, M_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t / 600) % 10, M_[1], NULL, NULL, NULL, -1, 1);
		hud_img(HG.n30 + (t % 60) % 10, S_[0], NULL, NULL, NULL, -1, 1); hud_img(HG.n30 + (t % 60) / 10, S_[1], NULL, NULL, NULL, -1, 1);
	}
	// ---- kill feed (top right): killer, weapon, victim
	for (int k = 0; k < s->nfeed && k < HUD_FEED; k++) {
		const HudKill *f = &s->feed[k]; int nd = HG.kr[k]; if (nd < 0) continue;
		float a = f->t < 0.25f ? f->t / 0.25f : 1; float kc[4] = { f->kc[0], f->kc[1], f->kc[2], f->kc[3] * a }, vc[4] = { f->vc[0], f->vc[1], f->vc[2], f->vc[3] * a };
		float base = 8 + hasc * 12;
		hud_text(nd, -190, base, 12, 1, kc, HUD_Z4, f->killer);
		{ char b[16]; snprintf(b, 16, "wp.%d", f->weapon); int im = hud_find_img(b); float A[6] = { -0.3f, 0, -122, 0, 0.3f, 4 }, m[4] = { 0, 0, 0, a };
			if (im >= 0) hud_img(im, nd, A, m, WHITE_A, -1, 1); }
		hud_text(nd, -117, base, 12, 0, vc, HUD_Z4, f->victim);
	}
	// ---- chips (passive skills), lit while in effect
	for (int k = 0; k < s->nchip && k < HUD_CHIPS; k++) {
		int act = s->chip[k].active; float tm[4] = { 1, 1, 1, act ? 1.f : 0.45f }, ic[4] = { 0.7f, 0.7f, 0.7f, 0.45f }; if (!act) { tm[0] = tm[1] = tm[2] = 0.75f; }
		hud_text(HG.chip_row[k], 0, 0, 15, 1, tm, HUD_Z4, s->chip[k].name);
		char b[16]; snprintf(b, 16, "chipi.%d", s->chip[k].icon); int im = hud_find_img(b);
		if (im >= 0) hud_img(im, HG.chip_icon[k], NULL, act ? HUD_W1 : ic, HUD_Z4, -1, 1);
	}
	// ---- minimap (top left): the map turns with the player (facing up), clipped to the tilted viewport
	if (HG.mm_view >= 0) {
		PART(HG.mm_back, NULL);
		const float VW_ = 204, VH_ = 170, CX_ = 102, CY_ = 85, K_ = 0.07f;   // viewport (plane units), player spot, plane units per cm
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
			static const float MMUL[4] = { 3.2f, 3.0f, 2.6f, 0.92f };   // the game shows the radar image brightened (footage)
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
	if (s->notify && s->notify_t >= 0 && s->notify_t < 2.4f) {
		float t = s->notify_t, a = t < 0.12f ? t / 0.12f : t > 1.9f ? (2.4f - t) / 0.5f : 1, m[4] = { 1, 1, 1, a };
		hud_field(HG.kn, s->notify, m);
		if (s->notify_pts) { char b[16]; snprintf(b, 16, "+%d", s->notify_pts); hud_field(HG.kp, b, m); }
	}
#undef PART
}
