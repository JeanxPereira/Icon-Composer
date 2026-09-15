#pragma once
// The glass material: from an `.icon` GROUP to the numbers the target's
// displacement shader is handed.
//
// WHERE THE PARAMETERS ARE, AND IT IS NOT WHERE ONE WOULD LOOK
// ------------------------------------------------------------
// `[ART]` A structural sweep of the 145 corpus documents finds `glass` /
// `glass-specializations` 280 times and all 280 inside `groups[].layers[]`, of
// type Bool and only Bool. **No layer carries a glass PARAMETER.** The layer
// says who participates; the GROUP carries the material, in six keys, and it
// carries them unconditionally -- `translucency` and `shadow` are written in
// 271 of 271 groups, glass or no glass.
//
// `[INF]` That is exactly the shape `Icon.Layer.material` +
// `Icon.Element.participatesInGlass` predicts: the document's GROUP is the
// target's `Icon.Layer` and the document's LAYER is its `Icon.Element`.
//
// THE DEFECT THIS FILE IS BUILT TO SURVIVE
// ----------------------------------------
// Every one of the six keys may appear bare OR as a sibling
// `X-specializations` array of `{value, appearance, ...}`. `[ART]` Over the
// corpus the two spellings are mutually exclusive per group -- 0 groups carry
// both -- so a reader BRANCHES, it does not merge. That branch is not written
// here: `icf::resolve` already implements the measured precedence rule, and
// this file goes through it for all six keys rather than reaching for
// `find()`. Reaching for `find()` is the bug that has bitten this project
// before.
//
// WHAT IS TRANSCRIPTION AND WHAT IS NOT
// -------------------------------------
// The eight fields, their byte offsets, the two derived properties, the eight
// normalisation constants and the three formulas are all `[BIN]`, read from
// `IconRendering.arm64` (doc 03 §29.2-§29.4). What is NOT read, and is marked
// `[INF]` or `[OBS]` at each site:
//
//   `[OBS]` The dataflow from `material.refractionHeight` into the arithmetic
//   at `0x4A708` was never traced. Whole-module optimisation dissolved the
//   material into a 0xC0-byte draw descriptor copied whole out of an array
//   (doc 03 §29.6). The names, the offsets, the values, the arithmetic and the
//   consumer are all read; **which material field binds to which argument of
//   that arithmetic is INFERENCE by name**, supported by the clamp to [0,1]
//   before the `pow` and by the output being points on a 1024 canvas.
//
//   `[OBS]` `shadowOpacity`, `hasSpecular` and `specularPlacement` have no known
//   consumer. They cross Swift<->ObjC verbatim with no arithmetic anywhere on
//   the path, and there is no `Max`/`Power` partner for any of them in
//   `ICRRenderingParameters`. They are carried through here as the raw document
//   values and denormalised by nothing, because inventing a denormalisation for
//   them is exactly the kind of plausible-and-wrong this project refuses.
//
//   `translucency` WAS in that list until 2026-09-15 and is no longer.
//   `Docs/Laudos/2026-09-15-translucencia.md` found its consumer: it is the
//   interpolation weight of `ICRRenderingParameters.glyphTranslucency`, read at
//   `0x0000FDE0` and multiplied at `0x0000FDE4`, and its destination is the
//   `opacityBounds` of the shader `simplifiedShapeAwareGradientMask`. There is
//   no `Max`/`Power` partner because there is NO DENORMALISATION -- the
//   destination is already an opacity. `GlassTranslucency.h` carries the whole
//   of it. It is still transported raw through this file, which is now a
//   reading and not a shrug.
//
// WHAT THIS FILE DOES NOT DO
// --------------------------
// It stops at the numbers. `[BIN]` The height's last hop is a conversion to
// texels of the distance field, `height x (n - 2) / [self+0x568]` at `0x10C8C`;
// `[OBS]` that divisor was not identified, so the conversion is named here and
// not implemented. Nothing here touches a GPU, and nothing here is wired into
// the compositor.
#include <cstdint>
#include <optional>
#include <string>

#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Values.h"

