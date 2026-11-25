// net (stage 1): the session under the AD-HOC screens (NET.md): the I/O thread, packet framing, discovery, joining (with the
// build check), the room's slots, READY / team / START, keep-alives and time-outs, per-peer statistics. The match itself is
// net_game.h (it registers net_game_rx and the slot callbacks). Included by main.c after menu.h (menu_net.h: NUV / NUI).
// Roles: browse (probe the network once a second, list the rooms that answer), host (answer probes, keep the room, send its
// state 4x a second), client (joined a room: send our state 4x a second). Times: the process clock (real time on the Vita).
#pragma once
#include "net_tr.h"
#define NET_PROTO 1
#ifndef NET_VERSION
#define NET_VERSION "b123"            // bump with the build: different versions refuse each other (ux0:data/gitsfa/netversion.txt overrides it, tests)
#endif
#define NET_PMAX 1200                // largest datagram we send (PDP MFS 1444, Wi-Fi 1500)
#define NET_RING 128
#define NET_MAXPEER 7
#define NET_MAXSLOT 8
enum { NP_DISC = 1, NP_ROOM, NP_JOIN, NP_WELCOME, NP_LOBBY, NP_LSTATE, NP_BYE, NP_INPUT, NP_SNAP };
enum { NR_LOBBY, NR_STARTING, NR_MATCH, NR_RESULTS };
enum { NW_OK, NW_VERSION, NW_FULL, NW_PROGRESS, NW_CLOSED };
enum { NL_OFF, NL_BROWSE, NL_HOST, NL_JOINING, NL_CLIENT };
static void logp(const char *fmt, ...);   // main.c

// ------------------------------------------------------------------ byte packing (little-endian)
typedef struct { unsigned char *p; int n, cap; } NetW;
typedef struct { const unsigned char *p; int n, i, bad; } NetR;
static void nw8(NetW *w, unsigned v) { if (w->n < w->cap) w->p[w->n] = (unsigned char)v; w->n++; }
static void nw16(NetW *w, unsigned v) { nw8(w, v); nw8(w, v >> 8); }
static void nw32(NetW *w, unsigned v) { nw16(w, v); nw16(w, v >> 16); }
static void nwf(NetW *w, float f) { unsigned u; memcpy(&u, &f, 4); nw32(w, u); }
static void nwb(NetW *w, const void *d, int n) { for (int i = 0; i < n; i++) nw8(w, ((const unsigned char *)d)[i]); }
static void nws(NetW *w, const char *s, int n) { for (int i = 0; i < n; i++) nw8(w, s && i < (int)strlen(s) ? (unsigned char)s[i] : 0); }
static unsigned nr8(NetR *r) { if (r->i >= r->n) { r->bad = 1; return 0; } return r->p[r->i++]; }
static unsigned nr16(NetR *r) { unsigned a = nr8(r); return a | nr8(r) << 8; }
static unsigned nr32(NetR *r) { unsigned a = nr16(r); return a | nr16(r) << 16; }
static float nrf(NetR *r) { unsigned u = nr32(r); float f; memcpy(&f, &u, 4); return f; }
static void nrb(NetR *r, void *d, int n) { for (int i = 0; i < n; i++) ((unsigned char *)d)[i] = (unsigned char)nr8(r); }
static void nrs(NetR *r, char *s, int n) { for (int i = 0; i < n; i++) s[i] = (char)nr8(r); s[n - 1] = 0; for (int i = 0; s[i]; i++) if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) s[i] = '?'; }

