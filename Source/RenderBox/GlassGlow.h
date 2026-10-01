#pragma once
// The glass's INNER GLOW: a pass only design generation 26 has.
//
// Source: `References/2.0-125/out/slices/IconRendering.arm64` (VA == file
// offset) for the reader and the draw, and the shader the draw installs,
// `metallib-iconrendering/default_mod10.ll`, function `glow`.
//
// WHERE IT IS READ
// ----------------
// `[BIN]` `ICRRenderingParameters.glow` is an Optional of three `Double`s
// (`params+0x290`: `bias`, `innerRadius`, `innerOpacity`, tag at `+0x2A8`). The
// aggregate constructor leaves it nil; `0x76FC0` writes `0.5`, `42.38` and
// `0.03` (`0x77E5C`-`0x77E64`). `RenderingParameters.h` carries the block.
//
// `[BIN]` The reader is `0x48C20`-`0x48DA8`, inside the per-group composite
// `0x48B74`: after the content pass `0x4AC84` has returned (`0x48C08`) -- so
// after the group's image and the shadow's overdraw -- and before the highlights
// (`0x48DB4`). Four gates, in order: the Optional's tag (`ctx+0x4500`, `1` is
// nil, `0x48C28`); bit 4 of the rendering steps (`0x48C4C`); the group's SDF not
// empty (`0x48C54`-`0x48C5C`); and the group's `effectsFrame` not nil
// (`0x48C60`-`0x48C68`).
//
// `[BIN]` Then a clip and one draw. The clip is `clipShape:alpha:1 mode:0` of a
// rect (`0x48D78`): the `effectsFrame` times the canvas size, offset by the
// canvas rect's origin, and OUTSET by one `pixelUnit` (`ctx+0x46A8`, negated
// into `CGRectInset` at `0x48D54`). The draw is `0xF534(sdf, sdf.maxDistance,
// -innerRadius, bias, innerOpacity x group.opacity, blend 0x1B)`:
//
//   * it returns when the alpha is not positive (`0xF570`);
//   * it builds the shader `glow` (the name is the immediate `0x776F6C67`,
//     `0xF5BC`) with five arguments:
//       0  radius      = -innerRadius x (sdfTexelsW - 2) / canvasWidth   (`0xF620`)
//       1  bias_amount = 1 / bias - 2                                    (`0xF650`)
//       2  colour      = `RBColorWhite`                                  (`0xF684`)
//       3  sdfScale    = -2 x maxDistance                                (`0xF6B0`)
//       4  sdfZero     = 0.5                                             (`0xF6D8`)
//     and the SDF's texture as the sixth;
//   * and it draws the SDF's rect with that fill, `alpha: (float)alpha
//     blendMode: 0x1B` (`0xF92C`) -- 27 is RenderBox's plus-lighter, handed
//     over raw: there is no `clampedPlusL` on this path.
//
// THE FRAGMENT, whole (`default_mod10.ll`, `@glow`, `%8`-`%39`):
//
//     d    = (sdf.r - sdfZero) x sdfScale
//     x    = d / radius
//     g    = exp(-0.5 x x^2)
//     den  = max(1 + (1 - g) x bias_amount, 2^-10)
//     fw   = clamp(fwidth(x), 2^-10, 2.0) x 0.8330078125
//     edge = saturate(x / fw + 0.5)
//     out  = colour x (edge x g / den)
//
// `sdfScale` and `radius` are both negative, so `x` is the depth INSIDE the
// shape over `innerRadius`, positive inside: the glow is brightest along the
// contour and falls off inwards as a Gaussian of sigma `innerRadius`, cut at
// the contour by the same one-pixel edge the highlights use.
//
// THE DEPTH SATURATES, AND IN GENERATION 26 THAT IS MOST OF THE PICTURE
// --------------------------------------------------------------------
// `[BIN]` `sdf.r` is not a distance, it is the distance ENCODED by RenderBox's
// distance filter, and that encoding is clamped: `filter_distance`
// (`metallib-renderbox/default_mod74.ll`, `%94`-`%99`) writes
// `saturate(signedDistance x scale + bias)`. The filter is installed with
// `zeroDistance = +maxDistance, oneDistance = -maxDistance` (RenderBox
// `0x3F354`), so the texel is 0.5 on the contour, 1 at `maxDistance` inside and
// NO MORE than 1 beyond it. Decoded with `sdfScale = -2 x maxDistance`, the
// depth the fragment sees is `clamp(depth, -maxDistance, +maxDistance)`.
//
// And `maxDistance` is the reach of the group's field (`0x1C6B8`): the largest
// `max(distance x min(w, h) / 1024, minDistancePixels)` over the group's
// highlights. Generation 26's glyph sets carry a `distance` of 12 points at
// most, against an `innerRadius` of 42.38: the deepest `x` is `12 / 42.38`, and
// `g` never falls below `exp(-0.5 x 0.0802) = 0.9607`. So in the generation
// that has it, the glow is close to a FLAT lift of `innerOpacity` over the whole
// inside of the glass, with an antialiased edge -- not a rim.
//
// WHAT IS NOT READ
// ----------------
//   * `[OBS]` `fwidth(x)`. It is the screen-space derivative of the sampled
//     field; here it is the slope of a field that advances one unit per pixel,
//     `1 / radius`, and zero where the encoding has saturated -- the convention
//     `glassHighlightFragment` already has for the same term.
//   * `[OBS]` The precision: the target computes in `half` (`exp` in `float`),
//     this in `double`.
//   * `[OBS]` Whether the fragment's output is taken as premultiplied. It is
//     `white x coverage` on all four channels, which is a premultiplied white at
//     that coverage, and it is composited as one.
//   * `[OBS]` A group whose field has NO reach -- no specular, so `0x1C6B8`
//     answers zero. `sdfScale` is then zero and the fragment as read gives half
//     the opacity over the whole clip rect, shape or no shape; whether the
//     target's SDF even exists for such a group was not read. The glow is not
//     drawn there, and `kGlowNoReachNote` says so.
//   * `[INF]` The clip's coverage on a rect whose edges fall between pixels is
//     the area of the pixel inside it. RenderBox's own rule was not read.
#include <cstddef>
#include <vector>

