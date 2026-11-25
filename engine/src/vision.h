// vision.h - how characters and objects LOOK beyond plain shading: teammates seen through walls, enemies revealed by skills
// (heat-scan silhouettes, recon / cyber-wall marks), thermoptic camouflage (third person, the player's own arms, the
// cloak / uncloak flashes, the hit flicker), the eye glints, and a hologram / x-ray look for objects (Think Tanks, pick-ups).
//
// What the original does (read from the game's own shader folder, Data/shaders, with tools/vision/d3dasm.py; footage refs in
// tools/vision/REFS.txt):
//  * PostDepthIDMaterial writes (actor id, outline table, depth) into an id buffer; PostEdgePass draws edges where the id /
//    depth changes (g_fEdgeColor), PostActorEnemyEdgePass a 1-texel edge around actor id 10 in g_fEnemyEdgeColor (default
//    1,0,0). Footage: a teammate behind cover shows ONLY a thin teal outline (core ~ (150,245,255), body (115,200,212) at
//    1280x720, 2-3 px), the wall stays visible inside it; visible teammates have no outline.
//  * PostSolidMaterial + Solid.fxo: actors drawn flat into a silhouette buffer (always / hidden-only / visible-only), blurred,
//    composited as an outer glow (blur^2 x 5) plus an additive pattern fill. Footage: heat-scanned enemies are flat red
//    (~(190,25,20)) soft-edged silhouettes drawn over everything, even over a teammate standing in front.
//  * WGStandardMaterial (characters): refraction camo = the scene behind, sampled with a view-dependent offset, mixed with the
//    lit texture by g_RefractionAlpha, plus an edge ramp (smoothstep(|N.V| / length), scrolled by g_RefractionOutLineValue)
//    -> glassy body with bright, shimmering edges.
//  * Therm-optic camo in the footage (final build: Neon Ruins 12-17 s, 94-127 s; tutorial camo training video 319-326 s; refs
//    in tools/vision/ref): an enemy / the cloaked operator is the background seen through a lens (displaced, doubled horizon
//    lines, a slightly lighter body, white glints on the edges); the player's own gun almost vanishes (a faint refraction
//    ghost, the red-dot reticle keeps glowing) with a white haze at the screen edges (skills.h overlay); FIRING drops the camo
//    (own gun fully visible from the first shot until ~0.9 s after the last one, then it re-cloaks in ~0.5 s with bright
//    white edge lines); camo on: white edge lines sweep over the body and it fades out in ~0.5 s (camouflage_start 0.67 s);
//    camo off: the body comes back with white glowing edges (camouflage_end 0.5 s), at its peak a silver-white body;
//    teammates who share the camo (SkillSync) are a light-cyan hologram (~(100,150,156) median, (135,196,200) lit) with a cyan
//    visor streak; shadows go with the camo.
//  * OutLineAlphaMaterial: alpha = max(texture alpha, sat((1-|N.V|)^exp * pow)) -> holograms (tutorial targets, deployable
//    holograms, the hacked Think Tank in red).
//  * effect.dat character\ally_eye / enemy_eye / *_s_eye: additive glow + horizontal streak at the visor (cyan / red).
//
// How it is done here (no render-to-texture, no full-screen pass, no screen copy): the characters are already CPU-skinned
// into per-bot vertex buffers (chr.h CSkin, per LOD), so every extra look is just another draw of those buffers with one small
// Cg program (VIS_VS / VIS_FS): fresnel rim, an optional screen-anchored pattern, and an optional screen-space shell
// extrusion of N pixels along the projected normal. Hidden-only effects use glDepthFunc(GL_GREATER) with the body pulled a
// few cm toward the eye; outlines are the extruded shell with the stencil masking the body itself (stencil values >= 0x80,
// after bots_planar_shadows, so its 1..n refs are untouched). Glass = premultiplied blend (ONE, ONE_MINUS_SRC_ALPHA).
// Camo refraction (VIS_VS_R / VIS_FS_R) samples the PREVIOUS frame: vitaGL's front display buffer (already resolved, 960x544
// RGBA, finished by the GPU before this frame's fragments run) is read through one linear texture per display buffer, each
// pointed at its buffer once at start-up (vis_scr_*: the sceGxmTextureInitLinear trick of vitaGL's own vglTexImageDepthBuffer),
// no copy, no extra pass. The lookup is offset by
// the body's screen-projected normal (lens), and the body keeps part of the CURRENT background below it (blend), so the
// one-frame lag / the body's own previous image only add a soft streak.
//
// GPU cost (Vita, 960x544 2xMSAA): a hidden teammate = 2 extra draws of its skinned buffers (~6k vertices each) whose
// fragments mostly fail the depth test (TBDR: rejected before shading); a heat silhouette = 2 draws with full fill of the
// body's screen area (~1-3 % of the screen each); a cloaked body = 1 draw instead of the normal one (3 texture reads per
// pixel), + 1 more during the 0.3-0.5 s cloak / uncloak / re-cloak transitions; eye glints = 1 sprite batch. Hidden-only
// passes run only for teammates a map ray says are (partly) behind something (2 bots per frame, 3 rays).
// Fallback: if VIS_VS / VIS_FS fail to compile, the model shader draws flat tinted silhouettes (no outlines), the skills.h
// heat ghosts stay, and cloaked bots keep the old additive ghost; if only VIS_FS_R fails (or the display buffer can't be
// wrapped), the camo uses the glass look (fresnel rim + scanlines, no refraction).
//
// Camo state comes from skills.h's vision accessors (sk_cloak, sk_cloak_flags, sk_cloak_shimmer, sk_cloak_break; see
// tools/skills/INTEGRATION.md): skills.h says what state an actor's camo is in, vision.h how each state looks.
//
// Engine hooks: bots.h bot_vis_body (body draw), main.c vis_init / vis_draw_world / vis_viewmodel / vis_round_start (players
// placed at the spawns: match start, Demolition round reset). Other modules:
//   vis_model(M, sc, VP, Mw, P, cam, col, mode)  - VIS_HOLO / VIS_XRAY_LINE / VIS_XRAY_FILL look for any GVM (Think Tank ...)
//   vis_outline_model(...)                       - = vis_model(..., VIS_XRAY_LINE)
//   vis_cloak(a)                                 - how cloaked actor a looks now, 0..1 (HUD / name tags may fade with it)
#pragma once
#include "model.h"
#include "bots.h"
#include "skills.h"
#include "sprite.h"
#include "chr.h"
#include "wpn.h"

// ---- colours / sizes (footage-measured where noted, 0..1 linear like the engine's other tints)
static const float VIS_ALLY_LINE[3] = { 0.47f, 0.86f, 0.90f };    // teammate outline behind cover (HQ footage, ~(120,220,230))
static const float VIS_ALLY_INNER[3] = { 0.10f, 0.32f, 0.34f };   // soft inner edge glow of the hidden part
static const float VIS_ENEMY_LINE[3] = { 1.00f, 0.16f, 0.10f };   // marked enemy outline (g_fEnemyEdgeColor 1,0,0, a little warm)
static const float VIS_ENEMY_INNER[3] = { 0.34f, 0.05f, 0.03f };
static const float VIS_HEAT[3] = { 0.62f, 0.085f, 0.065f };       // heat-scan silhouette fill (in-game tutorial: median (147,27,21), edges ~(195,32,30))
static const float VIS_HEAT_EDGE[3] = { 0.55f, 0.06f, 0.03f };    // its soft outer glow
static const float VIS_SYNC_FILL[3] = { 0.92f, 0.95f, 1.00f };   // SkillSync-linkable ally on the sharer's screen: a flat white silhouette
static const float VIS_SYNC_EDGE[3] = { 0.55f, 0.62f, 0.70f };
static const float VIS_LINE_PX = 1.7f, VIS_GLOW_PX = 2.8f;        // shell widths in Vita pixels (footage 2-3 px at 720p)
static const float VIS_PULL = 15.0f;                              // cm toward the eye: visible surfaces fail the GREATER test
enum { VIS_HOLO, VIS_XRAY_LINE, VIS_XRAY_FILL };

