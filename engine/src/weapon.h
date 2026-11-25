// First-person weapons: the table (weapons.txt, written by vm/make_weapons_txt.py, + the M4A1 built in) and the
// loadout slots, each with its own viewmodel (vm_<short>.gvm: arms + weapon, the game's clips). The four loadout
// viewmodels stay loaded so switching is instant (~14 MB); the armory cycle reloads the primary slot from disk.
#pragma once
#include "model.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

enum { CL_IDLE, CL_WALK, CL_RUN, CL_SPRINT, CL_FIRE, CL_FIRE_ADS, CL_ADS_IDLE, CL_RELOAD, CL_RELOAD_E, CL_DRAW, CL_HOLSTER,
	CL_JUMP, CL_FALL, CL_LAND, CL_BOLT, CL_RL_START, CL_RL_LOOP, CL_RL_END, CL_COOK, CL_SLIDE, CL_SLIDE_FIRE,
	CL_FIRE2, CL_CQC_READY, CL_CQC, CL_CLIMB, CL_VAULT, CL_CQCB_READY, CL_CQCB, CL_N };   // knife: second slash, stealth kill (wpn.h); climbing onto a ledge, vaulting over cover;
	// b70: the cyborg's arm blade (vm_axe_cy.gvm): cqc_ready + cqc = the execution from the front, cqcb_ready + cqcb = from behind
static const char *CL_NAME[CL_N] = { "idle", "walk", "run", "sprint", "fire", "fire_ads", "ads_idle", "reload", "reload_empty", "draw", "holster",
	"jump", "fall", "land", "bolt", "reload_start", "reload_loop", "reload_end", "cook", "slide", "slide_fire", "fire2", "cqc_ready", "cqc", "climb", "vault", "cqcb_ready", "cqcb" };
enum { WK_AUTO, WK_SEMI, WK_BOLT, WK_PUMP, WK_KNIFE, WK_GRENADE, WK_BURST };
#ifndef WICON_MELEE
#define WICON_MELEE(i) ((i) == 51 || (i) == 80)   // b98: a hit's icon says melee: the MEGATECH KNIFE (51) or the DOUBLE KATAR (80)
#endif   // (b92: WK_BURST - three shots a pull, the 93-SE)
typedef struct {
	char sh[12], name[24], cls[8]; int icon;              // short name (file vm_<sh>.gvm), HUD name, class, kill-feed icon (wp.N)
	char s_fire[40], s_fire3[40], s_rl[40], s_eq[32];     // sound prefixes: 1st-person fire, 3rd-person fire, reload, equip
	float rpm, dmg, dmg_h; int mag, res;                  // rounds per minute, damage body / head, magazine, reserve
	float rng_m;                                          // b90: the v2.0 panel's RANGE (m, weapons.txt column 30): full damage to it, then less (wdef_falloff)
	float rl, rl_e, spread, ads_fov, hip_vfov, hip_off[3], ads_vfov, ads_off[3];
	int kind, pellets; float recoil;                      // recoil: view kick per shot at the hip (rad)
	int sniper;                                           // b91: a scoped rifle (class sr): the scope view, x8 / x12 zoom, its own spread; kind = bolt or semi
} WDef;
#define WDEF_MAX 64   // (b89: was 16 - the 21-gun table lost its last five: M249, pistols, knife, grenade; tools/audit checks the table fits)
static WDef wdef[WDEF_MAX]; static int nwdef;
// b90 (the First Assault wiki, "Weapon statistics": a gun does its listed damage out to its RANGE, then drops off to a minimum
// of 70 %): full damage to the range, then down linearly to 70 % at twice the range (dist in cm)
static float wdef_falloff(const WDef *w, float dist) {
	if (!w || w->rng_m <= 0 || dist <= w->rng_m * 100) return 1;
	float k = 1 - 0.3f * (dist - w->rng_m * 100) / (w->rng_m * 100); return k < 0.7f ? 0.7f : k;
}
static int wdef_find(const char *sh) { for (int i = 0; i < nwdef; i++) if (!strcmp(wdef[i].sh, sh)) return i; return -1; }

