// Generate a walkable grid nav mesh (*.gnav) for maps whose own AI mesh is missing or floats above the floor.
// Cells of CELL cm; each walkable floor spot becomes a square (two triangles). Neighbours connect when the step
// up is small and nothing blocks the way at knee and chest height; drops connect one way.
// Uses the player collision (TF_PLAYER triangles of .gcol v3) and the engine's player physics (engine/src/player.h).
// usage: navgen MAP.gcol OUT.gnav
#include <stdio.h>
#include "player.h"
#define GCELL 150.0f
#define MAXF 4
typedef struct { int cx, cy; float z; int id, comp; } Node;
static Node *nodes; static int nn, ncap;
static int *cellfirst; static int gwid, ghei; static float bx0, by0;
static int at(int cx, int cy, int k) { // k-th floor node of a cell or -1
	if (cx < 0 || cy < 0 || cx >= gwid || cy >= ghei) return -1;
	int f = cellfirst[cy * gwid + cx]; if (f < 0) return -1;
	for (int i = f; i < nn && nodes[i].cx == cx && nodes[i].cy == cy; i++) if (k-- == 0) return i;
	return -1;
}
static int clear_line(V3 a, V3 b) { V3 d = vsub(b, a); float l = sqrtf(vdot(d, d)); if (l < 1) return 1; float h = raycast_mask(a, vmul(d, 1 / l), l, TF_PLAYER, NULL); return h < 0; }
int main(int argc, char **argv) {
	if (argc < 3 || load_col(argv[1]) < 0) { puts("usage: navgen MAP.gcol OUT.gnav"); return 1; }
	float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f, ztop = -1e30f;
	for (int i = 0; i < nspawn; i++) { mnx = fminf(mnx, spawns[i].x); mxx = fmaxf(mxx, spawns[i].x); mny = fminf(mny, spawns[i].y); mxy = fmaxf(mxy, spawns[i].y); ztop = fmaxf(ztop, spawns[i].z); }
	float pad = 5000; bx0 = mnx - pad; by0 = mny - pad; gwid = (int)((mxx - mnx + 2 * pad) / GCELL) + 1; ghei = (int)((mxy - mny + 2 * pad) / GCELL) + 1; ztop += 1500;
	cellfirst = malloc(sizeof(int) * gwid * ghei);
	ncap = 1 << 16; nodes = malloc(sizeof(Node) * ncap);
	for (int cy = 0; cy < ghei; cy++) for (int cx = 0; cx < gwid; cx++) {
		cellfirst[cy * gwid + cx] = -1;
		float x = bx0 + (cx + 0.5f) * GCELL, y = by0 + (cy + 0.5f) * GCELL, z = ztop; int nf = 0;
		for (int it = 0; it < 12 && nf < MAXF; it++) {
			int ti; float h = raycast_mask(v3(x, y, z), v3(0, 0, -1), 3000, TF_PLAYER, &ti); if (h < 0) break; V3 n = ctris[ti].n; if (n.z < 0) n = vmul(n, -1);
			float fz = z - h; z = fz - 3;
			if (n.z < P_WALKABLE) continue;
			// room to stand: nothing within 160 cm above, and the body isn't inside a wall
			if (raycast_mask(v3(x, y, fz + 5), v3(0, 0, 1), 175, TF_PLAYER, NULL) >= 0) continue;
			Player p; memset(&p, 0, sizeof p); p.pos = v3(x, y, fz + 3);
			for (int s = 0; s < 4; s++) player_update(&p, 0, 0, 0, 0, 1 / 30.f);
			if (fabsf(p.pos.x - x) > 25 || fabsf(p.pos.y - y) > 25 || fabsf(p.pos.z - fz) > 30 || !p.grounded) continue;
			if (nn == ncap) { ncap *= 2; nodes = realloc(nodes, sizeof(Node) * ncap); }
			if (cellfirst[cy * gwid + cx] < 0) cellfirst[cy * gwid + cx] = nn;
			nodes[nn].cx = cx; nodes[nn].cy = cy; nodes[nn].z = p.pos.z; nodes[nn].comp = -1; nn++; nf++;
		}
	}
	// links: dir 0 = +x, 1 = +y, 2 = -x, 3 = -y; link[i][d] = neighbour node or -1 (walkable from i)
	int (*link)[4] = malloc(sizeof(int) * 4 * nn);
	static const int DX[4] = { 1, 0, -1, 0 }, DY[4] = { 0, 1, 0, -1 };
	int nl = 0;
	for (int i = 0; i < nn; i++) for (int d = 0; d < 4; d++) {
		link[i][d] = -1; Node *a = &nodes[i];
		float ax = bx0 + (a->cx + 0.5f) * GCELL, ay = by0 + (a->cy + 0.5f) * GCELL;
		int best = -1; float bdz = 1e9f;
		for (int k = 0; k < MAXF; k++) { int j = at(a->cx + DX[d], a->cy + DY[d], k); if (j < 0) break;
			float dz = nodes[j].z - a->z; if (dz > P_STEP || dz < -200) continue;
			float bxx = ax + DX[d] * GCELL, byy = ay + DY[d] * GCELL, hz = fmaxf(a->z, nodes[j].z);
			if (!clear_line(v3(ax, ay, hz + 55), v3(bxx, byy, hz + 55)) || !clear_line(v3(ax, ay, hz + 120), v3(bxx, byy, hz + 120))) continue;
			if (fabsf(dz) < bdz) { bdz = fabsf(dz); best = j; } }
		link[i][d] = best; if (best >= 0) nl++;
	}
	// components (undirected); keep the ones that hold spawn points, or the largest
	int ncomp = 0, *stack = malloc(sizeof(int) * nn), *csize = calloc(nn, sizeof(int));
	for (int i = 0; i < nn; i++) { if (nodes[i].comp >= 0) continue; int sp = 0; stack[sp++] = i; nodes[i].comp = ncomp;
		while (sp) { int c = stack[--sp]; csize[ncomp]++; for (int d = 0; d < 4; d++) { int j = link[c][d]; if (j >= 0 && nodes[j].comp < 0) { nodes[j].comp = ncomp; stack[sp++] = j; } }
			for (int d = 0; d < 4; d++) { int j = at(nodes[c].cx + DX[d], nodes[c].cy + DY[d], 0); for (int k = 0; j >= 0 && k < MAXF; k++, j = at(nodes[c].cx + DX[d], nodes[c].cy + DY[d], k)) if (link[j][(d + 2) % 4] == c && nodes[j].comp < 0) { nodes[j].comp = ncomp; stack[sp++] = j; } } }
		ncomp++; }
	unsigned char *keep = calloc(ncomp, 1); int kept = 0, big = 0;
	for (int c = 0; c < ncomp; c++) if (csize[c] > csize[big]) big = c;
	for (int s = 0; s < nspawn; s++) { int cx = (int)((spawns[s].x - bx0) / GCELL), cy = (int)((spawns[s].y - by0) / GCELL), bi = -1; float bd = 1e9f;
		for (int k = 0; k < MAXF; k++) { int j = at(cx, cy, k); if (j < 0) break; float d = fabsf(nodes[j].z - spawns[s].z); if (d < bd) { bd = d; bi = j; } }
		if (bi >= 0 && bd < 150 && csize[nodes[bi].comp] > 30) keep[nodes[bi].comp] = 1; }
	keep[big] = 1;
	int *tri0 = malloc(sizeof(int) * nn), nt = 0;   // first triangle of each kept node
	for (int i = 0; i < nn; i++) { tri0[i] = keep[nodes[i].comp] ? nt : -1; if (tri0[i] >= 0) { nt += 2; kept++; } }
	FILE *o = fopen(argv[2], "wb"); fwrite("GNAV", 4, 1, o); fwrite(&nt, 4, 1, o);
	unsigned char *flags = calloc(nt, 1);
	for (int i = 0; i < nn; i++) { if (tri0[i] < 0) continue; Node *a = &nodes[i];
		float x0 = bx0 + a->cx * GCELL, y0 = by0 + a->cy * GCELL, x1 = x0 + GCELL, y1 = y0 + GCELL, z = a->z;
		// T0 = (v00, v10, v11): e0 south (-y), e1 east (+x), e2 diagonal. T1 = (v00, v11, v01): e0 diagonal, e1 north (+y), e2 west (-x)
		float T0[9] = { x0, y0, z, x1, y0, z, x1, y1, z }, T1[9] = { x0, y0, z, x1, y1, z, x0, y1, z };
		int s = link[i][3], e = link[i][0], nth = link[i][1], w = link[i][2];
		// a neighbour is linked both ways in the mesh; a one-way drop is marked blocked on the way back
		int n0[3] = { s >= 0 && tri0[s] >= 0 ? tri0[s] + 1 : -1, e >= 0 && tri0[e] >= 0 ? tri0[e] + 1 : -1, tri0[i] + 1 };
		int n1[3] = { tri0[i], nth >= 0 && tri0[nth] >= 0 ? tri0[nth] : -1, w >= 0 && tri0[w] >= 0 ? tri0[w] : -1 };
		// links that exist only from the other side (we can't walk there but they can walk here): add as blocked
		for (int d = 0; d < 4; d++) { int j = link[i][d]; if (j >= 0) continue;
			for (int k = 0; k < MAXF; k++) { int q = at(a->cx + DX[d], a->cy + DY[d], k); if (q < 0) break;
				if (link[q][(d + 2) % 4] == i && tri0[q] >= 0) {
					if (d == 3) { n0[0] = tri0[q] + 1; flags[tri0[i]] |= 1; } else if (d == 0) { n0[1] = tri0[q] + 1; flags[tri0[i]] |= 2; }
					else if (d == 1) { n1[1] = tri0[q]; flags[tri0[i] + 1] |= 2; } else { n1[2] = tri0[q]; flags[tri0[i] + 1] |= 4; } } } }
		fwrite(T0, 4, 9, o); fwrite(n0, 4, 3, o); fwrite(T1, 4, 9, o); fwrite(n1, 4, 3, o);
	}
	fwrite("FLG1", 4, 1, o); fwrite(flags, 1, nt, o); fclose(o);
	printf("grid %dx%d, floor spots %d, links %d, components %d, kept %d spots -> %d triangles\n", gwid, ghei, nn, nl, ncomp, kept, nt);
	return 0;
}
