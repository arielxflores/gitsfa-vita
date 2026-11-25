// Host stand-in for the vitaGL subset used by model.h / chr.h: buffers and textures in memory, and a software
// rasterizer that runs the model shader (MD_VS / MD_FS_PLAIN) per vertex / pixel. For previews and tests only.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef int GLsizei; typedef float GLfloat;
enum { GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,
	GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_DYNAMIC_DRAW, GL_WRITE_ONLY, GL_VERTEX_SHADER,
	GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TEXTURE0, GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE, GL_FLOAT, GL_BYTE, GL_UNSIGNED_BYTE,
	GL_TRIANGLES, GL_UNSIGNED_SHORT, GL_STENCIL_TEST, GL_SCISSOR_TEST, GL_KEEP, GL_REPLACE, GL_GREATER, GL_STENCIL_BUFFER_BIT, GL_DEPTH_BUFFER_BIT, GL_COLOR_BUFFER_BIT,
	GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE4, GL_RGB, GL_RGBA, GL_ACTIVE_UNIFORMS, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE, GL_REPEAT, GL_NEAREST };
enum { GL_FALSE = 0, GL_TRUE = 1 };
enum { GL_ZERO = 100, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA };
#ifndef SW_W
#define SW_W 640
#endif
#ifndef SW_H
#define SW_H 640
#endif
typedef struct { int w, h; unsigned char *px; } HTex;
typedef struct { unsigned char *data; int size; } HBuf;
static HTex htex[256]; static int nhtex = 1, htex_bound;
static HBuf hbuf[4096]; static int nhbuf = 1, hbuf_arr, hbuf_el;
typedef struct { int buf, size, type, norm, stride, off; } HAttr;
static HAttr hattr[5];
static float h_pal_stride = 4;   // rows per palette entry in the emulated shader (model.h MD_VS: 4 since b41; 3 = the b40 layout, STRIDE3)
static float hu_m[4][4], hu_pal[192][4], hu_ldir[4], hu_tint[4], hu_fog[4], hu_fogp[4], hu_emis[4];   // (b71 emis: model.h MD_FS glow)
static int hblend, hdepth = 1, hdmask = 1; static GLenum hbs = GL_ONE, hbd = GL_ZERO;
static float hfb[SW_H][SW_W][3], hzb[SW_H][SW_W]; static long hpix;
static unsigned char hcov[SW_H][SW_W]; static int hvw = SW_W, hvh = SW_H, hfbo;   // viewport size (offscreen tests: skin_gpu.h), covered pixels
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = nhtex++; }
static void glDeleteTextures(int n, const GLuint *t) { (void)n; (void)t; }
static void glBindTexture(GLenum t, GLuint id) { (void)t; htex_bound = id; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glActiveTexture(GLenum t) { (void)t; }
static void h_dxt_block(const unsigned char *b, int fmt, unsigned char out[16][4]) {
	unsigned char al[16]; for (int i = 0; i < 16; i++) al[i] = 255;
	const unsigned char *cb = b;
	if (fmt == 5) {
		unsigned a0 = b[0], a1 = b[1]; unsigned char t[8]; t[0] = a0; t[1] = a1;
		if (a0 > a1) for (int i = 1; i < 7; i++) t[i + 1] = (unsigned char)(((7 - i) * a0 + i * a1) / 7);
		else { for (int i = 1; i < 5; i++) t[i + 1] = (unsigned char)(((5 - i) * a0 + i * a1) / 5); t[6] = 0; t[7] = 255; }
		uint64_t ab = 0; for (int i = 0; i < 6; i++) ab |= (uint64_t)b[2 + i] << (8 * i);
		for (int i = 0; i < 16; i++) al[i] = t[(ab >> (3 * i)) & 7];
		cb = b + 8;
	}
	unsigned c0 = cb[0] | cb[1] << 8, c1 = cb[2] | cb[3] << 8; unsigned char c[4][4];
	c[0][0] = (c0 >> 11) * 255 / 31; c[0][1] = ((c0 >> 5) & 63) * 255 / 63; c[0][2] = (c0 & 31) * 255 / 31; c[0][3] = 255;
	c[1][0] = (c1 >> 11) * 255 / 31; c[1][1] = ((c1 >> 5) & 63) * 255 / 63; c[1][2] = (c1 & 31) * 255 / 31; c[1][3] = 255;
	if (c0 > c1 || fmt == 5) { for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; } c[2][3] = c[3][3] = 255; }
	else { for (int k = 0; k < 3; k++) { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; } c[2][3] = 255; c[3][3] = 0; }
	unsigned idx = cb[4] | cb[5] << 8 | cb[6] << 16 | (unsigned)cb[7] << 24;
	for (int i = 0; i < 16; i++) { int ci = (idx >> (2 * i)) & 3; memcpy(out[i], c[ci], 4); if (fmt == 5) out[i][3] = al[i]; }
}
static void glCompressedTexImage2D(GLenum t, GLint lv, GLenum fmt, GLsizei w, GLsizei h, GLint bd, GLsizei sz, const void *data) {
	(void)t; (void)bd; (void)sz; if (lv) return;
	HTex *T = &htex[htex_bound]; T->w = w; T->h = h; T->px = malloc(w * h * 4); const unsigned char *b = data; int f = fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 1 : 5;
	for (int by = 0; by < h / 4; by++) for (int bx = 0; bx < w / 4; bx++, b += f == 1 ? 8 : 16) { unsigned char o[16][4]; h_dxt_block(b, f, o);
		for (int i = 0; i < 16; i++) memcpy(T->px + (((by * 4 + i / 4) * w) + bx * 4 + i % 4) * 4, o[i], 4); }
}
static inline int sceGxmTransferFinish(void) { return 0; }
static void glGenBuffers(int n, GLuint *b) { for (int i = 0; i < n; i++) { b[i] = nhbuf++; hbuf[b[i]].data = NULL; hbuf[b[i]].size = 0; } }
static void glDeleteBuffers(int n, const GLuint *b) { for (int i = 0; i < n; i++) { free(hbuf[b[i]].data); hbuf[b[i]].data = NULL; } }
static void glBindBuffer(GLenum t, GLuint id) { if (t == GL_ARRAY_BUFFER) hbuf_arr = id; else hbuf_el = id; }
static void glBufferData(GLenum t, int size, const void *d, GLenum u) { (void)u; HBuf *B = &hbuf[t == GL_ARRAY_BUFFER ? hbuf_arr : hbuf_el]; free(B->data); B->data = malloc(size); B->size = size; if (d) memcpy(B->data, d, size); else memset(B->data, 0, size); }
static void *glMapBuffer(GLenum t, GLenum a) { (void)a; return hbuf[t == GL_ARRAY_BUFFER ? hbuf_arr : hbuf_el].data; }
static int glUnmapBuffer(GLenum t) { (void)t; return 1; }
static void glVertexAttribPointer(GLuint i, int size, GLenum type, GLboolean n, int stride, const void *off) { hattr[i].buf = hbuf_arr; hattr[i].size = size; hattr[i].type = type; hattr[i].norm = n; hattr[i].stride = stride; hattr[i].off = (int)(intptr_t)off; }
static void glEnableVertexAttribArray(GLuint i) { (void)i; }
static void glDisableVertexAttribArray(GLuint i) { (void)i; }
static GLuint glCreateShader(GLenum t) { return t; }
static void glDeleteShader(GLuint s) { (void)s; }
static void glGetActiveUniform(GLuint p, GLuint i, GLsizei n, GLsizei *l, GLint *sz, GLenum *ty, char *nm) { (void)p; (void)i; (void)ty; if (l) *l = 3; if (sz) *sz = 144; if (n > 3) strcpy(nm, "pal"); }
static void glTexImage2D(GLenum t, GLint lv, GLint ifmt, GLsizei w, GLsizei h, GLint bd, GLenum fmt, GLenum ty, const void *d) { (void)t; (void)lv; (void)ifmt; (void)w; (void)h; (void)bd; (void)fmt; (void)ty; (void)d; }
#define glGetShaderiv(a, b, c) (*(c) = 1)
#define glGetProgramiv(a, b, c) (*(c) = 1)
static GLuint glCreateProgram(void) { return 1; }
#define glAttachShader(a, b)
#define glBindAttribLocation(a, b, c)
#define glLinkProgram(a)
#define glUseProgram(a)
enum { HU_M0 = 1, HU_M1, HU_M2, HU_M3, HU_PAL, HU_LDIR, HU_TINT, HU_FOG, HU_FOGP, HU_DIF, HU_LUT, HU_EMIS };
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p;
	if (!strcmp(n, "m0")) return HU_M0; if (!strcmp(n, "m1")) return HU_M1; if (!strcmp(n, "m2")) return HU_M2; if (!strcmp(n, "m3")) return HU_M3;
	if (!strcmp(n, "pal") || !strcmp(n, "bm")) return HU_PAL;   // (bm: model.h's one-bone program, b41)
	 if (!strcmp(n, "ldir")) return HU_LDIR; if (!strcmp(n, "tint")) return HU_TINT; if (!strcmp(n, "fogc")) return HU_FOG; if (!strcmp(n, "fogp")) return HU_FOGP;
	if (!strcmp(n, "emis")) return HU_EMIS;
	if (!strcmp(n, "dif")) return HU_DIF; return -1; }
