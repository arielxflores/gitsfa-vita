#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
int main(int argc, char **argv) {
	mkbg(); sprite_init(mk); if (fx_init("/root/fx/out/fx.gfx") < 0) return 1;
	int id = atoi(argv[1]); float dist = atof(argv[2]); char nm[64];
	const float caps[] = { 0.03f, 0.1f, 0.25f, 0.5f };
	setcam(0, 0, 150, 0, 0, 70); fx_draw(VP, cam, R, U);
	float p[3] = { dist, 0, 150 }, n[3] = { -1, 0, 0 }, d[3] = { 1, 0, 0 };
	fx_spawn(id, p, n, d); float t = 0;
	for (int i = 0; i < 4; i++) { run_to(&t, caps[i]); scene(VP, dist + 400); fx_draw(VP, cam, R, U); snprintf(nm, 64, "one_%d", i); save(nm); }
	return 0;
}
