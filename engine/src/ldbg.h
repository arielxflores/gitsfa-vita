// ldbg.h (perf b45): one helper thread for loading. ldbg_start(fn, ctx) runs fn(ctx) on a new thread on core 1 (where the
// jobs.h workers sit; they are idle while a map loads) and returns at once; ldbg_wait() waits for it. Used by main.c's
// load_level: collision, navigation and floor light (CPU and file reads only) load there while the main thread loads the
// world (textures, meshes: GL calls, which must stay on the main thread).
// Rules for fn, as for jobs.h job functions: no GL / vitaGL calls, no logp, no rand(); it may malloc / free and read files
// (newlib's malloc and stdio lock). Nothing else may touch what fn builds until ldbg_wait() returned.
// If the thread cannot be made, fn runs inline in ldbg_start (the old order).
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "glz.h"   // perf b56: packed data files
static void logp(const char *fmt, ...);   // main.c
typedef void (*ldbg_fn)(void *ctx);
static struct { SceUID th; ldbg_fn fn; void *ctx; uint64_t us; int threaded; } LDBG = { -1, 0, 0, 0, 0 };   // threaded: the last fn ran on the thread
static int ldbg_entry(SceSize args, void *argp) {
	(void)args; (void)argp;
	uint64_t t0 = sceKernelGetSystemTimeWide(); LDBG.fn(LDBG.ctx); LDBG.us = sceKernelGetSystemTimeWide() - t0;
	return 0;
}
static void ldbg_start(ldbg_fn fn, void *ctx) {
	LDBG.fn = fn; LDBG.ctx = ctx; LDBG.us = 0; LDBG.th = -1; LDBG.threaded = 0;
	SceUID th = sceKernelCreateThread("gitsfa_ldbg", ldbg_entry, 0x10000100, 0x40000, 0, SCE_KERNEL_CPU_MASK_USER_0 << 1, NULL);
#ifdef LDBG_INLINE   // (test builds: every job inline, the old order, to compare the state a load leaves)
	if (th >= 0) { sceKernelDeleteThread(th); th = -1; }
#endif
	ldp_tid_aux = th;   // (ldprof.h: its read-only files get the larger stdio buffer too)
	if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) { LDBG.th = th; LDBG.threaded = 1; return; }
	if (th >= 0) sceKernelDeleteThread(th);
	ldbg_entry(0, NULL);   // no thread: inline
}
static void ldbg_wait(void) {
	if (LDBG.th < 0) return;
	sceKernelWaitThreadEnd(LDBG.th, NULL, NULL); sceKernelDeleteThread(LDBG.th); LDBG.th = -1;
}

