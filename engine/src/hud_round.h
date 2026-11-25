// Screens of the round-based modes (Elimination, Demolition) in the original's layout (InGameUI.swf: modeAlMC round
// banner, roundRe result / switching-sides bands, noti1txt notices, gmtimeAL timer warning, cdMC countdown, watch
// spectator line, score_DM 'SAFE ZONE' tag). Parts come from hud.ghud (rr.*, ch.*, rh.*) when it has them, else from
// the overlay pack (ovl.ghud: sp.*, es.*, nt.*, tw). Layout from the Elimination / Demolition footage (720p ->
// Vita: x 0.75, y 0.7556).
#pragma once
#include "hud.h"
#define RH_HUD_PACK 0   // hud.ghud is the first pack loaded (main.c)
static float rh_smooth(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
static float rh_fade(float t, float in, float hold, float out) { if (t < 0) return 0; if (t < in) return t / in; t -= in; if (t < hold) return 1; t -= hold; return t < out ? 1 - t / out : 0; }
static __attribute__((unused)) int rh_has(const char *img) { return hud_npack > RH_HUD_PACK && hud_find_img(img) >= 0; }   // (call with the hud pack current)
// a solid rectangle in screen pixels with the pack's 'white' part
static void rh_rect(float x0, float y0, float x1, float y1, const float *c) {
	int wi = hud_find_img("white"), nd = hud_find_node("screen"); if (wi < 0 || nd < 0) return; const float *l = himg[wi].loc;
	float A[6] = { (x1 - x0) / (l[2] - l[0]), 0, x0 - l[0] * (x1 - x0) / (l[2] - l[0]), 0, (y1 - y0) / (l[3] - l[1]), y0 - l[1] * (y1 - y0) / (l[3] - l[1]) };
	hud_img(wi, nd, A, c, HUD_Z4, -1, 1);
}
// a horizontal band that fades out towards both ends (the original's soft gradient bars), screen pixels
static void rh_band(float cx, float y0, float y1, float half_w, const float *c) {
	for (int k = 0; k < 8; k++) { float a0 = half_w * k / 8, a1 = half_w * (k + 1) / 8, f = k < 5 ? 1 : 1 - (k - 4) / 4.f; float cc[4] = { c[0] * f, c[1] * f, c[2] * f, c[3] * f };
		rh_rect(cx + a0, y0, cx + a1, y1, cc); rh_rect(cx - a1, y0, cx - a0, y1, cc); }
}
static void rh_text(float x, float y, float size, int align, const float *c, const char *s) { int nd = hud_find_node("screen"); if (nd >= 0) hud_text(nd, x, y, size, align, c, HUD_Z4, s); }
// ui b42 (HUD de-clutter): the wide bands drawn narrower (k: x scale about the image's centre) so they stay in the centre column
// (~192..768 on screen: clear of the minimap and the kill feed); the movie's band reached the minimap and the screen edges
static void rh_img_k(int im, int nd, float k, const float *mul) {
	if (im < 0 || nd < 0) return; const float *l = himg[im].loc; float cx = (l[0] + l[2]) / 2, A[6] = { k, 0, cx * (1 - k), 0, 1, 0 }; hud_img(im, nd, A, mul, NULL, -1, 1);
}
#define RH_BAND_K 0.75f   // rr.band: visible 100..865 -> ~193..766
// legibility floor for the overlay-pack texts the round screens use (the ovl pack belongs to the menus: whatever size it ships
// with, these come out at least this tall on the Vita; a no-op once the pack itself is big enough). Single-line, centred fields.
static void rh_legible_ovl(void) {
	static int done; if (done || menu_ovl < 0 || menu_ovl >= hud_npack) return; done = 1;
	static const struct { const char *f; float px; } FL[] = { { "nt.txt", 24 }, { "sp.round", 26 }, { "sp.rmission", 22 }, { "sp.mission", 22 }, { "sp.mode", 34 }, { "tw.title", 32 }, { "tw.sub", 22 } };
	HudPack *P = &hud_pack[menu_ovl];
	for (unsigned k = 0; k < sizeof(FL) / sizeof(FL[0]); k++) for (int i = 0; i < P->nfield; i++) if (!strcmp(P->field[i].name, FL[k].f)) {
		HField *F = &P->field[i]; const float *H = P->node[F->node].H; float x = F->b[0], y = F->b[2];
		float w0 = H[6] * x + H[7] * y + H[8], w1 = H[6] * x + H[7] * (y + F->size) + H[8];
		float px = fabsf((H[3] * x + H[4] * (y + F->size) + H[5]) / w1 - (H[3] * x + H[4] * y + H[5]) / w0);
		if (px > 0.5f && px < FL[k].px) { float s0 = F->size; F->size *= FL[k].px / px; float d = (P->asc + P->desc) * (F->size - s0) / 2; F->b[2] -= d; F->b[3] += d; }
	}
}
// ---- in the hud pack (call between hud_begin / hud_end with hud.ghud current)
// red "ZONE LOCKED" tag under the round timer (score_DM's safe-zone label; footage: centre 640,83 at 720p). hud_game.h draws it
// with the movie's own part (HudState.zone_tag); this plain version is the fallback for a pack without it. 24 px (legibility).
static void rh_zone_tag(const char *txt, float now) {
	HUD_TAG("zone_tag");   // ui b42 (overlap checker)
	static const float BG[4] = { 0.18f, 0.02f, 0.03f, 0.55f }; float a = 0.85f + 0.15f * sinf(now * 5), R[4] = { 0.95f * a, 0.22f * a, 0.25f * a, a };
	float w = hud_text_w(txt) * 24 + 16; rh_rect(480 - w / 2, 80, 480 + w / 2, 108, BG); rh_text(480, 102, 24, 2, R, txt);
}
// countdown digit under the notice line (cdMC: white, footage centre 640,231; 40 px here: a heading on the Vita)
static void rh_countdown(int n, int red) {
	HUD_TAG("countdown");   // ui b42 (overlap checker)
	char b[16]; snprintf(b, sizeof(b), "%d", n); float W[4] = { 1, 1, 1, 1 }, Rd[4] = { 1, 0.86f, 0.25f, 1 }, Sh[4] = { 0, 0, 0, 0.6f };
	rh_text(481.5f, 201.5f, 40, 2, Sh, b); rh_text(480, 200, 40, 2, red ? Rd : W, b);
	hud_claim(460, 168, 500, 204, HUD_P_NOTICE);   // ui b42 (hud.h claims)
}
// spectator line at the bottom (watch: the watched ally's name in yellow, stats under it; the previous / next ally beside the
// buttons that switch - footage: "MMvX1 <mouse>  Veronica08  <mouse> TuPapiRico19"; on the Vita the L / R shoulder buttons).
// 24 px name, 20 px stats and neighbours (legibility); prev / next NULL: no neighbour shown
static void rh_lr_glyph(float cx, float cy, int right, const float *col) {   // the Vita shoulder button (btn.lsh, mirrored for R) + its letter
	int im = hud_find_img("btn.lsh"), nd = hud_find_node("screen"); float m[4] = { 0.8f, 0.84f, 0.88f, col[3] * 0.9f };
	if (im >= 0 && nd >= 0) { const float *l = himg[im].loc; float sx = 44 / (l[2] - l[0]) * (right ? -1 : 1), sy = 28 / (l[3] - l[1]);
		float A[6] = { sx, 0, cx - sx * (l[0] + l[2]) / 2, 0, sy, cy - sy * (l[1] + l[3]) / 2 }; hud_img(im, nd, A, m, HUD_Z4, -1, 1);
		rh_text(cx + (right ? -2 : 2), cy + 8, 22, 2, col, right ? "R" : "L"); }
	else rh_text(cx, cy + 8, 24, 2, col, right ? "R >" : "< L");
}
static void rh_spectate4(const char *name, const char *stats, const char *prev, const char *next) {
	HUD_TAG("spectate");   // ui b42 (overlap checker)
	static const float BG[4] = { 0, 0.02f, 0.04f, 0.62f }, Y[4] = { 1, 0.86f, 0.25f, 1 }, W[4] = { 0.92f, 0.95f, 1, 0.95f }, G[4] = { 0.7f, 0.75f, 0.8f, 0.9f };
	rh_band(480, 476, 542, 266, BG);   // ui b42: +-266 (was 330: its left end ran over the skill gauge's buttons)
	if (name) { rh_text(480, 501, 24, 2, Y, name);
		rh_lr_glyph(318, 504, 0, G); rh_lr_glyph(642, 504, 1, G);
		if (prev) rh_text(290, 512, 20, 1, W, prev); if (next) rh_text(670, 512, 20, 0, W, next); }
	if (stats) rh_text(480, 530, 20, 2, W, stats);
}
static void rh_spectate(const char *name, const char *stats) { rh_spectate4(name, stats, NULL, NULL); }
// ---- full screens (switch packs themselves; call outside hud_begin / hud_end)
// round banner: emblem + ROUND n + objective (modeAlMC); round 1 shows the mode name first. hud_emb: an emblem of the hud pack
// (footage: Elimination shows the team's emblem, rr.emb.emA; Demolition the side's), NULL = the overlay pack's mode icon
static void rh_round_banner(int mode, int round, const char *mission, float t, int mode_first, const char *hud_emb) {
	HUD_TAG("round_banner");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0 || t > 6) return; rh_legible_ovl();
	float a = rh_fade(t, 0.3f, 4.8f, 0.6f), s = 1.15f - 0.15f * rh_smooth(t / 0.35f), m[4] = { 1, 1, 1, a };
	if (hud_blocked(314, 94, 646, 222, HUD_P_BANNER)) return;   // ui b42: not over a round result / end band (hud.h claims)
	// ui b44 (QA test C on b42): a soft band + drop shadow under the white mode name / ROUND n / objective (menu.h mu_title_band),
	// one band for all the lines this banner shows, faded with whichever shows; the claim grows to hold it
	const char *mname = mode == MODE_EM ? "ELIMINATION" : MENU_MODE_NAME[mode < 0 || mode >= MODE_COUNT ? 0 : mode];
	char r[24]; snprintf(r, sizeof(r), "ROUND %d", round); float t2 = mode_first ? t - 2.4f : t, a2 = !mode_first ? a : t2 < 0 ? 0 : rh_fade(t2, 0.3f, 2.6f, 0.6f);
	float cr[4] = { 314, 94, 646, 222 };
	int done = 0;
	if (hud_emb && !(mode_first && t < 2.4f)) { menu_begin(RH_HUD_PACK); int im = hud_find_img(hud_emb), nd = hud_find_node("sp.emb");   // (the mode's name comes with the mode's icon: footage, Downtown round 1)
		if (im >= 0 && nd >= 0) { const float *l = himg[im].loc; float cx = (l[0] + l[2]) / 2, cy = (l[1] + l[3]) / 2; float A[6] = { s, 0, cx * (1 - s), 0, s, cy * (1 - s) }; hud_img(im, nd, A, m, NULL, -1, 1); done = 1; }
		menu_end(); }
	menu_begin(menu_ovl);
	{ float u[4] = { 1, 1, 0, 0 }, tb[4];
	  if (mode_first && mu_fld_text_box("sp.mode", mname, 0, tb)) mu_box_union(u, tb);
	  if (mu_fld_text_box("sp.round", r, 0, tb)) mu_box_union(u, tb);
	  if (mission) { hud_dy = 9; if (mu_fld_text_box("sp.rmission", mission, 0, tb)) mu_box_union(u, tb); hud_dy = 0; }
	  if (u[0] < u[2]) { float br[4]; mu_title_band(u, fmaxf(a, a2), br); cr[0] = fminf(cr[0], br[0]); cr[2] = fmaxf(cr[2], br[2]); cr[3] = fmaxf(cr[3], br[3]); } }
	if (fmaxf(a, a2) > 0.02f) hud_claim(cr[0], cr[1], cr[2], cr[3], HUD_P_BANNER);
	char b[32]; snprintf(b, sizeof(b), "sp.mode.%s", mode >= 0 && mode < MODE_COUNT ? MENU_MODE_ICON[mode] : "tdm"); int im = done ? -1 : MI(b);
	if (im >= 0) { const float *l = himg[im].loc; float cx = (l[0] + l[2]) / 2, cy = (l[1] + l[3]) / 2; float A[6] = { s, 0, cx * (1 - s), 0, s, cy * (1 - s) };
		hud_dy = 2; hud_img(im, MN("sp.emblem"), A, m, NULL, -1, 1); hud_dy = 0; }   // ui b44: 2 px lower (DEMOLITION's emblem came 5 px under score_DM's slot row)
	if (mode_first && t < 2.4f) mt_shadowed("sp.mode", mname, m);
	else { float m2[4] = { 1, 1, 1, a2 }; mt_shadowed("sp.round", r, m2); hud_dy = 9; mt_shadowed("sp.rmission", mission, m2); hud_dy = 0; }   // (a little gap under the bigger ROUND n)
	menu_end();
}
// timer warning (gmtimeAL: alarm icon, title, subtitle), t = seconds since shown (~5 s); dy: moved down by this many pixels
// (clear of the ZONE LOCKED tag / the bomb row under the timer)
static void rh_timewarn(const char *title, const char *sub, float t, float dy) {
	HUD_TAG("rh_timewarn");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0 || t > 5.2f) return; rh_legible_ovl();
	menu_begin(menu_ovl); if (!mtw_ok) { mg_init(&MTW, "tw"); mtw_ok = 1; }
	float m[4] = { 1, 1, 1, rh_fade(t, 0.2f, 4.2f, 0.6f) }, oy = hud_dy;
	// ui b42: 6 px under the score panel (the alarm icon touched it); clear of a banner (hud.h claims), else hidden meanwhile
	float d = hud_place(216, 90 + dy, 750, 193 + dy, HUD_P_NOTICE, 88, 330); if (d >= HUD_HIDE) { menu_end(); return; }
	hud_dy = oy + dy + 6 + d; mg_draw(&MTW, m); MT("tw.title", title, m); MT("tw.sub", sub, m); hud_dy = oy;
	if (m[3] > 0.02f) hud_claim(216, 90 + dy + d, 750, 193 + dy + d, HUD_P_NOTICE);
	menu_end();
}
// round result band (roundRe.resultMC): ROUND n, VICTORY / DEFEAT / DRAW, reason, both emblems VS and the scores
// side: -1 no sides (Elimination), 0 the player's team defends, 1 it attacks (Demolition: emblems + DEFENSE / ATTACK labels)
#define RH_EMB_ATT "rr.emb.attE"   // Demolition's side emblems as the footage shows them for both teams: the red attack tower,
#define RH_EMB_DEF "rr.emb.defA"   // the blue defense hexagon (the movie's other two colourings are unused)
static const char *rh_lbl_a, *rh_lbl_e;   // b63 (Ghost Assault): the side names under the emblems without Demolition's sides (CYBORG / GHOST)
static void rh_result_band(int round, int res, const char *reason, int sl, int sr, float t, int side) {
	HUD_TAG("result_band");   // ui b42 (overlap checker)
	if (t < 0) return;
	static const char *TT[3] = { "VICTORY", "DEFEAT", "DRAW" };
	static const float TC[3][3] = { { 1, 1, 1 }, { 0.98f, 0.24f, 0.27f }, { 0.85f, 0.85f, 0.85f } };
	if (res < 0 || res > 2) res = 2;
	float a = rh_smooth(t / 0.3f) * (t > 3.9f ? fmaxf(0, 1 - (t - 3.9f) / 0.4f) : 1);
	menu_begin(RH_HUD_PACK);
	int rr = hud_find_node("rr.round") >= 0;
	hud_dy = 5;   // ui b42: 5 px lower, clear of Demolition's bomb row under the timer (was 3 px)
	if (rr) {   // the original parts (hud.ghud built with tools/ui/make_hud.py --round)
		float m[4] = { 1, 1, 1, a }, Y[4] = { 0.98f, 0.92f, 0.22f, a }, Ti[4] = { TC[res][0], TC[res][1], TC[res][2], a };
		int bi = hud_find_img("rr.band"), bn = hud_find_node("rr.bg"); rh_img_k(bi, bn, RH_BAND_K, m);   // ui b42: narrowed (was over the minimap)
		if (a > 0.02f) hud_claim(192, 115, 768, 475, HUD_P_RESULT);
		char r[24]; snprintf(r, sizeof(r), "ROUND %d", round); hud_field(hud_find_field("rr.round"), r, m);
		hud_field(hud_find_field("rr.title"), TT[res], Ti); hud_field(hud_find_field("rr.sub"), reason, Y);
		float e = rh_smooth((t - 0.2f) / 0.3f), me[4] = { 1, 1, 1, a * e };
		// Demolition: each side's own emblem - the attack tower red (rr.emb.attE), the defense hexagon blue (rr.emb.defA) - the enemy's
		// tinted rose and darker (footage: Train Station, Downtown, HQ); Elimination: the two team emblems
		int ea = hud_find_img(side == 1 ? RH_EMB_ATT : side == 0 ? RH_EMB_DEF : "rr.emb.emA"), ee = hud_find_img(side == 0 ? RH_EMB_ATT : side == 1 ? RH_EMB_DEF : "rr.emb.emE");
		float mt[4] = { 0.86f, 0.6f, 0.6f, a * e }; const float *mE = side >= 0 ? mt : me;
		for (const char *p = "rr.d7\0rr.d11\0rr.d53\0"; *p; p += strlen(p) + 1) { int im = hud_find_img(p); if (im >= 0) hud_img(im, hud_find_node(p), NULL, me, NULL, -1, 1); }   // (plates under the labels)
		if (ea >= 0) hud_img(ea, hud_find_node("rr.campa"), NULL, me, NULL, -1, 1); if (ee >= 0) hud_img(ee, hud_find_node("rr.campb"), NULL, mE, NULL, -1, 1);
		if (side >= 0) { float tw[4] = { 1, 1, 1, a * e }, tr[4] = { 0.98f, 0.36f, 0.38f, a * e };   // footage: the player's side label white, the enemy's red
			hud_field(hud_find_field("rr.typeA"), side ? "ATTACK" : "DEFENSE", tw); hud_field(hud_find_field("rr.typeE"), side ? "DEFENSE" : "ATTACK", tr); }
		else if (rh_lbl_a && rh_lbl_e) { float tw[4] = { 1, 1, 1, a * e }, tr[4] = { 0.98f, 0.36f, 0.38f, a * e }; hud_field(hud_find_field("rr.typeA"), rh_lbl_a, tw); hud_field(hud_find_field("rr.typeE"), rh_lbl_e, tr); }
		int g0 = hud_find_img("n30.0"); float cl[4] = { 0.35f, 0.88f, 0.95f, a * e }, cr[4] = { 0.98f, 0.3f, 0.32f, a * e };
		if (g0 >= 0) { int na = hud_find_node("rrA.n1"), ne = hud_find_node("rrE.n1"); if (na >= 0) hud_img(g0 + sl % 10, na, NULL, cl, HUD_Z4, -1, 1); if (ne >= 0) hud_img(g0 + sr % 10, ne, NULL, cr, HUD_Z4, -1, 1); }
	} else {    // fallback: a band drawn with plain parts
		float bg[4] = { 0, 0.01f, 0.02f, 0.62f * a }, m[4] = { 1, 1, 1, a }, Y[4] = { 0.98f, 0.92f, 0.22f, a }, Ti[4] = { TC[res][0], TC[res][1], TC[res][2], a };
		rh_band(480, 138, 215, 260, bg); char r[24]; snprintf(r, sizeof(r), "ROUND %d", round);
		rh_text(480, 155, 24, 2, m, r); rh_text(480, 190, 36, 2, Ti, TT[res]); rh_text(480, 214, 24, 2, Y, reason);
		char sc[16]; snprintf(sc, sizeof(sc), "%d", sl); float cl[4] = { 0.35f, 0.88f, 0.95f, a }, cr[4] = { 0.98f, 0.3f, 0.32f, a };
		rh_text(394, 370, 20, 2, cl, sc); snprintf(sc, sizeof(sc), "%d", sr); rh_text(568, 370, 20, 2, cr, sc); rh_text(480, 285, 14, 2, m, "VS");
	}
	hud_dy = 0; menu_end();
}
// switching sides band (roundRe.changeMC): ROUND n, SWITCHING SIDES, objective; side >= 0 (Demolition): the emblem of the side the
// player's team takes now (0 defense, 1 attack) under the band with its name - the old side's emblem first, turning over into
// the new one (footage: Train Station round 6, the ATTACK tower turns into the DEFENSE hexagon)
static void rh_switch_band(int round, const char *mission, float t, int side) {
	HUD_TAG("switch_band");   // ui b42 (overlap checker)
	if (t < 0) return;
	float a = rh_smooth(t / 0.3f) * (t > 7.6f ? fmaxf(0, 1 - (t - 7.6f) / 0.5f) : 1), m[4] = { 1, 1, 1, a }, Y[4] = { 0.98f, 0.92f, 0.22f, a };
	menu_begin(RH_HUD_PACK);
	char r[24]; snprintf(r, sizeof(r), "ROUND %d", round);
	if (hud_find_node("ch.title") >= 0) {
		int bi = hud_find_img("rr.band"), bn = hud_find_node("ch.bg"); rh_img_k(bi, bn, RH_BAND_K, m);   // ui b42: narrowed (was over the minimap)
		if (a > 0.02f) hud_claim(192, 110, 768, side >= 0 ? 350 : 230, HUD_P_RESULT);
		hud_field(hud_find_field("ch.round"), r, m); hud_field(hud_find_field("ch.title"), "SWITCHING SIDES", m); hud_field(hud_find_field("ch.sub"), mission, Y);
		int en = hud_find_node("ch.emb");
		if (side >= 0 && en >= 0) {
			float p = rh_smooth((t - 0.7f) / 0.6f); int neu = p >= 0.5f, im = hud_find_img(neu == !!side ? RH_EMB_ATT : RH_EMB_DEF);
			if (im >= 0) { const float *l = himg[im].loc; float sx = fmaxf(0.02f, fabsf(cosf(3.14159265f * p))), cx = (l[0] + l[2]) / 2, A[6] = { sx, 0, cx * (1 - sx), 0, 1, 0 }, me[4] = { 1, 1, 1, a * rh_smooth((t - 0.15f) / 0.3f) };
				hud_img(im, en, A, me, NULL, -1, 1);
				// the side's name right under the emblem (footage: ~30 px below it; the movie's field sits lower)
				float ex, ey; hud_node_screen(en, cx, l[3], &ex, &ey); float ta[4] = { 1, 1, 1, a * rh_smooth((t - 1.2f) / 0.3f) };
				rh_text(480, ey + 34, 28, 2, ta, side ? "ATTACK" : "DEFENSE"); } }
	} else { float bg[4] = { 0, 0.01f, 0.02f, 0.62f * a }; rh_band(480, 132, 222, 300, bg); rh_text(480, 155, 24, 2, m, r); rh_text(480, 190, 36, 2, m, "SWITCHING SIDES"); rh_text(480, 214, 24, 2, Y, mission); }
	menu_end();
}
// ---- Demolition parts (in the hud pack, between hud_begin / hud_end)
// plant / defuse bar (bombset): title, the bar filling with the progress, HOLD + the button. prog < 0 hidden, 0 = a target is in
// reach (the prompt), > 0 planting / defusing. Sizes: the movie's, scaled for legibility by tools/ui/make_hud.py.
static void rh_plant_bar(float prog, int defuse, float now) {
	HUD_TAG("plant_bar");   // ui b42 (overlap checker)
	if (prog < 0) return;
	int bg = hud_find_img("dm.set.bg"), gb = hud_find_img("dm.set.gauge"), bar = hud_find_img("dm.set.bar"), key = hud_find_img("dm.set.key"), sq = hud_find_img("btn.sq");
	if (bg < 0 || bar < 0) return;
	static const float W[4] = { 1, 1, 1, 1 }, DK[4] = { 0, 0, 0, 0.55f };
	hud_img(bg, hud_find_node("dm.set.bg"), NULL, NULL, NULL, -1, 1);
	if (gb >= 0) hud_img(gb, hud_find_node("dm.set.gauge"), NULL, DK, HUD_Z4, -1, 1);
	if (prog > 0) { const float *l = himg[bar].loc; float p = prog > 1 ? 1 : prog, A[6] = { p, 0, l[0] * (1 - p), 0, 1, 0 }; hud_img(bar, hud_find_node("dm.set.bar"), A, NULL, NULL, -1, 1); }
	hud_field(hud_find_field("dm.set.txt"), prog > 0 ? (defuse ? "Defusing" : "Planting") : (defuse ? "Defuse Bomb" : "Plant Bomb"), W);
	hud_field(hud_find_field("dm.set.hold"), "HOLD", W);
	(void)key;   // ui agent: no PC key-cap frame behind the Vita Square button (BRIEF.md Vita prompts rule)
	if (sq >= 0) { float a = prog > 0 ? 1 : 0.75f + 0.25f * sinf(now * 6), m[4] = { 1, 1, 1, a }; hud_img(sq, hud_find_node("dm.set.keytxt"), NULL, m, HUD_Z4, -1, 1); }
}
// in-world bomb-site marker (InGameUI_D bomb): the circle with the site letter in the attack / defense colours (the bomb icon
// once it is planted there), the distance under it; dir >= 0: the marker sits on the screen edge, the arrow points (rad, 0 = up)
static void rh_site_marker(float sx, float sy, int site, int attack, int bomb, int dist_m, float dir, float now) {
	HUD_TAG("~site_marker");   // ui b42 (overlap checker)
	int an = hud_find_node("wm.anchor"); if (an < 0) return;
	char b[24]; snprintf(b, sizeof(b), "wm.%s%s%c", attack ? "atk" : "def", bomb ? "Bomb" : "", site ? 'B' : 'A'); int im = hud_find_img(b); if (im < 0) return;
	float ax, ay; hud_node_screen(an, 0, 0, &ax, &ay); float ox = hud_dx, oy = hud_dy; hud_dx = sx - ax; hud_dy = sy - ay;
	float m[4] = { 1, 1, 1, bomb ? 0.7f + 0.3f * sinf(now * 8) : 1 };
	hud_img(im, hud_find_node("wm.icon"), NULL, m, NULL, -1, 1);
	if (dir >= 0) { int ar = hud_find_img(bomb ? "wm.arrowR" : "wm.arrowW"), nd = hud_find_node("wm.arrow"), sn = hud_find_node("screen");
		if (ar >= 0 && nd >= 0 && sn >= 0) {   // the movie's arrow (above the circle, pointing up) turned about the circle centre, in screen pixels
			const float *l = himg[ar].loc, *L = himg[im].loc; float ac = (l[0] + l[2]) / 2, ab = (l[1] + l[3]) / 2, ix, iy, px, py, qx, qy;
			hud_node_screen(hud_find_node("wm.icon"), (L[0] + L[2]) / 2, (L[1] + L[3]) / 2, &ix, &iy); hud_node_screen(nd, ac, ab, &px, &py); hud_node_screen(nd, l[2], ab, &qx, &qy);
			float k = sqrtf((qx - px) * (qx - px) + (qy - py) * (qy - py)) / ((l[2] - l[0]) / 2 + 1e-3f), r = sqrtf((px - ix) * (px - ix) + (py - iy) * (py - iy)) + 6;
			float c = cosf(dir), s = sinf(dir), ox2 = ix + hud_dx + r * s, oy2 = iy + hud_dy - r * c;
			float A[6] = { k * c, -k * s, ox2 - k * (c * ac - s * ab), k * s, k * c, oy2 - k * (s * ac + c * ab) };
			float dx0 = hud_dx, dy0 = hud_dy; hud_dx = hud_dy = 0; hud_img(ar, sn, A, NULL, NULL, -1, 1); hud_dx = dx0; hud_dy = dy0; } }
	if (dist_m >= 0) { char t[16]; snprintf(t, sizeof(t), "%dm", dist_m); hud_field(hud_find_field("wm.dist"), t, NULL); }
	hud_dx = ox; hud_dy = oy;
}
// ---- in-match scoreboard (the original's TAB board: mode, objective, map with the players, both teams' tables), re-flowed for
// the Vita: 24 px rows, the columns that matter offline (operator, name, score, kills, deaths). Hud pack current.
typedef struct { char name[24]; int portrait, team, kills, deaths, score, alive, me; } RhRow;
typedef struct { GLuint tex; int w, h; float aff[6]; struct { float x, y, yaw; int type; } dot[13]; int ndot; } RhMap;   // radar image + dots (type 0 me, 1 ally, 2 enemy)
static void rh_portrait(int portrait, float x, float y, float size, const float *mul) {   // hp.chr.N in a size x size box at (x, y)
	char b[16]; snprintf(b, sizeof(b), "hp.chr.%d", portrait); int im = hud_find_img(b), nd = hud_find_node("screen"); if (im < 0 || nd < 0) return;
	const float *l = himg[im].loc; float s = size / fmaxf(l[2] - l[0], l[3] - l[1]), A[6] = { s, 0, x + size / 2 - s * (l[0] + l[2]) / 2, 0, s, y + size / 2 - s * (l[1] + l[3]) / 2 };
	hud_img(im, nd, A, mul, HUD_Z4, -1, 1);
}
// perf (b38, QA b37): the board's header names the match type (main.c sets it when a match starts; it said QUICK MATCH for a
// custom match too) and shows the time left (main.c, from the HUD timer, which the board covers and is hidden while it is up; -1 none)
static const char *rh_sb_type = "BOT MATCH"; static int rh_sb_time = -1;
static void rh_scoreboard(const char *mode, const char *goal, const char *map, const RhRow *rows, int n, int scoreA, int scoreE, const RhMap *M, float a) {
	HUD_TAG("#scoreboard");   // ui b42 (overlap checker)
	if (a <= 0.01f) return;
	const float BG[4] = { 0.015f * a, 0.03f * a, 0.045f * a, 0.84f * a }, LN[4] = { 0.55f * a, 0.62f * a, 0.68f * a, 0.7f * a }, HB[4] = { 0.1f * a, 0.12f * a, 0.14f * a, 0.9f * a };
	const float Y[4] = { 0.98f, 0.87f, 0.2f, a }, W[4] = { 0.95f, 0.97f, 1, a }, G[4] = { 0.62f, 0.66f, 0.7f, a }, TL[4] = { 0.45f, 0.85f, 0.55f, a };
	const float CY[4] = { 0.36f, 0.88f, 0.95f, a }, RD[4] = { 0.98f, 0.33f, 0.35f, a }, ME[4] = { 0.08f * a, 0.38f * a, 0.62f * a, 0.85f * a };
	rh_rect(30, 60, 930, 512, BG); rh_rect(30, 60, 930, 62, LN); rh_rect(30, 510, 930, 512, LN);
	if (rh_sb_time >= 0) { char tb[12]; snprintf(tb, sizeof(tb), "%d:%02d", rh_sb_time / 60 % 100, rh_sb_time % 60);   // perf (b38): type left, time right
		rh_rect(352, 60, 608, 86, HB); rh_text(364, 80, 20, 0, G, rh_sb_type); rh_text(596, 81, 24, 1, W, tb); }
	else { rh_rect(390, 60, 570, 86, HB); rh_text(480, 80, 20, 2, G, rh_sb_type); }
	// left: mode, objective, map name, the map with the players
	{ float ms_ = fminf(34, 290 / fmaxf(hud_text_w(mode), 0.01f)); rh_text(52, 120, ms_, 0, Y, mode); }   // qa (b45 fix, marked): the mode name fits 52..342 (TEAM DEATHMATCH at 34 px ran into the team score at 360)
	rh_text(52, 148, 20, 0, W, goal);
	rh_text(52, 186, 22, 0, TL, map); rh_rect(52, 193, 330, 194, LN);
	if (M && M->tex) {
		int nd = hud_find_node("screen"); const float X0 = 66, Y0 = 204, S = 250;
		float P[4][2] = { { X0, Y0 }, { X0 + S, Y0 }, { X0 + S, Y0 + S }, { X0, Y0 + S } }, U[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		float mm[4] = { 2.1f * a, 1.8f * a, 1.7f * a, 0.92f * a }; hud_poly(M->tex, nd, (const float (*)[2])P, (const float (*)[2])U, 4, mm);
		static const char *IC[3] = { "mmi.player", "mmi.ally", "mmi.enemy" };
		for (int k = 0; k < M->ndot; k++) { int ty = M->dot[k].type < 0 || M->dot[k].type > 2 ? 2 : M->dot[k].type, im = hud_find_img(IC[ty]); if (im < 0) continue;
			const float *f = M->aff; float u = f[0] * M->dot[k].x + f[1] * M->dot[k].y + f[2], v = f[3] * M->dot[k].x + f[4] * M->dot[k].y + f[5];
			float px = X0 + u / M->w * S, py = Y0 + v / M->h * S; if (px < X0 || px > X0 + S || py < Y0 || py > Y0 + S) continue;
			float th = 1.5707963f - M->dot[k].yaw, c = cosf(th) * 0.42f, s = sinf(th) * 0.42f, A2[6] = { c, -s, px, s, c, py }, m[4] = { 1, 1, 1, a };   // (radar v grows south: the icon points along the yaw)
			hud_img(im, nd, A2, m, HUD_Z4, -1, 1); }
	}
	// right: the two teams (the player's first), columns: operator, name, score, kills, deaths
	for (int sd = 0; sd < 2; sd++) {
		float y = 98 + sd * 210; const float *TC = sd ? RD : CY; char b[16];
		snprintf(b, sizeof(b), "%d", sd ? scoreE : scoreA); rh_text(360, y + 8, 32, 0, TC, b); rh_text(396, y + 4, 24, 0, TC, rh_lbl_a && rh_lbl_e ? (sd ? rh_lbl_e : rh_lbl_a) : sd ? "GHOST A" : "GHOST B");   // (b63 Ghost Assault: CYBORG / GHOST)
		rh_text(452, y + 32, 20, 0, G, "Nickname"); rh_text(728, y + 32, 20, 1, G, "Score"); rh_text(818, y + 32, 20, 1, G, "Kills"); rh_text(908, y + 32, 20, 1, G, "Deaths");
		rh_rect(352, y + 38, 912, y + 39, LN);
		int r = 0;
		for (int i = 0; i < n && r < 5; i++) if (rows[i].team == sd) {
			float ry = y + 42 + r * 32; r++;
			if (rows[i].me) rh_rect(352, ry, 912, ry + 30, ME);
			const float dim[4] = { a * 0.45f, a * 0.45f, a * 0.45f, a }, *NC = rows[i].alive ? (rows[i].me ? W : TC) : G;
			rh_portrait(rows[i].portrait, 360, ry + 1, 28, rows[i].alive ? NULL : dim);
			rh_text(402, ry + 24, 24, 0, NC, rows[i].name);
			snprintf(b, sizeof(b), "%d", rows[i].score); rh_text(728, ry + 24, 24, 1, W, b);
			snprintf(b, sizeof(b), "%d", rows[i].kills); rh_text(818, ry + 24, 24, 1, W, b);
			snprintf(b, sizeof(b), "%d", rows[i].deaths); rh_text(908, ry + 24, 24, 1, W, b);
		}
	}
}
// in-match pause button for the touch screen (the original has none: Esc on the PC): a small dark disc with the pause bars,
// drawn at the right edge of the s x s touch target at (x0, y0); brighter while pressed. Hud pack current.
static void rh_pause_button(float x0, float y0, float s, int pressed) {
	HUD_TAG("pause_btn");   // ui b42 (overlap checker)
	int d = hud_find_img("hp.disc"), nd = hud_find_node("screen"); float cx = x0 + s / 2 + 8, cy = y0 + s / 2, r = 21;
	float a = pressed ? 0.95f : 0.6f, m[4] = { 1, 1, 1, a }, W[4] = { 0.92f, 0.96f, 1, a };
	if (d >= 0 && nd >= 0) { const float *l = himg[d].loc; float k = 2 * r / (l[2] - l[0]), A[6] = { k, 0, cx - k * (l[0] + l[2]) / 2, 0, k, cy - k * (l[1] + l[3]) / 2 }; hud_img(d, nd, A, m, NULL, -1, 1); }
	rh_rect(cx - 7, cy - 9, cx - 3, cy + 9, W); rh_rect(cx + 3, cy - 9, cx + 7, cy + 9, W);
}
