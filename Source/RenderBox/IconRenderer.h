#pragma once
// A whole `.icon` bundle drawn: groups, layers, and the art each one names.
//
// WHAT THIS ADDS OVER `renderSvg`
// -------------------------------
// `renderSvg` draws ONE svg fitted to the target. A document is a stack: groups
// in order, layers in order inside them, each naming art that is PLACED on a
// canvas rather than fitted to it, each with its own scale, translation and
// opacity, and each resolved for a context (`appearance`, `idiom`).
//
// THE TWO THINGS THE DOCUMENT DOES NOT SAY, AND ARE THEREFORE ASSUMPTIONS
// -----------------------------------------------------------------------
// Measured over all 145 corpus documents: **no canvas-size key exists in any of
// them**. The size lives in the renderer -- `IconRendering.GlobalConfiguration`
// declares `canvasSize` (doc 03 §17.5) -- and that default has not been read.
//
//   1. `kCanvasPoints = 1024`. What supports it: the most common art viewBox is
//      `0 0 1024 1024` (36 of the corpus's 149 SVGs), and the largest layer
//      translation measured is 590 points, which sits inside a ±512 canvas plus
//      overhang and would be far outside a 512-point one. What does NOT support
//      it: nothing read it from the binary.
//
//   2. The origin is the canvas CENTRE and +y points DOWN. Centre because
//      translations are frequently negative, which a top-left origin would not
//      produce. The y direction was settled by rendering a composed icon whose
//      parts are recognisable and looking at it -- see doc 03 §22.
//
// Both are marked in the code where they are used. An assumption that is written
// down can be corrected; one that is merely coded becomes folklore.
//
// AND THE FILL, SINCE 2026-09-03
// ------------------------------
// The document's `fill` is no longer read by this file. `FillResolve.h` reads
// it -- twice, because the target reads it twice: a BACKGROUND converter and a
// LAYER converter that answer differently to the same word. This file consumes
// those answers and turns them into paint.
//
// Three consequences show in the pictures:
//
//   1. THE BACKGROUND IS DRAWN. Until now `renderIcon` drew layers over
//      nothing, and the root `fill` -- which every one of the 145 corpus
//      documents carries -- was not read at all.
//   2. `automatic` DRAWS. On the background it is a system chiclet ramp, or,
//      under `tinted`, `IconColor.clear`. On a LAYER it is not a colour: it is
//      what that same layer resolves at the `light` slot, which `[ART]` is nil
//      in 152 of its 156 corpus firings -- the art keeps its own colours.
//   3. `automatic-gradient` DRAWS. `2026-09-01-gradiente.md` §4.4 refused it
//      because THE AXIS HAD NEVER BEEN READ. It has been: all three converter
//      sites pass `placement: nil`, and nil is `GradientPlacement.default`,
//      `(0,0)->(0,1)`. The reason for that refusal is gone.
//
// AND THE ORIENTATION THE BACKGROUND THROWS AWAY
// ----------------------------------------------
// `[BIN]` Exactly one of the four gradient-constructing sites reads the
// document's `orientation`: the LAYER's `linear-gradient`. A BACKGROUND
// `linear-gradient` that names an orientation draws on the default vertical
// axis anyway. `[ART]` 61 background resolutions over the corpus name an
// orientation the target discards. Honouring it here would be more correct than
// the target and therefore a different pixel, so the background's axis comes
// from `resolveBackgroundFill`, which passes the nil as a literal.
//
// TWO THINGS DRAWN WITHOUT BEING READ, AND THEY SAY SO IN `notes`
// ----------------------------------------------------------------
// `kGradientAxisDirectionNote` and `kBackgroundShapeNote`. Each names a gap
// that changes the pixels; neither is a reason to refuse to draw, and neither
// is allowed to be silent. (There were three until 2026-10-01: the rect a
// system fill is placed against was read, and `kChicletRectNote` went with the
// gap it named -- `SystemFill.h`.)
//
// AND THE GLASS, SINCE 2026-09-02
// -------------------------------
// A layer whose `glass` bit is set is no longer skipped. The material comes off
// the GROUP (`GlassMaterial.h`), the layer's art becomes contours, the contours
// become a field, the field becomes a displacement map, and the accumulator --
// which IS the backdrop -- is refracted through it before the layer's own art
// is drawn on top. `GlassLayer.h` carries the whole chain, the ordering
// decision, and the two gaps that are named rather than filled: the
// points-to-pixels ruler (which lands in `RenderedIcon::notes`) and raster art,
// which has no contour to flatten.
//
// AND THE SHADOW, SINCE 2026-09-15
// --------------------------------
// A glass layer casts one under itself before its art is drawn. The alpha is
// three multiplications with no clamp anywhere, the offset and the blur radius
// come off `ICRRenderingParameters.Shadow`, and `shadowStyle` picks the opacity
// table, the colour and the blend byte at once. `GlassShadow.h` carries all of
// it, including the two steps that are NAMED instead of drawn: the ring that
// clips the shadow (present by default, and its geometry unread) and the second
// overdraw composite. Unlike the translucency mask, this one runs on raster art
// too -- it needs the art's alpha and not a contour.
//
// AND THE GROUP, SINCE 2026-10-01
// -------------------------------
// The two blocks above describe the glass as a LAYER's, which is how it was
// first drawn. `[BIN]` In the target it is a GROUP's: `FinalizedIcon.Layer` -- a
// document group -- carries ONE image, ONE SDF and ONE shadow image, and its
// compositor has no element loop. So `renderIcon` now flattens a group's layers
// into one image, each layer's opacity and blend meeting only the layers of its
// own group; stacks the glass layers' fields into one; and then puts the group
// on the picture once -- the refraction, the shadow (cast from the art BEFORE
// the translucency), the mask over the whole image, the image under the group's
// own `opacity` and `blend-mode`, the shadow's overdraw, the highlights. A
// group's `hidden` and `opacity`, which nothing read, are read, and so is its
// `lighting`: it picks whether the one field is the elements' fields stacked or
// the field of their union. A group of one layer keeps the arithmetic it had
// wherever the target's is the same. `IconRenderer.cpp` carries the addresses
// step by step.
#include <cstdint>
#include <string>
#include <vector>

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/FillResolve.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/PixelGrid.h"
#include "Source/RenderBox/RenderCache.h"
#include "Source/RenderBox/RenderingParameters.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/RenderBox/SystemFill.h"

