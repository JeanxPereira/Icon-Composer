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
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
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
