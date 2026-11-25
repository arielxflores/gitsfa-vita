// Skinned, animated models (GVM1 files built by vm/gvmlib.py): the first-person arms + weapon
// (bone poses in camera space) and third-person characters (bone poses in character space:
// z up, centimetres, the character faces -y). Several animation states can share one model.
#pragma once
#include <vitaGL.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "grade.h"

typedef struct { int tex, blend, nv, ni; GLuint vbo, ibo; unsigned char *cpu; int rigid; GLuint rvbo; } MMesh;
// cpu: vertex copy (kept for CPU skinning); rigid: palette entry of a mesh on a single bone (-1 otherwise), rvbo: its vertices re-pointed at entry 0
typedef struct { char name[16]; int nf, loop; float fps; float *data; } MClip;   // data: nf * nb * 7 (qx qy qz qw px py pz)
typedef struct { float q[4], p[3]; } MBone;
typedef struct {
	int ntex, nb, npal, nmesh, nclip, natt, ok;
	GLuint *tex; MMesh *mesh; MClip *clip; int *pal_slot; float *pal_s2b; char (*bone_name)[32];
	float att[8][3]; char att_name[8][16];
	float *upper;   // per bone: 1 = upper body (spine and above, arms, weapon), 0 = pelvis and legs
} Model;

static int model_find_clip(const Model *m, const char *n) { for (int i = 0; i < m->nclip; i++) if (!strcmp(m->clip[i].name, n)) return i; return -1; }
static int model_find_bone(const Model *m, const char *n) { for (int i = 0; i < m->nb; i++) if (!strcmp(m->bone_name[i], n)) return i; return -1; }
static const float *model_attach(const Model *m, const char *n) { for (int i = 0; i < m->natt; i++) if (!strcmp(m->att_name[i], n)) return m->att[i]; return NULL; }
static float model_clip_len(const Model *m, int ci) { return ci < 0 ? 0 : (m->clip[ci].nf - 1) / m->clip[ci].fps; }

