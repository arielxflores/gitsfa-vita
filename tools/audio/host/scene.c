// off-device render of a TDM-like scene through audio.h's event runtime -> scene.wav (48 kHz stereo)
#include "fopen_shim.h"
#include <stdlib.h>
#include <math.h>
#include "audio.h"
static short *outbuf; static int outn;
static void grains(float secs) {
	int n = (int)(secs * 48000 / GRAIN + 0.5f);
	for (int i = 0; i < n; i++) {
		snd_update((float)GRAIN / 48000); for (int k = 0; k < 4; k++) audio_io_step();
		audio_mix_grain(); for (int k = 0; k < GRAIN * 2; k++) outbuf[outn * 2 + k] = limit(mix[k]); outn += GRAIN;
	}
}
#undef fopen
static void wav(const char *p) {
	FILE *f = fopen(p, "wb"); int bytes = outn * 4; int hdr[11] = { 0x46464952, 36 + bytes, 0x45564157, 0x20746d66, 16, 0x00020001, 48000, 48000 * 4, 0x00100004, 0x61746164, bytes };
	fwrite(hdr, 4, 11, f); fwrite(outbuf, 4, outn, f); fclose(f);
}
int main(int argc, char **argv) {
	srand(1);
	outbuf = malloc(48000 * 4 * 120);
	printf("load %d sounds, %d voice lines, %.1f MB\n", (ev_load("ux0:data/gitsfa/"), audio_load_all("ux0:data/gitsfa/")), nvline, sounds_bytes() / 1048576.f);
	for (int i = 0; i < NSTREAM; i++) { streams[i].fd = -1; streams[i].state = ST_IDLE; }
	float lis[3] = { 0, 0, 170 }; audio_listener(lis, 0);
	const char *mode = argc > 1 ? argv[1] : "levels";
	if (!strcmp(mode, "levels")) {   // each element alone, 1.5 s apart, for level checks against the FEV design
		// 0: C30 fire fp (outdoor)  1.5: C30 indoor  3: M4A1 fp + tail  4.5: run footsteps x4  6: fastrun x4  7.5: bot TP fire at 1000  9: at 4000
		// 10.5: impact stone at 300  12: radio gamestart  15: music tdm ready (alone, 6 s)
		snd_indoor_now = 0; ev_play("weapon/ar/seburo_c-30/fire_fp", NULL); grains(1.5f);
		snd_indoor_now = 1; ev_play("weapon/ar/seburo_c-30/fire_fp", NULL); grains(1.5f); snd_indoor_now = 0;
		ev_play("weapon/ar/m4a1/fire_fp", NULL); ev_play("weapon/ar/m4a1/fire_fp_sustain", NULL); grains(1.5f);
		for (int i = 0; i < 4; i++) { snd_foley(0, "foot_run", 8, NULL); grains(0.33f); } grains(0.18f);
		for (int i = 0; i < 4; i++) { snd_foley(0, "foot_fastrun", 8, NULL); grains(0.265f); } grains(0.44f);
		{ float p[3] = { 1000, 0, 170 }; snd_weapon("weapon/ar/m4a1", "fire_tp", p, 0); grains(1.5f); }
		{ float p[3] = { 0, 4000, 170 }; snd_weapon("weapon/ar/m4a1", "fire_tp", p, 0); grains(1.5f); }
		{ float p[3] = { 300, 0, 100 }; snd_impact("ar", 8, p); grains(1.5f); }
		radio_play("voice/donut/tdm/gamestart"); grains(3.0f);
		music_ev("bgm/in_game_tdm_01_ready", 0); grains(6.0f); music_stop(200); grains(0.5f);
		amb_map_start("downtown"); grains(6.0f);
	} else if (!strcmp(mode, "amb")) {   // every map: beds, then the listener next to each emitter
		for (unsigned m = 0; m < sizeof(AMB_MAP) / sizeof(AMB_MAP[0]); m++) {
			int f0 = stream_open_fail; float p[3] = { 0, 0, 170 }; audio_listener(p, 0);
			amb_map_start(AMB_MAP[m].map); grains(1.0f); snd_indoor_now = 1; grains(2.0f); snd_indoor_now = 0;
			int ne = 0;
			for (unsigned i = 0; i < sizeof(AMB_EMIT) / sizeof(AMB_EMIT[0]); i++) if (!strcmp(AMB_EMIT[i].map, AMB_MAP[m].map)) {
				float q[3] = { AMB_EMIT[i].x + 50, AMB_EMIT[i].y, AMB_EMIT[i].z }; audio_listener(q, 0); grains(0.6f); ne++; }
			if (!strcmp(AMB_MAP[m].map, "trainstation") || !strcmp(AMB_MAP[m].map, "urban")) grains(2.0f);
			int act = 0; for (int i = 0; i < NSTREAM; i++) act += streams[i].state != ST_IDLE;
			printf("%-14s beds %s / %s, %d emitters, open fails %d, streams active %d\n", AMB_MAP[m].map, AMB_MAP[m].out, AMB_MAP[m].in, ne, stream_open_fail - f0, act);
		}
	} else if (!strcmp(mode, "match")) {   // the main.c flow with the helpers: start, firefight, a kill, death, respawn, end
		snd_set_ingame(argc > 2 ? atoi(argv[2]) : 1);   // b59: the in-match mix (argv[2] 0: the old one)
		amb_map_start("downtown"); music_ev("bgm/in_game_tdm_01_ready", 300); radio_play("voice/donut/tdm/gamestart");
		ev_play("ui/game_start_tdm_text", NULL); ev_play_later("ui/game_start_tdm_start", NULL, 3.9f); grains(5.0f);
		for (int i = 0; i < 12; i++) { snd_aim = i > 6; snd_wfire("scar"); if (i % 2 == 0) snd_wshell("scar", 8, 0.4f); grains(0.08f); }
		{ float p[3] = { 900, 300, 120 }; snd_whit("scar", 9, p); ev_play("impact/killshot_fp", NULL); ev_play("impact/ui_killshot", NULL); snd_killvoice("motoko", "doublekill"); }
		grains(1.0f); snd_wreload("scar", 1.93f); grains(2.5f);
		for (int i = 0; i < 8; i++) { snd_foley(0, "foot_fastrun", 8, NULL); grains(0.256f); }
		{ float g[3] = { 600, -200, 50 }; snd_gren_bounce(8, 1, g); grains(0.4f); snd_gren_bounce(8, 0.4f, g); grains(1.2f); snd_gren_exp(g); grains(2.0f); }
		ev_play("impact/bullet_impact_fp", NULL); snd_voice("motoko", "damage_1st", NULL); grains(0.6f);
		ev_play("ui/dead", NULL); snd_voice("motoko", "death_1st", NULL); music_ev("bgm/dead", 100); grains(4.0f);
		ev_play("ui/respawn", NULL); music_stop(800); grains(2.0f);
		ev_play("ui/crisis", NULL); grains(1.0f); music_ev("bgm/tdm_end_count", 600); grains(3.0f); radio_play("voice/donut/tdm/20sec"); grains(4.0f);
		ev_play("ui/in_game_end", NULL); music_ev("bgm/win", 200); radio_play_at("voice/donut/tdm/gamewin", 2.1f); grains(6.0f);
		amb_map_start(""); music_ev("bgm/result", 500); ev_play("ui/result_victory_defeat", NULL); grains(4.0f);
		printf("dropped %d limiter %d\n", audio_dropped, audio_limited);
	} else if (!strcmp(mode, "menu")) {   // b59: the lobby music with the menu sounds over it, 0.75 s apart (mix check): 4 s music alone, then the sounds
		// b59: the menu mix as the game plays it (menu_music / ui_sfx); argv[2] "0": the sounds alone (no music)
		if (!(argc > 2 && !strcmp(argv[2], "0"))) menu_music("bgm/lobby", 0); grains(4.0f);
		static const char *U[] = { "ui/mouse_over_basic", "ui/click_basic", "ui/tab_on", "ui/window_open_basic", "ui/window_close_basic", "ui/click_game_start", "ui/character_select", "ui/match_success", "ui/ready_count", "ui/ready_game_start_end", "ui/lobby_on", "ui/whoosh", "ui/character_select_mouse_over", "ui/weapon_change_on", "ui/logo_loading", "ui/room_pop_up_on" };
		for (int i = 0; i < 16; i++) { ui_sfx(U[i]); grains(1.0f); }
		printf("dropped %d limiter %d\n", audio_dropped, audio_limited);
	} else if (!strcmp(mode, "one")) {   // b59: one event alone (argv[2], volume argv[3], seconds argv[4]) -> one.wav
		if (argc > 6) {   // b107: in a map's room (argv[6] the map, argv[7] the indoor blend 0..1): the reverb alone, no ambience beds
			for (int i = 0; i < amb_nmap; i++) if (!strcmp(AMB_MAP[i].map, argv[6]) && AMB_REV) reverb_set(&REV_DEF[AMB_REV[i][0]], &REV_DEF[AMB_REV[i][1]]);
			rv_indoor = argc > 7 ? atof(argv[7]) : 0; }
		ev_play_ex(ev_find(argv[2]), NULL, argc > 5 ? atoi(argv[5]) : 0, 0, 0.f, argc > 3 ? atof(argv[3]) : 1.f); grains(argc > 4 ? atof(argv[4]) : 4.f);   // (b107: argv[5] 1 = the indoor variant)
		wav("one.wav"); printf("%d frames, limiter %d\n", outn, audio_limited); return 0;
	} else if (!strcmp(mode, "dump")) {   // b59: the design levels of the events whose name starts with argv[2] (kind, layer gain, sound)
		const char *pf = argc > 2 ? argv[2] : "ui/"; size_t n = strlen(pf);
		for (int e = 0; e < EV_COUNT; e++) { const EvDef *E = &EV_DEF[e]; if (strncmp(E->name, pf, n)) continue;
			printf("%-44s vol %.3f fl %d\n", E->name, E->vol, E->flags);
			for (int li = 0; li < E->nl; li++) { const EvLayer *L = &EV_LAYER[E->l0 + li]; float sec = -1, pk = -1; if ((L->kind & 15) == 0) { const Sound *S = find_sound(L->snd); if (S) { sec = (float)S->frames / S->rate; } } else if ((L->kind & 15) == 1) { const VEntry *V = find_vline(L->snd); if (V) sec = vline_secs(V); }
				printf("    k%-2d g %.3f %.3f  %-40s %.2f s\n", L->kind, L->g[0], L->g[2], L->snd, sec); (void)pk; } }
		return 0;
	} else if (!strcmp(mode, "burst")) {   // a 30-round C30 burst with tails, footsteps and 4 bots firing: voice pressure
		amb_map_start("downtown"); music_ev("bgm/in_game_tdm_01_ready", 0);
		for (int i = 0; i < 30; i++) {
			ev_play("weapon/ar/seburo_c-30/fire_fp", NULL);
			if (i % 2 == 0) { float s[3] = { 40, 30, 0 }; snd_shell("ar", 8, s, 0.4f); }
			if (i % 3 == 0) snd_foley(0, "foot_run", 8, NULL);
			for (int b = 0; b < 4; b++) if ((i + b) % 2 == 0) { float p[3] = { 800.f + b * 900, (b - 2) * 700.f, 170 }; snd_weapon("weapon/ar/m4a1", "fire_tp", p, 0); }
			grains(0.08f);
		}
		grains(3.0f);
		printf("max active %d, dropped %d, limiter %d\n", audio_maxvoices, audio_dropped, audio_limited);
	}
	wav("scene.wav");
	printf("%d frames, stream underruns %d, open fails %d, dropped %d\n", outn, stream_underruns, stream_open_fail, audio_dropped);
	return 0;
}
