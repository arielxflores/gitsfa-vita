// Host test for engine/src/tut_ui.h: draws each tutorial panel layout with the recording GL stand-in of
// tools/menu/host (vitaGL.h) and writes one capture per screen; tools/tutorial/host/run.sh rasterises them
// (tools/menu/raster.py) and puts them next to the footage frames (tools/tutorial/host/pairs.py).
// The layout follows tutorial.h tut_hud(): objective band -> button prompts under it -> checklist (right) and
// portrait (left) from y2 = max(band bottom + 48, 96); prompts in the lower half; SUCCESS / warnings in the centre.
#include "vitaGL.h"
#include <math.h>
#include <sys/stat.h>
#define sceGxmTransferFinish() 0
#include "../../../engine/src/tut_ui.h"
static GLuint mk(GLenum t, const char *s) { (void)t; (void)s; return 1; }
static const char *OUT; static char cur[64];
static void shot(const char *name) { char p[256]; snprintf(cur, sizeof(cur), "%s", name); snprintf(p, sizeof(p), "%s/%s.cap", OUT, name); cap_begin(p); }
static void done(void) { char p[256]; cap_end(); snprintf(p, sizeof(p), "%s/%s.tex", OUT, cur); mkdir(p, 0755); cap_dump_textures(p); }
enum { OPR = 12, BAT = 2 };   // tutorial.h FC_OPERATOR, FC_BATOU
// one in-program HUD frame as tut_hud lays it out
static void hud(const char *title, const char *text, unsigned keys, const TuiCheck *ck, int n, int cur, int face, const char *who, const char *cap) {
	tui_begin();
	float by = title ? tui_objective(title, text, 2, 1) : 0;
	if (keys) tui_keys(keys, by + 8, 1);   /* ui b42: as tutorial.h (8 px under the band) */
	float y2 = fmaxf(by + 48, 96);
	if (ck) tui_checklist(ck, n, cur, y2, 2);
	if (face >= 0) tui_portrait(face, who, cap, y2, 2);
}
int main(int argc, char **argv) {
	const char *data = argc > 1 ? argv[1] : "/root/work/tutorial/build/data/"; OUT = argc > 2 ? argv[2] : "/tmp/claude-0/tut/cap";
	int r = menu_init(data, mk); printf("menu_init %d ovl %d\n", r, menu_ovl);
	r = tui_init(data); printf("tui_init %d\n", r);
	menu_time = 3.7f;
	static const TuiCheck CC[6] = { { "Reach Marker" }, { "Sprint" }, { "Crouch" }, { "Walk" }, { "Jump" }, { "Context Movement" } };
	shot("c_basic"); hud("Basic Move", "Use {ls} to reach the marker.", TKB(TK_MOVE) | TKB(TK_LOOK), CC, 6, 0, OPR, "Operator", "Starting Prosthetic Body Restoration Program");
	tui_marker(470, 300, 6, 0, 1); tui_end(); done();
	shot("c_sprint"); hud("Sprint", "Tap {down} while moving forward to sprint.\nThe gate will open if you run for at least 1 second.", TKB(TK_MOVE) | TKB(TK_SPRINT), CC, 6, 1, OPR, "Operator", "Sprint");
	tui_marker(480, 250, 12, 0, 1); tui_end(); done();
	shot("c_crouch"); hud("Crouching", "Press {o} to crouch.\nReach the marker while crouching.", TKB(TK_MOVE) | TKB(TK_CROUCH), CC, 6, 2, OPR, "Operator", "Crouching");
	tui_success(0.6f); tui_end(); done();
	shot("c_spotted"); hud("Walk", "Push {ls} gently to sneak.\nSneak past the enemies and reach the marker.", TKB(TK_WALK), CC, 6, 3, OPR, "Operator", "Walking");
	tui_message("You have been spotted!", 1, NULL); tui_end(); done();
	shot("c_ctx"); hud("Context\nMovement", "Pressing {x} near an obstacle with a {x} icon on it will allow you to vault or climb. Reach the marker.", TKB(TK_MOVE) | TKB(TK_JUMP), CC, 6, 5, OPR, "Operator", "Special Move");
	tui_prompt("Climb", -1, "{x}", 340, 0.9f); tui_end(); done();
	shot("c_end"); tui_begin(); tui_endprog(NULL, NULL, 2); tui_checklist(CC, 6, 6, fmaxf(TUI.band_y1 + 48, 96), 2);
	tui_portrait(BAT, "Batou", "Ending Prosthetic Body Restoration Program", fmaxf(TUI.band_y1 + 48, 96), 2); tui_end(); done();
	static const TuiCheck SC[6] = { { "Primary Weapons" }, { "Eliminate target", 2, 5 }, { "Precision Aiming" }, { "Precision Firing", 0, 5 }, { "Secondary Weapons" }, { "Secondary Weapons Fire", 0, 3 } };
	shot("s_loading"); hud("Loading\nCombat", "Move to the marker and load the weapons program.\nKeep pressing {s} until the gauge is full.", TKB(TK_USE) | TKB(TK_MOVE), NULL, 0, 0, BAT, "Batou", "Firearms Testing Program Loaded");
	tui_prompt("Loading...", 0.35f, "HOLD {s}", 360, 1); tui_end(); done();
	shot("s_hip"); hud("Hip Firing", "Neutralize all targets with your primary weapon.\nPress {r} to fire.", TKB(TK_FIRE), SC, 6, 1, OPR, "Operator", "Primary Weapons"); tui_end(); done();
	static const TuiCheck SC2[4] = { { "Weapon Pick Up" }, { "Sniper Rifles", 1, 3 }, { "Throwing Weapons" }, { "Throwing Weapons Throw", 0, 3 } };
	shot("s_pickup"); hud("Weapon Pick\nUp", "Swap your weapon with the weapon at the center of ground.\nHold {s} near the weapon to swap.", TKB(TK_USE) | TKB(TK_MOVE), SC2, 4, 0, BAT, "Batou", "Sniper Rifles");
	tui_prompt("L118A-S1b", 0.5f, "HOLD {s}  SWAP", 360, 1); tui_end(); done();
	static const TuiExplain EX = { "CYBERBRAIN SKILL", "- Each character has their own unique skill, with two tiers: the two rings at the bottom left of the screen.\n- Small ring, TIER 1: tap {right}. It counts down while it recharges.\n- Big ring, TIER 2: fills with Intel. When it turns orange, hold {right} for the stronger Tier 2.\n- While Tier 1 recharges, a tap uses Tier 2 if it is ready.", { 275, 400 } };
	shot("k_explain"); tui_begin(); tui_explain(&EX, 0, 2, 1); tui_end(); done();
	static const TuiExplain EX6 = { "ENEMY THINKTANK", "- Capturing a terminal in TERMINAL CONQUEST will call in a ThinkTank to support its team.\n- ThinkTanks are equipped with high-tech armor, but can be taken down by hacking its defenses or attacking its ocular sensors.\n- Hacking a ThinkTank will expose your position to enemies.", { 275, 400 } };
	shot("t_explain"); tui_begin(); tui_explain(&EX6, 0, 2, 1); tui_end(); done();
	static const TuiCheck KC[4] = { { "Reach Marker" }, { "Use Skill" }, { "Therm-optic Camo Movement" }, { "Kill Enemies", 0, 3 } };
	shot("k_skill"); hud("Use Skill", "Press {right} to turn on Therm-optic Camo.", TKB(TK_SKILL), KC, 4, 1, OPR, "Operator", "Cyberbrain Skill Training Initiated"); tui_end(); done();
	static const TuiCheck KC2[5] = { { "Reach Marker" }, { "SkillSync : Share" }, { "SkillSync : Download" }, { "Weapon Switching" }, { "Melee" } };
	shot("k_stealth"); hud("Melee", "Get close to the enemy without being spotted.\nPress {left} when prompted to perform a stealth kill.", TKB(TK_MELEE) | TKB(TK_MOVE), KC2, 5, 4, BAT, "Batou", "Melee Combat Initiated");
	tui_prompt("Stealth Kill", -1, "PRESS {left}", 368, 1); tui_end(); done();
	static const TuiCheck TC[3] = { { "Reach Marker" }, { "Hacking Think Tank" }, { "Destroying Think Tank" } };
	shot("t_hack"); hud("Hacking Think\nTanks", "Press {s} when near an enemy Think Tank to hack its defenses.", TKB(TK_USE) | TKB(TK_MOVE), TC, 3, 1, OPR, "Operator", "Enemy Think Tank inbound!");
	tui_prompt("Hacking Think Tank", -1, "PRESS {s}", 360, 1); tui_end(); done();
	shot("t_destroy"); hud("Destroying\nThink Tanks", "Once the Think Tank's defenses have been hacked, it will take far more damage. Destroy it now.", TKB(TK_FIRE) | TKB(TK_AIM), TC, 3, 2, -1, NULL, NULL);
	tui_hack(0.3f, 1234, 1); tui_end(); done();
	shot("t_failed"); hud("Destroying\nThink Tanks", "Once the Think Tank's defenses have been hacked, it will take far more damage. Destroy it now.", TKB(TK_FIRE) | TKB(TK_AIM), TC, 3, 2, BAT, "Batou", "Think Tank disabling failed.");
	tui_message("You failed to destroy enemy Think Tank.", 1, NULL); tui_end(); done();
	static const TuiLoading LD = { "CONTROL TRAINING PROGRAM", NULL, "MOVEMENT CONTROL", "Tier 2 Arm Launcher will fire 2 missiles at once.", "tutorial" };
	shot("l_control"); tui_loading(&LD, 0.45f, 1); done();
	{ static const TuiLoading L2 = { "COMBAT TRAINING PROGRAM", NULL, "SHOOTING RANGE", "Skills have two levels: Tier 1 & Tier 2. Tier 1 consumes less Intel, but provides less powerful benefits.", "tutorial2" },
	    L3 = { "CYBERBRAIN TRAINING PROGRAM", NULL, "SKILL TRAINING", "Tier 2 Cyber Sentries have a much larger cone of detection.", "tutorial3" };
	  shot("l_combat"); tui_loading(&L2, 0.45f, 1); done(); shot("l_cyber"); tui_loading(&L3, 0.45f, 1); done(); }   // b59: each program's art (faces vs the bands)
	shot("x_connect"); tui_begin(); tui_connect(2.8f, 5.0f); tui_end(); done();
	return 0;
}
