// gxpdsm: USSE disassembly of PS Vita GXP programs with Vita3K's shader translator (render b38, GPU cost model).
// usage: gxpdsm <out dir> <file.gxp|shader-cache .bin> ...   writes <out dir>/<basename>.dsm (the instructions the
// translator decoded, secondary program first, then PHAS and the primary program)
#include <shader/spirv_recompiler.h>
#include <gxm/types.h>
#include <features/state.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <algorithm>
int main(int argc, char **argv) {
    if (argc < 3) { std::cerr << "usage: gxpdsm <out dir> <gxp>...\n"; return 2; }
    std::string out = argv[1]; int ok = 0;
    for (int i = 2; i < argc; i++) {
        std::ifstream f(argv[i], std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(f)), {});
        const char mg[4] = { 'G', 'X', 'P', 0 }; auto it = std::search(b.begin(), b.end(), mg, mg + 4);
        if (it == b.end()) { std::cerr << argv[i] << ": no GXP\n"; continue; }
        std::vector<char> g(it, b.end()); g.resize(g.size() + 64, 0);
        const SceGxmProgram &p = *reinterpret_cast<const SceGxmProgram *>(g.data());
        FeatureState features{}; features.support_shader_interlock = true; features.direct_fragcolor = false;
        shader::Hints hints{}; hints.attributes = nullptr; hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
        std::fill_n(hints.vertex_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
        std::fill_n(hints.fragment_textures, SCE_GXM_MAX_TEXTURE_UNITS, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
        std::string dsm;
        auto dumper = [&](const std::string &ext, const std::string &d) { if (ext == "dsm") dsm = d; return true; };
        shader::convert_gxp(p, "gxpdsm", features, shader::Target::SpirVVulkan, hints, false, false, dumper);
        std::string base = argv[i]; base = base.substr(base.find_last_of('/') + 1);
        std::ofstream o(out + "/" + base + ".dsm"); o << dsm; ok++;
    }
    std::cerr << ok << " programs disassembled\n";
    return 0;
}
