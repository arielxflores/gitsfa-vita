// shcache.h (render, b36): a cache of compiled shader binaries, so later starts skip SceShaccCg.
// Included by texcanary.h; every shader of the engine goes through main.c's mkshader -> glShaderSource + glCompileShader,
// which the wrappers below turn into:
//   glShaderSource   -> the source is kept (copied), not handed to vitaGL yet
//   glCompileShader  -> key = 64-bit hash of (shader type, source, this build's compile date/time);
//                       ux0:data/gitsfa/shadercache/<build>/<key>.bin present and intact (magic, key, length, checksum)
//                         -> glShaderBinary (vitaGL's own serialisation: vglGetShaderBinary / glShaderBinary) - no compiler;
//                       else, or if vitaGL does not accept it -> glShaderSource + glCompileShader as before, then the binary
//                         (vglGetShaderBinary) is written for the next start.
// A new build uses a new <build> folder (the old ones are emptied when the first binary of the new build is saved): a
// changed shader, compiler or vitaGL never reuses an old binary. ux0:data/gitsfa/noshadercache switches the cache off.
// shc_report() (from tc_tick, once): "shader cache: N loaded in X ms, M compiled in Y ms (saved S)" - the start-up's shader
// time both ways.
#pragma once
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifndef SHC_ROOT
#define SHC_ROOT "ux0:data/gitsfa/shadercache"
#endif
#define SHC_MAXH 1024              // shader handles tracked (vitaGL hands out small ids)
#define SHC_BUF (256 * 1024)       // the largest binary accepted (Cg programs here are a few KB)
static struct { char *src; int len; } shc_s[SHC_MAXH];
static int shc_state;              // 0 not decided, 1 on, -1 off
static int shc_hits, shc_miss, shc_bad, shc_saved, shc_reported, shc_fails; static unsigned long long shc_us_load, shc_us_comp;
static char shc_dir[96];
static unsigned char shc_buf[SHC_BUF];
static unsigned long long shc_hash64(unsigned long long h, const void *p, int n) {
	const unsigned char *b = (const unsigned char *)p; for (int i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h;
}
static unsigned shc_sum(const unsigned char *p, unsigned n) { unsigned h = 2166136261u; for (unsigned i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u; return h; }
#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
static int shc_read(const char *path, unsigned char *buf, int max) { SceUID f = sceIoOpen(path, SCE_O_RDONLY, 0); if (f < 0) return -1; int n = sceIoRead(f, buf, max); sceIoClose(f); return n; }
static int shc_write(const char *path, const void *a, int na, const void *b, int nb) {
	SceUID f = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777); if (f < 0) return -1;
	int ok = sceIoWrite(f, a, na) == na && sceIoWrite(f, b, nb) == nb; sceIoClose(f); return ok ? 0 : -1;
}
static int shc_exists(const char *path) { SceIoStat st; return sceIoGetstat(path, &st) >= 0; }
static void shc_mkdir(const char *p) { sceIoMkdir(p, 0777); }
static void shc_clean_old(void) {   // empty the folders of other builds (they can never match again)
	SceUID d = sceIoDopen(SHC_ROOT); if (d < 0) return;
	SceIoDirent e; char sub[320], fp[640];
	while (sceIoDread(d, &e) > 0) {
		if (e.d_name[0] == '.' || !SCE_S_ISDIR(e.d_stat.st_mode)) continue;
		snprintf(sub, sizeof(sub), SHC_ROOT "/%s", e.d_name); if (!strcmp(sub, shc_dir)) continue;
		SceUID d2 = sceIoDopen(sub); if (d2 < 0) continue;
		SceIoDirent e2; while (sceIoDread(d2, &e2) > 0) { if (e2.d_name[0] == '.') continue; snprintf(fp, sizeof(fp), "%s/%s", sub, e2.d_name); sceIoRemove(fp); }
		sceIoDclose(d2); sceIoRmdir(sub);
	}
	sceIoDclose(d);
}
#else   // host tests: plain stdio (the directory must exist; old builds are not cleaned)
static int shc_read(const char *path, unsigned char *buf, int max) { FILE *f = fopen(path, "rb"); if (!f) return -1; int n = (int)fread(buf, 1, max, f); fclose(f); return n; }
static int shc_write(const char *path, const void *a, int na, const void *b, int nb) {
	FILE *f = fopen(path, "wb"); if (!f) return -1; int ok = (int)fwrite(a, 1, na, f) == na && (int)fwrite(b, 1, nb, f) == nb; fclose(f); return ok ? 0 : -1;
}
static int shc_exists(const char *path) { FILE *f = fopen(path, "rb"); if (f) fclose(f); return f != NULL; }
static void shc_mkdir(const char *p) { char c[200]; snprintf(c, sizeof(c), "mkdir -p '%s'", p); if (system(c)) { } }
static void shc_clean_old(void) { }
#endif
#ifndef SHC_BUILD
#define SHC_BUILD __DATE__ " " __TIME__   // this eboot's compile time: every build gets its own cache
#endif
typedef struct { char magic[4]; unsigned len, sum; unsigned long long key; } ShcHdr;
// ---- render b45: the offline shader bank (tools/shbank). Every program the engine can bind, compiled at build time by the
// Vita's own compiler (SceShaccCg, in the emulator harness) and shipped in the VPK as app0:shbank.bin, so a start needs no
// compiler: no libshacccg.suprx, no first-boot compile. Looked up before the card cache; a miss (a source the bank does not
// hold) is logged and goes the old way (card cache, then the runtime compiler when it is there).
// Key: FNV-64 over 'V' / 'F' and the source text (no build string: an unchanged source keeps its entry across builds).
// File (little endian): "SHB1", u32 n, u32 0, u32 0; n x { u64 key, u32 offset, u32 length } sorted by key (length 0: the
// compiler rejected that source, so it fails here too); then the GXP binaries (vitaGL's shader binary = the GXP).
// ux0:data/gitsfa/noshaderbank: the bank is not read (A/B, compiler tests). Test-only switches (tools/shbank):
// shbank_enum appends every source this start compiles to ux0:data/gitsfa/shbank_src.bin (the host game makes the build's
// domain with it); shbank_gen + shbank_src.bin: compile that list, write ux0:data/gitsfa/shbank_gxp.bin and stop.
#ifndef SHB_PATH
#define SHB_PATH "app0:shbank.bin"
#endif
#define SHB_SRC "ux0:data/gitsfa/shbank_src.bin"
#define SHB_GXP "ux0:data/gitsfa/shbank_gxp.bin"
typedef struct { unsigned long long key; unsigned off, len; } ShbEnt;
static unsigned char shc_kind[SHC_MAXH];   // 'V' / 'F', from glCreateShader (shc_glCreateShader)
static ShbEnt *shb_ent; static int shb_n, shb_state, shb_enum; static unsigned shb_base;   // shb_state: 0 not opened, 1 open, -1 none / off
static int shb_hits, shb_miss, shb_fails, shb_bad, shb_nlogged; static unsigned long long shb_us;
static unsigned long long shb_key(int kind, const char *src, int len) { unsigned char k = (unsigned char)kind; return shc_hash64(shc_hash64(14695981039346656037ull, &k, 1), src, len); }
#ifdef __vita__
static SceUID shb_fd = -1;
static int shb_pread(void *buf, unsigned off, unsigned n) { if (sceIoLseek(shb_fd, off, SCE_SEEK_SET) != (SceOff)off) return -1; return sceIoRead(shb_fd, buf, n) == (int)n ? 0 : -1; }
static int shb_fopen(void) { shb_fd = sceIoOpen(SHB_PATH, SCE_O_RDONLY, 0); return shb_fd >= 0 ? 0 : -1; }
#else
static FILE *shb_f;
static int shb_pread(void *buf, unsigned off, unsigned n) { if (fseek(shb_f, (long)off, SEEK_SET)) return -1; return fread(buf, 1, n, shb_f) == n ? 0 : -1; }
static int shb_fopen(void) { shb_f = fopen(SHB_PATH, "rb"); return shb_f ? 0 : -1; }
#endif
static int shb_open(void) {
	if (shb_state) return shb_state;
	shb_state = -1; shb_enum = shc_exists("ux0:data/gitsfa/shbank_enum");
	if (shc_exists("ux0:data/gitsfa/noshaderbank") || shb_fopen() < 0) return shb_state;
	unsigned h[4]; if (shb_pread(h, 0, 16) < 0 || memcmp(h, "SHB1", 4) || h[1] == 0 || h[1] > 65536) return shb_state;
	shb_ent = (ShbEnt *)malloc(sizeof(ShbEnt) * h[1]); if (!shb_ent) return shb_state;
	if (shb_pread(shb_ent, 16, sizeof(ShbEnt) * h[1]) < 0) { free(shb_ent); shb_ent = NULL; return shb_state; }
	shb_n = (int)h[1]; shb_base = 16 + sizeof(ShbEnt) * h[1]; return shb_state = 1;
}
static const ShbEnt *shb_find(unsigned long long key) {
	int lo = 0, hi = shb_n - 1;
	while (lo <= hi) { int m = (lo + hi) / 2; if (shb_ent[m].key == key) return &shb_ent[m]; if (shb_ent[m].key < key) lo = m + 1; else hi = m - 1; }
	return NULL;
}
static void shb_enum_add(int kind, const char *src, int len) {   // test runs: the build's program domain (tools/shbank/enum.sh)
	FILE *f = fopen(SHB_SRC, "ab"); if (!f) return;
	unsigned char k = (unsigned char)kind; unsigned l = (unsigned)len; fwrite(&k, 1, 1, f); fwrite(&l, 4, 1, f); fwrite(src, 1, len, f); fclose(f);
}
// tools/shbank/gen.sh: compile every source of shbank_src.bin with the runtime compiler; returns 1 when it ran (the caller stops)
static int shb_generate(void (*log)(const char *, ...)) {
	if (!shc_exists("ux0:data/gitsfa/shbank_gen")) return 0;
	FILE *in = fopen(SHB_SRC, "rb"), *out = fopen(SHB_GXP, "wb"); int n = 0, nok = 0; unsigned long long t0 = tc_us();
	if (!in || !out) { if (log) log("shader bank generation: cannot open %s / %s\n", SHB_SRC, SHB_GXP); if (in) fclose(in); if (out) fclose(out); return 1; }
	static char src[65536]; unsigned char k; unsigned l;
	while (fread(&k, 1, 1, in) == 1 && fread(&l, 4, 1, in) == 1 && l < sizeof(src) && fread(src, 1, l, in) == l) {
		src[l] = 0; GLuint s = glCreateShader(k == 'V' ? GL_CG_VERTEX_SHADER_EXT : GL_CG_FRAGMENT_SHADER_EXT); const GLchar *sp = src; GLint sl = (GLint)l;
		glShaderSource(s, 1, &sp, &sl); glCompileShader(s); GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); GLsizei bl = 0;
		if (ok) vglGetShaderBinary(s, SHC_BUF, &bl, shc_buf);
		unsigned long long key = shb_key(k, src, (int)l); unsigned len = ok && bl > 0 && bl <= SHC_BUF ? (unsigned)bl : 0;
		fwrite(&key, 8, 1, out); fwrite(&len, 4, 1, out); if (len) fwrite(shc_buf, 1, len, out);
		glDeleteShader(s); n++; nok += len > 0;
		if (log && (n % 25 == 0 || !len)) log("shader bank generation: %d compiled (%d failed)%s\n", n, n - nok, len ? "" : " - this one failed");
	}
	fclose(in); fclose(out);
	if (log) log("shader bank generation: done, %d sources, %d compiled, %d rejected by the compiler, %u s\n", n, nok, n - nok, (unsigned)((tc_us() - t0) / 1000000));
	return 1;
}
static GLuint shc_glCreateShader(GLenum type) {   // (texcanary.h's macro) the kind of each shader, for the bank key
	GLuint s = glCreateShader(type); if (s < SHC_MAXH) shc_kind[s] = type == GL_CG_VERTEX_SHADER_EXT || type == GL_VERTEX_SHADER ? 'V' : 'F'; return s;
}
static void shc_decide(void) {
	if (shc_state) return;
	shc_state = shc_exists("ux0:data/gitsfa/noshadercache") ? -1 : 1;
	unsigned long long b = shc_hash64(14695981039346656037ull, SHC_BUILD, (int)strlen(SHC_BUILD));
	snprintf(shc_dir, sizeof(shc_dir), SHC_ROOT "/%08x", (unsigned)(b ^ (b >> 32)));
}
static void tc_glShaderSource(GLuint sh, GLsizei count, const GLchar *const *string, const GLint *length) {
	int n = 0; for (int i = 0; i < count; i++) n += length && length[i] >= 0 ? length[i] : (int)strlen(string[i]);
	if (sh >= SHC_MAXH) { glShaderSource(sh, count, string, length); return; }
	char *s = (char *)malloc(n + 1); if (!s) { glShaderSource(sh, count, string, length); return; }
	int o = 0; for (int i = 0; i < count; i++) { int li = length && length[i] >= 0 ? length[i] : (int)strlen(string[i]); memcpy(s + o, string[i], li); o += li; }
	s[n] = 0; free(shc_s[sh].src); shc_s[sh].src = s; shc_s[sh].len = n;
}
// 1 = compiled (or loaded); the caller checks GL_COMPILE_STATUS as before
static void shc_compile(GLuint sh) {
	if (sh >= SHC_MAXH || !shc_s[sh].src) { unsigned long long t0 = tc_us(); glCompileShader(sh); shc_us_comp += tc_us() - t0; return; }   // source went straight to vitaGL
	shc_decide();
	char *src = shc_s[sh].src; int len = shc_s[sh].len; shc_s[sh].src = NULL;
	GLint type = 0; glGetShaderiv(sh, GL_SHADER_TYPE, &type);
	unsigned long long key = shc_hash64(shc_hash64(shc_hash64(14695981039346656037ull, &type, 4), src, len), SHC_BUILD, (int)strlen(SHC_BUILD));
	char path[160]; snprintf(path, sizeof(path), "%s/%016llx.bin", shc_dir, key);
	unsigned long long t0 = tc_us();
	{   // render b45: the shader bank first
		int kind = shc_kind[sh] ? shc_kind[sh] : (type == GL_VERTEX_SHADER || type == GL_CG_VERTEX_SHADER_EXT ? 'V' : 'F');
		unsigned long long bk = shb_key(kind, src, len); shb_open();
		if (shb_enum) shb_enum_add(kind, src, len);
		if (shb_state > 0) {
			const ShbEnt *e = shb_find(bk);
			if (e && !e->len) { shb_hits++; shb_fails++; shb_us += tc_us() - t0; free(src); return; }   // rejected by the compiler at build time: fails here too
			if (e && e->len <= SHC_BUF && !shb_pread(shc_buf, shb_base + e->off, e->len)) {
				glShaderBinary(1, &sh, 0, shc_buf, (GLsizei)e->len);
				GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
				if (ok) { shb_hits++; shb_us += tc_us() - t0; free(src); return; }
			}
			if (e) shb_bad++; else shb_miss++;
			if (shb_nlogged++ < 8) logp("shader bank %s: %s shader %016llx, %d bytes: %.90s\n", e ? "entry refused" : "MISS", kind == 'V' ? "vertex" : "fragment", bk, len, src);
		}
	}
	if (shc_state > 0) {
		int n = shc_read(path, shc_buf, SHC_BUF);
		if (n >= (int)sizeof(ShcHdr)) {
			ShcHdr h; memcpy(&h, shc_buf, sizeof(h));
			if (!memcmp(h.magic, "SHCF", 4) && h.key == key && h.len == 0) { shc_hits++; shc_fails++; shc_us_load += tc_us() - t0; free(src); return; }   // failed before: stays failed (no compiler)
			if (!memcmp(h.magic, "SHC1", 4) && h.key == key && h.len == (unsigned)n - sizeof(h) && shc_sum(shc_buf + sizeof(h), h.len) == h.sum) {
				glShaderBinary(1, &sh, 0, shc_buf + sizeof(h), (GLsizei)h.len);
				GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
				if (ok) { shc_hits++; shc_us_load += tc_us() - t0; free(src); return; }
			}
			shc_bad++;   // damaged, stale or refused: compile it
		}
	}
	const GLchar *sp = src; GLint sl = len; glShaderSource(sh, 1, &sp, &sl); glCompileShader(sh); shc_miss++;
	GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	shc_us_comp += tc_us() - t0;
	if (!ok && shc_state > 0) {   // remembered as failed (the same source fails again; mkshader logs it)
		ShcHdr h; memset(&h, 0, sizeof(h)); memcpy(h.magic, "SHCF", 4); h.len = 0; h.sum = 0; h.key = key;   // (b37, perf: memset - the 4 padding bytes went to the card uninitialised; valgrind)
		if (!shc_saved) { shc_mkdir(SHC_ROOT); shc_mkdir(shc_dir); shc_clean_old(); }
		if (!shc_write(path, &h, sizeof(h), "", 0)) shc_saved++;
	}
	if (ok && shc_state > 0) {
		GLsizei bl = 0; vglGetShaderBinary(sh, SHC_BUF, &bl, shc_buf);
		if (bl > 0 && bl <= SHC_BUF) {
			if (!shc_saved) { shc_mkdir(SHC_ROOT); shc_mkdir(shc_dir); shc_clean_old(); }
			ShcHdr h; memset(&h, 0, sizeof(h)); memcpy(h.magic, "SHC1", 4); h.len = (unsigned)bl; h.sum = shc_sum(shc_buf, (unsigned)bl); h.key = key;
			if (!shc_write(path, &h, sizeof(h), shc_buf, bl)) shc_saved++;
		}
	}
	free(src);
}
#if defined(__vita__) && defined(__arm__)
extern unsigned char is_shark_online;   // vitaGL (custom_shaders.c): 1 when its start-up loaded SceShaccCg (libshacccg.suprx)
#define SHC_COMPILER (is_shark_online ? "present" : "ABSENT (the bank only)")
#else
#define SHC_COMPILER "host"
#endif
static void shc_report(void) {
	if (shc_reported || !(shc_hits + shc_miss + shb_hits)) return;
	shc_reported = 1;
	logp("shader bank (%s): %s, %d entries; %d loaded in %u ms (%d of them known failures), %d missed, %d refused; runtime compiler %s\n", SHB_PATH,   // render b45
		shb_state > 0 ? "open" : shc_exists("ux0:data/gitsfa/noshaderbank") ? "OFF (noshaderbank)" : "NOT FOUND", shb_n, shb_hits, (unsigned)(shb_us / 1000), shb_fails, shb_miss, shb_bad, SHC_COMPILER);
	logp("shader cache (%s): %d loaded in %u ms (%d of them known failures), %d compiled in %u ms (%d saved, %d damaged / stale entries)%s\n", shc_dir, shc_hits, (unsigned)(shc_us_load / 1000),
		shc_fails, shc_miss, (unsigned)(shc_us_comp / 1000), shc_saved, shc_bad, shc_state < 0 ? " - cache OFF (noshadercache)" : "");
}