static int wdef_parse(WDef *w, char *line) {
	char *f[32]; int n = 0; char *s = line;
	while (n < 32) { f[n++] = s; char *b = strchr(s, '|'); if (!b) break; *b = 0; s = b + 1; }
	if (n < 23) return -1;
	memset(w, 0, sizeof(*w));
	snprintf(w->sh, sizeof(w->sh), "%s", f[0]); snprintf(w->name, sizeof(w->name), "%s", f[1]); snprintf(w->cls, sizeof(w->cls), "%s", f[2]); w->icon = atoi(f[3]);
	snprintf(w->s_fire, sizeof(w->s_fire), "%s", f[4]); snprintf(w->s_fire3, sizeof(w->s_fire3), "%s", f[5]);
	snprintf(w->s_rl, sizeof(w->s_rl), "%s", strcmp(f[6], "-") ? f[6] : ""); snprintf(w->s_eq, sizeof(w->s_eq), "%s", f[7]);
	w->rpm = atof(f[8]); w->dmg = atof(f[9]); w->dmg_h = atof(f[10]); w->mag = atoi(f[11]); w->res = atoi(f[12]);
	w->rl = atof(f[13]); w->rl_e = atof(f[14]); w->spread = atof(f[15]); w->ads_fov = atof(f[16]); w->hip_vfov = atof(f[17]);
	sscanf(f[18], "%f,%f,%f", &w->hip_off[0], &w->hip_off[1], &w->hip_off[2]); w->ads_vfov = atof(f[19]);
	if (sscanf(f[20], "%f,%f,%f", &w->ads_off[0], &w->ads_off[1], &w->ads_off[2]) != 3) memcpy(w->ads_off, w->hip_off, 12);
	w->pellets = 1;
	if (!strcmp(w->cls, "hg")) w->kind = WK_SEMI;
	else if (!strcmp(w->cls, "sr")) { w->kind = WK_BOLT; w->sniper = 1; }
	else if (!strcmp(w->cls, "sg")) { w->kind = WK_PUMP; w->pellets = 8; }
	else if (!strcmp(w->cls, "knife")) { w->kind = WK_KNIFE; w->dmg = w->dmg_h = 100; }   // the table's 55 is a class guess; the game's knife is a one-hit kill
	else if (!strcmp(w->cls, "grenade")) w->kind = WK_GRENADE;
	else w->kind = WK_AUTO;
	if (w->hip_vfov <= 0) w->hip_vfov = 49.75f;
	if (w->ads_vfov <= 0) w->ads_vfov = w->hip_vfov;
	w->recoil = n > 25 ? (float)atof(f[25]) : 0;
	w->rng_m = n > 30 ? (float)atof(f[30]) : 0;
	if (n > 31 && !strcmp(f[31], "semi")) w->kind = WK_SEMI;
	if (n > 31 && !strcmp(f[31], "burst")) w->kind = WK_BURST;   // b92: the 93-SE (burst-fire pistol)   // b91: column 31 = the action when it is not the class's (the semi-auto sniper rifles: SA50, SEO90, WS2088)
	if (w->recoil <= 0) w->recoil = 0.0065f * sqrtf(w->dmg * w->pellets / 24.f);   // (older table: the old damage-based kick)
	return 0;
}
// weapons.txt (the M4A1 row is built in as well, for an older table without it)
static int weapons_load_table(const char *path) {
	static char m4[] = "m4a1|M4A1|ar|2|wp_m4a1_fire_fp_outdoor_|wp_m4a1_fire_tp_distance_|wp_ar_m4a1_reload_|wp_ar_equip_on_1|690|35|80|30|90|2.0|2.77|0.006|51.4|49.75|-2.37,0.08,0|11.5|0.00,-0.77,-15.6|1|1|-|-|0.0064";
	nwdef = 0;
	FILE *f = fopen(path, "rb");
	char line[512];
	while (f && nwdef < WDEF_MAX && fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] < ' ') continue;
		char *e = line + strlen(line); while (e > line && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
		WDef w; if (wdef_parse(&w, line) == 0 && wdef_find(w.sh) < 0) wdef[nwdef++] = w;
	}
	if (f) fclose(f);
	if (wdef_find("m4a1") < 0 && nwdef < WDEF_MAX) { char b[256]; strcpy(b, m4); if (!wdef_parse(&wdef[nwdef], b)) nwdef++; }   // (older weapons.txt without the M4A1 row)
	return nwdef;
}

