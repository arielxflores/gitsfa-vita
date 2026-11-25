// Front-end screens for menu.h (included from it): title, lobby, PLAY cards, quick match (mode cards), training programs,
// custom match (+ map picker), operatives, armory, options (CONTROLS / GRAPHICS / GAME / SOUND), exit confirmation.
// Layout follows the original's menus (top bar with the yellow location tab and the red DIVE badge, dark teal windows with
// light grey selected tabs, label / value rows with arrows), sized for the Vita's 5" screen (the legibility rule: labels >= 24 px,
// nothing under 20 px, touch targets >= 72 px tall and >= 12 px apart; tools/menu/check_legibility.py checks the host captures).
// Touch: a tap selects a card / list entry, a second tap (or the confirm button) activates it; buttons, lobby tiles, tabs and
// arrows act on one tap; sliders follow the finger; long lists scroll with a swipe. The d-pad / sticks / buttons work as before.
#pragma once
enum { FE_TITLE, FE_LOBBY, FE_PLAY, FE_CUSTOM, FE_OPERATIVES, FE_LOADOUT, FE_OPTIONS, FE_EXIT, FE_QUICK, FE_TUTORIAL, FE_MAPS, FE_MATCHING, FE_NET, FE_NETHOST, FE_NETROOM, FE_TANK, FE_NSCREEN };   // (b74: FE_TANK = MY THINK TANK, menu_tank.h)   // net: FE_NET* = the AD-HOC screens (menu_net.h)
enum { FE_NONE, FE_START, FE_QUIT, FE_APPLY };     // menu_fe_update results (FE_APPLY: options changed, re-read cfg)
static struct {
	int screen, prev, sel[FE_NSCREEN], ok;              // current screen, selection per screen
	float t, st;                                        // time on this screen, time since the screen changed
	unsigned held_prev; float rep_t;                    // d-pad auto-repeat
	int ci[6];                                          // custom match option indices: mode (MENU_MODE_OFFER), map, score, time, team, skill
	int ocat, wslot, wcat;                              // options tab, armory slot, armory category tab
	GLuint thumbs;                                      // map thumbnails + training art (mthumb.gtx), loaded while the front-end is up
	MuScroll scr[FE_NSCREEN];                           // per-screen scroll views
	int bg_drawn, opt_back, map_back;                   // front-end backdrop drawn this frame; screens to return to
	float slide_snd;                                    // slider sound throttle
	float tl_go; int tl_snd;                            // b59: the title's exit (s since the press, 0 none); its reveal sound played
	int intro_req;                                     // b70: the title has been left: main.c plays the intro movie before the lobby
	MenuConfig opt_saved; int opt_snap, opt_alert, opt_csel;   // options: the settings when the window opened, ALERT (save changes?) up, its choice
	int det_on, det_op, det_tab, det_err; float det_t; GLuint det_tex;   // b63: the operative screen's DETAILS window (fe_details_*)
	int insp, insp_w; float insp_t;                     // b89: the armory's INSPECT view is up, its gun (MENU_WPN index), time since it opened / changed gun
} MFE = { .opt_back = FE_LOBBY };
static void fe_net_open(void);   // net: the AD-HOC window (menu_net.h)
static void fe_net_badge(int screen, const char **l1, const char **l2);
static void fe_det_close(void);   // b63: DETAILS (operatives)
static void fe_tank_open(const MenuConfig *c);   // b74: MY THINK TANK (menu_tank.h)
static void menu_fe_open(int screen) {
	if (MFE.det_on) fe_det_close();
	if (screen == FE_OPTIONS && MFE.screen != FE_OPTIONS && MFE.screen != FE_EXIT) MFE.opt_back = MFE.screen == FE_TITLE ? FE_LOBBY : MFE.screen;
	MFE.prev = MFE.screen; MFE.screen = screen; MFE.st = 0; menu_touch_lock(); if (screen != FE_OPTIONS) MFE.insp = 0;
	if (screen == FE_TITLE) { MFE.tl_go = 0; MFE.tl_snd = 0; }
}
static void menu_fe_close(void) { if (MFE.det_on) fe_det_close(); if (MFE.thumbs) { glDeleteTextures(1, &MFE.thumbs); MFE.thumbs = 0; } }   // before loading a match
static int mwrap(int v, int n) { return ((v % n) + n) % n; }
// d-pad with auto-repeat: returns the pressed mask plus repeats of held directions
static unsigned menu_repeat(unsigned pressed, unsigned held, float dt) {
	unsigned dirs = held & (MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT);
	if (!dirs || dirs != (MFE.held_prev & (MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT))) { MFE.rep_t = 0; MFE.held_prev = held; return pressed; }
	MFE.held_prev = held; MFE.rep_t += dt;
	if (MFE.rep_t > 0.4f) { MFE.rep_t -= 0.09f; return pressed | dirs; }
	return pressed;
}
// thumbnail atlas slot i (0..16 maps in MAPS[] order, 17..20 training programs) into a screen rectangle (cover-cropped to 16:9)
// tr > 0 (b95, user: "the map boxes top right corners overlap over their outlines"): the picture's top right corner cut by tr px,
// so it stays inside a card whose own corner is cut (fe_card_bg: 22 px); FE_CUT_IN gives tr for a picture inset (r, t) from the card
#define FE_CUT_IN(r, t) (22 + 4 * 1.41421356f - (r) - (t))   // (4 px clear of the card's diagonal outline)
static void fe_thumb_c(int i, float x0, float y0, float x1, float y1, float a, float tr) {
	if (!MFE.thumbs) { char p[200]; snprintf(p, sizeof(p), "%smthumb.gtx", menu_dir); MFE.thumbs = menu_tex_load(p); }
	if (!MFE.thumbs || i < 0 || i > 23 || !mu_tex(MFE.thumbs, 9)) { float c[4] = { 0.1f, 0.18f, 0.2f, a }; mu_cut(x0, y0, x1, y1, 0, tr, 0, 0, c); return; }
	float u0 = ((i % 4) * 256 + 8) / 1024.f, v0 = ((i / 4) * 144 + 4) / 1024.f, u1 = u0 + 240 / 1024.f, v1 = v0 + 135 / 1024.f;
	float ra = (x1 - x0) / (y1 - y0), r0 = 240.f / 135.f;
	if (ra > r0) { float k = r0 / ra, dv = (v1 - v0) * (1 - k) / 2; v0 += dv; v1 -= dv; } else { float k = ra / r0, du = (u1 - u0) * (1 - k) / 2; u0 += du; u1 -= du; }
	float m[4] = { 1, 1, 1, a };
	if (tr <= 0) { mu_emit(x0, y0, u0, v0, m); mu_emit(x1, y0, u1, v0, m); mu_emit(x1, y1, u1, v1, m); mu_emit(x0, y0, u0, v0, m); mu_emit(x1, y1, u1, v1, m); mu_emit(x0, y1, u0, v1, m); return; }
	float ux = (u1 - u0) / (x1 - x0), vy = (v1 - v0) / (y1 - y0), P[5][4] = { { x0, y0 }, { x1 - tr, y0 }, { x1, y0 + tr }, { x1, y1 }, { x0, y1 } };
	for (int k = 0; k < 5; k++) { P[k][2] = u0 + (P[k][0] - x0) * ux; P[k][3] = v0 + (P[k][1] - y0) * vy; }
	for (int t = 1; t < 4; t++) { mu_emit(P[0][0], P[0][1], P[0][2], P[0][3], m); mu_emit(P[t][0], P[t][1], P[t][2], P[t][3], m); mu_emit(P[t + 1][0], P[t + 1][1], P[t + 1][2], P[t + 1][3], m); }
}
static void fe_thumb(int i, float x0, float y0, float x1, float y1, float a) { fe_thumb_c(i, x0, y0, x1, y1, a, 0); }
// touch helpers for lists: select on press (still grids) or on a tap (scrolling lists); a tap on the selected entry activates it
static int fe_pick(int kind, int *sel, int n) {
	if (MUT.press && MU_KIND(MUT.id) == kind && MU_IX(MUT.id) < n) { int i = MU_IX(MUT.id); MUT.was_sel = i == *sel; if (i != *sel) { *sel = i; MENU_SFX("ui/mouse_over_basic"); } }
	if (MUT.tap >= 0 && MU_KIND(MUT.tap) == kind && MUT.was_sel) { MENU_SFX("ui/click_basic"); return MU_IX(MUT.tap); }
	return -1;
}
static int fe_pick_tap(int kind, int *sel, int n) {
	if (MUT.tap >= 0 && MU_KIND(MUT.tap) == kind && MU_IX(MUT.tap) < n) { int i = MU_IX(MUT.tap);
		if (i == *sel) { MENU_SFX("ui/click_basic"); return i; } *sel = i; MENU_SFX("ui/mouse_over_basic"); }
	return -1;
}
// ------------------------------------------------------------------ common chrome: top bar (back tab, DIVE badge, navigation), hint line
// The bar is 56 px tall; its touch targets reach down to 72 px (MU_TAP) and screens start their targets at MU_Y0 (84).
// The yellow tab names where it goes back to, as the original's "LOBBY [ESC]" / "SERVER LIST [ESC]".
static const char *FE_BACKTO[FE_NSCREEN] = { "", "EXIT GAME", "LOBBY", "LOBBY", "LOBBY", "LOBBY", "BACK", "LOBBY", "LOBBY", "LOBBY", "CUSTOM MATCH", "CANCEL", "LOBBY", "MULTIPLAYER", "LEAVE ROOM", "LOBBY" };   // (b109, user: "change the AD-HOC option on the main menu to MULTIPLAYER now since it includes more than just ad-hoc")
static void fe_badge_text(int screen, const char **l1, const char **l2);   // DIVE MODE / matchmaking badge texts (below)
static void fe_gear(float cx, float cy, float r, const float *c) {
	mu_ring(cx, cy, r * 0.42f, r * 0.72f, c);
	for (int i = 0; i < 8; i++) { float t = i * 0.785398f; mu_line(cx + cosf(t) * r * 0.6f, cy + sinf(t) * r * 0.6f, cx + cosf(t) * r, cy + sinf(t) * r, r * 0.34f, c); }
}
#define FE_TB_H 56.0f
// navigation targets (right): gear, ARMORY, OPERATIVE from the right edge, 12 px apart
static void fe_nav_rect(int i, float *x0, float *x1) {
	static const char *nv[2] = { "OPERATIVE", "ARMORY" }; float r = 956;
	if (i == 2) { *x0 = r - 60; *x1 = r; return; } r -= 60 + MU_GAP;
	float wa = mu_tw(nv[1], MU_TEXT) + 22; if (i == 1) { *x0 = r - wa; *x1 = r; return; } r -= wa + MU_GAP;
	*x1 = r; *x0 = r - (mu_tw(nv[0], MU_TEXT) + 22);
}
static void fe_topbar(int screen) {
	static const float b0[4] = { 0.02f, 0.05f, 0.06f, 0.96f }, b1[4] = { 0.04f, 0.10f, 0.12f, 0.96f };
	mu_rectv(0, 0, 960, FE_TB_H, b0, b1); mu_rect(0, FE_TB_H, 960, FE_TB_H + 2, MU_CYAND);
	// back tab (yellow, the destination; b50: no Circle - the hint row has it); a tap goes back. The label keeps 16 px right of the
	// darker translucent corner rising from the underline (its tip at x 38, y 28) and 8 px left of the light stripe at the right
	// end (x 212 at the cap's top): a long label shrinks to fine print (CUSTOM MATCH) rather than run into either
	{ int id = MU_ID(MU_K_TOP, 0), pr = mu_pressing(id); mu_img3(MI(pr ? "tb.light" : "tb.yellow"), 0, 4, 258, FE_TB_H, 0.16f, 0.2f, NULL);
	  mu_back_label(54, 2, 204, 52, MU_TEXT, MU_DARK, pr, FE_BACKTO[screen]);
	  mu_reg(id, 0, 0, 258, MU_TAP); }
	// DIVE badge (quick match; START on the match setup screens)
	if (screen != FE_MAPS) {
		int id = MU_ID(MU_K_TOP, 1), pr = mu_pressing(id); mu_img(MI(pr ? "tb.dive.on" : "tb.dive"), 268, 0, 582, 58, NULL);
		const char *l1 = screen == FE_CUSTOM ? "CUSTOM MATCH" : screen == FE_TUTORIAL ? "TRAINING" : "BOTS", *l2 = screen == FE_CUSTOM || screen == FE_TUTORIAL ? "START" : "DIVE";
		if (screen == FE_QUICK || screen == FE_MATCHING) fe_badge_text(screen, &l1, &l2);   // SETTING / SCANNING n / NEXT GAME STARTS IN n
		if (screen >= FE_NET && screen <= FE_NETROOM) fe_net_badge(screen, &l1, &l2);   // net: AD-HOC / LAN, ROOM n/m
		float c1[4] = { 1, 0.85f, 0.82f, 1 }; mu_text(425, 22, MU_FINE, 2, c1, l1); mu_text(425, 51, MU_H2, 2, MU_WHITE, l2); mu_reg(id, 270, 0, 580, MU_TAP);
	}
	// navigation: OPERATIVE, ARMORY, options (gear)
	static const char *nv[2] = { "OPERATIVE", "ARMORY" };
	{ int id = MU_ID(MU_K_NAV, 2), on = screen == FE_OPTIONS, pr = mu_pressing(id); float x0, x1; fe_nav_rect(2, &x0, &x1);
	  if (on || pr) mu_rect(x0, 6, x1, FE_TB_H, pr ? MU_CYAN : MU_TABSEL); fe_gear((x0 + x1) / 2, 31, 17, on || pr ? MU_DARK : MU_WHITE); mu_reg(id, x0, 0, x1, MU_TAP); }
	for (int i = 1; i >= 0; i--) { int id = MU_ID(MU_K_NAV, i), on = screen == (i ? FE_LOADOUT : FE_OPERATIVES), pr = mu_pressing(id); float x0, x1; fe_nav_rect(i, &x0, &x1);
		if (on || pr) mu_cut(x0, 6, x1, FE_TB_H, 10, 0, 0, 0, pr ? MU_CYAN : MU_TABSEL);
		mu_textbox(x0, 6, x1, FE_TB_H, MU_TEXT, 2, 4, on || pr ? MU_DARK : MU_WHITE, nv[i]); mu_reg(id, x0, 0, x1, MU_TAP); }
}
static void fe_hints(const char *s) { float c[4] = { 0.01f, 0.03f, 0.04f, 1 }; mu_rect(0, MU_HINT_BAR, 960, 544, c); mu_hints_fit(s, MU_HINT_Y, 10); }   // b76: fitted (the OPERATIVE screen's seven ran off the left edge)   // b61: opaque (a window's hints showed the screen's own under them)
// top bar taps: back / DIVE / navigation -> extra MB_* bits or a screen change; returns the bits to act on
static unsigned fe_topbar_update(MenuConfig *c) {
	(void)c; unsigned p = 0;
	if (mu_tapped(MU_ID(MU_K_TOP, 0))) { p |= MB_BACK; MENU_SFX("ui/character_cancel"); }
	if (mu_tapped(MU_ID(MU_K_TOP, 1))) {
		MENU_SFX("ui/click_basic");
		if (MFE.screen == FE_QUICK || MFE.screen == FE_CUSTOM || MFE.screen == FE_TUTORIAL || MFE.screen == FE_MATCHING || MFE.screen == FE_NETHOST || MFE.screen == FE_NETROOM) p |= MB_START;
		else if (MFE.screen != FE_NET) { menu_fe_open(FE_QUICK); }
	}
	if (mu_tapped(MU_ID(MU_K_NAV, 0)) && MFE.screen != FE_OPERATIVES) { MENU_SFX("ui/click_basic"); menu_fe_open(FE_OPERATIVES); MFE.sel[FE_OPERATIVES] = -1; }
	if (mu_tapped(MU_ID(MU_K_NAV, 1)) && MFE.screen != FE_LOADOUT) { MENU_SFX("ui/click_basic"); menu_fe_open(FE_LOADOUT); MFE.sel[FE_LOADOUT] = -1; }
	if (mu_tapped(MU_ID(MU_K_NAV, 2)) && MFE.screen != FE_OPTIONS) { MENU_SFX("ui/window_open_basic"); menu_fe_open(FE_OPTIONS); MFE.sel[FE_OPTIONS] = 0; }
	return p;
}
// a card (play cards, quick match modes, training programs): dark tile with a cut corner; selected = lifted, cyan rim
static void fe_card_bg(float x0, float y0, float x1, float y1, int sel, int press) {
	static const float c0[4] = { 0.07f, 0.16f, 0.19f, 0.94f }, c1[4] = { 0.03f, 0.07f, 0.09f, 0.94f }, s0[4] = { 0.15f, 0.36f, 0.42f, 0.97f }, s1[4] = { 0.05f, 0.14f, 0.17f, 0.97f };
	if (sel) { float g[4] = { 0.38f, 0.82f, 0.88f, 0.25f }; mu_cut(x0 - 5, y0 - 5, x1 + 5, y1 + 5, 0, 26, 0, 0, g); }
	mu_cutv(x0, y0, x1, y1, 0, 22, 0, 0, sel ? s0 : c0, sel ? s1 : c1);
	mu_cutline(x0, y0, x1, y1, 0, 22, 0, 0, sel ? 2.5f : 1.2f, press ? MU_WHITE : sel ? MU_CYAN : MU_CYAND);
	mu_rect(x0, y0, x1 - 22, y0 + 4, sel ? MU_CYAN : MU_CYAND);
}
// ------------------------------------------------------------------ title
// b59 (user: visual and sound effects for the start screen, "the best way of it looking and sounding"): the backdrop comes
// up from black and breathes (menu_fe_draw_bg), faint rings turn behind the logo and data motes rise; the logo is scanned in
// left to right (a cyan bar, glitch slices), flashes white with a ring going out (ui/logo_loading), then a light sweeps
// across it every few seconds; the prompt comes up over a line opening from the centre. A press (ui/click_game_start +
// ui/whoosh) blinks the prompt, flashes and fades the screen out, and the lobby comes in from black (ui/lobby_on, the
// lobby music crossfading from the logo music: menu_fe_update FE_TITLE). Some 120 quads a frame.
#define TL_GO 0.5f                                     // seconds from the press to the lobby
static float tl_hash(int i) { unsigned h = (unsigned)i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (float)(h & 0xffff) / 65535.f; }
static void tl_logo_rect(int im, float grow, float *r) {   // where the logo sits (as mu_imgfit in 150..810 x 100..330), grown about its centre
	const float *l = himg[im].loc; float w = l[2] - l[0], h = l[3] - l[1], s = fminf(660 / w, 230 / h) * grow;
	r[0] = 480 - w * s / 2; r[1] = 215 - h * s / 2; r[2] = 480 + w * s / 2; r[3] = 215 + h * s / 2;
}
static void tl_logo_part(int im, const float *r, float u0, float u1, float v0, float v1, float dx, const float *m) {   // columns u0..u1, rows v0..v1 (0..1)
	if (u0 < 0) u0 = 0; if (u1 > 1) u1 = 1; if (v0 < 0) v0 = 0; if (v1 > 1) v1 = 1; if (u1 <= u0 || v1 <= v0) return;
	const HImg *I = &himg[im]; float W = r[2] - r[0], H = r[3] - r[1];
	mu_img_part(im, u0 * I->w, v0 * I->h, u1 * I->w, v1 * I->h, r[0] + u0 * W + dx, r[1] + v0 * H, r[0] + u1 * W + dx, r[1] + v1 * H, m);
}
static void tl_arc(float cx, float cy, float r0, float r1, float a0, float a1, const float *c) {   // a ring segment, one quad per 5 degrees
	int n = (int)(fabsf(a1 - a0) / 0.087f) + 1; float d = (a1 - a0) / n;
	for (int i = 0; i < n; i++) { float b0 = a0 + i * d, b1 = b0 + d;
		float p[4][2] = { { cx + r0 * cosf(b0), cy + r0 * sinf(b0) }, { cx + r1 * cosf(b0), cy + r1 * sinf(b0) }, { cx + r1 * cosf(b1), cy + r1 * sinf(b1) }, { cx + r0 * cosf(b1), cy + r0 * sinf(b1) } };
		mu_poly(4, (const float (*)[2])p, c); }
}
static void fe_title_draw(void) {
	float u = MFE.st, go = MFE.tl_go, out = go > 0 ? msmooth((go - 0.08f) / (TL_GO - 0.12f)) : 0, T = MFE.t;
	{ float k = 1 - msmooth(u / 0.9f); if (k > 0) { float b[4] = { 0, 0, 0, k }; mu_rect(0, 0, 960, 544, b); } }   // up from black
	// a faint scan band drifting down the screen
	{ float y = fmodf(T * 42, 760) - 110, a = 0.035f * msmooth((u - 0.5f) / 1.5f); float c0[4] = { 0.40f, 0.85f, 0.95f, 0 }, c1[4] = { 0.40f, 0.85f, 0.95f, a }, c2[4] = { 0.55f, 0.95f, 1, a * 1.6f };
	  mu_rectv(0, y, 960, y + 90, c0, c1); mu_rect(0, y + 90, 960, y + 91, c2); }
	// rings behind the logo: a broken thick one and a dashed thin one, turning the opposite ways
	{ float a = 0.075f * msmooth((u - 0.5f) / 1.4f) * (1 - out), c[4] = { 0.50f, 0.90f, 1, a };
	  if (a > 0.002f) for (int k = 0; k < 2; k++) { int n = k ? 48 : 18; float R = k ? 300 : 258, w = k ? 1.5f : 3.5f, rot = T * (k ? -0.045f : 0.07f);
		for (int i = 0; i < n; i++) { if (!k && i % 6 == 5) continue; float a0 = rot + i * 6.2831853f / n; tl_arc(480, 215, R, R + w, a0, a0 + 6.2831853f / n * (k ? 0.45f : 0.82f), c); } } }
	// data motes rising
	for (int i = 0; i < 36; i++) {
		float sp = 8 + 20 * tl_hash(i * 3 + 1), x = 20 + 920 * tl_hash(i * 3), y = 560 - fmodf(T * sp + 600 * tl_hash(i * 3 + 2), 600), s = 1.5f + 2.f * tl_hash(i + 77);
		float tw = 0.5f + 0.5f * sinf(T * (1.3f + tl_hash(i + 9)) + i), a = (0.10f + 0.32f * tw) * msmooth((u - 0.3f) / 1.5f) * (0.25f + 0.75f * fmaxf(0, y) / 544) * (1 - out);
		if (a > 0.004f) { float c[4] = { 0.45f, 0.90f, 1, a }; mu_rect(x, y, x + s, y + s, c); } }
	int im = MI("tl.logo");
	if (im >= 0) {
		float r[4]; tl_logo_rect(im, 1 + 0.05f * out, r); float W = r[2] - r[0], k = msmooth((u - 0.15f) / 0.55f), lm[4] = { 1, 1, 1, 1 - out };
		if (k >= 1) mu_img(im, r[0], r[1], r[2], r[3], lm);
		else if (k > 0) {   // scanned in: the revealed columns, glitch slices, the scan bar at the front
			tl_logo_part(im, r, 0, k, 0, 1, 0, lm);
			int sd = (int)(T * 24); for (int j = 0; j < 3; j++) { float v0 = tl_hash(sd * 7 + j), h = 0.04f + 0.07f * tl_hash(sd * 7 + j + 3), dx = (tl_hash(sd * 7 + j + 5) - 0.5f) * 26, gm[4] = { 0.55f, 1, 1, 0.6f };
				tl_logo_part(im, r, 0, k, v0, v0 + h, dx, gm); }
			float sx = r[0] + k * W, g0[4] = { 0.30f, 0.80f, 0.95f, 0 }, g1[4] = { 0.30f, 0.80f, 0.95f, 0.30f }, bar[4] = { 0.80f, 1, 1, 0.95f };
			mu_recth(sx - 70, r[1] - 14, sx, r[3] + 14, g0, g1); mu_rect(sx - 1.5f, r[1] - 22, sx + 1.5f, r[3] + 22, bar);
		}
		{ float e = (u - 0.70f) / 0.65f; if (e > 0 && e < 1) {   // the flash: the logo goes white and fades back, a ring goes out
			float f = (1 - e) * (1 - e) * 0.85f, fm[4] = { 3, 3, 3, f }; mu_img(im, r[0], r[1], r[2], r[3], fm);
			float rr = 70 + 430 * msmooth(e), rc[4] = { 0.65f, 0.95f, 1, 0.38f * (1 - e) }; tl_arc(480, 215, rr, rr + 2.5f, 0, 6.2831853f, rc); } }
		if (u > 2.3f && !out) { float ph = fmodf(u - 2.3f, 6.5f) / 0.9f;   // the light sweep: a slanted band (four row bands), soft sides
			if (ph < 1) { float p = -0.2f + 1.4f * msmooth(ph);
				for (int j = 0; j < 4; j++) { float v0 = j * 0.25f, v1 = v0 + 0.25f, o = (1.5f - j) * 0.045f, c = p + o, s1[4] = { 3, 3, 3, 0.20f }, s2[4] = { 3, 3, 3, 0.45f };
					tl_logo_part(im, r, c - 0.07f, c - 0.022f, v0, v1, 0, s1); tl_logo_part(im, r, c - 0.022f, c + 0.022f, v0, v1, 0, s2); tl_logo_part(im, r, c + 0.022f, c + 0.07f, v0, v1, 0, s1); } } }
	}
	// the prompt over a line that opens from the centre; a press blinks it
	{ float a = msmooth((u - 1.0f) / 0.5f) * (1 - out), lw = 230 * msmooth((u - 0.9f) / 0.5f);
	  if (lw > 1) { float c0[4] = { 0.40f, 0.85f, 0.95f, 0 }, c1[4] = { 0.40f, 0.85f, 0.95f, 0.55f * (1 - out) }; mu_recth(480 - lw, 452, 480, 453.5f, c0, c1); mu_recth(480, 452, 480 + lw, 453.5f, c1, c0);
		float dk = 6 * msmooth((u - 1.1f) / 0.3f), dc[4] = { 0.70f, 0.97f, 1, 0.9f * (1 - out) }, d[4][2] = { { 480, 452.75f - dk }, { 480 + dk, 452.75f }, { 480, 452.75f + dk }, { 480 - dk, 452.75f } };
		if (dk > 0.5f) mu_poly(4, (const float (*)[2])d, dc); }   // (the original's loading line has its emblem in the middle)
	  float b = go > 0 ? (((int)(go * 18)) & 1 ? 1.f : 0.3f) : 0.55f + 0.45f * cosf(T * 3.2f), mb[4] = { 1, 1, 1, a * b };
	  const char *t1 = "PRESS", *t2 = "OR TAP THE SCREEN"; float s = 26, w = mu_tw(t1, s) + 40 + mu_tw(t2, s), x = 480 - w / 2;
	  mu_text(x, 430, s, 0, mb, t1); x += mu_tw(t1, s) + 20; mu_a = a * b; mu_glyph(MG_CROSS, x, 421, 14); mu_a = 1; mu_text(x + 20, 430, s, 0, mb, t2); }
	// b55: the "OFFLINE - YOU AND BOTS ON THE PS VITA" line under the prompt is gone (user)
	if (go > 0) { float fl = 0.28f * (1 - go / 0.18f); if (fl > 0) { float w[4] = { 0.85f, 0.97f, 1, fl }; mu_rect(0, 0, 960, 544, w); }
		float b[4] = { 0, 0, 0, out }; mu_rect(0, 0, 960, 544, b); }
	mu_reg(MU_ID(MU_K_BG, 0), 0, 0, 960, 544);
}
static void fe_custom_init(MenuConfig *c);
static void fe_dive_open(const MenuConfig *c);
// ------------------------------------------------------------------ lobby: the original's NETWORK ACCESS panel (tiles), the operator on the right
enum { LB_QUICK, LB_CUSTOM, LB_OPER, LB_TUTOR, LB_RANGE, LB_ARMORY, LB_OPTIONS, LB_ADHOC, LB_TANK, LB_N };   // (b74: TACHIKOMA - MY THINK TANK)   // net: AD-HOC under QUICK MATCH (its column, half height each)
// tiles 12 px apart (also their touch targets), below the top bar's targets; the widths fit the labels at 24 px
static const short FE_LB_RECT[LB_N][4] = {
	{ 20, 108, 196, 232 }, { 208, 108, 394, 232 }, { 406, 108, 612, 232 }, { 208, 244, 394, 368 }, { 406, 244, 612, 368 },
	{ 20, 380, 270, 504 }, { 454, 380, 612, 504 }, { 20, 244, 196, 368 }, { 282, 380, 442, 504 } };   // b74: ARMORY | TACHIKOMA | OPTIONS in the bottom row (was ARMORY 20-394, OPTIONS 406-612)
