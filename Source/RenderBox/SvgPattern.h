#pragma once
// A `<pattern>` fill resolved to a place in a decoded raster.
//
// WHAT THIS IS FOR, AND WHY IT IS ONE THING AND NOT THREE
// -------------------------------------------------------
// `use`, `pattern` and embedded raster were three separate lines of
// `scripts/slice-reach.py`, and they are one construct: how Figma writes "this
// shape is filled with a picture". A shape fills with `url(#p)`, the pattern
// holds ONE `<use>`, and the `<use>` names an `<image>` carrying a
// `data:image/png;base64` payload. `[ART]` All 11 of the corpus's patterns are
// exactly that, across three files, all Delta's.
//
// `[BIN]` And the target draws it, which is NOT what the same question about
// `<filter>` returned: `SVGPattern` is a class with `draw` (CoreSVG.arm64
// 0x12184), `drawCells` (0x11D50) and `attributeIsUserSpace` (0x11FA8), reaching
// `CGPatternCreate` and `CGContextSetFillPattern`. So this is a transcription
// with nothing to refuse on the target's behalf -- the refusals below are all
// OURS, and each says so.
//
// THE TILE IS BIGGER THAN THE SHAPE, IN EVERY CORPUS CASE
// -------------------------------------------------------
// `[ART]` `width` and `height` are in `objectBoundingBox` units and run from
// 2,93 to 82,34 -- that is 2,93 to 82 times the shape's own box. So one tile
// covers the shape and THE TILING NEVER HAPPENS in this corpus. It is built
// anyway, because refusing a second tile would be a limit with no reason behind
// it, and it is written down here that it has no witness -- the same standing as
// the seven caps and three joins of the stroke, of which the corpus exercises
// one pair (doc 03 §31).
#include <cstdint>
#include <string>

#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"

namespace rb {

struct ResolvedPattern {
    bool ok = false;
    std::string why;  // when `ok` is false, in words

    // The tile, in the SVG's user space.
    double tileX = 0, tileY = 0, tileW = 0, tileH = 0;
    // An offset INSIDE the tile, carried to a pixel of `image`. Row-major 2x3.
    double m[6]{1, 0, 0, 0, 1, 0};
    const icf::DecodedPng* image = nullptr;
};

// Resolves `id` against the document's patterns for a shape whose user-space
// bounding box is `bx0..by1`, with `image` the already-decoded raster the
// pattern's `<use>` names. `image` may be null, which is itself a refusal.
ResolvedPattern resolvePattern(const icf::svg::SvgDocument& doc, const std::string& id,
                               double bx0, double by0, double bx1, double by1,
                               const icf::DecodedPng* image);

// Samples the pattern at a point in user space, into straight RGBA. Returns
// false where the tile carries nothing -- outside the image, which happens when
// the `<use>`'s transform does not fill the tile it sits in.
bool samplePattern(const ResolvedPattern& p, double ux, double uy, float out[4]);

}  // namespace rb