// ---- shaders: the model shader's skinning (MD_VS) + rim, pattern coordinates and a screen-space shell extrusion
static const char *VIS_VS =
"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[192],"
" uniform float4 eyep, uniform float4 infl, uniform float4 pscl, uniform float4 rdir,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float out fr : TEXCOORD2) {"
" float4 p = float4(pos, 1); float3 n = nrm.xyz;"
" int i0 = (int)(bi.x * 4.0 + 0.5), i1 = (int)(bi.y * 4.0 + 0.5), i2 = (int)(bi.z * 4.0 + 0.5), i3 = (int)(bi.w * 4.0 + 0.5);"
" float3 s = bw.x * float3(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p))"
"          + bw.y * float3(dot(pal[i1], p), dot(pal[i1 + 1], p), dot(pal[i1 + 2], p))"
"          + bw.z * float3(dot(pal[i2], p), dot(pal[i2 + 1], p), dot(pal[i2 + 2], p))"
"          + bw.w * float3(dot(pal[i3], p), dot(pal[i3 + 1], p), dot(pal[i3 + 2], p));"
" float3 sn = bw.x * float3(dot(pal[i0].xyz, n), dot(pal[i0 + 1].xyz, n), dot(pal[i0 + 2].xyz, n))"
"           + bw.y * float3(dot(pal[i1].xyz, n), dot(pal[i1 + 1].xyz, n), dot(pal[i1 + 2].xyz, n));"
" float3 nn = normalize(sn + float3(0, 0, 0.0001));"
" float3 ve = normalize(eyep.xyz - s);"
" float3 q = s + ve * infl.y;"
" float4 c = float4(q, 1); float4 cp = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c));"
" float4 e = float4(q + nn * 8.0, 1); float4 ce = float4(dot(m0, e), dot(m1, e), dot(m2, e), dot(m3, e));"
" float2 d = (ce.xy / max(ce.w, 1.0) - cp.xy / max(cp.w, 1.0)) * float2(480.0, 272.0);"
" float2 dn = d / (length(d) + 0.0001);"
" cp.xy = cp.xy + dn * float2(infl.x / 480.0, infl.x / 272.0) * cp.w;"
" vpos = cp; t0 = uv;"
" t1 = float2(dot(s, rdir.xyz), s.z) * pscl.x + pscl.zw;"
" fr = 1.0 - abs(dot(nn, ve)); }";
static const char *VIS_FS =
"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float fr : TEXCOORD2, uniform sampler2D dif : TEXUNIT0,"
" uniform sampler2D pat : TEXUNIT1, uniform float4 col, uniform float4 rim, uniform float4 prm) : COLOR {"
" float f2 = fr * fr; float r = lerp(f2, f2 * f2, prm.w);"
" float4 pt = tex2D(pat, t1); float p = 1.0 + (pt.r - 1.0) * prm.z;"   // (tex2D is half4 on the Vita: no half / float mixing in lerp / dot)
" float4 d = tex2D(dif, t0);"
" float3 c = (col.rgb + rim.rgb * r) * p + d.rgb * prm.y;"
" float a = saturate((col.a + rim.a * r) * p + d.a * prm.x);"
" return float4(c, a); }";
// render b39: VIS_FS and VIS_FS_R in half precision (texture coordinates stay float, the fresnel varying is half); vis_init tries them first (grade.h
// grade_half: the nohalf switch), the float ones if they do not compile
static const char *VIS_FS_H =
"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, half fr : TEXCOORD2, uniform sampler2D dif : TEXUNIT0,"
" uniform sampler2D pat : TEXUNIT1, uniform float4 col, uniform float4 rim, uniform float4 prm) : COLOR {"
" half4 q = (half4)prm; half f = fr; half f2 = f * f; half r = lerp(f2, f2 * f2, q.w);"
" half p = 1.0h + ((half)tex2D(pat, t1).r - 1.0h) * q.z;"
" half4 d = (half4)tex2D(dif, t0);"
" half3 c = ((half3)col.rgb + (half3)rim.rgb * r) * p + d.rgb * q.y;"
" half a = saturate(((half)col.a + (half)rim.a * r) * p + d.a * q.x);"
" return float4((float3)c, (float)a); }";
static const char *VIS_FS_R_H =
"float4 main(float2 t0 : TEXCOORD0, float4 t1 : TEXCOORD1, half fr : TEXCOORD2, float2 t3 : TEXCOORD3,"
" uniform sampler2D dif : TEXUNIT0, uniform sampler2D pat : TEXUNIT1, uniform sampler2D scr : TEXUNIT5,"
" uniform float4 rfr, uniform float4 col, uniform float4 rim, uniform float4 prm, uniform float4 shd) : COLOR {"
" half3 bg = (half3)tex2D(scr, t1.xy / t1.w).rgb; half4 d = (half4)tex2D(dif, t0); half pr = (half)tex2D(pat, t3).r;"
" half4 q = (half4)prm; half l = dot(d.rgb, half3(0.30h, 0.59h, 0.11h));"
" half f = fr; half f2 = f * f; half r = f2 + (f2 * f2 - f2) * q.w;"
" half p = 1.0h + (pr - 1.0h) * q.z;"
" half3 c = bg * (half3)rfr.rgb * (1.0h - (half)shd.x * (1.0h - l)) + (half3)col.rgb * (l * q.x + q.y) + (half3)rim.rgb * (r * p);"
" return float4((float3)c, (float)saturate((half)rfr.a + (half)col.a + (half)rim.a * r)); }";
static int vis_half;   // render b39: bit 0 VIS_FS, bit 1 VIS_FS_R in half
static const char *VIS_FS_PLAIN =   // fallback: no textures
"float4 main(float fr : TEXCOORD2, uniform float4 col, uniform float4 rim) : COLOR { return col + rim * (fr * fr); }";
// camo refraction: VIS_VS's skinning; t1 = the previous frame's screen position (times w, for a perspective-correct divide in
// the fragment shader) offset by the screen-projected normal: infl.z = offset per 8 cm of in-plane normal (NDC), infl.w = its
// maximum (NDC), infl.y = pull toward the eye (cm); wob.x = shimmer wobble (NDC), wob.y = time, wob.z = screen-texture u scale
// (display width / stride), wob.w = 1 (texture row 0 = top of the screen, vitaGL's display buffer) or -1 (flipped)
static const char *VIS_VS_R =
"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[192],"
" uniform float4 eyep, uniform float4 infl, uniform float4 pscl, uniform float4 rdir, uniform float4 wob,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float4 out t1 : TEXCOORD1, float out fr : TEXCOORD2, float2 out t3 : TEXCOORD3) {"
" float4 p = float4(pos, 1); float3 n = nrm.xyz;"
" int i0 = (int)(bi.x * 4.0 + 0.5), i1 = (int)(bi.y * 4.0 + 0.5), i2 = (int)(bi.z * 4.0 + 0.5), i3 = (int)(bi.w * 4.0 + 0.5);"
" float3 s = bw.x * float3(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p))"
"          + bw.y * float3(dot(pal[i1], p), dot(pal[i1 + 1], p), dot(pal[i1 + 2], p))"
"          + bw.z * float3(dot(pal[i2], p), dot(pal[i2 + 1], p), dot(pal[i2 + 2], p))"
"          + bw.w * float3(dot(pal[i3], p), dot(pal[i3 + 1], p), dot(pal[i3 + 2], p));"
" float3 sn = bw.x * float3(dot(pal[i0].xyz, n), dot(pal[i0 + 1].xyz, n), dot(pal[i0 + 2].xyz, n))"
"           + bw.y * float3(dot(pal[i1].xyz, n), dot(pal[i1 + 1].xyz, n), dot(pal[i1 + 2].xyz, n));"
" float3 nn = normalize(sn + float3(0, 0, 0.0001));"
" float3 ve = normalize(eyep.xyz - s);"
" float3 q = s + ve * infl.y;"
" float4 c = float4(q, 1); float4 cp = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c));"
" float4 e = float4(q + nn * 8.0, 1); float4 ce = float4(dot(m0, e), dot(m1, e), dot(m2, e), dot(m3, e));"
" float w = max(cp.w, 1.0);"
" float2 o = (ce.xy / max(ce.w, 1.0) - cp.xy / w) * infl.z;"
" float ol = length(o); o = o * (min(ol, infl.w) / (ol + 0.00001));"
" o = o + float2(sin(s.z * 0.13 + wob.y * 7.0), cos((s.x + s.y) * 0.11 + wob.y * 6.0)) * wob.x;"
" vpos = cp; t0 = uv;"
" t1 = float4((cp.w + cp.x + o.x * cp.w) * 0.5 * wob.z, (cp.w - (cp.y + o.y * cp.w) * wob.w) * 0.5, 0.0, cp.w);"
" t3 = float2(dot(s, rdir.xyz), s.z) * pscl.x + pscl.zw;"
" fr = 1.0 - abs(dot(nn, ve)); }";
// out = previous frame behind (lens) * rfr.rgb * (1 - shd.x * (1 - luminance)) + texture luminance * col (hologram) + rim *
// fresnel^2 * pattern; premultiplied
// alpha = rfr.a + col.a + rim.a * fresnel^2. prm.x / prm.y: col * (luminance * x + y); prm.z: pattern amount; prm.w: 0 rim ~
// fresnel^2, 1 rim ~ fresnel^4 (thinner)
static const char *VIS_FS_R =
"float4 main(float2 t0 : TEXCOORD0, float4 t1 : TEXCOORD1, float fr : TEXCOORD2, float2 t3 : TEXCOORD3,"
" uniform sampler2D dif : TEXUNIT0, uniform sampler2D pat : TEXUNIT1, uniform sampler2D scr : TEXUNIT5,"
" uniform float4 rfr, uniform float4 col, uniform float4 rim, uniform float4 prm, uniform float4 shd) : COLOR {"
" float4 b4 = tex2D(scr, t1.xy / t1.w); float4 d = tex2D(dif, t0); float4 pt = tex2D(pat, t3);"
" float3 bg = b4.rgb; float l = dot(d.rgb, float3(0.30, 0.59, 0.11));"
" float f2 = fr * fr; float r = f2 + (f2 * f2 - f2) * prm.w;"
" float p = 1.0 + (pt.r - 1.0) * prm.z;"
" float3 c = bg * rfr.rgb * (1.0 - shd.x * (1.0 - l)) + col.rgb * (l * prm.x + prm.y) + rim.rgb * (r * p);"
" return float4(c, saturate(rfr.a + col.a + rim.a * r)); }";

static GLuint vis_prog, vis_tex_blocks, vis_tex_lines, vis_tex_glint, vis_tex_hex, vis_tex_haze, vis_tex_corner, vis_tex_crack;
static GLint vu_m[4], vu_pal, vu_eyep, vu_infl, vu_pscl, vu_rdir, vu_col, vu_rim, vu_prm;
static int vis_ok, vis_frame, vis_stat_passes;   // vis_ok: 1 = full shader, 2 = plain fragment shader
// cost statistics (boot.log every ~5 s while anything is drawn): CPU time of the vision passes, rays, draws, vertices
typedef struct { unsigned long long us, ray_us; int frames, rays, passes, verts, line, heat, camo, holo; } VisStat;
static VisStat vis_st;
#define VIS_T0() unsigned long long vis_t0_ = sceKernelGetProcessTimeWide()
#define VIS_T1() (vis_st.us += sceKernelGetProcessTimeWide() - vis_t0_)
static GLuint vis_rprog; static int vis_r_ok;    // camo refraction program (1 = ok)
static GLint vr_shd, vr_m[4], vr_pal, vr_eyep, vr_infl, vr_pscl, vr_rdir, vr_wob, vr_rfr, vr_col, vr_rim, vr_prm;

