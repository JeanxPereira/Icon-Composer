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
#include "Source/RenderBox/Image.h"

namespace rb {

struct RenderOptions {
    std::uint32_t width = 512;
    std::uint32_t height = 512;
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

}  // namespace rb
