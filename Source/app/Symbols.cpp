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
#include <string_view>
#include <unordered_map>
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

// ---- `<use>` e `<mask>` nos simbolos da UI ----------------------------------
//
// Os assets que vieram do `.car` por um exportador de desenho (os dois
// `toolbar-grid-*`) guardam a forma em `<defs>` e a desenham por `<use>`, e
// recortam o miolo por `<mask>`. O CoreSVG deste repositorio le o que o do alvo
// le -- e nenhum dos dois e geral ali --, entao o `-off` saia vazio e o `-on`
// so com o contorno.
//
// A arte de um DOCUMENTO tem de passar pelo parser como ele e; um simbolo da
// barra nao. Entao so aqui, antes do parse: cada `<use>` vira o `<path>` que
// ele aponta, com os atributos do proprio `<use>`, e a mascara -- uma forma
// branca, nos dois arquivos -- vira o `clipPath` que ela e.
//
// Varredura a mao e nao `std::regex`: um `d` tem oito mil caracteres, e a
// regex da libstdc++ recursa por caractere.
std::string attributeOf(std::string_view tag, std::string_view name) {
    for (std::size_t at = tag.find(name); at != std::string_view::npos; at = tag.find(name, at + name.size())) {
        const std::size_t eq = at + name.size();
        const bool starts = at > 0 && std::isspace(static_cast<unsigned char>(tag[at - 1]));
        if (!starts || eq + 1 >= tag.size() || tag[eq] != '=' || tag[eq + 1] != '"') continue;
        const std::size_t close = tag.find('"', eq + 2);
        if (close == std::string_view::npos) return {};
        return std::string(tag.substr(eq + 2, close - (eq + 2)));
    }
    return {};
}

void eraseAttribute(std::string& attrs, std::string_view name) {
    for (std::size_t at = attrs.find(name); at != std::string::npos; at = attrs.find(name, at + name.size())) {
        const std::size_t eq = at + name.size();
        const bool starts = at > 0 && std::isspace(static_cast<unsigned char>(attrs[at - 1]));
        if (!starts || eq + 1 >= attrs.size() || attrs[eq] != '=' || attrs[eq + 1] != '"') continue;
        const std::size_t close = attrs.find('"', eq + 2);
        if (close == std::string::npos) return;
        attrs.erase(at, close + 1 - at);
        return;
    }
}

void replaceAll(std::string& text, std::string_view from, std::string_view to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
}

std::string flattenUses(std::string svg) {
    if (svg.find("<use") == std::string::npos) return svg;
    // Os caminhos com nome. O ultimo de um id vale, como no parser.
    std::unordered_map<std::string, std::string> paths;
    for (std::size_t at = svg.find("<path"); at != std::string::npos; at = svg.find("<path", at + 5)) {
        const std::size_t end = svg.find('>', at);
        if (end == std::string::npos) break;
        const std::string_view tag(svg.data() + at, end + 1 - at);
        const std::string id = attributeOf(tag, "id"), d = attributeOf(tag, "d");
        if (!id.empty() && !d.empty()) paths[id] = d;
    }

    std::string out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t use = svg.find("<use", from);
        if (use == std::string::npos) break;
        const std::size_t end = svg.find('>', use);
        if (end == std::string::npos) break;
        out.append(svg, from, use - from);
        const std::string tag = svg.substr(use, end + 1 - use);
        const bool selfClosed = tag.size() >= 2 && tag[tag.size() - 2] == '/';
        std::size_t next = end + 1;
        if (!selfClosed && svg.compare(next, 6, "</use>") == 0) next += 6;

        std::string href = attributeOf(tag, "xlink:href");
        if (href.empty()) href = attributeOf(tag, "href");
        const auto target = href.size() > 1 && href[0] == '#' ? paths.find(href.substr(1)) : paths.end();
        if (target == paths.end()) {
            out.append(svg, use, next - use);   // nao e um caminho com nome: fica como esta
        } else {
            std::string attrs = tag.substr(4, tag.size() - 4 - (selfClosed ? 2 : 1));
            eraseAttribute(attrs, "xlink:href");
            eraseAttribute(attrs, "href");
            out += "<path d=\"" + target->second + "\"" + attrs + "/>";
        }
        from = next;
    }
    out.append(svg, from, std::string::npos);

    replaceAll(out, "<mask", "<clipPath");
    replaceAll(out, "</mask>", "</clipPath>");
    replaceAll(out, " mask=\"url(", " clip-path=\"url(");
    return out;
}

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
                    const std::string text = flattenUses(
                        std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()));
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
