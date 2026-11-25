// jobs.h - a small fork-join worker pool for the spare CPU cores (build 35).
// The game loop runs on one core; the Vita has three user cores (four with CapUnlocker). jobs_parallel_for(fn, ctx, n)
// runs fn(ctx, i) for every i in [0, n) on the workers AND the calling thread, and returns when all are done.
// Rules for job functions: no GL / vitaGL calls (vitaGL is not thread-safe), no logp (the log FILE is not locked),
// only memory the caller handed over (each i writes its own outputs). Nested calls run inline on the caller.
//   jobs_init()          start the workers (once, after the power clocks are set); safe to call twice
//   jobs_parallel_for()  fork-join; n <= 0 returns at once; with no workers it is a plain loop
//   jobs_nworkers()      workers running (0..3)
//   jobs_stats(&busy_us, &calls)  totals since the last call (for the per-second perf line)
#pragma once
#include <stdint.h>
#include <string.h>
typedef void (*job_fn)(void *ctx, int i);
#define JOBS_MAX 3
static struct {
	volatile job_fn fn; void *volatile ctx; volatile int n;
	volatile int next, done, inflight, closed, running, inside;
	int nw; unsigned long long busy_us, calls;
} JB = { .closed = 1 };
static int jobs_core3;   // b61: the workers may use core 3 (CapUnlocker)
static inline int jobs_take(void) { return __sync_fetch_and_add(&JB.next, 1); }
// b37 (host ThreadSanitizer): the hand-over between the caller and the workers is written with acquire / release atomics
// instead of plain volatile accesses between full barriers. The ordering is the same (the barriers stay); the sanitizer
// can only follow it through atomics, and a load's acquire is the explicit form of what the barriers already gave.
#define JB_LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define JB_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)
// a worker's turn: it counts itself in flight before it looks at the job, so the caller can wait for every worker
// that saw this call before it publishes the next one (a worker that wakes late finds the call closed and goes back)
static void jobs_run_items(void) {
	__sync_fetch_and_add(&JB.inflight, 1); __sync_synchronize();
	if (!JB_LOAD(JB.closed)) {
		__sync_synchronize();
		job_fn fn = JB_LOAD(JB.fn); void *ctx = JB_LOAD(JB.ctx); int n = JB_LOAD(JB.n);
		for (int i = jobs_take(); i < n; i = jobs_take()) { fn(ctx, i); __sync_fetch_and_add(&JB.done, 1); }
	}
	__sync_fetch_and_sub(&JB.inflight, 1);
}
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
static SceUID jobs_go[JOBS_MAX] = { -1, -1, -1 }, jobs_th[JOBS_MAX] = { -1, -1, -1 };
static int jobs_worker(SceSize args, void *argp) {
	int k = *(int *)argp;
	while (JB_LOAD(JB.running)) {
		if (sceKernelWaitSema(jobs_go[k], 1, NULL) < 0) break;
		if (!JB_LOAD(JB.running)) break;
		jobs_run_items();
	}
	return 0;
}
static void jobs_init(void) {
	if (JB.nw) return;
	JB.running = 1;
	// cores: the game loop is on core 0 and the audio mixer on core 2 (at a priority above the workers'). b61: each worker
	// may run on any of cores 1-3 (3: CapUnlocker; the system's own threads run there too): a worker that the system
	// pre-empts in the middle of an item can carry on on another core instead of keeping the caller waiting for it (device
	// b60: the occlusion picture once waited 6.3 s for one such item on E-Center; pictures of 20 ms every few seconds).
	// Without CapUnlocker the mask with core 3 is refused: then cores 1-2.
	static const int prio[JOBS_MAX] = { 0x10000100, 0x10000100, 0x10000100 };
	static const int mask3 = SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2 | (SCE_KERNEL_CPU_MASK_USER_0 << 3), mask2 = SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
	static int idx[JOBS_MAX]; int use3 = 1;
	for (int k = 0; k < JOBS_MAX; k++) {
		idx[JB.nw] = JB.nw;
		char nm[32]; memcpy(nm, "gitsfa_job_s", 13); nm[11] = (char)('0' + JB.nw);
		SceUID s = sceKernelCreateSema(nm, 0, 0, JOBS_MAX * 4, NULL); if (s < 0) continue;
		memcpy(nm, "gitsfa_job_t", 13); nm[11] = (char)('0' + JB.nw);
		SceUID t = use3 ? sceKernelCreateThread(nm, jobs_worker, prio[k], 0x10000, 0, mask3, NULL) : -1;
		if (t < 0) { use3 = 0; if (k < 2) t = sceKernelCreateThread(nm, jobs_worker, prio[k], 0x10000, 0, mask2, NULL); }   // (two workers on cores 1-2 without core 3)
		if (t < 0) { sceKernelDeleteSema(s); continue; }
		jobs_go[JB.nw] = s; jobs_th[JB.nw] = t;
		sceKernelStartThread(t, sizeof(int), &idx[JB.nw]);
		JB.nw++;
	}
	jobs_core3 = use3;
}
static inline unsigned long long jobs_now_us(void) { return sceKernelGetProcessTimeWide(); }
static inline void jobs_wake(void) { for (int k = 0; k < JB.nw; k++) sceKernelSignalSema(jobs_go[k], 1); }
static inline void jobs_pause(void) { sceKernelDelayThread(0); }
#else   // host builds: pthreads (tests)
#include <pthread.h>
#include <sched.h>
#include <time.h>
static pthread_t jobs_pt[JOBS_MAX]; static pthread_mutex_t jobs_mx = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t jobs_cv = PTHREAD_COND_INITIALIZER;
static volatile int jobs_ticket;
static void *jobs_worker(void *a) {
	(void)a; int seen = 0;
	for (;;) {
		pthread_mutex_lock(&jobs_mx); while (JB.running && jobs_ticket == seen) pthread_cond_wait(&jobs_cv, &jobs_mx); seen = jobs_ticket; int run = JB.running; pthread_mutex_unlock(&jobs_mx);
		if (!run) break;
		jobs_run_items();
	}
	return NULL;
}
static void jobs_init(void) {
	if (JB.nw) return;
	JB.running = 1;
	for (int k = 0; k < JOBS_MAX; k++) if (!pthread_create(&jobs_pt[k], NULL, jobs_worker, NULL)) JB.nw++;
}
static inline unsigned long long jobs_now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000ull + t.tv_nsec / 1000; }
static inline void jobs_wake(void) { pthread_mutex_lock(&jobs_mx); jobs_ticket++; pthread_cond_broadcast(&jobs_cv); pthread_mutex_unlock(&jobs_mx); }
static inline void jobs_pause(void) { sched_yield(); }
#endif
static int jobs_nworkers(void) { return JB.nw; }
static void jobs_parallel_for(job_fn fn, void *ctx, int n) {
	if (n <= 0) return;
	if (!JB.nw || JB.inside || n == 1) { for (int i = 0; i < n; i++) fn(ctx, i); return; }
	unsigned long long t0 = jobs_now_us();
	JB.inside = 1;
	while (JB_LOAD(JB.inflight)) jobs_pause();   // (a straggler from the last call is leaving)
	JB_STORE(JB.fn, fn); JB_STORE(JB.ctx, ctx); JB_STORE(JB.n, n); JB_STORE(JB.done, 0); JB_STORE(JB.next, 0); __sync_synchronize(); JB_STORE(JB.closed, 0); __sync_synchronize();
	jobs_wake();
	for (int i = jobs_take(); i < n; i = jobs_take()) { fn(ctx, i); __sync_fetch_and_add(&JB.done, 1); }
	while (JB_LOAD(JB.done) < n) jobs_pause();   // the last items finish on the workers
	JB_STORE(JB.closed, 1); __sync_synchronize();
	while (JB_LOAD(JB.inflight)) jobs_pause();   // workers that joined this call are out before the caller's data goes away
	JB.inside = 0;
	JB.busy_us += jobs_now_us() - t0; JB.calls++;
}
static void jobs_stats(unsigned long long *busy_us, unsigned long long *calls) {
	if (busy_us) *busy_us = JB.busy_us; if (calls) *calls = JB.calls; JB.busy_us = 0; JB.calls = 0;
}
