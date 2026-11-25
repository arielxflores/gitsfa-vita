// skills.h viewer: the software rasterizer of /root/fx/host (sprites, textured triangles) + no-op stubs for what it lacks
// (indexed / buffer draws: the deployable models are not drawn here; their previews are tools/skills/out/models_preview.png).
#pragma once
#include "vitaGL_sw.h"
#define GL_UNSIGNED_SHORT 0x1403
#define GL_BYTE 0x1400
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_WRITE_ONLY 0x88B9
#define GL_REPEAT 0x2901
#define GL_TEXTURE2 0x84C2
#define GL_CG_VERTEX_SHADER_EXT GL_VERTEX_SHADER
#define GL_CG_FRAGMENT_SHADER_EXT GL_FRAGMENT_SHADER
#define GL_STENCIL_TEST 0x0B90
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_GREATER 0x0204
#define GL_STENCIL_BUFFER_BIT 0x400
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGB 0x1907
#ifndef GL_ACTIVE_UNIFORMS
#define GL_ACTIVE_UNIFORMS 0x8B86
#endif
static GLuint sw_nbuf = 1000;
static void glGenBuffers(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = sw_nbuf++; }
static void glBufferData(GLenum a, long n, const void *d, GLenum u) { (void)a; (void)n; (void)d; (void)u; }
static void glDeleteBuffers(int n, const GLuint *b) { (void)n; (void)b; }
static void glDeleteTextures(int n, const GLuint *b) { (void)n; (void)b; }
static void glGenerateMipmap(GLenum t) { (void)t; }
static void glUniform1f(GLint l, float v) { (void)l; (void)v; }
static void glShaderSource(GLuint s, int n, const char **src, const GLint *len) { (void)s; (void)n; (void)src; (void)len; }
static void glCompileShader(GLuint s) { (void)s; }
static void glGetShaderInfoLog(GLuint s, int n, int *l, char *b) { (void)s; (void)n; (void)l; if (b) b[0] = 0; }
static long sw_skipped_draws;
static void glDrawElements(GLenum m, int n, GLenum t, const void *i) { (void)m; (void)n; (void)t; (void)i; sw_skipped_draws++; }
static unsigned char sw_map[1 << 20]; static void *glMapBuffer(GLenum t, GLenum a) { (void)t; (void)a; return sw_map; } static int glUnmapBuffer(GLenum t) { (void)t; return 1; }
static void glStencilOp(GLenum a, GLenum b, GLenum c) { (void)a; (void)b; (void)c; } static void glStencilFunc(GLenum a, int b, unsigned c) { (void)a; (void)b; (void)c; } static void glClear(unsigned m) { (void)m; }
