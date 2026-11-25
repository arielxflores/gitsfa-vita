// First-person viewmodel draw (needs model.h). Host test: tools/weapons/vmdraw_host.c (software GL, the shader's maths)
#pragma once
#include <string.h>

// ---- lens glass (b37, weapons agent): meshes with blend bit 7 (vm/make_vm_weapon.py LENS): a tube sight's frontmost lens
// (LENS_01) and the holo window (LENS_03). Texture: RGB = the closed lens seen at hip (the game's LENS_Blur* tint, its
// baked LENS_Blur_S reflections and dome shading), A = 0.5 + 0.5 x the aimed vignette (never 0: keeps the RGB in the mips;
// the holo window keeps LENS_03's own alpha). Attachments: 'lensenv' = (reflection texture,
// 1 = open glass / else a tube lens's opacity when closed, reflection weight in the middle of a closed lens), 'lenscoat' =
// coating colour. Drawn last (after the additive dots): a closed lens dims the reticle behind it (range footage: a faint red
// reticle in the dark lens at hip).
// The lens fades from closed (hip) to its vignette (aimed) with `ads` and takes a moving reflection: lensenv (the game's
// CUBE.dds street, mirrored into a seamless panorama) looked up with the reflected view ray, scrolled by the camera yaw /
// pitch, tinted by the coating, stronger toward the rim. One extra texture read on a few thousand pixels; no extra pass.
// Host twin: vm/vmgl.py LVS / LFS. Without the program (wvm_lens_ok 0) the lens meshes go through the model shader.
#define WVM_LENS_BIT 128
static const char *WVM_LENS_VS =
"void main(float3 pos, float4 nrm, float2 uv,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[3], uniform float4 lp, uniform float4 ko,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out te : TEXCOORD1, float out fr : TEXCOORD2) {"
" float4 p = float4(pos, 1); float3 s = float3(dot(pal[0], p), dot(pal[1], p), dot(pal[2], p));"
" float3 n = normalize(float3(dot(pal[0].xyz, nrm.xyz), dot(pal[1].xyz, nrm.xyz), dot(pal[2].xyz, nrm.xyz)) + float3(0, 0, 0.0001));"
" float3 v = normalize(s + ko.xyz); float3 r = reflect(v, n); float c = 1.0 - saturate(-dot(v, n)); fr = 0.6 + 0.4 * c * c;"
" te = float2(r.x * 0.14 + lp.y, 0.5 - r.y * 0.2 + lp.z);"
" float4 q = float4(s, 1); vpos = float4(dot(m0, q), dot(m1, q), dot(m2, q), dot(m3, q)); t0 = uv; }";
#define WVM_LENS_FS_PARAMS "float4 main(float2 t0 : TEXCOORD0, float2 te : TEXCOORD1, float fr : TEXCOORD2, uniform sampler2D dif : TEXUNIT0, uniform sampler2D env : TEXUNIT1, uniform float4 tint, uniform float4 coat, uniform float4 lk"
#define WVM_LENS_FS_BODY " float4 b = tex2D(dif, t0); float3 e = tex2D(env, te).rgb; float2 d = t0 * 2.0 - 1.0; float rr = saturate(dot(d, d));" \
	" float a = lerp(coat.a, lerp(saturate(b.a * 2.0 - 1.0), b.a, lk.y), lk.x); float3 bc = b.rgb * lerp(1.0, lk.y, lk.x); float3 rf = e * coat.rgb * (fr * lerp(lk.w + (1.0 - lk.w) * rr, rr * rr * lk.z, lk.x * (1.0 - lk.y)));" \
	" float ao = saturate(a + dot(rf, float3(0.3, 0.5, 0.2))); float3 fc = (bc * a + rf) * tint.rgb / max(ao, 0.002);"
