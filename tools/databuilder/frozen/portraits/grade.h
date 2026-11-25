// Colour grading: each map's own 16x16x16 colour-grading LUT (map.dat Fix_<map>\*_CG_*.tga, a 256x16 strip of
// 16 blue slices) applied at the end of the world and character shaders (keeps the 2x MSAA; no extra pass).
//
// Up to build 29 every LUT version failed on the Vita: tex2D(...).xyz is half3 for SceShaccCg, and lerp(half3, half3,
// float) is ambiguous ("could be lerp(half3, half3, half) or lerp(half3, half3, half3)"); the reads now go into float3
// variables first. Mixed half/float arguments to lerp/min/max/clamp must be avoided in every shader for the same reason.
// The grade is made in one of several ways, best first; grade_select() test-compiles each and keeps the first one the
// compiler accepts, and every program that uses it still falls back to the next way (finally: no grading) on its own:
//   GRADE_LUT2    the map's LUT: bilinear in red/green, lerp between the blue slices (2 dependent reads, ~10 ALU)
//   GRADE_POLY    no texture reads: g(x) = k9 + k0 x.r + k1 x.g + k2 x.b + k3 x.r^2 + ... + k8 x.b^3 (x = saturate(c)),
//                 a fit of the map's LUT weighted by the colours the map really shows (tools/grade_fit.py; ~1-2/255
//                 mean error, the same picture as the LUT in side-by-side host renders). 2 mul + 10 mad.
//   GRADE_LUT2F   the LUT in half precision (the reads stay half3, lerp(half3, half3, half))
//   GRADE_LUT1    one read, nearest blue slice
//   GRADE_AFFINE  k9 + k0 x.r + k1 x.g + k2 x.b (an affine fit; 4 mad)
//   GRADE_OFF     plain
// Shaders get the code from grade_decl() (extra fragment parameters) and grade_code() (reads float3 fc, defines
// float3 gc); after linking, a program calls grade_register() once and grade.h keeps its coefficients up to date.
// The .glut file: 12288 bytes of LUT, then optionally 'GFIT', u32 1, 10 x float4 POLY, 10 x float4 AFFINE.
#pragma once
#ifndef VGL_XFER_WAIT
#ifdef __vita__
#define VGL_XFER_WAIT() sceGxmTransferFinish()   // vitaGL uploads copy asynchronously: wait before the source buffer is reused
#else
#define VGL_XFER_WAIT() ((void)0)
#endif
#endif
#include <vitaGL.h>
#include <stdio.h>
#include <string.h>
// The build 26-28 LUT tail (still used by main.c's shader probe): grades the float3 expression src into dst.
#define GRADE_FN 1
#define GRADE_INL(src, dst) \
" float3 gq_ = saturate(" src ") * 15.0; float gb_ = min(floor(gq_.z), 14.0); float gf_ = gq_.z - gb_;" \
" float2 gu_ = float2((gq_.x + 0.5 + gb_ * 16.0) / 256.0, (gq_.y + 0.5) / 16.0);" \
" float3 ga_ = tex2D(lut, gu_).xyz; float3 gh_ = tex2D(lut, gu_ + float2(0.0625, 0.0)).xyz; float3 " dst " = lerp(ga_, gh_, gf_);"

enum { GRADE_LUT2, GRADE_POLY, GRADE_LUT2F, GRADE_LUT1, GRADE_AFFINE, GRADE_OFF, GRADE_NMODES };
static const char *const grade_mode_name[GRADE_NMODES] = { "LUT (2 reads)", "polynomial", "LUT (2 reads, half)", "LUT (1 read)", "affine", "off" };
static int grade_mode = GRADE_OFF;   // the best way the compiler accepted (grade_select)
static int grade_have;               // the current map has its own grade (else: identity)

#define GRADE_LUT_DECL ", uniform sampler2D lut : TEXUNIT2"
#define GRADE_K_DECL ", uniform float4 gk0, uniform float4 gk1, uniform float4 gk2, uniform float4 gk3, uniform float4 gk4, uniform float4 gk5," \
	" uniform float4 gk6, uniform float4 gk7, uniform float4 gk8, uniform float4 gk9"
