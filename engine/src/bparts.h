// bparts.h - b80: the PC's destruction deaths. Paz video EP19 (TDM Electronics Center, v2.0) 271.7 / 336.5 / 410.6 s: a
// headshot kill pops the head - the enemy mask with it - off the body in a white flash and a ~2 m red cloud with machine-part
// debris and sparks; the head tumbles away dark, with glowing orange cracks (the game's DissolveRamp red -> yellow), and the
// neck stump crackles orange for a moment. The game's data for it: the destruction death clips (chr.h d_xhead, d_xlarm,
// d_xrarm, d_xlleg, d_xrleg), the foley destruct_head / _arm / _leg, effect.dat character\body_arm_explosion.nif /
// body_all_explosion.nif / body_arm_break_a, _b, _blood.nif (fx_sk.gfx DX_*: tools/skills/build_skills_fx.py). Heavy hits
// (bots.h bot_hurt: kind 2) do the same to an arm or a leg.
// How: bots.h bot_hurt -> bot_on_destruct = bp_spawn. The body keeps drawing with the part's palette entries collapsed onto
// its root joint (chr.h CSkin cmask / croot: no geometry change, the neck tapers into the stump); the part is the same model
// with every other entry collapsed (cinv), skinned once in the pose of the moment (CPU skinning: its own buffers, one set,
// allocated at match start), drawn under a moving model matrix (ballistic, spinning, bouncing on the floor, shrinking away
// at the end), then vision.h's vis_burn adds the glowing cracks. Up to BP_N parts fly at once (the oldest is reused).
// main.c: bp_reset (match start), bp_update (after the bots), bp_draw (after bots_draw), bot_on_destruct = bp_spawn.
#pragma once
#define BP_N 3
#define BP_LOD 1          // the parts' skinning LOD (the head is small on screen; 75 % of LOD 0's vertices)
#define BP_LIFE 3.2f
typedef struct {
	int on, bot, part, masked, fem, skinned; float t, glow;
	const Model *M; CSkin sk; MBone pose[64]; int head_mb; const float *mask_at;
	float Mw0[16];            // the body's model -> world at the death
	float c0[3], c[3], v[3], ax[3], ang, w, lit[3], ember_t, rad;
	int rest;
} BPart;
static BPart bp[BP_N];
typedef struct { float t, ele_t, drip_t; int mb, part; } BStump;   // per bot: the stump's crackle / blood (t = time left)
static BStump bst[NBOT_MAX];
static int bp_fx[4] = { -1, -1, -1, -1 };   // DX_BURST, DX_STUMP, DX_DRIP, DX_EMBER (by name: fx_sk.gfx)
static int bp_nspawn;
static float bp_rnd(void) { return (float)rand() / (float)RAND_MAX; }
static void bp_mul(float *o, const float *a, const float *b) {   // column-major o = a * b
	float r[16]; for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) { float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k]; r[c * 4 + rr] = s; }
	memcpy(o, r, 64);
}
static void bp_body_matrix(const Bot *b, float *M) {   // as bots_draw
	float th = b->pl.yaw + 1.5707963f, c = cosf(th), s = sinf(th);
	float m[16] = { c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, b->pl.pos.x, b->pl.pos.y, b->pl.pos.z - b->death_drop, 1 }; memcpy(M, m, 64);
}
static void bp_xform(const float *M, const float *p, float *o) { for (int r = 0; r < 3; r++) o[r] = M[r] * p[0] + M[4 + r] * p[1] + M[8 + r] * p[2] + M[12 + r]; }
static int bp_bank_root(const CBank *B, int part) {
	static const char *N[6] = { NULL, "Bip01_Head", "Bip01_L_UpperArm", "Bip01_R_UpperArm", "Bip01_L_Thigh", "Bip01_R_Thigh" };
	return part >= 1 && part <= 5 ? cb_find_bone(B, N[part]) : -1;
}
// one buffer set (s = 0) per slot, sized for the LOD BP_LOD meshes of model M (grows; never shrinks)
static void bp_alloc(CSkin *c, const Model *M) {
	if (chr_gpu_skin || !M || !M->ok) return;
	int lod = chr_clamp_lod(M, BP_LOD), ms[CHR_SLOTS], n = chr_lod_meshes(M, lod, ms);
	for (int k = 0; k < n; k++) { const MMesh *m = &M->mesh[ms[k]]; if (!m->sp) continue; int need = SKIN_OUT_BYTES(m->sp); if (need <= c->cap[k]) continue;
		if (!c->vbo[0][k]) glGenBuffers(1, &c->vbo[0][k]);
		glBindBuffer(GL_ARRAY_BUFFER, c->vbo[0][k]); glBufferData(GL_ARRAY_BUFFER, need, NULL, GL_DYNAMIC_DRAW); c->cap[k] = need; }
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}
static int bp_fx_find(const char *n) { if (!fx_ok) return -1; for (int i = 0; i < fx_neff; i++) if (!strncmp(fx_eff[i].name, n, 16)) return i; return -1; }
static void (*bp_logf)(const char *fmt, ...);
// match start: the effects, the buffers for every operator model loaded now, nothing flying
static void bp_reset(void) {
	static const char *FN[4] = { "DX_BURST", "DX_STUMP", "DX_DRIP", "DX_EMBER" };
	for (int k = 0; k < 4; k++) bp_fx[k] = bp_fx_find(FN[k]);
	for (int k = 0; k < BP_N; k++) { bp[k].on = 0; for (int c = 0; c < nchrt; c++) bp_alloc(&bp[k].sk, &chrt[c].m); }
	memset(bst, 0, sizeof(bst));
	static int logged; if (!logged && bp_logf) { logged = 1; int cap = 0; for (int k = 0; k < CHR_SLOTS; k++) cap += bp[0].sk.cap[k];
		bp_logf("destruction deaths: effects %s, part buffers %d x %d KB (b80)\n", bp_fx[0] >= 0 && bp_fx[1] >= 0 ? "ok" : "MISSING", BP_N, cap / 1024); }
}
static BPart *bp_slot(void) {
	BPart *best = &bp[0];
	for (int k = 0; k < BP_N; k++) { if (!bp[k].on) return &bp[k]; if (bp[k].t > best->t) best = &bp[k]; }
	return best;
}
// bots.h bot_on_destruct: bot i died a destruction death that blew off part (1 head, 2 / 3 left / right arm, 4 / 5 left / right
// leg); dir = where the killing hit came from (towards the victim)
static void bp_spawn(int i, int part, V3 dir) {
	if (i < 0 || i >= nbot || part < 1 || part > 5) return;
	Bot *b = &bots[i]; ChrType *C = &chrt[b->ct]; const Model *M = &C->m; if (!b->pose || !M->ok) return;
	const CBank *B = &chr_bank[C->female & 1]; int root = -1;
	unsigned long long m = chr_part_mask(M, &C->bind, B, bp_bank_root(B, part), &root); if (!m || root < 0) return;
	b->sk.cmask = m; b->sk.croot = root; b->sk.cinv = 0; b->sk.cur = -1; b->pose_new = 1;   // the body: the part collapsed (re-skinned at its next draw)
	float Mw[16]; bp_body_matrix(b, Mw);
	// the part: its own skinning of this pose, every other palette entry collapsed onto the root joint
	BPart *P = bp_slot(); memset(&P->c0, 0, sizeof(float) * 3); P->on = 1; P->bot = i; P->part = part; P->t = 0; P->glow = 1; P->rest = 0;
	P->M = M; int nb = M->nb < 64 ? M->nb : 64; memcpy(P->pose, b->pose, sizeof(MBone) * nb); memcpy(P->Mw0, Mw, 64);
	P->sk.cmask = m; P->sk.croot = root; P->sk.cinv = 1; P->sk.ok = 1; P->skinned = 0;
	P->fem = C->female & 1; P->head_mb = C->head; P->mask_at = C->mask_at;
	P->masked = part == 1 && bot_mask_on && b->team == 1 && C->mask_at && C->head >= 0 && bot_mask_ok[P->fem] > 0;   // (bots.h bot_masked before the head went)
	if (chr_gpu_skin) { P->sk.cur = 0; P->sk.lod = chr_clamp_lod(M, BP_LOD); P->skinned = 1; }
	else {
		int lod = chr_clamp_lod(M, BP_LOD), ms[CHR_SLOTS], n = chr_lod_meshes(M, lod, ms), ok = n > 0;
		float pal[48 * 12]; chr_palette(M, &P->sk, P->pose, pal); const float *qp = skin_pal(pal, M->npal);
		for (int k = 0; k < n; k++) { const MMesh *mm = &M->mesh[ms[k]]; if (!mm->sp || !P->sk.vbo[0][k] || SKIN_OUT_BYTES(mm->sp) > P->sk.cap[k] || !skin_add(mm->sp, qp + mm->pbase * 12, P->sk.vbo[0][k])) ok = 0; }
		skin_run(); P->sk.cur = 0; P->sk.lod = lod; P->skinned = ok;
	}
	// its centre: the root joint and the bones below it (the head: the mask's seat, or 10 cm up the head bone)
	float cm[3] = { 0, 0, 0 }; int nc = 0; const MBone *R = &P->pose[root];
	if (part == 1) { float o[3], v[3] = { 10, 0, 0 }; if (C->mask_at) memcpy(v, C->mask_at, 12); quat_rotate(R->q, v, o); for (int k = 0; k < 3; k++) cm[k] = R->p[k] + o[k]; nc = 1; P->rad = 13; }
	else { for (int mb = 0; mb < nb && mb < C->bind.n; mb++) { int bb = C->bind.map[mb]; for (int g = 0; bb >= 0 && g < CB_MAXB; bb = B->parent[bb], g++) if (bb == C->bind.map[root]) { for (int k = 0; k < 3; k++) cm[k] += P->pose[mb].p[k]; nc++; break; } }
		if (nc) for (int k = 0; k < 3; k++) cm[k] /= nc; P->rad = 8; }
	bp_xform(Mw, cm, P->c0); memcpy(P->c, P->c0, 12);
	// the throw: away from the shot and up (the footage: the head pops straight up, then tumbles away), a fast tumble
	float dl = sqrtf(dir.x * dir.x + dir.y * dir.y); float dx = dl > 1e-3f ? dir.x / dl : cosf(b->pl.yaw + 3.14159f), dy = dl > 1e-3f ? dir.y / dl : sinf(b->pl.yaw + 3.14159f);
	float side = (bp_rnd() * 2 - 1) * 90, fwd = 150 + 130 * bp_rnd();
	P->v[0] = dx * fwd - dy * side; P->v[1] = dy * fwd + dx * side; P->v[2] = part == 1 ? 360 + 120 * bp_rnd() : 230 + 100 * bp_rnd();
	float ax[3] = { -dy + 0.4f * (bp_rnd() - 0.5f), dx + 0.4f * (bp_rnd() - 0.5f), 0.6f * (bp_rnd() - 0.5f) }, al = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]) + 1e-6f;
	for (int k = 0; k < 3; k++) P->ax[k] = ax[k] / al; P->ang = 0; P->w = (bp_rnd() < 0.5f ? -1 : 1) * (7 + 5 * bp_rnd()); P->ember_t = 0.05f;
	memcpy(P->lit, b->lit, 12);
	// the burst at the joint (white flash, sparks, debris, the red cloud) and the stump's crackle / blood
	float jw[3]; bp_xform(Mw, R->p, jw); float up[3] = { 0, 0, 1 }, d3[3] = { dir.x, dir.y, dir.z }; float l3 = sqrtf(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
	if (l3 > 1e-3f) for (int k = 0; k < 3; k++) d3[k] /= l3; else memcpy(d3, up, 12);
	if (bp_fx[0] >= 0) fx_spawn(bp_fx[0], jw, up, d3);
	bst[i].t = 1.7f; bst[i].ele_t = 0.06f; bst[i].drip_t = 0.05f; bst[i].mb = root; bst[i].part = part;
	if (bp_logf && bp_nspawn < 4) bp_logf("  destruction: bot %d lost its %s (%d palette entries, part %s) (b80)\n", i, part == 1 ? "head" : part <= 3 ? "arm" : "leg",
		__builtin_popcountll(m), P->skinned ? "skinned" : "NOT skinned");
	bp_nspawn++;
}
static void bp_update(float dt) {
	if (dt <= 0) return; if (dt > 0.1f) dt = 0.1f;
	for (int k = 0; k < BP_N; k++) {
		BPart *P = &bp[k]; if (!P->on) continue; P->t += dt; if (P->t > BP_LIFE) { P->on = 0; continue; }
		P->glow = P->t < 0.6f ? 1 : fmaxf(0.3f, 1 - (P->t - 0.6f) / 1.8f * 0.7f);
		if (!P->rest) {
			float mv[3] = { P->v[0] * dt, P->v[1] * dt, 0 }, ml = sqrtf(mv[0] * mv[0] + mv[1] * mv[1]);
			if (ml > 0.01f) { V3 o = v3(P->c[0], P->c[1], P->c[2]); float hw = raycast(o, v3(mv[0] / ml, mv[1] / ml, 0), ml + P->rad, NULL, NULL);
				if (hw >= 0) { P->v[0] *= -0.3f; P->v[1] *= -0.3f; mv[0] = mv[1] = 0; P->w *= 0.7f; } }   // a wall: back off it
			P->v[2] -= 980 * dt; P->c[0] += mv[0]; P->c[1] += mv[1]; P->c[2] += P->v[2] * dt;
			V3 up = v3(P->c[0], P->c[1], P->c[2] + 60); float hf = raycast(up, v3(0, 0, -1), 500, NULL, NULL);
			float fz = hf >= 0 ? up.z - hf : -1e9f;
			if (P->c[2] - P->rad < fz && P->v[2] < 0) {   // a bounce: loses most of it
				P->c[2] = fz + P->rad; P->v[2] = -P->v[2] * 0.3f; P->v[0] *= 0.55f; P->v[1] *= 0.55f; P->w *= 0.55f;
				if (P->v[2] < 70) { P->v[2] = 0; if (fabsf(P->v[0]) + fabsf(P->v[1]) < 40) P->rest = 1; } }
			P->ang += P->w * dt;
		}
		P->ember_t -= dt;
		if (P->ember_t <= 0 && P->glow > 0.45f && bp_fx[3] >= 0) { float up_[3] = { 0, 0, 1 }; fx_spawn(bp_fx[3], P->c, up_, NULL); P->ember_t = 0.06f + 0.05f * bp_rnd(); }
	}
	for (int i = 0; i < nbot && i < NBOT_MAX; i++) {   // the stumps: crackle and blood at the joint, on the body as it falls
		BStump *S = &bst[i]; if (S->t <= 0) continue; Bot *b = &bots[i]; S->t -= dt;
		if (b->state != BS_DEAD || !b->pose) { S->t = 0; continue; }
		float Mw[16], jw[3]; bp_body_matrix(b, Mw); bp_xform(Mw, b->pose[S->mb].p, jw); float up[3] = { 0, 0, 1 };
		S->ele_t -= dt; if (S->ele_t <= 0 && bp_fx[1] >= 0) { fx_spawn(bp_fx[1], jw, up, NULL); S->ele_t = 0.05f + 0.05f * bp_rnd(); }
		S->drip_t -= dt; if (S->drip_t <= 0 && S->t > 0.5f && bp_fx[2] >= 0) { fx_spawn(bp_fx[2], jw, up, NULL); S->drip_t = 0.09f + 0.06f * bp_rnd(); }
	}
}
// the part's model -> world: about its centre, spun and moved, shrinking away in its last 0.4 s
static void bp_matrix(const BPart *P, float *Mo) {
	float s = P->t > BP_LIFE - 0.4f ? fmaxf(0, (BP_LIFE - P->t) / 0.4f) : 1;
	float x = P->ax[0], y = P->ax[1], z = P->ax[2], c = cosf(P->ang), sn = sinf(P->ang), t = 1 - c;
	float Rm[9] = { t * x * x + c, t * x * y + sn * z, t * x * z - sn * y,  t * x * y - sn * z, t * y * y + c, t * y * z + sn * x,  t * x * z + sn * y, t * y * z - sn * x, t * z * z + c };   // columns
	float A[16] = { Rm[0] * s, Rm[1] * s, Rm[2] * s, 0, Rm[3] * s, Rm[4] * s, Rm[5] * s, 0, Rm[6] * s, Rm[7] * s, Rm[8] * s, 0, 0, 0, 0, 1 };
	for (int r = 0; r < 3; r++) A[12 + r] = P->c[r] - (A[r] * P->c0[0] + A[4 + r] * P->c0[1] + A[8 + r] * P->c0[2]);   // T(c) S R T(-c0)
	bp_mul(Mo, A, P->Mw0);
}
static void vis_burn(const Model *M, CSkin *sk, const float *pal, const float *MVP, const float *Mw, const float *cam, float glow, float seed);
static void bp_draw(const float *VP, const float *fogc, const float *cam) {
	static const float Lw[3] = { 0.33f, 0.42f, 0.84f };
	for (int k = 0; k < BP_N; k++) {
		BPart *P = &bp[k]; if (!P->on) continue;
		float Mp[16], MVP[16]; bp_matrix(P, Mp); bp_mul(MVP, VP, Mp);
		float ld[4] = { Mp[0] * Lw[0] + Mp[1] * Lw[1] + Mp[2] * Lw[2], Mp[4] * Lw[0] + Mp[5] * Lw[1] + Mp[6] * Lw[2], Mp[8] * Lw[0] + Mp[9] * Lw[1] + Mp[10] * Lw[2], 0 };
		float ll = sqrtf(ld[0] * ld[0] + ld[1] * ld[1] + ld[2] * ld[2]) + 1e-6f; for (int r = 0; r < 3; r++) ld[r] /= ll;
		float ch = 0.55f + 0.2f * (1 - P->glow), tint[4] = { 1, 1, 1, 1 }; for (int r = 0; r < 3; r++) tint[r] = fminf(1.5f, fmaxf(0.18f, P->lit[r] * 1.15f)) * ch;   // charred
		md_rim = BOT_RIM; md_masked = P->masked;
		if (P->skinned) chr_draw(P->M, &P->sk, P->sk.lod, MVP, P->pose, ld, tint, fogc, 0);
		MBone Bm; float mpal[12];
		if (P->masked) { Bm = P->pose[P->head_mb]; float o[3]; quat_rotate(Bm.q, P->mask_at, o); for (int r = 0; r < 3; r++) Bm.p[r] += o[r];
			chr_draw_rigid(&bot_mask[P->fem], MVP, &Bm, ld, tint, fogc);
			const float *S = bot_mask[P->fem].pal_s2b; float x = Bm.q[0], y = Bm.q[1], z = Bm.q[2], w = Bm.q[3];
			float R[9] = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) };
			for (int r = 0; r < 3; r++) for (int q = 0; q < 4; q++) mpal[r * 4 + q] = R[r * 3] * S[q] + R[r * 3 + 1] * S[4 + q] + R[r * 3 + 2] * S[8 + q] + (q == 3 ? Bm.p[r] : 0); }
		md_masked = 0; md_rim = 0;
		// the glowing cracks (vision.h): the part, then the mask over it
		if (P->skinned) { float pal[48 * 12]; chr_palette(P->M, &P->sk, P->pose, pal); vis_burn(P->M, &P->sk, pal, MVP, Mp, cam, P->glow, (float)k * 0.37f); }
		if (P->masked) vis_burn(&bot_mask[P->fem], NULL, mpal, MVP, Mp, cam, P->glow, (float)k * 0.37f + 0.5f);
	}
}
