// host stand-in for the GL that world.h + grade.h use (tools/world_host/glowbind.c). It keeps the state a draw depends on:
// the texture bound on each unit, each program's shaders, the unit each sampler was pointed at (glUniform1i), the bound
// buffers. A sampler "exists" in a program only when its fragment source reads it (tex2D(name, ...)), as with the Vita's
// compiler, which drops unused samplers (glGetUniformLocation then returns -1). glDrawElements calls h_draw_hook.
#pragma once
#define W_SEG_BYTES 0   // perf (b46): world.h's shared mesh buffers off here (these tools find a draw's mesh by its index buffer)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned GLuint; typedef int GLint, GLsizei; typedef unsigned GLenum, GLbitfield; typedef void GLvoid; typedef intptr_t GLsizeiptr;
typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_TRIANGLES 0x0004
#define GL_ONE 1
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_LEQUAL 0x0203
#define GL_CULL_FACE 0x0B44
#define GL_BLEND 0x0BE2
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_BYTE 0x1400   // b67
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE6 0x84C6   // b67 (world.h env maps)
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define H_MAXS 512
#define H_MAXP 256
#define H_NSMP 4
static const char *const h_smp_name[H_NSMP] = { "dif", "lm", "lut", "glw" };
static char *h_src[H_MAXS]; static GLenum h_stype[H_MAXS]; static GLuint h_nshader;
typedef struct { GLuint vs, fs; int unit[H_NSMP]; } HProg;
static HProg h_prog[H_MAXP]; static GLuint h_nprog, h_cur;
static GLuint h_unit_tex[16]; static int h_active;
static GLuint h_ntex, h_nbuf, h_array_buf, h_elem_buf;
static long h_draws;
static void (*h_draw_hook)(void);

static GLuint glCreateShader(GLenum t) { if (++h_nshader >= H_MAXS) { fprintf(stderr, "host GL: out of shaders\n"); exit(2); } h_stype[h_nshader] = t; return h_nshader; }
static void glShaderSource(GLuint s, GLsizei n, const GLchar *const *str, const GLint *len) { (void)n; (void)len; free(h_src[s]); h_src[s] = strdup(str[0]); }
static void glCompileShader(GLuint s) { (void)s; }
static void glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void)s; (void)p; *v = 1; }
static void glDeleteShader(GLuint s) { (void)s; }
static GLuint glCreateProgram(void) { if (++h_nprog >= H_MAXP) { fprintf(stderr, "host GL: out of programs\n"); exit(2); } memset(&h_prog[h_nprog], 0, sizeof(HProg)); return h_nprog; }
static void glAttachShader(GLuint p, GLuint s) { if (h_stype[s] == GL_VERTEX_SHADER) h_prog[p].vs = s; else h_prog[p].fs = s; }
static void glBindAttribLocation(GLuint p, GLuint i, const GLchar *n) { (void)p; (void)i; (void)n; }
static void glLinkProgram(GLuint p) { (void)p; }
static void glGetProgramiv(GLuint p, GLenum e, GLint *v) { (void)p; (void)e; *v = 1; }
static void glUseProgram(GLuint p) { h_cur = p; }
static int h_reads(GLuint p, int k) { char pat[16]; snprintf(pat, sizeof(pat), "tex2D(%s,", h_smp_name[k]); return p && h_prog[p].fs && h_src[h_prog[p].fs] && strstr(h_src[h_prog[p].fs], pat) != NULL; }
// locations: program * 64 + (sampler k + 1), or program * 64 + 32 for any other uniform named in a source
static GLint glGetUniformLocation(GLuint p, const GLchar *n) {
	for (int k = 0; k < H_NSMP; k++) if (!strcmp(n, h_smp_name[k])) return h_reads(p, k) ? (GLint)(p * 64 + k + 1) : -1;
	const char *a = h_prog[p].vs ? h_src[h_prog[p].vs] : NULL, *b = h_prog[p].fs ? h_src[h_prog[p].fs] : NULL;
	return (a && strstr(a, n)) || (b && strstr(b, n)) ? (GLint)(p * 64 + 32) : -1;
}
static void glUniform1i(GLint l, GLint v) { if (l <= 0) return; int k = l % 64 - 1; if (k >= 0 && k < H_NSMP) h_prog[l / 64].unit[k] = v; }
static void glUniform1f(GLint l, GLfloat v) { (void)l; (void)v; }
static void glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { (void)l; (void)n; (void)v; }
static void glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = ++h_ntex; }
static void glDeleteTextures(GLsizei n, const GLuint *t) { (void)n; (void)t; }
static void glActiveTexture(GLenum u) { h_active = (int)(u - GL_TEXTURE0); }
static void glBindTexture(GLenum t, GLuint id) { (void)t; h_unit_tex[h_active] = id; }
static void glTexParameteri(GLenum t, GLenum p, GLint v) { (void)t; (void)p; (void)v; }
static void glTexImage2D(GLenum a, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const GLvoid *d) { (void)a; (void)l; (void)i; (void)w; (void)h; (void)b; (void)f; (void)ty; (void)d; }
static void glCompressedTexImage2D(GLenum a, GLint l, GLenum i, GLsizei w, GLsizei h, GLint b, GLsizei s, const void *d) { (void)a; (void)l; (void)i; (void)w; (void)h; (void)b; (void)s; (void)d; }
static void *vglGetTexDataPointer(GLenum t) { (void)t; return NULL; }
static void sceGxmTransferFinish(void) {}
static void glGenBuffers(GLsizei n, GLuint *b) { for (int i = 0; i < n; i++) b[i] = ++h_nbuf; }
static void glDeleteBuffers(GLsizei n, const GLuint *b) { (void)n; (void)b; }
static void glBindBuffer(GLenum t, GLuint b) { if (t == GL_ARRAY_BUFFER) h_array_buf = b; else h_elem_buf = b; }
static void glBufferData(GLenum t, GLsizeiptr n, const void *d, GLenum u) { (void)t; (void)n; (void)d; (void)u; }
static void glVertexAttribPointer(GLuint i, GLint n, GLenum t, GLboolean nm, GLsizei s, const void *p) { (void)i; (void)n; (void)t; (void)nm; (void)s; (void)p; }
static void glEnableVertexAttribArray(GLuint i) { (void)i; }
static void glDisableVertexAttribArray(GLuint i) { (void)i; }
static void glEnable(GLenum c) { (void)c; }
static void glDisable(GLenum c) { (void)c; }
static void glDepthMask(GLboolean m) { (void)m; }
static void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
static void glDrawElements(GLenum m, GLsizei n, GLenum t, const void *o) { (void)m; (void)n; (void)t; (void)o; h_draws++; if (h_draw_hook) h_draw_hook(); }
// the texture the current program's sampler k reads in a draw (0: the program does not read it)
static GLuint h_sampled(int k) { return h_reads(h_cur, k) ? h_unit_tex[h_prog[h_cur].unit[k] & 15] : 0; }
