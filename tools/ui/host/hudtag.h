// ui b42: element tags in a HUD capture (tools/ui/host/overlap.py measures each tagged element's visible pixels and flags pairs
// that overlap or come closer than the minimum gap). Force-include it in a host harness build (gcc -include hudtag.h, with the
// harness's own vitaGL.h on the include path): the engine's HUD_TAG("name") marks then write a record into the capture
// stream - header { -1, vertices still pending in hud.h's batch (they belong to the element before), name length }, then the
// name padded to 4 bytes. tools/menu/raster.py skips these records.
#pragma once
#include <string.h>
#include <vitaGL.h>   // (the harness's own stand-in, from the include path)
static void cap_tag(const char *name, int pending) {
	if (!cap_out) return; int n = (int)strlen(name), h[3] = { -1, pending, n }; static const char pad[4] = { 0 };
	fwrite(h, 4, 3, cap_out); fwrite(name, 1, n, cap_out); fwrite(pad, 1, (4 - n % 4) % 4, cap_out);
}
#define HUD_TAG(n) cap_tag((n), hvn)