namespace rb {

// `[INF]` The canvas the document's points are measured in. See the header note.
inline constexpr double kCanvasPoints = 1024.0;

// O retangulo que o chamador quer ver, em pixels da grade que `size` define
// (spec 2026-09-16). O padrao e o canvas inteiro, e nesse caso todo sitio se
// reduz a aritmetica de antes.
//
// O INVARIANTE: um render de viewport e IGUAL, float a float, ao recorte
// correspondente de um render cheio na mesma `size`. `Tests/test_viewport_render.cpp`
// e quem cobra.
struct IconViewport {
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t width = 0;    // 0 == `size`
    std::uint32_t height = 0;   // 0 == `size`
};

struct IconRenderOptions {
    std::uint32_t size = 512;
    icf::Context context;
    int subdivisions = 16;

    // WHICH PARAMETER BLOCK THE ICON IS DRAWN WITH (`DesignGeneration.h`,
    // `RenderingParameters.h`). 27 is the target's unmutated default and what
    // this renderer has always drawn; 26 is the block Apple's own editor draws
    // with. It is an option of the render and not a key of the document, which
    // is where the target keeps it too.
    DesignGeneration generation = DesignGeneration::G27;

    // Which of a `SizeBasedValue`'s four slots every per-size parameter is read
    // from: the highlights' opacity, distance and inset, the shadow's tables,
    // `glyphTranslucency.strength`.
    //
    // `[BIN]` The byte the target switches on is `ctx+0x469F`, and its writer is
    // read: `0x42D54`-`0x42DE4` classifies `min(width, height)` of the icon's
    // rect IN POINTS against `ICRRenderingParameters.thresholds`
    // (`sizeClassFor`, `RenderingParameters.h`). That rect is where the icon
    // lands on screen, which a render asked for by pixel size does not know --
    // so the class stays the caller's to give.
    //
    // `[INF]` UNSET is `sizeClassFor(kReferenceIconSidePoints, thresholds)`:
    // the 206 pt rect of the one reference picture this project has. That is
    // `large` under generation 27 (256) -- the default this field had while it
    // was a plain value -- and `display` under generation 26 (128).
    // `effectiveSizeClass` is that rule.
    std::optional<IconSizeClass> sizeClass;

