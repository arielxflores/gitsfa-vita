// Tutorial screens and panels (the original's in-tutorial UI, rebuilt from the tutorial footage, 1:04-8:36):
//   objective band (icon, yellow title, instructions), the button prompts of the step (the original's keyboard guide,
//   here Vita buttons), checklist (right), operator portrait + caption (the radio widget: ovl pack cm.face.N),
//   SUCCESS disc, "End Program / Prosthetic Calibration Complete", yellow warnings ("You have been spotted!"), the
//   waypoint flag, hold / press prompts with a gauge, the Think Tank hacking read-out, the skill explanation screens
//   (title, video panel, TIER 1 / TIER 2 gauges, bullet text, NEXT) and the program loading screen.
// Legibility rule (BRIEF.md): at 960x544 body text >= 24 px, fine print >= 20 px, headings 32-48 px, touch targets
// >= 72 px tall; long texts are re-flowed onto more lines, never shrunk. The original's layout is kept in spirit
// (band at the top, checklist on the right, portrait on the left, the explanation screen's video / gauges / text).
// Drawn with hud.h in the ovl pack (its font, its "screen" node) plus one 512x512 atlas (tut_ui.gtx,
// tools/tutorial/make_tut_assets.py; rectangles in tut_atlas.h). Text may contain button tokens drawn as glyphs:
// {x} {o} {t} {s} cross / circle / triangle / square, {up} {down} {left} {right} D-pad, {l} {r} shoulders, {ls} {rs}
// sticks, {start}. Host check: tools/tutorial/host (records the draws like tools/menu/host and rasterises them).
#pragma once
#include "menu.h"
#include "tut_atlas.h"
#define TUI_FH 36.f   // headings
#define TUI_FT 32.f   // the objective title
#define TUI_FB 24.f   // body text and labels
#define TUI_FS 20.f   // fine print
#define TUI_LH 28.f   // body line height
static struct { GLuint tex, ld_tex; int ok, nd; char dir[128]; float band_y1, ck_x0, ck_y1, pt_x1; } TUI = { 0, 0, 0, -1, "", 0, 0, 0, 0 };   // ck_ / pt_: this frame's checklist / portrait extents
static const float TUI_YEL[4] = { 0.98f, 0.94f, 0.16f, 1 }, TUI_WHT[4] = { 0.95f, 0.96f, 0.97f, 1 }, TUI_GRY[4] = { 0.66f, 0.70f, 0.72f, 1 },
	TUI_CYN[4] = { 0.44f, 0.90f, 0.98f, 1 }, TUI_ORG[4] = { 0.96f, 0.68f, 0.16f, 1 };
// GTX1 with fmt 5 (DXT5) as well as 0 (RGBA8) / 1 (DXT1)
static GLuint tui_tex_load(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	unsigned char h[16]; if (fread(h, 1, 16, f) != 16 || memcmp(h, "GTX1", 4)) { fclose(f); return 0; }
	int w = h[4] | h[5] << 8, hh = h[6] | h[7] << 8, fmt = h[8]; unsigned size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
	void *buf = malloc(size); if (!buf || fread(buf, 1, size, f) != size) { free(buf); fclose(f); return 0; }
	fclose(f); GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	if (fmt == 1 || fmt == 5) glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w, hh, 0, size, buf);
	else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	VGL_XFER_WAIT();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free(buf); return t;
}
static int tui_init(const char *dir) {
	snprintf(TUI.dir, sizeof(TUI.dir), "%s", dir);
	if (!TUI.tex) { char p[200]; snprintf(p, sizeof(p), "%stut_ui.gtx", dir); TUI.tex = tui_tex_load(p); }
	TUI.ok = TUI.tex != 0 && menu_ovl >= 0; return TUI.ok ? 0 : -1;
}
// between tui_begin / tui_end every call draws in screen pixels (960x544, origin top left)
static int tui_begin(void) { if (!TUI.ok) return 0; menu_begin(menu_ovl); TUI.nd = MN("screen"); TUI.ck_x0 = TUI.ck_y1 = TUI.pt_x1 = 0; return TUI.nd >= 0; }
static void tui_end(void) { menu_end(); }
static void tui_img(int id, float x, float y, float w, float h, const float *col) {
	if (id < 0 || id >= TUI_COUNT) return; const unsigned short *r = TUI_RECT[id];
	float u0 = (r[0] + 0.5f) / TUI_W, v0 = (r[1] + 0.5f) / TUI_H, u1 = (r[0] + r[2] - 0.5f) / TUI_W, v1 = (r[1] + r[3] - 0.5f) / TUI_H;
	float xy[4][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h } }, uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } };
	hud_poly(TUI.tex, TUI.nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, col ? col : HUD_W1);
}
static void tui_img4(int id, const float (*xy)[2], const float *col) {   // corners given explicitly (mirrored frame brackets)
	const unsigned short *r = TUI_RECT[id]; float u0 = (r[0] + 0.5f) / TUI_W, v0 = (r[1] + 0.5f) / TUI_H, u1 = (r[0] + r[2] - 0.5f) / TUI_W, v1 = (r[1] + r[3] - 0.5f) / TUI_H;
	float uv[4][2] = { { u0, v0 }, { u1, v0 }, { u1, v1 }, { u0, v1 } }; hud_poly(TUI.tex, TUI.nd, xy, (const float (*)[2])uv, 4, col);
}
static void tui_rect(float x0, float y0, float x1, float y1, const float *col) { tui_img(TUI_WHITE, x0, y0, x1 - x0, y1 - y0, col); }
static void tui_col(float *o, const float *c, float a) { o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = c[3] * a; }
static void tui_band(float x0, float y0, float x1, float y1, const float *c, float a0, float a1) {   // horizontal gradient
	for (int i = 0; i < 8; i++) { float f = (i + 0.5f) / 8, m[4]; tui_col(m, c, a0 + (a1 - a0) * f); tui_rect(x0 + (x1 - x0) * i / 8, y0, x0 + (x1 - x0) * (i + 1) / 8, y1, m); }
}
// ---- text with button glyphs
typedef struct { const char *tok; int img; } TuiTok;
static const TuiTok TUI_TOK[] = { { "x", TUI_BTN_X }, { "o", TUI_BTN_O }, { "t", TUI_BTN_T }, { "s", TUI_BTN_S }, { "up", TUI_DPAD_U }, { "down", TUI_DPAD_D },
	{ "left", TUI_DPAD_L }, { "right", TUI_DPAD_R }, { "l", TUI_SHOULDER }, { "r", TUI_SHOULDER }, { "ls", TUI_STICK }, { "rs", TUI_STICK }, { "start", TUI_START } };
