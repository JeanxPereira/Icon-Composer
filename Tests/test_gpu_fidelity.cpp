// O caminho residente (`renderIconGpu`) contra o gabarito de CPU (`renderIcon`),
// frente GPU, G1 -- `Docs/Plans/2026-09-29-render-gpu.md`.
//
// O TETO E O DO PLANO (restricao 2): por documento, erro medio <= 0,5 nivel de 8
// bits e pior pixel <= 4 niveis, os dois lados convertidos com a conversao do
// PNG; e as DECISOES (skipped, shapeGaps, notes, contadores) iguais, porque as
// duas funcoes chamam a mesma `renderIconOn`. Um documento acima do teto se
// explica; o teto nao sobe.
//
// POR QUE UM RECORTE DO CORPUS E NAO OS 145: a suite roda em Debug, onde os 145
// pelos dois caminhos custam minutos. O corpus inteiro e o `icfidelity` (Release,
// ~20 s); aqui ficam os documentos que exercitam cada porta do caminho
// residente -- o que reprovou o teto ao longo da frente (os rasters de vidro:
// ARMSX2, NotchMyProblem, harnss, swmpc), mescla de grupo isolado
// (AssetCatalogTinkerer), SVG com filtro e mask que cai para a CPU (PDF-Archiver,
// PiStats, Delta), traco (quick-push), o de maior media (macai) e o Apollo, que
// tem clip-path, override de camada e sombra.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/cli/Fidelity.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

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

fs::path corpus(const char* name) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    return fs::path(dir ? dir : "") / name;
}

const char* const kDocuments[] = {
    "Apollo-Reborn__Apollo-Reborn__AppIcon",
    "ARMSX2__ARMSX1__icon",
    "Aeastr__NotchMyProblem__icon",
    "OpenSource03__harnss__icon",
    "CamilleScholtz__swmpc__swmpc",
    "insidegui__AssetCatalogTinkerer__AppIcon",
    "PDF-Archiver__PDF-Archiver__AppIcon",
    "Bunn__PiStats__pistats",
    "rileytestut__Delta__MicrochipIcon",
    "Code-with-Beto__quick-push__QuickPushIcon",
    "Renset__macai__AppIcon",
};

}  // namespace

TEST_CASE(gpu_fidelity_within_ceiling_on_the_corpus_sample) {
    Device& device = gpu();
    REQUIRE(device.valid());
    REQUIRE(std::getenv("IC_CORPUS_DIR") != nullptr);
    for (const char* name : kDocuments) {
        auto bundle = icf::IconBundle::open(corpus(name));
        REQUIRE(bundle.has_value());
        auto s = iccli::fidelityOf(device, *bundle, 512);
        REQUIRE(s.has_value());
        if (!s->withinCeiling()) {
            std::printf("  %s: media %.4f pior %d em (%u,%u) canal %d%s%s\n", name, s->mean,
                        s->max, s->worstX, s->worstY, s->worstChannel,
                        s->sameShape ? "" : " -- decisoes divergem: ", s->shapeWhy.c_str());
        }
        CHECK(s->sameShape);
        CHECK(s->mean <= iccli::kFidelityMeanCeiling);
        CHECK(s->max <= iccli::kFidelityMaxCeiling);
    }
}

// O mesmo teto sob o outro idioma que muda a pastilha (watchOS: um circulo com
// meio pixel de recuo) e sob o escuro.
TEST_CASE(gpu_fidelity_within_ceiling_under_other_contexts) {
    Device& device = gpu();
    REQUIRE(device.valid());
    auto bundle = icf::IconBundle::open(corpus("Apollo-Reborn__Apollo-Reborn__AppIcon"));
    REQUIRE(bundle.has_value());
    icf::Context watch;
    watch.idiom = icf::Idiom::WatchOS;
    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    for (const icf::Context& ctx : {watch, dark}) {
        auto s = iccli::fidelityOf(device, *bundle, 512, ctx);
        REQUIRE(s.has_value());
        CHECK(s->withinCeiling());
    }
}