    IconViewport viewport;

    // A RECOLORACAO DAS RENDICOES TINGIDAS. Vazio e `renderingMode == .color`, o
    // documento como foi autorado. Presente e `.tinted(color, saturation)`:
    // `[BIN]` `RenderingMode.tinted(with:)` (IconRendering 0x5A6F4) guarda a cor
    // com alfa 1 e o alfa dela como `saturation`. O que ISTO muda no render e a
    // sombra: `[BIN]` 0x49F40 forca `neutral` em todo modo que nao e `.color`
    // (`shadowEffectiveStyle`). A recoloracao dos pixels e `applyTintedDark`,
    // aplicada por quem pede o render sobre a imagem pronta -- na geracao 27; na
    // 26 o proprio render a aplica, antes dos realces do chiclet, e avisa em
    // `RenderedIcon::tintApplied`.
    struct TintRecolour {
        double r = 1.0, g = 1.0, b = 1.0;
        double saturation = 1.0;
        bool operator==(const TintRecolour&) const = default;
    };
    std::optional<TintRecolour> tint;

    // A MASCARA do Clear em vez do icone: cada camada de conteudo entra pela
    // matriz do conteudo (`IconSurface::blendArt`); o resto (sombra, especular,
    // realces do chiclet) entra cru, como no alvo. `applyClear` le o resultado.
    // `[BIN]` So numa geracao que TEM um modo Clear: na 26 `clearMode` e nil
    // (`0x77064`) e o render sai o do icone -- `RenderedIcon::clearMask` diz
    // qual dos dois veio.
    bool clearMask = false;

    // `[OBS]` THE SECOND GATE OF THE PLUS-LIGHTER CLAMP, which is not read. The
    // target hands a plus-lighter group's image to the `clampedPlusL` blend
    // shader when `shouldClampPlusLBlending` is set AND the byte at `ctx+0x528`
    // is 1 (`0x4B538`-`0x4B54C`). The flag is the generation's
    // (`RenderingParameters.h`); the byte is a `Bool` of the drawing context
    // whose writer was not found (`BlendFormula.h`). This is that byte. `false`
    // keeps the composite the plain sum it has always been here; a caller that
    // comes to know the target's value can say so without touching the
    // renderer. It only matters under generation 27 -- 26 clears the flag.
    bool drawingContextClampsPlusLighter = false;

    // What one render leaves for the next (`RenderCache.h`). Null is the
    // render with no memory, step for step the one before the cache existed;
    // the pixels are the same either way, and the gate in
    // `Tests/test_render_cache.cpp` holds it to that.
    RenderCache* cache = nullptr;
};

// The size class a render runs with: the caller's, or -- unset -- the class of
// the reference rect under the generation's thresholds (see
// `IconRenderOptions::sizeClass`).
IconSizeClass effectiveSizeClass(const IconRenderOptions& options);

// A layer that was not drawn, and why. Named, never dropped in silence.
struct SkippedLayer {
    std::size_t group = 0;
    std::string layer;
    std::string why;
};

struct RenderedIcon {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;   // straight RGBA, row major

