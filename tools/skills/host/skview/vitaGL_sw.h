// Host stub of the vitaGL subset used by sprite.h / fx.h, backed by a tiny software rasterizer (tests only).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef int GLsizei; typedef float GLfloat;
enum { GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_CLAMP_TO_EDGE,
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_RGBA, GL_UNSIGNED_BYTE, GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER,
	GL_VERTEX_SHADER, GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TEXTURE0, GL_TEXTURE1, GL_BLEND, GL_DEPTH_TEST, GL_CULL_FACE, GL_FLOAT, GL_TRIANGLES };
enum { GL_FALSE = 0, GL_TRUE = 1 };
enum { GL_ZERO = 100, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_ALPHA };
typedef struct { int w, h; unsigned char *px; } SwTex;
static SwTex sw_tex[64]; static int sw_ntex = 1, sw_bound, sw_blend, sw_depth = 1, sw_dmask = 1; static GLenum sw_bs = GL_ONE, sw_bd = GL_ZERO;
static float sw_m[4][4]; static const float *sw_attr[3]; static int sw_astride[3];
#define SW_W 960
#define SW_H 544
static float sw_fb[SW_H][SW_W][3], sw_zb[SW_H][SW_W]; static long sw_pixels;
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = sw_ntex++; }
static void glBindTexture(GLenum t, GLuint id) { (void)t; sw_bound = id; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glActiveTexture(GLenum t) { (void)t; }
static void sw_dxt5_block(const unsigned char *b, unsigned char out[16][4]) {
	unsigned a0 = b[0], a1 = b[1]; unsigned char al[8]; al[0] = a0; al[1] = a1;
	if (a0 > a1) for (int i = 1; i < 7; i++) al[i + 1] = (unsigned char)(((7 - i) * a0 + i * a1) / 7);
	else { for (int i = 1; i < 5; i++) al[i + 1] = (unsigned char)(((5 - i) * a0 + i * a1) / 5); al[6] = 0; al[7] = 255; }
	uint64_t ab = 0; for (int i = 0; i < 6; i++) ab |= (uint64_t)b[2 + i] << (8 * i);
	unsigned c0 = b[8] | b[9] << 8, c1 = b[10] | b[11] << 8; unsigned char c[4][3];
	c[0][0] = (c0 >> 11) * 255 / 31; c[0][1] = ((c0 >> 5) & 63) * 255 / 63; c[0][2] = (c0 & 31) * 255 / 31;
	c[1][0] = (c1 >> 11) * 255 / 31; c[1][1] = ((c1 >> 5) & 63) * 255 / 63; c[1][2] = (c1 & 31) * 255 / 31;
	for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; }
	unsigned idx = b[12] | b[13] << 8 | b[14] << 16 | (unsigned)b[15] << 24;
	for (int i = 0; i < 16; i++) { int ci = (idx >> (2 * i)) & 3; out[i][0] = c[ci][0]; out[i][1] = c[ci][1]; out[i][2] = c[ci][2]; out[i][3] = al[(ab >> (3 * i)) & 7]; }
}
static void glCompressedTexImage2D(GLenum t, GLint lv, GLenum fmt, GLsizei w, GLsizei h, GLint bd, GLsizei sz, const void *data) {
	(void)t; (void)fmt; (void)bd; (void)sz; if (lv) return;
	SwTex *T = &sw_tex[sw_bound]; T->w = w; T->h = h; T->px = malloc(w * h * 4); const unsigned char *b = data;
	for (int by = 0; by < h / 4; by++) for (int bx = 0; bx < w / 4; bx++, b += 16) { unsigned char o[16][4]; sw_dxt5_block(b, o);
		for (int i = 0; i < 16; i++) memcpy(T->px + (((by * 4 + i / 4) * w) + bx * 4 + i % 4) * 4, o[i], 4); }
}
static void glTexImage2D(GLenum t, GLint lv, GLenum f, GLsizei w, GLsizei h, GLint b, GLenum f2, GLenum ty, const void *px) {
	(void)t; (void)lv; (void)f; (void)b; (void)f2; (void)ty; SwTex *T = &sw_tex[sw_bound]; T->w = w; T->h = h; T->px = malloc(w * h * 4); memcpy(T->px, px, w * h * 4); }
