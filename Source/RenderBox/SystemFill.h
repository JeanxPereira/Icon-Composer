#pragma once
// `Icon.SystemFill`: the two canned greyscale ramps, the opacity rewrite that
// resolves them, and the gradient PLACEMENT that this project spent an entire
// spec unable to read.
//
// WHAT THIS CLOSES
// ----------------
// `2026-09-01-gradiente.md` §4.4 left `automatic-gradient` deliberately
// UNDRAWN, with an explicit rule against approximating it. Its reason was not
// the six parameters -- those were measured, and `AutomaticGradient.cpp`
// transcribes the rule that applies them -- it was that **the AXIS was never
// read**. A ramp with no geometry is not a gradient.
//
// The axis is now read, and it is read in a place nobody was looking: it is not
// on the ramp at all. `[BIN]` `IconColor.LinearGradient` carries ONLY `stops`.
// The geometry is a separate `Icon.GradientPlacement {start, end}`, `.system`
// resolves it to `nil`, and `nil` is not "absent" -- it is a defined value,
// `GradientPlacement.default`, substituted by the draw path. That is why the
// axis could not be found by reading the gradient: the gradient does not have
// one. See `defaultGradientPlacement` below for the four instructions.
//
// `[BIN]` All three converter sites pass `placement: nil` for
// `automaticGradient`, so `automatic-gradient` inherits this same default
// vertical axis. §4.4's reason no longer holds.
//
// WHAT IS HERE AND WHAT IS NOT
// ----------------------------
// This is a CPU-only transcription in the shape of `GlassMaterial.h`: read
// constants, read arithmetic, no GPU, and NOTHING wired into the renderer. The
// join to the renderer is a later task and is deliberately not made here.
//
// The ramp stop type is `rb::RampStop` from `AutomaticGradient.h`, reused
// rather than redeclared -- these ramps and that one feed the same consumer,
// and two structurally identical stop types in one namespace would be an
// invitation to convert between them.
#include <cstdint>
#include <optional>
#include <vector>

#include "Source/RenderBox/AutomaticGradient.h"