static int model_keep_cpu;   // set before model_load to keep a CPU copy of the vertices (diagnostics / software skinning)
static int model_load(Model *M, const char *path) {
	memset(M, 0, sizeof(*M));
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned h[5]; fread(mg, 4, 1, f); fread(h, 4, 5, f);
	if (memcmp(mg, "GVM1", 4)) { fclose(f); return -2; }
	M->ntex = h[0]; M->nb = h[1]; M->npal = h[2]; M->nmesh = h[3]; M->nclip = h[4];
	if (M->npal > 48) { fclose(f); return -3; }   // shader palette holds 48 matrices
	M->tex = calloc(M->ntex, sizeof(GLuint)); glGenTextures(M->ntex, M->tex);
	void *buf = malloc(2 << 20);
	for (int i = 0; i < M->ntex; i++) {
		unsigned short w, hh; unsigned char fmt, nmp; fread(&w, 2, 1, f); fread(&hh, 2, 1, f); fread(&fmt, 1, 1, f); fread(&nmp, 1, 1, f);
		glBindTexture(GL_TEXTURE_2D, M->tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmp > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		for (int m = 0; m < nmp; m++) { unsigned sz; fread(&sz, 4, 1, f); fread(buf, sz, 1, f);
			glCompressedTexImage2D(GL_TEXTURE_2D, m, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w >> m, hh >> m, 0, sz, buf); sceGxmTransferFinish(); }
	}
	free(buf);
	M->bone_name = calloc(M->nb, 32); fread(M->bone_name, 32, M->nb, f);
	M->pal_slot = calloc(M->npal, sizeof(int)); M->pal_s2b = calloc(M->npal * 12, sizeof(float));
	for (int i = 0; i < M->npal; i++) { fread(&M->pal_slot[i], 4, 1, f); fread(&M->pal_s2b[i * 12], 4, 12, f); }
	M->mesh = calloc(M->nmesh, sizeof(MMesh));
	for (int i = 0; i < M->nmesh; i++) {
		MMesh *m = &M->mesh[i]; fread(&m->tex, 4, 1, f); fread(&m->blend, 4, 1, f); fread(&m->nv, 4, 1, f); fread(&m->ni, 4, 1, f);
		void *v = malloc(m->nv * 32), *ix = malloc(m->ni * 2); fread(v, 32, m->nv, f); fread(ix, 2, m->ni, f); if (m->ni & 1) { short pad; fread(&pad, 2, 1, f); }
		glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, m->nv * 32, v, GL_STATIC_DRAW);
		glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->ni * 2, ix, GL_STATIC_DRAW);
		if (model_keep_cpu) m->cpu = v; else free(v);
		free(ix);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	M->clip = calloc(M->nclip, sizeof(MClip));
	for (int i = 0; i < M->nclip; i++) {
		MClip *c = &M->clip[i]; fread(c->name, 16, 1, f); fread(&c->nf, 4, 1, f); fread(&c->loop, 4, 1, f); fread(&c->fps, 4, 1, f);
		c->data = malloc(c->nf * M->nb * 7 * 4); fread(c->data, 4, c->nf * M->nb * 7, f);
	}
	unsigned na = 0; if (fread(&na, 4, 1, f) == 1) for (unsigned i = 0; i < na && i < 8; i++) { fread(M->att_name[i], 16, 1, f); fread(M->att[i], 4, 3, f); M->natt++; }
	fclose(f);
	M->upper = calloc(M->nb, sizeof(float));
	for (int b = 0; b < M->nb; b++) {
		const char *n = M->bone_name[b];
		int lower = strstr(n, "Pelvis") || strstr(n, "Thigh") || strstr(n, "Calf") || strstr(n, "Foot") || strstr(n, "Toe") || !strcmp(n, "Bip01") || strstr(n, "Footsteps");
		M->upper[b] = lower ? 0.f : 1.f;
	}
	M->ok = 1; return 0;
}

// frees everything model_load allocated (GL textures and buffers, clips); the struct is zeroed
static void model_free(Model *M) {
	if (!M->ok) return;
	if (M->tex) { glDeleteTextures(M->ntex, M->tex); free(M->tex); }
	for (int i = 0; M->mesh && i < M->nmesh; i++) { MMesh *m = &M->mesh[i]; glDeleteBuffers(1, &m->vbo); glDeleteBuffers(1, &m->ibo); if (m->rvbo) glDeleteBuffers(1, &m->rvbo); free(m->cpu); }
	free(M->mesh);
	for (int i = 0; M->clip && i < M->nclip; i++) free(M->clip[i].data);
	free(M->clip); free(M->bone_name); free(M->pal_slot); free(M->pal_s2b); free(M->upper);
	memset(M, 0, sizeof(*M));
}

