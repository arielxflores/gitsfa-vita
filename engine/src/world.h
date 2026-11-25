// Map rendering (owns everything about the world's look): loads <stem>.gmap (+ <stem>.gsky), draws the map
// with the materials the original NIFs ask for, and the sky.
//
//   colour = diffuse * tint * lightmap * 2 (+ glow * glowk) -> exponential fog -> the map's colour-grading LUT
//
// Passes (all inside world_draw):
//   1. opaque + alpha-tested meshes, sorted by program / texture / lightmap at conversion time
//   2. sky layers centred on the camera, drawn at the far plane with depth test and no depth writes, so only the
//      pixels the world leaves uncovered are shaded (no full-screen pass, ~0.1-0.3 ms when the sky fills half the view)
//   3. world-space sky layers, then alpha-blended meshes (decals, glass, backdrops) back to front
//
// .gmap v1 (7-float vertices, alpha flag) still loads; v2 adds per-mesh material records (tools/map_visual.py).
// Every mesh always samples a real lightmap: meshes without one get a neutral one (0.5, x2 = 1.0), so no shader branches.
// Shaders keep to plain main() functions (the Vita's compiler rejected helper functions taking samplers in b25).
#pragma once
#ifndef LDP_PUSH   // perf (b45): ldprof.h's load-phase timers (main.c); nothing without it
#define LDP_PUSH(id) ((void)0)
#define LDP_POP() ((void)0)
#endif
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   // (render b45: uintptr_t)
#include <math.h>
#include "grade.h"
#include "gpumodel.h"   // b38: the offline GPU cost model's prediction per map (logged beside the probe)

#define WORLD_UNITS 4   // texture units used: 0 diffuse, 1 lightmap, 2 colour-grading LUT, 3 glow (b67: + 6, the environment map: 4 is model.h's, 5 vision.h's)

typedef GLuint (*world_mkshader_fn)(GLenum type, const char *src);
typedef void (*world_log_fn)(const char *fmt, ...);

typedef struct { int tex, lm, glow, mode, flags, nv, ni; float ref, glowk, tint[4], mn[3], mx[3]; GLuint vbo, ibo; int c0, c1; unsigned voff, ioff; int env; float envk; int envpx; float fz; } WMeshW;   /* (b116 fz: a ground decal's height, the middle of its level triangles) */   // (b67 env / envk: WFLAG_ENV)   // (render b45: c0..c1 its chunks; perf b46: voff / ioff, its bytes in the shared buffers)
// render b45: chunks. The SGX shades every vertex of a draw and drops triangles only after that, and the tiler bins what is
// left, so what the CPU submits sets the vertex and tiling work. A mesh that writes depth (opaque, alpha-tested) is split
// at load by the 10 m x 10 m cell of each triangle's first vertex (w_cell_of: floor(x / 1000), exact under -ffast-math), its
// triangles sorted by cell (stable: the order inside a cell is the file's); world_draw tests each chunk's box against the
// frustum and draws runs of neighbouring visible chunks as one draw. Blended and no-depth-write meshes stay whole (their
// order is the look). Host count (tools/gpumodel/cullstat.py, every map's spawn views): world indices per view -15..-55 %,
// mean 167k -> 125k. ux0:data/gitsfa/nochunkcull: whole meshes (as b44).
typedef struct { unsigned key; int first, count; float mn[3], mx[3]; } WChunk;
#define W_FDMAX 16
#define W_GCMAX 24
static int w_ngpatch, w_gcn;   // b117: blended low-relief ground patches of the map; the candidates for it
static struct { int mesh, n; float p[4][3]; } w_gc[W_GCMAX];
static int w_nflat_test, w_fd[W_FDMAX];   // b116: alpha-tested ground decals of the map / this frame's, lowest first
#include "occl.h"   // b60: occlusion culling (the map's big opaque triangles into a small depth picture each frame; chunk and mesh boxes tested against it)
static WChunk *w_chunk; static int w_nchunk, w_chunk_on = 1; static long w_idx_drawn, w_idx_all, w_idx_occ; static int w_runs, w_occ_skip, w_probe_fullfp;   // (the probe's frames without occlusion culling / with the full first-person mesh)   // (w_idx_occ, b60: indices of what occl.h hid)
static int w_chunk_skip, w_chunk_win = -1, w_pframe3, w_pdone3; static long w_idx_frame, w_cidx[2]; static unsigned w_cn[2];   // the chunk probe (w_probe_frame)   // (w_idx_*: indices submitted / in frustum-visible meshes, for the log)
typedef struct { int tex, flags, nv, ni; float tint[4], scroll[2], rotz, fog; GLuint vbo, ibo;
	int cyl, pair, merged; float cr, ck, cb, zv[4]; } WSkyLayer;   // b35: a textured cylinder around z (u = ck * angle + cb), its merged partner
enum { WMODE_OPAQUE = 0, WMODE_TEST = 1, WMODE_BLEND = 2 };
enum { WFLAG_TWO = 1, WFLAG_NOZW = 2, WFLAG_VCOL = 4, WFLAG_ADD = 8, WFLAG_REB = 16, WFLAG_FLAT = 32, WFLAG_ENV = 64, WFLAG_GPATCH = 128 /* b117, set at load: a blended low-relief ground patch */, WFLAG_HIDE = 0x4000 };   // FLAT (b59, set at load): a blended ground decal; ENV (b67, file): an environment map
enum { WSKY_ADD = 1, WSKY_ALPHA = 2, WSKY_FAR = 4 };

// ---- state (stats readable by main.c)
static GLuint *w_tex; static int world_ntex; static unsigned *w_tex_bytes, *w_tex_sum, *w_tex_meta; static unsigned char **w_tex_ptr;
static void (*world_progress)(float frac, const char *status);   // optional: called while textures load (frac 0.05..0.6, as main.c's load_map did)
static WMeshW *w_mesh; static int world_nmesh; static long world_tris; static int world_version;
// b67: environment maps (W_ENV): on unless ux0:data/gitsfa/noenvmap; w_env_range (cm) adapts per frame (world_draw)
#define W_ENV_START 2500.f
#define W_ENV_MAX 4000.f
static int w_env_px = 1, w_env_px_offs, w_env_px_big, w_env_pv_only, w_env_px_late, w_env_pv_late; static float w_env_px_okt, w_env_px_wait = 10, w_env_px_ont; static long w_env_px_now, w_env_px_frames;   // b110: per-pixel reflection coordinates while frames are on time (world_draw)
static int w_env_on = 1, w_env_meshes, w_env_tris, w_env_late, w_env_fixed; static float w_env_range = W_ENV_START, w_env_min_seen = W_ENV_START, w_env_ceil = W_ENV_MAX, w_eye[3]; static long w_env_px_draws;
static int w_env_pwin, w_env_poff, w_pframe4;   // b67: the GPU probe's reflection window (18.6-20 s): on / off by turns at W_ENV_MAX
// (w_env_ceil: where the range last ran late x 0.9, creeping back up 2 m a second, so the range settles under the distance
// that cost a frame instead of finding it again every few seconds; envfixed (test switch): the range stays W_ENV_MAX - the
// emulator's frames are all late)
static WSkyLayer *w_sky; static int world_sky_layers; static GLuint *w_sky_tex; static int w_sky_ntex;
static float (*w_sky_flat)[4];                  // b35: per sky texture, its one colour (alpha < 0: not one colour)
static GLuint w_neutral, w_white;               // 4x4 textures: lightmap 0.5 (x2 = 1.0), white
static int world_glow_on = 1;                   // world_set_glow(0) draws glow meshes with the plain programs
static int world_uv_rebased_meshes, world_uv_added;
static world_log_fn w_log;
static int *w_order; static float *w_dist;      // blended meshes visible this frame (sorted back to front)

// ---- shaders
// programs: 0 opaque, 1 alpha test, 2 blend, 3 opaque+glow, 4 test+glow, 5 blend+glow, 6 blend + vertex colour, 7 sky (graded),
// 8 blended sky layers (not graded: grading each additive layer separately would be wrong, and it is cheaper),
// b35 lean variants (same pixels, fewer instructions; render): 9-11 = 2, 5, 6 without the alpha discard (meshes whose
// ref <= 0: the test can never fail; a shader with discard is the slow "punch-through" path on the SGX), 14-16 = 9-11
// without fog (maps whose fog amount is 0: the VR rooms), 12 = blended sky layer without the fog lerp (layer fog 0),
// 13 = flat sky layer (a one-colour texture: the colour, tint, fog and grading are worked out on the CPU, no texture read),
// 17 = two additive sky cylinders in one pass (same band turning both ways: one rasterisation, two texture reads)
// b67 (user: "reflective floors and windows ... without making things worse"): 18-22 = 0, 1, 3, 2, 9 with the map's
// environment map (W_ENV below): opaque, alpha test, opaque + glow, blend, blend without discard
// Grading per program (b32): the opaque and alpha-tested surfaces use the best grading the compiler accepts (grade.h:
// the map's LUT, 2 dependent texture reads); the blended surfaces (2, 5, 6: decals, glass, holograms, glows -- the VR
// rooms are ~200 of them, overdrawn several times) and the far sky (7) use the AFFINE fit of the same LUT (4 mad, no
// texture reads; grade_fit.py: mean error 4-7/255): b31 graded every blended layer with the LUT (VR rooms 43 fps).
// Program sets: 0 = the one drawn; with the autoplay test run 1-3 are built too for the grading probe (world_draw):
// 1 = opaque polynomial, 2 = no grading at all, 3 = everything LUT (b31) -- logged as ms per frame on each map.
#define WP_N 28   // (b110: 23..27 = the env programs 18..22 with the reflection's coordinates per vertex)
#define WPS_N 5
// b84: set 4 = the network view (W_NETSET): the v2.0 "Waiting for players..." room and Reiko's Net Jump (EP26 1:38-2:06,
// 4:43-4:45.5, 7:16-7:22): the world dark, its floors lit teal, its creases and texture lines drawn as bright cyan lines,
// vertical streaks running down the walls; programs 0 opaque, 1 alpha test, 2 blend only (world_net, w_prog_for)
#define W_NETSET 4
static int w_net, w_net_ok;          // the network view is on / its programs were built (1: with derivatives, 2: without)
static float w_netk[4] = { 4.0f, 0.10f, 5.0f, 0 };   // crease line gain / bias, texture line gain, streak clock (ux0:data/gitsfa/netk.txt in test runs)
static GLint w_unetk[WP_N * WPS_N];
static GLuint w_prog[WP_N * WPS_N]; static GLint w_um[WP_N * WPS_N][4], w_ufogc[WP_N * WPS_N], w_ufogp[WP_N * WPS_N], w_utint[WP_N * WPS_N], w_uref[WP_N * WPS_N], w_uglowk[WP_N * WPS_N], w_usp[WP_N * WPS_N];
static GLint w_ueye[WP_N * WPS_N], w_uvr[WP_N * WPS_N][3];   // b67: the env programs' eye (w = envk) and view rotation rows
static int w_graded = GRADE_OFF;   // the grading of set 0's opaque programs (grade.h modes)
static int w_set, w_nsets = 1;     // program set drawn (w_base_set except during the probe), sets built
static int w_base_set;             // b35: per map, 0 = the LUT on opaque surfaces, 1 = the polynomial (maps where the device measured it faster)
static int w_fog_off;              // b35: this frame's fog amount is 0 (lean programs 14-16)
// b35: maps whose opaque surfaces draw faster with the polynomial fit than with the LUT. b32 device probe (ms/frame, LUT ->
// polynomial): port 19.57 -> 17.58, tutorial3 19.36 -> 17.26, tutorial 17.86 -> 17.45 (mean of 3 visits). Host renders,
// LUT vs polynomial: port mean 4.7 / 255 (95 % of pixels within 11), tutorial3 0.8 (3), tutorial 2.7 (5).
// ux0:data/gitsfa/polymaps.txt (one map stem per line; may be empty) replaces this list without a rebuild.
static const char *const w_poly_maps[] = { "port", "tutorial3", "tutorial", NULL };
static int w_is_poly_map(const char *stem) {
	FILE *f = fopen("ux0:data/gitsfa/polymaps.txt", "r");
	if (f) { char l[64]; int hit = 0; while (fgets(l, sizeof(l), f)) { l[strcspn(l, "\r\n ")] = 0; if (l[0] && !strcmp(l, stem)) hit = 1; } fclose(f); return hit; }
	for (int i = 0; w_poly_maps[i]; i++) if (!strcmp(stem, w_poly_maps[i])) return 1;
	return 0;
}
static int w_gm[WP_N * WPS_N];     // the grading each program was built with
// render b37: 1 = the program reads the glow map (sampler glw, unit 3); w_bind_mesh binds the mesh's glow texture for exactly
// these. b32-b34 tested the absolute program index (3..5), true only for set 0: while the test run's grading probe drew
// with sets 1-3 (10-20 s into every map) no glow texture was bound and glow surfaces sampled whatever unit 3 still held
// from the last set-0 frame (the blended signs' glow: tutorial2's sign atlas on the corridor floor, b32 shots 57 / 74 /
// 87, emutest-6 shot-78). b35's pk = pi % WP_N fixed it; this asks the linked program instead of a list of numbers.
static unsigned char w_glw[WP_N * WPS_N];
static char w_stem[24]; static unsigned long long w_pt0, w_plast; static double w_pms[WPS_N]; static int w_pn[WPS_N], w_pframe, w_pdone, w_pprev = -1;
static int w_pdone2, w_pframe2;    // b38: the parts probe before the grading probe (w_probe_frame)
static int w_probe_aim = -1;      // b43: main.c holds the autoplay's aim at this (0 hip, 1 ADS) while >= 0 (the weapon probe)
static const char *W_VS =
"void main(float3 pos, float2 uv0, float2 uv1, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv0; t1 = uv1;"
" fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
#define W_VS_FOGOUT ", float out fog : TEXCOORD2"
#define W_VS_FOGLINE " fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y));"
static const char *W_VS_VC =
"void main(float3 pos, float2 uv0, float2 uv1, float4 col, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2, float4 out vc : TEXCOORD3) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv0; t1 = uv1; vc = col;"
" fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
// b67 W_ENV: the game's environment maps on the world (tools/map_visual.py: the NiTextureEffect environment map with sphere-map
// coordinates that the maps hang above most indoor / city meshes, x the mean of the material's gloss map = envk). Per vertex:
// the view-space reflection of eye -> vertex about the normal (int8 x 3 slot; vr0..2 = the view rotation's rows, from the MVP's
// rows on the CPU) -> sphere-map coordinates, as the PC's fixed-function-style generation; per pixel ONE texture read more
// (unit 6), added to the lit colour before the fog. eye.w = envk. Weighted per pixel by a Fresnel term (F0 0.1: little
// facing the camera, the full sheen toward grazing; fv = eye -> vertex and its dot with the normal, both linear, so exact on
// the big floor triangles where a per-vertex term took the far corners' grazing value everywhere) and by the lightmap's
// brightness (shade reflects less): without them the soft reflection pictures washed shaded shutters, screens and whole
// floors in one even tint (host A/B, tools/mapvis/render.py's twin of this shader).
// b109 (user, Urban: "when you enter the corridor [the bus you climb into] it looks like either part of the reflection or lighting in it
// overstretches out ... the street reflections ... looked a bit off at times"): the sphere-map coordinates were worked out per VERTEX
// and interpolated - fine on small faces, wrong on the big ones the eye stands on or beside (the bus floor: two 12 m triangles; the
// road: up to 200 m2 each): between the corners the picture was pulled straight across what is a strongly curved mapping, and it
// swam as the eye moved. Now the vertex sends the reflected eye -> vertex vector UNNORMALISED (d - 2 (d.n) n: linear over a flat
// face, so its interpolation is exact; its length is |d|, which the Fresnel term already inverts per pixel) and the pixel does the
// sphere map itself: one rsqrt and three multiply-adds more than before, no extra texture read.
static const char *W_VS_ENV =
"void main(float3 pos, float2 uv0, float2 uv1, float4 nrm, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp, uniform float4 eye, uniform float4 vr0, uniform float4 vr1, uniform float4 vr2,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2, float4 out ev : TEXCOORD3, float4 out fv : TEXCOORD4) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv0; t1 = uv1;"
" float3 d = pos - eye.xyz; float dn = dot(d, nrm.xyz); fv = float4(d, dn); float3 r = d - 2.0 * dn * nrm.xyz; ev = float4(dot(vr0.xyz, r), dot(vr1.xyz, r), dot(vr2.xyz, r), eye.w);"
" fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
#define W_ENV_IN ", float4 ev : TEXCOORD3, float4 fv : TEXCOORD4"
#define W_ENV_U ", uniform samplerCUBE envt : TEXUNIT6"
#define W_ENV_U_V ", uniform sampler2D envt : TEXUNIT6"
// b111: the per-pixel kind reads a CUBE map (w_cube_make: the sphere picture resampled onto six 64 px faces at load) with the
// unnormalised reflected vector - the hardware does the mapping, the pixel does no more arithmetic than b108's per-vertex kind
// (b109's two-rsqrt sphere mapping per pixel cost frames on Port and E-Center, and b110's switching it off on late frames left it
// in use 12% of the time on Urban).
#define W_ENV_FR " float cs = 1.0 - min(abs(fv.w) * rsqrt(dot(fv.xyz, fv.xyz) + 1.0), 1.0), c2 = cs * cs, ek = ev.w * (0.1 + 0.9 * c2 * c2 * cs);"
#define W_ENV_ADD W_ENV_FR " lc += texCUBE(envt, ev.xyz).rgb * (ek * saturate(dot(l2, float3(0.3333, 0.3333, 0.3333))));"
#define W_ENV_ADD_H W_ENV_FR " lc += (half3)texCUBE(envt, ev.xyz).rgb * ((half)ek * saturate(dot(l2, half3(0.3333h, 0.3333h, 0.3333h))));"   // (the cosine in float: cm^2 overflows half)
// b110: b109's per-pixel coordinates cost frames where a reflecting floor fills the picture and the map has no time to spare
// (the Vita, pass 1 of the check, late frames a map: Port 5-6 in b105..b107 -> 39 and the reflections' range down to 0; E-Center
// 1-2 -> 12, range 40 -> 4 m; the others as before). The rule is no frame lost: both kinds are built - 18..22 per pixel, 23..27 =
// b108's per vertex (the _V texts below) - and world_draw uses the per-pixel ones only while frames are on time (w_env_px), and only
// on meshes with a triangle edge over 2 m (envpx: on smaller faces the two look the same).
static const char *W_VS_ENV_V =
"void main(float3 pos, float2 uv0, float2 uv1, float4 nrm, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp, uniform float4 eye, uniform float4 vr0, uniform float4 vr1, uniform float4 vr2,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2, float3 out ev : TEXCOORD3, float4 out fv : TEXCOORD4) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv0; t1 = uv1;"
" float3 d = pos - eye.xyz; fv = float4(d, dot(d, nrm.xyz)); float3 r = reflect(normalize(d), nrm.xyz); float3 rv = float3(dot(vr0.xyz, r), dot(vr1.xyz, r), dot(vr2.xyz, r));"
" float em = 2.0 * sqrt(rv.x * rv.x + rv.y * rv.y + (rv.z + 1.0) * (rv.z + 1.0)) + 0.00001; ev = float3(rv.x / em + 0.5, 0.5 - rv.y / em, eye.w);"
" fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
#define W_ENV_IN_V ", float3 ev : TEXCOORD3, float4 fv : TEXCOORD4"
#define W_ENV_FR_V " float cs = 1.0 - min(abs(fv.w) * rsqrt(dot(fv.xyz, fv.xyz) + 1.0), 1.0), c2 = cs * cs, ek = ev.z * (0.1 + 0.9 * c2 * c2 * cs);"
#define W_ENV_ADD_V W_ENV_FR_V " lc += tex2D(envt, ev.xy).rgb * (ek * saturate(dot(l2, float3(0.3333, 0.3333, 0.3333))));"
#define W_ENV_ADD_V_H W_ENV_FR_V " lc += (half3)tex2D(envt, ev.xy).rgb * ((half)ek * saturate(dot(l2, half3(0.3333h, 0.3333h, 0.3333h))));"
#define W_ENV_L2 " float3 l2 = tex2D(lm, t1).rgb * 2.0;"   // (the env bodies keep the lightmap: the reflection's shade)
#define W_ENV_L2_H " half3 l2 = " W_LM_H ";"
static const char *W_VS_SKY =   // sp = (scroll u, scroll v, far: 1 = at the far plane, 0)
"void main(float3 pos, float2 uv0, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 sp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); vpos.z = lerp(vpos.z, vpos.w * 0.99999, sp.z); t0 = uv0 + sp.xy; }";
// fragment parameters / bodies; grade.h adds the grading parameters and code at init (grade_src)
#define W_FS_PARAMS(extra_in) "float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float fog : TEXCOORD2" extra_in "," \
	" uniform sampler2D dif : TEXUNIT0, uniform sampler2D lm : TEXUNIT1, uniform sampler2D glw : TEXUNIT3," \
	" uniform float4 fogc, uniform float4 tint, uniform float ref, uniform float glowk"
