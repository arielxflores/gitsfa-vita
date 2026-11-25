// Host implementation of the VitaSDK / vitaGL / vitashark shims (perf, b37: tools/hostgame; headers in shim/).
// Built with the game under AddressSanitizer + UBSan (or ThreadSanitizer). What it adds to the sanitizers ("HOSTGL" lines,
// each distinct call site once, with its stack):
//   - buffers / textures own host memory of exactly the size vitaGL gives them (maps and uploads overflow into redzones),
//   - draws read every vertex they use (client arrays and buffer objects: past-the-end reads are reported) and touch the
//     memory of the textures the current program samples (freed or re-pointed texture memory shows up as use-after-free),
//   - uniform arrays are checked against the sizes declared in the Cg sources,
//   - texture uploads are copied at the next sceGxmTransferFinish / glFinish / swap (vitaGL's transfers are asynchronous):
//     a source freed or changed before that is caught,
//   - GL calls from a thread other than the main one, ids that are not live, compressed sizes that do not match.
// Environment: HG_OUT (writes, default ./hg_out), HG_MAPS, HG_DATA (reads: HG_OUT, then HG_MAPS, then HG_DATA),
//   HG_MAX_FRAMES (stop after that many swaps), HG_TAIL_FRAMES (frames after the tour's end, default 900),
//   HG_ECHO_LOG=1 (boot.log to stderr too), HG_ABORT=1 (abort at the first HOSTGL finding), HG_DT_US (default 16667),
//   HG_BUF_SPREAD=1 (buffer names round-robin, as vitaGL's), HG_STATUS_EVERY (frames between status lines, default 3600),
//   perf b46: HG_DRAWHASH=N (a hash of what every draw of every Nth frame fetched: "HOSTGL draws" at exit; HG_DRAWHASH_LOG=1
//   per sampled frame; HG_DRAWHASH_FROM / _TO: every draw of the sampled frames in that range; HG_DRAWHASH_ARRAYS=0: indexed
//   draws only), HG_AUDIO_OFF=1 (no audio port). Run to run (main too), with audio the draws differ, and without it one HUD
//   quad drawn from client arrays (program 78 at b46): compare with HG_AUDIO_OFF=1 HG_DRAWHASH_ARRAYS=0.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>   // (gettid: sceKernelGetThreadId)
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/mman.h>
#include "shim/vitasdk.h"
#include "shim/vitaGL.h"
#include "shim/vitashark.h"
#undef fopen
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define HG_SAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define HG_SAN 1
#endif
#ifdef HG_SAN
#include <sanitizer/common_interface_defs.h>
#define HG_STACK() __sanitizer_print_stack_trace()
#else
#include <execinfo.h>
static void hg_bt(void) { void *a[32]; int n = backtrace(a, 32); backtrace_symbols_fd(a, n, 2); }
#define HG_STACK() hg_bt()
#endif

// ------------------------------------------------------------------ reports
static pthread_t hg_main; static int hg_main_set;
__attribute__((constructor)) static void hg_init_main(void) { hg_main = pthread_self(); hg_main_set = 1; }
static int hg_is_main(void) { return !hg_main_set || pthread_equal(pthread_self(), hg_main); }
static unsigned long long hg_up_hash, hg_up_bytes;   // (perf b45: defined with hg_up below)
#define HG_MAXR 512
static struct { const char *kind; void *site; int n; } hg_r[HG_MAXR]; static int hg_nr, hg_total; static pthread_mutex_t hg_rlk = PTHREAD_MUTEX_INITIALIZER;
static unsigned hg_frame;
static void hg_report(void *site, const char *kind, const char *fmt, ...) {
	pthread_mutex_lock(&hg_rlk); hg_total++;
	for (int i = 0; i < hg_nr; i++) if (hg_r[i].site == site && !strcmp(hg_r[i].kind, kind)) { hg_r[i].n++; pthread_mutex_unlock(&hg_rlk); return; }
	if (hg_nr < HG_MAXR) { hg_r[hg_nr].kind = kind; hg_r[hg_nr].site = site; hg_r[hg_nr].n = 1; hg_nr++; }
	char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a);
	fprintf(stderr, "HOSTGL %s (frame %u): %s\n", kind, hg_frame, b); HG_STACK(); fflush(stderr);
	pthread_mutex_unlock(&hg_rlk);
	if (getenv("HG_ABORT")) abort();
}
#define SITE __builtin_return_address(0)
static unsigned long long hg_dh = 1469598103934665603ull, hg_dh_draws, hg_dh_verts; static unsigned hg_dh_frames;   // perf (b46): HG_DRAWHASH
static void hg_summary(void) {
	fprintf(stderr, "HOSTGL summary: %d findings at %d call sites, %u frames\n", hg_total, hg_nr, hg_frame);
	if (hg_dh_frames) fprintf(stderr, "HOSTGL draws: %u sampled frames, %llu draws, %llu vertices, hash %016llx (the attribute bytes every draw fetched, in order: HG_DRAWHASH)\n", hg_dh_frames, hg_dh_draws, hg_dh_verts, hg_dh);
	fprintf(stderr, "HOSTGL uploads: %llu bytes, hash %016llx (every texture and buffer upload's data, in order)\n", hg_up_bytes, hg_up_hash);
	for (int i = 0; i < hg_nr; i++) fprintf(stderr, "  %-28s x%-6d at %p\n", hg_r[i].kind, hg_r[i].n, hg_r[i].site);
}
#define GLCHECK() do { if (!hg_is_main()) hg_report(SITE, "GL-OFF-MAIN-THREAD", "a GL call from a worker thread (%s)", __func__); } while (0)

// ------------------------------------------------------------------ paths: "ux0:data/gitsfa/..." -> host folders
static const char *hg_env(const char *k, const char *d) { const char *v = getenv(k); return v && *v ? v : d; }
static void hg_mkdirs(const char *path) {   // the parent folders of path
	char t[1024]; snprintf(t, sizeof(t), "%s", path);
	for (char *p = t + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(t, 0777); *p = '/'; }
}
static int hg_exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static const char *hg_map(const char *path, int write, char *out, size_t n) {
	const char *rel = NULL; char tmp[512]; const char *c = strchr(path, ':'), *sl = strchr(path, '/');
	if (!strncmp(path, "ux0:data/gitsfa", 15)) { rel = path + 15; while (*rel == '/') rel++; }
	else if (c && (!sl || c < sl)) { snprintf(tmp, sizeof(tmp), "dev_%.*s/%s", (int)(c - path), path, c + 1 + (c[1] == '/')); rel = tmp; }   // another device: under HG_OUT
	if (!rel) { snprintf(out, n, "%s", path); return out; }
	const char *o = hg_env("HG_OUT", "hg_out");
	if (write) { snprintf(out, n, "%s/%s", o, rel); hg_mkdirs(out); return out; }
	snprintf(out, n, "%s/%s", o, rel); if (hg_exists(out)) return out;
	snprintf(out, n, "%s/%s", hg_env("HG_MAPS", "/root/gits/build/maps"), rel); if (hg_exists(out)) return out;
	snprintf(out, n, "%s/%s", hg_env("HG_DATA", "/root/gits/build/data"), rel); if (hg_exists(out)) return out;
	snprintf(out, n, "%s/%s", o, rel); return out;
}
FILE *hg_fopen(const char *path, const char *mode) {
	char p[1024]; int w = strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+');
	return fopen(hg_map(path, w, p, sizeof(p)), mode);
}

// ------------------------------------------------------------------ the clock (virtual: the game's time moves with its frames)
static volatile uint64_t hg_vt = 1000000;
// net (stage 1): HG_REALTIME=1 - the game clock is the real clock and every swap waits for the next 1/60 s (two or three
// instances talking over the network need the same time base); otherwise the virtual clock as before
static int hg_rt = -1; static uint64_t hg_rt0;
static uint64_t hg_real_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + t.tv_nsec / 1000; }
static int hg_realtime(void) { if (hg_rt < 0) { const char *e = getenv("HG_REALTIME"); hg_rt = e && atoi(e) ? 1 : 0; hg_rt0 = hg_real_us(); } return hg_rt; }
static uint64_t hg_vget(void) { if (hg_realtime()) return hg_real_us() - hg_rt0 + 1000000; return __atomic_load_n(&hg_vt, __ATOMIC_RELAXED); }
static void hg_vadd(uint64_t d) { if (hg_realtime()) { if (d) usleep(d < 200000 ? (useconds_t)d : 200000); return; } __atomic_fetch_add(&hg_vt, d, __ATOMIC_RELAXED); }
static void hg_frame_pace(uint64_t dt) {   // real-time mode: the next frame starts at the next multiple of dt (no catching up after a stall)
	static uint64_t next; uint64_t now = hg_real_us(); if (!next || now > next + 4 * dt) next = now; next += dt; if (next > now) usleep((useconds_t)(next - now)); }
SceUInt64 sceKernelGetProcessTimeWide(void) { return hg_vget(); }
SceUInt64 sceKernelGetSystemTimeWide(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (SceUInt64)t.tv_sec * 1000000u + t.tv_nsec / 1000; }   // real time (ldprof.h)
int sceKernelGetThreadId(void) { return (int)syscall(SYS_gettid); }
unsigned sceDisplayGetVcount(void) { return (unsigned)(hg_vget() / 16667); }   // perf (b45): dstep.h
SceUInt32 sceKernelGetProcessTimeLow(void) { return (SceUInt32)hg_vget(); }
int sceKernelDelayThread(SceUInt us) {
	if (hg_is_main()) { if (us) hg_vadd(us); sched_yield(); }   // (the main thread waits in game time; the others for real)
	else if (us) usleep(us < 200000 ? us : 200000); else sched_yield();
	return 0;
}

