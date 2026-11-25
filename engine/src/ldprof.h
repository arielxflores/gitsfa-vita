// ldprof.h (perf b45): where loading spends its time. One line per load ("LOADPROF ..."): the start-up up to the title
// screen, every map load and every match start (START / the tour's next map -> the first frame of play), split into phases.
//
// Phases nest (ldp_push / ldp_pop): a phase's time is its own (self) time, what its inner phases took is theirs. File reads
// are a phase of their own (LDP_IO), counted with their bytes and calls under the phase that asked for them:
//   - on the Vita every sceIoRead / sceIoOpen of the main thread, through the linker (CMakeLists.txt: --wrap=sceIoRead,
//     --wrap=sceIoOpen; newlib's fread / fopen end there), so stdio's buffering is seen as the card sees it ("N reads" =
//     card read calls); fopen is wrapped too, for the larger stdio buffer (below);
//   - on the host (tools/hostgame, HG_HOST) fread / fopen, wrapped the same way (build.sh): page-cache reads, so the host's
//     read times say nothing about the card; its bytes and call counts are the same as the Vita's.
// Clock: sceKernelGetSystemTimeWide (real time on the Vita; the host shim's is the real clock too, not the game's virtual one).
// Main thread only: the audio streaming and the job workers are not counted. Cost: two clock reads per phase change.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static void logp(const char *fmt, ...);   // main.c

