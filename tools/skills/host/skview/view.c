// skills.h viewer: renders the skills' sprites (rings, firing arcs, beams, pillar, markers) and the screen overlays with the
// software rasterizer of /root/fx/host, to check placement / orientation / blending by eye. Models are not drawn (indexed
// draws are stubbed). Output: out/world_*.ppm, out/ovl_*.ppm (run.sh turns them into PNG sheets).
#include <stdio.h>
#include "vitaGL.h"
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"
#include "skills.h"

static void m4mul(float *o, const float *a, const float *b) { float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; } memcpy(o, r, 64); }
static float VPm[16], CAM[3], RR[3], UU[3];
static void setcam(float x, float y, float z, float yaw, float pitch, float fov) {
	float P[16], Vw[16], F[3], cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw), t = 1 / tanf(fov * 3.14159f / 360), n = 10, f = 12000;
	memset(P, 0, 64); P[0] = t / (960.f / 544); P[5] = t; P[10] = (f + n) / (n - f); P[11] = -1; P[14] = 2 * f * n / (n - f);
	F[0] = cy * cp; F[1] = sy * cp; F[2] = sp; RR[0] = sy; RR[1] = -cy; RR[2] = 0; UU[0] = RR[1] * F[2] - RR[2] * F[1]; UU[1] = RR[2] * F[0] - RR[0] * F[2]; UU[2] = RR[0] * F[1] - RR[1] * F[0];
	CAM[0] = x; CAM[1] = y; CAM[2] = z;
	float v[16] = { RR[0], UU[0], -F[0], 0, RR[1], UU[1], -F[1], 0, RR[2], UU[2], -F[2], 0, -(RR[0] * x + RR[1] * y + RR[2] * z), -(UU[0] * x + UU[1] * y + UU[2] * z), (F[0] * x + F[1] * y + F[2] * z), 1 };
	memcpy(Vw, v, 64); m4mul(VPm, P, Vw);
}
static SwTex bgt[2];
static void mkbg(void) { for (int t = 0; t < 2; t++) { bgt[t].w = bgt[t].h = 128; bgt[t].px = malloc(128 * 128 * 4);
	for (int i = 0; i < 128 * 128; i++) { int x = i % 128, y = i / 128; float g = (t ? 0.36f : 0.5f) * ((x % 32 < 1 || y % 32 < 1) ? 0.7f : 1) * (0.95f + 0.05f * sinf(x * 0.7f + y * 0.3f));
		bgt[t].px[i * 4] = (unsigned char)(g * 255); bgt[t].px[i * 4 + 1] = (unsigned char)(g * 250); bgt[t].px[i * 4 + 2] = (unsigned char)(g * 240); bgt[t].px[i * 4 + 3] = 255; } } }
