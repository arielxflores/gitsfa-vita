// Host shim of the VitaSDK APIs the game uses (perf, b37: tools/hostgame). The whole game (engine/src/main.c) builds on
// Linux against this header, vitaGL.h and vitashark.h from this folder; hostshim.c implements them on POSIX:
//   threads / semaphores -> pthreads, sceIo* / fopen on "ux0:data/gitsfa/..." -> the host data folders (HG_OUT, HG_MAPS,
//   HG_DATA), the clock -> a virtual clock (1/60 s per vglSwapBuffers), audio out -> a sink that reads every buffer and
//   blocks for its duration, AvPlayer -> a fake decoder that uses the game's memory replacement callbacks.
// Not a device model: it exists so AddressSanitizer / UBSan / ThreadSanitizer see the real game code run.
#pragma once
#ifndef HOSTGAME_SHIM_VITASDK
#define HOSTGAME_SHIM_VITASDK
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef int SceUID; typedef unsigned int SceSize; typedef unsigned int SceUInt; typedef unsigned int SceUInt32; typedef int SceInt32;
typedef uint64_t SceUInt64; typedef int64_t SceInt64; typedef int64_t SceOff; typedef unsigned short SceUShort16; typedef unsigned char SceUChar8;
typedef int SceBool; typedef unsigned int SceMode; typedef char SceChar8; typedef int SceInt; typedef unsigned short SceUInt16; typedef unsigned char SceUInt8;
typedef int (*SceKernelThreadEntry)(SceSize args, void *argp);

// ---- kernel
#define SCE_KERNEL_CPU_MASK_USER_0 0x00010000
#define SCE_KERNEL_CPU_MASK_USER_1 0x00020000
#define SCE_KERNEL_CPU_MASK_USER_2 0x00040000
#define SCE_KERNEL_CPU_MASK_USER_ALL 0x00070000
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW 0x0c20d060
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE 0x0c208060
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW 0x09408060
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW 0x0c80d060
#define SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW 0x0c80d060   // (tut_video.h b42; the shim does not tell the types apart)
#define SCE_KERNEL_ALLOC_MEMBLOCK_ATTR_HAS_ALIGNMENT 0x00000004U
typedef SceUInt SceKernelMemBlockType;
typedef struct SceKernelAllocMemBlockOpt { SceSize size; SceUInt32 attr; SceSize alignment; SceUInt32 uidBaseBlock; const char *strBaseBlockName; int flags; int reserved[10]; } SceKernelAllocMemBlockOpt;
#define SCE_KERNEL_ERROR_WAIT_TIMEOUT 0x80028005
typedef struct { SceSize size; int size_user; int size_cdram; int size_phycont; } SceKernelFreeMemorySizeInfo;
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int prio, SceSize stack, SceUInt attr, int cpu_mask, const void *opt);
int sceKernelStartThread(SceUID thid, SceSize arglen, void *argp);
int sceKernelWaitThreadEnd(SceUID thid, int *stat, SceUInt *timeout);
int sceKernelDeleteThread(SceUID thid);
int sceKernelExitDeleteThread(int status);
int sceKernelDelayThread(SceUInt usec);
SceUID sceKernelCreateSema(const char *name, SceUInt attr, int init, int max, void *opt);
int sceKernelWaitSema(SceUID semaid, int need, SceUInt *timeout);
int sceKernelSignalSema(SceUID semaid, int signal);
int sceKernelDeleteSema(SceUID semaid);
SceUInt64 sceKernelGetProcessTimeWide(void);
SceUInt64 sceKernelGetSystemTimeWide(void);   // real time (ldprof.h; the process time above is the game's virtual clock)
int sceKernelGetThreadId(void);
unsigned sceDisplayGetVcount(void);   // (dstep.h) VBlanks of the virtual clock: one per 16667 us
SceUInt32 sceKernelGetProcessTimeLow(void);
int sceKernelExitProcess(int res);
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *info);
SceUID sceKernelAllocMemBlock(const char *name, SceUInt type, SceSize size, void *opt);
int sceKernelGetMemBlockBase(SceUID uid, void **base);
int sceKernelFreeMemBlock(SceUID uid);

