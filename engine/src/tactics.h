// Bot tactics through bots.h's goal hook (modest: where to walk; fighting stays in bots.h). Before this, a bot with no
// objective walked straight to a live enemy's position (or the player's): every bot knew where everyone was. Now a team only
// knows what it has seen or heard:
//   - contact: a teammate's sighting (an engaged teammate's last-seen spot) or gunfire heard within ~35 m pulls bots in;
//   - buddies: a bot alone far from its team joins a teammate;
//   - patrol: otherwise the bots sweep the middle of the map (spawn points nearest the line between the two team bases).
//   Demolition defenders also rotate to a site where the attackers were spotted; Elimination teams hunt the last survivors.
// Included by elim_game.h; tactics_setup() runs at every match start (from elim_match_setup), after the mode's own setup;
// tactics_frame() each frame (from elim_frame).
#pragma once
static float tac_base[2][2]; static int tac_bases_ok, tac_nspawn = -1;
static float tac_pshot_age = 99, tac_pshot[3];   // the player's last shot: how long ago, where
static void tactics_frame(float dt) {
	tac_pshot_age += dt;
	if (EMB.pflash && *EMB.pflash > 0 && EMB.pl && !*EMB.fly) { tac_pshot_age = 0; tac_pshot[0] = EMB.pl->pos.x; tac_pshot[1] = EMB.pl->pos.y; tac_pshot[2] = EMB.pl->pos.z; }
}
static void tac_bases(void) {   // centres of the two team bases (team_bases' spawn groups), once per map
	if (tac_nspawn == nspawn) return; tac_nspawn = nspawn; tac_bases_ok = 0;
	float sx[2] = { 0 }, sy[2] = { 0 }; int n[2] = { 0 };
	for (int i = 0; i < nspawn; i++) { int g = spawns[i].gi; if (!teams_ok || g < 0 || g >= 64) continue; int t = team_of_group[g]; sx[t] += spawns[i].x; sy[t] += spawns[i].y; n[t]++; }
	if (n[0] && n[1]) { for (int t = 0; t < 2; t++) { tac_base[t][0] = sx[t] / n[t]; tac_base[t][1] = sy[t] / n[t]; } tac_bases_ok = 1; }
}
// what team `team` knows about its enemies: the freshest sighting or gunfire nearest to (x, y); 1 = found
static int tac_contact(int team, float x, float y, float *out) {
	float bd = 1e18f; int found = 0;
	for (int j = 0; j < nbot; j++) { const Bot *t = &bots[j]; if (t->team != team || t->state == BS_DEAD) continue;   // teammates' sightings
		if (t->state == BS_ENGAGE && t->lost_t < 6) { float dx = t->last_seen.x - x, dy = t->last_seen.y - y, d = dx * dx + dy * dy;
			if (d < bd) { bd = d; out[0] = t->last_seen.x; out[1] = t->last_seen.y; out[2] = t->last_seen.z; found = 1; } } }
	for (int j = 0; j < nbot; j++) { const Bot *e = &bots[j]; if (e->team == team || e->state == BS_DEAD || e->flash_t <= 0) continue;   // enemy gunfire heard
		for (int k = 0; k < nbot; k++) { const Bot *t = &bots[k]; if (t->team != team || t->state == BS_DEAD) continue;
			float hx = e->pl.pos.x - t->pl.pos.x, hy = e->pl.pos.y - t->pl.pos.y; if (hx * hx + hy * hy > 3500.f * 3500.f) continue;
			float dx = e->pl.pos.x - x, dy = e->pl.pos.y - y, d = dx * dx + dy * dy; if (d < bd) { bd = d; out[0] = e->pl.pos.x; out[1] = e->pl.pos.y; out[2] = e->pl.pos.z; found = 1; } break; } }
	if (team == 1 && tac_pshot_age < 4) {   // the player's shots in the last seconds, heard by enemies within ~35 m
		for (int k = 0; k < nbot; k++) { const Bot *t = &bots[k]; if (t->team != 1 || t->state == BS_DEAD) continue;
			float hx = tac_pshot[0] - t->pl.pos.x, hy = tac_pshot[1] - t->pl.pos.y; if (hx * hx + hy * hy > 3500.f * 3500.f) continue;
			memcpy(out, tac_pshot, 12); return 1; } }
	return found;
}
static void tac_scatter(float *g, float r) { g[0] += (frand() - 0.5f) * r; g[1] += (frand() - 0.5f) * r; }
// patrol point: of a few random spawn points, the one nearest the middle line between the team bases
static int tac_patrol(int team, float *g) {
	if (!nspawn) return 0; tac_bases();
	int best = -1; float bv = 1e30f;
	for (int k = 0; k < 4; k++) { int s = rand() % nspawn; float v = frand();
		if (tac_bases_ok) { float da = hypotf(spawns[s].x - tac_base[team][0], spawns[s].y - tac_base[team][1]), db = hypotf(spawns[s].x - tac_base[1 - team][0], spawns[s].y - tac_base[1 - team][1]);
			v = fabsf(da - db) - 0.25f * da; }   // the middle, leaning away from home
		if (v < bv) { bv = v; best = s; } }
	if (best < 0) return 0; g[0] = spawns[best].x; g[1] = spawns[best].y; g[2] = spawns[best].z; return 1;
}
static int tac_buddy(int i, float *g) {   // alone (no teammate within 15 m) -> join the nearest teammate (the player counts for team 0)
	const Bot *b = &bots[i]; float bd = 1e18f; int found = 0;
	for (int j = 0; j < nbot; j++) { const Bot *t = &bots[j]; if (j == i || t->team != b->team || t->state == BS_DEAD) continue;
		float dx = t->pl.pos.x - b->pl.pos.x, dy = t->pl.pos.y - b->pl.pos.y, d = dx * dx + dy * dy; if (d < bd) { bd = d; g[0] = t->pl.pos.x; g[1] = t->pl.pos.y; g[2] = t->pl.pos.z; found = 1; } }
	if (b->team == 0 && EMB.pl && EMB.pdead && *EMB.pdead <= 0 && !*EMB.fly) { float dx = EMB.pl->pos.x - b->pl.pos.x, dy = EMB.pl->pos.y - b->pl.pos.y, d = dx * dx + dy * dy;
		if (d < bd) { bd = d; g[0] = EMB.pl->pos.x; g[1] = EMB.pl->pos.y; g[2] = EMB.pl->pos.z; found = 1; } }
	return found && bd > 1500.f * 1500.f;
}
// team deathmatch (and any mode without an objective of its own)
static int tdm_goal(int i, float *g) {
	const Bot *b = &bots[i];
	if (tac_contact(b->team, b->pl.pos.x, b->pl.pos.y, g) && frand() < 0.8f) { tac_scatter(g, 500); return 1; }
	if (frand() < 0.4f && tac_buddy(i, g)) { tac_scatter(g, 350); return 1; }
	return tac_patrol(b->team, g);
}
// Demolition: the base behaviour (dem_goal), plus defenders rotating to the site the attackers were spotted at
static int (*tac_dem_base)(int i, float *g);
static int tac_dem_goal(int i, float *g) {
	if (!tac_dem_base || !tac_dem_base(i, g)) return 0;
	const Bot *b = &bots[i]; float c[3];
	if (dem_on && dm.state == DS_LIVE && !dem_attacking(&dm, b->team) && dmap.nsite > 1 && tac_contact(b->team, b->pl.pos.x, b->pl.pos.y, c) && frand() < 0.7f) {
		int best = -1; float bd = 2800.f * 2800.f;   // a sighting near a site: everyone on defense rotates there
		for (int k = 0; k < dmap.nsite; k++) { float dx = c[0] - dmap.site[k].stand[0], dy = c[1] - dmap.site[k].stand[1], d = dx * dx + dy * dy; if (d < bd) { bd = d; best = k; } }
		if (best >= 0) { memcpy(g, dmap.site[best].stand, 12); tac_scatter(g, 700); }
	}
	return 1;
}
// Elimination: the base behaviour (elim_goal: zones, the middle), and the hunt for the last one or two survivors
static int (*tac_elim_base)(int i, float *g);
static int tac_elim_goal(int i, float *g) {
	if (!tac_elim_base || !tac_elim_base(i, g)) return 0;
	const Bot *b = &bots[i]; float c[3];
	int foes = em.alive[b->team ? 0 : 1];
	if (foes <= 2 && !(em.locked && elim_zone_at(&emap, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z) >= 0) && tac_contact(b->team, b->pl.pos.x, b->pl.pos.y, c)
		&& !(em.locked && elim_zone_at(&emap, c[0], c[1], c[2]) >= 0)) { memcpy(g, c, 12); tac_scatter(g, 400); }
	return 1;
}
static void tactics_setup(void) {
	tac_nspawn = -1;
	if (bot_goal == NULL) bot_goal = tdm_goal;
	else if (bot_goal == dem_goal) { tac_dem_base = dem_goal; bot_goal = tac_dem_goal; }
	else if (bot_goal == elim_goal) { tac_elim_base = elim_goal; bot_goal = tac_elim_goal; }
}