static void quad3(const float p[4][3], const SwTex *T) {
	float c[4][4], a[4][6], uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for (int i = 0; i < 4; i++) { for (int r = 0; r < 4; r++) c[i][r] = VPm[r] * p[i][0] + VPm[4 + r] * p[i][1] + VPm[8 + r] * p[i][2] + VPm[12 + r]; a[i][0] = uv[i][0]; a[i][1] = uv[i][1]; a[i][2] = a[i][3] = a[i][4] = a[i][5] = 1; }
	int tri[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } }; for (int t = 0; t < 2; t++) { float c1[3][4], a1[3][6]; for (int k = 0; k < 3; k++) { memcpy(c1[k], c[tri[t][k]], 16); memcpy(a1[k], a[tri[t][k]], 24); } sw_tri(c1, a1, T); }
}
static void bg(void) {   // floor (z 0) in 2 m tiles and the test wall (x 2000..2100)
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) { sw_fb[y][x][0] = 0.45f; sw_fb[y][x][1] = 0.52f; sw_fb[y][x][2] = 0.6f; sw_zb[y][x] = 1; }
	sw_blend = 0; sw_dmask = 1; sw_depth = 1;
	for (int i = -10; i < 12; i++) for (int j = -10; j < 10; j++) { float p[4][3] = { { i * 200.f, j * 200.f, 0 }, { i * 200.f + 200, j * 200.f, 0 }, { i * 200.f + 200, j * 200.f + 200, 0 }, { i * 200.f, j * 200.f + 200, 0 } }; quad3(p, &bgt[1]); }
	for (int j = -5; j < 5; j++) for (int k = 0; k < 2; k++) { float p[4][3] = { { 2000, j * 200.f, k * 200.f }, { 2000, j * 200.f + 200, k * 200.f }, { 2000, j * 200.f + 200, k * 200.f + 200 }, { 2000, j * 200.f, k * 200.f + 200 } }; quad3(p, &bgt[0]); }
}
static void save(const char *name) {
	char p[256]; snprintf(p, 256, "out/%s.ppm", name); FILE *f = fopen(p, "wb"); fprintf(f, "P6 %d %d 255\n", SW_W, SW_H);
	for (int y = 0; y < SW_H; y++) for (int x = 0; x < SW_W; x++) for (int k = 0; k < 3; k++) fputc((int)(sw_fb[y][x][k] * 255 + 0.5f), f); fclose(f);
}
// ---- a world with actors (same callbacks as the test, simplified)
typedef struct { V3 pos, vel; float yaw, pitch, hp; int alive, team; } TA; static TA ta[SK_MAXACT];
static int w_n(void) { return SK_MAXACT; } static V3 w_pos(int a) { return ta[a].pos; } static V3 w_vel(int a) { return ta[a].vel; } static float w_yaw(int a) { return ta[a].yaw; }
static int w_alive(int a) { return ta[a].alive; } static V3 w_eye(int a) { return v3(ta[a].pos.x, ta[a].pos.y, ta[a].pos.z + P_EYE); }
static V3 w_dir(int a) { return v3(cosf(ta[a].yaw) * cosf(ta[a].pitch), sinf(ta[a].yaw) * cosf(ta[a].pitch), sinf(ta[a].pitch)); }
static float w_hp(int a) { return ta[a].hp; }
static int w_los(V3 a, V3 b) { V3 d = vsub(b, a); float l = sqrtf(vdot(d, d)); if (l < 1) return 1; float h = raycast(a, vmul(d, 1 / l), l, NULL, NULL); return h < 0 || h > l - 25; }
static int w_damage(int v, float dmg, int att, int kind) { (void)v; (void)dmg; (void)att; (void)kind; return 0; }
static const SkWorld W = { .n_actors = w_n, .pos = w_pos, .vel = w_vel, .yaw = w_yaw, .alive = w_alive, .eye = w_eye, .aim_dir = w_dir, .hp = w_hp, .los = w_los, .damage = w_damage };
static void make_col(const char *path) {
	float V[][3] = { { -10000, -10000, 0 }, { 10000, -10000, 0 }, { 10000, 10000, 0 }, { -10000, 10000, 0 }, { 2000, -1000, 0 }, { 2000, 1000, 0 }, { 2000, 1000, 400 }, { 2000, -1000, 400 } };
	unsigned I[][3] = { { 0, 1, 2 }, { 0, 2, 3 }, { 4, 5, 6 }, { 4, 6, 7 } }; unsigned nv = 8, nt = 4, ver = 3; int ns = 0;
	FILE *f = fopen(path, "wb"); fwrite("GCOL", 4, 1, f); fwrite(&ver, 4, 1, f); fwrite(&nv, 4, 1, f); fwrite(&nt, 4, 1, f); fwrite(V, 12, nv, f); fwrite(I, 12, nt, f); fwrite(&ns, 4, 1, f);
	for (unsigned i = 0; i < nt; i++) { unsigned char m = 8 | (TF_PLAYER | TF_BULLET) << 4; fwrite(&m, 1, 1, f); } fclose(f);
}
static GLuint mk(GLenum t, const char *s) { (void)s; return glCreateShader(t); }
static void step(float s) { for (int i = 0; i < (int)(s * 60); i++) skills_update(1 / 60.f); }