// ------------------------------------------------------------------ threads, semaphores
#define HG_MAXT 64
typedef struct { int used; char name[32]; SceKernelThreadEntry entry; pthread_t th; int started, joined; void *arg; SceSize arglen; int ret; } HgThread;
static HgThread hg_t[HG_MAXT]; static pthread_mutex_t hg_tlk = PTHREAD_MUTEX_INITIALIZER;
static void *hg_thread_main(void *p) { HgThread *t = p; pthread_setname_np(pthread_self(), t->name); t->ret = t->entry(t->arglen, t->arg); return NULL; }
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, SceSize stack, SceUInt attr, int cpu_mask, const void *opt) {
	(void)prio; (void)stack; (void)attr; (void)cpu_mask; (void)opt;
	pthread_mutex_lock(&hg_tlk); int i = 0; while (i < HG_MAXT && hg_t[i].used) i++;
	if (i == HG_MAXT) { pthread_mutex_unlock(&hg_tlk); return -1; }
	memset(&hg_t[i], 0, sizeof(hg_t[i])); hg_t[i].used = 1; snprintf(hg_t[i].name, sizeof(hg_t[i].name), "%.15s", name ? name : "thread"); hg_t[i].entry = entry;
	pthread_mutex_unlock(&hg_tlk); return 0x4000 + i;
}
static HgThread *hg_th(SceUID id) { int i = id - 0x4000; return i >= 0 && i < HG_MAXT && hg_t[i].used ? &hg_t[i] : NULL; }
int sceKernelStartThread(SceUID id, SceSize arglen, void *argp) {
	HgThread *t = hg_th(id); if (!t || t->started) return -1;
	t->arglen = arglen; t->arg = NULL; if (arglen && argp) { t->arg = malloc(arglen); memcpy(t->arg, argp, arglen); }   // (the Vita copies the arguments to the new stack)
	if (pthread_create(&t->th, NULL, hg_thread_main, t)) return -1;
	t->started = 1; return 0;
}
int sceKernelWaitThreadEnd(SceUID id, int *stat, SceUInt *timeout) {
	(void)timeout; HgThread *t = hg_th(id); if (!t || !t->started) return -1;
	if (!t->joined) { pthread_join(t->th, NULL); t->joined = 1; } if (stat) *stat = t->ret; return 0;
}
int sceKernelDeleteThread(SceUID id) { HgThread *t = hg_th(id); if (!t) return -1; if (t->started && !t->joined) return -1; free(t->arg); pthread_mutex_lock(&hg_tlk); t->used = 0; pthread_mutex_unlock(&hg_tlk); return 0; }
int sceKernelExitDeleteThread(int status) { pthread_exit((void *)(intptr_t)status); return 0; }
#define HG_MAXS 64
typedef struct { int used; pthread_mutex_t m; pthread_cond_t c; int count, max; } HgSema;
static HgSema hg_s[HG_MAXS];
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int init, int max, void *opt) {
	(void)name; (void)attr; (void)opt;
	pthread_mutex_lock(&hg_tlk); int i = 0; while (i < HG_MAXS && hg_s[i].used) i++;
	if (i == HG_MAXS) { pthread_mutex_unlock(&hg_tlk); return -1; }
	hg_s[i].used = 1; pthread_mutex_init(&hg_s[i].m, NULL); pthread_cond_init(&hg_s[i].c, NULL); hg_s[i].count = init; hg_s[i].max = max;
	pthread_mutex_unlock(&hg_tlk); return 0x5000 + i;
}
static HgSema *hg_sema(SceUID id) { int i = id - 0x5000; return i >= 0 && i < HG_MAXS && hg_s[i].used ? &hg_s[i] : NULL; }
int sceKernelWaitSema(SceUID id, int need, SceUInt *timeout) {
	HgSema *s = hg_sema(id); if (!s) return -1;
	pthread_mutex_lock(&s->m); int r = 0;
	if (timeout) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); uint64_t ns = ts.tv_nsec + (uint64_t)*timeout * 1000; ts.tv_sec += ns / 1000000000; ts.tv_nsec = ns % 1000000000;
		while (s->count < need) if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT) { r = (int)SCE_KERNEL_ERROR_WAIT_TIMEOUT; break; } }
	else while (s->count < need) pthread_cond_wait(&s->c, &s->m);
	if (!r) s->count -= need;
	pthread_mutex_unlock(&s->m); return r;
}
int sceKernelSignalSema(SceUID id, int n) {
	HgSema *s = hg_sema(id); if (!s) return -1;
	pthread_mutex_lock(&s->m); if (s->count + n > s->max) { pthread_mutex_unlock(&s->m); return (int)0x8002802A; } s->count += n; pthread_cond_broadcast(&s->c); pthread_mutex_unlock(&s->m); return 0;
}
int sceKernelDeleteSema(SceUID id) { HgSema *s = hg_sema(id); if (!s) return -1; s->used = 0; return 0; }
int sceKernelExitProcess(int res) { fflush(NULL); hg_summary(); exit(res); }
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info) { if (!info) return -1; info->size_user = 96 << 20; info->size_cdram = 64 << 20; info->size_phycont = 26 << 20; return 0; }
#define HG_MAXB 64
static struct { int used; void *p; } hg_mb[HG_MAXB];
SceUID sceKernelAllocMemBlock(const char *name, SceUInt type, SceSize size, void *opt) {
	(void)name; (void)type; const SceKernelAllocMemBlockOpt *o = opt; size_t al = o && (o->attr & SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT) && o->alignment > 4096 ? o->alignment : 4096;
	for (int i = 0; i < HG_MAXB; i++) if (!hg_mb[i].used) { void *p = NULL; if (posix_memalign(&p, al, size ? size : 1)) return -1; hg_mb[i].used = 1; hg_mb[i].p = p; return 0x6000 + i; }
	return -1;
}
int sceKernelGetMemBlockBase(SceUID uid, void **base) { int i = uid - 0x6000; if (i < 0 || i >= HG_MAXB || !hg_mb[i].used) return -1; *base = hg_mb[i].p; return 0; }
int sceKernelFreeMemBlock(SceUID uid) { int i = uid - 0x6000; if (i < 0 || i >= HG_MAXB || !hg_mb[i].used) return -1; free(hg_mb[i].p); hg_mb[i].used = 0; return 0; }

// ------------------------------------------------------------------ io
static int hg_logfd = -1; static unsigned hg_stop_at;
static void hg_scan_log(const char *p, SceSize n) {
	static char tail[256]; static int tn; char b[1024]; int k = tn < 200 ? tn : 200; memcpy(b, tail + tn - k, k); int m = n < 800 ? (int)n : 800; memcpy(b + k, p + n - m, m); b[k + m] = 0;
	if (!hg_stop_at && (strstr(b, "autoplay: end of match screens next") || strstr(b, "stopping:"))) {
		hg_stop_at = hg_frame + (unsigned)atoi(hg_env("HG_TAIL_FRAMES", "900")); fprintf(stderr, "hostgame: the tour ended (frame %u): stopping in %s frames\n", hg_frame, hg_env("HG_TAIL_FRAMES", "900")); }
	int keep = m < 200 ? m : 200; memcpy(tail, p + n - keep, keep); tn = keep;
}
SceUID sceIoOpen(const char *file, int flags, SceMode mode) {
	char p[1024]; int w = (flags & (SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC | SCE_O_APPEND)) != 0;
	int f = 0; if ((flags & 3) == SCE_O_RDONLY) f = O_RDONLY; else if ((flags & 3) == SCE_O_WRONLY) f = O_WRONLY; else f = O_RDWR;
	if (flags & SCE_O_CREAT) f |= O_CREAT; if (flags & SCE_O_TRUNC) f |= O_TRUNC; if (flags & SCE_O_APPEND) f |= O_APPEND; if (flags & SCE_O_EXCL) f |= O_EXCL;
	int fd = open(hg_map(file, w, p, sizeof(p)), f, mode ? mode : 0666);
	if (fd >= 0 && w && strstr(file, "boot.log")) hg_logfd = fd;
	return fd >= 0 ? fd : (int)0x80010002;
}
int sceIoClose(SceUID fd) { if (fd == hg_logfd) hg_logfd = -1; return close(fd) == 0 ? 0 : -1; }
int sceIoRead(SceUID fd, void *data, SceSize size) { ssize_t r = read(fd, data, size); return r < 0 ? -1 : (int)r; }
int sceIoWrite(SceUID fd, const void *data, SceSize size) {
	if (fd == hg_logfd && size) { hg_scan_log(data, size); if (getenv("HG_ECHO_LOG")) fwrite(data, 1, size, stderr); }
	ssize_t r = write(fd, data, size); return r < 0 ? -1 : (int)r;
}
SceOff sceIoLseek(SceUID fd, SceOff off, int whence) { return lseek(fd, off, whence == SCE_SEEK_SET ? SEEK_SET : whence == SCE_SEEK_CUR ? SEEK_CUR : SEEK_END); }
int sceIoMkdir(const char *dir, SceMode mode) { char p[1024]; hg_map(dir, 1, p, sizeof(p)); return mkdir(p, mode ? mode : 0777) == 0 ? 0 : (int)0x80010011; }
int sceIoRmdir(const char *path) { char p[1024]; return rmdir(hg_map(path, 1, p, sizeof(p))) == 0 ? 0 : -1; }
int sceIoRemove(const char *file) { char p[1024]; return unlink(hg_map(file, 1, p, sizeof(p))) == 0 ? 0 : -1; }
static void hg_stat(const struct stat *s, SceIoStat *o) { memset(o, 0, sizeof(*o)); o->st_mode = S_ISDIR(s->st_mode) ? SCE_S_IFDIR | 0777 : SCE_S_IFREG | 0666; o->st_size = s->st_size; }
int sceIoGetstat(const char *file, SceIoStat *st) { char p[1024]; struct stat s; if (stat(hg_map(file, 0, p, sizeof(p)), &s)) return (int)0x80010002; hg_stat(&s, st); return 0; }
int sceIoDevctl(const char *dev, unsigned int cmd, void *in, int inlen, void *out, int outlen) { (void)dev; (void)in; (void)inlen; if (cmd == 0x3001 && out && outlen >= (int)sizeof(SceIoDevInfo)) { SceIoDevInfo *d = out; d->max_size = 64ll << 30; d->free_size = 20ll << 30; d->cluster_size = 32768; return 0; } return (int)0x80010016; }
#define HG_MAXD 16
static struct { DIR *d; char path[1024]; } hg_d[HG_MAXD];
SceUID sceIoDopen(const char *dirname) {
	char p[1024]; hg_map(dirname, 0, p, sizeof(p)); DIR *d = opendir(p); if (!d) return (int)0x80010002;
	for (int i = 0; i < HG_MAXD; i++) if (!hg_d[i].d) { hg_d[i].d = d; snprintf(hg_d[i].path, sizeof(hg_d[i].path), "%s", p); return 0x7000 + i; }
	closedir(d); return -1;
}
int sceIoDread(SceUID fd, SceIoDirent *e) {
	int i = fd - 0x7000; if (i < 0 || i >= HG_MAXD || !hg_d[i].d) return -1;
	struct dirent *de = readdir(hg_d[i].d); if (!de) return 0;
	memset(e, 0, sizeof(*e)); snprintf(e->d_name, sizeof(e->d_name), "%s", de->d_name);
	char fp[1400]; struct stat s; snprintf(fp, sizeof(fp), "%s/%s", hg_d[i].path, de->d_name); if (!stat(fp, &s)) hg_stat(&s, &e->d_stat);
	return 1;
}
int sceIoDclose(SceUID fd) { int i = fd - 0x7000; if (i < 0 || i >= HG_MAXD || !hg_d[i].d) return -1; closedir(hg_d[i].d); hg_d[i].d = NULL; return 0; }

// ------------------------------------------------------------------ input, power, modules (the autoplay drives the game)
int sceCtrlSetSamplingMode(int mode) { (void)mode; return 0; }
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count) { (void)port; for (int i = 0; i < count; i++) { memset(&pad[i], 0, sizeof(*pad)); pad[i].lx = pad[i].ly = pad[i].rx = pad[i].ry = 128; pad[i].timeStamp = hg_vget(); } return count; }
int sceCtrlReadBufferPositive(int port, SceCtrlData *pad, int count) { return sceCtrlPeekBufferPositive(port, pad, count); }
int sceTouchSetSamplingState(int port, int state) { (void)port; (void)state; return 0; }
int sceTouchPeek(int port, SceTouchData *d, int n) { (void)port; for (int i = 0; i < n; i++) { memset(&d[i], 0, sizeof(*d)); d[i].timeStamp = hg_vget(); } return n; }
int scePowerSetArmClockFrequency(int f) { (void)f; return 0; }
int scePowerSetBusClockFrequency(int f) { (void)f; return 0; }
int scePowerSetGpuClockFrequency(int f) { (void)f; return 0; }
int scePowerSetGpuXbarClockFrequency(int f) { (void)f; return 0; }
int sceSysmoduleLoadModule(int id) { (void)id; return 0; }
void shark_install_log_cb(shark_log_cb cb) { (void)cb; }

