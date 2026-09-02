#pragma once
// The glass layer, JOINED: the shape, the field, the displacement map, and the
// backdrop refracted through it.
//
// Every stage this file calls already passes a gate of its own. Nothing here is
// a new transcription; what is new is the JOIN, and the join is where the two
// unread things live. They are named in the two sections below rather than
// papered over, because a wrong pixel that looks right is the worst thing this
// project can produce.
//
// WHAT THE ICON'S GLASS ACTUALLY IS
// ---------------------------------
// `[ART]` `RenderBox` exports five system shaders and `IconRendering` imports
// exactly one, `_RBSystemShaderDisplacementMap`; the string `glassBackground`
// appears zero times in all seven non-RenderBox slices (spec §4.4, doc 03 §29).
// So the icon's glass is NOT `glassBackground_v1`. It is displacement
// refraction, in four steps:
//
//   1. group material -> eight fields -> points   `GlassMaterial.h`
//   2. shape -> signed distance field             `DistanceField.h` (OURS)
//   3. field + params -> displacement map         `SdfDisplacement.h`
//   4. map + source -> refracted pixels           `DisplacementOracle.h`
//
// and the missing link between 1 and 2 -- turning a `CoreSVG` path into the
// polylines `generateField` eats -- is the first half of this file.
//
// GAP ONE, THE RULER. `[OBS]`
// ---------------------------
// `refractionHeight` denormalises to POINTS: `[ART]` the corpus's two enabled
// `refractivity` entries give 105.32 and 134.51 pt. `SdfDisplacement`'s
// `kBandCutoff = -5.0` is a LENGTH in the FIELD's units. The two coincide only
// at one pixel per point, i.e. only at `options.size == kCanvasPoints`.
//
// This file applies the obvious linear factor, `pointsToPixels = size / 1024`,
// and marks it `[INF]`: nothing was read that says the target scales this way,
// and `GlassMaterial.h` records the last hop -- `height x (n - 2) / [self+0x568]`
// at `0x10C8C` -- with the divisor UNIDENTIFIED. The sibling project hit the
// same wall from the other side (AquaKit, 2026-08-30: the recipe delivers
// points and the shader measures pixels).
//
// `[OBS]` And the factor cannot be applied to all of it. `kBandCutoff` lives
// INSIDE the transcribed `sdfdisp::alpha`, where it belongs -- it is Apple's
// constant and this file does not get to rewrite it. So at any size other than
// 1024 the band's hard cutoff sits at the wrong distance. `glassRulerNote`
// returns the sentence that says so, and the renderer reports it rather than
// drawing quietly.
//
// GAP TWO, RASTER ART HAS NO CONTOURS.
// ------------------------------------
// `[ART]` Of the corpus's 171 glass-true layers, 144 name `.svg` art, 45 name
// `.png` and one names `.heic`. A raster has no path to flatten, so steps 2-4
// have no shape to start from. That is a DIFFERENT gap from "the glass is not
// transcribed" and it gets its own reason string in the renderer. Folding the
// two together would hide the fact that the glass now draws.
//
// WHAT WOULD CLOSE IT, and it is not a contour tracer: the field could be built
// from the raster's ALPHA instead of from a path. That is a second field
// generator, with its own error behaviour, and it is not written here.
#include <cstdint>
#include <string>
#include <vector>

#include "Source/CoreSVG/Document.h"
#include "Source/RenderBox/DisplacementOracle.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/SdfDisplacement.h"

namespace rb {

// ===========================================================================
// PART ONE -- the flattener. OUR CODE, NOT A TRANSCRIPTION.
// ===========================================================================
//
// `[OBS]` Nothing in the target flattens a path on the CPU. Doc 03 §7 measured
// it: `PathBuffer` hands cubic segments straight to the GPU and "the path is
// never flattened on the CPU, anywhere". That is a finding about the TARGET's
// rasteriser, not a prohibition on us -- `generateField` wants polylines, and
// somebody has to make them. So this carries no seal at all.
//
// THE ERROR, STATED. A flattened cubic is a polygon inscribed in the curve, so
// every point of the polygon is on or inside the true curve and the distance
// field it generates is wrong by at most the SAGITTA of one subdivision. That
// is the same bound `DistanceField.h` argues for the circle and the same one
// `test_rb_field.cpp` measures: an N-gon on a circle of radius r deviates by
// `r * (1 - cos(pi/N))`. `IconRenderOptions::subdivisions` (16 by default) is
// the knob, and it is the SAME knob the coverage rasteriser already uses, so a
// layer's glass and a layer's fill are flattened to the same fidelity by
// construction.

// One SVG document's outline, in the pixel space a `PathGlobals` maps into.
struct GlassContours {
    std::vector<FieldContour> contours;

