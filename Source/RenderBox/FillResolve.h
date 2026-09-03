#pragma once
// `Fill` resolution: the document's seven cases, and the TWO converters that
// read them differently.
//
// THE FINDING, AND IT IS THE WHOLE FILE
// -------------------------------------
// `[BIN]` `IconComposerKit.arm64` imports the constructors of `Icon.Fill` and
// every call site of them lives in exactly two functions, whose bounds
// `LC_FUNCTION_STARTS` gives exactly:
//
//   `0x10AD9C`-`0x10B080`   the BACKGROUND / chiclet fill. Its only caller
//                           descends into `Icon(name:chiclet:layers:...)`.
//   `0x10B7EC`-`0x10C448`   the LAYER fill. It calls `Layer.fill`,
//                           `Layer.isGlass` and `Layer.frame`, and ends in
//                           `Icon.Element(contents:bounds:fill:...)`.
//
// The two read the same document type and answer differently, and the two
// cases they disagree on are the two a single shared converter would have got
// wrong:
//
//   `none`      the background takes the SAME PATH as `automatic`, byte for
//               byte; the layer answers `nil`. On the background, `none` does
//               not mean "do not draw".
//   `automatic` the background answers a system chiclet ramp -- or, under
//               `tinted`, a TRANSPARENT solid; the layer answers no colour at
//               all. It answers what this same layer's fill resolves at the
//               `light` specialization slot, which makes it an operator on
//               specializations rather than a colour.
//
// `[ART]` The corpus predicted the split before it was counted. Over the 145
// documents, `none` occurs 0 times on the background and 49 times on a layer,
// and `system-light`/`system-dark` occur 14+19 times on the background and 0
// times on a layer. Two clean 0/N divisions, both on the side the reading
// requires. `automatic` occurs 28 times on the background and 55 on a layer.
//
// WHAT THIS FILE DOES NOT DO
// --------------------------
// It resolves; it does not paint. The two system ramps and the alpha rewrite,
// `GradientPlacement.default` and the substitution it performs for a nil
// placement, and the derivation of the `automatic-gradient` ramp
// (`AutomaticGradient.h`) all live outside this file. Nothing here is wired
// into the renderer -- a later task makes that join, and until it does, a
// disagreement between this and what the renderer draws is expected.
//
// The `Icon.SystemFill` case and the `GradientPlacement` this answers with are
// `SystemFill.h`'s, reused rather than redeclared. Two structurally identical
// placements in one namespace would be an invitation to convert between them,
// and the ramp a `.system` answer names is the same enum that file resolves.
#include <cstdint>
#include <optional>
#include <string>

#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/RenderBox/SystemFill.h"