#define WVM_LENS_FS_RET " return float4(gc, ao * tint.a); }"
// render b39: the same in half precision (texture coordinates stay float, the fresnel varying is half; grade.h grade_src_h tails); tried first
#define WVM_LENS_FS_PARAMS_H "float4 main(float2 t0 : TEXCOORD0, float2 te : TEXCOORD1, half fr : TEXCOORD2, uniform sampler2D dif : TEXUNIT0, uniform sampler2D env : TEXUNIT1, uniform float4 tint, uniform float4 coat, uniform float4 lk"
#define WVM_LENS_FS_BODY_H " half4 b = (half4)tex2D(dif, t0); half3 e = (half3)tex2D(env, te).rgb; half2 d = (half2)t0 * 2.0h - 1.0h; half rr = saturate(dot(d, d));" \
	" half4 k = (half4)lk; half4 ct = (half4)coat;" \
	" half a = lerp(ct.a, lerp(saturate(b.a * 2.0h - 1.0h), b.a, k.y), k.x); half3 bc = b.rgb * lerp(1.0h, k.y, k.x); half3 rf = e * ct.rgb * (fr * lerp(k.w + (1.0h - k.w) * rr, rr * rr * k.z, k.x * (1.0h - k.y)));" \
	" half ao = saturate(a + dot(rf, half3(0.3h, 0.5h, 0.2h))); half3 fc = (bc * a + rf) * (half3)tint.rgb / max(ao, 0.002h);"
#define WVM_LENS_FS_RET_H " return float4((float3)gc, (float)(ao * (half)tint.a)); }"
// b66 (user: "make opaque matte sniper or gun scopes look more reflective or glassy when carrying them around"; the PC
// footage, e.g. the L118A at hip in "TACHIKOMAS!! ... Machina Media" 5:50-6:20: a deep copper dome with the sky reflected
// on its upper half, darker toward the bezel, soft streaks; the b64 device shot of the same lens: the same mean colour
// but flat, 3x less variation). The closed lens (hip) becomes curved glass: the lens normal is bent outward from the
// lens centre (lc: centre in the mesh's space, w = dome / radius; MMesh.lc, model.h) so the panorama reflection wraps
// round it; Schlick fresnel (F0 0.15) between the b65 lens colour (fo x 0.85: each scope keeps its own copper / amber /
// blue, a soft shadow inside the bezel) and the panorama
// (squared x 2.45 for contrast: bright sky, dark ground) tinted by the coating; a soft glint from the viewmodel's highlight
// direction (gl: camera space, w = strength). Aimed (lk.x = 1) and the holo window (always open) are the b65 formula
// unchanged (fo); in between the two mix. Host check: mean colour as b65 per scope (within 6 %, same hue),
// 2-3x the variation (footage 17-21, b65 6.7-9.5, b66 15-19). Host twin: vm/vmgl.py LVS / LFS. Test switch oldlens: b65.
// b82 (the user's Batou footage and screenshot: "make the scopes transparent"; the M4A1's tube red dot at hip is dark
// see-through glass - the inner tube and its rings, the world at the far end, the red dot, a sharp highlight): at hip the
// closed lens is no longer an opaque coloured dome. Its opacity is coat.a x 0.3 (the tint) + a dark bezel shadow toward the
// rim + the reflection's own brightness; the reflection is glass-like (Schlick F0 0.04, x 0.6, the coating half mixed in)
// and the glint stays. Aimed and the holo window are unchanged (lk.x = 1). vmgl renders: acr / scar / l118 / m4a1rd.
static const char *WVM_GLASS_VS =
"void main(float3 pos, float4 nrm, float2 uv,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[3], uniform float4 lp, uniform float4 ko, uniform float4 lc,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float2 out te : TEXCOORD1, float out fr : TEXCOORD2, float2 out te2 : TEXCOORD3, float3 out nd : TEXCOORD4, float3 out vv : TEXCOORD5) {"
" float4 p = float4(pos, 1); float3 s = float3(dot(pal[0], p), dot(pal[1], p), dot(pal[2], p));"
" float3 n = normalize(float3(dot(pal[0].xyz, nrm.xyz), dot(pal[1].xyz, nrm.xyz), dot(pal[2].xyz, nrm.xyz)) + float3(0, 0, 0.0001));"
" float3 v = normalize(s + ko.xyz); float3 r = reflect(v, n); float c = 1.0 - saturate(-dot(v, n)); fr = 0.6 + 0.4 * c * c;"
" te = float2(r.x * 0.14 + lp.y, 0.5 - r.y * 0.2 + lp.z);"
" float4 q = float4(lc.xyz, 1); float3 sc = float3(dot(pal[0], q), dot(pal[1], q), dot(pal[2], q)); float3 n2 = normalize(n + (s - sc) * lc.w);"
" float3 r2 = reflect(v, n2); te2 = float2(r2.x * 0.14 + lp.y, 0.5 - r2.y * 0.2 + lp.z); nd = n2; vv = v;"
" float4 w = float4(s, 1); vpos = float4(dot(m0, w), dot(m1, w), dot(m2, w), dot(m3, w)); t0 = uv; }";
#define WVM_GLASS_FS_PARAMS "float4 main(float2 t0 : TEXCOORD0, float2 te : TEXCOORD1, float fr : TEXCOORD2, float2 te2 : TEXCOORD3, float3 nd : TEXCOORD4, float3 vv : TEXCOORD5, uniform sampler2D dif : TEXUNIT0, uniform sampler2D env : TEXUNIT1, uniform float4 tint, uniform float4 coat, uniform float4 lk, uniform float4 gl"
#define WVM_GLASS_FS_BODY " float4 b = tex2D(dif, t0); float3 e = tex2D(env, te).rgb; float2 d = t0 * 2.0 - 1.0; float rr = saturate(dot(d, d));" \
	" float a = lerp(coat.a, lerp(saturate(b.a * 2.0 - 1.0), b.a, lk.y), lk.x); float3 bc = b.rgb * lerp(1.0, lk.y, lk.x); float3 rf = e * coat.rgb * (fr * lerp(lk.w + (1.0 - lk.w) * rr, rr * rr * lk.z, lk.x * (1.0 - lk.y)));" \
	" float ao = saturate(a + dot(rf, float3(0.3, 0.5, 0.2))); float3 fo = (bc * a + rf) / max(ao, 0.002);" \
	" float3 n = normalize(nd), v = normalize(vv); float cs = 1.0 - saturate(-dot(v, n)); float c2 = cs * cs; float F = 0.04 + 0.96 * c2 * c2 * cs;" \
	" float3 e2 = tex2D(env, te2).rgb; e2 = e2 * e2 * 2.45; float g = pow(saturate(dot(n, normalize(gl.xyz - v))), 28.0) * gl.w;" \
	" float3 R = e2 * lerp(float3(1.0, 1.0, 1.0), coat.rgb, 0.5) * (F * 0.6) + g; float A0 = coat.a * 0.3;" \
	" float Ah = saturate(A0 + 0.55 * smoothstep(0.55, 1.0, rr) + dot(R, float3(0.3, 0.5, 0.2))); float3 cl = (fo * (0.4 * A0) + R) / max(Ah, 0.002);" \
	" float3 fc = lerp(cl, fo, lk.x) * tint.rgb; ao = lerp(Ah, ao, lk.x);"