// O invariante de ladrilho (spec 2026-09-16) no caminho residente: um viewport e
// o recorte do render cheio -- aqui "igual" e dentro do teto, e as decisoes
// iguais. O ladrilho escolhido corta a pastilha e a arte do Apollo.
TEST_CASE(gpu_viewport_tile_equals_the_crop_of_a_full_render) {
    Device& device = gpu();
    REQUIRE(device.valid());
    auto bundle = icf::IconBundle::open(corpus("Apollo-Reborn__Apollo-Reborn__AppIcon"));
    REQUIRE(bundle.has_value());
    IconRenderOptions full;
    full.size = 512;
    auto whole = renderIconGpu(device, *bundle, full);
    REQUIRE(whole.has_value());

    IconRenderOptions tile = full;
    tile.viewport = IconViewport{37, 181, 150, 97};
    auto part = renderIconGpu(device, *bundle, tile);
    REQUIRE(part.has_value());
    REQUIRE(part->width == 150 && part->height == 97);

    RenderedIcon crop = *whole;
    crop.width = part->width;
    crop.height = part->height;
    crop.originX = part->originX;
    crop.originY = part->originY;
    crop.rgba.assign(static_cast<std::size_t>(part->width) * part->height * 4, 0.0f);
    for (std::uint32_t y = 0; y < part->height; ++y) {
        for (std::uint32_t x = 0; x < part->width; ++x) {
            const std::size_t s =
                ((static_cast<std::size_t>(y) + part->originY) * whole->width + x + part->originX) * 4;
            const std::size_t d = (static_cast<std::size_t>(y) * part->width + x) * 4;
            for (int k = 0; k < 4; ++k) crop.rgba[d + k] = whole->rgba[s + k];
        }
    }
    // So os PIXELS: o relatorio de um ladrilho e o dele (um efeito fora do
    // ladrilho nao anota nada), e e contra o ladrilho da CPU que ele se cobra.
    const iccli::FidelityStats s = iccli::compareIcons(crop, *part);
    if (s.mean > iccli::kFidelityMeanCeiling || s.max > iccli::kFidelityMaxCeiling) {
        std::printf("  ladrilho: media %.4f pior %d em (%u,%u)\n", s.mean, s.max, s.worstX,
                    s.worstY);
    }
    CHECK(s.mean <= iccli::kFidelityMeanCeiling);
    CHECK(s.max <= iccli::kFidelityMaxCeiling);

    // E o ladrilho da GPU contra o ladrilho da CPU.
    auto cpuPart = renderIcon(device, *bundle, tile);
    REQUIRE(cpuPart.has_value());
    CHECK(iccli::compareIcons(*cpuPart, *part).withinCeiling());
}

// O cache vale para o que ficou na CPU (campo, sombra, realces da pastilha): a
// segunda chamada com o mesmo cache devolve os MESMOS floats da primeira e de uma
// chamada sem cache -- as funcoes cacheadas sao as do caminho de CPU.
TEST_CASE(gpu_render_with_a_warm_cache_is_the_render_without_one) {
    Device& device = gpu();
    REQUIRE(device.valid());
    auto bundle = icf::IconBundle::open(corpus("Aeastr__NotchMyProblem__icon"));
    REQUIRE(bundle.has_value());
    IconRenderOptions plain;
    plain.size = 256;
    auto base = renderIconGpu(device, *bundle, plain);
    REQUIRE(base.has_value());

    RenderCache cache;
    IconRenderOptions cached = plain;
    cached.cache = &cache;
    auto cold = renderIconGpu(device, *bundle, cached);
    auto warm = renderIconGpu(device, *bundle, cached);
    REQUIRE(cold.has_value() && warm.has_value());
    CHECK(cold->rgba == base->rgba);
    CHECK(warm->rgba == base->rgba);
    CHECK(warm->notes == base->notes);
}