namespace rb {

// `[BIN]` `Icon.SystemFill`, the payload the resolve masks and compares:
// `IconRendering 0x3CF74` does `and x8, x10, #0xff ; cmp x8, #1 ; csel x21,
// x13, x12, eq` -- x13 the dark ramp, x12 the light one. So the raw value is a
// byte, and 1 is dark. `[INF]` That 0 is light rests on the `csel`'s false arm
// being the light ramp plus the two-case shape of the type; no `displayName`
// chain was decoded for this enum the way one was for `Fill.Kind`.
enum class SystemFill : std::uint8_t { Light = 0, Dark = 1 };

// ---------------------------------------------------------------------------
// The two ramps
// ---------------------------------------------------------------------------
//
// `[BIN]` One builder, `IconRendering 0x3E680`, signature `(d0: grey0, d1:
// grey1) -> IconColor.LinearGradient`. It writes exactly TWO stops -- the array
// header is the constant at `0x938B0`, `{count 2, capacity 4}`, and the two
// locations are the constant at `0x971F0`, `{1.0, 0.0}`:
//
//     stop0 = IconColor(g0, g0, g0, alpha 1.0) @ location 0.0
//     stop1 = IconColor(g1, g1, g1, alpha 1.0) @ location 1.0
//
// so `r == g == b`, alpha is 1.0, and the locations are exactly the endpoints.
//
// `[BIN]` It is called twice from the `ICRRenderingParameters` default
// initialiser, and those two call sites are where the four greys live:
//
//     0x5E8E8  d0 = 1.0                   d1 = 0.9607843137254902  -> +0x80
//     0x5E908  d0 = 0.12156862745098039   d1 = 0.058823529411764705 -> +0x88
//
// `[INF]` Each of the four is an exact 255th -- 255, 245, 31, 15. That is
// arithmetic on the constants as read, not a corpus measurement, and it is
// corroboration rather than a reason to rewrite them: they are transcribed as
// read, so `0.9607843137254902` stays `0.9607843137254902` and is NOT
// "corrected" into the prettier number it happens to equal.
//
// `[ART]` What consumes these: the corpus has 28 BACKGROUND `automatic`
// occurrences and 55 LAYER ones, and per `2026-09-02-automatic.md` §2.4 only
// the 28 reach `Icon.Fill.Contents.system`. The 55 never arrive here at all --
// the layer's `automatic` is a specialisation-inheritance operator, not a
// colour. So these ramps are the answer for 28 occurrences, not 83.
//
// `[OBS]` THE DIRECTION OF THE RAMP IS NOT DETERMINED. Location 0.0 gets `g0`
// and location 1.0 gets `g1` -- that much is read -- but the default axis runs
// `(0,0) -> (0,1)` and the y-handedness of the RB display list was never
// established, so which END OF THE SHAPE receives 255 and which 245 is unread.
// On the light ramp the two answers differ by four parts in 255 and are nearly
// invisible; on the dark pair, 31 against 15, they are not. This file does not
// pick: it reproduces the ORDER as written and names the ambiguity here.
//
// `[OBS]` The colour space of the greys is unread -- `IconColor` is four bare
// `Double`s with no space tag (the same gap `AutomaticGradient.h` names).
std::vector<RampStop> systemLightGradient();
std::vector<RampStop> systemDarkGradient();

// The `csel` at `0x3CF7C`, as a function.
std::vector<RampStop> systemGradient(SystemFill which);

// ---------------------------------------------------------------------------
// The opacity rewrite
// ---------------------------------------------------------------------------
//
// `[BIN]` `Icon.Fill.resolve(parameters:)` at `IconRendering 0x3CE80` calls the
// helper at `0x3CFF4`, which walks the stop array (stride `0x28`, elements at
// `+0x20`, count at `+0x10`) and rebuilds every stop as
//
//     Stop(color: (r, g, b, opacity), location: location)
//
// loading `r,g,b` from `+0x00/+0x08/+0x10` and `location` from `+0x20`, and
// **never loading `+0x18`**, which is where the source alpha lives.
//
// So the fill's opacity REPLACES the canned alpha. It does not multiply it.
// The two are indistinguishable at opacity 1.0 -- which is every stop of both
// canned ramps, and therefore every case this binary can produce -- so nothing
// in the target would ever expose a wrong choice here. That is exactly why it
// is a separate named function with a gate of its own: an error that only shows
// up on input the target never produces is an error that stays green forever.
std::vector<RampStop> rewriteStopOpacity(const std::vector<RampStop>& stops, double opacity);

// ---------------------------------------------------------------------------
// The placement, and the nil that means something
// ---------------------------------------------------------------------------

// A point in the placement's own coordinates. Unit coordinates before
// `placeUnitPoint`, and rect coordinates after -- the same struct on both sides
// because the target uses `CGPoint` on both sides.
struct PlacementPoint {
    double x = 0.0;
    double y = 0.0;
};

// `[BIN]` `Icon.GradientPlacement`, two `CGPoint`s, from the 0x20-byte store in
// `GradientPlacement.default`.
struct GradientPlacement {
    PlacementPoint start;
    PlacementPoint end;
};

// `[BIN]` `GradientPlacement.default`, `IconRendering 0x38CF4`, four
// instructions and no arithmetic:
//
//     stp xzr, xzr, [x8]          ; start = (0.0, 0.0)
//     mov x9, #0x3FF0000000000000 ; 1.0
//     stp xzr, x9,  [x8, #0x10]   ; end   = (0.0, 1.0)
//
// A vertical axis: x is zero at both ends, y runs 0 to 1. This is THE value
// that `2026-09-01-gradiente.md` §4.4 could not find.
GradientPlacement defaultGradientPlacement();

// The rect a placement is resolved against. `[BIN]` Normally the
// `boundingRect` of the shape being filled, read at `0x1BC8C`.
struct PlacementRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// `[BIN]` `0x1BB60`-`0x1BB74`: the placement is in UNIT coordinates of a rect,
// and the mapping is
//
//     point = rect.origin + unit * (CGRectGetWidth(rect), CGRectGetHeight(rect))
//
// -- an offset and a per-axis scale, with no aspect correction between the two
// axes. A unit square maps to the rect, not to a square inscribed in it, so on
// a non-square rect the default vertical axis stays vertical but its LENGTH is
// the rect's height.
PlacementPoint placeUnitPoint(PlacementPoint unit, const PlacementRect& rect);

// The two endpoints after placement.
struct GradientAxis {
    PlacementPoint start;
    PlacementPoint end;
};

// `[BIN]` The draw path at `0x1BAB8`-`0x1BAE4` reads the
// `Optional<GradientPlacement>` discriminator and substitutes
// `GradientPlacement.default` when it says `.none`. Taking an `optional` here
// rather than a bool-plus-value is not decoration: the substitution is the
// whole finding, and a caller that had to remember to substitute would be a
// caller that forgets.
GradientAxis placeGradient(const std::optional<GradientPlacement>& placement,
                           const PlacementRect& rect);

// ---------------------------------------------------------------------------
// THE RECT THAT WAS NOT READ
// ---------------------------------------------------------------------------
//
// `[BIN]` `ICRRenderingParameters+0x360` is written `1` by the default
// initialiser -- `strh w22, [x19, #0x360]` with `w22 = 1` at `0x5EDB0`. When
// set, and `[BIN]` ONLY for `Contents == .system` (the branch at `0x1AD40`),
// the rect stops being the shape's `boundingRect` and becomes origin `(0,0)`
// with a `CGSize` taken from the drawing context at `+0x48`.
//
// `[INF]` The NAME `supportsChicletAlignmentForSystemFills` rests on the
// declaration order of the last three `Bool`s of that parameter block lining up
// with the three field names, not on a symbol that points at `+0x360`. The
// behaviour at that offset is read; the label on it is inference.
//
// `[OBS]` **WHETHER THAT `CGSize` IS THE CANVAS, THE CHICLET, OR THE FULL-BLEED
// FRAME WAS NOT READ.** The three differ by enough to move the whole ramp, so
// the alternative is declared here and left unimplemented rather than guessed.
// `systemFillRect` answers `nullopt` for it, which is a gate a wrong guess
// cannot slip past: there is no number to be wrong.
inline constexpr bool kSupportsChicletAlignmentForSystemFills = true;

enum class SystemFillRectSource {
    // The shape's own bounding rect. This is what the first implementation
    // draws over, and it is a real path in the target -- the one taken whenever
    // chiclet alignment is off.
    BoundingRect,
    // Origin (0,0) with the drawing context's size. Named, not implemented.
    ChicletAligned,
};

// `nullopt` means "this branch exists in the target and its rect was not read",
// never "no rect". A caller must not fall back to `boundingRect` on a `nullopt`
// -- that would be the guess this refuses to make, wearing a default's clothes.
std::optional<PlacementRect> systemFillRect(SystemFillRectSource source,
                                            const PlacementRect& boundingRect);

// ---------------------------------------------------------------------------
// The resolve, whole
// ---------------------------------------------------------------------------

// `[BIN]` What `Icon.Fill.resolve(parameters:)` leaves behind for a
// `.system` fill: the chosen ramp with every alpha rewritten, and a placement
// region that is ZEROED and then tagged --
//
//     0x3CFAC  movi v0.2d,#0 ; stur q0,[x19,#8] ; stur q0,[x19,#0x18]
//     0x3CFB8  mov  w8, #0x81 ; strb w8,[x19,#0x28]
//
// 32 zeroed bytes is exactly the two `CGPoint`s of a `GradientPlacement`.
// `[INF]` That the `0x81` byte is the `.gradient` case with the Optional
// discriminator packed into the spare bits is inference from the layout: the
// payload region it tags is the placement, and the value is written after the
// region is zeroed rather than as part of a live placement.
//
// The consequence is the point: a `.system` fill NEVER carries geometry of its
// own, so it always draws on `defaultGradientPlacement()`.
struct ResolvedSystemFill {
    std::vector<RampStop> stops;
    // Always `nullopt` for `.system`. Kept as an `optional` and not dropped,
    // because it is the input `placeGradient` is designed to receive.
    std::optional<GradientPlacement> placement;
};

ResolvedSystemFill resolveSystemFill(SystemFill which, double opacity);

}  // namespace rb
