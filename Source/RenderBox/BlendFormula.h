#pragma once
// The blend arithmetic itself, transcribed from `RB::Shader::blend` -- the CPU
// oracle for the nine modes the corpus actually asks for.
//
// EVERYTHING HERE IS PREMULTIPLIED
// --------------------------------
// The shader operates on premultiplied `half4`, and so does this. Converting to
// straight alpha at the boundary and back would introduce a division this
// target does not do, and the two disagree wherever alpha is zero.
//
// WHICH ARGUMENT IS THE SOURCE, AND HOW THAT WAS SETTLED
// ------------------------------------------------------
// `[BIN]` The mangled signature is
// `blend(ShaderState, half4, half4)` with the state passed as FOUR separate
// words, so the two colour operands are `%4` and `%5`, and the declaration
// names them `src` then `dst`.
//
// That ordering is easy to get backwards and hard to catch, because **screen,
// multiply, darken and lighten are all symmetric in the two operands** -- they
// read identically under either assignment. The asymmetric pair settles it:
// `[BIN]` case 26 branches on `dst.rgb > 0.5*dst.a` and case 32 on
// `src.rgb > 0.5*src.a`, and `RB::blend_name` calls 26 `overlay` and 32
// `hard_light`. Overlay branching on the backdrop and hard-light on the source
// is the W3C definition of the two; under the reversed assignment the names
// would be swapped. Three independent sources agree -- the signature, the
// name table, and the CoreGraphics index -- so this is not a coin toss that
// happened to land right.
//
// THE COMPOSITION TAIL IS SHARED, AND ONE MODE SKIPS IT
// -----------------------------------------------------
// `[BIN]` Every separable PDF mode ends in the same three lines (`pdf_mode`,
// and inline in cases 25-32):
//
//     out.rgb = src*(1 - dst.a) + dst*(1 - src.a) + B
//     out.a   = src.a + dst.a - src.a*dst.a
//
// where `B` is the mode's own term, already scaled by `src.a * dst.a`.
//
// `[BIN]` **Screen does not use it.** Case 12 sits in the cheap band 11-18 that
// doc 03 §26.1 identified, and computes `src + dst*(1 - src)` on all four
// channels with no tail at all. Folding it into the general shape would give a
// different number, so it is kept separate.
#include <cstdint>

#include "Source/RenderBox/BlendMode.h"

namespace rb {

// One premultiplied RGBA colour, in doubles. The shader is `half`; this is the
// oracle, and it is deliberately wider -- a CPU reference that carried the
// target's rounding could not tell a transcription error from a rounding one.
struct BlendColour {
    double rgba[4] = {0.0, 0.0, 0.0, 0.0};
};

// `[BIN]` `extended_color` (`RenderState` word 3, bit 2) makes `pdf_mode` clamp
// the blended rgb to `[0, out.a]`. The sense is the opposite of the intuitive
// one: the clamp runs when the bit is ON (doc 03 §26.4).
//
// `[OBS]` What the icon path sets it to was not read, so it is a parameter with
// no default baked into the arithmetic. A caller that does not know says so by
// leaving it false, and gets the unclamped number.
struct BlendOptions {
    bool extendedColor = false;
};

// The nine the corpus asks for are total; the rest of the eighteen fall back to
// `Normal` (source-over) rather than to a silent identity, because a mode this
// file has not transcribed must not look like one it has.
bool blendIsTranscribed(BlendMode mode);

// `src` over `dst`, premultiplied, by `mode`.
BlendColour blend(BlendMode mode, const BlendColour& src, const BlendColour& dst,
                  const BlendOptions& options = {});

}  // namespace rb
