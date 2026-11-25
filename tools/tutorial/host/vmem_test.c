// host test of engine/src/tut_vmem.h (the movie frame-memory allocator): gcc -O1 -o vmem_test vmem_test.c && ./vmem_test
// Fake memblocks from aligned_alloc, with limits per kind (phycont / cdram free), replaying the device's requests (b41 log).
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
static long fake_free[4];            // bytes the fake kernel still has per kind (index = TVK_*)
static void *fake_base[64]; static unsigned fake_size[64]; static int fake_kind[64], fake_n, fake_live, fake_maps, fake_unmaps, arena_frees;
static char *fake_alloc(int kind, unsigned size, unsigned align, int *uid) {
	if ((long)size > fake_free[kind]) { *uid = (int)0x80020006; return NULL; }   // (no memory)
	void *p = aligned_alloc(align > 4096 ? align : 4096, size); fake_free[kind] -= size;
	fake_base[fake_n] = p; fake_size[fake_n] = size; fake_kind[fake_n] = kind; *uid = 0x1000 + fake_n++; fake_live++; return p;
}
static void fake_bfree(int uid) { int i = uid - 0x1000; free(fake_base[i]); fake_free[fake_kind[i]] += fake_size[i]; fake_live--; }
#define TVM_BLOCK_ALLOC(kind, size, align, uid) fake_alloc(kind, size, align, uid)
#define TVM_BLOCK_FREE(uid) fake_bfree(uid)
#define TVM_GPU_MAP(base, size) (fake_maps++, 0)
#define TVM_GPU_UNMAP(base) (fake_unmaps++)
#define TVM_ARENA_FREE(raw) (free(raw), arena_frees++)
#define TVM_YIELD() ((void)0)
#include "../../../engine/src/tut_vmem.h"

