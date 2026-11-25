// perf b35: fx_alloc check: a random effect storm (the pool full most frames), the whole particle pool hashed every frame. Build it
// against two versions of fx.h (gcc -O2 -I<fx.h dir> -Itools/fx/host_test -Iengine/src fx_alloc_stress.c -lm) and compare the
// hashes: fx_alloc must pick the same particles. Args: frames, spawns per frame (mean / 2).
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static unsigned long long fnv(unsigned long long h, const void *p, size_t n) { const unsigned char *c = p; for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; } return h; }
static unsigned rs = 12345; static float rr(void) { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.f; }
int main(int argc, char **argv) {
	int frames = argc > 1 ? atoi(argv[1]) : 3000, rate = argc > 2 ? atoi(argv[2]) : 6;
	sprite_init(mk); if (fx_init("/root/work/modes/build/data/fx.gfx") < 0) return 1; setcam(0, 0, 160, 0, 0, 70);
	unsigned long long h = 1469598103934665603ull; long full = 0, allocs_full = 0;
	for (int f = 0; f < frames; f++) {
		int n = (int)(rr() * rate * 2);
		for (int k = 0; k < n; k++) {
			float p[3] = { 200 + rr() * 2000, (rr() - 0.5f) * 2000, rr() * 300 }, nn[3] = { rr() - 0.5f, rr() - 0.5f, 0.5f + rr() }, d[3] = { 1, rr() - 0.5f, 0 }, q[3] = { p[0] + 3000, p[1], p[2] };
			int np0 = fx_np; float w = rr(); if (np0 == FX_MAXP) allocs_full++;
			if (w < 0.05f) fx_explosion(p, nn); else if (w < 0.35f) fx_impact((int)(rr() * 11), p, nn, d); else if (w < 0.6f) fx_tracer(p, q);
			else if (w < 0.8f) fx_muzzle_3p(p, d); else if (w < 0.9f) fx_spawn((int)(rr() * FX_NUM), p, nn, d); else fx_muzzle_fp(p, d);
		}
		if (fx_np == FX_MAXP) full++;
		fx_update(rr() < 0.1f ? 0 : 1 / 60.f + rr() * 0.02f);
		if (f % 997 == 500) fx_clear();
		h = fnv(h, &fx_np, sizeof(fx_np)); h = fnv(h, fx_p, sizeof(FxP) * fx_np); h = fnv(h, fx_q, sizeof(FxQ) * fx_nq);
	}
	printf("frames %d, pool full after spawning in %ld, spawn calls into a full pool %ld, hash %016llx\n", frames, full, allocs_full, h);
	return 0;
}