// ------------------------------------------------------------------ audio out: every buffer is read, the call blocks for its duration
#define HG_MAXP 8
static struct { int used, len, freq, ch; } hg_port[HG_MAXP]; static volatile unsigned hg_sink;
int sceAudioOutOpenPort(int type, int len, int freq, int mode) {
	(void)type; static int off = -1; if (off < 0) off = atoi(hg_env("HG_AUDIO_OFF", "0")) != 0;
	if (off) return (int)0x80260004;   // perf (b46): HG_AUDIO_OFF=1: no audio port (the game's audio, on real time, makes the draws differ run to run)
	pthread_mutex_lock(&hg_tlk);
	for (int i = 0; i < HG_MAXP; i++) if (!hg_port[i].used) { hg_port[i].used = 1; hg_port[i].len = len; hg_port[i].freq = freq; hg_port[i].ch = mode == SCE_AUDIO_OUT_MODE_STEREO ? 2 : 1; pthread_mutex_unlock(&hg_tlk); return i + 1; }
	pthread_mutex_unlock(&hg_tlk); return (int)0x80260004;
}
int sceAudioOutReleasePort(int port) { if (port < 1 || port > HG_MAXP) return -1; hg_port[port - 1].used = 0; return 0; }
int sceAudioOutSetConfig(int port, int len, int freq, int mode) {
	if (port < 1 || port > HG_MAXP) return -1; if (len > 0) hg_port[port - 1].len = len; if (freq > 0) hg_port[port - 1].freq = freq; if (mode >= 0) hg_port[port - 1].ch = mode == SCE_AUDIO_OUT_MODE_STEREO ? 2 : 1; return 0;
}
int sceAudioOutSetVolume(int port, int flag, int *vol) { (void)port; volatile int s = 0; if (flag & 1) s += vol[0]; if (flag & 2) s += vol[1]; (void)s; return 0; }
int sceAudioOutOutput(int port, const void *buf) {
	if (port < 1 || port > HG_MAXP || !hg_port[port - 1].used) return -1;
	int n = hg_port[port - 1].len * hg_port[port - 1].ch * 2;
	if (buf) { static __thread unsigned char tmp[65536]; if (n > (int)sizeof(tmp)) n = sizeof(tmp); memcpy(tmp, buf, n); __atomic_fetch_add(&hg_sink, tmp[0] + tmp[n - 1], __ATOMIC_RELAXED); }
	usleep((useconds_t)(hg_port[port - 1].len * 1000000LL / (hg_port[port - 1].freq > 0 ? hg_port[port - 1].freq : 48000)));
	return 0;
}

// ------------------------------------------------------------------ GL state
#define HG_MAXTEX 8192
#define HG_MAXBUF 8192
#define HG_MAXSH 1024
#define HG_MAXU 96
#define HG_MAXATTR 16
typedef struct { unsigned char *data; int32_t size32; int live; size_t size; int mapped, ever_mapped; } HBuf;   // head = vitaGL's vbo struct (ptr, size): texcanary.h reads it through the buffer id
typedef struct { int live, gen; unsigned w, h; GLenum fmt; int kind, nlev; size_t size; unsigned char *data; SceGxmTexture gxm; void *site; } HTex;
typedef struct { char name[48]; int comps, arr, sampler, unit; } HUni;   // comps per element, array size (1: not an array)
typedef struct { int live, type, compiled; char *src; HUni u[HG_MAXU]; int nu; } HShader;
typedef struct { int live, linked; GLuint sh[4]; int nsh; HUni u[HG_MAXU]; int nu; unsigned attrmask; } HProg;
static HBuf *hgb; static HTex *hgt; static HShader *hgs; static HProg *hgp;
static GLuint hg_arraybuf, hg_elembuf, hg_prog, hg_fb; static int hg_unit; static GLuint hg_bound[16]; static GLuint hg_fbtex[64];
static struct { int enabled, size, stride, norm; GLenum type; const void *ptr; GLuint buf; } hga[HG_MAXATTR];
// perf (b45): a hash of all the data uploaded (glBufferData / glTexImage2D / glCompressedTexImage2D), so two builds that load
// differently (read-ahead, threads) can be checked to hand the GL the same bytes in the same order (printed at exit)
static unsigned long long hg_up_hash = 1469598103934665603ull, hg_up_bytes;
static void hg_up(const void *d, size_t n) { const unsigned char *b = d; unsigned long long h = hg_up_hash; if (b) for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; h = (h ^ n) * 1099511628211ull; hg_up_hash = h; hg_up_bytes += n; }
static void hg_gl_init(void) {
	if (hgb) return;
	// vitaGL's buffer names are pointers to its vbo structs (texcanary.h reads them): the pool lives below 2 GB so they fit a GLuint
	hgb = mmap(NULL, sizeof(HBuf) * HG_MAXBUF, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (hgb == MAP_FAILED) { fprintf(stderr, "hostgame: no low memory for the buffer pool\n"); exit(1); }
	hgt = calloc(HG_MAXTEX, sizeof(HTex)); hgs = calloc(HG_MAXSH, sizeof(HShader)); hgp = calloc(HG_MAXSH, sizeof(HProg));
}
static GLuint hg_bid(int k) { return (GLuint)(uintptr_t)&hgb[k]; }
static HBuf *hg_buf(GLuint id) {   // a live buffer by name, or NULL
	uintptr_t a = id, b0 = (uintptr_t)hgb; if (!id || !hgb || a < b0 || a >= b0 + sizeof(HBuf) * HG_MAXBUF || (a - b0) % sizeof(HBuf)) return NULL;
	HBuf *B = (HBuf *)a; return B->live ? B : NULL;
}
static unsigned hg_tsize(GLenum t) { return t == GL_FLOAT || t == GL_INT || t == GL_UNSIGNED_INT ? 4 : t == GL_SHORT || t == GL_UNSIGNED_SHORT || t == GL_HALF_FLOAT ? 2 : 1; }
// pending texture uploads (vitaGL copies through the transfer queue: the source must stay valid until it has finished)
typedef struct { GLuint tex; int gen; size_t off, len; const unsigned char *src; uint64_t hash; void *site; int rows, row_src, row_dst; } HXfer;
static HXfer *hgx; static int hgx_n, hgx_cap;
static uint64_t hg_hash(const unsigned char *p, size_t n) { uint64_t h = 1469598103934665603ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; } return h; }
static void hg_xfer_run(HXfer *x) {
	HTex *T = x->tex < HG_MAXTEX ? &hgt[x->tex] : NULL; if (!T || !T->live || T->gen != x->gen) return;
	size_t span = x->rows ? (size_t)x->row_src * x->rows : x->len;
	if (hg_hash(x->src, span) != x->hash) hg_report(x->site, "UPLOAD-SOURCE-CHANGED", "texture %u: the source of an upload (%zu bytes at %p) changed before the transfer finished (the GPU would copy the new bytes)", x->tex, span, (const void *)x->src);
	if (x->rows) { for (int r = 0; r < x->rows; r++) memcpy(T->data + x->off + (size_t)r * x->row_dst, x->src + (size_t)r * x->row_src, x->row_src); }
	else memcpy(T->data + x->off, x->src, x->len);
}
static void hg_xfer_flush_tex(GLuint id) { for (int i = 0; i < hgx_n; ) { if (hgx[i].tex == id) { HXfer x = hgx[i]; memmove(&hgx[i], &hgx[i + 1], (size_t)(hgx_n - i - 1) * sizeof(HXfer)); hgx_n--; hg_xfer_run(&x); } else i++; } }
static void hg_xfer_flush(void) { for (int i = 0; i < hgx_n; i++) hg_xfer_run(&hgx[i]); hgx_n = 0; }
static void hg_xfer_add(GLuint tex, size_t off, const void *src, size_t len, int rows, int row_src, int row_dst, void *site) {
	if (!src || (!len && !rows)) return;
	if (hgx_n == hgx_cap) { hgx_cap = hgx_cap ? hgx_cap * 2 : 256; hgx = realloc(hgx, sizeof(HXfer) * hgx_cap); }
	HXfer *x = &hgx[hgx_n++]; x->tex = tex; x->gen = hgt[tex].gen; x->off = off; x->len = len; x->src = src; x->site = site; x->rows = rows; x->row_src = row_src; x->row_dst = row_dst;
	x->hash = hg_hash(src, rows ? (size_t)row_src * rows : len);   // (reads the whole source now: an undersized one shows up here)
}
void sceGxmTransferFinish(void) { hg_xfer_flush(); }
int sceGxmEndScene(void *c, const SceGxmNotification *v, const SceGxmNotification *f) { (void)c; (void)v; (void)f; return 0; }