static const char *grade_decl(int mode) {
	switch (mode) {
	case GRADE_POLY: return GRADE_K_DECL;
	case GRADE_AFFINE: return ", uniform float4 gk0, uniform float4 gk1, uniform float4 gk2, uniform float4 gk9";
	case GRADE_LUT2: case GRADE_LUT2F: case GRADE_LUT1: return GRADE_LUT_DECL;
	default: return "";
	}
}
static const char *grade_code(int mode) {
	switch (mode) {
	case GRADE_POLY: return
		" float3 gx = saturate(fc); float3 gy = gx * gx; float3 gz = gy * gx;"
		" float3 gc = gk9.xyz + gk0.xyz * gx.x + gk1.xyz * gx.y + gk2.xyz * gx.z + gk3.xyz * gy.x + gk4.xyz * gy.y + gk5.xyz * gy.z"
		" + gk6.xyz * gz.x + gk7.xyz * gz.y + gk8.xyz * gz.z;";
	case GRADE_AFFINE: return
		" float3 gx = saturate(fc); float3 gc = gk9.xyz + gk0.xyz * gx.x + gk1.xyz * gx.y + gk2.xyz * gx.z;";
	case GRADE_LUT2: return
		" float3 gq = saturate(fc) * 15.0; float gb = min(floor(gq.z), 14.0); float gf = gq.z - gb;"
		" float2 gu = float2((gq.x + 0.5 + gb * 16.0) / 256.0, (gq.y + 0.5) / 16.0);"
		" float3 ga = tex2D(lut, gu).xyz; float3 gh = tex2D(lut, gu + float2(0.0625, 0.0)).xyz; float3 gc = lerp(ga, gh, gf);";
	case GRADE_LUT2F: return
		" float3 gq = saturate(fc) * 14.999; float gf = frac(gq.z); float gb = gq.z - gf;"
		" float2 gu = float2((gq.x + 0.5 + gb * 16.0) / 256.0, (gq.y + 0.5) / 16.0);"
		" half3 ga = tex2D(lut, gu).xyz; half3 gh = tex2D(lut, gu + float2(0.0625, 0.0)).xyz; float3 gc = lerp(ga, gh, (half)gf);";
	case GRADE_LUT1: return
		" float3 gq = saturate(fc) * 15.0; float gb = floor(gq.z + 0.5);"
		" float3 gc = tex2D(lut, float2((gq.x + 0.5 + gb * 16.0) / 256.0, (gq.y + 0.5) / 16.0)).xyz;";   // (b29 probe 7: compiles)
	default: return " float3 gc = fc;";
	}
}
// fragment source = params + grade_decl + ") : COLOR {" + body + grade_code + ret; returns the length, -1 when it does not fit
static int grade_src(char *dst, int n, const char *params, const char *body, const char *ret, int mode) {
	int r = snprintf(dst, n, "%s%s) : COLOR {%s%s%s", params, grade_decl(mode), body, grade_code(mode), ret);
	return r < 0 || r >= n ? -1 : r;
}

// ---- coefficients: [0] POLY, [1] AFFINE; the identity until a map's fit is loaded
static float grade_k[2][10][4];
static GLuint grade_tex;
#define GRADE_MAXP 32
static struct { GLuint p; int mode; GLint loc[10]; } grade_reg[GRADE_MAXP]; static int grade_nreg;
static void grade_k_identity(void) {
	memset(grade_k, 0, sizeof(grade_k));
	for (int s = 0; s < 2; s++) for (int c = 0; c < 3; c++) grade_k[s][c][c] = 1;
}
static void grade_push(int i) {   // the current coefficients into one registered program (makes it current)
	glUseProgram(grade_reg[i].p);
	const float (*k)[4] = grade_k[grade_reg[i].mode == GRADE_POLY ? 0 : 1];
	for (int j = 0; j < 10; j++) if (grade_reg[i].loc[j] >= 0) glUniform4fv(grade_reg[i].loc[j], 1, k[j]);
}
static void grade_push_all(void) { for (int i = 0; i < grade_nreg; i++) grade_push(i); }
// after linking a program built with grade_decl/grade_code(mode)
static void grade_register(GLuint p, int mode) {
	if (mode == GRADE_OFF) return;
	glUseProgram(p);
	if (mode != GRADE_POLY && mode != GRADE_AFFINE) { GLint l = glGetUniformLocation(p, "lut"); if (l >= 0) glUniform1i(l, 2); return; }
	if (grade_nreg >= GRADE_MAXP) return;
	int i = grade_nreg++; grade_reg[i].p = p; grade_reg[i].mode = mode;
	for (int j = 0; j < 10; j++) { char nm[4] = { 'g', 'k', (char)('0' + j), 0 }; grade_reg[i].loc[j] = glGetUniformLocation(p, nm); }
	grade_push(i);
}