    // Onde `rgba` fica na grade: `width` x `height` pixels a partir de
    // (`originX`, `originY`), numa grade de `size`. Num render cheio, a origem e
    // zero e `width == height == size`.
    std::uint32_t size = 0;
    std::int32_t originX = 0, originY = 0;
    // O buffer em que o render RODOU, margem incluida. Existe para o
    // diagnostico do gate: uma diferenca colada numa borda interna deste
    // retangulo e margem curta.
    PixelGrid buffer;
    // O buffer planejado passou do teto de area (spec, "O teto de area"). Nada
    // foi desenhado; quem pediu decide se cai para a resolucao base.
    bool viewportRefused = false;

    std::size_t drawn = 0;
    std::size_t total = 0;
    std::vector<SkippedLayer> skipped;
    // Shapes inside drawn layers that the SVG renderer itself could not draw.
    std::vector<std::string> shapeGaps;

    // Things that WERE drawn but whose provenance is short of a measurement,
    // said in words. A gap that changes the pixels and is not reported becomes
    // folklore the moment the picture looks plausible -- so the glass's
    // points-to-pixels ruler (`GlassLayer.h`, GAP ONE) announces itself here
    // instead of scaling quietly. Deduplicated: one sentence per render, not
    // one per layer.
    std::vector<std::string> notes;

    // The document's root `fill` -- the icon's background -- counted APART from
    // the layers. `drawn`, `total` and `skipped` are the layer ruler, and the
    // corpus gate asserts `drawn + skipped <= total` on it; a background folded
    // into those numbers would break that arithmetic and, worse, would make a
    // document with one layer and a background read as two layers.
    bool backgroundPainted = false;
    // Whether `rgba` is the Clear MASK -- lightening in red, darkening in the
    // complement of green, highlights in blue -- and not a picture. True when
    // the options asked for the mask AND the generation has a clear mode;
    // `finishMono` runs `applyClear` only over a mask.
    bool clearMask = false;
    // Whether the tinted-dark recolouring is ALREADY in `rgba`. It is when the
    // generation draws the chiclet's highlights outside the tinted layer (26):
    // the recolouring then ran inside the render, under the highlights, and
    // `finishMono` must not run `applyTintedDark` over it again.
    bool tintApplied = false;
    // Non-empty when the root `fill` is present and could not be turned into
    // paint. `[ART]` All 145 corpus documents carry a root `fill`, so an empty
    // `backgroundGap` with `backgroundPainted == false` means the document
    // named none -- which no corpus document does.
    std::string backgroundGap;

    // THE FIVE GLASS COUNTERS BELOW COUNT GROUPS, since 2026-10-01: each effect
    // runs once per group (`[BIN]` `IconRendering` `0x48B74`), so a group of
    // three glass layers adds one to each and three to `drawn`. Their comments
    // still say "layers", which is what they counted while every layer ran the
    // effects on its own; read "groups".
    //
    // Layers whose art was multiplied by the `simplifiedShapeAwareGradientMask`
    // that `translucency` opens. Counted apart from `drawn` for the same reason
    // `glassRefracted` is: a group whose `translucency` is absent, zero or
    // switched off produces an identity mask, and "drew opaque because the
    // document said opaque" must stay distinguishable from "drew opaque because
    // the effect is not implemented" -- which is what this whole number is for.
    std::size_t glassTranslucent = 0;

    // Layers drawn with their glass refracting the backdrop underneath them.
    // Counted apart from `drawn` because a glass layer whose refraction is the
    // identity (`refractionStrength == 0`, the read default) draws its art and
    // refracts nothing, and the two outcomes must be distinguishable.
    std::size_t glassRefracted = 0;

    // Layers whose `specular` actually put a highlight on the pixel. Counted
    // apart from `drawn` for the fourth time and the fourth version of the same
    // reason: a layer can ask for a specular and get none, either because its
    // art is a raster with no contour to build a field from, or because every
    // one of the resolved highlights came out at zero opacity. "Asked and
    // got nothing" must not read as "asked and got something".
    std::size_t glassSpecular = 0;

    // Layers that cast a glass shadow under themselves. Counted apart from
    // `drawn` for the third time and for the third version of the same reason:
    // `[ART]` 25 of the corpus's 302 `shadow` resolutions are `none` and 226 of
    // the rest carry `opacity: 0.5`, so "drew no shadow because the document
    // asked for none" has to stay distinguishable from "drew no shadow because
    // the renderer has none" -- which is what every render of this project said,
    // silently, until 2026-09-15.
    std::size_t glassShadowed = 0;