// ------------------------------------------------------------------ leak tracker (build.sh leaks: -DHG_LEAKS, -Wl,--wrap=malloc ...)
// Every heap block the game allocates (and every GL texture / buffer name) is recorded with its call stack; hg_snapshot(label)
// writes the live ones grouped by stack to HG_OUT/snap_<label>.txt (tools/hostgame/leakdiff.py compares two snapshots).
#ifdef HG_LEAKS
#include <execinfo.h>
void *__real_malloc(size_t); void __real_free(void *); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
int __real_posix_memalign(void **, size_t, size_t); void *__real_memalign(size_t, size_t); void *__real_aligned_alloc(size_t, size_t);
#define HL_DEPTH 8
#define HL_SITES 65536
#define HL_TAB (1u << 22)
typedef struct { uintptr_t pc[HL_DEPTH]; int n; } HlSite;
typedef struct { uintptr_t p; size_t n; int site; } HlBlk;   // p 0: empty, 1: deleted
static HlSite *hl_site; static int hl_nsite, *hl_sidx; static HlBlk *hl_tab; static pthread_mutex_t hl_lk = PTHREAD_MUTEX_INITIALIZER; static __thread int hl_in;
static int hl_tex_site[HG_MAXTEX], hl_buf_site[HG_MAXBUF];
static int hl_capture(void) {   // the current stack (skipping this function and the wrapper) -> site id
	void *a[HL_DEPTH + 2]; int n = backtrace(a, HL_DEPTH + 2); HlSite S; memset(&S, 0, sizeof(S));
	for (int i = 2; i < n && S.n < HL_DEPTH; i++) S.pc[S.n++] = (uintptr_t)a[i];
	unsigned h = 2166136261u; for (int i = 0; i < S.n; i++) { h ^= (unsigned)(S.pc[i] >> 2); h *= 16777619u; }
	for (unsigned k = 0; k < 2 * HL_SITES; k++, h++) { int *e = &hl_sidx[h & (2 * HL_SITES - 1)];
		if (!*e) { if (hl_nsite >= HL_SITES - 1) return HL_SITES - 1; hl_site[hl_nsite] = S; *e = hl_nsite + 1; return hl_nsite++; }
		if (hl_site[*e - 1].n == S.n && !memcmp(hl_site[*e - 1].pc, S.pc, sizeof(S.pc))) return *e - 1; }
	return HL_SITES - 1;
}
static void hl_init(void) { if (!hl_tab) { hl_tab = __real_calloc(HL_TAB, sizeof(HlBlk)); hl_site = __real_calloc(HL_SITES, sizeof(HlSite)); hl_sidx = __real_calloc(2 * HL_SITES, sizeof(int)); } }
static void hl_add(void *p, size_t n) {
	if (!p || hl_in) return; hl_in = 1; pthread_mutex_lock(&hl_lk); hl_init();
	int site = hl_capture(); uintptr_t k = (uintptr_t)p; unsigned h = (unsigned)((k >> 4) * 2654435761u) & (HL_TAB - 1);
	for (unsigned i = 0; i < HL_TAB; i++, h = (h + 1) & (HL_TAB - 1)) if (hl_tab[h].p <= 1) { hl_tab[h].p = k; hl_tab[h].n = n; hl_tab[h].site = site; break; }
	pthread_mutex_unlock(&hl_lk); hl_in = 0;
}
static void hl_del(void *p) {
	if (!p || !hl_tab || hl_in) return; pthread_mutex_lock(&hl_lk);
	uintptr_t k = (uintptr_t)p; unsigned h = (unsigned)((k >> 4) * 2654435761u) & (HL_TAB - 1);
	for (unsigned i = 0; i < HL_TAB; i++, h = (h + 1) & (HL_TAB - 1)) { if (!hl_tab[h].p) break; if (hl_tab[h].p == k) { hl_tab[h].p = 1; break; } }
	pthread_mutex_unlock(&hl_lk);
}
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); hl_add(p, n); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); hl_add(p, a * b); return p; }
void *__wrap_realloc(void *o, size_t n) { hl_del(o); void *p = __real_realloc(o, n); hl_add(p, n); return p; }
void __wrap_free(void *p) { hl_del(p); __real_free(p); }
int __wrap_posix_memalign(void **r, size_t a, size_t n) { int e = __real_posix_memalign(r, a, n); if (!e) hl_add(*r, n); return e; }
void *__wrap_memalign(size_t a, size_t n) { void *p = __real_memalign(a, n); hl_add(p, n); return p; }
void *__wrap_aligned_alloc(size_t a, size_t n) { void *p = __real_aligned_alloc(a, n); hl_add(p, n); return p; }
static void hl_gl(int *tab, int id, int add) { if (hl_in) return; hl_in = 1; pthread_mutex_lock(&hl_lk); hl_init(); tab[id] = add ? hl_capture() + 1 : 0; pthread_mutex_unlock(&hl_lk); hl_in = 0; }
void hg_snapshot(const char *label) {
	if (!hl_tab) return; hl_in = 1; pthread_mutex_lock(&hl_lk);
	long *cnt = __real_calloc(HL_SITES, sizeof(long)), *byt = __real_calloc(HL_SITES, sizeof(long)), *tc = __real_calloc(HL_SITES, sizeof(long)), *bc = __real_calloc(HL_SITES, sizeof(long));
	for (unsigned h = 0; h < HL_TAB; h++) if (hl_tab[h].p > 1) { cnt[hl_tab[h].site]++; byt[hl_tab[h].site] += (long)hl_tab[h].n; }
	for (int i = 0; i < HG_MAXTEX; i++) if (hl_tex_site[i] && hgt && hgt[i].live) { tc[hl_tex_site[i] - 1]++; byt[hl_tex_site[i] - 1] += 0; }
	for (int i = 0; i < HG_MAXBUF; i++) if (hl_buf_site[i] && hgb && hgb[i].live) bc[hl_buf_site[i] - 1]++;
	char p[1024], lb[200]; snprintf(lb, sizeof(lb), "%s", label); for (char *c = lb; *c; c++) if (*c == ' ' || *c == '/') *c = '_';
	snprintf(p, sizeof(p), "%s/snap_%s.txt", hg_env("HG_OUT", "hg_out"), lb); FILE *f = fopen(p, "w");
	if (f) { for (int i = 0; i < hl_nsite; i++) if (cnt[i] || tc[i] || bc[i]) { fprintf(f, "%d %ld %ld %ld %ld", i, cnt[i], byt[i], tc[i], bc[i]); for (int k = 0; k < hl_site[i].n; k++) fprintf(f, " %lx", (unsigned long)hl_site[i].pc[k]); fprintf(f, "\n"); } fclose(f); }
	__real_free(cnt); __real_free(byt); __real_free(tc); __real_free(bc);
	pthread_mutex_unlock(&hl_lk); hl_in = 0;
}
#define HL_TEX(id, add) hl_gl(hl_tex_site, (int)(id), add)
#define HL_BUF(k, add) hl_gl(hl_buf_site, (int)(k), add)
#else
void hg_snapshot(const char *label) { (void)label; }
#define HL_TEX(id, add) ((void)0)
#define HL_BUF(k, add) ((void)0)
#endif
// ---- buffers
// HG_BUF_SPREAD=1: buffer names handed out round-robin over the whole pool (next free slot after the last one given) instead of
// the lowest free one, so names keep changing as vitaGL's (its vbo structs are heap blocks at varying addresses). perf b38:
// texcanary.h's buffer table filled up with the names of deleted buffers that way (QA's 75-minute soak, b37).
void glGenBuffers(GLsizei n, GLuint *b) { GLCHECK(); hg_gl_init(); static int spread = -1, cur = 1; if (spread < 0) spread = atoi(hg_env("HG_BUF_SPREAD", "0")) != 0;
	for (int i = 0; i < n; i++) { int k = 1;
		if (spread) { k = HG_MAXBUF; for (int s = 0; s < HG_MAXBUF - 1; s++) { int c = 1 + (cur - 1 + s) % (HG_MAXBUF - 1); if (!hgb[c].live) { k = c; cur = c % (HG_MAXBUF - 1) + 1; break; } } }
		else while (k < HG_MAXBUF && hgb[k].live) k++;
		if (k == HG_MAXBUF) { hg_report(SITE, "GL-OUT-OF-IDS", "buffers"); b[i] = 0; continue; } memset(&hgb[k], 0, sizeof(HBuf)); hgb[k].live = 1; b[i] = hg_bid(k); HL_BUF(k, 1); } }
static int hg_live_bufs(void) { int n = 0; if (hgb) for (int k = 1; k < HG_MAXBUF; k++) n += hgb[k].live != 0; return n; }
void glDeleteBuffers(GLsizei n, const GLuint *b) {
	GLCHECK(); for (int i = 0; i < n; i++) { GLuint id = b[i]; if (!id) continue; HBuf *B = hg_buf(id); if (!B) { hg_report(SITE, "GL-DELETE-NOT-LIVE", "glDeleteBuffers(%u): not a live buffer", id); continue; }
		free(B->data); memset(B, 0, sizeof(HBuf)); HL_BUF(B - hgb, 0);
		if (hg_arraybuf == id) hg_arraybuf = 0; if (hg_elembuf == id) hg_elembuf = 0; }
}
void glBindBuffer(GLenum target, GLuint id) {
	GLCHECK(); hg_gl_init(); if (id && !hg_buf(id)) hg_report(SITE, "GL-BIND-NOT-LIVE", "glBindBuffer(%u): not a live buffer", id);
	if (target == GL_ARRAY_BUFFER) hg_arraybuf = id; else if (target == GL_ELEMENT_ARRAY_BUFFER) hg_elembuf = id;
}
static HBuf *hg_tbuf(GLenum target) { GLuint id = target == GL_ARRAY_BUFFER ? hg_arraybuf : target == GL_ELEMENT_ARRAY_BUFFER ? hg_elembuf : 0; return hg_buf(id); }
void glBufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
	hg_up(data, (size_t)size);
	GLCHECK(); (void)usage; HBuf *B = hg_tbuf(target); if (!B) { hg_report(SITE, "GL-NO-BUFFER", "glBufferData with no buffer bound"); return; }
	if (size < 0) { hg_report(SITE, "GL-BAD-SIZE", "glBufferData size %ld", (long)size); return; }
	free(B->data); B->data = size ? malloc((size_t)size) : NULL; B->size = (size_t)size; B->size32 = (int32_t)size; B->mapped = 0;
	if (B->data) { if (data) memcpy(B->data, data, (size_t)size); else memset(B->data, 0, (size_t)size); }
}
void glBufferSubData(GLenum target, GLintptr off, GLsizeiptr size, const void *data) {
	GLCHECK(); HBuf *B = hg_tbuf(target); if (!B) { hg_report(SITE, "GL-NO-BUFFER", "glBufferSubData with no buffer bound"); return; }
	if (off < 0 || size < 0 || (size_t)off + (size_t)size > B->size) { hg_report(SITE, "GL-BUFFER-OVERFLOW", "glBufferSubData %ld + %ld into a buffer of %zu bytes", (long)off, (long)size, B->size); return; }
	if (size && data) memcpy(B->data + off, data, (size_t)size);
}
void *glMapBuffer(GLenum target, GLbitfield access) { GLCHECK(); (void)access; HBuf *B = hg_tbuf(target); if (!B) { hg_report(SITE, "GL-NO-BUFFER", "glMapBuffer with no buffer bound"); return NULL; } B->mapped = 1; B->ever_mapped = 1; return B->data; }
GLboolean glUnmapBuffer(GLenum target) { GLCHECK(); HBuf *B = hg_tbuf(target); if (!B) return GL_FALSE; B->mapped = 0; return GL_TRUE; }

