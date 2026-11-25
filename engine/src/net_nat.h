// net (b108, internet play): how a room is reached from outside the host's home network, with no server of ours.
// (The user: "could you implement multiplayer across the internet with other vitas? i don't know if we'd need to set up a
// server or people with vitas could just connect directly to each other over the internet".) Directly: the match is
// host-authoritative UDP already (net_game.h), so only the HOST must be reachable - the players who join send first, and
// their routers let the answers back in. The host's Vita, when it opens an INTERNET room, on a worker thread:
//   1. asks its router to forward the game's UDP port to it: UPnP IGD (SSDP search on the LAN and at the gateway, the
//      device description over HTTP, SOAP AddPortMapping on the WANIPConnection / WANPPPConnection service), else NAT-PMP
//      (two small datagrams to the gateway's port 5351);
//   2. learns its public address: the router's own answer (GetExternalIPAddress / NAT-PMP), and a STUN binding request sent
//      FROM THE GAME SOCKET to a public STUN server (net_stun.txt, one host:port a line; the defaults below) - that is the
//      address the internet really sees, also when no port could be opened (a "full cone" router still lets players in).
// The address goes into a ROOM CODE (nat_code: 4 address bytes + port + check character, Crockford base 32,
// XXXX-XXXX-XXX) that the host reads to the friends; they type it on the INTERNET screen (net_lobby.h) and their probes go
// to that address, the room answers, and the rest is the LAN lobby. What cannot work this way: a host whose provider
// shares one public address among customers (carrier-grade NAT, most phone hotspots) or whose router refuses both
// protocols and has no manual forward - then another player hosts. A meeting-point server would lift most of that; it is
// not needed for this.
// Test hooks: net_igd.txt = the gateway's description URL (skips the SSDP search); the host build's gateway is HG_GATEWAY.
#pragma once
#include <strings.h>
enum { NAT_IDLE, NAT_WORKING, NAT_DONE };
static struct {
	volatile int state, want_unmap;
	int port; unsigned lan_ip;                       // the game's UDP port, this machine's address (network order)
	volatile int mapped;                             // 0 no, 1 UPnP, 2 NAT-PMP
	unsigned gate;                                   // the LAN gateway (the router)
	volatile unsigned gw_ip, stun_ip; volatile int stun_port;   // the router's WAN address; what a STUN server saw (network order, host port)
	char loc_host[48], path[200], svc[80]; int loc_port;
	char log[160];
	SceUID th;
} NAT = { .th = -1 };
static void logp(const char *fmt, ...);
static int nat_private(unsigned ip_net) {   // an address that is not on the internet (RFC 1918, carrier-grade 100.64/10, link-local, loopback)
	const unsigned char *b = (const unsigned char *)&ip_net;
	return !ip_net || b[0] == 10 || b[0] == 127 || (b[0] == 172 && (b[1] & 0xF0) == 16) || (b[0] == 192 && b[1] == 168) || (b[0] == 100 && (b[1] & 0xC0) == 64) || (b[0] == 169 && b[1] == 254);
}
// ---- the room code
static const char NAT_B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
static int nat_chk(unsigned long long v) { unsigned h = 5; for (int i = 0; i < 6; i++) h = (h * 31 + (unsigned)((v >> (8 * i)) & 255) + 7) & 0xFFFF; return (int)((h ^ (h >> 5) ^ (h >> 10)) & 31); }
static void nat_code(unsigned ip_net, int port, char *out, int n) {   // "XXXX-XXXX-XXX"
	const unsigned char *b = (const unsigned char *)&ip_net; unsigned long long v = 0; for (int i = 0; i < 4; i++) v = v << 8 | b[i];
	v = v << 16 | (unsigned)(port & 0xFFFF); char c[12]; for (int i = 0; i < 10; i++) c[i] = NAT_B32[(v >> (5 * (9 - i))) & 31]; c[10] = NAT_B32[nat_chk(v)];
	snprintf(out, n, "%.4s-%.4s-%.3s", c, c + 4, c + 8);
}
// a typed room code (dashes, spaces, lower case, O / I / L for 0 / 1 / 1 forgiven) or a plain "a.b.c.d[:port]" -> 1 ok
static int nat_decode(const char *s, unsigned *ip_net, int *port, int defport) {
	unsigned a, b, c, d, p = (unsigned)defport;
	if (strchr(s, '.')) { int k = sscanf(s, " %u.%u.%u.%u:%u", &a, &b, &c, &d, &p); if (k < 4 || a > 255 || b > 255 || c > 255 || d > 255 || p < 1 || p > 65535) return 0;
		unsigned char *o = (unsigned char *)ip_net; o[0] = (unsigned char)a; o[1] = (unsigned char)b; o[2] = (unsigned char)c; o[3] = (unsigned char)d; *port = (int)p; return 1; }
	unsigned long long v = 0; int n = 0, chk = -1;
	for (; *s; s++) { int ch = *s; if (ch >= 'a' && ch <= 'z') ch -= 32;
		if (ch == 'O') ch = '0'; else if (ch == 'I' || ch == 'L') ch = '1'; else if (ch == 'U') ch = 'V';
		const char *q = ch ? strchr(NAT_B32, ch) : NULL; if (!q) { if (ch == '-' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t') continue; return 0; }
		if (n < 10) v = v << 5 | (unsigned)(q - NAT_B32); else if (n == 10) chk = (int)(q - NAT_B32); else return 0;
		n++; }
	if (n != 11 || chk != nat_chk(v)) return 0;
	unsigned char *o = (unsigned char *)ip_net; o[0] = (unsigned char)(v >> 40); o[1] = (unsigned char)(v >> 32); o[2] = (unsigned char)(v >> 24); o[3] = (unsigned char)(v >> 16); *port = (int)(v & 0xFFFF);
	return *port > 0;
}
// ---- small socket helpers (blocking, with time-outs: the worker thread only)
static int nat_tmo(int s, int ms) { int us = ms * 1000; sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO, &us, sizeof(us)); return sceNetSetsockopt(s, SCE_NET_SOL_SOCKET, SCE_NET_SO_SNDTIMEO, &us, sizeof(us)); }
static void nat_sa(SceNetSockaddrIn *a, unsigned ip_net, int port) { memset(a, 0, sizeof(*a)); a->sin_len = sizeof(*a); a->sin_family = SCE_NET_AF_INET; a->sin_port = sceNetHtons((unsigned short)port); a->sin_addr.s_addr = ip_net; }
// one HTTP exchange: connect, send req, read until the peer closes (or the buffer is full / 4 s pass) -> bytes read (< 0 failed)
static int nat_http(unsigned ip_net, int port, const char *req, int reqn, char *resp, int cap) {
	int s = sceNetSocket("gitsfa_nat", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0); if (s < 0) return -1;
	nat_tmo(s, 4000); SceNetSockaddrIn a; nat_sa(&a, ip_net, port); int n = -1;
	if (sceNetConnect(s, (SceNetSockaddr *)&a, sizeof(a)) >= 0) {
		int o = 0; while (o < reqn) { int k = sceNetSend(s, req + o, reqn - o, 0); if (k <= 0) break; o += k; }
		if (o == reqn) { n = 0; while (n < cap - 1) { int k = sceNetRecv(s, resp + n, cap - 1 - n, 0); if (k <= 0) break; n += k;
			const char *he = strstr(resp, "\r\n\r\n"), *cl = NULL; resp[n] = 0;   // (Content-Length known and all of it here: no need to wait for the close)
			if (he) { for (const char *q = resp; q < he; q++) if (!strncasecmp(q, "Content-Length:", 15)) { cl = q + 15; break; } if (cl && n >= (int)(he + 4 - resp) + atoi(cl)) break; } }
			resp[n] = 0; } }
	sceNetSocketClose(s); return n;
}
static const char *nat_tag(const char *x, const char *tag, char *out, int n) {   // the text of the first <tag>...</tag> at or after x
	char open[48]; snprintf(open, sizeof(open), "<%s>", tag); const char *a = strstr(x, open); if (!a) return NULL;
	a += strlen(open); const char *b = strchr(a, '<'); if (!b || b - a >= n) return NULL;
	memcpy(out, a, (size_t)(b - a)); out[b - a] = 0; return b;
}
static int nat_url(const char *u, char *host, int hn, int *port, char *path, int pn) {   // http://host[:port]/path
	if (strncasecmp(u, "http://", 7)) return 0;
	u += 7; const char *sl = strchr(u, '/'), *co = strchr(u, ':'); if (!sl) sl = u + strlen(u);
	*port = 80; int hl = (int)((co && co < sl ? co : sl) - u); if (hl <= 0 || hl >= hn) return 0;
	memcpy(host, u, (size_t)hl); host[hl] = 0; if (co && co < sl) *port = atoi(co + 1);
	snprintf(path, pn, "%s", *sl ? sl : "/"); for (char *q = path; *q; q++) if (*q == '\r' || *q == '\n' || *q == ' ') { *q = 0; break; }
	return 1;
}
// ---- UPnP IGD
static int nat_ssdp(unsigned gw_net, char *loc, int n) {   // the gateway device's description URL -> 1 found
	{ FILE *f = fopen("ux0:data/gitsfa/net_igd.txt", "rb"); if (f) { int ok = fgets(loc, n, f) != NULL; fclose(f); if (ok && !strncasecmp(loc, "http://", 7)) return 1; } }
	int s = sceNetSocket("gitsfa_ssdp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP); if (s < 0) return 0;
	nat_tmo(s, 350); int found = 0; unsigned mc = 0; sceNetInetPton(SCE_NET_AF_INET, "239.255.255.250", &mc);
	static const char *ST[3] = { "urn:schemas-upnp-org:device:InternetGatewayDevice:1", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1" };
	for (int round = 0; round < 3 && !found; round++) {
		for (int k = 0; k < 3; k++) { char q[256]; int qn = snprintf(q, sizeof(q), "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: %s\r\n\r\n", ST[k]);
			SceNetSockaddrIn a; nat_sa(&a, mc, 1900); sceNetSendto(s, q, qn, 0, (SceNetSockaddr *)&a, sizeof(a));
			if (gw_net) { nat_sa(&a, gw_net, 1900); sceNetSendto(s, q, qn, 0, (SceNetSockaddr *)&a, sizeof(a)); } }   // (many routers answer a search sent straight to them)
		for (int t = 0; t < 3 && !found; t++) { char r[1024]; SceNetSockaddrIn fr; unsigned fl = sizeof(fr); int k = sceNetRecvfrom(s, r, sizeof(r) - 1, 0, (SceNetSockaddr *)&fr, &fl); if (k <= 0) continue;
			r[k] = 0; for (char *q = r; *q; q++) if (!strncasecmp(q, "LOCATION:", 9)) { q += 9; while (*q == ' ') q++; snprintf(loc, n, "%s", q); for (char *e = loc; *e; e++) if (*e == '\r' || *e == '\n') { *e = 0; break; } found = !strncasecmp(loc, "http://", 7); break; } }
	}
	sceNetSocketClose(s); return found;
}
static int nat_soap(unsigned ip_net, const char *action, const char *args, char *resp, int cap) {   // -> the HTTP status (200 ok), < 0 no answer
	static char body[900], req[1400];
	int bn = snprintf(body, sizeof(body), "<?xml version=\"1.0\"?>\r\n<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n", action, NAT.svc, args, action);
	int rn = snprintf(req, sizeof(req), "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s", NAT.path, NAT.loc_host, NAT.loc_port, NAT.svc, action, bn, body);
	if (rn >= (int)sizeof(req)) return -1;
	int n = nat_http(ip_net, NAT.loc_port, req, rn, resp, cap); if (n < 12) return -1;
	return atoi(resp + 9);
}
static int nat_upnp(unsigned gw_net) {   // 1: the port is forwarded (NAT.gw_ip = the router's WAN address when it tells)
	static char loc[256], buf[16384], host[48], path[200]; int port;
	if (!nat_ssdp(gw_net, loc, sizeof(loc)) || !nat_url(loc, host, sizeof(host), &port, path, sizeof(path))) { snprintf(NAT.log, sizeof(NAT.log), "no UPnP gateway answered"); return 0; }
	unsigned ip = 0; if (sceNetInetPton(SCE_NET_AF_INET, host, &ip) != 1) { snprintf(NAT.log, sizeof(NAT.log), "UPnP: the gateway's address %s is not numeric", host); return 0; }
	char req[400]; int rn = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n", path, host, port);
	int n = nat_http(ip, port, req, rn, buf, sizeof(buf)); if (n <= 0) { snprintf(NAT.log, sizeof(NAT.log), "UPnP: no description from %s:%d", host, port); return 0; }
	// the WAN connection service and its control URL
	const char *sv = NULL; static const char *SV[3] = { "urn:schemas-upnp-org:service:WANIPConnection:2", "urn:schemas-upnp-org:service:WANIPConnection:1", "urn:schemas-upnp-org:service:WANPPPConnection:1" };
	for (int k = 0; k < 3 && !sv; k++) { sv = strstr(buf, SV[k]); if (sv) snprintf(NAT.svc, sizeof(NAT.svc), "%s", SV[k]); }
	char ctl[200]; if (!sv || !nat_tag(sv, "controlURL", ctl, sizeof(ctl))) { snprintf(NAT.log, sizeof(NAT.log), "UPnP: the gateway has no WAN connection service"); return 0; }
	snprintf(NAT.loc_host, sizeof(NAT.loc_host), "%s", host); NAT.loc_port = port;
	if (!strncasecmp(ctl, "http://", 7)) { char h2[48]; int p2; if (!nat_url(ctl, h2, sizeof(h2), &p2, NAT.path, sizeof(NAT.path))) return 0; NAT.loc_port = p2; snprintf(NAT.loc_host, sizeof(NAT.loc_host), "%s", h2); sceNetInetPton(SCE_NET_AF_INET, h2, &ip); }
	else snprintf(NAT.path, sizeof(NAT.path), "%s%.190s", ctl[0] == '/' ? "" : "/", ctl);
	const unsigned char *l = (const unsigned char *)&NAT.lan_ip; char args[420]; int st = 0;
	for (int lease = 0; lease < 2; lease++) {   // a permanent mapping first (the commonest), a one-hour lease for routers that refuse it (error 725)
		snprintf(args, sizeof(args), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol><NewInternalPort>%d</NewInternalPort><NewInternalClient>%u.%u.%u.%u</NewInternalClient><NewEnabled>1</NewEnabled><NewPortMappingDescription>GITS First Assault</NewPortMappingDescription><NewLeaseDuration>%d</NewLeaseDuration>",
			NAT.port, NAT.port, l[0], l[1], l[2], l[3], lease ? 3600 : 0);
		st = nat_soap(ip, "AddPortMapping", args, buf, sizeof(buf)); if (st == 200) break; }
	if (st != 200) { char ec[16] = ""; nat_tag(buf, "errorCode", ec, sizeof(ec)); snprintf(NAT.log, sizeof(NAT.log), "UPnP: the router refused the port (HTTP %d, error %s)", st, ec[0] ? ec : "?"); return 0; }
	if (nat_soap(ip, "GetExternalIPAddress", "", buf, sizeof(buf)) == 200) { char e[32]; unsigned w = 0; if (nat_tag(buf, "NewExternalIPAddress", e, sizeof(e)) && sceNetInetPton(SCE_NET_AF_INET, e, &w) == 1) NAT.gw_ip = w; }
	return 1;
}
static void nat_upnp_unmap(void) {
	if (NAT.mapped != 1) return; unsigned ip = 0; if (sceNetInetPton(SCE_NET_AF_INET, NAT.loc_host, &ip) != 1) return;
	static char buf[2048]; char args[160]; snprintf(args, sizeof(args), "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort><NewProtocol>UDP</NewProtocol>", NAT.port);
	nat_soap(ip, "DeletePortMapping", args, buf, sizeof(buf));
}
// ---- NAT-PMP (RFC 6886): op 0 = the public address, op 1 = map UDP; life 0 removes the mapping
static int nat_pmp(unsigned gw_net, int life) {
	if (!gw_net) return 0;
	int s = sceNetSocket("gitsfa_pmp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, SCE_NET_IPPROTO_UDP); if (s < 0) return 0;
	nat_tmo(s, 400); SceNetSockaddrIn a; nat_sa(&a, gw_net, 5351); int ok = 0;
	for (int t = 0; t < 3 && !ok; t++) {
		unsigned char q[12] = { 0, 1, 0, 0, (unsigned char)(NAT.port >> 8), (unsigned char)NAT.port, (unsigned char)(NAT.port >> 8), (unsigned char)NAT.port, (unsigned char)(life >> 24), (unsigned char)(life >> 16), (unsigned char)(life >> 8), (unsigned char)life }, r[32];
		sceNetSendto(s, q, 12, 0, (SceNetSockaddr *)&a, sizeof(a));
		int k = sceNetRecvfrom(s, r, sizeof(r), 0, NULL, NULL); if (k >= 16 && r[0] == 0 && r[1] == 129 && r[2] == 0 && r[3] == 0 && (r[10] << 8 | r[11]) == NAT.port) ok = 1;
	}
	if (ok && life) for (int t = 0; t < 2; t++) { unsigned char q[2] = { 0, 0 }, r[32]; sceNetSendto(s, q, 2, 0, (SceNetSockaddr *)&a, sizeof(a));
		int k = sceNetRecvfrom(s, r, sizeof(r), 0, NULL, NULL); if (k >= 12 && r[1] == 128 && r[2] == 0 && r[3] == 0) { unsigned w; memcpy(&w, r + 8, 4); NAT.gw_ip = w; break; } }
	sceNetSocketClose(s); return ok;
}
// ---- STUN (RFC 5389 binding) through the game socket: nat_stun_rx is called by the UDP receive path for every datagram
static unsigned char nat_stun_id[12]; static unsigned nat_stun_srv[4]; static int nat_stun_sport[4], nat_stun_n;
static int nat_stun_rx(const unsigned char *d, int n) {   // 1: a STUN answer (consumed)
	if (n < 20 || d[0] != 1 || d[1] != 1 || d[4] != 0x21 || d[5] != 0x12 || d[6] != 0xA4 || d[7] != 0x42 || memcmp(d + 8, nat_stun_id, 12)) return 0;
	for (int o = 20; o + 4 <= n; ) { int t = d[o] << 8 | d[o + 1], l = d[o + 2] << 8 | d[o + 3]; const unsigned char *v = d + o + 4; if (o + 4 + l > n) break;
		if ((t == 0x0020 || t == 0x0001) && l >= 8 && v[1] == 1) { unsigned char ip[4]; int port = v[2] << 8 | v[3]; memcpy(ip, v + 4, 4);
			if (t == 0x0020) { port ^= 0x2112; ip[0] ^= 0x21; ip[1] ^= 0x12; ip[2] ^= 0xA4; ip[3] ^= 0x42; }
			unsigned w; memcpy(&w, ip, 4); NAT.stun_port = port; NAT.stun_ip = w; if (t == 0x0020) break; }
		o += 4 + ((l + 3) & ~3); }
	return 1;
}
static int nat_resolve(const char *host, unsigned *ip_net) {
	if (sceNetInetPton(SCE_NET_AF_INET, host, ip_net) == 1) return 1;
	int rid = sceNetResolverCreate("gitsfa_dns", NULL, 0); if (rid < 0) return 0;
	SceNetInAddr a; memset(&a, 0, sizeof(a)); int r = sceNetResolverStartNtoa(rid, host, &a, 2000000, 2, 0); sceNetResolverDestroy(rid);
	if (r < 0) return 0; *ip_net = a.s_addr; return 1;
}
static int (*nat_game_send)(unsigned ip_net, int port, const void *p, int n);   // net_tr.h: a datagram from the game socket
static void nat_stun(void) {
	static const char *DEF[] = { "stun.l.google.com:19302", "stun.cloudflare.com:3478", "stun1.l.google.com:19302" }; char line[4][96]; int nl = 0;
	{ FILE *f = fopen("ux0:data/gitsfa/net_stun.txt", "rb"); if (f) { while (nl < 4 && fgets(line[nl], sizeof(line[0]), f)) { char *e = line[nl]; while (*e && *e != '\r' && *e != '\n' && *e != ' ') e++; *e = 0; if (line[nl][0] && line[nl][0] != '#') nl++; } fclose(f); } }
	if (!nl) for (; nl < 3; nl++) snprintf(line[nl], sizeof(line[0]), "%s", DEF[nl]);
	nat_stun_n = 0; for (int i = 0; i < nl; i++) { char *c = strrchr(line[i], ':'); int port = 3478; if (c) { *c = 0; port = atoi(c + 1); } unsigned ip; if (port > 0 && nat_resolve(line[i], &ip)) { nat_stun_srv[nat_stun_n] = ip; nat_stun_sport[nat_stun_n++] = port; } }
	if (!nat_stun_n || !nat_game_send) return;
	SceUInt64 t = sceKernelGetProcessTimeWide(); for (int i = 0; i < 12; i++) nat_stun_id[i] = (unsigned char)((t >> (i * 5)) * 131 + i * 29 + (NAT.port & 255));
	unsigned char q[20] = { 0, 1, 0, 0, 0x21, 0x12, 0xA4, 0x42 }; memcpy(q + 8, nat_stun_id, 12);
	for (int tr = 0; tr < 8 && !NAT.stun_ip; tr++) { nat_game_send(nat_stun_srv[tr % nat_stun_n], nat_stun_sport[tr % nat_stun_n], q, 20); sceKernelDelayThread(250000); }
}
// ---- the worker
static int nat_thread(SceSize args, void *argp) {
	(void)args; (void)argp;
	if (NAT.want_unmap) { if (NAT.mapped == 1) nat_upnp_unmap(); else if (NAT.mapped == 2) nat_pmp(NAT.gate, 0); NAT.mapped = 0; NAT.state = NAT_IDLE; return sceKernelExitDeleteThread(0); }
	unsigned gw = 0; { SceNetCtlInfo info; memset(&info, 0, sizeof(info)); if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_DEFAULT_ROUTE, &info) >= 0) sceNetInetPton(SCE_NET_AF_INET, info.default_route, &gw); }
	NAT.log[0] = 0; NAT.mapped = 0; NAT.gw_ip = 0; NAT.stun_ip = 0; NAT.stun_port = 0; NAT.gate = gw;
	if (nat_upnp(gw)) NAT.mapped = 1; else if (nat_pmp(gw, 7200)) NAT.mapped = 2;
	nat_stun();
	const unsigned char *g = (const unsigned char *)&NAT.gw_ip, *s = (const unsigned char *)&NAT.stun_ip;
	logp("net: internet room: port %d %s%s%s; router's address %u.%u.%u.%u, seen from the internet as %u.%u.%u.%u:%d\n", NAT.port, NAT.mapped == 1 ? "forwarded by UPnP" : NAT.mapped == 2 ? "forwarded by NAT-PMP" : "NOT forwarded",
		NAT.log[0] ? " - " : "", NAT.log, g[0], g[1], g[2], g[3], s[0], s[1], s[2], s[3], NAT.stun_port);
	NAT.state = NAT_DONE; return sceKernelExitDeleteThread(0);
}
static void nat_start(int port, unsigned lan_ip, int unmap) {
	if (NAT.state == NAT_WORKING) return;
	NAT.port = port ? port : NAT.port; if (lan_ip) NAT.lan_ip = lan_ip; NAT.want_unmap = unmap; if (unmap && !NAT.mapped) return;
	NAT.state = NAT_WORKING; NAT.th = sceKernelCreateThread("gitsfa_nat", nat_thread, 0x10000100 + 20, 0x10000, 0, 0, NULL);
	if (NAT.th < 0 || sceKernelStartThread(NAT.th, 0, NULL) < 0) NAT.state = unmap ? NAT_IDLE : NAT_DONE;
}
// the address to put in the room code and a line for the player; -> 0 still working, 1 an internet address, 2 this network only
static int nat_result(unsigned *ip_net, int *port, char *note, int n) {
	if (NAT.state != NAT_DONE) { snprintf(note, n, "ASKING YOUR ROUTER TO OPEN THE PORT..."); return 0; }
	unsigned pub = NAT.stun_ip ? NAT.stun_ip : NAT.gw_ip;
	if (!pub || nat_private(pub)) { *ip_net = NAT.lan_ip; *port = NAT.port; snprintf(note, n, NAT.gw_ip && nat_private(NAT.gw_ip) && !NAT.stun_ip ? "SHARED PROVIDER ADDRESS: SAME WI-FI ONLY" : "NO INTERNET ADDRESS FOUND: SAME WI-FI ONLY"); return 2; }
	*ip_net = pub; *port = NAT.mapped ? NAT.port : NAT.stun_port ? NAT.stun_port : NAT.port;
	if (NAT.mapped && NAT.stun_ip && NAT.gw_ip && NAT.gw_ip != NAT.stun_ip) snprintf(note, n, "PORT OPEN, BEHIND 2 ROUTERS: MAY NOT WORK");
	else if (NAT.mapped) snprintf(note, n, "PORT %d OPENED BY YOUR ROUTER", NAT.port);
	else snprintf(note, n, "PORT NOT OPEN: FORWARD UDP %d ON ROUTER", NAT.port);
	return 1;
}
