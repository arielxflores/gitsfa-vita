// Worst-case CPU spikes of the round modes, measured on the host (perf, b35): one map, 8 bots, then the one-frame bursts
// the match loop can hit, each in a function of its own so callgrind counts it exactly (instructions; spike_probe.sh):
//   probe_round_reset    Elimination / Demolition round start: bot_spawn for every bot in one frame (main.c, elim_game.h)
//   probe_respawn        one bot respawning (deathmatch, Terminal Conquest waves) after the player moved
//   probe_los            the bots' sight tests (bot_los) between every pair of spawn points 1.5 m up
//   probe_zone_frame     Elimination with the zones locked: elim_zone_at for every bot and the player (elim_frame)
//   probe_reset_frames   the bots_update frames right after a round start (the path searches of every bot)
// Build + run: tools/perf/host/spike_probe.sh <map stem> (CG=1: under callgrind, prints instructions per call)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vitaGL.h"
#include "jobs.h"
#include "player.h"
static int bot_cam_hidden;
#include "bots.h"
#include "cpuprof.h"
#include "elim.h"
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void lg(const char *f, ...) { (void)f; }
static BotTarget T; static ElimMap emap; static int zone_hits;
#define PROBE __attribute__((noinline))
static void player_moves(void) { T.pos.x += 1; }   // (a new position: bot_spawn tests the spawn points again)
PROBE static void probe_round_reset(void) { for (int i = 0; i < nbot; i++) bot_spawn(&bots[i], &T); }
PROBE static void probe_respawn(int i) { bot_spawn(&bots[i], &T); }
static int los_vis;
PROBE static void probe_los(void) { for (int i = 0; i < nspawn; i++) for (int j = i + 1; j < nspawn; j++) los_vis += bot_los(v3(spawns[i].x, spawns[i].y, spawns[i].z + 150), v3(spawns[j].x, spawns[j].y, spawns[j].z + 150)); }
PROBE static void probe_zone_frame(void) {
	for (int i = 0; i < nbot; i++) zone_hits += elim_zone_at(&emap, bots[i].pl.pos.x, bots[i].pl.pos.y, bots[i].pl.pos.z) >= 0;
	zone_hits += elim_zone_at(&emap, T.pos.x, T.pos.y, T.pos.z) >= 0;
}
PROBE static void probe_reset_frame(float dt) { bots_update(dt, &T); }
int main(int argc, char **argv) {
	if (argc < 4) { fprintf(stderr, "usage: spike_probe <data dir> <maps dir> <map stem> [seed]\n"); return 1; }
	const char *data = argv[1], *mdir = argv[2], *stem = argv[3]; unsigned seed = argc > 4 ? (unsigned)atoi(argv[4]) : 7;
	char p[256];
	snprintf(p, sizeof(p), "%s/%s.gcol", mdir, stem); if (load_col(p) < 0) { fprintf(stderr, "no col %s\n", p); return 1; }
	snprintf(p, sizeof(p), "%s/%s.gnav", mdir, stem); if (nav_load(p) < 0) fprintf(stderr, "no nav\n");
	snprintf(p, sizeof(p), "%s/%s.glgt", mdir, stem); light_load(p);
	snprintf(p, sizeof(p), "%s/%s.gelim", data, stem); int em = elim_load(&emap, p) == 0;
	model_shader(mk, lg);
	static const char *CHARS[] = { "bato", "motoko", "saito", "togusa", "ishikawa", "borma", "paz", "azuma", "kuro", "maven", "reiko", "sitara" };
	for (int i = 0; i < 12; i++) { snprintf(p, sizeof(p), "%s/chr_%s.gvm", data, CHARS[i]); chrt_load(p, CHARS[i]); }
	srand(seed);
	T = (BotTarget){ v3(spawns[0].x, spawns[0].y, spawns[0].z + 5), v3(0, 0, 0), 1 };
	if (bots_init(8, &T) < 0) { fprintf(stderr, "bots_init failed\n"); return 1; }
	for (int i = 0; i < nbot; i++) bots[i].team = i < 4 ? 0 : 1;
	int nfar = 0; for (int i = 0; i < nspawn; i++) { float dx = spawns[i].x - T.pos.x, dy = spawns[i].y - T.pos.y; nfar += dx * dx + dy * dy >= 2500.f * 2500.f; }
	printf("%s: %d spawn points (%d beyond 25 m of the player), %d zones%s\n", stem, nspawn, nfar, em ? emap.nzone : 0, em ? "" : " (no .gelim)");
	player_moves(); probe_round_reset();
	for (int i = 0; i < nbot; i++) { player_moves(); probe_respawn(i); }
	probe_los(); printf("  sight tests between the spawn points: %d of %d clear\n", los_vis, nspawn * (nspawn - 1) / 2);
	player_moves(); probe_round_reset();
	long long e0 = nav_exp_count; for (int f = 0; f < 120; f++) { probe_reset_frame(1 / 60.f); bot_frame++; }
	printf("  2 s after a round start: %lld path expansions\n", nav_exp_count - e0);
	if (em) { for (int k = 0; k < 100; k++) probe_zone_frame(); printf("  zone checks: %d inside (100 frames)\n", zone_hits); }
	return 0;
}
