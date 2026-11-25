// texup.h (perf b56): uploads a DXT texture's mip chain with one reallocation instead of one per level.
// vitaGL's glCompressedTexImage2D grows a texture's memory when a level past its current chain arrives (vgl_realloc, or a
// new block + a CPU copy of every level so far into uncached GPU memory): levels 0, 1, 2 ... in order grew it once per level.
// Here level 0 goes first (a fresh allocation), then the LAST level (the chain grows once, to its full size), then the
// levels between, which fit (vitaGL reuses the buffer). The texture ends up the same. Each upload still waits for its
// transfer (sceGxmTransferFinish: the next realloc / the caller's buffer). Switch file ux0:data/gitsfa/mipinorder: the
// old order (A/B on the device; LOADPROF "tex upload").
#pragma once
#include <vitaGL.h>
#include <stdio.h>
static int texup_inorder = -1;
static int texup_order_old(void) {
	if (texup_inorder < 0) { FILE *f = fopen("ux0:data/gitsfa/mipinorder", "rb"); texup_inorder = f != NULL; if (f) fclose(f); }
	return texup_inorder;
}
// levels 1 .. nm-1 of the bound texture, their data at lv[m] (sizes sz[m]); level 0 already uploaded from lv0 (sz0 bytes,
// still valid: when the canary is in (texcanary.h) level 0 is compared again after the chain grew: a copy gone wrong in
// vitaGL's reallocation is counted in the "dxt uploads" line and rewritten from lv0)
static void texup_rest(GLenum ifmt, int w, int h, int nm, const unsigned char *const *lv, const unsigned *sz, const unsigned char *lv0, unsigned sz0) {
	if (nm < 2) return;
	int old = texup_order_old();
	for (int k = 1; k < nm; k++) {
		int m = old ? k : (k == 1 ? nm - 1 : k - 1);   // new order: last, then 1 .. nm-2
		int mw = w >> m, mh = h >> m; if (mw < 1) mw = 1; if (mh < 1) mh = 1;
		glCompressedTexImage2D(GL_TEXTURE_2D, m, ifmt, mw, mh, 0, sz[m], lv[m]); sceGxmTransferFinish();
	}
#ifdef TC_DXT_LOG
	if (lv0 && !old) tc_dxt_check(0, ifmt == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT ? 1 : 5, (unsigned)w, (unsigned)h, sz0, lv0, 1);
#else
	(void)lv0; (void)sz0;
#endif
}
