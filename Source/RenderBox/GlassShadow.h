#pragma once
// The glass material's SHADOW: from the group's `shadow` key to the pixel.
//
// Source: `Docs/Laudos/2026-09-15-sombra.md`, which reads the whole consumption
// out of `References/2.0-125/out/slices/IconRendering.arm64`, and
// `Docs/Laudos/2026-09-15-sombra-desenho.md`, which is this front. Every address
// quoted below is that slice's (VA == file offset).
//
// WHAT WAS DRAWN BEFORE THIS FILE: NOTHING. `GlassMaterial.h` transported
// `shadowStyle` and `shadowOpacity` and said, in as many words, that they had no
// known consumer. `grep -n shadow Source/RenderBox/IconRenderer.cpp` found one
// comment about an SVG's own drop shadow and no arithmetic. The document carried
// the fields, the inspector edited them, and the picture ignored both.
//
// THE ALPHA, WHICH IS THREE FACTORS AND NO CLAMP
// -----------------------------------------------
// `[BIN]` The whole of it, from the function at `0x49ED4`-`0x4A2D4`:
//
//     alpha = shadowOpacity
//           x Shadow.<vibrant|neutral>Opacity[3 - sizeClass]
//           x layerOpacity                                     <- see below
//
// Two `fmul`, at `0x4A06C` and `0x4A070`, and NOTHING else on the path: no
// `fminnm` of a clamp, no `pow`, no `Max`/`Power` partner in
// `ICRRenderingParameters`. The laudo swept all seven `_pow` callers and none is
// in this chain.
//
// `[ART]` THE ABSENCE OF THE CLAMP IS NOT ACADEMIC, and the corpus settles it in
// a way a synthetic test never could. Of 302 `shadow` resolutions over the 145
// documents, THREE carry an opacity ABOVE 1.0 -- `Apollo-Reborn/AppIcon` groups
// 0 and 2 at `2.4` and `1.6`, and `Apollo-Reborn/LG-antenna` group 0 at `2.4`,
// all three `neutral`, all three on groups that contain glass. And they land on
// round numbers against the measured table:
//
//     2.4 x neutralOpacity 0.375 = 0.9        1.6 x 0.375 = 0.6
//
// Two authored values, two exact tenths. That is the table's default and the
// missing clamp corroborating each other from the document side: a transcription
// that clamped `shadowOpacity` to `[0,1]` would draw Apollo's shadow at 0.375
// instead of 0.9 and would have no way to notice.
//
// THE THIRD FACTOR, WHICH THE LAUDO LEFT UNNAMED AND THIS FRONT NAMED
// ---------------------------------------------------------------------
// `[BIN]` `ldr d9, [x0, #0x38]` at `0x49F10`, multiplied in at `0x4A070`; the
// same `Double` multiplies the element's own draw at `0x495F0`. The laudo
// (§5.2, open item 1) could only say "a Double just past the `GlassMaterial`,
// suspected layer opacity, writer not followed". It is
//
//     IconRendering.FinalizedIcon.Layer.opacity : Swift.Double
//
// and the `0xC0`-byte "draw descriptor" has a name too: it IS
// `FinalizedIcon.Layer`. Three independent readings, each address-backed:
//
//   1. `[BIN]` REFLECTION. `out/fieldmd_iconrendering.txt`, from
//      `__swift5_fieldmd` at `0xA301C`: `[struct] IconRendering.
//      FinalizedIcon.Layer (9 fields)` -- `material`, `blendMode`, **`opacity`
//      typed `Sd`**, `knocksOutBorder`, `image`, `contentFrame`, `effectsFrame`,
//      `sdf`, `shadowImage`. Third field, and `Sd` IS `Swift.Double`.
//   2. `[BIN]` THE EMITTED FIELD-OFFSET VECTOR, so the offsets are read and not
//      computed from the field order: `__swift5_types` at `0xAC614` ->
//      descriptor `0x9EF30` (`FieldOffsetVectorOffset` 2) -> static metadata at
//      `0xBD3F0`, giving `material@0x00, blendMode@0x31, opacity@0x38,
//      knocksOutBorder@0x40, image@0x48, contentFrame@0x50, effectsFrame@0x70,
//      sdf@0x98, shadowImage@0xB0`. The value-witness table at `0xBD388` gives
//      size and stride `0xC0`/`0xC0` -- the laudo's array stride, confirmed from
//      the type rather than from the `add x23, x23, #0xc0`.
//   3. `[BIN]` THE WRITER, which the laudo had not followed. The conversion loop
//      at `0x16314`-`0x174D8` walks the source with stride `0x58` (`add x26,
//      x26, #0x58`, `0x1702C` -- `Icon.Layer`'s own size) and the destination
//      with stride `0xC0` (`add x24, x24, #0xc0`, `0x1703C`), and at `0x17038`
//      does `str d8, [x8, #0x58]` with `x8 = arrayObject + x24`, i.e. element
//      offset `0x58 - 0x20` = **`+0x38`**, the `0x20` being the Swift array
//      header. `d8` is loaded at `0x1704C` as `ldur d8, [x26, #-0x10]`, which is
//      `Icon.Layer.opacity`. **Copied verbatim: no clamp, no transform.**
//
// AND THE LAYOUT CORROBORATES ITSELF INSIDE THE SHADOW FUNCTION. `[BIN]` The
// very next instruction after the one that reads the factor is `0x49F14
// ldp x19, x24, [x0, #0xb0]` -- and `+0xB0` is `shadowImage`. The function that
// draws the shadow reads the field the metadata calls the shadow's image, four
// bytes after reading the field the metadata calls the opacity. Two fields, one
// table, no arithmetic in between.
//
// WHOSE OPACITY IT IS, CORRECTED ON 2026-10-01. Everything above stands: the
// field is `FinalizedIcon.Layer.opacity`, copied verbatim from
// `Icon.Layer.opacity`. What this header then got wrong was the DOCUMENT side of
// that name. `[BIN]` In the target's model an `Icon.Layer` is a document GROUP,
// and a document layer is an `Icon.Element`: the converter (`IconComposerKit`
// `0x10C494`) builds one `Icon.Layer` per group and hands the value of
// `Group.opacity.getter` (`0x10C91C`) to its init (`0x10CB08`). So the third
// factor is the GROUP's `opacity`, and `0x495F0`, quoted above as "the element's
// own draw", is the highlight pass multiplying by that same group field. The
// document LAYER's `opacity` is `Icon.Element.opacity`, and it is applied inside
// the group's image (`0x1AD9C`/`0x1B448`) and inside the shadow's own source list
// (`0x1C0DC`, drawn with the element's real opacity) -- which is how it still
// reaches the shadow, by a door that is not this factor.
//
// `[BIN]` **AND THE LAUDO'S COUNTER-INDICATION DISSOLVES.** It hesitated because
// `Shadow.ignoreFillOpacity` is `true` by default and is never consulted at
// `0x49ED4`. It need not be: `ignoreFillOpacity` belongs to
// `ICRRenderingParameters.Shadow`, a PARAMETERS struct, and `+0x38` is a LAYER's
// opacity in a different type entirely. They are two quantities, not two
// readings of one, and the tension was between a field and its own namesake.
//
// TWO CORRECTIONS TO THE LAUDO FALL OUT OF THE SAME TABLE. `[BIN]` The byte at
// `+0x31`, which §5.3 calls "um `Bool` do descritor", is `FinalizedIcon.Layer.
// blendMode` -- an `Icon.BlendMode`, an 18-case enum that packs into one byte
// because `Icon.GlassMaterial` has size `0x31` and stride `0x38`. And
// `[descriptor+0xB0]`, which §5.2 calls only "a imagem", is `shadowImage`: the
// output of the §5.1 preparation, fed back in at §5.2. Neither changes an
// arithmetic; both close a name.
//
// THE INDEX TRAP, WHICH IS SILENT WITH THIS VERSION'S DEFAULTS
// -------------------------------------------------------------
// `[BIN]` Every four-slot table here is read `slots[3 - sizeClass]`: the size
// enum is `small 0 .. display 3` (`0xA5800`) and `SizeBasedValue` declares its
// four members `display, large, medium, small` (`0xA5780`), so the two run
// opposite ways. `0x49FA4`-`0x49FD0` is the `csel` ladder that does it.
//
// With THIS version's defaults all four slots of all five `Shadow` tables hold
// the same number, so `slots[c]` and `slots[3 - c]` give a BIT-IDENTICAL picture
// and no test in this repository can tell them apart. It is written the way it
// was read anyway, through `sizeBasedValue` in `GlassTranslucency.h`, which is
// the same function the translucency front already routes its identical trap
// through. One inversion, one place.
//
// THE GEOMETRY, WHICH THE LAUDO ALSO MEASURED
// --------------------------------------------
// `[BIN]` §5.1, function `0x20B14` (and the same sequence inlined at
// `0x1946C`-`0x195CC`). With `s = min(width, height) / 1024` -- the literal
// `2^-10` at `0x20B88` -- the shadow image is the element's own drawing with:
//
//   1. a COLOUR filter: `addColorMultiplyFilterWithColor(v,v,v,1)` with
//      `v = Shadow.vibrantBrightness` on the vibrant branch (`0x20BB4`, SKIPPED
//      when `v == 1.0`, `0x20BC0`), or `addAlphaMultiplyFilterWithColor(
//      RBColorBlack)` on the neutral one (`0x20BAC`, GOT `0xC5818`);
//   2. a TRANSLATION of `(s*offsetX, s*offsetY)` (`0x20BDC`-`0x20BEC`);
//   3. a BLUR of `radius = s * blurStrengthMax * clamp(Shadow.radius[3-c], 0, 1)`
//      (`0x20C38`), clamped on BOTH sides (`0x20C14`-`0x20C28`) -- unlike the
//      material's own blur at `0x4A948`, which has a ceiling and no floor;
//   4. a RING clip when `Shadow.ringWidth` is non-nil -- read end to end by this
//      front, see below;
//   5. `drawDisplayList:` (`0x20CF4`).
//
// THE RING, WHICH IS A RING AFTER ALL
// -----------------------------------
// The laudo (§8, open item 5) left the clip's GEOMETRY untranscribed: it had the
// width read, scaled and negated at `0x20C8C` and handed to `0x11C40`, and
// stopped there. `0x11C40` is 1604 bytes and it is read out here.
//
// `[BIN]` **`0x11C40` IS `0x10E1C` WITH TWO FILTERS AROUND IT.** It takes the
// same fourteen arguments as `0x10E1C` -- the generic "draw this SDF into this
// rect" helper -- and forwards every one of them except three: the width
// `d5` becomes `1.0`, the colour pointer `x2` becomes `nil`, and the flag `w5`
// becomes `0` (`0x12144`-`0x1216C`). Between `save` (`0x11CB8`) and `restore`
// (`0x12174`) it installs exactly two filters, and nothing else:
//
//   A. `[BIN]` `addAlphaThresholdFilterWithMinAlpha:maxAlpha:color:colorSpace:`
//      (`0x11D64`), `colorSpace = 3`, colour = the four `Double` at the
//      lazily-initialised global `0xCDE70`, whose `swift_once` initialiser
//      (`0x3DAA0`) writes `fmov v0.2d, #1.0` twice: **opaque white**.
//   B. `[BIN]` `addColorMatrixFilterWithArray:flags:0` (`0x12138`) over twenty
//      `Float` assembled from `0xCE900`. Nineteen of them are ZERO; the only
//      non-zero is flat index 15, which is `1.0`. In a 4x5 matrix over
//      `(r,g,b,a,1)` that is `out.rgb = 0, out.a = in.r` -- the move that turns
//      a white-on-transparent premultiplied ramp into a pure alpha mask, which
//      is what `clipLayerWithAlpha:mode:` at `0x20CE8` then consumes.
//
// So the "shape" is not a path. `[BIN]` The mask is the LAYER'S OWN SIGNED
// DISTANCE FIELD -- `FinalizedIcon.Layer.sdf` (`IconRendering.SDF`, the
// `{texture, maxDistance: Double}` pair the reflection lists at `0xA2FF4`,
// occupying `Layer+0x98..0xB0`) -- run through a BAND PASS of its alpha. The
// band is computed at `0x11CF8`-`0x11D3C`:
//
//     u        = -(ringWidth[3-c] * s) * (sdfTexelsW - 2) / rectW   // texels
//     minAlpha = min( u/(-2*maxDistance) + 0.5 , 0 /(-2*maxDistance) + 0.5 )
//     maxAlpha = max( ... )                                         // = the pair
//
// with `u <= 0`, so `minAlpha = 0.5` and `maxAlpha = 0.5 + w/(2*maxDistance)`,
// `w` being the ring width in SDF texels.
//
// `[BIN]` **AND THE FILTER IS A BAND PASS.** That is read out of
// `RenderBox.arm64`, which unlike `IconRendering` KEEPS its symbols:
// `RB::_GLOBAL__N_1::render_(AlphaThresholdEffect const&, ...)` at `0x893A4`
// computes, into the shader globals at `+0x44`/`+0x48`,
//
//     scale = 1 / (maxAlpha - minAlpha)        (`0x89400`-`0x89410`)
//     bias  = -minAlpha * scale                (`0x89414`-`0x89424`)
//
// i.e. `t = (alpha - minAlpha) / (maxAlpha - minAlpha)`. Until 2026-10-01 this
// header stopped there and concluded "one scale and one bias is monotone, so it
// is not a crown". The scale and the bias are only `t`; what the FRAGMENT does
// with `t` was not read, and it is this (`alpha_effect`, `default_mod66.ll`
// %46-%61):
//
//     a = saturate(t / fwidth(t) + 0.5)
//     b = saturate((t - 1) / fwidth(t) + 0.5)
//     out = colour * (a - a * b)               state bits 9-11 clear
//
// one for `0 <= t <= 1` and zero on BOTH sides, antialiased over a pixel. The
// single-step variant (`out = colour * a`, bit 9) is selected only when
// `maxAlpha` is +infinity (`0x8952C`-`0x89548`) -- the one-argument
// `addAlphaThresholdFilterWithAlpha:` -- and `0x11D08`-`0x11D64` hands a finite
// one. **It is a crown between two contours.**
//
// `[BIN]` THE SIGN, which decides inward from outward, comes from the same
// binary. `-[RBDisplayList addDistanceFilterWithMaxDistance:scale:flags:]`
// (`0x3F354`) builds an `RBDisplayListDistanceFilter` whose first two fields are
// the ones its sibling `addDistanceFilterWithZeroDistance:oneDistance:scale:`
// (`0x3F390`) takes in that order, and it fills them `zeroDistance = +maxDistance,
// oneDistance = -maxDistance` (`fneg d2, d0` at `0x3F360`, `stp d0, d2` at
// `0x3F368`). Alpha therefore RISES as the signed distance falls: `0` at
// `+maxDistance` (outside), `0.5` on the contour, `1` at `-maxDistance` (inside)
// -- which is also the only encoding under which dividing by `2 * maxDistance`
// is the right conversion. So `alpha > 0.5` is INSIDE, and the band
// `[0.5, 0.5 + w/(2*maxDistance)]` is the band of depths `[0, w]` INSIDE the
// silhouette.
//
// THE WHOLE THING COLLAPSES, AND `maxDistance` CANCELS:
//
//     t(p)    = depthInside(p) / (ringWidth[3-c] * s)
//     mask(p) = 1 for 0 <= t <= 1, 0 elsewhere
//
// one from the layer's own outline to `ringWidth` points inside it, zero deeper
// and zero outside, each edge antialiased over a pixel. The shadow source is
// the RIM of the silhouette, not its body. `shadowRingMask` is that line.
//
// `[BIN]` Two guards precede it, and the laudo named only one of them: the clip
// is skipped when `Shadow.ringWidth` is `nil` (`ldrb w8,[x27,#0x30]; cmp #1` at
// `0x20C48`) AND when the low byte of the SDF's second word is `0xFF`
// (`mvn w8,w25; tst x8,#0xff` at `0x20C3C`) -- the empty case of the layer's own
// SDF. A layer with no distance field gets no ring.
//
// `[INF]` THE CLIP RUNS BEFORE THE BLUR, and that is a reading and not a
// measurement. `beginLayer`/`clipLayerWithAlpha:` installs the mask as a CLIP on
// the drawing state that `drawDisplayList:` then draws the art into, while the
// blur added at `0x20C38` is a FILTER on that same state's layer; a filter
// applies to the layer once composited, so the clip is inside it. The
// consequence is visible: clipping first lets the blur soften the feathered edge,
// clipping afterwards would leave the shadow hard-edged along the silhouette.
//
// and is then composited (§5.2) as `drawShape:fill:alpha:blendMode:` at
// `0x4A20C`: a rect filled with that image under a `tintColor:`, at the alpha
// above, under `Shadow.blendMode` -- `2 == multiply` by default.
//
// TRANSLATE-THEN-BLUR IS NOT AN ORDERING CHOICE. A Gaussian is shift invariant,
// so steps 2 and 3 commute exactly; this file blurs first only because blurring
// the untranslated art keeps the sampling on the art's own grid.
//
// FOUR THINGS `shadowStyle` SWITCHES, AND THREE OF THEM CHANGE THE PICTURE
// -------------------------------------------------------------------------
// `[BIN]` §4. `none` returns without drawing (`0x49F74`); the style picks the
// opacity TABLE (`0x4A248`, and `automatic` groups with `vibrant`); it picks the
// COLOUR -- white `(1,1,1,1)` at `0xCDE70` for vibrant, black `(0,0,0,1)` at
// `0xCDE98` for neutral -- and it picks the blend byte. A white tint is the
// identity, which is what `GlassMaterial::shadowInfusesGlyphColor` means in
// pixels: the vibrant shadow keeps the glyph's colours, dimmed to
// `vibrantBrightness`; the neutral one is a black silhouette.
//
// THE GATE ABOVE ALL OF IT, AND WHY THIS FILE IS ALLOWED TO IGNORE IT
// --------------------------------------------------------------------
// `[BIN]` `0x49F40`-`0x49F70`: if `[ctx+0x510] != 1`, or if any of five
// `Double` at `ctx+0x4E8..0x508` is non-zero, the shadow is NEUTRAL no matter
// what the document said. `[OBS]` The laudo could not name those six -- they
// have the same shape as the recolouring gate the specular consults at
// `0x49280` -- but it did read what they DO. This renderer draws a document as
// authored, with no recolouring state, which is the identity case where the
// gate falls through to the style. `shadowIsForcedNeutral` exists so that the
// reading is in the code rather than only in this comment.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Source/RenderBox/BlendMode.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassTranslucency.h"

