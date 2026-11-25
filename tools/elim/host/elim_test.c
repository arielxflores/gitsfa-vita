// Host check of elim.h: loads the .gelim files, tests zone queries at the spawns / safe points, and runs the round
// machine through scripted rounds (team wipes, a time-up draw, the side switch) printing every event with its time.
// build: gcc -O1 -o /tmp/elim_test elim_test.c -lm && /tmp/elim_test ../../../build/data/
#include "../../../engine/src/elim.h"
static const char *EVN[] = { "RESET", "START", "ZONE_WARN", "EVAC", "COUNT", "LOCK", "TIMEWARN", "ROUND_END", "SWITCH", "MATCH_END", "ONE_LEFT_0", "ONE_LEFT_1", "RESPAWN_SND" };
int main(int argc, char **argv) {
	const char *D = argc > 1 ? argv[1] : "build/data/"; char p[256]; int bad = 0;
	const char *maps[3] = { "urban", "port", "ghost_storage" };
	for (int k = 0; k < 3; k++) {
		ElimMap M; snprintf(p, sizeof(p), "%s%s.gelim", D, maps[k]); int r = elim_load(&M, p);
		printf("%s: load %d, spawns %d/%d, zones %d, barriers %d, safe %d\n", maps[k], r, M.nsp[0], M.nsp[1], M.nzone, M.nbar, M.nsafe);
		if (r) { bad++; continue; }
		int inz = 0, safe_in = 0; for (int s = 0; s < 2; s++) for (int i = 0; i < M.nsp[s]; i++) inz += elim_zone_at(&M, M.sp[s][i].x, M.sp[s][i].y, M.sp[s][i].z) >= 0;
		for (int i = 0; i < M.nsafe; i++) safe_in += elim_zone_at(&M, M.safe[i][0], M.safe[i][1], M.safe[i][2]) >= 0;
		printf("  spawns inside a lockable zone: %d of %d; safe points inside: %d\n", inz, M.nsp[0] + M.nsp[1], safe_in);
		if (safe_in) bad++;
		elim_free(&M);
	}
	ElimMap M; snprintf(p, sizeof(p), "%surban.gelim", D); elim_load(&M, p);
	ElimMatch m; elim_start(&m, &M, 0); float T = 0, dt = 1 / 60.f; int last_round = 0;
	// script: round 1 team 1 wiped at 30 s, round 2 time up, round 3.. alternate; until the match ends
	while (m.state != ES_MATCH_END && T < 2000) {
		int a0 = 3, a1 = 3; float into = m.state == ES_LIVE ? m.round_time - m.round_left : 0;
		if (m.state == ES_LIVE && m.round % 3 == 1 && into > 30) a1 = 0;
		if (m.state == ES_LIVE && m.round % 3 == 0 && into > 50) a0 = into > 55 ? 0 : 1;
		if (m.state == ES_LIVE && into > 20 && m.round % 3 == 0) a1 = 1;
		elim_set_alive(&m, a0, a1); unsigned ev = elim_update(&m, dt); T += dt;
		for (int b = 0; b < 13; b++) if (ev & (1u << b)) {
			if ((1u << b) == EEV_COUNT && m.round > 2) continue;
			if (m.round <= 2 || (1u << b) & (EEV_RESET | EEV_ROUND_END | EEV_SWITCH | EEV_MATCH_END))
				printf("t %7.2f round %d (%s) left %5.2f  %-11s score %d-%d%s\n", T, m.round, m.state == ES_FREEZE ? "freeze" : m.state == ES_LIVE ? "live" : "end", m.round_left, EVN[b], m.score[0], m.score[1],
					(1u << b) == EEV_COUNT ? (char[16]){0} : "");
			if ((1u << b) == EEV_COUNT) printf("            countdown %d\n", m.count_left);
		}
		if (m.round != last_round) last_round = m.round;
	}
	printf("match over at %.1f s: %d - %d after %d rounds (switched %d)\n", T, m.score[0], m.score[1], m.round, m.switched);
	ElimMatch q; elim_start(&q, &M, 0); elim_set_round_time(&q, 45); printf("test-run timeline: round %.0f s, warn %.1f, evac %.1f, lock %.1f, timewarn %.1f\n", q.round_time, q.warn_at, q.evac_at, q.lock_at, q.timewarn_at);
	return bad;
}
