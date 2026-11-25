#include "fopen_shim.h"
#include <stdlib.h>
#include <math.h>
#include AUDIO_H
int main(void) {
	audio_load_all("ux0:data/gitsfa/");
	for (int i = 0; i < NSTREAM; i++) { streams[i].fd = -1; streams[i].state = ST_IDLE; }
	srand(3); long long tot = 0; int ng = 0;
	const char *pre[] = { "wp_m4a1_fire_fp_outdoor_", "foot_co_run_stone_", "hit_ar_stone_", "wp_m4a1_fire_tp_distance_", "ui_hit_common_", "shelldrop_ar_sr_stone_" };
	for (int rep = 0; rep < 40; rep++) {
		for (int i = 0; i < NVOICE; i++) play(pre[i % 6], 100);
		for (int g = 0; g < 30; g++) {
			for (int i = 0; i < 4; i++) play(pre[(g + i) % 6], 100);    // keep every voice busy
			SceUInt64 t = sceKernelGetProcessTimeWide(); audio_mix_grain(); tot += sceKernelGetProcessTimeWide() - t; ng++;
		}
	}
	int act = 0; for (int i = 0; i < NVOICE; i++) act += voices[i].active;
	printf("%s: %.1f us per grain (%d voices active at the end)\n", AUDIO_H, (double)tot / ng, act);
}