namespace rb {

// `[BIN]` `ICRRenderingParameters.Shadow` -- the 15-field sub-struct at
// `params + 0x2B0`, `0xF8` bytes, whose layout the laudo read from two
// independent places that agree byte for byte: the `Shadow == Shadow` equality
// at `0x6DFE4` and the field-by-field copy at `0x4EABC`-`0x4EBB4`. The order
// matches the 15 `CodingKeys` at `0xA5064` exactly -- no field spare, none
// missing -- and the four `SizeBasedValue` are named as such by their accessors'
// mangled symbols (`0xE2E4F`, `0xE2E25`, `0xE31A3`, `0xE32B7`), so the types are
// READ and not deduced from size.
//
// Every default below is `[BIN]`, from the aggregate constructor embedded in the
// `ICRRenderingParameters` one at `0x5EC40`-`0x5ECDC`.
//
// Ten of these fifteen are carried and not consumed by anything in this file.
// They are here for the same reason `TranslucencyEffect` carries its two unread
// booleans: a reader who found five fields where the binary has fifteen would
// think the layout had been mismeasured.
struct ShadowParameters {
    // `[BIN]` `0.0` and `32.0`, from the pool at `0x98620` (`0x5EC54`). The
    // offset is DOWN only, and 32 is a thirty-second of the canvas.
    double offsetX = 0.0;
    double offsetY = 32.0;

