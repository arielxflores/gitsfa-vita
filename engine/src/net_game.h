// net (stage 2): the match over the network (NET.md sections 4-5). Host-authoritative: the host runs bots, damage, scores and
// the match flow; a remote player is a bot slot with netc = 1 that the host moves with the player's commands. Every other
// machine draws the other actors as puppets (netp = 1) from 30 Hz snapshots, 100 ms behind the host's clock, and predicts its
// own movement (replaying its commands over the host's state when they differ). Shots are judged on the host against the actors
// rewound to the time the shooter saw (lag compensation). Included by main.c after the skills / tanks glue (SKB, feed_add ...).
//   Actors: the room's slots that play (net_lobby.h: humans + the bots that fill), in slot order. Locally actor `me` is the
//   player and bots[j] is actor (j < me ? j : j + 1); teams are local (0 = mine), absolute in the packets (0 GHOST A).
#pragma once
#define NG_SNAP_DT 0.0333f       // snapshots and inputs: 30 a second
#define NG_INTERP_MS 100.0       // puppets are drawn this far behind the newest host time
#define NG_MAXA NET_MAXSLOT
#define NG_HDR 160          // match header; bytes 16.. hold the round mode's state (ng_mode_pack)
#define NG_MODE_OFS 16
#define NG_AREC 42
#define NG_SKAR 34          // per actor
#define NG_SKOR 52          // per deployable
#define NG_SKMR 32          // per missile
#define NG_SKB (NG_MAXA * NG_SKAR + SK_MAXOBJ * NG_SKOR + SK_MAXMS * NG_SKMR)
#define NG_TKR 60          // per Think Tank
#define NG_TGR 28          // per tank grenade
#define NG_TKB (TK_MAX * NG_TKR + 6 * NG_TGR)
#define NG_STATE_MAX (NG_HDR + NG_MAXA * NG_AREC + NG_SKB + NG_TKB + (int)sizeof(Player) + 16)
#define NG_NCMD 64
#define NG_NBASE 32
#define NG_NEV 64
#define NG_EVMAX 72
enum { NGE_SHOT = 1, NGE_GREN, NGE_MELEE, NGE_TKCALL, NGE_FEED = 16, NGE_HIT, NGE_DMG, NGE_GRENV, NGE_LEFT, NGE_MODE };
typedef struct { unsigned short seq, dt; unsigned char btn, spd, wpn, rel; signed char mx, my; unsigned short yaw; short pitch; } NgCmd;   // dt: 10 us units; btn: 1 sprint 2 jump 4 crouch 8 aim 16 trigger
typedef struct { float x, y, z, vx, vy, vz, yaw, pitch; int hp, ap, flags, flags2, wpn, chr, team, shots, hurts, reloads, acts, act_kind, spawns, death, kills, deaths, score, mantle_k; } NgA;
enum { NAF_ALIVE = 1, NAF_GROUND = 2, NAF_CROUCH = 4, NAF_SPRINT = 8, NAF_SLIDE = 16, NAF_MANTLE = 32, NAF_ADS = 64, NAF_HUMAN = 128 };
typedef struct { unsigned short seq; short len; unsigned char d[NG_STATE_MAX]; } NgBase;
typedef struct { unsigned short seq; unsigned char len; unsigned char d[NG_EVMAX]; } NgEv;
typedef struct { NgEv q[NG_NEV]; int n; unsigned short next, acked; } NgEvQ;   // a reliable stream: resent until acked, in order
static int net_respawn_now;
static int ng_sk_eats(int l); static void ng_sk_watch(void); static int ng_lid_in(int v); static int ng_q8(float v); static int ng_lid_to_actor(int l); static int ng_act_to_lid(int a); static void ng_tk_pack(unsigned char *b);
static int ng_use_held, ng_skill_btn, ng_skill_fire;   // client: Square held (plant / defuse / hack / SkillSync), the skill button (1 pressed this frame, 2 held)   // client: the host respawned us (main.c's respawn takes the position the net code set)
static void ng_mode_pack(unsigned char *b); static void ng_mode_unpack(const unsigned char *b); static unsigned ng_mode_flip_ev(int m, unsigned bits); static unsigned ng_mode_pend[4];
static int ng_seq_gt(unsigned short a, unsigned short b) { return (short)(a - b) > 0; }
static int ngq_push(NgEvQ *Q, const unsigned char *d, int n) {
	if (n > NG_EVMAX || Q->n >= NG_NEV) return -1; NgEv *e = &Q->q[Q->n++]; e->seq = ++Q->next; e->len = (unsigned char)n; memcpy(e->d, d, n); return 0;
}
static void ngq_ack(NgEvQ *Q, unsigned short ack) { int k = 0; while (k < Q->n && !ng_seq_gt(Q->q[k].seq, ack)) k++; if (k) { memmove(Q->q, Q->q + k, sizeof(NgEv) * (Q->n - k)); Q->n -= k; } Q->acked = ack; }
static void ngq_write(NetW *w, const NgEvQ *Q, int budget) {   // u8 count, then (u16 seq, u8 len, bytes) while they fit
	int at = w->n, cnt = 0; nw8(w, 0);
	for (int k = 0; k < Q->n && cnt < 255; k++) { const NgEv *e = &Q->q[k]; if (w->n + 3 + e->len > budget) break; nw16(w, e->seq); nw8(w, e->len); nwb(w, e->d, e->len); cnt++; }
	if (at < w->cap) w->p[at] = (unsigned char)cnt;
}
// ---- the state buffer: XOR with the receiver's acked copy, zero runs coded (0x00..0x7F: 1..128 zero bytes; 0x80..0xFF: 1..128 literals)
static int ng_rle(const unsigned char *a, const unsigned char *base, int n, unsigned char *out, int cap) {
	int o = 0, i = 0;
	while (i < n) {
		int z = 0; while (i + z < n && z < 128 && (a[i + z] ^ (base ? base[i + z] : 0)) == 0) z++;
		if (z) { if (o + 1 > cap) return -1; out[o++] = (unsigned char)(z - 1); i += z; continue; }
		int l = 0; while (i + l < n && l < 128 && (a[i + l] ^ (base ? base[i + l] : 0)) != 0) l++;
		if (o + 1 + l > cap) return -1; out[o++] = (unsigned char)(0x80 | (l - 1)); for (int k = 0; k < l; k++) out[o++] = a[i + k] ^ (base ? base[i + k] : 0); i += l;
	}
	return o;
}
static int ng_unrle(const unsigned char *in, int n, const unsigned char *base, int blen, unsigned char *out, int len) {
	int o = 0, i = 0;
	while (i < n && o < len) { int t = in[i++];
		if (t < 0x80) { int z = t + 1; for (int k = 0; k < z && o < len; k++, o++) out[o] = base && o < blen ? base[o] : 0; }
		else { int l = (t & 0x7F) + 1; if (i + l > n) return -1; for (int k = 0; k < l && o < len; k++, o++) out[o] = in[i + k] ^ (base && o < blen ? base[o] : 0); i += l; } }
	return o == len ? 0 : -1;
}
static void ng_put16(unsigned char *p, int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void ng_put32(unsigned char *p, int v) { ng_put16(p, v); ng_put16(p + 2, v >> 16); }
static int ng_get16(const unsigned char *p) { return (short)(p[0] | p[1] << 8); }
static unsigned ng_getu16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int ng_get32(const unsigned char *p) { return (int)(p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24); }
static int ng_clamp16(float v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int)lrintf(v); }
static void ng_pack(unsigned char *p, const NgA *a) {
	ng_put32(p, (int)lrintf(a->x * 8)); ng_put32(p + 4, (int)lrintf(a->y * 8)); ng_put32(p + 8, (int)lrintf(a->z * 8));
	ng_put16(p + 12, ng_clamp16(a->vx)); ng_put16(p + 14, ng_clamp16(a->vy)); ng_put16(p + 16, ng_clamp16(a->vz));
	float y = fmodf(a->yaw, 6.2831853f); if (y < 0) y += 6.2831853f; ng_put16(p + 18, (int)(y * (65536 / 6.2831853f)) & 0xFFFF); ng_put16(p + 20, ng_clamp16(a->pitch * 10000));
	int v[16] = { a->hp, a->ap, a->flags, a->flags2, a->wpn, a->chr, a->team, a->shots, a->hurts, a->reloads, a->acts, a->act_kind, a->spawns, a->death, a->kills, a->deaths };
	for (int k = 0; k < 16; k++) p[22 + k] = (unsigned char)(v[k] < 0 ? 0 : v[k]);
	ng_put16(p + 38, a->score > 65535 ? 65535 : a->score < 0 ? 0 : a->score); p[40] = (unsigned char)a->mantle_k; p[41] = 0;
}
static void ng_unpack(const unsigned char *p, NgA *a) {
	a->x = ng_get32(p) / 8.f; a->y = ng_get32(p + 4) / 8.f; a->z = ng_get32(p + 8) / 8.f; a->vx = ng_get16(p + 12); a->vy = ng_get16(p + 14); a->vz = ng_get16(p + 16);
	a->yaw = ng_getu16(p + 18) * (6.2831853f / 65536); a->pitch = ng_get16(p + 20) / 10000.f;
	int *v[16] = { &a->hp, &a->ap, &a->flags, &a->flags2, &a->wpn, &a->chr, &a->team, &a->shots, &a->hurts, &a->reloads, &a->acts, &a->act_kind, &a->spawns, &a->death, &a->kills, &a->deaths };
	for (int k = 0; k < 16; k++) *v[k] = p[22 + k];
	a->score = ng_getu16(p + 38); a->mantle_k = p[40];
}
// ---- the actors of this match (both sides)
static struct { int on, host, nact, me, my_team, slot[NG_MAXA], human[NG_MAXA]; char name[NG_MAXA][20]; double t0; } NG;
static int ng_local(int a) { return a == NG.me ? -1 : a < NG.me ? a : a - 1; }   // actor -> bots[] index (-1: the player)
static int ng_actor_of_bot(int j) { return j < NG.me ? j : j + 1; }
static int ng_abs_team(int a) { return NG.slot[a] & 1; }
static double ng_ms(void) { return (net_now() - NG.t0) * 1000.0; }   // host: ms since the match began
// main()'s locals the match code needs (SKB holds the rest)
static struct { float *match_t, *match_over_t, *pdead; int *end_result, *m_pdeaths, *reloads, *shots, *pdeaths; } NGB;
static void net_game_bind(float *match_t, float *match_over_t, int *end_result, int *m_pdeaths, int *reloads, int *shots, int *pdeaths) {
	NGB.match_t = match_t; NGB.match_over_t = match_over_t; NGB.end_result = end_result; NGB.m_pdeaths = m_pdeaths; NGB.reloads = reloads; NGB.shots = shots; NGB.pdeaths = pdeaths; }
static int net_host_on(void) { return NG.on && NG.host; }
static int net_client_on(void) { return NG.on && !NG.host; }
static int ng_bot_wpn(const char *sh) { for (int w = 0; w < NBOT_WPN; w++) if (!strcmp(BOT_WPN[w].sh, sh)) return w; return 0; }
static int ng_my_wpn(void) { if (!vm_ok || wsl[wcur].def < 0) return 0; int d = WP.quick_from >= 0 && wsl[WP.quick_from].def >= 0 ? wsl[WP.quick_from].def : wsl[wcur].def; return ng_bot_wpn(wdef[d].sh); }
static int ng_ct_of_op(int op) { if (op < 0 || op >= 12) return 0; for (int i = 0; i < nchrt; i++) if (!strcmp(chrt[i].name, MENU_CHR[op].stem)) return i; return 0; }
static void ng_set_wpn(Bot *b, int w) {
	if (w < 0 || w >= NBOT_WPN || w == b->wpn) return; b->wpn = w; const CBank *B = &chr_bank[chrt[b->ct].female]; int set = cb_find_set(B, BOT_WPN[w].sh);
	if (set >= 0) b->ca.set = set; if (b->state != BS_DEAD) ca_upper(&b->ca, S_SWAPEND, 0.05f, 0.2f, 1);
}
// ---- statistics (the "net:" line)
static struct { uint64_t us; int frames, snaps, snap_bytes, inputs, corr, replays, ev_in, ev_out, shots_in, hits; float corr_max; } NGS;
#define NG_T0() uint64_t ng_t0_ = sceKernelGetSystemTimeWide()
#define NG_T1() (NGS.us += sceKernelGetSystemTimeWide() - ng_t0_)

