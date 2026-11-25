// PS Vita button prompts for every screen of the game (the user's "Vita prompts rule", BRIEF.md: no PC keys or mouse
// anywhere). Procedural glyphs drawn through hud.h's batch with the current pack's 'white' texel, in screen pixels
// (960x544), so any pack works (hud.ghud, dem_hud.ghud, ovl.ghud, menu.ghud) and no texture memory is needed.
//   vp_glyph(VP_SQUARE, cx, cy, 24, alpha)              one button, centred, sized for 24 px text
//   vp_text(x, y, 24, align, col, "HOLD {s} SWAP")       text with inline buttons (y = baseline; align 0 left, 1 right, 2 centre)
// Tokens (the same names as tut_ui.h): {x} {o} {s} {t} cross / circle / square / triangle, {l} {r} shoulders,
// {up} {down} {left} {right} D-pad directions, {dpad}, {start} {select}, {touch} front touch, {ls} {rs} sticks.
// The letters inside L / R / START / SELECT / stick glyphs are >= 20 px (Legibility rule). Needs only hud.h.
#pragma once
#include "hud.h"
enum { VP_CROSS, VP_CIRCLE, VP_SQUARE, VP_TRIANGLE, VP_L, VP_R, VP_START, VP_SELECT, VP_DPAD, VP_UP, VP_DOWN, VP_LEFT, VP_RIGHT, VP_TOUCH, VP_LS, VP_RS, VP_UD, VP_LR, VP_COUNT };   // b76: VP_UD / VP_LR = the D-pad with both arms of an axis lit
static const char *const VP_TOK[VP_COUNT] = { "x", "o", "s", "t", "l", "r", "start", "select", "dpad", "up", "down", "left", "right", "touch", "ls", "rs", "updown", "leftright" };
static struct { const void *pack; int nimg, white, screen; GLuint tex; float u, v; } VPS = { 0, -1, -1, -1, 0, 0, 0 };
static int vp_bind(void) {   // the current pack's 'white' texel and 'screen' node (cached per pack)
	if (!HP || !HP->img) return 0;
	if (VPS.pack != (const void *)HP || VPS.nimg != nhimg) {
		VPS.pack = HP; VPS.nimg = nhimg; VPS.white = hud_find_img("white"); VPS.screen = hud_find_node("screen");
		if (VPS.white >= 0) { const HImg *I = &himg[VPS.white]; VPS.tex = htex[I->tex]; VPS.u = (I->x + I->w * 0.5f) / htw[I->tex]; VPS.v = (I->y + I->h * 0.5f) / hth[I->tex]; }
	}
	return VPS.white >= 0 && hud_ok;
}
static void vp_emit(float x, float y, const float *c) {
	float *o = &hvb[hvn * HUD_VF], X = x + hud_dx, Y = y + hud_dy;
	o[0] = X / 480.0f - 1; o[1] = 1 - Y / 272.0f; o[2] = 0; o[3] = 1; o[4] = VPS.u; o[5] = VPS.v; o[6] = VPS.u; o[7] = VPS.v;
	o[8] = c[0]; o[9] = c[1]; o[10] = c[2]; o[11] = c[3]; o[12] = o[13] = o[14] = o[15] = 0; o[16] = 2.0f; hvn++;
}
static void vp_poly(int n, const float (*p)[2], const float *c) {   // convex polygon
	if (n < 3 || !vp_bind()) return;
	if (VPS.tex != hcur) { hud_flush(); hcur = VPS.tex; } if (hvn + (n - 2) * 3 > HUD_MAXQ * 6) hud_flush();
	for (int t = 1; t + 1 < n; t++) { vp_emit(p[0][0], p[0][1], c); vp_emit(p[t][0], p[t][1], c); vp_emit(p[t + 1][0], p[t + 1][1], c); }
}
static void vp_rect(float x0, float y0, float x1, float y1, const float *c) { float p[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }; vp_poly(4, (const float (*)[2])p, c); }
static void vp_line(float x0, float y0, float x1, float y1, float w, const float *c) {
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 1e-3f) return; float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
	float p[4][2] = { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } }; vp_poly(4, (const float (*)[2])p, c);
}
static void vp_disc(float cx, float cy, float r, const float *c) {
	float p[16][2]; for (int i = 0; i < 16; i++) { float t = i * 6.2831853f / 16; p[i][0] = cx + r * cosf(t); p[i][1] = cy + r * sinf(t); } vp_poly(16, (const float (*)[2])p, c);
}
static void vp_ring(float cx, float cy, float r0, float r1, const float *c) {
	for (int i = 0; i < 20; i++) { float a = i * 6.2831853f / 20, b = (i + 1) * 6.2831853f / 20;
		float p[4][2] = { { cx + r0 * cosf(a), cy + r0 * sinf(a) }, { cx + r1 * cosf(a), cy + r1 * sinf(a) }, { cx + r1 * cosf(b), cy + r1 * sinf(b) }, { cx + r0 * cosf(b), cy + r0 * sinf(b) } };
		vp_poly(4, (const float (*)[2])p, c); }
}
static void vp_plate(float x0, float y0, float x1, float y1, float cl, float cr, const float *bg, const float *rim) {   // a plate with cut top corners (shoulder buttons)
	float p[6][2] = { { x0, y0 + cl }, { x0 + cl, y0 }, { x1 - cr, y0 }, { x1, y0 + cr }, { x1, y1 }, { x0, y1 } }; vp_poly(6, (const float (*)[2])p, bg);
	for (int i = 0; i < 6; i++) { const float *a = p[i], *b = p[(i + 1) % 6]; if ((a[0] != b[0] || a[1] != b[1])) vp_line(a[0], a[1], b[0], b[1], 1.5f, rim); }
}
// button letters (L, R, START, SELECT) are fine print at least
static float vp_lsize(float size) { return fmaxf(20.0f, size * 0.8f); }
static float vp_glyph_w(int g, float size) {   // advance of a glyph inside text of this size (glyph + small gaps)
	float r = size * 0.6f, ls = vp_lsize(size);
	if (g == VP_L || g == VP_R) return hud_text_w("L") * ls + r * 1.3f + 6;
	if (g == VP_START || g == VP_SELECT) return hud_text_w(g == VP_START ? "START" : "SELECT") * ls + r * 0.9f + 6;
	if (g == VP_LS || g == VP_RS) return fmaxf(2 * r, hud_text_w("L") * ls + 10) + 6;
	return 2 * r + 6;
}
// one button centred at (cx, cy); size = the text size it goes with (radius 0.6 * size); a = alpha
static void vp_glyph(int g, float cx, float cy, float size, float a) {
	float r = size * 0.6f, ls = vp_lsize(size);
	const float bg[4] = { 0.02f, 0.05f, 0.06f, 0.9f * a }, rim[4] = { 0.72f, 0.80f, 0.83f, a }, wh[4] = { 0.97f, 0.98f, 1.0f, a }, dim[4] = { 0.30f, 0.36f, 0.39f, a };
	const float gc[4][4] = { { 0.50f, 0.68f, 1.0f, a }, { 1.0f, 0.42f, 0.45f, a }, { 0.98f, 0.55f, 0.85f, a }, { 0.35f, 0.92f, 0.70f, a } };
	if (g <= VP_TRIANGLE) {   // face buttons: dark disc, light rim, the coloured symbol
		vp_disc(cx, cy, r, bg); vp_ring(cx, cy, r - 1.6f, r, rim); float s = r * 0.5f, w = fmaxf(2.0f, r * 0.2f);
		if (g == VP_CROSS) { vp_line(cx - s, cy - s, cx + s, cy + s, w, gc[0]); vp_line(cx - s, cy + s, cx + s, cy - s, w, gc[0]); }
		else if (g == VP_CIRCLE) vp_ring(cx, cy, s - w * 0.5f, s + w * 0.5f, gc[1]);
		else if (g == VP_SQUARE) { float q = s * 0.85f; vp_rect(cx - q, cy - q, cx + q, cy - q + w, gc[2]); vp_rect(cx - q, cy + q - w, cx + q, cy + q, gc[2]); vp_rect(cx - q, cy - q, cx - q + w, cy + q, gc[2]); vp_rect(cx + q - w, cy - q, cx + q, cy + q, gc[2]); }
		else { float p0[2] = { cx, cy - s }, p1[2] = { cx + s * 0.95f, cy + s * 0.65f }, p2[2] = { cx - s * 0.95f, cy + s * 0.65f };
			vp_line(p0[0], p0[1], p1[0], p1[1], w, gc[3]); vp_line(p1[0], p1[1], p2[0], p2[1], w, gc[3]); vp_line(p2[0], p2[1], p0[0], p0[1], w, gc[3]); }
		return;
	}
	int nd = VPS.screen;
	if (g == VP_L || g == VP_R || g == VP_START || g == VP_SELECT) {   // shoulder plates / START / SELECT pill with the name
		const char *t = g == VP_L ? "L" : g == VP_R ? "R" : g == VP_START ? "START" : "SELECT"; float w = vp_glyph_w(g, size) - 6, h = fmaxf(r * 1.75f, ls * 1.05f);
		float x0 = cx - w / 2, x1 = cx + w / 2, y0 = cy - h / 2, y1 = cy + h / 2;
		if (g == VP_L || g == VP_R) vp_plate(x0, y0, x1, y1, g == VP_L ? h * 0.45f : 0, g == VP_R ? h * 0.45f : 0, bg, rim);
		else { float k = h * 0.5f; vp_rect(x0 + k, y0, x1 - k, y1, rim); vp_disc(x0 + k, cy, k, rim); vp_disc(x1 - k, cy, k, rim);   // a pill: light rim, dark inside
			vp_rect(x0 + k, y0 + 1.6f, x1 - k, y1 - 1.6f, bg); vp_disc(x0 + k, cy, k - 1.6f, bg); vp_disc(x1 - k, cy, k - 1.6f, bg); }
		if (nd >= 0) hud_text(nd, cx, cy + ls * 0.34f, ls, 2, wh, HUD_Z4, t);
		return;
	}
	if ((g >= VP_DPAD && g <= VP_RIGHT) || g == VP_UD || g == VP_LR) {   // the D-pad: four arms; a direction lights its arm (b76: VP_UD / VP_LR two)
		// b56 (user: the D-pad prompt read white beside the dark face buttons): black like them - a dark disc and dark arms
		// drawn in the light rim's outline; only a single direction ({right} ...) still lights its arm
		float s = r * 0.82f, w = r * 0.54f, o = 1.4f; vp_disc(cx, cy, r, bg); vp_ring(cx, cy, r - 1.6f, r, rim);
		vp_rect(cx - w / 2 - o, cy - s - o, cx + w / 2 + o, cy + s + o, rim); vp_rect(cx - s - o, cy - w / 2 - o, cx + s + o, cy + w / 2 + o, rim);   // the cross's outline
		vp_rect(cx - w / 2, cy - s, cx + w / 2, cy + s, bg); vp_rect(cx - s, cy - w / 2, cx + s, cy + w / 2, bg);
		if (g == VP_UP || g == VP_UD) vp_rect(cx - w / 2, cy - s, cx + w / 2, cy - w / 2, wh); if (g == VP_DOWN || g == VP_UD) vp_rect(cx - w / 2, cy + w / 2, cx + w / 2, cy + s, wh);
		if (g == VP_LEFT || g == VP_LR) vp_rect(cx - s, cy - w / 2, cx - w / 2, cy + w / 2, wh); if (g == VP_RIGHT || g == VP_LR) vp_rect(cx + w / 2, cy - w / 2, cx + s, cy + w / 2, wh);
		(void)dim; return;
	}
	if (g == VP_TOUCH) { vp_disc(cx, cy, r, bg); vp_ring(cx, cy, r * 0.45f, r * 0.62f, rim); vp_disc(cx, cy, r * 0.22f, rim); return; }
	if (g == VP_LS || g == VP_RS) {   // a stick: ring with L / R on the thumb pad
		float rr = (vp_glyph_w(g, size) - 6) * 0.5f; vp_disc(cx, cy, rr, bg); vp_ring(cx, cy, rr - 2.5f, rr, rim);
		if (nd >= 0) hud_text(nd, cx, cy + ls * 0.34f, ls, 2, wh, HUD_Z4, g == VP_LS ? "L" : "R");
	}
}
static int vp_tok(const char *s, int *len) {   // s points after '{'; the token, len = chars up to and including '}'
	for (int k = 0; k < VP_COUNT; k++) { size_t n = strlen(VP_TOK[k]); if (!strncmp(s, VP_TOK[k], n) && s[n] == '}') { *len = (int)n + 1; return k; } }
	return -1;
}
static float vp_text_w(const char *s, float size) {
	float w = 0; if (!s) return 0;
	while (*s) { if (*s == '{') { int l, k = vp_tok(s + 1, &l); if (k >= 0) { w += vp_glyph_w(k, size); s += l + 1; continue; } } w += hadv[(unsigned char)*s] * size; s++; }
	return w;
}
// text with inline buttons in the current pack's screen plane; y = baseline; returns the width
static float vp_text(float x, float y, float size, int align, const float *col, const char *s) {
	if (!s || !vp_bind() || VPS.screen < 0) return 0;
	float w = vp_text_w(s, size); if (align == 1) x -= w; else if (align == 2) x -= w / 2;
	char seg[200]; int n = 0;
	while (*s) {
		if (*s == '{') { int l, k = vp_tok(s + 1, &l); if (k >= 0) {
			if (n) { seg[n] = 0; hud_text(VPS.screen, x, y, size, 0, col, HUD_Z4, seg); x += hud_text_w(seg) * size; n = 0; }
			float gw = vp_glyph_w(k, size); vp_glyph(k, x + gw / 2, y - size * 0.34f, size, col[3]); x += gw; s += l + 1; continue; } }
		if (n < 199) seg[n++] = *s; s++;
	}
	if (n) { seg[n] = 0; hud_text(VPS.screen, x, y, size, 0, col, HUD_Z4, seg); }
	return w;
}
