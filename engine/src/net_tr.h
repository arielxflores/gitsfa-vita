// net (stage 1): the transport under the local multiplayer (NET.md). Two backends behind one table (NetTr):
//   tr_pdp - PS Vita ad-hoc: the PSP-compatible ScePspnetAdhoc library (PDP datagrams between MAC addresses). The Vita joins
//            the ad-hoc group through the system NetCheck dialog (SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN): the Vita's
//            adhocctl library exports no connect call. The call sequence is the one vitaQuake ships (NET.md section 1).
//   tr_udp - UDP sockets (sceNet BSD API): a Vita on an infrastructure Wi-Fi LAN, Vita3K, the host build; b108: TR_INET, the
//            same sockets across the internet (net_nat.h: the host's port and room code).
// Addresses are 8 opaque bytes (UDP: IPv4 + port in network order; PDP: MAC + port). Every call is non-blocking; the I/O
// thread (net_lobby.h) owns the socket. Nothing here runs offline: net_lobby.h brings a backend up when the AD-HOC window opens.
#pragma once
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define NET_UDP_PORT 47750            // the host's game port (a second host on the same machine takes 47751..47753)
#define NET_UDP_NPORT 4
#define NET_PDP_PORT 0x4747           // 18247: the same in either byte order (NET.md: the PDP port's byte order is not documented)
#define NET_ADHOC_ID "GITS00001"      // ad-hoc id (SCE_NET_ADHOCCTL_ADHOCID_LEN 9 = the title id)
typedef struct { unsigned char b[8]; } NetAddr;
static int net_addr_eq(const NetAddr *a, const NetAddr *b) { return !memcmp(a->b, b->b, 8); }
typedef struct NetTr {
	const char *name;                                    // shown in the menus: "AD-HOC" / "LAN"
	int (*up)(char *msg, int n);                         // bring the network up: 0 ready, 1 pending (a system dialog), < 0 failed (msg says why)
	int (*poll_up)(char *msg, int n);                    // while up() is pending, once a frame: 0 ready, 1 pending, < 0 failed
	int (*open)(int host);                               // the socket (host: the game port); < 0 failed
	void (*close)(void);
	void (*down)(void);                                  // leave the network (ad-hoc: the group; the Vita goes back to its Wi-Fi)
	int (*send)(const NetAddr *to, const void *p, int n);
	int (*bcast)(const void *p, int n);                  // discovery probes: every host that can hear us
	int (*recv)(NetAddr *from, void *p, int cap);        // > 0 bytes, 0 nothing waiting, < 0 error
	void (*str)(const NetAddr *a, char *s, int n);
	int (*self)(NetAddr *a);                             // our own address (0 ok)
} NetTr;
static volatile int net_dialog;                          // 1 while a system dialog is up: the frame is swapped with vglSwapBuffers(GL_TRUE)

// ------------------------------------------------------------------ the SceNet library (both backends)
static void *net_pool; static int net_lib_up;
static int net_lib_init(char *msg, int n) {
	if (net_lib_up) return 0;
	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	int r = sceNetShowNetstat();
	if (r == (int)SCE_NET_ERROR_ENOTINIT) {
		if (!net_pool) net_pool = malloc(512 * 1024);
		SceNetInitParam ip; ip.memory = net_pool; ip.size = 512 * 1024; ip.flags = 0;
		r = sceNetInit(&ip); if (r < 0) { snprintf(msg, n, "Network start failed (0x%08X)", r); return -1; }
	}
	r = sceNetCtlInit(); if (r < 0 && r != (int)0x80412102 /* already initialised */) { snprintf(msg, n, "Network control failed (0x%08X)", r); return -1; }
	net_lib_up = 1; return 0;
}

