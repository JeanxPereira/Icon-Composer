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
#include "Source/RenderBox/GlassGlow.h"
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

// A arte anda por referencia entre a passada dos elementos e a imagem do grupo:
// a de um grupo de UM elemento e a propria arte dele (o alias), e copia-la so
// para trocar de dono custaria o buffer inteiro.
using SurfaceArtRef = std::shared_ptr<SurfaceArt>;

// A IMAGEM DE UM GRUPO, em construcao. `[BIN]` O alvo desenha todos os elementos
// de um grupo num display list so e o rasteriza SOZINHO (IconRendering 0x1A9D0,
// 0x13590): a opacidade e a mescla de cada elemento entram ali, contra os
// elementos ANTERIORES do mesmo grupo sobre o transparente.
//
// Os elementos chegam um a um (`addToGroupImage`), de tras para a frente. O
// primeiro so e GUARDADO: um grupo de um elemento nunca aloca um alvo, e a imagem
// dele e a arte do elemento, com a opacidade ainda por aplicar -- e isso que
// mantem a aritmetica dos grupos de uma camada onde ela estava. O alvo
// (pre-multiplicado, zerado, na grade do buffer) nasce com o segundo.
struct SurfaceGroupImage {
    std::size_t count = 0;
    SurfaceArtRef first;
    double firstOpacity = 1.0;
    // Na CPU, o alvo; na GPU, o buffer dele (na grade LARGA: a imagem alimenta a
    // sombra e a mascara, que moram nela).
    std::vector<float> target;
    std::shared_ptr<void> resident;
    // A GPU com cache: o que entrou, para a chave da imagem pronta.
    struct Part {
        CacheKey key;
        float alpha = 1.0f;
        BlendMode mode = BlendMode::Normal;
    };
    std::vector<Part> parts;
    bool keyed = true;
};

// A imagem pronta e a opacidade que ainda falta nela: a do unico elemento, no
// alias; 1 quando ha um alvo (as opacidades ja estao dentro dele).
struct SurfaceGroupTaken {
    SurfaceArtRef image;
    double opacity = 1.0;
};

// O campo de distancia de UM elemento de vidro -- ou o do grupo, que e o
// empilhamento deles --, onde a superficie o guarda. Na CPU
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

// A mascara de translucidez de UM grupo: na CPU, `glassOpacityMask`; na GPU, o
// campo e os argumentos (a mascara e feita na passada que a aplica).
struct SurfaceMask {
    std::shared_ptr<const OpacityMask> cpu;
    SurfaceField field;
    OpacityMaskArguments args;
};

// ATE ONDE UM CAMPO PRECISA SER EXATO, em pixels do contorno: alem disso quem o
// le satura, e qualquer valor alem da banda (com o sinal certo) da o mesmo pixel.
// 0 e exato em todo lugar. `accumulator` vale onde o acumulador le o campo (o
// especular, a refracao: a grade estreita de `begin`); `art` no resto do buffer,
// onde so a mascara de translucidez o le.
struct FieldBands {
    float accumulator = 0.0f;
    float art = 0.0f;
};

// Onde uma contagem da GPU chega (ver o topo deste arquivo).
using CountSink = std::function<void(std::size_t)>;
using MaskSink = std::function<void(std::size_t missed, std::size_t painted)>;

