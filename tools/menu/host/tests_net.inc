	// net (stage 1): the AD-HOC screens with a made-up view (menu_net.h: the screens only read NUV)
	{
	menu_config_default(&cfg); MFE.t = 2.0f; MFE.st = 1.0f;
	menu_fe_open(FE_LOBBY); MFE.sel[FE_LOBBY] = LB_ADHOC; fe_frame(0, 1.0f); fe_shot("fe_lobby_adhoc");
	memset(&NUV, 0, sizeof(NUV)); NUV.countdown = -1; NUV.phase = 2; NUV.transport = 0; snprintf(NUV.status, sizeof(NUV.status), "SCANNING  -  AD-HOC");
	NUV.nroom = 3;
	{ static const char *H[3] = { "MOTOKO-VITA", "BATOU", "TOGUSA_99" }; static const int M[3] = { MODE_TDM, MODE_DEM, MODE_TTM }, P[3] = { 11, 6, 10 }, NH[3] = { 2, 3, 8 }, ST[3] = { 0, 2, 0 };
	  for (int k = 0; k < 3; k++) { snprintf(NUV.room[k].host, 20, "%s", H[k]); NUV.room[k].mode = M[k]; NUV.room[k].map = P[k]; NUV.room[k].team_size = 4; NUV.room[k].humans = NH[k]; NUV.room[k].maxh = 8; NUV.room[k].bots = 1; NUV.room[k].state = ST[k]; }
	  NUV.room[2].other_build = 1; }
	menu_fe_open(FE_NET); MFE.sel[FE_NET] = 0; fe_frame(0, 1.0f); fe_shot("fe_net");
	NUV.nroom = 0; MFE.sel[FE_NET] = 10; fe_frame(0, 1.0f); fe_shot("fe_net_empty");
	menu_fe_open(FE_NETHOST); MFE.sel[FE_NETHOST] = 6; fe_custom_init(&cfg); cfg.team_size = 4; fnet_hsel = 2; fe_frame(0, 1.0f); fe_shot("fe_nethost");
	// the waiting room: host view, 3 v 3 with two players, bots fill
	NUV.phase = 4; NUV.is_host = 1; NUV.mode = MODE_TDM; NUV.map = 11; NUV.team_size = 3; NUV.bots = 1; NUV.skill = 1; NUV.score_limit = 50; NUV.time_limit = 10; snprintf(NUV.host, 20, "MOTOKO-VITA");
	NUV.nslot = 6; memset(NUV.slot, 0, sizeof(NUV.slot));
	for (int k = 0; k < 6; k++) { NUV.slot[k].team = k & 1; NUV.slot[k].chr = (k * 5 + 3) % 12; }
	NUV.slot[0].human = NUV.slot[0].used = 1; NUV.slot[0].ready = 1; NUV.slot[0].host = 1; NUV.slot[0].me = 1; NUV.slot[0].connected = 1; NUV.slot[0].chr = 1; snprintf(NUV.slot[0].name, 20, "MOTOKO-VITA");
	NUV.slot[1].human = NUV.slot[1].used = 1; NUV.slot[1].ready = 0; NUV.slot[1].connected = 1; NUV.slot[1].chr = 0; snprintf(NUV.slot[1].name, 20, "BATOU");
	NUV.slot[2].human = NUV.slot[2].used = 1; NUV.slot[2].ready = 1; NUV.slot[2].connected = 1; NUV.slot[2].chr = 3; snprintf(NUV.slot[2].name, 20, "TOGUSA_99");
	NUV.all_ready = 0; menu_fe_open(FE_NETROOM); fe_frame(0, 1.0f); fe_shot("fe_netroom_host");
	NUV.slot[1].ready = 1; NUV.all_ready = 1; fe_frame(0, 0.1f); fe_shot("fe_netroom_host_ready");
	// the client's view (BATOU), then START IN
	NUV.is_host = 0; NUV.slot[0].me = 0; NUV.slot[1].me = 1; NUV.slot[1].ready = 0; NUV.all_ready = 0; fe_frame(0, 0.1f); fe_shot("fe_netroom_client");
	NUV.countdown = 2.4f; fe_frame(0, 0.1f); fe_shot("fe_netroom_count"); NUV.countdown = -1;
	menu_fe_open(FE_NET); NUV.phase = 2; NUV.nroom = 0; snprintf(NUV.alert, sizeof(NUV.alert), "DIFFERENT BUILD"); snprintf(NUV.alert2, sizeof(NUV.alert2), "This room runs build b48, this game is b49. Both players need the same build.");
	fe_frame(0, 1.0f); fe_shot("fe_net_alert"); NUV.alert[0] = 0;
	// b108: INTERNET - no code yet, a code being looked for, the host's waiting room with its room code
	memset(&NUV, 0, sizeof(NUV)); NUV.countdown = -1; NUV.phase = 2; NUV.transport = 2; snprintf(NUV.status, sizeof(NUV.status), "INTERNET  -  NO ROOM CODE YET");
	menu_fe_open(FE_NET); MFE.sel[FE_NET] = 10; fe_frame(0, 1.0f); fe_shot("fe_inet_nocode");
	snprintf(NUV.code_in, sizeof(NUV.code_in), "666D-J0KE-M6R"); snprintf(NUV.status, sizeof(NUV.status), "SEARCHING  -  INTERNET"); fe_frame(0, 0.1f); fe_shot("fe_inet_search");
	NUV.phase = 4; NUV.is_host = 1; NUV.mode = MODE_TDM; NUV.map = 11; NUV.team_size = 4; NUV.bots = 1; NUV.skill = 1; NUV.score_limit = 50; NUV.time_limit = 10; snprintf(NUV.host, 20, "MOTOKO-VITA");
	NUV.nslot = 8; for (int k = 0; k < 8; k++) { NUV.slot[k].team = k & 1; NUV.slot[k].chr = (k * 5 + 3) % 12; }
	NUV.slot[0].human = NUV.slot[0].used = 1; NUV.slot[0].ready = 1; NUV.slot[0].host = 1; NUV.slot[0].me = 1; NUV.slot[0].connected = 1; NUV.slot[0].chr = 1; snprintf(NUV.slot[0].name, 20, "MOTOKO-VITA");
	snprintf(NUV.code, sizeof(NUV.code), "666D-J0KE-M6R"); menu_fe_open(FE_NETROOM);
	{ static const char *NT[] = { "PORT NOT OPEN: FORWARD UDP 47750 ON ROUTER", "PORT 47750 OPENED BY YOUR ROUTER", "PORT OPEN, BEHIND 2 ROUTERS: MAY NOT WORK", "SHARED PROVIDER ADDRESS: SAME WI-FI ONLY", "NO INTERNET ADDRESS FOUND: SAME WI-FI ONLY", "ASKING YOUR ROUTER TO OPEN THE PORT..." };
	  for (int k = 0; k < 6; k++) { char nm[32]; snprintf(nm, sizeof(nm), k ? "fe_inet_room%d" : "fe_inet_room", k); snprintf(NUV.note, sizeof(NUV.note), "%s", NT[k]); if (k == 5) NUV.code[0] = 0; fe_frame(0, k ? 0.1f : 1.5f); fe_shot(nm); } }
	memset(&NUV, 0, sizeof(NUV)); NUV.countdown = -1; menu_fe_open(FE_LOBBY);
	}
