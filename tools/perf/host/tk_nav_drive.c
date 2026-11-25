// perf b35: Think Tank driving statistics, synchronous vs time-sliced path searches (thinktank.h TK_NAV_ASYNC); tk_nav_stats.sh
// Per run: a TTM map, 2 tanks (one per team) dropped at their team's spawn points, nobody in sight, 120 s of tk_update:
// the tanks walk to the active terminal's area and keep re-planning (ttm_tank_goal). Then the scripted match of tk_test.c.
#define main tk_test_main
#include "tk_test.c"
#undef main
static void drive(const char *map, unsigned seed) {
	char p[256]; snprintf(p, sizeof(p), "%s%s.gcol", MAPS, map); load_col(p); snprintf(p, sizeof(p), "%s%s.gnav", MAPS, map); nav_load(p); snprintf(p, sizeof(p), "%s%s.glgt", MAPS, map); light_load(p);
	int team[1] = { 0 }; V3 pos[1] = { v3(1e6f, 1e6f, 0) }; actors(1, team, pos);
	srand(seed); if (ttm_begin(DATA, map, 4, 900)) { printf("ttm_begin %s failed\n", map); return; }
	const TtmTerm *A = &ttm_map.t[tm.active]; int id[2];
	for (int t = 0; t < 2; t++) { float s[3], y; ttm_spawn_point(t, s, &y); id[t] = tk_spawn(v3(s[0] + (t ? 150 : -150), s[1], s[2] + 2), y, t, -1, "", "tank", 0); }
	float walked[2] = { 0 }, arrive[2] = { -1, -1 }, near_t[2] = { 0 }, dsum[2] = { 0 }; int dead[2] = { 0 }; V3 last[2]; for (int t = 0; t < 2; t++) last[t] = tk[id[t]].pl.pos;
	long long p0 = tk_nav_paths, w0 = tk_nav_wait_sum; int nf = 30 * 120;
	for (int f = 0; f < nf; f++) { tk_update(1 / 30.f);
		for (int t = 0; t < 2; t++) { Tank *k = &tk[id[t]]; if (k->state != TK_LIVE) { dead[t] = 1; continue; }
			V3 q = vsub(k->pl.pos, last[t]); float l = sqrtf(q.x * q.x + q.y * q.y); if (l < 100) walked[t] += l; last[t] = k->pl.pos;
			float dx = k->pl.pos.x - A->dev[0], dy = k->pl.pos.y - A->dev[1], d = sqrtf(dx * dx + dy * dy); dsum[t] += d;
			if (d < 1500) { near_t[t] += 1 / 30.f; if (arrive[t] < 0) arrive[t] = f / 30.f; } } }
	for (int t = 0; t < 2; t++) printf("RUN %s %u %d walked %.1f arrive %.1f near %.1f meand %.1f dead %d\n", map, seed, t, walked[t] / 100, arrive[t] < 0 ? 120 : arrive[t], near_t[t], dsum[t] / nf / 100, dead[t]);
	printf("NAV %s %u paths %lld wait %.2f\n", map, seed, tk_nav_paths - p0, tk_nav_paths > p0 ? (double)(tk_nav_wait_sum - w0) / (tk_nav_paths - p0) : 0);
	ttm_stop();
}
int main(int argc, char **argv) {
	OUT = "/tmp/tkstat_out"; { char c[64]; snprintf(c, 64, "mkdir -p %s", OUT); if (system(c)) {} }
	aud_init(); g_raster = 0;
	model_shader(mk, lg); sprite_init(mk); fx_init(DATA "fx.gfx"); hud_load(DATA "hud.ghud", mk); hud_game_init();
	tk_bind(&pl, &W); tk_hook_bots(); tk_load(DATA);
	int n = argc > 1 ? atoi(argv[1]) : 40, matches = argc > 2 ? atoi(argv[2]) : 0;
	const char *maps[3] = { "underground", "ruinedtokyo", "berutalube" };
	for (int s = 1; s <= n; s++) for (int m = 0; m < 3; m++) drive(maps[m], 1000 + s);
	for (int s = 1; s <= matches; s++) { srand(2000 + s); test_match(); }
	printf("tank paths %lld, waited %.2f frames on average, at most %d\n", tk_nav_paths, tk_nav_paths ? (double)tk_nav_wait_sum / tk_nav_paths : 0, tk_nav_wait_max);
	return 0;
}