// ------------------------------------------------------------------ the I/O thread and its two rings (single producer, single consumer)
typedef struct { NetAddr a; unsigned short n, bc; unsigned char d[NET_PMAX]; } NetPkt;
typedef struct { NetPkt q[NET_RING]; unsigned head, tail; unsigned drops; } NetRing;
static NetPkt *ring_wr(NetRing *r) { unsigned h = __atomic_load_n(&r->head, __ATOMIC_RELAXED), t = __atomic_load_n(&r->tail, __ATOMIC_ACQUIRE); if (h - t >= NET_RING) { r->drops++; return NULL; } return &r->q[h % NET_RING]; }
static void ring_commit(NetRing *r) { __atomic_store_n(&r->head, __atomic_load_n(&r->head, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE); }
static NetPkt *ring_rd(NetRing *r) { unsigned t = __atomic_load_n(&r->tail, __ATOMIC_RELAXED), h = __atomic_load_n(&r->head, __ATOMIC_ACQUIRE); return t == h ? NULL : &r->q[t % NET_RING]; }
static void ring_pop(NetRing *r) { __atomic_store_n(&r->tail, __atomic_load_n(&r->tail, __ATOMIC_RELAXED) + 1, __ATOMIC_RELEASE); }

typedef struct { unsigned char mode, map, team_size, bots, skill, score_limit, time_limit; } NetSet;
typedef struct { NetAddr addr; unsigned room, build; char host[20], ver[12]; NetSet set; int state, humans, maxh; double seen; } NetRoom;
typedef struct { int used, ready, connected, host, peer, chr, prim, sec; unsigned id; char name[20]; } NetSlot;   // id: the player's join nonce (& 0xFFFF; the host 0)   // a human in a room slot (slot k: team k & 1)
typedef struct {
	int used; NetAddr addr; unsigned nonce; int slot, loaded; double last_rx; char name[20]; int chr, prim, sec, ready, want_team;
	unsigned rx_b, tx_b, rx_p, tx_p, s_rx_b, s_tx_b, s_rx_p, s_tx_p;   // this second / the last whole second
	float rtt;
} NetPeer;
static struct {
	int role, kind, up;                     // NL_*, 0 ad-hoc / 1 LAN / 2 internet (b108), 0 down / 1 pending / 2 up / -1 failed
	char code[16], code_in[16], note[72]; double nat_t;   // b108: our room's code (host) / the code looked for (browse)
	const NetTr *tr; int sock, sock_host;
	SceUID th, sema; int quit; unsigned busy_us, wakes; NetRing in, out;
	unsigned build; char ver[12], name[20];
	// the room (host: ours; client: the one joined)
	unsigned room; NetSet set; int state, gen, my_slot, nslot; float countdown; unsigned seed; NetSlot slot[NET_MAXSLOT]; unsigned short lseq;
	NetPeer peer[NET_MAXPEER];
	NetAddr host; double host_rx; unsigned nonce; double join_t; int join_tries, join_room, started_gen, lost, my_ready, last_state;
	NetRoom rooms[NUV_ROOMS]; int nrooms; double disc_t, send_t, stat_t, start_t;
	unsigned tot_rx_b, tot_tx_b, s_rx_b, s_tx_b;   // client: bytes this / last second
	char why[96];
} NL = { .th = -1, .sema = -1, .sock = -1 };
static double net_now(void) { return sceKernelGetProcessTimeWide() / 1e6; }
static void (*net_game_rx)(int peer, const unsigned char *d, int n);   // net_game.h: INPUT (host; peer index) / SNAP (client; peer -1)
static void (*net_game_slot_left)(int slot);                           // net_game.h: a player left a running match (host)
static void (*net_game_slot_joined)(int slot);                         // net_game.h: a player joined a running match (host)

static unsigned net_fnv(const char *s) { unsigned h = 2166136261u; while (*s) h = (h ^ (unsigned char)*s++) * 16777619u; return h; }
static void net_version_init(void) {
	if (NL.build) return;
	snprintf(NL.ver, sizeof(NL.ver), "%s", NET_VERSION);
	FILE *f = fopen("ux0:data/gitsfa/netversion.txt", "rb"); if (f) { char b[16] = { 0 }; if (fscanf(f, "%11s", b) == 1) snprintf(NL.ver, sizeof(NL.ver), "%s", b); fclose(f); }
	char b[32]; snprintf(b, sizeof(b), "%s/p%d", NL.ver, NET_PROTO); NL.build = net_fnv(b) | 1;
}
static void net_hdr(NetW *w, unsigned char *buf, int type, unsigned room) { w->p = buf; w->n = 0; w->cap = NET_PMAX; nw8(w, 'G'); nw8(w, 'N'); nw8(w, NET_PROTO); nw8(w, type); nw32(w, room); }
static int net_peer_of(const NetAddr *a) { for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used && net_addr_eq(&NL.peer[i].addr, a)) return i; return -1; }
static void net_kick(void) { if (NL.sema >= 0) sceKernelSignalSema(NL.sema, 1); }
static int net_send(const NetAddr *to, const unsigned char *d, int n) {
	if (n > NET_PMAX || NL.sock < 0) return -1;
	NetPkt *p = ring_wr(&NL.out); if (!p) return -1;
	if (to) p->a = *to; p->bc = to == NULL; p->n = (unsigned short)n; memcpy(p->d, d, n); ring_commit(&NL.out);
	int pi = to ? net_peer_of(to) : -1; if (pi >= 0) { NL.peer[pi].tx_b += n + 28; NL.peer[pi].tx_p++; }   // (+28: the IP / UDP headers on the air)
	NL.tot_tx_b += n + 28; return 0;
}
static int net_thread(SceSize args, void *argp) {
	(void)args; (void)argp; static unsigned char buf[NET_PMAX + 64];
	while (!__atomic_load_n(&NL.quit, __ATOMIC_ACQUIRE)) {
		SceUInt to = 1000; sceKernelWaitSema(NL.sema, 1, &to);
		uint64_t t0 = sceKernelGetSystemTimeWide(); int work = 0; NetPkt *p;
		while ((p = ring_rd(&NL.out))) { if (p->bc) NL.tr->bcast(p->d, p->n); else NL.tr->send(&p->a, p->d, p->n); ring_pop(&NL.out); work++; }
		for (int k = 0; k < 64; k++) { NetAddr from; int n = NL.tr->recv(&from, buf, sizeof(buf)); if (n <= 0) break; work++;
			if (n > NET_PMAX) continue; NetPkt *q = ring_wr(&NL.in); if (!q) continue; q->a = from; q->n = (unsigned short)n; q->bc = 0; memcpy(q->d, buf, n); ring_commit(&NL.in); }
		if (work) __atomic_fetch_add(&NL.busy_us, (unsigned)(sceKernelGetSystemTimeWide() - t0), __ATOMIC_RELAXED);
		__atomic_fetch_add(&NL.wakes, 1, __ATOMIC_RELAXED);
	}
	return 0;
}
static void net_thread_stop(void) {
	if (NL.th < 0) return;
	__atomic_store_n(&NL.quit, 1, __ATOMIC_RELEASE); net_kick(); sceKernelWaitThreadEnd(NL.th, NULL, NULL); sceKernelDeleteThread(NL.th); NL.th = -1;
}
static int net_thread_start(void) {
	if (NL.th >= 0) return 0;
	if (NL.sema < 0) NL.sema = sceKernelCreateSema("gitsfa_net_s", 0, 0, 1, NULL);
	NL.in.head = NL.in.tail = NL.out.head = NL.out.tail = 0; NL.quit = 0;
	NL.th = sceKernelCreateThread("gitsfa_net", net_thread, 0x10000100, 0x4000, 0, 0, NULL);
	if (NL.th < 0 || sceKernelStartThread(NL.th, 0, NULL) < 0) { logp("net: I/O thread failed (0x%08X)\n", NL.th); NL.th = -1; return -1; }
	return 0;
}
// the socket: as a host (the game port) or a client (any port); the I/O thread is stopped while it changes
static int net_socket(int host) {
	net_thread_stop(); if (NL.sock >= 0) NL.tr->close(); NL.sock = -1;
	int r = NL.tr->open(host); if (r < 0) { logp("net: %s socket (%s) failed (0x%08X)\n", NL.tr->name, host ? "host" : "client", r); return r; }
	NL.sock = 1; NL.sock_host = host; net_thread_start();
	NetAddr me; char s[40]; NL.tr->self(&me); NL.tr->str(&me, s, sizeof(s)); logp("net: %s socket open as %s (%s)\n", NL.tr->name, host ? "host" : "client", s);
	return 0;
}
static void net_alert(const char *t, const char *l2) { snprintf(NUV.alert, sizeof(NUV.alert), "%s", t); snprintf(NUV.alert2, sizeof(NUV.alert2), "%s", l2 ? l2 : ""); logp("net: %s - %s\n", t, l2 ? l2 : ""); }
// b76: the Vita's user name in the HUD fonts' alphabet (upper case ASCII, others '?'), up to 16 characters; 0 when there is
// none or it is mostly outside that alphabet (host: HG_USER_NAME)
static int vita_user_name(char *out, int n) {
	char u[64] = { 0 };
#ifdef HG_HOST
	{ const char *e = getenv("HG_USER_NAME"); if (e) snprintf(u, sizeof(u), "%s", e); }
#else
	static int au; if (!au) { SceAppUtilInitParam ip; SceAppUtilBootParam bp; memset(&ip, 0, sizeof(ip)); memset(&bp, 0, sizeof(bp)); au = sceAppUtilInit(&ip, &bp) >= 0 ? 1 : -1; }
	if (au <= 0 || sceAppUtilSystemParamGetString(SCE_SYSTEM_PARAM_ID_USERNAME, (SceChar8 *)u, sizeof(u) < SCE_SYSTEM_PARAM_USERNAME_MAXSIZE ? sizeof(u) : SCE_SYSTEM_PARAM_USERNAME_MAXSIZE) < 0) return 0;
#endif
	int bad = 0, len = 0; char t[20];
	for (const char *p = u; *p && len < 16; p++) { char ch = *p; if (ch >= 'a' && ch <= 'z') ch -= 32; if ((unsigned char)ch > 126 || (unsigned char)ch < 32) { ch = '?'; bad++; } t[len++] = ch; }
	t[len] = 0; if (!len || bad * 2 > len) return 0;
	snprintf(out, n, "%s", t); return 1;
}
static void net_name_init(const MenuConfig *c) {
	snprintf(NL.name, sizeof(NL.name), "%s", c->name[0] ? c->name : "OPERATOR");
#ifdef HG_HOST
	{ const char *e = getenv("HG_NET_NAME"); if (e && *e) snprintf(NL.name, sizeof(NL.name), "%.16s", e); }
#else
	if (!strcmp(NL.name, "OPERATOR")) vita_user_name(NL.name, sizeof(NL.name));   // the default profile name: the Vita's own user name is more useful in a room
#endif
	for (char *p = NL.name; *p; p++) { if (*p >= 'a' && *p <= 'z') *p -= 32; if ((unsigned char)*p > 126 || (unsigned char)*p < 32) *p = '?'; }   // (the HUD fonts: upper case ASCII)
}