// ---- shader (shared by all models). Fog matches the map shader; fogc.a = 0 turns it off.
static const char *MD_VS =
"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[144], uniform float4 ldir, uniform float4 fogp,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float out sh : TEXCOORD1, float out fog : TEXCOORD2) {"
" float4 p = float4(pos, 1); float3 n = nrm.xyz;"
" int i0 = (int)(bi.x * 3.0 + 0.5), i1 = (int)(bi.y * 3.0 + 0.5), i2 = (int)(bi.z * 3.0 + 0.5), i3 = (int)(bi.w * 3.0 + 0.5);"
" float3 s = bw.x * float3(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p))"
"          + bw.y * float3(dot(pal[i1], p), dot(pal[i1 + 1], p), dot(pal[i1 + 2], p))"
"          + bw.z * float3(dot(pal[i2], p), dot(pal[i2 + 1], p), dot(pal[i2 + 2], p))"
"          + bw.w * float3(dot(pal[i3], p), dot(pal[i3 + 1], p), dot(pal[i3 + 2], p));"
" float3 sn = bw.x * float3(dot(pal[i0].xyz, n), dot(pal[i0 + 1].xyz, n), dot(pal[i0 + 2].xyz, n))"
"           + bw.y * float3(dot(pal[i1].xyz, n), dot(pal[i1 + 1].xyz, n), dot(pal[i1 + 2].xyz, n));"
" float4 c = float4(s, 1); vpos = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c)); t0 = uv;"
" sh = 0.40 + 0.75 * saturate(dot(normalize(sn + float3(0, 0, 0.0001)), ldir.xyz)); fog = fogp.z * (1.0 - exp(-max(vpos.w - fogp.x, 0.0) * fogp.y)); }";
static const char *MD_FS_PLAIN =
"float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc) : COLOR {"
" float4 c = tex2D(dif, t0); return float4(lerp(c.rgb * sh * tint.rgb, fogc.rgb, fog * fogc.a), c.a * tint.a); }";
// graded version: built at init with grade.h's way of grading (grade_src)
#define MD_FS_PARAMS "float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, float fog : TEXCOORD2, uniform sampler2D dif : TEXUNIT0, uniform float4 tint, uniform float4 fogc"
#define MD_FS_BODY " float4 c = tex2D(dif, t0); float3 fc = lerp(c.rgb * sh * tint.rgb, fogc.rgb, fog * fogc.a);"
#define MD_FS_RET " return float4(gc, c.a * tint.a); }"
static int md_grade = GRADE_OFF;   // the grading the model program uses

static GLuint md_prog; static GLint md_u_m[4], md_u_pal, md_u_ldir, md_u_tint, md_u_fog, md_u_fogp; static float md_fogp[4] = { 1000, 0.0001f, 0.3f, 0 }; static int md_shadow;   // shadow pass: skip additive meshes, leave the blend state alone
static int md_mesh0 = 0, md_mesh1 = 1 << 30;   // only meshes md_mesh0 <= i < md_mesh1 are drawn (first-person grenade: hide it after the throw)
typedef GLuint (*mkshader_fn)(GLenum, const char *);
static int model_shader(mkshader_fn mk, void (*log)(const char *, ...)) {
	GLuint vs = mk(GL_VERTEX_SHADER, MD_VS), fs = 0;
	GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1;
	static char src[1600]; int gm;
	for (gm = grade_mode; gm < GRADE_NMODES; gm++) {   // the map's grading, else the next ways down to none (MD_FS_PLAIN)
		if (gm == GRADE_OFF) fs = mk(GL_FRAGMENT_SHADER, MD_FS_PLAIN);
		else if (grade_src(src, sizeof(src), MD_FS_PARAMS, MD_FS_BODY, MD_FS_RET, gm) >= 0) fs = mk(GL_FRAGMENT_SHADER, src);
		else continue;
		glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (ok) break;
		log("model shader: grading %s failed\n", grade_mode_name[gm]);
	}
	if (!ok) return -1;
	md_prog = glCreateProgram(); glAttachShader(md_prog, vs); glAttachShader(md_prog, fs);
	glBindAttribLocation(md_prog, 0, "pos"); glBindAttribLocation(md_prog, 1, "nrm"); glBindAttribLocation(md_prog, 2, "uv");
	glBindAttribLocation(md_prog, 3, "bi"); glBindAttribLocation(md_prog, 4, "bw");
	glLinkProgram(md_prog); glGetProgramiv(md_prog, GL_LINK_STATUS, &ok); if (!ok) return -2;
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; md_u_m[r] = glGetUniformLocation(md_prog, nm); }
	md_u_pal = glGetUniformLocation(md_prog, "pal"); md_u_ldir = glGetUniformLocation(md_prog, "ldir"); md_u_tint = glGetUniformLocation(md_prog, "tint"); md_u_fog = glGetUniformLocation(md_prog, "fogc"); md_u_fogp = glGetUniformLocation(md_prog, "fogp");
	glUseProgram(md_prog); glUniform1i(glGetUniformLocation(md_prog, "dif"), 0);
	md_grade = gm; grade_register(md_prog, gm);
	log("model shader: pal %d ldir %d tint %d fog %d, colour grading: %s\n", md_u_pal, md_u_ldir, md_u_tint, md_u_fog, grade_mode_name[gm]);
	return md_u_pal == -1 ? -3 : 0;
}

