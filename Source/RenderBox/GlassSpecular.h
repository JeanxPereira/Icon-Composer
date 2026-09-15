#pragma once
// `hasSpecular` / `specularPlacement`: from the group's material to the shader
// that would draw the highlight -- and to the wall in front of it.
//
// Source: `Docs/Laudos/2026-09-15-especular.md`, read out of
// `References/2.0-125/out/slices/IconRendering.arm64` (VA == file offset) and
// out of `References/2.0-125/out/metallib-iconrendering/default_mod1.ll`.
//
// THIS FILE DOES NOT DRAW, AND THE REASON IS THE POINT
// ----------------------------------------------------
// The destination closed. The numbers did not. Both halves are `[BIN]`, and
// stating the second is what keeps this from becoming a plausible wrong
// picture -- the failure doc 03 §29.8 names by hand.
//
//   `[BIN]` THE DESTINATION. `hasSpecular` gates a whole function, and that
//   function ends in a Metal shader called `glassHighlight`:
//
//     0x00049200  ldrb w8, [x0]        ; material.hasSpecular, descriptor +0x00
//     0x00049204  cmp  w8, #1
//     0x00049208  b.ne #0x49cb8        ; -> epilogue. FALSE DRAWS NOTHING.
//
//   The function is `0x000491C0`-`0x00049DBC` (3068 bytes, the one immediately
//   before the shadow's `0x00049ED4`). It calls `0x0000E834` twice
//   (`0x00049590`, `0x000497AC`), and `0x0000E834` builds the shader by name:
//
//     0x0000E92C-0x0000E948  the Swift small-string literal "glassHighlight"
//                            (x0 = "glassHig", x1 = "hlight\0\xEE", count 14)
//     0x0000E960  -[RBShader initWithLibrary:function:]
//     0x0000E990-0x0000EAF8  ten setArgumentBytes:atIndex:type:count:flags:
//     0x0000EB94  setCIFilterProvider:
//     0x0000ECE8  setShader:bounds:flags:
//     0x0000ED00  -[RBDisplayList drawShape:fill:alpha:blendMode:]
//
//   `[BIN]` So it is NOT a `CAFilter`, NOT `addBlurFilterWithRadius:` and NOT
//   the RenderBox `glass-highlight` drawing style -- doc 03 §29.5's note that
//   `IconRendering` never uses RenderBox style 2 still stands. It is
//   `IconRendering`'s OWN metallib piece, one of the seven of doc 03 §2.
//   This closes the `[OBS] consumo` that §29.7 carried for `hasSpecular`.
//
//   `[OBS]` THE NUMBERS. Every value the shader consumes -- `height`, `inset`,
//   `curvature`, `spread`, `bias`, `direction`, `color`, `opacity`,
//   `blendMode` -- comes from `ICRRenderingParameters.Highlights`
//   (`params+0x250`), a **16113-byte** block (`swift_allocObject` with
//   `w1 = 0x3F01` at `0x0005EBC8`, memcpy of `0x3EF1` at `0x0005EBD8`, stored
//   at `0x0005EBE0`) built by `0x00062A78`-`0x00063C1C` and then resolved per
//   pass through `0x0005E59C` (556 B) and `0x0004C314` (1128 B). **Not one of
//   those numbers has been read.** The document contributes a gate and one
//   bit; it contributes no magnitude at all. Drawing from here would mean
//   inventing the whole parameter set.
//
// WHAT THE SHADER DOES, SO THAT "INSIDE" AND "OUTSIDE" MEAN SOMETHING
// -------------------------------------------------------------------
// `[BIN]` `_glassHighlight` (`default_mod1.ll:60-116`), with `sd` the signed
// distance ALREADY offset by `inset` (`glassHighlight_v1` line 48:
// `%48 = (sdfZero - tex.r) * sdfScale - inset`; `sdfZero == 0.5`, `0x0000EAA8`,
// and `sdfScale == -2 * [descriptor+0xA8]`, `0x0000EA7C`-`0x0000EA84` -- the
// same pair the translucency front read, so `sd` is POSITIVE INSIDE):
//
//     w      = clamp(fwidth(sd), 2^-10, 2.0) * 0.83349
//     band   = saturate(sd/w + 0.5) * saturate((height - sd)/w + 0.5)
//     k      = clamp((height - 1) * 0.5, 0, 1)
//     shade  = mix(1.0, 1 - saturate(sd/height), k*k * curvature * (3 - 2*k))
//     lit    = saturate((dot(direction, normal) - spread) / max(1 - spread, 2^-10))
//     a      = lit * shade
//     out    = (band * a / max(1 + (1 - a) * bias, 2^-10)) * color
//
// `band` is the whole geometric story: the highlight lives exactly where
// `0 <= distance - inset <= height`, antialiased on both edges. `inset` is the
// anchor, `height` the thickness.
//
// AND THAT IS WHAT THE THREE PLACEMENTS ARE. `[BIN]` `0x000495D8`-`0x000495F0`:
//
//     bit set (inside) : (height, inset,          curvature)   alpha = settings.opacity
//     bit clear (out.) : (height, inset - height, 0        )   alpha = constraints.outsetOpacity
//
// Same thickness, same anchor, MIRRORED ABOUT IT: inside puts the band at
// `[inset, inset+height]`, outside at `[inset-height, inset]`. That is, letter
// for letter, what Apple's own inspector string says -- "Choose how highlights
// align with each layer, either inside or outside, or let Icon Composer decide
// automatically" (`Docs/_confrontar/editor-ui.md`, `GroupSpecularInspector`).
//
// `[BIN]` The outside case ALSO zeroes `curvature` (`str xzr, [sp, #0x160]`,
// `0x000495E8`), and zero curvature makes `shade == 1.0` identically. So the
// outside band is FLAT and the inside band falls off with depth. Two of three
// cases are not the same case, and the difference is not only where.
#include <cstdint>

