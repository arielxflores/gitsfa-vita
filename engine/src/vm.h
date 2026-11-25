// First-person viewmodel: skinned arms + weapon, animated with the game's own (decoded) morpheme clips.
// Data file: vm_*.gvm (built by vm/make_vm.py). Bone poses are stored in camera space
// (x right, y up, z back, centimetres), already relative to the eye.
#pragma once
#include <vitaGL.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct { int tex, blend, nv, ni; GLuint vbo, ibo; } VMMesh;
typedef struct { char name[16]; int nf, loop; float fps; float *data; } VMClip;   // data: nf * nb * 7 (qx qy qz qw px py pz)
typedef struct { float q[4], p[3]; } VMBone;

static int vm_ntex, vm_nb, vm_npal, vm_nmesh, vm_nclip, vm_ok;
static GLuint *vm_tex; static VMMesh *vm_mesh; static VMClip *vm_clip;
static int *vm_pal_slot; static float *vm_pal_s2b; // 12 floats (3x4 row-major) per palette entry
static char (*vm_bone_name)[32];
static float vm_att[8][3]; static char vm_att_name[8][16]; static int vm_natt;
static GLuint vm_prog; static GLint vm_u_m[4], vm_u_pal, vm_u_ldir, vm_u_tint;

static int vm_find_clip(const char *n) { for (int i = 0; i < vm_nclip; i++) if (!strcmp(vm_clip[i].name, n)) return i; return -1; }
static int vm_find_bone(const char *n) { for (int i = 0; i < vm_nb; i++) if (!strcmp(vm_bone_name[i], n)) return i; return -1; }
static const float *vm_attach(const char *n) { for (int i = 0; i < vm_natt; i++) if (!strcmp(vm_att_name[i], n)) return vm_att[i]; return NULL; }