typedef GLuint (*grade_mkshader_fn)(GLenum, const char *);
// picks grade_mode: the first way whose test shader (a character-shader-like fragment program) compiles.
// force >= 0 skips the test (a setting, or a mode known from earlier runs); returns the mode.
#ifndef GRADE_FORCE_FILE
#define GRADE_FORCE_FILE "ux0:data/gitsfa/grade.txt"   // optional: one number, the grade mode to use (tests: 0 LUT, 1 polynomial, ... 5 off)
#endif
static int grade_select(grade_mkshader_fn mk, void (*log)(const char *, ...), int force) {
	grade_k_identity();
	if (force < 0) { FILE *f = fopen(GRADE_FORCE_FILE, "rb"); if (f) { if (fscanf(f, "%d", &force) != 1) force = -1; fclose(f); } }
	if (force >= 0 && force < GRADE_NMODES) { grade_mode = force; if (log) log("colour grading: %s (forced)\n", grade_mode_name[force]); return force; }
	static char src[1600];
	for (int m = 0; m < GRADE_OFF; m++) {
		if (grade_src(src, sizeof(src), "float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc",
			" float4 c = tex2D(dif, t0); float3 fc = lerp(c.rgb * sh * tint.rgb, fogc.rgb, fog * fogc.a);", " return float4(gc, c.a * tint.a); }", m) < 0) continue;
		GLuint s = mk(GL_FRAGMENT_SHADER, src); GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); glDeleteShader(s);
		if (log) log("colour grading test: %s %s\n", grade_mode_name[m], ok ? "ok" : "FAILED");
		if (ok) { grade_mode = m; return m; }
	}
	grade_mode = GRADE_OFF; return GRADE_OFF;
}

static void grade_upload(const unsigned char *rgb) {
	if (!grade_tex) glGenTextures(1, &grade_tex);
	glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, grade_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 16, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb); VGL_XFER_WAIT();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glActiveTexture(GL_TEXTURE0);
}
static int grade_uses_lut(void) { return grade_mode == GRADE_LUT2 || grade_mode == GRADE_LUT2F || grade_mode == GRADE_LUT1; }
// no grading (menus, results, maps without a LUT)
static void grade_identity(void) {
	if (grade_uses_lut()) {
		static unsigned char px[256 * 16 * 3];
		for (int y = 0; y < 16; y++) for (int x = 0; x < 256; x++) { unsigned char *p = px + (y * 256 + x) * 3; p[0] = (x % 16) * 17; p[1] = y * 17; p[2] = (x / 16) * 17; }
		grade_upload(px);
	}
	grade_k_identity(); grade_push_all(); grade_have = 0;
}
// <stem>.glut (tools/make_lut.py + tools/grade_fit.py); identity when the map has none. Returns 0 ok, <0 none.
static int grade_load(const char *path) {
	static unsigned char px[256 * 16 * 3]; FILE *f = fopen(path, "rb");
	if (!f) { grade_identity(); return -1; }
	int n = (int)fread(px, 1, sizeof(px), f);
	char mg[4] = { 0 }; unsigned ver = 0; static float k[2][10][4];
	int fit = fread(mg, 1, 4, f) == 4 && !memcmp(mg, "GFIT", 4) && fread(&ver, 4, 1, f) == 1 && ver == 1 && fread(k, sizeof(k), 1, f) == 1;
	fclose(f);
	if (n != (int)sizeof(px)) { grade_identity(); return -2; }
	if (grade_uses_lut()) grade_upload(px);
	else if (!fit) { grade_identity(); return -3; }   // the ALU ways need the fit
	if (fit) memcpy(grade_k, k, sizeof(k)); else grade_k_identity();
	grade_push_all(); grade_have = 1; return 0;
}
static void grade_bind(void) { if (grade_uses_lut()) { glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, grade_tex); glActiveTexture(GL_TEXTURE0); } }
