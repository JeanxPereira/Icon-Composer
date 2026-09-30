#pragma once
// ONDE OS PIXELS DE `renderIcon` MORAM, SEPARADO DE QUEM DECIDE O QUE DESENHAR.
//
// POR QUE ISTO EXISTE (frente GPU, G1 -- `Docs/Plans/2026-09-29-render-gpu.md`)
// -----------------------------------------------------------------------------
// `renderIcon` e mil linhas de DECISAO -- que grupo mescla, que camada pula e por
// que, que nota vai para o relatorio, em que ordem vidro, sombra, arte e realce
// entram -- e umas vinte chamadas que MEXEM EM PIXEL. O caminho residente
// (`renderIconGpu`) tem que tomar exatamente as mesmas decisoes, com os mesmos
// `skipped`, `shapeGaps` e `notes`, e mexer nos pixels em outro lugar. Duas copias
// das mil linhas se separariam no primeiro commit que tocasse uma delas.
//
// Entao a decisao ficou num lugar so (`renderIconOn`, em IconRenderer.cpp) e as
// vinte chamadas viraram esta interface. `CpuSurface` (no mesmo arquivo) e cada
// metodo chamando A MESMA funcao que a linha correspondente chamava antes, com os
// mesmos argumentos -- o gate de SHA do corpus prova que nenhum byte andou. A
// `GpuSurface` (IconRendererGpu.cpp) guarda o acumulador e a arte em buffers da
// GPU e so desce para a CPU onde a interface diz.
//
// NAO HA PORTA PARA A CPU NA INTERFACE desde G4: cada etapa de vidro -- o campo,
// a sombra, a mascara, a refracao, o especular, os realces da pastilha -- e um
// metodo, e a `GpuSurface` faz cada uma sobre os buffers dela. O que a GPU ainda
// faz na CPU (a cobertura de um traco, um SVG com filtro, o campo de um raster)
// sobe e esta declarado em IconRendererGpu.cpp.
//
// AS CONTAGENS CHEGAM DEPOIS. Tres decisoes dependem de um numero que so a
// passada conhece -- quantos pixels um realce mudou, quantos a mascara pintou e
// perdeu. Na CPU o numero existe na hora; na GPU ele desce UMA vez, no `finish`.
// Entao esses metodos entregam o numero a um `sink`, que a CPU chama na hora e
// a GPU chama no fim, e `renderIconOn` guarda o lugar da nota ou da lacuna que
// o numero decide (IconRenderer.cpp, `kPendingEntry`).
//
// Interno: so IconRenderer.cpp e IconRendererGpu.cpp incluem.
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/BlendMode.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/RenderBox/SvgRenderer.h"

