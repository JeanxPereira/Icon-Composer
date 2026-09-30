// `renderIconGpu` -- as decisoes de `renderIcon`, com os pixels na GPU.
//
// O QUE E RESIDENTE (frente GPU, G1-G4 -- `Docs/Plans/2026-09-29-render-gpu.md`)
// ----------------------------------------------------------------------------
// O acumulador, o alvo de um grupo isolado e a arte de cada camada sao buffers da
// GPU do comeco ao fim. Na GPU, sem nada subir ou descer:
//   - o fundo (cor ou rampa) e o recorte a pastilha (`icon_paint`, `icon_chiclet`);
//   - a cobertura de cada caminho do SVG (a mesma `CoveragePass`) e o resolve
//     por forma -- regra de preenchimento, tinta de override em rampa ou cor,
//     gradiente do proprio SVG, `compositeFlat`, opacidade, clip-path
//     (`icon_svg`) --, sem o readback por forma do caminho de CPU;
//   - a colocacao bilinear da arte raster (`icon_raster`), quando o vidro nao
//     tira o campo do alfa dela (ver [UP3]);
//   - a composicao com os modos transcritos, grupo isolado incluido (`icon_blend`);
//   - o vidro (GpuGlass.cpp): o campo de um vetor (`icon_field`, G2), a sombra
//     e o overdraw (`icon_ring`, `icon_shadow`, `icon_blur`, G3), a mascara de
//     translucidez (`icon_glass_mask`), a refracao (`icon_displace`,
//     `icon_refract`), o especular e os realces da pastilha (`icon_highlight`, G4)
//     -- o campo, a sombra e os realces bit a bit os da CPU, a mascara e a
//     refracao a um ulp;
//   - o recorte e a des-multiplicacao do fim (`icon_finish`), e UM readback, que
//     traz junto as contagens que decidem notas e lacunas (IconSurface.h).
//
// O QUE AINDA SOBE, cada um marcado onde acontece (nada mais DESCE no meio):
//   [UP1] traco do SVG: a cobertura e rasterizada na CPU e sobe.
//   [UP2] SVG com filtro, mask, pattern ou mais de um clip: o render inteiro da
//         camada e o de CPU (`renderSvgPlaced`) e sobe.
//   [UP3] raster de vidro: colocado na CPU (exato) e sobe, porque o campo sai do
//         alfa dele -- `placeRaster` abaixo diz o que foi medido.
//   [UP4] o campo de um raster (`generateFieldFromAlpha`, sobre a copia de
//         [UP3]) e feito na CPU e sobe: as duas transformadas de Felzenszwalb com
//         a semente sub-texel ficam para depois, e o raster de vidro e minoria no
//         corpus (45 de 171 camadas de vidro).
//   O sinal do campo de um vetor (`fieldInsideMask`, um byte por pixel) e a
//   grade de segmentos sobem com ele.
// Um aparelho sem `shaderFloat64` faz o vidro todo na CPU, pelas idas e voltas
// de G1 (`onTarget`, `artOnCpu`, `fieldOnCpu` abaixo).
//
// O CACHE (`RenderCache.h`) guarda buffers da GPU com chave de conteudo -- ver
// "o cache residente" abaixo.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/GpuGlass.h"
#include "Source/RenderBox/GpuResident.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/IconSurface.h"
#include "Source/RenderBox/PathBuffer.h"
#include "Source/RenderBox/PathResolveOracle.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/RenderBox/StrokeRender.h"
#include "Source/RenderBox/SvgRenderer.h"

