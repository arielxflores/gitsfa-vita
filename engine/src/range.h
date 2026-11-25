// Shooting Range and the tutorial's shooting lesson: the practice targets.
//
// What the original does (footage "Shooting Range to test my modifications with M4A1 assault rifle" + world.dat):
//  - the targets are holographic cyborg dummies (model\Cyborg\Cyborg\OUTFIT_Target_Dummy.nif = the bare cy_male body),
//    glowing cyan, standing in the third-person rifle stance on platforms 30 m, 40 m and 55 m out (and around the arena);
//    7 of the 17 walk back and forth along a 4-point path (world\TutShootBot.tsd)
//  - they never shoot back; a hit makes the target flash orange with a white spark; destroyed, it bursts in a white flash
//    with a horizontal light streak and cyan shards (sound hit_firingrange_target_kill) and fades back in 1.5 s later
//    ([RESET_TIME] 1.5, [LOOP] 1 in world\TutorialStep.txt). Two M4A1 head hits destroy one.
//  - aiming at one shows a red "ENEMY" plate with a health bar above it (it stays a moment after the kill);
//    the crosshair turns red over a target
//  - no score, kill feed, notification, timer, minimap or team; ammo never runs out: when magazine and reserve are both
//    empty the reserve is refilled to 120 (4 magazines); at the top: "[ESC] - Leave Shooting Range" and, in yellow,
//    "The targets in the Shooting Range are practice dummies. There may be differences in damage in actual combat."
//  - the tutorial's shooting lesson (tutorial3, world\TutShootBot1.tsd) shows target groups one after another
//
// Data: <dir><stem>.grange (tools/range/make_grange.py; format described there) and <dir>chr_dummy.gvm
// (tools/range/make_dummy.py). Without chr_dummy.gvm the first loaded character model (bots.h chrt[0]) stands in.
//
// Calls (see the integration notes in the report): range_load() after a map is loaded, range_update() every frame,
// range_aim() for the crosshair, range_hit_test() / range_damage() for the player's shots, range_draw() after the
// bots, range_hud() between hud_begin() and hud_end(), range_refill() after the weapon logic.
#pragma once
#include "bots.h"        // player.h (V3, raycast), model.h, audio.h, sprite.h; bot_sound, wrap_pi, frand
#include "hud_game.h"
#include "vita_prompt.h"   // ui agent: Vita button glyphs in prompts (BRIEF.md Vita prompts rule)
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RG_MAX 32
#define RG_GROUPS 16
#define RG_NPOSE 5          // shared skinned poses: idle (standing targets), walk forward / back / left / right
#define RG_MAXP 192         // effect particles
#define RG_SPAWN_T 0.5f     // fade-in
#define RG_PLATE_T 0.6f     // the name plate stays this long after the crosshair leaves the target
enum { RG_OFF, RG_SPAWN, RG_UP, RG_DEAD };
enum { RGP_GLOW, RGP_SHARD, RGP_STREAK };

typedef struct {
	int id, type, nwp, tsd_b; float travel, wp[4][4], cum[4], len;   // waypoints x y z yaw; cumulative path length
	int state, pose; float t, hp, hit_t, path_t, show_hp;            // t: seconds in this state; path_t: walking clock
	V3 pos, head, chest, pelvis; float yaw;
} RgTarget;
typedef struct { int n, idx[RG_MAX]; char cue[48]; } RgGroup;
typedef struct { float p[3], v[3], age, life, size, rot, spin; int kind; float col[3]; } RgPart;

static struct {
	int ok, loop, n, ngroup, event, group, done; float reset, hp, group_t;
	RgTarget t[RG_MAX]; RgGroup g[RG_GROUPS];
	Model own; Model *M; int model_ok, model_own;
	Anim an[RG_NPOSE]; SkinCache sc[RG_NPOSE]; MBone *pose[RG_NPOSE]; int head_b, pelvis_b, chest_b;
	int aim, plate; float plate_t, done_t;
	int kills, hits; char stem[32];
	RgPart p[RG_MAXP]; int np;
	GLuint tex_glow, tex_shard;
} RG = { 0, .aim = -1, .plate = -1 };

