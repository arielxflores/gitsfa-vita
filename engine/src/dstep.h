// dstep.h (perf b45): display-locked game steps for matches (the technique of Grarak's MH3 Tri Vita port, docs
// rendering/presentation.md "display-locked steps"; no code taken).
//
// At a locked 60 the frames reach the screen one VBlank apart, but the step measured with the CPU clock between frame
// starts jitters with the display queue's waits (the swap returns a little before or after a VBlank): a camera turn moves
// unevenly on screen while the game's own values are smooth. Here a match frame's step is whole VBlanks instead: one,
// plus the VBlanks the display held the previous picture because a frame came late (sceDisplayGetVcount right after each
// swap), paid back over the next frames, so a late frame does not jolt the next one: each frame pays the recent rate of extra
// VBlanks (a running mean, a quarter per frame: a steady 30 or 40 fps steps evenly at 2 or 1.5 VBlanks) plus an eighth of
// what is owed beyond it (more than 4 left owed: all at once). The VBlank's length is measured: the CPU clock over the
// VBlanks of the last ~2 s.
// Off (the CPU clock step as before): menus, loading screens, the first frames after a load or a pause, vsync off
// (frametime.h's probe windows), until the VBlank length is known. The host build's VBlanks follow its virtual clock (one
// per swap), so its steps are the clock's.
// Per second (verbose runs): "step:" line, how far each frame's game step was from its flip's interval (VBlanks shown x
// VBlank length), for the display step and for what the clock step would have been.
#pragma once
#include <stdint.h>
#include <math.h>

static struct {
	int on, warm;                  // on: steps from VBlanks; warm: frames since the last reset (the first ones use the clock)
	unsigned v_prev; uint64_t t_prev;   // the VBlank count and clock (us) after the previous swap
	float owed, rate;              // VBlanks the display held a picture longer than one, not paid back yet; their recent rate per frame
	int n_last;                    // VBlanks between the last two swaps
	unsigned pv0; uint64_t pt0; double period_us;   // VBlank length: measured over a window
	// per-second stats
	unsigned frames, late; float owed_max; double err_ds, err_clk, err_ds_max, err_clk_max, step_sum;
} DS = {
#ifdef HG_HOST
	.period_us = 16667.0   // (the host's virtual clock: one VBlank per swap, 16667 us)
#else
	.period_us = 16683.0   // (59.94 Hz until measured)
#endif
};

static void ds_reset(void) { DS.warm = 0; DS.owed = 0; DS.rate = 0; DS.n_last = 0; DS.v_prev = 0; DS.t_prev = 0; }
// right after a match frame's swap: the VBlanks shown since the previous one, and the VBlank length
static void ds_after_swap(uint64_t now_us) {
	unsigned v = sceDisplayGetVcount();
	if (DS.t_prev) DS.n_last = (int)(v - DS.v_prev); else DS.n_last = 0;
	DS.v_prev = v; DS.t_prev = now_us;
	if (!DS.pt0) { DS.pv0 = v; DS.pt0 = now_us; }
	else if (v - DS.pv0 >= 120) {   // ~2 s: a new estimate, kept if sane (59-61 Hz)
		double p = (double)(now_us - DS.pt0) / (double)(v - DS.pv0);
		if (p > 16390 && p < 16950) DS.period_us = DS.period_us * 0.5 + p * 0.5;
		DS.pv0 = v; DS.pt0 = now_us;
	}
	DS.warm++;
}
// the step (s) of the frame that starts now; clock_dt: the CPU clock's step (used when off), already clamped by the caller
static float ds_step(float clock_dt, int allowed) {
	if (!allowed) { ds_reset(); return clock_dt; }
	if (DS.warm < 3 || DS.n_last <= 0 || DS.n_last > 8) return clock_dt;   // not settled, or no VBlank passed / a long stall: the clock
	float extra = (float)(DS.n_last - 1);
	DS.owed += extra; DS.rate = DS.rate * 0.75f + extra * 0.25f;   // (b45 emulator review: paying only an eighth of the debt made a
	float pay = DS.rate + (DS.owed > DS.rate ? (DS.owed - DS.rate) * 0.125f : 0.0f);   // steady 30 fps step 1.1 ... 5.4 VBlanks)
	if (pay > DS.owed) pay = DS.owed;
	if (DS.owed - pay > 4) pay = DS.owed;
	DS.owed -= pay;
	float step = (float)((1.0f + pay) * DS.period_us * 1e-6);
	// stats: the flip interval this frame's step stands for, against both steps (as the caller clamps them: 50 ms)
	double flip = DS.n_last * DS.period_us * 1e-6; if (flip > 0.05) flip = 0.05;
	double e1 = fabs((step > 0.05f ? 0.05f : step) - flip), e2 = fabs(clock_dt - flip);
	DS.frames++; DS.late += DS.n_last > 1; if (DS.owed + pay > DS.owed_max) DS.owed_max = DS.owed + pay;
	DS.err_ds += e1; DS.err_clk += e2; if (e1 > DS.err_ds_max) DS.err_ds_max = e1; if (e2 > DS.err_clk_max) DS.err_clk_max = e2; DS.step_sum += step;
	return step;
}
// the per-second line (main.c's verbose block)
static void ds_second(void (*log)(const char *, ...)) {
	if (DS.frames && log)
		log("step: display-locked %u frames, mean %.2f ms, VBlank %.1f us, %u late flips (owed max %.1f VBlanks) | |step - flip| mean %.2f ms max %.2f (clock step: mean %.2f max %.2f)\n",
			DS.frames, DS.step_sum / DS.frames * 1e3, DS.period_us, DS.late, DS.owed_max, DS.err_ds / DS.frames * 1e3, DS.err_ds_max * 1e3,
			DS.err_clk / DS.frames * 1e3, DS.err_clk_max * 1e3);
	DS.frames = DS.late = 0; DS.owed_max = 0; DS.err_ds = DS.err_clk = DS.err_ds_max = DS.err_clk_max = DS.step_sum = 0;
}