static int vm_load(const char *path) {
	FILE *f = fopen(path, "rb"); if (!f) return -1;
	char mg[4]; unsigned h[5]; fread(mg, 4, 1, f); fread(h, 4, 5, f);
	if (memcmp(mg, "GVM1", 4)) { fclose(f); return -2; }
	vm_ntex = h[0]; vm_nb = h[1]; vm_npal = h[2]; vm_nmesh = h[3]; vm_nclip = h[4];
	vm_tex = calloc(vm_ntex, sizeof(GLuint)); glGenTextures(vm_ntex, vm_tex);
	void *buf = malloc(2 << 20);
	for (int i = 0; i < vm_ntex; i++) {
		unsigned short w, hh; unsigned char fmt, nmp; fread(&w, 2, 1, f); fread(&hh, 2, 1, f); fread(&fmt, 1, 1, f); fread(&nmp, 1, 1, f);
		glBindTexture(GL_TEXTURE_2D, vm_tex[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nmp > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		for (int m = 0; m < nmp; m++) { unsigned sz; fread(&sz, 4, 1, f); fread(buf, sz, 1, f);
			glCompressedTexImage2D(GL_TEXTURE_2D, m, fmt == 1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, w >> m, hh >> m, 0, sz, buf); }
	}
	free(buf);
	vm_bone_name = calloc(vm_nb, 32); fread(vm_bone_name, 32, vm_nb, f);
	vm_pal_slot = calloc(vm_npal, sizeof(int)); vm_pal_s2b = calloc(vm_npal * 12, sizeof(float));
	for (int i = 0; i < vm_npal; i++) { fread(&vm_pal_slot[i], 4, 1, f); fread(&vm_pal_s2b[i * 12], 4, 12, f); }
	vm_mesh = calloc(vm_nmesh, sizeof(VMMesh));
	for (int i = 0; i < vm_nmesh; i++) {
		VMMesh *m = &vm_mesh[i]; fread(&m->tex, 4, 1, f); fread(&m->blend, 4, 1, f); fread(&m->nv, 4, 1, f); fread(&m->ni, 4, 1, f);
		void *v = malloc(m->nv * 32), *ix = malloc(m->ni * 2); fread(v, 32, m->nv, f); fread(ix, 2, m->ni, f); if (m->ni & 1) { short pad; fread(&pad, 2, 1, f); }
		glGenBuffers(1, &m->vbo); glBindBuffer(GL_ARRAY_BUFFER, m->vbo); glBufferData(GL_ARRAY_BUFFER, m->nv * 32, v, GL_STATIC_DRAW);
		glGenBuffers(1, &m->ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, m->ni * 2, ix, GL_STATIC_DRAW);
		free(v); free(ix);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	vm_clip = calloc(vm_nclip, sizeof(VMClip));
	for (int i = 0; i < vm_nclip; i++) {
		VMClip *c = &vm_clip[i]; fread(c->name, 16, 1, f); fread(&c->nf, 4, 1, f); fread(&c->loop, 4, 1, f); fread(&c->fps, 4, 1, f);
		c->data = malloc(c->nf * vm_nb * 7 * 4); fread(c->data, 4, c->nf * vm_nb * 7, f);
	}
	unsigned na = 0; if (fread(&na, 4, 1, f) == 1) for (unsigned i = 0; i < na && i < 8; i++) { fread(vm_att_name[i], 16, 1, f); fread(vm_att[i], 4, 3, f); vm_natt++; }
	fclose(f); return 0;
}

// ---- shader
static const char *VM_VS =
"void main(float3 pos, float4 nrm, float2 uv, float4 bi, float4 bw,"
" uniform float4 m0, uniform float4 m1, uniform float4 m2, uniform float4 m3, uniform float4 pal[144], uniform float4 ldir,"
" float4 out vpos : POSITION, float2 out t0 : TEXCOORD0, float out sh : TEXCOORD1) {"
" float4 p = float4(pos, 1); float3 n = nrm.xyz;"
" int i0 = (int)(bi.x * 3.0 + 0.5), i1 = (int)(bi.y * 3.0 + 0.5), i2 = (int)(bi.z * 3.0 + 0.5), i3 = (int)(bi.w * 3.0 + 0.5);"
" float3 s = bw.x * float3(dot(pal[i0], p), dot(pal[i0 + 1], p), dot(pal[i0 + 2], p))"
"          + bw.y * float3(dot(pal[i1], p), dot(pal[i1 + 1], p), dot(pal[i1 + 2], p))"
"          + bw.z * float3(dot(pal[i2], p), dot(pal[i2 + 1], p), dot(pal[i2 + 2], p))"
"          + bw.w * float3(dot(pal[i3], p), dot(pal[i3 + 1], p), dot(pal[i3 + 2], p));"
" float3 sn = bw.x * float3(dot(pal[i0].xyz, n), dot(pal[i0 + 1].xyz, n), dot(pal[i0 + 2].xyz, n))"
"           + bw.y * float3(dot(pal[i1].xyz, n), dot(pal[i1 + 1].xyz, n), dot(pal[i1 + 2].xyz, n));"
" float4 c = float4(s, 1); vpos = float4(dot(m0, c), dot(m1, c), dot(m2, c), dot(m3, c)); t0 = uv;"
" sh = 0.40 + 0.75 * saturate(dot(normalize(sn + float3(0, 0, 0.0001)), ldir.xyz)); }";
static const char *VM_FS =
"float4 main(float2 t0 : TEXCOORD0, float sh : TEXCOORD1, uniform sampler2D dif : TEXUNIT0, uniform float4 tint) : COLOR {"
" float4 c = tex2D(dif, t0); return float4(c.rgb * sh * tint.rgb, c.a * tint.a); }";

typedef GLuint (*mkshader_fn)(GLenum, const char *);
static int vm_shader(mkshader_fn mk, void (*log)(const char *, ...)) {
	GLuint vs = mk(GL_VERTEX_SHADER, VM_VS), fs = mk(GL_FRAGMENT_SHADER, VM_FS);
	GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok); if (!ok) return -1;
	glGetShaderiv(fs, GL_COMPILE_STATUS, &ok); if (!ok) return -1;
	vm_prog = glCreateProgram(); glAttachShader(vm_prog, vs); glAttachShader(vm_prog, fs);
	glBindAttribLocation(vm_prog, 0, "pos"); glBindAttribLocation(vm_prog, 1, "nrm"); glBindAttribLocation(vm_prog, 2, "uv");
	glBindAttribLocation(vm_prog, 3, "bi"); glBindAttribLocation(vm_prog, 4, "bw");
	glLinkProgram(vm_prog); glGetProgramiv(vm_prog, GL_LINK_STATUS, &ok); if (!ok) return -2;
	for (int r = 0; r < 4; r++) { char nm[4] = { 'm', (char)('0' + r), 0 }; vm_u_m[r] = glGetUniformLocation(vm_prog, nm); }
	vm_u_pal = glGetUniformLocation(vm_prog, "pal"); vm_u_ldir = glGetUniformLocation(vm_prog, "ldir"); vm_u_tint = glGetUniformLocation(vm_prog, "tint");
	glUseProgram(vm_prog); glUniform1i(glGetUniformLocation(vm_prog, "dif"), 0);
	log("viewmodel shader: pal %d ldir %d tint %d\n", vm_u_pal, vm_u_ldir, vm_u_tint);
	return vm_u_pal == -1 ? -3 : 0;
}

// ---- poses
static VMBone *vm_tmp[4];
static void vm_alloc(void) { for (int i = 0; i < 4; i++) vm_tmp[i] = calloc(vm_nb, sizeof(VMBone)); }
static void vm_sample(int ci, float t, VMBone *out) {
	VMClip *c = &vm_clip[ci]; float fr = t * c->fps; int n = c->nf;
	if (c->loop) { float len = (float)(n - 1); fr = fmodf(fr, len); if (fr < 0) fr += len; }
	else { if (fr < 0) fr = 0; if (fr > n - 1) fr = (float)(n - 1); }
	int i0 = (int)fr; if (i0 >= n - 1) i0 = n - 2 < 0 ? 0 : n - 2; int i1 = i0 + 1 < n ? i0 + 1 : i0; float a = fr - i0; if (a > 1) a = 1;
	const float *A = c->data + (size_t)i0 * vm_nb * 7, *B = c->data + (size_t)i1 * vm_nb * 7;
	for (int b = 0; b < vm_nb; b++, A += 7, B += 7) {
		float q[4], l = 0; for (int k = 0; k < 4; k++) { q[k] = A[k] + (B[k] - A[k]) * a; l += q[k] * q[k]; }
		l = 1.0f / sqrtf(l > 1e-12f ? l : 1); for (int k = 0; k < 4; k++) out[b].q[k] = q[k] * l;
		for (int k = 0; k < 3; k++) out[b].p[k] = A[4 + k] + (B[4 + k] - A[4 + k]) * a;
	}
}
static void vm_blend(VMBone *dst, const VMBone *src, float w) { // dst = lerp(dst, src, w)
	if (w <= 0) return; if (w >= 1) { memcpy(dst, src, vm_nb * sizeof(VMBone)); return; }
	for (int b = 0; b < vm_nb; b++) {
		float d = 0; for (int k = 0; k < 4; k++) d += dst[b].q[k] * src[b].q[k];
		float s = d < 0 ? -w : w, l = 0;
		for (int k = 0; k < 4; k++) { dst[b].q[k] = dst[b].q[k] * (1 - w) + src[b].q[k] * s; l += dst[b].q[k] * dst[b].q[k]; }
		l = 1.0f / sqrtf(l); for (int k = 0; k < 4; k++) dst[b].q[k] *= l;
		for (int k = 0; k < 3; k++) dst[b].p[k] += (src[b].p[k] - dst[b].p[k]) * w;
	}
}

// ---- animation controller: a looping base layer with crossfades + a one-shot action layer
static int vm_base = -1, vm_base_prev = -1; static float vm_base_t, vm_base_prev_t, vm_base_fade = 1, vm_base_fade_len = 0.2f; static int vm_base_frozen;
static int vm_act = -1; static float vm_act_t, vm_act_in = 0.1f, vm_act_out = 0.15f, vm_act_w;
static float vm_clip_len(int ci) { return ci < 0 ? 0 : (vm_clip[ci].nf - 1) / vm_clip[ci].fps; }
static void vm_set_base(int ci, float fade, int frozen) {
	if (ci < 0 || (ci == vm_base && frozen == vm_base_frozen)) return;
	vm_base_prev = vm_base; vm_base_prev_t = vm_base_t; vm_base = ci; vm_base_t = 0; vm_base_fade = vm_base_prev < 0 ? 1 : 0; vm_base_fade_len = fade; vm_base_frozen = frozen;
}
static void vm_play(int ci, float fin, float fout) {
	if (ci < 0) return;
	if (ci == vm_act && vm_act_w > 0.5f) { vm_act_t = 0; vm_act_out = fout; return; }  // restart (e.g. automatic fire)
	vm_act = ci; vm_act_t = 0; vm_act_in = fin; vm_act_out = fout; if (vm_act_w > 1) vm_act_w = 1;
}
static int vm_action_busy(void) { return vm_act >= 0 && vm_act_t < vm_clip_len(vm_act) - vm_act_out; }
static void vm_update(float dt) {
	if (!vm_base_frozen) vm_base_t += dt; vm_base_prev_t += dt;
	if (vm_base_fade < 1) { vm_base_fade += dt / (vm_base_fade_len > 0.01f ? vm_base_fade_len : 0.01f); if (vm_base_fade > 1) vm_base_fade = 1; }
	if (vm_act >= 0) {
		vm_act_t += dt; float len = vm_clip_len(vm_act);
		float target = vm_act_t > len - vm_act_out ? (len - vm_act_t) / (vm_act_out > 0.01f ? vm_act_out : 0.01f) : 1;
		if (target < 0) target = 0;
		if (vm_act_w < target) { vm_act_w += dt / (vm_act_in > 0.01f ? vm_act_in : 0.01f); if (vm_act_w > target) vm_act_w = target; } else vm_act_w = target;
		if (vm_act_t >= len) { vm_act = -1; vm_act_w = 0; }
	}
}
static VMBone *vm_pose(void) {
	VMBone *P = vm_tmp[0];
	vm_sample(vm_base, vm_base_t, P);
	if (vm_base_fade < 1 && vm_base_prev >= 0) { vm_sample(vm_base_prev, vm_base_prev_t, vm_tmp[1]); vm_blend(vm_tmp[1], P, vm_base_fade * vm_base_fade * (3 - 2 * vm_base_fade)); memcpy(P, vm_tmp[1], vm_nb * sizeof(VMBone)); }
	if (vm_act >= 0 && vm_act_w > 0) { vm_sample(vm_act, vm_act_t, vm_tmp[2]); vm_blend(P, vm_tmp[2], vm_act_w); }
	return P;
}
// bone transform applied to a point in that bone's own space (e.g. an attachment in Grip space)
static void vm_bone_xform(const VMBone *B, const float *v, float *out) {
	float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
	float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
	for (int r = 0; r < 3; r++) out[r] = R[r * 3] * v[0] + R[r * 3 + 1] * v[1] + R[r * 3 + 2] * v[2] + B->p[r];
}

// draw: proj = column-major 4x4 projection; off = camera-space offset (sway/kick); pitch = small extra rotation about camera x
static float vm_palbuf[144 * 4];
static void vm_draw(const float *proj, const VMBone *P, const float *off, float kick_pitch, const float *tint) {
	float c = cosf(kick_pitch), s = sinf(kick_pitch);
	for (int e = 0; e < vm_npal && e < 48; e++) {
		const VMBone *B = &P[vm_pal_slot[e]];
		float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3];
		float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
		float T[3] = { B->p[0], B->p[1], B->p[2] };
		// kick: rotate everything about the camera x axis, then offset
		float R2[9], T2[3];
		for (int col = 0; col < 3; col++) { R2[0 * 3 + col] = R[0 * 3 + col]; R2[1 * 3 + col] = c * R[1 * 3 + col] - s * R[2 * 3 + col]; R2[2 * 3 + col] = s * R[1 * 3 + col] + c * R[2 * 3 + col]; }
		T2[0] = T[0] + off[0]; T2[1] = c * T[1] - s * T[2] + off[1]; T2[2] = s * T[1] + c * T[2] + off[2];
		const float *S = &vm_pal_s2b[e * 12]; float *o = &vm_palbuf[e * 12];
		for (int r = 0; r < 3; r++) for (int k = 0; k < 4; k++)
			o[r * 4 + k] = R2[r * 3] * S[k] + R2[r * 3 + 1] * S[4 + k] + R2[r * 3 + 2] * S[8 + k] + (k == 3 ? T2[r] : 0);
	}
	glUseProgram(vm_prog);
	for (int r = 0; r < 4; r++) { float row[4] = { proj[r], proj[4 + r], proj[8 + r], proj[12 + r] }; glUniform4fv(vm_u_m[r], 1, row); }
	glUniform4fv(vm_u_pal, (vm_npal < 48 ? vm_npal : 48) * 3, vm_palbuf);   // never above pal[144]: vitaGL does not bound the count
	const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }; glUniform4fv(vm_u_ldir, 1, ld);
	glEnableVertexAttribArray(3); glEnableVertexAttribArray(4); glActiveTexture(GL_TEXTURE0);
	for (int pass = 0; pass < 2; pass++)
	for (int i = 0; i < vm_nmesh; i++) {
		VMMesh *m = &vm_mesh[i];
		if ((m->blend == 2) != pass) continue;
		if (m->blend == 2) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE); glDepthMask(GL_FALSE); }
		glUniform4fv(vm_u_tint, 1, tint);
		glBindTexture(GL_TEXTURE_2D, vm_tex[m->tex]);
		glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
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