// ---- poses
static void model_sample(const Model *M, int ci, float t, MBone *out) {
	const MClip *c = &M->clip[ci]; float fr = t * c->fps; int n = c->nf;
	if (c->loop) { float len = (float)(n - 1); fr = fmodf(fr, len); if (fr < 0) fr += len; }
	else { if (fr < 0) fr = 0; if (fr > n - 1) fr = (float)(n - 1); }
	int i0 = (int)fr; if (i0 >= n - 1) i0 = n - 2 < 0 ? 0 : n - 2; int i1 = i0 + 1 < n ? i0 + 1 : i0; float a = fr - i0; if (a > 1) a = 1;
	const float *A = c->data + (size_t)i0 * M->nb * 7, *B = c->data + (size_t)i1 * M->nb * 7;
	for (int b = 0; b < M->nb; b++, A += 7, B += 7) {
		float q[4], l = 0, d = A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3], sb = d < 0 ? -a : a;
		for (int k = 0; k < 4; k++) { q[k] = A[k] * (1 - a) + B[k] * sb; l += q[k] * q[k]; }
		l = 1.0f / sqrtf(l > 1e-12f ? l : 1); for (int k = 0; k < 4; k++) out[b].q[k] = q[k] * l;
		for (int k = 0; k < 3; k++) out[b].p[k] = A[4 + k] + (B[4 + k] - A[4 + k]) * a;
	}
}
// dst = lerp(dst, src, w * mask[b])
static void model_blend(int nb, MBone *dst, const MBone *src, float w, const float *mask) {
	if (w <= 0) return; if (w >= 1 && !mask) { memcpy(dst, src, nb * sizeof(MBone)); return; }
	for (int b = 0; b < nb; b++) {
		float wb = mask ? w * mask[b] : w; if (wb <= 0) continue;
		float d = 0; for (int k = 0; k < 4; k++) d += dst[b].q[k] * src[b].q[k];
		float s = d < 0 ? -wb : wb, l = 0;
		for (int k = 0; k < 4; k++) { dst[b].q[k] = dst[b].q[k] * (1 - wb) + src[b].q[k] * s; l += dst[b].q[k] * dst[b].q[k]; }
		l = 1.0f / sqrtf(l); for (int k = 0; k < 4; k++) dst[b].q[k] *= l;
		for (int k = 0; k < 3; k++) dst[b].p[k] += (src[b].p[k] - dst[b].p[k]) * wb;
	}
}
static void quat_rotate(const float *q, const float *v, float *o) {
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	for (int r = 0; r < 3; r++) o[r] = R[r * 3] * v[0] + R[r * 3 + 1] * v[1] + R[r * 3 + 2] * v[2];
}
// a point given in a bone's own space -> model space
static void model_bone_xform(const MBone *B, const float *v, float *out) { quat_rotate(B->q, v, out); for (int r = 0; r < 3; r++) out[r] += B->p[r]; }