static void rg_mul4(float *o, const float *a, const float *b) {   // column-major o = a * b
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static V3 rg_world(const RgTarget *T, const float *p) {            // model space (faces -y) -> world, as bots.h bot_world
	float th = T->yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	return v3(T->pos.x + c * p[0] - s * p[1], T->pos.y + s * p[0] + c * p[1], T->pos.z + p[2]);
}

// ---- effect textures: a soft white glow and a sharp white shard (made here, no file)
static void rg_textures(void) {
	if (RG.tex_glow) return;
	static unsigned char g[64 * 64 * 4], s[32 * 32 * 4];
	for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { float dx = (x + 0.5f) / 32 - 1, dy = (y + 0.5f) / 32 - 1, r2 = dx * dx + dy * dy;
		float a = expf(-r2 * 5.0f) - expf(-5.0f); if (a < 0) a = 0; unsigned char *p = g + (y * 64 + x) * 4; p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255); }
	for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {   // a long thin triangle (glass splinter)
		float u = (x + 0.5f) / 32, v = (y + 0.5f) / 32, half = 0.42f * (1 - v); float a = (fabsf(u - 0.5f) < half && v > 0.04f) ? 1.f : 0.f;
		unsigned char *p = s + (y * 32 + x) * 4; p[0] = p[1] = p[2] = 255; p[3] = (unsigned char)(a * 255); }
	glGenTextures(1, &RG.tex_glow); glBindTexture(GL_TEXTURE_2D, RG.tex_glow);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, g); VGL_XFER_WAIT();
	glGenTextures(1, &RG.tex_shard); glBindTexture(GL_TEXTURE_2D, RG.tex_shard);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, s); VGL_XFER_WAIT();
}

// ---- the dummy model (once): chr_dummy.gvm, else the first character model
static int rg_model(const char *dir) {
	if (RG.model_ok) return 0;
	char p[160]; snprintf(p, sizeof(p), "%schr_dummy.gvm", dir);
	model_keep_cpu = 1; int r = model_load(&RG.own, p); model_keep_cpu = 0;
	if (r >= 0) { model_prepare_rigid(&RG.own); RG.M = &RG.own; RG.model_own = 1; }
	else if (nchrt > 0) RG.M = &chrt[0].m;   // already prepared for CPU skinning by chrt_load
	else return -1;
	static const char *CL[RG_NPOSE] = { "idle", "walk_f", "walk_b", "walk_l", "walk_r" };
	for (int k = 0; k < RG_NPOSE; k++) {
		anim_init(&RG.an[k], RG.M); int c = model_find_clip(RG.M, CL[k]); if (c < 0) c = model_find_clip(RG.M, "idle");
		if (c < 0) return -2;
		anim_set_base(&RG.an[k], c, 0, 0); RG.an[k].base_t = k * 0.21f; skincache_init(&RG.sc[k], RG.M); RG.pose[k] = NULL;
	}
	RG.head_b = model_find_bone(RG.M, "Bip01_Head"); RG.pelvis_b = model_find_bone(RG.M, "Bip01_Pelvis"); RG.chest_b = model_find_bone(RG.M, "Bip01_Spine2");
	if (RG.chest_b < 0) RG.chest_b = RG.pelvis_b;
	if (RG.head_b < 0 || RG.pelvis_b < 0) return -3;
	RG.model_ok = 1; return 0;
}

