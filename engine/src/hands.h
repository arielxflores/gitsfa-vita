// b99: the operator's hands alone, for the two things the PC plays whatever gun is out (the user: "add everything that you can";
// the Demolition video):
//  - the skill gesture (7:23.5-7:25): the gun goes down, the right forearm comes up across the view and the left hand taps it
//    (the M4A1 set's skill_start, 1.37 s), then the gun comes back up;
//  - planting (4:47-4:52, 5.0 s): the left hand holds the TATP-2M up across the view, the right hand works it (install_start,
//    4.63 s, stretched over the plant), then the hands let go as it sticks to the wall (install_end 0.43 s) and the gun comes back.
// vm_hands.gvm (vm/make_vm_weapon.py CFG 'hands'): the guns' arms on the M4A1 rig, no gun, camera space as the viewmodels.
// The device (dem_bomb_1p.gvm, the game's bomb_1p.nif in its Bomb_Root space) rides on the left hand turned half round the
// hand's z (the left side's mirrored axes): found by trying the 24 axis conventions on every bone against the footage - only
// this one holds it level, face to the camera, left end in the left hand, as the footage has it through the whole clip.
// Lenses fitted to the footage frame by frame (as the arm launcher's, skills.h b78): the gesture under the guns' 49.75 deg, the
// plant under 60 deg (the device 72% of the screen wide); both 6 cm higher than the gun clips' camera puts them.
// b100 (the user: "make sure that the bomb device renders and functions properly with all it's animations and effects"): the
// device's own effects from the game's files (tools/dem/make_bomb_fx.py prints the numbers below from them; dem_fx.gtx):
//  - effect\DM\DM_device_install_button.nif: five keys on its face flash white as the right hand taps along them (0.1 s each
//    at 0 / 0.2 / 0.47 / 0.8 / 1.2 s; started 2.0 s into the plant - the footage's taps, 4:49.0-4:50.3);
//  - effect\DM\DM_bomb_1p_fx.nif, 2.5 s, over the second half of the plant (the footage's knob lights 4.0 s in = its 1.4 s):
//    the two dark bands of the display fill red - a tint from grey-teal to red, a flip book of 32 red waves over it for the
//    first seconds (additive, the picture growing), sparks drifting along (two scrolling layers) - and the knob's lamp
//    comes on red at 1.33-1.47 s.
// The bands' tint is drawn 1.6 x the file's alpha (0.5 at the end: half-dark red on the Vita; the footage ends bright red).
// b101: DM_device_install_success.nif is the hexagon read-out on the device's face while planting (dm_device_w.dds).
// Defusing (the Demolition video 6:57.8-7:05): the M4A1 set's clear_start (2.53 s, its last frame held) and clear_end - the
// right fist comes in low on the right, the open left hand passes before it; no gun in the picture; over the forearm the
// game's read-out, effect\kfm\DM_device_install (hv_dev_draw below, dem_dev.gtx).
// Not drawn: the device's lead ('line', mesh 1: its place in the right hand is not known).
// b121 (the user: "make sure that reiko's net jump work properly"; EP26 4:43-4:46 and 7:16-7:22): in the network Reiko's own
// hands are in the picture, open and empty, as pale holograms - the right one low on the right with its fingers spread, the
// left one swinging in at the lower left while she sprints. The game has the clips: its 'overload' set (the hand-held
// charge's: stand_idle 3.57 s, stand_run_f 0.67 s, sprint 0.47 s, swap_end 0.5 s = the hand coming up, jump_fall), baked
// into vm_hands.gvm as nj_* (vm/make_vm_weapon.py CFG 'hands'). Lens and place laid over the footage (five frames, the
// hand's outline: tools' fit 0.64 of the area shared): a 62 deg lens, 9 cm right, 2 cm up, 4 cm nearer.
enum { HVC_SKILL, HVC_INST, HVC_INST_END, HVC_CLEAR, HVC_CLEAR_END, HVC_NJ_IDLE, HVC_NJ_RUN, HVC_NJ_SPRINT, HVC_NJ_IN, HVC_NJ_FALL, HVC_N };
enum { HV_OFF, HV_SKILL, HV_PLANT, HV_LETGO, HV_DEFUSE, HV_DEFEND, HV_NETJUMP };
static float hv_nj_ph[3], hv_nj_w[3]; static int hv_nj_end;   // the idle / run / sprint clips' clocks; the run, sprint and in-the-air weights; the jump ended this frame
static void vis_hands_holo(const Model *M, const float *PV, const MBone *P, float k);   // (vision.h)
static Model hv_m, hv_bomb; static int hv_ok, hv_bomb_ok, hv_c[HVC_N], hv_lh = -1, hv_rh = -1, hv_mode, hv_said, hv_dev_was; static float hv_t;
static GLuint hv_dev_tex;   // dem_dev.gtx: the read-out over the forearm while defusing (b101)
// b102: the hologram of the skill gesture (the Demolition video 7:24-7:25: a disc of rings with the skill's picture over the
// device just behind the right wrist, the picture turning orange as the skill engages) = effect\cyberbrain\device_<skill>.nif,
// a keyed effect scene (fxscene.h; sk_dev_<operator>.gsc, tools/fx/make_scene.py). Its space is the right forearm's: x from
// the elbow to the wrist - the rings gather at x 19.4 of the 21.5 cm, 6 cm off the bone toward the viewer, the device's
// ring and three keys light on the arm at x 13-19 (they sit on the PC's cyborg forearm; faint glows here). Six operators
// have one: Motoko (op_camouflage), Saito (heat), Togusa (m_tachikoma), Ishikawa (turret), Borma (endocoverflow), Paz (sprint).
static FxScene hv_sc; static int hv_rft = -1, hv_rfa = -1, hv_sc_chr = -1;   // the scene of the local player's operator; the right fore-twist bone (at the elbow, the forearm's axes)
static void hv_skill_scene(const char *vdir, const char *stem, int chr) {   // at match setup
	if (chr == hv_sc_chr) return; fxs_free(&hv_sc); hv_sc_chr = chr; if (hv_ok <= 0 || hv_rft < 0 || !stem) return;
	char p[192]; snprintf(p, sizeof(p), "%ssk_dev_%s.gsc", vdir, stem);
	if (fxs_load(&hv_sc, p) == 0) logp("hands: the skill gesture's hologram for %s (sk_dev_%s.gsc, %.1f s - b102)\n", stem, stem, fxs_len(&hv_sc));
}
static GLuint hv_fx_tex;
// bomb_01 (the device's own space: x along it, -y toward the holder, z up) -> Bomb_Root, the model's space (bomb_1p.nif)
static const float HVF_M[3][4] = { { -0.6804f, -0.1031f, -0.7255f, -14.9861f }, { -0.4317f, 0.8564f, 0.2832f, -9.2513f }, { 0.5921f, 0.5060f, -0.6272f, 5.4612f } };
#define HVF_T0 2.5f      // the red fill starts (s into the plant)
#define HVF_KEYS 2.0f    // the keys' flashes start
#define HVF_BX0 (-13.80f)   // the bands: x from / to, the plane (just before the recess' wall), upper and lower z
#define HVF_BX1 8.34f
#define HVF_BY (-0.95f)
static void hvf_pt(const float D[3][4], float x, float y, float z, float *o) { for (int r = 0; r < 3; r++) o[r] = D[r][0] * x + D[r][1] * y + D[r][2] * z + D[r][3]; }
static void hvf_quad(const float D[3][4], float x0, float x1, float y, float z0, float z1, float u0, float v0, float u1, float v1, const float *col) {
	float a[3], b[3], c[3], d[3]; hvf_pt(D, x0, y, z1, a); hvf_pt(D, x1, y, z1, b); hvf_pt(D, x1, y, z0, c); hvf_pt(D, x0, y, z0, d);
	sprite_quad(a, b, c, d, u0, v0, u1, v1, col);
}
// a band-long strip of a row of the atlas scrolled by off (the texture wraps: two pieces)
static void hvf_scroll(const float D[3][4], float y, float z0, float z1, float v0, float v1, float off, const float *col) {
	float f = off - floorf(off), xm = HVF_BX0 + (HVF_BX1 - HVF_BX0) * (1 - f);
	if (f < 0.999f) hvf_quad(D, HVF_BX0, xm, y, z0, z1, f, v0, 0.999f, v1, col);
	if (f > 0.001f) hvf_quad(D, xm, HVF_BX1, y, z0, z1, 0.001f, v0, f, v1, col);
}
// pt: seconds into the plant; B: the device's bone (camera space)
static void hv_fx_draw(const float *PV, const MBone *B, float pt) {
	if (!hv_fx_tex || !sp_ok || sp_n) return;
	float x = B->q[0], y = B->q[1], z = B->q[2], w = B->q[3], R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) }, { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) }, { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
	float D[3][4]; for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) D[r][c] = R[r][0] * HVF_M[0][c] + R[r][1] * HVF_M[1][c] + R[r][2] * HVF_M[2][c] + (c == 3 ? B->p[r] : 0);
	const float T = 1.0f / 512;
	if (pt > 0.6f && !hv_dev_tex) {   // b101: the hexagon read-out on the device's face (DM_device_install_success.nif: dm_device_w.dds on a 14.7 x 7.3 cm plane
		// 3 cm before the middle of the face; shown half a second, faded out over a sixth, again - as the footage, from the device being up)
		float ph = fmodf(pt - 0.6f, 0.6667f), al = ph < 0.5f ? 1 : 1 - (ph - 0.5f) / 0.1667f, col[4] = { 1, 1, 1, 0.9f * al };
		hvf_quad(D, -7.33f, 7.33f, -3.045f, -3.665f, 3.665f, 129 * T, 385 * T, 383 * T, 511 * T, col); sprite_flush(PV, hv_fx_tex, 0); }
	{   // the keys (DM_device_install_button.nif: nodes 4, 3, 5, 2, 1)
		static const float K[5][3] = { { -2.482f, -1.456f, 0.0f }, { -0.205f, -0.132f, 0.2f }, { 1.986f, -1.437f, 0.467f }, { -6.908f, 1.124f, 0.8f }, { -6.887f, -1.461f, 1.2f } };
		for (int k = 0; k < 5; k++) { float t = pt - HVF_KEYS - K[k][2]; if (t <= 0 || t >= 0.1f) continue;
			float al = t < 0.0333f ? t / 0.0333f : 1 - (t - 0.0333f) / 0.0667f, col[4] = { 1, 1, 1, al };
			hvf_quad(D, K[k][0] - 1.3624f, K[k][0] + 1.3624f, -4.324f, K[k][1] - 1.3624f, K[k][1] + 1.3624f, 1 * T, 385 * T, 63 * T, 447 * T, col); }
		sprite_flush(PV, hv_fx_tex, 1);
	}
	float t = pt - HVF_T0; if (t <= 0) return; if (t > 2.5f) t = 2.5f;
	for (int k = 0; k < 2; k++) {   // the bands' tint: grey-teal to red
		float z0 = k ? -6.836f : 2.263f, z1 = k ? -1.551f : 7.548f, f = t / 2.5f, col[4] = { 0.604f + 0.396f * f, 0.733f * (1 - f), 0.745f * (1 - f), fminf(1, 0.5f * f * 1.6f) };
		hvf_quad(D, HVF_BX0, HVF_BX1, HVF_BY, z0, z1, 78 * T, 394 * T, 82 * T, 398 * T, col); }
	sprite_flush(PV, hv_fx_tex, 0);
	for (int k = 0; k < 2; k++) {   // the waves (32 frames in 1.067 s, the lower band's from 0.5 s; the picture grows) and the sparks
		float z0 = k ? -6.836f : 2.263f, z1 = k ? -1.551f : 7.548f, tw = t - (k ? 0.5f : 0), al = t < 1 ? 0.5f * t : 0.5f * (1 - (t - 1) / 1.5f);
		if (tw >= 0 && al > 0.003f) { int fr = (int)(tw * 30) % 32; float sc = (k ? 0.4f : 0.5f) + (k ? -0.25f : -0.25f) * fminf(1, t / 2), cu = (fr % 8 + 0.5f) * 64, cv = (fr / 8 + 0.5f) * 64, hs = sc * 32 - 0.5f, col[4] = { 1, 1, 1, al };
			hvf_quad(D, HVF_BX0, HVF_BX1, HVF_BY - 0.05f, z0, z1, (cu - hs) * T, (cv - hs) * T, (cu + hs) * T, (cv + hs) * T, col); }
		float sa = 0.25f * fminf(1, t), scol[4] = { 1, 1, 1, sa }, zc = k ? -4.213f : 4.893f;
		hvf_scroll(D, HVF_BY - 0.1f, zc - 2.625f, zc + 2.625f, 289 * T, 319 * T, (k ? -0.29f : 0) - t / 2, scol);
		hvf_scroll(D, HVF_BY - 0.1f, zc - 2.625f, zc + 2.625f, 257 * T, 287 * T, (k ? -0.17f : 0) - t / 4, scol);
	}
	if (t > 1.333f) { float col[4] = { 1, 0.05f, 0, 0.5f * fminf(1, (t - 1.333f) / 0.134f) };   // the knob's lamp
		hvf_quad(D, 14.364f - 1.5f, 14.364f + 1.5f, -2.45f, -7.68f - 1.5f, -7.68f + 1.5f, 78 * T, 394 * T, 82 * T, 398 * T, col); }
	sprite_flush(PV, hv_fx_tex, 1);
}
// b101: the read-out over the forearm while defusing = effect\kfm\DM_device_install.nif played through its three sequences
// (DM_device_install_root_start / loop / end.kf, decoded by tools/fx/kfseq.py; the tables are tools/dem/make_bomb_fx.py's print).
// A 22.1 x 11.05 cm panel of hexagon cells: a dark backing, the outline, the cells' teal fill (added at half strength, and once
// more scrolling down 4.27 times a loop, pulsing to 0.29), growing from a tenth in 0.167 s under a flickering white flash; 5.7 cm
// right of its middle a ring (5.2 cm, one turn a loop of 1.067 s) round the bomb's picture (3.2 cm, pulsing 1 - 0.41), each in
// blue with a red and a green ghost at half strength that tremble about a millimetre (the colour fringes of the footage).
// Left out: three hairlines (0.04 cm) and three strips of 2 mm text that sweep it, and the turning ball of hexagons in the ring.
// C: its middle in the view's space (the panel faces the view); t: seconds since it opened; te: seconds since it began to close (< 0: open).
static const float HVD_START_S[9] = { 0.100f, 0.137f, 0.236f, 0.377f, 0.539f, 0.702f, 0.847f, 0.952f, 0.999f };
static const float HVD_START_FLASH[9] = { 0.000f, 0.848f, 0.562f, 0.284f, 0.509f, 0.727f, 0.432f, 0.130f, 0.981f };
static const float HVD_START_A[9] = { 0.000f, 0.042f, 0.152f, 0.308f, 0.488f, 0.669f, 0.830f, 0.947f, 0.999f };
static const float HVD_BG_A[17] = { 0.000f, 0.113f, 0.186f, 0.261f, 0.293f, 0.265f, 0.191f, 0.114f, 0.078f, 0.106f, 0.180f, 0.257f, 0.293f, 0.268f, 0.197f, 0.126f, 0.002f };
static const float HVD_BG_V[17] = { 0.000f, 0.109f, 0.469f, 1.485f, 1.655f, 1.720f, 1.796f, 1.846f, 1.998f, 2.376f, 2.847f, 3.201f, 3.439f, 3.668f, 3.938f, 4.170f, 4.270f };
static const float HVD_PIC_A[17] = { 1.000f, 0.774f, 0.627f, 0.478f, 0.413f, 0.470f, 0.618f, 0.772f, 0.845f, 0.788f, 0.640f, 0.486f, 0.414f, 0.463f, 0.605f, 0.747f, 0.997f };
static const float HVD_GHOST_A[17] = { 0.500f, 0.387f, 0.314f, 0.239f, 0.207f, 0.235f, 0.309f, 0.386f, 0.422f, 0.394f, 0.320f, 0.243f, 0.207f, 0.232f, 0.303f, 0.374f, 0.498f };
static const float HVD_RED_X[17] = { 5.657f, 5.658f, 5.717f, 5.737f, 5.760f, 5.675f, 5.679f, 5.742f, 5.770f, 5.783f, 5.682f, 5.683f, 5.733f, 5.742f, 5.756f, 5.657f, 5.644f };
static const float HVD_RED_Z[17] = { -0.208f, -0.106f, -0.083f, -0.134f, -0.189f, -0.166f, -0.094f, -0.078f, -0.147f, -0.225f, -0.196f, -0.122f, -0.111f, -0.158f, -0.216f, -0.182f, -0.085f };
static const float HVD_GREEN_X[17] = { 5.535f, 5.522f, 5.628f, 5.649f, 5.564f, 5.568f, 5.622f, 5.644f, 5.657f, 5.550f, 5.548f, 5.602f, 5.613f, 5.630f, 5.540f, 5.543f, 5.609f };
static const float HVD_GREEN_Z[17] = { -0.331f, -0.214f, -0.250f, -0.314f, -0.295f, -0.230f, -0.218f, -0.281f, -0.358f, -0.325f, -0.239f, -0.224f, -0.270f, -0.322f, -0.290f, -0.208f, -0.173f };
static const float HVD_END_S[9] = { 1.000f, 0.963f, 0.864f, 0.723f, 0.561f, 0.398f, 0.253f, 0.148f, 0.101f };
static const float HVD_END_FLASH[9] = { 0.000f, 0.348f, 0.608f, 0.775f, 0.843f, 0.806f, 0.657f, 0.392f, 0.003f };
static const float HVD_END_A[9] = { 1.000f, 0.958f, 0.848f, 0.692f, 0.512f, 0.331f, 0.170f, 0.053f, 0.001f };
static float hvd_s(const float *T, int n, float u) { u = fminf(1, fmaxf(0, u)) * (n - 1); int i = (int)u; if (i >= n - 1) return T[n - 1]; return T[i] + (T[i + 1] - T[i]) * (u - i); }
// b109: the panel's plane - across, up, toward the viewer (the forearm's read-out faces the view; the device's lies on its face)
static float hvd_ax[3] = { 1, 0, 0 }, hvd_az[3] = { 0, 1, 0 }, hvd_an[3] = { 0, 0, 1 };
static void hvd_pt(const float *C, float x, float z, float n, float *o) { for (int k = 0; k < 3; k++) o[k] = C[k] + hvd_ax[k] * x + hvd_az[k] * z + hvd_an[k] * n; }
static void hvd_quad(const float *C, float x0, float x1, float z0, float z1, float u0, float v0, float u1, float v1, const float *col) {
	float a[3], b[3], c[3], d[3]; hvd_pt(C, x0, z1, 0, a); hvd_pt(C, x1, z1, 0, b); hvd_pt(C, x1, z0, 0, c); hvd_pt(C, x0, z0, 0, d);
	sprite_quad(a, b, c, d, u0, v0, u1, v1, col);
}
static void hvd_spin(const float *C, float x, float z, float r, float ang, float u0, float v0, float u1, float v1, const float *col) {   // a square turned in the panel's plane
	float cs = cosf(ang) * r, sn = sinf(ang) * r, P[4][3]; static const float K[4][2] = { { -1, 1 }, { 1, 1 }, { 1, -1 }, { -1, -1 } };
	for (int k = 0; k < 4; k++) hvd_pt(C, x + K[k][0] * cs - K[k][1] * sn, z + K[k][0] * sn + K[k][1] * cs, 1.07f, P[k]);   // (1.07 cm before the panel)
	sprite_quad(P[0], P[1], P[2], P[3], u0, v0, u1, v1, col);
}
static int hvd_depth;   // b109: 1 = the depth test stays on (the device's panel: the fingers before it hide it)
static void hv_dev_draw(const float *PV, const float *C, float t, float te) {
	if (!hv_dev_tex || !sp_ok || sp_n || t < 0) return;
	float S = 1, fl = 0, A = 1, lp = -1;
	if (te >= 0) { if (te >= 0.0667f) return; float u = te / 0.0667f; S = hvd_s(HVD_END_S, 9, u); fl = hvd_s(HVD_END_FLASH, 9, u); A = hvd_s(HVD_END_A, 9, u); }
	else if (t < 0.1667f) { float u = t / 0.1667f; S = hvd_s(HVD_START_S, 9, u); fl = hvd_s(HVD_START_FLASH, 9, u); A = hvd_s(HVD_START_A, 9, u); }
	else lp = fmodf(t - 0.1667f, 1.0667f) / 1.0667f;
	const float U = 1.0f / 512, V = 1.0f / 256, hx = 11.05f * S, hz = 5.525f * S;
	if (!hvd_depth) glDisable(GL_DEPTH_TEST);
	{ const float k[4] = { 0, 0, 0, 0.5f }, w[4] = { 1, 1, 1, 1 };   // the backing and the outline (blended)
	  hvd_quad(C, -hx, hx, -hz, hz, 256.5f * U, 0.5f * V, 511.5f * U, 127.5f * V, k); hvd_quad(C, -hx, hx, -hz, hz, 256.5f * U, 128.5f * V, 511.5f * U, 255.5f * V, w);
	  sprite_flush(PV, hv_dev_tex, 0); }
	{ const float f[4] = { 1, 1, 1, 0.5f }; hvd_quad(C, -hx, hx, -hz, hz, 0.5f * U, 0.5f * V, 255.5f * U, 127.5f * V, f); }   // the fill
	if (lp >= 0) { float v = hvd_s(HVD_BG_V, 17, lp), c2[4] = { 1, 1, 1, hvd_s(HVD_BG_A, 17, lp) }; v -= floorf(v);   // the fill again, scrolled (wrapped: two parts)
		float zs = hz - 2 * hz * (1 - v), vs = (0.5f + 127 * v) * V;
		hvd_quad(C, -hx, hx, zs, hz, 0.5f * U, vs, 255.5f * U, 127.5f * V, c2); hvd_quad(C, -hx, hx, -hz, zs, 0.5f * U, 0.5f * V, 255.5f * U, vs, c2); }
	if (fl > 0.003f) { float c2[4] = { 1, 1, 1, fl }; hvd_quad(C, -hx, hx, -hz, hz, 256.5f * U, 0.5f * V, 511.5f * U, 127.5f * V, c2); }   // the flash as it opens and closes
	{ float ang = lp >= 0 ? lp * 6.2831853f : 0, pa = lp >= 0 ? hvd_s(HVD_PIC_A, 17, lp) : A, ga = lp >= 0 ? hvd_s(HVD_GHOST_A, 17, lp) : 0.5f * A, u = lp >= 0 ? lp : te >= 0 ? 1 : 0;
	  float rx = hvd_s(HVD_RED_X, 17, u), rz = hvd_s(HVD_RED_Z, 17, u), gx = hvd_s(HVD_GREEN_X, 17, u), gz = hvd_s(HVD_GREEN_Z, 17, u);
	  const float rb[4] = { 0, 0.45f, 1, A }, rr[4] = { 1, 0, 0, 0.5f * A }, rg[4] = { 0, 1, 0.07f, 0.5f * A }, pb[4] = { 0, 0.59f, 1, pa }, pr[4] = { 1, 0, 0, ga }, pg[4] = { 0, 1, 0.07f, ga };
	  hvd_spin(C, 5.657f, -0.208f, 2.587f, ang, 0.5f * U, 128.5f * V, 127.5f * U, 255.5f * V, rb); hvd_spin(C, rx, rz, 2.587f, ang, 0.5f * U, 128.5f * V, 127.5f * U, 255.5f * V, rr); hvd_spin(C, gx, gz, 2.587f, ang, 0.5f * U, 128.5f * V, 127.5f * U, 255.5f * V, rg);
	  hvd_spin(C, 5.657f, -0.208f, 1.617f, 0, 128.5f * U, 128.5f * V, 255.5f * U, 255.5f * V, pb); hvd_spin(C, rx, rz, 1.617f, 0, 128.5f * U, 128.5f * V, 255.5f * U, 255.5f * V, pr); hvd_spin(C, gx, gz, 1.617f, 0, 128.5f * U, 128.5f * V, 255.5f * U, 255.5f * V, pg); }
	sprite_flush(PV, hv_dev_tex, 1); glEnable(GL_DEPTH_TEST);
}
static void hv_load(const char *vdir) {
	static const char *const N[HVC_N] = { "skill", "install_start", "install_end", "clear_start", "clear_end", "nj_idle", "nj_run", "nj_sprint", "nj_in", "nj_fall" };
	char p[192]; if (hv_ok) return;
	snprintf(p, sizeof(p), "%svm_hands.gvm", vdir); if (model_load(&hv_m, p) < 0) { hv_ok = -1; logp("hands: no vm_hands.gvm - the skill gesture and the bomb in the hands are off (b99)\n"); return; }
	for (int k = 0; k < HVC_N; k++) hv_c[k] = model_find_clip(&hv_m, N[k]);
	hv_lh = model_find_bone(&hv_m, "Bip01_L_Hand");
	hv_rh = model_find_bone(&hv_m, "Bip01_R_Hand"); hv_rft = model_find_bone(&hv_m, "Bip01_R_ForeTwist"); hv_rfa = model_find_bone(&hv_m, "Bip01_R_Forearm");   // (b105: the forearm bone carries the folded blade's housing)
	if (hv_m.nb > 64 || hv_c[HVC_SKILL] < 0) { model_free(&hv_m); hv_ok = -1; return; }
	hv_ok = 1;
	snprintf(p, sizeof(p), "%sdem_bomb_1p.gvm", vdir); hv_bomb_ok = hv_lh >= 0 && hv_c[HVC_INST] >= 0 && model_load(&hv_bomb, p) == 0 && hv_bomb.nb <= 8;
	if (hv_bomb_ok) { snprintf(p, sizeof(p), "%sdem_fx.gtx", vdir); hv_fx_tex = sk_tex_load(p); logp("hands: the device's effects %s (dem_fx.gtx, b100)\n", hv_fx_tex ? "loaded" : "missing: the device without them"); }
	if (hv_c[HVC_CLEAR] >= 0) { snprintf(p, sizeof(p), "%sdem_dev.gtx", vdir); hv_dev_tex = sk_tex_load(p); logp("hands: the read-out over the forearm while defusing %s (dem_dev.gtx, b101)\n", hv_dev_tex ? "loaded" : "missing: the hands without it"); }
	logp("hands: vm_hands.gvm %d bones, skill gesture %.2f s, plant %.2f + %.2f s, the device %s (b99)\n", hv_m.nb, model_clip_len(&hv_m, hv_c[HVC_SKILL]),
		model_clip_len(&hv_m, hv_c[HVC_INST]), model_clip_len(&hv_m, hv_c[HVC_INST_END]), hv_bomb_ok ? "in the left hand" : "missing");
	if (hv_c[HVC_NJ_IDLE] >= 0 && hv_c[HVC_NJ_RUN] >= 0 && hv_c[HVC_NJ_SPRINT] >= 0) logp("hands: the Net Jump's open hands (nj_idle %.2f s, nj_run %.2f s, nj_sprint %.2f s - b121)\n", model_clip_len(&hv_m, hv_c[HVC_NJ_IDLE]), model_clip_len(&hv_m, hv_c[HVC_NJ_RUN]), model_clip_len(&hv_m, hv_c[HVC_NJ_SPRINT]));
	else logp("hands: an older vm_hands.gvm without the Net Jump's hands (b121): no hands in the network\n");
}
static int hv_nj_ok(void) { return hv_ok > 0 && hv_c[HVC_NJ_IDLE] >= 0 && hv_c[HVC_NJ_RUN] >= 0 && hv_c[HVC_NJ_SPRINT] >= 0; }
// b121: per frame, before hv_update. on: the local player is in the network (skills.h: the Net Jump runs); speed: over the
// ground, cm/s; grounded. The hands come up as it starts and are gone the frame it ends (the gun's draw clip follows).
static void hv_netjump(int on, float speed, int grounded, float dt) {
	if (!hv_nj_ok()) return;
	if (on && hv_mode != HV_NETJUMP) { hv_mode = HV_NETJUMP; hv_t = 0; for (int k = 0; k < 3; k++) hv_nj_ph[k] = hv_nj_w[k] = 0;
		if (!(hv_said & 8)) { hv_said |= 8; logp("  hands: the Net Jump - the open hands as holograms (b121)\n"); } }
	else if (!on && hv_mode == HV_NETJUMP) { hv_mode = HV_OFF; hv_nj_end = 1; }
	if (hv_mode != HV_NETJUMP) return;
	hv_t += dt; float k = fminf(1, dt * 9);
	float tg[3] = { grounded ? fminf(1, speed / 250.0f) : hv_nj_w[0], grounded ? fminf(1, fmaxf(0, (speed - 380.0f) / 110.0f)) : hv_nj_w[1], grounded ? 0 : 1 };
	for (int i = 0; i < 3; i++) hv_nj_w[i] += (tg[i] - hv_nj_w[i]) * k;
	hv_nj_ph[0] += dt; hv_nj_ph[1] += dt * fminf(1.3f, fmaxf(0.5f, speed / 335.0f)); hv_nj_ph[2] += dt;   // (the run clip is the game's 335 cm/s run)
}
static float hv_hold = -1;   // b105 test switch hvhold.txt (seconds): the skill gesture held at that time, for the pictures against the footage
static int hv_on(void) { return hv_ok > 0 && hv_mode != HV_OFF; }
static void hv_skill(void) { if (hv_ok > 0 && hv_mode == HV_OFF) { hv_mode = HV_SKILL; hv_t = 0; } }
// per frame. planting: the local player's plant progress (0..1), < 0 none; planted: the bomb is armed (the round's state).
// Returns 1 on the frame the hands are done and the gun comes back up (the caller plays its draw clip).
static int hv_update(float dt, float planting, int planted, float defusing, int alive) {
	if (hv_ok <= 0) return 0;
	if (!alive) { hv_mode = HV_OFF; hv_nj_end = 0; return 0; }
	if (hv_nj_end) { hv_nj_end = 0; if (hv_mode == HV_OFF) return 1; }   // b121: out of the network - the gun comes back up
	if (hv_mode == HV_NETJUMP) return 0;
	if (hv_hold >= 0) { hv_mode = HV_SKILL; hv_t = hv_hold; return 0; }
	if (planting > 0 && hv_bomb_ok && (hv_mode == HV_OFF || hv_mode == HV_SKILL)) { hv_mode = HV_PLANT; if (!(hv_said & 1)) { hv_said |= 1; logp("  hands: planting - the device in the left hand (b99)\n"); } }
	if (defusing > 0 && hv_c[HVC_CLEAR] >= 0 && (hv_mode == HV_OFF || hv_mode == HV_SKILL || hv_mode == HV_DEFEND)) { hv_mode = HV_DEFUSE; hv_t = 0;
		if (!(hv_said & 4)) { hv_said |= 4; logp("  hands: defusing - the fist, the open left hand and the read-out over the forearm (b101)\n"); } }
	switch (hv_mode) {
	case HV_SKILL: hv_t += dt; if (hv_t >= model_clip_len(&hv_m, hv_c[HVC_SKILL])) { hv_mode = HV_OFF; return 1; }
		if (!(hv_said & 2)) { hv_said |= 2; logp("  hands: the skill gesture (b99)\n"); } break;
	case HV_PLANT:
		if (planting > 0) { hv_t = fminf(1, planting) * model_clip_len(&hv_m, hv_c[HVC_INST]); break; }
		if (planted && hv_c[HVC_INST_END] >= 0) { hv_mode = HV_LETGO; hv_t = 0; break; }   // armed: the hands let go
		hv_mode = HV_OFF; return 1;                                                        // let go of the button: the gun again
	case HV_DEFUSE: hv_t += dt;   // clear_start, then its last frame held
		if (defusing > 0) break;
		if (hv_c[HVC_CLEAR_END] >= 0) { hv_mode = HV_DEFEND; hv_t = 0; break; }
		hv_mode = HV_OFF; return 1;
	case HV_DEFEND: hv_t += dt; if (hv_t >= model_clip_len(&hv_m, hv_c[HVC_CLEAR_END])) { hv_mode = HV_OFF; return 1; } break;
	case HV_LETGO: hv_t += dt; if (hv_t >= model_clip_len(&hv_m, hv_c[HVC_INST_END])) { hv_mode = HV_OFF; return 1; } break;
	default: break;
	}
	return 0;
}
// instead of the gun (after its depth clear). lit: the light at the player's spot (may be NULL); swx / swy: the guns' sway (cm).
static void hv_draw(const float *lit, float swx, float swy) {
	if (!hv_on()) return;
	static MBone P[64]; const Model *M = &hv_m; int plant = hv_mode != HV_SKILL;
	if (hv_mode == HV_NETJUMP) {   // b121: the Net Jump's open hands - the loops blended by her speed, the hand coming up first
		static MBone Q[64]; float PV[16], th = tanf(62.0f * 0.5f * 3.14159265f / 180), as = 960.0f / 544, n = 2, f = 400;
		model_sample(M, hv_c[HVC_NJ_IDLE], hv_nj_ph[0], P);
		if (hv_nj_w[0] > 0.01f) { model_sample(M, hv_c[HVC_NJ_RUN], hv_nj_ph[1], Q); model_blend(M->nb, P, Q, hv_nj_w[0], NULL); }
		if (hv_nj_w[1] > 0.01f) { model_sample(M, hv_c[HVC_NJ_SPRINT], hv_nj_ph[2], Q); model_blend(M->nb, P, Q, hv_nj_w[1], NULL); }
		if (hv_nj_w[2] > 0.01f && hv_c[HVC_NJ_FALL] >= 0) { model_sample(M, hv_c[HVC_NJ_FALL], hv_nj_ph[0], Q); model_blend(M->nb, P, Q, hv_nj_w[2], NULL); }
		float Lin = hv_c[HVC_NJ_IN] >= 0 ? model_clip_len(M, hv_c[HVC_NJ_IN]) : 0;
		if (hv_t < Lin) { model_sample(M, hv_c[HVC_NJ_IN], hv_t, Q); float w = fminf(1, fmaxf(0, (hv_t - (Lin - 0.2f)) / 0.2f)); model_blend(M->nb, Q, P, w, NULL); memcpy(P, Q, M->nb * sizeof(MBone)); }
		memset(PV, 0, sizeof(PV)); PV[0] = 1 / (th * as); PV[5] = 1 / th; PV[10] = (f + n) / (n - f); PV[11] = -1; PV[14] = 2 * f * n / (n - f);
		const float ox = 9.0f + swx * 0.5f, oy = 2.0f + swy * 0.5f, oz = -4.0f;   // (PV x a translation)
		PV[12] = PV[0] * ox; PV[13] = PV[5] * oy; PV[14] += PV[10] * oz; PV[15] = PV[11] * oz;
		vis_hands_holo(M, PV, P, fminf(1, hv_t / 0.15f));
		return; }
	static const int CL[7] = { HVC_SKILL, HVC_SKILL, HVC_INST, HVC_INST_END, HVC_CLEAR, HVC_CLEAR_END, HVC_SKILL };
	model_sample(M, hv_c[CL[hv_mode]], hv_mode == HV_DEFUSE ? fminf(hv_t, model_clip_len(M, hv_c[HVC_CLEAR])) : hv_t, P);
	float PV[16], th = tanf((plant ? 60.0f : 56.7f) * 0.5f * 3.14159265f / 180), as = 960.0f / 544, n = 2, f = 400;
	memset(PV, 0, sizeof(PV)); PV[0] = 1 / (th * as); PV[5] = 1 / th; PV[10] = (f + n) / (n - f); PV[11] = -1; PV[14] = 2 * f * n / (n - f);
	const int dfu = hv_mode == HV_DEFUSE || hv_mode == HV_DEFEND;   // (b101, the Demolition video 6:58-7:05: the fist low on the right at 4/5 of the width, the open left hand before it)
	// b105 (user, with screenshots of the gesture beside the footage: "the skill arm animation looks a bit off and misaligned ...
	// make sure the skill arm animation looks just like the footage we've previously reviewed"; "make sure the arm and everything
	// looks correct"): the footage's forearm wears the folded blade of the knife (the game's weapon/CW_CW/1st/
	// WP_CN_CW_NoActive.nif: the housing round the forearm, the linkage with its round pivot at the wrist) - it was missing here,
	// and b99 had fitted the lens to its upper edge taken for the forearm. vm_hands.gvm carries it now (vm/make_vm_weapon.py
	// CFG 'hands' 'fore': on the forearm bone where the knife's own clips hold the knife's Grip), and with it in the picture the
	// gesture is laid over the footage again (the Demolition video 7:24.5 and 7:24.9): a 56.7 deg lens, 2.3 cm left and 1.4 cm
	// up - the fist's outline, the pivot's ring and the hologram's centre all lie within 10 px of the footage's (of 960).
	float ox = (dfu ? -3.0f : plant ? 4.5f : -2.3f) + swx * 0.5f, oy = (dfu ? 1.0f : plant ? 5.4f : 1.4f) + swy * 0.5f;   // (PV x a translation: the fitted offsets, half the guns' sway)
	PV[12] = PV[0] * ox; PV[13] = PV[5] * oy;
	float tint[4] = { 1, 1, 1, 1 }; if (lit) for (int c = 0; c < 3; c++) tint[c] = fminf(1.4f, fmaxf(0.25f, lit[c] * 1.1f));
	// b105: the footage's arm is not the dull black it was here - its edges catch the light all the time (the glove and the
	// housing read silver-blue in the gesture, the plant and the defuse alike), and it turns pale blue while the hologram is up.
	// The model program's rim term (as the operators' outlines, md_rim) and a stronger highlight do the first; the second is
	// the scene's own light: the tint lifted toward its cyan from the moment the rings open until the hand drops.
	float rim0 = md_rim, spec0 = md_spec, glow = 0;
	if (hv_mode == HV_SKILL && hv_sc.ok) { float L = model_clip_len(M, hv_c[HVC_SKILL]); glow = fminf(1, fmaxf(0, (hv_t - 0.45f) / 0.25f)) * fminf(1, fmaxf(0, (L - hv_t) / 0.2f)); }
	tint[0] += 0.20f * glow; tint[1] += 0.45f * glow; tint[2] += 0.58f * glow; md_rim = 0.28f + 0.22f * glow; md_spec = 1.5f + 0.4f * glow;
	static const float ld[4] = { -0.35f, 0.75f, 0.56f, 0 }, nofog[4] = { 0, 0, 0, 0 }, sld[3] = { 0.15f, 1.0f, -0.25f };
	glDisable(GL_CULL_FACE); md_sld_dir = sld; md_fogp[3] = 2.0f;   // (the guns' highlight and sheen)
	model_draw_cpu(M, NULL, PV, P, ld, tint, nofog, 1);
	md_sld_dir = NULL; md_fogp[3] = 0; md_rim = rim0; md_spec = spec0;
	if (hv_mode == HV_PLANT && hv_bomb_ok) {   // the device: one bone = the left hand, half a turn round its z
		static MBone B[8]; const MBone *L = &P[hv_lh];
		for (int k = 0; k < 8; k++) { B[k].q[0] = L->q[1]; B[k].q[1] = -L->q[0]; B[k].q[2] = L->q[3]; B[k].q[3] = -L->q[2]; memcpy(B[k].p, L->p, sizeof(B[k].p)); }
		md_mesh0 = 0; md_mesh1 = 1; model_draw(&hv_bomb, PV, B, ld, tint, nofog); md_mesh0 = 0; md_mesh1 = 1 << 30;
		const float pt_ = hv_t / fmaxf(0.01f, model_clip_len(&hv_m, hv_c[HVC_INST])) * 5.0f;   // (hv_t runs the clip over the plant's 5 s)
		hv_fx_draw(PV, &B[0], pt_);
		// b109 (user, a PC screenshot of the plant: "make sure the bomb in demolition and it's screen look like the footage"): the device's
		// screen shows the whole read-out while it is being set - the white frame, the hexagon cells, the ring turning round the bomb's
		// picture (effect\kfm\DM_device_install: the panel b101 drew over the forearm for the defuse only; the plant had a blinking
		// outline). Here it lies on the device's face (22.1 x 11.05 cm = the display's two bands), 3 cm before it, from the device being up.
		if (pt_ > 0.6f && hv_dev_tex) { const MBone *Bd = &B[0]; float x = Bd->q[0], y = Bd->q[1], z = Bd->q[2], w = Bd->q[3], R[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) }, { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) }, { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
			float D[3][4], C[3]; for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) D[r][c] = R[r][0] * HVF_M[0][c] + R[r][1] * HVF_M[1][c] + R[r][2] * HVF_M[2][c] + (c == 3 ? Bd->p[r] : 0);
			hvf_pt(D, 0.5f * (HVF_BX0 + HVF_BX1), -3.045f, 0.356f, C);
			for (int k = 0; k < 3; k++) { hvd_ax[k] = D[k][0]; hvd_az[k] = D[k][2]; hvd_an[k] = -D[k][1]; } hvd_depth = 1;
			hv_dev_draw(PV, C, pt_ - 0.6f, -1);
			hvd_ax[0] = 1; hvd_ax[1] = hvd_ax[2] = 0; hvd_az[1] = 1; hvd_az[0] = hvd_az[2] = 0; hvd_an[2] = 1; hvd_an[0] = hvd_an[1] = 0; hvd_depth = 0; }
	}
	if (hv_mode == HV_SKILL && hv_sc.ok && hv_rft >= 0) {   // b102: the gesture's hologram, in the forearm's space
		// b105: the scene's space is the folded blade's own (the housing's): the forearm bone turned 49.0 deg about its length and
		// 0.95 cm off it (the knife's Grip, vm/make_vm_weapon.py FORE_Q / FORE_T) - there the rings gather over the housing's
		// side just behind the pivot, as in the footage (centre within 4 px of 960 in both frames). An older vm_hands.gvm
		// without the forearm bone: the fore-twist bone's axis with the hand's roll, as before.
		float PVM[16], Mb[16];
		if (hv_rfa >= 0) { const float *q = P[hv_rfa].q, *t = P[hv_rfa].p; float x = q[0], y = q[1], z = q[2], w = q[3];
			const float X[3] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w) }, Y[3] = { 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w) },
				Z[3] = { 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y) }, cs = 0.65594f, sn = 0.75481f;   // (cos / sin 49.008 deg)
			for (int k = 0; k < 3; k++) { Mb[k] = X[k]; Mb[4 + k] = Y[k] * cs + Z[k] * sn; Mb[8 + k] = Z[k] * cs - Y[k] * sn; Mb[12 + k] = t[k] - Y[k] * 0.95377f; }
			Mb[3] = Mb[7] = Mb[11] = 0; Mb[15] = 1;
		} else {
		const float *q = P[hv_rft].q, *t = P[hv_rft].p; float x = q[0], y = q[1], z = q[2], w = q[3];
		float X[3] = { 1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w) }, Y[3] = { 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w) }, Z[3];
		if (hv_rh >= 0) { const float *h = P[hv_rh].q; float hx = h[0], hy = h[1], hz = h[2], hw = h[3], H[3] = { 2 * (hx * hy - hz * hw), 1 - 2 * (hx * hx + hz * hz), 2 * (hy * hz + hx * hw) };
			float d = H[0] * X[0] + H[1] * X[1] + H[2] * X[2], l = 0; for (int k = 0; k < 3; k++) { H[k] -= d * X[k]; l += H[k] * H[k]; }
			if (l > 1e-4f) { l = 1 / sqrtf(l); for (int k = 0; k < 3; k++) Y[k] = H[k] * l; } }
		Z[0] = X[1] * Y[2] - X[2] * Y[1]; Z[1] = X[2] * Y[0] - X[0] * Y[2]; Z[2] = X[0] * Y[1] - X[1] * Y[0];
		for (int k = 0; k < 3; k++) { Mb[k] = X[k]; Mb[4 + k] = Y[k]; Mb[8 + k] = Z[k]; Mb[12 + k] = t[k] + X[k] * 3.0f + Y[k] * 3.4f; }
		Mb[3] = Mb[7] = Mb[11] = 0; Mb[15] = 1; }
		fxs_mul(PVM, PV, Mb); fxs_draw(&hv_sc, hv_t, PVM, 1); glDisable(GL_CULL_FACE); }
	if (hv_mode == HV_DEFUSE) hv_dev_was = hv_t > 0.5f;   // b101: the read-out over the forearm (it opens as the fist comes into the picture, and closes as the hands leave)
	if (dfu && hv_rh >= 0 && hv_dev_was) { const float *c = P[hv_rh].p, C[3] = { c[0] + 5.5f, c[1] - 2.1f, c[2] + 2.8f }; hv_dev_draw(PV, C, hv_mode == HV_DEFEND ? 1 : hv_t - 0.5f, hv_mode == HV_DEFEND ? hv_t : -1); }
	glEnable(GL_CULL_FACE);
}

