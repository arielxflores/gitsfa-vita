// host test of engine/src/texcanary.h (+ frametime.h): every report kind once, and the double buffer delete is not passed on
// build: gcc -O1 -Wall -Wextra -I. test.c -o /tmp/tct && /tmp/tct   (run from tools/texcanary_host)
#include <stdio.h>
#include <stdarg.h>
static int nlines; static char last[512];
static void logp(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vsnprintf(last, sizeof(last), fmt, ap); va_end(ap); fputs(last, stdout); nlines++; }
#include "../../engine/src/texcanary.h"
#define DXT5 0x83F3
static unsigned char src5[4096 + 1024], rgba[32 * 32 * 4];
static void ticks(float *t, int n) { for (int f = 0; f < n; f++) { *t += 1 / 60.f; tc_tick(*t); } }
int main(void) {
	float t = 0; GLuint a, b, c, d, buf; int ok = 1;
	for (unsigned i = 0; i < sizeof(src5); i++) src5[i] = (unsigned char)(i * 7 + (i >> 8));
	for (unsigned i = 0; i < sizeof(rgba); i++) rgba[i] = (unsigned char)(i * 13);
	glGenTextures(1, &a); glBindTexture(GL_TEXTURE_2D, a);
	glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 64, 64, 0, 4096, src5); glCompressedTexImage2D(GL_TEXTURE_2D, 1, DXT5, 32, 32, 0, 1024, src5 + 4096); TC_NAME(a, "map tex 0");
	glGenTextures(1, &b); glBindTexture(GL_TEXTURE_2D, b); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	tc_srclog("test", &a, 1);
	printf("   source hash of tex a: %08x (python: see texsrc_check.py --selftest)\n", tc_t[a].src);
	TC_EVENT("map load test");
	ticks(&t, 200);
	int base = tc_anom; printf("-- 0: clean uploads and 200 frames: %d anomalies (want 0)\n", base); ok &= base == 0;
	printf("-- 1: a CPU write into texture a (level 1)\n"); memset(tc_t[a].data + 4096 + 100, 0x5a, 300); ticks(&t, 200); ok &= tc_anom == base + 1;
	printf("-- 2: a broken upload\n"); h_bad_upload = 1; glGenTextures(1, &d); glBindTexture(GL_TEXTURE_2D, d); glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 64, 64, 0, 4096, src5); ok &= tc_anom == base + 2;
	printf("-- 3: texture b's descriptor re-pointed behind vitaGL's back\n"); htx[b].g.data = tc_t[a].data; ticks(&t, 200); ok &= tc_anom == base + 3;
	printf("-- 4: a buffer deleted twice\n"); glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf); glBufferData(GL_ARRAY_BUFFER, 4096, NULL, GL_STATIC_DRAW);
	glDeleteBuffers(1, &buf); glDeleteBuffers(1, &buf); printf("   double frees passed to the GL: %d\n", h_double_free);
	printf("-- 5: a texture id deleted twice (stale), then bound\n"); glDeleteTextures(1, &b); glDeleteTextures(1, &b); glBindTexture(GL_TEXTURE_2D, b);
	printf("-- 6: the allocator hands out texture a's memory again\n"); h_force = tc_t[a].data + 512; glGenTextures(1, &c); glBindTexture(GL_TEXTURE_2D, c); glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 16, 16, 0, 256, NULL);
	printf("-- 7: a buffer allocated over texture a\n"); glGenBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf); h_force = tc_t[a].data + 100; glBufferData(GL_ARRAY_BUFFER, 2048, NULL, GL_STATIC_DRAW);
	printf("-- 8: map of a deleted buffer\n"); glDeleteBuffers(1, &buf); glBindBuffer(GL_ARRAY_BUFFER, buf); void *m = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY); printf("   mapped: %p\n", m);
	printf("-- 9: a texture placed in a freed vglAlloc block (note), then the block's old owner writes into it\n");
	unsigned char *arena = vglAlloc(1 << 16, VGL_MEM_VRAM); vglFree(arena); arena = malloc(1 << 16);   // (host: keep the memory)
	tc_a[tc_na - 1].p = arena;   // (host: the shim's malloc differs from the block's address; point the record at it)
	h_force = arena; GLuint e; glGenTextures(1, &e); glBindTexture(GL_TEXTURE_2D, e); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	for (int i = 0; i < 2048; i += 2) { short s = (short)(3000 * __builtin_sinf(i * 0.01f)); memcpy(arena + 1024 + i, &s, 2); }   // "audio" written by a leaked decoder
	ticks(&t, 200);
	printf("-- 10: hitch (frame timers)\n"); FT("game logic"); vglSwapBuffers(0); fflush(stdout); t += 1.5f; tc_tick(t);
	printf("-- 11: deleting texture a verifies it\n"); memset(tc_t[a].data + 50, 1, 600); glDeleteTextures(1, &a);
	printf("anomalies %d\n", tc_anom);
	ok &= tc_anom == 10 && h_double_free == 0 && !m;   // (a texture's second change only counts pages: one report per texture)
	printf(ok ? "PASS\n" : "FAIL\n");
	return ok ? 0 : 1;
}