#include "Source/RenderBox/GlassMaterial.h"

namespace rb {

// `[BIN]` The three `Double`s the placement rewrites, in the order the shader
// receives them (argument indices 0, 1 and 4 of `glassHighlight`).
//
// They are `GlassHighlightSettings.height`, `.inset` and `.curvature`
// (`+0x30`, `+0x38`, `+0x40` of a `0x60`-byte element). The layout is fixed by
// `__C.ICRUnitCartesianCoordinates` having **three** fields and not two
// (`fieldmd_iconrendering.txt 0xA2AD8`): with `direction` three `Double`s wide
// the nine scalars run `opacity, direction.x, direction.y, direction.z,
// spread, bias, height, inset, curvature`, then `color` (`RBColor`, 4 floats)
// at `+0x48` and `blendMode` at `+0x58` -- size `0x59`, stride `0x60`, which is
// the stride the loop at `0x0004954C`/`0x00049594` walks. `direction.z` is the
// one the builder tests against zero (`0x0000EE58`), and `spread` the one it
// tests against pi (`0x0000EE3C`-`0x0000EE50`), which is what an angular cone
// would be tested against and an opacity would not.
struct SpecularBand {
    double height = 0.0;
    double inset = 0.0;
    double curvature = 0.0;
};

// `[BIN]` `0x000495E0`-`0x000495EC`. `inside == false` slides the band out by
// its own thickness and flattens it.
SpecularBand specularBand(const SpecularBand& declared, bool inside);

// `[BIN]` `specularPlacement` collapsing to one bit, `0x0004926C`-`0x000492CC`:
//
//     inside  (1) -> 1
//     outside (2) -> 0
//     automatic(0)-> (ctx+0x463 == 1) && (ctx+0x470..0x490 all zero)
//                                     && (ctx+0x498 == 1)
//
// `[OBS]` That last conjunction is the SAME five `Double`s and gate byte the
// shadow front met at `ctx+0x4E8..0x510` and could not name. What it does is
// read -- it is the icon's identity recolour state -- what it IS is not. The
// asymmetry with the shadow is worth writing down: outside the identity state
// the shadow is forced NEUTRAL, and the specular is forced OUTSIDE.
//
// `identityRecolour` is that conjunction, handed in rather than guessed.
bool specularPlacementBit(SpecularPlacement placement, bool identityRecolour);

// `[BIN]` The bit is only HALF the decision, and the other half is not the
// document's. `0x000494E0`-`0x000494FC`:
//
//     w10 = placementBit | (constraints.isDarklight ^ 1)
//     out = (constraints.outsetOpacity_tag == 1) ? 1 : w10        ; csinc
//
// Two overrides, and both force INSIDE:
//
//   1. `constraints.isDarklight == false` makes `w10 == 1` whatever the
//      document said. The bright highlight is drawn inside, always; only the
//      DARK one (the `multiply` pass, blend byte 2 at `0x00049A44`, against
//      `screen` byte 6 at `0x00049554` -- doc 03 §17.3's numbering) can be
//      moved outside.
//   2. `constraints.outsetOpacity` is `Double?`, and tag `1` is `nil`
//      (`HighlightsPass.Constraints`, `fieldmd 0xA3F98`: value at `+0x38`, tag
//      at `+0x40`, read at `0x000494A4`/`0x000494A8`). Outside is what the
//      outset opacity is FOR, so with no outset opacity there is no outside.
//
// This is why an enum of three collapsing to one bit is not a sign that two
// cases coincide: the bit can only ever ASK for outside, and two conditions
// that belong to the renderer, not to the document, can refuse.
bool specularDrawsInside(SpecularPlacement placement, bool identityRecolour, bool isDarklight,
                         bool hasOutsetOpacity);

// Whether the document asked for a highlight at all -- the `cmp w8, #1` of
// `0x00049204`, and nothing more.
//
// Both spellings of the material answer it, because nothing between them
// touches this field: `denormaliseGlass` carries `hasSpecular` across raw, and
// says so where it carries it.
bool documentAsksForSpecular(const GlassMaterial& material);
bool documentAsksForSpecular(const DenormalisedGlass& glass);

// The sentence `IconRenderer` says when a document asks and nothing comes out.
//
// It names the shader and the block that is missing, because "the specular did
// not draw" without an address is the kind of report this project treats as
// worse than silence.
const char* specularDoesNotDrawNote();

}  // namespace rb
