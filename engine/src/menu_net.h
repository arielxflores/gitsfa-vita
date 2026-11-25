// net (stage 1): the AD-HOC screens of the front end (included by menu_fe.h). Three screens:
//   FE_NET      the AD-HOC window: connection (AD-HOC / LAN), the rooms found nearby (host, mode, map, players), HOST ROOM
//   FE_NETHOST  the room's settings (the custom match rows): mode, map, score, time, team size (1 v 1 .. 4 v 4), bots (off / skill)
//   FE_NETROOM  the waiting room (the FE_MATCHING look): the room on the left, GHOST A / GHOST B with the real players, their
//               READY state and the bots that fill the empty places; READY (players) / START (host); team change; START IN 3
// The screens know nothing about sockets: the net code (net_lobby.h) fills NUV every frame and acts through NUI. With no net code
// (the host menu test) NUI is empty and NUV holds whatever the test puts there. Vita prompts and touch, the legibility sizes.
#pragma once
#define NUV_ROOMS 6
#define NUV_SLOTS 8
typedef struct { char host[20]; int mode, map, team_size, humans, maxh, bots, state, other_build, ping; } NuvRoom;   // state: 0 lobby, 1 starting, 2 in a match
typedef struct { int used, human, ready, me, host, connected; int team, chr; char name[20]; } NuvSlot;
typedef struct {
	int phase;                       // 0 off, 1 bringing the network up, 2 browsing, 3 joining, 4 in a room (host or client)
	int transport;                   // 0 ad-hoc, 1 LAN, 2 internet (b108)
	char code[16], note[72];         // b108, the host of an INTERNET room: its room code ("" until known) and a line about the port
	char code_in[16];                // b108, INTERNET browsing: the room code being looked for ("" none yet)
	int is_host;
	char status[64];                 // the connection line (scanning, joining ...)
	int nroom; NuvRoom room[NUV_ROOMS];
	// the room (host: being set up / open; client: joined)
	int mode, map, team_size, bots, skill, score_limit, time_limit; char host[20];
	NuvSlot slot[NUV_SLOTS]; int nslot;   // nslot = 2 x team size; slot k is on team k & 1 (GHOST A even, GHOST B odd)
	float countdown;                 // >= 0: START IN
	int all_ready;                   // host: everyone is ready (START enabled)
	char alert[64], alert2[96];      // a message for the player (version, room full, host left ...): shown until dismissed
} NetUiView;
static NetUiView NUV;
static struct {
	void (*open)(int transport);         // the AD-HOC window opened (bring the network up, start browsing)
	void (*close)(void);                  // left the AD-HOC window for the lobby (network down)
	void (*set_transport)(int t);
	int (*host)(const MenuConfig *c);     // create the room with c's settings (mode, map, limits, team size, bot skill / NUV.bots)
	void (*settings)(const MenuConfig *c);   // the host changed the room's settings (not used yet: the room keeps its settings)
	void (*join)(int room);
	void (*ready)(int on);
	void (*team)(int team);               // move to that team (abs: 0 GHOST A, 1 GHOST B)
	void (*start)(void);                  // host: START (countdown)
	void (*leave)(void);                  // leave the room (host: close it) and browse again
	int (*poll_start)(MenuConfig *c);     // the countdown has ended: c holds the match settings, return 1 to start
	void (*code_enter)(void);             // b108, INTERNET: type a room code (the system keyboard)
} NUI;
// ------------------------------------------------------------------ AD-HOC window
static int fnet_hsel;   // host settings: the bots stepper (0 off, 1..4 = EASY..VETERAN)
static void fe_net_open(void) {
	if (NUV.phase == 4) { menu_fe_open(FE_NETROOM); return; }   // still in a room (back from OPERATIVE / ARMORY)
	menu_fe_open(FE_NET); MFE.sel[FE_NET] = NUV.nroom ? 0 : 10; if (NUI.open) NUI.open(NUV.transport);
}
static void fe_net_badge(int screen, const char **l1, const char **l2) {
	static char b[16]; int nh = 0; for (int i = 0; i < NUV.nslot; i++) nh += NUV.slot[i].human;
	*l1 = NUV.transport == 2 ? "INTERNET" : NUV.transport ? "LAN" : "AD-HOC";
	if (screen == FE_NETROOM) { snprintf(b, sizeof(b), "%d/%d", nh, NUV.nslot); *l2 = b; } else *l2 = screen == FE_NETHOST ? "CREATE" : "ROOMS";
}
static const char *fe_net_tname(int t) { return t == 2 ? "INTERNET" : t ? "LAN" : "AD-HOC"; }
static void fe_net_alert_draw(void) {
	if (!NUV.alert[0]) return;
	float dim[4] = { 0, 0, 0, 0.55f }; mu_rect(0, 0, 960, 544, dim);
	static const float band[4] = { 0.03f, 0.08f, 0.10f, 0.97f }, t0[4] = { 0.12f, 0.27f, 0.31f, 0.98f }, t1[4] = { 0.06f, 0.15f, 0.18f, 0.98f };
	mu_rect(0, 148, 960, 396, band); mu_rect(0, 148, 960, 150, MU_CYAN); mu_rect(0, 394, 960, 396, MU_CYAND);
	mu_rectv(0, 150, 960, 204, t0, t1); mu_textbox(0, 150, 960, 204, MU_H2, 2, 0, MU_WHITE, NUV.alert);
	if (NUV.alert2[0]) mu_wrapa(480, 240, 860, MU_BODY, 32, 2, 2, MU_WHITE, NUV.alert2);
	int id = MU_ID(MU_K_CONFIRM, 0); mu_button(390, 306, 570, 370, "OK", MU_H2 * 0.9f, 1, mu_pressing(id), MU_BTN_YES, 0); mu_reg(id, 384, 300, 576, 376);
	{ float c[4] = { 0.01f, 0.03f, 0.04f, 1 }; mu_rect(0, MU_HINT_BAR, 960, 544, c); } mu_hints("x:OK", MU_HINT_Y);
}
static int fe_net_alert_update(unsigned p) {   // 1 while the alert is up (it takes the input)
	if (!NUV.alert[0]) return 0;
	if ((p & (MB_OK | MB_BACK | MB_START)) || mu_tapped(MU_ID(MU_K_CONFIRM, 0))) { NUV.alert[0] = 0; NUV.alert2[0] = 0; MENU_SFX("ui/click_basic"); }
	return 1;
}
#define FNET_ROWY(k) (114 + (k) * 84)
static void fe_net_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_NET]; mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.88f }, hd0[4] = { 0.07f, 0.16f, 0.19f, 0.95f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, box[4] = { 0.03f, 0.08f, 0.10f, 0.9f };
	mu_rect(10, 64, 652, 510, pan); mu_rectv(10, 64, 652, 104, hd0, hd1); mu_rect(10, 104, 652, 106, MU_CYAND);
	mu_text(24, 96, MU_H2, 0, MU_WHITE, fe_net_tname(NUV.transport));
	{ float k = NUV.phase == 2 ? 0.6f + 0.4f * sinf(MFE.t * 5) : 1, cc[4] = { MU_CYAN[0], MU_CYAN[1], MU_CYAN[2], k }; mu_textbox(170, 64, 644, 104, MU_FINE, 1, 4, cc, NUV.status[0] ? NUV.status : "OFFLINE"); }
	// the rooms found (72 px rows, 12 px apart); a tap selects, a second tap / Cross joins
	int n = NUV.nroom < 3 ? NUV.nroom : 3;
	for (int k = 0; k < n; k++) { const NuvRoom *R = &NUV.room[k]; float y0 = FNET_ROWY(k), y1 = y0 + 72; int id = MU_ID(MU_K_LIST, k), on = sel == k, pr = mu_pressing(id), off = R->other_build || R->humans >= R->maxh;
		mu_rect(22, y0, 642, y1, on ? MU_ROWSEL : MU_ROWL); if (on) { mu_rect(22, y0, 27, y1, MU_CYAN); mu_frame(22, y0, 642, y1, 1.5f, MU_CYAND); } if (pr) mu_frame(20, y0 - 2, 644, y1 + 2, 2, MU_WHITE);
		float m[4] = { 1, 1, 1, off ? 0.5f : 0.95f }; mu_imgfit(MI(MENU_MODE_EMB[R->mode]), 34, y0 + 8, 90, y1 - 8, m);
		mu_textbox(100, y0 + 2, 500, y0 + 38, MU_BODY, 0, 0, off ? MU_OFF : MU_WHITE, R->host);
		char b[64]; snprintf(b, sizeof(b), "%s  -  %s", MENU_MODE_NAME[R->mode], MENU_MAP[R->map].name); mu_textbox(100, y0 + 36, 476, y1 - 4, MU_FINE, 0, 0, MU_DIM, b);
		snprintf(b, sizeof(b), "%d/%d", R->humans, R->maxh); mu_textbox(500, y0 + 2, 632, y0 + 38, MU_BODY, 1, 4, off ? MU_OFF : MU_WHITE, b);
		mu_textbox(480, y0 + 36, 632, y1 - 4, MU_FINE, 1, 4, R->other_build ? MU_RED : R->state == 2 ? MU_YELLOW : MU_CYAN, R->other_build ? "OTHER BUILD" : R->state == 2 ? "IN MATCH" : R->humans >= R->maxh ? "FULL" : "OPEN");
		mu_reg(id, 22, y0, 642, y1); }
	if (!n) { mu_rect(22, 114, 642, 354, box); mu_frame(22, 114, 642, 354, 1, MU_CYAND);
		if (NUV.transport == 2 && NUV.phase >= 2) {   // b108: the room is found by its code (a tap on the box or Square types one)
			int ic = MU_ID(MU_K_ITEM, 0); char b[48]; if (mu_pressing(ic)) mu_frame(20, 112, 644, 356, 2, MU_WHITE);
			mu_wrapa(332, 176, 560, MU_BODY, 34, 2, 2, MU_DIM, NUV.code_in[0] ? "LOOKING FOR THE ROOM..." : "ENTER THE HOST'S ROOM CODE");
			if (NUV.code_in[0]) { snprintf(b, sizeof(b), "%s", NUV.code_in); mu_textbox(22, 206, 642, 254, MU_H2, 2, 0, MU_WHITE, b); }
			mu_wrapa(332, 290, 560, MU_SMALL, 28, 2, 2, MU_OFF, NUV.code_in[0] ? "It appears here when the host's room is open." : "The host reads it in the waiting room.");
			mu_reg(ic, 22, 114, 642, 354); }
		else {
		mu_wrapa(332, 200, 560, MU_BODY, 34, 2, 2, MU_DIM, NUV.phase == 1 ? "CONNECTING..." : NUV.phase >= 2 ? "NO ROOMS FOUND YET" : NUV.transport == 2 ? "INTERNET IS OFF" : NUV.transport ? "LAN IS OFF" : "AD-HOC IS OFF");
		mu_wrapa(332, 284, 560, MU_SMALL, 28, 2, 2, MU_OFF, NUV.phase >= 2 ? "Rooms of nearby players appear here. Host a room for others to join." : ""); } }
	mu_wrap(26, 386, 616, MU_FINE, 24, 2, MU_OFF, NUV.transport == 2 ? "INTERNET: a friend's room anywhere, found by its room code." : NUV.transport ? "LAN: players on the same Wi-Fi network." : "AD-HOC: PS Vita systems near each other, no access point needed.");
	{ int it = MU_ID(MU_K_BTN, 1), ih = MU_ID(MU_K_BTN, 0); char b[32]; snprintf(b, sizeof(b), "CONNECTION: %s", fe_net_tname(NUV.transport));
	  mu_button(22, 432, 322, 504, b, MU_BODY, sel == 11, mu_pressing(it), MU_BTN, 0); mu_reg(it, 22, 432, 322, 504);
	  mu_button(334, 432, 642, 504, "HOST ROOM", MU_H2, sel == 10, mu_pressing(ih), MU_BTN_GO, NUV.phase < 2); if (NUV.phase >= 2) mu_reg(ih, 334, 432, 642, 504); }
	mu_a = 1;
	fe_name_tag(c, 806, 70);
	fe_topbar(FE_NET); fe_hints(NUV.transport == 2 ? (n ? "x:JOIN|s:ROOM CODE|t:CONNECTION|o:BACK" : "x:HOST|s:ROOM CODE|t:CONNECTION|o:BACK") : n ? "d:MOVE|x:JOIN|t:CONNECTION|o:BACK" : "x:HOST|t:CONNECTION|o:BACK");
	fe_net_alert_draw();
}
// ------------------------------------------------------------------ host settings
static const char *FNET_ROW[6] = { "GAME MODE", "MAP", "SCORE LIMIT", "TIME LIMIT", "TEAM SIZE", "BOTS" };
static void fe_net_host_value(const MenuConfig *c, int row, char *v, int n) {
	if (row == 4) snprintf(v, n, "%d VS %d", c->team_size, c->team_size);
	else if (row == 5) snprintf(v, n, "%s", fnet_hsel ? MENU_SKILL[(fnet_hsel - 1) & 3] : "OFF");
	else fe_row_value(c, row, v, n);
}
static void fe_net_host_change(MenuConfig *c, int row, int d) {
	if (row == 4) { c->team_size = c->team_size + d; if (c->team_size < 1) c->team_size = 4; if (c->team_size > 4) c->team_size = 1; MFE.ci[4] = 0; return; }
	if (row == 5) { fnet_hsel = mwrap(fnet_hsel + d, 5); if (fnet_hsel) c->bot_skill = fnet_hsel - 1; return; }
	int ts = c->team_size; fe_custom_change(c, row, d); c->team_size = ts;   // (the custom match rows; the team size is the room's own 1..4)
}
static void fe_net_host_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_NETHOST]; mu_a = a;
	{ static const float pan[4] = { 0.02f, 0.06f, 0.07f, 0.88f }; mu_cut(14, 66, 380, 510, 0, 16, 0, 0, pan);
	  int id = MU_ID(MU_K_ITEM, 0), pr = mu_pressing(id); fe_thumb(c->map, 24, 84, 370, 278, mu_a); mu_frame(24, 84, 370, 278, pr ? 3 : 1.5f, pr ? MU_WHITE : MU_CYAND);
	  float sb[4] = { 0, 0, 0, 0.65f }; mu_rect(24, 236, 370, 278, sb); mu_textbox(24, 236, 330, 278, MU_BODY, 0, 10, MU_WHITE, MENU_MAP[c->map].name); mu_tri(350, 257, 16, 1, MU_CYAN); mu_reg(id, 24, 84, 370, 278); }
	mu_imgfit(MI(MENU_MODE_EMB[c->mode]), 20, 290, 98, 368, NULL);
	mu_textbox(106, 290, 376, 322, MU_BODY, 0, 2, MU_WHITE, MENU_MODE_NAME[c->mode]);
	mu_wrap(108, 328, 266, MU_SMALL, 26, 3, MU_DIM, MENU_MODE_GOAL[c->mode]);
	{ char b[96]; snprintf(b, sizeof(b), "%s ROOM. UP TO %d PLAYERS; %s", fe_net_tname(NUV.transport), c->team_size * 2, fnet_hsel ? "BOTS FILL THE EMPTY PLACES." : "NO BOTS.");
	  mu_wrap(26, 420, 344, MU_SMALL, 26, 3, MU_OFF, b); }
	for (int i = 0; i < 6; i++) { float x0, y0, x1, y1; char v[48]; fe_net_host_value(c, i, v, sizeof(v)); fe_custom_rect(i, &x0, &y0, &x1, &y1);
		if (i < 2) fe_stepper_row(x0, y0, x1, y1, 600, i, FNET_ROW[i], v, i == sel, 0); else fe_stepper_cell(x0, y0, x1, y1, i, FNET_ROW[i], v, i == sel); }
	{ int ib = MU_ID(MU_K_BTN, 1), is = MU_ID(MU_K_BTN, 0);
	  mu_button(392, 432, 640, 504, "BACK", MU_BODY, sel == 7, mu_pressing(ib), MU_BTN, 0); mu_reg(ib, 392, 432, 640, 504);
	  mu_button(656, 432, 944, 504, "CREATE ROOM", MU_H2, sel == 6, mu_pressing(is), MU_BTN_GO, 0); mu_reg(is, 656, 432, 944, 504); }
	mu_a = 1;
	fe_topbar(FE_NETHOST); fe_hints("d:CHANGE|x:CREATE|o:BACK");
	fe_net_alert_draw();
}
// ------------------------------------------------------------------ waiting room
static int fe_net_me(void) { for (int i = 0; i < NUV.nslot; i++) if (NUV.slot[i].me) return i; return -1; }
static void fe_net_room_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.88f }, hd1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, row[4] = { 0.06f, 0.14f, 0.17f, 0.9f }, me[4] = { 0.12f, 0.42f, 0.46f, 0.95f }, rdy[4] = { 0.08f, 0.26f, 0.29f, 0.95f };
	// the room (left): map, mode, settings
	mu_rect(10, 64, 214, 510, pan); mu_rect(10, 64, 214, 100, hd1); mu_textbox(18, 64, 210, 100, MU_SMALL, 0, 0, MU_WHITE, NUV.is_host ? "YOUR ROOM" : "ROOM");
	fe_thumb(NUV.map, 18, 108, 206, 204, mu_a); mu_frame(18, 108, 206, 204, 1.5f, MU_CYAND);
	{ float sb[4] = { 0, 0, 0, 0.6f }; mu_rect(18, 172, 206, 204, sb); mu_textbox(18, 172, 206, 204, MU_FINE, 2, 8, MU_WHITE, MENU_MAP[NUV.map].name); }
	mu_imgfit(MI(MENU_MODE_EMB[NUV.mode]), 18, 212, 66, 260, NULL); mu_wrapa(140, 230, 136, MU_FINE, 22, 2, 2, MU_YELLOW, MENU_MODE_NAME2[NUV.mode]);
	{ char b[48]; float y = 290; static const char *U[MODE_COUNT] = { "KILLS", "ROUNDS", "POINTS", "", "ROUNDS", "ROUNDS" };
	  snprintf(b, sizeof(b), "%d VS %d", NUV.team_size, NUV.team_size); mu_textbox(18, y - 26, 206, y + 4, MU_TEXT, 0, 2, MU_WHITE, b); y += 34;
	  snprintf(b, sizeof(b), "BOTS: %s", NUV.bots ? MENU_SKILL[NUV.skill & 3] : "OFF"); mu_textbox(18, y - 26, 206, y + 4, MU_FINE, 0, 2, MU_DIM, b); y += 30;
	  if (NUV.score_limit > 0) { snprintf(b, sizeof(b), "%d %s", NUV.score_limit, U[NUV.mode % MODE_COUNT]); mu_textbox(18, y - 26, 206, y + 4, MU_FINE, 0, 2, MU_DIM, b); y += 30; }
	  if (NUV.time_limit > 0) { snprintf(b, sizeof(b), "%d MIN", NUV.time_limit); mu_textbox(18, y - 26, 206, y + 4, MU_FINE, 0, 2, MU_DIM, b); y += 30; }
	  snprintf(b, sizeof(b), "%s", fe_net_tname(NUV.transport)); mu_textbox(18, y - 26, 206, y + 4, MU_FINE, 0, 2, MU_CYAN, b); y += 30;
	  mu_textbox(18, 440, 206, 470, MU_FINE, 0, 2, MU_OFF, "HOST"); mu_textbox(18, 466, 206, 500, MU_TEXT, 0, 2, MU_WHITE, NUV.host); }
	// the teams (centre): real players, bots, open places; a tap on the other list moves you there
	mu_rect(222, 64, 652, 510, pan); mu_rect(222, 64, 652, 100, hd1);
	{ int nh = 0; for (int i = 0; i < NUV.nslot; i++) nh += NUV.slot[i].human; char b[48]; snprintf(b, sizeof(b), "WAITING ROOM  -  %d/%d PLAYERS", nh, NUV.nslot); mu_textbox(222, 64, 652, 100, MU_SMALL, 2, 0, MU_DIM, b); }
	int mi = fe_net_me(), myteam = mi >= 0 ? mi & 1 : 0;
	{ static const float cA[4] = { 0.45f, 0.85f, 0.95f, 1 }, cB[4] = { 1, 0.45f, 0.42f, 1 };
	  mu_text(232, 134, MU_H2, 0, cA, "A"); mu_text(258, 132, MU_TEXT, 0, MU_WHITE, "GHOST"); mu_text(642, 134, MU_H2, 1, cB, "B"); mu_text(614, 132, MU_TEXT, 1, MU_WHITE, "GHOST");
	  for (int t = 0; t < 2; t++) { float lx0 = t ? 440 : 230, lx1 = lx0 + 204; int lid = MU_ID(MU_K_LIST, t);
		if (t != myteam && mu_pressing(lid)) mu_frame(lx0 - 3, 141, lx1 + 3, 144 + 4 * 58 + 1, 2, MU_WHITE);
		for (int i = 0; i < NUV.team_size && i < 4; i++) { int k = i * 2 + t; if (k >= NUV.nslot) break; const NuvSlot *S = &NUV.slot[k]; float x0 = lx0, x1 = lx1, y0 = 144 + i * 58, y1 = y0 + 52; char b[24];
			float app = msmooth((MFE.st - 0.1f * (i + t * 0.5f)) / 0.3f); if (app <= 0) continue; float mm = mu_a; mu_a *= app;
			int bot = !S->human && NUV.bots, open = !S->human && !NUV.bots;
			mu_rect(x0, y0, x1, y1, S->me ? me : S->human && S->ready ? rdy : row);
			if (S->human || bot) { snprintf(b, sizeof(b), "op.face.%d", MENU_CHR_FACE[S->chr % 12]); float fm[4] = { 1, 1, 1, bot ? 0.55f : 1 }; mu_imgfit(MI(b), x1 - 52, y0 + 2, x1 - 2, y1 - 2, fm); }
			mu_textbox(x0 + 8, y0 + 2, x1 - 56, y0 + 30, MU_TEXT, 0, 0, open ? MU_OFF : bot ? MU_DIM : MU_WHITE, S->human ? S->name : bot ? MENU_CHR[S->chr % 12].name : "OPEN");
			const char *tag = S->human ? (S->host ? "HOST" : !S->connected ? "LEFT" : S->ready ? "READY" : "NOT READY") : bot ? "BOT" : "";
			const float *tc = S->host ? MU_YELLOW : S->human && S->ready ? MU_CYAN : MU_OFF;
			if (*tag) mu_textbox(x0 + 8, y0 + 28, x1 - 56, y1, MU_FINE, 0, 0, tc, tag);
			if (S->me) { float yb[4] = { 0.02f, 0.06f, 0.08f, 0.85f }; mu_rect(x1 - 52, y1 - 22, x1 - 2, y1 - 2, yb); mu_textbox(x1 - 52, y1 - 22, x1 - 2, y1 - 2, MU_FINE, 2, 0, MU_WHITE, "YOU"); }   // (on the face: the tag line keeps its room)
			mu_a = mm; }
		if (t != myteam) mu_reg(lid, lx0, 140, lx1, 144 + 4 * 58); } }
	if (NUV.transport == 2 && NUV.is_host) {   // b108: the room code the host reads to the friends, and how the port stands
		char b[40]; snprintf(b, sizeof(b), "ROOM CODE  %s", NUV.code[0] ? NUV.code : "..."); mu_textbox(222, 378, 652, 408, MU_TEXT, 2, 0, NUV.code[0] ? MU_YELLOW : MU_DIM, b);
		mu_textbox(226, 406, 648, 430, MU_FINE, 2, 0, MU_DIM, NUV.note); }
	// the band: READY (players) / START (host)
	{ int id = MU_ID(MU_K_BTN, 0), pr = mu_pressing(id);
	  if (NUV.is_host) { if (NUV.all_ready) { mu_button(230, 432, 644, 504, "START", MU_H2, 1, pr, MU_BTN_GO, 0); mu_reg(id, 230, 432, 644, 504); }
		  else { mu_rect(222, 432, 652, 504, hd1); mu_rect(222, 432, 652, 434, MU_CYAND); mu_textbox(222, 432, 652, 504, MU_TEXT, 2, 6, MU_DIM, "WAITING FOR PLAYERS TO BE READY..."); } }
	  else { int r = mi >= 0 && NUV.slot[mi].ready; mu_button(230, 432, 644, 504, r ? "READY  -  CANCEL" : "READY", MU_H2, 1, pr, r ? MU_BTN : MU_BTN_YES, 0); mu_reg(id, 230, 432, 644, 504); } }
	mu_a = 1;
	fe_name_tag(c, 806, 70);
	fe_topbar(FE_NETROOM);
	if (NUV.countdown >= 0) {   // START IN (the matchmaking's countdown look)
		float dim[4] = { 0, 0, 0, 0.6f }; mu_rect(0, 0, 960, 544, dim); int nn = NUV.countdown > 0 ? (int)ceilf(NUV.countdown) : 0; float f = nn > 0 ? NUV.countdown - (nn - 1) : 0, k = nn > 0 ? msmooth((1 - f) / 0.25f) : 1;   // b59: 3, 2, 1, 0 (0 held)
		float gl[4] = { 0.25f, 0.75f, 0.85f, 0.35f }, ring[4] = { 0.70f, 0.92f, 0.97f, 0.85f }, band[4] = { 0.04f, 0.10f, 0.12f, 0.95f }, core[4] = { 0.05f, 0.25f, 0.30f, 0.95f };
		mu_disc(480, 250, 120, gl); fe_logo9(480, 250, 120, ring); mu_disc(480, 250, 52, core);
		char b[4]; snprintf(b, sizeof(b), "%d", nn); float sz = 64 * (1.3f - 0.3f * k), cc[4] = { 1, 1, 1, 0.4f + 0.6f * k }; mu_text(480, 250 + sz * MU_CAP * 0.5f, sz, 2, cc, b);
		mu_rect(380, 318, 580, 360, band); mu_textbox(380, 318, 580, 360, MU_TEXT, 2, 0, MU_WHITE, "START IN");
	} else fe_hints(NUV.is_host ? (NUV.all_ready ? "x:START|t:TEAM|o:CLOSE ROOM" : "t:TEAM|o:CLOSE ROOM") : "x:READY|t:TEAM|o:LEAVE");
	fe_net_alert_draw();
}
// ------------------------------------------------------------------ input
static int fe_net_update(MenuConfig *c, unsigned p, float dt) {
	(void)dt; int *sel = &MFE.sel[MFE.screen], r = FE_NONE;
	if (fe_net_alert_update(p)) return r;
	switch (MFE.screen) {
	case FE_NET: {
		int n = NUV.nroom < 3 ? NUV.nroom : 3;
		if (*sel != 10 && *sel != 11 && *sel >= n) *sel = n ? n - 1 : 10;
		if (p & MB_UP) *sel = *sel >= 10 ? (n ? n - 1 : *sel) : *sel > 0 ? *sel - 1 : *sel;
		if (p & MB_DOWN) *sel = *sel < 10 ? (*sel + 1 < n ? *sel + 1 : 10) : *sel;
		if (p & (MB_LEFT | MB_RIGHT)) { if (*sel >= 10) *sel = *sel == 10 ? 11 : 10; }
		int act = fe_pick_tap(MU_K_LIST, sel, n);
		if (p & MB_TRI || mu_tapped(MU_ID(MU_K_BTN, 1))) { NUV.transport = (NUV.transport + 1) % 3; if (NUI.set_transport) NUI.set_transport(NUV.transport); MENU_SFX("ui/tab_on"); }   // (b108: AD-HOC / LAN / INTERNET)
		if (NUV.transport == 2 && NUV.phase >= 2 && ((p & MB_SQR) || mu_tapped(MU_ID(MU_K_ITEM, 0))) && NUI.code_enter) { NUI.code_enter(); MENU_SFX("ui/window_open_basic"); }
		if (p & MB_OK) { if (*sel < n) act = *sel; else if (*sel == 11) { NUV.transport = (NUV.transport + 1) % 3; if (NUI.set_transport) NUI.set_transport(NUV.transport); } else if (NUV.phase >= 2) { menu_fe_open(FE_NETHOST); MFE.sel[FE_NETHOST] = 6; fe_custom_init(c); fnet_hsel = 1 + (c->bot_skill & 3); if (c->team_size < 1 || c->team_size > 4) c->team_size = 2; } }
		if (mu_tapped(MU_ID(MU_K_BTN, 0)) && NUV.phase >= 2) { MENU_SFX("ui/window_open_basic"); menu_fe_open(FE_NETHOST); MFE.sel[FE_NETHOST] = 6; fe_custom_init(c); fnet_hsel = 1 + (c->bot_skill & 3); }
		if (act >= 0 && act < n && NUI.join) { NUI.join(act); MENU_SFX("ui/click_game_start"); }
		if (p & MB_BACK) { if (NUI.close) NUI.close(); menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_ADHOC; }
		break; }
	case FE_NETHOST: {
		if (p & MB_UP) *sel = *sel == 7 ? 5 : *sel == 6 ? 5 : mwrap(*sel - 1, 6);
		if (p & MB_DOWN) *sel = *sel == 5 ? 6 : *sel >= 6 ? 0 : *sel + 1;
		if (p & (MB_LEFT | MB_RIGHT)) { int d = p & MB_LEFT ? -1 : 1; if (*sel < 6) fe_net_host_change(c, *sel, d); else *sel = *sel == 6 ? 7 : 6; }
		if (MUT.press) { int k = MU_KIND(MUT.id), i = MU_IX(MUT.id); if ((k == MU_K_ROW || k == MU_K_ROWL || k == MU_K_ROWR) && i < 6 && *sel != i) { *sel = i; MENU_SFX("ui/mouse_over_basic"); } }
		for (int i = 0; i < 6; i++) {
			if (mu_tapped(MU_ID(MU_K_ROWL, i)) || mu_repeat(MU_ID(MU_K_ROWL, i), dt)) { fe_net_host_change(c, i, -1); MENU_SFX("ui/mouse_over_basic"); }
			if (mu_tapped(MU_ID(MU_K_ROWR, i)) || mu_repeat(MU_ID(MU_K_ROWR, i), dt)) { fe_net_host_change(c, i, 1); MENU_SFX("ui/mouse_over_basic"); } }
		if (mu_tapped(MU_ID(MU_K_ITEM, 0))) { fe_net_host_change(c, 1, 1); MENU_SFX("ui/mouse_over_basic"); }
		int go = mu_tapped(MU_ID(MU_K_BTN, 0)) || (p & MB_START) || ((p & MB_OK) && *sel == 6), back = (p & MB_BACK) || mu_tapped(MU_ID(MU_K_BTN, 1)) || ((p & MB_OK) && *sel == 7);
		if ((p & MB_OK) && *sel < 6) *sel = 6;
		if (back) { menu_fe_open(FE_NET); MFE.sel[FE_NET] = 10; break; }
		if (go) { NUV.bots = fnet_hsel > 0; if (fnet_hsel) c->bot_skill = fnet_hsel - 1; MENU_SFX("ui/click_game_start");
			if (NUI.host && NUI.host(c) == 0) { menu_fe_open(FE_NETROOM); } }
		break; }
	case FE_NETROOM: {
		int mi = fe_net_me();
		if (NUV.countdown < 0) {
			if ((p & MB_TRI) && mi >= 0 && NUI.team) { NUI.team((mi & 1) ^ 1); MENU_SFX("ui/tab_on"); }
			for (int t = 0; t < 2; t++) if (mu_tapped(MU_ID(MU_K_LIST, t)) && NUI.team) { NUI.team(t); MENU_SFX("ui/tab_on"); }
			int band = (p & MB_OK) || mu_tapped(MU_ID(MU_K_BTN, 0)) || (NUV.is_host && (p & MB_START));
			if (band) { if (NUV.is_host) { if (NUV.all_ready && NUI.start) { NUI.start(); MENU_SFX("ui/click_game_start"); } }
				else if (mi >= 0 && NUI.ready) { NUI.ready(!NUV.slot[mi].ready); MENU_SFX(NUV.slot[mi].ready ? "ui/character_cancel" : "ui/click_basic"); } }
			if (p & MB_BACK) { if (NUI.leave) NUI.leave(); menu_fe_open(FE_NET); MFE.sel[FE_NET] = 10; MENU_SFX("ui/window_close_basic"); break; }
		}
		if (NUI.poll_start && NUI.poll_start(c)) { c->tut = 0; menu_ld_tut = 0; r = FE_START; }
		if (NUV.phase < 4 && MFE.screen == FE_NETROOM) { menu_fe_open(FE_NET); MFE.sel[FE_NET] = 10; }   // the room is gone (the alert says why)
		break; }
	}
	if (MFE.screen == FE_NET && NUV.phase == 4) menu_fe_open(FE_NETROOM);   // joined
	return r;
}