    // `[BIN]` `SizeBasedValue<Double>?` -- an OPTIONAL, and the `Sg` of its
    // mangled accessor (`0xE32B7`) is the same tag byte the laudo measured at
    // `Shadow+0x30`. Present by default, all four slots `16` (`0x5EC58`).
    //
    // `[OBS]` Consumed by NOTHING here: see `kShadowRingNote`.
    std::optional<SizeBasedValue> ringWidth = SizeBasedValue{{16.0, 16.0, 16.0, 16.0}};

    // `[BIN]` `[0.3, 0.3, 0.3, 0.3]` (`0x5EC68`). The blur's fraction, not a
    // length: it is multiplied by `blurStrengthMax` to become points.
    SizeBasedValue radius{{0.3, 0.3, 0.3, 0.3}};

    // `[BIN]` `[0.75 x4]` (`0x5EC7C`) and `[0.375 x4]` (`0x5EC88`). These two are
    // the table the style chooses between, and the 0.375 is the number
    // Apollo-Reborn's `2.4` was authored against.
    SizeBasedValue vibrantOpacity{{0.75, 0.75, 0.75, 0.75}};
    SizeBasedValue neutralOpacity{{0.375, 0.375, 0.375, 0.375}};

    // `[BIN]` `2 = multiply`, `0 = normal`, `2 = multiply`, written by the
    // `strh w24` at `0x5EC94` with `w24 == 2` and the byte at `0x5EC98`. These
    // are `Icon.BlendMode` indices -- the format's eighteen (§17.3) -- and NOT
    // RenderBox's fifty-six; the `overdrawBlendMode` accessor's mangled type
    // (`0xE2F53`) says `Icon.BlendMode` outright, which is what settles it.
    BlendMode blendMode = BlendMode::Multiply;
    BlendMode blendModeForVibrantOnDim = BlendMode::Normal;
    BlendMode overdrawBlendMode = BlendMode::Multiply;