static const signed char FE_LB_NAV[LB_N][4] = {   // up, down, left, right
	{ -1, LB_ADHOC, -1, LB_CUSTOM }, { -1, LB_TUTOR, LB_QUICK, LB_OPER }, { -1, LB_RANGE, LB_CUSTOM, -1 }, { LB_CUSTOM, LB_TANK, LB_ADHOC, LB_RANGE },
	{ LB_OPER, LB_OPTIONS, LB_TUTOR, -1 }, { LB_ADHOC, -1, -1, LB_TANK }, { LB_RANGE, -1, LB_TANK, -1 }, { LB_QUICK, LB_ARMORY, -1, LB_TUTOR },
	{ LB_TUTOR, -1, LB_ARMORY, LB_OPTIONS } };
static const char *FE_LB_NAME[LB_N] = { "BOTS", "CUSTOM MATCH", "OPERATIVE", "TUTORIAL", "SHOOTING RANGE", "ARMORY", "OPTIONS", "MULTIPLAYER", "TACHIKOMA" };   // (b109, user: AD-HOC -> MULTIPLAYER; "change the QUICKMATCH options in the main menu to BOTS ... actual multiplayer matches happen in the new MULTIPLAYER option")
// a lobby tile: teal gradient, thin rim, dark label strip at the bottom; selected = bright cyan rim
static void fe_tile(float x0, float y0, float x1, float y1, int sel, int press) {
	static const float t0[4] = { 0.19f, 0.42f, 0.50f, 0.92f }, t1[4] = { 0.06f, 0.19f, 0.24f, 0.92f }, p0[4] = { 0.32f, 0.62f, 0.70f, 0.95f }, rim[4] = { 0.55f, 0.80f, 0.85f, 0.45f };
	if (sel) { float g[4] = { 0.38f, 0.82f, 0.88f, 0.30f }; mu_rect(x0 - 4, y0 - 4, x1 + 4, y1 + 4, g); }
	mu_rectv(x0, y0, x1, y1, press ? p0 : t0, t1); mu_frame(x0, y0, x1, y1, sel ? 2.5f : 1.0f, sel ? MU_CYAN : rim);
}
static void fe_tile_label(float x0, float y1, float x1, float h, float size, const char *s, int sel) {
	static const float sb[4] = { 0.02f, 0.06f, 0.08f, 0.82f }; mu_rect(x0 + 1, y1 - h, x1 - 1, y1 - 1, sb); mu_textbox(x0, y1 - h, x1, y1, size, 2, 8, sel ? MU_WHITE : MU_DIM, s);
}
static void fe_logo9(float cx, float cy, float r, const float *c) {   // the game's ring emblem (QUICK MATCH tile), drawn
	for (int i = 0; i < 13; i++) { float a0 = -1.2f + i * 0.40f, a1 = a0 + 0.34f, r0 = r * 0.66f, r1 = r * 0.82f;
		float p[4][2] = { { cx + r0 * cosf(a0), cy + r0 * sinf(a0) }, { cx + r1 * cosf(a0), cy + r1 * sinf(a0) }, { cx + r1 * cosf(a1), cy + r1 * sinf(a1) }, { cx + r0 * cosf(a1), cy + r0 * sinf(a1) } };
		if (i != 3) mu_poly(4, (const float (*)[2])p, c); }
	mu_ring(cx, cy, r * 0.30f, r * 0.44f, c); mu_ring(cx, cy, r * 0.90f, r * 0.96f, c);
	mu_line(cx + r * 0.35f, cy + r * 0.62f, cx - r * 0.05f, cy + r * 1.18f, r * 0.16f, c);
}
static void fe_icon_custom(float cx, float cy, float s, const float *c) {
	float w = 1.6f; mu_cutline(cx - s, cy - s * 0.8f, cx - s * 0.08f, cy + s * 0.8f, 0, 0, 0, s * 0.3f, w * 1.5f, c); mu_cutline(cx + s * 0.08f, cy - s * 0.8f, cx + s, cy + s * 0.8f, 0, s * 0.3f, 0, 0, w * 1.5f, c);
}
static void fe_icon_tutorial(float cx, float cy, float s, const float *c) {
	mu_frame(cx - s, cy - s * 0.75f, cx + s, cy + s * 0.75f, 3, c); mu_rect(cx - s * 0.7f, cy - s * 0.4f, cx - s * 0.05f, cy - s * 0.25f, c); mu_rect(cx - s * 0.7f, cy - s * 0.05f, cx - s * 0.15f, cy + s * 0.1f, c);
	mu_rect(cx - s * 0.7f, cy + s * 0.3f, cx - s * 0.05f, cy + s * 0.45f, c); mu_text(cx + s * 0.45f, cy + s * 0.42f, s * 1.3f, 2, c, "?");
}
static void fe_icon_adhoc(float cx, float cy, float s, const float *c) {   // net: the AD-HOC tile's icon: a handheld sending waves
	mu_frame(cx - s * 0.55f, cy - s * 0.15f, cx + s * 0.55f, cy + s * 0.75f, 2.5f, c); mu_rect(cx - s * 0.3f, cy + s * 0.05f, cx + s * 0.3f, cy + s * 0.5f, c);
	for (int k = 0; k < 3; k++) { float r = s * (0.45f + 0.32f * k); for (int i = 0; i < 6; i++) { float a0 = -2.3f + i * 0.27f, a1 = a0 + 0.27f; mu_line(cx + r * cosf(a0), cy - s * 0.25f + r * sinf(a0), cx + r * cosf(a1), cy - s * 0.25f + r * sinf(a1), 2.5f, c); } }
}
static void fe_icon_tank(float cx, float cy, float s, const float *c) {   // b74: the TACHIKOMA tile's icon: the pod, the dome and its eyes, four legs
	mu_disc(cx + s * 0.45f, cy - s * 0.35f, s * 0.55f, c); mu_disc(cx - s * 0.2f, cy, s * 0.62f, c);
	float d[4] = { 0.06f, 0.18f, 0.22f, 1 }; mu_disc(cx - s * 0.48f, cy - s * 0.05f, s * 0.17f, d); mu_disc(cx - s * 0.05f, cy - s * 0.12f, s * 0.17f, d);
	for (int k = 0; k < 4; k++) { float x = cx - s * 0.75f + k * s * 0.5f; mu_line(x, cy + s * 0.35f, x + (k < 2 ? -s * 0.25f : s * 0.25f), cy + s * 0.95f, s * 0.16f, c); }
}
static void fe_lobby_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_LOBBY];
	mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.86f }, hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f };
	mu_rect(10, 64, 622, 510, pan); mu_rectv(10, 64, 622, 100, hd0, hd1); mu_rect(10, 100, 622, 102, MU_CYAND);
	mu_textbox(22, 64, 420, 100, MU_TEXT, 0, 0, MU_WHITE, "NETWORK ACCESS..."); mu_textbox(420, 64, 614, 100, MU_FINE, 1, 4, MU_CYAN, "OFFLINE");
	for (int i = 0; i < LB_N; i++) {
		const short *R = FE_LB_RECT[i]; float x0 = R[0], y0 = R[1], x1 = R[2], y1 = R[3], cx = (x0 + x1) / 2; int id = MU_ID(MU_K_ITEM, i), on = i == sel, pr = mu_pressing(id);
		fe_tile(x0, y0, x1, y1, on, pr);
		static const float ic[4] = { 0.88f, 0.95f, 0.97f, 0.9f }, wm[4] = { 0.80f, 0.92f, 0.95f, 0.30f };
		switch (i) {
		case LB_QUICK: {   // b76: the Section 9 emblem as an image (menu.ghud 'lb.s9', tools/menu/s9_emblem.py); b46-b75 the quads of fe_logo9
			int im = MI("lb.s9"); float m[4] = { 0.92f, 0.98f, 1.0f, on ? 1.0f : 0.82f };
			if (im >= 0) mu_imgfit(im, cx - 30, y0 + 3, cx + 30, y1 - 43, m); else fe_logo9(cx, y0 + 46, 40, wm);
			fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], 1); break; }   // net: half height (AD-HOC below)
		case LB_ADHOC: fe_icon_adhoc(cx, y0 + 50, 26, ic); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break;
		case LB_CUSTOM: fe_icon_custom(cx, y0 + 44, 26, ic); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break;
		case LB_TUTOR: fe_icon_tutorial(cx, y0 + 44, 26, ic); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break;
		case LB_OPER: { float m[4] = { 1, 1, 1, on ? 1 : 0.85f }; mu_imgcover(MI("lb.team"), x0 + 1, y0 + 1, x1 - 1, y1 - 40, m); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break; }
		case LB_RANGE: fe_thumb(20, x0 + 1, y0 + 1, x1 - 1, y1 - 40, mu_a * (on ? 1 : 0.85f)); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break;
		case LB_ARMORY: {   // the loadout: ARMORY / LOADOUT on top, primary + secondary pictures with their names
			mu_textbox(x0 + 10, y0 + 2, x0 + 150, y0 + 34, MU_BODY, 0, 0, on ? MU_WHITE : MU_DIM, "ARMORY"); mu_textbox(x0 + 130, y0 + 2, x1 - 10, y0 + 34, MU_FINE, 1, 0, MU_CYAN, "LOADOUT");
			const int w2[2] = { c->primary, c->secondary }; float hw = (x1 - x0) / 2;
			for (int k = 0; k < 2; k++) { float bx = x0 + k * hw; char b[16]; snprintf(b, sizeof(b), "wpi.%d", MENU_WPN[w2[k]].icon);
				mu_imgfit(MI(b), bx + 20, y0 + 34, bx + hw - 20, y1 - 30, NULL); mu_textbox(bx + 4, y1 - 30, bx + hw - 4, y1 - 4, MU_FINE, 2, 0, on ? MU_WHITE : MU_DIM, MENU_WPN[w2[k]].name); }
			break; }
		case LB_OPTIONS: fe_gear(cx, y0 + 42, 24, ic); fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break;
		case LB_TANK: {   // b76: the game's blue Think Tank from the front (menu.ghud 'lb.tank', tools/menu/tank_icon.py); else the drawn icon (b74)
			int im = MI("lb.tank"); float m[4] = { 1, 1, 1, on ? 1 : 0.88f };
			if (im >= 0) mu_imgfit(im, x0 + 1, y0 + 1, x1 - 1, y1 - 40, m); else fe_icon_tank(cx, y0 + 44, 28, ic);
			fe_tile_label(x0, y1, x1, 40, MU_TEXT, FE_LB_NAME[i], on); break; }
		}
		mu_reg(id, x0, y0, x1, y1);
	}
	// operator name tag over the 3D figure (as in the original lobby), profile bottom right
	{ const MenuChr *C = &MENU_CHR[c->chr]; float w = mu_tw(C->name, MU_TEXT) + 62, x0 = 788 - w / 2; static const float bg[4] = { 0.02f, 0.06f, 0.07f, 0.75f }; char b[16];
	  mu_cut(x0, 70, x0 + w, 108, 0, 10, 0, 10, bg); mu_disc(x0 + 23, 89, 16, MU_DARK); mu_ring(x0 + 23, 89, 13.5f, 16, MU_CYAN);
	  snprintf(b, sizeof(b), "sk.%d", MENU_CHR_SKICO[c->chr]); mu_imgfit(MI(b), x0 + 12, 78, x0 + 34, 100, NULL);
	  mu_textbox(x0 + 44, 70, x0 + w, 108, MU_TEXT, 0, 4, MU_WHITE, C->name); }
	{ static const float bg[4] = { 0.02f, 0.06f, 0.07f, 0.82f }, gb[4] = { 0.1f, 0.2f, 0.22f, 1 }; char b[24];
	  mu_cut(632, 446, 952, 510, 0, 14, 0, 14, bg); mu_rect(632, 446, 938, 448, MU_CYAND);
	  snprintf(b, sizeof(b), "rk.%d", c->level < 1 ? 0 : c->level > 20 ? 19 : c->level - 1); mu_imgfit(MI(b), 638, 451, 690, 503, NULL);
	  mu_textbox(698, 448, 870, 482, MU_BODY, 0, 0, MU_WHITE, c->name); snprintf(b, sizeof(b), "LV %d", c->level); mu_textbox(860, 448, 946, 482, MU_SMALL, 1, 4, MU_CYAN, b);
	  float f = c->exp_next > 0 ? (float)c->exp / c->exp_next : 0; mu_rect(698, 490, 942, 498, gb); mu_rect(698, 490, 698 + 244 * mu_clamp(f, 0, 1), 498, MU_CYAN); }
	mu_a = 1;
	fe_topbar(FE_LOBBY); fe_hints("d:MOVE|R:ROTATE|x:SELECT|o:EXIT");
}
static int fe_lobby_activate(MenuConfig *c, int i) {
	switch (i) {
	case LB_QUICK: fe_dive_open(c); break;
	case LB_CUSTOM: menu_fe_open(FE_CUSTOM); MFE.sel[FE_CUSTOM] = 6; fe_custom_init(c); break;
	case LB_OPER: menu_fe_open(FE_OPERATIVES); MFE.sel[FE_OPERATIVES] = -1; break;
	case LB_TUTOR: menu_fe_open(FE_TUTORIAL); if (MFE.sel[FE_TUTORIAL] < 0 || MFE.sel[FE_TUTORIAL] > 3) MFE.sel[FE_TUTORIAL] = 0; break;
	case LB_RANGE: c->mode = MODE_TDM; c->map = MENU_MAP_SHOOTING; c->team_size = 1; c->time_limit = 0; c->tut = 0; menu_ld_tut = 0; MENU_SFX("ui/click_game_start"); return FE_START;
	case LB_ARMORY: menu_fe_open(FE_LOADOUT); MFE.sel[FE_LOADOUT] = -1; break;
	case LB_OPTIONS: menu_fe_open(FE_OPTIONS); MFE.sel[FE_OPTIONS] = -1; break;
	case LB_ADHOC: fe_net_open(); break;   // net (menu_net.h)
	case LB_TANK: fe_tank_open(c); break;   // b74: MY THINK TANK (menu_tank.h)
	}
	return FE_NONE;
}
// ------------------------------------------------------------------ cards (quick match modes, training programs): 4 across, 16 px apart
#define FE_CARD_X(k) (26 + (k) * 230)
#define FE_CARD_W 214
#define FE_CARD_Y0 84
#define FE_CARD_Y1 414
static void fe_card_text(float x0, float x1, float y, const char *title, const char *text, float a) {
	float w = x1 - x0 - 20, cx = (x0 + x1) / 2; (void)a;
	int nl = mu_lines(title, w, MU_BODY); float ty = nl > 1 ? y : y + 15;
	mu_wrapa(cx, ty, w, MU_BODY, 30, 2, 2, MU_WHITE, title);
	if (text) mu_wrapa(cx, y + 70, w, MU_SMALL, 26, 3, 2, MU_DIM, text);
}
// the bottom row of the card screens: a summary line (left) and the red action button (right)
static void fe_go_button(const char *label) {
	int id = MU_ID(MU_K_BTN, 0); mu_button(700, 432, 944, 504, label, MU_H2, 1, mu_pressing(id), MU_BTN_GO, 0); mu_reg(id, 700, 432, 944, 504);
}
// ------------------------------------------------------------------ quick match: the original's DIVE MODE window, then matchmaking
// Footage (Machina Media 0:02-0:40, Adman314 1:04-3:00, Objective Rush Mid 0:00-0:20): the lobby's DIVE MODE window (mode
// tiles RANDOM / TEAM DEATHMATCH / TERMINAL CONQUEST / DEMOLITION, the mode text, a map preview, TEAM, DIVE), "SCANNING..."
// for a few seconds, the waiting room (MAP VOTE on the left, GHOST A / GHOST B lists under AUTO BALANCE, "MAP VOTING IS IN
// PROGRESS..." then "AUTO-BALANCING, MATCH WILL START IN A MOMENT", the badge "NEXT GAME STARTS IN n"), the "START IN" count.
// Offline the lists are you and bots and the vote is yours (bots vote too); Cross skips each wait, Circle cancels.
#define FE_NDIVE 6
static const int FE_DIVE_MODE[FE_NDIVE] = { -1, MODE_TDM, MODE_TTM, MODE_DEM, MODE_EM, MODE_GA };   // tile order as the footage (+ Elimination, b63: Ghost Assault)
static const char *FE_DIVE_NAME[FE_NDIVE] = { "RANDOM", "TEAM\nDEATHMATCH", "TERMINAL\nCONQUEST", "DEMOLITION", "ELIMINATION", "GHOST\nASSAULT" };
static const char *FE_DIVE_DESC[FE_NDIVE] = {   // the original's texts (footage); Elimination's is ours
	"Step into the shoes of a Section 9 agent in a variety of operations across the world, including battles against cyber-criminals, terrorists, and assassins of all kinds.",
	"Organized criminal organizations and terrorists are battling for control of the city. Suit up as part of Section 9 to put a stop to these forces and bring peace to the streets.",
	"Rogue forces are attempting to gain control of strategic territories throughout the country. Join a full suite of Section 9 agents alongside Tachikoma tanks to put a stop to their plans.",
	"Intelligence has uncovered a terrorist plot to bomb key infrastructure points across the city. Work with your team to put a stop to them before it's too late. You've got one shot, so make it count.",
	"One life each round. Wipe out the enemy squad before they wipe out yours, and win the rounds to take the match.",
	"A deadly game of hide and seek between Ghosts and Cyborgs. Cyborgs hunt with their blades; Ghosts hide under therm-optic camo and survive until time runs out.",
};
#define FDV_SCAN 3.0f                                  // seconds of SCANNING...
#define FDV_VOTE 5.0f                                  // map voting
#define FDV_BAL 2.5f                                   // auto-balancing
#define FDV_COUNT 3.0f                                 // START IN 3, 2, 1, then 0
#define FDV_ZERO 0.6f                                  // b59 (user: count to zero and stay there, never back to 3): 0 is held this long, then the match loads
static struct { int phase; float t; int mode, cand[2], votes[2], myvote, win, team[2][4], nteam; float next_vote; } FDV;   // phase 0 setting, 1 scanning, 2 vote, 3 balance, 4 count, 5 started (0 shown until the screen goes)
static unsigned fdv_rng = 12345u;
static int fdv_rand(int n) { fdv_rng = fdv_rng * 1664525u + 1013904223u; return n > 0 ? (int)((fdv_rng >> 8) % (unsigned)n) : 0; }
static void fe_dive_open(const MenuConfig *c) {
	menu_fe_open(FE_QUICK); FDV.phase = 0; FDV.t = 0; MFE.sel[FE_QUICK] = 0;
	for (int k = 1; k < FE_NDIVE; k++) if (FE_DIVE_MODE[k] == c->mode) MFE.sel[FE_QUICK] = k;
}
static void fe_badge_text(int screen, const char **l1, const char **l2) {
	static char n[8]; float left = 0;
	if (screen == FE_QUICK && FDV.phase == 0) { *l1 = "BOTS"; *l2 = "SETTING"; return; }
	if (screen == FE_QUICK) { *l1 = "SCANNING"; left = FDV_SCAN - FDV.t; }
	else { *l1 = "NEXT GAME STARTS IN"; left = (FDV.phase == 2 ? FDV_VOTE - FDV.t + FDV_BAL : FDV.phase == 3 ? FDV_BAL - FDV.t : 0) + (FDV.phase == 4 ? FDV_COUNT - FDV.t : FDV.phase >= 5 ? 0 : FDV_COUNT); }
	snprintf(n, sizeof(n), "%d", (int)ceilf(fmaxf(0, left))); *l2 = n;
}
// the RANDOM tile's icon: two arcs chasing each other (the original's circular arrows)
static void fe_icon_random(float cx, float cy, float r, const float *c) {
	for (int h = 0; h < 2; h++) { float a0 = h * 3.14159f + 0.35f, a1 = a0 + 2.45f; int n = 10;
		for (int i = 0; i < n; i++) { float t0 = a0 + (a1 - a0) * i / n, t1 = a0 + (a1 - a0) * (i + 1) / n; mu_line(cx + r * cosf(t0), cy + r * sinf(t0), cx + r * cosf(t1), cy + r * sinf(t1), r * 0.22f, c); }
		float ex = cx + r * cosf(a1), ey = cy + r * sinf(a1), tx = -sinf(a1), ty = cosf(a1), s = r * 0.42f;   // arrow head along the tangent
		float p[3][2] = { { ex + tx * s, ey + ty * s }, { ex - ty * s * 0.75f - tx * s * 0.2f, ey + tx * s * 0.75f - ty * s * 0.2f }, { ex + ty * s * 0.75f - tx * s * 0.2f, ey - tx * s * 0.75f - ty * s * 0.2f } };
		mu_poly(3, (const float (*)[2])p, c); }
	mu_ring(cx, cy, r * 0.30f, r * 0.42f, c);
}
#define FDV_TX0 22
#define FDV_TW 120
#define FDV_TH 100
static void fe_dive_tile_rect(int k, float *x0, float *y0, float *x1, float *y1) { *x0 = FDV_TX0 + (k % 3) * (FDV_TW + 12); *y0 = 114 + (k / 3) * (FDV_TH + 12); *x1 = *x0 + FDV_TW; *y1 = *y0 + FDV_TH; }
static void fe_dive_tile(int k, int on, int pr, int dim) {
	float x0, y0, x1, y1; fe_dive_tile_rect(k, &x0, &y0, &x1, &y1); float cx = (x0 + x1) / 2;
	static const float t0[4] = { 0.10f, 0.22f, 0.26f, 0.94f }, t1[4] = { 0.05f, 0.12f, 0.15f, 0.94f }, g0[4] = { 0.62f, 0.46f, 0.12f, 0.96f }, g1[4] = { 0.40f, 0.28f, 0.06f, 0.96f },
		lab[4] = { 0.02f, 0.06f, 0.08f, 0.85f }, ic[4] = { 0.62f, 0.68f, 0.70f, 0.9f }, icon[4] = { 0.95f, 0.92f, 0.80f, 1 };
	mu_rectv(x0, y0, x1, y1, on ? g0 : pr ? MU_ROWSEL : t0, on ? g1 : t1); mu_frame(x0, y0, x1, y1, on ? 2.5f : 1, on ? MU_YELLOW : pr ? MU_WHITE : MU_CYAND);
	if (k == 0) fe_icon_random(cx, y0 + 28, 19, on ? icon : ic);
	else { float m[4] = { on ? 1.f : ic[0], on ? 1.f : ic[1], on ? 1.f : ic[2], dim ? 0.5f : 0.9f }; mu_imgfit(MI(MENU_MODE_EMB[FE_DIVE_MODE[k]]), cx - 30, y0 + 5, cx + 30, y0 + 51, m); }
	mu_rect(x0 + 1, y1 - 48, x1 - 1, y1 - 1, on ? MU_YELLOW : lab);
	const char *nm = FE_DIVE_NAME[k]; const float *tc = on ? MU_DARK : dim ? MU_OFF : MU_WHITE;
	if (strchr(nm, '\n')) { char a[24], b[24]; const char *nl = strchr(nm, '\n'); int n1 = (int)(nl - nm); snprintf(a, sizeof(a), "%.*s", n1, nm); snprintf(b, sizeof(b), "%s", nl + 1);
		mu_textbox(x0, y1 - 48, x1, y1 - 24, MU_FINE, 2, 7, tc, a); mu_textbox(x0, y1 - 26, x1, y1 - 2, MU_FINE, 2, 7, tc, b); }   // b49: 7 px clear of the tile's sides
	else mu_textbox(x0, y1 - 48, x1, y1 - 1, MU_FINE, 2, 7, tc, nm);
}
// the operator's name tag over the 3D figure (DIVE MODE, waiting room)
static void fe_name_tag(const MenuConfig *c, float cx, float y0) {
	const MenuChr *C = &MENU_CHR[c->chr]; float w = mu_tw(C->name, MU_TEXT) + 62, x0 = cx - w / 2; static const float bg[4] = { 0.02f, 0.06f, 0.07f, 0.75f }; char b[16];
	mu_cut(x0, y0, x0 + w, y0 + 38, 0, 10, 0, 10, bg); mu_disc(x0 + 23, y0 + 19, 16, MU_DARK); mu_ring(x0 + 23, y0 + 19, 13.5f, 16, MU_CYAN);
	snprintf(b, sizeof(b), "sk.%d", MENU_CHR_SKICO[c->chr]); mu_imgfit(MI(b), x0 + 12, y0 + 8, x0 + 34, y0 + 30, NULL);
	mu_textbox(x0 + 44, y0, x0 + w, y0 + 38, MU_TEXT, 0, 4, MU_WHITE, C->name);
}
static void fe_quick_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_QUICK], scan = FDV.phase == 1; if (sel < 0 || sel >= FE_NDIVE) sel = 0;
	mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.88f }, hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, box[4] = { 0.03f, 0.08f, 0.10f, 0.9f };
	mu_rect(10, 64, 652, 510, pan); mu_rectv(10, 64, 652, 104, hd0, hd1); mu_rect(10, 104, 652, 106, MU_CYAND);
	mu_text(24, 96, MU_H2, 0, MU_WHITE, "DIVE MODE");
	{ float st[4] = { 0.10f, 0.40f, 0.45f, 0.5f }; for (int i = 0; i < 8; i++) { float x = 420 + i * 28; float p[4][2] = { { x, 70 }, { x + 16, 70 }, { x + 4, 100 }, { x - 12, 100 } }; mu_poly(4, (const float (*)[2])p, st); } }
	// mode tiles (3 per row, 12 px apart), the mode's text under them
	for (int k = 0; k < FE_NDIVE; k++) { int id = MU_ID(MU_K_ITEM, k); fe_dive_tile(k, k == sel, mu_pressing(id), scan && k != sel);
		float x0, y0, x1, y1; fe_dive_tile_rect(k, &x0, &y0, &x1, &y1); if (!scan) mu_reg(id, x0, y0, x1, y1); }
	mu_rect(22, 338, 406, 504, box); mu_frame(22, 338, 406, 504, 1, MU_CYAND);
	mu_wrap(32, 346, 366, MU_SMALL, 26, 6, MU_DIM, FE_DIVE_DESC[sel]);
	// right: the map preview (cycles through the mode's maps), TEAM, DIVE / SCANNING...
	{ int L[MENU_NPLAY], n = sel == 0 ? MENU_NPLAY : menu_maps_of(FE_DIVE_MODE[sel], L); if (sel == 0) for (int i = 0; i < n; i++) L[i] = i;
	  int mi = n > 0 ? L[(int)(MFE.t / 2.5f) % n] : 0; fe_thumb(mi, 418, 114, 642, 240, mu_a); mu_frame(418, 114, 642, 240, 1.5f, MU_CYAND);
	  float ln[4] = { 0.4f, 0.95f, 0.9f, 0.35f }; mu_rect(418, 176, 642, 177, ln); mu_rect(529, 114, 530, 240, ln);   // the preview's sight lines
	  float sb[4] = { 0, 0, 0, 0.6f }; mu_rect(418, 212, 642, 240, sb); mu_textbox(424, 212, 640, 240, MU_FINE, 0, 4, MU_WHITE, MENU_MAP[mi].name); }
	mu_rect(418, 250, 642, 420, box); mu_rect(418, 250, 642, 286, hd1); mu_textbox(428, 250, 560, 286, MU_TEXT, 0, 0, MU_WHITE, "TEAM");
	mu_rect(562, 255, 638, 281, MU_ROWV); mu_textbox(562, 255, 638, 281, MU_FINE, 2, 2, MU_OFF, "INVITE");
	{ char b[24]; snprintf(b, sizeof(b), "rk.%d", c->level < 1 ? 0 : c->level > 20 ? 19 : c->level - 1); mu_rect(420, 292, 640, 332, MU_ROWL); mu_imgfit(MI(b), 424, 294, 460, 330, NULL);
	  mu_textbox(466, 292, 640, 332, MU_TEXT, 0, 0, MU_WHITE, c->name);
	  snprintf(b, sizeof(b), "%d VS %d  -  BOTS", c->team_size, c->team_size); mu_textbox(428, 340, 640, 372, MU_SMALL, 0, 0, MU_DIM, b);
	  mu_textbox(428, 372, 640, 404, MU_SMALL, 0, 0, MU_CYAN, MENU_SKILL[c->bot_skill & 3]); }
	if (!scan) { int id = MU_ID(MU_K_BTN, 0); mu_button(418, 432, 642, 504, "DIVE", MU_H2, 1, mu_pressing(id), MU_BTN, 0); mu_reg(id, 418, 432, 642, 504); }
	else { char b[32]; mu_rect(418, 432, 642, 504, box); mu_frame(418, 432, 642, 504, 1.5f, MU_CYAND);
		float k = 0.6f + 0.4f * sinf(MFE.t * 6); float cc[4] = { MU_WHITE[0], MU_WHITE[1], MU_WHITE[2], k }; mu_textbox(418, 434, 642, 470, MU_BODY, 2, 0, cc, "SCANNING...");
		snprintf(b, sizeof(b), "( %d sec remaining )", (int)ceilf(fmaxf(0, FDV_SCAN - FDV.t))); mu_textbox(418, 468, 642, 500, MU_SMALL, 2, 0, MU_DIM, b);
		int id = MU_ID(MU_K_BTN, 2); mu_reg(id, 418, 432, 642, 504); }   // a tap skips the wait
	mu_a = 1;
	fe_name_tag(c, 806, 70);
	fe_topbar(FE_QUICK); fe_hints(scan ? "x:SKIP|o:CANCEL" : "d:MOVE|R:ROTATE|x:DIVE|o:BACK");
}
// DIVE: the mode (RANDOM picks one), two map candidates for the vote, the two teams (you + bots)
static void fe_dive_go(const MenuConfig *c, int k) {
	fdv_rng ^= (unsigned)(MFE.t * 1000);
	FDV.mode = k > 0 ? FE_DIVE_MODE[k] : MENU_MODE_OFFER[fdv_rand(MENU_NOFFER)];
	int L[MENU_NPLAY], n = menu_maps_of(FDV.mode, L); FDV.cand[0] = L[fdv_rand(n)]; FDV.cand[1] = FDV.cand[0];
	if (n > 1) while (FDV.cand[1] == FDV.cand[0]) FDV.cand[1] = L[fdv_rand(n)];
	FDV.votes[0] = FDV.votes[1] = 0; FDV.myvote = -1; FDV.win = -1; FDV.next_vote = 0.6f;
	FDV.nteam = c->team_size < 1 ? 1 : c->team_size > 4 ? 4 : c->team_size;
	int used[12] = { 0 }; used[c->chr] = 1; FDV.team[0][0] = c->chr;
	for (int t = 0; t < 2; t++) for (int i = t ? 0 : 1; i < FDV.nteam; i++) { int o = fdv_rand(12), g = 0; while (used[o] && g++ < 12) o = (o + 1) % 12; used[o] = 1; FDV.team[t][i] = o; }
	FDV.phase = 1; FDV.t = 0; MENU_SFX("ui/click_game_start");
}
static void fe_match_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.88f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, box[4] = { 0.03f, 0.08f, 0.10f, 0.9f }, row[4] = { 0.06f, 0.14f, 0.17f, 0.9f }, me[4] = { 0.12f, 0.42f, 0.46f, 0.95f };
	// MAP VOTE (left): the result tile, then the two candidates with their votes (tap / d-pad to vote)
	mu_rect(10, 64, 214, 510, pan); mu_rect(10, 64, 214, 100, hd1);
	{ char b[32]; if (FDV.phase == 2) snprintf(b, sizeof(b), "MAP VOTE   %d", (int)ceilf(fmaxf(0, FDV_VOTE - FDV.t))); else snprintf(b, sizeof(b), "VOTING ENDED");
	  mu_textbox(18, 64, 210, 100, MU_SMALL, 0, 0, MU_WHITE, b); }
	if (FDV.win < 0) { mu_rect(18, 108, 206, 204, box); mu_frame(18, 108, 206, 204, 1, MU_CYAND); mu_textbox(18, 108, 206, 170, 52, 2, 0, MU_OFF, "?"); mu_textbox(18, 172, 206, 204, MU_FINE, 2, 8, MU_YELLOW, MENU_MODE_NAME[FDV.mode]); }
	else { fe_thumb(FDV.cand[FDV.win], 18, 108, 206, 204, mu_a); mu_frame(18, 108, 206, 204, 2, MU_CYAN); float sb[4] = { 0, 0, 0, 0.6f }; mu_rect(18, 172, 206, 204, sb); mu_textbox(18, 172, 206, 204, MU_FINE, 2, 8, MU_WHITE, MENU_MAP[FDV.cand[FDV.win]].name); }
	for (int k = 0; k < 2; k++) { float y0 = 216 + k * 108, y1 = y0 + 96; int id = MU_ID(MU_K_LIST, k), mine = FDV.myvote == k, won = FDV.win == k, out = FDV.win >= 0 && !won;
		float mm = mu_a; mu_a *= out ? 0.4f : 1;
		fe_thumb(FDV.cand[k], 18, y0, 206, y1, mu_a); float sb[4] = { 0, 0, 0, 0.65f }; mu_rect(18, y1 - 30, 206, y1, sb); mu_textbox(18, y1 - 30, 206, y1, MU_FINE, 0, 8, MU_WHITE, MENU_MAP[FDV.cand[k]].name);   // b49: 8 px clear of the tile sides
		mu_rect(160, y0 + 2, 204, y0 + 46, box); { char b[8]; snprintf(b, sizeof(b), "%d", FDV.votes[k]); mu_textbox(160, y0 + 2, 204, y0 + 46, MU_H2, 2, 0, MU_WHITE, b); }
		mu_frame(18, y0, 206, y1, mine || won ? 2.5f : 1, mine || won ? MU_CYAN : MU_CYAND); if (mu_pressing(id)) mu_frame(16, y0 - 2, 208, y1 + 2, 2, MU_WHITE);
		mu_a = mm; if (FDV.phase == 2) mu_reg(id, 18, y0, 206, y1); }
	// the teams under AUTO BALANCE (centre)
	mu_rect(222, 64, 652, 510, pan); mu_rect(222, 64, 652, 100, hd1); mu_textbox(222, 64, 652, 100, MU_SMALL, 2, 0, MU_DIM, "AUTO BALANCE");
	{ static const float cA[4] = { 0.45f, 0.85f, 0.95f, 1 }, cB[4] = { 1, 0.45f, 0.42f, 1 };
	  mu_text(232, 134, MU_H2, 0, cA, "A"); mu_text(258, 132, MU_TEXT, 0, MU_WHITE, "GHOST"); mu_text(642, 134, MU_H2, 1, cB, "B"); mu_text(614, 132, MU_TEXT, 1, MU_WHITE, "GHOST");
	  for (int t = 0; t < 2; t++) for (int i = 0; i < FDV.nteam; i++) { float x0 = t ? 440 : 230, x1 = x0 + 204, y0 = 144 + i * 58, y1 = y0 + 52; int o = FDV.team[t][i], isme = t == 0 && i == 0; char b[16];
		float app = msmooth((MFE.st - 0.15f * (i + t * 0.5f)) / 0.3f); if (app <= 0) continue; float mm = mu_a; mu_a *= app;
		mu_rect(x0, y0, x1, y1, isme ? me : row);
		snprintf(b, sizeof(b), "op.face.%d", MENU_CHR_FACE[o]); mu_imgfit(MI(b), x1 - 52, y0 + 2, x1 - 2, y1 - 2, NULL);
		snprintf(b, sizeof(b), "sk.%d", MENU_CHR_SKICO[o]); mu_disc(x1 - 72, (y0 + y1) / 2, 15, MU_DARK); mu_ring(x1 - 72, (y0 + y1) / 2, 12.5f, 15, t ? cB : cA); mu_imgfit(MI(b), x1 - 82, y0 + 16, x1 - 62, y1 - 16, NULL);
		mu_textbox(x0 + 8, y0, x1 - 97, y1, MU_TEXT, 0, 0, MU_WHITE, isme ? c->name : menu_chr_short(o)); mu_a = mm; } }   // b59: MOTOKO, not MOTOKO KUSANAGI (it ran over the skill disc)   // b49: names 10 px clear of the skill disc (they touched)
	// the status band (a tap skips the wait)
	{ int id = MU_ID(MU_K_BTN, 2), pr = mu_pressing(id); mu_rect(222, 432, 652, 504, pr ? MU_ROWSEL : hd1); mu_rect(222, 432, 652, 434, MU_CYAND);
	  if (FDV.phase == 2) mu_textbox(222, 432, 652, 504, MU_TEXT, 2, 6, MU_WHITE, "MAP VOTING IS IN PROGRESS...");
	  else { mu_textbox(222, 436, 652, 468, MU_TEXT, 2, 6, MU_WHITE, "AUTO-BALANCING,"); mu_textbox(222, 466, 652, 500, MU_TEXT, 2, 6, MU_WHITE, "MATCH WILL START IN A MOMENT"); }
	  if (FDV.phase <= 3) mu_reg(id, 222, 432, 652, 504); }
	// AUTO-BALANCE flash over the lists (footage)
	if (FDV.phase == 3 && FDV.t < 1.0f) { float k = mfade(FDV.t, 0.15f, 0.45f, 0.4f), g[4] = { 0.35f, 0.85f, 0.95f, 0.35f * k }, w[4] = { 0.85f, 0.97f, 1, k }, rg[4] = { 0.6f, 0.9f, 0.95f, 0.5f * k };
		// b61 (user: nothing over anything it should not cover): the lists dim under the flash and its word sits on its own band
		float kb = fminf(1, k * 3), dk[4] = { 0.01f, 0.04f, 0.05f, 0.72f * kb }, bd[4] = { 0.02f, 0.08f, 0.10f, 0.97f * kb };   // (the band comes up before its word)
		// b105 (user: "the 'start in' and auto balance screen and accompanying icons look kind of rough please try and improve upon them
		// with the game's data files"): the flash was a 12-sided disc and a ring of quads. The game's own (PopupManager.swf
		// AutoMatchShuffleMc, menu.ghud 'st.flare'): a streak of light across the lists, AUTO BALANCE over it, SHUFFLING under.
		int ifl = MI("st.flare");
		if (ifl >= 0) { float fl[4] = { 1, 1, 1, k }, sh[4] = { 0.62f, 0.88f, 0.96f, k };
			mu_rect(222, 100, 652, 432, dk); mu_rect(222, 246, 652, 334, bd); mu_img(ifl, 222 - 60 * FDV.t, 208, 652 + 60 * FDV.t, 372, fl);   // (the streak grows a little as it fades)
			mu_textbox(222, 254, 652, 298, MU_H2, 2, 0, w, "AUTO BALANCE"); mu_textbox(222, 298, 652, 326, MU_FINE, 2, 0, sh, "SHUFFLING"); }
		else { mu_rect(222, 100, 652, 432, dk); mu_disc(437, 290, 90 + 20 * FDV.t, g); fe_logo9(437, 290, 70, rg); mu_rect(222, 266, 652, 314, bd); mu_textbox(222, 270, 652, 310, MU_H2, 2, 0, w, "AUTO-BALANCE"); } }
	mu_a = 1;
	fe_name_tag(c, 806, 70);
	fe_topbar(FE_MATCHING);
	if (FDV.phase >= 4) {   // START IN: the ring emblem, the number, the band (over a dimmed screen); 3, 2, 1, then 0 held until the match loads
		float dim[4] = { 0, 0, 0, 0.6f }; mu_rect(0, 0, 960, 544, dim); float left = FDV.phase >= 5 ? 0 : FDV_COUNT - FDV.t; int n = left > 0 ? (int)ceilf(left) : 0;
		float k = n > 0 ? msmooth((1 - (left - (n - 1))) / 0.25f) : FDV.phase >= 5 ? 1 : msmooth(-left / 0.25f);   // (each number pops in; 0 pops once and stays)
		float gl[4] = { 0.25f, 0.75f, 0.85f, 0.35f }, ring[4] = { 0.70f, 0.92f, 0.97f, 0.85f }, band[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, core[4] = { 0.05f, 0.25f, 0.30f, 0.95f };
		// b105: the game's own count (PopupManager.swf GameStartCountMc; menu.ghud 'st.glow', 'st.smoke', 'st.nine'), laid out as
		// its movie has it on the 1600 x 900 stage (x 0.6): the round glow 988 x 904 at (305, 0), the smoke 681 x 487 at (457, 210),
		// the Section 9 emblem about (800, 431) with the number in its eye and START IN on a band under it. The number is larger
		// than the PC's (40 of 900 px there) and the label 24 px: the Vita's screen.
		int ig = MI("st.glow"), is = MI("st.smoke"), i9 = MI("st.nine"), old = ig < 0 || i9 < 0;
		if (!old) { float pulse = 0.85f + 0.15f * k, cg[4] = { 0.22f, 0.72f, 0.86f, 0.85f * pulse }, cs[4] = { 0.60f, 0.90f, 0.98f, 0.55f }, c9[4] = { 0.58f, 0.86f, 0.95f, 0.92f };
			mu_img(ig, 183, 2, 776, 544, cg); if (is >= 0) mu_img(is, 274, 128, 683, 420, cs);
			mu_img(i9, 480 - 104.3f, 261 - 140.3f, 480 + 114.4f, 261 + 189.1f, c9); }
		else { mu_disc(480, 250, 120, gl); fe_logo9(480, 250, 120, ring); mu_disc(480, 250, 52, core); }
		float ny = old ? 250 : 261;
		char b[4]; snprintf(b, sizeof(b), "%d", n); float sz = 64 * (1.3f - 0.3f * k), cc[4] = { 1, 1, 1, 0.4f + 0.6f * k }; mu_text(480, ny + sz * MU_CAP * 0.5f, sz, 2, cc, b);
		if (old) { mu_rect(380, 318, 580, 360, band); mu_textbox(380, 318, 580, 360, MU_TEXT, 2, 0, MU_WHITE, "START IN"); }
		else { float bd2[4] = { 0.03f, 0.13f, 0.17f, 0.90f }; mu_rect(404, 304, 556, 340, bd2); mu_rect(404, 304, 556, 306, MU_CYAND); mu_textbox(404, 306, 556, 340, MU_TEXT, 2, 0, MU_WHITE, "START IN"); }
	} else fe_hints(FDV.phase == 2 ? "d:VOTE|x:SKIP|o:CANCEL" : "x:SKIP|o:CANCEL");
}
static int fe_match_start(MenuConfig *c) {
	int mode = FDV.mode; c->mode = mode; c->map = FDV.cand[FDV.win < 0 ? 0 : FDV.win]; c->score_limit = MENU_LIMITS[mode][MENU_LIMIT_DEF[mode]]; c->time_limit = MENU_TIME_DEF[mode];
	c->tut = 0; menu_ld_tut = 0; return FE_START;
}
static void fdv_end_vote(void) {
	FDV.win = FDV.votes[1] > FDV.votes[0] ? 1 : FDV.votes[0] > FDV.votes[1] ? 0 : FDV.myvote >= 0 ? FDV.myvote : 0;
	FDV.phase = 3; FDV.t = 0; MENU_SFX("ui/lobby_autobalance");   // b59: the teams shuffle
}
// ------------------------------------------------------------------ training programs
static void fe_tutorial_draw(void) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_TUTORIAL];
	for (int k = 0; k < 4; k++) {
		int id = MU_ID(MU_K_ITEM, k), on = k == sel, pr = mu_pressing(id); float lift = on ? -6 * msmooth(MFE.st * 5) : 0;
		float x0 = FE_CARD_X(k), x1 = x0 + FE_CARD_W, y0 = FE_CARD_Y0 + lift, y1 = FE_CARD_Y1 + lift; mu_a = msmooth((MFE.st - 0.05f * k) / 0.3f) * a;
		fe_card_bg(x0, y0, x1, y1, on, pr);
		fe_thumb_c(MENU_TUT[k].thumb, x0 + 8, y0 + 10, x1 - 8, y0 + 118, on ? mu_a : mu_a * 0.7f, FE_CUT_IN(8, 10));
		fe_card_text(x0, x1, y0 + 128, MENU_TUT[k].name2, MENU_TUT[k].desc, 1);
		mu_reg(id, x0, FE_CARD_Y0, x1, FE_CARD_Y1);
	}
	mu_a = 1;
	mu_text(30, 478, MU_TEXT, 0, MU_DIM, "PROSTHETIC CALIBRATION");
	fe_go_button("START");
	fe_topbar(FE_TUTORIAL); fe_hints("d:MOVE|x:START|o:BACK");
}
static int fe_tutorial_start(MenuConfig *c, int k) {
	c->mode = MODE_TDM; c->map = MENU_TUT[k].map; c->tut = k + 1; c->team_size = c->team_size < 1 ? 3 : c->team_size; menu_ld_tut = k + 1;
	MENU_SFX("ui/click_game_start"); return FE_START;
}
// ------------------------------------------------------------------ custom match (room settings): mode, map, limits, team size, bots
static const char *FE_ROW[6] = { "GAME MODE", "MAP", "SCORE LIMIT", "TIME LIMIT", "TEAM SIZE", "BOT SKILL" };
static void fe_custom_apply(MenuConfig *c) {
	int mode = MENU_MODE_OFFER[mwrap(MFE.ci[0], MENU_NOFFER)];
	if (!menu_map_ok(mode, MFE.ci[1])) { int L[MENU_NPLAY]; if (menu_maps_of(mode, L) > 0) MFE.ci[1] = L[0]; }
	c->mode = mode; c->map = MFE.ci[1]; c->score_limit = MENU_LIMITS[mode][MFE.ci[2] & 3];
	c->time_limit = MENU_TIME[MFE.ci[3] & 3]; c->team_size = MENU_TEAM[mwrap(MFE.ci[4], 4)]; c->bot_skill = MFE.ci[5] & 3; c->tut = 0;
}
static void fe_custom_init(MenuConfig *c) {
	MFE.ci[0] = 0; for (int k = 0; k < MENU_NOFFER; k++) if (MENU_MODE_OFFER[k] == c->mode) MFE.ci[0] = k;
	int mode = MENU_MODE_OFFER[MFE.ci[0]]; MFE.ci[1] = menu_map_ok(mode, c->map) ? c->map : -1;
	MFE.ci[2] = MENU_LIMIT_DEF[mode]; MFE.ci[3] = 1;
	for (int i = 0; i < 4; i++) { if (MENU_LIMITS[mode][i] == c->score_limit) MFE.ci[2] = i; if (MENU_TIME[i] == c->time_limit) MFE.ci[3] = i; }
	MFE.ci[4] = 2; for (int i = 0; i < 4; i++) if (MENU_TEAM[i] == c->team_size) MFE.ci[4] = i;
	MFE.ci[5] = c->bot_skill & 3; fe_custom_apply(c);
}
static void fe_custom_change(MenuConfig *c, int row, int d) {
	if (row == 0) { int m0 = c->mode; MFE.ci[0] = mwrap(MFE.ci[0] + d, MENU_NOFFER); int mode = MENU_MODE_OFFER[MFE.ci[0]]; if (m0 != mode) MFE.ci[2] = MENU_LIMIT_DEF[mode]; }
	else if (row == 1) { int L[MENU_NPLAY], n = menu_maps_of(c->mode, L), k = 0; for (int i = 0; i < n; i++) if (L[i] == MFE.ci[1]) k = i; if (n) MFE.ci[1] = L[mwrap(k + d, n)]; }
	else if (row == 4) MFE.ci[4] = mwrap(MFE.ci[4] + d, 4);
	else MFE.ci[row] = mwrap(MFE.ci[row] + d, 4);
	fe_custom_apply(c);
}
static void fe_row_value(const MenuConfig *c, int row, char *v, int n) {
	switch (row) {
	case 0: snprintf(v, n, "%s", MENU_MODE_NAME[c->mode]); break;
	case 1: snprintf(v, n, "%s", MENU_MAP[c->map].name); break;
	case 2: if (c->mode == MODE_TTM && c->score_limit == MENU_LIMITS[MODE_TTM][MENU_LIMIT_DEF[MODE_TTM]]) snprintf(v, n, "MAP");   // main.c: the default = the map's own target (ttm_begin 0)
		else snprintf(v, n, "%d %s", c->score_limit, MENU_LIMIT_UNIT[c->mode]); break;
	case 3: snprintf(v, n, "%d MIN", c->time_limit); break;
	case 4: snprintf(v, n, "%d VS %d", c->team_size, c->team_size); break;
	default: snprintf(v, n, "%s", MENU_SKILL[c->bot_skill & 3]); break;
	}
}
// a label / value row with arrows (custom match, options steppers); ids ROW / ROWL / ROWR + i. Arrow targets span the row's
// height (>= MU_TAP) and sit inside the row's own target.
static void fe_stepper_row(float x0, float y0, float x1, float y1, float xv, int i, const char *label, const char *val, int sel, int off) {
	int il = MU_ID(MU_K_ROWL, i), ir = MU_ID(MU_K_ROWR, i), ib = MU_ID(MU_K_ROW, i);
	mu_rect(x0, y0, xv - 2, y1, sel ? MU_ROWSEL : MU_ROWL); mu_rect(xv, y0, x1, y1, MU_ROWV);
	if (sel) { mu_rect(x0, y0, x0 + 5, y1, MU_CYAN); mu_frame(x0, y0, x1, y1, 1.5f, MU_CYAND); }
	mu_textbox(x0 + 8, y0, xv - 4, y1, MU_TEXT, 0, 10, sel ? MU_WHITE : MU_DIM, label);
	float aw = 60; const float *ac = off ? MU_OFF : MU_WHITE;
	if (!off) { if (mu_pressing(il)) mu_rect(xv, y0, xv + aw, y1, MU_CYAND); if (mu_pressing(ir)) mu_rect(x1 - aw, y0, x1, y1, MU_CYAND);
		mu_tri(xv + aw / 2, (y0 + y1) / 2, 20, 0, ac); mu_tri(x1 - aw / 2, (y0 + y1) / 2, 20, 1, ac); }
	mu_textbox(xv + (off ? 8 : aw), y0, x1 - (off ? 8 : aw), y1, MU_BODY, off ? 0 : 2, 4, off ? MU_WHITE : sel ? MU_WHITE : MU_DIM, val);
	if (off) return;   // information rows: nothing to tap
	mu_reg(ib, x0, y0, x1, y1); mu_reg(il, xv, y0, xv + aw + 10, y1); mu_reg(ir, x1 - aw - 10, y0, x1, y1);
}
// a compact stepper (custom match grid): label on a band at the top, value between the arrows below; the same ids
static void fe_stepper_cell(float x0, float y0, float x1, float y1, int i, const char *label, const char *val, int sel) {
	int il = MU_ID(MU_K_ROWL, i), ir = MU_ID(MU_K_ROWR, i), ib = MU_ID(MU_K_ROW, i); float aw = 60, ym = (y0 + 30 + y1) / 2;
	mu_rect(x0, y0, x1, y1, MU_ROWV); mu_rect(x0, y0, x1, y0 + 30, sel ? MU_ROWSEL : MU_ROWL);
	if (sel) { mu_rect(x0, y0, x0 + 5, y1, MU_CYAN); mu_frame(x0, y0, x1, y1, 1.5f, MU_CYAND); }
	if (mu_pressing(il)) mu_rect(x0, y0 + 30, x0 + aw, y1, MU_CYAND); if (mu_pressing(ir)) mu_rect(x1 - aw, y0 + 30, x1, y1, MU_CYAND);
	mu_textbox(x0 + 8, y0, x1 - 8, y0 + 30, MU_TEXT, 2, 0, sel ? MU_WHITE : MU_DIM, label);
	mu_tri(x0 + aw / 2, ym, 20, 0, MU_WHITE); mu_tri(x1 - aw / 2, ym, 20, 1, MU_WHITE);
	mu_textbox(x0 + aw, y0 + 30, x1 - aw, y1, MU_BODY, 2, 2, sel ? MU_WHITE : MU_DIM, val);
	mu_reg(ib, x0, y0, x1, y1); mu_reg(il, x0, y0, x0 + aw + 10, y1); mu_reg(ir, x1 - aw - 10, y0, x1, y1);
}
// custom match layout: GAME MODE and MAP rows, then SCORE | TIME and TEAM | BOTS cells; BACK / START below (all 72 px, 12 px apart)
static void fe_custom_rect(int i, float *x0, float *y0, float *x1, float *y1) {
	if (i < 2) { *x0 = 392; *x1 = 944; *y0 = 84 + i * 84; }
	else { int k = i - 2; *x0 = k % 2 ? 674 : 392; *x1 = *x0 + 270; *y0 = 252 + (k / 2) * 84; }
	*y1 = *y0 + 72;
}
static void fe_custom_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_CUSTOM];
	mu_a = a;
	// left: the map (tap = map list), the mode emblem and objective
	{ int id = MU_ID(MU_K_ITEM, 0), pr = mu_pressing(id); static const float pan[4] = { 0.02f, 0.06f, 0.07f, 0.88f };
	  mu_cut(14, 66, 380, 510, 0, 16, 0, 0, pan);
	  fe_thumb(c->map, 24, 84, 370, 278, mu_a); mu_frame(24, 84, 370, 278, pr ? 3 : 1.5f, pr ? MU_WHITE : MU_CYAND);
	  float sb[4] = { 0, 0, 0, 0.65f }; mu_rect(24, 236, 370, 278, sb); mu_textbox(24, 236, 330, 278, MU_BODY, 0, 10, MU_WHITE, MENU_MAP[c->map].name);
	  mu_tri(350, 257, 16, 1, MU_CYAN); mu_reg(id, 24, 84, 370, 278); }
	mu_imgfit(MI(MENU_MODE_EMB[c->mode]), 20, 290, 98, 368, NULL);
	mu_textbox(106, 290, 376, 322, MU_BODY, 0, 2, MU_WHITE, MENU_MODE_NAME[c->mode]);
	mu_wrap(108, 328, 266, MU_SMALL, 26, 3, MU_DIM, MENU_MODE_GOAL[c->mode]);
	mu_wrap(26, 420, 344, MU_SMALL, 26, 3, MU_OFF, "OFFLINE MATCH AGAINST BOTS, WITH YOUR OPERATOR AND LOADOUT.");
	// right: the settings
	for (int i = 0; i < 6; i++) { float x0, y0, x1, y1; char v[48]; fe_row_value(c, i, v, sizeof(v)); fe_custom_rect(i, &x0, &y0, &x1, &y1);
		if (i < 2) fe_stepper_row(x0, y0, x1, y1, 600, i, FE_ROW[i], v, i == sel, 0); else fe_stepper_cell(x0, y0, x1, y1, i, FE_ROW[i], v, i == sel); }
	{ int ib = MU_ID(MU_K_BTN, 1), is = MU_ID(MU_K_BTN, 0);
	  mu_button(392, 432, 640, 504, "BACK", MU_BODY, sel == 7, mu_pressing(ib), MU_BTN, 0); mu_reg(ib, 392, 432, 640, 504);
	  mu_button(656, 432, 944, 504, "START", MU_H2, sel == 6, mu_pressing(is), MU_BTN_GO, 0); mu_reg(is, 656, 432, 944, 504); }
	mu_a = 1;
	fe_topbar(FE_CUSTOM); fe_hints("d:CHANGE|x:START|o:BACK");
}
// ------------------------------------------------------------------ map list (from the custom match MAP row): thumbnails, 3 per row, scrolls
#define FE_MAPS_Y0 84
#define FE_MAPS_Y1 420
#define FE_MAPS_CW 296
#define FE_MAPS_CH 162   // b95: two rows fit the view (84..420), clear of the SELECT button (was 172: the lower row ran under it)
#define FE_MAPS_P (FE_MAPS_CH + 12)
static void fe_maps_draw(const MenuConfig *c) {
	int L[MENU_NPLAY], n = menu_maps_of(c->mode, L), sel = MFE.sel[FE_MAPS]; MuScroll *S = &MFE.scr[FE_MAPS];
	float a = msmooth(MFE.st / 0.25f); mu_a = a;
	{ char b[48]; snprintf(b, sizeof(b), "%s  -  %d MAPS", MENU_MODE_NAME[c->mode], n); mu_textbox(26, 432, 690, 504, MU_TEXT, 0, 0, MU_DIM, b); }
	float view = FE_MAPS_Y1 - FE_MAPS_Y0; int rows = (n + 2) / 3; S->max = fmaxf(0, rows * FE_MAPS_P - 12 - view);
	mu_reg(MU_ID(MU_K_LIST, 255), 0, FE_MAPS_Y0, 960, FE_MAPS_Y1);
	for (int k = 0; k < n; k++) {
		float x0 = 22 + (k % 3) * (FE_MAPS_CW + 14), y0 = FE_MAPS_Y0 + (k / 3) * FE_MAPS_P - S->pos, x1 = x0 + FE_MAPS_CW, y1 = y0 + FE_MAPS_CH;
		float v = mu_vis(y0, y1, FE_MAPS_Y0, FE_MAPS_Y1); if (v <= 0.02f) continue; mu_a = a * v;
		int id = MU_ID(MU_K_LIST, k), on = k == sel, cur = L[k] == c->map;
		fe_card_bg(x0, y0, x1, y1, on, mu_pressing(id));
		fe_thumb_c(L[k], x0 + 6, y0 + 8, x1 - 6, y1 - 42, mu_a, FE_CUT_IN(6, 8));
		mu_textbox(x0, y1 - 42, x1, y1 - 4, MU_TEXT, 2, 6, on ? MU_WHITE : MU_DIM, MENU_MAP[L[k]].name);
		if (cur) { mu_cut(x0 + 6, y0 + 8, x0 + 124, y0 + 38, 0, 0, 10, 0, MU_YELLOW); mu_textbox(x0 + 6, y0 + 8, x0 + 116, y0 + 38, MU_FINE, 2, 2, MU_DARK, "CURRENT"); }
		mu_reg_clip(id, x0, y0, x1, y1, FE_MAPS_Y0, FE_MAPS_Y1);
	}
	mu_a = a; mu_scrollbar(951, FE_MAPS_Y0 + 14, FE_MAPS_Y1 - 14, S, view);
	{ int id = MU_ID(MU_K_BTN, 0); mu_button(700, 432, 944, 504, "SELECT", MU_BODY, 1, mu_pressing(id), MU_BTN, 0); mu_reg(id, 700, 432, 944, 504); }
	mu_a = 1;
	fe_topbar(FE_MAPS); fe_hints("d:MOVE|x:SELECT|o:BACK");
}
// ------------------------------------------------------------------ operatives: profile card (left), the operator in 3D with the original's
// "SELECT AN OPERATIVE" bar (centre), the roster by class (right, scrolls)
// b87 (user: the operatives should match the PC's): the v2.0 classes and roster, as the user's "Menu Navigation Ver. 2.0" video
// (2:50-3:30): F1 ASSAULT Batou, Ishikawa, Borma, Azuma; F2 INFILTRATOR Motoko, Paz, Maven, Kuro, Reiko; F3 SPECIALIST Saito,
// Togusa, Sitara (was v1's ASSAULT / RECON / SUPPORT grouping)
static const int FE_OP_SLOT[14] = { 0, 4, 5, 7, -1, 1, 6, 9, 8, 10, 2, 3, 11, -1 };   // roster slots by class -> operator (CHARS order)
static const char *FE_OP_CLASS[3] = { "ASSAULT", "INFILTRATOR", "SPECIALIST" };
static const float FE_OP_STAT[12][3] = {   // firepower, armour, mobility shown as gauges (menu flavour, not game data)
	{ 0.85f, 0.80f, 0.45f }, { 0.75f, 0.55f, 0.85f }, { 0.80f, 0.45f, 0.55f }, { 0.55f, 0.55f, 0.65f }, { 0.50f, 0.60f, 0.55f }, { 0.70f, 0.85f, 0.40f },
	{ 0.70f, 0.50f, 0.80f }, { 0.65f, 0.75f, 0.55f }, { 0.60f, 0.50f, 0.75f }, { 0.75f, 0.60f, 0.70f }, { 0.55f, 0.50f, 0.80f }, { 0.50f, 0.65f, 0.70f } };
