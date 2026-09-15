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
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Source/RenderBox/BlendMode.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassTranslucency.h"

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

// ===========================================================================
// THE 16113 BYTES, READ -- `Docs/Laudos/2026-09-15-highlights.md`
// ===========================================================================
//
// Everything above this line was written on 2026-09-15 with the numbers still
// missing. They are not missing any more. `ICRRenderingParameters.Highlights`
// (`params+0x250`, `0x3EF1` == 16113 bytes, built by `0x00062A78`-`0x00063C1C`)
// opened, and it opened because its own layout is spelled out in the binary as
// IMMEDIATES rather than as pool constants.
//
// THE LAYOUT, AND WHY IT IS NOT A GUESS
// -------------------------------------
// `[BIN]` `Highlights` is a 280-byte preamble and then TEN `HighlightsSet` of
// `0x630` bytes each (size `0x629`, the last one unpadded):
//
//     0x0118 chicletDefault   0x0748 chicletBright   0x0D78 chicletDim
//     0x13A8 chicletClear     0x19D8 chicletScreened
//     0x2008 glyphsDefault    0x2638 glyphsBright    0x2C68 glyphsDim
//     0x3298 glyphsClear      0x38C8 glyphsScreened
//
//     0x0118 + 9*0x630 + 0x629 == 0x3EF1.                    <- closes exactly
//
// and the SELECTOR AT `0x000627B4` MATERIALISES FIVE OF THOSE OFFSETS AS
// LITERALS -- `mov w22, #0x3298`, `mov w23, #0x38C8`, `mov w8, #0x2638`,
// `mov w8, #0x2008`, `mov w8, #0x2C68` -- and then memcpies `mov w2, #0x629`
// bytes from `x20 + offset`. The arithmetic and the binary agree on the same
// two numbers, from opposite directions.
//
// `[BIN]` A `HighlightsSet` is six `HighlightSettings` at stride `0x108`
// (`keySharp 0x000, keyDiffuse 0x108, fillSharp 0x210, fillDiffuse 0x318,
// dark 0x420, rim 0x528`), read straight off the six memcpy destinations of
// `0x00030EC0`-`0x00030F1C` and off the constructor's own
// `x19+0x118 … x19+0x640`.
//
// `[BIN]` A `HighlightSettings` is `0x101` == 257 bytes, ten fields, in the
// metadata's declaration order (`fieldmd 0xA33C4`). The offsets are the
// resolver's own loads at `0x0004BDCC`-`0x0004BE50`:
//
//     +0x00  brightness         Double
//     +0x08  opacity            SizeBasedValue<Double>
//     +0x28  outsetOpacity      SizeBasedValue<Double>?   tag at +0x48
//     +0x50  distance           SizeBasedValue<Double>
//     +0x70  minDistancePixels  SizeBasedValue<Double>    (NOT optional)
//     +0x90  inset              SizeBasedValue<Double>
//     +0xB0  minInsetPixels     SizeBasedValue<Double>?   tag at +0xD0
//     +0xD8  spread             SizeBasedValue<Angle>
//     +0xF8  bias               Double
//     +0x100 blendModeOverride  Icon.BlendMode?           (byte 18 == nil)
//
// `[BIN]` That last byte is also the discriminator of the two enums wrapped
// around this struct, which is how the whole thing stays 257 bytes and how a
// disabled highlight is spelled: `0x00035450` reads `+0x100` and returns
// `max(0, byte - 18)`, so **19 means `HighlightSettings? == nil`**;
// `0x00033F04` uses 19 as `FillHighlights.matchKey`; `0x00035470` uses 20 as
// `FillHighlights? == nil`. Three tag readers, one byte, no extra storage.
//
// THE SELECTION RULE, MEASURED
// ----------------------------
// `[BIN]` `0x000627B4` is the GLYPH closure (`0x0005E194` installs it as `x1`
// and tail-calls the blob-copier `0x0005E59C`, which memcpies all `0x3EF1`
// bytes onto the stack at `0x0005E684`). It picks by TWO things and neither is
// the size class:
//
//     if (fill[+0x90] == 1 && (fill[+0x68]|+0x70|+0x78|+0x80|+0x88) == 0)
//         // a PLAIN fill: five pointers all null
//         switch (fill[+0x5B]) { 0 -> glyphsDefault
//                                1 -> glyphsBright
//                                _ -> glyphsDim }
//     else
//         // a fill that is not plain: two shape comparisons
//         // (0x00040E60 / 0x0006D6B0) choose
//         glyphsScreened or glyphsClear
//
// `[OBS]` What `fill[+0x5B]` IS was not read. Its three-way shape and the
// preamble's `maxDimChicletLuminance = 0.2` / `minBrightChicletLuminance = 0.99`
// (`Highlights+0x98`, `+0xA0`) say luminance class, but the write was not
// found. IT DOES NOT MATTER FOR THIS VERSION'S PIXELS, and that is `[BIN]`:
// `0x00063AEC`-`0x00063BF4` builds all five glyph sets by calling the SAME
// factory `0x00064604` with the SAME three arguments. The five differ by
// nothing. Measuring the index harder would change no pixel here.
//
// THE EXPANSION: ONE SET BECOMES UP TO SEVEN HIGHLIGHTS
// -----------------------------------------------------
// `[BIN]` `0x00030E88` turns a `HighlightsSet` into an array of `Highlight`
// (size `0x131`, stride `0x138`: settings `0x101` padded to `+0x108`, then
// `angleFromKey` Double, `curvature` SizeBasedValue at `+0x110`, `isDarklight`
// at `+0x130`). Seven slots, written at `x19+0x20` + i*`0x138`:
//
//     i  settings                          angleFromKey  curvature        dark
//     0  keySharp                          0             glyphHighlight*  no
//     1  keyDiffuse                        0             1,1,1,1          no
//     2  fillSharp   (matchKey->keySharp)  +pi           glyphHighlight*  no
//     3  fillDiffuse (matchKey->keyDiffuse)+pi           1,1,1,1          no
//     4  dark                              +pi/2         glyphDarklight*  YES
//     5  dark                              -pi/2         glyphDarklight*  YES
//     6  rim                               0             glyphHighlight*  no
//
// then `0x00031338`-`0x000313EC` drops every slot whose settings were nil.
// `[BIN]` With this version's glyph defaults `fillDiffuse` and `rim` ARE nil,
// so **five highlights survive**, and the dark one is drawn twice, mirrored.
//
// THE RESOLUTION, FIELD BY FIELD
// ------------------------------
// `[BIN]` `0x0004BD90` turns a `Highlight` into the `0x60`-byte
// `GlassHighlightSettings` the shader gets. Every line of it:
//
//     k         = ctx[0x469F]                  the size class, 0..3
//     v[k]      = SizeBasedValue.slots[3 - k]  0x0004BEB0-0x0004BEF4
//     height    = max(distance[k], minDistancePixels[k] * pixelUnit)   0x0004BF00
//     inset     = max(inset[k],    minInsetPixels[k]    * pixelUnit)   0x0004BF2C
//                 minInsetPixels == nil feeds -INFINITY there          0x0004BF20
//     theta     = angleFromKey + lightLongitude                        0x0004BEF8
//     direction = (cos(phi)*sin(theta), cos(phi)*cos(theta), sin(phi)) 0x0004C014
//     opacity   = ctx[0] * opacity[k]                                  0x0004C0A8
//     colour    = (brightness, brightness, brightness, 1.0)            0x0004C0AC
//     blendMode = blendModeOverride ?? (brightness < 0.5 ? 4 : 8)      0x0004C0A0
//     spread, bias, curvature: carried through
//
// `[BIN]` THE SIZE-CLASS INVERSION IS THE SHADOW FRONT'S, CONFIRMED FROM A
// SECOND SITE. `0x0004BEB0`-`0x0004BEF4` is a four-way `cbz`/`b.eq` ladder that
// lands `k == 0` on `slots[3]` and `k == 3` on `slots[0]`, exactly the
// `slots[3 - sizeClass]` of `GlassShadow.h` -- read here out of a different
// function on a different struct.
//
// `[BIN]` AND THE BLEND NUMBERS LAND ON THIS PROJECT'S OWN ENUM. `4` and `8`
// out of `csel w8, w9, w8, mi` are `BlendMode::PlusDarker` and
// `BlendMode::PlusLighter` as `BlendMode.h` already numbered them, from the
// RenderBox side, months before this was read. A dark highlight subtracts and a
// bright one adds, and nobody had to choose that.
//
// THE TWO SHADER-SIDE TRANSFORMS
// ------------------------------
// `[BIN]` `bias' = 1/bias - 2`, `0x0000E9DC`-`0x0000E9EC`, as a float. `0.5`
// is the neutral value: it makes `bias' == 0` and the denominator 1.
//
// `[BIN]` `spread' = cos(spread)`, with the sentinel `-1000.0f` when
// `spread > pi` (`0x0000EE3C`-`0x0000EE54`, `0x0000EF50`-`0x0000EF5C`). This is
// what makes the cone work at all: `lit = saturate((dot - spread')/max(1 -
// spread', 2^-10))` with a RADIAN in `spread'` would be zero everywhere, and
// with its cosine it is a half-angle. `pi/2 -> 0` is a hemisphere; `pi/3 ->
// 0.5` is a 60-degree cone; `pi -> -1000` is "always lit".
//
// `[BIN]` `curvature` is zeroed when `inset < 0` (`0x0000EF60`-`0x0000EF68`),
// and the direction reaches the shader as the float2 `(x, -y)`
// (`fneg s6`, `0x0000EF8C`).
//
// WHAT IS STILL `[OBS]`, NAMED SO THE PIXEL CAN BE DOUBTED IN THE RIGHT PLACE
// ---------------------------------------------------------------------------
//   1. `ctx[0]`, the scalar every opacity is multiplied by (`0x0004C010`), and
//      `ctx[0x08..0x20]`, the light latitude `phi`. Not read. Taken as `1.0`
//      and `phi == 0` here -- which is the branch `0x0004BE74` takes when
//      `ctx[0x20] == 1`, so `phi == 0` is at least a state the target has.
//   2. `0x00012550`, a 1.5 KB post-pass over the resolved settings (the
//      `spatialHighlighting` parameters, `params+0x258`). Not followed.
//   3. `fill[+0x5B]`, above: three-way, source unread, and inert in 2.0-125.
//   4. The SDF texel encoding, which `GlassTranslucency.h` already carries as
//      `[OBS]`. Here the `.gb` normal joins it: this file feeds the shader the
//      field's own gradient, normalised, and the target feeds it `1 - 2*tex.gb`.
//
// ===========================================================================
// WHAT APPLE'S OWN RENDER SAID ABOUT THE BAND -- `2026-09-15-realce-forma.md`
// ===========================================================================
//
// The colour of the band closed against the oracle (bias +34.3 -> +0.10). The
// PLACE did not, and the oracle says where it is instead. Measured against
// `References/27.0-129/out/apple-512.png` (and the `.icns` 256 rendition), on
// the 13 498 px of the band, with the render at 412 px pasted at (50,50) in a
// 512 frame -- no resampling.
//
// `[BIN]` **THE UNITS AND THE SIZE CLASS ARE OURS ALREADY, AND THAT IS READ,
// NOT ASSUMED.** `0x00042D3C`-`0x00042D50`: `ctx+0x46A0 = 1/scale` and
// `ctx+0x46A8 = (1/scale) / [contentsScale]`, with
// `scale = min(CGRectGetWidth(rect)/W, CGRectGetHeight(rect)/H)` of the canvas
// (`_CGRectGetWidth` = `0x8DA28`, `_CGRectGetHeight` = `0x8D9D4`, resolved
// through the INDIRECT SYMBOL TABLE and not by name). So `ctx+0x46A8` -- the
// multiplier of `minDistancePixels` -- is CANVAS UNITS PER PIXEL, which is the
// reciprocal of `SpecularArguments::pixelsPerPoint`, and `height`/`inset` live
// in canvas units. `[BIN]` The size class is `min(rect.w, rect.h)` against
// `params+0x230/0x238/0x240` (`0x00042D54`-`0x00042DE4`, and again in
// `0x00018D70`-`0x00018DCC` over `size/scale`), whose values are read out of
// the pool at `0x98600`/`0x98610`: `minMediumSize = 25`, `minLargeSize = 60`,
// `minDisplaySize = 256`. The oracle's 512 px rendition is a 256 pt icon whose
// chiclet rect is 206 pt -- `large`, which is what this renderer uses.
//
// `[BIN]` **AND `inset` REALLY IS ZERO.** The factory `0x00064604` was re-read
// field by field: `keySharp` writes zeros at `+0x90`/`+0xA0` and the `nil` tag
// `1` at `+0xD0` (`0x00064698`-`0x000646B0`), `keyDiffuse` the same
// (`0x00064730`-`0x00064738`). Nothing in `0x0004BD90`, in `0x0000ED94` (which
// only multiplies `height` and `inset` by `(sdfTexels-2)/rect.width`) or in the
// pass grouping `0x0004C314` (which only COMPARES) adds a term.
//
// `[OBS]` **AND YET THE TARGET'S BAND IS ~7.5 CANVAS UNITS DEEPER THAN OURS.**
// Profiles of luma against the field's own depth, per normal sector: Apple's
// bright band runs from ~5 to ~20-30 units with its peak at ~9, over a rim
// (0..~5 units) that is DARKER than the interior; ours peaks at 0..2.5 units
// and is over by 6. Sliding our `sd` inward is the only thing that moves the
// mean band error below the "draw no highlight" control:
//
//     none (control) 19.78   base 23.42   sd-2.4px 17.93   sd-3px 17.60
//
// and the same scan at the 256 px rendition peaks at 1.5 px -- `7.5 +- 0.9`
// canvas units at BOTH sizes, so the displacement is geometric and not a pixel
// artefact. NOTHING WAS CHANGED BECAUSE OF IT: a slide with no `[BIN]` behind
// it is exactly the overfitting the oracle exists to prevent, and on the user's
// own icon it moves 91 301 px (9.25 %) and takes the highlight off every stroke
// thinner than 7.5 units. The two inputs that could carry such an offset are
// both unread: the SDF texture's own zero level (CoreUI's
// `sdfTextureWithBufferAllocator:`, in neither slice) and `[descriptor+0xA8]`,
// the `maxDistance` that `0x00049238` loads for `sdfScale`.
//
// `[BIN]`+oracle **THE GRADIENT'S SIGN IS SETTLED.** The laudo of the
// highlights carried it as ARGUED ("outward, because that is what makes
// `angleFromKey = 0` light the top"). All eight symmetries of `(nx, ny)` were
// rendered against the oracle; the identity is the best of the eight, and the
// plain flip is the worst (band error 23.42 vs 25.74). The argument and the
// oracle agree.
//
// `[ART]` **AND THE SPATIAL POST-PASS CANNOT BE THE CULPRIT HERE.**
// `customLightDirection` is a field of `GlobalConfiguration`, not of the
// document: of the 145 corpus documents, 82 carry a `lighting` key and its only
// values are `individual` and `combined` -- no document can spell a light
// direction, so the `nil` tag that makes `0x00012550` the identity is the state
// every document renders in.