// ---- textures (vitaGL's layouts: RGBA8 rows padded to 8 pixels, DXT 4x4 blocks, the mip chain in one allocation)
static int hg_kind(GLenum f) { return f == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || f == GL_COMPRESSED_RGB_S3TC_DXT1_EXT ? 1 : f == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT ? 3 : f == GL_COMPRESSED_RGBA_S3TC_DXT5_EXT ? 5 : 0; }
static unsigned hg_bpp(GLenum f) { return f == GL_RGBA || f == GL_RGB ? 4 : f == GL_LUMINANCE_ALPHA ? 2 : 1; }   // stored bytes per pixel
static size_t hg_lsize(int kind, GLenum fmt, unsigned w, unsigned h) {
	if (kind) return (size_t)((w + 3) / 4) * ((h + 3) / 4) * (kind == 1 ? 8 : 16);
	return (size_t)((w + 7) & ~7u) * h * hg_bpp(fmt);
}
static size_t hg_loff(const HTex *T, int level) { size_t o = 0; unsigned w = T->w, h = T->h; for (int k = 0; k < level; k++) { o += hg_lsize(T->kind, T->fmt, w, h); if (w > 1) w /= 2; if (h > 1) h /= 2; } return o; }
static HTex *hg_curtex(void) { GLuint id = hg_bound[hg_unit]; return id && id < HG_MAXTEX && hgt[id].live ? &hgt[id] : NULL; }
void glGenTextures(GLsizei n, GLuint *t) {
	GLCHECK(); hg_gl_init(); for (int i = 0; i < n; i++) { int k = 1; while (k < HG_MAXTEX && hgt[k].live) k++; if (k == HG_MAXTEX) { hg_report(SITE, "GL-OUT-OF-IDS", "textures"); t[i] = 0; continue; }
		int g = hgt[k].gen; memset(&hgt[k], 0, sizeof(HTex)); hgt[k].live = 1; hgt[k].gen = g + 1; t[i] = k; HL_TEX(k, 1); }
}
void glDeleteTextures(GLsizei n, const GLuint *t) {
	GLCHECK(); for (int i = 0; i < n; i++) { GLuint id = t[i]; if (!id) continue; if (id >= HG_MAXTEX || !hgt[id].live) { hg_report(SITE, "GL-DELETE-NOT-LIVE", "glDeleteTextures(%u): not a live texture", id); continue; }
		free(hgt[id].data); int g = hgt[id].gen; memset(&hgt[id], 0, sizeof(HTex)); hgt[id].gen = g; HL_TEX(id, 0);
		for (int u = 0; u < 16; u++) if (hg_bound[u] == id) hg_bound[u] = 0; }
}
void glBindTexture(GLenum target, GLuint id) { GLCHECK(); hg_gl_init(); (void)target; if (id && (id >= HG_MAXTEX || !hgt[id].live)) hg_report(SITE, "GL-BIND-NOT-LIVE", "glBindTexture(%u): not a live texture", id); hg_bound[hg_unit] = id < HG_MAXTEX ? id : 0; }
void glActiveTexture(GLenum t) { GLCHECK(); int u = (int)t - GL_TEXTURE0; if (u < 0 || u >= 16) { hg_report(SITE, "GL-BAD-ENUM", "glActiveTexture(0x%X)", t); return; } hg_unit = u; }
static void hg_tex_alloc(HTex *T, GLuint id, size_t need) {   // grow the texture's memory (vitaGL: a new allocation, the old levels copied)
	if (need <= T->size && T->data) return;
	hg_xfer_flush_tex(id);
	unsigned char *n = malloc(need ? need : 1); memset(n, 0, need ? need : 1); if (T->data) memcpy(n, T->data, T->size < need ? T->size : need);
	free(T->data); T->data = n; T->size = need; T->gxm.data = n;
}
void glTexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void *data) {
	GLCHECK(); (void)target; (void)ifmt; (void)border; HTex *T = hg_curtex(); GLuint id = hg_bound[hg_unit];
	if (!T) { hg_report(SITE, "GL-NO-TEXTURE", "glTexImage2D with no live texture bound (unit %d, id %u)", hg_unit, id); return; }
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || level < 0 || level > 12) { hg_report(SITE, "GL-BAD-SIZE", "glTexImage2D %dx%d level %d", w, h, level); return; }
	if (data && fmt == GL_RGBA && type == GL_UNSIGNED_BYTE) hg_up(data, (size_t)w * h * 4);   // (perf b45: the upload hash; other formats left out)
	if (type != GL_UNSIGNED_BYTE) hg_report(SITE, "GL-BAD-ENUM", "glTexImage2D type 0x%X", type);
	if (level == 0) { hg_xfer_flush_tex(id); free(T->data); T->data = NULL; T->size = 0; T->w = w; T->h = h; T->fmt = fmt; T->kind = 0; T->nlev = 1; T->site = SITE; }
	else if (T->kind || T->fmt != fmt) { hg_report(SITE, "GL-MIP-MISMATCH", "glTexImage2D level %d format 0x%X onto texture %u (level 0 format 0x%X)", level, fmt, id, T->fmt); return; }
	size_t off = hg_loff(T, level), ls = hg_lsize(0, fmt, w, h); hg_tex_alloc(T, id, off + ls); if (level + 1 > T->nlev) T->nlev = level + 1;
	T->gxm.data = T->data; T->gxm.w = T->w; T->gxm.h = T->h; T->gxm.fmt = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR; T->gxm.kind = 0;
	if (data) { unsigned sb = fmt == GL_RGB ? 3 : fmt == GL_RGBA ? 4 : fmt == GL_LUMINANCE_ALPHA ? 2 : 1, db = hg_bpp(fmt);
		if (sb == db) hg_xfer_add(id, off, data, 0, h, w * sb, ((w + 7) & ~7) * db, SITE);
		else { const unsigned char *s = data; volatile unsigned x = 0; x += s[0]; x += s[(size_t)w * h * sb - 1]; (void)x; hg_xfer_add(id, off, data, (size_t)w * h * sb < ls ? (size_t)w * h * sb : ls, 0, 0, 0, SITE); } }
}
void glCompressedTexImage2D(GLenum target, GLint level, GLenum ifmt, GLsizei w, GLsizei h, GLint border, GLsizei sz, const void *data) {
	hg_up(data, (size_t)sz);
	GLCHECK(); (void)target; (void)border; HTex *T = hg_curtex(); GLuint id = hg_bound[hg_unit]; int kind = hg_kind(ifmt);
	if (!T) { hg_report(SITE, "GL-NO-TEXTURE", "glCompressedTexImage2D with no live texture bound (unit %d, id %u)", hg_unit, id); return; }
	if (!kind) { hg_report(SITE, "GL-BAD-ENUM", "glCompressedTexImage2D format 0x%X", ifmt); return; }
	if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || level < 0 || level > 12) { hg_report(SITE, "GL-BAD-SIZE", "glCompressedTexImage2D %dx%d level %d", w, h, level); return; }
	size_t ls = hg_lsize(kind, ifmt, w, h);
	if ((size_t)sz != ls) hg_report(SITE, "GL-COMPRESSED-SIZE", "glCompressedTexImage2D %dx%d level %d: %d bytes given, the format needs %zu", w, h, level, sz, ls);
	if (level == 0) { hg_xfer_flush_tex(id); free(T->data); T->data = NULL; T->size = 0; T->w = w; T->h = h; T->fmt = ifmt; T->kind = kind; T->nlev = 1; T->site = SITE; }
	else if (T->kind != kind) { hg_report(SITE, "GL-MIP-MISMATCH", "compressed level %d of another format onto texture %u", level, id); return; }
	else { unsigned ew = T->w, eh = T->h; for (int k = 0; k < level; k++) { if (ew > 1) ew /= 2; if (eh > 1) eh /= 2; } if ((unsigned)w != ew || (unsigned)h != eh) hg_report(SITE, "GL-MIP-MISMATCH", "texture %u level %d is %dx%d, the chain wants %ux%u", id, level, w, h, ew, eh); }
	size_t off = hg_loff(T, level); hg_tex_alloc(T, id, off + ls); if (level + 1 > T->nlev) T->nlev = level + 1;
	T->gxm.data = T->data; T->gxm.w = T->w; T->gxm.h = T->h; T->gxm.fmt = kind == 1 ? SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR : SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR; T->gxm.kind = kind;
	if (data && sz > 0) hg_xfer_add(id, off, data, (size_t)sz < ls ? (size_t)sz : ls, 0, 0, 0, SITE);
	if (data && (size_t)sz > ls) { volatile unsigned char x = ((const unsigned char *)data)[sz - 1]; (void)x; }   // (the caller says it has sz bytes)
}
void glTexParameteri(GLenum target, GLenum pname, GLint param) { GLCHECK(); (void)target; (void)pname; (void)param; }
void glGenerateMipmap(GLenum target) {
	GLCHECK(); (void)target; HTex *T = hg_curtex(); if (!T) { hg_report(SITE, "GL-NO-TEXTURE", "glGenerateMipmap with no live texture bound"); return; }
	if (T->kind) return; int n = 1; unsigned w = T->w, h = T->h; while (w > 1 || h > 1) { if (w > 1) w /= 2; if (h > 1) h /= 2; n++; }
	hg_tex_alloc(T, hg_bound[hg_unit], hg_loff(T, n)); T->nlev = n; T->gxm.data = T->data;
}
void *vglGetTexDataPointer(GLenum target) { (void)target; HTex *T = hg_curtex(); return T ? T->data : NULL; }
SceGxmTexture *vglGetGxmTexture(GLenum target) { (void)target; HTex *T = hg_curtex(); return T ? &T->gxm : NULL; }
void vglTexImageDepthBuffer(GLenum target) { (void)target; }
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *data, SceGxmTextureFormat fmt, unsigned int w, unsigned int h, unsigned int mips) {
	if (!t) return -1; t->data = (void *)data; t->fmt = fmt; t->w = w; t->h = h; t->mips = mips; t->kind = 9; return 0;
}
int sceGxmTextureSetMinFilter(SceGxmTexture *t, SceGxmTextureFilter f) { t->minf = f; return 0; }
int sceGxmTextureSetMagFilter(SceGxmTexture *t, SceGxmTextureFilter f) { t->magf = f; return 0; }
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m) { t->ua = m; return 0; }
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m) { t->va = m; return 0; }
void *sceGxmTextureGetData(const SceGxmTexture *t) { return t->data; }
unsigned int sceGxmTextureGetWidth(const SceGxmTexture *t) { return t->w; }
unsigned int sceGxmTextureGetHeight(const SceGxmTexture *t) { return t->h; }
SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *t) { return t->fmt; }