namespace rb {
namespace {

using gpu::Range;
using gpu::Resident;
using gpu::Slab;
using gpu::groups16;
using gpu::whole;

// ---- os blocos de push constant, espelho dos `layout(push_constant)` -----------

struct PaintPush {
    float colour[4];
    float m0, m1, m2;
    std::uint32_t kind;
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t smoothRamp;
    std::uint32_t nstops;
};
static_assert(sizeof(PaintPush) == 56);

struct ChicletPush {
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t size;
    std::uint32_t npoly;
    std::uint32_t pass;
};
static_assert(sizeof(ChicletPush) == 28);

struct BlendPush {
    std::uint32_t w, h;
    std::uint32_t mode;
    std::uint32_t premultipliedSource;
    float alpha;
    std::uint32_t sw;
    std::int32_t sx, sy;
};
static_assert(sizeof(BlendPush) == 32);

struct SvgPush {
    float colour[4];
    float gm[6];
    float invSx, invSy, m2x, m2y;
    float opacity;
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t mode;
    std::uint32_t state;
    std::uint32_t paint;
    std::uint32_t nstops;
    std::uint32_t gstate;
    std::uint32_t hasClip;
};
static_assert(sizeof(SvgPush) == 100);

struct FinishPush {
    std::uint32_t vw, vh;
    std::uint32_t srcW;
    std::int32_t cx, cy;
};
static_assert(sizeof(FinishPush) == 20);


struct StrokePush {
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t cap;
    std::uint32_t hard;
};
static_assert(sizeof(StrokePush) == 24);

struct RasterPush {
    std::uint32_t w, h;
    std::uint32_t iw, ih;
};
static_assert(sizeof(RasterPush) == 16);

// As paradas no formato do `RampStop` dos shaders: localizacao em x, e a cor.
Result<Range> stageStops(Resident& r, const std::vector<RampPoint>& stops) {
    std::vector<float> packed;
    packed.reserve(std::max<std::size_t>(stops.size(), 1) * 8);
    for (const RampPoint& p : stops) {
        packed.insert(packed.end(), {p.location, 0.0f, 0.0f, 0.0f});
        packed.insert(packed.end(), {p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]});
    }
    if (packed.empty()) packed.assign(8, 0.0f);
    return r.stage(packed.data(), packed.size() * sizeof(float));
}

Slab artSlab(const SurfaceArt& art) { return std::static_pointer_cast<Buffer>(art.resident); }
Slab fieldSlab(const SurfaceField& f) { return std::static_pointer_cast<Buffer>(f.resident); }

// ---- o cache residente ------------------------------------------------------------
//
// O QUE UM RENDER DEIXA NA GPU PARA O PROXIMO. As entradas moram no MESMO
// `RenderCache` do caminho de CPU (a mesma chave de conteudo, o mesmo teto, o
// mesmo LRU), com um dominio proprio e um tipo proprio: um buffer da GPU nunca
// responde por um valor de CPU. Um buffer guardado e so LIDO por quem o recebe;
// quem precisa escrever copia antes. O `RenderCache` tem de morrer antes do
// `Device` (o buffer volta ao pool do aparelho, ou e destruido com ele).
//
// O que fica, por camada, e com que chave:
//   - a arte do SVG: o TEXTO, a colocacao e as opcoes (a chave de
//     `svgRenderCached`); a de um raster: os pixels, a colocacao, a grade e se a
//     colocacao foi a exata de [UP3]. O buffer guardado E o da camada
//     (`SurfaceArt::shared`): a mascara, a unica que escreve na arte, copia antes;
//   - o campo de um vetor: os contornos (a chave de `fieldFromContoursCached`);
//     o de um raster: a chave da arte; o da pastilha: a grade e a plataforma;
//   - a sombra DESFOCADA (antes da translacao e do overdraw): a chave da arte
//     COMO ELA ESTA -- a do render encadeada com a do campo e os argumentos da
//     mascara, quando houve uma. A translacao e o overdraw sao refeitos a cada
//     render, na grade estreita.
// Um passo cujo insumo nao mudou nao roda. O que roda todo quadro e o que le o
// acumulador (refracao, especular, realces da pastilha) e as mesclas.
//
// O PRECO DO PRIMEIRO QUADRO, que era de alocacao: um buffer guardado nao volta
// ao heap, e ate 29/09 cada buffer novo era um `vkAllocateMemory` (`[ART]` ~7 ms
// para 16 MB neste aparelho) -- o Apollo a 1024 px saia em 148 ms no frio COM
// cache contra 88 ms sem. Agora os buffers sao pedacos de blocos grandes
// (GpuResident.h), e o frio com cache so aloca quando o heap nao cabe.
//
// O ZOOM PROFUNDO (duas grades). `planViewport` da ao buffer a margem da sombra
// (sigma e deslocamento em PONTOS: a 8192 px ~880 px por lado), e um ladrilho de
// 1000x800 vivia num buffer de ~2800x2600 -- ~9x a area pedida -- em que TODO
// passo rodava. So a arte, o campo que a mascara le e a sombra precisam dessa
// margem; o acumulador so precisa do recorte mais o alcance encadeado das
// refracoes (a `narrow` do plano). Entao o acumulador, o fundo, a pastilha, os
// realces, o especular, a refracao e as mesclas moram na grade ESTREITA, e leem
// a arte, o campo e a sombra da LARGA por um deslocamento (`SourceView`). E as
// bordas do buffer de um ladrilho caem numa grade de `kTileLattice` px, para que
// um pan curto ache a arte, o campo e a sombra no cache.
// Os contadores de um render: uma palavra por contagem pedida (`CountSink`,
// `MaskSink`), descidos todos juntos no `finish`.
constexpr std::uint32_t kCounterWords = 1024;

// A grade dos buffers de ladrilho (`planViewport`, `lattice`). Um pan curto no
// zoom profundo cai quase sempre no MESMO buffer, e a arte, o campo e a sombra
// -- guardados pela grade -- saem do cache; o preco e ate 511 px de margem a
// mais num ladrilho novo.
constexpr std::uint32_t kTileLattice = 512;
// A grade do retangulo em que o campo de um glifo tem a banda do acumulador.
constexpr std::uint32_t kNearLattice = 256;

struct GpuCachedField {
    Slab data;
    std::uint32_t width = 0, height = 0;
    std::int32_t originX = 0, originY = 0;
};

struct GpuCachedArt {
    Slab art;
    std::vector<SkippedShape> skipped;
};

// A sombra desfocada, antes da translacao e do overdraw (`gpu::shadowBlur`).
struct GpuCachedShadow {
    Slab image;
    bool blurred = false;
};

// Os mesmos campos que `hashInto` de IconRenderer.cpp nomeia, com o mesmo
// tripwire de tamanho.
void hashPaint(KeyHasher& h, const FillOverride& f) {
    static_assert(sizeof(FillOverride) == 104, "a FillOverride field is missing from the key");
    h.value(f.kind).span(f.colour, 4).span(f.m, 6).value(f.smooth);
    h.value(f.stops.size());
    for (const RampPoint& p : f.stops) h.value(p.location).span(p.rgba, 4);
}

void hashRender(KeyHasher& h, const PathGlobals& g, const RenderOptions& o) {
    static_assert(sizeof(PathGlobals) == 52, "a PathGlobals field is missing from the key");
    h.span(g.m0, 2).span(g.m1, 2).span(g.m2, 2).span(g.twoOverSize, 2).span(g.origin, 2);
    h.value(g.depth).value(g.urx).value(g.arg);
    static_assert(sizeof(RenderOptions) == 136, "a RenderOptions field is missing from the key");
    h.value(o.width).value(o.height).value(o.originX).value(o.originY);
    h.value(o.projectionWidth).value(o.projectionHeight).value(o.subdivisions);
    hashPaint(h, o.override);
}

void hashKey(KeyHasher& h, const CacheKey& k) { h.value(k.a).value(k.b); }

void hashField(KeyHasher& h, const std::vector<FieldContour>& contours, std::uint32_t width,
               std::uint32_t height, const FieldOptions& o, std::uint32_t ss) {
    h.value(contours.size());
    for (const FieldContour& c : contours) h.span(c.xy.data(), c.xy.size());
    h.value(width).value(height).value(ss);
    static_assert(sizeof(FieldOptions) == 20, "a FieldOptions field is missing from the key");
    h.value(o.rule).value(o.aaWidth).value(o.originX).value(o.originY).value(o.subpixelSeed);
}

// ---- o SVG residente --------------------------------------------------------------

constexpr std::uint32_t kPlainFill = 1u << kShapeModeShift;

std::uint32_t stateFor(icf::svg::FillRule rule) {
    return kPlainFill | (rule == icf::svg::FillRule::EvenOdd ? kEvenOdd : 0u);
}

// O que o laco residente nao desenha e entrega ao de CPU [UP2]: um grupo com
// filtro (desfoque de CPU sobre o grupo composto), uma mask de luminancia (um
// sub-render inteiro), um pattern (amostragem de CPU) e o produto de mais de um
// clip. `[ART]` Poucos documentos do corpus chegam aqui.
bool svgNeedsCpu(const icf::svg::SvgDocument& doc) {
    for (const icf::svg::Shape& s : doc.shapes) {
        if (s.filterInstance != 0) return true;
        if (!s.masks.empty()) return true;
        if (s.clipPaths.size() > 1) return true;
        if (s.fill.kind == icf::svg::PaintKind::Reference && doc.patterns.count(s.fill.reference)) {
            return true;
        }
    }
    return false;
}

// A cobertura de um caminho no alvo `R16G16` e copiada para o buffer de
// cobertura, gravadas no lote -- o `pass->draw` + `readBack` do caminho de CPU,
// sem a espera e sem a descida.
Result<void> drawCoverage(Resident& r, PathBuffer path, const PathGlobals& globals,
                          CoverageViewport vp, std::uint32_t w, std::uint32_t h) {
    if (auto ok = r.coverage.check(r.device(), w, h, path, vp); !ok) return ok;
    auto seg = r.stage(path.entries.data(), path.entries.size() * sizeof(CubicSegment));
    if (!seg) return std::unexpected(seg.error());
    auto set = r.descriptorSet(r.coverage.setLayout(), {*seg});
    if (!set) return std::unexpected(set.error());
    auto held = std::make_shared<const PathBuffer>(std::move(path));
    const CoveragePass* pass = &r.coverage;
    const VkFramebuffer fb = r.coverageFramebuffer();
    const VkDescriptorSet ds = *set;
    r.record([=](VkCommandBuffer cmd) { pass->record(cmd, fb, ds, w, h, *held, globals, vp); });
    const DeviceApi* api = &r.device().api();
    const VkImage img = r.coverageImage().handle();
    const VkBuffer cov = r.coverageBuffer()->handle();
    r.record([=](VkCommandBuffer cmd) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {w, h, 1};
        api->vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cov, 1,
                                    &region);
    });
    return {};
}

