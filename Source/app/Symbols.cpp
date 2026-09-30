#include "Source/app/Symbols.h"

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

namespace icapp {
namespace {

namespace fs = std::filesystem;

struct Raster {
    std::string key;
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
};

std::string keyOf(std::string_view name, bool custom) {
    return std::string(custom ? "c:" : "s:") + std::string(name);
}

// Os dois assets que vieram de PDF com degrade e branco DENTRO da forma
// (Sym.tsx, `image`): como mascara virariam um disco chapado, entao ficam com
// a cor deles.
bool keepsColour(const std::string& stem) { return stem == "Opacity" || stem == "Blendmode"; }

}  // namespace

void AppSymbols::schedule(JobQueue& jobs, rb::Device& device, TexturePool& pool, const fs::path& appleDir,
                          std::uint32_t px) {
    if (appleDir.empty()) return;
    auto out = std::make_shared<std::vector<Raster>>();
    jobs.submit(
        [out, &device, appleDir, px] {
            const auto t0 = std::chrono::steady_clock::now();
            std::size_t failed = 0;
            for (const bool custom : {false, true}) {
                const fs::path dir = appleDir / (custom ? "custom" : "symbols");
                std::error_code ec;
                for (const auto& e : fs::directory_iterator(dir, ec)) {
                    if (e.path().extension() != ".svg") continue;
                    std::ifstream f(e.path(), std::ios::binary);
                    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                    auto doc = icf::svg::SvgDocument::parse(text);
                    if (!doc) {
                        ++failed;
                        continue;
                    }
                    rb::RenderOptions o;
                    o.width = px;
                    o.height = px;
                    auto img = rb::renderSvg(device, *doc, o);
                    if (!img || img->width == 0) {
                        ++failed;
                        continue;
                    }
                    const std::string stem = e.path().stem().string();
                    Raster r{keyOf(stem, custom), img->width, img->height, {}};
                    r.rgba.resize(static_cast<std::size_t>(img->width) * img->height * 4);
                    const bool colour = keepsColour(stem);
                    for (std::size_t i = 0, n = r.rgba.size() / 4; i < n; ++i) {
                        auto to8 = [](float v) {
                            return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                        };
                        r.rgba[i * 4 + 0] = colour ? to8(img->rgba[i * 4 + 0]) : 255;
                        r.rgba[i * 4 + 1] = colour ? to8(img->rgba[i * 4 + 1]) : 255;
                        r.rgba[i * 4 + 2] = colour ? to8(img->rgba[i * 4 + 2]) : 255;
                        r.rgba[i * 4 + 3] = to8(img->rgba[i * 4 + 3]);
                    }
                    out->push_back(std::move(r));
                }
            }
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::fprintf(stderr, "iconcomposer: %zu symbols rasterised in %.0f ms (%zu failed)\n", out->size(), ms,
                         failed);
        },
        [this, out, &pool] {
            for (const Raster& r : *out) {
                const ImTextureID id = pool.create(r.w, r.h, r.rgba.data(), nullptr);
                if (id) map_[r.key] = id;
            }
        });
}

ImTextureID AppSymbols::symbol(std::string_view name, bool custom) {
    const auto it = map_.find(keyOf(name, custom));
    return it == map_.end() ? ImTextureID_Invalid : it->second;
}

}  // namespace icapp