// ---- shaders and programs (Cg sources parsed for their uniforms: names, sizes, sampler units)
static int hg_comps(const char *ty) {
	if (!strncmp(ty, "sampler", 7)) return -1;
	int c = 1; const char *d = ty; while (*d && !(*d >= '0' && *d <= '9')) d++;
	if (*d) { c = *d - '0'; if (d[1] == 'x' && d[2] >= '1' && d[2] <= '4') c *= d[2] - '0'; }
	return c;
}
static void hg_parse_uniforms(HShader *S) {
	S->nu = 0; const char *p = S->src; if (!p) return;
	while ((p = strstr(p, "uniform")) && S->nu < HG_MAXU) {
		if (p > S->src && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) { p += 7; continue; }
		const char *q = p + 7; while (*q == ' ' || *q == '\t' || *q == '\n') q++;
		char ty[32] = { 0 }, nm[48] = { 0 }; int i = 0; while ((isalnum((unsigned char)*q) || *q == '_') && i < 31) ty[i++] = *q++;
		while (*q == ' ' || *q == '\t' || *q == '\n') q++;
		i = 0; while ((isalnum((unsigned char)*q) || *q == '_') && i < 47) nm[i++] = *q++;
		p = q; if (!ty[0] || !nm[0]) continue;
		int arr = 1; while (*q == ' ') q++; if (*q == '[') arr = atoi(q + 1);
		int unit = -1; const char *col = q; while (*col && *col != ',' && *col != ')' && *col != ';' && *col != ':' ) col++;
		if (*col == ':') { const char *tu = col + 1; while (*tu == ' ') tu++; if (!strncmp(tu, "TEXUNIT", 7)) unit = atoi(tu + 7); }
		int c = hg_comps(ty); int dup = 0; for (int k = 0; k < S->nu; k++) if (!strcmp(S->u[k].name, nm)) dup = 1; if (dup) continue;
		HUni *U = &S->u[S->nu++]; snprintf(U->name, sizeof(U->name), "%s", nm); U->sampler = c < 0; U->comps = c < 0 ? 1 : c; U->arr = arr > 0 ? arr : 1; U->unit = unit;
	}
}
GLuint glCreateShader(GLenum type) { GLCHECK(); hg_gl_init(); for (int k = 1; k < HG_MAXSH; k++) if (!hgs[k].live) { memset(&hgs[k], 0, sizeof(HShader)); hgs[k].live = 1; hgs[k].type = type; return k; } hg_report(SITE, "GL-OUT-OF-IDS", "shaders"); return 0; }
static HShader *hg_sh(GLuint s) { return s && s < HG_MAXSH && hgs[s].live ? &hgs[s] : NULL; }
void glShaderSource(GLuint s, GLsizei count, const GLchar *const *str, const GLint *len) {
	GLCHECK(); HShader *S = hg_sh(s); if (!S) { hg_report(SITE, "GL-NOT-LIVE", "glShaderSource(%u)", s); return; }
	size_t tot = 0; for (int i = 0; i < count; i++) tot += len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]);
	free(S->src); S->src = malloc(tot + 1); size_t o = 0;
	for (int i = 0; i < count; i++) { size_t l = len && len[i] >= 0 ? (size_t)len[i] : strlen(str[i]); memcpy(S->src + o, str[i], l); o += l; } S->src[o] = 0;
}
void glCompileShader(GLuint s) { GLCHECK(); HShader *S = hg_sh(s); if (!S) { hg_report(SITE, "GL-NOT-LIVE", "glCompileShader(%u)", s); return; } S->compiled = S->src != NULL; hg_parse_uniforms(S); }
void glShaderBinary(GLsizei n, const GLuint *sh, GLenum fmt, const void *bin, GLsizei len) {
	GLCHECK(); (void)fmt; if (n != 1) return; HShader *S = hg_sh(sh[0]); if (!S || !bin || len < 8) return;
	const unsigned char *b = bin; volatile unsigned char x = b[len - 1]; (void)x;
	if (memcmp(b, "HGSB", 4)) { S->compiled = 0; return; }
	free(S->src); S->src = malloc((size_t)len - 8 + 1); memcpy(S->src, b + 8, (size_t)len - 8); S->src[len - 8] = 0; S->compiled = 1; hg_parse_uniforms(S);
}
void vglGetShaderBinary(GLuint s, GLsizei bufSize, GLsizei *length, void *binary) {
	HShader *S = hg_sh(s); if (length) *length = 0; if (!S || !S->src || !binary) return;
	size_t l = strlen(S->src); if ((size_t)bufSize < l + 8) { hg_report(SITE, "GL-SMALL-BUFFER", "vglGetShaderBinary: %d bytes for a %zu byte binary", bufSize, l + 8); return; }
	unsigned char *b = binary; memcpy(b, "HGSB", 4); unsigned t = (unsigned)S->type; memcpy(b + 4, &t, 4); memcpy(b + 8, S->src, l); if (length) *length = (GLsizei)(l + 8);
}
void glGetShaderiv(GLuint s, GLenum pname, GLint *p) {
	GLCHECK(); HShader *S = hg_sh(s); if (!S) { hg_report(SITE, "GL-NOT-LIVE", "glGetShaderiv(%u)", s); *p = 0; return; }
	*p = pname == GL_COMPILE_STATUS ? S->compiled : pname == GL_SHADER_TYPE ? S->type : pname == GL_INFO_LOG_LENGTH ? 1 : 0;
}
void glGetShaderInfoLog(GLuint s, GLsizei max, GLsizei *len, GLchar *log) { (void)s; if (max > 0 && log) log[0] = 0; if (len) *len = 0; }
void glDeleteShader(GLuint s) { GLCHECK(); HShader *S = hg_sh(s); if (!S) return; free(S->src); memset(S, 0, sizeof(*S)); }
GLuint glCreateProgram(void) { GLCHECK(); hg_gl_init(); for (int k = 1; k < HG_MAXSH; k++) if (!hgp[k].live) { memset(&hgp[k], 0, sizeof(HProg)); hgp[k].live = 1; return k; } hg_report(SITE, "GL-OUT-OF-IDS", "programs"); return 0; }
static HProg *hg_pr(GLuint p) { return p && p < HG_MAXSH && hgp[p].live ? &hgp[p] : NULL; }
void glDeleteProgram(GLuint p) { GLCHECK(); HProg *P = hg_pr(p); if (P) memset(P, 0, sizeof(*P)); if (hg_prog == p) hg_prog = 0; }
void glAttachShader(GLuint p, GLuint s) { GLCHECK(); HProg *P = hg_pr(p); if (!P || !hg_sh(s)) { hg_report(SITE, "GL-NOT-LIVE", "glAttachShader(%u, %u)", p, s); return; } if (P->nsh < 4) P->sh[P->nsh++] = s; }
void glBindAttribLocation(GLuint p, GLuint i, const GLchar *name) { GLCHECK(); HProg *P = hg_pr(p); (void)strlen(name); if (P && i < HG_MAXATTR) P->attrmask |= 1u << i; }
void glLinkProgram(GLuint p) {
	GLCHECK(); HProg *P = hg_pr(p); if (!P) { hg_report(SITE, "GL-NOT-LIVE", "glLinkProgram(%u)", p); return; }
	P->nu = 0; P->linked = 1;
	for (int k = 0; k < P->nsh; k++) { HShader *S = hg_sh(P->sh[k]); if (!S) { P->linked = 0; continue; } if (!S->compiled) P->linked = 0;
		for (int j = 0; j < S->nu && P->nu < HG_MAXU; j++) { int dup = -1; for (int m = 0; m < P->nu; m++) if (!strcmp(P->u[m].name, S->u[j].name)) dup = m;
			if (dup < 0) P->u[P->nu++] = S->u[j]; else if (P->u[dup].arr < S->u[j].arr) P->u[dup].arr = S->u[j].arr; } }
	for (int m = 0; m < P->nu; m++) if (P->u[m].sampler && P->u[m].unit < 0) P->u[m].unit = 0;
	if (!P->attrmask) P->attrmask = 1;
}
void glGetProgramiv(GLuint p, GLenum pname, GLint *v) { GLCHECK(); HProg *P = hg_pr(p); *v = !P ? 0 : pname == GL_LINK_STATUS ? P->linked : pname == GL_ACTIVE_UNIFORMS ? P->nu : 0; }
void glGetActiveUniform(GLuint p, GLuint i, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name) {
	GLCHECK(); HProg *P = hg_pr(p); if (!P || (int)i >= P->nu) { if (length) *length = 0; return; }
	const HUni *U = &P->u[i]; int l = (int)strlen(U->name); if (l > bufSize - 1) l = bufSize - 1; if (l < 0) l = 0;
	if (bufSize > 0) { memcpy(name, U->name, l); name[l] = 0; } if (length) *length = l; if (size) *size = U->arr; if (type) *type = U->sampler ? 0x8B5E : 0x8B52;
}
#define HG_LOC(p, k) ((GLint)((p) * 128 + (k) + 1))
GLint glGetUniformLocation(GLuint p, const GLchar *name) {
	GLCHECK(); HProg *P = hg_pr(p); if (!P) { hg_report(SITE, "GL-NOT-LIVE", "glGetUniformLocation(%u, %s)", p, name); return -1; }
	char nm[64]; snprintf(nm, sizeof(nm), "%s", name); char *br = strchr(nm, '['); if (br) *br = 0;
	for (int k = 0; k < P->nu; k++) if (!strcmp(P->u[k].name, nm)) return HG_LOC(p, k);
	return -1;
}
void glUseProgram(GLuint p) { GLCHECK(); if (p && !hg_pr(p)) hg_report(SITE, "GL-NOT-LIVE", "glUseProgram(%u)", p); hg_prog = p; }
static HUni *hg_uni(GLint loc, void *site) {
	if (loc < 0) return NULL;
	GLuint p = (GLuint)((loc - 1) / 128); int k = (loc - 1) % 128; HProg *P = hg_pr(p);
	if (!P || k >= P->nu) { hg_report(site, "GL-UNIFORM-STALE", "uniform location %d: no such uniform", loc); return NULL; }
	if (p != hg_prog) { hg_report(site, "GL-UNIFORM-OTHER-PROGRAM", "uniform %s of program %u set while program %u is in use (vitaGL writes the other program's buffer)", P->u[k].name, p, hg_prog); }
	return &P->u[k];
}
static void hg_uniform(GLint loc, int comps, int count, const GLfloat *v, void *site) {
	HUni *U = hg_uni(loc, site); if (!U) return;
	if (count < 0) { hg_report(site, "GL-BAD-SIZE", "uniform %s count %d", U->name, count); return; }
	if (!U->sampler && comps > 0 && (long)comps * count > (long)U->comps * U->arr) hg_report(site, "GL-UNIFORM-OVERFLOW", "uniform %s: %d x %d floats written, declared %d x %d", U->name, count, comps, U->arr, U->comps);
	if (v && count > 0) { static float sink[4096]; size_t n = (size_t)comps * count; if (n > 4096) n = 4096; memcpy(sink, v, n * sizeof(float)); }
}
void glUniform1f(GLint l, GLfloat a) { GLCHECK(); float v[1] = { a }; hg_uniform(l, 1, 1, v, SITE); }
void glUniform2f(GLint l, GLfloat a, GLfloat b) { GLCHECK(); float v[2] = { a, b }; hg_uniform(l, 2, 1, v, SITE); }
void glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { GLCHECK(); float v[3] = { a, b, c }; hg_uniform(l, 3, 1, v, SITE); }
void glUniform4f(GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d) { GLCHECK(); float v[4] = { a, b, c, d }; hg_uniform(l, 4, 1, v, SITE); }
void glUniform1fv(GLint l, GLsizei n, const GLfloat *v) { GLCHECK(); hg_uniform(l, 1, n, v, SITE); }
void glUniform2fv(GLint l, GLsizei n, const GLfloat *v) { GLCHECK(); hg_uniform(l, 2, n, v, SITE); }
void glUniform3fv(GLint l, GLsizei n, const GLfloat *v) { GLCHECK(); hg_uniform(l, 3, n, v, SITE); }
void glUniform4fv(GLint l, GLsizei n, const GLfloat *v) { GLCHECK(); hg_uniform(l, 4, n, v, SITE); }
void glUniformMatrix4fv(GLint l, GLsizei n, GLboolean t, const GLfloat *v) { GLCHECK(); (void)t; hg_uniform(l, 16, n, v, SITE); }
void glUniform1i(GLint l, GLint i) { GLCHECK(); HUni *U = hg_uni(l, SITE); if (U && U->sampler) U->unit = i; }

