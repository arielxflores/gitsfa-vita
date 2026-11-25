// Software vitaGL stand-in for the viewmodel draw test (tools/weapons/vmdraw_host.c): buffers are kept, glUniform4fv
// fills the shader's uniforms (m0..m3, pal[192], tint) and glDrawElements runs model.h's MD_VS maths on the CPU (palette
// skinning: pal[bi*4 ..] since chars b41, 4 weights) and rasterises the triangles with a depth buffer into sgl_img (960x544), coloured
// per mesh class (sgl_col, set by the test) and shaded by the normal. Everything else is a no-op.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef float GLfloat; typedef int GLsizei;
enum { GL_ACTIVE_UNIFORMS = 0x8B86, GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_FALSE = 0, GL_TRUE = 1, GL_TRIANGLES = 4,
	GL_ARRAY_BUFFER = 100, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_DYNAMIC_DRAW, GL_WRITE_ONLY, GL_VERTEX_SHADER = 200, GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS,
	GL_TEXTURE0 = 300, GL_BLEND = 400, GL_DEPTH_TEST, GL_CULL_FACE, GL_ONE = 500, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FLOAT = 600, GL_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT,
	GL_TEXTURE1 = 301, GL_TEXTURE2 = 302, GL_TEXTURE3 = 303, GL_TEXTURE4 = 304, GL_RGB = 700, GL_RGBA, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE, GL_NEAREST,
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3 };
static int hgl_pal_uploads;
#define sceGxmTransferFinish() 0
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = i + 1; }
static void glDeleteTextures(int n, const GLuint *t) { (void)n; (void)t; }
static void glBindTexture(GLenum a, GLuint b) { (void)a; (void)b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glCompressedTexImage2D(GLenum t, int l, GLenum f, int w, int h, int b, int s, const void *p) { (void)t; (void)l; (void)f; (void)w; (void)h; (void)b; (void)s; (void)p; }
static void glTexImage2D(GLenum t, int l, int i, int w, int h, int b, GLenum f, GLenum y, const void *p) { (void)t; (void)l; (void)i; (void)w; (void)h; (void)b; (void)f; (void)y; (void)p; }
static void glDeleteBuffers(int n, const GLuint *b) { (void)n; (void)b; }
static void *glMapBuffer(GLenum a, GLenum b) { (void)a; (void)b; return NULL; }
static void glUnmapBuffer(GLenum a) { (void)a; }
static GLuint glCreateShader(GLenum t) { (void)t; return 1; }
static void glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void)s; (void)p; *v = 1; }
static GLuint glCreateProgram(void) { return 1; }
static void glAttachShader(GLuint a, GLuint b) { (void)a; (void)b; }
static void glBindAttribLocation(GLuint a, GLuint b, const char *c) { (void)a; (void)b; (void)c; }
static void glLinkProgram(GLuint a) { (void)a; }
static void glGetProgramiv(GLuint a, GLenum b, GLint *c) { (void)a; (void)b; *c = 1; }
static void glGetActiveUniform(GLuint p, GLuint i, GLsizei b, GLsizei *l, GLint *s, GLenum *t, char *n) { (void)p; (void)i; (void)b; *l = 0; *s = 0; *t = 0; n[0] = 0; }
static void glUseProgram(GLuint a) { (void)a; }
static void glUniform1i(GLint a, int b) { (void)a; (void)b; }
static void glEnableVertexAttribArray(GLuint a) { (void)a; }
static void glDisableVertexAttribArray(GLuint a) { (void)a; }
static void glVertexAttribPointer(GLuint a, int b, GLenum c, GLboolean d, int e, const void *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static void glActiveTexture(GLenum a) { (void)a; }
static void glEnable(GLenum a) { (void)a; }
static void glDisable(GLenum a) { (void)a; }
static void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
static void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { (void)a; (void)b; (void)c; (void)d; }   // (b116)
static void glDepthMask(GLboolean a) { (void)a; }

#include <math.h>
#define SGL_W 960
#define SGL_H 544
static unsigned char sgl_img[SGL_H][SGL_W][3]; static float sgl_z[SGL_H][SGL_W]; static float sgl_col[3] = { 1, 1, 1 };
static int sgl_vbo_class[4096]; static float sgl_class_col[4][3] = { { 0.75f, 0.75f, 0.8f }, { 1.0f, 0.62f, 0.35f }, { 0.45f, 0.62f, 1.0f }, { 1, 1, 1 } };   // colour by vertex buffer: 1 arms, 2 gun
static int sgl_tris, sgl_bad;   // triangles drawn, vertices that came out non-finite
typedef struct { void *d; long n; } SglBuf; static SglBuf sgl_buf[4096]; static GLuint sgl_nbuf = 1, sgl_arr, sgl_elem;
static void glGenBuffers(int n, GLuint *b) { for (int i = 0; i < n; i++) b[i] = sgl_nbuf++; }
static void glBindBuffer(GLenum a, GLuint b) { if (a == GL_ARRAY_BUFFER) sgl_arr = b; else sgl_elem = b; }
static void glBufferData(GLenum a, long s, const void *d, GLenum u) { (void)u; GLuint b = a == GL_ARRAY_BUFFER ? sgl_arr : sgl_elem; free(sgl_buf[b].d); sgl_buf[b].d = malloc(s); if (d) memcpy(sgl_buf[b].d, d, s); sgl_buf[b].n = s; }
// uniform locations: 1..4 m0..m3, 5 pal, 6 ldir, 7 tint, 8 fogc, 9 fogp
static float sgl_m[4][4], sgl_pal[192][4], sgl_tint[4] = { 1, 1, 1, 1 };
static GLint glGetUniformLocation(GLuint a, const char *n) { (void)a; if (n[0] == 'm' && n[1] >= '0' && n[1] <= '3' && !n[2]) return 1 + n[1] - '0';
	if (!strcmp(n, "pal")) return 5; if (!strcmp(n, "ldir")) return 6; if (!strcmp(n, "tint")) return 7; if (!strcmp(n, "fogc")) return 8; if (!strcmp(n, "fogp")) return 9; return -1; }
static void glUniform4fv(GLint a, int n, const float *v) {
	if (a >= 1 && a <= 4) memcpy(sgl_m[a - 1], v, 16);
	else if (a == 5) { if (n > 192) n = 192; memcpy(sgl_pal, v, n * 16); }
	else if (a == 7) memcpy(sgl_tint, v, 16);
}
static void sgl_clear(float r, float g, float b) { for (int y = 0; y < SGL_H; y++) for (int x = 0; x < SGL_W; x++) { sgl_img[y][x][0] = r * 255; sgl_img[y][x][1] = g * 255; sgl_img[y][x][2] = b * 255; sgl_z[y][x] = 1e9f; } }
static void glDrawElements(GLenum mode, int cnt, GLenum t, const void *off) {
	(void)mode; (void)t; (void)off; const unsigned char *V = sgl_buf[sgl_arr].d; const unsigned short *I = sgl_buf[sgl_elem].d; if (!V || !I) return;
	int nv = sgl_buf[sgl_arr].n / 32; float (*S)[4] = malloc(nv * 16); float *sh = malloc(nv * 4);
	for (int i = 0; i < nv; i++) {   // MD_VS
		const unsigned char *v = V + i * 32; float p[4] = { 0, 0, 0, 1 }, n[3]; memcpy(p, v, 12); for (int k = 0; k < 3; k++) n[k] = (signed char)v[12 + k] / 127.f;
		float s[3] = { 0, 0, 0 }, sn[3] = { 0, 0, 0 };
		for (int j = 0; j < 4; j++) { float w = v[28 + j] / 255.f; int ix = (int)(v[24 + j] * 4.0f + 0.5f); if (ix > 188) ix = 188;   // 4 rows per entry (model.h, chars b41)
			for (int r = 0; r < 3; r++) { const float *R = sgl_pal[ix + r]; s[r] += w * (R[0] * p[0] + R[1] * p[1] + R[2] * p[2] + R[3]); if (j < 2) sn[r] += w * (R[0] * n[0] + R[1] * n[1] + R[2] * n[2]); } }
		float c[4] = { s[0], s[1], s[2], 1 }; for (int r = 0; r < 4; r++) S[i][r] = sgl_m[r][0] * c[0] + sgl_m[r][1] * c[1] + sgl_m[r][2] * c[2] + sgl_m[r][3];
		float l = sqrtf(sn[0] * sn[0] + sn[1] * sn[1] + sn[2] * sn[2]) + 1e-6f, d = (sn[0] * -0.35f + sn[1] * 0.75f + sn[2] * 0.56f) / l; sh[i] = 0.40f + 0.75f * (d < 0 ? 0 : d > 1 ? 1 : d);
		if (!isfinite(S[i][0]) || !isfinite(S[i][1]) || !isfinite(S[i][3])) sgl_bad++;
	}
	for (int t3 = 0; t3 + 2 < cnt; t3 += 3) {
		float X[3], Y[3], Z[3], L[3]; int ok = 1;
		for (int k = 0; k < 3; k++) { const float *q = S[I[t3 + k]]; if (!(q[3] > 0.01f) || !isfinite(q[0])) { ok = 0; break; } X[k] = (q[0] / q[3] + 1) * SGL_W / 2; Y[k] = (1 - q[1] / q[3]) * SGL_H / 2; Z[k] = q[3]; L[k] = sh[I[t3 + k]]; }
		if (!ok) continue; sgl_tris++;
		int x0 = (int)fmaxf(0, floorf(fminf(X[0], fminf(X[1], X[2])))), x1 = (int)fminf(SGL_W - 1, ceilf(fmaxf(X[0], fmaxf(X[1], X[2]))));
		int y0 = (int)fmaxf(0, floorf(fminf(Y[0], fminf(Y[1], Y[2])))), y1 = (int)fminf(SGL_H - 1, ceilf(fmaxf(Y[0], fmaxf(Y[1], Y[2]))));
		float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]); if (fabsf(den) < 1e-9f) continue;
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
			float px = x + 0.5f, py = y + 0.5f, a = ((Y[1] - Y[2]) * (px - X[2]) + (X[2] - X[1]) * (py - Y[2])) / den, b = ((Y[2] - Y[0]) * (px - X[2]) + (X[0] - X[2]) * (py - Y[2])) / den, c = 1 - a - b;
			if (a < 0 || b < 0 || c < 0) continue; float z = a * Z[0] + b * Z[1] + c * Z[2]; if (z >= sgl_z[y][x]) continue; sgl_z[y][x] = z;
			float l = a * L[0] + b * L[1] + c * L[2]; const float *cc = sgl_vbo_class[sgl_arr] ? sgl_class_col[sgl_vbo_class[sgl_arr] & 3] : sgl_col; for (int k = 0; k < 3; k++) { float v = cc[k] * l * 255 * sgl_tint[k]; sgl_img[y][x][k] = v > 255 ? 255 : v; } }
	}
	free(S); free(sh);
}
static void sgl_write(const char *path) { FILE *f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", SGL_W, SGL_H); fwrite(sgl_img, 1, sizeof(sgl_img), f); fclose(f); }