#define WVM_GLASS_FS_PARAMS_H "float4 main(float2 t0 : TEXCOORD0, float2 te : TEXCOORD1, half fr : TEXCOORD2, float2 te2 : TEXCOORD3, half3 nd : TEXCOORD4, half3 vv : TEXCOORD5, uniform sampler2D dif : TEXUNIT0, uniform sampler2D env : TEXUNIT1, uniform float4 tint, uniform float4 coat, uniform float4 lk, uniform float4 gl"
#define WVM_GLASS_FS_BODY_H " half4 b = (half4)tex2D(dif, t0); half3 e = (half3)tex2D(env, te).rgb; half2 d = (half2)t0 * 2.0h - 1.0h; half rr = saturate(dot(d, d));" \
	" half4 k = (half4)lk; half4 ct = (half4)coat;" \
	" half a = lerp(ct.a, lerp(saturate(b.a * 2.0h - 1.0h), b.a, k.y), k.x); half3 bc = b.rgb * lerp(1.0h, k.y, k.x); half3 rf = e * ct.rgb * (fr * lerp(k.w + (1.0h - k.w) * rr, rr * rr * k.z, k.x * (1.0h - k.y)));" \
	" half ao = saturate(a + dot(rf, half3(0.3h, 0.5h, 0.2h))); half3 fo = (bc * a + rf) / max(ao, 0.002h);" \
	" half3 n = normalize(nd), v = normalize(vv); half cs = 1.0h - saturate(-dot(v, n)); half c2 = cs * cs; half F = 0.04h + 0.96h * c2 * c2 * cs;" \
	" half3 e2 = (half3)tex2D(env, te2).rgb; e2 = e2 * e2 * 2.45h; half g = pow(saturate(dot(n, normalize((half3)gl.xyz - v))), 28.0h) * (half)gl.w;" \
	" half3 R = e2 * lerp(half3(1.0h, 1.0h, 1.0h), ct.rgb, 0.5h) * (F * 0.6h) + g; half A0 = ct.a * 0.3h;" \
	" half Ah = saturate(A0 + 0.55h * smoothstep(0.55h, 1.0h, rr) + dot(R, half3(0.3h, 0.5h, 0.2h))); half3 cl = (fo * (0.4h * A0) + R) / max(Ah, 0.002h);" \
	" half3 fc = lerp(cl, fo, k.x) * (half3)tint.rgb; ao = lerp(Ah, ao, k.x);"
