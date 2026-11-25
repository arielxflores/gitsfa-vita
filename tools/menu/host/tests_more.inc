	// in-match overlays over a game screenshot (the rasteriser composites over bg.jpg when present)
	shot("splash_tdm"); menu_splash_draw(MODE_TDM, 0, MENU_MODE_GOAL[MODE_TDM], 1.0f); done();
	shot("splash_tdm2"); menu_splash_draw(MODE_TDM, 0, MENU_MODE_GOAL[MODE_TDM], 3.0f); menu_commander_draw(FACE_ARAMAKI, "Chief Aramaki", "Eliminate all enemy agents!", 1.0f); done();
	shot("splash_dem_round"); menu_splash_draw(MODE_DEM, 1, "Destroy all hostiles", 3.0f); done();
	shot("splash_ttm"); menu_splash_draw(MODE_TTM, 0, MENU_MODE_GOAL[MODE_TTM], 1.0f); menu_notice_draw("START", 1.0f); done();
	shot("splash_em"); menu_splash_draw(MODE_EM, 1, MENU_MODE_GOAL[MODE_EM], 3.0f); done();
	shot("commander"); menu_commander_draw(FACE_ARAMAKI, "ARAMAKI", "All units: eliminate the enemy team. Good luck.", 1.0f); menu_notice_draw("START", 1.0f); done();
	for (int f = 10; f <= 15; f++) { char n_[24]; snprintf(n_, sizeof(n_), "cm_face%d", f); shot(n_); menu_commander_draw(f, "TERRORIST LEADER", "Find the Ghosts and eliminate them!", 1.0f); menu_notice_draw("Seeker Drones report enemy locations.", 1.0f); done(); }   // b63: Ghost Assault's cyborg commander (which portrait)
	{ MenuRespawn r = { "Yuzuru", "Waifu Delta", 3, 2, 2, 4.37f, "TIP : Aim for the head: headshots deal double damage." }; shot("respawn"); menu_respawn_draw(&r, 1.0f); done(); }
	shot("endsplash_win"); menu_endsplash_draw(0, 8, 1, 1.0f); done();
	shot("endsplash_lose"); menu_endsplash_draw(1, 3900, 4300, 1.0f); done();
	shot("endsplash_draw"); menu_endsplash_draw(2, 0, 0, 1.0f); done();   // b61 (user: 0s over text at the end of a draw)
	shot("endsplash_draw2"); menu_endsplash_draw(2, 3000, 3000, 1.0f); done();
	shot("timewarn"); menu_timewarn_draw(1.0f); done();
	menu_pause_open(); menu_pause_update(0, 0.5f); shot("pause"); menu_pause_draw(); done();
	menu_pause_update(MB_DOWN, 0.1f); menu_pause_update(MB_DOWN, 0.1f); menu_pause_update(MB_OK, 0.1f); menu_pause_update(0, 0.3f); shot("pause_quit"); menu_pause_draw(); done();
	// touch on the pause menu: tap OPTIONS (one tap acts)
	{ menu_pause_open(); menu_pause_update(0, 0.5f); menu_pause_draw(); menu_touch_inject(1, 1, 780, 254); menu_touch_poll(0); menu_pause_update(0, 0.016f); menu_pause_draw();
	  menu_touch_inject(1, 0, 780, 254); menu_touch_poll(0); int a = menu_pause_update(0, 0.016f); menu_touch_inject(0, 0, 0, 0); menu_touch_poll(0);
	  printf("pause tap OPTIONS -> %d (want %d)\n", a, PAUSE_OPTIONS); }
	// the autoplay tour, frame by frame as main.c runs it (60 fps), with a capture every 4 s
	{ menu_config_default(&cfg); menu_fe_open(FE_TITLE); MFE.t = MFE.st = 0; float t = 0; int dn = 0, k = 0, last = -1;
	  while (!dn && t < 120) { unsigned b = menu_fe_autoplay(t, &dn); int sc = MFE.screen;
		if ((int)(t / 4) != last) { last = (int)(t / 4); char nm2[32]; snprintf(nm2, sizeof(nm2), "tour_%02d", k++); shot(nm2); fe_frame(b, 1 / 60.f); done(); }
		else fe_frame(b, 1 / 60.f);
		if (MFE.screen != sc) printf("tour %5.1f s: screen %d -> %d\n", t, sc, MFE.screen);
		t += 1 / 60.f; }
	  printf("tour done at %.1f s, screen %d, chr %d primary %d secondary %d mode %d map %d\n", t, MFE.screen, cfg.chr, cfg.primary, cfg.secondary, cfg.mode, cfg.map); }