// ------------------------------------------------------------------ the room's slots (host)
static int net_humans(int team) { int n = 0; for (int k = 0; k < NL.nslot; k++) if (NL.slot[k].used && (team < 0 || (k & 1) == team)) n++; return n; }
static int net_free_slot(int team) { for (int k = 0; k < NL.nslot; k++) if (!NL.slot[k].used && (k & 1) == team) return k; return -1; }
static void net_slot_name(int k, const char *want) {   // unique in the room
	char b[20]; snprintf(b, sizeof(b), "%.16s", want);
	for (int n = 2; n < 9; n++) { int clash = 0; for (int j = 0; j < NL.nslot; j++) if (j != k && NL.slot[j].used && !strcmp(NL.slot[j].name, b)) clash = 1; if (!clash) break; snprintf(b, sizeof(b), "%.14s %d", want, n); }
	snprintf(NL.slot[k].name, sizeof(NL.slot[k].name), "%s", b);
}
static int net_take_slot(int pref_team) {   // a place for a joining player: the team with fewer players (in a match: a bot's place)
	int t = pref_team >= 0 && pref_team < 2 ? pref_team : net_humans(1) < net_humans(0) ? 1 : 0;
	int k = net_free_slot(t); if (k < 0) k = net_free_slot(t ^ 1);
	if (k >= 0 && NL.state == NR_MATCH && !NL.set.bots) return -1;   // in a match without bots there is no body to take
	return k;
}
static void net_move_slot(int from, int to) {
	NL.slot[to] = NL.slot[from]; memset(&NL.slot[from], 0, sizeof(NetSlot));
	if (NL.slot[to].peer >= 0) NL.peer[NL.slot[to].peer].slot = to;
	if (from == NL.my_slot) NL.my_slot = to;
}
static void net_drop_peer(int i, const char *why) {
	NetPeer *P = &NL.peer[i]; if (!P->used) return; int k = P->slot;
	logp("net: player %s left (%s)\n", P->name, why);
	if (k >= 0 && k < NET_MAXSLOT && NL.slot[k].peer == i) { memset(&NL.slot[k], 0, sizeof(NetSlot)); if (NL.state == NR_MATCH && net_game_slot_left) net_game_slot_left(k); }
	memset(P, 0, sizeof(*P)); NL.lseq++;
}

