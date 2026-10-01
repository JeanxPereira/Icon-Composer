#pragma once
// `translucency`: from the group's material to the pixel, through a mask.
//
// Source: `Docs/Laudos/2026-09-15-translucencia.md`, which reads the whole
// consumption out of `References/2.0-125/out/slices/IconRendering.arm64`. Every
// address quoted below is that slice's (VA == file offset).
//
// WHAT `translucency` IS, AND WHAT IT IS NOT
// ------------------------------------------
// `[BIN]` It is NOT a layer alpha and NOT a blend mode. The `alpha` argument of
// the `drawShape:fill:alpha:blendMode:` that finishes this path is the constant
// `1.0` (`0x103F4`) -- the proof by negation that closes the question. What it
// IS is the WEIGHT with which a whole opacity profile,
// `ICRRenderingParameters.glyphTranslucency`, is interpolated in from 1.0:
//
//     f      = glyphTranslucency.strength[sizeClass] x material.translucency
//     eff(x) = 1 - (1 - x) * f                  ; i.e. mix(1.0, x, f)
//
// `[BIN]` `0x0000FDDC`-`0x0000FDF4` (`ldr d1, [x9]` / `ldr d2, [x1, #0x10]` /
// `fmul d12, d1, d2` / `fsub` `fmul` `fsub`), and the SAME `f` in `d12` again at
// `0x0000FF48`-`0x0000FF50` over `upperContourOpacity`.
//
// `[BIN]` There is NO `pow`, NO clamp, NO floor and NO ceiling anywhere on this
// path: the four `_pow` callers `GlassMaterial.h` already inventoried are still
// all of them, and none is the function `0x0000FD28` that consumes this field.
// So `translucency` reaches the multiplication RAW. That is also why there is no
// `translucencyMax`/`translucencyPower` pair to look for -- the destination is
// already an opacity, and an opacity needs no denormalisation.
//
// `f == 0` gives `eff(x) == 1.0` for every `x`: opaque, no effect at all. It is
// a TAP, not a scale.
//
// THE CONSUMER, NAMED
// -------------------
// `[BIN]` The results become arguments 1 and 2 of the shader
// `simplifiedShapeAwareGradientMask` (`0x0000FFC8`,
// `initWithLibrary:function:`, the Swift literal of length 32 built at
// `0x0000FF8C`): `opacityBounds` and `contourOpacityBounds`. The seven
// `setArgumentBytes:atIndex:type:count:flags:` at `0x00010074`-`0x00010204` bind
// them, and `0x000102A0` installs the same effect for CoreImage under
// `"CUISimplifiedShapeAwareGradientMask"`.
//
// THE THREE TRAPS THE LAUDO NAMES, AND WHERE EACH ONE IS ANSWERED HERE
// --------------------------------------------------------------------
//   1. THE ENUM RUNS BACKWARDS. `sizeBasedValue` below, and it is the reason
//      that function exists at all rather than being an array index at the call
//      site.
//   2. `borderWidth` DEFAULTS TO ZERO, so `contourOpacityBounds` is never
//      distinguishable from `opacityBounds` with the defaults of generation 27.
//      The contour path was transcribed because the AIR was read, before
//      anything used it. GENERATION 26 USES IT: `0x76FC0` writes a border of
//      25.8 points and a contour pair of its own (`RenderingParameters.h`), and
//      it is that generation, not 27, that takes the shader this file
//      transcribes -- see `TranslucencyEffect::useSimpleMask`.
//   3. THERE IS A SECOND CONSUMER OF `translucency`, inside the SHADOW
//      (`t = clamp01(translucency / Shadow.translucencyForMaxOverdraw)`,
//      `Docs/Laudos/2026-09-15-sombra.md`). It is NOT this one, it is not
//      implemented anywhere in this tower, and nothing in this file touches it.
//      It is named so that the two are never confused for one.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/DistanceField.h"