// ---- animation state: a looping base layer with crossfades + a one-shot action layer (optionally upper body only)
typedef struct {
	const Model *m; MBone *tmp[3];
	int base, base_prev, base_frozen; float base_t, base_prev_t, base_fade, base_fade_len, rate;
	int act; float act_t, act_in, act_out, act_w; int act_upper;
} Anim;
static void anim_init(Anim *a, const Model *m) {
	memset(a, 0, sizeof(*a)); a->m = m; a->base = a->base_prev = a->act = -1; a->base_fade = 1; a->base_fade_len = 0.2f; a->rate = 1;
	for (int i = 0; i < 3; i++) a->tmp[i] = calloc(m->nb, sizeof(MBone));
}
static void anim_free(Anim *a) { for (int i = 0; i < 3; i++) { free(a->tmp[i]); a->tmp[i] = NULL; } a->m = NULL; }
static void anim_set_base(Anim *a, int ci, float fade, int frozen) {
	if (ci < 0 || (ci == a->base && frozen == a->base_frozen)) return;
	a->base_prev = a->base; a->base_prev_t = a->base_t; a->base = ci; a->base_t = 0; a->base_fade = a->base_prev < 0 ? 1 : 0; a->base_fade_len = fade; a->base_frozen = frozen;
}
static void anim_play(Anim *a, int ci, float fin, float fout, int upper) {
	if (ci < 0) return;
	if (ci == a->act && a->act_w > 0.5f) { a->act_t = 0; a->act_out = fout; return; }  // restart (automatic fire)
	a->act = ci; a->act_t = 0; a->act_in = fin; a->act_out = fout; a->act_upper = upper; if (a->act_w > 1) a->act_w = 1;
}
static void anim_stop(Anim *a) { a->act = -1; a->act_w = 0; }
static int anim_busy(const Anim *a) { return a->act >= 0 && a->act_t < model_clip_len(a->m, a->act) - a->act_out; }
static void anim_update(Anim *a, float dt) {
	if (!a->base_frozen) a->base_t += dt * a->rate; a->base_prev_t += dt * a->rate;
	if (a->base_fade < 1) { a->base_fade += dt / (a->base_fade_len > 0.01f ? a->base_fade_len : 0.01f); if (a->base_fade > 1) a->base_fade = 1; }
	if (a->act >= 0) {
		a->act_t += dt; float len = model_clip_len(a->m, a->act);
		float target = a->act_t > len - a->act_out ? (len - a->act_t) / (a->act_out > 0.01f ? a->act_out : 0.01f) : 1;
		if (target < 0) target = 0;
		if (a->act_w < target) { a->act_w += dt / (a->act_in > 0.01f ? a->act_in : 0.01f); if (a->act_w > target) a->act_w = target; } else a->act_w = target;
		if (a->act_t >= len) { a->act = -1; a->act_w = 0; }
	}
}
// root: bone whose position anchors an upper-body action on top of the base pose (character pelvis), -1 for none
static MBone *anim_pose(Anim *a, int root) {
	const Model *M = a->m; MBone *P = a->tmp[0];
	model_sample(M, a->base, a->base_t, P);
	if (a->base_fade < 1 && a->base_prev >= 0) { model_sample(M, a->base_prev, a->base_prev_t, a->tmp[1]); model_blend(M->nb, a->tmp[1], P, a->base_fade * a->base_fade * (3 - 2 * a->base_fade), NULL); memcpy(P, a->tmp[1], M->nb * sizeof(MBone)); }
	if (a->act >= 0 && a->act_w > 0) {
		MBone *S = a->tmp[2]; model_sample(M, a->act, a->act_t, S);
		if (a->act_upper && root >= 0) { // keep the upper body on top of the base pelvis
			float d[3]; for (int k = 0; k < 3; k++) d[k] = P[root].p[k] - S[root].p[k];
			for (int b = 0; b < M->nb; b++) if (M->upper[b] > 0) for (int k = 0; k < 3; k++) S[b].p[k] += d[k];
		}
		model_blend(M->nb, P, S, a->act_w, a->act_upper ? M->upper : NULL);
	}
	return P;
}
// tilt the upper body about the model x axis through a pivot bone (aim up/down; + = up for a character facing -y)
static void pose_aim_pitch(const Model *M, MBone *P, int pivot, float pitch) {
	if (pivot < 0 || fabsf(pitch) < 1e-3f) return;
	float h = -pitch * 0.5f, r[4] = { sinf(h), 0, 0, cosf(h) }, c[3] = { P[pivot].p[0], P[pivot].p[1], P[pivot].p[2] };
	for (int b = 0; b < M->nb; b++) {
		if (M->upper[b] <= 0) continue;
		float v[3] = { P[b].p[0] - c[0], P[b].p[1] - c[1], P[b].p[2] - c[2] }, o[3]; quat_rotate(r, v, o);
		for (int k = 0; k < 3; k++) P[b].p[k] = c[k] + o[k];
		float *q = P[b].q, x = q[0], y = q[1], z = q[2], w = q[3];  // q = r * q
		q[0] = r[3] * x + r[0] * w + r[1] * z - r[2] * y; q[1] = r[3] * y - r[0] * z + r[1] * w + r[2] * x;
		q[2] = r[3] * z + r[0] * y - r[1] * x + r[2] * w; q[3] = r[3] * w - r[0] * x - r[1] * y - r[2] * z;
	}
}