// ---- loadout slots
enum { WS_PRIMARY, WS_SECONDARY, WS_KNIFE, WS_GRENADE, WS_N };
typedef struct {
	int def, ok; Model m; Anim a; int c[CL_N]; int grip; const float *mz, *hud1, *scope, *tip, *tbase;   // tip / tbase: the knife blade (b63 slash trail)
	int ammo, res; float len[CL_N];
	int cy;   // b63: the cyborg's variant (vm_<short>_cy.gvm, Ghost Assault); b70: 2 = the arm blade (vm_axe_cy.gvm) in the knife's slot
} WSlot;
static int wvm_cyborg;   // b63 (Ghost Assault, ga.h): the knife is loaded in the cyborg's arms (vm_knife_cy.gvm) when the data has it
static WSlot wsl[WS_N];
static void wslot_free(int s) { WSlot *w = &wsl[s]; if (w->ok) { anim_free(&w->a); model_free(&w->m); } memset(w, 0, sizeof(*w)); w->def = -1; }
// returns 0 or the model_load error; the slot keeps its ammo only when keep_ammo is set and the weapon is the same
static void wslot_free_ptr(WSlot *w) { if (w->ok) { anim_free(&w->a); model_free(&w->m); } memset(w, 0, sizeof(*w)); w->def = -1; }
#ifdef WCACHE_N
// perf (b46, marked edit in the weapons agent's file): the last viewmodels put down stay loaded (up to WCACHE_N, defined
// by main.c), so a loadout that comes back to one of them (the next match, the tour's next map) takes no load (a viewmodel
// is 2-5.5 MB, mostly textures: 1.1-1.5 s a slot on the b41 device, 20-70 ms on Vita3K at b45). Only while vitaGL keeps room:
// a weapon is kept only when VRAM has WCACHE_VRAM_MIN free after it (vitaGL puts textures that no longer fit in VRAM into
// main RAM, slower for the GPU), and main.c drops the kept ones before a map loads with under 30 MB of VRAM free.
#ifndef WCACHE_VRAM_MIN
#define WCACHE_VRAM_MIN (32u << 20)
#endif
static WSlot wcache[WCACHE_N]; static unsigned wcache_age[WCACHE_N], wcache_clock; static int wcache_hit, wcache_kept, wcache_dropped;
static void wcache_drop(int k) { WSlot *c = &wcache[k]; if (c->ok) { anim_free(&c->a); model_free(&c->m); wcache_dropped++; } memset(c, 0, sizeof(*c)); c->def = -1; }
static int wcache_park(WSlot *w) {   // 1: w's weapon is kept (w is left empty)
	if (!w->ok || vglMemFree(VGL_MEM_VRAM) < WCACHE_VRAM_MIN) return 0;
	int k = 0; for (int i = 1; i < WCACHE_N; i++) if (!wcache[i].ok ? wcache[k].ok : (wcache[k].ok && wcache_age[i] < wcache_age[k])) k = i;   // an empty entry, else the oldest
	wcache_drop(k); wcache[k] = *w; wcache[k].a.m = &wcache[k].m; wcache_age[k] = ++wcache_clock; wcache_kept++;
	memset(w, 0, sizeof(*w)); w->def = -1; return 1;
}
static int wcache_take(WSlot *w, int def) {   // 1: w now holds def as a fresh load leaves it
	for (int k = 0; k < WCACHE_N; k++) if (wcache[k].ok && wcache[k].def == def) {
		*w = wcache[k]; memset(&wcache[k], 0, sizeof(wcache[k])); wcache[k].def = -1;
		anim_free(&w->a); anim_init(&w->a, &w->m); anim_set_base(&w->a, w->c[CL_IDLE], 0, 0);   // (pointers into the moved model: as wpn.h's wslot_fix)
		w->mz = model_attach(&w->m, "mz_grip"); w->hud1 = model_attach(&w->m, "hud1"); w->scope = model_attach(&w->m, "scope"); w->tip = model_attach(&w->m, "blade_tip"); w->tbase = model_attach(&w->m, "blade_base");
		w->ammo = wdef[def].mag; w->res = wdef[def].res; wcache_hit = 1; return 1;
	}
	return 0;
}
static void wcache_trim(unsigned vram_min) { for (int k = 0; k < WCACHE_N; k++) if (wcache[k].ok && vglMemFree(VGL_MEM_VRAM) < vram_min) wcache_drop(k); }
#endif
// b55 (user: the SEBURO M5's slide came off the gun and turned right when moving in ADS): repairs of the converted
// viewmodel clips, done on every gun at load (tools/weapons/rigid_host.c lists what it finds). The gun's own parts
// (bones that are not the arms' Bip01 / the Grip) are compared, per clip, with where the idle clip's first frame holds
// them on the Grip:
//  1. a part whose track is that of ANOTHER part (its first frame sits where the idle holds the other one, not itself)
//     gives it to that part (the M5's reload: the magazine's drop was on the trigger, which fell 30 cm while the magazine
//     stayed in);
//  2. a part held still at a wrong angle or place for the whole clip (a missing track filled with another rest pose:
//     the M5's slide 32 degrees off in the aimed walk, its magazine 29 degrees off in the aimed shot and the reload) is
//     put back where the idle holds it, following the Grip.
// Parts that really move (slides, belts, cylinders, magazines in a reload) are left as they are.
static void (*wvm_log)(const char *fmt, ...);   // main.c: logp (host tests: none)
static void wq_mul(const float *a, const float *b, float *o) {
	float r[4] = { a[3]*b[0] + a[0]*b[3] + a[1]*b[2] - a[2]*b[1], a[3]*b[1] - a[0]*b[2] + a[1]*b[3] + a[2]*b[0],
	               a[3]*b[2] + a[0]*b[1] - a[1]*b[0] + a[2]*b[3], a[3]*b[3] - a[0]*b[0] - a[1]*b[1] - a[2]*b[2] }; memcpy(o, r, 16); }