enum {
	LDP_IO, LDP_OPEN, LDP_SCREEN,                                       // file reads, file opens, loading screen draws + swaps (vsync)
	LDP_WTEX, LDP_TEXUP, LDP_WMESH, LDP_BUFUP, LDP_SKY, LDP_UNLOAD,     // world.h: textures (parse), GL texture uploads, meshes, GL buffer uploads, sky, the old map's release
	LDP_CANARY, LDP_DXTCHK, LDP_TEXCHECK,                               // texcanary.h: its bookkeeping + verification (test builds hash), the DXT upload check (b41 fix), world.h's texture check
	LDP_COL, LDP_NAV, LDP_MINIMAP, LDP_GRADE, LDP_LIGHT, LDP_RANGE,     // the level's other files
	LDP_VM, LDP_SKINPLAN, LDP_CLIPS, LDP_CHR,                           // models: viewmodels, CPU skinning plans, animation clips, characters
	LDP_AUDIO, LDP_BOTS, LDP_MODE, LDP_SPARE,                           // sounds, bots (setup, spawns), mode setup (DEM / TC / EM), the bots' spare gun
	LDP_SHADER, LDP_GLINIT, LDP_HUD, LDP_MENU, LDP_FX, LDP_SKILLS,      // start-up: shaders, vitaGL, HUD, menus, effects, skills
	LDP_LOGOS, LDP_FRAME, LDP_BGWAIT, LDP_TUT,                          // the start-up logo movie; the first frame of play (draws, lazy loads); waiting for ldbg.h's loader thread; tutorial assets
	LDP_CHUNK,                                                          // perf (b46): world.h's chunk split (or its precomputed .gchk: hash + placing)
	LDP_N
};
static const char *const LDP_NAME[LDP_N] = {
	"read", "open", "screen", "world tex", "tex upload", "world meshes", "buf upload", "sky", "unload",
	"canary", "dxt check", "texcheck", "collision", "nav", "minimap", "grade", "light", "range",
	"viewmodels", "skin plans", "clips", "characters", "audio", "bots", "mode", "spare gun",
	"shaders", "vitagl", "hud", "menus", "fx", "skills", "logos", "first frame", "loader wait", "tutorial", "chunk split"
};
#define LDP_DEPTH 16
static struct {
	int on, sp, stack[LDP_DEPTH], main_tid; uint64_t t0, mark;
	uint64_t us[LDP_N], bytes[LDP_N]; unsigned calls[LDP_N], reads[LDP_N], nread, nopen; uint64_t rbytes;   // reads: card read calls under the phase
	unsigned ra_files, ra_packed; uint64_t ra_bytes, ra_us, ra_raw;   // ldbg.h's read-ahead: files, bytes from the card, the reader thread's read time; perf b56: packed files and their unpacked bytes
	char what[48], note[360];   // note: the loader / read-ahead threads' own times (ldbg.h), printed after the phases
} LDP;
static inline uint64_t ldp_now(void) { return sceKernelGetSystemTimeWide(); }
// the threads that load (main, ldbg.h's loader): their read-only stdio files get a 64 KB buffer (the fopen wrapper below)
static int ldp_tid_main, ldp_tid_aux = -1;
#define LDP_FBUF (64 * 1024)
static int ldp_loading_thread(void) { int t = sceKernelGetThreadId(); return (ldp_tid_main && t == ldp_tid_main) || t == ldp_tid_aux; }
static inline int ldp_main(void) { return LDP.on && sceKernelGetThreadId() == LDP.main_tid; }
static void ldp_push(int id) {
	if (!ldp_main() || LDP.sp >= LDP_DEPTH) return;
	uint64_t t = ldp_now(); if (LDP.sp > 0) LDP.us[LDP.stack[LDP.sp - 1]] += t - LDP.mark;
	LDP.stack[LDP.sp++] = id; LDP.calls[id]++; LDP.mark = t;
}
static void ldp_pop(void) {
	if (!ldp_main() || LDP.sp <= 0) return;
	uint64_t t = ldp_now(); LDP.us[LDP.stack[--LDP.sp]] += t - LDP.mark; LDP.mark = t;
}
static int ldp_top(void) { return LDP.sp > 0 ? LDP.stack[LDP.sp - 1] : -1; }
// a new account: what = "boot", "map zioplant", "match zioplant" ...
static void ldp_start(const char *what) {
	int tid = sceKernelGetThreadId(); memset(&LDP, 0, sizeof(LDP)); LDP.main_tid = tid; LDP.on = 1; if (!ldp_tid_main) ldp_tid_main = tid;
	snprintf(LDP.what, sizeof(LDP.what), "%s", what); LDP.t0 = LDP.mark = ldp_now();
}
static void ldp_note(const char *s) {   // appended to the account's line (several notes add up)
	if (!LDP.on) return; size_t n = strlen(LDP.note); snprintf(LDP.note + n, sizeof(LDP.note) - n, " %s", s);
}
static void ldp_io(uint64_t us, long got, int open) {   // a file read / open of the main thread (the wrappers below)
	int top = ldp_top(); if (top >= 0 && top != LDP_IO && top != LDP_OPEN) { LDP.bytes[top] += got > 0 ? (uint64_t)got : 0; if (!open) LDP.reads[top]++; }
	if (open) LDP.nopen++; else { LDP.nread++; LDP.rbytes += got > 0 ? (uint64_t)got : 0; }
	(void)us;
}
// one line: total, then the phases from the largest down (those >= 1 ms), with the MB read under each
static void ldp_report(void) {
	if (!LDP.on) return;
	while (LDP.sp > 0) ldp_pop();
	uint64_t tot = ldp_now() - LDP.t0, sum = 0; for (int i = 0; i < LDP_N; i++) sum += LDP.us[i];
	char b[1400]; int k = snprintf(b, sizeof(b), "LOADPROF %s: %.2f s | read %.2f MB in %u reads + %u opens, %.0f ms",
		LDP.what, tot / 1e6, LDP.rbytes / 1048576.0, LDP.nread, LDP.nopen, (LDP.us[LDP_IO] + LDP.us[LDP_OPEN]) / 1e3);
	int ord[LDP_N], n = 0; for (int i = 0; i < LDP_N; i++) if (i != LDP_IO && i != LDP_OPEN && LDP.us[i] >= 1000) ord[n++] = i;
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && LDP.us[ord[j]] > LDP.us[ord[j - 1]]; j--) { int t = ord[j]; ord[j] = ord[j - 1]; ord[j - 1] = t; }
	for (int i = 0; i < n && k < (int)sizeof(b) - 64; i++) { int p = ord[i];
		k += snprintf(b + k, sizeof(b) - k, " | %s %.0f ms", LDP_NAME[p], LDP.us[p] / 1e3);
		if (LDP.bytes[p] >= 65536) k += snprintf(b + k, sizeof(b) - k, " (%.1f MB, %u reads)", LDP.bytes[p] / 1048576.0, LDP.reads[p]); }
	if (k < (int)sizeof(b) - 40) k += snprintf(b + k, sizeof(b) - k, " | other %.0f ms", tot > sum ? (tot - sum) / 1e3 : 0.0);
	if (LDP.ra_files && k < (int)sizeof(b) - 90) { k += snprintf(b + k, sizeof(b) - k, " || read-ahead %u files, %.1f MB, %.0f ms reading on its thread", LDP.ra_files, LDP.ra_bytes / 1048576.0, LDP.ra_us / 1e3); if (LDP.ra_packed && k < (int)sizeof(b) - 60) k += snprintf(b + k, sizeof(b) - k, " (%u packed: %.1f MB unpacked)", LDP.ra_packed, LDP.ra_raw / 1048576.0); }
	if (LDP.note[0] && k < (int)sizeof(b) - 8) k += snprintf(b + k, sizeof(b) - k, " ||%s", LDP.note);
	logp("%s\n", b);
	LDP.on = 0;
}
// file reads and opens of the main thread, timed (see the top). fopen: newlib's stdio buffer is ~1 KB on the Vita, so a file
// read in small pieces (headers, records) cost one card read per KB (b45 emulator: 64857 reads for 102 MB at start-up); a
// read-only file opened by a loading thread gets LDP_FBUF instead (reads bigger than it still go straight through).
static FILE *ldp_fbuf(FILE *f, const char *mode) { if (f && mode && mode[0] == 'r' && !strchr(mode, '+') && ldp_loading_thread()) setvbuf(f, NULL, _IOFBF, LDP_FBUF); return f; }
#ifdef HG_HOST
size_t __real_fread(void *p, size_t s, size_t n, FILE *f); FILE *__real_fopen(const char *path, const char *mode);
size_t __wrap_fread(void *p, size_t s, size_t n, FILE *f) {
	if (!ldp_main()) return __real_fread(p, s, n, f);
	ldp_push(LDP_IO); size_t r = __real_fread(p, s, n, f); ldp_pop(); ldp_io(0, (long)(r * s), 0); return r;
}
FILE *__wrap_fopen(const char *path, const char *mode) {
	if (!ldp_main()) return ldp_fbuf(__real_fopen(path, mode), mode);
	ldp_push(LDP_OPEN); FILE *r = __real_fopen(path, mode); ldp_pop(); ldp_io(0, 0, 1); return ldp_fbuf(r, mode);
}
#else
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode) { return ldp_fbuf(__real_fopen(path, mode), mode); }   // (the open itself is timed in sceIoOpen below)
int __real_sceIoRead(SceUID fd, void *data, SceSize size); SceUID __real_sceIoOpen(const char *file, int flags, SceMode mode);
int __wrap_sceIoRead(SceUID fd, void *data, SceSize size) {
	if (!ldp_main()) return __real_sceIoRead(fd, data, size);
	ldp_push(LDP_IO); int r = __real_sceIoRead(fd, data, size); ldp_pop(); ldp_io(0, r, 0); return r;
}
SceUID __wrap_sceIoOpen(const char *file, int flags, SceMode mode) {
	if (!ldp_main()) return __real_sceIoOpen(file, flags, mode);
	ldp_push(LDP_OPEN); SceUID r = __real_sceIoOpen(file, flags, mode); ldp_pop(); ldp_io(0, 0, 1); return r;
}
#endif
// for the engine's other headers (each defines both as nothing when ldprof.h is not included: the host test harnesses)
#define LDP_PUSH(id) ldp_push(id)
#define LDP_POP() ldp_pop()