// ---- the previous frame as a texture: vitaGL's front display buffer (the frame submitted last; the GPU finishes it before
// this frame's fragment work starts, and nothing writes it while it is shown), wrapped in place as a linear RGBA texture
extern void *gxm_color_surfaces_addr[];   // vitaGL globals (gxm.c)
extern unsigned int gxm_front_buffer_index;
extern int DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE;
#define VIS_SCR_VFLIP 1.0f   // -1 if the camo shows the scene behind upside down on the device (vitaGL source: row 0 = top)
extern uint8_t gxm_display_buffer_count;
#define VIS_SCR_N 5   // vitaGL's DISPLAY_MAX_BUFFER_COUNT
static GLuint vis_scr_texs[VIS_SCR_N]; static void *vis_scr_bufs[VIS_SCR_N], *vis_scr_owns[VIS_SCR_N]; static int vis_scr_n;
static int vis_scr_ok; static unsigned vis_scr_fn = ~0u; static float vis_scr_su = 1;
static void (*vis_logf)(const char *, ...);
// Safety (b31 corruption audit, /root/work/CORRUPTION.md): one GL texture per display buffer, each pointed at its buffer ONCE
// here (no per-frame sceGxmTextureInitLinear); each frame only binds the one whose buffer is the front buffer. The GPU only
// READS through them, the display buffers are never freed while the app runs, and vitaGL's own bookkeeping (tex->data, its
// size, its free on delete) stays on the 4x4 allocation it made for each, which is never deleted. Each frame the bound
// texture's data pointer is checked: if any code ever deleted / re-used one of these ids, the refraction switches itself
// off (glass look) instead of sampling someone else's texture.
static int vis_scr_init(void) {
	static unsigned char px[4 * 4 * 4];   // (static: vitaGL's upload transfer may read it after glTexImage2D returns)
	int n = gxm_display_buffer_count;
	vis_scr_ok = n >= 1 && n <= VIS_SCR_N && DISPLAY_WIDTH >= 480 && DISPLAY_HEIGHT >= 272 && DISPLAY_STRIDE >= DISPLAY_WIDTH && DISPLAY_STRIDE <= 4096 && DISPLAY_HEIGHT <= 4096 && !(DISPLAY_STRIDE & 7);
	if (!vis_scr_ok) return 0;
	glActiveTexture(GL_TEXTURE5);   // unit 5: only VIS_FS_R reads it
	for (int i = 0; i < n && vis_scr_ok; i++) {
		void *a = gxm_color_surfaces_addr[i]; if (!a) { vis_scr_ok = 0; break; }
		glGenTextures(1, &vis_scr_texs[i]); glBindTexture(GL_TEXTURE_2D, vis_scr_texs[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();   // a valid slot to point at the buffer
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		vis_scr_owns[i] = vglGetTexDataPointer(GL_TEXTURE_2D); SceGxmTexture *t = vglGetGxmTexture(GL_TEXTURE_2D);
		if (!vis_scr_owns[i] || !t || sceGxmTextureInitLinear(t, a, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, DISPLAY_STRIDE, DISPLAY_HEIGHT, 0) < 0) { vis_scr_ok = 0; break; }
		sceGxmTextureSetMinFilter(t, SCE_GXM_TEXTURE_FILTER_LINEAR); sceGxmTextureSetMagFilter(t, SCE_GXM_TEXTURE_FILTER_LINEAR);
		sceGxmTextureSetUAddrMode(t, SCE_GXM_TEXTURE_ADDR_CLAMP); sceGxmTextureSetVAddrMode(t, SCE_GXM_TEXTURE_ADDR_CLAMP);
		vis_scr_bufs[i] = a; vis_scr_n = i + 1;
	}
	glActiveTexture(GL_TEXTURE0);
	if (vis_scr_ok) vis_scr_su = (float)DISPLAY_WIDTH / DISPLAY_STRIDE;
	return vis_scr_ok;
}
// once per frame, before the first camo draw: bind the texture of the front buffer (the frame submitted last)
static void vis_scr_frame(void) {
	unsigned fn = vglGetFrameNumber(); if (!vis_scr_ok || fn == vis_scr_fn) return; vis_scr_fn = fn;
	void *a = gxm_color_surfaces_addr[gxm_front_buffer_index % VIS_SCR_N]; int k = -1;
	for (int i = 0; i < vis_scr_n; i++) if (vis_scr_bufs[i] == a) k = i;
	if (k < 0) { vis_scr_ok = 0; if (vis_logf) vis_logf("vision: unknown display buffer %p: camo refraction off\n", a); return; }
	glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, vis_scr_texs[k]);
	if (vglGetTexDataPointer(GL_TEXTURE_2D) != vis_scr_owns[k]) {   // not our slot any more: stop (glass look)
		vis_scr_ok = 0; if (vis_logf) vis_logf("vision: display-buffer texture %u changed hands: camo refraction off\n", vis_scr_texs[k]); }
	glActiveTexture(GL_TEXTURE0);
}
typedef struct { int frame, occ, ngl; float M[16], occ_t, glint_t, seen; } VisBot;
static VisBot visb[NBOT_MAX];
static float vis_now, vis_dt = 1 / 60.0f;

// ---- small maths
static void vis_mul4(float *o, const float *a, const float *b) {   // column-major o = a * b
	float t[16]; for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) t[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
	memcpy(o, t, sizeof(t));
}
static void vis_inv_point(const float *Mw, const float *p, float *o) {   // Mw^-1 * p for an affine column-major matrix
	const float *a = Mw; float m[9] = { a[0], a[4], a[8], a[1], a[5], a[9], a[2], a[6], a[10] };   // rows of the 3x3
	float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
	float id = fabsf(det) > 1e-12f ? 1 / det : 0, v[3] = { p[0] - a[12], p[1] - a[13], p[2] - a[14] };
	float inv[9] = { (m[4] * m[8] - m[5] * m[7]) * id, (m[2] * m[7] - m[1] * m[8]) * id, (m[1] * m[5] - m[2] * m[4]) * id,
	                 (m[5] * m[6] - m[3] * m[8]) * id, (m[0] * m[8] - m[2] * m[6]) * id, (m[2] * m[3] - m[0] * m[5]) * id,
	                 (m[3] * m[7] - m[4] * m[6]) * id, (m[1] * m[6] - m[0] * m[7]) * id, (m[0] * m[4] - m[1] * m[3]) * id };
	for (int r = 0; r < 3; r++) o[r] = inv[r * 3] * v[0] + inv[r * 3 + 1] * v[1] + inv[r * 3 + 2] * v[2];
}
static float vis_clamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static unsigned vis_seed = 0x9E3779B9u;
static float vis_rnd(void) { vis_seed ^= vis_seed << 13; vis_seed ^= vis_seed >> 17; vis_seed ^= vis_seed << 5; return (vis_seed >> 8) * (1.0f / 16777216.0f); }

// ---- textures, built at start-up (no data files): digital blocks (pattern of the solid / heat fills, after
// CyberWorldForActorSolid / digital_pattern_box), scanline noise (camo shimmer and hit glitch, after camouflage_outside_line /
// cm_noise_line) and the eye glint (dot + horizontal streak, fitted to effect\texture\eye_line.dds / ally_eye.dds)
static GLuint vis_tex_make(int w, int h, const unsigned char *px, int repeat) {
	GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();
	return t;
}
static void vis_textures(void) {
	static unsigned char px[128 * 64 * 4];
	unsigned sd = 0x2545F491u;
	#define VIS_R() (sd ^= sd << 13, sd ^= sd >> 17, sd ^= sd << 5, (sd >> 8) * (1.0f / 16777216.0f))
	// blocks: 8 x 8 cells of 8 px, random level 0.45..1, a dark 1 px seam, some cells split in two
	for (int cy = 0; cy < 8; cy++) for (int cx = 0; cx < 8; cx++) {
		float v = 0.45f + 0.55f * VIS_R(), v2 = VIS_R() < 0.35f ? 0.45f + 0.55f * VIS_R() : v;
		for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { float l = (x >= 4 ? v2 : v) * (x == 0 || y == 0 ? 0.55f : 1); unsigned char *p = px + (((cy * 8 + y) * 64) + cx * 8 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(l * 255); p[3] = 255; }
	}
	vis_tex_blocks = vis_tex_make(64, 64, px, 1);
	// lines: per row a random level, a few bright rows, slight variation along x
	for (int y = 0; y < 64; y++) { float v = 0.35f + 0.4f * VIS_R(); if (VIS_R() < 0.12f) v = 1; if (VIS_R() < 0.15f) v = 0.1f;
		for (int x = 0; x < 64; x++) { float l = v * (0.85f + 0.15f * VIS_R()); unsigned char *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(vis_clamp(l, 0, 1) * 255); p[3] = 255; } }
	vis_tex_lines = vis_tex_make(64, 64, px, 1);
	// glint: 128 x 64 (the quad is twice as wide as high); intensity in rgb, alpha 1 (sprite additive: tex * colour)
	for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) {
		float u = (x + 0.5f) / 64 - 1, v = (y + 0.5f) / 32 - 1, r = sqrtf(u * u * 4 + v * v) * 0.5f;   // r in units of the quad height
		float core = expf(-(r / 0.035f) * (r / 0.035f)), streak = 0.55f * expf(-fabsf(u) / 0.16f) * expf(-(v / 0.07f) * (v / 0.07f)), halo = 0.22f * expf(-r / 0.09f);
		float l = vis_clamp(core + streak + halo, 0, 1); if (x == 0 || x == 127 || y == 0 || y == 63) l = 0;
		unsigned char *p = px + (y * 128 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(l * 255); p[3] = 255; }
	vis_tex_glint = vis_tex_make(128, 64, px, 0);
	// hexagons (the round-start hologram, after CyberWorldForActorDissolve): staggered 16 x 16 px cells (4 x 4 per tile, tileable),
	// per cell a random level 0.45..0.85, bright 1-2 px cell edges
	{ float cv[4][4]; for (int j = 0; j < 4; j++) for (int i = 0; i < 4; i++) cv[j][i] = 0.45f + 0.4f * VIS_R();
	  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
		float d1 = 1e9f, d2 = 1e9f; int ci = 0, cj = 0;
		for (int j = -1; j <= 4; j++) for (int i = -1; i <= 4; i++) {
			float cx = i * 16 + ((j & 1) ? 8 : 0) + 8, cy = j * 16 + 8, dx = x + 0.5f - cx, dy = (y + 0.5f - cy) * 1.15f, d = dx * dx + dy * dy;
			if (d < d1) { d2 = d1; d1 = d; ci = i; cj = j; } else if (d < d2) d2 = d; }
		float e = sqrtf(d2) - sqrtf(d1), l = e < 1.2f ? 1.0f : e < 2.4f ? 0.8f : cv[(cj + 4) & 3][(ci + 4) & 3];
		unsigned char *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(l * 255); p[3] = 255; } }
	vis_tex_hex = vis_tex_make(64, 64, px, 1);
	// haze: white, brightest along the top edge and in its two ends (the respawn screen edge)
	for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { float vy = 1 - (y + 0.5f) / 64, ax = fabsf((x + 0.5f) / 32 - 1);
		float l = vy * vy * (0.55f + 0.45f * ax * ax); unsigned char *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(vis_clamp(l, 0, 1) * 255); p[3] = 255; }
	vis_tex_haze = vis_tex_make(64, 64, px, 0);
	// a screen corner (texel 0,0 = the corner): hexagon cell outlines over a soft haze, fading away from the corner
	for (int y = 0; y < 64; y++) for (int x = 0; x < 128; x++) {   // 128 x 64: the corner patch is ~2x wider than tall on screen
		float u = (x + 0.5f) / 128, v = (y + 0.5f) / 64, r = sqrtf(u * u + v * v * 1.2f), fo = vis_clamp(1 - r / 0.95f, 0, 1); fo *= fo;
		float hxp = x + 0.5f, hyp = (y + 0.5f) * 1.0f, d1 = 1e9f, d2 = 1e9f;   // hexagons of ~22 px (staggered rows)
		for (int j = -1; j <= 4; j++) for (int i = -1; i <= 7; i++) { float cx = i * 22 + ((j & 1) ? 11 : 0), cy = j * 19, dx = hxp - cx, dy = hyp - cy, d = dx * dx + dy * dy;
			if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d; }
		float e = sqrtf(d2) - sqrtf(d1), edge = e < 1.3f ? 1.0f : e < 2.6f ? 0.45f : 0;
		float l = fo * (0.30f + 0.70f * edge); unsigned char *p = px + (y * 128 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(vis_clamp(l, 0, 1) * 255); p[3] = 255; }
	vis_tex_corner = vis_tex_make(128, 64, px, 0);
	// b80 cracks (bparts.h's burning parts, after the PC's DissolveNoise / DissolveRamp burn): the cell borders of a jittered 4 x 4
	// Voronoi (tileable 64 px) as 1-2 px veins with a soft halo, a faint floor (the glowing surface between them)
	{ float cp[16][2]; for (int k = 0; k < 16; k++) { cp[k][0] = ((k & 3) + 0.15f + 0.7f * VIS_R()) * 16; cp[k][1] = ((k >> 2) + 0.15f + 0.7f * VIS_R()) * 16; }
	  for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { float d1 = 1e9f, d2 = 1e9f;
		for (int k = 0; k < 16; k++) for (int oy = -1; oy <= 1; oy++) for (int ox = -1; ox <= 1; ox++) {
			float dx = x + 0.5f - (cp[k][0] + ox * 64), dy = y + 0.5f - (cp[k][1] + oy * 64), d = dx * dx + dy * dy; if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d; }
		float e = sqrtf(d2) - sqrtf(d1), l = expf(-(e / 1.1f) * (e / 1.1f)) + 0.22f * expf(-e / 2.5f) + 0.03f;
		unsigned char *p = px + (y * 64 + x) * 4; p[0] = p[1] = p[2] = (unsigned char)(vis_clamp(l, 0, 1) * 255); p[3] = 255; } }
	vis_tex_crack = vis_tex_make(64, 64, px, 1);
	#undef VIS_R
}

static void vis_draw_bot_hook_set(void);
// after model_shader / sprite_init / skills_init. Returns 1 full, 2 plain fragment shader, 0 no shader (fallbacks)
static int vis_init(mkshader_fn mk, void (*log)(const char *, ...)) {
	vis_logf = log; vis_textures(); memset(visb, 0, sizeof(visb));
	GLuint vs = mk(GL_VERTEX_SHADER, VIS_VS), fs = 0; GLint ok = 0;   // (render b39: fs = 0)
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	if (!ok) { log("vision: VIS_VS failed: flat tinted silhouettes, no outlines\n"); vis_ok = 0; vis_draw_bot_hook_set(); return 0; }
	ok = 0; vis_half = 0;
#ifdef GRADE_HALF
	if (grade_half()) { fs = mk(GL_FRAGMENT_SHADER, VIS_FS_H); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); vis_half = ok; if (!ok) log("vision: VIS_FS half failed, float\n"); }   // render b39
#endif
	if (!ok) { fs = mk(GL_FRAGMENT_SHADER, VIS_FS); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); }
	vis_ok = 1;
	if (!ok) { log("vision: VIS_FS failed, using the plain one\n"); fs = mk(GL_FRAGMENT_SHADER, VIS_FS_PLAIN); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); vis_ok = 2;
		if (!ok) { log("vision: plain fragment shader failed too\n"); vis_ok = 0; vis_draw_bot_hook_set(); return 0; } }
	vis_prog = glCreateProgram(); glAttachShader(vis_prog, vs); glAttachShader(vis_prog, fs);
	glBindAttribLocation(vis_prog, 0, "pos"); glBindAttribLocation(vis_prog, 1, "nrm"); glBindAttribLocation(vis_prog, 2, "uv");
	glBindAttribLocation(vis_prog, 3, "bi"); glBindAttribLocation(vis_prog, 4, "bw");
	glLinkProgram(vis_prog); glGetProgramiv(vis_prog, GL_LINK_STATUS, &ok);
	if (!ok) { log("vision: link failed\n"); vis_ok = 0; vis_draw_bot_hook_set(); return 0; }
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; vu_m[r] = glGetUniformLocation(vis_prog, nm); }
	vu_pal = glGetUniformLocation(vis_prog, "pal"); vu_eyep = glGetUniformLocation(vis_prog, "eyep"); vu_infl = glGetUniformLocation(vis_prog, "infl");
	vu_pscl = glGetUniformLocation(vis_prog, "pscl"); vu_rdir = glGetUniformLocation(vis_prog, "rdir"); vu_col = glGetUniformLocation(vis_prog, "col");
	vu_rim = glGetUniformLocation(vis_prog, "rim"); vu_prm = glGetUniformLocation(vis_prog, "prm");
	glUseProgram(vis_prog); { GLint l = glGetUniformLocation(vis_prog, "dif"); if (l >= 0) glUniform1i(l, 0); l = glGetUniformLocation(vis_prog, "pat"); if (l >= 0) glUniform1i(l, 1); }
	log("vision: %s%s (pal %d eyep %d infl %d col %d rim %d prm %d)\n", vis_ok == 1 ? "ok" : "plain fragment shader", vis_half & 1 ? ", half" : "", vu_pal, vu_eyep, vu_infl, vu_col, vu_rim, vu_prm);   // (render b39: half note)