// A sombra de UM grupo e o seu overdraw. A sombra sai da FONTE (antes da
// mascara); o overdraw, dela e do conteudo ja mascarado. Na CPU sao as imagens de
// `shadowImageCached`/`shadowOverdrawImage`; na GPU, buffers.
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
    //
    // `narrow` (contida em `grid`) e onde o ACUMULADOR precisa estar certo: o
    // recorte pedido mais o alcance encadeado das refracoes (ViewportPlan.h). O
    // resto do buffer so existe para a sombra -- a arte, o campo que a mascara
    // le e a propria sombra precisam dele; o acumulador nao. A CPU ignora (o
    // acumulador dela e o buffer inteiro, o gabarito). A GPU guarda o acumulador
    // na estreita: no zoom profundo a margem da sombra e ~9x a area do recorte.
    //
    // `params` e o bloco da geracao deste render (`RenderingParameters.h`), que
    // vive mais que a superficie: os passos cujo parametro nao chega por
    // argumento (a imagem da sombra le dele o `Shadow` da geracao) o leem
    // dali.
    virtual Result<void> begin(const PixelGrid& grid, const PixelGrid& narrow,
                               const RenderingParameters& params) = 0;

    // A grade em que as bordas do buffer de um ladrilho caem (`planViewport`,
    // `lattice`). 0: o plano de sempre -- a CPU.
    virtual std::uint32_t bufferLattice() const { return 0; }

    // O fundo ESCRITO (nao composto) no acumulador ainda vazio, e o recorte a
    // pastilha. `paintBackground` e `clipToChiclet` de IconRenderer.cpp e
    // ChicletShape.cpp.
    virtual Result<void> paintBackground(const FillOverride& paint) = 0;
    virtual Result<void> clipToChiclet(IconPlatform platform) = 0;

    // Os realces da pastilha sobre o alvo corrente: `chicletHighlightsCached`.
    // `sink` recebe os pixels mudados.
    virtual Result<void> chicletHighlights(RenderCache* cache, const SpecularArguments& args,
                                           IconPlatform platform, CountSink sink) = 0;

    // A imagem de um grupo (`SurfaceGroupImage`). `addToGroupImage` e `blendOver`
    // de IconRenderer.cpp no alvo do GRUPO, com a opacidade e a mescla do
    // elemento; quem chama passa `Normal` para o primeiro, que nao encontra nada.
    // `takeGroupImage` devolve a arte do unico elemento ou o alvo des-multiplicado
    // (`rgb = a > 0 ? c / a : 0`, a conta do `finish`) como uma arte RETA, que e
    // o que a mascara, a sombra e `blendArt` leem. Na GPU: `icon_group.comp`.
    virtual Result<void> addToGroupImage(SurfaceGroupImage& group, const SurfaceArtRef& art,
                                         double opacity, BlendMode mode) = 0;
    virtual Result<SurfaceGroupTaken> takeGroupImage(SurfaceGroupImage& group) = 0;

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
    // Os pixels de uma arte na CPU (RGBA reto, a grade do buffer), para quem os le
    // FORA da superficie: a silhueta de um grupo de iluminacao combinada, que
    // `renderIconOn` monta na CPU nos dois caminhos. Na CPU e a propria arte; na
    // GPU, a copia de [UP3] de um raster colocado com `feedsField` -- ou um
    // readback, se ela nao veio.
    virtual Result<const std::vector<float>*> artPixels(SurfaceArt& art) = 0;

    // O campo de uma camada de vidro. `contourField` e `fieldFromContoursCached`
    // (os contornos de `flattenSvgToContours`); `alphaField` e
    // `fieldFromAlphaCached` sobre o alfa da arte colocada. Na GPU o primeiro e
    // `icon_field.comp` e o segundo segue na CPU (a arte de um raster de vidro ja
    // tem a copia de CPU, [UP3]).
    // `bands`: ate onde quem le este campo precisa dele exato (IconRenderer.cpp
    // diz quem e quanto); a GPU nao busca o pe alem disso. A CPU ignora.
    virtual Result<SurfaceField> contourField(RenderCache* cache,
                                              const std::vector<FieldContour>& contours,
                                              std::uint32_t width, std::uint32_t height,
                                              const FieldOptions& options,
                                              std::uint32_t superSample,
                                              const FieldBands& bands) = 0;
    virtual Result<SurfaceField> alphaField(RenderCache* cache, SurfaceArt& art,
                                            std::uint32_t width, std::uint32_t height,
                                            const FieldOptions& options) = 0;
    // O campo de um GRUPO que ilumina elemento por elemento: `stackFields`
    // (DistanceField.h) do campo de cima sobre o de baixo, texel a texel. Na GPU
    // e `icon_field_stack.comp` -- uma escolha, sem conta, entao o resultado e
    // um dos dois de entrada bit a bit.
    virtual Result<SurfaceField> stackField(const SurfaceField& lower,
                                            const SurfaceField& upper, float reach,
                                            bool advanced) = 0;
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

    // O brilho interno da geracao 26 sobre o alvo corrente: `drawGlow`
    // (GlassGlow.h), pelo campo do grupo. Quem chama ja conferiu `glowDraws`.
    // Na GPU: `icon_glow`, a mesma conta em double.
    virtual Result<void> glow(const SurfaceField& field, const GlowArguments& args) = 0;

    // `blendOver` no alvo corrente, da arte de uma camada. `clearContent`: a
    // arte passa antes pela matriz do CONTEUDO do Clear (IconRendering 0x4AF20):
    // `(0.85 R, 1, 0, A)` sobre a cor reta. `clampPlusLighter`: com `mode ==
    // PlusLighter`, o composite e o `clampedPlusL` (BlendFormula.h) no lugar da
    // soma -- so este desenho o alvo grampeia (0x4B530), nunca a sombra, o brilho
    // ou os realces.
    virtual Result<void> blendArt(const SurfaceArt& art, float alpha, BlendMode mode,
                                  bool clearContent, bool clampPlusLighter) = 0;

    // A sombra de `source` (`shadowImageCached`), como a fonte e AGORA -- antes
    // da mascara de translucidez, que nao entra nela. `clipShadowOverdraw` faz o
    // overdraw dela (`shadowOverdrawImage`) contra `content`, a imagem do grupo
    // JA mascarada: duas chamadas porque as duas leituras caem de lados opostos
    // da mascara. `blendShadow` compoe uma das duas no alvo corrente com
    // `blendOver`. Na GPU: `icon_ring`, `icon_shadow`, `icon_blur`.
    virtual Result<SurfaceShadow> makeShadow(RenderCache* cache, SurfaceArt& source,
                                             ShadowStyle style,
                                             const ShadowGeometry& geometry) = 0;
    virtual Result<void> clipShadowOverdraw(SurfaceShadow& shadow, SurfaceArt& content,
                                            double clipAlpha) = 0;
    virtual Result<void> blendShadow(const SurfaceShadow& shadow, bool overdraw, float alpha,
                                     BlendMode mode) = 0;

    // A recoloracao do Tinted Dark sobre o alvo corrente, pre-multiplicado:
    // `tintDark` de IconRenderer.cpp (a conta de `applyTintedDark`, que e linear
    // e sem deslocamento). So a geracao 26 a pede DENTRO do render -- la os
    // realces da pastilha ficam fora da camada tingida. Na GPU: `icon_tint`.
    virtual Result<void> tint(const IconRenderOptions::TintRecolour& tint) = 0;

    // O recorte pedido, des-multiplicado: o `RenderedIcon::rgba`.
    virtual Result<std::vector<float>> finish(std::int32_t cropX, std::int32_t cropY,
                                              std::uint32_t width, std::uint32_t height) = 0;
};

// Os campos e a sombra com o `RenderCache` de IconRenderer.cpp (nulo = sem cache).
std::shared_ptr<const std::vector<float>> shadowImageCached(
    RenderCache* cache, const std::vector<float>& art, std::uint32_t width,
    std::uint32_t height, ShadowStyle style, const ShadowGeometry& geometry,
    const ShadowParameters& parameters);
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

// A recoloracao do Tinted Dark sobre `count` pixels RGBA, no lugar
// (`applyTintedDark`, e a superficie de CPU).
void tintDark(float* rgba, std::size_t count, const IconRenderOptions::TintRecolour& tint);

// As decisoes de `renderIcon`, desenhadas em `surface`.
Result<RenderedIcon> renderIconOn(IconSurface& surface, const icf::IconBundle& bundle,
                                  IconRenderOptions options);

}  // namespace rb
