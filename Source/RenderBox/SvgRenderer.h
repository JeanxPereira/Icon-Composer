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
// COLOUR SPACE IS NOT CONVERTED, AND THAT IS REPORTED
// ----------------------------------------------------
// `color(display-p3 ...)` occurs about a dozen times in the corpus. Converting
// it to sRGB needs a matrix that has not been measured from the target, and
// drawing its components as though they were sRGB would shift every one of them
// silently. So such a shape IS drawn -- dropping it would be worse -- and its
// index is reported in `unconvertedP3`.
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
};

struct RenderOptions {
    std::uint32_t width = 512;
    std::uint32_t height = 512;
    // When set, every shape in the document is painted with this instead of its
    // own fill. The shape's own gradient reference is then not consulted.
    FillOverride override;
    // How many line segments one cubic becomes. `[INF]`, and inherited from
    // `BuildOptions`: the target carries the count in the buffer, so the RULE
    // that picks it lives on its CPU side and was not in the shader to read.
    int subdivisions = 16;
};

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
    // Drawn, but with components that were never converted out of display-p3.
    std::vector<std::size_t> unconvertedP3;
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
ResolvedGradient resolveGradient(const icf::svg::SvgDocument& doc, const std::string& id,
                                 double bx0, double by0, double bx1, double by1);

}  // namespace rb