    // The fill rule the field's SIGN is taken with. `generateField` takes one
    // rule for the whole shape and an SVG may carry a different rule per shape.
    FieldRule rule = FieldRule::NonZero;

    // Set when the document's painted shapes do NOT agree on a fill rule. The
    // caller is expected to NAME the layer rather than pick one: choosing
    // silently would flip the inside/outside of part of the shape, and the
    // picture would still look like a picture.
    bool mixedRules = false;

    // Shapes whose `fill` is `none` contribute no area, so they contribute no
    // contour. Counted so a layer that turns out to be all-stroke reports as
    // empty instead of as a shape with no inside.
    std::size_t unpaintedShapes = 0;
};

// Every painted subpath of `doc`, transformed by `globals` (`world = m0*x +
// m1*y + m2`, the same affine `placeOnCanvas` builds and the same one
// `PathVertex.glsl` applies), with each cubic cut into `subdivisions` chords.
//
// The closing segment is IMPLIED, as `FieldContour` requires: an explicit `Z`
// does not repeat the first point, and a subpath left open is closed anyway --
// a distance field has no notion of an open region, and refusing to close would
// give the shape an inside that leaks.
GlassContours flattenSvgToContours(const icf::svg::SvgDocument& doc,
                                   const PathGlobals& globals, int subdivisions);

// ===========================================================================
// PART TWO -- the parameters, and the ruler
// ===========================================================================

// `[INF]` The linear factor between the document's POINTS and this render's
// PIXELS. See GAP ONE. `[ART]` `kCanvasPoints = 1024` is corroborated by five
// `ICRRenderingParameters` constants landing on exact fractions of 1024
// (spec §4.4), and by nothing that states it.
inline double glassPointsToPixels(std::uint32_t size) {
    return static_cast<double>(size) / kCanvasPoints;
}

// The sentence a render at a size other than 1024 has to carry, or the empty
// string when the ruler is exactly 1 and there is nothing to declare.
std::string glassRulerNote(std::uint32_t size);

// What one glass layer refracts with: the four scalars
// `RB::DisplayList::State::add_glass_displacement`'s XML serialiser names
// (`component . gradient . range . offset . height . curvature . angle .
// mask-offset`, `0xEDB24`), plus the shader's own argument and variant.
struct GlassRefraction {
    // `[BIN]+[INF]` `denormaliseRefractionHeight` gives POINTS; this is that
    // number in FIELD PIXELS, by the `[INF]` factor above.
    float heightPixels = 0.0f;

    // `[BIN]` `+[CASDFGlassDisplacementEffect defaultValues]` @ `0x18b316684`,
    // a frozen dictionary of COUNT 3. `[OBS]` No `.icon` key maps to `angle` or
    // `curvature` -- the document's `refractivity` carries `strength` and
    // `depth` and nothing else -- so these are the effect's defaults and not a
    // reading of the document.
    float curvature = sdfdisp::kDefaultCurvature;
    float angleCos = 1.0f;   // cos(kDefaultAngle) = cos(0)
    float angleSin = 0.0f;

    // `[OBS]` `offset` and `mask-offset` are named by RenderBox's serialiser
    // and are NOT in the frozen dictionary of three. `[INF]` They are left at
    // zero because an Objective-C object's ivars are born zero and the setter
    // is simply never called -- which is `SdfDisplacement.h`'s own reading of
    // why `kDefaultMaskOffset` does not exist. Zero here is an inference about
    // an uninitialised ivar, not a fourth measurement.
    float offset = 0.0f;
    float maskOffset = 0.0f;

    // `[BIN]` `displacementMap_v1`'s `params[0]`, which `IconRendering` sets to
    // the NEGATED strength (`0x10C14`, `0x805F4`), converted to pixels by the
    // same `[INF]` factor. A displacement offset is a length in `p`'s space and
    // `p` is pixels here, so it scales exactly as the height does.
    float scalePixels = 0.0f;

