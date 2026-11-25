#pragma once
// host stubs of the Vita APIs audio.h uses (off-device mixer tests)
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
typedef int SceUID; typedef unsigned int SceSize; typedef int64_t SceOff; typedef uint64_t SceUInt64; typedef unsigned int SceUInt;
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_PORT_TYPE_BGM 1
#define SCE_AUDIO_OUT_MODE_STEREO 1
#define SCE_AUDIO_OUT_MAX_VOL 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 2
#define SCE_O_RDONLY 1
#define SCE_SEEK_SET 0
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 0
static const char *HOST_DATA = "/root/gits/build/data/";
static int sceAudioOutOpenPort(int t, int g, int f, int m) { return -1; }
static int sceAudioOutReleasePort(int p) { return 0; }
static int sceAudioOutSetVolume(int p, int f, int *v) { return 0; }
static int sceAudioOutOutput(int p, const void *b) { return 0; }
static SceUID sceIoOpen(const char *path, int fl, int m) {
	char p[512]; const char *q = strstr(path, "gitsfa/"); snprintf(p, sizeof(p), "%s%s", HOST_DATA, q ? q + 7 : path);
	int fd = open(p, O_RDONLY); return fd < 0 ? -1 : fd;
}
static int sceIoRead(SceUID fd, void *b, SceSize n) { return (int)read(fd, b, n); }
static SceOff sceIoLseek(SceUID fd, SceOff o, int w) { return lseek(fd, o, SEEK_SET); }
static int sceIoClose(SceUID fd) { return close(fd); }
static SceUInt64 sceKernelGetProcessTimeWide(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (SceUInt64)t.tv_sec * 1000000 + t.tv_nsec / 1000; }
static int sceKernelDelayThread(SceUInt us) { return 0; }
static SceUID sceKernelCreateThread(const char *n, int (*f)(SceSize, void *), int p, int s, int a, int c, void *o) { return -1; }
static int sceKernelStartThread(SceUID t, SceSize a, void *p) { return -1; }
static SceUID sceKernelAllocMemBlock(const char *n, int t, int s, void *o) { return -1; }
static int sceKernelGetMemBlockBase(SceUID b, void **p) { return -1; }
