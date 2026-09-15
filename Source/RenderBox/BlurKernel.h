#pragma once
// WHAT `addBlurFilterWithRadius:` BUILDS FROM ITS RADIUS.
//
// Source: `Docs/Laudos/2026-09-15-desfoque.md`. Every address below is
// `References/2.0-125/out/slices/RenderBox.arm64` unless it says
// `IconRendering`; in both slices VA == file offset.
//
// THE ONE SENTENCE: `[BIN]` **THE RADIUS IS THE SIGMA.** The argument of
// `addBlurFilterWithRadius:` is the standard deviation of a true Gaussian, in
// the display list's own coordinates. Not a three-sigma support, not a box
// radius, not a Core Image `inputRadius` in disguise.
//
// This file exists because until 2026-09-15 that sentence was a CONVENTION in
// two headers -- `GlassShadow.h` chose `sigma = radius/3` and said in as many
// words that it was the one number in the file that was not measured, and
// `GlassMaterial.h` said the same about the material's own blur. It is now a
// measurement, read three independent ways.
//
// -------------------------------------------------------------------------
// READING 1, THE GPU PATH, WHICH IS THE ONE THE TARGET ACTUALLY RENDERS WITH
// -------------------------------------------------------------------------
//
// `[BIN]` The chain, entry to taps:
//
//   `-[RBDisplayList addBlurFilterWithRadius:opaque:]`  `0x3E8D4`
//     -- moves `opaque` into the flags word, loads an infinite bounds rect from
//        `[0x1972D8]`, and TAIL CALLS
//   `_RBDrawingStateAddBlurFilter`                      `0x3E69C`
//     -- `fcvt s0, d8` (`0x3E728`): the `double` radius narrows to `float` and
//        is passed UNCHANGED to
//   `RB::Filter::GaussianBlur::GaussianBlur(float, Item*, Rect*, flags)` `0xFE598`
//     -- `dup v0.2s, v0.s[0]` then `str d0, [x0]`: the scalar radius is
//        splatted into a PER-AXIS pair and stored at offset 0. No arithmetic.
//   `RB::Filter::GaussianBlur::render(...)`             `0xFEC34`
//     -- `0xFED04  fmul v0.2s, v10.2s, v10.2s` / `str d0, [x0, #0x10]`:
//        **radius SQUARED becomes the renderer's VARIANCE field.**
//   `RB::Filter::(anon)::BlurRenderer::render(...)`     `0xFF964`
//     -- `0xFFEF0`: `variance / passCount` is handed to
//   `RB::Filter::NarrowBlurKernel::get(float)`          `0xFE2E8`
//   `RB::Filter::NarrowBlurKernel::construct(float v)`  `0xFE378`
//     -- `fadd s0,s0,s0` then `fdiv d10, 1.0, d0` gives `1/(2v)`, and the loop
//        at `0xFE3CC`-`0xFE3F0` computes, for `x = 15 .. 1`,
//        `exp(-x*x / (2v))` (the `exp` stub is `0x1545FC` -> GOT `0x197A68`).
//        The centre tap is the literal `1.0` at `0xFE3FC`; the whole of it is
//        then normalised by `1/(2*sum + 1)` (`0xFE408`-`0xFE430`).
//
// So the kernel's weight at distance `x` is `exp(-x^2 / (2v))` with
// `v = radius^2`, which IS `exp(-x^2 / (2*sigma^2))` with `sigma = radius`.
//
// `[BIN]` **AND THE VARIANCE IS CONFIRMED BY A BAKED TABLE, not only by the
// code that would fill it.** `RB::(anon)::narrow_blur_15` (`0x15F9D0`) is a
// literal 15-tap kernel in `__const`. Its centre is `0.11769579` and its
// outermost tap is `0.015928393`; the ratio is `0.13533528`, and
// `exp(-7^2 / (2 * 12.25)) = exp(-2) = 0.13533528` to every digit printed. The
// `12.25` is not a guess either: `NarrowBlurKernel::get` compares its argument
// to the immediate `0x41440000` = **`12.25` = `3.5^2`** (`0xFE2E8`, and again
// at `0xFE450`) before returning that very table. Argument, table and threshold
// all agree that the number being passed around is a VARIANCE.
//
// `[BIN]` THE THREE CACHED KERNELS AND THE PASS COUNT, because they are what
// makes an unbounded sigma fit in 31 taps. `GaussianBlur::render` reads two
// quality bits (`ubfx w9, w8, #4, #2`, `0xFED34`) and picks a maximum sigma of
// `3.5` (15 taps), `5.25` (23 taps, the DEFAULT -- the `else` of the ladder) or
// `7.0` (31 taps); squares it (`0xFED54`); and then
//
//     passes = clamp(ceil(max(rx^2, ry^2) / sigmaMax^2 - 0.001), 1, 32)
//
// (`0xFED5C`-`0xFED8C`, the `-0.001` being the float at `0x1619D0`). Gaussian
// variances ADD, so `n` passes of variance `v/n` compose into variance `v`, and
// `0xFFEF0` hands `NarrowBlurKernel::get` exactly `v/n`. Wider still and the
// renderer downsamples first, subtracting the variance the downsample itself
// contributes: `0xFFA2C` does `v/16 - 0.47265625` for a 4x reduce and `0xFFB1C`
// does `v/4 - 2.56` for a 2x one -- and `0.47265625 = 0.6875^2`,
// `2.56 = 1.6^2`, which is the same variance bookkeeping stated in sigmas.
// NONE of this changes the total: it is how a big sigma is paid for, not what
// it means.
//
// -------------------------------------------------------------------------
// READING 2, THE CPU PATH, WHICH IS A TEXTBOOK GAUSSIAN AND SAYS SO
// -------------------------------------------------------------------------
//
// `[BIN]` `RB::CGContext::apply_blur(float, OptionSet<BlurFlag>)` (`0xBFDC0`)
// passes its float argument STRAIGHT to `RB::(anon)::gaussian_kernel_`
// (`0xC35F4`, called at `0xBFE38`, no instruction in between touches `v0`), and
// that function is this, line for line:
//
//     halfWidth = min(ceil(sigma * 2.8), 1024)          ; 0xC3634-0xC3654
//     w[i]      = exp(-(i*i) / (2 * sigma*sigma))       ; 0xC3684-0xC36CC
//     w[0]      = 1.0 ; mirrored ; normalised by 2*sum+1
//
// -- `fmul s0,s8,s8` then `fdiv s0, 1.0, s0` then `fmul s9, -0.5, s0` is
// literally `-1/(2*sigma^2)`. And `GaussianBlur::render(CGContext&)`
// (`0xFF0D0`) is what feeds it: `faddp s8, v0.2s` (the two axis radii) then
// `sigma = 0.5 * ctmScale * (rx + ry)`, i.e. the MEAN of the two radii scaled by
// the transform. A radius that had to be halved or tripled on its way to a
// sigma would have been halved or tripled HERE, and it is not.
//
// -------------------------------------------------------------------------
// READING 3, THE BOUNDS, WHICH IS THE SAME CONSTANT FROM A THIRD PLACE
// -------------------------------------------------------------------------
//
// `[BIN]` `RB::Filter::GaussianBlur::roi(Rect&)` (`0xFEB58`) grows the region of
// interest by `max(ceil(radius * 2.8), 0)` per axis -- the immediate is
// materialised by `mov w8, #0x3333` + `movk w8, #0x4033, lsl #16` (`0xFEBA4`),
// which is `0x40333333` = `2.7999999523162842`, the SAME bit pattern the CPU
// kernel loads from `0x15ECF0`. A filter whose bounds grow by `2.8 * r` is a
// filter whose taps die out at `2.8 * r`, and 2.8 sigmas is a Gaussian's
// working support. Under `sigma = radius/3` the target would have been
// reserving `8.4` sigmas of margin for a kernel it truncates at three.
//
// -------------------------------------------------------------------------
// WHAT THIS REPOSITORY'S OWN GAUSSIAN ALREADY DID
// -------------------------------------------------------------------------
//
// `SvgFilter.cpp`'s `gaussian` -- and the copy of it `GlassShadow.cpp` carried
// -- computes `exp(-(i*i) / (2*sigma*sigma))`, normalised, truncated at
// `ceil(3 * sigma)`. **It is the same curve.** The only difference in the whole
// of it is the truncation: `3.0` there against the measured `2.8` here. A unit
// Gaussian keeps `0.99730` of its mass inside 3 sigmas and `0.99489` inside 2.8,
// so the renormalisation moves every weight by about `0.24%` -- which is why
// this file states the 2.8 and uses it, and why nobody should expect that change
// alone to move a pixel. What moved pixels was the SIGMA.
//
// `SvgFilter.h` is not affected either way: an SVG `feGaussianBlur` reaches the
// target through `CIGaussianBlur`'s `inputRadius`, which is a Core Image
// question and a different binary. This file is about RenderBox's own filter.
#include <cstdint>
#include <vector>

