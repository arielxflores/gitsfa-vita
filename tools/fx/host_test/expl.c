// b35 host check of fx.h's fill budget: a grenade explosion at 1.5 m and 4 m in front of the camera, fragments shaded
// (software rasteriser count) and the picture, with the budget (default) and without (-DFX_FILL_BUDGET=1e9)
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
int main(int argc, char **argv) {
	const char *tag = argc > 1 ? argv[1] : "b";
	mkbg(); sprite_init(mk); if (fx_init("/root/fx/out/fx.gfx") < 0) return 1;
	const float dist[2] = { 150, 400 }, caps[5] = { 0.05f, 0.15f, 0.35f, 0.7f, 1.2f }; char nm[64]; long tot = 0;
	for (int k = 0; k < 2; k++) {
		fx_clear(); setcam(0, 0, 160, 0, 0, 70); fx_draw(VP, cam, R, U);
		float p[3] = { dist[k], 0, 60 }, n[3] = { 0, 0, 1 }; fx_spawn(FX_EXPLOSION, p, n, NULL);
		float t = 0;
		for (int i = 0; i < 5; i++) { run_to(&t, caps[i]); scene(VP, 2000); sw_pixels = 0; fx_draw(VP, cam, R, U);
			printf("%s dist %3.0f t %.2f: particles %3d, fill estimate %.1f screens, cut so far %d, fragments %.2f screens\n", tag, dist[k], t, fx_np, fx_stat_fill, fx_stat_cut, sw_pixels / (960.0 * 544));
			tot += sw_pixels; snprintf(nm, 64, "expl_%s_%d_%d", tag, k, i); save(nm); }
	}
	printf("%s total fragments %.1f screens\n", tag, tot / (960.0 * 544));
	return 0;
}