namespace rb {

// `[BIN]` Dense bytes in reflection-metadata order, from the one-instruction
// getters at `IconRendering.arm64` `0x38DC8` and `0x38E30`.
//
// `[INF]` The format's fourth shadow case is spelled `layer-color` and the
// renderer's is `vibrant`. The pairing is forced, not chosen: same cardinality,
// three shared names, one renamed across the boundary.
enum class ShadowStyle : std::uint8_t { Automatic = 0, None = 1, Vibrant = 2, Neutral = 3 };
enum class SpecularPlacement : std::uint8_t { Automatic = 0, Inside = 1, Outside = 2 };

// `[BIN]` `IconRendering.Icon.GlassMaterial`, eight fields, with the byte
// offsets read from the exported one-instruction getters -- so the layout is
// read, not deduced from field order:
//
//   +0x00 hasSpecular Bool        (0x38DB8)   +0x01 shadowStyle       (0x38DC8)
//   +0x08 shadowOpacity Double    (0x37F1C)   +0x10 translucency      (0x38DF0)
//   +0x18 blurStrength Double     (0x38E00)   +0x20 refractionHeight  (0x38E10)
//   +0x28 refractionStrength      (0x38E20)   +0x30 specularPlacement (0x38E30)
struct GlassMaterial {
    bool hasSpecular = false;
    ShadowStyle shadowStyle = ShadowStyle::Automatic;
    double shadowOpacity = 0.0;
    double translucency = 0.0;
    // The switch that stands beside the value, carried SEPARATELY. The document
    // keeps both facts -- `[ART]` 33 corpus groups are `(false, 0.5)`, a value
    // remembered behind a closed switch -- so this transport keeps both too.
    // Folding them together here would erase the remembered value, and would
    // break the rule the rest of this struct follows: the other two `enabled`
    // bits travel raw as well, and the refraction is stopped by a strength of
    // zero, never by its bit. The fold happens where the mask is built.
    bool translucencyEnabled = true;

    // `[OBS]` Zero here is NOT a read default. The five-argument convenience
    // init supplies defaults for the three fields below and for nothing else,
    // so what the target puts in these when a document is silent was not read.
    double blurStrength = 0.0;

    // `[BIN]` The five-argument init at `0x38E58` fills the tail from the
    // constant at `0x93B30`: height 0.5, strength 0.0, placement automatic. The
    // same constant is the compatibility fallback at `0x0BC98`, used when an
    // `ICRIconLayer` does not respond to `refractionHeight`.
    double refractionHeight = 0.5;
    double refractionStrength = 0.0;
    SpecularPlacement specularPlacement = SpecularPlacement::Automatic;

    // `[BIN]` Two properties the field metadata does not list because they are
    // computed, read from `0x38F58` (`cmp #1, cset ne`) and `0x38FB0`
    // (`cmp #2, cset eq`). Both come from `shadowStyle` alone.
    //
    // The trap is in the names: `neutral` and `automatic` DRAW a shadow and do
    // not infuse colour. Only `vibrant` infuses. A transcription that read
    // `neutral` as "no shadow" would miss the pixel and the name would not warn
    // it.
    bool hasShadow() const { return shadowStyle != ShadowStyle::None; }
    bool shadowInfusesGlyphColor() const { return shadowStyle == ShadowStyle::Vibrant; }
};

// What the GROUP said, key by key, after `icf::resolve`.
//
// Absent means absent. Every field is optional because the format writes these
// keys unevenly -- `[ART]` 271 groups carry `shadow`, 159 carry `blur-material`,
// 5 carry `refractivity` -- and a reader that answered a missing key with a
// number would be inventing the one thing this file exists to avoid.
struct GlassMaterialDocument {
    // `[ART]` bool x197 and the string "inside" x3, and the two shapes are the
    // same vocabulary: `SpecularHighlight`'s four cases.
    std::optional<icf::SpecularHighlight> specular;

    std::optional<icf::Shadow> shadow;
    std::optional<icf::Translucency> translucency;

    // `[OBS]` `blur-material` is `number | null`, and null in 85 of the 159
    // groups that carry it. It is the `BlurMaterial {enabled, explicitStrength}`
    // pair collapsed into one slot, and it is NOT determinable from the document
    // whether null means "enabled with automatic strength" or "disabled". So a
    // null is represented as `blurMaterial` absent WITH `blurMaterialKey` true:
    // the caller can tell "the group said nothing" from "the group said null",
    // which is the only distinction the corpus supports.
    bool blurMaterialKey = false;
    std::optional<double> blurMaterial;

    std::optional<icf::Refractivity> refractivity;