// `[BIN]` The `SizeBasedValue<Double>` of `HighlightSettings`, plus the
// `Optional` tag where the field has one. Four slots in MEMORY order --
// `display, large, medium, small` -- indexed `slots[3 - sizeClass]`.
struct HighlightSizeValue {
    double slots[4] = {0.0, 0.0, 0.0, 0.0};
    bool present = true;  // the Optional tag; `false` == `nil`
};

double highlightSizeValue(const HighlightSizeValue& v, IconSizeClass sizeClass);

// `[BIN]` `IconRendering.HighlightSettings`, ten fields, `0x101` bytes.
struct HighlightSettings {
    double brightness = 0.0;
    HighlightSizeValue opacity;
    HighlightSizeValue outsetOpacity;      // `present == false` in four of five
    HighlightSizeValue distance;           // -> the shader's `height`
    HighlightSizeValue minDistancePixels;
    HighlightSizeValue inset;
    HighlightSizeValue minInsetPixels;
    HighlightSizeValue spread;             // radians
    double bias = 0.5;
    // `blendModeOverride` is `nil` in every default this version ships, so it
    // is a `std::optional` and not a raw byte: a reader must not mistake the
    // sentinel 18 for the mode 18.
    bool hasBlendModeOverride = false;
    BlendMode blendModeOverride = BlendMode::Normal;
};