#define W_FOG " float3 fc = lerp(lc, fogc.rgb, fog);"   // fog, then the grading tail (grade_code reads fc)
static const char *W_FS_BODY[7] = {
	/* 0 opaque */ " float4 c = tex2D(dif, t0) * tint; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0; float a = 1.0;" W_FOG,
	/* 1 test   */ " float4 c = tex2D(dif, t0) * tint; if (c.a < ref) discard; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0; float a = 1.0;" W_FOG,
	/* 2 blend  */ " float4 c = tex2D(dif, t0) * tint; if (c.a < ref) discard; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0; float a = c.a;" W_FOG,
	/* 3 opq+g  */ " float4 c = tex2D(dif, t0) * tint; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0 + tex2D(glw, t0).rgb * glowk; float a = 1.0;" W_FOG,
	/* 4 tst+g  */ " float4 c = tex2D(dif, t0) * tint; if (c.a < ref) discard; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0 + tex2D(glw, t0).rgb * glowk; float a = 1.0;" W_FOG,
	/* 5 bld+g  */ " float4 c = tex2D(dif, t0) * tint; if (c.a < ref) discard; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0 + tex2D(glw, t0).rgb * glowk; float a = c.a;" W_FOG,
	/* 6 bld+vc */ " float4 c = tex2D(dif, t0) * tint * vc; if (c.a < ref) discard; float3 lc = c.rgb * tex2D(lm, t1).rgb * 2.0; float a = c.a;" W_FOG,
};
#define W_FS_SKY_PARAMS "float4 main(float2 t0 : TEXCOORD0, uniform sampler2D dif : TEXUNIT0, uniform float4 fogc, uniform float4 tint, uniform float ref"
#define W_FS_SKY_BODY " float4 c = tex2D(dif, t0) * tint; float3 fc = lerp(c.rgb, fogc.rgb, ref); float a = c.a;"
// b38: the same programs with the colour math in half precision. The USSE2 does a half3 operation in one cycle where a
// float3 one takes two; texture coordinates and the LUT's addressing stay float. GPU cost model (tools/gpumodel): the
// opaque program (LUT) 33 -> ~24 ALU cycles a pixel, the polynomial one 41 -> ~22. Host check (gradeprec.py, every map's
// own colours): the fp16 polynomial is within 1 step of 255 of the fp32 one on 99 % of pixels, 2 at most. A program the
// compiler rejects in half is built in float (w_build); ux0:data/gitsfa/nohalf builds everything in float (A/B runs).
#define W_FOG_H " half3 fc = lerp(lc, (half3)fogc.rgb, (half)fog);"
// render b43: the half programs take the fog packed by the vertex shader, fogv = (fog colour * f, 1 - f), so the fog is one
// half3 multiply-add a pixel instead of two (fc = lc (1 - f) + fogc f, the same sum). The float programs keep the scalar.
// ux0:data/gitsfa/nofogpack: the b39 way (w_fogpack).
#define W_FOG_HP " half3 fc = lc * fogv.w + fogv.rgb;"
#define W_VS_FOGLINE_P " float f = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); fogv = float4(fogc.rgb * f, 1.0 - f);"
static int w_fogpack = 1;
#define W_TEX_H " half4 c = (half4)tex2D(dif, t0) * (half4)tint;"
#define W_LM_H "(half3)tex2D(lm, t1).rgb * 2.0h"
#define W_GLW_H " + (half3)tex2D(glw, t0).rgb * (half)glowk"
static const char *W_FS_BODY_H[7] = {   // (discarding ones read every texture before the test: the compiler then fetches them
	// ahead of the shader instead of with dependent reads after the discard, one dependent read less per fragment)
	/* 0 opaque */ W_TEX_H " half3 lc = c.rgb * " W_LM_H "; half a = 1.0h;" W_FOG_H,
	/* 1 test   */ W_TEX_H " half3 l = " W_LM_H "; if (c.a < (half)ref) discard; half3 lc = c.rgb * l; half a = 1.0h;" W_FOG_H,
	/* 2 blend  */ W_TEX_H " half3 l = " W_LM_H "; if (c.a < (half)ref) discard; half3 lc = c.rgb * l; half a = c.a;" W_FOG_H,
	/* 3 opq+g  */ W_TEX_H " half3 lc = c.rgb * " W_LM_H W_GLW_H "; half a = 1.0h;" W_FOG_H,
	/* 4 tst+g  */ W_TEX_H " half3 l = " W_LM_H "; half3 gw = (half3)tex2D(glw, t0).rgb * (half)glowk; if (c.a < (half)ref) discard; half3 lc = c.rgb * l + gw; half a = 1.0h;" W_FOG_H,
	/* 5 bld+g  */ W_TEX_H " half3 l = " W_LM_H "; half3 gw = (half3)tex2D(glw, t0).rgb * (half)glowk; if (c.a < (half)ref) discard; half3 lc = c.rgb * l + gw; half a = c.a;" W_FOG_H,
	/* 6 bld+vc */ " half4 c = (half4)tex2D(dif, t0) * (half4)tint * (half4)vc; half3 l = " W_LM_H "; if (c.a < (half)ref) discard; half3 lc = c.rgb * l; half a = c.a;" W_FOG_H,
};
#define W_FS_SKY_BODY_H " half4 c = (half4)tex2D(dif, t0) * (half4)tint; half3 fc = lerp(c.rgb, (half3)fogc.rgb, (half)ref); half a = c.a;"
static int w_half_on = 1;                        // b38: try the half form of every program first (ux0:data/gitsfa/nohalf: off)
static unsigned char w_half[WP_N * WPS_N];       // 1 = this program was built in half precision
static int w_set_half[WPS_N] = { 1, 1, 1, 1 };   // per set: half allowed (the test run's probe builds one set in float)
static char w_src[2600];   // (b67: 2000 -> 2600, the env programs)
static GLuint w_compile(world_mkshader_fn mk, GLenum type, const char *src, int *fail) {
	GLuint s = mk(type, src); GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok); if (!ok) *fail = 1; return s;
}
static int w_link(world_mkshader_fn mk, int i, const char *vs, const char *fs, int sky, int gmode) {
	int fail = 0; GLuint v = w_compile(mk, GL_VERTEX_SHADER, vs, &fail), f = w_compile(mk, GL_FRAGMENT_SHADER, fs, &fail);
	if (fail) return -1;
	GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
	glBindAttribLocation(p, 0, "pos"); glBindAttribLocation(p, 1, "uv0");
	if (!sky) glBindAttribLocation(p, 2, "uv1");
	if (i % WP_N == 6 || i % WP_N == 11 || i % WP_N == 16) glBindAttribLocation(p, 3, "col");
	if (i % WP_N >= 18) glBindAttribLocation(p, 4, "nrm");   // b67
	glLinkProgram(p); GLint lk = 0; glGetProgramiv(p, GL_LINK_STATUS, &lk); if (!lk) return -2;
	w_prog[i] = p; w_gm[i] = gmode; w_glw[i] = !sky && glGetUniformLocation(p, "glw") >= 0;
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; w_um[i][r] = glGetUniformLocation(p, nm); }
	w_ufogc[i] = glGetUniformLocation(p, "fogc"); w_ufogp[i] = glGetUniformLocation(p, "fogp"); w_utint[i] = glGetUniformLocation(p, "tint");
	w_uref[i] = glGetUniformLocation(p, "ref"); w_uglowk[i] = glGetUniformLocation(p, "glowk"); w_usp[i] = glGetUniformLocation(p, "sp");
	glUseProgram(p);
	glUniform1i(glGetUniformLocation(p, "dif"), 0);
	if (!sky) { glUniform1i(glGetUniformLocation(p, "lm"), 1); glUniform1i(glGetUniformLocation(p, "glw"), 3); }
	w_unetk[i] = glGetUniformLocation(p, "netk");   // b84
	w_ueye[i] = glGetUniformLocation(p, "eye"); w_uvr[i][0] = glGetUniformLocation(p, "vr0"); w_uvr[i][1] = glGetUniformLocation(p, "vr1"); w_uvr[i][2] = glGetUniformLocation(p, "vr2");
	if (w_ueye[i] >= 0) glUniform1i(glGetUniformLocation(p, "envt"), 6);   // b67
	grade_register(p, gmode);
	return 0;
}
static void w_strrep(char *d, const char *a, const char *b) {   // replace the first a in d by b (d has room)
	char *p = strstr(d, a); if (!p) return;
	size_t la = strlen(a), lb = strlen(b); memmove(p + lb, p + la, strlen(p + la) + 1); memcpy(p, b, lb);
}
// one program k of set s, grading gm; falls back down grade.h's list (finally: no grading). Returns the mode used, <0 failed.
// b38: each way is tried in half precision first (grade.h grade_code_h), then in float.
static int w_build1(world_mkshader_fn mk, int s, int k, int m, int h) {   // one attempt: program k of set s, grading m, half h
	static char body[700], par[480], vs[1300];
	const int i = s * WP_N + k, pv = k >= 23; if (pv) k -= 5;   // (b110: 23..27 = 18..22 per vertex)
	int env = k >= 18, base = k == 18 ? 0 : k == 19 ? 1 : k == 20 ? 3 : k == 21 || k == 22 ? 2 : k == 9 || k == 14 ? 2 : k == 10 || k == 15 ? 5 : k == 11 || k == 16 ? 6 : k;
	int nodisc = (k >= 9 && k <= 16 && k != 12 && k != 13) || k == 22, nofog = k >= 14 && k <= 16;
	const char *ret = h ? " return float4((float3)gc, (float)a); }" : " return float4(gc, a); }";
	if (base < 7) {
		const char *gt = h ? grade_code_h(m) : grade_code(m); if (!gt) return -1;
		snprintf(body, sizeof(body), "%s", h ? W_FS_BODY_H[base] : W_FS_BODY[base]); snprintf(par, sizeof(par), "%s", base == 6 ? W_FS_PARAMS(", float4 vc : TEXCOORD3") : W_FS_PARAMS(""));
		snprintf(vs, sizeof(vs), "%s", env ? (pv ? W_VS_ENV_V : W_VS_ENV) : base == 6 ? W_VS_VC : W_VS);
		if (env) {   // b67: the lightmap read once into l2 (the colour and the reflection's shade), the reflection added before the fog
			w_strrep(body, h ? W_LM_H : "tex2D(lm, t1).rgb * 2.0", "l2"); memmove(body + strlen(h ? W_ENV_L2_H : W_ENV_L2), body, strlen(body) + 1); memcpy(body, h ? W_ENV_L2_H : W_ENV_L2, strlen(h ? W_ENV_L2_H : W_ENV_L2));
			w_strrep(body, h ? W_FOG_H : W_FOG, pv ? (h ? W_ENV_ADD_V_H W_FOG_H : W_ENV_ADD_V W_FOG) : h ? W_ENV_ADD_H W_FOG_H : W_ENV_ADD W_FOG); w_strrep(par, ", float fog : TEXCOORD2", pv ? ", float fog : TEXCOORD2" W_ENV_IN_V : ", float fog : TEXCOORD2" W_ENV_IN); strcat(par, pv ? W_ENV_U_V : W_ENV_U);
		}
		if (nodisc) w_strrep(body, h ? "if (c.a < (half)ref) discard; " : "if (c.a < ref) discard; ", "");
		if (nofog) { w_strrep(body, h ? W_FOG_H : W_FOG, h ? " half3 fc = lc;" : " float3 fc = lc;"); w_strrep(par, ", float fog : TEXCOORD2", ""); w_strrep(vs, W_VS_FOGOUT, ""); w_strrep(vs, W_VS_FOGLINE, ""); }
		else if (h && w_fogpack) {   // render b43: the packed fog (W_FOG_HP); fogc moves to the vertex shader
			w_strrep(body, W_FOG_H, W_FOG_HP); w_strrep(par, ", float fog : TEXCOORD2", ", half4 fogv : TEXCOORD2"); w_strrep(par, " uniform float4 fogc,", "");
			w_strrep(vs, W_VS_FOGOUT, ", float4 out fogv : TEXCOORD2"); w_strrep(vs, W_VS_FOGLINE, W_VS_FOGLINE_P); w_strrep(vs, "uniform float4 fogp,", "uniform float4 fogp, uniform float4 fogc,");
		}
		else if (h) w_strrep(par, ", float fog : TEXCOORD2", ", half fog : TEXCOORD2");   // b39: the fog varying in half (no conversion move)
		int r = snprintf(w_src, sizeof(w_src), "%s%s) : COLOR {%s%s%s", par, grade_decl(m), body, gt, ret);
		if (r < 0 || r >= (int)sizeof(w_src)) return -1;
		if (w_link(mk, i, vs, w_src, 0, m) < 0) return -1;
	} else if (k == 13) {   // flat: the colour comes in as tint
		if (h) return -1;
		if (w_link(mk, i, W_VS_SKY, "float4 main(float2 t0 : TEXCOORD0, uniform float4 tint) : COLOR { return tint; }", 1, GRADE_OFF) < 0) return -1;
	} else if (k == 17) {   // a pair of additive cylinders (blend ONE, ONE): u offsets in ref4.xy
		if (w_link(mk, i, W_VS_SKY, h ? "float4 main(float2 t0 : TEXCOORD0, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc) : COLOR {"
				" half4 a = (half4)tex2D(dif, float2(t0.x + fogc.x, t0.y)) * (half4)tint; half4 b = (half4)tex2D(dif, float2(t0.x + fogc.y, t0.y)) * (half4)tint;"
				" return float4((float3)(a.rgb * a.a + b.rgb * b.a), 1.0); }"
			: "float4 main(float2 t0 : TEXCOORD0, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc) : COLOR {"
				" float4 a = tex2D(dif, float2(t0.x + fogc.x, t0.y)) * tint; float4 b = tex2D(dif, float2(t0.x + fogc.y, t0.y)) * tint; return float4(a.rgb * a.a + b.rgb * b.a, 1.0); }", 1, GRADE_OFF) < 0) return -1;
	} else {
		int gm = k == 7 ? m : GRADE_OFF; const char *gt = h ? grade_code_h(gm) : grade_code(gm); if (!gt) return -1;
		snprintf(body, sizeof(body), "%s", h ? W_FS_SKY_BODY_H : W_FS_SKY_BODY);
		if (k == 12) w_strrep(body, h ? "lerp(c.rgb, (half3)fogc.rgb, (half)ref)" : "lerp(c.rgb, fogc.rgb, ref)", "c.rgb");
		const char *rs = k == 7 ? ret : h ? " return float4((float3)fc, (float)a); }" : " return float4(fc, a); }";
		int r = snprintf(w_src, sizeof(w_src), "%s%s) : COLOR {%s%s%s", W_FS_SKY_PARAMS, grade_decl(gm), body, gt, rs);
		if (r < 0 || r >= (int)sizeof(w_src)) return -1;
		if (w_link(mk, i, W_VS_SKY, w_src, 1, gm) < 0) return -1;
	}
	w_half[i] = (unsigned char)h;
	return 0;
}
static int w_build(world_mkshader_fn mk, world_log_fn log, int s, int k, int gm, int chain) {
	for (int m = gm; m < (chain ? GRADE_NMODES : gm + 1); m++) {
		for (int h = w_half_on && w_set_half[s] && k != 13; h >= 0; h--) {
			if (w_build1(mk, s, k, m, h) == 0) return m;
			if (log) log("world: program %d of set %d (grading: %s%s) failed\n", k, s, grade_mode_name[m], h ? ", half" : "");
		}
		if (k == 13 || k == 17) return -1;
	}
	return -1;
}
// set s: opaque programs graded gm_o, blended + far sky gm_b. Returns the opaque grading used, <0 failed.
static int w_build_set(world_mkshader_fn mk, world_log_fn log, int s, int gm_o, int gm_b) {
	int used = GRADE_OFF, used_b = GRADE_OFF;
	for (int k = 0; k < WP_N; k++) {
		int blend = k == 2 || k == 5 || k == 6 || k == 7 || (k >= 9 && k <= 16 && k != 12 && k != 13) || k == 21 || k == 22 || k == 26 || k == 27, m = blend && gm_b != gm_o ? w_build(mk, log, s, k, gm_b, 0) : -1;   // the blended way, else the opaque chain
		if (k == 13 || k == 17) { if (w_build(mk, log, s, k, GRADE_OFF, 0) < 0) w_prog[s * WP_N + k] = 0; continue; }   // (optional: without them the layers use 7 / 8)
		if (m < 0) m = w_build(mk, log, s, k, gm_o, 1);
		if (m < 0 && k >= 18) { w_prog[s * WP_N + k] = 0; if (log) log("world: env program %d of set %d failed: those meshes draw without the reflection\n", k, s); continue; }   // b67
		if (m < 0 && k >= 9) {   // a lean variant the compiler rejects: draw with the program it was cut from
			int bk = k == 9 || k == 14 ? 2 : k == 10 || k == 15 ? 5 : k == 11 || k == 16 ? 6 : 8, d = s * WP_N + k, o = s * WP_N + bk;
			w_prog[d] = w_prog[o]; memcpy(w_um[d], w_um[o], sizeof(w_um[d])); w_ufogc[d] = w_ufogc[o]; w_ufogp[d] = w_ufogp[o]; w_utint[d] = w_utint[o]; w_uref[d] = w_uref[o];
			w_uglowk[d] = w_uglowk[o]; w_usp[d] = w_usp[o]; w_gm[d] = w_gm[o]; w_glw[d] = w_glw[o]; w_half[d] = w_half[o]; if (log) log("world: lean program %d of set %d falls back to %d\n", k, s, bk); continue; }
		if (m < 0) return -1;
		if (k == 0) used = m;
		if (k == 2) used_b = m;
	}
	if (log) log("world: program set %d linked (opaque grading: %s, blended + sky: %s)\n", s, grade_mode_name[used], grade_mode_name[used_b]);
	return used;
}
#ifndef WORLD_PROBE_FILE
#define WORLD_PROBE_FILE "ux0:data/gitsfa/gpuprobe"   // b49: its own switch (was autoplay): the probes leave parts out frame by frame, which the user saw as maps flickering after they load
#endif
static int w_probe;
// b84: the network view's programs (W_NETSET). The vertex program passes the world position on; the fragment one makes the
// face normal from its screen derivatives (ddx / ddy of the position: flat per triangle), lights floors by it, draws a line
// where the normal changes inside a 2x2 pixel quad (creases) and where the texture's brightness does (its painted lines),
// and runs thin streaks down the walls (world x + y picks the columns, z + the clock the dashes). A compiler without
// derivatives gets the second form (the lightmap's brightness, the streaks; no lines).
static const char *W_VS_NET =
"void main(float3 pos, float2 uv0, float2 uv1, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 fogp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fog : TEXCOORD2, float3 out wp : TEXCOORD3) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); t0 = uv0; t1 = uv1; wp = pos;"
" fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
#define W_NET_PAR "float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float fog : TEXCOORD2, float3 wp : TEXCOORD3," \
	" uniform sampler2D dif : TEXUNIT0, uniform sampler2D lm : TEXUNIT1, uniform float4 fogc, uniform float4 tint, uniform float ref, uniform float4 netk) : COLOR {"
