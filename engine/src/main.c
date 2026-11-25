// GITS:FA Vita - engine (build 25)
#include <stdarg.h>
#include <vitasdk.h>
#include <vitaGL.h>
#include "ldprof.h"   // perf (b45): load-phase timers (LOADPROF lines), file reads counted per phase
#include "ldbg.h"     // perf (b45): the loader thread (load_level: collision, navigation, floor light beside the world)
#include "dstep.h"    // perf (b45): display-locked match steps (whole VBlanks, late frames paid back gradually)
#include "texcanary.h"   // render: GPU-memory canary (wraps the GL texture / buffer calls of every header below)
#include <vitashark.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>   // offsetof (b58: cfg_load)
#include <math.h>
#include <malloc.h>
#include "player.h"
#include "audio.h"
#include "grade.h"
#include "world.h"   // map rendering: .gmap v2 materials, sky, world effects
#include "model.h"
#include "jobs.h"   // b35: fork-join worker pool on the spare cores (jobs_parallel_for)
#include "skinprobe.h"   // b33: start-up test of the bone palette on the GPU (logging only)
#include "nav.h"
#include "bots.h"
#include "light.h"
#include "sprite.h"
#include "fxscene.h"   // b102: keyed effect scenes of the game (the match start's dive)
#include "fx.h"
#define WCACHE_N 3   // perf (b46): weapon.h keeps the last 3 viewmodels put down (memory permitting: a primary change and a secondary swapped back)
#include "weapon.h"
#include "wsnd.h"     // b107: the clips' sound schedule (wpn_snd.txt)
#include "hud_game.h"
#include "menu.h"
#include "net_lobby.h"   // net (stage 1): local multiplayer session (net_tr.h transport, menu_net.h screens; NET.md)
#include "range.h"
#include "dem.h"
#include "skills.h"   // operator skills (tools/skills/SKILLS.md, INTEGRATION.md)
// b118 (user, playing the tutorial by hand: "i can't damage the think thank in the tutorial, bullets wont even go through the
// horizontal openings in what looks to be a force field around it"): the tank stands in the arena's glass cylinder (5.65 m
// around -12950 / 0). The game's bullet collision there (world_colb: cob_metal) is the cylinder's frame, open from 0.9 to
// 1.85 m over the floor between its posts - the PC's shots go through that slit. The cylinder's player collision (world_colp:
// one wall from the floor to 13 m) keeps the player out, and the port's rule that player walls stop shots too sealed the slit
// (the test tour never noticed: it hurts the tank directly). These walls - vertical, over 10 m tall, 4.5 to 7 m from the
// cylinder's axis - still stop the player, no longer shots. Call after the collision is loaded (as range.h range_col_fix).
// b121 (user: "the force field in the think thank tutorial still doesn't open, is it not supposed to?"): it is. EP14 7:03-7:06:
// the hack runs from outside the cylinder; at SUCCESS its tall glass and roof are gone - the blue floor ring and the white
// frames stay - and the player walks onto the pad and shoots the tank from beside it. Here the cylinder never opened. Its
// parts: three blended world meshes round the pad are hidden (the two roof discs at z 413, and of the two walls the one with
// more triangles: the glass; the other is the frames), its player walls (the 52 above) and the bullet-only frame between
// them stop nothing while it is open. tut_cage_set(1) from the moment the hack of the tutorial's tank starts (the PC's
// SUCCESS ring, the code's digits still running); closed again only with the tank's defences up, no hack running and the
// player outside.
static int tut_cage_tri[1024], tut_cage_ntri, tut_cage_mesh[6], tut_cage_nmesh, tut_cage_is_open; static unsigned char tut_cage_fl[1024];
static void tut_cage_col_fix(void) {
	int n = 0; tut_cage_ntri = tut_cage_nmesh = tut_cage_is_open = 0;
	for (int i = 0; i < nctri; i++) { Tri *t = &ctris[i]; float mx = (t->a.x + t->b.x + t->c.x) / 3 + 12950, my = (t->a.y + t->b.y + t->c.y) / 3, r = hypotf(mx, my); if (r < 450 || r > 700) continue;
		int both = (t->flags & (TF_PLAYER | TF_BULLET)) == (TF_PLAYER | TF_BULLET) && t->mat == 0 && fabsf(t->n.z) <= 0.2f && t->zmax - t->zmin >= 1000;
		int frame = (t->flags & (TF_PLAYER | TF_BULLET)) == TF_BULLET && fabsf(t->n.z) < 0.7f && t->zmin > -300 && t->zmax < 1100;   // (the cob_metal frame of the slit)
		if (!both && !frame) continue;
		if (both) { t->flags &= ~TF_BULLET; n++; }
		if (tut_cage_ntri < 1024) { tut_cage_tri[tut_cage_ntri] = i; tut_cage_fl[tut_cage_ntri++] = t->flags; } }
	int wall[2] = { -1, -1 };
	for (int i = 0; i < world_nmesh; i++) { const WMeshW *m = &w_mesh[i]; float cx = (m->mn[0] + m->mx[0]) * 0.5f + 12950, cy = (m->mn[1] + m->mx[1]) * 0.5f, hx = (m->mx[0] - m->mn[0]) * 0.5f, hy = (m->mx[1] - m->mn[1]) * 0.5f;
		if (m->mode != WMODE_BLEND || hypotf(cx, cy) > 100 || hx < 540 || hx > 620 || hy < 540 || hy > 620) continue;
		if (m->mx[2] - m->mn[2] > 300) { if (wall[0] < 0 || m->ni > w_mesh[wall[0]].ni) { wall[1] = wall[0]; wall[0] = i; } else if (wall[1] < 0) wall[1] = i; }
		else if (m->mn[2] > 300 && tut_cage_nmesh < 5) tut_cage_mesh[tut_cage_nmesh++] = i; }   // the roof's discs (the floor's stays)
	if (wall[0] >= 0 && wall[1] >= 0) tut_cage_mesh[tut_cage_nmesh++] = wall[0];   // (one wall alone: which it is cannot be told - left standing)
	if (n) { col_rebuild_lists(); float d1 = raycast(v3(-11950, 0, -120), v3(-1, 0, 0), 900, NULL, NULL), d2 = raycast(v3(-11950, 0, -20), v3(-1, 0, 0), 900, NULL, NULL);
		logp("tutorial: %d walls of the Think Tank's cylinder let shots through (b118; a level shot from 10 m at 1.3 m height: %s, at 2.3 m: %s)\n", n, d1 < 0 ? "reaches the tank" : "STOPPED", d2 < 0 ? "reaches the tank" : "stopped by the frame"); }
	logp("tutorial: the cylinder opens with the hack (b121): %d collision triangles (%d walls + the frame), %d meshes to hide (the glass wall %d of %d indices, the frames' wall %d stays)\n", tut_cage_ntri, n, tut_cage_nmesh, wall[0], wall[0] >= 0 ? w_mesh[wall[0]].ni : 0, wall[1]);
}
static void tut_cage_set(int open) {
	if (!tut_cage_ntri || open == tut_cage_is_open) return; tut_cage_is_open = open;
	for (int k = 0; k < tut_cage_ntri; k++) { Tri *t = &ctris[tut_cage_tri[k]]; t->flags = open ? (unsigned char)(tut_cage_fl[k] & ~(TF_PLAYER | TF_BULLET)) : tut_cage_fl[k]; }
	col_rebuild_lists();
	for (int k = 0; k < tut_cage_nmesh; k++) { WMeshW *m = &w_mesh[tut_cage_mesh[k]]; if (open) m->flags |= WFLAG_HIDE; else m->flags &= ~WFLAG_HIDE; }
	float d1 = raycast_mask(v3(-11950, 0, -150), v3(-1, 0, 0), 900, TF_PLAYER, NULL), d2 = raycast(v3(-11950, 0, -20), v3(-1, 0, 0), 900, NULL, NULL);
	logp("  tutorial: the Think Tank's cylinder %s (b121; the way to the pad from 10 m: %s, a shot at 2.3 m: %s)\n", open ? "opens - the glass and the roof gone" : "closes again", d1 < 0 ? "free" : "walled", d2 < 0 ? "reaches the tank" : "stopped");
}
static int ledgesweep;   // test switch (b122)
// b122 test (switch ledgesweep): every drawn object of the map that looks like something to climb - its box 50..300 cm above the
// floor beside it, up to 12 m long - approached from 24 places round it, facing it: what does a jump do there (player.h)?
static void ledge_mesh_check(const Player *pl0, const char *stem) {
	int nobj = 0, nbad = 0;
	for (int i = 0; i < world_nmesh; i++) { const WMeshW *m = &w_mesh[i]; float sx = m->mx[0] - m->mn[0], sy = m->mx[1] - m->mn[1];
		if (sx > 1200 || sy > 1200 || (sx < 20 && sy < 20)) continue;
		int tries = 0, ok[4] = { 0, 0, 0, 0 }, why[12] = { 0 }, stand = 0; float rise_min = 1e9f, rise_max = -1e9f, fl = 0;
		for (int k = 0; k < 24; k++) { float u = (k % 6 + 0.5f) / 6; int side = k / 6; V3 p, dir;
			if (side == 0) { p = v3(m->mn[0] - 60, m->mn[1] + sy * u, 0); dir = v3(1, 0, 0); } else if (side == 1) { p = v3(m->mx[0] + 60, m->mn[1] + sy * u, 0); dir = v3(-1, 0, 0); }
			else if (side == 2) { p = v3(m->mn[0] + sx * u, m->mn[1] - 60, 0); dir = v3(0, 1, 0); } else { p = v3(m->mn[0] + sx * u, m->mx[1] + 60, 0); dir = v3(0, -1, 0); }
			int ti = -1; float fd = raycast_mask(v3(p.x, p.y, m->mn[2] + 60), v3(0, 0, -1), 400, TF_PLAYER, &ti); if (fd < 0 || ti < 0 || fabsf(ctris[ti].n.z) < P_WALKABLE) continue;
			p.z = m->mn[2] + 60 - fd + 1; if (body_blocked(p, P_HEIGHT, 2.0f)) continue; float rise = m->mx[2] - p.z; if (rise < 50 || rise > 300) continue;
			Player b = *pl0; b.pos = p; b.grounded = 1; b.height = P_HEIGHT; b.mantle_t = 0; b.slide_t = 0; b.yaw = atan2f(dir.y, dir.x); V3 dest; float peak; fl = p.z;
			tries++; if (rise < rise_min) rise_min = rise; if (rise > rise_max) rise_max = rise;
			int sm = smf_n ? smf_pick(&b, dir) : -1; if (sm >= 0) { ok[3]++; continue; }
			int kd = player_ledge(&b, dir, &dest, &peak); if (kd) ok[kd]++; else { why[ledge_why < 12 ? ledge_why : 0]++; if (ledge_why == 1) { b.pos.x += dir.x * 40; b.pos.y += dir.y * 40; if (!body_blocked(b.pos, P_HEIGHT, 2.0f)) stand++; } } }
		if (!tries) continue; nobj++; int good = ok[1] + ok[2] + ok[3]; if (good * 2 < tries) nbad++;
		char w[120]; int o = 0; w[0] = 0; for (int k = 0; k < 12; k++) if (why[k]) o += snprintf(w + o, sizeof(w) - o, " %d:%d", k, why[k]);
		float ct = raycast_mask(v3((m->mn[0] + m->mx[0]) / 2, (m->mn[1] + m->mx[1]) / 2, m->mx[2] + 40), v3(0, 0, -1), 500, TF_PLAYER, NULL);
		logp("ledgeobj %s mesh %3d: box %6.0f %6.0f .. %6.0f %6.0f, %4.0f x %4.0f cm, top %4.0f..%4.0f above the floor (%.0f), mode %d tris %d: %2d places, climb %d vault %d maneuver %d, no:%s (walked through at 40 cm: %d); collision under its middle %s%.0f below its top%s\n",
			stem, i, m->mn[0], m->mn[1], m->mx[0], m->mx[1], sx, sy, rise_min, rise_max, fl, m->mode, m->ni / 3, tries, ok[1], ok[2], ok[3], w, stand, ct < 0 ? "none " : "", ct < 0 ? 0 : ct - 40, good * 2 < tries ? "   <<<" : ""); }
	logp("ledgeobj %s: %d drawn objects of a climbable height, %d where a jump does nothing from most places\n", stem, nobj, nbad);
}
static int seekview;   // test switch (b117)
static float w_floor_under(float x, float y, float z) { float d = raycast(v3(x, y, z + 25), v3(0, 0, -1), 45, NULL, NULL); return d < 0 ? -1e9f : z + 25 - d; }   // b117 (world_ground_patches)
#include "ttm.h"      // Think Tanks (thinktank.h) + Terminal Conquest (ttm.h): API and notes at the top of both headers
#include "wpn.h"      // weapon presentation: sight blend modes, sniper scope, melee, pick-ups (weapons agent)
#include "hands.h"    // b99: the hands alone - the skill gesture, the bomb in the hands (Demolition)
#include "vision.h"   // teammates through walls, revealed enemies, camo looks, eye glints (vision.h header)
#include "bparts.h"   // b80: destruction deaths - a headshot kill pops the head off (the Paz video EP19), heavy hits an arm / a leg
#include "wobj.h"     // b87: the maps' animated objects (Train Station's train: model, timeline, sounds, dust, the pit kill)
static int nbot_full;   // the bots made at start; maps with practice targets (range.h) run with none

// b106 (the Vita, b105-4): the game quit while loading the 12th map of a session (Underground Base) with the heap's arena at
// 114640 KB of these 114688 - 87.6 MB in use and ~27 MB of gaps between the blocks (that gap settles at 20-27 MB from the
// fourth map on; the use itself has crept up 5.6 MB since b86). Main RAM had room all along: vitaGL leaves TV_RAM_FREE of
// it to the system for the movie player, and of those 24 MB no log of any run shows more than 2 MB taken (the player's own
// allocations come from this heap, its frames from phycont). 10 MB of that go to the heap: 122 MB here, TV_RAM_FREE 14 MB
// (tut_video.h) - vitaGL's own pool stays exactly as large as it was. log_mem prints the system's free main RAM now.
int _newlib_heap_size_user = 122 * 1024 * 1024;
#define VDIR "ux0:data/gitsfa/"
#define LOG_PATH VDIR "boot.log"
#include "alog.h"   // render (b35): logp never blocks the frame (RAM ring + a low-priority writer thread)
static int g_log;    // render (b35): 1 while boot.log is open (alog.h)
static void logp(const char *fmt, ...) {
	if (!g_log || alog_mute) return; va_list a; va_start(a, fmt); alog_vprintf(fmt, a); va_end(a);
}

typedef struct { int tex, lm, alpha, nv, ni; float mn[3], mx[3]; GLuint vbo, ibo; } Mesh;
static GLuint *texs; static int ntex; static Mesh *meshes; static int nmesh; static unsigned *tex_bytes, *tex_sum; static unsigned char **tex_ptr;

static const char *VS =
"void main(float3 pos, float2 uv0, float2 uv1, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2) {"
" float4 p = float4(pos,1); vpos = float4(dot(m0,p), dot(m1,p), dot(m2,p), dot(m3,p)); t0 = uv0; t1 = uv1; fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
static const char *FS_OPAQUE_PLAIN =
"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float fog : TEXCOORD2,"
" uniform sampler2D dif : TEXUNIT0, uniform sampler2D lm : TEXUNIT1, uniform float4 fogc, uniform float lmon) : COLOR {"
" float3 c = tex2D(dif, t0).rgb; float3 l = lmon > 0.5 ? tex2D(lm, t1).rgb * 2.0 : float3(1,1,1);"
" return float4(lerp(c*l, fogc.rgb, fog), 1); }";
static const char *FS_ALPHA_PLAIN =
"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float fog : TEXCOORD2,"
" uniform sampler2D dif : TEXUNIT0, uniform sampler2D lm : TEXUNIT1, uniform float4 fogc, uniform float lmon) : COLOR {"
" float4 c = tex2D(dif, t0); if (c.a < 0.5) discard; float3 l = lmon > 0.5 ? tex2D(lm, t1).rgb * 2.0 : float3(1,1,1);"
" return float4(lerp(c.rgb*l, fogc.rgb, fog), 1); }";
static GLuint prog[2]; static GLint u_mvp[2][4], u_fog[2], u_lmon[2], u_fogp[2]; static float fogp[4]; static int map_audio_pending;
static int shader_fail;
// the shader compiler's own messages (vitaGL is built without HAVE_SHARK_LOG, so glGetShaderInfoLog stays empty)
static void shk_log(const char *msg, shark_log_level lv, int line) {
	logp("  shacc %s (line %d): %s\n", lv == SHARK_LOG_ERROR ? "error" : lv == SHARK_LOG_WARNING ? "warning" : "info", line, msg ? msg : "");
}
static GLuint mkshader(GLenum t, const char *src) {
	GLuint s = glCreateShader(t == GL_VERTEX_SHADER ? GL_CG_VERTEX_SHADER_EXT : GL_CG_FRAGMENT_SHADER_EXT); GLint len = strlen(src), ok = 0;
	glShaderSource(s, 1, &src, &len); shark_install_log_cb(shk_log); glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[2048] = {0}; glGetShaderInfoLog(s, sizeof(log) - 1, NULL, log);
		logp("%s shader failed to compile:\n%s\n", t == GL_VERTEX_SHADER ? "vertex" : "fragment", log); shader_fail = 1;
		logp("  source: %s\n", src); }
	else logp("%s shader ok\n", t == GL_VERTEX_SHADER ? "vertex" : "fragment");
	return s;
}
static void mkprog(int i, const char *fs) {
	prog[i] = glCreateProgram();
	GLuint vs = mkshader(GL_VERTEX_SHADER, VS), fsh = mkshader(GL_FRAGMENT_SHADER, fs);
	if (shader_fail) return;
	glAttachShader(prog[i], vs); glAttachShader(prog[i], fsh);
	glBindAttribLocation(prog[i], 0, "pos"); glBindAttribLocation(prog[i], 1, "uv0"); glBindAttribLocation(prog[i], 2, "uv1");
	logp("linking program %d\n", i);
	glLinkProgram(prog[i]);
	{ GLint lk = 0; glGetProgramiv(prog[i], GL_LINK_STATUS, &lk); logp("program %d link %s\n", i, lk ? "ok" : "FAILED"); }
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; u_mvp[i][r] = glGetUniformLocation(prog[i], nm); } u_fog[i] = glGetUniformLocation(prog[i], "fogc"); u_lmon[i] = glGetUniformLocation(prog[i], "lmon"); u_fogp[i] = glGetUniformLocation(prog[i], "fogp");
	glUseProgram(prog[i]);
	glUniform1i(glGetUniformLocation(prog[i], "dif"), 0); glUniform1i(glGetUniformLocation(prog[i], "lm"), 1); glUniform1i(glGetUniformLocation(prog[i], "lut"), 2);
}

static const char *VS_FLAT = "void main(float2 pos, float4 out vpos : POSITION) { vpos = float4(pos, 0, 1); }";
static const char *FS_FLAT = "float4 main(uniform float4 col) : COLOR { return col; }";
static GLuint prog_flat; static GLint u_col;
static void mkflat(void) {
	GLuint vs = mkshader(GL_VERTEX_SHADER, VS_FLAT), fsh = mkshader(GL_FRAGMENT_SHADER, FS_FLAT);
	prog_flat = glCreateProgram(); glAttachShader(prog_flat, vs); glAttachShader(prog_flat, fsh);
	glBindAttribLocation(prog_flat, 0, "pos"); glLinkProgram(prog_flat); u_col = glGetUniformLocation(prog_flat, "col");
}
static void rect(float x0, float y0, float x1, float y1, const float *c) { // screen pixels, origin top-left
	float v[8] = { x0 / 480 - 1, 1 - y0 / 272, x1 / 480 - 1, 1 - y0 / 272, x0 / 480 - 1, 1 - y1 / 272, x1 / 480 - 1, 1 - y1 / 272 };
	glUniform4fv(u_col, 1, c); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, v); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
// 7-segment digits for the HUD (placeholder until the game's own HUD art is ported)
static void digit(float x, float y, float h, int d, const float *c) {
	static const unsigned char seg[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
	float w = h * 0.5f, t = h * 0.12f, m = y + h / 2; unsigned g = seg[d % 10];
	if (g & 1) rect(x, y, x + w, y + t, c);            if (g & 2) rect(x + w - t, y, x + w, m, c);
	if (g & 4) rect(x + w - t, m, x + w, y + h, c);    if (g & 8) rect(x, y + h - t, x + w, y + h, c);
	if (g & 16) rect(x, m, x + t, y + h, c);           if (g & 32) rect(x, y, x + t, m, c);
	if (g & 64) rect(x, m - t / 2, x + w, m + t / 2, c);
}
static __attribute__((unused)) void number(float x, float y, float h, int n, int right, const float *c) {
	char b[12]; int l = sprintf(b, "%d", n); float adv = h * 0.65f; if (right) x -= l * adv;
	for (int i = 0; i < l; i++) digit(x + i * adv, y, h, b[i] - '0', c);
}

// 5x7 bitmap text (menus, map names, test readouts): every lit pixel is a quad, one draw call per string
#include "font5x7.h"
static float text_v[96 * 35 * 12];
static float text_w(const char *s, float px) { return strlen(s) * 6 * px - px; }
static void text(float x, float y, float px, const char *s, const float *c) {   // px: size of one font pixel on screen
	int n = 0;
	for (int i = 0; s[i] && i < 96; i++) {
		char ch = s[i]; if (ch >= 'a' && ch <= 'z') ch -= 32; unsigned long long b = 0;
		for (unsigned k = 0; k < sizeof(FONT5x7) / sizeof(FONT5x7[0]); k++) if (FONT5x7[k].c == ch) { b = FONT5x7[k].bits; break; }
		float ox = x + i * 6 * px;
		for (int r = 0; r < 7; r++) for (int cc = 0; cc < 5; cc++) if (b >> (34 - (r * 5 + cc)) & 1) {
			float x0 = (ox + cc * px) / 480 - 1, x1 = (ox + (cc + 1) * px) / 480 - 1, y0 = 1 - (y + r * px) / 272, y1 = 1 - (y + (r + 1) * px) / 272;
			float *v = &text_v[n * 12]; v[0] = x0; v[1] = y0; v[2] = x1; v[3] = y0; v[4] = x0; v[5] = y1; v[6] = x1; v[7] = y0; v[8] = x1; v[9] = y1; v[10] = x0; v[11] = y1; n++;
		}
	}
	if (!n) return;
	glUniform4fv(u_col, 1, c); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, text_v); glDrawArrays(GL_TRIANGLES, 0, n * 6);
}
static void textf(float x, float y, float px, const float *c, const char *fmt, ...) { char b[128]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof(b), fmt, a); va_end(a); text(x, y, px, b, c); }

// first-person arms + weapon: the current loadout slot (weapon.h); VM / VA / C_* name its model, animation state and clips
static int vm_ok, md_ok, wcur = WS_PRIMARY;
#define PEYE (pl.eye > 1 ? pl.eye : P_EYE)   // the player's eye height (player.h eases it between standing / crouched / sliding)
#define WSC (&wsl[wcur])
#define CY_OFF_X 0.0f
#define CY_OFF_Y -4.5f
#define CY_OFF_Z 0.0f
#define CY_LOFF_X 7.0f
#define CY_LOFF_Y 3.0f
#define CY_LOFF_Z 0.0f
#define CY_LTWIST 35.0f
#define CY_RTHUMB -70.0f
#define WD (&wdef[WSC->def >= 0 ? WSC->def : 0])
#define VM (WSC->m)
#define VA (WSC->a)
#define C_IDLE (WSC->c[CL_IDLE])
#define C_RUN (WSC->c[CL_RUN])
#define C_SPRINT (WSC->c[CL_SPRINT])
#define C_FIRE (WSC->c[CL_FIRE])
#define C_FIRE_ADS (WSC->c[CL_FIRE_ADS])
#define C_ADS_WALK (WSC->c[CL_WALK])
#define C_RELOAD (WSC->c[CL_RELOAD])
#define C_RELOAD_E (WSC->c[CL_RELOAD_E])
#define C_DRAW (WSC->c[CL_DRAW])
#define C_JUMP (WSC->c[CL_JUMP])
#define C_FALL (WSC->c[CL_FALL])
#define C_LAND (WSC->c[CL_LAND])
#define C_ADS_IDLE (WSC->c[CL_ADS_IDLE])
#define vm_grip (WSC->grip)
#define vm_mz (WSC->mz)
#define vm_hud1 (WSC->hud1)
// weapon switching / quick actions (knife on D-pad left, grenade on D-pad up)
static struct { int sw_to, quick_from, rl_phase, gren_state, gren_thrown, bolt_pending; float sw_t, draw_t, bolt_t, pump_t, quick_t, cook_t, gren_t; } WP = { -1, -1 };
static float self_dmg, self_dmg_dir; static int feed_wicon = 2, killed_by_icon = 2; static const char *pvoice = "motoko";   // the player operator's voice set (act_ / k_ / d_ <name>)
static MBone gren_pose[64]; static float gren_cam[16];   // the grenade's pose at release (viewmodel space) and the camera->world matrix then
typedef struct { int tex, blend, nv, ni; GLuint vbo, ibo; } WMesh;
static WMesh *wm; static int nwm; static GLuint *wtex; static float muzzle[3];
static int load_weapon(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned ver, nt, nm; fread(mg, 4, 1, f); fread(&ver, 4, 1, f); fread(&nt, 4, 1, f); fread(&nm, 4, 1, f);
	wtex = calloc(nt, sizeof(GLuint)); glGenTextures(nt, wtex); void *buf = malloc(2 << 20);
	for (unsigned i = 0; i < nt; i++) {
		unsigned short w, h; unsigned char fmt, nmp; fread(&w, 2, 1, f); fread(&h, 2, 1, f); fread(&fmt, 1, 1, f); fread(&nmp, 1, 1, f);
		glBindTexture(GL_TEXTURE_2D, wtex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmp > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		for (int m = 0; m < nmp; m++) { unsigned sz; fread(&sz, 4, 1, f); fread(buf, sz, 1, f);
			glCompressedTexImage2D(GL_TEXTURE_2D, m, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w >> m, h >> m, 0, sz, buf); sceGxmTransferFinish(); }
	}
	free(buf); nwm = nm; wm = calloc(nm, sizeof(WMesh));
	for (unsigned i = 0; i < nm; i++) {
		WMesh *m = &wm[i]; fread(&m->tex, 4, 1, f); fread(&m->blend, 4, 1, f); fread(&m->nv, 4, 1, f); fread(&m->ni, 4, 1, f);
		void *v = malloc(m->nv * 20), *ix = malloc(m->ni * 2); fread(v, 20, m->nv, f); fread(ix, 2, m->ni, f); if (m->ni & 1) { short pad; fread(&pad, 2, 1, f); }
		glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, m->nv * 20, v, GL_STATIC_DRAW);
		glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->ni * 2, ix, GL_STATIC_DRAW);
		free(v); free(ix);
	}
	char nmn[16]; float p[3]; for (int k = 0; k < 3; k++) { if (fread(nmn, 16, 1, f) != 1) break; fread(p, 12, 1, f); if (!strcmp(nmn, "wea_bi")) memcpy(muzzle, p, 12); }
	fclose(f); return 0;
}

static long map_tris;
typedef struct { FILE *f; char base[128]; int piece; } PF;
static PF pf;
static size_t pread_(void *d, size_t n) {
	size_t got = 0;
	while (got < n && pf.f) {
		size_t r = fread((char *)d + got, 1, n - got, pf.f); got += r;
		if (got < n) { fclose(pf.f); pf.f = NULL; if (pf.piece >= 0) { char nm[160]; sprintf(nm, "%s.%03d", pf.base, ++pf.piece); pf.f = fopen(nm, "rb"); } }
	}
	return got;
}
#define fread(p, s, n, f) pread_((p), (s) * (n))
#define fseek(f, o, w) do { char _sk[4]; pread_(_sk, (o)); } while (0)
static FILE *popen_(const char *path) {
	strcpy(pf.base, path); pf.piece = -1; pf.f = fopen(path, "rb");
	if (!pf.f) { char nm[160]; sprintf(nm, "%s.000", path); pf.piece = 0; pf.f = fopen(nm, "rb"); }
	return pf.f;
}
// Keep texture coordinates small. Some maps tile textures with UVs of 30..270000; at those magnitudes the Vita's
// texture units lose precision and the surface turns into speckle/mosaic noise. Every triangle's UVs are shifted by a
// whole multiple of 8 (textures repeat, so the picture is unchanged); vertices shared by triangles that need
// different shifts are duplicated. Triangles that repeat a texture 64+ times across themselves would only ever show
// noise: they get one texel. Returns the new vertex count (vertices: 7 floats, uv at [3],[4]).
static int uv_stats_meshes, uv_stats_added;
static int uv_rebase(float **pv, int nv, unsigned short *ix, int ni) {
	float *v = *pv, mx = 0;
	for (int i = 0; i < nv; i++) { float a = fabsf(v[i * 7 + 3]), b = fabsf(v[i * 7 + 4]); if (a > mx) mx = a; if (b > mx) mx = b; }
	if (mx <= 8) return nv;
	int cap = 1; while (cap < ni * 2) cap <<= 1;
	int *hk = malloc(sizeof(int) * cap * 4);   // key: vertex, su, sv, flat-triangle (or -1); value: new index
	int *hv = malloc(sizeof(int) * cap); for (int i = 0; i < cap; i++) hv[i] = -1;
	float *nvb = malloc(sizeof(float) * 7 * ni); int nn = 0;
	for (int t = 0; t < ni / 3; t++) {
		float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
		for (int k = 0; k < 3; k++) { const float *q = v + ix[t * 3 + k] * 7; u0 = fminf(u0, q[3]); v0 = fminf(v0, q[4]); u1 = fmaxf(u1, q[3]); v1 = fmaxf(v1, q[4]); }
		int su = (int)floorf(u0 / 8) * 8, sv = (int)floorf(v0 / 8) * 8, flat = (u1 - u0 > 64 || v1 - v0 > 64) ? t : -1;
		for (int k = 0; k < 3; k++) {
			int vi = ix[t * 3 + k]; unsigned h = ((unsigned)vi * 73856093u) ^ ((unsigned)su * 19349663u) ^ ((unsigned)sv * 83492791u) ^ ((unsigned)flat * 2654435761u);
			int slot = h & (cap - 1);
			while (hv[slot] >= 0 && !(hk[slot * 4] == vi && hk[slot * 4 + 1] == su && hk[slot * 4 + 2] == sv && hk[slot * 4 + 3] == flat)) slot = (slot + 1) & (cap - 1);
			if (hv[slot] < 0) {
				if (nn >= 65535) { free(hk); free(hv); free(nvb); return nv; }   // would not fit 16-bit indices: leave this mesh as it is
				hk[slot * 4] = vi; hk[slot * 4 + 1] = su; hk[slot * 4 + 2] = sv; hk[slot * 4 + 3] = flat; hv[slot] = nn;
				float *o = nvb + nn * 7; memcpy(o, v + vi * 7, 28); o[3] -= su; o[4] -= sv; nn++;
			}
			ix[t * 3 + k] = (unsigned short)hv[slot];
		}
		if (flat >= 0) { // one texel for the whole triangle
			float cu = 0, cv = 0; for (int k = 0; k < 3; k++) { float *o = nvb + ix[t * 3 + k] * 7; cu += o[3] - floorf(o[3]); cv += o[4] - floorf(o[4]); }
			for (int k = 0; k < 3; k++) { float *o = nvb + ix[t * 3 + k] * 7; o[3] = cu / 3; o[4] = cv / 3; }
		}
	}
	free(hk); free(hv); free(v); *pv = nvb; uv_stats_meshes++; uv_stats_added += nn - nv; return nn;
}
static void loading_progress(float p, const char *status);
static unsigned *tex_meta;   // per map texture: w | h << 12 | fmt << 24 | mip count << 28 (texture dumps)
static __attribute__((unused)) int load_map(const char *path) {
	FILE *f = popen_(path); if (!f) { logp("open fail %s\n", path); return -1; }
	char mg[4]; unsigned ver; fread(mg, 4, 1, f); fread(&ver, 4, 1, f); fread(&ntex, 4, 1, f); fread(&nmesh, 4, 1, f);
	texs = calloc(ntex, sizeof(GLuint)); glGenTextures(ntex, texs); tex_bytes = calloc(ntex, 4); tex_sum = calloc(ntex, 4); tex_ptr = calloc(ntex, sizeof(void *)); free(tex_meta); tex_meta = calloc(ntex, 4);
	void *buf = malloc(4 << 20);
	for (int i = 0; i < ntex; i++) {
		if ((i & 15) == 0) loading_progress(0.05f + 0.55f * i / (ntex ? ntex : 1), "LOADING TEXTURES");
		unsigned short w, h; unsigned char fmt, nm; fread(&w, 2, 1, f); fread(&h, 2, 1, f); fread(&fmt, 1, 1, f); fread(&nm, 1, 1, f);
		glBindTexture(GL_TEXTURE_2D, texs[i]); tex_meta[i] = w | (unsigned)h << 12 | (unsigned)(fmt & 15) << 24 | (unsigned)(nm & 15) << 28;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nm > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		for (int m = 0; m < nm; m++) {
			unsigned sz; fread(&sz, 4, 1, f); fread(buf, sz, 1, f); tex_bytes[i] += sz;
			glCompressedTexImage2D(GL_TEXTURE_2D, m, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w >> m, h >> m, 0, sz, buf); sceGxmTransferFinish();
		}
	}
	free(buf);
	logp("textures uploaded: %d\n", ntex);
	meshes = calloc(nmesh, sizeof(Mesh));
	long tris = 0;
	for (int i = 0; i < nmesh; i++) {
		Mesh *m = &meshes[i];
		fread(&m->tex, 4, 1, f); fread(&m->lm, 4, 1, f); fread(&m->alpha, 4, 1, f); fread(&m->nv, 4, 1, f); fread(&m->ni, 4, 1, f);
		fread(m->mn, 12, 1, f); fread(m->mx, 12, 1, f);
		int vb = m->nv * 28, ib = m->ni * 2; void *v = malloc(vb), *ix = malloc(ib);
		fread(v, vb, 1, f); fread(ix, ib, 1, f); if (m->ni & 1) fseek(f, 2, SEEK_CUR);
		{ float *fv = v; m->nv = uv_rebase(&fv, m->nv, ix, m->ni); v = fv; vb = m->nv * 28; }
		glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, vb, v, GL_STATIC_DRAW);
		glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, ib, ix, GL_STATIC_DRAW);
		free(v); free(ix); tris += m->ni / 3;
	}
	#undef fread
	#undef fseek
	if (pf.f) fclose(pf.f); (void)f; map_tris = tris; logp("map: %d tex, %d meshes, %ld tris; texture coordinates rebased in %d meshes (+%d vertices)\n", ntex, nmesh, tris, uv_stats_meshes, uv_stats_added); uv_stats_meshes = uv_stats_added = 0;
	return 0;
}

static __attribute__((unused)) void unload_map(void) {
	for (int i = 0; i < nmesh; i++) { glDeleteBuffers(1, &meshes[i].vbo); glDeleteBuffers(1, &meshes[i].ibo); }
	free(meshes); meshes = NULL; nmesh = 0;
	if (ntex) glDeleteTextures(ntex, texs); free(texs); texs = NULL; ntex = 0; free(tex_bytes); free(tex_sum); free(tex_ptr); tex_bytes = tex_sum = NULL; tex_ptr = NULL;
}
// test: where each map texture lives in graphics memory, whether any two overlap, and whether their contents change after loading
static unsigned tex_hash(const unsigned char *p, unsigned n) { unsigned h = 2166136261u; for (unsigned i = 0; i + 4 <= n; i += 16) { unsigned w; memcpy(&w, p + i, 4); h = (h ^ w) * 16777619u; } return h; }
static int tex_ord_cmp(const void *a, const void *b) { const unsigned char *pa = tex_ptr[*(const int *)a], *pb = tex_ptr[*(const int *)b]; return pa < pb ? -1 : pa > pb; }
static __attribute__((unused)) void tex_check(int again) {
	int changed = 0, overl = 0, nulls = 0;
	for (int i = 0; i < ntex; i++) {
		glBindTexture(GL_TEXTURE_2D, texs[i]); unsigned char *p = vglGetTexDataPointer(GL_TEXTURE_2D);
		if (!p) { nulls++; continue; }
		unsigned h = tex_hash(p, tex_bytes[i]);
		if (!again) { tex_ptr[i] = p; tex_sum[i] = h; }
		else if (p != tex_ptr[i] || h != tex_sum[i]) { if (changed < 6) logp("  texture %d (%u bytes) %s since loading\n", i, tex_bytes[i], p != tex_ptr[i] ? "moved" : "CHANGED"); changed++; }
	}
	if (!again) {
		int *ord = malloc(sizeof(int) * ntex); for (int i = 0; i < ntex; i++) ord[i] = i; qsort(ord, ntex, sizeof(int), tex_ord_cmp);
		for (int k = 1; k < ntex; k++) { int a = ord[k - 1], b = ord[k]; if (tex_ptr[a] && tex_ptr[a] + tex_bytes[a] > tex_ptr[b]) { if (overl < 6) logp("  textures %d and %d OVERLAP in memory\n", a, b); overl++; } }
		free(ord);
		logp("texture check: %d textures, %d overlapping, %d without data\n", ntex, overl, nulls);
	} else logp("texture check after play: %d of %d textures changed\n", changed, ntex);
}
// the 17 maps in the game's data (map.dat FixedMap/Fix_*); names until the game's string table is decoded
typedef struct { const char *stem, *name; } MapInfo;   /* databuilder (b47): the fog columns moved to each map's data (<map>.glgt FOGP trailer, from its World.ini; map_fog) */
static const MapInfo MAPS[] = {
	{ "downtown", "Downtown" },
	{ "berutalube", "Berutalube" },
	{ "ecenter", "E-Center" },
	{ "ghost_storage", "Ghost Storage" },
	{ "headquarter", "Headquarter" },
	{ "port", "Port" },
	{ "pss9", "PSS9" },
	{ "ruinednight", "Ruined Night" },
	{ "ruinedtokyo", "Ruined Tokyo" },
	{ "trainstation", "Train Station" },
	{ "underground", "Underground" },
	{ "urban", "Urban" },
	{ "zioplant", "Geofront" },
	{ "shooting", "Shooting Range" },
	{ "tutorial", "Tutorial 1" },
	{ "tutorial2", "Tutorial 2" },
	{ "tutorial3", "Tutorial 3" },
};
#define NMAPS ((int)(sizeof(MAPS) / sizeof(MAPS[0])))
#ifdef HG_HOST
// perf (b45, host test only): a hash of what load_level leaves behind (collision, navigation, floor light: field by field, struct
// padding left out) so a change to how a level loads can be checked to give the same state ("LEVELHASH" lines in boot.log)
static unsigned lvh_fnv(unsigned h, const void *p, size_t n) { const unsigned char *b = p; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u; return h; }
static void level_hash_log(const char *stem) {
	unsigned hc = 2166136261u, hn = 2166136261u, hl = 2166136261u;
	for (int i = 0; i < nctri; i++) { const Tri *t = &ctris[i]; hc = lvh_fnv(hc, &t->a, 48); hc = lvh_fnv(hc, &t->zmin, 8); hc = lvh_fnv(hc, &t->mat, 1); hc = lvh_fnv(hc, &t->flags, 1); }
	if (cstart) { int nc = gw * gh; hc = lvh_fnv(hc, cstart, 4 * (nc + 1)); hc = lvh_fnv(hc, pstart, 4 * (nc + 1)); hc = lvh_fnv(hc, clist, 4 * cstart[nc]); hc = lvh_fnv(hc, plist, 4 * pstart[nc]);
		if (czr) hc = lvh_fnv(hc, czr, 4 * cstart[nc]); if (pzr) hc = lvh_fnv(hc, pzr, 4 * pstart[nc]); }
	hc = lvh_fnv(hc, &gx0, 4); hc = lvh_fnv(hc, &gy0, 4); hc = lvh_fnv(hc, &gw, 4); hc = lvh_fnv(hc, &gh, 4); hc = lvh_fnv(hc, &col_zmin, 4); hc = lvh_fnv(hc, &zq_base, 4); hc = lvh_fnv(hc, &zq_inv, 4);
	hc = lvh_fnv(hc, &nspawn, 4); if (nspawn) hc = lvh_fnv(hc, spawns, sizeof(Spawn) * nspawn);
	for (int i = 0; i < nnav; i++) { const NavTri *t = &navt[i]; hn = lvh_fnv(hn, t->v, 36); hn = lvh_fnv(hn, t->nb, 12); hn = lvh_fnv(hn, t->c, 12); hn = lvh_fnv(hn, &t->blocked, 1); }
	if (nnav && nav_bstart) { int nb = nav_bw * nav_bh; hn = lvh_fnv(hn, nav_bstart, 4 * (nb + 1)); hn = lvh_fnv(hn, nav_blist, 4 * nav_bstart[nb]); }
	if (nav_bsum) hn = lvh_fnv(hn, nav_bsum, 4 * (size_t)(nav_bw + 1) * (nav_bh + 1));
	if (nav_comp) hn = lvh_fnv(hn, nav_comp, 4 * (size_t)nnav); if (nav_scc) hn = lvh_fnv(hn, nav_scc, 4 * (size_t)nnav); hn = lvh_fnv(hn, &nav_nscc, 4);
	if (nav_swp) hn = lvh_fnv(hn, nav_swp, nnav);
	if (nav_reach && nav_reach_w > 0) hn = lvh_fnv(hn, nav_reach, 4 * (size_t)nav_reach_w * (nav_nscc > 0 ? nav_nscc : 1));
	int nlc = lg_w * lg_h; if (nlc > 0) { hl = lvh_fnv(hl, lg_start, 4 * nlc); hl = lvh_fnv(hl, lg_n, nlc); long tot = 0; for (int i = 0; i < nlc; i++) tot += lg_n[i];
		for (long k = 0; k < tot; k++) { hl = lvh_fnv(hl, &lg_f[k].z, 2); hl = lvh_fnv(hl, &lg_f[k].r, 3); } }
	hl = lvh_fnv(hl, lg_avg, 12); hl = lvh_fnv(hl, lg_gain, 12); hl = lvh_fnv(hl, lg_amb, 12);
	logp("LEVELHASH %s: collision %08x (%d tris, %d spawns) navigation %08x (%d tris) light %08x\n", stem, hc, nctri, nspawn, hn, nnav, hl);
}
#endif
// loads map + collision + navigation mesh; returns <0 when the map or its collision is missing
// perf (b45): the level's CPU-only files load on the loader thread (ldbg.h) while the main thread loads the world (GL): collision
// (with its grids and spawn fixes), then the navigation mesh (only with a collision, as before), then the floor light. Same
// functions, same files, same order among themselves: the same state (host check: LEVELHASH lines). Nothing reads them
// before load_level's ldbg_wait (the loading screen does not; bots, sounds and modes start after load_level).
typedef struct { char col[96], nav[96], lgt[96]; int rcol, rnav, rlgt; uint64_t us[3]; } LvBg;
static LvBg lvbg;
static void level_bg_job(void *ctx) {   // (loader thread: no GL, no logp, no rand)
	LvBg *b = ctx; uint64_t t0 = sceKernelGetSystemTimeWide();
	b->rcol = load_col(b->col); uint64_t t1 = sceKernelGetSystemTimeWide();
	b->rnav = b->rcol < 0 ? -9 : nav_load(b->nav); uint64_t t2 = sceKernelGetSystemTimeWide();
	b->rlgt = b->rcol < 0 ? -9 : light_load(b->lgt); uint64_t t3 = sceKernelGetSystemTimeWide();
	b->us[0] = t1 - t0; b->us[1] = t2 - t1; b->us[2] = t3 - t2;
}
// perf (b45): lite = 1 (the start-up): collision, navigation and light only, for the start-up's player and bot placement; the
// world (textures, meshes, sky), minimap, grading and range wait for the first map actually played (world_lite stays 1 until
// then: START always loads its map; main asks for the start-up map when nothing else will, see world_lite below)
static int world_lite;
static float view_far = 12000;   // b105: where the view ends (cm) - past the map's furthest corner (load_level_ex)
static int load_level_ex(int mi, int lite) {
	char p[128]; int r;
	snprintf(lvbg.col, sizeof(lvbg.col), VDIR "%s.gcol", MAPS[mi].stem); snprintf(lvbg.nav, sizeof(lvbg.nav), VDIR "%s.gnav", MAPS[mi].stem);
	snprintf(lvbg.lgt, sizeof(lvbg.lgt), VDIR "%s.glgt", MAPS[mi].stem);
	ldbg_start(level_bg_job, &lvbg);   // perf (b45): collision, navigation, light on the loader thread from here to ldbg_wait below
	if (!lite) {
#ifdef WCACHE_N   // perf (b46): the kept viewmodels (weapon.h) go first when the old map's release leaves less VRAM than a big map wants
		{ LDP_PUSH(LDP_UNLOAD); world_unload(); LDP_POP(); int kept = 0; for (int k = 0; k < WCACHE_N; k++) kept += wcache[k].ok;
		  if (kept) { unsigned vf = (unsigned)(vglMemFree(VGL_MEM_VRAM) >> 20); int d0 = wcache_dropped; wcache_trim(30u << 20);   // (vitaGL frees the old map's memory a few frames later: Vita3K showed 35-42 MB here, ~40 MB free in play)
			logp("viewmodel cache: %d kept, VRAM %u MB free before the map%s\n", kept, vf, wcache_dropped > d0 ? ": dropped (under 30 MB)" : ""); } }
#endif
		r = world_load(VDIR, MAPS[mi].stem); if (r < 0) { ldbg_wait(); return -1; }
		// b105 (user: "on the urban map the skybox seems to overlap over some of the more distant buildings when you look at it the
		// wrong way"): the view ended 120 m out on every map. Urban's skyline stands up to 360 m from the streets (88 of its
		// 376 meshes can be further than 120 m from where a player stands, 19 always are): the far plane cut those towers
		// off - flat across the view, so a tower straight ahead lost its top while the same tower at the edge of the screen
		// was whole - and the sky dome showed in the cut. The view now ends beyond the map's furthest corner. What that adds
		// to a frame (triangles past 120 m inside the view, before the occlusion test, 720 views per map): Urban 1,030 on
		// average (8,400 at most), Train Station 1,050, Ruined Tokyo 860, the others under 500. Not on Berutalube and
		// Underground: their own streets and halls run past 120 m (17-19 thousand more triangles a frame): as before.
		{ float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
		  for (int i = 0; i < world_nmesh; i++) for (int k = 0; k < 3; k++) { if (w_mesh[i].mn[k] < mn[k]) mn[k] = w_mesh[i].mn[k]; if (w_mesh[i].mx[k] > mx[k]) mx[k] = w_mesh[i].mx[k]; }
		  float dg = world_nmesh > 0 ? sqrtf((mx[0] - mn[0]) * (mx[0] - mn[0]) + (mx[1] - mn[1]) * (mx[1] - mn[1]) + (mx[2] - mn[2]) * (mx[2] - mn[2])) : 0;
		  // b106 (the Vita, b105-1 / b105-4): with the far plane at the map's size on every map, Train Station drew 169-187 meshes
		  // a frame instead of 83-97 and ran at 58.4-58.9 fps with dips to 48 (59.8 / 57.9 before) - the user's rule: never a
		  // frame rate drop. Only Urban, whose skyline was the reason (and which ran exactly as before: 59.6-59.9), keeps the long
		  // view; every other map has its 120 m back, as up to b104.
		  view_far = !strcmp(MAPS[mi].stem, "urban") ? fminf(90000, fmaxf(12000, dg + 500)) : 12000;
		  logp("view: far plane %.0f m (the map's box %.0f m corner to corner)\n", view_far / 100, dg / 100); }
		// perf (b45): each step a load phase (ldprof.h)
		sprintf(p, VDIR "%s.gmm", MAPS[mi].stem); LDP_PUSH(LDP_MINIMAP); r = minimap_load(p); LDP_POP(); logp("minimap: %s\n", r < 0 ? "none" : "ok");
		sprintf(p, VDIR "%s.glut", MAPS[mi].stem); LDP_PUSH(LDP_GRADE); r = grade_load(p); LDP_POP(); logp("colour grading: %s\n", r < 0 ? "none (neutral)" : "map LUT");
		{ LDP_PUSH(LDP_RANGE); int rr = range_load(VDIR, MAPS[mi].stem); LDP_POP(); if (rr) logp("range: %d practice targets (%s.grange)%s\n", rr, MAPS[mi].stem, rr < 0 ? " FAILED" : ""); }
		if (wo_load(VDIR, MAPS[mi].stem) > 0 && wo_trainview) WO.t = 2.6f;   // b87: animated objects (wobj.h); trainview: the train runs ~3 s in
		{ int ns = smf_load(VDIR "smf.txt", MAPS[mi].stem); if (ns) logp("special maneuvers: %d (smf.txt, b87: vaults, climbs, leaps as the PC's)\n", ns); }
		p_climb_max = !strcmp(MAPS[mi].stem, "shooting") ? 260.0f : 230.0f;   // b122: the range's 2.35-2.55 m blocks can be climbed (player.h)
		if (p_climb_max > 230.0f) logp("climbing: ledges up to %.0f cm above the feet on this map (b122: the shooting range's blocks)\n", p_climb_max);
	} else { wo_free(); smf_free(); }
	if (LDBG.th >= 0) loading_progress(0.75f, "LOADING COLLISION");
	LDP_PUSH(LDP_BGWAIT); ldbg_wait(); LDP_POP();
	{ char nb[120]; snprintf(nb, sizeof(nb), "loader thread: collision %.0f ms, navigation %.0f ms, light %.0f ms (%s)", lvbg.us[0] / 1e3, lvbg.us[1] / 1e3, lvbg.us[2] / 1e3,
		LDBG.threaded ? "beside the world" : "inline: no thread"); ldp_note(nb); }
	if (lvbg.rcol < 0) { logp("open fail %s\n", lvbg.col); return -2; }
	if (!lite) range_col_fix();   // b55: the Shooting Range's invisible boundary lets shots through (range.h)
	if (!strcmp(MAPS[mi].stem, "tutorial")) tut_cage_col_fix(); else tut_cage_ntri = tut_cage_nmesh = tut_cage_is_open = 0;   // b118: the Think Tank's glass cylinder (above)
	world_ground_patches(!strcmp(MAPS[mi].stem, "ghost_storage") ? w_floor_under : NULL);   // b117: which low blended meshes lie on a floor (world.h). Ghost Storage only: its broken concrete flickered on the Vita. The rule also finds 2 meshes on Urban, 1 in the E-Center, 1 Underground and 6 in the third training room - nobody saw those flicker and the emulator cannot show it, so they are drawn as before
	if (lvbg.rnav < 0) logp("no navigation mesh (%s): bots walk straight\n", lvbg.nav);
	logp("light samples: %d (map average %.2f %.2f %.2f)\n", lvbg.rlgt, lg_avg[0], lg_avg[1], lg_avg[2]);
	if (!lite) { LDP_PUSH(LDP_TEXCHECK); world_tex_check(0); LDP_POP(); }
	world_lite = lite;
	if (lite) logp("level: %s loaded without its world (start-up: collision, navigation, light; the world loads with the first map played)\n", MAPS[mi].stem);
#ifdef HG_HOST
	level_hash_log(MAPS[mi].stem);   // perf (b45): host test
#endif
	return 0;
}
static int load_level(int mi) { return load_level_ex(mi, 0); }
// databuilder (b47): the loaded map's fog colour and fogp (start cm, density 1/cm, max amount), from its .glgt (light.h: FOGP
// trailer, the map's World.ini values; they were MAPS[]' fog columns). Data older than b47 has none: fog off.
static void map_fog(float *fog, float *fogp) {
	if (lg_fog_ok) { memcpy(fog, lg_fog, 12); memcpy(fogp, lg_fog + 3, 12); logp("fog: %.3f %.3f %.3f, from %.0f cm, density %.6f, amount %.2f\n", fog[0], fog[1], fog[2], fogp[0], fogp[1], fogp[2]); return; }
	fog[0] = 0.55f; fog[1] = 0.6f; fog[2] = 0.66f; fogp[0] = 1000; fogp[1] = 0; fogp[2] = 0;
	logp("fog: none in the map's .glgt (data older than b47): fog off\n");
}
// perf (b45): the start-up's sound banks and voice pack index (~34 MB: card reads and parsing, no GL / log / rand) load on
// the loader thread while the main thread compiles shaders and loads the HUD, menus and skills; nothing reads the sound
// tables before the wait (ev_find is the static event table). After the wait the finished tables are put aside until the
// point where audio_load_all ran before (the loadout in between found no sounds then and must not start a voice now: a voice
// holds a pointer into the table, which add_tone reallocates; host ASan)
static int boot_audio_r; static uint64_t boot_audio_us;
static struct { Sound *snds; int nsnd; VEntry *vlines; int nvline, nvpack; } boot_audio_aside;
static void boot_audio_job(void *ctx) { (void)ctx; uint64_t t0 = sceKernelGetSystemTimeWide(); boot_audio_r = audio_load_all(VDIR); boot_audio_us = sceKernelGetSystemTimeWide() - t0; }

#ifdef GITSFA_PGO_GEN
// perf (b45): a profile run for a profile-guided build: CMake GITSFA_PGO="-fprofile-arcs -DGITSFA_PGO_GEN". VitaSDK's libgcov is
// built without a C library (no file output, no value profilers), so this is edge counters only, written here: __gcov_init keeps
// each unit's compiled-in gcov_info, and at exit libgcov's freestanding serializer (__gcov_info_to_gcda) writes them to
// ux0:data/gitsfa/pgo/<unit>.gcda. Copy those next to the build's objects (CMakeFiles/gitsfa.dir/src/) and build with
// GITSFA_PGO="-fprofile-use -fprofile-partial-training -Wno-missing-profile" (see the b45 report).
struct gcov_info;
extern void __gcov_info_to_gcda(const struct gcov_info *, void (*)(const char *, void *), void (*)(const void *, unsigned, void *), void *(*)(unsigned, void *), void *) __attribute__((weak));   // (weak: a profile-use build keeps these hooks, unused, without libgcov)
static const struct gcov_info *pgo_info[8]; static int pgo_n;
void __gcov_init(struct gcov_info *i) { if (pgo_n < 8) pgo_info[pgo_n++] = i; }
void __gcov_exit(void) { }
static FILE *pgo_f; static unsigned pgo_bytes;
static void pgo_name(const char *name, void *arg) { (void)arg; const char *b = strrchr(name, '/'); char p[200]; snprintf(p, sizeof(p), VDIR "pgo/%s", b ? b + 1 : name);
	if (pgo_f) fclose(pgo_f); pgo_f = fopen(p, "wb"); }
static void pgo_data(const void *d, unsigned n, void *arg) { (void)arg; if (pgo_f) { fwrite(d, 1, n, pgo_f); pgo_bytes += n; } }
static void *pgo_alloc(unsigned n, void *arg) { (void)arg; return malloc(n); }
static void pgo_write(void) {
	sceIoMkdir(VDIR "pgo", 0777); pgo_bytes = 0;
	for (int i = 0; i < pgo_n; i++) { __gcov_info_to_gcda(pgo_info[i], pgo_name, pgo_data, pgo_alloc, NULL); if (pgo_f) { fclose(pgo_f); pgo_f = NULL; } }
	logp("pgo: %d units, %u bytes of counters written to %spgo/\n", pgo_n, pgo_bytes, VDIR);
}
#endif
// --- math (column-major) ---
static void mul4(float *o, const float *a, const float *b) {
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
		float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static void persp(float *m, float fovy, float asp, float n, float f) {
	float t = 1.0f / tanf(fovy * 0.5f); memset(m, 0, 64);
	m[0] = t / asp; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
}
// b102 (task #160; the user's Cyber Ward video, 118-122 s): the picture in a Think Tank's panel is live on the PC - the tank
// itself, seen by a camera that rides before it (its front with the three eyes left of the middle, the body running off to
// the right, nothing of the world behind it, all in the team's colour). Each panel that the HUD drew last frame gets its
// tank drawn into its rectangle here, after the scene and before the HUD; the tank's skinning of this frame is reused, a tank
// that is out of the main picture is skinned here every other frame. (ttm.h's panel then lays the team's wash and its texts
// over it; without this - no panel last frame - it shows the still picture as before.)
static int ttm_test = -1;   // test switch ttmtest (the emulator's pictures): no scissor and no viewport change - the rectangle is folded into the
// projection and the tank may run over the panel's edge. vitaGL's scissor is a GXM mask update drawn as a rectangle; Vita3K
// paints those white (b102-13 .. 18: the whole frame from the scissor's switching off; the menus' white figure panels are the same)
static void ttm_portrait(int i, int x, int y) {
	Tank *k = &tk[i]; if (k->state == TK_OFF || !k->pose || !sp_ok || sp_n || !tk_white) return;
	float M[16], V[16], Pm[16], VP[16], MVP[16]; tk_world_mat(k, M);
	float cy = cosf(k->pl.yaw), sy = sinf(k->pl.yaw), O[3] = { M[12], M[13], M[14] };
	float eye[3] = { O[0] + cy * 430 - sy * 150, O[1] + sy * 430 + cy * 150, O[2] + 190 }, at[3] = { O[0] + cy * 60, O[1] + sy * 60, O[2] + 150 };
	float f[3] = { at[0] - eye[0], at[1] - eye[1], at[2] - eye[2] }, fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]); for (int r = 0; r < 3; r++) f[r] /= fl;
	float rr[3] = { f[1], -f[0], 0 }, rl = sqrtf(rr[0] * rr[0] + rr[1] * rr[1]); rr[0] /= rl; rr[1] /= rl;   // right = f x up(0,0,1)
	float u[3] = { rr[1] * f[2] - rr[2] * f[1], rr[2] * f[0] - rr[0] * f[2], rr[0] * f[1] - rr[1] * f[0] };
	float Vm[16] = { rr[0], u[0], -f[0], 0, rr[1], u[1], -f[1], 0, rr[2], u[2], -f[2], 0, -(rr[0] * eye[0] + rr[1] * eye[1] + rr[2] * eye[2]), -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]), f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2], 1 };
	memcpy(V, Vm, sizeof(V)); persp(Pm, 26 * 3.14159265f / 180, (float)TTM_PW / TTM_PH, 30, 3000); mul4(VP, Pm, V); mul4(MVP, VP, M);
	float th = k->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th), Lw[3] = { eye[0] - at[0] - sy * 200, eye[1] - at[1] + cy * 200, 330 }, ll = sqrtf(Lw[0] * Lw[0] + Lw[1] * Lw[1] + Lw[2] * Lw[2]);
	for (int r = 0; r < 3; r++) Lw[r] /= ll;   // the light: from the camera's side, above
	const float ld[4] = { c * Lw[0] + s * Lw[1], -s * Lw[0] + c * Lw[1], Lw[2], 0 }, nofog[4] = { 0, 0, 0, 0 }; float tc[4]; ttm_team_col(k->team, tc);
	const float tint[4] = { 0.55f + 0.5f * tc[0], 0.55f + 0.5f * tc[1], 0.55f + 0.5f * tc[2], 1 }, bg[4] = { tc[0] * 0.5f, tc[1] * 0.5f, tc[2] * 0.5f, 1 };
	static const float qa[3] = { -1, 1, 0.9999f }, qb[3] = { 1, 1, 0.9999f }, qc[3] = { 1, -1, 0.9999f }, qd[3] = { -1, -1, 0.9999f };
	const int emu = ttm_test > 0; float Sf[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	if (emu) { const float sx = TTM_PW / 960.0f, sy = TTM_PH / 544.0f, ox = (2 * x + TTM_PW) / 960.0f - 1, oy = 1 - (2 * y + TTM_PH) / 544.0f;   // the rectangle folded into the projection
		Sf[0] = sx; Sf[5] = sy; Sf[12] = ox; Sf[13] = oy;
		for (int c_ = 0; c_ < 4; c_++) { MVP[c_ * 4] = sx * MVP[c_ * 4] + ox * MVP[c_ * 4 + 3]; MVP[c_ * 4 + 1] = sy * MVP[c_ * 4 + 1] + oy * MVP[c_ * 4 + 3]; } }
	else { glViewport(x, 544 - y - TTM_PH, TTM_PW, TTM_PH); glEnable(GL_SCISSOR_TEST); glScissor(x, 544 - y - TTM_PH, TTM_PW, TTM_PH); }
	// the rectangle's backdrop, written at the far plane with the depth test passing always: the picture's own depth range
	// without a clear
	sprite_quad(qa, qb, qc, qd, 0, 0, 1, 1, bg);
	glUseProgram(sp_prog); for (int r = 0; r < 4; r++) { float row[4] = { Sf[r], Sf[4 + r], Sf[8 + r], Sf[12 + r] }; glUniform4fv(sp_u_m[r], 1, row); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tk_white);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, sp_v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, sp_v + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, sp_v + 5);
	glDrawArrays(GL_TRIANGLES, 0, sp_n); sp_n = 0; glDepthFunc(GL_LEQUAL);
	Model *ms[1 + TKP_NS]; CSkin *css[1 + TKP_NS]; int nm = tk_models(k, ms, css), reskin = k->seen_frame != bot_frame && ((bot_frame + i) & 1);
	for (int q = 0; q < nm; q++) chr_draw(ms[q], css[q], 0, MVP, k->pose, ld, tint, nofog, reskin);
	if (!emu) { glDisable(GL_SCISSOR_TEST); glViewport(0, 0, 960, 544); }
	glEnable(GL_CULL_FACE);
	ttm_live_frame[i] = bot_frame + 1;
}
static void ttm_portraits(void) {
	if (!ttm_on || tk_ok <= 0) return;
	if (ttm_test < 0) { FILE *tf = fopen(VDIR "ttmtest", "rb"); ttm_test = tf != NULL; if (tf) fclose(tf); }
	int me = tk_local_team, mine = -1, theirs = -1; for (int i = 0; i < TK_MAX; i++) if (tk_alive_i(i)) { if (tk[i].team == me) { if (mine < 0) mine = i; } else if (theirs < 0) theirs = i; }
	if (mine >= 0 && ttm_panel_frame[mine] == bot_frame) ttm_portrait(mine, 190, 2);   // (the panels' places: ttm.h ttm_hud)
	if (theirs >= 0 && ttm_panel_frame[theirs] == bot_frame) ttm_portrait(theirs, 770 - TTM_PW, 2);
}
// Z-up world. yaw around Z, pitch up/down.
static void view(float *m, const float *p, float yaw, float pitch, float *fwd) {
	float cp = cosf(pitch), sp = sinf(pitch), cy = cosf(yaw), sy = sinf(yaw);
	float F[3] = { cy * cp, sy * cp, sp }, R[3] = { sy, -cy, 0 }, U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
	memcpy(fwd, F, 12);
	float v[16] = { R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0,
		-(R[0] * p[0] + R[1] * p[1] + R[2] * p[2]), -(U[0] * p[0] + U[1] * p[1] + U[2] * p[2]), (F[0] * p[0] + F[1] * p[1] + F[2] * p[2]), 1 };
	memcpy(m, v, 64);
}
static __attribute__((unused)) int aabb_visible(const float *mvp, const float *mn, const float *mx) {
	// test against 6 planes extracted from mvp
	float pl[6][4];
	for (int i = 0; i < 4; i++) {
		float r3 = mvp[i * 4 + 3];
		pl[0][i] = r3 + mvp[i * 4 + 0]; pl[1][i] = r3 - mvp[i * 4 + 0];
		pl[2][i] = r3 + mvp[i * 4 + 1]; pl[3][i] = r3 - mvp[i * 4 + 1];
		pl[4][i] = r3 + mvp[i * 4 + 2]; pl[5][i] = r3 - mvp[i * 4 + 2];
	}
	for (int p = 0; p < 6; p++) {
		float x = pl[p][0] >= 0 ? mx[0] : mn[0], y = pl[p][1] >= 0 ? mx[1] : mn[1], z = pl[p][2] >= 0 ? mx[2] : mn[2];
		if (pl[p][0] * x + pl[p][1] * y + pl[p][2] * z + pl[p][3] < 0) return 0;
	}
	return 1;
}

// impact sparks / dust (player shots, bot near-misses, hits on bots: mat 9)
static void on_bot_impact(V3 p, V3 n, int mat) { fx_impact(mat, &p.x, &n.x, NULL); snd_impact("ar", mat, &p.x); }
// b59 (user: keep the blood): a hit on an operator - the blood spray, and a splat on the wall or floor behind (fx.gfx's
// blood_decal_a, which nothing placed before); splats at most every 0.12 s
static void blood_hit(V3 p, V3 d) {
	float l = sqrtf(vdot(d, d)); if (l < 1e-4f) return; d = vmul(d, 1 / l);
	V3 nn = vmul(d, -1); fx_impact(9, &p.x, &nn.x, &d.x);
	static SceUInt64 last; SceUInt64 now = sceKernelGetProcessTimeWide(); if (now - last < 120000) return; last = now;
	V3 o = vadd(p, vmul(d, 25)), n; int mat = 0; float h = raycast(o, d, 240, &mat, &n);
	if (h > 0) { V3 w = vadd(o, vmul(d, h)); fx_decal(9, &w.x, &n.x); }
	else if ((h = raycast(p, v3(0, 0, -1), 170, &mat, &n)) > 0) { V3 w = vadd(p, v3(0, 0, -h)); fx_decal(9, &w.x, &n.x); }
}
// kill feed entry (newest at the bottom, 5 rows); colours: the player yellow, enemies red
// colours by side: 1 = the player (yellow), 0 = ally (white), -1 = enemy (red)
static void (*net_feed_hook)(const char *killer, int kme, const char *victim, int vme);   // net (net_game.h): the host shows every kill to the room
static void (*net_kill_hook)(int killer_bot, int victim_bot, int head);                  // net: the actors of the next feed entry
static void feed_add(HudKill *f, int *n, const char *killer, int kme, const char *victim, int vme) {
	if (*n >= HUD_FEED) { memmove(f, f + 1, sizeof(HudKill) * (HUD_FEED - 1)); *n = HUD_FEED - 1; }
	HudKill *k = &f[(*n)++]; memset(k, 0, sizeof(*k)); snprintf(k->killer, 24, "%s", killer); snprintf(k->victim, 24, "%s", victim);
	static const float Y[4] = { 1, 0.86f, 0.25f, 1 }, R[4] = { 0.95f, 0.17f, 0.24f, 1 }, W[4] = { 0.92f, 0.95f, 1, 1 };
	memcpy(k->kc, kme > 0 ? Y : kme == 0 ? W : R, 16); memcpy(k->vc, vme > 0 ? Y : vme == 0 ? W : R, 16); k->weapon = feed_wicon; k->t = 0;
	if (net_feed_hook) net_feed_hook(killer, kme, victim, vme);
}
// match state (TDM): team scores, kill feed
static HudKill feed[HUD_FEED]; static int nfeed, score_l, score_r;
static const char *chr_display(const char *n);
static const char *bot_name(int i);   // net: a remote player's name, else the operator's
static int dem_on_flag(void);
static void dem_died(int id);
static int bot_wicon(int b);
static void on_bot_kill(int killer, int victim, int head) {   // bot-on-bot kills (the player's own kills are counted where the shot is fired)
	if (net_kill_hook) net_kill_hook(killer, victim, head);
	(void)head; if (dem_on_flag()) dem_died(1 + victim); if (killer < 0) return;
	if (bots[killer].team == 0) score_l += 100; else score_r += 100;
	int w0 = feed_wicon; feed_wicon = bot_kill_icon > 0 ? bot_kill_icon : bot_wicon(killer);   // b89: the killer's own gun (was always the M4A1's icon)
	feed_add(feed, &nfeed, bot_name(killer), bots[killer].team == 0 ? 0 : -1, bot_name(victim), bots[victim].team == 0 ? 0 : -1); feed_wicon = w0;
}
static float bot_falloff_of(const char *sh, float dist) { int d = wdef_find(sh); return d >= 0 ? wdef_falloff(&wdef[d], dist) : 1; }   // b90: bots.h bot_falloff_hook
static int bot_wicon(int b) {   // b89: the kill-feed icon (WpIconMC frame) of the gun bot b carries: weapons.txt's icon column
	if (b < 0 || b >= nbot || bots[b].wpn < 0 || bots[b].wpn >= NBOT_WPN) return 2;
	int d = wdef_find(BOT_WPN[bots[b].wpn].sh); return d >= 0 && wdef[d].icon > 0 ? wdef[d].icon : 2;
}
// team bases: the spawn groups split into the two ends of the map (2-means on the group centres)
static int team_of_group[64]; static int teams_ok;
static void team_bases(void) {
	float cx[64] = { 0 }, cy[64] = { 0 }; int cn[64] = { 0 }, ng = 0; teams_ok = 0;
	for (int i = 0; i < nspawn; i++) { int g = spawns[i].gi; if (g < 0 || g >= 64) continue; cx[g] += spawns[i].x; cy[g] += spawns[i].y; cn[g]++; if (g + 1 > ng) ng = g + 1; }
	int a = -1, b = -1; float bd = -1;
	for (int i = 0; i < ng; i++) if (cn[i]) { cx[i] /= cn[i]; cy[i] /= cn[i]; }
	for (int i = 0; i < ng; i++) for (int j = i + 1; j < ng; j++) if (cn[i] && cn[j]) { float d = (cx[i] - cx[j]) * (cx[i] - cx[j]) + (cy[i] - cy[j]) * (cy[i] - cy[j]); if (d > bd) { bd = d; a = i; b = j; } }
	if (a < 0 || bd < 2000.f * 2000.f) return;
	for (int i = 0; i < ng; i++) { float da = (cx[i] - cx[a]) * (cx[i] - cx[a]) + (cy[i] - cy[a]) * (cy[i] - cy[a]), db = (cx[i] - cx[b]) * (cx[i] - cx[b]) + (cy[i] - cy[b]) * (cy[i] - cy[b]); team_of_group[i] = da <= db ? 0 : 1; }
	teams_ok = 1;
}
static int team_spawn(int team) {
	if (!teams_ok || !nspawn) return -1;
	int c[256], n = 0; for (int i = 0; i < nspawn && n < 256; i++) { int g = spawns[i].gi; if (g >= 0 && g < 64 && team_of_group[g] == team) c[n++] = i; }
	return n ? c[rand() % n] : -1;
}
static const char *chr_display(const char *n) {
	static const char *a[][2] = { { "bato", "BATOU" }, { "motoko", "MOTOKO" }, { "saito", "SAITO" }, { "togusa", "TOGUSA" }, { "ishikawa", "ISHIKAWA" }, { "borma", "BORMA" },
		{ "paz", "PAZ" }, { "azuma", "AZUMA" }, { "kuro", "KURO" }   /* b85: v2.0 says KURO (EP25 DETAILS) */, { "maven", "MAVEN" }, { "reiko", "REIKO" }, { "sitara", "SITARA" }, { "cyborg", "CYBORG" } };
	for (unsigned i = 0; i < sizeof(a) / sizeof(a[0]); i++) if (!strcmp(a[i][0], n)) return a[i][1];
	return n;
}
static const char *bot_name(int i) { return i >= 0 && i < NBOT_MAX && bot_label[i][0] ? bot_label[i] : chr_display(chrt[bots[i].ct].name); }
// b70 (Ghost Assault): a cyborg bot executes the player (bots.h bot_exec_start): no control, the view held on the attacker
// until the stab kills (bots.h deals it)
static int pgrab_bot = -1; static float pgrab_t;
static void on_bot_exec_player(int i, int front) { pgrab_bot = i; pgrab_t = 0; logp("  executed by bot %d (%s)\n", i, front ? "front" : "from behind"); }
static void on_bot_shot(V3 from, V3 to) { V3 d = vsub(to, from); float l = sqrtf(vdot(d, d)); if (l < 1) return; d = vmul(d, 1 / l); fx_muzzle_3p(&from.x, &d.x); fx_tracer(&from.x, &to.x); }
// project a world point to the 960x544 screen; returns 0 when behind the camera
static int to_screen(const float *M, V3 p, float *sx, float *sy, float *w) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = M[r] * p.x + M[4 + r] * p.y + M[8 + r] * p.z + M[12 + r];
	if (q[3] < 5) return 0; *sx = (q[0] / q[3] + 1) * 480; *sy = (1 - q[1] / q[3]) * 272; *w = q[3]; return 1;
}

static void loading_screen(const char *name) {
	for (int k = 0; k < 2; k++) {
		glClearColor(0.004f, 0.03f, 0.055f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glUseProgram(prog_flat); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
		const float cy[4] = { 0.35f, 0.9f, 1, 1 }, wh[4] = { 1, 1, 1, 1 };
		if (name) { text(480 - text_w("LOADING", 3) / 2, 214, 3, "LOADING", cy); text(480 - text_w(name, 5) / 2, 258, 5, name, wh); }
		else {   // b87 (user: no "loading the last map" page when the game starts): black, the game's own small loading diamond
			     // in the middle (EP26 11:02-11:08) - the start-up map still loads underneath, for a quick first match
			glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
			const float dm[4] = { 0.85f, 0.9f, 0.92f, 0.9f };
			for (int r = -5; r <= 5; r++) { float w = 6 - (r < 0 ? -r : r); rect(480 - w, 272 + r, 480 + w, 273 + r, dm); } }
		glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
		vglSwapBuffers(GL_FALSE);
	}
}
// b105 (user: "at the start of the game there seems to be a long black screen with a small white diamond in the middle"): the
// start-up load (about 20 s on the Vita before the logos: 46 MB read, the sounds, the textures) showed b87's still diamond
// and nothing else. The diamond stays (the PC's own start-up mark) - with a ring of sixteen marks round it that light one
// after another as the load goes on, LOADING under it and a bar: drawn again at every step of the load (the world's own
// progress calls while the start-up map is read, then each loading phase and each operator), so the screen is seen to work.
// Not in the test tour (it shows its "LOADING <map>" page).
static int boot_scr_on; static float boot_scr_p, boot_scr_a = 0.02f, boot_scr_b = 0.36f;   // a..b: the span the world's 0..1 progress covers
static void boot_step(float p) {
	if (!boot_scr_on) return; if (p > 1) p = 1; if (p > boot_scr_p) boot_scr_p = p; p = boot_scr_p;
	glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glUseProgram(prog_flat); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
	const float dm[4] = { 0.85f, 0.9f, 0.92f, 1 }, cy[4] = { 0.35f, 0.9f, 1, 1 }, dk[4] = { 0.06f, 0.16f, 0.19f, 1 }, dim[4] = { 0.55f, 0.66f, 0.70f, 1 };
	const float cx = 480, cyy = 236;
	for (int r = -5; r <= 5; r++) { float w = 6 - (r < 0 ? -r : r); rect(cx - w, cyy + r, cx + w, cyy + 1 + r, dm); }   // the diamond
	for (int k = 0; k < 16; k++) { float a = k * 0.3926991f, x = cx + sinf(a) * 30, y = cyy - cosf(a) * 30; int on = k < (int)(p * 16 + 0.5f);   // the ring: clockwise from the top
		rect(x - (on ? 3 : 2), y - (on ? 3 : 2), x + (on ? 3 : 2), y + (on ? 3 : 2), on ? cy : dk); }
	text(480 - text_w("LOADING", 4) / 2, 300, 4, "LOADING", dim);   // (28 px letters)
	rect(300, 348, 660, 354, dk); rect(300, 348, 300 + 360 * p, 354, cy);
	glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	vglSwapBuffers(GL_FALSE);
}
static void log_mem(void) {
	struct mallinfo mi = mallinfo();
	int sys_main = -1;
#ifdef __vita__
	{ SceKernelFreeMemorySizeInfo fi; memset(&fi, 0, sizeof(fi)); fi.size = sizeof(fi); if (sceKernelGetFreeMemorySize(&fi) >= 0) sys_main = fi.size_user / 1024; }
#endif
	logp("memory: heap in use %d KB (arena %d KB of %d); graphics free: vram %d KB, ram %d KB; system main free %d KB\n", (int)(mi.uordblks / 1024), (int)(mi.arena / 1024),
		_newlib_heap_size_user / 1024, (int)(vglMemFree(VGL_MEM_VRAM) / 1024), (int)(vglMemFree(VGL_MEM_RAM) / 1024), sys_main);
}
// ---- weapons: loadout, switching
static void weapon_begin_switch(int to, float holster) {
	if (to == wcur || to < 0 || to >= WS_N || !wsl[to].ok) return;
	WSlot *w = WSC; if (holster > 0 && w->c[CL_HOLSTER] >= 0) { anim_play(&w->a, w->c[CL_HOLSTER], 0.05f, 0.05f, 0); holster = fminf(holster, w->len[CL_HOLSTER]); }
	WP.sw_to = to; WP.sw_t = holster; WP.rl_phase = 0; WP.bolt_pending = 0;
}
static void weapon_finish_switch(void) {
	if (WP.sw_to < 0) return;
	wcur = WP.sw_to; WP.sw_to = -1; WSlot *w = WSC;
	anim_stop(&w->a); w->a.base = -1; anim_set_base(&w->a, w->c[CL_IDLE], 0, 0);
	if (w->c[CL_DRAW] >= 0) { anim_play(&w->a, w->c[CL_DRAW], 0.01f, 0.15f, 0); WP.draw_t = fminf(0.45f, w->len[CL_DRAW] * 0.7f); } else WP.draw_t = 0.25f;
	snd_wev(WD->sh, "equip_on", 0);
}
// loads the four loadout viewmodels (primary / secondary by weapons.txt short name); keeps a slot when it already holds that weapon
// (b98: mel - the melee weapon, "knife" or "axe": the DOUBLE KATAR)
static void loadout_load(const char *prim, const char *sec, const char *mel) {
	const char *want[WS_N] = { prim, sec, mel && wdef_find(mel) >= 0 ? mel : "knife", "m67" };
	for (int s = 0; s < WS_N; s++) {
		int d = wdef_find(want[s]); if (d < 0 && s == WS_PRIMARY) d = wdef_find("m4a1"); if (d < 0 && s == WS_SECONDARY) d = wdef_find("seburom5");
		if (d < 0) { wslot_free(s); logp("loadout: no weapon '%s' in the table\n", want[s]); continue; }
		if (wsl[s].ok && wsl[s].def == d) { wslot_refill(s); continue; }
		SceUInt64 t0 = sceKernelGetProcessTimeWide(); LDP_PUSH(LDP_VM); int r = wslot_load(s, d, VDIR); LDP_POP();   // perf (b45): load phase
		if (r < 0) logp("loadout: vm_%s.gvm not loaded (%d)\n", wdef[d].sh, r);
		else { logp("loadout slot %d: %s (vm_%s.gvm, %d meshes, %d clips, %.0f ms%s; scope %s, hud1 %s)\n", s, wdef[d].name, wdef[d].sh, wsl[s].m.nmesh, wsl[s].m.nclip,
			(sceKernelGetProcessTimeWide() - t0) / 1000.f, wcache_hit ? ", kept from before (perf b46: viewmodel cache)" : "", wsl[s].scope ? "yes" : "no", wsl[s].hud1 ? "yes" : "no");
			{ const Model *Mv = &wsl[s].m; int full = 0, light = 0; for (int k = 0; k < Mv->nmesh; k++) { full += Mv->mesh[k].ni / 3; light += (Mv->fp && Mv->fp[k].ni ? Mv->fp[k].ni : Mv->mesh[k].ni) / 3; }   // b60
			  if (Mv->fp) logp("  first-person mesh: %d triangles, %d while only the everyday clips play (vm_%s.gfp)\n", full, light, wdef[d].sh);
			  else logp("  first-person mesh: %d triangles (no light mesh%s)\n", full, wvm_fp_off ? ": nofp switch" : ": vm_*.gfp not in the data"); } }
	}
	if (!wsl[WS_PRIMARY].ok && wsl[WS_SECONDARY].ok) wcur = WS_SECONDARY; else wcur = WS_PRIMARY;
	vm_ok = wsl[wcur].ok; WP.sw_to = -1; WP.quick_from = -1; WP.gren_state = 0; WP.rl_phase = 0; WP.bolt_pending = 0;
	if (vm_ok) { WP.sw_to = wcur; weapon_finish_switch(); }
}
// the armory cycle (test): the next gun of the table in the primary slot
static void weapon_cycle_primary(void) {
	int d = wsl[WS_PRIMARY].ok ? wsl[WS_PRIMARY].def : 0;
	for (int k = 1; k <= nwdef; k++) { int c = (d + k) % nwdef; if (wdef[c].kind != WK_KNIFE && wdef[c].kind != WK_GRENADE) { d = c; break; } }
	SceUInt64 t0 = sceKernelGetProcessTimeWide(); int r = wslot_load(WS_PRIMARY, d, VDIR);
	logp("armory: %s %s (%.0f ms)\n", wdef[d].name, r < 0 ? "FAILED" : "loaded", (sceKernelGetProcessTimeWide() - t0) / 1000.f);
	if (r >= 0) { if (wcur == WS_PRIMARY) { WP.sw_to = WS_PRIMARY; weapon_finish_switch(); } else weapon_begin_switch(WS_PRIMARY, 0.25f); }
	else if (wcur == WS_PRIMARY && wsl[WS_SECONDARY].ok) { WP.sw_to = WS_SECONDARY; weapon_finish_switch(); }
	vm_ok = wsl[wcur].ok;
}
static __attribute__((unused)) void mat4_from_bone(const MBone *b, float *m) {   // column-major 4x4 of a bone pose (rotation q, translation p)
	float x = b->q[0], y = b->q[1], z = b->q[2], w = b->q[3];
	m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y + z * w); m[2] = 2 * (x * z - y * w); m[3] = 0;
	m[4] = 2 * (x * y - z * w); m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z + x * w); m[7] = 0;
	m[8] = 2 * (x * z + y * w); m[9] = 2 * (y * z - x * w); m[10] = 1 - 2 * (x * x + y * y); m[11] = 0;
	m[12] = b->p[0]; m[13] = b->p[1]; m[14] = b->p[2]; m[15] = 1;
}
// sniper scope overlay: black outside the lens circle (screen height), the scope's ring and reticle textures from the viewmodel
static void scope_overlay(GLuint reticle, GLuint ring, float a) {
	if (HG.white < 0 || HG.screen < 0) return;
	const HImg *I = &himg[HG.white]; GLuint wt = htex[I->tex]; float wu = (I->x + I->w * 0.5f) / htw[I->tex], wv = (I->y + I->h * 0.5f) / hth[I->tex];
	const float blk[4] = { 0, 0, 0, a }, wm[4] = { 1, 1, 1, a };
	const float cx = 480, cy = 272, R = 272, x0 = cx - R, x1 = cx + R;
	float A[6] = { x0 + 1, 0, 0, 0, 544, 0 }; hud_img(HG.white, HG.screen, A, blk, HUD_Z4, -1, 1);
	float B[6] = { 960 - x1 + 1, 0, x1 - 1, 0, 544, 0 }; hud_img(HG.white, HG.screen, B, blk, HUD_Z4, -1, 1);
	static const float CX[4] = { 0, 1, 1, 0 }, CY[4] = { 0, 0, 1, 1 };
	for (int c = 0; c < 4; c++) {   // the four corners between the square and the circle, as fans from the corner
		float px = x0 + CX[c] * 2 * R, py = CY[c] * 544, a0 = 3.14159f * (1 + 0.5f * c);
		for (int k = 0; k < 8; k++) { float t0 = a0 + k * 3.14159f / 16, t1 = a0 + (k + 1) * 3.14159f / 16;
			float xy[3][2] = { { px, py }, { cx + R * 1.01f * cosf(t0), cy + R * 1.01f * sinf(t0) }, { cx + R * 1.01f * cosf(t1), cy + R * 1.01f * sinf(t1) } }, uv[3][2] = { { wu, wv }, { wu, wv }, { wu, wv } };
			hud_poly(wt, HG.screen, (const float (*)[2])xy, (const float (*)[2])uv, 3, blk); } }
	float q[4][2] = { { x0, 0 }, { x1, 0 }, { x1, 544 }, { x0, 544 } }, uq[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	if (ring) hud_poly(ring, HG.screen, (const float (*)[2])q, (const float (*)[2])uq, 4, wm);
	if (reticle) hud_poly(reticle, HG.screen, (const float (*)[2])q, (const float (*)[2])uq, 4, wm);
}
// ---- game flow: front-end menus -> match -> results (menu.h); settings kept in config.bin
enum { PH_FRONT, PH_MATCH, PH_RESULTS };
static int phase = PH_MATCH, n_allies = 2, score_limit, match_len = 600; static MenuConfig cfg; static float look_sens = 1; static int invert_y;
static int kills_on_me[NBOT_MAX], my_kills_on[NBOT_MAX], killer_bot = -1;
static void cfg_check(MenuConfig *c) {
	MenuConfig d; menu_config_default(&d);
	if (c->chr < 0 || c->chr >= 12) c->chr = d.chr; if (c->primary < 0 || c->primary >= MENU_NWPN || MENU_WPN[c->primary].slot != 0) c->primary = d.primary;
	if (!menu_wpn_class_ok(c->chr, c->primary)) c->primary = menu_class_default(c->chr);   // b88: v2.0 classes (an older config.bin)
	if (c->secondary < 0 || c->secondary >= MENU_NWPN || MENU_WPN[c->secondary].slot != 1) c->secondary = d.secondary;
	if (c->map < 0 || c->map >= MENU_NMAPS) c->map = d.map; if (c->team_size < 1 || c->team_size > 4) c->team_size = d.team_size; if (c->mode < 0 || c->mode >= MODE_COUNT) c->mode = 0;
	c->name[sizeof(c->name) - 1] = 0;
	// b76: the player's name in matches (kill feed, scoreboard, results, the respawn panel) - the default profile name
	// "OPERATOR" was shown as the player's; the Vita's user name instead (net_lobby.h vita_user_name), kept in config.bin
	if (!c->name[0] || !strcmp(c->name, "OPERATOR")) { if (vita_user_name(c->name, sizeof(c->name))) logp("profile name: %s (the Vita's user name, b76)\n", c->name); else snprintf(c->name, sizeof(c->name), "OPERATOR"); }
	if (c->level < 1) { c->level = d.level; c->exp = d.exp; c->exp_next = d.exp_next; }
	// b77 (emulator config.bin showed "EXP : 248 / 100"): the experience needed grows 250 a level from 1000 at level 5; a config
	// outside that is put back on it and its spare experience turned into levels
	if (c->exp_next < 1000 || c->exp_next > 1000000) c->exp_next = 1000 + 250 * (c->level > 5 ? c->level - 5 : 0);
	if (c->exp < 0) c->exp = 0; while (c->exp >= c->exp_next && c->level < 99) { c->exp -= c->exp_next; c->level++; c->exp_next += 250; }
	c->intro_off = c->intro_off != 0;   // b70
	c->mask_off = c->mask_off != 0;     // b71
	c->melee = c->melee != 0;           // b98
	c->bot_skin_std = c->bot_skin_std != 0;   // b71 (skin[]: taken modulo each operator's count where used)
	for (int k = 0; k < 6; k++) if (c->tk_part[k] > 3) c->tk_part[k] = 0;   // b74 (tk_parts.txt has 2 per slot; thinktank.h tkp_k falls back to 0)
}
// b60: written by the log's writer thread over the existing file (alog.h alog_file_put: this thread never waits for the card)
static void cfg_save(void) { static unsigned char b[4 + sizeof(cfg)]; unsigned v = 0x31464347; memcpy(b, &v, 4); memcpy(b + 4, &cfg, sizeof(cfg)); alog_file_put(VDIR "config.bin", b, sizeof(b)); }
static void cfg_load(void) {
	menu_config_default(&cfg); FILE *f = fopen(VDIR "config.bin", "rb");
	if (f) { unsigned v = 0; MenuConfig c = cfg;   // b58: a shorter file (before the career fields) loads too; what it lacks keeps the defaults
	  if (fread(&v, 4, 1, f) == 1 && v == 0x31464347 && fread(&c, 1, sizeof(c), f) >= offsetof(MenuConfig, ach)) cfg = c; fclose(f); }
	cfg_check(&cfg); cfg.tut = 0;   // a tutorial program is picked in the menu each time, never restored
}
static void cfg_apply(void) {
	look_sens = 0.4f + 1.2f * cfg.look_sens; invert_y = cfg.invert_y;
	// b59: the sliders scale their category around the design mix (the music slider's default 0.7 = the design level);
	// AMBIENT / EFFECTS covers the ambience and every effect (RAM sounds, interface sounds)
	master_gain = 0.55f * cfg.vol_master; music_gain = cfg.vol_music / 0.7f; voice_gain = cfg.vol_voice; amb_gain = sfx_gain = cfg.vol_sfx;
}
static const char *op_voice(int op) {   // the voice set of an operator (Kurutsu's is filed under the codename "emp")
	if (op < 0 || op >= 12) return "motoko"; return MENU_CHR[op].stem;   // audio.h maps each operator (Kurutsu = "kuro") to its own voice set
}
static int op_of_chrt(int ct) { for (int i = 0; i < 12; i++) if (!strcmp(MENU_CHR[i].stem, chrt[ct].name)) return i; return -1; }
static unsigned menu_buttons(unsigned b) {   // Vita buttons -> MB_* masks
	unsigned m = 0;
	if (b & SCE_CTRL_UP) m |= MB_UP; if (b & SCE_CTRL_DOWN) m |= MB_DOWN; if (b & SCE_CTRL_LEFT) m |= MB_LEFT; if (b & SCE_CTRL_RIGHT) m |= MB_RIGHT;
	if (b & SCE_CTRL_CROSS) m |= MB_OK; if (b & SCE_CTRL_CIRCLE) m |= MB_BACK; if (b & SCE_CTRL_START) m |= MB_START;
	if (b & SCE_CTRL_LTRIGGER) m |= MB_L; if (b & SCE_CTRL_RTRIGGER) m |= MB_R; if (b & SCE_CTRL_TRIANGLE) m |= MB_TRI; if (b & SCE_CTRL_SQUARE) m |= MB_SQR;
	return m;
}
// the bot count of a match (allies + enemies); bots past the first allocation get their character on first use
static int nbot_alloc;
static void bots_set_count(int n) {
	if (n > NBOT_MAX) n = NBOT_MAX; if (n < 0) n = 0; if (!nchrt) { nbot = 0; return; }
	for (int i = nbot_alloc; i < n; i++) { memset(&bots[i], 0, sizeof(Bot)); bot_set_char(&bots[i], i % nchrt); }
	if (n > nbot_alloc) nbot_alloc = n; nbot = n; nbot_full = n;
}
// a 3D operator in a screen rectangle (front-end and results), holding a gun. ui b50 (user report b49: TOP OPERATIVE's figure held
// nothing in a gun pose): the pose is the in-game animation bank's idle of the weapon's own animation set (CAnim, as the bots: a
// sniper / machine gun / shotgun hold of its own), and the third-person gun the bots carry (w3_<wpn>.gvm, bots.h: loaded with
// the characters, so the menus load nothing more) is drawn on the bank's Grip bone, as bots.h draws it. wpn: weapon.h short name
// (MENU_WPN[].sh / bot_weapon), NULL or unknown = the M4A1 set without a gun. Own skinning buffers.
static SkinCache prev_sc; static CAnim prev_ca; static int prev_ct = -1, prev_w = -2;
// b63 (user: the operator's animation when picked, footage "EP. 24 Azuma" 20-23 s): on the operative screen the figure plays
// its out-of-game "choice" pose, then that pose's idle (chr_og_m / chr_og_f.gab, the original's GIS_*_3rd_OutGame networks),
// with the operator's own select line (v_character_select_<op>); Azuma's shield forms round him as he touches his wrist
// (skills.h sk_draw_menu_shield). chr_prev_choice: set by the caller for that screen; og_op -1 = start again.
static float og_slow = 1;   // b105 test switch ogslow: the operative page's choice pose at a tenth of its speed (the emulator's pictures, one a second)
static int chr_prev_choice, og_op = -1; static float og_t, og_sh_dist;   // (og_sh_dist, b103: Azuma's panel stands this far in front of him - past his arm, below)
// b77 (user: the result tabs' operator "and its animations" as footage #49): chr_prev_result 1 victory / 2 defeat / 3 draw = the
// out-of-game bank's result set - the intro (res_win / res_lose; a draw starts in res_idle), its loop, and once the exit countdown
// runs (chr_prev_ready) the ready clip into res_rdy_idle (footage 0:14-0:16: the hand comes off the hip as "EXITING TO LOBBY"
// appears), crossfaded 0.35 s from the pose shown
static int chr_prev_result, chr_prev_ready, res_key = -1, res_rd; static float res_t, res_xf; static CLoc res_from[CB_MAXB];
// b71: operator skins (bots.h chrt_reskin; chr_skins.txt): the character type of menu operator op; the menus' hooks; each
// match's skins - the player's operator in the player's choice, the others random (OPTIONS > GAME > BOT SKINS: standard)
static int ct_of_op(int op) { if (op < 0 || op >= 12) return -1; for (int i = 0; i < nchrt; i++) if (!strcmp(chrt[i].name, MENU_CHR[op].stem)) return i; return -1; }
static int mfe_skin_n_(int op) { int ct = ct_of_op(op); return ct < 0 ? 1 : chr_skin_n(chrt[ct].name); }
static const char *mfe_skin_name_(int op, int k) { int ct = ct_of_op(op); return ct < 0 ? "STANDARD" : chr_skin_name(chrt[ct].name, k); }
static int mfe_skin_gog_(int op, int k) { int ct = ct_of_op(op); return ct >= 0 && chr_skin_has_gog(chrt[ct].name, k); }   // b103
static int net_client_on(void); static int net_host_on(void);
static void match_pick_skins(void) {
	int nl = 0; char line[256]; line[0] = 0;
	for (int op = 0; op < 12; op++) { int ct = ct_of_op(op); if (ct < 0) continue;
		int n = chr_skin_n(chrt[ct].name), k = op == cfg.chr ? cfg.skin[op] % n : (cfg.bot_skin_std || net_host_on() || net_client_on()) ? 0 : rand() % n;   // (net: no skins are sent - the standard ones everywhere)
		if (chrt_reskin(ct, k) > 0) nl++;
		if (chrt[ct].skin) { int l = (int)strlen(line); snprintf(line + l, sizeof(line) - l, " %s=%s", chrt[ct].name, chr_skin_name(chrt[ct].name, chrt[ct].skin)); } }
	logp("match skins:%s (%d reloaded; the rest standard) (b71)\n", line[0] ? line : " all standard", nl);
}
static int bot_wpn_index(const char *sh) { if (sh) for (int w = 0; w < NBOT_WPN; w++) if (!strcmp(BOT_WPN[w].sh, sh)) return w; return -1; }
// b74: the MY THINK TANK screen's tank (menu_tank.h; menu_fe_preview -> -2): the core and the parts being tried (thinktank.h
// tkp_load: only a changed part loads), the out-game idle clip (OutGame_Tachikoma_IdleA, as the PC's menu tank), the
// operators' camera conventions (cam[0] eye height, [1] distance, [2] look-at height, [3] fov, [4] yaw). The models are
// freed when the screen is left (tk_menu_free) and loaded again by Terminal Conquest's map load.
static Anim tkm_an; static int tkm_on, tkm_used, tkm_bad, tkm_wait;
static void tk_menu_free(void) { tkm_bad = tkm_wait = 0; mtk_loading = 0; if (tkm_used) { if (tkm_on) anim_free(&tkm_an); tkm_on = tkm_used = 0; tk_unload(); logp("menu think tank: freed (b74)\n"); } }
// the screen's part list (menu_tank.h mtk) from tk_parts.txt, once at start-up
static void mtk_fill(void) {
	if (tkp_info_load(VDIR) < 0) return;
	for (int s_ = 0; s_ < TKP_NS; s_++) { mtk_n[s_] = tkp_n[s_]; for (int k = 0; k < tkp_n[s_] && k < 4; k++) { const TkPart *P = &tkp_info[s_][k]; MenuTkPart *M = &mtk[s_][k];
		snprintf(M->code, sizeof(M->code), "%s", P->code); M->weight = P->weight; M->hp = P->hp; M->loadage = P->loadage; M->speed = P->speed; M->dps = P->dps; M->rdmg = P->rdmg; M->accel = P->accel; M->rspread = P->rspread; } }
}
static void tk_menu_preview(const float *rect, const float *cam, float dt) {
	if (tkm_bad) return;   // a part file failed: not again every frame (until the screen is left)
	// the first load (the stock model's 1024 textures + the parts: ~0.3-0.4 s on the device) waits for the screen's slide-in and
	// one frame of LOADING, so the stall falls on a still screen that says so
	if (!tkp_ready() && (MFE.st < 0.32f || tkm_wait++ < 1)) { mtk_loading = 1; return; }
	mtk_loading = 0; tkm_used = 1; if (tkp_load(VDIR, mtk_work) < 0) { tkm_bad = 1; logp("menu think tank: parts missing: no preview (b74)\n"); return; }
	if (!tkm_on) { anim_init(&tkm_an, &tk_model[0]); int c = model_find_clip(&tk_model[0], "og_idle_a"); anim_set_base(&tkm_an, c >= 0 ? c : tk_cl[TC_IDLE], 0, 0); tkm_on = 1;
		logp("menu think tank: %s, parts %d%d%d%d%d%d (b74)\n", c >= 0 ? "og_idle_a" : "idle", mtk_work[0], mtk_work[1], mtk_work[2], mtk_work[3], mtk_work[4], mtk_work[5]); }
	anim_update(&tkm_an, dt); const MBone *pose = anim_pose(&tkm_an, -1);
	int x = (int)rect[0], y = (int)rect[1], w_ = (int)rect[2], h = (int)rect[3]; if (w_ < 8 || h < 8) return;
	int e = 544 - y - h; if (e < 0) e = 0; int he = h + e;   // b78: the view runs on to the screen's bottom (room for the reflection)
	glViewport(x, 0, w_, he); glEnable(GL_SCISSOR_TEST); glScissor(x, 0, w_, he); glClear(GL_DEPTH_BUFFER_BIT);
	float Pm[16], V[16], VP[16], fwd[3], yaw = cam[4] * 3.14159f / 180, d = cam[1], eye[3] = { sinf(yaw) * d, -cosf(yaw) * d, cam[0] };
	persp(Pm, cam[3] * 3.14159f / 180, (float)w_ / h, 10, 4000); Pm[5] *= (float)h / he; Pm[9] = -(float)e / he;   // (the same lens, its frustum stretched downward: the tank stays put)
	view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(cam[2] - cam[0], d), fwd); mul4(VP, Pm, V);
	glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
	const float cy_ = cosf(yaw), sy_ = sinf(yaw), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 }, tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	md_rim = BOT_RIM; md_emis_k = 1;
	// b78 (user: "make the floor reflective for the tachikoma like in the screenshot sent" - the PC's MyThinkTank): the tank
	// stands on a glossy floor: its mirror image in the floor plane (z = 0, the feet) drawn first - alpha 0.3, fogged with
	// depth toward the backdrop's floor teal so it fades the deeper it goes - then the depth cleared and the tank drawn over
	// it. The view reaches down to the screen's bottom edge (the same lens, its frustum stretched downward: the tank stays
	// where it was, its reflection has room under it).
	{ float Mr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 4.6f, 1 }, RVP[16]; mul4(RVP, VP, Mr);   // (mirrored in z = 2.3: the wheels' bottom in og_idle_a)
	  const float rt[4] = { 0.9f, 0.95f, 1.0f, 0.30f }, fogc[4] = { 0.07f, 0.19f, 0.22f, 1 }; float fp[4]; memcpy(fp, md_fogp, sizeof(fp));
	  float de = sqrtf(d * d + cam[0] * cam[0]); md_fogp[0] = de - 30; md_fogp[1] = 1.0f / 70; md_fogp[2] = 1;
	  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	  glEnable(GL_BLEND); chr_draw(&tkp_core, &tkp_cs[TKP_NS], 0, RVP, pose, ld, rt, fogc, 1);
	  for (int s_ = 0; s_ < TKP_NS; s_++) if (tkp_have[s_] >= 0) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); chr_draw(&tkp_m[s_], &tkp_cs[s_], 0, RVP, pose, ld, rt, fogc, 1); }
	  glDisable(GL_BLEND); memcpy(md_fogp, fp, sizeof(fp)); glClear(GL_DEPTH_BUFFER_BIT); }
	chr_draw(&tkp_core, &tkp_cs[TKP_NS], 0, VP, pose, ld, tint, nofog, 0);   // (b78: skinned for the reflection this frame)
	for (int s_ = 0; s_ < TKP_NS; s_++) if (tkp_have[s_] >= 0) chr_draw(&tkp_m[s_], &tkp_cs[s_], 0, VP, pose, ld, tint, nofog, 0);
	md_rim = 0;
	glDisable(GL_SCISSOR_TEST); glViewport(0, 0, 960, 544); glEnable(GL_CULL_FACE);
}
// b76 (user: the operatives' fidelity / polygon count when they are turned or looked at in the menus): the figure is the
// high-detail model of its type's skin (bots.h chr_hi_stem: chr_<..>_hi.gvm, the TPS body's top LOD, 12-15 k vertices, mesh
// only) with the in-game model's textures lent to it; loaded 0.2 s after the figure settles (no loads while scrolling the
// roster), freed when a match starts (menu_fe_unload's places); the in-game model is drawn until then, or if it is missing
static Model hi_m; static CBind hi_bind; static SkinCache hi_sc; static char hi_cur[40], hi_want[40]; static float hi_wait; static int hi_ok;
static void chr_hi_free(void) { if (hi_ok) { hi_m.tex = NULL; hi_m.ntex = 0; model_free(&hi_m); hi_ok = 0; } hi_cur[0] = hi_want[0] = 0; }
static Model *chr_hi_for(ChrType *C, const CBank *B, float dt, CBind **bind, SkinCache **sc) {
	const char *st = chr_hi_stem(C->name, C->skin); if (!st) return NULL;
	if (strcmp(st, hi_want)) { snprintf(hi_want, sizeof(hi_want), "%s", st); hi_wait = 0.2f; }
	if (strcmp(hi_cur, hi_want)) {
		if ((hi_wait -= dt) > 0) return NULL;
		chr_hi_free(); snprintf(hi_cur, sizeof(hi_cur), "%s", st); snprintf(hi_want, sizeof(hi_want), "%s", st);
		char p[192]; snprintf(p, sizeof(p), "%s%s.gvm", chr_dir, st); SceUInt64 t0 = sceKernelGetProcessTimeWide();
		int r = model_load(&hi_m, p), bad = r < 0 || hi_m.ntex != 0 || hi_m.nb > 64, nv = 0;
		if (r >= 0) { if (hi_m.tex) free(hi_m.tex); hi_m.tex = NULL; hi_m.ntex = 0; for (int i = 0; i < hi_m.nmesh; i++) { nv += hi_m.mesh[i].nv; if (hi_m.mesh[i].tex < 0 || hi_m.mesh[i].tex >= C->m.ntex) bad = 1; } }
		if (bad) { if (r >= 0) model_free(&hi_m); logp("menu figure: %s not used (%d) - the in-game model (b76)\n", p, r); return NULL; }   // (hi_cur keeps the name: no retry)
		cb_bind(&hi_bind, B, &hi_m); skincache_init(&hi_sc, &hi_m); hi_ok = 1; chr_gog_apply(&hi_m, st);   // (b103: its mask / goggle meshes)
		logp("menu figure: %s, %d meshes, %d vertices, %.0f ms (b76)\n", st, hi_m.nmesh, nv, (sceKernelGetProcessTimeWide() - t0) / 1000.0);
	}
	if (!hi_ok) return NULL;
	for (int i = 0; i < hi_m.nmesh; i++) if (hi_m.mesh[i].tex >= C->m.ntex) return NULL;
	for (int i = 0; i < hi_m.nmesh; i++) { hi_m.mesh[i].rim = hi_m.mesh[i].fres = 0;   // (b115: the textures' material light comes with them, model.h md_mesh_sheen)
		for (int k = 0; k < C->m.nmesh; k++) if (C->m.mesh[k].tex == hi_m.mesh[i].tex && (C->m.mesh[k].rim > 0 || C->m.mesh[k].fres > 0)) { hi_m.mesh[i].rim = C->m.mesh[k].rim; hi_m.mesh[i].fres = C->m.mesh[k].fres; break; } }
	hi_m.tex = C->m.tex; *bind = &hi_bind; *sc = &hi_sc; return &hi_m;
}
// b99: the stance an operative stands in on the lobby and in the results line-up - one per weapon family (the out-of-game
// bank's sets "lb.<the in-game bank's set>", slot lobby: tools/anim/make_og_bank.py); -1: the bank has none
// hero: the top operative's - the gun hanging from the right hand, the left hand on the hip: the results set's res_lose_idle, the
// pose the win animation of the results tabs starts from (the footage and the user's two PC screenshots show it; the lobby
// stance of an SMG or a rifle holds the gun across the body)
static int og_lobby_clip(const CBank *OG, const CBank *B, int set, int hero) {
	if (!OG->ok || !B->ok) return -1;
	if (hero) { int c = cb_clip(OG, cb_find_set(OG, "result"), cb_find_slot(OG, "res_lose_idle")); if (c >= 0 && cb_find_set(OG, "result") >= 0) return c; }
	int sl = cb_find_slot(OG, "lobby"); if (sl < 0) return -1;
	char sn[24]; snprintf(sn, sizeof(sn), "lb.%.12s", B->setname[set < 0 || set >= B->nset ? 0 : set]);
	int st = cb_find_set(OG, sn); if (st < 0 || OG->set[st * OG->nslot + sl] < 0) st = cb_find_set(OG, "lb.m4a1");
	return st < 0 ? -1 : OG->set[st * OG->nslot + sl];
}
// b103 (user: "at the end of a match on the top operative screen i was playing as motoko and her head kept looking down"): the
// top operative's stance is the results set's losing idle (above: its body is the PC screen's pose), whose head hangs; the head
// is held as in the same set's neutral idle (res_idle, the same loop length: level, toward the camera, its small motion kept)
static void lu_head_level(const CBank *OG, const CBank *B, float t, MBone *P) {
	static CLoc L[CB_MAXB], T[CB_MAXB]; static MBone Q[CB_MAXB];
	int st = cb_find_set(OG, "result"); if (st < 0 || og_lobby_clip(OG, B, 0, 1) != cb_clip(OG, st, cb_find_slot(OG, "res_lose_idle"))) return;
	int c = cb_clip(OG, st, cb_find_slot(OG, "res_idle")), hd = cb_find_bone(OG, "Bip01_Head");
	if (c < 0 || hd < 0 || !cb_pose_seq(OG, -1, c, t, 0.3f, L, T, Q)) return;
	memcpy(P[hd].q, Q[hd].q, sizeof(P[hd].q));
}
// b103: the player took the mask / goggles of the skin that character type wears off (the type is the player's operator)
// b109 (user: "make sure that if you toggled the mask off for an operative or their skin it stays off in the top operative screen"): any
// operator's type, not only the one the player plays - a team mate (a bot) who is the top operative wears the skin as the player set it
static void opmask_apply(void) { FILE *zf = fopen(VDIR "opmask", "rb"); if (zf) { fclose(zf); memset(cfg.gog_off, 0xff, sizeof(cfg.gog_off)); logp("opmask: every skin's mask off (test switch; b109: from the start, without the menus too)\n"); }
	zf = fopen(VDIR "opstart.txt", "rb"); if (zf) { char nm[16] = ""; if (fscanf(zf, "%15s", nm) == 1) for (int q = 0; q < 12; q++) if (!strcmp(MENU_CHR[q].stem, nm)) { cfg.chr = q; logp("opstart: playing %s (test switch; b109: without the menus too)\n", nm); } fclose(zf); } }
static int gog_off_ct(int ct) { if (ct < 0 || ct >= nchrt) return 0; for (int op = 0; op < 12; op++) if (!strcmp(chrt[ct].name, MENU_CHR[op].stem)) return (cfg.gog_off[op] >> (chrt[ct].skin & 15)) & 1; return 0; }
// b103 test switch menufold (the emulator's pictures of the menus' figures and of INSPECT, as ttmtest for the tank portrait):
// no scissor and no viewport change - the rectangle (GL: x, bottom, w, h) is folded into the projection
static int menu_fold = -1;
static int menu_fold_on(void) { if (menu_fold < 0) { FILE *f = fopen(VDIR "menufold", "rb"); menu_fold = f != NULL; if (f) fclose(f); if (menu_fold) logp("menufold: menu figures without scissor / viewport (test switch, b103)\n"); } return menu_fold; }
static void menu_fold_vp(float *VP, int x, int yb, int w, int h) {
	const float sx = w / 960.0f, sy = h / 544.0f, ox = (2 * x + w) / 960.0f - 1, oy = (2 * yb + h) / 544.0f - 1;
	for (int c = 0; c < 4; c++) { VP[c * 4] = sx * VP[c * 4] + ox * VP[c * 4 + 3]; VP[c * 4 + 1] = sy * VP[c * 4 + 1] + oy * VP[c * 4 + 3]; }
}
// the depth of the whole screen reset without glClear (Vita3K paints a mid-frame clear white, as the scissor's rectangles): a
// quad at the far plane with alpha 0, the depth test passing always. menufold only - the Vita's path keeps its clear
static void menu_fold_depth(void) {
	if (!sp_ok || sp_n) { glClear(GL_DEPTH_BUFFER_BIT); return; }
	static const float qa[3] = { -1, 1, 0.9999f }, qb[3] = { 1, 1, 0.9999f }, qc[3] = { 1, -1, 0.9999f }, qd[3] = { -1, -1, 0.9999f }, c0[4] = { 0, 0, 0, 0 };
	sprite_quad(qa, qb, qc, qd, 0, 0, 1, 1, c0);
	glUseProgram(sp_prog); for (int r = 0; r < 4; r++) { float row[4] = { r == 0, r == 1, r == 2, r == 3 }; glUniform4fv(sp_u_m[r], 1, row); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, sp_blob);
	glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, sp_v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, sp_v + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, sp_v + 5);
	glDrawArrays(GL_TRIANGLES, 0, sp_n); sp_n = 0; glDepthFunc(GL_LEQUAL); glDisable(GL_BLEND);
}
// b118 (user: with the PP-2200 equipped Togusa, Paz, Ishikawa, Saito and Kuro "look like they're pointing at their hand with it"
// on the operative screen): the menu poses hold a rifle - the left hand under a handguard 25-35 cm ahead of the grip. The
// PP-2200 ends 22 cm from its grip: the hand hung in the air in front of the muzzle. The game has the answer in its files:
// the eight guns that borrow another family's animations (PP-2200, MP5K, UMP-45, F2000, X95R, L86, SEBURO MG, SR-V97) carry a
// node LHand_IK in their third-person body - the place of the left wrist on THAT gun (PP-2200: 5 cm ahead of the grip, at the
// trigger guard) - and the PC bends the arm to it. vm/make_w3.py writes it as the model's attachment 'lhand'; here, where a
// pose has the left wrist on the gun (ahead of the grip, beside / under its line), the arm is bent to that place (two-bone
// IK: the shoulder stays, the elbow keeps its side; the hand keeps its turn, the fingers come with it). A gun without the
// node whose muzzle ends behind the pose's hand gets the hand under its front end the same way.
// P: the pose in the bank's bones (model space); out: the changed copy.
static void og_q_arc(const float *a, const float *b, float *q) {   // the shortest turn taking unit a to unit b
	float c[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }, d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	if (d < -0.9999f) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
	float sq = sqrtf((1 + d) * 2); q[0] = c[0] / sq; q[1] = c[1] / sq; q[2] = c[2] / sq; q[3] = sq / 2;
}
static int og_hand_dbg = -1, og_hand_noturn;   // (test switch file oghandpos: the hand moved only, its turn kept)
static const MBone *og_short_gun(const CBank *B, const MBone *P, const Model *G, MBone *out, const char *who, const char *gun) {
	const float *mz = G ? model_attach(G, "muzzle") : NULL; if (!mz || B->grip < 0) return P;
	int ua = -1, fa = -1, hd = -1;
	for (int b = 0; b < B->nb; b++) { if (!strcmp(B->bone[b], "Bip01_L_UpperArm")) ua = b; else if (!strcmp(B->bone[b], "Bip01_L_Forearm")) fa = b; else if (!strcmp(B->bone[b], "Bip01_L_Hand")) hd = b; }
	if (ua < 0 || fa < 0 || hd < 0) return P;
	const MBone *g = &P[B->grip]; float gi[4] = { -g->q[0], -g->q[1], -g->q[2], g->q[3] }, dv[3] = { P[hd].p[0] - g->p[0], P[hd].p[1] - g->p[1], P[hd].p[2] - g->p[2] }, h[3]; cq_rot(gi, dv, h);
	if (og_hand_dbg < 0) { FILE *f = fopen(VDIR "verbose", "rb"); og_hand_dbg = f != NULL; if (f) fclose(f);  f = fopen(VDIR "oghandpos", "rb"); og_hand_noturn = f != NULL; if (f) fclose(f); }
	static char said[40]; char key[40]; snprintf(key, sizeof(key), "%s/%s", who, gun);
	int say = og_hand_dbg && strcmp(said, key); if (say) snprintf(said, sizeof(said), "%s", key);
	const float *lh = model_attach(G, "lhand");   // the gun's own place for the left wrist
	float front = mz[0] - 9; if (front < 8) front = 8;   // (no such place: the wrist at most this far ahead of the grip - the palm ends under the muzzle end)
	int two = h[0] > 4 && fabsf(h[1]) < 20 && h[2] > -20 && h[2] < 16;   // the hand is on the gun: ahead of the grip, beside / under its line
	int move = two && (lh ? (h[0] - lh[0]) * (h[0] - lh[0]) + (h[1] - lh[1]) * (h[1] - lh[1]) + (h[2] - lh[2]) * (h[2] - lh[2]) > 4 : h[0] > front);
	if (say) logp("menu figure: %s with %s: left wrist %.1f / %.1f / %.1f cm in the gun's space, muzzle at %.1f: %s\n", who, gun, h[0], h[1], h[2], mz[0], !two ? "not on the gun" : !move ? "on the gun" : lh ? "moved to the gun's own left-hand place" : "ahead of the gun's front: brought back");
	if (say) { float qr[4], ax[3], az[3]; static const float X[3] = { 1, 0, 0 }, Z[3] = { 0, 0, 1 }; cq_mul(gi, P[hd].q, qr); cq_rot(qr, X, ax); cq_rot(qr, Z, az); logp("menu figure:   the hand's x axis %.2f %.2f %.2f, z axis %.2f %.2f %.2f in the gun's space\n", ax[0], ax[1], ax[2], az[0], az[1], az[2]); }
	if (say) { int rh = -1; for (int b = 0; b < B->nb; b++) if (!strcmp(B->bone[b], "Bip01_R_Hand")) rh = b;   // (the right hand's hold, for the left hand's mirror of it: Paz)
		if (rh >= 0) { float ri[4] = { -P[rh].q[0], -P[rh].q[1], -P[rh].q[2], P[rh].q[3] }, d_[3] = { g->p[0] - P[rh].p[0], g->p[1] - P[rh].p[1], g->p[2] - P[rh].p[2] }, t_[3], qr[4], ax[3], ay[3], az[3]; static const float X[3] = { 1, 0, 0 }, Y[3] = { 0, 1, 0 }, Z[3] = { 0, 0, 1 };
			cq_rot(ri, d_, t_); cq_mul(ri, g->q, qr); cq_rot(qr, X, ax); cq_rot(qr, Y, ay); cq_rot(qr, Z, az);
			logp("menu figure:   the gun in the right hand's space: at %.2f %.2f %.2f, forward %.2f %.2f %.2f, left %.2f %.2f %.2f, up %.2f %.2f %.2f\n", t_[0], t_[1], t_[2], ax[0], ax[1], ax[2], ay[0], ay[1], ay[2], az[0], az[1], az[2]); } }
	if (!move) return P;
	memcpy(out, P, sizeof(MBone) * B->nb);
	float h2[3] = { lh ? lh[0] : front, lh ? lh[1] : h[1], lh ? lh[2] : h[2] }, T[3]; cq_rot(g->q, h2, T); for (int k = 0; k < 3; k++) T[k] += g->p[k];
	const float *S = P[ua].p, *E = P[fa].p, *W = P[hd].p; float se[3], ew[3], st[3], l1 = 0, l2 = 0, d = 0;
	for (int k = 0; k < 3; k++) { se[k] = E[k] - S[k]; ew[k] = W[k] - E[k]; st[k] = T[k] - S[k]; l1 += se[k] * se[k]; l2 += ew[k] * ew[k]; d += st[k] * st[k]; }
	l1 = sqrtf(l1); l2 = sqrtf(l2); d = sqrtf(d); if (l1 < 1 || l2 < 1 || d < 1) return P;
	float dc = d; if (dc > l1 + l2 - 0.5f) dc = l1 + l2 - 0.5f; if (dc < fabsf(l1 - l2) + 0.5f) dc = fabsf(l1 - l2) + 0.5f;
	float u[3] = { st[0] / d, st[1] / d, st[2] / d }, pe = se[0] * u[0] + se[1] * u[1] + se[2] * u[2], m[3] = { se[0] - pe * u[0], se[1] - pe * u[1], se[2] - pe * u[2] }, ml = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	if (ml < 1e-3f) return P; for (int k = 0; k < 3; k++) m[k] /= ml;   // (the elbow's side of the shoulder-hand line: kept)
	float ca = (l1 * l1 + dc * dc - l2 * l2) / (2 * l1 * dc), sa = sqrtf(fmaxf(0, 1 - ca * ca)), E2[3], W2[3];
	for (int k = 0; k < 3; k++) { E2[k] = S[k] + l1 * (ca * u[k] + sa * m[k]); W2[k] = S[k] + dc * u[k]; }
	float a0[3] = { se[0] / l1, se[1] / l1, se[2] / l1 }, a1[3] = { (E2[0] - S[0]) / l1, (E2[1] - S[1]) / l1, (E2[2] - S[2]) / l1 }, r1[4], r2[4], t[4], b0[3], b1[3], bw[3] = { ew[0] / l2, ew[1] / l2, ew[2] / l2 };
	og_q_arc(a0, a1, r1); cq_mul(r1, P[ua].q, t); memcpy(out[ua].q, t, sizeof(t)); cq_norm(out[ua].q);
	cq_rot(r1, bw, b0); for (int k = 0; k < 3; k++) b1[k] = (W2[k] - E2[k]) / l2;
	og_q_arc(b0, b1, r2); cq_mul(r1, P[fa].q, t); cq_mul(r2, t, out[fa].q); cq_norm(out[fa].q); memcpy(out[fa].p, E2, sizeof(E2));
	float mv[3] = { W2[0] - W[0], W2[1] - W[1], W2[2] - W[2] }, r21[4], dq[4] = { 0, 0, 0, 1 }; cq_mul(r2, r1, r21); cq_norm(r21);
	// the hand's own turn at that place (the node's axes, 'lhand_x' / 'lhand_y' of vm/make_w3.py): the palm lies on the gun as the
	// game's node has it - a hand that kept the rifle hold's turn reached across a short gun with its fingers ahead of the muzzle
	const float *lx = lh ? model_attach(G, "lhand_x") : NULL, *ly = lh ? model_attach(G, "lhand_y") : NULL; int turn = 0;
	if (lx && ly && !og_hand_noturn) { float X[3], Y[3], Z[3], ql[4], qt[4], hi[4] = { -P[hd].q[0], -P[hd].q[1], -P[hd].q[2], P[hd].q[3] };
		for (int k = 0; k < 3; k++) { X[k] = (lx[k] - lh[k]) / 10; Y[k] = (ly[k] - lh[k]) / 10; }
		Z[0] = X[1] * Y[2] - X[2] * Y[1]; Z[1] = X[2] * Y[0] - X[0] * Y[2]; Z[2] = X[0] * Y[1] - X[1] * Y[0];
		float tr = X[0] + Y[1] + Z[2];   // (the rotation whose columns are X, Y, Z, as a quaternion)
		if (tr > 0) { float s_ = sqrtf(tr + 1) * 2; ql[3] = s_ / 4; ql[0] = (Y[2] - Z[1]) / s_; ql[1] = (Z[0] - X[2]) / s_; ql[2] = (X[1] - Y[0]) / s_; }
		else if (X[0] > Y[1] && X[0] > Z[2]) { float s_ = sqrtf(1 + X[0] - Y[1] - Z[2]) * 2; ql[3] = (Y[2] - Z[1]) / s_; ql[0] = s_ / 4; ql[1] = (Y[0] + X[1]) / s_; ql[2] = (Z[0] + X[2]) / s_; }
		else if (Y[1] > Z[2]) { float s_ = sqrtf(1 + Y[1] - X[0] - Z[2]) * 2; ql[3] = (Z[0] - X[2]) / s_; ql[0] = (Y[0] + X[1]) / s_; ql[1] = s_ / 4; ql[2] = (Z[1] + Y[2]) / s_; }
		else { float s_ = sqrtf(1 + Z[2] - X[0] - Y[1]) * 2; ql[3] = (X[1] - Y[0]) / s_; ql[0] = (Z[0] + X[2]) / s_; ql[1] = (Z[1] + Y[2]) / s_; ql[2] = s_ / 4; }
		cq_norm(ql); cq_mul(g->q, ql, qt); cq_norm(qt); cq_mul(qt, hi, dq); cq_norm(dq); turn = 1; }
	if (say && turn) logp("menu figure:   the hand turned to the node's axes (b118)\n");
	for (int b = 0; b < B->nb; b++) {   // the arm's other bones go with their part: twist bones with the upper arm / the forearm, the fingers with the hand
		if (b == ua || b == fa) continue;
		int part = 0; for (int a = b, gdn = 0; a >= 0 && gdn < CB_MAXB; a = B->parent[a], gdn++) { if (a == hd) { part = 3; break; } if (a == fa) { part = 2; break; } if (a == ua) { part = 1; break; } }
		if (!part) continue;
		const float *o0 = part == 1 ? S : part == 2 ? E : W, *o1 = part == 1 ? S : part == 2 ? E2 : W2, *q_ = part == 1 ? r1 : part == 2 ? r21 : dq;
		if (part == 3 && !turn) { for (int k = 0; k < 3; k++) out[b].p[k] = P[b].p[k] + mv[k]; continue; }
		float rel[3] = { P[b].p[0] - o0[0], P[b].p[1] - o0[1], P[b].p[2] - o0[2] }, rr[3], tq[4]; cq_rot(q_, rel, rr);
		for (int k = 0; k < 3; k++) out[b].p[k] = o1[k] + rr[k];
		cq_mul(q_, P[b].q, tq); cq_norm(tq); memcpy(out[b].q, tq, sizeof(tq)); }
	return out;
}
static void chr_preview(int op, const char *wpn, const float *rect, const float *cam, float dt) {
	if (op < 0 || op >= 12) return; int ct = -1; for (int i = 0; i < nchrt; i++) if (!strcmp(chrt[i].name, MENU_CHR[op].stem)) ct = i; if (ct < 0) return;
	ChrType *C = &chrt[ct]; const CBank *B = &chr_bank[C->female]; if (!B->ok) return;
	int w = bot_wpn_index(wpn);
	if (ct != prev_ct || w != prev_w) {
		if (ct != prev_ct) { if (prev_sc.ok) skincache_free(&prev_sc); skincache_init(&prev_sc, &C->m); }
		int set = w >= 0 ? cb_find_set(B, BOT_WPN[w].sh) : -1; ca_init(&prev_ca, B, set < 0 ? 0 : set);   // (no events: no footsteps in the menus)
		logp("menu figure: %s, %s idle, %s%s\n", C->name, B->setname[set < 0 ? 0 : set], w >= 0 && bot_w3_ok[w] ? "w3_" : "no gun", w >= 0 && bot_w3_ok[w] ? BOT_WPN[w].sh : "");
		if (prev_w == -2) { int n = 0; for (int k = 0; k < NBOT_WPN; k++) n += bot_w3_ok[k] > 0;
			logp("menu figure: guns from the bots' w3 models (loaded with the characters: no extra loading; %d of %d present)\n", n, NBOT_WPN); }
		prev_ct = ct; prev_w = w;
	}
	CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1; ca_update(&prev_ca, &in, dt);
	MBone *P = ca_pose(&prev_ca, &in), pose[64];
	const CBank *OG = &chr_og[C->female]; int ogs = -1;
	if (chr_prev_choice && OG->ok) { char sn[24]; snprintf(sn, sizeof(sn), "op.%s", MENU_CHR[op].stem); ogs = cb_find_set(OG, sn); }
	int rset = chr_prev_result && OG->ok ? cb_find_set(OG, "result") : -1;
	if (rset >= 0) {   // b77: the result tabs
		int key = op * 4 + chr_prev_result; if (key != res_key) { res_key = key; res_t = 0; res_rd = 0; res_xf = 0; } else res_t += dt;
		static const char *in_[3] = { "res_win", "res_lose", "res_idle" }, *lp_[3] = { "res_win_idle", "res_lose_idle", "res_idle" }, *rd_[3] = { "res_win_rdy", "res_lose_rdy", "res_win_rdy" };
		int k = chr_prev_result - 1; if (k < 0 || k > 2) k = 2;
		static CLoc rL[CB_MAXB], rT[CB_MAXB]; static MBone rP[CB_MAXB];
		if (chr_prev_ready && !res_rd) { res_rd = 1; memcpy(res_from, rL, sizeof(res_from)); res_t = 0; res_xf = 0.35f; }   // (rL: the pose shown last frame)
		int c0 = cb_clip(OG, rset, cb_find_slot(OG, res_rd ? rd_[k] : in_[k])), c1 = cb_clip(OG, rset, cb_find_slot(OG, res_rd ? "res_rdy_idle" : lp_[k]));
		if (cb_pose_seq(OG, c0, c1, res_t, 0.3f, rL, rT, rP)) {
			if (res_xf > 0) { float w = msmooth(res_xf / 0.35f); CLoc tmp[CB_MAXB]; memcpy(tmp, rL, sizeof(tmp)); cb_blend(OG->nb, tmp, res_from, w, NULL); cb_fk(OG, tmp, 0, rP); res_xf -= dt; }
			P = rP; }
		og_op = -1;
	} else
	if (ogs >= 0) {
		static CLoc ogL[CB_MAXB], ogT[CB_MAXB]; static MBone ogP[CB_MAXB];
		if (op != og_op) { og_op = op; og_t = 0; char vl[48]; snprintf(vl, sizeof(vl), "v_character_select_%s", strcmp(MENU_CHR[op].stem, "kuro") ? MENU_CHR[op].stem : "emp");
			voice_stop_all(80); voice_play(vl, 230, 0);
			// b103: how far the arms reach forward (the figure faces -y) while Azuma's panel shows (0.40-1.45 s of the pose, skills.h
			// sk_menu_shield_k): the panel stands 8 cm past that - the pose holds the left forearm out in front of the chest
			og_sh_dist = 22;
			if (!strcmp(MENU_CHR[op].stem, "azuma")) { float reach = 0;
				for (float ts = 0.40f; ts <= 1.451f; ts += 0.05f)
					if (cb_pose_seq(OG, cb_clip(OG, ogs, cb_find_slot(OG, "choice")), cb_clip(OG, ogs, cb_find_slot(OG, "idle")), ts, 0.3f, ogL, ogT, ogP))
						for (int bq = 0; bq < OG->nb; bq++) if (strstr(OG->bone[bq], "Forearm") || strstr(OG->bone[bq], "Hand") || strstr(OG->bone[bq], "Finger") || strstr(OG->bone[bq], "ForeTwist")) { if (-ogP[bq].p[1] > reach) reach = -ogP[bq].p[1]; }
				if (reach + 8 > og_sh_dist) og_sh_dist = reach + 8; if (og_sh_dist > 80) og_sh_dist = 80;
				logp("menu figure: Azuma's panel %.0f cm in front of him (his arms reach %.0f cm while it shows; b103)\n", og_sh_dist, reach); } }
		else og_t += dt * og_slow;
		if (cb_pose_seq(OG, cb_clip(OG, ogs, cb_find_slot(OG, "choice")), cb_clip(OG, ogs, cb_find_slot(OG, "idle")), og_t, 0.3f, ogL, ogT, ogP)) P = ogP;
	} else { og_op = -1;   // b99: the lobby stance of the weapon's family (as the PC's lobby and armory figure; until b98 the match's aiming idle)
		static CLoc lbL[CB_MAXB], lbT[CB_MAXB]; static MBone lbP[CB_MAXB]; static float lb_t; lb_t += dt;
		if (OG->ok && cb_pose_seq(OG, -1, og_lobby_clip(OG, B, prev_ca.set, 0), lb_t, 0.3f, lbL, lbT, lbP)) P = lbP; }
	CBind *hb = NULL; SkinCache *hs = NULL; Model *HM = chr_hi_for(C, B, dt, &hb, &hs);   // b76: the high-detail figure when it is loaded
	// b105 (user: "kriss vector seems to clip through paz's arm in his operatives screen animation pose"): the PC's Paz does not
	// hold his gun in that pose at all (the Menu Navigation Ver. 2.0 video, 3:12-3:16): the right fist comes up bare and the wrist
	// blade of the Megatech knife opens out of its housing on that forearm, while the gun hangs from the LEFT hand at his side.
	// The port drew the gun at the pose's Grip, which stays in the right hand there - in line with the straight forearm, so any
	// gun with a stock ran through the arm. As the PC now: the gun on the operator model's own left-hand mount (Weapon_L of
	// the game's Paz_*_TPS.nif: half a turn about the hand's z, 6.76 / 3.27 / 0.64 cm from the wrist), and the knife (the game's
	// third-person CW_3rd.nif, w3_knife.gvm) on the forearm mount (Weapon_R_forearm of the same NIF) from the moment the fist
	// is up (1.4 s into the pose; the footage's blade opens between 1.2 and 1.7 s).
	static MBone og_fixP[CB_MAXB];   // b118: a short gun's support hand (og_short_gun above; not Paz's choice pose - his gun hangs from the left hand)
	if (w >= 0 && bot_w3_ok[w] && !(ogs >= 0 && !strcmp(MENU_CHR[op].stem, "paz"))) P = (MBone *)og_short_gun(B, P, &bot_w3[w], og_fixP, MENU_CHR[op].stem, BOT_WPN[w].sh);
	const MBone *gunb = &P[B->grip]; MBone gun_l, knife_b; int og_knife = 0;
	if (ogs >= 0 && !strcmp(MENU_CHR[op].stem, "paz")) { int lh = -1, rf = -1;
		for (int bq = 0; bq < OG->nb; bq++) { if (!strcmp(OG->bone[bq], "Bip01_L_Hand")) lh = bq; else if (!strcmp(OG->bone[bq], "Bip01_R_Forearm")) rf = bq; }
		if (lh >= 0) { static const float ql[4] = { 0, 0, 1, 0 }, tl[3] = { 6.756f, 3.273f, 0.637f }; cq_mul(P[lh].q, ql, gun_l.q); model_bone_xform(&P[lh], tl, gun_l.p); gunb = &gun_l;
			// b109 (user: "paz just has a gun clipping through his left arm and isn't really holding it it's just stuck to his arm"): with the
			// NIF's half turn about the hand's z the gun pointed back UP the forearm from the wrist and lay through it (the Vita, and the
			// emulator's close-up). The bank's left hand is not the NIF's mirror frame: without the turn the gun hangs from the hand, muzzle
			// down beside the leg, as the PC's frames (work\b105ref\menunav\paz_in.jpg) show it.
			// b118 (user: "paz also doesn't look like he's holding the pp-2200 correctly on the operatives screen it just look like it's
			// stuck to and hanging out his left arm"): with the hand's own turn the gun's top lay toward the little finger - the
			// whole gun upside down against the hand, on the outside of the arm (side views of the emulator; from the front it
			// passed). The left hand's hold is the right hand's mirrored: the right hand has the gun at 5.4 / 2.6 / -0.7 cm with
			// no turn (its top toward the thumb, the palm on its right side; logged by og_short_gun), and the two hand bones differ
			// by their z axis - so in the left hand the gun is turned half a turn about the hand's x: the grip in the palm, the top
			// toward the thumb, the muzzle down, a stock up along the thumb's side of the forearm instead of through it.
			{ static const float qx[4] = { 1, 0, 0, 0 }; cq_mul(P[lh].q, qx, gun_l.q); cq_norm(gun_l.q); } }
		if (rf >= 0 && bot_w3_knife_ok > 0 && og_t >= 1.4f) { static const float qk[4] = { 0.7529f, -0.0109f, -0.0185f, 0.6577f }; cq_mul(P[rf].q, qk, knife_b.q); memcpy(knife_b.p, P[rf].p, sizeof(knife_b.p)); og_knife = 1; } }
	cb_to_model(HM ? hb : &C->bind, P, pose);
	int x = (int)rect[0], y = (int)rect[1], w_ = (int)rect[2], h = (int)rect[3]; if (w_ < 8 || h < 8) return;
	int e = 544 - y - h; if (e < 0) e = 0; int he = h + e;   // b95: the view runs on to the screen's bottom (room for the floor's reflection)
	const int fold = menu_fold_on(); if (!fold) { glViewport(x, 0, w_, he); glEnable(GL_SCISSOR_TEST); glScissor(x, 0, w_, he); glClear(GL_DEPTH_BUFFER_BIT); } else menu_fold_depth();
	float Pm[16], V[16], VP[16], fwd[3], yaw = cam[4] * 3.14159f / 180, d = cam[1], eye[3] = { sinf(yaw) * d, -cosf(yaw) * d, cam[0] };
	persp(Pm, cam[3] * 3.14159f / 180, (float)w_ / h, 10, 3000); Pm[8] = -menu_prev_shift; menu_prev_shift = 0;   // (b76: the lens shift, TOP OPERATIVE)
	Pm[5] *= (float)h / he; Pm[9] = -(float)e / he;   // (b95: the same lens, its frustum stretched downward: the figure stays put)
	view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(cam[2] - cam[0], d), fwd); mul4(VP, Pm, V); if (fold) menu_fold_vp(VP, x, 0, w_, he);
	glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
	const float cy_ = cosf(yaw), sy_ = sinf(yaw), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 }, tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };   // b57: the light turns with the camera (a spun figure stays lit)
	md_rim = BOT_RIM;   // (b62: rim light, as the bots)
	md_nogog = (cfg.gog_off[op] >> (C->skin & 15)) & 1;   // b103: the skin's mask / goggles taken off on the operative screen (Square)
	// b95 (user: "make the floor reflective for operatives in the menus like they are in the footage of the pc version"): as the
	// Think Tank's (b78) - the operator's mirror image in the floor (z = 0, the feet) first, alpha 0.3, fogged with depth toward
	// the backdrop's floor teal; the depth cleared, then the operator over it
	{ float Mr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1 }, RVP[16]; mul4(RVP, VP, Mr);
	  const float rt[4] = { 0.9f, 0.95f, 1.0f, 0.30f }, fogc[4] = { 0.07f, 0.19f, 0.22f, 1 }; float fp[4]; memcpy(fp, md_fogp, sizeof(fp));
	  float de = sqrtf(d * d + cam[0] * cam[0]); md_fogp[0] = de - 30; md_fogp[1] = 1.0f / 70; md_fogp[2] = 1;
	  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	  model_draw_cpu(HM ? HM : &C->m, HM ? hs : &prev_sc, RVP, pose, ld, rt, fogc, 1);
	  if (w >= 0 && bot_w3_ok[w]) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); chr_draw_rigid(&bot_w3[w], RVP, gunb, ld, rt, fogc); }
	  if (og_knife) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); chr_draw_rigid(&bot_w3_knife, RVP, &knife_b, ld, rt, fogc); }
	  glDisable(GL_BLEND); memcpy(md_fogp, fp, sizeof(fp)); if (fold) menu_fold_depth(); else glClear(GL_DEPTH_BUFFER_BIT); }
	model_draw_cpu(HM ? HM : &C->m, HM ? hs : &prev_sc, VP, pose, ld, tint, nofog, 1);   // (chars b35: CPU skinning unless the GPU path is verified; b76 the high-detail model)
	if (HM) hi_m.tex = NULL;   // (lent for the draw only)
	md_nogog = 0;
	if (w >= 0 && bot_w3_ok[w]) chr_draw_rigid(&bot_w3[w], VP, gunb, ld, tint, nofog);   // ui b50: the gun in the hand (bots.h; b105: Paz's choice pose - in the left hand)
	if (og_knife) chr_draw_rigid(&bot_w3_knife, VP, &knife_b, ld, tint, nofog);
	md_rim = 0;
	if (ogs >= 0 && !strcmp(MENU_CHR[op].stem, "azuma") && sk_menu_shield_k(og_t) > 0) {   // b63: the shield as he touches his wrist
		float R[3] = { fwd[1], -fwd[0], 0 }, rl = sqrtf(R[0] * R[0] + R[1] * R[1]); if (rl > 1e-4f) { R[0] /= rl; R[1] /= rl; }
		float U[3] = { R[1] * fwd[2] - R[2] * fwd[1], R[2] * fwd[0] - R[0] * fwd[2], R[0] * fwd[1] - R[1] * fwd[0] };
		sk_draw_menu_shield(VP, v3(eye[0], eye[1], eye[2]), v3(0, 0, 0), v3(0, -1, 0), R, U, og_t, og_sh_dist);
	}
	if (!fold) { glDisable(GL_SCISSOR_TEST); glViewport(0, 0, 960, 544); } glEnable(GL_CULL_FACE);
}
#include "wpn_insp.h"   // b89: the armory's INSPECT view
// b58 (footage: before TOP OPERATIVE the side stands together on the stage, a name over each operator): up to 5 operators
// in a V (menu_results_lineup: the top operative in front), each turned partly towards the camera plus the stick / touch spin,
// each with its own idle (the gun that player used) started at a different time. Skinned together (one skin_run), as the
// bots are. Writes the screen points over the heads for menu.h's name tags.
static CAnim lu_ca[5]; static int lu_ct[5] = { -1, -1, -1, -1, -1 }, lu_w[5] = { -2, -2, -2, -2, -2 }; static MBone lu_pose[5][64], lu_hpose[64];
// b96 (Demolition video 573.0-573.5 s): as the camera closes in on the top operative the team behind goes out of focus. The team
// is drawn into a small target (LU_BW x LU_BH, its own depth), blurred there (13 taps, 2 texels: ~8 px on the screen) into a
// second one and brought up to half the screen's size in a third (4 taps: smooth whatever the texture filter does), which is
// laid over the screen. All three before anything is drawn on the screen (a change of target mid-frame loses the screen's
// picture: b96-1 in the emulator showed a white backdrop). lu_fb_ok 0: no targets - the team is hazed toward the backdrop.
#define LU_OUT 0.62f   // b99: the others' turn outward in the results poses (rad)
#define LU_BW 240
#define LU_BH 136
static GLuint lu_fb[3], lu_tx[3], lu_rb; static int lu_fb_ok = -1;
static int lu_blur_init(void) {
	if (lu_fb_ok >= 0) return lu_fb_ok;
	int ok = 1; GLenum st = 0;
	for (int i = 0; i < 3; i++) { int w = i == 2 ? LU_BW * 2 : LU_BW, h = i == 2 ? LU_BH * 2 : LU_BH;
		glGenTextures(1, &lu_tx[i]); glBindTexture(GL_TEXTURE_2D, lu_tx[i]); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glGenFramebuffers(1, &lu_fb[i]); glBindFramebuffer(GL_FRAMEBUFFER, lu_fb[i]); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lu_tx[i], 0);
		if (i == 0) { glGenRenderbuffers(1, &lu_rb); glBindRenderbuffer(GL_RENDERBUFFER, lu_rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, w, h);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, lu_rb); }
		st = glCheckFramebufferStatus(GL_FRAMEBUFFER); ok = ok && st == GL_FRAMEBUFFER_COMPLETE && lu_fb[i] && lu_tx[i]; }
	glBindFramebuffer(GL_FRAMEBUFFER, 0); lu_fb_ok = ok;
	logp("results: the team's out-of-focus layer %dx%d %s (status 0x%x, b96)\n", LU_BW, LU_BH, lu_fb_ok ? "ready" : "not available: hazed instead", (unsigned)st);
	return lu_fb_ok;
}
// the queued sprite quads with a blend function of the caller's (colour, alpha): the layer's passes work in premultiplied alpha
static void lu_flush(GLuint tex, GLenum sc, GLenum dc, GLenum sa, GLenum da) {
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	if (!sp_n || !sp_ok) { sp_n = 0; return; }
	glUseProgram(sp_prog); for (int r = 0; r < 4; r++) { float row[4] = { I[r], I[4 + r], I[8 + r], I[12 + r] }; glUniform4fv(sp_u_m[r], 1, row); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_BLEND); glBlendFuncSeparate(sc, dc, sa, da); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, sp_v); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, sp_v + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, sp_v + 5);
	glDrawArrays(GL_TRIANGLES, 0, sp_n);
	glDepthMask(GL_TRUE); glDisable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_DEPTH_TEST); sp_n = 0;
}
static void lu_quad(float dx, float dy, const float *col) {   // the whole target, shifted (ndc)
	float q0[3] = { -1 + dx, 1 + dy, 0 }, q1[3] = { 1 + dx, 1 + dy, 0 }, q2[3] = { 1 + dx, -1 + dy, 0 }, q3[3] = { -1 + dx, -1 + dy, 0 };
	sprite_quad(q0, q1, q2, q3, 0, 1, 1, 0, col);
}
// stage 0 (before anything is drawn on the screen this frame): the poses, the skinning and, when the team is out of focus, its
// layer - the screen is cleared again after it; stage 1: the reflections, the team, the layer, the top operative
static void chr_lineup(int stage, int n, const int *op, const char **wpn, const int *pi, const float *cam, float spin, float dt) {
	// b99 (user: "make sure that the top operative screen has the correct animations and poses" + two PC screenshots; the Demolition
	// video 567.5-576 s): the PC's line-up stands in the lobby stances, not in the match's aiming stance - each operative in the
	// stance of the weapon's family (a rifle or an SMG held low across the body), close in behind the top operative's shoulders
	// and turned outward; the top operative with the gun hanging from the right hand and the left hand on the hip
	// (og_lobby_clip). SLOT_R / LU_OUT fitted to the footage's line-up (the others' heads 22% of the screen's width to each side).
	static const float SLOT_A[5][2] = { { 0, 0 }, { -95, 50 }, { 95, 50 }, { -190, 100 }, { 190, 100 } }, SLOT_R[5][2] = { { 0, 0 }, { -62, 34 }, { 62, 34 }, { -122, 64 }, { 122, 64 } };
	const int lres = og_lobby_clip(&chr_og[0], &chr_bank[0], 0, 0) >= 0 && og_lobby_clip(&chr_og[1], &chr_bank[1], 0, 0) >= 0;
	const float (*SLOT)[2] = lres ? SLOT_R : SLOT_A;
	// (test runs: ux0:data/gitsfa/lutest_knife - every operative of the line-up with the Megatech Knife, in the knife's stance)
	static int lu_kt = -1; if (lu_kt < 0) { FILE *tf = fopen(VDIR "lutest_knife", "rb"); lu_kt = tf != NULL; if (tf) fclose(tf); }
#define LU_HAS(k) (lu_kt > 0 ? bot_w3_knife_ok > 0 : wk[k] >= 0 && bot_w3_ok[wk[k]])
#define LU_W3(k) (lu_kt > 0 ? &bot_w3_knife : &bot_w3[wk[k]])
// b107 (the user: "on the top operative screen motoko is facing left instead of front facing with the animation while it zooms
// in on her"; the PC, two of the user's videos with Motoko on top - Demolition Headquarters 692-696 s, Elimination Urban's
// end: the top operative faces the camera in the close-up, eye to eye): the close-up's camera swings 20 degrees round the
// figure (CU below), and the figure used to stand still - seen from the side, the head (turned a little to her right in
// the stance) looking off the screen. Now the top operative turns with the camera as it closes in, and LU_FACE further,
// so the face comes square to the lens.
#define LU_FACE 0.14f
#define LU_HEAD_Z 151.0f   // (the head joint's height the close-up's camera is level with. Emulator b109-20/21: Motoko's stands at 156.9 -> the camera 5.9 up, the hair's top at 13% of the height as in the user's PC pictures; the Operator's at 160.0 -> 9.0 up, his hair inside the picture)
#define LU_TURN(k, x, y) (0.7f * atan2f(-(x), d + (y)) + (lres && (k) ? ((x) < 0 ? -1 : 1) * LU_OUT : 0) + ((k) ? 0 : yaw + LU_FACE * fo) + spin * 3.14159f / 180)
	static int ctk[5], wk[5]; static MBone *Pk[5]; static CBind *hb; static SkinCache *hs; static Model *HM;
	if (n > 5) n = 5; if (n <= 0) return;
	// b96: the camera between the line-up's (cam) and TOP OPERATIVE's close-up (b76's: eye 150, 170 cm away, look-at 140, 34 deg,
	// turned 15 deg, the picture's centre at x 310 - menu.h), mrs_focus 0..1
	// b109: the close-up's heights follow the top operative's head joint (lu_cu_dz, taken from the stance while the line-up shows):
	// at one fixed height the Operator stood with his hair out of the picture's top and the shoulder plate up in the title's
	// place (emulator b109-19)
	static const float CU0[5] = { 150, 236, 146, 17.15f, 20 }; static float lu_cu_dz; float CU[5]; memcpy(CU, CU0, sizeof(CU)); CU[0] += lu_cu_dz; CU[2] += lu_cu_dz; const float fo = mrs_focus, csh = (345.f / 480.f - 1) * fo;   // (b99: closer and further right, as the user's PC screenshots: the head a third of the screen tall, the figure's middle at x 38%; b107: 36% - turned to the lens the shoulder reached the title)
	float cc[5]; for (int i = 0; i < 5; i++) cc[i] = cam[i] + (CU[i] - cam[i]) * fo;
	const float sl = mrs_slide;   // b99: the line-up's camera slides in from the side (the game's cam_result: 557 cm in a third of a second)
	float Pm[16], V[16], VP[16], fwd[3], d = cc[1], yaw = cc[4] * 3.14159f / 180, eye[3] = { sinf(yaw) * d, -cosf(yaw) * d, cc[0] };
	persp(Pm, cc[3] * 3.14159f / 180, 960.f / 544.f, 10, 4000); Pm[8] = -csh; view(V, eye, atan2f(-eye[1], -eye[0]), atan2f(cc[2] - cc[0], d), fwd); mul4(VP, Pm, V);
	const int bok = n > 1 && lu_blur_init() > 0, blur = fo > 0.12f && bok, haze = fo > 0.12f && n > 1 && !bok;   // (the targets made as the line-up starts; out of focus from the move's start)
	const float ba = sk_clamp((fo - 0.12f) / 0.25f, 0, 1);   // the out-of-focus layer over the sharp team (a cross-fade, 0.1 s)
	const float tint[4] = { 1.1f, 1.1f, 1.1f, 1 }, nofog[4] = { 0, 0, 0, 0 };
	if (stage == 0) {
		HM = NULL; hb = NULL; hs = NULL;
		for (int k = 0; k < n; k++) {
			ctk[k] = -1; if (op[k] < 0 || op[k] >= 12) continue;
			for (int i = 0; i < nchrt; i++) if (!strcmp(chrt[i].name, MENU_CHR[op[k]].stem)) ctk[k] = i;
			if (ctk[k] < 0) continue; ChrType *C = &chrt[ctk[k]]; const CBank *B = &chr_bank[C->female]; if (!B->ok) { ctk[k] = -1; continue; }
			int w = bot_wpn_index(wpn[k]); wk[k] = w;
			CInput in; memset(&in, 0, sizeof(in)); in.grounded = 1;
			const int fresh = ctk[k] != lu_ct[k] || w != lu_w[k];
			if (fresh) { int set = lu_kt > 0 ? cb_find_set(B, "knife") : w >= 0 ? cb_find_set(B, BOT_WPN[w].sh) : -1; ca_init(&lu_ca[k], B, set < 0 ? 0 : set); lu_ct[k] = ctk[k]; lu_w[k] = w; ca_update(&lu_ca[k], &in, 0.83f * k); }
			ca_update(&lu_ca[k], &in, dt); MBone *P = ca_pose(&lu_ca[k], &in);
			if (lres) {   // b99: the lobby stances (a 6 s loop each, the others out of step with the top operative)
				const CBank *OG = &chr_og[C->female]; static CLoc rL[CB_MAXB], rT[CB_MAXB]; static MBone rP[5][CB_MAXB]; static float rtm[5];
				rtm[k] = fresh ? 0.83f * k : rtm[k] + dt;
				if (cb_pose_seq(OG, -1, og_lobby_clip(OG, B, lu_ca[k].set, k == 0), rtm[k], 0.3f, rL, rT, rP[k])) { P = rP[k]; if (k == 0) lu_head_level(OG, B, rtm[k], rP[k]); }
			}
			{ static MBone lu_fix[5][CB_MAXB];   // b118: the left hand on a gun that has its own place for it (og_short_gun)
			  if (lu_kt <= 0 && w >= 0 && bot_w3_ok[w]) P = (MBone *)og_short_gun(B, P, &bot_w3[w], lu_fix[k], "line-up", BOT_WPN[w].sh); }
			Pk[k] = P; cb_to_model(&C->bind, P, lu_pose[k]);
			if (k == 0 && fo <= 0.001f) { int hd = cb_find_bone(B, "Bip01_Head"); static float was = -99;
				if (hd >= 0) { lu_cu_dz = sk_clamp(P[hd].p[2] - LU_HEAD_Z, -12, 30); if (fabsf(lu_cu_dz - was) > 1.5f) { was = lu_cu_dz; logp("lineup: the top operative's head at %.1f cm, the close-up %+.1f cm\n", P[hd].p[2], lu_cu_dz); } } }
			if (k == 0 && (HM = chr_hi_for(C, B, dt, &hb, &hs)) != NULL) { cb_to_model(hb, P, lu_hpose); model_skin_queue(HM, hs, lu_hpose); }   // b96: the top operative in the menus' high detail (b76; loaded during the line-up)
			else model_skin_queue(&C->m, NULL, lu_pose[k]);
		}
		skin_run();
		if (!blur) return;
		glBindFramebuffer(GL_FRAMEBUFFER, lu_fb[0]); glViewport(0, 0, LU_BW, LU_BH); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
	}
	if (stage == 1) {
		mrs_ntag = 0; glViewport(0, 0, 960, 544); glClear(GL_DEPTH_BUFFER_BIT); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
		// b96 (user: the operatives' reflections in the menus, the PC's result line-up; EP16 0:21-0:24 shows the feet mirrored faintly in
		// the stage floor, fading below): as chr_preview's (b95) - each operator's mirror image in the floor (z = 0) first, alpha 0.3,
		// fogged with its depth below the floor toward the backdrop's floor teal; the depth cleared, then the operators over them.
		// (The same pose as the operator's: model_skin_get finds it skinned this frame, so the mirror costs a draw, no skinning.)
		const float rt[4] = { 0.9f, 0.95f, 1.0f, 0.30f }, fogc[4] = { 0.07f, 0.19f, 0.22f, 1 }; float fp[4]; memcpy(fp, md_fogp, sizeof(fp));
		static const float Mr[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1 };
		for (int k = 0; k < n; k++) {
			if (ctk[k] < 0 || (k && (blur || haze))) continue; ChrType *C = &chrt[ctk[k]]; const CBank *B = &chr_bank[C->female];   // (the team's: not once out of focus - the floor is out of the close-up)
			float x = SLOT[k][0], y = SLOT[k][1], r = LU_TURN(k, x, y), cr = cosf(r), sr = sinf(r); x += sl;
			float Mm[16] = { cr, sr, 0, 0, -sr, cr, 0, 0, 0, 0, 1, 0, x, y, 0, 1 }, MVP[16], RMVP[16]; mul4(MVP, VP, Mm); mul4(RMVP, MVP, Mr);
			float ry = -r, cy_ = cosf(ry), sy_ = sinf(ry), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 };
			float ex = eye[0] - x, ey = eye[1] - y, de = sqrtf(ex * ex + ey * ey + cc[0] * cc[0]); md_fogp[0] = de - 30; md_fogp[1] = 1.0f / 70; md_fogp[2] = 1;   // (the fog from the feet down)
			glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			md_nogog = gog_off_ct(ctk[k]);   // (b103)
			if (k == 0 && HM) model_draw_cpu(HM, hs, RMVP, lu_hpose, ld, rt, fogc, 1); else model_draw_cpu(&C->m, NULL, RMVP, lu_pose[k], ld, rt, fogc, 1);
			md_nogog = 0;
			if (LU_HAS(k)) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); chr_draw_rigid(LU_W3(k), RMVP, &Pk[k][B->grip], ld, rt, fogc); }
		}
		glDisable(GL_BLEND); memcpy(md_fogp, fp, sizeof(fp)); glClear(GL_DEPTH_BUFFER_BIT);
		// the name tags' points over the heads
		for (int k = 0; k < n; k++) { if (ctk[k] < 0) continue;
			float hp[4] = { SLOT[k][0] + sl, SLOT[k][1], 182, 1 }, c[4];   /* (just over the head: the operators stand ~170 tall in the aim idles) */ for (int i = 0; i < 4; i++) c[i] = VP[i] * hp[0] + VP[4 + i] * hp[1] + VP[8 + i] * hp[2] + VP[12 + i];
			if (c[3] > 1 && mrs_ntag < 6) { mrs_tag[mrs_ntag][0] = (c[0] / c[3] * 0.5f + 0.5f) * 960; mrs_tag[mrs_ntag][1] = (0.5f - c[1] / c[3] * 0.5f) * 544; mrs_tagp[mrs_ntag++] = pi[k]; } }
		// b109: the top operative's outline for the title's place (menu.h mrs_title_place): the figure's own vertices in this pose
		// (the skin plans' blocks, each with its first bone - one bone moves 72-87% of them whole), seen by the close-up's FINAL
		// camera (so the place does not move while the camera closes in); per bone the right-hand hull over 8 px rows (the armour's
		// plates are large: their vertices alone leave rows empty), the furthest right of them all per row in mrs_row.
		// (A first version put circles about the bones: the Operator's shoulder plate is no part of any such circle, and the
		// title lay across it - emulator, b109-19.)
		mrs_nfig = 0;
		if (fo > 0 && ctk[0] >= 0 && Pk[0]) {
			const ChrType *C0 = &chrt[ctk[0]]; const Model *OM = HM ? HM : &C0->m; const MBone *OP = HM ? lu_hpose : lu_pose[0];
			const float d1 = CU[1], yaw1 = CU[4] * 3.14159f / 180, fov1 = CU[3] * 3.14159f / 180; float e1[3] = { sinf(yaw1) * d1, -cosf(yaw1) * d1, CU[0] }, P1[16], V1[16], VP1[16], MV1[16], f1[3];
			persp(P1, fov1, 960.f / 544.f, 10, 4000); P1[8] = -(345.f / 480.f - 1); view(V1, e1, atan2f(-e1[1], -e1[0]), atan2f(CU[2] - CU[0], d1), f1); mul4(VP1, P1, V1);
			const float x = SLOT[0][0], y = SLOT[0][1], r1 = 0.7f * atan2f(-x, d1 + y) + yaw1 + LU_FACE + spin * 3.14159f / 180, cr = cosf(r1), sr = sinf(r1);
			{ float Mm[16] = { cr, sr, 0, 0, -sr, cr, 0, 0, 0, 0, 1, 0, x, y, 0, 1 }; mul4(MV1, VP1, Mm); }
			static float cl[48][MRS_ROWS]; unsigned char used[48]; memset(used, 0, sizeof(used)); float pal[48 * 12]; model_palette(OM, OP, pal);
			md_nogog = gog_off_ct(ctk[0]); md_lod_sel = 0;
			for (int i = 0; i < OM->nmesh; i++) { const MMesh *m = &OM->mesh[i]; const struct SkinPlan *sp = m->sp; if (!sp || !md_lod_ok(m) || !md_mesh_vis(m) || MD_BM_LATE(m->blend & 15)) continue;
				for (int b = 0, nb = sp->nb1 + sp->nb2 + sp->nb3; b < nb; b++) { int e = sp->bone[b * 4]; if (e == 255) continue; e += m->pbase; if (e < 0 || e >= OM->npal || e >= 48) continue;
					const float *R = &pal[e * 12], *X = &sp->xyz[b * 12];
					for (int l = 0; l < 4; l++) { const float px = X[l], py = X[4 + l], pz = X[8 + l], o[3] = { R[0] * px + R[1] * py + R[2] * pz + R[3], R[4] * px + R[5] * py + R[6] * pz + R[7], R[8] * px + R[9] * py + R[10] * pz + R[11] };
						const float cw = MV1[3] * o[0] + MV1[7] * o[1] + MV1[11] * o[2] + MV1[15]; if (cw <= 20) continue;
						const float sx = ((MV1[0] * o[0] + MV1[4] * o[1] + MV1[8] * o[2] + MV1[12]) / cw * 0.5f + 0.5f) * 960, sy = (0.5f - (MV1[1] * o[0] + MV1[5] * o[1] + MV1[9] * o[2] + MV1[13]) / cw * 0.5f) * 544;
						const int row = (int)floorf(sy / MRS_ROWH); if (row < 0 || row >= MRS_ROWS) continue;
						if (!used[e]) { used[e] = 1; for (int q = 0; q < MRS_ROWS; q++) cl[e][q] = -1e9f; }
						if (sx > cl[e][row]) cl[e][row] = sx; } } }
			md_nogog = 0;
			for (int q = 0; q < MRS_ROWS; q++) mrs_row[q] = -1e9f;
			for (int e = 0; e < 48; e++) { if (!used[e]) continue; int hk[MRS_ROWS], nh = 0;   // the hull from the right: rows with a point, kept while they turn the same way
				for (int q = 0; q < MRS_ROWS; q++) { if (cl[e][q] < -1e8f) continue;
					while (nh >= 2) { const int a = hk[nh - 2], b = hk[nh - 1]; if ((cl[e][b] - cl[e][a]) * (q - a) <= (cl[e][q] - cl[e][a]) * (b - a)) nh--; else break; }
					hk[nh++] = q; }
				for (int h = 0; h < nh; h++) { const int a = hk[h], b = h + 1 < nh ? hk[h + 1] : a;
					for (int q = a; q <= b; q++) { const float v = b > a ? cl[e][a] + (cl[e][b] - cl[e][a]) * (q - a) / (float)(b - a) : cl[e][a]; if (v > mrs_row[q]) mrs_row[q] = v; } } }
			mrs_nfig = 1;
		}
	}
	// the figures. Stage 0: the team into the first target. Stage 1: pass 0 the team on the screen (sharp: the line-up, and
	// under the layer while it fades in), then the layer over it, pass 1 the top operative over it all
	for (int pass = 0; pass < (stage ? 2 : 1); pass++) {
		if (stage && pass == 0 && blur && ba >= 1) goto layer;
		for (int k = pass ? 0 : 1; pass ? k < 1 : k < n; k++) {
			if (ctk[k] < 0) continue; ChrType *C = &chrt[ctk[k]]; const CBank *B = &chr_bank[C->female];
			float x = SLOT[k][0], y = SLOT[k][1], r = LU_TURN(k, x, y), cr = cosf(r), sr = sinf(r); x += sl;
			float Mm[16] = { cr, sr, 0, 0, -sr, cr, 0, 0, 0, 0, 1, 0, x, y, 0, 1 }, MVP[16]; mul4(MVP, VP, Mm);
			float ry = -r, cy_ = cosf(ry), sy_ = sinf(ry), ld[4] = { 0.30f * cy_ + 0.70f * sy_, 0.30f * sy_ - 0.70f * cy_, 0.65f, 0 };   // the light from the camera's side, in the figure's frame
			float fp[4], hz[4] = { 0.07f, 0.19f, 0.22f, 1 }; memcpy(fp, md_fogp, sizeof(fp));
			if (k && haze) { md_fogp[0] = 0; md_fogp[1] = 1; md_fogp[2] = 0.55f * fo; }   // (no targets: the team recedes into the backdrop's haze)
			md_rim = BOT_RIM;   // (b62: rim light, as the bots)
md_nogog = gog_off_ct(ctk[k]); 			if (k == 0 && HM) model_draw_cpu(HM, hs, MVP, lu_hpose, ld, tint, nofog, 1); else model_draw_cpu(&C->m, NULL, MVP, lu_pose[k], ld, tint, k && haze ? hz : nofog, 1); md_nogog = 0;
			if (LU_HAS(k)) chr_draw_rigid(LU_W3(k), MVP, &Pk[k][B->grip], ld, tint, k && haze ? hz : nofog);
			md_rim = 0; memcpy(md_fogp, fp, sizeof(fp));
		}
	layer:
		if (stage && pass == 0 && blur) {   // the layer (premultiplied) over the screen, the top operative then over it (the layer has no depth)
			float col[4] = { ba, ba, ba, ba }; lu_quad(0, 0, col); lu_flush(lu_tx[2], GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_CULL_FACE); glClear(GL_DEPTH_BUFFER_BIT); }
	}
	if (stage == 0) {   // the team's picture blurred into the second target, brought up into the third; back to the screen, cleared again
		static const signed char TAP[13][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 2, 0 }, { -2, 0 }, { 0, 2 }, { 0, -2 }, { 1, 1 }, { -1, 1 }, { 1, -1 }, { -1, -1 } };
		glBindFramebuffer(GL_FRAMEBUFFER, lu_fb[1]); glViewport(0, 0, LU_BW, LU_BH); glClear(GL_COLOR_BUFFER_BIT);
		{ float col[4] = { 1, 1, 1, 1.0f / 13 }; for (int i = 0; i < 13; i++) lu_quad(TAP[i][0] * 2.0f / LU_BW, TAP[i][1] * 2.0f / LU_BH, col);
		  lu_flush(lu_tx[0], GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE); }   // (the first target is not premultiplied: colour x its alpha x 1/13, alpha x 1/13)
		glBindFramebuffer(GL_FRAMEBUFFER, lu_fb[2]); glViewport(0, 0, LU_BW * 2, LU_BH * 2); glClear(GL_COLOR_BUFFER_BIT);
		{ float col[4] = { 0.25f, 0.25f, 0.25f, 0.25f }; for (int i = 0; i < 4; i++) lu_quad((i & 1 ? 0.5f : -0.5f) * 2.0f / (LU_BW * 2), (i & 2 ? 0.5f : -0.5f) * 2.0f / (LU_BH * 2), col);
		  lu_flush(lu_tx[1], GL_ONE, GL_ONE, GL_ONE, GL_ONE); }
		glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 960, 544); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		return;
	}
	if (HM) hi_m.tex = NULL;   // (lent for the draw only, as chr_preview)
	glEnable(GL_CULL_FACE);
}
// b57 (user: spin the operator with the right stick and the touch screen wherever the menus show one): an angle added to the
// figure's camera. Right stick: up to 240 degrees a second; a drag that starts on the figure turns it with the finger
// (0.6 degree a pixel) and a flick keeps it turning, slowing down (half a second). A new screen starts facing as designed.
static float fig_spin, fig_vel, fig_zoom; static int fig_key = -999, fig_drag, fig_zoom_en;   // b76: fig_zoom 0..1 where the caller sets fig_zoom_en (the OPERATIVE screen)
static void fig_input(const SceCtrlData *pd, const float *rc, int key, float dt) {
	if (key != fig_key) { fig_key = key; fig_spin = 0; fig_vel = 0; fig_drag = 0; }
	int rx = (int)pd->rx - 128; if (rx > 24 || rx < -24) { float k = (rx > 0 ? rx - 24 : rx + 24) / 103.0f; fig_spin += k * 240 * dt; fig_vel = 0; }
	if (MUT.down) {   // (the press's start point decides: a drag that starts on the figure)
		fig_drag = MUT.sx >= rc[0] && MUT.sx <= rc[0] + rc[2] && MUT.sy >= rc[1] && MUT.sy <= rc[1] + rc[3];
		if (fig_drag && MUT.moved) { float d = -MUT.dx * 0.6f;   /* b59 (user: it turned the wrong way): the side under the finger follows it (fig_spin turns the camera round) */ fig_spin += d; fig_vel = dt > 0 ? fig_vel * 0.5f + 0.5f * d / dt : 0; }
	} else { fig_drag = 0; if (fig_vel != 0) { fig_spin += fig_vel * dt; fig_vel *= expf(-dt / 0.5f); if (fabsf(fig_vel) < 5) fig_vel = 0; } }
	if (fig_spin > 180) fig_spin -= 360; else if (fig_spin < -180) fig_spin += 360;
	if (fig_zoom_en) {   // b76 (user: zoom in and out with the right stick): stick up = closer (to the head in ~0.7 s), a drag up on the figure too
		int ry = (int)pd->ry - 128; if (ry > 24 || ry < -24) fig_zoom -= (ry > 0 ? ry - 24 : ry + 24) / 103.0f * 1.4f * dt;
		if (fig_drag && MUT.down && MUT.moved) fig_zoom -= MUT.dy * 0.004f;
		fig_zoom = fig_zoom < 0 ? 0 : fig_zoom > 1 ? 1 : fig_zoom;
	} else fig_zoom = 0;
}
// loading screen: the game's loading page with a progress bar, drawn between loading steps
static const char *ld_stem, *ld_name; static int ld_on;
static int tut_loading_draw(int prog, float progress);   // tutorial.h: the training programs' loading page
static void loading_progress(float p, const char *status) {
	if (!ld_on) { if (boot_scr_on) boot_step(boot_scr_a + (boot_scr_b - boot_scr_a) * (p < 0 ? 0 : p > 1 ? 1 : p)); return; }   // (b105: the start-up load's own screen)
	LDP_PUSH(LDP_SCREEN);   // perf (b45): load phases (ldprof.h)
	glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	menu_tick(0.016f); if (!tut_loading_draw(cfg.tut, p)) menu_loading_draw(ld_stem, ld_name, cfg.mode, p, status, cfg.chr);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	vglSwapBuffers(GL_FALSE);
	LDP_POP();
}
// test runs: the map's textures exactly as the GPU holds them (swizzled DXT) -> texdump_<stem>.bin, for host-side checks
static void tex_dump(const char *stem) { char p[96]; snprintf(p, sizeof(p), VDIR "texdump_%s.bin", stem); world_tex_dump(p); }
// ---- Demolition (dem.h): two bomb sites, rounds without respawns, attack / defense swap after half the match
static DemMap dmap; static DemMatch dm; static int dem_on; static Model dem_bomb; static int dem_bomb_ok;
static float dem_banner_t = 99, dem_band_t = 99, dem_sw_t = 99, dem_start_t = 99, dem_prog = -1; static int dem_band_win, dem_site_of[NBOT_MAX];
static const char *DEM_REASON[6] = { "", "Mission Successful: Team Eliminated", "Mission Successful: Team Eliminated", "Mission Successful: Detonation", "Mission Successful: Bomb Defused", "Mission Successful: Time Over" };
// where a bot goes: attackers to this round's target site (one of them to the other), defenders guard the sites or the planted bomb
static int dem_bot_defusing(int i) { (void)i; return dm.state == DS_PLANTED; }   // bots.h: a bot held on the site defuses once the bomb is down
static int dem_goal(int i, float *g) {
	if (!dem_on || !dmap.nsite || dm.state == DS_FREEZE) return 0;
	Bot *b = &bots[i]; int att = dem_attacking(&dm, b->team);
	if (dm.state == DS_PLANTED && dm.bomb_site >= 0) {
		if (!att) { memcpy(g, dm.bomb_pos, 12); return 1; }
		const DemSite *s = &dmap.site[dm.bomb_site]; g[0] = s->stand[0] + (frand() - 0.5f) * 900; g[1] = s->stand[1] + (frand() - 0.5f) * 900; g[2] = s->stand[2]; return 1;   // guard the bomb
	}
	int si = att ? (dm.round + (i % 3 == 2)) % dmap.nsite : i % dmap.nsite; dem_site_of[i] = si;
	const DemSite *s = &dmap.site[si];
	if (att) { memcpy(g, s->stand, 12); return 1; }
	g[0] = s->stand[0] + (frand() - 0.5f) * 1000; g[1] = s->stand[1] + (frand() - 0.5f) * 1000; g[2] = s->stand[2]; return 1;
}
// world point -> screen, for the site markers
static __attribute__((unused)) int dem_screen(const float *MVP, const float *p, float *sx, float *sy) {
	float q[4]; for (int r = 0; r < 4; r++) q[r] = MVP[r] * p[0] + MVP[4 + r] * p[1] + MVP[8 + r] * p[2] + MVP[12 + r];
	if (q[3] < 10) return 0; *sx = (q[0] / q[3] + 1) * 480; *sy = (1 - q[1] / q[3]) * 272; return 1;
}
static int dem_on_flag(void) { return dem_on; }
static void dem_died(int id) { dem_player_died(&dm, id); }
#define AW_LOG logp
#include "autowalk.h"
#include "ablur.h"   // b102: the smear as an ability starts
#include "wroom.h"       // b84: the v2.0 dive + "Waiting for players..." room before each match
#include "elim_game.h"   // Elimination (elim.h + its match glue; hooks: elim_bind, elim_match_setup, elim_frame, elim_camera, elim_hud*, elim_overlays_draw)
#include "dem_game.h"    // Demolition HUD parts, round screens, site collision (hooks: dem_add_site_col, dem_hud_draw, dem_overlays_draw)
#include "hud_match.h"   // HUD state: portrait, skill gauge flags, round-mode slots and bomb timer (hook: hud_match_state)

// ---- operator skills (skills.h, tools/skills/INTEGRATION.md): the world as the skill code sees it. Actor 0 = the player, 1 + i = bot i.
// main()'s locals the callbacks need are bound once (skw_bind); bots get their character's skill and their kills as intel (x100).
static struct { Player *pl; float *php, *pap, *pdead, *pitch, *pflash, *kill_marker, *hit_marker, *notify_t, *streak_t; int *fly, *intel, *kills, *m_pkills, *notify_pts, *streak, *autoplay; const char **notify; } SKB;
static signed char skw_ct_skill[NCHR_MAX]; static int skw_ct_n = -1;
static int skw_n(void) { return 1 + nbot; }
static V3 skw_pos(int a) { return a == 0 ? SKB.pl->pos : bots[a - 1].pl.pos; }
static V3 skw_vel(int a) { return a == 0 ? SKB.pl->vel : bots[a - 1].pl.vel; }
static float skw_yaw(int a) { return a == 0 ? SKB.pl->yaw : bots[a - 1].pl.yaw; }
static int skw_alive(int a) { if (a == 0) return !*SKB.fly && *SKB.pdead <= 0 && *SKB.php > 0; return a - 1 < nbot && bots[a - 1].state != BS_DEAD; }
static V3 skw_eye(int a) { if (a <= 0 || a > nbot || a > NBOT_MAX) return v3(SKB.pl->pos.x, SKB.pl->pos.y, SKB.pl->pos.z + (SKB.pl->eye > 0 ? SKB.pl->eye : P_EYE)); return bot_eye(&bots[a - 1]); }
static V3 skw_dir(int a) { float y = skw_yaw(a), p = a == 0 ? *SKB.pitch : bots[a - 1].aim_pitch; return v3(cosf(y) * cosf(p), sinf(y) * cosf(p), sinf(p)); }
static float skw_hp(int a) { return a == 0 ? *SKB.php : bots[a - 1].hp; }
static int skw_los(V3 a, V3 b) { return bot_los(a, b); }
static int net_client_on(void); static int net_host_on(void); static void net_host_skill_hit(int att, int v, float dmg, int killed);
static int skw_damage(int v, float dmg, int att, int kind) {
	if (net_client_on()) return 0;   // net: only the host's skills hurt (its snapshots and events show it here)
	if (v == 0) {   // the player: the same path as bot bullets (armour first, death, killer = the bot behind it)
		V3 ap = att > 0 ? skw_pos(att) : SKB.pl->pos; self_dmg += dmg; self_dmg_dir = atan2f(ap.y - SKB.pl->pos.y, ap.x - SKB.pl->pos.x); if (att >= 1) { bot_last_hitter = att - 1; bot_assist_hit(0, att); } if (net_host_on()) net_host_skill_hit(att, 0, dmg, 0); return 0; }
	int i = v - 1; if (i >= nbot || bots[i].state == BS_DEAD) return 0;
	int killed = bot_hurt(i, dmg, 0, att == 0 ? -1 : att >= 1 ? att - 1 : -2);
	if (net_host_on()) net_host_skill_hit(att, v, dmg, killed);   // net: a remote player's hit marker / damage direction
	if (killed && att == 0) {   // the player's skill kill: the same bookkeeping as a gun kill
		static const char *KN[5] = { "SENTRY KILL", "ROCKET KILL", "NICE KILL", "DRONE KILL", "NICE KILL" };
		(*SKB.kills)++; *SKB.kill_marker = 0.7f; ev_play("impact/ui_killshot", NULL); ev_play("impact/killshot_fp", NULL);
		score_l += 100; *SKB.streak = *SKB.streak_t > 0 ? *SKB.streak + 1 : 1; *SKB.streak_t = 4; *SKB.intel += 100;
		*SKB.notify = kind >= 0 && kind < 5 ? KN[kind] : "NICE KILL"; *SKB.notify_t = 0; *SKB.notify_pts = 100;
		feed_add(feed, &nfeed, cfg.name, 1, bot_name(i), -1); (*SKB.m_pkills)++; my_kills_on[i]++;
		logp("  killed bot %d (skill, kind %d)\n", i, kind); }
	return killed;
}
static void skw_heal(int a, float add, float cap) { if (net_client_on()) return; float *hp = a == 0 ? SKB.php : &bots[a - 1].hp, mx = 100 * cap;
	if (add > 0) { float nh = *hp + add, lim = fmaxf(*hp, mx); *hp = nh > lim ? lim : nh; } else if (*hp > mx) *hp = mx; }
static void skw_armor(int a, float add, float cap) { if (net_client_on()) return; float *ap = a == 0 ? SKB.pap : &bots[a - 1].ap, mx = 100 * cap;   // b83 (Nano-Gel Armor)
	if (add > 0) { float n = *ap + add, lim = fmaxf(*ap, mx); *ap = n > lim ? lim : n; } else if (*ap > mx) *ap = mx; }
static int skw_firing(int a) { return a == 0 ? *SKB.pflash > 0.04f : bots[a - 1].flash_t > 0.04f; }
static int skw_bot_info(int a, int *t, int *s, float *l, V3 *ls) {
	if (a < 1 || a - 1 >= nbot) return 0; const Bot *b = &bots[a - 1];
	*t = b->tgt == -1 ? 0 : b->tgt >= 0 ? 1 + b->tgt : -1; *s = b->sees && b->state == BS_ENGAGE; *l = b->state == BS_ENGAGE ? b->lost_t : 99; *ls = b->last_seen; return 1; }
static void skw_notify(int a, int what, const char *text, int pts) {
	if (a != 0) return; if (what == SKN_HIT) { *SKB.hit_marker = 0.2f; return; } if (what == SKN_TEXT) { *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = 0; return; }
	if (what == SKN_POINTS) { *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = pts; *SKB.intel += pts; return; }   // "SKILLSYNC COMPLETED" +20
	*SKB.kill_marker = 0.5f; *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = pts; *SKB.intel += pts; ev_play("impact/ui_killshot", NULL); }
static const char *skw_name(int a) { if (a == 0) return cfg.name; return a - 1 < nbot ? bot_name(a - 1) : NULL; }
static V3 skw_muzzle(int a) { return a > 0 ? bots[a - 1].muzzle : skw_eye(a); }   // b52: third-person shields clear the gun
static const SkWorld SKW = { .muzzle = skw_muzzle, .name = skw_name, .n_actors = skw_n, .pos = skw_pos, .vel = skw_vel, .yaw = skw_yaw, .alive = skw_alive, .eye = skw_eye, .aim_dir = skw_dir, .hp = skw_hp,
	.los = skw_los, .damage = skw_damage, .heal = skw_heal, .armor = skw_armor, .firing = skw_firing, .bot_info = skw_bot_info, .shot_fx = on_bot_shot, .notify = skw_notify };
#ifdef BOT_SK_HOOKS   // bots.h hooks (apply_skills_to_main.py with bots.h): camo / hack visibility, shields, EMP aim, revealed enemies, rush ...
static int skb_can_see(int i, int t) { return sk_can_see(1 + i, t < 0 ? 0 : 1 + t) != 0; }
static int skb_block(int i, V3 from, V3 to, V3 *end) { return sk_bullet_block(1 + i, from, to, end); }
static float skb_aim(int i) { return sk_aim_factor(1 + i); }
static int skb_goal(int i, float *g) { return sk_bot_goal(1 + i, g); }
static float skb_melee_mul(int i) { return sk_hs_melee_mul(1 + i); }   // b79
static void skb_move(int i, float *mx, float *my, int *sprint, int *jump) { Bot *b = &bots[i]; float y = b->pl.yaw; sk_bot_move(1 + i, mx, my, sprint, jump, &y); b->pl.yaw = y; b->pl.speed_mul = sk_speed_mul(1 + i, *sprint); b->pl.jump_mul = sk_jump_mul(1 + i); }
static float skb_alpha(int i) { return sk_draw_alpha(1 + i, lis_pos); }
static float skb_active_t(int i) { int a = 1 + i; return sk_valid(a) && SKA[a].state == SKS_ACTIVE ? SKA[a].t : -1; }   // bots.h (chars): the skill activation gesture
static int skw_hidden_from_bots(void) { return 0; }   // per bot, through bot_sk_can_see
#else
static int skw_hidden_from_bots(void) { return sk_camo_on(0); }   // no bot hooks: bots simply ignore a cloaked player (the old behaviour)
#endif
static void skw_sync(void) {   // each actor's operator / team, tiers (player: intel; bots: kills x 100)
	if (skw_ct_n != nchrt) { for (int c = 0; c < NCHR_MAX; c++) skw_ct_skill[c] = (signed char)(c < nchrt ? sk_skill_of_stem(chrt[c].name) : -1); skw_ct_n = nchrt; }
	sk_actor_set(0, ga_actor_skill(0, cfg.chr >= 0 && cfg.chr < SK_COUNT ? cfg.chr : SK_OPTICAL), 0); sk_set_tier(0, *SKB.intel);   // (b63 Ghost Assault: Hyper Sprint for the cyborgs, the camo for the ghosts)
	for (int i = 0; i < nbot && i + 1 < SK_MAXACT; i++) { int s = skw_ct_skill[bots[i].ct]; sk_actor_set(1 + i, ga_actor_skill(1 + i, s >= 0 ? s : SK_OPTICAL), bots[i].team); sk_set_tier(1 + i, bots[i].kills * 100); }
}
// b76: the shooting range's targets for the skills (skills.h sk_rg_*: the arm launcher's locks, missile hits and blasts)
static int skrg_n(void) { return range_active() ? RG.n : 0; }
static int skrg_up(int i) { return range_active() && i >= 0 && i < RG.n && (RG.t[i].state == RG_UP || RG.t[i].state == RG_SPAWN); }
static V3 skrg_chest(int i) { return i >= 0 && i < RG.n ? RG.t[i].chest : v3(0, 0, 0); }
static int skrg_hit(V3 o, V3 d, float maxd, float *t) { int h; return range_hit_test(o, d, maxd, t, &h); }
static void skrg_hurt(int i, float dmg, V3 hp, V3 d) { static int nlog; if (nlog < 4) { nlog++; logp("range: a skill missile hit target %d (%.0f damage, b76)\n", i, dmg); }
	if (range_damage(i, dmg, 0, hp, d)) { if (SKB.kill_marker) *SKB.kill_marker = 0.7f; } else if (SKB.hit_marker) *SKB.hit_marker = 0.2f; }
static void skrg_blast(V3 c, float r, float dmg) { int n = range_blast(c, r, dmg); static int nlog; if (n && nlog < 4) { nlog++; logp("range: a skill blast destroyed %d target%s (b76)\n", n, n > 1 ? "s" : ""); } if (n && SKB.kill_marker) *SKB.kill_marker = 0.7f; }
static void skw_frame(float dt) { if (!SKB.pl) return; skw_sync(); skills_update(dt); }
static void skw_match_start(void) { if (!SKB.pl) return; skw_sync(); skills_reset_match(); }
static void skw_bind(Player *pl, float *php, float *pap, float *pdead, float *pitch, float *pflash, float *kill_marker, float *hit_marker, float *notify_t, float *streak_t,
	int *fly, int *intel, int *kills, int *m_pkills, int *notify_pts, int *streak, int *autoplay, const char **notify) {
	SKB.pl = pl; SKB.php = php; SKB.pap = pap; SKB.pdead = pdead; SKB.pitch = pitch; SKB.pflash = pflash; SKB.kill_marker = kill_marker; SKB.hit_marker = hit_marker;
	SKB.notify_t = notify_t; SKB.streak_t = streak_t; SKB.fly = fly; SKB.intel = intel; SKB.kills = kills; SKB.m_pkills = m_pkills; SKB.notify_pts = notify_pts;
	SKB.streak = streak; SKB.autoplay = autoplay; SKB.notify = notify;
	sk_rg_n = skrg_n; sk_rg_up = skrg_up; sk_rg_chest = skrg_chest; sk_rg_hit = skrg_hit; sk_rg_hurt = skrg_hurt; sk_rg_blast = skrg_blast;   // b76
#ifdef BOT_SK_HOOKS
	sk_nav_path = nav_path;   // b117: the Seeker Drones run along the bots' navigation mesh
	bot_sk_can_see = skb_can_see; bot_sk_block = skb_block; bot_sk_aim = skb_aim; bot_sk_goal = skb_goal; bot_sk_move = skb_move; bot_sk_alpha = skb_alpha; bot_sk_melee_mul = skb_melee_mul;
	bot_sk_active_t = skb_active_t;
#endif
}

// ---- Think Tanks + Terminal Conquest glue (thinktank.h, ttm.h): names, kill feed, notices, per-frame call; the logic lives in the headers
static const char *tkg_name(int a) { return a == 0 ? cfg.name : a - 1 < nbot ? bot_name(a - 1) : ""; }
static void tkg_feed(const char *killer, int kside, const char *victim, int vside, int icon) {
	if (vside == -2 || net_client_on()) return;   // (net: a client gets the host's lines)
	int w = feed_wicon; if (icon > 0) feed_wicon = icon; feed_add(feed, &nfeed, killer && *killer ? killer : "THINK TANK", kside, victim, vside); feed_wicon = w; }
static void tkg_points(int a, const char *text, int pts) { if (a != 0 || !SKB.pl) return; *SKB.notify = text; *SKB.notify_t = 0; *SKB.notify_pts = pts; *SKB.intel += pts; *SKB.kill_marker = 0.4f; }
static int tkg_vis(const Model *M, const float *VP, const float *Mw, const MBone *P, const float *col, int look) {   // vision.h's looks for the tanks
	if (!vis_ok) return 0; const float cam[3] = { lis_pos.x, lis_pos.y, lis_pos.z }; vis_model(M, NULL, VP, Mw, P, cam, col, look ? VIS_XRAY_LINE : VIS_HOLO); return 1; }
static void wpn_holo_draw(const Model *M, const float *MVP, const float *Mw, const MBone *P, float sp) {   // b79: wpn.h's pick-up hologram = the spawn hologram's hexagon overlay
	if (!vis_ok) return; float pal[48 * 12]; VisObj o; memset(&o, 0, sizeof(o)); o.M = M; o.pal = pal; o.Mw = Mw; model_palette(M, P, pal);
	if (!md_gpu_skin) o.skv = model_skin_get(M, P, md_mesh0, md_mesh1, 0);
	const float cam[3] = { lis_pos.x, lis_pos.y, lis_pos.z }; vis_spawn_overlay(&o, MVP, cam, sp, 1 / 12.0f, 0.5f); glDisable(GL_CULL_FACE); }
static void tkg_bind(Player *p) { tk_bind(p, &SKW); tk_name_fn = tkg_name; tk_hook_bots(); ttm_feed = tkg_feed; ttm_points = tkg_points; tk_vdir = VDIR; tk_vis_model = tkg_vis; ttm_logf = logp; ttm_hold_dt = scoreboard_hold_dt; ttm_board_up = scoreboard_shown; }   // perf (b38): the scoreboard hooks
static void net_host_ttm(unsigned ev, float dt);
static void tkg_frame(float dt, Player *p, float pitch, int use_held, int call_pr, int deploy_pr, int alive, int *end_result, float *match_over_t, float *pdead_t) {   // after bots_update
	tk_update(dt); wo_update(dt, &lis_pos.x); ttm_voice_stem = pvoice; ttm_respawn_hold(pdead_t, dt);
	V3 e = v3(p->pos.x, p->pos.y, p->pos.z + (p->eye > 1 ? p->eye : P_EYE)), d = v3(cosf(p->yaw) * cosf(pitch), sinf(p->yaw) * cosf(pitch), sinf(pitch));
	if (!ttm_on) { tk_use_prog = alive && (tk_count(0) || tk_count(1)) ? tk_use(0, e, d, use_held, dt) : -1; return; }   // tanks outside the mode (tutorial)
	unsigned ev = ttm_frame(dt, e, d, use_held, call_pr, deploy_pr, alive); net_host_ttm(ev, dt);   // net: the events to the clients, their hacks
	if (ev & TEV_ONLINE) logp("  terminal conquest: terminal %c online\n", ttm_map.t[tm.active].letter);
	if (ev & TEV_CAPTURE) logp("  terminal conquest: team %d captured (%d - %d)%s\n", tm.last_team, tm.score[0], tm.score[1], tm.state == TS_END ? ", match over" : "");
	if ((ev & TEV_END) && *match_over_t <= 0) { *end_result = tm.winner == 0 ? 0 : tm.winner == 1 ? 1 : 2; *match_over_t = 8;
		music_ev(*end_result == 2 ? "bgm/even" : *end_result == 0 ? "bgm/win" : "bgm/lose", 200); logp("  terminal conquest: match over %d - %d\n", tm.score[0], tm.score[1]); }
}
#include "net_game.h"   // net (stage 2): the match over the network (snapshots, inputs, puppets, lag-compensated hits)
#include "net_auto.h"   // net (test): netauto.txt scripts a room member (host / join) for the multi-instance tests
#include "tutorial.h"   // the training programs, the intro movies (tutorial.h, tut_ui.h, tut_video.h)
// b63: the operative screen's DETAILS videos (menu_fe.h fe_details_*): the tutorial's player (tut_video.h), looping; an operator
// profile (anime intro) plays its sound with the menu music turned down until the window closes
static int mfe_vid_open_(const char *f, int audio) {
	char p[160]; snprintf(p, sizeof(p), VDIR "%s", f); TV.vol = fmaxf(0.05f, fminf(1.0f, cfg.vol_master * fmaxf(cfg.vol_voice, 0.3f)));
	cfg_apply(); if (audio) music_gain *= 0.12f;
	return tv_open(p, 1, audio);
}
static GLuint mfe_vid_frame_(void) { return tv_frame(); }
static void mfe_vid_close_(void) { tv_close_soon(); cfg_apply(); }

static unsigned long long oc_clock_us(void) { return sceKernelGetProcessTimeWide(); }
int main() {
	ldp_start("boot to the title screen");   // perf (b45): load phases (ldprof.h; reported at the first menu frame)
	scePowerSetArmClockFrequency(444); scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222); scePowerSetGpuXbarClockFrequency(166);
	jobs_init();   // b35: workers on cores 1, 3 (CapUnlocker) and 2 (below the audio mixer)
	// b60: nothing on the card is created or truncated at start-up in normal play (alog.h: the first allocation of a run
	// stalled 21 s on the device): the folder is made only if missing, the log is written over the last one in place;
	// test runs (the autoplay switch file) also time the card on the writer thread (alog_card_probe, alog_probe_end)
	{ SceIoStat ds; if (sceIoGetstat("ux0:data/gitsfa", &ds) < 0) sceIoMkdir("ux0:data/gitsfa", 0777);
	  alog_probe = sceIoGetstat(VDIR "autoplay", &ds) >= 0; g_log = alog_open(LOG_PATH, 1) == 0; }   // render (b35): async log (alog.h)
	logp("GITS:FA Vita build 123\n");
	logp("jobs: %d worker threads, each on any of cores %s (b61)\n", jobs_nworkers(), jobs_core3 ? "1-3" : "1-2");
	{ SceIoStat fs; wvm_fp_off = sceIoGetstat(VDIR "nofp", &fs) >= 0; }   // b60: test switch: the full first-person meshes always (weapon.h)
	{ SceIoStat fs; w_texcap = sceIoGetstat(VDIR "tex256", &fs) >= 0 ? 256 : 0;   // b61: test switch tex256 (the maps' textures as before: 256 px)
	  oc_on = sceIoGetstat(VDIR "noocc", &fs) < 0; oc_par = jobs_parallel_for; oc_clock = sceIoGetstat(VDIR "occfree", &fs) < 0 ? oc_clock_us : NULL; }   // b60: occlusion culling (occl.h): on all cores, timed; test switches noocc (off), occfree (no time budget: the emulator)
	{ FILE *nf = fopen(VDIR "noreadahead", "rb"); ldra_off = nf != NULL; if (nf) fclose(nf);   // perf (b45): A/B switch (ldbg.h)
	  logp("read-ahead: %s\n", ldra_off ? "off (noreadahead switch: maps, models and HUD packs read through stdio)" : "on (one reader thread on core 2: maps, models, HUD packs)"); }
	{
		SceKernelFreeMemorySizeInfo mi = { sizeof(mi) }; sceKernelGetFreeMemorySize(&mi);
		logp("free memory before init: main %d KB, cdram %d KB, phycont %d KB\n", mi.size_user / 1024, mi.size_cdram / 1024, mi.size_phycont / 1024);
	}
	// note: vglInit* returns GL_TRUE only when it had to fall back to a lower resolution; GL_FALSE is normal success
	// (perf b56's timed card probe ran here, on this thread: 21.5 s on the device. b60: alog.h does it on the writer thread.)
	int msaa_off = 0; { FILE *mf = fopen(VDIR "msaaoff", "rb"); if (mf) { msaa_off = 1; fclose(mf); } }   // render b45: test switch, MSAA off (what 2x MSAA costs the GPU)
	int msaa_4 = 0; { SceIoStat ms; msaa_4 = !msaa_off && sceIoGetstat(VDIR "msaa4", &ms) >= 0; }   // b60: test switch, 4x MSAA (smoother edges: is there GPU time for it once less is drawn?)
	LDP_PUSH(LDP_GLINIT); GLboolean lowres = vglInitWithCustomThreshold(0, 960, 544, TV_RAM_FREE, 0, TV_PHYCONT_FREE, 0x8C6000, msaa_off ? SCE_GXM_MULTISAMPLE_NONE : msaa_4 ? SCE_GXM_MULTISAMPLE_4X : SCE_GXM_MULTISAMPLE_2X);   // = vglInitExtended (0x8C6000: its cdialog threshold) minus what sceAvPlayer needs: all of phycont, TV_RAM_FREE of main RAM (tut_video.h)
	LDP_POP(); logp("vitaGL started (%s)%s\n", msaa_off ? "MSAA off: msaaoff test switch" : msaa_4 ? "4x MSAA: msaa4 test switch" : "2x MSAA", lowres ? ", resolution lowered" : ", 960x544");   // (render b45: msaaoff; perf b45: LDP phase)
	{
		SceKernelFreeMemorySizeInfo mi = { sizeof(mi) }; sceKernelGetFreeMemorySize(&mi);
		logp("free memory after init: main %d KB, cdram %d KB, phycont %d KB\n", mi.size_user / 1024, mi.size_cdram / 1024, mi.size_phycont / 1024);
	}
	logp("graphics started\n");
	if (shb_generate(logp)) { alog_close(); sceKernelExitProcess(0); }   // render b45: tools/shbank/gen.sh (test-only: compile the shader bank's sources, then stop)
	LDP_PUSH(LDP_SHADER); mkprog(0, FS_OPAQUE_PLAIN); mkprog(1, FS_ALPHA_PLAIN);   // prog[0] is still used by the fallback (non-viewmodel) weapon path
	grade_select(mkshader, logp, -1); shader_fail = 0;   // colour grading: the best way the shader compiler accepts (grade.h)
	{ int ws = world_shaders(mkshader, logp); shader_fail = 0; if (ws < 0) { logp("stopping: world shaders failed\n"); alog_close(); sceKernelExitProcess(0); } }
	world_progress = loading_progress;
	mkflat();
	if (shader_fail) { logp("stopping: shader error\n"); alog_close(); sceKernelExitProcess(0); }
	LDP_POP(); logp("shaders linked\n");
	// render (b34): the b29 start-up shader probe (9 grading-tail test shaders, two failing by design) is removed: nothing read it
	for (int i = 0; i < 2; i++) { glClearColor(0.004f, 0.03f, 0.055f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); vglSwapBuffers(GL_FALSE); }  // ends the vitaGL splash
	FILE *ap = fopen(VDIR "autoplay", "rb"); int autoplay = ap != NULL; if (ap) fclose(ap);
	// b76 (user: Select in the shooting range flew a free camera round the map): the free-fly camera is a developer tool -
	// only with the switch file ux0:data/gitsfa/devfly; in play Select (held) shows the scoreboard, or calls the Tachikoma in
	ap = fopen(VDIR "fastexit", "rb"); mrs_fast_exit = ap != NULL; if (ap) fclose(ap);
	ap = fopen(VDIR "restest", "rb"); const int restest = ap != NULL; if (ap) fclose(ap); mrs_test_hold = restest;   // b107 (tests, with autoplay): the end-of-match screens 6 s into the first map (the line-up, TOP OPERATIVE)   // b77 (tests): the results' exit countdown 3 s into the tabs
	ap = fopen(VDIR "dropdemo", "rb"); int drop_demo = ap != NULL; float drop_demo_t = 0; if (ap) fclose(ap);   // b79 (tests): in the shooting range a gun is thrown every 3 s, 3.5 m ahead
	ap = fopen(VDIR "devfly", "rb"); int dev_fly = ap != NULL; if (ap) fclose(ap); if (dev_fly) logp("devfly: Select toggles the free-fly camera (b76)\n");
	// b70 test switch exectest (with autoplay, Ghost Assault on Ghost Storage = TOURSTART 8): every 5 s an execution is set up in
	// front of the player - the player's from the front / from behind (cyborg rounds), a cyborg bot's on a ghost bot seen from
	// the side, a cyborg bot's on the player (ghost rounds) - and played at quarter speed, so the 1 s screenshots catch each stage
	int hv_test = 0; { FILE *hf_ = fopen(VDIR "handstest", "rb"); hv_test = autoplay && hf_; if (hf_) { fclose(hf_); logp("handstest: on (b99: the device in the hands every 14 s)\n"); } }
	int tourknife = 0; { FILE *xf = fopen(VDIR "tourknife", "rb"); if (xf) { fclose(xf); tourknife = 1; logp("tourknife: the tour keeps the knife on every map (b118 test switch)\n"); } }
	int exectest = 0; float xt_t = 0; int xt_n = 0; { FILE *xf = fopen(VDIR "exectest", "rb"); exectest = autoplay && xf; if (xf) { fclose(xf); logp("exectest: on\n"); } }
	// b71 test switch skinview (with autoplay, not Ghost Assault): every 2.5 s three bots (in turn) stand 2.6-3.4 m in front of
	// the player facing it: the enemy masks, the glow maps and the match's skins up close in the screenshots
	int wroom_test = 0; { FILE *wf = fopen(VDIR "waitroom", "rb"); wroom_test = autoplay && wf; if (wf) { fclose(wf); logp("waitroom: on (b84: the waiting room before every map of the test run)\n"); } }
	int tuttank_test = 0; { FILE *tf_ = fopen(VDIR "tuttank", "rb"); tuttank_test = autoplay && tf_; if (tf_) { fclose(tf_); logp("tuttank: on (b86: the test run goes straight to the Think Tank training program)\n"); } }
	int knifeview = 0; float kv_t = 0; { FILE *tf_ = fopen(VDIR "knifeview", "rb"); knifeview = autoplay && tf_; if (tf_) { fclose(tf_); logp("knifeview: on (b87: a quick knife every 2.5 s, for screenshots of the knife)\n"); } }
	int smf_test = 0; { FILE *tf_ = fopen(VDIR "smftest", "rb"); smf_test = autoplay && tf_; if (tf_) { fclose(tf_); logp("smftest: on (b87: the map's special maneuvers one by one, player.h)\n"); } }
	{ FILE *tf_ = fopen(VDIR "ogslow", "rb"); if (tf_) { fclose(tf_); og_slow = 0.1f; logp("ogslow: on (b105 test switch)\n"); } }
	{ FILE *tf_ = fopen(VDIR "hvhold.txt", "rb"); if (tf_) { if (fscanf(tf_, "%f", &hv_hold) != 1) hv_hold = -1; fclose(tf_); logp("hvhold: the skill gesture held at %.2f s (b105 test switch)\n", hv_hold); } }
	{ FILE *tf_ = fopen(VDIR "nodecalbias", "rb"); w_no_bias = tf_ != NULL; if (tf_) { fclose(tf_); logp("nodecalbias: on (b105 test switch: the ground decals without their depth bias)\n"); } }
	{ FILE *tf_ = fopen(VDIR "ledgesweep", "rb"); ledgesweep = tf_ != NULL; if (tf_) { fclose(tf_); logp("ledgesweep: on (b122: every map's obstacles a jump does nothing at)\n"); } }
	{ FILE *tf_ = fopen(VDIR "seekview", "rb"); seekview = autoplay && tf_; if (tf_) { fclose(tf_); logp("seekview: on (b117: the camera follows the player's Seeker Drone)\n"); } }
	{ FILE *tf_ = fopen(VDIR "wobjview", "rb"); wo_view = autoplay && tf_; if (tf_) { fclose(tf_); logp("wobjview: on (b88: the camera follows the map's moving object, wobj.h)\n"); } }
	{ FILE *tf_ = fopen(VDIR "trainview", "rb"); wo_trainview = autoplay && tf_; if (tf_) { fclose(tf_); logp("trainview: on (b87: on Train Station the player stands on the platform edge watching the train, wobj.h)\n"); } }
	int tkslow_test = 0; { FILE *tf_ = fopen(VDIR "tkslow", "rb"); tkslow_test = autoplay && tf_; if (tf_) { fclose(tf_); logp("tkslow: on (b86: x0.15 slow motion after a hack is planted / a tank blows up, for screenshots)\n"); } }
	{ FILE *nf = fopen(VDIR "netk.txt", "rb"); if (nf) { float k_[3]; if (fscanf(nf, "%f %f %f", &k_[0], &k_[1], &k_[2]) == 3) { w_netk[0] = k_[0]; w_netk[1] = k_[1]; w_netk[2] = k_[2]; logp("netk.txt: network view lines %.2f %.2f %.2f\n", k_[0], k_[1], k_[2]); } fclose(nf); } }   // b84 (test runs: tune the network view)
	int skinview = 0; float sv_t = 0; int sv_n = 0; { FILE *sf = fopen(VDIR "skinview", "rb"); skinview = autoplay && sf; if (sf) { fclose(sf); logp("skinview: on (b71)\n"); } }
	// b80 test switch decapdemo (with skinview): 0.8 s after each placement the middle bot dies of a headshot (bparts.h: the head
	// flies off in front of the camera)
	int decapdemo = 0, dd_bot = -1; float dd_t = -1; { FILE *df = fopen(VDIR "decapdemo", "rb"); decapdemo = skinview && df; if (df) { fclose(df); logp("decapdemo: on (b80)\n"); } }
	{ // perf (b45): display-locked match steps (dstep.h): on, unless ux0:data/gitsfa/clockstep exists (A/B); the host build keeps the
	  // clock step (its tours and hashes) unless its out folder holds "dstep"
#ifdef HG_HOST
	  FILE *df = fopen(VDIR "dstep", "rb"); DS.on = df != NULL; if (df) fclose(df);
#else
	  FILE *df = fopen(VDIR "clockstep", "rb"); DS.on = df == NULL; if (df) fclose(df);
#endif
	  logp("game step: %s\n", DS.on ? "display-locked in matches (whole VBlanks, late flips paid back over the next frames; clockstep switches it off)" : "CPU clock"); }
	{ FILE *vf = fopen(VDIR "verbose", "rb"); alog_verbose = !alog_release && (autoplay || vf); if (vf) fclose(vf); }   // render (b35): the per-second log block only in test runs
	// test tour order: the maps that showed speckled textures first (Urban first, so it loads into fresh memory)
	static const int TOUR[] = { 11, 5, 4, 8, 6, 0, 1, 2, 3, 7, 9, 10, 12, 13, 14, 15, 16 };
	int cur_map = autoplay ? TOUR[0] : 0;   // the last map picked in the menu, except in the test tour
	int tour_start = 0;   // test runs: ux0:data/gitsfa/tourstart.txt = the tour step to start at (screenshots of one map)
	int tour_chr = -1;    // b76 test runs: ux0:data/gitsfa/tourchr.txt = the operator for every map (0 Batou: the arm launcher in the shooting range)
	if (autoplay) { FILE *tf = fopen(VDIR "tourchr.txt", "rb"); if (tf) { int v = -1; if (fscanf(tf, "%d", &v) == 1 && v >= 0 && v < 12) tour_chr = v; fclose(tf); } }
	if (autoplay) { FILE *tf = fopen(VDIR "tourstart.txt", "rb"); if (tf) { int v = 0; if (fscanf(tf, "%d", &v) == 1 && v > 0 && v < (int)(sizeof(TOUR) / sizeof(TOUR[0]))) tour_start = v; fclose(tf); } if (tour_start) cur_map = TOUR[tour_start]; }
	if (!autoplay) { FILE *lf = fopen(VDIR "lastmap.txt", "rb"); if (lf) { int v = -1; if (fscanf(lf, "%d", &v) == 1 && v >= 0 && v < NMAPS) cur_map = v; fclose(lf); } }
	glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glEnable(GL_CULL_FACE); glCullFace(GL_BACK); glFrontFace(GL_CCW);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	float fog[4] = { 0.55f, 0.6f, 0.66f, 1 };
	SceUInt64 load0 = sceKernelGetProcessTimeWide();
	LDP_PUSH(LDP_SCREEN); loading_screen(autoplay ? MAPS[cur_map].name : NULL); boot_scr_on = !autoplay; boot_step(0.02f); LDP_POP();   // perf (b45): phase (b87: no map name when the player starts the game)
	logp("loading map %s\n", MAPS[cur_map].stem);
	if (load_level_ex(cur_map, 1) < 0 && (cur_map == 0 || load_level_ex(cur_map = 0, 1) < 0)) { logp("stopping: map data missing\n"); alog_close(); sceKernelExitProcess(0); }   // perf (b45): lite (load_level_ex)
	{ int ne = ev_load(VDIR); logp("audio events: %d (events.gev)%s\n", ne, ne ? "" : " MISSING: every event is silent"); }
	{ FILE *rf = fopen(VDIR "noreverb", "rb"); if (rf) { fclose(rf); rv_disabled = 1; }   // b107: the rooms' reverb (reverb.h; test switch noreverb = as b106)
	  if (rv_disabled) logp("reverb: off (test switch noreverb)\n"); else if (rev_ndef) logp("reverb: on, %d room definitions (events.gev)\n", rev_ndef); else logp("reverb: none in events.gev (an older data set): dry, as before\n"); }   // databuilder: the FMOD event table is data (ev_table.h), loaded before anything plays
	ldbg_start(boot_audio_job, NULL);   // perf (b45): the sounds load beside the rest of the start-up (waited for before the loadout)
	float load_s = (sceKernelGetProcessTimeWide() - load0) / 1e6f;
	map_fog(fog, fogp); /* databuilder */ memcpy(md_fogp, fogp, 12); map_audio_pending = 1; glClearColor(fog[0], fog[1], fog[2], 1);
	logp("collision: %d tris, grid %dx%d, %d spawn points; navigation mesh: %d triangles; loaded in %.2f s\n", nctri, gw, gh, nspawn, nnav, load_s);
	log_mem();
	LDP_PUSH(LDP_VM); int lw_ = load_weapon(VDIR "c30.gmdl"); LDP_POP(); if (lw_ < 0) logp("weapon model missing\n"); else logp("weapon: %d meshes, muzzle at %.1f %.1f %.1f\n", nwm, muzzle[0], muzzle[1], muzzle[2]);
	// first-person arms + weapon with the game's animations
	{
		boot_step(0.38f); LDP_PUSH(LDP_SHADER); int sr = model_shader(mkshader, logp); md_ok = sr >= 0; shader_fail = 0;
		if (md_ok) skinprobe_run(logp);
		{ FILE *sf = fopen(VDIR "sheen.txt", "rb"); if (sf) { float v[2]; if (fscanf(sf, "%f %f", &v[0], &v[1]) == 2) { md_sheen_k[0] = v[0]; md_sheen_k[1] = v[1]; logp("sheen.txt: rim x %.2f, Fresnel x %.2f (test switch, model.h md_mesh_sheen)\n", v[0], v[1]); } fclose(sf); } }
		if (md_ok) md_env_load(VDIR "env_ref.gtx", logp);   // b65: the models' reflection (model.h MD_VS_ENV; vm/make_env.py from the game's CUBE.dds)
		{ FILE *ef = fopen(VDIR "noenvref", "rb"); if (ef) { fclose(ef); md_env_on = 0; logp("model reflection: off (test switch noenvref)\n"); } }   // (A/B test switch)
		{ int spr = sprite_init(mkshader); shader_fail = 0; logp("sprites: %s (%d)\n", spr < 0 ? "FAILED" : "ok", spr); }
		{ int fr = fxs_init(mkshader); shader_fail = 0; logp("effect scenes: %s (%d) (b102)\n", fr < 0 ? "FAILED: the old dive" : "ok", fr); }
		{ int ar = ab_init(mkshader); shader_fail = 0; logp("ability blur: %s (%d) (b102)\n", ar < 0 ? "not available" : ar ? "off: abluroff test switch" : AB.force ? "ready; ablurtest: every 4 s" : "ready", ar); }
		LDP_POP(); boot_step(0.44f); LDP_PUSH(LDP_FX);
		{ FILE *nt = fopen(VDIR "notrim", "rb"); if (nt) { fx_trim_on = 0; fclose(nt); } }   // b81 test switch notrim: particles as whole quads (A/B of fx.h's trimming)
		{ FILE *nr = fopen(VDIR "noragdoll", "rb"); if (nr) { pbody_ragdoll = 0; fclose(nr); } }   // b82 test switch noragdoll: the player's body in its death clip alone (b81)
		fx_logf = logp;   // b81: fx.h reports its particle trims once its worker thread has built them
		{ int fr = fx_init(VDIR "fx_sk.gfx"); if (fr < 0) fr = fx_init(VDIR "fx.gfx"); /* fx_sk.gfx = fx.gfx + the skill effects (build_skills_fx.py) */ logp("effects: %s (%d)\n", fr < 0 ? "FAILED" : "ok", fr);
		  if (fr >= 0) { char tb[160]; fx_trim_report(tb, sizeof(tb)); logp("  %s\n", tb); } }
		LDP_POP(); boot_step(0.50f); LDP_PUSH(LDP_HUD);
		{ int hr = hud_load(VDIR "hud.ghud", mkshader); shader_fail = 0; int gr = hr >= 0 ? hud_game_init() : -1;
		  logp("hud: %s (%d, %d images, %d nodes, %d fields; widgets %s)\n", hr < 0 ? "FAILED" : "ok", hr, hr >= 0 ? nhimg : 0, hr >= 0 ? nhnode : 0, hr >= 0 ? nhfield : 0, gr < 0 ? "MISSING" : "ok"); }
		LDP_POP(); boot_step(0.55f); LDP_PUSH(LDP_MENU);
		{ int mr = menu_init(VDIR, mkshader); shader_fail = 0; logp("menus: in-match overlays %s, front-end %s\n", mr & 1 ? "ok" : "MISSING", mr & 2 ? "ok" : "MISSING"); }
		LDP_POP(); boot_step(0.62f); LDP_PUSH(LDP_SKILLS);
		{ int skr = skills_init(VDIR, &SKW, md_ok); logp("skills: %s (%d parts: sk_*.gvm + sk_ovl.gtx)\n", skr < 0 ? "FAILED" : "ok", skr); }
		if (md_ok) hv_load(VDIR);   // b99: the hands alone (hands.h)
		LDP_POP(); LDP_PUSH(LDP_SHADER);
		if (md_ok) { vis_init(mkshader, logp); shader_fail = 0; }   // vision.h (after the model shader, sprites and skills)
		if (md_ok) { wvm_lens_init(mkshader, logp); shader_fail = 0; }   // wpn_vm.h: scope / holo lens glass (weapons agent)
		LDP_POP();
		if (sr < 0) logp("skinning shader failed (%d): static weapon, no characters\n", sr);
		{ wvm_log = logp; wpn_holo_hook = wpn_holo_draw; LDP_PUSH(LDP_VM); int nw = weapons_load_table(VDIR "weapons.txt"); LDP_POP(); logp("weapons: %d in the table\n", nw); { int ns = wsnd_load(VDIR, CL_NAME, CL_N); logp("weapon sounds: %s\n", ns ? "the game's schedule (wpn_snd.txt)" : "wpn_snd.txt missing (an older data set): reload sounds spread evenly, as before"); } bot_falloff_hook = bot_falloff_of; }   // perf (b45): phase
		for (int k = 0; k < WS_N; k++) wsl[k].def = -1;
		boot_step(0.68f); LDP_PUSH(LDP_BGWAIT); ldbg_wait(); LDP_POP(); boot_step(0.72f);   // perf (b45): the sounds (boot_audio_job), put aside until the audio start below
		{ char nb[96]; snprintf(nb, sizeof(nb), "loader thread: sounds %.0f ms (%s);", boot_audio_us / 1e3, LDBG.threaded ? "beside the start-up" : "inline: no thread"); ldp_note(nb); }
#ifdef HG_HOST
		{ unsigned h = 2166136261u; for (int i = 0; i < nsnd; i++) { const Sound *q = &snds[i]; h = lvh_fnv(h, q->name, 48); h = lvh_fnv(h, &q->ch, 8); h = lvh_fnv(h, &q->rate, 12);   // perf (b45, host test)
			if (q->fmt == 0 && q->pcm) h = lvh_fnv(h, q->pcm, (size_t)q->frames * q->ch * 2); if (q->fmt == 1 && q->adp) h = lvh_fnv(h, q->adp, (size_t)q->nblk * q->ch * ABB); }
		  if (nvline) h = lvh_fnv(h, vlines, sizeof(VEntry) * nvline); logp("SOUNDHASH: %08x (%d sounds, %d voice lines)\n", h, nsnd, nvline); }
#endif
		boot_audio_aside.snds = snds; boot_audio_aside.nsnd = nsnd; boot_audio_aside.vlines = vlines; boot_audio_aside.nvline = nvline; boot_audio_aside.nvpack = nvpack;
		snds = NULL; nsnd = 0; vlines = NULL; nvline = 0; nvpack = 0;
		if (md_ok) loadout_load("m4a1", "seburom5", "knife");
		if (!vm_ok) logp("viewmodel: not used, using the static weapon\n");
	}
	boot_step(0.75f); LDP_PUSH(LDP_AUDIO);
	snds = boot_audio_aside.snds; nsnd = boot_audio_aside.nsnd; vlines = boot_audio_aside.vlines; nvline = boot_audio_aside.nvline; nvpack = boot_audio_aside.nvpack;   // perf (b45): the loader thread's tables, where audio_load_all ran before
	if (boot_audio_r < 0) logp("sounds missing\n"); else { add_tone(); add_melody(); logp("sounds: %d in memory (%.1f MB), %d voice lines streamed from %d packs\n", nsnd, sounds_bytes() / 1048576.f, nvline, nvpack); }   // (perf b45: loaded by boot_audio_job)
	{ int ar = audio_init(); logp("audio: %s (0x%08X), port %d, mixer on core %d, %d frames per block, uncached buffers %s\n", ar < 0 ? "FAILED" : "ok", ar, audio_port, audio_core, GRAIN, ubuf[0] ? "ok" : "FAILED"); }
	LDP_POP();
	// weapon state (Seburo C-30; first-pass numbers until the weapons table is decoded)
	int shots = 0, reloads = 0, firing_auto = 0; float fire_cd = 0, reload_t = 0, ads = 0, kick = 0, recoil = 0, bob = 0, stepd = 0, spread = 0;
	float RELOAD_TIME = 2.2f, reload_len = 2.2f; int was_ground = 1; float fall_speed = 0, air_time = 0, sway_x = 0, sway_y = 0;
	int hits_by_mat[9] = {0}, misses = 0;
	float hs_lunge = 0; int hs_from = -1, hs_was = 0, hs_back = -1;   // b79 HYPER SPRINT: the knife lunge left, the gun to go back to, the skill last frame
	float php = 100, hurt_flash = 0, dmg_ind = 0, dmg_dir = 0, hit_marker = 0, kill_marker = 0, pflash = 0, regen_cd = 0, pdead_t = 0, dmg_snd_cd = 0, dmg_taken = 0, pd_last_dmg = 0, spawn_glow_t = 9;   // b72: the killing hit's damage (pbody.h death set), s since the last respawn (edge glow)
	int kills = 0, pdeaths = 0, phits = 0, pheads = 0, bots_ok = 0, bots_draw_us = 0;
	float match_over_t = 0, pap = 100, match_t = 600, cool_t = -1, camo_t = 0, streak_t = 0, notify_t = 9, dmg_a[HUD_DMG] = { 0 }, dmg_tt[HUD_DMG] = { 0 }; int intel = 0, streak = 0, notify_pts = 0, fp_shot = 0; const char *notify = NULL; (void)cool_t; (void)camo_t;
	float bot_ping[NBOT_MAX] = { 0 };
	Player pl; memset(&pl, 0, sizeof(pl));
	int sp0 = 0;
	pl.pos = v3(spawns[sp0].x, spawns[sp0].y, spawns[sp0].z + 5); pl.yaw = spawns[sp0].yaw;
	// bots: navigation mesh + third-person Batou
	{
		// the playable characters (third person): every chr_<name>.gvm that is present
		static const char *CHARS[] = { "bato", "motoko", "saito", "togusa", "ishikawa", "borma", "paz", "azuma", "kuro", "maven", "reiko", "sitara" };
		{ FILE *cf = fopen(VDIR "cpuall", "rb"); if (cf) { skin_all_cpu = 1; fclose(cf); } }
		LDP_PUSH(LDP_CHR);
		for (int i = 0; md_ok && i < (int)(sizeof(CHARS) / sizeof(CHARS[0])); i++) {
			boot_step(0.78f + 0.17f * i / 12); char p[96]; sprintf(p, VDIR "chr_%s.gvm", CHARS[i]); int r = chrt_load(p, CHARS[i]);
			if (r >= 0) { Model *M = &chrt[r].m; int nr = 0; for (int k = 0; k < M->nmesh; k++) nr += M->mesh[k].rigid >= 0;
				logp("character %s: %d bones, %d meshes (%d rigid), %d clips\n", CHARS[i], M->nb, M->nmesh, nr, M->nclip); }
			else if (r != -1) logp("character %s not used (%d)\n", CHARS[i], r);
		}
		chr_reskin_log = logp; chr_skins_load(VDIR, logp); chr_hi_load(VDIR, logp);   // b71: the operators' skins (bots.h)
		logp("operator masks: %d models with a mask / goggle part in chr_gog.txt%s (b103)\n", nchr_gog, nchr_gog ? "" : " (none: no toggle)");
		if (md_ok) { int r = chrt_load(VDIR "chr_cyborg.gvm", "cyborg");   // b63: Ghost Assault's cyborg body (vm/make_cyborg.py): a type outside the operators' list (nchrt)
			if (r >= 0) { ga_cyb_ct = r; nchrt--; logp("character cyborg (Ghost Assault): %d bones, %d meshes\n", chrt[r].m.nb, chrt[r].m.nmesh); } else logp("character cyborg: not loaded (%d): Ghost Assault's cyborgs keep their operators\n", r); }
		if (md_ok) skin_gpu_select(VDIR, vm_ok ? &wsl[wcur].m : NULL, logp);   // chars b35: GPU or CPU skinning per class (bots.h / skin_gpu.h: start-up self-test)
		LDP_POP(); boot_step(0.95f); LDP_PUSH(LDP_BOTS);   // perf (b45): phases
		int cr = nchrt ? 0 : -1;
		if (cr < 0) logp("bots: no character models, no bots\n");
		else {
			BotTarget T = { pl.pos, pl.vel, 1 }; lis_pos = pl.pos; bot_on_impact = on_bot_impact; bot_on_shot = on_bot_shot; bot_on_blood = blood_hit; bot_on_destruct = bp_spawn; bp_logf = logp; bot_exec_player = on_bot_exec_player; pgrab_bot = -1;
			team_bases(); bot_spawn_pick = team_spawn; bot_on_kill = on_bot_kill;
			if (bots_init(5, &T) < 0) logp("bots: clips or bones missing\n");
			else { bots_ok = 1; for (int i = 0; i < nbot; i++) { bots[i].team = i < 2 ? 0 : 1; bot_spawn(&bots[i], &T); }
				logp("bots: %d (2 allies, 3 enemies), from %d characters; team bases %s\n", nbot, nchrt, teams_ok ? "found" : "not found (free for all spawns)");
				{ int ts = team_spawn(0); if (ts >= 0) { sp0 = ts; pl.pos = v3(spawns[ts].x, spawns[ts].y, spawns[ts].z + 5); pl.yaw = spawns[ts].yaw; } }
				for (int i = 0; i < nbot; i++) logp("  bot %d (%s) spawns at %.0f %.0f %.0f, path of %d points\n", i, chrt[bots[i].ct].name, bots[i].pl.pos.x, bots[i].pl.pos.y, bots[i].pl.pos.z, bots[i].npath); }
		}
		LDP_POP();
	}
	nbot_full = nbot; if (range_active()) nbot = 0;
	float pos[3], yaw = 0, pitch = 0, P[16], V[16], MVP[16], fwd[3];
	int fly = 0, sel_board = 0;   // (b76: sel_board = Select held: the scoreboard)
	persp(P, 70 * 3.14159f / 180, 960.0f / 544, 10, view_far);
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
	int vmfp_frames = 0, vmfp_light = 0; long vmfp_idx = 0;   // b60: the per-second line's first-person mesh figures
	SceCtrlData pad, old = {0}; int lmon = 1, cull = 1, frames = 0; SceUInt64 t0 = sceKernelGetProcessTimeWide(), tl = t0;
	int drawn = 0;
	float tsec = 0, fmin = 1e9, fsum = 0; int fcount = 0;
	int audio_test_phase = 9; /* audio diagnostic sequence off (kept for future checks) */ float audio_test_t = 0, burst_t = 0, burst_cd = 0, melody_again = 0;
	// autoplay tours every map: a few seconds of third-person camera on a bot, then first person: walk the navigation mesh to random spawn points, fighting bots on the way
	int viewat_on = 0;   // (b109 test switch viewat.txt)
	const float TOUR_SPECT = 4, TOUR_MAP_T = 22, TOUR_MAX = 760; int spect_bot = 0, tour_i = tour_start; float spect_ang = 0, fight_t = 0, ap_gren_hold = 0, ap_next_gren = 4, ap_next_swap = 9;
	int falls = 0;
	AutoWalk aw; memset(&aw, 0, sizeof(aw)); aw_reset(&aw);
	// per-map numbers for the tour log
	float map_t = 0, m_fsum = 0, m_fmin = 1e9f, m_dist = 0, m_load = load_s, m_bots_ms = 0; int m_fcount = 0, m_falls = 0;
	int m_kills0 = 0, m_shots0 = 0, m_hits0 = 0, m_pd0 = 0, m_sr0 = 0, m_bots_n = 0; long m_drawn = 0, m_dframes = 0;
	int want_map = -1, tex_rechecked = 0; float last_fps = 0, vm_lit[3] = { 1, 1, 1 };
	if (autoplay) logp("autoplay: touring all %d maps, %.0f s each (%.0f s third-person camera, then first person)\n", NMAPS, TOUR_MAP_T, TOUR_SPECT);
	float ground_time = 0, total_time = 0;
	float fe_t = 0, tw_t = 9, pause_dt = 0; int paused = 0, opts_open = 0, ap_finish = 0, end_result = 2, m_pkills = 0, m_pdeaths = 0;
	if (autoplay) menu_config_default(&cfg); else cfg_load();
	opmask_apply();
	mtk_fill();   // b74: MY THINK TANK's parts (tk_parts.txt)
	cfg_apply(); pvoice = op_voice(cfg.chr);
	skw_bind(&pl, &php, &pap, &pdead_t, &pitch, &pflash, &kill_marker, &hit_marker, &notify_t, &streak_t, &fly, &intel, &kills, &m_pkills, &notify_pts, &streak, &autoplay, &notify); skw_match_start();
	tkg_bind(&pl);   // Think Tanks / Terminal Conquest (thinktank.h, ttm.h)
	mfe_vid_open = mfe_vid_open_; mfe_vid_frame = mfe_vid_frame_; mfe_vid_close = mfe_vid_close_;   // b63: DETAILS videos
	{ FILE *tv = fopen(VDIR "mv_sk_togusa.mp4", "rb"); mfe_tog_skill_vid = tv != NULL; if (tv) fclose(tv); logp("details: Togusa's skill video %s (mv_sk_togusa.mp4, b117)\n", tv ? "there" : "missing: his PROFILE tab only"); }
	mfe_skin_n = mfe_skin_n_; mfe_skin_name = mfe_skin_name_;   // b71: operator skins
	mfe_skin_gog = mfe_skin_gog_;   // b103: the skin has a mask / goggle part (Square on the operative screen)
	tut_bind(&pl, &pitch, &php, &pap, &pdead_t, &intel, &fly, &autoplay); LDP_PUSH(LDP_TUT); int tir_ = tut_init(VDIR); LDP_POP(); logp("tutorial: %s\n", tir_ < 0 ? "assets MISSING (tut_ui.gtx / tut_fx.gtx)" : "ok");
	boot_step(1.0f); boot_scr_on = 0; LDP_PUSH(LDP_LOGOS); tut_play_logos(); LDP_POP();   // start-up logos (tut_logos.mp4), skippable
	elim_bind(&pl, &pitch, &php, &pap, &pdead_t, &hurt_flash, &reload_t, &fly, &autoplay, &match_over_t, &end_result, &pflash);
	net_bind_menu(&cfg); net_game_bind(&match_t, &match_over_t, &end_result, &m_pdeaths, &reloads, &shots, &pdeaths);   // net: the AD-HOC screens' hooks, the match code's view of main's state
	int netauto = net_auto_load(); if (netauto) logp("netauto: %s test (%s)\n", NA.host ? "host" : "join", NA.probe ? "inettest" : "netauto.txt");   // net (test): a scripted room member
	if (netauto && NA.probe) autoplay = 0;   // (b108, inettest: the room's test, not the tour - the test scripts always add autoplay)
	if (menu_fe < 0 && world_lite) want_map = cur_map;   // perf (b45): no front end: straight into a match on the start-up map, so its world loads first
	if (menu_fe >= 0) {   // start in the front-end (the test run tours the menus first)
		phase = PH_FRONT; menu_fe_open(FE_TITLE); grade_identity(); map_audio_pending = 0;
		if (music_cur_ev != ev_find("bgm/logo")) menu_music("bgm/logo", 300);   // b59: the logos' music goes on under the title (tut_play_logos starts it)
	}
	for (;;) {
		if (phase != PH_MATCH) {   // front-end / results: menus only (the last map stays loaded underneath)
			sceCtrlPeekBufferPositive(0, &pad, 1);
			SceUInt64 now = sceKernelGetProcessTimeWide(); float dt = (now - tl) / 1e6f; tl = now; if (dt > 0.05f) dt = 0.05f;
			unsigned prb = pad.buttons & ~old.buttons; old = pad;
			// b70 (user: the intro video with Batou and Motoko "when you start the game yourself", as the "Tutorial Stuff" / Early
			// Access footage: title -> INTRO MOVIE page -> the cut scene): played once a start-up after the title is left, unless
			// OPTIONS > GAME > INTRO MOVIE is OFF; skippable after 1 s ({x} / {o} / START / a tap). The test tour plays it in the
			// tutorial (tut_play_intro, its first 20 s) as before; the program's own play is skipped when it has already run.
			if (MFE.intro_req) { MFE.intro_req = 0;
				if (!autoplay && !cfg.intro_off && !TU.intro_done) { TU.intro_done = 1; logp("intro movie after the title (b70)\n"); tut_play_intro(); menu_music("bgm/lobby", 1500); }
				tl = sceKernelGetProcessTimeWide(); sceCtrlPeekBufferPositive(0, &pad, 1); old = pad; prb = 0; }
			unsigned mh = menu_buttons(pad.buttons), mp = menu_buttons(prb);
			{ static unsigned sold; unsigned sb = 0;   // the left stick works like the d-pad
			  if (pad.ly < 40) sb |= MB_UP; if (pad.ly > 216) sb |= MB_DOWN; if (pad.lx < 40) sb |= MB_LEFT; if (pad.lx > 216) sb |= MB_RIGHT; mh |= sb; mp |= sb & ~sold; sold = sb; }
			fe_t += dt; snd_update(dt); menu_tick(dt); snd_set_ingame(0); tv_close_poll();   // b59: the menu mix (b63: a DETAILS video closed on its own thread)
			net_frame(dt);   // net: the session (discovery, the room) runs under the menus
			if (autoplay) {   // scripted tour of the menus for the test run (menu_fe.h MENU_TOUR: buttons and taps), then the map tour
				mp = mh = 0;
				if (phase == PH_FRONT) { int ap_dn = 0; mp = mh = menu_fe_autoplay(fe_t, &ap_dn);
					if (ap_dn) { menu_config_default(&cfg); opmask_apply(); cfg_apply(); phase = PH_MATCH; music_stop(400); menu_fe_unload(); chr_hi_free(); wpn_insp_free(); logp("autoplay: menu tour done (%.0f s)\n", fe_t); map_audio_pending = 1; elim_match_setup(MAPS[cur_map].stem, !strcmp(MAPS[cur_map].stem, "urban"), 1, bots_ok);   /* the test run plays Elimination on Urban */ { char gp[96]; sprintf(gp, VDIR "%s.glut", MAPS[cur_map].stem); grade_load(gp); } tl = t0 = sceKernelGetProcessTimeWide(); frames = 0; map_t = 0; if (tour_start || world_lite) want_map = cur_map; continue; } }   /* perf (b45): world_lite: the start-up loaded no world */   // b43: a tour started mid-way (tourstart.txt) reloads its first map through the full setup, so Demolition / Terminal Conquest maps start in their mode (QA b42)
				else { static float rt; rt += dt; if ((rt > 6 && rt - dt <= 6) || (rt > 11 && rt - dt <= 11) || (rt > 15 && rt - dt <= 15)) mp = rt < 7 ? MB_OK : MB_R;
					if (rt > (mrs_test_hold ? 32 : 19)) { log_mem(); logp("autoplay finished\n"); break; } }   // (b108, restest: line-up 13 s, TOP OPERATIVE 13 s, the tabs)
			}
			if (netauto) { mp = mh = 0; if (net_auto_menu(&cfg, dt, phase == PH_RESULTS, &mp) < 0) { logp("stopping: netauto done\n"); break; } }   // net (test)
			{ static float dlg_t; if (net_dialog) dlg_t = 0.35f; else if (dlg_t > 0) dlg_t -= dt;   // b108: while a system dialog (the ad-hoc connection, the room code's keyboard) is up, and a moment after: its buttons and taps are not the menu's
			  if (dlg_t > 0) { mp = mh = 0; mu_touch_raw(0, (float)MUT.x, (float)MUT.y); } else menu_touch_poll(dt); }   // the front panel (or the tour's scripted taps) for the menus
			tc_tick(now / 1e6f);   // render: texture-memory canary (texcanary.h)
			glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
			int start = 0;
			if (phase == PH_FRONT) {
				int fe_scr0 = MFE.screen, r = menu_fe_update(&cfg, mp, mh, dt);
				if (r == FE_QUIT) { logp("quit: EXIT GAME\n"); cfg_save(); break; }   // b39: quit lines (QA: the emulator keeps its window after the game exits)
				if (r == FE_APPLY) { cfg_check(&cfg); cfg_apply(); cfg_save(); }
				if (r == FE_START) { cfg_check(&cfg); if (!netauto) cfg_save(); start = 1; rh_sb_type = MFE.screen == FE_NETROOM ? (NL.kind == 2 ? "INTERNET MATCH" : NL.kind ? "LAN MATCH" : "AD-HOC MATCH") : MFE.screen == FE_CUSTOM ? "CUSTOM MATCH" : cfg.map == MENU_MAP_SHOOTING ? "SHOOTING RANGE" : "BOT MATCH"; }   // perf (b38): the scoreboard header (hud_round.h)
				if (fe_scr0 != FE_TITLE) { if (mp & MB_OK) ui_sfx("ui/click_basic"); else if (mp & (MB_UP | MB_DOWN | MB_LEFT | MB_RIGHT)) ui_sfx("ui/mouse_over_basic"); else if (mp & MB_BACK) ui_sfx("ui/character_cancel"); }   // (the title has its own)
				menu_fe_draw_bg(); { float rc[4], cm[5]; int op = menu_fe_preview(&cfg, rc, cm); if (op == -2) { fig_input(&pad, rc, MFE.screen * 16, dt); cm[4] += fig_spin; tk_menu_preview(rc, cm, dt); } else if (MFE.screen != FE_TANK && !(MFE.screen == FE_OPTIONS && MFE.opt_back == FE_TANK)) tk_menu_free();   /* b74 */
				  if (op == -3) { static float tilt; const float drag_[4] = { 376, 60, 584, 446 }; fig_input(&pad, drag_, 3000 + MFE.insp_w, dt); int ry = (int)pad.ry - 128; if (ry > 24 || ry < -24) tilt = fminf(40, fmaxf(-40, tilt - (ry > 0 ? ry - 24 : ry + 24) / 103.0f * 90 * dt));   /* b89: INSPECT - the stick / a drag turns it, up / down tilts it */
				    menu_insp_state = wpn_inspect(menu_wpn_vm(MFE.insp_w), rc, cm[4] - fig_spin, cm[2] + tilt, dt);   /* (minus: the side under the finger follows it) */ } else if (winsp_ok || winsp_want[0]) wpn_insp_free();
				  fig_zoom_en = op >= 0 && MFE.screen == FE_OPERATIVES && !MFE.det_on; { static int opz = -1; if (opz < 0) { FILE *zf = fopen(VDIR "opzoom", "rb"); opz = zf != NULL; if (zf) fclose(zf);
				      zf = fopen(VDIR "opmask", "rb"); if (zf) { fclose(zf); memset(cfg.gog_off, 0xff, sizeof(cfg.gog_off)); logp("opmask: every skin's mask off (test switch, b103)\n"); }
				      zf = fopen(VDIR "opstart.txt", "rb"); if (zf) { char nm[16] = ""; if (fscanf(zf, "%15s", nm) == 1) for (int q = 0; q < 12; q++) if (!strcmp(MENU_CHR[q].stem, nm)) { cfg.chr = q; logp("opstart: %s (test switch, b103)\n", nm); } fclose(zf); }
				      zf = fopen(VDIR "opwpn.txt", "rb"); if (zf) { char nm[16] = ""; if (fscanf(zf, "%15s", nm) == 1) for (int q = 0; q < MENU_NWPN; q++) if (MENU_WPN[q].slot == 0 && !strcmp(MENU_WPN[q].vm ? MENU_WPN[q].vm : MENU_WPN[q].sh, nm)) { cfg.primary = q; logp("opwpn: %s (b105 test switch: the gun the menu figure holds)\n", nm); } fclose(zf); } }
				    { static int opv = -1; static float opv_c[4]; if (opv < 0) { FILE *vf = fopen(VDIR "ophand.txt", "rb"); opv = vf && fscanf(vf, "%f %f %f %f", &opv_c[0], &opv_c[1], &opv_c[2], &opv_c[3]) == 4; if (vf) fclose(vf); if (opv) logp("ophand: the operative page's figure seen at height %.0f from %.0f cm, eye height %.0f, turned %.0f deg (b118 test switch)\n", opv_c[0], opv_c[1], opv_c[2], opv_c[3]); }
				      if (opv && op >= 0 && MFE.screen == FE_OPERATIVES) { cm[0] = opv_c[0]; cm[1] = opv_c[1]; cm[2] = opv_c[2]; cm[4] = opv_c[3]; } }
				    if (opz && fig_zoom_en) fig_zoom = 0.8f; }   /* (b103 test switches: opzoom - the operative page's figure close up; opmask - masks off; opstart.txt - the operator the tour starts at) */ menu_fe_zoom = fig_zoom;   /* b76 */
				  if (op >= 0) { fig_input(&pad, rc, MFE.screen * 16 + (MFE.screen == FE_OPERATIVES ? MFE.sel[FE_OPERATIVES] & 15 : 0), dt); cm[4] += fig_spin; chr_prev_choice = MFE.screen == FE_OPERATIVES && !MFE.det_on; chrt_reskin(ct_of_op(op), menu_fe_skin_shown(&cfg, op) % mfe_skin_n_(op));   /* b76: the skin shown, not yet selected */ chr_preview(op, MENU_WPN[cfg.primary].sh, rc, cm, dt); chr_prev_choice = 0; } if (MFE.screen != FE_OPERATIVES) og_op = -1; } menu_fe_draw(&cfg);   // ui b50: with the loadout's primary
			} else {
				int r = menu_results_update(mp, dt);
				{ int lo[5], lp[5]; const char *lw[5]; float lc[5], full[4] = { 0, 0, 960, 544 }; int ln = menu_results_lineup(lo, lw, lp, lc); if (ln > 0) { fig_input(&pad, full, 1000 + MRS2.phase, dt); chr_lineup(0, ln, lo, lw, lp, lc, -fig_spin, dt); } }   // b96: the line-up's poses and its out-of-focus layer, before the screen is drawn on
				menu_results_draw_bg(); { float rc[4], cm[5]; int op = menu_results_preview(rc, cm); if (op >= 0) { const float drag_[4] = { 600, 56, 360, 380 }; fig_input(&pad, MRS2.phase == MRS_TABS ? drag_ : rc, 1000 + MRS2.phase, dt); cm[4] += fig_spin;   /* b77: on the tabs the figure turns from a drag on it, not the table */
				  chr_prev_result = menu_results_anim(&chr_prev_ready); chr_preview(op, menu_results_preview_weapon(), rc, cm, dt); chr_prev_result = chr_prev_ready = 0; } else res_key = -1; }
				{ int lo[5], lp[5]; const char *lw[5]; float lc[5]; int ln = menu_results_lineup(lo, lw, lp, lc); if (ln > 0) chr_lineup(1, ln, lo, lw, lp, lc, -fig_spin, dt); }   // b58: the team line-up (it turns the operators, not the camera: the opposite sign turns them the same way); b96: TOP OPERATIVE too, the camera closing in
				menu_results_draw();   // ui b50: the gun that operator used
				if (r == 1) { int was_net = NG.on; net_back_to_room(); phase = PH_FRONT; menu_fe_open(was_net ? FE_NETROOM : FE_LOBBY); menu_music("bgm/lobby", 800); }   // net: back to the room
			}
			glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
			vglSwapBuffers(net_dialog ? GL_TRUE : GL_FALSE);   // net: the ad-hoc connection dialog (a system common dialog) draws in the swap
			if (LDP.on && !strcmp(LDP.what, "boot to the title screen")) {
				ldp_report();   // perf (b45): the first menu frame is on screen
#ifdef GITSFA_PGO_GEN
				{ FILE *pe = fopen(VDIR "pgoearly", "rb"); if (pe) { fclose(pe); pgo_write(); } }   // (test: the profile written at the title too)
#endif
			}
			if (start) {   // the player's choices take effect: operator, loadout, bots, limits; then load the map
				ldp_start("START to play");   // perf (b45): load phases up to the first frame of play (ldprof.h)
				menu_fe_unload(); chr_hi_free(); wpn_insp_free(); pvoice = op_voice(cfg.chr);
				if (cfg.tut == 0 && cfg.map == MENU_MAP_TUTORIAL && cfg.team_size == 1) cfg.tut = TUT_FULL_RUN;   // the TUTORIAL card without a program choice: the whole tutorial
				if (cfg.tut > 0) tut_match_setup(&cfg, 0);   // map, team, limits; the intro cut scene before the first program
				if (md_ok) loadout_load(menu_wpn_vm(cfg.primary), menu_wpn_vm(cfg.secondary), cfg.melee ? "axe" : "knife");   // (b82: the variant's own first-person model)
				int ts = cfg.team_size < 1 ? 1 : cfg.team_size > 4 ? 4 : cfg.team_size; n_allies = ts - 1; if (bots_ok) bots_set_count(ts * 2 - 1);
				score_limit = cfg.mode == MODE_TDM ? cfg.score_limit : 0; match_len = cfg.time_limit > 0 ? cfg.time_limit * 60 : 0;
				logp("match: %s on %s, operator %s, %s + %s, %d v %d, score limit %d, %d min\n", MENU_MODE_NAME[cfg.mode], MAPS[cfg.map].stem, MENU_CHR[cfg.chr].name, MENU_WPN[cfg.primary].name, MENU_WPN[cfg.secondary].name, ts, ts, score_limit, match_len / 60);
				if (music_cur_ev != ev_find("bgm/ready_room")) menu_music("bgm/ready_room", 300);   // b59: (the waiting room may have started it)
				phase = PH_MATCH; want_map = cfg.map;
			}
			continue;
		}
		if (want_map >= 0) {   // load another map and start fresh on it
			int mi = want_map; want_map = -1;
#ifdef GITSFA_PGO_GEN   // perf (b45): a profile run: the counters so far to the card at every map load (the run may end any way)
			pgo_write();
#endif
			if (LDP.on && !strcmp(LDP.what, "START to play")) snprintf(LDP.what, sizeof(LDP.what), "START to play on %s", MAPS[mi].stem);   // perf (b45)
			else { ldp_start(""); snprintf(LDP.what, sizeof(LDP.what), "map change to %s", MAPS[mi].stem); }
			SceUInt64 l0 = sceKernelGetProcessTimeWide();
#if defined(__vita__) && defined(__arm__)
			vglWaitVblankStart(GL_FALSE);   // perf (b46): no vsync for the loading screen's redraws (each waited up to a VBlank); back on below
#endif
			ld_on = menu_ovl >= 0; ld_stem = MAPS[mi].stem; ld_name = MENU_MAP[mi].name; menu_loading_reset();
			if (ld_on) loading_progress(0, "LOADING"); else loading_screen(MAPS[mi].name);
			logp("loading map %s\n", MAPS[mi].stem);
			if (load_level(mi) < 0) { logp("map %s failed to load; back to %s\n", MAPS[mi].stem, MAPS[cur_map].stem); if (load_level(cur_map) < 0) { logp("stopping: reload failed\n"); break; } }
			else cur_map = mi;
			m_load = (sceKernelGetProcessTimeWide() - l0) / 1e6f;
			map_fog(fog, fogp); /* databuilder */ memcpy(md_fogp, fogp, 12); map_audio_pending = 1; glClearColor(fog[0], fog[1], fog[2], 1);
			logp("collision: %d tris, grid %dx%d, %d spawn points; navigation mesh: %d triangles; loaded in %.2f s\n", nctri, gw, gh, nspawn, nnav, m_load);
			log_mem();
			if (!autoplay) { char lm[16]; snprintf(lm, sizeof(lm), "%-8d\n", cur_map); alog_file_put(VDIR "lastmap.txt", lm, 9); }   // (b60: fixed size, in place, off this thread)
			else if (0) tex_dump(MAPS[cur_map].stem);   // texture dumps for host checks (b26); off
			team_bases(); sp0 = nspawn ? rand() % nspawn : 0; { int ts = team_spawn(0); if (ts >= 0) sp0 = ts; } memset(&pl, 0, sizeof(pl));
			if (nspawn) { pl.pos = v3(spawns[sp0].x, spawns[sp0].y, spawns[sp0].z + 5); pl.yaw = spawns[sp0].yaw; } else pl.pos = v3(0, 0, 500);
			pitch = 0; fly = 0; php = 100; pdead_t = 0; hs_lunge = 0; hs_from = hs_back = -1; hs_was = 0; for (int k = 0; k < WS_N; k++) wslot_refill(k); reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0; memset(gren, 0, sizeof(gren)); hurt_flash = dmg_ind = 0; fx_clear(); bp_reset(); recoil = 0; pap = 100; match_t = match_len > 0 ? match_len : 600; score_l = score_r = 0; intel = 0; nfeed = 0; cool_t = -1; camo_t = 0; streak = 0;
			match_over_t = 0; tw_t = 9; paused = 0; m_pkills = m_pdeaths = 0; killer_bot = -1; memset(kills_on_me, 0, sizeof(kills_on_me)); memset(my_kills_on, 0, sizeof(my_kills_on)); WP.quick_from = -1; WP.gren_state = 0;
			if (autoplay && md_ok) { FILE *wf_ = fopen(VDIR "viewwpn.txt", "rb"); if (wf_) { char a_[16] = "m4a1", b_[16] = "seburom5"; if (fscanf(wf_, "%15s %15s", a_, b_) >= 1) { loadout_load(a_, b_, "knife"); logp("viewwpn: %s %s (b109 test switch)\n", a_, b_); } fclose(wf_); } }
			if (autoplay && md_ok && tour_i > 0 && !viewat_on) { static const char *TW[13] = { "m4a1rd", "scar", "acr", "mp5", "kriss", "l118", "870", "mg36", "srv97", "seo90", "ksg", "sta12", "uss12" };   /* b97: + the three shotguns (maps 10-12) */   /* b91: + a bolt-action rifle whose bolt plays faster and a semi-auto one; b92: the MG36-A for the M249 */   /* b82: the red-dot M4A1 (the irons: tutorial maps) */ { static const char *TS[4] = { "seburom5", "b93se", "mateba", "mag44" }; loadout_load(TW[tour_i % 13], TS[tour_i & 3], tour_i % 3 == 2 && !tourknife ? "axe" : "knife");   /* (b98: the Double Katar on every third map) */ } }   /* b92: + the 93-SE (bursts) and the 44 MAGNUM */
			else if (vm_ok) { WP.sw_to = wcur; weapon_finish_switch(); }
			aw_reset(&aw);
			nbot = range_active() ? 0 : nbot_full;
			LDP_PUSH(LDP_SPARE); wpn_spare_prepare(VDIR, bots_ok && nbot > 0); LDP_POP();   // the bots' gun stays loaded for pick-ups (wpn.h); perf (b45): phases
			LDP_PUSH(LDP_BOTS);
			if (bots_ok && nspawn) { BotTarget T = { pl.pos, pl.vel, 1 }; match_pick_skins(); bp_reset(); bots_pick_chars(); bot_assist_reset(); for (int i = 0; i < nbot; i++) { bots[i].team = i < n_allies ? 0 : 1; bots[i].kills = bots[i].deaths = 0; bot_spawn(&bots[i], &T); } spect_bot = 0;
				logp("bots:"); for (int i = 0; i < nbot; i++) logp(" %s", chrt[bots[i].ct].name); logp("\n"); }
			LDP_POP(); LDP_PUSH(LDP_MODE);
			{ int want_dem = autoplay ? !strcmp(MAPS[cur_map].stem, "pss9") || !strcmp(MAPS[cur_map].stem, "downtown") : cfg.mode == MODE_DEM;   // the test run plays Demolition on two maps
			  dem_on = 0; bot_no_respawn = 0; bot_goal = NULL; bot_freeze = 0; dem_prog = -1; dem_banner_t = dem_band_t = dem_sw_t = dem_start_t = 99;
			  if (dmap.ok) dem_free(&dmap);
			  if (want_dem && bots_ok && nbot > 0) { char dp[96]; sprintf(dp, VDIR "%s.gdem", MAPS[cur_map].stem); int dr = dem_load(&dmap, dp);
				if (dr == 0 && dmap.nsite) { dem_on = 1; dem_start(&dm, &dmap, 0); dem_add_site_col(&dmap);
					if (!autoplay && cfg.score_limit > 0) { dm.to_win = cfg.score_limit; dm.switch_after = dm.to_win - 1 > 0 ? dm.to_win - 1 : 1; }   // b83 (v2.0, EP20 / EP26: first to 6, sides switch after round 5)
					bot_no_respawn = 1; bot_goal = dem_goal; bot_hold_defuse = dem_bot_defusing; match_len = 0; score_limit = 0;
					if (!dem_bomb_ok && md_ok) dem_bomb_ok = model_load(&dem_bomb, VDIR "dem_bomb.gvm") == 0 ? 1 : -1;
					logp("demolition: %d sites, %d / %d spawns, first to %d rounds (switch after %d), bomb model %s\n", dmap.nsite, dmap.nsp[0], dmap.nsp[1], dm.to_win, dm.switch_after, dem_bomb_ok > 0 ? "ok" : "missing"); }
				else { if (dmap.ok) dem_free(&dmap); logp("demolition: %s not usable (%d): team deathmatch instead\n", dp, dr); } } }
			{ int want_ttm = autoplay ? !strcmp(MAPS[cur_map].stem, "underground") : cfg.mode == MODE_TTM;   // the test run plays Terminal Conquest on Underground
			  ttm_stop(); tk_clear(); bot_spawn_pick = team_spawn;
			  if (want_ttm && !dem_on && bots_ok && nbot > 0) memcpy(tkp_sel, cfg.tk_part, sizeof(tkp_sel));   // b74: My Think Tank's parts (ttm_begin loads them)
			  if (want_ttm && !dem_on && bots_ok && nbot > 0) { int tr = ttm_begin(VDIR, MAPS[cur_map].stem, autoplay || cfg.score_limit == MENU_LIMITS[MODE_TTM][MENU_LIMIT_DEF[MODE_TTM]] ? 0 : cfg.score_limit, autoplay ? 0 : (float)match_len);   // the menu's default: the map's own (first to 5 / 4)
				if (tr == 0) { bot_goal = ttm_bot_goal; bot_spawn_pick = ttm_spawn_pick_bot; match_len = 0; score_limit = 0; ttm_auto_deploy = autoplay || netauto; net_auto_ttm_start();
					for (int i = 0; i < nbot; i++) ttm_place_bot(i); { float sp_[3], sy_; if (ttm_spawn_point(0, sp_, &sy_)) { pl.pos = v3(sp_[0], sp_[1], sp_[2] + 5); pl.yaw = sy_; } }
					if (autoplay) { tm.t = TTM_REBOOT - 4; tm.speed = 3; ttm_call_tank(nbot > n_allies ? 1 + n_allies : 1, bots[nbot > n_allies ? n_allies : 0].pl.pos, 0); tm.ready[0] = 1; tm.gauge[0] = 1; ttm_ready_t = -1; }   // test: online in 4 s, 3x capture speed, an enemy tank from the start, the player's call-in (placement, auto deploy)
					logp("terminal conquest: %d terminals, first %c, first to %d, tanks %s\n", ttm_map.n, ttm_map.t[ttm_map.start].letter, tm.to_win, tk_ok > 0 ? "ok" : "MISSING"); }
				else logp("terminal conquest: %s.gttm not usable (%d): team deathmatch instead\n", MAPS[cur_map].stem, tr); } }
			{ int em_want = autoplay ? (!strcmp(MAPS[cur_map].stem, "urban") ? 1 : !strcmp(MAPS[cur_map].stem, "ghost_storage") ? 2 : 0) : cfg.mode == MODE_EM ? 1 : cfg.mode == MODE_GA ? 2 : 0;
			  if (em_want == 2 && (net_host_on() || net_client_on())) { em_want = 1; logp("ghost assault: offline only; the room plays Elimination\n"); }   // (net: the mode's state is not in the snapshots)
			  elim_match_setup(MAPS[cur_map].stem, em_want, autoplay, bots_ok); }   // Elimination, Ghost Assault (elim_game.h, ga.h)
			LDP_POP();
			net_match_setup(); net_feed_hook = net_host_on() ? net_host_feed : NULL; net_kill_hook = net_host_on() ? net_host_kill_note : NULL;   // net: the room's players take their places (net_game.h)
			if (!autoplay && !menu_wpn_class_ok(cfg.chr, cfg.primary)) cfg.primary = menu_class_default(cfg.chr);   // b88: v2.0 classes (whatever screen changed the operator)
			if (autoplay) { cfg.chr = tour_chr >= 0 ? tour_chr : tour_i % 12; pvoice = op_voice(cfg.chr); }   // test run: a different operator (skill) on each map (b76: or tourchr.txt's)
			ab_map_start();
			skw_match_start();   // skills: everyone ready, nothing deployed
			hv_skill_scene(VDIR, MENU_CHR[cfg.chr].stem, cfg.chr);   // b102: the skill gesture's hologram of this operator (hands.h)
			WR.phase = WR_OFF; wr_dive_free();   // b84: the dive and the waiting room (wroom.h; b102: its scenes freed): real matches, not the training programs, the range or a net room
			if ((!autoplay || wroom_test) && cfg.tut <= 0 && !range_active() && !NG.on && md_ok) {
				wr_begin(dem_on ? MODE_DEM : ttm_on ? MODE_TTM : elim_on ? (ga_on ? MODE_GA : MODE_EM) : cfg.mode); wr_add(cfg.name, cfg.chr, 0, 1);
				for (int i = 0; bots_ok && i < nbot; i++) { int c_ = op_of_chrt(bots[i].ct); wr_add(bot_name(i), c_ < 0 ? 0 : c_, bots[i].team, 0); }
				wr_sp = pl.pos; wr_yaw = pl.yaw; }
			tex_rechecked = 0; light_at(pl.pos.x, pl.pos.y, pl.pos.z + 20, vm_lit);
			map_t = 0; m_fsum = 0; m_fmin = 1e9f; m_fcount = 0; m_dist = 0; aw.reached = aw.stuck = aw.moved = 0; m_falls = 0; m_bots_ms = 0; m_bots_n = 0; m_drawn = m_dframes = 0;
			m_kills0 = kills; m_shots0 = bot_total_shots; m_hits0 = bot_total_hits; m_pd0 = pdeaths; m_sr0 = bot_stuck_respawns;
			if (cfg.tut > 0) tut_begin(cfg.tut);   // a training program on this map (tutorial.h)
			tl = t0 = sceKernelGetProcessTimeWide(); frames = 0;
			LDP_PUSH(LDP_FRAME);   // perf (b45): the first frame of play ends the account (after its swap, below)
			ds_reset();   // perf (b45): the display step restarts after a load (dstep.h)
#if defined(__vita__) && defined(__arm__)
			vglWaitVblankStart(ft_unlock ? GL_FALSE : GL_TRUE);   // perf (b46): vsync back for play (frametime.h's probe windows keep it off)
#endif
			continue;
		}
		sceCtrlPeekBufferPositive(0, &pad, 1);
		SceUInt64 now = sceKernelGetProcessTimeWide(); float dt = (now - tl) / 1e6f; tl = now; if (dt > 0.05f) dt = 0.05f;
		if (DS.on) { dt = ds_step(dt, !paused && !ft_unlock); if (dt > 0.05f) dt = 0.05f; }   // perf (b45): display-locked step (dstep.h); the clock's when paused / vsync off
		if (tkslow_test && tk_slow_t > 0) { tk_slow_t -= dt; dt *= 0.15f; }   // b86 test switch (thinktank.h tk_slow_t)
		if (exectest && ga_on && em.state == ES_LIVE && bots_ok && pdead_t <= 0 && !paused) {   // b70 test (see exectest above)
			int busy_ = wst.on || pgrab_bot >= 0; for (int j = 0; j < nbot; j++) if (bots[j].ex_on) busy_ = 1;
			if (busy_) dt *= 0.25f;
			else if ((xt_t += dt) > 5) { xt_t = 0; int k_ = xt_n++ % 3, pc_ = ga_player_cyborg(); float c_ = cosf(pl.yaw), s_ = sinf(pl.yaw);
				int gh_ = -1, cy_ = -1; for (int j = 0; j < nbot; j++) if (bots[j].state != BS_DEAD && !bots[j].grabbed && !bots[j].ex_on) { if (ga_cyborg(1 + j)) { if (cy_ < 0) cy_ = j; } else if (gh_ < 0) gh_ = j; }
				if (pc_ && k_ < 2 && gh_ >= 0 && wcur == WS_KNIFE) { Bot *v_ = &bots[gh_]; v_->pl.pos = v3(pl.pos.x + c_ * 110, pl.pos.y + s_ * 110, pl.pos.z); v_->pl.vel = v3(0, 0, 0);
					v_->pl.yaw = k_ == 0 ? pl.yaw + 3.14159265f : pl.yaw; v_->prev_yaw = v_->pl.yaw; v_->hp = 40; pitch = 0;
					wpn_exec_start(&wsl[WS_KNIFE], gh_, -1, k_ == 0, pl.pos, &pl.yaw); logp("exectest: the player executes bot %d (%s)\n", gh_, k_ == 0 ? "front" : "from behind"); }
				else if (pc_ && cy_ >= 0 && gh_ >= 0) { Bot *v_ = &bots[gh_], *a_ = &bots[cy_]; V3 vp_ = v3(pl.pos.x + c_ * 330, pl.pos.y + s_ * 330, pl.pos.z);
					v_->pl.pos = vp_; v_->pl.vel = v3(0, 0, 0); v_->pl.yaw = pl.yaw + 1.5708f; v_->prev_yaw = v_->pl.yaw; v_->hp = 40;
					a_->pl.pos = v3(vp_.x + cosf(v_->pl.yaw) * 70, vp_.y + sinf(v_->pl.yaw) * 70, vp_.z); a_->pl.vel = v3(0, 0, 0); a_->pl.yaw = v_->pl.yaw + 3.14159265f; a_->prev_yaw = a_->pl.yaw;
					bot_exec_start(cy_, gh_, 1); logp("exectest: bot %d executes bot %d (front, seen from the side)\n", cy_, gh_); }
				else if (!pc_ && cy_ >= 0) { Bot *a_ = &bots[cy_]; a_->pl.pos = v3(pl.pos.x + c_ * 75, pl.pos.y + s_ * 75, pl.pos.z); a_->pl.vel = v3(0, 0, 0); a_->pl.yaw = pl.yaw + 3.14159265f; a_->prev_yaw = a_->pl.yaw;
					bot_exec_start(cy_, -1, 1); logp("exectest: bot %d executes the player (front)\n", cy_); } } }
		if (skinview && !ga_on && bots_ok && nbot > 0 && pdead_t <= 0 && !paused && (sv_t += dt) > 2.5f) {   // b71 test (see skinview above)
			sv_t = 0; int n_ = 0; float c_ = cosf(pl.yaw), s_ = sinf(pl.yaw); char ln_[160]; int l_ = snprintf(ln_, sizeof(ln_), "skinview:");
			for (int j0 = 0; j0 < nbot && n_ < 3; j0++) { int j = (sv_n + j0) % nbot; Bot *b_ = &bots[j]; if (b_->state == BS_DEAD) continue;
				float side = (n_ - 1) * 95.f, dist = 260 + 40 * n_;
				b_->pl.pos = v3(pl.pos.x + c_ * dist - s_ * side, pl.pos.y + s_ * dist + c_ * side, pl.pos.z); b_->pl.vel = v3(0, 0, 0);
				b_->pl.yaw = pl.yaw + 3.14159265f + 0.35f * (n_ - 1); b_->prev_yaw = b_->pl.yaw; if (n_ == 1 && decapdemo) { dd_bot = j; dd_t = 0.8f; } n_++;
				if (l_ < (int)sizeof(ln_) - 40) l_ += snprintf(ln_ + l_, sizeof(ln_) - l_, " %s(%s%s)", chrt[b_->ct].name, chr_skin_name(chrt[b_->ct].name, chrt[b_->ct].skin), bot_masked(b_) ? ", masked" : ""); }
			sv_n += 3; pitch = 0; logp("%s\n", ln_); }
		if (decapdemo && dd_t > 0 && (dd_t -= dt) <= 0 && dd_bot >= 0 && dd_bot < nbot && bots[dd_bot].state != BS_DEAD) {   // b80 test (see decapdemo above)
			bot_hit_info(-1, vsub(bots[dd_bot].head, v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE))); bots_damage(dd_bot, 150, 1); logp("decapdemo: bot %d shot in the head\n", dd_bot); }
		{ NG_T0(); net_frame(dt); NG_T1(); NGS.frames++; }   // net: packets in, the room's keep-alives
		if (net_match_lost()) {   // net: the host is gone: back to the AD-HOC window (the alert says why)
			logp("net: leaving the match: %s\n", NUV.alert[0] ? NUV.alert : "the room is gone"); net_back_to_room(); amb_map_start(""); voice_stop_all(200); music_stop(200);
			menu_fe_load(); menu_fe_open(FE_NET); phase = PH_FRONT; grade_identity(); menu_music("bgm/lobby", 800); continue; }
		float lx = (pad.lx - 128) / 128.0f, ly = (pad.ly - 128) / 128.0f, rx = (pad.rx - 128) / 128.0f, ry = (pad.ry - 128) / 128.0f;
		if (fabsf(lx) < 0.15f) lx = 0; if (fabsf(ly) < 0.15f) ly = 0; if (fabsf(rx) < 0.15f) rx = 0; if (fabsf(ry) < 0.15f) ry = 0;
		unsigned pr = pad.buttons & ~old.buttons; old = pad;
		if ((pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_SELECT)) { logp("quit: START+SELECT (match)\n"); break; }
		pause_dt = dt;
		if (!autoplay && !paused && (pr & SCE_CTRL_START) && !(pad.buttons & SCE_CTRL_SELECT) && match_over_t <= 0 && !wr_on()) { paused = 1; menu_pause_open(); ui_sfx("ui/tab_on"); pr &= ~SCE_CTRL_START; }
		if (paused) {   // pause menu (the match stops underneath; OPTIONS opens the front-end's options page over it)
			unsigned mp = menu_buttons(pr); menu_touch_poll(dt);
			if (opts_open) { int r = menu_fe_update(&cfg, mp, menu_buttons(pad.buttons), dt); if (r == FE_APPLY) { cfg_check(&cfg); cfg_apply(); }
				if (MFE.screen != FE_OPTIONS) { opts_open = 0; menu_fe_unload(); cfg_save(); menu_pause_open();
					bot_mask_on = !cfg.mask_off && !ga_on; } }   // b96 (user: masks toggle on and off as the PC's): ENEMY MASKS changed in the pause menu applies at once
			else { int r = menu_pause_update(mp, dt);
				if (mp & MB_OK) ui_sfx("ui/click_basic"); else if (mp & (MB_UP | MB_DOWN)) ui_sfx("ui/mouse_over_basic");
				if (r == PAUSE_RESUME) { paused = 0; ui_sfx("ui/tab_off"); }
				else if (r == PAUSE_OPTIONS && menu_fe_load() >= 0) { opts_open = 1; menu_fe_open(FE_OPTIONS); MFE.sel[FE_OPTIONS] = 0; }
				else if (r == PAUSE_QUIT) { paused = 0; end_result = score_l > score_r ? 0 : score_l < score_r ? 1 : 2; match_over_t = 0.01f; match_t = 0; logp("  quit the match from the pause menu\n"); if (NG.on) nui_leave(); } }   // net: quitting leaves (host: closes) the room
			pr = 0; pad.buttons = 0; lx = ly = rx = ry = 0; dt = 0;
		}
		if (wr_on()) {   // b84: the dive and the waiting room (wroom.h): the world in the network view, the sim held
			if (wr_update(dt, paused ? 0 : menu_buttons(pr))) {   // the room is over: the match starts from the spawn
				world_net(0); bot_freeze = 0; pl.pos = wr_sp; pl.vel = v3(0, 0, 0); pl.yaw = wr_yaw; pitch = 0; map_t = 0; vis_round_start(); glClearColor(fog[0], fog[1], fog[2], 1);
				if (vm_ok) { WP.sw_to = wcur; weapon_finish_switch(); } }
			else { bot_freeze = 1; if (WR.phase != WR_DIVE) vis_round_start(); }
		}
		if (!wr_on()) map_t += dt;
		if (autoplay && !fly) {   // test run: show the respawn screen, the pause menu and the last-minute warning once each
			static float ap_pause_t;
			if (tour_i == 2 && map_t > 14 && map_t - dt <= 14 && pdead_t <= 0) { pdead_t = killcam_death_len(); php = 0; killer_bot = nbot > 2 ? 2 : -1; ev_play("ui/dead", NULL); logp("  test: respawn screen\n"); }
			if (tour_i == 3 && map_t > 15 && map_t - dt <= 15) { paused = 1; menu_pause_open(); ap_pause_t = 3; logp("  test: pause menu\n"); }
			if (paused) { ap_pause_t -= pause_dt; if (ap_pause_t <= 0) paused = 0; }
			if (tour_i == 4 && map_t > 10 && map_t - dt <= 10) { tw_t = 0; logp("  test: last-minute warning\n"); }
		}
		{ float L[3]; light_at(pl.pos.x, pl.pos.y, pl.pos.z + 20, L); float k = fminf(1, 3 * dt); for (int c = 0; c < 3; c++) vm_lit[c] += (L[c] - vm_lit[c]) * k; }
		int tk_call_pr = ttm_on && tm.ready[0] && !fly && ((pr & SCE_CTRL_SELECT) || ((autoplay || netauto) && ttm_ready_t > 2 && !ttm_placing)); if (tk_call_pr) pr &= ~SCE_CTRL_SELECT;   // TACHIKOMA READY: Select calls it in (ttm.h)
		static int sel_used; if (tk_call_pr) sel_used = 1; else if (!(pad.buttons & SCE_CTRL_SELECT)) sel_used = 0;   // (a Select press that called the tank shows no board)
		sel_board = !dev_fly && !paused && (pad.buttons & SCE_CTRL_SELECT) && !(pad.buttons & SCE_CTRL_START) && !sel_used;   // b76: hold Select = the scoreboard
		if (dev_fly && (pr & SCE_CTRL_SELECT)) { fly = !fly; if (!fly) { pl.pos = v3(pos[0], pos[1], pos[2] - PEYE); pl.vel = v3(0, 0, 0); } logp("mode: %s\n", fly ? "fly" : "walk"); }
		if (fly && (pr & SCE_CTRL_TRIANGLE)) lmon = !lmon;
		int in_skill = !fly && (pr & SCE_CTRL_RIGHT) && !(pad.buttons & SCE_CTRL_LTRIGGER);
		ng_skill_btn = net_client_on() && !fly && pdead_t <= 0 ? (in_skill ? 1 : 0) | ((pad.buttons & SCE_CTRL_RIGHT) && !(pad.buttons & SCE_CTRL_LTRIGGER) ? 2 : 0) : 0;   // net: a client's skill button goes to the host
		if (!fly && pdead_t <= 0 && !net_client_on() && !wr_on() && sk_button(0, in_skill, (pad.buttons & SCE_CTRL_RIGHT) && !(pad.buttons & SCE_CTRL_LTRIGGER), dt)) logp("  skill: %s\n", sk_state_name(0));   // operator skill (skills.h): D-pad right = Tier 1, hold = Tier 2; again = cancel / toggle
		if (fly && (pr & SCE_CTRL_SQUARE)) cull = !cull;
		if (fly && (pr & SCE_CTRL_RIGHT)) { sp0 = (sp0 + 1) % nspawn; pl.pos = v3(spawns[sp0].x, spawns[sp0].y, spawns[sp0].z + 5); pl.vel = v3(0,0,0); pl.yaw = spawns[sp0].yaw; }
		// D-pad down toggles sprint (one tap, no holding): it runs until the player stops moving forward, taps down again,
		// aims, fires, crouches (a tap while sprinting still slides) or dies
		static int sprint_tog; static float sprint_idle;
		int scope_dn = !fly && (pr & SCE_CTRL_DOWN) && wpn_scope_dpad((pad.buttons & SCE_CTRL_LTRIGGER) != 0); if (scope_dn) pr &= ~SCE_CTRL_DOWN;   // scoped: D-pad down = x8 / x12 (wpn.h), not sprint
		if (!fly && (pr & SCE_CTRL_DOWN)) { sprint_tog ^= 1; sprint_idle = 0; }
		if (fly || pdead_t > 0) sprint_tog = 0;
		int jump = (pr & SCE_CTRL_CROSS) != 0, sprint = sprint_tog;
		if (pr & (SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_CIRCLE)) sprint_tog = 0;   // this frame still counts as sprinting, so Circle slides
		{ static int crouch_tog;   // Circle: crouch (toggle); tapped while sprinting: slide. Sprinting or jumping stands up.
		  if (!fly && (pr & SCE_CTRL_CIRCLE)) crouch_tog ^= 1; if ((pr & SCE_CTRL_DOWN) || jump) crouch_tog = (pr & SCE_CTRL_CIRCLE) ? crouch_tog : 0; if (fly || pdead_t > 0) crouch_tog = 0;
		  pl.in_crouch = crouch_tog; }
		int trig = !fly && (pad.buttons & SCE_CTRL_RTRIGGER), aim = !fly && (pad.buttons & SCE_CTRL_LTRIGGER), want_reload = !fly && (pr & SCE_CTRL_SQUARE);
		// Triangle: swap primary / secondary; D-pad left: knife; D-pad up: grenade (hold to cook); L + D-pad right: next gun (armory test)
		int trig_pr = !fly && (pr & SCE_CTRL_RTRIGGER), in_swap = !fly && (pr & SCE_CTRL_TRIANGLE), in_knife = !fly && (pr & SCE_CTRL_LEFT), in_gren = !fly && (pr & SCE_CTRL_UP);
		int gren_held = !fly && (pad.buttons & SCE_CTRL_UP), in_cycle = !fly && (pr & SCE_CTRL_RIGHT) && (pad.buttons & SCE_CTRL_LTRIGGER), use_held = !fly && (pad.buttons & SCE_CTRL_SQUARE);
		int tk_deploy_pr = ttm_placing ? trig_pr : 0; if (ttm_placing) { trig = trig_pr = aim = want_reload = in_gren = gren_held = in_knife = 0; }   // placing the Tachikoma: R deploys it, the gun stays quiet (ttm.h)
		float mx = lx, my = -ly;
		if (sprint_tog) { if (my < 0.3f) { sprint_idle += dt; if (sprint_idle > 0.15f) sprint_tog = 0; } else sprint_idle = 0; }   // stopped moving forward: the sprint ends
		if (autoplay && audio_test_phase < 9) {
			audio_test_t += dt;
			// four output setups, 5 s each, the same melody twice in each; then the setup that is expected to be right
			static const float at[9] = { 0.0f, 0.6f, 5.0f, 5.6f, 10.0f, 10.6f, 15.0f, 15.6f, 20.0f };
			if (audio_test_phase < 9 && audio_test_t >= at[audio_test_phase]) {
				int ph = audio_test_phase;
				if (ph < 8 && (ph & 1) == 0) { audio_variant = ph / 2 + 1; logp("audio test setup %d: %s port, %s buffers\n", ph / 2 + 1, ph / 2 >= 2 ? "BGM" : "MAIN", (ph / 2) & 1 ? "uncached" : "static"); }
				if (ph & 1) { play("test_melody", 256); melody_again = 2.2f; }
				if (ph == 8) { audio_variant = 2; logp("audio test done (output errors %d, last 0x%08X); walking with setup 2\n", audio_out_err, audio_out_errcode); }
				audio_test_phase++;
			}
			if (melody_again > 0) { melody_again -= dt; if (melody_again <= 0) play("test_melody", 256); }
			if (burst_t > 0) { burst_t -= dt; burst_cd -= dt; while (burst_cd <= 0) { burst_cd += 0.08f; int id = play("wp_ar_seburoc30_fire_fp_outdoor_", 200); fade_others("wp_ar_seburoc30_fire_fp", id, 90); } }
			mx = my = 0; trig = 0;
		} else if (autoplay || netauto) {   // (net test: the same soldier, without the tour)
			tsec += dt;
			int fighting = 0;
			if (bots_ok) { // fight: turn to the nearest bot in sight and shoot it (the walk waits)
				V3 eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE); int tb = bots_visible_nearest(eye, 2600); V3 rc_ = eye; int rt_ = tb < 0 ? range_visible_nearest(eye, 7000, &rc_) : -1;
				if (tb >= 0 || rt_ >= 0) {
					V3 c = tb >= 0 ? bots[tb].chest : rc_; float dx = c.x - eye.x, dy = c.y - eye.y, dz = c.z - eye.z, hd = sqrtf(dx * dx + dy * dy);
					float wy = atan2f(dy, dx), wp = atan2f(dz, hd), ey = wrap_pi(wy - pl.yaw);
					pl.yaw += ey * fminf(1, 9 * dt); pitch += (wp - pitch) * fminf(1, 9 * dt);
					aim = 1; sprint = 0; mx = my = 0; jump = 0; trig = fabsf(ey) < 0.06f && fabsf(wp - pitch) < 0.06f && reload_t <= 0;
					{ static int tog; tog ^= 1; trig_pr = trig && tog; }   // semi-automatic weapons: one press every other frame
					// exercise the other weapons: knife up close, a grenade now and then, the secondary for a while
					if (hd < 230) in_knife = 1;
					else if (fight_t > ap_next_gren && hd > 700 && hd < 2400 && WP.gren_state == 0) { in_gren = 1; ap_gren_hold = 0.8f; ap_next_gren = fight_t + 8; }
					else if (fight_t > ap_next_swap) { in_swap = 1; ap_next_swap = fight_t + 7; }
					if (WSC->ammo == 0 && reload_t <= 0) want_reload = 1;
					fighting = 1; fight_t += dt;
				}
			}
			if (autoplay && map_t < TOUR_SPECT) {   // third-person camera on a bot; the player stands still (invulnerable) and shoots back
				if (!fighting) { mx = my = 0; sprint = 0; trig = 0; aim = 0; jump = 0; }
				if (WSC->ammo == 0 && reload_t <= 0) want_reload = 1;
			} else if (!fighting) {   // walk the navigation mesh to a random spawn point
				mx = 0; trig = 0; aim = 0; aw_step(&aw, &pl, &pitch, dt, &my, &sprint, &jump); pl.in_crouch = aw.crouch;
				if (WSC->ammo * 3 < WD->mag && reload_t <= 0) want_reload = 1;
			}
			if (w_probe_aim >= 0 && map_t > TOUR_SPECT) aim = w_probe_aim;   // render b43: the weapon probe holds hip / ADS (world.h w_probe_frame)
			if (decapdemo) { mx = my = 0; sprint = 0; trig = trig_pr = 0; aim = 0; jump = 0; in_knife = in_gren = in_swap = 0; }   // b80 test: stand and watch (decapdemo)
			if (knifeview && (kv_t += dt) > 2.5f) { kv_t = 0; in_knife = 1; }   // b87 test switch knifeview
			if (ledgesweep && map_t > 1) { static int swept = -1; if (swept != cur_map) { swept = cur_map; smf_logf = logp; ledge_sweep(&pl, MAPS[cur_map].stem); ledge_mesh_check(&pl, MAPS[cur_map].stem); } }   // b122 test switch ledgesweep (player.h)
			if (smf_test && smf_n && map_t > 2) {   // b87 test switch smftest: every special maneuver of the map in turn (player.h)
				static int si = -1, sk_started; static float st = 0, syaw = 0; static V3 plan; st += dt;
				{ static int swept = -1; if (swept != cur_map) { swept = cur_map; smf_logf = logp; smf_sweep(&pl, MAPS[cur_map].stem); } }   // (b109: first the sweep of every zone, planned only)
				if (si >= smf_n) si = -1;
				if (si < 0 || st > 3.0f) {
					if (si >= 0) { const SmfMan *m = &smf_m[si]; V3 e = vsub(pl.pos, plan); logp("smftest %2d: %s, start z %.0f, ended %.0f %.0f %.0f, planned end %.0f %.0f %.0f (off %.0f cm), grounded %d |%s\n", si, sk_started ? "ran" : "NOT STARTED",
						m->zz, pl.pos.x, pl.pos.y, pl.pos.z, plan.x, plan.y, plan.z, sqrtf(vdot(e, e)), pl.grounded, smf_dbg); (void)m; }
					si = si + 1 < smf_n ? si + 1 : 0; st = 0; sk_started = 0; const SmfMan *m = &smf_m[si];
					pl.pos = v3((m->zx[0] + m->zx[1] + m->zx[2] + m->zx[3]) / 4, (m->zy[0] + m->zy[1] + m->zy[2] + m->zy[3]) / 4, m->zz + 2); pl.vel = v3(0, 0, 0); pl.mantle_t = 0;
					float dx = m->p[3].x - m->p[0].x, dy = m->p[3].y - m->p[0].y; if (dx * dx + dy * dy < 900) { dx = m->p[2].x - m->p[0].x; dy = m->p[2].y - m->p[0].y; } syaw = atan2f(dy, dx); }
				pl.yaw = syaw; pl.in_crouch = 0; mx = 0; my = st > 0.3f && st < 0.45f ? 1 : 0; jump = st > 0.3f && st < 0.36f; trig = trig_pr = 0; sprint = 0; aim = 0; pitch = 0;
				if (pl.mantle_t > 0 && pl.mantle_kind == 3 && !sk_started) { sk_started = 1; plan = pl.m_to; }
			}
			{   // b109 test switch viewat.txt (with autoplay; the emulator's pictures of a place): lines "x y z yaw pitch seconds" (degrees) - the player stands there and looks, each spot in turn; the map does not change
				static struct { V3 p; float yaw, pitch, secs; int act; } VSP[24]; static int va_n = -1, vi = -1; static float vt;
				if (va_n < 0) { va_n = 0; FILE *vf = fopen(VDIR "viewat.txt", "rb"); if (vf) { char ln[160]; while (va_n < 24 && fgets(ln, sizeof(ln), vf)) { float v[7] = { 0 }; if (sscanf(ln, "%f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]) >= 6) { VSP[va_n].act = (int)v[6]; VSP[va_n].p = v3(v[0], v[1], v[2]); VSP[va_n].yaw = v[3] * 0.0174533f; VSP[va_n].pitch = v[4] * 0.0174533f; VSP[va_n].secs = v[5]; va_n++; } } fclose(vf); logp("viewat: %d spots (viewat.txt, b109)\n", va_n); } }
				viewat_on = va_n > 0;
				if (va_n > 0 && map_t > 2) { vt += dt; if (vi < 0 || vt > VSP[vi].secs) { vi = (vi + 1) % va_n; vt = 0; logp("viewat %d: %.0f %.0f %.0f yaw %.0f pitch %.0f\n", vi, VSP[vi].p.x, VSP[vi].p.y, VSP[vi].p.z, VSP[vi].yaw * 57.2958f, VSP[vi].pitch * 57.2958f); }
					pl.pos = VSP[vi].p; pl.vel = v3(0, 0, 0); pl.grounded = 1; pl.mantle_t = 0; pl.yaw = VSP[vi].yaw; pitch = VSP[vi].pitch; mx = my = 0; jump = 0; trig = trig_pr = 0; sprint = 0; aim = (VSP[vi].act & 1) != 0; php = 100; in_swap = in_knife = in_gren = 0; want_reload = 0;
					if ((VSP[vi].act & 4) && wcur != WS_SECONDARY && vt > 0.3f && vt < 0.4f) in_swap = 1;   /* (a 7th number: 1 aim, 2 an empty reload 1 s in, 4 the pistol) */
					if ((VSP[vi].act & 2) && vt > 1.6f && vt - dt <= 1.6f && reload_t <= 0) { WSC->ammo = 0; want_reload = 1; } }
			}
			if (seekview) {   // b117 test switch seekview (with autoplay): the player stands still and watches the Seeker Drone it released
				for (int o_ = 0; o_ < SK_MAXOBJ; o_++) if (SKO[o_].alive && SKO[o_].type == SKO_SEEKER && SKO[o_].owner == 0) {
					float dx = SKO[o_].cur.x - pl.pos.x, dy = SKO[o_].cur.y - pl.pos.y, dz = SKO[o_].cur.z + 25 - (pl.pos.z + P_EYE);
					pl.yaw = atan2f(dy, dx); pitch = atan2f(dz, sqrtf(dx * dx + dy * dy)); mx = my = 0; sprint = 0; trig = trig_pr = 0; aim = 0; jump = 0;
					{ static float lg_; lg_ -= dt; if (lg_ <= 0) { lg_ = 0.5f; int tg_ = SKO[o_].target; V3 tp_ = tg_ >= 0 ? sk_pos(tg_) : v3(0, 0, 0);
						logp("  seekview: drone %d t %.1f phase %d at %.0f %.0f %.0f, target %d at %.0f %.0f %.0f, heading for %.0f %.0f %.0f\n", o_, SKO[o_].t, SKO[o_].phase, SKO[o_].cur.x, SKO[o_].cur.y, SKO[o_].cur.z, tg_, tp_.x, tp_.y, tp_.z, SKO[o_].from.x, SKO[o_].from.y, SKO[o_].from.z); } }
					break; } }
			if (wo_view && WO.ok && WO.shown) {   // b88 test switch wobjview: look at the map's moving object (wobj.h bone 0) while it is out
				float dx = WO.pose[0].p[0] - pl.pos.x, dy = WO.pose[0].p[1] - pl.pos.y, dz = WO.pose[0].p[2] - (pl.pos.z + P_EYE);
				pl.yaw = atan2f(dy, dx); pitch = atan2f(dz, sqrtf(dx * dx + dy * dy)); if (pitch > 1.2f) pitch = 1.2f; mx = my = 0; sprint = 0; trig = trig_pr = 0; aim = 0; jump = 0; }
			if (wo_trainview && WO.ok) { pl.pos = v3(4420, -2600, -276); pl.vel = v3(0, 0, 0); pl.yaw = 1.40f; pitch = -0.03f; mx = my = 0; sprint = 0; trig = trig_pr = 0; aim = 0; jump = 0; }   // b87 test switch trainview (wobj.h)
			if (tuttank_test && !ap_finish && cfg.tut <= 0 && !tut_on() && TU.tour_k < TUT_TANK - 1 && map_t > 3) {   // b86 test switch: the Think Tank program at once
				TU.tour_k = TUT_TANK - 1; TU.intro_done = 1; TU.tour_budget = 150; TU.tour_spent = 0; tour_i = NMAPS - 1;
				if (tut_tour_next(&cfg, &want_map)) logp("tuttank: straight to the Think Tank program\n"); }
			if (autoplay && !ap_finish && !viewat_on && (map_t >= TOUR_MAP_T + elim_tour_extra() + (ttm_on ? 35 : 0) || now / 1e6f > TOUR_MAX || (restest && map_t > 6)) && (restest || !tut_tour_busy(map_t))) {   // (Terminal Conquest: 35 s more to see captures and a call-in)
				logp("TOUR %2d/%d %-14s load %.1f s, %ld tris, %d meshes, %d tex, %d spawns, nav %d | fps avg %.1f worst %.1f, %.0f meshes drawn, bots %.2f ms | walked %.0f m, reached %d, stuck %d, moved %d, fell %d | kills %d, bot shots %d hits %d, player deaths %d, bot stuck respawns %d\n",
					tour_i + 1, NMAPS, MAPS[cur_map].stem, m_load, world_tris, world_nmesh, world_ntex, nspawn, nnav, m_fcount ? m_fsum / m_fcount : 0, m_fcount ? m_fmin : 0, m_dframes ? (float)m_drawn / m_dframes : 0, m_bots_n ? m_bots_ms / m_bots_n : 0,
					m_dist, aw.reached, aw.stuck, aw.moved, m_falls, kills - m_kills0, bot_total_shots - m_shots0, bot_total_hits - m_hits0, pdeaths - m_pd0, bot_stuck_respawns - m_sr0);
				if (!restest && tour_i + 1 < NMAPS && now / 1e6f <= TOUR_MAX) want_map = TOUR[++tour_i];
				else if (!restest && tut_tour_next(&cfg, &want_map)) {}   // then each training program (tutorial.h)
				else {
					logp("tour finished: %d of %d maps in %.0f s\n", tour_i + 1, NMAPS, tsec); alog_probe_end();
					if (fcount) logp("all maps: avg %.1f fps, worst second %.1f fps\n", fsum / fcount, fmin);
					logp("weapon: %d shots, %d reloads; fell out of the map %d times\n", shots, reloads, falls);
					if (bots_ok) logp("combat: player hit bots %d times (%d headshots), %d kills; bots fired %d shots, hit the player %d times (%.0f damage, would have died %d times); %.0f s spent fighting; %d bot stuck respawns\n", phits, pheads, kills, bot_total_shots, bot_total_hits, dmg_taken, pdeaths, fight_t, bot_stuck_respawns);
					logp("bullet hits by surface: player-mesh %d, wood %d, metal %d, sand %d, glass %d, car %d, boxes %d, soft %d, concrete %d, missed %d\n", hits_by_mat[0], hits_by_mat[1], hits_by_mat[2], hits_by_mat[3], hits_by_mat[4], hits_by_mat[5], hits_by_mat[6], hits_by_mat[7], hits_by_mat[8], misses);
					logp("audio: output errors %d (last 0x%08X), limiter engaged on %d samples, %d grains, %d late (>30 ms), worst gap %d us, worst mix time %d us\n", audio_out_err, audio_out_errcode, audio_limited, audio_grains, audio_late, audio_worst_us, audio_mix_us);
					{ FILE *cf = fopen(VDIR "mix.raw", "wb"); if (cf) { fwrite(cap_buf, 4, cap_frames, cf); fclose(cf); logp("audio capture: %d frames written to mix.raw\n", cap_frames); } }
					log_mem(); ap_finish = 1; match_t = 0.01f; logp("autoplay: end of match screens next\n");
				}
			}
		}
		if (autoplay || netauto) { if (ap_gren_hold > 0) { ap_gren_hold -= dt; gren_held = 1; } }
		if (netauto && net_auto_match(map_t, match_over_t)) { end_result = score_l > score_r ? 0 : score_l < score_r ? 1 : 2; match_over_t = 0.01f; match_t = 0; nui_leave(); }   // net (test): leave / quit on cue
		if (!fly && pdead_t <= 0) {   // operator skill: R places / launches / dives while the skill aims; no gun while placing, diving, rushing or hacked
			if ((autoplay || (netauto && NG.on)) && map_t > TOUR_SPECT && !viewat_on) { int ap_p = 0, ap_f = 0; sk_autoplay(0, trig || aim, dt, &ap_p, &ap_f);
				if (ap_p && net_client_on()) { ng_skill_btn = 1; logp("  skill: press (to the host)\n"); } else if (ap_p && sk_press(0)) logp("  skill: %s\n", sk_state_name(0)); if (ap_f) trig_pr = 1; }
			if (trig_pr && net_client_on() && ng_sk_eats(0)) { ng_skill_fire = 1; trig = trig_pr = 0; }   // net: the host places / launches / dives
			else if (trig_pr && !net_client_on() && sk_fire(0)) trig = trig_pr = 0;
			if ((pr & SCE_CTRL_SQUARE) && sk_sync_try(0)) want_reload = 0;   // SkillSync: download a teammate's shared Tier 2
			if (!sk_can_fire(0)) { trig = trig_pr = 0; in_knife = in_gren = gren_held = 0; want_reload = 0; aim = 0; }
		}
		if (dem_on && dem_prog >= 0) want_reload = 0;   // Square at a bomb site plants / defuses instead
		{   // b99: the hands alone (hands.h): the gesture as the local player's skill starts, the device while planting; no gun meanwhile
			static int sk_was; int sa = sk_ok && sk_valid(0) && SKA[0].state == SKS_ACTIVE && !sk_hide_viewmodel(0);
			if (sa && !sk_was && pdead_t <= 0 && !fly) hv_skill(); sk_was = sa;
			float hpp = dem_on && dm.state == DS_LIVE && pdead_t <= 0 && !fly ? dem_prog : -1, hdf = dem_on && dm.state == DS_PLANTED && pdead_t <= 0 && !fly ? dem_prog : -1; int hpl = dem_on && dm.state == DS_PLANTED;
			if (hv_test && map_t > 14) { float c = fmodf(map_t, 14); int odd = (int)(map_t / 14) & 1;   // (test runs: a plant, then a defuse, every 14 s, for the screenshots)
				if (odd) { if (c < 6) hdf = c / 7 + 0.001f; } else if (c < 5) hpp = c / 5 + 0.001f; else if (c < 5.6f) hpl = 1; }
			hv_netjump(sk_ok && sk_valid(0) && sk_is_active(0, SK_CYBERDIVE) && pdead_t <= 0 && !fly, sqrtf(pl.vel.x * pl.vel.x + pl.vel.y * pl.vel.y), pl.grounded, dt);   // b121: the Net Jump's open hands
			if (hv_update(dt, hpp, hpl, hdf, pdead_t <= 0 && !fly) && vm_ok) {
				WSlot *w = WSC; if (w->c[CL_DRAW] >= 0) { anim_play(&w->a, w->c[CL_DRAW], 0.01f, 0.15f, 0); WP.draw_t = fminf(0.45f, w->len[CL_DRAW] * 0.7f); } }
			if (hv_on()) { trig = trig_pr = aim = 0; in_swap = in_knife = in_gren = gren_held = in_cycle = 0; want_reload = 0; }
		}
		if (tk_use_prog >= 0) want_reload = 0;           // ... and next to an enemy Think Tank it hacks it (thinktank.h)
		if (dem_on && (dm.state == DS_FREEZE || dem_prog > 0)) { mx = my = 0; sprint = 0; jump = 0; trig = trig_pr = aim = 0; in_swap = in_knife = in_gren = gren_held = in_cycle = 0; }
		if (elim_on && em.state == ES_FREEZE) { mx = my = 0; sprint = 0; jump = 0; trig = trig_pr = aim = 0; in_swap = in_knife = in_gren = gren_held = in_cycle = 0; }
		if (net_frozen()) { mx = my = 0; sprint = 0; jump = 0; trig = trig_pr = aim = 0; in_swap = in_knife = in_gren = gren_held = in_cycle = 0; }   // net: everyone waits until all players have loaded
		{ int ph_; float t_; if (sk_launcher_vm(0, &ph_, &t_)) sprint = 0; }   // b84: "Readying the missile launcher will disable your ability to sprint" (v2.0 DETAILS, EP16)
		if (wr_on()) { trig = trig_pr = aim = want_reload = 0; in_swap = in_knife = in_gren = gren_held = in_cycle = use_held = 0; if (WR.phase != WR_WAIT) { mx = my = 0; sprint = 0; jump = 0; rx = ry = 0; } }   // b84: the waiting room - walk and look only
		if (pdead_t > 0) { mx = my = 0; sprint = 0; trig = aim = jump = want_reload = 0; trig_pr = in_swap = in_knife = in_gren = gren_held = in_cycle = 0; }
		if (wst.on) { mx = my = 0; sprint = 0; trig = aim = jump = want_reload = 0; trig_pr = in_swap = in_knife = in_gren = gren_held = in_cycle = 0; }   // stealth kill in progress (wpn.h)
		if ((wst.on && wst.axe) || pgrab_bot >= 0) { pl.vel.x = pl.vel.y = 0; }   // b70: an execution holds both bodies where they are (no slide from a run)
		if (pgrab_bot >= 0) {   // b70: in a cyborg's grip (bots.h executions): no control, the view on the attacker's face
			mx = my = 0; sprint = 0; trig = aim = jump = want_reload = 0; trig_pr = in_swap = in_knife = in_gren = gren_held = in_cycle = use_held = 0; rx = ry = 0;
			Bot *ab_ = &bots[pgrab_bot]; pgrab_t += dt;
			if (pdead_t > 0 || ab_->state == BS_DEAD || !ab_->ex_on || pgrab_t > 4) pgrab_bot = -1;
			else { V3 h_ = ab_->head; float dx_ = h_.x - pl.pos.x, dy_ = h_.y - pl.pos.y, hd_ = sqrtf(dx_ * dx_ + dy_ * dy_), d_ = wrap_pi(atan2f(dy_, dx_) - pl.yaw);
				pl.yaw += d_ * fminf(1, 9 * dt); pitch += (fminf(0.5f, atan2f(h_.z - (pl.pos.z + PEYE), fmaxf(hd_, 20.f))) - pitch) * fminf(1, 6 * dt); } }
		if (tut_on()) tut_input(dt, pr, pad.buttons, &mx, &my, &rx, &ry, &sprint, &jump, &trig, &trig_pr, &aim, &want_reload, &in_swap, &in_knife, &in_gren, &gren_held, &in_cycle, &use_held);
		if (fly) {
			yaw -= rx * 2.2f * dt; pitch -= ry * 1.8f * dt;
			float spd = (pad.buttons & SCE_CTRL_CROSS) ? 3000 : 900;
			view(V, pos, yaw, pitch, fwd);
			float rg[3] = { sinf(yaw), -cosf(yaw), 0 };
			for (int i = 0; i < 3; i++) pos[i] += (fwd[i] * -ly + rg[i] * lx) * spd * dt;
			if (pad.buttons & SCE_CTRL_RTRIGGER) pos[2] += spd * dt;
			if (pad.buttons & SCE_CTRL_LTRIGGER) pos[2] -= spd * dt;
		} else {
			if (!autoplay) { float af = vm_ok && WD->ads_fov > 0 ? wpn_ads_fov(WD) : 51, zs = (tanf((59 + (af - 59) * ads) * 0.5f * 3.14159f / 180) / tanf(59 * 0.5f * 3.14159f / 180)); zs = fmaxf(af < 12 ? 0.17f : 0.3f, zs) * look_sens * sk_look_mul(0);   /* (b85: an EMP stun barely lets the view turn) */
				pl.yaw -= rx * 2.4f * dt * zs; pitch -= (invert_y ? -ry : ry) * 1.8f * dt * zs; }
			// b103 (user: "the megatech knife seems to glitch whenever you have it equipped and use the L button to attack"): L with the
			// knife (or a grenade) in the hand is not aiming - the knife's L is its second slash / stealth kill (its press, below). Until
			// b102 the held button also put the arms into the aiming base pose, which those models do not have (the slash clip held
			// as a pose, or no clip at all) and slowed the walk to the aiming pace.
			if (vm_ok && (WD->kind == WK_KNIFE || WD->kind == WK_GRENADE || WD->ads_fov <= 0)) aim = 0;
			pl.in_ads = aim; pl.speed_mul = vm_ok && (((WD->kind == WK_SEMI || WD->kind == WK_BURST) && !WD->sniper) || WD->kind == WK_KNIFE) ? 1.25f : 1.0f;   // footage: ~670 cm/s sprint with a pistol
			sk_move_input(0, &mx, &my, &sprint, &jump, dt); { float sm_ = sk_speed_mul(0, sprint && my > 0.3f); if (sm_ > 1) pl.speed_mul = sm_; else if (sm_ < 1) { pl.speed_mul *= sm_; sprint = 0; } }   /* (b85: EMP stun: slow, no sprint) */ pl.jump_mul = sk_jump_mul(0);   /* dive: the stick flies the ghost; b79 HYPER SPRINT: sprint x2 (2.2), running x1.3 (1.4), jumps higher */
			if (hs_lunge > 0) { hs_lunge -= dt; mx = 0; my = 1; sprint = 1; }   // b79: a hyper sprint's knife lunge (the slash closes the gap)
			float mdt_ = dt; net_client_quantize(&pl, &mx, &my, &mdt_);   // net: a client moves at the command's resolution (= what the host runs)
			{ CP_T0(tpp_); player_update(&pl, mx, my, sprint, jump, mdt_); CP_ADD(CP_PLAYER, tpp_); }   // perf: cpuprof.h accumulators
			ng_use_held = use_held; ga_use_held = use_held && pdead_t <= 0; net_client_after_move(&pl, mx, my, sprint, jump, mdt_, pitch, trig);   // net: the command and its result, for the host and the replays
			FT("input, menus, player movement");   // render: frame timer (frametime.h)
			if (tut_on()) tut_after_move(dt);   // tutorial gates, the ladder
			if (pl.events & PE_CROUCH) play("foley_co_sit_", 170);
			if (pl.events & PE_STAND) play("foley_co_stand_", 170);
			if (pl.events & PE_SLIDE) { char fn[40]; snprintf(fn, 40, "foley_co_slide_%s", mat_foot[floor_mat]); play(fn, 205); }
			if (pl.events & (PE_CLIMB | PE_VAULT)) snd_foley(0, "jump", floor_mat, NULL);
			if (vm_ok && (pl.events & (PE_CLIMB | PE_VAULT)) && !wst.on) anim_play(&VA, WSC->c[(pl.events & PE_CLIMB) ? CL_CLIMB : CL_VAULT], 0.14f, 0.25f, 0);   /* b59: longer blends (it popped in) */   // 1st-person climb / vault arms (weapon.h)
			total_time += dt; if (pl.grounded) ground_time += dt;
			if (pl.pos.z < col_zmin - 500 && nspawn) { falls++; m_falls++; logp("  fell out of the map at %.0f %.0f\n", pl.pos.x, pl.pos.y); pl.pos = v3(spawns[sp0].x, spawns[sp0].y, spawns[sp0].z + 5); pl.vel = v3(0,0,0); aw.np = 0; }
			// ---- weapon (weapon.h): switching, quick knife / grenade, reloads, firing; hits on bots are collected in ph[] and scored below
			float hspeed = sqrtf(pl.vel.x * pl.vel.x + pl.vel.y * pl.vel.y); m_dist += hspeed * dt / 100;
			if (sprint && my > 0.3f) aim = 0;
			typedef struct { int bi, head; float dmg; V3 p, d; int icon; } PHit; PHit ph[24]; int nph = 0, do_melee = 0;
			if (!vm_ok) { want_reload = 0; trig = trig_pr = 0; in_swap = in_knife = in_gren = in_cycle = gren_held = 0; }   // no viewmodel: no weapon
			if (ga_wpn_lock) { in_swap = in_cycle = in_gren = gren_held = 0; }   // b63 Ghost Assault: the cyborgs' blade, the ghosts' pistol only
			// b79 (Paz video EP19 5:09-5:14, the game's skill video): a hyper sprint puts the knife in the hand; the gun comes back after it
			// b80: the gun stays out in a hyper sprint (b79 drew the knife: the Paz video EP19 188.6 / 312.5 s shows the gun up in the
			// sprint pose for the whole charge, point-blank kills with it at 318 / 336 s); the knife button's reach and lunge stay
			{ int hs_ = sk_hs_tier(0) > 0 && pdead_t <= 0;
			  if (hs_ && !hs_was) logp("  hyper sprint: on, the gun stays out (b80)\n");
			  hs_was = hs_; if (!hs_) hs_lunge = 0; (void)hs_from; }
			if (vm_ok && pdead_t <= 0 && WP.sw_to < 0 && WP.quick_from < 0 && WP.gren_state == 0) {
				if (in_swap && wsl[WS_SECONDARY].ok) { weapon_begin_switch(wcur == WS_PRIMARY ? WS_SECONDARY : WS_PRIMARY, 0.2f); reload_t = 0; }
				else if (in_cycle) { weapon_cycle_primary(); reload_t = 0; }
				else if (in_knife && wsl[WS_KNIFE].ok) {   // quick melee: the knife comes up already slashing, then the gun returns
					WP.quick_from = wcur; wcur = WS_KNIFE; reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0;
					WSlot *k = WSC; anim_stop(&k->a); k->a.base = -1; anim_set_base(&k->a, k->c[CL_IDLE], 0, 0); anim_play(&k->a, k->c[CL_FIRE], 0.02f, 0.1f, 0);
					WP.quick_t = k->len[CL_FIRE] > 0.2f ? k->len[CL_FIRE] - 0.05f : 0.5f; do_melee = 1; snd_wfire(k->def >= 0 ? wdef[k->def].sh : "knife");   /* (b98: the melee weapon carried - the knife or the Double Katar) */
					{ int stv_ = wpn_stealth_target(pl.pos, pl.yaw, NULL); if (stv_ >= 0) { wpn_stealth_start(k, stv_, WP.quick_from, pl.pos, &pl.yaw); do_melee = 0; WP.quick_t = 99; } }   // stealth kill (wpn.h)
				} else if (in_gren && wsl[WS_GRENADE].ok && wsl[WS_GRENADE].ammo + wsl[WS_GRENADE].res > 0) {   // quick grenade: pull the pin (cook) while held, throw on release
					WP.quick_from = wcur; wcur = WS_GRENADE; reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0;
					WSlot *g = WSC; if (g->ammo <= 0) { g->ammo = 1; g->res--; }
					anim_stop(&g->a); g->a.base = -1; anim_set_base(&g->a, g->c[CL_IDLE], 0, 0); anim_play(&g->a, g->c[CL_COOK] >= 0 ? g->c[CL_COOK] : g->c[CL_DRAW], 0.02f, 0.1f, 0);
					WP.gren_state = 1; WP.cook_t = 0; WP.gren_thrown = 0; ev_play("weapon/grenade/common/pin_pull", NULL);
				}
			}
			if (WP.sw_to >= 0) { WP.sw_t -= dt; if (WP.sw_t <= 0) weapon_finish_switch(); }
			WP.draw_t -= dt;
			const WDef *D = WD; WSlot *W = WSC;
			int busy = WP.sw_to >= 0 || WP.draw_t > 0 || WP.quick_from >= 0 || pl.mantle_t > 0;   // (no shots / aiming while climbing or vaulting)
			int bolting = WP.bolt_pending || (W->c[CL_BOLT] >= 0 && W->a.act == W->c[CL_BOLT] && anim_busy(&W->a));
			{ static float ads_lin; float tg = aim && D->ads_fov > 0 && !busy && reload_t <= 0 && !bolting ? 1.f : 0.f;   // footage: ~0.2 s with a slow start
			  if (wpn_scope_tex(W, NULL) >= 0) ads_lin = tg = wpn_scope_target(aim, tg > 0, dt, scope_dn);   // sniper scope: after the aim time, in one frame; D-pad down = x8 / x12 (wpn.h)
			  { static float was_tg; if (tg != was_tg && D->ads_fov > 0 && vm_ok) snd_wev(D->sh, tg > 0 ? "aim_on" : "aim_off", 0); was_tg = tg; }
			  ads_lin += (tg > ads_lin ? 1 : -1) * fminf(fabsf(tg - ads_lin), dt / 0.2f); ads = ads_lin * ads_lin * (3 - 2 * ads_lin); }
			{ static float swy, swp; float ty = 0, tp = 0; wscope.on = wpn_scope_tex(W, NULL) >= 0 && ads > 0.5f;   // scoped: the view drifts slightly
			  if (wscope.on) { wscope.t += dt; wpn_scope_sway(wscope.t, &ty, &tp); } pl.yaw += ty - swy; pitch += tp - swp; swy = ty; swp = tp;
			  { static float py_, pp_; float dy_ = pl.yaw - py_, dp_ = pitch - pp_; py_ = pl.yaw; pp_ = pitch; while (dy_ > 3.14159f) dy_ -= 6.2831853f; while (dy_ < -3.14159f) dy_ += 6.2831853f;   // b52: the scope shadow follows the motion
			    float cy_ = cosf(pl.yaw), sy_ = sinf(pl.yaw), vs_ = pl.vel.x * sy_ - pl.vel.y * cy_, vf_ = pl.vel.x * cy_ + pl.vel.y * sy_;
			    if (wscope.on) wpn_scope_motion(dy_, dp_, vs_, vf_, sqrtf(pl.vel.x * pl.vel.x + pl.vel.y * pl.vel.y), wscope.t, dt); else wsc_ox = wsc_oy = 0; } }
			snd_aim = ads > 0.5f;
			fire_cd -= dt;
			// quick knife: back to the gun when the slash is over
			if (WP.quick_from >= 0 && wcur == WS_KNIFE) { WP.quick_t -= dt; if (WP.quick_t <= 0) { if (!autoplay && (pad.buttons & SCE_CTRL_LEFT)) WP.quick_from = -1; /* held: keep the knife out (wpn.h) */ else { WP.sw_to = WP.quick_from; WP.quick_from = -1; weapon_finish_switch(); } } }
			// grenade: cook -> throw (the grenade leaves the hand at 0.12 s) -> back to the gun
			if (WP.gren_state == 1) { WP.cook_t += dt; if ((!gren_held && WP.cook_t > 0.35f) || WP.cook_t > 2.6f) { WP.gren_state = 2; WP.gren_t = 0; anim_play(&VA, C_FIRE, 0.03f, 0.1f, 0); ev_play("weapon/grenade/common/throw", NULL);
					if (rand() % 2 == 0) snd_voice(pvoice, "grenadethrow_1st", NULL); } }
			else if (WP.gren_state == 2) {
				WP.gren_t += dt;
				if (!WP.gren_thrown && WP.gren_t >= 0.12f) {
					WP.gren_thrown = 1; W->ammo--;
					int gi = -1; for (int k = 0; k < NGREN; k++) if (!gren[k].on) { gi = k; break; }
					if (gi >= 0) {
						Grenade *g = &gren[gi]; float cy = cosf(pl.yaw), sy = sinf(pl.yaw), cp = cosf(pitch), sp_ = sinf(pitch);
						V3 f = v3(cy * cp, sy * cp, sp_), r = v3(sy, -cy, 0), u = vcross(r, f), eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE);
						V3 p0 = vadd(vadd(eye, vmul(f, 25)), vadd(vmul(r, 10), vmul(u, -6))); float h0 = raycast(eye, vmul(vsub(p0, eye), 1.f / 28), 28, NULL, NULL); if (h0 > 0) p0 = vadd(eye, vmul(f, fmaxf(2, h0 - 8)));
						V3 v0 = vadd(vadd(vmul(f, 1500), vmul(u, 260)), vmul(pl.vel, 0.6f));
						memset(g, 0, sizeof(*g)); g->on = 1; g->p[0] = p0.x; g->p[1] = p0.y; g->p[2] = p0.z; g->v[0] = v0.x; g->v[1] = v0.y; g->v[2] = v0.z; g->fuse = fmaxf(0.6f, 3.2f - WP.cook_t); g->spin = 9;
						MBone *pose = anim_pose(&W->a, -1); if (W->m.nb <= 64) memcpy(gren_pose, pose, sizeof(MBone) * W->m.nb);
						const float *gc = W->grip >= 0 ? gren_pose[W->grip].p : (const float[3]){ 0, 0, -30 };
						float M_[16] = { r.x, r.y, r.z, 0, u.x, u.y, u.z, 0, -f.x, -f.y, -f.z, 0, 0, 0, 0, 1 }; memcpy(gren_cam, M_, 64); gren_cam[12] = gc[0]; gren_cam[13] = gc[1]; gren_cam[14] = gc[2];   // rotation camera->world; [12..14] = pivot in camera space
						logp("  grenade thrown (cooked %.1f s, fuse %.1f s)\n", WP.cook_t, g->fuse); net_client_gren(g); net_host_gren_note(g);   // net: the host simulates it, the others see it
					}
				}
				if (WP.gren_t >= fmaxf(0.35f, W->len[CL_FIRE] - 0.05f)) { WP.gren_state = 0; WP.gren_thrown = 0; WP.sw_to = WP.quick_from; WP.quick_from = -1; weapon_finish_switch(); }
			}
			if (drop_demo && range_active() && vm_ok && (drop_demo_t += dt) > 3) { drop_demo_t = 0;   // b79 test switch dropdemo
				float f_[3] = { pl.pos.x + 350 * cosf(pl.yaw), pl.pos.y + 350 * sinf(pl.yaw), pl.pos.z }; wpn_drop_throw(WSC->def, f_, pl.yaw + 1.2f, v3(pl.pos.x - 200 * sinf(pl.yaw), pl.pos.y + 200 * cosf(pl.yaw), pl.pos.z), 0, 0);
				logp("  gun drop: demo throw (b79)\n"); }
			wpn_drops_update(dt, pl.pos, pl.yaw);   // dropped guns: hold Square to swap (wpn.h)
			if (vm_ok && pdead_t <= 0 && !busy && WP.gren_state == 0 && !wst.on && wpn_pick_input(use_held && !(dem_on && dem_prog >= 0), &want_reload, dt)) {
				int ns_ = wpn_pick_up(pl.pos, pl.yaw); if (ns_ >= 0) { reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0; WP.sw_to = ns_; weapon_finish_switch(); vm_ok = WSC->ok; logp("  picked up %s\n", WD->name); } }
			D = WD; W = WSC;
			if ((trig_pr || (trig && D->kind == WK_AUTO)) && W->ammo == 0 && W->res > 0 && D->kind != WK_KNIFE && D->kind != WK_GRENADE) want_reload = 1;   // the original reloads by itself when you fire on empty
			// reload (the 870 loads shell by shell: reload_start, reload_loop per shell, reload_end)
			if (!busy && want_reload && reload_t <= 0 && D->kind != WK_KNIFE && D->kind != WK_GRENADE && W->ammo < D->mag && W->res > 0) {
				WP.bolt_pending = 0;
				if (D->kind == WK_PUMP && W->c[CL_RL_START] >= 0) { WP.rl_phase = 1; anim_play(&VA, W->c[CL_RL_START], 0.1f, 0.05f, 0); reload_t = reload_len = fmaxf(0.1f, W->len[CL_RL_START]); reloads++; if (wsnd_play(D->sh, CL_RL_START, W->len[CL_RL_START]) < 0) snd_wev(D->sh, "reload_01", 0); }
				else {
					int c = W->ammo ? C_RELOAD : C_RELOAD_E; float CL_ = c >= 0 ? W->len[W->ammo ? CL_RELOAD : CL_RELOAD_E] : (W->ammo ? D->rl : D->rl_e);
					if (CL_ < 0.6f) CL_ = W->ammo ? D->rl : D->rl_e;
					// b107: the PC plays the clip in the weapon's reload time (wsnd.h: the long empty reloads 2 to 4 times faster)
					int rcl_ = W->ammo ? CL_RELOAD : CL_RELOAD_E; float T_ = c >= 0 ? wsnd_secs(D->sh, rcl_) : 0; if (!(T_ > 0.3f)) T_ = CL_;
					RELOAD_TIME = T_ * 0.78f;   // the magazine is in (and the gun usable) before the clip's settle tail: footage, M4A1 empty reload 2.05 s of 2.67
					if (c >= 0) { anim_play(&VA, c, 0.15f, 0.25f, 0); VA.act_rate = CL_ / T_; }
					reload_t = reload_len = RELOAD_TIME; reloads++;
					if (wsnd_play(D->sh, rcl_, T_) < 0) snd_wreload(D->sh, T_);   // (b107: the game's own schedule; the even spread without the table)
				}
			}
			if (D->kind == WK_PUMP && WP.rl_phase && trig_pr && W->ammo > 0 && WP.rl_phase != 3) {   // fire interrupts a shell reload
				WP.rl_phase = 3; anim_play(&VA, W->c[CL_RL_END], 0.05f, 0.1f, 0); reload_t = fmaxf(0.1f, W->len[CL_RL_END] * 0.7f); if (wsnd_play(D->sh, CL_RL_END, W->len[CL_RL_END]) < 0) snd_wev(D->sh, "reload_03", 0.1f); }
			if (WP.rl_phase) {
				if (reload_t > 0) { reload_t -= dt; if (reload_t <= 0) {
					if (WP.rl_phase == 2) { W->ammo++; W->res--; }
					if (WP.rl_phase <= 2 && W->ammo < D->mag && W->res > 0 && !trig) { WP.rl_phase = 2; anim_play(&VA, W->c[CL_RL_LOOP], 0.05f, 0.05f, 0); reload_t = fmaxf(0.2f, W->len[CL_RL_LOOP]); if (wsnd_play(D->sh, CL_RL_LOOP, W->len[CL_RL_LOOP]) < 0) snd_wev(D->sh, "reload_02", reload_t * 0.45f); }
					else if (WP.rl_phase <= 2) { WP.rl_phase = 3; anim_play(&VA, W->c[CL_RL_END], 0.05f, 0.15f, 0); reload_t = fmaxf(0.2f, W->len[CL_RL_END]); if (wsnd_play(D->sh, CL_RL_END, W->len[CL_RL_END]) < 0) snd_wev(D->sh, "reload_03", reload_t * 0.3f); }
					else { WP.rl_phase = 0; reload_t = 0; } } }
				else WP.rl_phase = 0;
			} else if (reload_t > 0) { reload_t -= dt; if (reload_t <= 0) { int need = D->mag - W->ammo; if (need > W->res) need = W->res; W->ammo += need; W->res -= need; } }
			// bolt action: the bolt is worked after each shot (the scope comes down meanwhile)
			if (WP.bolt_pending) { WP.bolt_t -= dt; if (WP.bolt_t <= 0) { WP.bolt_pending = 0; anim_play(&VA, W->c[CL_BOLT], 0.06f, 0.15f, 0);
				// b91: the bolt worked within the gun's shot interval (its v2.0 FIRE RATE): a long bolt clip (the SR-V97's) plays faster
				float avail_ = 60.f / (D->rpm > 1 ? D->rpm : 30) - fmaxf(0.2f, W->len[CL_FIRE] - 0.15f) - 0.05f; if (avail_ > 0.3f && W->len[CL_BOLT] > avail_) VA.act_rate = W->len[CL_BOLT] / avail_;
				if (wsnd_play(D->sh, CL_BOLT, W->len[CL_BOLT] / (VA.act_rate > 0 ? VA.act_rate : 1)) < 0) snd_wev(D->sh, "shot_reload", 0); } }   // (b107: the bolt's sound where the clip has it)
			int want_fire = D->kind == WK_AUTO || D->kind == WK_KNIFE ? trig : trig_pr;
			// b92: a burst gun (93-SE) fires three shots at its rate for one pull, finishing the burst after release; a short
			// pause before the next pull. Reloads, swaps and an empty magazine end it
			static int burst_left = 0; static const WDef *burst_d = NULL;
			if (D->kind != WK_BURST || burst_d != D || busy || reload_t > 0 || W->ammo <= 0) burst_left = 0;
			burst_d = D->kind == WK_BURST ? D : NULL;
			if (D->kind == WK_BURST) { if (trig_pr && burst_left == 0 && fire_cd <= 0) burst_left = 3; want_fire = burst_left > 0; }
			if (want_fire && !busy && reload_t <= 0 && WP.gren_state == 0 && D->kind != WK_GRENADE && !(sprint && my > 0.3f)) {
				if (W->ammo > 0 || D->kind == WK_KNIFE) { while (fire_cd <= 0 && (W->ammo > 0 || D->kind == WK_KNIFE) && (D->kind != WK_BURST || burst_left > 0)) {
					fire_cd += 60.0f / (D->rpm > 1 ? D->rpm : 600); if (D->kind != WK_KNIFE) W->ammo--; shots++;
					if (D->kind == WK_BURST && --burst_left == 0) fire_cd += 0.22f;   // (b92: the gap between bursts)
					if (vm_ok) anim_play(&VA, ads > 0.5f && C_FIRE_ADS >= 0 && !wscope.on ? C_FIRE_ADS : C_FIRE, 0.03f, 0.15f, 0);
					if (wscope.on) wscope.flash = 1;
					snd_wfire(D->sh);
					if (D->kind == WK_KNIFE) do_melee = 1;
					else { // trace the shot (or each pellet) from the eye along the view, with spread when not aiming
						float base = D->sniper ? D->spread * (1 - 0.97f * ads) : (D->spread + 0.03f * spread) * (1 - 0.85f * ads);
						V3 eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE); int snd = 0;
						pflash = 0.05f; fp_shot = 1;
						for (int pe = 0; pe < D->pellets; pe++) {
							float sp = D->pellets > 1 ? 0.045f * (1 - 0.25f * ads) + base * 0.3f : base, a_ = frand() * 6.2832f, r_ = D->pellets > 1 ? sqrtf(frand()) : 1;
							float yy = pl.yaw + (D->pellets > 1 ? cosf(a_) * r_ * sp : ((rand() % 2001) - 1000) / 1000.f * sp), pp = pitch + (D->pellets > 1 ? sinf(a_) * r_ * sp : ((rand() % 2001) - 1000) / 1000.f * sp);
							V3 dir = v3(cosf(yy) * cosf(pp), sinf(yy) * cosf(pp), sinf(pp)); net_client_pellet(dir);   // net: the ray goes to the host
							int mat = 0; V3 n; float dist = raycast(eye, dir, 15000, &mat, &n);
							float bt = 0; int bh = 0, bi = bots_ok ? bots_hit_test(eye, dir, dist > 0 ? dist : 15000, &bt, &bh) : -1;
							{ float lim_ = bi >= 0 ? bt : dist > 0 ? dist : 15000; if (sk_bullet(eye, dir, lim_, 0, bh ? D->dmg_h : D->dmg) < lim_) { bi = -1; dist = -1; } }   // stopped by an enemy shield / cyber wall / deployable (skills.h)
							{ float lim_ = bi >= 0 ? bt : dist > 0 ? dist : 15000, tt_ = 0; int ti_ = tk_ray(eye, dir, lim_, &tt_);   // an enemy Think Tank (thinktank.h)
							  if (ti_ >= 0) { V3 hp_ = vadd(eye, vmul(dir, tt_)), nn_ = vmul(dir, -1); tk_damage(ti_, bh ? D->dmg_h : D->dmg, 0, hp_, dir, SKD_BULLET); fx_impact_nodecal(2, &hp_.x, &nn_.x, &dir.x);
								hit_marker = 0.2f; if (!snd) { ev_play("impact/ui_tachikoma_dmg", &hp_.x); snd = 1; } bi = -1; dist = -1; } }
							if (D->pellets == 1 && (shots & 1) && (dist > 400 || dist <= 0)) {   // tracer from our own gun (footage: hip fire shows them)
								float cyw = cosf(pl.yaw), syw = sinf(pl.yaw), cpt = cosf(pitch), spt = sinf(pitch); V3 F_ = v3(cyw * cpt, syw * cpt, spt), R_ = v3(syw, -cyw, 0), U_ = vcross(R_, F_);
								V3 m0 = vadd(vadd(eye, vmul(F_, 70)), vadd(vmul(R_, 9 * (1 - ads)), vmul(U_, -7 + 4 * ads))), m1 = vadd(eye, vmul(dir, bi >= 0 ? bt : dist > 0 ? dist : 8000)); fx_tracer(&m0.x, &m1.x); }
							float rt_ = 0; int rh_ = 0, ri_ = range_hit_test(eye, dir, bi >= 0 ? bt : dist > 0 ? dist : 15000, &rt_, &rh_);
							if (ri_ >= 0) { int dead_ = range_damage(ri_, (rh_ ? D->dmg_h : D->dmg) * wdef_falloff(D, rt_), rh_, vadd(eye, vmul(dir, rt_)), dir); phits++; if (rh_) pheads++; hit_marker = 0.2f;
								if (dead_) kill_marker = 0.7f; else if (!snd) { ev_play(rh_ ? "impact/ui_headshot" : "impact/target_dmg", NULL); snd = 1; } }
							else if (bi >= 0) { if (nph < 24) { PHit *q = &ph[nph++]; q->bi = bi; q->head = bh; q->dmg = (bh ? D->dmg_h : D->dmg) * wdef_falloff(D, bt); q->p = vadd(eye, vmul(dir, bt)); q->d = dir; q->icon = D->icon; } }
							else if (dist > 0) {
								V3 hp = vadd(eye, vmul(dir, dist)); hits_by_mat[mat]++;
								if (!snd) { snd_whit(D->sh, mat, &hp.x); snd = 1; }
								if (pe < 3) fx_impact(mat, &hp.x, &n.x, &dir.x);
							} else misses++;
						}
						net_client_shot(eye, W->def);   // net: the shot's rays, judged on the host
						if (D->kind == WK_BOLT && W->c[CL_BOLT] >= 0) { WP.bolt_pending = 1; WP.bolt_t = fmaxf(0.2f, W->len[CL_FIRE] - 0.15f); }
						if (D->kind == WK_PUMP && wsnd_play(D->sh, CL_FIRE, W->len[CL_FIRE]) < 0) snd_wev(D->sh, "shot_reload", 0.38f);   // (b107: the pump where the fire clip has it - the KSG's at once, the 870's and the STA-12's 0.3 s in)
						if (shots % 2 == 0 || D->kind != WK_AUTO) snd_wshell(D->sh, floor_mat, 0.35f + (rand() % 20) / 100.f);
						kick = 1; float rk = D->recoil * (1 - 0.57f * ads); recoil += rk; pitch += rk; pl.yaw += ((rand() % 200) - 100) * 0.00004f * (D->kind == WK_AUTO ? 1 : 2.5f);   // footage: 0.0064 rad hip, 0.0026 aimed
						spread += D->kind == WK_AUTO ? 0.25f : 0.5f; if (spread > 1) spread = 1;
					}
					if (D->kind != WK_AUTO) break;
				} }
				else if (!firing_auto) { snd_wev(D->sh, "dryfire", 0); }
				firing_auto = 1;
			} else { firing_auto = 0; if (fire_cd < 0) fire_cd = 0; }
			if (vm_ok && wcur == WS_KNIFE && WP.quick_from < 0 && !wst.on && !busy && pdead_t <= 0 && !fly && (pr & SCE_CTRL_LTRIGGER) && !anim_busy(&VA)) {   // knife out: L (wpn.h)
				int fr_; int stv_ = wpn_stealth_target(pl.pos, pl.yaw, &fr_); ev_play("weapon/knife/swing_r", NULL);
				if (stv_ >= 0) wpn_stealth_start(W, stv_, -1, pl.pos, &pl.yaw);
				else { if (fr_ >= 0) wst_fail_t = 2.5f; anim_play(&VA, W->c[CL_FIRE2] >= 0 ? W->c[CL_FIRE2] : C_FIRE, 0.03f, 0.15f, 0); do_melee = W->c[CL_FIRE2] >= 0 ? 2 : 1; }
			}
			if (exectest && wst.on && wst.victim >= 0) { static float xdbg_t; if ((xdbg_t += dt) > 0.08f) { xdbg_t = 0; const Bot *vb_ = &bots[wst.victim];   // (b70 test: where the victim is)
				logp("  exectest t %.2f: victim %d %.0f cm away, grabbed %d front %d, state %d, cloak %.2f, yaw diff %.0f deg\n", wst.t, wst.victim, sqrtf((vb_->pl.pos.x - pl.pos.x) * (vb_->pl.pos.x - pl.pos.x) + (vb_->pl.pos.y - pl.pos.y) * (vb_->pl.pos.y - pl.pos.y)),
					vb_->grabbed, vb_->grab_front, vb_->state, sk_cloak(1 + wst.victim), wrap_pi(vb_->pl.yaw - pl.yaw) * 57.3f); } }
			{ int done_; int sv_ = wpn_stealth_update(dt, pl.pos, &pl.yaw, &pitch, &done_, &wsl[WS_KNIFE]);
			  if (wst_stab_ev && wst.victim >= 0) {   // b70: the arm blade goes in under the chin (IGN 253.25 s: blood, the head thrown back)
				Bot *vb_ = &bots[wst.victim]; V3 hp_ = vb_->head; hp_.z -= 14; V3 dd_ = vsub(hp_, v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE)); float l_ = sqrtf(vdot(dd_, dd_)); dd_ = vmul(dd_, 1 / (l_ + 1e-3f)); dd_.z += 0.5f;
				blood_hit(hp_, dd_); blood_hit(hp_, v3(dd_.x * 0.5f, dd_.y * 0.5f, 0.9f)); kick = fmaxf(kick, 0.9f);
				bot_cqc_voice(chrt[vb_->ct].name, 0, wst.front, &hp_.x); }
			  if (sv_ >= 0 && wst.axe) bot_cqc_voice(pvoice, 1, wst.front, NULL);
			  if (sv_ >= 0 && !wst.axe) { V3 cp_ = bots[sv_].chest, nn_ = v3(-cosf(pl.yaw), -sinf(pl.yaw), 0), dd_ = v3(cosf(pl.yaw), sinf(pl.yaw), 0); fx_spawn(FX_HIT_CYBORG, &cp_.x, &nn_.x, &dd_.x); kick = fmaxf(kick, 0.9f); }   // b63 (IGN footage: the kill's white burst)
			  if (sv_ >= 0 && nph < 24) { PHit *q = &ph[nph++]; q->bi = sv_; q->head = 0; q->dmg = 999; q->p = bots[sv_].chest; q->d = v3(cosf(pl.yaw), sinf(pl.yaw), 0); q->icon = wsl[WS_KNIFE].ok ? wdef[wsl[WS_KNIFE].def].icon : 51; logp("  %s on bot %d (%.0f cm away)\n", wst.axe ? (wst.front ? "execution (front)" : "execution (from behind)") : "stealth kill", sv_, sqrtf((bots[sv_].pl.pos.x - pl.pos.x) * (bots[sv_].pl.pos.x - pl.pos.x) + (bots[sv_].pl.pos.y - pl.pos.y) * (bots[sv_].pl.pos.y - pl.pos.y))); }
			  if (done_ && WP.quick_from >= 0) WP.quick_t = 0;
			  wst_prompt = vm_ok && !wst.on && pdead_t <= 0 && wsl[WS_KNIFE].ok && wpn_stealth_target(pl.pos, pl.yaw, NULL) >= 0; if (wst_fail_t > 0) wst_fail_t -= dt; }
			// melee: three short rays in a fan in front of the eye.
			// b63 (the user: "look and play as good as" IGN's "9 Minutes of Ghost Assault": there the blade sweeps across the view,
			// the hit lands as it passes - a white burst on the body - and a target in reach is hard to miss): the slash is judged
			// when the blade crosses the middle of the view (vm_knife's fire clip ~0.11 s, the second slash ~0.30 s), the view
			// turns onto an enemy in reach meanwhile (within 26 deg and 2.6 m, in sight: the Vita's sticks), whose reach is then
			// 2.5 m; the operator grunts (the game's voice/action/<op>/<op>_knife lines); a hit jolts the view
			static float mel_t = -1, mel_turn = 0; static int mel_tgt = -1;
			if (pdead_t > 0) mel_t = -1;
			// b98: the melee weapon's reach from its v2.0 RANGE (weapons.txt; the constants below are the MEGATECH KNIFE's 1.6 m; the DOUBLE KATAR 1.5 m)
			const float mel_rng = wsl[WS_KNIFE].def >= 0 && wdef[wsl[WS_KNIFE].def].rng_m > 0.5f ? wdef[wsl[WS_KNIFE].def].rng_m / 1.6f : 1;
			if (do_melee) {
				V3 eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE); net_client_melee(eye, pl.yaw, pitch, -1);   // net: the host judges the slash
				mel_t = do_melee == 2 ? 0.30f : 0.11f; mel_tgt = -1; mel_turn = 0; float best = 1e9f;
				for (int b = 0; bots_ok && b < nbot; b++) { Bot *bb = &bots[b]; if (bb->state == BS_DEAD || bb->team == 0) continue;
					V3 e = vsub(bb->chest, eye); float hd = sqrtf(e.x * e.x + e.y * e.y); if (hd > 260 * mel_rng * sk_hs_melee_mul(0) || fabsf(e.z) > 110) continue;   // (b79: a hyper sprint's knife reaches further)
					float da = atan2f(e.y, e.x) - pl.yaw; while (da > 3.14159265f) da -= 6.2831853f; while (da < -3.14159265f) da += 6.2831853f;
					if (fabsf(da) > 0.45f) continue; float sc = hd * (1 + 2 * fabsf(da)); if (sc < best && bot_los(eye, bb->chest)) { best = sc; mel_tgt = b; } }
				if (mel_tgt >= 0) { mel_turn = mel_t * 0.9f; if (sk_hs_tier(0)) { V3 e_ = vsub(bots[mel_tgt].chest, eye); if (e_.x * e_.x + e_.y * e_.y > 180 * 180) hs_lunge = 0.22f; } }   // b79: the lunge
				{ char vn[64]; snprintf(vn, sizeof(vn), "voice/action/%s/%s_knife", pvoice, pvoice); ev_play(vn, NULL); }
				do_melee = 0;
			}
			if (mel_t >= 0) {
				V3 eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE);
				if (mel_tgt >= 0 && mel_turn > 0 && bots[mel_tgt].state != BS_DEAD) { V3 e = vsub(bots[mel_tgt].chest, eye);
					float da = atan2f(e.y, e.x) - pl.yaw; while (da > 3.14159265f) da -= 6.2831853f; while (da < -3.14159265f) da += 6.2831853f;
					float k = fminf(1, dt / mel_turn), wp = atan2f(e.z, sqrtf(e.x * e.x + e.y * e.y)); pl.yaw += da * k; pitch += (wp - pitch) * k * 0.6f; mel_turn -= dt; }
				mel_t -= dt; if (mel_t < 0) do_melee = 1;
			}
			if (do_melee) {
				V3 eye = v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE); int hit = 0; const float reach = (mel_tgt >= 0 ? 250 : 220) * mel_rng * sk_hs_melee_mul(0);
				for (int k = 0; k < 3 && !hit; k++) { float yy = pl.yaw + (k == 0 ? 0 : k == 1 ? 0.3f : -0.3f); V3 dir = v3(cosf(yy) * cosf(pitch), sinf(yy) * cosf(pitch), sinf(pitch));
					float bt = 0; int bh = 0, bi = bots_ok ? bots_hit_test(eye, dir, reach, &bt, &bh) : -1;
					if (bi >= 0 && nph < 24) { PHit *q = &ph[nph++]; q->bi = bi; q->head = bh; q->dmg = wdef[wsl[WS_KNIFE].def].dmg; q->p = vadd(eye, vmul(dir, bt)); q->d = dir; q->icon = wdef[wsl[WS_KNIFE].def].icon; hit = 1; } }
				if (!hit) { V3 dir = v3(cosf(pl.yaw) * cosf(pitch), sinf(pl.yaw) * cosf(pitch), sinf(pitch)); int mat = 0; V3 n; float d_ = raycast(eye, dir, 200, &mat, &n);
					if (d_ > 0) { V3 hp = vadd(eye, vmul(dir, d_)); char sn[40]; static const char *KM[9] = { "stone", "wood", "metal", "dirt", "stone", "metal", "wood", "dirt", "stone" };
						(void)KM; (void)sn; { char en[40]; snprintf(en, 40, "impact/knife/%s", SND_MAT_IMPACT[mat < 9 ? mat : 0]); ev_play(en, &hp.x); } fx_impact(mat, &hp.x, &n.x, &dir.x); kick = fmaxf(kick, 0.35f); } }   // (b63: judged as the blade passes: the sound now, no extra delay)
			}
			// thrown grenades: bounce, then explode (damage falls off with distance, blocked by walls)
			for (int gi = 0; gi < NGREN; gi++) {
				Grenade *g = &gren[gi]; if (!g->on) continue;
				g->v[2] -= 980 * dt; V3 p = v3(g->p[0], g->p[1], g->p[2]), v = v3(g->v[0], g->v[1], g->v[2]), st = vmul(v, dt); float L = sqrtf(vdot(st, st));
				if (L > 0.01f) { V3 dir = vmul(st, 1 / L), n; int mat = 0; float h = raycast(p, dir, L + 5, &mat, &n);
					if (h > 0) { p = vadd(p, vmul(dir, fmaxf(0, h - 5))); float vn = vdot(v, n); v = vmul(vsub(v, vmul(n, 1.45f * vn)), 0.72f); g->spin *= 0.6f;
						float sp_ = sqrtf(vdot(v, v)); if (sp_ > 120) snd_gren_bounce(mat, sp_ > 700 ? 1.f : sp_ > 350 ? 0.5f : 0.f, &p.x);
						else if (n.z > 0.6f) { v.x *= 0.9f; v.y *= 0.9f; if (v.z < 0) v.z = 0; } }
					else p = vadd(p, st); }
				g->p[0] = p.x; g->p[1] = p.y; g->p[2] = p.z; g->v[0] = v.x; g->v[1] = v.y; g->v[2] = v.z; g->ang += g->spin * dt;
				g->fuse -= dt;
				if (g->fuse <= 0 || p.z < col_zmin - 500) {
					g->on = 0; V3 c = vadd(p, v3(0, 0, 15));
					{ float up_[3] = { 0, 0, 1 }; fx_explosion(&c.x, up_); }
					{ V3 gn; int gm = 0; float gd = raycast(c, v3(0, 0, -1), 80, &gm, &gn); if (gd > 0 && gn.z > 0.5f) { V3 gp = vadd(c, v3(0, 0, -gd)); fx_scorch(&gp.x, &gn.x); } }   // blast mark when it went off on the ground
					V3 eye = v3(pos[0], pos[1], pos[2]); V3 dd = vsub(c, eye); float de = sqrtf(vdot(dd, dd));
					snd_gren_exp(&c.x); if (!g->net_owner) { sk_blast(c, 650, 160, 0); tk_blast(c, 650, 160, 0); }
					if (de < 1500) { kick = fmaxf(kick, 1.5f * (1 - de / 1500)); hurt_flash = fmaxf(hurt_flash, 0.25f * (1 - de / 1500)); }
					const float R = 650;
					if (g->net_owner) { if (net_host_on()) net_host_blast(g->net_owner - 1, c, R); logp("  grenade of actor %d exploded at %.0f %.0f %.0f\n", g->net_owner - 1, c.x, c.y, c.z); continue; }   // net: another player's grenade (host: its damage)
					for (int b = 0; bots_ok && b < nbot; b++) { Bot *bb = &bots[b]; if (bb->state == BS_DEAD || bb->team == 0) continue;
						V3 e = vsub(bb->chest, c); float d_ = sqrtf(vdot(e, e)); if (d_ > R || !bot_los(c, bb->chest)) continue;
						if (nph < 24) { PHit *q = &ph[nph++]; q->bi = b; q->head = 0; q->dmg = 160 * powf(1 - d_ / R, 0.9f) + 10; q->p = bb->chest; q->d = vmul(e, 1 / (d_ + 1)); q->icon = wsl[WS_GRENADE].ok ? wdef[wsl[WS_GRENADE].def].icon : 41; } }
					{ V3 pc = v3(pl.pos.x, pl.pos.y, pl.pos.z + 100), e = vsub(pc, c); float d_ = sqrtf(vdot(e, e));
					  if (d_ < R && pdead_t <= 0 && bot_los(c, pc) && !net_client_on()) { self_dmg += 120 * powf(1 - d_ / R, 1.2f); self_dmg_dir = atan2f(-e.y, -e.x); } }   // (net client: the host deals it)
					if (range_blast(c, R, 160)) kill_marker = 0.7f;
					logp("  grenade exploded at %.0f %.0f %.0f\n", c.x, c.y, c.z);
				}
			}
			// the player's hits on bots this frame (bullets, pellets, knife, grenade)
			{ int snd_hit = 0;
			for (int h = 0; h < nph; h++) {
				PHit *q = &ph[h]; int bi = q->bi; if (bots[bi].state == BS_DEAD) continue;
				// b70 (Ghost Assault, the arm blade): the blow that would kill a ghost in reach is an execution (wpn.h)
				if (WICON_MELEE(q->icon) && q->dmg < 999 && ga_on && ga_player_cyborg() && !ga_cyborg(1 + bi) && wsl[WS_KNIFE].cy != 0 && wcur == WS_KNIFE && !wst.on && pdead_t <= 0 && !net_client_on()
				    && bots[bi].hp <= q->dmg && !bots[bi].grabbed) {
					int ex_ = wpn_exec_check(bi, pl.pos, pl.yaw); int taken_ = 0; for (int j = 0; j < nbot; j++) if (bots[j].ex_on && bots[j].ex_tgt == bi) taken_ = 1;
					if (ex_ && !taken_) { wpn_exec_start(&wsl[WS_KNIFE], bi, WP.quick_from, ex_ == 1, pl.pos, &pl.yaw); if (WP.quick_from >= 0) WP.quick_t = 99; mel_t = -1; pl.vel.x = pl.vel.y = 0;
						logp("  execution starts on bot %d (%s)\n", bi, ex_ == 1 ? "front" : "from behind"); continue; } }
				if (net_client_on()) { V3 nn = vmul(q->d, -1); fx_impact(9, &q->p.x, &nn.x, &q->d.x); if (q->dmg >= 999) net_client_melee(v3(pl.pos.x, pl.pos.y, pl.pos.z + PEYE), pl.yaw, pitch, bi); continue; }   // net: the host judges (its HIT / FEED events)
				bot_hit_info(WICON_MELEE(q->icon) || q->icon == 41 ? q->icon : -1, q->d);   // (bots.h: death / hit-reaction clip by weapon and direction)
				int killed = bots_damage(bi, q->dmg, q->head); phits++; if (q->head) pheads++;
				{ V3 nn = vmul(q->d, -1); blood_hit(q->p, q->d); if (q->head || WICON_MELEE(q->icon)) fx_spawn(FX_HIT_CYBORG, &q->p.x, &nn.x, &q->d.x); }   // (b63: a slash too - IGN footage, the white burst)
				if (WICON_MELEE(q->icon)) kick = fmaxf(kick, 0.6f);   // b63: the slash connects: the view jolts
				hit_marker = 0.2f;
				if (WICON_MELEE(q->icon)) ev_play("impact/knife/body", &q->p.x); else snd_whit(D->sh, 9, &q->p.x);
				if (killed) { kills++; kill_marker = 0.7f; ev_play(q->head ? "impact/ui_headshot" : "impact/ui_killshot", NULL); ev_play("impact/killshot_fp", NULL); if (q->head) ev_play("impact/headshot_fp", NULL); logp("  killed bot %d%s (%s)\n", bi, q->head ? " (headshot)" : "", q->icon == 41 ? "grenade" : q->icon == 51 ? "knife" : D->name);
					score_l += 100; streak = streak_t > 0 ? streak + 1 : 1; streak_t = 4; int pts = 100 + (q->head ? 50 : 0); intel += pts;
					notify = q->head ? "HEADSHOT" : streak == 2 ? "DOUBLE KILL" : streak == 3 ? "TRIPLE KILL" : streak >= 4 ? "MULTI KILL" : "NICE KILL"; notify_t = 0; notify_pts = pts;
					feed_wicon = q->icon; feed_add(feed, &nfeed, cfg.name, 1, bot_name(bi), -1); feed_wicon = 2; m_pkills++; my_kills_on[bi]++;
					if (streak >= 2) { ev_play("ui/kill_special", NULL); snd_killvoice(pvoice, "doublekill"); } else if (q->icon == 41) snd_killvoice(pvoice, "bombkill"); else if (bi == killer_bot) snd_killvoice(pvoice, "revenge"); }
				else if (!snd_hit) { if (q->head) { ev_play("impact/headshot_fp", NULL); ev_play("impact/ui_heavy", NULL); } else ev_play("impact/ui_common", NULL); }
				snd_hit = 1;
			} }
			kick *= expf(-18 * dt); spread *= expf(-4 * dt); { CP_T0(ta_); snd_update(dt); CP_ADD(CP_AUDIO, ta_); }
			{ CP_T0(tf_); fx_update(dt); bp_update(dt); CP_ADD(CP_FX, tf_); }   // (b80: bparts.h - the flying parts and the stumps' effects)
			FT("weapons, hits, sound, fx update");   // render: frame timer (frametime.h)
			{ float back = recoil * fminf(1, 30 * dt); recoil -= back; pitch -= back; }   // footage: the view is back within ~0.07 s, even during a burst
			// footsteps and landing
			if (pl.grounded) { stepd += hspeed * dt; bob += hspeed * dt * 0.012f; if (stepd > (pl.crouched ? 76 : hspeed > 450 ? 135 : hspeed > 280 ? 112 : 96) && pl.slide_t <= 0) { stepd = 0; snd_foley(0, hspeed > 450 ? "foot_fastrun" : hspeed > 280 ? "foot_run" : "foot_walk", floor_mat, NULL); } }
			if (!was_ground && pl.grounded && fall_speed < -300) snd_foley(0, hspeed > 450 ? "fastrun_land" : hspeed > 280 ? "run_land" : "land", floor_mat, NULL);
			if (jump && was_ground) { snd_foley(0, hspeed > 450 ? "fastrun_jump" : hspeed > 280 ? "run_jump" : "jump", floor_mat, NULL); if (vm_ok && !anim_busy(&VA)) anim_play(&VA, C_JUMP, 0.1f, 0.25f, 0); }
			if (!was_ground && pl.grounded && fall_speed < -300 && vm_ok && !anim_busy(&VA)) anim_play(&VA, C_LAND, 0.06f, 0.2f, 0);
			sk_vm_jt += dt; sk_vm_lt += dt; if (jump && was_ground) sk_vm_jt = 0; if (!was_ground && pl.grounded && fall_speed < -300) sk_vm_lt = 0;   // b109: the arm launcher's own jump / land clips (skills.h sk_draw_vm), as the guns'
			air_time = pl.grounded ? 0 : air_time + dt; sk_vm_at = air_time;
			if (vm_ok) { // base layer: what the arms do when no one-shot animation is playing
				int sprinting = sprint && my > 0.3f && pl.grounded && reload_t <= 0 && !trig;
				if (pl.slide_t > 0 && WSC->c[CL_SLIDE] >= 0) anim_set_base(&VA, trig && WSC->c[CL_SLIDE_FIRE] >= 0 ? WSC->c[CL_SLIDE_FIRE] : WSC->c[CL_SLIDE], 0.12f, 0);
				else if (!pl.grounded && air_time > 0.45f) anim_set_base(&VA, C_FALL, 0.3f, 0);
				else if (aim && reload_t <= 0) { if (hspeed > 60) anim_set_base(&VA, C_ADS_WALK, 0.15f, 0); else if (C_ADS_IDLE >= 0) anim_set_base(&VA, C_ADS_IDLE, 0.15f, 0); else anim_set_base(&VA, C_FIRE_ADS, 0.15f, 1); }
				else if (sprinting) anim_set_base(&VA, C_SPRINT, 0.25f, 0);
				else if (hspeed > 60) anim_set_base(&VA, C_RUN, 0.25f, 0);
				else anim_set_base(&VA, C_IDLE, 0.3f, 0);
				anim_update(&VA, dt);
				float k = fminf(1, 8 * dt); sway_x += ((autoplay ? 0 : -rx * 1.2f) - sway_x) * k; sway_y += ((autoplay ? 0 : ry * 0.8f) - sway_y) * k;
			}
			was_ground = pl.grounded; fall_speed = pl.vel.z < 0 ? pl.vel.z : 0; if (pl.grounded) fall_speed = 0; else fall_speed = pl.vel.z;
			yaw = pl.yaw; pos[0] = pl.pos.x; pos[1] = pl.pos.y; pos[2] = pl.pos.z + PEYE; net_client_camera(pos, dt);   // net: a correction eases out
		}
		if (!fly) { V3 sce_; if (sk_camera(0, &sce_)) { pos[0] = sce_.x; pos[1] = sce_.y; pos[2] = sce_.z; } }   // cyber dive: the ghost camera
		if (pitch > 1.5f) pitch = 1.5f; if (pitch < -1.5f) pitch = -1.5f;
		float vpitch = pitch + mantle_view_pitch(&pl); int spect = autoplay && map_t < TOUR_SPECT && bots_ok && nbot > 0 && !fly && !wr_on();
		if (spect) { // third-person camera orbiting one bot (switches when it has been dead a while)
			if (bots[spect_bot].state == BS_DEAD && bots[spect_bot].dead_t > 2.5f)
				for (int k = 1; k < nbot; k++) { int c = (spect_bot + k) % nbot; if (bots[c].state != BS_DEAD) { spect_bot = c; logp("  camera: now following bot %d\n", c); break; } }
			Bot *b = &bots[spect_bot]; spect_ang += 0.3f * dt;
			V3 tg = v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 110), cam = v3(tg.x + cosf(spect_ang) * 360, tg.y + sinf(spect_ang) * 360, tg.z + 70);
			V3 dd = vsub(cam, tg); float l = sqrtf(vdot(dd, dd)); V3 dn = vmul(dd, 1 / l); float h = raycast(tg, dn, l, NULL, NULL);
			if (h > 0 && h < 200) { // a wall right behind the camera: swing round to the most open side instead of ending up inside the bot
				float ba = spect_ang, bh = h;
				for (int k = 1; k < 8; k++) { float a = spect_ang + k * 0.785f; V3 c2 = v3(tg.x + cosf(a) * 360, tg.y + sinf(a) * 360, tg.z + 70), d2 = vsub(c2, tg);
					float l2 = sqrtf(vdot(d2, d2)), h2 = raycast(tg, vmul(d2, 1 / l2), l2, NULL, NULL); if (h2 < 0) h2 = l2; if (h2 > bh + 1) { bh = h2; ba = a; } if (h2 >= l2) break; }
				spect_ang = ba; cam = v3(tg.x + cosf(ba) * 360, tg.y + sinf(ba) * 360, tg.z + 70); dd = vsub(cam, tg); l = sqrtf(vdot(dd, dd)); dn = vmul(dd, 1 / l); h = raycast(tg, dn, l, NULL, NULL);
			}
			if (h > 0) cam = vadd(tg, vmul(dn, fmaxf(40, h - 25)));
			pos[0] = cam.x; pos[1] = cam.y; pos[2] = cam.z; V3 f = vsub(tg, cam); yaw = atan2f(f.y, f.x); vpitch = atan2f(f.z, sqrtf(f.x * f.x + f.y * f.y));
		}
		net_mirror_flags(); skw_frame(wr_on() ? 0 : dt);   // operator skills: actors, tiers, skills_update (skills.h); net: a client mirrors the host's skills and tanks (net_game.h)
		// ---- bots
		FT("footsteps, skills");   // perf: (the skills' frame and the player's sounds were counted with the bots)
		lis_pos = v3(pos[0], pos[1], pos[2]); lis_yaw = yaw; audio_listener(pos, yaw);
		net_host_pre(dt); net_client_pre(dt);   // net: remote players' commands (host), puppets from the snapshots and our own state (client)
		if (bots_ok) {
			BotTarget T = { pl.pos, pl.vel, !fly && pdead_t <= 0 && !skw_hidden_from_bots() }; /* camo, hacks: per bot through bot_sk_can_see (skills.h) */
			bots_update(wr_on() ? 0 : dt, &T);
			FT("bots AI");   // render: frame timer (frametime.h)
			if (self_dmg > 0) { bot_dmg_to_player += self_dmg; bot_dmg_dir = self_dmg_dir; self_dmg = 0; }   // our own grenade
			if (bot_dmg_to_player > 0 && pdead_t <= 0) {
				float ab = fminf(pap, bot_dmg_to_player * 0.6f); pap -= ab;   // armour (AP) takes most of the damage while it lasts
				dmg_taken += bot_dmg_to_player; pd_last_dmg = bot_dmg_to_player; php -= bot_dmg_to_player - ab; hurt_flash = fminf(1, hurt_flash + 0.3f); dmg_ind = 1.2f; dmg_dir = bot_dmg_dir; regen_cd = 4;
				{ int sl = 0; for (int k = 0; k < HUD_DMG; k++) { if (fabsf(wrap_pi(dmg_a[k] - bot_dmg_dir)) < 0.4f) { sl = k; break; } if (dmg_tt[k] < dmg_tt[sl]) sl = k; } dmg_a[sl] = bot_dmg_dir; dmg_tt[sl] = 1.4f; }
				if (dmg_snd_cd <= 0) { ev_play("impact/bullet_impact_fp", NULL); if (rand() % 3 == 0) snd_voice(pvoice, "damage_1st", NULL); dmg_snd_cd = 0.35f; }
				if (php <= 0 && net_client_on()) php = 1;   // net: the host says when we die (its FEED event)
				if (php <= 0) {
					pdeaths++;
					if (autoplay) { php = 100; logp("  player would have died (%d so far; autoplay is invulnerable)\n", pdeaths); }
					else { php = 0; pdead_t = killcam_death_len(); ev_play("ui/dead", NULL); snd_voice(pvoice, "death_1st", NULL); m_pdeaths++; killer_bot = bot_last_hitter; bot_assist_kill(0, killer_bot >= 0 ? 1 + killer_bot : -1); if (killer_bot >= 0 && killer_bot < NBOT_MAX) kills_on_me[killer_bot]++; music_ev("bgm/dead", 100); logp("  player died\n");
						score_r += 100; if (net_kill_hook) net_kill_hook(bot_last_hitter, -1, 0);
						killed_by_icon = bot_last_hitter >= 0 ? (bot_last_icon > 0 ? bot_last_icon : bot_wicon(bot_last_hitter)) : 2;   // b89: the killer's gun / knife (was always the M4A1's)
						{ int w0 = feed_wicon; feed_wicon = killed_by_icon; feed_add(feed, &nfeed, bot_last_hitter >= 0 ? bot_name(bot_last_hitter) : "ENEMY", -1, cfg.name, 1); feed_wicon = w0; } }
				}
			}
		}
		net_host_post(); net_client_post();   // net: lag-compensation history and snapshots (host), our input packet (client)
		range_update(dt, v3(pos[0], pos[1], pos[2]));
		tkg_frame(wr_on() ? 0 : dt, &pl, pitch, use_held, tk_call_pr, tk_deploy_pr, pdead_t <= 0 && !fly && php > 0, &end_result, &match_over_t, &pdead_t);   // Think Tanks, Terminal Conquest
		if (tut_cage_ntri) {   // b121: the Think Tank's cylinder opens with the hack (tut_cage_set above)
			int tk_ = tut_on() && TU.prog == TUT_TANK && TU.tank >= 0, op_ = tk_ && (tut_tank.hacking(TU.tank) >= 0 || tut_tank.open(TU.tank) || tut_tank.destroyed(TU.tank));   // (from the hack's START: EP14 7:04.5 - SUCCESS, the glass gone, the code still running at its second digit)
			if (op_) tut_cage_set(1); else if (tut_cage_is_open && tk_ && hypotf(pl.pos.x + 12950, pl.pos.y) > 680) tut_cage_set(0); }
		if (tut_on()) { int tr_ = tut_update(dt);   // training program steps; at its end the next program (full run) or the lobby
			if (tr_ == TR_NEXT) { int nm_ = tut_next_program(&cfg); if (nm_ >= 0) want_map = nm_; else { tut_exit_to_lobby(); continue; } }
			else if (tr_ == TR_EXIT) { tut_exit_to_lobby(); continue; } }
		// ---- Demolition round flow (dem.h): spawns, freeze, plant / defuse (player: hold Square at a site; bots at their stand point), bomb
		if (dem_on && !wr_on()) {   // (b84: held in the waiting room)
			int a0 = pdead_t <= 0 && !fly ? 1 : 0, a1 = 0, ncl_ = net_client_on();   // net: a client shows the host's round (no logic here)
			for (int i = 0; i < nbot; i++) if (bots[i].state != BS_DEAD) { if (bots[i].team == 0) a0++; else a1++; }
			if (!ncl_) dem_set_alive(&dm, a0, a1); bot_freeze = dm.state == DS_FREEZE;
			{ float e_[3] = { pl.pos.x, pl.pos.y, pl.pos.z + PEYE }, d_[3] = { cosf(pl.yaw) * cosf(pitch), sinf(pl.yaw) * cosf(pitch), sinf(pitch) };
			  if (ncl_) { DemMatch t_ = dm; dem_prog = pdead_t > 0 || fly ? -1 : dem_use(&t_, 0, 0, e_, d_, use_held, dt); }   // (on a copy: the prompt and bar; the host plants)
			  else dem_prog = pdead_t > 0 || fly ? -1 : dem_use(&dm, 0, 0, e_, d_, use_held, dt); }
			for (int i = 0; !ncl_ && i < nbot; i++) { Bot *b = &bots[i]; int att = dem_attacking(&dm, b->team); b->hold = 0;
				if (b->state == BS_DEAD) continue;
				if (b->netc) { float e[3] = { b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + (b->pl.eye > 1 ? b->pl.eye : P_EYE) }, d[3] = { cosf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->pl.yaw) * cosf(b->aim_pitch), sinf(b->aim_pitch) };   // net: a remote player plants / defuses with Square
					dem_use(&dm, 1 + i, b->team, e, d, b->net_use, dt); b->hold = dm.planter == 1 + i || dm.defuser == 1 + i; b->hold_yaw = b->pl.yaw; b->hold_pitch = b->aim_pitch; continue; }
				if (b->state == BS_ENGAGE) { if (dm.planter == 1 + i || dm.defuser == 1 + i) { float e0[3] = { 0, 0, -1e6f }, d0[3] = { 1, 0, 0 }; dem_use(&dm, 1 + i, b->team, e0, d0, 0, dt); } continue; }
				const float *aim = NULL;
				if (att && dm.state == DS_LIVE && dmap.nsite) { const DemSite *s = &dmap.site[dem_site_of[i] % dmap.nsite]; float dx = s->stand[0] - b->pl.pos.x, dy = s->stand[1] - b->pl.pos.y;
					if (dx * dx + dy * dy < 130 * 130 && fabsf(s->stand[2] - b->pl.pos.z) < 120) aim = s->aim; }
				else if (!att && dm.state == DS_PLANTED) { float dx = dm.bomb_pos[0] - b->pl.pos.x, dy = dm.bomb_pos[1] - b->pl.pos.y; if (dx * dx + dy * dy < 150 * 150) aim = dm.bomb_pos; }
				if (aim) { float e[3] = { b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + P_EYE }, d[3] = { aim[0] - e[0], aim[1] - e[1], aim[2] - e[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) + 1e-3f;
					for (int k = 0; k < 3; k++) d[k] /= l;
					if (dem_use(&dm, 1 + i, b->team, e, d, 1, dt) >= 0) { b->hold = 1; b->hold_yaw = atan2f(d[1], d[0]); b->hold_pitch = asinf(d[2]); } }
			}
			unsigned ev = ncl_ ? net_mode_take(1) : dem_update(&dm, dt); net_host_mode(1, ev); dem_game_frame(ev, dt);   // Aramaki's lines, timer warning (dem_game.h); net: the host's events
			if ((ev & DEV_RESET) && ncl_) {   // net client: the host placed everyone; here the round's fresh start (weapons, health, effects)
				sk_round_reset(); vis_round_start(); php = 100; pap = 100; pdead_t = 0; hurt_flash = 0; for (int k = 0; k < WS_N; k++) wslot_refill(k); reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0; WP.gren_state = 0; memset(gren, 0, sizeof(gren));
				if (vm_ok) { if (WP.quick_from >= 0) { WP.sw_to = WP.quick_from; WP.quick_from = -1; } else WP.sw_to = wcur; weapon_finish_switch(); }
				dem_band_t = dem_sw_t = 99; ev_play("ui/respawn", NULL); logp("  demolition: round %d (host), score %d - %d\n", dm.round, dm.score[0], dm.score[1]); }
			else if (ev & DEV_RESET) {   // everyone back to the attack / defense spawns, alive, frozen until START
				sk_round_reset();   // skills: deployables gone, active skills end (cooldowns keep running)
				vis_round_start();  // vision.h: round-start hologram
				const DemSpawn *sp = dem_spawn(&dmap, dem_attacking(&dm, 0) ? 0 : 1, 0);
				if (sp) { pl.pos = v3(sp->x, sp->y, sp->z + 5); pl.yaw = sp->yaw; } pl.vel = v3(0, 0, 0); pitch = 0;
				php = 100; pap = 100; pdead_t = 0; hurt_flash = 0; for (int k = 0; k < WS_N; k++) wslot_refill(k); reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0; WP.gren_state = 0; memset(gren, 0, sizeof(gren));
				if (vm_ok) { if (WP.quick_from >= 0) { WP.sw_to = WP.quick_from; WP.quick_from = -1; } else WP.sw_to = wcur; weapon_finish_switch(); }
				BotTarget T = { pl.pos, pl.vel, 1 }; int k0 = 1, k1 = 0;
				for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; bot_spawn(b, &T); const DemSpawn *s2 = dem_spawn(&dmap, dem_attacking(&dm, b->team) ? 0 : 1, b->team == 0 ? k0++ : k1++);
					if (s2) { b->pl.pos = v3(s2->x, s2->y, s2->z + 5); b->pl.yaw = s2->yaw; b->floor_z = s2->z; } b->npath = 0; b->repath_t = 0; b->hold = 0; b->tgt = -2; }
				dem_band_t = dem_sw_t = 99; ev_play("ui/respawn", NULL);
				logp("  demolition: round %d, %s, score %d - %d\n", dm.round, dem_attacking(&dm, 0) ? "attacking" : "defending", dm.score[0], dm.score[1]);
			}
			if (ev & DEV_BANNER) { dem_banner_t = 0; ev_play("ui/game_start_dm_text", NULL); int a = dem_attacking(&dm, 0);
				if (dm.round == 1) radio_play(a ? "voice/donut/dm/attack_start" : "voice/donut/dm/defense_start");
				else if (dm.round == dm.switch_after + 1) radio_play(a ? "voice/donut/dm/defense_switchtoattack" : "voice/donut/dm/attack_switchtodefense"); }
			if (ev & DEV_START) { dem_start_t = 0; ev_play("ui/game_start_dm_start", NULL); }
			if (ev & DEV_PLANT_BEGIN) { if (dm.planter == 0) ev_play("ui/bomb_plant", NULL); else if (dm.planter > 0 && dm.planter <= nbot) ev_play("foley/dm/bomb_plant_start", &bots[dm.planter - 1].pl.pos.x); }
			if (ev & DEV_DEFUSE_BEGIN) { if (dm.defuser == 0) ev_play("ui/bomb_deactivate", NULL); else ev_play("foley/dm/bomb_deactivate_start", dm.bomb_pos); }
			if (ev & DEV_PLANT_STOP) fade_others("ui_plant_bomb", -1, 120);
			if (ev & DEV_DEFUSE_STOP) fade_others("ui_deactivate_bomb", -1, 120);
			if (ev & DEV_PLANTED) { ev_play("foley/dm/bomb_plant_done", dm.bomb_pos); radio_play(dem_attacking(&dm, 0) ? "voice/donut/dm/plantbomb_team" : "voice/donut/dm/plantbomb_enemy");
				music_ev("bgm/bomb_count", 300); logp("  demolition: bomb planted at site %c\n", dmap.site[dm.bomb_site].letter); }
			if (ev & DEV_DEFUSED) { ev_play("foley/dm/bomb_deactivate_done", dm.bomb_pos); logp("  demolition: bomb defused\n"); }
			if (ev & (DEV_BEEP1 | DEV_BEEP2 | DEV_BEEP3)) bot_sound(ev & DEV_BEEP3 ? "ui_bomb_count_03" : ev & DEV_BEEP2 ? "ui_bomb_count_02" : "ui_bomb_count_01", v3(dm.bomb_pos[0], dm.bomb_pos[1], dm.bomb_pos[2]), 215, 7000);
			if (ev & DEV_EXPLODE) {   // the bomb: everything near it dies
				V3 c = v3(dm.bomb_pos[0], dm.bomb_pos[1], dm.bomb_pos[2]); { float up_[3] = { 0, 0, 1 }; fx_explosion(&c.x, up_); fx_explosion(&c.x, up_); V3 gn; int gm = 0; float gd = raycast(vadd(c, v3(0, 0, 30)), v3(0, 0, -1), 120, &gm, &gn); if (gd > 0 && gn.z > 0.5f) { V3 gp = vadd(c, v3(0, 0, 30 - gd)); fx_scorch(&gp.x, &gn.x); } }
				ev_play("impact/bomb_exp", &c.x); V3 de = vsub(c, v3(pos[0], pos[1], pos[2])); float dd = sqrtf(vdot(de, de)); if (dd < 3000) { kick = fmaxf(kick, 2.5f * (1 - dd / 3000)); hurt_flash = fmaxf(hurt_flash, 0.5f * (1 - dd / 3000)); }
				for (int i = 0; !ncl_ && i < nbot; i++) if (bots[i].state != BS_DEAD) { V3 e = vsub(bots[i].chest, c); if (vdot(e, e) < 1100.f * 1100.f) bot_hurt(i, 500, 0, -2); }   // (net client: the host deals it)
				{ V3 e = vsub(v3(pl.pos.x, pl.pos.y, pl.pos.z + 100), c); if (vdot(e, e) < 1100.f * 1100.f && pdead_t <= 0 && !autoplay && !ncl_) { self_dmg += 500; self_dmg_dir = atan2f(-e.y, -e.x); } }
				logp("  demolition: bomb exploded\n"); }
			if (ev & DEV_ROUND_END) { dem_band_t = 0; dem_band_win = dm.last_winner == 0; fade_others("ui_plant_bomb", -1, 100); fade_others("ui_deactivate_bomb", -1, 100);
				music_ev(dem_band_win ? "bgm/win" : "bgm/lose", 300); ev_play("ui/round_end", NULL);
				logp("  demolition: round %d to team %d (%s), %d - %d\n", dm.round, dm.last_winner, DEM_REASON[dm.last_reason % 6], dm.score[0], dm.score[1]); }
			if (ev & DEV_SWITCH) { dem_sw_t = 0; ev_play("ui/round_change", NULL); }
			if (ev & DEV_INTRO_MUSIC) music_ev("bgm/in_game_dm_01_round", 300);
			if (ev & DEV_ONE_LEFT_0) radio_play("voice/donut/dm/oneleft_self");
			if (ev & DEV_ONE_LEFT_1) radio_play("voice/donut/dm/oneleft_enemy");
			if (ev & DEV_MATCH_END) { end_result = dm.score[0] > dm.score[1] ? 0 : dm.score[0] < dm.score[1] ? 1 : 2;
				radio_play(end_result == 0 ? "voice/donut/dm/common_game_win" : end_result == 1 ? "voice/donut/dm/common_game_lose" : "voice/donut/dm/common_game_draw");
				music_ev(end_result == 0 ? "bgm/win" : end_result == 1 ? "bgm/lose" : "bgm/even", 200); match_over_t = 8; logp("  demolition: match over %d - %d\n", dm.score[0], dm.score[1]); }
		}
		{ float od_ = scoreboard_hold_dt(dt); dem_banner_t += od_; dem_band_t += od_; dem_sw_t += od_; dem_start_t += od_; }   // perf (b38): Demolition's screens wait under the held scoreboard (hud_match.h)
		if (!wr_on()) elim_frame(dt, pr);   // Elimination round flow, zones, spectating (elim_game.h)
		// (the operator skill timers live in skills.h now; cool_t / camo_t are unused)
		// match flow (until the menus take over): announcer, last-minute music, end of match after 10 minutes
		if (map_audio_pending && !wr_on()) { map_audio_pending = 0; amb_map_start(MAPS[cur_map].stem); snd_set_ingame(1);   // b59: the in-match mix (audio.h)
			if (!range_active() && !dem_on && !elim_on && !ttm_on) { vis_round_start(); music_ev("bgm/in_game_tdm_01_ready", 300); radio_play("voice/donut/tdm/gamestart"); ev_play("ui/game_start_tdm_text", NULL); ev_play_later("ui/game_start_tdm_start", NULL, 3.9f); }
			else if (ttm_on) vis_round_start();   // Terminal Conquest: ttm.h has its own start cues
			else if (dem_on) music_ev("bgm/in_game_dm_01_ready", 300); }
		if (score_limit > 0 && match_t > 0.01f && match_over_t <= 0 && (score_l >= score_limit * 100 || score_r >= score_limit * 100)) { match_t = 0.01f; logp("  score limit reached\n"); }
		if (match_t > 60 && match_t - dt <= 60) { tw_t = 0; ev_play("ui/crisis", NULL); }
		tw_t += scoreboard_hold_dt(dt);   // perf (b38): the last-minute warning waits under the held scoreboard (hud_match.h)
		if (match_t > 40 && match_t - dt <= 40) music_ev("bgm/tdm_end_count", 600);
		if (match_t > 20 && match_t - dt <= 20) radio_play("voice/donut/tdm/20sec");
		if (match_t > 0 && match_t - dt <= 0) { int win = score_l > score_r, draw = score_l == score_r;
			ev_play("ui/in_game_end", NULL); music_ev(draw ? "bgm/even" : win ? "bgm/win" : "bgm/lose", 200);
			radio_play_at(draw ? "voice/donut/tdm/gamedraw" : win ? "voice/donut/tdm/gamewin" : "voice/donut/tdm/gamelose", 2.1f); match_over_t = 8; end_result = draw ? 2 : win ? 0 : 1; logp("  match over: %d - %d\n", score_l, score_r); }
		if (match_over_t > 0) { match_over_t -= dt; if (match_over_t <= 0) {
			if (autoplay && !ap_finish) { match_t = 600; score_l = score_r = 0; intel = 0; nfeed = 0; map_audio_pending = 1; }
			else if (tut_on()) tut_exit_to_lobby();   // a training program left from the pause menu
			else {   // the results screens (front-end pack), with the scoreboard of this match
				MenuResults R; memset(&R, 0, sizeof(R)); R.result = end_result; R.score[0] = ttm_on ? tm.score[0] : dem_on ? dm.score[0] : elim_on ? em.score[0] : score_l / 100; R.score[1] = ttm_on ? tm.score[1] : dem_on ? dm.score[1] : elim_on ? em.score[1] : score_r / 100; R.mode = cfg.mode; R.map = cur_map;
				R.play_time = (match_len > 0 ? match_len : 600) - match_t; if (R.play_time < 0) R.play_time = 0;
				R.gp[0] = m_pkills * 10; R.gp[1] = intel / 20; R.gp[2] = end_result == 0 ? 50 : 10; R.xp[0] = m_pkills * 25; R.xp[1] = intel / 10; R.xp[2] = end_result == 0 ? 200 : 60;
				cfg.exp += R.xp[0] + R.xp[1] + R.xp[2]; while (cfg.exp >= cfg.exp_next) { cfg.exp -= cfg.exp_next; cfg.level++; cfg.exp_next += 250; }
				R.level = cfg.level; R.exp = cfg.exp; R.exp_next = cfg.exp_next;
				#define OBJ_OF(id) (dem_on ? (int)dm.obj_n[(id) & 15] : ttm_on && (id) < TTM_NACT ? (int)tm.obj_n[id] : 0)   // b58: objectives per player id (0 you, 1 + i bot i)
				MenuPlayer *me = &R.p[R.nplayers++]; snprintf(me->name, 24, "%s", cfg.name); me->team = 0; me->chr = cfg.chr; me->kills = m_pkills; me->deaths = m_pdeaths; me->assists = bot_player_assists; me->score = m_pkills * 100 + intel / 2; me->me = 1; snprintf(me->wpn, sizeof(me->wpn), "%s", MENU_WPN[cfg.primary].sh);   // ui b50: the loadout's primary (TOP OPERATIVE's gun)
				me->obj = OBJ_OF(0); me->skill = sk_uses[0];
				for (int i = 0; i < nbot && R.nplayers < 12; i++) { MenuPlayer *q = &R.p[R.nplayers++]; snprintf(q->name, 24, "%s", bot_name(i)); q->team = bots[i].team; q->chr = op_of_chrt(bots[i].ct); if (q->chr < 0) q->chr = 0;
					q->kills = bots[i].kills; q->deaths = bots[i].deaths; q->assists = bots[i].assists; snprintf(q->wpn, sizeof(q->wpn), "%s", bot_weapon(i));   // ui b50: the gun the bot used
					q->obj = OBJ_OF(1 + i); q->skill = 1 + i < SK_MAXACT ? sk_uses[1 + i] : 0;
					q->score = NG.on ? bots[i].net_score : bots[i].kills * 100 + q->obj * 250 + q->skill * 25; }   // b58: objectives and skills count for the bots as intel does for you
				#undef OBJ_OF
				menu_career(&cfg, &R); if (!autoplay) cfg_save();   // b58: awards, missions, achievements (saved with the settings)
				logp("  results: missions %d, achievements +%d (%d of %d), awards %d %d %d\n", R.mission_n, R.nach_new, mbits(R.ach), MENU_NACH, R.award[0], R.award[1], R.award[2]);
				amb_map_start(""); menu_music("bgm/result", 500); ui_sfx("ui/result_victory_defeat");
				net_match_end(); menu_fe_load(); menu_results_open(&R); phase = PH_RESULTS; grade_identity(); logp("  results: %s %d - %d, %d kills, %d deaths\n", end_result == 0 ? "victory" : end_result == 1 ? "defeat" : "draw", R.score[0], R.score[1], m_pkills, m_pdeaths);
			} } }
		{ static int seen_assists; if (bot_player_assists < seen_assists) seen_assists = bot_player_assists;   // (a new match reset them)
		  if (bot_player_assists > seen_assists) { seen_assists = bot_player_assists; intel += 30;   // b82 (Batou footage 3:10, 1:31): "ASSIST +30" when someone else finishes a target the player hurt
		    if (!(notify && notify_t < 0.6f)) { notify = "ASSIST"; notify_t = 0; notify_pts = 30; } } }
		streak_t -= dt; notify_t += hud_notify_hold_dt(dt);   /* qa (b42 fix): a hidden kill notify waits (hud_game.h) */ if (match_t > 0 && (match_len > 0 || autoplay) && (!range_active() || ap_finish) && !wr_on()) match_t -= dt;
		for (int k = 0; k < HUD_DMG; k++) dmg_tt[k] -= dt; for (int k = 0; k < nfeed; k++) feed[k].t += dt;
		while (nfeed > 0 && feed[0].t > 7) { memmove(feed, feed + 1, sizeof(HudKill) * (nfeed - 1)); nfeed--; }
		for (int i = 0; i < nbot && i < NBOT_MAX; i++) { if (bots[i].flash_t > 0) bot_ping[i] = 1.5f; else bot_ping[i] -= dt; if (ga_on && GA.ping_show[1 + i] > 0 && bot_ping[i] < 0.2f) bot_ping[i] = 0.2f; }   // (b63 Ghost Assault: a ghost's ping shows on the cyborg's radar too)
		hurt_flash *= expf(-2.5f * dt); spawn_glow_t += dt; dmg_ind -= dt; hit_marker -= dt; kill_marker -= dt; pflash -= dt; dmg_snd_cd -= dt; regen_cd -= dt;
		{ float hm_ = ga_on ? p_hp_max_ga : 100; if (regen_cd <= 0 && php > 0 && php < hm_) { php += 15 * dt * hm_ / 100; if (php > hm_) php = hm_; } }   // (b63 Ghost Assault: the cyborgs' 200)
		if (autoplay && vm_ok && WSC->res < WD->mag) WSC->res = WD->res;
		if (vm_ok && range_refill(&WSC->ammo, &WSC->res, WD->mag)) logp("  range: ammo refilled (%d in reserve)\n", WSC->res);
		// b72: the player's own body in third person from the moment of death (pbody.h, killcam.h; PC footage v49 433-449 s)
		{ static float pd_prev_; if (pdead_t > 0 && pd_prev_ <= 0) { killcam_death(pdead_t);
			int ct_ = ga_on && ga_player_cyborg() && ga_cyb_ct >= 0 ? ga_cyb_ct : ct_of_op(cfg.chr); float eye_[3] = { pos[0], pos[1], pos[2] };
			if (!tut_on() && bots_ok && pbody_start(ct_, pl.pos, yaw, eye_, vpitch, v3(-cosf(dmg_dir), -sinf(dmg_dir), 0), pd_last_dmg, MENU_WPN[cfg.primary].sh))
				logp("  death: %s's body in third person, %s%s (b72)\n", chrt[ct_].name, PB.ca.death.clip >= 0 ? PB.ca.B->clip[PB.ca.death.clip].name : "-", PB.rd_ok ? ", then a ragdoll (b82)" : "");
			else logp("  death: first-person fall (no body: type %d)\n", ct_); }
		  if (pdead_t <= 0 && PB.on) pbody_stop(); pd_prev_ = pdead_t; pbody_update(dt); }
		if (pdead_t > 0) {
			pdead_t -= dt; float k = fminf(1, killcam_elapsed(pdead_t) / 0.6f); if (!PB.on) pos[2] = pl.pos.z + PEYE - 110 * k;
			if (pdead_t <= 0 && (dem_on || elim_on) && !net_respawn_now) pdead_t = 0.0001f;   // Demolition, Elimination: dead until the next round (net: unless the host placed us)
			else if (net_client_on() && !net_respawn_now) { if (pdead_t <= 0) pdead_t = 0.0001f; }   // net: a client is dead until the host respawns it
			else if (pdead_t <= 0 || net_respawn_now) { // respawn at the spawn point farthest from the living bots (net client: where the host put us)
				if (!net_respawn_now) {
				int best = team_spawn(0); float bd = best >= 0 ? 1e30f : -1; if (best < 0) best = 0;
				for (int i = 0; i < nspawn; i++) { float md = 1e18f;
					for (int b = 0; b < nbot; b++) if (bots[b].state != BS_DEAD) { float dx = spawns[i].x - bots[b].pl.pos.x, dy = spawns[i].y - bots[b].pl.pos.y; if (dx * dx + dy * dy < md) md = dx * dx + dy * dy; }
					if (md > bd) { bd = md; best = i; } }
				pl.pos = v3(spawns[best].x, spawns[best].y, spawns[best].z + 5); pl.vel = v3(0, 0, 0); pl.yaw = spawns[best].yaw; pitch = vpitch = 0;
				{ float tsp_[3], tyaw_; if (ttm_spawn_point(0, tsp_, &tyaw_)) { pl.pos = v3(tsp_[0], tsp_[1], tsp_[2] + 5); pl.yaw = tyaw_; } }   // Terminal Conquest: the active terminal's spawns
				} net_respawn_now = 0; pdead_t = 0;
				php = 100; pap = 100; for (int k = 0; k < WS_N; k++) wslot_refill(k); reload_t = 0; WP.rl_phase = 0; WP.bolt_pending = 0; WP.gren_state = 0; ev_play("ui/respawn", NULL); music_stop(800);
				hs_back = -1;
				spawn_glow_t = 0; pbody_stop();   // b72: the cyan edge glow (hud_game.h)
				if (vm_ok) { if (WP.quick_from >= 0) { WP.sw_to = WP.quick_from; WP.quick_from = -1; } else WP.sw_to = wcur; weapon_finish_switch(); }
				yaw = pl.yaw; pos[0] = pl.pos.x; pos[1] = pl.pos.y; pos[2] = pl.pos.z + PEYE;
			}
		}
		killcam_camera(pos, &yaw, &vpitch, pdead_t, killer_bot, dt);   // respawn modes: the killer in third person after the fall (killcam.h)
		elim_camera(pos, &yaw, &vpitch, dt);   // Elimination, Demolition: watching an ally after death
		{ float af = vm_ok && WD->ads_fov > 0 ? wpn_ads_fov(WD) : 51; float hf = tanf(59 * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(af * 0.5f * 3.14159f / 180) * ads;   // aiming zooms the world (2x ACOG, 4x sniper scope)
		  persp(P, spect ? 70 * 3.14159f / 180 : 2 * atanf(hf) + sk_fov_add(0) * 3.14159f / 180, 960.0f / 544, 10, view_far); }
		view(V, pos, yaw, vpitch, fwd); mul4(MVP, P, V);
		// b102: the dive's second half (wroom.h) - the game's camera path (effect\camera\cam_cyber_world.nif) from 226 m straight
		// down onto the player's spawn, turning an eighth of a turn; the scene's +y lies along the player's facing
		float dvM[16]; int dv_ = 0;
		{ float e[3], d[3], u[3];
		  if (!fly && wr_dive_cam(e, d, u)) {
			float a_ = pl.yaw - 1.5707963f, ca = cosf(a_), sa = sinf(a_), we[3] = { pl.pos.x + ca * e[0] - sa * e[1], pl.pos.y + sa * e[0] + ca * e[1], pl.pos.z + e[2] };
			float wd[3] = { ca * d[0] - sa * d[1], sa * d[0] + ca * d[1], d[2] }, wu[3] = { ca * u[0] - sa * u[1], sa * u[0] + ca * u[1], u[2] };
			float rr[3] = { wd[1] * wu[2] - wd[2] * wu[1], wd[2] * wu[0] - wd[0] * wu[2], wd[0] * wu[1] - wd[1] * wu[0] }, rl = sqrtf(rr[0] * rr[0] + rr[1] * rr[1] + rr[2] * rr[2]);
			if (rl > 1e-4f) { for (int r = 0; r < 3; r++) rr[r] /= rl;
				float uu[3] = { rr[1] * wd[2] - rr[2] * wd[1], rr[2] * wd[0] - rr[0] * wd[2], rr[0] * wd[1] - rr[1] * wd[0] };
				float Vd[16] = { rr[0], uu[0], -wd[0], 0, rr[1], uu[1], -wd[1], 0, rr[2], uu[2], -wd[2], 0, -(rr[0] * we[0] + rr[1] * we[1] + rr[2] * we[2]), -(uu[0] * we[0] + uu[1] * we[1] + uu[2] * we[2]), wd[0] * we[0] + wd[1] * we[1] + wd[2] * we[2], 1 };
				float Pd[16], PVd[16], Ms[16] = { ca, sa, 0, 0, -sa, ca, 0, 0, 0, 0, 1, 0, pl.pos.x, pl.pos.y, pl.pos.z, 1 };
				memcpy(V, Vd, sizeof(V)); persp(P, 59 * 3.14159f / 180, 960.0f / 544, 10, view_far); mul4(MVP, P, V);
				persp(Pd, 59 * 3.14159f / 180, 960.0f / 544, 50, 90000); mul4(PVd, Pd, V); mul4(dvM, PVd, Ms);   // (the scene's own far plane: its ground is 700 m wide)
				pos[0] = we[0]; pos[1] = we[1]; pos[2] = we[2]; fwd[0] = wd[0]; fwd[1] = wd[1]; fwd[2] = wd[2]; dv_ = 1; } } }

		// test view: the last seconds on the maps that had speckled textures are drawn without lightmaps (labelled on screen)
		int diag_lmoff = 0;   // (the lightmaps-off test view of b19-b25 is no longer needed)
		if (autoplay && !tex_rechecked && map_t > 12) { tex_rechecked = 1; world_tex_check(1); }
		w_probe_frame_early();   // b61: the GPU probe's turn, decided before the mark that opens the world's segment (world.h)
		FT("modes, tutorial, camera");   // render: frame timer (frametime.h)
		cp_ft_collect();   // perf: this frame's segments into the per-second cpu line (cpuprof.h)
		tc_tick(now / 1e6f);   // render: texture-memory canary (texcanary.h)
		int netv_ = (wr_on() && wr_netview()) || (!spect && (sk_is_active(0, SK_CYBERDIVE) || sk_nj_hold > 0));   /* b121: + the half second the exit scene's streaks run */   // b84: the network view - the waiting room, the local Net Jump
		ab_frame_begin(dt, sk_blur_req, !fly && !paused && !spect && !wr_on() && !dv_ && pdead_t <= 0); sk_blur_req = 0;   // b102: the smear as an ability starts - these frames go into its target (ablur.h)
		world_net(netv_); sk_net_view = netv_ && w_net; if (netv_) glClearColor(0, 0.008f, 0.012f, 1); else glClearColor(fog[0], fog[1], fog[2], 1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		drawn = wr_dive_dark() ? 0 : world_draw(MVP, pos, fog, fogp, lmon && !diag_lmoff, cull, (float)(sceKernelGetProcessTimeWide() % 2400000000ULL) * 1e-6f);   // (b102: not behind the dive's black)   // sky motion clock (wraps every 40 min)
		if (dv_) wr_dive_world(dvM);   // b102: the network's map on the ground (effect\cyber_world\start_bg_deco.nif), added over the world
		FT("world draw");   // render: frame timer (frametime.h)
		m_drawn += drawn; m_dframes++;
		glEnable(GL_CULL_FACE);
		bot_mask_on = !cfg.mask_off && !ga_on;   // b71: the enemy team's masks (OPTIONS > GAME; not in Ghost Assault: cyborgs and ghosts)
		if (bots_ok && !netv_ && !(wr_on() && WR.phase != WR_OUT)) { SceUInt64 tb0 = sceKernelGetProcessTimeWide(); bots_shadows(MVP); glDisable(GL_CULL_FACE); bots_draw(MVP, fog, mul4); bp_draw(MVP, fog, pos); pbody_draw(MVP, fog, mul4); bots_planar_shadows(MVP, mul4); vis_draw_world(MVP, pos, now / 1e6f); glEnable(GL_CULL_FACE); bots_draw_us += (int)(sceKernelGetProcessTimeWide() - tb0); }
		tk_draw(MVP, fog); ttm_draw(MVP, fog); wo_draw(MVP, fog);   // Think Tanks, terminals, b87: the train (wobj.h)
		FT("characters, shadows, tanks");   // render: frame timer (frametime.h)
		// the planted bomb (Demolition)
		static float hvt_b[4]; int hvt_on = 0;   // (test runs, handstest: the planted bomb carried 2.2 m before the player)
		if (hv_test) { if (!dem_bomb_ok && md_ok) dem_bomb_ok = model_load(&dem_bomb, VDIR "dem_bomb.gvm") == 0 ? 1 : -1;
			hvt_b[0] = pl.pos.x + cosf(pl.yaw + 0.25f) * 220; hvt_b[1] = pl.pos.y + sinf(pl.yaw + 0.25f) * 220; hvt_b[2] = pl.pos.z + 125; hvt_b[3] = pl.yaw; hvt_on = map_t > 16 && dem_bomb_ok > 0; }
		if (hvt_on || (dem_on && dem_bomb_ok > 0 && dm.bomb_site >= 0 && (dm.state == DS_PLANTED || (dm.state == DS_ROUND_END && dm.last_reason != DR_DETONATION)))) {
			const float *bp_ = hvt_on ? hvt_b : dm.bomb_pos; float dx = hvt_on ? -cosf(hvt_b[3]) : dmap.site[dm.bomb_site].stand[0] - dm.bomb_pos[0], dy = hvt_on ? -sinf(hvt_b[3]) : dmap.site[dm.bomb_site].stand[1] - dm.bomb_pos[1], th = atan2f(dx, -dy), c = cosf(th), sn = sinf(th);
			float M_[16] = { c, sn, 0, 0, -sn, c, 0, 0, 0, 0, 1, 0, bp_[0], bp_[1], bp_[2] - 15, 1 }, BM[16]; mul4(BM, MVP, M_);
			static MBone one[8]; for (int k = 0; k < 8; k++) { memset(&one[k], 0, sizeof(MBone)); one[k].q[3] = 1; }
			float tint[4] = { vm_lit[0], vm_lit[1], vm_lit[2], 1 }; const float ld[4] = { 0.3f, -0.6f, 0.75f, 0 };
			glDisable(GL_CULL_FACE); if (dem_bomb.nb <= 8) model_draw(&dem_bomb, BM, one, ld, tint, fog);
			if (dem_bomb.nb <= 8 && dem_bomb.nmesh > 0) {   // b101: the rest of the game's planted bomb (bomb_box.nif): the device pulsing red once a second
				// (its bomb_02 shell: additive, to a quarter), then the display, the lamp, the flare, the beam and the red diamond (hands.h)
				static float bt_; bt_ += dt; MMesh mm = dem_bomb.mesh[0]; mm.blend = (mm.blend & ~15) | 2; Model Tm = dem_bomb; Tm.mesh = &mm; Tm.nmesh = 1;
				float ph = fmodf(bt_, 1.0f), rc[4] = { 1, 0, 0, 0.25f * (1 - fabsf(ph * 2 - 1)) }; model_draw(&Tm, BM, one, ld, rc, fog);
				hv_bomb_world(MVP, M_, pos, bt_); glEnable(GL_DEPTH_TEST); }
			glEnable(GL_CULL_FACE);
		}
		// thrown grenades: the first-person grenade's own meshes, posed as it left the hand, tumbling along the throw
		if (wsl[WS_GRENADE].ok) for (int gi = 0; gi < NGREN; gi++) { Grenade *g = &gren[gi]; if (!g->on) continue;
			const float *A = gren_cam; float ca = cosf(g->ang), sa = sinf(g->ang), c0[3] = { A[0], A[1], A[2] }, c1[3], c2[3], t[3];
			for (int k = 0; k < 3; k++) { c1[k] = A[4 + k] * ca + A[8 + k] * sa; c2[k] = -A[4 + k] * sa + A[8 + k] * ca; }
			for (int k = 0; k < 3; k++) t[k] = g->p[k] - (c0[k] * A[12] + c1[k] * A[13] + c2[k] * A[14]);
			float M_[16] = { c0[0], c0[1], c0[2], 0, c1[0], c1[1], c1[2], 0, c2[0], c2[1], c2[2], 0, t[0], t[1], t[2], 1 }, GM[16]; mul4(GM, MVP, M_);
			float tint[4] = { vm_lit[0], vm_lit[1], vm_lit[2], 1 }; const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 };
			glDisable(GL_CULL_FACE); md_mesh0 = 2; model_draw(&wsl[WS_GRENADE].m, GM, gren_pose, ld, tint, fog); md_mesh0 = 0; glEnable(GL_CULL_FACE);
		}
		wpn_drops_draw(MVP, fog);   // dropped guns (wpn.h)
		{ float R_[3] = { sinf(yaw), -cosf(yaw), 0 }, U_[3] = { R_[1] * fwd[2] - R_[2] * fwd[1], R_[2] * fwd[0] - R_[0] * fwd[2], R_[0] * fwd[1] - R_[1] * fwd[0] };
		  sk_draw_world(MVP, pos, R_, U_, fog); range_draw(MVP, pos, R_, U_); tut_draw_world(MVP, pos, R_, U_); fx_draw(MVP, pos, R_, U_); sk_draw_world_late(MVP, pos, R_, U_, fog); /* skills b50: shields and walls after the particles */ glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST); }
		if (!fly && (nwm || vm_ok)) {
			// ---- first-person weapon (weapon space: x forward, y left, z up; camera space: x right, y up, -z forward)
			FT("bomb, drops, skills, range, effects");   // render: frame timer (frametime.h)
			glClear(GL_DEPTH_BUFFER_BIT);
			float PV[16], M[16], MV[16], VMVP[16]; MBone *vpose = NULL;
			if (vm_ok) { const WDef *Dv = WD; float th = tanf(Dv->hip_vfov * 0.5f * 3.14159f / 180) * (1 - ads) + tanf(Dv->ads_vfov * 0.5f * 3.14159f / 180) * ads; persp(PV, 2 * atanf(th), 960.0f / 544, 2, 400); }
			else persp(PV, (62 - 14 * ads) * 3.14159f / 180, 960.0f / 544, 2, 400);
			int scoped = vm_ok && WD->sniper && WSC->scope && ads > (WSC->scope[2] > 1.5f ? 0.5f : 0.85f);   // sniper scope: the overlay replaces the viewmodel
			float hs = sqrtf(pl.vel.x * pl.vel.x + pl.vel.y * pl.vel.y) / 535.0f * (1 - ads) * (pl.grounded ? 1 : 0.3f);
			if (spect || dv_ || sk_hide_viewmodel(0) || elim_spectating()) { if (!spect && !dv_ && !elim_spectating()) { if (hv_mode == HV_NETJUMP) hv_draw(vm_lit, sway_x, sway_y); else { sk_vm_sw[0] = sway_x; sk_vm_sw[1] = sway_y; sk_draw_vm(0, vm_lit); } }   // (b121: in the network her open hands, hands.h)   // arm-launcher mode: Batou's launcher arm (skills.h); cyber dive, spectating (Elimination): no gun
			} else if (hv_on()) { hv_draw(vm_lit, sway_x, sway_y);   // b99: the hands alone - the skill gesture, the device being planted, defusing (hands.h; no gun
				// in the picture then: the Demolition video 6:58-7:05 shows the fist, the open left hand and the read-out only)
			} else if (vm_ok) {
				glDisable(GL_CULL_FACE);
				MBone *pose = wpn_vm_pose(WSC, ads); vpose = pose;   // (wpn.h: ADS fade + fire clip follow `ads`)
				float ka = kick * 0.015f, kc = cosf(ka), ks = sinf(ka);
				const WDef *Dv = WD; float ox = Dv->hip_off[0] * (1 - ads) + Dv->ads_off[0] * ads, oy = Dv->hip_off[1] * (1 - ads) + Dv->ads_off[1] * ads - 0.3f * kick * ads, oz = Dv->hip_off[2] * (1 - ads) + Dv->ads_off[2] * ads + 3.0f * kick * ads;
				if (WSC->cy) {   // b112: the cyborg's arms sit lower in the picture than an operator's knife hand, the open left hand beside the
					// skill badge (the footage: both hands low in the corners - IGN 0:13-0:22, Nick Bunyun 0:01-0:10): the whole viewmodel
					// by CY_OFF, the left arm's bones by CY_LOFF more (camera space, cm).
					// b113: and the left forearm turned CY_LTWIST degrees about its own length (elbow -> wrist), palm down: the footage
					// shows the BACK of the left hand, its fingers forward - the knife clips hold it thumb up, its dark inner side to
					// the eye. Test switch cyoff.txt "x y z lx ly lz twist".
					// b114: and the right thumb folded CY_RTHUMB degrees about the hand's own y, under the fingers: opened (b107) its
					// white base block pointed at the eye, the largest pale thing in the picture. cyoff.txt: an 8th number.
					static float cyo[8] = { CY_OFF_X, CY_OFF_Y, CY_OFF_Z, CY_LOFF_X, CY_LOFF_Y, CY_LOFF_Z, CY_LTWIST, CY_RTHUMB }; static int cyo_rd, cyo_e, cyo_w, cyo_rh, cyo_rt[3]; static const Model *cyo_m; static unsigned long long cyo_l, cyo_t;
					if (!cyo_rd) { cyo_rd = 1; FILE *cf = fopen(VDIR "cyoff.txt", "rb"); if (cf) { float v[8]; memcpy(v, cyo, sizeof(v)); if (fscanf(cf, "%f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 3) { memcpy(cyo, v, sizeof(cyo)); logp("cyoff.txt: the cyborg arms' offset %.1f %.1f %.1f cm, the left arm %.1f %.1f %.1f more, its forearm turned %.0f deg, the right thumb folded %.0f deg\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]); } fclose(cf); } }
					if (cyo_m != &VM) { cyo_m = &VM; cyo_l = cyo_t = 0; cyo_e = cyo_w = cyo_rh = -1; cyo_rt[0] = cyo_rt[1] = cyo_rt[2] = -1;
						for (int b = 0; b < VM.nb; b++) { const char *n = VM.bone_name[b];
							if (!strcmp(n, "Bip01_R_Hand")) cyo_rh = b;
							if (!strcmp(n, "Bip01_R_Finger0")) cyo_rt[0] = b;
							if (!strcmp(n, "Bip01_R_Finger01")) cyo_rt[1] = b;
							if (!strcmp(n, "Bip01_R_Finger02")) cyo_rt[2] = b; }
						for (int b = 0; b < VM.nb && b < 64; b++) { const char *n = VM.bone_name[b]; if (!strstr(n, "_L_")) continue;
							const int fo = strstr(n, "Fore") != NULL, ha = strstr(n, "Hand") != NULL, fi = strstr(n, "Finger") != NULL;
							if (strstr(n, "Arm") || fo || ha || fi) cyo_l |= 1ull << b;
							if (fo || ha || fi) cyo_t |= 1ull << b;
							if (!strcmp(n, "Bip01_L_Forearm")) cyo_e = b; if (!strcmp(n, "Bip01_L_Hand")) cyo_w = b; } }
					// (not while an action clip plays - the slash, the heavy stab, the execution, the draw keep the place they were made
					// and checked for, b105 / b107: the offsets fade with the action's weight)
					float ck = 1 - (WSC->a.act >= 0 ? sk_clamp(WSC->a.act_w, 0, 1) : 0); ck = ck * ck * (3 - 2 * ck);
					if (cyo_e >= 0 && cyo_w >= 0 && cyo[6] * ck != 0) {
						float E[3], ax[3]; memcpy(E, pose[cyo_e].p, 12); for (int c = 0; c < 3; c++) ax[c] = pose[cyo_w].p[c] - E[c];
						float al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
						if (al > 1e-3f) { const float ha = cyo[6] * ck * 0.00872665f, sn = sinf(ha) / al; const float r[4] = { ax[0] * sn, ax[1] * sn, ax[2] * sn, cosf(ha) };
							for (int b = 0; b < VM.nb && b < 64; b++) if (cyo_t >> b & 1) { float d[3] = { pose[b].p[0] - E[0], pose[b].p[1] - E[1], pose[b].p[2] - E[2] }, o[3];
								quat_rotate(r, d, o); for (int c = 0; c < 3; c++) pose[b].p[c] = E[c] + o[c]; wq_mul(r, pose[b].q, pose[b].q); } } }
					if (cyo_rh >= 0 && cyo_rt[0] >= 0 && cyo[7] * ck != 0) {
						const float hy[3] = { 0, 1, 0 }, ha = cyo[7] * ck * 0.00872665f, sn = sinf(ha); float ax[3], P0[3]; quat_rotate(pose[cyo_rh].q, hy, ax); memcpy(P0, pose[cyo_rt[0]].p, 12);
						const float r[4] = { ax[0] * sn, ax[1] * sn, ax[2] * sn, cosf(ha) };
						for (int k = 0; k < 3; k++) { const int b = cyo_rt[k]; if (b < 0) continue; float d[3] = { pose[b].p[0] - P0[0], pose[b].p[1] - P0[1], pose[b].p[2] - P0[2] }, o[3];
							quat_rotate(r, d, o); for (int c = 0; c < 3; c++) pose[b].p[c] = P0[c] + o[c]; wq_mul(r, pose[b].q, pose[b].q); } }
					for (int b = 0; b < VM.nb && b < 64; b++) if (cyo_l >> b & 1) { pose[b].p[0] += cyo[3] * ck; pose[b].p[1] += cyo[4] * ck; pose[b].p[2] += cyo[5] * ck; }
					ox += cyo[0] * ck; oy += cyo[1] * ck; oz += cyo[2] * ck; }
				float K[16] = { 1, 0, 0, 0, 0, kc, ks, 0, 0, -ks, kc, 0, sway_x * (1 - ads) + ox, sway_y * (1 - ads) + oy, kick * 0.8f * (1 - ads) + oz, 1 }; mul4(VMVP, PV, K);
				wvm_env_u = yaw * 0.1591549f; wvm_env_v = vpitch; memcpy(wvm_ko, K + 12, 12);   // lens reflections (wpn_vm.h, weapons agent)
				float tint[4] = { 1, 1, 1, 1 }; for (int c = 0; c < 3; c++) tint[c] = fminf(1.4f, fmaxf(0.25f, vm_lit[c] * 1.1f));   // the weapon and arms take the light of the spot the player stands in
				light_muzzle(tint, pflash / 0.035f);   // render: muzzle-flash light on the gun (light.h); pflash is 0.033 on the first frame drawn
				const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }, nofog[4] = { 0, 0, 0, 0 };
				if (wcur == WS_GRENADE && WP.gren_thrown) md_mesh1 = 2;   // the grenade has left the hand
				wvm_fp_on = wvm_fp_state(WSC) && !w_probe_fullfp;   // b60: the light first-person meshes while only the everyday clips play (wpn_vm.h)
				static const float vm_sld[3] = { 0.15f, 1.0f, -0.25f };   // b62: the gun's highlights from overhead, a little beyond it (camera space): the top edges catch them, as in the footage
				md_sld_dir = vm_sld; md_fogp[3] = 2.0f;   // (b68: md_env stays 1, the PC's g_CubeMapBrightness; b65-b67 1.25 here, 0.6 elsewhere - the per-vertex weights from the PC's materials now carry it; fogp.w: the PC's Fresnel_Value 2, model.h MD_VS_FRES)
				if (!scoped && !vis_viewmodel(&VM, VMVP, K, pose, ld, tint, ads)) { wvm_draw(&VM, VMVP, pose, ld, tint, ads, nofog); vmfp_frames++; vmfp_light += wvm_fp_on; vmfp_idx += wvm_fp_idx; }   // (vision.h: camo / round-start looks)
				md_sld_dir = NULL; md_fogp[3] = 0;
				// b63: the knife's slash trail (IGN "9 Minutes of Ghost Assault": the blade leaves a streak across the view). The
				// blade (vm_knife's tip / 30 % attachments) is sampled each frame in viewmodel space; while the tip moves fast
				// (a slash: > 900 units/s, the idle sway ~50) a ribbon over the last 0.09 s is drawn additive, fading with age,
				// across it the soft profile of the effects atlas's tracer streak. 8 samples, <= 7 quads: nothing measurable.
				{ static float tr_p[8][7]; static int tr_n, tr_h;
				  if (WSC->tip && WSC->tbase && vm_grip >= 0 && !scoped) {
					float *s_ = tr_p[tr_h]; model_bone_xform(&pose[vm_grip], WSC->tip, s_); model_bone_xform(&pose[vm_grip], WSC->tbase, s_ + 3); s_[6] = now / 1e6f;
					tr_h = (tr_h + 1) & 7; if (tr_n < 8) tr_n++;
					if (fx_ok && tr_n >= 2) { const float tnow = now / 1e6f; int drawn = 0;
						const FxEmit *te = &fx_em[fx_eff[FX_TRACER].em[0]]; const float *cu = fx_cell[te->cell], um = (cu[0] + cu[2]) * 0.5f;
						for (int k = 1; k < tr_n; k++) { const float *a_ = tr_p[(tr_h - k) & 7], *b_ = tr_p[(tr_h - k - 1) & 7];
							float age = tnow - b_[6]; if (age > 0.09f || a_[6] - b_[6] <= 0) break;
							float dx = a_[0] - b_[0], dy = a_[1] - b_[1], dz = a_[2] - b_[2], v_ = sqrtf(dx * dx + dy * dy + dz * dz) / (a_[6] - b_[6]); if (v_ < 900) break;
							float al = 0.5f * (1 - (tnow - a_[6]) / 0.09f), bl = 0.5f * (1 - age / 0.09f); if (al <= 0) break;
							const float ca[4] = { 0.70f * al, 0.82f * al, 1.0f * al, al }, cb[4] = { 0.70f * bl, 0.82f * bl, 1.0f * bl, bl };
							if (sp_n + 6 > SP_MAXQ * 6) break;
							sp_vert(a_ + 3, um, cu[1], ca); sp_vert(a_, um, cu[3], ca); sp_vert(b_, um, cu[3], cb); sp_vert(a_ + 3, um, cu[1], ca); sp_vert(b_, um, cu[3], cb); sp_vert(b_ + 3, um, cu[1], cb); drawn++; }
						if (drawn) sprite_flush(VMVP, fx_tex, 1); }
				  } else tr_n = 0; }
				wvm_fp_on = 0;
				md_mesh1 = 1 << 30;
				if (vm_grip >= 0 && vm_mz) {   // first-person muzzle flash + shell (view space), drawn after the gun
					float m3[3]; model_bone_xform(&pose[vm_grip], vm_mz, m3);
					if (fp_shot) { float fd[3] = { 0, 0, -1 }, sp[3] = { m3[0] + 2, m3[1] + 1, m3[2] + 40 }, ej[3] = { 0.85f, 0.45f, 0.3f }; fx_muzzle_fp(m3, fd); if (strcmp(WD->sh, "mateba")) fx_shell_fp(sp, ej, vpitch); fp_shot = 0; }
					// aiming narrows the viewmodel lens ~4x: shrink the flash and shells about the muzzle so they keep their on-screen size
					float f = 1 - 0.72f * ads, S[16] = { f, 0, 0, 0, 0, f, 0, 0, 0, 0, f, 0, m3[0] * (1 - f), m3[1] * (1 - f), m3[2] * (1 - f), 1 }, VS[16]; mul4(VS, VMVP, S);
					if (!scoped) fx_draw_fp(VS);
				}
				glEnable(GL_CULL_FACE);
			} else {
			float bx = sinf(bob) * 1.2f * hs, by = -fabsf(cosf(bob)) * 0.9f * hs;
			float ox = 10.0f * (1 - ads) + bx, oy = -17.0f * (1 - ads) + (-16.2f) * ads + by, oz = -24.0f * (1 - ads) - 4.0f * ads + kick * 2.2f;
			float ka = kick * 0.05f; float c = cosf(ka), sn = sinf(ka);
			// columns: images of weapon x, y, z axes, then translation; small pitch-up kick around camera x
			float ax[3] = { 0, sn, -c }, ay[3] = { -1, 0, 0 }, az[3] = { 0, c, sn };
			float Mm[16] = { ax[0], ax[1], ax[2], 0, ay[0], ay[1], ay[2], 0, az[0], az[1], az[2], 0, ox, oy, oz, 1 };
			memcpy(M, Mm, 64); mul4(MV, PV, M);
			glUseProgram(prog[0]); for (int r = 0; r < 4; r++) { float row[4] = { MV[r], MV[4 + r], MV[8 + r], MV[12 + r] }; glUniform4fv(u_mvp[0][r], 1, row); }
			glUniform4fv(u_fog[0], 1, fog); glUniform4fv(u_fogp[0], 1, fogp); glUniform1f(u_lmon[0], 0.f);
			glActiveTexture(GL_TEXTURE0); glDisable(GL_CULL_FACE);
			for (int i = 0; i < nwm; i++) {
				WMesh *m = &wm[i];
				if (m->blend == 2) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
				glBindTexture(GL_TEXTURE_2D, wtex[m->tex]);
				glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void *)0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void *)12); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
				if (m->blend == 2) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
			}
			glEnable(GL_CULL_FACE);
			}
			ab_resolve();   // b102: the smeared frame onto the screen, before the first screen layer
			if (!spect) sk_draw_overlays(0);   // skill screen layers (camo haze, dive, glitch ...)
			// ---- HUD: the game's own widgets (hud_game.h)
			hud_claims_frame();   // ui b42: hud.h claims (HUD de-clutter), one set a frame, before the HUD and the in-match screens
			if (!spect && HG.ok && !wr_on()) {
				HudState H_; memset(&H_, 0, sizeof(H_)); CP_T0(thp_);
				static const int TL[4] = { 0, 200, 400, 800 }, TH[4] = { 200, 400, 800, 800 };
				int tier = intel >= 800 ? 3 : intel >= 400 ? 2 : intel >= 200 ? 1 : 0;
				H_.hp = php < 0 ? 0 : php; H_.ap = pap < 0 ? 0 : pap; H_.intel = intel; H_.tier = tier; H_.tier_lo = TL[tier]; H_.tier_hi = TH[tier];
				sk_hud_state(0, &H_); H_.key = NULL;   // skills.h: Tier-2 intel ring (intel / cost), Tier-1 cooldown arc, icon
				H_.scoreL = score_l; H_.scoreR = score_r; H_.goal = score_limit > 0 ? score_limit * 100 : 10000;   /* b79: the HUD shows the match's goal (v2.0 TDM: 8000) */ H_.time_s = (int)(match_t + 0.999f); H_.time_warn = match_t < 60;
				if (ttm_on) { H_.scoreL = tm.score[0]; H_.scoreR = tm.score[1]; H_.goal = tm.to_win; H_.time_s = (int)((tm.overtime ? tm.ot_left : tm.time_left) + 0.999f); H_.time_warn = tm.overtime || tm.time_left < 20; }
				if (dem_on) { H_.scoreL = dm.score[0]; H_.scoreR = dm.score[1]; H_.goal = dm.to_win; float tl_ = dm.state == DS_PLANTED ? dm.fuse_left : dm.round_left; H_.time_s = (int)(tl_ + 0.999f); H_.time_warn = dm.state == DS_PLANTED || tl_ < 30; }
				elim_hud(&H_); hud_match_state(&H_, cfg.chr, pdead_t > 0);
				H_.ads = ads; H_.spread = 6 + spread * 14 + hs * 10;
				{ V3 eye = v3(pos[0], pos[1], pos[2]), dir = v3(fwd[0], fwd[1], fwd[2]); float bt_; int bh_; H_.aim_red = bots_ok && bots_hit_test(eye, dir, 6000, &bt_, &bh_) >= 0;
				  if (range_active()) { float wd_ = raycast(eye, dir, 9000, NULL, NULL); if (range_aim(eye, dir, wd_ > 0 ? wd_ : 9000) >= 0) H_.aim_red = 1; } }
				H_.hit_t = hit_marker; H_.kill_t = kill_marker;
				{ const WSlot *Wd = WP.quick_from >= 0 ? &wsl[WP.quick_from] : WSC;   // quick knife / grenade: keep showing the gun's counter
				  if (vm_ok && Wd->ok) { H_.mag = Wd->ammo; H_.reserve = Wd->res; H_.cap = wdef[Wd->def].mag; H_.wname = wdef[Wd->def].name; } else { H_.mag = 0; H_.reserve = 0; H_.cap = 0; H_.wname = NULL; }
				  if (ga_wpn_lock == 1) { H_.mag = H_.reserve = H_.cap = 0; H_.wname = NULL; }   // b63 Ghost Assault: the cyborg's blade has no counter (footage)
				  { int ph_; float t_; if (sk_launcher_vm(0, &ph_, &t_)) { H_.mag = H_.reserve = H_.cap = 0; H_.wname = NULL; } }
				  if (hv_on()) { H_.mag = H_.reserve = H_.cap = 0; H_.wname = NULL; } }   // b99: nor while the hands are out (Demolition video 4:47-4:52: no counter over the device)   // b78: no gun counter while the arm launcher is up (Batou video 12:16-12:21)
				{ float hx = 574, hy = 377;   // hip: the counter floats at the gun's hud1 point; aiming/sprinting: fixed under the sight
				  if (vpose && vm_grip >= 0 && vm_hud1) { float m3[3], q[4]; model_bone_xform(&vpose[vm_grip], vm_hud1, m3);
					for (int r = 0; r < 4; r++) q[r] = VMVP[r] * m3[0] + VMVP[4 + r] * m3[1] + VMVP[8 + r] * m3[2] + VMVP[12 + r];
					if (q[3] > 0.5f) { hx = (q[0] / q[3] + 1) * 480; hy = (1 - q[1] / q[3]) * 272; } }
				  float f = fmaxf(ads, VA.base == C_SPRINT ? 1.f : 0.f); H_.mag_x = hx * (1 - f) + 475 * f; H_.mag_y = hy * (1 - f) + 490 * f; }
				H_.notify = notify; H_.notify_t = notify_t; H_.notify_pts = notify_pts;
				for (int k = 0; k < HUD_DMG; k++) { H_.dmg_t[k] = dmg_tt[k]; H_.dmg_ang[k] = -wrap_pi(dmg_a[k] - yaw); }
				H_.hurt = hurt_flash * 0.8f + (php < 30 && php > 0 ? 0.25f : 0);
				sk_low_hp = php > 0 && pdead_t <= 0 ? fminf(1, fmaxf(0, (32 - php) / 22)) : 0;   // b79: the critical-HP haze (skills.h sk_draw_overlays)
				if (killcam_body()) H_.hurt = fmaxf(H_.hurt, 0.35f);   // b72: the body cam's dark red frame (PC footage v49 438 s; 0.85 was far redder than the PC)
				H_.spawn_glow = spawn_glow_t < 1.3f ? 1 - spawn_glow_t / 1.3f : 0; if (pdead_t > 0) H_.spawn_glow = 0;
				memcpy(H_.feed, feed, sizeof(feed)); H_.nfeed = nfeed;
				if (ttm_on) { H_.feed_top = 2 + TTM_PH + 6; H_.feed_rows = 3; H_.score_narrow = 1; }   // ui b42: the kill feed under the Think Tank panels, the score plate between them
				{ static const int ci[5] = { 40, 39, 17, 18, 7 }; static const char *cn[5] = { "RAPID AIM", "RAPID RELOAD", "SILENT CAMO", "EXTENDED MEMORY", "ACTIVE CAMO" };
				  int ca[5] = { ads > 0.5f, reload_t > 0, sk_camo_on(0), 0, sk_camo_on(0) };
				  for (int k = 0; k < 5; k++) { H_.chip[k].icon = ci[k]; H_.chip[k].name = cn[k]; H_.chip[k].active = ca[k]; } H_.nchip = 5; }
				H_.dead = pdead_t > 0; H_.spectating = H_.dead && (elim_on || dem_on);   // b61: the spectate band (elim_game.h) replaces the skill gauge
				for (int i = 0; bots_ok && i < nbot && H_.ntag < 8; i++) { Bot *b = &bots[i]; float sx_, sy_, w_; if (b->team != 0 || b->state == BS_DEAD) continue;
					V3 hp_ = v3(b->head.x, b->head.y, b->head.z + 32); if (!to_screen(MVP, hp_, &sx_, &sy_, &w_) || w_ > 4000) continue;
					H_.tag[H_.ntag].x = sx_; H_.tag[H_.ntag].y = sy_; H_.tag[H_.ntag].a = w_ > 3000 ? (4000 - w_) / 1000 : 1; H_.tag[H_.ntag].name = bot_name(i); H_.tag[H_.ntag].enemy = 0; H_.ntag++; }
				if (sk_ok && sk_is_active(0, SK_CYBERDIVE) && pdead_t <= 0) for (int i = 0; bots_ok && i < nbot && H_.ntag < 8; i++) { Bot *b = &bots[i]; float sx_, sy_, w_; if (b->team == 0 || b->state == BS_DEAD) continue;   // b121: in the network the enemies' names too, red, through the walls (skills.h draws their marks)
					V3 hp_ = v3(b->head.x, b->head.y, b->head.z + 32); if (!to_screen(MVP, hp_, &sx_, &sy_, &w_) || w_ > 4000) continue;
					H_.tag[H_.ntag].x = sx_; H_.tag[H_.ntag].y = sy_; H_.tag[H_.ntag].a = w_ > 3000 ? (4000 - w_) / 1000 : 1; H_.tag[H_.ntag].name = bot_name(i); H_.tag[H_.ntag].enemy = 1; H_.ntag++; }
				H_.mm_tex = mm_tex; H_.mm_w = mm_w; H_.mm_h = mm_h; memcpy(H_.mm_aff, mm_aff, sizeof(mm_aff)); H_.px = pos[0]; H_.py = pos[1]; H_.pyaw = yaw;
				for (int i = 0; bots_ok && i < nbot && H_.nblip < 16; i++) { Bot *b = &bots[i]; if (b->state == BS_DEAD) continue;
					if (b->team == 0) { H_.blip[H_.nblip].x = b->pl.pos.x; H_.blip[H_.nblip].y = b->pl.pos.y; H_.blip[H_.nblip].yaw = b->pl.yaw; H_.blip[H_.nblip].type = 2; H_.nblip++; continue; }
					int seen = (sk_draw_alpha(1 + i, v3(pos[0], pos[1], pos[2])) >= 1 && bot_los(v3(pos[0], pos[1], pos[2]), b->chest)) || sk_known(1 + i);
					if (seen || bot_ping[i] > 0) { H_.blip[H_.nblip].x = b->pl.pos.x; H_.blip[H_.nblip].y = b->pl.pos.y; H_.blip[H_.nblip].yaw = b->pl.yaw; H_.blip[H_.nblip].type = seen ? 0 : 1; H_.nblip++; } }
				if (range_active()) { H_.nfeed = 0; H_.nchip = 0; H_.mm_tex = 0; H_.notify = NULL; H_.ntag = 0; H_.nblip = 0; H_.no_score = 1; }   // the range shows none of these (perf b38: nor the score + timer, QA b37)
				rh_sb_time = range_active() ? -1 : H_.time_s; if (scoreboard_shown()) H_.no_score = 1;   // perf (b38): the held board covers the top-centre panel; its header shows the time (hud_round.h)
				for (int k = 0; dem_on && k < dmap.nsite && H_.nblip < 16; k++) { int bomb = dm.bomb_site == k && dm.state == DS_PLANTED;   // bomb sites on the minimap
					H_.blip[H_.nblip].x = dmap.site[k].marker[0]; H_.blip[H_.nblip].y = dmap.site[k].marker[1]; H_.blip[H_.nblip].yaw = 0; H_.blip[H_.nblip].type = (bomb ? 5 : 3) + (k & 1); H_.nblip++; }
				FT("viewmodel, HUD state");   // render: frame timer (frametime.h)
				ttm_portraits();   // b102: the Think Tank panels' live pictures, under the HUD
				CP_ADD(CP_HUD_PREP, thp_); hud_begin();
				if (scoped) { float sh_; int st_ = wpn_scope_tex(WSC, &sh_);
					if (st_ >= 0) { wpn_scope_draw(VM.tex[st_], sh_, vm_lit, wscope.flash); wscope.flash -= 30 * dt; }   // the baked scoped view (vm_l118.gvm v2)
					else { int ri = (int)WSC->scope[0]; float a = fminf(1, (ads - 0.85f) / 0.1f); scope_overlay(ri >= 0 && ri < VM.ntex ? VM.tex[ri] : 0, ri + 1 < VM.ntex ? VM.tex[ri + 1] : 0, a); }
					H_.ads = 1; H_.spread = 0; } else wscope.flash = 0;
				// b61 (user: 0s of the held scoreboard over the HP / AP text and its ring, the minimap under its title): the board
				// replaces the game HUD while it is up (its own map picture, time and scores), nothing of the HUD shows through it
				int sb_up_ = !tut_on() && scoreboard_shown();
				if (!sb_up_) {
				if (tut_on()) tut_hud_game(&H_, now / 1e6f); else hud_game_draw(&H_, now / 1e6f);
				skills_hud(0, MVP, now / 1e6f); ga_hud_mvp = MVP; elim_hud_draw(now / 1e6f); ga_hud_mvp = NULL;
				if (pdead_t <= 0) wpn_hud(wcur == WS_KNIFE && WP.quick_from < 0);   // pick-up and stealth-kill prompts (wpn.h)
				if (tut_on()) tut_hud(MVP, now / 1e6f); else range_hud(MVP, now / 1e6f);
				dem_hud_draw(MVP, pos, now / 1e6f, pdead_t > 0);   // Demolition: plant / defuse bar, site markers (dem_game.h)
				tk_hud(MVP, v3(pos[0], pos[1], pos[2]), now / 1e6f); ttm_hud(MVP, pos, now / 1e6f);   // Think Tank / Terminal Conquest HUD
				}
				if (!tut_on()) scoreboard_draw(cfg.name, m_pkills, m_pdeaths, m_pkills * 100 + intel / 2, pdead_t > 0, pos, yaw, MAPS[cur_map].name, sel_board || (autoplay && ((elim_on && map_t > 30 && map_t < 34) || (dem_on && map_t > 15 && map_t < 18))), dt);   // hold the score panel (hud_match.h)
				if (pdead_t > 0 && !killcam_active() && HG.white >= 0 && HG.screen >= 0) { float A[6] = { 960, 0, 0, 0, 544, 0 }, dm[4] = { 0.25f, 0.02f, 0.03f, (killcam_body() ? 0.22f : 0.35f) * fminf(1, killcam_elapsed(pdead_t) / 0.5f) }; hud_img(HG.white, HG.screen, A, dm, HUD_Z4, -1, 1); }
				hud_end();
				FT("HUD draw");   // render: frame timer (frametime.h)
			}
		}
		ab_resolve();   // (b102: no weapon block this frame)
		// the game's in-match screens (ovl pack): start splash + commander, respawn, last minute, end of match, pause menu
		if (!fly && wr_on()) { glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); wr_draw(now / 1e6f); glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2); }   // b84
		else if (!fly) {
			glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			if (map_t < 7.5f && match_over_t <= 0 && !dem_on && !elim_on && !range_active()) { if (!scoreboard_shown()) { int md_ = ttm_on ? MODE_TTM : cfg.mode; menu_splash_draw(md_, 0, MENU_MODE_GOAL[md_], map_t - 0.2f); menu_commander_draw(FACE_ARAMAKI, "ARAMAKI", ttm_on ? "Capture the terminal!" : "All units: eliminate the enemy team. Good luck.", map_t - 1.4f); } }   // b38 (QA test C): the TDM/TTM start splash + Aramaki not over the held scoreboard
			else if (ttm_on && ttm_cm_t < 6 && !scoreboard_shown()) menu_commander_draw(ttm_cm_face, ttm_cm_name, ttm_cm_msg, ttm_cm_t);   // Terminal Conquest radio (ttm.h); perf (b38): not over the held scoreboard
			{ int f_; const char *n_, *m_; float t_; if (map_t >= 7.5f && match_over_t <= 0 && !tut_on() && !scoreboard_shown() && !(ttm_on && ttm_cm_t < 6) && !(elim_on && (em_t_cmd < 6 || em_t_lone < 6)) && sk_msg(&f_, &n_, &m_, &t_)) menu_commander_draw(f_, n_, m_, t_); }   // skill messages: ally use, enemy skill, ready, SkillSync (skills.h sk_msg); perf (b38): not over the held scoreboard
			if (killcam_panel_t(pdead_t) >= 0) { MenuRespawn R; memset(&R, 0, sizeof(R)); R.killer = killer_bot >= 0 ? bot_name(killer_bot) : "ENEMY"; R.you = cfg.name;
				R.killer_kills = killer_bot >= 0 ? kills_on_me[killer_bot] : 0; R.your_kills = killer_bot >= 0 ? my_kills_on[killer_bot] : 0; R.weapon_icon = killed_by_icon; R.respawn_in = dem_on ? 0 : pdead_t; R.tip = MENU_TIPS[(int)(map_t * 0.1f) % MENU_NTIPS];
				if (ttm_on) R.respawn_in = ttm_respawn_in;   // Terminal Conquest: the wait for the redeploy wave (ttm.h)
				menu_respawn_draw(&R, killcam_panel_t(pdead_t)); }
			if (tw_t < 3 && !scoreboard_shown()) menu_timewarn_draw(tw_t);   // perf (b38): not over the held scoreboard
			if (match_over_t > 0) menu_endsplash_draw(end_result, ttm_on ? tm.score[0] : dem_on ? dm.score[0] : elim_on ? em.score[0] : score_l / 100, ttm_on ? tm.score[1] : dem_on ? dm.score[1] : elim_on ? em.score[1] : score_r / 100, 8 - match_over_t);
			if (match_over_t <= 0) dem_overlays_draw();   // Demolition: round banner, START, round result, switching sides (dem_game.h)
			elim_overlays_draw();   // Elimination: round banner, zone notices, result / switching bands (elim_game.h)
			ttm_overlay();   // Terminal Conquest banners ("Terminal secured!", "FINAL TERMINAL!", "TACHIKOMA READY" ...)
			tut_overlay(now / 1e6f);   // tutorial: connect sequence, skill explanation screens
			if (paused) { menu_tick(pause_dt); if (opts_open) { float dim[4] = { 0, 0, 0, 0.6f }; menu_begin(menu_fe); menu_rect(0, 0, 960, 544, dim); menu_end(); menu_fe_draw(&cfg); } else menu_pause_draw(); }
			glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
		}
		FT("in-match screens");   // perf: the ovl-pack screens above (respawn, banners, pause menu)
		if (autoplay) { // text: the test tour readout
			glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glUseProgram(prog_flat); glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
			const float __attribute__((unused)) wht[4] = { 1, 1, 1, 0.95f }, cy[4] = { 0.35f, 0.9f, 1, 0.95f }, shade[4] = { 0, 0.02f, 0.05f, 0.72f };
			if (autoplay) { rect(0, 0, 400, 22, shade); textf(6, 4, 2, cy, "TEST %d/%d %s  FPS %.0f", tour_i + 1, NMAPS, MAPS[cur_map].name, last_fps);
				if (diag_lmoff) { const float yl[4] = { 1, 0.85f, 0.2f, 1 }; rect(0, 24, 330, 46, shade); text(6, 28, 2, "TEST VIEW: LIGHTMAPS OFF", yl); } }
			glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
		}
		vglSwapBuffers(GL_FALSE);
		if (DS.on) ds_after_swap(sceKernelGetProcessTimeWide());   // perf (b45): the VBlanks this frame's picture waited (dstep.h)
		ab_after_swap(DS.on && !ft_unlock && !paused ? DS.n_last : 0);   // b102: the ability blur ends itself when its frames come late (ablur.h)
		if (LDP.on && ldp_top() == LDP_FRAME) ldp_report();   // perf (b45): START / the map change -> this first frame of play (ldprof.h)
		frames++;
		if (now - t0 >= 1000000) {
			float fps = frames * 1e6f / (now - t0); if (autoplay && tsec > 3) { fsum += fps; fcount++; if (fps < fmin) fmin = fps; }
			last_fps = fps; if (autoplay && map_t > 2) { m_fsum += fps; m_fcount++; if (fps < m_fmin) m_fmin = fps; }
			alog_mute = !alog_verbose;   // render (b35): the per-second diagnostic lines only in test runs (alog.h)
			if (DS.on) ds_second(logp);   // perf (b45): "step:" line (dstep.h)
			logp("fps %.1f drawn %d/%d feet %.0f %.0f %.0f %s speed %.0f ammo %d/%d voices %d\n", fps, drawn, world_nmesh, pl.pos.x, pl.pos.y, pl.pos.z, pl.grounded ? "ground" : "air", sqrtf(pl.vel.x * pl.vel.x + pl.vel.y * pl.vel.y), vm_ok ? WSC->ammo : 0, vm_ok ? WSC->res : 0, audio_maxvoices); audio_maxvoices = 0;
				if (vm_ok) logp("  arms: %s%s%s | first-person mesh: light in %d of %d frames, %.1fk indices/frame\n", VM.clip[VA.base].name, VA.act >= 0 ? " + " : "", VA.act >= 0 ? VM.clip[VA.act].name : "", vmfp_light, vmfp_frames, vmfp_frames ? vmfp_idx / 1000.f / vmfp_frames : 0.f);
				vmfp_frames = vmfp_light = 0; vmfp_idx = 0;
				skin_log(logp, frames);   // chars b35: skinning cost (skin.h) and paths
				if (oc_on) logp("  occlusion: picture %.2f ms (worst %.2f), range %.0f m%s\n", oc_avg_us / 1000.f, oc_worst_us / 1000.f, oc_far / 100, oc_gave_up ? ", off for now (over budget): tries again in a few seconds" : oc_ready ? "" : " (not drawn this frame)");   // b60 (occl.h)
				if (bots_ok) { logp("  bots drawn: %.2f ms/frame (skinning %.2f ms), %d vertices skinned/frame; animated: %.2f ms/frame\n", bots_draw_us / 1000.f / (frames ? frames : 1), chr_skin_us / 1000.f / (frames ? frames : 1), skin_verts / (frames ? frames : 1), bot_anim_us / 1000.f / (frames ? frames : 1)); bot_anim_us = 0; chr_skin_us = 0; m_bots_ms += bots_draw_us / 1000.f / (frames ? frames : 1); m_bots_n++; bots_draw_us = 0; skin_verts = 0;
					logp("  player hp %.0f kills %d deaths %d | sound %s %.2f, mix %d us | bots:", php, kills, pdeaths, snd_indoor_now ? "indoor" : "outdoor", (double)snd_indoor, audio_mix_us);   // (b107: the indoor probe, as the fire sounds use it)
					for (int i = 0; i < nbot; i++) { Bot *b = &bots[i]; const Model *M = &chrt[b->ct].m; logp(" %d:%s %s %.0f %s%s%s (%.0f %.0f)", i, chrt[b->ct].name, bot_state_name[b->state], b->hp > 0 ? b->hp : 0, bot_anim_name(b), "", "", b->pl.pos.x, b->pl.pos.y); (void)M; }
					logp("\n"); }
			if (range_active()) { char rs_[160]; range_status(rs_, sizeof(rs_)); logp("  %s\n", rs_); }
			if (audio_late) logp("  audio: %d late grains so far, worst gap %d us, worst mix %d us\n", audio_late, audio_worst_us, audio_mix_us);
			cp_print(frames, logp);   // perf: "cpu:" segments + "cpu parts:" subsystems, ms per frame (cpuprof.h)
			if (NG.on) { int m_ = alog_mute; alog_mute = 0; net_line(); alog_mute = m_; }   // net: bandwidth, CPU, corrections (always logged in network matches)
			alog_mute = 0;   // render (b35)
			frames = 0; t0 = now;
		}
	}
#ifdef GITSFA_PGO_GEN   // perf (b45): a profile run: the counters to the card now (the game ends with sceKernelExitProcess: no exit handlers)
	  pgo_write();
#endif
	  alog_close(); sceKernelExitProcess(0); return 0;
}