// `[BIN]` `IconRendering.Highlight` -- one settings block plus the three things
// `0x00030E88` attaches to it.
struct HighlightSlot {
    HighlightSettings settings;
    double angleFromKey = 0.0;  // radians
    HighlightSizeValue curvature;
    bool isDarklight = false;
};

// `[BIN]` The five that survive for a GLYPH in 2.0-125, in draw order.
// Identical for all five `glyphs*` sets -- see the note above.
const HighlightSlot* glyphHighlightSlots(std::size_t& count);

// `[BIN]` `IconRendering.GlassHighlightSettings`, the `0x60`-byte element the
// shader is handed. `height` and `inset` are already in PIXELS, because that is
// where the two halves of `max(distance, minDistancePixels * pixelUnit)` can
// meet: one side is canvas points and the other is device pixels, and the
// target's `pixelUnit`/`escala` pair cancels to exactly this ratio. `spread` is
// the raw ANGLE, not its cosine -- the cosine is applied where the target
// applies it, at the shader boundary.
struct GlassHighlightSettings {
    double opacity = 0.0;
    double directionX = 0.0, directionY = 0.0, directionZ = 0.0;
    double spread = 0.0;
    double bias = 0.5;
    double height = 0.0;
    double inset = 0.0;
    double curvature = 0.0;
    double colour[4] = {0.0, 0.0, 0.0, 1.0};
    BlendMode blendMode = BlendMode::PlusLighter;
};

