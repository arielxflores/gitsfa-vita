// net (stage 1): the host build's network (shim/psp2/net_shim.h). SceNet sockets are POSIX UDP sockets (the same numbers in
// network order); ScePspnetAdhoc PDP is emulated on 127.0.0.1: instance k (the first free of 0..7) owns UDP port 47800 + k and
// the fake MAC 02:00:00:00:00:0k; a PDP datagram to MAC m goes to 47800 + m[5], the broadcast MAC to all eight. The NetCheck
// dialog succeeds at once. HG_NET_LOSS=percent drops that share of the datagrams we send (both kinds), HG_NET_LAG=ms holds
// them (a delay line on a thread), so tests see loss and latency on one machine.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include "shim/vitasdk.h"
static unsigned hn_rng = 0x9e3779b9u;
static int hn_loss(void) { static int l = -1; if (l < 0) { const char *e = getenv("HG_NET_LOSS"); l = e ? atoi(e) : 0; } if (!l) return 0; hn_rng = hn_rng * 1664525u + 1013904223u; return (int)((hn_rng >> 8) % 100) < l; }
// ---- the delay line (HG_NET_LAG): datagrams wait their time on a thread, then go out with sendto
typedef struct { int fd; struct sockaddr_in to; int n; unsigned char d[1500]; uint64_t at; } HnQ;
#define HN_Q 512
static HnQ hn_q[HN_Q]; static int hn_qh, hn_qt, hn_lag = -1; static pthread_mutex_t hn_qm = PTHREAD_MUTEX_INITIALIZER; static pthread_t hn_qth; static int hn_qon;
static uint64_t hn_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + t.tv_nsec / 1000; }
static void *hn_qthread(void *a) { (void)a; for (;;) { pthread_mutex_lock(&hn_qm); uint64_t now = hn_us();
	while (hn_qt != hn_qh && hn_q[hn_qt].at <= now) { HnQ *q = &hn_q[hn_qt]; sendto(q->fd, q->d, q->n, 0, (struct sockaddr *)&q->to, sizeof(q->to)); hn_qt = (hn_qt + 1) % HN_Q; }
	pthread_mutex_unlock(&hn_qm); usleep(1000); } return NULL; }
