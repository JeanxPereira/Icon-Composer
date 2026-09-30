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
// A UNICA PORTA PARA A CPU E `onTarget` (mais `artOnCpu` para a arte). Tudo que
// ainda roda na CPU sobre o acumulador -- a refracao (`glassOver`), o especular
// (`drawSpecular`) e os realces da pastilha (`drawChicletHighlights`) -- passa
// por ela; na GPU isso e uma ida e volta (readback + upload) DECLARADA, e as
// tasks G2-G4 do plano existem para tira-las.
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
    bool empty() const { return width == 0; }
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

    // Um passo de CPU sobre o ALVO corrente (o acumulador, ou o do grupo quando
    // o grupo e isolado), pre-multiplicado, no lugar. Na GPU: readback, passo,
    // upload.
    virtual Result<void> onTarget(const std::function<void(std::vector<float>&)>& step) = 0;

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
    virtual Result<SurfaceArt> placeRaster(const icf::DecodedPng& png,
                                           const LayerPlacement& placement,
                                           bool feedsField) = 0;

    // A arte na CPU, para quem ainda so existe la: o campo de um raster e a
    // sombra. Na GPU: readback.
    virtual Result<const std::vector<float>*> artOnCpu(SurfaceArt& art) = 0;

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
    // O campo na CPU, para quem ainda so existe la. Na GPU: readback.
    virtual Result<std::shared_ptr<const FieldImage>> fieldOnCpu(SurfaceField& field) = 0;

    // `opacityMaskMissedPixels` seguido de `applyOpacityMask`: devolve os
    // perdidos e escreve os pintados em `painted`.
    virtual Result<std::size_t> applyMask(SurfaceArt& art, const OpacityMask& mask,
                                          std::size_t& painted) = 0;

    // `blendOver` no alvo corrente: arte de camada, ou uma imagem feita na CPU
    // (a sombra e o seu overdraw).
    virtual Result<void> blendArt(const SurfaceArt& art, float alpha, BlendMode mode) = 0;
    virtual Result<void> blendImage(const std::vector<float>& straight, float alpha,
                                    BlendMode mode) = 0;

    // O recorte pedido, des-multiplicado: o `RenderedIcon::rgba`.
    virtual Result<std::vector<float>> finish(std::int32_t cropX, std::int32_t cropY,
                                              std::uint32_t width, std::uint32_t height) = 0;
};

// Os campos com o `RenderCache` de IconRenderer.cpp (nulo = sem cache).
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