// `renderSvgPlaced` com a arte num buffer da GPU (RGBA reto). Mesmo laco, mesmas
// razoes de pulo, na mesma ordem; o que ele nao cobre vai para [UP2].
Result<RenderedImage> svgResident(Resident& r, Device& device, const icf::svg::SvgDocument& doc,
                                  const PathGlobals& placement, const RenderOptions& options,
                                  const Slab& art) {
    if (options.width == 0 || options.height == 0) {
        return std::unexpected("a render target of zero area was asked for");
    }
    const std::uint32_t w = options.width, h = options.height;
    const std::size_t texels = static_cast<std::size_t>(w) * h;

    if (svgNeedsCpu(doc)) {
        // [UP2]
        auto drew = renderSvgPlaced(device, doc, placement, options);
        if (!drew) return drew;
        if (auto ok = r.upload(art, drew->rgba.data(), drew->rgba.size() * sizeof(float)); !ok) {
            return std::unexpected(ok.error());
        }
        drew->rgba.clear();
        return drew;
    }

    RenderedImage out;
    out.width = w;
    out.height = h;
    if (auto ok = r.ensureCoverage(w, h); !ok) return std::unexpected(ok.error());
    auto accMade = r.acquire(texels * 16);
    if (!accMade) return std::unexpected(accMade.error());
    const Slab acc = *accMade;
    r.fill(acc, 0);

    const PathGlobals globals = placement;
    const CoverageViewport vp{options.originX, options.originY, options.projectionWidth,
                              options.projectionHeight};
    const Slab& covBuf = r.coverageBuffer();
    const Range dummy = whole(r.dummy());

    auto basePush = [&]() {
        SvgPush p{};
        p.w = w;
        p.h = h;
        p.ox = options.originX;
        p.oy = options.originY;
        p.opacity = 1.0f;
        return p;
    };

    // Os clips, feitos uma vez cada e sob demanda, como na CPU.
    std::map<std::string, Slab> clipMasks;
    std::string clipError;
    auto clipFor = [&](const std::string& id) -> const Slab* {
        auto it = clipMasks.find(id);
        if (it != clipMasks.end()) return &it->second;
        auto def = doc.clipPaths.find(id);
        if (def == doc.clipPaths.end()) return nullptr;
        auto made = r.acquire(texels * 4);
        if (!made) { clipError = made.error(); return nullptr; }
        r.fill(*made, 0);
        for (const icf::svg::Path& path : def->second) {
            if (path.segments.empty()) continue;
            auto buffer = buildPathBuffer(path, BuildOptions{options.subdivisions});
            if (!buffer) { clipError = buffer.error(); return nullptr; }
            if (auto ok = drawCoverage(r, std::move(*buffer), globals, vp, w, h); !ok) {
                clipError = ok.error();
                return nullptr;
            }
            SvgPush p = basePush();
            p.mode = 2;
            p.state = stateFor(icf::svg::FillRule::NonZero);
            if (auto ok = r.dispatch(r.svg, {whole(acc), whole(covBuf), dummy, whole(*made), dummy},
                                     &p, groups16(w), groups16(h));
                !ok) {
                clipError = ok.error();
                return nullptr;
            }
        }
        return &clipMasks.emplace(id, *made).first->second;
    };

    for (std::size_t i = 0; i < doc.shapes.size(); ++i) {
        const icf::svg::Shape& shape = doc.shapes[i];
        const bool strokePaints = shape.stroke.kind != icf::svg::PaintKind::None &&
                                  shape.strokeWidth > 0.0;
        if (shape.fill.kind == icf::svg::PaintKind::None &&
            options.override.kind == FillOverride::Kind::None && !strokePaints) {
            continue;
        }
        ResolvedGradient ramp;
        const bool overridden = options.override.kind != FillOverride::Kind::None;
        if (!overridden && shape.fill.kind == icf::svg::PaintKind::Reference) {
            double bx0, by0, bx1, by1;
            svgPathBounds(shape.path, bx0, by0, bx1, by1);
            ramp = resolveGradient(doc, shape.fill.reference, bx0, by0, bx1, by1, shape.ctm);
            if (!ramp.ok) {
                out.skipped.push_back({i, shape.element, ramp.why});
                continue;
            }
        }
        if (shape.path.segments.empty()) continue;

        const Slab* clip = nullptr;
        std::string missingClip;
        for (const std::string& id : shape.clipPaths) {
            const Slab* m = clipFor(id);
            if (!clipError.empty()) return std::unexpected(clipError);
            if (!m) { missingClip = id; break; }
            clip = m;
        }
        if (!missingClip.empty()) {
            out.skipped.push_back({i, shape.element,
                                   "clip-path url(#" + missingClip +
                                       ") nao resolve para nenhum clipPath do documento"});
            continue;
        }
        const Range clipRange = clip ? whole(*clip) : dummy;

        auto buffer = buildPathBuffer(shape.path, BuildOptions{options.subdivisions});
        if (!buffer) {
            out.skipped.push_back({i, shape.element, buffer.error()});
            continue;
        }
        if (auto ok = drawCoverage(r, std::move(*buffer), globals, vp, w, h); !ok) {
            return std::unexpected(ok.error());
        }

        if (!overridden && shape.fill.color.displayP3) out.unconvertedP3.push_back(i);
        SvgPush p = basePush();
        p.colour[0] = static_cast<float>(shape.fill.color.r);
        p.colour[1] = static_cast<float>(shape.fill.color.g);
        p.colour[2] = static_cast<float>(shape.fill.color.b);
        p.colour[3] = static_cast<float>(shape.fill.color.a);
        if (options.override.kind == FillOverride::Kind::Solid) {
            for (int k = 0; k < 4; ++k) p.colour[k] = options.override.colour[k];
        }
        p.state = stateFor(shape.fillRule);
        p.opacity = static_cast<float>(shape.opacity);
        p.hasClip = clip ? 1u : 0u;
        p.invSx = globals.m0[0] != 0.0f ? static_cast<float>(1.0 / globals.m0[0]) : 0.0f;
        p.invSy = globals.m1[1] != 0.0f ? static_cast<float>(1.0 / globals.m1[1]) : 0.0f;
        p.m2x = globals.m2[0];
        p.m2y = globals.m2[1];

        const bool fillPaints = shape.fill.kind != icf::svg::PaintKind::None ||
                                options.override.kind != FillOverride::Kind::None;
        if (fillPaints) {
            const std::vector<RampPoint>* stops = nullptr;
            if (options.override.kind == FillOverride::Kind::Ramp) {
                p.paint = options.override.smooth ? 1u : 2u;
                for (int k = 0; k < 3; ++k) p.gm[k] = static_cast<float>(options.override.m[k]);
                stops = &options.override.stops;
            } else if (ramp.ok) {
                p.paint = 3u;
                p.gstate = ramp.state;
                for (int k = 0; k < 6; ++k) p.gm[k] = static_cast<float>(ramp.m[k]);
                stops = &ramp.stops;
            }
            Range stopRange = dummy;
            if (stops) {
                auto staged = stageStops(r, *stops);
                if (!staged) return std::unexpected(staged.error());
                stopRange = *staged;
                p.nstops = static_cast<std::uint32_t>(stops->size());
            }
            p.mode = 0;
            if (auto ok = r.dispatch(r.svg, {whole(acc), whole(covBuf), stopRange, clipRange, dummy},
                                     &p, groups16(w), groups16(h));
                !ok) {
                return std::unexpected(ok.error());
            }
        }

        if (shape.stroke.kind != icf::svg::PaintKind::None && shape.strokeWidth > 0.0) {
            if (shape.stroke.kind == icf::svg::PaintKind::Reference) {
                out.skipped.push_back({i, shape.element,
                                       "o traco pinta com url(#...) e so traco chapado"
                                       " esta transcrito"});
            } else {
                StrokePlacement sp;
                sp.m0[0] = globals.m0[0];
                sp.m0[1] = globals.m0[1];
                sp.m1[0] = globals.m1[0];
                sp.m1[1] = globals.m1[1];
                sp.m2[0] = globals.m2[0];
                sp.m2[1] = globals.m2[1];
                sp.scale = std::sqrt(static_cast<double>(globals.m0[0]) * globals.m0[0] +
                                     static_cast<double>(globals.m0[1]) * globals.m0[1]);
                StrokeParams params;
                params.cap = LineCap::Butt;
                params.join = LineJoin::Miter;
                params.miterLimit = 4.0;
                // A cobertura do traco: `icon_stroke` (era [UP1]) -- os segmentos e
                // as caixas saem da CPU (`strokeInstances`), a varredura e o maximo
                // sao da GPU. Sem double, a de CPU sobe como antes.
                bool drawn = false;
                Range covRange = dummy;
                if (r.float64()) {
                    const StrokeInstances inst =
                        strokeInstances(shape, sp, w, h, options.subdivisions, params,
                                        options.originX, options.originY);
                    drawn = inst.drawn;
                    if (drawn) {
                        auto buf = r.acquire(texels * 4);
                        if (!buf) return std::unexpected(buf.error());
                        r.fill(*buf, 0);
                        const std::size_t n = inst.records.size() / kStrokeRecordStride;
                        if (n > 0) {
                            auto recs = r.stage(inst.records.data(),
                                                inst.records.size() * sizeof(double));
                            if (!recs) return std::unexpected(recs.error());
                            const StrokePush push{w, h, options.originX, options.originY,
                                                  static_cast<std::uint32_t>(params.cap),
                                                  params.hardCoverage ? 1u : 0u};
                            if (auto ok = r.dispatch(r.stroke, {whole(*buf), *recs}, &push,
                                                     static_cast<std::uint32_t>(n), 1);
                                !ok) {
                                return std::unexpected(ok.error());
                            }
                        }
                        covRange = whole(*buf);
                    }
                } else {
                    const std::vector<float> cov =
                        rasteriseStroke(shape, sp, w, h, options.subdivisions, params,
                                        options.originX, options.originY);
                    drawn = !cov.empty();
                    if (drawn) {
                        auto staged = r.stage(cov.data(), cov.size() * sizeof(float));
                        if (!staged) return std::unexpected(staged.error());
                        covRange = *staged;
                    }
                }
                if (drawn) {
                    if (shape.stroke.color.displayP3) out.unconvertedP3.push_back(i);
                    const Range* staged = &covRange;
                    SvgPush sp2 = basePush();
                    sp2.mode = 1;
                    sp2.colour[0] = static_cast<float>(shape.stroke.color.r);
                    sp2.colour[1] = static_cast<float>(shape.stroke.color.g);
                    sp2.colour[2] = static_cast<float>(shape.stroke.color.b);
                    sp2.colour[3] = static_cast<float>(shape.stroke.color.a);
                    sp2.opacity = static_cast<float>(shape.opacity);
                    sp2.hasClip = clip ? 1u : 0u;
                    if (auto ok = r.dispatch(r.svg,
                                             {whole(acc), whole(covBuf), dummy, clipRange, *staged},
                                             &sp2, groups16(w), groups16(h));
                        !ok) {
                        return std::unexpected(ok.error());
                    }
                    ++out.strokesDrawn;
                }
            }
        }
        ++out.drawn;
    }

    // O fim de `renderSvgPlaced`: des-multiplica para a arte reta.
    FinishPush f{w, h, w, 0, 0};
    if (auto ok = r.dispatch(r.finish, {whole(acc), whole(art)}, &f, groups16(w), groups16(h));
        !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

// ---- a superficie -----------------------------------------------------------------

class GpuSurface final : public IconSurface {
public:
    GpuSurface(Device& device, Resident& r) : device_(device), r_(r) {}

    std::uint32_t bufferLattice() const override { return kTileLattice; }

    Result<void> begin(const PixelGrid& grid, const PixelGrid& narrow) override {
        grid_ = grid;
        // A GRADE ESTREITA so vale com o vidro na GPU: as quedas de CPU (aparelho
        // sem double) levam o alvo e o campo juntos, na mesma grade.
        narrow_ = r_.float64() ? narrow : grid;
        auto a = r_.acquire(bytes());
        if (!a) return std::unexpected(a.error());
        acc_ = *a;
        r_.fill(acc_, 0);
        auto c = r_.acquire(kCounterWords * 4);
        if (!c) return std::unexpected(c.error());
        counters_ = *c;
        r_.fill(counters_, 0);
        return {};
    }

    Result<void> paintBackground(const FillOverride& paint) override {
        PaintPush p{};
        for (int k = 0; k < 4; ++k) p.colour[k] = paint.colour[k];
        p.m0 = static_cast<float>(paint.m[0]);
        p.m1 = static_cast<float>(paint.m[1]);
        p.m2 = static_cast<float>(paint.m[2]);
        p.kind = paint.kind == FillOverride::Kind::Ramp ? 1u : 0u;
        p.w = narrow_.width;
        p.h = narrow_.height;
        p.ox = narrow_.originX;
        p.oy = narrow_.originY;
        p.smoothRamp = paint.smooth ? 1u : 0u;
        p.nstops = static_cast<std::uint32_t>(paint.stops.size());
        auto stops = stageStops(r_, paint.stops);
        if (!stops) return std::unexpected(stops.error());
        return r_.dispatch(r_.paint, {whole(acc_), *stops}, &p, groups16(narrow_.width),
                           groups16(narrow_.height));
    }

    Result<void> clipToChiclet(IconPlatform platform) override {
        if (narrow_.size == 0 || narrow_.width == 0 || narrow_.height == 0) {
            r_.fill(acc_, 0);
            return {};
        }
        const std::vector<icf::svg::Point> poly = chicletPolygon(narrow_.size, platform);
        if (poly.size() < 3) {   // a CPU devolve cobertura zero, e o fundo some
            r_.fill(acc_, 0);
            return {};
        }
        std::vector<float> xy;
        xy.reserve(poly.size() * 2);
        for (const icf::svg::Point& q : poly) {
            xy.push_back(static_cast<float>(q.x));
            xy.push_back(static_cast<float>(q.y));
        }
        auto staged = r_.stage(xy.data(), xy.size() * sizeof(float));
        if (!staged) return std::unexpected(staged.error());
        auto spans = r_.acquire(static_cast<VkDeviceSize>(narrow_.height) * 4 * 9 * sizeof(float));
        if (!spans) return std::unexpected(spans.error());
        ChicletPush p{narrow_.width, narrow_.height, narrow_.originX, narrow_.originY, narrow_.size,
                      static_cast<std::uint32_t>(poly.size()), 0};
        if (auto ok = r_.dispatch(r_.chiclet, {whole(acc_), *staged, whole(*spans)}, &p, 1,
                                  groups16(narrow_.height));
            !ok) {
            return ok;
        }
        p.pass = 1;
        return r_.dispatch(r_.chiclet, {whole(acc_), *staged, whole(*spans)}, &p,
                           groups16(narrow_.width), groups16(narrow_.height));
    }

    // Os realces da pastilha (G4, era [RT1]): o campo da pastilha na GPU,
    // guardado no cache pela grade e pela plataforma, e `icon_highlight`.
    Result<void> chicletHighlights(RenderCache* cache, const SpecularArguments& args,
                                   IconPlatform platform, CountSink sink) override {
        std::size_t count = 0;
        const HighlightSlot* slots = chicletHighlightSlots(count);
        std::vector<double> records;
        const bool transcribed = gpu::resolveHighlights(slots, count, args, records);
        if (!r_.float64() || !transcribed) {
            // A ida e volta de antes: o alvo desce, `drawChicletHighlights`, sobe.
            // Com a grade estreita o alvo entra num buffer da grade larga (zerado
            // fora dela -- alfa 0, que o recorte pelo alfa pula), para que o campo
            // grampeie na borda do buffer como na CPU.
            std::size_t drawn = 0;
            if (auto ok = onTarget([&](std::vector<float>& acc) {
                    if (sameGrids()) {
                        drawn = drawChicletHighlights(acc, grid_, args, platform);
                        return;
                    }
                    std::vector<float> wide(grid_.texels() * 4, 0.0f);
                    copyRows(acc, narrow_.width, 0, 0, wide, grid_.width, dx(), dy(),
                             narrow_.width, narrow_.height);
                    drawn = drawChicletHighlights(wide, grid_, args, platform);
                    copyRows(wide, grid_.width, dx(), dy(), acc, narrow_.width, 0, 0,
                             narrow_.width, narrow_.height);
                });
                !ok) {
                return ok;
            }
            sink(drawn);
            return {};
        }
        if (narrow_.size == 0 || narrow_.width == 0 || narrow_.height == 0) {
            sink(0);
            return {};
        }
        // O ALCANCE DOS REALCES. `fragmentOf` (icon_highlight.comp) zera o realce
        // quando `sd - inset > height + banda/2`, e fora da pastilha o laco pula
        // `sd < -2`: alem de `inset + height + banda` para dentro e de 2 para fora
        // o campo nao muda pixel nenhum. Entao ele so e exato ate ai (`farBand`):
        // no zoom profundo o miolo da pastilha fica a milhares de pixels do
        // contorno, e a busca exata la custava `[ART]` ~90 ms de GPU num
        // ladrilho do Kenzu a 8192 px (12 785 segmentos), para valores que
        // nenhum realce le.
        double reach = 2.0;
        for (std::size_t i = 0; i + gpu::kHighlightStride <= records.size();
             i += gpu::kHighlightStride) {
            reach = std::max(reach, std::fabs(records[i + 0]) + records[i + 1] + records[i + 13]);
        }
        const float farBand = static_cast<float>(std::ceil(reach)) + 2.0f;
        CacheKey key;
        std::optional<GpuCachedField> field;
        if (cache) {
            KeyHasher h("gpu-chiclet-field");
            h.value(narrow_.size).value(narrow_.originX).value(narrow_.originY);
            h.value(narrow_.width).value(narrow_.height).value(platform).value(farBand);
            h.value(grid_.originX).value(grid_.originY).value(grid_.width).value(grid_.height);
            key = h.finish();
            if (auto hit = cache->find<GpuCachedField>(key)) field = *hit;
        }
        if (!field) {
            // `drawChicletHighlights`: o contorno do canvas inteiro, o campo com a
            // origem do buffer, uma amostra por pixel -- aqui na grade ESTREITA,
            // a unica que os realces tocam, com o grampo de dentro na borda do
            // buffer (a larga), como o `FieldImage` da CPU.
            const std::vector<FieldContour> contours = chicletFieldContours(narrow_.size, platform);
            GpuCachedField made;
            if (!contours.empty()) {
                FieldOptions fo;
                fo.originX = narrow_.originX;
                fo.originY = narrow_.originY;
                const gpu::FieldClamp clamp{dx(), dy(), grid_.width, grid_.height};
                auto f = gpu::fieldFromContours(r_, contours, narrow_.width, narrow_.height, fo, 1,
                                                farBand, &clamp);
                if (!f) return std::unexpected(f.error());
                made = GpuCachedField{f->data, f->width, f->height, f->originX, f->originY};
            }
            if (cache) cache->store(key, made, made.data ? made.data->size() : 0);
            field = made;
        }
        if (field->width == 0) {
            sink(0);
            return {};
        }
        const std::uint32_t slot = counterSlot(1);
        if (auto ok = gpu::highlights(r_, target(), field->data, narrow_.width, narrow_.height,
                                      records, true, false, false, counters_, slot);
            !ok) {
            return ok;
        }
        pending_.push_back([this, slot, sink = std::move(sink)]() { sink(counted(slot)); });
        return {};
    }

    Result<void> beginGroup(bool isolated) override {
        group_.reset();
        if (!isolated) return {};
        auto g = r_.acquire(bytes());
        if (!g) return std::unexpected(g.error());
        group_ = *g;
        r_.fill(group_, 0);
        return {};
    }

    Result<void> endGroup(std::optional<BlendMode> blend) override {
        Slab g = std::move(group_);
        group_.reset();
        if (!g || !blend) return {};
        BlendPush p{narrow_.width, narrow_.height, static_cast<std::uint32_t>(*blend), 1u, 1.0f,
                    narrow_.width, 0, 0};
        return r_.dispatch(r_.blend, {whole(acc_), whole(g)}, &p, groups16(narrow_.width),
                           groups16(narrow_.height));
    }

    // Com cache, a arte fica guardada na GPU pela chave de `svgRenderCached` (o
    // TEXTO do SVG, a colocacao, as opcoes): o proximo render copia o buffer em
    // vez de refazer a cobertura. A copia e porque a mascara escreve na arte.
    Result<SurfaceArt> drawSvg(RenderCache* cache, const std::string& text,
                               const icf::svg::SvgDocument& svg, const PathGlobals& placement,
                               const RenderOptions& ro) override {
        CacheKey key;
        if (cache) {
            KeyHasher h("gpu-svg-render");
            h.bytes(text.data(), text.size());
            hashRender(h, placement, ro);
            key = h.finish();
            if (auto hit = cache->find<GpuCachedArt>(key)) {
                SurfaceArt art;
                art.resident = hit->art;
                art.skipped = hit->skipped;
                art.key = key;
                art.keyed = true;
                art.shared = true;
                return art;
            }
        }
        auto a = r_.acquire(static_cast<VkDeviceSize>(ro.width) * ro.height * 16);
        if (!a) return std::unexpected(a.error());
        auto drew = svgResident(r_, device_, svg, placement, ro, *a);
        if (!drew) return std::unexpected(drew.error());
        SurfaceArt art;
        art.resident = *a;
        art.skipped = std::move(drew->skipped);
        if (cache) {
            cache->store(key, GpuCachedArt{*a, art.skipped}, (*a)->size());
            art.key = key;
            art.keyed = true;
            art.shared = true;
        }
        return art;
    }

    Result<SurfaceArt> placeRaster(RenderCache* cache, const icf::DecodedPng& png,
                                   const LayerPlacement& lp, bool feedsField) override {
        CacheKey key;
        if (cache) {
            KeyHasher h("gpu-raster");
            h.value(png.width).value(png.height).span(png.rgba.data(), png.rgba.size());
            static_assert(sizeof(LayerPlacement) == 24, "a LayerPlacement field is missing");
            h.value(lp.scale).value(lp.translateX).value(lp.translateY);
            h.value(grid_.size).value(grid_.originX).value(grid_.originY);
            h.value(grid_.width).value(grid_.height);
            // A colocacao exata de [UP3] e a de float nao respondem uma pela outra.
            h.value(feedsField);
            key = h.finish();
            if (auto hit = cache->find<GpuCachedArt>(key)) {
                SurfaceArt art;
                art.resident = hit->art;
                art.key = key;
                art.keyed = true;
                art.shared = true;
                return art;
            }
        }
        auto placed = placeRasterUncached(png, lp, feedsField);
        if (!placed) return placed;
        if (cache) {
            const Slab kept = artSlab(*placed);
            cache->store(key, GpuCachedArt{kept, {}}, kept->size());
            placed->key = key;
            placed->keyed = true;
            placed->shared = true;
        }
        return placed;
    }

    Result<SurfaceArt> placeRasterUncached(const icf::DecodedPng& png, const LayerPlacement& lp,
                                           bool feedsField) {
        auto a = r_.acquire(wideBytes());
        if (!a) return std::unexpected(a.error());
        SurfaceArt art;
        art.resident = *a;
        if (png.width == 0 || png.height == 0) {
            r_.fill(*a, 0);
            return art;
        }
        if (feedsField) {
            // [UP3] O vidro vai construir o campo do ALFA desta arte, e o campo
            // decide dentro/fora com `alpha >= 0.5`. A colocacao em float erra um
            // ulp aqui e ali -- o produto de dois pesos arredondados contra o
            // peso arredondado de um produto em double --, e um texel que cruza o
            // limiar muda o campo em volta dele: `[ART]` ARMSX2 (escala 0,17),
            // NotchMyProblem, harnss e swmpc passavam do teto com 14 a 35
            // niveis. Entao, so neste caso, a colocacao e a de CPU, exata, e
            // sobe; a copia fica em `rgba` e o campo nao precisa de readback.
            art.rgba = placeRasterOnCpu(png, lp, grid_);
            if (auto ok = r_.upload(*a, art.rgba.data(), art.rgba.size() * sizeof(float)); !ok) {
                return std::unexpected(ok.error());
            }
            return art;
        }
        auto staged = r_.stage(png.rgba.data(), png.rgba.size() * sizeof(float));
        if (!staged) return std::unexpected(staged.error());
        // A aritmetica de `placeRaster` (IconRenderer.cpp), em double, eixo a
        // eixo -- o shader so multiplica os pesos (icon_raster.comp diz por que).
        const double k = static_cast<double>(grid_.size) / kCanvasPoints;
        const double wd = png.width * lp.scale, hd = png.height * lp.scale;
        const double left = (kCanvasPoints - wd) * 0.5 + lp.translateX;
        const double top = (kCanvasPoints - hd) * 0.5 + lp.translateY;
        std::vector<float> axes;
        axes.reserve((static_cast<std::size_t>(grid_.width) + grid_.height) * 4);
        auto axis = [&](double c, double limit) {
            if (c < -1.0 || c > limit) {
                axes.insert(axes.end(), {0.0f, 0.0f, 0.0f, 0.0f});
                return;
            }
            const double f = std::floor(c);
            const double t = c - f;
            axes.insert(axes.end(), {static_cast<float>(f), static_cast<float>(t),
                                     static_cast<float>(1.0 - t), 1.0f});
        };
        for (std::uint32_t x = 0; x < grid_.width; ++x) {
            const double gx = static_cast<double>(static_cast<std::int64_t>(x) + grid_.originX);
            axis(((gx + 0.5) / k - left) / lp.scale - 0.5, png.width);
        }
        for (std::uint32_t y = 0; y < grid_.height; ++y) {
            const double gy = static_cast<double>(static_cast<std::int64_t>(y) + grid_.originY);
            axis(((gy + 0.5) / k - top) / lp.scale - 0.5, png.height);
        }
        auto axesStaged = r_.stage(axes.data(), axes.size() * sizeof(float));
        if (!axesStaged) return std::unexpected(axesStaged.error());
        RasterPush p{grid_.width, grid_.height, png.width, png.height};
        if (auto ok = r_.dispatch(r_.raster, {whole(*a), *staged, *axesStaged}, &p, groups16(grid_.width),
                                  groups16(grid_.height));
            !ok) {
            return std::unexpected(ok.error());
        }
        return art;
    }

    Result<SurfaceField> contourField(RenderCache* cache, const std::vector<FieldContour>& contours,
                                      std::uint32_t width, std::uint32_t height,
                                      const FieldOptions& fo, std::uint32_t ss,
                                      const FieldBands& bands) override {
        if (!r_.float64()) {
            // Sem double na GPU o campo e o de CPU.
            return cpuField(fieldFromContoursCached(cache, contours, width, height, fo, ss));
        }
        // A banda do acumulador vale na estreita, alargada ate a grade de
        // `kNearLattice` px: um pan curto cai no mesmo retangulo e acha o campo no
        // cache. Fora dela so a mascara le o campo (`FieldBands`).
        const gpu::FieldNear nearRect = nearOf(fo, width, height, bands.accumulator);
        // DUAS CHAVES. `content` e o que a mascara le -- o campo ate a banda da
        // arte, igual em qualquer retangulo --, e e ela que encadeia a arte
        // mascarada e a sombra: um pan que move o retangulo nao invalida a sombra.
        // `key` e o buffer inteiro, com o retangulo.
        CacheKey key, content;
        if (cache) {
            KeyHasher c("gpu-field-contours");
            hashField(c, contours, width, height, fo, ss);
            c.value(bands.art);
            content = c.finish();
            KeyHasher h("gpu-field-contours-near");
            hashKey(h, content);
            h.value(nearRect.band);
            h.value(nearRect.x0).value(nearRect.y0).value(nearRect.x1).value(nearRect.y1);
            key = h.finish();
            if (auto hit = cache->find<GpuCachedField>(key)) {
                SurfaceField f;
                f.resident = hit->data;
                f.width = hit->width;
                f.height = hit->height;
                f.originX = hit->originX;
                f.originY = hit->originY;
                f.key = content;
                f.keyed = true;
                return f;
            }
        }
        auto made = gpu::fieldFromContours(r_, contours, width, height, fo, ss, bands.art,
                                           nullptr, &nearRect);
        if (!made) return std::unexpected(made.error());
        SurfaceField f;
        f.resident = made->data;
        f.width = made->width;
        f.height = made->height;
        f.originX = made->originX;
        f.originY = made->originY;
        if (cache) {
            f.key = content;
            f.keyed = true;
            const std::size_t bytes = made->data ? made->data->size() : 0;
            cache->store(key, GpuCachedField{made->data, f.width, f.height, f.originX, f.originY},
                         bytes);
        }
        return f;
    }

    // O raster de vidro tem a copia de CPU de [UP3]; o campo dele segue na CPU.
    Result<SurfaceField> alphaField(RenderCache* cache, SurfaceArt& art, std::uint32_t width,
                                    std::uint32_t height, const FieldOptions& fo) override {
        // Com cache, o campo que subiu fica na GPU pela chave da arte: o proximo
        // render nem hasheia o alfa nem sobe nada.
        CacheKey key;
        const bool keyed = cache && art.keyed && r_.float64();
        if (keyed) {
            KeyHasher h("gpu-field-alpha");
            hashKey(h, art.key);
            hashField(h, {}, width, height, fo, 1);
            key = h.finish();
            if (auto hit = cache->find<GpuCachedField>(key)) {
                SurfaceField f;
                f.resident = hit->data;
                f.width = hit->width;
                f.height = hit->height;
                f.originX = hit->originX;
                f.originY = hit->originY;
                f.key = key;
                f.keyed = true;
                return f;
            }
        }
        auto onCpu = artOnCpu(art);
        if (!onCpu) return std::unexpected(onCpu.error());
        auto f = cpuField(fieldFromAlphaCached(cache, **onCpu, width, height, fo));
        if (!f) return f;
        if (keyed) {
            const Slab s = f->resident ? fieldSlab(*f) : Slab{};
            cache->store(key, GpuCachedField{s, f->width, f->height, f->originX, f->originY},
                         s ? s->size() : 0);
            f->key = key;
            f->keyed = true;
        }
        return f;
    }

    // A refracao (G4, era [RT2]): `icon_displace` e `icon_refract`, float como a CPU.
    Result<void> refract(const SurfaceField& field, const GlassRefraction& refraction) override {
        if (!r_.float64()) {
            // Sem double: o campo e de CPU e a refracao tambem (o alvo vai e volta).
            return onTarget([&](std::vector<float>& t) {
                glassOver(t, grid_, glassDisplacementMap(*field.cpu, refraction), refraction);
            });
        }
        const gpu::SourceView view = viewOf(field);
        return gpu::refract(r_, target(), fieldSlab(field), narrow_, refraction, &view);
    }

    // A mascara (G4, era [RT6]) e feita na passada que a aplica.
    Result<SurfaceMask> opacityMask(const SurfaceField& field,
                                    const OpacityMaskArguments& args) override {
        SurfaceMask m;
        m.field = field;
        m.args = args;
        return m;
    }

    Result<void> applyMask(SurfaceArt& art, const SurfaceMask& mask, MaskSink sink) override {
        if (!r_.float64()) {
            // Sem double: a arte desce, a mascara de CPU, a arte sobe.
            auto onCpu = artOnCpu(art);
            if (!onCpu) return std::unexpected(onCpu.error());
            std::vector<float> rgba = **onCpu;
            const OpacityMask m = glassOpacityMask(*mask.field.cpu, mask.args);
            std::size_t painted = 0;
            const std::size_t missed = opacityMaskMissedPixels(rgba, m, painted);
            applyOpacityMask(rgba, m);
            if (auto ok = r_.upload(artSlab(art), rgba.data(), rgba.size() * sizeof(float)); !ok) {
                return ok;
            }
            art.rgba = std::move(rgba);
            sink(missed, painted);
            return {};
        }
        if (art.shared) {
            // A arte e tambem a do cache: a mascara escreve numa copia.
            auto own = duplicate(artSlab(art));
            if (!own) return std::unexpected(own.error());
            art.resident = *own;
            art.shared = false;
        }
        const std::uint32_t slot = counterSlot(2);
        if (auto ok = gpu::glassMask(r_, artSlab(art), fieldSlab(mask.field), grid_.width,
                                     grid_.height, mask.field.originY, mask.args, counters_, slot);
            !ok) {
            return ok;
        }
        art.rgba.clear();   // a copia de CPU, se havia, ficou velha
        if (art.keyed && mask.field.keyed) {
            KeyHasher h("gpu-masked");
            hashKey(h, art.key);
            hashKey(h, mask.field.key);
            static_assert(sizeof(OpacityMaskArguments) == 36, "a mask argument is missing");
            h.value(mask.args.borderWidth).span(mask.args.opacityBounds, 2);
            h.span(mask.args.contourOpacityBounds, 2).span(mask.args.bounds, 4);
            h.value(mask.field.originY);
            art.key = h.finish();
        } else {
            art.keyed = false;
        }
        pending_.push_back(
            [this, slot, sink = std::move(sink)]() { sink(counted(slot + 1), counted(slot)); });
        return {};
    }

    // O especular (G4, era [RT3]): `icon_highlight` sobre o alvo.
    Result<void> specular(const SurfaceField& field, const SpecularArguments& args,
                          CountSink sink) override {
        std::size_t count = 0;
        const HighlightSlot* slots = glyphHighlightSlots(count);
        std::vector<double> records;
        const bool transcribed = gpu::resolveHighlights(slots, count, args, records);
        if (!r_.float64() || !transcribed) {
            SurfaceField copy = field;
            auto onCpu = fieldOnCpu(copy);
            if (!onCpu) return std::unexpected(onCpu.error());
            // O campo e da grade larga e o alvo da estreita: o pedaco dela (o campo
            // amostra absoluto, entao recortar e so mover o indice).
            std::shared_ptr<const FieldImage> fieldHere = *onCpu;
            if (!sameGrids()) {
                FieldImage part;
                part.width = narrow_.width;
                part.height = narrow_.height;
                part.originX = narrow_.originX;
                part.originY = narrow_.originY;
                part.rgba.assign(narrow_.texels() * 4, 0.0f);
                const gpu::SourceView v = viewOf(copy);
                copyRows((*onCpu)->rgba, v.width, v.x, v.y, part.rgba, narrow_.width, 0, 0,
                         narrow_.width, narrow_.height);
                fieldHere = std::make_shared<const FieldImage>(std::move(part));
            }
            std::size_t moved = 0;
            if (auto ok = onTarget([&](std::vector<float>& t) {
                    moved = drawSpecular(t, *fieldHere, args);
                });
                !ok) {
                return ok;
            }
            sink(moved);
            return {};
        }
        const std::uint32_t slot = counterSlot(1);
        const gpu::SourceView view = viewOf(field);
        if (auto ok = gpu::highlights(r_, target(), fieldSlab(field), narrow_.width, narrow_.height,
                                      records, false, args.useVCM, args.clampPlusLighter,
                                      counters_, slot, &view);
            !ok) {
            return ok;
        }
        pending_.push_back([this, slot, sink = std::move(sink)]() { sink(counted(slot)); });
        return {};
    }

    // [RT7] o campo desce -- so para a queda de CPU acima.
    Result<std::shared_ptr<const FieldImage>> fieldOnCpu(SurfaceField& field) {
        if (field.cpu) return field.cpu;
        FieldImage img;
        img.width = field.width;
        img.height = field.height;
        img.originX = field.originX;
        img.originY = field.originY;
        img.rgba.resize(static_cast<std::size_t>(field.width) * field.height * 4);
        const Slab s = fieldSlab(field);
        if (auto ok = r_.download(s, img.rgba.data(), img.rgba.size() * sizeof(float)); !ok) {
            return std::unexpected(ok.error());
        }
        field.cpu = std::make_shared<const FieldImage>(std::move(img));
        return field.cpu;
    }

    Result<void> blendArt(const SurfaceArt& art, float alpha, BlendMode mode,
                          bool clearContent) override {
        return blendRange(whole(artSlab(art)), alpha, mode, wideView(), clearContent ? 2u : 0u);
    }

    // A sombra da arte residente (G3): `icon_ring`, `icon_shadow`, `icon_blur`,
    // sem descer a arte e sem subir a sombra.
    Result<SurfaceShadow> makeShadow(RenderCache* cache, SurfaceArt& art, ShadowStyle style,
                                     const ShadowGeometry& geometry,
                                     double overdrawAlpha) override {
        SurfaceShadow s;
        if (!r_.float64()) {
            // Sem double na GPU a sombra e a de CPU: a arte desce, a sombra sobe.
            auto onCpu = artOnCpu(art);
            if (!onCpu) return std::unexpected(onCpu.error());
            s.image = shadowImageCached(cache, **onCpu, grid_.width, grid_.height, style,
                                        geometry);
            if (overdrawAlpha > 0.0) {
                s.overdraw = shadowOverdrawImage(*s.image, **onCpu, grid_.width, grid_.height,
                                                 overdrawAlpha);
            }
            s.hasOverdraw = !s.overdraw.empty();
            return s;
        }
        // Com cache, a sombra DESFOCADA (a parte cara, na grade larga) fica na GPU
        // pela chave da arte como ela esta agora (a do render, encadeada com a da
        // mascara quando houve uma). A translacao e o overdraw sao refeitos a cada
        // render, na grade estreita -- sao duas passadas por pixel, e a estreita
        // muda a cada pan.
        CacheKey key;
        const bool keyed = cache && art.keyed;
        if (keyed) {
            KeyHasher h("gpu-shadow");
            hashKey(h, art.key);
            h.value(grid_.width).value(grid_.height).value(style);
            static_assert(sizeof(ShadowGeometry) == 40, "a ShadowGeometry field is missing");
            h.value(geometry.offsetX).value(geometry.offsetY).value(geometry.blurRadius);
            h.value(geometry.ringWidth.has_value());
            if (geometry.ringWidth) h.value(*geometry.ringWidth);
            key = h.finish();
        }
        std::optional<gpu::ShadowBlur> blur;
        if (keyed) {
            if (auto hit = cache->find<GpuCachedShadow>(key)) blur = gpu::ShadowBlur{hit->image, hit->blurred};
        }
        if (!blur) {
            auto made = gpu::shadowBlur(r_, artSlab(art), grid_.width, grid_.height, style, geometry);
            if (!made) return std::unexpected(made.error());
            blur = *made;
            if (keyed) cache->store(key, GpuCachedShadow{blur->image, blur->blurred}, blur->image->size());
        }
        auto made = gpu::shadowPlace(r_, *blur, artSlab(art), grid_.width, grid_.height, geometry,
                                     overdrawAlpha,
                                     gpu::ShadowOutput{narrow_.width, narrow_.height, dx(), dy()});
        if (!made) return std::unexpected(made.error());
        s.residentImage = made->image;
        s.residentOverdraw = made->overdraw;
        s.hasOverdraw = made->overdraw != nullptr;
        return s;
    }

    Result<void> blendShadow(const SurfaceShadow& shadow, bool overdraw, float alpha,
                             BlendMode mode) override {
        const std::shared_ptr<void>& slab = overdraw ? shadow.residentOverdraw : shadow.residentImage;
        if (slab) {
            // A sombra residente ja e da grade estreita (`shadowPlace`).
            return blendRange(whole(std::static_pointer_cast<Buffer>(slab)), alpha, mode,
                              gpu::SourceView{narrow_.width, 0, 0});
        }
        const std::vector<float>& img = overdraw ? shadow.overdraw : *shadow.image;
        auto staged = r_.stage(img.data(), img.size() * sizeof(float));
        if (!staged) return std::unexpected(staged.error());
        return blendRange(*staged, alpha, mode, wideView());
    }

    Result<std::vector<float>> finish(std::int32_t cropX, std::int32_t cropY, std::uint32_t viewW,
                                      std::uint32_t viewH) override {
        const std::size_t n = static_cast<std::size_t>(viewW) * viewH * 4;
        std::vector<float> rgba(n, 0.0f);
        if (n == 0) {
            if (auto ok = resolveCounts(); !ok) return std::unexpected(ok.error());
            return rgba;
        }
        auto out = r_.acquire(n * sizeof(float));
        if (!out) return std::unexpected(out.error());
        // O recorte vem na grade larga; o acumulador e a estreita.
        FinishPush p{viewW, viewH, narrow_.width, cropX - dx(), cropY - dy()};
        if (auto ok = r_.dispatch(r_.finish, {whole(acc_), whole(*out)}, &p, groups16(viewW),
                                  groups16(viewH));
            !ok) {
            return std::unexpected(ok.error());
        }
        // O UNICO readback do caminho residente quando nenhuma etapa de vidro pede
        // a CPU -- e, com ele, as contagens.
        if (auto ok = r_.download(*out, rgba.data(), n * sizeof(float)); !ok) {
            return std::unexpected(ok.error());
        }
        if (auto ok = resolveCounts(); !ok) return std::unexpected(ok.error());
        return rgba;
    }

private:
    // Uma ida e volta do alvo, para as quedas de CPU (aparelho sem double, ou um
    // modo de mescla de realce que `icon_highlight` nao transcreve).
    Result<void> onTarget(const std::function<void(std::vector<float>&)>& step) {
        std::vector<float> host(narrow_.texels() * 4);
        const Slab& t = target();
        if (auto ok = r_.download(t, host.data(), bytes()); !ok) return ok;
        step(host);
        return r_.upload(t, host.data(), bytes());
    }

    // Um buffer novo com o conteudo de `s` (gravado no lote).
    Result<Slab> duplicate(const Slab& s) {
        auto d = r_.acquire(s->size());
        if (!d) return d;
        r_.copy(whole(s), *d);
        return d;
    }

    std::uint32_t counterSlot(std::uint32_t words) {
        const std::uint32_t at = nextCounter_;
        nextCounter_ += words;
        return at;
    }
    std::size_t counted(std::uint32_t slot) const {
        return slot < counts_.size() ? counts_[slot] : 0;
    }
    Result<void> resolveCounts() {
        if (pending_.empty()) return {};
        if (nextCounter_ > kCounterWords) {
            return std::unexpected(std::string("contagens demais num render residente"));
        }
        counts_.assign(kCounterWords, 0);
        if (auto ok = r_.download(counters_, counts_.data(), kCounterWords * 4); !ok) return ok;
        for (auto& resolve : pending_) resolve();
        pending_.clear();
        return {};
    }

    // A arte na CPU: a copia de [UP3] quando ha, senao um readback.
    Result<const std::vector<float>*> artOnCpu(SurfaceArt& art) {
        if (art.rgba.empty()) {
            const Slab s = artSlab(art);
            art.rgba.resize(s->size() / sizeof(float));
            if (auto ok = r_.download(s, art.rgba.data(), s->size()); !ok) {
                return std::unexpected(ok.error());
            }
        }
        return &art.rgba;
    }

    // Um campo feito na CPU (o de um raster, [UP4]; ou qualquer um sem double)
    // sobe uma vez, para os consumidores residentes.
    Result<SurfaceField> cpuField(std::shared_ptr<const FieldImage> image) {
        SurfaceField f;
        f.width = image->width;
        f.height = image->height;
        f.originX = image->originX;
        f.originY = image->originY;
        if (f.width != 0) {
            auto s = r_.acquire(image->rgba.size() * sizeof(float));
            if (!s) return std::unexpected(s.error());
            if (auto ok = r_.upload(*s, image->rgba.data(), image->rgba.size() * sizeof(float));
                !ok) {
                return std::unexpected(ok.error());
            }
            f.resident = *s;
        }
        f.cpu = std::move(image);
        return f;
    }

    // O acumulador e o alvo de grupo moram na grade estreita; a arte, o campo e a
    // sombra na larga.
    VkDeviceSize bytes() const { return static_cast<VkDeviceSize>(narrow_.texels()) * 16; }
    VkDeviceSize wideBytes() const { return static_cast<VkDeviceSize>(grid_.texels()) * 16; }
    // A estreita no campo (origem `fo`), alargada ate a grade de `kNearLattice`.
    gpu::FieldNear nearOf(const FieldOptions& fo, std::uint32_t width, std::uint32_t height,
                          float band) const {
        constexpr std::int32_t L = static_cast<std::int32_t>(kNearLattice);
        auto down = [](std::int32_t v) { return (v >= 0 ? v : v - (L - 1)) / L * L; };
        auto up = [&](std::int32_t v) { return down(v + L - 1); };
        gpu::FieldNear n;
        n.x0 = std::max(0, down(narrow_.originX) - fo.originX);
        n.y0 = std::max(0, down(narrow_.originY) - fo.originY);
        n.x1 = std::min(static_cast<std::int32_t>(width),
                        up(narrow_.originX + static_cast<std::int32_t>(narrow_.width)) - fo.originX);
        n.y1 = std::min(static_cast<std::int32_t>(height),
                        up(narrow_.originY + static_cast<std::int32_t>(narrow_.height)) - fo.originY);
        n.band = band;
        return n;
    }
    bool sameGrids() const {
        return narrow_.originX == grid_.originX && narrow_.originY == grid_.originY &&
               narrow_.width == grid_.width && narrow_.height == grid_.height;
    }
    std::int32_t dx() const { return narrow_.originX - grid_.originX; }
    std::int32_t dy() const { return narrow_.originY - grid_.originY; }
    gpu::SourceView wideView() const { return gpu::SourceView{grid_.width, dx(), dy()}; }
    gpu::SourceView viewOf(const SurfaceField& f) const {
        return gpu::SourceView{f.width, narrow_.originX - f.originX, narrow_.originY - f.originY};
    }
    static void copyRows(const std::vector<float>& from, std::uint32_t fromW, std::int32_t fx,
                         std::int32_t fy, std::vector<float>& to, std::uint32_t toW,
                         std::int32_t tx, std::int32_t ty, std::uint32_t w, std::uint32_t h) {
        for (std::uint32_t y = 0; y < h; ++y) {
            const std::size_t s = ((static_cast<std::size_t>(y) + fy) * fromW + fx) * 4;
            const std::size_t d = ((static_cast<std::size_t>(y) + ty) * toW + tx) * 4;
            std::copy(from.begin() + s, from.begin() + s + w * 4, to.begin() + d);
        }
    }
    const Slab& target() const { return group_ ? group_ : acc_; }

    // `source`: 0 a arte reta, 2 a arte reta pela matriz do conteudo do Clear.
    Result<void> blendRange(Range src, float alpha, BlendMode mode, const gpu::SourceView& sv,
                            std::uint32_t source = 0u) {
        BlendPush p{narrow_.width, narrow_.height, static_cast<std::uint32_t>(mode), source, alpha,
                    sv.width, sv.x, sv.y};
        return r_.dispatch(r_.blend, {whole(target()), src}, &p, groups16(narrow_.width),
                           groups16(narrow_.height));
    }

    Device& device_;
    Resident& r_;
    PixelGrid grid_;     // o buffer do render (a grade larga)
    PixelGrid narrow_;   // a estreita: o recorte mais o alcance encadeado
    Slab acc_;
    Slab group_;
    Slab counters_;
    std::uint32_t nextCounter_ = 0;
    std::vector<std::uint32_t> counts_;
    std::vector<std::function<void()>> pending_;
};

// ---- o perfil (IC_GPU_PROFILE) ----------------------------------------------------
//
// Para MEDIR onde vai o tempo de um render residente: cada chamada da superficie
// esvazia o lote (`flush`) ao sair, entao o relogio de cada metodo e a CPU dele
// MAIS a GPU que ele gravou. Com o perfil o render e mais lento (uma espera por
// passo em vez de uma so); o que ele mede e a PARTILHA. Desligado, nada disto roda.
class ProfiledSurface final : public IconSurface {
    struct Spent {
        double ms = 0.0;
        int calls = 0;
    };
    template <class F>
    auto time(const char* name, F&& f) {
        const auto t0 = std::chrono::steady_clock::now();
        auto result = f();
        (void)r_.flush();
        Spent& s = spent_[name];
        s.ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count();
        ++s.calls;
        return result;
    }
public:
    ProfiledSurface(IconSurface& inner, Resident& r) : in_(inner), r_(r) {}
    ~ProfiledSurface() override {
        double total = 0.0;
        for (const auto& [name, t] : spent_) total += t.ms;
        std::fprintf(stderr, "[perfil] %-18s %8.2f ms\n", "total", total);
        for (const auto& [name, t] : spent_) {
            std::fprintf(stderr, "[perfil] %-18s %8.2f ms  x%d\n", name.c_str(), t.ms, t.calls);
        }
    }

    std::uint32_t bufferLattice() const override { return in_.bufferLattice(); }
    Result<void> begin(const PixelGrid& g, const PixelGrid& n) override {
        std::fprintf(stderr, "[perfil] buffer %ux%u origem (%d,%d) canvas %u\n", g.width,
                     g.height, g.originX, g.originY, g.size);
        std::fprintf(stderr, "[perfil] estreita %ux%u origem (%d,%d)\n", n.width, n.height,
                     n.originX, n.originY);
        return time("begin", [&] { return in_.begin(g, n); });
    }
    Result<void> paintBackground(const FillOverride& p) override {
        return time("paintBackground", [&] { return in_.paintBackground(p); });
    }
    Result<void> clipToChiclet(IconPlatform p) override {
        return time("clipToChiclet", [&] { return in_.clipToChiclet(p); });
    }
    Result<void> chicletHighlights(RenderCache* c, const SpecularArguments& a, IconPlatform p,
                                   CountSink s) override {
        return time("chicletHighlights", [&] { return in_.chicletHighlights(c, a, p, s); });
    }
    Result<void> beginGroup(bool i) override {
        return time("beginGroup", [&] { return in_.beginGroup(i); });
    }
    Result<void> endGroup(std::optional<BlendMode> b) override {
        return time("endGroup", [&] { return in_.endGroup(b); });
    }
    Result<SurfaceArt> drawSvg(RenderCache* c, const std::string& t,
                               const icf::svg::SvgDocument& s, const PathGlobals& p,
                               const RenderOptions& o) override {
        return time("drawSvg", [&] { return in_.drawSvg(c, t, s, p, o); });
    }
    Result<SurfaceArt> placeRaster(RenderCache* c, const icf::DecodedPng& png,
                                   const LayerPlacement& lp, bool f) override {
        return time("placeRaster", [&] { return in_.placeRaster(c, png, lp, f); });
    }
    Result<SurfaceField> contourField(RenderCache* c, const std::vector<FieldContour>& k,
                                      std::uint32_t w, std::uint32_t h, const FieldOptions& o,
                                      std::uint32_t ss, const FieldBands& b) override {
        return time("contourField", [&] { return in_.contourField(c, k, w, h, o, ss, b); });
    }
    Result<SurfaceField> alphaField(RenderCache* c, SurfaceArt& a, std::uint32_t w,
                                    std::uint32_t h, const FieldOptions& o) override {
        return time("alphaField", [&] { return in_.alphaField(c, a, w, h, o); });
    }
    Result<void> refract(const SurfaceField& f, const GlassRefraction& g) override {
        return time("refract", [&] { return in_.refract(f, g); });
    }
    Result<SurfaceMask> opacityMask(const SurfaceField& f, const OpacityMaskArguments& a) override {
        return time("opacityMask", [&] { return in_.opacityMask(f, a); });
    }
    Result<void> applyMask(SurfaceArt& a, const SurfaceMask& m, MaskSink s) override {
        return time("applyMask", [&] { return in_.applyMask(a, m, s); });
    }
    Result<void> specular(const SurfaceField& f, const SpecularArguments& a,
                          CountSink s) override {
        return time("specular", [&] { return in_.specular(f, a, s); });
    }
    Result<void> blendArt(const SurfaceArt& a, float al, BlendMode m, bool cc) override {
        return time("blendArt", [&] { return in_.blendArt(a, al, m, cc); });
    }
    Result<SurfaceShadow> makeShadow(RenderCache* c, SurfaceArt& a, ShadowStyle s,
                                     const ShadowGeometry& g, double o) override {
        return time("makeShadow", [&] { return in_.makeShadow(c, a, s, g, o); });
    }
    Result<void> blendShadow(const SurfaceShadow& s, bool o, float a, BlendMode m) override {
        return time("blendShadow", [&] { return in_.blendShadow(s, o, a, m); });
    }
    Result<std::vector<float>> finish(std::int32_t x, std::int32_t y, std::uint32_t w,
                                      std::uint32_t h) override {
        return time("finish", [&] { return in_.finish(x, y, w, h); });
    }

private:
    IconSurface& in_;
    Resident& r_;
    std::map<std::string, Spent> spent_;
};

}  // namespace

Result<RenderedIcon> renderIconGpu(Device& device, const icf::IconBundle& bundle,
                                   IconRenderOptions options) {
    auto resident = gpu::Resident::of(device);
    if (!resident) return std::unexpected(resident.error());
    Resident& r = **resident;
    std::lock_guard<std::mutex> lock(r.mutex());
    static const bool profiled = std::getenv("IC_GPU_PROFILE") != nullptr;
    Result<RenderedIcon> out = [&]() {
        GpuSurface surface(device, r);
        if (profiled) {
            ProfiledSurface p(surface, r);
            return renderIconOn(p, bundle, options);
        }
        return renderIconOn(surface, bundle, options);
    }();
    // O que um erro deixou gravado roda (e e descartado) aqui, para o lote
    // comecar vazio no proximo render.
    auto flushed = r.flush();
    r.trim();
    if (out && !flushed) return std::unexpected(flushed.error());
    return out;
}

}  // namespace rb
