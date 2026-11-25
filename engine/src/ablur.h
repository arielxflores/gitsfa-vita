// ablur.h - b102 (the user: "there's a bit of a blur effect when using the speed boost and other abilities"; "if you can figure
// out how to add the blur effect on abilities without costing frame rate please do").
// The PC (Demolition video 445.22-445.50 s, an ability engaging): the picture is smeared outward from the centre - sharp in the
// middle, by about a tenth of the way to the centre at the edges - at full strength for a tenth of a second and gone within
// three; the HUD stays sharp.
// Here: for those frames the level and the weapon are drawn into a target the size of the screen instead of onto it, and one
// rectangle then puts it on the screen, each pixel the mean of 8 samples along its line to the centre (the 8 texture
// coordinates come from the vertex program, so the fetches are plain ones). The level is drawn once, as always; the HUD and
// the screen layers go on the screen after it.
// What it costs, and how that is kept from the frame rate:
//  - the target is multisampled as the screen is (the vitaGL of the SDK - ee07531, built without HAVE_ACCURATE_MSAA: its
//    glFramebufferTexture2DMultisample is an empty function - gives every target the display's mode, 2x, and patches no
//    program anew for it): the level costs what it costs on the screen and looks the same; added are a second scene and
//    one rectangle of 8 fetches a pixel. NOT measured on a Vita (none at hand for b102).
//  - so it watches the display (dstep.h's count of the VBlanks each picture took): it starts only after AB_STEADY frames in a
//    row that all took the same number; a frame that takes longer while it runs ends it at once, and after two such endings
//    it stays off until the next map (the log says so). Without that count (vsync off, the CPU clock step) it never runs.
//  - the program and the target are made at start-up and the target is drawn into once there: the first use builds nothing.
// Test switches (ux0:data/gitsfa): abluroff - never; ablurtest - every 4 s in a match, whatever the display says (the
// emulator's pictures: its frames never come steadily).
//   ab_init(mk)                 start-up (the program is in the shader bank)
//   ab_map_start()              a map starts: the endings are forgotten
//   ab_frame_begin(dt, req, ok) before the frame's clear: req = an ability of the local player started; ok = a plain match frame
//   ab_resolve()                before the first screen layer: the target onto the screen (nothing when this frame went direct)
//   ab_after_swap(n)            after the swap: the VBlanks that frame took (0 = not known)
#pragma once
#define AB_LEN 0.28f      // seconds: full for AB_HOLD, then fading out
#define AB_HOLD 0.10f
#define AB_REACH 0.085f   // the farthest of the 8 samples: this part of the way to the centre
#define AB_STEADY 45
static const char *AB_VS =
"void main(float3 pos, uniform float4 k0, uniform float4 k1, float4 out vpos : POSITION,"
" float2 out t0 : TEXCOORD0, float2 out t1 : TEXCOORD1, float2 out t2 : TEXCOORD2, float2 out t3 : TEXCOORD3,"
" float2 out t4 : TEXCOORD4, float2 out t5 : TEXCOORD5, float2 out t6 : TEXCOORD6, float2 out t7 : TEXCOORD7) {"
" vpos = float4(pos.xy, 0, 1); float2 h = pos.xy * 0.5;"
" t0 = h * k0.x + 0.5; t1 = h * k0.y + 0.5; t2 = h * k0.z + 0.5; t3 = h * k0.w + 0.5;"
" t4 = h * k1.x + 0.5; t5 = h * k1.y + 0.5; t6 = h * k1.z + 0.5; t7 = h * k1.w + 0.5; }";
static const char *AB_FS =
"float4 main(float2 t0 : TEXCOORD0, float2 t1 : TEXCOORD1, float2 t2 : TEXCOORD2, float2 t3 : TEXCOORD3,"
" float2 t4 : TEXCOORD4, float2 t5 : TEXCOORD5, float2 t6 : TEXCOORD6, float2 t7 : TEXCOORD7, uniform sampler2D tex : TEXUNIT0) : COLOR {"
" float3 c = tex2D(tex, t0).xyz + tex2D(tex, t1).xyz + tex2D(tex, t2).xyz + tex2D(tex, t3).xyz"
" + tex2D(tex, t4).xyz + tex2D(tex, t5).xyz + tex2D(tex, t6).xyz + tex2D(tex, t7).xyz; return float4(c * 0.125, 1); }";
static struct {
	int ok, off, force;          // ready; the switches
	GLuint prog, fb, tex, rb; GLint u_k0, u_k1;
	float t;                     // > 0: running, seconds since it started
	int drawing, framed;         // this frame's level goes into the target; (until the swap) this frame went through it
	int base, steady;            // the VBlanks a frame takes, and for how many frames in a row it has been just that
	int strikes, used, cut;      // endings by a late frame on this map; times it ran / was ended early (the log)
	float test_t;
} AB;
static int ab_init(sp_mkshader_fn mk) {
	{ FILE *f = fopen(VDIR "abluroff", "rb"); if (f) { AB.off = 1; fclose(f); } }
	{ FILE *f = fopen(VDIR "ablurtest", "rb"); if (f) { AB.force = 1; fclose(f); } }
	GLuint vs = mk(GL_VERTEX_SHADER, AB_VS), fs = mk(GL_FRAGMENT_SHADER, AB_FS); GLint ok = 0; GLenum st;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1; glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -2;
	AB.prog = glCreateProgram(); glAttachShader(AB.prog, vs); glAttachShader(AB.prog, fs); glBindAttribLocation(AB.prog, 0, "pos");
	glLinkProgram(AB.prog); glGetProgramiv(AB.prog, GL_LINK_STATUS, &ok); if (!ok) return -3;
	AB.u_k0 = glGetUniformLocation(AB.prog, "k0"); AB.u_k1 = glGetUniformLocation(AB.prog, "k1");
	glUseProgram(AB.prog); glUniform1i(glGetUniformLocation(AB.prog, "tex"), 0);
	if (AB.off) return 1;   // (the program is compiled all the same: the bank holds every source of the build)
	glGenTextures(1, &AB.tex); glBindTexture(GL_TEXTURE_2D, AB.tex); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &AB.fb); glBindFramebuffer(GL_FRAMEBUFFER, AB.fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, AB.tex, 0);
	glGenRenderbuffers(1, &AB.rb); glBindRenderbuffer(GL_RENDERBUFFER, AB.rb); glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 960, 544);   // (the level's passes use the stencil: bots.h, vision.h)
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, AB.rb);
	st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (st != GL_FRAMEBUFFER_COMPLETE || !AB.fb || !AB.tex) { glBindFramebuffer(GL_FRAMEBUFFER, 0); return -4; }
	glViewport(0, 0, 960, 544); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);   // (drawn into once: its surfaces exist before the first use)
	glBindFramebuffer(GL_FRAMEBUFFER, 0); AB.ok = 1; return 0;
}
static void ab_map_start(void) {
	if (AB.used) logp("ability blur: ran %d times on the last map, %d ended early by a late frame%s\n", AB.used, AB.cut, AB.strikes >= 2 ? " (then off for that map)" : "");
	AB.t = 0; AB.drawing = AB.framed = 0; AB.strikes = AB.used = AB.cut = 0; AB.steady = 0; AB.test_t = 0;
}
static void ab_frame_begin(float dt, int req, int ok) {
	AB.drawing = 0; if (!AB.ok) return;
	if (AB.force && ok) { AB.test_t += dt; if (AB.test_t > 4) { AB.test_t = 0; req = 1; } }
	if (AB.t > 0) { AB.t += dt; if (AB.t >= AB_LEN || !ok) AB.t = 0; }
	else if (req && ok && (AB.force || (AB.strikes < 2 && AB.steady >= AB_STEADY))) { AB.t = 0.0001f; AB.used++; }
	if (AB.t <= 0) return;
	glBindFramebuffer(GL_FRAMEBUFFER, AB.fb); glViewport(0, 0, 960, 544); AB.drawing = AB.framed = 1;
}
static void ab_resolve(void) {
	if (!AB.drawing) return;
	AB.drawing = 0;
	static const float Q[6 * 9] = {   // (the sprites' layout - position, uv, colour - so all three arrays point at real memory)
		-1, -1, 0, 0, 0, 1, 1, 1, 1,   1, -1, 0, 0, 0, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 1, 1, 1,
		-1, -1, 0, 0, 0, 1, 1, 1, 1,   1, 1, 0, 0, 0, 1, 1, 1, 1,   -1, 1, 0, 0, 0, 1, 1, 1, 1 };
	float s = AB.t < AB_HOLD ? 1 : 1 - (AB.t - AB_HOLD) / (AB_LEN - AB_HOLD), k[8]; if (s < 0) s = 0;
	for (int j = 0; j < 8; j++) k[j] = 1 - AB_REACH * s * j / 7;
	glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 960, 544); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	glUseProgram(AB.prog); glEnable(GL_BLEND); glDisable(GL_BLEND);   // (a real change of the blend state with the program bound: fxscene.h's note)
	glUniform4fv(AB.u_k0, 1, k); glUniform4fv(AB.u_k1, 1, k + 4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, AB.tex);
	glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 36, Q); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 36, Q + 3); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 36, Q + 5);
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
}
static void ab_after_swap(int n) {
	if (!AB.ok) return;
	int framed = AB.framed; AB.framed = 0;
	if (n <= 0 || n > 8) { AB.steady = 0; return; }
	if (framed) {
		if (!AB.force && n > AB.base) { AB.t = 0; AB.strikes++; AB.cut++; AB.steady = 0;
			logp("ability blur: a frame took %d VBlanks instead of %d - ended%s\n", n, AB.base, AB.strikes >= 2 ? "; off until the next map" : ""); }
		return; }
	if (n == AB.base) { if (AB.steady < 100000) AB.steady++; } else { AB.base = n; AB.steady = 0; }
}