// `[BIN]` `ICRRenderingParameters.SpatialHighlighting`, `params+0x258`, six
// `Double`s, named by `fieldmd_iconrendering.txt 0xA48AC` and consumed -- all
// six, in this order and nowhere else -- by `0x00012550`.
//
// `[OBS]` THE SIX VALUES ARE NOT READ. The builder that fills `params+0x258`
// was not followed. What IS read is the function, and the function is what
// decides whether they can matter: see `spatialHighlight()`.
struct SpatialHighlighting {
    double alignmentRange = 0.0;   // `params+0x258`, an ANGLE: `sin()` is taken
    double intensityPower = 0.0;   // `params+0x260`
    double minIntensity = 0.0;     // `params+0x268`
    double spreadPower = 0.0;      // `params+0x270`
    double heightPower = 0.0;      // `params+0x278`
    double maxExtraHeight = 0.0;   // `params+0x280`
    // `false` says the six above are placeholders, not measurements. The only
    // caller that may pass `false` is one where the pass is provably the
    // identity -- which is every caller with `lightLatitude == 0`.
    bool read = false;
};

// `[BIN]` `0x00012550` (288 bytes), the post-pass `0x0004C0F8` runs over the
// `GlassHighlightSettings` it has just built, in place. Four rewrites, and one
// scalar drives all four:
//
//     t = max(0, 1 - hypot(dir.x, dir.y) / sin(alignmentRange))   ; 0x12594-0x125B8
//     opacity *= minIntensity + (1 - minIntensity)*(1 - t^intensityPower)
//                                                                ; 0x125BC-0x125E0
//     spread  += t^spreadPower * (pi - spread)                   ; 0x125E4-0x12614
//     height  *= 1 + maxExtraHeight * t^heightPower              ; 0x12618-0x12638
//     theta    = atan2(dir.x, dir.y)
//     dir      = (sin theta, cos theta, 0)                       ; 0x1263C-0x12650
//
// The stubs are named, not guessed: `0x8DD28` is `_hypot`, `0x8DE00` is `_sin`,
// `0x8DDF4` is `_pow`, `0x8DCEC` is `_atan2`, `0x8DC68` is `___sincos_stret`.
// The pi at `0x125F0`-`0x125FC` is a `mov`/`movk` quartet spelling
// `0x400921FB54442D18`, which is why it is not in the constant pool.
//
// TWO CONSEQUENCES, AND BOTH ARE RESULTS RATHER THAN CODE
// -------------------------------------------------------
//   1. `[BIN]` THE LAST REWRITE DELETES THE LATITUDE. The direction that
//      reaches the shader is ALWAYS `(sin theta, cos theta, 0)`, whatever
//      `phi` was: `atan2` throws the length away and `str xzr, [x20, #0x18]`
//      zeroes `z` unconditionally. So `phi` cannot move a highlight. It can
//      only reach the pixel through `t`.
//
//   2. `[BIN]` AT `phi == 0` THE WHOLE PASS IS THE IDENTITY, AND THE SIX
//      UNREAD NUMBERS CANNOT CHANGE THAT. `resolveHighlight` builds
//      `dir = (cos(phi) sin(theta), cos(phi) cos(theta), sin(phi))`, so
//      `hypot(dir.x, dir.y) == |cos(phi)| == 1`. `sin` of any angle is at most
//      `1`, so `1/sin(alignmentRange) >= 1` and `t = max(0, 1 - something >= 1)`
//      is exactly `0`. `pow(0, p) == 0`, so the opacity factor collapses to
//      `minIntensity + (1 - minIntensity) == 1`, the spread gains nothing, the
//      height gains nothing, and the direction is already flat.
//
// And `phi` IS `0` here -- not by assumption but by the tag: `0x0004BE74`
// takes the `phi = 0` branch when `ctx[0x20] == 1`, and `ctx+0x00..0x67` is a
// verbatim copy of `IconRendering.GlobalConfiguration` (see
// `SpecularArguments::lightIntensity`), whose second field is
// `customLightDirection: ICRUnitCartesianCoordinates?` -- so `ctx[0x20]` is
// that Optional's tag and `== 1` is `nil`.
void spatialHighlight(GlassHighlightSettings& settings, const SpatialHighlighting& p);