    // `[BIN]` `ICRRenderingParameters.refractionSupersampling = 2` goes to
    // `-[RBShader setVariant:]`, which is `RB::shaderVariant`, which is what
    // `displacementTapCount` switches on. Variant 2 is the four-tap pattern.
    std::uint32_t variant = 2;
};

// The material's numbers, put on this render's ruler.
GlassRefraction glassRefractionFor(const DenormalisedGlass& glass, std::uint32_t size);

// `[INF]` A zero displacement scale is the identity and this says so.
//
// `displacementDecodeOffset` is `fma(disp, 2s, -s)`; at `s == 0` that is exactly
// zero for EVERY map value, so no sample moves. `[ART]` It is also the common
// case by a wide margin: only 5 of the corpus's 271 groups carry `refractivity`
// at all and only 2 of those carry a non-zero strength.
//
// The renderer uses this to skip the whole refraction rather than run it, and
// the reason is not speed. `displacementMap_v1` SUPERSAMPLES: at variant 2 it
// averages four taps jittered off the pixel centre. With a zero offset those
// four taps still read four different texels, so running the stage anyway would
// BLUR the backdrop by a quarter of a pixel -- a visible change produced by a
// parameter that says "do not refract". Identity is the correct answer and it
// is what `Tests/test_glass_layer.cpp` pins bit-for-bit.
bool glassRefractionIsIdentity(const GlassRefraction& r);

// ===========================================================================
// PART THREE -- the displacement map, and the composite
// ===========================================================================

// The map in the encoding `displacementMap_v1` decodes, four floats per pixel:
//
//   .x .y   `0.5 + 0.5 * disp`, so that `fma(disp, 2s, -s)` recovers
//           `disp * s`. `[OBS]` The ENCODING is not read -- the target's map is
//           a texture somebody else writes and this project never saw it
//           written. What IS read is the decode, and 0.5 is the only bias that
//           makes the decode's own neutral point (`DisplacementOracle.h`: "a
//           map of exactly 0.5 gives an offset of exactly zero") agree with a
//           displacement of zero.
//   .z      the PER-TAP WEIGHT the shader splats over the colour. It is 1 here:
//           the shader already divides by the tap count, so any other weight
//           would darken the refracted backdrop.
//   .w      the mask `sdfdisp::alpha`. `displacementMap_v1` never reads `.w`;
//           it is carried for the composite below. `[OBS]` Where the target
//           consumes this mask was not read.
struct DisplacementImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;
};

// `[INF]` `fwidth(t)` on a pixel grid.
//
// The transcription takes `fwidthT` as an argument because a CPU has no
// fragment quad (`SdfDisplacement.h`). Here it is supplied analytically:
// `t = -(d + offset) - maskOffset`, and the field's gradient is a unit vector
// pointing the way `d` increases, so `dt/dx = -gx` and `dt/dy = -gy` at one
// field pixel per output pixel. GLSL's `fwidth` is `|dFdx| + |dFdy|`, hence
// `|gx| + |gy|`. Exposed so a test can hold it instead of a magic number.
float glassFilterWidth(const FieldSample& sample);

// The map for one field. One `sdfdisp::glassDisplacement` per pixel.
DisplacementImage glassDisplacementMap(const FieldImage& field, const GlassRefraction& r);

// The glass composite, and the fork of `over`.
//
// `acc` is PREMULTIPLIED, as it is everywhere inside `renderIcon` while layers
// stack, and it stays premultiplied: the refracted colour is sampled from a
// premultiplied buffer and mixed back into one, which is the only form in which
// a weighted average of two coverages means anything. That invariant is a bug
// this repository already paid for once.
//
//   acc' = mix(acc, refract(acc), mask)
//
// so the backdrop is bit-identical wherever the mask is zero -- outside the
// shape, and past the band's cutoff. The `mix` is the composite this file
// CHOOSES; `[OBS]` the target's own blend of the refracted backdrop against the
// unrefracted one was not read, and the mask that gates it is the shader's own
// `.z` output, which is the only thing that makes the choice more than free.
void glassOver(std::vector<float>& acc, std::uint32_t width, std::uint32_t height,
               const DisplacementImage& map, const GlassRefraction& r);

}  // namespace rb
