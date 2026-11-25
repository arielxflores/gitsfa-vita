// Menu UI kit for menu.h: shapes, text, button glyphs and touch input, all in Vita screen pixels (960x544).
// Everything is drawn through hud.h's batch (same shader, same vertex layout) with the current pack's 'white' texel,
// so panels, tabs, arrows and gradients cost no texture memory. Touch: the draw functions register hit rectangles
// (mu_reg); the next frame's update functions test the front panel against them (mu_in_begin / mu_tap ...).
#pragma once
#include "hud.h"
#include "vita_prompt.h"
#ifdef __vita__
#include <psp2/touch.h>
#endif
// sound for touch-driven actions (buttons keep main.c's sounds): audio.h's event player when it is compiled in
#if defined(EV_COUNT) || defined(MENU_HAVE_AUDIO)
#define MENU_SFX(n) ((void)ui_sfx(n))   // b59: at the menu mix (audio.h ui_sfx_gain)
#define MENU_MUSIC(n, ms) ((void)menu_music(n, ms))   // b59: a menu music cue at the menu level (audio.h menu_music_gain)
#else
#define MENU_SFX(n) ((void)0)
#define MENU_MUSIC(n, ms) ((void)0)
#endif
#define MU_W 960
#define MU_H 544
// text sizes (em in pixels; the game font's capitals are 0.68 em tall). The user's legibility rule on the Vita's 5" screen:
// body / labels >= 24 px, secondary / fine print >= 20 px, headings 32-48 px; touch targets >= 72 px tall, >= 12 px apart.
#define MU_H1 42.0f
#define MU_H2 32.0f
#define MU_BODY 26.0f                          // prominent labels: list names, buttons, values
#define MU_TEXT 24.0f                          // body text and labels
#define MU_SMALL 22.0f                         // secondary text (descriptions)
#define MU_FINE 20.0f                          // fine print (tags, units, hints) - nothing is drawn smaller
#define MU_CAP 0.68f
#define MU_TAP 72.0f                           // minimum touch target height
#define MU_GAP 12.0f                           // minimum gap between touch targets
#define MU_BAR 60.0f                           // top bar (its targets reach down to MU_TAP)
#define MU_Y0 84.0f                            // content area: below the top bar's targets ...
#define MU_Y1 504.0f                           // ... above the hint strip (512..544)
// colours (straight alpha) of the original's menus: dark teal panels, cyan accents, light grey selected tabs, yellow notices
static const float MU_PANEL[4] = { 0.035f, 0.10f, 0.12f, 0.90f }, MU_PANEL2[4] = { 0.07f, 0.17f, 0.20f, 0.92f }, MU_ROWL[4] = { 0.09f, 0.21f, 0.25f, 0.92f },
	MU_ROWV[4] = { 0.035f, 0.09f, 0.11f, 0.92f }, MU_ROWSEL[4] = { 0.16f, 0.36f, 0.42f, 0.95f }, MU_CYAN[4] = { 0.38f, 0.82f, 0.88f, 1 }, MU_CYAND[4] = { 0.22f, 0.50f, 0.56f, 1 },
	MU_WHITE[4] = { 0.93f, 0.97f, 0.98f, 1 }, MU_DIM[4] = { 0.62f, 0.75f, 0.79f, 1 }, MU_OFF[4] = { 0.40f, 0.48f, 0.51f, 1 }, MU_DARK[4] = { 0.06f, 0.12f, 0.14f, 1 },
	MU_TABSEL[4] = { 0.78f, 0.82f, 0.83f, 1 }, MU_YELLOW[4] = { 1.0f, 0.76f, 0.0f, 1 }, MU_RED[4] = { 0.86f, 0.18f, 0.16f, 1 }, MU_BLACK[4] = { 0, 0, 0, 1 };