static inline int sceGxmTransferFinish(void) { return 0; }
static GLuint glCreateShader(GLenum t) { return t; }
#define glGetShaderiv(a, b, c) (*(c) = 1)
#define glGetProgramiv(a, b, c) (*(c) = 1)
static GLuint glCreateProgram(void) { return 1; }
#define glAttachShader(a, b)
#define glBindAttribLocation(a, b, c)
#define glLinkProgram(a)
#define glUseProgram(a)
#define glUniform1i(a, b)
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p; return n[0] == 'm' ? 10 + (n[1] - '0') : 20; }
static void glGetActiveUniform(GLuint p, GLuint i, GLsizei n, GLsizei *l, GLint *s, GLenum *t, char *nm) { (void)p; (void)i; (void)n; if (l) *l = 0; if (s) *s = 0; if (t) *t = 0; if (nm) nm[0] = 0; }
static void glUniform4fv(GLint loc, int n, const float *v) { (void)n; if (loc >= 10 && loc < 14) memcpy(sw_m[loc - 10], v, 16); }
#define glBindBuffer(a, b)
#define glEnableVertexAttribArray(a)
#define glDisableVertexAttribArray(a)
static void glVertexAttribPointer(GLuint i, int n, GLenum t, GLboolean nm, int stride, const void *p) { (void)n; (void)t; (void)nm; if (i < 3) { sw_attr[i] = p; sw_astride[i] = stride / 4; } }
static void glEnable(GLenum c) { if (c == GL_BLEND) sw_blend = 1; if (c == GL_DEPTH_TEST) sw_depth = 1; }
static void glDisable(GLenum c) { if (c == GL_BLEND) sw_blend = 0; if (c == GL_DEPTH_TEST) sw_depth = 0; }
static void glDepthMask(GLboolean m) { sw_dmask = m; }
static void glBlendFunc(GLenum s, GLenum d) { sw_bs = s; sw_bd = d; }
static void glBlendFuncSeparate(GLenum s, GLenum d, GLenum sa, GLenum da) { (void)sa; (void)da; sw_bs = s; sw_bd = d; }   // (b116)
static void sw_sample(const SwTex *T, float u, float v, float *o) {
	if (!T->px) { o[0] = o[1] = o[2] = o[3] = 1; return; }
	float x = u * T->w - 0.5f, y = v * T->h - 0.5f; int x0 = (int)floorf(x), y0 = (int)floorf(y); float fx = x - x0, fy = y - y0;
	for (int k = 0; k < 4; k++) o[k] = 0;
	for (int j = 0; j < 4; j++) { int xx = x0 + (j & 1), yy = y0 + (j >> 1); if (xx < 0) xx = 0; if (yy < 0) yy = 0; if (xx >= T->w) xx = T->w - 1; if (yy >= T->h) yy = T->h - 1;
		float w = ((j & 1) ? fx : 1 - fx) * ((j >> 1) ? fy : 1 - fy); const unsigned char *p = T->px + (yy * T->w + xx) * 4; for (int k = 0; k < 4; k++) o[k] += w * p[k] / 255.f; }
}
static float sw_f(GLenum f, const float *s, const float *d, int k) {
	switch (f) { case GL_ZERO: return 0; case GL_ONE: return 1; case GL_SRC_ALPHA: return s[3]; case GL_ONE_MINUS_SRC_ALPHA: return 1 - s[3];
	case GL_DST_COLOR: return d[k]; case GL_SRC_COLOR: return s[k]; case GL_ONE_MINUS_SRC_COLOR: return 1 - s[k]; default: return 1; }
}
// one triangle: clip-space positions c[3][4], attributes a[3][6] (u, v, r, g, b, a); tex = NULL: untextured
static void sw_tri(float c[3][4], float a[3][6], const SwTex *T) {
	for (int i = 0; i < 3; i++) if (c[i][3] < 0.5f) return;
	float sx[3], sy[3], sz[3], iw[3];
	for (int i = 0; i < 3; i++) { iw[i] = 1 / c[i][3]; sx[i] = (c[i][0] * iw[i] + 1) * 0.5f * SW_W; sy[i] = (1 - c[i][1] * iw[i]) * 0.5f * SW_H; sz[i] = c[i][2] * iw[i]; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]); if (fabsf(area) < 1e-6f) return;
	int x0 = (int)fmaxf(0, floorf(fminf(sx[0], fminf(sx[1], sx[2])))), x1 = (int)fminf(SW_W - 1, ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))));
	int y0 = (int)fmaxf(0, floorf(fminf(sy[0], fminf(sy[1], sy[2])))), y1 = (int)fminf(SW_H - 1, ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))));
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
		float px = x + 0.5f, py = y + 0.5f;
		float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area, w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area, w2 = 1 - w0 - w1;
		if (w0 < 0 || w1 < 0 || w2 < 0) continue;
		float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
		if (sw_depth && z > sw_zb[y][x]) continue;
		float q = w0 * iw[0] + w1 * iw[1] + w2 * iw[2], at[6];
		for (int k = 0; k < 6; k++) at[k] = (w0 * a[0][k] * iw[0] + w1 * a[1][k] * iw[1] + w2 * a[2][k] * iw[2]) / q;
		float s[4]; if (T) sw_sample(T, at[0], at[1], s); else s[0] = s[1] = s[2] = s[3] = 1;
		for (int k = 0; k < 4; k++) s[k] *= at[2 + k];
		float *d = sw_fb[y][x], dd[4] = { d[0], d[1], d[2], 1 };
		if (sw_blend) for (int k = 0; k < 3; k++) d[k] = s[k] * sw_f(sw_bs, s, dd, k) + dd[k] * sw_f(sw_bd, s, dd, k); else for (int k = 0; k < 3; k++) d[k] = s[k];
		for (int k = 0; k < 3; k++) { if (d[k] > 1) d[k] = 1; if (d[k] < 0) d[k] = 0; }   // 8-bit framebuffer
		if (sw_dmask) sw_zb[y][x] = z;
		sw_pixels++;
	}
}
static void glDrawArrays(GLenum mode, int first, int n) {
	(void)mode;
	for (int t = first; t + 2 < first + n; t += 3) { float c[3][4], a[3][6];
		for (int i = 0; i < 3; i++) { const float *p = sw_attr[0] + (t + i) * sw_astride[0], *uv = sw_attr[1] + (t + i) * sw_astride[1], *col = sw_attr[2] + (t + i) * sw_astride[2];
			for (int r = 0; r < 4; r++) c[i][r] = sw_m[r][0] * p[0] + sw_m[r][1] * p[1] + sw_m[r][2] * p[2] + sw_m[r][3];
			a[i][0] = uv[0]; a[i][1] = uv[1]; for (int k = 0; k < 4; k++) a[i][2 + k] = col[k]; }
		sw_tri(c, a, &sw_tex[sw_bound]); }
}
