#include <stdio.h>
#include <time.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
int main(void) {
	mkbg(); sprite_init(mk); int r = fx_init("/root/fx/out/fx.gfx"); printf("fx_init %d (emitters %d cells %d)\n", r, fx_nem, fx_ncell); if (r < 0) return 1;
	printf("sizeof FxEmit %zu FxP %zu FxDecal %zu\n", sizeof(FxEmit), sizeof(FxP), sizeof(FxDecal));
	const float caps[] = { 0.02f, 0.06f, 0.12f, 0.25f, 0.5f, 0.9f, 4.0f }; char nm[64];
	// A: shots into a wall, one per material 0..8, then a character hit (9) and a cyborg hit in the air
	setcam(0, 0, 160, 0, -0.03f, 70); fx_draw(VP, cam, R, U);
	for (int m = 0; m < 9; m++) { float p[3] = { 350, -300 + 75 * m, 150 }, n[3] = { -1, 0, 0 }, d[3] = { p[0] - cam[0], p[1] - cam[1], p[2] - cam[2] }; fx_norm(d); fx_impact(m, p, n, d); }
	{ float p[3] = { 300, -150, 60 }, d[3] = { 1, -0.4f, -0.3f }; fx_norm(d); float n[3] = { -d[0], -d[1], -d[2] }; fx_impact(9, p, n, d); }
	{ float p[3] = { 300, 150, 60 }, d[3] = { 1, 0.4f, -0.3f }; fx_norm(d); float n[3] = { -d[0], -d[1], -d[2] }; fx_spawn(FX_HIT_CYBORG, p, n, d); }
	float t = 0; for (int i = 0; i < 7; i++) { run_to(&t, caps[i]); scene(VP, 350); fx_draw(VP, cam, R, U); snprintf(nm, 64, "wall_%d", i); save(nm); printf("wall t=%.2f particles %d quads %d flushes %d px %ld\n", t, fx_np, fx_stat_quads, fx_stat_flush, sw_pixels); sw_pixels = 0; }
	// B: shots into the floor, seen from above-ish
	fx_clear(); setcam(0, 0, 170, 0, -0.55f, 70); fx_draw(VP, cam, R, U);
	for (int m = 0; m < 9; m++) { float p[3] = { 260 + (m % 3) * 0, -240 + 60 * m, 0 }, n[3] = { 0, 0, 1 }, d[3] = { p[0] - cam[0], p[1] - cam[1], p[2] - cam[2] }; fx_norm(d); fx_impact(m, p, n, d); }
	t = 0; for (int i = 0; i < 7; i++) { run_to(&t, caps[i]); scene(VP, 2000); fx_draw(VP, cam, R, U); snprintf(nm, 64, "floor_%d", i); save(nm); }
	// C: bot muzzle flashes (side-on and facing), shells, tracers
	fx_clear(); setcam(0, 0, 160, 0, 0, 70); fx_draw(VP, cam, R, U);
	{ float p[3] = { 300, -90, 140 }, d[3] = { 0, 1, 0 }; fx_muzzle_3p(p, d); float e[3] = { 0.3f, 0.2f, 0.9f }; fx_spawn(FX_SHELL_3P, p, e, NULL); }
	{ float p[3] = { 450, 120, 140 }, d[3] = { -1, -0.3f, 0 }; fx_norm(d); fx_muzzle_3p(p, d); }
	{ float a[3] = { 300, -90, 140 }, b[3] = { 300, 900, 140 }; fx_tracer(a, b); }
	{ float a[3] = { 1500, 300, 150 }, b[3] = { 50, -40, 150 }; fx_tracer(a, b); }
	{ float a[3] = { 900, -600, 120 }, b[3] = { 900, 600, 200 }; fx_tracer(a, b); }
	const float caps3[] = { 0.0f, 0.033f, 0.066f, 0.09f, 0.3f };
	t = 0; for (int i = 0; i < 5; i++) { run_to(&t, caps3[i] + 1 / 60.f); scene(VP, 3000); fx_draw(VP, cam, R, U); snprintf(nm, 64, "muzzle3p_%d", i); save(nm); }
	// D: first-person flash at ADS (view space, fov 48) and hip (fov 62), shells
	for (int ads = 0; ads < 2; ads++) {
		fx_clear(); float PV[16]; persp(PV, (62 - 14 * ads) * 3.14159f / 180, 960.0f / 544, 2, 400);
		float mz[3] = { ads ? 0.f : 9.f, ads ? -7.f : -14.f, ads ? -62.f : -58.f }, d[3] = { 0, 0, -1 }; fx_muzzle_fp(mz, d);
		float port[3] = { ads ? 2.f : 10.f, ads ? -6.f : -12.f, -22 }, ej[3] = { 0.8f, 0.45f, 0.35f }; fx_shell_fp(port, ej, 0);
		t = 0; for (int i = 0; i < 4; i++) { run_to(&t, caps3[i] + 1 / 60.f); clear(0.45f, 0.47f, 0.5f); fx_draw_fp(PV); snprintf(nm, 64, "fp%d_%d", ads, i); save(nm); }
	}
	// E: cost with a full pool (sustained fire into concrete + near misses + blood): CPU only (no raster)
	fx_clear(); setcam(0, 0, 160, 0, 0, 70); fx_draw(VP, cam, R, U); sp_ok = 0;
	clock_t c0 = clock(); int frames = 2000; long q = 0, np = 0;
	for (int i = 0; i < frames; i++) {
		for (int k = 0; k < 3; k++) { float p[3] = { 350, -300 + (rand() % 600), 60 + rand() % 200 }, n[3] = { -1, 0, 0 }, d[3] = { 1, 0, 0 }; fx_impact((i * 3 + k) % 10, p, n, d); }
		fx_update(1 / 30.f); fx_draw(VP, cam, R, U); q += fx_stat_quads; np += fx_np; }
	double ms = (double)(clock() - c0) * 1000 / CLOCKS_PER_SEC / frames;
	printf("stress: %.0f particles avg (cap %d), %.0f quads/frame, decals %d: update+build %.4f ms/frame on host\n", (double)np / frames, FX_MAXP, (double)q / frames, fx_nd, ms);
	return 0;
}