static float mu_a = 1;                         // alpha multiplier for everything drawn (screen fades)
static int mu_wi = -1, mu_scr = -1; static GLuint mu_wtex; static float mu_wu, mu_wv;
static void mu_bind(int wi, int scr) {         // after hud_use(): the pack's 'white' image and 'screen' node
	mu_wi = wi; mu_scr = scr;
	if (mu_wi >= 0) { const HImg *I = &himg[mu_wi]; mu_wtex = htex[I->tex]; mu_wu = (I->x + I->w * 0.5f) / htw[I->tex]; mu_wv = (I->y + I->h * 0.5f) / hth[I->tex]; }
}
static float mu_clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float mu_ease(float x) { x = mu_clamp(x, 0, 1); return x * x * (3 - 2 * x); }
static void mu_col(float *o, const float *c, float a) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3] * a; }
static void mu_mix(float *o, const float *a, const float *b, float t) { for (int k = 0; k < 4; k++) o[k] = a[k] + (b[k] - a[k]) * t; }
// ------------------------------------------------------------------ shapes
static int mu_tex(GLuint t, int nv) { if (!hud_ok) return 0; if (t != hcur) { hud_flush(); hcur = t; } if (hvn + nv > HUD_MAXQ * 6) hud_flush(); return 1; }
static void mu_emit(float x, float y, float u, float v, const float *c) {
	float *o = &hvb[hvn * HUD_VF], X = x + hud_dx, Y = y + hud_dy;
	o[0] = X / 480.0f - 1; o[1] = 1 - Y / 272.0f; o[2] = 0; o[3] = 1; o[4] = u; o[5] = v; o[6] = u; o[7] = v;
	o[8] = c[0]; o[9] = c[1]; o[10] = c[2]; o[11] = c[3] * mu_a; o[12] = o[13] = o[14] = o[15] = 0; o[16] = 2.0f; HUD_AUD_V(o); hvn++;
}
static void mu_polyc(int n, const float (*p)[2], const float (*c)[4]) {   // convex polygon, a colour per vertex
	if (n < 3 || mu_wi < 0 || !mu_tex(mu_wtex, (n - 2) * 3)) return;
#ifdef MU_AUDIT
	{ float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f, a = 1; for (int i = 0; i < n; i++) { x0 = fminf(x0, p[i][0]); y0 = fminf(y0, p[i][1]); x1 = fmaxf(x1, p[i][0]); y1 = fmaxf(y1, p[i][1]); a = fminf(a, c[i][3] * mu_a); } hud_aud_cover(x0, y0, x1, y1, a); }
#endif
	for (int t = 1; t + 1 < n; t++) { mu_emit(p[0][0], p[0][1], mu_wu, mu_wv, c[0]); mu_emit(p[t][0], p[t][1], mu_wu, mu_wv, c[t]); mu_emit(p[t + 1][0], p[t + 1][1], mu_wu, mu_wv, c[t + 1]); }
}
static void mu_poly(int n, const float (*p)[2], const float *c) {
	if (n < 3 || mu_wi < 0 || !mu_tex(mu_wtex, (n - 2) * 3)) return;
#ifdef MU_AUDIT
	{ float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; for (int i = 0; i < n; i++) { x0 = fminf(x0, p[i][0]); y0 = fminf(y0, p[i][1]); x1 = fmaxf(x1, p[i][0]); y1 = fmaxf(y1, p[i][1]); } hud_aud_cover(x0, y0, x1, y1, c[3] * mu_a); }
#endif
	for (int t = 1; t + 1 < n; t++) { mu_emit(p[0][0], p[0][1], mu_wu, mu_wv, c); mu_emit(p[t][0], p[t][1], mu_wu, mu_wv, c); mu_emit(p[t + 1][0], p[t + 1][1], mu_wu, mu_wv, c); }
}
static void mu_rect(float x0, float y0, float x1, float y1, const float *c) { float p[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }; mu_poly(4, (const float (*)[2])p, c); }
static void mu_rectv(float x0, float y0, float x1, float y1, const float *top, const float *bot) {   // vertical gradient
	float p[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, c[4][4]; memcpy(c[0], top, 16); memcpy(c[1], top, 16); memcpy(c[2], bot, 16); memcpy(c[3], bot, 16);
	mu_polyc(4, (const float (*)[2])p, (const float (*)[4])c);
}
static void mu_recth(float x0, float y0, float x1, float y1, const float *l, const float *r) {     // horizontal gradient
	float p[4][2] = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } }, c[4][4]; memcpy(c[0], l, 16); memcpy(c[1], r, 16); memcpy(c[2], r, 16); memcpy(c[3], l, 16);
	mu_polyc(4, (const float (*)[2])p, (const float (*)[4])c);
}
static void mu_frame(float x0, float y0, float x1, float y1, float w, const float *c) {
	mu_rect(x0, y0, x1, y0 + w, c); mu_rect(x0, y1 - w, x1, y1, c); mu_rect(x0, y0 + w, x0 + w, y1 - w, c); mu_rect(x1 - w, y0 + w, x1, y1 - w, c);
#ifdef MU_AUDIT
	hud_aud_add(x0, y0, x1, y1, NULL, w);
#endif
}
static void mu_line(float x0, float y0, float x1, float y1, float w, const float *c) {
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy); if (l < 1e-3f) return; float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
	float p[4][2] = { { x0 + nx, y0 + ny }, { x1 + nx, y1 + ny }, { x1 - nx, y1 - ny }, { x0 - nx, y0 - ny } }; mu_poly(4, (const float (*)[2])p, c);
}
// a panel with cut corners (top-left, top-right, bottom-right, bottom-left cut sizes), as the original's windows and buttons
static int mu_cutpts(float x0, float y0, float x1, float y1, const float *k, float (*p)[2]) {
	int n = 0;
	if (k[0] > 0) { p[n][0] = x0; p[n][1] = y0 + k[0]; n++; p[n][0] = x0 + k[0]; p[n][1] = y0; n++; } else { p[n][0] = x0; p[n][1] = y0; n++; }
	if (k[1] > 0) { p[n][0] = x1 - k[1]; p[n][1] = y0; n++; p[n][0] = x1; p[n][1] = y0 + k[1]; n++; } else { p[n][0] = x1; p[n][1] = y0; n++; }
	if (k[2] > 0) { p[n][0] = x1; p[n][1] = y1 - k[2]; n++; p[n][0] = x1 - k[2]; p[n][1] = y1; n++; } else { p[n][0] = x1; p[n][1] = y1; n++; }
	if (k[3] > 0) { p[n][0] = x0 + k[3]; p[n][1] = y1; n++; p[n][0] = x0; p[n][1] = y1 - k[3]; n++; } else { p[n][0] = x0; p[n][1] = y1; n++; }
	return n;
}
static void mu_cut(float x0, float y0, float x1, float y1, float tl, float tr, float br, float bl, const float *c) {
	float k[4] = { tl, tr, br, bl }, p[8][2]; int n = mu_cutpts(x0, y0, x1, y1, k, p); mu_poly(n, (const float (*)[2])p, c);
}
static void mu_cutv(float x0, float y0, float x1, float y1, float tl, float tr, float br, float bl, const float *top, const float *bot) {
	float k[4] = { tl, tr, br, bl }, p[8][2], c[8][4]; int n = mu_cutpts(x0, y0, x1, y1, k, p);
	for (int i = 0; i < n; i++) mu_mix(c[i], top, bot, (p[i][1] - y0) / (y1 - y0 > 0 ? y1 - y0 : 1));
	mu_polyc(n, (const float (*)[2])p, (const float (*)[4])c);
}
static void mu_cutline(float x0, float y0, float x1, float y1, float tl, float tr, float br, float bl, float w, const float *c) {   // outline
	float k[4] = { tl, tr, br, bl }, p[8][2]; int n = mu_cutpts(x0, y0, x1, y1, k, p);
	for (int i = 0; i < n; i++) { const float *a = p[i], *b = p[(i + 1) % n]; mu_line(a[0], a[1], b[0], b[1], w, c); }
#ifdef MU_AUDIT
	hud_aud_add(x0, y0, x1, y1, k, w);
#endif
}
static void mu_disc(float cx, float cy, float r, const float *c) {
	float p[12][2]; for (int i = 0; i < 12; i++) { float t = i * 6.2831853f / 12; p[i][0] = cx + r * cosf(t); p[i][1] = cy + r * sinf(t); } mu_poly(12, (const float (*)[2])p, c);
}
static void mu_ring(float cx, float cy, float r0, float r1, const float *c) {
	for (int i = 0; i < 16; i++) { float a = i * 6.2831853f / 16, b = (i + 1) * 6.2831853f / 16;
		float p[4][2] = { { cx + r0 * cosf(a), cy + r0 * sinf(a) }, { cx + r1 * cosf(a), cy + r1 * sinf(a) }, { cx + r1 * cosf(b), cy + r1 * sinf(b) }, { cx + r0 * cosf(b), cy + r0 * sinf(b) } };
		mu_poly(4, (const float (*)[2])p, c); }
}
// b78 (user: "the menu background animated like in the footage"; #49 0:13-0:40 frame differences: the result tabs' rings turn
// round the emblem and the hex floor shimmers; the lobby's teal lights drift, low behind the operator): arcs turning on the
// rings, the hexes breathing, soft lights drifting along the backdrop's horizon - a few hundred triangles of the menu's own
// white image, no textures
static void mu_arc(float cx, float cy, float r0, float r1, float a0, float a1, const float *c0, const float *c1) {   // degrees; colour c0 at a0 .. c1 at a1
	int n = (int)ceilf(fabsf(a1 - a0) / 8); if (n < 1) n = 1; if (n > 45) n = 45;
	for (int i = 0; i < n; i++) {
		float t0 = (float)i / n, t1 = (float)(i + 1) / n, a = (a0 + (a1 - a0) * t0) * 0.01745329f, b = (a0 + (a1 - a0) * t1) * 0.01745329f, ca[4], cb[4];
		mu_mix(ca, c0, c1, t0); mu_mix(cb, c0, c1, t1);
		float p[4][2] = { { cx + r0 * cosf(a), cy + r0 * sinf(a) }, { cx + r1 * cosf(a), cy + r1 * sinf(a) }, { cx + r1 * cosf(b), cy + r1 * sinf(b) }, { cx + r0 * cosf(b), cy + r0 * sinf(b) } };
		float c[4][4]; memcpy(c[0], ca, 16); memcpy(c[1], ca, 16); memcpy(c[2], cb, 16); memcpy(c[3], cb, 16); mu_polyc(4, (const float (*)[2])p, (const float (*)[4])c);
	}
}
static void mu_soft(float cx, float cy, float r, const float *c) {   // a soft light: three discs, the alpha building to the middle
	float k[4]; for (int i = 0; i < 3; i++) { mu_col(k, c, 0.34f); mu_disc(cx, cy, r * (1 - 0.3f * i), k); }
}
static void mu_tri(float cx, float cy, float s, int dir, const float *c) {   // solid arrow head: dir 0 left, 1 right, 2 up, 3 down
	float p[3][2];
	if (dir == 0) { p[0][0] = cx - s * 0.5f; p[0][1] = cy; p[1][0] = cx + s * 0.4f; p[1][1] = cy - s * 0.55f; p[2][0] = cx + s * 0.4f; p[2][1] = cy + s * 0.55f; }
	else if (dir == 1) { p[0][0] = cx + s * 0.5f; p[0][1] = cy; p[1][0] = cx - s * 0.4f; p[1][1] = cy + s * 0.55f; p[2][0] = cx - s * 0.4f; p[2][1] = cy - s * 0.55f; }
	else if (dir == 2) { p[0][0] = cx; p[0][1] = cy - s * 0.5f; p[1][0] = cx + s * 0.55f; p[1][1] = cy + s * 0.4f; p[2][0] = cx - s * 0.55f; p[2][1] = cy + s * 0.4f; }
	else { p[0][0] = cx; p[0][1] = cy + s * 0.5f; p[1][0] = cx - s * 0.55f; p[1][1] = cy - s * 0.4f; p[2][0] = cx + s * 0.55f; p[2][1] = cy - s * 0.4f; }
	mu_poly(3, (const float (*)[2])p, c);
}
// ------------------------------------------------------------------ images (parts of the current pack) in screen rectangles
static void mu_img(int im, float x0, float y0, float x1, float y1, const float *mul) {
	if (im < 0 || mu_scr < 0) return; const float *l = himg[im].loc; float sx = (x1 - x0) / (l[2] - l[0]), sy = (y1 - y0) / (l[3] - l[1]);
	float A[6] = { sx, 0, x0 - l[0] * sx, 0, sy, y0 - l[1] * sy }, m[4] = { 1, 1, 1, mu_a }; if (mul) { m[0] = mul[0]; m[1] = mul[1]; m[2] = mul[2]; m[3] = mul[3] * mu_a; }
	hud_img(im, mu_scr, A, m, HUD_Z4, -1, 1);
}
static void mu_imgfit(int im, float x0, float y0, float x1, float y1, const float *mul) {   // keep the aspect, centred
	if (im < 0) return; const float *l = himg[im].loc; float w = l[2] - l[0], h = l[3] - l[1], s = fminf((x1 - x0) / w, (y1 - y0) / h), cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
	mu_img(im, cx - w * s / 2, cy - h * s / 2, cx + w * s / 2, cy + h * s / 2, mul);
}
static void mu_imgcover(int im, float x0, float y0, float x1, float y1, const float *mul) {   // fill the rect, cropping the image (centre)
	if (im < 0 || !mu_tex(htex[himg[im].tex], 6)) return; const HImg *I = &himg[im]; const float *l = I->loc; float w = l[2] - l[0], h = l[3] - l[1];
	float ra = (x1 - x0) / (y1 - y0), u0 = 0, u1 = 1, v0 = 0, v1 = 1; if (w / h > ra) { float k = ra / (w / h); u0 = 0.5f - k / 2; u1 = 0.5f + k / 2; } else { float k = (w / h) / ra; v0 = 0.5f - k / 2; v1 = 0.5f + k / 2; }
	float tw = (float)htw[I->tex], th = (float)hth[I->tex], U0 = (I->x + u0 * I->w) / tw, U1 = (I->x + u1 * I->w) / tw, V0 = (I->y + v0 * I->h) / th, V1 = (I->y + v1 * I->h) / th;
	float m[4] = { 1, 1, 1, 1 }; if (mul) memcpy(m, mul, 16);
	mu_emit(x0, y0, U0, V0, m); mu_emit(x1, y0, U1, V0, m); mu_emit(x1, y1, U1, V1, m); mu_emit(x0, y0, U0, V0, m); mu_emit(x1, y1, U1, V1, m); mu_emit(x0, y1, U0, V1, m);
}
// horizontal 3-slice: the image's left cap (cl of its width) and right cap (cr) keep their aspect, the middle stretches
static void mu_img3(int im, float x0, float y0, float x1, float y1, float cl, float cr, const float *mul) {
	if (im < 0 || !mu_tex(htex[himg[im].tex], 18)) return; const HImg *I = &himg[im];
	float tw = (float)htw[I->tex], th = (float)hth[I->tex], h = y1 - y0, ar = (float)I->w / I->h, wl = cl * ar * h, wr = cr * ar * h;
	if (wl + wr > x1 - x0) { float k = (x1 - x0) / (wl + wr); wl *= k; wr *= k; }
	float xs[4] = { x0, x0 + wl, x1 - wr, x1 }, us[4] = { 0, cl, 1 - cr, 1 }, m[4] = { 1, 1, 1, 1 }; if (mul) memcpy(m, mul, 16);
	float V0 = (I->y + 0.5f) / th, V1 = (I->y + I->h - 0.5f) / th;
	for (int s = 0; s < 3; s++) { float U0 = (I->x + us[s] * I->w) / tw, U1 = (I->x + us[s + 1] * I->w) / tw;
		mu_emit(xs[s], y0, U0, V0, m); mu_emit(xs[s + 1], y0, U1, V0, m); mu_emit(xs[s + 1], y1, U1, V1, m);
		mu_emit(xs[s], y0, U0, V0, m); mu_emit(xs[s + 1], y1, U1, V1, m); mu_emit(xs[s], y1, U0, V1, m); }
}
// ------------------------------------------------------------------ text (the pack's font)
static float mu_tw(const char *s, float size) { return hud_text_w(s) * size; }
static void mu_text(float x, float y, float size, int align, const float *c, const char *s) {   // y = baseline; align 0 left, 1 right, 2 centre
	if (!s || mu_scr < 0) return; float m[4] = { c[0], c[1], c[2], c[3] * mu_a }; hud_text(mu_scr, x, y, size, align, m, HUD_Z4, s);
}
// text centred vertically in a box, aligned horizontally inside it (pad from the sides); shrinks to fit the width
// (may hold Vita button tokens, {x} {right} ...: drawn as glyphs, vita_prompt.h)
static float mu_textg(float x, float y, float size, int align, const float *c, const char *s);
static float mu_twg(const char *s, float size);
#define MU_SQUEEZE 0.78f                       // b49: the narrowest a label is condensed to (glyph width; the height stays)
static void logp(const char *fmt, ...);        // main.c
static int mu_overflows;                       // b49: labels that did not fit their box even condensed
static void mu_overflow(const char *s, float w, float avail) {   // logged once per text (the menu tour finds them)
	static const char *seen[32]; static int nseen; mu_overflows++;
	for (int i = 0; i < nseen; i++) if (seen[i] == s) return;
	if (nseen < 32) seen[nseen++] = s; logp("menu text too wide: \"%s\" %.0f px in %.0f px (condensed to %.0f %%)\n", s, w, avail, MU_SQUEEZE * 100);
}
static void mu_textbox(float x0, float y0, float x1, float y1, float size, int align, float pad, const float *c, const char *s) {
	if (!s) return; float w = mu_twg(s, size), avail = x1 - x0 - 2 * pad; if (w > avail && w > 0) size = fmaxf(MU_FINE, size * avail / w);   // never below fine print
	float y = (y0 + y1) * 0.5f + size * MU_CAP * 0.5f, x = align == 0 ? x0 + pad : align == 1 ? x1 - pad : (x0 + x1) * 0.5f;
	if (strchr(s, '{')) { mu_textg(x, y, size, align, c, s); return; }
	// b49 (user report on b48: TEAM DEATHMATCH ran over its DIVE MODE tile): still too wide at fine print -> condensed to fit
	float wf = mu_tw(s, size); if (wf > avail && wf > 0) { float k = avail / wf; if (k < MU_SQUEEZE) { k = MU_SQUEEZE; mu_overflow(s, wf, avail);
		  // b59 (user: a name ran over the icon beside it): still too wide condensed -> cut, ending in "...", so it never overlaps
		  char t[160]; int n = snprintf(t, sizeof(t), "%s", s); if (n > (int)sizeof(t) - 4) n = (int)sizeof(t) - 4;
		  while (n > 1 && (mu_tw(t, size) + mu_tw("...", size)) * k > avail) t[--n] = 0;
		  while (n > 1 && t[n - 1] == ' ') t[--n] = 0; memcpy(t + n, "...", 4);
		  hud_text_sx = k; mu_text(x, y, size, align, c, t); hud_text_sx = 1.0f; return; }
		hud_text_sx = k; }
	mu_text(x, y, size, align, c, s); hud_text_sx = 1.0f;
}
// wrapped text from the top of a box (x = left edge, or the centre with align 2); returns the lines drawn
static int mu_wrapa(float x, float y, float w, float size, float lh, int maxl, int align, const float *c, const char *s) {
	if (!s) return 0; char line[200]; int lines = 0; float yb = y + size * MU_CAP;
	while (*s && lines < maxl) {
		int len = 0, brk = -1; float lw = 0;
		while (s[len] && s[len] != '\n') { lw += hadv[(unsigned char)s[len]] * size; if (lw > w && brk > 0) break; if (s[len] == ' ') brk = len; len++; }
		if (s[len] && s[len] != '\n' && lw > w && brk > 0) len = brk;
		int n = len < 199 ? len : 199; memcpy(line, s, n); line[n] = 0;
		if (lines == maxl - 1 && s[len] && s[len] != '\n') { int k = n; while (k > 0 && mu_tw(line, size) + mu_tw("...", size) > w) line[--k] = 0; strcat(line, "..."); }
		{ float lw2 = mu_tw(line, size), sz = size; if (lw2 > w && lw2 > 0) sz = fmaxf(MU_FINE, size * w / lw2);   // one long word: shrink it (not below fine print)
		  float lw3 = mu_tw(line, sz); if (lw3 > w && lw3 > 0) hud_text_sx = fmaxf(MU_SQUEEZE, w / lw3);   // b49: then condense it
		  mu_text(x, yb, sz, align == 2 ? 2 : 0, c, line); hud_text_sx = 1.0f; }
		yb += lh; lines++;
		s += len; while (*s == ' ' || *s == '\n') s++;
	}
	return lines;
}
static int mu_wrap(float x, float y, float w, float size, float lh, int maxl, const float *c, const char *s) { return mu_wrapa(x, y, w, size, lh, maxl, 0, c, s); }
static int mu_lines(const char *s, float w, float size) {   // how many lines mu_wrapa needs
	int lines = 0; while (s && *s) { int len = 0, brk = -1; float lw = 0;
		while (s[len] && s[len] != '\n') { lw += hadv[(unsigned char)s[len]] * size; if (lw > w && brk > 0) break; if (s[len] == ' ') brk = len; len++; }
		if (s[len] && s[len] != '\n' && lw > w && brk > 0) len = brk; lines++; s += len; while (*s == ' ' || *s == '\n') s++; }
	return lines;
}
static float mu_wrap_maxw(const char *s, float w, float size, int maxl) {   // the widest line mu_wrapa draws (box widths)
	float mw = 0; int lines = 0; while (s && *s && lines < maxl) { int len = 0, brk = -1; float lw = 0;
		while (s[len] && s[len] != '\n') { lw += hadv[(unsigned char)s[len]] * size; if (lw > w && brk > 0) break; if (s[len] == ' ') brk = len; len++; }
		if (s[len] && s[len] != '\n' && lw > w && brk > 0) len = brk;
		float lw2 = 0; for (int k = 0; k < len; k++) lw2 += hadv[(unsigned char)s[k]] * size; if (lw2 > w) lw2 = w; if (lw2 > mw) mw = lw2;
		lines++; s += len; while (*s == ' ' || *s == '\n') s++; }
	return mw;
}
// a texel rectangle of a pack image (texels c0..c1, r0..r1 of image im) stretched over a screen rectangle (nine-slices)
static void mu_img_part(int im, float c0, float r0, float c1, float r1, float x0, float y0, float x1, float y1, const float *mul) {
	if (im < 0 || !mu_tex(htex[himg[im].tex], 6)) return; const HImg *I = &himg[im]; float tw = (float)htw[I->tex], th = (float)hth[I->tex];
	float U0 = (I->x + c0) / tw, U1 = (I->x + c1) / tw, V0 = (I->y + r0) / th, V1 = (I->y + r1) / th, m[4] = { 1, 1, 1, 1 }; if (mul) memcpy(m, mul, 16);
	mu_emit(x0, y0, U0, V0, m); mu_emit(x1, y0, U1, V0, m); mu_emit(x1, y1, U1, V1, m); mu_emit(x0, y0, U0, V0, m); mu_emit(x1, y1, U1, V1, m); mu_emit(x0, y1, U0, V1, m);
}
// ------------------------------------------------------------------ PS Vita button glyphs (vita_prompt.h: one look for every screen)
enum { MG_CROSS, MG_CIRCLE, MG_SQUARE, MG_TRIANGLE, MG_L, MG_R, MG_START, MG_DPAD, MG_TOUCH };
static const int MG_VP[9] = { VP_CROSS, VP_CIRCLE, VP_SQUARE, VP_TRIANGLE, VP_L, VP_R, VP_START, VP_DPAD, VP_TOUCH };
static void mu_glyph(int g, float cx, float cy, float r) { vp_glyph(MG_VP[g], cx, cy, r / 0.6f, mu_a); }   // r = face button radius
// ui b50 (user report b49: the Circle on the back tab's slanted end, half on the dark bar, looked out of place): a face button
// printed in a label's ink - a disc in that colour with the symbol cut out of it in the colour under it (paper; NULL: the
// outline and the symbol in ink, no disc), r = the disc's radius. The back tabs (menu_fe.h fe_topbar, the result screens) put
// the Circle inside the tab, left of the label, at its cap height.
// the back tab's label (b50, user: no Circle on the tab - the hint row already says Circle is back): the destination alone,
// inside the tab's solid yellow - clear of the darker underline's raised left corner and of the light stripe at the right end
// (callers pass that box; it condenses a long label rather than run into either)
static void mu_back_label(float x0, float y0, float x1, float y1, float size, const float *ink, int pressed, const char *s) {
	(void)pressed; mu_textbox(x0, y0, x1, y1, size, 0, 0, ink, s);
}
// text with inline Vita buttons ({x} {o} {s} {t} {l} {r} {up} {down} {left} {right} {start} {select} ...), screen fades applied
static float mu_textg(float x, float y, float size, int align, const float *c, const char *s) { float m[4] = { c[0], c[1], c[2], c[3] * mu_a }; return vp_text(x, y, size, align, m, s); }
static float mu_twg(const char *s, float size) { return vp_text_w(s, size); }
// wrapped text with inline buttons from the top of a box (x = left edge); returns the lines drawn
static int mu_wrapg(float x, float y, float w, float size, float lh, int maxl, const float *c, const char *s) {
	char line[220]; int lines = 0; float yb = y + size * MU_CAP;
	while (s && *s && lines < maxl) {
		int len = 0, brk = -1; float lw = 0;
		while (s[len] && s[len] != '\n') {
			if (s[len] == '{') { int l, k = vp_tok(s + len + 1, &l); if (k >= 0) { lw += vp_glyph_w(k, size); if (lw > w && brk > 0) break; len += l + 1; continue; } }
			lw += hadv[(unsigned char)s[len]] * size; if (lw > w && brk > 0) break; if (s[len] == ' ') brk = len; len++; }
		if (s[len] && s[len] != '\n' && brk > 0 && lw > w) len = brk;
		int n = len < 219 ? len : 219; memcpy(line, s, n); line[n] = 0; mu_textg(x, yb, size, 0, c, line);
		yb += lh; lines++; s += len; while (*s == ' ' || *s == '\n') s++;
	}
	return lines;
}
// b55 (user: the button prompts sat too close to the bottom edge): the hint bar starts at y 510 (was 514) and the
// prompts are centred at y 524 (were 528-529): the 22 px glyphs end 9 px above the edge instead of 4.
#define MU_HINT_BAR 510
#define MU_HINT_Y 524
// bottom hint line: "x:SELECT|o:BACK|lr:TAB|s:DEFAULT|d:MOVE|R:ROTATE" (R: the right stick; b76: U = D-pad up / down, H = left / right) items separated by '|', drawn right to left from the right edge
static float mu_hint_ts = MU_SMALL, mu_hint_gs = 22, mu_hint_gap = 18;   // b56: label size, glyph size, gap between items (mu_hints_fit narrows them)
static void mu_hints(const char *spec, float y) {
	char buf[160]; snprintf(buf, sizeof(buf), "%s", spec); char *items[8]; int n = 0;
	for (char *p = strtok(buf, "|"); p && n < 8; p = strtok(NULL, "|")) items[n++] = p;
	float x = MU_W - 14, ts = mu_hint_ts, gs = mu_hint_gs;   // glyphs sized for 22 px text
	for (int i = n - 1; i >= 0; i--) {
		char *c = strchr(items[i], ':'); if (!c) continue; *c = 0; const char *k = items[i], *lab = c + 1;
		mu_text(x, y + ts * MU_CAP * 0.5f, ts, 1, MU_WHITE, lab); x -= mu_tw(lab, ts) + 4;
		for (int j = (int)strlen(k) - 1; j >= 0; j--) {
			int g = k[j] == 'x' ? VP_CROSS : k[j] == 'o' ? VP_CIRCLE : k[j] == 's' ? VP_SQUARE : k[j] == 't' ? VP_TRIANGLE : k[j] == 'l' ? VP_L : k[j] == 'r' ? VP_R : k[j] == 'S' ? VP_START : k[j] == 'd' ? VP_DPAD : k[j] == 'R' ? VP_RS : k[j] == 'U' ? VP_UD : k[j] == 'H' ? VP_LR : VP_TOUCH;
			float gw = vp_glyph_w(g, gs); vp_glyph(g, x - gw / 2, y, gs, mu_a); x -= gw; }
		x -= mu_hint_gap;
	}
}
// width of a hint line as mu_hints draws it
static float mu_hints_w(const char *spec) {
	char buf[160]; snprintf(buf, sizeof(buf), "%s", spec); float w = 0, ts = mu_hint_ts, gs = mu_hint_gs; int n = 0;
	for (char *p = strtok(buf, "|"); p; p = strtok(NULL, "|")) { char *c = strchr(p, ':'); if (!c) continue; *c = 0; if (n++) w += mu_hint_gap;
		w += mu_tw(c + 1, ts) + 4;
		for (const char *k = p; *k; k++) { int g = *k == 'x' ? VP_CROSS : *k == 'o' ? VP_CIRCLE : *k == 's' ? VP_SQUARE : *k == 't' ? VP_TRIANGLE : *k == 'l' ? VP_L : *k == 'r' ? VP_R : *k == 'S' ? VP_START : *k == 'd' ? VP_DPAD : *k == 'R' ? VP_RS : *k == 'U' ? VP_UD : *k == 'H' ? VP_LR : VP_TOUCH; w += vp_glyph_w(g, gs); } }
	return w;
}
// b55 (user: in the pause menu the D-pad prompt ran over the left edge of the panel): hints kept right of x0.
// b56 (user: one line, the D-pad left of Cross, smaller if need be): narrower gaps, then smaller labels and glyphs down to
// fine print (20 px); only a line that still does not fit goes on two lines
static void mu_hints_fit(const char *spec, float y, float x0) {
	static const float TRY[3][3] = { { MU_SMALL, 22, 18 }, { MU_SMALL, 22, 10 }, { MU_FINE, 20, 8 } };
	for (int k = 0; k < 3; k++) { mu_hint_ts = TRY[k][0]; mu_hint_gs = TRY[k][1]; mu_hint_gap = TRY[k][2];
		if (mu_hints_w(spec) <= MU_W - 14 - x0) { mu_hints(spec, y); mu_hint_ts = MU_SMALL; mu_hint_gs = 22; mu_hint_gap = 18; return; } }
	mu_hint_ts = MU_SMALL; mu_hint_gs = 22; mu_hint_gap = 18;
	char a[160], b[160]; const char *bar = strchr(spec, '|'); if (!bar) { mu_hints(spec, y); return; }
	snprintf(a, sizeof(a), "%.*s", (int)(bar - spec), spec); snprintf(b, sizeof(b), "%s", bar + 1);
	mu_hints(a, y - 30); mu_hints(b, y);
}
// ------------------------------------------------------------------ touch input (front panel)
// Hit rectangles are registered while drawing (mu_reg) and tested on the next frame, against what the player saw.
#define MU_MAXHIT 128
#define MU_SLOP 14.0f                          // a press that moves farther than this is a drag, not a tap
typedef struct { short x0, y0, x1, y1; int id; } MuHit;
static MuHit mu_hits[2][MU_MAXHIT]; static int mu_nhits[2], mu_hcur, mu_draws, mu_draws_seen, mu_noreg;   // mu_noreg: drawing a screen behind a popup
static void mu_reg(int id, float x0, float y0, float x1, float y1) {
	int b = mu_hcur ^ 1; if (mu_noreg || mu_nhits[b] >= MU_MAXHIT) return; MuHit *h = &mu_hits[b][mu_nhits[b]++];
	h->x0 = (short)x0; h->y0 = (short)y0; h->x1 = (short)x1; h->y1 = (short)y1; h->id = id;
}
// a list entry scrolled partly out of its view: register the visible part only while it is still a full-size target
static void mu_reg_clip(int id, float x0, float y0, float x1, float y1, float v0, float v1) {
	y0 = fmaxf(y0, v0); y1 = fminf(y1, v1); if (y1 - y0 >= MU_TAP - 0.5f) mu_reg(id, x0, y0, x1, y1);
}
static void mu_draw_begin(void) { mu_draws++; }   // every menu draw function calls this (the next update swaps the lists)
static int mu_hit_at(float x, float y) {      // topmost (last registered) rectangle containing the point
	const MuHit *H = mu_hits[mu_hcur]; for (int i = mu_nhits[mu_hcur] - 1; i >= 0; i--) if (x >= H[i].x0 && x < H[i].x1 && y >= H[i].y0 && y < H[i].y1) return H[i].id;
	return -1;
}
static const MuHit *mu_hit_rect(int id) { const MuHit *H = mu_hits[mu_hcur]; for (int i = mu_nhits[mu_hcur] - 1; i >= 0; i--) if (H[i].id == id) return &H[i]; return NULL; }
static const MuHit *mu_hit_drawn(int id) {   // in the last frame drawn (scripted taps), else the current list
	int b = mu_hcur ^ 1; const MuHit *H = mu_hits[b]; for (int i = mu_nhits[b] - 1; i >= 0; i--) if (H[i].id == id) return &H[i]; return mu_hit_rect(id);
}
static struct {
	int down, x, y, sx, sy;                 // finger now / where it went down (screen pixels)
	int e_press, e_release;                 // edges from the panel since the last update
	int press, release, tap, id, moved;     // this update: went down / lifted / tapped id (or -1) / id under the press / dragged
	int was_sel;                            // the pressed item was already selected (a second tap activates it)
	float hold, dx, dy, vy, rep;            // seconds held, movement this update, fling speed, auto-repeat clock
	int lock, inj, ix, iy, idown, off;      // wait for a lift (after a screen change); injected touches (autoplay); touch disabled
	int lastx, lasty;
} MUT = { .tap = -1, .id = -1 };
static void mu_touch_raw(int down, float x, float y) {   // feed the finger state (once per frame)
	if (MUT.off) down = 0;
	if (down && !MUT.down) { MUT.e_press = 1; MUT.sx = (int)x; MUT.sy = (int)y; MUT.lastx = (int)x; MUT.lasty = (int)y; }
	if (!down && MUT.down) MUT.e_release = 1;
	MUT.down = down; if (down) { MUT.x = (int)x; MUT.y = (int)y; }
}
// autoplay / host tests: a scripted finger (x, y in screen pixels); menu_touch_poll uses it instead of the panel while set
static void menu_touch_inject(int on, int down, float x, float y) { MUT.inj = on; MUT.idown = down; MUT.ix = (int)x; MUT.iy = (int)y; }
static void menu_touch_lock(void) { MUT.lock = 1; MUT.id = -1; MUT.tap = -1; MUT.moved = 0; }   // ignore the finger until it lifts
// once per frame, before the menu update functions (front-end, results, pause)
static void menu_touch_poll(float dt) {
	(void)dt;
	if (MUT.inj) { mu_touch_raw(MUT.idown, (float)MUT.ix, (float)MUT.iy); return; }
#ifdef __vita__
	static int init; if (!init) { sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START); init = 1; }
	SceTouchData td; memset(&td, 0, sizeof(td));
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) >= 0 && td.reportNum > 0) mu_touch_raw(1, td.report[0].x * (float)MU_W / 1920.0f, td.report[0].y * (float)MU_H / 1088.0f);
	else mu_touch_raw(0, (float)MUT.x, (float)MUT.y);