#define W_NET_TEX " float4 c = tex2D(dif, t0) * tint; float3 l = tex2D(lm, t1).rgb * 2.0; float lu = dot(l, float3(0.3, 0.5, 0.2)); float tl = dot(c.rgb, float3(0.3, 0.59, 0.11));"
#define W_NET_RAIN " float rn = step(0.982, frac((wp.x + wp.y * 0.71) * 0.0123)) * saturate(frac(wp.z * 0.0035 + netk.w) * 3.0 - 2.0);"
static const char *W_NET_D =   // with derivatives
" float3 dx = ddx(wp), dy = ddy(wp); float3 n = normalize(cross(dx, dy)); float up = abs(n.z); float3 fn = fwidth(n);"
" float e = saturate((fn.x + fn.y + fn.z) * netk.x - netk.y) + saturate(fwidth(tl * lu) * netk.z - 0.12) * 0.6;"
// b121 (the user: "make sure that reiko's net jump work properly"; b120 beside EP26 4:43-4:45 and the room at 1:45): the PC's
// network is nearly black and nearly grey - its lit floors (36..75, 60..100, 50..90) of 255 (green over red 1.6, blue under
// green), its walls 10-20, its lines white. b84's colours were a saturated cyan (floors to (25,112,110)): now the footage's,
// the floor's own texture showing in it a little (the footage's floors keep their streaks).
" float3 lc = float3(0.17, 0.28, 0.24) * (up * up * (0.35 + lu * 0.65) * (0.6 + tl * 0.8)) + float3(0.035, 0.055, 0.050) * lu * (1.0 - up * 0.5) + float3(0.85, 0.95, 0.92) * saturate(e) + float3(0.16, 0.26, 0.24) * rn * (1.0 - up);";
static const char *W_NET_N =   // without
" float3 lc = float3(0.16, 0.27, 0.23) * (lu * lu * 0.6 + tl * lu * 0.2) + float3(0.16, 0.26, 0.24) * rn;";
static int w_build_net(world_mkshader_fn mk, world_log_fn log) {
	static char fs[1700];
	w_net_ok = 0;
	for (int k = 0; k < 3; k++) {
		int i = W_NETSET * WP_N + k, ok = 0;
		for (int v = 0; v < 2 && !ok; v++) {
			int r = snprintf(fs, sizeof(fs), "%s%s%s%s%s float3 fc = lerp(lc, fogc.rgb, fog); return float4(fc, %s); }", W_NET_PAR, W_NET_TEX, k ? " if (c.a < ref) discard;" : "",
				W_NET_RAIN, v ? W_NET_N : W_NET_D, k == 2 ? "c.a * 0.6" : "1.0");
			if (r < 0 || r >= (int)sizeof(fs)) return -1;
			if (w_link(mk, i, W_VS_NET, fs, 0, GRADE_OFF) == 0) ok = 1 + v;
			else if (log) log("world: network view program %d (%s) failed\n", k, v ? "without derivatives" : "with derivatives");
		}
		if (!ok) { for (int j = 0; j < 3; j++) w_prog[W_NETSET * WP_N + j] = 0; return -1; }
		if (k == 0) w_net_ok = ok;
	}
	if (log) log("world: network view programs (b84): %s\n", w_net_ok == 1 ? "with the derivative lines" : "without derivatives (no lines)");
	return 0;
}
// b84: the network view on / off (the clear colour goes black while it is on: no sky is drawn)
static void world_net(int on) { w_net = on && w_net_ok; }
// builds the programs (set 0; with the test run also sets 1-3). Returns 0 ok, <0 failure.
static int world_shaders(world_mkshader_fn mk, world_log_fn log) {
	w_log = log;
	if (!grade_half()) w_half_on = 0;   // b39: grade.h reads the nohalf switch for every program
	{ FILE *f = fopen("ux0:data/gitsfa/nofogpack", "rb"); if (f) { fclose(f); w_fogpack = 0; } }   // render b43: A/B switch for the packed fog
	int lut = grade_mode != GRADE_OFF && grade_mode != GRADE_AFFINE, gb = lut ? GRADE_AFFINE : grade_mode;
	int r = w_build_set(mk, log, 0, grade_mode, gb); if (r < 0) return -1;
	w_graded = r; w_nsets = 1;
	if (lut && w_build_set(mk, log, 1, GRADE_POLY, gb) >= 0) w_nsets = 2;   // b35: the polynomial set, for the maps that draw faster with it (w_poly_maps)
	FILE *pf = fopen(WORLD_PROBE_FILE, "rb"); w_probe = pf != NULL && lut && w_nsets == 2; if (pf) fclose(pf);
	if (w_probe) {   // b38: set 2 = set 0 in float (what b37 drew: the half gain per map), set 3 = no grading (what grading costs)
		w_set_half[2] = 0;
		if (w_build_set(mk, log, 2, grade_mode, gb) < 0 || w_build_set(mk, log, 3, GRADE_OFF, GRADE_OFF) < 0) w_probe = 0;
		else w_nsets = 4;
	}
	w_build_net(mk, log);   // b84 (optional: without it the waiting room and the Net Jump draw the plain world)
	if (log) { int nh = 0, n = 0; for (int i = 0; i < WP_N * WPS_N; i++) if (w_prog[i]) { n++; nh += w_half[i]; }
		log("world: %d of %d programs in half precision%s; packed fog (render b43): %s\n", nh, n, w_half_on ? "" : " (nohalf)", w_fogpack ? "on" : "off (nofogpack)"); }
	return 0;
}
static void __attribute__((unused)) world_set_glow(int on) { world_glow_on = on; }   // 0: glow maps off (saves one texture fetch on glow meshes)

#include "glz.h"   // perf b56: packed maps
#include "texup.h"   // perf b56: mip chains with one reallocation
// ---- file reading (maps may be split into <file>.000, .001, ... pieces)
typedef struct { FILE *f; char base[160]; int piece; } WFile;
static WFile w_f;
// perf (b45): a 128 KB stdio buffer for the map file (newlib's default is ~1 KB on the Vita: every small header read after a
// big direct one cost a card read call, ~2-3 per mesh and texture level); reads bigger than it still go straight through
#define W_FBUF (128 * 1024)
static FILE *w_fopen(const char *nm) { FILE *f = fopen(nm, "rb"); if (f) setvbuf(f, NULL, _IOFBF, W_FBUF); return f; }
#if defined(LDRA_B) && !defined(LDRA_OFF)   // perf (b45): ldbg.h's read-ahead (main.c includes it): the card reads ahead while this thread uploads; else stdio below
#define W_RA 1
static LdRa w_ra;
#endif
#ifdef HG_HOST   // perf (b45, host test): a hash of every byte the map readers handed out ("WORLDHASH" in boot.log, world_load)
static unsigned w_bhash = 2166136261u; static unsigned long long w_bcount;
static size_t w_read_(void *d, size_t n);
static size_t w_read(void *d, size_t n) { size_t r = w_read_(d, n); const unsigned char *b = d; for (size_t i = 0; i < r; i++) w_bhash = (w_bhash ^ b[i]) * 16777619u; w_bcount += r; return r; }
#define w_read w_read_
#endif
static size_t w_read(void *d, size_t n) {
#ifdef W_RA
	if (w_ra.on) return ldra_read(&w_ra, d, n);
#endif
	size_t got = 0;
	while (got < n && w_f.f) {
		size_t r = fread((char *)d + got, 1, n - got, w_f.f); got += r;
		if (got < n) { fclose(w_f.f); w_f.f = NULL; if (w_f.piece >= 0) { char nm[176]; snprintf(nm, sizeof(nm), "%s.%03d", w_f.base, ++w_f.piece); w_f.f = w_fopen(nm); } }
	}
	return got;
}
#ifdef HG_HOST
#undef w_read
#endif
static int w_open(const char *path) {   // pieces first: a split map wins over an older single file of the same name
#ifdef W_RA
	if (ldra_open(&w_ra, path) == 0) return 0;   // perf (b45): read-ahead (same pieces-first rule)
#endif
	// perf b56: glz.h joins the pieces and unpacks a packed map (plain single files come back as plain FILEs)
	snprintf(w_f.base, sizeof(w_f.base), "%s", path); w_f.piece = -1; w_f.f = glz_fopen_ex(path, 1, NULL);
	if (w_f.f) setvbuf(w_f.f, NULL, _IOFBF, W_FBUF);
	return w_f.f ? 0 : -1;
}
static void w_close(void) {
#ifdef W_RA
	ldra_close(&w_ra);   // perf (b45)
#endif
	if (w_f.f) fclose(w_f.f);
	w_f.f = NULL; }

// ---- textures: u16 w, u16 h, u8 fmt (0 RGBA8, 1 DXT1, 5 DXT5), u8 nmips, nmips x (u32 size, data)
static void *w_buf; static unsigned w_bufsz;
// b61: the maps' base textures come up to 512 px (tools/map_visual.py hi_plan). w_texcap (test switch tex256): a texture
// larger than this starts at the first mip level that fits (the 256 px look, a quarter of the memory) - read, not uploaded.
static int w_texcap; static unsigned w_tex_skipped, w_tex_big;
// b69 (user, after the b68 device run: "parts of the ground and floor looked really blurry"): anisotropic filtering on the
// maps' base textures. Their textures already ship at the original's size (b61); a floor seen at a grazing angle took a mip
// level 2-3 steps down on trilinear filtering and smeared (host A/B, mapvis/render.py at 960 x 544 without supersampling:
// anisotropic 4x brings the tiles, grates and papers back 5-20 m out, as on the PC). vitaGL takes
// GL_TEXTURE_MAX_ANISOTROPY_EXT and does nothing with it, so the GXM texture is set itself: min filter ANISO_LINEAR (2;
// vitasdk's gxm.h calls the value MIPMAP_LINEAR, PSP2SDK's SCE_GXM_TEXTURE_FILTER_ANISO_LINEAR) and the anisotropic mode bit
// (control word 0 bit 14, PSP2SDK SCE_GXM_TEXTURE_ANISO_ENABLED), checked with sceGxmTextureValidate (a texture it rejects
// stays as it was). Base textures only (the lightmaps and glow maps are smooth). Test switch noaniso (A/B); the governor in
// world_draw turns it off while frames run late with the reflections already off, back on after 4 s without; the GPU probe
// times it frame by frame (18.6-20 s, tag 18). Vita builds only (__vita__): the host builds keep their own texture layout.
static int w_aniso_pwin; static unsigned char *w_aniso_mark; static int w_aniso_on = 1, w_aniso_cur, w_aniso_n, w_aniso_bad, w_aniso_offs; static float w_aniso_okt;
// b70: the b69 device run rejected the raw control-word setting (min filter 2 + bit 14) on every texture
// (sceGxmTextureValidate < 0). Now the library's own setters encode it (sceGxmTextureSetMinFilter with
// SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR = PSP2SDK ANISO_LINEAR), and the variants are tried on the first texture in turn:
// 0 min aniso-linear, 1 + mag aniso-linear, 2 = 0 + bit 14, 3 = 1 + bit 14, 4 min aniso-point; the first that the library
// and sceGxmTextureValidate accept serves every texture; each variant's return codes are logged (world_load)
static int w_aniso_var = -1, w_aniso_rc[5][3], w_aniso_ntry;
#if defined(__vita__) && !defined(HG_HOST)
static int w_aniso_try(SceGxmTexture *t, int v, int *rc) {
	SceGxmTexture keep = *t; int r1 = sceGxmTextureSetMinFilter(t, v == 4 ? SCE_GXM_TEXTURE_FILTER_MIPMAP_POINT : SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR);
	int r2 = v == 1 || v == 3 ? sceGxmTextureSetMagFilter(t, SCE_GXM_TEXTURE_FILTER_MIPMAP_LINEAR) : 0;
	if (v == 2 || v == 3) *(uint32_t *)(void *)t |= 0x4000u;
	int r3 = sceGxmTextureValidate(t); if (rc) { rc[0] = r1; rc[1] = r2; rc[2] = r3; }
	if (r1 < 0 || r2 < 0 || r3 < 0) { *t = keep; return -1; }
	return 0;
}
#endif
static void w_aniso_set(int on) {
	if (!w_aniso_mark || on == w_aniso_cur) return;
	w_aniso_cur = on;
#if defined(__vita__) && !defined(HG_HOST)   // (the host builds - HG_HOST, the data tools' world_host - have their own texture layout: nothing to set)
	for (int i = 0; i < world_ntex; i++) if (w_aniso_mark[i] == 1) {
		glBindTexture(GL_TEXTURE_2D, w_tex[i]); SceGxmTexture *t = vglGetGxmTexture(GL_TEXTURE_2D); if (!t) continue;
		if (!on) { sceGxmTextureSetMinFilter(t, SCE_GXM_TEXTURE_FILTER_LINEAR); sceGxmTextureSetMagFilter(t, SCE_GXM_TEXTURE_FILTER_LINEAR); *(uint32_t *)(void *)t &= ~0x4000u; continue; }   // (vitaGL's trilinear)
		if (w_aniso_var < 0) {   // the first texture: find the variant
			for (int v = 0; v < 5 && w_aniso_var < 0; v++) { w_aniso_ntry = v + 1; if (w_aniso_try(t, v, w_aniso_rc[v]) == 0) w_aniso_var = v; }
			if (w_aniso_var < 0) { w_aniso_var = 99; }   // none: every texture stays trilinear
		}
		if (w_aniso_var > 4 || w_aniso_try(t, w_aniso_var, NULL) < 0) { w_aniso_mark[i] = 2; w_aniso_bad++; }
	}
#endif
}
// b111: the reflection pictures' cube twins. The env maps are 128 x 128 DXT1 sphere maps (view space: u = 0.5 + x / (2 sqrt(2 z + 2)),
// v = 0.5 - y / (2 sqrt(2 z + 2)) for the unit reflected vector). w_load_tex keeps level 0 of every texture of that kind (which
// ones are env maps is known only after the meshes); world_load makes a cube for each one a mesh uses and frees the rest.
#define W_CUBE_N 64
#ifndef GL_TEXTURE_CUBE_MAP   // (the host build's GL header)
#define GL_TEXTURE_CUBE_MAP 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#endif
static struct { GLuint t; unsigned char *d; } w_cst[160]; static int w_cstn;
static GLuint *w_envc; static int w_envc_made, w_env_gov;   // per texture index: its cube (0 = none); w_env_gov: test switch envgov = b110's governor on
static void w_dxt1_rgb(const unsigned char *s, int w, int h, unsigned char *o) {   // -> w * h * 3
	for (int by = 0; by < h; by += 4) for (int bx = 0; bx < w; bx += 4, s += 8) {
		unsigned c0 = s[0] | s[1] << 8, c1 = s[2] | s[3] << 8, bits = s[4] | s[5] << 8 | s[6] << 16 | (unsigned)s[7] << 24; unsigned char c[4][3];
		c[0][0] = (c0 >> 11) * 255 / 31; c[0][1] = ((c0 >> 5) & 63) * 255 / 63; c[0][2] = (c0 & 31) * 255 / 31;
		c[1][0] = (c1 >> 11) * 255 / 31; c[1][1] = ((c1 >> 5) & 63) * 255 / 63; c[1][2] = (c1 & 31) * 255 / 31;
		for (int k = 0; k < 3; k++) { if (c0 > c1) { c[2][k] = (2 * c[0][k] + c[1][k]) / 3; c[3][k] = (c[0][k] + 2 * c[1][k]) / 3; } else { c[2][k] = (c[0][k] + c[1][k]) / 2; c[3][k] = 0; } }
		for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++, bits >>= 2) memcpy(o + ((by + y) * w + bx + x) * 3, c[bits & 3], 3);
	}
}
static GLuint w_cube_make(const unsigned char *dxt) {
	static unsigned char rgb[128 * 128 * 3], face[W_CUBE_N * W_CUBE_N * 4]; GLuint id = 0;
	w_dxt1_rgb(dxt, 128, 128, rgb);
	(glGenTextures)(1, &id); if (!id) return 0;
	glActiveTexture(GL_TEXTURE6); (glBindTexture)(GL_TEXTURE_CUBE_MAP, id);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	for (int f = 0; f < 6; f++) {
		for (int y = 0; y < W_CUBE_N; y++) for (int x = 0; x < W_CUBE_N; x++) {
			// (the outermost texels sit ON the face's edge - s, t = +-1 - so neighbouring faces end in the same colours: no seam where
			// the filter stops at a face's border)
			const float a = 2.0f * x / (W_CUBE_N - 1) - 1, b = 2.0f * y / (W_CUBE_N - 1) - 1; float v[3];
			switch (f) { case 0: v[0] = 1; v[1] = -b; v[2] = -a; break; case 1: v[0] = -1; v[1] = -b; v[2] = a; break; case 2: v[0] = a; v[1] = 1; v[2] = b; break;
				case 3: v[0] = a; v[1] = -1; v[2] = -b; break; case 4: v[0] = a; v[1] = -b; v[2] = 1; break; default: v[0] = -a; v[1] = -b; v[2] = -1; break; }
			const float il = 1.0f / sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); v[0] *= il; v[1] *= il; v[2] *= il;
			const float k = 0.5f / sqrtf(2.0f * v[2] + 2.0002f); float fu = (0.5f + v[0] * k) * 128 - 0.5f, fw = (0.5f - v[1] * k) * 128 - 0.5f;
			if (fu < 0) fu = 0; if (fu > 127) fu = 127; if (fw < 0) fw = 0; if (fw > 127) fw = 127;
			const int x0 = (int)fu, y0 = (int)fw, x1 = x0 < 127 ? x0 + 1 : 127, y1 = y0 < 127 ? y0 + 1 : 127; const float tx = fu - x0, ty = fw - y0;
			unsigned char *o = face + (y * W_CUBE_N + x) * 4;
			for (int c = 0; c < 3; c++) { const float p = (rgb[(y0 * 128 + x0) * 3 + c] * (1 - tx) + rgb[(y0 * 128 + x1) * 3 + c] * tx) * (1 - ty) + (rgb[(y1 * 128 + x0) * 3 + c] * (1 - tx) + rgb[(y1 * 128 + x1) * 3 + c] * tx) * ty; o[c] = (unsigned char)(p + 0.5f); }
			o[3] = 255;
		}
		(glTexImage2D)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA, W_CUBE_N, W_CUBE_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, face); sceGxmTransferFinish();   // (the copy is asynchronous: `face` is reused)
	}
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	(glBindTexture)(GL_TEXTURE_CUBE_MAP, 0); glActiveTexture(GL_TEXTURE0);
	return id;
}
static void w_cube_free(void) {
	for (int i = 0; i < w_cstn; i++) free(w_cst[i].d);
	w_cstn = 0;
	if (w_envc) { for (int i = 0; i < world_ntex; i++) if (w_envc[i]) (glDeleteTextures)(1, &w_envc[i]); free(w_envc); w_envc = NULL; }
	w_envc_made = 0;
}
static int w_load_tex(GLuint t, unsigned *bytes, unsigned *meta) {
	unsigned short w, h; unsigned char fmt, nm;
	if (w_read(&w, 2) != 2 || w_read(&h, 2) != 2 || w_read(&fmt, 1) != 1 || w_read(&nm, 1) != 1) return -1;
	int skip = 0; if (fmt != 0 && nm > 1 && (w > 256 || h > 256) && w <= 512 && h <= 512) w_tex_big++;   // (lightmaps are 1024)
	if (w_texcap && fmt != 0 && nm > 1 && (w > 256 || h > 256) && w <= 512 && h <= 512) while (skip < nm - 1 && ((w >> skip) > w_texcap || (h >> skip) > w_texcap)) skip++;
	if (skip) {   // b61: read every level, upload from level `skip` down
		static unsigned char *all; static unsigned all_cap; const unsigned char *lv[16]; unsigned lsz[16], off[16], tot = 0;
		if (nm > 16) return -5;
		for (int m = 0; m < nm; m++) {
			if (w_read(&lsz[m], 4) != 4) return -2;
			if (tot + lsz[m] > all_cap) { unsigned nc = (tot + lsz[m]) * 2 + 4096; unsigned char *nb = realloc(all, nc); if (!nb) return -3; all = nb; all_cap = nc; }
			if (w_read(all + tot, lsz[m]) != lsz[m]) return -4;
			off[m] = tot; tot += lsz[m];
		}
		int w2 = w >> skip, h2 = h >> skip, nm2 = nm - skip; unsigned sz2[16];
		for (int m = 0; m < nm2; m++) { lv[m] = all + off[m + skip]; sz2[m] = lsz[m + skip]; if (bytes) *bytes += sz2[m]; }
		if (meta) *meta = w2 | (unsigned)h2 << 12 | (unsigned)(fmt & 15) << 24 | (unsigned)(nm2 & 15) << 28;
		glBindTexture(GL_TEXTURE_2D, t);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nm2 > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		GLenum ifmt = fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
		LDP_PUSH(LDP_TEXUP); glCompressedTexImage2D(GL_TEXTURE_2D, 0, ifmt, w2, h2, 0, sz2[0], lv[0]); sceGxmTransferFinish();
		texup_rest(ifmt, w2, h2, nm2, lv, sz2, lv[0], sz2[0]); LDP_POP();
		w_tex_skipped++; return 0;
	}
	if (meta) *meta = w | (unsigned)h << 12 | (unsigned)(fmt & 15) << 24 | (unsigned)(nm & 15) << 28;
	glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nm > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	GLenum ifmt = fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; unsigned sz0 = 0;
	for (int m = 0; m < nm; m++) {
		unsigned sz; if (w_read(&sz, 4) != 4) return -2;
		if (sz > w_bufsz) { free(w_buf); w_bufsz = sz; w_buf = malloc(sz); if (!w_buf) { w_bufsz = 0; return -3; } }
		if (w_read(w_buf, sz) != sz) return -4;
		if (bytes) *bytes += sz;
		int mw = w >> m, mh = h >> m; if (mw < 1) mw = 1; if (mh < 1) mh = 1;
		LDP_PUSH(LDP_TEXUP);   // perf (b45)
		if (fmt == 0) glTexImage2D(GL_TEXTURE_2D, m, GL_RGBA, mw, mh, 0, GL_RGBA, GL_UNSIGNED_BYTE, w_buf);
		else glCompressedTexImage2D(GL_TEXTURE_2D, m, ifmt, mw, mh, 0, sz, w_buf);
		sceGxmTransferFinish();   // vitaGL copies asynchronously: the buffer is reused for the next level
		LDP_POP();
		if (m == 0 && fmt == 1 && w == 128 && h == 128 && sz == 8192 && w_cstn < 160) { unsigned char *c = malloc(8192); if (c) { memcpy(c, w_buf, 8192); w_cst[w_cstn].t = t; w_cst[w_cstn++].d = c; } }   // b111: a possible env map (w_cube_make)
		if (m == 0 && fmt != 0 && nm > 1) { sz0 = sz; break; }   // perf b56: DXT levels 1.. below, in texup.h's order
	}
	if (fmt != 0 && nm > 1) {
		static unsigned char *rest; static unsigned rest_cap; const unsigned char *lv[16]; unsigned lsz[16], off[16], tot = 0;
		if (nm > 16) return -5;
		for (int m = 1; m < nm; m++) {   // levels 1.. into one buffer (a third of level 0's size)
			if (w_read(&lsz[m], 4) != 4) return -2;
			if (tot + lsz[m] > rest_cap) { unsigned nc = (tot + lsz[m]) * 2 + 4096; unsigned char *nb = realloc(rest, nc); if (!nb) return -3; rest = nb; rest_cap = nc; }
			if (w_read(rest + tot, lsz[m]) != lsz[m]) return -4;
			off[m] = tot; tot += lsz[m]; if (bytes) *bytes += lsz[m];
		}
		for (int m = 1; m < nm; m++) lv[m] = rest + off[m];
		LDP_PUSH(LDP_TEXUP); texup_rest(ifmt, w, h, nm, lv, lsz, (const unsigned char *)w_buf, sz0); LDP_POP();
	}
	return 0;
}
static GLuint w_tex4(unsigned char r, unsigned char g, unsigned char b) {
	unsigned char px[64]; for (int i = 0; i < 16; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
	GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); sceGxmTransferFinish();
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	return t;
}