namespace rb {

// A arte de UMA camada, ja desenhada e ainda nao composta: RGBA reto (nao
// pre-multiplicado), a grade do buffer.
struct SurfaceArt {
    // Na CPU e a arte. Na GPU fica vazio ate alguem pedir `artOnCpu`.
    std::vector<float> rgba;
    // Na GPU, o buffer; opaco para quem nao e a `GpuSurface`.
    std::shared_ptr<void> resident;
    // O que o render do SVG nao desenhou, com o indice da forma.
    std::vector<SkippedShape> skipped;
    // A GPU com cache: a chave de conteudo desta arte COMO ELA ESTA AGORA -- a do
    // render (ou da colocacao), e depois da mascara a mesma encadeada com a do
    // campo e os argumentos. A sombra se guarda por ela. A CPU nao usa.
    CacheKey key;
    bool keyed = false;
    // A GPU: o buffer tambem e o valor de uma entrada do cache, entao quem vai
    // ESCREVER nele (a mascara) copia antes.
    bool shared = false;
};

// O campo de distancia de UMA camada de vidro, onde a superficie o guarda. Na CPU
// e o `FieldImage`; na GPU e um buffer (`cpu` so e preenchido por `fieldOnCpu`).
// `width == 0` e o campo vazio -- o `FieldImage` vazio de sempre, que vira nota.
struct SurfaceField {
    std::shared_ptr<const FieldImage> cpu;
    std::shared_ptr<void> resident;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    // A GPU com cache: a chave de conteudo do campo (ver `SurfaceArt::key`).
    CacheKey key;
    bool keyed = false;
    bool empty() const { return width == 0; }
};

// A mascara de translucidez de UMA camada: na CPU, `glassOpacityMask`; na GPU, o
// campo e os argumentos (a mascara e feita na passada que a aplica).
struct SurfaceMask {
    std::shared_ptr<const OpacityMask> cpu;
    SurfaceField field;
    OpacityMaskArguments args;
};

// Onde uma contagem da GPU chega (ver o topo deste arquivo).
using CountSink = std::function<void(std::size_t)>;
using MaskSink = std::function<void(std::size_t missed, std::size_t painted)>;

// A sombra de UMA camada e o seu overdraw, feitos da arte ja mascarada. Na CPU
// sao as imagens de `shadowImageCached`/`shadowOverdrawImage`; na GPU, buffers.
struct SurfaceShadow {
    std::shared_ptr<const std::vector<float>> image;
    std::vector<float> overdraw;
    std::shared_ptr<void> residentImage;
    std::shared_ptr<void> residentOverdraw;
    bool hasOverdraw = false;
};

class IconSurface {
public:
    virtual ~IconSurface() = default;

    // O acumulador, pre-multiplicado e zerado, na grade do buffer.
    virtual Result<void> begin(const PixelGrid& grid) = 0;

    // O fundo ESCRITO (nao composto) no acumulador ainda vazio, e o recorte a
    // pastilha. `paintBackground` e `clipToChiclet` de IconRenderer.cpp e
    // ChicletShape.cpp.
    virtual Result<void> paintBackground(const FillOverride& paint) = 0;
    virtual Result<void> clipToChiclet(IconPlatform platform) = 0;

    // Os realces da pastilha sobre o alvo corrente: `chicletHighlightsCached`.
    // `sink` recebe os pixels mudados.
    virtual Result<void> chicletHighlights(RenderCache* cache, const SpecularArguments& args,
                                           IconPlatform platform, CountSink sink) = 0;

    // Um grupo cuja mescla nao e `normal` desenha num alvo proprio e so no fim
    // entra no acumulador, com `blendPremulOver`. `endGroup` recebe o modo
    // quando o grupo foi isolado e nullopt quando nao.
    virtual Result<void> beginGroup(bool isolated) = 0;
    virtual Result<void> endGroup(std::optional<BlendMode> blend) = 0;

    // A arte de uma camada. `drawSvg` e `svgRenderCached` (o texto e a chave de
    // cache); `placeRaster` e a colocacao bilinear de IconRenderer.cpp.
    // `feedsField` diz que o vidro vai construir o campo a partir do ALFA desta
    // arte: a GPU entao coloca na CPU (a conta exata, em double) e guarda a
    // copia em `rgba`, porque o limiar `alpha >= 0.5` do campo nao tolera o ulp
    // de diferenca da colocacao em float -- medido, IconRendererGpu.cpp.
    virtual Result<SurfaceArt> drawSvg(RenderCache* cache, const std::string& text,
                                       const icf::svg::SvgDocument& svg,
                                       const PathGlobals& placement,
                                       const RenderOptions& options) = 0;
    virtual Result<SurfaceArt> placeRaster(RenderCache* cache, const icf::DecodedPng& png,
                                           const LayerPlacement& placement,
                                           bool feedsField) = 0;