// ---- vertex arrays and draws
void glEnableVertexAttribArray(GLuint i) { GLCHECK(); if (i < HG_MAXATTR) hga[i].enabled = 1; }
void glDisableVertexAttribArray(GLuint i) { GLCHECK(); if (i < HG_MAXATTR) hga[i].enabled = 0; }
void glVertexAttribPointer(GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr) {
	GLCHECK(); if (i >= HG_MAXATTR) { hg_report(SITE, "GL-BAD-ENUM", "attribute %u", i); return; }
	hga[i].size = size; hga[i].type = type; hga[i].norm = norm; hga[i].stride = stride; hga[i].ptr = ptr; hga[i].buf = hg_arraybuf;
}
static unsigned char *hg_scratch; static size_t hg_scratch_n;
static void hg_read(const void *p, size_t n) { if (!n) return; if (n > hg_scratch_n) { hg_scratch_n = n * 2; hg_scratch = realloc(hg_scratch, hg_scratch_n); } memcpy(hg_scratch, p, n); }
static void hg_touch_tex(int unit, void *site) {
	GLuint id = hg_bound[unit]; if (!id) return;
	if (id >= HG_MAXTEX || !hgt[id].live) { hg_report(site, "GL-DRAW-DELETED-TEXTURE", "draw samples unit %d holding texture %u that is not live", unit, id); return; }
	const HTex *T = &hgt[id]; const SceGxmTexture *g = &T->gxm; volatile unsigned char x = 0;
	if (g->kind == 9 && g->data) {   // re-pointed (sceGxmTextureInitLinear): the memory it points at, first and last byte
		size_t sz = g->fmt == SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1 ? (size_t)g->w * g->h * 3 / 2 : (size_t)g->w * g->h * 4;
		const unsigned char *d = g->data; x += d[0]; if (sz) x += d[sz - 1];
	} else if (T->data && T->size) { x += T->data[0]; x += T->data[T->size - 1]; }
	(void)x;
}
// perf (b46): HG_DRAWHASH=N: on every Nth frame, a hash of the bytes each enabled attribute of the program fetches for every
// vertex every draw reads, in draw order: the same for any layout of the data in buffers (world.h's shared buffers, b46)
static int hg_dh_on(void) { static int n = -1; if (n < 0) n = atoi(hg_env("HG_DRAWHASH", "0")); return n > 0 && hg_frame % (unsigned)n == 0; }
static void hg_dh_vertex(unsigned v) {
	HProg *P = hg_pr(hg_prog); if (!P) return; unsigned long long h = hg_dh;
	for (unsigned i = 0; i < HG_MAXATTR; i++) {
		if (!(P->attrmask >> i & 1) || !hga[i].enabled) continue;
		size_t es = (size_t)hga[i].size * hg_tsize(hga[i].type), st = hga[i].stride ? (size_t)hga[i].stride : es; const unsigned char *b;
		if (hga[i].buf) { HBuf *B = hg_buf(hga[i].buf); size_t off = (size_t)(uintptr_t)hga[i].ptr + (size_t)v * st; if (!B || !B->data || off + es > B->size) continue; b = B->data + off; }
		else { if (!hga[i].ptr) continue; b = (const unsigned char *)hga[i].ptr + (size_t)v * st; }
		h = (h ^ i) * 1099511628211ull; for (size_t k = 0; k < es; k++) h = (h ^ b[k]) * 1099511628211ull;
	}
	hg_dh = h; hg_dh_verts++;
}
static void hg_draw_check(unsigned vmin, unsigned vmax, void *site) {
	HProg *P = hg_pr(hg_prog); if (!P) { hg_report(site, "GL-DRAW-NO-PROGRAM", "draw with program %u", hg_prog); return; }
	for (unsigned i = 0; i < HG_MAXATTR; i++) {
		if (!(P->attrmask >> i & 1) || !hga[i].enabled) continue;
		size_t es = (size_t)hga[i].size * hg_tsize(hga[i].type), st = hga[i].stride ? (size_t)hga[i].stride : es, first = (size_t)vmin * st, end = (size_t)vmax * st + es;
		if (hga[i].buf) {
			GLuint b = hga[i].buf; HBuf *B = hg_buf(b); if (!B) { hg_report(site, "GL-DRAW-DELETED-BUFFER", "attribute %u reads buffer %u that is not live", i, b); continue; }
			size_t off = (size_t)(uintptr_t)hga[i].ptr;
			if (off + end > B->size) { hg_report(site, "GL-DRAW-PAST-BUFFER", "attribute %u reads vertices %u..%u (bytes %zu..%zu) of buffer %u holding %zu bytes", i, vmin, vmax, off + first, off + end, b, B->size); continue; }
			if (B->data && B->ever_mapped) hg_read(B->data + off + first, end - first);   // (buffers the CPU writes after creation: what the GPU would read, for ThreadSanitizer)
		} else {
			if (!hga[i].ptr) { hg_report(site, "GL-DRAW-NULL-ARRAY", "attribute %u enabled with no array", i); continue; }
			hg_read((const unsigned char *)hga[i].ptr + first, end - first);
		}
	}
	for (int m = 0; m < P->nu; m++) if (P->u[m].sampler && P->u[m].unit >= 0 && P->u[m].unit < 16) hg_touch_tex(P->u[m].unit, site);
}
static unsigned hg_draws;
void glDrawArrays(GLenum mode, GLint first, GLsizei count) { GLCHECK(); (void)mode; hg_draws++; if (count <= 0) return; if (first < 0) { hg_report(SITE, "GL-BAD-SIZE", "glDrawArrays first %d", first); return; } hg_draw_check((unsigned)first, (unsigned)(first + count - 1), SITE);
	static int arr = -1; if (arr < 0) arr = atoi(hg_env("HG_DRAWHASH_ARRAYS", "1")) != 0;   // (0: glDrawElements only: one HUD quad drawn from client arrays differs run to run, b46)
	if (arr && hg_dh_on()) { hg_dh_draws++; unsigned long long prev = hg_dh; hg_dh = 1469598103934665603ull ^ (unsigned)mode; for (GLsizei k = 0; k < count; k++) hg_dh_vertex((unsigned)(first + k));   // (perf b46)
		unsigned long long d = hg_dh; hg_dh = (prev ^ d) * 1099511628211ull;
		if (hg_frame >= (unsigned)atoi(hg_env("HG_DRAWHASH_FROM", "0")) && hg_frame <= (unsigned)atoi(hg_env("HG_DRAWHASH_TO", "0"))) fprintf(stderr, "HOSTGL drawarrays f%u: prog %u count %d tex0 %u -> %016llx\n", hg_frame, hg_prog, count, hg_bound[0], d); } }
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const void *indices) {
	GLCHECK(); (void)mode; hg_draws++; if (count <= 0) return; size_t is = hg_tsize(type); const unsigned char *ip = indices;
	if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT && type != GL_UNSIGNED_BYTE) { hg_report(SITE, "GL-BAD-ENUM", "glDrawElements type 0x%X", type); return; }
	if (hg_elembuf) { HBuf *B = hg_buf(hg_elembuf); size_t off = (size_t)(uintptr_t)indices;
		if (!B || off + count * is > B->size) { hg_report(SITE, "GL-DRAW-PAST-BUFFER", "glDrawElements reads %d indices at +%zu of an element buffer holding %zu bytes", count, off, B ? B->size : 0); return; }
		ip = B->data + off; }
	else if (!ip) { hg_report(SITE, "GL-DRAW-NULL-ARRAY", "glDrawElements with no indices"); return; }
	hg_read(ip, count * is); unsigned mn = ~0u, mx = 0;
	for (int k = 0; k < count; k++) { unsigned v = is == 2 ? ((const unsigned short *)ip)[k] : is == 4 ? ((const unsigned *)ip)[k] : ip[k]; if (v < mn) mn = v; if (v > mx) mx = v; }
	hg_draw_check(mn, mx, SITE);
	if (hg_dh_on()) { hg_dh_draws++; unsigned long long prev = hg_dh; hg_dh = 1469598103934665603ull ^ (unsigned)mode;   // (perf b46)
		for (int k = 0; k < count; k++) hg_dh_vertex(is == 2 ? ((const unsigned short *)ip)[k] : is == 4 ? ((const unsigned *)ip)[k] : ip[k]);
		unsigned long long d = hg_dh; hg_dh = (prev ^ d) * 1099511628211ull;
		static unsigned f0 = 1u << 31, f1; if (f0 == 1u << 31) { f0 = (unsigned)atoi(hg_env("HG_DRAWHASH_FROM", "0")); f1 = (unsigned)atoi(hg_env("HG_DRAWHASH_TO", "0")); }
		if (hg_frame >= f0 && hg_frame <= f1) fprintf(stderr, "HOSTGL draw f%u: prog %u count %d tex0 %u -> %016llx\n", hg_frame, hg_prog, count, hg_bound[0], d); }
}
void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void *data) {
	GLCHECK(); (void)x; (void)y; (void)type; if (w <= 0 || h <= 0 || !data) return; memset(data, 0, (size_t)w * h * (fmt == GL_RGB ? 3 : 4));
}
void glGetIntegerv(GLenum pname, GLint *v) { GLCHECK(); *v = pname == GL_TEXTURE_BINDING_2D ? (GLint)hg_bound[hg_unit] : pname == GL_ACTIVE_TEXTURE ? (GLint)(GL_TEXTURE0 + hg_unit) : 0; }
// ---- framebuffers
void glGenFramebuffers(GLsizei n, GLuint *f) { GLCHECK(); static GLuint next = 1; for (int i = 0; i < n; i++) { f[i] = next < 64 ? next++ : 0; } }
void glDeleteFramebuffers(GLsizei n, const GLuint *f) { GLCHECK(); for (int i = 0; i < n; i++) if (f[i] < 64) hg_fbtex[f[i]] = 0; }
void glBindFramebuffer(GLenum t, GLuint f) { GLCHECK(); (void)t; hg_fb = f < 64 ? f : 0; }
// b96: depth render buffers (main.c lu_blur: the result line-up's out-of-focus layer) - names only, complete
void glGenRenderbuffers(GLsizei n, GLuint *r) { GLCHECK(); static GLuint next = 1; for (int i = 0; i < n; i++) r[i] = next++; }
void glDeleteRenderbuffers(GLsizei n, const GLuint *r) { GLCHECK(); (void)n; (void)r; }
void glBindRenderbuffer(GLenum t, GLuint r) { GLCHECK(); (void)t; (void)r; }
void glRenderbufferStorage(GLenum t, GLenum f, GLsizei w, GLsizei h) { GLCHECK(); (void)t; (void)f; (void)w; (void)h; }
void glFramebufferRenderbuffer(GLenum t, GLenum a, GLenum rt, GLuint r) { GLCHECK(); (void)t; (void)a; (void)rt; (void)r; }
GLenum glCheckFramebufferStatus(GLenum t) { GLCHECK(); (void)t; return 0x8CD5; }
void glBlendFuncSeparate(GLenum a, GLenum b, GLenum c, GLenum d) { GLCHECK(); (void)a; (void)b; (void)c; (void)d; }
void glFramebufferTexture2D(GLenum t, GLenum a, GLenum tt, GLuint tex, GLint level) { GLCHECK(); (void)t; (void)a; (void)tt; (void)level; if (hg_fb) hg_fbtex[hg_fb] = tex; if (tex && (tex >= HG_MAXTEX || !hgt[tex].live)) hg_report(SITE, "GL-NOT-LIVE", "glFramebufferTexture2D(%u)", tex); }
// ---- state (no effect)
void glBlendFunc(GLenum a, GLenum b) { GLCHECK(); (void)a; (void)b; }
void glClear(GLbitfield m) { GLCHECK(); (void)m; if (hg_fb && hg_fbtex[hg_fb]) { GLuint t = hg_fbtex[hg_fb]; if (t >= HG_MAXTEX || !hgt[t].live) hg_report(SITE, "GL-DRAW-DELETED-TEXTURE", "render target texture %u is not live", t); } }
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { GLCHECK(); (void)r; (void)g; (void)b; (void)a; }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { GLCHECK(); (void)r; (void)g; (void)b; (void)a; }
void glCullFace(GLenum m) { GLCHECK(); (void)m; }
void glDepthFunc(GLenum f) { GLCHECK(); (void)f; }
void glDepthMask(GLboolean f) { GLCHECK(); (void)f; }
void glDisable(GLenum c) { GLCHECK(); (void)c; }
void glEnable(GLenum c) { GLCHECK(); (void)c; }
void glFrontFace(GLenum m) { GLCHECK(); (void)m; }
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) { GLCHECK(); (void)x; (void)y; (void)w; (void)h; }
void glStencilFunc(GLenum f, GLint r, GLuint m) { GLCHECK(); (void)f; (void)r; (void)m; }
void glStencilOp(GLenum a, GLenum b, GLenum c) { GLCHECK(); (void)a; (void)b; (void)c; }
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) { GLCHECK(); (void)x; (void)y; (void)w; (void)h; }
void glFinish(void) { GLCHECK(); hg_xfer_flush(); }

