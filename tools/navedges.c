// Offline: walk the player physics (engine/src/player.h) across every nav-mesh edge, both directions, and record
// which ones it can cross (with jumps, climbs and vaults): writes one byte per triangle, bit k = edge k blocked.
// Built and run by make_map.py (nav mesh section). usage: navedges MAP.gcol MAP.gnav OUT.bin   (PRINT=1 lists the blocked edges)
#include <stdio.h>
#include "player.h"
#include "nav.h"
static int try_cross(int a, int b, int k, float f) {
	NavTri *A = &navt[a]; float *p0 = A->v[k], *p1 = A->v[(k + 1) % 3];
	float mx = p0[0] + (p1[0] - p0[0]) * f, my = p0[1] + (p1[1] - p0[1]) * f;
	float ex = p1[0] - p0[0], ey = p1[1] - p0[1], el = sqrtf(ex * ex + ey * ey); float nx = ey / el, ny = -ex / el; // outward normal (CCW: interior on the left)
	float sx = mx - nx * 70, sy = my - ny * 70, gx = mx + nx * 70, gy = my + ny * 70;
	Player p; memset(&p, 0, sizeof p);
	int ta = nav_find(sx, sy, A->c[2]); if (ta < 0) return 0;
	p.pos = v3(sx, sy, nav_tri_z(&navt[ta], sx, sy) + 30);
	for (int i = 0; i < 20; i++) player_update(&p, 0, 0, 0, 0, 1 / 60.f);
	float gz = nav_tri_z(&navt[b], gx, gy); float best = 1e9, bt = 0; float jcd = 0;
	for (int f2 = 0; f2 < 60 * 4; f2++) {
		float dx = gx - p.pos.x, dy = gy - p.pos.y, d = sqrtf(dx * dx + dy * dy);
		if (d < 35 && fabsf(p.pos.z - gz) < 120) return 1;
		p.yaw = atan2f(dy, dx); int jump = 0; jcd -= 1 / 60.f;
		if (d < best - 10) { best = d; bt = 0; } else bt += 1 / 60.f;
		if (bt > 0.4f && jcd <= 0 && p.grounded) { jump = 1; jcd = 0.8f; }
		player_update(&p, 0, d > 60 ? 1 : 0.5f, 0, jump, 1 / 60.f);
		if (p.pos.z < -1500) return 0;
	}
	return 0;
}
int main(int argc, char **argv) {
	if (argc < 4) return 1; load_col(argv[1]); nav_load(argv[2]);
	FILE *o = fopen(argv[3], "wb"); int tot = 0, ok = 0;
	for (int a = 0; a < nnav; a++) { unsigned char m = 0;
		for (int k = 0; k < 3; k++) { int b = navt[a].nb[k]; if (b < 0) continue; tot++;
			int pass = try_cross(a, b, k, 0.5f) || try_cross(a, b, k, 0.25f) || try_cross(a, b, k, 0.75f);
			if (pass) ok++; else { m |= 1 << k; if (getenv("PRINT")) printf("blocked %d -> %d (edge %d) at %.0f %.0f %.0f dz %.0f\n", a, b, k, (navt[a].v[k][0] + navt[a].v[(k + 1) % 3][0]) / 2, (navt[a].v[k][1] + navt[a].v[(k + 1) % 3][1]) / 2, navt[a].c[2], navt[b].c[2] - navt[a].c[2]); } }
		if (nav_swp[a]) m = (m & 2) | ((m & 1) << 2) | ((m & 4) >> 2);   // back to file edge order
		fputc(m, o); }
	fclose(o); printf("edges %d passable %d blocked %d\n", tot, ok, tot - ok);
}