// Texture coordinates of v1 files (and of v2 meshes not rebased by the converter) are kept small here: large values
// (tiling offsets of 30..270000) lose precision in the Vita's texture units and show as speckle. Each triangle's uv0 is
// shifted by a multiple of 8 (the picture is unchanged); triangles repeating a texture 64+ times get one texel.
// Works on a copy of the indices and only commits when everything fits in 16-bit indices.
static int w_uv_rebase(float **pv, int nv, unsigned short *ix, int ni, int stride) {
	float *v = *pv, mx = 0;
	for (int i = 0; i < nv; i++) { float a = fabsf(v[i * stride + 3]), b = fabsf(v[i * stride + 4]); if (a > mx) mx = a; if (b > mx) mx = b; }
	if (mx <= 8) return nv;
	int cap = 1; while (cap < ni * 2) cap <<= 1;
	int *hk = malloc(sizeof(int) * cap * 4), *hv = malloc(sizeof(int) * cap); float *nvb = malloc(sizeof(float) * stride * ni);
	unsigned short *nix = malloc(2 * ni); int nn = 0;
	if (!hk || !hv || !nvb || !nix) { free(hk); free(hv); free(nvb); free(nix); return nv; }
	for (int i = 0; i < cap; i++) hv[i] = -1;
	for (int t = 0; t < ni / 3; t++) {
		float u0 = 1e30f, v0 = 1e30f, u1 = -1e30f, v1 = -1e30f;
		for (int k = 0; k < 3; k++) { const float *q = v + ix[t * 3 + k] * stride; u0 = fminf(u0, q[3]); v0 = fminf(v0, q[4]); u1 = fmaxf(u1, q[3]); v1 = fmaxf(v1, q[4]); }
		int su = (int)floorf(u0 / 8) * 8, sv = (int)floorf(v0 / 8) * 8, flat = (u1 - u0 > 64 || v1 - v0 > 64) ? t : -1;
		for (int k = 0; k < 3; k++) {
			int vi = ix[t * 3 + k]; unsigned h = ((unsigned)vi * 73856093u) ^ ((unsigned)su * 19349663u) ^ ((unsigned)sv * 83492791u) ^ ((unsigned)flat * 2654435761u);
			int slot = h & (cap - 1);
			while (hv[slot] >= 0 && !(hk[slot * 4] == vi && hk[slot * 4 + 1] == su && hk[slot * 4 + 2] == sv && hk[slot * 4 + 3] == flat)) slot = (slot + 1) & (cap - 1);
			if (hv[slot] < 0) {
				if (nn >= 65535) { free(hk); free(hv); free(nvb); free(nix); return nv; }   // would not fit: leave the mesh as it is
				hk[slot * 4] = vi; hk[slot * 4 + 1] = su; hk[slot * 4 + 2] = sv; hk[slot * 4 + 3] = flat; hv[slot] = nn;
				float *o = nvb + nn * stride; memcpy(o, v + vi * stride, 4 * stride); o[3] -= su; o[4] -= sv; nn++;
			}
			nix[t * 3 + k] = (unsigned short)hv[slot];
		}
		if (flat >= 0) {
			float cu = 0, cv = 0; for (int k = 0; k < 3; k++) { float *o = nvb + nix[t * 3 + k] * stride; cu += o[3] - floorf(o[3]); cv += o[4] - floorf(o[4]); }
			for (int k = 0; k < 3; k++) { float *o = nvb + nix[t * 3 + k] * stride; o[3] = cu / 3; o[4] = cv / 3; }
		}
	}
	memcpy(ix, nix, 2 * ni); free(nix); free(hk); free(hv); free(v); *pv = nvb; world_uv_rebased_meshes++; world_uv_added += nn - nv; return nn;
}

// render b45: the largest integer c with c * 1000 <= x (exact: the division is checked with exact products)
static int w_cell_of(float x) { int c = (int)floorf(x * 0.001f); if ((float)c * 1000.0f > x) c--; else if ((float)(c + 1) * 1000.0f <= x) c++; return c; }
static int w_u64_cmp(const void *a, const void *b) { unsigned long long x = *(const unsigned long long *)a, y = *(const unsigned long long *)b; return x < y ? -1 : x > y; }
static int w_frames;   // frames drawn since the map loaded (the chunk log at unload)
#ifdef W_GCHK_GEN
static unsigned short *w_gen_rank; static int w_gen_split;   // perf (b46): tools/gchk: the last split's chunk per triangle
#endif
static void w_split_mesh(WMeshW *m, const float *v, int stride, unsigned short *ix) {
	int nt = m->ni / 3, split = w_chunk_on && m->mode != WMODE_BLEND && !(m->flags & WFLAG_NOZW) && nt > 1;
#ifdef W_GCHK_GEN   // perf (b46): tools/gchk
	free(w_gen_rank); w_gen_rank = malloc(sizeof(unsigned short) * (nt ? nt : 1)); w_gen_split = split;
#endif
	unsigned *key = split ? malloc(sizeof(unsigned) * nt) : NULL; int *ord = split ? malloc(sizeof(int) * nt) : NULL;
	if (split && (!key || !ord)) { free(key); free(ord); key = NULL; split = 0; }
	if (split) for (int i = 0; i < nt; i++) { const float *p = v + ix[i * 3] * stride; key[i] = (unsigned)(w_cell_of(p[0]) + 32768) | ((unsigned)(w_cell_of(p[1]) + 32768) << 16); ord[i] = i; }
	if (split) {   // stable: sorted by (key, index)
		unsigned long long *kk = malloc(sizeof(unsigned long long) * nt);
		if (!kk) split = 0;
		else { for (int i = 0; i < nt; i++) kk[i] = (unsigned long long)key[i] << 32 | (unsigned)i; qsort(kk, nt, sizeof(*kk), w_u64_cmp); for (int i = 0; i < nt; i++) ord[i] = (int)(kk[i] & 0xffffffffu); free(kk); }
	}
	int nc = 1; if (split) for (int i = 1; i < nt; i++) nc += key[ord[i]] != key[ord[i - 1]];
	WChunk *nw = realloc(w_chunk, sizeof(WChunk) * (w_nchunk + nc)); if (!nw) { free(key); free(ord); m->c0 = m->c1 = 0; return; }
	w_chunk = nw; m->c0 = w_nchunk; m->c1 = w_nchunk + nc;
	unsigned short *nix = split ? malloc(2 * m->ni) : NULL; if (split && !nix) split = 0;
	WChunk *c = &w_chunk[w_nchunk]; c->key = split ? key[ord[0]] : 0; c->first = 0; c->count = 0;
	for (int k = 0; k < 3; k++) { c->mn[k] = 1e30f; c->mx[k] = -1e30f; }
	for (int i = 0; i < nt; i++) {
		int o = split ? ord[i] : i;
		if (split && i && key[o] != c->key) { c++; c->key = key[o]; c->first = i * 3; c->count = 0; for (int k = 0; k < 3; k++) { c->mn[k] = 1e30f; c->mx[k] = -1e30f; } }
#ifdef W_GCHK_GEN   // perf (b46): tools/gchk: which chunk each triangle went to
		if (split && w_gen_rank) w_gen_rank[o] = (unsigned short)(c - &w_chunk[m->c0]);
#endif
		for (int j = 0; j < 3; j++) { unsigned short vi = ix[o * 3 + j]; const float *p = v + vi * stride; if (split) nix[i * 3 + j] = vi;
			for (int k = 0; k < 3; k++) { if (p[k] < c->mn[k]) c->mn[k] = p[k]; if (p[k] > c->mx[k]) c->mx[k] = p[k]; } }
		c->count += 3;
	}
	if (split) memcpy(ix, nix, 2 * m->ni);
	w_nchunk += split ? nc : 1; if (!split) m->c1 = m->c0 + 1;
	free(nix); free(key); free(ord);
}
// ---- perf (b46): the chunk split precomputed at build time (tools/gchk/gchk.sh -> <map>.gchk beside the .gmap; the .gmap
// itself is unchanged). Per mesh, in the .gmap's order: a 64-bit hash of what the split depends on (mode, flags, nv, ni,
// the vertex and index bytes as read), then its chunks (key, triangles, box) and, for a split mesh, the chunk of each
// triangle. world_load uses a mesh's record only when its hash matches the mesh just read (and the record says split
// exactly when w_split_mesh would); otherwise that mesh is split as before. The result is the same bytes either way
// (tools/gchk checks every map both ways). No file / another version: w_split_mesh for every mesh.
// File: "GCHK", u32 version 1, u32 cell (1000), u32 meshes, u32 0; per mesh: u64 hash, u32 triangles, u32 chunks,
// u32 split, chunks x (u32 key, u32 triangles, f32 mn[3], f32 mx[3]), [split, chunks > 1: triangles x u8 (chunks <= 256)
// or u16 chunk index, padded to 4 bytes].
static uint64_t w_hash64(uint64_t h, const void *p, size_t n) {   // two 32-bit lanes (multiply, xor-shift): ~1 cycle a byte
	uint32_t a = (uint32_t)h, b = (uint32_t)(h >> 32); const unsigned char *q = (const unsigned char *)p; size_t i = 0;
	for (; i + 8 <= n; i += 8) { uint32_t x, y; memcpy(&x, q + i, 4); memcpy(&y, q + i + 4, 4); a = (a ^ x) * 0x01000193u; b = (b ^ y) * 0x9E3779B1u; a ^= a >> 15; b ^= b >> 13; }
	for (; i < n; i++) { a = (a ^ q[i]) * 0x01000193u; a ^= a >> 15; }
	return (uint64_t)b << 32 | a;
}
static uint64_t w_mesh_hash(const WMeshW *m, const void *v, size_t vb, const void *ix, size_t ib) {
	int hd[4] = { m->mode, m->flags, m->nv, m->ni }; uint64_t h = w_hash64(0x9E3779B97F4A7C15ull, hd, sizeof(hd));
	return w_hash64(w_hash64(h, v, vb), ix, ib);
}
static struct { unsigned char *d; size_t n, pos; int used, missed, nmesh; } w_gk;
static int w_gchk_on = 1; static const char *w_gchk_dir;   // (tools/gchk: off while it makes the files; its own folder)
static void w_gchk_load(const char *dir, const char *stem, unsigned nm) {
	free(w_gk.d); memset(&w_gk, 0, sizeof(w_gk)); if (!w_gchk_on || !w_chunk_on) return;
	char p[200]; snprintf(p, sizeof(p), "%s%s.gchk", w_gchk_dir ? w_gchk_dir : dir, stem);
	FILE *f = fopen(p, "rb"); if (!f) return;
	long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1; unsigned hd[5];
	if (n >= 20 && fseek(f, 0, SEEK_SET) == 0 && (w_gk.d = malloc((size_t)n)) && fread(w_gk.d, 1, (size_t)n, f) == (size_t)n) {
		memcpy(hd, w_gk.d, 20);
		if (memcmp(w_gk.d, "GCHK", 4) || hd[1] != 1 || hd[2] != 1000 || hd[3] != nm) { free(w_gk.d); w_gk.d = NULL; if (w_log) w_log("world: %s ignored (not for this map / version)\n", p); }
		else { w_gk.n = (size_t)n; w_gk.pos = 20; w_gk.nmesh = (int)nm; }
	} else { free(w_gk.d); w_gk.d = NULL; }
	fclose(f);
}
static int w_gchk_apply(WMeshW *m, unsigned short *ix, uint64_t h) {   // 1: m's chunks (and ix's order) from the file
	if (!w_gk.d) return 0;
	const unsigned char *r = w_gk.d + w_gk.pos; size_t left = w_gk.n - w_gk.pos; uint64_t rh; unsigned nt, nc, sp;
	if (left < 20) { free(w_gk.d); w_gk.d = NULL; return 0; }
	memcpy(&rh, r, 8); memcpy(&nt, r + 8, 4); memcpy(&nc, r + 12, 4); memcpy(&sp, r + 16, 4);
	size_t rl = 20 + (size_t)nc * 32 + (sp && nc > 1 ? ((size_t)nt * (nc <= 256 ? 1 : 2) + 3) & ~(size_t)3 : 0);
	if (!nc || nc > 65536 || rl > left) { free(w_gk.d); w_gk.d = NULL; return 0; }   // (a broken file: no more records)
	w_gk.pos += rl;
	int ntt = m->ni / 3, want = w_chunk_on && m->mode != WMODE_BLEND && !(m->flags & WFLAG_NOZW) && ntt > 1;
	if (rh != h || (int)nt != ntt || (int)sp != want || (!sp && nc != 1)) { w_gk.missed++; return 0; }
	const unsigned char *cp = r + 20; unsigned sum = 0;
	for (unsigned k = 0; k < nc; k++) { unsigned t; memcpy(&t, cp + k * 32 + 4, 4); sum += t; }
	if (sum != nt) { w_gk.missed++; return 0; }
	unsigned short *nix = NULL; unsigned *pos = NULL;
	if (sp && nc > 1) {   // each triangle to its chunk, in the file's order inside a chunk (= w_split_mesh's stable sort)
		nix = malloc(2 * (size_t)m->ni); pos = malloc(sizeof(unsigned) * 2 * nc);
		if (!nix || !pos) { free(nix); free(pos); w_gk.missed++; return 0; }
		unsigned f0 = 0; for (unsigned k = 0; k < nc; k++) { unsigned t; memcpy(&t, cp + k * 32 + 4, 4); pos[2 * k] = f0; pos[2 * k + 1] = f0 + t; f0 += t; }
		const unsigned char *rk = cp + (size_t)nc * 32;
		for (unsigned i = 0; i < nt; i++) {
			unsigned k = nc <= 256 ? rk[i] : (unsigned)(rk[2 * i] | rk[2 * i + 1] << 8);
			if (k >= nc || pos[2 * k] >= pos[2 * k + 1]) { free(nix); free(pos); w_gk.missed++; return 0; }
			unsigned d = pos[2 * k]++; memcpy(nix + 3 * d, ix + 3 * i, 6);
		}
	}
	WChunk *nw = realloc(w_chunk, sizeof(WChunk) * (w_nchunk + nc)); if (!nw) { free(nix); free(pos); w_gk.missed++; return 0; }
	w_chunk = nw; m->c0 = w_nchunk; m->c1 = w_nchunk + (int)nc;
	unsigned f0 = 0;
	for (unsigned k = 0; k < nc; k++) { WChunk *c = &w_chunk[w_nchunk + k]; unsigned t; const unsigned char *q = cp + k * 32;
		memcpy(&c->key, q, 4); memcpy(&t, q + 4, 4); memcpy(c->mn, q + 8, 12); memcpy(c->mx, q + 20, 12); c->first = (int)f0 * 3; c->count = (int)t * 3; f0 += t; }
	if (nix) memcpy(ix, nix, 2 * (size_t)m->ni);
	free(nix); free(pos); w_nchunk += (int)nc; w_gk.used++; return 1;
}
#ifdef W_GCHK_GEN
static void w_gen_mesh(int i, const WMeshW *m, uint64_t h, const unsigned short *ix);   // tools/gchk
#endif

