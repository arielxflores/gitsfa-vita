// Host shim of vitaGL (perf, b37: tools/hostgame; see vitasdk.h). A recording GL: no pixels, but every buffer and texture
// owns host memory of the size vitaGL would give it, every upload / map / draw / uniform reads or writes exactly the bytes
// the real call would, and hostshim.c checks what AddressSanitizer cannot see on its own (draws past a buffer's end,
// uniform arrays longer than declared, compressed sizes, ids that are not live). Uploads are copied at the next
// sceGxmTransferFinish / glFinish / swap, as vitaGL's transfers are asynchronous.
#pragma once
#ifndef HOSTGAME_SHIM_VITAGL
#define HOSTGAME_SHIM_VITAGL
#include "vitasdk.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int GLenum; typedef unsigned char GLboolean; typedef unsigned int GLbitfield; typedef void GLvoid; typedef signed char GLbyte;
typedef short GLshort; typedef int GLint; typedef int GLsizei; typedef unsigned char GLubyte; typedef unsigned short GLushort; typedef unsigned int GLuint;
typedef float GLfloat; typedef float GLclampf; typedef char GLchar; typedef intptr_t GLintptr; typedef intptr_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_TEXTURE_2D 0x0DE1
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_HALF_FLOAT 0x140B
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR 0x1E02
#define GL_DECR 0x1E03
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_MIRRORED_REPEAT 0x8370
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE6 0x84C6
#define GL_TEXTURE7 0x84C7
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_WRITE_ONLY 0x88B9
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_SHADER_TYPE 0x8B4F
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A   /* b102: ablur.h */
#define GL_DEPTH24_STENCIL8 0x88F0
#define GL_RENDERBUFFER 0x8D41
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT16 0x81A5
void glGenRenderbuffers(GLsizei n, GLuint *r);
void glDeleteRenderbuffers(GLsizei n, const GLuint *r);
void glBindRenderbuffer(GLenum t, GLuint r);
void glRenderbufferStorage(GLenum t, GLenum f, GLsizei w, GLsizei h);
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rt, GLuint r);
GLenum glCheckFramebufferStatus(GLenum t);
void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d);
#define GL_CG_VERTEX_SHADER_EXT 0x8D61
#define GL_CG_FRAGMENT_SHADER_EXT 0x8D62

typedef enum { VGL_MEM_VRAM = 0, VGL_MEM_RAM, VGL_MEM_SLOW, VGL_MEM_BUDGET, VGL_MEM_EXTERNAL, VGL_MEM_ALL } vglMemType;

void glActiveTexture(GLenum texture);
void glAttachShader(GLuint program, GLuint shader);
void glBindAttribLocation(GLuint program, GLuint index, const GLchar *name);
void glBindBuffer(GLenum target, GLuint buffer);
void glBindFramebuffer(GLenum target, GLuint fb);
void glBindTexture(GLenum target, GLuint texture);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
void glClear(GLbitfield mask);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glCompileShader(GLuint shader);
void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum type);
void glCullFace(GLenum mode);
void glDeleteBuffers(GLsizei n, const GLuint *buffers);
void glDeleteFramebuffers(GLsizei n, const GLuint *fbs);
void glDeleteProgram(GLuint program);
void glDeleteShader(GLuint shader);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glDisable(GLenum cap);
void glDisableVertexAttribArray(GLuint index);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices);
void glEnable(GLenum cap);
void glEnableVertexAttribArray(GLuint index);
void glFinish(void);
void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
void glFrontFace(GLenum mode);
void glGenBuffers(GLsizei n, GLuint *buffers);
void glGenFramebuffers(GLsizei n, GLuint *fbs);
void glGenTextures(GLsizei n, GLuint *textures);
void glGenerateMipmap(GLenum target);
void glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
void glGetIntegerv(GLenum pname, GLint *data);
void glGetProgramiv(GLuint program, GLenum pname, GLint *params);
void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei *length, GLchar *infoLog);
void glGetShaderiv(GLuint shader, GLenum pname, GLint *params);
GLint glGetUniformLocation(GLuint program, const GLchar *name);
void glLinkProgram(GLuint program);
void *glMapBuffer(GLenum target, GLbitfield access);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *data);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glShaderBinary(GLsizei count, const GLuint *shaders, GLenum binaryformat, const void *binary, GLsizei length);
void glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
void glStencilFunc(GLenum func, GLint ref, GLuint mask);
void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *data);
void glTexParameteri(GLenum target, GLenum pname, GLint param);
void glUniform1f(GLint location, GLfloat v0);
void glUniform1i(GLint location, GLint v0);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void glUniform1fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform2fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform3fv(GLint location, GLsizei count, const GLfloat *value);
void glUniform4fv(GLint location, GLsizei count, const GLfloat *value);
void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
GLboolean glUnmapBuffer(GLenum target);
void glUseProgram(GLuint program);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

GLboolean vglInit(int legacy_pool_size);
GLboolean vglInitExtended(int legacy_pool_size, int width, int height, int ram_threshold, SceGxmMultisampleMode msaa);
GLboolean vglInitWithCustomThreshold(int pool_size, int width, int height, int ram_threshold, int cdram_threshold, int phycont_threshold, int cdlg_threshold, SceGxmMultisampleMode msaa);
void vglSwapBuffers(GLboolean has_commondialog);
unsigned int vglGetFrameNumber(void);
size_t vglMemFree(vglMemType type);
void *vglAlloc(uint32_t size, vglMemType type);
void *vglMemalign(uint32_t alignment, uint32_t size);
void vglFree(void *addr);
size_t vglMallocUsableSize(void *ptr);
void *vglGetTexDataPointer(GLenum target);
SceGxmTexture *vglGetGxmTexture(GLenum target);
void vglGetShaderBinary(GLuint shader, GLsizei bufSize, GLsizei *length, void *binary);
void vglPhycontMemLazyInit(void);
void vglTexImageDepthBuffer(GLenum target);
#ifdef __cplusplus
}
#endif
#endif
