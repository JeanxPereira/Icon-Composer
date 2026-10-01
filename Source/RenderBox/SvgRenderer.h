#pragma once
// An `SvgDocument` drawn into pixels: the connector between the two gated halves.
//
// WHAT THIS IS
// ------------
// Every stage below already passes a gate of its own -- the path buffer in the
// target's layout, the vertex maths, the coverage fragments, the fill rule, the
// flat composite, the render target -- and until now nothing joined them. A
// stage gate cannot catch a composition error: P5's two defects (the NDC y, and
// the quad's corner order) both survived their unit tests and died the moment a
// pixel was compared against a closed form. This is where the stages compose.
//
// WHAT IT DRAWS, AND WHAT IT REFUSES TO PRETEND
// ---------------------------------------------
// Flat-filled paths, non-zero or even-odd, composited one over the next. That is
// `scripts/slice-reach.py`'s slice, and the same script says what it is worth:
// **22 of the corpus's 194 layers, and 0 of its 55 judgeable documents**. A
// shape this cannot draw is NAMED in `skipped`, never dropped in silence -- the
// same rule as `SvgDocument::unsupported` and `IconDocument::unknownKeys`.
//
// THE RESOLVE AND THE COMPOSITE RUN ON THE CPU, ON PURPOSE
// ---------------------------------------------------------
// The coverage is rasterised on the GPU by the same shader files the probes are
// gated against. The fill rule and the flat composite then run through
// `accumulatorShape` and `compositeFlat` -- the oracles. That is not a fidelity
// compromise: those two are gated bit-for-bit against `PathResolve.glsl` and
// `PathComposite.glsl`, so CPU and GPU produce the same numbers by measurement.
// It is a scope choice. Wiring those two shaders into passes of their own is
// work this does not need in order to make a picture, and the differential that
// would guard it already exists.
//
// COLOUR SPACE IS CONVERTED AT THE PAINT
// --------------------------------------
// `color(display-p3 ...)` occurs about a dozen times in the corpus, and a
// document can declare every UNTAGGED colour of its SVGs to be Display P3
// (`color-space-for-untagged-svg-colors`, `RenderOptions::
// untaggedColoursAreDisplayP3`). Either way the components are taken to the
// working space by `svgColourToWorking` when a paint becomes floats -- the
// matrix and the curve are RenderBox's own, read in `ColorSpace.h`. Until
// 2026-10-01 such a shape was drawn unconverted and reported.
#include <cstdint>
#include <string>
#include <vector>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/CoveragePass.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/GradientOracle.h"
#include "Source/RenderBox/Image.h"

namespace rb {

// What a LAYER's own fill does to the art it names.
//
// `[INF]` The art gives the SHAPE and the layer's fill gives the COLOUR. The
// evidence is in the corpus: `Apollo-Reborn/AppIcon` draws `Eyes 3.svg`, whose
// art is `#000000`, under a layer fill of `display-p3:0.695,0.153,0.477` -- a
// pink. Nobody authors pink eyes as black art unless the fill retints them. And
// `GlowGetter`'s two halves are `#D9D9D9`, the placeholder grey a design tool
// leaves behind, under a fill of white.
//
// Measured: of the corpus's layers that carry BOTH a solid fill and SVG art,
// 17 name monochrome art and 7 name polychrome art. The polychrome ones flatten
// under this rule, and whether the target flattens them too is `[OBS]`.
struct FillOverride {
    enum class Kind { None, Solid, Ramp };
    Kind kind = Kind::None;
    float colour[4]{0, 0, 0, 1};
    // For `Ramp`: the stops and the map from CANVAS pixels into the ramp's
    // parameter, which the compositor builds because only it knows the canvas.
    std::vector<RampPoint> stops;
    double m[6]{1, 0, 0, 0, 1, 0};
    // `[BIN]` A DOCUMENT fill's ramp is not the piecewise-linear one. It is the
    // monotone cubic of `GradientOracle.h`'s `rampSmoothAtPositions`, because
    // `IconRendering 0x1BC74` asks for interpolation code 4. An SVG's own
    // gradient is a different question with a different answer, and nothing here
    // read it, so this stays false for those.
    bool smooth = false;
};

struct RenderOptions {
    std::uint32_t width = 512;
    std::uint32_t height = 512;
    // O alvo e `width` x `height`, mas a PROJECAO pode ser maior: o desenho
    // avalia em coordenada absoluta e o deslocamento entra como um inteiro no
    // viewport. Zero em `projectionWidth/Height` quer dizer "a do alvo".
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::uint32_t projectionWidth = 0;
    std::uint32_t projectionHeight = 0;
    // When set, every shape in the document is painted with this instead of its
    // own fill. The shape's own gradient reference is then not consulted.
    FillOverride override;
    // How many line segments one cubic becomes. `[INF]`, and inherited from
    // `BuildOptions`: the target carries the count in the buffer, so the RULE
    // that picks it lives on its CPU side and was not in the shader to read.
    int subdivisions = 16;

