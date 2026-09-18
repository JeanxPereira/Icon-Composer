// O gate do render de viewport (spec 2026-09-16, "O invariante que governa o
// desenho"): um render de viewport e IGUAL ao recorte correspondente de um
// render cheio na mesma `size`, float a float, tolerancia ZERO.
//
// Quando nao e, o relatorio diz DE QUE TIPO e a diferenca: delta grande colado
// numa borda interna do buffer e margem curta; delta na ordem de ULP espalhado
// pelas bordas antialiasadas e aritmetica da GPU. So o primeiro e o desenho
// errado -- e nenhum dos dois se resolve afrouxando a tolerancia.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/IconRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

struct ViewportDiff {
    std::size_t differing = 0;
    float maxAbs = 0.0f;
    std::uint32_t worstX = 0, worstY = 0;   // na grade de `size`
    // Distancia do pior pixel ate a borda do buffer mais proxima que NAO
    // coincide com a borda do canvas. Maximo quando o buffer e o canvas.
    std::uint32_t worstInnerEdge = std::numeric_limits<std::uint32_t>::max();
};

ViewportDiff diffAgainstCrop(const RenderedIcon& full, const RenderedIcon& part) {
    ViewportDiff d;
    const PixelGrid& b = part.buffer;
    for (std::uint32_t y = 0; y < part.height; ++y) {
        for (std::uint32_t x = 0; x < part.width; ++x) {
            const std::uint32_t gx = static_cast<std::uint32_t>(part.originX) + x;
            const std::uint32_t gy = static_cast<std::uint32_t>(part.originY) + y;
            const std::size_t pi = (static_cast<std::size_t>(y) * part.width + x) * 4;
            const std::size_t fi = (static_cast<std::size_t>(gy) * full.width + gx) * 4;
            float worst = 0.0f;
            for (int k = 0; k < 4; ++k) {
                worst = std::max(worst, std::fabs(part.rgba[pi + k] - full.rgba[fi + k]));
            }
            if (worst == 0.0f) continue;
            ++d.differing;
            if (worst <= d.maxAbs) continue;
            d.maxAbs = worst;
            d.worstX = gx;
            d.worstY = gy;
            std::uint32_t edge = std::numeric_limits<std::uint32_t>::max();
            const auto bx0 = static_cast<std::uint32_t>(b.originX);
            const auto by0 = static_cast<std::uint32_t>(b.originY);
            if (bx0 > 0) edge = std::min(edge, gx - bx0);
            if (by0 > 0) edge = std::min(edge, gy - by0);
            if (bx0 + b.width < part.size) edge = std::min(edge, bx0 + b.width - 1 - gx);
            if (by0 + b.height < part.size) edge = std::min(edge, by0 + b.height - 1 - gy);
            d.worstInnerEdge = edge;
        }
    }
    return d;
}

// Renderiza cheio e em cada viewport, compara, e imprime o relatorio de
// cada viewport que diferir. Devolve quantos viewports diferiram.
int compareViewports(const icf::IconBundle& bundle, std::uint32_t size,
                     const std::vector<IconViewport>& views, RenderedIcon* fullOut = nullptr) {
    Device& dev = gpu();
    IconRenderOptions o;
    o.size = size;
    auto full = renderIcon(dev, bundle, o);
    if (!full) {
        std::printf("  FAIL render cheio: %s\n", full.error().c_str());
        ++ictest::failures();
        return 1;
    }
    int bad = 0;
    for (const IconViewport& v : views) {
        IconRenderOptions vo = o;
        vo.viewport = v;
        auto part = renderIcon(dev, bundle, vo);
        if (!part) {
            std::printf("  FAIL viewport (%d,%d %ux%u): %s\n", v.originX, v.originY, v.width,
                        v.height, part.error().c_str());
            ++bad;
            continue;
        }
        const ViewportDiff d = diffAgainstCrop(*full, *part);
        if (d.differing == 0) continue;
        ++bad;
        std::printf("  viewport (%d,%d %ux%u) buffer (%d,%d %ux%u): %zu px diferentes, "
                    "max |d| = %.9g em (%u,%u), a %u px da borda interna\n",
                    v.originX, v.originY, v.width, v.height, part->buffer.originX,
                    part->buffer.originY, part->buffer.width, part->buffer.height, d.differing,
                    static_cast<double>(d.maxAbs), d.worstX, d.worstY, d.worstInnerEdge);
    }
    if (fullOut) *fullOut = std::move(*full);
    return bad;
}

