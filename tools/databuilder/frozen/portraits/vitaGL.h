// Host stand-in for the vitaGL subset used by thinktank.h / ttm.h and the headers they include (model.h, sprite.h, fx.h,
// hud.h, skills.h ...), for tools/tank/host/tk_test.c. Three jobs:
//   - a software rasterizer (960 x 544) that runs the model shader (palette skinning, lambert, tint, fog) and the sprite
//     shader (pos, uv, colour) so the Think Tanks, terminals, shadows and lasers can be looked at (tk_save writes a PPM);
//   - HUD capture: while cap_begin() is open, glDrawArrays of the HUD shader (5 attributes, 17 floats per vertex) is
//     recorded in the format of tools/menu/raster.py (cap_dump_textures writes the HUD textures next to it);
//   - counters (draw calls, triangles, pixels) for the cost estimates.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef int GLsizei; typedef float GLfloat;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_REPEAT 0x2901
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_BYTE 0x1400
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_WRITE_ONLY 0x88B9
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_CG_VERTEX_SHADER_EXT 0x8B31
#define GL_CG_FRAGMENT_SHADER_EXT 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_DST_COLOR 0x0306
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_ALWAYS 0x0207
#define GL_STENCIL_BUFFER_BIT 0x400
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_COLOR_BUFFER_BIT 0x4000
#define SW_W 960
#define SW_H 544
typedef struct { int w, h, fmt, size; unsigned char *raw, *rgba; } GTex;     // raw: as uploaded (HUD dump); rgba: decoded
typedef struct { unsigned char *data; int size; } GBuf;
typedef struct { int buf, size, type, norm, stride; const void *ptr; } GAttr;
static GTex g_tex[512]; static int g_ntex = 1, g_bound;
static GBuf g_buf[8192]; static int g_nbuf = 1, g_arr, g_el;
static GAttr g_attr[6];
static float g_uni[64][16][48 * 3][4];   // [program][uniform slot][vec4 index] (palette = 144 vec4)
static int g_prog, g_nprog = 1, g_blend, g_depth = 1, g_dmask = 1, g_cull = 1; static GLenum g_bs = GL_ONE, g_bd = GL_ZERO, g_dfunc = GL_LEQUAL;
static float g_fb[SW_H][SW_W][3], g_zb[SW_H][SW_W];
static long g_draws, g_tris, g_pixels; static int g_raster = 1;   // g_raster 0: count only (timing runs)
static FILE *cap_out;
// ---- textures
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = g_ntex < 511 ? g_ntex++ : 511; }
static void glDeleteTextures(int n, const GLuint *t) { for (int i = 0; i < n; i++) { GTex *T = &g_tex[t[i]]; free(T->raw); free(T->rgba); memset(T, 0, sizeof(*T)); } }
static void glBindTexture(GLenum a, GLuint b) { (void)a; g_bound = (int)b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glActiveTexture(GLenum t) { (void)t; }
static void glGenerateMipmap(GLenum t) { (void)t; }
static void g_dxt_block(const unsigned char *b, int fmt5, unsigned char out[16][4]) {
	unsigned char al[16]; for (int i = 0; i < 16; i++) al[i] = 255;
	const unsigned char *cb = b;
	if (fmt5) {
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
	if (c0 > c1 || fmt5) { for (int k = 0; k < 3; k++) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; } c[2][3] = c[3][3] = 255; }
	else { for (int k = 0; k < 3; k++) { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; } c[2][3] = 255; c[3][3] = 0; }
	unsigned idx = cb[4] | cb[5] << 8 | cb[6] << 16 | (unsigned)cb[7] << 24;
	for (int i = 0; i < 16; i++) { int ci = (idx >> (2 * i)) & 3; memcpy(out[i], c[ci], 4); if (fmt5) out[i][3] = al[i]; }
}
static void glCompressedTexImage2D(GLenum t, GLint lv, GLenum fmt, GLsizei w, GLsizei h, GLint bd, GLsizei sz, const void *data) {
	(void)t; (void)bd; if (lv) return;
	GTex *T = &g_tex[g_bound]; free(T->raw); free(T->rgba); T->w = w; T->h = h; T->fmt = (int)fmt; T->size = sz; T->raw = malloc(sz); memcpy(T->raw, data, sz);
	T->rgba = malloc((size_t)w * h * 4); const unsigned char *b = data; int f5 = fmt == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	for (int by = 0; by < h / 4; by++) for (int bx = 0; bx < w / 4; bx++, b += f5 ? 16 : 8) { unsigned char o[16][4]; g_dxt_block(b, f5, o);
		for (int i = 0; i < 16; i++) memcpy(T->rgba + (((size_t)(by * 4 + i / 4) * w) + bx * 4 + i % 4) * 4, o[i], 4); }
}
static void glTexImage2D(GLenum t, GLint lv, GLint ifmt, GLsizei w, GLsizei h, GLint bd, GLenum fmt, GLenum ty, const void *px) {
	(void)t; (void)ifmt; (void)bd; (void)ty; if (lv) return;
	GTex *T = &g_tex[g_bound]; free(T->raw); free(T->rgba); int bpp = fmt == GL_RGB ? 3 : 4;
	T->w = w; T->h = h; T->fmt = bpp; T->size = w * h * bpp; T->raw = malloc(T->size); if (px) memcpy(T->raw, px, T->size); else memset(T->raw, 0, T->size);
	T->rgba = malloc((size_t)w * h * 4); for (int i = 0; i < w * h; i++) { for (int k = 0; k < 3; k++) T->rgba[i * 4 + k] = T->raw[i * bpp + k]; T->rgba[i * 4 + 3] = bpp == 4 ? T->raw[i * 4 + 3] : 255; }
}
static inline int sceGxmTransferFinish(void) { return 0; }
// ---- buffers
static void glGenBuffers(int n, GLuint *b) { for (int i = 0; i < n; i++) { b[i] = g_nbuf < 8191 ? g_nbuf++ : 8191; g_buf[b[i]].data = NULL; g_buf[b[i]].size = 0; } }
static void glDeleteBuffers(int n, const GLuint *b) { for (int i = 0; i < n; i++) { free(g_buf[b[i]].data); g_buf[b[i]].data = NULL; } }
static void glBindBuffer(GLenum t, GLuint id) { if (t == GL_ARRAY_BUFFER) g_arr = (int)id; else g_el = (int)id; }
static void glBufferData(GLenum t, long size, const void *d, GLenum u) { (void)u; GBuf *B = &g_buf[t == GL_ARRAY_BUFFER ? g_arr : g_el]; free(B->data); B->data = malloc(size); B->size = (int)size; if (d) memcpy(B->data, d, size); else memset(B->data, 0, size); }
static void *glMapBuffer(GLenum t, GLenum a) { (void)a; return g_buf[t == GL_ARRAY_BUFFER ? g_arr : g_el].data; }
static int glUnmapBuffer(GLenum t) { (void)t; return 1; }
static void glVertexAttribPointer(GLuint i, int size, GLenum type, GLboolean n, int stride, const void *p) { if (i < 6) { GAttr *A = &g_attr[i]; A->buf = g_arr; A->size = size; A->type = type; A->norm = n; A->stride = stride; A->ptr = p; } }
static void glEnableVertexAttribArray(GLuint i) { (void)i; }
static void glDisableVertexAttribArray(GLuint i) { (void)i; }
// ---- shaders / programs / uniforms: uniforms live per program; location = slot (names hashed into 16 slots)
static GLuint glCreateShader(GLenum t) { (void)t; return 1; }
static void glDeleteShader(GLuint s) { (void)s; }
static void glShaderSource(GLuint s, int n, const char **src, const GLint *len) { (void)s; (void)n; (void)src; (void)len; }
static void glCompileShader(GLuint s) { (void)s; }
#define glGetShaderiv(a, b, c) (*(c) = 1)
#define glGetProgramiv(a, b, c) (*(c) = 1)
static void glGetShaderInfoLog(GLuint s, int n, int *l, char *b) { (void)s; (void)n; if (l) *l = 0; if (b) b[0] = 0; }
static void glGetProgramInfoLog(GLuint s, int n, int *l, char *b) { (void)s; (void)n; if (l) *l = 0; if (b) b[0] = 0; }
static GLuint glCreateProgram(void) { return g_nprog < 63 ? (GLuint)g_nprog++ : 63; }
static void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
static void glBindAttribLocation(GLuint p, GLuint i, const char *n) { (void)p; (void)i; (void)n; }
static void glLinkProgram(GLuint p) { (void)p; }
static void glUseProgram(GLuint p) { g_prog = (int)p; }
enum { GU_M0 = 1, GU_M1, GU_M2, GU_M3, GU_PAL, GU_LDIR, GU_TINT, GU_FOG, GU_FOGP, GU_OTHER };
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p;
	if (!strcmp(n, "m0")) return GU_M0; if (!strcmp(n, "m1")) return GU_M1; if (!strcmp(n, "m2")) return GU_M2; if (!strcmp(n, "m3")) return GU_M3;
	if (!strcmp(n, "pal")) return GU_PAL; if (!strcmp(n, "ldir")) return GU_LDIR; if (!strcmp(n, "tint")) return GU_TINT; if (!strcmp(n, "fogc")) return GU_FOG; if (!strcmp(n, "fogp")) return GU_FOGP;
	return GU_OTHER; }