static int wvm_glass;   // b66: 1 = the lens program is WVM_GLASS (else b65's WVM_LENS: test switch oldlens, or it failed)
static GLint wvm_lu_lc = -1, wvm_lu_gl = -1;
static int wvm_lens_half;   // render b39: 1 = the lens program is the half one
static GLuint wvm_lprog; static GLint wvm_lu_m[4], wvm_lu_pal, wvm_lu_lp, wvm_lu_ko, wvm_lu_tint, wvm_lu_coat, wvm_lu_lk; static int wvm_lens_ok;
static float wvm_env_u, wvm_env_v, wvm_ko[3];   // main.c, per frame: camera yaw / 2 pi, camera pitch (rad), the viewmodel's K offset
static int wvm_lens_draws;   // lens meshes drawn (stats)
// render b43: on the GPU-skinning path (md_gpu_vm) the rigid parts (the gun's body, sights, attachments: 2-13K vertices a
// frame) go through model.h's one-bone program MD_VS_R (~25 instructions a vertex) instead of the 4-bone MD_VS (64) with
// a one-entry palette: the same maths (rigid vertices are entry 0 at weight 1). ux0:data/gitsfa/novmrigid: as before.
static int wvm_rigid1 = 1;
static int wvm_lens_init(GLuint (*mk)(GLenum, const char *), void (*log)(const char *, ...)) {
	{ FILE *f = fopen("ux0:data/gitsfa/novmrigid", "rb"); if (f) { fclose(f); wvm_rigid1 = 0; } }   // render b43
	log("viewmodel rigid parts on the GPU path: %s (render b43)\n", wvm_rigid1 ? "one-bone program" : "4-bone program (novmrigid)");
	static char src[3000]; GLint ok = 0; GLuint vs = 0, fs = 0;   // (render b39: fs = 0)
	{ FILE *f = fopen("ux0:data/gitsfa/oldlens", "rb"); wvm_glass = f == NULL; if (f) fclose(f); }   // b66: the glass lens first (test switch oldlens: b65's)
	for (;; wvm_glass = 0) {   // (b66: the glass program, else b65's lens program)
	const char *VS = wvm_glass ? WVM_GLASS_VS : WVM_LENS_VS, *PA = wvm_glass ? WVM_GLASS_FS_PARAMS : WVM_LENS_FS_PARAMS, *BO = wvm_glass ? WVM_GLASS_FS_BODY : WVM_LENS_FS_BODY;
	const char *PAH = wvm_glass ? WVM_GLASS_FS_PARAMS_H : WVM_LENS_FS_PARAMS_H, *BOH = wvm_glass ? WVM_GLASS_FS_BODY_H : WVM_LENS_FS_BODY_H;
	ok = 0; vs = mk(GL_VERTEX_SHADER, VS);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) { log("lens shader: %s vertex shader failed\n", wvm_glass ? "glass" : "lens"); if (wvm_glass) continue; return -1; }
	ok = 0; wvm_lens_half = 0;   // render b39: the half form first (nohalf: off), float if it does not compile
	if (grade_half() && grade_src_h(src, sizeof(src), PAH, BOH, WVM_LENS_FS_RET_H, md_grade) >= 0) {
		fs = mk(GL_FRAGMENT_SHADER, src); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); wvm_lens_half = ok;
		if (!ok) log("lens shader: half form failed, using float\n");
	}
	if (!ok) {
	if (md_grade == GRADE_OFF) snprintf(src, sizeof(src), "%s) : COLOR {%s float3 gc = fc;%s", PA, BO, WVM_LENS_FS_RET);
	else if (grade_src(src, sizeof(src), PA, BO, WVM_LENS_FS_RET, md_grade) < 0) { log("lens shader: source too long\n"); if (wvm_glass) continue; return -2; }
	fs = mk(GL_FRAGMENT_SHADER, src); glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) { log("lens shader: %s fragment shader failed\n", wvm_glass ? "glass" : "lens"); if (wvm_glass) continue; return -3; }
	}
	break; }
	wvm_lprog = glCreateProgram(); glAttachShader(wvm_lprog, vs); glAttachShader(wvm_lprog, fs);
	glBindAttribLocation(wvm_lprog, 0, "pos"); glBindAttribLocation(wvm_lprog, 1, "nrm"); glBindAttribLocation(wvm_lprog, 2, "uv");
	glLinkProgram(wvm_lprog); glGetProgramiv(wvm_lprog, GL_LINK_STATUS, &ok); if (!ok) { log("lens shader: link failed\n"); return -4; }
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; wvm_lu_m[r] = glGetUniformLocation(wvm_lprog, nm); }
	wvm_lu_pal = glGetUniformLocation(wvm_lprog, "pal"); wvm_lu_lp = glGetUniformLocation(wvm_lprog, "lp"); wvm_lu_ko = glGetUniformLocation(wvm_lprog, "ko");
	wvm_lu_tint = glGetUniformLocation(wvm_lprog, "tint"); wvm_lu_coat = glGetUniformLocation(wvm_lprog, "coat"); wvm_lu_lk = glGetUniformLocation(wvm_lprog, "lk");
	wvm_lu_lc = glGetUniformLocation(wvm_lprog, "lc"); wvm_lu_gl = glGetUniformLocation(wvm_lprog, "gl");   // b66 (glass only)
	glUseProgram(wvm_lprog); glUniform1i(glGetUniformLocation(wvm_lprog, "dif"), 0); glUniform1i(glGetUniformLocation(wvm_lprog, "env"), 1);
	grade_register(wvm_lprog, md_grade);
	wvm_lens_ok = wvm_lu_pal >= 0 && wvm_lu_m[0] >= 0;
	log("lens shader: %s%s (pal %d lp %d lk %d, grading %s%s)\n", wvm_lens_ok ? "ok" : "FAILED", wvm_glass ? ", glass (b66)" : "", wvm_lu_pal, wvm_lu_lp, wvm_lu_lk, grade_mode_name[md_grade], wvm_lens_half ? ", half" : "");   // (render b39: half note)
	glUseProgram(md_prog); return wvm_lens_ok ? 0 : -5;
}
// one lens mesh (rigid, its bone's matrix as a one-entry palette) with the lens program; leaves the model program current
static void wvm_lens_mesh(const Model *M, const MMesh *m, const float *mvp, const float *tint, float ads) {
	const float *le = model_attach(M, "lensenv"), *lc = model_attach(M, "lenscoat");
	int et = le ? (int)le[0] : -1; if (et < 0 || et >= M->ntex || m->rigid < 0) return;
	int glass = le[1] >= 1; float op = glass ? 1 : fminf(1, fmaxf(0, (ads - 0.38f) * 8.4f));   // a tube lens opens up to its vignette at ads 0.38-0.5 (as the hip-only inner tube goes)
	float lp[4] = { 0, wvm_env_u, fminf(0.15f, fmaxf(-0.15f, -wvm_env_v * 0.25f)), 0 }, ko[4] = { wvm_ko[0], wvm_ko[1], wvm_ko[2], 0 };
	// b109 (user, the KRIS-SV: "the reticle ... seems to be like a foggy transparent square which doesn't look correct"): the holo window's
	// reflection of the street picture was as strong as a tube lens's coating - a milky veil over the whole window, right where the
	// reticle and the target are (the Vita's own shots 86 / 114 of b108). An open window keeps a quarter of it: clear glass with a
	// faint sheen and the game's own slight tint (LENS_03's alpha), the reticle on top.
	const float gk = glass ? 0.25f : 1.0f;
	float coat[4] = { (lc ? lc[0] : 0.5f) * gk, (lc ? lc[1] : 0.5f) * gk, (lc ? lc[2] : 0.5f) * gk, glass ? 1 : le[1] }, lk[4] = { op, glass ? 1.f : 0.f, 0.6f, le[2] };
	glUseProgram(wvm_lprog);
	for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(wvm_lu_m[r], 1, row); }
	glUniform4fv(wvm_lu_pal, 3, md_palbuf + m->rigid * 12); glUniform4fv(wvm_lu_lp, 1, lp); glUniform4fv(wvm_lu_ko, 1, ko);
	glUniform4fv(wvm_lu_tint, 1, tint); glUniform4fv(wvm_lu_coat, 1, coat); glUniform4fv(wvm_lu_lk, 1, lk);
	if (wvm_glass) {   // b66: the dome (centre in the mesh's space, dome / radius) and the glint (the viewmodel's highlight direction, camera space)
		float lcu[4] = { m->lc[0], m->lc[1], m->lc[2], m->lc[3] > 1e-4f ? 2.0f / m->lc[3] : 0 }, gl[4] = { 0.15f, 1.0f, -0.25f, 0.5f };
		if (md_sld_dir) memcpy(gl, md_sld_dir, 12);
		{ float n = sqrtf(gl[0] * gl[0] + gl[1] * gl[1] + gl[2] * gl[2]); if (n > 1e-6f) for (int k = 0; k < 3; k++) gl[k] /= n; }
		glUniform4fv(wvm_lu_lc, 1, lcu); glUniform4fv(wvm_lu_gl, 1, gl); }
	glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, M->tex[et]); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4); glBindBuffer(GL_ARRAY_BUFFER, m->vbo);   // (no bone attributes: one rigid matrix)
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
	glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glUseProgram(md_prog); wvm_lens_draws++;
}