// ---- vitaGL: init, memory, the frame
SceGxmNotification query_fence; static volatile unsigned int hg_fence_val;
void *gxm_color_surfaces_addr[5]; unsigned int gxm_front_buffer_index; int DISPLAY_WIDTH = 960, DISPLAY_HEIGHT = 544, DISPLAY_STRIDE = 960; uint8_t gxm_display_buffer_count = 2;
static GLboolean hg_vgl_init(void) {
	hg_gl_init(); query_fence.address = &hg_fence_val; query_fence.value = 0;
	for (int i = 0; i < gxm_display_buffer_count; i++) if (!gxm_color_surfaces_addr[i]) gxm_color_surfaces_addr[i] = calloc((size_t)DISPLAY_STRIDE * DISPLAY_HEIGHT, 4);
	return GL_FALSE;
}
GLboolean vglInit(int a) { (void)a; return hg_vgl_init(); }
GLboolean vglInitExtended(int a, int w, int h, int r, SceGxmMultisampleMode m) { (void)a; (void)w; (void)h; (void)r; (void)m; return hg_vgl_init(); }
GLboolean vglInitWithCustomThreshold(int a, int w, int h, int r, int c, int p, int d, SceGxmMultisampleMode m) { (void)a; (void)w; (void)h; (void)r; (void)c; (void)p; (void)d; (void)m; return hg_vgl_init(); }
size_t vglMemFree(vglMemType t) { (void)t; return 64u << 20; }
void *vglAlloc(uint32_t size, vglMemType t) { (void)t; return malloc(size ? size : 1); }
void *vglMemalign(uint32_t a, uint32_t size) { void *p = NULL; if (a < sizeof(void *)) a = sizeof(void *); if (posix_memalign(&p, a, size ? size : 1)) return NULL; return p; }
void vglFree(void *p) { free(p); }
size_t vglMallocUsableSize(void *p) { return p ? malloc_usable_size(p) : 0; }
void vglPhycontMemLazyInit(void) { }
unsigned int vglGetFrameNumber(void) { return hg_frame; }
static unsigned hg_max_frames;
void vglSwapBuffers(GLboolean cd) {
	GLCHECK(); (void)cd; hg_xfer_flush();
	if (hg_dh_on()) { hg_dh_frames++; static int lg = -1; if (lg < 0) lg = atoi(hg_env("HG_DRAWHASH_LOG", "0")); if (lg) fprintf(stderr, "HOSTGL drawhash frame %u: %016llx after %llu draws\n", hg_frame, hg_dh, hg_dh_draws); }   // (perf b46: the frame just drawn was sampled)
	hg_frame++; __atomic_add_fetch(&hg_fence_val, 1, __ATOMIC_RELEASE); gxm_front_buffer_index = (gxm_front_buffer_index + 1) % gxm_display_buffer_count;
	static unsigned dt; if (!dt) dt = (unsigned)atoi(hg_env("HG_DT_US", "16667")); if (hg_realtime()) hg_frame_pace(dt); else hg_vadd(dt);
	if (!hg_max_frames) hg_max_frames = (unsigned)atoi(hg_env("HG_MAX_FRAMES", "400000"));
	static unsigned every; if (!every) every = (unsigned)atoi(hg_env("HG_STATUS_EVERY", "3600")); if (!every) every = 3600;   // (frames between status lines)
	if (hg_frame % every == 0) { fprintf(stderr, "hostgame: frame %u, game time %.0f s, %u draws per frame, %d live buffers, %d HOSTGL findings\n", hg_frame, hg_vget() / 1e6, hg_draws / every, hg_live_bufs(), hg_total); hg_draws = 0; }
	if (hg_frame >= hg_max_frames || (hg_stop_at && hg_frame >= hg_stop_at)) { fprintf(stderr, "hostgame: stopping at frame %u\n", hg_frame); fflush(NULL); hg_summary(); exit(0); }
}

// ------------------------------------------------------------------ gxm programs (skin_gpu.h's dump: the fake binaries have no GXP header)
unsigned int sceGxmProgramGetSize(const SceGxmProgram *p) { (void)p; return 0; }
unsigned int sceGxmProgramGetParameterCount(const SceGxmProgram *p) { (void)p; return 0; }
unsigned int sceGxmProgramGetDefaultUniformBufferSize(const SceGxmProgram *p) { (void)p; return 0; }
const SceGxmProgramParameter *sceGxmProgramGetParameter(const SceGxmProgram *p, unsigned int i) { (void)p; (void)i; return NULL; }
SceGxmParameterCategory sceGxmProgramParameterGetCategory(const SceGxmProgramParameter *q) { (void)q; return SCE_GXM_PARAMETER_CATEGORY_UNIFORM; }
unsigned int sceGxmProgramParameterGetType(const SceGxmProgramParameter *q) { (void)q; return 0; }
unsigned int sceGxmProgramParameterGetComponentCount(const SceGxmProgramParameter *q) { (void)q; return 0; }
unsigned int sceGxmProgramParameterGetArraySize(const SceGxmProgramParameter *q) { (void)q; return 0; }
unsigned int sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *q) { (void)q; return 0; }
unsigned int sceGxmProgramParameterGetContainerIndex(const SceGxmProgramParameter *q) { (void)q; return 0; }
const char *sceGxmProgramParameterGetName(const SceGxmProgramParameter *q) { (void)q; return ""; }

// ------------------------------------------------------------------ AvPlayer: a fake decoder with the real one's memory use
// Init takes decoder memory (allocate); AddSource takes the output frames (allocateTexture: 2 or 4, 960x544 or 640x272 YUV420);
// every GetVideoData hands out the next frame and "decodes" into the one after it (writes its first and last bytes); the
// clip lasts ~1.2 s per MB of file (looping clips loop); Stop / Close give every frame and block back.
typedef struct { int used, active, loop, nfr, cur, frames, total, w, h; SceAvPlayerInitData d; void *dec, *abuf; void *fr[8]; uint32_t frsize; pthread_mutex_t m; } HgAv;
static HgAv hg_av[4];
SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData *d) {
	for (int i = 0; i < 4; i++) if (!hg_av[i].used) { HgAv *A = &hg_av[i]; memset(A, 0, sizeof(*A)); pthread_mutex_init(&A->m, NULL); A->used = 1; A->d = *d;
		A->dec = d->memoryReplacement.allocate ? d->memoryReplacement.allocate(d->memoryReplacement.objectPointer, 64, 256 * 1024) : NULL;
		A->abuf = d->memoryReplacement.allocate ? d->memoryReplacement.allocate(d->memoryReplacement.objectPointer, 64, 4096) : NULL;
		if (A->dec) memset(A->dec, 0, 256 * 1024); if (A->abuf) memset(A->abuf, 0, 4096);
		return 0x100 + i; }
	return 0;
}
static HgAv *hg_avh(SceAvPlayerHandle h) { int i = h - 0x100; return i >= 0 && i < 4 && hg_av[i].used ? &hg_av[i] : NULL; }
int sceAvPlayerAddSource(SceAvPlayerHandle h, const char *file) {
	HgAv *A = hg_avh(h); if (!A) return -1; char p[1024]; struct stat st; if (stat(hg_map(file, 0, p, sizeof(p)), &st)) return (int)0x806A0001;
	A->w = st.st_size < 3000000 ? 640 : 960; A->h = A->w == 640 ? 272 : 544; A->frsize = (uint32_t)A->w * A->h * 3 / 2;
	A->nfr = A->d.numOutputVideoFrameBuffers > 0 && A->d.numOutputVideoFrameBuffers <= 8 ? A->d.numOutputVideoFrameBuffers : 2;
	for (int k = 0; k < A->nfr; k++) { A->fr[k] = A->d.memoryReplacement.allocateTexture ? A->d.memoryReplacement.allocateTexture(A->d.memoryReplacement.objectPointer, 0x40000, A->frsize) : NULL;
		if (!A->fr[k]) { for (int j = 0; j < k; j++) A->d.memoryReplacement.deallocateTexture(A->d.memoryReplacement.objectPointer, A->fr[j]); A->nfr = 0; return (int)0x806A0002; }
		memset(A->fr[k], 16, A->frsize); }
	A->total = (int)(st.st_size / (1 << 20) * 36 + 30); A->active = 1; A->frames = 0; A->cur = 0;
	if (A->d.eventReplacement.eventCallback) A->d.eventReplacement.eventCallback(A->d.eventReplacement.objectPointer, 1, 0, NULL);
	return 0;
}
static void hg_av_release(HgAv *A) {
	for (int k = 0; k < A->nfr; k++) if (A->fr[k]) { A->d.memoryReplacement.deallocateTexture(A->d.memoryReplacement.objectPointer, A->fr[k]); A->fr[k] = NULL; }
	A->nfr = 0; A->active = 0;
}
int sceAvPlayerStop(SceAvPlayerHandle h) { HgAv *A = hg_avh(h); if (!A) return -1; pthread_mutex_lock(&A->m); hg_av_release(A); pthread_mutex_unlock(&A->m); return 0; }
int sceAvPlayerClose(SceAvPlayerHandle h) {
	HgAv *A = hg_avh(h); if (!A) return -1; pthread_mutex_lock(&A->m); hg_av_release(A);
	if (A->dec) A->d.memoryReplacement.deallocate(A->d.memoryReplacement.objectPointer, A->dec); if (A->abuf) A->d.memoryReplacement.deallocate(A->d.memoryReplacement.objectPointer, A->abuf);
	A->dec = A->abuf = NULL; pthread_mutex_unlock(&A->m); pthread_mutex_destroy(&A->m); A->used = 0; return 0;
}
int sceAvPlayerSetLooping(SceAvPlayerHandle h, SceBool l) { HgAv *A = hg_avh(h); if (!A) return -1; A->loop = l; return 0; }
SceBool sceAvPlayerIsActive(SceAvPlayerHandle h) { HgAv *A = hg_avh(h); if (!A) return 0; pthread_mutex_lock(&A->m); int a = A->active; pthread_mutex_unlock(&A->m); return a; }
SceBool sceAvPlayerGetVideoData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *f) {
	HgAv *A = hg_avh(h); if (!A) return 0; pthread_mutex_lock(&A->m);
	if (!A->active || !A->nfr) { pthread_mutex_unlock(&A->m); return 0; }
	memset(f, 0, sizeof(*f)); f->pData = A->fr[A->cur]; f->details.video.width = A->w; f->details.video.height = A->h; f->timeStamp = (uint64_t)A->frames * 33;
	A->cur = (A->cur + 1) % A->nfr; { unsigned char *n = A->fr[A->cur]; n[0] = (unsigned char)A->frames; n[A->frsize - 1] = (unsigned char)A->frames; }   // the decoder writes the next one
	if (++A->frames >= A->total && !A->loop) A->active = 0;
	pthread_mutex_unlock(&A->m); return 1;
}
SceBool sceAvPlayerGetAudioData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *f) {
	HgAv *A = hg_avh(h); if (!A) return 0; pthread_mutex_lock(&A->m);
	if (!A->active || !A->abuf) { pthread_mutex_unlock(&A->m); return 0; }
	memset(f, 0, sizeof(*f)); f->pData = A->abuf; f->details.audio.channelCount = 2; f->details.audio.sampleRate = 48000; f->details.audio.size = 4096;
	pthread_mutex_unlock(&A->m); return 1;
}
