// Host test for dem.h + the .gdem files: loads every DEM map, checks that each site can be planted from its "stand"
// point (eye 152.6 cm above it, looking at its aim point) and runs a scripted match through all round endings.
// build: cc -O2 -o /tmp/dem_test dem_test.c -lm ; run: /tmp/dem_test /root/gits/build/data
#include "dem.h"
static const char *STEMS[] = { "downtown", "ecenter", "pss9", "trainstation", "zioplant" };
static void aim_from_stand(const DemSite *s, float *eye, float *dir) {
	eye[0] = s->stand[0]; eye[1] = s->stand[1]; eye[2] = s->stand[2] + 152.6f;
	const float *c = s->aim;
	float l = 0; for (int k = 0; k < 3; k++) { dir[k] = c[k] - eye[k]; l += dir[k] * dir[k]; } l = sqrtf(l); for (int k = 0; k < 3; k++) dir[k] /= l;
}
static const char *evname(unsigned b) {
	static const char *n[] = { "RESET", "BANNER", "START", "PLANT_BEGIN", "PLANTED", "DEFUSE_BEGIN", "DEFUSED", "EXPLODE", "BEEP1", "BEEP2", "BEEP3",
		"ROUND_END", "SWITCH", "MATCH_END", "ONE_LEFT_0", "ONE_LEFT_1", "INTRO_MUSIC", "DEFUSE_STOP", "PLANT_STOP" };
	for (int i = 0; i < 19; i++) { if (b == (1u << i)) return n[i]; }
	return "?";
}
int main(int argc, char **argv) {
	const char *dir = argc > 1 ? argv[1] : "/root/gits/build/data"; char p[512]; int fails = 0;
	for (int mi = 0; mi < 5; mi++) {
		DemMap M; snprintf(p, sizeof(p), "%s/%s.gdem", dir, STEMS[mi]);
		int r = dem_load(&M, p); if (r) { printf("%s: load failed %d\n", STEMS[mi], r); fails++; continue; }
		printf("%s: %d sites, %d attack / %d defense spawns\n", STEMS[mi], M.nsite, M.nsp[0], M.nsp[1]);
		for (int i = 0; i < M.nsite; i++) {
			float eye[3], d[3], t; aim_from_stand(&M.site[i], eye, d);
			int s = dem_site_ray(&M, eye, d, DEM_REACH_PLANT, &t);
			printf("  site %c %-15s tris %4d  stand (%.0f %.0f %.0f) -> ray hits site %d at %.0f cm%s\n", M.site[i].letter, M.site[i].name, M.site[i].nt,
			       M.site[i].stand[0], M.site[i].stand[1], M.site[i].stand[2], s, t, s == i ? "" : "   <-- NOT PLANTABLE FROM STAND");
			if (s != i) fails++;
		}
		if (mi == 0) {   // scripted match on the first map
			DemMatch m; dem_start(&m, &M, 0); float T = 0, dt = 1 / 30.f; int scenario = 0; float eye[3], d[3]; unsigned all = 0;
			while (m.state != DS_MATCH_END && T < 3600) {
				int att = dem_attacking(&m, 0) ? 0 : 1, def = 1 - att, a[2] = { 3, 3 };
				if (m.state == DS_LIVE || m.state == DS_PLANTED) {
					float rt = DEM_ROUND_TIME - m.round_left;
					switch (scenario % 4) {
					case 0: if (rt > 20) a[def] = 0; break;                                            // defenders eliminated
					case 1: aim_from_stand(&M.site[0], eye, d); dem_use(&m, 1, att, eye, d, 1, dt); break;   // plant A, let it blow
					case 2: if (m.state == DS_LIVE) { aim_from_stand(&M.site[1], eye, d); dem_use(&m, 1, att, eye, d, 1, dt); }
					        else { float e2[3] = { m.bomb_pos[0] + 60, m.bomb_pos[1], m.bomb_pos[2] }, d2[3] = { -1, 0, 0 }; a[att] = 0; dem_use(&m, 7, def, e2, d2, 1, dt); } break;   // plant B, attackers die, defuse
					case 3: break;                                                                     // time runs out
					}
				}
				dem_set_alive(&m, a[0], a[1]);
				unsigned ev = dem_update(&m, dt); all |= ev; T += dt;
				for (int b = 0; b < 19; b++) if (ev & (1u << b) && !((1u << b) & (DEV_BEEP1 | DEV_BEEP2)))
					printf("    t=%7.2f round %2d %-12s score %d-%d%s\n", T, m.round, evname(1u << b), m.score[0], m.score[1],
					       (1u << b) == DEV_ROUND_END ? (m.last_reason == DR_DETONATION ? " (detonation)" : m.last_reason == DR_DEFUSED ? " (defused)" : m.last_reason == DR_TIME ? " (time)" : " (eliminated)") : "");
				if (ev & DEV_RESET) scenario = m.round - 1;
			}
			printf("  match over at %.1f s, %d rounds, score %d-%d, events seen 0x%x\n", T, m.round, m.score[0], m.score[1], all);
			if (!(all & DEV_EXPLODE) || !(all & DEV_DEFUSED) || !(all & DEV_SWITCH) || !(all & DEV_BEEP3)) { printf("  missing events\n"); fails++; }
		}
		dem_free(&M);
	}
	printf(fails ? "FAILS: %d\n" : "all ok\n", fails); return fails != 0;
}