// ---- viewmodel draw (same shader and uniforms as model_draw; md_mesh0 / md_mesh1 still select the meshes)
// chars b35: the arms and the gun's moving parts are skinned on the CPU (model.h model_skin_get: skin.h, NEON on the
// worker cores, once per frame and pose) and drawn with a one-entry identity palette; rigid parts with their bone's
// matrix as a one-entry palette. b19-b32 on the device: the shader's palette path lost the arms in most poses and
// stretched the M4A1's front into spikes when sprinting (b32 shot 17): it is used only when md_gpu_vm says the start-up
// self-test proved it on this device (skin_gpu.h).
// b60: wvm_fp_on (main.c sets it around the first-person draw when wvm_fp_state() says so): meshes that have a light
// first-person version (Model.fp) are drawn from it: the same triangles wherever the gun and arms can be on screen in
// those clips, a coarse closed surface elsewhere. Rigid parts and GPU-skinned ones; the CPU skinning fallback keeps the
// full meshes (its skin plans are built from them). wvm_fp_idx: indices drawn by the last call (the per-second line).
static int wvm_fp_on, wvm_fp_idx;
static void wvm_draw_(const Model *M, const float *mvp, const MBone *P, const float *ldir, const float *tint, float ads, const float *fogc);
static void wvm_draw(const Model *M, const float *mvp, const MBone *P, const float *ldir, const float *tint, float ads, const float *fogc) {
	md_env_use(M); wvm_draw_(M, mvp, P, ldir, tint, ads, fogc); md_env_use(NULL);   // (b112: the cyborg arms' own reflection picture, model.h)
}
static void wvm_draw_(const Model *M, const float *mvp, const MBone *P, const float *ldir, const float *tint, float ads, const float *fogc) {
	static const float nofog[4] = { 0, 0, 0, 0 }; if (!fogc) fogc = nofog;
	wvm_fp_idx = 0;
	model_palette(M, P, md_palbuf);
	const GLuint *vb = md_gpu_vm ? NULL : model_skin_get(M, P, md_mesh0, md_mesh1, 0);
	GLint ub = md_u_pal; GLuint prog = md_prog;
	if (vb) { ub = md_begin_bone(mvp, ldir, tint, fogc); prog = md_prog_r ? md_prog_r : md_prog; }   // CPU-skinned: the one-bone program (model.h, chars b41)
	else {
		glUseProgram(md_prog); md_cur_r = 0;
		for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(md_u_m[r], 1, row); }
		glUniform4fv(md_u_ldir, 1, ldir); glUniform4fv(md_u_tint, 1, tint); glUniform4fv(md_u_fog, 1, fogc); if (md_u_fogp >= 0) glUniform4fv(md_u_fogp, 1, md_fogp);
		md_eye_set(md_u_eye, md_u_sld, mvp, ldir);   // (b62: highlight)
		glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	}
	int pb = -1;
	int vr = !vb && md_prog_r && wvm_rigid1, cr = 0; GLint ubr = -1;   // render b43: rigid parts through MD_VS_R on the GPU path (cr: it is current)
	for (int pass = 0; pass < 4; pass++) {   // 0 opaque, 1 alpha, 2 additive, 3 lens glass (alpha, over the reticles and dots behind it)
		if (pass == 1 || pass == 3) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE); }
		if (pass == 2) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		for (int i = 0; i < M->nmesh; i++) {
			const MMesh *m = &M->mesh[i]; int b = m->blend & 127, ni = m->ni; GLuint ibo = m->ibo, vbo = m->vbo, lens = (m->blend & WVM_LENS_BIT) && wvm_lens_ok && m->rigid >= 0;
			if (i < md_mesh0 || i >= md_mesh1) continue;
			if (wvm_fp_on && M->fp && M->fp[i].ni && (m->rigid >= 0 || !vb)) { vbo = M->fp[i].vbo; ibo = M->fp[i].ibo; ni = M->fp[i].ni; }   // b60: the light mesh
			if (pass == 0 ? !(b == 0 || (b == 3 && ads < 0.5f) || b > 3) : pass == 1 ? b != 1 || lens : pass == 2 ? b != 2 : !lens) continue;
			if (lens) { wvm_lens_mesh(M, m, mvp, tint, ads); pb = -1; cr = 0; md_cur_r = 0; if (prog != md_prog) { glUseProgram(prog); md_cur_r = prog == md_prog_r; } continue; }   // lens glass (it leaves md_prog current; render b43: cr)
			glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
			if (m->rigid >= 0 && vr) {   // render b43: the one-bone program (uniforms set once a draw; md_prog keeps its palette window)
				if (!cr) { if (ubr < 0) ubr = md_begin_bone(mvp, ldir, tint, fogc); else { glUseProgram(md_prog_r); md_cur_r = 1; glDisableVertexAttribArray(3); glDisableVertexAttribArray(4); } cr = 1; }
				glUniform4fv(ubr, 3, md_palbuf + m->rigid * 12); glBindBuffer(GL_ARRAY_BUFFER, vbo); }
			else if (m->rigid >= 0) { glUniform4fv(ub, 3, md_palbuf + m->rigid * 12); pb = -1; glBindBuffer(GL_ARRAY_BUFFER, vbo); }   // one bone (model.h)
			else if (vb) { if (i >= SKF_MESH || !vb[i]) continue; glUniform4fv(ub, 3, md_identity); pb = -1; skin_bind_out(m->sp, vb[i]); ibo = m->sp->ibo; ni = m->sp->ni; }
			else { if (cr) { glUseProgram(md_prog); md_cur_r = 0; glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); cr = 0; }   // render b43: back to the skinning program
				if (m->pbase != pb && m->pbase < M->npal) { pb = m->pbase; md_upload_window(md_u_pal, md_palbuf, pb, M->npal); } glBindBuffer(GL_ARRAY_BUFFER, vbo); }   // palette window (model.h: 4 rows per entry)
			if (m->rigid >= 0 || !vb) {
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
				glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
				glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16);
				glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
				glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
			}
			md_mesh_emis(m);   // (b71: the glow, model.h)
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glDrawElements(GL_TRIANGLES, ni, GL_UNSIGNED_SHORT, 0); wvm_fp_idx += ni;
		}
	}
	glDisable(GL_BLEND); glDepthMask(GL_TRUE);
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