    // `[BIN]` `0.75` (`0x5EC9C`). The grey the VIBRANT branch multiplies the
    // glyph's own colour by before blurring.
    //
    // ONE CONSUMER, AND NOW BY SWEEP RATHER THAN BY ABSENCE OF SEARCH. The laudo
    // could only say it had found one. `[BIN]` A pass over all 142,694
    // instructions of `__text` finds every `ldr dN, [xM, #0xa0]` (ten) and every
    // `ldr dN, [xM, #0x350]` (six -- `Shadow` sits at `params+0x2B0`, so the
    // field has two spellings). Of the sixteen: `0x20BB4` is the `colorMultiply`
    // grey of §5.1; `0x6E024` and `0x6E044` are the two sides of the
    // `Shadow == Shadow` compare at `0x6DFE4`; `0x2E500`, `0x4E0D8`, `0x711C4`,
    // `0x713FC` and `0x81540` are each one line of a field-by-field COPY, every
    // one flanked by the neighbouring offsets `+0x348` and `+0x358`; and the
    // remaining eight belong to other structs (`0xFD8C`'s base indexes a byte at
    // `+0x469F` and puts a four-slot table at `+0x330`, which would fall inside
    // `neutralOpacity` if the base were the parameters). **No second arithmetic.**
    double vibrantBrightness = 0.75;

