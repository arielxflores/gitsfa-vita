// Block IMA ADPCM used by the Vita audio mixer (engine/src/audio.h).
// Block = per channel: s16 first sample, u8 step index, u8 0, then (BF-1) 4-bit codes (low nibble first), BF/2 bytes.
// Stereo blocks are stored channel after channel. Encoder restarts its predictor from the exact first sample of each block.
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
static const int STEP[89] = {7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int IDX[16] = {-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8};
static inline int dec1(int *pred, int *idx, int nib) {
	int st = STEP[*idx], diff = st >> 3;
	if (nib & 4) diff += st; if (nib & 2) diff += st >> 1; if (nib & 1) diff += st >> 2;
	if (nib & 8) diff = -diff;
	int p = *pred + diff; if (p > 32767) p = 32767; if (p < -32768) p = -32768; *pred = p;
	int i = *idx + IDX[nib]; if (i < 0) i = 0; if (i > 88) i = 88; *idx = i; return p;
}
static int LOOK = 2;   // samples of lookahead in the encoder search (0 = standard nearest code)
static inline int greedy_err(int pred, int idx, const int *x, int n) {   // squared error of standard encoding of the next n samples
	long long e = 0;
	for (int k = 0; k < n; k++) {
		int st = STEP[idx], d = x[k] - pred, nib = 0; if (d < 0) { nib = 8; d = -d; }
		int m = 4, s = st; for (int b = 2; b >= 0; b--) { if (d >= s) { nib |= m; d -= s; } s >>= 1; m >>= 1; }
		int v = dec1(&pred, &idx, nib); e += (long long)(v - x[k]) * (v - x[k]);
		if (e > (1LL << 40)) break;
	}
	return e > 0x7fffffff ? 0x7fffffff : (int)e;
}
static inline int enc_look(int *pred, int *idx, const int *x, int nlook) {
	int best = 0, bp = 0, bi = 0; long long be = -1;
	for (int nib = 0; nib < 16; nib++) {
		int p = *pred, i = *idx; int v = dec1(&p, &i, nib); long long e = (long long)(v - x[0]) * (v - x[0]);
		if (be >= 0 && e >= be) continue;
		if (nlook > 0) e += greedy_err(p, i, x + 1, nlook);
		if (be < 0 || e < be) { be = e; best = nib; bp = p; bi = i; }
	}
	*pred = bp; *idx = bi; return best;
}
void adpcm_set_look(int n) { LOOK = n; }
static inline int enc1(int *pred, int *idx, int x) {
	// try the standard code and its neighbours, keep the closest (cheap 1-step search)
	int st = STEP[*idx], d = x - *pred, nib = 0;
	if (d < 0) { nib = 8; d = -d; }
	int m = 4, s = st; for (int b = 2; b >= 0; b--) { if (d >= s) { nib |= m; d -= s; } s >>= 1; m >>= 1; }
	int best = nib, bp = 0, bi = 0, be = 1 << 30;
	for (int k = 0; k < 3; k++) {
		int cand = k == 0 ? nib : (k == 1 ? ((nib & 8) | ((nib & 7) > 0 ? (nib & 7) - 1 : 0)) : ((nib & 8) | ((nib & 7) < 7 ? (nib & 7) + 1 : 7)));
		int p = *pred, i = *idx; int v = dec1(&p, &i, cand); int e = abs(v - x);
		if (e < be) { be = e; best = cand; bp = p; bi = i; }
	}
	*pred = bp; *idx = bi; return best;
}
int adpcm_block_bytes(int bf) { return 4 + bf / 2; }
// in: interleaved s16 (n frames, ch channels). out size: nblk * ch * block_bytes. returns nblk
int adpcm_encode(const int16_t *in, int n, int ch, int bf, uint8_t *out) {
	int bb = adpcm_block_bytes(bf), nblk = (n + bf - 1) / bf; int idx[2] = {0, 0};
	for (int b = 0; b < nblk; b++) for (int c = 0; c < ch; c++) {
		uint8_t *o = out + ((size_t)b * ch + c) * bb; memset(o, 0, bb);
		int f0 = b * bf; int s0 = f0 < n ? in[(size_t)f0 * ch + c] : 0;
		if (b == 0) { int d = n > 1 ? abs(in[ch + c] - s0) : 0; int i = 0; while (i < 88 && STEP[i] < d / 2) i++; idx[c] = i; }
		o[0] = s0 & 255; o[1] = (s0 >> 8) & 255; o[2] = idx[c]; o[3] = 0;
		int pred = s0, ix = idx[c];
		for (int k = 1; k < bf; k++) {
			int f = f0 + k; int x = f < n ? in[(size_t)f * ch + c] : pred; int nib;
			if (LOOK > 0) { int xs[9]; int nl = LOOK < 8 ? LOOK : 8; for (int j = 0; j <= nl; j++) { int ff = f + j; xs[j] = (ff < n && k + j < bf) ? in[(size_t)ff * ch + c] : (j ? xs[j - 1] : x); }
				nib = enc_look(&pred, &ix, xs, (bf - 1 - k) < nl ? (bf - 1 - k) : nl); }
			else nib = enc1(&pred, &ix, x);
			int q = k - 1; o[4 + q / 2] |= (q & 1) ? (nib << 4) : nib;
		}
		idx[c] = ix;
	}
	return nblk;
}
int adpcm_decode(const uint8_t *in, int nblk, int ch, int bf, int16_t *out) {
	int bb = adpcm_block_bytes(bf);
	for (int b = 0; b < nblk; b++) for (int c = 0; c < ch; c++) {
		const uint8_t *p = in + ((size_t)b * ch + c) * bb;
		int pred = (int16_t)(p[0] | (p[1] << 8)), ix = p[2]; if (ix > 88) ix = 88;
		out[((size_t)b * bf) * ch + c] = pred;
		for (int k = 1; k < bf; k++) { int q = k - 1; int nib = (p[4 + q / 2] >> ((q & 1) * 4)) & 15;
			out[((size_t)b * bf + k) * ch + c] = dec1(&pred, &ix, nib); }
	}
	return nblk * bf;
}
