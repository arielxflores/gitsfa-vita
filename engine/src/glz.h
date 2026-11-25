// glz.h (perf b56): LZ4-compressed data files. The Vita's memory card reads ~10 MB/s (b54 LOADPROF: Ruined Tokyo's map change
// waited 2.0 s for 36 MB); the big map, collision, model and HUD files pack to about half (HUD packs to a fifth) and LZ4
// unpacks at well over 100 MB/s on one Cortex-A9 core, so a load reads half the bytes for a small CPU cost on the reader.
//
// Container (little endian), written by tools/glz/glz.c (the data builder's last step; tools/glz/pack.sh for build/):
//   char magic[4] = "GLZ1"; u32 raw_size; u32 block (raw bytes per block, GLZ_BLOCK);
//   then ceil(raw_size / block) blocks: u32 h; h & 0x7FFFFFFF bytes follow: an LZ4 block (lz4.org block format) when
//   bit 31 is clear, the bytes as they are (incompressible) when set. Every block but the last unpacks to exactly `block`.
// A split file (<name>.000, .001, ...: world.h's maps) is one container cut into pieces.
// Readers: ldbg.h's read-ahead unpacks on its reader thread; glz_fopen() gives a sequential FILE* for the rest (stdio
// fallbacks, the loader thread's collision). A file without the magic reads as before (old data keeps working).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define GLZ_BLOCK (256 * 1024)
#define GLZ_MAGIC "GLZ1"
#define GLZ_RAWBIT 0x80000000u
#define GLZ_ZMAX (GLZ_BLOCK + GLZ_BLOCK / 255 + 64)   // the largest stored block (LZ4's bound)

// LZ4 block -> dst (exactly dn bytes expected). Returns the bytes written, or -1 on any malformed input (never reads
// or writes outside src[0..sn) / dst[0..dn)).
static int glz_lz4_dec(const unsigned char *src, int sn, unsigned char *dst, int dn) {
	const unsigned char *ip = src, *iend = src + sn; unsigned char *op = dst, *oend = dst + dn;
	for (;;) {
		if (ip >= iend) return -1;
		unsigned tok = *ip++; size_t ll = tok >> 4;
		if (ll == 15) { unsigned b; do { if (ip >= iend) return -1; b = *ip++; ll += b; } while (b == 255); }
		if ((size_t)(iend - ip) < ll || (size_t)(oend - op) < ll) return -1;
		if (ll >= 16 && (size_t)(oend - op) >= ll + 16 && (size_t)(iend - ip) >= ll + 16) {   // wide copy (may write past ll: fine, overwritten next)
			const unsigned char *s = ip; unsigned char *d = op, *e = op + ll; do { memcpy(d, s, 16); d += 16; s += 16; } while (d < e);
		} else memcpy(op, ip, ll);
		ip += ll; op += ll;
		if (ip == iend) break;   // the last sequence has literals only
		if (iend - ip < 2) return -1;
		size_t off = (size_t)ip[0] | (size_t)ip[1] << 8; ip += 2;
		if (!off || off > (size_t)(op - dst)) return -1;
		size_t ml = tok & 15;
		if (ml == 15) { unsigned b; do { if (ip >= iend) return -1; b = *ip++; ml += b; } while (b == 255); }
		ml += 4; if ((size_t)(oend - op) < ml) return -1;
		const unsigned char *m = op - off;
		if (off >= 16 && (size_t)(oend - op) >= ml + 16) { unsigned char *d = op, *e = op + ml; do { memcpy(d, m, 16); d += 16; m += 16; } while (d < e); }
		else if (off >= 8 && (size_t)(oend - op) >= ml + 8) { unsigned char *d = op, *e = op + ml; do { memcpy(d, m, 8); d += 8; m += 8; } while (d < e); }
		else { for (size_t i = 0; i < ml; i++) op[i] = m[i]; }   // overlapping (run-length) matches byte by byte
		op += ml;
	}
	return (int)(op - dst);
}
static unsigned glz_u32(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24; }
// is this the start of a container? (12 header bytes) -> raw size and block size
static int glz_header(const unsigned char *h, unsigned *raw, unsigned *blk) {
	if (memcmp(h, GLZ_MAGIC, 4)) return 0;
	*raw = glz_u32(h + 4); *blk = glz_u32(h + 8);
	return *blk == GLZ_BLOCK;
}