// ---- draw. mvp: column-major model->clip; ldir: light direction in model space; fogc.a = fog amount (0 = none)
static float md_palbuf[48 * 12];
static void model_palette(const Model *M, const MBone *P, float *out) {
	for (int e = 0; e < M->npal; e++) {
		const MBone *B = &P[M->pal_slot[e]];
		float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
		float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		const float *S = &M->pal_s2b[e * 12]; float *o = &out[e * 12];
		for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++)
			o[r * 4 + k] = R[r * 3] * S[k] + R[r * 3 + 1] * S[4 + k] + R[r * 3 + 2] * S[8 + k] + (k == 3 ? B->p[r] : 0);
	}
}
// software skinning of one mesh (same maths as the shader) into dst (32-byte vertices, palette index 0, weight 1)
static void model_skin_cpu(const Model *M, int mi, const float *pal, unsigned char *dst) {
	const MMesh *m = &M->mesh[mi]; if (!m->cpu) return;
	for (int i = 0; i < m->nv; i++) {
		const unsigned char *v = m->cpu + i * 32; unsigned char *o = dst + i * 32; float p[3], s[3] = { 0, 0, 0 };
		memcpy(p, v, 12); memcpy(o, v, 32);
		for (int j = 0; j < 4; j++) { float w = v[28 + j] / 255.f; if (w <= 0) continue; const float *R = &pal[v[24 + j] * 12];
			for (int r = 0; r < 3; r++) s[r] += w * (R[r * 4] * p[0] + R[r * 4 + 1] * p[1] + R[r * 4 + 2] * p[2] + R[r * 4 + 3]); }
		memcpy(o, s, 12); o[24] = o[25] = o[26] = o[27] = 0; o[28] = 255; o[29] = o[30] = o[31] = 0;
	}
}
static void model_draw_pal(const Model *M, const float *mvp, const float *pal, int npal, const float *ldir, const float *tint, const float *fogc, const GLuint *vbos);
static void model_draw(const Model *M, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc) {
	model_palette(M, P, md_palbuf);
	model_draw_pal(M, mvp, md_palbuf, M->npal, ldir, tint, fogc, NULL);
}
// draw with a ready palette; vbos (optional) replace the meshes' vertex buffers
static void model_draw_pal(const Model *M, const float *mvp, const float *pal, int npal, const float *ldir, const float *tint, const float *fogc, const GLuint *vbos) {
	glUseProgram(md_prog);
	for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(md_u_m[r], 1, row); }
	glUniform4fv(md_u_pal, npal * 3, pal);
	glUniform4fv(md_u_ldir, 1, ldir); glUniform4fv(md_u_tint, 1, tint); glUniform4fv(md_u_fog, 1, fogc); if (md_u_fogp >= 0) glUniform4fv(md_u_fogp, 1, md_fogp);
	glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh; i++) {
		const MMesh *m = &M->mesh[i];
		if ((m->blend == 2) != pass || (md_shadow && m->blend == 2) || i < md_mesh0 || i >= md_mesh1) continue;
		if (m->blend == 2) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
		glBindBuffer(GL_ARRAY_BUFFER, vbos ? vbos[i] : m->vbo);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
		glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16);
		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
		if (m->blend == 2) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// ---- CPU skinning (used for the third-person characters). On the Vita the shader path draws them wrong