#if defined(GRADE_HALF) && defined(HUD_VF)
	log("half precision (render b39): model %d, HUD %d, sprites %d, vision %d%s; HUD lean program (b40): %s\n", md_half, hud_half, sp_half, vis_half & 1, grade_half() ? "" : " (nohalf)",
		hprog_lean ? (hud_lean_half ? "half" : "float") : "off");
#endif
	if (vu_pal < 0) { vis_ok = 0; vis_draw_bot_hook_set(); return 0; }
	// camo refraction (falls back to the glass look of VIS_FS)
	if (vis_ok == 1 && vis_scr_init()) {
		GLuint rvs = mk(GL_VERTEX_SHADER, VIS_VS_R), rfs = 0; GLint rok = 0; glGetShaderiv(rvs, GL_COMPILE_STATUS, &rok);
		if (rok) { int rv = rok; rok = 0;   // render b39: the half form first
#ifdef GRADE_HALF
			if (grade_half()) { rfs = mk(GL_FRAGMENT_SHADER, VIS_FS_R_H); glGetShaderiv(rfs, GL_COMPILE_STATUS, &rok); if (rok) vis_half |= 2; else log("vision: VIS_FS_R half failed, float\n"); }
#endif
			if (!rok && rv) { rfs = mk(GL_FRAGMENT_SHADER, VIS_FS_R); glGetShaderiv(rfs, GL_COMPILE_STATUS, &rok); } }
		if (rok) {
			vis_rprog = glCreateProgram(); glAttachShader(vis_rprog, rvs); glAttachShader(vis_rprog, rfs);
			glBindAttribLocation(vis_rprog, 0, "pos"); glBindAttribLocation(vis_rprog, 1, "nrm"); glBindAttribLocation(vis_rprog, 2, "uv");
			glBindAttribLocation(vis_rprog, 3, "bi"); glBindAttribLocation(vis_rprog, 4, "bw");
			glLinkProgram(vis_rprog); glGetProgramiv(vis_rprog, GL_LINK_STATUS, &rok);
		}
		if (rok) {
			for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; vr_m[r] = glGetUniformLocation(vis_rprog, nm); }
			vr_pal = glGetUniformLocation(vis_rprog, "pal"); vr_eyep = glGetUniformLocation(vis_rprog, "eyep"); vr_infl = glGetUniformLocation(vis_rprog, "infl");
			vr_pscl = glGetUniformLocation(vis_rprog, "pscl"); vr_rdir = glGetUniformLocation(vis_rprog, "rdir"); vr_wob = glGetUniformLocation(vis_rprog, "wob");
			vr_rfr = glGetUniformLocation(vis_rprog, "rfr"); vr_shd = glGetUniformLocation(vis_rprog, "shd"); vr_col = glGetUniformLocation(vis_rprog, "col"); vr_rim = glGetUniformLocation(vis_rprog, "rim"); vr_prm = glGetUniformLocation(vis_rprog, "prm");
			glUseProgram(vis_rprog); { GLint l = glGetUniformLocation(vis_rprog, "dif"); if (l >= 0) glUniform1i(l, 0); l = glGetUniformLocation(vis_rprog, "pat"); if (l >= 0) glUniform1i(l, 1);
				l = glGetUniformLocation(vis_rprog, "scr"); if (l >= 0) glUniform1i(l, 5); }
			vis_r_ok = vr_pal >= 0 && vr_rfr >= 0 && vr_wob >= 0;
		}
		log("vision: camo refraction %s%s (display %dx%d stride %d, %d buffers, pal %d rfr %d wob %d)\n", vis_r_ok ? "ok" : "FAILED (glass look)", vis_half & 2 ? ", half" : "", DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_STRIDE, vis_scr_n, vr_pal, vr_rfr, vr_wob);
	} else log("vision: camo refraction off (%s): glass look\n", vis_ok != 1 ? "plain fragment shader" : "display buffer not usable");
	sk_draw_mask &= ~SK_DRAW_HEATGHOST;   // skills.h's heat ghosts: replaced by the revealed character's own silhouette (the haze and kal marks stay with skills.h)
	vis_draw_bot_hook_set();
	return vis_ok;
}