// ------------------------------------------------------------------ packets
static void net_send_room(const NetAddr *to) {
	unsigned char b[NET_PMAX]; NetW w; net_hdr(&w, b, NP_ROOM, NL.room); nw32(&w, NL.build); nws(&w, NL.ver, 12);
	nw8(&w, NL.state); nwb(&w, &NL.set, sizeof(NetSet)); nw8(&w, net_humans(-1)); nw8(&w, NL.nslot); nws(&w, NL.name, 20);
	net_send(to, b, w.n);
}
static void net_send_lobby(const NetAddr *to) {
	unsigned char b[NET_PMAX]; NetW w; net_hdr(&w, b, NP_LOBBY, NL.room);
	nw16(&w, NL.lseq); nw8(&w, NL.state); nw8(&w, NL.gen); nw8(&w, (unsigned)(NL.countdown > 0 ? NL.countdown * 10 + 0.5f : 0)); nw32(&w, NL.seed); nwb(&w, &NL.set, sizeof(NetSet));
	nw8(&w, NL.nslot);
	for (int k = 0; k < NL.nslot; k++) { const NetSlot *S = &NL.slot[k]; nw8(&w, (S->used ? 1 : 0) | (S->ready ? 2 : 0) | (S->connected ? 4 : 0) | (S->host ? 8 : 0)); nw8(&w, S->chr); nw8(&w, S->prim); nw8(&w, S->sec); nw16(&w, S->id); nws(&w, S->name, 16); }
	net_send(to, b, w.n);
}
static void net_send_lobby_all(void) { for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used) net_send_lobby(&NL.peer[i].addr); }
static void net_send_bye(const NetAddr *to, int reason) { unsigned char b[64]; NetW w; net_hdr(&w, b, NP_BYE, NL.room); nw8(&w, reason); net_send(to, b, w.n); }
static void net_welcome(const NetAddr *to, unsigned nonce, int res, int slot) {
	unsigned char b[64]; NetW w; net_hdr(&w, b, NP_WELCOME, NL.room); nw32(&w, nonce); nw8(&w, res); nw8(&w, slot); nw32(&w, NL.build); nws(&w, NL.ver, 12); net_send(to, b, w.n);
}
static int net_refuse_log(void) { static unsigned n; n++; return n <= 8 || !(n & 255); }   // b108: an internet room can be knocked on by anyone: the log keeps the first eight refusals, then every 256th
static void net_rx_host(const NetAddr *from, int type, NetR *r) {
	int pi = net_peer_of(from); if (pi >= 0 && type != NP_DISC) { NL.peer[pi].last_rx = net_now(); NL.peer[pi].rx_b += r->n + 28; NL.peer[pi].rx_p++; }   // (a probe is not a sign of life in the room)
	switch (type) {
	case NP_DISC: net_send_room(from); break;
	case NP_JOIN: {
		unsigned build = nr32(r), nonce = nr32(r); char nm[20]; nrs(r, nm, 20); int chr = nr8(r), prim = nr8(r), sec = nr8(r); if (r->bad) return;
		if (pi >= 0) { if (NL.peer[pi].nonce == nonce) net_welcome(from, nonce, NW_OK, NL.peer[pi].slot); return; }   // (a repeat: the answer got lost)
		if (build != NL.build) { net_welcome(from, nonce, NW_VERSION, 0); if (net_refuse_log()) logp("net: refused %s: other build\n", nm); return; }
		if (NL.state == NR_STARTING || NL.state == NR_RESULTS) { net_welcome(from, nonce, NW_PROGRESS, 0); return; }
		int fp = -1; for (int i = 0; i < NET_MAXPEER; i++) if (!NL.peer[i].used) { fp = i; break; }
		int k = fp >= 0 ? net_take_slot(-1) : -1;
		if (k < 0) { net_welcome(from, nonce, NL.state == NR_MATCH ? NW_PROGRESS : NW_FULL, 0); if (net_refuse_log()) logp("net: refused %s: %s\n", nm, NL.state == NR_MATCH ? "match without a free body" : "room full"); return; }
		NetPeer *P = &NL.peer[fp]; memset(P, 0, sizeof(*P)); P->used = 1; P->addr = *from; P->nonce = nonce; P->slot = k; P->last_rx = net_now(); P->chr = chr % 12; P->prim = prim; P->sec = sec; snprintf(P->name, sizeof(P->name), "%s", nm);
		NetSlot *S = &NL.slot[k]; memset(S, 0, sizeof(*S)); S->used = 1; S->connected = 1; S->peer = fp; S->chr = P->chr; S->prim = prim; S->sec = sec; S->id = nonce & 0xFFFF; net_slot_name(k, nm);
		{ char s[40]; NL.tr->str(from, s, sizeof(s)); logp("net: %s joined from %s: slot %d (GHOST %c)%s\n", S->name, s, k, 'A' + (k & 1), NL.state == NR_MATCH ? ", into the running match" : ""); }
		NL.lseq++; net_welcome(from, nonce, NW_OK, k); net_send_lobby(from);
		if (NL.state == NR_MATCH && net_game_slot_joined) net_game_slot_joined(k);
		break; }
	case NP_LSTATE: {
		if (pi < 0) return; NetPeer *P = &NL.peer[pi]; int ready = nr8(r), team = nr8(r), chr = nr8(r), prim = nr8(r), sec = nr8(r), loaded = nr8(r); if (r->bad) return;
		P->ready = ready; P->want_team = team; P->chr = chr % 12; P->prim = prim; P->sec = sec; P->loaded = loaded;
		int k = P->slot; if (k < 0 || k >= NL.nslot) return; NetSlot *S = &NL.slot[k];
		if (NL.state == NR_LOBBY) {
			if (S->ready != !!ready || S->chr != P->chr || S->prim != prim || S->sec != sec) NL.lseq++;
			S->ready = !!ready; S->chr = P->chr; S->prim = prim; S->sec = sec;
			if (team < 2 && team != (k & 1)) { int t = net_free_slot(team); if (t >= 0) { net_move_slot(k, t); NL.lseq++; logp("net: %s moved to GHOST %c\n", NL.slot[t].name, 'A' + team); } }
		}
		break; }
	case NP_BYE: if (pi >= 0) net_drop_peer(pi, "left the room"); break;
	case NP_INPUT: if (pi >= 0 && net_game_rx) net_game_rx(pi, r->p, r->n); break;
	}
}
static void net_rx_client(const NetAddr *from, int type, unsigned room, NetR *r) {
	if (type == NP_ROOM && (NL.role == NL_BROWSE || NL.role == NL_JOINING)) {
		NetRoom R; memset(&R, 0, sizeof(R)); R.addr = *from; R.room = room; R.build = nr32(r); nrs(r, R.ver, 12); R.state = nr8(r); nrb(r, &R.set, sizeof(NetSet)); R.humans = nr8(r); R.maxh = nr8(r); nrs(r, R.host, 20);
		if (r->bad || R.set.mode >= MODE_COUNT || R.set.map >= MENU_NPLAY) return; R.seen = net_now();
		int k = 0; while (k < NL.nrooms && NL.rooms[k].room != room) k++;
		if (k == NL.nrooms) { if (NL.nrooms >= NUV_ROOMS) return; NL.nrooms++; char s[40]; NL.tr->str(from, s, sizeof(s)); logp("net: found room \"%s\" at %s (%s on %s, %d/%d, build %s)\n", R.host, s, MENU_MODE_NAME[R.set.mode], MENU_MAP[R.set.map].stem, R.humans, R.maxh, R.ver); }
		NL.rooms[k] = R; return;
	}
	if (NL.role != NL_JOINING && NL.role != NL_CLIENT) return;
	if (!net_addr_eq(from, &NL.host) || room != NL.room) return;
	NL.host_rx = net_now(); NL.tot_rx_b += r->n + 28;
	switch (type) {
	case NP_WELCOME: {
		unsigned nonce = nr32(r); int res = nr8(r), slot = nr8(r); unsigned hb = nr32(r); char hv[12]; nrs(r, hv, 12); (void)hb;
		if (r->bad || nonce != NL.nonce || NL.role != NL_JOINING) return;
		if (res != NW_OK) { char l2[96]; snprintf(l2, sizeof(l2), res == NW_VERSION ? "This room runs build %s, this game is %s. Both players need the same build." : res == NW_FULL ? "Every place in this room is taken." : res == NW_PROGRESS ? "The match there cannot take another player now." : "The room was closed.", hv, NL.ver);
			net_alert(res == NW_VERSION ? "DIFFERENT BUILD" : res == NW_FULL ? "ROOM FULL" : res == NW_PROGRESS ? "MATCH IN PROGRESS" : "ROOM CLOSED", l2); NL.role = NL_BROWSE; return; }
		NL.role = NL_CLIENT; NL.my_slot = slot; NL.lost = 0; logp("net: joined the room, slot %d\n", slot); break; }
	case NP_LOBBY: {
		unsigned short seq = (unsigned short)nr16(r); int st = nr8(r), gen = nr8(r), cd = nr8(r); unsigned seed = nr32(r); NetSet s; nrb(r, &s, sizeof(s)); int ns = nr8(r);
		if (r->bad || ns > NET_MAXSLOT || s.mode >= MODE_COUNT || s.map >= MENU_NPLAY) return; (void)seq;
		NetSlot sl[NET_MAXSLOT]; memset(sl, 0, sizeof(sl));
		for (int k = 0; k < ns; k++) { int f = nr8(r); sl[k].used = f & 1; sl[k].ready = (f >> 1) & 1; sl[k].connected = (f >> 2) & 1; sl[k].host = (f >> 3) & 1; sl[k].chr = nr8(r) % 12; sl[k].prim = nr8(r); sl[k].sec = nr8(r); sl[k].id = nr16(r); nrs(r, sl[k].name, 16); sl[k].peer = -1; }
		if (r->bad) return;
		NL.set = s; NL.nslot = ns; memcpy(NL.slot, sl, sizeof(sl)); NL.seed = seed; NL.gen = gen;
		if (st == NR_STARTING) NL.countdown = cd / 10.f; else NL.countdown = -1;
		if (st == NR_LOBBY && NL.last_state != NR_LOBBY && NL.last_state != NR_STARTING) NL.my_ready = 0;   // back in the room after a match: ready again
		NL.state = NL.last_state = st;
		for (int k = 0; k < ns; k++) if (sl[k].used && sl[k].id == (NL.nonce & 0xFFFF)) NL.my_slot = k;   // where am I (a team change moves us)
		break; }
	case NP_BYE: net_alert("ROOM CLOSED", "The host closed the room."); NL.role = NL_BROWSE; NL.lost = 1; break;
	case NP_SNAP: if (NL.role == NL_CLIENT && net_game_rx) net_game_rx(-1, r->p, r->n); break;
	}
}
static void net_rx(const NetAddr *from, const unsigned char *d, int n) {
	if (n < 8 || d[0] != 'G' || d[1] != 'N' || d[2] != NET_PROTO) return;
	int type = d[3]; unsigned room = d[4] | d[5] << 8 | d[6] << 16 | (unsigned)d[7] << 24;
	NetR r = { d + 8, n - 8, 0, 0 };
	if (NL.role == NL_HOST) { if (type == NP_DISC || room == NL.room) net_rx_host(from, type, &r); }
	else net_rx_client(from, type, room, &r);
}

