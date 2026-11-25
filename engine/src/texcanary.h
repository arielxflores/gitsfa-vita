// GPU-memory canary (render): finds what corrupts texture memory (b31/b32: colourful block noise on world textures, sky
// layers and HUD atlases, worse on maps loaded late in the tour). b34 version: decisive checks.
//
// Included by main.c right after <vitaGL.h>, so every later header's GL calls go through the wrappers below (the macros
// at the end): every texture and buffer the engine creates, uploads, binds, maps and deletes is recorded with the
// file:line that did it. Logged to boot.log as "TEXCANARY ..." lines (capped), each with the last engine events.
// The question it answers: do the texture's BYTES change, or does the GPU SAMPLE THE WRONG PLACE?
//   - BAD UPLOAD: right after an upload's transfer finished, the texture memory of that level does not hold the source
//     bytes (an order-independent checksum of 4-byte words, so vitaGL's swizzle does not matter; DXT levels >= 4x4 of
//     power-of-two textures, RGBA8 level 0 with a width multiple of 8). = the upload itself went wrong.
//   - CHANGED: the bytes changed AFTER the upload. Every 4 KB page of every texture (all levels) is hashed right after
//     its upload (16 x 64-byte lines per page: any damage over 256 bytes is seen) and re-checked in rotation (time
//     budget 0.3 ms a frame: every texture every few seconds), in full before every map load and when a texture is
//     deleted. The first changed page of a texture is dumped: texture, owner file:line, level, offset, the bytes as hex
//     and what they look like (text, floats, 16-bit audio, video luma, zeros), when the texture was last verified good,
//     the events since, and what else now lives at that address (live textures, buffers, vglAlloc / vglMemalign blocks,
//     live or freed). Then one line with how many pages of that texture changed.
//   - DESCRIPTOR: the SceGxmTexture the GPU samples (data pointer, format, size) differs from what the upload left,
//     without a GL call (re-pointed / overwritten descriptor). Textures re-pointed on purpose (sceGxmTextureInitLinear:
//     movie frames, the camo's screen texture) are skipped.
//   - MOVED: the texture's data pointer changed without a GL call (freed / re-made behind its owner).
//   - OVERLAP: a new texture's memory overlapping a live texture, a live buffer or a live vglAlloc / vglMemalign block
//     (memory handed out twice). A texture placed inside a FREED vglAlloc block (e.g. a movie's frame arena) is noted.
//   - STALE / DOUBLE DELETE, BIND of a deleted id, MAP of a deleted buffer, GEN of an id still live (as in b32).
//   - texsrc lines (world.h, one block per map): a hash of every world texture's source bytes as read from the .gmap;
//     tools/texcanary_host/texsrc_check.py compares them with the shipped files (a file read gone wrong).
// Frame timers (frametime.h, included below): a frame over 0.2 s is logged with what it spent its time on.
// tc_tick(seconds) once a frame (main.c, before the frame's draws).
#pragma once
#include <vitaGL.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static void logp(const char *fmt, ...);   // main.c
#ifndef LDP_PUSH   // perf (b45): ldprof.h's load-phase timers (the canary's own work is phase "canary", the DXT check "dxt check")
#define LDP_PUSH(id) ((void)0)
#define LDP_POP() ((void)0)
#endif
#ifdef LDP_FBUF   // perf (b45): ldbg.h's loader thread opens and closes files too: the event ring and frametime.h's I/O counters stay the main thread's
#define TC_MAIN_THREAD() (!ldp_tid_main || sceKernelGetThreadId() == ldp_tid_main)
#else
#define TC_MAIN_THREAD() 1
#endif

#define TC_MAXT 4096       // texture ids tracked (vitaGL hands out the lowest free id: the engine stays far below)
#define TC_MAXB 8192       // buffer table (open addressing on the vbo pointer)
#define TC_NEV 32          // engine events kept for the reports
#define TC_MAXLOG 90       // anomaly lines per run
#define TC_MAXHITCH 20     // render b39: HITCH lines per run (their own budget, not taken from TC_MAXLOG)
#define TC_PAGE 4096       // verification page
#define TC_MAXA 32         // vglAlloc / vglMemalign blocks remembered (live and freed)
#ifdef GITSFA_RELEASE
#define TC_VERIFY 0        // publishing build: bookkeeping only (no hashing, no upload checks, no rotation)
#else
// perf b56: the verification (page hashes at upload, a full pass before every map load, 0.3 ms a frame of rotation) cost
// 1.0-1.8 s of every load and the boot in test builds the user plays himself (b54 LOADPROF "canary"). The corruption it
// hunted was found in b37 (CORRUPTION.md). It now runs in scripted test runs (the autoplay switch) or with the switch file
// ux0:data/gitsfa/canary; otherwise test builds keep only the bookkeeping, as the publishing build does.
static int tc_verify_on = -1;
static int tc_verify(void) {
	if (tc_verify_on < 0) { FILE *f = fopen("ux0:data/gitsfa/canary", "rb"); if (!f) f = fopen("ux0:data/gitsfa/autoplay", "rb");
		tc_verify_on = f != NULL; if (f) fclose(f); logp("texcanary: verification %s\n", tc_verify_on ? "on (test run / canary switch)" : "off (no autoplay or canary switch)"); }
	return tc_verify_on;
}
#define TC_VERIFY tc_verify()
#endif
typedef struct {
	const char *file, *dfile; unsigned short line, dline;
	unsigned char live, bind_logged, kind, nocheck, repointed, reported, nlev, pad_;
	unsigned char *data; unsigned size, vsize;   // size: bytes uploaded (all levels); vsize: bytes verified from data
	unsigned short w, h; float t_up, t_good;
	unsigned *ph, npg;                           // page hashes
	unsigned nchg, chg_lo, chg_hi;               // pages changed in the current pass
	void *gdata; unsigned gfmt, gw, gh;          // the SceGxmTexture as the last upload left it
	unsigned src;                                // hash of the source bytes (all levels)
	char name[24];
} TcTex;
typedef struct { uintptr_t key; const char *file, *dfile; unsigned short line, dline; unsigned char live; } TcBuf;
typedef struct { unsigned char *p; unsigned n; const char *file; unsigned short line; unsigned char live, vram; float t0, t1; } TcBlk;
static TcTex tc_t[TC_MAXT]; static int tc_maxid;
static TcBuf tc_b[TC_MAXB]; static int tc_nbuf;
static TcBlk tc_a[TC_MAXA]; static int tc_na;
static struct { float t; int n; char s[80]; } tc_ev[TC_NEV]; static int tc_evh, tc_evn;
static float tc_now, tc_prev; static int tc_loaded, tc_nup, tc_nup_tick; static int tc_nlog, tc_anom, tc_checked, tc_nnote, tc_ndump; static float tc_last_sum;
static uintptr_t tc_bound_ab, tc_bound_eb;   // GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER bound through the wrappers
static int tc_vid = 1; static unsigned tc_vpg; static int tc_npass; static float tc_pass_t0, tc_pass_len;   // verification rotation
static unsigned long long tc_chk_us, tc_ver_us; static int tc_chk_n, tc_chk_bad, tc_chk_skip;   // upload checks (since the last map load)

