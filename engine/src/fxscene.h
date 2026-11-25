// fxscene.h - b102 (task #154, the user: the match start's "animation and accompanying logo were a little rough ... include
// as many original elements / animations as possible"): keyed effect scenes of the game, played back as the PC plays them.
// A scene is one of the game's effect NIFs made of plain meshes under keyed nodes (no particles), baked by
// tools/fx/make_scene.py into a .gsc: every track sampled at 30 Hz - per mesh and frame a colour with alpha (0 = not drawn),
// and where they move a model matrix and a texture transform; the vertices carry an alpha of their own (the game's light
// streaks are a white base whose alpha fades the ends times a scrolling pattern: the tool gives the mesh the pattern and puts
// the base's alpha into the vertices). The format is in the tool's header.
//   fxs_init(mk)                      at start-up, with the other programs (every shader is compiled at start-up: the bank)
//   fxs_load(&s, path) / fxs_free(&s) a scene's textures, vertex buffers and tables (-1: no file / not a scene)
//   fxs_draw(&s, t, VP, fade)         the scene at t seconds. VP NULL: a screen scene through its own camera (the file's view
//                                     and lens); else a world scene, VP = projection x view x the scene's place.
//                                     Blended in the file's order (far first), no depth test or write.
//   fxs_camera(&s, t, eye, dir, up)   a world scene's camera track (scene space), 0 when it has none
#pragma once
typedef struct { unsigned v0, nv, i0, ni; short tex, tex2; unsigned char add, anim; unsigned short pad; } FxsMesh;   // (as in the file)
#define FXS_MAXM 128
#define FXS_MAXT 40
typedef struct {
	int ok, nf, fps, nm, nt, flags; float fov, view[12];
	GLuint tex[FXS_MAXT], vb[FXS_MAXM], ib[FXS_MAXM]; FxsMesh *m; unsigned char *tab; const unsigned char *col; const float *mat[FXS_MAXM], *uv0[FXS_MAXM], *cam;
} FxScene;
static const char *FXS_VS =   // (one texture a mesh: the tool folds the game's base x dark pairs - make_scene.py's note)
"void main(float3 pos, float2 uv, float va, uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3,"
" uniform float4 ua, uniform float4 ub, uniform float4 col,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float4 out c0 : COLOR) {"
" float4 p = float4(pos, 1); vpos = float4(dot(m0, p), dot(m1, p), dot(m2, p), dot(m3, p)); float3 q = float3(uv, 1);"
" t0 = float2(dot(ua.xyz, q), dot(ub.xyz, q)); c0 = float4(col.xyz, col.w * va); }";
static const char *FXS_FS =   // (the sprites' fragment program)
"float4 main(float2 t0 : TEXCOORD0, float4 c0 : COLOR, uniform sampler2D tex : TEXUNIT0) : COLOR { return tex2D(tex, t0) * c0; }";
enum { FXU_M0, FXU_M1, FXU_M2, FXU_M3, FXU_UA, FXU_UB, FXU_COL, FXU_N };
static GLuint fxs_prog, fxs_white; static GLint fxs_u[FXU_N]; static int fxs_ok;
static int fxs_dbg_lo, fxs_dbg_hi = FXS_MAXM, fxs_dbg_log;   // test (divedbg.txt, wroom.h): the screen scenes' meshes lo .. hi - 1 only
static int fxs_init(sp_mkshader_fn mk) {
	static const char *const UN[FXU_N] = { "m0", "m1", "m2", "m3", "ua", "ub", "col" };
	GLuint vs = mk(GL_VERTEX_SHADER, FXS_VS), fs = mk(GL_FRAGMENT_SHADER, FXS_FS); GLint ok = 0;
	glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1; glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -2;
	fxs_prog = glCreateProgram(); glAttachShader(fxs_prog, vs); glAttachShader(fxs_prog, fs);
	glBindAttribLocation(fxs_prog, 0, "pos"); glBindAttribLocation(fxs_prog, 1, "uv"); glBindAttribLocation(fxs_prog, 2, "va");
	glLinkProgram(fxs_prog); glGetProgramiv(fxs_prog, GL_LINK_STATUS, &ok); if (!ok) return -3;
	for (int u = 0; u < FXU_N; u++) fxs_u[u] = glGetUniformLocation(fxs_prog, UN[u]);
	glUseProgram(fxs_prog); glUniform1i(glGetUniformLocation(fxs_prog, "tex"), 0);
	static unsigned char px[4 * 4 * 4]; memset(px, 255, sizeof(px));
	glGenTextures(1, &fxs_white); glBindTexture(GL_TEXTURE_2D, fxs_white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); VGL_XFER_WAIT();
	fxs_ok = 1; return 0;
}
static void fxs_free(FxScene *s) {
	if (!s->ok) { memset(s, 0, sizeof(*s)); return; }
	for (int i = 0; i < s->nt; i++) if (s->tex[i]) glDeleteTextures(1, &s->tex[i]);
	for (int i = 0; i < s->nm && i < FXS_MAXM; i++) { int ov = 1, oi = 1;   // (a shared buffer is its first mesh's)
		for (int j = 0; j < i; j++) { if (s->vb[j] == s->vb[i]) ov = 0; if (s->ib[j] == s->ib[i]) oi = 0; }
		if (s->vb[i] && ov) glDeleteBuffers(1, &s->vb[i]); if (s->ib[i] && oi) glDeleteBuffers(1, &s->ib[i]); }
	free(s->m); free(s->tab); memset(s, 0, sizeof(*s));
}
static unsigned fxs_u32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static int fxs_load(FxScene *s, const char *path) {
	memset(s, 0, sizeof(*s)); if (!fxs_ok) return -1;
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	unsigned char h[76]; void *buf = NULL; unsigned nv, ni;
	if (fread(h, 1, 76, f) != 76 || memcmp(h, "GSC2", 4)) goto bad;
	s->nf = h[4] | h[5] << 8; s->fps = h[6] | h[7] << 8; s->nm = h[8] | h[9] << 8; s->nt = h[10] | h[11] << 8; nv = fxs_u32(h + 12); ni = fxs_u32(h + 16); s->flags = h[20] | h[21] << 8;
	memcpy(&s->fov, h + 24, 4); memcpy(s->view, h + 28, 48);
	if (s->nf < 1 || s->fps < 1 || s->nm < 1 || s->nm > FXS_MAXM || s->nt > FXS_MAXT || nv > 400000 || ni > 1200000) goto bad;
	for (int i = 0; i < s->nt; i++) {   // one level each, exactly its size (never let the driver read past it)
		unsigned char th[12]; if (fread(th, 1, 12, f) != 12) goto bad;
		int w = th[0] | th[1] << 8, hh = th[2] | th[3] << 8, fmt = th[4], wrap = th[5]; unsigned size = fxs_u32(th + 8);
		if (w < 4 || hh < 4 || w > 1024 || hh > 1024 || (fmt != 1 && fmt != 2) || size != (unsigned)(w / 4) * (hh / 4) * (fmt == 1 ? 8 : 16)) goto bad;
		buf = malloc(size); if (!buf || fread(buf, 1, size, f) != size) goto bad;
		glGenTextures(1, &s->tex[i]); glBindTexture(GL_TEXTURE_2D, s->tex[i]);
		glCompressedTexImage2D(GL_TEXTURE_2D, 0, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w, hh, 0, size, buf); VGL_XFER_WAIT();
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		free(buf); buf = NULL;
	}
	s->m = malloc(sizeof(FxsMesh) * s->nm); if (!s->m || fread(s->m, sizeof(FxsMesh), s->nm, f) != (size_t)s->nm) goto bad;
	for (int i = 0; i < s->nm; i++) { const FxsMesh *m = &s->m[i];
		if (m->nv > 65535 || m->v0 + m->nv > nv || m->i0 + m->ni > ni || m->tex >= s->nt || m->tex2 >= 0) goto bad; }
	// a vertex and an index buffer for every mesh, each drawn from its start (emu b102-6 .. 10: with one buffer for the scene and
	// the meshes drawn from offsets into it - the attribute pointers at v0 * 24 - single meshes came out as something else for
	// some frames: a ring as a bar across the picture)
	{ void *vb = malloc(nv * 24 + 4), *ib = malloc(ni * 2 + 4); int bad_ = !vb || !ib || fread(vb, 24, nv, f) != nv || fread(ib, 2, ni, f) != ni;
	  const unsigned short *ix = ib;
	  for (int i = 0; !bad_ && i < s->nm; i++) { const FxsMesh *m = &s->m[i]; for (unsigned k = 0; k < m->ni; k++) if (ix[m->i0 + k] >= m->nv) { bad_ = 1; break; } }
	  for (int i = 0; !bad_ && i < s->nm; i++) { const FxsMesh *m = &s->m[i]; if (!m->nv || !m->ni) continue;
		int jv = -1, ji = -1;   // (meshes of the same geometry - the dive's ten light fans are one grid - share its buffers)
		for (int j = 0; j < i; j++) { const FxsMesh *o = &s->m[j]; if (jv < 0 && s->vb[j] && o->v0 == m->v0 && o->nv == m->nv) jv = j; if (ji < 0 && s->ib[j] && o->i0 == m->i0 && o->ni == m->ni) ji = j; }
		if (jv >= 0) s->vb[i] = s->vb[jv];
		else { glGenBuffers(1, &s->vb[i]); glBindBuffer(GL_ARRAY_BUFFER, s->vb[i]); glBufferData(GL_ARRAY_BUFFER, m->nv * 24, (const char *)vb + (size_t)m->v0 * 24, GL_STATIC_DRAW); }
		if (ji >= 0) s->ib[i] = s->ib[ji];
		else { glGenBuffers(1, &s->ib[i]); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ib[i]); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->ni * 2, ix + m->i0, GL_STATIC_DRAW); } }
	  VGL_XFER_WAIT(); free(vb); free(ib); glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	  if (bad_) goto bad; }
	if (ni & 1) fseek(f, 2, SEEK_CUR);
	{ size_t n = (size_t)s->nf * s->nm * 4, o;
	  for (int i = 0; i < s->nm; i++) { const FxsMesh *m = &s->m[i]; n += (m->anim & 1 ? s->nf : 1) * 48 + (m->anim & 2 ? s->nf : 1) * 24; }
	  if (s->flags & 1) n += (size_t)s->nf * 36;
	  s->tab = malloc(n); if (!s->tab || fread(s->tab, 1, n, f) != n) goto bad;
	  s->col = s->tab; o = (size_t)s->nf * s->nm * 4;
	  for (int i = 0; i < s->nm; i++) { s->mat[i] = (const float *)(s->tab + o); o += (s->m[i].anim & 1 ? s->nf : 1) * 48; }
	  for (int i = 0; i < s->nm; i++) { s->uv0[i] = (const float *)(s->tab + o); o += (s->m[i].anim & 2 ? s->nf : 1) * 24; }
	  if (s->flags & 1) s->cam = (const float *)(s->tab + o); }
	fclose(f); s->ok = 1; return 0;
