// cpuprof.h (perf, b35): where the match loop's CPU time goes, once a second in boot.log:
//   cpu: <segment> <ms> | ... | busy <ms> + swap wait <ms> = frame <ms> | <subsystem> <ms> ... | jobs <ms> in <n> calls
// all in ms per frame, averaged over the second. Two sources:
//  - the frame segments of frametime.h (render's FT() marks; main.c calls cp_ft_collect() just before tc_tick, so each
//    frame's segments are summed by name: world draw, characters, HUD draw, the swap wait ...)
//  - accumulators for the subsystems inside the segments (CP_T0 / CP_ADD around a block; ~0.3 us per pair on the Vita):
//    bots (total, navigation, physics, animation state, pose evaluation), player physics, fx update, vision rays, audio feed ...
// Host builds (tools/perf/host) use the same accumulators with clock_gettime. Thread note: the accumulators belong to the
// game thread; a jobs.h job never adds to them (the job's caller times the whole fork-join).
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#ifdef __vita__
#include <psp2/kernel/processmgr.h>
static inline unsigned long long cp_us(void) { return sceKernelGetProcessTimeWide(); }
#else
#include <time.h>
static inline unsigned long long cp_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long long)t.tv_sec * 1000000ull + t.tv_nsec / 1000; }
#endif
enum { CP_BOTS, CP_BOT_NAV, CP_BOT_PHYS, CP_BOT_ANIM, CP_BOT_POSE, CP_BOT_RAYS, CP_PLAYER, CP_FX, CP_VIS_RAYS, CP_AUDIO, CP_HUD_PREP, CP_N };
static const char *const CP_NAME[CP_N] = { "bots", "nav", "bot physics", "anim state", "pose eval", "AI rays", "player physics", "fx update", "vision rays", "audio feed", "HUD prep" };
static unsigned long long cp_acc[CP_N]; static unsigned cp_cnt[CP_N];
#define CP_T0(v) unsigned long long v = cp_us()
#define CP_ADD(id, t0) do { cp_acc[id] += cp_us() - (t0); cp_cnt[id]++; } while (0)
// frame segments by name (the FT mark strings are literals: compared by pointer, then by text)
#define CP_NSEG 24
static struct { const char *n; unsigned long long us; } cp_seg[CP_NSEG]; static int cp_nseg, cp_frames;
static void cp_seg_add(const char *n, unsigned long long us) {
	int k = 0; while (k < cp_nseg && cp_seg[k].n != n) k++;
	if (k == cp_nseg) { k = 0; while (k < cp_nseg && strcmp(cp_seg[k].n, n)) k++; }
	if (k == cp_nseg) { if (cp_nseg >= CP_NSEG) return; cp_seg[cp_nseg].n = n; cp_seg[cp_nseg].us = 0; cp_nseg++; }
	cp_seg[k].us += us;
}
#ifdef FT_MAXM
// main.c: once a frame, just before tc_tick (which resets the marks): the segments since the last tick
static void cp_ft_collect(void) {
	unsigned long long prev = ft_t0, now = tc_us();
	if (!prev) return;
	for (int i = 0; i < ft_nm; i++) { cp_seg_add(ft_m[i].n, ft_m[i].t - prev); prev = ft_m[i].t; }
	cp_seg_add("logic up to the canary", now - prev);
	cp_frames++;
}
#endif
// short names for the FT marks (the long ones say what ran since the previous mark)
static const char *cp_short(const char *n) {
	static const char *const M[][2] = { { "input, menus, player movement", "input+player" }, { "weapons, hits, sound, fx update", "weapons+fx" },
		{ "bots AI", "bots" }, { "modes, tutorial, camera", "modes+camera" }, { "world draw", "world" }, { "characters, shadows, tanks", "chars" },
		{ "bomb, drops, skills, range, effects", "effects" }, { "viewmodel, HUD state", "viewmodel+HUDstate" }, { "HUD draw", "HUD" },
		{ "draws until the swap", "screens" }, { "vglSwapBuffers (GPU / vsync wait)", "swap wait" }, { "logic up to the canary", "to canary" } };
	for (unsigned k = 0; k < sizeof(M) / sizeof(M[0]); k++) if (!strcmp(M[k][0], n)) return M[k][1];
	return n;
}
static int cp_is_wait(const char *n) { return !strcmp(n, "vglSwapBuffers (GPU / vsync wait)"); }
// the per-second lines (frames: frames since the last call; lg: logp, called once per line: logp flushes every call).
// Resets everything.
static void cp_print(int frames, void (*lg)(const char *, ...)) {
	static char b[1024]; int n = 0;
	#define CP_PUT(...) do { if (n < (int)sizeof(b) - 1) { int r_ = snprintf(b + n, sizeof(b) - n, __VA_ARGS__); if (r_ > 0) n += r_; if (n > (int)sizeof(b) - 1) n = sizeof(b) - 1; } } while (0)
	if (frames <= 0) frames = 1;
	float fr = cp_frames > 0 ? (float)cp_frames : (float)frames;
	if (cp_nseg) {
		unsigned long long busy = 0, wait = 0;
		CP_PUT("cpu:");
		for (int k = 0; k < cp_nseg; k++) { CP_PUT(" %s %.2f |", cp_short(cp_seg[k].n), cp_seg[k].us / 1000.f / fr); if (cp_is_wait(cp_seg[k].n)) wait += cp_seg[k].us; else busy += cp_seg[k].us; }
		CP_PUT(" busy %.2f + swap wait %.2f = frame %.2f ms\n", busy / 1000.f / fr, wait / 1000.f / fr, (busy + wait) / 1000.f / fr);
		lg("%s", b); n = 0;
	}
	CP_PUT("cpu parts (ms/frame):");
	for (int k = 0; k < CP_N; k++) if (cp_cnt[k]) CP_PUT(" %s %.3f (%.1f/frame),", CP_NAME[k], cp_acc[k] / 1000.f / frames, cp_cnt[k] / (float)frames);
#ifdef JOBS_MAX
	{ unsigned long long jb, jc; jobs_stats(&jb, &jc); CP_PUT(" jobs %.3f in %.1f calls/frame on %d workers", jb / 1000.f / frames, jc / (float)frames, jobs_nworkers()); }
#endif
	CP_PUT("\n"); lg("%s", b);
	#undef CP_PUT
	memset(cp_acc, 0, sizeof(cp_acc)); memset(cp_cnt, 0, sizeof(cp_cnt)); cp_nseg = 0; cp_frames = 0;
}