namespace rb {

// `[BIN]` The four-case size enum that sits three entries from `SizeBasedValue`
// in the same metadata block (`0xA5800`), in ITS OWN declaration order.
enum class IconSizeClass : std::uint8_t { Small = 0, Medium = 1, Large = 2, Display = 3 };

// `[BIN]` `IconRendering.SizeBasedValue` (metadata `0xA5780`): four `Double`
// declared `display, large, medium, small` -- which is the enum above REVERSED.
//
// The slots are an array and not four named members on purpose: the selection is
// an index computation, and writing it as one is what makes the inversion
// visible instead of hidden inside a switch.
struct SizeBasedValue {
    // [0] display  [1] large  [2] medium  [3] small
    double slots[4] = {1.0, 1.0, 1.0, 1.0};
};

// `[BIN]` The selection, from the `csel` ladder at `0x0000FD9C`-`0x0000FDC8`.
// The byte it switches on is read at `0x0000FDA0` from `self+0x469F`; the ladder
// resolves to
//
//     w10 == 0 -> self+0x348 (strength.small)    w10 == 2 -> self+0x338 (large)
//     w10 == 1 -> self+0x340 (strength.medium)   w10 >= 3 -> self+0x330 (display)
//
// which is, slot for slot, `slots[3 - sizeClass]`.
//
// THIS IS THE BUG NO TEST OF TODAY WOULD CATCH. `[BIN]` The aggregate
// initialiser at `0x0005ECE8`-`0x0005ECF4` writes `1.0` into all four slots, so
// with the read defaults `slots[c]` and `slots[3 - c]` return the same number
// and produce a bit-identical picture. It only wakes up on a parameter file that
// differentiates the size classes -- `[OBS]` laudo §7.3: `TranslucencyEffect` is
// `Codable` and such a file exists outside these binaries. So the inversion is
// written the way it was read, and this paragraph is why.
double sizeBasedValue(const SizeBasedValue& v, IconSizeClass sizeClass);

// `[BIN]` `ICRRenderingParameters.glyphTranslucency: TranslucencyEffect`, eight
// fields, read in order in seven consecutive instructions at
// `0x0000FD74`-`0x0000FDA4` (field descriptor `0xA49FC`, sub-struct size `0x50`,
// embedded at `ICRRenderingParameters+0x2B8` == `self+0x320`).
//
// The defaults below are `[BIN]` too, from the aggregate initialiser at
// `0x0005ECE0`-`0x0005ED00`.
struct TranslucencyEffect {
    // `[BIN]` `str xzr, [x19, #0x2b8]`. ZERO, and it is load-bearing: at zero the
    // `fcsel` at `0x0000FF5C`-`0x0000FF70` makes `contourOpacityBounds` a COPY of
    // `opacityBounds`, which is how "no border" is expressed without a boolean.
    // In CANVAS POINTS: the shader is handed it converted to texels of the SDF
    // (`opacityMaskArguments`). Generation 26: `25.8` (`0x77F18`).
    double borderWidth = 0.0;

    // `[BIN]` `strh w25` with `w25 == 0x101`: both true in generation 27. They
    // select branches in `0x0000FD28` that live above the mask: `useSimpleMask`
    // (`self+0x329`, `cbz w8` at `0x0000FE00`) picks an axial GRADIENT on an
    // infinite shape instead of the shader, and `replicateBadSmoothing`
    // (`0x0000FE74`) forks inside that between 17 smoothed stops and 2.
    //
    // `[BIN]` SO THE SHADER BELOW IS GENERATION 26's MASK, where `useSimpleMask`
    // is `false` (`0x77F20`), and generation 27's is the gradient
    // (`gradientOpacityMask`). Until 2026-10-01 this renderer drew 27 with the
    // shader at `borderWidth == 0`, which has the gradient's vertical profile
    // and adds the shape's coverage to it; the target's 27 mask has no SDF term
    // at all. `opacityMaskArguments` picks the branch off this field.
    bool replicateBadSmoothing = true;
    bool useSimpleMask = true;

    SizeBasedValue strength{};   // `[BIN]` all four 1.0