static int hn_sendto(int fd, const void *d, int n, const struct sockaddr_in *to) {
	if (hn_loss()) return n;
	if (hn_lag < 0) { const char *e = getenv("HG_NET_LAG"); hn_lag = e ? atoi(e) : 0; }
	if (hn_lag <= 0 || n > 1500) { int r = (int)sendto(fd, d, n, 0, (const struct sockaddr *)to, sizeof(*to)); return r; }
	pthread_mutex_lock(&hn_qm); if (!hn_qon) { hn_qon = 1; pthread_create(&hn_qth, NULL, hn_qthread, NULL); }
	if ((hn_qh + 1) % HN_Q != hn_qt) { HnQ *q = &hn_q[hn_qh]; q->fd = fd; q->to = *to; q->n = n; memcpy(q->d, d, n); q->at = hn_us() + (uint64_t)hn_lag * 1000; hn_qh = (hn_qh + 1) % HN_Q; }
	pthread_mutex_unlock(&hn_qm); return n;
}
// ---- SceNet / SceNetCtl
int sceNetShowNetstat(void) { static int up; if (!up) { up = 1; return (int)SCE_NET_ERROR_ENOTINIT; } return 0; }
int sceNetInit(SceNetInitParam *p) { (void)p; return 0; }
int sceNetCtlInit(void) { return 0; }
int sceNetCtlInetGetState(int *s) { *s = SCE_NETCTL_STATE_CONNECTED; return 0; }
int sceNetCtlInetGetInfo(int code, SceNetCtlInfo *info) {
	memset(info, 0, sizeof(*info));
	if (code == SCE_NETCTL_INFO_GET_IP_ADDRESS) snprintf(info->ip_address, 16, "127.0.0.1");
	else if (code == SCE_NETCTL_INFO_GET_NETMASK) snprintf(info->netmask, 16, "255.255.255.255");
	else if (code == SCE_NETCTL_INFO_GET_DEFAULT_ROUTE) snprintf(info->default_route, 16, "%s", getenv("HG_GATEWAY") ? getenv("HG_GATEWAY") : "127.0.0.1");   // (b108: the "router" of the tests)
	else return -1;
	return 0;
}
int sceNetInetPton(int af, const char *src, void *dst) { (void)af; return inet_pton(AF_INET, src, dst) == 1 ? 1 : 0; }
unsigned short sceNetHtons(unsigned short n) { return htons(n); }
unsigned short sceNetNtohs(unsigned short n) { return ntohs(n); }
int sceNetSocket(const char *name, int domain, int type, int protocol) { (void)name; (void)domain; (void)protocol; int s = socket(AF_INET, type == SCE_NET_SOCK_STREAM ? SOCK_STREAM : SOCK_DGRAM, 0); return s < 0 ? -1 : s; }   // (b108: TCP too)
int sceNetSetsockopt(int s, int level, int opt, const void *v, unsigned int len) {
	(void)level; (void)len; int i = *(const int *)v;
	if (opt == SCE_NET_SO_NBIO) { int f = fcntl(s, F_GETFL, 0); return fcntl(s, F_SETFL, i ? f | O_NONBLOCK : f & ~O_NONBLOCK) < 0 ? -1 : 0; }
	if (opt == SCE_NET_SO_BROADCAST) return setsockopt(s, SOL_SOCKET, SO_BROADCAST, &i, sizeof(i));
	if (opt == SCE_NET_SO_RCVBUF) return setsockopt(s, SOL_SOCKET, SO_RCVBUF, &i, sizeof(i));
	if (opt == SCE_NET_SO_REUSEADDR) return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));
	if (opt == SCE_NET_SO_RCVTIMEO || opt == SCE_NET_SO_SNDTIMEO) { struct timeval tv = { i / 1000000, i % 1000000 }; return setsockopt(s, SOL_SOCKET, opt == SCE_NET_SO_RCVTIMEO ? SO_RCVTIMEO : SO_SNDTIMEO, &tv, sizeof(tv)); }   // (b108: microseconds)
	return 0;
}
static void hn_in(const SceNetSockaddr *a, struct sockaddr_in *o) { const SceNetSockaddrIn *i = (const SceNetSockaddrIn *)a; memset(o, 0, sizeof(*o)); o->sin_family = AF_INET; o->sin_port = i->sin_port; o->sin_addr.s_addr = i->sin_addr.s_addr; }
static void hn_out(const struct sockaddr_in *i, SceNetSockaddr *a) { SceNetSockaddrIn *o = (SceNetSockaddrIn *)a; memset(o, 0, sizeof(*o)); o->sin_len = sizeof(*o); o->sin_family = SCE_NET_AF_INET; o->sin_port = i->sin_port; o->sin_addr.s_addr = i->sin_addr.s_addr; }
int sceNetBind(int s, const SceNetSockaddr *addr, unsigned int len) { (void)len; struct sockaddr_in a; hn_in(addr, &a); return bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 ? (int)0x80410130 : 0; }
int sceNetGetsockname(int s, SceNetSockaddr *name, unsigned int *len) { struct sockaddr_in a; socklen_t l = sizeof(a); if (getsockname(s, (struct sockaddr *)&a, &l) < 0) return -1; hn_out(&a, name); *len = sizeof(SceNetSockaddrIn); return 0; }
int sceNetSendto(int s, const void *msg, unsigned int len, int flags, const SceNetSockaddr *to, unsigned int tolen) {
	(void)flags; (void)tolen; struct sockaddr_in a; hn_in(to, &a); int r = hn_sendto(s, msg, (int)len, &a);
	if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? (int)SCE_NET_ERROR_EAGAIN : (int)(0x80410100 | (errno & 0xFF));
	return r;
}
int sceNetRecvfrom(int s, void *buf, unsigned int len, int flags, SceNetSockaddr *from, unsigned int *fromlen) {
	struct sockaddr_in a; socklen_t l = sizeof(a); int r = (int)recvfrom(s, buf, len, (flags & SCE_NET_MSG_DONTWAIT) ? MSG_DONTWAIT : 0, (struct sockaddr *)&a, &l);
	if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? (int)SCE_NET_ERROR_EAGAIN : (int)(0x80410100 | (errno & 0xFF));
	if (from) hn_out(&a, from); if (fromlen) *fromlen = sizeof(SceNetSockaddrIn); return r;
}
int sceNetSocketClose(int s) { return close(s); }
// ---- ScePspnetAdhoc over UDP on 127.0.0.1
#define HN_PDP_BASE 47800
static int hn_pdp_fd = -1, hn_pdp_k = -1;
int sceNetAdhocInit(void) { return 0; }
int sceNetAdhocTerm(void) { return 0; }
int sceNetAdhocctlInit(const SceNetAdhocctlAdhocId *id) { (void)id; return 0; }
int sceNetAdhocctlTerm(void) { return 0; }
static int hn_pdp_bind(void) {
	if (hn_pdp_fd >= 0) return 0;
	for (int k = 0; k < 8; k++) { int s = socket(AF_INET, SOCK_DGRAM, 0); struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_port = htons(HN_PDP_BASE + k); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (bind(s, (struct sockaddr *)&a, sizeof(a)) == 0) { hn_pdp_fd = s; hn_pdp_k = k; fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); return 0; } close(s); }
	return -1;
}
int sceNetAdhocctlGetEtherAddr(SceNetEtherAddr *addr) { if (hn_pdp_bind() < 0) return -1; static const unsigned char m[6] = { 2, 0, 0, 0, 0, 0 }; memcpy(addr->data, m, 6); addr->data[5] = (unsigned char)hn_pdp_k; return 0; }
int sceNetAdhocPdpCreate(const SceNetEtherAddr *saddr, unsigned short sport, unsigned int bufsize, int flag) { (void)saddr; (void)sport; (void)bufsize; (void)flag; return hn_pdp_bind() < 0 ? (int)0x8041070A : 1; }
int sceNetAdhocPdpDelete(int id, int flag) { (void)id; (void)flag; if (hn_pdp_fd >= 0) close(hn_pdp_fd); hn_pdp_fd = -1; return 0; }
int sceNetAdhocPdpSend(int id, const SceNetEtherAddr *daddr, unsigned short dport, const void *data, int len, unsigned int timeout, int flag) {
	(void)id; (void)dport; (void)timeout; (void)flag; if (hn_pdp_fd < 0) return (int)0x80410701;
	int bc = 1; for (int i = 0; i < 6; i++) if (daddr->data[i] != 0xFF) bc = 0;
	struct sockaddr_in a; memset(&a, 0, sizeof(a)); a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bc) { for (int k = 0; k < 8; k++) if (k != hn_pdp_k) { a.sin_port = htons(HN_PDP_BASE + k); hn_sendto(hn_pdp_fd, data, len, &a); } return len; }
	if (daddr->data[0] != 2 || daddr->data[5] > 7) return (int)0x80410702;
	a.sin_port = htons(HN_PDP_BASE + daddr->data[5]); int r = hn_sendto(hn_pdp_fd, data, len, &a);
	return r < 0 ? (errno == EAGAIN ? (int)SCE_ERROR_NET_ADHOC_WOULD_BLOCK : (int)0x80410717) : r;
}
int sceNetAdhocPdpRecv(int id, SceNetEtherAddr *saddr, unsigned short *sport, void *buf, int *len, unsigned int timeout, int flag) {
	(void)id; (void)timeout; (void)flag; if (hn_pdp_fd < 0) return (int)0x80410701;
	struct sockaddr_in a; socklen_t l = sizeof(a); int r = (int)recvfrom(hn_pdp_fd, buf, *len, MSG_DONTWAIT, (struct sockaddr *)&a, &l);
	if (r < 0) return errno == EAGAIN || errno == EWOULDBLOCK ? (int)SCE_ERROR_NET_ADHOC_WOULD_BLOCK : (int)0x80410717;
	int k = ntohs(a.sin_port) - HN_PDP_BASE; static const unsigned char m[6] = { 2, 0, 0, 0, 0, 0 }; memcpy(saddr->data, m, 6); saddr->data[5] = (unsigned char)(k & 7);
	*sport = 0x4747; *len = r; return 0;
}
// ---- the NetCheck dialog: done at once
static int hn_dlg;
int sceNetCheckDialogInit(SceNetCheckDialogParam *p) { (void)p; hn_dlg = 1; return 0; }
int sceNetCheckDialogGetStatus(void) { return hn_dlg ? SCE_COMMON_DIALOG_STATUS_FINISHED : SCE_COMMON_DIALOG_STATUS_NONE; }
int sceNetCheckDialogGetResult(SceNetCheckDialogResult *r) { memset(r, 0, sizeof(*r)); r->result = SCE_COMMON_DIALOG_RESULT_OK; return 0; }
int sceNetCheckDialogTerm(void) { hn_dlg = 0; return 0; }