// =========================================================== host
typedef struct {
	int on, peer, actor, bi; NgCmd q[32]; int qn; unsigned short cmd_last; int have_cmd, loaded, rel_last;
	NgBase base[NG_NBASE]; unsigned short snap_seq, snap_ack; int have_ack; NgEvQ out; unsigned short in_last;
	unsigned echo; double echo_at; float shot_cd, regen; int wpn_last; unsigned char rel_cnt, btn_last; double last_in;
} NgCli;
static NgCli ngc[NET_MAXPEER];
typedef struct { double ms; struct { V3 head, chest, low; int alive, team; } a[NG_MAXA]; } NgHist;
static NgHist ng_hist[64]; static int ng_hn, ng_hw;
static double ng_snap_t; static unsigned short ng_snap_seq; static int ng_waiting; static double ng_wait_t;
static int ng_pend_k = -1, ng_pend_v = -1, ng_pend_head;   // the slots of the kill feed_add is about to show (host)
static void ng_bcast_ev(const unsigned char *d, int n) { for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on) ngq_push(&ngc[i].out, d, n); }
static NgCli *ng_cli_of_actor(int a) { for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on && ngc[i].actor == a) return &ngc[i]; return NULL; }
static int ng_actor_of_slot(int k) { for (int a = 0; a < NG.nact; a++) if (NG.slot[a] == k) return a; return -1; }
static V3 ng_actor_pos(int a) { int j = ng_local(a); return j < 0 ? SKB.pl->pos : bots[j].pl.pos; }
static void ng_hist_record(void) {   // after bots_update: every actor's hit shapes now (lag compensation)
	NgHist *H = &ng_hist[ng_hw]; ng_hw = (ng_hw + 1) & 63; if (ng_hn < 64) ng_hn++; H->ms = ng_ms();
	for (int a = 0; a < NG.nact; a++) { int j = ng_local(a);
		if (j < 0) { V3 p = SKB.pl->pos; float e = SKB.pl->eye > 1 ? SKB.pl->eye : P_EYE; H->a[a].head = v3(p.x, p.y, p.z + e + 6); H->a[a].chest = v3(p.x, p.y, p.z + e * 0.72f); H->a[a].low = v3(p.x, p.y, p.z + 22);
			H->a[a].alive = *SKB.pdead <= 0 && *SKB.php > 0; }
		else { Bot *b = &bots[j]; H->a[a].head = b->head; H->a[a].chest = b->chest; H->a[a].low = v3(b->pelvis.x, b->pelvis.y, b->pl.pos.z + 22); H->a[a].alive = b->state != BS_DEAD && b->pose; }
		H->a[a].team = ng_abs_team(a); }
}
static void ng_hist_at(double ms, int a, V3 *head, V3 *chest, V3 *low, int *alive) {   // actor a's shapes at host time ms (linear between frames)
	int best = -1, next = -1; double bm = -1e18, nm = 1e18;
	for (int k = 0; k < ng_hn; k++) { double t = ng_hist[k].ms; if (t <= ms && t > bm) { bm = t; best = k; } if (t > ms && t < nm) { nm = t; next = k; } }
	if (best < 0) best = next; if (best < 0) { *alive = 0; return; }
	const NgHist *A = &ng_hist[best], *B = next >= 0 ? &ng_hist[next] : A; float f = next >= 0 && nm > bm ? (float)((ms - bm) / (nm - bm)) : 0;
	if (!A->a[a].alive || !B->a[a].alive) f = 0;
	#define NG_L(F) v3(A->a[a].F.x + (B->a[a].F.x - A->a[a].F.x) * f, A->a[a].F.y + (B->a[a].F.y - A->a[a].F.y) * f, A->a[a].F.z + (B->a[a].F.z - A->a[a].F.z) * f)
	*head = NG_L(head); *chest = NG_L(chest); *low = NG_L(low); *alive = A->a[a].alive;
	#undef NG_L
}
// a remote actor's hit lands on actor v (host): damage, the kill feed, the shooter's confirmation, the victim's damage report
static void ng_hurt(int shooter, int v, float dmg, int head, V3 dir, int icon) {
	int sj = ng_local(shooter), vj = ng_local(v); if (sj < 0) return; Bot *sb = &bots[sj]; int killed = 0;
	ng_pend_k = shooter; ng_pend_v = v; ng_pend_head = head;
	if (vj < 0) {   // the host's player: main's damage path (armour, death, feed)
		if (*SKB.pdead > 0) return; self_dmg += dmg; self_dmg_dir = atan2f(sb->pl.pos.y - SKB.pl->pos.y, sb->pl.pos.x - SKB.pl->pos.x); bot_last_hitter = sj;
		killed = *SKB.php - dmg <= 0;   // (a guess for the hit marker: armour may save it)
	} else { if (bots[vj].state == BS_DEAD) return; bot_hit_info(WICON_MELEE(icon) || icon == 41 ? icon : -1, dir); killed = bot_hurt(vj, dmg, head, sj); }
	NGS.hits++;
	NgCli *C = ng_cli_of_actor(shooter); if (C) { unsigned char e[8] = { NGE_HIT, (unsigned char)v, (unsigned char)(head | (killed ? 2 : 0)) }; ngq_push(&C->out, e, 3); }
	NgCli *V = ng_cli_of_actor(v); if (V) { unsigned char e[12]; e[0] = NGE_DMG; e[1] = (unsigned char)shooter; ng_put16(e + 2, (int)(dmg * 10)); float ang = atan2f(sb->pl.pos.y - bots[vj].pl.pos.y, sb->pl.pos.x - bots[vj].pl.pos.x);
		ng_put16(e + 4, (int)(ang * 10000)); ngq_push(&V->out, e, 6); }
	ng_pend_k = ng_pend_v = -1;
}
// the shapes test of bots_hit_test against every actor not on the shooter's team, at host time ms
static int ng_trace(int shooter, V3 eye, V3 dir, float maxd, double ms, float *t, int *head) {
	int hit = -1; float bt = maxd; int team = ng_abs_team(shooter);
	for (int a = 0; a < NG.nact; a++) { if (a == shooter || ng_abs_team(a) == team) continue;
		V3 hd, ch, lo; int al; ng_hist_at(ms, a, &hd, &ch, &lo, &al); if (!al) continue;
		V3 c = vsub(ch, eye); if (vdot(c, dir) < -100 || vdot(c, c) > (bt + 200) * (bt + 200)) continue;
		float th = ray_sphere(eye, dir, hd, 15), tb = ray_capsule(eye, dir, lo, ch, 26), tt = -1; int hh = 0;
		if (th >= 0 && (tb < 0 || th <= tb + 10)) { tt = th; hh = 1; } else if (tb >= 0) tt = tb;
		if (tt >= 0 && tt < bt) { bt = tt; hit = a; *head = hh; } }
	if (hit >= 0) *t = bt; return hit;
}
// b108: an internet room's packets come from anyone: a position a remote player sends is used only when it is a number near where the host has them
static int ng_near(V3 p, V3 c, float d) { V3 e = vsub(p, c); return vdot(e, e) <= d * d; }   // (false for NaN / infinity too)
static V3 ng_eye_of(const Bot *b) { return v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + (b->pl.eye > 1 ? b->pl.eye : P_EYE)); }
static void ng_host_shot(NgCli *C, NetR *r) {
	double rt = nr32(r); int w = nr8(r), np = nr8(r); V3 eye; eye.x = nrf(r); eye.y = nrf(r); eye.z = nrf(r); if (r->bad || np < 1 || np > 12 || w >= nwdef) return;
	Bot *b = &bots[C->bi]; if (b->state == BS_DEAD) return; const WDef *D = &wdef[w]; NGS.shots_in++;
	V3 he = ng_eye_of(b); if (!ng_near(eye, he, 250)) eye = he;   // (an eye far from where the host has them: theirs is wrong)
	double now = ng_ms(); if (rt > now) rt = now; if (rt < now - 400) rt = now - 400;
	b->shots++; b->flash_t = 0.05f; { const BotWpn *W = &BOT_WPN[b->wpn]; if (W->kind) ca_play(&b->ca.fire, ca_c(&b->ca, S_FIRE), 0.03f, 0.15f, CA_UPPER, 1); else ca_fire(&b->ca); snd_wfire_tp(W->sh, &b->muzzle.x); }
	for (int k = 0; k < np; k++) {
		float yaw = nr16(r) * (6.2831853f / 65536), pitch = (short)nr16(r) / 10000.f; if (r->bad) return;
		V3 dir = v3(cosf(yaw) * cosf(pitch), sinf(yaw) * cosf(pitch), sinf(pitch)); float dist = raycast(eye, dir, 15000, NULL, NULL), t = 0; int head = 0;
		int v = ng_trace(C->actor, eye, dir, dist > 0 ? dist : 15000, rt, &t, &head), l = 1 + C->bi; float dmg = head ? D->dmg_h : D->dmg;
		{ float lim = v >= 0 ? t : dist > 0 ? dist : 15000, sb = sk_bullet(eye, dir, lim, l, dmg); if (sb < lim) { v = -1; dist = sb; } }   // an enemy shield / cyber wall / deployable (skills.h)
		{ float lim = v >= 0 ? t : dist > 0 ? dist : 15000, tt = 0; int ti = ttm_on || tk_count(0) || tk_count(1) ? tk_ray_team(eye, dir, lim, &tt, tk_actor_team(l)) : -1;   // an enemy Think Tank
		  if (ti >= 0) { V3 hp = vadd(eye, vmul(dir, tt)); tk_damage(ti, dmg, l, hp, dir, SKD_BULLET); v = -1; dist = tt; } }   // (the client shows its own hit on the tank)
		if (k == 0) { V3 end = vadd(eye, vmul(dir, v >= 0 ? t : dist > 0 ? dist : 8000)); if (bot_on_shot) bot_on_shot(b->muzzle, end); }
		if (v >= 0) ng_hurt(C->actor, v, dmg, head, dir, D->icon);
	}
}
static void ng_host_melee(NgCli *C, NetR *r) {
	int kind = nr8(r), tgt = nr8(r); V3 eye; eye.x = nrf(r); eye.y = nrf(r); eye.z = nrf(r); float yaw = nr16(r) * (6.2831853f / 65536), pitch = (short)nr16(r) / 10000.f; if (r->bad) return;
	Bot *b = &bots[C->bi]; if (b->state == BS_DEAD) return; int kd = wsl[WS_KNIFE].def >= 0 ? wsl[WS_KNIFE].def : wdef_find("knife");
	{ V3 he = ng_eye_of(b); if (!ng_near(eye, he, 250)) eye = he; }   // (b108: as the shots)
	float dmg = kd >= 0 ? wdef[kd].dmg : 55; int icon = kd >= 0 ? wdef[kd].icon : 51;
	if (kind == 1) {   // stealth kill from behind: the target must be within reach
		if (tgt < NG.nact && tgt != C->actor && ng_abs_team(tgt) != ng_abs_team(C->actor)) { V3 q = vsub(ng_actor_pos(tgt), b->pl.pos); if (vdot(q, q) < 320.f * 320.f) ng_hurt(C->actor, tgt, 999, 0, v3(cosf(yaw), sinf(yaw), 0), 51); }
		return; }
	b->acts++; b->act_kind = 1; { float L = ca_play_from(&b->ca, &b->ca.up, "knife", S_FIRE, 0.06f, 0.15f, CA_UPPER); if (L <= 0) L = 0.5f; b->melee_t = 0; b->melee_hit = 1; b->knife_t = L; b->wpn_hide = L; ca_stop(&b->ca.fire); ev_play("weapon/knife/swing_tp", &b->pl.pos.x); }
	double now = ng_ms();
	for (int k = 0; k < 3; k++) { float yy = yaw + (k == 0 ? 0 : k == 1 ? 0.3f : -0.3f); V3 dir = v3(cosf(yy) * cosf(pitch), sinf(yy) * cosf(pitch), sinf(pitch)); float t; int head = 0;
		int v = ng_trace(C->actor, eye, dir, 220, now - 100, &t, &head); if (v >= 0) { ng_hurt(C->actor, v, dmg, head, dir, icon); break; } }
}
static void ng_gren_spawn(int owner, const float *p0, const float *v0, float fuse) {   // a grenade in gren[] (owner >= 0: an actor's, not ours)
	int gi = -1; for (int k = 0; k < NGREN; k++) if (!gren[k].on) { gi = k; break; } if (gi < 0) return;
	Grenade *g = &gren[gi]; memset(g, 0, sizeof(*g)); g->on = 1; memcpy(g->p, p0, 12); memcpy(g->v, v0, 12); g->fuse = fuse; g->spin = 9; g->net_owner = owner + 1;
}
static void ng_host_gren(NgCli *C, NetR *r) {
	float p0[3], v0[3], fuse; for (int k = 0; k < 3; k++) p0[k] = nrf(r); for (int k = 0; k < 3; k++) v0[k] = nrf(r); fuse = nrf(r); if (r->bad || !(fuse >= 0.3f && fuse <= 4)) return;
	Bot *b = &bots[C->bi]; if (b->state == BS_DEAD) return;
	if (!ng_near(v3(p0[0], p0[1], p0[2]), ng_eye_of(b), 400) || !ng_near(v3(v0[0], v0[1], v0[2]), v3(0, 0, 0), 6000)) return;   // (b108: thrown from where they stand, at a throw's speed)
	b->acts++; b->act_kind = 2; bot_anim_throw(C->bi);
	ng_gren_spawn(C->actor, p0, v0, fuse);
	unsigned char e[40]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_GRENV); nw8(&w, C->actor); for (int k = 0; k < 3; k++) nwf(&w, p0[k]); for (int k = 0; k < 3; k++) nwf(&w, v0[k]); nwf(&w, fuse);
	for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on && &ngc[i] != C) ngq_push(&ngc[i].out, e, w.n);
}
// a grenade of actor `owner` went off at c (host): damage to its enemies and itself, with the owner's credit
static void net_host_blast(int owner, V3 c, float R) {
	int team = ng_abs_team(owner); { int l = ng_act_to_lid(owner); if (l >= 0) { sk_blast(c, 650, 160, l); tk_blast(c, 650, 160, l); } }   // deployables, Think Tanks
	for (int a = 0; a < NG.nact; a++) { if (a != owner && ng_abs_team(a) == team) continue; int j = ng_local(a);
		V3 pc = j < 0 ? v3(SKB.pl->pos.x, SKB.pl->pos.y, SKB.pl->pos.z + 100) : bots[j].chest; if (j >= 0 && bots[j].state == BS_DEAD) continue; if (j < 0 && *SKB.pdead > 0) continue;
		V3 e = vsub(pc, c); float d = sqrtf(vdot(e, e)); if (d > R || !bot_los(c, pc)) continue;
		float dmg = a == owner ? 120 * powf(1 - d / R, 1.2f) : 160 * powf(1 - d / R, 0.9f) + 10;
		if (a == owner) { if (j >= 0) { bot_hit_info(41, e); bot_hurt(j, dmg, 0, -2); } continue; }
		ng_hurt(owner, a, dmg, 0, vmul(e, 1 / (d + 1)), 41); }
}
static void ng_host_tkcall(NgCli *C, NetR *r);
static void ng_host_rx(int peer, const unsigned char *d, int n) {
	NgCli *C = &ngc[peer]; if (!C->on || n < 8) return; NetR r = { d, n, 0, 0 };
	unsigned echo = nr32(&r); unsigned short sack = (unsigned short)nr16(&r), rack = (unsigned short)nr16(&r); int nc = nr8(&r); if (r.bad || nc > 8) return;
	C->echo = echo; C->echo_at = net_now(); C->last_in = net_now(); if (!C->loaded) { C->loaded = 1; logp("net: %s is in the match\n", NL.slot[NG.slot[C->actor]].name); }
	if (!C->have_ack || ng_seq_gt(sack, C->snap_ack)) { C->snap_ack = sack; C->have_ack = 1; }
	ngq_ack(&C->out, rack); NGS.inputs++;
	for (int k = 0; k < nc; k++) { NgCmd m; m.seq = (unsigned short)nr16(&r); m.dt = (unsigned short)nr16(&r); m.mx = (signed char)nr8(&r); m.my = (signed char)nr8(&r); m.btn = (unsigned char)nr8(&r); m.yaw = (unsigned short)nr16(&r);
		m.pitch = (short)nr16(&r); m.spd = (unsigned char)nr8(&r); m.wpn = (unsigned char)nr8(&r); m.rel = (unsigned char)nr8(&r); if (r.bad) return;
		if (C->have_cmd && !ng_seq_gt(m.seq, C->cmd_last)) continue; int dup = 0; for (int q = 0; q < C->qn; q++) if (C->q[q].seq == m.seq) dup = 1; if (dup) continue;
		if (C->qn < 32) { int at = C->qn; while (at > 0 && ng_seq_gt(C->q[at - 1].seq, m.seq)) { C->q[at] = C->q[at - 1]; at--; } C->q[at] = m; C->qn++; } }
	int ne = nr8(&r); for (int k = 0; k < ne; k++) { unsigned short seq = (unsigned short)nr16(&r); int len = nr8(&r); if (r.bad || len < 1 || r.i + len > r.n) return;
		NetR e = { r.p + r.i, len, 0, 0 }; r.i += len; if (seq != (unsigned short)(C->in_last + 1)) continue; C->in_last = seq; NGS.ev_in++;
		int t = nr8(&e); if (t == NGE_SHOT) ng_host_shot(C, &e); else if (t == NGE_MELEE) ng_host_melee(C, &e); else if (t == NGE_GREN) ng_host_gren(C, &e); else if (t == NGE_TKCALL) ng_host_tkcall(C, &e); }
}
// before bots_update: every remote player's commands (their movement), regeneration, the waiting room before the start
static void net_host_pre(float dt) {
	if (!net_host_on()) return; NG_T0();
	sk_manual = 0; for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on && 1 + ngc[i].bi < SK_MAXACT) sk_manual |= 1u << (1 + ngc[i].bi);   // remote players: no bot brain for their skills
	for (int i = 0; i < NET_MAXPEER; i++) { NgCli *C = &ngc[i]; if (!C->on) continue; Bot *b = &bots[C->bi]; int ev = 0, ran = 0;
		while (C->qn > 0 && ran < 6) { NgCmd m = C->q[0]; memmove(C->q, C->q + 1, sizeof(NgCmd) * --C->qn);
			C->cmd_last = m.seq; C->have_cmd = 1; ran++; int sfire = m.wpn & 0x80; m.wpn &= 0x7F; unsigned char pb = C->btn_last; C->btn_last = m.btn;
			if (m.wpn != C->wpn_last) { C->wpn_last = m.wpn; ng_set_wpn(b, m.wpn); }
			if (m.rel != C->rel_cnt) { C->rel_cnt = m.rel; if (b->state != BS_DEAD) { b->reloads++; const BotWpn *W = &BOT_WPN[b->wpn]; if (W->kind == 1) ca_reload_shells(&b->ca, 4); else ca_reload(&b->ca, 0); } }
			if (b->state == BS_DEAD || ng_waiting || bot_freeze) continue;
			b->pl.yaw = m.yaw * (6.2831853f / 65536); b->aim_pitch = m.pitch / 10000.f; b->pl.in_crouch = (m.btn & 4) != 0; b->pl.in_ads = (m.btn & 8) != 0; b->net_ads = (m.btn & 8) != 0;
			b->pl.speed_mul = m.spd / 64.f; b->pl.local_floor = 1; b->net_use = (m.btn & 32) != 0; b->net_skill = m.btn >> 6;
			player_update(&b->pl, m.mx / 127.f, m.my / 127.f, (m.btn & 1) != 0, (m.btn & 2) != 0, m.dt / 100000.f); ev |= b->pl.events;
			{ int l = 1 + C->bi; if (sk_valid(l)) {   // the operator skill: D-pad right (pressed / held), the trigger while it aims, Square for SkillSync
				sk_button(l, (m.btn >> 6) & 1, (m.btn >> 7) & 1, m.dt / 100000.f); if (sfire && ng_sk_eats(l)) sk_fire(l); if ((m.btn & 32) && !(pb & 32)) sk_sync_try(l); } } }
		if (C->qn > 12) C->qn = 0;   // (a burst after a stall: drop the backlog rather than run it all)
		b->pl.events = ev;
		if (b->state != BS_DEAD) { if (b->regen_cd > 0) b->regen_cd -= dt; else if (b->hp < 100) { b->hp += 15 * dt; if (b->hp > 100) b->hp = 100; } }
		if (C->shot_cd > 0) C->shot_cd -= dt; }
	if (ng_waiting) {   // the match starts when every player has loaded (or after 15 s)
		int all = 1; for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on && !ngc[i].loaded) all = 0;
		if (all || net_now() - ng_wait_t > 15) { ng_waiting = 0; bot_freeze = 0; NG.t0 = net_now() - ng_ms() / 1000.0; logp("net: everyone is in: the match runs (%.1f s wait)\n", net_now() - ng_wait_t); }
		else { bot_freeze = 1; if (NGB.match_t) *NGB.match_t += dt; *SKB.notify = "WAITING FOR PLAYERS"; *SKB.notify_t = 0; *SKB.notify_pts = 0; } }
	NG_T1();
}
// one actor's record (host)
static void ng_actor_rec(int a, NgA *o) {
	memset(o, 0, sizeof(*o)); int j = ng_local(a); o->team = ng_abs_team(a);
	if (j < 0) { const Player *p = SKB.pl; o->x = p->pos.x; o->y = p->pos.y; o->z = p->pos.z; o->vx = p->vel.x; o->vy = p->vel.y; o->vz = p->vel.z; o->yaw = p->yaw; o->pitch = *SKB.pitch;
		o->hp = (int)(*SKB.php > 0 ? *SKB.php + 0.5f : 0); o->ap = (int)(*SKB.pap > 0 ? *SKB.pap + 0.5f : 0); int alive = *SKB.pdead <= 0 && *SKB.php > 0;
		o->flags = (alive ? NAF_ALIVE : 0) | (p->grounded ? NAF_GROUND : 0) | (p->crouched && p->slide_t <= 0 ? NAF_CROUCH : 0) | (p->sprinting ? NAF_SPRINT : 0) | (p->slide_t > 0 ? NAF_SLIDE : 0) | (p->mantle_t > 0 ? NAF_MANTLE : 0) | (p->in_ads ? NAF_ADS : 0) | NAF_HUMAN;
		o->flags2 = p->mantle_kind & 3; o->mantle_k = p->mantle_t > 0 ? (int)(255 * (1 - p->mantle_t / (p->mantle_len > 0 ? p->mantle_len : 1))) : 0;
		o->wpn = ng_my_wpn(); o->chr = cfg.chr; o->shots = *NGB.shots & 255; o->reloads = *NGB.reloads & 255; o->kills = *SKB.m_pkills; o->deaths = *NGB.m_pdeaths; o->score = *SKB.m_pkills * 100 + *SKB.intel / 2;
		static int sp_; static float pd_; if (pd_ > 0 && *SKB.pdead <= 0) sp_++; pd_ = *SKB.pdead; o->spawns = sp_ & 255;
		static int ac_, ak_, gs_, ks_; if (WP.gren_thrown && !gs_) { ac_++; ak_ = 2; } gs_ = WP.gren_thrown; { int kn = wcur == WS_KNIFE && WP.quick_from >= 0; if (kn && !ks_) { ac_++; ak_ = 1; } ks_ = kn; } o->acts = ac_ & 255; o->act_kind = ak_;
		return; }
	const Bot *b = &bots[j]; const Player *p = &b->pl;
	o->x = p->pos.x; o->y = p->pos.y; o->z = p->pos.z; o->vx = p->vel.x; o->vy = p->vel.y; o->vz = p->vel.z; o->yaw = p->yaw; o->pitch = b->aim_pitch;
	o->hp = (int)(b->hp > 0 ? b->hp + 0.5f : 0); o->ap = (int)(b->ap > 0 ? b->ap + 0.5f : 0);
	int ads = b->netc ? b->net_ads : b->state == BS_ENGAGE && b->sees;
	o->flags = (b->state != BS_DEAD ? NAF_ALIVE : 0) | (p->grounded || p->mantle_t > 0 ? NAF_GROUND : 0) | (p->crouched && p->slide_t <= 0 ? NAF_CROUCH : 0) | (p->sprinting ? NAF_SPRINT : 0) | (p->slide_t > 0 ? NAF_SLIDE : 0) | (p->mantle_t > 0 ? NAF_MANTLE : 0) | (ads ? NAF_ADS : 0) | (b->netc ? NAF_HUMAN : 0);
	o->flags2 = (p->mantle_kind & 3) | (b->hold ? 16 | (b->hold_kind & 3) << 2 : 0) | (b->grabbed ? 32 : 0);
	o->mantle_k = p->mantle_t > 0 ? (int)(255 * (1 - p->mantle_t / (p->mantle_len > 0 ? p->mantle_len : 1))) : 0;
	o->wpn = b->wpn; o->chr = op_of_chrt(b->ct) < 0 ? 0 : op_of_chrt(b->ct); o->shots = b->shots & 255; o->hurts = b->hurts & 255; o->reloads = b->reloads & 255; o->acts = b->acts & 255; o->act_kind = b->act_kind;
	o->spawns = b->spawns & 255; o->death = b->death_info; o->kills = b->kills; o->deaths = b->deaths; o->score = b->kills * 100;
}
static void ng_sk_pack(unsigned char *b);
static int ng_state(int for_actor, unsigned char *buf) {   // the state buffer one client gets (its own Player at the end)
	memset(buf, 0, NG_STATE_MAX); int my = ng_abs_team(NG.me);
	ng_put16(buf, (int)fminf(65535, fmaxf(0, *NGB.match_t * 10))); int sA = my == 0 ? score_l : score_r, sB = my == 0 ? score_r : score_l; ng_put16(buf + 2, sA); ng_put16(buf + 4, sB);
	buf[6] = ng_waiting ? 0 : *NGB.match_over_t > 0 ? 2 : 1; buf[7] = (unsigned char)(*NGB.end_result == 2 ? 2 : (*NGB.end_result == 0) == (my == 0) ? 0 : 1); buf[8] = (unsigned char)cfg.mode; buf[9] = (unsigned char)NG.nact;
	buf[10] = (unsigned char)NG.my_team; ng_mode_pack(buf + NG_MODE_OFS);
	for (int a = 0; a < NG.nact; a++) { NgA r; ng_actor_rec(a, &r); ng_pack(buf + NG_HDR + a * NG_AREC, &r); }
	int n = NG_HDR + NG.nact * NG_AREC; int j = ng_local(for_actor);
	ng_sk_pack(buf + n); n += NG_SKB; ng_tk_pack(buf + n); n += NG_TKB;   // skills, Think Tanks (stage 3)
	if (j >= 0) { memcpy(buf + n, &bots[j].pl, sizeof(Player)); n += sizeof(Player); }
	return n;
}
static void net_host_post(void) {   // after bots_update: the lag-compensation history, then the snapshots when due
	if (!net_host_on()) return; NG_T0();
	ng_hist_record(); ng_sk_watch();
	double now = net_now();
	if (now - ng_snap_t >= NG_SNAP_DT - 0.002) { ng_snap_t = now; ng_snap_seq++; unsigned hms = (unsigned)ng_ms();
		static unsigned char st[NG_STATE_MAX], pk[NET_PMAX], enc[NET_PMAX];
		for (int i = 0; i < NET_MAXPEER; i++) { NgCli *C = &ngc[i]; if (!C->on) continue;
			int n = ng_state(C->actor, st);
			const NgBase *B = NULL; if (C->have_ack) for (int k = 0; k < NG_NBASE; k++) if (C->base[k].len == n && C->base[k].seq == C->snap_ack) B = &C->base[k];
			int en = ng_rle(st, B ? B->d : NULL, n, enc, 1100); if (en < 0) continue;
			NgBase *S = &C->base[ng_snap_seq % NG_NBASE]; S->seq = ng_snap_seq; S->len = (short)n; memcpy(S->d, st, n);
			NetW w; net_hdr(&w, pk, NP_SNAP, NL.room); nw16(&w, ng_snap_seq); nw16(&w, B ? B->seq : 0xFFFF); nw32(&w, hms); nw16(&w, C->cmd_last); nw16(&w, C->in_last);
			nw32(&w, C->echo); nw16(&w, (int)fminf(65535, (now - C->echo_at) * 1000)); nw16(&w, n); nw16(&w, en); nwb(&w, enc, en);
			ngq_write(&w, &C->out, w.n + 600 < NET_PMAX - 8 ? w.n + 600 : NET_PMAX - 8); NGS.ev_out += C->out.n;
			net_send(&NL.peer[C->peer].addr, pk, w.n); NGS.snaps++; NGS.snap_bytes += w.n; } }
	NG_T1();
}
// main.c's feed_add on the host: the same entry for everyone (killer / victim actors when known)
static void net_host_feed(const char *k, int kme, const char *v, int vme) {
	if (!net_host_on()) return; (void)kme; (void)vme;
	unsigned char e[48]; NetW w = { e, 0, sizeof(e) }; int my = ng_abs_team(NG.me);
	nw8(&w, NGE_FEED); nw8(&w, ng_pend_k >= 0 ? ng_pend_k : 255); nw8(&w, ng_pend_v >= 0 ? ng_pend_v : 255); nw8(&w, feed_wicon); nw8(&w, ng_pend_head);
	nw8(&w, kme == -1 ? my ^ 1 : my); nw8(&w, vme == -1 ? my ^ 1 : my); nws(&w, k, 16); nws(&w, v, 16);
	ng_bcast_ev(e, w.n); ng_pend_k = ng_pend_v = -1; ng_pend_head = 0;
}
static void net_host_kill_note(int killer_bot, int victim_bot, int head) {   // bots.h's bot_on_kill: the actors of the next feed entry
	if (!net_host_on()) return; ng_pend_k = killer_bot == -1 ? NG.me : killer_bot >= 0 ? ng_actor_of_bot(killer_bot) : -1; ng_pend_v = victim_bot >= 0 ? ng_actor_of_bot(victim_bot) : NG.me; ng_pend_head = head;
}
static void net_host_gren_note(const Grenade *g) {   // our own grenade: the others see it fly
	if (!net_host_on()) return; unsigned char e[40]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_GRENV); nw8(&w, NG.me);
	for (int k = 0; k < 3; k++) nwf(&w, g->p[k]); for (int k = 0; k < 3; k++) nwf(&w, g->v[k]); nwf(&w, g->fuse); ng_bcast_ev(e, w.n);
}
static void ng_host_slot_left(int k) {
	int a = ng_actor_of_slot(k); if (a < 0 || !net_host_on()) return; int j = ng_local(a);
	for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on && ngc[i].actor == a) ngc[i].on = 0;
	if (j >= 0) { char nm[24]; snprintf(nm, sizeof(nm), "%s", bot_label[j]); bots[j].netc = 0; bot_label[j][0] = 0; bots[j].repath_t = 0; bots[j].tgt = -2;
		char t[48]; snprintf(t, sizeof(t), "%s LEFT", nm); *SKB.notify = NULL; logp("net: %s left the match: a bot takes over\n", nm);
		unsigned char e[24]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_LEFT); nw8(&w, a); nws(&w, nm, 16); ng_bcast_ev(e, w.n); }
	NG.human[a] = 0;
}
static void ng_host_attach(int a, int peer) {   // actor a is played from peer from now on
	int j = ng_local(a); if (j < 0) return; NgCli *C = &ngc[peer]; memset(C, 0, sizeof(*C));
	C->on = 1; C->peer = peer; C->actor = a; C->bi = j; C->wpn_last = 255;
	Bot *b = &bots[j]; const NetSlot *S = &NL.slot[NG.slot[a]]; int ct = ng_ct_of_op(S->chr);
	if (b->ct != ct) bot_set_char(b, ct); b->netc = 1; b->ap = 100; b->regen_cd = 0; snprintf(bot_label[j], sizeof(bot_label[j]), "%s", S->name);
	ng_set_wpn(b, ng_bot_wpn(S->prim < MENU_NWPN ? MENU_WPN[S->prim].sh : "m4a1")); NG.human[a] = 1; snprintf(NG.name[a], sizeof(NG.name[a]), "%s", S->name);
}
static void ng_host_slot_joined(int k) {
	int a = ng_actor_of_slot(k); if (a < 0) return; int peer = NL.slot[k].peer; if (peer < 0) return;
	ng_host_attach(a, peer); logp("net: %s takes over actor %d (bot %d)\n", NL.slot[k].name, a, ng_local(a));
}