// ---- io
#define SCE_O_RDONLY 0x0001
#define SCE_O_WRONLY 0x0002
#define SCE_O_RDWR   0x0003
#define SCE_O_APPEND 0x0100
#define SCE_O_CREAT  0x0200
#define SCE_O_TRUNC  0x0400
#define SCE_O_EXCL   0x0800
#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2
#define SCE_S_IFMT  0xF000
#define SCE_S_IFDIR 0x1000
#define SCE_S_IFREG 0x2000
#define SCE_S_ISDIR(m) (((m) & SCE_S_IFMT) == SCE_S_IFDIR)
#define SCE_S_ISREG(m) (((m) & SCE_S_IFMT) == SCE_S_IFREG)
typedef struct { unsigned short year, month, day, hour, minute, second; unsigned int microsecond; } SceDateTime;
typedef struct { SceMode st_mode; unsigned int st_attr; SceOff st_size; SceDateTime sce_ctime, sce_atime, sce_mtime; /* renamed: glibc defines st_ctime as a macro */ unsigned int st_private[6]; } SceIoStat;
typedef struct { SceIoStat d_stat; char d_name[256]; void *d_private; int dummy; } SceIoDirent;
SceUID sceIoOpen(const char *file, int flags, SceMode mode);
int sceIoClose(SceUID fd);
int sceIoRead(SceUID fd, void *data, SceSize size);
int sceIoWrite(SceUID fd, const void *data, SceSize size);
SceOff sceIoLseek(SceUID fd, SceOff offset, int whence);
int sceIoMkdir(const char *dir, SceMode mode);
int sceIoRmdir(const char *path);
int sceIoRemove(const char *file);
int sceIoGetstat(const char *file, SceIoStat *stat);
typedef struct SceIoDevInfo { SceOff max_size, free_size; SceSize cluster_size; void *unk; } SceIoDevInfo;   // (b60: alog.h's card probe)
int sceIoDevctl(const char *dev, unsigned int cmd, void *indata, int inlen, void *outdata, int outlen);
SceUID sceIoDopen(const char *dirname);
int sceIoDread(SceUID fd, SceIoDirent *dir);
int sceIoDclose(SceUID fd);

// ---- input
#define SCE_CTRL_SELECT   0x000001
#define SCE_CTRL_L3       0x000002
#define SCE_CTRL_R3       0x000004
#define SCE_CTRL_START    0x000008
#define SCE_CTRL_UP       0x000010
#define SCE_CTRL_RIGHT    0x000020
#define SCE_CTRL_DOWN     0x000040
#define SCE_CTRL_LEFT     0x000080
#define SCE_CTRL_LTRIGGER 0x000100
#define SCE_CTRL_RTRIGGER 0x000200
#define SCE_CTRL_L1       0x000400
#define SCE_CTRL_R1       0x000800
#define SCE_CTRL_TRIANGLE 0x001000
#define SCE_CTRL_CIRCLE   0x002000
#define SCE_CTRL_CROSS    0x004000
#define SCE_CTRL_SQUARE   0x008000
#define SCE_CTRL_MODE_DIGITAL 0
#define SCE_CTRL_MODE_ANALOG 1
#define SCE_CTRL_MODE_ANALOG_WIDE 2
typedef struct { uint64_t timeStamp; unsigned int buttons; unsigned char lx, ly, rx, ry; unsigned char up, right, down, left, lt, rt, l1, r1, triangle, circle, cross, square; uint8_t reserved[4]; } SceCtrlData;
int sceCtrlSetSamplingMode(int mode);
int sceCtrlPeekBufferPositive(int port, SceCtrlData *pad, int count);
int sceCtrlReadBufferPositive(int port, SceCtrlData *pad, int count);
#define SCE_TOUCH_PORT_FRONT 0
#define SCE_TOUCH_PORT_BACK 1
#define SCE_TOUCH_SAMPLING_STATE_STOP 0
#define SCE_TOUCH_SAMPLING_STATE_START 1
#define SCE_TOUCH_MAX_REPORT 8
typedef struct { uint8_t id; uint8_t force; uint16_t x; uint16_t y; int8_t reserved[8]; uint16_t info; } SceTouchReport;
typedef struct { uint64_t timeStamp; uint32_t status; uint32_t reportNum; SceTouchReport report[SCE_TOUCH_MAX_REPORT]; } SceTouchData;
int sceTouchSetSamplingState(int port, int state);
int sceTouchPeek(int port, SceTouchData *data, int nbufs);

// ---- power, modules
int scePowerSetArmClockFrequency(int freq);
int scePowerSetBusClockFrequency(int freq);
int scePowerSetGpuClockFrequency(int freq);
int scePowerSetGpuXbarClockFrequency(int freq);
#define SCE_SYSMODULE_AVPLAYER 0x001F
int sceSysmoduleLoadModule(int id);

