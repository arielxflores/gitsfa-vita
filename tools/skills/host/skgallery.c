// Host-side check of the skill effects in fx_sk.gfx with the engine's own fx.h (software GL from /root/fx/host).
// build: gcc -std=gnu11 -O2 -w -I/root/fx/host -I/root/gits/engine/src skgallery.c -o skgallery -lm ; ./skgallery file.gfx
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
static int fx_find(const char *n) { for (int i = 0; i < fx_neff; i++) if (!strncmp(fx_eff[i].name, n, 16)) return i; return -1; }
int main(int argc, char **argv) {
	mkbg(); sprite_init(mk); int r = fx_init(argc > 1 ? argv[1] : "/root/gits/build/data/fx_sk.gfx"); if (r < 0) { printf("fx_init %d\n", r); return 1; }
	static const char *names[] = { "SK_CAMO_ON", "SK_CAMO_HIT", "SK_AL_FIRE", "SK_MS_TRAIL", "SK_MS_EXP", "SK_BUILD", "SK_UNBUILD", "SK_DESTROY", "SK_TU_LOCK",
		"SK_META_ON", "SK_META_TICK", "SK_RUSH", "SK_RUSH_HIT", "SK_SH_HIT", "SK_SH_BREAK", "SK_WALL_PASS", "SK_DL_HIT", "SK_DIVE_ON", "SK_DIVE_HIT", "SK_EMP_EXP",
		"SK_EMP_SHOCK", "SK_EMP_CHG", "SK_RE_PING", "SK_HS_FLASH" };
	const float caps[] = { 0.05f, 0.15f, 0.3f, 0.6f, 1.0f }; char nm[64];
	for (unsigned k = 0; k < sizeof(names) / sizeof(*names); k++) {
		int id = fx_find(names[k]); if (id < 0) { printf("missing %s\n", names[k]); continue; }
		fx_clear(); setcam(0, 0, 150, 0, -0.08f, 70); fx_draw(VP, cam, R, U);
		float p[3] = { 320, 0, 0 }, n[3] = { 0, 0, 1 }, d[3] = { 1, 0, 0 };
		int moving = !strcmp(names[k], "SK_MS_TRAIL") || !strcmp(names[k], "SK_RUSH");
		if (!strcmp(names[k], "SK_AL_FIRE") || !strcmp(names[k], "SK_TU_LOCK") || !strcmp(names[k], "SK_DL_HIT") || !strcmp(names[k], "SK_SH_HIT")) { p[2] = 140; n[0] = -1; n[2] = 0; d[0] = 0; d[1] = 1; d[2] = 0; if (!strcmp(names[k], "SK_AL_FIRE")) { n[0] = 0; n[1] = 1; } }
		if (!strcmp(names[k], "SK_MS_EXP") || !strcmp(names[k], "SK_EMP_EXP")) p[0] = 700;
		fx_spawn(id, p, n, d); float t = 0;
		for (int i = 0; i < 5; i++) {
			while (t < caps[i] - 1e-4f) { fx_update(1 / 60.f); t += 1 / 60.f;
				if (moving) { static int c = 0; if (++c % 2 == 0) { float q[3] = { 320, -200 + t * 500, 140 }, dd[3] = { 0, 1, 0 }, nn[3] = { 0, -1, 0 }; if (!strcmp(names[k], "SK_RUSH")) { q[2] = 0; nn[1] = 0; nn[2] = 1; } fx_spawn(id, q, nn, dd); } }
				if (!strcmp(names[k], "SK_DL_HIT") || !strcmp(names[k], "SK_EMP_SHOCK") || !strcmp(names[k], "SK_EMP_CHG") || !strcmp(names[k], "SK_META_TICK") || !strcmp(names[k], "SK_CAMO_HIT")) { static int c2 = 0; if (++c2 % 6 == 0) fx_spawn(id, p, n, d); } }
			scene(VP, 1400); fx_draw(VP, cam, R, U); 
			char pth[256]; snprintf(pth, 256, "/root/gits/tools/skills/host/out/s%02u_%d.ppm", k, i);
			FILE *f = fopen(pth, "wb"); fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
			for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int c = 0; c < 3; c++) { float v = sw_fb[y][x][c]; fputc((int)((v > 1 ? 1 : v < 0 ? 0 : v) * 255 + 0.5f), f); }
			fclose(f);
		}
		printf("%s particles %d\n", names[k], fx_np);
	}
	return 0;
}