    // `[BIN]` both `true`, from the `strh w25` at `0x5ECA4` with `w25 == 0x0101`.
    //
    // `ignoreFillOpacity` is consulted NOWHERE in `0x49ED4`. The laudo held that
    // against the reading of `[descriptor+0x38]`; it should not have, and the
    // header says why -- this field belongs to a parameters struct and that one
    // is a `FinalizedIcon.Layer`'s own opacity.
    //
    // `[BIN]` WHAT DOES READ IT was found on 2026-10-01, and it is the FINALISER,
    // not the compositor: building the shadow's source list (`0x1C0DC`), it
    // rewrites every fill's alpha to 1.0 when this is set (`0x1C260`,
    // `0x1C3E8`-`0x1C430`). So it is about the element's FILL, not about any
    // opacity, and it asks for a second render of the art -- which
    // `IconRenderer.cpp` makes, for an element whose fill is translucent, and
    // casts the shadow from. Generation 26 clears it (`0x77ED0`).
    bool ignoreFillOpacity = true;
    bool drawOverContent = true;

    // `[BIN]` `0.3` (pool `0x98630`, `0x5ECAC`), `[0.2 x4]`, `[0.5 x4]` (the
    // fourth of the last from `0x5ECD4`, the pair from the `fmov v3.2d, #0.5` at
    // `0x5E928`).
    //
    // `[OBS]` These three drive the OVERDRAW pass (laudo §5.3) --
    // `t = clamp01(translucency / translucencyForMaxOverdraw)`, `alpha = t *
    // max<Neutral|Vibrant>OverdrawOpacity[3-c]`, fed to `clipLayerWithAlpha:`
    // at `0x45FF4`. That is a SECOND consumer of `translucency`, distinct from
    // the mask in `GlassTranslucency.h`, and this file does not implement it:
    // see `kShadowOverdrawNote`.
    double translucencyForMaxOverdraw = 0.3;
    SizeBasedValue maxNeutralOverdrawOpacity{{0.2, 0.2, 0.2, 0.2}};
    SizeBasedValue maxVibrantOverdrawOpacity{{0.5, 0.5, 0.5, 0.5}};
};

// The block as this version's binary initialises it: GENERATION 27's. The icon
// renderer does not draw with it -- every step it runs takes the `shadow` of the
// generation's block (`RenderingParameters.h`), and generation 26 rewrites
// twelve of the fifteen fields (`0x77E80`-`0x77ED0`). This is the default of the
// functions below for a caller that has no block: the tests of one formula.
inline constexpr ShadowParameters kShadow{};

// `[BIN]` `0x4A248`: `none` returns, `neutral` takes the neutral table, and
// `automatic` and `vibrant` BOTH take the vibrant one. The grouping of
// `automatic` with `vibrant` is the branch's own shape (`0x4A254` and `0x4A260`
// both fall into `w8 = 0`), not a default chosen here.
//
// `[ART]` No corpus document spells `automatic` -- 302 resolutions are
// `neutral` 190, `layer-color` 87, `none` 25 -- so this grouping costs nothing
// today and is transcribed because the binary does it.
bool shadowUsesVibrantTable(ShadowStyle style);

// `[BIN]` The gate of `0x49F40`-`0x49F70`, written down so the reading survives
// outside a comment. `recolouring` is the state this renderer never enters: it
// draws the document as authored. Passing `true` forces `neutral` no matter what
// the style says, which is what the binary does.
//
// `[OBS]` The six context fields the gate actually reads were not named by the
// laudo. This is their EFFECT, parameterised, not their identity.
ShadowStyle shadowEffectiveStyle(ShadowStyle style, bool recolouring);

// Everything the alpha needs, in one place, so the three factors are visible
// together rather than spread over a call's argument list.
struct ShadowInputs {
    ShadowStyle style = ShadowStyle::Automatic;

    // `[BIN]` `Icon.GlassMaterial+0x08`, `ldr d8, [x0, #0x08]` at `0x49F0C`. Raw:
    // the document's number, unclamped and undenormalised.
    double shadowOpacity = 0.0;

    // `[BIN]` `FinalizedIcon.Layer.opacity` -- `ldr d9, [x0, #0x38]` at
    // `0x49F10`, named by the reflection metadata at `0xA301C`, placed by the
    // field-offset vector at `0xBD3F0`, and written at `0x17038` from
    // `Icon.Layer.opacity`. See the header for all three.
    //
    // IT IS THE GROUP'S OPACITY -- an `Icon.Layer` is a document group; the
    // header says how that was settled on 2026-10-01. The same number multiplies
    // the group's content draw (`0x4B4EC`) and every highlight (`0x495F0`), so it
    // is applied once per pass and not once per group, and halving it in any of
    // them would be a compensation for an arithmetic the binary does not
    // perform. Where the shadow's source is one element's own art,
    // `IconRenderer.cpp` multiplies that element's opacity in here as well: the
    // target has it inside the source, and the chain between the two is linear.
    //
    // `[OBS]` The DEFAULT was not found. `Icon.Layer.opacity` is copied verbatim
    // and its initialiser is not materialised in this slice -- a sweep of all
    // 117 `fmov dN, #1.0` sites in `__text` finds no store into a `Layer`, and
    // there is no `Icon.Layer.<anon>.CodingKeys` to carry a `?? 1.0`. `1.0` is
    // the multiplicative identity and is what this renderer uses when a document
    // group names no `opacity`, but that is the DOCUMENT's rule (`numberOr(...,
    // 1.0)`), not a value read out of the binary.
    double layerOpacity = 1.0;

