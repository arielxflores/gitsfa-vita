// alog.h (render, b35): logging that never blocks the frame.
// logp() used to fflush() every line to the memory card on the game thread (main.c); a card busy with the test script's
// Wi-Fi file moves then stalled the frame. Now:
//   alog_vprintf(fmt, ap)  formats into a RAM ring (512 KB) under a producers' spin lock (any thread may log; the lock is
//                          held for a memcpy, never for I/O; the writer never takes it). If the ring is full the line is
//                          DROPPED and counted, never waited for.
//   a writer thread (lowest game priority, any core) drains the ring to boot.log with sceIoWrite every 100 ms, or at
//   once when a line asks (alog_urgent: "stopping", "TEXCANARY", crash context). It may stall on the card as long as it
//   likes: the frame does not wait for it.
//   alog_flush()           synchronous drain (fatal paths, before sceKernelExitProcess); alog_close() stops the thread.
//   alog_verbose           0: the per-second diagnostic block of main.c is not logged (normal play, release builds);
//                          1: autoplay test runs, or ux0:data/gitsfa/verbose exists. Release builds (-DGITSFA_RELEASE)
//                          are never verbose and write boot.log only for start-up, map loads and errors (the same file).
//   alog_stats(&bytes, &dropped, &maxfill, &write_ms_worst) for the per-second timing line.
// Crash context: at most ~100 ms of lines (plus whatever the card has not taken yet) can be lost if the game dies; the
// lines a crash would need (stopping / anomalies) wake the writer at once.
// b41 (device run b40-2: the log stopped at map 8, 623 KB dropped): the writer thread was created with priority
// 0x10000100 + 40, outside the legal relative range (-32..+31), so on the Vita it never existed and the ring was only
// written at exit (Vita3K accepts the value). Now: +31, then +20 / +0 if refused, and the result is logged; without a
// thread at all the ring is drained synchronously whenever it is over 64 KB. The last ALOG_RESERVE bytes of the ring are
// kept for the lines a test run must not lose (alog_important: TOUR, probes, the model, match / map / quit / stopping
// lines, summaries): ordinary lines are dropped first.
// b60 (device runs b48 -> b58: the first card write of a start-up took 1.7, 11.5, 17.8, 21.4, 21.4 s, more with every
// week of test data on the card; b58's timed probe: a new 4 KB file 21.5 s, its read back 44 ms): what stalls looks like
// the first cluster ALLOCATION (a search for free space), not writing as such. So in normal play nothing allocates:
//   alog_open(path, inplace=1)  opens the existing boot.log WITHOUT truncating it and writes over it from the start; an
//                          end line (ALOG_END) follows the newest text wherever older, longer text is left below it
//                          (readers stop there: tools/logcut.py). A missing file is created as before.
//   alog_file_put(path, data, n)  small settings files (config.bin, lastmap.txt): copied here, written by the writer
//                          thread into the existing file, never truncated (fixed-size contents).
// Test runs (autoplay) write in place too, so that their start-up is the players' (alog_probe): the writer thread first
// times a 4 KB write over last run's probe file (no allocation), then blanks what is left of the old log with newlines
// (in place: a reader of the file never finds an older run's lines, such as its "tour finished"); when the tour is over
// (alog_probe_end) it times the free-space query and the first write to a NEW file, which is what the next device run
// must tell us. The first write that takes the log past its old size is timed in every run.
#pragma once
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define ALOG_RING (512 * 1024)
#define ALOG_RESERVE (96 * 1024)   // b41: only important lines may use the last 96 KB
static char alog_ring[ALOG_RING];
static volatile unsigned alog_w, alog_r;           // bytes written into / drained from the ring (they only grow)
static volatile int alog_lk, alog_dlk;             // producer lock, drain lock
static volatile unsigned alog_drop, alog_drop_logged, alog_bytes, alog_maxfill, alog_worst_us, alog_drop_imp;
static int alog_thread_ok;   // b41: 1 when the writer thread runs
static int alog_verbose = 1, alog_mute;   // alog_mute: main.c sets it around its per-second block when not verbose
#ifdef GITSFA_RELEASE
static const int alog_release = 1;
#else
static const int alog_release = 0;
#endif
static void alog_pause(void);
// producers only (the writer never takes this lock: it reads the write position, a single aligned word, without it).
// A spinner sleeps 50 us after a few tries, so a lower-priority holder on the same core can always finish.
static inline void alog_lock(volatile int *l) { int k = 0; while (__sync_lock_test_and_set(l, 1)) { if (++k > 64) alog_pause(); } }
static inline void alog_unlock(volatile int *l) { __sync_lock_release(l); }
static void alog_wake(void);
static void alog_put(const char *s, unsigned n, int important) {   // copy n bytes into the ring, or drop them
	alog_lock(&alog_lk);
	unsigned used = alog_w - __atomic_load_n(&alog_r, __ATOMIC_ACQUIRE);   // (b37, perf: acquire / release on the two positions, for ThreadSanitizer; the barriers stay)
	if (used + n > ALOG_RING - (important ? 0 : ALOG_RESERVE)) { alog_drop += n; if (important) alog_drop_imp += n; alog_unlock(&alog_lk); return; }
	unsigned o = alog_w % ALOG_RING, a = ALOG_RING - o < n ? ALOG_RING - o : n;
	memcpy(alog_ring + o, s, a); if (a < n) memcpy(alog_ring, s + a, n - a);
	__sync_synchronize(); __atomic_store_n(&alog_w, alog_w + n, __ATOMIC_RELEASE); alog_bytes += n; if (used + n > alog_maxfill) alog_maxfill = used + n;
	alog_unlock(&alog_lk);
}
// b41: the lines a test run is read by (tools and the coordinator's checks); continuation lines of a report are not
static int alog_important(const char *s) {
	static const char *const P[] = { "TOUR", "world probe", "world model", "gpu parts", "gpu unlocked", "autoplay", "quit", "stopping", "FATAL",
		"match:", "loading map", "shader cache", "texsrc ", "dxt uploads", "texcanary:", "memory:", "results", "summary", "[log:", "log writer", NULL };
	for (int i = 0; P[i]; i++) if (!strncmp(s, P[i], strlen(P[i]))) return 1;
	return 0;
}
static void alog_drain(void);
static void alog_vprintf(const char *fmt, va_list ap) {
	char line[1024]; int n = vsnprintf(line, sizeof(line), fmt, ap);
	if (n <= 0) return;
	if (n >= (int)sizeof(line)) n = sizeof(line) - 1;
	alog_put(line, (unsigned)n, alog_important(line));
	if (!strncmp(line, "stopping", 8) || !strncmp(line, "TEXCANARY", 9) || !strncmp(line, "FATAL", 5)) alog_wake();
	if (!alog_thread_ok && alog_w - alog_r > 64 * 1024) alog_drain();   // b41: no writer thread: drain here (blocks this caller on the card)
}
#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
static SceUID alog_fd = -1, alog_th = -1, alog_sema = -1; static volatile int alog_run;
static int alog_inplace, alog_probe, alog_blanked, alog_grew, alog_grew_said; static unsigned alog_pos, alog_presize, alog_first_us, alog_nwrite, alog_grow_us; static unsigned long long alog_grow_at;   // b60
static volatile int alog_probe_req;
#define ALOG_END "\n@@@ END OF THIS RUN'S LOG (below: what is left of an earlier, longer one) @@@\n"
static unsigned long long alog_now(void) { return sceKernelGetProcessTimeWide(); }
static void alog_pause(void) { sceKernelDelayThread(50); }
static void alog_write(const char *p, unsigned n) {
	unsigned long long t0 = alog_now(); int grows = alog_inplace && !alog_grew && alog_pos + n > alog_presize;
	while (n > 0) { int r = sceIoWrite(alog_fd, p, n); if (r <= 0) break; p += r; n -= (unsigned)r; alog_pos += (unsigned)r; }
	if (alog_inplace && !alog_blanked && alog_pos < alog_presize) {   // b60: older text is left below: mark where this run's ends, stay within the old size
		static const char e[] = ALOG_END; unsigned room = alog_presize - alog_pos, m = sizeof(e) - 1;
		if (m <= room) sceIoWrite(alog_fd, e, m); else { char nl[96]; memset(nl, '\n', sizeof(nl)); sceIoWrite(alog_fd, nl, room < sizeof(nl) ? room : sizeof(nl)); }
		sceIoLseek(alog_fd, (SceOff)alog_pos, SCE_SEEK_SET); }
	unsigned d = (unsigned)(alog_now() - t0); if (d > alog_worst_us) alog_worst_us = d;
	if (!alog_nwrite++) alog_first_us = d;
	if (grows) { alog_grew = 1; alog_grow_us = d; alog_grow_at = t0; }   // the first write past the old file's end: the card allocates
}
// small settings files, written by the writer thread over the existing file (no truncation: fixed-size contents)
#define ALOG_NFILE 4
#define ALOG_FILE_MAX 2048
static struct { char path[72]; unsigned char data[ALOG_FILE_MAX]; unsigned n; int pending; } alog_file[ALOG_NFILE]; static volatile int alog_flk;
static void alog_files_write(void) {
	for (int i = 0; i < ALOG_NFILE; i++) {
		static unsigned char tmp[ALOG_FILE_MAX]; char path[72]; unsigned n = 0;   // (one writer at a time: the thread, or alog_close after it ended)
		alog_lock(&alog_flk); if (alog_file[i].pending) { n = alog_file[i].n; memcpy(tmp, alog_file[i].data, n); memcpy(path, alog_file[i].path, sizeof(path)); alog_file[i].pending = 0; } alog_unlock(&alog_flk);
		if (!n) continue;
		unsigned long long t0 = alog_now();
		SceUID fd = sceIoOpen(path, SCE_O_WRONLY, 0777); int made = 0; if (fd < 0) { fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT, 0777); made = 1; }
		int w = fd >= 0 ? sceIoWrite(fd, tmp, n) : -1; if (fd >= 0) sceIoClose(fd);
		unsigned us = (unsigned)(alog_now() - t0);
		if (w != (int)n || us > 100000 || made) { char s[200]; int k = snprintf(s, sizeof(s), "settings file %s: %d of %u bytes written %s in %u ms\n", path, w, n, made ? "to a new file" : "in place", us / 1000); alog_put(s, (unsigned)k, 0); }
	}
}
static void alog_wake(void);
static void alog_file_put(const char *path, const void *d, unsigned n) {
	if (n > ALOG_FILE_MAX) return;
	alog_lock(&alog_flk); int k = -1;
	for (int i = 0; i < ALOG_NFILE; i++) if (!strcmp(alog_file[i].path, path)) k = i;
	for (int i = 0; i < ALOG_NFILE && k < 0; i++) if (!alog_file[i].path[0]) k = i;
	if (k >= 0) { snprintf(alog_file[k].path, sizeof(alog_file[k].path), "%s", path); memcpy(alog_file[k].data, d, n); alog_file[k].n = n; alog_file[k].pending = 1; }
	alog_unlock(&alog_flk);
	if (alog_thread_ok) alog_wake(); else alog_files_write();
}
#else
static FILE *alog_hf; static volatile int alog_run; static int alog_fd = -1; static int alog_probe;
static unsigned long long alog_now(void) { return 0; }
static void alog_pause(void) { }
static void alog_write(const char *p, unsigned n) { if (alog_hf) fwrite(p, 1, n, alog_hf); }
static void alog_file_put(const char *path, const void *d, unsigned n) { FILE *f = fopen(path, "wb"); if (f) { fwrite(d, 1, n, f); fclose(f); } }   // (host: at once)
#endif
static void alog_drain(void) {   // writer side: everything up to the current write position goes to the file
	if (alog_fd < 0
#ifndef __vita__
		&& !alog_hf
#endif
		) return;
	alog_lock(&alog_dlk);
	unsigned w = __atomic_load_n(&alog_w, __ATOMIC_ACQUIRE), d = alog_drop; __sync_synchronize();   // (the producer publishes w after its bytes)
	unsigned r = alog_r;
	while (r != w) { unsigned o = r % ALOG_RING, n = w - r; if (n > ALOG_RING - o) n = ALOG_RING - o; alog_write(alog_ring + o, n); r += n; }
	__sync_synchronize(); __atomic_store_n(&alog_r, r, __ATOMIC_RELEASE);
	if (d != alog_drop_logged) { char s[160]; int n = snprintf(s, sizeof(s), "[log: %u bytes dropped so far (%u of them important lines): the ring was full while the card was busy]\n", d, alog_drop_imp); alog_write(s, (unsigned)n); alog_drop_logged = d; }
	alog_unlock(&alog_dlk);
}
#ifdef __vita__
static void alog_wake(void) { if (alog_sema >= 0) sceKernelSignalSema(alog_sema, 1); }
// b60, test runs: what does the card's first write wait for?
// At the start, before anything else is written: 4 KB over last run's probe file where it is (no allocation), then the
// old log's text blanked in place. The probe file is made at the end of the first run that finds none.
static char alog_dir[80]; static char alog_pb[16384];
static void alog_card_probe(void) {
	char path[96], s[360]; snprintf(path, sizeof(path), "%scardprobe.bin", alog_dir); memset(alog_pb, 0x5A, 4096);
	unsigned long long t0 = alog_now(); SceUID fd = sceIoOpen(path, SCE_O_WRONLY, 0777); int w = fd >= 0 ? sceIoWrite(fd, alog_pb, 4096) : -1; if (fd >= 0) sceIoClose(fd);
	unsigned long long t1 = alog_now(); unsigned left = 0, did = 0;
	alog_lock(&alog_dlk);
	if (alog_inplace && !alog_pos && alog_presize) {   // (nothing of this run is in the file yet)
		memset(alog_pb, '\n', sizeof(alog_pb)); left = alog_presize;
		while (left > 0) { int r = sceIoWrite(alog_fd, alog_pb, left < sizeof(alog_pb) ? left : sizeof(alog_pb)); if (r <= 0) break; left -= (unsigned)r; did += (unsigned)r; }
		sceIoLseek(alog_fd, 0, SCE_SEEK_SET); alog_blanked = left == 0;
	}
	alog_unlock(&alog_dlk);
	unsigned long long t2 = alog_now();
	int n = snprintf(s, sizeof(s), "card probe 1 (writer thread, %.2f s after the start): 4 KB over last run's probe file, nothing allocated: %.1f ms (%d) | the old log blanked in place: %u KB in %.1f ms%s\n",
		t0 / 1e6, (t1 - t0) / 1e3, w, did >> 10, (t2 - t1) / 1e3, alog_inplace ? (left ? " (NOT all of it: the end line marks this run's end)" : "") : " (a new log file: it allocates)");
	alog_put(s, (unsigned)n, 1);
}
// When the test tour is over (nothing else needs the card): the free-space query, then 4 KB into a NEW file (truncated or
// made: the card allocates) - the write that took 21 s at the start of b58.
static void alog_card_probe_end(void) {
	char path[96], s[420]; SceIoStat st; snprintf(path, sizeof(path), "%scardprobe.bin", alog_dir); memset(alog_pb, 0x5A, 4096);
	unsigned long long t0 = alog_now(); SceIoDevInfo di; memset(&di, 0, sizeof(di)); int dr = sceIoDevctl("ux0:", 0x3001, NULL, 0, &di, sizeof(di));
	unsigned long long t1 = alog_now(); snprintf(path, sizeof(path), "%scardprobe2.bin", alog_dir);
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777); int w = fd >= 0 ? sceIoWrite(fd, alog_pb, 4096) : -1; if (fd >= 0) sceIoClose(fd);
	unsigned long long t2 = alog_now(); int made = 0; snprintf(path, sizeof(path), "%scardprobe.bin", alog_dir);
	if (sceIoGetstat(path, &st) < 0) { fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT, 0777); if (fd >= 0) { sceIoWrite(fd, alog_pb, 4096); sceIoClose(fd); made = 1; } }
	char g[96]; if (alog_grew) snprintf(g, sizeof(g), "yes, %.1f s after the start, that write %.1f ms", alog_grow_at / 1e6, alog_grow_us / 1e3); else snprintf(g, sizeof(g), "no");
	int n = snprintf(s, sizeof(s), "card probe 2 (writer thread, %.2f s after the start): free-space query %.1f ms (0x%08x): %llu MB free of %llu MB, cluster %u KB | 4 KB into a new file (allocates): %.1f ms (%d) | the log grew past its old size before this: %s%s\n",
		t0 / 1e6, (t1 - t0) / 1e3, (unsigned)dr, (unsigned long long)(di.free_size >> 20), (unsigned long long)(di.max_size >> 20), (unsigned)(di.cluster_size >> 10), (t2 - t1) / 1e3, w, g,
		made ? " | probe file made for the next run" : "");
	alog_put(s, (unsigned)n, 1);
}
static void __attribute__((unused)) alog_probe_end(void) { if (alog_probe && alog_thread_ok) { alog_probe_req = 1; alog_wake(); } }
static int alog_thread(SceSize args, void *argp) {
	(void)args; (void)argp;
	if (alog_probe) alog_card_probe();
	while (alog_run) {
		SceUInt tmo = 100000; sceKernelWaitSema(alog_sema, 1, &tmo); alog_drain(); alog_files_write();
		if (alog_grew && !alog_grew_said) { char s[200]; int n = snprintf(s, sizeof(s), "log writer: the log grew past last run's size (%u KB) %.1f s after the start; that write took %.1f ms\n", alog_presize >> 10, alog_grow_at / 1e6, alog_grow_us / 1e3); alog_grew_said = 1; alog_put(s, (unsigned)n, 1); }
		if (alog_probe_req) { alog_probe_req = 0; alog_card_probe_end(); }
	}
	if (alog_probe_req) { alog_probe_req = 0; alog_card_probe_end(); }   // (asked for just before the end: alog_close drains its line)
	return 0;
}
static int alog_open(const char *path, int inplace) {
	{ const char *sl = strrchr(path, '/'); unsigned n = sl ? (unsigned)(sl - path) + 1 : 0; if (n >= sizeof(alog_dir)) n = sizeof(alog_dir) - 1; memcpy(alog_dir, path, n); alog_dir[n] = 0; }
	alog_fd = -1; alog_inplace = 0; alog_pos = alog_presize = 0; alog_blanked = alog_grew = alog_grew_said = 0;
	if (inplace) {   // b60: over the existing file, no truncation, no allocation while this run's text is shorter
		alog_fd = sceIoOpen(path, SCE_O_WRONLY, 0777);
		if (alog_fd >= 0) { SceOff e = sceIoLseek(alog_fd, 0, SCE_SEEK_END); sceIoLseek(alog_fd, 0, SCE_SEEK_SET); alog_presize = e > 0 ? (e > 0x7fffffff ? 0x7fffffffu : (unsigned)e) : 0; alog_inplace = 1; }
	}
	if (alog_fd < 0) alog_fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (alog_fd < 0) return -1;
	alog_sema = sceKernelCreateSema("gitsfa_log", 0, 0, 1 << 20, NULL);
	alog_run = 1;
	// b41: the lowest legal relative priority is +31 (b35-b40 asked for +40: refused on the Vita, no writer thread at all)
	static const int pr[3] = { 0x10000100 + 31, 0x10000100 + 20, 0x10000100 }; int err = 0, used = -1;
	for (int k = 0; k < 3 && alog_th < 0; k++) { alog_th = sceKernelCreateThread("gitsfa_log", alog_thread, pr[k], 0x4000, 0, 0, NULL); if (alog_th < 0) err = alog_th; else used = k; }
	if (alog_th >= 0 && sceKernelStartThread(alog_th, 0, NULL) >= 0) alog_thread_ok = 1; else alog_run = 0;   // (no thread: drained by the callers, see alog_vprintf)
	{ char s[160], e[48] = ""; if (err) snprintf(e, sizeof(e), ", refused before: 0x%08x", (unsigned)err);
	  int n = snprintf(s, sizeof(s), "log writer: %s (priority default%+d%s)\n", alog_thread_ok ? "thread running" : "NO THREAD, lines are written by the game thread",
		used >= 0 ? pr[used] - 0x10000100 : 0, e); alog_put(s, (unsigned)n, 1);
	  n = snprintf(s, sizeof(s), "log file: %s\n", alog_inplace ? alog_probe ? "written over the last one in place (no truncation; the old text is blanked first)" : "written over the last one in place (no truncation; an end line marks this run's end)" : "new (truncated)"); alog_put(s, (unsigned)n, 1); }
	return 0;
}
static void __attribute__((unused)) alog_flush(void) { alog_drain(); }
static void alog_close(void) {
	if (alog_th >= 0) { alog_run = 0; alog_wake(); sceKernelWaitThreadEnd(alog_th, NULL, NULL); sceKernelDeleteThread(alog_th); alog_th = -1; }
	alog_drain(); alog_files_write();
	if (alog_fd >= 0) { sceIoClose(alog_fd); alog_fd = -1; }
}
#else   // host builds and tests: drained by hand (alog_flush)
static void alog_wake(void) { }
static int alog_open(const char *path, int inplace) { (void)inplace; alog_hf = fopen(path, "w"); return alog_hf ? 0 : -1; }
static void alog_flush(void) { alog_drain(); if (alog_hf) fflush(alog_hf); }
static void alog_close(void) { alog_drain(); if (alog_hf) fclose(alog_hf); alog_hf = NULL; }
#endif
static int __attribute__((unused)) alog_ok(void) { return alog_fd >= 0
#ifndef __vita__
	|| alog_hf
#endif
	; }
static void alog_stats(unsigned *bytes, unsigned *dropped, unsigned *maxfill, unsigned *worst_us) {   // since the last call
	*bytes = alog_bytes; *dropped = alog_drop; *maxfill = alog_maxfill; *worst_us = alog_worst_us; alog_bytes = 0; alog_maxfill = 0; alog_worst_us = 0;
}