static void check(void) {   // blocks inside their chunks, no two overlap, every frame block 256 KB-aligned
	for (int i = 0; i < TVM.nb; i++) {
		uintptr_t s = (uintptr_t)TVM.b[i].p, e = s + TVM.b[i].size; int in = 0;
		for (int c = 0; c < TVM.nch; c++) in |= s >= (uintptr_t)TVM.ch[c].base && e <= (uintptr_t)TVM.ch[c].base + TVM.ch[c].size;
		assert(in && (s & (TVM_MIN_ALIGN - 1)) == 0);
		for (int j = 0; j < i; j++) { uintptr_t s2 = (uintptr_t)TVM.b[j].p, e2 = s2 + TVM.b[j].size; assert(e <= s2 || e2 <= s); }
	}
}
static void arena(unsigned mb) { char *raw = aligned_alloc(TVM_MIN_ALIGN, mb << 20); tvm_add_arena(raw, raw, mb << 20); }
static void show(const char *t) {
	printf("%s [mode %d: %s]\n", t, TVM.mode, tvm_mode_name(TVM.mode));
	for (int i = 0; i < TVM.rq_n; i++) { TvmReq *q = &TVM.rq[i];
		if (!q->tex) printf("  #%d %8u B align 0x%-6X general allocation %s\n", i + 1, q->size, q->align, q->chunk < 0 ? "FAILED" : "ok");
		else if (q->chunk >= 0) printf("  #%d %8u B align 0x%-6X (0x%X) -> %s%s %d (%u KB)%s\n", i + 1, q->size, q->align, q->used_align, q->reused ? "reused " : "",
			TVK_NAME[TVM.ch[q->chunk].kind], q->chunk, TVM.ch[q->chunk].size / 1024, TVM.ch[q->chunk].own ? (TVM.ch[q->chunk].mapped ? ", own, mapped" : ", own, not mapped") : ", carved");
		else printf("  #%d %8u B align 0x%-6X -> FAILED (phycont 0x%08X, cdram 0x%08X)\n", i + 1, q->size, q->align, (unsigned)q->err_p, (unsigned)q->err_c); }
	printf("  peak %u KB, arena %u KB, phycont %u KB in %d, cached %u KB, cdram %u KB, %d reused\n", TVM.peak / 1024, tvm_kb(TVK_ARENA), tvm_kb(TVK_PHYCONT), tvm_count(TVK_PHYCONT), tvm_kb(TVK_PHYCONT_C), tvm_kb(TVK_CDRAM), TVM.n_reuse);
}
static void dev(unsigned out, unsigned dec) { void *a = tvm_alloc(0x80, out), *b = tvm_alloc(0x100000, dec); assert(a && b); check(); }   // the device's #4 / #5
int main(void) {
	const long PH = 18 << 20;
	// 1. b42 default: the device's logos requests, each in its own 256 KB-aligned phycont memblock, mapped RW at once
	tvm_reset(TVM_OWN); fake_free[TVK_PHYCONT] = PH; fake_free[TVK_CDRAM] = 0;
	tvm_note_general(0x20, 1536 << 10, 1); dev(1566720, 11534336); show("logos (960x544)");
	assert(TVM.nch == 2 && TVM.ch[0].own && TVM.ch[0].mapped && TVM.ch[1].mapped && TVM.ch[0].size == 2 << 20 && TVM.ch[1].size == 11 << 20);
	assert(((uintptr_t)TVM.ch[1].base & 0xFFFFF) == 0 && fake_maps == 2);
	// the player frees and asks again (a looping clip): the same memblock is reused, nothing new is taken
	void *f = TVM.ch[0].base; tvm_free(f); void *g = tvm_alloc(0x80, 1566720); assert(g == f && TVM.n_reuse == 1 && TVM.nch == 2);
	tvm_release(); assert(fake_live == 0 && fake_unmaps == 2 && fake_free[TVK_PHYCONT] == PH);
	// 2. clips: 0.5 MB + 5 MB; then the same with the other modes the boot movie test tries
	const int modes[4] = { TVM_OWN | TVM_NOMAP, TVM_OWN | TVM_CACHED, TVM_ARENA, TVM_ARENA | TVM_OWN };
	tvm_reset(TVM_OWN); dev(522240, 5242880); show("clip (640x272)"); tvm_release();
	for (int m = 0; m < 4; m++) {
		tvm_reset(modes[m]); fake_free[TVK_PHYCONT_C] = PH;
		if (modes[m] & TVM_ARENA) arena(modes[m] & TVM_OWN ? 4 : 7);
		dev(522240, 5242880); show("clip"); if (modes[m] & TVM_NOMAP) assert(!TVM.ch[0].mapped);
		if (modes[m] == TVM_ARENA) assert(TVM.nch == 1);
		if (modes[m] == (TVM_ARENA | TVM_OWN)) assert(TVM.ch[TVM.rq[1].chunk].own);   // 5 MB does not fit the 4 MB arena
		tvm_release(); assert(fake_live == 0);
	}
	// 3. nothing fits anywhere: the request fails, both kernel errors recorded
	tvm_reset(TVM_OWN); fake_free[TVK_PHYCONT] = 2 << 20; fake_free[TVK_CDRAM] = 0;
	assert(!tvm_alloc(0x100000, 6u << 20)); assert(TVM.n_fail == 1 && TVM.rq[0].err_p && TVM.rq[0].err_c); show("no room"); tvm_release();
	// 4. arena carving + own memblocks: random alloc / free with odd alignments, all 256 KB-aligned, none overlapping
	tvm_reset(TVM_ARENA | TVM_OWN); fake_free[TVK_PHYCONT] = 64 << 20; arena(8); srand(1);
	void *p[TVM_NBLK]; int n = 0, full = 0;
	for (int i = 0; i < 400; i++) {
		if (n > 0 && (rand() & 1)) { int k = rand() % n; tvm_free(p[k]); p[k] = p[--n]; }
		else if (n < TVM_NBLK) { unsigned al = 1u << (4 + rand() % 17), sz = 1 + rand() % (900 << 10); if (i % 7 == 0) al = 0x60000;
			void *q = tvm_alloc(al, sz); if (!q) { full++; continue; } p[n++] = q; }
		check();
	}
	printf("random: %d live blocks, %d chunks (%u KB phycont), %d requests (%d refused), %d reused, peak %u KB\n", n, TVM.nch, tvm_kb(TVK_PHYCONT), TVM.n_req, full, TVM.n_reuse, TVM.peak / 1024);
	tvm_release(); assert(fake_live == 0);
	printf("vmem_test: ok\n");
	return 0;
}
