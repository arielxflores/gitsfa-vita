// Host test: runs menu.h screens with the recording GL stand-in and writes one capture per screen (+ its touch targets)
#include "vitaGL.h"
#include <sys/stat.h>
static void mkdir_p(const char *p) { mkdir(p, 0755); }
#include <math.h>
#define sceGxmTransferFinish() 0
#include "../../../engine/src/menu.h"
#include <stdarg.h>
static void logp(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }   // (main.c on the Vita; b49: menu_ui.h logs labels that do not fit)
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static const char *OUT;
static char cur[64]; static int shot_draws;
static int shot_skip;   // ui b50: env MENU_ONLY=a,b,c writes only these captures (each one dumps every texture: ~16 MB)
static int shot_wanted(const char *name) { const char *l = getenv("MENU_ONLY"); if (!l || !*l) return 1; size_t n = strlen(name);
	for (const char *q = l; (q = strstr(q, name)); q++) if ((q == l || q[-1] == ',') && (q[n] == ',' || !q[n])) return 1; return 0; }
static void shot(const char *name) { char p[256]; snprintf(cur, sizeof(cur), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); shot_skip = !shot_wanted(name); if (!shot_skip) cap_begin(p); hud_claims_frame(); 
#ifdef MU_AUDIT
	mu_aud_name = cur; hud_aud_reset();   // b95: the outline audit (hud.h) runs for every screen, written or not
#endif
	hud_claims_frame();   /* ui b42: no hud.h claims (HUD de-clutter) carried over from the shot before */ shot_draws = mu_draws; }
static void done(void) {
#ifdef MU_AUDIT
	hud_aud_flush(); mu_aud_name = "";
#endif
	char p[256]; cap_end(); if (shot_skip) return; snprintf(p, sizeof(p), "%s/%s.tex", OUT, cur); mkdir_p(p); cap_dump_textures(p);
	snprintf(p, sizeof(p), "%s/%s.hits", OUT, cur); FILE *f = fopen(p, "w"); if (f) { if (mu_draws != shot_draws) mu_dump_hits(f); fclose(f); }   // only targets drawn by this capture
}
static MenuConfig cfg;
// one front-end frame as main.c runs it: touch poll, update, backdrop, 3D preview slot, screen
static int fe_frame(unsigned pressed, float dt) {
	float R[4], C[5]; menu_touch_poll(dt); int r = menu_fe_update(&cfg, pressed, pressed, dt);
	menu_fe_draw_bg(); menu_fe_preview(&cfg, R, C); if (!getenv("MENU_BGONLY")) menu_fe_draw(&cfg); return r;   // (ui b50: MENU_BGONLY=1: the backdrop alone, for tools/chars/host/menufig.sh)
}
static void fe_shot(const char *nm) { shot(nm); fe_frame(0, 0.5f); done(); }
static void tap(float x, float y) { menu_touch_inject(1, 1, x, y); fe_frame(0, 0.016f); menu_touch_inject(1, 0, x, y); fe_frame(0, 0.016f); menu_touch_inject(0, 0, 0, 0); fe_frame(0, 0.016f); }
static void tapid(int id) { const MuHit *h = mu_hit_drawn(id); if (!h) { printf("tapid: no target %d (kind %d)\n", id, id >> 8); return; } tap((h->x0 + h->x1) * 0.5f, (h->y0 + h->y1) * 0.5f); }
static void swipe(float x, float y0, float y1) { for (int k = 0; k <= 6; k++) { menu_touch_inject(1, 1, x, y0 + (y1 - y0) * k / 6); fe_frame(0, 0.016f); } menu_touch_inject(1, 0, x, y1); fe_frame(0, 0.016f); menu_touch_inject(0, 0, 0, 0); fe_frame(0, 0.016f); }
#define menu_results_draw() (getenv("MENU_BGONLY") ? (void)0 : menu_results_draw())   // (ui b50: as fe_frame)
static int t_skin_n8(int op) { (void)op; return 8; }   // b103: stand-ins for main.c's skin hooks (the hint line at its longest)
static const char *t_skin_name8(int op, int k) { (void)op; static const char *N[8] = { "STANDARD", "PHOENIX", "ARCTIC", "DESERT", "URBAN", "NIGHT", "ELITE", "CLASSIC" }; return N[k & 7]; }
static int t_skin_gog1(int op, int k) { (void)op; (void)k; return 1; }
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "/root/gits/build/data/"; OUT = argc > 2 ? argv[2] : "/tmp/claude-0/menu-ui/cap";
	int r = menu_init(data, mk); printf("menu_init %d ovl %d fe %d\n", r, menu_ovl, menu_fe);
	menu_time = 3.7f;
	shot("loading_port"); menu_loading_draw("port", MENU_MAP[5].name, MODE_TDM, 0.62f, "Loading map data...", 1); done();
	shot("loading_ttm"); menu_loading_reset(); menu_loading_draw("underground", MENU_MAP[10].name, MODE_TTM, 0.3f, "Loading map data...", 11); done();
	shot("loading_em"); menu_loading_reset(); menu_loading_draw("urban", MENU_MAP[11].name, MODE_EM, 0.8f, "Loading textures...", 0); done();
	if (getenv("LD_ALL")) for (int m = 0; m < MENU_NMAPS; m++) { char nm[48]; snprintf(nm, sizeof(nm), "ldall_%02d_%s", m, MENU_MAP[m].stem); shot(nm); menu_loading_reset();   // b59: every map's loading art (faces vs the bands)
		menu_loading_draw(MENU_MAP[m].stem, MENU_MAP[m].name, MODE_DEM, 0.5f, "Loading map data...", m % 12); done(); }
	menu_ld_tut = 2; shot("loading_tut"); menu_loading_reset(); menu_loading_draw("tutorial3", MENU_MAP[16].name, MODE_TDM, 0.4f, "Loading map data...", 1); done(); menu_ld_tut = 0;
#include "tests_fe.inc"
#include "tests_net.inc"   // net: the AD-HOC screens
#include "tests_more.inc"
	return 0;
}
