// Frame timers (render, b34/b35): what a frame spends its time on. Included by texcanary.h (after its own functions).
//   FT("what ran since the previous mark")  -- marks in main.c's match loop (each closes the segment before it)
//   vglSwapBuffers and glFinish are timed by their wrappers (two automatic marks around every swap);
//   fwrite / fflush / fclose are timed by theirs: the slowest file call of the frame is named, with file:line (not fread:
//   main.c has its own fread macro for its pak reader); glDrawElements / glDrawArrays are counted (calls, indices) and
//   every 8th one is timed (the CPU cost of vitaGL's draw path).
// tc_tick calls ft_frame once a frame:
//   - a frame over 0.2 s (not a map load) is logged:
//     FRAMETIME t=... a frame took 1.81 s: <segment> <ms>; ... | io: N calls X ms (slowest: fflush 1795 ms at main.c:39) |
//     glFinish ms | graphics memory free
//   - once a second (test runs: alog_verbose), the GPU-side line (the averages of frame / busy / swap wait are in the perf
//     agent's "cpu:" line, cpuprof.h, built from the same FT marks; this one adds what that one cannot see):
//     perf gpu: fragment G ms/scene (worst, scenes) | worst frame W ms (its cpu and swap wait) | draws, indices, draw-call
//     CPU ms | fx fill | log bytes, ring peak, dropped, worst card write
//     frame = tc_tick to tc_tick; swap = time inside vglSwapBuffers (scene end + waiting for a free display buffer: GPU or
//     vsync bound); cpu = frame - swap. gpu fragment = from vitaGL's per-scene fragment notification (query_fence, written
//     by the GPU when a scene's pixels are done; a watcher thread on a spare core polls it every 0.25 ms): per scene,
//     done(n) - max(submitted(n), done(n-1)) = how long the GPU's fragment work of that scene took. A map is GPU-bound
//     when gpu fragment is near 16.7 ms, CPU-bound when cpu is.
#pragma once
#include "alog.h"
#define FT_MAXM 28
static struct { const char *n; unsigned long long t; } ft_m[FT_MAXM]; static int ft_nm;
static unsigned long long ft_t0, ft_fin_us, ft_io_us, ft_io_worst; static int ft_io_n, ft_io_l, ft_nlog; static const char *ft_io_op, *ft_io_f; static unsigned ft_io_b;
static float ft_worst; static int ft_nslow;
// b38 safety net (all builds): ft_gpu_over = share of recent frames that missed 60 fps while vglSwapBuffers waited
// (no free display buffer: the GPU is behind), smoothed over ~1 s; fx.h shrinks its fill budget while it is high
static float ft_gpu_over, ft_fx_scale = 1, ft_fx_scale_min = 1;
static float ft_fx_fill_max; static int ft_fx_cut, ft_fx_cut0;
static float ft_fx_trim_q, ft_fx_trim_p, ft_fx_trim_worst; static int ft_fx_trim_n;   // b81 fx.h trimming this second: quad / polygon screens of the trimmed particles (sum over frames), the most saved in one frame, how many   // fx.h: the worst frame's world-effect fill (screens) this second, particles left out (total)
static unsigned long long ft_sw_us, ft_dc_us; static unsigned ft_dc, ft_dv, ft_dk;   // this frame: swap time, draw calls (timed sample), indices
static struct { float t0; unsigned n, dmax, scenes, gn; double fr, frmax, cpu, cpumax, sw, gpu, gpumax, dc_ms, cpu_at_max, sw_at_max; unsigned long long draws, idx; } ft_s;
// b38: the parts probe (world.h, test runs): the draws of one frame segment can be left out, to measure its GPU cost.
// ft_skip_after names the FT mark that opens the segment (main.c's names); the next mark closes it.
static const char *ft_skip_after; static int ft_skipping;
static void ft_mark(const char *n) {
	if (ft_nm < FT_MAXM) { ft_m[ft_nm].n = n; ft_m[ft_nm].t = tc_us(); ft_nm++; }
	ft_skipping = ft_skip_after && !strcmp(n, ft_skip_after);
}
#define FT(n) ft_mark(n)
static void ft_io(unsigned long long t0, const char *op, unsigned b, const char *f, int l) {
	unsigned long long d = tc_us() - t0; ft_io_us += d; ft_io_n++;
	if (d >= ft_io_worst) { ft_io_worst = d; ft_io_op = op; ft_io_b = b; ft_io_f = f; ft_io_l = l; }
}
static size_t tc_fwrite(const void *p, size_t s, size_t n, FILE *fp, const char *f, int l) { unsigned long long t0 = tc_us(); size_t r = fwrite(p, s, n, fp); ft_io(t0, "fwrite", (unsigned)(s * n), f, l); return r; }
static int __attribute__((unused)) tc_fflush(FILE *fp, const char *f, int l) { unsigned long long t0 = tc_us(); int r = fflush(fp); ft_io(t0, "fflush", 0, f, l); return r; }
static int tc_fclose(FILE *fp, const char *f, int l) { unsigned long long t0 = tc_us(); int r = fclose(fp); if (TC_MAIN_THREAD()) ft_io(t0, "fclose", 0, f, l); return r; }   // (perf b45: the main thread's counters)
static void tc_glFinish(void) { unsigned long long t0 = tc_us(); glFinish(); ft_fin_us += tc_us() - t0; }
static void tc_glDrawElements(GLenum m, GLsizei c, GLenum t, const void *i) {
	if (ft_skipping) return;   // b38 parts probe
	if ((++ft_dc & 7) == 0) { unsigned long long t0 = tc_us(); glDrawElements(m, c, t, i); ft_dc_us += tc_us() - t0; ft_dk++; } else glDrawElements(m, c, t, i);
	ft_dv += (unsigned)c;
}
static void tc_glDrawArrays(GLenum m, GLint f, GLsizei c) {
	if (ft_skipping) return;   // b38 parts probe
	if ((++ft_dc & 7) == 0) { unsigned long long t0 = tc_us(); glDrawArrays(m, f, c); ft_dc_us += tc_us() - t0; ft_dk++; } else glDrawArrays(m, f, c);
	ft_dv += (unsigned)c;
}
// ---- the GPU's per-scene fragment notification (vitaGL: query_fence, value + 1 at every sceGxmEndScene)
#define FT_NQ 64
static unsigned long long ft_qsub[FT_NQ]; static unsigned ft_qsubv[FT_NQ], ft_qseen;
// b38: a tag per scene (world.h's grading probe: the program set it drew), so the probe can report GPU fragment ms per set
#define FT_TAGS 19   // 0-3: grading probe sets, 8-13: parts probe variants, 16 / 17: world reflections on / off (b67), 18: anisotropic filtering off (b69)
static int ft_tag = -1; static signed char ft_qtag[FT_NQ]; static double ft_tag_ms[FT_TAGS]; static unsigned ft_tag_n[FT_TAGS];
static volatile unsigned long long ft_qdone[FT_NQ]; static volatile unsigned ft_qlast;
#if defined(__vita__) && !defined(GITSFA_RELEASE)
extern SceGxmNotification query_fence;   // libvitaGL.a (buffers.c): passed as the fragment notification of every scene
static volatile int ft_qrun; static SceUID ft_qth = -1;
static int ft_qthread(SceSize a, void *p) {
	(void)a; (void)p;
	while (ft_qrun) {
		unsigned v = *(volatile unsigned *)query_fence.address;
		if (v != ft_qlast) { unsigned long long now = sceKernelGetProcessTimeWide(); int g = 0;
			for (unsigned u = ft_qlast + 1; (int)(v - u) >= 0 && g < FT_NQ; u++, g++) ft_qdone[u % FT_NQ] = now;
			__sync_synchronize(); ft_qlast = v; }
		sceKernelDelayThread(250);
	}
	return 0;
}
static void ft_qstart(void) {   // on a spare core (3 with CapUnlocker, else 2, else any), above the job workers: it only polls
	if (ft_qth >= 0 || !query_fence.address) return;
	ft_qlast = ft_qseen = *(volatile unsigned *)query_fence.address; ft_qrun = 1;
	static const int mask[3] = { SCE_KERNEL_CPU_MASK_USER_0 << 3, SCE_KERNEL_CPU_MASK_USER_0 << 2, 0 };
	for (int k = 0; k < 3 && ft_qth < 0; k++) { SceUID t = sceKernelCreateThread("gitsfa_gpuwatch", ft_qthread, 0x10000100 - 4, 0x2000, 0, mask[k], NULL); if (t >= 0 && sceKernelStartThread(t, 0, NULL) >= 0) ft_qth = t; }
	if (ft_qth < 0) ft_qrun = 0;
}
static unsigned ft_qvalue(void) { return query_fence.value; }
#else
static void ft_qstart(void) { }
static unsigned ft_qvalue(void) { return 0; }
#endif
// b41: GPU work that is not vsync-bound (device b40: with vsync every scene's fragment time reads ~16.5 ms, its wait for a
// display buffer included). world.h's probe windows (test runs only) set ft_unlock: vsync off (vglWaitVblankStart) and
// the CPU paces the frames to 60 Hz instead, so the frame rate (and the TOUR statistics) stay as they are, the GPU never
// waits for a buffer, and the per-scene time below is the scene's own GPU time. The picture may tear in those windows.
static int ft_unlock; static unsigned long long ft_pace_t;
static void ft_set_unlock(int on) {
	if (on == ft_unlock) return;
	ft_unlock = on; ft_pace_t = 0;
#if defined(__vita__) && defined(__arm__)
	vglWaitVblankStart(on ? GL_FALSE : GL_TRUE);
#endif
}
static void tc_vglSwapBuffers(GLboolean cd) {
	ft_mark("draws until the swap");
	unsigned long long t0 = tc_us(); vglSwapBuffers(cd);
#if defined(__vita__) && defined(__arm__)
	if (ft_unlock) {   // b41: 60 Hz by the CPU while vsync is off
		unsigned long long now = tc_us(), next = ft_pace_t + 16667;
		if (ft_pace_t && now < next) { sceKernelDelayThread((SceUInt)(next - now)); ft_pace_t = next; } else ft_pace_t = now;
	}
#endif
	ft_sw_us += tc_us() - t0;
	unsigned v = ft_qvalue(); ft_qsub[v % FT_NQ] = t0; ft_qsubv[v % FT_NQ] = v; ft_qtag[v % FT_NQ] = (signed char)ft_tag;   // this scene was submitted when the swap began
	ft_mark("vglSwapBuffers (GPU / vsync wait)");
}
static void ft_second(float now) {   // the per-second timing line
	if (alog_verbose && ft_s.n) {
		unsigned bytes, dropped, peak, wus; alog_stats(&bytes, &dropped, &peak, &wus);
		double n = ft_s.n;
		logp("perf gpu: fragment %.2f ms/scene (worst %.1f, %u scenes) | worst frame %.1f ms (cpu %.1f, swap wait %.1f in it) | draws %.0f/frame (max %u), %.1fk indices/frame, draw calls ~%.2f ms CPU/frame | fx fill worst %.1f screens, %d particles cut, budget x%.2f (GPU-behind frames %.0f%%), trimmed %d: %.1f -> %.1f screens (worst frame -%.2f) | log %u B/s, ring peak %u KB, dropped %u B, card write worst %u ms\n",
			ft_s.gn ? ft_s.gpu / ft_s.gn : 0.0, ft_s.gpumax, ft_s.scenes, ft_s.frmax, ft_s.cpu_at_max, ft_s.sw_at_max,
			ft_s.draws / n, ft_s.dmax, ft_s.idx / n / 1000.0, ft_s.dc_ms / n, ft_fx_fill_max, ft_fx_cut - ft_fx_cut0, ft_fx_scale_min, ft_gpu_over * 100, ft_fx_trim_n, ft_fx_trim_q, ft_fx_trim_p, ft_fx_trim_worst, bytes, peak >> 10, dropped, wus / 1000);
	}
	memset(&ft_s, 0, sizeof(ft_s)); ft_s.t0 = now; ft_fx_fill_max = 0; ft_fx_cut0 = ft_fx_cut; ft_fx_scale_min = ft_fx_scale; ft_fx_trim_q = ft_fx_trim_p = ft_fx_trim_worst = 0; ft_fx_trim_n = 0;
}
static void ft_frame(int slow, float dt) {   // tc_tick, once a frame: log a slow frame, account the frame, then start the next
	unsigned long long now = tc_us();
	if (slow) { ft_nslow++; if (dt > ft_worst) ft_worst = dt; }
	if (slow && ft_nlog < 40) {
		ft_nlog++;
		logp("FRAMETIME t=%.1f: a frame took %.2f s:", tc_now, dt);
		unsigned long long prev = ft_t0;
		for (int i = 0; i < ft_nm; i++) { unsigned long long d = ft_m[i].t - prev; if (d >= 1000) logp(" %s %.1f ms;", ft_m[i].n, d / 1000.0); prev = ft_m[i].t; }
		logp(" (game logic up to the canary) %.1f ms", (now - prev) / 1000.0);
		if (ft_nm >= FT_MAXM) logp(" (marks overflowed)");
		logp("\n  io: %d calls %.1f ms", ft_io_n, ft_io_us / 1000.0);
		if (ft_io_op) logp(" (slowest: %s %u B at %s:%d, %.1f ms)", ft_io_op, ft_io_b, tc_base(ft_io_f), ft_io_l, ft_io_worst / 1000.0);
		logp("; glFinish %.1f ms; draws %u; graphics free: vram %u KB, ram %u KB\n", ft_fin_us / 1000.0, ft_dc, (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 10), (unsigned)(vglMemFree(VGL_MEM_RAM) >> 10));
	}
	if (dt > 0 && dt < 0.2f) {   // b38 safety net: GPU-behind frames -> effects budget (fx.h), back up when they stop
		int over = dt > 0.0175f && ft_sw_us > 2500;
		ft_gpu_over += (over - ft_gpu_over) * 0.03f;
		if (ft_gpu_over > 0.2f) { ft_fx_scale -= 0.01f; if (ft_fx_scale < 0.5f) ft_fx_scale = 0.5f; }
		else if (ft_gpu_over < 0.05f) { ft_fx_scale += 0.005f; if (ft_fx_scale > 1) ft_fx_scale = 1; }
		if (ft_fx_scale < ft_fx_scale_min) ft_fx_scale_min = ft_fx_scale;
	}
	// the per-second statistics (frames of a map load are left out)
	if (dt > 0 && !slow && dt < 0.2f) {
		double fr = dt * 1000.0, sw = ft_sw_us / 1000.0, cpu = fr - sw;
		ft_s.n++; ft_s.fr += fr; if (fr > ft_s.frmax) { ft_s.frmax = fr; ft_s.cpu_at_max = cpu; ft_s.sw_at_max = sw; } ft_s.cpu += cpu; if (cpu > ft_s.cpumax) ft_s.cpumax = cpu; ft_s.sw += sw;
		ft_s.draws += ft_dc; ft_s.idx += ft_dv; if (ft_dc > ft_s.dmax) ft_s.dmax = ft_dc; ft_s.dc_ms += ft_dk ? ft_dc_us / 1000.0 * ft_dc / ft_dk : 0;
	}
	ft_qstart();
	{ unsigned last = ft_qlast; __sync_synchronize(); int g = 0;
		for (unsigned u = ft_qseen + 1; (int)(last - u) >= 0 && g < FT_NQ; u++, g++) {
			unsigned long long d = ft_qdone[u % FT_NQ], p = ft_qdone[(u - 1) % FT_NQ], st = p;
			if (ft_qsubv[u % FT_NQ] == u && ft_qsub[u % FT_NQ] > st) st = ft_qsub[u % FT_NQ];
			if (d > st && d - st < 200000) { double ms = (d - st) / 1000.0; ft_s.gpu += ms; ft_s.gn++; if (ms > ft_s.gpumax) ft_s.gpumax = ms;
				int tg = ft_qsubv[u % FT_NQ] == u ? ft_qtag[u % FT_NQ] : -1; if (tg >= 0 && tg < FT_TAGS) { ft_tag_ms[tg] += ms; ft_tag_n[tg]++; } }
			ft_s.scenes++; }
		ft_qseen = last; }
	if (tc_now - ft_s.t0 >= 1.0f || tc_now < ft_s.t0) ft_second(tc_now);
	ft_nm = 0; ft_t0 = now; ft_io_us = ft_io_worst = 0; ft_io_n = 0; ft_io_op = NULL; ft_fin_us = 0; ft_sw_us = 0; ft_dc = ft_dv = ft_dk = 0; ft_dc_us = 0;
}
#define fwrite(p, s, n, fp) tc_fwrite(p, s, n, fp, __FILE__, __LINE__)
#define fflush(fp) tc_fflush(fp, __FILE__, __LINE__)
#define fclose(fp) tc_fclose(fp, __FILE__, __LINE__)
#define glFinish() tc_glFinish()
#define vglSwapBuffers(cd) tc_vglSwapBuffers(cd)
#define glDrawElements(m, c, t, i) tc_glDrawElements(m, c, t, i)
#define glDrawArrays(m, f, c) tc_glDrawArrays(m, f, c)