    // `[BIN]` `lowerOpacity = 0.0`, `upperOpacity = 1.0`,
    // `lowerContourOpacity = 0.0`, `upperContourOpacity = 1.0`.
    double lowerOpacity = 0.0;
    double upperOpacity = 1.0;
    double lowerContourOpacity = 0.0;
    double upperContourOpacity = 1.0;
};

// The block as generation 27 initialises it. The renderer takes the
// generation's own from `RenderingParameters::glyphTranslucency`.
inline constexpr TranslucencyEffect kGlyphTranslucency{};

// `[BIN]` WHETHER THE MASK IS BUILT AT ALL, as the group's content pass decides
// it before calling `0x0000FD28` (`0x4ACB4`-`0x4AD88`): effects enabled, a
// non-empty SDF, and
//
//     0x4AD08  ldr  d0, [sp, #0x10]      ; material.translucency, RAW
//     0x4AD0C  fcmp d0, #0.0
//     0x4AD10  b.le 0x4AD8C              ; not above zero: no layer, no clip
//
// The test is on the document's number and not on the profile it would open.
// Until 2026-10-01 this renderer asked `opacityMaskIsIdentity` instead, which
// agrees with it while every bound at `f == 0` is 1.0 -- generation 27 -- and
// does not in generation 26, whose `lowerContourOpacity` is `0.61` RAW: there a
// group with no translucency at all would have been given a rim.
bool translucencyDrawsMask(double materialTranslucency);

// `[BIN]` `f = strength[sizeClass] * material.translucency` -- `fmul d12, d1, d2`
// at `0x0000FDE4`, and nothing else happens to either operand first.
double translucencyFactor(const TranslucencyEffect& effect, IconSizeClass sizeClass,
                          double materialTranslucency);

// `[BIN]` `eff(x) = 1 - (1 - x) * f`, the `fsub`/`fmul`/`fsub` of
// `0x0000FDEC`-`0x0000FDF4`, written in that order because the order is what was
// read. Algebraically `mix(1.0, x, f)`; not rearranged, since the two differ in
// the last bit and this project's differentials are bit-for-bit.
double effectiveOpacity(double x, double f);

// The two `float2` the shader is handed, plus the two scalars that shape them.
//
// `[BIN]` THE ASYMMETRY IS THE FINDING, and a transcriber would smooth it over:
// only `lowerOpacity` and `upperContourOpacity` pass through `eff()`.
// `upperOpacity` and `lowerContourOpacity` go RAW (`0x10078`-`0x10088` writes
// `(lowerOpacity_eff, upperOpacity)`; `0x0000FF48` is the only other `eff` site
// and it is over `d11` == `upperContourOpacity`). With the defaults that is
// invisible -- `eff(1.0) == 1.0` and the contour pair is a copy -- and with a
// loaded parameter file it is not.
// `[BIN]` The two branches of `0x0000FD28`, picked by `useSimpleMask`
// (`cbz w8, #0xfea8` at `0x0000FE00`).
enum class OpacityMaskKind : std::uint8_t {
    // `simplifiedShapeAwareGradientMask`, over the SDF. Generation 26.
    Shader = 0,
    // An axial gradient on an infinite shape: no SDF. Generation 27.
    Gradient = 1,
};

// The most segments the gradient's ramp has: sixteen, between seventeen stops.
inline constexpr std::uint32_t kOpacityMaskRampSegments = 16;

struct OpacityMaskArguments {
    // In TEXELS of the distance field -- pixels of the render.
    float borderWidth = 0.0f;

    // index 1 of the shader: `(lower_eff, upper)`.
    float opacityBounds[2] = {1.0f, 1.0f};
    // index 2: the `fcsel` of `0x0000FF5C`-`0x0000FF70`.
    float contourOpacityBounds[2] = {1.0f, 1.0f};

    // The rect the vertical ramp is measured in, TARGET PIXELS, as
    // `(x, y, width, height)` -- shader argument 6.
    //
    // `[OBS]` WHICH rect the target reports there was not read. `0x10140`-
    // `0x101B8` calls four rect accessors and the laudo did not follow them to
    // their owner. This is the same open question `IconRenderer.h` already
    // records for a gradient's placement rect -- the element's constructed
    // bounds, the art's viewBox, or the tight box of the paths actually drawn --
    // and the renderer answers it the same way, with the art's viewBox placed on
    // the canvas, so that the two do not disagree about the same unread thing.
    float bounds[4] = {0.0f, 0.0f, 1.0f, 1.0f};

