// extra GL names the skill / model / fx headers use that the ui agent's recording stand-in does not define (no-ops here:
// the skills HUD test only records the HUD draws)
#pragma once
#ifndef GL_DST_COLOR
#define GL_DST_COLOR 0x0306
#endif
#ifndef GL_SRC_COLOR
#define GL_SRC_COLOR 0x0300
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_ACTIVE_UNIFORMS
#define GL_ACTIVE_UNIFORMS 0x8B86
#endif
#ifndef GL_BYTE
#define GL_BYTE 0x1400
#endif
#ifndef GL_UNSIGNED_SHORT
#define GL_UNSIGNED_SHORT 0x1403
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
static void glGenBuffers(int n, GLuint *b) { static GLuint nb = 5000; for (int i = 0; i < n; i++) b[i] = nb++; }
static void glBufferData(GLenum t, long n, const void *d, GLenum u) { (void)t; (void)n; (void)d; (void)u; }
