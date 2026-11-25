// menu_tank.h - b74: MY THINK TANK (the PC's TACHIKOMA screen, "My Think Tank / CUSTOMIZE THINK TANK"; the user's two
// screenshots + the Fandom "Tachikoma" page). Six slots - PROPULSION, HULL, CABIN, MAIN ARM, SUB ARM, SUPPORT - each with a
// part code, its weight and a picture of the tank turning beside them (main.c draws it: menu_fe_preview -> -2); the stats
// along the top: WEIGHT (the parts' sum against the legs' loadage, a bar), HP, MAX SPEED, ACCELERATION, DPS; REVERT and
// SAVE CHANGES. "Customize your Tachikoma to be called in during Terminal Conquest." (the screenshots' footer).
// The PC laid the slots out in one column with the stats on top; the Vita's touch targets (>= 72 px, 12 apart) put the
// slots in two columns of three. Included by menu_fe.h (needs MFE, fe_topbar, fe_hints, mu_*).
typedef struct { char code[16]; int weight, hp, loadage, speed, dps, rdmg; float accel, rspread; } MenuTkPart;
static MenuTkPart mtk[6][4]; static int mtk_n[6];      // main.c fills them from tk_parts.txt (thinktank.h tkp_info)
static unsigned char mtk_work[6];                       // the parts being tried (SAVE CHANGES puts them in cfg.tk_part)
static float mtk_msg_t = 9; static const char *mtk_msg; static int mtk_msg_bad;
static int mtk_loading;                                // main.c tk_menu_preview: the tank's first load is waiting (LOADING shows)
static const char *const MTK_SLOT[6] = { "PROPULSION", "HULL", "CABIN", "MAIN ARM", "SUB ARM", "SUPPORT" };
static int mtk_k(const unsigned char *sel, int s) { return mtk_n[s] > 0 && sel[s] < mtk_n[s] ? sel[s] : 0; }
typedef struct { int weight, loadage, hp, speed, dps; float accel; } MtkTot;
static MtkTot mtk_tot(const unsigned char *sel) {
	MtkTot t; memset(&t, 0, sizeof(t));
	for (int s = 0; s < 6; s++) { const MenuTkPart *P = &mtk[s][mtk_k(sel, s)]; t.weight += P->weight; t.hp += P->hp; }
	t.loadage = mtk[0][mtk_k(sel, 0)].loadage; t.speed = mtk[0][mtk_k(sel, 0)].speed; t.accel = mtk[0][mtk_k(sel, 0)].accel; t.dps = mtk[3][mtk_k(sel, 3)].dps;
	return t;
}
static int mtk_dirty(const MenuConfig *c) { for (int s = 0; s < 6; s++) if (mtk_k(mtk_work, s) != mtk_k(c->tk_part, s)) return 1; return 0; }
static void fe_tank_open(const MenuConfig *c) { menu_fe_open(FE_TANK); for (int s = 0; s < 6; s++) mtk_work[s] = (unsigned char)mtk_k(c->tk_part, s); MFE.sel[FE_TANK] = 0; mtk_msg_t = 9; }
static void mtk_say(const char *m, int bad) { mtk_msg = m; mtk_msg_bad = bad; mtk_msg_t = 0; }
// layout: slot tiles 200 x 102, two columns 12 apart, three rows 12 apart; the stats panel and the buttons on the right
#define MTK_TX(k) (16.0f + ((k) % 2) * 212)
#define MTK_TY(k) (124.0f + ((k) / 2) * 114)
#define MTK_TW 200.0f
#define MTK_TH 102.0f
static void mtk_stat(float x, float y, const char *lab, const char *val, int cmp) {   // cmp: +1 better than saved, -1 worse, 0 same
	mu_text(x, y, MU_FINE, 0, MU_DIM, lab); const float up[4] = { 0.55f, 0.95f, 0.45f, 1 };
	mu_text(x + 236, y, MU_TEXT, 1, cmp > 0 ? up : cmp < 0 ? MU_RED : MU_WHITE, val);
}
static void fe_tank_draw(const MenuConfig *c) {
	float a = msmooth(MFE.st / 0.3f); int sel = MFE.sel[FE_TANK]; mu_a = a;
	static const float pan[4] = { 0.02f, 0.06f, 0.08f, 0.86f };
	mu_rect(8, 64, 430, 508, pan); mu_rect(8, 64, 430, 66, MU_CYAND);
	mu_text(20, 96, MU_H2, 0, MU_WHITE, "My Think Tank"); mu_text(20, 116, MU_FINE, 0, MU_CYAN, "CUSTOMIZE THINK TANK");
	if (mtk_n[0] <= 0) { mu_textbox(20, 200, 420, 300, MU_TEXT, 0, 0, MU_DIM, "Think Tank parts not found (tk_parts.txt)."); mu_a = 1; fe_topbar(FE_TANK); fe_hints("o:BACK"); return; }
	for (int s = 0; s < 6; s++) {
		float x0 = MTK_TX(s), y0 = MTK_TY(s), x1 = x0 + MTK_TW, y1 = y0 + MTK_TH; int id = MU_ID(MU_K_ITEM, s), on = s == sel, pr = mu_pressing(id), k = mtk_k(mtk_work, s);
		fe_card_bg(x0, y0, x1, y1, on, pr);
		mu_text(x0 + 12, y0 + 30, MU_TEXT, 0, on ? MU_WHITE : MU_DIM, MTK_SLOT[s]);
		mu_textbox(x0 + 8, y0 + 36, x1 - 8, y0 + 68, MU_BODY, 0, 4, MU_CYAN, mtk[s][k].code);
		char b[24]; snprintf(b, sizeof(b), "WEIGHT %d", mtk[s][k].weight); mu_text(x0 + 12, y1 - 12, MU_FINE, 0, MU_DIM, b);
		{ static const float off[4] = { 0.30f, 0.40f, 0.44f, 1 };   // which of the slot's parts: one dot each (a "2 / 2" ran into the weight)
		  for (int i = 0; i < mtk_n[s]; i++) mu_disc(x1 - 34 - (mtk_n[s] - 1 - i) * 13, y1 - 19, 4, i == k ? MU_CYAN : off); }
		mu_tri(x1 - 18, y1 - 19, 7, 1, on ? MU_CYAN : MU_CYAND);
		if (k != mtk_k(c->tk_part, s)) mu_disc(x1 - 16, y0 + 18, 6, MU_YELLOW);   // changed since the last save
		mu_reg(id, x0, y0, x1, y1);
	}
	mu_wrap(20, 461, 404, MU_FINE, 23, 2, MU_YELLOW, "Customize your Tachikoma to be called in during Terminal Conquest.");   // two lines (one was condensed and cut)
	// the stats (the PC's panel above the tank): WEIGHT bar, then HP / MAX SPEED / ACCELERATION / DPS; green / red against the saved parts
	MtkTot T = mtk_tot(mtk_work), S0 = mtk_tot(c->tk_part); int over = T.weight > T.loadage; char b[32];
	mu_rect(442, 64, 952, 178, pan); mu_rect(442, 64, 952, 66, MU_CYAND);
	mu_text(456, 94, MU_FINE, 0, MU_DIM, "WEIGHT"); snprintf(b, sizeof(b), "%d / %d", T.weight, T.loadage); mu_text(938, 94, MU_TEXT, 1, over ? MU_RED : MU_WHITE, b);
	{ static const float tr[4] = { 0.10f, 0.20f, 0.23f, 1 }; float f = T.loadage > 0 ? mu_clamp((float)T.weight / T.loadage, 0, 1) : 0;
	  mu_rect(456, 104, 938, 114, tr); mu_rect(456, 104, 456 + 482 * f, 114, over ? MU_RED : MU_CYAN); }
	snprintf(b, sizeof(b), "%d", T.hp); mtk_stat(456, 140, "HP", b, T.hp > S0.hp ? 1 : T.hp < S0.hp ? -1 : 0);
	snprintf(b, sizeof(b), "%d", T.speed); mtk_stat(706, 140, "MAX SPEED", b, T.speed > S0.speed ? 1 : T.speed < S0.speed ? -1 : 0);
	snprintf(b, sizeof(b), "%.1f", T.accel); mtk_stat(456, 168, "ACCELERATION", b, T.accel > S0.accel + 1e-3f ? 1 : T.accel < S0.accel - 1e-3f ? -1 : 0);
	snprintf(b, sizeof(b), "%d", T.dps); mtk_stat(706, 168, "DPS", b, T.dps > S0.dps ? 1 : T.dps < S0.dps ? -1 : 0);
	if (mtk_loading) mu_textbox(442, 280, 952, 330, MU_TEXT, 2, 0, MU_DIM, "LOADING...");   // the tank's first load (main.c waits for the slide-in)
	// REVERT / SAVE CHANGES (the PC's buttons, bottom right)
	int dirty = mtk_dirty(c);
	{ int id = MU_ID(MU_K_BTN, 0); mu_button(442, 436, 690, 508, "REVERT", MU_BODY, 0, mu_pressing(id), MU_BTN, !dirty); mu_reg(id, 442, 436, 690, 508); }
	{ int id = MU_ID(MU_K_BTN, 1); mu_button(702, 436, 952, 508, "SAVE CHANGES", MU_BODY, dirty && !over, mu_pressing(id), MU_BTN_GOLD, !dirty || over); mu_reg(id, 702, 436, 952, 508); }
	if (mtk_msg_t < 2.5f && mtk_msg) { float k = mu_clamp(2.5f - mtk_msg_t, 0, 1); static const float bg[4] = { 0.02f, 0.05f, 0.06f, 0.9f }; float m[4]; mu_col(m, bg, k);
		mu_rect(456, 388, 938, 426, m); float tc[4]; mu_col(tc, mtk_msg_bad ? MU_RED : MU_YELLOW, k); mu_textbox(456, 388, 938, 426, MU_TEXT, 1, 0, tc, mtk_msg); }
	mu_a = 1;
	fe_topbar(FE_TANK); fe_hints("d:MOVE|x:CHANGE PART|s:REVERT|t:SAVE|R:ROTATE|o:BACK");
}
static int fe_tank_update(MenuConfig *c, unsigned p, float dt) {
	int *sel = &MFE.sel[FE_TANK], r = FE_NONE; mtk_msg_t += dt;
	if (*sel < 0 || *sel > 5) *sel = 0;
	if (p & MB_LEFT) *sel = (*sel & ~1); if (p & MB_RIGHT) *sel = (*sel | 1);
	if (p & MB_UP) *sel = *sel >= 2 ? *sel - 2 : *sel; if (p & MB_DOWN) *sel = *sel + 2 < 6 ? *sel + 2 : *sel;
	if (MUT.press && MU_KIND(MUT.id) == MU_K_ITEM && MU_IX(MUT.id) < 6 && MU_IX(MUT.id) != *sel) { *sel = MU_IX(MUT.id); MENU_SFX("ui/mouse_over_basic"); }
	int act = -1; if (MUT.tap >= 0 && MU_KIND(MUT.tap) == MU_K_ITEM && MU_IX(MUT.tap) < 6) act = MU_IX(MUT.tap); if (p & MB_OK) act = *sel;
	if (act >= 0 && mtk_n[act] > 0) { mtk_work[act] = (unsigned char)((mtk_k(mtk_work, act) + 1) % mtk_n[act]); MENU_SFX("ui/weapon_change_on");
		MtkTot T = mtk_tot(mtk_work); if (T.weight > T.loadage) mtk_say("Over the legs' loadage: lighter parts or stronger legs.", 1); }
	int dirty = mtk_dirty(c); MtkTot T = mtk_tot(mtk_work);
	if (((p & MB_SQR) || mu_tapped(MU_ID(MU_K_BTN, 0))) && dirty) { memcpy(mtk_work, c->tk_part, 6); MENU_SFX("ui/window_close_basic"); mtk_say("Parts reverted.", 0); }
	else if (((p & MB_TRI) || mu_tapped(MU_ID(MU_K_BTN, 1))) && dirty) {
		if (T.weight > T.loadage) { MENU_SFX("ui/character_cancel"); mtk_say("Too heavy for these legs: the changes are not saved.", 1); }
		else { memcpy(c->tk_part, mtk_work, 6); MENU_SFX("ui/weapon_change_on"); mtk_say("Saved: your Think Tank for Terminal Conquest.", 0); r = FE_APPLY; }
	}
	else if (p & MB_BACK) {   // leaving: changes that fit are kept (the PC asks; here the Circle press is the answer), others dropped
		if (dirty && T.weight <= T.loadage) { memcpy(c->tk_part, mtk_work, 6); r = FE_APPLY; }
		menu_fe_open(FE_LOBBY);
	}
	return r;
}