static void glUniform1i(GLint l, int v) { (void)l; (void)v; }
static void glUniform1f(GLint l, float v) { (void)l; (void)v; }
static void glUniform4f(GLint l, float a, float b, float c, float d) { if (l > 0 && l < 16) { float *u = g_uni[g_prog][l][0]; u[0] = a; u[1] = b; u[2] = c; u[3] = d; } }
static void glUniform4fv(GLint l, int n, const float *v) { if (l > 0 && l < 16) { if (n > 144) n = 144; memcpy(g_uni[g_prog][l], v, 16 * n); } }
static void glUniformMatrix4fv(GLint l, int n, GLboolean t, const float *v) { (void)l; (void)n; (void)t; (void)v; }
// ---- state
static void glEnable(GLenum c) { if (c == GL_BLEND) g_blend = 1; if (c == GL_DEPTH_TEST) g_depth = 1; if (c == GL_CULL_FACE) g_cull = 1; }
static void glDisable(GLenum c) { if (c == GL_BLEND) g_blend = 0; if (c == GL_DEPTH_TEST) g_depth = 0; if (c == GL_CULL_FACE) g_cull = 0; }
static void glBlendFunc(GLenum s, GLenum d) { g_bs = s; g_bd = d; }
static void glDepthMask(GLboolean m) { g_dmask = m; }
static void glDepthFunc(GLenum f) { g_dfunc = f; }
static void glStencilOp(GLenum a, GLenum b, GLenum c) { (void)a; (void)b; (void)c; }
static void glStencilFunc(GLenum a, int b, unsigned c) { (void)a; (void)b; (void)c; }
static void glStencilMask(unsigned m) { (void)m; }
static void glColorMask(GLboolean a, GLboolean b, GLboolean c, GLboolean d) { (void)a; (void)b; (void)c; (void)d; }
static void glClear(unsigned m) { (void)m; }
static void glClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
static void glViewport(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static void glScissor(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; }
static void glPolygonOffset(float a, float b) { (void)a; (void)b; }
// ---- rasterizer
static void g_read(int i, int v, float *o) {   // attribute i of vertex v (buffer offset or client pointer)
	GAttr *A = &g_attr[i]; const unsigned char *base = A->buf ? g_buf[A->buf].data + (intptr_t)A->ptr : (const unsigned char *)A->ptr;
	int st = A->stride ? A->stride : A->size * (A->type == GL_FLOAT ? 4 : 1); const unsigned char *p = base + (size_t)v * st;
	for (int k = 0; k < A->size; k++) {
		if (A->type == GL_FLOAT) { float f; memcpy(&f, p + 4 * k, 4); o[k] = f; }
		else if (A->type == GL_BYTE) o[k] = A->norm ? ((signed char)p[k]) / 127.f : (signed char)p[k];
		else o[k] = A->norm ? p[k] / 255.f : p[k];
	}
}
typedef struct { float pos[4], uv[2], col[4], fog; } GV;   // col: shading x tint (model) or vertex colour (sprite)
static float g_fac(GLenum f, const float *s, const float *d, int k) {
	switch (f) { case GL_ZERO: return 0; case GL_ONE: return 1; case GL_SRC_ALPHA: return s[3]; case GL_ONE_MINUS_SRC_ALPHA: return 1 - s[3];
	case GL_DST_COLOR: return d[k]; case GL_SRC_COLOR: return s[k]; case GL_ONE_MINUS_SRC_COLOR: return 1 - s[k]; default: return 1; }
}
static void g_tri(const GV *a, const GV *b, const GV *c, const float *fogc, int alpha_test) {
	g_tris++; if (!g_raster) return;
	const GV *v[3] = { a, b, c }; float sx[3], sy[3], sz[3], iw[3];
	for (int i = 0; i < 3; i++) { if (v[i]->pos[3] <= 1) return; iw[i] = 1 / v[i]->pos[3]; sx[i] = (v[i]->pos[0] * iw[i] + 1) * 0.5f * SW_W; sy[i] = (1 - v[i]->pos[1] * iw[i]) * 0.5f * SW_H; sz[i] = v[i]->pos[2] * iw[i]; }
	float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]); if (fabsf(area) < 1e-6f) return;
	int x0 = (int)fmaxf(0, floorf(fminf(sx[0], fminf(sx[1], sx[2])))), x1 = (int)fminf(SW_W - 1, ceilf(fmaxf(sx[0], fmaxf(sx[1], sx[2]))));
	int y0 = (int)fmaxf(0, floorf(fminf(sy[0], fminf(sy[1], sy[2])))), y1 = (int)fminf(SW_H - 1, ceilf(fmaxf(sy[0], fmaxf(sy[1], sy[2]))));
	const GTex *T = &g_tex[g_bound];
	for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
		float px = x + 0.5f, py = y + 0.5f;
		float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) / area, w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) / area, w2 = 1 - w0 - w1;
		if (w0 < 0 || w1 < 0 || w2 < 0) continue;
		float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2]; if (z < -1 || z > 1) continue;
		if (g_depth && (g_dfunc == GL_GREATER ? z <= g_zb[y][x] : z > g_zb[y][x])) continue;
		float pw = w0 * iw[0] + w1 * iw[1] + w2 * iw[2], at[7];
		for (int k = 0; k < 7; k++) { float q0 = k < 2 ? v[0]->uv[k] : k < 6 ? v[0]->col[k - 2] : v[0]->fog, q1 = k < 2 ? v[1]->uv[k] : k < 6 ? v[1]->col[k - 2] : v[1]->fog, q2 = k < 2 ? v[2]->uv[k] : k < 6 ? v[2]->col[k - 2] : v[2]->fog;
			at[k] = (w0 * q0 * iw[0] + w1 * q1 * iw[1] + w2 * q2 * iw[2]) / pw; }
		float t4[4] = { 1, 1, 1, 1 };
		if (T->rgba) { float u = at[0] * T->w - 0.5f, vv = at[1] * T->h - 0.5f; int tx = (((int)floorf(u + 0.5f)) % T->w + T->w) % T->w, ty = (((int)floorf(vv + 0.5f)) % T->h + T->h) % T->h;
			const unsigned char *t = T->rgba + ((size_t)ty * T->w + tx) * 4; for (int k = 0; k < 4; k++) t4[k] = t[k] / 255.f; }
		if (alpha_test && t4[3] < 0.5f) continue;
		float s[4]; for (int k = 0; k < 4; k++) s[k] = t4[k] * at[2 + k];
		if (fogc && fogc[3] > 0) for (int k = 0; k < 3; k++) s[k] += (fogc[k] - s[k]) * at[6] * fogc[3];
		float *d = g_fb[y][x], dd[4] = { d[0], d[1], d[2], 1 };
		if (g_blend) for (int k = 0; k < 3; k++) d[k] = s[k] * g_fac(g_bs, s, dd, k) + dd[k] * g_fac(g_bd, s, dd, k); else for (int k = 0; k < 3; k++) d[k] = s[k];
		for (int k = 0; k < 3; k++) { if (d[k] > 1) d[k] = 1; if (d[k] < 0) d[k] = 0; }
		if (g_dmask) g_zb[y][x] = z;
		g_pixels++;
	}
}
// the model shader (model.h MD_VS / MD_FS): palette skinning, lambert 0.40 + 0.75 n.l, x tint, exp fog
static void g_model_vs(int v, GV *o) {
	float p[4] = { 0, 0, 0, 1 }, n[4] = { 0 }, uv[2] = { 0 }, bi[4] = { 0 }, bw[4] = { 0 };
	g_read(0, v, p); g_read(1, v, n); g_read(2, v, uv); g_read(3, v, bi); g_read(4, v, bw); p[3] = 1;
	float (*U)[48 * 3][4] = g_uni[g_prog]; float s[3] = { 0 }, sn[3] = { 0 };
	for (int j = 0; j < 4; j++) { int i = (int)(bi[j] * 3 + 0.5f); if (bw[j] <= 0 || i > 141) continue;
		for (int r = 0; r < 3; r++) { const float *R = U[GU_PAL][i + r]; s[r] += bw[j] * (R[0] * p[0] + R[1] * p[1] + R[2] * p[2] + R[3]); if (j < 2) sn[r] += bw[j] * (R[0] * n[0] + R[1] * n[1] + R[2] * n[2]); } }
	float c[4] = { s[0], s[1], s[2], 1 };
	for (int r = 0; r < 4; r++) { const float *M = U[GU_M0 + r][0]; o->pos[r] = M[0] * c[0] + M[1] * c[1] + M[2] * c[2] + M[3] * c[3]; }
	o->uv[0] = uv[0]; o->uv[1] = uv[1];
	const float *L = U[GU_LDIR][0], *T = U[GU_TINT][0], *FP = U[GU_FOGP][0];
	float l = sqrtf(sn[0] * sn[0] + sn[1] * sn[1] + sn[2] * sn[2]) + 1e-4f, d = (sn[0] * L[0] + sn[1] * L[1] + sn[2] * L[2]) / l, sh = 0.40f + 0.75f * (d < 0 ? 0 : d > 1 ? 1 : d);
	for (int k = 0; k < 3; k++) o->col[k] = sh * T[k]; o->col[3] = T[3];
	float fz = o->pos[3] - FP[0]; o->fog = FP[2] * (1 - expf(-(fz > 0 ? fz : 0) * FP[1]));
}
static void glDrawElements(GLenum m, int n, GLenum t, const void *off) {
	(void)m; (void)t; g_draws++;
	const unsigned short *ix = (const unsigned short *)(g_el ? g_buf[g_el].data + (intptr_t)off : (const unsigned char *)off);
	const float *fogc = g_uni[g_prog][GU_FOG][0];
	for (int i = 0; i + 2 < n; i += 3) { GV a, b, c; g_model_vs(ix[i], &a); g_model_vs(ix[i + 1], &b); g_model_vs(ix[i + 2], &c); g_tri(&a, &b, &c, fogc, 0); }
}
// glDrawArrays: the HUD (5 attributes; recorded while a capture is open) or sprites (pos3 uv2 col4, drawn)
static void glDrawArrays(GLenum mode, int first, int count) {
	(void)mode; g_draws++;
	if (g_attr[4].size == 1 && g_attr[3].size == 4 && g_attr[0].size == 4) {   // the HUD shader's layout
		if (!cap_out) return;
		int hdr[3] = { g_bound, g_bs == GL_ONE ? 1 : 0, count }; fwrite(hdr, 4, 3, cap_out);
		for (int v = first; v < first + count; v++) for (int a = 0; a < 5; a++) { float f[4] = { 0, 0, 0, 1 }; g_read(a, v, f); fwrite(f, 4, a == 4 ? 1 : 4, cap_out); }
		return;
	}
	float (*U)[48 * 3][4] = g_uni[g_prog];
	for (int t = first; t + 2 < first + count; t += 3) { GV q[3];
		for (int i = 0; i < 3; i++) { float p[4] = { 0, 0, 0, 1 }, uv[2] = { 0 }, col[4] = { 1, 1, 1, 1 }; g_read(0, t + i, p); g_read(1, t + i, uv); g_read(2, t + i, col); p[3] = 1;
			for (int r = 0; r < 4; r++) { const float *M = U[GU_M0 + r][0]; q[i].pos[r] = M[0] * p[0] + M[1] * p[1] + M[2] * p[2] + M[3]; }
			q[i].uv[0] = uv[0]; q[i].uv[1] = uv[1]; memcpy(q[i].col, col, 16); q[i].fog = 0; }
		g_tri(&q[0], &q[1], &q[2], NULL, 0); }
}
// ---- host helpers
static void g_clear(float r, float g, float b) { for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { g_fb[y][x][0] = r; g_fb[y][x][1] = g; g_fb[y][x][2] = b; g_zb[y][x] = 1; } }
static void g_save(const char *path) {
	FILE *f = fopen(path, "wb"); if (!f) return; fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) { float v = g_fb[y][x][k]; fputc((int)((v < 0 ? 0 : v > 1 ? 1 : v) * 255 + 0.5f), f); }
	fclose(f);
}
static void cap_begin(const char *path) { cap_out = fopen(path, "wb"); }
static void cap_end(void) { if (cap_out) fclose(cap_out); cap_out = NULL; }
static void cap_dump_textures(const char *dir) {   // HUD textures (uncompressed) in tools/menu/raster.py's format
	char p[512]; for (int i = 1; i < g_ntex; i++) { GTex *T = &g_tex[i]; int dxt1 = T->fmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || T->fmt == GL_COMPRESSED_RGB_S3TC_DXT1_EXT; if (!T->raw || (T->fmt != 3 && T->fmt != 4 && !dxt1) || (dxt1 && T->size > 65536)) continue;   // DXT1: small ones (HUD pictures), not the model textures
		snprintf(p, sizeof(p), "%s/tex%d.bin", dir, i); FILE *f = fopen(p, "wb"); if (!f) continue; int h[4] = { T->w, T->h, T->fmt, T->size }; fwrite(h, 4, 4, f); fwrite(T->raw, 1, T->size, f); fclose(f); }
}