    // Layers whose shadow was composited a SECOND time, over the art, by the
    // overdraw pass (`GlassShadow.h`). Counted apart from `glassShadowed`
    // because the two gates are different: the first asks whether the document
    // wants a shadow, the second whether its `translucency` opens
    // `clamp01(translucency / translucencyForMaxOverdraw)` above zero. A group
    // with a shadow and no translucency draws one pass and not two, and that is
    // the document's answer rather than a missing renderer.
    std::size_t glassShadowOverdrawn = 0;

    // Groups over which the inner glow of generation 26 was drawn
    // (`GlassGlow.h`). Always zero in generation 27, whose `Glow` is nil; in 26,
    // the groups with a specular -- the glow rides the field the highlights
    // give a reach to.
    std::size_t glassGlowed = 0;
};

// TINTED DARK, sobre a imagem pronta (straight RGBA). `[BIN]` O alvo desenha o
// icone inteiro dentro de uma camada filtrada (IconRendering 0x43190-0x4334C e
// 0x48254-0x48414, so com aparencia escura e modo tingido):
//
//   1. `addSaturationFilterWithAmount:(saturation)` -- RenderBox `set_saturate`
//      (0x11C000), a matriz Rec.709 (0.2126 / 0.7152 / 0.0722, lidas como
//      0x3E59B3D0 e 0x3F371759), com a quantidade presa em >= 0;
//   2. a matriz de cor do duotom (0x7E5A0-0x7E5E0): `out = lo + in * (hi - lo)`
//      por canal, alfa intocado, com `lo = mix(preto, mix(preto, tint, f), a)` e
//      `hi = mix(branco, tint, tint.a)`. Com `f = darkTintDuotoneShadowBlendFactor
//      = 0` e `tint.a = 1` (os padroes) isso e `lo = preto`, `hi = tint`.
//
// Na geracao 27 os realces do chiclet entram na mesma camada
// (`darkTintHighlightsBlendWithContent = true`, `0x483D4`-`0x483E0`), entao
// aplicar na imagem final e o mesmo que aplicar na camada. Na geracao 26 NAO
// entram -- o corpo dela fecha a camada antes de chamar `0x475A0` (`0x4332C`,
// `0x43338`) --, e ai a recoloracao roda dentro do render, entre o conteudo e os
// realces (`RenderedIcon::tintApplied`), e esta funcao nao e chamada. Com
// `lo = preto` a operacao e linear e sem deslocamento: vale igual em cor
// pre-multiplicada ou nao. `[OBS]` se o RenderBox aplica em espaco linear ou
// codificado nao foi lido; aqui e no espaco da imagem. A composicao do alvo
// sobre o fundo (`kCAFilterScreenBlendMode`) nao entra: sobre transparente ela
// e a propria imagem.
void applyTintedDark(RenderedIcon& icon, const IconRenderOptions::TintRecolour& tint);

// O FUNDO atras do icone, como a tela o mostra (RGBA8 sRGB codificado, sem
// pre-multiplicar): o Clear e composto SOBRE ele, entao ele e entrada.
struct ClearBackdrop {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Pixels da copia por ponto da tela: a casca manda em meia resolucao, e o
    // `resultBlurRadius` do vidro (2 pontos) e medido nisto.
    double pixelsPerPoint = 1.0;
    std::vector<std::uint8_t> rgba;
};

// O CLEAR INTEIRO, sobre a mascara de um render com `clearMask`. Receita:
// laudo de 30/09 §5.
//
//   matriz total (0x47D2C, ColorClamp 0..1):  R'' = R,  G'' = 0.3 (1 - G),  B'' = B
//   L  lerp(fundo, VCM(fundo; 0.9, 2.5, 2.0), R''·A)      vibrantColorMatrix
//   D  fundo - G''·A, preso em 0                           plusD
//   H  lerp(fundo, VCM(fundo; 0.2, 1.35, 1.4), B''·A)      vibrantColorMatrix
//
// na ordem de `passOrder` (L, D, H). O resultado e reescrito como cor reta com
// o alfa da mascara, de modo que compor sobre o MESMO fundo devolve a
// composicao: `rgb = (saida - fundo (1 - A)) / A`. Fora da mascara nada muda.
// `[OBS]` H le o resultado de L e D, nao o fundo original (nao lido).
//
// `glass`: o vidro simulado sob o icone (`SimulatedGlass.h`); as passadas leem
// o fundo COM ele, que e o que esta atras delas no alvo. Nulo, o fundo cru.
void applyClear(RenderedIcon& icon, const ClearBackdrop& backdrop, double squareX,
                double squareY, double squareSide, std::uint32_t canvasSize,
                const struct SimulatedGlass* glass = nullptr);

// O icone pronto (cor reta, alfa) composto sobre o fundo com o vidro simulado,
// reescrito como cor reta sobre o fundo cru -- para o Tinted Dark, que tambem
// tem o vidro por baixo (Kit 0x1290FC: rendicao >= 2).
void applyOverGlass(RenderedIcon& icon, const ClearBackdrop& backdrop, double squareX,
                    double squareY, double squareSide, std::uint32_t canvasSize,
                    const struct SimulatedGlass& glass);

// The placement of one layer's art on the canvas, in the target's own terms.
// Exposed so the transform can be checked without a GPU.
struct LayerPlacement {
    double scale = 1.0;
    double translateX = 0.0;   // canvas points, from the CENTRE
    double translateY = 0.0;
};

// `[INF]` A group's transform applies to the layer's, so the two compose:
// `G(L(p)) = gs*ls*p + gs*lt + gt`. The translation of the inner one is scaled
// by the outer, which is what composition means and is not something the
// document states.
LayerPlacement compose(const LayerPlacement& group, const LayerPlacement& layer);

// The globals that place `box` on the canvas under `p`, for a square target.
//
// `size` E SO ESCALA, mesmo num render de viewport: a colocacao continua sendo
// a do canvas inteiro e nao sabe que o alvo pode ser um pedaco dele. O
// deslocamento do buffer entra DEPOIS, como um inteiro no viewport do Vulkan
// (`RenderOptions::originX/Y`), porque dobra-lo dentro de `m2` movia o
// arredondamento do estagio de vertice -- a medida esta no corpo da funcao
// (spec 2026-09-16, "O invariante que governa o desenho").
PathGlobals placeOnCanvas(const icf::svg::ViewBox& box, const LayerPlacement& p,
                          std::uint32_t size);

// ---- the fill, from a resolution to paint --------------------------------

// The rect a fill's gradient is placed against, in TARGET PIXELS.
//
// `[BIN]` The draw path resolves a placement against the `boundingRect` of the
// shape being filled (`0x1BC8C`), and the unit-to-rect mapping is
// `SystemFill.h`'s `placeUnitPoint`. So the rect is not decoration: it is where
// the axis gets its length and its offset.
//
// `[OBS]` WHICH rect an `Icon.Element` reports there -- the `bounds` it is
// constructed with, the art's viewBox, or the tight bounding box of the paths
// actually drawn -- was not read. This returns the art's viewBox as placed on
// the canvas, which is the box the element's own `contents` declare.
//
// This replaces an earlier assumption. Until now a layer `linear-gradient` took
// its `orientation`'s unit square to be THE CANVAS, marked `[OBS]` because the
// corpus cannot tell the two apart while every orientation runs corner to
// corner. The rect is now read, so the assumption is retired rather than kept
// beside the reading.
//
// O retangulo e ABSOLUTO tambem num render de viewport, porque quem o consome
// -- a rampa do compositor e os `bounds` da translucidez -- avalia em
// coordenada absoluta.
PlacementRect artPlacementRect(const icf::svg::ViewBox& box, const LayerPlacement& p,
                               std::uint32_t size);

// A caixa do que a arte DESENHA, no canvas e recortada nele -- o retangulo em
// que a rampa da translucidez e medida (a leitura esta em IconRenderer.cpp).
PlacementRect artContentRect(const icf::svg::SvgDocument& svg, const LayerPlacement& p,
                             std::uint32_t size);

// O mesmo para arte RASTER: a caixa em que `placeRaster` larga a imagem, em
// pixels do alvo. A aritmética é a de `artPlacementRect` com a largura e a
// altura da imagem no lugar da extensão do `viewBox` -- ver a nota junto da
// definição, em IconRenderer.cpp.
//
// Declarada aqui desde 19/09 porque ela é o ORÁCULO do hit-test de uma camada
// `.png` (`ick::canvasLayerRect`), do mesmo jeito que `artPlacementRect` é o de
// uma camada SVG: duas cópias da mesma aritmética são duas que param de
// concordar, e o caso do canvas cobra contra esta em vez de contra si mesmo.
PlacementRect rasterPlacementRect(std::uint32_t imgW, std::uint32_t imgH, const LayerPlacement& p,
                                  std::uint32_t size);

// A resolved fill turned into what the compositor paints with, against
// `shapeRect`.
//
// `why` is set, and the returned paint is `Kind::None`, when a fill that
// resolved cannot be painted -- which is one case only: a gradient whose
// placement collapses to a point, where there is no axis to project onto.
// `[ART]` No corpus fill does that; the guard exists because a document could.
//
// Exposed so the whole corpus can be swept through it with no GPU: the
// interesting failures of this function are arithmetic, and a sweep that needed
// a device would not run in the places that most need it.
//
// `params` is the generation's block: an `automatic-gradient` is derived with
// its six constants and a `.system` fill takes its two ramps from it. The
// default is generation 27, for the callers that sweep the corpus's fills
// without a render.
FillOverride fillPaint(
    const ResolvedFill& fill, const PlacementRect& shapeRect, std::string& why,
    const RenderingParameters& params = renderingParameters(DesignGeneration::G27));

// ---- what is drawn without having been read ------------------------------
//
// Each of these lands in `RenderedIcon::notes`, deduplicated, the way
// `glassRulerNote` does. They are not skip reasons: the pixel IS drawn. They
// exist because a gap that changes the picture and is not said out loud becomes
// folklore the moment the picture looks plausible.

// `[OBS]` The default axis is `(0,0)->(0,1)` and that is read. Which END of the
// shape receives the first stop is not: the y-handedness of the RB display list
// was never established. On the light chiclet ramp the two answers differ by
// four parts in 255; on the dark one, 31 against 15, they do not.
extern const char* const kGradientAxisDirectionNote;

// `[BIN]` The background converter reads `primaryColor` and `secondaryColor`
// and never touches `orientation`. Said only when the document actually named
// one, because the note is about THIS document having asked for something the
// target throws away -- not about the rule.
extern const char* const kDiscardedBackgroundOrientationNote;

// `[OBS]` The background is painted over the whole canvas square. The chiclet's
// own geometry -- the corner radius that would clip it -- was not read, so
// there is no shape to cut it to.
extern const char* const kBackgroundShapeNote;

// The layer's fill reaches the VECTOR path only. `[ART]` 45 of the corpus's
// layers name `.png` art and some of those carry a fill; the raster is placed
// with its own colours. `[OBS]` Whether the target retints a raster element the
// way it retints a vector one was not read, so this is named rather than
// guessed in either direction.
extern const char* const kRasterFillNote;

// What the translucency mask is, said out loud every time it draws: which of
// the target's two branches each generation takes (both transcribed), and the
// two things under it that are not read -- the rect its vertical ramp is
// measured in, for a raster, and which end of that rect the ramp starts at.
extern const char* const kTranslucencyBoundsNote;
// A render asked for the Clear mask under a generation whose `clearMode` is nil
// (26): what is drawn instead, and what of the target's rendition is not.
extern const char* const kClearModeNilNote;
// A group blended plus-lighter under a generation whose
// `shouldClampPlusLBlending` is set, drawn WITHOUT the clamp because the second
// gate (`IconRenderOptions::drawingContextClampsPlusLighter`) is not read.
extern const char* const kPlusLighterClampNote;

// A glass layer whose art is a RASTER, whose distance field was therefore built
// from the art's own alpha rather than from a flattened contour.
//
// It replaces `kTranslucencyRasterNote`, which said the opposite -- that a
// raster has no contour and so the mask cannot run. `[BIN]` The target builds
// its field from a rasterised alpha too (`DistanceField.h` §PART THREE carries
// the addresses), so refraction, translucency and specular all run here now.
// The note stays because one thing under it is still unread: the GRID the
// target rasterises onto before it transforms.
extern const char* const kGlassRasterFieldNote;

// A glass layer whose art is a VECTOR, whose distance field was nonetheless
// built by rasterising that vector and transforming the rasterisation -- the
// same door the raster art takes, and the same door `[BIN]` the target takes
// for both. What the note carries that the raster one does not is the price:
// the exact `argmin` over the contour is gone, and with it the sub-texel
// distance and the continuous gradient direction. The laudo measures what that
// costs in the picture.
extern const char* const kGlassVectorFieldNote;

// A group with MORE THAN ONE glass element lit one by one (`lighting`
// `individual`, which is also what a missing key means): its field is the
// elements' fields stacked back to front, the upper one replacing the lower
// inside its own dilated footprint (`DistanceField.h`, part four). The stack's
// parameters and both of its fragments are read; that the upper field REPLACES
// the lower one is the inference under them, and the note says what it shows as.
// It fires only when a stack is actually made.
extern const char* const kFieldStackNote;

// A group with more than one glass element lit as ONE shape (`lighting`
// `combined`): its field is the field of the union of their silhouettes, so no
// rim is drawn where two of them meet. The mode and its one-list construction
// are read; the silhouette's COVERAGE is this project's (sampled on the CPU so
// both render paths agree), and the note says so. It fires when the union's
// field is actually built.
extern const char* const kCombinedFieldNote;

// `[BIN]` The same mode's quirk, said only when it bites: glass elements that
// come after the first RASTER one are not drawn into the union. That is the
// target's own short-circuit and it is reproduced, not corrected.
extern const char* const kCombinedRasterNote;

// `[OBS]` A glass layer at zero opacity, which this renderer drops whole and the
// target does not: there it is left out of the group's image and still draws
// its silhouette into the group's field. Said only for a document that has one
// in a group whose field is used.
extern const char* const kInvisibleGlassNote;

Result<RenderedIcon> renderIcon(Device& device, const icf::IconBundle& bundle,
                                IconRenderOptions options = IconRenderOptions{});

// O MESMO RENDER, COM OS PIXELS NA GPU (frente GPU, G1 -- `Docs/Plans/2026-09-29-render-gpu.md`).
//
// As decisoes sao as de `renderIcon`, literalmente -- as duas chamam
// `renderIconOn` (IconSurface.h) --, entao `skipped`, `shapeGaps`, `notes`, os
// contadores e a semantica de viewport/ladrilho sao os mesmos. O que muda e onde
// os pixels moram: acumulador, grupo e arte em buffers da GPU, composicao e
// mescla em compute, um readback no fim. As etapas de vidro ainda sao de CPU e
// entram por ida e volta declarada (IconRendererGpu.cpp lista cada uma).
//
// NAO e byte a byte com `renderIcon`: a GPU calcula em float onde a CPU calcula
// em double. O teto medido pelo plano e media <= 0,5 nivel e pior pixel <= 4
// niveis de 8 bits por documento; `Tests/test_gpu_fidelity.cpp` cobra. A
// exportacao e o `IconComposerCli` sem `--gpu` seguem em `renderIcon`, o gabarito.
Result<RenderedIcon> renderIconGpu(Device& device, const icf::IconBundle& bundle,
                                   IconRenderOptions options = IconRenderOptions{});

}  // namespace rb
