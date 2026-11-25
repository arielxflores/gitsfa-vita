// skinprobe.h - start-up test of the model shader's bone palette (md_prog) on this GPU (build 33; chars b41).
// One small quad per palette entry k (every vertex on bone k, weight 1) is drawn into a 64x4 render target where entry e
// moves a quad to column e; the pixels read back say which entries land where they should. A quad in the wrong column
// says which entry the shader really read; no quad ('x') means the fetch was garbage (clipped).
//
// b40 on the device (3 rows per entry): even entries right, odd entries 'x', in single-entry draws and with all 48 in one
// draw. The compiled vertex program (GXP disassembly, Vita3K's translator) fetches an entry's 3 rows with ONE 48-byte LDR:
//   VPCKU16F32 idx = u16(bi * 3 + 0.5);  IMAD2 addr = idx * 16 + pal_base;  LDR r[0..11] <- addr, 48 bytes
// so entry e is read from pal_base + 48e, which is 16 mod 32 for odd e: the GPU does not read a multi-row load right
// from an address that is not 32-byte aligned (Vita3K does). b41: 4 rows per entry (model.h md_upload_window), so every
// entry starts at pal_base + 64e.
// What this logs (all logging; skin_probe_ok feeds skin_gpu.h's decision):
//   0. a readback control (white clear, read back) at the start (up to 3 tries) and the end: if it does not read 256 of 256,
//      the readback is broken and every 'x' is meaningless (skin_probe_ok 0 all the same)
//   1. per window and all 48 entries in one draw, with md_prog (4 rows per entry)
//   2. the same one-draw test with probe-only programs in other layouts (b40's 3 rows per entry: expected ".x.x..." on
//      the device; 3 rows from row 1, 4 rows from row 1 or 2, 2 rows per entry): which byte offsets inside pal work
//   3. the one-draw test with md_prog after shifting vitaGL's uniform pool by 0..7 small uniform uploads (a tiny draw whose
//      default uniform buffer is 8 bytes, logged): vitaGL packs default uniform buffers back to back, so the absolute
//      address of pal moves with whatever was drawn before. If only some phases pass, the absolute address matters, not
//      only the offset inside pal, and the shader path cannot be trusted in a real frame.
#pragma once
// (skin_probe_ok lives in model.h: 1 every entry right in every test above, 0 not, -1 not run)
#ifdef __vita__
// comparison layouts (probe-only programs): entry e = rows S*e + O .. S*e + O + 2 of pal[], i.e. fetched from byte
// 16 * (S*e + O) of pal. S 3 O 0 is b40's layout; the others pin down the alignment rule (32 bytes? 64?)
static char skp_vs_src[900];
static const char *skp_vs(int S, int O) {
	snprintf(skp_vs_src, sizeof(skp_vs_src),
		"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw,"
		" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[%d], uniform float4 ldir, uniform float4 fogp,"
		" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float out sh : TEXCOORD1, float out fog : TEXCOORD2) {"
		" float4 p = float4(pos, 1); int i0 = (int)(bi.x * %d.0 + %d.5);"
		" float4 c = float4(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p), 1); vpos = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c)); t0 = uv;"
		" sh = 1.0; fog = 0.0; }", S * 48 + O + 3, S, O);
	return skp_vs_src;
}
static const char *SKP_FS = "float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2) : COLOR { return float4(1, 1, 1, 1); }";
// the pool shifter: one float2 uniform, so each draw reserves a small default uniform buffer from vitaGL's pool
static const char *SKP_SVS = "void main(float2 p, uniform float2 sft, float4 out vpos : POSITION) { vpos = float4(p + sft, 0.5, 1); }";
static const char *SKP_SFS = "float4 main() : COLOR { return float4(0, 0, 0, 0); }";
static unsigned skp_dub(GLuint sh) {   // a compiled shader's default uniform buffer size in bytes (0: unknown)
	static unsigned char bin[16384] __attribute__((aligned(16))); GLsizei len = 0; if (!sh) return 0;
	vglGetShaderBinary(sh, sizeof(bin), &len, bin);
	for (int i = 0; i + 104 <= len && i + 104 <= (int)sizeof(bin); i += 4) if (!memcmp(bin + i, "GXP", 4)) return sceGxmProgramGetDefaultUniformBufferSize((const SceGxmProgram *)(bin + i));
	return 0;
}
static GLuint skp_prog(const char *vs, const char *fs, int skin, GLuint *vsh) {
	if (!md_mk) return 0;
	GLuint v = md_mk(GL_VERTEX_SHADER, vs), f = md_mk(GL_FRAGMENT_SHADER, fs); GLint ok = 0;
	if (vsh) *vsh = v;
	glGetShaderiv(v, GL_COMPILE_STATUS, &ok); if (!ok) return 0; glGetShaderiv(f, GL_COMPILE_STATUS, &ok); if (!ok) return 0;
	GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
	if (skin) { glBindAttribLocation(p, 0, "pos"); glBindAttribLocation(p, 1, "nrm"); glBindAttribLocation(p, 2, "uv"); glBindAttribLocation(p, 3, "bi"); glBindAttribLocation(p, 4, "bw"); }
	else glBindAttribLocation(p, 0, "p");
	glLinkProgram(p); glGetProgramiv(p, GL_LINK_STATUS, &ok);
	return ok ? p : 0;
}
enum { SKP_W = 64, SKP_H = 4, SKP_NE = 48 };
static unsigned char skp_px[SKP_W * SKP_H * 4];
// draws quads k0 .. k0+n-1 with the program in use (uniforms set by the caller) and reads back; marks[k]: '.' right,
// 'w' wrong column (one quad only), 'x' none
static int skp_read(int k0, int n, int pb, char *marks, char *wrong, int wsz) {
	glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, k0 * 6, n * 6);
	memset(skp_px, 0, sizeof(skp_px)); glReadPixels(0, 0, SKP_W, SKP_H, GL_RGBA, GL_UNSIGNED_BYTE, skp_px);
	int nok = 0, wl = 0; if (wrong) wrong[0] = 0;
	if (n == 1) {   // one quad: where did it land?
		int lit = -1, nlit = 0; for (int x = 0; x < SKP_W; x++) if (skp_px[x * 4 + 3] > 128) { if (lit < 0) lit = x; nlit++; }
		int want = pb + k0; marks[0] = nlit == 1 && lit == want ? '.' : nlit ? 'w' : 'x'; nok = marks[0] == '.';
		if (nlit && marks[0] != '.' && wrong) snprintf(wrong, wsz, " %d->%d%s", k0, lit - pb, nlit > 1 ? "+" : "");
		return nok;
	}
	for (int k = 0; k < n; k++) { int x = pb + k0 + k, lit = x < SKP_W && skp_px[x * 4 + 3] > 128; marks[k] = lit ? '.' : 'x'; nok += lit; }
	for (int x = pb + k0 + n; x < SKP_W; x++) wl += skp_px[x * 4 + 3] > 128;
	if (wrong && wl) snprintf(wrong, wsz, ", %d columns lit past the palette", wl);
	return nok;
}
// control: clear the target to opaque white and read it back; returns how many of the 256 pixels came back opaque. Less
// than 256 means the readback itself does not work here (then every 'x' below says nothing about the palette)
static int skp_control(void) {
	glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
	memset(skp_px, 0, sizeof(skp_px)); glReadPixels(0, 0, SKP_W, SKP_H, GL_RGBA, GL_UNSIGNED_BYTE, skp_px);
	int n = 0; for (int i = 0; i < SKP_W * SKP_H; i++) n += skp_px[i * 4 + 3] > 128;
	glClearColor(0, 0, 0, 0); return n;
}
static void skp_uniforms(GLuint prog, const float M[4][4]) {
	glUseProgram(prog);
	const float ld[4] = { 0, 0, 1, 0 }, tint[4] = { 1, 1, 1, 1 }, fogc[4] = { 0, 0, 0, 0 }, fogp[4] = { 1000, 0, 0, 0 };
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; glUniform4fv(glGetUniformLocation(prog, nm), 1, M[r]); }
	GLint u;
	if ((u = glGetUniformLocation(prog, "ldir")) >= 0) glUniform4fv(u, 1, ld);
	if ((u = glGetUniformLocation(prog, "tint")) >= 0) glUniform4fv(u, 1, tint);
	if ((u = glGetUniformLocation(prog, "emis")) >= 0) { static const float z4[4] = { 0, 0, 0, 0 }; glUniform4fv(u, 1, z4); }   // (b71: no glow)
	if ((u = glGetUniformLocation(prog, "fogc")) >= 0) glUniform4fv(u, 1, fogc);
	if ((u = glGetUniformLocation(prog, "fogp")) >= 0) glUniform4fv(u, 1, fogp);
}
static void skinprobe_run(void (*log)(const char *, ...)) {
	static unsigned char vb[SKP_NE * 6 * 32];
	static const float cx[6] = { 0.15f, 0.85f, 0.85f, 0.15f, 0.85f, 0.15f }, cy[6] = { 0, 0, SKP_H, 0, SKP_H, SKP_H };
	memset(vb, 0, sizeof(vb));
	for (int k = 0; k < SKP_NE; k++) for (int v = 0; v < 6; v++) {
		unsigned char *q = vb + (k * 6 + v) * 32; float p[3] = { cx[v], cy[v], 0 }, uv[2] = { 0.5f, 0.5f };
		memcpy(q, p, 12); q[14] = 127; memcpy(q + 16, uv, 8);
		q[24] = q[25] = q[26] = q[27] = (unsigned char)k; q[28] = 255;
	}
	static unsigned char white[4 * 4 * 4]; memset(white, 255, sizeof(white));
	GLuint vbo = 0, wt = 0, ct = 0, fb = 0, svbo = 0;
	glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(vb), vb, GL_STATIC_DRAW);
	{ const float sv[6] = { 0, 0, 0, 0, 0, 0 }; glGenBuffers(1, &svbo); glBindBuffer(GL_ARRAY_BUFFER, svbo); glBufferData(GL_ARRAY_BUFFER, sizeof(sv), sv, GL_STATIC_DRAW); }
	glGenTextures(1, &wt); glBindTexture(GL_TEXTURE_2D, wt); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenTextures(1, &ct); glBindTexture(GL_TEXTURE_2D, ct); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SKP_W, SKP_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fb); glBindFramebuffer(GL_FRAMEBUFFER, fb); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
	glViewport(0, 0, SKP_W, SKP_H); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDepthMask(GL_FALSE);
	const float M[4][4] = { { 2.f / SKP_W, 0, 0, -1 }, { 0, 2.f / SKP_H, 0, -1 }, { 0, 0, 0, 0.5f }, { 0, 0, 0, 1 } };
	static float pal[SKP_NE * 12]; memset(pal, 0, sizeof(pal));
	for (int e = 0; e < SKP_NE; e++) { float *R = &pal[e * 12]; R[0] = 1; R[3] = (float)e; R[5] = 1; R[10] = 1; }
	glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, wt);
	#define SKP_ATTRIBS() do { glBindBuffer(GL_ARRAY_BUFFER, vbo); for (int a = 0; a < 5; a++) glEnableVertexAttribArray(a); \
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0); glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12); \
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16); glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24); \
		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28); } while (0)
	SKP_ATTRIBS();
	int all_ok = 1; char marks[SKP_NE + 1], wrong[256];
	int ctl0 = 0, ctries = 0;   // (Vita3K, b41: the first read-back of a run comes back empty; up to 3 tries, logged)
	while (ctries < 3 && (ctl0 = skp_control()) != SKP_W * SKP_H) ctries++;
	log("skin probe: readback control (white clear) at start: %d of %d pixels%s, %d empty or short read-backs before it\n", ctl0, SKP_W * SKP_H,
		ctl0 == SKP_W * SKP_H ? " (readback works)" : "  READBACK BROKEN: the lines below say nothing about the palette", ctries);
	if (ctl0 != SKP_W * SKP_H) all_ok = 0;
	// 1. md_prog (4 rows per entry): per window, one entry per draw
	static const int PB[5] = { 0, 6, 12, 24, 36 };
	skp_uniforms(md_prog, M);
	for (int t = 0; t < 5; t++) {
		int pb = PB[t], n = SKP_NE - pb, nok = 0, firstbad = -1, wl = 0; char wr[256]; wr[0] = 0;
		md_upload_window(md_u_pal, pal, pb, SKP_NE);
		for (int k = 0; k < n; k++) { char w1[32]; nok += skp_read(k, 1, pb, &marks[k], w1, sizeof(w1)); if (marks[k] != '.' && firstbad < 0) firstbad = k; if (w1[0] && wl < (int)sizeof(wr) - 24) wl += snprintf(wr + wl, sizeof(wr) - wl, "%s", w1); }
		marks[n] = 0; if (nok != n) all_ok = 0;
		log("skin probe: 4 rows per entry, window from entry %2d (%2d entries, %3d rows uploaded): %d ok, first bad %d  [%s]%s%s\n", pb, n, n * 4, nok, firstbad, marks, wl ? " read instead:" : "", wr);
	}
	// ... and all 48 in one draw
	md_upload_window(md_u_pal, pal, 0, SKP_NE);
	{ int nok = skp_read(0, SKP_NE, 0, marks, wrong, sizeof(wrong)); marks[SKP_NE] = 0; if (nok != SKP_NE) all_ok = 0;
	  log("skin probe: 4 rows per entry, all %d entries in one draw: %d right  [%s]%s\n", SKP_NE, nok, marks, wrong); }
	// 2. comparison layouts, all 48 in one draw: S rows per entry from row O (b40: 3 / 0)
	{ static const int L[5][2] = { { 3, 0 }, { 3, 1 }, { 4, 1 }, { 4, 2 }, { 2, 0 } };
	  static float rows[(4 * 48 + 8) * 4];
	  for (int v = 0; v < 5; v++) {
		int S = L[v][0], O = L[v][1]; GLuint pv = skp_prog(skp_vs(S, O), SKP_FS, 1, NULL);
		if (!pv) { log("skin probe: layout %d rows per entry from row %d: program not built\n", S, O); continue; }
		memset(rows, 0, sizeof(rows));
		for (int e = 0; e < SKP_NE; e++) for (int r = 0; r < 3; r++) memcpy(rows + ((S * e + O + r) * 4), pal + e * 12 + r * 4, 16);
		skp_uniforms(pv, M); glUniform4fv(glGetUniformLocation(pv, "pal"), S * SKP_NE + O + 3, rows); SKP_ATTRIBS();
		int nok = skp_read(0, SKP_NE, 0, marks, wrong, sizeof(wrong)); marks[SKP_NE] = 0;
		log("skin probe: layout %d rows per entry from row %d (entry e read from pal + %d e + %2d bytes), all %d in one draw: %2d right  [%s]%s%s\n",
			S, O, 16 * S, 16 * O, SKP_NE, nok, marks, wrong, S == 3 && O == 0 ? "  (b40's layout)" : "");
		glDeleteProgram(pv);
	  } }
	// 3. md_prog after shifting the uniform pool by 0..7 small uploads
	{ GLuint svs = 0, sp = skp_prog(SKP_SVS, SKP_SFS, 0, &svs); unsigned sd = skp_dub(svs), md = skp_dub(md_vs);
	  if (!sp) log("skin probe: pool shifter not built: phases not tested\n");
	  else {
		GLint us = glGetUniformLocation(sp, "sft"); char ph[9]; int nph = 0;
		for (int t = 0; t < 8; t++) {
			glUseProgram(sp); glBindBuffer(GL_ARRAY_BUFFER, svbo); for (int a = 1; a < 5; a++) glDisableVertexAttribArray(a); glEnableVertexAttribArray(0);
			glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, (void *)0);
			for (int k = 0; k < t; k++) { float s[2] = { 10.f + k, 10.f }; glUniform2fv(us, 1, s); glDrawArrays(GL_TRIANGLES, 0, 3); }   // (a dot outside the target: only its uniforms matter)
			skp_uniforms(md_prog, M); md_upload_window(md_u_pal, pal, 0, SKP_NE); SKP_ATTRIBS();
			int nok = skp_read(0, SKP_NE, 0, marks, NULL, 0); marks[SKP_NE] = 0;
			ph[t] = nok == SKP_NE ? '.' : 'x'; nph += nok == SKP_NE; if (nok != SKP_NE) all_ok = 0;
			if (nok != SKP_NE) log("skin probe: 4 rows per entry after %d pool shifts: %d right  [%s]\n", t, nok, marks);
		}
		ph[8] = 0;
		log("skin probe: 4 rows per entry, one draw after 0..7 uniform-pool shifts of %u bytes (model vertex buffer %u bytes): %d of 8 phases right  [%s]\n", sd, md, nph, ph);
		glDeleteProgram(sp);
	  } }
	{ int ctl1 = skp_control(); if (ctl1 != SKP_W * SKP_H) all_ok = 0;
	  log("skin probe: readback control (white clear) at end: %d of %d pixels%s\n", ctl1, SKP_W * SKP_H, ctl1 == SKP_W * SKP_H ? "" : "  READBACK BROKEN"); }
	skin_probe_ok = all_ok;
	log("skin probe: %s\n", all_ok ? "the GPU palette path reads every entry (4 rows per entry)" : "the GPU palette path is NOT reliable: CPU skinning everywhere");
	#undef SKP_ATTRIBS
	for (int a = 0; a < 5; a++) glDisableVertexAttribArray(a);
	glUseProgram(md_prog); md_cur_r = 0;
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glDeleteFramebuffers(1, &fb); glDeleteTextures(1, &ct); glDeleteTextures(1, &wt); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &svbo);
	glViewport(0, 0, 960, 544); glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
}
#else
static void skinprobe_run(void (*log)(const char *, ...)) { (void)log; }
#endif
