#include <stdio.h>
#include <stdlib.h>
#include "reverb.h"
int main(int argc, char **argv) {
	RevDef d = { "t", -200, -500, -1500, 2.5f, 0.8f, -300, 0.014f, 600, 0.02f, 100, 20, 6000, 200 };
	if (argc > 1) { d.room = 0; d.room_hf = 0; d.room_lf = 0; d.reverb = 0; d.refl = -10000; d.decay_hf = 1; d.density = atof(argv[1]); }
	reverb_set(&d, &d);
	static int bus[1024], out[2048]; int N = 48000 * 6 / 1024; FILE *f = fopen("ir.raw", "wb");
	double ein = 0, eout = 0; srand(3);
	for (int g = 0; g < N; g++) {
		memset(bus, 0, sizeof(bus)); memset(out, 0, sizeof(out));
		if (g == 0) for (int k = 0; k < 480; k++) { bus[k] = (rand() % 20001) - 10000; }
		// energy at 24 kHz: pairs averaged
		for (int k = 0; k < 1024; k += 2) { double x = (bus[k] + bus[k + 1]) * 0.5; ein += x * x; }
		reverb_grain(bus, out, 1024);
		for (int k = 0; k < 1024; k++) { eout += 0.5 * (double)out[2 * k] * out[2 * k]; }
		fwrite(out, 4, 2048, f);
	}
	fclose(f); printf("energy out(L, per 24k sample)/in = %.3f (%.2f dB)\n", eout / ein, 10 * log10(eout / ein)); return 0; }