// (spikes; the first-person arms with the same shader are fine), while the same vertices skinned here and drawn
// with a one-entry palette come out right (model lab, build 19). Each instance has its own vertex buffers, three
// sets in rotation so the GPU never reads a buffer that is being rewritten.
typedef struct { GLuint vbo[3][8]; int cur, ok; } SkinCache;
static unsigned char *skin_scratch; static int skin_scratch_n, skin_all_cpu;
// meshes on a single bone are drawn by the GPU as rigid pieces with a one-entry palette (that path works)
static void model_prepare_rigid(Model *M) {
	for (int i = 0; i < M->nmesh; i++) {
		MMesh *m = &M->mesh[i]; m->rigid = -1; if (!m->cpu || skin_all_cpu) continue;
		int e = m->cpu[24], ok = 1;
		for (int v = 0; v < m->nv && ok; v++) { const unsigned char *q = m->cpu + v * 32; if (q[24] != e || q[28] < 250) ok = 0; }
		if (!ok) continue;
		unsigned char *buf = malloc(m->nv * 32); memcpy(buf, m->cpu, m->nv * 32);
		for (int v = 0; v < m->nv; v++) { unsigned char *q = buf + v * 32; q[24] = q[25] = q[26] = q[27] = 0; q[28] = 255; q[29] = q[30] = q[31] = 0; }
		glGenBuffers(1, &m->rvbo); glBindBuffer(GL_ARRAY_BUFFER, m->rvbo); glBufferData(GL_ARRAY_BUFFER, m->nv * 32, buf, GL_STATIC_DRAW); free(buf); m->rigid = e;
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
static void skincache_init(SkinCache *c, const Model *M) {
	memset(c, 0, sizeof(*c));
	for (int k = 0; k < 3; k++) for (int i = 0; i < M->nmesh && i < 8; i++) {
		const MMesh *m = &M->mesh[i]; if (!m->cpu || m->rigid >= 0) continue;
		glGenBuffers(1, &c->vbo[k][i]); glBindBuffer(GL_ARRAY_BUFFER, c->vbo[k][i]); glBufferData(GL_ARRAY_BUFFER, m->nv * 32, m->cpu, GL_DYNAMIC_DRAW);
		if (m->nv > skin_scratch_n) { skin_scratch_n = m->nv; skin_scratch = realloc(skin_scratch, m->nv * 32); }
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0); c->ok = 1; c->cur = -1;
}
static void skincache_free(SkinCache *c) {
	for (int k = 0; k < 3; k++) for (int i = 0; i < 8; i++) if (c->vbo[k][i]) glDeleteBuffers(1, &c->vbo[k][i]);
	memset(c, 0, sizeof(*c));
}
// positions with all weights, normals with the first bone; output vertices point at palette entry 0 (identity)
static void skin_mesh(const MMesh *m, const float *pal, unsigned char *dst) {
	for (int i = 0; i < m->nv; i++) {
		const unsigned char *v = m->cpu + i * 32; unsigned char *o = dst + i * 32;
		const float *p = (const float *)v; float px = p[0], py = p[1], pz = p[2], x, y, z;
		const float *R = &pal[v[24] * 12];
		if (v[29] == 0) { x = R[0] * px + R[1] * py + R[2] * pz + R[3]; y = R[4] * px + R[5] * py + R[6] * pz + R[7]; z = R[8] * px + R[9] * py + R[10] * pz + R[11]; }
		else {
			x = y = z = 0;
			for (int j = 0; j < 4; j++) { float w = v[28 + j] * (1.0f / 255); if (w <= 0) continue; const float *Q = &pal[v[24 + j] * 12];
				x += w * (Q[0] * px + Q[1] * py + Q[2] * pz + Q[3]); y += w * (Q[4] * px + Q[5] * py + Q[6] * pz + Q[7]); z += w * (Q[8] * px + Q[9] * py + Q[10] * pz + Q[11]); }
		}
		float nx = (signed char)v[12], ny = (signed char)v[13], nz = (signed char)v[14];
		signed char on[4] = { (signed char)(R[0] * nx + R[1] * ny + R[2] * nz), (signed char)(R[4] * nx + R[5] * ny + R[6] * nz), (signed char)(R[8] * nx + R[9] * ny + R[10] * nz), 0 };
		float op[3] = { x, y, z }; memcpy(o, op, 12); memcpy(o + 12, on, 4); memcpy(o + 16, v + 16, 8);
		static const unsigned char b0[8] = { 0, 0, 0, 0, 255, 0, 0, 0 }; memcpy(o + 24, b0, 8);
	}
}
static const float md_identity[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
static int skin_verts;   // vertices skinned this frame (stats)
static void model_draw_cpu(const Model *M, SkinCache *c, const float *mvp, const MBone *P, const float *ldir, const float *tint, const float *fogc, int reskin) {
	float pal[48 * 12]; model_palette(M, P, pal);
	if (c->cur < 0) reskin = 1;
	if (reskin) c->cur = (c->cur + 1) % 3;
	GLuint vbos[8] = { 0 };
	for (int i = 0; i < M->nmesh && i < 8; i++) {
		const MMesh *m = &M->mesh[i]; if (!m->cpu || m->rigid >= 0) continue;
		vbos[i] = c->vbo[c->cur][i]; if (!reskin) continue;
		skin_mesh(m, pal, skin_scratch); skin_verts += m->nv;
		glBindBuffer(GL_ARRAY_BUFFER, vbos[i]);
		void *dst = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		if (dst) { memcpy(dst, skin_scratch, m->nv * 32); glUnmapBuffer(GL_ARRAY_BUFFER); }
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	// rigid pieces: their bone's matrix as the only palette entry
	glUseProgram(md_prog);
	for (int r = 0; r < 4; r++) { float row[4] = { mvp[r], mvp[4 + r], mvp[8 + r], mvp[12 + r] }; glUniform4fv(md_u_m[r], 1, row); }
	glUniform4fv(md_u_ldir, 1, ldir); glUniform4fv(md_u_tint, 1, tint); glUniform4fv(md_u_fog, 1, fogc); if (md_u_fogp >= 0) glUniform4fv(md_u_fogp, 1, md_fogp);
	glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < M->nmesh && i < 8; i++) {
		const MMesh *m = &M->mesh[i]; if ((m->blend == 2) != pass || (md_shadow && m->blend == 2)) continue;
		if (m->rigid >= 0) { glUniform4fv(md_u_pal, 3, &pal[m->rigid * 12]); glBindBuffer(GL_ARRAY_BUFFER, m->rvbo); }
		else if (vbos[i]) { glUniform4fv(md_u_pal, 3, md_identity); glBindBuffer(GL_ARRAY_BUFFER, vbos[i]); }
		else continue;
		if (m->blend == 2) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		glBindTexture(GL_TEXTURE_2D, M->tex[m->tex]);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
		glVertexAttribPointer(1, 4, GL_BYTE, GL_TRUE, 32, (void *)12);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)16);
		glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, 32, (void *)24);
		glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *)28);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glDrawElements(GL_TRIANGLES, m->ni, GL_UNSIGNED_SHORT, 0);
		if (m->blend == 2) { glDisable(GL_BLEND); glDepthMask(GL_TRUE); }
	}
	glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}
