// Menus and full-screen overlays, laid out for the Vita's 5" touch screen in the style of the game's own Scaleform screens
// (GameLoading, InGameUI overlays, GameExitPopup, GameResult, Lobby, CharacterSelect, GameOptionWindow, MainMenu).
// tools/menu/make_ovl.py and make_menu.py pre-render their art into two packs drawn with hud.h:
//   ovl.ghud  - loading screen + in-match overlays + pause menu (always loaded)
//   menu.ghud - front-end art (top bar, emblems, card art, portraits, weapon icons) + results art; freed during a match
//               (menu_fe_unload / menu_fe_load)
// plus one DXT1 background at a time (mload_<stem>.gtx: map loading art, 'title', 'lobby') and the thumbnail atlas
// (mthumb.gtx: map thumbnails + training program art) while the front-end is up.
// Panels, rows, tabs, buttons and arrows are drawn procedurally (menu_ui.h); text is at least 20 px (labels 24 px, headings 32+)
// and every touch target at least 72 px tall, 12 px apart. Touch: main.c calls menu_touch_poll(dt) once per frame before the
// menu update functions.
#pragma once
#include <stdarg.h>
#include "hud.h"
#ifdef __vita__
#include <psp2/gxm.h>
#endif
#include "menu_ui.h"
// ------------------------------------------------------------------ shared
static int menu_ovl = -1, menu_fe = -1;            // pack indices (hud.h packs)
static char menu_dir[128];
static float menu_time;                            // seconds, advanced by the caller (menu_tick)
typedef struct { int start, count; short node[96]; } MGroup;   // "<prefix>/0", "<prefix>/1"... parts in draw order
// name -> index lookups per pack (hashed; hud.h's finders are linear)
#define MH_SIZE 2048
typedef struct { short img[MH_SIZE], node[MH_SIZE], field[MH_SIZE]; int built; } MHash;
static MHash menu_hash[HUD_MAXPACK];
static unsigned mh_fnv(const char *s) { unsigned h = 2166136261u; while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; } return h; }
static void mh_build(MHash *M) {
	for (int i = 0; i < MH_SIZE; i++) M->img[i] = M->node[i] = M->field[i] = -1;
	for (int i = 0; i < nhimg; i++) { unsigned h = mh_fnv(himg[i].name) & (MH_SIZE - 1); while (M->img[h] >= 0) h = (h + 1) & (MH_SIZE - 1); M->img[h] = (short)i; }
	for (int i = 0; i < nhnode; i++) { unsigned h = mh_fnv(hnode[i].name) & (MH_SIZE - 1); while (M->node[h] >= 0) h = (h + 1) & (MH_SIZE - 1); M->node[h] = (short)i; }
	for (int i = 0; i < nhfield; i++) { unsigned h = mh_fnv(hfield[i].name) & (MH_SIZE - 1); while (M->field[h] >= 0) h = (h + 1) & (MH_SIZE - 1); M->field[h] = (short)i; }
	M->built = 1;
}
static MHash *mh_cur(void) { MHash *M = &menu_hash[HP - hud_pack]; if (!M->built) mh_build(M); return M; }
static int MI(const char *n) { MHash *M = mh_cur(); unsigned h = mh_fnv(n) & (MH_SIZE - 1); while (M->img[h] >= 0) { if (!strcmp(himg[M->img[h]].name, n)) return M->img[h]; h = (h + 1) & (MH_SIZE - 1); } return -1; }
static int MN(const char *n) { MHash *M = mh_cur(); unsigned h = mh_fnv(n) & (MH_SIZE - 1); while (M->node[h] >= 0) { if (!strcmp(hnode[M->node[h]].name, n)) return M->node[h]; h = (h + 1) & (MH_SIZE - 1); } return -1; }
static int MF(const char *n) { MHash *M = mh_cur(); unsigned h = mh_fnv(n) & (MH_SIZE - 1); while (M->field[h] >= 0) { if (!strcmp(hfield[M->field[h]].name, n)) return M->field[h]; h = (h + 1) & (MH_SIZE - 1); } return -1; }
// draw image `name` in the node of the same name (or `node`), optional local affine / colour
static void MD(const char *name, const char *node, const float *A, const float *mul) { hud_img(MI(name), MN(node ? node : name), A, mul, NULL, -1, 1); }
static void MT(const char *field, const char *s, const float *mul) { hud_field(MF(field), s, mul); }
static int mg_init(MGroup *g, const char *prefix) {
	char b[48]; snprintf(b, sizeof(b), "%s/0", prefix); g->start = MI(b); g->count = 0;
	if (g->start < 0) return -1;
	size_t pl = strlen(prefix);
	while (g->start + g->count < nhimg && g->count < 96) { const char *n = himg[g->start + g->count].name;
		if (strncmp(n, prefix, pl) || n[pl] != '/') break; g->node[g->count] = (short)MN(n); g->count++; }
	return g->count;
}
static void mg_draw(const MGroup *g, const float *mul) {
	for (int i = 0; i < g->count; i++) hud_img(g->start + i, g->node[i], NULL, mul, NULL, -1, 1);
}
static __attribute__((unused)) void mg_draw_at(const MGroup *g, const float *mul, float dx, float dy) {   // shifted in each part's own plane units
	float A[6] = { 1, 0, dx, 0, 1, dy }; for (int i = 0; i < g->count; i++) hud_img(g->start + i, g->node[i], A, mul, NULL, -1, 1);
}
// GTX1: 'GTX1' u16 w, u16 h, u8 fmt (1 = DXT1), u8 0, u16 0, u32 size, data
#ifdef __vita__
#define MENU_TEX_US() sceKernelGetProcessTimeWide()   // (b118: the upload's own clock - this header is built without texcanary.h in the menu host test)
#else
#define MENU_TEX_US() 0ULL
#endif
static GLuint menu_tex_load(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return 0;
	unsigned char h[16]; if (fread(h, 1, 16, f) != 16 || memcmp(h, "GTX1", 4)) { fclose(f); return 0; }
	int w = h[4] | h[5] << 8, hh = h[6] | h[7] << 8, fmt = h[8]; unsigned size = h[12] | h[13] << 8 | h[14] << 16 | (unsigned)h[15] << 24;
	void *buf = malloc(size); if (!buf || fread(buf, 1, size, f) != size) { free(buf); fclose(f); return 0; }
	fclose(f); GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	unsigned long long mt0 = MENU_TEX_US();
	if (fmt == 1) glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, w, hh, 0, size, buf);
	else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf);
	unsigned long long mt1 = MENU_TEX_US();
#ifdef __vita__
	sceGxmTransferFinish();