#endif
}
// at the start of every menu update: swap in the last drawn hit list, turn the panel edges into this update's events
static void mu_in_begin(float dt) {
	if (mu_draws != mu_draws_seen) { mu_hcur ^= 1; mu_draws_seen = mu_draws; mu_nhits[mu_hcur ^ 1] = 0; }
	MUT.press = MUT.e_press; MUT.release = MUT.e_release; MUT.e_press = MUT.e_release = 0; MUT.tap = -1;
	MUT.dx = MUT.dy = 0;
	if (MUT.lock) { if (!MUT.down && !MUT.press) MUT.lock = 0; MUT.press = MUT.release = 0; MUT.id = -1; return; }
	if (MUT.press) { MUT.id = mu_hit_at((float)MUT.sx, (float)MUT.sy); MUT.moved = 0; MUT.hold = 0; MUT.vy = 0; MUT.rep = 0; MUT.was_sel = 0; }
	if (MUT.down || MUT.release) {
		MUT.dx = (float)(MUT.x - MUT.lastx); MUT.dy = (float)(MUT.y - MUT.lasty); MUT.lastx = MUT.x; MUT.lasty = MUT.y;
		if (dt > 0) MUT.vy = MUT.vy * 0.5f + 0.5f * MUT.dy / dt;
		if (fabsf((float)(MUT.x - MUT.sx)) > MU_SLOP || fabsf((float)(MUT.y - MUT.sy)) > MU_SLOP) MUT.moved = 1;
		MUT.hold += dt;
	}
	if (MUT.release) { if (!MUT.moved && MUT.id >= 0 && mu_hit_at((float)MUT.x, (float)MUT.y) == MUT.id) MUT.tap = MUT.id; }
}
static void mu_in_end(void) { if (MUT.release) MUT.id = -1; }
// queries for the update functions
static int mu_tapped(int id) { return MUT.tap == id; }
static int mu_pressing(int id) {             // finger held on id (pressed look); stays while it is still over the rect
	if (!MUT.down || MUT.lock || MUT.id != id || MUT.moved) return 0; const MuHit *h = mu_hit_rect(id); return !h || (MUT.x >= h->x0 && MUT.x < h->x1 && MUT.y >= h->y0 && MUT.y < h->y1);
}
static int mu_repeat(int id, float dt) {     // a held arrow repeats: first after 0.45 s, then every 0.11 s
	if (!mu_pressing(id)) return 0; MUT.rep += dt; if (MUT.hold > 0.45f && MUT.rep > 0.11f) { MUT.rep = 0; return 1; } return 0;
}
// ids: kind << 8 | index
#define MU_ID(kind, i) (((kind) << 8) | ((i) & 255))
#define MU_KIND(id) ((id) >> 8)
#define MU_IX(id) ((id) & 255)
// a vertical scroll view: drag to scroll, fling, clamps with a soft edge
typedef struct { float pos, vel, max; int drag; } MuScroll;
static void mu_scroll_update(MuScroll *S, int owns_touch, float dt) {
	if (owns_touch && MUT.down && MUT.moved && !MUT.lock) { S->drag = 1; S->pos -= MUT.dy; S->vel = -MUT.vy; }
	else {
		if (S->drag && !MUT.down) S->drag = 0;
		S->pos += S->vel * dt; S->vel *= expf(-5.0f * dt); if (fabsf(S->vel) < 5) S->vel = 0;
		if (S->pos < 0) { S->pos += (0 - S->pos) * fminf(1, 12 * dt); if (S->pos > -0.5f) S->pos = 0; S->vel = 0; }
		if (S->pos > S->max) { S->pos += (S->max - S->pos) * fminf(1, 12 * dt); if (S->pos < S->max + 0.5f) S->pos = S->max; S->vel = 0; }
	}
}
static void mu_scroll_show(MuScroll *S, float y0, float y1, float view) {   // keep [y0, y1] (content coords) inside the view
	if (S->drag) return; if (y0 < S->pos) S->pos = y0; if (y1 > S->pos + view) S->pos = y1 - view; if (S->pos < 0) S->pos = 0; if (S->pos > S->max) S->pos = S->max;
}
static float mu_vis(float y0, float y1, float v0, float v1) {   // how much of a row is inside a scrolled view (0..1), for fading rows at the edges
	if (y1 <= v0 || y0 >= v1) return 0; float a = 1; if (y0 < v0) a = fminf(a, (y1 - v0) / (y1 - y0)); if (y1 > v1) a = fminf(a, (v1 - y0) / (y1 - y0)); return a * a;
}
static void mu_scrollbar(float x, float y0, float y1, const MuScroll *S, float view) {
	if (S->max <= 0) return; float tot = S->max + view, h = (y1 - y0) * view / tot, t = y0 + (y1 - y0 - h) * mu_clamp(S->pos / S->max, 0, 1);
	float bg[4] = { 0, 0, 0, 0.35f }; mu_rect(x - 2, y0, x + 2, y1, bg); mu_rect(x - 3, t, x + 3, t + h, MU_CYAND);
	mu_tri(x, y0 - 8, 9, 2, MU_CYAND); mu_tri(x, y1 + 8, 9, 3, MU_CYAND);
}
// debugging aid for the host renderer: the hit rectangles of the last drawn frame
static __attribute__((unused)) int mu_dump_hits(FILE *f) { int b = mu_hcur ^ 1, n = mu_nhits[b]; for (int i = 0; i < n; i++) fprintf(f, "%d %d %d %d %d\n", mu_hits[b][i].id, mu_hits[b][i].x0, mu_hits[b][i].y0, mu_hits[b][i].x1, mu_hits[b][i].y1); return n; }
