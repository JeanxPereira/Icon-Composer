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
#include "Source/RenderBox/BlendFormula.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GlassTranslucency.h"
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

// O CACHE RESIDENTE ATRAVESSA UMA EDICAO: arte, campo, sombra e pastilha ficam na
// GPU pela chave de conteudo, entao trocar o que um passo le (aqui a aparencia,
// que muda os preenchimentos, e o tamanho de classe, que muda a sombra) re-roda
// so o que mudou -- e cada render com o cache e o render SEM ele, float por
// float, notas e contadores inclusive. O Apollo tem vidro vetorial com mascara e
// sombra; o NotchMyProblem, vidro sobre raster.
TEST_CASE(gpu_resident_cache_survives_an_edit) {
    Device& device = gpuDevice();
    REQUIRE(device.valid());
    for (const char* name : {"Apollo-Reborn__Apollo-Reborn__AppIcon", "Aeastr__NotchMyProblem__icon"}) {
        auto bundle = icf::IconBundle::open(corpus(name));
        REQUIRE(bundle.has_value());
        IconRenderOptions light;
        light.size = 256;
        IconRenderOptions dark = light;
        dark.context.appearance = icf::Appearance::Dark;
        IconRenderOptions small = light;
        small.sizeClass = IconSizeClass::Small;

        RenderCache cache;
        for (const IconRenderOptions& o : {light, dark, small, light, dark}) {
            auto plain = renderIconGpu(device, *bundle, o);
            IconRenderOptions cached = o;
            cached.cache = &cache;
            auto warm = renderIconGpu(device, *bundle, cached);
            REQUIRE(plain.has_value() && warm.has_value());
            CHECK(warm->rgba == plain->rgba);
            CHECK(warm->notes == plain->notes);
            CHECK(warm->shapeGaps == plain->shapeGaps);
            CHECK_EQ(warm->glassSpecular, plain->glassSpecular);
            CHECK_EQ(warm->glassShadowed, plain->glassShadowed);
        }
        CHECK(cache.stats().hits > 0);
    }
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

// G4: CADA PASSO DE VIDRO DA GPU CONTRA O DA CPU, sobre o mesmo campo e o mesmo
// alvo, num ladrilho com origem. Os realces (especular com e sem VCM, e os da
// pastilha) sao double como a CPU e tem de sair iguais bit a bit, com a mesma
// contagem; a mascara e a refracao sao float, e a divisao float da GPU pode errar
// um ulp, entao ali o teto e o do ulp (1e-6) -- e a contagem da mascara, exata.
TEST_CASE(gpu_glass_steps_are_the_cpu_steps) {
    Device& device = gpuDevice();
    REQUIRE(device.valid());
    if (!device.float64()) {
        std::printf("  (sem shaderFloat64 neste aparelho: os realces ficam na CPU)\n");
        return;
    }
    auto resident = gpu::Resident::of(device);
    REQUIRE(resident.has_value());
    gpu::Resident& r = **resident;

    const PixelGrid grid{512, 37, 21, 300, 260};
    // O campo de uma estrela, na grade do ladrilho.
    FieldContour star;
    for (int k = 0; k < 10; ++k) {
        const double a = k * 3.14159265358979 / 5.0;
        const double rr = (k % 2) ? 90.0 : 200.0;
        star.xy.push_back(static_cast<float>(250.0 + rr * std::sin(a)));
        star.xy.push_back(static_cast<float>(240.0 - rr * std::cos(a)));
    }
    FieldOptions fo;
    fo.originX = grid.originX;
    fo.originY = grid.originY;
    const FieldImage field = generateFieldFromContours({star}, grid.width, grid.height, fo, 1);
    REQUIRE(field.width == grid.width);

    // Um alvo pre-multiplicado com cor e alfa variando.
    std::vector<float> acc(grid.texels() * 4);
    for (std::uint32_t y = 0; y < grid.height; ++y) {
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            float* p = &acc[(static_cast<std::size_t>(y) * grid.width + x) * 4];
            const float a = 0.25f + 0.75f * static_cast<float>((x * 7 + y * 3) % 97) / 96.0f;
            p[0] = a * static_cast<float>(x) / grid.width;
            p[1] = a * 0.5f;
            p[2] = a * static_cast<float>(y) / grid.height;
            p[3] = a;
        }
    }

    // (REQUIRE sai da funcao com `return;`, entao aqui dentro e CHECK.)
    auto upload = [&](const std::vector<float>& v) -> gpu::Slab {
        auto s = r.acquire(v.size() * sizeof(float));
        CHECK(s.has_value());
        if (!s) return nullptr;
        CHECK(r.upload(*s, v.data(), v.size() * sizeof(float)).has_value());
        return *s;
    };
    auto down = [&](const gpu::Slab& s, std::size_t n) -> std::vector<float> {
        std::vector<float> v(n);
        CHECK(r.download(s, v.data(), n * sizeof(float)).has_value());
        return v;
    };
    auto differ = [](const std::vector<float>& a, const std::vector<float>& b, float& worst) {
        std::size_t n = 0;
        worst = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i) {
            if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) {
                ++n;
                worst = std::max(worst, std::fabs(a[i] - b[i]));
            }
        }
        return n;
    };

    std::lock_guard<std::mutex> lock(r.mutex());
    const gpu::Slab fieldSlab = upload(field.rgba);
    auto counters = r.acquire(64);
    REQUIRE(counters.has_value());
    r.fill(*counters, 0);

    // -- os realces ---------------------------------------------------------
    SpecularArguments args;
    args.sizeClass = IconSizeClass::Large;
    args.pixelsPerPoint = 0.5;
    args.layerOpacity = 0.8;
    struct Case {
        const char* name;
        bool chiclet, vcm;
        std::uint32_t slot;
    };
    for (const Case c : {Case{"especular VCM", false, true, 0}, Case{"especular mescla", false, false, 1},
                         Case{"pastilha", true, false, 2}}) {
        SpecularArguments a = args;
        a.useVCM = c.vcm;
        std::vector<float> want = acc;
        std::size_t wantCount = 0;
        std::size_t n = 0;
        const HighlightSlot* slots = c.chiclet ? chicletHighlightSlots(n) : glyphHighlightSlots(n);
        if (c.chiclet) {
            // `drawChicletHighlights` faz o proprio campo da pastilha: aqui o campo
            // e o da estrela, entao a referencia e o laco dele sobre ESTE campo.
            wantCount = 0;
            std::vector<char> hit(grid.texels(), 0);
            for (std::size_t s = 0; s < n; ++s) {
                const GlassHighlightSettings g = resolveHighlight(slots[s], a);
                if (g.opacity <= 0.0 || g.height <= 0.0) continue;
                for (std::size_t t = 0; t < grid.texels(); ++t) {
                    const float* p = &field.rgba[t * 4];
                    const double sd = -static_cast<double>(p[0]);
                    if (sd < -2.0) continue;
                    if (p[1] == 0.0f && p[2] == 0.0f) continue;
                    const double clip = want[t * 4 + 3];
                    if (clip <= 0.0) continue;
                    const double f = glassHighlightFragment(g, sd, p[1], p[2], 1.0);
                    if (f <= 0.0) continue;
                    const double alpha = f * g.opacity * clip;
                    BlendColour src;
                    for (int k = 0; k < 3; ++k) src.rgba[k] = g.colour[k] * alpha;
                    src.rgba[3] = alpha;
                    BlendColour dst;
                    for (int k = 0; k < 4; ++k) dst.rgba[k] = want[t * 4 + k];
                    const BlendColour o = blend(g.blendMode, src, dst);
                    for (int k = 0; k < 4; ++k) {
                        const double v = o.rgba[k] < 0.0 ? 0.0 : o.rgba[k];
                        if (static_cast<float>(v) != want[t * 4 + k]) hit[t] = 1;
                        want[t * 4 + k] = static_cast<float>(v);
                    }
                }
            }
            for (char h : hit) wantCount += static_cast<std::size_t>(h);
        } else {
            wantCount = drawSpecular(want, field, a);
        }
        std::vector<double> records;
        REQUIRE(gpu::resolveHighlights(slots, n, a, records));
        const gpu::Slab target = upload(acc);
        REQUIRE(gpu::highlights(r, target, fieldSlab, grid.width, grid.height, records, c.chiclet,
                                a.useVCM, a.clampPlusLighter, *counters, c.slot)
                    .has_value());
        const std::vector<float> got = down(target, acc.size());
        std::uint32_t counts[16] = {};
        REQUIRE(r.download(*counters, counts, sizeof counts).has_value());
        float worst = 0.0f;
        const std::size_t d = differ(got, want, worst);
        if (d) std::printf("  %s: %zu floats diferem, pior %g\n", c.name, d, static_cast<double>(worst));
        CHECK_EQ(d, std::size_t{0});
        CHECK_EQ(static_cast<std::size_t>(counts[c.slot]), wantCount);
        CHECK(wantCount > 0);
    }

    // -- a mascara ------------------------------------------------------------
    {
        OpacityMaskArguments ma;
        ma.borderWidth = 12.0f;
        ma.opacityBounds[0] = 0.3f;
        ma.opacityBounds[1] = 0.9f;
        ma.contourOpacityBounds[0] = 0.6f;
        ma.contourOpacityBounds[1] = 1.0f;
        ma.bounds[0] = 40.0f;
        ma.bounds[1] = 30.0f;
        ma.bounds[2] = 400.0f;
        ma.bounds[3] = 420.0f;
        std::vector<float> want = acc;
        const OpacityMask mask = glassOpacityMask(field, ma);
        std::size_t painted = 0;
        const std::size_t missed = opacityMaskMissedPixels(want, mask, painted);
        applyOpacityMask(want, mask);
        const gpu::Slab art = upload(acc);
        REQUIRE(gpu::glassMask(r, art, fieldSlab, grid.width, grid.height, field.originY, ma,
                               *counters, 4)
                    .has_value());
        const std::vector<float> got = down(art, acc.size());
        std::uint32_t counts[16] = {};
        REQUIRE(r.download(*counters, counts, sizeof counts).has_value());
        float worst = 0.0f;
        const std::size_t d = differ(got, want, worst);
        if (d) std::printf("  mascara: %zu floats diferem, pior %g\n", d, static_cast<double>(worst));
        CHECK(worst <= 1e-6f);
        CHECK_EQ(static_cast<std::size_t>(counts[4]), painted);
        CHECK_EQ(static_cast<std::size_t>(counts[5]), missed);
    }

    // -- a refracao ------------------------------------------------------------
    for (std::uint32_t variant : {0u, 2u, 3u}) {
        GlassRefraction g;
        g.heightPixels = 24.0f;
        g.curvature = 0.7f;
        g.angleCos = 0.8f;
        g.angleSin = 0.6f;
        g.offset = 1.5f;
        g.maskOffset = 0.5f;
        g.scalePixels = 18.0f;
        g.variant = variant;
        // A estrela tem pixels com `s == height` exato: e ali que a divisao float da
        // GPU jogava `flatProfile` para o outro lado do degrau (icon_displace.comp)
        // e esta comparacao dava meio nivel inteiro de diferenca.
        std::vector<float> want = acc;
        glassOver(want, grid, glassDisplacementMap(field, g), g);
        const gpu::Slab target = upload(acc);
        REQUIRE(gpu::refract(r, target, fieldSlab, grid, g).has_value());
        const std::vector<float> got = down(target, acc.size());
        float worst = 0.0f;
        const std::size_t d = differ(got, want, worst);
        if (d) {
            std::printf("  refracao variante %u: %zu floats diferem, pior %g\n", variant, d,
                        static_cast<double>(worst));
        }
        CHECK(worst <= 1e-5f);
    }
}
