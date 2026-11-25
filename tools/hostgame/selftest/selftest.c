// hostgame shim self-test: each check the shim adds must fire (build: tools/hostgame/selftest.sh)
#include <vitasdk.h>
#include <vitaGL.h>
#include <pthread.h>
static void *offmain(void *p) { (void)p; GLuint t; glGenTextures(1, &t); return NULL; }
int main(int argc, char **argv) {
	int which = argc > 1 ? atoi(argv[1]) : 0; vglInit(0);
	GLuint vs = glCreateShader(GL_VERTEX_SHADER); const char *src = "void main(float3 pos, uniform float4 pal[4], uniform sampler2D dif : TEXUNIT0, float4 out p : POSITION) { p = float4(pos, 1); }";
	glShaderSource(vs, 1, &src, NULL); glCompileShader(vs); GLuint pr = glCreateProgram(); glAttachShader(pr, vs); glBindAttribLocation(pr, 0, "pos"); glLinkProgram(pr); glUseProgram(pr);
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b); glBufferData(GL_ARRAY_BUFFER, 12 * 3, NULL, GL_STATIC_DRAW); glEnableVertexAttribArray(0);
	switch (which) {
	case 1: glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0); glDrawArrays(GL_TRIANGLES, 0, 6); break;               // HOSTGL GL-DRAW-PAST-BUFFER
	case 2: { float v[32] = { 0 }; glUniform4fv(glGetUniformLocation(pr, "pal"), 8, v); break; }                            // HOSTGL GL-UNIFORM-OVERFLOW
	case 3: { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char *px = malloc(16 * 16 * 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); free(px); vglSwapBuffers(GL_FALSE); break; }   // ASan heap-use-after-free (deferred upload)
	case 4: { float *m = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY); m[9] = 1; break; }                                    // ASan heap-buffer-overflow (36-byte buffer)
	case 5: { pthread_t th; pthread_create(&th, NULL, offmain, NULL); pthread_join(th, NULL); break; }                      // HOSTGL GL-OFF-MAIN-THREAD
	case 6: { float v[6] = { 0 }; glBindBuffer(GL_ARRAY_BUFFER, 0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, v); glDrawArrays(GL_TRIANGLES, 0, 3); break; }   // ASan stack-buffer-overflow (client array)
	case 7: { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char *px = malloc(64); glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 16, 16, 0, 64, px); break; }   // HOSTGL GL-COMPRESSED-SIZE
	case 8: { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char *px = malloc(256); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
		unsigned char *p = vglAlloc(4096, VGL_MEM_VRAM); sceGxmTextureInitLinear(vglGetGxmTexture(GL_TEXTURE_2D), p, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 16, 16, 0); vglFree(p);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, 0); glDrawArrays(GL_TRIANGLES, 0, 3); break; }                        // ASan heap-use-after-free (re-pointed texture)
	case 9: { GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t); unsigned char px[64]; memset(px, 1, 64); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px); px[3] = 7; sceGxmTransferFinish(); break; }   // HOSTGL UPLOAD-SOURCE-CHANGED
	}
	puts("selftest done"); return 0;
}