#ifndef GLZ_DECODE_ONLY   // (tools/glz/glz.c: the decoder only)
// ---- a sequential FILE* over a file that may be a container, in one piece or in pieces (<path>.000 ...)
typedef struct {
	FILE *f; char base[176]; int piece;   // the raw source
	unsigned raw, left;                   // container: raw size, raw bytes not yet unpacked
	unsigned char *blk, *z; unsigned pos, len;
	int bad;
} GlzFile;
static size_t glz_src(GlzFile *g, void *d, size_t n) {   // raw bytes across pieces
	size_t got = 0;
	while (got < n && g->f) {
		size_t r = fread((char *)d + got, 1, n - got, g->f); got += r;
		if (got < n) { fclose(g->f); g->f = NULL; if (g->piece >= 0) { char nm[192]; snprintf(nm, sizeof(nm), "%s.%03d", g->base, ++g->piece); g->f = fopen(nm, "rb"); } }
	}
	return got;
}
static int glz_next(GlzFile *g) {   // unpack the next block into g->blk
	if (!g->left || g->bad) return 0;
	unsigned char h4[4]; if (glz_src(g, h4, 4) != 4) { g->bad = 1; return 0; }
	unsigned h = glz_u32(h4), cn = h & ~GLZ_RAWBIT, want = g->left < GLZ_BLOCK ? g->left : GLZ_BLOCK;
	if (cn > GLZ_ZMAX) { g->bad = 1; return 0; }
	if (h & GLZ_RAWBIT) { if (cn != want || glz_src(g, g->blk, cn) != cn) { g->bad = 1; return 0; } }
	else { if (glz_src(g, g->z, cn) != cn || glz_lz4_dec(g->z, (int)cn, g->blk, (int)want) != (int)want) { g->bad = 1; return 0; } }
	g->pos = 0; g->len = want; g->left -= want; return 1;
}
static size_t glz_gread(GlzFile *g, char *d, size_t n) {
	size_t got = 0;
	while (got < n) {
		if (g->pos == g->len && !glz_next(g)) break;
		size_t c = n - got; if (c > g->len - g->pos) c = g->len - g->pos;
		memcpy(d + got, g->blk + g->pos, c); g->pos += (unsigned)c; got += c;
	}
	return got;
}
static int glz_gclose(void *c) { GlzFile *g = (GlzFile *)c; if (g->f) fclose(g->f); free(g->blk); free(g->z); free(g); return 0; }
#if defined(_WIN32)
#define GLZ_TMPFILE 1   // (host tools on Windows: no fopencookie / funopen; the file is unpacked into a tmpfile)
#elif defined(HG_HOST) || !defined(__vita__)
#define GLZ_COOKIE 1
static ssize_t glz_ck_read(void *c, char *b, size_t n) { return (ssize_t)glz_gread((GlzFile *)c, b, n); }
static ssize_t glz_ck_src(void *c, char *b, size_t n) { return (ssize_t)glz_src((GlzFile *)c, b, n); }
#else
static int glz_ck_read(void *c, char *b, int n) { return n > 0 ? (int)glz_gread((GlzFile *)c, b, (size_t)n) : 0; }
static int glz_ck_src(void *c, char *b, int n) { return n > 0 ? (int)glz_src((GlzFile *)c, b, (size_t)n) : 0; }
#endif
// path, or its pieces when pieces != 0 (pieces first, as world.h). A container -> a sequential FILE* that unpacks it
// (reads only: no seeks); a plain single file -> plain fopen (seeks work); plain pieces -> a FILE* that joins them.
// *packed (optional) = 1 for a container.
static FILE *glz_fopen_ex(const char *path, int pieces, int *packed) {
	if (packed) *packed = 0;
	GlzFile *g = calloc(1, sizeof(GlzFile)); if (!g) return NULL;
	snprintf(g->base, sizeof(g->base), "%s", path); g->piece = -1;
	if (pieces) { char nm[192]; snprintf(nm, sizeof(nm), "%s.000", path); g->f = fopen(nm, "rb"); if (g->f) g->piece = 0; }
	if (!g->f) g->f = fopen(path, "rb");
	if (!g->f) { free(g); return NULL; }
	unsigned char h[12]; size_t hn = fread(h, 1, 12, g->f); unsigned raw = 0, blk = 0;
	int is = hn == 12 && glz_header(h, &raw, &blk);
	if (!is) {
		if (g->piece < 0) { fclose(g->f); free(g); return fopen(path, "rb"); }   // plain file: opened afresh (the caller may set its buffer, seek)
		fclose(g->f); char nm[192]; snprintf(nm, sizeof(nm), "%s.000", path); g->f = fopen(nm, "rb"); g->piece = 0;              // plain pieces, from the start
		if (!g->f) { free(g); return NULL; }
	} else { g->raw = g->left = raw; g->blk = malloc(GLZ_BLOCK + 16); g->z = malloc(GLZ_ZMAX + 16); if (!g->blk || !g->z) { glz_gclose(g); return NULL; } }
	FILE *f;
#ifdef GLZ_TMPFILE
	f = tmpfile();
	if (f) { char *b = malloc(1 << 16); size_t n; while (b && (n = is ? glz_gread(g, b, 1 << 16) : glz_src(g, b, 1 << 16)) > 0) fwrite(b, 1, n, f); free(b); rewind(f); }
	glz_gclose(g); if (packed) *packed = is; return f;
#elif defined(GLZ_COOKIE)
	cookie_io_functions_t io; memset(&io, 0, sizeof(io)); io.read = is ? glz_ck_read : glz_ck_src; io.close = glz_gclose; f = fopencookie(g, "rb", io);
#else
	f = funopen(g, is ? glz_ck_read : glz_ck_src, NULL, NULL, glz_gclose);
#endif
	if (!f) { glz_gclose(g); return NULL; }
	if (packed) *packed = is;
	setvbuf(f, NULL, _IOFBF, 16 * 1024);
	return f;
}
static FILE *glz_fopen(const char *path) { return glz_fopen_ex(path, 0, NULL); }
#endif