    // O campo de uma camada de vidro. `contourField` e `fieldFromContoursCached`
    // (os contornos de `flattenSvgToContours`); `alphaField` e
    // `fieldFromAlphaCached` sobre o alfa da arte colocada. Na GPU o primeiro e
    // `icon_field.comp` e o segundo segue na CPU (a arte de um raster de vidro ja
    // tem a copia de CPU, [UP3]).
    virtual Result<SurfaceField> contourField(RenderCache* cache,
                                              const std::vector<FieldContour>& contours,
                                              std::uint32_t width, std::uint32_t height,
                                              const FieldOptions& options,
                                              std::uint32_t superSample) = 0;
    virtual Result<SurfaceField> alphaField(RenderCache* cache, SurfaceArt& art,
                                            std::uint32_t width, std::uint32_t height,
                                            const FieldOptions& options) = 0;
    // A refracao do alvo corrente pelo campo:
    // `glassOver(alvo, grade, glassDisplacementMap(campo, r), r)`.
    virtual Result<void> refract(const SurfaceField& field, const GlassRefraction& refraction) = 0;

    // `glassOpacityMask`, e depois `opacityMaskMissedPixels` seguido de
    // `applyOpacityMask` sobre a arte; `sink` recebe os perdidos e os pintados.
    virtual Result<SurfaceMask> opacityMask(const SurfaceField& field,
                                            const OpacityMaskArguments& args) = 0;
    virtual Result<void> applyMask(SurfaceArt& art, const SurfaceMask& mask, MaskSink sink) = 0;

    // `drawSpecular` sobre o alvo corrente; `sink` recebe os pixels mudados.
    virtual Result<void> specular(const SurfaceField& field, const SpecularArguments& args,
                                  CountSink sink) = 0;

    // `blendOver` no alvo corrente, da arte de uma camada.
    virtual Result<void> blendArt(const SurfaceArt& art, float alpha, BlendMode mode) = 0;

    // A sombra de `art` (`shadowImageCached`) e, com `overdrawAlpha > 0`, o
    // overdraw dela (`shadowOverdrawImage`), feitos de uma vez porque os dois
    // leem a arte como ela e AGORA. `blendShadow` compoe uma das duas no alvo
    // corrente com `blendOver`. Na GPU: `icon_ring`, `icon_shadow`, `icon_blur`.
    virtual Result<SurfaceShadow> makeShadow(RenderCache* cache, SurfaceArt& art,
                                             ShadowStyle style, const ShadowGeometry& geometry,
                                             double overdrawAlpha) = 0;
    virtual Result<void> blendShadow(const SurfaceShadow& shadow, bool overdraw, float alpha,
                                     BlendMode mode) = 0;

    // O recorte pedido, des-multiplicado: o `RenderedIcon::rgba`.
    virtual Result<std::vector<float>> finish(std::int32_t cropX, std::int32_t cropY,
                                              std::uint32_t width, std::uint32_t height) = 0;
};

// Os campos e a sombra com o `RenderCache` de IconRenderer.cpp (nulo = sem cache).
std::shared_ptr<const std::vector<float>> shadowImageCached(
    RenderCache* cache, const std::vector<float>& art, std::uint32_t width,
    std::uint32_t height, ShadowStyle style, const ShadowGeometry& geometry);
std::shared_ptr<const FieldImage> fieldFromContoursCached(
    RenderCache* cache, const std::vector<FieldContour>& contours, std::uint32_t width,
    std::uint32_t height, const FieldOptions& fo, std::uint32_t superSample);
std::shared_ptr<const FieldImage> fieldFromAlphaCached(RenderCache* cache,
                                                       const std::vector<float>& rgba,
                                                       std::uint32_t width, std::uint32_t height,
                                                       const FieldOptions& fo);

// `placeRaster` de IconRenderer.cpp, a colocacao de CPU, para a superficie da GPU.
std::vector<float> placeRasterOnCpu(const icf::DecodedPng& png, const LayerPlacement& placement,
                                    const PixelGrid& grid);

// As decisoes de `renderIcon`, desenhadas em `surface`.
Result<RenderedIcon> renderIconOn(IconSurface& surface, const icf::IconBundle& bundle,
                                  IconRenderOptions options);

}  // namespace rb