// ------------------------------------------------------------------ the menus' hooks (NUI)
static void net_view(void);
static MenuConfig *net_cfg; static int net_want_team = -1;
static void net_close_all(void) {
	if (NL.role == NL_HOST) for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used) net_send_bye(&NL.peer[i].addr, 0);
	if ((NL.role == NL_CLIENT || NL.role == NL_JOINING)) net_send_bye(&NL.host, 0);
	net_kick(); if (NL.th >= 0) sceKernelDelayThread(20000);   // (let the goodbyes out)
	net_thread_stop(); if (NL.sock >= 0 && NL.tr) NL.tr->close(); NL.sock = -1;
	if (NL.tr && NL.up == 2) NL.tr->down();
	NL.up = 0; NL.role = NL_OFF; memset(NL.peer, 0, sizeof(NL.peer)); NL.nrooms = 0; NL.state = NR_LOBBY;
}
// b108 (INTERNET): the room code the player typed -> the address the search goes to (net_tr.h inet_target); kept for next time
static int net_code_set(const char *txt, int save) {
	unsigned ip = 0; int port = 0;
	if (!nat_decode(txt, &ip, &port, NET_UDP_PORT)) { if (save) net_alert("CODE NOT VALID", "Check the room code with the host: 11 letters and digits, like K7QF-2M9X-AB3."); return 0; }
	udp_addr(&inet_target, ip, (unsigned short)port); inet_has_target = 1; nat_code(ip, port, NL.code_in, sizeof(NL.code_in)); NL.disc_t = 0; NL.nrooms = 0;
	{ char s[32]; udp_str(&inet_target, s, sizeof(s)); logp("net: room code %s -> %s\n", NL.code_in, s); }
	if (save) { FILE *f = fopen("ux0:data/gitsfa/net_code.txt", "wb"); if (f) { fputs(NL.code_in, f); fclose(f); } }
	return 1;
}
#ifndef HG_HOST
#include <psp2/ime_dialog.h>
static int net_ime_on; static SceWChar16 net_ime_buf[24], net_ime_title[16], net_ime_init[24];
static void nui_code_enter(void) {
	if (net_ime_on || net_dialog) return;
	{ const char *t = "ROOM CODE"; int i = 0; for (; t[i] && i < 15; i++) net_ime_title[i] = (SceWChar16)t[i]; net_ime_title[i] = 0; }
	for (int i = 0; i < 16; i++) net_ime_init[i] = (SceWChar16)(unsigned char)NL.code_in[i]; net_ime_init[15] = 0; memset(net_ime_buf, 0, sizeof(net_ime_buf));
	{ SceCommonDialogConfigParam cp; sceCommonDialogConfigParamInit(&cp); sceCommonDialogSetConfigParam(&cp); }
	SceImeDialogParam p; sceImeDialogParamInit(&p); p.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH; p.languagesForced = SCE_TRUE; p.type = SCE_IME_TYPE_DEFAULT;
	p.title = net_ime_title; p.maxTextLength = 21; p.initialText = net_ime_init; p.inputTextBuffer = net_ime_buf;
	int r = sceImeDialogInit(&p);
	if (r < 0) { logp("net: the system keyboard did not open (0x%08X)\n", r); net_alert("KEYBOARD UNAVAILABLE", "Put the room code in ux0:data/gitsfa/net_code.txt and open this screen again."); return; }
	net_ime_on = 1; net_dialog = 1;
}
static void net_ime_poll(void) {
	if (!net_ime_on || sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;
	SceImeDialogResult res; memset(&res, 0, sizeof(res)); sceImeDialogGetResult(&res);
	if (res.button == SCE_IME_DIALOG_BUTTON_ENTER) { char t[24]; int i = 0; for (; i < 22 && net_ime_buf[i]; i++) t[i] = net_ime_buf[i] < 128 ? (char)net_ime_buf[i] : '?'; t[i] = 0; if (t[0]) net_code_set(t, 1); }
	sceImeDialogTerm(); net_ime_on = 0; net_dialog = 0;
}
#else
static void nui_code_enter(void) { const char *e = getenv("HG_NET_CODE"); if (e) net_code_set(e, 1); }   // (the host build has no keyboard dialog: tests)
static void net_ime_poll(void) {}
#endif
static void nui_open(int transport) {
	net_version_init(); if (net_cfg) net_name_init(net_cfg);
	if (NL.up == 2 && NL.kind == transport && NL.role != NL_OFF) return;
	net_close_all(); NL.kind = transport; NL.tr = transport == 2 ? &TR_INET : transport ? &TR_UDP : &TR_PDP; NL.nrooms = 0;
	if (transport == 2 && !NL.code_in[0]) { char t[40] = ""; FILE *cf = fopen("ux0:data/gitsfa/net_code.txt", "rb"); if (cf) { if (!fgets(t, sizeof(t), cf)) t[0] = 0; fclose(cf); } if (t[0]) net_code_set(t, 0); }   // (b108: the last room code typed)
	int r = NL.tr->up(NL.why, sizeof(NL.why));
	logp("net: %s up: %s%s\n", NL.tr->name, r < 0 ? "FAILED: " : r ? "waiting for the system dialog" : "ok", r < 0 ? NL.why : "");
	if (r < 0) { NL.up = -1; net_alert(transport == 2 ? "INTERNET UNAVAILABLE" : transport ? "LAN UNAVAILABLE" : "AD-HOC UNAVAILABLE", NL.why); return; }
	NL.up = r ? 1 : 2;
	if (NL.up == 2) { if (net_socket(0) < 0) { NL.up = -1; net_alert("NETWORK ERROR", "The game could not open its network port."); return; } NL.role = NL_BROWSE; NL.disc_t = 0; }
}
static void nui_close(void) { logp("net: closed (back to the lobby)\n"); net_close_all(); }
static void nui_set_transport(int t) { NUV.transport = t; nui_open(t); }
static int nui_host(const MenuConfig *c) {
	if (NL.up != 2) return -1;
	if (net_socket(1) < 0) { net_alert("CANNOT HOST", "The game port is in use or the network is down."); net_socket(0); return -1; }
	NL.role = NL_HOST; NL.room = (unsigned)(sceKernelGetProcessTimeWide() * 2654435761u) | 1; NL.state = NR_LOBBY; NL.gen = 0; NL.countdown = -1; NL.lseq = 1;
	NL.set.mode = (unsigned char)c->mode; NL.set.map = (unsigned char)c->map; NL.set.team_size = (unsigned char)(c->team_size < 1 ? 1 : c->team_size > 4 ? 4 : c->team_size);
	NL.set.bots = (unsigned char)NUV.bots; NL.set.skill = (unsigned char)(c->bot_skill & 3); NL.set.score_limit = (unsigned char)c->score_limit; NL.set.time_limit = (unsigned char)c->time_limit;
	NL.nslot = NL.set.team_size * 2; memset(NL.slot, 0, sizeof(NL.slot)); memset(NL.peer, 0, sizeof(NL.peer));
	NetSlot *S = &NL.slot[0]; S->used = 1; S->ready = 1; S->connected = 1; S->host = 1; S->peer = -1; S->chr = c->chr; S->prim = c->primary; S->sec = c->secondary; snprintf(S->name, sizeof(S->name), "%s", NL.name);
	NL.my_slot = 0; NL.code[0] = 0; NL.note[0] = 0;
	if (NL.kind == 2) { nat_game_send = udp_game_send; nat_start(udp_port, udp_ip, 0); NL.nat_t = net_now(); }   // b108: the port and our public address (net_nat.h)
	logp("net: hosting \"%s\": %s on %s, %d v %d, bots %s, %s\n", NL.name, MENU_MODE_NAME[c->mode], MENU_MAP[c->map].stem, NL.set.team_size, NL.set.team_size, NL.set.bots ? MENU_SKILL[NL.set.skill] : "off", NL.tr->name);
	return 0;
}
static void nui_join(int i) {
	if (i < 0 || i >= NL.nrooms || NL.role != NL_BROWSE) return; NetRoom *R = &NL.rooms[i];
	if (R->build != NL.build) { char l2[96]; snprintf(l2, sizeof(l2), "This room runs build %s, this game is %s. Both players need the same build.", R->ver, NL.ver); net_alert("DIFFERENT BUILD", l2); return; }
	NL.role = NL_JOINING; NL.host = R->addr; NL.room = R->room; NL.join_room = i; NL.nonce = (unsigned)(sceKernelGetProcessTimeWide() * 2246822519u) | 1; NL.join_t = 0; NL.join_tries = 0; NL.host_rx = net_now();
	NL.set = R->set; NL.state = NL.last_state = NR_LOBBY; NL.nslot = 0; NL.countdown = -1; NL.started_gen = -1; NL.my_ready = 0; net_want_team = -1;
	snprintf(NUV.host, sizeof(NUV.host), "%s", R->host); logp("net: joining \"%s\"\n", R->host);
}
static void nui_ready(int on) { NL.my_ready = on; if (NL.role == NL_CLIENT && NL.my_slot < NL.nslot) NL.slot[NL.my_slot].ready = on; NL.send_t = 0; }
static void nui_team(int t) {
	if (NL.role == NL_HOST) { int k = NL.my_slot; if ((k & 1) == t) return; int f = net_free_slot(t); if (f >= 0) { net_move_slot(k, f); NL.lseq++; } return; }
	net_want_team = t; NL.send_t = 0;
}
static void nui_start(void) {
	if (NL.role != NL_HOST || NL.state != NR_LOBBY) return;
	NL.state = NR_STARTING; NL.countdown = 3; NL.seed = (unsigned)(sceKernelGetProcessTimeWide() * 2654435761u); NL.lseq++; NL.send_t = 0;
	logp("net: START (%d players)\n", net_humans(-1));
}
static void nui_leave(void) {
	if (NL.role == NL_HOST) { for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used) net_send_bye(&NL.peer[i].addr, 0); memset(NL.peer, 0, sizeof(NL.peer)); logp("net: room closed\n"); net_kick(); sceKernelDelayThread(20000); if (NL.kind == 2) nat_start(0, 0, 1);   /* (b108: the port is given back) */ net_socket(0); }
	else if (NL.role == NL_CLIENT || NL.role == NL_JOINING) { net_send_bye(&NL.host, 0); logp("net: left the room\n"); }
	NL.role = NL.up == 2 ? NL_BROWSE : NL_OFF; NL.state = NR_LOBBY; NL.nrooms = 0; NL.nslot = 0; NL.disc_t = 0;
}
static int net_match_go;   // 1 once the countdown ended (host) / the host's match began (client): poll_start hands the settings over
static int nui_poll_start(MenuConfig *c) {
	if (!net_match_go) return 0; net_match_go = 0;
	c->mode = NL.set.mode; c->map = NL.set.map; c->team_size = NL.set.team_size; c->bot_skill = NL.set.skill; c->score_limit = NL.set.score_limit; c->time_limit = NL.set.time_limit;
	return 1;
}
static void net_bind_menu(MenuConfig *c) {
	net_cfg = c; NUI.open = nui_open; NUI.close = nui_close; NUI.set_transport = nui_set_transport; NUI.host = nui_host; NUI.join = nui_join; NUI.ready = nui_ready;
	NUI.team = nui_team; NUI.start = nui_start; NUI.leave = nui_leave; NUI.poll_start = nui_poll_start; NUI.code_enter = nui_code_enter; NUV.countdown = -1; NUV.transport = 0;
#ifdef HG_HOST
	NUV.transport = 1;   // (the host build has no ad-hoc radio: LAN, or the PDP shim when HG_NET_PDP=1)
	if (getenv("HG_NET_PDP")) NUV.transport = 0;
#endif
}