// Everything the resolution needs that is not in the slot.
struct SpecularArguments {
    IconSizeClass sizeClass = IconSizeClass::Large;
    // pixels per canvas point: `size / kCanvasPoints`, the same ratio the
    // shadow uses.
    double pixelsPerPoint = 1.0;
    // `[BIN]` `Highlights.defaultGlyphLight.longitude`, `Highlights+0x08`,
    // which this version sets to `0.0` (`stp xzr, xzr, [x8]`, `0x00062AB8`).
    double lightLongitude = 0.0;
    // `[BIN]` THE SCALAR THAT MULTIPLIES EVERY RESOLVED OPACITY, and it has a
    // name now. `0x0004C010` loads `ctx[0x00]` and `0x0004C0A8`
    // (`fmul d12, d13, d12`) multiplies the size-resolved opacity by it; it is
    // loaded in exactly ONE place in the whole slice and stored in exactly one
    // (`0x00042884`, `str q0, [sp, #0x480]`, where `sp+0x480` is the context
    // base -- `add x19, sp, #0x480` at `0x00042B28`, immediately before
    // `add x0, x19, #0x5f0` and the `0x3EF1`-byte `memmove` of `Highlights`).
    //
    // `[BIN]` Those first `0x68` bytes are copied verbatim from the fifth
    // argument (`x4`) of `0x0004266C`, and the struct is
    // `IconRendering.GlobalConfiguration` (`fieldmd 0xA327C`, 13 fields). Its
    // layout falls out field for field:
    //
    //     +0x00 lightIntensity            Double   <- THIS
    //     +0x08 customLightDirection      ICRUnitCartesianCoordinates? (3 x Double
    //           .. +0x18                           + tag at +0x20)
    //     +0x21 effectsAreEnabled         Bool     +0x22 drawMitigatedVersion
    //     +0x23 forceEnableEnhancedGlass  Bool     +0x24/+0x25 the two ClearMode
    //     +0x26 allowHDR                  Bool?
    //     +0x28 enabledRenderingSteps     Int      <- the one `0x0004284C` ANDs
    //     +0x30 _relativeIconInset        Double?  +0x40/+0x48 canvasSize
    //     +0x50 chicletDropShadow         Bool?    +0x51..+0x67 iconShape
    //
    // which is `0x68` bytes exactly, and which is why `0x0004BE74` reads
    // `ctx[0x20]` as an Optional tag and `0x0004BEA8` reads a size class far
    // above it.
    //
    // `[BIN]` The value is `1.0`. `Docs/_confrontar/kernels/default-ramps.md`
    // read the other initialiser of the same struct -- the `lightAngle:` one at
    // `0x35DF0` -- and found `*param_1 = 0x3ff0000000000000` baked in at struct
    // offset `0x0` precisely because that init supplies no intensity. So the
    // identity this front took on faith is the identity the target ships.
    double lightIntensity = 1.0;
    // `[BIN]` The light's latitude, `phi`. `0x0004BE74` takes the `phi = 0`
    // branch when the `customLightDirection` Optional tag (`ctx+0x20`) is `1`,
    // i.e. `nil` -- the state this renderer is in. And see
    // `spatialHighlight()`: even a non-zero `phi` cannot move a highlight,
    // because `0x00012550` overwrites the direction with the `phi = 0` form.
    double lightLatitude = 0.0;
    // `[BIN]` `[descriptor+0x38]`, the same multiplier the shadow front named:
    // it scales the `alpha:` of the `drawShape:` (`fmul d0, d10, d0`,
    // `0x000495F0`).
    double layerOpacity = 1.0;
    SpecularPlacement placement = SpecularPlacement::Automatic;
    bool identityRecolour = true;
    SpatialHighlighting spatial;
    // `shouldClampPlusLBlending` (`params+0x220`), which swaps CoreGraphics'
    // plus-lighter for the `clampedPlusL` Metal shader. The flag is `[BIN]` and
    // `true`, and the shader is `[BIN]` and transcribed in `BlendFormula.h`.
    //
    // `[OBS]` **IT IS OFF HERE, AND THAT IS A MEASUREMENT, NOT A HEDGE.** The
    // substitution happens in exactly four places (`0x00044654`, `0x00044908`,
    // `0x0004B57C`, `0x0004B7F4`), all inside the two content-draw functions
    // `0x000435A0` and `0x0004B4EC`. **The glyph specular draw reaches none of
    // them**: `0x000491C0`-`0x00049DBC` calls neither function, and it hands its
    // blend mode straight to `drawShape:fill:alpha:blendMode:` at the bottom of
    // `0x0000E834` (`0x0000ED00`), where there is no gate.
    //
    // So the clamp is real, it is on in the target, and what it covers is NOT
    // this. Turning it on here moved zero pixels of the user's icon through the
    // glyph path, which is what that reading predicts. The field is kept so the
    // front that identifies the covered draws has somewhere to put the answer.
    bool clampPlusLighter = false;
    // `[BIN]` `glyphHighlightsUseVCM`, `Highlights+0x90`, read at `0x000494D8`,
    // `true` in this version. When set, a highlight does not paint its colour:
    // its shape becomes a CLIP and the backdrop under it is run through
    // `glyphVCMMatrix()` -- see `applyGlyphVCM` for the whole chain and the
    // RenderBox addresses that prove the matrix reads the backdrop.
    //
    // `false` keeps the old plusLighter/plusDarker composite, which is the
    // `useVCM == false` branch scaled by `glyphHighlightNonVCMScale == 1.0`
    // (`0x0004955C`). No caller in this renderer takes it any more.
    bool useVCM = true;
};

