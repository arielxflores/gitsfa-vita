// Video playback for the tutorial (sceAvPlayer, H.264 .mp4): the start-up logos and the intro cut scene full screen,
// the skill explanation clips inside their panel. One player at a time, opened and closed on the game thread.
//   tv_open(path, loop, audio) -> 0 ok, < 0 failed (the caller shows its fallback: a still or nothing). Every failing
//                                 step is logged with its own code: -1 file, -2 sysmodule, -3 sceAvPlayerInit, -4 the frame
//                                 memory arena, -5 sceAvPlayerAddSource
//   tv_frame()                 -> the texture holding the newest decoded frame (0 until the first one); call once a frame
//   tv_done()                  -> 1 when a non-looping video has played to the end (or the player stopped)
//   tv_close()
// Memory:
//   - main.c's vitaGL init (H2) leaves the whole physically contiguous partition (phycont, 18 MB on the device) and
//     TV_RAM_FREE of main RAM to the system; the game never uses phycont.
//   - the player's general allocations (allocate / deallocate): newlib memalign / free (thread-safe), counted.
//   - frame memory (allocateTexture / deallocateTexture: the decoder's frames, written by the hardware decoder and
//     sampled by the GPU): tut_vmem.h - an arena from vitaGL's CDRAM pool taken on the game thread before the player
//     exists, then physically contiguous memblocks (phycont, then CDRAM) taken by the callback for whatever does not fit.
//     b40 (device): the arena alone was 12 MB, the second request did not fit -> "stopped without a frame". The
//     callbacks make no vitaGL calls (they may run on the player's threads); everything is freed in tv_close, after the
//     player is closed and the GPU is done. Every request (callback, size, alignment, where it went or both kernel errors)
//     is logged by the game thread, and tv_close logs a summary line (frames shown, peak frame memory, where it was).
//     Cost to a match: none - the arena exists only while a movie plays (the logos at boot, the intro before the first
//     program; a clip's is TV_ARENA_CLIP at most, inside a tutorial program) and phycont is not used by the game.
// The decoded frames (NV12) are sampled directly: the GL texture's SceGxmTexture is re-pointed at the frame
// (YVU420P2_CSC1, the GPU converts to RGB) - no copy. On close the textures are re-pointed at a valid black image before
// the frame memory is freed (no texture is ever left pointing at freed memory). See /root/work/CORRUPTION.md.
// Audio (intro only): the player's PCM goes to its own sceAudioOut port (the game's mixer keeps the other one).
// Encoding (tools/tutorial/enc_clips.sh, make_tut_logos.py): H.264 Baseline, no B-frames, yuv420p, width a multiple of
// 64 and height of 16 (the decoder's frame pitch = width: no stride), AAC LC 48 kHz stereo.
#pragma once
#define TV_PHYCONT_FREE (64 * 1024 * 1024)   // > the whole phycont partition: vitaGL keeps none of it (sceAvPlayer's decoder)
#define TV_RAM_FREE (14 * 1024 * 1024)       // main RAM vitaGL leaves to the system (b106: was 24 MB, of which the logs never show more than 2 MB taken; the other 10 MB are the heap's now, main.c _newlib_heap_size_user)
static struct { int on, done, frames, w, h, idx, audio, err; GLuint tex[2]; float vol; char name[48]; } TV;
#ifdef __vita__
#include <psp2/avplayer.h>
#include <psp2/sysmodule.h>
#include <psp2/audioout.h>
#include <psp2/gxm.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/sysmem.h>
#include <malloc.h>
static SceAvPlayerHandle tv_h; static int tv_mod; static SceUID tv_th = -1; static int tv_port = -1; static volatile int tv_arun;
static SceUInt64 tv_t0; static int tv_warned;
static char *tv_block_alloc(int kind, unsigned size, unsigned align, int *uid);
#define TVM_BLOCK_ALLOC(kind, size, align, uid) tv_block_alloc(kind, size, align, uid)
#define TVM_BLOCK_FREE(uid) sceKernelFreeMemBlock(uid)
#define TVM_GPU_MAP(base, size) sceGxmMapMemory(base, size, SCE_GXM_MEMORY_ATTRIB_RW)
#define TVM_GPU_UNMAP(base) sceGxmUnmapMemory(base)
#define TVM_ARENA_FREE(raw) vglFree(raw)
#define TVM_YIELD() sceKernelDelayThread(50)
#include "tut_vmem.h"
static char *tv_block_alloc(int kind, unsigned size, unsigned align, int *uid) {   // (tut_vmem.h, any thread: kernel calls only)
	SceKernelMemBlockType t = kind == TVK_PHYCONT ? SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW : kind == TVK_PHYCONT_C ? SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW : SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW;
	SceKernelAllocMemBlockOpt o; memset(&o, 0, sizeof(o)); o.size = sizeof(o); o.attr = SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT; o.alignment = align;
	SceUID b = sceKernelAllocMemBlock("gitsfa_movie", t, size, align > 4096 ? &o : NULL);
	if (b < 0 && align > 4096) { SceUID b2 = sceKernelAllocMemBlock("gitsfa_movie", t, size, NULL);   // (no alignment option: fine if the base is aligned anyway)
		if (b2 >= 0) { void *p2 = NULL; sceKernelGetMemBlockBase(b2, &p2); if (p2 && !((uintptr_t)p2 & (align - 1))) b = b2; else sceKernelFreeMemBlock(b2); } }
	if (b < 0) { *uid = b; return NULL; }
	void *p = NULL; sceKernelGetMemBlockBase(b, &p); if (!p) { sceKernelFreeMemBlock(b); *uid = -1; return NULL; }
	*uid = b; return (char *)p;
}
// frame memory mode (tut_vmem.h): TVM_OWN unless ux0:data/gitsfa/moviemem.txt says otherwise (a number: the TVM_* bits,
// e.g. 9 = arena first then own memblocks), so the device can switch without a new build; the boot movie test tries others
static int tv_mem_mode = -1;
static int tv_mode_default(void) {
	if (tv_mem_mode < 0) { tv_mem_mode = TVM_OWN; FILE *f = fopen(VDIR "moviemem.txt", "rb"); if (f) { int v = 0; if (fscanf(f, "%d", &v) == 1 && v > 0 && v < 16 && (v & (TVM_OWN | TVM_ARENA))) tv_mem_mode = v; fclose(f);
		logp("movie: frame memory mode %d from moviemem.txt (%s)\n", tv_mem_mode, tvm_mode_name(tv_mem_mode)); } }
	return tv_mem_mode;
}
static int tv_next_mode = -1;   // the next tv_open only (the boot movie test)
// the CDRAM arena sizes tried (largest first) when the mode uses one: alone it must hold the decoder's memory too
// (b41: 11 MB + 1.5 MB for 960x544, 5 MB + 0.5 MB for 640x272); first, before own memblocks, b41's sizes
static volatile int tv_ev[16], tv_ev_n, tv_ev_logged;
static void tv_meminfo(char *b, int n) {
	SceKernelFreeMemorySizeInfo mi; memset(&mi, 0, sizeof(mi)); mi.size = sizeof(mi); sceKernelGetFreeMemorySize(&mi);
	snprintf(b, n, "free main %d KB, cdram %d KB, phycont %d KB", mi.size_user / 1024, mi.size_cdram / 1024, mi.size_phycont / 1024);
}
static void *tv_alloc(void *p, uint32_t align, uint32_t size) {
	(void)p; void *r = memalign(align < 16 ? 16 : align, size); tvm_note_general(align, size, r != NULL); return r;
}
static void tv_free(void *p, void *ptr) { (void)p; free(ptr); }
static const unsigned TV_ARENA_SZ[4][4] = { { 16u << 20, 15u << 20, 14u << 20, 13u << 20 }, { 8u << 20, 7u << 20, 6u << 20, 0 },   // arena alone: movie / clip
	{ 16u << 20, 12u << 20, 8u << 20, 4u << 20 }, { 4u << 20, 2u << 20, 0, 0 } };                                                      // arena first: movie / clip
