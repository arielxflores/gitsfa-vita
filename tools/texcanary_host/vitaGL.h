// host stand-in for the parts of vitaGL texcanary.h uses: texture slots with malloc'd data, buffers as {ptr, size}
#pragma once
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
typedef unsigned GLuint; typedef int GLint, GLsizei; typedef unsigned GLenum, GLbitfield; typedef void GLvoid; typedef intptr_t GLsizeiptr;
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_UNSIGNED_BYTE 0x1401
#define GL_WRITE_ONLY 0x88B9
#define GL_STATIC_DRAW 0x88E4
typedef struct { void *data; int fmt; unsigned w, h; } SceGxmTexture; typedef int SceGxmTextureFormat; typedef unsigned char GLboolean; typedef char GLchar;
typedef enum { VGL_MEM_VRAM, VGL_MEM_RAM, VGL_MEM_PHYCONT, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL, VGL_MEM_ALL } vglMemType;
typedef struct { int used; unsigned char *data; size_t n; SceGxmTexture g; } HTex;
static HTex htx[64]; static GLuint hbound; static unsigned char *h_force;   // h_force: the next allocation returns this (simulated heap corruption)
static int h_bad_upload;   // the next upload stores wrong bytes (simulated broken transfer)
static unsigned char *h_alloc(size_t n) { if (h_force) { unsigned char *p = h_force; h_force = NULL; return p; } return calloc(1, n); }
static void glGenTextures(GLsizei n, GLuint *t) { for (int i = 0; i < n; i++) { for (GLuint k = 1; k < 64; k++) if (!htx[k].used) { htx[k].used = 1; htx[k].data = NULL; t[i] = k; break; } } }
static void glDeleteTextures(GLsizei n, const GLuint *t) { for (int i = 0; i < n; i++) if (t[i] < 64) { htx[t[i]].used = 0; htx[t[i]].data = NULL; } }
static void glBindTexture(GLenum a, GLuint t) { (void)a; hbound = t; }
static void glTexImage2D(GLenum a, GLint l, GLint i, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const GLvoid *d) { (void)a; (void)i; (void)b; (void)f; (void)ty; if (!l) { HTex *T = &htx[hbound]; T->n = (size_t)w * h * 4; T->data = h_alloc(T->n); if (d) memcpy(T->data, d, T->n); if (h_bad_upload) { h_bad_upload = 0; T->data[5] ^= 0x55; } T->g.data = T->data; T->g.fmt = 1; T->g.w = w; T->g.h = h; } }
static void glCompressedTexImage2D(GLenum a, GLint l, GLenum i, GLsizei w, GLsizei h, GLint b, GLsizei s, const void *d) { (void)a; (void)b; HTex *T = &htx[hbound];
	size_t off = 0; if (!l) { T->n = s; T->data = h_alloc(s); T->g.fmt = (int)i; T->g.w = w; T->g.h = h; } else { off = T->n; T->n += s; unsigned char *q = malloc(T->n); memcpy(q, T->data, off); T->data = q; }   // (a mip level moves the data, like vgl_realloc)
	if (d) for (int k = 0; k < s / 4; k++) memcpy(T->data + off + 4 * k, (const unsigned char *)d + s - 4 - 4 * k, 4);   // 'swizzled': the words in another order
	if (h_bad_upload && d) { h_bad_upload = 0; T->data[off + 9] ^= 0x55; } T->g.data = T->data; }
static void glGenerateMipmap(GLenum t) { (void)t; }
static void glGetIntegerv(GLenum p, GLint *v) { *v = p == GL_TEXTURE_BINDING_2D ? (GLint)hbound : GL_TEXTURE0; }
static void glActiveTexture(GLenum t) { (void)t; }
static void *vglGetTexDataPointer(GLenum t) { (void)t; return htx[hbound].data; }
static SceGxmTexture *vglGetGxmTexture(GLenum t) { (void)t; return &htx[hbound].g; }
static void *sceGxmTextureGetData(const SceGxmTexture *g) { return g->data; }
static SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *g) { return g->fmt; }
static unsigned sceGxmTextureGetWidth(const SceGxmTexture *g) { return g->w; }
static unsigned sceGxmTextureGetHeight(const SceGxmTexture *g) { return g->h; }
static size_t vglMallocUsableSize(void *p) { (void)p; return 0; }
static size_t vglMemFree(vglMemType t) { (void)t; return 1 << 20; }
static void *vglAlloc(uint32_t n, vglMemType t) { (void)t; return malloc(n); }
static void glFinish(void) {}
static void glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) { (void)m; (void)c; (void)t; (void)i; }
static void glDrawArrays(GLenum m, GLint f, GLsizei c) { (void)m; (void)f; (void)c; }
static void vglSwapBuffers(GLboolean c) { (void)c; }
typedef struct { void *ptr; int32_t size; } HBuf;
static GLuint hab;
static HBuf *h_bufs; static int h_nb;   // buffer structs in the low 4 GB (ids are 32-bit vbo pointers, as on the Vita)
static void glGenBuffers(GLsizei n, GLuint *b) { if (!h_bufs) h_bufs = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0); for (int i = 0; i < n; i++) b[i] = (GLuint)(uintptr_t)&h_bufs[h_nb++]; }
static int h_double_free;
static void glDeleteBuffers(GLsizei n, const GLuint *b) { for (int i = 0; i < n; i++) { HBuf *B = (HBuf *)(uintptr_t)b[i]; if (B->size < 0) h_double_free++; B->size = -1; } }   // (kept allocated so a double delete is visible)
static void glBindBuffer(GLenum t, GLuint b) { (void)t; hab = b; }
static void glBufferData(GLenum t, GLsizei s, const GLvoid *d, GLenum u) { (void)t; (void)u; HBuf *B = (HBuf *)(uintptr_t)hab; B->ptr = h_alloc(s); B->size = s; if (d) memcpy(B->ptr, d, s); }
static void *glMapBuffer(GLenum t, GLbitfield a) { (void)t; (void)a; return ((HBuf *)(uintptr_t)hab)->ptr; }
static void *vglMemalign(uint32_t a, uint32_t n) { (void)a; return malloc(n); }
static void vglFree(void *p) { free(p); }
static void glCompileShader(GLuint s) { (void)s; }
#define GL_SHADER_TYPE 0x8B4F
#define GL_COMPILE_STATUS 0x8B81
static void glShaderSource(GLuint s, GLsizei n, const GLchar *const *str, const GLint *len) { (void)s; (void)n; (void)str; (void)len; }
static void glGetShaderiv(GLuint s, GLenum p, GLint *v) { (void)s; *v = p == GL_COMPILE_STATUS; }
static void glShaderBinary(GLsizei c, const GLuint *h, GLenum f, const void *b, GLsizei l) { (void)c; (void)h; (void)f; (void)b; (void)l; }
static void vglGetShaderBinary(GLuint s, GLsizei bs, GLsizei *len, void *b) { (void)s; (void)bs; (void)b; *len = 0; }
static void glLinkProgram(GLuint p) { (void)p; }
static int sceGxmTextureInitLinear(SceGxmTexture *t, const void *d, SceGxmTextureFormat f, unsigned w, unsigned h, unsigned m) { (void)t; (void)d; (void)f; (void)w; (void)h; (void)m; return 0; }
