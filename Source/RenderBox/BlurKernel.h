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

// O PLANO DA ESCADA, sem pixel nenhum: as decisoes que `blurLadder` toma para
// `variance` sobre `width x height` -- cada reducao (fator e tamanho reduzido),
// e no fundo o numero de passadas e o sigma de cada uma. O caminho residente
// (`GpuGlass.cpp`) executa a mesma sequencia na GPU: reduz na ordem de
// `levels`, roda `passes` passadas separaveis de `sigmaPerPass` no ultimo nivel
// e expande de volta na ordem inversa. `levels[0]` e o tamanho pedido
// (`factor == 1`). `passes == 0` e "nada a fazer".
struct BlurLadderLevel {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int factor = 1;   // quanto ESTE nivel encolheu o anterior
};
struct BlurLadderPlan {
    std::vector<BlurLadderLevel> levels;
    int passes = 0;
    double sigmaPerPass = 0.0;
};
BlurLadderPlan blurLadderPlan(std::uint32_t width, std::uint32_t height, double variance);

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

// The same blur, over `img` itself: for a caller that no longer needs the
// input, which saves the copy. When it blurs, `img` comes back holding exactly
// `width x height` texels, as the returned vector above would.
void blurPremultipliedRgbaInPlace(std::vector<float>& img, std::uint32_t width,
                                  std::uint32_t height, double sigma);

