// Textured, coloured quads in the world (blob shadows now; particles and effects use the same path).
#pragma once
#ifndef VGL_XFER_WAIT
#ifdef __vita__
#define VGL_XFER_WAIT() sceGxmTransferFinish()   // vitaGL uploads copy asynchronously: wait before the source buffer is reused
#else
#define VGL_XFER_WAIT() ((void)0)
#endif
#endif
#include <vitaGL.h>
#include <math.h>
#include <string.h>
static const char *SP_VS =
"void main(float3 pos, float2 uv, float4 col, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float4 out c0 : COLOR) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv; c0 = col; }";
static const char *SP_FS =
"float4 main(float2 t0 : TEXCOORD0, float4 c0 : COLOR, uniform sampler2D tex : TEXUNIT0) : COLOR { return tex2D(tex, t0) * c0; }";
#ifdef GRADE_HALF
static const char *SP_FS_H =   // render b39: the same in half precision, half colour varying (sprite_init tries it first; grade.h nohalf: off)
"float4 main(float2 t0 : TEXCOORD0, half4 c0 : COLOR, uniform sampler2D tex : TEXUNIT0) : COLOR { return (float4)((half4)tex2D(tex, t0) * c0); }";
static int sp_half;   // render b39: 1 = the sprite program is the half one
#endif
static GLuint sp_prog, sp_blob; static GLint sp_u_m[4]; static int sp_ok;
#define SP_MAXQ 256
static float sp_v[SP_MAXQ * 6 * 9]; static int sp_n;
typedef GLuint (*sp_mkshader_fn)(GLenum, const char *);
static int sprite_init(sp_mkshader_fn mk) {
	GLuint vs = mk(GL_VERTEX_SHADER, SP_VS), fs = 0; GLint ok = 0;
#ifdef GRADE_HALF
	if (grade_half()) { fs = mk(GL_FRAGMENT_SHADER, SP_FS_H); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); sp_half = ok; }   // render b39: half first
#endif
	if (!ok) fs = mk(GL_FRAGMENT_SHADER, SP_FS);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1; glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -1;
	sp_prog = glCreateProgram(); glAttachShader(sp_prog, vs); glAttachShader(sp_prog, fs);
	glBindAttribLocation(sp_prog, 0, "pos"); glBindAttribLocation(sp_prog, 1, "uv"); glBindAttribLocation(sp_prog, 2, "col");
	glLinkProgram(sp_prog); glGetProgramiv(sp_prog, GL_LINK_STATUS, &ok); if (!ok) return -2;
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; sp_u_m[r] = glGetUniformLocation(sp_prog, nm); }
	glUseProgram(sp_prog); glUniform1i(glGetUniformLocation(sp_prog, "tex"), 0);
	// soft round shadow: 64x64, alpha falls off towards the edge
	static unsigned char px[64 * 64 * 4];
	for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { float dx = (x + 0.5f) / 32 - 1, dy = (y + 0.5f) / 32 - 1, r = sqrtf(dx * dx + dy * dy);
		float a = r >= 1 ? 0 : (1 - r * r); a = a * a * (3 - 2 * a); unsigned char *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = 0; p[3] = (unsigned char)(a * 255); }
	glGenTextures(1, &sp_blob); glBindTexture(GL_TEXTURE_2D, sp_blob);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();
	sp_ok = 1; return 0;
}
static void sp_vert(const float *p, float u, float v, const float *c) { float *o = &sp_v[sp_n * 9]; o[0] = p[0]; o[1] = p[1]; o[2] = p[2]; o[3] = u; o[4] = v; memcpy(o + 5, c, 16); sp_n++; }
// a flat quad on the ground: centre, half sizes along a direction (yaw) and across it
static void sprite_ground(float x, float y, float z, float hx, float hy, float yaw, const float *col) {
	if (sp_n + 6 > SP_MAXQ * 6) return;
	float c = cosf(yaw), s = sinf(yaw), ax = c * hx, ay = s * hx, bx = -s * hy, by = c * hy;
	float p0[3] = { x - ax - bx, y - ay - by, z }, p1[3] = { x + ax - bx, y + ay - by, z }, p2[3] = { x + ax + bx, y + ay + by, z }, p3[3] = { x - ax + bx, y - ay + by, z };
	sp_vert(p0, 0, 0, col); sp_vert(p1, 1, 0, col); sp_vert(p2, 1, 1, col); sp_vert(p0, 0, 0, col); sp_vert(p2, 1, 1, col); sp_vert(p3, 0, 1, col);
}
static void sprite_flush(const float *MVP, GLuint tex, int additive) {
	if (!sp_n || !sp_ok) { sp_n = 0; return; }
	glUseProgram(sp_prog); for (int r = 0; r < 4; r++) { float row[4] = { MVP[r], MVP[4 + r], MVP[8 + r], MVP[12 + r] }; glUniform4fv(sp_u_m[r], 1, row); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, sp_v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, sp_v + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, sp_v + 5);
	glDrawArrays(GL_TRIANGLES, 0, sp_n);
	glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE); sp_n = 0;
}
// ---- generic quads and a third blend mode (used by fx.h)
// a quad with corners a (u0,v0), b (u1,v0), c (u1,v1), d (u0,v1)
static void sprite_quad(const float *a, const float *b, const float *c, const float *d, float u0, float v0, float u1, float v1, const float *col) {
	if (sp_n + 6 > SP_MAXQ * 6) return;
	sp_vert(a, u0, v0, col); sp_vert(b, u1, v0, col); sp_vert(c, u1, v1, col); sp_vert(a, u0, v0, col); sp_vert(c, u1, v1, col); sp_vert(d, u0, v1, col);
}
// b81 (fx.h particle trimming): a convex polygon, n corners with their uv, as a fan of n - 2 triangles
static void sprite_poly(int n, const float (*p)[3], const float (*uv)[2], const float *col) {
	if (n < 3 || sp_n + 3 * (n - 2) > SP_MAXQ * 6) return;
	for (int k = 1; k + 1 < n; k++) { sp_vert(p[0], uv[0][0], uv[0][1], col); sp_vert(p[k], uv[k][0], uv[k][1], col); sp_vert(p[k + 1], uv[k + 1][0], uv[k + 1][1], col); }
}
// mode 0: alpha blend, 1: additive, 2: multiply x2 (DST_COLOR * SRC_COLOR: mid-grey texels leave the surface unchanged)
static void sprite_flush_mode(const float *MVP, GLuint tex, int mode) {
	if (mode != 2) { sprite_flush(MVP, tex, mode); return; }
	if (!sp_n || !sp_ok) { sp_n = 0; return; }
	glUseProgram(sp_prog); for (int r = 0; r < 4; r++) { float row[4] = { MVP[r], MVP[4 + r], MVP[8 + r], MVP[12 + r] }; glUniform4fv(sp_u_m[r], 1, row); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_BLEND); glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, sp_v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, sp_v + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, sp_v + 5);
	glDrawArrays(GL_TRIANGLES, 0, sp_n);
	glDepthMask(GL_TRUE); glDisable(GL_BLEND); glEnable(GL_CULL_FACE); sp_n = 0;
}