    // `[INF]` `lighting` maps to `Icon.Layer.performsLightingByElement`, which
    // lives on the LAYER struct and not inside `GlassMaterial`. It is read here
    // because it is one of the six material-family group keys, and it is
    // deliberately not a field of `GlassMaterial`. `[INF]` individual -> true is
    // by name; no arithmetic or branch on it was read.
    std::optional<icf::Lighting> lighting;
};

// Why a read refused. A key that is present and unreadable is an error, not a
// default -- the same rule `Values.h` states for the layer below this one.
struct GlassMaterialReadError {
    std::string key;   // the group key that refused, e.g. "shadow"
    std::string why;
};

// The six material keys of one group, resolved for `ctx`.
//
// `nullopt` only when a key is PRESENT and cannot be read. A group that carries
// none of the six reads successfully and answers with every field absent --
// `[ART]` which is a real case: 74 of the corpus's 271 groups carry no
// `blur-material` at all.
std::optional<GlassMaterialDocument> readGlassMaterial(const icf::Group& group,
                                                       icf::Context ctx,
                                                       GlassMaterialReadError* error = nullptr);

// The field-for-field mapping onto the target's eight.
//
// `[INF]` Two collapses happen here and both are inference from cardinality,
// not from read code:
//
//   1. `SpecularHighlight`'s FOUR cases become `hasSpecular` (off vs the rest)
//      plus `specularPlacement` (three cases). 4 = 1 + 3 exactly, and it is what
//      explains the document's two shapes: a bool is the off/on axis, the string
//      is the placement axis. `[INF]` A bare `true` maps to `automatic` -- the
//      case that names no placement.
//   2. `refractivity {strength, depth}` -> `{refractionStrength,
//      refractionHeight}`. `depth` -> `refractionHeight` is by name; the target
//      has exactly one height-shaped field and the format exactly one
//      depth-shaped key.
//
// THE ENABLED BITS. `refractivity.enabled` and the null-ness of `blur-material`
// still have no counterpart in the eight-field `GlassMaterial` and where they
// collapse was `[OBS]` not read, so this carries their VALUE through and leaves
// the bit visible on the document struct. It matters: `[ART]` 42 corpus groups
// are `(enabled: false, value: 0.5)`, so zeroing a disabled value and ignoring
// the bit give different answers.
//
// `translucency.enabled` is the exception since 2026-09-15, and it is marked
// `[INF]` at the line that does it: the bit is folded as `enabled ? value : 0.0`
// because `f = 0` is exactly "opaque" and because `[BIN]` there is provably no
// field anywhere in the render model for the bit to survive in. `[OBS]` WHERE
// the target performs that fold is still unlocated.
//
// `[OBS]` An absent key leaves the corresponding field at its struct default,
// and only three of those defaults are read (see `GlassMaterial`).
GlassMaterial glassMaterialFrom(const GlassMaterialDocument& doc);

// `[BIN]` `ICRRenderingParameters`, the block whose offsets come from exported
// getters (`0x61F68`..`0x62058`) and whose values come from the aggregate
// constructor at `0x5E838` writing pools at `0x985D0`/`0x985E0`/`0x985F0`.
//
// `[ART]` Five of them are exact fractions of 1024 -- 0.0625, 0.0125, 0.25,
// 0.625, 0.26. That corroborates `IconRenderer.h`'s `kCanvasPoints = 1024.0`
// without proving it: none of the five SAYS 1024, but five independent
// constants landing on round fractions of it is not coincidence. The units of
// everything below are therefore POINTS on a 1024-point canvas, not fractions.
struct GlassRenderingParameters {
    double blurStrengthMax = 64.0;            // +0x1E8
    double refractionHeightMin = 12.8;        // +0x1F0
    double refractionHeightMax = 256.0;       // +0x1F8
    double refractionHeightPower = 1.0;       // +0x200
    double refractionStrengthMax = 640.0;     // +0x208
    double refractionStrengthPower = 1.0;     // +0x210

    // `[BIN]` Goes to `-[RBShader setVariant:]`, confirmed independently of the
    // offset arithmetic: `0x10CAC` loads `[x20, #0x280]` and `0x280 - 0x68` is
    // `+0x218`, the block being embedded at `self+0x68`.
    int refractionSupersampling = 2;          // +0x218

