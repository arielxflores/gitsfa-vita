// Host simulation of the match loop's CPU side for the perf work (b35): engine/src/bots.h (AI, navigation, physics,
// chr.h animation + pose evaluation) on a real map with real characters, 8 bots, the player walking a loop. It times the
// subsystems (cpuprof.h accumulators, the same ones the Vita's per-second "cpu:" line prints) and hashes the simulation
// state every frame, so a run with the jobs.h workers can be compared bit for bit with a run without them.
// Build + run: tools/perf/host/match_sim.sh <map stem> [seconds] [jobs 0/1] [seed]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef CG
#include <valgrind/callgrind.h>
#define CG_ON() CALLGRIND_TOGGLE_COLLECT
#else
#define CG_ON()
#endif
#include "vitaGL.h"
#include "jobs.h"
#include "player.h"
static int bot_cam_hidden;
#include "bots.h"
#include "cpuprof.h"
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static unsigned long long fnv(unsigned long long h, const void *p, size_t n) { const unsigned char *c = p; for (size_t i = 0; i < n; i++) { h ^= c[i]; h *= 1099511628211ull; } return h; }
static unsigned long long state_hash(void) {
	unsigned long long h = 1469598103934665603ull;
	for (int i = 0; i < nbot; i++) { const Bot *b = &bots[i];
		h = fnv(h, &b->pl.pos, sizeof(V3)); h = fnv(h, &b->pl.vel, sizeof(V3)); h = fnv(h, &b->pl.yaw, 4); h = fnv(h, &b->state, 4); h = fnv(h, &b->hp, 4);
		h = fnv(h, &b->tgt, 4); h = fnv(h, &b->npath, 4); h = fnv(h, &b->ipath, 4); h = fnv(h, &b->kills, 4); h = fnv(h, &b->deaths, 4);
		h = fnv(h, &b->head, sizeof(V3)); h = fnv(h, &b->chest, sizeof(V3)); h = fnv(h, &b->muzzle, sizeof(V3));
		if (b->pose) h = fnv(h, b->mpose, sizeof(MBone) * chrt[b->ct].bind.n); }
	h = fnv(h, &bot_dmg_to_player, 4);
	return h;
}
int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: match_sim <data dir> <maps dir> <map stem> [seconds] [jobs] [seed] [nbots]\n"); return 1; }
	const char *data = argv[1], *mdir = argv[2], *stem = argv[3]; float secs = argc > 4 ? atof(argv[4]) : 30; int use_jobs = argc > 5 ? atoi(argv[5]) : 0;
	unsigned seed = argc > 6 ? (unsigned)atoi(argv[6]) : 7; int nb = argc > 7 ? atoi(argv[7]) : 8;
	char p[256];
	snprintf(p, sizeof(p), "%s/%s.gcol", mdir, stem); if (load_col(p) < 0) { fprintf(stderr, "no col %s\n", p); return 1; }
	snprintf(p, sizeof(p), "%s/%s.gnav", mdir, stem); if (nav_load(p) < 0) fprintf(stderr, "no nav\n");
	snprintf(p, sizeof(p), "%s/%s.glgt", mdir, stem); light_load(p);
	model_shader(mk, lg);
	static const char *CHARS[] = { "bato", "motoko", "saito", "togusa", "ishikawa", "borma", "paz", "azuma", "kuro", "maven", "reiko", "sitara" };
	for (int i = 0; i < 12; i++) { snprintf(p, sizeof(p), "%s/chr_%s.gvm", data, CHARS[i]); chrt_load(p, CHARS[i]); }
	if (use_jobs) jobs_init();
	srand(seed);
	BotTarget T = { v3(spawns[0].x, spawns[0].y, spawns[0].z + 5), v3(0, 0, 0), 1 };
	if (bots_init(nb, &T) < 0) { fprintf(stderr, "bots_init failed (%d characters)\n", nchrt); return 1; }
	for (int i = 0; i < nbot; i++) { bots[i].team = i < 3 ? 0 : 1; bot_spawn(&bots[i], &T); }
	int allvis = getenv("ALLVIS") != NULL;   // every bot counts as drawn (the worst case: all pose every frame)
	float dt = 1 / 60.f; int nf = (int)(secs / dt);
	long long nav_worst = 0; int nav_spiky = 0;
	unsigned long long h = 0, t_all = 0; double worst = 0, dmg = 0, dist = 0; V3 last[NBOT_MAX]; for (int i = 0; i < nbot; i++) last[i] = bots[i].pl.pos;
	for (int f = 0; f < nf; f++) {
		float t = f * dt;
		T.pos = v3(spawns[0].x + 600 * cosf(t * 0.15f), spawns[0].y + 600 * sinf(t * 0.15f), spawns[0].z + 5);
		float yaw = t * 0.15f + 1.5707963f; lis_pos = v3(T.pos.x, T.pos.y, T.pos.z + 160); lis_yaw = yaw;
		long long ne0 = nav_exp_count; unsigned long long t0 = cp_us(); CG_ON(); bots_update(dt, &T); CG_ON(); unsigned long long d = cp_us() - t0; t_all += d; if (d > worst) worst = d;
		{ long long ne = nav_exp_count - ne0; if (ne > nav_worst) nav_worst = ne; if (ne > 600) nav_spiky++; }
		// what bots_draw does to the bots' state: the frame counter, which bots were on screen (they pose every frame)
		bot_frame++;
		for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; if (!b->pose) continue; V3 q = vsub(b->pl.pos, lis_pos); float l2 = vdot(q, q); if (l2 > 7000.f * 7000.f) continue;
			float fa = fabsf(wrap_pi(atan2f(q.y, q.x) - yaw)); if (fa > 0.75f && l2 > 300.f * 300.f && !allvis) continue; b->seen_frame = bot_frame; b->pose_new = 0; }
		h = fnv(h, &(unsigned long long){ state_hash() }, 8); dmg += bot_dmg_to_player;
		for (int i = 0; i < nbot; i++) { V3 q = vsub(bots[i].pl.pos, last[i]); float l = sqrtf(vdot(q, q)); if (l < 200) dist += l; last[i] = bots[i].pl.pos; }
		if ((f + 1) % 600 == 0) printf("t %5.1f hash %016llx\n", t + dt, h);
	}
	printf("frames %d, bots %d, jobs %s (%d workers): bots_update %.3f ms/frame avg, worst %.3f ms (host)\n", nf, nbot, use_jobs ? "on" : "off", jobs_nworkers(), t_all / 1000.0 / nf, worst / 1000.0);
	cp_print(nf, (void (*)(const char *, ...))printf);
	{ int k = 0, d = 0; for (int i = 0; i < nbot; i++) { k += bots[i].kills; d += bots[i].deaths; }
	  printf("gameplay: kills %d deaths %d shots %d hits %d stuck %d damage to player %.0f bots walked %.0f m\n", k, d, bot_total_shots, bot_total_hits, bot_stuck_respawns, dmg, dist / 100); }
	printf("nav: %lld expansions, worst frame %lld, frames over 600: %d", nav_exp_count, nav_worst, nav_spiky);
#if BOT_NAV_ASYNC
	printf("; paths waited %.1f frames on average, at most %d (%d paths, %d searches)", bnav_wait_n ? (double)bnav_wait_sum / bnav_wait_n : 0, bnav_wait_max, bnav_wait_n, bnav_searches);
#endif
	printf("\n");
	printf("final hash %016llx\n", h);
	return 0;
}
