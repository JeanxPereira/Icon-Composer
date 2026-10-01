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
//
// `clampPlusLighter` is a THIRD case, and the difference matters: the flag it
// carries IS read and IS `true` in generation 27, but what it reaches is
// narrower than "every plus-lighter composite" -- one draw, the image of a group
// -- and it has a second gate that is not read. So it defaults to `false` here
// and the one caller inside that set passes it. `clampedPlusL()` below has the
// formula AND the boundary.
struct BlendOptions {
    bool extendedColor = false;
    bool clampPlusLighter = false;
};

// The nine the corpus asks for are total; the rest of the eighteen fall back to
// `Normal` (source-over) rather than to a silent identity, because a mode this
// file has not transcribed must not look like one it has.
bool blendIsTranscribed(BlendMode mode);

// `[BIN]` THE SHADER THE TARGET SUBSTITUTES FOR PLUS-LIGHTER.
//
// `plusLighter` is the one blend mode the target does not let CoreGraphics
// composite. `0x0004B530` and `0x00044618` are the same three lines in two
// draw paths:
//
//     ldrb w24, [x0, #0x31]     ; the drawn node's blend mode
//     cmp  w24, #8              ; 8 == BlendMode::PlusLighter (BlendMode.h)
//     b.ne <plain path>
//     ldrb w8,  [x20, #0x288]   ; ICRRenderingParameters+0x220, embedded at
//     cmp  w8,  #1              ; self+0x68 -- shouldClampPlusLBlending
//     b.ne <plain path>
//     ... objc_msgSend$setBlendShader:<the once-built global at 0xCE8D0>
//
// and the global is built by the `swift_once` body at `0x0000D848`, which calls
// `initWithLibrary:function:` with a function name spelled by two `mov`/`movk`
// chains rather than living in `__cstring` -- `0x0000D88C` gives
// `0x5064_6570_6D61_6C63` and `0x0000D89C` gives `0xEC00_0000_4C73_756C`, which
// as a Swift small string is **`clampedPlusL`** (12 bytes, discriminator `0xEC`).
//
// `[BIN]` And that name is a Metal entry point in this bundle's own stitching
// library, `metallib-iconrendering/default_mod8.ll:37`, whole:
//
//     %3  = fadd fast <4 x half> %1, %0
//     %5  = air.fmin.v3f16(splat(half 0xH3C00), %3.rgb)     ; min against 1.0
//     %10 = air.saturate.f16(%0.a + %1.a)
//     %12 = air.fmax.v4f16(%1, (%5, %10))
//
// `[BIN]` WHICH OPERAND IS WHICH IS NOT INFERRED. The AIR metadata names them:
// `!19 = !{i32 0, ..., !"air.arg_name", !"source"}` and `!20` the same for
// `dest`. So `%0` is the source and `%1` the backdrop, and the `fmax` floors
// the result at the BACKDROP.
//
// Three things follow, and the middle one is the whole point:
//
//   * the rgb cap is the literal `1.0`, NOT the output alpha -- a different
//     clamp from the `extendedColor` one in `pdf_mode`;
//   * so a stack of bright highlights stops at white instead of running to 2 or
//     3, which is what leaves the `plusDarker` highlights drawn afterwards
//     somewhere to descend from;
//   * and the `fmax` against `dest` means the cap can never DARKEN a backdrop
//     that was already above one -- it removes the overshoot, it does not
//     remove light.
//
// `[BIN]` `plusDarker` gets no such treatment: the gate is `cmp w24, #8` and
// nothing else, so mode 4 keeps the plain formula.
//
// `[BIN]` THE FLAG IS `true`. `ICRRenderingParameters.shouldClampPlusLBlending`
// is `params+0x220` (`fieldmd_iconrendering.txt`, field 21 of 35; the struct is
// embedded at `self+0x68`, which is why the two readers say `+0x288`). The
// aggregate default constructor writes it at `0x0005EAE8`
// (`strb w22, [x19, #0x220]`) with `w22` set to `1` at `0x0005E8BC` and never
// reassigned in between.
//
// `[BIN]` And the offset is anchored from OUTSIDE this reading: the very next
// store, `0x0005EAFC str x8, [x19, #0x228]`, is the `0x4070A3D70A3D70A4` that
// doc 03 §29.3 already read as `defaultChicletCornerRadius` = `266.24`, and the
// field list puts `shouldClampPlusLBlending` immediately before it. `+0x218`
// holds the `Int` `2` that is `refractionSupersampling`, the field before that;
// `+0x230` is the `thresholds` the shadow front read. Four consecutive fields
// in declaration order, two of them read by other fronts on other days.
//
// WHICH COMPOSITES THIS REACHES -- AND THE GATE THAT KEEPS IT OFF
// ----------------------------------------------------------------
// `[BIN]` **THE CONSUMER SET IS READ, AS OF 2026-10-01, AND IT IS ONE DRAW.**
// Both gates read the blend mode out of a draw descriptor (`ldrb w24, [x0,
// #0x31]`, `0x0004B518`), and the substitution happens in exactly four places
// -- `0x00044654` and `0x00044908` inside `0x000435A0`, and `0x0004B57C` and
// `0x0004B7F4` inside `0x0004B4EC`. The descriptor is a `FinalizedIcon.Layer`
// -- `+0x31` its `blendMode`, the document GROUP's (`GlassShadow.h`) -- and
// `0x4B4EC` is the draw of that group's flattened image (`drawShape:fill:alpha:
// blendMode:` with `[+0x38]` and `[+0x31]`); the two sites in `0x435A0` are the
// same draw written inline. So the clamp reaches the image of a group whose
// blend is plus-lighter, and nothing else: an ELEMENT's plus-lighter is applied
// inside the group's image by the finaliser (`0x1AD9C`/`0x1B448`), on another
// path, where no such gate was read.
//
// `[OBS]` **AND IT HAS A SECOND GATE, WHICH IS NOT READ.** Right after the flag,
// both sites test one more byte:
//
//     ldrb w8, [x20, #0x528]    ; 0x4B544
//     cmp  w8, #1
//     b.ne <plain path>
//
// `ctx+0x520`..`+0x538` is the drawing context's tuple -- the display list at
// `+0x520`, this `Bool` at `+0x528`, the fill and the shape at `+0x530` and
// `+0x538` -- and the content wrappers copy the byte along when they build a
// fresh tuple (`0x46AFC` -> `strb w21, [sp, #0x5b0]`). Its ORIGINAL writer was
// not found: the three `strb wN, [xM, #0x528]` of the whole `__text`
// (`0x4DB38`, `0x4E3C8`, `0x4F08C`) are value-witness copies of the context,
// and the tuple's initialiser stores it some other way. With the byte unknown,
// turning the clamp on would move every plus-lighter group of generation 27 on
// a guess. It is therefore wired -- `IconRenderOptions::
// drawingContextClampsPlusLighter` is that byte, `IconSurface::blendArt` takes
// the conjunction -- and OFF by default, and `kPlusLighterClampNote` says so in
// the report of every render it could have changed.
//
// `[BIN]` What IS settled is a NEGATIVE, and it is the one that matters for the
// front that went looking: **the glyph specular highlights are not clamped.**
// Their draw is `0x000491C0`-`0x00049DBC`, it calls neither `0x000435A0` nor
// `0x0004B4EC`, and the blend mode it resolves goes straight to
// `-[RBDisplayList drawShape:fill:alpha:blendMode:]` at `0x0000ED00`, the tail
// of the `glassHighlight` builder `0x0000E834`. There is no `cmp #8` and no
// `setBlendShader:` on that path.
//
// `[BIN]` The document layer NODE is likewise built without a gate:
// `0x00025354` calls `addLayer:`, `setOpacity:`, then `setBlendMode:` with `w2`
// read from the 18-entry CGBlendMode table at `0x00094AC0`
// (`ldr w2, [x8, x23, lsl #2]`, `0x00025578` -- the table `BlendMode.h`
// transcribes), then `setHasSpecular:`. That does not settle the DRAW, which is
// a different function and is where the gate lives.
//
// So this file transcribes the shader and reads the flag; the renderer wires it
// to the one draw it reaches and leaves the unread gate to the caller.
BlendColour clampedPlusL(const BlendColour& source, const BlendColour& dest);

// `src` over `dst`, premultiplied, by `mode`.
BlendColour blend(BlendMode mode, const BlendColour& src, const BlendColour& dst,
                  const BlendOptions& options = {});

}  // namespace rb