// `[BIN]` The five fields of a `VCM` ("Video Color Matrix"): `glyphHighlightVCM`
// at `Highlights+0xB0` and `glyphDarklightVCM` at `+0xD8`
// (`Docs/Laudos/2026-09-15-highlights.md` §4.1, `0x00062AB8`-`0x00062B34`).
struct GlyphVCM {
    double lumaFloor = 0.0;     // VCM[0]: Y <- (VCM[1] - VCM[0]) * Y + VCM[0]   (0x00049A64)
    double lumaCeiling = 1.0;   // VCM[1]
    double saturation = 1.0;    // VCM[2]: Cb,Cr <- VCM[2]*c + (0.5 - 0.5*VCM[2]) (0x00049B18)
    double headroom = 0.0;      // VCM[3]: the `addContentHeadroom:` argument
    bool skipHeadroomClamp = true;  // VCM[4]: `cmp w19, #1; b.eq` at 0x000497C0
};

// `[BIN]` `[0.2, 1.2, 1.25, 0.0, true]` and `[-0.15, 0.7, 1.25, 0.0, true]`.
const GlyphVCM& glyphHighlightVCM();
const GlyphVCM& glyphDarklightVCM();

// Runs one STRAIGHT (not premultiplied) rgb triple through the target's chain:
// BT.709 RGB->YCbCr (`0xE2960`, from `__const 0x938E0`), levels on Y, chroma
// gain about 0.5, BT.709 YCbCr->RGB (`0xE2910`, from `__const 0x93920`). No
// clamp: `VCM[4] == 1` skips the `RB::Filter::ColorClamp` of `addStyle:9`.
void applyGlyphVCM(const GlyphVCM& vcm, double rgb[3]);