#endif
	// b118 (the device's b117 log: one frame of 25.5 s as the lobby opened after the results, with this upload in it and a
	// system screenshot taken seconds before; the same step took its usual time the two other times): which call waited
	{ unsigned long long mt2 = MENU_TEX_US(); if (mt2 - mt0 > 200000) logp("menu texture %s: the upload took %.0f ms, the wait for its copy %.0f ms (b118: a stall here is logged)\n", path, (mt1 - mt0) / 1000.0, (mt2 - mt1) / 1000.0); }
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	free(buf); return t;
}
// one cached full-screen background (map loading art, title art)
static GLuint menu_bg_tex; static char menu_bg_name[48];
static void menu_bg(const char *name, float alpha) {
	if (strcmp(menu_bg_name, name)) {
		if (menu_bg_tex) { hud_flush(); glDeleteTextures(1, &menu_bg_tex); menu_bg_tex = 0; }
		char p[200]; snprintf(p, sizeof(p), "%smload_%s.gtx", menu_dir, name); menu_bg_tex = menu_tex_load(p); snprintf(menu_bg_name, sizeof(menu_bg_name), "%s", name);
	}
	int nd = MN("screen"); if (!menu_bg_tex || nd < 0) return;
	static const float xy[4][2] = { { 0, 0 }, { 960, 0 }, { 960, 544 }, { 0, 544 } }, uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	float m[4] = { alpha, alpha, alpha, alpha }; hud_poly(menu_bg_tex, nd, xy, uv, 4, m);
}
static HudPack *menu_prev;
static void menu_begin(int pack) { menu_prev = HP; hud_begin(); hud_use(pack); mu_bind(MI("white"), MN("screen")); mu_a = 1; }
static void menu_end(void) { hud_end(); HP = menu_prev; mu_a = 1; }
// rectangle in screen pixels (kept for main.c: dims the screen under the in-match options)
static void menu_rect(float x0, float y0, float x1, float y1, const float *col) { mu_rect(x0, y0, x1, y1, col); }
static __attribute__((unused)) void menu_text(float x, float y, float size, int align, const float *col, const char *s) { mu_text(x, y, size, align, col, s); }
// a text field's look (bounds, size, alignment, colour) drawn in another node's plane (e.g. one field for every button)
static __attribute__((unused)) void menu_field_in(int fi, int nd, const char *s, const float *mul) {
	if (fi < 0 || nd < 0 || !s) return; const HField *F = &hfield[fi];
	float col[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f }; if (mul) for (int k = 0; k < 4; k++) col[k] *= mul[k];
	float w = hud_text_w(s) * F->size, x = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2;
	hud_text(nd, x, F->b[2] + 2 + hasc * F->size, F->size, 0, col, HUD_Z4, s);
}
static __attribute__((unused)) void menu_fieldf(const char *field, const float *mul, const char *fmt, ...) {
	char b[160]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); hud_field(MF(field), b, mul);
}
// a field of the original layout drawn at a readable size: the text sits where the field is (its alignment, vertically
// centred in its box), with an explicit em size in screen pixels; col NULL = the field's own colour
static int mu_field_box(const char *field, float *x0, float *y0, float *x1, float *y1) {
	int fi = MF(field); if (fi < 0) return -1; const HField *F = &hfield[fi]; float ax, ay, bx, by;
	hud_node_screen(F->node, F->b[0], F->b[2], &ax, &ay); hud_node_screen(F->node, F->b[1], F->b[3], &bx, &by);
	*x0 = fminf(ax, bx); *x1 = fmaxf(ax, bx); *y0 = fminf(ay, by); *y1 = fmaxf(ay, by); return fi;
}
static void mu_fld(const char *field, const char *s, float size, const float *col, float a) {
	float x0, y0, x1, y1; int fi = mu_field_box(field, &x0, &y0, &x1, &y1); if (fi < 0 || !s) return; const HField *F = &hfield[fi];
	float c[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f }; if (col) memcpy(c, col, 16); c[3] *= a;
	float y = (y0 + y1) * 0.5f + size * MU_CAP * 0.5f, x = F->align == 0 ? x0 + 2 : F->align == 1 ? x1 - 2 : (x0 + x1) * 0.5f;
	mu_text(x, y, size, F->align == 0 ? 0 : F->align == 1 ? 1 : 2, c, s);
}
// ------------------------------------------------------------------ data
enum { MODE_TDM, MODE_DEM, MODE_TTM, MODE_GM, MODE_EM, MODE_GA, MODE_COUNT };   // b63: GA = Ghost Assault (ga.h; the 2015 alpha's mode, footage: IGN, Nick Bunyun)
// names and objectives as the original shows them (scoreboard / match start splash in the gameplay footage). "em" is
// Elimination: the Western build's minimaps EM_urbancity / EM_Port / EM_01 and the footage's ELIMINATION on Urban City.
static const char *MENU_MODE_NAME[MODE_COUNT] = { "TEAM DEATHMATCH", "DEMOLITION", "TERMINAL CONQUEST", "GHOST MISSION", "ELIMINATION", "GHOST ASSAULT" };
static const char *MENU_MODE_NAME2[MODE_COUNT] = { "TEAM\nDEATHMATCH", "DEMOLITION", "TERMINAL\nCONQUEST", "GHOST\nMISSION", "ELIMINATION", "GHOST\nASSAULT" };   // two-line form for cards
static const char *MENU_MODE_ICON[MODE_COUNT] = { "tdm", "dm", "ttm", "gm", "em", "gm" };            // ovl pack: ld.mode.* / sp.mode.*
static const char *MENU_MODE_EMB[MODE_COUNT] = { "me.tdm", "me.dem", "me.ttm", "me.gm", "me.em", "me.gm" };   // menu pack emblems
static const char *MENU_MODE_GOAL[MODE_COUNT] = { "Eliminate all enemy reinforcements", "Work against the enemy team in a bombing mission", "Secure the terminals", "Complete the mission", "Eliminate all enemy reinforcements", "A deadly game of hide and seek between Ghosts and Cyborgs" };
static const int MENU_MODE_OFFER[5] = { MODE_TDM, MODE_DEM, MODE_TTM, MODE_EM, MODE_GA };   // the modes the menus offer, in this order
#define MENU_NOFFER 5
// score limit per mode: values and unit (TDM kills, DEM rounds won, TTM points (first to 4 in the footage), EM rounds won (first to 6))
static const int MENU_LIMITS[MODE_COUNT][4] = { { 30, 50, 80, 100 }, { 4, 5, 6, 8 }, { 2, 3, 4, 5 }, { 1, 1, 1, 1 }, { 3, 4, 5, 6 }, { 3, 4, 5, 6 } };   // (GA: rounds won, first to 4 = the footage's best of 7)
static const int MENU_LIMIT_DEF[MODE_COUNT] = { 2, 2, 2, 0, 3, 1 };   // (b83: Demolition first to 6, v2.0)   // default index (quick match; b79: TDM first to 8000 = 80 kills, v2.0 - the Paz video EP19's "8000")
static const char *MENU_LIMIT_UNIT[MODE_COUNT] = { "KILLS", "ROUNDS", "POINTS", "", "ROUNDS", "ROUNDS" };
static const int MENU_TIME_DEF[MODE_COUNT] = { 10, 10, 15, 10, 10, 10 };   // quick match time limit (minutes)
// the 12 operators, in the order of the engine's CHARS[] (bots.h / main.c)
typedef struct { const char *stem, *name, *skill, *skill_msg; int skill_icon, portrait; } MenuChr;
static const MenuChr MENU_CHR[12] = {   // skills: names from the voice lines (d_<operator>_use_<skill>), icons = GameLoading skillIconMc frames (-1: none there)
	{ "bato", "BATOU", "ARM LAUNCHER", "Fire a missile from the launcher hidden in your arm.", -1, 1 },   // (b84: v2.0 texts, EP16 / EP15 / EP18 / EP26)
	{ "motoko", "MOTOKO KUSANAGI", "THERM-OPTIC CAMO", "An active camo layer that makes you nearly invisible.", 0, 2 },
	{ "saito", "SAITO", "HEAT SENSOR", "Uplink to a satellite and see enemies through walls.", 1, 3 },   // b83: v2.0 names / texts (EP20-EP23)
	{ "togusa", "TOGUSA", "SEEKER DRONE", "A hunter drone that explodes on contact with an enemy.", 5, 4 },   // (b85: v2.0, EP17)
	{ "ishikawa", "ISHIKAWA", "CYBER SENTRY", "A sentry that targets any enemies in range.", 7, 5 },
	{ "borma", "BORMA", "NANO-GEL ARMOR", "Repair gel that boosts and regenerates your HP and armor.", 2, 6 },
	{ "paz", "PAZ", "HYPER SPRINT", "Boost your speed and jumps, and reach further with your knife.", 3, 7 },   // the footage's operative select: HYPER SPRINT
	{ "azuma", "AZUMA", "TACTICAL SHIELD", "Raise an energy shield that stops attacks from the front.", -1, 8 },   // b63: the final build's name (footage "EP. 24 Azuma": operative screen, DETAILS)
	{ "kuro", "KURO", "EMP GENERATOR", "Disables the skills and electronics of nearby enemies.", -1, 9 },   // (b85: v2.0, EP25)
	{ "maven", "MAVEN", "THERM-OPTIC BARRIER", "A therm-optic wall that blocks sight and slows bullets.", 6, 10 },
	{ "reiko", "REIKO", "NET JUMP", "Hack the eyes watching you and move unseen.", -1, 11 },
	{ "sitara", "SITARA", "SIPHON DRONE", "A drone that scorches nearby enemies and heals you a little.", -1, 12 },
};
// b59: an operator's name where room is short (the match lobby's rows; as the bots are called in the match): its first word
static const char *menu_chr_short(int o) {
	static char b[4][24]; static int k; if (o < 0 || o >= 12) return ""; const char *n = MENU_CHR[o].name, *sp = strchr(n, ' ');
	if (!sp) return n; k = (k + 1) & 3; snprintf(b[k], sizeof(b[k]), "%.*s", (int)(sp - n), n); return b[k];
}
// the HUD's skill glyph of each operator (pack image sk.N; InGameUI skillMC.skillIcon frames, tools/skills/SKILLS.md)
static const int MENU_CHR_SKICO[12] = { 12, 8, 3, 13, 1, 2, 9, 17, 16, 5, 18, 19 };
static const char *MENU_TIPS[] = {   // Vita buttons as {tokens} (vita_prompt.h glyphs): no PC keys anywhere
	"Hold {l} to aim down sights: your shots group much tighter.",
	"Armour (AP) absorbs most of the damage you take while it lasts.",
	"Kills fill the big skill ring. When it turns orange, hold {right} for Tier 2.",
	"The small skill ring is Tier 1: tap {right}. It counts down while it recharges.",
	"Headshots deal more than twice the damage of body shots.",
	"Reload with {s} behind cover: an empty magazine takes longer.",
	"Tap {down} to sprint. Sprinting lowers your weapon: slow down at corners.",
	"Your health regenerates if you avoid damage for a few seconds.",
	"Hold {right} for Tier 2: allies near you download it with {s}.",
	"{up} throws a grenade, {left} draws your knife.",
	"Sprint, then press {o} to slide into cover.",
};
#define MENU_NTIPS ((int)(sizeof(MENU_TIPS) / sizeof(MENU_TIPS[0])))
// training programs behind the TUTORIAL card (cfg.tut = index + 1; tutorial.h runs them). Maps from the tutorial footage:
// Control = the corridors with crates and ladders (Fix_Tutorial2), Combat = the holographic range (Fix_Tutorial3),
// Cyberbrain and the Think Tank drill = the hangar with desks and the glass enclosures (Fix_Tutorial).
typedef struct { const char *name, *name2, *desc; int map, thumb; } MenuTut;
static const MenuTut MENU_TUT[4] = {
	{ "CONTROL TRAINING PROGRAM", "CONTROL\nTRAINING", "Move, sprint, crouch, walk, jump and climb.", 15, 17 },
	{ "COMBAT TRAINING PROGRAM", "COMBAT\nTRAINING", "Hip fire, iron sights, sidearms, sniper rifles and grenades.", 16, 18 },
	{ "CYBERBRAIN TRAINING PROGRAM", "CYBERBRAIN\nTRAINING", "Therm-optic camo, intel tiers and SkillSync.", 14, 19 },
	{ "THINK TANK TRAINING PROGRAM", "THINK TANK\nTRAINING", "Hack an enemy Think Tank, then destroy it.", 14, 20 },
};
static const char *MENU_TUT_GOAL = "Complete the prosthetic calibration program";
// what the menus hand back to the game
typedef struct {
	int mode, map, chr, primary, secondary;          // MODE_*, map (MAPS[] index), operator (CHARS[] index), MENU_WPN indices
	int score_limit, time_limit, team_size, bot_skill;   // kills (TDM), rounds to win (DEM, EM), points (TTM); minutes; players per team; 0 easy .. 3 veteran
	float look_sens, aim_sens; int invert_y, aim_assist;  // sensitivities 0..1 (0.5 default)
	float vol_master, vol_music, vol_sfx, vol_voice;     // 0..1
	char name[20]; int level, exp, exp_next;             // profile shown in the lobby bar
	int tut;                                             // tutorial program to run: 0 none, 1 control, 2 combat, 3 cyberbrain, 4 thinktank (tutorial.h)
	int touch_off;                                       // 1: the menus ignore the touch screen (OPTIONS > CONTROLS)
	// b58 (footage: MISSION +1 / ACHIEVEMENT +1 on the result tabs): the career, saved with the settings (main.c cfg_load
	// reads an older, shorter config.bin too: these start at 0)
	unsigned ach;                                        // achievements earned (MENU_ACH bits)
	int c_kills, c_wins, c_matches; unsigned c_maps, c_modes;   // enemies defeated, matches won / played; match maps played on, modes won in (bits)
	int intro_off;                                       // b70: 1 = no intro movie after the title (OPTIONS > GAME; an older config.bin: 0, plays)
	int mask_off;                                        // b71: 1 = the enemy team without the PC's enemy masks (OPTIONS > GAME; an older config.bin: 0, masks on)
	unsigned char skin[12];                              // b71: each operator's skin (operative screen; bots.h chr_skins.txt row of that operator, 0 = standard)
	int bot_skin_std;                                    // b71: 1 = the bots in their standard outfits (OPTIONS > GAME > BOT SKINS; 0 random)
	unsigned char tk_part[6], tk_pad[2];                 // b74: My Think Tank's parts (PROPULSION, HULL, CABIN, MAIN ARM, SUB ARM, SUPPORT: tk_parts.txt k; an older config.bin: 0 = the 001A set)
	int melee;                                           // b98: the melee weapon - 0 MEGATECH KNIFE, 1 DOUBLE KATAR (ARMORY > MELEE; an older config.bin: 0)
	unsigned short gog_off[12];                          // b103: per operator, bit k = skin k's mask / goggles taken off (operative screen, Square; an older config.bin: 0, worn)
} MenuConfig;
static const int MENU_TIME[4] = { 5, 10, 15, 20 }, MENU_TEAM[4] = { 1, 2, 3, 4 };   // players per team (the Vita runs up to 7 bots; b55: 1 VS 1 too - the D-pad could not get back to it)
static const int MENU_SCORE_TDM[4] = { 30, 50, 80, 100 }, MENU_ROUNDS_DEM[4] = { 3, 5, 7, 9 };
static const char *MENU_SKILL[4] = { "EASY", "NORMAL", "HARD", "VETERAN" };
#define MENU_WPN_M4RD 10   // b82: MENU_WPN's red-dot M4A1, the default primary of a new profile (the footage's M4A1)
static void menu_config_default(MenuConfig *c) {
	memset(c, 0, sizeof(*c)); c->mode = MODE_TDM; c->map = 5; c->chr = 1; c->primary = MENU_WPN_M4RD; c->secondary = 8;
	c->score_limit = 80; c->time_limit = 10; c->team_size = 3; c->bot_skill = 1;
	c->look_sens = c->aim_sens = 0.5f; c->aim_assist = 1; c->vol_master = 1; c->vol_music = 0.7f; c->vol_sfx = 1; c->vol_voice = 1;
	snprintf(c->name, sizeof(c->name), "OPERATOR"); c->level = 5; c->exp = 48; c->exp_next = 1000;
}
static int menu_insp_state = 1;   // b89: main.c wpn_inspect's last answer for the INSPECT view (1 drawn, 0 loading, -1 not available: the icon instead)
static float menu_prev_shift;   // b76: horizontal lens shift (NDC) of the next 3D preview, set by the *_preview functions, read and cleared by main.c chr_preview
static void menu_tick(float dt) { menu_time += dt; }
static int menu_tut_of_map(const char *stem);   // (after MENU_MAP)
static int menu_ld_tut;                          // training program being loaded (set by the front-end), for the loading screen
// ------------------------------------------------------------------ loading screen (GameLoading.swf)
static struct { MGroup bg; int ok; char date[16]; int tip; } MLD;
// draws the whole loading screen (call between loading steps, then swap): progress 0..1, chr = operator index or -1
static void menu_loading_draw(const char *map_stem, const char *map_name, int mode, float progress, const char *status, int chr) {
	if (menu_ovl < 0) return;
	menu_begin(menu_ovl);
	if (!MLD.ok) { mg_init(&MLD.bg, "ld.bg"); MLD.ok = 1; snprintf(MLD.date, sizeof(MLD.date), "20%02d.%d.%d", 24 + (int)(menu_time * 7) % 10, 3 + (int)(menu_time * 13) % 10, 1 + (int)(menu_time * 31) % 28); MLD.tip = (int)(menu_time * 1000) % MENU_NTIPS; }
	int tut = menu_ld_tut > 0 && menu_ld_tut <= 4 && map_stem && !strncmp(map_stem, "tutorial", 8) ? menu_ld_tut : 0;
	menu_bg(tut ? (tut == 1 ? "tutorial" : tut == 2 ? "tutorial2" : "tutorial3") : map_stem, 1);   // the program's loading art (the footage: Combat = Tutorial2_big)
	mg_draw(&MLD.bg, NULL);
	if (mode < 0 || mode >= MODE_COUNT) mode = 0;
	int range = map_stem && !strcmp(map_stem, "shooting");
	{ char b[32]; snprintf(b, sizeof(b), "ld.mode.%s", MENU_MODE_ICON[tut || range ? 0 : mode]); MD(b, "ld.modeicon", NULL, NULL); }
	MT("ld.modename", tut ? "TUTORIAL" : range ? "SHOOTING RANGE" : MENU_MODE_NAME[mode], NULL);
	// b59 (user: names ran over what is beside them): the map name keeps to its own field; the goal takes the room after it
	float mx = 0, mend = 0;
	{ char tn[32]; if (tut) { snprintf(tn, sizeof(tn), "%s", MENU_TUT[tut - 1].name2); for (char *q = tn; *q; q++) if (*q == '\n') *q = ' '; }   // "COMBAT TRAINING"
	  const char *mn = tut ? tn : map_name; float x0, y0, x1, y1; int fi = mu_field_box("ld.map", &x0, &y0, &x1, &y1);
	  if (fi >= 0 && mn) { const HField *F = &hfield[fi]; float c[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f };
		float w = fminf(mu_tw(mn, MU_BODY), 300); mu_textbox(x0, y0, x0 + w + 4, y1, MU_BODY, 0, 2, c, mn); mx = x0; mend = x0 + w + 4; } }
	MT("ld.date", MLD.date, NULL);
	{ const char *g = tut ? MENU_TUT_GOAL : range ? "Test your loadout. No time limit." : MENU_MODE_GOAL[mode]; float x0, y0, x1, y1; int fi = mu_field_box("ld.tip", &x0, &y0, &x1, &y1);
	  if (fi >= 0) { const HField *F = &hfield[fi]; float c[4] = { F->col[0] / 255.f, F->col[1] / 255.f, F->col[2] / 255.f, F->col[3] / 255.f };
		float l = fmaxf(x0, mend + 20); (void)mx; mu_textbox(l, y0, x1, y1, MU_TEXT, F->align == 0 ? 0 : F->align == 1 ? 1 : 2, 2, c, g); } }
	// b59 (user: the loading screens were too cluttered, covering the picture): one tip per load - the operator's skill or a
	// gameplay tip, by turns - in a low band along the bottom edge, with the status, the percentage and the bar
	static const float band0[4] = { 0, 0, 0, 0 }, band1[4] = { 0.01f, 0.03f, 0.04f, 0.86f };
	mu_rectv(0, 432, 960, 492, band0, band1); { static const float b2[4] = { 0.01f, 0.03f, 0.04f, 0.86f }; mu_rect(0, 492, 960, 544, b2); }
	if (chr >= 0 && chr < 12 && (MLD.tip & 1)) {   // the operator's skill: its glyph in a circle, TIP + the skill, what it does
		char b[24]; snprintf(b, sizeof(b), "sk.%d", MENU_CHR_SKICO[chr]); static const float disc[4] = { 0.02f, 0.06f, 0.07f, 0.85f };
		mu_disc(46, 490, 26, disc); mu_ring(46, 490, 23.5f, 26, MU_CYAN); mu_imgfit(MI(b), 29, 473, 63, 507, NULL);
		float x = 86, xs = x + mu_tw("TIP ", MU_SMALL) + 6; mu_text(x, 482, MU_SMALL, 0, MU_YELLOW, "TIP"); mu_text(xs, 482, MU_BODY, 0, MU_WHITE, MENU_CHR[chr].skill);
		mu_textbox(x, 490, 880, 520, MU_TEXT, 0, 0, MU_WHITE, MENU_CHR[chr].skill_msg);
	} else {   // a gameplay tip
		mu_text(28, 498, MU_SMALL, 0, MU_YELLOW, "TIP"); mu_textbox(28 + mu_tw("TIP ", MU_SMALL) + 6, 478, 880, 508, MU_TEXT, 0, 0, MU_WHITE, MENU_TIPS[MLD.tip]);
	}
	(void)status;   // b63 (user: "loading textures" / "loading collision" seemed unnecessary): no stage text, the bar and the percentage only
	// progress bar along the bottom edge, the percentage, a small spinner beside it
	if (progress >= 0) {
		if (progress > 1) progress = 1;
		float bg[4] = { 0.05f, 0.08f, 0.09f, 1 }, fg[4] = { 0.30f, 0.85f, 0.88f, 1 };
		mu_rect(0, 540, 960, 544, bg); mu_rect(0, 540, 960 * progress, 544, fg);
		{ char b[16]; snprintf(b, sizeof(b), "%d%%", (int)(progress * 100 + 0.5f)); mu_text(944, 530, MU_BODY, 1, MU_WHITE, b); }
	}
	for (int i = 0; i < 8; i++) { float ph = fmodf(menu_time * 1.6f - i * 0.125f, 1.0f); if (ph < 0) ph += 1; float a = 0.2f + 0.8f * (ph < 0.25f ? 1 - ph * 4 : 0), an = i * 0.785398f;
		float c[4] = { 0.45f, 0.88f, 0.95f, a }; mu_disc(872 + 9 * cosf(an), 520 + 9 * sinf(an), 2.2f, c); }
	menu_end();
}
static void menu_loading_reset(void) { MLD.ok = 0; }
// ------------------------------------------------------------------ in-match overlays (InGameUI.swf / GameExitPopup.swf, ovl pack)
static float mfade(float t, float in, float hold, float out) {   // 0..1 envelope: fade in, hold, fade out
	if (t < 0) return 0; if (t < in) return t / in; t -= in; if (t < hold) return 1; t -= hold; return t < out ? 1 - t / out : 0;
}
static float msmooth(float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }
// ui b44 (QA test C on b42: the white TEAM DEATHMATCH splash was unreadable over pss9's bright spawn-room window): white-on-world
// titles (the start splash, the mode cards, the round banners and their goal lines) get a soft dark band - flat behind the text,
// fading out over 44 px at the ends and 6 px above / below (no hard box: the original's look) - and a 2 px drop shadow, both
// faded with the title. The band claims its rectangle with the title (hud.h claims).
#define MU_TB_FX 44
#define MU_TB_FY 6
static void mu_softband(float x0, float y0, float x1, float y1, float k) {   // k: the band's alpha in its flat middle
	if (k <= 0.004f) return;
	const float xs[4] = { x0 - MU_TB_FX, x0, x1, x1 + MU_TB_FX }, ys[4] = { y0 - MU_TB_FY, y0, y1, y1 + MU_TB_FY }, w[4] = { 0, 1, 1, 0 };
	for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
		const int ci[4][2] = { { i, j }, { i + 1, j }, { i + 1, j + 1 }, { i, j + 1 } }; float p[4][2], c[4][4];
		for (int v = 0; v < 4; v++) { p[v][0] = xs[ci[v][0]]; p[v][1] = ys[ci[v][1]]; c[v][0] = 0.01f; c[v][1] = 0.03f; c[v][2] = 0.04f; c[v][3] = k * w[ci[v][0]] * w[ci[v][1]]; }
		mu_polyc(4, (const float (*)[2])p, (const float (*)[4])c); }
}
// screen box (x0 y0 x1 y1: the cap height) of s in a text field: drawn by mu_fld at size px (size > 0) or by the field itself (MT, size 0)
static int mu_fld_text_box(const char *field, const char *s, float size, float *b) {
	float x0, y0, x1, y1; int fi = mu_field_box(field, &x0, &y0, &x1, &y1); if (fi < 0 || !s) return 0; const HField *F = &hfield[fi];
	if (size > 0) { float w = mu_tw(s, size), l = F->align == 0 ? x0 + 2 : F->align == 1 ? x1 - 2 - w : (x0 + x1) / 2 - w / 2, cy = (y0 + y1) / 2;
		b[0] = l; b[2] = l + w; b[1] = cy - size * MU_CAP / 2; b[3] = cy + size * MU_CAP / 2; }
	else { float w = hud_text_w(s) * F->size, l = F->align == 0 ? F->b[0] + 2 : F->align == 1 ? F->b[1] - 2 - w : (F->b[0] + F->b[1]) / 2 - w / 2, base = F->b[2] + 2 + hasc * F->size;
		float ax, ay, bx, by; hud_node_screen(F->node, l, base - MU_CAP * F->size, &ax, &ay); hud_node_screen(F->node, l + w, base, &bx, &by);
		b[0] = fminf(ax, bx); b[2] = fmaxf(ax, bx); b[1] = fminf(ay, by); b[3] = fmaxf(ay, by); }
	b[0] += hud_dx; b[2] += hud_dx; b[1] += hud_dy; b[3] += hud_dy; return 1;
}
static void mu_box_union(float *u, const float *b) { if (u[0] > u[2]) { memcpy(u, b, 16); return; } u[0] = fminf(u[0], b[0]); u[1] = fminf(u[1], b[1]); u[2] = fmaxf(u[2], b[2]); u[3] = fmaxf(u[3], b[3]); }
// the band behind the lines' union u (from mu_fld_text_box), alpha a; returns its outer rectangle in r (for the claim)
static void mu_title_band(const float *u, float a, float *r) {
	float x0 = u[0] - 10, y0 = u[1] - 7, x1 = u[2] + 10, y1 = u[3] + 5;   // (5 px under the baseline: a y's tail; the START notice under a round banner stays near its b43 place)
	mu_softband(x0, y0, x1, y1, 0.52f * a);
	r[0] = x0 - MU_TB_FX; r[1] = y0 - MU_TB_FY; r[2] = x1 + MU_TB_FX; r[3] = y1 + MU_TB_FY;
}
static const float MU_SHADOW[4] = { 0, 0, 0, 0.62f };
static void mu_fld_shadowed(const char *field, const char *s, float size, const float *col, float a) {   // mu_fld with the drop shadow
	float dx = hud_dx, dy = hud_dy; hud_dx += 2; hud_dy += 2; mu_fld(field, s, size, MU_SHADOW, a); hud_dx = dx; hud_dy = dy; mu_fld(field, s, size, col, a);
}
static void mt_shadowed(const char *field, const char *s, const float *m) {   // MT (the field's own look) with the drop shadow
	float sh[4] = { 0, 0, 0, MU_SHADOW[3] * (m ? m[3] : 1) }, dx = hud_dx, dy = hud_dy; hud_dx += 2; hud_dy += 2; MT(field, s, sh); hud_dx = dx; hud_dy = dy; MT(field, s, m);
}
// digits with the Font_Number_30 glyphs in node nd's plane: origin = top-left (align 0) or top-right (align 1) or top-centre (2)
static void menu_digits(int nd, int v, int align, const float *mul) {
	char t[16]; int n = snprintf(t, sizeof(t), "%d", v < 0 ? 0 : v); float adv = 33, w = n * adv, x = align == 1 ? -w : align == 2 ? -w / 2 : 0;
	int g0 = MI("n30.0"); if (g0 < 0) return;
	for (int i = 0; i < n; i++) { float A[6] = { 1, 0, x + i * adv, 0, 1, 0 }; hud_img(g0 + (t[i] - '0'), nd, A, mul, HUD_Z4, -1, 1); }
}
static void menu_digit_slots(const char *prefix, int nslots, int v, const float *mul) {   // slot nodes prefix1 (ones) .. prefixN
	char t[16], b[32]; int n = snprintf(t, sizeof(t), "%d", v < 0 ? 0 : v); int g0 = MI("n30.0"); if (g0 < 0) return;
	for (int i = 0; i < nslots && i < n; i++) { snprintf(b, sizeof(b), "%s%d", prefix, i + 1); hud_img(g0 + (t[n - 1 - i] - '0'), MN(b), NULL, mul, HUD_Z4, -1, 1); }
}
// match start splash: mode emblem + name, then round / mission line (t = seconds since the match started; ~5 s)
static void menu_splash_draw(int mode, int round, const char *mission, float t) {
	HUD_TAG("splash");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0 || t > 6) return;
	menu_begin(menu_ovl);
	if (mode < 0 || mode >= MODE_COUNT) mode = 0;
	float a = mfade(t, 0.3f, 4.6f, 0.8f), s = 1.15f - 0.15f * msmooth(t / 0.35f), m[4] = { 1, 1, 1, a };
	float a2 = t < 2.4f || !mission ? 0 : mfade(t - 2.4f, 0.3f, 2.6f, 0.8f); char r[24]; snprintf(r, sizeof(r), "ROUND %d", round);
	// ui b44: one soft band behind the title and the objective that follows it (the wider of the two; faded with whichever shows)
	float u[4] = { 1, 1, 0, 0 }, tb[4], cr[4] = { 296, 84, 664, 210 };
	if (mu_fld_text_box("sp.mode", MENU_MODE_NAME[mode], 30, tb)) mu_box_union(u, tb);
	if (mission && round > 0) { if (mu_fld_text_box("sp.round", r, 28, tb)) mu_box_union(u, tb); if (mu_fld_text_box("sp.rmission", mission, 24, tb)) mu_box_union(u, tb); }
	else if (mission && mu_fld_text_box("sp.mission", mission, 24, tb)) mu_box_union(u, tb);
	if (u[0] < u[2]) { float br[4]; mu_title_band(u, fmaxf(a, a2), br); cr[0] = fminf(cr[0], br[0]); cr[1] = fminf(cr[1], br[1]); cr[2] = fmaxf(cr[2], br[2]); cr[3] = fmaxf(cr[3], br[3]); }
	char b[32]; snprintf(b, sizeof(b), "sp.mode.%s", MENU_MODE_ICON[mode]); int im = MI(b), en = MN("sp.emblem"), emb = im >= 0 && en >= 0;
	if (emb) {   // ui b44: not over Terminal Conquest's bar (it holds the emblem's spot under the clock: ttm.h claims it first) - the title shows alone
		const float *l = himg[im].loc; float ex0, ey0, ex1, ey1; hud_node_screen(en, l[0], l[1], &ex0, &ey0); hud_node_screen(en, l[2], l[3], &ex1, &ey1);
		emb = !hud_blocked(fminf(ex0, ex1), fminf(ey0, ey1), fmaxf(ex0, ex1), fmaxf(ey0, ey1), HUD_P_BANNER); }   // (before the splash's own claim)
	if (fmaxf(a, a2) > 0.02f) hud_claim(cr[0], cr[1], cr[2], cr[3], HUD_P_BANNER);   // ui b42 (hud.h claims); b44: with the band
	if (emb) { const float *l = himg[im].loc; float cx = (l[0] + l[2]) / 2, cy = (l[1] + l[3]) / 2; float A[6] = { s, 0, cx * (1 - s), 0, s, cy * (1 - s) }; hud_img(im, en, A, m, NULL, -1, 1); }
	if (t < 2.4f || !mission) mu_fld_shadowed("sp.mode", MENU_MODE_NAME[mode], 30, MU_WHITE, a);
	else {   // then the objective (with the round number in round-based modes)
		if (round > 0) { mu_fld_shadowed("sp.round", r, 28, MU_WHITE, a2); mu_fld_shadowed("sp.rmission", mission, 24, NULL, a2); }
		else mu_fld_shadowed("sp.mission", mission, 24, NULL, a2);
	}
	menu_end();
}
// a yellow notice line on a dark bar ("START", "Skill Ready" ...), t = seconds since shown (~2.5 s)
static void menu_notice_draw(const char *txt, float t) {
	HUD_TAG("notice");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || !txt || t < 0 || t > 3) return;
	menu_begin(menu_ovl); float a = mfade(t, 0.15f, 2.0f, 0.6f), m[4] = { 1, 1, 1, a };
	// ui b42 (HUD de-clutter, user report b41: the bar crossed the minimap): the bar as wide as its line plus the soft ends, at most
	// the centre column (192..768: clear of the minimap and the kill feed); a long line drops to 24 px to fit; moved clear of a
	// banner / the timer warning (hud.h claims), else hidden while they show
	int im = MI("nt.bar"), nd = MN("nt.bar"); float size = 30, tw = mu_tw(txt, size); if (tw > 500) { size = fmaxf(MU_TEXT, size * 500 / tw); tw = mu_tw(txt, size); }
	float hw = fminf(288, tw * 0.5f + 64), y0 = 125, y1 = 148;
	if (im >= 0 && nd >= 0) { const float *l = himg[im].loc; float ax, ay, bx, by; hud_node_screen(nd, l[0], (l[1] + l[3]) / 2, &ax, &ay); hud_node_screen(nd, l[2], (l[1] + l[3]) / 2, &bx, &by);
		hud_node_screen(nd, l[0], l[1], &ax, &y0); hud_node_screen(nd, l[0], l[3], &ax, &y1); }
	float d = hud_place(480 - hw, y0, 480 + hw, y1, HUD_P_NOTICE, 88, 330); if (d >= HUD_HIDE) { menu_end(); return; }
	hud_dy = d;
	if (im >= 0 && nd >= 0) { const float *l = himg[im].loc; float ax, ay, bx, by; hud_node_screen(nd, l[0], (l[1] + l[3]) / 2, &ax, &ay); hud_node_screen(nd, l[2], (l[1] + l[3]) / 2, &bx, &by);
		float k = fminf(1, 2 * hw / fmaxf(1, bx - ax)), cx = (l[0] + l[2]) / 2, A[6] = { k, 0, cx * (1 - k), 0, 1, 0 }; hud_img(im, nd, A, m, NULL, -1, 1); }
	mu_fld("nt.txt", txt, size, NULL, a); hud_dy = 0;
	if (a > 0.02f) hud_claim(480 - hw, y0 + d, 480 + hw, y1 + d, HUD_P_NOTICE);
	menu_end();
}
// portraits (InGameUI donut chaIcon frames = cm.face.N), checked against the footage by the skills work (tools/skills/SKILLS.md):
// 4 is Paz and 6 is Togusa (the mullet) - they were swapped here until b41
// b103 (user: "sitara's character portrait also seems to be mismatched with maven ... make sure that no operatives have mismatching
// character portraits"): the five later operatives re-checked against the game's own models (vm/preview_chr.py: each head, without
// its goggles, beside the frames) - 8 is Maven (the white hair), 9 Kuro, 15 Azuma, 16 Reiko (the red marks under the eyes), 17
// Sitara: the frames run in the order the operatives joined the game, with the commanders' faces (11 Aramaki, 12 the Operator, 13 the
// Tachikoma, 14 the terrorist leader) between Kuro and Azuma - as hud_match.h's HP ring frames do (8 Maven .. 12 Sitara, b87).
// Until b102: Kuro 8, Azuma 9, Reiko 12 (the Operator), Sitara 16, Maven 17.
enum { FACE_MOTOKO = 1, FACE_BATOU = 2, FACE_SAITO = 3, FACE_TOGUSA = 6, FACE_ISHIKAWA = 5, FACE_PAZ = 4, FACE_BORMA = 7, FACE_MAVEN = 8, FACE_KURUTSU = 9, FACE_ARAMAKI = 11, FACE_AZUMA = 15, FACE_REIKO = 16, FACE_SITARA = 17 };
static const int MENU_CHR_FACE[12] = { FACE_BATOU, FACE_MOTOKO, FACE_SAITO, FACE_TOGUSA, FACE_ISHIKAWA, FACE_BORMA, FACE_PAZ, FACE_AZUMA, FACE_KURUTSU, FACE_MAVEN, FACE_REIKO, FACE_SITARA };
// commander message (top left, tilted like the HUD; the pack builds it 1.45x the original size): the original's frame (portrait
// ring + name plate: the ovl pack's cm.frame/0), the portrait, the teal name, and ONE dark text panel sized to the text at body
// size (24 px; 1-2 lines, widened up to 390 px before a 3rd line is used). cm.frame/0 also carries the original's own text panel (image rows 150..215, the frame's 140 px
// width); since b41 it is not drawn - the legibility round had drawn a wider box over it and the original panel showed below it
// and double-darkened under it (b40 user report). Our panel is a nine-slice of that panel's own texels (top / bottom lines,
// fill), in screen space. A name wider than the plate (CHIEF ARAMAKI at 22 px) stretches the plate's fill in its own plane.
#define CM_COLS 170            // cm.frame/0: 170 x 217 texels; rows 128..147 name plate, 148..149 gap, 150..215 text panel
#define CM_ROWS 217
#define CM_PLATE 128
#define CM_CUT 149
static MGroup MCM; static int mcm_ok;
static void cm_local(const HImg *I, float c, float r, float *x, float *y) { const float *l = I->loc; *x = l[0] + c / CM_COLS * (l[2] - l[0]); *y = l[1] + r / CM_ROWS * (l[3] - l[1]); }
// texels (c0..c1, r0..r1) of cm.frame/0 drawn in its own (tilted) plane, at local x lx0..lx1 (the texels' own rows for y)
static void cm_plane(const HImg *I, int nd, float c0, float r0, float c1, float r1, float lx0, float lx1, const float *mul) {
	float a, y0, y1; cm_local(I, 0, r0, &a, &y0); cm_local(I, 0, r1, &a, &y1); float tw = (float)htw[I->tex], th = (float)hth[I->tex];
	float xy[4][2] = { { lx0, y0 }, { lx1, y0 }, { lx1, y1 }, { lx0, y1 } }, U0 = (I->x + c0) / tw, U1 = (I->x + c1) / tw, V0 = (I->y + r0) / th, V1 = (I->y + r1) / th;
	float uv[4][2] = { { U0, V0 }, { U1, V0 }, { U1, V1 }, { U0, V1 } }; hud_poly(htex[I->tex], nd, (const float (*)[2])xy, (const float (*)[2])uv, 4, mul);
}
// ui b42 (HUD de-clutter, user report b41: the box's bottom touched the skill gauge's "400"): the ring + plate drawn at CM_K of the
// pack's size about their top left corner (the ring's top stays clear of the minimap; the box comes up 8 px), 26 px lines; the
// widget claims its three parts (ring, plate, box: hud.h) so the centre notices and the Terminal Conquest letters keep clear
#define CM_K 0.86f
// b93 (user's PC screenshot: the portrait smaller and further into the corner): the face's centre lands here (screen px; the PC's
// ~(55, 195), lower: our minimap is bigger; user b93: more room between the minimap and the portrait - the block
// sits between the minimap and the skill gauge, nearer the minimap: ~33 px under it, b94 user), the plane scaled by CM_K about the point that keeps it there
#define CM_AT_X 64.0f
#define CM_AT_Y 236.0f
static void cm_node_scale(int nd, float k, float px, float py, float *save) {   // node nd's plane scaled by k about screen point (px, py)
	float *H = hnode[nd].H; memcpy(save, H, 9 * sizeof(float));
	for (int c = 0; c < 3; c++) { H[c] = k * H[c] + (1 - k) * px * H[6 + c]; H[3 + c] = k * H[3 + c] + (1 - k) * py * H[6 + c]; }
}
static void menu_commander_draw(int face, const char *name, const char *msg, float t) {
	HUD_TAG("commander");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0 || t > 6) return;
	menu_begin(menu_ovl); if (!mcm_ok) { mg_init(&MCM, "cm.frame"); mcm_ok = 1; }
	float a = mfade(t, 0.25f, 4.6f, 0.6f), m[4] = { 1, 1, 1, a };
	if (MCM.count < 1 || MCM.node[0] < 0 || himg[MCM.start].w != CM_COLS || himg[MCM.start].h != CM_ROWS) { mg_draw(&MCM, m); menu_end(); return; }   // (another pack layout)
	const HImg *I = &himg[MCM.start]; int nd = MCM.node[0]; if (!name) name = ""; if (!msg) msg = "";
	int fnd = MN("cm.face"); float sv0[9], sv1[9], ox, oy; { float lx, ly; cm_local(I, 0, 0, &lx, &ly); hud_node_screen(nd, lx, ly, &ox, &oy); }
	if (fnd >= 0) { float fx, fy; hud_node_screen(fnd, 79, 79, &fx, &fy); ox = (CM_AT_X - CM_K * fx) / (1 - CM_K); oy = (CM_AT_Y - CM_K * fy) / (1 - CM_K); }   // b93 (cm.face: 160 x 158 local, the face circle's centre at 79, 79)
	cm_node_scale(nd, CM_K, ox, oy, sv0); if (fnd >= 0 && fnd != nd) cm_node_scale(fnd, CM_K, ox, oy, sv1);
	// the name plate: inner width (cols 18..154) vs the name at 22 px; a longer name stretches the plate's fill (col 140) to the right
	float ns = 20, nw = mu_tw(name, ns) + 20, xl, yl, xr, yr, lx18, lx150, lx154, ly, E = 0, d;
	cm_local(I, 18, 137, &lx18, &ly); cm_local(I, 150, 137, &lx150, &d); cm_local(I, 154, 137, &lx154, &d);
	hud_node_screen(nd, lx18, ly, &xl, &yl); hud_node_screen(nd, lx154, ly, &xr, &yr);
	while (xr - xl < nw && E < 240) { E += 2; hud_node_screen(nd, lx154 + E, ly, &xr, &yr); }
	{ float x0, xe, y; cm_local(I, 0, 0, &x0, &y); cm_local(I, CM_COLS, 0, &xe, &y);
	  if (E <= 0) cm_plane(I, nd, 0, 0, CM_COLS, CM_CUT, x0, xe, m);   // ring + name plate, the original panel left out
	  else { cm_plane(I, nd, 0, 0, CM_COLS, CM_PLATE, x0, xe, m); cm_plane(I, nd, 0, CM_PLATE, 150, CM_CUT, x0, lx150, m);
		cm_plane(I, nd, 140.5f, CM_PLATE, 140.5f, CM_CUT, lx150, lx150 + E, m); cm_plane(I, nd, 150, CM_PLATE, CM_COLS, CM_CUT, lx150 + E, xe + E, m); } }
	char b[24]; snprintf(b, sizeof(b), "cm.face.%d", face); MD(b, "cm.face", NULL, m);
	{ static const float teal[4] = { 113 / 255.f, 244 / 255.f, 1, 1 }; float c[4] = { teal[0], teal[1], teal[2], a };
	  mu_text((xl + xr) * 0.5f, (yl + yr) * 0.5f + ns * MU_CAP * 0.5f, ns, 2, c, name); }
	// the text panel: just under the plate (the original's 2 px gap), at least the frame's width, 1 or 2 lines of 24 px
	{ float px0, py0, pxe, pye, lx, lyy; cm_local(I, 0, 150, &lx, &lyy); hud_node_screen(nd, lx, lyy, &px0, &py0); cm_local(I, CM_COLS, 150, &lx, &lyy); hud_node_screen(nd, lx, lyy, &pxe, &pye);   // where the original panel began
	  const float ts = 21, lh = 23, pad = 9; float ww = 260; if (mu_lines(msg, ww, ts) > 2) ww = 300; if (mu_lines(msg, ww, ts) > 2) ww = 340;   // (b93: 21 px, was 24 - the PC's box is small)
	  int n = mu_lines(msg, ww, ts); if (n < 1) n = 1; if (n > 3) n = 3;   // 2 lines keep the skill gauge's label (y ~370) clear; a 3rd only for a very long line
	  float bx0 = fmaxf(8, floorf(px0)), by0 = floorf(py0 + 0.5f), bw = fmaxf(pxe - px0, mu_wrap_maxw(msg, ww, ts, 3) + 2 * pad), by1 = by0 + 9 + ts * MU_CAP + (n - 1) * lh + 7, bx1 = bx0 + bw;
	  // nine-slice of the original panel (cm.frame/0 cols 0..3 | 3..167 | 167..170, rows 150..152 | 152..213 | 213..216; 0.82 px a texel)
	  const float k = 0.82f, cx[4] = { 0, 3, 167, CM_COLS }, ry[4] = { 150, 152, 213, 216 }, sx[4] = { bx0, bx0 + 3 * k, bx1 - 3 * k, bx1 }, sy[4] = { by0, by0 + 2 * k, by1 - 3 * k, by1 };
	  mu_a = a; for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) mu_img_part(MCM.start, cx[i], ry[j], cx[i + 1], ry[j + 1], sx[i], sy[j], sx[i + 1], sy[j + 1], NULL);
	  static const float wh[4] = { 1, 1, 1, 1 }; mu_wrap(bx0 + pad, by0 + 9, ww, ts, lh, 3, wh, msg); mu_a = 1;
	  // ui b42: claims - the ring (+ face), the plate (+ name), the box
	  { float rx, ry, rx1, ry1, lx, ly; cm_local(I, 0, 0, &lx, &ly); hud_node_screen(nd, lx, ly, &rx, &ry); cm_local(I, 150, CM_PLATE, &lx, &ly); hud_node_screen(nd, lx, ly, &rx1, &ry1);
	    if (a > 0.02f) { hud_claim(rx, ry, rx1, ry1, HUD_P_BANNER); hud_claim(fminf(rx, xl - 12), ry1, fmaxf(xr, rx1), py0, HUD_P_BANNER); hud_claim(bx0, by0, bx1, by1, HUD_P_BANNER); } } }
	memcpy(hnode[nd].H, sv0, sizeof(sv0)); if (fnd >= 0 && fnd != nd) memcpy(hnode[fnd].H, sv1, sizeof(sv1));
	menu_end();
}
// respawn screen: killer bar (killer name, kills on you, weapon, your kills on him, your name), countdown, tip
typedef struct { const char *killer, *you; int killer_kills, your_kills, weapon_icon; float respawn_in; const char *tip; } MenuRespawn;
static MGroup MRS, MRT; static int mrs_ok;
static void menu_respawn_draw(const MenuRespawn *r, float t) {
	HUD_TAG("respawn");   // ui b42 (overlap checker)
	if (menu_ovl < 0) return;
	menu_begin(menu_ovl); if (!mrs_ok) { mg_init(&MRS, "rs.bar"); mg_init(&MRT, "rt.frame"); mrs_ok = 1; }
	float a = msmooth(t / 0.4f), m[4] = { 1, 1, 1, a };
	mg_draw(&MRS, m);
	mu_fld("rs.enemy", r->killer ? r->killer : "", 24, NULL, a); mu_fld("rs.user", r->you ? r->you : "", 24, NULL, a);
	menu_digit_slots("rs.ep", 2, r->killer_kills, NULL); menu_digit_slots("rs.up", 2, r->your_kills, NULL);
	if (r->weapon_icon > 0) { char b[16]; snprintf(b, sizeof(b), "wp.%d", r->weapon_icon); float wm[4] = { 0, 0, 0, a }, wa[4] = { 1, 1, 1, 0 }; hud_img(MI(b), MN("rs.wp"), NULL, wm, wa, -1, 1); }
	if (r->tip) { float x0, y0, x1, y1; if (mu_field_box("rs.tip", &x0, &y0, &x1, &y1) >= 0) { float c[4] = { 1, 0.85f, 0.2f, 1 }; mu_a = a; mu_textbox(40, y0 - 8, 920, y1 + 8, MU_TEXT, 2, 0, c, r->tip); mu_a = 1; } }
	if (r->respawn_in > 0) {
		mg_draw(&MRT, m); mu_fld("rt.txt", "RESPAWN TIMER", MU_TEXT, NULL, a);
		int cs = (int)(r->respawn_in * 100); if (cs < 0) cs = 0; int sec = cs / 100, hs = cs % 100; char b[16]; if (sec > 99) sec = 99;
		int g0 = MI("n30.0");
		snprintf(b, sizeof(b), "%02d", sec); hud_img(g0 + b[1] - '0', MN("rt.s1"), NULL, NULL, NULL, -1, 1); hud_img(g0 + b[0] - '0', MN("rt.s2"), NULL, NULL, NULL, -1, 1);
		snprintf(b, sizeof(b), "%02d", hs); hud_img(g0 + b[1] - '0', MN("rt.ms1"), NULL, NULL, NULL, -1, 1); hud_img(g0 + b[0] - '0', MN("rt.ms2"), NULL, NULL, NULL, -1, 1);
	}
	menu_end();
}
// end of match: VICTORY / DEFEAT / DRAW band, emblem, team scores (result 0 win, 1 loss, 2 draw)
static void menu_endsplash_draw(int result, int score_l, int score_r, float t) {
	HUD_TAG("endsplash");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0) return;
	menu_begin(menu_ovl);
	static const char *tt[3] = { "VICTORY", "DEFEAT", "DRAW" }, *em[3] = { "es.emb.victory", "es.emb.defeat", "es.emb.draw" };
	static const float tc[3][4] = { { 0.40f, 0.88f, 1, 1 }, { 1, 0.36f, 0.36f, 1 }, { 0.85f, 0.85f, 0.85f, 1 } };
	if (result < 0 || result > 2) result = 2;
	float a = msmooth(t / 0.35f), m[4] = { 1, 1, 1, a }, c[4] = { tc[result][0], tc[result][1], tc[result][2], a };
	{ int im = MI("es.band"), nd = MN("es.band"); if (im >= 0 && nd >= 0) { const float *l = himg[im].loc; float k = 0.94f, cx = (l[0] + l[2]) / 2, A[6] = { k, 0, cx * (1 - k), 0, 1, 0 };
		hud_img(im, nd, A, m, NULL, -1, 1); } }   // ui b42: 94 % wide (its left end reached the minimap)
	if (a > 0.02f) hud_claim(192, 130, 768, 336, HUD_P_END);   // ui b42 (hud.h claims)
	{ int fi = MF("es.title"); if (fi >= 0) hud_field(fi, tt[result], c); }
	float s = 1.25f - 0.25f * msmooth((t - 0.15f) / 0.3f), ea = msmooth((t - 0.15f) / 0.3f), m2[4] = { 1, 1, 1, ea };
	{ int im = MI(em[result]); if (im >= 0) { const float *l = himg[im].loc; float cx = (l[0] + l[2]) / 2, cy = (l[1] + l[3]) / 2; float A[6] = { s, 0, cx * (1 - s), 0, s, cy * (1 - s) }; hud_img(im, MN("es.emb"), A, m2, NULL, -1, 1); } }
	float wl[4] = { 1, 1, 1, ea }, rl[4] = { 1, 0.42f, 0.42f, ea };
	menu_digits(MN("es.left"), score_l, 1, wl); menu_digits(MN("es.right"), score_r, 0, rl);
	menu_end();
}
// timer warning (last minute)
static MGroup MTW; static int mtw_ok;
static void menu_timewarn_draw(float t) {
	HUD_TAG("timewarn");   // ui b42 (overlap checker)
	if (menu_ovl < 0 || t < 0 || t > 3) return;
	menu_begin(menu_ovl); if (!mtw_ok) { mg_init(&MTW, "tw"); mtw_ok = 1; }
	float a = mfade(t, 0.2f, 2.0f, 0.6f), m[4] = { 1, 1, 1, a };
	// ui b42: 6 px under the score panel (was 4: the alarm icon touched it); clear of a banner (hud.h claims), else hidden meanwhile
	float d = hud_place(216, 90, 750, 193, HUD_P_NOTICE, 88, 330); if (d >= HUD_HIDE) { menu_end(); return; }
	hud_dy = 6 + d; mg_draw(&MTW, m);
	mu_fld("tw.title", "1 MINUTE LEFT", MU_H2, NULL, a); mu_fld("tw.sub", "The match ends when the timer runs out.", MU_TEXT, NULL, a); hud_dy = 0;
	if (a > 0.02f) hud_claim(216, 90 + d, 750, 193 + d, HUD_P_NOTICE);
	menu_end();
}
// ------------------------------------------------------------------ buttons, rows and the confirmation band (shared by both packs)
enum { MU_BTN, MU_BTN_GO, MU_BTN_YES, MU_BTN_GOLD };   // plain (teal), primary (red, like DIVE), affirmative (lime, like the original's YES), gold (RECRUIT / CUSTOMIZE)
static void mu_button(float x0, float y0, float x1, float y1, const char *label, float size, int sel, int press, int style, int off) {
	static const float d0[4] = { 0.07f, 0.15f, 0.18f, 0.94f }, d1[4] = { 0.03f, 0.07f, 0.09f, 0.94f }, s0[4] = { 0.20f, 0.50f, 0.57f, 0.97f }, s1[4] = { 0.09f, 0.29f, 0.34f, 0.97f },
		p0[4] = { 0.40f, 0.80f, 0.86f, 1 }, p1[4] = { 0.20f, 0.55f, 0.62f, 1 }, r0[4] = { 0.80f, 0.16f, 0.13f, 0.97f }, r1[4] = { 0.45f, 0.05f, 0.04f, 0.97f },
		rs0[4] = { 0.98f, 0.30f, 0.24f, 1 }, rs1[4] = { 0.62f, 0.10f, 0.08f, 1 }, y0c[4] = { 0.74f, 0.90f, 0.30f, 0.97f }, y1c[4] = { 0.48f, 0.66f, 0.12f, 0.97f },
		go_edge[4] = { 1, 0.55f, 0.5f, 1 };   // perf (b37, host ASan): was a compound literal inside the if below, used after its block ended
	const float *top = d0, *bot = d1, *edge = MU_CYAND, *tc = MU_WHITE; float k = fminf(12, (y1 - y0) * 0.25f);
	if (style == MU_BTN_GO) { top = sel || press ? rs0 : r0; bot = sel || press ? rs1 : r1; edge = sel ? MU_WHITE : go_edge; }
	else if (style == MU_BTN_YES && sel) { top = y0c; bot = y1c; edge = MU_WHITE; tc = MU_DARK; }
	else if (style == MU_BTN_GOLD && sel) { static const float g0[4] = { 1.0f, 0.80f, 0.12f, 0.98f }, g1[4] = { 0.85f, 0.58f, 0.0f, 0.98f }, gp[4] = { 1, 0.9f, 0.5f, 1 };
		top = press ? gp : g0; bot = g1; edge = MU_WHITE; tc = MU_DARK; }
	else if (press) { top = p0; bot = p1; edge = MU_WHITE; tc = MU_DARK; }
	else if (sel) { top = s0; bot = s1; edge = MU_CYAN; }
	if (press && style != MU_BTN) { float b[4] = { 1, 1, 1, 0.25f }; mu_cut(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 0, k + 2, 0, k + 2, b); }
	mu_cutv(x0, y0, x1, y1, 0, k, 0, k, top, bot); mu_cutline(x0, y0, x1, y1, 0, k, 0, k, sel || press ? 2.0f : 1.5f, edge);
	if (sel && style == MU_BTN) mu_rect(x0 + 4, y0 + 6, x0 + 8, y1 - fmaxf(6, k - 1), MU_CYAN);   // (b95: clear of the bottom-left cut's outline)
	mu_textbox(x0, y0, x1, y1, size, 2, 14, off ? MU_OFF : tc, label);
}
static const char *mu_confirm_lbl[2] = { "YES", "NO" };   // the band's two buttons (the options ALERT uses CONFIRM / CANCEL)
// a full-width confirmation band (GameExitPopup's exit popup, enlarged): title, two lines, YES / NO; ids kind<<8 | 0 (yes), 1 (no)
static void mu_confirm_draw(const char *title, const char *l1, const char *l2, int csel, int kind, float a) {
	float dim[4] = { 0, 0, 0, 0.55f }; mu_a = a; mu_rect(0, 0, 960, 544, dim);
	static const float band[4] = { 0.03f, 0.08f, 0.10f, 0.97f }, t0[4] = { 0.12f, 0.27f, 0.31f, 0.98f }, t1[4] = { 0.06f, 0.15f, 0.18f, 0.98f };
	mu_rect(0, 148, 960, 396, band); mu_rect(0, 148, 960, 150, MU_CYAN); mu_rect(0, 394, 960, 396, MU_CYAND);
	mu_rectv(0, 150, 960, 204, t0, t1); mu_textbox(0, 150, 960, 204, MU_H2, 2, 0, MU_WHITE, title);
	if (l1) mu_textbox(40, 216, 920, 254, MU_BODY, 2, 0, MU_WHITE, l1);
	if (l2) mu_textbox(40, 250, 920, 284, MU_TEXT, 2, 0, MU_DIM, l2);
	const char *const *bl = mu_confirm_lbl;
	for (int k = 0; k < 2; k++) { float x0 = k ? 494 : 286, x1 = x0 + 180; int id = MU_ID(kind, k);
		mu_button(x0, 306, x1, 370, bl[k], MU_H2 * 0.9f, csel == k, mu_pressing(id), k == 0 ? MU_BTN_YES : MU_BTN, 0); mu_reg(id, x0 - 6, 300, x1 + 6, 376); }
	{ float c[4] = { 0.01f, 0.03f, 0.04f, 0.92f }; mu_rect(0, MU_HINT_BAR, 960, 544, c); } mu_hints("d:CHOOSE|x:CONFIRM|o:CANCEL", MU_HINT_Y);
	mu_a = 1;
}
// pressed / held masks from the caller: MB_* bits
enum { MB_UP = 1, MB_DOWN = 2, MB_LEFT = 4, MB_RIGHT = 8, MB_OK = 16, MB_BACK = 32, MB_START = 64, MB_L = 128, MB_R = 256, MB_TRI = 512, MB_SQR = 1024 };
// confirmation update: d-pad / Cross / Circle, or a tap on YES / NO. Returns 1 yes, 0 no, -1 still open
enum { MU_K_TOP = 1, MU_K_TAB, MU_K_ITEM, MU_K_BTN, MU_K_ROWL, MU_K_ROWR, MU_K_ROW, MU_K_SLIDER, MU_K_LIST, MU_K_CONFIRM, MU_K_NAV, MU_K_PAUSE, MU_K_RTAB, MU_K_BG };
static int mu_confirm_update(int *csel, unsigned p) {
	if (p & (MB_LEFT | MB_RIGHT | MB_UP | MB_DOWN)) *csel ^= 1;
	if (p & MB_BACK) return 0;
	if (p & MB_OK) return *csel == 0;
	if (MUT.press && MU_KIND(MUT.id) == MU_K_CONFIRM && *csel != MU_IX(MUT.id)) { *csel = MU_IX(MUT.id); MENU_SFX("ui/mouse_over_basic"); }
	for (int k = 0; k < 2; k++) if (mu_tapped(MU_ID(MU_K_CONFIRM, k))) { MENU_SFX(k ? "ui/window_close_basic" : "ui/click_basic"); return k == 0; }
	return -1;
}
// ------------------------------------------------------------------ pause menu (in-match ESC menu: GameExitPopup's right-hand panel, enlarged)
enum { PAUSE_NONE, PAUSE_RESUME, PAUSE_OPTIONS, PAUSE_QUIT };
static struct { int sel, confirm, csel; MGroup panel, xpanel; int ok; float t; } MPM;
static void menu_pause_open(void) { MPM.sel = 0; MPM.confirm = 0; MPM.csel = 1; MPM.t = 0; menu_touch_lock(); }
static const char *MENU_PAUSE_ITEM[3] = { "CONTINUE", "OPTIONS", "QUIT MATCH" };
// pressed: newly pressed buttons this frame (MB_*). Touch: tap an item (one tap acts), YES / NO on the quit band
static int menu_pause_update(unsigned pressed, float dt) {
	MPM.t += dt; mu_in_begin(dt); int r = PAUSE_NONE;
	if (MPM.confirm) {
		int c = mu_confirm_update(&MPM.csel, pressed);
		if (c == 1) r = PAUSE_QUIT; else if (c == 0) { MPM.confirm = 0; menu_touch_lock(); }
	} else {
		if (pressed & MB_UP) MPM.sel = (MPM.sel + 2) % 3;
		if (pressed & MB_DOWN) MPM.sel = (MPM.sel + 1) % 3;
		int act = -1;
		if (pressed & (MB_BACK | MB_START)) r = PAUSE_RESUME;
		if (pressed & MB_OK) act = MPM.sel;
		if (MUT.press && MU_KIND(MUT.id) == MU_K_PAUSE && MU_IX(MUT.id) != MPM.sel) { MPM.sel = MU_IX(MUT.id); MENU_SFX("ui/mouse_over_basic"); }
		for (int i = 0; i < 3; i++) if (mu_tapped(MU_ID(MU_K_PAUSE, i))) { act = i; MENU_SFX("ui/click_basic"); }
		if (mu_tapped(MU_ID(MU_K_BTN, 0))) { r = PAUSE_RESUME; MENU_SFX("ui/click_basic"); }
		if (act == 0) r = PAUSE_RESUME; else if (act == 1) r = PAUSE_OPTIONS; else if (act == 2) { MPM.confirm = 1; MPM.csel = 1; menu_touch_lock(); MENU_SFX("ui/window_open_basic"); }
	}
	mu_in_end(); return r;
}
static void menu_pause_draw(void) {
	HUD_TAG("#pause");   // ui b42 (overlap checker)
	if (menu_ovl < 0) return;
	mu_draw_begin(); menu_begin(menu_ovl);
	float a = msmooth(MPM.t / 0.2f), dim[4] = { 0, 0, 0, 0.40f }; mu_rect(0, 0, 960, 544, dim);
	static const float p0[4] = { 0.02f, 0.06f, 0.07f, 0.95f }, p1[4] = { 0.04f, 0.11f, 0.13f, 0.95f };
	mu_a = a; hud_dx = 80 * (1 - a);
	mu_recth(590, 0, 960, 544, p0, p1); mu_rect(590, 0, 592, 544, MU_CYAND);
	mu_text(624, 98, MU_H1, 0, MU_WHITE, "MENU"); mu_rect(624, 114, 940, 116, MU_CYAND);
	mu_noreg = MPM.confirm;   // the quit band is modal: only its YES / NO take taps
	for (int i = 0; i < 3; i++) { float y0 = 136 + i * 84; int id = MU_ID(MU_K_PAUSE, i);   // 72 px buttons, 12 px apart
		if (i == 2) y0 = 392;   // QUIT MATCH sits apart, as in the original (b55: 16 px higher, room for the hints on two lines)
		mu_button(608, y0, 944, y0 + 72, MENU_PAUSE_ITEM[i], MU_H2 * 0.9f, i == MPM.sel, mu_pressing(id), MU_BTN, 0); mu_reg(id, 608, y0, 944, y0 + 72); }
	mu_hints_fit("d:MOVE|x:SELECT|o:RESUME", MU_HINT_Y, 604);   // b55: inside the panel (x 590..960)
	hud_dx = 0; mu_noreg = 0;
	if (MPM.confirm) mu_confirm_draw("QUIT MATCH", "Leave this match and return to the lobby?", "Your progress in this match will be lost.", MPM.csel, MU_K_CONFIRM, 1);
	else mu_reg(MU_ID(MU_K_BTN, 0), 0, 0, 578, 544);   // a tap on the game (left of the panel) resumes
	mu_a = 1; menu_end();
}
// ------------------------------------------------------------------ maps
#define MM_TDM (1 << MODE_TDM)
#define MM_DEM (1 << MODE_DEM)
#define MM_TTM (1 << MODE_TTM)
#define MM_EM (1 << MODE_EM)
#define MM_GA (1 << MODE_GA)   // b63: Ghost Assault on every match map (its spawns: Elimination's where the map has them, else the TDM bases)
typedef struct { const char *stem, *name; int modes; } MenuMap;   // in the engine's MAPS[] order (main.c); modes 0 = training map
// names: the original's (scoreboard / title of the footage) where known. Modes beyond TDM: Demolition as the demolition work
// set them up (.gdem sites); Terminal Conquest on the maps with TTM minimaps in the Western UI data (TTM01bg/TTM02bg/TTM03bg/
// TTM_002; the footage plays it on Underground Base); Elimination on the maps with EM minimaps (EM_urbancity, EM_Port, EM_01 =
// Ghost Storage's layout; the footage plays it on Urban City).
static const MenuMap MENU_MAP[17] = {
	{ "downtown", "DOWNTOWN", MM_TDM | MM_DEM | MM_GA }, { "berutalube", "BERUTALUBE", MM_TDM | MM_TTM | MM_GA }, { "ecenter", "ELECTRONICS CENTER", MM_TDM | MM_DEM | MM_GA },
	{ "ghost_storage", "GHOST STORAGE", MM_TDM | MM_EM | MM_GA }, { "headquarter", "HEADQUARTERS", MM_TDM | MM_DEM | MM_GA }, { "port", "PORT", MM_TDM | MM_EM | MM_GA },
	{ "pss9", "PSS9", MM_TDM | MM_DEM | MM_GA }, { "ruinednight", "NEON RUINS", MM_TDM | MM_GA }, { "ruinedtokyo", "RUINED TOKYO", MM_TDM | MM_TTM | MM_GA },
	{ "trainstation", "STATION", MM_TDM | MM_DEM | MM_GA }, { "underground", "UNDERGROUND BASE", MM_TDM | MM_TTM | MM_GA }, { "urban", "URBAN CITY", MM_TDM | MM_EM | MM_GA },
	{ "zioplant", "GEOFRONT", MM_TDM | MM_DEM | MM_GA }, { "shooting", "SHOOTING RANGE", 0 }, { "tutorial", "TUTORIAL", 0 },
	{ "tutorial2", "TUTORIAL 2", 0 }, { "tutorial3", "TUTORIAL 3", 0 },
};
#define MENU_NMAPS 17
#define MENU_NPLAY 13            // maps 0..12 are match maps
#define MENU_MAP_SHOOTING 13
#define MENU_MAP_TUTORIAL 14
static __attribute__((unused)) int menu_tut_of_map(const char *stem) { for (int k = 0; k < 4; k++) if (!strcmp(MENU_MAP[MENU_TUT[k].map].stem, stem)) return k + 1; return 0; }
static int menu_map_ok(int mode, int map) { return map >= 0 && map < MENU_NPLAY && ((MENU_MAP[map].modes >> mode) & 1); }
static int menu_maps_of(int mode, int *out) { int n = 0; for (int i = 0; i < MENU_NPLAY; i++) if (menu_map_ok(mode, i)) { if (out) out[n] = i; n++; } return n; }
// weapons offered in the loadout (icon = WpIconMC frame); slot 0 primary, 1 secondary. The caller maps them to its models.
// b89 (the user's "gits armory" videos): the v2.0 armory panel of each gun - its name, class, DAMAGE / ACCURACY / STABILITY /
// FIRE RATE (the numbers printed beside the bars), RANGE in metres, magazine / reserve - the same numbers weapons.txt plays
// with (vm/make_weapons_txt.py V2; tools/audit checks the two agree). desc: the port's own one-line summary.
typedef struct { const char *name, *cls; int icon, slot; float dmg, acc, stab, rate, rng; int mag, res; const char *sh, *vm, *desc; } MenuWpn;   // sh: weapons.txt short name (vm_<sh>.gvm); b82 vm: another first-person model / weapons.txt row of the same gun (a sight fitted; NULL = sh), sh stays its third-person / bot / sound name
static const MenuWpn MENU_WPN[] = {
	{ "M4A1", "ASSAULT RIFLE", 2, 0, 35, 69, 45, 69, 20, 30, 90, "m4a1", NULL, "A dependable all-round carbine for any range." },
	{ "SC-AR 45", "ASSAULT RIFLE", 18, 0, 40, 73, 45, 62, 24, 30, 90, "scar", NULL, "A hard-hitting rifle with a 2x combat sight." },
	{ "ACR1-G2", "ASSAULT RIFLE", 38, 0, 34, 70, 52, 63, 22, 30, 90, "acr", NULL, "A modular rifle with steady recoil and a red dot." },
	{ "MP-V5A", "SUBMACHINE GUN", 1, 0, 28, 48, 60, 83, 15, 30, 90, "mp5", NULL, "A compact SMG with accurate first shots." },
	{ "KRIS-SV", "SUBMACHINE GUN", 4, 0, 25, 53, 62, 88, 12, 28, 84, "kriss", NULL, "A fast-firing SMG with very little kick." },
	{ "L118A-S1b", "SNIPER RIFLE", 21, 0, 156, 100, 15, 4, 42, 5, 15, "l118", NULL, "A bolt-action rifle with an 8x scope." },
	{ "870-ICS", "SHOTGUN", 5, 0, 84, 35, 33, 6, 8, 8, 24, "870", NULL, "A pump-action shotgun, devastating up close." },
	{ "M249-G4", "LIGHT MACHINE GUN", 27, 0, 43, 30, 35, 75, 25, 100, 200, "m249", NULL, "A belt-fed machine gun with a 100-round box." },
	{ "SEBURO M5", "PISTOL", 31, 1, 25, 51, 26, 60, 10, 20, 60, "seburom5", NULL, "Section 9's standard-issue 9 mm sidearm." },
	{ "MATEBA 2006SM", "PISTOL", 32, 1, 48, 38, 25, 21, 15, 6, 18, "mateba", NULL, "A six-shot revolver with heavy stopping power." },
	// b82 (the user's Batou footage: his M4A1 carries the UH3 tube red dot): appended, so saved loadout indices keep their guns
	{ "M4A1 RED DOT", "ASSAULT RIFLE", 2, 0, 35, 69, 45, 69, 20, 30, 90, "m4a1", "m4a1rd", "The M4A1 with a tube red-dot sight fitted." },
	// b89: the rifles of the user's AR video (iron sights, as the armory shows them with no sight fitted), appended
	{ "AK47", "ASSAULT RIFLE", 15, 0, 41, 78, 39, 58, 25, 30, 90, "ak47", NULL, "Heavy rounds and a strong kick: fire in bursts." },
	{ "K22", "ASSAULT RIFLE", 14, 0, 36, 76, 47, 68, 22, 30, 90, "k2", NULL, "A quick, precise rifle for fast follow-up shots." },
	{ "KHX-160", "ASSAULT RIFLE", 79, 0, 38, 65, 55, 75, 22, 30, 90, "arx160", NULL, "A light rifle with the steadiest recoil of its class." },
	{ "KH-417", "ASSAULT RIFLE", 39, 0, 40, 76, 42, 60, 24, 30, 90, "hk417", NULL, "A heavy-calibre rifle that rewards careful aim." },
	{ "F2020", "ASSAULT RIFLE", 13, 0, 34, 74, 40, 80, 24, 30, 90, "f2000", NULL, "A long-barrelled bullpup with a high rate of fire." },
	{ "F-MAS G1", "ASSAULT RIFLE", 17, 0, 33, 64, 50, 88, 20, 25, 75, "famas", NULL, "A very fast-firing bullpup, deadliest up close." },
	{ "S25-R", "ASSAULT RIFLE", 40, 0, 33, 66, 51, 85, 22, 30, 90, "x95r", NULL, "A compact bullpup with stable, rapid fire." },
	{ "SEBURO C-30", "ASSAULT RIFLE", 46, 0, 37, 67, 50, 71, 23, 30, 90, "c30", NULL, "Section 9's sturdy bullpup with a long barrel." },
	// b90: the SMGs of the user's SMG video, and the PP-2200 (v2.0: a PERSONAL DEFENSE WEAPON - the Specialists' class), appended
	{ "SEBURO C-26A", "SUBMACHINE GUN", 57, 0, 26, 54, 62, 85, 15, 30, 90, "c26a", NULL, "Section 9's compact bullpup for close assaults." },
	{ "DEATHSTALKER EVO", "SUBMACHINE GUN", 30, 0, 29, 56, 61, 88, 18, 30, 90, "evo", NULL, "A well-rounded SMG that holds up in most fights." },
	{ "KA-P5W", "SUBMACHINE GUN", 29, 0, 35, 57, 58, 70, 20, 30, 90, "kacpdw", NULL, "Heavier rounds and a longer reach than most SMGs." },
	{ "MP5K-S3", "SUBMACHINE GUN", 37, 0, 26, 57, 61, 85, 15, 30, 90, "mp5k", NULL, "A short SMG made for tight corners." },
	{ "SEO MPX", "SUBMACHINE GUN", 28, 0, 30, 53, 60, 83, 15, 30, 90, "mpx", NULL, "Fast fire that makes up for light rounds." },
	{ "P-90S1", "SUBMACHINE GUN", 6, 0, 26, 45, 62, 88, 15, 50, 150, "p90", NULL, "A 50-round bullpup SMG for close range." },
	{ "PP-2200", "PERSONAL DEFENSE WEAPON", 71, 0, 27, 54, 63, 85, 12, 20, 60, "pp2000", NULL, "A stable, accurate compact weapon for self-defence." },
	{ "UMP-45S1", "SUBMACHINE GUN", 8, 0, 33, 62, 59, 60, 20, 25, 75, "ump45", NULL, "A slower SMG that hits harder at close range." },
	// b91: the sniper rifles of the user's SR video, and the WS2088 (the beta's WA2000, its 2015 panel: ACCURACY on that older scale), appended
	{ "CM2010", "SNIPER RIFLE", 53, 0, 180, 100, 24, 3, 45, 5, 15, "cm2010", NULL, "A precise bolt-action rifle with a bipod." },
	{ "MM200", "SNIPER RIFLE", 24, 0, 176, 100, 26, 3, 38, 5, 15, "mm200", NULL, "A bolt-action rifle with a steadier kick." },
	{ "M99D", "SNIPER RIFLE", 23, 0, 184, 100, 17, 3, 45, 10, 30, "m99d", NULL, "The hardest hitter, with a 10-round magazine." },
	{ "SA50", "SNIPER RIFLE", 50, 0, 135, 85, 21, 7, 43, 5, 15, "sa50", NULL, "A semi-auto .50 rifle for quick follow-up shots." },
	{ "SEBURO SR", "SNIPER RIFLE", 59, 0, 178, 100, 21, 3, 48, 5, 15, "seburosr", NULL, "Section 9's bolt-action rifle with the longest reach." },
	{ "SEO90", "SNIPER RIFLE", 78, 0, 79, 58, 29, 33, 40, 10, 30, "seo90", NULL, "A fast semi-auto rifle for mid-range fights." },
	{ "SR-V97", "SNIPER RIFLE", 25, 0, 154, 100, 36, 4, 40, 10, 30, "srv97", NULL, "The steadiest bolt-action rifle, 10 rounds." },
	{ "WS2088", "SNIPER RIFLE", 77, 0, 85, 113, 31, 12, 40, 6, 18, "ws2088", NULL, "A semi-auto bullpup cut after the 2015 beta." },
	// b92: the pistols of the user's handgun video (+ the MATEBA 2008M, removed after the beta: the 2006SM's numbers) and the LMGs
	{ "44 MAGNUM", "PISTOL", 33, 1, 51, 44, 23, 20, 16, 6, 18, "mag44", NULL, "A long-barrel revolver that hits the hardest." },
	{ "93-SE", "PISTOL", 34, 1, 25, 38, 32, 85, 8, 20, 60, "b93se", NULL, "Fires a three-round burst with each pull." },
	{ "KCZ-85", "PISTOL", 70, 1, 27, 53, 24, 50, 12, 17, 51, "kcz85", NULL, "An accurate pistol with a 17-round magazine." },
	{ "SN 5-7", "PISTOL", 75, 1, 25, 47, 27, 60, 10, 20, 60, "sn57", NULL, "A light pistol with a fast rate of fire." },
	{ "SEO-P226", "PISTOL", 49, 1, 28, 47, 25, 50, 12, 12, 36, "p226", NULL, "A dependable pistol with solid stopping power." },
	{ "MATEBA 2008M", "PISTOL", 73, 1, 48, 38, 25, 21, 15, 6, 18, "m2008", NULL, "A revolver cut after the 2015 beta." },
	{ "L86-SEO", "LIGHT MACHINE GUN", 56, 0, 42, 40, 36, 66, 25, 50, 150, "l86", NULL, "A steady bullpup machine gun with a drum." },
	{ "MG33-A", "LIGHT MACHINE GUN", 81, 0, 42, 30, 31, 85, 25, 100, 200, "mg33", NULL, "The fastest-firing belt-fed machine gun." },
	{ "MG36-A", "LIGHT MACHINE GUN", 48, 0, 45, 31, 35, 73, 28, 50, 150, "mg36", NULL, "A light machine gun with a drum magazine." },
	{ "SEBURO MG", "LIGHT MACHINE GUN", 69, 0, 46, 36, 36, 68, 28, 50, 150, "seburomg", NULL, "Section 9's machine gun, accurate for its class." },
	// b97: the shotguns of the user's shotgun video (KSG-G1, STA-12S2 pump and load shell by shell; USS 12 semi-automatic, box magazine)
	{ "KSG-G1", "SHOTGUN", 47, 0, 88, 48, 27, 7, 12, 14, 42, "ksg", NULL, "A bullpup pump shotgun with a longer reach." },
	{ "STA-12S2", "SHOTGUN", 76, 0, 107, 38, 25, 7, 12, 15, 45, "sta12", NULL, "A dual-tube pump shotgun that hits the hardest." },
	{ "USS 12", "SHOTGUN", 45, 0, 58, 35, 31, 20, 7, 6, 24, "uss12", NULL, "A semi-automatic shotgun for close quarters." },
	// b98: the melee weapons (slot 2; the user's equipment video: DAMAGE, FIRE RATE, RANGE - no accuracy, stability or ammunition)
	{ "MEGATECH KNIFE", "MELEE", 51, 2, 80, 0, 0, 45, 1.6f, 0, 0, "knife", NULL, "A forearm-mounted combat blade." },
	{ "DOUBLE KATAR", "MELEE", 55, 2, 90, 0, 0, 55, 1.5f, 0, 0, "axe", NULL, "Twin folding blades: faster, a shorter reach." },
};
#define MENU_NWPN ((int)(sizeof(MENU_WPN) / sizeof(MENU_WPN[0])))
#define MENU_WPN_MAX 64   // (b89: list buffers; MENU_NWPN stays below it; b91: 48; b92: 64)
_Static_assert(MENU_NWPN <= MENU_WPN_MAX, "MENU_WPN_MAX");
// the armory's order: the default M4A1 and its red-dot version first, then each class A-Z as the user's armory videos list them
// (b121: the PP-2200 in its A-Z place among the submachine guns, where the PC's armory lists it - menu_fe.h fe_wpn_shown)
static const int MENU_WPN_ORDER[] = { 0, 10, 2, 11, 15, 16, 12, 14, 13, 17, 1, 18, 20, 21, 4, 3, 22, 24, 25, 19, 23, 26, 27, 5, 29, 28, 30, 31, 32, 33, 34, 45, 46, 47, 6,
	41, 7, 42, 43, 44, 35, 36, 37, 9, 8, 39, 38, 40, 48, 49 };   // (b92: the LMGs and pistols in the videos' order; b97: the shotguns, the 870-ICS last as the video)