namespace rb {

// `[BIN]` The identity that is the whole point of the file. Kept as a function
// rather than as a bare `1.0` constant so that a caller reads the claim and its
// address instead of reading a multiplication by one.
inline constexpr double blurSigmaForRadius(double radius) { return radius; }

// `[BIN]` `ceil(sigma * 2.8)`, capped at 1024 (`0xC3634`-`0xC3654`), floored at
// zero. This is the CPU path's number and `roi`'s number; the GPU path spends
// the same support differently, across passes of at most 15 taps each side.
int blurKernelHalfWidth(double sigma);

// `[BIN]` The taps, normalised, `2 * halfWidth + 1` of them, index `0` being the
// tap at `-halfWidth`. Empty when `sigma <= 0`, which is the binary's own
// `fcmp s0, #0.0 ; b.le` exit at `0xBFDE8`.
std::vector<double> blurKernel(double sigma);

// ---- THE QUALITY LADDER: how a big sigma is PAID FOR ----------------------
//
// A sigma of 64 -- which is what `s * 64 * clamp(radius,0,1)` reaches on a
// 1024-pixel canvas -- is `ceil(64 * 2.8) = 180` taps each side, 361 per axis.
// **The target never does that**, and the reading above already said why
// without anybody transcribing it: `GaussianBlur::render` picks a sigma CEILING
// from the quality bits, splits the variance across passes that each stay under
// it, and when even 32 passes would not be enough it RENDERS SMALLER first.
// This front transcribed that machinery. The two things it adds to the reading
// in `Docs/Laudos/2026-09-15-desfoque.md` are the THRESHOLD that selects a
// reduce and the DEFAULT-QUALITY variance it subtracts; both are `[BIN]` and
// both have addresses.
//
// `[BIN]` **The raw pass count is the thing the ladder is keyed on.** At
// `0xFED5C`-`0xFED74`:
//
//     nRaw = ceil(max(rx^2, ry^2) / sigmaMax^2 - 0.001)          ; fcvtps
//
// and `0xFED78`-`0xFED8C` stores `clamp(nRaw, 1, 32)` into the renderer's
// `+0x1c`. But the RESOLUTION decision at `0xFEDF0` and `0xFEE24` tests the
// UNCLAMPED `w25`, against 7 and against 3:
//
//     nRaw >= 7  ->  render at (d + 3) >> 2   , i.e. a 4x reduce   ; 0xFEE04-0xFEE08
//     nRaw >= 3  ->  render at (d + 1) >> 1   , i.e. a 2x reduce   ; 0xFEE3C-0xFEE50
//     else       ->  render at full size
//
// (For the top quality rung, `w24 == 0x30`, both branches reduce X only; that is
// the `fcsel` at `0xFEE1C` / `0xFEE5C` and it is not the default rung.)
//
// `[BIN]` **And the reduce pays its own way.** `BlurRenderer::render` (`0xFF964`)
// reads that same `+0x1c`, takes the matching branch, and rewrites the variance
// at `+0x10` before any kernel is built:
//
//     4x, default quality  : v/16 - 0.47265625   ; 0xFFA18-0xFFA30, 0.6875^2
//     2x, default quality  : v/4  - 0.765625     ; 0xFFB78-0xFFB8C, 0.875^2
//     2x, quality rung 3   : v/4  - 2.56         ; 0xFFB08-0xFFB20, 1.6^2
//
// The `-0.47265625` and `-0.765625` are materialised by `mov w8, #imm` --
// `#-0x410e0000` is `0xBEF20000` at `0xFFA24` and `#-0x40bc0000` is `0xBF440000`
// at `0xFFB80` -- so neither is in the constant pool, which is the lesson this
// repository paid for four times on 2026-09-15 and the reason they were found.
// The laudo quoted the `2.56`; that one is the `[x20+9]` branch, and `[x20+9]` is
// written at `0xFECF8`-`0xFED00` as `(flags & 0x30) == 0x30`, i.e. the TOP rung.
// **The default rung subtracts `0.875^2`, not `1.6^2`.**
//
// Variances add, so subtracting what the resampling itself contributes is what
// keeps the total honest: the shrunken image already carries `0.6875` (resp.
// `0.875`) sigmas of blur in its own grid, and the kernel is asked for the rest.
//
// `[INF]` **What is NOT read is whether the ladder RECURSES.** `GaussianBlur::
// render` sizes the multipass target once; `BlurRenderer::render` reduces again
// on its own and writes the variance back; and who recomputes `+0x1c` in between
// was not followed. This file takes the reading under which both of its branches
// are reachable and the pass count stays bounded: **reduce, recompute `nRaw` at
// the smaller size, and reduce again while it is still 3 or more.** It is also
// the simplest rule that preserves the total variance exactly, which is the test
// that matters -- every level subtracts precisely what it adds. Under it a
// 1024-pixel canvas with `sigma = 64` reduces twice (4x, then 4x) and finishes
// with ONE pass of `sigma = 3.94` on a 64x64 grid, instead of 361 taps per axis
// on a 1024x1024 one.
//
// `[INF]` Two smaller choices, named rather than implied. The reduce is a BOX
// average of `factor x factor` source texels and the expand is BILINEAR; the
// target's own resampling filters were not read, and these two contribute about
// `0.25^2` and `0.29^2` of variance rather than the `0.875^2` / `0.6875^2` being
// subtracted, so the result is very slightly NARROWER than the transcription
// asks for -- `0.09%` of the total variance at the 4x rung, `3.9%` at the 2x one.
// The measured cost of that is in `Docs/Laudos/2026-09-15-desfoque-escada.md`.
// And a reduce is refused when it would take either side below 8 texels, so a
// thumbnail does not get blurred on a 2x2 grid.
//
// `nRaw` for a variance, unclamped -- the number the ladder branches on.
int blurPassCountForVariance(double variance);

// `4`, `2` or `1`: what `nRaw` asks the renderer to shrink by, before the size
// floor is applied.
int blurReduceFactorForVariance(double variance);

// A separable blur of straight RGBA, run over PREMULTIPLIED values with CLAMPED
// edges, returning straight RGBA.
//
// The two conventions are not free choices and `GlassShadow.h` already argued
// them: averaging straight colour drags the colour of fully transparent pixels
// into visible ones, and an edge that read zero from outside would fade a shadow
// that runs off the canvas. They are restated here because this is now the one
// place that owns them.
//
// It runs the LADDER above rather than one wide kernel, so its cost is bounded
// by the canvas and not by the radius: `sigma = 64` on a 1024-pixel canvas costs
// one 25-tap pass on a 64x64 grid, not 361 taps per axis on the full one.
//
// `sigma <= 0`, a degenerate size, or a short buffer all return `src` unchanged.
std::vector<float> blurPremultipliedRgba(const std::vector<float>& src, std::uint32_t width,
                                         std::uint32_t height, double sigma);

// ---- the group's `blur-material`, which this renderer still does not draw ---
//
// `[ART]` The corpus carries 123 `blur-material` keys -- **123 GROUPS over 73
// documents**, not 123 documents, and the difference matters to anyone sizing
// the prize. 75 of the 123 are numbers and 48 are an explicit `null`
// (`Docs/01-o-formato-icon.md`, re-counted for this front); all 75 numbers are
// POSITIVE, spread from `0.05` to `1.0`, which is `3.2` to `64` points of
// radius, and they sit in 46 documents. The inspector edits the key, and
// `GlassMaterial.cpp` already turns it into `DenormalisedGlass::
// blurRadiusPoints` through `denormaliseBlurRadius`. `[BIN]` The arithmetic is
// `radius = min(b, 1.0) * blurStrengthMax` with `blurStrengthMax = 64.0`
// (`IconRendering` `0x4A948` and `0x4A598`), and the destination is
// `addBlurFilterWithRadius:opaque:` -- so as of this file the RADIUS, the
// DESTINATION and the KERNEL are all read.
//
// WHAT IS STILL NOT READ IS THE SURFACE, and it is one question rather than the
// two it used to be. `[BIN]` Both call sites wrap the filter around a LAYER, and
// the layer flag is the thing that decides what gets blurred:
//
//   * `IconRendering` `0x4A5B4`: `clipShape:alpha:mode:` (`0x4A580`), then
//     `addBlurFilterWithRadius:opaque:`, then `beginLayerWithFlags:` with
//     **flag 1**, then an IMMEDIATE `drawLayerWithAlpha:blendMode:` with nothing
//     drawn inside. Flag 1 is named by RenderBox's own XML serialiser:
//     `RB::XML::DisplayList::begin_layer` (`0xE9E78`) tests bit 0 and emits the
//     attribute **`needs-background`** (`0xE9F48`). A layer that needs the
//     background, contains nothing, and carries a blur IS a backdrop blur.
//     (It cannot be RenderBox's other backdrop mechanism:
//     `GenericFilter<GaussianBlur>::make_backdrop_item` (`0x1CBF4`) is
//     `mov x0, #0 ; ret`, so a Gaussian has no `BackdropFilterItem` the way
//     `LuminanceCurve` (`0x44DA4`) and `ColorClamp` (`0x454C4`) do.)
//
//   * `IconRendering` `0x4A960`: the same radius, but `beginLayerWithFlags:`
//     with **flag 0x80**, which the same serialiser calls
//     `ignored-by-needs-background` (`0xE9F08`), around a body of `save` /
//     `drawLayerWithAlpha:` / `restore`.
//
// `[OBS]` Three things block the pixel. (1) WHICH of the two branches runs for a
// document is not read. (2) The clip is a RECT built at `0x4A4A4`-`0x4A56C` from
// the layer's own frame, mapped by the renderer's canvas offset and scale
// (`self+0x558..0x570`) and then inset by `-[self+0x46A8]` -- a field whose
// value was not read, around a frame this renderer does not model. (3) A
// backdrop blur needs the composite BENEATH the group, and where in this
// renderer's group loop that composite is complete was not read either.
//
// Drawing it anyway would mean inventing the extent of a blur over 123
// documents, which is exactly the plausible-and-wrong this tower refuses. The
// note below says so, with the radius the document asked for, so a reader sees a
// stated gap instead of a silent one.
extern const char* const kBlurMaterialSurfaceNote;

}  // namespace rb
