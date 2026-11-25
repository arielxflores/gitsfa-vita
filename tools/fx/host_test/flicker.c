// b36 host check of fx.h's thinning over time: a grenade at 1.5 m drawn every 1/60 s for 1.3 s; per frame the fragments
// and the mean change from the previous frame (a thinning that flips particles in and out shows as spikes)
#include <stdio.h>
#include <string.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static float prev[SW_H][SW_W][3];
int main(int argc, char **argv) {
	const char *tag = argc > 1 ? argv[1] : "x";
	mkbg(); sprite_init(mk); if (fx_init("/root/fx/out/fx.gfx") < 0) return 1;
	fx_clear(); setcam(0, 0, 160, 0, 0, 70); fx_draw(VP, cam, R, U);
	float p[3] = { 150, 0, 60 }, n[3] = { 0, 0, 1 }; fx_spawn(FX_EXPLOSION, p, n, NULL);
	double worst = 0, sum = 0, frag = 0, peak = 0; int nf = 0;
	for (int f = 0; f < 78; f++) {
		fx_update(1 / 60.f); scene(VP, 2000); sw_pixels = 0; fx_draw(VP, cam, R, U);
		double d = 0; for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) d += fabsf(sw_fb[y][x][k] - prev[y][x][k]);
		d = d / (SW_W * SW_H * 3) * 255; memcpy(prev, sw_fb, sizeof(prev));
		if (f > 0) { sum += d; nf++; if (d > worst) worst = d; }
		frag += sw_pixels / (960.0 * 544); if (sw_pixels / (960.0 * 544) > peak) peak = sw_pixels / (960.0 * 544);
	}
	printf("%s: fragments %.1f screens over 1.3 s (peak %.2f a frame); frame-to-frame change mean %.2f / 255, worst %.2f\n", tag, frag, peak, sum / nf, worst);
	return 0;
}