    // WHICH BRANCH. With `Gradient` the two contour bounds and the border are
    // not read, `bounds` is the frame on the CANVAS -- no texture-size term and
    // no one-texel shift, `0x0000FE04`-`0x0000FE70` -- and the profile is
    // `ramp`: one cubic per segment, `c0 + f (c1 + f (c2 + f c3))`, the
    // coefficients computed once (`opacityMaskArguments`) so that the CPU and
    // the GPU evaluate the same sixteen floats times four.
    OpacityMaskKind kind = OpacityMaskKind::Shader;
    std::uint32_t rampSegments = 0;
    float ramp[kOpacityMaskRampSegments][4] = {};
};

// `[BIN]` The material's `translucency` (`Icon.GlassMaterial+0x10`, `ldr d2,
// [x1, #0x10]` at `0x0000FDE0`) turned into the arguments above.
//
// `[BIN]` `texelsPerPoint` is what `borderWidth` is converted with,
// `0x00010048`-`0x00010054`:
//
//     scvtf d0, x25            ; n = the SDF texture's width - 2
//     fdiv  d0, d0, d9         ; / self[0x568], the canvas width
//     fmul  d0, d8, d0         ; x borderWidth (the DOUBLE, in canvas points)
//     fcvt  s0, d0             ; and only then a float
//
// -- `n / canvasWidth`, which on a square render is `size / 1024`. The test
// that makes the contour pair a copy of the body pair (`fcmp d8, #0.0`,
// `0x0000FF5C`) is on the UNCONVERTED double. Until 2026-10-01 the width went
// to the shader in points, which no pixel showed while it was zero. The
// default of 1 is one texel per point: a 1024 px render.
OpacityMaskArguments opacityMaskArguments(const TranslucencyEffect& effect,
                                          IconSizeClass sizeClass,
                                          double materialTranslucency,
                                          double texelsPerPoint = 1.0);

// `[BIN]` THE GRADIENT BRANCH, `0x0000FE04`-`0x0001064C`, one pixel. Generation
// 27's mask:
//
//   * The rect is `effectsFrame x canvas`, in canvas points: four `CGRectGet*`
//     each multiplied by `self[0x568]` or `self[0x570]` (`0x0000FE1C`-
//     `0x0000FE70`). No `(n - 2) / canvas` scale and no `-1`.
//   * `replicateBadSmoothing` (`cbz w24`, `0x0000FE74`) picks the stops. Set:
//     `0xD28C(0, 1, 0.0625, upper, lowerEff)` strides `x` from 0 through 1 by a
//     sixteenth -- SEVENTEEN stops -- each black with
//
//         alpha = S(upper + (lowerEff - upper) x) ,  S(a) = (a a)(3 - (a + a))
//
//     (`0xD3A4`-`0xD3B8`, no clamp). Clear: two stops, `upper` at 0 and
//     `lowerEff` at 1, raw (`0x00010484`-`0x000104A8`).
//   * The shape is made INFINITE (`setInfinite`, `0x00010524`) and filled with
//     `setAxialGradientStartPoint:(midX, minY) endPoint:(midX, maxY)
//     stopCount: colors: colorSpace:0 locations: flags:0x400` (`0x0001062C`),
//     then `drawShape:fill:alpha:1 blendMode:0` (`0x0001064C`).
//
// `flags 0x400` is interpolation code 4, the monotone cubic of
// `GradientOracle.h` (`smoothColorCoefficients`), over evenly spaced stops. So
//
//     mask = cubicRamp(saturate((y - minY) / height))
//
// at every pixel of the group's image: there is no distance, no coverage and
// no border in it. Where a stop sits (`v = k/16`) the ramp IS the stop, which
// is the shader's own `a a (3 - 2a)` of the body ramp -- "replicate bad
// smoothing" -- and between stops it is the cubic through them.
//
// `[INF]`/`[OBS]` under it, the same as for every document gradient: the
// target stores the coefficients as `half` and runs the spline on premultiplied
// colour (`GradientOracle.h`); here they are `float`, and the colour is black,
// so only the alpha channel exists. And WHICH end of the rect is `minY` depends
// on the y-handedness of the RB display list, the gap
// `kGradientAxisDirectionNote` names.
//
// `py` is the pixel centre, `y + 0.5`, on the grid `bounds` is on.
float gradientOpacityMask(const OpacityMaskArguments& args, float py);

// True when the mask is 1.0 at every pixel, so applying it is a no-op.
//
// `f == 0` is the case that matters: `eff(x) == 1` for all `x`, all four bounds
// become 1.0, the ramp is flat at 1.0 and `mix(1.0, 1.0, cov) == 1.0`. This
// tests the four bounds rather than `f`, so a parameter file that happens to
// describe a flat profile is recognised too.
bool opacityMaskIsIdentity(const OpacityMaskArguments& args);

// `[BIN]` `simplifiedShapeAwareGradientMask`, transcribed from
// `References/2.0-125/out/metallib-iconrendering/default_mod4.ll:153-196`:
//
//     u        = saturate(sd / borderWidth)
//     v        = saturate((py - bounds.y) / bounds.w)
//     body     = mix(opacityBounds.y,        opacityBounds.x,        v)
//     contour  = mix(contourOpacityBounds.y, contourOpacityBounds.x, v)
//     a        = clamp(mix(contour, body, u), 0, 1)
//     a        = a*a*(3 - 2*a)
//     cov      = saturate(sd + 1)
//     out      = mix(1.0, a, cov)
//
// `[INF]` "lower" and "upper" are VERTICAL POSITIONS, not a numeric range: `.y`
// (upper) rules at the top of `bounds` and `.x` (lower) at the bottom. Reading
// them as a min/max would run the ramp backwards and still look like a picture.
//
// `sd` IS PASSED IN, and that is the one place this departs from the IR's shape.
// The shader recovers it from a texture -- `sd = (sdfZero - t.sample(p).r) *
// sdfScale` with `sdfZero == 0.5` (`0x1010C`) and `sdfScale` pre-multiplied by
// `-2.0` (`0x100D8`-`0x100E8`) -- i.e. from an ENCODING of a distance field that
// this project has never seen written. `[OBS]` So the encoding is not modelled;
// the decoded quantity is, and the caller supplies it. `sd` is POSITIVE INSIDE
// (that is what makes `u` rise with depth and `cov` reach 1 in the interior),
// while `FieldSample::distance` is NEGATIVE INSIDE, so the caller negates. The
// unit is one field pixel, which is what `saturate(sd + 1)` measures with.
//
// THE DIVISION BY ZERO, AND THE ONE DEVIATION THIS FILE ADMITS TO. With
// `borderWidth == 0` -- the read default -- `sd / borderWidth` is an infinity,
// and `mix(c, b, inf)` with `c == b` is `c + 0 * inf`, a NaN. The `fcsel` above
// guarantees `c == b` in exactly that case, so the interpolation is the identity
// for any finite `u` and `u` is not needed. This takes that branch explicitly
// rather than evaluating the division. `[INF]` Whether the target's shader
// reaches the same answer by a guard, by a fast-math contraction, or because
// `borderWidth` is never zero in a real parameter set was NOT read.
float simplifiedShapeAwareGradientMask(const OpacityMaskArguments& args, float sd, float py);

// The mask over a whole field, one float per pixel.
struct OpacityMask {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> a;

