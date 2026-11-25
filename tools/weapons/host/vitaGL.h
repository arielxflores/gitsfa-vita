// Minimal no-op vitaGL stand-in so engine headers (model.h, weapon.h, wpn_pose.h) build on the PC for pose tests.
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef unsigned char GLboolean; typedef float GLfloat; typedef int GLsizei;
enum { GL_ACTIVE_UNIFORMS = 0x8B86, GL_TEXTURE_2D = 1, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, GL_FALSE = 0, GL_TRUE = 1, GL_TRIANGLES = 4,
	GL_ARRAY_BUFFER = 100, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_DYNAMIC_DRAW, GL_WRITE_ONLY, GL_VERTEX_SHADER = 200, GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS,
	GL_TEXTURE0 = 300, GL_BLEND = 400, GL_DEPTH_TEST, GL_CULL_FACE, GL_ONE = 500, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_FLOAT = 600, GL_BYTE, GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT,
	GL_TEXTURE1 = 301, GL_TEXTURE2 = 302, GL_RGB = 700, GL_RGBA, GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE, GL_NEAREST,
	GL_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3 };
static int hgl_pal_uploads;
#define sceGxmTransferFinish() 0
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = i + 1; }
static void glDeleteTextures(int n, const GLuint *t) { (void)n; (void)t; }
static void glBindTexture(GLenum a, GLuint b) { (void)a; (void)b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glCompressedTexImage2D(GLenum t, int l, GLenum f, int w, int h, int b, int s, const void *p) { (void)t; (void)l; (void)f; (void)w; (void)h; (void)b; (void)s; (void)p; }
static void glTexImage2D(GLenum t, int l, int i, int w, int h, int b, GLenum f, GLenum y, const void *p) { (void)t; (void)l; (void)i; (void)w; (void)h; (void)b; (void)f; (void)y; (void)p; }
static void glGenBuffers(int n, GLuint *b) { for (int i = 0; i < n; i++) b[i] = i + 1; }
static void glDeleteBuffers(int n, const GLuint *b) { (void)n; (void)b; }
static void glBindBuffer(GLenum a, GLuint b) { (void)a; (void)b; }
static void glBufferData(GLenum a, long s, const void *d, GLenum u) { (void)a; (void)s; (void)d; (void)u; }
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
static GLint glGetUniformLocation(GLuint a, const char *b) { (void)a; (void)b; return 1; }
static void glUseProgram(GLuint a) { (void)a; }
static void glUniform1i(GLint a, int b) { (void)a; (void)b; }
static void glUniform4fv(GLint a, int b, const float *c) { (void)a; (void)b; (void)c; hgl_pal_uploads++; }
static void glEnableVertexAttribArray(GLuint a) { (void)a; }
static void glDisableVertexAttribArray(GLuint a) { (void)a; }
static void glVertexAttribPointer(GLuint a, int b, GLenum c, GLboolean d, int e, const void *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
static void glActiveTexture(GLenum a) { (void)a; }
static void glEnable(GLenum a) { (void)a; }
static void glDisable(GLenum a) { (void)a; }
static void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
static void glDepthMask(GLboolean a) { (void)a; }
static void glDrawElements(GLenum a, int b, GLenum c, const void *d) { (void)a; (void)b; (void)c; (void)d; }