static void glUniform1i(GLint l, int v) { (void)l; (void)v; }
static void glUniform4fv(GLint l, int n, const float *v) {
	if (l >= HU_M0 && l <= HU_M3) memcpy(hu_m[l - HU_M0], v, 16);
	else if (l == HU_PAL) { memcpy(hu_pal, v, 16 * n);
		if (getenv("STRIDE3") && n > 3) { h_pal_stride = 3; for (int e = 0; e < n / 4; e++) for (int r = 0; r < 3; r++) memcpy(hu_pal[e * 3 + r], v + (e * 4 + r) * 4, 16); }   // (emulate the b40 layout: 3 rows per entry)
		else h_pal_stride = 4; } else if (l == HU_LDIR) memcpy(hu_ldir, v, 16); else if (l == HU_TINT) memcpy(hu_tint, v, 16);
	else if (l == HU_FOG) memcpy(hu_fog, v, 16); else if (l == HU_FOGP) memcpy(hu_fogp, v, 16); else if (l == HU_EMIS) memcpy(hu_emis, v, 16);
}
static void glEnable(GLenum c) { if (c == GL_BLEND) hblend = 1; if (c == GL_DEPTH_TEST) hdepth = 1; }
static void glDisable(GLenum c) { if (c == GL_BLEND) hblend = 0; if (c == GL_DEPTH_TEST) hdepth = 0; }
static void glBlendFunc(GLenum s, GLenum d) { hbs = s; hbd = d; }
static void glDepthMask(GLboolean m) { hdmask = m; }
static void glStencilOp(GLenum a, GLenum b, GLenum c) { (void)a; (void)b; (void)c; }
static void glStencilFunc(GLenum a, int b, unsigned c) { (void)a; (void)b; (void)c; }
static float hcc[4];
static void glClearColor(float r, float g, float b, float a) { hcc[0] = r; hcc[1] = g; hcc[2] = b; hcc[3] = a; }
static void glClear(GLenum m) { if (!hfbo || !(m & GL_COLOR_BUFFER_BIT)) return; for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { for (int k = 0; k < 3; k++) hfb[y][x][k] = hcc[k]; hzb[y][x] = 1; hcov[y][x] = 0; } }
static void glViewport(int a, int b, int c, int d) { (void)a; (void)b; hvw = c < SW_W ? c : SW_W; hvh = d < SW_H ? d : SW_H; }
static void glScissor(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static void h_attr(int i, int v, float *o) {
	HAttr *A = &hattr[i]; const unsigned char *p = hbuf[A->buf].data + A->off + v * A->stride;
	for (int k = 0; k < A->size; k++) {
		if (A->type == GL_FLOAT) { float f; memcpy(&f, p + 4 * k, 4); o[k] = f; }
		else if (A->type == GL_BYTE) o[k] = A->norm ? ((signed char)p[k]) / 127.f : (signed char)p[k];
		else o[k] = A->norm ? p[k] / 255.f : p[k];
	}
}
typedef struct { float pos[4], uv[2], sh, fog; } HV;
static int h_bi_from = -1;   // BIMODE 4: the bone indices of another vertex (one per group of vertices processed together)
static void h_vs(int v, HV *o) {
	float p[4] = { 0, 0, 0, 1 }, n[4] = { 0 }, uv[2] = { 0 }, bi[4] = { 0 }, bw[4] = { 0 };
	h_attr(0, v, p); h_attr(1, v, n); h_attr(2, v, uv); h_attr(3, h_bi_from >= 0 ? h_bi_from : v, bi); h_attr(4, v, bw); p[3] = 1;
	float s[3] = { 0 }, sn[3] = { 0 };
	static int bim = -1; if (bim < 0) bim = getenv("BIMODE") ? atoi(getenv("BIMODE")) : 0;   // device-bug models: 1 bone index read as 0, 2 read as i/255, 3 rows past 128 read as 0
	if (bim == 1) for (int j = 0; j < 4; j++) bi[j] = 0;
	if (bim == 2 && !hattr[3].norm) for (int j = 0; j < 4; j++) bi[j] /= 255.f;
	float hpal_t[192][4]; float (*PL)[4] = hu_pal;
	if (bim == 5) { for (int e = 0; e < 48; e++) for (int r = 0; r < 3; r++) { for (int k = 0; k < 3; k++) hpal_t[e * 4 + r][k] = hu_pal[e * 4 + k][r]; hpal_t[e * 4 + r][3] = hu_pal[e * 4 + r][3]; } PL = hpal_t; }   // 5: rotations transposed
#define hu_pal PL
	for (int j = 0; j < 4; j++) { int i = (int)(bi[j] * h_pal_stride + 0.5f); if (bw[j] <= 0) continue;
		if (bim == 7 && (i & 1)) i -= 1;   // 7: the b40 device: a 3-row fetch from a 16-mod-32 address reads from 16 bytes lower
		static int lim = -1; if (lim < 0) lim = getenv("BILIM") ? atoi(getenv("BILIM")) : 128;
		if (bim == 3 && i + 2 >= lim) continue;   // rows past lim read as zero
		for (int r = 0; r < 3; r++) { if (bim == 6) { int q = (i + r) % lim; s[r] += bw[j] * (hu_pal[q][0] * p[0] + hu_pal[q][1] * p[1] + hu_pal[q][2] * p[2] + hu_pal[q][3]); continue; }   // 6: rows wrap modulo lim
			s[r] += bw[j] * (hu_pal[i + r][0] * p[0] + hu_pal[i + r][1] * p[1] + hu_pal[i + r][2] * p[2] + hu_pal[i + r][3]);
			if (j < 2) sn[r] += bw[j] * (hu_pal[i + r][0] * n[0] + hu_pal[i + r][1] * n[1] + hu_pal[i + r][2] * n[2]); } }
#undef hu_pal
	float c[4] = { s[0], s[1], s[2], 1 };
	for (int r = 0; r < 4; r++) o->pos[r] = hu_m[r][0] * c[0] + hu_m[r][1] * c[1] + hu_m[r][2] * c[2] + hu_m[r][3] * c[3];
	o->uv[0] = uv[0]; o->uv[1] = uv[1];
	float l = sqrtf(sn[0] * sn[0] + sn[1] * sn[1] + sn[2] * sn[2]) + 1e-4f, d = (sn[0] * hu_ldir[0] + sn[1] * hu_ldir[1] + sn[2] * hu_ldir[2]) / l;
	o->sh = 0.40f + 0.75f * (d < 0 ? 0 : d > 1 ? 1 : d);
	float fz = o->pos[3] - hu_fogp[0]; o->fog = hu_fogp[2] * (1 - expf(-(fz > 0 ? fz : 0) * hu_fogp[1]));
}
static void h_tri(const HV *a, const HV *b, const HV *c) {
	if (a->pos[3] <= 1 || b->pos[3] <= 1 || c->pos[3] <= 1) return;
	const HV *v[3] = { a, b, c }; float sx[3], sy[3], sz[3], iw[3];
	for (int i = 0; i < 3; i++) { iw[i] = 1 / v[i]->pos[3]; sx[i] = (v[i]->pos[0] * iw[i] + 1) * 0.5f * hvw; sy[i] = (1 - v[i]->pos[1] * iw[i]) * 0.5f * hvh; sz[i] = v[i]->pos[2] * iw[i]; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]); if (fabsf(area) < 1e-6f) return;
	int x0 = (int)fmaxf(0, floorf(fminf(sx[0], fminf(sx[1], sx[2])))), x1 = (int)fminf(hvw - 1, ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))));
	int y0 = (int)fmaxf(0, floorf(fminf(sy[0], fminf(sy[1], sy[2])))), y1 = (int)fminf(hvh - 1, ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))));
	HTex *T = &htex[htex_bound];
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
		float px = x + 0.5f, py = y + 0.5f;
		float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area, w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area, w2 = 1 - w0 - w1;
		if (w0 < 0 || w1 < 0 || w2 < 0) continue;
		float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2]; if (z < -1 || z > 1) continue;
		if (hdepth && z > hzb[y][x]) continue;
		float pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2], u = (w0 * v[0]->uv[0] * iw[0] + w1 * v[1]->uv[0] * iw[1] + w2 * v[2]->uv[0] * iw[2]) / pw, vv = (w0 * v[0]->uv[1] * iw[0] + w1 * v[1]->uv[1] * iw[1] + w2 * v[2]->uv[1] * iw[2]) / pw;
		float sh = w0 * v[0]->sh + w1 * v[1]->sh + w2 * v[2]->sh, fog = w0 * v[0]->fog + w1 * v[1]->fog + w2 * v[2]->fog;
		float c[4] = { 1, 1, 1, 1 };
		if (T->px) { int tx = ((int)floorf(u * T->w) % T->w + T->w) % T->w, ty = ((int)floorf(vv * T->h) % T->h + T->h) % T->h; const unsigned char *t = T->px + (ty * T->w + tx) * 4; for (int k = 0; k < 4; k++) c[k] = t[k] / 255.f; }
		float out[3], g = 1 - c[3]; for (int k = 0; k < 3; k++) { float col = c[k] * (sh + g * hu_emis[k]) * hu_tint[k]; out[k] = col + (hu_fog[k] - col) * fog * hu_fog[3]; }   // (b71: + the glow)
		float al = (c[3] + g * hu_emis[3]) * hu_tint[3];
		if (hblend) { for (int k = 0; k < 3; k++) { float s = hbs == GL_SRC_ALPHA ? al : 1, d = hbd == GL_ONE ? 1 : hbd == GL_ONE_MINUS_SRC_ALPHA ? 1 - al : 0; hfb[y][x][k] = out[k] * s + hfb[y][x][k] * d; } }
		else for (int k = 0; k < 3; k++) hfb[y][x][k] = out[k];
		if (hdmask) hzb[y][x] = z; hpix++; hcov[y][x] = 1;
	}
}
static void glDrawElements(GLenum m, int n, GLenum t, const void *off) {
	(void)m; (void)t; const unsigned short *ix = (const unsigned short *)(hbuf[hbuf_el].data + (intptr_t)off);
	static int G = -1; if (G < 0) G = getenv("BIMODE") && atoi(getenv("BIMODE")) == 4 ? (getenv("BIGRP") ? atoi(getenv("BIGRP")) : 4) : 0;
	for (int i = 0; i + 2 < n; i += 3) { HV a, b, c;
		if (G) { h_bi_from = ix[(i / G) * G]; h_vs(ix[i], &a); h_bi_from = ix[((i + 1) / G) * G]; h_vs(ix[i + 1], &b); h_bi_from = ix[((i + 2) / G) * G]; h_vs(ix[i + 2], &c); h_bi_from = -1; }
		else { h_vs(ix[i], &a); h_vs(ix[i + 1], &b); h_vs(ix[i + 2], &c); }
		h_tri(&a, &b, &c); }
}
static void h_clear(float r, float g, float b) { for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { hfb[y][x][0] = r; hfb[y][x][1] = g; hfb[y][x][2] = b; hzb[y][x] = 1; } }
static void h_save(const char *path) {
	FILE *f = fopen(path, "wb"); fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) { float v = hfb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); }
	fclose(f);
}
// offscreen targets (skin_gpu.h's self-test): one shared target = the framebuffer above, read back bottom-up
enum { GL_FRAMEBUFFER = 900, GL_COLOR_ATTACHMENT0, GL_UNSIGNED_BYTE_ = 0 };
static void glGenFramebuffers(int n, GLuint *f) { for (int i = 0; i < n; i++) f[i] = 1; }
static void glDeleteFramebuffers(int n, const GLuint *f) { (void)n; (void)f; }
static void glBindFramebuffer(GLenum t, GLuint f) { (void)t; hfbo = f; if (!f) { hvw = SW_W; hvh = SW_H; } }
static void glFramebufferTexture2D(GLenum a, GLenum b, GLenum c, GLuint t, int l) { (void)a; (void)b; (void)c; (void)t; (void)l; }
static void glReadPixels(int x, int y, int w, int h, GLenum f, GLenum t, void *out) {
	(void)f; (void)t; unsigned char *o = out;
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) { int yy = hvh - 1 - (y + j), xx = x + i; unsigned char *q = o + (j * w + i) * 4;
		if (yy < 0 || xx >= SW_W || yy >= SW_H) { memset(q, 0, 4); continue; }
		for (int k = 0; k < 3; k++) { float v = hfb[yy][xx][k]; q[k] = (unsigned char)((v < 0 ? 0 : v > 1 ? 1 : v) * 255); } q[3] = hcov[yy][xx] ? 255 : 0; }
}