// b108 (internet play, net_nat.h): TCP to the router's UPnP service, the resolver for the STUN servers' names
int sceNetConnect(int s, const SceNetSockaddr *name, unsigned int namelen) { (void)namelen; struct sockaddr_in a; hn_in(name, &a); return connect(s, (struct sockaddr *)&a, sizeof(a)) < 0 ? -1 : 0; }
int sceNetSend(int s, const void *msg, unsigned int len, int flags) { (void)flags; return (int)send(s, msg, len, MSG_NOSIGNAL); }
int sceNetRecv(int s, void *buf, unsigned int len, int flags) { (void)flags; return (int)recv(s, buf, len, 0); }
int sceNetResolverCreate(const char *name, void *param, int flags) { (void)name; (void)param; (void)flags; return 1; }
int sceNetResolverStartNtoa(int rid, const char *hostname, SceNetInAddr *addr, int timeout, int retry, int flags) {
	(void)rid; (void)timeout; (void)retry; (void)flags; struct addrinfo h, *r = NULL; memset(&h, 0, sizeof(h)); h.ai_family = AF_INET;
	if (getenv("HG_NO_DNS") || getaddrinfo(hostname, NULL, &h, &r) || !r) return -1;
	addr->s_addr = ((struct sockaddr_in *)r->ai_addr)->sin_addr.s_addr; freeaddrinfo(r); return 0;
}
int sceNetResolverDestroy(int rid) { (void)rid; return 0; }