// ---- audio out
#define SCE_AUDIO_OUT_PORT_TYPE_MAIN 0
#define SCE_AUDIO_OUT_PORT_TYPE_BGM 1
#define SCE_AUDIO_OUT_PORT_TYPE_VOICE 2
#define SCE_AUDIO_OUT_MODE_MONO 0
#define SCE_AUDIO_OUT_MODE_STEREO 1
#define SCE_AUDIO_OUT_MAX_VOL 32768
#define SCE_AUDIO_VOLUME_FLAG_L_CH 0x1
#define SCE_AUDIO_VOLUME_FLAG_R_CH 0x2
int sceAudioOutOpenPort(int type, int len, int freq, int mode);
int sceAudioOutReleasePort(int port);
int sceAudioOutOutput(int port, const void *buf);
int sceAudioOutSetVolume(int port, int flag, int *vol);
int sceAudioOutSetConfig(int port, int len, int freq, int mode);

// ---- gxm (textures, programs, notifications)
typedef unsigned int SceGxmTextureFormat;
#define SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR 0x0C001000u
#define SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB 0x0C000000u
#define SCE_GXM_TEXTURE_FORMAT_YVU420P2_CSC1 0x91000000u
#define SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR 0x85001000u
#define SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR 0x87001000u
typedef enum { SCE_GXM_TEXTURE_FILTER_POINT = 0, SCE_GXM_TEXTURE_FILTER_LINEAR = 1 } SceGxmTextureFilter;
typedef enum { SCE_GXM_TEXTURE_ADDR_REPEAT = 0, SCE_GXM_TEXTURE_ADDR_CLAMP = 1 } SceGxmTextureAddrMode;
typedef enum { SCE_GXM_MULTISAMPLE_NONE = 0, SCE_GXM_MULTISAMPLE_2X = 1, SCE_GXM_MULTISAMPLE_4X = 2 } SceGxmMultisampleMode;
typedef struct SceGxmTexture { void *data; unsigned int w, h, fmt, mips, minf, magf, ua, va, kind; } SceGxmTexture;   // (host layout; the device's is 16 opaque bytes)
typedef struct { volatile unsigned int *address; unsigned int value; } SceGxmNotification;
typedef struct SceGxmProgram SceGxmProgram;
typedef struct SceGxmProgramParameter SceGxmProgramParameter;
typedef enum { SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE = 0, SCE_GXM_PARAMETER_CATEGORY_UNIFORM = 1, SCE_GXM_PARAMETER_CATEGORY_SAMPLER = 2 } SceGxmParameterCategory;
int sceGxmTextureInitLinear(SceGxmTexture *t, const void *data, SceGxmTextureFormat fmt, unsigned int w, unsigned int h, unsigned int mips);
int sceGxmTextureSetMinFilter(SceGxmTexture *t, SceGxmTextureFilter f);
int sceGxmTextureSetMagFilter(SceGxmTexture *t, SceGxmTextureFilter f);
int sceGxmTextureSetUAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m);
int sceGxmTextureSetVAddrMode(SceGxmTexture *t, SceGxmTextureAddrMode m);
void *sceGxmTextureGetData(const SceGxmTexture *t);
unsigned int sceGxmTextureGetWidth(const SceGxmTexture *t);
unsigned int sceGxmTextureGetHeight(const SceGxmTexture *t);
SceGxmTextureFormat sceGxmTextureGetFormat(const SceGxmTexture *t);
void sceGxmTransferFinish(void);
int sceGxmEndScene(void *ctx, const SceGxmNotification *v, const SceGxmNotification *f);
unsigned int sceGxmProgramGetSize(const SceGxmProgram *p);
unsigned int sceGxmProgramGetParameterCount(const SceGxmProgram *p);
unsigned int sceGxmProgramGetDefaultUniformBufferSize(const SceGxmProgram *p);
const SceGxmProgramParameter *sceGxmProgramGetParameter(const SceGxmProgram *p, unsigned int i);
SceGxmParameterCategory sceGxmProgramParameterGetCategory(const SceGxmProgramParameter *q);
unsigned int sceGxmProgramParameterGetType(const SceGxmProgramParameter *q);
unsigned int sceGxmProgramParameterGetComponentCount(const SceGxmProgramParameter *q);
unsigned int sceGxmProgramParameterGetArraySize(const SceGxmProgramParameter *q);
unsigned int sceGxmProgramParameterGetResourceIndex(const SceGxmProgramParameter *q);
unsigned int sceGxmProgramParameterGetContainerIndex(const SceGxmProgramParameter *q);
const char *sceGxmProgramParameterGetName(const SceGxmProgramParameter *q);
extern SceGxmNotification query_fence;   // vitaGL globals the game reads (frametime.h, vision.h)
extern void *gxm_color_surfaces_addr[];
extern unsigned int gxm_front_buffer_index;
extern int DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE;
extern uint8_t gxm_display_buffer_count;