// ---- drawing with the vision program
typedef struct { float col[4], rim[4], prm[4], infl[4], pscl[4]; GLuint pat; } VisLook;
// one part to draw: a model, where its vertices come from this frame, and its model -> world matrix
//   sk / lod: chr.h characters (the CSkin buffers of LOD lod, skinned this frame; or the GPU path when chr_gpu_skin is set)
//   sc:       model.h's CPU-skinned SkinCache (other modules), else the palette-skinned (GPU) path with pal / npal (0 = M->npal)
typedef struct { const Model *M; const float *pal; int npal; CSkin *sk; int lod; const SkinCache *sc; const float *Mw; const GLuint *skv; } VisObj;   // skv: model.h's skinned buffers of this frame (model_skin_get)
static GLint vis_upal = -1;   // palette uniform of the program in use when it is not vis_prog (vis_meshes)
static void vis_begin(const float *MVP, const float *eyep, const float *rdir, const VisLook *L) {
	glUseProgram(vis_prog);
	for (int r = 0; r < 4; r++) { float row[4] = { MVP[r], MVP[4 + r], MVP[8 + r], MVP[12 + r] }; glUniform4fv(vu_m[r], 1, row); }
	float e4[4] = { eyep[0], eyep[1], eyep[2], 1 }, r4[4] = { rdir[0], rdir[1], rdir[2], 0 };
	glUniform4fv(vu_eyep, 1, e4); if (vu_rdir >= 0) glUniform4fv(vu_rdir, 1, r4);
	glUniform4fv(vu_infl, 1, L->infl); if (vu_pscl >= 0) glUniform4fv(vu_pscl, 1, L->pscl);
	glUniform4fv(vu_col, 1, L->col); glUniform4fv(vu_rim, 1, L->rim); if (vu_prm >= 0) glUniform4fv(vu_prm, 1, L->prm);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, L->pat ? L->pat : vis_tex_blocks); glActiveTexture(GL_TEXTURE0);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2); glEnableVertexAttribArray(3); glEnableVertexAttribArray(4);
	vis_stat_passes++; vis_st.passes++;
}
static void vis_end(void) { glDisableVertexAttribArray(3); glDisableVertexAttribArray(4); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); }
static void vis_attr32(GLuint vbo) {   // a 32-byte model vertex (model.h layout)
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
	glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
}
static void vis_elems(const MMesh *m) { glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0); vis_st.verts += m->nv; }
static void vis_elems_sk(const MMesh *m) { skin_draw_elems(m->sp); vis_st.verts += m->nv; }   // CPU-skinned output: the plan's triangles (skin.h)
// mesh blend mode: bits 0..3 (2 additive and 3 ADS-only are skipped by the looks; the sight glows stay with the normal draw),
// bits 4..6 the character LODs (chr.h), the palette window in MMesh.pbase (model.h)
#define VIS_BLEND(m) ((m)->blend & 15)
static int vis_chr_lod(const Model *M, int lod) { int nl = chr_nlods(M); return lod >= nl ? nl - 1 : lod < 0 ? 0 : lod; }
// the non-additive meshes of one part with the program in use (vis_upal / vu_pal)
static void vis_meshes(const VisObj *o) {
	const Model *M = o->M; const float *pal = o->pal; glActiveTexture(GL_TEXTURE0); GLint up = vis_upal >= 0 ? vis_upal : vu_pal;
	if (o->sk && o->sk->ok && o->sk->cur >= 0 && !chr_gpu_skin) {   // chr.h / skin.h: 16-byte skinned positions / normals + the plan's static uv / bone buffer
		const CSkin *c = o->sk; int lod = c->lod, ms[CHR_SLOTS], n = chr_lod_meshes(M, lod, ms);
		for (int i = 0; i < M->nmesh && i < 8; i++) { const MMesh *m = &M->mesh[i];
			if (MD_BM_LATE(VIS_BLEND(m)) || !(chr_mesh_lods(m) & (1 << lod))) continue;
			glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
			if (m->rigid >= 0) { glUniform4fv(up, 3, &pal[m->rigid * 12]); vis_attr32(m->rvbo); vis_elems(m); }
			else { int k = -1; for (int j = 0; j < n; j++) if (ms[j] == i) k = j;
				if (k < 0 || !m->sp || SKIN_OUT_BYTES(m->sp) > c->cap[k]) continue;
				glUniform4fv(up, 3, md_identity); skin_bind_out(m->sp, c->vbo[c->cur][k]); vis_elems_sk(m); } }
	} else if (o->skv && !md_gpu_skin) {   // model.h CPU skinning (skin.h output of this frame)
		for (int i = 0; i < M->nmesh; i++) { const MMesh *m = &M->mesh[i]; if (MD_BM_LATE(VIS_BLEND(m)) || !md_lod_ok(m) || i < md_mesh0 || i >= md_mesh1) continue;
			glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
			if (m->rigid >= 0) { glUniform4fv(up, 3, &pal[m->rigid * 12]); vis_attr32(m->rvbo); vis_elems(m); }
			else if (i < SKF_MESH && o->skv[i]) { glUniform4fv(up, 3, md_identity); skin_bind_out(m->sp, o->skv[i]); vis_elems_sk(m); } }
	} else {   // palette skinning on the GPU (verified on the device / forced, or a model without CPU buffers): rigid meshes with a
		// one-entry palette, the others with their palette windows (model.h model_draw_pal)
		int npal = o->npal > 0 ? o->npal : M->npal, pb = -1, lod = o->sk ? vis_chr_lod(M, o->lod) : 0;
		for (int i = 0; i < M->nmesh; i++) { const MMesh *m = &M->mesh[i];
			if (MD_BM_LATE(VIS_BLEND(m)) || i < md_mesh0 || i >= md_mesh1) continue;
			if (o->sk ? !(chr_mesh_lods(m) & (1 << lod)) : !md_lod_ok(m)) continue;
			if (m->rigid >= 0) { if (m->rigid >= npal) continue; glUniform4fv(up, 3, pal + m->rigid * 12); pb = -1; }
			else if (m->pbase != pb && m->pbase < npal) { pb = m->pbase; md_upload_window(up, pal, pb, npal); }   // (model.h: 4 rows per entry, at most 48 entries = pal[192])
			glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]); vis_attr32(m->vbo); vis_elems(m); }
	}
}
// chr.h skinning without the draw (chr_draw's first half): this frame's buffers of LOD lod when reskin
static void vis_chr_skin(const Model *M, CSkin *c, int lod, const MBone *P, int reskin, float *pal) {
	chr_palette(M, c, P, pal);   // (b80: a blown-off part stays collapsed)
	if (!c->ok || chr_gpu_skin) return;
	if (c->cur < 0) reskin = 1;
	if (!reskin) return;
	chr_skin_queue(M, c, vis_chr_lod(M, lod), P); skin_run();
}
// the palette entry of a rigid model held at bone B (chr.h chr_draw_rigid: the bots' third-person weapon)
static void vis_rigid_pal(const Model *M, const MBone *B, float *pal) {
	const float *S = M->pal_s2b; float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++) pal[r * 4 + k] = R[r * 3] * S[k] + R[r * 3 + 1] * S[4 + k] + R[r * 3 + 2] * S[8 + k] + (k == 3 ? B->p[r] : 0);
}
static void vis_blend_premul(void) { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
static void vis_eye_rdir(const float *MVP, const float *Mw, const float *cam, float *eyep, float *rd) {   // camera in model space, camera right
	rd[0] = MVP[0]; rd[1] = MVP[4]; rd[2] = MVP[8]; float rl = sqrtf(rd[0] * rd[0] + rd[1] * rd[1] + rd[2] * rd[2]);
	if (rl > 1e-6f) for (int k = 0; k < 3; k++) rd[k] /= rl;
	vis_inv_point(Mw, cam, eyep);
}
// one look on one part drawn with MVP (its Mw: model -> world), cam = camera (world)
static void vis_draw_mvp(const VisObj *o, const float *MVP, const float *cam, const VisLook *L) {
	float eyep[3], rd[3]; vis_eye_rdir(MVP, o->Mw, cam, eyep, rd);
	vis_begin(MVP, eyep, rd, L); vis_meshes(o); vis_end();
}
static void vis_draw_look(const VisObj *o, int n, const float *VP, const float *cam, const VisLook *L) {
	for (int k = 0; k < n; k++) { float MVP[16]; vis_mul4(MVP, VP, o[k].Mw); vis_draw_mvp(&o[k], MVP, cam, L); }
}

// a hidden-part outline (teammates, marked enemies, objects): body pass = stencil mark + inner glow where hidden,
// shell pass = the extruded ring outside the body where hidden. ref: even, unique per object this frame (0x80..0xFE):
// the body writes ref, the shells test (stencil & 0xFE) != ref and write ref | 1, so every ring pixel is drawn once.
// o[0..n-1]: the parts of one object (a character and the weapon in its hand)
static void vis_xray_line(const VisObj *o, int n, const float *VP, const float *cam, const float *line, const float *inner, float a, int ref) {
	VisLook L; memset(&L, 0, sizeof(L));
	glEnable(GL_STENCIL_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul(); glDepthFunc(GL_GREATER);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glStencilFunc(GL_ALWAYS, ref, 0xFF);
	for (int k = 0; k < 3; k++) L.rim[k] = inner[k] * a; L.prm[3] = 1; L.infl[1] = VIS_PULL; L.pscl[0] = 1;
	vis_draw_look(o, n, VP, cam, &L);
	glStencilFunc(GL_NOTEQUAL, ref | 1, 0xFE);   // the ring: outside the body (ref), once per pixel (writes ref | 1)
	memset(L.rim, 0, sizeof(L.rim)); for (int k = 0; k < 3; k++) L.col[k] = line[k] * a; L.infl[0] = VIS_LINE_PX;
	vis_draw_look(o, n, VP, cam, &L);
	glDepthFunc(GL_LEQUAL); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}
// a filled silhouette over everything (heat scan): red fill with brighter edges and the block pattern, soft outer glow
static int vis_fill_lite;   // 1: only the narrow shell (silhouettes beyond 25 m are a few pixels wide)
static void vis_xray_fill(const VisObj *o, int n, const float *VP, const float *cam, const float *fill, const float *edge, float a, int ref, float now) {
	VisLook L; memset(&L, 0, sizeof(L));
	glEnable(GL_STENCIL_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul(); glDepthFunc(GL_ALWAYS);
	glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); glStencilFunc(GL_ALWAYS, ref, 0xFF);
	float fa = 0.78f * a; for (int k = 0; k < 3; k++) { L.col[k] = fill[k] * fa; L.rim[k] = fill[k] * 0.15f * a; } L.col[3] = fa; L.rim[3] = 0.1f * a;
	L.prm[2] = 0.2f; L.pscl[0] = 1 / 14.0f; L.pscl[3] = now * 0.35f; L.pat = vis_tex_blocks;
	vis_draw_look(o, n, VP, cam, &L);
	// soft edge (the original blurs a low-resolution silhouette): a narrow brighter shell, then a wide faint one
	glStencilFunc(GL_NOTEQUAL, ref | 1, 0xFE);   // outside the body (ref), each pixel once (both shells write ref | 1)
	memset(&L, 0, sizeof(L)); for (int k = 0; k < 3; k++) L.col[k] = edge[k] * a; L.infl[0] = VIS_GLOW_PX * 0.5f; L.pscl[0] = 1;
	vis_draw_look(o, n, VP, cam, &L);
	if (!vis_fill_lite) { for (int k = 0; k < 3; k++) L.col[k] = edge[k] * 0.45f * a; L.infl[0] = VIS_GLOW_PX * 1.3f;
		vis_draw_look(o, n, VP, cam, &L); }   // (far silhouettes: one shell)
	glDepthFunc(GL_LEQUAL); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}

// ---- thermoptic camouflage looks. The state comes from skills.h's vision accessors (tools/skills/INTEGRATION.md):
// sk_cloak (0..1, its fade in / out / death ramps), sk_cloak_flags, sk_cloak_shimmer (forming / dissolving / hit sparkle),
// sk_cloak_break; the LOOK of each state is decided here.
typedef struct { double fired, spawn; int alive, died; } VisCamo;   // fired: last shot of the cloaked actor seen (SKCF_FIRED); spawn: last respawn
static VisCamo visc[SK_MAXACT];
static double vis_clock;           // s (process clock), set once per frame by vis_camo_tick
static unsigned vis_camo_fn = ~0u;
static float vis_sstep(float a, float b, float x) { float t = vis_clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
// how actor a looks now: *c 0..1 cloak (0 = the normal body), *e 0..1 white edge glow (forming / dissolving / re-cloak),
// *h 0..1 hit flicker. Returns 0 when there is no camo look at all (draw normally).
// Footage (final build, Neon Ruins): firing shows the body (the own gun fully visible) from the first shot until ~0.9 s after
// the last one, then it re-cloaks in ~0.5 s with bright white edge lines (96.5-100.5 s, 118-121 s).
static int vis_camo_eval(int a, float *c, float *e, float *h) {
	*c = *e = *h = 0;
	if (!sk_ok || a < 0 || a >= SK_MAXACT || !sk_valid(a)) return 0;
	int fl = sk_cloak_flags(a); float k = sk_cloak(a);
	if (k <= 0 && !(fl & SKCF_ON)) return 0;
	*c = k; *e = vis_clamp((sk_cloak_shimmer(a) - 0.15f) / 0.85f, 0, 1);   // (0.15 = the steady shimmer: the looks have their own glints)
	if (fl & SKCF_HIT) *h = vis_clamp(2 * sk_cloak_break(a), 0, 1);
	if (fl & SKCF_ON) {
		float f = (fl & SKCF_FIRED) ? 0 : (float)(vis_clock - visc[a].fired) + 0.3f;   // s since the last shot (SKCF_FIRED lasts 0.3 s)
		if (f < 1.5f) { *c *= vis_sstep(0.9f, 1.4f, f); if (f >= 0.9f) *e = fmaxf(*e, f < 1.05f ? (f - 0.9f) / 0.15f : vis_clamp(1 - (f - 1.05f) / 0.45f, 0, 1)); }
	}
	return 1;
}
// once per frame: the clock and the shots of every cloaked actor (off-screen ones too)
static void vis_camo_tick(void) {
	unsigned fn = vglGetFrameNumber(); if (fn == vis_camo_fn) return; vis_camo_fn = fn;
	vis_clock = sceKernelGetProcessTimeWide() * 1e-6;
	if (sk_ok) for (int a = 0; a < SK_MAXACT; a++) { VisCamo *V = &visc[a]; if (!sk_valid(a)) continue;
		if (sk_cloak_flags(a) & SKCF_FIRED) V->fired = vis_clock;
		int al = sk_alive(a);   // a respawn (dead -> alive after having died): the respawn hologram
		if (al && !V->alive && V->died) V->spawn = vis_clock;
		if (!al && V->alive) V->died = 1;
		V->alive = al; }
}
// a cloaked body for the refraction program (premultiplied, see VIS_FS_R): enemy = a lens over the background, slightly
// lighter, white glints on the edges; ally = light-cyan hologram over a weak lens; self = the own gun as a faint lens.
// + white edge lines / silver body (e), electric flicker (h)
typedef struct { float rfr[4], col[4], rim[4], prm[4], infl[4], wob[4], pscl[4], shd[4]; GLuint pat; } VisRL;
enum { VIS_CAMO_ENEMY, VIS_CAMO_ALLY, VIS_CAMO_SELF };
// per kind (enemy, ally, self): lens = amount of the (displaced) background and its tint; offsets = NDC per 8 cm of in-plane
// normal, max, wobble; rim = white glints (rgb, coverage); pattern = scanline amount on the rim, thin rim (fresnel^4) 0 / 1
static const float VIS_CAMO_LENS[3][4] = { { 0.90f, 1.10f, 1.10f, 1.14f }, { 0.15f, 1.0f, 1.0f, 1.0f }, { 0.30f, 1.03f, 1.03f, 1.05f } };
static const float VIS_CAMO_OFS[3][3] = { { 4.5f, 0.11f, 0.003f }, { 1.5f, 0.05f, 0.002f }, { 1.6f, 0.05f, 0.0015f } };
static const float VIS_CAMO_RIM[3][4] = { { 0.32f, 0.33f, 0.36f, 0 }, { 0.30f, 0.70f, 0.75f, 0.25f }, { 0.07f, 0.07f, 0.08f, 0 } };
static const float VIS_CAMO_PAT[3][2] = { { 0.8f, 1 }, { 0.35f, 0 }, { 0.6f, 1 } };
static const float VIS_CAMO_HOLO[4] = { 0.46f, 0.82f, 0.86f, 0.70f };   // teammate hologram: cyan * (luminance * 1.5 + 0.22), coverage
static const float VIS_CAMO_SILVER[3] = { 0.45f, 0.45f, 0.05f };       // flash: silver body amount (enemy, ally, self)
static const float VIS_CAMO_FLASH[3][2] = { { 1.4f, 0.5f }, { 1.4f, 0.5f }, { 1.1f, 1 } };   // flash: white edge lines, thin (fresnel^4) 0..1
static const float VIS_CAMO_SHADE[3] = { 0.25f, 0, 0.15f };          // the suit's dark parts darken the lens (fraction; enemy, ally, self)
static void vis_camo_rl(VisRL *R, int kind, float c, float e, float h, float dist) {
	memset(R, 0, sizeof(*R)); const float *L = VIS_CAMO_LENS[kind], *O = VIS_CAMO_OFS[kind], *M = VIS_CAMO_RIM[kind];
	float tm = (float)fmod(vis_clock, 1000.0), a = L[0] * c, k = c * (kind == VIS_CAMO_ENEMY ? vis_clamp(1 - dist / 4000, 0.3f, 1) : 1);
	R->pat = vis_tex_lines; R->pscl[0] = 1 / 9.0f; R->pscl[3] = tm * 0.9f; R->prm[2] = VIS_CAMO_PAT[kind][0]; R->prm[3] = VIS_CAMO_PAT[kind][1];
	for (int j = 0; j < 3; j++) R->rfr[j] = L[1 + j] * a; R->rfr[3] = a;
	R->shd[0] = VIS_CAMO_SHADE[kind] * c; R->infl[2] = O[0]; R->infl[3] = O[1]; R->wob[0] = O[2] * c; R->wob[1] = tm; R->wob[2] = vis_scr_su; R->wob[3] = VIS_SCR_VFLIP;
	for (int j = 0; j < 4; j++) R->rim[j] = M[j] * k;
	if (kind == VIS_CAMO_ALLY) { for (int j = 0; j < 3; j++) R->col[j] = VIS_CAMO_HOLO[j] * VIS_CAMO_HOLO[3] * c; R->col[3] = VIS_CAMO_HOLO[3] * c; R->prm[0] = 1.5f; R->prm[1] = 0.22f; }
	if (e > 0) {   // white edge lines sweeping over the body, a silver body at the peak
		if (R->prm[0] == 0 && R->prm[1] == 0) { R->prm[0] = 0.9f; R->prm[1] = 0.25f; }
		float s = VIS_CAMO_SILVER[kind];
		for (int j = 0; j < 3; j++) { R->col[j] += (0.55f + 0.02f * j) * e * s; R->rim[j] += VIS_CAMO_FLASH[kind][0] * e; }
		R->col[3] += 0.7f * e * s; R->rim[3] += 0.15f * e; R->prm[2] = fmaxf(R->prm[2], 0.8f); R->prm[3] = VIS_CAMO_FLASH[kind][1]; R->pscl[3] = tm * 3.0f;
	}
	if (h > 0) {   // camouflage_attack: electric flicker, the lens jumps
		float j = h * (0.3f + 1.0f * vis_rnd());
		R->rim[0] += 0.55f * j; R->rim[1] += 0.75f * j; R->rim[2] += 1.0f * j; R->prm[2] = 0.9f; R->pscl[3] = vis_rnd() * 8; R->infl[2] *= 1 + j;
	}
}
static void vis_rdraw(const VisObj *o, const float *MVP, const float *cam, const VisRL *R) {
	vis_scr_frame();
	float eyep[3], rd[3]; vis_eye_rdir(MVP, o->Mw, cam, eyep, rd);
	glUseProgram(vis_rprog);
	for (int r = 0; r < 4; r++) { float row[4] = { MVP[r], MVP[4 + r], MVP[8 + r], MVP[12 + r] }; glUniform4fv(vr_m[r], 1, row); }
	float e4[4] = { eyep[0], eyep[1], eyep[2], 1 }, r4[4] = { rd[0], rd[1], rd[2], 0 };
	glUniform4fv(vr_eyep, 1, e4); if (vr_rdir >= 0) glUniform4fv(vr_rdir, 1, r4);
	glUniform4fv(vr_infl, 1, R->infl); if (vr_pscl >= 0) glUniform4fv(vr_pscl, 1, R->pscl); glUniform4fv(vr_wob, 1, R->wob);
	glUniform4fv(vr_rfr, 1, R->rfr); if (vr_col >= 0) glUniform4fv(vr_col, 1, R->col); if (vr_rim >= 0) glUniform4fv(vr_rim, 1, R->rim); if (vr_prm >= 0) glUniform4fv(vr_prm, 1, R->prm); if (vr_shd >= 0) glUniform4fv(vr_shd, 1, R->shd);
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, R->pat ? R->pat : vis_tex_lines); glActiveTexture(GL_TEXTURE0);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2); glEnableVertexAttribArray(3); glEnableVertexAttribArray(4);
	vis_stat_passes++; vis_st.passes++;
	vis_upal = vr_pal; vis_meshes(o); vis_upal = -1; vis_end();
}
// ---- spawn hologram (footage: TDM Neon Ruins 0-4 s and Demolition Geofront 3.5-6.5 s after each round reset; TDM PSS9
// 248-250.5 s after a respawn): the teammates and the player's own gun / arms are cyan holograms (the texture tinted, hexagon
// cells, bright edges; the maps' colour grades make it mint on Geofront / Neon) for ~3 s after a round start or the actor's
// own respawn. main.c: vis_round_start() when the players are placed at the spawns (match start, Demolition round reset);
// respawns are seen here (skills.h sk_alive: dead -> alive).
static double vis_round_t0 = -100;
static void vis_round_start(void) { vis_round_t0 = sceKernelGetProcessTimeWide() * 1e-6; }
// actor a (0 = the player, 1 + i = bot i): since the last round start or its own respawn (PSS9 TDM 248-250.5 s: ~3 s)
static int vis_kc_actor = -1; static float vis_kc_amt;   // b72 (killcam.h): the killer, a cyan hologram as the kill cam cuts to him (v49 440 s)
static float vis_spawn_amount(int a) {
	double t0 = vis_round_t0; if (a >= 0 && a < SK_MAXACT && visc[a].spawn > t0) t0 = visc[a].spawn;
	float t = (float)(vis_clock - t0), r = t < 0 ? 0 : t < 3.0f ? 1 : vis_clamp(1 - (t - 3.0f) / 0.4f, 0, 1);
	if (a == vis_kc_actor && vis_kc_amt > r) r = vis_kc_amt;
	return r;
}
static const float VIS_SPAWN_TINT[3] = { 0.55f, 1.60f, 1.60f };   // texture * tint (+ the additive overlay): PSS9 (neutral grade) gun median (52,175,173), lit (90,222,217); the maps' colour grades turn it mint (Geofront (51,86,62))
static void vis_spawn_tint(const float *tint, float sp, float *t2) { for (int k = 0; k < 3; k++) t2[k] = tint[k] * (1 - sp + sp * VIS_SPAWN_TINT[k]); t2[3] = tint[3]; }
static void vis_spawn_overlay(const VisObj *o, const float *MVP, const float *cam, float sp, float scale, float pull) {
	if (!vis_ok || !vis_tex_hex) return;
	VisLook L; memset(&L, 0, sizeof(L));
	for (int k = 0; k < 3; k++) { L.col[k] = VIS_SPAWN_TINT[k] * 0.20f * sp; L.rim[k] = VIS_SPAWN_TINT[k] * 0.35f * sp; }   // additive (alpha 0)
	L.prm[2] = 0.85f; L.pscl[0] = scale; L.pat = vis_tex_hex; L.infl[1] = pull;
	glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul();
	vis_draw_mvp(o, MVP, cam, &L);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}

// ---- glass look (fallback when the refraction program or the display buffer is not available)
// glass look: enemy = nearly invisible (white rim glints over a faintly brightened background); ally = cyan glass;
// self = pale arms. col / rim premultiplied (rgb, coverage); VIS_GLASS_TEX = how much of the body texture shows.
static const float VIS_GLASS_ENEMY_COL[4] = { 0.035f, 0.040f, 0.045f, 0.03f }, VIS_GLASS_ENEMY_RIM[4] = { 0.62f, 0.66f, 0.72f, 0.06f };
static const float VIS_GLASS_ALLY_COL[4] = { 0.09f, 0.27f, 0.33f, 0.22f }, VIS_GLASS_ALLY_RIM[4] = { 0.22f, 0.62f, 0.72f, 0.20f };
static const float VIS_GLASS_SELF_COL[4] = { 0.05f, 0.055f, 0.06f, 0.12f }, VIS_GLASS_SELF_RIM[4] = { 0.45f, 0.50f, 0.55f, 0.18f };
static const float VIS_GLASS_TEX[3] = { 0.02f, 0.05f, 0.18f };   // enemy, ally, self
enum { VIS_GLASS_ENEMY, VIS_GLASS_ALLY, VIS_GLASS_SELF };
static void vis_glass_look(VisLook *L, int kind, float v, float flash, float hit, float fire, float now) {
	memset(L, 0, sizeof(*L)); L->pscl[0] = 1 / 9.0f; L->pscl[3] = now * 0.9f; L->pat = vis_tex_lines; L->prm[2] = kind == VIS_GLASS_SELF ? 0.25f : 0.45f;
	const float *c = kind == VIS_GLASS_ENEMY ? VIS_GLASS_ENEMY_COL : kind == VIS_GLASS_ALLY ? VIS_GLASS_ALLY_COL : VIS_GLASS_SELF_COL;
	const float *r = kind == VIS_GLASS_ENEMY ? VIS_GLASS_ENEMY_RIM : kind == VIS_GLASS_ALLY ? VIS_GLASS_ALLY_RIM : VIS_GLASS_SELF_RIM;
	for (int k = 0; k < 4; k++) { L->col[k] = c[k] * v; L->rim[k] = r[k] * v; } L->prm[1] = VIS_GLASS_TEX[kind] * v;
	if (fire > 0) { L->col[3] += 0.12f * fire; L->prm[1] += 0.18f * fire; for (int k = 0; k < 3; k++) L->rim[k] += 0.2f * fire; }   // a shot gives her away for a moment
	if (hit > 0) { float j = hit * (0.4f + 1.2f * vis_rnd()); for (int k = 0; k < 3; k++) L->rim[k] += 0.5f * j; L->prm[2] = 0.9f; L->pscl[3] = vis_rnd() * 8; }   // camouflage_attack flicker
	if (flash > 0) { for (int k = 0; k < 3; k++) { L->col[k] += 0.6f * flash; L->rim[k] += 0.4f * flash; } L->col[3] += 0.45f * flash; L->prm[1] += 0.35f * flash; L->prm[2] *= 1 - flash; }
	L->prm[3] = 0;   // rim ~ (1 - |N.V|)^2
	(void)now;
}

// ---- bots.h hook: draws the body (and the weapon in its hand) while it has a look of its own: the round-start hologram, or
// a camo look (cloaked, forming, dissolving, re-cloaking after shots). 0 = bots_draw draws it normally.
// the body + weapon parts of bot i for the vision passes (pal: the body palette, wpal: 12 floats for the weapon)
static int vis_bot_parts(int i, const float *M, float *pal, float *wpal, VisObj *o) {
	Bot *b = &bots[i]; ChrType *C = &chrt[b->ct]; int n = 1;
	memset(o, 0, 2 * sizeof(VisObj));
	o[0].M = &C->m; o[0].pal = pal; o[0].sk = &b->sk; o[0].lod = b->lod; o[0].Mw = M;
	const Model *hm = b->state != BS_DEAD && b->ca.B ? bot_hand_model(b) : NULL;
	if (hm && hm->ok) { vis_rigid_pal(hm, &b->ca.pose[b->ca.B->grip], wpal); o[1].M = hm; o[1].pal = wpal; o[1].npal = 1; o[1].Mw = M; n = 2; }
	return n;
}
static void vis_bot_normal(int i, const float *MVP, const float *ld, const float *tint, const float *fogc, int reskin) {   // = bots_draw's own draw
	Bot *b = &bots[i]; ChrType *C = &chrt[b->ct];
	chr_draw(&C->m, &b->sk, b->lod, MVP, b->pose, ld, tint, fogc, reskin);
	const Model *hm = b->ca.B ? bot_hand_model(b) : NULL; if (hm) chr_draw_rigid(hm, MVP, &b->ca.pose[b->ca.B->grip], ld, tint, fogc);
}
static int vis_bot_body(int i, const float *MVP, const float *M, const float *ld, const float *tint, const float *fogc, int reskin) {
	Bot *b = &bots[i]; ChrType *C = &chrt[b->ct]; VisBot *V = &visb[i];
	V->frame = vis_frame; memcpy(V->M, M, sizeof(V->M));
	vis_camo_tick();
	float pal[48 * 12], wpal[12], cam[3] = { lis_pos.x, lis_pos.y, lis_pos.z }; VisObj o[2];
	float sp = vis_spawn_amount(1 + i);
	if (sp > 0 && b->state != BS_DEAD) {   // round-start hologram
		float t2[4]; vis_spawn_tint(tint, sp, t2); vis_bot_normal(i, MVP, ld, t2, fogc, reskin);
		VIS_T0(); model_palette(&C->m, b->pose, pal); int n = vis_bot_parts(i, M, pal, wpal, o);
		for (int k = 0; k < n; k++) vis_spawn_overlay(&o[k], MVP, cam, sp, 1 / 40.0f, 1.5f);
		vis_st.holo++; VIS_T1(); return 1;
	}
	if (!sk_ok || !vis_ok) return 0;
	int a = 1 + i; float c, e, h;
	if (a >= SK_MAXACT || !vis_camo_eval(a, &c, &e, &h)) return 0;
	int ally = sk_valid(sk_local) && !sk_hostile(a, sk_local);
	V3 dv = vsub(b->chest, lis_pos); float dist = sqrtf(vdot(dv, dv));
	if (h > 0) c *= 1 - 0.35f * h * vis_rnd();   // hit: the camo stutters
	float ba = 1 - c;                             // the normal body under the camo layer (forming / dissolving / firing)
	if (ba > 0.01f) {
		float t2[4] = { tint[0], tint[1], tint[2], ba };
		if (ba < 0.99f) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(ba > 0.5f ? GL_TRUE : GL_FALSE); }
		vis_bot_normal(i, MVP, ld, t2, fogc, reskin);
		glDisable(GL_BLEND); glDepthMask(GL_TRUE);
		model_palette(&C->m, b->pose, pal);
	} else vis_chr_skin(&C->m, &b->sk, b->lod, b->pose, reskin, pal);
	if (c < 0.01f && e < 0.01f && h < 0.01f) return 1;
	if (!ally && c > 0.5f && e < 0.01f && h < 0.01f && dist > 5000) return 1;   // a cloaked enemy beyond 50 m: a few pixels of lens, skip it
	VIS_T0(); int n = vis_bot_parts(i, M, pal, wpal, o); vis_st.camo++;
	glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul();
	if (vis_r_ok && vis_scr_ok) {
		VisRL R; vis_camo_rl(&R, ally ? VIS_CAMO_ALLY : VIS_CAMO_ENEMY, c, e, h, dist); if (ba > 0.01f) R.infl[1] = 1.5f;   // over the drawn body: no z-fight
		for (int k = 0; k < n; k++) vis_rdraw(&o[k], MVP, cam, &R);
	} else {
		VisLook L; vis_glass_look(&L, ally ? VIS_GLASS_ALLY : VIS_GLASS_ENEMY, c * (ally ? 1 : vis_clamp(1 - dist / 2500, 0.2f, 1)), e, h, 0, (float)vis_clock);
		if (ba > 0.01f) L.infl[1] = 1.5f;
		for (int k = 0; k < n; k++) vis_draw_mvp(&o[k], MVP, cam, &L);
	}
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); VIS_T1();
	return 1;
}
// cloak amount of actor a for other passes (eye glints, ...): 0 visible .. 1 cloaked
static float vis_cloak(int a) { float c, e, h; if (!vis_camo_eval(a, &c, &e, &h)) return 0; return c; }
static void vis_draw_bot_hook_set(void) { bot_vis_body = vis_bot_body; }

