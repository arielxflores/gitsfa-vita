// net (test): a scripted room member for the multi-instance tests (tools/hostgame/nettest.sh; works on the Vita too). It runs
// when ux0:data/gitsfa/netauto.txt exists: one line, the role and key=value settings, e.g.
//   host lan players=3 mode=0 map=11 team=2 bots=1 time=3 score=50 matches=1 quit_after=0
//   join lan team=1 leave_after=0 matches=1
// role host|join, transport lan|adhoc; players = humans the host waits for (it starts when they are all READY), mode / map =
// MODE_* / MAPS[] index, team = players per team, bots 0|1 (fill), time / score = limits, matches = matches before quitting,
// leave_after / quit_after = seconds into the first match when a client leaves / the host closes the room (0 never); op = the
// operator (cfg.chr, skills.h SK_* order: 0 arm launcher, 4 sentry, 7 shield, 11 drone ...); fast=1 (host): Terminal Conquest
// runs 3x and every Tachikoma is ready at once (as the autoplay tour); fast=2 also hacks and destroys an enemy tank.
// The player in the match is the autoplay soldier (fights the nearest enemy, walks the navigation mesh).
#pragma once
static struct { int on, host, lan, players, mode, map, team, bots, time, score, matches, played, want_team; float leave_after, quit_after, t, rt; int st, ready_sent, op, fast, probe, room_said; char code[40]; } NA;
// b123 (the user's first internet test: the PC never saw the room code): the host also puts its room code and its address in
// the Wi-Fi into ux0:data/gitsfa/netroom.txt, a 256-byte file the test reads while the game runs (the log cannot be downloaded
// then: it changes between the reader's 16 KB pieces). Fixed size, as every small file the writer thread puts in place.
static void na_room_file(const char *state) {
	char lan[40] = "", b[256]; NetAddr me; if (NL.tr && NL.up == 2) { NL.tr->self(&me); NL.tr->str(&me, lan, sizeof(lan)); }
	memset(b, ' ', sizeof(b)); b[sizeof(b) - 1] = '\n';
	int n = snprintf(b, sizeof(b) - 1, "state %s\ncode %s\nlan %s\nnote %s\nend\n", state, NUV.code[0] ? NUV.code : "-", lan[0] ? lan : "-", NUV.note[0] ? NUV.note : "-");
	if (n > 0 && n < (int)sizeof(b) - 1) b[n] = ' ';
	alog_file_put(VDIR "netroom.txt", b, sizeof(b));
}
static int net_auto_load(void) {
	FILE *f = fopen(VDIR "netauto.txt", "rb");
	if (!f) {   // b108, test switch inettest (an empty file, gitsfa-check.sh pass 12): open an INTERNET room alone, log what the router and the STUN server answered, hold the waiting room for a screenshot, close the room (the port is given back), quit
		f = fopen(VDIR "inettest", "rb"); if (!f) return 0; fclose(f);
		memset(&NA, 0, sizeof(NA)); NA.on = 1; NA.host = 1; NA.lan = 2; NA.players = 99; NA.mode = MODE_TDM; NA.map = 11; NA.team = 4; NA.bots = 1; NA.time = 10; NA.score = 50; NA.matches = 1; NA.want_team = -1; NA.op = -1; NA.probe = 1; return 1; }
	char line[256] = { 0 }; if (!fgets(line, sizeof(line), f)) line[0] = 0; fclose(f);
	memset(&NA, 0, sizeof(NA)); NA.on = 1; NA.lan = 1; NA.players = 2; NA.mode = MODE_TDM; NA.map = 11; NA.team = 2; NA.bots = 1; NA.time = 3; NA.score = 50; NA.matches = 1; NA.want_team = -1; NA.op = -1;
	for (char *t = strtok(line, " \t\r\n"); t; t = strtok(NULL, " \t\r\n")) { int v = 0; float fv = 0;
		if (!strcmp(t, "host")) NA.host = 1; else if (!strcmp(t, "join")) NA.host = 0; else if (!strcmp(t, "lan")) NA.lan = 1; else if (!strcmp(t, "adhoc")) NA.lan = 0; else if (!strcmp(t, "inet")) NA.lan = 2; else if (!strncmp(t, "code=", 5)) snprintf(NA.code, sizeof(NA.code), "%s", t + 5);   /* (b108: inet [code=ROOM-CODE | code=ip:port]) */
		else if (sscanf(t, "players=%d", &v) == 1) NA.players = v; else if (sscanf(t, "mode=%d", &v) == 1) NA.mode = v; else if (sscanf(t, "map=%d", &v) == 1) NA.map = v;
		else if (sscanf(t, "team=%d", &v) == 1) NA.team = v; else if (sscanf(t, "bots=%d", &v) == 1) NA.bots = v; else if (sscanf(t, "time=%d", &v) == 1) NA.time = v;
		else if (sscanf(t, "score=%d", &v) == 1) NA.score = v; else if (sscanf(t, "matches=%d", &v) == 1) NA.matches = v; else if (sscanf(t, "side=%d", &v) == 1) NA.want_team = v; else if (sscanf(t, "op=%d", &v) == 1) NA.op = v % 12; else if (sscanf(t, "fast=%d", &v) == 1) NA.fast = v;
		else if (sscanf(t, "leave_after=%f", &fv) == 1) NA.leave_after = fv; else if (sscanf(t, "quit_after=%f", &fv) == 1) NA.quit_after = fv; }
	return 1;
}
// the front end: returns the buttons to press, < 0 when the test is over
static int na_was_on;   // (b116: was a static inside net_auto_match that never went back to 0 - a host's matches=2 counted one match and played on for ever)
static int net_auto_menu(MenuConfig *c, float dt, int results, unsigned *mp) {
	NA.t += dt; NA.rt += dt; na_was_on = 0;
	if (results) { if (NA.rt > 4) { NA.rt = 0; *mp = MB_OK; } return 0; }
	switch (NA.st) {
	case 0: if (NA.op >= 0) c->chr = NA.op;   // op = the operator (skills.h SK_* order)
		if (NA.t > 1.5f) { menu_fe_open(FE_LOBBY); NA.st = 1; NA.t = 0; } break;
	case 1: if (NA.t > 0.5f) { NUV.transport = NA.lan; fe_net_open(); NA.st = 2; NA.t = 0; logp("netauto: AD-HOC window (%s)\n", NA.lan == 2 ? "internet" : NA.lan ? "LAN" : "ad-hoc"); if (NA.host) na_room_file("starting"); } break;
	case 2:
		if (NUV.phase < 2) { if (NA.t > 20) { logp("netauto: the network did not come up\n"); return -1; } break; }
		if (NA.lan == 2 && !NA.host && NA.code[0] && !NUV.code_in[0]) net_code_set(NA.code, 0);   // (b108)
		if (NA.host) { c->mode = NA.mode; c->map = NA.map; c->team_size = NA.team; c->time_limit = NA.time; c->score_limit = NA.score; c->bot_skill = 1; NUV.bots = NA.bots;
			if (NUI.host(c) == 0) { menu_fe_open(FE_NETROOM); NA.st = 3; NA.t = 0; } else return -1; }
		else if (NUV.nroom > 0) { NUI.join(0); NA.st = 3; NA.t = 0; }
		else if (NA.t > 30) { logp("netauto: no room found in 30 s\n"); return -1; }
		break;
	case 3:   // in the room
		if (NA.host && NA.room_said < (NUV.code[0] ? 2 : 1)) { NA.room_said = NUV.code[0] ? 2 : 1; na_room_file(NUV.code[0] ? "open" : "open-no-code"); }   // (b123)
		if (!NA.host && NUV.phase < 3) { if (NA.played >= NA.matches || NA.t > 25) { logp("netauto: out of the room (%s)\n", NUV.alert[0] ? NUV.alert : "left"); return -1; } NA.st = 2; NA.t = 0; break; }
		if (NA.host && NA.probe) {
			if (NA.probe == 1 && (NUV.code[0] || NA.t > 45)) { NA.probe = 2; NA.t = 0; if (NUV.code[0]) logp("inettest: ROOM CODE %s - %s\n", NUV.code, NUV.note); else logp("inettest: no room code after 45 s (%s)\n", NUV.note); }
			if (NA.probe == 2 && NA.t > 14) { NA.probe = 3; NA.t = 0; NUI.leave(); logp("inettest: room closed (a port the router opened is given back)\n"); }
			if (NA.probe == 3 && NA.t > 5) { logp("autoplay finished\n"); return -1; }
			break; }
		if (NA.host) { int nh = 0; for (int i = 0; i < NUV.nslot; i++) nh += NUV.slot[i].human;
			if (NA.played >= NA.matches) { if (NA.t > 3) { NUI.leave(); logp("netauto: %d matches played: closing the room\n", NA.played); return -1; } break; }
			if (nh >= NA.players && NUV.all_ready && NUV.countdown < 0 && NA.t > 2) { logp("netauto: %d players ready: START\n", nh); NUI.start(); NA.t = 0; }
			if (NA.t > 120) { logp("netauto: waited 120 s for %d players (%d here)\n", NA.players, nh); return -1; } }
		else if (NUV.phase == 4) { int mi = fe_net_me();
			if (NA.played >= NA.matches) { if (NA.t > 3) { NUI.leave(); logp("netauto: %d matches played: leaving\n", NA.played); return -1; } break; }
			if (mi >= 0 && NA.want_team >= 0 && (mi & 1) != NA.want_team && NA.t > 1) { NUI.team(NA.want_team); NA.want_team = -1; }
			if (mi >= 0 && !NUV.slot[mi].ready && NA.t > 2) { NUI.ready(1); logp("netauto: READY\n"); NA.t = 0; } }
		if (MFE.screen != FE_NETROOM && NUV.phase == 4) menu_fe_open(FE_NETROOM);
		break;
	}
	return 0;
}
// in a match: 1 = leave now (the pause menu's QUIT)
static int net_auto_match(float map_t, float match_over_t) {
	if (NG.on && !na_was_on) { NA.played++; logp("netauto: match %d\n", NA.played); NA.t = 0; } na_was_on = NG.on;
	if (match_over_t > 0) return 0;
	if (NA.host && NA.fast >= 2 && ttm_on) {   // (host) fast=2: the tank tests - a hack of the first enemy tank at 50 s, its end at 110 s
		static int st; if (map_t < 5) st = 0; int k = -1; for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i) && tk[i].team != tk_local_team) k = i;
		if (k >= 0 && st == 0 && map_t > 50 && tk[k].state == TK_LIVE) { st = 1; int a = -1; for (int j = 0; j < nbot; j++) if (bots[j].team != tk[k].team && bots[j].state != BS_DEAD) a = 1 + j;
			if (a > 0) { bots[a - 1].pl.pos = v3(tk[k].pl.pos.x + 200, tk[k].pl.pos.y, tk[k].pl.pos.z + 5); tk_hack_begin(k, a); logp("netauto: actor %d hacks tank %d\n", a, k); } }
		if (k >= 0 && st == 1 && map_t > 110) { st = 2; tk_damage(k, 1e6f, 0, tk[k].sph[0], v3(0, 0, -1), SKD_BLAST); logp("netauto: tank %d destroyed\n", k); } }
	if (!NA.host && NA.leave_after > 0 && map_t > NA.leave_after) { NA.leave_after = 0; logp("netauto: leaving the match at %.0f s\n", map_t); NA.played = NA.matches; return 1; }
	if (NA.host && NA.quit_after > 0 && map_t > NA.quit_after) { NA.quit_after = 0; logp("netauto: closing the room at %.0f s\n", map_t); NA.played = NA.matches; return 1; }
	return 0;
}

static void net_auto_ttm_start(void) {   // (host) fast=1: the terminals at 3x, every Tachikoma ready
	if (!NA.on || !NA.fast || (NG.on && !NG.host)) return; tm.t = TTM_REBOOT - 4; tm.speed = 3;
	for (int a = 0; a < TTM_NACT; a++) { tm.ready[a] = 1; tm.gauge[a] = 1; tm.ready_t[a] = 3 + a; } logp("netauto: Terminal Conquest fast\n");
}