namespace rb {

// `[BIN]` The document's own case order, and it was not assumed: everything
// downstream hangs off the numbering, so it was read from an instruction that
// NAMES the cases -- `Fill.Kind.displayName`, `IconComposerFoundation.arm64`
// `0x0A8D40`, a branchless `csel` chain over Swift small strings.
//
//   0 none  1 automatic  2 solid  3 automaticGradient "Standard Gradient"
//   4 linearGradient "Custom Gradient"  5 systemLight  6 systemDark
//
// `icf::FillKind` already carries these seven in this order; this file adds no
// enum of its own for them and takes `icf::FillKind` as its input. The two
// display names in bold above are the reason the reading is worth having: the
// UI calls case 3 "Standard Gradient" and case 4 "Custom Gradient", so a
// transcription that matched cases to names by their spelling would have paired
// `automatic-gradient` with "Custom" and been silently one case out.

// `[BIN]` `Icon.Rendition`, and the mapping it exposes as `sourceAppearance` --
// `IconComposerFoundation 0x3D374`, four instructions indexing the packed byte
// table `0x0000'0303'0303'0201`.
//
// The table is the interesting half: `lightColor` and `darkColor` map to their
// own appearance, and ALL FOUR of `lightTint`, `darkTint`, `lightClear` and
// `darkClear` collapse onto `tinted`. Four renditions therefore reach the
// background converter's third arm, not one.
enum class Rendition : std::uint8_t {
    LightColor = 0,
    DarkColor = 1,
    LightTint = 2,
    DarkTint = 3,
    LightClear = 4,
    DarkClear = 5,
};

// `[BIN]` The byte table above, transcribed. `[OBS]` No rendition maps to
// `base`; `base` reaches the background switch only from a specialization slot
// built by something other than a rendition, and where that happens was not
// read.
icf::Appearance sourceAppearance(Rendition rendition);

// The four `.system(...)` construction sites of the two converters name the two
// `Icon.SystemFill` cases -- `.lightChicletGradient` at `0x10AF14` and
// `0x10C148`, `.darkChicletGradient` at `0x10AF10` and `0x10C260`. That enum is
// `rb::SystemFill`, whose two tag values are read in `SystemFill.h`; this file
// only says which site names which case.

// What a converter answers with: the target's `Icon.Fill.Contents`, restricted
// to the four cases these two functions actually construct.
//
// `[OBS]` `IconComposerKit` imports six `Icon.Fill` constructors and only these
// four are reached from the two converters. What the other two build, and who
// calls them, was not read.
struct ResolvedFill {
    enum class Contents : std::uint8_t {
        Solid,              // `.solid(color)`
        AutomaticGradient,  // `.automaticGradient(color, placement:)`
        Gradient,           // `.gradient(primary, secondary, placement:)`
        System,             // `.system(ramp, Double, Bool)`
    };

    Contents contents = Contents::Solid;

    // `[BIN]` `Fill+0x08` for every case that carries a colour, and `Fill+0x30`
    // for the second colour of `linear-gradient`. `primary` is the solid's
    // colour, the `automatic-gradient`'s single colour and the ramp's first;
    // `secondary` is meaningful only for `Contents::Gradient`.
    icf::Color primary;
    icf::Color secondary;

    // The gradient axis, and its ABSENCE is the finding of the spec's §5.2.
    //
    // `[BIN]` Of the four gradient-constructing sites, exactly ONE passes a
    // placement: the LAYER's `linear-gradient`, which builds it from
    // `orientation.start` and `orientation.stop` (`0x10C1A0`, constructor at
    // `0x10C1DC`). The background converter reads `primaryColor` (`Fill+0x08`)
    // and `secondaryColor` (`Fill+0x30`) and NEVER TOUCHES `orientation`
    // (`Fill+0x58`), and all three `automaticGradient` sites pass nil.
    //
    // `[ART]` So the discarding is not theoretical: 8 background
    // `linear-gradient` fills in the corpus name an orientation at the document
    // root and 6 more do inside a specialization, and 8 `automatic-gradient`
    // fills (4 background, 4 layer) name one that no site of the four reads.
    //
    // A nil placement MEANS something -- `GradientPlacement.default`,
    // `(0,0)->(0,1)`, substituted by the drawing path -- and `SystemFill.h`
    // transcribes both the default and the substitution. It is deliberately NOT
    // applied here: this carries the nil, because nil versus not-nil is exactly
    // the distinction being recorded, and `placeGradient` is the function built
    // to receive it.
    //
    // `[OBS]` Whether the target's `Fill.orientation` is optional or carries a
    // decoded default was not read. A document fill that names no orientation
    // gets no placement here. `[ART]` That is 26 of the corpus's 48 layer
    // `linear-gradient` fills, so it is the common case and not an edge.
    std::optional<GradientPlacement> placement;

    // Meaningful only for `Contents::System`.
    SystemFill ramp = SystemFill::Light;

    // `[BIN]` All four `.system(...)` sites pass the literal `1.0` and the
    // literal `true`. `[INF]` The `Double` is the fill's opacity: spec §3.1
    // reads `Icon.Fill.resolve` REWRITING each ramp stop's alpha with it
    // (`IconRendering 0x3CFF4` skips the source alpha at `+0x18`), which is the
    // only consumer of a fill-level scalar on that path.
    double opacity = 1.0;

