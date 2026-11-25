// Host stub of vitaGL for the skills.h test: every call is a no-op except the counters (draw calls, triangles,
// blend / depth state) that the test reads to estimate the per-frame drawing cost.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
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
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_STENCIL_TEST 0x0B90
#define GL_SCISSOR_TEST 0x0C11
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_COLOR 0x0306
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_GREATER 0x0204
#define GL_STENCIL_BUFFER_BIT 0x400
#define GL_DEPTH_BUFFER_BIT 0x100
#define GL_COLOR_BUFFER_BIT 0x4000
static long gls_draws, gls_tris, gls_sprite_quads; static int gls_depth = 1, gls_blend, gls_cull = 1, gls_dmask = 1; static GLenum gls_bs = 1, gls_bd = 0;
static GLuint gls_next = 1;
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = gls_next++; }
static void glGenBuffers(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = gls_next++; }
static void glDeleteTextures(int n, const GLuint *t) { (void)n; (void)t; }
static void glDeleteBuffers(int n, const GLuint *b) { (void)n; (void)b; }
static GLuint gls_tex; static FILE *gls_cap; static float gls_m[4][4]; static const void *gls_ap[3]; static int gls_ast[3];
static void glBindTexture(GLenum a, GLuint b) { (void)a; gls_tex = b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glCompressedTexImage2D(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLsizei sz, const void *d) { (void)t; (void)l; (void)f; (void)w; (void)h; (void)b; (void)sz; (void)d; }
static void glTexImage2D(GLenum t, GLint l, GLenum f, GLsizei w, GLsizei h, GLint b, GLenum f2, GLenum ty, const void *d) { (void)t; (void)l; (void)f; (void)w; (void)h; (void)b; (void)f2; (void)ty; (void)d; }
static void glGenerateMipmap(GLenum t) { (void)t; }
static void glBindBuffer(GLenum a, GLuint b) { (void)a; (void)b; }
static void glBufferData(GLenum a, long n, const void *d, GLenum u) { (void)a; (void)n; (void)d; (void)u; }
static unsigned char gls_map[1 << 20];
static void *glMapBuffer(GLenum t, GLenum a) { (void)t; (void)a; return gls_map; }
static int glUnmapBuffer(GLenum t) { (void)t; return 1; }
static GLuint glCreateShader(GLenum t) { (void)t; return gls_next++; }
static void glShaderSource(GLuint s, int n, const char **src, const GLint *len) { (void)s; (void)n; (void)src; (void)len; }
static void glCompileShader(GLuint s) { (void)s; }
#define glGetShaderiv(a, b, c) (*(c) = 1)
#define glGetProgramiv(a, b, c) (*(c) = 1)
static void glGetShaderInfoLog(GLuint s, int n, int *l, char *b) { (void)s; (void)n; (void)l; if (b) b[0] = 0; }
static GLuint glCreateProgram(void) { return gls_next++; }
static void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
static void glBindAttribLocation(GLuint p, GLuint i, const char *n) { (void)p; (void)i; (void)n; }
static void glLinkProgram(GLuint p) { (void)p; }
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p; if (n[0] == 'm' && n[1] >= '0' && n[1] <= '3' && !n[2]) return 10 + n[1] - '0'; return 1; }
static void glGetActiveUniform(GLuint p, GLuint i, GLsizei n, GLsizei *l, GLint *s, GLenum *t, char *nm) { (void)p; (void)i; (void)n; if (l) *l = 0; if (s) *s = 0; if (t) *t = 0; if (nm) nm[0] = 0; }
static void glUseProgram(GLuint p) { (void)p; }
static void glUniform1i(GLint l, int v) { (void)l; (void)v; }
static void glUniform1f(GLint l, float v) { (void)l; (void)v; }
static void glUniform4fv(GLint l, int n, const float *v) { (void)n; if (l >= 10 && l < 14) memcpy(gls_m[l - 10], v, 16); }
static void glEnableVertexAttribArray(GLuint i) { (void)i; }
static void glDisableVertexAttribArray(GLuint i) { (void)i; }
static void glVertexAttribPointer(GLuint i, int n, GLenum t, GLboolean nm, int st, const void *p) { (void)n; (void)t; (void)nm; if (i < 3) { gls_ap[i] = p; gls_ast[i] = st; } }
static void glActiveTexture(GLenum t) { (void)t; }
static void glEnable(GLenum c) { if (c == GL_BLEND) gls_blend = 1; if (c == GL_DEPTH_TEST) gls_depth = 1; if (c == GL_CULL_FACE) gls_cull = 1; }
static void glDisable(GLenum c) { if (c == GL_BLEND) gls_blend = 0; if (c == GL_DEPTH_TEST) gls_depth = 0; if (c == GL_CULL_FACE) gls_cull = 0; }
static void glBlendFunc(GLenum s, GLenum d) { gls_bs = s; gls_bd = d; }
static void glBlendFuncSeparate(GLenum s, GLenum d, GLenum sa, GLenum da) { (void)sa; (void)da; gls_bs = s; gls_bd = d; }   // (b116: model.h MD_BM_ON)
static void glDepthMask(GLboolean m) { gls_dmask = m; }
static void glDrawArrays(GLenum m, int f, int n) { (void)m; (void)f; gls_draws++; gls_tris += n / 3; gls_sprite_quads += n / 6;
	if (gls_cap && gls_ap[0] && gls_ast[0] == 36 && n > 0) {   // capture a sprite-batch draw: tex, blend, the 4 matrix rows, the vertices
		int hd[4] = { (int)gls_tex, (int)gls_bs, (int)gls_bd, n }; fwrite(hd, 4, 4, gls_cap); fwrite(gls_m, 4, 16, gls_cap); fwrite((const float *)gls_ap[0] + f * 9, 36, n, gls_cap); } }
static void glDrawElements(GLenum m, int n, GLenum t, const void *i) { (void)m; (void)t; (void)i; gls_draws++; gls_tris += n / 3; }
static void glStencilOp(GLenum a, GLenum b, GLenum c) { (void)a; (void)b; (void)c; }
static void glStencilFunc(GLenum a, int b, unsigned c) { (void)a; (void)b; (void)c; }
static void glClear(unsigned m) { (void)m; }
static inline int sceGxmTransferFinish(void) { return 0; }
