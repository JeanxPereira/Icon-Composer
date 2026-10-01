#include "Source/app/Symbols.h"

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/IconComposerFoundation/Png.h"

#include <algorithm>
#include <cctype>
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
            std::fprintf(stderr, "IconComposer: %zu symbols rasterised in %.0f ms (%zu failed)\n", out->size(), ms,
                         failed);
        },
        [this, out, &pool] {
            for (const Raster& r : *out) {
                const ImTextureID id = pool.create(r.w, r.h, r.rgba.data(), nullptr);
                if (id) map_[r.key] = id;
            }
        });
}

ick::ArtThumb AppArt::art(const fs::path& file) {
    const std::string key = file.string();
    Entry& e = map_[key];
    const auto now = std::chrono::steady_clock::now();
    // A data do arquivo, no maximo uma vez por segundo por arte: e uma chamada
    // ao sistema por linha, e a sidebar desenha todo quadro.
    if (!e.pending && now - e.checked > std::chrono::seconds(1)) {
        e.checked = now;
        std::error_code ec;
        const auto stamp = fs::last_write_time(file, ec);
        if (!ec && (stamp != e.stamp || e.thumb.texture == ImTextureID_Invalid)) {
            e.stamp = stamp;
            e.pending = true;
            struct Out {
                std::uint32_t w = 0, h = 0;
                float aw = 0.0f, ah = 0.0f;
                std::vector<std::uint8_t> rgba;
            };
            auto out = std::make_shared<Out>();
            constexpr std::uint32_t kSide = 96;   // 34 pt a 2x, com folga para encolher liso
            jobs_.submit(
                [out, file, this] {
                    const auto to8 = [](float v) {
                        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                    };
                    std::string ext = file.extension().string();
                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext == ".svg") {
                        std::ifstream f(file, std::ios::binary);
                        const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                        auto doc = icf::svg::SvgDocument::parse(text);
                        if (!doc) return;
                        rb::RenderOptions o;
                        o.width = kSide;
                        o.height = kSide;
                        auto img = rb::renderSvg(device_, *doc, o);
                        if (!img || img->width == 0) return;
                        out->w = img->width;
                        out->h = img->height;
                        // O SVG e encaixado no quadrado; a proporcao e a do viewBox.
                        const auto& box = doc->viewBox;
                        out->aw = static_cast<float>(box.width > 0 ? box.width : 1.0);
                        out->ah = static_cast<float>(box.height > 0 ? box.height : 1.0);
                        out->rgba.resize(img->rgba.size());
                        for (std::size_t i = 0; i < img->rgba.size(); ++i) out->rgba[i] = to8(img->rgba[i]);
                    } else if (ext == ".png") {
                        const icf::DecodedPng png = icf::readPng(file.string());
                        if (!png.error.empty() || png.width == 0) return;
                        // Reduzido por caixa ate caber em kSide.
                        const std::uint32_t step = std::max<std::uint32_t>(1, std::max(png.width, png.height) / kSide);
                        out->w = png.width / step;
                        out->h = png.height / step;
                        out->aw = static_cast<float>(png.width);
                        out->ah = static_cast<float>(png.height);
                        out->rgba.resize(static_cast<std::size_t>(out->w) * out->h * 4);
                        for (std::uint32_t y = 0; y < out->h; ++y)
                            for (std::uint32_t x = 0; x < out->w; ++x)
                                for (int c = 0; c < 4; ++c) {
                                    float acc = 0.0f;
                                    for (std::uint32_t dy = 0; dy < step; ++dy)
                                        for (std::uint32_t dx = 0; dx < step; ++dx)
                                            acc += png.rgba[((static_cast<std::size_t>(y) * step + dy) * png.width +
                                                             x * step + dx) * 4 + c];
                                    out->rgba[(static_cast<std::size_t>(y) * out->w + x) * 4 + c] =
                                        to8(acc / static_cast<float>(step * step));
                                }
                    }
                },
                [this, key, out] {
                    Entry& done = map_[key];
                    done.pending = false;
                    if (out->rgba.empty()) return;
                    if (done.thumb.texture != ImTextureID_Invalid) pool_.remove(done.thumb.texture);
                    done.thumb.texture = pool_.create(out->w, out->h, out->rgba.data(), nullptr);
                    // Um SVG vem num quadrado com a arte centrada: a proporcao
                    // desenhada e a do quadrado, a da arte esta dentro dele.
                    const bool square = out->w == out->h;
                    done.thumb.width = square ? static_cast<float>(out->w) : out->aw;
                    done.thumb.height = square ? static_cast<float>(out->h) : out->ah;
                });
        }
    }
    return e.thumb;
}

ImTextureID AppSymbols::symbol(std::string_view name, bool custom) {
    const auto it = map_.find(keyOf(name, custom));
    return it == map_.end() ? ImTextureID_Invalid : it->second;
}

}  // namespace icapp