// ---- the group's `blur-material`: the SURFACE ------------------------------
//
// `[ART]` The corpus carries 123 `blur-material` keys -- **123 GROUPS over 73
// documents**, not 123 documents. 75 of the 123 are numbers and 48 are an
// explicit `null`; all 75 numbers are POSITIVE, from `0.05` to `1.0`, which is
// `3.2` to `64` canvas units of radius, and they sit in 46 documents. (Counted
// a third time for this front, straight off `IC_CORPUS_DIR`; the three counts
// agree.) `GlassMaterial.cpp` already turns the key into `DenormalisedGlass::
// blurRadiusPoints` through `denormaliseBlurRadius`. `[BIN]` The arithmetic is
// `radius = min(b, 1.0) * blurStrengthMax` with `blurStrengthMax = 64.0`
// (`IconRendering` `0x4A948` and `0x4A598`), and the destination is
// `addBlurFilterWithRadius:opaque:`.
//
// Until 2026-09-15 the RADIUS, the DESTINATION and the KERNEL were read and the
// SURFACE was three open questions. Two of the three are now closed by reading
// and the third was ours to answer. What follows is the call site, transcribed,
// because everything below is arithmetic on it.
//
// THE FUNCTION. `[BIN]` `IconRendering` `0x4A2D4`-`0x4AC84`, one function, both
// blur sites inside it. It is called from five places (`0x43A84`, `0x44028`,
// `0x45DDC`, `0x48BD4`, `0x48F30`) with `x0` = one `0xC0`-byte group descriptor
// copied out of the Swift array behind `ctx+0x400` (`0x4397C`-`0x4398C`; the
// element stride is the literal `add x23, x23, #0xc0` at `0x439C8`) and `x20` =
// the renderer context. The descriptor's first `0x38` bytes are the eight-field
// glass material of doc 03 §29.2, so `+0x18` is `blurStrength`, `+0x20`
// `refractionHeight` and `+0x28` `refractionStrength` -- the same three offsets
// §29.7 lists, and `0x4A598` proves `+0x18` by using it as the blur's `b`.
//
// -------------------------------------------------------------------------
// WALL 1, WHICH BRANCH RUNS: `refractionStrength`, and it is ALWAYS the first
// -------------------------------------------------------------------------
//
// `[BIN]` The function loads `d13 = [x0+0x18]` (blur) and `d1 = [x0+0x28]`
// (refraction strength) at `0x4A30C`-`0x4A310` and branches on those two and
// nothing else:
//
//     0x4A404  fcmp d13, #0.0 ; b.le 0x4A5DC      ; blur <= 0 ?
//     0x4A40C  fcmp d1,  #0.0 ; b.ne 0x4A82C      ; refraction != 0 ?
//
//   blur > 0, refraction == 0  -> `0x4A418`: the group's content is drawn
//        (`bl 0x49ED4` at `0x4A488`), then `save`, `clipShape:alpha:mode:`
//        (`0x4A580`), `addBlurFilterWithRadius:opaque:` (`0x4A5B4`),
//        `beginLayerWithFlags:` **1** (`0x4A5C0`), an IMMEDIATE
//        `drawLayerWithAlpha:1.0 blendMode:0` (`0x4A5D0`), `restore`. Nothing
//        is drawn inside the layer.
//   blur > 0, refraction != 0  -> `0x4A82C`: the same rect and clip, the same
//        radius at `0x4A960`, but `beginLayerWithFlags:` **0x80** wrapped round
//        the refraction body.
//   blur == 0, refraction != 0 -> `0x4A5DC`: no blur; flag 1 round the body.
//   both zero                  -> `0x4A34C`: plain draw, no layer at all.
//
// Flag 1 is bit 0, which RenderBox's own XML serialiser calls
// **`needs-background`** (`RB::XML::DisplayList::begin_layer` `0xE9E78`, the
// attribute emitted at `0xE9F48`), and which
// `Docs/Laudos/2026-09-15-realce-vcm-fechado.md` read a second, independent
// way: `Builder::null_style_draw` (`0xCDD80`, `tbz` at `0xCE02C`) allocates a
// `BackdropFilterItem` on the PARENT layer for it. A layer that needs the
// background, contains nothing and carries a blur is a backdrop blur, by two
// readings that share no path. `0x80` is `ignored-by-needs-background`
// (`0xE9F08`).
//
// `[ART]` **And `refractionStrength` is zero for every document there is.** It
// is not a `.icon` key: the eight-field material's default constant (`0x93B30`,
// doc 03 §29.2) sets `refractionStrength = 0.0`, and a grep for `refraction`
// over all 145 corpus documents returns ZERO files -- so does the 27.0-129
// gabarito, and so does the user's `GoWToolkit.icon`. Every document this
// renderer will be handed takes the FIRST branch. Wall 1 is down, and the answer
// is the pure backdrop blur.
//
// `[BIN]` One consequence of the ORDER, and it is the shape of the whole effect:
// the content is drawn BEFORE the layer (`0x4A488` precedes `0x4A48C`), so the
// background the layer needs INCLUDES the group's own art. The group is blurred
// together with everything beneath it, not merely over it.
//
// -------------------------------------------------------------------------
// WALL 2, THE CLIP: six CGRect calls, and `[ctx+0x46A8]` is exactly ONE PIXEL
// -------------------------------------------------------------------------
//
// `[BIN]` `0x4A4A4`-`0x4A56C`, with every stub resolved through the INDIRECT
// SYMBOL TABLE and not by guessing a neighbour (the lesson that nearly cost this
// project a false fix on 2026-09-15): `0x8DA10` `_CGRectGetMinX`, `0x8DA1C`
// `_CGRectGetMinY`, `0x8DA28` `_CGRectGetWidth`, `0x8D9D4` `_CGRectGetHeight`,
// `0x8DA58` `_CGRectOffset`, `0x8DA34` `_CGRectInset`. Six doubles into each of
// the last two is what makes `CGRectOffset(r,dx,dy)` and `CGRectInset(r,dx,dy)`
// line up with the argument registers. Transcribed:
//
//     frame  = CGRect at descriptor +0x70 .. +0x88
//     canvas = CGRect at ctx       +0x558 .. +0x570
//     r = CGRectMake(MinX(frame)*Width(canvas),  MinY(frame)*Height(canvas),
//                    Width(frame)*Width(canvas), Height(frame)*Height(canvas))
//     r = CGRectOffset(r, MinX(canvas), MinY(canvas))
//     r = CGRectInset (r, -[ctx+0x46A8], -[ctx+0x46A8])      ; 0x4A558-0x4A564
//     [shape setRect:r] ; [list clipShape:shape alpha:1 mode:0]
//
// A frame multiplied by the canvas's size and offset by its origin is a frame in
// UNIT coordinates. That is what the multiplication means, and it is why the
// canvas rect is in the expression at all.
//
// `[BIN]` **The canvas rect is `(0, 0, 1024, 1024)` for a square icon**, built
// at `0x4291C`-`0x42968` inside the same context constructor the highlight front
// read: the origin is `movi v0.2d, #0` stored to `ctx+0x558`
// (`0x4295C`-`0x42960`) and the size is
//
//     canvasW = 1024 * (w/s) / min(w/s, h/s)   ;   canvasH likewise
//
// with `1024.0` materialised as the immediate `0x4090000000000000` at `0x42940`
// -- the `s` cancels, so it is `1024 * w / min(w, h)`.
//
// `[BIN]` **`[ctx+0x46A8]` is the SAME field the highlight front read**, and so
// this is the same `self`. `Docs/Laudos/2026-09-15-realce-forma.md` §4.1 read
// `0x42D3C`-`0x42D50` as `ctx+0x46A0 = 1/escala` and
// `ctx+0x46A8 = (1/escala)/contentsScale`, with
// `escala = min(rectW/canvasW, rectH/canvasH)`. Three checks that it is one
// object, none of them the fit itself: the constructor takes the address of its
// own frame into **`x20`** at `0x42F40` (`add x20, sp, #0x480`) and writes
// `sp+0x9A0`, `sp+0x9E8` and `sp+0x9F0` -- which are `ctx+0x520`, `+0x568` and
// `+0x570`, exactly the display list and the canvas size THIS call site loads
// off `x20`; a scan of every unsigned-offset load or store of `#0x46A8` in
// `__text` finds eleven, ten on that same register convention and one
// (`0x4EC38`-`0x4EC3C`) a field-for-field copy between two of the same type; and
// `0x4BEAC` is the highlight pass's own use, four instructions from the
// `0x4BF00` that laudo names.
//
// So `[ctx+0x46A8]` is canvas units per PIXEL, and `CGRectInset` by its NEGATIVE
// is an OUTSET of **exactly one device pixel** on every side. It is a bleed
// guard, not a crop. Wall 2 is down: the clip is arithmetic, and in this
// renderer's units it is `frame x size`, grown by one pixel.
//
// `[BIN]` **THE FRAME HAS A NAME NOW, AND IT IS OPTIONAL: descriptor `+0x70` is
// `IconRendering.FinalizedIcon.Layer.effectsFrame`, a `CGRect?`.** Read from the
// Swift reflection metadata that `IconRendering` still carries, three sections
// that agree:
//
//   * `__swift5_fieldmd` `0xA306C` is the field descriptor of the type whose
//     mangled name resolves to `IconRendering.FinalizedIcon.Layer`
//     (`__constg_swiftt` `0x9EF80`). Its nine fields are `material`,
//     `blendMode`, `opacity`, `knocksOutBorder`, `image`, `contentFrame`,
//     `effectsFrame`, `sdf`, `shadowImage`.
//   * the struct METADATA is at `0xBD3F0` (kind word `0x200`, descriptor
//     pointer at `0xBD3F8`), and its field-offset vector at `0xBD400` is
//     `0x00, 0x31, 0x38, 0x40, 0x48, 0x50, 0x70, 0x98, 0xB0`. So
//     `contentFrame` is at `+0x50` and **`effectsFrame` is at `+0x70`**, and
//     the record is `0xC0` bytes -- the very stride of `add x23, x23, #0xc0`.
//   * the typerefs confirm the types: `contentFrame` (`0x9C5CA`) resolves to
//     `__C.CGRect` and `sdf` (`0x9CAA2`) to `IconRendering.SDF`.
//
// Three independent controls say the offsets are right and not an encouraging
// coincidence. `sdf` at `+0x98` with its final `Double` at `+0xA8` is exactly
// what the glass-displacement front read from the CODE (`0x4A324`/`0x4A3F0`
// load `d14 = [x0,#0xA8]`); `shadowImage` at `+0xB0` is exactly the `ldur q0,
// [x0, #0xb0]` of `0x4A3AC`; and an `Optional<CGRect>` is 32 bytes of payload
// plus a tag byte, which is precisely why the prologue reads the rect as
// `+0x70`/`+0x78`/`+0x80`/`+0x88` (`0x4A314`-`0x4A318`) and then a lone BYTE at
// `+0x90` (`ldrb w25, [x0, #0x90]`, `0x4A31C`).
//
// `[BIN]` **AND THAT TAG BYTE IS THE GATE ON THE WHOLE EFFECT.** `0x4A334`-
// `0x4A348`:
//
//     ldr  x21, [x20, #0x28]        ; renderer flags
//     tbz  w21, #9,   0x4A34C       ; flag clear -> plain draw
//     and  x8,  x23, #0xff          ; x23 = [x0+0xA0], inside sdf
//     cmp  x8,  #0xff
//     ccmp w25, #1, #4, ne          ; w25 = the effectsFrame tag
//     b.ne 0x4A3F0                  ; effects ONLY if tag != 1
//
// and `0x4A3F0` is where the four doubles of the rect move into `d12/d11/d10/d9`
// -- the very registers the clip arithmetic of `0x4A4A4`-`0x4A504` feeds to
// `CGRectGetMinX/MinY/Width/Height`. The `blurStrength`/`refractionStrength`
// ramification at `0x4A404`/`0x4A40C` sits BELOW that branch. So the entire
// blur-material block is reachable only when `effectsFrame != nil`; when it is
// nil the function falls through to `0x4A3AC`, draws the group's content through
// `0x49ED4` and returns **without any layer at all**.
//
// `[OBS]` **Who COMPUTES `effectsFrame` is still not read**, and it is not a
// document key: `[ART]` a scan of the 146 corpus bundles finds zero
// `effects-frame`/`effectsFrame` keys and exactly one `"frame"` anywhere. It is
// produced by the finaliser, in Swift, with no symbol. So this renderer still
// cannot say what rect to clip to -- but it now knows that the honest default is
// NOT "the whole canvas", it is "do not draw", because that is what the target
// does for a nil frame.
//
// `blurMaterialSurface` takes the frame as an ARGUMENT for exactly that reason:
// the caller states which reading it is using, in one visible place, instead of
// the choice being baked into the blur.
//
// AND THE GABARITO REFUSED THE ONLY READING THIS FRONT HAD FOR IT. With the
// frame at the unit rect -- the whole canvas -- `AppIcon-27` was rendered at
// `--size 412`, placed at the legacy inset in the 512 frame and compared with
// `References/27.0-129/out/apple-512.png`:
//
//     mean channel error   R      G      B      A
//     blur NOT drawn      8.95  10.03   9.89   4.95
//     blur, unit frame   15.58  20.09  22.49   7.09
//
// and the four corners of the squircle are the worst 8x8 blocks in the frame. A
// second diagnostic -- compositing only the colour and keeping the destination's
// alpha, so the blur cannot move the silhouette -- lands at `14.35 / 18.90 /
// 21.33`, still far above the control, so it is not the alpha alone. The luma
// profile says it in the shape `Docs/Laudos/2026-09-15-realce-forma.md` taught:
// down the centre column of the 512 frame the gabarito falls `157 -> 49` over
// nine rows and then holds a FLAT `49`; the blur-off render falls `202 -> 49`
// over fifteen and holds the same `49`; the blurred render is flat at `84` and
// never reaches `49` anywhere. A canvas-wide backdrop blur at
// `sigma = 0.56 * 64 = 35.84` canvas units erases structure the target keeps.
//
// So the frame is NOT the unit rect, and this front does not know what it is.
// `blurMaterialSurface` and `drawBlurMaterial` below are the transcription and
// they are SWITCHED OFF at the one call site -- the same thing `BlendFormula.h`
// does with `shouldClampPlusLBlending`, and for the same reason: turning it on
// without the reading would move pixels on the measurer's authority instead of
// the target's.
//
// THE `opaque:` ARGUMENT AND BIT 20, WHICH IS NOW CLOSED -- and it is an ALPHA
// hint, not a region one. `opaque:` is `mov w2, #1` at BOTH sites (`0x4A5B0`,
// `0x4A95C`). It enters the filter flags at `0x3E8D4`, sits in bit 0 of
// `GaussianBlur+0x18` (`0xFE5C0`), and from there it forks TWICE, not once.
//
// `[BIN]` FORK A, the render state. It is copied to `renderer+8` at
// `0xFECE8`-`0xFECEC`; `BlurRenderer::render` (`0xFF964`) reads it at `0xFFF98`
// and, if it is `1` AND this is the LAST pass (`[x20+0x1c]` just decremented to
// zero, `0xFFF8C`-`0xFFFB0`), sets `w23 = 0x10`, which `0xFFFF8`-`0x100004`
// folds into the packed state as bit 20.
//
// The state is NOT an opaque cookie -- `FormattedRenderState::description()`
// (`0x13365C`) names every field of it, and the names decode the word:
// `function` = bits 0..5, `coverage_state` = bits 6..15 (`0x1337DC`),
// **`fill_state` = bits 16..31** (`0x13380C`). So bit 20 is bit 4 of
// `fill_state`. And `RenderState::name()` (`0x1328B0`) indexes a 39-entry table
// at `0x1900D8` with bits 0..5: the constant `w9 = 0x03C0001E` that `0x100004`
// ORs in makes the function id `0x1E` = **`filter_blur`**.
//
// `[BIN]` `fill_state` IS PER-FUNCTION -- every accessor special-cases on the
// function id -- so the question is what bit 4 means FOR `filter_blur`, and the
// answer is: nothing that the CPU ever asks. Every accessor, audited:
// `dest_write_mask` (`0x132DA8`) takes `ubfx #0x16, #4`, i.e. bits 22..25;
// `reads_destination` (`0x132AD0`) and `reads_coverage` (`0x132B80`) both test
// `fill_state & 0xF == 9`; `reads_noise` (`0x132BBC`) dispatches through the
// byte table at `0x16266C`, where id `0x1E` takes case 15 -> `0x132C24` = bit
// **21**; `reads_tables` (`0x132C3C`, table `0x162692`) takes case 27 ->
// `0x132CD4`, a constant zero; `uses_shader_blending` (`0x132930`) returns 0 for
// `0x1E` before looking at any bit; `other_dest_write_mask` (`0x132DC4`) only
// fires for id `0x23`. And a sweep of the whole `__text` for a bit-20 test finds
// no `tbz/tbnz #20` at all, one `orr #0x100000` (`0x26280`, a writer), and one
// `ubfx #20, #1` -- `0x132C04`, which is `reads_noise` case 7, i.e.
// `filter_color`/`filter_custom`, not us.
//
// `[BIN]` Where bit 20 DOES go is the GPU. `0x1000AC` ORs the state into `x1`
// for `RenderPass::draw_indexed_primitives(RenderState, ...)` (`0x11A8AC`), the
// state is the pipeline-cache key, and `Device::make_render_pipeline_descriptor`
// (`0xD8560`) hands the whole thing to the shader verbatim: `0xD86E8`-`0xD870C`
// packs the 64-bit state plus the derived word into 16 bytes and calls
// `setConstantValue:type:atIndex:` with type `0x24` = `MTLDataTypeUInt4`, index
// 0. So bit 20 selects a compiled variant of the `filter_blur` Metal fragment
// shader, whose source is in no slice we have.
//
// `[BIN]` FORK B is the one that is legible, and it decides the question. The
// same flag is passed as the trailing `OptionSet<Filter::Flag>` of
// `RenderGroup::add_multipass_renderer` (`0xFEE60`: `and w8, w8, #1`; call at
// `0xFEE88`), stored at `MultipassInfo+0x85` (`0x105EE0`), and read by
// `RenderGroup::resolve_unary_subgroup` (`0x1073D4`) in exactly two shapes,
// both about ALPHA:
//
//   `0x107970`/`0x107A5C`  `tbnz w9, #0` -- if opaque, SKIP
//                          `RenderPass::resolve_srgb_alpha()` (`0x11AD68`);
//   `0x107564`/`0x107AAC`  `tst w8,#1 ; cset w3, eq` -- pass `!opaque` as the
//                          trailing bool of `RenderPass::color_convert(
//                          ColorSpace, ColorSpace, bool)` (`0x11ABE4`).
//
// `[BIN]` **And it touches no geometry anywhere.** `GaussianBlur::adjust_roi`
// (`0xFEB04`), `roi`/`dod` (`0xFEB58`) and `layer_scale` (`0xFE6E4`) read only
// the sigma pair at `+0x00`, the quality bits `+0x18[4:5]` and `+0x20`; none of
// them reads bit 0 of `+0x18`. So `opaque:1` does NOT restrict the sampled
// region, does not let the compositor skip the backdrop, and cannot be what
// makes a backdrop blur coexist with a sharp silhouette. It says "this content
// has no alpha worth resolving", and that is all it says.
//
// -------------------------------------------------------------------------
// WALL 3, WHERE THE BACKDROP IS COMPLETE -- ours to answer, and answered
// -------------------------------------------------------------------------
//
// This one was never a reading of the target; it is a question about OUR group
// loop, and the loop already had the answer. `IconRenderer.cpp` composites
// groups back-to-front into a single premultiplied accumulator `acc`, so at the
// END of a group's layer loop `acc` holds precisely "everything beneath this
// group, plus this group" -- which, by the order read above, is precisely the
// background the target's layer needs. The call goes there.
//
// The one case where that does NOT hold is a group with a non-normal
// `blend-mode`: it draws into a target of its own and the accumulator is not
// underneath it. Blurring an empty group target would be the silent-wrong this
// file refuses, so that case is REFUSED by name -- the same shape of refusal
// `groupWouldRefract` already uses, and for the same reason. It is a refusal
// that would survive the frame being read, which is why it is written down now
// rather than when the rest of it turns on.
//
// -------------------------------------------------------------------------

