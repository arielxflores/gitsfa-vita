// glz (perf b56): packs data files into engine/src/glz.h's GLZ1 container (LZ4 blocks of 256 KB), and checks / unpacks them.
//   glz c IN OUT [level]   pack (level 1..9: match search effort, default 6); a file that would not shrink by 3 % is
//                          copied as it is (the engine reads both)
//   glz d IN OUT           unpack (any container; a plain file is copied)
//   glz t IN               test: unpack in memory, print sizes and the speed
// The LZ4 block format is lz4.org's; the decoder used here is the engine's own (glz.h). Build: cc -O2 -o glz glz.c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define GLZ_DECODE_ONLY
#include "../../engine/src/glz.h"

#define HBITS 16
#define WIN 65535
#define MINM 4
#define LASTLIT 5    // LZ4: the last 5 bytes are literals
#define MFLIMIT 12   // LZ4: a match may not start within the last 12 bytes

static unsigned rd32(const unsigned char *p) { unsigned v; memcpy(&v, p, 4); return v; }
static unsigned hsh(unsigned v) { return (v * 2654435761u) >> (32 - HBITS); }
static unsigned char *put_len(unsigned char *op, size_t n) { while (n >= 255) { *op++ = 255; n -= 255; } *op++ = (unsigned char)n; return op; }
static unsigned char *emit(unsigned char *op, const unsigned char *lit, size_t ll, size_t off, size_t ml) {
	unsigned char *tok = op++; unsigned t = 0;
	if (ll >= 15) { t = 15 << 4; op = put_len(op, ll - 15); } else t = (unsigned)ll << 4;
	memcpy(op, lit, ll); op += ll;
	if (!ml) { *tok = (unsigned char)t; return op; }
	*op++ = (unsigned char)off; *op++ = (unsigned char)(off >> 8);
	size_t m = ml - MINM; if (m >= 15) { t |= 15; op = put_len(op, m - 15); } else t |= (unsigned)m;
	*tok = (unsigned char)t; return op;
}
// one block (n <= 256 KB): returns the packed size, written to out (needs GLZ_ZMAX)
static int lz4_block(const unsigned char *in, int n, unsigned char *out, int level) {
	static int head[1 << HBITS]; static int chain[GLZ_BLOCK];
	for (int i = 0; i < (1 << HBITS); i++) head[i] = -1;
	int maxtry = 1 << (level < 1 ? 1 : level > 9 ? 9 : level);   // 2 .. 512 candidates
	unsigned char *op = out; int anchor = 0, i = 0, mlim = n - MFLIMIT, elim = n - LASTLIT;
	while (i < mlim) {
		unsigned h = hsh(rd32(in + i)); int best = 0, boff = 0, c = head[h], tries = maxtry;
		while (c >= 0 && i - c <= WIN && tries--) {
			if (rd32(in + c) == rd32(in + i)) { int l = 4; while (i + l < elim && in[c + l] == in[i + l]) l++; if (l > best) { best = l; boff = i - c; if (i + l >= elim) break; } }
			c = chain[c];
		}
		chain[i] = head[h]; head[h] = i;
		if (best < MINM) { i++; continue; }
		// lazy step: a longer match one byte later wins
		if (level >= 4 && i + 1 < mlim) {
			unsigned h2 = hsh(rd32(in + i + 1)); int c2 = head[h2], t2 = maxtry / 2, b2 = 0, o2 = 0;
			while (c2 >= 0 && i + 1 - c2 <= WIN && t2--) { if (rd32(in + c2) == rd32(in + i + 1)) { int l = 4; while (i + 1 + l < elim && in[c2 + l] == in[i + 1 + l]) l++; if (l > b2) { b2 = l; o2 = i + 1 - c2; } } c2 = chain[c2]; }
			if (b2 > best + 1) { chain[i + 1] = head[h2]; head[h2] = i + 1; i++; best = b2; boff = o2; }
		}
		op = emit(op, in + anchor, (size_t)(i - anchor), (size_t)boff, (size_t)best);
		for (int k = i + 1; k < i + best && k < mlim; k++) { unsigned hk = hsh(rd32(in + k)); chain[k] = head[hk]; head[hk] = k; }
		i += best; anchor = i;
	}
	op = emit(op, in + anchor, (size_t)(n - anchor), 0, 0);
	return (int)(op - out);
}
static unsigned char *slurp(const char *p, size_t *n) {
	FILE *f = fopen(p, "rb"); if (!f) { perror(p); exit(2); }
	fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, 0, SEEK_SET);
	unsigned char *d = malloc(s + 1); if (fread(d, 1, s, f) != (size_t)s) { perror(p); exit(2); } fclose(f); *n = (size_t)s; return d;
}
static void w32(FILE *f, unsigned v) { unsigned char b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static int unpack(const unsigned char *d, size_t n, unsigned char **out, size_t *on) {
	unsigned raw, blk; if (n < 12 || !glz_header(d, &raw, &blk)) return 0;
	unsigned char *o = malloc(raw + 16); size_t ip = 12, op = 0;
	while (op < raw) {
		if (ip + 4 > n) return -1; unsigned h = glz_u32(d + ip), cn = h & ~GLZ_RAWBIT, want = raw - op < blk ? raw - (unsigned)op : blk; ip += 4;
		if (ip + cn > n) return -1;
		if (h & GLZ_RAWBIT) { if (cn != want) return -1; memcpy(o + op, d + ip, cn); }
		else if (glz_lz4_dec(d + ip, (int)cn, o + op, (int)want) != (int)want) return -1;
		ip += cn; op += want;
	}
	if (ip != n) return -1;
	*out = o; *on = raw; return 1;
}
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: glz c IN OUT [level] | glz d IN OUT | glz t IN\n"); return 1; }
	size_t n; unsigned char *d = slurp(argv[2], &n);
	if (argv[1][0] == 'c') {
		if (argc < 4) return 1; int level = argc > 4 ? atoi(argv[4]) : 6;
		unsigned raw, blk; if (n >= 12 && glz_header(d, &raw, &blk)) { fprintf(stderr, "%s: already packed\n", argv[2]); return 3; }
		size_t nb = (n + GLZ_BLOCK - 1) / GLZ_BLOCK; unsigned char *z = malloc(GLZ_ZMAX + 64); size_t total = 12;
		char tmp[1024]; snprintf(tmp, sizeof(tmp), "%s.glztmp", argv[3]); FILE *f = fopen(tmp, "wb"); if (!f) { perror(tmp); return 2; }
		fwrite(GLZ_MAGIC, 1, 4, f); w32(f, (unsigned)n); w32(f, GLZ_BLOCK);
		for (size_t b = 0; b < nb; b++) {
			int bn = (int)(n - b * GLZ_BLOCK < GLZ_BLOCK ? n - b * GLZ_BLOCK : GLZ_BLOCK);
			int zn = lz4_block(d + b * GLZ_BLOCK, bn, z, level);
			unsigned char *chk = malloc(bn + 16); int ok = glz_lz4_dec(z, zn, chk, bn) == bn && !memcmp(chk, d + b * GLZ_BLOCK, bn); free(chk);
			if (!ok) { fprintf(stderr, "%s: block %zu does not round-trip\n", argv[2], b); return 4; }
			if (zn >= bn) { w32(f, (unsigned)bn | GLZ_RAWBIT); fwrite(d + b * GLZ_BLOCK, 1, bn, f); total += 4 + bn; }
			else { w32(f, (unsigned)zn); fwrite(z, 1, zn, f); total += 4 + zn; }
		}
		fclose(f);
		if (total > n * 0.97) { remove(tmp); FILE *o = fopen(argv[3], "wb"); fwrite(d, 1, n, o); fclose(o); printf("%s: %zu bytes, kept as it is (packed %zu)\n", argv[2], n, total); return 0; }
		if (rename(tmp, argv[3]) != 0) { remove(argv[3]); if (rename(tmp, argv[3]) != 0) { perror(argv[3]); return 2; } }
		printf("%s: %zu -> %zu bytes (%.2fx)\n", argv[2], n, total, (double)n / total); return 0;
	}
	if (argv[1][0] == 'd') {
		unsigned char *o; size_t on; int r = unpack(d, n, &o, &on); if (r < 0) { fprintf(stderr, "%s: damaged\n", argv[2]); return 4; }
		FILE *f = fopen(argv[3], "wb"); if (r) fwrite(o, 1, on, f); else fwrite(d, 1, n, f); fclose(f); return 0;
	}
	if (argv[1][0] == 't') {
		struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a); unsigned char *o; size_t on; int r = unpack(d, n, &o, &on); clock_gettime(CLOCK_MONOTONIC, &b);
		double s = (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9;
		if (r < 0) { printf("%s: DAMAGED\n", argv[2]); return 4; }
		if (!r) { printf("%s: plain (%zu bytes)\n", argv[2], n); return 0; }
		printf("%s: %zu -> %zu bytes, unpacked in %.1f ms (%.0f MB/s here)\n", argv[2], n, on, s * 1e3, on / s / 1e6); return 0;
	}
	return 1;
}
