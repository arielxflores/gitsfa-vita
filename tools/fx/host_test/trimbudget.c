// b81 host check of the fill budget with trimming (fx.h fx_trim_budget): the budget thins big puffs when a pass would shade
// more than FX_FILL_BUDGET screens. Counting the trimmed polygons' area instead of the quads' lets more puffs through for
// the same fragments. Three pictures per step: the reference (no budget at all, all puffs), b80 (quads, quad-area budget),
// b81 (trimmed, polygon-area budget); mean difference from the reference (lower = closer to the full effect) and fragments.
//   build: gcc -std=gnu11 -O2 -w -I. -I../../../engine/src trimbudget.c -o trimbudget -lm
#include <stdio.h>
static float fx_budget_test = 3.0f;
#define FX_FILL_BUDGET fx_budget_test
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static float REF[SW_H][SW_W][3];
static int find_eff(const char *nm) { for (int i = 0; i < fx_neff; i++) if (!strcmp(fx_eff[i].name, nm)) return i; return -1; }
static double mdiff(void) { double s = 0; for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) s += fabs(REF[y][x][k] - sw_fb[y][x][k]); return s / (SW_W * SW_H * 3) * 255; }
int main(int argc, char **argv) {
	const char *path = argc > 1 ? argv[1] : "/root/gits/build/data/fx_sk.gfx";
	mkbg(); sprite_init(mk); if (fx_init(path) < 0) return 1;
	struct { const char *eff; float dist, z; int n; } S[] = { { "EXPLOSION", 150, 60, 1 }, { "EXPLOSION", 250, 60, 1 }, { "DX_BURST", 80, 165, 1 }, { "HIT_BLOOD", 70, 150, 4 }, { "SK_EMP_EXP", 200, 100, 1 }, { "SK_MS_EXP", 180, 80, 1 } };
	const float ts[6] = { 0.05f, 0.12f, 0.25f, 0.4f, 0.7f, 1.1f };
	double sd80 = 0, sd81 = 0; long f80 = 0, f81 = 0, fref = 0; int c80 = 0, c81 = 0;
	for (unsigned s = 0; s < sizeof(S) / sizeof(S[0]); s++) {
		int id = find_eff(S[s].eff); if (id < 0) continue;
		fx_clear(); fx_seed = 4242u + s; setcam(0, 0, 160, 0, 0, 70); float t = 0;
		for (int k = 0; k < S[s].n; k++) { float p[3] = { S[s].dist, 12.0f * k - 18, S[s].z + 6.0f * k }, n[3] = { -1, 0, 0.1f }, d[3] = { 1, 0, 0 }; fx_norm(n); fx_spawn(id, p, n, d); }
		for (int k = 0; k < 6; k++) { run_to(&t, ts[k]);
			fx_budget_test = 1e9f; fx_trim_on = 0; fx_trim_budget = 0; scene(VP, 2000); sw_pixels = 0; fx_draw(VP, cam, R, U); long fr0 = sw_pixels; memcpy(REF, sw_fb, sizeof(REF));
			fx_budget_test = 3.0f; int cut0 = fx_stat_cut; scene(VP, 2000); sw_pixels = 0; fx_draw(VP, cam, R, U); long fr80 = sw_pixels; double d80 = mdiff(); int cu80 = fx_stat_cut - cut0;
			fx_trim_on = 1; fx_trim_budget = 1; cut0 = fx_stat_cut; scene(VP, 2000); sw_pixels = 0; fx_draw(VP, cam, R, U); long fr81 = sw_pixels; double d81 = mdiff(); int cu81 = fx_stat_cut - cut0;
			printf("%-11s %3.0f cm t %.2f: particles %2d | all puffs %7ld fragments | b80 %7ld, %2d cut, picture off by %5.2f | b81 %7ld, %2d cut, off by %5.2f\n", S[s].eff, S[s].dist, t, fx_np, fr0, fr80, cu80, d80, fr81, cu81, d81);
			sd80 += d80; sd81 += d81; f80 += fr80; f81 += fr81; fref += fr0; c80 += cu80; c81 += cu81;
		}
	}
	printf("TOTAL: all puffs %ld fragments | b80 %ld (%d cut, mean picture difference %.2f / 255) | b81 %ld (%d cut, %.2f / 255)\n", fref, f80, c80, sd80 / 36, f81, c81, sd81 / 36);
	return 0;
}