    // `[BIN]` `IconComposition.assumedSVGColorSpace` (the root key
    // `color-space-for-untagged-svg-colors`, whose one case is `display-p3`):
    // Foundation `0xF324` hands CoreSVG `{"preferredColorSpace": 1}`, and
    // `CGSVGDocumentGetColorOfPaint` (`CoreSVG 0x21028`) gives that space to
    // every colour that carries no tag of its own. `[INF]` Absent is sRGB.
    bool untaggedColoursAreDisplayP3 = false;

    // WHAT A FILL OVERRIDE DOES TO A SHAPE THE ART LEFT UNPAINTED.
    //
    // `[BIN]` The target does not repaint the art: it clones the layer's SVG
    // into a white silhouette, uses that as the MASK of the layer's fill, and
    // the closure that whitens each node is `IconRendering 0x8654` (called per
    // node from `0x8174`). For a shape node (`CGSVGNodeGetType == 2`):
    //
    //     opacity (attr 0x29)       set to the silhouette's, UNLESS it parses,
    //                               is <= 0 and the flag is clear     0x86B4-0x86FC
    //     fill (0x12)               set to the silhouette's when
    //                               `CGSVGPaintIsVisible`, or when the
    //                               flag is set                       0x8700-0x8730
    //     fill-opacity (0x13)       as `opacity`                      0x8734-0x8784
    //     stroke, stroke-opacity    no flag on either                 0x8788-0x8824
    //
    // The flag is `ICRRenderingParameters.recreateRadar153477135` (`+0x361`):
    // clear in generation 27, set in generation 26 (`0x7707C`). So in 27 a
    // shape with `fill="none"`, a fill of zero alpha or `opacity="0"` stays
    // out of the mask, and in 26 all three are forced in -- the shape becomes
    // solid.
    //
    // This is that flag. It only speaks when `override` is set. Until
    // 2026-10-01 this renderer painted an unpainted shape under an override
    // unconditionally, which is the flag's SET behaviour for the fill and
    // neither generation's for the opacity. `svgFillPaints` and
    // `svgShapeOpacity` are the two rules.
    //
    // `[OBS]` WHAT IS NOT REPRODUCED, in either generation: the closure also
    // sets every opacity that is ABOVE zero to the silhouette's, on shape and
    // group nodes alike, so under a fill the art's own `opacity="0.5"` does not
    // dim the mask. Here `Shape::opacity` still multiplies. And `Shape::opacity`
    // is the product down the ancestors, so a GROUP at zero cannot be told from
    // a shape at zero: with the flag set both are forced, where the target
    // forces only the shape's own.
    bool overrideForcesHiddenPaint = false;
};

// Whether a shape's FILL is painted under `options`. With no override it is the
// art's own answer (`fill` is not `none`). With one, a visible fill always
// paints and an invisible one -- `none`, or a colour whose alpha (which carries
// `fill-opacity`) is not above zero -- paints only under
// `overrideForcesHiddenPaint`. `[INF]` That `CGSVGPaintIsVisible` is exactly
// "not none and alpha above zero": the function is CoreSVG's and was not read.
bool svgFillPaints(const icf::svg::Shape& shape, const RenderOptions& options);

// The opacity a shape is composited with under `options`: its own, except that
// an override with `overrideForcesHiddenPaint` lifts a shape at or below zero
// to one.
double svgShapeOpacity(const icf::svg::Shape& shape, const RenderOptions& options);

// A shape that was not drawn, and why. `why` is meant to be shown to a person.
struct SkippedShape {
    std::size_t index = 0;
    std::string element;
    std::string why;
};

struct RenderedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Straight (NOT premultiplied) RGBA, row major, four floats per pixel.
    // Straight because that is what a PNG wants, and the un-multiply happens
    // once here rather than in every consumer.
    std::vector<float> rgba;

