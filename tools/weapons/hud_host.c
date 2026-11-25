// Host test of the weapon HUD pieces (engine/src/wpn_hud.h) with the real hud.ghud and the recording GL stand-in of
// tools/menu/host: one capture per state, rasterised by tools/menu/raster.py (see hud_host.sh).
#include "vitaGL.h"
#include <math.h>
#define sceGxmTransferFinish() 0
#include "../../engine/src/hud_game.h"
#include "../../engine/src/wpn_hud.h"
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static void one(const char *out, const char *name, const WHud *h) {
	char p[256]; snprintf(p, sizeof(p), "%s/%s.cap", out, name); cap_begin(p);
	hud_begin(); wpn_hud_draw(h); hud_end(); cap_end();
	snprintf(p, sizeof(p), "%s/%s.tex", out, name); mkdir(p, 0755); cap_dump_textures(p);
}
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "/root/work/weapons/build/data/", *out = argc > 2 ? argv[2] : "/tmp/claude-0/wpnhud";
	char p[256]; snprintf(p, sizeof(p), "%shud.ghud", data);
	int r = hud_load(p, mk), g = r >= 0 ? hud_game_init() : -1; printf("hud_load %d hud_game_init %d screen %d white %d\n", r, g, HG.screen, HG.white);
	WHud h; memset(&h, 0, sizeof(h));
	h.pick = 1; h.pick_name = "L118A-S1b"; h.pick_ammo = 20; h.pick_icon = 21; h.pick_hold = 0.6f; one(out, "pick_l118", &h);
	h.pick_name = "M4A1"; h.pick_ammo = 90; h.pick_icon = 2; h.pick_hold = 0; one(out, "pick_m4a1", &h);
	memset(&h, 0, sizeof(h)); h.stealth = 1; h.knife_out = 1; one(out, "stealth_knife", &h);
	h.knife_out = 0; h.fail_t = 1; one(out, "stealth_quick_fail", &h);
	return 0;
}