// The surface of one group's `blur-material`, in PIXELS.
//
// `frame*` is the group's frame in UNIT coordinates -- the `[OBS]` above. The
// canvas is `1024 * w / min(w,h)` by `1024 * h / min(w,h)` with its origin at
// zero, so a square icon maps one canvas unit to `size/1024` pixels and the
// outset of `[ctx+0x46A8]` canvas units becomes one pixel flat.
//
// `blurRadiusCanvasUnits` is `DenormalisedGlass::blurRadiusPoints`, which is
// `min(b,1) * 64` and lives in the display list's own coordinates -- the same
// 1024-canvas units the clip rect is built in. `sigmaPixels` is therefore
// `radius * min(w,h) / 1024`, and it is a SIGMA because the radius is the sigma.
// `[BIN]` The `1024.0` of `canvasW = 1024 * w / min(w,h)`, materialised as the
// immediate `0x4090000000000000` at `IconRendering` `0x42940` -- not in the
// constant pool, which is why it is quoted as an immediate.
inline constexpr double kBlurMaterialCanvasUnits = 1024.0;

struct BlurMaterialSurface {
    bool draws = false;
    double sigmaPixels = 0.0;
    // Half-open, already clamped to the canvas.
    std::uint32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

BlurMaterialSurface blurMaterialSurface(double blurRadiusCanvasUnits, double frameX,
                                        double frameY, double frameWidth, double frameHeight,
                                        std::uint32_t width, std::uint32_t height);

// The target's `beginLayerWithFlags:1` + `drawLayerWithAlpha:1.0 blendMode:0`
// on a PREMULTIPLIED accumulator: blur what is already there and composite the
// blurred copy back over it, source-over, inside the clip. Returns how many
// texels the composite actually moved, so a caller can tell "drawn" from "drawn
// and changed nothing".
std::size_t drawBlurMaterial(std::vector<float>& acc, std::uint32_t width, std::uint32_t height,
                             const BlurMaterialSurface& surface);

// Said once per document that asks, because the frame above is still `[OBS]`.
extern const char* const kBlurMaterialFrameNote;

// A group carrying both a non-normal `blend-mode` and a positive
// `blur-material`: the backdrop this renderer would hand the blur is the group's
// own empty target and not the canvas, so the blur is refused by name.
extern const char* const kBlurMaterialBlendedGroupNote;

}  // namespace rb
