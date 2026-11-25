#include "fopen_shim.h"
#include <stdlib.h>
#include <math.h>
#include AUDIO_H
int main(void) {
	audio_load_all("ux0:data/gitsfa/");
	for (int i = 0; i < NSTREAM; i++) { streams[i].fd = -1; streams[i].state = ST_IDLE; }
	music_play("ux0:data/gitsfa/bgm_lobby_sj_01.gstr", 1, 100, 0);
	amb_play(0, "ux0:data/gitsfa/ambi_hongkong_outside_01.gstr", 100); amb_play(1, "ux0:data/gitsfa/ambi_port_hall_01.gstr", 100);
	for (int i = 0; i < 3; i++) voice_play("act_motoko_", 100, 0);
#ifdef EMIT
	const char *em[4] = { "ambi_airplane", "ambi_dog_01", "ambi_construction1_02", "ambi_ambulance_01" };
	for (int i = 0; i < 4; i++) stream_file(4, em[i], 1, 30000, 0, 0);
#endif
	for (int k = 0; k < 8; k++) audio_io_step();
	srand(3); long long tot = 0; int ng = 0, worst = 0;
	const char *pre[] = { "wp_m4a1_fire_fp_outdoor_", "foot_co_run_stone_", "hit_ar_stone_", "wp_m4a1_fire_tp_distance_", "ui_hit_common_", "shelldrop_ar_sr_stone_" };
	for (int i = 0; i < NVOICE; i++) play(pre[i % 6], 100);
	for (int g = 0; g < 600; g++) {
		for (int i = 0; i < 4; i++) play(pre[(g + i) % 6], 100);
		audio_io_step();
		SceUInt64 t = sceKernelGetProcessTimeWide(); int n = audio_mix_grain(); int d = (int)(sceKernelGetProcessTimeWide() - t); tot += d; ng++; if (d > worst) worst = d;
		if (g == 300) printf("  active %d\n", n);
	}
	printf("%s%s: %.1f us per grain\n", AUDIO_H,
#ifdef EMIT
	" +4 emitters"
#else
	""
#endif
	, (double)tot / ng);
}