static int tui_tok(const char *s, int *len) {   // s points after '{'; returns the token index, len = chars up to and including '}'
	for (unsigned k = 0; k < sizeof(TUI_TOK) / sizeof(TUI_TOK[0]); k++) { size_t n = strlen(TUI_TOK[k].tok);
		if (!strncmp(s, TUI_TOK[k].tok, n) && s[n] == '}') { *len = (int)n + 1; return (int)k; } }
	return -1;
}
// the letter inside a stick / shoulder glyph is fine print too: >= 20 px (Legibility rule), so those glyphs are a bit larger
static float tui_glyph_l(float size) { return fmaxf(20.f, size * 0.85f); }
static float tui_glyph_w(int k, float size) { int im = TUI_TOK[k].img;
	return im == TUI_START ? fmaxf(1.6f * size, hud_text_w("START") * 20 + 14) + 4 : im == TUI_SHOULDER ? fmaxf(1.6f * size, 34) + 4 : im == TUI_STICK ? fmaxf(1.3f * size, 28) + 4 : 1.12f * size + 4; }
static float tui_text_w(const char *s, float size) {
	float w = 0; while (*s) { if (*s == '{') { int l, k = tui_tok(s + 1, &l); if (k >= 0) { w += tui_glyph_w(k, size); s += l + 1; continue; } }
		w += hadv[(unsigned char)*s] * size; s++; }
	return w;
}
static void tui_glyph(int k, float x, float y, float size, float a) {   // x left, y baseline
	int im = TUI_TOK[k].img; float m[4] = { a, a, a, a }, wh[4] = { a, a, a, a };
	float ls = tui_glyph_l(size), dk[4] = { 0.06f * a, 0.08f * a, 0.1f * a, a };
	if (im == TUI_SHOULDER || im == TUI_START) {
		float w = tui_glyph_w(k, size) - 4, h = fmaxf(w * (im == TUI_START ? 20.f / 48 : 28.f / 48), ls * 1.15f), y0 = y - size * 0.78f - (h - size * 0.78f) * 0.5f;
		tui_img(im, x + 2, y0, w, h, m);
		const char *t = im == TUI_START ? "START" : TUI_TOK[k].tok[0] == 'l' ? "L" : "R";
		hud_text(TUI.nd, x + 2 + w / 2, y0 + h * 0.5f + ls * 0.34f, ls, 2, wh, HUD_Z4, t);
	} else if (im == TUI_STICK) {   // the stick with L / R on its thumb pad
		float w = tui_glyph_w(k, size) - 4, y0 = y - size * 0.78f - (w - size * 0.78f) * 0.5f; tui_img(im, x + 2, y0, w, w, m);
		hud_text(TUI.nd, x + 2 + w / 2, y0 + w * 0.5f + ls * 0.34f, ls, 2, wh, HUD_Z4, TUI_TOK[k].tok[0] == 'l' ? "L" : "R"); (void)dk;
	} else { float w = 1.12f * size, y0 = y - size * 0.92f; tui_img(im, x + 2, y0, w, w, m); }
}
// one line; align 0 left, 1 right, 2 centre; returns the width
static float tui_text(float x, float y, float size, int align, const float *col, const char *s) {
	float w = tui_text_w(s, size); if (align == 1) x -= w; else if (align == 2) x -= w / 2;
	char seg[200]; int n = 0;
	while (*s) {
		if (*s == '{') { int l, k = tui_tok(s + 1, &l); if (k >= 0) {
			if (n) { seg[n] = 0; hud_text(TUI.nd, x, y, size, 0, col, HUD_Z4, seg); x += hud_text_w(seg) * size; n = 0; }
			tui_glyph(k, x, y, size, col[3]); x += tui_glyph_w(k, size); s += l + 1; continue; } }
		if (n < 199) seg[n++] = *s; s++;
	}
	if (n) { seg[n] = 0; hud_text(TUI.nd, x, y, size, 0, col, HUD_Z4, seg); }
	return w;
}
// word-wrapped text in a box of width w ('\n' breaks); y = first baseline; draw 0: only count. Returns the lines used.
static int tui_wrap_ex(float x, float y, float w, float size, float lh, const float *col, const char *s, int maxl, int align, int draw) {
	char line[220]; int lines = 0;
	while (*s && lines < maxl) {
		int len = 0, brk = -1; float lw = 0;
		while (s[len] && s[len] != '\n') {
			if (s[len] == '{') { int l, k = tui_tok(s + len + 1, &l); if (k >= 0) { lw += tui_glyph_w(k, size); if (lw > w && brk > 0) break; len += l + 1; continue; } }
			lw += hadv[(unsigned char)s[len]] * size; if (lw > w && brk > 0) break; if (s[len] == ' ') brk = len; len++; }
		if (s[len] && s[len] != '\n' && brk > 0 && lw > w) len = brk;
		int n = len < 219 ? len : 219; memcpy(line, s, n); line[n] = 0;
		if (draw) tui_text(align == 2 ? x + w / 2 : x, y + lines * lh, size, align, col, line);
		lines++; s += len; while (*s == ' ' || *s == '\n') s++;
	}
	return lines;
}
static int tui_wrap(float x, float y, float w, float size, float lh, const float *col, const char *s, int maxl) { return tui_wrap_ex(x, y, w, size, lh, col, s, maxl, 0, 1); }
static float tui_ease(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
static float tui_fade(float t, float in, float hold, float out) { return mfade(t, in, hold, out); }
static const float TUI_BANDC[4] = { 0.07f, 0.11f, 0.14f, 1 };
// ------------------------------------------------------------------ objective band (top): icon, title (1-2 lines), instructions
// t: seconds since the objective changed (slides in); returns the band's bottom edge (TUI.band_y1)
static float tui_objective(const char *title, const char *text, float t, float a) {
	HUD_TAG("tut.objective");   // ui b42 (overlap checker)
	if (!title) return TUI.band_y1 = 0;
	float k = tui_ease(t / 0.35f) * a, dx = 30 * (1 - tui_ease(t / 0.35f));
	char t1[64], t2[64]; const char *nl = strchr(title, '\n'); int two = nl != NULL;
	if (two) { int n = (int)(nl - title); if (n > 63) n = 63; memcpy(t1, title, n); t1[n] = 0; snprintf(t2, sizeof(t2), "%s", nl + 1); } else { snprintf(t1, sizeof(t1), "%s", title); t2[0] = 0; }
	float tw = fmaxf(tui_text_w(t1, TUI_FT), two ? tui_text_w(t2, TUI_FT) : 0), xs = 58 + tw + 14, xt = xs + 14, wt = 948 - xt;
	int lines = text ? tui_wrap_ex(xt, 0, wt, TUI_FB, TUI_LH, TUI_WHT, text, 5, 0, 0) : 0;
	float h = fmaxf(two ? 2 * 34.f + 14 : 34.f + 16, lines * TUI_LH + 16), y0 = 4, y1 = y0 + h;
	tui_band(0, y0, 960, y1, TUI_BANDC, 0.78f * k, 0.55f * k);
	{ float ln[4] = { 0.45f, 0.55f, 0.60f, 0.6f * k }; tui_rect(0, y1, 960, y1 + 1.5f, ln); }
	float m[4] = { 1, 1, 1, k }; tui_img(TUI_OBJ, 10 + dx, y0 + h / 2 - 20, 40, 40, m);
	float yc[4]; tui_col(yc, TUI_YEL, k);
	if (two) { tui_text(58 + dx, y0 + h / 2 - 5, TUI_FT, 0, yc, t1); tui_text(58 + dx, y0 + h / 2 + 27, TUI_FT, 0, yc, t2); }
	else tui_text(58 + dx, y0 + h / 2 + 11, TUI_FT, 0, yc, t1);
	{ float sep[4] = { 0.62f, 0.68f, 0.72f, 0.8f * k }; tui_rect(xs, y0 + 8, xs + 1.5f, y1 - 8, sep); }
	float wc[4]; tui_col(wc, TUI_WHT, k);
	if (text) tui_wrap(xt + dx, y0 + h / 2 - (lines * TUI_LH) / 2 + 20, wt, TUI_FB, TUI_LH, wc, text, 5);
	return TUI.band_y1 = y1;
}
// "End Program / Prosthetic Calibration Complete": the band, centred text
static void tui_endprog(const char *l1, const char *l2, float t) {
	HUD_TAG("tut.endprog");   // ui b42 (overlap checker)
	float k = tui_ease(t / 0.4f); tui_band(0, 4, 960, 100, TUI_BANDC, 0.78f * k, 0.78f * k);
	{ float ln[4] = { 0.45f, 0.55f, 0.60f, 0.6f * k }; tui_rect(0, 100, 960, 101.5f, ln); }
	float yc[4], wc[4]; tui_col(yc, TUI_YEL, k); tui_col(wc, TUI_WHT, k);
	tui_text(480, 46, TUI_FH, 2, yc, l1 ? l1 : "End Program"); tui_text(480, 84, TUI_FB, 2, wc, l2 ? l2 : "Prosthetic Calibration Complete");
	TUI.band_y1 = 102;
}
// ------------------------------------------------------------------ the step's button prompts (the original's keyboard guide)
enum { TK_MOVE, TK_WALK, TK_SPRINT, TK_CROUCH, TK_JUMP, TK_SKILL, TK_USE, TK_SWAP, TK_MELEE, TK_GREN, TK_AIM, TK_FIRE, TK_LOOK, TK_N };
#define TKB(k) (1u << (k))
static void tui_keys(unsigned lit, float y, float a) {   // y: top of the row (under the band)
	HUD_TAG("tut.keys");   // ui b42 (overlap checker)
	static const char *TOK[TK_N] = { "{ls}", "{ls}", "{down}", "{o}", "{x}", "{right}", "{s}", "{t}", "{left}", "{up}", "{l}", "{r}", "{rs}" };
	static const char *LAB[TK_N] = { "MOVE", "GENTLY: WALK", "SPRINT", "CROUCH", "JUMP", "SKILL", "INTERACT", "SWITCH WEAPON", "MELEE", "GRENADE", "AIM", "FIRE", "LOOK" };
	float x = 12;
	for (int k = 0; k < TK_N; k++) { if (!((lit >> k) & 1)) continue;
		char b[48]; snprintf(b, sizeof(b), "%s %s", TOK[k], LAB[k]); float w = tui_text_w(b, TUI_FB);
		float bg[4] = { 0.04f, 0.06f, 0.08f, 0.62f * a }, c[4] = { 0.95f, 0.96f, 0.97f, a };
		tui_rect(x, y, x + w + 16, y + 34, bg); tui_text(x + 8, y + 26, TUI_FB, 0, c, b); x += w + 26; }
}
// ------------------------------------------------------------------ checklist (right)
typedef struct { const char *label; int count, goal; } TuiCheck;   // goal > 0: "label 2/5"
static void tui_checklist(const TuiCheck *it, int n, int cur, float y0, float t) {
	HUD_TAG("tut.checklist");   // ui b42 (overlap checker)
	if (n <= 0) return; float k = tui_ease(t / 0.3f), x0 = 672, x1 = 956, mw = 0;
	for (int i = 0; i < n; i++) { char b[64]; if (it[i].goal > 0) snprintf(b, sizeof(b), "%s %d/%d", it[i].label, it[i].goal, it[i].goal); else snprintf(b, sizeof(b), "%s", it[i].label); mw = fmaxf(mw, tui_text_w(b, TUI_FB)); }
	if (x0 + 24 + mw > x1 - 6) x0 = x1 - 6 - 24 - mw;   // long labels (24 px): the panel grows to the left
	TUI.ck_x0 = x0 - 8; TUI.ck_y1 = y0 + n * 30 + 6;
	{ float bg[4] = { 0.03f, 0.05f, 0.07f, 0.45f * k }; tui_rect(x0 - 8, y0 - 4, x1 + 4, y0 + n * 30 + 6, bg); }
	{ float ln[4] = { 0.55f, 0.62f, 0.66f, 0.7f * k }; tui_rect(x0 - 8, y0 - 4, x1 + 4, y0 - 2.5f, ln); }
	for (int i = 0; i < n; i++) {
		float y = y0 + 25 + i * 30; char b[64];
		if (it[i].goal > 0) snprintf(b, sizeof(b), "%s %d/%d", it[i].label, it[i].count, it[i].goal); else snprintf(b, sizeof(b), "%s", it[i].label);
		if (i < cur) {   // done: check mark, dimmed, struck through
			float g[4] = { 0.62f, 0.66f, 0.68f, 0.75f * k }, w[4] = { 0.92f, 0.94f, 0.95f, 0.9f * k };
			tui_img(TUI_CHECK, x0, y - 16, 16, 16, w); float tw = tui_text(x0 + 24, y, TUI_FB, 0, g, b); tui_rect(x0 + 22, y - 9, x0 + 26 + tw, y - 7.5f, w);
		} else if (i == cur) { float o[4], w[4]; tui_col(o, TUI_ORG, k); tui_col(w, TUI_WHT, k); tui_rect(x0 + 2, y - 15, x0 + 14, y - 3, o); tui_text(x0 + 24, y, TUI_FB, 0, w, b); }
		else { float d[4] = { 0.25f, 0.28f, 0.30f, 0.8f * k }, g[4] = { 0.70f, 0.73f, 0.75f, 0.85f * k }; tui_rect(x0 + 2, y - 15, x0 + 14, y - 3, d); tui_text(x0 + 24, y, TUI_FB, 0, g, b); }
	}
}
// ------------------------------------------------------------------ portrait + caption (the original's radio widget, larger)
// face: ovl pack cm.face.N; y0: top; t: seconds since the line started (fades in, holds ~4.6 s, fades out)
static void tui_portrait(int face, const char *name, const char *caption, float y0, float t) {
	HUD_TAG("tut.portrait");   // ui b42 (overlap checker)
	if (t < 0 || t > 6) return; float a = tui_fade(t, 0.25f, 4.6f, 0.6f); if (a <= 0) return;
	char b[24]; snprintf(b, sizeof(b), "cm.face.%d", face); int im = MI(b); float cx = 80, cy = y0 + 66, m[4] = { a, a, a, a }; TUI.pt_x1 = 246;
	if (im >= 0) { const float *l = himg[im].loc; float s = 104 / (l[2] - l[0]); float A[6] = { s, 0, cx - 52 - s * l[0], 0, s, cy - 52 - s * l[1] }; hud_img(im, TUI.nd, A, m, HUD_Z4, -1, 1); }
	tui_img(TUI_PRING, cx - 64, cy - 64, 128, 128, m);
	float c[4]; tui_col(c, TUI_CYN, a); if (name) { float bg[4] = { 0.05f, 0.08f, 0.1f, 0.8f * a }; float w = tui_text_w(name, TUI_FS); tui_rect(cx - w / 2 - 8, cy + 50, cx + w / 2 + 8, cy + 74, bg); tui_text(cx, cy + 69, TUI_FS, 2, c, name); }
	if (caption && caption[0]) { float w[4]; tui_col(w, TUI_WHT, a); int n = tui_wrap_ex(16, 0, 222, TUI_FS, 24, w, caption, 4, 0, 0);
		float bg[4] = { 0.05f, 0.07f, 0.09f, 0.75f * a }, ln[4] = { 0.6f, 0.66f, 0.7f, 0.7f * a }; tui_rect(8, cy + 78, 246, cy + 86 + n * 24, bg); tui_rect(8, cy + 78, 246, cy + 79.5f, ln);
		tui_wrap(16, cy + 100, 222, TUI_FS, 24, w, caption, 4); }
}
// ------------------------------------------------------------------ SUCCESS (centre), warnings, prompts
static void tui_success(float t) {
	HUD_TAG("tut.success");   // ui b42 (overlap checker)
	if (t < 0 || t > 2.2f) return;
	float a = tui_fade(t, 0.12f, 1.5f, 0.5f), s = 1.25f - 0.25f * tui_ease(t / 0.2f), R = 68 * s, m[4] = { a, a, a, a };
	tui_img(TUI_SUCCESS, 480 - R, 236 - R, 2 * R, 2 * R, m);
	float w[4] = { 0.92f, 0.97f, 1, a }; tui_text(480, 245, TUI_FB * s, 2, w, "SUCCESS");
}
// a yellow warning under the band ("You have been spotted!"); re-flowed between the portrait and the checklist when it
// does not fit centred (draw it after them)
static void tui_message(const char *s, float t, const float *col) {
	HUD_TAG("tut.message");   // ui b42 (overlap checker)
	if (!s || t < 0 || t > 3.2f) return; float a = tui_fade(t, 0.15f, 2.4f, 0.6f), c[4]; tui_col(c, col ? col : TUI_YEL, a);
	float w = tui_text_w(s, 30), bg[4] = { 0.03f, 0.04f, 0.05f, 0.55f * a }, l = TUI.pt_x1 > 0 ? TUI.pt_x1 + 12 : 16, r = TUI.ck_x0 > 0 ? TUI.ck_x0 - 12 : 944, y = 170;
	if (480 - w / 2 - 16 >= l && 480 + w / 2 + 16 <= r) { tui_rect(480 - w / 2 - 16, y, 480 + w / 2 + 16, y + 44, bg); tui_text(480, y + 33, 30, 2, c, s); return; }
	float bw = r - l - 32; int n = tui_wrap_ex(l + 16, 0, bw, 30, 36, c, s, 3, 2, 0);
	tui_rect(l, y, r, y + 8 + n * 36, bg); tui_wrap_ex(l + 16, y + 33, bw, 30, 36, c, s, 3, 2, 1);
}
// a centred prompt: label, gauge (frac < 0: none), key line ("HOLD {s}")
static void tui_prompt(const char *label, float frac, const char *key, float y, float a) {
	HUD_TAG("tut.prompt");   // ui b42 (overlap checker)
	float wl = label ? tui_text_w(label, TUI_FB) : 0, wk = key ? tui_text_w(key, TUI_FB) : 0, w = fmaxf(fmaxf(wl, wk), 220) + 40, h = (label ? 34 : 0) + (frac >= 0 ? 16 : 0) + (key ? 34 : 0) + 8;
	float bg[4] = { 0.04f * a, 0.06f * a, 0.08f * a, 0.7f * a }, wc[4] = { 0.95f, 0.96f, 0.97f, a }; float yy = y;
	tui_rect(480 - w / 2, y, 480 + w / 2, y + h, bg);
	if (label) { tui_text(480, yy + 28, TUI_FB, 2, wc, label); yy += 34; }
	if (frac >= 0) { float d[4] = { 0.25f * a, 0.05f * a, 0.06f * a, 0.85f * a }, r[4] = { 0.88f * a, 0.14f * a, 0.18f * a, a }; if (frac > 1) frac = 1;
		tui_rect(480 - w / 2 + 16, yy + 4, 480 + w / 2 - 16, yy + 12, d); tui_rect(480 - w / 2 + 16, yy + 4, 480 - w / 2 + 16 + (w - 32) * frac, yy + 12, r); yy += 16; }
	if (key) tui_text(480, yy + 28, TUI_FB, 2, wc, key);
}
// the Think Tank hacking read-out: "Bypassing encryption..." red band with the code being cracked
static void tui_hack(float frac, unsigned seed, float a) {
	HUD_TAG("tut.hack");   // ui b42 (overlap checker)
	char code[16]; int n = 10, known = (int)(frac * n + 0.0001f); unsigned s = seed;
	for (int i = 0; i < n; i++) { s = s * 1103515245u + 12345u; code[i] = i < known ? (char)('0' + (s >> 16) % 10) : '?'; } code[n] = 0;
	float r[4] = { 0.62f * a, 0.08f * a, 0.10f * a, 0.85f * a }, w[4] = { 1, 0.95f, 0.95f, a }, p[4] = { 1, 0.78f, 0.78f, a }, g[4] = { 0.95f, 0.96f, 0.97f, a };
	tui_rect(330, 372, 630, 418, r); tui_text(480, 366, TUI_FS, 2, p, "Bypassing encryption...");
	tui_text(480, 409, 36, 2, w, code);
	tui_text(480, 444, TUI_FS, 2, g, "Complete hacking to shut down defensive protocols.");
}
// the waypoint flag at a screen point, the distance under it; edge: 1 when it is pinned to the screen edge
static void tui_marker(float sx, float sy, float dist_m, int edge, float a) {
	HUD_TAG("~tut.marker");   // ui b42 (overlap checker)
	float m[4] = { a, a, a, a }; tui_img(TUI_FLAG, sx - 19, sy - 19, 38, 38, m);
	if (!edge) { char b[16]; snprintf(b, sizeof(b), "%dm", (int)(dist_m + 0.5f)); float w[4] = { 0.97f, 0.98f, 1, a }; tui_text(sx, sy + 40, TUI_FS, 2, w, b); }
}
// ------------------------------------------------------------------ explanation screen (CYBERBRAIN SKILL, THERM-OPTIC CAMO, INTEL, ...)
// re-flowed for legibility: video and tier gauges on the left, the bullet text on the right, NEXT (72 px) at the bottom
typedef struct { const char *title; const char *text; int tier_values[2]; } TuiExplain;
#define TUI_NEXT_X0 624.f   // b59: under the text column (the 16:9 video's gauges reach lower on the left)
#define TUI_NEXT_Y0 462.f
#define TUI_NEXT_X1 824.f
#define TUI_NEXT_Y1 534.f
static void tui_gauge(int id, float x, float y, int tier, int value, const float *col, const char *label, float a) {   // x, y: top left of a 220 x 160 block
	float m[4] = { a, a, a, a }, c[4], w[4] = { 1, 1, 1, a }, g[4] = { 0.85f, 0.87f, 0.88f, a }; tui_col(c, col, a);
	tui_text(x, y + 24, TUI_FB, 0, c, label); tui_rect(x, y + 30, x + tui_text_w(label, TUI_FB), y + 31.5f, c);
	tui_img(id, x, y + 40, 100, 100, m); tui_text(x + 50, y + 162, TUI_FS, 2, w, "{right}");
	char b[16]; snprintf(b, sizeof(b), "%d", value); tui_text(x + 108, y + 116, 36, 0, w, b);
	tui_text(x + 108, y + 76, TUI_FS, 0, g, "TIER"); { float bx = x + 112 + tui_text_w("TIER", TUI_FS); tui_rect(bx, y + 58, bx + 20, y + 80, c); float d[4] = { 0.05f, 0.08f, 0.1f, a }; snprintf(b, sizeof(b), "%d", tier); tui_text(bx + 10, y + 77, TUI_FS, 2, d, b); }
	tui_text(x + 108, y + 140, TUI_FS, 0, g, "INTEL");
}
// vtex: the video frame texture (0: black panel); t: seconds on this screen; next: show the NEXT button
static void tui_explain(const TuiExplain *e, GLuint vtex, float t, int next) {
	HUD_TAG("#tut.explain");   // ui b42 (overlap checker)
	float a = tui_ease(t / 0.3f), dim[4] = { 0.01f, 0.02f, 0.03f, 0.85f * a }; tui_rect(0, 0, 960, 544, dim);
	float yc[4]; tui_col(yc, TUI_YEL, a); tui_text(480, 40, TUI_FH, 2, yc, e->title);
	// b59 (user: the clips looked stretched wide): the video box is 16:9 (the clips are encoded 512x288, make_tut_clips.py)
	float X0 = 24, Y0 = 54, X1 = 464, Y1 = 54 + 440 * 9 / 16.f, cyan[4] = { 0.50f, 0.84f, 0.90f, 0.9f * a }, cyl[4] = { 0.50f, 0.84f, 0.90f, 0.5f * a };
	{ float bk[4] = { 0, 0, 0, a }; tui_rect(X0 + 4, Y0 + 4, X1 - 4, Y1 - 4, bk); }
	if (vtex) { float xy[4][2] = { { X0 + 5, Y0 + 5 }, { X1 - 5, Y0 + 5 }, { X1 - 5, Y1 - 5 }, { X0 + 5, Y1 - 5 } }, uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } }, m[4] = { a, a, a, a };
		hud_poly(vtex, TUI.nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, m); }
	tui_rect(X0, Y0 + 26, X0 + 2, Y1 - 26, cyl); tui_rect(X1 - 2, Y0 + 26, X1, Y1 - 26, cyl); tui_rect(X0 + 30, Y0, X1 - 30, Y0 + 1.5f, cyl); tui_rect(X0 + 30, Y1 - 1.5f, X1 - 30, Y1, cyl);
	{ const float S = 30; float c[4][4][2] = { { { X0, Y0 }, { X0 + S, Y0 }, { X0 + S, Y0 + S }, { X0, Y0 + S } }, { { X1, Y0 }, { X1 - S, Y0 }, { X1 - S, Y0 + S }, { X1, Y0 + S } },
		{ { X1, Y1 }, { X1 - S, Y1 }, { X1 - S, Y1 - S }, { X1, Y1 - S } }, { { X0, Y1 }, { X0 + S, Y1 }, { X0 + S, Y1 - S }, { X0, Y1 - S } } };
	  for (int k = 0; k < 4; k++) tui_img4(TUI_BRACKET, (const float (*)[2])c[k], cyan); }
	tui_gauge(TUI_GAUGE1, 14, Y1 + 6, 1, e->tier_values[0] ? e->tier_values[0] : 275, TUI_CYN, "TIER 1", a);
	tui_gauge(TUI_GAUGE2, 250, Y1 + 6, 2, e->tier_values[1] ? e->tier_values[1] : 400, TUI_YEL, "TIER 2", a);
	{ float ln[4] = { 0.40f, 0.48f, 0.52f, 0.6f * a }; tui_rect(488, Y0, 489.5f, 446, ln); }
	float w[4]; tui_col(w, TUI_WHT, a); tui_wrap(502, 84, 446, TUI_FB, TUI_LH, w, e->text, 13);
	if (next) { float p = 0.8f + 0.2f * sinf(t * 4); float bg[4] = { 0.10f * a, 0.15f * a, 0.18f * a, 0.9f * a }, bd[4] = { 0.50f, 0.84f, 0.90f, 0.8f * a }, wb[4] = { 1, 1, 1, a * p };
		tui_rect(TUI_NEXT_X0, TUI_NEXT_Y0, TUI_NEXT_X1, TUI_NEXT_Y1, bg); tui_rect(TUI_NEXT_X0, TUI_NEXT_Y0, TUI_NEXT_X1, TUI_NEXT_Y0 + 2, bd); tui_rect(TUI_NEXT_X0, TUI_NEXT_Y1 - 2, TUI_NEXT_X1, TUI_NEXT_Y1, bd);
		tui_text((TUI_NEXT_X0 + TUI_NEXT_X1) / 2, TUI_NEXT_Y0 + 47, 28, 2, wb, "{x}  NEXT"); }
}
// ------------------------------------------------------------------ program loading screen (InGameLoading, footage 1:04)
typedef struct { const char *program, *subtitle, *band, *tip, *bg; } TuiLoading;
static void tui_loading(const TuiLoading *L, float progress, float t) {
	HUD_TAG("#tut.loading");   // ui b42 (overlap checker)
	if (!TUI.ok) return;
	static char cur[48];
	if (strcmp(cur, L->bg)) { if (TUI.ld_tex) { hud_flush(); glDeleteTextures(1, &TUI.ld_tex); TUI.ld_tex = 0; }
		char p[200]; snprintf(p, sizeof(p), "%smload_%s.gtx", TUI.dir, L->bg); TUI.ld_tex = menu_tex_load(p); snprintf(cur, sizeof(cur), "%s", L->bg); }
	if (!tui_begin()) return;
	if (TUI.ld_tex) { float xy[4][2] = { { 0, 0 }, { 960, 0 }, { 960, 544 }, { 0, 544 } }, uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
		hud_poly(TUI.ld_tex, TUI.nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, HUD_W1); }
	// b59 (user: the loading screens covered the operators' faces, too cluttered): no bars over the picture; the program's
	// title goes to the side away from the operator (Tutorial1_big: on the left, its head ~(317, 136); Tutorial2/3_big: on the
	// right, heads ~(653, 109) / (700, 136)) in a block as wide as its text; band, tip and the bar sit low, under the heads
	int right = !strcmp(L->bg, "tutorial");
	if (L->program && L->program[0]) {   // b109 (user: "on the top left it says tutorial intro movie before the intro movie plays please remove that text"): a page without a program name has no title block
	{ for (int i = 0; i < 6; i++) { float k[4] = { 0, 0, 0, 0.55f * (1 - i / 6.f) }; tui_rect(0, i * 8, 300, i * 8 + 8, k); } }   // (behind TUTORIAL only)
	{ float g[4] = { 0.55f, 0.58f, 0.6f, 1 }; tui_rect(6, 14, 11, 42, g); }
	tui_text(20, 40, TUI_FT, 0, TUI_WHT, "TUTORIAL");
	{ float lx = 20 + tui_text_w("TUTORIAL", TUI_FT) + 16, w[4] = { 0.9f, 0.92f, 0.93f, 1 };   // the slanted tick after it
	  float xy[4][2] = { { lx, 46 }, { lx + 4, 46 }, { lx + 28, 20 }, { lx + 24, 20 } }, uv[4][2] = { { 0.5f / TUI_W, 0.5f / TUI_H }, { 7.5f / TUI_W, 0.5f / TUI_H }, { 7.5f / TUI_W, 7.5f / TUI_H }, { 0.5f / TUI_W, 7.5f / TUI_H } };
	  hud_poly(TUI.tex, TUI.nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, w); tui_rect(lx + 28, 18, lx + 36, 20, w); }
	{ const char *sub = !L->subtitle ? "COMPLETE THE PROSTHETIC CALIBRATION PROGRAM" : L->subtitle;
	  float w = fmaxf(tui_text_w(L->program, TUI_FT), sub[0] ? tui_text_w(sub, TUI_FS) : 0) + 28, x0 = right ? 960 - w : 0, x1 = right ? 960 : w, tx = right ? 944 : 16; int al = right ? 1 : 0;
	  float sh[4] = { 0, 0, 0, 0.45f }; tui_rect(x0, 62, x1, sub[0] ? 150 : 128, sh);
	  tui_text(tx, 96, TUI_FT, al, TUI_WHT, L->program);
	  { float g[4] = { 0.85f, 0.87f, 0.88f, 1 }; tui_text(tx + (right ? 0 : 2), 118, TUI_FS, al, g, "2029.10.19"); }
	  if (sub[0]) tui_text(tx, 142, TUI_FS, al, TUI_WHT, sub); }
	}
	int tl = L->tip ? tui_wrap_ex(40, 0, 880, TUI_FB, TUI_LH, TUI_WHT, L->tip, 2, 2, 0) : 0;
	// the low block, bottom up: the bar (528), Loading... (518), the tip's lines (last baseline 494), the band title above them
	float tb = 494 - (tl > 1 ? tl - 1 : 0) * TUI_LH, bt = (tl ? tb : 500) - 40, y0 = (L->band ? bt - 32 : tb - 26) - 10;
	for (int i = 0; i < 10; i++) { float b[4] = { 0.01f, 0.03f, 0.04f, 0.88f * (i + 1) / 10.f }; float ya = y0 - 40 + i * 4; tui_rect(0, ya, 960, ya + 4, b); }
	{ float b[4] = { 0.01f, 0.03f, 0.04f, 0.88f }; tui_rect(0, y0, 960, 544, b); }
	if (L->band) { float g[4] = { 0.78f, 0.83f, 0.85f, 1 }; char sp[96]; int n = 0;   // letter-spaced band title
		for (const char *p = L->band; *p && n < 92; p++) { sp[n++] = *p; if (p[1]) sp[n++] = ' '; } sp[n] = 0; tui_text(480, bt, TUI_FT, 2, g, sp); }
	if (L->tip) tui_wrap_ex(40, tb, 880, TUI_FB, TUI_LH, TUI_WHT, L->tip, 2, 2, 1);
	{ float g[4] = { 0.88f, 0.9f, 0.91f, 1 }; tui_text(120, 518, TUI_FS, 0, g, "Loading..."); }
	if (progress >= 0) { if (progress > 1) progress = 1; float bg[4] = { 0.12f, 0.16f, 0.18f, 0.9f }, fg[4] = { 0.42f, 0.62f, 0.78f, 1 };
		tui_rect(120, 528, 840, 536, bg); tui_rect(120, 528, 120 + 720 * progress, 536, fg); }
	(void)t; tui_end();
}
// ------------------------------------------------------------------ the cyber "connect" sequence before each program (footage 1:16-1:26):
// falling data lines, "Prosthetic Calibration Program" -> "Connecting to system..." -> "Connected", then a white flash
static void tui_connect(float t, float len) {
	HUD_TAG("#tut.connect");   // ui b42 (overlap checker)
	float out = t > len - 0.6f ? (len - t) / 0.6f : 1; if (out < 0) out = 0;
	float dark[4] = { 0.0f, 0.02f, 0.03f, 0.92f * out }; tui_rect(0, 0, 960, 544, dark);
	for (int i = 0; i < 60; i++) {   // data rain: columns of cyan dashes
		unsigned h = (unsigned)i * 2654435761u; float x = (float)(h % 960), sp = 120 + (h >> 10) % 200, ph = ((h >> 4) % 1000) / 1000.f;
		float y = fmodf(t * sp + ph * 600, 640) - 60, L = 30 + (h >> 7) % 90, a = (0.15f + 0.35f * ((h >> 12) % 100) / 100.f) * out;
		float c[4] = { 0.25f, 0.85f, 0.92f, a }; tui_rect(x, y, x + 1.5f + (h >> 20) % 2, y + L, c);
	}
	const char *msg = t < 2.2f ? "Prosthetic Calibration Program" : t < 3.6f ? "Connecting to system..." : "Connected";
	float a = tui_ease(t / 0.4f) * out, c[4]; tui_col(c, TUI_YEL, a); tui_text(480, 210, 30, 2, c, msg);
	if (t > 1.0f && t < len - 0.6f) {   // SKIP: a 72 px touch target (bottom right) as well as Cross
		float bg[4] = { 0.04f, 0.10f, 0.12f, 0.75f * out }, bd[4] = { 0.50f, 0.84f, 0.90f, 0.6f * out }, w[4] = { 0.9f, 0.94f, 0.96f, 0.9f * out };
		tui_rect(800, 464, 952, 536, bg); tui_rect(800, 464, 952, 466, bd); tui_text(876, 509, TUI_FB, 2, w, "{x} SKIP"); }
	if (t > len - 0.6f) { float f = 1 - (len - t) / 0.6f; float w[4] = { 0.85f, 0.97f, 1, (f < 0.5f ? f * 2 : 2 - f * 2) * 0.9f }; tui_rect(0, 0, 960, 544, w); }
}
