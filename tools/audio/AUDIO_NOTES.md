# Audio for the Vita build

Pipeline: `scan.py` (decrypts the FMOD banks into /root/fsbdec) -> `decode_banks.py` (IMA banks decoded in Python, MPEG banks
via ffmpeg -> int16 .npy) -> `fev.py` (sound definitions from sac.fev, for the manifest) -> `build_audio.py OUTDIR`.
Codec: block IMA ADPCM, 1024 frames/block, `adpcm.c` (encoder with 2-sample lookahead), decoded in the mixer (audio.h).

Output (build/data):
- sfx_0.gsnd, sfx_1.gsnd   RAM pack, 1824 sounds, 31.5 MB (all weapons, impacts, flybys, shells, foley/footsteps, in-game UI,
  Tachikoma, skills). Mono except first-person stereo UI stingers; 32 kHz weapons/UI stingers, 24 kHz foley/footsteps/menu UI.
- voice_0..2.gvp           streamed, 2182 lines (all voice incl. barks, radio, announcer/operator, menu UI jingles), 58 MB
- audio/*.gstr             streamed: 31 music tracks (44.1 kHz stereo, 49.8 MB) + 88 ambience beds (native rate <= 32 kHz, 24.7 MB)
- audio_manifest.txt       name, kind (ram/voice/music/amb), rate, channels, seconds, bank, FMOD sound definition

Engine API (audio.h): `audio_load_all(VDIR)` replaces `load_sounds(VDIR "sounds.gsnd")`; `play/play_pan/fade_others/play_later`
unchanged; `voice_play(prefix, vol, pan)`, `music_play(path, loop, vol, fade_ms)`, `music_stop(ms)`,
`amb_for_map(VDIR, stem, vol)`, `amb_play(slot, path, vol)`, `amb_stop(slot, ms)`, `voice_stop_all(ms)`, `voice_busy()`.
Gains: master_gain, music_gain, amb_gain, voice_gain. Diagnostics: stream_underruns, stream_open_fail.

Event prefixes (play = RAM, voice = voice_play):
- player fire: play("wp_<gun>_fire_fp_outdoor_") e.g. wp_m4a1_, wp_ar_seburoc30_, wp_c30_, wp_ak47_, wp_scar_, wp_mp7_, wp_ump45_,
  wp_p226_, wp_deserteagle_, wp_870mcs_, wp_sv98_, wp_m200_, wp_m249_ ... (list: grep fire_fp_outdoor audio_manifest.txt)
- bot fire: near = same fp sound panned; far = play_pan("wp_<gun>_fire_tp_distance_") (m4a1, c30, ak47, k2, scar, famas, hk417, ...)
- reload: wp_ar_m4a1_reload_01/02/03 (mag out / in / bolt), wp_ar_reload_magazine_drop, dry: wp_ar_dryfire_, equip: wp_ar_equip_on_,
  aim: wp_ar_aim_on / wp_ar_aim_off, sniper zoom: wp_sr_zoom_in_
- impacts: hit_ar_<stone|wood|metal|dirt|glass|water|metal_car|body>_, knife: hit_knife2_<mat>_, player hit: hit_bullet_fp_body_
- flyby: hit_bullet_fly_l_ (light), hit_bullet_fly_h_ (heavy), bullet_fly_
- shells: shelldrop_ar_sr_<dirt|stone|wood>_, shelldrop_common_<metal|water>_, shelldrop_pistol_smg_, shelldrop_shotgun_
- grenades: wp_grenade_pin_pull_throw_, wp_grenade_bounce_<a|b|c>_<mat>_, grenade_exp_, exp_distance_, flashbang_exp, smoke_exp
- hurt/death: voice("act_<char>_damage"), voice("act_<char>_death"), voice("act_<char>_death_large") (7 chars), cqc: cqc_split_<char>_death_f/_b;
  body foley: play("foley_co_damage_"), play("foley_co_death_<back|knee|spin|run|jump>_"). Kuro has no voice lines: use togusa's.
- barks: act_<char>_reload3, _enemydetect3, _detect_grenade3, _throw_grenade3, _teamdeath3, _enemydeath3, _jump, _land, _knife
- kills: play("ui_hit_kill_01") + play("hit_killshot_"); headshot play("ui_hit_heatshot2") + play("hit_headshot_"); hit play("ui_hit_common_");
  special/streak play("ui_kill_special"); killer line voice("k_<char>_killshot" | "_grenade" | "_revenge" | "_tachi")
- announcer (Aramaki): voice("d_aramaki_tdm_gamestart" | "_20sec" | "_win" | "_lose" | "_draw");
  Demolition: d_aramaki_dm_attack_start, _defense_start, _plantbomb_team, _plantbomb_enemy, _oneleft_self, _oneleft_enemy,
  _attack_todefense, _defense_toattack, _common_win/_lose/_draw
- operator: voice("d_op_ready_<skill>"), voice("d_op_enemy_<skill>"), skills: optical heatscan armlauncher sentrygun metaboost cyberwall
  plasmashield rush specialrecon dmddrone cyberdive emp; own skill line voice("d_<char>_use_<skill>")
- round: play("ui_ready_count_start"), ("ui_ready_count_end"), ("ui_game_start_start_"), ("ui_game_start_mode"), voice("ui_ingame_start"),
  timer: play("ui_crisis"), play("ui_ghost_time_warning"); bomb: voice("ui_plant_bomb"), voice("ui_deactivate_bomb"), play("ui_bomb_count_01"/"_02"), voice("ui_bomb_count_03")
  end: voice("ui_result_victory_defeat"), voice("ui_result")
- UI: ui_click1, ui_mouse_over_basic, ui_mouse_over_medium_01, ui_tab_on, ui_tab_off, ui_whoosh, ui_weapon_change_on/off,
  voice("ui_character_select_cancel"), voice("ui_cha_select")
- respawn play("ui_respawn"); death play("ui_dead") + music bgm_dead_0N
- Tachikoma: tachi_idle, tachi_foot_, tachi_drive_loop, tachi_jump, tachi_land, tachi_fire_ready/_loop/_end, tachi_hit_, tachi_exp,
  fp_tachi_* (cockpit), wp_tachi_* (its guns), hit_bullet_fly_l_tachi_
- skills: cloaking_start/_loop/_end (therm-optic camo), is_heat_scan_*, is_armlauncher_*, is_shield_*, is_turret_hacking_*,
  is_dmd_* (drone), is_emp*, is_metabolism_*, is_noise_scan_*, is_screen_*, is_mini_tachikoma_*, is_transfer_fp, is_internal_heal

Music (data/audio/*.gstr): bgm_logo, bgm_intro_sj, bgm_lobby_sj_01 (menu, loop), bgm_ready_sj_01 (character select, loop),
bgm_in_game_dm_round (round start), bgm_end_count (last 40 s), bgm_bomb_count (bomb planted), bgm_dead_01..03, bgm_win, bgm_lose,
bgm_even, bgm_result, bgm_tutorial, bgm_matrix.

## FMOD events (build 26)
- `fevx/fevlib.py` reads sac.fev (FMOD Designer 4.44, LGCY chunk): event groups, events (volume, pitch, 2D/3D, rolloff,
  min/max distance, max playbacks, priority), layers, sound instances (range on a parameter), envelopes (points in the
  EPRP chunk; flags 0x804 index 0 / 0xc / 0x24 = volume, 0x404 = 3D pan level, 0x2004 = reverb, envelope param = t[0]),
  parameters (+ sustain points), user properties, category; sound definitions. Dumps: `ref/fev_events.txt`.
- `build_extras.py` -> data/sfx_x.gsnd + voice_x.gvp (indoor fire variants of the 12 used guns, the real KRISS / MP5 /
  M249 mech + far / MG36 far / C-30 far sounds, grenade_exp_indoor, ui_result_test). Run after build_audio.py.
- `make_events.py gen` writes EV_DEF / EV_LAYER / EV_CURVE + per-map ambience (AMB_MAP beds, AMB_EMIT from the maps'
  SoundData.sdf in `ref/sdf/`, AMB_TIMED train / helicopter) into engine/src/audio.h between the GENERATED markers.
- Parameters the game sets: 'sustain' = indoor (outdoor 0.2 / indoor 0.75: picks *_fp_outdoor vs *_fp_indoor and the
  indoor tail level), 'aim' (ADS: fire x0.84), '(distance)' (custom rolloff curves), param00 (e.g. grenade bounce a/b/c).
- Findings from the footage (`ana/`): designed volumes = what the game plays (range video: sfx and ambience within 0.5 dB);
  win plays bgm_lose.wav and lose plays bgm_win.wav (FEV); ui_dmg is silent (vol 0); sprint steps every 0.256 s;
  time-up sting ui_result_test, the announcer comes 2.1 s later over the radio click (ui_doughnut_on/off).
- Off-device tests: `host/` (stub Vita headers; scene.c renders levels / match / burst / amb scenes, bench*.c time the mixer).