// b101 (the user: the bomb device "with all it's animations and effects"): the planted bomb's own parts, from the game's
// worldEnv / EnvBomb / bomb_box.nif (the port drew the device and its scan-line shell only). In the bomb's space (x along it, -y out of the wall,
// z up from its foot; cm), t = seconds: the display red at half strength with two white bands glowing over it (seen through the
// face's two openings: the white-pink glow of the footage), the knob's lamp pulsing every 2 s, the lamp's flare blinking once a
// second (a 6.5 m quad of lenz01), a thin red beam out of the lamp flickering 7.5 times a second (dm_up_light on a
// 4 x 116 cm strip turned to the viewer), and the red diamond round it all (arrow.dds, 75 cm, facing the viewer). Mb: the
// bomb's matrix (column-major), eye: the camera. (The device's red pulse - bomb_02 - is main.c's second draw of the device.)
static void hv_wpt(const float *Mb, float x, float y, float z, float *o) { for (int r = 0; r < 3; r++) o[r] = Mb[r] * x + Mb[4 + r] * y + Mb[8 + r] * z + Mb[12 + r]; }
static void hv_wquad(const float *Mb, float x0, float x1, float y, float z0, float z1, float u0, float v0, float u1, float v1, const float *col) {
	float a[3], b[3], c[3], d[3]; hv_wpt(Mb, x0, y, z1, a); hv_wpt(Mb, x1, y, z1, b); hv_wpt(Mb, x1, y, z0, c); hv_wpt(Mb, x0, y, z0, d); sprite_quad(a, b, c, d, u0, v0, u1, v1, col);
}
static void hv_wbill(const float *c, const float *eye, float hw, float hh, float push, float u0, float v0, float u1, float v1, const float *col) {
	float v[3] = { eye[0] - c[0], eye[1] - c[1], eye[2] - c[2] }, l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l < 1) return; for (int k = 0; k < 3; k++) v[k] /= l;
	float r[3] = { -v[1], v[0], 0 }, rl = sqrtf(r[0] * r[0] + r[1] * r[1]); if (rl < 1e-3f) return; r[0] /= rl; r[1] /= rl;
	float u[3] = { v[1] * r[2] - v[2] * r[1], v[2] * r[0] - v[0] * r[2], v[0] * r[1] - v[1] * r[0] }, p[4][3];
	for (int k = 0; k < 3; k++) { float m = c[k] + v[k] * push; p[0][k] = m - r[k] * hw + u[k] * hh; p[1][k] = m + r[k] * hw + u[k] * hh; p[2][k] = m + r[k] * hw - u[k] * hh; p[3][k] = m - r[k] * hw - u[k] * hh; }
	sprite_quad(p[0], p[1], p[2], p[3], u0, v0, u1, v1, col);
}
static void hv_bomb_world(const float *VP, const float *Mb, const float *eye, float t) {
	if (!hv_fx_tex || !sp_ok || sp_n) return; const float T = 1.0f / 512;
	{ const float red[4] = { 1, 0, 0, 0.5f }; hv_wquad(Mb, -18.36f, 11.16f, -2.6f, 5.0f, 24.2f, 78 * T, 394 * T, 82 * T, 398 * T, red); sprite_flush(VP, hv_fx_tex, 0); }
	{ const float wh[4] = { 1, 1, 1, 0.5f }; hv_wquad(Mb, -17.6f, 10.4f, 0, 5.0f, 12.0f, 78 * T, 394 * T, 82 * T, 398 * T, wh); hv_wquad(Mb, -17.6f, 10.4f, 0, 17.1f, 24.1f, 78 * T, 394 * T, 82 * T, 398 * T, wh);
	  float ph = fmodf(t, 2.0f), lc[4] = { 1, 0.05f, 0, 0.5f * (1 - fabsf(ph - 1)) }; hv_wquad(Mb, 17.2f, 21.2f, -6.3f, 1.9f, 5.9f, 78 * T, 394 * T, 82 * T, 398 * T, lc);
	  sprite_flush(VP, hv_fx_tex, 1); }
	{ float ph = fmodf(t, 1.0f), al = ph > 0.43f && ph < 0.57f ? 1 - fabsf(ph - 0.5f) / 0.07f : 0, c[3];   // the flare's blink
	  if (al > 0) { float col[4] = { 1, 1, 1, al }; hv_wpt(Mb, 19.2f, -17.4f, 3.9f, c); hv_wbill(c, eye, 325, 325, 0, 1 * T, 321 * T, 63 * T, 383 * T, col); } }
	{ float ph = fmodf(t, 0.1333f) / 0.1333f, col[4] = { 1, 1, 1, 0.5f * (1 - fabsf(ph * 2 - 1)) }, p0[3], p1[3];   // the beam: a strip along the bomb's -y, turned to the eye
	  hv_wpt(Mb, 19.2f, -6.3f, 3.9f, p0); hv_wpt(Mb, 19.2f, -104.7f, 3.9f, p1);
	  float ax[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] }, ev[3] = { eye[0] - p0[0], eye[1] - p0[1], eye[2] - p0[2] };
	  float w[3] = { ax[1] * ev[2] - ax[2] * ev[1], ax[2] * ev[0] - ax[0] * ev[2], ax[0] * ev[1] - ax[1] * ev[0] }, wl = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
	  if (wl > 1e-3f) { float a[3], b[3], c[3], d[3]; for (int k = 0; k < 3; k++) { w[k] *= 2.08f / wl; a[k] = p0[k] + w[k]; b[k] = p1[k] + w[k]; c[k] = p1[k] - w[k]; d[k] = p0[k] - w[k]; }
		sprite_quad(a, b, c, d, 190 * T, 321 * T, 65 * T, 351 * T, col); } }
	sprite_flush(VP, hv_fx_tex, 1);
	{ const float col[4] = { 1, 1, 1, 1 }; float c[3]; hv_wpt(Mb, -2.8f, 0, 14.2f, c); hv_wbill(c, eye, 37.5f, 37.5f, 30, 385 * T, 385 * T, 511 * T, 511 * T, col); sprite_flush(VP, hv_fx_tex, 0); }
}