// ---- avplayer
typedef int SceAvPlayerHandle;
typedef void *(*SceAvPlayerAlloc)(void *arg, uint32_t alignment, uint32_t size);
typedef void (*SceAvPlayerFree)(void *arg, void *ptr);
typedef void *(*SceAvPlayerAllocFrame)(void *arg, uint32_t alignment, uint32_t size);
typedef void (*SceAvPlayerFreeFrame)(void *arg, void *ptr);
typedef void (*SceAvPlayerEventCallback)(void *p, int32_t argEventId, int32_t argSourceId, void *argEventData);
typedef int (*SceAvPlayerOpenFile)(void *p, const char *filename);
typedef int (*SceAvPlayerCloseFile)(void *p);
typedef int (*SceAvPlayerReadOffsetFile)(void *p, uint8_t *buffer, uint64_t position, uint32_t length);
typedef uint64_t (*SceAvPlayerSizeFile)(void *p);
typedef struct { void *objectPointer; SceAvPlayerAlloc allocate; SceAvPlayerFree deallocate; SceAvPlayerAllocFrame allocateTexture; SceAvPlayerFreeFrame deallocateTexture; } SceAvPlayerMemReplacement;
typedef struct { void *objectPointer; SceAvPlayerOpenFile open; SceAvPlayerCloseFile close; SceAvPlayerReadOffsetFile readOffset; SceAvPlayerSizeFile size; } SceAvPlayerFileReplacement;
typedef struct { void *objectPointer; SceAvPlayerEventCallback eventCallback; } SceAvPlayerEventReplacement;
typedef struct { SceAvPlayerMemReplacement memoryReplacement; SceAvPlayerFileReplacement fileReplacement; SceAvPlayerEventReplacement eventReplacement;
	int32_t debugLevel; uint32_t basePriority; int32_t numOutputVideoFrameBuffers; SceBool autoStart; uint8_t reserved[3]; const char *defaultLanguage; } SceAvPlayerInitData;
typedef struct { uint16_t channelCount; uint8_t reserved[2]; uint32_t sampleRate; uint32_t size; uint8_t languageCode[4]; } SceAvPlayerAudio;
typedef struct { uint32_t width; uint32_t height; float aspectRatio; uint8_t languageCode[4]; } SceAvPlayerVideo;
typedef union { SceAvPlayerVideo video; SceAvPlayerAudio audio; } SceAvPlayerStreamDetails;
typedef struct { uint8_t *pData; uint8_t reserved[4]; uint64_t timeStamp; SceAvPlayerStreamDetails details; } SceAvPlayerFrameInfo;
SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData *data);
int sceAvPlayerAddSource(SceAvPlayerHandle h, const char *filename);
int sceAvPlayerClose(SceAvPlayerHandle h);
int sceAvPlayerStop(SceAvPlayerHandle h);
int sceAvPlayerSetLooping(SceAvPlayerHandle h, SceBool loop);
SceBool sceAvPlayerIsActive(SceAvPlayerHandle h);
SceBool sceAvPlayerGetVideoData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *info);
SceBool sceAvPlayerGetAudioData(SceAvPlayerHandle h, SceAvPlayerFrameInfo *info);
typedef struct SceShaccCg SceShaccCg;

void hg_snapshot(const char *label);   // host leak tracker (tools/hostgame, build.sh leaks): live heap blocks / GL names by stack
// ---- the game's stdio files on the memory card ("ux0:data/gitsfa/...") come from the host folders
FILE *hg_fopen(const char *path, const char *mode);
#define fopen(p, m) hg_fopen((p), (m))

// b41 (tut_vmem.h / tut_video.h): GPU mapping of movie frame memory (host: nothing to map)
#define SCE_GXM_MEMORY_ATTRIB_READ 1
#define SCE_GXM_MEMORY_ATTRIB_WRITE 2
#define SCE_GXM_MEMORY_ATTRIB_RW 3
static inline int sceGxmMapMemory(void *base, unsigned size, int attr) { (void)base; (void)size; (void)attr; return 0; }
static inline int sceGxmUnmapMemory(void *base) { (void)base; return 0; }

// net (stage 1): SceNet / SceNetCtl / ScePspnetAdhoc / the NetCheck dialog (hostnet.c)
#include "psp2/net_shim.h"

#ifdef __cplusplus
}
#endif
#endif