// ---- perf (b46): optionally, the world's vertex and index data in a few big GL buffers (segments of up to W_SEG_BYTES, each
// mesh at an offset: WMeshW voff / ioff) instead of two per mesh (urban: 752 -> 9). OFF unless ux0:data/gitsfa/bufpool (or
// bufpool.txt "<vertex KB> <index KB> [alignment]") exists: Vita3K's buffer trapping keeps far more host memory for vertex
// streams that start inside a shared buffer (b46, instance 8, its RSS after the first map: two buffers per mesh 1.4 GB;
// shared 1 MB segments 5.4 GB and killed by the OOM killer later; 64 KB 4.8 GB; vertex only 5.4 GB; 4 KB-aligned 2.7 GB),
// and QA's long runs are on Vita3K. On the device it saves ~740 vitaGL allocations and vbo structs per map load and
// texcanary's per-buffer checks: an A/B for a device run. Same draws either way (host: HG_DRAWHASH, b46). QA b45's soak
// counted texcanary's "buffer table rebuilt": that table holds buffer names, which are vitaGL's vbo structs' heap
// addresses; they changed with the heap's use (b45: more, different temporary allocations at load), not with the buffer
// count (render b45's chunks draw ranges of each mesh's one index buffer). W_SEG_BYTES 0 (tools/world_host's stub GL: its
// tools find a draw's mesh by its index buffer): never shared.
#ifndef W_SEG_BYTES
#define W_SEG_BYTES (1024 * 1024)
#endif
typedef struct { unsigned char *buf; unsigned n, cap; int from; } WSeg;   // staging: bytes used, its size, the first mesh waiting for it
static WSeg w_sv, w_si; static GLuint *w_bufs; static int w_nbufs, w_bufcap, w_nseg, w_seg_on;   // w_seg_on: the bufpool switch (A/B)
static unsigned w_seg_v = W_SEG_BYTES, w_seg_i = W_SEG_BYTES, w_seg_al = 16;   // segment sizes (vertex, index); test: ux0:data/gitsfa/bufpool.txt "<vertex KB> <index KB>" (0: per mesh)
static GLuint w_upload(GLenum target, const void *d, unsigned n) {
	LDP_PUSH(LDP_BUFUP); GLuint b = 0; glGenBuffers(1, &b); glBindBuffer(target, b); glBufferData(target, n, d, GL_STATIC_DRAW); LDP_POP();
	if (w_nbufs == w_bufcap) { int c = w_bufcap ? w_bufcap * 2 : 64; GLuint *q = (GLuint *)realloc(w_bufs, c * sizeof(GLuint)); if (q) { w_bufs = q; w_bufcap = c; } }
	if (w_nbufs < w_bufcap) w_bufs[w_nbufs++] = b;
	w_nseg++; return b;
}
static void w_seg_flush(WSeg *S, GLenum target, int upto, int vert) {   // one buffer for the meshes [from, upto) still waiting
	if (S->n) { GLuint b = w_upload(target, S->buf, S->n);
		for (int k = S->from; k < upto; k++) { WMeshW *m = &w_mesh[k]; if (vert) { if (!m->vbo) m->vbo = b; } else if (!m->ibo) m->ibo = b; } }
	S->n = 0; S->from = upto;
}
static unsigned char *w_seg_take(WSeg *S, GLenum target, unsigned n, int mesh, unsigned *off, int vert) {   // NULL: its own buffer
	unsigned cap = vert ? w_seg_v : w_seg_i;
	if (!cap || !w_seg_on || n > cap) return NULL;
	if (!S->buf) { S->buf = (unsigned char *)malloc(cap); if (!S->buf) return NULL; S->n = 0; S->cap = cap; S->from = mesh; }
	unsigned o = (S->n + w_seg_al - 1) & ~(w_seg_al - 1);
	if (o + n > S->cap) { w_seg_flush(S, target, mesh, vert); o = 0; }
	*off = o; S->n = o + n; return S->buf + o;
}
static void w_seg_end(int upto) {   // the last segments, then the staging back to the heap
	if (w_sv.buf) w_seg_flush(&w_sv, GL_ARRAY_BUFFER, upto, 1);
	if (w_si.buf) w_seg_flush(&w_si, GL_ELEMENT_ARRAY_BUFFER, upto, 0);
	free(w_sv.buf); free(w_si.buf); memset(&w_sv, 0, sizeof(w_sv)); memset(&w_si, 0, sizeof(w_si));
}
static void world_unload(void) {
	for (int i = 0; i < w_nbufs; i++) glDeleteBuffers(1, &w_bufs[i]);   // perf (b46): the world's buffers (shared by meshes)
	free(w_bufs); w_bufs = NULL; w_nbufs = w_bufcap = 0; w_nseg = 0;
	free(w_mesh); w_mesh = NULL; world_nmesh = 0; world_tris = 0;
	if (w_log && oc_st_frames) w_log("world occlusion: %ld indices a frame hidden behind walls (%ld drawn), %ld of %ld box tests hidden, %ld occluders in %ld cells a frame, picture %.2f ms a frame (worst %.2f), range %.0f m, %d time(s) off for a while (over its time budget at the shortest range), %u cell(s) left out for time (b60, occl.h)\n",
		w_idx_occ / oc_st_frames, w_idx_drawn / (w_frames ? w_frames : 1), oc_st_hidden / oc_st_frames, oc_st_tests / oc_st_frames, oc_st_tris / oc_st_frames, oc_st_cells / oc_st_frames,
		oc_st_us / 1000.f / oc_st_frames, oc_worst_us / 1000.f, oc_far / 100, oc_n_gave_up, oc_st_late);
	oc_reset(); oc_st_tris = oc_st_tests = oc_st_hidden = oc_st_frames = oc_st_cells = oc_st_us = 0; w_idx_occ = 0;
	if (w_log && w_frames) w_log("world chunks: %ld of %ld indices of the frustum-visible meshes drawn (%.0f%%), %.0f draws a frame (render b45%s)\n", w_idx_drawn / w_frames, w_idx_all / w_frames,
		w_idx_all ? 100.0 * w_idx_drawn / w_idx_all : 0.0, (double)w_runs / w_frames, w_chunk_on ? "" : ", nochunkcull");
	if (w_log && w_frames && w_env_meshes) w_log("world env: %d meshes (%dk triangles) with the map's environment map%s, %.1f env draws a frame, range %.0f m now (lowest %.0f m), %d late frames (b67)\n",
		w_env_meshes, w_env_tris / 1000, w_env_on ? "" : " (noenvmap: off)", (double)w_env_px_draws / w_frames, w_env_range / 100, w_env_min_seen / 100, w_env_late);
	if (w_log && w_frames && w_env_meshes) w_log("world env: cube reflection on %d of %d meshes (an edge over 2 m; %d cube maps), late frames with a cube draw in them %d, without %d%s (b111)\n",
		w_env_px_big, w_env_meshes, w_envc_made, w_env_px_late, w_env_pv_late, w_env_gov ? " (envgov: a late frame takes it away for a while)" : "");
	if (w_log && w_frames && w_env_meshes && w_env_gov) w_log("world env: envgov: in use %d%% of the frames, taken away %d time(s), %s now (next try after %.0f s on time)\n", (int)(100.0 * w_env_px_frames / w_frames), w_env_px_offs, w_env_px ? "on" : "off", w_env_px_wait);
	free(w_chunk); w_chunk = NULL; w_nchunk = 0; w_idx_drawn = w_idx_all = 0; w_runs = w_frames = 0;   // render b45
	if (w_log && w_frames && w_aniso_n) w_log("world aniso: %s, %d time(s) off for late frames (b69)\n", w_aniso_on ? "on" : "off (noaniso)", w_aniso_offs);
	free(w_aniso_mark); w_aniso_mark = NULL; w_aniso_n = 0; w_aniso_cur = 0;
	if (world_ntex) glDeleteTextures(world_ntex, w_tex);
	w_cube_free();   // b111 (before world_ntex goes)
	free(w_tex); w_tex = NULL; world_ntex = 0; free(w_tex_bytes); free(w_tex_sum); free(w_tex_ptr); free(w_tex_meta); w_tex_bytes = w_tex_sum = w_tex_meta = NULL; w_tex_ptr = NULL;
	for (int i = 0; i < world_sky_layers; i++) { glDeleteBuffers(1, &w_sky[i].vbo); glDeleteBuffers(1, &w_sky[i].ibo); }
	free(w_sky); w_sky = NULL; world_sky_layers = 0;
	if (w_sky_ntex) glDeleteTextures(w_sky_ntex, w_sky_tex);
	free(w_sky_tex); w_sky_tex = NULL; w_sky_ntex = 0; free(w_sky_flat); w_sky_flat = NULL;
	free(w_order); free(w_dist); w_order = NULL; w_dist = NULL;
	free(w_buf); w_buf = NULL; w_bufsz = 0;
}
// b35: the one colour of a 4x4 texture with a single level (RGBA8 or DXT1) whose 16 texels are equal; 0 = not one colour
static int w_tex_flat(unsigned meta, const unsigned char *d, float *out) {
	unsigned w = meta & 4095, h = (meta >> 12) & 4095, fmt = (meta >> 24) & 15, nm = meta >> 28; unsigned char t[16][4];
	if (w != 4 || h != 4 || nm != 1 || !d) return 0;
	if (fmt == 0) memcpy(t, d, 64);
	else if (fmt == 1) {
		unsigned c[2] = { d[0] | d[1] << 8, d[2] | d[3] << 8 }, idx = d[4] | d[5] << 8 | d[6] << 16 | (unsigned)d[7] << 24; unsigned char p[4][4];
		for (int k = 0; k < 2; k++) { unsigned r = (c[k] >> 11) & 31, g = (c[k] >> 5) & 63, b = c[k] & 31; p[k][0] = (unsigned char)(r << 3 | r >> 2); p[k][1] = (unsigned char)(g << 2 | g >> 4); p[k][2] = (unsigned char)(b << 3 | b >> 2); p[k][3] = 255; }
		for (int j = 0; j < 4; j++) {
			if (c[0] > c[1]) { p[2][j] = (unsigned char)((2 * p[0][j] + p[1][j]) / 3); p[3][j] = (unsigned char)((p[0][j] + 2 * p[1][j]) / 3); }
			else { p[2][j] = (unsigned char)((p[0][j] + p[1][j]) / 2); p[3][j] = 0; } }
		if (c[0] <= c[1]) p[2][3] = 255;
		for (int i = 0; i < 16; i++) memcpy(t[i], p[(idx >> (2 * i)) & 3], 4);
	} else return 0;
	for (int i = 1; i < 16; i++) if (memcmp(t[i], t[0], 4)) return 0;
	for (int j = 0; j < 4; j++) out[j] = t[0][j] / 255.f;
	return 1;
}
// b35: is this layer a textured cylinder around the z axis whose u runs with the angle (u = ck * angle + cb, whole
// repeats around) and whose v depends on z only? (the VR rooms' turning bands: two layers with the same band turning
// opposite ways are then drawn in one pass, w_sky_pairs)
static void w_sky_cyl(WSkyLayer *L, const float *v) {
	L->cyl = 0; L->pair = -1; L->merged = 0; if (L->nv < 8) return;
	float rmin = 1e30f, rmax = 0, umin = 1e30f, umax = -1e30f;
	for (int i = 0; i < L->nv; i++) { const float *p = v + i * 5; float r = sqrtf(p[0] * p[0] + p[1] * p[1]); if (r < rmin) rmin = r; if (r > rmax) rmax = r; if (p[3] < umin) umin = p[3]; if (p[3] > umax) umax = p[3]; }
	float n = floorf(umax - umin + 0.5f); if (rmin < 1 || rmax > rmin * 1.005f || n < 1 || fabsf(umax - umin - n) > 0.02f) return;
	for (int sg = -1; sg <= 1; sg += 2) {
		float k = sg * n / 6.2831853f, cs = 0, sn = 0;
		for (int i = 0; i < L->nv; i++) { const float *p = v + i * 5; float d = p[3] - k * atan2f(p[1], p[0]); cs += cosf(6.2831853f * d); sn += sinf(6.2831853f * d); }
		if (sqrtf(cs * cs + sn * sn) < 0.999f * L->nv) continue;
		L->cyl = 1; L->cr = 0.5f * (rmin + rmax); L->ck = k; L->cb = atan2f(sn, cs) / 6.2831853f;
	}
	if (!L->cyl) return;
	// v as a function of z: two (z, v) samples at the extremes must describe every vertex (a straight band)
	float z0 = 1e30f, z1 = -1e30f, v0 = 0, v1 = 0;
	for (int i = 0; i < L->nv; i++) { const float *p = v + i * 5; if (p[2] < z0) { z0 = p[2]; v0 = p[4]; } if (p[2] > z1) { z1 = p[2]; v1 = p[4]; } }
	for (int i = 0; i < L->nv && L->cyl; i++) { const float *p = v + i * 5; float e = z1 > z0 ? v0 + (v1 - v0) * (p[2] - z0) / (z1 - z0) : v0; if (fabsf(e - p[4]) > 1e-3f) L->cyl = 0; }
	L->zv[0] = z0; L->zv[1] = v0; L->zv[2] = z1; L->zv[3] = v1;
}
static void w_sky_pairs(void) {   // same texture, tint, fog, band: the later one is drawn by the earlier one
	int n = 0;
	for (int i = 0; i < world_sky_layers; i++) { WSkyLayer *A = &w_sky[i]; if (!A->cyl || A->merged || A->pair >= 0 || (A->flags & (WSKY_ADD | WSKY_FAR)) != (WSKY_ADD | WSKY_FAR) || A->fog > 0 || A->scroll[0] || A->scroll[1]) continue;
		for (int j = i + 1; j < world_sky_layers; j++) { WSkyLayer *B = &w_sky[j];
			if (!B->cyl || B->merged || B->pair >= 0 || B->flags != A->flags || B->tex != A->tex || memcmp(B->tint, A->tint, 16) || B->fog > 0 || B->scroll[0] || B->scroll[1]) continue;
			if (fabsf(B->cr - A->cr) > A->cr * 0.005f || fabsf(B->ck - A->ck) > 1e-4f) continue;
			int same = 1; for (int q = 0; q < 4; q++) if (fabsf(B->zv[q] - A->zv[q]) > (q & 1 ? 1e-3f : 1.0f)) same = 0;
			if (!same) continue;
			A->pair = j; B->merged = 1; n++; break; } }
	if (n && w_log) w_log("world: %d pairs of sky bands drawn in one pass each\n", n);
}
static int w_load_sky(const char *path) {
	if (w_open(path) < 0) return 0;   // no sky for this map (indoor maps): the fog colour stays as the background
	char mg[4]; unsigned ver = 0, nl = 0, nt = 0;
	if (w_read(mg, 4) != 4 || memcmp(mg, "GSKY", 4) || w_read(&ver, 4) != 4 || w_read(&nl, 4) != 4 || w_read(&nt, 4) != 4 || ver != 1 || nl > 64 || nt > 64) { w_close(); return -1; }
	w_sky_tex = calloc(nt ? nt : 1, sizeof(GLuint)); glGenTextures(nt, w_sky_tex); w_sky_ntex = nt; w_sky_flat = calloc(nt ? nt : 1, sizeof(*w_sky_flat));
	for (unsigned i = 0; i < nt; i++) { unsigned meta = 0; if (w_load_tex(w_sky_tex[i], NULL, &meta) < 0) { w_close(); return -2; }
		if (!w_tex_flat(meta, (const unsigned char *)w_buf, w_sky_flat[i])) w_sky_flat[i][3] = -1;
#ifdef TC_NAME
		TC_NAME(w_sky_tex[i], "sky tex %u", i);
#endif
	}
	w_sky = calloc(nl ? nl : 1, sizeof(WSkyLayer));
	for (unsigned i = 0; i < nl; i++) {
		WSkyLayer *L = &w_sky[i];
		if (w_read(&L->tex, 4) != 4 || w_read(&L->flags, 4) != 4 || w_read(L->tint, 16) != 16 || w_read(L->scroll, 8) != 8 || w_read(&L->rotz, 4) != 4 || w_read(&L->fog, 4) != 4 ||
		    w_read(&L->nv, 4) != 4 || w_read(&L->ni, 4) != 4) { w_close(); return -3; }
		void *v = malloc(L->nv * 20), *ix = malloc(L->ni * 2 + 2);
		if (!v || !ix || w_read(v, L->nv * 20) != (size_t)L->nv * 20 || w_read(ix, L->ni * 2) != (size_t)L->ni * 2) { free(v); free(ix); w_close(); return -4; }
		if (L->ni & 1) { short pad; w_read(&pad, 2); }
		w_sky_cyl(L, (const float *)v);
		glGenBuffers(1, &L->vbo); glBindBuffer(GL_ARRAY_BUFFER, L->vbo); glBufferData(GL_ARRAY_BUFFER, L->nv * 20, v, GL_STATIC_DRAW);
		glGenBuffers(1, &L->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, L->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, L->ni * 2, ix, GL_STATIC_DRAW);
		free(v); free(ix); world_sky_layers = i + 1;
	}
	w_close(); w_sky_pairs(); return world_sky_layers;
}
// loads <dir><stem>.gmap and, when present, <dir><stem>.gsky. Returns 0 ok, <0 when the map is missing or broken.
static int world_load(const char *dir, const char *stem) {
	char p[200]; LDP_PUSH(LDP_UNLOAD); world_unload(); LDP_POP(); world_uv_rebased_meshes = world_uv_added = 0;   // perf (b45): phases
#ifdef FT_TAGS
	ft_tag = -1; ft_skip_after = NULL;   // (a probe cut short by a map change)
#endif
#ifdef FT_TAGS
	ft_set_unlock(0);   // b41: a map left inside a probe window gets vsync back
#endif
	w_probe_aim = -1;   // b43
	snprintf(w_stem, sizeof(w_stem), "%s", stem); w_pt0 = 0; w_pdone = 0; w_pdone2 = 0; w_pframe2 = 0; w_pframe = 0; w_pprev = -1; w_pdone3 = 0; w_pframe3 = 0; w_pframe4 = 0; w_cidx[0] = w_cidx[1] = 0; w_cn[0] = w_cn[1] = 0; memset(w_pms, 0, sizeof(w_pms)); memset(w_pn, 0, sizeof(w_pn));
	w_base_set = w_nsets >= 2 && w_is_poly_map(stem);
	w_set = w_base_set;
#ifdef TC_EVENT
	TC_EVENT("map load %s", stem);
#endif
	if (!w_neutral) { w_neutral = w_tex4(128, 128, 128); w_white = w_tex4(255, 255, 255); }
	snprintf(p, sizeof(p), "%s%s.gmap", dir, stem);
	if (w_open(p) < 0) { if (w_log) w_log("world: open fail %s\n", p); return -1; }
	char mg[4]; unsigned ver = 0, nt = 0, nm = 0;
	if (w_read(mg, 4) != 4 || memcmp(mg, "GMAP", 4) || w_read(&ver, 4) != 4 || w_read(&nt, 4) != 4 || w_read(&nm, 4) != 4 || (ver != 1 && ver != 2)) { w_close(); if (w_log) w_log("world: bad header %s\n", p); return -2; }
	world_version = ver; world_ntex = nt; w_tex_skipped = w_tex_big = 0;
	w_tex = calloc(nt ? nt : 1, sizeof(GLuint)); glGenTextures(nt, w_tex); w_tex_bytes = calloc(nt ? nt : 1, 4); w_tex_sum = calloc(nt ? nt : 1, 4); w_tex_ptr = calloc(nt ? nt : 1, sizeof(void *));
	w_tex_meta = calloc(nt ? nt : 1, 4);
	LDP_PUSH(LDP_WTEX);
	for (unsigned i = 0; i < nt; i++) {
		if ((i & 15) == 0 && world_progress) world_progress(0.05f + 0.55f * i / (nt ? nt : 1), "LOADING TEXTURES");
		if (w_load_tex(w_tex[i], &w_tex_bytes[i], &w_tex_meta[i]) < 0) { LDP_POP(); w_close(); if (w_log) w_log("world: texture %u truncated\n", i); return -3; }
#ifdef TC_NAME
		TC_NAME(w_tex[i], "%.12s tex %u", stem, i);   // texcanary.h labels
#endif
	}
	LDP_POP();
#ifdef TC_NAME
	tc_srclog(stem, w_tex, (int)nt);   // texcanary.h: source hash of every texture as read (texsrc_check.py) + the upload checks
#endif
	w_mesh = calloc(nm ? nm : 1, sizeof(WMeshW)); world_nmesh = 0; w_nflat_test = 0; w_ngpatch = 0; w_gcn = 0;
	{ FILE *f = fopen("ux0:data/gitsfa/noenvmap", "rb"); w_env_on = f == NULL; if (f) fclose(f); }   // b67: test switch (A/B)
	{ FILE *f = fopen("ux0:data/gitsfa/envfixed", "rb"); w_env_fixed = f != NULL; if (f) fclose(f); }   // b67: test switch (emulator)
	{ FILE *f = fopen("ux0:data/gitsfa/envpv", "rb"); w_env_pv_only = f != NULL; if (f) fclose(f); }   // b110: test switch: the per-vertex reflection programs only
	{ FILE *f = fopen("ux0:data/gitsfa/envgov", "rb"); w_env_gov = f != NULL; if (f) fclose(f); }   // b111: test switch: b110's governor (a late frame takes the cube kind away for a while)
	w_env_px = 1; w_env_px_offs = w_env_px_big = 0; w_env_px_late = w_env_pv_late = 0; w_env_px_okt = w_env_px_ont = 0; w_env_px_wait = 10; w_env_px_now = w_env_px_frames = 0;   // b110
	w_env_meshes = w_env_tris = w_env_late = 0; w_env_px_draws = 0; w_env_range = w_env_min_seen = w_env_fixed ? W_ENV_MAX : W_ENV_START; w_env_ceil = W_ENV_MAX;
	{ FILE *f = fopen("ux0:data/gitsfa/nochunkcull", "rb"); w_chunk_on = f == NULL; if (f) fclose(f); }   // render b45
	{ FILE *f = fopen("ux0:data/gitsfa/bufpool", "rb"); w_seg_on = f != NULL; if (f) fclose(f); }   // perf (b46): shared buffers, a test switch (above)
	{ FILE *f = fopen("ux0:data/gitsfa/bufpool.txt", "rb"); unsigned a, b, c; if (f) { int n = fscanf(f, "%u %u %u", &a, &b, &c); if (n >= 2) { w_seg_v = a << 10; w_seg_i = b << 10; w_seg_on = 1; } if (n == 3 && c && !(c & (c - 1))) w_seg_al = c; fclose(f); } }   // perf (b46): test sizes
	LDP_PUSH(LDP_WMESH);
	w_gchk_load(dir, stem, nm);   // perf (b46): the precomputed chunk split, if there is one for this map
	for (unsigned i = 0; i < nm; i++) {
		WMeshW *m = &w_mesh[i]; int ok;
		if (ver == 1) {
			int a = 0; ok = w_read(&m->tex, 4) == 4 && w_read(&m->lm, 4) == 4 && w_read(&a, 4) == 4 && w_read(&m->nv, 4) == 4 && w_read(&m->ni, 4) == 4;
			m->glow = -1; m->mode = a ? WMODE_TEST : WMODE_OPAQUE; m->flags = a ? WFLAG_TWO : 0; m->ref = 0.5f; m->glowk = 0; m->tint[0] = m->tint[1] = m->tint[2] = m->tint[3] = 1;
		} else {
			ok = w_read(&m->tex, 4) == 4 && w_read(&m->lm, 4) == 4 && w_read(&m->glow, 4) == 4 && w_read(&m->mode, 4) == 4 && w_read(&m->flags, 4) == 4 &&
			     w_read(&m->nv, 4) == 4 && w_read(&m->ni, 4) == 4 && w_read(&m->ref, 4) == 4 && w_read(&m->glowk, 4) == 4 && w_read(m->tint, 16) == 16;
			m->flags &= ~(WFLAG_FLAT | WFLAG_GPATCH);   // (b67: a load-time flag; the file's bit 32 is never set. b117: nor 128)
			if (ok && (m->flags & WFLAG_ENV)) ok = w_read(&m->env, 4) == 4 && w_read(&m->envk, 4) == 4;   // b67
		}
		if (!(m->flags & WFLAG_ENV)) { m->env = -1; m->envk = 0; }
		ok = ok && w_read(m->mn, 12) == 12 && w_read(m->mx, 12) == 12;
		if (!ok || m->nv <= 0 || m->nv > 65536 || m->ni <= 0) { LDP_POP(); w_seg_end(i); w_close(); if (w_log) w_log("world: mesh %u truncated\n", i); return -4; }
		int stride = 7 + !!(m->flags & WFLAG_VCOL) + !!(m->flags & WFLAG_ENV), vb = m->nv * stride * 4, ib = m->ni * 2, reb = !(m->flags & WFLAG_REB);   // (b67: + the env slot)
		// perf (b46): read straight into the shared buffers' staging (a mesh re-based below: its own memory, copied in after)
		unsigned voff = 0, ioff = 0; m->vbo = m->ibo = 0;
		unsigned char *sv = reb ? NULL : w_seg_take(&w_sv, GL_ARRAY_BUFFER, (unsigned)vb, (int)i, &voff, 1);
		unsigned char *si = w_seg_take(&w_si, GL_ELEMENT_ARRAY_BUFFER, (unsigned)ib + 2, (int)i, &ioff, 0);
		void *v = sv ? (void *)sv : malloc(vb), *ix = si ? (void *)si : malloc(ib + 2);
		if (!v || !ix || w_read(v, vb) != (size_t)vb || w_read(ix, ib) != (size_t)ib) { LDP_POP(); if (!sv) free(v); if (!si) free(ix); w_seg_end(i); w_close(); return -5; }
		if (m->ni & 1) { short pad; w_read(&pad, 2); }
		uint64_t mh = 0;   // perf (b46): what the precomputed split was made from
#ifdef W_GCHK_GEN
		mh = w_mesh_hash(m, v, (size_t)vb, ix, (size_t)ib);
#else
		if (w_gk.d) { LDP_PUSH(LDP_CHUNK); mh = w_mesh_hash(m, v, (size_t)vb, ix, (size_t)ib); LDP_POP(); }
#endif
		if (reb) { float *fv = v; m->nv = w_uv_rebase(&fv, m->nv, ix, m->ni, stride); v = fv; vb = m->nv * stride * 4; }
		if (m->tex >= (int)nt) m->tex = -1;
		if (m->lm >= (int)nt) m->lm = -1;
		if (m->glow >= (int)nt) m->glow = -1;
		if (m->env >= (int)nt) { m->env = -1; m->envk = 0; }   // b67
		if (m->env >= 0) { w_env_meshes++; w_env_tris += m->ni / 3; }
		m->envpx = 0;
		if (m->env >= 0) {   // b110: a triangle edge over 2 m: the per-pixel reflection is worth its cost here (w_prog_for)
			const float *fv = v; const unsigned short *ii = ix;
			for (int t = 0; t + 2 < m->ni && !m->envpx; t += 3) for (int e = 0; e < 3; e++) { const float *a = fv + (size_t)ii[t + e] * stride, *b = fv + (size_t)ii[t + (e + 1) % 3] * stride;
				const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2]; if (dx * dx + dy * dy + dz * dz > 200.f * 200.f) { m->envpx = 1; w_env_px_big++; break; } }
		}
		// b59 (user: broken concrete and weeds on the ground of Ghost Storage flickered as the camera moved): flat blended ground
		// decals wrote depth - their transparent texels too - so each hid the others' pixels in whatever order that frame's
		// distance sort put them; they write no depth now and are drawn first among the blended meshes, in the file's order
		// b105 (user: "glitchy ... broke concrete floor on ghost port that flickers when you look or move around it that ... we had
		// already fixed previously, same thing happens with these ... rectangular sewer lids they sort of flicker and disappear
		// when you move and look around them specially at a distance"): b59's test was the MESH's height (under 20 cm). The
		// maps' files join decals of one texture whatever floor they lie on: Urban's lids are one mesh on the street (z -5.6)
		// and on the pavement (z 15.2) - 20.8 cm, so not "flat": sorted by its box's distance with the star tiles and the
		// tyre marks it lies on (their meshes: 20.5-22.8 cm), a lid went under them whenever the order turned over; Ghost
		// Storage's concrete has the same in two meshes (1.0-88.7 and -222..1 cm). The test is now each TRIANGLE: a blended
		// mesh is a ground decal when nine tenths of its triangles lie level (under 1 cm of height each).
		{ int nt_ = m->ni / 3, fl_ = 0; const float *vf = (const float *)v; const unsigned short *xi = (const unsigned short *)ix;
		  if (m->mode == WMODE_BLEND) for (int t_ = 0; t_ < nt_; t_++) { float z0 = vf[xi[t_ * 3] * stride + 2], z1 = vf[xi[t_ * 3 + 1] * stride + 2], z2 = vf[xi[t_ * 3 + 2] * stride + 2];
			float lo = fminf(z0, fminf(z1, z2)), hi = fmaxf(z0, fmaxf(z1, z2)); fl_ += hi - lo < 1.0f; }
		  if (m->mode == WMODE_BLEND && nt_ > 0 && fl_ * 10 >= nt_ * 9 && (m->mx[0] - m->mn[0]) + (m->mx[1] - m->mn[1]) > 150) m->flags |= WFLAG_NOZW | WFLAG_FLAT; }
		// b117 (the user's screenshot of the patch, after b116 had treated the wrong meshes): the patch is ghost_storage's broken
		// concrete with the rebar grid (texture 17; meshes 260 and 261: 401 and 786 triangles, BLENDED with depth written, a relief
		// of -2 to +12 cm whose flat part lies 1.35 cm over the yard floor and whose faded rim dips under it). 35 % of its triangles
		// are level, so it never was a "ground decal": it was sorted by distance among the other blended meshes. The yard's dark
		// stain (mesh 254: blended, no depth, 1.0 cm, under the patch on the PC) overlaps both patches and came before or after
		// them by where the player stood - after them it lay OVER the patch, more so since b109 gave it the decals' bias (a slope
		// step is about 1 cm of height at 5 m, 2 cm at 10 m: more than the 0.35 cm between the two). Such a mesh (blended, depth
		// written, at most 30 cm high, wider than 1.5 m, not a flat decal) is a GROUND PATCH now: drawn right after the flat decals,
		// in file order, with twice the decals' bias, its depth written - whatever comes later lies under it, from every side.
		// Low and wide is not enough (the same test finds awnings, frames under ceilings and the VR rooms' panels on other maps,
		// whose back-to-front order must stay): the candidates keep up to 4 of their level triangles' centres, and
		// world_ground_patches() (main.c calls it when the collision mesh is loaded) keeps those with a floor right under them.
		if (m->mode == WMODE_BLEND && !(m->flags & (WFLAG_FLAT | WFLAG_NOZW | WFLAG_ADD)) && m->mx[2] - m->mn[2] <= 30 && (m->mx[0] - m->mn[0]) + (m->mx[1] - m->mn[1]) > 150 && w_gcn < W_GCMAX) {
			const float *vf = (const float *)v; const unsigned short *xi = (const unsigned short *)ix; int nt_ = m->ni / 3, nl = 0, lv[4], cnt = 0;
			for (int t_ = 0; t_ < nt_; t_++) { float z0 = vf[xi[t_ * 3] * stride + 2], z1 = vf[xi[t_ * 3 + 1] * stride + 2], z2 = vf[xi[t_ * 3 + 2] * stride + 2]; if (fmaxf(z0, fmaxf(z1, z2)) - fminf(z0, fminf(z1, z2)) < 1.0f) nl++; }
			for (int t_ = 0, seen = 0; t_ < nt_ && cnt < 4 && nl > 0; t_++) { float z0 = vf[xi[t_ * 3] * stride + 2], z1 = vf[xi[t_ * 3 + 1] * stride + 2], z2 = vf[xi[t_ * 3 + 2] * stride + 2]; if (fmaxf(z0, fmaxf(z1, z2)) - fminf(z0, fminf(z1, z2)) >= 1.0f) continue;
				if (seen++ == (cnt * nl) / 4) lv[cnt++] = t_; }   // the first level triangle of each quarter of them
			if (cnt) { w_gc[w_gcn].mesh = i; w_gc[w_gcn].n = cnt;
				for (int k = 0; k < cnt; k++) for (int c = 0; c < 3; c++) w_gc[w_gcn].p[k][c] = (vf[xi[lv[k] * 3] * stride + c] + vf[xi[lv[k] * 3 + 1] * stride + c] + vf[xi[lv[k] * 3 + 2] * stride + c]) / 3;
				w_gcn++; } }
		// b116 (user: "there's still this small patch of broken concrete on the floor in the map ghost storage that seems to glitch and
		// flicker as you move or look around it"): the patch is not one of b59's / b105's BLENDED decals. It is the white debris
		// (ghost_storage mesh 249, 54 triangles 1.23 cm over the yard's floor) - an ALPHA-TESTED decal, as the painted lines and the
		// manhole under it are (mesh 250, 0.84-2.11 cm). Those were drawn in the opaque pass with plain depth: 1.2 cm over a floor seen
		// from standing height is 1.2 cm x height / distance of depth - 0.07 cm at 30 m, about one step of the depth buffer there -
		// and the debris lies 0.1-0.9 cm over or under the lines. They are ground decals too now: the same level-triangles test,
		// depth still written (their texels are cut out, not blended), drawn after the opaque meshes with a depth bias, lowest
		// first (w_draw_world: w_fd). (b117: NOT the patch the user meant - see above. Kept: these decals are 0.1-0.9 cm apart.)
		{ int nt_ = m->ni / 3, fl_ = 0; float zs_ = 0; const float *vf = (const float *)v; const unsigned short *xi = (const unsigned short *)ix;
		  if (m->mode == WMODE_TEST) for (int t_ = 0; t_ < nt_; t_++) { float z0 = vf[xi[t_ * 3] * stride + 2], z1 = vf[xi[t_ * 3 + 1] * stride + 2], z2 = vf[xi[t_ * 3 + 2] * stride + 2];
			float lo = fminf(z0, fminf(z1, z2)), hi = fmaxf(z0, fmaxf(z1, z2)); if (hi - lo < 1.0f) { fl_++; zs_ += lo; } }
		  m->fz = fl_ ? zs_ / fl_ : 0;
		  if (m->mode == WMODE_TEST && nt_ > 0 && fl_ * 10 >= nt_ * 9 && (m->mx[0] - m->mn[0]) + (m->mx[1] - m->mn[1]) > 150) { m->flags |= WFLAG_FLAT; w_nflat_test++; } }
		LDP_PUSH(LDP_CHUNK);   // perf (b46): its own phase
		if (!w_gchk_apply(m, (unsigned short *)ix, mh))   // perf (b46): precomputed (tools/gchk), else as render b45:
			w_split_mesh(m, (const float *)v, stride, (unsigned short *)ix);   // render b45: chunks (sorts ix by chunk)
		LDP_POP();
#ifdef W_GCHK_GEN
		w_gen_mesh((int)i, m, mh, (const unsigned short *)ix);
#endif
		if (m->mode == WMODE_OPAQUE && !(m->flags & WFLAG_NOZW)) oc_add_mesh((const float *)v, stride, (const unsigned short *)ix, m->ni, (m->flags & WFLAG_TWO) != 0);   // b60: its big triangles occlude (occl.h)
#ifdef W_MESH_HOOK   // (host tests: the mesh as loaded, indices in chunk order)
		W_MESH_HOOK((int)i, m, (const float *)v, stride, (const unsigned short *)ix);
#endif
		if (!sv) {   // perf (b46): into the staging now, or its own buffer (bigger than a segment)
			unsigned char *d = w_seg_take(&w_sv, GL_ARRAY_BUFFER, (unsigned)vb, (int)i, &voff, 1);
			if (d) memcpy(d, v, vb); else { m->vbo = w_upload(GL_ARRAY_BUFFER, v, (unsigned)vb); voff = 0; }
			free(v);
		}
		if (!si) { m->ibo = w_upload(GL_ELEMENT_ARRAY_BUFFER, ix, (unsigned)ib); ioff = 0; free(ix); }
		m->voff = voff; m->ioff = ioff;
		world_tris += m->ni / 3; world_nmesh = i + 1;
	}
	w_seg_end(world_nmesh);   // perf (b46)
	LDP_POP();
	w_close();
	// b111: a cube map for each reflection picture a mesh with large triangles uses (w_cube_make); the kept level-0 copies go
	{ int nc = 0; w_envc = calloc(world_ntex ? world_ntex : 1, sizeof(GLuint));
		for (int i = 0; i < world_nmesh && w_envc; i++) { const WMeshW *m = &w_mesh[i]; if (m->env < 0 || !m->envpx || w_envc[m->env]) continue;
			for (int c = 0; c < w_cstn; c++) if (w_cst[c].t == w_tex[m->env]) { w_envc[m->env] = w_cube_make(w_cst[c].d); nc += w_envc[m->env] != 0; break; } }
		for (int c = 0; c < w_cstn; c++) free(w_cst[c].d);
		if (w_log && w_env_meshes) w_log("world env: %d cube map(s) of %d px a face from the reflection pictures (%d candidates kept at load; b111)\n", nc, W_CUBE_N, w_cstn);
		w_cstn = 0; w_envc_made = nc; }
	if (w_log && w_nflat_test) w_log("world: %d alpha-tested ground decals (drawn after the opaque meshes with the decals' depth bias, b116)\n", w_nflat_test);
	oc_finish(); if (w_log) w_log("world: %d occluders (opaque triangles of 1 m2 and more) in %d cells%s\n", oc_ntri, oc_ncell, oc_on ? "" : " (occlusion culling off: noocc switch)");   // b60
	if (w_log && w_gk.nmesh) w_log("world: chunk split from %s.gchk for %d of %d meshes (%d did not match: split here)\n", stem, w_gk.used, world_nmesh, w_gk.missed);   // perf (b46)
	if (w_log && w_seg_on) w_log("world: %d GL buffers for %d meshes (perf b46 bufpool switch: vertex segments %u KB, index %u KB)\n", w_nbufs, world_nmesh, w_seg_v >> 10, w_seg_i >> 10);   // perf (b46)
	free(w_gk.d); w_gk.d = NULL;
	w_order = malloc(sizeof(int) * (world_nmesh + 1)); w_dist = malloc(sizeof(float) * (world_nmesh + 1));
	snprintf(p, sizeof(p), "%s%s.gsky", dir, stem); LDP_PUSH(LDP_SKY); int sl = w_load_sky(p); LDP_POP();
