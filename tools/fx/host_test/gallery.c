// one impact per row (materials 0..8 on a wall and on the floor, blood, cyborg), columns = time after the hit
#include <stdio.h>
#include "vitaGL.h"
#include "fx.h"
#include "harness.h"
int main(void) {
	mkbg(); sprite_init(mk); if (fx_init("/root/fx/out/fx.gfx") < 0) return 1;
	const float caps[] = { 0.03f, 0.1f, 0.25f, 0.5f, 0.85f, 3.0f }; char nm[64];
	for (int floor = 0; floor < 2; floor++) for (int m = 0; m < 11; m++) {
		if (floor && m >= 9) continue;
		fx_clear();
		if (!floor) setcam(0, 0, 150, 0, 0, 70); else setcam(0, 0, 170, 0, -0.6f, 70);
		fx_draw(VP, cam, R, U);
		float p[3] = { 260, 0, 150 }, n[3] = { -1, 0, 0 }; if (floor) { p[0] = 260; p[2] = 0; n[0] = 0; n[2] = 1; }
		float d[3] = { p[0] - cam[0], p[1] - cam[1], p[2] - cam[2] }; fx_norm(d);
		if (m == 9) { p[0] = 220; float nn[3] = { -d[0], -d[1], -d[2] }; fx_impact(9, p, nn, d); }
		else if (m == 10) { p[0] = 220; float nn[3] = { -d[0], -d[1], -d[2] }; fx_spawn(FX_HIT_CYBORG, p, nn, d); }
		else fx_impact(m, p, n, d);
		float t = 0;
		for (int i = 0; i < 6; i++) { run_to(&t, caps[i]); scene(VP, floor ? 2000 : 260); fx_draw(VP, cam, R, U); snprintf(nm, 64, "g%d_%d_%d", floor, m, i); save(nm); }
	}
	return 0;
}