bad:
	fclose(f); free(buf); s->ok = 1; fxs_free(s); return -1;
}
static float fxs_len(const FxScene *s) { return s->ok ? (float)(s->nf - 1) / s->fps : 0; }
static void fxs_mul(float *o, const float *a, const float *b) {   // column-major 4 x 4
	for (int c = 0; c < 4; c++) for (int r = 0; r < 4; r++) o[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] + a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
}
static void fxs_rows(float *o, const float *a, const float *b, float k, int n) { for (int i = 0; i < n; i++) o[i] = a[i] + (b[i] - a[i]) * k; }
// two frames' matrix rows are one motion (to be drawn in between) unless an axis turns by more than ~45 degrees from one to the
// next: a matrix half-way between two such places would be the mesh squashed flat. (The baked scenes have such steps only
// in the skill holograms - 20 in sk_dev_saito; the dive has none.)
static int fxs_jump(const float *a, const float *b) {
	for (int c = 0; c < 3; c++) { float d = 0, la = 0, lb = 0;
		for (int r = 0; r < 3; r++) { float x = a[r * 4 + c], y = b[r * 4 + c]; d += x * y; la += x * x; lb += y * y; }
		if (la * lb > 0 && d < 0.7f * sqrtf(la * lb)) return 1; }
	return 0;
}
// (texture rows: a looping scroll's wrap back is a jump too)
static int fxs_uvjump(const float *a, const float *b) { return fabsf(a[2] - b[2]) > 0.75f || fabsf(a[5] - b[5]) > 0.75f; }   // (the offsets; a fan opening changes its scale by more than that a frame - one motion)
static void fxs_draw(const FxScene *s, float t, const float *VP, float fade) {
	if (!s->ok || !fxs_ok || fade <= 0) return;
	float ft = t * s->fps; if (ft < 0) ft = 0; int f0 = (int)ft; if (f0 >= s->nf) return; int f1 = f0 + 1 < s->nf ? f0 + 1 : f0; float k = ft - f0;
	float PV[16];
	if (!VP) {   // its own camera
		float P[16], V[16] = { s->view[0], s->view[4], s->view[8], 0, s->view[1], s->view[5], s->view[9], 0, s->view[2], s->view[6], s->view[10], 0, s->view[3], s->view[7], s->view[11], 1 };
		float th = 1.0f / tanf(s->fov * 0.5f * 3.14159265f / 180), n = 2, f = 60000; memset(P, 0, sizeof(P));
		P[0] = th / (960.0f / 544); P[5] = th; P[10] = (f + n) / (n - f); P[11] = -1; P[14] = 2 * f * n / (n - f); fxs_mul(PV, P, V);
	} else memcpy(PV, VP, sizeof(PV));
	glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glDisable(GL_CULL_FACE); glEnable(GL_BLEND);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1); glEnableVertexAttribArray(2);
	// (the blend state is changed for real once with the program bound before the first mode is asked for. This came from emu
	// b102-2, where a blended mesh drawn first came out added; that picture may have been the shared vertex buffer's doing
	// (fxs_load's note) - vitaGL ee07531 compares each program's blend setting with the current one at every draw. Kept: free.)
	glUseProgram(fxs_prog); glDisable(GL_BLEND); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ZERO); glActiveTexture(GL_TEXTURE0);
	int bl = -1; const GLint *U = fxs_u;
	for (int i = 0; i < s->nm; i++) {
		const FxsMesh *m = &s->m[i]; const unsigned char *c0 = s->col + ((size_t)f0 * s->nm + i) * 4, *c1 = s->col + ((size_t)f1 * s->nm + i) * 4;
		if (!c0[3] || !m->ni || (!VP && (i < fxs_dbg_lo || i >= fxs_dbg_hi))) continue;
		float col[4]; for (int q = 0; q < 4; q++) col[q] = (c0[q] + (c1[q] - c0[q]) * k) * (1 / 255.0f); col[3] *= fade; if (col[3] <= 0.002f) continue;
		float R[12], M[16], MVP[16]; if (m->anim & 1) { const float *a0 = s->mat[i] + f0 * 12, *a1 = s->mat[i] + f1 * 12; fxs_rows(R, a0, a1, fxs_jump(a0, a1) ? 0 : k, 12); } else memcpy(R, s->mat[i], sizeof(R));
		M[0] = R[0]; M[1] = R[4]; M[2] = R[8]; M[3] = 0; M[4] = R[1]; M[5] = R[5]; M[6] = R[9]; M[7] = 0; M[8] = R[2]; M[9] = R[6]; M[10] = R[10]; M[11] = 0; M[12] = R[3]; M[13] = R[7]; M[14] = R[11]; M[15] = 1;
		fxs_mul(MVP, PV, M);
		for (int r = 0; r < 4; r++) { float row[4] = { MVP[r], MVP[4 + r], MVP[8 + r], MVP[12 + r] }; glUniform4fv(U[FXU_M0 + r], 1, row); }
		float uv[6], ua[4], ub[4]; if (m->anim & 2) { const float *a0 = s->uv0[i] + f0 * 6, *a1 = s->uv0[i] + f1 * 6; fxs_rows(uv, a0, a1, fxs_uvjump(a0, a1) ? 0 : k, 6); } else memcpy(uv, s->uv0[i], sizeof(uv));
		ua[0] = uv[0]; ua[1] = uv[1]; ua[2] = uv[2]; ua[3] = 0; ub[0] = uv[3]; ub[1] = uv[4]; ub[2] = uv[5]; ub[3] = 0; glUniform4fv(U[FXU_UA], 1, ua); glUniform4fv(U[FXU_UB], 1, ub);
		glUniform4fv(U[FXU_COL], 1, col);
		glBindTexture(GL_TEXTURE_2D, m->tex >= 0 ? s->tex[m->tex] : fxs_white);
		if (bl != m->add) { bl = m->add; glBlendFunc(GL_SRC_ALPHA, m->add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA); }
		glBindBuffer(GL_ARRAY_BUFFER, s->vb[i]); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s->ib[i]);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (const void *)0); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 24, (const void *)12); glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 24, (const void *)20);
		glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, (const void *)0);
		if (fxs_dbg_log) { fxs_dbg_log = 0; logp("fxsdbg: mesh %d f %d k %.3f col %.2f %.2f %.2f %.2f add %d tex %d nv %u ni %u | R %.2f %.2f %.2f %.2f / %.2f %.2f %.2f %.2f / %.2f %.2f %.2f %.2f | uv %.2f %.2f %.2f / %.2f %.2f %.2f\n", i, f0, k, col[0], col[1], col[2], col[3], m->add, m->tex, m->nv, m->ni, R[0], R[1], R[2], R[3], R[4], R[5], R[6], R[7], R[8], R[9], R[10], R[11], uv[0], uv[1], uv[2], uv[3], uv[4], uv[5]); }
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST); glEnable(GL_CULL_FACE);
}
static int fxs_camera(const FxScene *s, float t, float *eye, float *dir, float *up) {
	if (!s->ok || !s->cam) return 0;
	float ft = t * s->fps; if (ft < 0) ft = 0; int f0 = (int)ft; if (f0 >= s->nf) f0 = s->nf - 1; int f1 = f0 + 1 < s->nf ? f0 + 1 : f0; float k = ft - f0, c[9];
	fxs_rows(c, s->cam + f0 * 9, s->cam + f1 * 9, k, 9); memcpy(eye, c, 12); memcpy(dir, c + 3, 12); memcpy(up, c + 6, 12); return 1;
}