// ---- data
static void rg_path(RgTarget *T) {
	T->cum[0] = 0; for (int k = 1; k < T->nwp; k++) { float dx = T->wp[k][0] - T->wp[k - 1][0], dy = T->wp[k][1] - T->wp[k - 1][1], dz = T->wp[k][2] - T->wp[k - 1][2]; T->cum[k] = T->cum[k - 1] + sqrtf(dx * dx + dy * dy + dz * dz); }
	T->len = T->nwp > 1 ? T->cum[T->nwp - 1] : 0;
	if (T->type != 1 || T->len < 1 || T->travel <= 0.05f) { T->type = 0; T->nwp = 1; }
}
// position, facing and walking direction on the path (back and forth: travel seconds one way)
static void rg_place(RgTarget *T) {
	if (T->type != 1) { T->pos = v3(T->wp[0][0], T->wp[0][1], T->wp[0][2]); T->yaw = T->wp[0][3]; T->pose = 0; return; }
	float ph = fmodf(T->path_t / T->travel, 2.f); int back = ph > 1; float s = back ? 2 - ph : ph, d = s * T->len;
	int k = 0; while (k < T->nwp - 2 && d > T->cum[k + 1]) k++;
	float sl = T->cum[k + 1] - T->cum[k], f = sl > 1e-3f ? (d - T->cum[k]) / sl : 0; if (f < 0) f = 0; if (f > 1) f = 1;
	const float *a = T->wp[k], *b = T->wp[k + 1];
	T->pos = v3(a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f);
	T->yaw = a[3] + wrap_pi(b[3] - a[3]) * f;
	float mv = atan2f(b[1] - a[1], b[0] - a[0]) + (back ? 3.14159265f : 0), rel = wrap_pi(mv - T->yaw);
	T->pose = fabsf(rel) < 0.785f ? 1 : fabsf(rel) > 2.356f ? 2 : rel > 0 ? 3 : 4;   // forward, back, to its left, to its right
}
static void rg_spawn(RgTarget *T) { T->state = RG_SPAWN; T->t = 0; T->hp = RG.hp; T->show_hp = RG.hp; T->hit_t = 0; }
static void rg_group_start(int g) {
	RG.group = g; RG.group_t = -1;
	if (g >= RG.ngroup) { RG.done = 1; RG.done_t = 0; voice_play("sh_tut_operator_complete", 220, 0); voice_play("ui_tutorial_success", 200, 0); return; }
	for (int k = 0; k < RG.g[g].n; k++) rg_spawn(&RG.t[RG.g[g].idx[k]]);
	if (RG.g[g].cue[0]) voice_play(RG.g[g].cue, 220, 0);
}
// every target back to its start (map start, player respawn)
static void range_reset(void) {
	if (!RG.ok) return;
	for (int i = 0; i < RG.n; i++) { RgTarget *T = &RG.t[i]; T->state = RG_OFF; T->path_t = 0; rg_place(T); }
	RG.np = 0; RG.aim = RG.plate = -1; RG.plate_t = 0; RG.done = 0; RG.kills = RG.hits = 0;
	if (RG.loop) { for (int i = 0; i < RG.n; i++) { rg_spawn(&RG.t[i]); RG.t[i].t = RG_SPAWN_T; } }   // up straight away
	else rg_group_start(0);
}
static void range_unload(void) { RG.ok = 0; RG.n = RG.ngroup = 0; RG.np = 0; RG.aim = RG.plate = -1; }
// targets of map <stem>: >0 how many, 0 none on this map (no <dir><stem>.grange), <0 error
static int range_load(const char *dir, const char *stem) {
	range_unload(); snprintf(RG.stem, sizeof(RG.stem), "%s", stem);
	char p[160]; snprintf(p, sizeof(p), "%s%s.grange", dir, stem);
	FILE *f = fopen(p, "rb"); if (!f) return 0;
	char mg[4]; unsigned h[7]; float rs, hp;
	if (fread(mg, 4, 1, f) != 1 || memcmp(mg, "GRG1", 4) || fread(h, 4, 2, f) != 2 || fread(&rs, 4, 1, f) != 1 || fread(&hp, 4, 1, f) != 1 || fread(h + 2, 4, 3, f) != 3) { fclose(f); return -1; }
	unsigned flags = h[1], nt = h[3], ng = h[4];
	if (h[0] != 1 || nt > RG_MAX || ng > RG_GROUPS) { fclose(f); return -2; }
	RG.loop = flags & 1; RG.reset = rs > 0 ? rs : 1.5f; RG.hp = hp > 0 ? hp : 100; RG.event = h[2]; RG.n = nt; RG.ngroup = ng;
	for (unsigned i = 0; i < nt; i++) {
		RgTarget *T = &RG.t[i]; memset(T, 0, sizeof(*T)); int a[2]; float tr; int b[2];
		if (fread(a, 4, 2, f) != 2 || fread(&tr, 4, 1, f) != 1 || fread(b, 4, 2, f) != 2 || fread(T->wp, 4, 16, f) != 16) { fclose(f); return -3; }
		T->id = a[0]; T->type = a[1]; T->travel = tr; T->tsd_b = b[0]; T->nwp = b[1] < 1 ? 1 : b[1] > 4 ? 4 : b[1]; rg_path(T);
	}
	for (unsigned g = 0; g < ng; g++) {
		RgGroup *G = &RG.g[g]; unsigned n; memset(G, 0, sizeof(*G));
		if (fread(&n, 4, 1, f) != 1 || n > RG_MAX || fread(G->cue, 48, 1, f) != 1) { fclose(f); return -4; }
		G->cue[47] = 0; G->n = n; if (n && fread(G->idx, 4, n, f) != n) { fclose(f); return -4; }
		for (unsigned k = 0; k < n; k++) if (G->idx[k] < 0 || G->idx[k] >= (int)nt) G->idx[k] = 0;
	}
	fclose(f);
	if (rg_model(dir) < 0) { RG.n = 0; return -5; }
	rg_textures(); RG.ok = 1; range_reset();
	return RG.n;
}
static int range_active(void) { return RG.ok && RG.n > 0; }

