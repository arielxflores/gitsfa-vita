// b107: the rooms' reverb (the user, about the PC's shooting range: "sounds like its supposed to have more of an echo in
// the room ... while they're reloading shooting and such").
//
// What the PC does (sac.fev, tools/audio/fevx/fevlib.py reverb_defs): the FMOD Designer project holds 81 reverb definitions
// named like the ambience zones (shooting_range, train_station_outside, urban_inside ...), each an I3DL2 property set
// (room / reflections / reverb levels in millibels, decay time, HF ratio, delays, diffusion, density), and EVERY event has
// its own wet level: gunshots -16..-18 dB, reloads -12, pistol handling -8..-10, shell casings -8, footsteps -13.5,
// voices -13.6, the interface and the music none (-60). The showcase video of the range shows it on every mechanical
// click: after a magazine goes in, the room rings on about 21 dB under the click and dies away over the definition's
// 2.5 s, where the sample alone is 35-45 dB down within 0.2 s (tools/audio/ana/reverb_fit.py). The gunshots carry
// their own 2.2 s tails in the samples and get little on top - which is why only the handling sounded dry here.
//
// This file is that reverb for the port's mixer (audio.h): the voices and voice lines add their wet share to a mono bus
// (rvb[]); once per grain the bus is halved to 24 kHz, shaped by the definition's HF / LF room filters, and feeds
//   - the early reflections: six taps after the reflections delay (three to each ear), and
//   - the late field: two allpass diffusers, then an eight-line feedback delay network (Householder feedback, one-pole
//     damping per line for the HF decay ratio) whose loop gains give the decay time; two sign patterns give the ears
//     uncorrelated sums;
// the result is brought back to 48 kHz and added to the mix. A map has two definitions, its outdoor and its indoor zone
// (events.gev, the 'GRV1' chunk: tools/audio/make_events.py); levels, decay and damping follow the indoor probe's glide
// (snd_indoor) and each definition keeps its own delays (two tap sets, crossfaded).
// Level law: the late field's energy = the wet signal's energy x room x reverb (I3DL2's reading; the network's own
// energy gain, 64 / (sum(len) x (1 - loop gain^2)) per ear, is divided out), then RV_LATE / RV_EARLY, fitted to the video.
// Cost: 15 us per 21 ms grain on the dev host, about 0.3 ms on the Vita's audio core (the log's "mix ... us" shows the
// whole mixer's worst grain); nothing runs while
// the bus has been silent for longer than the tail (menus, quiet moments), or with the test switch ux0:data/gitsfa/noreverb.
#pragma once
#include <math.h>
#include <string.h>
#define RV_FS 24000.f
#define RV_N 8
#define RV_LINE 2048          // samples per line (85 ms at 24 kHz)
#define RV_PRE 8192           // the input's delay line (341 ms: reflections delay + reverb delay + the taps)
#ifndef RV_LATE
#define RV_LATE 1.3f          // fitted to the video (reverb_fit.py: eight reload clicks of the showcase video; 1.0 = the law above, 2.3 dB under the video)
#define RV_EARLY 1.0f
#define RV_DHF(x) (x)
#endif
typedef struct { char name[28]; float room, room_hf, room_lf, decay, decay_hf, refl, refl_delay, reverb, reverb_delay, diffusion, density, hf_ref, lf_ref; } RevDef;
static struct {
	float dl[RV_N][RV_LINE], lp[RV_N], pre[RV_PRE], ap[2][512];
	int len[RV_N], w, pw, apw[2], apl[2];
	float hf_s, lf_s, pl, pr;                       // room filter states; the last output pair (48 kHz interpolation)
	float g[RV_N], a[RV_N], hf_g, hf_a, lf_g, lf_a, k_late[2], k_early[2], apc;
	int late_d[2], tap_d[2][6];
	float w_in, w_set; int quiet, cfg_seq; RevDef d[2]; float tail;
} rv;
static RevDef rv_pend[2][2]; static volatile int rv_seq = 0, rv_on = 0; static volatile float rv_indoor = 0;
static int rv_disabled = 0;   // test switch (main.c: noreverb)
// game thread: the map's two definitions (outdoor, indoor); NULL = no reverb (menus)
static void reverb_set(const RevDef *out, const RevDef *in) {
	if (!out || rv_disabled) { __atomic_store_n(&rv_on, 0, __ATOMIC_RELEASE); return; }
	int s = rv_seq + 1; rv_pend[s & 1][0] = *out; rv_pend[s & 1][1] = in ? *in : *out;
	__atomic_store_n(&rv_seq, s, __ATOMIC_RELEASE); __atomic_store_n(&rv_on, 1, __ATOMIC_RELEASE);
}
static float rv_mb(float mb) { return powf(10.f, mb / 2000.f); }
static float rv_onepole(float hz) { float a = expf(-6.2831853f * hz / RV_FS); return a < 0 ? 0 : a > 0.9999f ? 0.9999f : a; }
// audio thread: a new pair of definitions: line lengths (the density sets the room's size), cleared state
static void rv_config(void) {
	static const float MS[RV_N] = { 29.7f, 37.1f, 41.1f, 43.7f, 47.9f, 53.3f, 59.9f, 67.1f };
	static const int PRIME[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37 };
	int s = __atomic_load_n(&rv_seq, __ATOMIC_ACQUIRE); rv.d[0] = rv_pend[s & 1][0]; rv.d[1] = rv_pend[s & 1][1]; rv.cfg_seq = s;
	float dens = 0.005f * (rv.d[0].density + rv.d[1].density), sc = 0.35f + 0.65f * (dens < 0 ? 0 : dens > 1 ? 1 : dens);
	for (int i = 0; i < RV_N; i++) {
		int n = (int)(MS[i] * sc * RV_FS / 1000.f) | 1;
		for (;; n += 2) {   // no common factor with the lines before it (their echoes never line up)
			int ok = 1; for (int j = 0; j < i && ok; j++) for (unsigned p = 1; p < sizeof(PRIME) / sizeof(PRIME[0]); p++) if (n % PRIME[p] == 0 && rv.len[j] % PRIME[p] == 0) { ok = 0; break; }
			if (ok) break; }
		rv.len[i] = n < RV_LINE - 1 ? n : RV_LINE - 1;
	}
	rv.apl[0] = 113; rv.apl[1] = 173;
	memset(rv.dl, 0, sizeof(rv.dl)); memset(rv.lp, 0, sizeof(rv.lp)); memset(rv.pre, 0, sizeof(rv.pre)); memset(rv.ap, 0, sizeof(rv.ap));
	rv.hf_s = rv.lf_s = rv.pl = rv.pr = 0; rv.w_set = -1; rv.quiet = 1 << 20;
	for (int k = 0; k < 2; k++) {
		static const float TAP[6] = { 0, 4.3f, 9.7f, 15.1f, 21.7f, 29.3f };
		float rd = rv.d[k].refl_delay * 1000.f;
		for (int t = 0; t < 6; t++) { int n = (int)((rd + TAP[t] * sc) * RV_FS / 1000.f); rv.tap_d[k][t] = n < 1 ? 1 : n > RV_PRE - 2 ? RV_PRE - 2 : n; }
		int n = (int)((rv.d[k].refl_delay + rv.d[k].reverb_delay) * RV_FS); rv.late_d[k] = n < 1 ? 1 : n > RV_PRE - 2 ? RV_PRE - 2 : n;
	}
}
// the coefficients for the blend w (0 = the outdoor definition, 1 = the indoor one)
static void rv_coeffs(float w) {
	const RevDef *A = &rv.d[0], *B = &rv.d[1]; float u = 1 - w;
	float decay = u * A->decay + w * B->decay, dhf = RV_DHF(u * A->decay_hf + w * B->decay_hf), hfref = u * A->hf_ref + w * B->hf_ref, lfref = u * A->lf_ref + w * B->lf_ref;
	if (decay < 0.1f) decay = 0.1f;
	if (dhf < 0.1f) dhf = 0.1f;
	float ch = cosf(6.2831853f * (hfref < 100 ? 100 : hfref > 11000 ? 11000 : hfref) / RV_FS), sum = 0;
	for (int i = 0; i < RV_N; i++) {
		float g = powf(10.f, -3.f * rv.len[i] / (decay * RV_FS)), gh = powf(10.f, -3.f * rv.len[i] / (decay * dhf * RV_FS)), a = 0;
		if (gh < g * 0.999f) {   // one pole y = (1-a) x + a y: |H| at the HF reference = gh / g
			float r = gh / g, r2 = r * r, bq = (2 * r2 * ch - 2) / (1 - r2), disc = bq * bq - 4;   // a^2 (1 - r2) + a (2 r2 ch - 2) + (1 - r2) = 0
			a = disc > 0 ? (-bq - sqrtf(disc)) * 0.5f : 0; if (!(a > 0)) a = 0;
			if (a > 0.98f) a = 0.98f;
		}
		rv.g[i] = g; rv.a[i] = a; sum += rv.len[i];
	}
	float e = 64.f / (sum * (1 - powf(10.f, -6.f / (decay * RV_FS))));   // the network's energy gain per ear: 8 inputs x 8 taps over the lines' total length, summed over the decay
	float kn = 1.f / sqrtf(e);
	rv.k_late[0] = u * rv_mb(A->room + A->reverb) * kn * RV_LATE; rv.k_late[1] = w * rv_mb(B->room + B->reverb) * kn * RV_LATE;
	rv.k_early[0] = u * rv_mb(A->room + A->refl) * RV_EARLY; rv.k_early[1] = w * rv_mb(B->room + B->refl) * RV_EARLY;
	rv.hf_g = u * rv_mb(A->room_hf) + w * rv_mb(B->room_hf); rv.hf_a = rv_onepole(hfref);
	rv.lf_g = u * rv_mb(A->room_lf) + w * rv_mb(B->room_lf); rv.lf_a = rv_onepole(lfref < 20 ? 20 : lfref);
	rv.apc = 0.006f * (u * A->diffusion + w * B->diffusion);
	rv.tail = decay * 1.3f + 0.6f; rv.w_set = w;
}
// one grain: bus[n] (the wet sends at 48 kHz, n even) -> added to out[2 n] (stereo, the mixer's integer scale)
static void reverb_grain(const int *bus, int *out, int n) {
	if (!__atomic_load_n(&rv_on, __ATOMIC_ACQUIRE)) return;
	if (rv.cfg_seq != __atomic_load_n(&rv_seq, __ATOMIC_ACQUIRE)) rv_config();
	int any = 0; for (int k = 0; k < n; k++) any |= bus[k];
	if (any) rv.quiet = 0; else if (rv.quiet * (n / 48000.f) > rv.tail) return; else rv.quiet++;   // (the tail has died away: nothing to do)
	float w = rv_indoor; w = w < 0 ? 0 : w > 1 ? 1 : w;
	if (fabsf(w - rv.w_set) > 0.01f || (rv.w_set != w && (w == 0 || w == 1))) rv_coeffs(w);
	static const float EG[6] = { 0.72f, -0.62f, 0.52f, -0.50f, 0.46f, -0.38f };   // (each ear's three: unit energy)
	static const float SL[RV_N] = { 1, 1, 1, 1, -1, -1, -1, -1 }, SR[RV_N] = { 1, -1, 1, -1, 1, -1, 1, -1 };
	const float ke0 = rv.k_early[0], ke1 = rv.k_early[1], kl = rv.k_late[0] + rv.k_late[1], apc = rv.apc;
	const float wl0 = kl > 0 ? rv.k_late[0] / kl : 1, wl1 = 1 - wl0;
	float pl = rv.pl, pr = rv.pr;
	for (int k = 0; k < n; k += 2) {
		float x = (float)(bus[k] + bus[k + 1]) * 0.5f;
		// the room's filters: highs (room_hf above the HF reference), lows (room_lf below the LF reference)
		rv.hf_s = x + (rv.hf_s - x) * rv.hf_a; x = rv.hf_s + (x - rv.hf_s) * rv.hf_g;
		rv.lf_s = x + (rv.lf_s - x) * rv.lf_a; x = x - rv.lf_s * (1 - rv.lf_g);
		int pw = rv.pw; rv.pre[pw] = x; rv.pw = (pw + 1) & (RV_PRE - 1);
#define RV_TAP(d) rv.pre[(pw - (d)) & (RV_PRE - 1)]
		float l = 0, r = 0;
		if (ke0 > 0) { const int *T = rv.tap_d[0]; l += ke0 * (EG[0] * RV_TAP(T[0]) + EG[2] * RV_TAP(T[2]) + EG[4] * RV_TAP(T[4])); r += ke0 * (EG[1] * RV_TAP(T[1]) + EG[3] * RV_TAP(T[3]) + EG[5] * RV_TAP(T[5])); }
		if (ke1 > 0) { const int *T = rv.tap_d[1]; l += ke1 * (EG[0] * RV_TAP(T[0]) + EG[2] * RV_TAP(T[2]) + EG[4] * RV_TAP(T[4])); r += ke1 * (EG[1] * RV_TAP(T[1]) + EG[3] * RV_TAP(T[3]) + EG[5] * RV_TAP(T[5])); }
		float in = wl0 * RV_TAP(rv.late_d[0]) + wl1 * RV_TAP(rv.late_d[1]);
#undef RV_TAP
		for (int j = 0; j < 2; j++) {   // diffusion: two allpasses
			int aw = rv.apw[j]; float *b = rv.ap[j], d = b[aw], v = in + apc * d; b[aw] = v; in = d - apc * v; if (++aw >= rv.apl[j]) aw = 0; rv.apw[j] = aw; }
		float z[RV_N], s = 0, ll = 0, rr = 0; int fw = rv.w;
		for (int i = 0; i < RV_N; i++) {
			float y = rv.dl[i][(fw - rv.len[i]) & (RV_LINE - 1)];
			rv.lp[i] = y + (rv.lp[i] - y) * rv.a[i]; z[i] = rv.lp[i] * rv.g[i]; s += z[i]; ll += SL[i] * y; rr += SR[i] * y;
		}
		s *= 2.f / RV_N; in += 1e-9f;   // (never a run of denormals)
		static const float SB[RV_N] = { 1, -1, -1, 1, 1, -1, -1, 1 };   // (the input's signs: orthogonal to both ears' patterns)
		for (int i = 0; i < RV_N; i++) rv.dl[i][fw] = SB[i] * in + z[i] - s;
		rv.w = (fw + 1) & (RV_LINE - 1);
		l += kl * ll; r += kl * rr;
		out[k * 2] += (int)((pl + l) * 0.5f); out[k * 2 + 1] += (int)((pr + r) * 0.5f); out[k * 2 + 2] += (int)l; out[k * 2 + 3] += (int)r;
		pl = l; pr = r;
	}
	rv.pl = pl; rv.pr = pr;
}
