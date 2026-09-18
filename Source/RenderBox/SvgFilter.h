#pragma once
// An SVG `<filter>` chain evaluated over a group's own rendering.
//
// WHY THIS IS A FILE OF ITS OWN
// -----------------------------
// A clip intersects per pixel and a mask multiplies per pixel, so either can be
// folded into each shape of a group and give the same answer as applying it to
// the composed group. A FILTER CANNOT: a blur of the sum is not the sum of the
// blurs. The group has to be composed into a target of its own first, and the
// chain applied to that -- which is the same shape of work `SvgRenderer` already
// does for a mask, and the reason this reuses `renderSvgPlaced` rather than
// growing a second renderer.
//
// WHAT THE TARGET BUILDS, AND WHAT THIS TRANSCRIBES
// -------------------------------------------------
// `[BIN]` The target constructs SIX primitives (`SVGFilter::filterPrimitive`,
// CoreSVG.arm64 0x2A230, table of six at `__const:0x327F0`) and drops the rest
// at construction; a chain holding a dropped one collapses to nothing drawn, and
// `SvgDocument` already reproduces that before a chain ever reaches here.
//
// Of the six, this transcribes THREE -- `feFlood`, `feBlend` and
// `feGaussianBlur` -- because those three are the whole of the corpus's one
// fully-supported chain, Delta's `00_stripes.svg`. `feOffset`, `feComposite` and
// `feConvolveMatrix` are REFUSED BY NAME: the target draws them, so a chain
// using one is a gap of ours and not a reproduction, and it has to say so.
//
// THE BLUR'S PLUMBING IS MEASURED AND ITS KERNEL IS NOT
// -----------------------------------------------------
// `[BIN]` `drawFeGaussianBlur` (0x23D20) reads `stdDeviation` as a CGPoint,
// CLAMPS EACH COMPONENT TO 100 (`fminnm` against `0x4059000000000000`), logs
// `"Different radii for gaussian blur not supported"` when the two differ and
// carries on with the X one, and then hands that number to
// `imageByApplyingFilter:withInputParameters:` as `CIGaussianBlur`'s
// `inputRadius`.
//
// `[OBS]` SO THE BLUR'S WIDTH IS NOT CLAIMED TO MATCH. `inputRadius` is not
// SVG's sigma, and what CoreImage builds from it lives in a framework this
// project has not read. What is transcribed here is the plumbing that WAS
// measured -- the clamp, the isotropy, the routing -- with the kernel taken as
// SVG 1.1's own Gaussian at `sigma = stdDeviation`. Every render that runs a
// blur says this in `notes`, the same way the glass ruler announces its own.
#include <string>
#include <vector>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/PathVertexOracle.h"

namespace rb {

struct FilteredGroup {
    // Straight (NOT premultiplied) RGBA, row major, the size of the canvas.
    std::vector<float> rgba;
    // True when the chain ran. False means `why` says what stopped it, and the
    // caller reports that rather than drawing a picture the chain did not make.
    bool ok = false;
    std::string why;
    // Said in words when something was drawn whose provenance is short of a
    // measurement -- the blur's kernel, today. Empty when there is nothing to
    // declare.
    std::vector<std::string> notes;
};

// Runs `filter` over `source`, which is the group's own rendering at the canvas
// size, straight RGBA. `placement` carries user space into pixels, which the
// filter region and `stdDeviation` both need.
// `originX`/`originY` sao a origem do alvo na grade da COLOCACAO: a regiao do
// filtro e medida contra `placement`, que e sempre a do canvas inteiro, entao
// o centro do pixel tem que ser o absoluto (spec 2026-09-16, "O invariante que
// governa o desenho"). Zero e o render cheio.
FilteredGroup applySvgFilter(const icf::svg::SvgDocument::Filter& filter,
                             const std::vector<float>& source,
                             std::uint32_t width, std::uint32_t height,
                             const PathGlobals& placement, std::int32_t originX = 0,
                             std::int32_t originY = 0);

}  // namespace rb
