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
//   4. a RING clip when `Shadow.ringWidth` is non-nil -- NOT implemented, see
//      `kShadowRingNote`;
//   5. `drawDisplayList:` (`0x20CF4`).
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
    double vibrantBrightness = 0.75;

    // `[BIN]` both `true`, from the `strh w25` at `0x5ECA4` with `w25 == 0x0101`.
    //
    // `[OBS]` `ignoreFillOpacity` is consulted NOWHERE in `0x49ED4`. The laudo
    // held that against the reading of `[descriptor+0x38]`; it should not have,
    // and the header says why -- this field belongs to a parameters struct and
    // that one is a `FinalizedIcon.Layer`'s own opacity. So the open question
    // this field raises is smaller than it looked and it is still open: what
    // DOES read it. Carried, not consumed.
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

// The block as this version's binary initialises it.
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
    // THE SAME NUMBER MULTIPLIES THE ELEMENT'S OWN DRAW (`0x495F0`), so it is
    // applied TWICE per layer and not once: once here, on the shadow, and once
    // on the art. `IconRenderer.cpp` already passed `opacity` to `blendOver` for
    // the art before this file existed; feeding it here as well is what makes
    // the two agree, and halving it in either place would be a compensation for
    // an arithmetic the binary does not perform.
    //
    // `[OBS]` The DEFAULT was not found. `Icon.Layer.opacity` is copied verbatim
    // and its initialiser is not materialised in this slice -- a sweep of all
    // 117 `fmov dN, #1.0` sites in `__text` finds no store into a `Layer`, and
    // there is no `Icon.Layer.<anon>.CodingKeys` to carry a `?? 1.0`. `1.0` is
    // the multiplicative identity and is what this renderer uses when a document
    // layer names no `opacity`, but that is the DOCUMENT's rule (`numberOr(...,
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
// `overdrawBlendMode`, and that pass is not implemented here.
//
// `[OBS]` `recolouringDim` is `ctx+0x463 == 2`, one of the unnamed gate bytes.
// This renderer is always in the identity state, so it always reads `blendMode`
// -- `multiply`. Parameterised for the same reason `shadowEffectiveStyle` is.
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
    // `0x20C8C` to inset a clip shape.
    //
    // `[OBS]` Carried and NOT applied. The width is read; the SHAPE it insets --
    // built through `0x11C40` and handed to `clipLayerWithAlpha:mode:` at
    // `0x20CE8` -- was not transcribed (laudo §8, open item 5). Drawing an
    // invented ring would be exactly the plausible-and-wrong this tower refuses,
    // and not drawing it silently would be worse: `kShadowRingNote` says it.
    std::optional<double> ringWidth;
};

ShadowGeometry shadowGeometry(std::uint32_t size, IconSizeClass sizeClass,
                              const ShadowParameters& p = kShadow,
                              const GlassRenderingParameters& g = GlassRenderingParameters{});

// `[OBS]` THE KERNEL BEHIND `addBlurFilterWithRadius:` WAS NOT READ, and this is
// the one number in this file that is a convention rather than a measurement.
// What is measured is the RADIUS -- the argument, in points, and the arithmetic
// that produces it. What a RenderBox blur builds from that argument lives in a
// binary this project has not decoded, exactly as `GlassMaterial.h` already says
// for the material's own `addBlurFilterWithRadius:opaque:`.
//
// So the radius is taken as the Gaussian's three-sigma support, `sigma =
// radius / 3`. That is not a new convention: it is the INVERSE of the one this
// repository's own separable Gaussian already uses, which truncates its taps at
// `ceil(3 * sigma)` (`SvgFilter.cpp`). Choosing `sigma = radius` instead would
// widen the shadow roughly threefold and is just as unread; picking the value
// that round-trips through a rule already written down is the only thing here
// that is better than a coin toss, and `kShadowBlurKernelNote` says so on every
// render that blurs.
inline constexpr double kShadowBlurSigmaPerRadius = 1.0 / 3.0;

// The shadow IMAGE: one layer's own art turned into what gets composited under
// it. `art` is STRAIGHT RGBA with its alpha in `.w`, the form `renderSvgPlaced`
// and `placeRaster` both produce, and the result has the same shape.
//
// `[BIN]` Colour, then translation, then blur -- steps 1 to 3 of §5.1. The ring
// (step 4) is not applied; `geometry.ringWidth` carries it for the caller to
// report.
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

// `Shadow.ringWidth` is non-nil by default, so by default this note ALWAYS
// fires when a shadow draws. That is correct and it is the point: with the read
// parameters the target clips its shadow with a ring this renderer does not
// build, so every shadow drawn here is short of one step.
extern const char* const kShadowRingNote;

// The blur radius is measured; the kernel it feeds is not.
extern const char* const kShadowBlurKernelNote;

// The overdraw pass of §5.3, which is a whole second composite this file does
// not perform. Said only when the group's `translucency` would actually open it,
// so it reports a gap in THIS document rather than restating the rule.
extern const char* const kShadowOverdrawNote;

// `[BIN]` The overdraw clip's alpha, transcribed so the note above can say
// whether this document would have opened it: `t = clamp01(translucency /
// translucencyForMaxOverdraw)`, `alpha = t * max<...>OverdrawOpacity[3-c]`,
// `0x45F18`-`0x45F94` and identically `0x4ADD0`-`0x4AE44`. Returns `0` when
// nothing would be drawn, which is the binary's own `alpha <= 0` exit.
double shadowOverdrawAlpha(double translucency, ShadowStyle style, IconSizeClass sizeClass,
                           const ShadowParameters& p = kShadow);

}  // namespace rb
