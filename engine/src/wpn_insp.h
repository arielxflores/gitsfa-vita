// b89 (user: "you can inspect it like you can in the videos in the menus"): the armory's INSPECT view - the selected gun
// on its own, large, turning slowly as the v2.0 armory shows it, turned with the right stick or a drag (main.c fig_input)
// and tilted with the right stick's up / down. The model is the gun's first-person model (vm_<sh>.gvm: the most detailed
// one) without its two arm meshes, on the first frame of its idle clip; wpn_inspect.txt (tools/menu/make_inspect.py)
// gives the middle and size of each gun in that pose, so it turns about its own middle and fills the same share of the view.
// Loaded 0.15 s after the choice settles (no loads while paging through the guns), freed when the view closes or a match
// starts (wpn_insp_free).
typedef struct { char sh[16]; float c[3], h[3]; } WInspInfo;
static WInspInfo winsp_info[96]; static int winsp_ninfo = -1;
static Model winsp_m; static char winsp_cur[16], winsp_want[16]; static float winsp_wait; static int winsp_ok; static MBone winsp_pose[96];
static void wpn_insp_free(void) { if (winsp_ok) model_free(&winsp_m); winsp_ok = 0; winsp_cur[0] = winsp_want[0] = 0; }
static const WInspInfo *wpn_insp_info(const char *sh) {
	if (winsp_ninfo < 0) {
		winsp_ninfo = 0; FILE *f = fopen(VDIR "wpn_inspect.txt", "rb"); char ln[160];
		if (f) { while (fgets(ln, sizeof(ln), f) && winsp_ninfo < 96) { WInspInfo *I = &winsp_info[winsp_ninfo]; if (ln[0] == '#') continue;
			if (sscanf(ln, "%15s %f %f %f %f %f %f", I->sh, &I->c[0], &I->c[1], &I->c[2], &I->h[0], &I->h[1], &I->h[2]) == 7) winsp_ninfo++; } fclose(f); }
		logp("inspect: %d guns in wpn_inspect.txt (b89)\n", winsp_ninfo);
	}
	for (int i = 0; i < winsp_ninfo; i++) if (!strcmp(winsp_info[i].sh, sh)) return &winsp_info[i];
	return NULL;
}
// 1 when the gun is drawn, 0 while it loads (the caller shows LOADING), -1 when it cannot be shown
static int wpn_inspect(const char *sh, const float *rect, float yaw_deg, float pitch_deg, float dt) {
	const WInspInfo *I = wpn_insp_info(sh); if (!I) return -1;
	if (strcmp(sh, winsp_want)) { snprintf(winsp_want, sizeof(winsp_want), "%s", sh); winsp_wait = 0.15f; }
	if (strcmp(winsp_cur, winsp_want)) {
		if ((winsp_wait -= dt) > 0) return 0;
		wpn_insp_free(); snprintf(winsp_cur, sizeof(winsp_cur), "%s", sh); snprintf(winsp_want, sizeof(winsp_want), "%s", sh);
		char p[160]; snprintf(p, sizeof(p), VDIR "vm_%s.gvm", sh); SceUInt64 t0 = sceKernelGetProcessTimeWide();
		int r = model_load(&winsp_m, p), ci = r >= 0 ? model_find_clip(&winsp_m, "idle") : -1;
		if (r < 0 || ci < 0 || winsp_m.nb > 96 || winsp_m.nmesh < 3) { if (r >= 0) model_free(&winsp_m); logp("inspect: %s not shown (%d) (b89)\n", p, r); return -1; }   // (winsp_cur keeps the name: no retry)
		model_sample(&winsp_m, ci, 0, winsp_pose); winsp_ok = 1;
		logp("inspect: %s, %d meshes, %.0f ms (b89)\n", p, winsp_m.nmesh, (sceKernelGetProcessTimeWide() - t0) / 1000.0);
	}
	if (!winsp_ok) return -1;
	int x = (int)rect[0], y = (int)rect[1], w_ = (int)rect[2], h = (int)rect[3]; if (w_ < 8 || h < 8) return -1;
	// camera at the origin looking down -z (the model's own camera space); the gun turned about its middle and moved out to
	// the distance where its bounding sphere fills the view (so no turn clips it)
	const float fov = 30 * 3.14159f / 180, R = sqrtf(I->h[0] * I->h[0] + I->h[1] * I->h[1] + I->h[2] * I->h[2]);
	const float asp = (float)w_ / h, tf = tanf(fov * 0.5f), D = R / (tf * fminf(1.0f, asp)) * 1.04f;
	float ya = yaw_deg * 3.14159f / 180, pa = pitch_deg * 3.14159f / 180, cy = cosf(ya), sy = sinf(ya), cp = cosf(pa), sp = sinf(pa);
	// rotation = Rx(pitch) * Ry(yaw) (column-major), applied to (v - c), then -D along z
	float Rm[9] = { cy, sp * sy, -cp * sy, 0, cp, sp, sy, -sp * cy, cp * cy };   // columns: images of x, y, z
	float t[3]; for (int r = 0; r < 3; r++) t[r] = -(Rm[r] * I->c[0] + Rm[3 + r] * I->c[1] + Rm[6 + r] * I->c[2]); t[2] -= D;
	float M[16] = { Rm[0], Rm[1], Rm[2], 0, Rm[3], Rm[4], Rm[5], 0, Rm[6], Rm[7], Rm[8], 0, t[0], t[1], t[2], 1 }, Pm[16], MVP[16];
	persp(Pm, fov, asp, fmaxf(1, D - R * 1.5f), D + R * 1.5f); mul4(MVP, Pm, M);
	const int fold = menu_fold_on(); if (fold) menu_fold_vp(MVP, x, 544 - y - h, w_, h);   // (b103: the emulator's pictures)
	// light from the upper left in front (eye space), taken into the gun's space (R^T)
	const float le[3] = { -0.45f, 0.62f, 0.64f }; float ld[4] = { 0, 0, 0, 0 };
	for (int k = 0; k < 3; k++) ld[k] = Rm[k * 3 + 0] * le[0] + Rm[k * 3 + 1] * le[1] + Rm[k * 3 + 2] * le[2];
	const float tint[4] = { 1.15f, 1.15f, 1.15f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	if (!fold) { glViewport(x, 544 - y - h, w_, h); glEnable(GL_SCISSOR_TEST); glScissor(x, 544 - y - h, w_, h); glClear(GL_DEPTH_BUFFER_BIT); } else menu_fold_depth();
	glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
	md_mesh0 = 2; md_rim = 0.25f; wvm_draw(&winsp_m, MVP, winsp_pose, ld, tint, 0, nofog); md_mesh0 = 0; md_rim = 0;
	if (!fold) { glDisable(GL_SCISSOR_TEST); glViewport(0, 0, 960, 544); } glEnable(GL_CULL_FACE);
	return 1;
}