#include "Source/RenderBox/DistanceField.h"

namespace rb {

struct GlowParameters;

// Everything the pass needs, in TARGET PIXELS, resolved once per group.
struct GlowArguments {
    // `innerRadius x pixelsPerPoint`: the magnitude of the shader's `radius`.
    double radius = 0.0;
    // `1 / bias - 2`.
    double biasAmount = 0.0;
    // `innerOpacity x group opacity`: the `alpha:` of the draw.
    double alpha = 0.0;
    // The reach of the group's field, where the encoded depth saturates.
    double maxDistance = 0.0;
    // The two values `fw` takes: where the field still has a slope,
    // `clamp(1 / radius, 2^-10, 2) x 0.8330078125`, and where the encoding has
    // saturated and the derivative is zero, `2^-10 x 0.8330078125`.
    double edgeWidth = 0.0;
    double edgeWidthFlat = 0.0;
    // The clip, `x0, y0, x1, y1`, in pixels of the canvas grid: the group's
    // `effectsFrame` outset by one pixel.
    double clip[4] = {0.0, 0.0, 0.0, 0.0};
};

// `frame` is the group's `effectsFrame` in pixels -- `x, y, width, height`.
GlowArguments glowArguments(const GlowParameters& glow, double groupOpacity,
                            double pixelsPerPoint, double maxDistancePixels,
                            const double frame[4]);

// Whether the pass draws: a positive alpha (`0xF570`), and a field with a reach
// to decode (the header's fourth `[OBS]`).
bool glowDraws(const GlowArguments& a);

// `exp(-0.5 x^2)`, by a route both render paths can walk to the same bits: the
// argument reduced by a multiple of `ln 2`, a Taylor polynomial on what is
// left, and the power of two put back by halving. Only `+`, `-`, `x`, `/` and a
// truncation, in double -- `icon_glow.comp` is this function again, line for
// line.
double glowGaussian(double x);

// The fragment: `edge x g / den` at a depth of `depth` pixels inside the shape
// (negative outside).
double glowFragment(double depth, const GlowArguments& a);

// How much of the pixel `(x, y)` of the canvas grid the clip rect covers.
double glowClipCoverage(double x, double y, const GlowArguments& a);

// The pass over a PREMULTIPLIED target that shares the field's grid: white at
// `fragment x clip x alpha`, plus-lighter. Returns the pixels it changed.
std::size_t drawGlow(std::vector<float>& rgba, const FieldImage& field, const GlowArguments& a);

extern const char* const kGlowNote;
extern const char* const kGlowNoReachNote;

}  // namespace rb