static void wq_rel(const float *G, const float *B, float *o, float *q) {   // B in G's frame (7 floats each: q xyzw, p xyz)
	float iq[4] = { -G[0], -G[1], -G[2], G[3] }, d[3] = { B[4] - G[4], B[5] - G[5], B[6] - G[6] }; quat_rotate(iq, d, o); wq_mul(iq, B, q); }
static float wq_ang(const float *a, const float *b) { float d = fabsf(a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3]); return d >= 1 ? 0 : 2 * acosf(d) * 57.29578f; }
static float wq_dist(const float *a, const float *b) { float x = a[0]-b[0], y = a[1]-b[1], z = a[2]-b[2]; return sqrtf(x*x + y*y + z*z); }
static int wvm_part(const Model *M, int b, int g) { const char *n = M->bone_name[b]; return b != g && !strstr(n, "Bip01") && !strstr(n, "amera"); }
static void wvm_fix_tracks(Model *M, int g, int idle, const char *who) {
#ifdef WVM_NOFIX
	return;   // (host tests: the clips as converted)
#endif
	if (g < 0 || idle < 0 || M->nb > 128 || M->clip[idle].nf < 1) return;
	enum { NBX = 128 }; static float ro[NBX][3], rq[NBX][4]; static int own[NBX];
	const float *I0 = M->clip[idle].data;
	for (int b = 0; b < M->nb; b++) wq_rel(I0 + g * 7, I0 + b * 7, ro[b], rq[b]);
	int fixes = 0;
	for (int c = 0; c < M->nclip; c++) { MClip *C = &M->clip[c]; if (C->nf < 1 || c == idle) continue; int nb = M->nb;
		for (int b = 0; b < nb; b++) own[b] = b;
		for (int b = 0; b < nb; b++) { if (!wvm_part(M, b, g)) continue; float o[3], q[4]; wq_rel(C->data + g * 7, C->data + b * 7, o, q);
			if (wq_dist(o, ro[b]) < 1.0f) continue;
			for (int k = 0; k < nb; k++) if (k != b && wvm_part(M, k, g) && wq_dist(o, ro[k]) < 0.3f && wq_dist(ro[k], ro[b]) > 1.0f) { own[b] = k; break; } }
		// 1: tracks given to the part they belong to (a part whose own track went elsewhere and got none back: rule 2 / idle)
		int moved = 0; for (int b = 0; b < nb; b++) if (own[b] != b) moved = 1;
		if (moved) { float *tmp = (float *)malloc(sizeof(float) * 7 * nb); if (!tmp) continue;
			int got[NBX]; for (int b = 0; b < nb; b++) got[b] = -1; for (int b = 0; b < nb; b++) if (own[b] != b) got[own[b]] = b;
			for (int f = 0; f < C->nf; f++) { float *F = C->data + (size_t)f * nb * 7; memcpy(tmp, F, sizeof(float) * 7 * nb);
				for (int b = 0; b < nb; b++) { if (got[b] >= 0) memcpy(F + b * 7, tmp + got[b] * 7, 28);
					else if (own[b] != b) { float *G = tmp + g * 7, *B = F + b * 7, v[3]; wq_mul(G, rq[b], B); quat_rotate(G, ro[b], v); for (int k = 0; k < 3; k++) B[4 + k] = G[4 + k] + v[k]; } } }
			free(tmp); for (int b = 0; b < nb; b++) if (got[b] >= 0) { fixes++; if (who && wvm_log) wvm_log("weapon %s: clip %s: %s takes the track of %s\n", who, C->name, M->bone_name[b], M->bone_name[got[b]]); } }
		// 2: a part held still off its idle place for the whole clip -> its idle place on the Grip
		for (int b = 0; b < nb; b++) { if (!wvm_part(M, b, g)) continue; float o0[3], q0[4], md = 0, ma = 0, off; wq_rel(C->data + g * 7, C->data + b * 7, o0, q0);
			for (int f = 1; f < C->nf; f++) { float o[3], q[4]; const float *F = C->data + (size_t)f * nb * 7; wq_rel(F + g * 7, F + b * 7, o, q); md = fmaxf(md, wq_dist(o, o0)); ma = fmaxf(ma, wq_ang(q, q0)); }
			off = wq_dist(o0, ro[b]); float oa = wq_ang(q0, rq[b]);
			if (C->nf >= 4 && md < 0.05f && ma < 0.5f && (off > 0.3f || oa > 2.0f)) {
				for (int f = 0; f < C->nf; f++) { float *F = C->data + (size_t)f * nb * 7, *G = F + g * 7, *B = F + b * 7, v[3]; wq_mul(G, rq[b], B); quat_rotate(G, ro[b], v); for (int k = 0; k < 3; k++) B[4 + k] = G[4 + k] + v[k]; }
				fixes++; if (who && wvm_log) wvm_log("weapon %s: clip %s: %s held %.1f cm / %.0f deg off the gun: put back\n", who, C->name, M->bone_name[b], off, oa); } }
	}
	(void)fixes;
}
static int wvm_fp_off;   // b60: test switch (ux0:data/gitsfa/nofp): the light first-person meshes are not loaded
static int wslot_load_ptr(WSlot *w, int def, const char *dir) {
	if (def < 0 || def >= nwdef) return -10;
	int cy = wvm_cyborg && wdef[def].kind == WK_KNIFE;
#ifdef WCACHE_N   // perf (b46): the weapon put down is kept, the one wanted may be kept already
	wcache_hit = 0;
	if (!(w->ok && w->def != def && !w->cy && wcache_park(w))) wslot_free_ptr(w);   // (b63: the cyborg's variant is not kept: the cache knows weapons by def)
	if (!cy && wcache_take(w, def)) return 0;
#else
	wslot_free_ptr(w);
#endif
	char p[160]; int r = -1;
	if (cy) {
		// b107 (user, Ghost Storage's Ghost Assault round: "what are the pale parts around the hands? they dont usually seem to
		// be there"; the port's view beside the footage): the PC's cyborg holds THE MEGATECH KNIFE'S BLADE, with the knife's own
		// first-person set - both hands open at the lower corners, the blade's two points straight ahead of the right forearm
		// (IGN "9 Minutes of Ghost Assault" 0:13-0:22, Nick Bunyun 0:01-0:10: frame for frame the pose of vm_knife_cy.gvm's
		// idle). b70 had read that two-pointed blade as "a double blade" and given the cyborg the Double Katar (vm_axe_cy.gvm:
		// fists, the katar's broad curved side to the camera, the forearm plates filling the corner). The knife in the cyborg's
		// arms first now, whatever melee weapon the player's loadout has; the katar's only when the data has no other.
		snprintf(p, sizeof(p), "%svm_knife_cy.gvm", dir); r = model_load(&w->m, p);
		if (r < 0) { memset(&w->m, 0, sizeof(w->m)); snprintf(p, sizeof(p), "%svm_axe_cy.gvm", dir); r = model_load(&w->m, p); if (r >= 0) cy = 2; }
		if (r < 0) { memset(&w->m, 0, sizeof(w->m)); cy = 0; } }   // (missing: the operator's arms)
	if (!cy) { snprintf(p, sizeof(p), "%svm_%s.gvm", dir, wdef[def].sh); r = model_load(&w->m, p); }
	if (r < 0) { memset(&w->m, 0, sizeof(w->m)); w->def = -1; return r; }
	if (!wvm_fp_off && !cy) { snprintf(p, sizeof(p), "%svm_%s.gfp", dir, wdef[def].sh); model_fp_load(&w->m, p); }   // b60: its light first-person meshes, if the data has them (b63: not the cyborg's)
	w->cy = cy;
	anim_init(&w->a, &w->m); w->def = def; w->ok = 1;
	for (int k = 0; k < CL_N; k++) { w->c[k] = model_find_clip(&w->m, CL_NAME[k]); w->len[k] = model_clip_len(&w->m, w->c[k]); }
	if (w->c[CL_HOLSTER] < 0) { w->c[CL_HOLSTER] = model_find_clip(&w->m, "swap_start"); w->len[CL_HOLSTER] = model_clip_len(&w->m, w->c[CL_HOLSTER]); }
	if (w->c[CL_DRAW] < 0) { w->c[CL_DRAW] = model_find_clip(&w->m, "swap_end"); w->len[CL_DRAW] = model_clip_len(&w->m, w->c[CL_DRAW]); }
	w->grip = model_find_bone(&w->m, "Grip"); w->mz = model_attach(&w->m, "mz_grip"); w->hud1 = model_attach(&w->m, "hud1"); w->scope = model_attach(&w->m, "scope"); w->tip = model_attach(&w->m, "blade_tip"); w->tbase = model_attach(&w->m, "blade_base");
	if (wdef[def].kind != WK_KNIFE && wdef[def].kind != WK_GRENADE) wvm_fix_tracks(&w->m, w->grip, w->c[CL_IDLE], wdef[def].sh);   // b55: parts off the gun in some clips (SEBURO M5, M4A1); guns only
	w->ammo = wdef[def].mag; w->res = wdef[def].res;
	anim_set_base(&w->a, w->c[CL_IDLE], 0, 0);
	return 0;
}
static int wslot_load(int s, int def, const char *dir) { return wslot_load_ptr(&wsl[s], def, dir); }
static void wslot_refill(int s) { WSlot *w = &wsl[s]; if (w->ok) { w->ammo = wdef[w->def].mag; w->res = wdef[w->def].res; } }

// the reload sounds of a weapon: every sound whose name starts with the prefix, in name order (_01, _02, ...)
static __attribute__((unused)) int weapon_reload_sounds(const char *prefix, const char **out, int max) {
	int pl = strlen(prefix), lo = 0, hi = nsnd, n = 0; if (!pl) return 0;
	while (lo < hi) { int m = (lo + hi) >> 1; if (strncmp(snds[m].name, prefix, pl) < 0) lo = m + 1; else hi = m; }
	for (int i = lo; i < nsnd && n < max && !strncmp(snds[i].name, prefix, pl); i++) out[n++] = snds[i].name;
	return n;
}

// ---- thrown grenades (world space, z up, cm): bounce on the collision mesh, explode when the fuse runs out
#define NGREN 8   // net: other players' grenades fly here too
typedef struct { int on; float p[3], v[3], fuse, spin, ang; int net_owner; } Grenade;   // net_owner: 0 ours, else 1 + the thrower's net actor (net_game.h)
static Grenade gren[NGREN];