static int fe_op_list[12], fe_op_n;           // operators in roster order
static void fe_op_build(void) { if (fe_op_n) return; for (int i = 0; i < 14; i++) if (FE_OP_SLOT[i] >= 0) fe_op_list[fe_op_n++] = FE_OP_SLOT[i]; }
static int fe_op_class(int chr) { for (int i = 0; i < 14; i++) if (FE_OP_SLOT[i] == chr) return i < 5 ? 0 : i < 10 ? 1 : 2; return 0; }
static int fe_op_first(int g) { fe_op_build(); for (int i = 0; i < fe_op_n; i++) if (fe_op_class(fe_op_list[i]) == g) return i; return 0; }   // b87: a class's first roster entry
static int fe_op_index(int chr) { fe_op_build(); for (int i = 0; i < fe_op_n; i++) if (fe_op_list[i] == chr) return i; return 0; }
#define FE_OP_X0 640
#define FE_OP_X1 950
#define FE_OP_Y0 106                           // b76: under the roster's "{updown} SELECT AN OPERATIVE" header (was 84)
#define FE_OP_Y1 420                           // b76: the SELECT button under the list (was 508)
#define FE_OP_ROW 84                           // roster entries: 72 px tall, 12 px apart
#define FE_OP_RH 72
#define FE_OP_HEAD 40
static float fe_op_y(int k) { int g = fe_op_class(fe_op_list[k]); return (g + 1) * FE_OP_HEAD + k * FE_OP_ROW; }   // content y of roster entry k
static void fe_skill_badge(int ch, float cx, float cy, float r) {   // the skill glyph in a ring (the footage's operative label)
	char b[16]; snprintf(b, sizeof(b), "sk.%d", MENU_CHR_SKICO[ch]); static const float d[4] = { 0.02f, 0.06f, 0.07f, 0.85f };
	mu_disc(cx, cy, r, d); mu_ring(cx, cy, r - 2.5f, r, MU_WHITE); mu_imgfit(MI(b), cx - r * 0.62f, cy - r * 0.62f, cx + r * 0.62f, cy + r * 0.62f, NULL);
}
// b71: operator skins (main.c hooks onto bots.h's chr_skins.txt list): how many operator op has (1: the standard only), skin k's name
static int (*mfe_skin_n)(int op); static const char *(*mfe_skin_name)(int op, int k);
static int (*mfe_skin_gog)(int op, int k);   // b103: skin k of the operator has a mask / goggle part (main.c: chr_gog.txt)
// b76 (user: left / right on the D-pad changes the skin, Cross selects): the skins being looked at on the OPERATIVE screen,
// one per operator, taken from the config when the screen opens; Cross (SELECT) puts the operator and its skin shown in
// the config, Circle leaves without. main.c's figure wears menu_fe_skin_shown.
static unsigned char fe_skin_view[12];
static int menu_fe_skin_shown(const MenuConfig *c, int op) {
	op &= 15; if (op >= 12) return 0;
	int s = MFE.screen == FE_OPTIONS ? MFE.opt_back : MFE.screen;
	return s == FE_OPERATIVES && !MFE.det_on ? fe_skin_view[op] : c->skin[op];
}
static void fe_operatives_draw(const MenuConfig *c) {
	fe_op_build(); float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_OPERATIVES]; if (sel < 0 || sel >= fe_op_n) sel = fe_op_index(c->chr);
	MuScroll *S = &MFE.scr[FE_OPERATIVES]; float view = FE_OP_Y1 - FE_OP_Y0; S->max = fmaxf(0, 3 * FE_OP_HEAD + fe_op_n * FE_OP_ROW - 12 - view);
	mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.86f };
	// roster (right): b76 a header that says how to move through it ({updown}), the list, SELECT under it
	mu_rect(FE_OP_X0, 64, FE_OP_X1 + 6, 510, pan);
	{ static const float hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }; mu_rectv(FE_OP_X0, 64, FE_OP_X1 + 6, 102, hd0, hd1); mu_rect(FE_OP_X0, 102, FE_OP_X1 + 6, 104, MU_CYAND);
	  mu_textbox(FE_OP_X0 + 10, 64, FE_OP_X1 - 4, 102, MU_FINE, 0, 0, MU_CYAN, "{updown} SELECT AN OPERATIVE"); }
	mu_reg(MU_ID(MU_K_LIST, 255), FE_OP_X0, FE_OP_Y0, FE_OP_X1, FE_OP_Y1);
	for (int g = 0; g < 3; g++) {   // class headers
		int k0 = g == 0 ? 0 : g == 1 ? 5 : 9; float y = FE_OP_Y0 + fe_op_y(k0) - FE_OP_HEAD - S->pos, v = mu_vis(y, y + FE_OP_HEAD, FE_OP_Y0, FE_OP_Y1);
		if (v <= 0.02f) continue; mu_a = a * v; char b[16]; snprintf(b, sizeof(b), "op.cls.%d", g);
		mu_imgfit(MI(b), FE_OP_X0 + 6, y + 4, FE_OP_X0 + 36, y + 34, NULL); mu_textbox(FE_OP_X0 + 42, y, FE_OP_X1, y + FE_OP_HEAD - 4, MU_TEXT, 0, 0, MU_CYAN, FE_OP_CLASS[g]);
		mu_rect(FE_OP_X0 + 42, y + FE_OP_HEAD - 6, FE_OP_X1 - 16, y + FE_OP_HEAD - 4, MU_CYAND);
	}
	for (int k = 0; k < fe_op_n; k++) {
		int ch = fe_op_list[k], id = MU_ID(MU_K_LIST, k), on = k == sel, eq = ch == c->chr;
		float y0 = FE_OP_Y0 + fe_op_y(k) - S->pos, y1 = y0 + FE_OP_RH, v = mu_vis(y0, y1, FE_OP_Y0, FE_OP_Y1); if (v <= 0.02f) continue; mu_a = a * v;
		float x0 = FE_OP_X0 + 4, x1 = FE_OP_X1 - 14;
		mu_rect(x0, y0, x1, y1, on ? MU_ROWSEL : mu_pressing(id) ? MU_ROWL : MU_ROWV); if (on) mu_frame(x0, y0, x1, y1, 2, MU_CYAN);
		if (eq) mu_rect(x0, y0, x0 + 6, y1, MU_YELLOW);
		{ char b[16]; snprintf(b, sizeof(b), "op.face.%d", MENU_CHR_FACE[ch]); mu_imgfit(MI(b), x0 + 10, y0 + 4, x0 + 72, y1 - 4, NULL); }
		mu_textbox(x0 + 78, y0 + 2, x1 - 4, y0 + 38, MU_TEXT, 0, 0, on ? MU_WHITE : MU_DIM, MENU_CHR[ch].name);
		mu_textbox(x0 + 78, y0 + 38, x1 - 4, y1 - 4, MU_SMALL, 0, 0, eq ? MU_YELLOW : on ? MU_CYAN : MU_OFF, eq ? "SELECTED" : MENU_CHR[ch].skill);
		mu_reg_clip(id, FE_OP_X0, y0, FE_OP_X1, y1, FE_OP_Y0, FE_OP_Y1);
	}
	mu_a = a; mu_scrollbar(FE_OP_X1 - 4, FE_OP_Y0 + 14, FE_OP_Y1 - 14, S, view);
	// profile card (left): name, portrait, class, gauges, skill
	int ch = fe_op_list[sel], cl = fe_op_class(ch), eqs = ch == c->chr; const MenuChr *C = &MENU_CHR[ch];
	int ns = mfe_skin_n ? mfe_skin_n(ch) : 1, skv = ns > 1 ? fe_skin_view[ch] % ns : 0, sel_same = eqs && (ns <= 1 || skv == c->skin[ch] % ns);   // b76
	{ int ib = MU_ID(MU_K_BTN, 0);   // b76: SELECT under the roster (Cross): the operator and the skin shown
	  mu_button(FE_OP_X0 + 6, 432, FE_OP_X1, 504, sel_same ? "SELECTED" : "SELECT", MU_BODY, !sel_same, mu_pressing(ib), sel_same ? MU_BTN : MU_BTN_GOLD, sel_same); mu_reg(ib, FE_OP_X0 + 6, 432, FE_OP_X1, 504); }
	static const float hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, gb[4] = { 0.1f, 0.2f, 0.22f, 1 };
	mu_rect(10, 64, 294, 510, pan); mu_rectv(10, 64, 294, 104, hd0, hd1); mu_rect(10, 104, 294, 106, MU_CYAND);
	mu_textbox(18, 64, 290, 104, MU_BODY, 0, 2, MU_WHITE, C->name);
	{ char b[16]; snprintf(b, sizeof(b), "op.face.%d", MENU_CHR_FACE[ch]); mu_imgfit(MI(b), 14, 112, 108, 206, NULL);
	  snprintf(b, sizeof(b), "op.cls.%d", cl); mu_imgfit(MI(b), 116, 116, 146, 146, NULL); mu_textbox(152, 114, 292, 148, MU_TEXT, 0, 0, MU_CYAN, FE_OP_CLASS[cl]);
	  mu_textbox(116, 150, 292, 178, MU_FINE, 0, 0, MU_DIM, "OPERATIVE"); mu_textbox(116, 178, 292, 206, MU_FINE, 0, 0, eqs ? MU_YELLOW : MU_OFF, eqs ? "SELECTED" : "AVAILABLE"); }
	static const char *gl[3] = { "FIREPOWER", "ARMOR", "MOBILITY" };
	for (int k = 0; k < 3; k++) { float y = 214 + k * 32;
		mu_textbox(18, y, 150, y + 30, MU_TEXT, 0, 0, MU_DIM, gl[k]); mu_rect(156, y + 11, 286, y + 19, gb); mu_rect(156, y + 11, 156 + 130 * FE_OP_STAT[ch][k] * msmooth(MFE.st / 0.5f), y + 19, MU_CYAN); }
	mu_rect(18, 314, 286, 316, MU_CYAND);
	{ int id = MU_ID(MU_K_BTN, 3); if (mu_pressing(id)) mu_rect(14, 318, 290, 506, MU_CYAND);   // b63: the skill block opens DETAILS (Triangle or a tap)
	  fe_skill_badge(ch, 42, 348, 26); mu_textbox(76, 322, 292, 350, MU_TEXT, 0, 0, MU_WHITE, C->skill); mu_textbox(76, 348, 292, 376, MU_FINE, 0, 0, MU_CYAN, "{t} DETAILS");
	  mu_wrap(20, 386, 266, MU_SMALL, 26, 4, MU_DIM, C->skill_msg); mu_reg(id, 14, 318, 290, 506); }
	// centre: label over the operator's head, corner marks, the skin bar (b76: the figure larger, 300-630 x 100-424; the
	// operator's SELECT moved under the roster, the bottom bar is the skins': D-pad left / right or its arrows)
	{ float x0 = 308, y0 = 118, x1 = 622, y1 = 420, k = 14; static const float cm[4] = { 0.85f, 0.95f, 0.97f, 0.8f };
	  mu_line(x0, y0, x0 + k, y0, 2, cm); mu_line(x0 + k / 2, y0 - k / 2, x0 + k / 2, y0 + k / 2, 2, cm); mu_line(x1 - k, y0, x1, y0, 2, cm); mu_line(x1 - k / 2, y0 - k / 2, x1 - k / 2, y0 + k / 2, 2, cm);
	  mu_line(x0, y1, x0 + k, y1, 2, cm); mu_line(x0 + k / 2, y1 - k / 2, x0 + k / 2, y1 + k / 2, 2, cm); mu_line(x1 - k, y1, x1, y1, 2, cm); mu_line(x1 - k / 2, y1 - k / 2, x1 - k / 2, y1 + k / 2, 2, cm);
	  fe_skill_badge(ch, 332, 88, 22); mu_line(346, 104, 390, 128, 1.5f, cm);
	  mu_textbox(360, 64, 630, 88, MU_FINE, 0, 0, MU_CYAN, C->skill); mu_textbox(360, 86, 630, 114, MU_TEXT, 0, 0, MU_WHITE, C->name); }
	{ static const float bb[4] = { 0.02f, 0.06f, 0.07f, 0.80f }; char b[48];   // the skin bar (b71 skins; b76 D-pad left / right, here at the bottom)
	  mu_rect(302, 428, 628, 508, bb);
	  if (ns > 1) { int il = MU_ID(MU_K_ROWL, 1), ir = MU_ID(MU_K_ROWR, 1);
		if (mu_pressing(il)) mu_rect(306, 432, 378, 504, MU_CYAND); if (mu_pressing(ir)) mu_rect(552, 432, 624, 504, MU_CYAND);
		mu_frame(306, 432, 378, 504, 1.5f, MU_CYAND); mu_frame(552, 432, 624, 504, 1.5f, MU_CYAND); mu_tri(342, 468, 20, 0, MU_WHITE); mu_tri(588, 468, 20, 1, MU_WHITE);
		mu_reg(il, 306, 432, 378, 504); mu_reg(ir, 552, 432, 624, 504);
		snprintf(b, sizeof(b), "{leftright} SKIN %d / %d", skv + 1, ns); }
	  else snprintf(b, sizeof(b), "SKIN");
	  int on = eqs && skv == c->skin[ch] % (ns > 0 ? ns : 1);
	  mu_textbox(384, 432, 546, 462, MU_FINE, 2, 0, MU_CYAN, b);
	  mu_textbox(384, 462, 546, 502, MU_TEXT, 2, 0, on ? MU_YELLOW : MU_WHITE, mfe_skin_name && ns > 1 ? mfe_skin_name(ch, skv) : "STANDARD"); }
	mu_a = 1;
	// b103 (user: "the option to toggle masks from operatives skins by pressing square to remove or equip them where present"):
	// Square takes the shown skin's mask / goggles off or puts them back; the prompt is in the hint line only where the skin
	// has such a part (the game's Default_Goggle / Goggle model of that outfit), and says what the press will do
	{ int gg = mfe_skin_gog && skv < 16 && mfe_skin_gog(ch, skv), off = gg && ((c->gog_off[ch] >> skv) & 1); char hb[160];
	  // (one row of 960 px: with the mask prompt the line drops OPERATIVE - the list's own heading shows the d-pad - and says TURN)
	  if (gg) snprintf(hb, sizeof(hb), "%s%slr:CLASS|R:TURN|t:DETAILS|x:SELECT|o:BACK", ns > 1 ? "H:SKIN|" : "", off ? "s:MASK ON|" : "s:MASK OFF|");
	  else snprintf(hb, sizeof(hb), "U:OPERATIVE|%slr:CLASS|R:TURN/ZOOM|t:DETAILS|x:SELECT|o:BACK", ns > 1 ? "H:SKIN|" : "");
	  fe_topbar(FE_OPERATIVES); fe_hints(hb); }
}
// ------------------------------------------------------------------ b63: DETAILS (footage "EP. 24 Azuma" 22-58 s: the operative screen's DETAILS
// window, the skill's own video playing in a loop with its description under a SKILL tab). The videos are the game's
// (ui.dat rsc\video\skill\*.usm, tools/menu/make_skill_videos.py): SKILL = the skill's gameplay video (no sound; Togusa has
// none), PROFILE = the operator's anime intro (<op>_ani.usm, with its sound; Batou, Borma, Ishikawa, Kurutsu, Maven, Motoko,
// Paz, Saito, Togusa). Played by main.c's hooks (tut_video.h, sceAvPlayer): one video at a time, closed with the window.
static int (*mfe_vid_open)(const char *file, int audio);   // VDIR file, looping; < 0 failed
static GLuint (*mfe_vid_frame)(void);                       // the newest frame (0 none yet)
static void (*mfe_vid_close)(void);
static const char *const FE_DET_TXT[12][4] = {   // Azuma's as the footage's window; the others written the same way from the skills' behaviour
	// (b83: Saito, Borma, Maven and Sitara as their v2.0 windows in the user's EP20-EP23 videos at 0:30, b84: Batou, Motoko, Ishikawa, Reiko (EP16, EP15, EP18, EP26), b85: Togusa, Kuro (EP17, EP25); Maven's "Can be repositioned" left out: not in the port yet)
	{ "- Launches a missile from a hidden launcher system in the user's arm.", "- Readying the missile launcher will disable your ability to sprint.", "- Tier 2 of this skill launches 4 missiles." },
	{ "- Renders an active camo layer that makes the wearer temporarily invisible.", "- The camo will not make you completely invisible, but remains active while moving.", "- Tier 2 of this skill will increase camo effectiveness.", "- SkillSync is available in Tier 2 of this skill." },
	{ "- Connects eyes to a satellite uplink to make enemies visible through walls.", "- If an enemy uses Heat Sensor, you will be notified.", "- Tier 2 of this skill will increase the number of scans.", "- SkillSync is available in Tier 2 of this skill." },
	{ "- Releases a hunter drone that explodes on contact with an enemy.", "- Drones can be destroyed by enemies.", "- Tier 2 of this skill releases 3 drones and adds a scrambler effect." },
	{ "- Deploys a sentry that automatically targets any enemies in range.", "- Sentries can be destroyed by enemies.", "- Tier 2 of this skill increases the detection range and HP." },
	{ "- Activates cellular repair gel to slightly increase and regenerate both armor and HP over time.", "- Tier 2 of this skill will increase the regeneration speed.", "- SkillSync is available in Tier 2 of this skill." },
	{ "- Charges forward at full speed for a short time.", "- Tier 2 of this skill lasts longer and can be shared through SkillSync.", "- Running into an enemy during the charge takes them down." },
	{ "- Projects an energy shield that blocks attacks from the front for a short time.", "- Tier 2 of this skill will increase the durability and duration of the shield and generate a stationary shield dome to protect others nearby.", "- Tactical shields can be destroyed by enemies." },
	{ "- Places an EMP Generator that disables skills and electronics of nearby enemies.", "- Tier 2 of this skill will increase the duration of the effect and the health of the generator.", "- EMP Generators can be destroyed by enemies." },
	{ "- Places a therm-optic wall capable of obscuring sight and slowing bullets.", "- Barriers can be destroyed by enemies.", "- Tier 2 of this skill increases the barrier size." },
	{ "- Hacks the vision of enemies looking at Reiko, allowing her to move unseen through the network.", "- Tier 2 of this skill will increase the duration spent in the network and create a holographic decoy in the place where she initiated the dive.", "- Holograms cannot be destroyed." },
	{ "- Launches a drone that attacks all enemies near it with high-intensity heat. The drone also heals Sitara by a small amount.", "- Tier 2 of this skill will increase the durability and attack range of the drone.", "- Siphon drones can be destroyed by enemies." } };
