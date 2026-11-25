// Frame memory for sceAvPlayer's decoder (tut_video.h's allocateTexture / deallocateTexture), safe to call from the
// player's threads (a spin lock; only kernel calls inside it - memblocks, sceGxmMapMemory - never vitaGL).
// What the device's player asks for (b41 log, every movie): #4 = its output frames (2 x w*h*1.5 bytes, align 0x80),
// #5 = the decoder's memory (11 MB for 960x544, 5 MB for 640x272, align 1 MB). b41 put #4 in a sub-block of vitaGL's
// CDRAM pool and #5 in a phycont memblock GPU-mapped only later (READ, at the first frame): every allocation succeeded,
// then the player stopped by itself. Modes (TVM.mode, set per movie by tut_video.h):
//   TVM_OWN (b42 default): every request in its OWN memblock - physically contiguous main memory (USER_MAIN_PHYCONT_NC_RW,
//     1 MB units; vitaGL keeps none of phycont, main.c H2, and the game never uses it: 18 MB free), else CDRAM (256 KB
//     units) - aligned to at least 256 KB (SceKernelAllocMemBlockOpt + HAS_ALIGNMENT), size rounded up to that, GPU-mapped
//     READ|WRITE right away in the callback (as Vita homebrew players do). A block the player frees is kept (the GPU may
//     still sample it) and reused for its next request that fits; all are unmapped and freed in tvm_release.
//   modifiers (the boot movie test tries them, to tell the next device log which one matters):
//     TVM_NOMAP   do not GPU-map at allocation (the game thread maps a block READ|WRITE before drawing a frame from it)
//     TVM_CACHED  cached phycont (USER_MAIN_PHYCONT_RW) instead of uncached
//   TVM_ARENA: requests carved (256 KB-aligned) out of one arena tut_video.h takes from vitaGL's CDRAM pool on the game
//     thread before the player exists (already GPU-mapped by vitaGL); with TVM_OWN as well: arena first, own memblocks for
//     whatever does not fit (b41's layout, now with 256 KB alignment and mapping at allocation).
// Every request is recorded (tvm_note) and logged by the game thread (tut_video.h tv_log_reqs).
// Platform hooks, defined before including this file (tut_video.h; tools/tutorial/host/vmem_test.c):
//   TVM_BLOCK_ALLOC(kind, size, align, int *uid) -> base or NULL (*uid = the error code)   kind: TVK_PHYCONT / TVK_PHYCONT_C / TVK_CDRAM
//   TVM_BLOCK_FREE(uid)   TVM_GPU_MAP(base, size) -> 0 ok (READ|WRITE)   TVM_GPU_UNMAP(base)   TVM_ARENA_FREE(raw)   TVM_YIELD()
#pragma once
#include <stdint.h>
#include <string.h>
#define TVM_NCH 24
#define TVM_NBLK 40
#define TVM_NREQ 48
#define TVM_MIN_ALIGN (256u << 10)
enum { TVM_OWN = 1, TVM_NOMAP = 2, TVM_CACHED = 4, TVM_ARENA = 8 };
enum { TVK_ARENA, TVK_PHYCONT, TVK_PHYCONT_C, TVK_CDRAM, TVK_N };
static const char *const TVK_NAME[TVK_N] = { "cdram arena", "phycont memblock", "cached phycont memblock", "cdram memblock" };
typedef struct { char *base, *raw; unsigned size; int kind, mapped, map_err, uid, own, in_use; } TvmChunk;
typedef struct { unsigned size, align, used_align; int tex, chunk, err_p, err_c, reused, map_err; char *addr; } TvmReq;   // tex: 1 allocateTexture, 0 allocate
static struct {
	int mode;
	TvmChunk ch[TVM_NCH]; int nch;
	struct { char *p; unsigned size; } b[TVM_NBLK]; int nb;
	unsigned used, peak; int n_req, n_fail, n_free, n_gen, n_gen_fail, n_reuse; unsigned gen_kb;
	TvmReq rq[TVM_NREQ]; volatile int rq_n; int rq_logged, rq_lost;
	volatile int lock;
} TVM;
static void tvm_take(void) { while (__sync_lock_test_and_set(&TVM.lock, 1)) TVM_YIELD(); }
static void tvm_give(void) { __sync_lock_release(&TVM.lock); }
static void tvm_reset(int mode) { memset(&TVM, 0, sizeof(TVM)); TVM.mode = mode; }   // (no player, nothing allocated)
static const char *tvm_mode_name(int m) {
	switch (m) {
	case TVM_OWN: return "own phycont memblocks, 256 KB-aligned, GPU-mapped RW at allocation";
	case TVM_OWN | TVM_NOMAP: return "own phycont memblocks, 256 KB-aligned, NOT mapped at allocation";
	case TVM_OWN | TVM_CACHED: return "own CACHED phycont memblocks, 256 KB-aligned, mapped RW";
	case TVM_ARENA: return "cdram arena only (vitaGL's pool), 256 KB-aligned";
	case TVM_ARENA | TVM_OWN: return "cdram arena first, own phycont memblocks (mapped RW) for the rest";
	default: return "custom";
	}
}
static void tvm_note(const TvmReq *r) {   // (lock held)
	int n = TVM.rq_n; if (n >= TVM_NREQ) { TVM.rq_lost++; return; }
	TVM.rq[n] = *r; __sync_synchronize(); TVM.rq_n = n + 1;
}
// the lowest address in arena chunk C, aligned to al (a power of two), where size bytes overlap no block
static char *tvm_carve(const TvmChunk *C, unsigned al, unsigned size) {
	uintptr_t lo = (uintptr_t)C->base, hi = lo + C->size, a = (lo + al - 1) & ~(uintptr_t)(al - 1);
	for (int k = 0; k <= TVM.nb; k++) {   // each step moves past one block
		if (a < lo || a + size > hi || a + size < a) return NULL;
		int hit = -1; for (int j = 0; j < TVM.nb && hit < 0; j++) { uintptr_t s = (uintptr_t)TVM.b[j].p, e = s + TVM.b[j].size; if (s < a + size && e > a) hit = j; }
		if (hit < 0) return (char *)a;
		a = ((uintptr_t)TVM.b[hit].p + TVM.b[hit].size + al - 1) & ~(uintptr_t)(al - 1);
	}
	return NULL;
}
static void tvm_add_arena(char *raw, char *base, unsigned size) {   // (game thread, before the player exists)
	tvm_take(); if (TVM.nch < TVM_NCH) { TvmChunk *C = &TVM.ch[TVM.nch++]; memset(C, 0, sizeof(*C)); C->raw = raw; C->base = base; C->size = size; C->kind = TVK_ARENA; C->mapped = 1; } tvm_give();
}
// a memblock of its own for one request (lock held): a free one of ours that fits, else a new one (phycont, then cdram)
static char *tvm_own(unsigned al, unsigned sz, TvmReq *q) {
	for (int i = 0; i < TVM.nch; i++) { TvmChunk *C = &TVM.ch[i];
		if (C->own && !C->in_use && C->size >= sz && !((uintptr_t)C->base & (al - 1))) { C->in_use = 1; q->chunk = i; q->reused = 1; TVM.n_reuse++; return C->base; } }
	const int G[2] = { (TVM.mode & TVM_CACHED) ? TVK_PHYCONT_C : TVK_PHYCONT, TVK_CDRAM };
	for (int g = 0; g < 2; g++) {
		if (TVM.nch >= TVM_NCH) { if (g == 0) q->err_p = -2; else q->err_c = -2; continue; }
		unsigned unit = G[g] == TVK_CDRAM ? 256u << 10 : 1u << 20, u = unit > al ? unit : al, bs = (sz + u - 1) & ~(u - 1); int uid = 0;
		char *b = TVM_BLOCK_ALLOC(G[g], bs, al, &uid);
		if (!b) { if (g == 0) q->err_p = uid ? uid : -1; else q->err_c = uid ? uid : -1; continue; }
		TvmChunk *C = &TVM.ch[TVM.nch]; memset(C, 0, sizeof(*C)); C->base = C->raw = b; C->size = bs; C->kind = G[g]; C->uid = uid; C->own = 1; C->in_use = 1;
		if (!(TVM.mode & TVM_NOMAP)) { int r = TVM_GPU_MAP(b, bs); if (r < 0) C->map_err = r; else C->mapped = 1; q->map_err = r < 0 ? r : 0; }
		q->chunk = TVM.nch++; return b;
	}
	return NULL;
}
static void *tvm_alloc(uint32_t align, uint32_t size) {
	unsigned al = align < 256 ? 256 : align; while (al & (al - 1)) al += al & (0u - al);   // a power of two
	if (al < TVM_MIN_ALIGN) al = TVM_MIN_ALIGN;
	unsigned sz = size ? size : 1; char *r = NULL;
	TvmReq q; memset(&q, 0, sizeof(q)); q.tex = 1; q.size = size; q.align = align; q.used_align = al; q.chunk = -1;
	tvm_take();
	if (TVM.nb < TVM_NBLK) {
		if (TVM.mode & TVM_ARENA) for (int i = 0; i < TVM.nch && !r; i++) if (TVM.ch[i].kind == TVK_ARENA && (r = tvm_carve(&TVM.ch[i], al, sz))) q.chunk = i;
		if (!r && (TVM.mode & TVM_OWN)) r = tvm_own(al, sz, &q);
	} else q.err_p = q.err_c = -3;   // (the block table is full)
	if (r) { TVM.b[TVM.nb].p = r; TVM.b[TVM.nb].size = sz; TVM.nb++; TVM.used += sz; if (TVM.used > TVM.peak) TVM.peak = TVM.used; }
	TVM.n_req++; if (!r) TVM.n_fail++;
	q.addr = r; tvm_note(&q);
	tvm_give();
	return r;
}
static void tvm_free(void *ptr) {   // (the memory stays ours until tvm_release; an own memblock becomes reusable)
	tvm_take();
	for (int i = 0; i < TVM.nb; i++) if (TVM.b[i].p == (char *)ptr) { TVM.used -= TVM.b[i].size; TVM.b[i] = TVM.b[--TVM.nb]; TVM.n_free++; break; }
	for (int i = 0; i < TVM.nch; i++) if (TVM.ch[i].own && TVM.ch[i].base == (char *)ptr) TVM.ch[i].in_use = 0;
	tvm_give();
}
static void tvm_note_general(uint32_t align, uint32_t size, int ok) {   // the player's other allocations: counted; big or failed ones recorded
	tvm_take(); TVM.n_gen++; if (!ok) TVM.n_gen_fail++; else TVM.gen_kb += size / 1024;
	if (!ok || size >= 256 * 1024) { TvmReq q; memset(&q, 0, sizeof(q)); q.size = size; q.align = align; q.chunk = ok ? 0 : -1; tvm_note(&q); }
	tvm_give();
}
static int tvm_chunk_of(const void *p) {   // the chunk holding p, -1 = none of ours
	int c = -1; tvm_take();
	for (int i = 0; i < TVM.nch; i++) if ((const char *)p >= TVM.ch[i].base && (const char *)p < TVM.ch[i].base + TVM.ch[i].size) { c = i; break; }
	tvm_give(); return c;
}
static int tvm_gpu_map(int c) {   // (game thread) 1 = mapped now, 0 = already mapped, < 0 = the error
	if (c < 0 || c >= TVM.nch || TVM.ch[c].mapped) return 0;
	int r = TVM_GPU_MAP(TVM.ch[c].base, TVM.ch[c].size); if (r < 0) return r;
	TVM.ch[c].mapped = 1; return 1;
}
static unsigned tvm_kb(int kind) { unsigned s = 0; for (int i = 0; i < TVM.nch; i++) if (TVM.ch[i].kind == kind) s += TVM.ch[i].size; return s / 1024; }
static int tvm_count(int kind) { int n = 0; for (int i = 0; i < TVM.nch; i++) n += TVM.ch[i].kind == kind; return n; }
static void tvm_release(void) {   // (game thread, after the player is closed and the GPU is done with every frame)
	for (int i = 0; i < TVM.nch; i++) { TvmChunk *C = &TVM.ch[i];
		if (C->kind == TVK_ARENA) TVM_ARENA_FREE(C->raw);
		else { if (C->mapped) TVM_GPU_UNMAP(C->base); TVM_BLOCK_FREE(C->uid); } }
	TVM.nch = 0; TVM.nb = 0; TVM.used = 0;
}