    std::size_t drawn = 0;
    // Strokes are counted apart from fills: one shape can contribute both,
    // and a single number could not say whether the stroke front is running.
    std::size_t strokesDrawn = 0;
    std::vector<SkippedShape> skipped;
    // Said in words when a FILTER drew something whose provenance is short of a
    // measurement. The blur is the one that speaks today: its routing into
    // `CIGaussianBlur`'s `inputRadius` is measured and the kernel CoreImage
    // builds from that is not, so the width of the blur is drawn without being
    // claimed. A gap that changes pixels and stays quiet becomes folklore the
    // moment the picture looks plausible.
    std::vector<std::string> filterNotes;
};

// The transform from the document's user space to pixels: a uniform fit of the
// viewBox into the target, centred, with no flip -- SVG's y and the raster's y
// both point down. Exposed because a test should be able to check the mapping
// without rendering, and because the icon compositor will need to compose it
// with a layer's own transform.
PathGlobals fitViewBox(const icf::svg::ViewBox& box, std::uint32_t width,
                       std::uint32_t height);

Result<RenderedImage> renderSvg(Device& device, const icf::svg::SvgDocument& doc,
                                RenderOptions options = RenderOptions{});

// The same, with the placement given rather than fitted. The icon compositor
// needs it: a layer's art is not fitted to the canvas, it is placed on it by the
// document's own scale and translation.
Result<RenderedImage> renderSvgPlaced(Device& device, const icf::svg::SvgDocument& doc,
                                      const PathGlobals& globals,
                                      RenderOptions options = RenderOptions{});

// A caixa de um caminho no espaco do proprio SVG, pontos de controle inclusos
// -- a que `objectBoundingBox` mede. Exposta para o caminho residente
// (`renderIconGpu`), que resolve os gradientes com a mesma caixa.
void svgPathBounds(const icf::svg::Path& path, double& x0, double& y0, double& x1, double& y1);

// ---- gradients -----------------------------------------------------------
//
// A gradient the renderer can evaluate: the ramp, plus the map that carries a
// point in the SVG's user space into the gradient's own -- which is where the
// target's geometry expects it, because `Gradient::value` for a linear gradient
// is just `p.x` (doc 03 §23.2). Everything the axis or the circle says is
// folded into this map.
struct ResolvedGradient {
    std::uint32_t state = 0;         // the geometry x spread field, doc 03 §23.1
    // user -> gradient space, row-major 2x3: gx = m[0]*x + m[1]*y + m[2]
    double m[6]{1, 0, 0, 0, 1, 0};
    std::vector<RampPoint> stops;    // ascending by location
    bool ok = false;
    std::string why;                 // when `ok` is false, in words
};

// Resolves an SVG paint reference against the document's gradients, for a shape
// whose user-space bounding box is `bx0..by1` -- which `objectBoundingBox`
// needs and `userSpaceOnUse` ignores.
// `ctm` is the referencing shape's `Shape::ctm`: a `userSpaceOnUse` gradient is
// measured in that shape's own user space.
// One SVG paint colour as the four floats the compositor uses: converted out
// of Display P3 when the colour is tagged so, or is untagged in a document that
// declares its untagged colours to be Display P3.
void svgColourToWorking(const icf::svg::SvgColor& c, bool untaggedIsDisplayP3, float (&rgba)[4]);

ResolvedGradient resolveGradient(const icf::svg::SvgDocument& doc, const std::string& id,
                                 double bx0, double by0, double bx1, double by1,
                                 const icf::svg::Transform& ctm = icf::svg::Transform{},
                                 bool untaggedIsDisplayP3 = false);

}  // namespace rb