static int mfe_tog_skill_vid;   // b117: mv_sk_togusa.mp4 is there (main.c looks; data made before b117 has none: the PROFILE tab only, as before)
static int fe_det_has(int ch, int tab) {   // 0 SKILL, 1 PROFILE
	if (ch < 0 || ch >= 12) return 0; const char *st = MENU_CHR[ch].stem;
	if (tab == 0) return strcmp(st, "togusa") != 0 || mfe_tog_skill_vid;   /* (b117: Togusa's Seeker Drone video, when the data has it) */
	return strcmp(st, "azuma") && strcmp(st, "reiko") && strcmp(st, "sitara");
}
static void fe_det_play(void) {
	if (mfe_vid_close) mfe_vid_close(); MFE.det_tex = 0; MFE.det_t = 0; MFE.det_err = 0;
	char f[48]; snprintf(f, sizeof(f), MFE.det_tab ? "mv_op_%s.mp4" : "mv_sk_%s.mp4", MENU_CHR[MFE.det_op].stem);
	MFE.det_err = !mfe_vid_open || mfe_vid_open(f, MFE.det_tab) < 0;
}
static void fe_det_open(int ch) {
	MFE.det_on = 1; MFE.det_op = ch; MFE.det_tab = fe_det_has(ch, 0) ? 0 : 1; MENU_SFX("ui/window_open_basic"); menu_touch_lock(); fe_det_play();
}
static void fe_det_close(void) { if (mfe_vid_close) mfe_vid_close(); MFE.det_on = 0; MFE.det_tex = 0; MENU_SFX("ui/window_close_basic"); menu_touch_lock(); }
static void fe_det_tab(int t) { if (t == MFE.det_tab || !fe_det_has(MFE.det_op, t)) return; MFE.det_tab = t; MENU_SFX("ui/tab_on"); fe_det_play(); }
static void fe_quad_tex(GLuint t, float x0, float y0, float x1, float y1) {   // a GL texture over a screen rectangle (the video frame)
	static const float W[4] = { 1, 1, 1, 1 }; if (!t || !mu_tex(t, 6)) return;
	mu_emit(x0, y0, 0, 0, W); mu_emit(x1, y0, 1, 0, W); mu_emit(x1, y1, 1, 1, W); mu_emit(x0, y0, 0, 0, W); mu_emit(x1, y1, 1, 1, W); mu_emit(x0, y1, 0, 1, W);
}
// layout: the window 24..936 x 58..510; title bar 58..130 with the close button (72 x 72); the video 40..552 x 146..434 at its
// own 512 x 288; right: the SKILL / PROFILE tabs (72 tall, 12 apart), the skill's name and its three lines
static void fe_details_draw(void) {
	float a = msmooth(MFE.det_t / 0.2f); int ch = MFE.det_op; if (ch < 0 || ch >= 12) return;
	if (mfe_vid_frame) { GLuint t = mfe_vid_frame(); if (t) MFE.det_tex = t; }
	static const float dim[4] = { 0, 0, 0, 0.78f }, win[4] = { 0.02f, 0.06f, 0.08f, 0.97f }, bar[4] = { 0.06f, 0.15f, 0.18f, 1 }, blk[4] = { 0, 0, 0, 1 };
	mu_a = 1; mu_rect(0, 0, 960, 544, dim); mu_a = a;
	mu_rect(24, 58, 936, 510, win); mu_frame(24, 58, 936, 510, 2, MU_CYAND); mu_rect(24, 58, 936, 130, bar); mu_rect(24, 130, 936, 132, MU_CYAND);
	mu_textbox(44, 58, 600, 130, MU_BODY, 0, 0, MU_WHITE, "DETAILS");
	mu_textbox(300, 58, 840, 130, MU_TEXT, 1, 8, MU_CYAN, MENU_CHR[ch].name);
	{ int id = MU_ID(MU_K_BTN, 4), pr = mu_pressing(id); if (pr) mu_rect(860, 58, 936, 130, MU_CYAND); mu_frame(860, 58, 936, 130, 1.5f, MU_CYAND);
	  mu_line(880, 76, 916, 112, 3, MU_WHITE); mu_line(916, 76, 880, 112, 3, MU_WHITE); mu_reg(id, 860, 58, 936, 130); }
	mu_rect(40, 146, 552, 434, blk);
	if (MFE.det_tex) fe_quad_tex(MFE.det_tex, 40, 146, 552, 434);
	else mu_textbox(40, 146, 552, 434, MU_TEXT, 2, 0, MU_DIM, MFE.det_err ? "VIDEO NOT FOUND" : "DETAIL MOVIE");   // (the original's placeholder while it loads)
	mu_frame(40, 146, 552, 434, 1.5f, MU_CYAND);
	mu_textbox(40, 442, 552, 494, MU_FINE, 0, 0, MU_DIM, MFE.det_tab ? "OPERATIVE BACKGROUND" : "SKILL VIDEO");
	static const char *TB[2] = { "SKILL", "BACKGROUND" };   // (b83: v2.0's second tab, EP21 0:30; was PROFILE)
	for (int t = 0; t < 2; t++) { int id = MU_ID(MU_K_TAB, 40 + t), has = fe_det_has(ch, t), on = t == MFE.det_tab; float x0 = 568 + t * 184, x1 = x0 + 172;
		mu_button(x0, 146, x1, 218, TB[t], MU_BODY, on, mu_pressing(id), on ? MU_BTN_GOLD : MU_BTN, !has); if (has) mu_reg(id, x0, 146, x1, 218); }
	mu_rect(568, 232, 924, 234, MU_CYAND);
	mu_textbox(568, 240, 924, 272, MU_TEXT, 0, 0, MU_YELLOW, MENU_CHR[ch].skill);
	{ float y = 280; for (int k = 0; k < 4; k++) if (FE_DET_TXT[ch][k]) y += mu_wrap(568, y, 356, MU_FINE, 23, 5, MU_WHITE, FE_DET_TXT[ch][k]) * 23 + (k < 3 && FE_DET_TXT[ch][3] ? 4 : 8); }   // (b83: a 4th line, Saito's; the gaps 4 px then)
	mu_a = 1;
	fe_hints("lr:TAB|o:CLOSE");
}
static void fe_details_update(unsigned p, float dt) {
	MFE.det_t += dt;
	if ((p & (MB_L | MB_LEFT)) || mu_tapped(MU_ID(MU_K_TAB, 40))) fe_det_tab(0);
	if ((p & (MB_R | MB_RIGHT)) || mu_tapped(MU_ID(MU_K_TAB, 41))) fe_det_tab(1);
	if ((p & (MB_BACK | MB_TRI)) || mu_tapped(MU_ID(MU_K_BTN, 4))) fe_det_close();
}
// ------------------------------------------------------------------ armory (the original's ARMORY): WEAPONS slots (left), the weapon grid with
// category tabs (centre, scrolls), the weapon card (right: the yellow info popup of the original, docked)
static int fe_wpn_chr = -1;   // b88: the operator whose class limits the primaries listed (the loadout screen sets it; -1 = every gun)
static int fe_insp_all;       // b89 test switch inspview: every gun listed, whatever the operator's class
static int fe_wpn_list(int slot, int *out) { int n = 0; for (int k = 0; k < MENU_NWPN; k++) { int i = MENU_WPN_ORDER[k]; if (MENU_WPN[i].slot == slot && (fe_wpn_chr < 0 || menu_wpn_class_ok(fe_wpn_chr, i))) out[n++] = i; } return n; }   // (b82: MENU_WPN_ORDER)
// b89: the tabs by the v2.0 classes (LIGHT MACHINE GUN, PISTOL)
static const char *fe_wpn_tag(const MenuWpn *W) { const char *c = W->cls; return W->slot == 2 ? "MELEE" : W->slot ? "PISTOL" : !strcmp(c, "SUBMACHINE GUN") ? "SMG" : !strcmp(c, "SHOTGUN") ? "SG" : !strcmp(c, "SNIPER RIFLE") ? "SR" : !strcmp(c, "LIGHT MACHINE GUN") ? "LMG" : !strcmp(c, "PERSONAL DEFENSE WEAPON") ? "PDW" : "AR"; }
#define FE_NWCAT 7   // (b90: + PDW, the Specialists' second class)
static const char *FE_WCAT[3][FE_NWCAT] = { { "ALL", "AR", "SMG", "SR", "SG", "LMG", "PDW" }, { "ALL", "PISTOL", 0, 0, 0, 0, 0 }, { "ALL", "MELEE", 0, 0, 0, 0, 0 } };   // (b98: slot 2 = the melee weapon)
// b98: the melee weapons are MENU_WPN rows of slot 2, in order: c->melee 0 = the first (MEGATECH KNIFE), 1 = the second (DOUBLE KATAR)
static int fe_melee_wpn(int m) { int k = 0; for (int i = 0; i < MENU_NWPN; i++) if (MENU_WPN[i].slot == 2 && k++ == (m != 0)) return i; return 0; }
static int fe_slot_cur(const MenuConfig *c, int slot) { return slot == 2 ? fe_melee_wpn(c->melee) : slot ? c->secondary : c->primary; }
static void fe_slot_set(MenuConfig *c, int slot, int w) { if (slot == 2) c->melee = w == fe_melee_wpn(1); else if (slot) c->secondary = w; else c->primary = w; }
// b105 (user: "there's sections in the armory for AR, SR, LMG and PDW that are greyed out and unable to be selected"): b88 listed
// only the guns of the operative's class (the v2.0 rule: assault AR + LMG, infiltrator SMG + SG, specialist SR + PDW) and
// shut the other kinds' tabs. Every kind's tab opens now and lists all of its guns - to look at, read the numbers of and
// INSPECT; a gun of another class says whose it is (on its tile, and where EQUIP was) and cannot be equipped. ALL stays
// the operative's own guns, so what can be carried is still one tab.
static int fe_wpn_shown(int slot, int cat, int *out) {   // the slot's weapons in the category tab (0 = all the operative can carry; a kind: all of that kind)
	int chr = fe_wpn_chr; if (cat) fe_wpn_chr = -1;
	// b121 (user: "PP-2200 seems to be missing from the armory"): it was under the PDW tab alone, the last of seven - the PC's armory
	// lists it with the submachine guns (the user's SMG video: ... P-90S1, PP-2200, SEBURO C-26A ...; its panel names its class,
	// PERSONAL DEFENSE WEAPON). The SMG tab lists it too now, in that place; the PDW tab stays.
	int L[MENU_WPN_MAX], n = fe_wpn_list(slot, L), m = 0; for (int k = 0; k < n; k++) { const char *tg = fe_wpn_tag(&MENU_WPN[L[k]]), *ct = cat ? FE_WCAT[slot][cat] : NULL;
		if (!cat || !strcmp(tg, ct) || (!strcmp(ct, "SMG") && !strcmp(tg, "PDW"))) out[m++] = L[k]; }
	fe_wpn_chr = chr; return m;
}
static int fe_wpn_locked(int w) { return fe_wpn_chr >= 0 && !menu_wpn_class_ok(fe_wpn_chr, w); }   // a gun of another class
static const char *fe_wpn_owner(int w) {   // the class that carries it (the PC's names)
	const char *c = MENU_WPN[w].cls; return !strcmp(c, "ASSAULT RIFLE") || !strcmp(c, "LIGHT MACHINE GUN") ? "ASSAULT" : !strcmp(c, "SUBMACHINE GUN") || !strcmp(c, "SHOTGUN") ? "INFILTRATOR" : "SPECIALIST"; }
