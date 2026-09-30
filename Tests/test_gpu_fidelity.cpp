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
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GpuGlass.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/cli/Fidelity.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <filesystem>
#include <string>

using namespace rb;

namespace {

namespace fs = std::filesystem;

Device& gpuDevice() {
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
    Device& device = gpuDevice();
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
    Device& device = gpuDevice();
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
    Device& device = gpuDevice();
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
    Device& device = gpuDevice();
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

// G2: O CAMPO DA GPU E O DA CPU, BIT A BIT. `icon_field.comp` faz em double a mesma
// conta de `exactFieldFromContours`, com o empate pela ordem dos segmentos, e o
// sinal e a mesma mascara -- entao nao ha teto aqui: cada float tem de ser igual.
// A pastilha (a forma de todo render), uma estrela even-odd com vertices sobre
// centros de pixel, e uma nuvem de quadrilateros sobrepostos sob non-zero, no
// canvas inteiro e num ladrilho com origem.
TEST_CASE(gpu_field_is_the_cpu_field_bit_for_bit) {
    Device& device = gpuDevice();
    REQUIRE(device.valid());
    if (!device.float64()) {
        std::printf("  (sem shaderFloat64 neste aparelho: o campo fica na CPU)\n");
        return;
    }
    auto resident = gpu::Resident::of(device);
    REQUIRE(resident.has_value());
    gpu::Resident& r = **resident;

    std::vector<std::pair<std::vector<FieldContour>, FieldRule>> shapes;
    {
        FieldContour c;
        for (const icf::svg::Point& q : chicletPolygon(512)) {
            c.xy.push_back(static_cast<float>(q.x));
            c.xy.push_back(static_cast<float>(q.y));
        }
        shapes.push_back({{c}, FieldRule::NonZero});
    }
    {
        FieldContour c;
        for (int k = 0; k < 5; ++k) {
            const double a = k * 4.0 * 3.14159265358979 / 5.0;
            c.xy.push_back(static_cast<float>(std::floor(256.0 + 200.0 * std::sin(a)) + 0.5));
            c.xy.push_back(static_cast<float>(std::floor(256.0 - 200.0 * std::cos(a)) + 0.5));
        }
        shapes.push_back({{c}, FieldRule::EvenOdd});
    }
    {
        std::vector<FieldContour> cloud;
        std::uint32_t seed = 12345;
        auto next = [&] {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>((seed >> 8) % 4800u) / 10.0f + 16.0f;
        };
        for (int k = 0; k < 40; ++k) {
            FieldContour c;
            const float cx = next(), cy = next();
            for (int v = 0; v < 4; ++v) {
                c.xy.push_back(cx + (next() - 256.0f) * 0.15f);
                c.xy.push_back(cy + (next() - 256.0f) * 0.15f);
            }
            cloud.push_back(c);
        }
        shapes.push_back({cloud, FieldRule::NonZero});
    }

    struct Frame {
        std::uint32_t w, h;
        std::int32_t ox, oy;
    };
    for (const auto& [contours, rule] : shapes) {
        for (const Frame f : {Frame{512, 512, 0, 0}, Frame{150, 97, 37, 181}}) {
            FieldOptions fo;
            fo.rule = rule;
            fo.originX = f.ox;
            fo.originY = f.oy;
            const FieldImage cpu = generateFieldFromContours(contours, f.w, f.h, fo, 1);
            std::lock_guard<std::mutex> lock(r.mutex());
            auto made = gpu::fieldFromContours(r, contours, f.w, f.h, fo, 1);
            REQUIRE(made.has_value());
            CHECK_EQ(made->width, cpu.width);
            if (cpu.width == 0) {
                REQUIRE(r.flush().has_value());
                continue;
            }
            std::vector<float> got(cpu.rgba.size());
            REQUIRE(r.download(made->data, got.data(), got.size() * sizeof(float)).has_value());
            std::size_t differ = 0;
            float worst = 0.0f;
            for (std::size_t i = 0; i < got.size(); ++i) {
                if (std::memcmp(&got[i], &cpu.rgba[i], sizeof(float)) != 0) {
                    ++differ;
                    worst = std::max(worst, std::fabs(got[i] - cpu.rgba[i]));
                }
            }
            if (differ) {
                std::printf("  campo %ux%u@(%d,%d): %zu floats diferem, pior %g\n", f.w, f.h,
                            f.ox, f.oy, differ, static_cast<double>(worst));
            }
            CHECK_EQ(differ, std::size_t{0});
        }
    }
}

// G3: A SOMBRA DA GPU E A DA CPU, BIT A BIT. O anel e uma transformada exata (os
// inteiros sao os mesmos), a escada segue `blurLadderPlan` e cada conta e a da CPU
// na mesma precisao -- entao, de novo, cada float igual. A geometria do alvo nas
// quatro classes de tamanho, e uma inventada que obriga a escada a reduzir duas
// vezes e a translacao a ser fracionaria; as duas cores; com e sem overdraw.
TEST_CASE(gpu_shadow_is_the_cpu_shadow_bit_for_bit) {
    Device& device = gpuDevice();
    REQUIRE(device.valid());
    if (!device.float64()) {
        std::printf("  (sem shaderFloat64 neste aparelho: a sombra fica na CPU)\n");
        return;
    }
    auto resident = gpu::Resident::of(device);
    REQUIRE(resident.has_value());
    gpu::Resident& r = **resident;

    auto compare = [](const std::vector<float>& got, const std::vector<float>& want,
                      const char* what) {
        std::size_t differ = 0;
        float worst = 0.0f;
        for (std::size_t i = 0; i < want.size(); ++i) {
            if (std::memcmp(&got[i], &want[i], sizeof(float)) != 0) {
                ++differ;
                worst = std::max(worst, std::fabs(got[i] - want[i]));
            }
        }
        if (differ) {
            std::printf("  %s: %zu floats diferem, pior %g\n", what, differ,
                        static_cast<double>(worst));
        }
        return differ;
    };

    for (const std::uint32_t size : {256u, 512u}) {
        // Um disco colorido de borda suave com um furo, e uma faixa que sai da
        // borda do buffer (o anel grampeia nas quatro linhas virtuais).
        std::vector<float> art(static_cast<std::size_t>(size) * size * 4, 0.0f);
        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                const double cx = x + 0.5 - size * 0.45, cy = y + 0.5 - size * 0.5;
                const double d = std::sqrt(cx * cx + cy * cy);
                double a = std::clamp(size * 0.3 - d, 0.0, 1.0) *
                           (d < size * 0.08 ? 0.0 : 1.0);
                if (y > size * 0.8 && x > size * 0.6) a = 0.75;
                float* p = &art[(static_cast<std::size_t>(y) * size + x) * 4];
                p[0] = static_cast<float>(x) / size;
                p[1] = 0.3f;
                p[2] = static_cast<float>(y) / size;
                p[3] = static_cast<float>(a);
            }
        }
        std::vector<ShadowGeometry> geometries;
        for (IconSizeClass c : {IconSizeClass::Small, IconSizeClass::Medium, IconSizeClass::Large,
                                IconSizeClass::Display}) {
            geometries.push_back(shadowGeometry(size, c));
        }
        ShadowGeometry wide;
        wide.offsetX = 3.3;
        wide.offsetY = -2.7;
        wide.blurRadius = 40.0;
        wide.ringWidth = 5.5;
        geometries.push_back(wide);
        wide.ringWidth.reset();
        geometries.push_back(wide);

        for (const ShadowGeometry& g : geometries) {
            for (ShadowStyle style : {ShadowStyle::Vibrant, ShadowStyle::Neutral}) {
                const std::vector<float> want = shadowImage(art, size, size, style, g);
                const std::vector<float> wantOver =
                    shadowOverdrawImage(want, art, size, size, 0.37);
                std::lock_guard<std::mutex> lock(r.mutex());
                auto slab = r.acquire(art.size() * sizeof(float));
                REQUIRE(slab.has_value());
                REQUIRE(r.upload(*slab, art.data(), art.size() * sizeof(float)).has_value());
                auto made = gpu::shadow(r, *slab, size, size, style, g, 0.37);
                REQUIRE(made.has_value());
                REQUIRE(made->overdraw != nullptr);
                std::vector<float> got(want.size()), gotOver(want.size());
                REQUIRE(r.download(made->image, got.data(), got.size() * sizeof(float))
                            .has_value());
                REQUIRE(r.download(made->overdraw, gotOver.data(),
                                   gotOver.size() * sizeof(float))
                            .has_value());
                CHECK_EQ(compare(got, want, "sombra"), std::size_t{0});
                CHECK_EQ(compare(gotOver, wantOver, "overdraw"), std::size_t{0});
            }
        }
    }
}