// ---- effects
static RgPart *rg_part(void) { if (RG.np < RG_MAXP) return &RG.p[RG.np++]; int b = 0; for (int i = 1; i < RG_MAXP; i++) if (RG.p[i].life - RG.p[i].age < RG.p[b].life - RG.p[b].age) b = i; return &RG.p[b]; }
static void rg_glow(V3 p, float size, float life, float r, float g, float b, int kind) {
	RgPart *q = rg_part(); memset(q, 0, sizeof(*q)); q->p[0] = p.x; q->p[1] = p.y; q->p[2] = p.z; q->size = size; q->life = life; q->kind = kind; q->col[0] = r; q->col[1] = g; q->col[2] = b;
}
static void rg_shards(V3 c, int n, float speed, float size) {
	for (int k = 0; k < n; k++) {
		RgPart *q = rg_part(); memset(q, 0, sizeof(*q)); q->kind = RGP_SHARD;
		float az = frand() * 6.2831853f, el = (frand() - 0.25f) * 1.6f, sp = speed * (0.35f + 0.65f * frand());
		q->p[0] = c.x + (frand() - 0.5f) * 40; q->p[1] = c.y + (frand() - 0.5f) * 40; q->p[2] = c.z + (frand() - 0.5f) * 90;
		q->v[0] = cosf(az) * cosf(el) * sp; q->v[1] = sinf(az) * cosf(el) * sp; q->v[2] = sinf(el) * sp + 120;
		q->size = size * (0.5f + frand()); q->life = 0.55f + 0.5f * frand(); q->rot = frand() * 6.28f; q->spin = (frand() - 0.5f) * 18;
		float w = frand(); q->col[0] = 0.45f + 0.55f * w; q->col[1] = 1; q->col[2] = 0.9f + 0.1f * w;   // cyan to white
	}
}

// ---- per frame
static void range_update(float dt, V3 eye) {
	if (!range_active()) return;
	(void)eye;
	int used[RG_NPOSE] = { 0 };
	for (int i = 0; i < RG.n; i++) {
		RgTarget *T = &RG.t[i]; T->t += dt; if (T->hit_t > 0) T->hit_t -= dt;
		if (T->type == 1) T->path_t += dt;
		rg_place(T);
		if (T->state == RG_SPAWN && T->t >= RG_SPAWN_T) { T->state = RG_UP; T->t = 0; }
		if (T->state == RG_DEAD && RG.loop && T->t >= RG.reset) rg_spawn(T);
		T->show_hp += (T->hp - T->show_hp) * fminf(1, 10 * dt);
		if (T->state != RG_OFF && T->state != RG_DEAD) used[T->pose] = 1;
	}
	used[0] = 1;
	for (int k = 0; k < RG_NPOSE; k++) if (used[k] || !RG.pose[k]) { anim_update(&RG.an[k], dt); RG.pose[k] = anim_pose(&RG.an[k], -1); }
	for (int i = 0; i < RG.n; i++) {   // world points for hit tests and the name plate, from the pose each target is drawn with
		RgTarget *T = &RG.t[i]; MBone *P = RG.pose[T->pose] ? RG.pose[T->pose] : RG.pose[0];
		float hp[3] = { P[RG.head_b].p[0], P[RG.head_b].p[1], P[RG.head_b].p[2] + 8 };
		T->head = rg_world(T, hp); T->pelvis = rg_world(T, P[RG.pelvis_b].p); T->chest = rg_world(T, P[RG.chest_b].p);
	}
	if (!RG.loop && !RG.done && RG.group < RG.ngroup) {   // lesson: the next group once this one is cleared
		int alive = 0; const RgGroup *G = &RG.g[RG.group];
		for (int k = 0; k < G->n; k++) if (RG.t[G->idx[k]].state != RG_DEAD) alive++;
		if (!alive) { if (RG.group_t < 0) RG.group_t = RG.reset; RG.group_t -= dt; if (RG.group_t <= 0) { for (int k = 0; k < G->n; k++) RG.t[G->idx[k]].state = RG_OFF; rg_group_start(RG.group + 1); } }
	}
	if (RG.done) RG.done_t += dt;
	if (RG.plate_t > 0) RG.plate_t -= dt;
	for (int i = 0; i < RG.np; ) {
		RgPart *q = &RG.p[i]; q->age += dt;
		if (q->age >= q->life) { RG.p[i] = RG.p[--RG.np]; continue; }
		if (q->kind == RGP_SHARD) { q->v[2] -= 700 * dt; float dr = 1 - fminf(1, 1.5f * dt); for (int k = 0; k < 3; k++) { q->v[k] *= dr; q->p[k] += q->v[k] * dt; } q->rot += q->spin * dt; }
		i++;
	}
}