    // `[BIN]` Read from the same block and carried for completeness. No formula
    // here consumes it -- it belongs to the chiclet, not to the material.
    double defaultChicletCornerRadius = 266.24;  // +0x228
};

// `[BIN]` Transcribed from `0x4A708`, the only `_pow` site in the binary that
// takes these parameters. Clamped on BOTH sides, and the clamp precedes the
// power:
//
//   h = fminnm(x, 1.0); h = (h >= 0) ? h : 0
//   out = min + (max - min) * pow(h, power)
//
// `[INF]` That the `x` is `material.refractionHeight` -- see the header's first
// `[OBS]`.
double denormaliseRefractionHeight(double normalisedHeight,
                                   const GlassRenderingParameters& p = GlassRenderingParameters{});

// `[BIN]` Also `0x4A708`, and the asymmetry against the height is the finding:
//
//   m = fminnm(|s|, 1.0)
//   g = copysign(1.0, s); g = (s == 0 || isnan(s)) ? 0 : g   ; bsl v2, v4, v3
//   out = max * g * pow(m, power)
//
// The SIGN IS PRESERVED and only the magnitude is clamped. `[INF]` This is what
// explains the corpus without having to explain it away: `[ART]` both enabled
// `refractivity` entries in all 145 documents carry a NEGATIVE strength
// (-0.5269921875 and -0.3591796875). The format signs it and the target carries
// the sign THROUGH the power instead of discarding it -- which it must, since
// `pow` of a negative base would be NaN. Anyone transcribing all three of these
// with `clamp(0,1)` gets exactly the two real corpus values wrong.
double denormaliseRefractionStrength(double normalisedStrength,
                                     const GlassRenderingParameters& p = GlassRenderingParameters{});

// `[BIN]` `0x4A948`, no `pow` and -- the point -- NO FLOOR:
//
//   radius = min(b, 1.0) * blurStrengthMax        -> addBlurFilterWithRadius:opaque:
//
// A negative `blurStrength` therefore produces a NEGATIVE radius and is not
// clamped up. That is not a bug being reproduced for its own sake: it is the
// third member of a set of three formulas that clamp three different ways, and
// the differences are the information.
double denormaliseBlurRadius(double normalisedBlur,
                             const GlassRenderingParameters& p = GlassRenderingParameters{});

// The numbers on the far side of the denormalisation.
struct DenormalisedGlass {
    // `[BIN]` Points on the 1024 canvas. This becomes the `height` field of the
    // `glass-displacement` display-list style (style 3, `add_glass_displacement`
    // at `0xF39F0`, field names read from the XML serialiser at `0xEDB24`)
    // after a conversion to SDF texels that this file does not perform.
    double refractionHeightPoints = 0.0;

    double refractionStrengthPoints = 0.0;

    // `[BIN]` The single argument of `displacementMap_v1`, and it is the
    // strength NEGATED: `setArgumentBytes:atIndex:0 type:1 count:1` with the
    // float `-out_strength`, at `0x10C14` and again at `0x805F4`. Two sites, one
    // pattern. Kept as its own field because the negation is a fact about the
    // shader's argument, not about the strength.
    double displacementShaderArgument = 0.0;

    // `[BIN]` Radius in points for `addBlurFilterWithRadius:opaque:`.
    double blurRadiusPoints = 0.0;

    // `[BIN]` `-[RBShader setVariant:]`.
    int refractionSupersampling = 2;

    // `[OBS]` Below this line nothing is denormalised, because nothing was read
    // that denormalises it. These are the document's values transported, and the
    // absence of any `Max`/`Power` partner in `ICRRenderingParameters` is the
    // evidence that there is no arithmetic to find, not merely that it was not
    // found.
    //
    // `[BIN]` For `translucency` that suspicion is now settled rather than
    // merely held: there is no denormalisation because the field is already an
    // opacity weight. It leaves here raw and `GlassTranslucency.h` multiplies it
    // by `glyphTranslucency.strength[sizeClass]`, which is the only arithmetic
    // it ever meets.
    double translucency = 0.0;
    // Carried through untouched, for the same reason as in GlassMaterial: the
    // value and its switch are two facts, and the fold belongs to whoever
    // builds the mask.
    bool translucencyEnabled = true;
    double shadowOpacity = 0.0;
    bool hasSpecular = false;
    SpecularPlacement specularPlacement = SpecularPlacement::Automatic;
    ShadowStyle shadowStyle = ShadowStyle::Automatic;

    // Derived, and `[BIN]`: see `GlassMaterial::hasShadow`.
    bool hasShadow = false;
    bool shadowInfusesGlyphColor = false;
};

DenormalisedGlass denormaliseGlass(const GlassMaterial& material,
                                   const GlassRenderingParameters& p = GlassRenderingParameters{});

}  // namespace rb