// ---- the world pass (main.c, after bots_draw + bots_planar_shadows): teammates behind cover, revealed enemies, eye glints
// is any of head / chest / knees of bot i hidden from the camera? (map rays, refreshed for 2 bots per frame)
static int vis_hidden(int i, V3 cam) {
	Bot *b = &bots[i]; VisBot *V = &visb[i];
	if (V->occ_t > 0 && (i + vis_frame) % 6) return V->occ;   // each bot re-tested every 6th frame (~0.1 s)
	V->occ_t = 1; V->occ = 0;
	V3 pts[3] = { b->head, b->chest, v3(b->pl.pos.x, b->pl.pos.y, b->pl.pos.z + 45) };
	unsigned long long r0 = sceKernelGetProcessTimeWide();
	for (int k = 0; k < 3 && !V->occ; k++) { V3 d = vsub(pts[k], cam); float l = sqrtf(vdot(d, d)); if (l < 30) continue;
		vis_st.rays++; if (ray_any(cam, vmul(d, 1 / l), l - 25)) V->occ = 1; }   // perf (b35): = raycast() > 0, stops at the first hit
	vis_st.ray_us += sceKernelGetProcessTimeWide() - r0;
	return V->occ;
}
static float vis_ally_range = 6000;   // teammate outlines fade out between 45 and 60 m
static void vis_glints(const float *VP, V3 cam);
static void vis_stats_frame(void) {   // boot.log: averages per frame over ~5 s, while vision draws anything
	if (++vis_st.frames < 300) return;
	if (vis_logf && (vis_st.passes || vis_st.rays)) { float f = (float)vis_st.frames;
		vis_logf("  vision: %.2f ms/frame CPU (rays %.2f ms, %.1f/frame), %.1f draws, %.0f vertices/frame; outlines %.1f heat %.1f camo %.1f holo %.1f\n",
			vis_st.us / 1000.0f / f, vis_st.ray_us / 1000.0f / f, vis_st.rays / f, vis_st.passes / f, vis_st.verts / f, vis_st.line / f, vis_st.heat / f, vis_st.camo / f, vis_st.holo / f); }
	memset(&vis_st, 0, sizeof(vis_st));
}
static void vis_draw_world(const float *VP, const float *cam3, float now) {
	VIS_T0();
	vis_dt = vis_clamp(now - vis_now, 0, 0.1f); vis_now = now; vis_camo_tick(); V3 cam = v3(cam3[0], cam3[1], cam3[2]); int lt = sk_valid(sk_local) ? SKA[sk_local].team & 1 : 0, ref = 0x80;   // stencil refs 0x80..0xBE (objects: 0xC0..)
	if (vis_ok) for (int i = 0; i < nbot && i < NBOT_MAX; i++) {
		Bot *b = &bots[i]; VisBot *V = &visb[i]; ChrType *C = &chrt[b->ct];
		if (V->frame != vis_frame || !b->pose || b->state == BS_DEAD) continue;   // not drawn this frame (off screen / too far)
		int a = 1 + i; V3 d = vsub(b->chest, cam); float dist = sqrtf(vdot(d, d));
		float heat = sk_ok && sk_valid(a) && b->team != lt ? SKA[a].heat_t[lt] : 0, mark = sk_ok && sk_valid(a) && b->team != lt ? SKA[a].mark_t[lt] : 0;
		float pal[48 * 12], wpal[12]; VisObj o[2]; int n;
		if (b->team != lt && heat > 0) {   // heat scan: the red silhouette, over everything
			model_palette(&C->m, b->pose, pal); n = vis_bot_parts(i, V->M, pal, wpal, o); float a2 = vis_clamp(heat / 0.6f, 0, 1) * (0.85f + 0.15f * sinf(now * 9 + i)); vis_fill_lite = dist > 2500;
			vis_xray_fill(o, n, VP, cam3, VIS_HEAT, VIS_HEAT_EDGE, a2, ref, now); vis_fill_lite = 0; ref += 2; vis_st.heat++;
		} else if (b->team != lt && mark > 0 && dist < 8000) {   // recon / cyber-wall / dive mark: red outline where hidden
			if (!vis_hidden(i, cam)) continue;
			model_palette(&C->m, b->pose, pal); n = vis_bot_parts(i, V->M, pal, wpal, o); vis_xray_line(o, n, VP, cam3, VIS_ENEMY_LINE, VIS_ENEMY_INNER, vis_clamp(mark, 0, 1), ref); ref += 2; vis_st.line++;
		} else if (b->team == lt && sk_ok && sk_sync_link(sk_local, a) == 1) {   // SkillSync: an ally who can download the player's Tier 2 (District Ruins 280 s: flat white)
			model_palette(&C->m, b->pose, pal); n = vis_bot_parts(i, V->M, pal, wpal, o); float a2 = 0.8f + 0.2f * sinf(now * 6 + i);
			vis_xray_fill(o, n, VP, cam3, VIS_SYNC_FILL, VIS_SYNC_EDGE, a2, ref, now); ref += 2; vis_st.heat++;
		} else if (b->team == lt && dist < vis_ally_range) {   // teammate behind cover: teal outline
			if (!vis_hidden(i, cam)) continue;
			float a2 = vis_clamp((vis_ally_range - dist) / 1500, 0, 1);
			model_palette(&C->m, b->pose, pal); n = vis_bot_parts(i, V->M, pal, wpal, o); vis_xray_line(o, n, VP, cam3, VIS_ALLY_LINE, VIS_ALLY_INNER, a2, ref); ref += 2; vis_st.line++;
		}
		if (ref > 0xBE) ref = 0x80;
	}
	vis_glints(VP, cam);
	vis_frame++;
	VIS_T1(); vis_stats_frame();
}
// eye glints: the visor light of every character facing the camera (effect\character\ally_eye / enemy_eye / *_s_eye):
// a dot + horizontal streak, cyan for teammates, red for enemies, brighter for a moment when it starts aiming at someone
static void vis_glints(const float *VP, V3 cam) {
	if (!vis_tex_glint || !sp_ok) return;
	float R[3] = { VP[0], VP[4], VP[8] }, U[3] = { VP[1], VP[5], VP[9] }, rl = sqrtf(R[0] * R[0] + R[1] * R[1] + R[2] * R[2]), ul = sqrtf(U[0] * U[0] + U[1] * U[1] + U[2] * U[2]);
	if (rl < 1e-6f || ul < 1e-6f) return; for (int k = 0; k < 3; k++) { R[k] /= rl; U[k] /= ul; }
	int lt = sk_valid(sk_local) ? SKA[sk_local].team & 1 : 0, n = 0;
	if (sp_n) sprite_flush(VP, sp_blob, 0);
	for (int i = 0; i < nbot; i++) {
		Bot *b = &bots[i]; VisBot *V = &visb[i];
		if (V->frame != vis_frame || b->state == BS_DEAD || !b->pose) continue;
		if (b->team != lt && vis_cloak(1 + i) > 0.5f) continue;   // a cloaked enemy: no visor light (a cloaked teammate keeps its cyan streak)
		V3 to = vsub(cam, b->head); float d = sqrtf(vdot(to, to)); if (d < 150 || d > 5000) continue;
		float fx = cosf(b->pl.yaw), fy = sinf(b->pl.yaw), face = (to.x * fx + to.y * fy) / d;   // 1 = looking straight at the camera
		int aim = b->state == BS_ENGAGE && b->sees;
		if (aim && V->seen <= 0) V->glint_t = 0.4f; V->seen = aim; if (V->glint_t > 0) V->glint_t -= vis_dt;
		float k = vis_clamp((face - 0.25f) / 0.5f, 0, 1); if (k <= 0) continue;
		int ally = b->team == lt;
		float pulse = V->glint_t > 0 ? (V->glint_t > 0.333f ? (0.4f - V->glint_t) / 0.067f : V->glint_t / 0.333f) : 0;   // *_s_eye: 0 -> 1 in 0.07 s -> 0 at 0.4 s
		float in = k * (aim ? 0.85f : 0.45f) + pulse * 0.8f, sz = fmaxf(1, d / 1500) * (1 + 0.8f * pulse);
		float col[4] = { ally ? 0.25f : 1.0f, ally ? 0.85f : 0.18f, ally ? 0.95f : 0.12f, vis_clamp(in, 0, 1) };
		float hw = 24 * sz, hh = 6 * sz;   // quad 48 x 12 cm at <= 15 m, constant screen size beyond
		V3 c = vadd(vadd(b->head, v3(fx * 9, fy * 9, -4)), vmul(to, 16 / d));   // at the visor, pulled 16 cm toward the camera (in front of the face)
		float p0[3], p1[3], p2[3], p3[3];
		for (int j = 0; j < 3; j++) { float cc = (&c.x)[j]; p0[j] = cc - R[j] * hw + U[j] * hh; p1[j] = cc + R[j] * hw + U[j] * hh; p2[j] = cc + R[j] * hw - U[j] * hh; p3[j] = cc - R[j] * hw - U[j] * hh; }
		sprite_quad(p0, p1, p2, p3, 0, 0, 1, 1, col); n++;
	}
	if (n) sprite_flush(VP, vis_tex_glint, 1);
}