    IconSizeClass sizeClass = IconSizeClass::Large;
};

// `[BIN]` `alpha = shadowOpacity * Opacity[3 - sizeClass] * layerOpacity`, the
// `fmul` pair at `0x4A06C`/`0x4A070`, in that order.
//
// NOT CLAMPED, ON PURPOSE. `2.4` goes through as `2.4` and comes out as `0.9`
// against the neutral table; a negative would come out negative. The clamping
// happens nowhere in the binary and it happens nowhere here -- the compositor's
// own arithmetic is where a value above 1 stops being meaningful, and that is a
// different place with a different answer. `[ART]` Three corpus resolutions
// depend on this.
//
// `none` answers `0.0`: the binary returns before the multiplications, and a
// zero alpha is the same picture by a route the caller does not have to special
// case. `shadowDraws` is the predicate for the branch that matters.
double shadowAlpha(const ShadowInputs& in, const ShadowParameters& p = kShadow);

// Whether anything is composited at all: `style != none` AND a positive alpha.
//
// A zero alpha is NOT a gap and must not be reported as one. `[ART]` 25 corpus
// resolutions are `none` and many more carry `opacity: 0` -- "the document asked
// for no shadow" and "the shadow is not implemented" have to stay
// distinguishable, which is the same rule `glassRefracted` follows for a
// zero-strength refraction.
bool shadowDraws(const ShadowInputs& in, const ShadowParameters& p = kShadow);

// `[BIN]` §4(d). `base = (recolouringDim ? blendModeForVibrantOnDim : blendMode)`
// on the vibrant branch, `blendMode` otherwise; the overdraw pass substitutes
// `overdrawBlendMode` (`tst w1, #1` at `0x49F94` / `0x4A004`), which
// `IconRenderer.cpp` passes itself when it composites that pass.
//
// `[BIN]` `recolouringDim` is `ctx+0x463 == 2` (`0x49FF4`-`0x4A000`), and the
// byte has a name since 2026-10-01: the configuration the two highlight
// selectors are handed is `ctx+0x408` (`0x475D8`, `0x492D0`) and they read
// `iconBrightness` at its `+0x5B` -- the same byte. `2` is `dim`. So the VIBRANT
// shadow of an icon whose chiclet classified as dim takes
// `blendModeForVibrantOnDim`: `normal` in generation 27, where every other
// shadow is `multiply`; `plusDarker` like the rest in generation 26. The
// neutral branch (`0x49F94`) never reads the byte. `IconRenderer.cpp` passes
// the class the chiclet's fill resolved to.
BlendMode shadowBlendMode(ShadowStyle style, bool recolouringDim = false,
                          const ShadowParameters& p = kShadow);

// ---- the geometry --------------------------------------------------------

// The lengths of §5.1, resolved onto a square target of `size` PIXELS.
//
// `[BIN]` The scale is `s = min(width, height) / 1024` (`0x20B88`), so on a
// square target it is exactly the `size / kCanvasPoints` ratio this renderer
// already uses everywhere else. Everything below is therefore TARGET PIXELS, not
// canvas points: at `size == 1024` and the read defaults the shadow sits 32 px
// down under a 19.2 px blur radius.
struct ShadowGeometry {
    double offsetX = 0.0;
    double offsetY = 0.0;

    // `[BIN]` `s * blurStrengthMax * clamp(radius[3-c], 0, 1)` -- and the clamp
    // is on BOTH sides (`0x20C14`-`0x20C28`), which is the third distinct
    // clamping shape in this material and therefore information: the refraction
    // height clamps both sides, the material's blur has a ceiling only, and this
    // one clamps both. `GlassMaterial.h` states the other two.
    double blurRadius = 0.0;