static void *tv_galloc(void *p, uint32_t align, uint32_t size) { (void)p; return tvm_alloc(align, size); }
static void tv_gfree(void *p, void *ptr) { (void)p; tvm_free(ptr); }   // (the memory itself is freed in tv_close)
static void tv_log_reqs(void) {   // (game thread) the callbacks' recorded requests, each once
	int n = TVM.rq_n; __sync_synchronize();
	for (; TVM.rq_logged < n; TVM.rq_logged++) { const TvmReq *q = &TVM.rq[TVM.rq_logged]; int k = TVM.rq_logged + 1;
		if (!q->tex) logp("movie %s: player allocation #%d: %u KB, align 0x%X %s\n", TV.name, k, q->size / 1024, q->align, q->chunk < 0 ? "FAILED (newlib heap)" : "ok");
		else if (q->chunk >= 0) { const TvmChunk *C = &TVM.ch[q->chunk]; unsigned a = (unsigned)(uintptr_t)q->addr;
			logp("movie %s: frame memory #%d: %u bytes (%u KB), align 0x%X (used 0x%X) -> %s%s %d (%u KB) at 0x%08X (mod 256 KB: 0x%X)%s, GPU map %s 0x%08X\n", TV.name, k, q->size, q->size / 1024, q->align, q->used_align,
				q->reused ? "reused " : "", TVK_NAME[C->kind], q->chunk, C->size / 1024, a, a & 0x3FFFF, C->own ? ", its own" : ", carved",
				C->kind == TVK_ARENA ? "vitaGL's (RW)" : C->mapped ? "RW at allocation" : C->map_err ? "FAILED" : "not yet", (unsigned)q->map_err); }
		else { char mem[128]; tv_meminfo(mem, sizeof(mem));
			logp("movie %s: frame memory #%d: %u bytes (%u KB), align 0x%X (used 0x%X) -> FAILED (%d chunks, %u KB in use): new phycont memblock 0x%08X, new cdram memblock 0x%08X; %s\n",
				TV.name, k, q->size, q->size / 1024, q->align, q->used_align, TVM.nch, TVM.used / 1024, (unsigned)q->err_p, (unsigned)q->err_c, mem); }
	}
	if (TVM.rq_lost) { logp("movie %s: %d more requests not recorded\n", TV.name, TVM.rq_lost); TVM.rq_lost = 0; }
}
static void tv_event(void *p, int32_t id, int32_t src, void *data) {
	(void)p; (void)data; int n = tv_ev_n; if (n < 16) { tv_ev[n] = id | (src & 0xFF) << 16; __sync_synchronize(); tv_ev_n = n + 1; }
}
static void tv_log_events(const char *when) {   // AvPlayer's event ids (as the PS4 / Vita3K's AvPlayer): 1 stop, 2 ready, 3 play, 4 pause, 5 buffering
	static const char *const E[6] = { "?", "stop", "ready", "play", "pause", "buffering" };
	while (tv_ev_logged < tv_ev_n) { int e = tv_ev[tv_ev_logged] & 0xFFFF, src = tv_ev[tv_ev_logged] >> 16;
		logp("movie %s: player event 0x%X (%s, source %d), seen %s\n", TV.name, e, e < 6 ? E[e] : e == 0x10 ? "timed text" : e == 0x20 ? "warning" : e == 0x30 ? "encryption" : "?", src, when);
		tv_ev_logged++; }
}
static int tv_audio_thread(SceSize args, void *argp) {
	(void)args; (void)argp; SceAvPlayerFrameInfo fr; int rate = 0, ch = 0, grain = 0;
	while (tv_arun) {
		if (tv_h != 0 && sceAvPlayerIsActive(tv_h) && sceAvPlayerGetAudioData(tv_h, &fr) && fr.pData) {
			int r = fr.details.audio.sampleRate, c = fr.details.audio.channelCount, g = c > 0 ? (int)(fr.details.audio.size / (2 * c)) : 1024;
			if (g < 64 || g > 65472 || (g & 63)) g = 1024;
			if (r != rate || c != ch || g != grain) { sceAudioOutSetConfig(tv_port, g, r, c == 1 ? SCE_AUDIO_OUT_MODE_MONO : SCE_AUDIO_OUT_MODE_STEREO); rate = r; ch = c; grain = g; }
			sceAudioOutOutput(tv_port, fr.pData);
		} else sceKernelDelayThread(2000);
	}
	return 0;
}
static void tv_probe(void) {   // once, before the first player: can a phycont memblock (tut_vmem.h) be taken 1 MB-aligned, GPU-mapped RW, freed?
	char m0[128], m1[128]; tv_meminfo(m0, sizeof(m0)); int uid = 0; const unsigned sz = 8u << 20; char *b = tv_block_alloc(TVK_PHYCONT, sz, 1u << 20, &uid);
	int mr = b ? sceGxmMapMemory(b, sz, SCE_GXM_MEMORY_ATTRIB_RW) : 0, ur = b && mr >= 0 ? sceGxmUnmapMemory(b) : 0, fr = b ? sceKernelFreeMemBlock(uid) : 0;
	tv_meminfo(m1, sizeof(m1));
	logp("movie: frame memory probe: phycont memblock %u MB (1 MB-aligned) %s 0x%08X, GPU map RW 0x%08X, unmap 0x%08X, free 0x%08X (before: %s; after: %s)\n",
		sz >> 20, b ? "ok at" : "FAILED", b ? (unsigned)(uintptr_t)b : (unsigned)uid, (unsigned)mr, (unsigned)ur, (unsigned)fr, m0, m1);
}
static void tv_tex_black(void) {   // the two frame textures back on their own (valid) black image
	static unsigned char black[8 * 8 * 4];
	for (int k = 0; k < 2; k++) if (TV.tex[k]) { glBindTexture(GL_TEXTURE_2D, TV.tex[k]); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, black); }
}
// perf b56: sceAvPlayerStop + Close took ~50 ms on the game thread (b54 device: a 52 ms frame each time a tutorial
// explanation closed). tv_close_soon() hands them to a short-lived thread and returns; tv_close_poll() (every frame)
// frees the frame memory once that thread is done and the GPU has drawn 3 more frames; tv_close() / tv_open() finish a
// pending close first. The player's callbacks (tvm_free) only mark memory free: nothing else touches it meanwhile.
static SceUID tv_cth = -1; static SceAvPlayerHandle tv_ch; static volatile int tv_cdone; static int tv_cframes;
static int tv_close_thread(SceSize args, void *argp) { (void)args; (void)argp; sceAvPlayerStop(tv_ch); sceAvPlayerClose(tv_ch); __sync_synchronize(); tv_cdone = 1; return 0; }
static void tv_close_finish(int wait);
static void tv_close(void) {
	if (tv_cth >= 0) tv_close_finish(1);
	if (tv_th >= 0) { tv_arun = 0; sceKernelWaitThreadEnd(tv_th, NULL, NULL); sceKernelDeleteThread(tv_th); tv_th = -1; }
	if (tv_port >= 0) { sceAudioOutReleasePort(tv_port); tv_port = -1; }
	int had = tv_h != 0 || TVM.nch > 0;
	if (tv_h != 0) { sceAvPlayerStop(tv_h); sceAvPlayerClose(tv_h); tv_h = 0; }   // the decoder stops writing and hands its frames back (tv_gfree)
	if (TV.frames) tv_tex_black();   // no texture keeps pointing into the frame memory
	if (had) {
		tv_log_events("at the close"); tv_log_reqs();
		float t = (sceKernelGetProcessTimeWide() - tv_t0) / 1e6f;
		logp("movie %s: closed after %.1f s: %d frames shown (%dx%d); frame memory [mode %d]: %d requests (%d failed, %d reused), peak %u KB, %d not handed back (freed with the rest now), in cdram arena %u KB + phycont %u KB (%d memblocks) + cached phycont %u KB (%d) + cdram %u KB (%d); player allocations: %d (%d failed, %u KB)\n",
			TV.name, t, TV.frames, TV.w, TV.h, TVM.mode, TVM.n_req, TVM.n_fail, TVM.n_reuse, TVM.peak / 1024, TVM.nb, tvm_kb(TVK_ARENA), tvm_kb(TVK_PHYCONT), tvm_count(TVK_PHYCONT),
			tvm_kb(TVK_PHYCONT_C), tvm_count(TVK_PHYCONT_C), tvm_kb(TVK_CDRAM), tvm_count(TVK_CDRAM), TVM.n_gen, TVM.n_gen_fail, TVM.gen_kb);
		if (TVM.nch) glFinish();   // the GPU is done with the last frame
		tvm_release(); tvm_reset(0);
	}
	TV.on = 0; TV.frames = 0;
}
static void tv_close_soon(void) {
	if (tv_cth >= 0 || tv_h == 0) { tv_close(); return; }
	if (tv_th >= 0) { tv_arun = 0; sceKernelWaitThreadEnd(tv_th, NULL, NULL); sceKernelDeleteThread(tv_th); tv_th = -1; }
	if (tv_port >= 0) { sceAudioOutReleasePort(tv_port); tv_port = -1; }
	if (TV.frames) tv_tex_black();   // no texture points into the frame memory from now on
	tv_ch = tv_h; tv_cdone = 0; tv_cframes = 0;
	SceUID th = sceKernelCreateThread("gitsfa_mvclose", tv_close_thread, 0x10000100 + 30, 0x4000, 0, 0, NULL);
	if (th < 0 || sceKernelStartThread(th, 0, NULL) < 0) { if (th >= 0) sceKernelDeleteThread(th); tv_close(); return; }   // (no thread: as before)
	tv_cth = th; tv_h = 0; TV.on = 0;
}
static void tv_close_finish(int wait) {   // the pending close: wait = block until it is done
	if (tv_cth < 0) return;
	if (!wait && (!tv_cdone || ++tv_cframes < 3)) return;
	sceKernelWaitThreadEnd(tv_cth, NULL, NULL); sceKernelDeleteThread(tv_cth); tv_cth = -1;
	tv_log_events("at the close"); tv_log_reqs();
	logp("movie %s: closed on its own thread after %.1f s: %d frames shown (%dx%d); frame memory [mode %d]: %d requests, peak %u KB, %d not handed back (freed with the rest now)\n",
		TV.name, (sceKernelGetProcessTimeWide() - tv_t0) / 1e6f, TV.frames, TV.w, TV.h, TVM.mode, TVM.n_req, TVM.peak / 1024, TVM.nb);
	if (TVM.nch) glFinish();
	tvm_release(); tvm_reset(0); TV.frames = 0;
}
static void tv_close_poll(void) { tv_close_finish(0); }
static int tv_open(const char *path, int loop, int audio) {
	tv_close(); TV.done = 0; TV.err = 0; TV.w = TV.h = 0; char mem[128]; tv_t0 = sceKernelGetProcessTimeWide();
	{ const char *n = strrchr(path, '/'); snprintf(TV.name, sizeof(TV.name), "%s", n ? n + 1 : path); }
	FILE *f = fopen(path, "rb"); if (!f) { logp("movie %s: file not found\n", path); return TV.err = -1; } fseek(f, 0, SEEK_END); long fsz = ftell(f); fclose(f);
	if (!tv_mod) { int r = sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER); tv_mod = r >= 0 ? 1 : -1; logp("movie: sceSysmoduleLoadModule(AVPLAYER) = 0x%08X\n", r); }
	if (tv_mod < 0) return TV.err = -2;
	{ static int probed; if (!probed) { probed = 1; tv_probe(); } }
	if (!TV.tex[0]) {   // two GL textures whose SceGxmTexture is re-pointed at decoded frames (kept for the whole run)
		glGenTextures(2, TV.tex); tv_tex_black();
		for (int k = 0; k < 2; k++) { glBindTexture(GL_TEXTURE_2D, TV.tex[k]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
	}
	// frame memory (tut_vmem.h): the mode; the CDRAM arena if it uses one (game thread, before the player exists: vitaGL's
	// allocator is not thread-safe) - the largest size vitaGL's pool still has, its base 256 KB-aligned
	int mode = tv_next_mode > 0 ? tv_next_mode : tv_mode_default(); tv_next_mode = -1;
	tvm_reset(mode); tv_ev_n = tv_ev_logged = 0;
	if (mode & TVM_ARENA) { const unsigned *SZ = TV_ARENA_SZ[((mode & TVM_OWN) ? 2 : 0) + (loop ? 1 : 0)];
	  for (int k = 0; k < 4 && SZ[k] && !TVM.nch; k++) { char *r = vglAlloc(SZ[k] + TVM_MIN_ALIGN, VGL_MEM_VRAM);
		if (r) tvm_add_arena(r, (char *)(((uintptr_t)r + TVM_MIN_ALIGN - 1) & ~(uintptr_t)(TVM_MIN_ALIGN - 1)), SZ[k]); } }
	tv_meminfo(mem, sizeof(mem));
	if ((mode & TVM_ARENA) && !TVM.nch) logp("movie %s: no cdram arena (vitaGL's pool is short)%s (%s)\n", TV.name, (mode & TVM_OWN) ? "; own memblocks only" : ": the player will get no frame memory", mem);
	for (int attempt = 0; attempt < 2 && tv_h == 0; attempt++) {
		SceAvPlayerInitData d; memset(&d, 0, sizeof(d));
		d.memoryReplacement.allocate = tv_alloc; d.memoryReplacement.deallocate = tv_free;
		d.memoryReplacement.allocateTexture = tv_galloc; d.memoryReplacement.deallocateTexture = tv_gfree;
		d.eventReplacement.eventCallback = tv_event;
		d.basePriority = 0xA0; d.numOutputVideoFrameBuffers = attempt ? 4 : 2; d.autoStart = 1; d.debugLevel = 0;
		tv_h = sceAvPlayerInit(&d);   // (the handle is a pointer: "negative" as an int is fine, 0 = failed)
		if (tv_h == 0) { tv_log_reqs(); logp("movie %s: sceAvPlayerInit failed (attempt %d, %d frame buffers): %d player allocations (%d failed, %u KB), %d frame memory requests (%d failed); %s\n",
			TV.name, attempt + 1, d.numOutputVideoFrameBuffers, TVM.n_gen, TVM.n_gen_fail, TVM.gen_kb, TVM.n_req, TVM.n_fail, mem); }
	}
	if (tv_h == 0) { tv_close(); return TV.err = -3; }
	int ra = sceAvPlayerAddSource(tv_h, path);
	if (ra < 0) { logp("movie %s: sceAvPlayerAddSource failed 0x%08X (%ld bytes)\n", TV.name, ra, fsz); tv_close(); return TV.err = -5; }
	sceAvPlayerSetLooping(tv_h, loop ? 1 : 0);
	TV.on = 1; TV.frames = 0; TV.audio = 0; tv_t0 = sceKernelGetProcessTimeWide(); tv_warned = 0;
	if (audio) {
		static const int PT[3] = { SCE_AUDIO_OUT_PORT_TYPE_MAIN, SCE_AUDIO_OUT_PORT_TYPE_VOICE, SCE_AUDIO_OUT_PORT_TYPE_BGM };   // the game's mixer may hold a MAIN / BGM port
		int pr = 0; for (int k = 0; k < 3 && tv_port < 0; k++) { tv_port = sceAudioOutOpenPort(PT[k], 1024, 48000, SCE_AUDIO_OUT_MODE_STEREO); if (tv_port < 0) pr = tv_port; }
		if (tv_port >= 0) { int v = (int)(SCE_AUDIO_OUT_MAX_VOL * (TV.vol > 0 ? TV.vol : 1)); int vols[2] = { v, v }; sceAudioOutSetVolume(tv_port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
			tv_arun = 1; tv_th = sceKernelCreateThread("gitsfa_movie_audio", tv_audio_thread, 0x10000100 - 8, 0x4000, 0, 0, NULL);
			if (tv_th >= 0) { sceKernelStartThread(tv_th, 0, NULL); TV.audio = 1; } }
		else logp("movie %s: no audio port (0x%08X): silent\n", TV.name, pr);
	}
	logp("movie %s: player started (%ld bytes; frame memory mode %d: %s; cdram arena %u KB; %s)\n", path, fsz, mode, tvm_mode_name(mode), tvm_kb(TVK_ARENA), mem);
	tv_log_reqs();
	return 0;
}
static GLuint tv_frame(void) {
	if (!TV.on) return 0;
	tv_log_events("while playing"); tv_log_reqs();
	float since = (sceKernelGetProcessTimeWide() - tv_t0) / 1e6f;
	if (!sceAvPlayerIsActive(tv_h)) {   // finished - or not started yet (grace: 3 s for the first frame)
		if (TV.frames || since > 3.0f) { if (!TV.done && !TV.frames) logp("movie %s: the player stopped without a frame (%.1f s; %d frame memory requests, %d failed)\n", TV.name, since, TVM.n_req, TVM.n_fail); TV.done = 1; }
		return TV.frames ? TV.tex[TV.idx] : 0;
	}
	SceAvPlayerFrameInfo fr;
	if (sceAvPlayerGetVideoData(tv_h, &fr) && fr.pData) {
		int c = tvm_chunk_of(fr.pData), m = tvm_gpu_map(c);   // a memblock the decoder wrote: GPU-mapped before its first draw
		if (m < 0) { if (!tv_warned) logp("movie %s: sceGxmMapMemory of %s %d failed 0x%08X: frame not drawn\n", TV.name, TVK_NAME[TVM.ch[c].kind], c, (unsigned)m); tv_warned = 1; return TV.frames ? TV.tex[TV.idx] : 0; }
		if (m > 0) logp("movie %s: %s %d mapped for the GPU (RW) at its first frame (%u KB)\n", TV.name, TVK_NAME[TVM.ch[c].kind], c, TVM.ch[c].size / 1024);
		if (!TV.frames) logp("movie %s: first frame %ux%u after %.2f s (in %s)\n", TV.name, (unsigned)fr.details.video.width, (unsigned)fr.details.video.height, since,
			c >= 0 ? TVK_NAME[TVM.ch[c].kind] : "memory the player allocated itself (emulator)");
		TV.idx ^= 1; glBindTexture(GL_TEXTURE_2D, TV.tex[TV.idx]);
		SceGxmTexture *g = vglGetGxmTexture(GL_TEXTURE_2D);
		TV.w = fr.details.video.width; TV.h = fr.details.video.height;
		sceGxmTextureInitLinear(g, fr.pData, SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1, TV.w, TV.h, 0);
		sceGxmTextureSetMinFilter(g, SCE_GXM_TEXTURE_FILTER_LINEAR); sceGxmTextureSetMagFilter(g, SCE_GXM_TEXTURE_FILTER_LINEAR);
		TV.frames++;
	} else if (!TV.frames && since > 5.0f && !tv_warned) { tv_warned = 1; logp("movie %s: no video frame 5 s after the start (%d frame memory requests, %d failed, %d events)\n", TV.name, TVM.n_req, TVM.n_fail, tv_ev_n); }
	return TV.frames ? TV.tex[TV.idx] : 0;
}
#else   // host builds: no video
static void tv_close(void) { TV.on = 0; }
static void tv_close_soon(void) { TV.on = 0; }
static void tv_close_poll(void) {}
static int tv_open(const char *path, int loop, int audio) { (void)path; (void)loop; (void)audio; TV.done = 1; return -1; }
static GLuint tv_frame(void) { return 0; }
#endif
static int tv_done(void) { return TV.done; }
