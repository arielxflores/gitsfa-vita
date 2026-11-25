// The game's own in-game HUD (InGameUI.swf / InGameUI_D.swf, Scaleform). tools/ui/make_hud.py renders every
// part flat in its own Flash plane and stores the plane's perspective (local -> screen homography) and
// colour transform; here each part is one perspective-correct quad. Gauges use a ramp texel per pixel
// (the fraction at which it lights up), text uses the Eurostile glyphs from fonts_en.swf.
#pragma once
#ifndef VGL_XFER_WAIT
#ifdef __vita__
#define VGL_XFER_WAIT() sceGxmTransferFinish()   // vitaGL uploads copy asynchronously: wait before the source buffer is reused
#else
#define VGL_XFER_WAIT() ((void)0)
#endif
#endif
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef struct { char name[32]; unsigned char tex, p0, p1, p2; unsigned short x, y, w, h; float loc[4]; } HImg;
typedef struct { char name[32]; float H[9]; float mul[4], add[4]; } HNode;
typedef struct { char name[32]; int node; float b[4]; float size; int align; unsigned char col[4]; } HField;
// a pack = one .ghud file (the in-game HUD, the menus ...); hud_use() picks the one the draw calls refer to
typedef struct { HImg *img; int nimg; HNode *node; int nnode; HField *field; int nfield; short glyph[256]; float adv[256], asc, desc; GLuint tex[2]; int tw[2], th[2]; } HudPack;
#define HUD_MAXPACK 4
static HudPack hud_pack[HUD_MAXPACK], *HP = &hud_pack[0]; static int hud_npack;
#define himg (HP->img)
#define nhimg (HP->nimg)
#define hnode (HP->node)
#define nhnode (HP->nnode)
#define hfield (HP->field)
#define nhfield (HP->nfield)
#define hglyph (HP->glyph)
#define hadv (HP->adv)
#define hasc (HP->asc)
#define hdesc (HP->desc)
#define htex (HP->tex)
#define htw (HP->tw)
#define hth (HP->th)
static GLuint hprog; static int hud_ok;
static float hud_dx, hud_dy;          // screen offset added to everything drawn (for parts that follow a 3D point)
#define HUD_VF 17
#define HUD_MAXQ 700
static float hvb[HUD_MAXQ * 6 * HUD_VF]; static int hvn; static GLuint hcur;
static const char *HUD_VS =
"void main(float4 pos, float4 uv, float4 mul, float4 add, float thr,"
" float4 out vpos : POSITION, float4 out t0 : TEXCOORD0, float4 out m0 : TEXCOORD1, float4 out a0 : TEXCOORD2, float out th : TEXCOORD3) {"
" vpos = pos; t0 = uv; m0 = mul; a0 = add; th = thr; }";
static const char *HUD_FS =
"float4 main(float4 t0 : TEXCOORD0, float4 m0 : TEXCOORD1, float4 a0 : TEXCOORD2, float th : TEXCOORD3, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" float4 c = tex2D(tex, t0.xy); float r = tex2D(tex, t0.zw).r; c *= saturate((th - r) * 200.0 + 0.5);"
" float4 o; o.rgb = (c.rgb * m0.rgb + a0.rgb * c.a) * m0.a; o.a = c.a * m0.a; o.rgb = min(o.rgb, o.aaa); return o; }";
static int hud_find_img(const char *n) { for (int i = 0; i < nhimg; i++) if (!strcmp(himg[i].name, n)) return i; return -1; }
static int hud_find_node(const char *n) { for (int i = 0; i < nhnode; i++) if (!strcmp(hnode[i].name, n)) return i; return -1; }
static int hud_find_field(const char *n) { for (int i = 0; i < nhfield; i++) if (!strcmp(hfield[i].name, n)) return i; return -1; }
typedef GLuint (*hud_mkshader_fn)(GLenum, const char *);
static void hud_flush(void);
static void hud_use(int pack) { if (pack >= 0 && pack < hud_npack && HP != &hud_pack[pack]) { hud_flush(); HP = &hud_pack[pack]; } }
// loads a .ghud into a new pack and makes it current; returns the pack index (<0 on error)
static int hud_load(const char *path, hud_mkshader_fn mk) {
	if (hud_npack >= HUD_MAXPACK) return -9;
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	HP = &hud_pack[hud_npack]; memset(HP, 0, sizeof(*HP));
	char mg[4]; unsigned ver, d[4], n; if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "GHUD", 4) || fread(&ver, 4, 1, f) != 1) { fclose(f); return -2; }
	if (fread(d, 4, 4, f) != 4) { fclose(f); return -3; } htw[0] = d[0]; hth[0] = d[1]; htw[1] = d[2]; hth[1] = d[3];
	fread(&n, 4, 1, f); nhimg = n; himg = malloc(sizeof(HImg) * n);
	for (unsigned i = 0; i < n; i++) { fread(himg[i].name, 1, 32, f); fread(&himg[i].tex, 1, 4, f); fread(&himg[i].x, 2, 4, f); fread(himg[i].loc, 4, 4, f); }
	fread(&n, 4, 1, f); nhnode = n; hnode = malloc(sizeof(HNode) * n);
	for (unsigned i = 0; i < n; i++) { fread(hnode[i].name, 1, 32, f); fread(hnode[i].H, 4, 9, f); fread(hnode[i].mul, 4, 4, f); fread(hnode[i].add, 4, 4, f); }
	fread(&n, 4, 1, f); nhfield = n; hfield = malloc(sizeof(HField) * (n ? n : 1));
	for (unsigned i = 0; i < n; i++) { fread(hfield[i].name, 1, 32, f); fread(&hfield[i].node, 4, 1, f); fread(hfield[i].b, 4, 4, f); fread(&hfield[i].size, 4, 1, f); fread(&hfield[i].align, 4, 1, f); fread(hfield[i].col, 1, 4, f); }
	fread(&n, 4, 1, f); fread(&hasc, 4, 1, f); fread(&hdesc, 4, 1, f);
	for (int i = 0; i < 256; i++) { hglyph[i] = -2; hadv[i] = 0.5f; }
	for (unsigned i = 0; i < n; i++) { unsigned short code; short ix; float adv; fread(&code, 2, 1, f); fread(&ix, 2, 1, f); fread(&adv, 4, 1, f); if (code < 256) { hglyph[code] = ix; hadv[code] = adv; } }
	for (int t = 0; t < 2; t++) {
		size_t sz = (size_t)htw[t] * hth[t] * 4; unsigned char *px = malloc(sz);
		if (!px || fread(px, 1, sz, f) != sz) { free(px); fclose(f); return -4; }
		glGenTextures(1, &htex[t]); glBindTexture(GL_TEXTURE_2D, htex[t]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, htw[t], hth[t], 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (t == 1) { glGenerateMipmap(GL_TEXTURE_2D); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); }
		else glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		free(px);
	}
	fclose(f);
	int pack = hud_npack++;
	if (hprog) return pack;
	GLuint vs = mk(GL_VERTEX_SHADER, HUD_VS), fs = mk(GL_FRAGMENT_SHADER, HUD_FS); GLint ok = 0;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -5; glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -6;
	hprog = glCreateProgram(); glAttachShader(hprog, vs); glAttachShader(hprog, fs);
	glBindAttribLocation(hprog, 0, "pos"); glBindAttribLocation(hprog, 1, "uv"); glBindAttribLocation(hprog, 2, "mul"); glBindAttribLocation(hprog, 3, "add"); glBindAttribLocation(hprog, 4, "thr");
	glLinkProgram(hprog); glGetProgramiv(hprog, GL_LINK_STATUS, &ok); if (!ok) return -7;
	glUseProgram(hprog); glUniform1i(glGetUniformLocation(hprog, "tex"), 0);
	hud_ok = 1; return pack;
}
static void hud_flush(void) {
	if (!hvn) return;
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, hcur);
	for (int a = 0; a < 5; a++) glEnableVertexAttribArray(a);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 4);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 8); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 12);
	glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, HUD_VF * 4, hvb + 16);
	glDrawArrays(GL_TRIANGLES, 0, hvn); hvn = 0;
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
}
static void hud_begin(void) {
	if (!hud_ok) return;
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glUseProgram(hprog); hvn = 0; hcur = 0; hud_dx = hud_dy = 0;
}
static void hud_end(void) {
	if (!hud_ok) return;
	hud_flush(); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
}
static const float HUD_W1[4] = { 1, 1, 1, 1 }, HUD_Z4[4] = { 0, 0, 0, 0 };
// image im in the plane of node nd; A (2x3 row-major, may be NULL) maps image-local -> node-local; ramp/thr for gauges
static void hud_img(int im, int nd, const float *A, const float *mul, const float *add, int ramp, float thr) {
	if (!hud_ok || im < 0 || nd < 0) return;
	const HImg *I = &himg[im]; const HNode *N = &hnode[nd];
	if (htex[I->tex] != hcur) { hud_flush(); hcur = htex[I->tex]; }
	if (hvn + 6 > HUD_MAXQ * 6) hud_flush();
	mul = mul ? mul : N->mul; add = add ? add : N->add;
	float tw = (float)htw[I->tex], th_ = (float)hth[I->tex];
	const float *l = I->loc; float lx[4] = { l[0], l[2], l[2], l[0] }, ly[4] = { l[1], l[1], l[3], l[3] };
	float u[4] = { I->x / tw, (I->x + I->w) / tw, (I->x + I->w) / tw, I->x / tw }, v[4] = { I->y / th_, I->y / th_, (I->y + I->h) / th_, (I->y + I->h) / th_ };
	float ru[4], rv[4];
	for (int k = 0; k < 4; k++) {
		if (ramp >= 0) { const HImg *R = &himg[ramp];
			ru[k] = (R->x + (lx[k] - R->loc[0]) / (R->loc[2] - R->loc[0]) * R->w) / tw; rv[k] = (R->y + (ly[k] - R->loc[1]) / (R->loc[3] - R->loc[1]) * R->h) / th_; }
		else { ru[k] = 2.0f / tw; rv[k] = 2.0f / th_; }
	}
	float th2 = ramp >= 0 ? thr : 2.0f, P[4][4];
	for (int k = 0; k < 4; k++) {
		float x = lx[k], y = ly[k];
		if (A) { float x2 = A[0] * x + A[1] * y + A[2], y2 = A[3] * x + A[4] * y + A[5]; x = x2; y = y2; }
		const float *H = N->H; float X = H[0] * x + H[1] * y + H[2], Y = H[3] * x + H[4] * y + H[5], W = H[6] * x + H[7] * y + H[8];
		X += hud_dx * W; Y += hud_dy * W;
		P[k][0] = X / 480.0f - W; P[k][1] = W - Y / 272.0f; P[k][2] = 0; P[k][3] = W;
	}
	static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
	for (int t = 0; t < 6; t++) { int k = tri[t]; float *o = &hvb[hvn * HUD_VF];
		o[0] = P[k][0]; o[1] = P[k][1]; o[2] = 0; o[3] = P[k][3]; o[4] = u[k]; o[5] = v[k]; o[6] = ru[k]; o[7] = rv[k];
		memcpy(o + 8, mul, 16); memcpy(o + 12, add, 16); o[16] = th2; hvn++; }
}
// a convex polygon (fan) in node nd's plane with its own texture (e.g. the minimap image); uv per vertex
static void hud_poly(GLuint tex, int nd, const float (*xy)[2], const float (*uv)[2], int n, const float *mul) {
	if (!hud_ok || nd < 0 || n < 3) return;
	if (tex != hcur) { hud_flush(); hcur = tex; }
	if (hvn + (n - 2) * 3 > HUD_MAXQ * 6) hud_flush();
	const HNode *N = &hnode[nd]; const float *H = N->H; float P[16][4]; if (n > 16) n = 16;
	for (int k = 0; k < n; k++) { float x = xy[k][0], y = xy[k][1], X = H[0] * x + H[1] * y + H[2], Y = H[3] * x + H[4] * y + H[5], W = H[6] * x + H[7] * y + H[8];
		X += hud_dx * W; Y += hud_dy * W; P[k][0] = X / 480.0f - W; P[k][1] = W - Y / 272.0f; P[k][2] = 0; P[k][3] = W; }
	for (int t = 1; t + 1 < n; t++) { int ix[3] = { 0, t, t + 1 };
		for (int j = 0; j < 3; j++) { int k = ix[j]; float *o = &hvb[hvn * HUD_VF];
			o[0] = P[k][0]; o[1] = P[k][1]; o[2] = 0; o[3] = P[k][3]; o[4] = uv[k][0]; o[5] = uv[k][1]; o[6] = uv[k][0]; o[7] = uv[k][1];
			memcpy(o + 8, mul ? mul : HUD_W1, 16); memset(o + 12, 0, 16); o[16] = 2.0f; hvn++; } }
}
// width of a string in em units
static float hud_text_w(const char *s) { float w = 0; for (; *s; s++) w += hadv[(unsigned char)*s]; return w; }
// text in node nd's plane: pen at (x, baseline y), size = em in node units; align 0 left, 1 right, 2 centre
static void hud_text(int nd, float x, float y, float size, int align, const float *mul, const float *add, const char *s) {
	float w = hud_text_w(s) * size;
	if (align == 1) x -= w; else if (align == 2) x -= w / 2;
	for (; *s; s++) { int c = (unsigned char)*s; int g = hglyph[c];
		if (g >= 0) { float A[6] = { size, 0, x, 0, size, y }; hud_img(g, nd, A, mul ? mul : HUD_W1, add ? add : HUD_Z4, -1, 1); }
		x += hadv[c] * size; }
}
// a dynamic text field from the SWF (its bounds, size, alignment and colour)
static void hud_field(int fi, const char *s, const float *mul) {
	if (fi < 0) return; const HField *F = &hfield[fi]; float col[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f };
	if (mul) for (int k = 0; k < 4; k++) col[k] *= mul[k];
	float w = hud_text_w(s) * F->size, x = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2;
	hud_text(F->node, x, F->b[2] + 2 + hasc * F->size, F->size, 0, col, HUD_Z4, s);
}
// screen position (Vita pixels) of a node-local point
static void hud_node_screen(int nd, float x, float y, float *sx, float *sy) {
	const float *H = hnode[nd].H; float W = H[6] * x + H[7] * y + H[8]; *sx = (H[0] * x + H[1] * y + H[2]) / W; *sy = (H[3] * x + H[4] * y + H[5]) / W;
}
// digits of v into slot nodes base[0] = ones, base[1] = tens ... (n slots); left: start in the leftmost slot
static void hud_number(const int *slots, int n, int glyph0, int v, int left, const float *mul, const float *add, const int *zslots, int zimg) {
	char t[16]; int len = snprintf(t, sizeof(t), "%d", v < 0 ? 0 : v); if (len > n) len = n;
	if (left) { for (int j = 0; j < len; j++) hud_img(glyph0 + (t[j] - '0'), slots[n - 1 - j], NULL, mul, add, -1, 1); return; }
	for (int i = 0; i < n; i++) {
		if (i < len) hud_img(glyph0 + (t[len - 1 - i] - '0'), slots[i], NULL, mul, add, -1, 1);
		else if (zslots && zslots[i] >= 0) hud_img(zimg, zslots[i], NULL, NULL, NULL, -1, 1);
	}
}