    // `[BIN]` Present when `Shadow.ringWidth` is non-nil (tag at `Shadow+0x30`,
    // tested at `0x20C48`), as `s * ringWidth[3-c]`; the binary NEGATES it at
    // `0x20C8C` because the threshold band it feeds is expressed against a
    // distance that grows OUTWARD. In this struct it is the positive depth, in
    // the same target pixels as `blurRadius`: the band runs from the silhouette
    // to `ringWidth` pixels inside it. See the header.
    std::optional<double> ringWidth;
};

ShadowGeometry shadowGeometry(std::uint32_t size, IconSizeClass sizeClass,
                              const ShadowParameters& p = kShadow,
                              const GlassRenderingParameters& g = GlassRenderingParameters{});

// `[BIN]` THE KERNEL BEHIND `addBlurFilterWithRadius:` IS NOW READ, and this
// number stopped being the one convention in the file. **THE RADIUS IS THE
// SIGMA.** `Source/RenderBox/BlurKernel.h` carries the whole reading -- the GPU
// chain that stores `radius^2` as the filter's VARIANCE (`0xFED04`) and builds
// `exp(-x^2 / 2v)` from it (`0xFE378`), the CPU path that hands its radius
// straight to a textbook Gaussian (`0xBFDC0` -> `0xC35F4`), and the `2.8 * r`
// that both the bounds (`0xFEB58`) and that kernel's own truncation
// (`0x15ECF0`) agree on.
//
// Until 2026-09-15 this was `1.0 / 3.0`: the radius taken as a three-sigma
// support, chosen because it inverted the `ceil(3 * sigma)` truncation this
// repository's own Gaussian already wrote down. That guess was wrong by a factor
// of three and it was wrong in the narrow direction -- the very alternative its
// own comment named as "just as unread" is the one the binary does.
inline constexpr double kShadowBlurSigmaPerRadius = 1.0;

// STEP 4, as one scalar per texel in `[0, 1]`, to multiply into the art's alpha.
//
// `mask = 1 - clamp(depthInside - ringWidth + 0.5, 0, 1)` inside the silhouette,
// zero outside it, `depthInside` measured from the art's own silhouette -- the
// header reads that band out of `0x11C40`, `0x893A4`, the `alpha_effect`
// fragment and `0x3F354`, and shows why `SDF.maxDistance` cancels out of it.
//
// THE SILHOUETTE IS THE `alpha >= 0.5` CONTOUR of `art`, and the distance is an
// exact Euclidean transform over pixel centres (Felzenszwalb-Huttenlocher),
// less the half pixel that puts the contour between centres rather than on one.
//
// `[BIN]` OUTSIDE THE CANVAS COUNTS AS OUTSIDE THE SHAPE, which is not a default
// but a transcription: the target's SDF texture is one texel WIDER than the rect
// it covers on every side -- `0x10E1C` scales by `rectW / (sdfTexelsW - 2)` and
// translates by `(-1, -1)` (`0x10F48`-`0x10F6C`) -- so the field is padded and
// its border ring is empty. A shape that runs off the canvas gets feathered at
// the canvas edge, not carried through it.
//
// `[OBS]` The TEXEL GRID is not transcribed, and cannot be from these slices:
// the target samples a distance field generated by `sdfTextureWithBufferAllocator:`
// into a `TXRTexture`, which is neither in `IconRendering.arm64` nor in
// `RenderBox.arm64`. Two consequences, both named by `kShadowRingNote`: the
// field's resolution is unknown, so this transform is computed at the target's
// own resolution instead; and if `ringWidth` in texels ever exceeded that
// field's `maxDistance` the band would run off the encodable range and its
// inner edge would truncate, which is a saturation this cannot see.
std::vector<float> shadowRingMask(const std::vector<float>& art, std::uint32_t width,
                                  std::uint32_t height, double ringWidth);

// The shadow IMAGE: one layer's own art turned into what gets composited under
// it. `art` is STRAIGHT RGBA with its alpha in `.w`, the form `renderSvgPlaced`
// and `placeRaster` both produce, and the result has the same shape.
//
// `[BIN]` The ring clip, then colour, then blur, then translation -- steps 1 to
// 4 of §5.1 in the order the header argues for. The clip is skipped exactly when
// `geometry.ringWidth` is absent.
//
// The blur runs on PREMULTIPLIED values and the edges are CLAMPED, which is the
// same pair of choices `SvgFilter.cpp`'s Gaussian already makes and for the same
// reasons: averaging straight colour drags the colour of transparent pixels into
// visible ones, and an edge that read zero from outside would fade a shadow that
// runs off the canvas.
std::vector<float> shadowImage(const std::vector<float>& art, std::uint32_t width,
                               std::uint32_t height, ShadowStyle style,
                               const ShadowGeometry& geometry,
                               const ShadowParameters& p = kShadow);

// ---- what is drawn without having been read ------------------------------
//
// Each of these belongs in `RenderedIcon::notes`. They are not skip reasons: the
// shadow IS drawn. They exist because this tower would rather say what it did
// not draw than draw something wrong and keep quiet -- which is the whole reason
// `notes` and `shapeGaps` exist at all.

// THERE IS NO NOTE FOR THE THIRD FACTOR, and the absence is deliberate. This
// file was written with one -- a sentence saying that `[descriptor+0x38]` had no
// name and that the layer's `opacity` was being fed into it on a guess. The name
// was then read (see the header), so the sentence went. A `notes` entry earns
// its place by naming a gap that is still open; keeping one after the gap closes
// trains a reader to skip the list, which costs the notes that are still true.

// The ring is now DRAWN, and this note no longer says it is missing. What it
// says is the one thing under it that is still convention: the band's arithmetic
// is measured, the distance field it samples is generated in a binary this
// project does not have. It fires only when a ring is actually applied.
//
// It is the LAST note of this shape in the file. `kShadowBlurKernelNote` was
// the other one and it is gone (see below), which makes this the single place
// where the shadow still rests on something unread.
extern const char* const kShadowRingNote;

// THERE IS NO NOTE FOR THE BLUR KERNEL EITHER, AND IT IS THE SAME RULE. This
// file shipped one -- `kShadowBlurKernelNote`, fired on every render that
// blurred, saying the radius was measured and the kernel it fed was not. The
// kernel was then read (`Source/RenderBox/BlurKernel.h`), so the note went, by
// the paragraph above. Deleting it is the point of that paragraph: a list that
// keeps closed gaps is a list readers stop reading.

// The overdraw pass IS NOW DRAWN, and this note no longer says it is missing.
//
// `[BIN]` ITS GATE IS READ TOO, and the paragraph that stood here until
// 2026-10-01 was wrong about it. The byte at `[descriptor+0x31]`, which
// `0x45F10` (`ldrb w8, [x24, #0xf1]`, descriptor base `x24+0xC0`) and `0x4ADBC`
// require to be ZERO before the pass opens, is not "a kind tag with no name":
// the header above already places `FinalizedIcon.Layer.blendMode` at `+0x31`,
// and that is the GROUP's `blend-mode`, copied verbatim by the finaliser
// (`0x19934`-`0x19940`). Zero is `normal`. So the pass runs only over a group
// that blends normally, and `0x4B518` (`cmp w24, #8`) is the content draw
// dispatching on the same mode. `[ART]` A `.icon` DOES select it: 18 corpus
// groups carry a non-normal blend beside glass. `IconRenderer.cpp` applies the
// gate.
//
// What the note still carries is what is left open under the pass: that the
// clip's float multiplies coverage (the `[INF]` below), and whether the
// translucency clip the pass sits under reaches the shadow once or twice.
//
// It fires only when the pass actually runs, for the same reason
// `kShadowRingNote` does.
extern const char* const kShadowOverdrawNote;

// WHAT THE SHADOW IS CAST FROM, where this renderer's source is not the
// target's. `[BIN]` The source is a display list of its own (`0x1C0DC`): the
// group's glass elements -- or all of them when `Shadow.ringWidth` is nil
// (`0x18338`-`0x18354`) -- drawn with their real opacity and blend. One thing
// about it is not reproduced, and the note fires only for a group where it
// bites:
//
//   * the ring is cut from the group's SDF, whose silhouettes are drawn at
//     opacity 1 (`0x1CB90`). Here it is cut from the `alpha >= 0.5` contour of
//     the source itself, which is the same thing for one element and is not for
//     a source flattened from several when one of them is translucent.
//
// The other thing this note named until 2026-10-01 IS reproduced now:
// `Shadow.ignoreFillOpacity` rewrites every fill's alpha to 1.0 in that list
// (`0x1C260`, `0x1C3E8`-`0x1C430`), so a translucent fill casts the shadow of
// an opaque one, and `IconRenderer.cpp` draws that second render of the art.
// `[OBS]` A fill does not reach raster art in this renderer at all
// (`kRasterFillNote`), so there is nothing to rewrite on one.
extern const char* const kShadowSourceNote;

// `[BIN]` The overdraw clip's alpha: `t = clamp01(translucency /
// translucencyForMaxOverdraw)`, `alpha = t * max<...>OverdrawOpacity[3-c]`,
// `0x45F18`-`0x45F94` and identically `0x4ADD0`-`0x4AE44`. Returns `0` when
// nothing would be drawn, which is the binary's own `alpha <= 0` exit.
//
// `[BIN]` THE TWO TABLES, RE-READ FOR THIS FRONT out of the same constructor as
// the rest of the block: `ldr q0, [0x98630]` at `0x5ECAC` is the pair
// `(0.3, 0.2)` -- `translucencyForMaxOverdraw` and the FIRST slot of
// `maxNeutralOverdrawOpacity` -- the `dup v1.2d` of `0x5ECB0`-`0x5ECBC` spells
// `0.2` by `mov`+`movk` (`0x3FC999999999999A`, which is why it is NOT in the
// pool) into slots 1 and 2, and `ldr q1, [0x98640]` at `0x5ECC8` carries slot 3
// (`0.2`) and the first slot of `maxVibrantOverdrawOpacity` (`0.5`); the middle
// pair of the vibrant table is the `fmov v3.2d, #0.5` of `0x5E928` parked at
// `[sp,#0x20]` by `0x5E938`, and the fourth is the `mov x9, #0x3FE0...` of
// `0x5ECD4`. So both tables are FLAT -- `[0.2 x4]` and `[0.5 x4]` -- which is
// exactly why the `[3 - c]` inversion is silent here and has to be routed
// through `sizeBasedValue` on principle rather than on evidence.
double shadowOverdrawAlpha(double translucency, ShadowStyle style, IconSizeClass sizeClass,
                           const ShadowParameters& p = kShadow);

// THE OVERDRAW PASS ITSELF -- the second composite, as one image ready to be
// blended with `overdrawBlendMode` and the SAME `shadowAlpha` as the first.
//
// `[BIN]` WHAT IT DRAWS IS THE SAME SHADOW, NOT A SECOND EFFECT. The pass is a
// second call to the very function that composites the main shadow -- `0x49ED4`,
// at `0x46034` and `0x4AEB4` -- with the same descriptor, so the same image
// (`[descriptor+0xB0]`, fetched through `0x85ED8` at `0x4A18C`), the same rect
// (`setRect:` at `0x4A1AC`), the same tint and the same three-factor alpha
// (`0x4A06C`/`0x4A070`). The ONLY thing the second call changes inside that
// function is `w1`: `tst w1, #1` at `0x49F94` and `0x4A004` substitutes
// `Shadow.overdrawBlendMode` for the blend byte the first call used. Everything
// that makes it an "overdraw" is OUTSIDE the function, in the clip.
//
// `[BIN]` AND THE CLIP IS THE ELEMENT'S OWN CONTENT. Around the second call the
// target does `beginLayer` (`0x45FA8` / `0x4AE90`), draws `0x4B4EC` into that
// layer (`0x45FE4` / `0x4AE98`), and closes it with
// `clipLayerWithAlpha: alpha mode: 0` (`0x45FF4` / `0x4AEA8`). `0x4B4EC` is not
// a mask-builder: it is THE CONTENT DRAW. The content pass one statement earlier
// (`0x4ADA4` -> `0x4AF20`) ends by tail-calling the same `0x4B4EC` with the same
// descriptor (`0x4B3EC`, `mov x0, x22`), having only optionally installed a
// colour-matrix filter first (`0x4B3E4`, and `0x4AF78` jumps straight past it).
// So the clip's coverage is the glyph's own, at the constant `clipAlpha`.
//
// That is why `drawOverContent` is the field's name and why the pass cannot be
// folded into the main one: the main shadow draws UNDER the art, unclipped; this
// one draws the identical image again, ON TOP of the art and clipped to it.
//
// `[BIN]` `mode: 0` IS `ClipMode::normal`, AND THE OLD READING OF IT IS WRONG.
// `_RBDrawingStateClipLayer` (`RenderBox.arm64` `0x3BB64`) passes that argument
// through the function at `0x8B624` before handing it to
// `DisplayList::Builder::clip_layer(Layer*, State&, float, ClipMode)`
// (`0xC9EFC`). `0x8B624` carries TWO mangled names -- `RB::aliasing_mode(RB::RenderingMode)`
// and `rb_clip_mode(RBClipMode)` -- because identical-code folding merged them:
// the body is three instructions, `cset w0, (w0 == 1)`. The parameter type at
// the call site is `ClipMode`, so the second name is the one that applies, and
// `RB::XML::Value::ClipMode::to_string` (`0x130BB0`) bounds that enum at TWO
// values and names them from the table at `0x18F8A8`: **`normal`** and
// **`inverse`**. `mode: 0` therefore means "clip normally", not "clip by alpha"
// (the `[INF]` the VCM laudo carried) and not "antialiased" (the `[INF]`
// `kShadowRingNote`'s laudo carried, which this DERRUBA). Both earlier readings
// happened to reach the right pixel; the reason they gave was not the binary's.
//
// `[INF]` That the float multiplies the clip's COVERAGE is still inference, from
// three sides: the selector is spelled `clipLayerWithAlpha:`; `clip_layer` frees
// the layer outright when the float is `0` and the layer is non-trivial
// (`0xC9F44`-`0xC9F50`); and every `make_clip` overload in `RenderBox.arm64`
// takes `(Builder&, float, ClipMode, vector<Clip*>&)` in that order, i.e. the
// float travels with the coverage and not with the fill.
//
// `shadow` is the image `shadowImage` already produced; `content` is the art as
// it was composited, STRAIGHT RGBA. The result is `shadow` with its alpha
// multiplied by `content`'s alpha and by `clipAlpha`.
std::vector<float> shadowOverdrawImage(const std::vector<float>& shadow,
                                       const std::vector<float>& content,
                                       std::uint32_t width, std::uint32_t height,
                                       double clipAlpha);

}  // namespace rb