// =========================================================== round modes (stage 3): Demolition / Elimination state in the snapshot,
// their events (DEV_* / EEV_*) as reliable events. Teams travel absolute, planter / defuser as net actors; the client runs no
// mode logic: main.c / elim_game.h show the events (net_mode_take) over the host's state.
static int NC_flip_(void);   // (after NC)
static int ng_hid_to_actor(int h) { return h < 0 ? 255 : h == 0 ? NG.me : ng_actor_of_bot(h - 1); }   // host: 0 the player, 1 + i bot i -> actor
static int ng_actor_to_lid(int a) { return a >= NG.nact ? -1 : a == NG.me ? 0 : 1 + ng_local(a); }      // client: actor -> 0 / 1 + bot
static void ng_mode_pack(unsigned char *b) {   // host
	int H = NG.my_team; NetW w = { b, 0, NG_HDR - NG_MODE_OFS };
	if (dem_on) { const DemMatch *m = &dm; nw8(&w, 1);
		nw8(&w, m->state); nw8(&w, m->round); nw8(&w, m->score[H]); nw8(&w, m->score[H ^ 1]); nw8(&w, m->first_attack ^ H); nw8(&w, m->switched); nw8(&w, m->last_winner < 0 ? 255 : m->last_winner ^ H); nw8(&w, m->last_reason);
		nw8(&w, m->alive[H]); nw8(&w, m->alive[H ^ 1]); nw8(&w, m->to_win); nw8(&w, m->switch_after); nw8(&w, m->bomb_site & 255); nw8(&w, ng_hid_to_actor(m->planter)); nw8(&w, m->plant_site & 255); nw8(&w, ng_hid_to_actor(m->defuser));
		nwf(&w, m->t); nwf(&w, m->round_left); nwf(&w, m->fuse_left); nwf(&w, m->plant_prog); nwf(&w, m->defuse_prog); for (int k = 0; k < 3; k++) nwf(&w, m->bomb_pos[k]); }
	else if (elim_on) { const ElimMatch *m = &em; nw8(&w, 2);
		nw8(&w, m->state); nw8(&w, m->round); nw8(&w, m->score[H]); nw8(&w, m->score[H ^ 1]); nw8(&w, m->first_side ^ H); nw8(&w, m->switched); nw8(&w, m->last_winner < 0 ? 255 : m->last_winner ^ H); nw8(&w, m->last_reason);
		nw8(&w, m->alive[H]); nw8(&w, m->alive[H ^ 1]); nw8(&w, m->warned); nw8(&w, m->evac); nw8(&w, m->locked); nw8(&w, m->timewarn); nw8(&w, m->count_left); nw8(&w, m->to_win); nw8(&w, m->switch_after);
		nwf(&w, m->t); nwf(&w, m->round_left); nwf(&w, m->round_time); nwf(&w, m->warn_at); nwf(&w, m->evac_at); nwf(&w, m->lock_at); nwf(&w, m->timewarn_at); }
	else if (ttm_on) { const TtmMatch *m = &tm; nw8(&w, 3);   // Terminal Conquest: teams absolute, actors as net actors, the bar as team A's share
		#define NG_TA(v) ((v) < 0 ? 255 : ((v) ^ H) & 1)
		nw8(&w, m->state); nw8(&w, m->active); nw8(&w, m->mult); nw8(&w, m->moving); nw8(&w, NG_TA(m->moving_team)); nw8(&w, m->nin[H]); nw8(&w, m->nin[H ^ 1]); nw8(&w, ng_lid_to_actor(m->capturer)); nw8(&w, NG_TA(m->cap_team));
		nw8(&w, m->score[H]); nw8(&w, m->score[H ^ 1]); nw8(&w, m->to_win); nw8(&w, m->overtime); nw8(&w, m->winner == 2 ? 2 : NG_TA(m->winner)); nw8(&w, NG_TA(m->last_team)); nw8(&w, ng_lid_to_actor(m->last_actor)); nw8(&w, NG_TA(m->final_team));
		nw8(&w, ng_lid_to_actor(m->first_in[H])); nw8(&w, ng_lid_to_actor(m->first_in[H ^ 1])); for (int i = 0; i < TTM_MAXT; i++) nw8(&w, NG_TA(m->owner[i]));
		unsigned in = 0, rd = 0; for (int a = 0; a < NG.nact && a < 8; a++) { int l = ng_act_to_lid(a); if (l < 0 || l >= TTM_NACT) continue; if (m->inside[l]) in |= 1u << a; if (m->ready[l]) rd |= 1u << a; } nw8(&w, in); nw8(&w, rd);
		for (int a = 0; a < 8; a++) { int l = a < NG.nact ? ng_act_to_lid(a) : -1; nw8(&w, l >= 0 && l < TTM_NACT ? ng_q8(m->gauge[l] * 255) : 0); }
		nwf(&w, m->t); nwf(&w, H ? 1 - m->bar : m->bar); nwf(&w, m->live_t); nwf(&w, m->time_left); nwf(&w, m->ot_left);
		#undef NG_TA
	}
	else nw8(&w, 0);
}
static int ng_tflip(int v, int M) { return v == 255 ? -1 : (v ^ M) & 1; }
static void ng_mode_unpack(const unsigned char *b) {   // client: into dm / em, in our own teams
	NetR r = { b, NG_HDR - NG_MODE_OFS, 0, 0 }; int kind = nr8(&r), M = NG.my_team;
	#define NG_T(v) ng_tflip((int)(v), M)
	if (kind == 1 && dem_on) { DemMatch *m = &dm; int sA, sB, al[2];
		m->state = nr8(&r); m->round = nr8(&r); sA = nr8(&r); sB = nr8(&r); m->score[M] = sA; m->score[M ^ 1] = sB; m->first_attack = NG_T(nr8(&r)); m->switched = nr8(&r); m->last_winner = NG_T(nr8(&r)); m->last_reason = nr8(&r);
		al[0] = nr8(&r); al[1] = nr8(&r); m->alive[M] = al[0]; m->alive[M ^ 1] = al[1]; m->to_win = nr8(&r); m->switch_after = nr8(&r); m->bomb_site = (signed char)nr8(&r);
		int pa = nr8(&r); m->planter = pa == 255 ? -1 : ng_actor_to_lid(pa); m->plant_site = (signed char)nr8(&r); int da = nr8(&r); m->defuser = da == 255 ? -1 : ng_actor_to_lid(da);
		m->t = nrf(&r); m->round_left = nrf(&r); m->fuse_left = nrf(&r); m->plant_prog = nrf(&r); m->defuse_prog = nrf(&r); for (int k = 0; k < 3; k++) m->bomb_pos[k] = nrf(&r); m->pend = 0; }
	else if (kind == 2 && elim_on) { ElimMatch *m = &em; int sA, sB, al[2];
		m->state = nr8(&r); m->round = nr8(&r); sA = nr8(&r); sB = nr8(&r); m->score[M] = sA; m->score[M ^ 1] = sB; m->first_side = NG_T(nr8(&r)); m->switched = nr8(&r); m->last_winner = NG_T(nr8(&r)); m->last_reason = nr8(&r);
		al[0] = nr8(&r); al[1] = nr8(&r); m->alive[M] = al[0]; m->alive[M ^ 1] = al[1]; m->warned = nr8(&r); m->evac = nr8(&r); m->locked = nr8(&r); m->timewarn = nr8(&r); m->count_left = nr8(&r); m->to_win = nr8(&r); m->switch_after = nr8(&r);
		m->t = nrf(&r); m->round_left = nrf(&r); m->round_time = nrf(&r); m->warn_at = nrf(&r); m->evac_at = nrf(&r); m->lock_at = nrf(&r); m->timewarn_at = nrf(&r); }
	else if (kind == 3 && ttm_on) { TtmMatch *m = &tm; int v;
		m->state = nr8(&r); v = nr8(&r); m->active = v < ttm_map.n ? v : m->active; m->mult = nr8(&r); m->moving = nr8(&r); m->moving_team = NG_T(nr8(&r)); v = nr8(&r); m->nin[M] = v; v = nr8(&r); m->nin[M ^ 1] = v;
		m->capturer = ng_lid_in(nr8(&r)); m->cap_team = NG_T(nr8(&r)); v = nr8(&r); m->score[M] = v; v = nr8(&r); m->score[M ^ 1] = v; m->to_win = nr8(&r); m->overtime = nr8(&r);
		v = nr8(&r); m->winner = v == 2 ? 2 : NG_T(v); m->last_team = NG_T(nr8(&r)); m->last_actor = ng_lid_in(nr8(&r)); m->final_team = NG_T(nr8(&r));
		v = nr8(&r); m->first_in[M] = ng_lid_in(v); v = nr8(&r); m->first_in[M ^ 1] = ng_lid_in(v); for (int i = 0; i < TTM_MAXT; i++) m->owner[i] = NG_T(nr8(&r));
		unsigned in = nr8(&r), rd = nr8(&r); unsigned char g[8]; for (int a = 0; a < 8; a++) g[a] = (unsigned char)nr8(&r);
		for (int a = 0; a < NG.nact && a < 8; a++) { int l = ng_act_to_lid(a); if (l < 0 || l >= TTM_NACT) continue; m->inside[l] = in >> a & 1; m->ready[l] = rd >> a & 1; m->gauge[l] = g[a] / 255.f; }
		m->t = nrf(&r); { float b = nrf(&r); m->bar = M ? 1 - b : b; } m->live_t = nrf(&r); m->time_left = nrf(&r); m->ot_left = nrf(&r); m->pend = 0; }
	#undef NG_T
}
static unsigned ng_mode_flip_ev(int m, unsigned bits) {   // the team-numbered event bits, in our own teams
	unsigned a = m == 1 ? DEV_ONE_LEFT_0 : m == 2 ? EEV_ONE_LEFT_0 : 0, b = m == 1 ? DEV_ONE_LEFT_1 : m == 2 ? EEV_ONE_LEFT_1 : 0;
	if (!a || !NC_flip_()) return bits;
	unsigned o = bits & ~(a | b); if (bits & a) o |= b; if (bits & b) o |= a; return o;
}
static void net_host_mode(int m, unsigned ev) {   // host: a mode's events this frame go to everyone
	if (!net_host_on() || !ev) return; unsigned char e[8]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_MODE); nw8(&w, m); nw32(&w, ev); ng_bcast_ev(e, w.n);
}
static unsigned net_mode_take(int m) { unsigned v = ng_mode_pend[m]; ng_mode_pend[m] = 0; return v; }   // client: events to show this frame
// =========================================================== client
typedef struct { double ms; int spawns_diff; NgA a[NG_MAXA]; } NgSnap;
static struct {
	NgCmd cmd[NG_NCMD]; Player pst[NG_NCMD]; unsigned short seq; int ncmd;
	NgBase base[NG_NBASE]; unsigned short snap_last; int have_snap;
	NgSnap ring[24]; int rn; double off; int off_ok, ring_n;
	NgEvQ out; unsigned short in_last; double send_t; unsigned char rel;
	Player hostpl; unsigned short cmd_ack; int have_pl, own_spawns, own_alive, first;
	int p_shots[NG_MAXA], p_hurts[NG_MAXA], p_rel[NG_MAXA], p_acts[NG_MAXA], p_spawns[NG_MAXA], p_alive[NG_MAXA], p_flags[NG_MAXA], p_init[NG_MAXA]; float p_vz[NG_MAXA];
	float err[3]; float rtt; int respawn; V3 rpos; float ryaw; int hdr_phase, hdr_result; float hdr_time; int sA, sB, flip;
	unsigned char skb[NG_SKB], skp[NG_MAXA * NG_SKAR], sko_ser[SK_MAXOBJ], skm_ser[SK_MAXMS]; int sk_new;
	unsigned char tkb[NG_TKB], tk_ser[TK_MAX], tg_ser[6]; int tk_new;   // Think Tanks: the newest block, the spawns / launches shown   // skills: the newest block, the last applied actors, the placements / launches shown
} NC;
static int NC_flip_(void) { return NC.flip; }
// =========================================================== operator skills (stage 3): the host runs every actor's skill (remote
// players through their command's skill / trigger / Square bits); the snapshot carries each actor's skill state, the deployables and
// the missiles. A client shows them through skills.h's mirror mode: the host's transitions call the same activation / ending code
// (sounds, effects, messages), the local simulation animates in between, and only the host deals damage or ends anything.
static int ng_lid_to_actor(int l) { return l < 0 || l >= SK_MAXACT ? 255 : l == 0 ? NG.me : l - 1 < NG.nact - 1 ? ng_actor_of_bot(l - 1) : 255; }   // skills.h actor -> net actor
static int ng_act_to_lid(int a) { return a >= NG.nact ? -1 : ng_actor_to_lid(a); }
static int ng_tgt_out(int t) { return t < 0 ? 255 : t >= SK_TGT_OBJ ? 128 + (t - SK_TGT_OBJ) : ng_lid_to_actor(t); }
static int ng_tgt_in(int v) { return v == 255 ? -1 : v >= 128 ? SK_TGT_OBJ + (v - 128) : ng_act_to_lid(v); }
static unsigned ng_mask_out(unsigned m) { unsigned r = 0; for (int l = 0; l < SK_MAXACT; l++) if (m >> l & 1) { int a = ng_lid_to_actor(l); if (a < 8) r |= 1u << a; } return r; }
static unsigned ng_mask_in(unsigned m) { unsigned r = 0; for (int a = 0; a < 8; a++) if (m >> a & 1) { int l = ng_act_to_lid(a); if (l >= 0) r |= 1u << l; } return r; }
static int ng_q8(float v) { return v <= 0 ? 0 : v >= 255 ? 255 : (int)(v + 0.5f); }
static int ng_sk_eats(int l) { const SkillActor *A = &SKA[l]; return sk_valid(l) && (A->state == SKS_AIM || A->post_t > 0 || (A->state == SKS_ACTIVE && A->skill == SK_CYBERDIVE)); }   // the trigger goes to the skill
static void ng_putf(unsigned char *p, float f) { memcpy(p, &f, 4); }
static float ng_getf(const unsigned char *p) { float f; memcpy(&f, p, 4); return f; }
static void ng_sk_pack(unsigned char *b) {   // host: the skill block (absolute teams, net actors)
	if (!sk_ok) return; int T = NG.my_team;
	for (int a = 0; a < NG.nact; a++) { int l = ng_act_to_lid(a); if (l < 0 || !sk_valid(l)) continue; const SkillActor *A = &SKA[l]; unsigned char *p = b + a * NG_SKAR;
		int act = A->state == SKS_ACTIVE || A->state == SKS_AIM;
		p[0] = (unsigned char)(1 | (A->state & 3) << 1 | (A->tier == 2 ? 8 : 0) | (A->toggled ? 16 : 0) | (A->dashing ? 32 : 0) | (A->sh_ph == 4 ? 64 : 0)); p[1] = (unsigned char)A->skill;
		ng_put16(p + 2, act ? (int)fminf(65535, A->t * 100) : 0); p[4] = (unsigned char)ng_q8(A->cool * 4); ng_put16(p + 5, A->intel); p[7] = (unsigned char)ng_q8(A->energy * 255); p[8] = (unsigned char)A->nlocks;
		int sx = A->sx_t > 0 && A->sx_skill >= 0; p[9] = (unsigned char)(sx ? A->sx_skill + 1 : 0); p[10] = (unsigned char)(sx ? ng_lid_to_actor(A->sx_from) : 255);
		p[11] = (unsigned char)(sx ? ng_q8(A->sx_t * 4) : 0); p[12] = (unsigned char)(sx ? ng_q8(A->sx_len * 4) : 0); p[13] = (unsigned char)(sx ? ng_q8(A->sx_energy * 255) : 0);
		p[14] = (unsigned char)ng_q8(A->hack_t * 10); p[15] = (unsigned char)ng_q8(A->emp_t * 10);
		for (int k = 0; k < 2; k++) { int K = (k ^ T) & 1; p[16 + K] = (unsigned char)ng_q8(A->heat_t[k] * 20); p[18 + K] = (unsigned char)ng_q8(A->mark_t[k] * 20);
			p[20 + K] = (unsigned char)(A->heat_t[k] > 0 ? ng_lid_to_actor(A->heat_by[k]) : 255); p[22 + K] = (unsigned char)(A->mark_t[k] > 0 ? ng_lid_to_actor(A->mark_by[k]) : 255); }
		p[24] = (unsigned char)ng_q8(A->post_t * 100); p[25] = (unsigned char)ng_mask_out(A->linked); p[26] = (unsigned char)ng_mask_out(A->synced); p[27] = (unsigned char)(A->obj >= 0 && A->obj < SK_MAXOBJ ? A->obj + 1 : 0);
		p[28] = (unsigned char)ng_q8(A->warn_t * 10); p[29] = (unsigned char)ng_q8(A->laser_t * 100);
		for (int k = 0; k < 4; k++) p[30 + k] = (unsigned char)(k < A->nlocks ? ng_tgt_out(A->locks[k]) : 255); }
	unsigned char *ob = b + NG_MAXA * NG_SKAR;
	for (int o = 0; o < SK_MAXOBJ; o++) { const SkObj *O = &SKO[o]; if (!O->alive) continue; unsigned char *p = ob + o * NG_SKOR;
		p[0] = O->ser ? O->ser : 1; p[1] = (unsigned char)((O->type & 7) | (O->tier >= 2 ? 8 : 0) | (O->phase & 3) << 4); p[2] = (unsigned char)ng_lid_to_actor(O->owner); p[3] = (unsigned char)((O->team ^ T) & 1);
		ng_putf(p + 4, O->pos.x); ng_putf(p + 8, O->pos.y); ng_putf(p + 12, O->pos.z); ng_put16(p + 16, ng_clamp16(O->nrm.x * 10000)); ng_put16(p + 18, ng_clamp16(O->nrm.y * 10000)); ng_put16(p + 20, ng_clamp16(O->nrm.z * 10000));
		float y = fmodf(O->yaw0, 6.2831853f); if (y < 0) y += 6.2831853f; ng_put16(p + 22, (int)(y * (65536 / 6.2831853f)) & 0xFFFF);
		ng_put16(p + 24, (int)fmaxf(0, fminf(65535, O->hp))); ng_put16(p + 26, (int)fmaxf(0, fminf(65535, O->hpmax))); ng_put16(p + 28, (int)fminf(65535, O->t * 4)); ng_put16(p + 30, (int)fminf(65535, O->life * 10));
		p[32] = (unsigned char)ng_tgt_out(O->target); p[33] = (unsigned char)ng_q8(O->energy * 255); ng_put16(p + 34, (int)fminf(65535, O->r)); ng_put16(p + 36, (int)fminf(65535, O->w)); ng_put16(p + 38, (int)fminf(65535, O->h));
		ng_putf(p + 40, O->from.x); ng_putf(p + 44, O->from.y); ng_putf(p + 48, O->from.z); }
	unsigned char *mb = ob + SK_MAXOBJ * NG_SKOR;
	for (int m = 0; m < SK_MAXMS; m++) { const SkMissile *M = &SKMS[m]; if (!M->on || !M->ser) continue; unsigned char *p = mb + m * NG_SKMR;
		p[0] = M->ser; p[1] = (unsigned char)M->kind; p[2] = (unsigned char)ng_lid_to_actor(M->owner); p[3] = (unsigned char)ng_tgt_out(M->target);
		ng_putf(p + 4, M->p0.x); ng_putf(p + 8, M->p0.y); ng_putf(p + 12, M->p0.z); ng_putf(p + 16, M->v0.x); ng_putf(p + 20, M->v0.y); ng_putf(p + 24, M->v0.z);
		p[28] = (unsigned char)ng_q8(M->delay0 * 100); ng_put16(p + 29, (int)fminf(65535, M->dmg)); p[31] = (unsigned char)((M->team ^ T) & 1); }
}
static int ng_sko_type(int skill) { return skill == SK_SENTRYGUN ? SKO_TURRET : skill == SK_CYBERWALL ? SKO_WALL : skill == SK_DMDDRONE ? SKO_DRONE : skill == SK_EMP ? SKO_EMP : skill == SK_SPECIALRECON ? SKO_RECON : -1; }
static void ng_ska_apply(int l, const unsigned char *p, const unsigned char *q) {   // client: one actor (p now, q the last applied)
	SkillActor *A = &SKA[l]; if (!(p[0] & 1) || !sk_valid(l) || A->skill != p[1]) return;
	int st = p[0] >> 1 & 3, tier = p[0] & 8 ? 2 : 1, pst = q[0] & 1 ? q[0] >> 1 & 3 : SKS_READY, ptier = q[0] & 8 ? 2 : 1, on = st == SKS_ACTIVE || st == SKS_AIM;
	if (on && pst != st && A->state != st && sk_alive(l)) {   // the host started it: the same start here (pays nothing: the host's numbers follow)
		A->state = SKS_READY; A->cool = 0; A->intel = sk_cost2(l); A->hack_t = A->emp_t = 0; A->obj = -1; A->post_t = 0; sk_use(l, tier); }
	else if (on && pst == st && tier == 2 && ptier == 1 && A->tier == 1) { A->t = 0; A->intel = sk_cost2(l); sk_upgrade(l); }   // held into Tier 2
	if (pst == SKS_ACTIVE && st != SKS_ACTIVE && A->state == SKS_ACTIVE) sk_end_active(l, (p[0] & 64) != 0);
	if (pst == SKS_AIM && st != SKS_AIM && A->state == SKS_AIM && A->skill == SK_ARMLAUNCHER) {   // the launcher fired (or folded up)
		if (p[24]) { V3 e = sk_eye(l), d = sk_dir(l); sk_ev(l, "intel_skill/armlauncher_fire"); sk_fx(SKF_AL_FIRE, vadd(e, vmul(d, 45)), d, &d); if (l == sk_local) ev_play_later("intel_skill/armlauncher_off", NULL, 0.4f); }
		else sk_ev(l, "intel_skill/armlauncher_off"); }
	if (pst == SKS_ACTIVE && st == SKS_ACTIVE && A->skill == SK_PLASMASHIELD && ((p[0] ^ q[0]) & 16) && A->toggled != ((p[0] & 16) != 0)) sk_ev(l, p[0] & 16 ? "intel_skill/shield_start" : "intel_skill/shield_end");
	int sx = p[9] - 1, psx = q[9] - 1;
	if (sx >= 0 && psx != sx && !(A->sx_t > 0 && A->sx_skill == sx)) { A->sx_skill = sx; A->sx_from = ng_act_to_lid(p[10]); A->sx_t = p[11] / 4.f; A->sx_len = p[12] / 4.f; sk_sx_start(l); }
	else if (sx < 0 && psx >= 0 && A->sx_t > 0) sk_sx_end(l, 0);
	// the host's numbers
	A->state = st; A->tier = tier; if (A->skill == SK_PLASMASHIELD) A->toggled = (p[0] & 16) != 0; A->dashing = (p[0] & 32) != 0;
	float t = ng_getu16(p + 2) / 100.f; if (on && fabsf(A->t - t) > 0.15f) A->t = t;
	float c = p[4] / 4.f; if (fabsf(A->cool - c) > 0.3f) A->cool = c;
	int in = ng_getu16(p + 5); if (in != A->intel) { A->intel = in; sk_intel_changed(l); }
	float e = p[7] / 255.f; if (fabsf(A->energy - e) > 0.03f) A->energy = e;
	if (st == SKS_AIM && A->skill == SK_ARMLAUNCHER) { int n = p[8] > 4 ? 4 : p[8];
		for (int k = 0; k < n; k++) { int tg = ng_tgt_in(p[30 + k]); if (k >= A->nlocks && l == sk_local) ev_play("intel_skill/armlauncher_on_ring", NULL); A->locks[k] = tg < 0 ? 0 : tg; }
		A->nlocks = n; } else A->nlocks = 0;
	if (sx >= 0) { A->sx_skill = sx; A->sx_from = ng_act_to_lid(p[10]); if (fabsf(A->sx_t - p[11] / 4.f) > 0.3f) A->sx_t = p[11] / 4.f; A->sx_len = p[12] / 4.f; if (fabsf(A->sx_energy - p[13] / 255.f) > 0.03f) A->sx_energy = p[13] / 255.f; }
	else if (A->sx_t > 0) { A->sx_t = 0; A->sx_skill = -1; }
	#define NG_SKT(F, v, tol) do { float v_ = (v); if (fabsf((F) - v_) > (tol)) (F) = v_; } while (0)
	NG_SKT(A->hack_t, p[14] / 10.f, 0.15f); NG_SKT(A->emp_t, p[15] / 10.f, 0.15f);
	for (int K = 0; K < 2; K++) { int k = (K ^ NG.my_team) & 1; NG_SKT(A->heat_t[k], p[16 + K] / 20.f, 0.1f); NG_SKT(A->mark_t[k], p[18 + K] / 20.f, 0.1f);
		A->heat_by[k] = (signed char)(p[20 + K] == 255 ? -1 : ng_act_to_lid(p[20 + K])); A->mark_by[k] = (signed char)(p[22 + K] == 255 ? -1 : ng_act_to_lid(p[22 + K])); }
	NG_SKT(A->post_t, p[24] / 100.f, 0.1f); A->linked = ng_mask_in(p[25]); A->synced = ng_mask_in(p[26]); A->obj = p[27] ? p[27] - 1 : -1;
	NG_SKT(A->warn_t, p[28] / 10.f, 0.2f); if (p[29]) A->laser_t = p[29] / 100.f;
	#undef NG_SKT
}
static void ng_sko_apply(int o, const unsigned char *p) {   // client: one deployable slot
	SkObj *O = &SKO[o]; int s = p[0];
	if (!s) { if (O->alive && NC.sko_ser[o]) { int destroyed = O->type == SKO_EMP ? O->t < 2.5f : O->type == SKO_DRONE ? O->phase != 2 && O->t < O->life - 0.5f : O->t < O->life - 0.5f; sk_obj_end(o, destroyed, -1); }
		NC.sko_ser[o] = 0; return; }
	int type = p[1] & 7, tier = p[1] & 8 ? 2 : 1, phase = p[1] >> 4 & 3, l = ng_act_to_lid(p[2]);
	V3 pos = v3(ng_getf(p + 4), ng_getf(p + 8), ng_getf(p + 12)), nrm = v3(ng_get16(p + 16) / 10000.f, ng_get16(p + 18) / 10000.f, ng_get16(p + 20) / 10000.f);
	if (s != NC.sko_ser[o]) {   // a new placement: the same placement here (its sounds and build effect), in the host's slot
		NC.sko_ser[o] = s; if (l < 0 || !sk_valid(l)) return;
		if (O->alive) sk_obj_free(o);
		SkillActor *A = &SKA[l]; SkillActor keep = *A; int n;
		if (ng_sko_type(A->skill) == type) { A->state = SKS_AIM; A->tier = tier; for (int k = 0; k < SK_MAXOBJ; k++) if (SKO[k].alive && SKO[k].owner == l && SKO[k].type == type && k != o) sk_obj_free(k);
			sk_place(l, pos, nrm); n = A->obj; }
		else n = sk_obj_new(type, l);
		*A = keep; A->obj = o;
		if (n < 0) return;
		if (n != o) { SKO[o] = SKO[n]; memset(&SKO[n], 0, sizeof(SkObj)); }
		O->ser = (unsigned char)s; O->tier = tier; O->pos = pos; O->nrm = nrm; O->yaw0 = O->yaw = ng_getu16(p + 22) * (6.2831853f / 65536); O->r = ng_getu16(p + 34); O->w = ng_getu16(p + 36); O->h = ng_getu16(p + 38);
		O->from = v3(ng_getf(p + 40), ng_getf(p + 44), ng_getf(p + 48)); if (type == SKO_DRONE && phase == 0) O->cur = O->from;
		O->t = ng_getu16(p + 28) / 4.f; O->phase = phase; }
	if (!O->alive) return;
	O->team = (p[3] ^ NG.my_team) & 1; O->tier = tier; O->hp = ng_getu16(p + 24); O->hpmax = ng_getu16(p + 26); O->life = ng_getu16(p + 30) / 10.f;
	{ float t = ng_getu16(p + 28) / 4.f; if (fabsf(O->t - t) > 0.4f) O->t = t; }
	if (phase > O->phase) O->phase = phase;
	O->target = ng_tgt_in(p[32]); if (type == SKO_DRONE && fabsf(O->energy - p[33] / 255.f) > 0.05f) O->energy = p[33] / 255.f;
}
static void ng_skm_apply(int m, const unsigned char *p) {   // client: a missile slot (a new launch starts here; it flies and blows up by itself)
	int s = p[0]; if (!s || s == NC.skm_ser[m]) { NC.skm_ser[m] = (unsigned char)s; return; } NC.skm_ser[m] = (unsigned char)s;
	int l = ng_act_to_lid(p[2]); if (l < 0) return;
	SkMissile *M = &SKMS[m]; memset(M, 0, sizeof(*M)); M->on = 1; M->ser = (unsigned char)s; M->kind = p[1]; M->owner = l; M->team = (p[31] ^ NG.my_team) & 1; M->target = ng_tgt_in(p[3]);
	M->p = M->p0 = v3(ng_getf(p + 4), ng_getf(p + 8), ng_getf(p + 12)); M->v = M->v0 = v3(ng_getf(p + 16), ng_getf(p + 20), ng_getf(p + 24)); M->delay = M->delay0 = p[28] / 100.f + 1e-4f; M->dmg = ng_getu16(p + 29);
	if (M->kind == 1) sk_ev_at("intel_skill/turret_rocket_fire", M->p);
}
static void ng_sk_apply(void) {   // client, after skills_update: the newest snapshot's skill block
	if (!NC.sk_new || !sk_ok) return; NC.sk_new = 0; sk_mirror_ok = 1;
	for (int a = 0; a < NG.nact; a++) { int l = ng_act_to_lid(a); if (l >= 0) ng_ska_apply(l, NC.skb + a * NG_SKAR, NC.skp + a * NG_SKAR); }
	const unsigned char *ob = NC.skb + NG_MAXA * NG_SKAR; for (int o = 0; o < SK_MAXOBJ; o++) ng_sko_apply(o, ob + o * NG_SKOR);
	const unsigned char *mb = ob + SK_MAXOBJ * NG_SKOR; for (int m = 0; m < SK_MAXMS; m++) ng_skm_apply(m, mb + m * NG_SKMR);
	memcpy(NC.skp, NC.skb, NG_MAXA * NG_SKAR); sk_mirror_ok = 0; ng_sk_watch();
}
// =========================================================== Terminal Conquest (stage 3): the terminals' state in the mode block
// (ng_mode_pack kind 3), the TEV_* events as NGE_MODE 3, the Think Tanks and their grenades in the snapshot (after the skills).
// A client runs no rules and no tank brains: ttm.h shows the host's tm through ttm_ev_fn, thinktank.h's tk_puppet follows the
// host's tanks, and the call-ins / hacks / deaths replay the same presentation here (ttm_call_tank, tk_hack_begin, tk_die).
static unsigned ng_ttm_ev(void) { return net_mode_take(3); }
static int ng_ttm_call(V3 at, float yaw) {   // client: our call-in goes to the host
	if (!net_client_on()) return -1; unsigned char e[16]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_TKCALL); nwf(&w, at.x); nwf(&w, at.y); nwf(&w, at.z); nw16(&w, (int)(yaw * 10000));
	ngq_push(&NC.out, e, w.n); logp("net: tachikoma call-in sent to the host\n"); return 0;
}
static void ng_host_tkcall(NgCli *C, NetR *r) {
	V3 at; at.x = nrf(r); at.y = nrf(r); at.z = nrf(r); float yaw = (short)nr16(r) / 10000.f; if (r->bad) return;
	int l = 1 + C->bi; Bot *b = &bots[C->bi]; if (b->state == BS_DEAD || !ng_near(at, b->pl.pos, 2000)) return;
	if (ttm_on && l < TTM_NACT && tm.ready[l] && tm.state != TS_END) { int i = ttm_call_tank(l, at, yaw); logp("net: %s calls in a tachikoma (%s)\n", NG.name[C->actor], i >= 0 ? "ok" : "refused"); }
}
static void net_host_ttm(unsigned ev, float dt) {   // host, after ttm_frame: the events go out; remote players hack with Square
	if (!net_host_on() || !ttm_on) return; net_host_mode(3, ev);
	for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (!b->netc || b->state == BS_DEAD) continue;
		V3 e = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + (b->pl.eye > 1 ? b->pl.eye : P_EYE)), d = v3(cosf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->aim_pitch));
		tk_use(1 + i, e, d, b->net_use, dt); }
}
static void ng_tk_pack(unsigned char *b) {   // host: the tanks and their grenades
	int H = NG.my_team;
	for (int i = 0; i < TK_MAX; i++) { const Tank *k = &tk[i]; if (k->state == TK_OFF) continue; unsigned char *p = b + i * NG_TKR;
		p[0] = (unsigned char)((k->state & 3) | (k->def ? 4 : 0) | (k->firing ? 8 : 0) | (k->sees ? 16 : 0) | (k->kind & 1) << 5); p[1] = k->ser ? k->ser : 1; p[2] = (unsigned char)((k->team ^ H) & 1); p[3] = (unsigned char)k->flags;
		p[4] = (unsigned char)ng_lid_to_actor(k->owner); p[5] = (unsigned char)ng_lid_to_actor(k->hacker); p[6] = (unsigned char)ng_lid_to_actor(k->tgt); p[7] = (unsigned char)ng_lid_to_actor(k->last_hacker); p[8] = (unsigned char)ng_lid_to_actor(k->killed_by);
		p[9] = (unsigned char)ng_q8(k->laser * 255); ng_putf(p + 10, k->pl.pos.x); ng_putf(p + 14, k->pl.pos.y); ng_putf(p + 18, k->pl.pos.z);
		float y = fmodf(k->pl.yaw, 6.2831853f); if (y < 0) y += 6.2831853f; ng_put16(p + 22, (int)(y * (65536 / 6.2831853f)) & 0xFFFF);
		ng_put16(p + 24, (int)fmaxf(0, fminf(65535, k->hp))); ng_put16(p + 26, (int)fmaxf(0, fminf(65535, k->hpmax))); ng_put16(p + 28, (int)fminf(65535, k->hack_t * 100)); ng_put16(p + 30, (int)fminf(65535, k->open_t * 100)); ng_put16(p + 32, (int)fminf(65535, k->t * 100));
		ng_putf(p + 34, k->aim_pt.x); ng_putf(p + 38, k->aim_pt.y); ng_putf(p + 42, k->aim_pt.z); ng_put16(p + 46, ng_clamp16(k->pl.vel.x)); ng_put16(p + 48, ng_clamp16(k->pl.vel.y)); ng_put16(p + 50, ng_clamp16(k->pl.vel.z));
		ng_put32(p + 52, (int)k->code[0]); ng_put32(p + 56, (int)k->code[1]); }
	unsigned char *gb = b + TK_MAX * NG_TKR;
	for (int j = 0; j < 6; j++) { const TkGren *G = &tk_gren[j]; if (!G->on || !G->ser) continue; unsigned char *p = gb + j * NG_TGR;
		p[0] = G->ser; p[1] = (unsigned char)((G->team ^ H) & 1); p[2] = (unsigned char)ng_lid_to_actor(G->owner); p[3] = (unsigned char)G->tank;
		ng_putf(p + 4, G->p0.x); ng_putf(p + 8, G->p0.y); ng_putf(p + 12, G->p0.z); ng_putf(p + 16, G->v0.x); ng_putf(p + 20, G->v0.y); ng_putf(p + 24, G->v0.z); }
}
static int ng_lid_in(int v) { return v == 255 ? -1 : ng_act_to_lid(v); }
static void ng_tk_apply(void) {   // client, before tk_update: the newest tank block
	if (!NC.tk_new) return; NC.tk_new = 0; int M = NG.my_team;
	for (int i = 0; i < TK_MAX; i++) { const unsigned char *p = NC.tkb + i * NG_TKR; Tank *k = &tk[i];   // first what is gone: a slot the host emptied or reused
		if ((p[0] & 3) == TK_OFF || !p[1] || p[1] != NC.tk_ser[i]) { if (k->state != TK_OFF && NC.tk_ser[i]) { if (k->state != TK_DEAD && (p[0] & 3) == TK_OFF) tk_die(i, -1); else tk_free_slot(k); } if ((p[0] & 3) == TK_OFF) NC.tk_ser[i] = 0; } }
	for (int i = 0; i < TK_MAX; i++) { const unsigned char *p = NC.tkb + i * NG_TKR; Tank *k = &tk[i]; int st = p[0] & 3, s = p[1];
		if (st == TK_OFF || !s) continue;
		V3 pos = v3(ng_getf(p + 10), ng_getf(p + 14), ng_getf(p + 18)); float yaw = ng_getu16(p + 22) * (6.2831853f / 65536); int team = (p[2] ^ M) & 1, owner = ng_lid_in(p[4]);
		if (s != NC.tk_ser[i]) {   // a new tank: the same arrival here (call-in banner, sounds), in the host's slot
			NC.tk_ser[i] = (unsigned char)s; if (st == TK_DEAD) continue;
			if (k->state != TK_OFF) tk_free_slot(k);
			int n = -1; char nb[32];
			if ((p[3] & TKF_DROP) && ttm_on && owner >= 0 && tk_actor_team(owner) == team && !tk_count(team)) n = ttm_call_tank(owner, pos, yaw);
			if (n < 0) n = tk_spawn(pos, yaw, team, owner, owner >= 0 ? tk_actor_name(owner) : "", ttm_on && owner >= 0 ? ttm_tank_name(owner, nb, sizeof(nb)) : "Think Tank", p[3]);
			if (n < 0) continue;
			if (n != i) { tk[i] = tk[n]; memset(&tk[n], 0, sizeof(Tank)); }
			k->ser = (unsigned char)s; k->pl.pos = pos; k->pl.yaw = yaw; k->kind = p[0] >> 5 & 1; }
		if (k->state == TK_OFF) continue;
		int def = (p[0] & 4) != 0, hacker = ng_lid_in(p[5]), lh = ng_lid_in(p[7]);
		if (st == TK_DEAD && k->state != TK_DEAD) tk_die(i, ng_lid_in(p[8]));
		else if (k->state != TK_DEAD) {
			if (hacker >= 0 && k->hacker != hacker) tk_hack_begin(i, hacker);
			else if (hacker < 0 && k->hacker >= 0 && def) { int a = k->hacker; k->hacker = -1; k->hack_t = 0; tk_event(i, TKE_HACK_FAIL, a); }
			if (!def && k->def) { k->def = 0; k->open_t = 0; k->hacker = -1; k->done_t = 0; anim_play(&k->an, tk_cl[TC_DMG_CRIT], 0.06f, 0.25f, 0); tk_snd("tachi_stun_start", k->sph[0], 0.25f, 8000);
				tk_voice("tachi_enemysuccess_", k->sph[0], 0.8f); tk_event(i, TKE_HACK_DONE, lh); }
			else if (def && !k->def) { k->def = 1; k->open_t = 0; tk_snd("tachi_stun_end", k->sph[0], 0.3f, 8000); tk_voice("tachi_teamsuccess_", k->sph[0], 0.6f); tk_event(i, TKE_RESTORED, lh); } }
		k->team = team; k->owner = owner; k->hacker = k->state == TK_DEAD ? -1 : hacker; k->last_hacker = lh; k->def = def; k->tgt = ng_lid_in(p[6]); k->sees = (p[0] & 16) != 0; k->net_firing = (p[0] & 8) != 0 && k->state == TK_LIVE;
		k->laser = p[9] / 255.f; k->hp = ng_getu16(p + 24); k->hpmax = ng_getu16(p + 26); { float ht = ng_getu16(p + 28) / 100.f; if (fabsf(k->hack_t - ht) > 0.2f) k->hack_t = ht; }
		{ float ot = ng_getu16(p + 30) / 100.f; if (fabsf(k->open_t - ot) > 0.3f) k->open_t = ot; }
		{ float t = ng_getu16(p + 32) / 100.f; if (st == k->state && fabsf(k->t - t) > 0.3f && t < 600) k->t = t; }
		k->aim_pt = v3(ng_getf(p + 34), ng_getf(p + 38), ng_getf(p + 42)); k->net_pos = pos; k->net_vel = v3(ng_get16(p + 46), ng_get16(p + 48), ng_get16(p + 50)); k->net_yaw = yaw; k->net_age = 0;
		k->code[0] = (unsigned)ng_get32(p + 52); k->code[1] = (unsigned)ng_get32(p + 56);
		if (k->state == TK_LIVE && k->tgt == 0 && k->sees) tk_targeted_t = 0.4f; }
	const unsigned char *gb = NC.tkb + TK_MAX * NG_TKR;
	for (int j = 0; j < 6; j++) { const unsigned char *p = gb + j * NG_TGR; int s = p[0]; if (!s || s == NC.tg_ser[j]) { NC.tg_ser[j] = (unsigned char)s; continue; } NC.tg_ser[j] = (unsigned char)s;
		TkGren *G = &tk_gren[j]; memset(G, 0, sizeof(*G)); G->on = 1; G->ser = (unsigned char)s; G->team = (p[1] ^ M) & 1; G->owner = ng_lid_in(p[2]); G->tank = p[3] < TK_MAX ? p[3] : 0;
		G->p = G->p0 = v3(ng_getf(p + 4), ng_getf(p + 8), ng_getf(p + 12)); G->v = G->v0 = v3(ng_getf(p + 16), ng_getf(p + 20), ng_getf(p + 24));
		tk_snd("tachi_gl_fire", G->p, 0.55f, 12000); { V3 d = G->v; fx_muzzle_3p(&G->p.x, &d.x); } }
}
static void net_mirror_flags(void) {   // every frame before the skills: what this machine simulates (all 0 / NULL offline)
	int c = net_client_on(); sk_mirror = c; if (!net_host_on()) sk_manual = 0; tk_mirror = c; ttm_ev_fn = c ? ng_ttm_ev : NULL; ttm_call_fn = c ? ng_ttm_call : NULL;
}
static void ng_sk_watch(void) {   // boot.log: every actor's skill transitions and the deployables / missiles (host and clients: the same lines)
	static int pst[NG_MAXA], ptier[NG_MAXA], psx[NG_MAXA]; static unsigned char pobj[SK_MAXOBJ], pms[SK_MAXMS]; static const char *SN[4] = { "READY", "AIM", "ACTIVE", "COOLDOWN" };
	if (!sk_ok) return;
	for (int a = 0; a < NG.nact; a++) { int l = ng_act_to_lid(a); if (l < 0 || !sk_valid(l)) continue; const SkillActor *A = &SKA[l]; int sx = A->sx_t > 0 ? A->sx_skill : -1;
		if (A->state != pst[a] || A->tier != ptier[a]) logp("net: skill actor %d (%s): %s -> %s, tier %d\n", a, SK_DEF[A->skill].id, SN[pst[a] & 3], SN[A->state & 3], A->tier);
		if (sx + 1 != psx[a]) logp("net: skill actor %d: skillsync %s\n", a, sx >= 0 ? SK_DEF[sx].id : "over");
		pst[a] = A->state; ptier[a] = A->tier; psx[a] = sx + 1; }
	for (int o = 0; o < SK_MAXOBJ; o++) { unsigned char s = SKO[o].alive ? SKO[o].ser : 0; if (s != pobj[o]) { if (s) logp("net: deployable %d: type %d of actor %d placed\n", o, SKO[o].type, ng_lid_to_actor(SKO[o].owner)); else logp("net: deployable %d gone\n", o); } pobj[o] = s; }
	int nm = 0; for (int m = 0; m < SK_MAXMS; m++) { unsigned char s = SKMS[m].on ? SKMS[m].ser : 0; if (s && s != pms[m]) nm++; pms[m] = s; } if (nm) logp("net: %d missile(s) launched\n", nm);
}
static void net_host_skill_hit(int att, int v, float dmg, int killed) {   // host: skill damage (skills.h actors): hit marker / damage direction for remote players
	if (!net_host_on()) return; int aa = ng_lid_to_actor(att), va = ng_lid_to_actor(v); if (va == 255) return;
	NgCli *C = aa != 255 && aa != va ? ng_cli_of_actor(aa) : NULL; if (C) { unsigned char e[4] = { NGE_HIT, (unsigned char)va, (unsigned char)(killed ? 2 : 0) }; ngq_push(&C->out, e, 3); }
	NgCli *V = ng_cli_of_actor(va); if (V) { V3 vp = ng_actor_pos(va), ap = aa != 255 ? ng_actor_pos(aa) : vp; unsigned char e[8]; e[0] = NGE_DMG; e[1] = (unsigned char)(aa == 255 ? va : aa); ng_put16(e + 2, (int)(dmg * 10));
		ng_put16(e + 4, (int)(atan2f(ap.y - vp.y, ap.x - vp.x) * 10000)); ngq_push(&V->out, e, 6); }
}
static double ng_lms(void) { return net_now() * 1000.0; }
static int net_frozen(void) { return NG.on && (NG.host ? ng_waiting : NC.hdr_phase == 0); }
static void ng_client_snap(const unsigned char *d, int n) {
	NetR r = { d, n, 0, 0 };
	unsigned short seq = (unsigned short)nr16(&r), bseq = (unsigned short)nr16(&r); unsigned hms = nr32(&r); unsigned short cack = (unsigned short)nr16(&r), rack = (unsigned short)nr16(&r);
	unsigned echo = nr32(&r); int hold = nr16(&r), len = nr16(&r), en = nr16(&r); if (r.bad || len > NG_STATE_MAX || r.i + en > r.n) return;
	const unsigned char *enc = r.p + r.i; r.i += en;
	if (NC.have_snap && !ng_seq_gt(seq, NC.snap_last)) goto events;   // old or repeated
	{ const NgBase *B = NULL; if (bseq != 0xFFFF) { for (int k = 0; k < NG_NBASE; k++) if (NC.base[k].seq == bseq && NC.base[k].len > 0) B = &NC.base[k]; if (!B) goto events; }
	  NgBase *S = &NC.base[seq % NG_NBASE]; if (ng_unrle(enc, en, B ? B->d : NULL, B ? B->len : 0, S->d, len) < 0) goto events; S->seq = seq; S->len = (short)len;
	  NC.snap_last = seq; NC.have_snap = 1; NGS.snaps++; NGS.snap_bytes += n + 8;
	  { double rtt = ng_lms() - echo - hold; if (echo && rtt >= 0 && rtt < 2000) NC.rtt += ((float)rtt - NC.rtt) * 0.2f; }
	  double dd = hms - ng_lms(); if (!NC.off_ok) { NC.off = dd; NC.off_ok = 1; } else if (dd > NC.off) NC.off += (dd - NC.off) * 0.3; else NC.off += (dd - NC.off) * 0.01;
	  const unsigned char *st = S->d; int na = st[9]; if (na != NG.nact) goto events;
	  NC.hdr_time = ng_getu16(st) / 10.f; NC.sA = ng_getu16(st + 2); NC.sB = ng_getu16(st + 4); NC.hdr_phase = st[6]; NC.hdr_result = st[7]; NC.flip = (st[10] ^ NG.my_team) & 1;
	  ng_mode_unpack(st + NG_MODE_OFS);
	  NgSnap *R = &NC.ring[NC.rn % 24]; NC.rn++; R->ms = hms; for (int a = 0; a < na; a++) ng_unpack(st + NG_HDR + a * NG_AREC, &R->a[a]);
	  int po = NG_HDR + na * NG_AREC; if (len >= po + NG_SKB + NG_TKB) { memcpy(NC.skb, st + po, NG_SKB); NC.sk_new = 1; po += NG_SKB; memcpy(NC.tkb, st + po, NG_TKB); NC.tk_new = 1; po += NG_TKB; }
	  if (len >= po + (int)sizeof(Player)) { memcpy(&NC.hostpl, st + po, sizeof(Player)); NC.cmd_ack = cack; NC.have_pl = 1; } }
events:
	ngq_ack(&NC.out, rack);
	{ int ne = nr8(&r); for (int k = 0; k < ne; k++) { unsigned short es = (unsigned short)nr16(&r); int el = nr8(&r); if (r.bad || el < 1 || r.i + el > r.n) return;
		NetR e = { r.p + r.i, el, 0, 0 }; r.i += el; if (es != (unsigned short)(NC.in_last + 1)) continue; NC.in_last = es; NGS.ev_in++;
		int t = nr8(&e);
		if (t == NGE_FEED) { int ka = nr8(&e), va = nr8(&e), icon = nr8(&e), head = nr8(&e), kt = nr8(&e), vt = nr8(&e); char kn[16], vn[16]; nrs(&e, kn, 16); nrs(&e, vn, 16);
			int kside = ka == NG.me ? 1 : kt == NG.my_team ? 0 : -1, vside = va == NG.me ? 1 : vt == NG.my_team ? 0 : -1, w0 = feed_wicon; feed_wicon = icon; feed_add(feed, &nfeed, kn, kside, vn, vside); feed_wicon = w0;
			if (ka == NG.me && va != NG.me && va < NG.nact) {   // our kill
				int vj = ng_local(va); (*SKB.kills)++; *SKB.kill_marker = 0.7f; ev_play(head ? "impact/ui_headshot" : "impact/ui_killshot", NULL); ev_play("impact/killshot_fp", NULL); if (head) ev_play("impact/headshot_fp", NULL);
				*SKB.streak = *SKB.streak_t > 0 ? *SKB.streak + 1 : 1; *SKB.streak_t = 4; int pts = 100 + (head ? 50 : 0); *SKB.intel += pts;
				*SKB.notify = head ? "HEADSHOT" : *SKB.streak == 2 ? "DOUBLE KILL" : *SKB.streak == 3 ? "TRIPLE KILL" : *SKB.streak >= 4 ? "MULTI KILL" : "NICE KILL"; *SKB.notify_t = 0; *SKB.notify_pts = pts;
				(*SKB.m_pkills)++; if (vj >= 0) my_kills_on[vj]++;
				if (*SKB.streak >= 2) { ev_play("ui/kill_special", NULL); snd_killvoice(pvoice, "doublekill"); } else if (icon == 41) snd_killvoice(pvoice, "bombkill"); else if (vj >= 0 && vj == killer_bot) snd_killvoice(pvoice, "revenge"); }
			if (va == NG.me && *SKB.pdead <= 0) {   // our death
				int kj = ka < NG.nact ? ng_local(ka) : -1; *SKB.php = 0; *SKB.pdead = 3.5f; ev_play("ui/dead", NULL); snd_voice(pvoice, "death_1st", NULL); (*NGB.m_pdeaths)++; (*NGB.pdeaths)++;
				killer_bot = kj; if (kj >= 0 && kj < NBOT_MAX) kills_on_me[kj]++; music_ev("bgm/dead", 100); logp("  player died (killed by actor %d)\n", ka); } }
		else if (t == NGE_HIT) { int va = nr8(&e), f = nr8(&e); (void)va; *SKB.hit_marker = 0.2f; if (f & 1) { ev_play("impact/headshot_fp", NULL); ev_play("impact/ui_heavy", NULL); } else ev_play("impact/ui_common", NULL); }
		else if (t == NGE_DMG) { int ka = nr8(&e); float dmg = nr16(&e) / 10.f, ang = (short)nr16(&e) / 10000.f; if (*SKB.pdead <= 0) { self_dmg += dmg; self_dmg_dir = ang; bot_last_hitter = ka < NG.nact ? ng_local(ka) : -1; } }
		else if (t == NGE_GRENV) { int ow = nr8(&e); float p0[3], v0[3]; for (int q = 0; q < 3; q++) p0[q] = nrf(&e); for (int q = 0; q < 3; q++) v0[q] = nrf(&e); float fu = nrf(&e); if (!e.bad && ow != NG.me) ng_gren_spawn(ow, p0, v0, fu); }
		else if (t == NGE_MODE) { int m = nr8(&e); unsigned bits = nr32(&e); if (!e.bad && m > 0 && m < 4) ng_mode_pend[m] |= ng_mode_flip_ev(m, bits); }
		else if (t == NGE_LEFT) { int a = nr8(&e); char nm[16]; nrs(&e, nm, 16); if (a < NG.nact) { NG.human[a] = 0; int j = ng_local(a); if (j >= 0) bot_label[j][0] = 0; }
			static char msg[40]; snprintf(msg, sizeof(msg), "%s LEFT", nm); *SKB.notify = msg; *SKB.notify_t = 0; *SKB.notify_pts = 0; logp("net: %s left the match\n", nm); } } }
}
static void ng_rx(int peer, const unsigned char *d, int n) { NG_T0(); if (peer >= 0) ng_host_rx(peer, d, n); else if (net_client_on()) ng_client_snap(d, n); NG_T1(); }
// the client's command for this frame (after player_update): kept with the resulting Player for the replays
static void net_client_after_move(const Player *pl, float mx, float my, int sprint, int jump, float dt, float pitch, int trig) {
	if (!net_client_on()) return; NgCmd *m = &NC.cmd[++NC.seq % NG_NCMD]; NC.ncmd++;
	m->seq = NC.seq; m->dt = (unsigned short)fminf(10000, fmaxf(1, dt * 100000 + 0.5f)); m->mx = (signed char)lrintf(fmaxf(-1, fminf(1, mx)) * 127); m->my = (signed char)lrintf(fmaxf(-1, fminf(1, my)) * 127);
	m->btn = (sprint ? 1 : 0) | (jump ? 2 : 0) | (pl->in_crouch ? 4 : 0) | (pl->in_ads ? 8 : 0) | (trig ? 16 : 0) | (ng_use_held ? 32 : 0) | (ng_skill_btn & 3) << 6;
	float y = fmodf(pl->yaw, 6.2831853f); if (y < 0) y += 6.2831853f; m->yaw = (unsigned short)((int)(y * (65536 / 6.2831853f)) & 0xFFFF); m->pitch = (short)ng_clamp16(pitch * 10000);
	m->spd = (unsigned char)fminf(255, (pl->speed_mul > 0 ? pl->speed_mul : 1) * 64); m->wpn = (unsigned char)(ng_my_wpn() | (ng_skill_fire ? 0x80 : 0)); ng_skill_fire = 0; m->rel = (unsigned char)(*NGB.reloads & 255);
	NC.pst[NC.seq % NG_NCMD] = *pl;
}
// replaying the commands the host has not run yet over its state (when our prediction went wrong)
static void ng_cmd_apply(Player *p, const NgCmd *m) {
	p->yaw = m->yaw * (6.2831853f / 65536); p->in_crouch = (m->btn & 4) != 0; p->in_ads = (m->btn & 8) != 0; p->speed_mul = m->spd / 64.f;
	player_update(p, m->mx / 127.f, m->my / 127.f, (m->btn & 1) != 0, (m->btn & 2) != 0, m->dt / 100000.f);
}
// the client moves with exactly what the host will run (the command's resolution): no drift between the two simulations
static void net_client_quantize(Player *pl, float *mx, float *my, float *dt) {
	if (!net_client_on()) return; *mx = lrintf(fmaxf(-1, fminf(1, *mx)) * 127) / 127.f; *my = lrintf(fmaxf(-1, fminf(1, *my)) * 127) / 127.f;
	*dt = (unsigned short)fminf(10000, fmaxf(1, *dt * 100000 + 0.5f)) / 100000.f;
	float y = fmodf(pl->yaw, 6.2831853f); if (y < 0) y += 6.2831853f; pl->yaw = (float)((int)(y * (65536 / 6.2831853f)) & 0xFFFF) * (6.2831853f / 65536);
	pl->speed_mul = (unsigned char)fminf(255, (pl->speed_mul > 0 ? pl->speed_mul : 1) * 64) / 64.f;
}
static void ng_reconcile(void) {
	if (!NC.have_pl) return; NC.have_pl = 0; Player *pl = SKB.pl;
	if (NC.respawn || *SKB.pdead > 0) return;
	unsigned short s = NC.cmd_ack; if (ng_seq_gt(s, NC.seq) || (unsigned short)(NC.seq - s) >= NG_NCMD - 1) return;
	const Player *mine = &NC.pst[s % NG_NCMD], *H = &NC.hostpl; V3 d = vsub(H->pos, mine->pos); float e2 = vdot(d, d);
	if (e2 < 2.f * 2.f && H->grounded == mine->grounded && H->crouched == mine->crouched && (H->mantle_t > 0) == (mine->mantle_t > 0)) return;
	Player t = *H; float keep_yaw = pl->yaw; int crouch = pl->in_crouch;
	for (unsigned short q = (unsigned short)(s + 1); q != (unsigned short)(NC.seq + 1); q++) { ng_cmd_apply(&t, &NC.cmd[q % NG_NCMD]); NC.pst[q % NG_NCMD] = t; }
	V3 jump = vsub(pl->pos, t.pos); float jl = sqrtf(vdot(jump, jump));
	if (jl < 300) { NC.err[0] += jump.x; NC.err[1] += jump.y; NC.err[2] += jump.z; }
	t.yaw = keep_yaw; t.in_crouch = crouch; *pl = t; NGS.corr++; NGS.replays++; if (jl > NGS.corr_max) NGS.corr_max = jl;
}
static void net_client_camera(float *pos, float dt) {   // the camera eases out a correction over ~0.1 s
	if (!net_client_on()) return; float k = expf(-dt / 0.06f); for (int c = 0; c < 3; c++) { NC.err[c] *= k; pos[c] += NC.err[c]; }
}
static void ng_puppet_shot(Bot *b) {
	const BotWpn *W = &BOT_WPN[b->wpn]; b->flash_t = 0.05f;
	if (W->kind) ca_play(&b->ca.fire, ca_c(&b->ca, S_FIRE), 0.03f, 0.15f, CA_UPPER, 1); else ca_fire(&b->ca);
	snd_wfire_tp(W->sh, &b->muzzle.x);
	V3 dir = v3(cosf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->aim_pitch)); float h = raycast(b->muzzle, dir, 8000, NULL, NULL);
	if (bot_on_shot) bot_on_shot(b->muzzle, vadd(b->muzzle, vmul(dir, h > 0 ? h : 8000)));
}
static void ng_puppet_die(Bot *b, int info) {
	int kind = info & 3, side = (info >> 2) & 3, head = (info >> 4) & 1; ChrType *C = &chrt[b->ct];
	b->state = BS_DEAD; b->dead_t = 0; b->grabbed = 0; b->melee_t = -1; b->knife_t = 0; b->gren_t = 0; b->pl.in_crouch = 0; b->hold = 0;
	ca_die(&b->ca, head, side, kind); bot_death_clear(b);
	b->xoff = ca_xlast; if (b->xoff && bot_on_destruct) bot_on_destruct((int)(b - bots), b->xoff, v3(-cosf(b->pl.yaw), -sinf(b->pl.yaw), 0));   // b80 (bparts.h)
	static const char *de[3] = { "foley/common/death_back", "foley/common/death_knee", "foley/common/death_spin" };
	const char *fe = kind == 2 ? (head ? "foley/common/destruct_head" : b->xoff == 2 || b->xoff == 3 ? "foley/common/destruct_arm" : b->xoff >= 4 ? "foley/common/destruct_leg" : "foley/common/destruct_fullbody") : side == 1 ? de[1] : de[rand() % 3];
	ev_play_ex(ev_find(fe), &b->pl.pos.x, -1, 0, snd_chr_female(C->name) ? 0.4f : 0.1f, 1.f); snd_voice(C->name, head ? "death_large_3rd" : "death_3rd", &b->pl.pos.x);
}
static void ng_puppet_revive(Bot *b) {
	b->state = BS_ROAM; b->dead_t = 0; b->death_drop = 0; b->death_vz = 0; b->wpn_hide = 0; b->melee_t = -1; b->knife_t = 0; b->gren_t = 0; b->grabbed = 0; b->hold = 0; b->hold_was = 0;
	ca_revive(&b->ca); b->sk.cmask = 0; b->sk.cinv = 0; b->xoff = 0; ca_upper(&b->ca, S_SWAPEND, 0.01f, 0.2f, 1); b->prev_yaw = b->pl.yaw;   // (b80: the part back)
	char e[64]; snprintf(e, sizeof(e), "%s/equip_on_tp", snd_wpn(BOT_WPN[b->wpn].sh)->base); ev_play(e, &b->pl.pos.x);
	light_at(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 20, b->lit); b->floor_z = b->pl.pos.z;
}
static float ng_lerp_ang(float a, float b, float f) { float d = wrap_pi(b - a); return a + d * f; }
// before bots_update: every puppet from the snapshots (100 ms behind), our own state from the newest one
static void net_client_pre(float dt) {
	if (!net_client_on()) return; NG_T0();
	if (!NC.rn) { NG_T1(); return; }
	const NgSnap *L = &NC.ring[(NC.rn - 1) % 24];
	// the match: clock, scores, the end
	*NGB.match_t = NC.hdr_time; score_l = NG.my_team == 0 ? NC.sA : NC.sB; score_r = NG.my_team == 0 ? NC.sB : NC.sA;
	if (NC.hdr_phase == 2 && *NGB.match_over_t <= 0) *NGB.match_t = 0.01f;   // the host ended the match: ours ends this frame (main.c's end code, the synced scores)
	if (*NGB.match_over_t > 0) *NGB.match_t = 0;
	if (NC.hdr_phase == 0) { *SKB.notify = "WAITING FOR PLAYERS"; *SKB.notify_t = 0; *SKB.notify_pts = 0; *NGB.match_t = NC.hdr_time; }
	// our own actor (the newest snapshot)
	{ const NgA *me = &L->a[NG.me];
	  if (!NC.first) { NC.first = 1; NC.own_spawns = me->spawns; NC.own_alive = me->flags & NAF_ALIVE; NC.respawn = 1; NC.rpos = v3(me->x, me->y, me->z); NC.ryaw = me->yaw; }
	  if (me->spawns != NC.own_spawns) { NC.own_spawns = me->spawns; NC.respawn = 1; NC.rpos = v3(me->x, me->y, me->z); NC.ryaw = me->yaw; }
	  if (NC.respawn && (me->flags & NAF_ALIVE)) {   // the host put us somewhere new (start, respawn): no prediction across it
		Player *pl = SKB.pl; float yaw = NC.ryaw; int was_dead = *SKB.pdead > 0;
		memset(pl, 0, sizeof(*pl)); pl->pos = NC.rpos; pl->yaw = yaw;
		NC.respawn = 0; NC.err[0] = NC.err[1] = NC.err[2] = 0; NC.have_pl = 0;
		if (was_dead) { NC.respawn = 0; net_respawn_now = 1; } }
	  if (*SKB.pdead <= 0) { *SKB.php = me->hp; *SKB.pap = me->ap; }
	  if (!(me->flags & NAF_ALIVE) && *SKB.pdead <= 0 && NC.own_alive) { *SKB.php = 0; *SKB.pdead = 3.5f; ev_play("ui/dead", NULL); (*NGB.m_pdeaths)++; }   // (dead without a feed entry: fell out of the map ...)
	  NC.own_alive = me->flags & NAF_ALIVE; }
	ng_reconcile();
	// the render time and the two snapshots around it
	double rt = ng_lms() + NC.off - NG_INTERP_MS; int n = NC.rn < 24 ? NC.rn : 24; const NgSnap *s0 = NULL, *s1 = NULL;
	for (int k = 0; k < n; k++) { const NgSnap *S = &NC.ring[(NC.rn - 1 - k) % 24]; if (S->ms <= rt) { s0 = S; break; } s1 = S; }
	if (!s0) s0 = &NC.ring[(NC.rn - (n < 24 ? n : 24)) % 24], s1 = NULL;
	float f = s1 && s1->ms > s0->ms ? (float)((rt - s0->ms) / (s1->ms - s0->ms)) : 0; if (f < 0) f = 0; if (f > 1) f = 1;
	for (int a = 0; a < NG.nact; a++) { if (a == NG.me) continue; int j = ng_local(a); if (j < 0 || j >= nbot) continue; Bot *b = &bots[j];
		const NgA *A = &s0->a[a], *B = s1 ? &s1->a[a] : A; float g = A->spawns == B->spawns && (A->flags & NAF_ALIVE) == (B->flags & NAF_ALIVE) ? f : 0;
		if (!s1 && rt > s0->ms && (A->flags & NAF_ALIVE)) {   // past the newest: a little extrapolation (at most 100 ms)
			float ex = (float)fmin(0.1, (rt - s0->ms) / 1000.0); b->pl.pos = v3(A->x + A->vx * ex, A->y + A->vy * ex, A->z + (A->flags & NAF_GROUND ? 0 : A->vz * ex)); }
		else b->pl.pos = v3(A->x + (B->x - A->x) * g, A->y + (B->y - A->y) * g, A->z + (B->z - A->z) * g);
		b->pl.vel = v3(A->vx + (B->vx - A->vx) * g, A->vy + (B->vy - A->vy) * g, A->vz + (B->vz - A->vz) * g);
		b->pl.yaw = ng_lerp_ang(A->yaw, B->yaw, g); b->aim_pitch = A->pitch + (B->pitch - A->pitch) * g;
		// character, weapon, team, name
		int ct = ng_ct_of_op(A->chr); if (!NC.p_init[a] || ct != b->ct) { if (ct != b->ct) { bot_set_char(b, ct); b->wpn = -1; } }
		if (A->wpn != b->wpn && A->wpn < NBOT_WPN) { int w = A->wpn; b->wpn = -1; ng_set_wpn(b, w); if (b->wpn != w) b->wpn = w; }
		b->team = A->team ^ NG.my_team; b->kills = A->kills; b->deaths = A->deaths; b->net_score = A->score; b->hp = A->hp;
		if ((A->flags & NAF_HUMAN) && NG.human[a]) snprintf(bot_label[j], sizeof(bot_label[j]), "%s", NG.name[a]); else bot_label[j][0] = 0;
		// stance and movement state for chr.h, and the one-frame events
		int fl = A->flags, pf = NC.p_flags[a]; b->pl.events = 0;
		b->pl.grounded = (fl & NAF_GROUND) != 0; b->pl.crouched = (fl & (NAF_CROUCH | NAF_SLIDE)) != 0; b->pl.sprinting = (fl & NAF_SPRINT) != 0;
		b->pl.slide_t = fl & NAF_SLIDE ? fmaxf(0.01f, b->pl.slide_t + dt) : 0; b->net_ads = (fl & NAF_ADS) != 0;
		if (fl & NAF_MANTLE) { b->pl.mantle_len = 1; b->pl.mantle_t = fmaxf(0.001f, 1 - A->mantle_k / 255.f); b->pl.mantle_kind = A->flags2 & 3; } else { b->pl.mantle_t = 0; b->pl.mantle_kind = 0; }
		b->hold = (A->flags2 & 16) != 0; b->hold_kind = (A->flags2 >> 2) & 3; b->hold_yaw = b->pl.yaw; b->hold_pitch = b->aim_pitch;
		if (NC.p_init[a]) {
			if ((fl & NAF_GROUND) && !(pf & NAF_GROUND)) { b->pl.events |= PE_LAND; b->pl.land_vz = NC.p_vz[a]; }
			if (!(fl & NAF_GROUND) && (pf & NAF_GROUND) && A->vz > 100) b->pl.events |= PE_JUMP;
			if ((fl & NAF_SLIDE) && !(pf & NAF_SLIDE)) b->pl.events |= PE_SLIDE;
			if ((fl & NAF_MANTLE) && !(pf & NAF_MANTLE)) b->pl.events |= (A->flags2 & 3) == 2 ? PE_VAULT : PE_CLIMB; }
		NC.p_flags[a] = fl; NC.p_vz[a] = A->vz;
		// life: deaths, respawns; actions: shots, hits, reloads, knife / throw
		int alive = (fl & NAF_ALIVE) != 0;
		if (!NC.p_init[a]) { NC.p_init[a] = 1; NC.p_shots[a] = A->shots; NC.p_hurts[a] = A->hurts; NC.p_rel[a] = A->reloads; NC.p_acts[a] = A->acts; NC.p_spawns[a] = A->spawns; NC.p_alive[a] = alive;
			if (!alive && b->state != BS_DEAD) { b->state = BS_DEAD; b->dead_t = 9; ca_die(&b->ca, 0, 0, 0); } else if (alive && b->state == BS_DEAD) ng_puppet_revive(b); continue; }
		if (A->spawns != NC.p_spawns[a] || (alive && !NC.p_alive[a])) { NC.p_spawns[a] = A->spawns; if (alive) ng_puppet_revive(b); }
		if (!alive && NC.p_alive[a] && b->state != BS_DEAD) ng_puppet_die(b, A->death);
		NC.p_alive[a] = alive;
		if (alive && b->state != BS_DEAD) {
			int ds = (A->shots - NC.p_shots[a]) & 255; if (ds > 3) ds = 3; for (int k = 0; k < ds; k++) ng_puppet_shot(b);
			if (((A->hurts - NC.p_hurts[a]) & 255) && b->reload_t <= 0) { ca_hit(&b->ca, 0); if (frand() < 0.4f) snd_voice(chrt[b->ct].name, "damage_3rd", &b->pl.pos.x); b->hurt_t = 1.5f; }
			if ((A->reloads - NC.p_rel[a]) & 255) { if (BOT_WPN[b->wpn].kind == 1) ca_reload_shells(&b->ca, 4); else ca_reload(&b->ca, 0); }
			if ((A->acts - NC.p_acts[a]) & 255) { if (A->act_kind == 1) { float Lk = ca_play_from(&b->ca, &b->ca.up, "knife", S_FIRE, 0.06f, 0.15f, CA_UPPER); if (Lk <= 0) Lk = 0.5f; b->melee_t = 0; b->knife_t = Lk; b->wpn_hide = Lk; ca_stop(&b->ca.fire); ev_play("weapon/knife/swing_tp", &b->pl.pos.x); }
				else if (A->act_kind == 2) bot_anim_throw(j); } }
		NC.p_shots[a] = A->shots; NC.p_hurts[a] = A->hurts; NC.p_rel[a] = A->reloads; NC.p_acts[a] = A->acts; }
	ng_sk_apply();   // skills: the host's states, deployables and missiles (after skills_update)
	ng_tk_apply();   // Think Tanks (before tk_update)
	NG_T1();
}
// our shot (client): the rays go to the host with the time we saw the others at
static float ng_pel[12][2]; static int ng_npel;
static void net_client_pellet(V3 dir) { if (ng_npel < 12) { ng_pel[ng_npel][0] = atan2f(dir.y, dir.x); ng_pel[ng_npel][1] = asinf(fmaxf(-1, fminf(1, dir.z))); ng_npel++; } }
static void net_client_shot(V3 eye, int wd) {
	if (!net_client_on() || !ng_npel) { ng_npel = 0; return; }
	unsigned char e[NG_EVMAX]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_SHOT); nw32(&w, (unsigned)fmax(0, ng_lms() + NC.off - NG_INTERP_MS)); nw8(&w, wd); nw8(&w, ng_npel);
	nwf(&w, eye.x); nwf(&w, eye.y); nwf(&w, eye.z);
	for (int k = 0; k < ng_npel; k++) { float y = fmodf(ng_pel[k][0], 6.2831853f); if (y < 0) y += 6.2831853f; nw16(&w, (int)(y * (65536 / 6.2831853f)) & 0xFFFF); nw16(&w, ng_clamp16(ng_pel[k][1] * 10000)); }
	ngq_push(&NC.out, e, w.n); ng_npel = 0;
}
static void net_client_melee(V3 eye, float yaw, float pitch, int stealth_bot) {
	if (!net_client_on()) return; unsigned char e[NG_EVMAX]; NetW w = { e, 0, sizeof(e) };
	nw8(&w, NGE_MELEE); nw8(&w, stealth_bot >= 0 ? 1 : 0); nw8(&w, stealth_bot >= 0 ? ng_actor_of_bot(stealth_bot) : 255); nwf(&w, eye.x); nwf(&w, eye.y); nwf(&w, eye.z);
	float y = fmodf(yaw, 6.2831853f); if (y < 0) y += 6.2831853f; nw16(&w, (int)(y * (65536 / 6.2831853f)) & 0xFFFF); nw16(&w, ng_clamp16(pitch * 10000)); ngq_push(&NC.out, e, w.n);
}
static void net_client_gren(const Grenade *g) {
	if (!net_client_on()) return; unsigned char e[NG_EVMAX]; NetW w = { e, 0, sizeof(e) }; nw8(&w, NGE_GREN);
	for (int k = 0; k < 3; k++) nwf(&w, g->p[k]); for (int k = 0; k < 3; k++) nwf(&w, g->v[k]); nwf(&w, g->fuse); ngq_push(&NC.out, e, w.n);
}
static void net_client_post(void) {   // end of the frame: the input packet when due
	if (!net_client_on()) return; NG_T0(); double now = net_now();
	if (now - NC.send_t >= NG_SNAP_DT - 0.002) { NC.send_t = now;
		unsigned char pk[NET_PMAX]; NetW w; net_hdr(&w, pk, NP_INPUT, NL.room); nw32(&w, (unsigned)ng_lms()); nw16(&w, NC.snap_last); nw16(&w, NC.in_last);
		int nc = NC.ncmd < 4 ? NC.ncmd : 4; nw8(&w, nc);
		for (int k = nc - 1; k >= 0; k--) { const NgCmd *m = &NC.cmd[(unsigned short)(NC.seq - k) % NG_NCMD]; nw16(&w, m->seq); nw16(&w, m->dt); nw8(&w, (unsigned char)m->mx); nw8(&w, (unsigned char)m->my); nw8(&w, m->btn); nw16(&w, m->yaw); nw16(&w, (unsigned short)m->pitch); nw8(&w, m->spd); nw8(&w, m->wpn); nw8(&w, m->rel); }
		ngq_write(&w, &NC.out, w.n + 480); NGS.ev_out += NC.out.n;   // (at most ~480 B of events a packet: a backlog drains over a few)
		net_send(&NL.host, pk, w.n); NGS.inputs++; }
	NG_T1();
}