#ifdef TC_DXT_LOG
	TC_DXT_LOG(stem);   // b41: texcanary.h's compressed-upload counts (staging alignment, CPU rewrites) up to this map's sky
#endif
	free(w_buf); w_buf = NULL; w_bufsz = 0;   // upload scratch back to the heap
	{ FILE *f = fopen("ux0:data/gitsfa/noaniso", "rb"); w_aniso_on = f == NULL; if (f) fclose(f); }   // b69: test switch (A/B)
	free(w_aniso_mark); w_aniso_mark = calloc(world_ntex ? world_ntex : 1, 1); w_aniso_n = w_aniso_bad = w_aniso_offs = w_aniso_cur = w_aniso_pwin = 0; w_aniso_okt = 0;
	if (w_aniso_mark) { for (int i = 0; i < world_nmesh; i++) { int tx = w_mesh[i].tex; if (tx >= 0 && tx < world_ntex && (w_tex_meta[tx] >> 28) > 1 && !w_aniso_mark[tx]) { w_aniso_mark[tx] = 1; w_aniso_n++; } }   // (b69: base textures with mips)
	  glActiveTexture(GL_TEXTURE0); if (w_aniso_on) w_aniso_set(1); }
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0);
#ifdef HG_HOST
	if (w_log) w_log("WORLDHASH %s: %08x over %llu bytes (map + sky as read)\n", stem, w_bhash, w_bcount);   // perf (b45): host test
	w_bhash = 2166136261u; w_bcount = 0;
#endif
	if (w_log) w_log("world: %s v%u, %d textures, %d meshes, %ld tris; sky: %s (%d layers); uv rebased in %d meshes (+%d vertices)\n", stem, ver, world_ntex, world_nmesh, world_tris,
		sl > 0 ? "yes" : sl == 0 ? "none" : "BROKEN", world_sky_layers, world_uv_rebased_meshes, world_uv_added);
	if (w_log) { unsigned long tb = 0; for (int i = 0; i < world_ntex; i++) tb += w_tex_bytes[i];
		w_log("world: textures %.1f MB; %u base textures at 512 px%s (b61)\n", tb / 1048576.0, w_tex_big, w_tex_skipped ? ", loaded at 256 px (tex256 test switch)" : "");
		w_log("world: anisotropic filtering %s on %d base textures (%d left trilinear: rejected by sceGxmTextureValidate) (b69)\n", w_aniso_on ? "on" : "OFF (noaniso)", w_aniso_n - w_aniso_bad, w_aniso_bad);
		static const char *VN[5] = { "min aniso", "min + mag aniso", "min aniso + bit 14", "min + mag aniso + bit 14", "min aniso-point" };
		for (int v = 0; v < w_aniso_ntry && v < 5; v++) w_log("world aniso variant %d (%s): SetMinFilter %08x, SetMagFilter %08x, Validate %08x%s (b70)\n", v, VN[v],
			(unsigned)w_aniso_rc[v][0], (unsigned)w_aniso_rc[v][1], (unsigned)w_aniso_rc[v][2], v == w_aniso_var ? " -> used" : "");
		w_aniso_ntry = 0; }
	if (w_log && w_env_meshes) w_log("world env: %d meshes (%dk triangles) with the map's environment map%s, programs %s (b67)\n", w_env_meshes, w_env_tris / 1000,
		w_env_on ? "" : " - off (noenvmap)", w_prog[w_set * WP_N + 18] ? "ok" : "MISSING (drawn without)");
	if (w_log) { int ns = 0; for (int i = 0; i < world_nmesh; i++) ns += w_mesh[i].c1 - w_mesh[i].c0 > 1;   // render b45
		w_log("world: %d chunks of 10 m (%d meshes split; blended / no-depth-write ones whole)%s\n", w_nchunk, ns, w_chunk_on ? "" : " - nochunkcull: whole meshes"); }
	{ int nlean = 0, nblend = 0, nflat = 0; for (int i = 0; i < world_nmesh; i++) if (w_mesh[i].mode == WMODE_BLEND) { nblend++; nlean += w_mesh[i].ref <= 0; }
	  for (int i = 0; i < world_sky_layers; i++) { int tx = w_sky[i].tex; nflat += tx < 0 || tx >= w_sky_ntex || (w_sky_flat && w_sky_flat[tx][3] >= 0); }
	  if (w_log) w_log("world: b35 lean drawing: %d of %d blended meshes without the alpha discard, %d of %d sky layers one colour; opaque grading %s\n",
		nlean, nblend, nflat, world_sky_layers, w_base_set ? "polynomial (w_poly_maps)" : "as built (set 0)"); }
	return 0;
}

// ---- texture check (main.c's test): where each map texture lives and whether its contents change after loading
static unsigned w_tex_hash(const unsigned char *p, unsigned n) { unsigned h = 2166136261u; for (unsigned i = 0; i + 4 <= n; i += 512) { unsigned w; memcpy(&w, p + i, 4); h = (h ^ w) * 16777619u; } return h; }   // sparse: GPU memory is slow to read (b31: the full-rate pass at 12 s cost a frame on the big maps)
static int w_tex_ord_cmp(const void *a, const void *b) { const unsigned char *pa = w_tex_ptr[*(const int *)a], *pb = w_tex_ptr[*(const int *)b]; return pa < pb ? -1 : pa > pb; }
static void world_tex_check(int again) {
	int changed = 0, overl = 0, nulls = 0;
	for (int i = 0; i < world_ntex; i++) {
		glBindTexture(GL_TEXTURE_2D, w_tex[i]); unsigned char *p = vglGetTexDataPointer(GL_TEXTURE_2D);
		if (!p) { nulls++; continue; }
		unsigned h = w_tex_hash(p, w_tex_bytes[i]);
		if (!again) { w_tex_ptr[i] = p; w_tex_sum[i] = h; }
		else if (p != w_tex_ptr[i] || h != w_tex_sum[i]) { if (changed < 6 && w_log) w_log("  texture %d (%u bytes) %s since loading\n", i, w_tex_bytes[i], p != w_tex_ptr[i] ? "moved" : "CHANGED"); changed++; }
	}
	if (!again) {
		int *ord = malloc(sizeof(int) * (world_ntex + 1)); for (int i = 0; i < world_ntex; i++) ord[i] = i; qsort(ord, world_ntex, sizeof(int), w_tex_ord_cmp);
		for (int k = 1; k < world_ntex; k++) { int a = ord[k - 1], b = ord[k]; if (w_tex_ptr[a] && w_tex_ptr[a] + w_tex_bytes[a] > w_tex_ptr[b]) { if (overl < 6 && w_log) w_log("  textures %d and %d OVERLAP in memory\n", a, b); overl++; } }
		free(ord);
		if (w_log) w_log("texture check: %d textures, %d overlapping, %d without data\n", world_ntex, overl, nulls);
	} else if (w_log) w_log("texture check after play: %d of %d textures changed, %d without data (deleted behind the world's back)\n", changed, world_ntex, nulls);
	glBindTexture(GL_TEXTURE_2D, 0);
}

// test runs: the map's textures exactly as the GPU holds them (swizzled) -> path ('TXD1', n, per texture: meta, bytes, 0, data)
static void __attribute__((unused)) world_tex_dump(const char *path) {
	FILE *f = fopen(path, "wb"); if (!f) return;
	unsigned hdr[2] = { 0x31445854, (unsigned)world_ntex }; fwrite(hdr, 4, 2, f); long tot = 0;
	for (int i = 0; i < world_ntex; i++) { glBindTexture(GL_TEXTURE_2D, w_tex[i]); unsigned char *d = vglGetTexDataPointer(GL_TEXTURE_2D);
		unsigned m[3] = { w_tex_meta[i], d ? w_tex_bytes[i] : 0, 0 }; fwrite(m, 4, 3, f); if (d) { fwrite(d, 1, w_tex_bytes[i], f); tot += w_tex_bytes[i]; } }
	fclose(f); glBindTexture(GL_TEXTURE_2D, 0); if (w_log) w_log("texture dump: %s (%d textures, %ld KB)\n", path, world_ntex, tot / 1024);
}

