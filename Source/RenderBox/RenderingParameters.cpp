#include "Source/RenderBox/RenderingParameters.h"

namespace rb {
namespace {

constexpr double kPi = 3.14159265358979323846;

HighlightSizeValue sbv(double display, double large, double medium, double small,
                       bool present = true) {
    HighlightSizeValue v;
    v.slots[0] = display;
    v.slots[1] = large;
    v.slots[2] = medium;
    v.slots[3] = small;
    v.present = present;
    return v;
}

HighlightSizeValue flat(double all, bool present = true) {
    return sbv(all, all, all, all, present);
}

// The shape every member of the ten sets shares, and what each one overrides
// from it is written at the member: `outsetOpacity` and `minInsetPixels` nil
// (tag `1` at `+0x48` and `+0xD0`), `minDistancePixels` and `inset` zero, no
// blend override (the byte `0x12` at `+0x100`).
HighlightSettings member(double brightness, const HighlightSizeValue& opacity,
                         const HighlightSizeValue& distance, const HighlightSizeValue& spread,
                         double bias) {
    HighlightSettings s;
    s.brightness = brightness;
    s.opacity = opacity;
    s.outsetOpacity = flat(0.0, false);
    s.distance = distance;
    s.minDistancePixels = flat(0.0);
    s.inset = flat(0.0);
    s.minInsetPixels = flat(0.0, false);
    s.spread = spread;
    s.bias = bias;
    s.hasBlendModeOverride = false;
    return s;
}

OptionalHighlight some(const HighlightSettings& s) { return OptionalHighlight{true, s}; }
OptionalHighlight none() { return OptionalHighlight{}; }

FillHighlights custom(const HighlightSettings& s) {
    return FillHighlights{FillHighlightsKind::Custom, some(s)};
}
FillHighlights customNil() { return FillHighlights{FillHighlightsKind::Custom, none()}; }
FillHighlights matchKey() { return FillHighlights{FillHighlightsKind::MatchKey, none()}; }
FillHighlights noFill() { return FillHighlights{FillHighlightsKind::None, none()}; }

// ---- generation 27's sets ------------------------------------------------------
//
// `[BIN]` `chicletDefault`, `chicletBright` and `chicletDim` as the constructor
// `0x00062A78` writes them (`0x00062BC8`-`0x00062EDC`, and the same slices copied
// twice more up to `0x00063608`): all six members present, every `SizeBasedValue`
// flat. `ChicletHighlights.h` §3 carries the reading.
HighlightsSet chicletPlain27() {
    HighlightsSet set;
    const HighlightSettings keySharp = member(1.1, flat(0.2), flat(10.0), flat(2.0 * kPi / 3.0), 0.5);
    set.keySharp = some(keySharp);
    set.keyDiffuse = some(member(1.1, flat(0.5), flat(40.0), flat(kPi / 2.0), 0.08));
    // Identical to `keySharp`, cone included -- and a payload of its own, not
    // `matchKey` (the byte is `0x12`).
    set.fillSharp = custom(keySharp);
    set.fillDiffuse = custom(member(1.1, flat(0.25), flat(40.0), flat(kPi / 2.0), 0.08));
    set.dark = some(member(0.0, flat(0.2), flat(10.0), flat(kPi / 3.0), 0.5));
    // Present, with `opacity == 0`: it exists and paints nothing. `bias == 1.0`.
    set.rim = some(member(1.0, flat(0.0), flat(10.0), flat(kPi), 1.0));
    return set;
}

// `[BIN]` `chicletClear` and `chicletScreened`, the two the constructor builds
// from new constants (`0x00063624`-`0x00063AEC`). Neither is touched by
// `0x76FC0`, so both generations carry these. The two fills are `matchKey`
// (`0x14`) and the rim is nil (`0x13`): six highlights each.
HighlightsSet chicletClear() {
    HighlightsSet set;
    set.keySharp = some(member(1.0, flat(1.25), flat(10.0), flat(5.0 * kPi / 9.0), 0.5));
    set.keyDiffuse = some(member(1.0, flat(0.5), flat(44.0), flat(kPi / 4.0), 0.5));
    set.fillSharp = matchKey();
    set.fillDiffuse = matchKey();
    set.dark = some(member(0.0, sbv(0.25, 0.1, 0.1, 0.0), sbv(9.0, 8.0, 8.0, 9.0),
                           flat(65.0 * kPi / 180.0), 0.5));
    set.rim = none();
    return set;
}

HighlightsSet chicletScreened() {
    HighlightsSet set;
    set.keySharp = some(member(1.0, sbv(0.6, 0.7, 0.7, 0.7), flat(10.0), flat(5.0 * kPi / 9.0), 0.5));
    set.keyDiffuse = some(member(1.0, sbv(0.25, 0.3, 0.3, 0.3), flat(44.0), flat(kPi / 4.0), 0.5));
    set.fillSharp = matchKey();
    set.fillDiffuse = matchKey();
    set.dark = some(member(0.0, sbv(0.25, 0.0, 0.0, 0.0), sbv(9.0, 8.0, 8.0, 9.0),
                           flat(65.0 * kPi / 180.0), 0.5));
    set.rim = none();
    return set;
}

// `[BIN]` The glyph factory `0x00064604`, called five times with the same three
// arguments (`0x00063AEC`-`0x00063BF4`): all five `glyphs*` sets of generation
// 27, and -- because `0x76FC0` leaves it alone -- `glyphsClear` of generation 26.
//
// `fillDiffuse` IS `matchKey`. `0x0006485C` calls `0x00033FA4`, which writes the
// byte `0x14`, and `0x00033F04` reads `0x14` as `FillHighlights.matchKey`: the
// expander copies `keyDiffuse` into the fourth slot. Until 2026-10-01 this was
// read one off -- `0x14` as nil -- and the set drew five highlights where the
// target draws six.
HighlightsSet glyphs27() {
    HighlightsSet set;
    // keySharp -- `0x00064648`-`0x000646F8`.
    HighlightSettings keySharp =
        member(1.0, sbv(1.0, 1.0, 1.0, 0.3), sbv(4.0, 6.0, 6.0, 6.0), flat(kPi / 2.0), 0.5);
    keySharp.minDistancePixels = flat(1.0);
    set.keySharp = some(keySharp);

    // keyDiffuse -- `0x000646FC`-`0x00064788`.
    HighlightSettings keyDiffuse =
        member(1.0, flat(1.0), sbv(16.0, 24.0, 24.0, 24.0), flat(kPi / 3.0), 0.08);
    keyDiffuse.minDistancePixels = flat(4.0);
    set.keyDiffuse = some(keyDiffuse);

    // fillSharp -- `0x0006478C`-`0x00064854`: `keySharp` with the narrower cone.
    HighlightSettings fillSharp = keySharp;
    fillSharp.spread = flat(kPi / 3.0);
    set.fillSharp = custom(fillSharp);

    set.fillDiffuse = matchKey();

    // dark -- `0x00064888`-`0x00064910`. The only member with an
    // `outsetOpacity`, which is what `0x000494F8` needs before it will draw
    // outside.
    HighlightSettings dark = member(0.0, sbv(1.0, 1.0, 0.2, 0.2), sbv(4.0, 6.0, 6.0, 6.0),
                                    sbv(kPi / 2.0, kPi / 2.0, kPi, kPi), 0.2);
    dark.outsetOpacity = sbv(0.9, 0.9, 0.2, 0.2);
    dark.minDistancePixels = flat(1.0);
    set.dark = some(dark);

    set.rim = none();
    return set;
}

// ---- generation 26's sets ------------------------------------------------------
//
// `[BIN]` `chicletDefault` (`0x772BC`-`0x7745C`) and `chicletDim`
// (`0x775BC`-`0x776F8`). Three members each: `keySharp`, `fillSharp` as a custom
// payload, and the rim. `keyDiffuse` and `dark` are nil, `fillDiffuse` is
// `.custom(nil)`. Every present member carries `blendModeOverride == .normal`
// (the byte `0`), the same four distances and a zero inset. The two sets differ
// in the three opacity tables and in nothing else.
HighlightsSet chiclet26(const HighlightSizeValue& keyOpacity, const HighlightSizeValue& fillOpacity,
                        const HighlightSizeValue& rimOpacity) {
    const HighlightSizeValue distance = sbv(22.0, 22.0, 30.0, 39.0);
    auto normal = [](HighlightSettings s) {
        s.hasBlendModeOverride = true;
        s.blendModeOverride = BlendMode::Normal;
        return s;
    };
    HighlightsSet set;
    set.keySharp = some(normal(member(1.1, keyOpacity, distance, flat(78.0 * kPi / 180.0), 0.5)));
    set.keyDiffuse = none();
    set.fillSharp = custom(normal(member(1.1, fillOpacity, distance, flat(65.0 * kPi / 180.0), 0.5)));
    set.fillDiffuse = customNil();
    set.dark = none();
    set.rim = some(normal(member(1.0, rimOpacity, distance, flat(kPi), 0.5)));
    return set;
}

// `[BIN]` `glyphsDefault` (`0x77870`-`0x77A10`), `glyphsBright`
// (`0x77AE8`-`0x77C4C`), `glyphsDim` (a byte copy of `glyphsDefault`, the
// `memcpy` at `0x77ACC`) and `glyphsScreened` (`0x77CC0`-`0x77E00`). Two members
// each: `keySharp` and the rim, sharing `distance`, `inset` and
// `minInsetPixels`. `keyDiffuse` and `dark` are nil and BOTH fills are
// `FillHighlights? == nil` (`0x15`). No override, no `outsetOpacity`, zero
// `minDistancePixels`. The rim's cone is `2 pi`, which the shader boundary reads
// as "no cone" (`highlightCone`).
HighlightsSet glyphs26(const HighlightSizeValue& keyOpacity, const HighlightSizeValue& rimOpacity,
                       const HighlightSizeValue& distance, const HighlightSizeValue& inset,
                       const HighlightSizeValue& minInsetPixels) {
    HighlightsSet set;
    HighlightSettings keySharp = member(1.0, keyOpacity, distance, flat(3.0 * kPi / 5.0), 0.5);
    keySharp.inset = inset;
    keySharp.minInsetPixels = minInsetPixels;
    set.keySharp = some(keySharp);
    set.keyDiffuse = none();
    set.fillSharp = noFill();
    set.fillDiffuse = noFill();
    set.dark = none();
    HighlightSettings rim = member(1.0, rimOpacity, distance, flat(2.0 * kPi), 0.5);
    rim.inset = inset;
    rim.minInsetPixels = minInsetPixels;
    set.rim = some(rim);
    return set;
}

RenderingParameters generation27() {
    RenderingParameters p;
    p.generation = DesignGeneration::G27;
    p.darkTintHighlightsBlendWithContent = true;
    // `automaticGradient`, `systemGradients`, `glass`, `thresholds`, `shadow` and
    // `glyphTranslucency` are the defaults of their own structs, which ARE this
    // generation: each of those headers carries the address it was read at.
    p.clearMode = ClearModeParameters{};
    p.useAdvancedStacking = true;
    p.shouldClampPlusLBlending = true;
    p.glow = std::nullopt;
    p.outlineUsesDynamicOpacity = true;
    p.supportsChicletAlignmentForSystemFills = true;
    p.recreateRadar153477135 = false;
    p.useOS26Compositing = false;

    HighlightParameters& h = p.highlights;
    h.defaultChicletLightLongitude = 0.0;
    h.defaultGlyphLightLongitude = 0.0;
    // `fmov v0.2d, #0.75`, stored four times over each (`0x00062AD0`-`0x00062AE0`).
    h.glyphHighlightCurvature = flat(0.75);
    h.glyphDarklightCurvature = flat(0.75);
    h.chicletHighlightCurvature = flat(0.75);
    h.chicletDarklightCurvature = flat(0.75);
    h.glyphHighlightsUseVCM = true;
    h.maxDimChicletLuminance = 0.2;
    h.minBrightChicletLuminance = 0.99;
    h.iconBrightnessOnlyUsesMax = false;
    h.glyphHighlightVCM = GlyphVCM{0.2, 1.2, 1.25, 0.0, true};
    h.glyphDarklightVCM = GlyphVCM{-0.15, 0.7, 1.25, 0.0, true};
    h.glyphHighlightNonVCMScale = 1.0;
    h.glyphDarklightNonVCMScale = 1.0;
    h.chicletHighlightsAppearanceMode = ChicletHighlightsAppearanceMode::ChicletLuminance;

    const auto at = [](HighlightsSetKind k) { return static_cast<int>(k); };
    h.chiclet[at(HighlightsSetKind::Default)] = chicletPlain27();
    h.chiclet[at(HighlightsSetKind::Bright)] = chicletPlain27();
    h.chiclet[at(HighlightsSetKind::Dim)] = chicletPlain27();
    h.chiclet[at(HighlightsSetKind::Clear)] = chicletClear();
    h.chiclet[at(HighlightsSetKind::Screened)] = chicletScreened();
    for (HighlightsSet& set : h.glyphs) set = glyphs27();
    return p;
}

// `[BIN]` `0x76FC0` over a copy of the block above. A field this does not
// assign keeps generation 27's value, which is what the target's function does
// by not writing it.
RenderingParameters generation26() {
    RenderingParameters p = generation27();
    p.generation = DesignGeneration::G26;

    p.darkTintHighlightsBlendWithContent = false;        // 0x77084

    // `0x770B0` and `0x770BC`. `saturationBoost` is rewritten with the value it
    // had.
    p.automaticGradient.basePosition = 0.3;
    p.automaticGradient.saturationBoost = 0.2;
    p.automaticGradient.dimLightening = 0.2;
    p.automaticGradient.midDimLightening = 0.25;
    p.automaticGradient.midBrightLightening = 0.3;
    p.automaticGradient.brightLightening = -0.1;

    // `0x770F8`-`0x77124` and `0x77154`-`0x77190`.
    p.systemGradients.light[0] = 1.0;
    p.systemGradients.light[1] = 0.925;
    p.systemGradients.dark[0] = 0.19215686274509805;     // 49 / 255
    p.systemGradients.dark[1] = 0.0784313725490196;      // 20 / 255

    p.clearMode = std::nullopt;                          // 0x77064, the tag `2`
    p.useAdvancedStacking = false;                       // 0x77F68
    p.glass.refractionStrengthMax = 0.0;                 // 0x77F64
    p.shouldClampPlusLBlending = false;                  // 0x77080
    p.thresholds.minDisplaySize = 128.0;                 // 0x77098
    p.glow = GlowParameters{0.5, 42.38, 0.03};           // 0x77E5C-0x77E64

    // The `Shadow` box, `0x77E80`-`0x77ED0`. `overdrawBlendMode` and the three
    // overdraw fields are rewritten with the values they had.
    ShadowParameters& s = p.shadow;
    s.offsetX = 16.0;
    s.offsetY = 16.0;
    s.ringWidth = std::nullopt;                          // 0x77E88, the tag `1`
    s.radius = SizeBasedValue{{0.35, 0.35, 0.35, 0.35}};
    s.vibrantOpacity = SizeBasedValue{{0.5, 0.5, 0.5, 0.5}};
    s.neutralOpacity = SizeBasedValue{{0.1, 0.1, 0.1, 0.1}};
    s.blendMode = BlendMode::PlusDarker;                 // 0x77EBC
    s.blendModeForVibrantOnDim = BlendMode::PlusDarker;  // 0x77EC0
    s.overdrawBlendMode = BlendMode::Multiply;           // 0x77EC8
    s.vibrantBrightness = 1.0;                           // 0x77ECC
    s.ignoreFillOpacity = false;                         // 0x77ED0
    s.drawOverContent = false;                           // 0x77ED0

    // `glyphTranslucency`, `0x77F18`-`0x77F40`. `replicateBadSmoothing`, the
    // four strengths and the two body opacities are rewritten unchanged.
    TranslucencyEffect& t = p.glyphTranslucency;
    t.borderWidth = 25.8;                                // 0x77F18
    t.useSimpleMask = false;                             // 0x77F20
    t.lowerContourOpacity = 0.61;                        // 0x77F3C
    t.upperContourOpacity = 0.2;                         // 0x77F40

    p.outlineUsesDynamicOpacity = false;                 // 0x77F54
    p.supportsChicletAlignmentForSystemFills = false;    // 0x7707C
    p.recreateRadar153477135 = true;                     // 0x7707C
    p.useOS26Compositing = true;                         // 0x77074

    HighlightParameters& h = p.highlights;
    h.defaultChicletLightLongitude = -kPi / 4.0;         // 0x771E0
    h.defaultGlyphLightLongitude = -kPi / 4.0;           // 0x771B8
    h.glyphHighlightCurvature = sbv(0.8, 0.0, 0.0, 0.0); // 0x77214, 0x77218
    h.chicletHighlightCurvature = flat(1.0);             // 0x77244
    h.chicletDarklightCurvature = flat(1.0);             // 0x77270
    h.glyphHighlightsUseVCM = false;                     // 0x77820
    h.iconBrightnessOnlyUsesMax = true;                  // 0x77848
    h.chicletHighlightsAppearanceMode =
        ChicletHighlightsAppearanceMode::SystemAppearance;   // 0x77298

    const auto at = [](HighlightsSetKind k) { return static_cast<int>(k); };
    h.chiclet[at(HighlightsSetKind::Default)] =
        chiclet26(sbv(0.6, 0.6, 0.5, 0.4), sbv(0.4, 0.4, 0.3, 0.25), sbv(0.08, 0.08, 0.06, 0.04));
    h.chiclet[at(HighlightsSetKind::Dim)] =
        chiclet26(sbv(0.4, 0.4, 0.3, 0.2), sbv(0.25, 0.25, 0.2, 0.15), sbv(0.05, 0.05, 0.04, 0.03));

    const HighlightSizeValue distance = sbv(12.0, 12.0, 10.0, 10.0);
    const HighlightSizeValue inset = sbv(6.0, 0.0, 2.0, 3.0);
    const HighlightSizeValue minInset = sbv(1.0, 0.0, 0.0, 0.0);
    const HighlightSizeValue rim = sbv(0.08, 0.05, 0.05, 0.03);
    h.glyphs[at(HighlightsSetKind::Default)] =
        glyphs26(sbv(0.3, 0.18, 0.2, 0.2), rim, distance, inset, minInset);
    h.glyphs[at(HighlightsSetKind::Bright)] =
        glyphs26(sbv(0.2, 0.2, 0.15, 0.1), sbv(0.04, 0.04, 0.03, 0.0), sbv(12.0, 7.0, 9.0, 9.0),
                 sbv(6.0, 8.0, 10.0, 12.0), sbv(1.0, 1.0, 0.0, 0.0));
    h.glyphs[at(HighlightsSetKind::Dim)] = h.glyphs[at(HighlightsSetKind::Default)];
    h.glyphs[at(HighlightsSetKind::Screened)] = glyphs26(flat(0.3), rim, distance, inset, minInset);
    return p;
}

// The ten expansions of one generation, in `family * 5 + kind` order.
struct Expanded {
    std::vector<HighlightSlot> lists[10];
};

Expanded expandAll(const RenderingParameters& p) {
    Expanded e;
    const HighlightParameters& h = p.highlights;
    for (int k = 0; k < 5; ++k) {
        // `[BIN]` The curvature pair is the family's: `Highlights+0x50`/`+0x70`
        // for the chiclet (`0x00062770`-`0x0006278C`), `+0x10`/`+0x30` for the
        // glyph (`0x0006297C`-`0x00062998`).
        e.lists[k] = expandHighlights(h.chiclet[k], h.chicletHighlightCurvature,
                                      h.chicletDarklightCurvature);
        e.lists[5 + k] =
            expandHighlights(h.glyphs[k], h.glyphHighlightCurvature, h.glyphDarklightCurvature);
    }
    return e;
}

}  // namespace

IconSizeClass sizeClassFor(double minSidePoints, const SizeClassThresholds& t) {
    if (minSidePoints < t.minMediumSize) return IconSizeClass::Small;
    if (minSidePoints < t.minLargeSize) return IconSizeClass::Medium;
    if (minSidePoints < t.minDisplaySize) return IconSizeClass::Large;
    return IconSizeClass::Display;
}

const RenderingParameters& renderingParameters(DesignGeneration generation) {
    static const RenderingParameters g27 = generation27();
    static const RenderingParameters g26 = generation26();
    return generation == DesignGeneration::G26 ? g26 : g27;
}

const std::vector<HighlightSlot>& expandedHighlights(DesignGeneration generation,
                                                     HighlightFamily family,
                                                     HighlightsSetKind kind) {
    static const Expanded e27 = expandAll(renderingParameters(DesignGeneration::G27));
    static const Expanded e26 = expandAll(renderingParameters(DesignGeneration::G26));
    const Expanded& e = generation == DesignGeneration::G26 ? e26 : e27;
    const int k = static_cast<int>(kind);
    return e.lists[(family == HighlightFamily::Glyph ? 5 : 0) + (k < 0 ? 0 : (k > 4 ? 4 : k))];
}

}  // namespace rb