// b88 (v2.0, the user's "Menu Navigation Ver. 2.0" video 1:52-2:45): each operator's class limits the primary weapon -
// ASSAULT (Batou, Ishikawa, Borma, Azuma): assault rifles, machine guns; INFILTRATOR (Motoko, Paz, Maven, Kuro, Reiko):
// submachine guns, shotguns; SPECIALIST (Saito, Togusa, Sitara): sniper rifles, PDWs (none in the port yet). "ALL CLASSES
// are able to use every sidearm, melee, & throwing weapon." The class defaults: Batou M4A1, Motoko MP-V5A, Saito L118A-S1b.
static const unsigned char MENU_CHR_CLASS[12] = { 0, 1, 2, 2, 0, 0, 1, 0, 1, 1, 1, 2 };   // MENU_CHR order: 0 assault, 1 infiltrator, 2 specialist
static int menu_chr_class(int chr) { return chr >= 0 && chr < 12 ? MENU_CHR_CLASS[chr] : 0; }
static int menu_wpn_class_ok(int chr, int w) {
	if (w < 0 || w >= MENU_NWPN) return 0; const MenuWpn *W = &MENU_WPN[w]; if (W->slot != 0) return 1;
	const char *c = W->cls; int k = menu_chr_class(chr);
	if (k == 0) return !strcmp(c, "ASSAULT RIFLE") || !strcmp(c, "LIGHT MACHINE GUN");
	if (k == 1) return !strcmp(c, "SUBMACHINE GUN") || !strcmp(c, "SHOTGUN");
	return !strcmp(c, "SNIPER RIFLE") || !strcmp(c, "PERSONAL DEFENSE WEAPON");
}
static int menu_class_default(int chr) { int k = menu_chr_class(chr); return k == 0 ? 0 : k == 1 ? 3 : 5; }   // M4A1, MP-V5A, L118A-S1b   // b82: the armory's order (the red-dot M4A1 next to the M4A1)
_Static_assert(sizeof(MENU_WPN_ORDER) / sizeof(MENU_WPN_ORDER[0]) == MENU_NWPN, "MENU_WPN_ORDER lists every MENU_WPN entry");
static const char *menu_wpn_vm(int i) { return i >= 0 && i < MENU_NWPN ? (MENU_WPN[i].vm ? MENU_WPN[i].vm : MENU_WPN[i].sh) : "m4a1"; }   // b82: the first-person model / weapons.txt row
// ------------------------------------------------------------------ front-end screens (menu pack)
#include "menu_fe.h"
// ------------------------------------------------------------------ after the match (menu pack): TOP OPERATIVE, then the
// result screen with three tabs (L / R or a tap): RESULTS (battle rewards, level bar), PROGRESS, STATISTICS (both teams)
typedef struct { char name[24]; int team, chr, kills, deaths, assists, score, me; char wpn[12]; int obj, skill; } MenuPlayer;   // team 0 = yours; wpn (ui b50): the gun used (weapon.h short name); b58: obj = bombs planted / defused, terminals captured; skill = skills used (Tier 2 = 2)
typedef struct {
	int result;                 // 0 victory, 1 defeat, 2 draw
	int score[2];               // your team, enemy team
	int mode, map; float play_time;   // seconds
	int gp[3], xp[3];           // combat, skill, objective
	int level, exp, exp_next;   // after the match
	int nplayers; MenuPlayer p[12];
	// b58: filled by menu_career (after the players): the awards (player index or -1: CONTROLLER, DESTROYER, TECHNICIAN),
	// this match's missions, the achievements earned now and the career for the PROGRESS tab
	int award[3], award_kd;     // award_kd: CONTROLLER went to the best K/D (a mode without objectives)
	char mis_text[3][40]; int mis_have[3], mis_need[3], mission_n;
	int ach_new[4], nach_new; unsigned ach; int c_kills; unsigned c_maps, c_modes;
} MenuResults;
// ---- b58: awards, missions and achievements (footage: the statistics tab's CONTROLLER / DESTROYER / TECHNICIAN; MISSION +1 /
// ACHIEVEMENT +1 beside the level). The original's missions and achievements came from its servers; these are the offline
// version's own: three missions a match, eleven achievements kept in the career (MenuConfig).
static const char *MENU_AWARD[3] = { "CONTROLLER", "DESTROYER", "TECHNICIAN" };
static const char *MENU_AWARD_IMG[3] = { "aw.ctrl", "aw.dest", "aw.tech" };
typedef struct { const char *name, *desc; } MenuAch;
static const MenuAch MENU_ACH[] = {
	{ "FIRST VICTORY", "Win a match" }, { "TOP OPERATIVE", "Top score, 2 or more a side" },   // (descriptions fit 300 px at fine print)
	{ "DESTROYER", "Earn the Destroyer award" }, { "CONTROLLER", "Earn the Controller award" }, { "TECHNICIAN", "Earn the Technician award" },
	{ "FLAWLESS", "Win: 5+ kills, no deaths" }, { "VETERAN", "Win against Veteran bots" },
	{ "CENTURION", "Defeat 100 enemies" }, { "ALL MODES", "Win in every game mode" }, { "WORLD TOUR", "Play on every battlefield" },
	{ "LEVEL 10", "Reach level 10" },
};
#define MENU_NACH ((int)(sizeof(MENU_ACH) / sizeof(MENU_ACH[0])))
#define MENU_MODES_ALL ((1u << MODE_TDM) | (1u << MODE_DEM) | (1u << MODE_TTM) | (1u << MODE_EM))
static int mbits(unsigned v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }
// an achievement's progress for the PROGRESS tab ("37 / 100"); 0 = not counted
static int menu_ach_prog(int k, const MenuResults *r, int level, int *have) {
	switch (k) { case 7: *have = r->c_kills; return 100; case 8: *have = mbits(r->c_modes & MENU_MODES_ALL); return 4;
	case 9: *have = mbits(r->c_maps & ((1u << MENU_NPLAY) - 1)); return MENU_NPLAY; case 10: *have = level; return 10; default: return 0; }
}
// the award winners: CONTROLLER = most objectives (bombs planted / defused, terminals captured; in a mode without objectives the
// best K/D with 3 kills or more), DESTROYER = most kills, TECHNICIAN = most skills used; ties go to the higher score
static void menu_awards(MenuResults *r) {
	int objm = r->mode == MODE_DEM || r->mode == MODE_TTM; r->award_kd = !objm;
	for (int k = 0; k < 3; k++) {
		int best = -1; float bv = 0;
		for (int i = 0; i < r->nplayers && i < 12; i++) { const MenuPlayer *P = &r->p[i]; float v;
			if (k == 0) v = objm ? (float)P->obj : P->kills >= 3 ? (float)P->kills / (P->deaths > 0 ? P->deaths : 1) : 0;
			else v = (float)(k == 1 ? P->kills : P->skill);
			if (v <= 0) continue;
			if (best < 0 || v > bv || (v == bv && P->score > r->p[best].score)) { best = i; bv = v; } }
		r->award[k] = best; }
}
// at the end of a match (after the players and the level): awards, missions, the career and the achievements it completes
static void menu_career(MenuConfig *c, MenuResults *r) {
	menu_awards(r);
	int me = -1; for (int i = 0; i < r->nplayers && i < 12; i++) if (r->p[i].me) me = i;
	const MenuPlayer *P = me >= 0 ? &r->p[me] : NULL; int win = r->result == 0, kills = P ? P->kills : 0, deaths = P ? P->deaths : 0;
	int rounds = r->mode == MODE_DEM || r->mode == MODE_EM || r->mode == MODE_GA, kn = rounds ? 5 : 10;
	snprintf(r->mis_text[0], 40, "Win the match"); r->mis_have[0] = win; r->mis_need[0] = 1;
	snprintf(r->mis_text[1], 40, "Defeat %d enemies", kn); r->mis_have[1] = kills; r->mis_need[1] = kn;
	if (r->mode == MODE_DEM) { snprintf(r->mis_text[2], 40, "Plant or defuse the bomb"); r->mis_have[2] = P ? P->obj : 0; r->mis_need[2] = 1; }
	else if (r->mode == MODE_TTM) { snprintf(r->mis_text[2], 40, "Capture a terminal"); r->mis_have[2] = P ? P->obj : 0; r->mis_need[2] = 1; }
	else { snprintf(r->mis_text[2], 40, "Use cyberbrain skills 3 times"); r->mis_have[2] = P ? P->skill : 0; r->mis_need[2] = 3; }
	r->mission_n = 0; for (int k = 0; k < 3; k++) r->mission_n += r->mis_have[k] >= r->mis_need[k];
	c->c_matches++; c->c_kills += kills; if (win) { c->c_wins++; if (r->mode >= 0 && r->mode < 32) c->c_modes |= 1u << r->mode; }
	if (r->map >= 0 && r->map < MENU_NPLAY) c->c_maps |= 1u << r->map;
	int top = -1, nteam = 0; for (int i = 0; i < r->nplayers && i < 12; i++) if (r->p[i].team == 0) { nteam++; if (top < 0 || r->p[i].score > r->p[top].score) top = i; }
	unsigned got = 0;
	if (win) got |= 1u << 0;
	if (me >= 0 && top == me && nteam >= 2 && r->p[me].score > 0) got |= 1u << 1;   // (a match quit at 0 - 0 has no top operative)
	if (me >= 0 && r->award[1] == me) got |= 1u << 2;
	if (me >= 0 && r->award[0] == me) got |= 1u << 3;
	if (me >= 0 && r->award[2] == me) got |= 1u << 4;
	if (win && deaths == 0 && kills >= 5) got |= 1u << 5;
	if (win && c->bot_skill >= 3) got |= 1u << 6;
	if (c->c_kills >= 100) got |= 1u << 7;
	if ((c->c_modes & MENU_MODES_ALL) == MENU_MODES_ALL) got |= 1u << 8;
	if ((c->c_maps & ((1u << MENU_NPLAY) - 1)) == (1u << MENU_NPLAY) - 1) got |= 1u << 9;
	if (c->level >= 10) got |= 1u << 10;
	unsigned nw = got & ~c->ach; c->ach |= got; r->nach_new = 0;
	for (int k = 0; k < MENU_NACH; k++) if ((nw >> k) & 1) { if (r->nach_new < 4) r->ach_new[r->nach_new] = k; r->nach_new++; }
	r->ach = c->ach; r->c_kills = c->c_kills; r->c_maps = c->c_maps; r->c_modes = c->c_modes;
}
static struct { MenuResults r; int phase, tab, top; float t, st, tt; MGroup ring, hex, mt, mb; int ok, snd; } MRS2;   // snd: the result tabs' sounds played (b59); tt: seconds on the tabs (b77)
// b77 (footage #49 0:15-0:40: "EXITING TO LOBBY IN 19 SEC" ~11 s after the tabs came up; the operator then turns to the
// ready pose): the countdown starts MRS_EXIT_AT s into the tabs, MRS_EXIT_LEN s long, then the lobby (Cross goes sooner)
#define MRS_EXIT_AT (mrs_fast_exit ? 3.0f : 12.0f)   // (test switch ux0:data/gitsfa/fastexit: 3 s - the emulator runs the game slower than the clock)
static int mrs_fast_exit, mrs_test_hold;
#define MRS_EXIT_LEN 20.0f
// b58: phases 0 = the team line-up (footage: the side posed on the stage, a name over each operator), 1 = TOP OPERATIVE,
// 2 = the result tabs
#define MRS_LINEUP 0
#define MRS_TOP 1
#define MRS_TABS 2
static float mrs_tag[6][2]; static int mrs_tagp[6], mrs_ntag;   // the line-up's name tags: screen points over the heads (main.c chr_lineup), player index
// b96 (Demolition video 567.5-576 s: the line-up ~5.4 s, then in ~0.5 s the camera closes in on the top operative, the team
// staying behind him out of focus): 0 = the line-up's camera, 1 = TOP OPERATIVE's close-up (main.c chr_lineup)
static float mrs_focus, mrs_slide;
// b109 (user, with two PC pictures: "make sure the top operative text or animation on the top operative screen doesnt cover up the
// player's character model or face"): the top operative's outline on the screen for the close-up's final camera (main.c chr_lineup:
// circles about the pose's bones, px), and where the title goes so that it clears them - right of the face, above the shoulder,
// as on PC. Motoko's place is the PC's; a broad operator (the Operator's shoulder plate) pushed into the title at the fixed place.
static float mrs_dt;
#define MRS_ROWS 68
#define MRS_ROWH 8.0f
static float mrs_row[MRS_ROWS]; static int mrs_nfig; static float mrs_title_dx, mrs_title_dy; static int mrs_title_set;   // (mrs_row: how far right the top operative reaches in each 8 px row - main.c chr_lineup)
static void mrs_title_place(float x0, float y0, float x1, float y1) {   // the title block's box at its authored place
	float best = 1e9f, bx = 0, by = 0, least = 1e9f, lx = 0, ly = 0;
	for (int k = 0; k <= 12; k++) { float dy = -11.f * k, ext = -1e9f; if (y0 + dy < 52) break;   // each height: how far right the figure reaches in the block's band
		int r0 = (int)floorf((y0 + dy - 8) / MRS_ROWH), r1 = (int)floorf((y1 + dy + 8) / MRS_ROWH); if (r0 < 0) r0 = 0; if (r1 >= MRS_ROWS) r1 = MRS_ROWS - 1;
		for (int r = r0; r <= r1; r++) if (mrs_row[r] > ext) ext = mrs_row[r];
		float dx = ext + 22 - x0, room = 948 - x1; if (dx < 0) dx = 0; if (room < 0) room = 0;
		if (dx > room) { float over = dx - room + 0.2f * -dy; if (over < least) { least = over; lx = room; ly = dy; } continue; }   // (no room at this height: remembered in case none has)
		float cost = dx + 1.4f * -dy; if (cost < best) { best = cost; bx = dx; by = dy; } }
	if (best < 1e9f) { mrs_title_dx = bx; mrs_title_dy = by; } else if (least < 1e9f) { mrs_title_dx = lx; mrs_title_dy = ly; }
}
#define MRS_LU_LEN 5.4f   // the line-up's length (s)
static void menu_results_open(const MenuResults *r) {
	memset(&MRS2.r, 0, sizeof(MRS2.r)); MRS2.r = *r; MRS2.phase = MRS_LINEUP; MRS2.tab = 0; MRS2.t = MRS2.st = MRS2.tt = 0; MRS2.top = -1; mrs_ntag = 0; MRS2.snd = 0;
	for (int i = 0; i < r->nplayers && i < 12; i++) if (r->p[i].team == 0 && (MRS2.top < 0 || r->p[i].score > r->p[MRS2.top].score)) MRS2.top = i;
	if (MRS2.top < 0) MRS2.phase = MRS_TABS;
	menu_touch_lock();
}
static void mrs_init(void) {
	if (MRS2.ok) return; MRS2.ok = 1;
	mg_init(&MRS2.ring, "res.ring"); mg_init(&MRS2.hex, "res.hex"); mg_init(&MRS2.mt, "mvp.title"); mg_init(&MRS2.mb, "mvp.bar");
}
static const char *MENU_RES_TAB[3] = { "RESULTS", "PROGRESS", "STATISTICS" };
// returns 1 when the player leaves to the lobby (Cross on the result screen, Circle, or a tap on CONTINUE / LOBBY)
static int menu_results_update(unsigned pressed, float dt) {
	MRS2.t += dt; MRS2.st += dt; mrs_dt = dt; mu_in_begin(dt); int r = 0;
	if (MRS2.phase < MRS_TABS) {   // the line-up (5.4 s, b96: the Demolition video's 567.5-572.9 s), TOP OPERATIVE (6 s): Cross / Circle or a tap go on (b118: not START - the Vita's screenshot chord)
		if (MRS2.st > (mrs_test_hold ? 13 : MRS2.phase == MRS_LINEUP ? MRS_LU_LEN : 6) || (!mrs_test_hold && ((pressed & (MB_OK | MB_BACK)) || mu_tapped(MU_ID(MU_K_BG, 0))))) { MRS2.phase++; MRS2.st = 0; menu_touch_lock();   // (b108, test switch restest: each held 13 s - the device's test screenshots come 10 s apart)
			MENU_SFX(MRS2.phase == MRS_TOP ? "ui/result_mvp_player" : "ui/result_player_list");   // b59: TOP OPERATIVE's fanfare, the tabs' sweep
			logp("%s\n", MRS2.phase == MRS_TOP ? "results: TOP OPERATIVE - the camera closes in (b96)" : "results: the tabs"); }
	} else {
		if (MRS2.tt < MRS_EXIT_AT && MRS2.tt + dt >= MRS_EXIT_AT) logp("results: EXITING TO LOBBY IN %.0f SEC (b77)\n", MRS_EXIT_LEN);
		MRS2.tt += dt; if (MRS2.tt >= MRS_EXIT_AT + MRS_EXIT_LEN) { r = 1; logp("results: the countdown ran out: to the lobby (b77)\n"); }   // b77: the exit countdown ran out
		// b59: the medal strip's sounds once the tabs are up: missions done, then new achievements
		if (MRS2.snd == 0 && MRS2.t > 0.4f && MRS2.st > 0.7f) { MRS2.snd = 1; if (MRS2.r.mission_n > 0) MENU_SFX("ui/mission_complete"); }
		if (MRS2.snd == 1 && MRS2.st > 1.3f) { MRS2.snd = 2; if (MRS2.r.nach_new > 0) MENU_SFX("ui/result_unlock"); }
		int t0 = MRS2.tab;
		if (pressed & (MB_L | MB_LEFT)) MRS2.tab = (MRS2.tab + 2) % 3;
		if (pressed & (MB_R | MB_RIGHT)) MRS2.tab = (MRS2.tab + 1) % 3;
		for (int i = 0; i < 3; i++) if (MUT.press && MUT.id == MU_ID(MU_K_RTAB, i)) MRS2.tab = i;   // (the medal strip is PROGRESS's too)
		if (MRS2.tab != t0) { MRS2.st = 0.31f; if (!(pressed & (MB_L | MB_R | MB_LEFT | MB_RIGHT))) MENU_SFX("ui/tab_on"); }
		// b118 (user: "the game also seems to freeze for a bit after you take a screenshot with the vita on the top operative screen";
		// the device's log: 25.5 s in the one frame in which the lobby opened): the Vita's screenshot is PS + START, and START went
		// on from the line-up / TOP OPERATIVE and left the tabs - the game changed screens, loading the lobby's pictures, while the
		// system was saving its screenshot. START does nothing on the results now (Cross, Circle and a tap go on, as their prompts say).
		if (MRS2.st > 0.5f && (pressed & (MB_OK | MB_BACK))) r = 1;
		if (mu_tapped(MU_ID(MU_K_TOP, 0))) { r = 1; MENU_SFX("ui/click_basic"); }   // (b77: the LOBBY tab; no CONTINUE button, as the footage's MATCH tab)
	}
	mu_in_end(); return r;
}
// the character the caller draws in 3D (after menu_results_draw_bg, before menu_results_draw): returns the operator
// (CHARS index) or -1; rect / cam as for menu_fe_preview. TOP OPERATIVE: close-up left of centre; RESULTS / PROGRESS: you, on
// the right (b58: STATISTICS has the awards there). The line-up draws through menu_results_lineup instead.
static int menu_results_preview(float *rect, float *cam) {
	menu_prev_shift = 0; if (MRS2.phase == MRS_LINEUP) return -1;
	// b76 (user: turning the TOP OPERATIVE's figure cut it and the gun off on the right): the whole screen, the picture's centre
	// shifted to x 310 as before (menu_prev_shift: main.c chr_preview's lens shift) - a turned gun now passes behind the panels
	// b96: TOP OPERATIVE is the line-up's camera closing in (menu_results_lineup, main.c chr_lineup: the same close-up, b76's numbers)
	if (MRS2.phase == MRS_TOP) return -1;
	// b77 (user: the result tabs as footage #49 0:04-0:40): you on every tab (STATISTICS too), the whole screen with the picture's
	// centre at x 738, head ~83 px down, cut below the knees (~500 px figure); camera matched on the host against the frame at
	// 0:07 (scratchpad b77 cal3: eye / look-at 105 cm, 322 cm away, 36 deg, turned -15 deg)
	rect[0] = 0; rect[1] = 0; rect[2] = 960; rect[3] = 544; cam[0] = 105; cam[1] = 322; cam[2] = 105; cam[3] = 36; cam[4] = -15; menu_prev_shift = 738.f / 480.f - 1;
	for (int i = 0; i < MRS2.r.nplayers; i++) if (MRS2.r.p[i].me) return MRS2.r.p[i].chr;
	return -1;
}
// b77: the result tabs' animation for main.c chr_preview (the out-of-game bank's "result" set): 0 none (TOP OPERATIVE / the
// line-up), 1 victory, 2 defeat, 3 draw; *ready: the exit countdown runs (the ready clips)
static int menu_results_anim(int *ready) {
	*ready = 0; if (MRS2.phase != MRS_TABS) return 0;
	*ready = MRS2.tt >= MRS_EXIT_AT; int res = MRS2.r.result < 0 || MRS2.r.result > 2 ? 2 : MRS2.r.result; return 1 + res;
}
// ui b50: the gun the previewed operator holds - TOP OPERATIVE: the gun that player used; the result tabs: yours (NULL: the M4A1 pose, no gun)
static const char *menu_results_preview_weapon(void) {
	if (MRS2.phase == MRS_TOP) return MRS2.top >= 0 && MRS2.r.p[MRS2.top].wpn[0] ? MRS2.r.p[MRS2.top].wpn : NULL;
	for (int i = 0; i < MRS2.r.nplayers; i++) if (MRS2.r.p[i].me) return MRS2.r.p[i].wpn[0] ? MRS2.r.p[i].wpn : NULL;
	return NULL;
}
// b58: the line-up - your side, the top operative in front and the others behind to each side, by score (at most 5).
// op: operator (CHARS index), wpn: the gun each used, pi: player index (name tags); cam: eye height, distance, look-at height,
// fov, yaw as menu_fe_preview (a slow push in). Returns the count (0: not the line-up).
static int menu_results_lineup(int *op, const char **wpn, int *pi, float *cam) {
	if (MRS2.phase != MRS_LINEUP && MRS2.phase != MRS_TOP) return 0;
	const MenuResults *r = &MRS2.r; int idx[12], n = 0;
	for (int i = 0; i < r->nplayers && i < 12; i++) if (r->p[i].team == 0) idx[n++] = i;
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && r->p[idx[j]].score > r->p[idx[j - 1]].score; j--) { int x = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = x; }
	if (n > 5) n = 5;
	for (int k = 0; k < n; k++) { const MenuPlayer *P = &r->p[idx[k]]; op[k] = P->chr; wpn[k] = P->wpn[0] ? P->wpn : NULL; pi[k] = idx[k]; }
	// b99: the game's own result cameras (effect.dat outgame_camera\cam_result*.nif, cam_cu_Easy*.nif): a long lens - 17.15 deg
	// top to bottom (its frustum +-0.1507; until b98 34 deg from half the distance: the operatives behind came out a fifth smaller,
	// the PC's stand nearly as large) - level, 536 cm from the operatives: thigh-up, the top operative's head a seventh of the
	// screen down (the Demolition video 568-573 s). It slides in from the side - 557 cm in a third of a second - and stands
	// still (b58-b98: a slow push in, b96 from further back for the floor - the PC shows no floor here); the close-up eases in
	// over the same third of a second (cam_cu_EasyIn; main.c CU).
	(void)n; cam[0] = 112; cam[1] = 536; cam[2] = 112; cam[3] = 17.15f; cam[4] = 0;
	mrs_slide = MRS2.phase == MRS_LINEUP ? 557 * (1 - msmooth(MRS2.st / 0.3333f)) : 0;
	mrs_focus = MRS2.phase == MRS_TOP ? msmooth(MRS2.st / 0.3333f) : 0;   // b96: the close-up's camera move (b99: 1/3 s)
	return n;
}
static void menu_results_draw_bg(void) {
	if (menu_fe < 0) return;
	menu_begin(menu_fe); mrs_init(); menu_bg("lobby", 1);
	if (MRS2.phase == MRS_TABS) { float m[4] = { 1, 1, 1, 0.62f + 0.22f * (0.5f + 0.5f * sinf(MRS2.t * 1.1f)) }; mg_draw(&MRS2.hex, m); fe_lights_anim(MRS2.t); }   // b78: the hexes breathe, the lights drift (footage #49)
	menu_end();
}
static void mrs_rank(int level, float x0, float y0, float x1, float y1, const float *m) { char b[8]; snprintf(b, sizeof(b), "rk.%d", level < 1 ? 0 : level > 20 ? 19 : level - 1); mu_imgfit(MI(b), x0, y0, x1, y1, m); }
static const char *MRS_RT[3] = { "VICTORY", "DEFEAT", "DRAW" }, *MRS_EM[3] = { "rw.emb.victory", "rw.emb.defeat", "rw.emb.draw" };
static const float MRS_RC[3][4] = { { 0.55f, 0.92f, 1, 1 }, { 1, 0.45f, 0.42f, 1 }, { 0.88f, 0.90f, 0.90f, 1 } };
// ---- b77 (user: "refer to the ... 12 Level #49 gameplay footage for how the progress, results and statistics screens should look
// alongside the character model of the player and its animations"; frames 0:04 RESULTS, 0:09 PROGRESS, 0:13-0:40 STATISTICS):
// the left half carries the tab's emblem, its title over a faint copy of itself, and its content straight on the backdrop (no
// panels); the operator stands on the right of every tab with the name plate over the legs (rank badge, name, LV under it);
// after a while "EXITING TO LOBBY IN n SEC" at the bottom. The original's 9-11 px text is set at the Vita's sizes (>= 20 px).
static const float MRS_GP[4] = { 0.30f, 0.82f, 0.68f, 1 };   // the footage's green-cyan GP figures
static void mrs_tipicon(float x, float y) { mu_ring(x, y, 5.5f, 7.5f, MU_WHITE); mu_line(x - 4.5f, y + 4.5f, x + 4.5f, y - 4.5f, 2, MU_WHITE); }   // the footage's "(/)" before a heading
static void mrs_heading(float x, float y, const char *t) { mrs_tipicon(x + 7, y - 8); mu_text(x + 20, y, MU_TEXT, 0, MU_WHITE, t); }
static void mrs_title(const char *t, const float *c) {   // the tab's title at x 178 over its faint copy further right (footage: "VICTORY", "PROGRESS")
	float g[4] = { 1, 1, 1, 0.07f }; mu_text(292, 178, 96, 0, g, t); mu_text(176, 172, 60, 0, c, t);
}
// the result tabs: two sweeps with fading tails turning on the outer ring, a dashed ring the other way inside it (the donut's
// centre at 117, 162: the res.ring parts as drawn)
static void mrs_rings_anim(float t, float a) {
	const float cx = 117, cy = 162; float h0[4] = { 0.55f, 0.88f, 0.92f, 0 }, h1[4] = { 0.62f, 0.92f, 0.96f, 0.50f * a }, d[4] = { 0.55f, 0.85f, 0.9f, 0.22f * a };
	float s = t * 16;   // deg / s
	mu_arc(cx, cy, 117, 122, s, s + 75, h0, h1); mu_arc(cx, cy, 117, 122, s + 180, s + 255, h0, h1);
	float b = -t * 9; for (int i = 0; i < 18; i++) mu_arc(cx, cy, 99, 101, b + i * 20, b + i * 20 + 9, d, d);
}
static void mrs_band(float y0, float y1) {   // the darker band behind a table (footage: behind BATTLE REWARDS), fading out to the right
	static const float d0[4] = { 0.0f, 0.04f, 0.05f, 0.42f }, d1[4] = { 0.0f, 0.04f, 0.05f, 0.0f };
	mu_rect(0, y0, 420, y1, d0); mu_recth(420, y0, 640, y1, d0, d1);
}
// the name plate (footage: rank badge 610-650 x 450-490, the name beside it, LV under the badge, a line along the bottom)
static void mrs_plate(const MenuResults *r) {
	const char *nm = "OPERATOR"; for (int i = 0; i < r->nplayers; i++) if (r->p[i].me) nm = r->p[i].name;
	static const float b0[4] = { 0.01f, 0.04f, 0.05f, 0.0f }, b1[4] = { 0.01f, 0.04f, 0.05f, 0.78f }, ln[4] = { 0.75f, 0.90f, 0.92f, 0.55f };
	mu_recth(560, 438, 660, 480, b0, b1); mu_rect(660, 438, 960, 480, b1); mu_rect(656, 479, 960, 481, ln);
	mrs_rank(r->level, 604, 434, 652, 482, NULL); mu_textbox(664, 438, 956, 480, MU_H2, 0, 0, MU_WHITE, nm);
	char b[16]; snprintf(b, sizeof(b), "LV %d", r->level); mu_text(628, 504, MU_FINE, 2, MU_WHITE, b);
}
static void mrs_countdown(void) {   // "EXITING TO LOBBY IN 14 SEC" (footage: bottom centre, the number large)
	if (MRS2.tt < MRS_EXIT_AT) return; int n = (int)ceilf(MRS_EXIT_AT + MRS_EXIT_LEN - MRS2.tt); if (n < 0) n = 0; char b[16]; snprintf(b, sizeof(b), "%d", n);
	float a = msmooth((MRS2.tt - MRS_EXIT_AT) / 0.4f), w0 = mu_tw("EXITING TO LOBBY IN", MU_FINE), w1 = mu_tw(b, MU_H1), w2 = mu_tw("SEC", MU_FINE), x = 330 - (w0 + w1 + w2 + 16) / 2;
	float c[4] = { 1, 1, 1, a };
	mu_text(x, 532, MU_FINE, 0, c, "EXITING TO LOBBY IN"); mu_text(x + w0 + 8, 536, MU_H1, 0, c, b); mu_text(x + w0 + w1 + 16, 532, MU_FINE, 0, c, "SEC");
}
// a PROGRESS card (footage: the M249-G4 / N7 RDS / PHYSICAL cards: a title band, the picture, a bar, the status strip)
static void mrs_card(float x0, float y0, float x1, float y1, const char *title, const char *sub, int img, float img_a, float bar, const char *bar_txt, const char *status, const float *sc) {
	static const float bg[4] = { 0.05f, 0.16f, 0.19f, 0.86f }, hd[4] = { 0.08f, 0.24f, 0.28f, 0.92f }, st[4] = { 0.02f, 0.07f, 0.09f, 0.92f }, rim[4] = { 0.45f, 0.70f, 0.75f, 0.45f }, tr[4] = { 0.10f, 0.22f, 0.25f, 1 };
	mu_rect(x0, y0, x1, y1, bg); mu_rect(x0, y0, x1, y0 + 52, hd); mu_frame(x0, y0, x1, y1, 1, rim);
	mu_wrap(x0 + 8, y0 + 4, x1 - x0 - 16, MU_FINE, 22, 2, MU_WHITE, title);
	if (sub && sub[0]) mu_textbox(x0 + 6, y0 + 54, x1 - 6, y0 + 78, MU_FINE, 2, 0, MU_DIM, sub);
	float m[4] = { 1, 1, 1, img_a }; float py0 = y0 + (sub && sub[0] ? 78 : 58), py1 = y1 - (bar >= 0 ? 52 : 34); if (img >= 0 && py1 - py0 >= 24) mu_imgfit(img, x0 + 10, py0, x1 - 10, py1, m);
	// (a short card with a description too - the achievements row: the figures go into the status strip, left; the status right)
	int low = sub && sub[0] && y1 - y0 < 140 && bar_txt;
	if (bar >= 0) { mu_rect(x0 + 12, y1 - 42, x1 - 12, y1 - 36, tr); mu_rect(x0 + 12, y1 - 42, x0 + 12 + (x1 - x0 - 24) * mu_clamp(bar, 0, 1), y1 - 36, MU_WHITE);
		if (bar_txt && !low) mu_text(x0 + 12, y1 - 46, MU_FINE, 0, MU_YELLOW, bar_txt); }
	mu_rect(x0, y1 - 30, x1, y1, st);
	if (low) { mu_textbox(x0, y1 - 30, x1, y1, MU_FINE, 0, 8, MU_YELLOW, bar_txt); mu_textbox(x0, y1 - 30, x1, y1, MU_FINE, 1, 8, sc, status); }
	else mu_textbox(x0, y1 - 30, x1, y1, MU_FINE, 2, 4, sc, status);
}
static void mrs_results_tab(const MenuResults *r, int res) {
	mu_imgfit(MI(MRS_EM[res]), 64, 96, 170, 214, NULL);
	mu_text(180, 112, MU_FINE, 0, MU_WHITE, "GHOST A"); mrs_title(MRS_RT[res], res == 1 ? MRS_RC[1] : MU_WHITE);
	mrs_band(186, 340);
	mrs_heading(184, 210, "BATTLE REWARDS"); mu_text(422, 210, MU_TEXT, 0, MU_WHITE, "GP"); mu_text(528, 210, MU_TEXT, 0, MU_WHITE, "EXP");   // (the footage's 337 / 492: its heading is 10 px, ours 24)
	static const char *pt[3] = { "COMBAT", "SKILL", "OBJECTIVE" }; int tg = 0, tx = 0; char b[40];   // (b77: 16 cut "EXP : 108 / 1000" to "/ 100")
	for (int i = 0; i < 3; i++) { float k = msmooth((MRS2.st - 0.3f - 0.15f * i) / 0.5f), y = 240 + i * 28;
		mu_text(204, y, MU_TEXT, 0, MU_DIM, pt[i]); snprintf(b, sizeof(b), "%d", (int)(r->gp[i] * k)); mu_text(422, y, MU_TEXT, 0, MRS_GP, b);
		snprintf(b, sizeof(b), "%d", (int)(r->xp[i] * k)); mu_text(528, y, MU_TEXT, 0, MU_DIM, b); tg += r->gp[i]; tx += r->xp[i]; }
	{ float k = msmooth((MRS2.st - 0.8f) / 0.5f); mu_text(196, 330, MU_H2, 0, MU_WHITE, "TOTAL"); snprintf(b, sizeof(b), "%d", (int)(tg * k)); mu_text(422, 330, MU_H2, 0, MU_WHITE, b);
	  snprintf(b, sizeof(b), "%d", (int)(tx * k)); mu_text(528, 330, MU_H2, 0, MU_WHITE, b); }
	// the level bar (footage: rank badges each end, LV under them, NEXT over the next one, the EXP figures under the bar)
	{ float f = r->exp_next > 0 ? (float)r->exp / r->exp_next : 0; static const float tr[4] = { 0.08f, 0.16f, 0.19f, 0.95f }, fg[4] = { 0.30f, 0.72f, 0.92f, 1 };
	  mrs_rank(r->level, 260, 358, 300, 398, NULL); mrs_rank(r->level + 1, 540, 358, 580, 398, NULL);
	  mu_rect(304, 372, 536, 384, tr); float x1 = 304 + 232 * f * msmooth((MRS2.st - 0.5f) / 0.8f); mu_rect(304, 372, x1, 384, fg); mu_rect(x1 - 2, 370, x1, 386, MU_WHITE);
	  snprintf(b, sizeof(b), "LV %d", r->level); mu_text(280, 418, MU_FINE, 2, MU_WHITE, b); snprintf(b, sizeof(b), "LV %d", r->level + 1); mu_text(560, 418, MU_FINE, 2, MU_WHITE, b);
	  mu_text(594, 384, MU_FINE, 0, MU_WHITE, "NEXT"); snprintf(b, sizeof(b), "EXP : %d / %d", r->exp, r->exp_next); mu_text(420, 406, MU_FINE, 2, MU_DIM, b); }
	// ADDITIONAL REWARDS (footage: the GP +10 % badge; here b58's mission / achievement medals - a tap opens PROGRESS)
	mrs_heading(184, 448, "ADDITIONAL REWARDS");
	static const char *lab[2] = { "MISSION", "ACHIEVEMENT" }, *im[2] = { "md.mission", "md.achieve" }; int n[2] = { r->mission_n, r->nach_new };
	for (int k = 0; k < 2; k++) { float x0 = 196 + k * 200, m[4] = { 1, 1, 1, n[k] ? 1 : 0.4f };
		mu_imgfit(MI(im[k]), x0, 456, x0 + 44, 500, m); snprintf(b, sizeof(b), "+%d", n[k]); mu_text(x0 + 52, 478, MU_TEXT, 0, n[k] ? MU_YELLOW : MU_DIM, b);
		mu_text(x0 + 52, 500, MU_FINE, 0, MU_WHITE, lab[k]); }
	mu_reg(MU_ID(MU_K_RTAB, 1), 184, 440, 600, 512);   // (b109: 72 px tall - the legibility rule for touch targets; it was 54)
}
static void mrs_progress_tab(const MenuResults *r) {
	mu_imgfit(MI("pg.emb"), 64, 96, 170, 214, NULL); mrs_title("PROGRESS", MU_WHITE);
	mrs_tipicon(191, 197); mu_text(204, 204, MU_FINE, 0, MU_WHITE, "Tip. Missions change every match.");
	// missions: one card each (footage: the unlock cards - title, picture, the bar, UNLOCKING)
	char b[48], t[48];
	for (int k = 0; k < 3; k++) {
		float x0 = 30 + k * 190, done = r->mis_have[k] >= r->mis_need[k]; int have = r->mis_have[k] > r->mis_need[k] ? r->mis_need[k] : r->mis_have[k];
		snprintf(b, sizeof(b), "%d / %d", have, r->mis_need[k]);
		mrs_card(x0, 216, x0 + 180, 372, r->mis_text[k], NULL, MI("md.mission"), done ? 1 : 0.35f, r->mis_need[k] > 0 ? (float)have / r->mis_need[k] : 0, b, done ? "COMPLETE" : "IN PROGRESS", done ? MU_YELLOW : MU_WHITE);
	}
	// achievements: the ones earned now; then the next to go for and the count
	int nc = 0;
	for (int j = 0; j < r->nach_new && nc < 3; j++, nc++) { const MenuAch *A = &MENU_ACH[r->ach_new[j]]; float x0 = 30 + nc * 190;
		mrs_card(x0, 382, x0 + 180, 500, A->name, A->desc, -1, 1, -1, NULL, "UNLOCKED", MU_YELLOW); }
	if (nc < 3) { int k = 0; while (k < MENU_NACH && ((r->ach >> k) & 1)) k++;
		if (k < MENU_NACH) { int have = 0, need = menu_ach_prog(k, r, r->level, &have); float x0 = 30 + nc * 190; snprintf(b, sizeof(b), "%d / %d", have > need ? need : have, need);
			mrs_card(x0, 382, x0 + 180, 500, MENU_ACH[k].name, MENU_ACH[k].desc, -1, 0.35f, need ? (float)(have > need ? need : have) / need : -1, need ? b : NULL, "NEXT", MU_WHITE); nc++; } }
	if (nc < 3) { float x0 = 30 + nc * 190; snprintf(t, sizeof(t), "%d / %d", mbits(r->ach), MENU_NACH);
		mrs_card(x0, 382, x0 + 180, 500, "ACHIEVEMENTS EARNED", NULL, -1, 0.75f, (float)mbits(r->ach) / MENU_NACH, t, mbits(r->ach) >= MENU_NACH ? "ALL EARNED" : "IN PROGRESS", MU_WHITE); }
}
static void mrs_stats_tab(const MenuResults *r, int res) {
	int mode = r->mode < 0 || r->mode >= MODE_COUNT ? 0 : r->mode; char b[32];
	mu_imgfit(MI(MENU_MODE_EMB[mode]), 64, 60, 156, 152, NULL);
	{ float g[4] = { 1, 1, 1, 0.07f }; mu_text(250, 128, 76, 0, g, MENU_MODE_NAME[mode]); }
	mu_text(166, 86, MU_TEXT, 0, MU_WHITE, r->map >= 0 && r->map < MENU_NMAPS ? MENU_MAP[r->map].name : "");
	mu_textbox(164, 92, 612, 130, 38, 0, 2, MU_WHITE, MENU_MODE_NAME[mode]);
	snprintf(b, sizeof(b), "PLAY TIME %02d:%02d", (int)r->play_time / 60, (int)r->play_time % 60); mu_text(170, 152, MU_FINE, 0, MU_WHITE, b);
	// columns (footage: RANK, NICKNAME, SCORE, KILLS, ASSISTS, KDR; a face in front of each row)
	static const char *hd[6] = { "RANK", "NICKNAME", "SCORE", "KILLS", "ASSISTS", "KDR" }; static const float hx[6] = { 214, 242, 380, 440, 512, 590 };   // (b77: 20 px headers clear of each other)
	for (int k = 0; k < 6; k++) mu_text(k ? hx[k] : 197, 178, MU_FINE, k == 1 ? 0 : 2, MU_WHITE, hd[k]);   // (RANK over the face and the badge: clear of NICKNAME)
	const float RH = 30, Y0[2] = { 186, 324 }; static const float sl[4] = { 0.55f, 0.75f, 0.78f, 0.16f };
	for (int t = 0; t < 2; t++) {
		int win = t == 0 ? res == 0 : res == 1, idx[12], n = 0; float y = Y0[t];
		for (int i = 0; i < r->nplayers; i++) if (r->p[i].team == t) idx[n++] = i;
		for (int i = 1; i < n; i++) for (int j = i; j > 0 && r->p[idx[j]].score > r->p[idx[j - 1]].score; j--) { int x = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = x; }
		if (n > 4) n = 4;
		mrs_band(y - 4, y + 4 * RH + 4);
		mu_text(112, y + 26, MU_TEXT, 2, MU_WHITE, t ? "GHOST B" : "GHOST A");
		mu_text(112, y + 54, MU_TEXT, 2, res == 2 ? MU_WHITE : win ? MU_WHITE : MRS_RC[1], res == 2 ? "DRAW" : win ? "VICTORY" : "DEFEAT");
		snprintf(b, sizeof(b), "%d", r->score[t]); mu_text(112, y + 106, 48, 2, MU_WHITE, b);
		for (int k = 0; k <= 4; k++) mu_rect(166, y + k * RH, 612, y + k * RH + 1, sl);   // the row slots (footage: the empty ones show as lines)
		for (int k = 0; k < n; k++) {
			const MenuPlayer *P = &r->p[idx[k]]; float ry = y + k * RH;
			if (P->me) { static const float c[4] = { 0.20f, 0.46f, 0.66f, 0.85f }; mu_rect(166, ry + 1, 612, ry + RH, c); }
			if (P->chr >= 0 && P->chr < 12) { char fb[20]; snprintf(fb, sizeof(fb), "op.face.%d", MENU_CHR_FACE[P->chr]); mu_imgfit(MI(fb), 168, ry + 2, 196, ry + RH, NULL); }
			mrs_rank(P->me ? r->level : 1 + (P->score > 0 ? P->score / 250 : 0), hx[0] - 13, ry + 3, hx[0] + 13, ry + RH - 1, NULL);   // (the bots: a badge from their score)
			mu_textbox(hx[1] - 4, ry, hx[2] - 34, ry + RH, MU_SMALL, 0, 0, MU_WHITE, P->name);
			snprintf(b, sizeof(b), "%d", P->score); mu_textbox(hx[2] - 32, ry, hx[2] + 32, ry + RH, MU_SMALL, 2, 0, MU_WHITE, b);
			snprintf(b, sizeof(b), "%d", P->kills); mu_textbox(hx[3] - 28, ry, hx[3] + 28, ry + RH, MU_SMALL, 2, 0, MU_WHITE, b);
			snprintf(b, sizeof(b), "%d", P->assists); mu_textbox(hx[4] - 30, ry, hx[4] + 30, ry + RH, MU_SMALL, 2, 0, MU_WHITE, b);
			{ float kd = P->deaths ? (float)P->kills / P->deaths : (float)P->kills; static const float up[4] = { 0.30f, 0.78f, 0.95f, 1 }, mid[4] = { 0.98f, 0.78f, 0.20f, 1 }, lo[4] = { 0.92f, 0.22f, 0.22f, 1 };
			  mu_tri(hx[5], ry + RH / 2, 7, kd >= 1 ? 2 : 3, kd >= 1 ? up : kd >= 0.5f ? mid : lo); }   // (footage: KDR as an arrow - cyan up, yellow / red down)
		}
	}
	// b58's awards under the tables (the footage's statistics has none; kept: who earned CONTROLLER / DESTROYER / TECHNICIAN)
	for (int k = 0; k < 3; k++) {
		float x0 = 20 + k * 196; int i = r->award[k]; const MenuPlayer *P = i >= 0 && i < r->nplayers ? &r->p[i] : NULL; float m[4] = { 1, 1, 1, P ? 1 : 0.35f };
		mu_imgfit(MI(MENU_AWARD_IMG[k]), x0, 452, x0 + 44, 496, m);
		mu_text(x0 + 50, 470, MU_FINE, 0, P ? MU_YELLOW : MU_DIM, MENU_AWARD[k]);
		mu_textbox(x0 + 48, 474, x0 + 192, 498, MU_FINE, 0, 2, P && P->me ? MU_CYAN : P ? MU_WHITE : MU_DIM, P ? P->name : "NOT AWARDED");
	}
}
static void menu_results_draw(void) {
	if (menu_fe < 0) return;
	mu_draw_begin(); menu_begin(menu_fe); mrs_init();
	const MenuResults *r = &MRS2.r; int res = r->result < 0 || r->result > 2 ? 2 : r->result;
	if (MRS2.phase == MRS_LINEUP) {   // b58: the team line-up (the figures are main.c's): result, a name over each operator
		float t = MRS2.st, a = msmooth(t / 0.5f), at = msmooth((t - 0.6f) / 0.5f);
		static const float c0[4] = { 0, 0, 0, 0 }, c1[4] = { 0.0f, 0.02f, 0.03f, 0.75f }; mu_rectv(0, 456, 960, 544, c0, c1); mu_rectv(0, 0, 960, 150, c1, c0);
		mu_a = a; hud_dx = -30 * (1 - a);
		mu_imgfit(MI(MRS_EM[res]), 16, 10, 104, 98, NULL);
		mu_text(116, 44, MU_TEXT, 0, MU_DIM, "GHOST A"); mu_text(112, 92, 48, 0, MRS_RC[res], MRS_RT[res]); hud_dx = 0;
		mu_a = at;
		for (int k = 0; k < mrs_ntag; k++) { int i = mrs_tagp[k]; if (i < 0 || i >= r->nplayers) continue; const MenuPlayer *P = &r->p[i];
			float x = mrs_tag[k][0], y = mrs_tag[k][1], w = mu_tw(P->name, MU_TEXT) + 56; if (x < w / 2 + 4) x = w / 2 + 4; if (x > 956 - w / 2) x = 956 - w / 2;
			if (y < 42) y = 42; if (x - w / 2 < 262 && y < 110) x = 262 + w / 2;   // (clear of the title: beside it - b99, the heads stand higher now; b58: under it)
			mu_img(MI("lu.tag"), x - w / 2, y - 20, x + w / 2, y + 14, NULL); mu_text(x, y + 6, MU_TEXT, 2, P->me ? MU_CYAN : MU_WHITE, P->name);
			if (i == MRS2.top) { float s[4] = { MU_YELLOW[0], MU_YELLOW[1], MU_YELLOW[2], 1 }; mu_text(x, y - 24, MU_FINE, 2, s, "TOP SCORE"); } }
		mu_a = a; mu_reg(MU_ID(MU_K_BG, 0), 0, 0, 960, 544); mu_hints("R:ROTATE|x:CONTINUE", MU_HINT_Y); mu_a = 1;
		menu_end(); return;
	}
	if (MRS2.phase == MRS_TOP) {   // TOP OPERATIVE of your team
		const MenuPlayer *P = &r->p[MRS2.top]; float t = MRS2.st, a = msmooth(t / 0.4f), m[4] = { 1, 1, 1, a };
		{   // b99: the title's echo behind it (the movie's titleback1: the same words 2.2 times the size, 39 px left / 22 px up of the
			// title's corner, drifting right 5 px a second; faint - the user's PC screenshots show it at about a twelfth of white)
			float x0, y0, x1, y1; if (mu_field_box("mvp.t1", &x0, &y0, &x1, &y1) >= 0) { float h = y1 - y0, c[4] = { 1, 1, 1, 0.075f * a };
				mu_text(x0 + mrs_title_dx - 38.6f + 4.7f * fminf(t, 12), y0 + mrs_title_dy - 22 + 2.2f * h * 0.5f + 2.2f * h * 0.72f * MU_CAP * 0.5f, 2.2f * h * 0.72f, 0, c, "TOP OPERATIVE"); } }
		{   // b109: the title clear of the figure (placed while it fades in; after that only ever further out of the way: the stance sways, R turns the figure)
			float x0, y0, x1, y1, u0, v0, u1, v1; if (t < 0.02f) { mrs_title_set = 0; mrs_title_dx = mrs_title_dy = 0; }
			if (mrs_nfig && mu_field_box("mvp.t1", &x0, &y0, &x1, &y1) >= 0) { if (mu_field_box("mvp.t2", &u0, &v0, &u1, &v1) >= 0) { if (v1 > y1) y1 = v1; if (u1 > x1) x1 = u1; if (u0 < x0) x0 = u0; }
				float ox = mrs_title_dx, oy = mrs_title_dy; mrs_title_place(x0, y0, x1, y1);
				if (mrs_title_set) { mrs_title_dx = fmaxf(ox, mrs_title_dx); mrs_title_dy = fminf(oy, mrs_title_dy); }   /* (only ever further out of the way) */ mrs_title_set = 1; } }
		static float tdx, tdy; if (t < 0.45f) { tdx = mrs_title_dx; tdy = mrs_title_dy; } else { float st = 260 * mrs_dt; tdx += fmaxf(-st, fminf(st, mrs_title_dx - tdx)); tdy += fmaxf(-st, fminf(st, mrs_title_dy - tdy)); }   // (later - the figure turned with R: it glides out of the way)
		// (the title's lines run 330 px left of the words in our layout - across the figure's neck; the PC's start at the words: cut there)
		{ float x0, y0, x1, y1; hud_dx = tdx + 36 * (1 - msmooth(t / 0.5f)); hud_dy = tdy; if (mu_field_box("mvp.t1", &x0, &y0, &x1, &y1) >= 0) hud_clipx = x0 + hud_dx - 14; }
		mg_draw(&MRS2.mt, m); hud_clipx = -1e9f; MT("mvp.t1", "TOP OPERATIVE", m); mu_fld("mvp.t2", "OF THE TEAM", MU_SMALL, NULL, a); hud_dx = 0; hud_dy = 0;
		mg_draw(&MRS2.mb, m); mu_fld("mvp.name", P->name, 34, NULL, a); { float dx = hud_dx, dy = hud_dy; hud_dx += 8; hud_dy -= 4; mu_fld("mvp.stitle", "SCORE", MU_SMALL, NULL, a); hud_dy = dy + 5;   // b49: our sizes are larger than the
		  char b[16]; snprintf(b, sizeof(b), "%d", P->score); mu_fld("mvp.score", b, 34, NULL, a); hud_dx = dx; hud_dy = dy; }   // fields': SCORE and the number touched, and the slanted line
		{   // b102: the name card and the emblem right of the score (mvpuserInfo's namecardicon at (238,-152) and emblemicon at
			// (498,-155), 0.6 px a unit; the movie brings the card from 60% to full and the emblem out of white over its 20
			// frames). The cards are the client's six (ui.dat rsc/item/Namecard): yours moves on to the next with every two
			// achievements earned, a bot's is picked by its name. The PC's emblem is the clan's, which the client does not carry: the place
			// shows the award this operative earned in the match (CONTROLLER / DESTROYER / TECHNICIAN), or stays empty.
			int nc = 0; if (P->me) nc = mbits(r->ach) * 6 / (MENU_NACH + 1); else { unsigned h = 2166136261u; for (const char *q = P->name; *q; q++) h = (h ^ (unsigned char)*q) * 16777619u; nc = (int)(h % 6); }
			char nm[16]; snprintf(nm, sizeof(nm), "nc.%d", nc); float k = msmooth((t - 0.1f) / 0.55f), c[4] = { 1, 1, 1, a * (0.6f + 0.4f * k) };
			mu_img(MI(nm), 652.8f, 452.8f, 866.8f, 505.6f, c);   // (30 px right of the movie's place: our SCORE figures are larger and reach 657)
			for (int w = 0; w < 3; w++) if (r->award[w] == MRS2.top) { float e[4] = { 1, 1, 1, a * k }; mu_imgfit(MI(MENU_AWARD_IMG[w]), 808.8f, 451, 856.8f, 499, e); break; } }
		mu_reg(MU_ID(MU_K_BG, 0), 0, 0, 960, 544); mu_a = a; mu_hints("R:ROTATE|x:CONTINUE", MU_HINT_Y); mu_a = 1;
		menu_end(); return;
	}
	float a = msmooth(MRS2.st / 0.3f), rg[4] = { 0.55f, 0.75f, 0.78f, 0.45f };
	mg_draw(&MRS2.ring, rg); mrs_rings_anim(MRS2.t, a);   // b78: the rings turn (footage #49)
	// top bar: LOBBY (back) + three tabs
	{ static const float b0[4] = { 0.02f, 0.06f, 0.07f, 0.95f }, b1[4] = { 0.04f, 0.10f, 0.12f, 0.95f }; mu_rectv(0, 0, 960, 54, b0, b1); mu_rect(0, 54, 960, 56, MU_CYAND);
	  int id = MU_ID(MU_K_TOP, 0), pr = mu_pressing(id); mu_img(MI(pr ? "tb.light" : "tb.yellow"), 0, 6, 232, 50, NULL);
	  mu_back_label(48, 4, 170, 46, MU_BODY, MU_DARK, pr, "LOBBY"); mu_reg(id, 0, 0, 240, MU_TAP); }   // b50: no Circle; clear of the underline corner and the light stripe
	for (int i = 0; i < 3; i++) { float x0 = 300 + i * 218, x1 = x0 + 206; int id = MU_ID(MU_K_RTAB, i), on = i == MRS2.tab;   // tabs: targets 72 px tall, 12 px apart
		if (on) { mu_rect(x0, 4, x1, 54, MU_TABSEL); } else if (mu_pressing(id)) { float c[4] = { 0.2f, 0.45f, 0.5f, 1 }; mu_rect(x0, 4, x1, 54, c); }
		mu_textbox(x0, 4, x1, 54, MU_BODY, 2, 8, on ? MU_DARK : MU_WHITE, MENU_RES_TAB[i]); mu_reg(id, x0, 0, x1, MU_TAP); }
	mu_a = a;
	if (MRS2.tab == 0) mrs_results_tab(r, res); else if (MRS2.tab == 1) mrs_progress_tab(r); else mrs_stats_tab(r, res);
	mu_a = 1;
	mrs_plate(r); mrs_countdown();
	mu_hints("lr:TABS|R:ROTATE|x:CONTINUE", MU_HINT_Y);
	menu_end();
}
// ------------------------------------------------------------------ init, pack residency
static hud_mkshader_fn menu_mk;
// dir: data folder with ovl.ghud / menu.ghud / mload_*.gtx / mthumb.gtx (e.g. "ux0:data/gitsfa/"); keeps the caller's current pack
static int menu_init(const char *dir, hud_mkshader_fn mk) {
	HudPack *keep = HP; char p[200]; snprintf(menu_dir, sizeof(menu_dir), "%s", dir); menu_mk = mk;
	snprintf(p, sizeof(p), "%sovl.ghud", dir); menu_ovl = hud_load(p, mk);
	snprintf(p, sizeof(p), "%smenu.ghud", dir); menu_fe = hud_load(p, mk);
	if (keep && hud_npack > 0 && keep->img) HP = keep; else if (menu_ovl >= 0) HP = &hud_pack[menu_ovl];
	return (menu_ovl >= 0 ? 1 : 0) | (menu_fe >= 0 ? 2 : 0);
}
// menu.ghud (front-end + results textures) is only needed outside a match: free it before loading a map,
// load it again when the match ends (same pack slot, so menu_fe keeps its value)
static void menu_fe_unload(void) {
	if (menu_fe < 0 || !hud_pack[menu_fe].img) return;
	HudPack *P = &hud_pack[menu_fe], *keep = HP; HP = P; hud_flush(); HP = keep;
	menu_fe_close(); if (menu_bg_tex) { glDeleteTextures(1, &menu_bg_tex); menu_bg_tex = 0; menu_bg_name[0] = 0; }
	glDeleteTextures(2, P->tex); free(P->img); free(P->node); free(P->field); free(P->occ); P->occ = NULL;   // (render b43: hud.h's cell table)
	P->img = NULL; P->node = NULL; P->field = NULL; P->nimg = P->nnode = P->nfield = 0; menu_hash[menu_fe].built = 0;
}
static int menu_fe_load(void) {
	if (menu_fe < 0 || hud_pack[menu_fe].img) return menu_fe;
	HudPack *keep = HP; int n = hud_npack; char p[200]; snprintf(p, sizeof(p), "%smenu.ghud", menu_dir);
	hud_npack = menu_fe; int r = hud_load(p, menu_mk); hud_npack = n; HP = keep;
	return r;
}