// ---- drawing
static int w_visible(const float *mvp, const float *mn, const float *mx) {
	for (int p = 0; p < 6; p++) {
		int r = p >> 1; float s = (p & 1) ? -1.f : 1.f, pl[4];
		for (int i = 0; i < 4; i++) pl[i] = mvp[i * 4 + 3] + s * mvp[i * 4 + r];
		float x = pl[0] >= 0 ? mx[0] : mn[0], y = pl[1] >= 0 ? mx[1] : mn[1], z = pl[2] >= 0 ? mx[2] : mn[2];
		if (pl[0] * x + pl[1] * y + pl[2] * z + pl[3] < 0) return 0;
	}
	return 1;
}
static void w_set_mvp(int pi, const float *M) { for (int r = 0; r < 4; r++) { float row[4] = { M[r], M[4 + r], M[8 + r], M[12 + r] }; glUniform4fv(w_um[pi][r], 1, row); } }
// b67: the env programs for a mesh with an environment map whose box is within w_env_range of the eye (the adaptive range,
// world_draw: shrinks when frames run late, grows back when they do not); else the plain programs (the b66 look)
static int w_env_near(const WMeshW *m) {
	float R = w_env_pwin ? (w_env_poff ? 0 : W_ENV_MAX) : w_env_range;   // (the probe's window: on at the full range / off)
	if (!w_env_on || m->env < 0 || R <= 0) return 0;
	float d2 = 0; for (int j = 0; j < 3; j++) { float v = w_eye[j] < m->mn[j] ? m->mn[j] - w_eye[j] : w_eye[j] > m->mx[j] ? w_eye[j] - m->mx[j] : 0; d2 += v * v; }
	return d2 < R * R;
}
static int w_prog_for(const WMeshW *m) {
	if (w_net) return W_NETSET * WP_N + (m->mode == WMODE_BLEND ? 2 : m->mode == WMODE_TEST ? 1 : 0);   // b84
	int g = world_glow_on && m->glow >= 0 && m->glowk > 0, b = w_set * WP_N;
	if ((m->flags & WFLAG_ENV) && !(m->flags & WFLAG_VCOL) && w_env_near(m)) {   // b67
		int k = m->mode == WMODE_BLEND ? (m->ref <= 0 ? 22 : 21) : m->mode == WMODE_TEST ? 19 : g ? 20 : 18;
		const int px = m->envpx && (w_env_px || !w_env_gov) && !w_env_pv_only && w_prog[b + k] && w_envc && w_envc[m->env];   // (b111: the cube kind; the governor only with the test switch envgov)   // b110: per pixel while frames are on time, else b108's per vertex (k + 5)
		if (!px && w_prog[b + k + 5]) k += 5;
		if (w_prog[b + k] && !(m->mode != WMODE_OPAQUE && g)) { w_env_px_draws++; if (k < 23) w_env_px_now++; return b + k; }
	}
	if (m->mode == WMODE_BLEND) { int k = (m->flags & WFLAG_VCOL) ? 6 : g ? 5 : 2;
		if (m->ref <= 0) k = (k == 2 ? 9 : k == 5 ? 10 : 11) + (w_fog_off ? 5 : 0);   // b35 lean: no discard (cannot fail), no fog (amount 0)
		return b + k; }
	if (m->mode == WMODE_TEST) return b + (g ? 4 : 1);
	return b + (g ? 3 : 0);
}
typedef struct { int prog, tex, lm, glow, cull, zw, blend, vcol, nrm, envt, envc; float tint[4], ref, glowk, envk; } WState;   // (b67 nrm, envt, envk)
static float w_vr[3][4];   // b67: this frame's view rotation rows (world_draw, from the MVP)
static void w_bind_mesh(WState *st, const WMeshW *m, int pi, const float *MVP, const float *fog, const float *fogp, int lmon) {
	if (pi != st->prog) {
		st->prog = pi; glUseProgram(w_prog[pi]); w_set_mvp(pi, MVP);
		if (w_unetk[pi] >= 0) { static const float nf[4] = { 0.0f, 0.012f, 0.016f, 1 }, np[4] = { 1500, 0.0005f, 0.85f, 0 };   // b84: the network view's dark distance
			glUniform4fv(w_ufogc[pi], 1, nf); glUniform4fv(w_ufogp[pi], 1, np); glUniform4fv(w_unetk[pi], 1, w_netk); }
		else { glUniform4fv(w_ufogc[pi], 1, fog); glUniform4fv(w_ufogp[pi], 1, fogp); }
		st->tint[0] = -1; st->ref = -1; st->glowk = -1; st->envk = -1;
		if (w_ueye[pi] >= 0) for (int r = 0; r < 3; r++) glUniform4fv(w_uvr[pi][r], 1, w_vr[r]);   // b67
	}
	int envp = w_ueye[pi] >= 0;   // b67: an env program
	if (envp && st->envk != m->envk) { float e[4] = { w_eye[0], w_eye[1], w_eye[2], m->envk }; st->envk = m->envk; glUniform4fv(w_ueye[pi], 1, e); }
	if (envp && pi % WP_N < 23) { if (m->env != st->envc) { st->envc = m->env; glActiveTexture(GL_TEXTURE6); (glBindTexture)(GL_TEXTURE_CUBE_MAP, w_envc ? w_envc[m->env] : 0); } }   // b111: the cube kind
	else if (envp && m->env != st->envt) { st->envt = m->env; glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, w_tex[m->env]); }
	int two = (m->flags & WFLAG_TWO) != 0; if (two != !st->cull) { if (two) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE); st->cull = !two; }
	int zw = !(m->flags & WFLAG_NOZW); if (zw != st->zw) { glDepthMask(zw ? GL_TRUE : GL_FALSE); st->zw = zw; }
	int bl = m->mode == WMODE_BLEND ? ((m->flags & WFLAG_ADD) ? 2 : 1) : 0;
	if (bl != st->blend) { if (!bl) glDisable(GL_BLEND); else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, bl == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); } st->blend = bl; }
	if (memcmp(st->tint, m->tint, 16)) { memcpy(st->tint, m->tint, 16); glUniform4fv(w_utint[pi], 1, m->tint); }
	if (st->ref != m->ref && w_uref[pi] >= 0) { st->ref = m->ref; glUniform1f(w_uref[pi], m->ref); }
	if (st->glowk != m->glowk && w_uglowk[pi] >= 0) { st->glowk = m->glowk; glUniform1f(w_uglowk[pi], m->glowk); }
	int t = m->tex; GLuint tt = t >= 0 ? w_tex[t] : w_white;
	if ((int)tt != st->tex) { st->tex = tt; glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tt); }
	GLuint lt = (lmon && m->lm >= 0) ? w_tex[m->lm] : w_neutral;
	if ((int)lt != st->lm) { st->lm = lt; glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, lt); }
	if (w_glw[pi] && m->glow >= 0 && m->glow != st->glow) { st->glow = m->glow; glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, w_tex[m->glow]); }
	int stride = 28 + ((m->flags & WFLAG_VCOL) ? 4 : 0) + ((m->flags & WFLAG_ENV) ? 4 : 0);   // (b67: + the env slot)
	glBindBuffer(GL_ARRAY_BUFFER, m->vbo); uintptr_t vo = m->voff;   // perf (b46): its offset in the shared buffer
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)vo);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void *)(vo + 12));
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, (void *)(vo + 20));
	if (m->flags & WFLAG_VCOL) { glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (void *)(vo + 28)); st->vcol = 1; }
	else if (st->vcol) { glDisableVertexAttribArray(3); st->vcol = 0; }
	if (envp) { if (!st->nrm) { glEnableVertexAttribArray(4); st->nrm = 1; } glVertexAttribPointer(4, 4, GL_BYTE, GL_TRUE, stride, (void *)(vo + stride - 4)); }   // b67: the normal slot (last)
	else if (st->nrm) { glDisableVertexAttribArray(4); st->nrm = 0; }
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo);
}
// b35: the grading of program pi, on the CPU (for the flat layers); 0 = cannot (a LUT program)
static int w_grade_cpu(int pi, const float *fc, float *gc) {
	int gm = w_gm[pi]; float x[3];
	if (gm == GRADE_OFF) { memcpy(gc, fc, 12); return 1; }
	if (gm != GRADE_AFFINE && gm != GRADE_POLY) return 0;
	for (int j = 0; j < 3; j++) x[j] = fc[j] < 0 ? 0 : fc[j] > 1 ? 1 : fc[j];
	const float (*k)[4] = grade_k[gm == GRADE_POLY ? 0 : 1];
	for (int j = 0; j < 3; j++) {
		gc[j] = k[9][j] + k[0][j] * x[0] + k[1][j] * x[1] + k[2][j] * x[2];
		if (gm == GRADE_POLY) gc[j] += k[3][j] * x[0] * x[0] + k[4][j] * x[1] * x[1] + k[5][j] * x[2] * x[2] + k[6][j] * x[0] * x[0] * x[0] + k[7][j] * x[1] * x[1] * x[1] + k[8][j] * x[2] * x[2] * x[2];
	}
	return 1;
}
static void w_draw_sky(const float *MVP, const float *cam, const float *fog, int far, float t) {
	if (!world_sky_layers) return;
	glDisable(GL_CULL_FACE); glDepthMask(GL_FALSE);
	int cur_blend = -1, cur_prog = -1;
	for (int i = 0; i < world_sky_layers; i++) {
		WSkyLayer *L = &w_sky[i]; if (!!(L->flags & WSKY_FAR) != far) continue;
		int bl = (L->flags & WSKY_ADD) ? 2 : (L->flags & WSKY_ALPHA) ? 1 : 0, k = bl ? 8 : 7, tx = L->tex >= 0 && L->tex < w_sky_ntex ? L->tex : -1;
		if (L->merged && w_prog[w_set * WP_N + 17]) continue;   // drawn with its partner
		if (L->pair >= 0 && w_prog[w_set * WP_N + 17]) {   // b35: the band and its partner in one pass (exact sum; no rotation of the mesh: the turn is a u offset)
			const WSkyLayer *B = &w_sky[L->pair]; int pi = w_set * WP_N + 17;
			if (pi != cur_prog) { cur_prog = pi; glUseProgram(w_prog[pi]); }
			float M[16]; memcpy(M, MVP, 64); for (int r = 0; r < 4; r++) M[12 + r] += M[r] * cam[0] + M[4 + r] * cam[1] + M[8 + r] * cam[2];
			w_set_mvp(pi, M);
			float oa = -L->ck * L->rotz * t, ob = B->cb - L->cb - B->ck * B->rotz * t, po[4] = { oa - floorf(oa), ob - floorf(ob), 0, 0 }, sp[4] = { 0, 0, 1, 0 };
			glUniform4fv(w_usp[pi], 1, sp); glUniform4fv(w_utint[pi], 1, L->tint); glUniform4fv(w_ufogc[pi], 1, po);
			if (cur_blend != 3) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); cur_blend = 3; }
			glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tx >= 0 ? w_sky_tex[tx] : w_white);
			glBindBuffer(GL_ARRAY_BUFFER, L->vbo);
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void *)0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, L->ibo); glDrawElements(GL_TRIANGLES, L->ni, GL_UNSIGNED_SHORT, 0);
			continue;
		}
		// b35 lean: a one-colour layer is drawn with its final colour (no texture read, no fog, no grading on the GPU); a blended layer
		// without fog skips the fog lerp
		float flat[4] = { 1, 1, 1, 1 };
		if (w_prog[w_set * WP_N + 13] && (tx < 0 || (w_sky_flat && w_sky_flat[tx][3] >= 0))) {
			float c[4], fc[3]; if (tx >= 0) memcpy(flat, w_sky_flat[tx], 16);
			for (int j = 0; j < 4; j++) c[j] = flat[j] * L->tint[j];
			for (int j = 0; j < 3; j++) fc[j] = c[j] + (fog[j] - c[j]) * L->fog;
			if (bl ? (memcpy(flat, fc, 12), 1) : w_grade_cpu(w_set * WP_N + 7, fc, flat)) { flat[3] = c[3]; k = 13; }
		}
		if (k == 8 && L->fog <= 0) k = 12;
		int pi = w_set * WP_N + k;
		if (pi != cur_prog) { cur_prog = pi; glUseProgram(w_prog[pi]); glUniform4fv(w_ufogc[pi], 1, fog); }
		float M[16], a = L->rotz * t, c = cosf(a), s = sinf(a); memcpy(M, MVP, 64);
		if (far) for (int r = 0; r < 4; r++) M[12 + r] += M[r] * cam[0] + M[4 + r] * cam[1] + M[8 + r] * cam[2];   // centred on the camera
		if (a != 0) for (int r = 0; r < 4; r++) { float x = M[r], y = M[4 + r]; M[r] = x * c + y * s; M[4 + r] = -x * s + y * c; }   // turn around z
		w_set_mvp(pi, M);
		float sp[4] = { L->scroll[0] * t, L->scroll[1] * t, far ? 1.f : 0.f, 0 }; sp[0] -= floorf(sp[0]); sp[1] -= floorf(sp[1]);
		glUniform4fv(w_usp[pi], 1, sp); glUniform4fv(w_utint[pi], 1, k == 13 ? flat : L->tint); glUniform1f(w_uref[pi], L->fog);
		if (bl != cur_blend) { if (!bl) glDisable(GL_BLEND); else { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, bl == 2 ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); } cur_blend = bl; }
		if (k != 13) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tx >= 0 ? w_sky_tex[tx] : w_white); }
		glBindBuffer(GL_ARRAY_BUFFER, L->vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, (void *)0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, (void *)12);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, L->ibo); glDrawElements(GL_TRIANGLES, L->ni, GL_UNSIGNED_SHORT, 0);
	}
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}
#ifndef GL_POLYGON_OFFSET_FILL   // (the host tools' stub GL has no depth bias: nothing to draw there)
#define GL_POLYGON_OFFSET_FILL 0x8037
#define glPolygonOffset(a, b) ((void)0)
#endif
static int w_no_bias;   // (test switch ux0:data/gitsfa/nodecalbias: the ground decals without their depth bias)
// b105: the map's blended meshes that lie wholly inside the box are not drawn (wobj.h: an animated copy of them is; NULL: all back)
static int w_hide_box(const float *bx) {
	int n = 0;
	for (int i = 0; i < world_nmesh; i++) { WMeshW *m = &w_mesh[i]; m->flags &= ~WFLAG_HIDE;
		if (!bx || m->mode != WMODE_BLEND) continue;
		if (m->mn[0] >= bx[0] - 5 && m->mn[1] >= bx[1] - 5 && m->mn[2] >= bx[2] - 5 && m->mx[0] <= bx[3] + 5 && m->mx[1] <= bx[4] + 5 && m->mx[2] <= bx[5] + 5) { m->flags |= WFLAG_HIDE; n++; } }
	return n;
}
// b117: floor(x, y, z) = the height of the collision floor under that point, searched from 25 cm above it to 20 cm below it
// (below -1e8: none). A candidate is a ground patch when at least half of its sample points have the floor 0-12 cm under them.
static void world_ground_patches(float (*floor_z)(float x, float y, float z)) {
	w_ngpatch = 0; if (!floor_z) return;
	for (int c = 0; c < w_gcn; c++) { int ok = 0; if (w_gc[c].mesh < 0 || w_gc[c].mesh >= world_nmesh) continue;
		for (int k = 0; k < w_gc[c].n; k++) { float f = floor_z(w_gc[c].p[k][0], w_gc[c].p[k][1], w_gc[c].p[k][2]); if (f > -1e8f && w_gc[c].p[k][2] - f >= -1.0f && w_gc[c].p[k][2] - f <= 12.0f) ok++; }
		if (ok * 2 >= w_gc[c].n) { w_mesh[w_gc[c].mesh].flags |= WFLAG_GPATCH; w_ngpatch++; } }
	if (w_log) w_log("world: %d of %d low blended meshes lie on a floor: ground patches (after the flat decals, twice their bias, b117)\n", w_ngpatch, w_gcn);
}
static int w_dist_cmp(const void *a, const void *b) { int ia = *(const int *)a, ib = *(const int *)b; float da = w_dist[ia], db = w_dist[ib]; return da < db ? 1 : da > db ? -1 : ia - ib; }   // (b59: ties in file order - a stable look)
// Draws the map. MVP column-major; cam = eye position (world); fog = fog colour rgba; fogp = (start cm, density 1/cm,
// max amount, 0) as in MAPS[]; lightmaps_on = 0 shows unlit textures; cull = frustum culling; t = seconds (sky motion).
// Expects depth test on (GL_LEQUAL), back-face culling, attributes 0..2 enabled. Leaves: culling on, depth writes on,
// blending off, texture unit 0 active, attribute 3 disabled. Returns the number of meshes drawn.
// ---- the grading probe (test run only): the program sets take turns every 30 frames from 10 to 20 s after the map's first
// frame; each set's mean frame time (the whole frame: GPU-bound frames show the grading's fill cost) is logged once per map
static unsigned long long w_clock_us(void) {
#ifdef __vita__
	return sceKernelGetProcessTimeWide();
#else
	return 0;
#endif
}
#ifdef GPUMODEL_H
static const GpuModelMap *w_model_for(const char *stem) { for (int i = 0; i < GPUMODEL_N; i++) if (!strcmp(gpumodel[i].stem, stem)) return &gpumodel[i]; return NULL; }
#endif
// b38 parts probe (test run, 4-10 s into every map, before the grading probe): the frame's draws of one part at a time are
// left out (frametime.h ft_skip_after: the FT mark in main.c that opens the part), 30 frames each in turn, so the GPU
// fragment ms per scene without each part give what the part costs on the GPU. The screen shows it (test runs only).
#define W_NPARTS 6
static const char *const w_part_name[W_NPARTS] = { "all", "without the world", "without characters", "without effects, drops, skills", "without the weapon", "without the HUD" };
static const char *const w_part_mark[W_NPARTS] = { NULL, "modes, tutorial, camera", "world draw", "characters, shadows, tanks", "bomb, drops, skills, range, effects", "viewmodel, HUD state" };
// b61 fix: main.c calls w_probe_frame_early() before its FT("modes, tutorial, camera") mark, the one that opens the world's
// segment: the parts probe's choice of what to leave out must be made before that mark reads it (frametime.h ft_mark). Called
// from world_draw (as before b61) the world's turn left the world out one frame late - in the "without characters" frame -
// so devices b38-b60 read "without the world" ~= all and "without characters" = world + characters (b48: 8.4 ms "characters").
static int w_probe_early;
static void w_probe_frame(void);
static void w_probe_frame_early(void) { w_probe_frame(); w_probe_early = 1; }
static void w_probe_frame(void) {
	w_set = w_base_set; w_chunk_skip = 0; w_occ_skip = 0; w_probe_fullfp = 0; w_chunk_win = -1; w_env_pwin = w_env_poff = 0; if (!w_probe || w_pdone3) return;   // (render b45: w_pdone3, the chunk window after the sets)
	unsigned long long now = w_clock_us(); if (!w_pt0) { w_pt0 = w_plast = now;
#ifdef FT_TAGS
		memset(ft_tag_ms, 0, sizeof(ft_tag_ms)); memset(ft_tag_n, 0, sizeof(ft_tag_n));
#endif
		return; }
	double t = (now - w_pt0) * 1e-6, dt = (now - w_plast) * 1e-3; w_plast = now;
	if (w_pprev >= 0 && dt < 250) { w_pms[w_pprev] += dt; w_pn[w_pprev]++; }   // the frame that just ended was drawn with set w_pprev
	w_pprev = -1;
	if (t >= 10 && t < 18.5) { w_set = w_pframe++ % w_nsets; w_pprev = w_set; }   // b43: the sets take turns frame by frame (the same views) (b67: to 18.5 s, was 20)
#ifdef FT_TAGS
	ft_set_unlock(t >= 4 && t < 21.6);   // b41: vsync off, 60 Hz paced by the CPU, through the probe windows (frametime.h; render b45: to 21.6 s)
	// b43: 4-7 s the parts take turns frame by frame with the aim held at hip; 7-10 s the aim is held down the sights
	// (settled after 0.3 s) and "all" / "without the weapon" take turns (tags 14 / 15): the weapon's cost at hip and in ADS
	// b60: at hip a seventh turn draws everything with the FULL first-person mesh (tag 4; main.c: w_probe_fullfp), to
	// tell what the light one (wpn_vm.h) saves against "all" (tag 8)
	int part = t >= 4 && t < 7 ? w_pframe2++ % (W_NPARTS + 1) : t >= 7.3 && t < 10 ? ((w_pframe2++ & 1) ? 4 : 0) : -1;
	if (part == W_NPARTS) { part = 0; w_probe_fullfp = 1; }
	w_probe_aim = t >= 4 && t < 7 ? 0 : t >= 7 && t < 10 ? 1 : -1;
	ft_tag = t >= 10 && t < 18.5 ? w_set : part >= 0 ? (t < 7 ? (w_probe_fullfp ? 4 : 8 + part) : part ? 15 : 14) : -1;   // frametime.h: the GPU fragment time of this frame's scene is booked to its tag
	// b67: the world's reflections on (tag 16) / off (17) by turns at the full range (W_ENV_MAX), on maps that have them
	// b69: a third turn with anisotropic filtering off and the reflections on (tag 18): its cost = tag 16 - tag 18
	if (t >= 18.6 && t < 20 && ((w_env_meshes && w_env_on) || (w_aniso_n && w_aniso_on))) {
		int k = w_pframe4++ % 3; if (!(w_env_meshes && w_env_on) && k == 1) k = 0; if (!(w_aniso_n && w_aniso_on) && k == 2) k = 1;
		w_env_pwin = w_env_meshes && w_env_on; w_env_poff = k == 1; ft_tag = 16 + k;
		if (w_aniso_n && w_aniso_on) { w_aniso_pwin = 1; w_aniso_set(k != 2); }
	} else if (w_aniso_pwin) { w_aniso_pwin = 0; w_aniso_set(1); }
	ft_skip_after = part > 0 ? w_part_mark[part] : NULL;
	// render b45: chunk culling on (tag 6) / off (7) by turns; b60: a third turn with the chunks on and occlusion culling off (tag 5)
	if (t >= 20.2 && t < 21.6 && w_chunk_on) { int k = w_pframe3++ % 3; w_chunk_win = k == 1; w_chunk_skip = k == 1; w_occ_skip = k == 2; ft_tag = k == 2 ? 5 : 6 + k; if (k == 2) w_chunk_win = -1; }
#endif
	if (t >= 10 && !w_pdone2) {
		w_pdone2 = 1;
#ifdef FT_TAGS
		if (w_log) { w_log("gpu parts %s (GPU fragment ms/scene, %s set, vsync off, frame-interleaved, hip):", w_stem, w_base_set ? "polynomial" : "LUT");
			for (int k = 0; k < W_NPARTS; k++) w_log(" %s%s %.2f (%u)", k ? "| " : "", w_part_name[k], ft_tag_n[8 + k] ? ft_tag_ms[8 + k] / ft_tag_n[8 + k] : 0.0, ft_tag_n[8 + k]);
			w_log(" | all with the full first-person mesh %.2f (%u)\n", ft_tag_n[4] ? ft_tag_ms[4] / ft_tag_n[4] : 0.0, ft_tag_n[4]);   // b60
			double ha = ft_tag_n[8] ? ft_tag_ms[8] / ft_tag_n[8] : 0, hw = ft_tag_n[12] ? ft_tag_ms[12] / ft_tag_n[12] : 0, aa = ft_tag_n[14] ? ft_tag_ms[14] / ft_tag_n[14] : 0, aw_ = ft_tag_n[15] ? ft_tag_ms[15] / ft_tag_n[15] : 0;
			w_log("gpu weapon %s (GPU fragment ms/scene, vsync off, frame-interleaved): hip all %.2f - without the weapon %.2f = %.2f (%u / %u) | ADS all %.2f - without %.2f = %.2f (%u / %u)\n",
				w_stem, ha, hw, ha - hw, ft_tag_n[8], ft_tag_n[12], aa, aw_, aa - aw_, ft_tag_n[14], ft_tag_n[15]); }
#endif
	}
	if (t >= 20 && !w_pdone) {
#ifdef FT_TAGS
		ft_tag = -1; ft_skip_after = NULL;
#endif
		w_pdone = 1; double a[WPS_N], g[WPS_N]; for (int k = 0; k < WPS_N; k++) { a[k] = w_pn[k] ? w_pms[k] / w_pn[k] : 0; g[k] = 0; }
#ifdef FT_TAGS
		for (int k = 0; k < WPS_N; k++) g[k] = ft_tag_n[k] ? ft_tag_ms[k] / ft_tag_n[k] : 0;
#endif
		if (w_log) w_log("world probe %s (ms/frame | GPU fragment ms/scene, vsync off): opaque LUT%s %.2f | %.2f (%d frames) | opaque polynomial%s %.2f | %.2f (%d) | b37 float %.2f | %.2f (%d) | no grading %.2f | %.2f (%d)\n",
			w_stem, w_base_set ? "" : " (shipping)", a[0], g[0], w_pn[0], w_base_set ? " (shipping)" : "", a[1], g[1], w_pn[1], a[2], g[2], w_pn[2], a[3], g[3], w_pn[3]);
#ifdef GPUMODEL_H
		const GpuModelMap *M = w_model_for(w_stem);
		if (M && w_log) w_log("world model %s (tools/gpumodel, fit to b41-2 vsync off): GPU frame %.1f | %.1f | %.1f | %.1f ms per set as above = world %.1f | %.1f | %.1f | %.1f + the rest %.1f\n",
			w_stem, M->world[0] + M->other, M->world[1] + M->other, M->world[2] + M->other, M->world[3] + M->other, M->world[0], M->world[1], M->world[2], M->world[3], M->other);
#endif
	}
	if (t >= 21.6 && !w_pdone3) {   // render b45: what the chunk culling saves the GPU (the same views, frame by frame; the tour leaves a map at 22 s)
		w_pdone3 = 1;
#ifdef FT_TAGS
		ft_tag = -1; ft_set_unlock(0);
		double on = ft_tag_n[6] ? ft_tag_ms[6] / ft_tag_n[6] : 0, off = ft_tag_n[7] ? ft_tag_ms[7] / ft_tag_n[7] : 0;
		double noocc = ft_tag_n[5] ? ft_tag_ms[5] / ft_tag_n[5] : 0;
		if (w_log) w_log("gpu chunks %s (GPU fragment ms/scene, vsync off, frame-interleaved): chunk + occlusion culling on %.2f (%u frames, %ldk world indices) | both off %.2f (%u, %ldk) = %.2f ms saved | occlusion culling off, chunks on %.2f (%u) = occlusion saves %.2f ms (b60)\n",
			w_stem, on, ft_tag_n[6], w_cn[0] ? w_cidx[0] / w_cn[0] / 1000 : 0, off, ft_tag_n[7], w_cn[1] ? w_cidx[1] / w_cn[1] / 1000 : 0, off - on, noocc, ft_tag_n[5], noocc - on);
		double eon = ft_tag_n[16] ? ft_tag_ms[16] / ft_tag_n[16] : 0, eoff = ft_tag_n[17] ? ft_tag_ms[17] / ft_tag_n[17] : 0;   // b67
		if (w_log && ft_tag_n[16] && ft_tag_n[17]) w_log("gpu env %s (GPU fragment ms/scene, vsync off, frame-interleaved, range %.0f m): reflections on %.2f (%u frames) | off %.2f (%u) = %.2f ms (b67)\n",
			w_stem, W_ENV_MAX / 100, eon, ft_tag_n[16], eoff, ft_tag_n[17], eon - eoff);
		double aoff = ft_tag_n[18] ? ft_tag_ms[18] / ft_tag_n[18] : 0;   // b69
		if (w_log && ft_tag_n[16] && ft_tag_n[18]) w_log("gpu aniso %s (GPU fragment ms/scene, vsync off, frame-interleaved): anisotropic on %.2f (%u frames) | off %.2f (%u) = %.2f ms on %d base textures (b69)\n",
			w_stem, eon, ft_tag_n[16], aoff, ft_tag_n[18], eon - aoff, w_aniso_n - w_aniso_bad);
#endif
	}
}
// b60: what is drawn this frame, decided before the draw loop on all cores (oc_par): per mesh and per chunk 0 = outside
// the frustum, 1 = drawn, 2 = hidden behind walls (occl.h). The frustum tests were made in the draw loop itself before.
static unsigned char *w_mvis, *w_cvis; static const float *w_vis_mvp; static int w_vis_cull, w_vis_n;
#define W_VIS_BATCH 16
static void w_vis_job(void *ctx, int j) {
	(void)ctx; int i1 = (j + 1) * W_VIS_BATCH; if (i1 > world_nmesh) i1 = world_nmesh;
	for (int i = j * W_VIS_BATCH; i < i1; i++) {
		const WMeshW *m = &w_mesh[i]; int v = !w_vis_cull || w_visible(w_vis_mvp, m->mn, m->mx);
		if (v && !oc_box_visible(m->mn, m->mx)) v = 2;
		w_mvis[i] = (unsigned char)v;
		if (v != 1 || !w_vis_cull || m->mode == WMODE_BLEND || m->c1 - m->c0 <= 1 || w_chunk_skip) continue;
		for (int c = m->c0; c < m->c1; c++) { int cv = w_visible(w_vis_mvp, w_chunk[c].mn, w_chunk[c].mx); if (cv && !oc_box_visible(w_chunk[c].mn, w_chunk[c].mx)) cv = 2; w_cvis[c] = (unsigned char)cv; }
	}
}
static int world_draw(const float *MVP, const float *cam, const float *fog, const float *fogp, int lightmaps_on, int cull, float t) {
	WState st; memset(&st, 0, sizeof(st)); st.prog = st.tex = st.lm = st.glow = st.envt = st.envc = -1; st.cull = 1; st.zw = 1; st.blend = 0;
	w_fog_off = fogp[2] <= 0; w_idx_frame = 0; if (!w_probe_early) w_probe_frame(); w_probe_early = 0; grade_bind(); w_frames++;
	// b67: the env programs' eye and view rotation (rows 0, 1 of the MVP are the view's right / up scaled by the projection,
	// row 3 is minus its forward), and the adaptive range: a frame later than 21.5 ms (a missed vsync) cuts it by 30 % (under
	// 3 m: off), 2 s without one grows it back by 10 m a second up to w_env_ceil. Not in a map's first 90 frames (loading).
	w_eye[0] = cam[0]; w_eye[1] = cam[1]; w_eye[2] = cam[2];
	for (int r = 0; r < 3; r++) { int src = r == 2 ? 3 : r; float sg = r == 2 ? -1.f : 1.f, v[3] = { MVP[src], MVP[4 + src], MVP[8 + src] }, n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		for (int j = 0; j < 3; j++) { w_vr[r][j] = n > 0 ? sg * v[j] / n : 0; }
		w_vr[r][3] = 0; }
	{ static unsigned long long last; static float okt; unsigned long long now = w_clock_us();
		float dt = last && now > last ? (now - last) / 1e6f : 0; last = now;
		// b69: anisotropic filtering off while frames run late with no reflection range left to give (or none on this map),
		// back on after 4 s without a late frame (the probe window sets it frame by frame itself)
		if (w_aniso_on && w_aniso_n && !w_aniso_pwin && w_frames > 90 && dt > 0 && dt < 0.5f && !w_env_fixed) {
			if (dt > 0.0215f && (!w_env_meshes || !w_env_on || w_env_range <= 0)) { if (w_aniso_cur) { w_aniso_set(0); w_aniso_offs++; } w_aniso_okt = 0; }
			else if (!w_aniso_cur && (w_aniso_okt += dt) > 4.f) w_aniso_set(1);
		}
		if (w_env_meshes && w_frames > 90 && dt > 0 && dt < 0.5f && !w_env_fixed) {
			// b110: the per-pixel reflection is the first thing a late frame takes away (when it drew in that frame): off at once, the
			// range as it was - without it the frame costs what b108's did. Back on after w_env_px_wait s without a late frame (10 s;
			// doubled, up to 120 s, each time a late frame follows within 5 s of switching it on: a map with no time to spare for it
			// settles on the per-vertex kind; back to 10 s once it has stayed on for 20 s).
			const long pxdrew = w_env_px_now; w_env_px_now = 0;
			if (w_env_px) { w_env_px_frames++; w_env_px_ont += dt; if (w_env_px_ont > 20) w_env_px_wait = 10; }
			if (dt > 0.0215f && w_env_px && pxdrew > 0 && w_env_gov) {
				w_env_px = 0; w_env_px_offs++; w_env_px_okt = 0; if (w_env_px_ont < 5 && w_env_px_offs > 1) { w_env_px_wait *= 2; if (w_env_px_wait > 120) w_env_px_wait = 120; } }
			else if (dt > 0.0215f) { w_env_px_okt = 0; w_env_ceil = w_env_range * 0.9f; w_env_range *= 0.7f; if (w_env_range < 300) w_env_range = 0; okt = 0; w_env_late++; if (w_env_range < w_env_min_seen) w_env_min_seen = w_env_range; }
			else if (!w_env_px && (w_env_px_okt += dt) > w_env_px_wait) { w_env_px = 1; w_env_px_ont = 0; }
			if (dt > 0.0215f && pxdrew > 0) w_env_px_late++; else if (dt > 0.0215f) w_env_pv_late++;   // b111: late frames with / without a cube draw in them
			if (!(dt > 0.0215f) && (okt += dt) > 2.f) {
				w_env_ceil += 200 * dt; if (w_env_ceil > W_ENV_MAX) w_env_ceil = W_ENV_MAX;
				if (w_env_range < w_env_ceil) { w_env_range += 1000 * dt; if (w_env_range > w_env_ceil) w_env_range = w_env_ceil; }
			}
		}
	}
	int drawn = 0, nb = 0, nfd = 0;
	if (cull && !w_chunk_skip && !w_occ_skip) oc_begin(MVP, cam); else oc_ready = 0;   // b60: this frame's occlusion picture (occl.h)
	if (w_vis_n != world_nmesh + w_nchunk) { free(w_mvis); free(w_cvis); w_mvis = malloc(world_nmesh + 1); w_cvis = malloc(w_nchunk + 1); w_vis_n = world_nmesh + w_nchunk; }
	w_vis_mvp = MVP; w_vis_cull = cull;
	{ int nj = (world_nmesh + W_VIS_BATCH - 1) / W_VIS_BATCH; if (oc_par) oc_par(w_vis_job, NULL, nj); else for (int j = 0; j < nj; j++) w_vis_job(NULL, j); }
	for (int i = 0; i < world_nmesh; i++) {
		WMeshW *m = &w_mesh[i];
		if (!w_mvis[i] || (m->flags & WFLAG_HIDE)) continue;   // (b105 HIDE: wobj.h draws its animated copy)
		if (oc_ready) { oc_st_tests++; if (w_mvis[i] == 2) { oc_st_hidden++; w_idx_occ += m->ni; continue; } }   // b60: wholly behind walls
		if (m->mode == WMODE_TEST && (m->flags & WFLAG_FLAT) && !w_no_bias && !w_net && nfd < W_FDMAX) {   // b116: after the opaque meshes, lowest first
			int k = nfd++; while (k > 0 && w_mesh[w_fd[k - 1]].fz > m->fz) { w_fd[k] = w_fd[k - 1]; k--; } w_fd[k] = i; continue; }
		if (m->mode == WMODE_BLEND) {   // later, back to front
			float d[3] = { (m->mn[0] + m->mx[0]) * 0.5f - cam[0], (m->mn[1] + m->mx[1]) * 0.5f - cam[1], (m->mn[2] + m->mx[2]) * 0.5f - cam[2] };
			w_order[nb] = i; w_dist[i] = (m->flags & WFLAG_FLAT) ? 1e30f : (m->flags & WFLAG_GPATCH) ? 9e29f : d[0] * d[0] + d[1] * d[1] + d[2] * d[2]; nb++; continue;   // (b59: ground decals first, in file order)
		}
		w_idx_all += m->ni;
		if (cull && m->c1 - m->c0 > 1 && !w_chunk_skip) {   // render b45: the chunks in the frustum, neighbouring ones as one draw
			int bound = 0, run0 = -1, run1 = 0;
			for (int c = m->c0; c <= m->c1; c++) {
				int vis = c < m->c1 && w_cvis[c] == 1;
				if (c < m->c1 && w_cvis[c] && oc_ready) { oc_st_tests++; if (w_cvis[c] == 2) { oc_st_hidden++; w_idx_occ += w_chunk[c].count; } }   // b60
				if (vis) { if (run0 < 0) run0 = w_chunk[c].first; run1 = w_chunk[c].first + w_chunk[c].count; continue; }
				if (run0 < 0) continue;
				if (!bound) { w_bind_mesh(&st, m, w_prog_for(m), MVP, fog, fogp, lightmaps_on); bound = 1; drawn++; }
				glDrawElements(GL_TRIANGLES, run1 - run0, GL_UNSIGNED_SHORT, (void *)(uintptr_t)(m->ioff + run0 * 2)); w_idx_drawn += run1 - run0; w_idx_frame += run1 - run0; w_runs++; run0 = -1;   // (perf b46: + ioff)
			}
			continue;
		}
		w_bind_mesh(&st, m, w_prog_for(m), MVP, fog, fogp, lightmaps_on);
		glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, (void *)(uintptr_t)m->ioff); drawn++; w_idx_drawn += m->ni; w_idx_frame += m->ni; w_runs++;
	}
	if (nfd) {   // b116: the alpha-tested ground decals (see the load: Ghost Storage's debris over its painted lines)
		glEnable(GL_POLYGON_OFFSET_FILL);
		for (int k = 0; k < nfd; k++) { WMeshW *m = &w_mesh[w_fd[k]]; glPolygonOffset(-1.0f, k ? -4.0f : -2.0f);   // (b117: the lowest one under the blended decals' -3 - the stain lies over the paint -, the others over them: the debris lies on the stain)
			w_bind_mesh(&st, m, w_prog_for(m), MVP, fog, fogp, lightmaps_on);
			glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, (void *)(uintptr_t)m->ioff); drawn++; w_idx_all += m->ni; w_idx_drawn += m->ni; w_idx_frame += m->ni; w_runs++; }
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
	if (w_chunk_win >= 0) { w_cidx[w_chunk_win] += w_idx_frame; w_cn[w_chunk_win]++; }   // render b45: the chunk probe's index counts
	if (st.vcol) { glDisableVertexAttribArray(3); st.vcol = 0; }
	if (st.nrm) { glDisableVertexAttribArray(4); st.nrm = 0; }   // b67
	if (!st.zw) glDepthMask(GL_TRUE);
	if (st.blend) glDisable(GL_BLEND);
	if (!st.cull) glEnable(GL_CULL_FACE);
	// sky: far layers behind everything opaque, then world-space layers
	if (!w_net) { w_draw_sky(MVP, cam, fog, 1, t); w_draw_sky(MVP, cam, fog, 0, t); }   // b84: no sky in the network view
	else w_netk[3] = fmodf(t * 0.45f, 1.0f);   // (the streaks' clock)
	memset(&st, 0, sizeof(st)); st.prog = st.tex = st.lm = st.glow = st.envt = st.envc = -1; st.cull = 1; st.zw = 1; st.blend = 0;
	if (nb > 1) qsort(w_order, nb, sizeof(int), w_dist_cmp);
	// b105: the ground decals lie 0.2-2.5 cm over the floor they are painted on (Urban's lids 0.22 cm): at a distance that is
	// less than the depth buffer tells apart, and the floor won pixels or whole decals from them as the view moved. They are
	// drawn with a depth bias toward the eye (one slope step + 3 depth steps: nothing a decal could show through that it
	// does not lie on); the other blended meshes as before.
	int po = 0;
	for (int k = 0; k < nb; k++) {
		WMeshW *m = &w_mesh[w_order[k]];
		if (w_net && (m->flags & WFLAG_ADD)) continue;   // b84: lights and glows stay out of the network view
		// b109 (user: "a sign on one of the buildings that says logistics in urban ... flickers a bit when you look at it or walk around it
		// from different angles"): the SIO LOGISTICS board's scan-line overlay (an additive mesh) lies IN the board's plane (0.00 cm), as
		// most glows and overlays of the signs do; drawn by another program its depth comes out a step before or behind the board's, by
		// the angle. Every blended mesh that does not write depth now gets the decals' bias (it only ever shows over what it lies on).
		{ int want = w_no_bias ? 0 : (m->flags & WFLAG_GPATCH) ? 2 : (m->flags & (WFLAG_FLAT | WFLAG_NOZW)) != 0; if (want != po) { po = want; if (po) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1.0f * po, -3.0f * po); } else glDisable(GL_POLYGON_OFFSET_FILL); } }   // (b117: 2 = a ground patch)
		w_bind_mesh(&st, m, w_prog_for(m), MVP, fog, fogp, lightmaps_on);
		glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, (void *)(uintptr_t)m->ioff); drawn++;   // (perf b46: + ioff)
	}
	if (po) glDisable(GL_POLYGON_OFFSET_FILL);
	if (st.vcol) glDisableVertexAttribArray(3);
	if (st.nrm) glDisableVertexAttribArray(4);   // b67
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
	glActiveTexture(GL_TEXTURE0); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	return drawn;
}