// ---- read-ahead (perf b45): one file (or its .000 .001 ... pieces, world.h's split maps) read by the reader thread ahead of
// the main thread, in LDRA_B blocks through a ring of LDRA_K (1 MB), so the card reads while the main thread parses and
// uploads what came before. ldra_open() returns 0 (or -1: no such file / no thread / switched off / the reader busy: the
// caller reads as before); ldra_read() as fread; ldra_close() stops the reader. The main thread's waits for a block count as
// ldprof.h's "read" phase.
// One reader thread for the whole run (MH3's dedicated reader; made at the first use, then it waits for the next file): the
// first b45 version made a thread per file, which cost more than it saved on small model files (Vita3K: start-up characters
// 724 -> 1100 ms, skills 9 -> 225 ms). One file at a time, from one thread (the first that asks: the main thread).
// ldra_off: main.c's "noreadahead" switch (A/B on the device: everything reads as before, through stdio).
#define LDRA_B (256 * 1024)
#define LDRA_K 4
typedef struct {
	SceUID s_free, s_full; volatile int stop; SceUID fd; int piece; char base[176];
	unsigned char *buf; int len[LDRA_K], wr_k, rd_k, pos, cur, eof, on;   // cur: bytes in block rd_k (-1: none taken yet)
	uint64_t r_us, bytes, cbytes;                                         // the reader's time in sceIoRead, bytes handed out, bytes read from the card
	int packed, bad;                                                      // perf b56: a GLZ1 container (glz.h); bad: a damaged one (reads stop there)
} LdRa;
static int ldra_off;
static struct { SceUID th, s_job, s_done; int owner, busy, failed; LdRa *volatile r; unsigned files; } LDRT = { -1, -1, -1, 0, 0, 0, NULL, 0 };
static int ldra_src(LdRa *r, unsigned char *b, int n) {   // raw bytes from the file (across its pieces): how many (0 at the end)
	int got = 0;
	while (got < n && r->fd >= 0) {
		uint64_t t0 = sceKernelGetSystemTimeWide(); int k = sceIoRead(r->fd, b + got, n - got); r->r_us += sceKernelGetSystemTimeWide() - t0;
		if (k > 0) { got += k; r->cbytes += k; continue; }
		sceIoClose(r->fd); r->fd = -1;   // end of this piece: the next one, if the file is in pieces
		if (r->piece >= 0) { char nm[192]; snprintf(nm, sizeof(nm), "%s.%03d", r->base, ++r->piece); r->fd = sceIoOpen(nm, SCE_O_RDONLY, 0); if (r->fd < 0) r->fd = -1; }
	}
	return got;
}
static void ldra_fill(LdRa *r) {   // the reader thread: one file, block after block, until its end or ldra_close's stop
	// perf b56: a GLZ1 container (glz.h) is unpacked here, block by block (its blocks are LDRA_B = GLZ_BLOCK raw bytes)
	unsigned char hd[12]; int hn = ldra_src(r, hd, 12), pend = hn; unsigned raw = 0, blk = 0, left = 0;
	int packed = hn == 12 && glz_header(hd, &raw, &blk) && blk == LDRA_B;
	unsigned char *z = NULL;
	if (packed) { pend = 0; left = raw; z = memalign(64, GLZ_ZMAX + 64); if (!z) { r->bad = 1; packed = 0; left = 0; } }
	for (;;) {
		if (sceKernelWaitSema(r->s_free, 1, NULL) < 0 || r->stop) break;
		unsigned char *b = r->buf + (size_t)r->wr_k * LDRA_B; int got = 0;
		if (packed) {
			if (left && !r->bad) {
				unsigned char h4[4]; unsigned want = left < LDRA_B ? left : LDRA_B;
				if (ldra_src(r, h4, 4) == 4) {
					unsigned h = glz_u32(h4), cn = h & ~GLZ_RAWBIT;
					if (cn <= GLZ_ZMAX && (h & GLZ_RAWBIT ? cn == want && ldra_src(r, b, (int)cn) == (int)cn
					                                      : ldra_src(r, z, (int)cn) == (int)cn && glz_lz4_dec(z, (int)cn, b, (int)want) == (int)want)) { got = (int)want; left -= want; }
					else r->bad = 1;
				} else r->bad = 1;
			}
		} else {
			if (pend) { memcpy(b, hd, pend); got = pend; pend = 0; }
			got += ldra_src(r, b + got, LDRA_B - got);
		}
		r->bytes += got; r->len[r->wr_k] = got; r->wr_k = (r->wr_k + 1) % LDRA_K;
		sceKernelSignalSema(r->s_full, 1);
		if (!got) break;   // (an empty block: the end)
	}
	free(z); r->packed = packed;
}
static int ldra_reader(SceSize args, void *argp) {
	(void)args; (void)argp;
	for (;;) {
		if (sceKernelWaitSema(LDRT.s_job, 1, NULL) < 0) break;
		LdRa *r = LDRT.r; if (r) ldra_fill(r);
		sceKernelSignalSema(LDRT.s_done, 1);   // (ldra_close waits for it: the reader no longer touches r)
	}
	return 0;
}
static int ldra_thread(void) {   // the reader thread, made once; core 2 (the audio mixer's, below its priority: the reader mostly waits for the card)
	if (LDRT.th >= 0) return 0;
	if (LDRT.failed) return -1;
	LDRT.s_job = sceKernelCreateSema("gitsfa_ra_job", 0, 0, 1, NULL); LDRT.s_done = sceKernelCreateSema("gitsfa_ra_done", 0, 0, 1, NULL);
	SceUID th = LDRT.s_job >= 0 && LDRT.s_done >= 0 ? sceKernelCreateThread("gitsfa_ra", ldra_reader, 0x10000100 + 20, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_0 << 2, NULL) : -1;
	if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) { LDRT.th = th; return 0; }
	if (th >= 0) sceKernelDeleteThread(th);
	if (LDRT.s_job >= 0) sceKernelDeleteSema(LDRT.s_job); if (LDRT.s_done >= 0) sceKernelDeleteSema(LDRT.s_done);
	LDRT.s_job = LDRT.s_done = -1; LDRT.failed = 1; return -1;
}
static int ldra_open_ex(LdRa *r, const char *path, int pieces) {   // pieces: try <path>.000 ... first, as world.h's w_open
	memset(r, 0, sizeof(*r)); r->s_free = r->s_full = -1; r->cur = -1; r->fd = -1; r->piece = -1;
	int tid = sceKernelGetThreadId(); if (!LDRT.owner) LDRT.owner = tid;
	if (ldra_off || LDRT.busy || tid != LDRT.owner || ldra_thread() < 0) return -1;
	char nm[192]; snprintf(r->base, sizeof(r->base), "%s", path);
	if (pieces) { snprintf(nm, sizeof(nm), "%s.000", path); r->piece = 0; r->fd = sceIoOpen(nm, SCE_O_RDONLY, 0); if (r->fd < 0) r->piece = -1; }
	if (r->fd < 0) r->fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (r->fd < 0) return -1;
	r->buf = memalign(64, (size_t)LDRA_B * LDRA_K);   // (MH3's disc access: 64-byte aligned buffers, large reads at block offsets)
	r->s_free = sceKernelCreateSema("gitsfa_ra_free", 0, LDRA_K, LDRA_K, NULL); r->s_full = sceKernelCreateSema("gitsfa_ra_full", 0, 0, LDRA_K, NULL);
	if (r->buf && r->s_free >= 0 && r->s_full >= 0) {
		LDRT.busy = 1; LDRT.r = r; LDRT.files++; r->on = 1;
		sceKernelSignalSema(LDRT.s_job, 1);
		return 0;
	}
	if (r->s_free >= 0) sceKernelDeleteSema(r->s_free); if (r->s_full >= 0) sceKernelDeleteSema(r->s_full);
	free(r->buf); sceIoClose(r->fd); memset(r, 0, sizeof(*r)); return -1;
}
static int ldra_open(LdRa *r, const char *path) { return ldra_open_ex(r, path, 1); }
static size_t ldra_read(LdRa *r, void *d, size_t n) {
	size_t got = 0;
	while (got < n && !r->eof) {
		if (r->cur < 0) {   // the next block
#ifdef LDP_PUSH
			LDP_PUSH(LDP_IO);
#endif
			sceKernelWaitSema(r->s_full, 1, NULL);
#ifdef LDP_PUSH
			LDP_POP();
#endif
			r->cur = r->len[r->rd_k]; r->pos = 0; if (!r->cur) { r->eof = 1; break; }
		}
		size_t c = n - got; if (c > (size_t)(r->cur - r->pos)) c = (size_t)(r->cur - r->pos);
		memcpy((char *)d + got, r->buf + (size_t)r->rd_k * LDRA_B + r->pos, c); r->pos += (int)c; got += c;
		if (r->pos == r->cur) { r->rd_k = (r->rd_k + 1) % LDRA_K; r->cur = -1; sceKernelSignalSema(r->s_free, 1); }
	}
	return got;
}
static void ldra_close(LdRa *r) {
	if (!r->on) return;
	r->stop = 1; sceKernelSignalSema(r->s_free, LDRA_K);   // (wakes a reader waiting for a free block)
	sceKernelWaitSema(LDRT.s_done, 1, NULL); LDRT.r = NULL; LDRT.busy = 0;   // the reader is done with this file
	if (r->fd >= 0) sceIoClose(r->fd);
	sceKernelDeleteSema(r->s_free); sceKernelDeleteSema(r->s_full); free(r->buf);
#ifdef LDP_PUSH
	if (LDP.on) { LDP.rbytes += r->cbytes; LDP.ra_files++; LDP.ra_bytes += r->cbytes; LDP.ra_us += r->r_us; if (r->packed) { LDP.ra_packed++; LDP.ra_raw += r->bytes; } }
#endif
	if (r->bad) logp("read-ahead: %s is a damaged packed file (read stopped at %u bytes)\n", r->base, (unsigned)r->bytes);
	r->on = 0;
}