    // The shader's own `cov = saturate(sd + 1)`, kept beside the mask.
    //
    // It is not decoration and it is not debugging. Where `cov == 0` the mask is
    // `mix(1.0, a, 0) == 1.0` NO MATTER what the ramp says -- so a pixel the art
    // paints but the distance field calls "outside" silently keeps its full
    // opacity. That happens for real: `flattenSvgToContours` merges every
    // painted subpath of an SVG into ONE contour set signed by ONE fill rule, so
    // two overlapping subpaths wound the opposite way cancel to outside under
    // `non-zero` (the caveat `DistanceField.h` states for overlapping contours).
    // Measured on `DimensionDev/Flare/AppIcon`: its six petals carry four
    // negative and two positive signed areas, and the winding at the middle of
    // the bottom-centre petal is 0. The renderer counts these against the art's
    // own alpha and says so in `notes`; without this channel it could not tell
    // "opaque because the ramp says opaque" from "opaque because the shape was
    // not there".
    std::vector<float> coverage;
};

// `sd = -field.distance`, and `py` is the PIXEL CENTRE `y + 0.5` -- the point
// `generateField` sampled the field at, so the mask and the field are measured
// at the same place.
//
// With `args.kind == Gradient` the field gives only the grid: every texel of a
// row gets `gradientOpacityMask`, and `coverage` is 1 everywhere -- that mask
// has no "outside", so it has no blind spot to count either.
OpacityMask glassOpacityMask(const FieldImage& field, const OpacityMaskArguments& args);

// The mask applied to one layer's art: `rgba` is STRAIGHT colour with its alpha
// in `.w`, which is the form `renderSvgPlaced` and `placeRaster` both hand to
// `blendOver`, so only `.w` is touched.
//
// `[OBS]` That the composite is a multiplication of the glyph's alpha is a
// reading of what an opacity mask IS, not a transcription of how the target
// applies it: `0x000103F0` sets the shader with flags this project did not
// decode and `0x00010408` draws the shape, and where the resulting grey lands is
// not in the laudo. What IS read is that it cannot be the draw's `alpha`, which
// is the constant 1.0.
void applyOpacityMask(std::vector<float>& rgba, const OpacityMask& mask);

// Pixels the art paints (alpha above `alphaFloor`) that the field calls outside
// -- the blind spot `OpacityMask::coverage` exists to expose. `painted` receives
// the count the fraction is taken against, so a caller can report both.
std::size_t opacityMaskMissedPixels(const std::vector<float>& rgba, const OpacityMask& mask,
                                    std::size_t& painted, float alphaFloor = 0.5f);

}  // namespace rb
