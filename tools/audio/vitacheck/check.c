// compile check of audio.h as main.c includes it (after player.h)
#include <vitasdk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../../engine/src/player.h"
#include "../../../engine/src/audio.h"
int main(void) {
	audio_load_all("ux0:data/gitsfa/"); audio_init();
	float p[3] = { 0, 0, 0 }; audio_listener(p, 0);
	ev_play("weapon/ar/m4a1/fire_fp", NULL); snd_foley(0, "foot_run", 8, NULL); snd_impact("ar", 2, p); snd_shell("ar", 1, p, 0.4f);
	snd_weapon("weapon/ar/m4a1", "reload_01", NULL, -1); music_ev("bgm/bgm_dead", 100); radio_play("voice/donut/tdm/gamestart");
	amb_map_start("downtown"); snd_update(0.016f); amb_for_map("ux0:data/gitsfa/", "port", 170);
	play("x", 1); play_later("x", 1, 1); fade_others("x", 0, 1); voice_play("x", 1, 0); voice_stop_all(1); voice_busy(); music_stop(1); music_playing(); amb_stop(0, 1);
	add_tone(); add_melody(); count_sounds("x"); sounds_bytes(); snd_indoor_at(p); stream_stop(1, 1);
	return 0;
}
void more(void) { float p[3] = { 0 }; snd_wfire("m4a1"); snd_wfire_tp("scar", p); snd_wev("l118", "shot_reload", 0); snd_wreload("m4a1", 2.0f); snd_wshell("m4a1", 1, 0.4f); snd_whit("870", 2, p); snd_gren_bounce(1, 0.9f, p); snd_gren_exp(p); snd_voice("motoko", "damage_1st", NULL); snd_killvoice("bato", "doublekill"); snd_chr_female("x"); }