// ---- the player's own gun and arms (main.c viewmodel draw): the round-start hologram and the camo. K: model -> camera
// matrix of the viewmodel (column-major), ads 0..1. Returns 1 when it drew them itself (else main.c draws them with wvm_draw).
// Footage (Neon Ruins): cloaked = the gun almost vanishes (a faint lens, the red-dot reticle keeps glowing); firing brings it
// back until ~0.9 s after the last shot, then white edge lines re-cloak it.
static float vis_vm_ads;
static void vis_vm_normal(const Model *M, const float *mvp, const MBone *P, const float *ld, const float *tint, const float *fog) {
	wvm_draw(M, mvp, P, ld, tint, vis_vm_ads, fog);   // wpn.h: sight glass, ADS-only parts, palette windows
}
static void vis_vm_glows(const Model *M, const float *mvp, const MBone *P, const float *ld, const float *tint, const float *fog) {   // only the additive meshes (sight dots)
	int m0 = md_mesh0, m1 = md_mesh1;
	for (int i = 0; i < M->nmesh; i++) { if (VIS_BLEND(&M->mesh[i]) != 2 || i < m0 || i >= m1) continue;
		int j = i; while (j + 1 < M->nmesh && j + 1 < m1 && VIS_BLEND(&M->mesh[j + 1]) == 2) j++;
		md_mesh0 = i; md_mesh1 = j + 1; vis_vm_normal(M, mvp, P, ld, tint, fog); i = j; }
	md_mesh0 = m0; md_mesh1 = m1;
}
// the respawn screen edge (Port 275.6-277 s): a cyan haze from the top and bright hexagon cells in the corners, fading over
// ~1.8 s after the player's own respawn. Additive sprites in NDC, no depth test (after the gun).
static void vis_spawn_screen(void) {
	if (!sp_ok || !vis_tex_corner || !sk_valid(sk_local)) return;
	float t = (float)(vis_clock - visc[sk_local].spawn); if (t < 0 || t > 1.8f) return;
	float f = 1 - t / 1.8f; f *= f;
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	if (sp_n) sprite_flush(I, sp_blob, 0);
	glDisable(GL_DEPTH_TEST);
	// a soft cyan haze hanging from the top edge, strongest in the top corners
	{ float col[4] = { 0.20f, 0.70f, 0.80f, 0.75f * f }, pa[3] = { -1, 1, 0 }, pb[3] = { 1, 1, 0 }, pc[3] = { 1, 0.25f, 0 }, pd[3] = { -1, 0.25f, 0 };
	  sprite_quad(pa, pb, pc, pd, 0, 0, 1, 1, col); sprite_flush(I, vis_tex_haze, 1); }
	// hexagon outlines in the four corners (one quad each, the texture's 0,0 at the screen corner)
	for (int cn = 0; cn < 4; cn++) {
		float sx = cn & 1 ? 1.0f : -1.0f, sy = cn & 2 ? 1.0f : -1.0f, w = 0.62f, hgt = 0.62f, col[4] = { 0.25f, 0.85f, 0.95f, 0.75f * f };
		float pa[3] = { sx, sy, 0 }, pb[3] = { sx - sx * w, sy, 0 }, pc[3] = { sx - sx * w, sy - sy * hgt, 0 }, pd[3] = { sx, sy - sy * hgt, 0 };
		sprite_quad(pa, pb, pc, pd, 0, 0, 1, 1, col); }
	sprite_flush(I, vis_tex_corner, 1); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);   // (main.c draws the viewmodel stage without culling)
}
static int vis_viewmodel(const Model *VMm, const float *mvp, const float *K, const MBone *P, const float *ld, const float *tint, float ads) {
	static const float nofog[4] = { 0, 0, 0, 0 }; float pal[48 * 12], zero[3] = { 0, 0, 0 };
	VisObj o; memset(&o, 0, sizeof(o)); o.M = VMm; o.pal = pal; o.Mw = K; if (!md_gpu_skin) o.skv = model_skin_get(VMm, P, md_mesh0, md_mesh1, 0);   // (wpn_vm.h skins the same)
	vis_camo_tick(); vis_vm_ads = ads;
	float sp = vis_spawn_amount(sk_local);
	if (sp > 0) {   // round-start hologram
		float t2[4]; vis_spawn_tint(tint, sp, t2); vis_vm_normal(VMm, mvp, P, ld, t2, nofog);
		model_palette(VMm, P, pal); vis_spawn_overlay(&o, mvp, zero, sp, 1 / 12.0f, 0.5f);
		vis_spawn_screen();
		return 1;
	}
	if (!vis_ok || !sk_ok || !sk_valid(sk_local)) return 0;
	float c, e, h;
	if (!vis_camo_eval(sk_local, &c, &e, &h)) return 0;
	if (h > 0) c *= 1 - 0.3f * h * vis_rnd();
	if (c < 0.01f && e < 0.01f && h < 0.01f) return 0;   // revealed (firing): main.c draws it as usual
	float ba = 1 - c;
	if (ba > 0.99f) vis_vm_normal(VMm, mvp, P, ld, tint, nofog);
	else if (ba > 0.01f) { float t2[4] = { tint[0], tint[1], tint[2], ba }; glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); vis_vm_normal(VMm, mvp, P, ld, t2, nofog); glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	else vis_vm_glows(VMm, mvp, P, ld, tint, nofog);
	model_palette(VMm, P, pal);
	glDepthMask(GL_TRUE); glDisable(GL_CULL_FACE); vis_blend_premul();
	if (vis_r_ok && vis_scr_ok) { VisRL R; vis_camo_rl(&R, VIS_CAMO_SELF, c, e, h, 0); if (ba > 0.01f) R.infl[1] = 0.5f; vis_rdraw(&o, mvp, zero, &R); }
	else { VisLook L; vis_glass_look(&L, VIS_GLASS_SELF, c * 0.5f, e, h, 0, (float)vis_clock); if (ba > 0.01f) L.infl[1] = 0.5f; vis_draw_mvp(&o, mvp, zero, &L); }
	glDisable(GL_BLEND);
	return 1;
}

