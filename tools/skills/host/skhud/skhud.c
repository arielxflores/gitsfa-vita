// Host render of the skills' HUD prompts and notices (Vita glyphs, Legibility rule): the engine's hud_game.h + skills.h with
// the ui agent's recording GL stand-in (tools/ui/host/vitaGL.h); raster_hud.py turns each capture into a PNG.
// Build + run: tools/skills/host/skhud/run.sh
#include "vitaGL.h"
#include <sys/stat.h>
#include <math.h>
#define sceGxmTransferFinish() 0
#include "player.h"
#include "audio.h"
#include "sprite.h"
#include "fx.h"
#include "model.h"
#include "light.h"
#include "hud_game.h"
#include "skills.h"
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static const char *OUT; static char cur[64];
static void shot(const char *name) { char p[256]; snprintf(cur, sizeof(cur), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); cap_begin(p); }
static void done(void) { char p[256]; cap_end(); snprintf(p, sizeof(p), "%s/%s.tex", OUT, cur); mkdir(p, 0755); cap_dump_textures(p); }
static V3 w_pos(int a) { return v3(a * 300.f, 0, 0); } static int w_alive(int a) { (void)a; return 1; } static int w_n(void) { return 4; }
static const char *w_name(int a) { static const char *N[4] = { "Player", "ISHIKAWA", "SAITO", "E" }; return N[a & 3]; }
static void frame(const char *name, void (*setup)(void)) {
	shot(name); setup(); hud_use(0); hud_begin(); HudState H; memset(&H, 0, sizeof(H)); H.hp = 100; H.ap = 100; H.cool = -1; sk_hud_state(0, &H); hud_game_draw(&H, 3.3f);
	float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }; skills_hud(0, I, 3.3f); hud_end(); done();
}
static void s_launcher(void) { sk_actor_set(0, SK_ARMLAUNCHER, 0); skills_reset_match(); sk_use(0, 1); SKA[0].t = 2; SKA[0].nlocks = 1; SKA[0].bar = 0.6f; }
static void s_place(void) { sk_actor_set(0, SK_SENTRYGUN, 0); skills_reset_match(); sk_use(0, 1); SKA[0].aim_ok = 1; }
static void s_place_bad(void) { sk_actor_set(0, SK_SENTRYGUN, 0); skills_reset_match(); sk_use(0, 1); SKA[0].aim_ok = 0; }
static void s_dive(void) { sk_actor_set(0, SK_CYBERDIVE, 0); skills_reset_match(); sk_use(0, 1); SKA[0].bar = 0.7f; }
static void s_shield(void) { sk_actor_set(0, SK_PLASMASHIELD, 0); skills_reset_match(); sk_use(0, 1); SKA[0].bar = 0.8f; }
static void s_shield_off(void) { s_shield(); SKA[0].toggled = 0; }
static void s_t2(void) { sk_actor_set(0, SK_OPTICAL, 0); skills_reset_match(); SKV.t2n = 2.0f; SKA[0].intel = 400; }
static void s_sync(void) { sk_actor_set(0, SK_OPTICAL, 0); sk_actor_set(1, SK_HEATSCAN, 0); skills_reset_match(); SKA[1].intel = 400; sk_use(1, 2); }
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "build/data/"; OUT = argc > 2 ? argv[2] : "/tmp/skhud"; char p[256];
	snprintf(p, sizeof(p), "%shud.ghud", data); int hr = hud_load(p, mk); int gr = hud_game_init();
	static const SkWorld W = { .n_actors = w_n, .pos = w_pos, .alive = w_alive, .name = w_name };
	int sr = skills_init(data, &W, 0); printf("hud %d widgets %d skills %d\n", hr, gr, sr);
	frame("launcher", s_launcher); frame("place", s_place); frame("place_bad", s_place_bad); frame("dive", s_dive);
	frame("shield_on", s_shield); frame("shield_off", s_shield_off); frame("t2_ready", s_t2); frame("skillsync", s_sync);
	return 0;
}
