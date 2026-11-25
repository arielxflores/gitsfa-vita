// Vita compile/link test for engine/src/menu.h: the whole game flow with every menu entry point
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/ctrl.h>
#include "../../engine/src/menu.h"
static GLuint mk(GLenum t, const char *src) { GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, NULL); glCompileShader(s); return s; }
static unsigned pad_bits(unsigned b) {
	return (b & SCE_CTRL_UP ? MB_UP : 0) | (b & SCE_CTRL_DOWN ? MB_DOWN : 0) | (b & SCE_CTRL_LEFT ? MB_LEFT : 0) | (b & SCE_CTRL_RIGHT ? MB_RIGHT : 0) |
	       (b & SCE_CTRL_CROSS ? MB_OK : 0) | (b & SCE_CTRL_CIRCLE ? MB_BACK : 0) | (b & SCE_CTRL_START ? MB_START : 0) | (b & SCE_CTRL_LTRIGGER ? MB_L : 0) |
	       (b & SCE_CTRL_RTRIGGER ? MB_R : 0) | (b & SCE_CTRL_TRIANGLE ? MB_TRI : 0) | (b & SCE_CTRL_SQUARE ? MB_SQR : 0);
}
int main(void) {
	vglInit(0x800000); sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	if (menu_init("ux0:data/gitsfa/", mk) != 3) return 1;
	MenuConfig cfg; menu_config_default(&cfg); MenuResults res; memset(&res, 0, sizeof(res));
	enum { S_FE, S_LOAD, S_MATCH, S_RESULTS } st = S_FE; unsigned old = 0; float t = 0, rect[4], cam[5];
	menu_fe_open(FE_TITLE);
	for (int frame = 0; frame < 100000; frame++) {
		SceCtrlData pad; sceCtrlPeekBufferPositive(0, &pad, 1); unsigned held = pad_bits(pad.buttons), pr = held & ~old; old = held; float dt = 1 / 60.f; t += dt;
		menu_tick(dt); glClear(GL_COLOR_BUFFER_BIT);
		if (st == S_FE) {
			int r = menu_fe_update(&cfg, pr, held, dt);
			menu_fe_draw_bg(); int who = menu_fe_preview(&cfg, rect, cam); (void)who; menu_fe_draw(&cfg);
			if (r == FE_QUIT) break;
			if (r == FE_START) { menu_fe_unload(); menu_loading_reset(); st = S_LOAD; t = 0; }
		} else if (st == S_LOAD) {
			menu_loading_draw(MENU_MAP[cfg.map].stem, MENU_MAP[cfg.map].name, cfg.mode, t / 2, "Loading map data...", cfg.chr);
			if (t > 2) { st = S_MATCH; t = 0; }
		} else if (st == S_MATCH) {
			menu_splash_draw(cfg.mode, cfg.mode == MODE_DEM ? 1 : 0, MENU_MODE_GOAL[cfg.mode], t);
			menu_commander_draw(FACE_ARAMAKI, "Chief Aramaki", "Eliminate all enemy agents!", t - 1);
			MenuRespawn rsp = { "Bot", "You", 1, 0, 2, 3 - t, "Tip" }; if (t > 7 && t < 10) menu_respawn_draw(&rsp, t - 7);
			menu_timewarn_draw(t - 10); menu_notice_draw("START", t - 6);
			if (t > 14) menu_endsplash_draw(0, 50, 37, t - 14);
			if (t > 18) { menu_fe_load(); res.result = 0; res.score[0] = 50; res.score[1] = 37; res.nplayers = 1; res.p[0].me = 1; res.p[0].chr = cfg.chr; res.level = cfg.level;
				menu_results_open(&res); st = S_RESULTS; }
			if (pr & MB_START) menu_pause_open();
		} else {
			if (menu_results_update(pr, dt)) { menu_fe_open(FE_LOBBY); st = S_FE; }
			menu_results_draw_bg(); menu_results_preview(rect, cam); menu_results_draw();
		}
		if (st == S_MATCH && (held & MB_SQR)) { int a = menu_pause_update(pr, dt); menu_pause_draw(); if (a == PAUSE_QUIT) st = S_RESULTS; }
		vglSwapBuffers(GL_FALSE);
	}
	sceKernelExitProcess(0); return 0;
}
