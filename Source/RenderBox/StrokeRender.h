#pragma once
// An SVG shape's stroke, turned into pixels.
//
// WHAT THIS CLOSES
// ----------------
// `SvgRenderer` draws fills. A stroked shape was drawn WITH ITS FILL and the
// stroke dropped -- silently, with nothing in the report. That is the same
// defect the group blend had until 2026-09-03: a plausible picture and a clean
// report, which this project treats as worse than not drawing.
//
// `[ART]` It is the corpus's biggest single blocker: 31 layers across 10
// documents carry a painted stroke.
//
// WHERE THE ARITHMETIC COMES FROM, AND WHERE IT DOES NOT
// -------------------------------------------------------
// The point stream and the coverage are `StrokeGeometry`, transcribed from the
// binary. What is HERE is the two things the target does not do on the CPU and
// so cannot be transcribed:
//
//   * FLATTENING. `[OBS]` Doc 03 §7: the target hands cubic segments straight
//     to the GPU and never flattens on the CPU. `GlassLayer.h` already had to
//     make this choice for the distance field and made it the same way, with
//     the same knob -- `subdivisions`, 16 by default -- so a layer's stroke and
//     its glass are flattened to the same fidelity by construction.
//
//   * THE SCAN. Evaluating coverage per pixel over a bounding box is this
//     renderer's way of getting the shader's answer without the shader. It is
//     the same shape of decision as `DistanceField`'s O(W*H*E) scan, and it
//     carries no seal.
//
// THE WIDTH LIVES IN USER SPACE and the coverage in pixels, so the map's scale
// applies to it. `[BIN]` §31.4: the width never reaches the GPU as a width at
// all -- it arrives as `radius = width/2` per point -- which is why scaling it
// here, once, is the whole of the conversion.
#include <cstdint>
#include <vector>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/StrokeGeometry.h"

namespace rb {

// One subpath, flattened. `closed` is the `Z` the SVG wrote, and it changes the
// point stream's shape -- an open subpath gets ghosts, a closed one wraps.
struct FlatSubpath {
    std::vector<StrokePoint> points;
    bool closed = false;
};

// `subdivisions` splits each cubic into that many line segments. A `Move` opens
// a subpath; a `Close` shuts the current one and opens nothing.
std::vector<FlatSubpath> flattenForStroke(const icf::svg::Path& path, int subdivisions);

// The affine map a `PathGlobals` carries, applied to a point. Kept here rather
// than reaching into `PathVertexOracle` so the stroke path cannot drift from
// the fill path without the compiler noticing.
struct StrokePlacement {
    double m0[2] = {1.0, 0.0};
    double m1[2] = {0.0, 1.0};
    double m2[2] = {0.0, 0.0};
    // The uniform scale the map applies, which is what the stroke width is
    // multiplied by. `[INF]` Taken as the length of `m0`: the placement this
    // renderer builds is a scale and a translation with no rotation and no
    // shear (`SvgRenderer.cpp`), so the two axes agree. A map that sheared
    // would need a width per direction, and none is built.
    double scale = 1.0;
};

// The coverage of one shape's stroke, as an alpha image `width * height`.
// Returns empty when the shape is not stroked or the width rounds to nothing.
//
// `originX`/`originY` sao a origem do alvo na grade da COLOCACAO. O mapa que
// `placement` aplica poe o traco na coordenada do canvas inteiro, entao a
// amostragem tem que perguntar pelo ponto absoluto `(x + origem) + 0.5` e so o
// indice no resultado anda (spec 2026-09-16, "O invariante que governa o
// desenho"). Zero e o render cheio, termo a termo.
std::vector<float> rasteriseStroke(const icf::svg::Shape& shape,
                                   const StrokePlacement& placement,
                                   std::uint32_t width, std::uint32_t height,
                                   int subdivisions, const StrokeParams& base,
                                   std::int32_t originX = 0, std::int32_t originY = 0);

}  // namespace rb
