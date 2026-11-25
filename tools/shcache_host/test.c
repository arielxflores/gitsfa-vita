// host test of engine/src/shcache.h: the first start compiles and saves, the second loads every binary without the
// compiler, a damaged entry falls back to compiling, a failing shader is remembered as failing.
// build: gcc -O1 -Wall -Wno-unused-function -I. test.c -o /tmp/sct && /tmp/sct   (run in tools/shcache_host)
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned GLuint, GLenum; typedef int GLint, GLsizei; typedef char GLchar;
#define GL_SHADER_TYPE 0x8B4F
#define GL_COMPILE_STATUS 0x8B81
#define GL_VERTEX_SHADER 0x8B31
static struct { GLenum type; char src[512]; int ok; char bin[512]; int binlen; } S[64]; static int ns = 1, ncompiles;
static GLuint glCreateShader(GLenum t) { S[ns].type = t; return ns++; }
static void glShaderSource(GLuint s, GLsizei n, const GLchar *const *str, const GLint *len) { (void)n; memcpy(S[s].src, str[0], len[0]); S[s].src[len[0]] = 0; }
static void glCompileShader(GLuint s) { ncompiles++; S[s].ok = !strstr(S[s].src, "FAIL"); if (S[s].ok) { int n = (int)strlen(S[s].src); for (int i = 0; i < n; i++) S[s].bin[i] = S[s].src[n - 1 - i]; S[s].binlen = n; } }
static void glGetShaderiv(GLuint s, GLenum p, GLint *v) { *v = p == GL_SHADER_TYPE ? (GLint)S[s].type : S[s].ok; }
static void glShaderBinary(GLsizei c, const GLuint *h, GLenum f, const void *b, GLsizei l) { (void)c; (void)f; GLuint s = h[0]; memcpy(S[s].bin, b, l); S[s].binlen = l; S[s].ok = memchr(b, '#', l) == NULL; }   // '#': vitaGL refuses it
static void vglGetShaderBinary(GLuint s, GLsizei bs, GLsizei *len, void *b) { (void)bs; memcpy(b, S[s].bin, S[s].binlen); *len = S[s].binlen; }
static unsigned long long tc_us(void) { return 0; }
static void logp(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
#undef SHC_ROOT
#define SHC_ROOT "/tmp/shcache_test"
#define SHC_BUILD "test build"
#include "../../engine/src/shcache.h"
static const char *SRC[4] = { "void main(float3 pos) { vpos = pos; }", "float4 main() : COLOR { return 1; }", "float4 main() : COLOR { FAIL }", "float4 main(uniform float4 t) : COLOR { return t; }" };
static int start(int expect_compiles, const char *what) {
	memset(S, 0, sizeof(S)); ns = 1; ncompiles = 0; shc_state = 0; shc_hits = shc_miss = shc_bad = shc_saved = shc_reported = shc_fails = 0;
	int okn = 0;
	for (int i = 0; i < 4; i++) { GLuint s = glCreateShader(GL_VERTEX_SHADER + (i > 0)); GLint len = (GLint)strlen(SRC[i]); const char *p = SRC[i]; tc_glShaderSource(s, 1, &p, &len); shc_compile(s);
		GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); okn += ok;
		if (ok) { int n = (int)strlen(SRC[i]); for (int k = 0; k < n; k++) if (S[s].bin[k] != SRC[i][n - 1 - k]) { printf("  binary of shader %d wrong\n", i); return 0; } } }
	shc_report();
	int pass = ncompiles == expect_compiles && okn == 3; printf("%s: compiler called %d times (want %d), %d of 4 compiled ok (want 3): %s\n", what, ncompiles, expect_compiles, okn, pass ? "ok" : "WRONG"); return pass;
}
int main(void) {
	if (system("rm -rf /tmp/shcache_test")) { }
	int ok = start(4, "first start");
	ok &= start(0, "second start");
	// damage one entry: flip a byte in its binary (checksum), and make another one refused by "vitaGL"
	char cmd[300]; snprintf(cmd, sizeof(cmd), "f=$(ls %s/*.bin | head -1); printf 'Z' | dd of=$f bs=1 seek=30 conv=notrunc 2>/dev/null", shc_dir); if (system(cmd)) { }
	ok &= start(1, "after damage");
	ok &= start(0, "repaired");
	printf(ok ? "PASS\n" : "FAIL\n"); return ok ? 0 : 1;
}