#define FE_WG_X0 226
#define FE_WG_X1 644
#define FE_WG_Y0 196
#define FE_WG_Y1 508
#define FE_WG_TW 203
#define FE_WG_TH 124
#define FE_WG_P (FE_WG_TH + 12)
static void fe_wtab_rect(int slot, int t, float *x0, float *x1) { int nc = slot ? 2 : FE_NWCAT; float w = (FE_WG_X1 - FE_WG_X0 - (nc - 1) * MU_GAP) / nc; *x0 = FE_WG_X0 + t * (w + MU_GAP); *x1 = *x0 + w; }
// b89: the v2.0 armory panel's rows - DAMAGE / ACCURACY / STABILITY / FIRE RATE as a bar with its number (a bar is full at 100),
// RANGE in metres, AMMO magazine / reserve - rows of rh px from y0 between x0 and x1; k: the bars' grow-in 0..1
static void fe_wpn_stats(const MenuWpn *W, float x0, float x1, float y0, float rh, float k) {
	static const char *st[6] = { "DAMAGE", "ACCURACY", "STABILITY", "FIRE RATE", "RANGE", "AMMO" }; const float v[4] = { W->dmg, W->acc, W->stab, W->rate };
	const float bx0 = x0 + (x1 - x0) * 0.44f, bx1 = x1 - 56, gb[4] = { 0.1f, 0.2f, 0.22f, 1 }, bar[4] = { 0.36f, 0.78f, 0.90f, 1 };
	if (W->slot == 2) {   // b98: a melee weapon - DAMAGE, FIRE RATE, RANGE (1.60 M), as the PC's panel
		static const int rr[3] = { 0, 3, 4 };
		for (int q = 0; q < 3; q++) { int r = rr[q]; float y = y0 + q * rh; char b[24]; mu_textbox(x0, y, bx0 - 4, y + rh, MU_TEXT, 0, 0, MU_DIM, st[r]);
			if (r < 4) { float f = fminf(1, v[r] / 100.f); mu_rect(bx0, y + rh * 0.5f - 4, bx1, y + rh * 0.5f + 4, gb); mu_rect(bx0, y + rh * 0.5f - 4, bx0 + (bx1 - bx0) * f * k, y + rh * 0.5f + 4, bar);
				snprintf(b, sizeof(b), "%d", (int)(v[r] + 0.5f)); mu_textbox(bx1 + 4, y, x1, y + rh, MU_TEXT, 1, 0, MU_WHITE, b); }
			else { snprintf(b, sizeof(b), "%.2f M", W->rng); mu_textbox(bx0, y, x1, y + rh, MU_TEXT, 0, 0, MU_WHITE, b); } }
		return; }
	for (int r = 0; r < 6; r++) { float y = y0 + r * rh; char b[24];
		mu_textbox(x0, y, bx0 - 4, y + rh, MU_TEXT, 0, 0, MU_DIM, st[r]);
		if (r < 4) { float f = fminf(1, v[r] / 100.f); mu_rect(bx0, y + rh * 0.5f - 4, bx1, y + rh * 0.5f + 4, gb); mu_rect(bx0, y + rh * 0.5f - 4, bx0 + (bx1 - bx0) * f * k, y + rh * 0.5f + 4, bar);
			snprintf(b, sizeof(b), "%d", (int)(v[r] + 0.5f)); mu_textbox(bx1 + 4, y, x1, y + rh, MU_TEXT, 1, 0, MU_WHITE, b); }
		else { if (r == 4) snprintf(b, sizeof(b), "%d M", (int)(W->rng + 0.5f)); else snprintf(b, sizeof(b), "%d / %d", W->mag, W->res);
			mu_textbox(bx0, y, x1, y + rh, MU_TEXT, 0, 0, MU_WHITE, b); }
	}
}
// b89: INSPECT (the user's armory videos: the gun alone and large, turning, beside its panel): the panel on the left, the
// gun on the right (main.c draws it: menu_fe_preview returns -3); L / R or left / right page through the guns of the tab,
// the right stick or a drag turns and tilts it
static const char *fe_wpn_sight(const MenuWpn *W) {
	const char *v = W->vm ? W->vm : W->sh;
	return !strcmp(W->cls, "SNIPER RIFLE") ? "8X RIFLESCOPE" : !strcmp(v, "scar") ? "2X COMBAT SIGHT" : !strcmp(v, "acr") || !strcmp(v, "m4a1rd") ? "RED DOT" : !strcmp(v, "kriss") ? "HOLOGRAPHIC" :
	       !strcmp(v, "c30") || !strcmp(v, "c26a") ? "BUILT-IN RED DOT" : !strcmp(v, "p90") ? "REFLEX SIGHT" : !strcmp(v, "seburomg") ? "HOLOGRAPHIC" : W->slot ? "BUILT-IN SIGHTS" : !strcmp(v, "870") ? "GHOST RING" : "IRON SIGHTS";
}
static void fe_inspect_draw(const MenuConfig *c, const int *L, int n) {
	const MenuWpn *W = &MENU_WPN[MFE.insp_w]; int cur = fe_slot_cur(c, W->slot), eq = MFE.insp_w == cur, k = 0;
	for (int i = 0; i < n; i++) if (L[i] == MFE.insp_w) k = i;
	float a = msmooth(MFE.insp_t / 0.25f); mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.80f }, hd[4] = { 0.04f, 0.11f, 0.13f, 0.92f };
	mu_rect(0, 56, 372, 510, pan); mu_rect(0, 56, 372, 216, hd); mu_rect(0, 216, 372, 218, MU_CYAND);
	mu_textbox(20, 64, 360, 92, MU_TEXT, 0, 0, MU_CYAN, W->cls);
	mu_textbox(18, 92, 364, 140, MU_H1, 0, 0, MU_WHITE, W->name);
	mu_wrap(20, 166, 340, MU_SMALL, 26, 2, MU_DIM, W->desc ? W->desc : "");
	fe_wpn_stats(W, 20, 356, 226, 32, msmooth(MFE.insp_t / 0.5f));
	mu_rect(20, 422, 356, 424, MU_CYAND);
	if (W->slot != 2) { mu_textbox(20, 428, 150, 458, MU_TEXT, 0, 0, MU_DIM, "SIGHT"); mu_textbox(150, 428, 356, 458, MU_TEXT, 0, 0, MU_WHITE, fe_wpn_sight(W)); }   // (b98: none on a blade)
	{ char b[24]; snprintf(b, sizeof(b), "%d / %d", k + 1, n); mu_textbox(20, 462, 150, 500, MU_TEXT, 0, 0, MU_DIM, b); }
	if (eq) { mu_rect(196, 466, 356, 500, MU_YELLOW); mu_textbox(196, 466, 356, 500, MU_FINE, 2, 2, MU_DARK, "EQUIPPED"); }
	else if (fe_wpn_locked(MFE.insp_w)) { char b[32]; snprintf(b, sizeof(b), "%s ONLY", fe_wpn_owner(MFE.insp_w)); mu_rect(160, 466, 356, 500, MU_DARK); mu_frame(160, 466, 356, 500, 1, MU_CYAND); mu_textbox(160, 466, 356, 500, MU_FINE, 2, 2, MU_CYAN, b); }   // b105: another class's gun
	// paging arrows either side of the gun (taps)
	{ int i0 = MU_ID(MU_K_BTN, 6), i1 = MU_ID(MU_K_BTN, 7); float cy = 300;
	  if (n > 1) { mu_tri(404, cy, 18, 0, mu_pressing(i0) ? MU_YELLOW : MU_WHITE); mu_reg(i0, 382, cy - 44, 462, cy + 44);
	               mu_tri(936, cy, 18, 1, mu_pressing(i1) ? MU_YELLOW : MU_WHITE); mu_reg(i1, 878, cy - 44, 958, cy + 44); } }
	if (menu_insp_state == 0) mu_textbox(560, 280, 860, 320, MU_TEXT, 2, 0, MU_DIM, "LOADING");
	else if (menu_insp_state < 0) { char b[16]; snprintf(b, sizeof(b), "wpi.%d", W->icon); mu_imgfit(MI(b), 470, 200, 900, 400, NULL); }
	mu_a = 1;
	fe_topbar(FE_LOADOUT); fe_hints(fe_wpn_locked(MFE.insp_w) ? (n > 1 ? "l:PREVIOUS|r:NEXT|R:TURN|o:BACK" : "R:TURN|o:BACK") : n > 1 ? "l:PREVIOUS|r:NEXT|R:TURN|x:EQUIP|o:BACK" : "R:TURN|x:EQUIP|o:BACK");
}
static void fe_loadout_draw(const MenuConfig *c) {
	fe_wpn_chr = fe_insp_all ? -1 : c->chr;   // b88: the operator's class
	float a = msmooth(MFE.st / 0.3f); int slot = MFE.wslot, L[MENU_WPN_MAX], cur[3] = { c->primary, c->secondary, fe_melee_wpn(c->melee) }, n = fe_wpn_shown(slot, MFE.wcat, L);
	if (MFE.insp && n > 0) { fe_inspect_draw(c, L, n); return; }   // b89
	int sel = MFE.sel[FE_LOADOUT]; if (sel < 0 || sel >= n) { sel = 0; for (int k = 0; k < n; k++) if (L[k] == cur[slot]) sel = k; }
	MuScroll *S = &MFE.scr[FE_LOADOUT]; float view = FE_WG_Y1 - FE_WG_Y0; S->max = fmaxf(0, ((n + 1) / 2) * FE_WG_P - 12 - view);
	mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.86f }, hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, tb[4] = { 0.04f, 0.10f, 0.12f, 0.96f };
	// grid (drawn first: the panel header and tabs cover rows scrolled under them)
	mu_rect(FE_WG_X0 - 8, 64, FE_WG_X1 + 6, 510, pan);
	mu_reg(MU_ID(MU_K_LIST, 255), FE_WG_X0, FE_WG_Y0, FE_WG_X1, FE_WG_Y1);
	for (int k = 0; k < n; k++) {
		int id = MU_ID(MU_K_LIST, k), on = k == sel, eq = L[k] == cur[slot]; const MenuWpn *W = &MENU_WPN[L[k]];
		float x0 = FE_WG_X0 + (k % 2) * (FE_WG_TW + 12), y0 = FE_WG_Y0 + (k / 2) * FE_WG_P - S->pos, x1 = x0 + FE_WG_TW, y1 = y0 + FE_WG_TH;
		float v = mu_vis(y0, y1, FE_WG_Y0, FE_WG_Y1); if (v <= 0.02f) continue; mu_a = a * v;
		static const float t0[4] = { 0.10f, 0.24f, 0.29f, 0.92f }, t1[4] = { 0.04f, 0.12f, 0.15f, 0.92f };
		mu_rectv(x0, y0, x1, y1, mu_pressing(id) ? MU_ROWSEL : t0, t1); mu_frame(x0, y0, x1, y1, on ? 2.5f : 1, on ? MU_YELLOW : MU_CYAND);
		mu_rect(x0, y0, x1, y0 + 32, on ? MU_YELLOW : MU_DARK); mu_textbox(x0, y0, x1, y0 + 32, MU_TEXT, 0, 8, on ? MU_DARK : MU_WHITE, W->name);
		{ char b[16]; snprintf(b, sizeof(b), "wpi.%d", W->icon); mu_imgfit(MI(b), x0 + 10, y0 + 36, x1 - 10, y1 - (eq || fe_wpn_locked(L[k]) ? 30 : 6), NULL); }
		if (eq) { mu_rect(x0 + 2, y1 - 28, x1 - 2, y1 - 2, MU_YELLOW); mu_textbox(x0, y1 - 28, x1, y1 - 2, MU_FINE, 2, 2, MU_DARK, "EQUIPPED"); }
		else if (fe_wpn_locked(L[k])) { mu_rect(x0 + 2, y1 - 28, x1 - 2, y1 - 2, MU_DARK); mu_textbox(x0, y1 - 28, x1, y1 - 2, MU_FINE, 2, 2, MU_CYAN, fe_wpn_owner(L[k])); }   // b105: whose it is
		mu_reg_clip(id, x0, y0, x1, y1, FE_WG_Y0, FE_WG_Y1);
	}
	mu_a = a;
	mu_rectv(FE_WG_X0 - 8, 64, FE_WG_X1 + 6, 106, hd0, hd1); mu_text(FE_WG_X0 + 6, 98, 34, 0, MU_WHITE, "ARMORY"); mu_rect(FE_WG_X0 - 8, 106, FE_WG_X1 + 6, 108, MU_CYAND);
	mu_rect(FE_WG_X0 - 8, 108, FE_WG_X1 + 6, FE_WG_Y0 - 2, tb);
	{ int nc = slot ? 2 : FE_NWCAT;
	  for (int t = 0; t < nc; t++) { float x0, x1; fe_wtab_rect(slot, t, &x0, &x1); int id = MU_ID(MU_K_TAB, 10 + t), on = t == MFE.wcat, pr = mu_pressing(id), Lt[MENU_WPN_MAX];
		int nk = t > 0 ? fe_wpn_shown(slot, t, Lt) : 1, empty = nk <= 0, other = !empty && t > 0 && fe_wpn_locked(Lt[0]);   // b105: a kind of another class is open too (its name in cyan); only a kind with no gun at all is shut
		mu_rect(x0, 112, x1, 184, on ? MU_TABSEL : pr && !empty ? MU_ROWSEL : MU_ROWV);
		mu_textbox(x0, 112, x1, 184, MU_TEXT, 2, 2, on ? MU_DARK : empty ? MU_OFF : other ? MU_CYAN : MU_WHITE, FE_WCAT[slot][t]); if (!empty) mu_reg(id, x0, 112, x1, 184); } }
	mu_scrollbar(FE_WG_X1 + 1, FE_WG_Y0 + 14, FE_WG_Y1 - 14, S, view);
	// WEAPONS: the three slots (left; b98: + MELEE - 84 px cards 12 px apart, the operative under them)
	mu_rect(10, 64, 214, 510, pan);
	mu_text(206, 96, MU_H2, 1, MU_WHITE, "WEAPONS"); mu_tri(24, 84, 14, 3, MU_WHITE);
	static const char *sl[3] = { "PRIMARY", "SECONDARY", "MELEE" };
	for (int s = 0; s < 3; s++) { float x0 = 10, x1 = 214, y0 = 112 + s * 96, y1 = y0 + 84; int id = MU_ID(MU_K_TAB, s), on = s == slot; const MenuWpn *W = &MENU_WPN[cur[s]];
		if (mu_pressing(id)) mu_rect(x0, y0, x1, y1, MU_ROWSEL); else mu_rect(x0, y0, x1, y1, MU_ROWV);
		mu_textbox(x0 + 4, y0, x1, y0 + 28, MU_TEXT, 0, 6, on ? MU_YELLOW : MU_WHITE, W->name);
		{ char b[16]; snprintf(b, sizeof(b), "wpi.%d", W->icon); mu_imgfit(MI(b), x0 + 16, y0 + 28, x1 - 10, y1 - 26, NULL); }
		if (on) { mu_frame(x0, y0, x1, y1, 2.5f, MU_YELLOW); mu_rect(x0, y1 - 26, x1, y1, MU_YELLOW); mu_tri(x1 + 8, (y0 + y1) / 2, 12, 1, MU_YELLOW); }
		else mu_rect(x0, y1 - 26, x1, y1, MU_DARK);
		mu_textbox(x0, y1 - 26, x1, y1, MU_FINE, 2, 3, on ? MU_DARK : MU_DIM, sl[s]); mu_reg(id, x0, y0, x1, y1); }
	{ char b[16]; snprintf(b, sizeof(b), "op.face.%d", MENU_CHR_FACE[c->chr]); mu_rect(16, 398, 208, 400, MU_CYAND);
	  mu_textbox(16, 402, 208, 428, MU_FINE, 0, 0, MU_CYAN, "OPERATIVE"); mu_imgfit(MI(b), 14, 432, 74, 504, NULL); mu_wrap(80, 436, 130, MU_SMALL, 26, 3, MU_WHITE, MENU_CHR[c->chr].name); }
	// the weapon card (right)
	if (n > 0) {
		const MenuWpn *W = &MENU_WPN[L[sel]]; int eq = L[sel] == cur[slot];
		mu_rect(656, 64, 952, 510, pan); mu_rect(656, 64, 952, 110, MU_YELLOW); mu_tri(670, 87, 12, 1, MU_DARK);
		mu_textbox(680, 64, 948, 110, MU_H2, 0, 0, MU_DARK, W->name);
		mu_textbox(666, 112, 946, 142, MU_TEXT, 0, 0, MU_CYAN, W->cls);
		{ char b[16]; snprintf(b, sizeof(b), "wpi.%d", W->icon); mu_imgfit(MI(b), 668, 144, 940, 204, NULL); }
		fe_wpn_stats(W, 664, 946, 210, 34, msmooth(MFE.st / 0.5f));   // (6 rows: 210-414)
		// INSPECT beside EQUIP (two 72 px targets, 12 px apart)
		{ int id = MU_ID(MU_K_BTN, 5); mu_button(664, 432, 794, 504, "INSPECT", MU_TEXT, 1, mu_pressing(id), MU_BTN, 0); mu_reg(id, 664, 432, 794, 504); }   // (the hint bar shows its button)
		if (fe_wpn_locked(L[sel])) {   // b105: another class's gun - whose it is, where EQUIP would be (two lines of 20 px)
			mu_rect(806, 432, 944, 504, MU_DARK); mu_frame(806, 432, 944, 504, 1, MU_CYAND);
			mu_textbox(806, 436, 944, 468, MU_FINE, 2, 2, MU_CYAN, fe_wpn_owner(L[sel])); mu_textbox(806, 468, 944, 500, MU_FINE, 2, 2, MU_DIM, "CLASS ONLY"); }
		else { int id = MU_ID(MU_K_BTN, 0); mu_button(806, 432, 944, 504, eq ? "EQUIPPED" : "EQUIP", eq ? MU_TEXT : MU_BODY, !eq, mu_pressing(id), eq ? MU_BTN : MU_BTN_GOLD, eq); mu_reg(id, 806, 432, 944, 504); }
	}
	mu_a = 1;
	fe_topbar(FE_LOADOUT); fe_hints(n > 0 && fe_wpn_locked(L[sel]) ? "d:MOVE|lr:SLOT|s:FILTER|t:INSPECT|o:BACK" : "d:MOVE|lr:SLOT|s:FILTER|t:INSPECT|x:EQUIP|o:BACK");
}
// ------------------------------------------------------------------ options window: CONTROLS / GRAPHICS / GAME / SOUND (the original's GameOptionWindow)
enum { OR_SECTION, OR_SLIDER, OR_STEP, OR_INFO };
typedef struct { int type; const char *label, *info; int key; } FeOpt;
enum { OK_NONE, OK_LOOK, OK_AIM, OK_INV, OK_ASSIST, OK_TOUCH, OK_SKILL, OK_TEAM, OK_TIME, OK_VM, OK_VMUS, OK_VFX, OK_VVOICE, OK_INTRO, OK_MASK, OK_BSKIN };
static const FeOpt FE_OPT_CONTROLS[] = {
	{ OR_SECTION, "CONTROLLER", 0, 0 }, { OR_SLIDER, "LOOK SENSITIVITY", 0, OK_LOOK }, { OR_SLIDER, "SENSITIVITY (IRON SIGHTS)", 0, OK_AIM },
	{ OR_STEP, "INVERT LOOK", 0, OK_INV }, { OR_STEP, "AIM ASSIST", 0, OK_ASSIST },
	{ OR_SECTION, "TOUCH SCREEN", 0, 0 }, { OR_STEP, "MENU TOUCH CONTROLS", 0, OK_TOUCH },
	{ OR_SECTION, "BUTTONS", 0, 0 }, { OR_INFO, "MOVE / LOOK", "{ls} / {rs}", 0 }, { OR_INFO, "FIRE / AIM", "{r} / {l}", 0 },
	{ OR_INFO, "JUMP / VAULT", "{x}", 0 }, { OR_INFO, "CROUCH / SLIDE", "{o}  (TOGGLE)", 0 }, { OR_INFO, "RELOAD / USE", "{s}  (HOLD TO USE)", 0 },
	{ OR_INFO, "SWITCH WEAPON", "{t}", 0 }, { OR_INFO, "SPRINT", "{down}  (TAP: ON / OFF)", 0 }, { OR_INFO, "SKILL TIER 1 (SMALL RING)", "{right}  TAP", 0 }, { OR_INFO, "SKILL TIER 2 (BIG RING)", "{right}  HOLD WHEN ORANGE", 0 },
	{ OR_INFO, "GRENADE / KNIFE", "{up} / {left}", 0 }, { OR_INFO, "TACHIKOMA CALL-IN", "{select} THEN {r}", 0 },
	{ OR_INFO, "PAUSE MENU", "{start}", 0 },
};
static const FeOpt FE_OPT_GRAPHICS[] = {
	{ OR_SECTION, "SCREEN", 0, 0 }, { OR_INFO, "RESOLUTION", "960 * 544", 0 }, { OR_INFO, "ANTI-ALIASING", "MSAA 2X", 0 }, { OR_INFO, "FRAME RATE", "60 FPS", 0 },
	{ OR_SECTION, "QUALITY", 0, 0 }, { OR_INFO, "TEXTURES", "PS VITA (DXT)", 0 }, { OR_INFO, "LIGHTING", "LIGHTMAPS + DYNAMIC", 0 },
};
static const FeOpt FE_OPT_GAME[] = {
	{ OR_SECTION, "MATCHES AGAINST BOTS", 0, 0 }, { OR_STEP, "BOT SKILL", 0, OK_SKILL }, { OR_STEP, "TEAM SIZE", 0, OK_TEAM }, { OR_STEP, "TIME LIMIT", 0, OK_TIME },
	{ OR_SECTION, "PROFILE", 0, 0 }, { OR_INFO, "NAME", 0, 1 }, { OR_INFO, "LEVEL", 0, 2 },
	{ OR_SECTION, "MATCH", 0, 0 }, { OR_STEP, "ENEMY MASKS", 0, OK_MASK }, { OR_STEP, "BOT SKINS", 0, OK_BSKIN },   // b71: the other team in the PC's red-visored masks; the bots' outfits
	{ OR_SECTION, "START-UP", 0, 0 }, { OR_STEP, "INTRO MOVIE", 0, OK_INTRO },   // b70: after the title, as the PC footage
};
static const FeOpt FE_OPT_SOUND[] = {
	// the footage's SOUND tab (Machina Media 0:44): MASTER, BGM, AMBIENT, EFFECTS, OUTPUT. The engine has one gain for ambience and
	// effect streams (audio.h amb_gain, cfg.vol_sfx) and one for voices, so: MASTER, BGM, AMBIENT / EFFECTS, VOICE, OUTPUT (stereo)
	{ OR_SLIDER, "MASTER", 0, OK_VM }, { OR_SLIDER, "BGM", 0, OK_VMUS }, { OR_SLIDER, "AMBIENT / EFFECTS", 0, OK_VFX }, { OR_SLIDER, "VOICE", 0, OK_VVOICE },
	{ OR_INFO, "OUTPUT", "STEREO", 0 },
};
static const FeOpt *FE_OPT[4] = { FE_OPT_CONTROLS, FE_OPT_GRAPHICS, FE_OPT_GAME, FE_OPT_SOUND };
static const int FE_NOPT[4] = { (int)(sizeof(FE_OPT_CONTROLS) / sizeof(FeOpt)), (int)(sizeof(FE_OPT_GRAPHICS) / sizeof(FeOpt)), (int)(sizeof(FE_OPT_GAME) / sizeof(FeOpt)), (int)(sizeof(FE_OPT_SOUND) / sizeof(FeOpt)) };
static const char *FE_OPT_TAB[4] = { "CONTROLS", "GRAPHICS", "GAME", "SOUND" };
static float *fe_opt_f(MenuConfig *c, int key) {
	switch (key) { case OK_LOOK: return &c->look_sens; case OK_AIM: return &c->aim_sens; case OK_VM: return &c->vol_master; case OK_VMUS: return &c->vol_music; case OK_VFX: return &c->vol_sfx; case OK_VVOICE: return &c->vol_voice; }
	return NULL;
}
static void fe_opt_value(const MenuConfig *c, const FeOpt *o, char *v, int n) {
	switch (o->key) {
	case OK_INV: snprintf(v, n, "%s", c->invert_y ? "ON" : "OFF"); break;
	case OK_ASSIST: snprintf(v, n, "%s", c->aim_assist ? "ON" : "OFF"); break;
	case OK_TOUCH: snprintf(v, n, "%s", c->touch_off ? "OFF" : "ON"); break;
	case OK_INTRO: snprintf(v, n, "%s", c->intro_off ? "OFF" : "ON"); break;
	case OK_MASK: snprintf(v, n, "%s", c->mask_off ? "OFF" : "ON"); break;
	case OK_BSKIN: snprintf(v, n, "%s", c->bot_skin_std ? "STANDARD" : "RANDOM"); break;
	case OK_SKILL: snprintf(v, n, "%s", MENU_SKILL[c->bot_skill & 3]); break;
	case OK_TEAM: snprintf(v, n, "%d VS %d", c->team_size, c->team_size); break;
	case OK_TIME: snprintf(v, n, "%d MIN", c->time_limit); break;
	default:
		if (o->type == OR_INFO && !o->info) { if (o->key == 1) snprintf(v, n, "%s", c->name); else snprintf(v, n, "LV %d  (EXP %d / %d)", c->level, c->exp, c->exp_next); }
		else snprintf(v, n, "%s", o->info ? o->info : "");
	}
}
static int fe_opt_step(MenuConfig *c, int key, int d) {   // returns 1 when something changed
	switch (key) {
	case OK_INV: c->invert_y ^= 1; return 1;
	case OK_ASSIST: c->aim_assist ^= 1; return 1;
	case OK_TOUCH: c->touch_off ^= 1; MUT.off = c->touch_off; return 1;
	case OK_INTRO: c->intro_off ^= 1; return 1;
	case OK_MASK: c->mask_off ^= 1; return 1;
	case OK_BSKIN: c->bot_skin_std ^= 1; return 1;
	case OK_SKILL: c->bot_skill = mwrap(c->bot_skill + d, 4); return 1;
	case OK_TEAM: c->team_size = 1 + mwrap(c->team_size - 1 + d, 4); return 1;   // b55: 1 VS 1 .. 4 VS 4
	case OK_TIME: { int k = 1; for (int i = 0; i < 4; i++) if (MENU_TIME[i] == c->time_limit) k = i; c->time_limit = MENU_TIME[mwrap(k + d, 4)]; return 1; }
	}
	float *v = fe_opt_f(c, key); if (v) { float nv = fminf(1, fmaxf(0, roundf(*v * 20 + d) / 20)); if (nv != *v) { *v = nv; return 1; } }
	return 0;
}
// the window: tabs (72 px targets, 12 px apart), the rows (scroll), DEFAULT / CLOSE. Rows you can change are 72 px tall and
// 12 px apart; section titles and information rows (nothing to tap) are shorter.
#define FE_OW_X0 30
#define FE_OW_X1 930
#define FE_OV_Y0 104
#define FE_OV_Y1 422
#define FE_OXV 470
#define FE_ORX0 (FE_OW_X0 + 16)
#define FE_ORX1 (FE_OW_X1 - 34)
#define FE_OST0 (FE_OXV + 24)                  // slider track
#define FE_OST1 (FE_ORX1 - 100)
static float fe_opt_h(int type) { return type == OR_SECTION ? 44 : type == OR_INFO ? 52 : 72; }
static float fe_opt_adv(int type) { return type == OR_SECTION ? 46 : type == OR_INFO ? 56 : 84; }
static float fe_opt_y(int tab, int i) { float y = 0; for (int k = 0; k < i; k++) y += fe_opt_adv(FE_OPT[tab][k].type); return y; }
// settings the window changes (the snapshot compares / restores only these)
static void fe_opt_restore(MenuConfig *c, const MenuConfig *o) {
	c->look_sens = o->look_sens; c->aim_sens = o->aim_sens; c->invert_y = o->invert_y; c->aim_assist = o->aim_assist; c->touch_off = o->touch_off;
	c->bot_skill = o->bot_skill; c->team_size = o->team_size; c->time_limit = o->time_limit;
	c->vol_master = o->vol_master; c->vol_music = o->vol_music; c->vol_sfx = o->vol_sfx; c->vol_voice = o->vol_voice;
}
static int fe_opt_dirty(const MenuConfig *c) {
	if (!MFE.opt_snap) return 0; MenuConfig t = MFE.opt_saved; fe_opt_restore(&t, c); return memcmp(&t, &MFE.opt_saved, sizeof(t)) != 0;
}
static void fe_otab_rect(int t, float *x0, float *x1) { float w = (FE_OW_X1 - FE_OW_X0 - 16 - 3 * MU_GAP) / 4; *x0 = FE_OW_X0 + 8 + t * (w + MU_GAP); *x1 = *x0 + w; }
static void fe_options_draw(const MenuConfig *cc, int front) {
	MenuConfig *c = (MenuConfig *)cc; int tab = MFE.ocat, sel = MFE.sel[FE_OPTIONS], n = FE_NOPT[tab]; const FeOpt *O = FE_OPT[tab];
	float a = msmooth(MFE.st / 0.2f); MuScroll *S = &MFE.scr[FE_OPTIONS];
	float view = FE_OV_Y1 - FE_OV_Y0, tot = fe_opt_y(tab, n); S->max = fmaxf(0, tot - view + 4);
	if (front) { float dim[4] = { 0, 0, 0, 0.55f }; mu_rect(0, 0, 960, 544, dim); }
	mu_reg(MU_ID(MU_K_BG, 0), 0, 0, 960, 544);   // swallow taps outside the window
	mu_a = a;
	static const float w0[4] = { 0.05f, 0.12f, 0.15f, 0.97f }, w1[4] = { 0.02f, 0.06f, 0.08f, 0.97f };
	mu_cutv(FE_OW_X0, 8, FE_OW_X1, 510, 0, 0, 0, 0, w0, w1); mu_rect(FE_OW_X0, 8, FE_OW_X1, 10, MU_CYAN); mu_frame(FE_OW_X0, 8, FE_OW_X1, 510, 1.2f, MU_CYAND);
	// rows
	mu_reg(MU_ID(MU_K_LIST, 255), FE_OW_X0, FE_OV_Y0, FE_OW_X1, FE_OV_Y1);
	for (int i = 0; i < n; i++) {
		const FeOpt *o = &O[i]; float y0 = FE_OV_Y0 + 2 + fe_opt_y(tab, i) - S->pos, y1 = y0 + fe_opt_h(o->type), v = mu_vis(y0, y1, FE_OV_Y0, FE_OV_Y1);
		if (v <= 0.02f) continue; mu_a = a * v; float x0 = FE_ORX0, x1 = FE_ORX1;
		if (o->type == OR_SECTION) { mu_textbox(x0, y0, x1, y1, MU_BODY, 0, 2, MU_CYAN, o->label); mu_rect(x0, y1 - 3, x1, y1 - 1, MU_CYAND); continue; }
		int on = i == sel; char val[64]; fe_opt_value(c, o, val, sizeof(val));
		int vis = y0 >= FE_OV_Y0 - 0.5f && y1 <= FE_OV_Y1 + 0.5f;   // fully inside the view: its targets are live
		if (o->type == OR_STEP || o->type == OR_INFO) { int nr = mu_noreg; if (!vis && !mu_noreg) mu_noreg = 1; fe_stepper_row(x0, y0, x1, y1, FE_OXV, i, o->label, val, on, o->type == OR_INFO); mu_noreg = nr; }
		else {   // slider: track, thumb, value box; the row selects, the track (inside it) follows the finger
			float *pv = fe_opt_f(c, o->key), fv = pv ? *pv : 0; int id = MU_ID(MU_K_SLIDER, i);
			mu_rect(x0, y0, FE_OXV - 2, y1, on ? MU_ROWSEL : MU_ROWL); mu_rect(FE_OXV, y0, x1, y1, MU_ROWV);
			if (on) { mu_rect(x0, y0, x0 + 5, y1, MU_CYAN); mu_frame(x0, y0, x1, y1, 1.5f, MU_CYAND); }
			mu_textbox(x0 + 8, y0, FE_OXV - 4, y1, MU_TEXT, 0, 10, on ? MU_WHITE : MU_DIM, o->label);
			float t0 = FE_OST0, t1 = FE_OST1, ym = (y0 + y1) / 2, gb[4] = { 0.12f, 0.24f, 0.27f, 1 };
			mu_rect(t0, ym - 4, t1, ym + 4, gb); mu_rect(t0, ym - 4, t0 + (t1 - t0) * fv, ym + 4, MU_CYAN);
			{ float tx = t0 + (t1 - t0) * fv; int pr = MUT.down && MUT.id == id; mu_rect(tx - 10, ym - 22, tx + 10, ym + 22, pr ? MU_WHITE : MU_TABSEL); mu_frame(tx - 10, ym - 22, tx + 10, ym + 22, 1.5f, MU_DARK); }
			{ char b[8]; snprintf(b, sizeof(b), "%d", (int)(fv * 100 + 0.5f)); mu_rect(x1 - 80, y0 + 12, x1 - 8, y1 - 12, MU_BLACK); mu_frame(x1 - 80, y0 + 12, x1 - 8, y1 - 12, 1, MU_CYAND); mu_textbox(x1 - 80, y0, x1 - 8, y1, MU_BODY, 2, 2, MU_WHITE, b); }
			if (vis) { mu_reg(MU_ID(MU_K_ROW, i), x0, y0, x1, y1); mu_reg(id, FE_OXV, y0, x1 - 84, y1); }
		}
	}
	mu_a = a;
	{ float m1[4]; mu_mix(m1, w0, w1, (FE_OV_Y0 - 8.f) / 502.f); mu_rectv(FE_OW_X0 + 1, 10, FE_OW_X1 - 1, FE_OV_Y0, w0, m1); }
	// tabs
	for (int t = 0; t < 4; t++) { float x0, x1; fe_otab_rect(t, &x0, &x1); int id = MU_ID(MU_K_TAB, t), on = t == tab, pr = mu_pressing(id);
		mu_rect(x0, 18, x1, 90, on ? MU_TABSEL : pr ? MU_ROWSEL : MU_ROWV);
		mu_textbox(x0, 18, x1, 90, MU_BODY, 2, 6, on ? MU_DARK : MU_WHITE, FE_OPT_TAB[t]); mu_reg(id, x0, 18, x1, 90); }
	mu_rect(FE_OW_X0 + 8, 96, FE_OW_X1 - 8, 98, MU_CYAND);
	{ float m0[4], m1[4]; mu_mix(m0, w0, w1, (FE_OV_Y1 - 8.f) / 502.f); mu_mix(m1, w0, w1, 1); mu_rectv(FE_OW_X0 + 1, FE_OV_Y1, FE_OW_X1 - 1, 509, m0, m1); mu_rect(FE_OW_X0 + 8, FE_OV_Y1, FE_OW_X1 - 8, FE_OV_Y1 + 2, MU_CYAND); }
	mu_scrollbar(FE_OW_X1 - 16, FE_OV_Y0 + 14, FE_OV_Y1 - 14, S, view);
	{ int idd = MU_ID(MU_K_BTN, 0), idc = MU_ID(MU_K_BTN, 1), ida = MU_ID(MU_K_BTN, 3), dirty = fe_opt_dirty(c);
	  mu_button(FE_OW_X0 + 16, 432, FE_OW_X0 + 266, 504, "DEFAULT", MU_BODY, 0, mu_pressing(idd), MU_BTN, 0); mu_reg(idd, FE_OW_X0 + 16, 432, FE_OW_X0 + 266, 504);
	  mu_button(FE_OW_X1 - 520, 432, FE_OW_X1 - 278, 504, "APPLY", MU_BODY, 0, dirty && mu_pressing(ida), MU_BTN, !dirty); if (dirty) mu_reg(ida, FE_OW_X1 - 520, 432, FE_OW_X1 - 278, 504);
	  mu_button(FE_OW_X1 - 266, 432, FE_OW_X1 - 16, 504, "CLOSE", MU_BODY, 1, mu_pressing(idc), MU_BTN, 0); mu_reg(idc, FE_OW_X1 - 266, 432, FE_OW_X1 - 16, 504); }
	mu_a = 1; fe_hints("lr:TAB|d:CHANGE|s:DEFAULT|o:CLOSE");
	if (MFE.opt_alert) { const char *keep[2]; memcpy(keep, mu_confirm_lbl, sizeof(keep)); mu_confirm_lbl[0] = "CONFIRM"; mu_confirm_lbl[1] = "CANCEL";
		mu_confirm_draw("ALERT", "Do you want to save the changed settings?", "CANCEL puts the previous settings back.", MFE.opt_csel, MU_K_CONFIRM, 1); memcpy(mu_confirm_lbl, keep, sizeof(keep)); }
	mu_a = 1;
}
static void fe_opt_close(void) { MFE.opt_snap = 0; MFE.opt_alert = 0; menu_fe_open(MFE.opt_back == FE_OPTIONS ? FE_LOBBY : MFE.opt_back); }
static int fe_options_update(MenuConfig *c, unsigned p, float dt) {
	if (!MFE.opt_snap) { MFE.opt_saved = *c; MFE.opt_snap = 1; MFE.opt_alert = 0; }   // the settings as they were when the window opened (CANCEL goes back to them)
	if (MFE.opt_alert) {   // ALERT: save the changed settings? CONFIRM keeps them, CANCEL restores the snapshot
		int k = mu_confirm_update(&MFE.opt_csel, p);
		if (k == 1) { fe_opt_close(); return FE_APPLY; }
		if (k == 0) { MenuConfig keep = *c; fe_opt_restore(c, &MFE.opt_saved); (void)keep; MUT.off = c->touch_off; fe_opt_close(); return FE_APPLY; }
		return FE_NONE;
	}
	int *sel = &MFE.sel[FE_OPTIONS], ch = 0, n = FE_NOPT[MFE.ocat]; const FeOpt *O = FE_OPT[MFE.ocat]; MuScroll *S = &MFE.scr[FE_OPTIONS];
	int t0 = MFE.ocat;
	if (p & MB_L) MFE.ocat = mwrap(MFE.ocat - 1, 4);
	if (p & MB_R) MFE.ocat = mwrap(MFE.ocat + 1, 4);
	for (int t = 0; t < 4; t++) if (MUT.press && MUT.id == MU_ID(MU_K_TAB, t)) MFE.ocat = t;
	if (MFE.ocat != t0) { if (!(p & (MB_L | MB_R))) MENU_SFX("ui/tab_on"); *sel = -1; S->pos = 0; S->vel = 0; n = FE_NOPT[MFE.ocat]; O = FE_OPT[MFE.ocat]; }
	if (*sel < 0 || *sel >= n || O[*sel].type == OR_SECTION) { int k = 0; while (k < n && O[k].type == OR_SECTION) k++; if (*sel < 0 || *sel >= n || O[*sel].type == OR_SECTION) *sel = k; }
	if (p & (MB_UP | MB_DOWN)) { int d = p & MB_UP ? -1 : 1, k = *sel; do k = mwrap(k + d, n); while (O[k].type == OR_SECTION); *sel = k;
		mu_scroll_show(S, fe_opt_y(MFE.ocat, k) - (k > 0 && O[k - 1].type == OR_SECTION ? 48 : 4), fe_opt_y(MFE.ocat, k) + fe_opt_h(O[k].type) + 6, FE_OV_Y1 - FE_OV_Y0); }
	if (p & (MB_LEFT | MB_RIGHT | MB_OK)) { const FeOpt *o = &O[*sel]; int d = p & MB_LEFT ? -1 : 1;
		if (o->type == OR_STEP) ch |= fe_opt_step(c, o->key, d); else if (o->type == OR_SLIDER && !(p & MB_OK)) ch |= fe_opt_step(c, o->key, d); }
	if (p & MB_SQR) { MenuConfig d; menu_config_default(&d);
		c->look_sens = d.look_sens; c->aim_sens = d.aim_sens; c->invert_y = d.invert_y; c->aim_assist = d.aim_assist; c->touch_off = 0; MUT.off = 0;
		c->vol_master = d.vol_master; c->vol_music = d.vol_music; c->vol_sfx = d.vol_sfx; c->vol_voice = d.vol_voice; ch = 1; }
	// touch: rows select, arrows step (and repeat while held), sliders follow the finger, the list scrolls
	int k = MU_KIND(MUT.id), i = MU_IX(MUT.id);
	if (MUT.press && (k == MU_K_ROW || k == MU_K_ROWL || k == MU_K_ROWR || k == MU_K_SLIDER) && i < n && O[i].type != OR_SECTION && *sel != i) { *sel = i; MENU_SFX("ui/mouse_over_basic"); }
	if (MUT.down && !MUT.lock && k == MU_K_SLIDER && i < n && (!MUT.moved || fabsf((float)(MUT.x - MUT.sx)) > fabsf((float)(MUT.y - MUT.sy)))) {
		float *v = fe_opt_f(c, O[i].key); float t0 = FE_OST0, t1 = FE_OST1;
		if (v) { float nv = roundf(mu_clamp((MUT.x - t0) / (t1 - t0), 0, 1) * 20) / 20; if (nv != *v) { *v = nv; ch = 1; MFE.slide_snd += 1; if (MFE.slide_snd >= 1) { MENU_SFX("ui/mouse_over_basic"); MFE.slide_snd = -1; } } }
	} else if (k == MU_K_LIST || k == MU_K_ROW || ((k == MU_K_ROWL || k == MU_K_ROWR) && MUT.moved)) mu_scroll_update(S, 1, dt);
	else mu_scroll_update(S, 0, dt);
	if (!MUT.down) MFE.slide_snd = 0;
	for (int j = 0; j < n; j++) {
		if (mu_tapped(MU_ID(MU_K_ROWL, j)) || mu_repeat(MU_ID(MU_K_ROWL, j), dt)) { ch |= fe_opt_step(c, O[j].key, -1); MENU_SFX("ui/mouse_over_basic"); }
		if (mu_tapped(MU_ID(MU_K_ROWR, j)) || mu_repeat(MU_ID(MU_K_ROWR, j), dt)) { ch |= fe_opt_step(c, O[j].key, 1); MENU_SFX("ui/mouse_over_basic"); }
		if (mu_tapped(MU_ID(MU_K_ROW, j)) && O[j].type == OR_STEP && (O[j].key == OK_INV || O[j].key == OK_ASSIST || O[j].key == OK_TOUCH)) { ch |= fe_opt_step(c, O[j].key, 1); MENU_SFX("ui/click_basic"); }
	}
	if (mu_tapped(MU_ID(MU_K_BTN, 0))) { MENU_SFX("ui/click_basic"); MenuConfig d; menu_config_default(&d);
		c->look_sens = d.look_sens; c->aim_sens = d.aim_sens; c->invert_y = d.invert_y; c->aim_assist = d.aim_assist; c->touch_off = 0; MUT.off = 0;
		c->vol_master = d.vol_master; c->vol_music = d.vol_music; c->vol_sfx = d.vol_sfx; c->vol_voice = d.vol_voice; ch = 1; }
	if (mu_tapped(MU_ID(MU_K_BTN, 3)) && fe_opt_dirty(c)) { MFE.opt_saved = *c; MENU_SFX("ui/click_basic"); ch = 1; }   // APPLY: keep the changes
	if ((p & MB_BACK) || mu_tapped(MU_ID(MU_K_BTN, 1))) { if (!(p & MB_BACK)) MENU_SFX("ui/window_close_basic");
		if (fe_opt_dirty(c)) { MFE.opt_alert = 1; MFE.opt_csel = 0; menu_touch_lock(); MENU_SFX("ui/window_open_basic"); } else fe_opt_close(); }
	return ch ? FE_APPLY : FE_NONE;
}
#include "menu_net.h"   // net: the AD-HOC screens
#include "menu_tank.h"   // b74: MY THINK TANK
// ------------------------------------------------------------------ dispatch
static void fe_init_groups(void) { if (MFE.ok) return; MFE.ok = 1; fe_op_build(); }
// the backdrop: title art on the title screen, the teal lobby backdrop elsewhere
// the front end: soft teal lights drifting along the horizon (y ~ 380-470) and a few higher, breathing
static void fe_lights_anim(float t) {
	static const float L[10][5] = {   // x, y, radius, drift px/s, phase
		{ 40, 418, 18, 6, 0.0f }, { 170, 446, 12, -4, 1.3f }, { 300, 404, 22, 5, 2.1f }, { 420, 460, 10, 7, 0.7f }, { 560, 430, 16, -5, 2.9f },
		{ 690, 452, 20, 4, 1.8f }, { 820, 412, 14, -6, 0.4f }, { 930, 440, 24, 5, 2.4f }, { 250, 300, 30, 3, 1.1f }, { 760, 260, 34, -3, 3.3f } };
	for (int i = 0; i < 10; i++) {
		float x = fmodf(L[i][0] + L[i][3] * t + 2000, 1040) - 40, br = 0.5f + 0.5f * sinf(t * 0.7f + L[i][4]), c[4] = { 0.35f, 0.85f, 0.90f, (0.05f + 0.07f * br) * (i >= 8 ? 0.6f : 1) };
		mu_soft(x, L[i][1] + 3 * sinf(t * 0.5f + L[i][4]), L[i][2], c);
	}
}
static void menu_fe_draw_bg(void) {
	if (menu_fe < 0) return;
	menu_begin(menu_fe); fe_init_groups();
	menu_bg(MFE.screen == FE_TITLE ? "title" : "lobby", 1);
	if (MFE.screen == FE_TITLE) { float d[4] = { 0, 0, 0, 0.25f }; mu_rect(0, 0, 960, 544, d); } else fe_lights_anim(MFE.t);   // b78: the backdrop's lights drift (footage)
	menu_end(); MFE.bg_drawn = 1;
}
// where the caller draws the selected operator in 3D (between menu_fe_draw_bg and menu_fe_draw): returns the operator
// index (CHARS order) or -1 for none; rect = x, y, w, h in Vita pixels (the operator stands centred in it, feet near the
// bottom); cam = eye height above the feet (cm), distance in front (cm), look-at height (cm), vertical fov (degrees);
// cam[4] = yaw of the operator in degrees (0 = facing the camera)
static float menu_fe_zoom;   // b76: the OPERATIVE screen's zoom 0 (whole figure) .. 1 (head and shoulders): main.c fig_input
static int menu_fe_preview(const MenuConfig *c, float *rect, float *cam) {
	menu_prev_shift = 0; int s = MFE.screen; if (s == FE_EXIT) s = FE_LOBBY; if (s == FE_OPTIONS) s = MFE.opt_back;
	switch (s) {
	// b76 (user: show the operator bigger in the main menu): from under the name tag (head ~118) to behind the profile plate (feet
	// ~520), ~400 px tall (was 108-444, ~250 px), as the PC's lobby shows its operator; the screens with mode cards alike
	case FE_LOBBY: rect[0] = 622; rect[1] = 56; rect[2] = 338; rect[3] = 488; cam[0] = 110; cam[1] = 371; cam[2] = 95; cam[3] = 32; cam[4] = -12 + 6 * sinf(MFE.t * 0.4f); return c->chr;
	case FE_QUICK: case FE_MATCHING: case FE_NET: case FE_NETROOM: rect[0] = 656; rect[1] = 56; rect[2] = 304; rect[3] = 488; cam[0] = 110; cam[1] = 371; cam[2] = 95; cam[3] = 32; cam[4] = -12 + 6 * sinf(MFE.t * 0.4f); return c->chr;
	case FE_OPERATIVES: { if (MFE.det_on) return -1;   // (b63: DETAILS covers the figure)
		fe_op_build(); int k = MFE.sel[FE_OPERATIVES]; if (k < 0 || k >= fe_op_n) k = fe_op_index(c->chr);
		// b76 (user: a bigger view of the operatives and their skins, zoom with the right stick): 300-630 x 100-424 (was 112-422,
		// 1.41 px / cm -> 1.72), and menu_fe_zoom 0..1 (main.c: the right stick's up / down, a vertical drag) from the whole figure
		// to the head and shoulders
		{ float z = msmooth(menu_fe_zoom); rect[0] = 300; rect[1] = 100; rect[2] = 330; rect[3] = 324;
		  cam[0] = 104 + 50 * z; cam[1] = 309 - 199 * z; cam[2] = 90 + 62 * z; cam[3] = 34; cam[4] = (-15 + 10 * sinf(MFE.t * 0.5f)) * (1 - z); }
		return fe_op_list[k]; }
	case FE_LOADOUT: if (!MFE.insp) return -1;   // b89: INSPECT - the gun right of the panel, turning slowly (cam[4] yaw, cam[2] tilt: main.c adds the stick's)
		rect[0] = 376; rect[1] = 60; rect[2] = 584; rect[3] = 446; cam[0] = cam[1] = cam[3] = 0; cam[2] = 8; cam[4] = 90 + 14 * MFE.insp_t; return -3;
	case FE_TANK: if (mtk_n[0] <= 0) return -1;   // b74: the tank on the right, under the stats (main.c tk_menu_preview: cam as the operators')
		rect[0] = 442; rect[1] = 182; rect[2] = 510; rect[3] = 248; cam[0] = 230; cam[1] = 640; cam[2] = 95; cam[3] = 30; cam[4] = 205 + 12 * sinf(MFE.t * 0.4f); return -2;
	default: return -1;
	}
}
static void fe_draw_screen(const MenuConfig *c, int s, int front) {
	switch (s) {
	case FE_TITLE: fe_title_draw(); break;
	case FE_LOBBY: fe_lobby_draw(c); break;
	case FE_PLAY: fe_lobby_draw(c); break;
	case FE_QUICK: fe_quick_draw(c); break;
	case FE_MATCHING: fe_match_draw(c); break;
	case FE_NET: fe_net_draw(c); break;
	case FE_NETHOST: fe_net_host_draw(c); break;
	case FE_NETROOM: fe_net_room_draw(c); break;
	case FE_TUTORIAL: fe_tutorial_draw(); break;
	case FE_CUSTOM: fe_custom_draw(c); break;
	case FE_MAPS: fe_maps_draw(c); break;
	case FE_OPERATIVES: fe_operatives_draw(c); if (MFE.det_on) fe_details_draw(); break;
	case FE_LOADOUT: fe_loadout_draw(c); break;
	case FE_TANK: fe_tank_draw(c); break;   // b74
	case FE_OPTIONS:
		if (front) { float st = MFE.st; MFE.st = 9; mu_noreg = 1; fe_draw_screen(c, MFE.opt_back == FE_OPTIONS ? FE_LOBBY : MFE.opt_back, 1); mu_noreg = 0; MFE.st = st; }
		fe_options_draw(c, front); break;
	case FE_EXIT:
		{ float st = MFE.st; MFE.st = 9; mu_noreg = 1; fe_lobby_draw(c); mu_noreg = 0; MFE.st = st; }
		mu_confirm_draw("EXIT GAME", "Quit Ghost in the Shell: First Assault?", "Your settings have been saved.", MFE.sel[FE_EXIT], MU_K_CONFIRM, msmooth(MFE.st / 0.2f)); break;
	}
}
static void menu_fe_draw(const MenuConfig *c) {
	if (menu_fe < 0) return;
	int front = MFE.bg_drawn; MFE.bg_drawn = 0;
	mu_draw_begin(); menu_begin(menu_fe); fe_init_groups();
	fe_draw_screen(c, MFE.screen, front);
	if (MFE.prev == FE_TITLE && MFE.screen == FE_LOBBY && MFE.st < 0.45f) { float b[4] = { 0, 0, 0, 1 - msmooth(MFE.st / 0.45f) }; mu_rect(0, 0, 960, 544, b); }   // b59: in from the title's fade-out
	menu_end();
}
// pressed / held: MB_* masks (newly pressed this frame / currently down). Returns FE_START (start a match with cfg),
// FE_QUIT (leave the game), FE_APPLY (options changed: re-read sensitivity / volume from cfg) or FE_NONE.
// b59: the START IN count's sounds (the waiting room's and the AD-HOC room's): a tick on 3, 2 and 1, the hit on 0 (once);
// left < 0: no count on screen
static int fe_cnt_last = -1;
static void fe_count_sfx(float left) {
	int n = left < 0 ? -1 : left > 0 ? (int)ceilf(left) : 0;
	if (n >= 0 && n != fe_cnt_last && (fe_cnt_last < 0 || n < fe_cnt_last)) MENU_SFX(n > 0 ? "ui/ready_count" : "ui/ready_game_start_end");
	fe_cnt_last = n;
}
static int menu_fe_update(MenuConfig *c, unsigned pressed, unsigned held, float dt) {
	MFE.t += dt; MFE.st += dt; MUT.off = c->touch_off;
	mu_in_begin(dt);
	unsigned p = menu_repeat(pressed, held, dt);
	int *sel = &MFE.sel[MFE.screen], r = FE_NONE, scr = MFE.screen;
	if (scr != FE_TITLE && scr != FE_OPTIONS && scr != FE_EXIT) p |= fe_topbar_update(c);
	if (MFE.screen != scr) { mu_in_end(); return FE_NONE; }   // the top bar opened another screen
	switch (scr) {
	case FE_TITLE:   // b59: the logo's reveal sound; a press starts the exit (fe_title_draw), the lobby comes in TL_GO later
		if (MFE.tl_go > 0) { MFE.tl_go += dt; if (MFE.tl_go >= TL_GO) { MFE.tl_go = 0; MFE.intro_req = 1; menu_fe_open(FE_LOBBY); MENU_MUSIC("bgm/lobby", 1500); MENU_SFX("ui/lobby_on"); } break; }
		if (!MFE.tl_snd && MFE.st >= 0.15f) { MFE.tl_snd = 1; MENU_SFX("ui/logo_loading"); }
		if ((p & (MB_OK | MB_START)) || mu_tapped(MU_ID(MU_K_BG, 0))) { MFE.tl_go = 1e-4f; MENU_SFX("ui/click_game_start"); MENU_SFX("ui/whoosh"); }
		break;
	case FE_LOBBY: {
		if (*sel < 0 || *sel >= LB_N) *sel = LB_QUICK;
		for (int d = 0; d < 4; d++) if (p & (d == 0 ? MB_UP : d == 1 ? MB_DOWN : d == 2 ? MB_LEFT : MB_RIGHT)) { int nx = FE_LB_NAV[*sel][d]; if (nx >= 0) *sel = nx; }
		int act = -1;   // the tiles are the lobby's buttons: a press highlights, a tap opens
		if (MUT.press && MU_KIND(MUT.id) == MU_K_ITEM && MU_IX(MUT.id) < LB_N && MU_IX(MUT.id) != *sel) { *sel = MU_IX(MUT.id); MENU_SFX("ui/mouse_over_basic"); }
		if (MUT.tap >= 0 && MU_KIND(MUT.tap) == MU_K_ITEM && MU_IX(MUT.tap) < LB_N) { act = MU_IX(MUT.tap); MENU_SFX("ui/click_basic"); }
		if (p & MB_OK) act = *sel;
		if (p & MB_BACK) { menu_fe_open(FE_EXIT); MFE.sel[FE_EXIT] = 1; break; }
		if (act >= 0) r = fe_lobby_activate(c, act);
		break; }
	case FE_PLAY: menu_fe_open(FE_LOBBY); break;   // (the lobby's tiles replaced the PLAY cards)
	case FE_TANK: r = fe_tank_update(c, p, dt); break;   // b74: MY THINK TANK (menu_tank.h)
	case FE_QUICK: {
		if (*sel < 0 || *sel >= FE_NDIVE) *sel = 0;
		if (FDV.phase == 1) {   // SCANNING...: Cross / a tap skips, Circle cancels
			FDV.t += dt;
			if (p & MB_BACK) { FDV.phase = 0; MENU_SFX("ui/window_close_basic"); break; }
			if (FDV.t >= FDV_SCAN || (p & (MB_OK | MB_START)) || mu_tapped(MU_ID(MU_K_BTN, 2))) { menu_fe_open(FE_MATCHING); FDV.phase = 2; FDV.t = 0; MENU_SFX("ui/match_success"); MENU_MUSIC("bgm/ready_room", 1200); }   // b59: MATCH FOUND, the waiting room's music
			break; }
		if (p & MB_LEFT) *sel = mwrap(*sel - 1, FE_NDIVE);
		if (p & MB_RIGHT) *sel = mwrap(*sel + 1, FE_NDIVE);
		if (p & MB_UP) *sel = *sel >= 3 ? *sel - 3 : *sel;
		if (p & MB_DOWN) *sel = *sel + 3 < FE_NDIVE ? *sel + 3 : *sel;
		if (p & MB_BACK) { menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_QUICK; break; }
		int act = fe_pick(MU_K_ITEM, sel, FE_NDIVE); if ((p & (MB_OK | MB_START)) || mu_tapped(MU_ID(MU_K_BTN, 0))) act = *sel;
		if (act >= 0) fe_dive_go(c, act);
		break; }
	case FE_MATCHING: {   // the waiting room: map vote -> auto-balance -> START IN
		FDV.t += dt; int skip = (p & (MB_OK | MB_START)) || mu_tapped(MU_ID(MU_K_BTN, 2));
		if ((p & MB_BACK) && FDV.phase < 4) { menu_fe_open(FE_QUICK); FDV.phase = 0; MENU_SFX("ui/window_close_basic"); MENU_MUSIC("bgm/lobby", 1000); break; }   // (b59: back from the waiting room's music)
		if (FDV.phase == 2) {
			if (p & (MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT)) { int v = FDV.myvote < 0 ? 0 : 1 - FDV.myvote; if (FDV.myvote >= 0) FDV.votes[FDV.myvote]--; FDV.myvote = v; FDV.votes[v]++; }
			for (int k = 0; k < 2; k++) if (mu_tapped(MU_ID(MU_K_LIST, k)) && FDV.myvote != k) { if (FDV.myvote >= 0) FDV.votes[FDV.myvote]--; FDV.myvote = k; FDV.votes[k]++; MENU_SFX("ui/click_basic"); }
			if (FDV.t >= FDV.next_vote && FDV.votes[0] + FDV.votes[1] - (FDV.myvote >= 0) < FDV.nteam * 2 - 1) { FDV.votes[fdv_rand(2)]++; FDV.next_vote = FDV.t + 0.4f + fdv_rand(8) * 0.1f; }   // the bots vote
			if (FDV.t >= FDV_VOTE || skip) fdv_end_vote();
		} else if (FDV.phase == 3) { if (FDV.t >= FDV_BAL || skip) { FDV.phase = 4; FDV.t = 0; } }   // (the count's sounds: fe_count_sfx)
		else if (FDV.phase == 4) { if (FDV.t >= FDV_COUNT + FDV_ZERO) { FDV.phase = 5; r = fe_match_start(c); } }   // (phase 5 keeps 0 on screen; fe_dive_go / fe_dive_open reset it)
		break; }
	case FE_TUTORIAL: {
		if (p & MB_LEFT) *sel = mwrap(*sel - 1, 4);
		if (p & MB_RIGHT) *sel = mwrap(*sel + 1, 4);
		if (p & MB_BACK) { menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_TUTOR; break; }
		int act = fe_pick(MU_K_ITEM, sel, 4); if ((p & (MB_OK | MB_START)) || mu_tapped(MU_ID(MU_K_BTN, 0))) act = *sel;
		if (act >= 0) r = fe_tutorial_start(c, act);
		break; }
	case FE_CUSTOM: {
		// rows in reading order: 0 mode, 1 map, 2 score, 3 time, 4 team, 5 skill, 6 START, 7 BACK
		if (p & MB_UP) *sel = *sel == 7 ? 5 : *sel == 6 ? 5 : mwrap(*sel - 1, 6);
		if (p & MB_DOWN) *sel = *sel == 5 ? 6 : *sel >= 6 ? 0 : *sel + 1;
		if (p & (MB_LEFT | MB_RIGHT)) { int d = p & MB_LEFT ? -1 : 1; if (*sel < 6) fe_custom_change(c, *sel, d); else *sel = *sel == 6 ? 7 : 6; }
		if (MUT.press) { int k = MU_KIND(MUT.id), i = MU_IX(MUT.id); if ((k == MU_K_ROW || k == MU_K_ROWL || k == MU_K_ROWR) && i < 6 && *sel != i) { *sel = i; MENU_SFX("ui/mouse_over_basic"); } }
		for (int i = 0; i < 6; i++) {
			if (mu_tapped(MU_ID(MU_K_ROWL, i)) || mu_repeat(MU_ID(MU_K_ROWL, i), dt)) { fe_custom_change(c, i, -1); MENU_SFX("ui/mouse_over_basic"); }
			if (mu_tapped(MU_ID(MU_K_ROWR, i)) || mu_repeat(MU_ID(MU_K_ROWR, i), dt)) { fe_custom_change(c, i, 1); MENU_SFX("ui/mouse_over_basic"); }
		}
		int maps = (p & MB_OK) && *sel == 1; if (mu_tapped(MU_ID(MU_K_ITEM, 0)) || mu_tapped(MU_ID(MU_K_ROW, 1))) { maps = 1; MENU_SFX("ui/window_open_basic"); }
		if (maps) { int L[MENU_NPLAY], n = menu_maps_of(c->mode, L); MFE.sel[FE_MAPS] = 0; for (int k = 0; k < n; k++) if (L[k] == c->map) MFE.sel[FE_MAPS] = k;
			MFE.scr[FE_MAPS].pos = 0; mu_scroll_show(&MFE.scr[FE_MAPS], (MFE.sel[FE_MAPS] / 3) * (float)FE_MAPS_P, (MFE.sel[FE_MAPS] / 3) * (float)FE_MAPS_P + FE_MAPS_CH, FE_MAPS_Y1 - FE_MAPS_Y0); menu_fe_open(FE_MAPS); break; }
		if ((p & MB_BACK) || mu_tapped(MU_ID(MU_K_BTN, 1))) { if (!(p & MB_BACK)) MENU_SFX("ui/character_cancel"); menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_CUSTOM; break; }
		if (mu_tapped(MU_ID(MU_K_BTN, 0)) || (p & MB_START)) { fe_custom_apply(c); menu_ld_tut = 0; MENU_SFX("ui/click_game_start"); r = FE_START; break; }
		if (p & MB_OK) { if (*sel == 7) { menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_CUSTOM; } else if (*sel == 6) { fe_custom_apply(c); menu_ld_tut = 0; r = FE_START; } else *sel = 6; }
		break; }
	case FE_MAPS: {
		int L[MENU_NPLAY], n = menu_maps_of(c->mode, L); MuScroll *S = &MFE.scr[FE_MAPS];
		if (n <= 0) { menu_fe_open(FE_CUSTOM); break; }
		if (*sel < 0 || *sel >= n) *sel = 0;
		int s0 = *sel;
		if (p & MB_LEFT) *sel = mwrap(*sel - 1, n);
		if (p & MB_RIGHT) *sel = mwrap(*sel + 1, n);
		if (p & MB_UP) *sel = *sel >= 3 ? *sel - 3 : *sel;
		if (p & MB_DOWN) *sel = *sel + 3 < n ? *sel + 3 : *sel;
		if (*sel != s0) mu_scroll_show(S, (*sel / 3) * (float)FE_MAPS_P, (*sel / 3) * (float)FE_MAPS_P + FE_MAPS_CH, FE_MAPS_Y1 - FE_MAPS_Y0);
		mu_scroll_update(S, MU_KIND(MUT.id) == MU_K_LIST, dt);
		int act = fe_pick_tap(MU_K_LIST, sel, n); if ((p & MB_OK) || mu_tapped(MU_ID(MU_K_BTN, 0))) act = *sel;
		if (act >= 0) { MFE.ci[1] = L[act]; fe_custom_apply(c); menu_fe_open(FE_CUSTOM); MFE.sel[FE_CUSTOM] = 1; }
		else if (p & MB_BACK) { menu_fe_open(FE_CUSTOM); MFE.sel[FE_CUSTOM] = 1; }
		break; }
	case FE_OPERATIVES: {
		if (MFE.det_on) { fe_details_update(p, dt); break; }   // b63: the DETAILS window has the input while it is up
		fe_op_build(); if (*sel < 0 || *sel >= fe_op_n) { *sel = fe_op_index(c->chr); MFE.scr[FE_OPERATIVES].pos = 0; mu_scroll_show(&MFE.scr[FE_OPERATIVES], fe_op_y(*sel) - FE_OP_HEAD, fe_op_y(*sel) + FE_OP_ROW, FE_OP_Y1 - FE_OP_Y0);
			memcpy(fe_skin_view, c->skin, sizeof(fe_skin_view)); }   // b76: the skins shown start as the selected ones
		int s0 = *sel; MuScroll *S = &MFE.scr[FE_OPERATIVES];
		// b76 (user): D-pad up / down = the operators, left / right = the skin shown, Cross = SELECT; L / R jump between the classes
		if (p & MB_UP) *sel = mwrap(*sel - 1, fe_op_n);
		if (p & MB_DOWN) *sel = mwrap(*sel + 1, fe_op_n);
		if (p & (MB_L | MB_R)) { int d = p & MB_L ? -1 : 1, g = mwrap(fe_op_class(fe_op_list[*sel]) + d, 3); *sel = fe_op_first(g); }
		if (*sel != s0) { mu_scroll_show(S, fe_op_y(*sel) - (*sel == fe_op_first(fe_op_class(fe_op_list[*sel])) ? FE_OP_HEAD : 0), fe_op_y(*sel) + FE_OP_ROW, FE_OP_Y1 - FE_OP_Y0); MENU_SFX("ui/character_select_mouse_over"); }
		mu_scroll_update(S, MU_KIND(MUT.id) == MU_K_LIST, dt);
		{ int ch = fe_op_list[*sel], ns = mfe_skin_n ? mfe_skin_n(ch) : 1;   // the skin shown (b71 skins)
		  int d = (p & MB_LEFT) || mu_tapped(MU_ID(MU_K_ROWL, 1)) ? -1 : (p & MB_RIGHT) || mu_tapped(MU_ID(MU_K_ROWR, 1)) ? 1 : 0;
		  if (ns > 1 && d) { fe_skin_view[ch] = (unsigned char)mwrap((fe_skin_view[ch] % ns) + d, ns); MENU_SFX("ui/weapon_change_on"); } }
		{ int ch = fe_op_list[*sel], ns = mfe_skin_n ? mfe_skin_n(ch) : 1, k = ns > 1 ? fe_skin_view[ch] % ns : 0;   // b103: Square - the shown skin's mask / goggles off / on
		  if ((p & MB_SQR) && k < 16 && mfe_skin_gog && mfe_skin_gog(ch, k)) { c->gog_off[ch] ^= (unsigned short)(1u << k); MENU_SFX("ui/weapon_change_on"); r = FE_APPLY; } }
		int act = fe_pick_tap(MU_K_LIST, sel, fe_op_n); if ((p & MB_OK) || mu_tapped(MU_ID(MU_K_BTN, 0))) act = *sel;
		if (act >= 0) { int ch = fe_op_list[act], ns = mfe_skin_n ? mfe_skin_n(ch) : 1, k = ns > 1 ? fe_skin_view[ch] % ns : 0;
			if (ch != c->chr || (ns > 1 && k != c->skin[ch] % ns)) { c->chr = ch; if (ns > 1) c->skin[ch] = (unsigned char)k; MENU_SFX("ui/character_select"); r = FE_APPLY;
				if (!menu_wpn_class_ok(ch, c->primary)) c->primary = menu_class_default(ch); } }   // b88: a primary of the new operator's class
		if ((p & MB_TRI) || mu_tapped(MU_ID(MU_K_BTN, 3))) fe_det_open(fe_op_list[*sel]);
		else if (p & MB_BACK) menu_fe_open(FE_LOBBY);
		break; }
	case FE_LOADOUT: {
		int L[MENU_WPN_MAX], n, s0w = MFE.wslot, c0 = MFE.wcat;
		if ((p & (MB_L | MB_R)) && !MFE.insp) MFE.wslot = (MFE.wslot + ((p & MB_R) ? 1 : 2)) % 3;   // (b98: three slots - R the next, L the one before)
		for (int k = 0; k < 3; k++) if (!MFE.insp && MUT.press && MUT.id == MU_ID(MU_K_TAB, k) && MFE.wslot != k) { MFE.wslot = k; MENU_SFX("ui/tab_on"); }
		if (MFE.wslot != s0w) MFE.wcat = 0;
		int ncat = MFE.wslot ? 2 : FE_NWCAT;
		fe_wpn_chr = fe_insp_all ? -1 : c->chr;
		if ((p & MB_SQR) && !MFE.insp) { int Lt[MENU_WPN_MAX]; for (int k = 0; k < ncat; k++) { MFE.wcat = mwrap(MFE.wcat + 1, ncat); if (!MFE.wcat || fe_wpn_shown(MFE.wslot, MFE.wcat, Lt)) break; } }   // (b88: past empty categories; b105: only a kind without any gun is empty)
		for (int k = 0; k < ncat; k++) if (MUT.press && MUT.id == MU_ID(MU_K_TAB, 10 + k) && MFE.wcat != k) { MFE.wcat = k; MENU_SFX("ui/tab_on"); }
		fe_wpn_chr = fe_insp_all ? -1 : c->chr; n = fe_wpn_shown(MFE.wslot, MFE.wcat, L); int cur = fe_slot_cur(c, MFE.wslot); MuScroll *S = &MFE.scr[FE_LOADOUT];   // (b88: the class's guns)
		float ph = FE_WG_P, view = FE_WG_Y1 - FE_WG_Y0;
		if (MFE.wslot != s0w || MFE.wcat != c0 || *sel < 0 || *sel >= n) { *sel = 0; for (int k = 0; k < n; k++) if (L[k] == cur) *sel = k; S->pos = 0; S->vel = 0;
			S->max = fmaxf(0, ((n + 1) / 2) * ph - 12 - view);   // b92: the new list's limit first (the draw sets it a frame later; the old tab's limit kept a far pick - the 8th pistol - out of view)
			mu_scroll_show(S, (*sel / 2) * ph, (*sel / 2) * ph + FE_WG_TH, view); }
		if (n <= 0) { MFE.insp = 0; if (p & MB_BACK) menu_fe_open(FE_LOBBY); break; }
		if (MFE.insp) {   // b89: INSPECT
			MFE.insp_t += dt; int k = 0; for (int i = 0; i < n; i++) if (L[i] == MFE.insp_w) k = i;
			int k0 = k; if ((p & (MB_L | MB_LEFT)) || mu_tapped(MU_ID(MU_K_BTN, 6))) k = (k + n - 1) % n; if ((p & (MB_R | MB_RIGHT)) || mu_tapped(MU_ID(MU_K_BTN, 7))) k = (k + 1) % n;
			if (k != k0) { MFE.insp_w = L[k]; MFE.insp_t = 0.25f; *sel = k; mu_scroll_show(S, (k / 2) * ph, (k / 2) * ph + FE_WG_TH, view); MENU_SFX("ui/mouse_over_basic"); }
			if ((p & MB_OK) && L[k] != cur && !fe_wpn_locked(L[k])) { fe_slot_set(c, MFE.wslot, L[k]); MENU_SFX("ui/weapon_change_on"); r = FE_APPLY; }
			else if ((p & MB_OK) && fe_wpn_locked(L[k])) MENU_SFX("ui/character_cancel");   // b105: another class's gun
			if (p & (MB_BACK | MB_TRI)) { MFE.insp = 0; MENU_SFX("ui/character_cancel"); }
			break; }
		int s0 = *sel;
		if (p & MB_LEFT) *sel = *sel % 2 ? *sel - 1 : *sel;
		if (p & MB_RIGHT) *sel = *sel % 2 == 0 && *sel + 1 < n ? *sel + 1 : *sel;
		if (p & MB_UP) *sel = *sel >= 2 ? *sel - 2 : *sel;
		if (p & MB_DOWN) *sel = *sel + 2 < n ? *sel + 2 : *sel;
		if (*sel != s0) mu_scroll_show(S, (*sel / 2) * ph, (*sel / 2) * ph + FE_WG_TH, view);
		mu_scroll_update(S, MU_KIND(MUT.id) == MU_K_LIST, dt);
		int act = fe_pick_tap(MU_K_LIST, sel, n); if ((p & MB_OK) || mu_tapped(MU_ID(MU_K_BTN, 0))) act = *sel;
		if (act >= 0 && L[act] != cur && !fe_wpn_locked(L[act])) { fe_slot_set(c, MFE.wslot, L[act]); MENU_SFX("ui/weapon_change_on"); r = FE_APPLY; }
		else if (act >= 0 && fe_wpn_locked(L[act]) && (p & MB_OK)) MENU_SFX("ui/character_cancel");   // b105: another class's gun: looked at, not carried
		if ((p & MB_TRI) || mu_tapped(MU_ID(MU_K_BTN, 5))) { MFE.insp = 1; MFE.insp_w = L[*sel]; MFE.insp_t = 0; MENU_SFX("ui/window_open_basic"); }   // b89
		else if (p & MB_BACK) menu_fe_open(FE_LOBBY);
		break; }
	case FE_OPTIONS: r = fe_options_update(c, p, dt); break;
	case FE_NET: case FE_NETHOST: case FE_NETROOM: r = fe_net_update(c, p, dt); break;   // net (menu_net.h)
	case FE_EXIT: {
		int k = mu_confirm_update(sel, p);
		if (k == 1) r = FE_QUIT; else if (k == 0) menu_fe_open(FE_LOBBY);
		break; }
	}
	fe_count_sfx(MFE.screen == FE_MATCHING ? (FDV.phase == 4 ? FDV_COUNT - FDV.t : FDV.phase >= 5 ? 0 : -1) : MFE.screen == FE_NETROOM ? NUV.countdown : -1);
	mu_in_end();
	return r;
}
// ------------------------------------------------------------------ the test run's scripted tour of the front-end (autoplay)
// Each screen stays up ~8 s so the test harness (a screenshot every 8 s) captures it. Most steps tap a target by its id (the
// centre of the rectangle drawn last frame), so the tour follows layout changes; swipes use screen points.
typedef struct { float t; unsigned b; int touch; float x, y; } MenuTourStep;   // touch: 0 none, 1 finger down at (x, y), 2 finger up, 3 move to (x, y), 4 down on target id x
#define TOUR_TAP(t, id) { t, 0, 4, (float)(id), 0 }, { (t) + 0.1f, 0, 2, 0, 0 }
static const MenuTourStep MENU_TOUR[] = {
	TOUR_TAP(3.5f, MU_ID(MU_K_BG, 0)),                                                   // title: tap -> lobby
	TOUR_TAP(11, MU_ID(MU_K_ITEM, LB_OPER)),                                              // OPERATIVE tile: a tap opens
	{ 13.5f, MB_DOWN, 0, 0, 0 },                                                          // b76: D-pad down = next operator
	TOUR_TAP(14.5f, MU_ID(MU_K_ROWR, 1)), { 15.5f, MB_RIGHT, 0, 0, 0 },                   // b76: the skin bar's > and D-pad right = next skins (shown, not selected)
	{ 17, 0, 1, 800, 400 }, { 17.15f, 0, 3, 800, 340 }, { 17.3f, 0, 3, 800, 260 }, { 17.45f, 0, 3, 800, 190 }, { 17.5f, 0, 2, 0, 0 },   // swipe the roster
	{ 19.5f, 0, 1, 800, 300 }, { 19.6f, 0, 2, 0, 0 },                                    // tap a roster entry (select, no equip)
	TOUR_TAP(22, MU_ID(MU_K_NAV, 1)),                                                    // top bar: ARMORY
	TOUR_TAP(25, MU_ID(MU_K_TAB, 12)),                                                   // SMG tab
	TOUR_TAP(27.5f, MU_ID(MU_K_TAB, 1)),                                                 // SECONDARY slot
	TOUR_TAP(31, MU_ID(MU_K_NAV, 2)),                                                    // top bar: options (CONTROLS)
	{ 34, 0, 1, 300, 400 }, { 34.15f, 0, 3, 300, 330 }, { 34.3f, 0, 3, 300, 230 }, { 34.45f, 0, 3, 300, 130 }, { 34.5f, 0, 2, 0, 0 },   // swipe to the buttons list
	{ 37, MB_R, 0, 0, 0 },                                                               // GRAPHICS
	TOUR_TAP(39, MU_ID(MU_K_TAB, 2)),                                                    // tap GAME
	{ 42, MB_R, 0, 0, 0 },                                                               // SOUND
	TOUR_TAP(45, MU_ID(MU_K_BTN, 1)),                                                    // CLOSE -> armory
	{ 46.5f, MB_BACK, 0, 0, 0 },                                                         // lobby
	TOUR_TAP(48, MU_ID(MU_K_ITEM, LB_TUTOR)),                                            // TUTORIAL tile -> training programs
	{ 53, MB_RIGHT, 0, 0, 0 }, { 53.4f, MB_RIGHT, 0, 0, 0 }, { 57, MB_BACK, 0, 0, 0 },   // CYBERBRAIN selected; back
	TOUR_TAP(58, MU_ID(MU_K_ITEM, LB_QUICK)),                                            // QUICK MATCH tile -> mode cards
	{ 63, MB_RIGHT, 0, 0, 0 }, { 63.4f, MB_RIGHT, 0, 0, 0 }, { 66, MB_RIGHT, 0, 0, 0 },  // TERMINAL CONQUEST, then ELIMINATION
	{ 67.5f, MB_BACK, 0, 0, 0 }, { 68, MB_RIGHT, 0, 0, 0 }, { 68.5f, MB_OK, 0, 0, 0 },   // back, CUSTOM MATCH
	TOUR_TAP(71, MU_ID(MU_K_ROWR, 0)), TOUR_TAP(72, MU_ID(MU_K_ROWR, 0)), TOUR_TAP(73, MU_ID(MU_K_ROWR, 0)),   // mode > x3 (ELIMINATION)
	TOUR_TAP(76, MU_ID(MU_K_ITEM, 0)),                                                   // tap the map: map list
	{ 83, MB_BACK, 0, 0, 0 }, { 84, MB_BACK, 0, 0, 0 },                                  // back out to the lobby
	TOUR_TAP(85.5f, MU_ID(MU_K_ITEM, LB_TANK)),                                          // b74: TACHIKOMA tile -> MY THINK TANK
	{ 88, MB_OK, 0, 0, 0 }, { 88.6f, MB_RIGHT, 0, 0, 0 }, { 89.2f, MB_OK, 0, 0, 0 },     // PN-004A legs, HL-004A hull (the stats, the tank)
	{ 92, MB_SQR, 0, 0, 0 }, { 94, MB_BACK, 0, 0, 0 },                                   // REVERT (the tour saves nothing), back
};
#define MENU_TOUR_END 96.0f
// b87 test switch opview (with autoplay): the OPERATIVE page, then every operator's every skin in turn (D-pad right every
// 1.2 s, 11 times - the most skins - then D-pad down), for screenshots of the previews; then the maps as usual
#define OPV_STEP 1.2f
#define OPV_PER_OP (11 * OPV_STEP + OPV_STEP)
static unsigned menu_fe_opview(float t, int *done) {
	static int i; unsigned b = 0;
	static const MenuTourStep head[] = { TOUR_TAP(3.5f, MU_ID(MU_K_BG, 0)), TOUR_TAP(11, MU_ID(MU_K_ITEM, LB_OPER)) };
	while (i < (int)(sizeof(head) / sizeof(head[0])) && t >= head[i].t) {
		const MenuTourStep *s = &head[i++];
		if (s->touch == 2) menu_touch_inject(1, 0, (float)MUT.ix, (float)MUT.iy);
		else if (s->touch == 4) { const MuHit *h = mu_hit_drawn((int)s->x); if (h) menu_touch_inject(1, 1, (h->x0 + h->x1) * 0.5f, (h->y0 + h->y1) * 0.5f); } }
	static int last = -1; float u = t - 14; int k = u < 0 ? -1 : (int)(u / OPV_STEP);
	if (k != last && k >= 0) { last = k; int in_op = k % 12; b = in_op == 11 ? MB_DOWN : MB_RIGHT; }
	*done = u > 12 * OPV_PER_OP; if (*done) menu_touch_inject(0, 0, 0, 0);
	return b;
}
// b89 test switch inspview (with autoplay): the armory's INSPECT view of every gun in turn (every primary, then the pistols:
// R every 2.5 s), for screenshots of the turning guns; then the maps as usual
#define INSPV_STEP 2.5f
static unsigned menu_fe_inspview(float t, int *done) {
	static int st, k, n; static float next; unsigned b = 0;
	if (st == 0 && t >= 3) { st = 1; fe_insp_all = 1; menu_fe_open(FE_LOADOUT); MFE.wslot = 0; MFE.wcat = 0; MFE.sel[FE_LOADOUT] = 0; next = t + 0.6f;
		{ FILE *f = fopen("ux0:data/gitsfa/inspmelee", "rb"); if (f) { fclose(f); MFE.wslot = 2; st = 5; } } }   // (b103 test switch inspmelee: the blades only)
	else if (st == 1 && t >= next) { int L[MENU_WPN_MAX]; fe_wpn_chr = -1; n = fe_wpn_shown(0, 0, L); MFE.insp = 1; MFE.insp_w = L[0]; MFE.insp_t = 0; st = 2; k = 1; next = t + INSPV_STEP; }
	else if (st == 2 && t >= next) { if (k < n) { b = MB_R; k++; next = t + INSPV_STEP; } else { MFE.insp = 0; MFE.wslot = 1; st = 3; next = t + 0.6f; } }
	else if (st == 3 && t >= next) { int L[MENU_WPN_MAX]; fe_wpn_chr = -1; n = fe_wpn_shown(1, 0, L); MFE.insp = 1; MFE.insp_w = L[0]; MFE.insp_t = 0; st = 4; k = 1; next = t + INSPV_STEP; }
	else if (st == 4 && t >= next) { if (k < n) { b = MB_R; k++; next = t + INSPV_STEP; } else { MFE.insp = 0; MFE.wslot = 2; st = 5; next = t + 0.6f; } }
	else if (st == 5 && t >= next) { int L[MENU_WPN_MAX]; fe_wpn_chr = -1; n = fe_wpn_shown(2, 0, L); MFE.insp = 1; MFE.insp_w = L[0]; MFE.insp_t = 0; st = 6; k = 1; next = t + INSPV_STEP; }   // (b103: and the blades)
	else if (st == 6 && t >= next) { if (k < n) { b = MB_R; k++; next = t + INSPV_STEP; } else { MFE.insp = 0; fe_insp_all = 0; st = 7; } }
	*done = st == 7; return b;
}
static unsigned menu_fe_autoplay(float t, int *done) {
	static int i, skip = -1, opv = -1, insv = -1; unsigned b = 0;
	if (skip < 0) { FILE *f = fopen("ux0:data/gitsfa/nomenutour", "rb"); skip = f != NULL; if (f) fclose(f); }   // b66: test switch: straight to the maps (emulator A/B runs of one map)
	if (opv < 0) { FILE *f = fopen("ux0:data/gitsfa/opview", "rb"); opv = f != NULL; if (f) fclose(f); }
	if (skip) { *done = 1; return 0; }
	if (opv) return menu_fe_opview(t, done);
	if (insv < 0) { FILE *f = fopen("ux0:data/gitsfa/inspview", "rb"); insv = f != NULL; if (f) fclose(f); }
	if (insv) return menu_fe_inspview(t, done);
	while (i < (int)(sizeof(MENU_TOUR) / sizeof(MENU_TOUR[0])) && t >= MENU_TOUR[i].t) {
		const MenuTourStep *s = &MENU_TOUR[i++]; b |= s->b;
		if (s->touch == 1 || s->touch == 3) menu_touch_inject(1, 1, s->x, s->y); else if (s->touch == 2) menu_touch_inject(1, 0, (float)MUT.ix, (float)MUT.iy);
		else if (s->touch == 4) { const MuHit *h = mu_hit_drawn((int)s->x); if (h) menu_touch_inject(1, 1, (h->x0 + h->x1) * 0.5f, (h->y0 + h->y1) * 0.5f); }
	}
	*done = t > MENU_TOUR_END; if (*done) menu_touch_inject(0, 0, 0, 0);
	return b;
}