// a read-ahead FILE for a loader that reads a whole file in order with fread (model.h's model_load): stdio on top of the
// reader thread's blocks (sequential reads only: no seeks). NULL when there is no such file or no thread: the caller
// then uses fopen as before.
static int ldra_ck_close(void *c) { ldra_close((LdRa *)c); free(c); return 0; }
#ifdef HG_HOST
static ssize_t ldra_ck_read(void *c, char *b, size_t n) { return (ssize_t)ldra_read((LdRa *)c, b, n); }
#else
static int ldra_ck_read(void *c, char *b, int n) { return n > 0 ? (int)ldra_read((LdRa *)c, b, (size_t)n) : 0; }
#endif
static FILE *ldra_fopen(const char *path) {
	LdRa *r = malloc(sizeof(LdRa)); if (!r) return NULL;
	if (ldra_open_ex(r, path, 0) < 0) { free(r); return NULL; }
#ifdef HG_HOST
	cookie_io_functions_t io = { ldra_ck_read, NULL, NULL, ldra_ck_close }; FILE *f = fopencookie(r, "rb", io);
#else
	FILE *f = funopen(r, ldra_ck_read, NULL, NULL, ldra_ck_close);
#endif
	if (!f) { ldra_close(r); free(r); return NULL; }
	setvbuf(f, NULL, _IOFBF, 16 * 1024);   // (small reads from stdio's buffer, big ones straight from the blocks)
	return f;
}