// Os quatro viewports da spec, numa grade de `s`: cruzando a borda da forma,
// encostado no canvas, com origem que nao e multipla de 4, e abaixo da forma.
std::vector<IconViewport> gateViewports(std::uint32_t s) {
    const std::int32_t q = static_cast<std::int32_t>(s / 4);
    const std::uint32_t side = s / 3;
    return {
        IconViewport{q - 7, q - 7, side, side},                                    // borda da forma
        IconViewport{0, static_cast<std::int32_t>(s - side), side, side},          // canto do canvas
        IconViewport{q + 3, q * 2 + 1, side + 5, side - 3},                        // origem nao alinhada
        IconViewport{q, static_cast<std::int32_t>(s - s / 5), side, s / 5},        // abaixo da forma
    };
}

// Um bundle de mentira, escrito no temporario, para gatear o que a Task 3
// converteu SEM depender de um documento do corpus: fundo em gradiente,
// pastilha, uma arte vetor e uma raster, e nenhum vidro -- que e exatamente a
// fatia que ja anda num buffer parcial.
class TempBundle {
public:
    explicit TempBundle(const std::string& name, const std::string& document) {
        dir_ = fs::temp_directory_path() / ("ic-viewport-" + name);
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // Um circulo: borda curva para o antialias da GPU, e uma rampa propria
        // que mede o retangulo de colocacao.
        write(dir_ / "Assets" / "disc.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<circle cx=\"256\" cy=\"256\" r=\"200\" fill=\"#3080ff\"/></svg>");
        // Um raster com borda dura e um degrade em x e em z, ampliado 6x: uma
        // amostragem deslocada meio texel aparece na cor, nao so na borda.
        const std::uint32_t side = 48;
        std::vector<float> px(static_cast<std::size_t>(side) * side * 4, 0.0f);
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * side + x) * 4;
                const bool in = (x - 24.0) * (x - 24.0) + (y - 24.0) * (y - 24.0) < 400.0;
                px[i + 0] = static_cast<float>(x) / side;
                px[i + 1] = 0.5f;
                px[i + 2] = static_cast<float>(y) / side;
                px[i + 3] = in ? 1.0f : 0.0f;
            }
        }
        const std::vector<std::uint8_t> png = icf::encodePng(px, side, side);
        std::FILE* f = std::fopen((dir_ / "Assets" / "dot.png").string().c_str(), "wb");
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }
    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

    TempBundle(const TempBundle&) = delete;
    TempBundle& operator=(const TempBundle&) = delete;

private:
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

// Fundo em gradiente + um disco vetor + um raster ampliado, os dois sem vidro.
// As cores sao a grafia do corpus (`display-p3:r,g,b,a`).
const char* const kPlainDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.90000,0.20000,0.30000,1.00000",
                                   "display-p3:0.10000,0.30000,0.90000,1.00000" ] },
  "groups" : [
    { "layers" : [
      { "glass" : false, "image-name" : "disc.svg", "name" : "disc" },
      { "glass" : false, "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ 120, -90 ] } }
    ] }
  ]
})";

}  // namespace

TEST_CASE(viewport_default_is_the_whole_canvas) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    CHECK_EQ(compareViewports(*bundle, 256, {IconViewport{}}), 0);
}

TEST_CASE(viewport_echoes_where_its_pixels_sit) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    // O eco e o que o Kit usa para por a textura no lugar; sem ele o gate nao
    // sabe onde recortar o render cheio.
    IconRenderOptions o;
    o.size = 256;
    o.viewport = IconViewport{96, 96, 64, 64};
    auto part = renderIcon(d, *bundle, o);
    REQUIRE(part.has_value());
    CHECK_EQ(part->width, 64u);
    CHECK_EQ(part->originX, 96);
}

// O INVARIANTE SOBRE PIXEL DE VERDADE, no que a Task 3 converteu: o fundo em
// rampa, o recorte da pastilha com os realces, a arte vetor pela GPU e a arte
// raster amostrada na CPU. Tolerancia ZERO nos quatro viewports da spec.
TEST_CASE(viewport_background_chiclet_and_art_match_the_full_render) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("plain", kPlainDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.backgroundPainted);
    CHECK_EQ(full.drawn, 2u);
}