static unsigned long long tc_us(void) {
#ifdef __vita__
	return sceKernelGetProcessTimeWide();
#else
	return 0;
#endif
}
static const char *tc_base(const char *f) { const char *s = f ? strrchr(f, '/') : NULL; return s ? s + 1 : f ? f : "?"; }
static void tc_verify_all(const char *when);
static void tc_event(const char *fmt, ...) {
	char s[80]; va_list ap; va_start(ap, fmt); vsnprintf(s, sizeof(s), fmt, ap); va_end(ap);
	if (!strncmp(s, "map load", 8)) {   // the next frame's long gap is the load, not a hitch; every texture verified first
		tc_loaded = 1; tc_verify_all(s); tc_chk_us = 0; tc_chk_n = tc_chk_bad = tc_chk_skip = 0; }
	int last = (tc_evh + TC_NEV - 1) % TC_NEV;
	if (tc_evn && !strcmp(tc_ev[last].s, s)) { tc_ev[last].n++; tc_ev[last].t = tc_now; return; }   // repeats: counted
	tc_ev[tc_evh].t = tc_now; tc_ev[tc_evh].n = 1; memcpy(tc_ev[tc_evh].s, s, sizeof(s)); tc_evh = (tc_evh + 1) % TC_NEV; if (tc_evn < TC_NEV) tc_evn++;
}
#define TC_EVENT(...) tc_event(__VA_ARGS__)
static void tc_events_since(float since, int maxn) {   // the recent events (oldest first), those at or after `since`
	int ne = 0, first = -1;
	for (int k = tc_evn - 1; k >= 0 && ne < maxn; k--) { int i = (tc_evh + TC_NEV - tc_evn + k) % TC_NEV; if (tc_ev[i].t < since) break; ne++; first = k; }
	logp("  events since t=%.1f:", since);
	if (!ne) logp(" (none recorded)");
	for (int k = first; ne > 0 && k < tc_evn; k++) { int i = (tc_evh + TC_NEV - tc_evn + k) % TC_NEV;
		if (tc_ev[i].n > 1) logp(" [%.1f %s x%d]", tc_ev[i].t, tc_ev[i].s, tc_ev[i].n); else logp(" [%.1f %s]", tc_ev[i].t, tc_ev[i].s); }
	logp("\n");
}
static int tc_report(const char *fmt, ...) {   // 1 = printed
	tc_anom++; if (tc_nlog >= TC_MAXLOG) return 0;
	tc_nlog++;
	char s[320]; va_list ap; va_start(ap, fmt); vsnprintf(s, sizeof(s), fmt, ap); va_end(ap);
	logp("TEXCANARY t=%.1f %s\n", tc_now, s);
	if (tc_nlog <= 50) {
		logp("  last events:");
		int ne = tc_evn < 10 ? tc_evn : 10;
		for (int k = 0; k < ne; k++) { int i = (tc_evh + TC_NEV - ne + k) % TC_NEV;
			if (tc_ev[i].n > 1) logp(" [%.1f %s x%d]", tc_ev[i].t, tc_ev[i].s, tc_ev[i].n); else logp(" [%.1f %s]", tc_ev[i].t, tc_ev[i].s); }
		logp("\n");
	}
	if (tc_nlog == TC_MAXLOG) logp("TEXCANARY: further reports suppressed\n");
	return 1;
}
static const char *tc_kname(int k) { return k == 1 ? "DXT1" : k == 3 ? "DXT3" : k == 5 ? "DXT5" : k == 4 ? "RGBA8" : "other"; }
static const char *tc_tname(int id) {
	static char b[2][112]; static int k; k ^= 1; const TcTex *T = &tc_t[id];
	snprintf(b[k], sizeof(b[k]), "%d (%s%s%s:%d %dx%d %s %uB)", id, T->name, T->name[0] ? ", " : "", tc_base(T->file), T->line, T->w, T->h, tc_kname(T->kind), T->size);
	return b[k];
}
// ---- hashes
static inline unsigned tc_rd(const unsigned char *p) { unsigned w; memcpy(&w, p, 4); return w; }
// source bytes (position dependent; tools/texcanary_host/texsrc_check.py computes the same)
static unsigned tc_srchash(const unsigned char *p, unsigned n) {
	unsigned h = 0, nw = n / 4;
	for (unsigned i = 0; i < nw; i++) h += (tc_rd(p + 4 * i) ^ (i * 0x9E3779B1u)) * (2u * i + 1u);
	return h ^ n;
}
// order-independent checksum of 4-byte words (a swizzled copy has the same words in another order)
static unsigned tc_bag(const unsigned char *p, unsigned n) {
	unsigned s = 0;
	for (unsigned i = 0; i + 4 <= n; i += 4) { unsigned h = (tc_rd(p + i) + 0x9E3779B9u) * 0x85EBCA77u; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16; s += h; }
	return s;
}
// one page: 16 lines of 64 bytes, one every 256 bytes
static unsigned tc_page(const unsigned char *p, unsigned n) {
	unsigned h = 2166136261u;
	for (unsigned o = 0; o < n; o += 256) { unsigned e = o + 64 < n ? o + 64 : n; for (unsigned i = o; i + 4 <= e; i += 4) h = (h ^ tc_rd(p + i)) * 16777619u; }
	return h;
}
// what bytes look like: zeros, text, floats, 16-bit audio, video luma (sampled like a page: 64 of every 256 bytes)
static void tc_describe(const unsigned char *p, unsigned n, char *o, int on) {
	static unsigned char b[1024]; unsigned nb = 0;
	for (unsigned off = 0; off < n && nb + 64 <= sizeof(b); off += 256) { unsigned e = off + 64 < n ? 64 : n - off; memcpy(b + nb, p + off, e); nb += e; }
	if (nb < 8) { snprintf(o, on, "(too short)"); return; }
	unsigned z = 0, txt = 0, luma = 0, fl = 0, nw = nb / 4, ns = nb / 2; double dl = 0, sa = 0, ds = 0;
	for (unsigned i = 0; i < nb; i++) { unsigned c = b[i]; z += !c; txt += (c >= 32 && c < 127) || c == 10 || c == 13 || c == 9; luma += c >= 16 && c <= 235; if (i) dl += abs((int)c - (int)b[i - 1]); }
	for (unsigned i = 0; i < nw; i++) { unsigned w = tc_rd(b + 4 * i), e = (w >> 23) & 255; fl += !w || (e >= 117 && e <= 143); }
	for (unsigned i = 0; i < ns; i++) { short s, q; memcpy(&s, b + 2 * i, 2); sa += abs(s); if (i) { memcpy(&q, b + 2 * i - 2, 2); ds += abs(s - q); } }
	sa /= ns; ds /= ns; dl /= nb;
	int k = snprintf(o, on, "zero %u%%, text %u%%, float-like %u%%, luma-range %u%% (mean step %.0f), s16 mean %.0f step %.0f",
		z * 100 / nb, txt * 100 / nb, fl * 100 / nw, luma * 100 / nb, dl, sa, ds);
	const char *g = z * 10 >= nb * 9 ? "mostly zeros" : txt * 10 >= nb * 9 ? "TEXT" : fl * 10 >= nw * 8 && z * 2 < nb ? "FLOATS (vertex / uniform data?)" :
		sa > 64 && ds < sa * 0.35 ? "16-BIT AUDIO-like (smooth s16)" : luma * 100 >= nb * 97 && dl < 10 ? "VIDEO LUMA-like (NV12 Y plane?)" : "texture-like / unrecognised";
	if (k < on) k += snprintf(o + k, on - k, " -> %s", g);
	if (txt * 10 >= nb * 7 && k + 60 < on) { k += snprintf(o + k, on - k, ": \""); for (unsigned i = 0; i < 48 && i < nb; i++) o[k++] = b[i] >= 32 && b[i] < 127 ? (char)b[i] : '.'; o[k++] = '"'; o[k] = 0; }
	if (fl * 10 >= nw * 8 && k + 80 < on) { float f[6]; memcpy(f, b, sizeof(f)); snprintf(o + k, on - k, ": %.3g %.3g %.3g %.3g %.3g %.3g", f[0], f[1], f[2], f[3], f[4], f[5]); }
}
// ---- buffers
// perf (b38): the table kept every buffer name it had seen (deleted ones too, for the double-delete check), so after ~6000
// names (vitaGL's names are its vbo structs' heap addresses: QA's soak, ~30 map loads) it was full: new buffers went
// untracked, their deletes were skipped as "never made" (each one leaked its memory: VRAM free fell to 0 in the soak's
// cycle 3) and their maps refused. Now a full table is rebuilt from its live entries (a delete of a name that was dropped
// is still refused, reported as never made).
static TcBuf *tc_bslot(uintptr_t key, int add);
static void tc_bcompact(void) {
	int n = 0; for (int k = 0; k < TC_MAXB; k++) n += tc_b[k].key && tc_b[k].live;
	TcBuf *keep = (TcBuf *)malloc((n ? n : 1) * sizeof(TcBuf)); if (!keep) return;
	n = 0; for (int k = 0; k < TC_MAXB; k++) if (tc_b[k].key && tc_b[k].live) keep[n++] = tc_b[k];
	int was = tc_nbuf; memset(tc_b, 0, sizeof(tc_b)); tc_nbuf = 0;
	for (int i = 0; i < n; i++) { TcBuf *B = tc_bslot(keep[i].key, 1); if (B) *B = keep[i]; }
	free(keep); logp("texcanary note t=%.1f: buffer table rebuilt, %d names -> %d live\n", tc_now, was, tc_nbuf);
}
static TcBuf *tc_bslot(uintptr_t key, int add) {
	for (int pass = 0; pass < 2; pass++) {
		unsigned h = (unsigned)((key >> 3) * 2654435761u) & (TC_MAXB - 1);
		for (int k = 0; k < TC_MAXB; k++, h = (h + 1) & (TC_MAXB - 1)) {
			if (tc_b[h].key == key) return &tc_b[h];
			if (!tc_b[h].key) { if (!add) return NULL; if (tc_nbuf > TC_MAXB * 3 / 4) break; tc_b[h].key = key; tc_nbuf++; return &tc_b[h]; }
		}
		if (!add || pass) return NULL;
		tc_bcompact();   // (full: drop the deleted names, then look again)
	}
	return NULL;
}
typedef struct { void *ptr; int32_t size; } TcVbo;   // the head of vitaGL's vbo struct (shared.h): ptr, size
static void tc_vbo_range(uintptr_t id, unsigned char **p, unsigned *n) { const TcVbo *v = (const TcVbo *)id; *p = (unsigned char *)v->ptr; *n = v->size > 0 ? (unsigned)v->size : 0; }
// a new memory range [p, p+n) vs every live texture (owner = what is being checked)
static void tc_overlap_tex(const unsigned char *p, unsigned n, int self, const char *what) {
	if (!p || !n) return;
	for (int i = 1; i <= tc_maxid; i++) { const TcTex *T = &tc_t[i]; if (i == self || !T->live || !T->data || !T->size) continue;
		if (p < T->data + T->size && T->data < p + n) { tc_report("OVERLAP: %s [%p +%u] overlaps live texture %s [%p +%u]", what, p, n, tc_tname(i), T->data, T->size); return; } }
}
static void tc_overlap_bufs(int id) {   // a texture's memory vs every live buffer
	const TcTex *T = &tc_t[id]; if (!T->data || !T->size) return;
	for (int k = 0; k < TC_MAXB; k++) { const TcBuf *B = &tc_b[k]; if (!B->key || !B->live) continue;
		unsigned char *p; unsigned n; tc_vbo_range(B->key, &p, &n);
		if (p && n && p < T->data + T->size && T->data < p + n) { tc_report("OVERLAP: texture %s [%p +%u] overlaps live buffer %08x (%s:%d) [%p +%u]", tc_tname(id), T->data, T->size, (unsigned)B->key, tc_base(B->file), B->line, p, n); return; } }
}
static void tc_overlap_blocks(int id) {   // a texture's memory vs vglAlloc / vglMemalign blocks (live: anomaly; freed: note)
	const TcTex *T = &tc_t[id]; if (!T->data || !T->size) return;
	for (int k = 0; k < tc_na; k++) { const TcBlk *A = &tc_a[k]; if (!A->p || !(A->p < T->data + T->size && T->data < A->p + A->n)) continue;
		if (A->live) tc_report("OVERLAP: texture %s [%p +%u] lies in the LIVE raw block of %s:%d [%p +%u] (allocated t=%.1f)", tc_tname(id), T->data, T->size, tc_base(A->file), A->line, A->p, A->n, A->t0);
		else if (tc_nnote < 40) { tc_nnote++; logp("texcanary note t=%.1f: texture %s was placed in memory that was the raw block of %s:%d [%p +%u] (t=%.1f..%.1f, freed)\n", tc_now, tc_tname(id), tc_base(A->file), A->line, A->p, A->n, A->t0, A->t1); }
		return; }
}
// what else lives at [p, p+n) now (for the CHANGED / DESCRIPTOR dumps)
static void tc_owners(const unsigned char *p, unsigned n, int self) {
	int found = 0; logp("  this memory [%p +%u] also belongs to:", p, n);
	for (int i = 1; i <= tc_maxid; i++) { const TcTex *T = &tc_t[i]; if (i == self || !T->live || !T->data) continue; unsigned sz = T->vsize > T->size ? T->vsize : T->size;
		if (p < T->data + sz && T->data < p + n) { logp(" [live texture %s]", tc_tname(i)); found++; } }
	for (int k = 0; k < TC_MAXB; k++) { const TcBuf *B = &tc_b[k]; if (!B->key || !B->live) continue;
		unsigned char *q; unsigned m; tc_vbo_range(B->key, &q, &m);
		if (q && m && p < q + m && q < p + n) { logp(" [live buffer %08x %s:%d +%u]", (unsigned)B->key, tc_base(B->file), B->line, m); found++; } }
	for (int k = 0; k < tc_na; k++) { const TcBlk *A = &tc_a[k];
		if (A->p && p < A->p + A->n && A->p < p + n) { logp(" [%s raw block %s:%d [%p +%u] allocated t=%.1f]", A->live ? "LIVE" : "FREED", tc_base(A->file), A->line, A->p, A->n, A->t0); found++; } }
	logp(found ? "\n" : " nothing the canary tracks (vitaGL-internal memory: circular pool, uniforms, programs, render targets)\n");
}
static void tc_glGenBuffers(GLsizei n, GLuint *ids, const char *f, int l) {
	glGenBuffers(n, ids);
	for (int i = 0; i < n; i++) { TcBuf *B = tc_bslot(ids[i], 1); if (!B) continue; B->live = 1; B->file = f; B->line = (unsigned short)l; }
}
static void tc_glDeleteBuffers(GLsizei n, const GLuint *ids, const char *f, int l) {
	for (int i = 0; i < n; i++) { if (!ids[i]) continue;
		TcBuf *B = tc_bslot(ids[i], 0);
		if (!B || !B->live) { tc_report("STALE BUFFER DELETE: buffer %08x deleted at %s:%d %s", (unsigned)ids[i], tc_base(f), l, B ? "(deleted before)" : "(never made: garbage id)");
			if (B) logp("  (it was made at %s:%d and deleted at %s:%d) -- NOT deleted again (vitaGL would free its memory twice)\n", tc_base(B->file), B->line, tc_base(B->dfile), B->dline);
			continue; }   // skipping the second free keeps vitaGL's heap intact
		if (strcmp(tc_base(B->file), tc_base(f))) tc_event("buf %08x of %s:%d deleted at %s:%d", (unsigned)ids[i], tc_base(B->file), B->line, tc_base(f), l);
		B->live = 0; B->dfile = f; B->dline = (unsigned short)l;
		if ((uintptr_t)ids[i] == tc_bound_ab) tc_bound_ab = 0;
		if ((uintptr_t)ids[i] == tc_bound_eb) tc_bound_eb = 0;
		glDeleteBuffers(1, &ids[i]);
	}
	tc_event("delete %d buffers %s:%d", (int)n, tc_base(f), l);
}
static void tc_glBindBuffer(GLenum target, GLuint id) {
	glBindBuffer(target, id);
	if (target == GL_ARRAY_BUFFER) tc_bound_ab = id; else if (target == GL_ELEMENT_ARRAY_BUFFER) tc_bound_eb = id;
}
static void tc_glBufferData(GLenum target, GLsizei size, const GLvoid *data, GLenum usage, const char *f, int l) {
	glBufferData(target, size, data, usage);
	uintptr_t id = target == GL_ARRAY_BUFFER ? tc_bound_ab : target == GL_ELEMENT_ARRAY_BUFFER ? tc_bound_eb : 0;
	if (!id || size < 1024) return;
	TcBuf *B = tc_bslot(id, 0); if (!B || !B->live) { tc_report("BUFFER DATA into a buffer that is not live (%08x) at %s:%d", (unsigned)id, tc_base(f), l); return; }
	LDP_PUSH(LDP_CANARY); unsigned char *p; unsigned n; tc_vbo_range(id, &p, &n); char w[64]; snprintf(w, sizeof(w), "buffer %08x (%s:%d)", (unsigned)id, tc_base(f), l); tc_overlap_tex(p, n, 0, w); LDP_POP();   // perf (b45)
}
static void *tc_glMapBuffer(GLenum target, GLbitfield access, const char *f, int l) {
	uintptr_t id = target == GL_ARRAY_BUFFER ? tc_bound_ab : target == GL_ELEMENT_ARRAY_BUFFER ? tc_bound_eb : 0;
	if (id) { TcBuf *B = tc_bslot(id, 0); if (!B || !B->live) { tc_report("MAP of a buffer that is not live (%08x) at %s:%d: NOT mapped", (unsigned)id, tc_base(f), l); return NULL; } }
	return glMapBuffer(target, access);
}
// ---- textures: verification
static void tc_hash_pages(TcTex *T, unsigned from) {   // (re)hash the pages from byte `from` to vsize
	unsigned np = (T->vsize + TC_PAGE - 1) / TC_PAGE; if (!np || !T->data) return;
	if (np > T->npg || !T->ph) { unsigned *q = (unsigned *)realloc(T->ph, np * sizeof(unsigned)); if (!q) { T->nocheck = 1; return; } T->ph = q; }
	T->npg = np;
	for (unsigned k = from / TC_PAGE; k < np; k++) { unsigned o = k * TC_PAGE, n = T->vsize - o < TC_PAGE ? T->vsize - o : TC_PAGE; T->ph[k] = tc_page(T->data + o, n); }
}
static int tc_level_at(const TcTex *T, unsigned off) {   // which mip level holds byte `off` (DXT chains)
	if (T->kind == 4 || !T->kind) return 0;
	unsigned w = T->w, h = T->h, o = 0, bs = T->kind == 1 ? 8 : 16;
	for (int k = 0; k < 16; k++) { unsigned s = ((w + 3) / 4) * ((h + 3) / 4) * bs; if (off < o + s) return k; o += s; if (w > 1) w /= 2; if (h > 1) h /= 2; }
	return -1;
}
static void tc_page_changed(int id, unsigned k, unsigned hnew, const char *when) {
	TcTex *T = &tc_t[id]; unsigned o = k * TC_PAGE, n = T->vsize - o < TC_PAGE ? T->vsize - o : TC_PAGE;
	if (!T->nchg) T->chg_lo = o;
	T->chg_hi = o + n; T->nchg++; T->ph[k] = hnew;
	if (T->reported) return;
	T->reported = 1;
	if (!tc_report("CHANGED: texture %s: its bytes changed after the upload (found %s): level %d, offset +0x%X (page %u of %u), address %p; last verified good at t=%.1f, uploaded at t=%.1f",
		tc_tname(id), when, tc_level_at(T, o), o, k, T->npg, T->data + o, T->t_good, T->t_up)) return;
	if (tc_ndump++ >= 12) return;
	char d[400]; tc_describe(T->data + o, n, d, sizeof(d)); logp("  the page now: %s\n", d);
	logp("  first 64 bytes:");
	for (unsigned i = 0; i < 64 && i < n; i++) logp("%s%02x", i % 16 ? " " : (i ? " | " : " "), T->data[o + i]);
	logp("\n");
	tc_owners(T->data + o, n, id);
	tc_events_since(T->t_good, 20);
}
static void tc_verify_done(int id) {   // a texture's pass finished: how many pages changed
	TcTex *T = &tc_t[id];
	if (T->nchg && tc_nlog < TC_MAXLOG) { tc_nlog++; logp("TEXCANARY t=%.1f   texture %s: %u of %u pages changed in this pass (offsets +0x%X..+0x%X of %u bytes)\n", tc_now, tc_tname(id), T->nchg, T->npg, T->chg_lo, T->chg_hi, T->vsize); }
	if (!T->nchg) T->t_good = tc_now;
	T->nchg = 0;
}
static void tc_verify_tex(int id, const char *when) {   // the whole texture now
	TcTex *T = &tc_t[id]; if (!T->live || T->nocheck || T->repointed || !T->ph || !T->data) return;
	for (unsigned k = 0; k < T->npg; k++) { unsigned o = k * TC_PAGE, n = T->vsize - o < TC_PAGE ? T->vsize - o : TC_PAGE; unsigned h = tc_page(T->data + o, n); if (h != T->ph[k]) tc_page_changed(id, k, h, when); }
	tc_verify_done(id);
}
static void tc_verify_all(const char *when) {
	unsigned long long t0 = tc_us(); int n = 0; LDP_PUSH(LDP_CANARY);   // perf (b45)
	for (int i = 1; i <= tc_maxid; i++) if (tc_t[i].live && tc_t[i].ph && !tc_t[i].nocheck && !tc_t[i].repointed) { tc_verify_tex(i, when); n++; }
	LDP_POP();
	tc_ver_us += tc_us() - t0; tc_vpg = 0;
	if (n) logp("texcanary: verified %d textures in full (before the %s) in %u ms\n", n, when, (unsigned)((tc_us() - t0) / 1000));
}
// ---- textures: GL wrappers
static int tc_bound_tex(void) { GLint id = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &id); return id; }
static void tc_glGenTextures(GLsizei n, GLuint *ids, const char *f, int l) {
	glGenTextures(n, ids);
	for (int i = 0; i < n; i++) { GLuint id = ids[i]; if (!id || id >= TC_MAXT) continue;
		TcTex *T = &tc_t[id];
		if (T->live) tc_report("GEN of texture id %u that is still live (%s:%d) at %s:%d (it was deleted behind our back)", id, tc_base(T->file), T->line, tc_base(f), l);
		free(T->ph); memset(T, 0, sizeof(*T)); T->live = 1; T->file = f; T->line = (unsigned short)l; if ((int)id > tc_maxid) tc_maxid = id; }
}
static void tc_glDeleteTextures(GLsizei n, const GLuint *ids, const char *f, int l) {
	for (int i = 0; i < n; i++) { GLuint id = ids[i]; if (!id) continue;
		if (id < TC_MAXT && !tc_t[id].live) {
			tc_report("STALE TEXTURE DELETE: id %u deleted at %s:%d (%s) -- skipped", id, tc_base(f), l,
				tc_t[id].file ? "made and deleted before" : "never made: garbage id");
			if (tc_t[id].file) logp("  (made at %s:%d, deleted at %s:%d)\n", tc_base(tc_t[id].file), tc_t[id].line, tc_base(tc_t[id].dfile), tc_t[id].dline);
			continue; }
		if (id < TC_MAXT) { TcTex *T = &tc_t[id];
			if (strcmp(tc_base(T->file), tc_base(f)) && tc_nnote < 40) { tc_nnote++; logp("texcanary note t=%.1f: texture %s deleted at %s:%d (made elsewhere)\n", tc_now, tc_tname(id), tc_base(f), l); }
			{ unsigned long long t0 = tc_us(); LDP_PUSH(LDP_CANARY); tc_verify_tex(id, "when it was deleted"); LDP_POP(); tc_ver_us += tc_us() - t0; }   // perf (b45)
			T->live = 0; T->dfile = f; T->dline = (unsigned short)l; T->data = NULL; free(T->ph); T->ph = NULL; T->npg = 0; }
		glDeleteTextures(1, &id);
	}
	tc_event("delete %d textures %s:%d", (int)n, tc_base(f), l);
}
static void tc_glBindTexture(GLenum target, GLuint id, const char *f, int l) {
	if (id && id < TC_MAXT && !tc_t[id].live && !tc_t[id].bind_logged) { tc_t[id].bind_logged = 1;
		tc_report("BIND of texture id %u that is not live at %s:%d (%s)", id, tc_base(f), l, tc_t[id].file ? "deleted before" : "never made"); }
	glBindTexture(target, id);
}
static void tc_desc_read(TcTex *T) {   // the bound texture's SceGxmTexture
	SceGxmTexture *g = vglGetGxmTexture(GL_TEXTURE_2D); if (!g) return;
	T->gdata = sceGxmTextureGetData(g); T->gfmt = (unsigned)sceGxmTextureGetFormat(g); T->gw = sceGxmTextureGetWidth(g); T->gh = sceGxmTextureGetHeight(g);
}
static int tc_pot(unsigned v) { return v && !(v & (v - 1)); }
// after an upload into texture id (the transfer has finished): kind 1/3/5 DXT, 4 RGBA8, 0 other; w, h: the level's size
static void tc_registered(int id, unsigned add, unsigned w, unsigned h, int level, int kind, const void *src, const char *f, int l) {
	tc_nup++; if (level == 0) tc_event("upload %ux%u %s:%d", w, h, tc_base(f), l);
	if (id <= 0 || id >= TC_MAXT) return;
	TcTex *T = &tc_t[id];
	if (!T->live) { tc_report("UPLOAD into texture id %d that is not live at %s:%d", id, tc_base(f), l); T->live = 1; T->file = f; T->line = (unsigned short)l; if (id > tc_maxid) tc_maxid = id; }
	unsigned char *p = (unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D);
	if (level == 0) {
		T->size = add; T->w = (unsigned short)w; T->h = (unsigned short)h; T->kind = (unsigned char)kind; T->nlev = 1; T->src = 0;
		T->repointed = 0; T->reported = 0; T->nchg = 0; T->vsize = 0;
		T->nocheck = !TC_VERIFY || !src || !p || !kind || (kind != 4 && (!tc_pot(w) || !tc_pot(h)));   // render targets / empty uploads, unknown layouts
	} else { T->size += add; if (level >= T->nlev) T->nlev = (unsigned char)(level + 1); }
	T->data = p; T->t_up = tc_now;
	if (src && TC_VERIFY) T->src = T->src * 16777619u + tc_srchash((const unsigned char *)src, add);
	tc_desc_read(T);
	if (level == 0 && p) { char what[128]; snprintf(what, sizeof(what), "new texture %s", tc_tname(id)); tc_overlap_tex(p, T->size, id, what); tc_overlap_bufs(id); tc_overlap_blocks(id); }
	if (T->nocheck || !p) return;
	if (kind == 4 && level > 0) return;   // uncompressed mip chains: vitaGL's layout is not modelled (level 0 stays verified)
	unsigned off = 0;
	if (kind == 4) T->vsize = ((w + 7) & ~7u) * h * 4;
	else { unsigned lw = T->w, lh = T->h, bs = kind == 1 ? 8 : 16;
		for (int k = 0; k < level; k++) { off += ((lw + 3) / 4) * ((lh + 3) / 4) * bs; if (lw > 1) lw /= 2; if (lh > 1) lh /= 2; }
		T->vsize = off + add; }
	unsigned us = (unsigned)vglMallocUsableSize(p);
	if (us && T->vsize > us) { tc_chk_skip++; T->vsize = us > off ? us : off; }
	// the level as the GPU will read it == the source? (both checksums over the level's bytes)
	if (src && off + add <= T->vsize && (kind == 4 ? (w % 8 == 0) : (w >= 4 && h >= 4))) {
		unsigned long long t0 = tc_us(); unsigned a = tc_bag((const unsigned char *)src, add), b = tc_bag(p + off, add); tc_chk_us += tc_us() - t0; tc_chk_n++;
		if (a != b) { tc_chk_bad++;
			if (tc_report("BAD UPLOAD: texture %s level %d (%ux%u, %u bytes at +0x%X, %p): right after the transfer the texture memory does NOT hold the source bytes (source %p)",
				tc_tname(id), level, w, h, add, off, p + off, src) && tc_ndump++ < 12) {
				char d[400]; tc_describe(p + off, add < TC_PAGE ? add : TC_PAGE, d, sizeof(d)); logp("  the texture memory: %s\n", d);
				tc_describe((const unsigned char *)src, add < TC_PAGE ? add : TC_PAGE, d, sizeof(d)); logp("  the source: %s\n", d);
				tc_owners(p + off, add, id); } }
	} else if (src) tc_chk_skip++;
	tc_hash_pages(T, off);
	T->t_good = tc_now;
}
// every upload waits for its transfer: vitaGL queues uploads on the GPU's transfer engine; whatever the library build
// does with the source, no upload is left in flight when the caller frees or reuses its buffer (load time only)
static void tc_xfer_wait(void) {
#ifdef __vita__
	sceGxmTransferFinish();
#endif
}
static unsigned tc_bpp(GLenum fmt, GLenum type) {
	if (type != GL_UNSIGNED_BYTE) return 2;
	return fmt == GL_RGBA ? 4 : fmt == GL_RGB ? 3 : fmt == GL_LUMINANCE_ALPHA ? 2 : 1;
}
static unsigned tc_stage_align(void);
static void tc_glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const GLvoid *data, const char *f, int l) {
	if (data) tc_stage_align();   // render b41: the staging copy of an upload starts 64-byte aligned (see tc_glCompressedTexImage2D)
	glTexImage2D(target, level, ifmt, w, h, border, fmt, type, data);
	tc_xfer_wait();
	int kind = ifmt == GL_RGBA && fmt == GL_RGBA && type == GL_UNSIGNED_BYTE ? 4 : 0;
	LDP_PUSH(LDP_CANARY); if (target == GL_TEXTURE_2D) tc_registered(tc_bound_tex(), (unsigned)w * h * tc_bpp(fmt, type), w, h, level, kind, data, f, l); LDP_POP();   // perf (b45)
}
// ---- render b41: compressed uploads (the b40 device run's BAD UPLOADs and the Shooting Range's speckled sky band)
// vitaGL (this libvitaGL.a) uploads a power-of-two DXT level by copying it into its frame data pool (vgl_reserve_data_pool:
// a bump allocator that does NOT align) and having the transfer engine copy that into the swizzled texture: RAW128 for
// DXT3/5, RAW64 for DXT1. On the device a RAW128 copy from a staging address that is 8 mod 16 leaves the texture holding
// other bytes (b40-2, Urban: all 11 BAD DXT5 textures had their staging at 8 mod 16, all 10 good ones at 0 mod 16: the
// DXT1 chains before them end in an 8-byte 4x4 level, which flips the pool's alignment). Vita3K does not model it.
// Fix: (1) the pool is padded to 64 bytes before every upload, so the staging copy is aligned; (2) every DXT3/5 level (and
// a DXT1 level whose staging was not 8-aligned) is compared with its source after the transfer (order-independent
// checksum) and, if different, written again by the CPU in the GPU's block order (tc_dxt_swizzle: Vita3K's
// resolve_z_order_compressed_image, inverted). Counts per map in the texsrc line; a level still wrong after that is a
// BAD UPLOAD report. Test switch ux0:data/gitsfa/dxtcpu: the CPU order is compared with every level the transfer wrote
// (all must be identical in Vita3K) and written anyway.
#if defined(__vita__) && defined(__arm__)
extern void *vgl_reserve_data_pool(unsigned size);   // libvitaGL.a vgl.o: returns the pool's current pointer, then advances it
static unsigned tc_stage_align(void) {   // returns the staging misalignment the upload would have had (mod 16)
	uintptr_t c = (uintptr_t)vgl_reserve_data_pool(0); unsigned pad = (unsigned)(-c & 63);
	if (pad) vgl_reserve_data_pool(pad);
	return (unsigned)(c & 15);
}
#else
static unsigned tc_stage_align(void) { return 0; }
#endif
static int tc_dxt_n, tc_dxt_mis, tc_dxt_bad, tc_dxt_fix, tc_dxt_left, tc_dxt_cpu = -1, tc_dxt_same, tc_dxt_cmp;   // since the last map load
static void tc_dxt_reset(void) { tc_dxt_n = tc_dxt_mis = tc_dxt_bad = tc_dxt_fix = tc_dxt_left = tc_dxt_same = tc_dxt_cmp = 0; }
static unsigned tc_compact1(unsigned x) { x &= 0x55555555; x = (x ^ (x >> 1)) & 0x33333333; x = (x ^ (x >> 2)) & 0x0f0f0f0f; x = (x ^ (x >> 4)) & 0x00ff00ff; x = (x ^ (x >> 8)) & 0x0000ffff; return x; }
// linear blocks (bw x bh, power-of-two counts) -> the GPU's swizzled order: block i holds (x, y) with y in the even and x in
// the odd bits of i over the smaller side, the rest of i extending the longer side
static void tc_dxt_swizzle(unsigned char *dst, const unsigned char *src, unsigned bw, unsigned bh, unsigned bs) {
	unsigned mn = bw < bh ? bw : bh, k = 0; while ((1u << (k + 1)) <= mn) k++;
	for (unsigned i = 0, n = bw * bh; i < n; i++) {
		unsigned x = tc_compact1(i >> 1) & (mn - 1), y = tc_compact1(i) & (mn - 1), up = (i >> (2 * k)) << k;
		if (bw >= bh) x |= up; else y |= up;
		memcpy(dst + (size_t)i * bs, src + ((size_t)y * bw + x) * bs, bs);
	}
}
static void tc_dxt_check(int level, int kind, unsigned w, unsigned h, unsigned sz, const void *src, unsigned mis) {
	if (!src || !kind || kind == 4 || !tc_pot(w) || !tc_pot(h) || w > 2048 || h > 2048) return;
	int id = tc_bound_tex(); if (id <= 0 || id >= TC_MAXT) return;
	unsigned bw0 = level == 0 ? w : tc_t[id].w, bh0 = level == 0 ? h : tc_t[id].h, bs = kind == 1 ? 8 : 16, off = 0;
	if (!bw0 || !bh0) return;
	for (int k = 0; k < level; k++) { off += ((bw0 + 3) / 4) * ((bh0 + 3) / 4) * bs; if (bw0 > 1) bw0 /= 2; if (bh0 > 1) bh0 /= 2; }
	unsigned bw = (w + 3) / 4, bh = (h + 3) / 4;
	if (bw * bh * bs != sz) return;   // not the layout modelled here (vitaGL's own CPU path covers the rest)
	unsigned char *p = (unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D); if (!p) return;
	unsigned us = (unsigned)vglMallocUsableSize(p); if (us && off + sz > us) return;
	if (tc_dxt_cpu < 0) { tc_dxt_cpu = 0;
#ifndef GITSFA_RELEASE
		FILE *cf = fopen("ux0:data/gitsfa/dxtcpu", "rb"); if (cf) { tc_dxt_cpu = 1; fclose(cf); }
#endif
	}
	tc_dxt_n++; if (mis & (kind == 1 ? 7 : 15)) tc_dxt_mis++;
	if (tc_dxt_cpu) {   // test: the CPU order against what the transfer wrote, then the CPU's bytes
		unsigned char *tmp = (unsigned char *)malloc(sz); if (!tmp) return;
		tc_dxt_swizzle(tmp, (const unsigned char *)src, bw, bh, bs); tc_dxt_cmp++; tc_dxt_same += !memcmp(tmp, p + off, sz);
		memcpy(p + off, tmp, sz); free(tmp); return;
	}
	if (kind == 1 && !(mis & 7)) return;   // RAW64 from an 8-aligned staging: not affected
	if (tc_bag((const unsigned char *)src, sz) == tc_bag(p + off, sz)) return;
	tc_dxt_bad++;
	tc_dxt_swizzle(p + off, (const unsigned char *)src, bw, bh, bs);
	if (tc_bag((const unsigned char *)src, sz) == tc_bag(p + off, sz)) tc_dxt_fix++; else tc_dxt_left++;
}
static void tc_glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei sz, const void *data, const char *f, int l) {
	unsigned mis = data ? tc_stage_align() : 0;   // render b41
	glCompressedTexImage2D(target, level, ifmt, w, h, border, sz, data);
	tc_xfer_wait();
	int kind = ifmt == 0x83F0 || ifmt == 0x83F1 ? 1 : ifmt == 0x83F2 ? 3 : ifmt == 0x83F3 ? 5 : 0;   // S3TC DXT1 RGB/RGBA, DXT3, DXT5
	if (target == GL_TEXTURE_2D) { LDP_PUSH(LDP_DXTCHK); tc_dxt_check(level, kind, (unsigned)w, (unsigned)h, (unsigned)sz, data, mis); LDP_POP();   // perf (b45): phases
		LDP_PUSH(LDP_CANARY); tc_registered(tc_bound_tex(), (unsigned)sz, w, h, level, kind, data, f, l); LDP_POP(); }
}
static void tc_glGenerateMipmap(GLenum target, const char *f, int l) {
	glGenerateMipmap(target);
	tc_xfer_wait();
	int id = tc_bound_tex(); if (target != GL_TEXTURE_2D || id <= 0 || id >= TC_MAXT) return;
	TcTex *T = &tc_t[id]; T->data = (unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D); T->t_up = tc_now; tc_desc_read(T);   // (vitaGL reallocates for the chain)
	if (!T->nocheck && T->data) { tc_hash_pages(T, 0); T->t_good = tc_now; }
	(void)f; (void)l;
}
static void tc_set_name(GLuint id, const char *fmt, ...) {
	if (!id || id >= TC_MAXT) return;
	va_list ap; va_start(ap, fmt); vsnprintf(tc_t[id].name, sizeof(tc_t[id].name), fmt, ap); va_end(ap);
}
#define TC_NAME(id, ...) tc_set_name(id, __VA_ARGS__)
static void tc_dxt_log(const char *what) {   // render b41: world.h, after a map's textures and sky (counts since the last line)
	logp("dxt uploads (up to %s): %d levels; staging realigned for %d (vitaGL's pool padded to 64 B); %d differed from the source after the transfer, %d rewritten by the CPU and now right, %d still wrong%s",
		what, tc_dxt_n, tc_dxt_mis, tc_dxt_bad, tc_dxt_fix, tc_dxt_left, tc_dxt_cmp ? "" : "\n");
	if (tc_dxt_cmp) logp(" | dxtcpu test: the CPU block order matched the transfer's bytes in %d of %d levels\n", tc_dxt_same, tc_dxt_cmp);
	tc_dxt_reset();
}
#define TC_DXT_LOG(s) tc_dxt_log(s)
// the source hashes of a map's textures (world.h, after its textures loaded): compared on the host with the .gmap
static void tc_srclog(const char *stem, const GLuint *ids, int n) {
	LDP_PUSH(LDP_CANARY);   // perf (b45)
	logp("texsrc %s: %d textures; upload check: %d levels compared (%d skipped), %d BAD, %u ms; source hash per texture (texsrc_check.py):\n",
		stem, n, tc_chk_n, tc_chk_skip, tc_chk_bad, (unsigned)(tc_chk_us / 1000));
	for (int i = 0; i < n; i += 16) {
		char s[16 * 9 + 48]; int k = snprintf(s, sizeof(s), "texsrc %.16s %d:", stem, i);
		for (int j = i; j < n && j < i + 16; j++) k += snprintf(s + k, sizeof(s) - k, " %08x", ids[j] && ids[j] < TC_MAXT ? tc_t[ids[j]].src : 0u);
		logp("%s\n", s);
	}
	LDP_POP();
}
// raw vitaGL memory and re-pointed textures (videos, the camo's screen texture)
static void tc_blk_add(void *p, unsigned n, int vram, const char *f, int l) {
	if (!p) return;
	int k = -1;
	if (tc_na < TC_MAXA) k = tc_na++;
	else for (int i = 0; i < tc_na; i++) if (!tc_a[i].live && (k < 0 || tc_a[i].t1 < tc_a[k].t1)) k = i;   // reuse the oldest freed slot
	if (k < 0) return;
	TcBlk *A = &tc_a[k]; A->p = (unsigned char *)p; A->n = n; A->file = f; A->line = (unsigned short)l; A->live = 1; A->vram = (unsigned char)vram; A->t0 = tc_now; A->t1 = 0;
	char w[64]; snprintf(w, sizeof(w), "raw block (%s:%d)", tc_base(f), l); tc_overlap_tex((unsigned char *)p, n, 0, w);
}
static __attribute__((unused)) void *tc_vglMemalign(uint32_t a, uint32_t n, const char *f, int l) {
	void *p = vglMemalign(a, n); tc_event("vglMemalign %u KB -> %p %s:%d", n >> 10, p, tc_base(f), l); tc_blk_add(p, n, 0, f, l);
	return p;
}
static void *tc_vglAlloc(uint32_t n, vglMemType t, const char *f, int l) {
	void *p = vglAlloc(n, t); tc_event("vglAlloc %u KB type %d -> %p %s:%d", n >> 10, (int)t, p, tc_base(f), l); tc_blk_add(p, n, 1, f, l);
	return p;
}
static void tc_vglFree(void *p, const char *f, int l) {
	for (int i = 0; i < tc_na; i++) if (tc_a[i].p == p && tc_a[i].live) { tc_a[i].live = 0; tc_a[i].t1 = tc_now; break; }
	tc_event("vglFree %p %s:%d", p, tc_base(f), l); vglFree(p);
}
static int tc_sceGxmTextureInitLinear(SceGxmTexture *t, const void *d, SceGxmTextureFormat fm, unsigned w, unsigned h, unsigned m, const char *f, int l) {
	int id = tc_bound_tex(); if (id > 0 && id < TC_MAXT && vglGetGxmTexture(GL_TEXTURE_2D) == t) tc_t[id].repointed = 1;   // re-pointed on purpose: not checked
	tc_event("repoint texture %d at %s:%d", id, tc_base(f), l);
	return sceGxmTextureInitLinear(t, d, fm, w, h, m);
}
#include "shcache.h"   // b36: compiled shader binaries cached on the memory card (later starts skip SceShaccCg)
static void tc_glCompileShader(GLuint sh, const char *f, int l) { unsigned long long t0 = tc_us(); shc_compile(sh); tc_event("compile shader %s:%d %u ms", tc_base(f), l, (unsigned)((tc_us() - t0) / 1000)); }
static void tc_glLinkProgram(GLuint p, const char *f, int l) { unsigned long long t0 = tc_us(); glLinkProgram(p); tc_event("link program %s:%d %u ms", tc_base(f), l, (unsigned)((tc_us() - t0) / 1000)); }
static FILE *tc_fopen(const char *path, const char *mode, const char *f, int l) { FILE *r = fopen(path, mode); if (TC_MAIN_THREAD()) tc_event("fopen %.40s%s (%s:%d)", tc_base(path), r ? "" : " FAILED", tc_base(f), l); return r; }   // (perf b45: main thread)
#include "frametime.h"   // frame timers (render): a frame over 0.2 s is logged with its breakdown
// ---- per frame: the data pointer and SceGxmTexture of the texture being verified, then its pages in rotation (0.3 ms)
static void tc_rot_next(float now) { tc_vpg = 0; tc_vid = tc_vid >= tc_maxid ? 1 : tc_vid + 1; if (tc_vid == 1) { tc_npass++; tc_pass_len = now - tc_pass_t0; tc_pass_t0 = now; } }
static void tc_tick(float now) {
	tc_now = now; shc_report();
	float dt = tc_prev > 0 ? now - tc_prev : 0; int slow = tc_prev > 0 && dt > 0.2f && !tc_loaded;
	// render b39 (QA c883d6a's fix, b38 soak): HITCH lines have their own budget of TC_MAXHITCH, outside the TC_MAXLOG anomaly
	// lines (in the emulator every frame is a hitch: they used up the 90 lines in a minute and hid the real anomalies)
	static int tc_nhitch;
	if (slow && dt > 0.25f && tc_nhitch < TC_MAXHITCH) { int nl = tc_nlog; tc_nhitch++; tc_anom--; tc_nlog = 0;
		tc_report("HITCH: a frame took %.2f s (%d texture uploads in it; what ran just before it: the events below)", dt, tc_nup - tc_nup_tick); tc_nlog = nl;
		if (tc_nhitch == TC_MAXHITCH) logp("TEXCANARY: further HITCH lines suppressed (the %d anomaly lines are kept for the rest)\n", TC_MAXLOG); }
	ft_frame(slow, dt);
	tc_prev = now; tc_loaded = 0; tc_nup_tick = tc_nup; if (tc_maxid <= 0) return;
	if (!tc_npass && tc_pass_t0 == 0) tc_pass_t0 = now;
	unsigned long long t0 = tc_us(); int pages = 0, guard = 0, bound = 0; GLint act = 0, cur = 0;
	while (pages < 64 && guard++ < 2 * TC_MAXT) {
		TcTex *T = &tc_t[tc_vid];
		if (!T->live || T->nocheck || T->repointed || !T->ph || !T->data) { tc_rot_next(now); continue; }
		if (tc_vpg == 0) {   // start of this texture's pass: its data pointer and its SceGxmTexture
			if (!bound) { bound = 1; glGetIntegerv(GL_ACTIVE_TEXTURE, &act); glActiveTexture(GL_TEXTURE0); cur = tc_bound_tex(); }
			glBindTexture(GL_TEXTURE_2D, tc_vid); tc_checked++;
			unsigned char *p = (unsigned char *)vglGetTexDataPointer(GL_TEXTURE_2D);
			if (p != T->data) { tc_report("MOVED: texture %s data %p -> %p without a GL call on it (freed / deleted behind its owner?)", tc_tname(tc_vid), T->data, p); T->data = p; tc_hash_pages(T, 0); }
			SceGxmTexture *g = vglGetGxmTexture(GL_TEXTURE_2D);
			if (g) { void *gd = sceGxmTextureGetData(g); unsigned gf = (unsigned)sceGxmTextureGetFormat(g), gw = sceGxmTextureGetWidth(g), gh = sceGxmTextureGetHeight(g);
				if (gd != T->gdata || gf != T->gfmt || gw != T->gw || gh != T->gh) {
					tc_report("DESCRIPTOR: texture %s: the GPU now samples %p format 0x%08X %ux%u, the upload left %p format 0x%08X %ux%u (texture memory %p) -- changed without a GL call",
						tc_tname(tc_vid), gd, gf, gw, gh, T->gdata, T->gfmt, T->gw, T->gh, T->data);
					if (gd != T->data) tc_owners((const unsigned char *)gd, 4096, tc_vid);
					T->gdata = gd; T->gfmt = gf; T->gw = gw; T->gh = gh; } }
		}
		unsigned k = tc_vpg, o = k * TC_PAGE, n = T->vsize - o < TC_PAGE ? T->vsize - o : TC_PAGE;
		unsigned h = tc_page(T->data + o, n); if (h != T->ph[k]) tc_page_changed(tc_vid, k, h, "in the rotation");
		pages++; tc_vpg++;
		if (tc_vpg >= T->npg) { tc_verify_done(tc_vid); tc_rot_next(now); }
		if ((pages & 7) == 0 && tc_us() - t0 > 300) break;
	}
	if (bound) { glBindTexture(GL_TEXTURE_2D, cur); glActiveTexture(act ? act : GL_TEXTURE0); }
	tc_ver_us += tc_us() - t0;
	if (now - tc_last_sum > 60) { tc_last_sum = now; int lt = 0, lv = 0; unsigned bytes = 0, vb = 0, lb = 0;
		for (int i = 1; i <= tc_maxid; i++) if (tc_t[i].live) { lt++; bytes += tc_t[i].size; if (tc_t[i].ph && !tc_t[i].nocheck) { lv++; vb += tc_t[i].vsize; } }
		for (int k2 = 0; k2 < TC_MAXB; k2++) if (tc_b[k2].key && tc_b[k2].live) lb++;
		logp("texcanary: %d live textures (%u KB; %d verified, %u KB), %u live buffers, %d descriptor checks, %d full passes (last %.1f s), verification %u ms so far, %d anomalies\n",
			lt, bytes >> 10, lv, vb >> 10, lb, tc_checked, tc_npass, tc_pass_len, (unsigned)(tc_ver_us / 1000), tc_anom); }
}
// ---- every later GL call of the engine goes through the wrappers (file:line of the caller)
#define glGenTextures(n, p) tc_glGenTextures(n, p, __FILE__, __LINE__)
#define glDeleteTextures(n, p) tc_glDeleteTextures(n, p, __FILE__, __LINE__)
#define glBindTexture(t, id) tc_glBindTexture(t, id, __FILE__, __LINE__)
#define glTexImage2D(t, lv, i, w, h, b, f, ty, d) tc_glTexImage2D(t, lv, i, w, h, b, f, ty, d, __FILE__, __LINE__)
#define glCompressedTexImage2D(t, lv, i, w, h, b, s, d) tc_glCompressedTexImage2D(t, lv, i, w, h, b, s, d, __FILE__, __LINE__)
#define glGenerateMipmap(t) tc_glGenerateMipmap(t, __FILE__, __LINE__)
#define glGenBuffers(n, p) tc_glGenBuffers(n, p, __FILE__, __LINE__)
#define glDeleteBuffers(n, p) tc_glDeleteBuffers(n, p, __FILE__, __LINE__)
#define glBindBuffer(t, id) tc_glBindBuffer(t, id)
#define glBufferData(t, s, d, u) tc_glBufferData(t, s, d, u, __FILE__, __LINE__)
#define glMapBuffer(t, a) tc_glMapBuffer(t, a, __FILE__, __LINE__)
#define vglMemalign(a, n) tc_vglMemalign(a, n, __FILE__, __LINE__)
#define vglAlloc(n, t) tc_vglAlloc(n, t, __FILE__, __LINE__)
#define vglFree(p) tc_vglFree(p, __FILE__, __LINE__)
#define glCompileShader(s) tc_glCompileShader(s, __FILE__, __LINE__)
#define glShaderSource(s, n, str, len) tc_glShaderSource(s, n, str, len)
#define glCreateShader(t) shc_glCreateShader(t)   // render b45: the shader's kind, for the shader bank's key (shcache.h)
#define glLinkProgram(p) tc_glLinkProgram(p, __FILE__, __LINE__)
#define fopen(p, m) tc_fopen(p, m, __FILE__, __LINE__)
#define sceGxmTextureInitLinear(t, d, f, w, h, m) tc_sceGxmTextureInitLinear(t, d, f, w, h, m, __FILE__, __LINE__)