// b55 (user: "a wall around the area" stopped every shot at the Shooting Range's targets): the range's collision is
// player collision only (no bullet meshes), and its tall invisible ring (16 sides, ~21-27 m from the spawns, 30 m high)
// keeps the player in while the targets stand outside it (make_grange.py). Its walls - vertical, over 10 m tall, nearer to
// the spawns than any target - still stop the player, no longer bullets. Call after the collision is loaded.
static void range_col_fix(void) {
	if (!RG.n || !RG.loop || !nctri || nspawn <= 0) return;
	float cx = 0, cy = 0; for (int i = 0; i < nspawn; i++) { cx += spawns[i].x; cy += spawns[i].y; } cx /= nspawn; cy /= nspawn;
	float tmin = 1e9f; for (int i = 0; i < RG.n; i++) for (int k = 0; k < (RG.t[i].type == 1 ? RG.t[i].nwp : 1) && k < 4; k++) tmin = fminf(tmin, hypotf(RG.t[i].wp[k][0] - cx, RG.t[i].wp[k][1] - cy));
	int n = 0;
	for (int i = 0; i < nctri; i++) { Tri *t = &ctris[i]; if (!(t->flags & TF_BULLET) || fabsf(t->n.z) > 0.2f || t->zmax - t->zmin < 1000) continue;
		float mx = (t->a.x + t->b.x + t->c.x) / 3 - cx, my = (t->a.y + t->b.y + t->c.y) / 3 - cy; if (hypotf(mx, my) >= tmin) continue;
		t->flags &= ~TF_BULLET; n++; }
	if (n) { col_rebuild_lists(); logp("range: %d boundary walls let shots through (targets from %.0f m)\n", n, tmin / 100); }
}
// ray against the targets (player bullets): index or -1; *t distance, *head 1 for a head hit (same shapes as bots.h)
static int range_hit_test(V3 o, V3 d, float maxd, float *t, int *head) {
	if (!range_active()) return -1;
	int hit = -1; float bt = maxd;
	for (int i = 0; i < RG.n; i++) {
		RgTarget *T = &RG.t[i]; if (T->state != RG_UP && T->state != RG_SPAWN) continue;
		V3 c = vsub(T->chest, o); if (vdot(c, d) < -100 || vdot(c, c) > (bt + 200) * (bt + 200)) continue;
		float th = ray_sphere(o, d, T->head, 15);
		float tb = ray_capsule(o, d, v3(T->pelvis.x, T->pelvis.y, T->pos.z + 22), T->chest, 26);
		float tt = -1; int hh = 0;
		if (th >= 0 && (tb < 0 || th <= tb + 10)) { tt = th; hh = 1; } else if (tb >= 0) tt = tb;
		if (tt >= 0 && tt < bt) { bt = tt; hit = i; *head = hh; }
	}
	if (hit >= 0) *t = bt;
	return hit;
}
// the target under the crosshair (-1: none); it gets the name plate. maxd: distance to the first wall along the ray
static int range_aim(V3 eye, V3 dir, float maxd) {
	float t; int h; int i = range_hit_test(eye, dir, maxd > 0 ? maxd : 15000, &t, &h);
	RG.aim = i; if (i >= 0) { RG.plate = i; RG.plate_t = RG_PLATE_T; }
	return i;
}
// a hit on target i at world point hp travelling along dir; returns 1 when it is destroyed
static int range_damage(int i, float dmg, int head, V3 hp, V3 dir) {
	if (!range_active() || i < 0 || i >= RG.n) return 0;
	RgTarget *T = &RG.t[i]; if (T->state != RG_UP && T->state != RG_SPAWN) return 0;
	(void)head; (void)dir;
	T->hp -= dmg; T->hit_t = 0.12f; RG.hits++; RG.plate = i; RG.plate_t = RG_PLATE_T;
	rg_glow(hp, 45, 0.09f, 1, 1, 1, RGP_GLOW); rg_shards(hp, 3, 220, 7);
	if (T->hp > 0) return 0;
	T->hp = 0; T->state = RG_DEAD; T->t = 0; RG.kills++;
	if (RG.kills <= 3 || RG.kills % 10 == 0) logp("range: target destroyed (%d destroyed, %d hits)\n", RG.kills, RG.hits);   // b55: the device / test log shows the range works
	V3 c = vadd(T->chest, v3(0, 0, -10));
	rg_glow(c, 170, 0.16f, 1, 1, 1, RGP_GLOW); rg_glow(c, 90, 0.30f, 0.55f, 1, 0.95f, RGP_GLOW); rg_glow(c, 420, 0.14f, 0.7f, 1, 1, RGP_STREAK);
	rg_shards(c, 30, 430, 13);
	ev_play("impact/target_kill", &c.x);
	return 1;
}

