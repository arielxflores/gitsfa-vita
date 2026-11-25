// Host stand-in for vitaGL (copy of tools/menu/host/vitaGL.h): records textures and draw calls so hud.h / hud_game.h / menu.h
// can be run on the PC and their output rasterised by tools/menu/raster.py exactly as the shader would.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef float GLfloat; typedef int GLsizei;
enum { GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_NEAREST, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE,
	GL_RGBA, GL_RGB, GL_UNSIGNED_BYTE, GL_FLOAT, GL_FALSE = 0, GL_TRUE = 1, GL_TRIANGLES = 4, GL_TRIANGLE_STRIP = 5, GL_ARRAY_BUFFER = 100, GL_ELEMENT_ARRAY_BUFFER,
	GL_VERTEX_SHADER = 200, GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TEXTURE0 = 300, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_TEXTURE4, GL_BLEND = 400, GL_DEPTH_TEST, GL_CULL_FACE,
	GL_ONE = 500, GL_ZERO, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3,
	GL_CG_VERTEX_SHADER_EXT = 600, GL_CG_FRAGMENT_SHADER_EXT, GL_COLOR_BUFFER_BIT = 0x4000, GL_DEPTH_BUFFER_BIT = 0x100 };
typedef struct { int w, h, fmt; unsigned char *px; int size; } CapTex;
static CapTex cap_tex[256]; static GLuint cap_ntex = 0, cap_bound = 0, cap_blend_src = GL_ONE, cap_blend_dst = GL_ONE_MINUS_SRC_ALPHA;
static const void *cap_ptr[8]; static int cap_size[8], cap_stride[8];
static FILE *cap_out;
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = ++cap_ntex; }
static void glDeleteTextures(int n, const GLuint *t) { for (int i = 0; i < n; i++) { free(cap_tex[t[i]].px); memset(&cap_tex[t[i]], 0, sizeof(CapTex)); } }
static void glDeleteBuffers(int n, const GLuint *b) { (void)n; (void)b; }   // ui b44: model.h's model_free (combo.c links it when -O1 keeps it)
static void glBindTexture(GLenum a, GLuint b) { (void)a; cap_bound = b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glTexImage2D(GLenum t, int lvl, int ifmt, int w, int h, int b, GLenum f, GLenum ty, const void *px) {
	(void)t; (void)ifmt; (void)b; (void)ty; if (lvl) return; CapTex *T = &cap_tex[cap_bound]; free(T->px);
	int bpp = f == GL_RGB ? 3 : 4; T->w = w; T->h = h; T->fmt = bpp; T->size = w * h * bpp; T->px = malloc(T->size); if (px) memcpy(T->px, px, T->size); else memset(T->px, 0, T->size); }
static void glCompressedTexImage2D(GLenum t, int lvl, GLenum f, int w, int h, int b, int size, const void *px) {
	(void)t; (void)b; if (lvl) return; CapTex *T = &cap_tex[cap_bound]; free(T->px); T->w = w; T->h = h; T->fmt = (int)f; T->size = size; T->px = malloc(size); memcpy(T->px, px, size); }
static void glGenerateMipmap(GLenum t) { (void)t; }
static void glBindBuffer(GLenum a, GLuint b) { (void)a; (void)b; }
static GLuint glCreateShader(GLenum t) { (void)t; return 1; }
static void glShaderSource(GLuint s, int n, const char **src, const GLint *len) { (void)s; (void)n; (void)src; (void)len; }
static void glCompileShader(GLuint s) { (void)s; }
static void glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void)s; (void)p; *v = 1; }
static void glGetShaderInfoLog(GLuint s, int n, int *l, char *b) { (void)s; (void)n; (void)l; b[0] = 0; }
static GLuint glCreateProgram(void) { return 1; }
static void glAttachShader(GLuint a, GLuint b) { (void)a; (void)b; }
static void glBindAttribLocation(GLuint a, GLuint b, const char *c) { (void)a; (void)b; (void)c; }
static void glLinkProgram(GLuint a) { (void)a; }
static void glGetProgramiv(GLuint a, GLenum b, GLint *c) { (void)a; (void)b; *c = 1; }
static GLint glGetUniformLocation(GLuint a, const char *b) { (void)a; (void)b; return 1; }
static void glUseProgram(GLuint a) { (void)a; }
static void glUniform1i(GLint a, int b) { (void)a; (void)b; }
static void glUniform4fv(GLint a, int b, const float *c) { (void)a; (void)b; (void)c; }
static void glEnableVertexAttribArray(GLuint a) { (void)a; }
static void glDisableVertexAttribArray(GLuint a) { (void)a; }
static void glActiveTexture(GLenum a) { (void)a; }
static void glEnable(GLenum a) { (void)a; }
static void glDisable(GLenum a) { (void)a; }
static void glBlendFunc(GLenum a, GLenum b) { cap_blend_src = a; cap_blend_dst = b; }
static void glDepthMask(GLboolean a) { (void)a; }
static void glClear(GLenum a) { (void)a; }
static void glClearColor(float r, float g, float b, float a) { (void)r; (void)g; (void)b; (void)a; }
static void glVertexAttribPointer(GLuint i, int size, GLenum ty, GLboolean n, int stride, const void *p) { (void)ty; (void)n; cap_ptr[i] = p; cap_size[i] = size; cap_stride[i] = stride; }
// one draw: tex id, blend, vertex count, then per vertex 17 floats (pos4 uv4 mul4 add4 thr)
static void glDrawArrays(GLenum mode, int first, int count) {
	(void)mode; if (!cap_out) return;
	int hdr[3] = { (int)cap_bound, cap_blend_src == GL_ONE ? 1 : 0, count }; fwrite(hdr, 4, 3, cap_out);
	for (int v = first; v < first + count; v++) for (int a = 0; a < 5; a++) {
		float f[4] = { 0, 0, 0, 1 }; const float *s = (const float *)((const char *)cap_ptr[a] + (size_t)v * cap_stride[a]);
		for (int k = 0; k < cap_size[a]; k++) f[k] = s[k];
		fwrite(f, 4, a == 4 ? 1 : 4, cap_out); }
}
static void cap_begin(const char *path) { cap_out = fopen(path, "wb"); }
static void cap_end(void) { if (cap_out) fclose(cap_out); cap_out = NULL; }
static void cap_dump_textures(const char *dir) {
	char p[512]; for (GLuint i = 1; i <= cap_ntex; i++) { CapTex *T = &cap_tex[i]; if (!T->px) continue;
		snprintf(p, sizeof(p), "%s/tex%u.bin", dir, i); FILE *f = fopen(p, "wb"); int h[4] = { T->w, T->h, T->fmt, T->size }; fwrite(h, 4, 4, f); fwrite(T->px, 1, T->size, f); fclose(f); }
}
