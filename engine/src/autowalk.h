// Autoplay driver for the test tour: walk the navigation mesh to random spawn points.
// Define AW_LOG(...) before including (logging of stuck spots).
#pragma once
typedef struct { float path[48][3]; int np, ip, goal, stuck_run, reached, stuck, moved; float best, best_t, seg_t, jump_cd, steer_yaw; int steer_n; float crouch_t; int crouch; } AutoWalk;
// aw.crouch: 1 while the walker wants to crouch (a ceiling too low to stand under): the caller copies it to Player.in_crouch

// a route to a random spawn point (preferably 10 m+ away); else to a random reachable nav triangle; else a straight line
static int aw_plan(V3 from, float (*path)[3], int *goal) {
	float s[3] = { from.x, from.y, from.z };
	for (int k = 0; k < 8 && nspawn; k++) {
		int si = rand() % nspawn; float dx = spawns[si].x - from.x, dy = spawns[si].y - from.y;
		if (dx * dx + dy * dy < 1000.f * 1000.f && k < 6) continue;
		float g[3] = { spawns[si].x, spawns[si].y, spawns[si].z }; int n = nnav ? nav_path(s, g, path, 48, 45) : 0;
		if (n > 0) { *goal = si; return n; }
	}
	for (int k = 0; k < 12 && nnav; k++) {
		const NavTri *t = &navt[rand() % nnav]; float dx = t->c[0] - from.x, dy = t->c[1] - from.y;
		if (dx * dx + dy * dy < 600.f * 600.f && k < 10) continue;
		int n = nav_path(s, t->c, path, 48, 45); if (n > 0) { *goal = -1; return n; }
	}
	int si = nspawn ? rand() % nspawn : 0; path[0][0] = nspawn ? spawns[si].x : 0; path[0][1] = nspawn ? spawns[si].y : 0; path[0][2] = nspawn ? spawns[si].z : 0; *goal = si; return 1;
}
static void aw_reset(AutoWalk *w) { w->np = w->ip = 0; w->stuck_run = 0; w->goal = -1; }
// one step: turns the player and sets the stick / sprint / jump it should press. *pitch eases to looking slightly down.
static void aw_step(AutoWalk *w, Player *pl, float *pitch, float dt, float *my, int *sprint, int *jump) {
	*my = 0; *sprint = 0; *jump = 0;
	if (w->np > 0 && w->ip >= w->np) { w->reached++; w->np = 0; }
	if (w->np == 0) { w->np = aw_plan(pl->pos, w->path, &w->goal); w->ip = 0; w->best = 1e9f; w->best_t = w->seg_t = 0; }
	float *p = w->path[w->ip]; float dx = p[0] - pl->pos.x, dy = p[1] - pl->pos.y, d = sqrtf(dx * dx + dy * dy);
	if (d < 55 && fabsf(p[2] - pl->pos.z) < 170) { w->ip++; w->best = 1e9f; w->best_t = w->seg_t = 0; w->stuck_run = 0; return; }
	// head straight for the next waypoint once it is in reach (corners of the nav mesh can sit inside walls)
	if (w->ip + 1 < w->np && pl->grounded && (w->steer_n & 7) == 0 && fabsf(w->path[w->ip + 1][2] - pl->pos.z) < 60 &&
	    player_clear_path(pl->pos, v3(w->path[w->ip + 1][0], w->path[w->ip + 1][1], w->path[w->ip + 1][2]), P_RADIUS)) { w->ip++; w->best = 1e9f; w->best_t = w->seg_t = 0; w->stuck_run = 0; }
	if (w->ip + 1 < w->np && d < 80 && w->seg_t - w->best_t > 0.8f) { w->ip++; w->best = 1e9f; w->best_t = w->seg_t = 0; return; }
	// steer around obstacles the navigation mesh does not know about (re-checked every 4th step)
	float want = atan2f(dy, dx);
	if (d > 50) { if ((w->steer_n++ & 3) == 0) { V3 st = player_steer(pl->pos, v3(dx / d, dy / d, 0), P_RADIUS, fminf(d, 110)); w->steer_yaw = atan2f(st.y, st.x); } want = w->steer_yaw; }
	float dyaw = wrap_pi(want - pl->yaw);
	pl->yaw += dyaw * fminf(1, 7 * dt); *pitch += (-0.08f - *pitch) * fminf(1, 4 * dt);
	*my = fabsf(dyaw) < 1.0f ? 1 : 0.25f; *sprint = d > 500 && fabsf(dyaw) < 0.3f;
	w->seg_t += dt; w->jump_cd -= dt;
	if (d < w->best - 15) { w->best = d; w->best_t = w->seg_t; }
	// blocked: crouch under a low ceiling, else jump (climbs onto ledges, vaults over cover and through windows)
	if (w->seg_t - w->best_t > 0.3f && !pl->crouched) { V3 ah = v3(pl->pos.x + cosf(pl->yaw) * 45, pl->pos.y + sinf(pl->yaw) * 45, pl->pos.z);
		if (body_blocked(ah, P_HEIGHT, 1) && !body_blocked(ah, P_HEIGHT_CROUCH, 1)) w->crouch_t = 2.0f; }
	w->crouch_t -= dt; w->crouch = w->crouch_t > 0;
	if (!w->crouch && w->seg_t - w->best_t > 0.8f && w->jump_cd <= 0) {   // face the waypoint if there is something to climb or vault that way
		V3 dest; float peak; if (d > 1 && player_ledge(pl, v3(dx / d, dy / d, 0), &dest, &peak)) pl->yaw = atan2f(dy, dx);
		*jump = 1; w->jump_cd = 1.0f; }
	if (w->seg_t - w->best_t > 4) {
		w->stuck++; AW_LOG("  stuck on the way to spawn %d at %.0f %.0f %.0f (waypoint %d of %d at %.0f %.0f %.0f, %.0f cm away)\n", w->goal, pl->pos.x, pl->pos.y, pl->pos.z, w->ip + 1, w->np, p[0], p[1], p[2], d);
		if (++w->stuck_run >= 2 && nspawn) { int si = rand() % nspawn; pl->pos = v3(spawns[si].x, spawns[si].y, spawns[si].z + 5); pl->vel = v3(0, 0, 0); w->moved++; w->stuck_run = 0; AW_LOG("  moved to spawn %d\n", si); }
		w->np = 0;
	}
}