// ------------------------------------------------------------------ UDP (LAN / host build / Vita3K)
#include "net_nat.h"   // b108: the internet room's port and public address (UPnP / NAT-PMP / STUN), the room code
static int udp_sock = -1, udp_port; static unsigned udp_ip, udp_mask;
static int udp_up(char *msg, int n) {
	if (net_lib_init(msg, n) < 0) return -1;
#ifndef HG_HOST
	int st = 0; sceNetCtlInetGetState(&st);
	if (st != SCE_NETCTL_STATE_CONNECTED) { snprintf(msg, n, "Not connected to a Wi-Fi network"); return -1; }
#endif
	SceNetCtlInfo info; memset(&info, 0, sizeof(info)); udp_ip = udp_mask = 0;
	if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info) >= 0) sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &udp_ip);
	memset(&info, 0, sizeof(info)); if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_NETMASK, &info) >= 0) sceNetInetPton(SCE_NET_AF_INET, info.netmask, &udp_mask);
	return 0;
}
static int udp_poll_up(char *msg, int n) { (void)msg; (void)n; return 0; }
static void udp_close(void) { if (udp_sock >= 0) sceNetSocketClose(udp_sock); udp_sock = -1; }
static int udp_open(int host) {
	udp_close();
	int s = sceNetSocket("gitsfa_udp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP); if (s < 0) return s;
	int one = 1; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_BROADCAST, &one, sizeof(one)); sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &one, sizeof(one));
	int rb = 128 * 1024; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVBUF, &rb, sizeof(rb));
	int ok = -1;
	for (int k = 0; k < (host ? NET_UDP_NPORT : 1); k++) {
		SceNetSockaddrIn a; memset(&a, 0, sizeof(a)); a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET; a.sin_port = host ? sceNetHtons(NET_UDP_PORT + k) : 0; a.sin_addr.s_addr = 0;
		if (sceNetBind(s, (SceNetSockaddr *)&a, sizeof(a)) >= 0) { ok = 0; udp_port = host ? NET_UDP_PORT + k : 0; break; }
	}
	if (ok < 0) { sceNetSocketClose(s); return -2; }
	if (!host) { SceNetSockaddrIn a; unsigned l = sizeof(a); if (sceNetGetsockname(s, (SceNetSockaddr *)&a, &l) >= 0) udp_port = sceNetNtohs(a.sin_port); }
	udp_sock = s; return 0;
}
static void udp_down(void) { udp_close(); }
static void udp_addr(NetAddr *o, unsigned ip_net, unsigned short port_host) { memset(o, 0, sizeof(*o)); memcpy(o->b, &ip_net, 4); unsigned short p = sceNetHtons(port_host); memcpy(o->b + 4, &p, 2); }
static int udp_send(const NetAddr *to, const void *p, int n) {
	if (udp_sock < 0) return -1;
	SceNetSockaddrIn a; memset(&a, 0, sizeof(a)); a.sin_len = sizeof(a); a.sin_family = SCE_NET_AF_INET; memcpy(&a.sin_addr.s_addr, to->b, 4); memcpy(&a.sin_port, to->b + 4, 2);
	int r = sceNetSendto(udp_sock, p, n, 0, (SceNetSockaddr *)&a, sizeof(a));
	return r == (int)SCE_NET_ERROR_EAGAIN ? 0 : r;
}
static int udp_bcast(const void *p, int n) {   // the limited broadcast, the subnet's broadcast and this machine (two games on one PC / emulator)
	NetAddr a; int sent = 0; unsigned bc = 0xFFFFFFFFu, lo = 0, sub = udp_ip && udp_mask ? (udp_ip | ~udp_mask) : 0;
	sceNetInetPton(SCE_NET_AF_INET, "127.0.0.1", &lo);
	for (int k = 0; k < NET_UDP_NPORT; k++) {
		udp_addr(&a, bc, NET_UDP_PORT + k); sent += udp_send(&a, p, n) > 0;
		if (sub && sub != bc) { udp_addr(&a, sub, NET_UDP_PORT + k); sent += udp_send(&a, p, n) > 0; }
		udp_addr(&a, lo, NET_UDP_PORT + k); sent += udp_send(&a, p, n) > 0;
	}
	return sent;
}
static int udp_recv(NetAddr *from, void *p, int cap) {
	if (udp_sock < 0) return -1;
	SceNetSockaddrIn a; unsigned l = sizeof(a); memset(&a, 0, sizeof(a));
	int r = sceNetRecvfrom(udp_sock, p, cap, SCE_NET_MSG_DONTWAIT, (SceNetSockaddr *)&a, &l);
	if (r == (int)SCE_NET_ERROR_EAGAIN || r == (int)SCE_NET_ERROR_EWOULDBLOCK) return 0;
	if (r < 0) return r;
	if (nat_stun_rx((const unsigned char *)p, r)) return 0;   // (b108: a STUN server's answer about our public address)
	memset(from, 0, sizeof(*from)); memcpy(from->b, &a.sin_addr.s_addr, 4); memcpy(from->b + 4, &a.sin_port, 2);
	return r;
}
static void udp_str(const NetAddr *a, char *s, int n) { unsigned short p; memcpy(&p, a->b + 4, 2); snprintf(s, n, "%u.%u.%u.%u:%u", a->b[0], a->b[1], a->b[2], a->b[3], sceNetNtohs(p)); }
static int udp_self(NetAddr *a) { udp_addr(a, udp_ip, (unsigned short)udp_port); return 0; }
static const NetTr TR_UDP = { "LAN", udp_up, udp_poll_up, udp_open, udp_close, udp_down, udp_send, udp_bcast, udp_recv, udp_str, udp_self };
// b108: INTERNET = the same sockets; the search goes to the address of the room code the player typed (and to this network,
// for a friend on the same Wi-Fi), the host's port is opened by net_nat.h
static NetAddr inet_target; static int inet_has_target;
static int inet_bcast(const void *p, int n) { int s = udp_bcast(p, n); if (inet_has_target) s += udp_send(&inet_target, p, n) > 0; return s; }
static int udp_game_send(unsigned ip_net, int port, const void *p, int n) { NetAddr a; udp_addr(&a, ip_net, (unsigned short)port); return udp_send(&a, p, n); }
static const NetTr TR_INET = { "INTERNET", udp_up, udp_poll_up, udp_open, udp_close, udp_down, udp_send, inet_bcast, udp_recv, udp_str, udp_self };

