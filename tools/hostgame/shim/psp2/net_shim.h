// net (stage 1): the network APIs the game's net_tr.h uses, for the host build (tools/hostgame/hostnet.c implements them):
// SceNet / SceNetCtl on POSIX UDP sockets, ScePspnetAdhoc PDP emulated over UDP on 127.0.0.1 (every instance listens on
// 47800 + k; a PDP broadcast goes to all eight), the NetCheck dialog finishes at once. Layouts as VitaSDK's headers.
#pragma once
#define SCE_SYSMODULE_NET 0x0001
#define SCE_SYSMODULE_PSPNET_ADHOC 0x0043
#define SCE_NET_ERROR_ENOTINIT 0x804101C8
#define SCE_NET_ERROR_EAGAIN 0x80410123
#define SCE_NET_ERROR_EWOULDBLOCK 0x80410123
#define SCE_NET_AF_INET 2
#define SCE_NET_SOCK_DGRAM 2
#define SCE_NET_IPPROTO_UDP 17
#define SCE_NET_SOL_SOCKET 0xFFFF
#define SCE_NET_SO_REUSEADDR 0x4
#define SCE_NET_SO_BROADCAST 0x20
#define SCE_NET_SO_RCVBUF 0x1002
#define SCE_NET_SO_NBIO 0x1100
#define SCE_NET_MSG_DONTWAIT 0x80
#define SCE_NETCTL_INFO_GET_IP_ADDRESS 15
#define SCE_NETCTL_INFO_GET_NETMASK 16
// b108 (internet play, net_nat.h): TCP, time-outs, the resolver, the gateway
#define SCE_NET_SOCK_STREAM 1
#define SCE_NET_SO_SNDTIMEO 0x1005
#define SCE_NET_SO_RCVTIMEO 0x1006
#define SCE_NETCTL_INFO_GET_DEFAULT_ROUTE 17
#define SCE_NETCTL_STATE_CONNECTED 3
typedef struct SceNetInitParam { void *memory; int size; int flags; } SceNetInitParam;
typedef struct SceNetInAddr { unsigned int s_addr; } SceNetInAddr;
typedef struct SceNetSockaddr { unsigned char sa_len; unsigned char sa_family; char sa_data[14]; } SceNetSockaddr;
typedef struct SceNetSockaddrIn { unsigned char sin_len; unsigned char sin_family; unsigned short sin_port; SceNetInAddr sin_addr; unsigned short sin_vport; char sin_zero[6]; } SceNetSockaddrIn;
typedef struct SceNetEtherAddr { unsigned char data[6]; } SceNetEtherAddr;
typedef union SceNetCtlInfo { char cnf_name[65]; char ip_address[16]; char netmask[16]; char default_route[16]; unsigned char pad[256]; } SceNetCtlInfo;
int sceNetShowNetstat(void);
int sceNetInit(SceNetInitParam *p);
int sceNetCtlInit(void);
int sceNetCtlInetGetState(int *state);
int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info);
int sceNetInetPton(int af, const char *src, void *dst);
int sceNetSocket(const char *name, int domain, int type, int protocol);
int sceNetSetsockopt(int s, int level, int optname, const void *optval, unsigned int optlen);
int sceNetBind(int s, const SceNetSockaddr *addr, unsigned int addrlen);
int sceNetGetsockname(int s, SceNetSockaddr *name, unsigned int *namelen);
int sceNetSendto(int s, const void *msg, unsigned int len, int flags, const SceNetSockaddr *to, unsigned int tolen);
int sceNetRecvfrom(int s, void *buf, unsigned int len, int flags, SceNetSockaddr *from, unsigned int *fromlen);
int sceNetSocketClose(int s);
unsigned short sceNetHtons(unsigned short n);
unsigned short sceNetNtohs(unsigned short n);
int sceNetConnect(int s, const SceNetSockaddr *name, unsigned int namelen);
int sceNetSend(int s, const void *msg, unsigned int len, int flags);
int sceNetRecv(int s, void *buf, unsigned int len, int flags);
int sceNetResolverCreate(const char *name, void *param, int flags);
int sceNetResolverStartNtoa(int rid, const char *hostname, SceNetInAddr *addr, int timeout, int retry, int flags);
int sceNetResolverDestroy(int rid);
// ScePspnetAdhoc / adhocctl
#define SCE_ERROR_NET_ADHOC_WOULD_BLOCK 0x80410709
#define SCE_ERROR_NET_ADHOC_TIMEOUT 0x80410715
#define SCE_ERROR_NET_ADHOC_ALREADY_INITIALIZED 0x80410713
#define SCE_ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED 0x80410B07
#define SCE_NET_ADHOC_F_NONBLOCK 1
#define SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED 1
#define SCE_NET_ADHOCCTL_ADHOCID_LEN 9
typedef struct SceNetAdhocctlAdhocId { int type; char data[9]; unsigned char padding[3]; } SceNetAdhocctlAdhocId;
typedef struct SceNetAdhocctlGroupName { char data[8]; } SceNetAdhocctlGroupName;
int sceNetAdhocInit(void);
int sceNetAdhocTerm(void);
int sceNetAdhocctlInit(const SceNetAdhocctlAdhocId *id);
int sceNetAdhocctlTerm(void);
int sceNetAdhocctlGetEtherAddr(SceNetEtherAddr *addr);
int sceNetAdhocPdpCreate(const SceNetEtherAddr *saddr, unsigned short sport, unsigned int bufsize, int flag);
int sceNetAdhocPdpSend(int id, const SceNetEtherAddr *daddr, unsigned short dport, const void *data, int len, unsigned int timeout, int flag);
int sceNetAdhocPdpRecv(int id, SceNetEtherAddr *saddr, unsigned short *sport, void *buf, int *len, unsigned int timeout, int flag);
int sceNetAdhocPdpDelete(int id, int flag);
// the NetCheck dialog (common dialog)
#define SCE_COMMON_DIALOG_STATUS_NONE 0
#define SCE_COMMON_DIALOG_STATUS_RUNNING 1
#define SCE_COMMON_DIALOG_STATUS_FINISHED 2
#define SCE_COMMON_DIALOG_RESULT_OK 0
#define SCE_COMMON_DIALOG_RESULT_USER_CANCELED 1
#define SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN 5
typedef struct SceNpCommunicationId { char data[9]; char term; unsigned char num; char dummy; } SceNpCommunicationId;
typedef struct SceNetCheckDialogParam { unsigned sdkVersion; int mode; SceNpCommunicationId npCommunicationId; void *ps3ConnectParam; SceNetAdhocctlGroupName *groupName; unsigned timeoutUs; } SceNetCheckDialogParam;
typedef struct SceNetCheckDialogResult { int result; int psnModeSucceeded; unsigned char reserved[124]; } SceNetCheckDialogResult;
static inline void sceNetCheckDialogParamInit(SceNetCheckDialogParam *p) { memset(p, 0, sizeof(*p)); }
int sceNetCheckDialogInit(SceNetCheckDialogParam *p);
int sceNetCheckDialogGetStatus(void);
int sceNetCheckDialogGetResult(SceNetCheckDialogResult *r);
int sceNetCheckDialogTerm(void);