// `[BIN]` `0x0004BD90`, plus the `inside`/`outside` fold of `0x000495D8`.
GlassHighlightSettings resolveHighlight(const HighlightSlot& slot, const SpecularArguments& args);

// `_glassHighlight`, `default_mod1.ll:60-116`, one fragment.
//
// `sd` is the signed distance in PIXELS, positive inside, BEFORE the `inset`
// offset (the shader subtracts it). `nx`,`ny` are the outward unit normal in
// image space (y down). `fwidthSd` is `fwidth(sd)`, which for a field whose
// slope is 1 per pixel is 1.
//
// Returns the scalar the colour is multiplied by -- `band * a / max(...)`.
double glassHighlightFragment(const GlassHighlightSettings& s, double sd, double nx, double ny,
                              double fwidthSd);

// Composites the five highlights over `rgba` (premultiplied, `field.width` x
// `field.height`). With `args.useVCM` (the shipped state) each highlight is
// `lerp(backdrop, VCM(backdrop), coverage)` with alpha untouched, so `rgba`
// must be the BACKDROP -- the composite the layer has already gone into, not
// the layer's own buffer. Without it, each highlight is its colour under its
// own blend mode. Returns the number of pixels the composite actually moved.
std::size_t drawSpecular(std::vector<float>& rgba, const FieldImage& field,
                         const SpecularArguments& args);

// The sentence `IconRenderer` says when a document asks and nothing comes out.
//
// It names the shader and the block that is missing, because "the specular did
// not draw" without an address is the kind of report this project treats as
// worse than silence. KEPT for the one case that still cannot draw -- raster
// art, which has no contour and therefore no distance field.
const char* specularDoesNotDrawNote();

// The sentence for the case that DOES draw: which five highlights went on, and
// which two inputs are still `[OBS]` underneath them.
const char* specularDrawnNote();

}  // namespace rb