// grenade blast at c: every standing target within radius (and in the open from c) takes dmg scaled down with distance;
// returns how many were destroyed
static int range_blast(V3 c, float radius, float dmg) {
	if (!range_active()) return 0;
	int n = 0;
	for (int i = 0; i < RG.n; i++) { RgTarget *T = &RG.t[i]; if (T->state != RG_UP && T->state != RG_SPAWN) continue;
		V3 e = vsub(T->chest, c); float d = sqrtf(vdot(e, e)); if (d > radius || !bot_los(c, T->chest)) continue;
		n += range_damage(i, dmg * powf(1 - d / radius, 0.9f) + 10, 0, T->chest, vmul(e, 1 / (d + 1))); }
	return n;
}
// nearest standing target the eye can see (autoplay aiming): index or -1, *aim = its chest
static int range_visible_nearest(V3 eye, float maxd, V3 *aim) {
	if (!range_active()) return -1;
	int best = -1; float bd = maxd;
	for (int i = 0; i < RG.n; i++) { RgTarget *T = &RG.t[i]; if (T->state != RG_UP) continue;
		V3 d = vsub(T->chest, eye); float l = sqrtf(vdot(d, d)); if (l < bd && bot_los(eye, T->chest)) { bd = l; best = i; } }
	if (best >= 0 && aim) *aim = RG.t[best].chest;
	return best;
}