// ---- for other modules (Think Tanks, pick-ups, the bomb ...): one of the looks on any GVM model.
//   VIS_HOLO       depth-tested hologram: translucent fill + bright fresnel rim (OutLineAlphaMaterial): col.a = strength
//                  (hacked Think Tank: col = 1, 0.2, 0.15 / pick-ups: 0.6, 0.95, 1)
//   VIS_XRAY_LINE  outline only where the model is hidden behind something (as teammates), col = line colour
//   VIS_XRAY_FILL  filled silhouette over everything (as heat-scanned enemies), col = fill colour
// M / sc / P: as drawn (sc = its SkinCache when it is drawn with model_draw_cpu this frame, else NULL and P = the pose).
// VP: view-projection, Mw: model -> world (column-major), cam: camera position (world). Call after the opaque scene.
static int vis_obj_ref = 0xC0;
static void vis_model(const Model *M, const SkinCache *sc, const float *VP, const float *Mw, const MBone *P, const float *cam, const float *col, int mode) {
	if (!M || !M->ok) return;
	float pal[48 * 12]; model_palette(M, P, pal); const float a = col[3] > 0 ? col[3] : 1;
	if (!vis_ok) {   // fallback: flat tint with the model shader
		float t[4] = { col[0], col[1], col[2], 0.5f * a }, ld[4] = { 0, 0, 1, 0 }, nofog[4] = { 0, 0, 0, 0 }, MVP[16]; vis_mul4(MVP, VP, Mw);
		glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); if (mode == VIS_XRAY_FILL) glDepthFunc(GL_ALWAYS); else if (mode == VIS_XRAY_LINE) glDepthFunc(GL_GREATER);
		model_draw_cpu(M, (SkinCache *)sc, MVP, P, ld, t, nofog, 0);   // (b35: CPU skinning unless the GPU path is verified)
		glDepthFunc(GL_LEQUAL); glDisable(GL_BLEND); glDepthMask(GL_TRUE); return;
	}
	VisObj o; memset(&o, 0, sizeof(o)); o.M = M; o.pal = pal; o.sc = sc; o.Mw = Mw; if (!md_gpu_skin) o.skv = model_skin_get(M, P, 0, 1 << 30, 0);   // (b35: every model CPU-skinned unless the GPU path is verified)
	int ref = vis_obj_ref; vis_obj_ref = vis_obj_ref >= 0xFC ? 0xC0 : vis_obj_ref + 2;
	if (mode == VIS_XRAY_LINE) { float in[3] = { col[0] * 0.25f, col[1] * 0.25f, col[2] * 0.25f }; vis_xray_line(&o, 1, VP, cam, col, in, a, ref); return; }
	if (mode == VIS_XRAY_FILL) { float e[3] = { col[0] * 0.6f, col[1] * 0.6f, col[2] * 0.6f }; vis_xray_fill(&o, 1, VP, cam, col, e, a, ref, vis_now); return; }
	VisLook L; memset(&L, 0, sizeof(L));
	for (int k = 0; k < 3; k++) { L.col[k] = col[k] * 0.22f * a; L.rim[k] = col[k] * 0.95f * a; } L.col[3] = 0.18f * a; L.rim[3] = 0.55f * a;
	L.prm[1] = 0.10f * a; L.prm[2] = 0.3f; L.prm[3] = 0; L.pscl[0] = 1 / 10.0f; L.pscl[3] = vis_now * 0.5f; L.pat = vis_tex_lines;
	glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul();
	vis_draw_look(&o, 1, VP, cam, &L);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}
// b121 (hands.h: the Net Jump's open hands, vm_hands.gvm's nj_* clips; EP26 4:43-4:46, 7:16-7:22): pale mint holograms in
// camera space (PV = the hands' lens x their place) - the dark world shows through them, their edges are near white, the
// finger behind shows through the one in front (no depth written: both sides of every finger add up, as the footage's
// do). k: 0..1 as they come up. Without the vision program: the model's own program, added at a third.
static void vis_hands_holo(const Model *M, const float *PV, const MBone *P, float k) {
	static const float I[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }, zero[3] = { 0, 0, 0 };
	if (!M || !M->ok || k <= 0) return;
	if (!vis_ok) { float t[4] = { 0.55f * k, 0.80f * k, 0.72f * k, 0.35f * k }, ld[4] = { 0, 0, 1, 0 }, nofog[4] = { 0, 0, 0, 0 };
		glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
		model_draw_cpu(M, NULL, PV, P, ld, t, nofog, 0);
		glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE); return; }
	float pal[48 * 12]; if (M->nb > 48) return; model_palette(M, P, pal);
	VisObj o; memset(&o, 0, sizeof(o)); o.M = M; o.pal = pal; o.Mw = I; if (!md_gpu_skin) o.skv = model_skin_get(M, P, 0, 1 << 30, 0);
	VisLook L; memset(&L, 0, sizeof(L));
	L.col[0] = 0.09f * k; L.col[1] = 0.15f * k; L.col[2] = 0.135f * k; L.col[3] = 0.17f * k;   // (premultiplied: the fill - emu b121-1 beside EP26 4:44: the hand's mean (125,153,147), its brightest (207,231,224))
	L.rim[0] = 0.48f * k; L.rim[1] = 0.58f * k; L.rim[2] = 0.55f * k; L.rim[3] = 0.32f * k;   // the edges
	L.prm[1] = 0.22f * k; L.prm[2] = 0.30f; L.prm[3] = 0; L.pscl[0] = 1 / 7.0f; L.pscl[3] = vis_now * 0.5f; L.pat = vis_tex_lines;
	glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul();
	vis_draw_mvp(&o, PV, zero, &L);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_CULL_FACE);
}
// b80 (bparts.h): the glowing cracks of a blown-off part - additive orange veins (vis_tex_crack) + a hot rim, fading with glow;
// sk: the part's own skinning (CPU) or NULL (a rigid model on pal entry 0); MVP / Mw as the part was drawn
static void vis_burn(const Model *M, CSkin *sk, const float *pal, const float *MVP, const float *Mw, const float *cam, float glow, float seed) {
	if (vis_ok != 1 || !vis_tex_crack || glow <= 0.02f || !M || !M->ok) return;
	VisObj o; memset(&o, 0, sizeof(o)); o.M = M; o.pal = pal; o.Mw = Mw;
	if (sk && sk->ok && sk->cur >= 0) { o.sk = sk; o.lod = sk->lod; }
	VisLook L; memset(&L, 0, sizeof(L)); float g = glow;
	L.col[0] = 1.00f * g; L.col[1] = 0.40f * g; L.col[2] = 0.07f * g; L.rim[0] = 0.55f * g; L.rim[1] = 0.28f * g; L.rim[2] = 0.07f * g;   // (DissolveRamp: red -> orange -> yellow)
	L.prm[2] = 1; L.pscl[0] = 1 / 28.0f; L.pscl[2] = seed; L.pscl[3] = seed * 1.7f; L.infl[1] = 0.6f; L.pat = vis_tex_crack;   // (a 28 cm tile: a few cracks across a head, as the footage - 9 cm made a glowing net)
	glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); vis_blend_premul();
	vis_draw_mvp(&o, MVP, cam, &L);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}
static inline void vis_outline_model(const Model *M, const SkinCache *sc, const float *VP, const float *Mw, const MBone *P, const float *cam, const float *col) { vis_model(M, sc, VP, Mw, P, cam, col, VIS_XRAY_LINE); }
