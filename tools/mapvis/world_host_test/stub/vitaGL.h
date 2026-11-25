// host stub of the few vitaGL / GL calls world.h makes (parsing + shader source test)
#pragma once
#include <stdio.h>
#include <string.h>
typedef unsigned GLuint; typedef int GLint; typedef unsigned GLenum; typedef int GLsizei; typedef unsigned char GLboolean; typedef float GLfloat; typedef void GLvoid;
#define GL_TRUE 1
#define GL_FALSE 0
enum { GL_VERTEX_SHADER=1, GL_FRAGMENT_SHADER, GL_COMPILE_STATUS, GL_LINK_STATUS, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR,
 GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_REPEAT, GL_CLAMP_TO_EDGE, GL_RGBA, GL_RGB, GL_UNSIGNED_BYTE, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT,
 GL_ARRAY_BUFFER, GL_ELEMENT_ARRAY_BUFFER, GL_STATIC_DRAW, GL_TEXTURE0, GL_TEXTURE1, GL_TEXTURE2, GL_TEXTURE3, GL_CULL_FACE, GL_BLEND, GL_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA,
 GL_FLOAT, GL_TRIANGLES, GL_UNSIGNED_SHORT };
static unsigned stub_id = 1; static int stub_draws, stub_tris;
static void glGenTextures(int n, GLuint *t) { for (int i = 0; i < n; i++) t[i] = stub_id++; }
static void glDeleteTextures(int n, const GLuint *t) { (void)n; (void)t; }
static void glGenBuffers(int n, GLuint *t) { glGenTextures(n, t); }
static void glDeleteBuffers(int n, const GLuint *t) { (void)n; (void)t; }
static void glBindTexture(GLenum a, GLuint b) { (void)a; (void)b; }
static void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a; (void)b; (void)c; }
static void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h, const void *i) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; }
static void glCompressedTexImage2D(GLenum a, GLint b, GLenum c, GLsizei d, GLsizei e, GLint f, GLsizei g, const void *h) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h; }
static void sceGxmTransferFinish(void) {}
static void glActiveTexture(GLenum a) { (void)a; }
static void glBindBuffer(GLenum a, GLuint b) { (void)a; (void)b; }
static void glBufferData(GLenum a, long n, const void *d, GLenum u) { (void)a; (void)n; (void)d; (void)u; }
static GLuint glCreateProgram(void) { return stub_id++; }
static void glAttachShader(GLuint p, GLuint s) { (void)p; (void)s; }
static void glBindAttribLocation(GLuint p, GLuint i, const char *n) { (void)p; (void)i; (void)n; }
static void glLinkProgram(GLuint p) { (void)p; }
static void glGetProgramiv(GLuint p, GLenum e, GLint *v) { (void)p; (void)e; *v = 1; }
static void glGetShaderiv(GLuint p, GLenum e, GLint *v) { (void)p; (void)e; *v = 1; }
static void glDeleteShader(GLuint s) { (void)s; }
static GLint glGetUniformLocation(GLuint p, const char *n) { (void)p; (void)n; return 1; }
static void glUseProgram(GLuint p) { (void)p; }
static void glUniform1i(GLint l, GLint v) { (void)l; (void)v; }
static void glUniform1f(GLint l, GLfloat v) { (void)l; (void)v; }
static void glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { (void)l; (void)n; (void)v; }
static void glEnable(GLenum e) { (void)e; } static void glDisable(GLenum e) { (void)e; }
static void glDepthMask(GLboolean b) { (void)b; } static void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
static void glEnableVertexAttribArray(GLuint i) { (void)i; } static void glDisableVertexAttribArray(GLuint i) { (void)i; }
static void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void *p) { (void)i;(void)s;(void)t;(void)n;(void)st;(void)p; }
static void glDrawElements(GLenum m, GLsizei n, GLenum t, const void *p) { (void)m; (void)t; (void)p; stub_draws++; stub_tris += n / 3; }
static unsigned char *vglGetTexDataPointer(GLenum t) { (void)t; return NULL; }
