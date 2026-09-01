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
#include <cstdint>
#include <string>
#include <vector>

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/SvgRenderer.h"

namespace rb {

// `[INF]` The canvas the document's points are measured in. See the header note.
inline constexpr double kCanvasPoints = 1024.0;

struct IconRenderOptions {
    std::uint32_t size = 512;
    icf::Context context;
    int subdivisions = 16;
};

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

    std::size_t drawn = 0;
    std::size_t total = 0;
    std::vector<SkippedLayer> skipped;
    // Shapes inside drawn layers that the SVG renderer itself could not draw.
    std::vector<std::string> shapeGaps;
};

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
PathGlobals placeOnCanvas(const icf::svg::ViewBox& box, const LayerPlacement& p,
                          std::uint32_t size);

Result<RenderedIcon> renderIcon(Device& device, const icf::IconBundle& bundle,
                                IconRenderOptions options = IconRenderOptions{});

}  // namespace rb
