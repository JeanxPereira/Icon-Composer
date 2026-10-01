#pragma once
// A document colour, brought into the space the compositor works in.
//
// WHAT THIS CLOSES
// ----------------
// `2026-09-01-gradiente.md` left "display-p3 -> sRGB: the matrix was never
// measured from the target" as a reported gap, `IconRenderer` carried it as
// `kBackgroundP3Note`, and `SvgRenderer` as `unconvertedP3`. The matrix is not
// measured. It is READ: RenderBox carries it as nine `float`s, and the transfer
// function around it as ten more.
//
// HOW A COLOUR TRAVELS IN THE TARGET
// ----------------------------------
// `[BIN]` Document colour -> `CGColor` in the space the document names
// (`IconComposerFoundation 0x9E978`, one `CGColorCreate*` per case) ->
// `IconRendering.IconColor.init(_: CGColor)` (`IconRendering 0x3D770`), which
// is one call: `RBColorFromCGColor2(color, 3)`. `[BIN]` `RBColorSpace 3` is
// Display P3 -- `rb_color_space` (`RenderBox 0x8B704`, byte table `0x16CC89`)
// maps it to `0x12`, which `RB::ColorSpace::name` (`0x77718`) prints as
// "DisplayP3". So an `IconColor` is four bare doubles holding GAMMA-ENCODED
// DISPLAY P3, whatever the document said, and that answers the `[OBS]` that
// `AutomaticGradient.h` and `SystemFill.h` both leave open. Every fill then
// reaches `RBFill` tagged with the same literal 3 (`0x1B168`, `0x1BC6C`).
//
// `[BIN]` `RBColorFromComponents3` (`RenderBox 0xC84B0`) recognises the source
// space by name (`color_space_from_cg_name`, `0x779BC`): sRGB and extended sRGB
// are `0x11`, Display P3 is `0x12`, and GenericGrayGamma2_2 and ExtendedGray
// are BOTH `0x10` -- grey primaries with transfer nibble 1, the SAME nibble
// sRGB carries. A grey is therefore replicated into r = g = b and never
// re-encoded from gamma 2.2. `[OBS]` Apple's own bitmap agrees: the background
// ramp `gray 0.192 -> 0.078` of the Icon Composer icon lands on 49 -> 20, and a
// 2.2 -> sRGB re-encode would have landed on 45 -> 12.
//
// THE CONVERSION ITSELF
// ---------------------
// `[BIN]` `RB::ColorSpace::Conversion::operator()(float3)`, `RenderBox
// 0x76C34`: decode with the source transfer (`0x76CB4`), multiply by the
// primaries matrix (`0x76DA8`, built by `RB::ColorSpace::Matrix`, `0x76040`),
// encode with the destination transfer (`0x76E10`). Three things about it that
// a textbook version would have got differently:
//
//   - NO CLAMP, anywhere. A Display P3 colour outside the sRGB gamut comes out
//     with a negative or above-one component and keeps it.
//   - BOTH TRANSFER FUNCTIONS ARE ODD. They run on `|x|` and the sign is put
//     back at the end (`fabs` ... `fcmlt #0.0` ... `bsl`), which is what lets a
//     negative component survive a round trip.
//   - THE MATRIX IS NOT THE D65 ONE. Its third column is not zero
//     (-2.7e-05, 2.1e-05): these are the numbers of a profile-derived matrix,
//     stored as `float`. They are transcribed by their bit patterns below and
//     NOT replaced by the prettier chromaticity-derived ones, which differ in
//     the fifth decimal.
//
// `[INF]` THE POWER. The target computes `x^e` as `exp2(log2(x) * e)` through
// `_simd_log2_f4` / `_simd_exp2_f4`. This does the same two steps in `float`
// with the C library's `log2f` / `exp2f`; the two libraries are not the same
// code, so the last ulp of a converted component is this machine's and not the
// target's. That is far below one 8-bit level and is not claimed.
//
// WHICH SPACE IS "WORKING" -- READING A, AND THE READING IT IS NOT
// -----------------------------------------------------------------
// The compositor here works in gamma-encoded sRGB with components allowed
// outside [0, 1]. That is READING A, and it is one of two the binary supports:
//
//   A. `[BIN]` The CGImage entry points (`IconRendering 0x19A3C`, `0x19B58`,
//      the exporter `0x22E48`) all call the helper at `0x13590` with
//      `RBImageRendererColorMode = 14` and `RBImageRendererColorSpace = 1`.
//      `RB::ColorMode` (`RenderBox 0x27738`) starts from `0x00111101` --
//      working `0x11`, target `0x11` -- and only the modes in the mask
//      `0x2A5C6` are switched to linear; 14 is not in it. The target is
//      `RGBA16Float` and the image is tagged extended sRGB
//      (`ImageProvider::cg_color_space`, `0x6B494`). Gamma-encoded,
//      extended-range sRGB, nothing clipped inside the renderer.
//   B. `[BIN]` The layer finaliser's texture path (`IconRendering 0x17F38`, at
//      `0x18D20` and `0x193C0`) sets working AND target to `RBColorSpace 3`
//      over `BGRA8Unorm`: gamma-encoded Display P3, eight bits.
//
// `[OBS]` Apple's baked catalog bitmaps look like B: the half-float rendition,
// taken back to Display P3, lands on integers and spans exactly 0..255, and
// the 8-bit rendition is its per-channel clip. Whether that is B, or A followed
// by an 8-bit Display P3 quantise, the data cannot separate and no CoreUI
// binary was read to decide it. BLENDING IS GAMMA-ENCODED IN BOTH.
//
// What choosing A costs is stated rather than hidden: every `clamp(.., 0, 1)`
// between here and the encode clips an out-of-gamut component earlier than A's
// half-float target would, and differently from B, which clips at the P3
// gamut. For a colour inside sRGB the three coincide.
//
// WHAT WAS NOT READ
// -----------------
// `[OBS]` How `RBFill` turns the P3-tagged colour into the display list's
// working space at render time: only `Conversion` was read, not the call that
// reaches it, and whether that call runs in `float` or in the `half` overload
// (`0x77434`) is unread. `[INF]` An sRGB document colour is passed through
// here unchanged; the target takes it sRGB -> P3 -> sRGB. The two matrices
// below are each other's inverse to about 1e-7, and through the curves in
// `float` the round trip comes back within 1e-4 -- under a fortieth of an
// 8-bit level (gated in `test_color_space.cpp`).
#include "Source/IconComposerFoundation/Values.h"