// ------------------------------------------------------------------ PDP (ad-hoc)
static int pdp_sock = -1, pdp_stage; static SceNetEtherAddr pdp_mac;
static int pdp_up(char *msg, int n) {
	if (net_lib_init(msg, n) < 0) return -1;
	int r = sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC); if (r < 0) { snprintf(msg, n, "Ad-hoc module missing (0x%08X)", r); return -1; }
	r = sceNetAdhocInit(); if (r < 0 && r != (int)SCE_ERROR_NET_ADHOC_ALREADY_INITIALIZED) { snprintf(msg, n, "Ad-hoc start failed (0x%08X)", r); return -1; }
	SceNetAdhocctlAdhocId id; memset(&id, 0, sizeof(id)); id.type = SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED; memcpy(id.data, NET_ADHOC_ID, SCE_NET_ADHOCCTL_ADHOCID_LEN);
	r = sceNetAdhocctlInit(&id); if (r < 0 && r != (int)SCE_ERROR_NET_ADHOCCTL_ALREADY_INITIALIZED) { snprintf(msg, n, "Ad-hoc control failed (0x%08X)", r); return -1; }
	// the system dialog joins (or forms) the ad-hoc group of this game
	SceNetCheckDialogParam dp; sceNetCheckDialogParamInit(&dp); static SceNetAdhocctlGroupName gn; memset(&gn, 0, sizeof(gn)); dp.groupName = &gn;
	memcpy(dp.npCommunicationId.data, NET_ADHOC_ID, 9); dp.npCommunicationId.term = 0; dp.npCommunicationId.num = 0;
	dp.mode = SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN; dp.timeoutUs = 0;
	r = sceNetCheckDialogInit(&dp); if (r < 0) { snprintf(msg, n, "Ad-hoc dialog failed (0x%08X)", r); return -1; }
	net_dialog = 1; pdp_stage = 1; return 1;
}
static int pdp_poll_up(char *msg, int n) {
	if (pdp_stage != 1) return pdp_stage == 2 ? 0 : -1;
	if (sceNetCheckDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return 1;
	SceNetCheckDialogResult res; memset(&res, 0, sizeof(res)); sceNetCheckDialogGetResult(&res); sceNetCheckDialogTerm(); net_dialog = 0;
	if (res.result != SCE_COMMON_DIALOG_RESULT_OK) { snprintf(msg, n, res.result == SCE_COMMON_DIALOG_RESULT_USER_CANCELED ? "Ad-hoc connection cancelled" : "Ad-hoc connection failed (%d)", res.result); pdp_stage = 0; return -1; }
	sceNetAdhocctlGetEtherAddr(&pdp_mac); pdp_stage = 2; return 0;
}
static void pdp_close(void) { if (pdp_sock >= 0) sceNetAdhocPdpDelete(pdp_sock, 0); pdp_sock = -1; }
static int pdp_open(int host) {
	(void)host; pdp_close();   // (every Vita binds the same port: one game per Vita)
	int s = sceNetAdhocPdpCreate(&pdp_mac, NET_PDP_PORT, 0x8000, 0); if (s < 0) s = sceNetAdhocPdpCreate(&pdp_mac, NET_PDP_PORT, 0x2000, 0);
	if (s < 0) return s; pdp_sock = s; return 0;
}
static void pdp_down(void) { pdp_close(); if (pdp_stage) { sceNetAdhocctlTerm(); sceNetAdhocTerm(); } pdp_stage = 0; }
static int pdp_send(const NetAddr *to, const void *p, int n) {
	if (pdp_sock < 0) return -1;
	int r = sceNetAdhocPdpSend(pdp_sock, (const SceNetEtherAddr *)to->b, NET_PDP_PORT, p, n, 0, SCE_NET_ADHOC_F_NONBLOCK);
	return r == (int)SCE_ERROR_NET_ADHOC_WOULD_BLOCK ? 0 : r < 0 ? r : n;
}
static int pdp_bcast(const void *p, int n) { NetAddr a; memset(&a, 0, sizeof(a)); memset(a.b, 0xFF, 6); return pdp_send(&a, p, n) > 0; }
static int pdp_recv(NetAddr *from, void *p, int cap) {
	if (pdp_sock < 0) return -1;
	SceNetEtherAddr m; SceUShort16 port = 0; int len = cap;
	int r = sceNetAdhocPdpRecv(pdp_sock, &m, &port, p, &len, 0, SCE_NET_ADHOC_F_NONBLOCK);
	if (r == (int)SCE_ERROR_NET_ADHOC_WOULD_BLOCK || r == (int)SCE_ERROR_NET_ADHOC_TIMEOUT) return 0;
	if (r < 0) return r;
	memset(from, 0, sizeof(*from)); memcpy(from->b, m.data, 6); memcpy(from->b + 6, &port, 2);
	return len;
}
static void pdp_str(const NetAddr *a, char *s, int n) { snprintf(s, n, "%02X:%02X:%02X:%02X:%02X:%02X", a->b[0], a->b[1], a->b[2], a->b[3], a->b[4], a->b[5]); }
static int pdp_self(NetAddr *a) { memset(a, 0, sizeof(*a)); memcpy(a->b, pdp_mac.data, 6); return 0; }
static const NetTr TR_PDP = { "AD-HOC", pdp_up, pdp_poll_up, pdp_open, pdp_close, pdp_down, pdp_send, pdp_bcast, pdp_recv, pdp_str, pdp_self };