    // `[OBS]` The third argument's NAME was not read. Three constructors write
    // `1` and no writer of `0` was found anywhere, so it is carried as a bit
    // with no meaning attached rather than given a plausible name --
    // `supportsChicletAlignmentForSystemFills` is nearby and true by default,
    // and that resemblance is not a reading.
    bool thirdArgument = true;
};

// `[BIN]` `IconColor.clear`, the four-`Double` initializer at `IconRendering
// 0x3DB0C`: `(0, 0, 0, 0)`.
//
// `[OBS]` `IconColor` is four `Double`s with no colour-space tag, so the space
// below is this file's carrier and not a reading. At alpha 0 it cannot change a
// pixel, which is why carrying one is tolerable here and would not be for a
// colour that shows.
icf::Color iconColorClear();

// Three answers, and they are genuinely three.
enum class FillOutcome : std::uint8_t {
    // The converter built an `Icon.Fill`.
    Resolved,
    // No fill. On a LAYER this is `Icon.Element.fill == nil`, which the
    // converter produces for `none` and for an `automatic` that terminates --
    // the element keeps the art's own colours. On the BACKGROUND it means only
    // that the document names no `fill` key at all, which no converter path
    // produces: `[ART]` all 145 corpus documents carry one.
    NoFill,
    // A `fill` key that is present and cannot be read. An error, not a default
    // -- the rule `Values.h` states one layer down.
    Refused,
};

struct FillResolution {
    FillOutcome outcome = FillOutcome::NoFill;
    ResolvedFill fill{};   // meaningful only when `Resolved`
    std::string why;       // meaningful only when `Refused`
};

// Field-for-field equality, for the one place the reading uses the word
// "verbatim": the layer's `automatic` is a COPY of the buffer the light slot
// produced, so a test of that claim has to compare whole results and not
// spot-check a field.
bool identical(const ResolvedFill& a, const ResolvedFill& b);

// ---------------------------------------------------------------------------
// The BACKGROUND / chiclet converter -- `0x10AD9C`-`0x10B080`.
// ---------------------------------------------------------------------------
//
// `[BIN]` The seven cases:
//
//   none               the same path as `automatic` (`0x10AEE8 cbz` ->
//                      `0x10AEF4`, the identical block)
//   automatic          the appearance switch below                  `0x10AEF0`
//   solid              `.solid(primaryColor)`                       `0x10AF68`
//   automaticGradient  `.automaticGradient(primary, placement: nil)` `0x10AE74`
//   linearGradient     `.gradient(primary, secondary, placement: nil)` `0x10AFAC`
//   systemLight        `.system(.lightChicletGradient, 1.0, true)`  `0x10AF14`
//   systemDark         `.system(.darkChicletGradient, 1.0, true)`   `0x10AF10`
//
// `[BIN]` And the switch that `none` and `automatic` share, at `0x10AEF4`:
//
//   and  w8, w26, #0xff     ; w26 = rendition.sourceAppearance
//   cmp  w8, #2
//   b.lo -> systemLightChicletGradient    ; base(0), light(1)
//   b.eq -> systemDarkChicletGradient     ; dark(2)
//        -> IconColor.clear ; Fill.solid(clear)   ; tinted(3)
//
// The third arm is the one nobody predicted and the one that shows in a pixel:
// under `tinted` the automatic background is not a grey and not a ramp, it is
// TRANSPARENT. A converter written from the hypothesis "automatic is the
// chiclet ramp, appearance picks the pole" would draw a light ramp there.
//
// `base` takes the light arm because the compare is UNSIGNED and covers both,
// not because anything chose it; `sourceAppearance` never produces `base` (see
// `Rendition`).
FillResolution backgroundFillFrom(const icf::Fill& fill, icf::Appearance appearance);

// The same, reading the `fill` key off the document root through
// `icf::resolve`, so the bare key and the `-specializations` list are the same
// code path and the appearance predicate applies. `[ART]` 34 of the 145
// documents specialize their background fill and 35 of those entries carry an
// `appearance`, so a reader that reached for `find()` would read the wrong
// background for every one of them.
//
// The appearance used for the switch is `ctx.appearance` -- `[INF]` the
// converter's `w26` is a rendition's `sourceAppearance`, and the specialization
// slot the same function resolves with is built from that same appearance, so
// the two are the same value and this takes one context rather than pretending
// they can differ.
FillResolution resolveBackgroundFill(const icf::json::Value& root, icf::Context ctx);

// ---------------------------------------------------------------------------
// The LAYER / element converter -- `0x10B7EC`-`0x10C448`.
// ---------------------------------------------------------------------------
//
// `[BIN]` The layer's fill is resolved TWICE, at two specialization slots: the
// current one, and one built at `0x10BDC0` with the appearance forced to
// `light` -- `mov w9, #0x100 ; bfxil w9, w0, #0, #8`, which replaces the
// appearance byte and keeps the rest of the slot.
//
// `[BIN]` The seven cases, answering `Icon.Element.fill: Fill?`:
//
//   none               `nil`               `0x10C0F0` -> `0x10C15C`
//   automatic          `nil` when the slot's appearance is `light`; otherwise a
//                      VERBATIM COPY of what this same layer's fill resolves at
//                      the `light` slot     `0x10C0FC`, `0x10C118`, `0x10C138`
//   solid              `.solid(primary)`                            `0x10C168`
//   automaticGradient  `.automaticGradient(primary, placement: nil)` `0x10C088`
//   linearGradient     `.gradient(primary, secondary, placement:
//                      GradientPlacement(orientation.start, orientation.stop))`
//                                            `0x10C1A0`, ctor `0x10C1DC`
//   systemLight        `.system(.lightChicletGradient, 1.0, true)`  `0x10C148`
//   systemDark         `.system(.darkChicletGradient, 1.0, true)`   `0x10C260`
//
// `[BIN]` The light-slot pass at `0x10BE20` maps `none` AND `automatic` to nil
// as well. That is what makes the recursion finite: an `automatic` inheriting
// from an `automatic` terminates at nil instead of looping, and the terminator
// is in the read code rather than in a depth limit invented here.
//
// `[OBS]` A dead `cmp w8, #2` sits at `0x10C11C` with no branch consuming it.
// The three-way switch was tail-merged, so `base`, `dark` and `tinted` all take
// the copy. Recorded because it is the shape of a fourth arm that no longer
// exists -- and NOT modelled as one.
//
// The document spells the axis `{start, stop}` and `GradientPlacement` spells
// it `{start, end}`. The rename is nothing but a rename -- the constructor at
// `0x10C1DC` takes the two `CGPoint`s in that order and stores them in that
// order -- and it is written down here because a silent field-name change is
// how a transposed axis gets shipped.
//
// `lightSlotFill` is what this same layer's `fill` resolves to at the light
// slot, or nullptr when the layer names none there. It is read only by the
// `automatic` case; every other case ignores it, as the binary does.
FillResolution layerFillFrom(const icf::Fill& fill, icf::Appearance appearance,
                             const icf::Fill* lightSlotFill);

// The same, performing both resolutions itself: `fill` at `ctx`, and `fill` at
// `ctx` with the appearance forced to `light`. The idiom is carried across
// unchanged, because the `bfxil` replaces one byte of the slot and leaves the
// other.
//
// A layer that names no `fill` at all answers `NoFill`. `[INF]` The target's
// converter runs on a `Layer.fill` that is already a value, so what happens
// when there is none is not a branch this file could have read; the answer it
// gives is the one `none` gives, and `[ART]` the corpus's 159 layers with no
// `fill` key sit alongside 49 that say `none`, which is only coherent if
// neither replaces the art.
FillResolution resolveLayerFill(const icf::Layer& layer, icf::Context ctx);

}  // namespace rb