namespace rb {

// `[BIN]` The sRGB transfer function, decode side, `RenderBox 0x76CB4`:
//
//     a      = |x|
//     linear = a * 0x3D9E8391                      (1 / 12.92)
//     curve  = exp2(log2(a * 0x3F72A76E + 0x3D558919) * 0x4019999A)
//              (1/1.055, 0.055/1.055, 2.4 -- the `a * m + c` is one `fmla`)
//     result = 0x3D25AEE6 >= a ? linear : curve    (0.04045, `fcmge`)
//     result = x < 0 ? -result : result
//
// Display P3 uses the same curve: its transfer nibble is 1 as well.
float srgbDecode(float encoded);

// `[BIN]` The encode side, `RenderBox 0x76E10`:
//
//     a      = |x|
//     linear = a * 0x414EB852                      (12.92)
//     curve  = exp2(log2(a) * 0x3ED555C5) * 0x3F870A3D + 0xBD6147AE
//              (0.41667, 1.055, -0.055 -- again one `fmla`)
//     result = 0x3B4D2E1C >= a ? linear : curve    (0.0031308)
//     result = x < 0 ? -result : result
//
// `[BIN]` The exponent is the `float` 0.4166699946, not 1/2.4 rounded to
// nearest (which is 0.4166666567). It is transcribed as it is.
float srgbEncode(float linear);

// `[BIN]` Gamma-encoded Display P3 to gamma-encoded sRGB, in place, alpha
// untouched: decode, the matrix `RB::ColorSpace::Matrix` builds for the key
// `0x12` (`RenderBox 0x760F8`, constants at `0x15E7C0`, `0x15E7D0`, `0x15E7E0`
// plus three immediates), encode. Not clamped.
void displayP3ToSrgb(float (&rgba)[4]);

// `[BIN]` The other direction, key `0x21` (`RenderBox 0x761A8`, constants at
// `0x15E7F0`, `0x15E800`, `0x15E810`). This is what `IconColor.init(CGColor)`
// does to an sRGB document colour.
void srgbToDisplayP3(float (&rgba)[4]);

// A document colour as the four components `IconColor` holds: gamma-encoded
// Display P3, the grey spaces widened to r = g = b. This is the input
// `automaticGradient` runs on in the target.
void toDisplayP3(const icf::Color& c, float (&rgba)[4]);

// Four components already widened to RGBA, taken from `space` into the working
// space (reading A): Display P3 is converted; sRGB and extended sRGB are the
// working space already; the grey spaces are sRGB-curved greys and pass.
void toWorking(icf::ColorSpace space, float (&rgba)[4]);

// The same from the document's own value, widening a grey first. This is the
// body `asColour` in `IconRenderer.cpp` is meant to have.
void toWorking(const icf::Color& c, float (&rgba)[4]);

}  // namespace rb