// ---- drawing: depth pre-pass (so each dummy shows only its front surface), then additive cyan
static void rg_draw_one(const float *VP, RgTarget *T, int pass, int *skinned) {
	float th = T->yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	float Mw[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, T->pos.x, T->pos.y, T->pos.z, 1 }, MVP[16]; rg_mul4(MVP, VP, Mw);
	static const float ld[4] = { 0.25f, -0.55f, 0.80f, 0 }, nofog[4] = { 0, 0, 0, 0 };   // model space: from the front and above
	float a = 0.85f, tint[4] = { 1, 1, 1, 1 };
	if (T->state == RG_SPAWN) { float k = T->t / RG_SPAWN_T; a *= k * k * (0.75f + 0.25f * sinf(T->t * 70)); }
	if (T->hit_t > 0) { tint[0] = 4.5f; tint[1] = 0.8f; tint[2] = 0.3f; }   // hit: orange flash (the texture is cyan: red is boosted most)
	tint[3] = a;
	int k = T->pose; MBone *P = RG.pose[k] ? RG.pose[k] : RG.pose[0]; if (!RG.pose[k]) k = 0;
	(void)pass;
	// b73: the dummy's LOD by its height on screen (chr_dummy.gvm: body 6000 / 3000 / 1500 triangles; a 180 cm figure over
	// ~110 px: the nearest, ~50 px: the middle one); both passes of a dummy pick the same one
	float q3 = VP[3] * T->pos.x + VP[7] * T->pos.y + VP[11] * (T->pos.z + 90) + VP[15], vpy = sqrtf(VP[1] * VP[1] + VP[5] * VP[5] + VP[9] * VP[9]);
	float px = q3 > 1 ? 180 * vpy / q3 * 272 : 1e9f; md_lod_sel = px > 110 ? 0 : px > 50 ? 1 : 2;
	model_draw_cpu(RG.M, &RG.sc[k], MVP, P, ld, tint, nofog, !skinned[k]); skinned[k] = 1;
	md_lod_sel = 0;
}
static int rg_visible(const float *VP, const RgTarget *T) {
	V3 d = vsub(T->pos, lis_pos); if (vdot(d, d) > 9000.f * 9000.f) return 0;
	float q[4]; for (int r = 0; r < 4; r++) q[r] = VP[r] * T->pos.x + VP[4 + r] * T->pos.y + VP[8 + r] * (T->pos.z + 90) + VP[12 + r];
	return !(q[3] < -150 || q[0] > q[3] + 260 || q[0] < -q[3] - 260 || q[1] > q[3] + 400 || q[1] < -q[3] - 400);
}
// VP: view-projection (column-major); cam: camera position, right / up: camera axes in world space (for the effects)
static void range_draw(const float *VP, const float *cam, const float *right, const float *up) {
	if (!range_active()) return;
	(void)cam;
	int vis[RG_MAX], nv = 0, skinned[RG_NPOSE] = { 0 };
	for (int i = 0; i < RG.n; i++) { RgTarget *T = &RG.t[i]; if ((T->state == RG_UP || T->state == RG_SPAWN) && rg_visible(VP, T)) vis[nv++] = i; }
	glDisable(GL_CULL_FACE);
	if (nv) {
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
		for (int k = 0; k < nv; k++) if (RG.t[vis[k]].state == RG_UP) rg_draw_one(VP, &RG.t[vis[k]], 0, skinned);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDepthMask(GL_FALSE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		for (int k = 0; k < nv; k++) rg_draw_one(VP, &RG.t[vis[k]], 1, skinned);
		glDepthMask(GL_TRUE); glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	if (RG.np && right && up) {   // effects: glows and streaks, then shards (additive)
		for (int pass = 0; pass < 2; pass++) {
			for (int i = 0; i < RG.np; i++) {
				RgPart *q = &RG.p[i]; if ((q->kind == RGP_SHARD) != pass) continue;
				float f = q->age / q->life, al = 1 - f, col[4] = { q->col[0], q->col[1], q->col[2], 1 }, A[3], B[3], C[3], D[3];
				if (q->kind == RGP_SHARD) {
					col[3] = al > 0.6f ? 1 : al / 0.6f; float cr = cosf(q->rot), sr = sinf(q->rot), w = q->size * 0.45f, h = q->size;
					float ax[3], ay[3]; for (int k = 0; k < 3; k++) { ax[k] = (cr * right[k] + sr * up[k]) * w; ay[k] = (-sr * right[k] + cr * up[k]) * h; }
					for (int k = 0; k < 3; k++) { A[k] = q->p[k] - ax[k] + ay[k]; B[k] = q->p[k] + ax[k] + ay[k]; C[k] = q->p[k] + ax[k] - ay[k]; D[k] = q->p[k] - ax[k] - ay[k]; }
				} else {
					float sz = q->size * (q->kind == RGP_GLOW ? 0.6f + 0.6f * f : 1), w = sz, h = q->kind == RGP_STREAK ? 7 : sz;
					col[3] = al * al;
					for (int k = 0; k < 3; k++) { A[k] = q->p[k] - right[k] * w + up[k] * h; B[k] = q->p[k] + right[k] * w + up[k] * h; C[k] = q->p[k] + right[k] * w - up[k] * h; D[k] = q->p[k] - right[k] * w - up[k] * h; }
				}
				sprite_quad(A, B, C, D, 0, 0, 1, 1, col);
			}
			sprite_flush(VP, pass ? RG.tex_shard : RG.tex_glow, 1);
		}
	}
	glEnable(GL_CULL_FACE); glDepthMask(GL_TRUE);
}

// ---- HUD (call between hud_begin() and hud_end()): the range's text at the top and the aimed target's name plate
static void range_hud(const float *VP, float now) {
	if (!range_active() || !HG.ok || HG.screen < 0) return;
	(void)now; HUD_TAG("range.top");   // ui b42: element names for the host overlap checker (no-op on the Vita)
	static const float W[4] = { 1, 1, 1, 0.95f }, Y[4] = { 0.74f, 0.86f, 0.20f, 0.95f }, RED[4] = { 0.95f, 0.22f, 0.27f, 1 }, DARK[4] = { 0.05f, 0.0f, 0.0f, 0.55f };
	if (RG.loop) {
		// ui agent: the START button glyph instead of the PC-style "[START]", and the Legibility rule's sizes (was 12 / 9.5 px)
		vp_text(480, 44, 24, 2, W, "{start}  Leave Shooting Range");
		hud_text(HG.screen, 480, 72, 20, 2, Y, HUD_Z4, "The targets in the Shooting Range are practice dummies.");
		hud_text(HG.screen, 480, 96, 20, 2, Y, HUD_Z4, "There may be differences in damage in actual combat.");
	} else {
		char b[96]; int left = 0;
		if (!RG.done && RG.group < RG.ngroup) for (int k = 0; k < RG.g[RG.group].n; k++) if (RG.t[RG.g[RG.group].idx[k]].state != RG_DEAD) left++;
		vp_text(480, 44, 24, 2, W, "{start}  Leave Tutorial");   // ui agent: START glyph, 24 px (was "[START] - ..." at 12 px)
		if (RG.done) snprintf(b, sizeof(b), "Shooting practice complete"); else snprintf(b, sizeof(b), "Destroy the targets: %d left   (step %d of %d)", left, RG.group + 1, RG.ngroup);
		hud_text(HG.screen, 480, 74, 24, 2, Y, HUD_Z4, b);   // ui agent: 24 px (was 10)
	}
	HUD_TAG("~range.plate");
	if (RG.plate >= 0 && RG.plate_t > 0 && HG.white >= 0) {   // red "ENEMY" plate above the head, health bar under the name
		RgTarget *T = &RG.t[RG.plate]; if (T->state == RG_OFF) return;
		float q[4], hx = T->head.x, hy = T->head.y, hz = T->head.z + 26;
		for (int r = 0; r < 4; r++) q[r] = VP[r] * hx + VP[4 + r] * hy + VP[8 + r] * hz + VP[12 + r];
		if (q[3] < 5) return;
		float sx = (q[0] / q[3] + 1) * 480, sy = (1 - q[1] / q[3]) * 272, fade = RG.plate_t > 0.2f ? 1 : RG.plate_t / 0.2f;
		float red[4] = { RED[0], RED[1], RED[2], RED[3] * fade }, dark[4] = { DARK[0], DARK[1], DARK[2], DARK[3] * fade };
		float A1[6] = { 6, 0, sx - 7, 0, 5, sy - 13 }; hud_img(HG.white, HG.screen, A1, red, HUD_Z4, -1, 1);                 // flag
		float A2[6] = { 1, 0, sx - 7, 0, 9, sy - 13 }; hud_img(HG.white, HG.screen, A2, red, HUD_Z4, -1, 1);                 // flag pole
		hud_text(HG.screen, sx + 1, sy - 8, 20, 0, red, HUD_Z4, "ENEMY");   // ui agent: 20 px (was 9)
		float bw = 42, fr = T->show_hp / RG.hp; if (fr < 0) fr = 0; if (fr > 1) fr = 1;
		float A3[6] = { bw, 0, sx + 1, 0, 2.5f, sy - 5 }; hud_img(HG.white, HG.screen, A3, dark, HUD_Z4, -1, 1);
		if (fr > 0) { float A4[6] = { bw * fr, 0, sx + 1, 0, 2.5f, sy - 5 }; hud_img(HG.white, HG.screen, A4, red, HUD_Z4, -1, 1); }
	}
}

// the range never runs dry: magazine and reserve both empty -> 4 magazines in reserve (the footage: 0|0 -> 0|120 -> 30|90).
// Returns 1 when it refilled.
static int range_refill(int *mag, int *reserve, int cap) {
	if (!range_active() || cap <= 0 || *mag > 0 || *reserve > 0) return 0;
	*reserve = 4 * cap; return 1;
}
// one line for the log
static int range_status(char *b, int n) {
	if (!range_active()) return snprintf(b, n, "range: off");
	int up = 0, dead = 0; for (int i = 0; i < RG.n; i++) { if (RG.t[i].state == RG_UP || RG.t[i].state == RG_SPAWN) up++; else if (RG.t[i].state == RG_DEAD) dead++; }
	return snprintf(b, n, "range %s: %d targets (%d up, %d down), %s %d/%d, hits %d, destroyed %d, effects %d", RG.stem, RG.n, up, dead,
		RG.loop ? "loop" : RG.done ? "lesson done, group" : "group", RG.loop ? 1 : RG.group < RG.ngroup ? RG.group + 1 : RG.ngroup, RG.loop ? 1 : RG.ngroup, RG.hits, RG.kills, RG.np);
}