// ------------------------------------------------------------------ once a frame (front end and matches)
static void net_view(void) {
	NUV.is_host = NL.role == NL_HOST; NUV.transport = NL.kind;
	NUV.phase = NL.up == 1 ? 1 : NL.up != 2 ? 0 : NL.role == NL_BROWSE ? 2 : NL.role == NL_JOINING ? 3 : NL.role == NL_HOST || NL.role == NL_CLIENT ? (NL.nslot ? 4 : 3) : 0;
	if (NL.up == 1) snprintf(NUV.status, sizeof(NUV.status), "CONNECTING...");
	else if (NL.up != 2) snprintf(NUV.status, sizeof(NUV.status), "OFFLINE");
	else if (NL.role == NL_BROWSE) snprintf(NUV.status, sizeof(NUV.status), "SCANNING  -  %s", NL.tr->name);
	else if (NL.role == NL_JOINING) snprintf(NUV.status, sizeof(NUV.status), "JOINING...");
	else snprintf(NUV.status, sizeof(NUV.status), "%s", NL.tr->name);
	if (NL.kind == 2 && NL.role == NL_HOST) {   // b108: the room code, once the router and the STUN server have answered
		unsigned ip = 0; int port = 0, k = nat_result(&ip, &port, NL.note, sizeof(NL.note)); if (k) nat_code(ip, port, NL.code, sizeof(NL.code));
		static int told; if (k && told != (int)(ip ^ (unsigned)port)) { told = (int)(ip ^ (unsigned)port); logp("net: ROOM CODE %s (%s)\n", NL.code, NL.note); } }
	snprintf(NUV.code, sizeof(NUV.code), "%s", NL.kind == 2 && NL.role == NL_HOST ? NL.code : ""); snprintf(NUV.note, sizeof(NUV.note), "%s", NL.note); snprintf(NUV.code_in, sizeof(NUV.code_in), "%s", NL.kind == 2 ? NL.code_in : "");
	if (NL.up == 2 && NL.role == NL_BROWSE && NL.kind == 2) snprintf(NUV.status, sizeof(NUV.status), NL.code_in[0] ? "SEARCHING  -  INTERNET" : "INTERNET  -  NO ROOM CODE YET");
	NUV.nroom = NL.nrooms;
	for (int i = 0; i < NL.nrooms; i++) { const NetRoom *R = &NL.rooms[i]; NuvRoom *V = &NUV.room[i]; snprintf(V->host, sizeof(V->host), "%s", R->host); V->mode = R->set.mode; V->map = R->set.map; V->team_size = R->set.team_size;
		V->humans = R->humans; V->maxh = R->maxh; V->bots = R->set.bots; V->state = R->state == NR_MATCH ? 2 : R->state == NR_STARTING ? 1 : 0; V->other_build = R->build != NL.build; }
	if (NL.role == NL_HOST || NL.role == NL_CLIENT) {
		NUV.mode = NL.set.mode; NUV.map = NL.set.map; NUV.team_size = NL.set.team_size; NUV.bots = NL.set.bots; NUV.skill = NL.set.skill; NUV.score_limit = NL.set.score_limit; NUV.time_limit = NL.set.time_limit;
		NUV.nslot = NL.nslot; int all = 1;
		for (int k = 0; k < NL.nslot && k < NUV_SLOTS; k++) { const NetSlot *S = &NL.slot[k]; NuvSlot *V = &NUV.slot[k]; V->used = S->used; V->human = S->used; V->ready = S->ready; V->host = S->host; V->connected = S->connected || S->host;
			V->me = S->used && k == NL.my_slot; V->team = k & 1; V->chr = S->used ? S->chr : (k * 5 + 3) % 12; snprintf(V->name, sizeof(V->name), "%s", S->name); if (S->used && !S->ready) all = 0;
			if (S->host) snprintf(NUV.host, sizeof(NUV.host), "%s", S->name); }
		NUV.all_ready = all && NL.state == NR_LOBBY; NUV.countdown = NL.state == NR_STARTING ? fmaxf(0, NL.countdown) : -1;
	} else { NUV.nslot = 0; NUV.countdown = -1; }
}
static void net_send_lstate(void) {
	unsigned char b[64]; NetW w; net_hdr(&w, b, NP_LSTATE, NL.room); int k = NL.my_slot < NL.nslot ? NL.my_slot : 0;
	int ready = NL.my_ready, team = net_want_team >= 0 ? net_want_team : 255; (void)k;
	if (net_want_team >= 0 && (k & 1) == net_want_team) net_want_team = -1;
	nw8(&w, ready); nw8(&w, team); nw8(&w, net_cfg ? net_cfg->chr : 0); nw8(&w, net_cfg ? net_cfg->primary : 0); nw8(&w, net_cfg ? net_cfg->secondary : 8); nw8(&w, NL.state == NR_MATCH ? 1 : 0); net_send(&NL.host, b, w.n);
}
static float net_last_dt;
static void net_frame(float dt) {
	net_last_dt = dt; net_ime_poll();
	if (NL.up == 1) { int r = NL.tr->poll_up(NL.why, sizeof(NL.why));
		if (r < 0) { NL.up = -1; NL.role = NL_OFF; net_alert("AD-HOC UNAVAILABLE", NL.why); }
		else if (r == 0) { NL.up = 2; logp("net: %s connected\n", NL.tr->name); if (net_socket(0) < 0) { NL.up = -1; net_alert("NETWORK ERROR", "The game could not open its network port."); } else { NL.role = NL_BROWSE; NL.disc_t = 0; } } }
	if (NL.up != 2 || NL.role == NL_OFF) { net_view(); return; }
	double now = net_now();
	// received packets
	for (int k = 0; k < NET_RING; k++) { NetPkt *p = ring_rd(&NL.in); if (!p) break; net_rx(&p->a, p->d, p->n); ring_pop(&NL.in); }
	switch (NL.role) {
	case NL_BROWSE: case NL_JOINING:
		if (now - NL.disc_t > 1.0) { NL.disc_t = now; unsigned char b[32]; NetW w; net_hdr(&w, b, NP_DISC, 0); nw32(&w, NL.build); net_send(NULL, b, w.n); }
		for (int i = 0; i < NL.nrooms; ) if (now - NL.rooms[i].seen > 4 && NL.role == NL_BROWSE) { memmove(&NL.rooms[i], &NL.rooms[i + 1], sizeof(NetRoom) * (NL.nrooms - i - 1)); NL.nrooms--; } else i++;
		if (NL.role == NL_JOINING && now - NL.join_t > 0.5) { NL.join_t = now;
			if (NL.join_tries++ >= 12) { net_alert("NO ANSWER", "The room did not answer. It may have closed or be out of range."); NL.role = NL_BROWSE; break; }
			unsigned char b[96]; NetW w; net_hdr(&w, b, NP_JOIN, NL.room); nw32(&w, NL.build); nw32(&w, NL.nonce); nws(&w, NL.name, 20); nw8(&w, net_cfg ? net_cfg->chr : 0); nw8(&w, net_cfg ? net_cfg->primary : 0); nw8(&w, net_cfg ? net_cfg->secondary : 8); net_send(&NL.host, b, w.n); }
		break;
	case NL_HOST: {
		NetSlot *me = &NL.slot[NL.my_slot]; if (net_cfg && NL.state == NR_LOBBY && (me->chr != net_cfg->chr || me->prim != net_cfg->primary || me->sec != net_cfg->secondary)) { me->chr = net_cfg->chr; me->prim = net_cfg->primary; me->sec = net_cfg->secondary; NL.lseq++; }
		for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used && now - NL.peer[i].last_rx > 5) net_drop_peer(i, "timed out");
		if (NL.state == NR_STARTING) { NL.countdown -= dt; if (NL.countdown <= -0.6f) {   /* b59: 0 shows for 0.6 s (user: count to zero and stay) */ NL.state = NR_MATCH; NL.gen = (NL.gen + 1) & 255; NL.countdown = -1; NL.lseq++; net_match_go = 1; NL.start_t = now; logp("net: match %d begins\n", NL.gen); } }
		double every = NL.state == NR_STARTING ? 0.1 : NL.state == NR_MATCH ? 1.0 : 0.25;
		if (now - NL.send_t > every) { NL.send_t = now; net_send_lobby_all(); }
		if (NL.kind == 2 && now - NL.nat_t > 1500 && NAT.state == NAT_DONE && NL.state == NR_LOBBY) { NL.nat_t = now; nat_start(udp_port, udp_ip, 0); }   // (b108: leases renewed between matches)
		break; }
	case NL_CLIENT:
		if (now - NL.host_rx > 4) { net_alert(NL.state == NR_MATCH ? "HOST LEFT" : "CONNECTION LOST", "The connection to the host was lost."); net_send_bye(&NL.host, 1); NL.role = NL_BROWSE; NL.lost = 1; NL.nslot = 0; break; }
		if (NL.state == NR_STARTING && NL.countdown > 0) NL.countdown -= dt;
		if (NL.state == NR_MATCH && NL.gen != NL.started_gen && NL.nslot) { NL.started_gen = NL.gen; net_match_go = 1; logp("net: the host's match %d begins\n", NL.gen); }
		if (now - NL.send_t > (NL.state == NR_MATCH ? 1.0 : 0.25)) { NL.send_t = now; net_send_lstate(); }
		break;
	}
	// statistics: per peer, once a second
	if (now - NL.stat_t >= 1) { NL.stat_t = now;
		for (int i = 0; i < NET_MAXPEER; i++) { NetPeer *P = &NL.peer[i]; P->s_rx_b = P->rx_b; P->s_tx_b = P->tx_b; P->s_rx_p = P->rx_p; P->s_tx_p = P->tx_p; P->rx_b = P->tx_b = P->rx_p = P->tx_p = 0; }
		NL.s_rx_b = NL.tot_rx_b; NL.s_tx_b = NL.tot_tx_b; NL.tot_rx_b = NL.tot_tx_b = 0; }
	net_kick();
	net_view();
}
static int net_on(void) { return (NL.role == NL_HOST || NL.role == NL_CLIENT) && NL.state == NR_MATCH; }