// =========================================================== match start / end (both)
// After main.c's match setup (bots made and spawned, teams by its own rule): the room's actors take over
static void net_match_setup(void) {
	memset(&NG, 0, sizeof(NG)); NG.on = 0; if (!(NL.role == NL_HOST || NL.role == NL_CLIENT) || NL.state != NR_MATCH) return;
	NG.on = 1; NG.host = NL.role == NL_HOST; NG.t0 = net_now(); NG.nact = 0;
	for (int k = 0; k < NL.nslot && NG.nact < NG_MAXA; k++) if (NL.slot[k].used || NL.set.bots) { int a = NG.nact++; NG.slot[a] = k; NG.human[a] = NL.slot[k].used; snprintf(NG.name[a], sizeof(NG.name[a]), "%s", NL.slot[k].name); if (k == NL.my_slot) NG.me = a; }
	NG.my_team = NL.my_slot & 1;
	if (nbot != NG.nact - 1) bots_set_count(NG.nact - 1);
	nbot = NG.nact - 1; memset(bot_label, 0, sizeof(bot_label)); memset(ngc, 0, sizeof(ngc)); memset(&NC, 0, sizeof(NC)); memset(&NGS, 0, sizeof(NGS)); ng_hn = ng_hw = 0; ng_snap_seq = 0; ng_snap_t = 0;
	BotTarget T = { SKB.pl->pos, SKB.pl->vel, 1 };
	for (int j = 0; j < nbot; j++) { Bot *b = &bots[j]; int a = ng_actor_of_bot(j); b->team = ng_abs_team(a) ^ NG.my_team; b->netc = b->netp = 0; b->kills = b->deaths = 0;
		if (!NG.host) { b->netp = 1; continue; }
		bot_spawn(b, &T); }   // (the host: teams changed, so each bot spawns again on its own side)
	if (NG.host) { for (int i = 0; i < NET_MAXPEER; i++) if (NL.peer[i].used) { int a = ng_actor_of_slot(NL.peer[i].slot); if (a >= 0) ng_host_attach(a, i); }
		ng_waiting = 1; ng_wait_t = net_now(); bot_freeze = 1; score_limit = cfg.mode == MODE_TDM ? cfg.score_limit : 0; }
	else { score_limit = 0; nfeed = 0; }
	net_game_rx = ng_rx; net_game_slot_left = ng_host_slot_left; net_game_slot_joined = ng_host_slot_joined;
	logp("net: match setup as %s: %d actors (me %d, GHOST %c), %d remote players\n", NG.host ? "host" : "client", NG.nact, NG.me, 'A' + NG.my_team, NG.host ? net_humans(-1) - 1 : 0);
	for (int a = 0; a < NG.nact; a++) logp("  actor %d: slot %d, GHOST %c, %s%s\n", a, NG.slot[a], 'A' + ng_abs_team(a), NG.human[a] ? NG.name[a] : "bot", a == NG.me ? " (me)" : "");
}
static void net_match_end(void) {   // the match's end screens begin (host: the room waits for the results)
	if (!NG.on) return; logp("net: match over (%s)\n", NG.host ? "host" : "client");
	if (NG.host && NL.state == NR_MATCH) { NL.state = NR_RESULTS; NL.lseq++; }
}
static void net_back_to_room(void) {   // leaving the end screens
	if (NL.role == NL_HOST && NL.state == NR_RESULTS) { NL.state = NR_LOBBY; for (int k = 0; k < NL.nslot; k++) if (NL.slot[k].used && !NL.slot[k].host) NL.slot[k].ready = 0; NL.lseq++; }
	NG.on = 0; for (int j = 0; j < NBOT_MAX; j++) { bots[j].netc = bots[j].netp = 0; bot_label[j][0] = 0; }
}
static int net_match_lost(void) {   // a client lost its host during the match: back to the AD-HOC window
	if (!NG.on || NG.host) return 0; if (NL.role == NL_CLIENT && NL.state != NR_LOBBY) return 0;
	if (NL.role == NL_CLIENT && NL.state == NR_LOBBY && !NL.lost) return 0;
	return NL.lost || NL.role != NL_CLIENT;
}
static void net_line(void) {   // once a second in a network match: the "net:" line (boot.log)
	if (!NG.on) return; float fr = NGS.frames ? (float)NGS.frames : 1; unsigned busy = __atomic_exchange_n(&NL.busy_us, 0, __ATOMIC_RELAXED), wakes = __atomic_exchange_n(&NL.wakes, 0, __ATOMIC_RELAXED);
	if (NG.host) { char pb[256]; int o = 0; pb[0] = 0;
		for (int i = 0; i < NET_MAXPEER; i++) if (ngc[i].on) { const NetPeer *P = &NL.peer[ngc[i].peer]; o += snprintf(pb + o, sizeof(pb) - o, " | %s out %.1f KB/s in %.1f KB/s (%u/%u pkt)", P->name, P->s_tx_b / 1024.f, P->s_rx_b / 1024.f, P->s_tx_p, P->s_rx_p); if (o >= (int)sizeof(pb)) break; }
		logp("net: host %.3f ms/frame main, I/O thread %u us/s (%u wakes), %d snapshots avg %d B, %d inputs, %d shots, %d hits, events in %d, ring drops %u/%u%s\n",
			NGS.us / 1000.f / fr, busy, wakes, NGS.snaps, NGS.snaps ? NGS.snap_bytes / NGS.snaps : 0, NGS.inputs, NGS.shots_in, NGS.hits, NGS.ev_in, NL.in.drops, NL.out.drops, pb); }
	else logp("net: client %.3f ms/frame main, I/O thread %u us/s (%u wakes), rtt %.0f ms, %d snapshots avg %d B, out %.1f KB/s in %.1f KB/s, %d corrections (max %.1f cm), events in %d, %d unacked, interp %.0f ms behind\n",
		NGS.us / 1000.f / fr, busy, wakes, NC.rtt, NGS.snaps, NGS.snaps ? NGS.snap_bytes / NGS.snaps : 0, NL.s_tx_b / 1024.f, NL.s_rx_b / 1024.f, NGS.corr, NGS.corr_max, NGS.ev_in, NC.out.n, NG_INTERP_MS);
	memset(&NGS, 0, sizeof(NGS));
}
