// The weapon HUD pieces that are not part of the original HUD movie (drawn inside hud_begin / hud_end, screen node,
// 960x544): the pick-up box (bottom centre, as the original: [button] ammo / name and the gun's icon), the stealth-kill
// prompt and "Stealth kill failed". Text sizes follow the legibility rule (BRIEF.md): labels >= 24 px, fine print >= 20 px.
// Vita prompts rule: the buttons are PS Vita glyphs drawn here (same look as menu_ui.h mu_glyph: dark disc, light rim,
// the symbol in its colour): Square (pick up = hold Square), D-pad left (quick knife), L (knife out: second slash).
// Needs hud_game.h (HG, hud_img, hud_poly, hud_text). Host test: tools/weapons/hud_host.c.
#pragma once
typedef struct { int pick; const char *pick_name; int pick_ammo, pick_icon; float pick_hold; int stealth, knife_out; float fail_t; } WHud;
static void wpn_rect(float x, float y, float w, float h, const float *c) { float A[6] = { w, 0, x, 0, h, y }; hud_img(HG.white, HG.screen, A, c, HUD_Z4, -1, 1); }
static void wpn_poly(const float (*xy)[2], int n, const float *c) {   // a convex polygon in the white image's colour (<= 16 corners)
	const HImg *I = &himg[HG.white]; float u = (I->x + I->w * 0.5f) / htw[I->tex], v = (I->y + I->h * 0.5f) / hth[I->tex], uv[16][2];
	for (int k = 0; k < n && k < 16; k++) { uv[k][0] = u; uv[k][1] = v; }
	hud_poly(htex[I->tex], HG.screen, xy, (const float (*)[2])uv, n, c);
}
static void wpn_disc(float cx, float cy, float r, const float *c) { float xy[16][2]; for (int k = 0; k < 16; k++) { float a = k * 6.2831853f / 16; xy[k][0] = cx + r * cosf(a); xy[k][1] = cy + r * sinf(a); } wpn_poly((const float (*)[2])xy, 16, c); }
static void wpn_ring(float cx, float cy, float r0, float r1, const float *c) {
	for (int k = 0; k < 16; k++) { float a0 = k * 6.2831853f / 16, a1 = (k + 1) * 6.2831853f / 16;
		float q[4][2] = { { cx + r0 * cosf(a0), cy + r0 * sinf(a0) }, { cx + r1 * cosf(a0), cy + r1 * sinf(a0) }, { cx + r1 * cosf(a1), cy + r1 * sinf(a1) }, { cx + r0 * cosf(a1), cy + r0 * sinf(a1) } };
		wpn_poly((const float (*)[2])q, 4, c); }
}
enum { WG_SQUARE, WG_DPAD_LEFT, WG_L };
static const float WG_BG[4] = { 0.017f, 0.04f, 0.05f, 0.85f }, WG_RIM[4] = { 0.70f, 0.80f, 0.83f, 1 }, WG_PINK[4] = { 0.98f, 0.55f, 0.85f, 1 };
// a Vita button glyph centred at (cx, cy), radius r (the L plate is wider); returns its width
static float wpn_glyph(int g, float cx, float cy, float r) {
	if (g == WG_L) {   // shoulder button: a plate with a cut corner and the letter at label size
		float w = hud_text_w("L") * 24 + r * 1.2f, x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - r * 0.9f, y1 = cy + r * 0.9f, k = r * 0.6f;
		float P[5][2] = { { x0 + k, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 }, { x0, y0 + k } }; wpn_poly((const float (*)[2])P, 5, WG_RIM);
		float in = 2, Q[5][2] = { { x0 + k + in * 0.4f, y0 + in }, { x1 - in, y0 + in }, { x1 - in, y1 - in }, { x0 + in, y1 - in }, { x0 + in, y0 + k + in * 0.4f } }; wpn_poly((const float (*)[2])Q, 5, WG_BG);
		const float wh[4] = { 0.95f, 0.96f, 0.97f, 1 }; hud_text(HG.screen, cx, cy + 24 * 0.36f, 24, 2, wh, HUD_Z4, "L"); return w;
	}
	wpn_disc(cx, cy, r, WG_BG); wpn_ring(cx, cy, r - 1.5f, r, WG_RIM);
	if (g == WG_SQUARE) { float s = r * 0.43f, t = fmaxf(2.0f, r * 0.2f);
		wpn_rect(cx - s, cy - s, 2 * s, t, WG_PINK); wpn_rect(cx - s, cy + s - t, 2 * s, t, WG_PINK); wpn_rect(cx - s, cy - s, t, 2 * s, WG_PINK); wpn_rect(cx + s - t, cy - s, t, 2 * s, WG_PINK); }
	else {   // the D-pad cross, its left arm lit
		float s = r * 0.8f, w = r * 0.5f; const float dim[4] = { 0.45f, 0.52f, 0.55f, 1 }, lit[4] = { 1, 0.85f, 0.2f, 1 };
		wpn_rect(cx - w / 2, cy - s, w, 2 * s, dim); wpn_rect(cx - s, cy - w / 2, 2 * s, w, dim); wpn_rect(cx - s, cy - w / 2, s - w / 2, w, lit);
	}
	return 2 * r;
}
static void wpn_hud_draw(const WHud *s) {
	if (HG.screen < 0 || HG.white < 0) return;
	const float wm[4] = { 1, 1, 1, 0.95f }, dk[4] = { 0, 0.02f, 0.04f, 0.7f }, ye[4] = { 1, 0.85f, 0.2f, 1 };
	HUD_TAG("wpn.pick");   // ui b42: element names for the host overlap checker (no-op on the Vita)
	if (s->pick) {   // bottom centre, as the original's pick-up box: [key] ammo / name, the gun's icon
		// b87 (user, device b86): the box read too big on the Vita's screen - about 0.8 of b86's 300x76, the text at the
		// legibility floor (ammo 24 px label, name / HOLD 20 px fine print), the width fits the longest name
		char am[16]; snprintf(am, sizeof(am), "%d", s->pick_ammo); const char *nm = s->pick_name ? s->pick_name : "";
		const float h = 60, tx = 48, iw0 = 96, ih0 = 28;
		float w = fmaxf(hud_text_w(nm) * 20 + 8 + hud_text_w("HOLD") * 20, hud_text_w(am) * 24 + 8 + iw0); w = fmaxf(240, tx + w + 8);
		const float x = 480 - w / 2; float y = 468;
		{ float d = hud_place(x, y, x + w, y + h + 4, HUD_P_FEED, 366, 544); if (d != HUD_HIDE) y += d; else y = 366; }   // b62 (device b61-2): aiming / sprinting puts the ammo counter under the sight (main.c 475, 490), inside this box: the box goes up over it (hud_game.h claims the counter)
		hud_claim(x, y, x + w, y + h + 4, HUD_P_FEED);   // (FEED, as the counter: a higher claim would be read back next frame and push the box off itself)
		wpn_rect(x, y, w, h, dk); wpn_glyph(WG_SQUARE, x + 24, y + 30, 19);
		hud_text(HG.screen, x + tx, y + 28, 24, 0, wm, HUD_Z4, am); hud_text(HG.screen, x + tx, y + 51, 20, 0, wm, HUD_Z4, nm);
		{ char b[16]; snprintf(b, 16, "wp.%d", s->pick_icon); int im = hud_find_img(b);
		  if (im >= 0) { const float *l = himg[im].loc; float iw = l[2] - l[0], ih = l[3] - l[1];   // fit the icon into the box's top right
			if (iw > 1 && ih > 1) { float sc = fminf(iw0 / iw, ih0 / ih), C[6] = { sc, 0, x + w - 8 - iw0 + (iw0 - iw * sc) / 2 - l[0] * sc, 0, sc, y + 6 - l[1] * sc };
				const float im_m[4] = { 0, 0, 0, 0.95f }, im_a[4] = { 1, 1, 1, 0 }; hud_img(im, HG.screen, C, im_m, im_a, -1, 1); } } }   // white silhouette, as the kill feed's
		hud_text(HG.screen, x + w - 8, y + 53, 20, 1, wm, HUD_Z4, "HOLD");
		if (s->pick_hold > 0) wpn_rect(x, y + h, w * fminf(1, s->pick_hold), 4, ye);
	}
	HUD_TAG("wpn.stealth");
	if (s->stealth) {   // under the crosshair: (L) STEALTH KILL with the knife out, (D-pad left) STEALTH KILL for the quick knife
		int g = s->knife_out ? WG_L : WG_DPAD_LEFT; float gw = g == WG_L ? hud_text_w("L") * 24 + 20 * 1.2f : 40, lw = hud_text_w("STEALTH KILL") * 24, w = 10 + gw + 10 + lw + 12, x = 480 - w / 2;
		wpn_rect(x, 314, w, 48, dk); wpn_glyph(g, x + 10 + gw / 2, 338, 20);
		hud_text(HG.screen, x + 10 + gw + 10, 347, 24, 0, ye, HUD_Z4, "STEALTH KILL");
	}
	HUD_TAG("wpn.fail");
	if (s->fail_t > 0) hud_text(HG.screen, 480, 160, 28, 2, ye, HUD_Z4, "Stealth kill failed");   // tutorial: yellow, top centre
}
