	// front-end and results (menu pack) on their own backdrops
	{
	menu_config_default(&cfg);
	MFE.t = 2.0f; MFE.st = 1.7f;   // (fe_shot's frame adds 0.5 s: drawn at 2.2 s)
	fe_shot("fe_title");
	MFE.st = -0.08f; MFE.t = 0.42f; fe_shot("fe_title_reveal"); MFE.st = 0.35f; fe_shot("fe_title_flash"); MFE.st = 2.25f; MFE.t = 2.75f; fe_shot("fe_title_shine"); MFE.st = 2.2f;   // b59: the title's effects (at 0.42, 0.85, 2.75 s)
	fe_frame(MB_OK, 1.0f); fe_frame(0, 1.0f); fe_frame(0, 1.0f); fe_shot("fe_lobby");   // (b59: the title's exit takes TL_GO)
	menu_touch_inject(1, 1, 509, 170); fe_frame(0, 0.016f); fe_shot("fe_lobby_touch"); menu_touch_inject(1, 1, 509, 400); fe_frame(0, 0.016f); menu_touch_inject(1, 0, 509, 400); fe_frame(0, 0.016f); menu_touch_inject(0, 0, 0, 0); fe_frame(0, 0.016f);   // finger on the OPERATIVE tile (pressed look), slides off: no action
	menu_touch_inject(1, 1, 100, 30); fe_frame(0, 0.016f); fe_shot("fe_back_pressed"); menu_touch_inject(1, 1, 509, 400); fe_frame(0, 0.016f); menu_touch_inject(1, 0, 509, 400); fe_frame(0, 0.016f); menu_touch_inject(0, 0, 0, 0); fe_frame(0, 0.016f);   // ui b50: the back tab held (its light state, the Circle inside)
	MFE.sel[FE_LOBBY] = LB_QUICK; fe_frame(MB_OK, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_quick");
	fe_frame(MB_RIGHT, 0.1f); fe_frame(MB_RIGHT, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_quick_dem");
	// DIVE: SCANNING..., the waiting room (map vote, auto-balance), START IN; then back to the lobby (the host ignores FE_START)
	fe_frame(MB_LEFT, 0.1f); fe_frame(MB_OK, 0.1f); fe_frame(0, 1.2f); fe_shot("fe_dive_scan");
	fe_frame(0, 2.0f); fe_frame(0, 0.5f); fe_frame(0, 1.2f); tapid(MU_ID(MU_K_LIST, 1)); fe_frame(0, 0.6f); fe_shot("fe_match_vote");
	tapid(MU_ID(MU_K_BTN, 2)); fe_frame(0, 0.35f); fe_shot("fe_match_balance");
	fe_frame(MB_OK, 0.1f); fe_frame(0, 0.6f); fe_shot("fe_match_count");
	fe_frame(0, 3.0f); fe_frame(0, 0.1f); printf("dive: screen %d mode %d map %d\n", MFE.screen, cfg.mode, cfg.map); menu_fe_open(FE_QUICK); FDV.phase = 0; fe_frame(0, 0.5f);
	fe_frame(MB_BACK, 0.1f); MFE.sel[FE_LOBBY] = LB_TUTOR; fe_frame(MB_OK, 0.1f); fe_frame(MB_RIGHT, 0.1f); fe_frame(MB_RIGHT, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_tutorial");
	fe_frame(MB_BACK, 0.1f); MFE.sel[FE_LOBBY] = LB_CUSTOM; fe_frame(MB_OK, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_custom");
	tapid(MU_ID(MU_K_ROWR, 0)); tapid(MU_ID(MU_K_ROWR, 0)); tapid(MU_ID(MU_K_ROWR, 0)); fe_frame(0, 1.0f); fe_shot("fe_custom_em");   // GAME MODE > three times: ELIMINATION
	tapid(MU_ID(MU_K_ITEM, 0)); fe_frame(0, 1.0f); fe_shot("fe_maps");                                    // the map: map list
	fe_frame(MB_BACK, 0.1f); fe_frame(MB_LEFT, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_custom_ttm");
	for (int k = 0; k < 6 && MENU_MODE_OFFER[MFE.ci[0]] != MODE_GA; k++) tapid(MU_ID(MU_K_ROWR, 0)); fe_frame(0, 1.0f); fe_shot("fe_custom_ga"); printf("custom: mode %d (want %d: Ghost Assault)\n", MENU_MODE_OFFER[MFE.ci[0]], MODE_GA);   // b63
	menu_fe_open(FE_OPERATIVES); MFE.sel[FE_OPERATIVES] = -1; fe_frame(0, 1.0f); fe_shot("fe_operatives");
	fe_frame(MB_RIGHT, 0.1f); fe_frame(MB_DOWN, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_operatives2");
	{ // b103: the hint line at its longest - skins and the mask prompt (Square); the toggle itself
	  mfe_skin_n = t_skin_n8; mfe_skin_name = t_skin_name8; mfe_skin_gog = t_skin_gog1; fe_frame(0, 0.5f); fe_shot("fe_operatives_mask");
	  int o_ = fe_op_list[MFE.sel[FE_OPERATIVES]]; fe_frame(MB_SQR, 0.1f); fe_frame(0, 0.5f); fe_shot("fe_operatives_mask_off");
	  int g1_ = cfg.gog_off[o_]; fe_frame(MB_SQR, 0.1f); printf("mask toggle: %d after Square, %d after Square again (want non-zero, 0)\n", g1_, cfg.gog_off[o_]);
	  mfe_skin_n = NULL; mfe_skin_name = NULL; mfe_skin_gog = NULL; }
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_op_details"); fe_frame(MB_R, 0.1f); fe_frame(0, 0.5f); fe_shot("fe_op_details2");   // b63: DETAILS (the skill video, the profile)
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.5f); printf("details: open %d after Circle (want 0), screen %d\n", MFE.det_on, MFE.screen);
	menu_fe_open(FE_LOADOUT); MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 1.0f); fe_frame(MB_DOWN, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_loadout");
	fe_frame(MB_R, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_loadout2");
	{ int chr0 = cfg.chr, p0 = cfg.primary; cfg.chr = 0; cfg.primary = 0; MFE.wslot = 0; MFE.wcat = 1; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_ar");   // b89: an ASSAULT operator's AR tab (12 rifles)
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect"); printf("inspect: open %d gun %s (want 1)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_R, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect2"); fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f); printf("inspect: open %d after Circle (want 0), screen %d\n", MFE.insp, MFE.screen); cfg.chr = chr0; cfg.primary = p0; MFE.wcat = 0; }
	{ int chr0 = cfg.chr, p0 = cfg.primary; cfg.chr = 2; cfg.primary = 27; MFE.wslot = 0; MFE.wcat = 3; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_sr");   // b91: a SPECIALIST's SR tab (9 rifles)
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect_sr"); printf("inspect: open %d gun %s (want 1, CM2010)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f); cfg.chr = chr0; cfg.primary = p0; MFE.wcat = 0; }
	{ int chr0 = cfg.chr, p0 = cfg.primary;   // b121 (user: "PP-2200 seems to be missing from the armory"): where the PP-2200 is listed
	cfg.chr = 10; cfg.primary = 3; MFE.wslot = 0; MFE.wcat = 2; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_smg_inf");   // an INFILTRATOR's SMG tab
	MFE.wcat = 6; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_pdw_inf");   // her PDW tab
	MFE.wcat = 0; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_all_inf");
	cfg.chr = 2; cfg.primary = 5; MFE.wcat = 0; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_all_spec");   // a SPECIALIST's ALL tab
	MFE.wcat = 6; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_pdw_spec");
	{ int L[MENU_WPN_MAX], n; for (int ch = 0; ch < 12; ch++) { fe_wpn_chr = ch; printf("armory: chr %d class %d:", ch, menu_chr_class(ch)); for (int cat = 0; cat < FE_NWCAT; cat++) { n = fe_wpn_shown(0, cat, L); int has = 0; for (int k = 0; k < n; k++) if (!strcmp(MENU_WPN[L[k]].name, "PP-2200")) has = 1; printf(" %s %d%s", FE_WCAT[0][cat], n, has ? "(PP-2200)" : ""); } printf("\n"); } fe_wpn_chr = -1; }
	cfg.chr = chr0; cfg.primary = p0; MFE.wcat = 0; }
	{ int m0 = cfg.melee;   // b98: the MELEE slot (the DOUBLE KATAR picked) and its INSPECT view
	cfg.melee = 1; MFE.wslot = 2; MFE.wcat = 0; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_melee");
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect_melee"); printf("inspect: open %d gun %s (want 1, DOUBLE KATAR)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f); cfg.melee = m0; MFE.wslot = 0; }
	{ int chr0 = cfg.chr, p0 = cfg.primary;   // b97: an INFILTRATOR's SG tab (4 shotguns, STA-12S2 picked) and its INSPECT view
	cfg.chr = 1; cfg.primary = 46; MFE.wslot = 0; MFE.wcat = 4; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_sg");
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect_sg"); printf("inspect: open %d gun %s (want 1, STA-12S2)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f); cfg.chr = chr0; cfg.primary = p0; }
	{ int chr0 = cfg.chr, p0 = cfg.primary, s0 = cfg.secondary;   // b92: an ASSAULT's LMG tab (MG36-A picked) and the PISTOL tab (MATEBA 2008M picked)
	cfg.chr = 0; cfg.primary = 43; MFE.wslot = 0; MFE.wcat = 5; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_shot("fe_loadout_lmg");
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect_lmg"); printf("inspect: open %d gun %s (want 1, MG36-A)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f);
	cfg.secondary = 40; MFE.wslot = 1; MFE.wcat = 1; MFE.sel[FE_LOADOUT] = -1; fe_frame(0, 0.6f); fe_frame(0, 0.8f); fe_shot("fe_loadout_hg");
	fe_frame(MB_TRI, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_inspect_hg"); printf("inspect: open %d gun %s (want 1, MATEBA 2008M)\n", MFE.insp, MENU_WPN[MFE.insp_w].name);
	fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.3f); cfg.chr = chr0; cfg.primary = p0; cfg.secondary = s0; MFE.wslot = 0; MFE.wcat = 0; }
	menu_fe_open(FE_LOBBY); fe_frame(0, 0.1f); menu_fe_open(FE_OPTIONS); MFE.sel[FE_OPTIONS] = -1; fe_frame(0, 1.0f); fe_shot("fe_options");
	swipe(300, 400, 120); swipe(300, 400, 120); fe_frame(0, 1.0f); fe_frame(0, 1.0f); fe_shot("fe_options_buttons");   // swipe to the button list (sprint)
	fe_frame(MB_R, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_options_gfx");
	fe_frame(MB_R, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_options_game");
	fe_frame(MB_R, 0.1f); fe_frame(0, 1.0f); fe_shot("fe_options_sound");
	fe_frame(MB_LEFT, 0.1f); fe_frame(0, 0.5f); fe_frame(MB_BACK, 0.1f); fe_frame(0, 0.5f); fe_shot("fe_options_alert");   // a change, then CLOSE: ALERT
	fe_frame(MB_RIGHT, 0.1f); fe_frame(MB_OK, 0.1f); fe_frame(0, 0.3f); printf("options alert: screen %d vol %.2f (want the old value back)\n", MFE.screen, cfg.vol_master);
	menu_fe_open(FE_OPTIONS); MFE.ocat = 3; fe_frame(0, 0.5f);
	menu_fe_open(FE_EXIT); MFE.st = 1; fe_shot("fe_exit");
	// in-match options (no backdrop: main.c dims the game and calls menu_fe_draw only)
	menu_fe_open(FE_OPTIONS); MFE.ocat = 0; MFE.sel[FE_OPTIONS] = -1; menu_fe_update(&cfg, 0, 0, 1.0f);
	shot("pause"); menu_pause_open(); MPM.t = 1; menu_pause_draw(); done();   // b55: the START menu (hints inside the panel)
	shot("opt_ingame"); { float dim[4] = { 0, 0, 0, 0.6f }; menu_begin(menu_fe); menu_rect(0, 0, 960, 544, dim); menu_end(); } menu_fe_draw(&cfg); done();
	MenuResults rr; memset(&rr, 0, sizeof(rr)); rr.result = 0; rr.score[0] = 50; rr.score[1] = 37; rr.mode = MODE_TDM; rr.map = 5; rr.play_time = 347;
	rr.gp[0] = 120; rr.gp[1] = 40; rr.gp[2] = 60; rr.xp[0] = 240; rr.xp[1] = 80; rr.xp[2] = 120; rr.level = 5; rr.exp = 488; rr.exp_next = 1000;
	static const char *nm[8] = { "Waifu Delta", "Genroa", "Yuzuru", "Kenji", "Bot Alpha", "Bot Bravo", "Bot Charlie", "Bot Delta" };
	rr.nplayers = 8; for (int i = 0; i < 8; i++) { snprintf(rr.p[i].name, 24, "%s", nm[i]); rr.p[i].team = i / 4; rr.p[i].chr = i; rr.p[i].kills = 20 - 2 * i; rr.p[i].deaths = 5 + i; rr.p[i].score = 2300 - 250 * i; rr.p[i].me = i == 0; }
	rr.p[1].score = 2600;
	for (int i = 0; i < 8; i++) { rr.p[i].obj = 0; rr.p[i].skill = (i * 5 + 3) % 7; } rr.p[0].skill = 4;   // b58: skills used (TECHNICIAN: Kenji 6), TDM: CONTROLLER = best K/D
	{ MenuConfig cc = cfg; cc.ach = 0; cc.c_kills = 37; cc.level = 5; MenuResults r2 = rr; menu_career(&cc, &r2);   // b58: a later match, nothing new: PROGRESS shows the next achievement
	  cc.ach = 0x0Fu; cc.c_kills = 61; menu_career(&cc, &r2); r2.nach_new = 0; r2.mission_n = 2;
	  menu_results_open(&r2); MRS2.phase = MRS_TABS; MRS2.tab = 1; menu_results_update(0, 1.0f); shot("res_progress2"); menu_results_draw_bg(); menu_results_draw(); done(); }
	{ MenuConfig cc = cfg; cc.ach = 0; cc.c_kills = 0; cc.bot_skill = 3; menu_career(&cc, &rr); }   // (Veteran bots: a 4th new achievement, the "MORE" line)
	menu_results_open(&rr); menu_results_update(0, 2.0f);   // b58: the line-up (the host draws no figures: name tags where four would stand)
	{ int lo[5], lp[5]; const char *lw[5]; float lc[5]; int n = menu_results_lineup(lo, lw, lp, lc); static const float tp[5][2] = { { 480, 120 }, { 296, 133 }, { 664, 133 }, { 149, 143 }, { 811, 143 } };   // (tools/chars/host/lineup: 4 at 413 cm)
	  for (int k = 0; k < n; k++) { mrs_tag[k][0] = tp[k][0]; mrs_tag[k][1] = tp[k][1]; mrs_tagp[k] = lp[k]; } mrs_ntag = n; }
	shot("res_lineup"); menu_results_draw_bg(); menu_results_draw(); done();
	menu_results_update(MB_OK, 0.1f); menu_results_update(0, 1.0f); shot("res_topop"); menu_results_draw_bg(); menu_results_draw(); done();
	menu_results_update(MB_OK, 0.1f); menu_results_update(0, 2.0f); shot("res_results"); menu_results_draw_bg(); menu_results_draw(); done();
	menu_results_update(MB_R, 0.1f); menu_results_update(0, 1.0f); shot("res_progress"); menu_results_draw_bg(); menu_results_draw(); done();
	menu_results_update(MB_R, 0.1f); menu_results_update(0, 1.0f); shot("res_stats"); menu_results_draw_bg(); menu_results_draw(); done();
	{ // b61 (user: at the end of a run that ended in a DRAW, 0s over text and their boxes): a 0 - 0 draw, nobody scored (the tutorial's)
	  MenuResults rd = rr; rd.result = 2; rd.score[0] = rd.score[1] = 0; rd.nplayers = 6; rd.play_time = 192; rd.mission_n = 0;
	  for (int i = 0; i < 6; i++) { rd.p[i].team = i / 3; rd.p[i].kills = rd.p[i].deaths = rd.p[i].score = 0; rd.p[i].skill = 0; rd.p[i].obj = 0; }
	  rd.gp[0] = rd.gp[1] = rd.gp[2] = 0; rd.xp[0] = rd.xp[1] = rd.xp[2] = 0;
	  { MenuConfig cc = cfg; cc.ach = 0xFFu; cc.c_kills = 61; menu_career(&cc, &rd); }
	  menu_results_open(&rd); menu_results_update(0, 2.0f);
	  { int lo[5], lp[5]; const char *lw[5]; float lc[5]; int n = menu_results_lineup(lo, lw, lp, lc); static const float tp[5][2] = { { 480, 120 }, { 296, 133 }, { 664, 133 }, { 149, 143 }, { 811, 143 } };
	    for (int k = 0; k < n; k++) { mrs_tag[k][0] = tp[k][0]; mrs_tag[k][1] = tp[k][1]; mrs_tagp[k] = lp[k]; } mrs_ntag = n; }
	  shot("res_d_lineup"); menu_results_draw_bg(); menu_results_draw(); done();
	  menu_results_update(MB_OK, 0.1f); menu_results_update(0, 1.0f); shot("res_d_topop"); menu_results_draw_bg(); menu_results_draw(); done();
	  menu_results_update(MB_OK, 0.1f); menu_results_update(0, 0.3f); shot("res_d_results0"); menu_results_draw_bg(); menu_results_draw(); done();
	  menu_results_update(0, 2.0f); shot("res_d_results"); menu_results_draw_bg(); menu_results_draw(); done();
	  menu_results_update(MB_R, 0.1f); menu_results_update(0, 1.0f); shot("res_d_progress"); menu_results_draw_bg(); menu_results_draw(); done();
	  menu_results_update(MB_R, 0.1f); menu_results_update(0, 1.0f); shot("res_d_stats"); menu_results_draw_bg(); menu_results_draw(); done(); }
	{ // b63 (device b62-2, the tutorial's end: a 0 - 0 draw with one player on GHOST A and nobody on GHOST B): the team column
	  MenuResults rt = rr; rt.result = 2; rt.score[0] = rt.score[1] = 0; rt.nplayers = 1; rt.play_time = 600; rt.mission_n = 0;
	  rt.p[0].team = 0; rt.p[0].kills = rt.p[0].deaths = rt.p[0].score = 0; rt.p[0].skill = 2; rt.p[0].obj = 0; rt.p[0].me = 1;
	  rt.gp[0] = rt.gp[1] = rt.gp[2] = 0; rt.xp[0] = rt.xp[1] = rt.xp[2] = 0; menu_awards(&rt);
	  menu_results_open(&rt); MRS2.phase = MRS_TABS; MRS2.tab = 2; menu_results_update(0, 1.0f); shot("res_t_stats"); menu_results_draw_bg(); menu_results_draw(); done(); }
	}