int main(void) {
	mkbg(); make_col("/tmp/skview.gcol"); load_col("/tmp/skview.gcol"); sprite_init(mk); fx_init("/root/gits/build/data/fx_sk.gfx");
	skills_init("/tmp/skview_nodata/", &W, 0);   /* no models (indexed draws are stubbed) and no GTX: the software GL decodes DXT5 only; the overlays come pre-decoded below */
	{ FILE *f = fopen("/tmp/sk_ovl.rgba", "rb"); unsigned wh[2]; if (f && fread(wh, 4, 2, f) == 2) { unsigned char *px = malloc(wh[0] * wh[1] * 4); if (fread(px, 4, wh[0] * wh[1], f) == wh[0] * wh[1]) { glGenTextures(1, &sk_ovl_tex); glBindTexture(GL_TEXTURE_2D, sk_ovl_tex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wh[0], wh[1], 0, GL_RGBA, GL_UNSIGNED_BYTE, px); } } if (f) fclose(f); }
	static const int K[SK_MAXACT] = { SK_SENTRYGUN, SK_SPECIALRECON, SK_HEATSCAN, SK_OPTICAL, SK_EMP, SK_DMDDRONE, SK_SENTRYGUN, SK_CYBERWALL, SK_ARMLAUNCHER };
	for (int a = 0; a < SK_MAXACT; a++) { ta[a].alive = 1; ta[a].hp = 100; ta[a].team = a < 3 ? 0 : 1; ta[a].pos = v3(-3000, -3000 + a * 100.f, 0); sk_actor_set(a, K[a], ta[a].team); }
	skills_reset_match(); sk_bots_use = 0;
	// ally turret (player 0) facing +x at (200, -300); enemy in its arc at (1100, -100); ally recon at (300, 500); enemy EMP at (900, 450); ally heat scan
	ta[0].pos = v3(-100, -300, 0); ta[0].yaw = 0; ta[0].pitch = -0.5f; sk_press(0); step(0.05f); sk_fire(0);
	ta[1].pos = v3(100, 500, 0); ta[1].yaw = 0; ta[1].pitch = -0.6f; sk_press(1); sk_bot_place_at(1, v3(300, 500, 0));
	ta[4].pos = v3(1100, 600, 0); ta[4].yaw = 3.14f; sk_press(4); sk_bot_place_at(4, v3(900, 450, 0));
	ta[3].pos = v3(1100, -100, 0); ta[5].pos = v3(1400, 250, 0); ta[5].vel = v3(0, 400, 0); ta[6].pos = v3(1700, -600, 0);
	step(2.4f); ta[2].pos = v3(-200, 100, 0); sk_press(2); step(0.9f);
	SKA[5].mark_t[0] = 3; SKA[6].mark_t[0] = 3;   // recon / wall marks (kal markers through walls)
	SKA[0].cool = 0; SKA[0].state = SKS_READY; ta[0].pos = v3(-400, -800, 0); sk_press(0);   // the player previews a second sentry: hologram ring + arc
	ta[0].pitch = -0.35f; step(0.1f);
	setcam(-900, -1100, 520, 0.62f, -0.28f, 70); bg(); sk_draw_world(VPm, CAM, RR, UU, (float[4]){ 0.5f, 0.55f, 0.6f, 1 }); save("world_a");
	setcam(500, -1500, 2200, 1.5708f, -1.05f, 70); bg(); sk_draw_world(VPm, CAM, RR, UU, (float[4]){ 0.5f, 0.55f, 0.6f, 1 }); save("world_top");
	// a T3 sentry's 360 arc, a drone aim pillar and a lasering drone
	skills_reset_match(); for (int a = 0; a < SK_MAXACT; a++) ta[a].pos = v3(-3000, -3000 + a * 100.f, 0);
	SKA[0].on = 0; sk_actor_set(0, SK_DMDDRONE, 0); ta[0].pos = v3(-300, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.25f; sk_press(0); step(0.1f);
	SKA[6].tier = 3; ta[6].pos = v3(600, -500, 0); ta[6].yaw = 3.14f; ta[6].pitch = -0.6f; sk_press(6); sk_bot_place_at(6, v3(500, -500, 0)); step(0.5f);
	SKA[5].on = 0; sk_actor_set(5, SK_DMDDRONE, 0); ta[5].pos = v3(-500, 600, 0); sk_press(5); sk_bot_place_at(5, v3(500, 500, 0)); ta[3].pos = v3(900, 400, 0); step(2.2f);
	setcam(-900, -900, 500, 0.62f, -0.25f, 70); bg(); sk_draw_world(VPm, CAM, RR, UU, (float[4]){ 0.5f, 0.55f, 0.6f, 1 }); save("world_b");
	// screen overlays, one state per image
	struct { const char *n; int what; } O[] = { { "camo", 0 }, { "heat", 1 }, { "meta", 2 }, { "shield", 3 }, { "rush", 4 }, { "dive", 5 }, { "emp", 6 }, { "wallpass", 7 }, { "hacked", 8 }, { "laser", 9 } };
	static const int OS[10] = { SK_OPTICAL, SK_HEATSCAN, SK_METABOOST, SK_PLASMASHIELD, SK_RUSH, SK_CYBERDIVE, SK_OPTICAL, SK_OPTICAL, SK_OPTICAL, SK_OPTICAL };
	for (unsigned k = 0; k < sizeof(O) / sizeof(O[0]); k++) {
		skills_reset_match(); SKA[0].on = 0; sk_actor_set(0, OS[k], 0); ta[0].pos = v3(-500, 0, 0); ta[0].yaw = 0; ta[0].pitch = -0.1f; memset(&SKV, 0, sizeof(SKV));
		switch (O[k].what) {
		case 0: sk_press(0); step(0.5f); break;
		case 1: sk_press(0); step(0.31f); break;
		case 2: sk_press(0); step(1.0f); break;
		case 3: sk_press(0); step(0.2f); SKV.shield_hit = 0.15f; break;
		case 4: sk_press(0); ta[0].vel = v3(1070, 0, 0); step(0.3f); break;
		case 5: sk_press(0); step(0.5f); break;
		case 6: SKV.glitch = SKV.glitch_len = 0.6f; SKV.glitch_c[0] = 0.4f; SKV.glitch_c[1] = 0.6f; SKV.glitch_c[2] = 1; break;
		case 7: SKV.hlines = 0.8f; break;
		case 8: SKV.laugh = 3; SKV.glitch = SKV.glitch_len = 0.25f; SKV.glitch_c[0] = 0.3f; SKV.glitch_c[1] = 1; SKV.glitch_c[2] = 0.5f; break;
		case 9: SKV.vig = SKV.vig_len = 0.3f; SKV.vig_c[0] = 0.9f; SKV.vig_c[1] = 0.1f; SKV.vig_c[2] = 0.05f; break;
		}
		setcam(-500, 0, P_EYE, 0, -0.1f, 70); bg(); sk_draw_overlays(0); char nm[32]; snprintf(nm, 32, "ovl_%s", O[k].n); save(nm);
		ta[0].vel = v3(0, 0, 0);
	}
	printf("rendered (indexed draws skipped: %ld)\n", sw_skipped_draws);
	return 0;
}
