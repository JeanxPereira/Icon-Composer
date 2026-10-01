// The two design generations: what `0x76FC0` rewrites, and what each consumer
// does with it.
//
// Every expectation in this file is a reading of
// `References/2.0-125/out/slices/IconRendering.arm64` -- a value of one of the
// two parameter blocks as `0x5E844` and `0x76FC0` leave them, or the arithmetic
// of the function that consumes it -- and never a number taken off a render.
// The address beside each one is where it was read.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/AutomaticGradient.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderingParameters.h"
#include "Source/RenderBox/SvgRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace rb;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tol = 1e-12) { return std::fabs(a - b) < tol; }

const std::vector<HighlightSlot>& glyphs(DesignGeneration g, HighlightsSetKind k) {
    return expandedHighlights(g, HighlightFamily::Glyph, k);
}
const std::vector<HighlightSlot>& chiclet(DesignGeneration g, HighlightsSetKind k) {
    return expandedHighlights(g, HighlightFamily::Chiclet, k);
}

bool slotsAre(const HighlightSizeValue& v, double display, double large, double medium,
              double small) {
    return v.slots[0] == display && v.slots[1] == large && v.slots[2] == medium &&
           v.slots[3] == small;
}

// One device for the cases that render, made on first use.
Device& gpuDevice() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

const HighlightsSetKind kAllKinds[5] = {HighlightsSetKind::Default, HighlightsSetKind::Bright,
                                        HighlightsSetKind::Dim, HighlightsSetKind::Clear,
                                        HighlightsSetKind::Screened};

}  // namespace

// ---- the block ---------------------------------------------------------------

// `[BIN]` The top-level fields `0x76FC0` rewrites, each with the address of the
// store, and the ones it must NOT have moved. A transcription that built
// generation 26 from scratch instead of from a copy would get the second half
// wrong without any of the first half noticing.
TEST_CASE(generation_26_is_generation_27_with_the_fields_0x76FC0_writes) {
    const RenderingParameters& g27 = renderingParameters(DesignGeneration::G27);
    const RenderingParameters& g26 = renderingParameters(DesignGeneration::G26);
    CHECK(g27.generation == DesignGeneration::G27);
    CHECK(g26.generation == DesignGeneration::G26);
    // The raw values of `ICRDesignGeneration`.
    CHECK_EQ(static_cast<int>(DesignGeneration::G26), 1);
    CHECK_EQ(static_cast<int>(DesignGeneration::G27), 2);

    // `+0x48`, `0x77084`.
    CHECK(g27.darkTintHighlightsBlendWithContent);
    CHECK(!g26.darkTintHighlightsBlendWithContent);

    // `+0x50`..`+0x78`, `0x770B0` / `0x770BC`.
    CHECK_EQ(g27.automaticGradient.basePosition, 0.0);
    CHECK_EQ(g27.automaticGradient.dimLightening, 0.04);
    CHECK_EQ(g27.automaticGradient.midDimLightening, 0.08);
    CHECK_EQ(g27.automaticGradient.midBrightLightening, 0.15);
    CHECK_EQ(g27.automaticGradient.brightLightening, -0.05);
    CHECK_EQ(g26.automaticGradient.basePosition, 0.3);
    CHECK_EQ(g26.automaticGradient.saturationBoost, 0.2);
    CHECK_EQ(g26.automaticGradient.dimLightening, 0.2);
    CHECK_EQ(g26.automaticGradient.midDimLightening, 0.25);
    CHECK_EQ(g26.automaticGradient.midBrightLightening, 0.3);
    CHECK_EQ(g26.automaticGradient.brightLightening, -0.1);

    // `+0x120`: the Optional's tag byte at `+0x169` is `2` in generation 26.
    CHECK(g27.clearMode.has_value());
    CHECK(!g26.clearMode.has_value());
    CHECK_EQ(g27.clearMode->contentLighteningStrength, 0.85);
    CHECK(g27.clearMode->applyToLightTintToo);

    // `+0x1D0` (`0x77F68`), `+0x208` (`0x77F64`), `+0x220` (`0x77080`).
    CHECK(g27.useAdvancedStacking);
    CHECK(!g26.useAdvancedStacking);
    CHECK_EQ(g27.glass.refractionStrengthMax, 640.0);
    CHECK_EQ(g26.glass.refractionStrengthMax, 0.0);
    CHECK_EQ(g26.glass.refractionHeightMax, 256.0);   // untouched
    CHECK(g27.shouldClampPlusLBlending);
    CHECK(!g26.shouldClampPlusLBlending);

    // `+0x290`: nil in 27, three doubles in 26 (`0x77E5C`-`0x77E64`).
    CHECK(!g27.glow.has_value());
    REQUIRE(g26.glow.has_value());
    CHECK_EQ(g26.glow->bias, 0.5);
    CHECK_EQ(g26.glow->innerRadius, 42.38);
    CHECK_EQ(g26.glow->innerOpacity, 0.03);

    // The `Shadow` box, `0x77E80`-`0x77ED0`.
    CHECK_EQ(g27.shadow.offsetX, 0.0);
    CHECK_EQ(g27.shadow.offsetY, 32.0);
    CHECK(g27.shadow.ringWidth.has_value());
    CHECK_EQ(g26.shadow.offsetX, 16.0);
    CHECK_EQ(g26.shadow.offsetY, 16.0);
    CHECK(!g26.shadow.ringWidth.has_value());
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(g26.shadow.radius.slots[k], 0.35);
        CHECK_EQ(g26.shadow.vibrantOpacity.slots[k], 0.5);
        CHECK_EQ(g26.shadow.neutralOpacity.slots[k], 0.1);
        // untouched
        CHECK_EQ(g26.shadow.maxNeutralOverdrawOpacity.slots[k], 0.2);
        CHECK_EQ(g26.shadow.maxVibrantOverdrawOpacity.slots[k], 0.5);
    }
    CHECK(g26.shadow.blendMode == BlendMode::PlusDarker);
    CHECK(g26.shadow.blendModeForVibrantOnDim == BlendMode::PlusDarker);
    CHECK(g26.shadow.overdrawBlendMode == BlendMode::Multiply);
    CHECK(g27.shadow.blendMode == BlendMode::Multiply);
    CHECK(g27.shadow.blendModeForVibrantOnDim == BlendMode::Normal);
    CHECK_EQ(g26.shadow.vibrantBrightness, 1.0);
    CHECK(!g26.shadow.ignoreFillOpacity);
    CHECK(!g26.shadow.drawOverContent);
    CHECK_EQ(g26.shadow.translucencyForMaxOverdraw, 0.3);

    // `_outlines+0x68` (`0x77F54`) and the three trailing bytes (`0x7707C`,
    // `0x77074`).
    CHECK(g27.outlineUsesDynamicOpacity);
    CHECK(!g26.outlineUsesDynamicOpacity);
    CHECK(g27.supportsChicletAlignmentForSystemFills);
    CHECK(!g27.recreateRadar153477135);
    CHECK(!g27.useOS26Compositing);
    CHECK(!g26.supportsChicletAlignmentForSystemFills);
    CHECK(g26.recreateRadar153477135);
    CHECK(g26.useOS26Compositing);

    // The `Highlights` preamble.
    const HighlightParameters& h27 = g27.highlights;
    const HighlightParameters& h26 = g26.highlights;
    CHECK_EQ(h27.defaultGlyphLightLongitude, 0.0);
    CHECK_EQ(h27.defaultChicletLightLongitude, 0.0);
    CHECK_EQ(h26.defaultGlyphLightLongitude, -kPi / 4.0);     // 0x771B8
    CHECK_EQ(h26.defaultChicletLightLongitude, -kPi / 4.0);   // 0x771E0
    CHECK(slotsAre(h27.glyphHighlightCurvature, 0.75, 0.75, 0.75, 0.75));
    CHECK(slotsAre(h26.glyphHighlightCurvature, 0.8, 0.0, 0.0, 0.0));   // 0x77214
    CHECK(slotsAre(h26.glyphDarklightCurvature, 0.75, 0.75, 0.75, 0.75));   // untouched
    CHECK(slotsAre(h26.chicletHighlightCurvature, 1.0, 1.0, 1.0, 1.0));     // 0x77244
    CHECK(slotsAre(h26.chicletDarklightCurvature, 1.0, 1.0, 1.0, 1.0));     // 0x77270
    CHECK(h27.glyphHighlightsUseVCM);
    CHECK(!h26.glyphHighlightsUseVCM);                        // 0x77820
    CHECK(!h27.iconBrightnessOnlyUsesMax);
    CHECK(h26.iconBrightnessOnlyUsesMax);                     // 0x77848
    CHECK(h27.chicletHighlightsAppearanceMode == ChicletHighlightsAppearanceMode::ChicletLuminance);
    CHECK(h26.chicletHighlightsAppearanceMode ==
          ChicletHighlightsAppearanceMode::SystemAppearance);   // 0x77298
    // untouched
    CHECK_EQ(h26.maxDimChicletLuminance, 0.2);
    CHECK_EQ(h26.minBrightChicletLuminance, 0.99);
    CHECK_EQ(h26.glyphHighlightVCM.lumaFloor, 0.2);
    CHECK_EQ(h26.glyphHighlightVCM.lumaCeiling, 1.2);
    CHECK_EQ(h26.glyphDarklightVCM.lumaFloor, -0.15);
    CHECK_EQ(h26.glyphHighlightNonVCMScale, 1.0);
}

// ---- H1: the size class --------------------------------------------------------

// `[BIN]` `0x42D54`-`0x42DE4`: three `b.pl` comparisons of `min(w, h)` against
// 25, 60 and `minDisplaySize`. The third threshold is the only one the
// generations disagree about (`0x77098`), and a side EQUAL to a threshold is in
// the class above it.
TEST_CASE(the_size_class_ladder_and_the_threshold_generation_26_moves) {
    const SizeClassThresholds t27 = renderingParameters(DesignGeneration::G27).thresholds;
    const SizeClassThresholds t26 = renderingParameters(DesignGeneration::G26).thresholds;
    CHECK_EQ(t27.minMediumSize, 25.0);
    CHECK_EQ(t27.minLargeSize, 60.0);
    CHECK_EQ(t27.minDisplaySize, 256.0);
    CHECK_EQ(t26.minMediumSize, 25.0);
    CHECK_EQ(t26.minLargeSize, 60.0);
    CHECK_EQ(t26.minDisplaySize, 128.0);

    for (const SizeClassThresholds& t : {t27, t26}) {
        CHECK(sizeClassFor(0.0, t) == IconSizeClass::Small);
        CHECK(sizeClassFor(24.999, t) == IconSizeClass::Small);
        CHECK(sizeClassFor(25.0, t) == IconSizeClass::Medium);
        CHECK(sizeClassFor(59.999, t) == IconSizeClass::Medium);
        CHECK(sizeClassFor(60.0, t) == IconSizeClass::Large);
        CHECK(sizeClassFor(127.999, t) == IconSizeClass::Large);
        CHECK(sizeClassFor(256.0, t) == IconSizeClass::Display);
        CHECK(sizeClassFor(1024.0, t) == IconSizeClass::Display);
    }
    // Between the two thresholds the generations part.
    CHECK(sizeClassFor(128.0, t27) == IconSizeClass::Large);
    CHECK(sizeClassFor(128.0, t26) == IconSizeClass::Display);
    CHECK(sizeClassFor(255.999, t27) == IconSizeClass::Large);
    CHECK(sizeClassFor(255.999, t26) == IconSizeClass::Display);

    // `[INF]` The 206 pt reference rect: `large` under 256, `display` under
    // 128. That is the class a render runs with when the caller names none.
    CHECK(sizeClassFor(kReferenceIconSidePoints, t27) == IconSizeClass::Large);
    CHECK(sizeClassFor(kReferenceIconSidePoints, t26) == IconSizeClass::Display);
    IconRenderOptions io;
    CHECK(!io.sizeClass.has_value());
    CHECK(io.generation == DesignGeneration::G27);
    CHECK(effectiveSizeClass(io) == IconSizeClass::Large);
    io.generation = DesignGeneration::G26;
    CHECK(effectiveSizeClass(io) == IconSizeClass::Display);
    // The caller's choice outranks both.
    io.sizeClass = IconSizeClass::Small;
    CHECK(effectiveSizeClass(io) == IconSizeClass::Small);
    io.generation = DesignGeneration::G27;
    CHECK(effectiveSizeClass(io) == IconSizeClass::Small);
}

// ---- H2: the expander ----------------------------------------------------------

// `[BIN]` `0x00030E88` over the ten sets of each generation: how many
// highlights each one draws. The counts are the expander's own, run over the
// two blocks. Generation 27's three plain chiclet sets keep all seven (the rim
// is present at opacity zero); a `matchKey` fill doubles the key; a nil member
// leaves the array.
TEST_CASE(the_expander_yields_the_counts_the_binary_yields_for_all_twenty_sets) {
    using K = HighlightsSetKind;
    const DesignGeneration g27 = DesignGeneration::G27, g26 = DesignGeneration::G26;
    CHECK_EQ(chiclet(g27, K::Default).size(), std::size_t{7});
    CHECK_EQ(chiclet(g27, K::Bright).size(), std::size_t{7});
    CHECK_EQ(chiclet(g27, K::Dim).size(), std::size_t{7});
    CHECK_EQ(chiclet(g27, K::Clear).size(), std::size_t{6});
    CHECK_EQ(chiclet(g27, K::Screened).size(), std::size_t{6});
    for (K k : kAllKinds) CHECK_EQ(glyphs(g27, k).size(), std::size_t{6});

    CHECK_EQ(chiclet(g26, K::Default).size(), std::size_t{3});
    CHECK_EQ(chiclet(g26, K::Bright).size(), std::size_t{7});     // untouched by 0x76FC0
    CHECK_EQ(chiclet(g26, K::Dim).size(), std::size_t{3});
    CHECK_EQ(chiclet(g26, K::Clear).size(), std::size_t{6});      // untouched
    CHECK_EQ(chiclet(g26, K::Screened).size(), std::size_t{6});   // untouched
    CHECK_EQ(glyphs(g26, K::Default).size(), std::size_t{2});
    CHECK_EQ(glyphs(g26, K::Bright).size(), std::size_t{2});
    CHECK_EQ(glyphs(g26, K::Dim).size(), std::size_t{2});
    CHECK_EQ(glyphs(g26, K::Clear).size(), std::size_t{6});       // untouched
    CHECK_EQ(glyphs(g26, K::Screened).size(), std::size_t{2});
}

// `[BIN]` The ORDER, and what the expander attaches to each slot: the angle
// from the key (`0`, `0`, `+pi`, `+pi`, `+pi/2`, `-pi/2`, `0`), the curvature
// (the parameter for the sharp ones and the rim, the literal `[1, 1, 1, 1]` for
// the two diffuse ones, the darklight parameter for the two dark ones) and the
// darklight bit.
TEST_CASE(the_expander_orders_the_slots_and_attaches_angle_curvature_and_the_dark_bit) {
    // Generation 27, a glyph set: six, the fourth being `keyDiffuse` matched.
    const std::vector<HighlightSlot>& g = glyphs(DesignGeneration::G27, HighlightsSetKind::Default);
    REQUIRE(g.size() == 6);
    const double angles[6] = {0.0, 0.0, kPi, kPi, kPi / 2.0, -kPi / 2.0};
    const bool dark[6] = {false, false, false, false, true, true};
    const double curvature[6] = {0.75, 1.0, 0.75, 1.0, 0.75, 0.75};
    for (int i = 0; i < 6; ++i) {
        CHECK_EQ(g[i].angleFromKey, angles[i]);
        CHECK_EQ(g[i].isDarklight, dark[i]);
        for (int k = 0; k < 4; ++k) CHECK_EQ(g[i].curvature.slots[k], curvature[i]);
    }
    // keySharp, keyDiffuse, fillSharp (the narrower cone), keyDiffuse again.
    CHECK(slotsAre(g[0].settings.distance, 4.0, 6.0, 6.0, 6.0));
    CHECK_EQ(g[0].settings.spread.slots[0], kPi / 2.0);
    CHECK(slotsAre(g[1].settings.distance, 16.0, 24.0, 24.0, 24.0));
    CHECK_EQ(g[2].settings.spread.slots[0], kPi / 3.0);
    CHECK(slotsAre(g[2].settings.opacity, 1.0, 1.0, 1.0, 0.3));
    CHECK(slotsAre(g[3].settings.distance, 16.0, 24.0, 24.0, 24.0));
    CHECK(slotsAre(g[3].settings.opacity, 1.0, 1.0, 1.0, 1.0));
    CHECK_EQ(g[3].settings.bias, 0.08);
    // The dark pair: the only member with an outset opacity, and a cone that
    // opens to pi in the two small classes.
    CHECK(slotsAre(g[4].settings.outsetOpacity, 0.9, 0.9, 0.2, 0.2));
    CHECK(g[4].settings.outsetOpacity.present);
    CHECK(slotsAre(g[4].settings.spread, kPi / 2.0, kPi / 2.0, kPi, kPi));
    CHECK_EQ(g[4].settings.bias, 0.2);

    // Generation 26, `glyphsDefault`: keySharp and the rim, both at angle 0 and
    // both with the HIGHLIGHT curvature, which curves `display` alone.
    const std::vector<HighlightSlot>& d = glyphs(DesignGeneration::G26, HighlightsSetKind::Default);
    REQUIRE(d.size() == 2);
    for (const HighlightSlot& s : d) {
        CHECK_EQ(s.angleFromKey, 0.0);
        CHECK(!s.isDarklight);
        CHECK(slotsAre(s.curvature, 0.8, 0.0, 0.0, 0.0));
        CHECK_EQ(s.settings.brightness, 1.0);
        CHECK_EQ(s.settings.bias, 0.5);
        CHECK(!s.settings.hasBlendModeOverride);
        CHECK(!s.settings.outsetOpacity.present);
        CHECK(slotsAre(s.settings.minDistancePixels, 0.0, 0.0, 0.0, 0.0));
        CHECK(slotsAre(s.settings.distance, 12.0, 12.0, 10.0, 10.0));
        CHECK(slotsAre(s.settings.inset, 6.0, 0.0, 2.0, 3.0));
        CHECK(s.settings.minInsetPixels.present);
        CHECK(slotsAre(s.settings.minInsetPixels, 1.0, 0.0, 0.0, 0.0));
    }
    CHECK(slotsAre(d[0].settings.opacity, 0.3, 0.18, 0.2, 0.2));
    CHECK(slotsAre(d[1].settings.opacity, 0.08, 0.05, 0.05, 0.03));
    for (int k = 0; k < 4; ++k) {
        CHECK_EQ(d[0].settings.spread.slots[k], 3.0 * kPi / 5.0);
        CHECK_EQ(d[1].settings.spread.slots[k], 2.0 * kPi);
    }

    // `glyphsDim` is a byte copy of `glyphsDefault` (the `memcpy` at `0x77ACC`).
    const std::vector<HighlightSlot>& dim = glyphs(DesignGeneration::G26, HighlightsSetKind::Dim);
    REQUIRE(dim.size() == 2);
    CHECK(slotsAre(dim[0].settings.opacity, 0.3, 0.18, 0.2, 0.2));
    CHECK(slotsAre(dim[1].settings.opacity, 0.08, 0.05, 0.05, 0.03));
    CHECK(slotsAre(dim[0].settings.inset, 6.0, 0.0, 2.0, 3.0));

    // `glyphsBright`: its own four tables.
    const std::vector<HighlightSlot>& b = glyphs(DesignGeneration::G26, HighlightsSetKind::Bright);
    REQUIRE(b.size() == 2);
    CHECK(slotsAre(b[0].settings.opacity, 0.2, 0.2, 0.15, 0.1));
    CHECK(slotsAre(b[1].settings.opacity, 0.04, 0.04, 0.03, 0.0));
    for (const HighlightSlot& s : b) {
        CHECK(slotsAre(s.settings.distance, 12.0, 7.0, 9.0, 9.0));
        CHECK(slotsAre(s.settings.inset, 6.0, 8.0, 10.0, 12.0));
        CHECK(slotsAre(s.settings.minInsetPixels, 1.0, 1.0, 0.0, 0.0));
    }

    // `glyphsScreened`: `glyphsDefault` with a flat key opacity.
    const std::vector<HighlightSlot>& sc =
        glyphs(DesignGeneration::G26, HighlightsSetKind::Screened);
    REQUIRE(sc.size() == 2);
    CHECK(slotsAre(sc[0].settings.opacity, 0.3, 0.3, 0.3, 0.3));
    CHECK(slotsAre(sc[1].settings.opacity, 0.08, 0.05, 0.05, 0.03));
    CHECK(slotsAre(sc[0].settings.inset, 6.0, 0.0, 2.0, 3.0));

    // `glyphsClear` is NOT rewritten, so in generation 26 it is generation
    // 27's six members -- under generation 26's curvatures: `[0.8, 0, 0, 0]` on
    // the sharp ones, the literal on the diffuse ones, and the untouched
    // darklight `[0.75 x4]` on the two dark ones.
    const std::vector<HighlightSlot>& c = glyphs(DesignGeneration::G26, HighlightsSetKind::Clear);
    REQUIRE(c.size() == 6);
    CHECK(slotsAre(c[0].curvature, 0.8, 0.0, 0.0, 0.0));
    CHECK(slotsAre(c[1].curvature, 1.0, 1.0, 1.0, 1.0));
    CHECK(slotsAre(c[2].curvature, 0.8, 0.0, 0.0, 0.0));
    CHECK(slotsAre(c[3].curvature, 1.0, 1.0, 1.0, 1.0));
    CHECK(slotsAre(c[4].curvature, 0.75, 0.75, 0.75, 0.75));
    CHECK(slotsAre(c[5].curvature, 0.75, 0.75, 0.75, 0.75));
    CHECK(c[4].isDarklight && c[5].isDarklight);
}

// `[BIN]` The three states of a fill member, through the expander alone: nil
// (`0x15`) leaves no slot, `matchKey` (`0x14`) copies the key member of the same
// sharpness, and a custom payload that is itself nil (`0x13`) leaves no slot
// either. And the byte at `x1+0x20` that swaps the dark curvature.
TEST_CASE(a_fill_member_is_nil_or_the_key_or_its_own_payload) {
    HighlightSizeValue hc, dc;
    for (int k = 0; k < 4; ++k) {
        hc.slots[k] = 0.25;
        dc.slots[k] = 0.5;
    }
    HighlightSettings key;
    key.brightness = 1.0;
    key.bias = 0.5;
    HighlightSettings diffuse = key;
    diffuse.bias = 0.08;
    HighlightSettings own = key;
    own.bias = 0.3;
    HighlightSettings darkSettings = key;
    darkSettings.brightness = 0.0;

    HighlightsSet set;
    set.keySharp = OptionalHighlight{true, key};
    set.keyDiffuse = OptionalHighlight{true, diffuse};
    set.dark = OptionalHighlight{true, darkSettings};

    // Both fills nil: key, key, dark, dark.
    std::vector<HighlightSlot> out = expandHighlights(set, hc, dc);
    REQUIRE(out.size() == 4);
    CHECK(out[2].isDarklight && out[3].isDarklight);
    CHECK_EQ(out[2].curvature.slots[0], 0.5);

    // `matchKey` on both: the sharp one copies `keySharp`, the diffuse one
    // `keyDiffuse` -- each at `+pi`.
    set.fillSharp.kind = FillHighlightsKind::MatchKey;
    set.fillDiffuse.kind = FillHighlightsKind::MatchKey;
    out = expandHighlights(set, hc, dc);
    REQUIRE(out.size() == 6);
    CHECK_EQ(out[2].settings.bias, 0.5);
    CHECK_EQ(out[3].settings.bias, 0.08);
    CHECK_EQ(out[2].angleFromKey, kPi);
    CHECK_EQ(out[3].angleFromKey, kPi);
    CHECK_EQ(out[2].curvature.slots[0], 0.25);
    CHECK_EQ(out[3].curvature.slots[0], 1.0);

    // A custom payload wins over the key, and a custom nil is no slot.
    set.fillSharp = FillHighlights{FillHighlightsKind::Custom, OptionalHighlight{true, own}};
    set.fillDiffuse = FillHighlights{FillHighlightsKind::Custom, OptionalHighlight{}};
    out = expandHighlights(set, hc, dc);
    REQUIRE(out.size() == 5);
    CHECK_EQ(out[2].settings.bias, 0.3);
    CHECK(out[3].isDarklight);

    // `matchKey` of a nil key is nil.
    set.keyDiffuse = OptionalHighlight{};
    set.fillDiffuse.kind = FillHighlightsKind::MatchKey;
    out = expandHighlights(set, hc, dc);
    REQUIRE(out.size() == 4);   // keySharp, fillSharp, dark, dark

    // The rim comes last and takes the highlight curvature.
    set.rim = OptionalHighlight{true, key};
    out = expandHighlights(set, hc, dc);
    REQUIRE(out.size() == 5);
    CHECK(!out[4].isDarklight);
    CHECK_EQ(out[4].angleFromKey, 0.0);
    CHECK_EQ(out[4].curvature.slots[0], 0.25);

    // `[BIN]` `0x000311C4`: with the byte set, the dark pair takes the
    // highlight curvature instead.
    out = expandHighlights(set, hc, dc, true);
    CHECK_EQ(out[2].curvature.slots[0], 0.25);
    CHECK_EQ(out[3].curvature.slots[0], 0.25);
}

// ---- H2: the resolver ----------------------------------------------------------

// `[BIN]` `0x0004BF0C`-`0x0004BFF0`: `inset = max(inset[k], minInsetPixels[k] *
// pixelUnit)`, with `pixelUnit` the canvas units of one pixel, and a nil
// `minInsetPixels` feeding `-infinity` so the `max` is the plain value. Here in
// pixels, which is the same inequality multiplied through by pixels per point.
TEST_CASE(the_resolved_inset_is_floored_in_pixels_per_size_class) {
    const std::vector<HighlightSlot>& def =
        glyphs(DesignGeneration::G26, HighlightsSetKind::Default);
    const std::vector<HighlightSlot>& bright =
        glyphs(DesignGeneration::G26, HighlightsSetKind::Bright);
    REQUIRE(def.size() == 2 && bright.size() == 2);

    SpecularArguments a;
    a.generation = DesignGeneration::G26;

    // 512 px: half a pixel per canvas point. `display` (k = 3): inset 6 points
    // is 3 px, above the 1 px floor.
    a.pixelsPerPoint = 0.5;
    a.sizeClass = IconSizeClass::Display;
    CHECK_EQ(resolveHighlight(def[0], a).inset, 3.0);
    CHECK_EQ(resolveHighlight(def[1], a).inset, 3.0);
    CHECK_EQ(resolveHighlight(def[0], a).height, 6.0);    // distance 12, no pixel floor
    // 64 px: a sixteenth. 6 points is 0.375 px, and the floor of 1 px wins.
    a.pixelsPerPoint = 1.0 / 16.0;
    CHECK_EQ(resolveHighlight(def[0], a).inset, 1.0);
    CHECK_EQ(resolveHighlight(def[0], a).height, 0.75);   // 12 / 16: `minDistancePixels` is 0

    // `large` (k = 2): inset 0 and floor 0 -- the band sits on the contour.
    a.sizeClass = IconSizeClass::Large;
    CHECK_EQ(resolveHighlight(def[0], a).inset, 0.0);
    // `medium` (k = 1) and `small` (k = 0): 2 and 3 points, no floor.
    a.sizeClass = IconSizeClass::Medium;
    CHECK_EQ(resolveHighlight(def[0], a).inset, 2.0 / 16.0);
    CHECK_EQ(resolveHighlight(def[0], a).height, 10.0 / 16.0);
    a.sizeClass = IconSizeClass::Small;
    CHECK_EQ(resolveHighlight(def[0], a).inset, 3.0 / 16.0);

    // `glyphsBright`: the floor is 1 px on `display` AND `large`.
    a.sizeClass = IconSizeClass::Large;
    CHECK_EQ(resolveHighlight(bright[0], a).inset, 1.0);            // 8 / 16 = 0.5 < 1
    CHECK_EQ(resolveHighlight(bright[0], a).height, 7.0 / 16.0);
    a.sizeClass = IconSizeClass::Medium;
    CHECK_EQ(resolveHighlight(bright[0], a).inset, 10.0 / 16.0);    // floor 0
    a.sizeClass = IconSizeClass::Small;
    CHECK_EQ(resolveHighlight(bright[0], a).inset, 12.0 / 16.0);
    a.pixelsPerPoint = 1.0;
    a.sizeClass = IconSizeClass::Large;
    CHECK_EQ(resolveHighlight(bright[0], a).inset, 8.0);            // 8 px > 1 px

    // The opacity is the table's entry for the class (times the two identity
    // multipliers), and the light is the one the caller names.
    a.sizeClass = IconSizeClass::Large;
    CHECK_EQ(resolveHighlight(def[0], a).opacity, 0.18);
    CHECK_EQ(resolveHighlight(def[1], a).opacity, 0.05);
    a.sizeClass = IconSizeClass::Display;
    CHECK_EQ(resolveHighlight(def[0], a).opacity, 0.3);
    CHECK_EQ(resolveHighlight(def[0], a).curvature, 0.8);
    a.sizeClass = IconSizeClass::Large;
    CHECK_EQ(resolveHighlight(def[0], a).curvature, 0.0);
    CHECK(resolveHighlight(def[0], a).blendMode == BlendMode::PlusLighter);

    // `[BIN]` theta = angleFromKey + longitude (`0x0004BEF8`); generation 26's
    // light is at -pi/4, so the key points up and to the LEFT.
    a.lightLongitude = renderingParameters(DesignGeneration::G26).highlights.defaultGlyphLightLongitude;
    const GlassHighlightSettings key = resolveHighlight(def[0], a);
    CHECK(near(key.directionX, -std::sqrt(0.5)));
    CHECK(near(key.directionY, std::sqrt(0.5)));

    // Generation 27 has no `minInsetPixels` anywhere: the inset is zero at any
    // scale, and it is the DISTANCE that has the pixel floor there.
    const std::vector<HighlightSlot>& g27 =
        glyphs(DesignGeneration::G27, HighlightsSetKind::Default);
    SpecularArguments b;
    b.pixelsPerPoint = 1.0 / 16.0;
    b.sizeClass = IconSizeClass::Large;
    for (const HighlightSlot& s : g27) CHECK_EQ(resolveHighlight(s, b).inset, 0.0);
    CHECK_EQ(resolveHighlight(g27[0], b).height, 1.0);   // max(6 / 16, 1)
    CHECK_EQ(resolveHighlight(g27[1], b).height, 4.0);   // max(24 / 16, 4)
    CHECK_EQ(resolveHighlight(g27[3], b).height, 4.0);   // the matched keyDiffuse
}

// ---- H2: the cone ----------------------------------------------------------------

// `[BIN]` `0x0000EE1C`-`0x0000EF5C`. Two conditions make "no cone at all": a
// spread past pi, and a spread whose FLOAT cosine is exactly -1 under a bias of
// exactly 1. Only the first was transcribed until 2026-10-01.
TEST_CASE(the_cone_sentinel_has_two_conditions) {
    // (Within a few ulps: the compiler folds `cos` of a constant exactly and
    // the C library at run time does not always round the same way.)
    CHECK(near(highlightCone(kPi / 2.0, 0.5), 0.0, 1e-15));
    CHECK(near(highlightCone(kPi / 3.0, 0.08), 0.5, 1e-15));
    // Past pi: generation 26's glyph rim, at 2 pi.
    CHECK_EQ(highlightCone(2.0 * kPi, 0.5), -1000.0);
    CHECK_EQ(highlightCone(std::nextafter(kPi, 4.0), 0.5), -1000.0);
    // Exactly pi: a cosine of -1, and the sentinel only when the bias is 1.
    CHECK_EQ(highlightCone(kPi, 0.5), -1.0);
    CHECK_EQ(highlightCone(kPi, 0.2), -1.0);
    CHECK_EQ(highlightCone(kPi, 1.0), -1000.0);
    // The comparison is on the float (`fcvt s14, d0`, `0x0000EE24`): a spread a
    // ten-thousandth SHORT of pi has a double cosine above -1 (by 5e-9) and a
    // float cosine of exactly -1.
    const double shy = kPi - 1e-4;
    CHECK(std::cos(shy) > -1.0);
    CHECK_EQ(static_cast<float>(std::cos(shy)), -1.0f);
    CHECK_EQ(highlightCone(shy, 1.0), -1000.0);
    CHECK(near(highlightCone(shy, 0.5), -1.0 + 5e-9, 1e-12));
    // A bias of 1 alone is not enough.
    CHECK(near(highlightCone(kPi / 2.0, 1.0), 0.0, 1e-15));

    // In the fragment: with the sentinel, `lit = (dot + 1000) / 1001` -- 1
    // facing the light and 999/1001 facing away; without it, a cone of pi is
    // `(dot + 1) / 2` -- 1 facing and 0 away.
    GlassHighlightSettings s;
    s.height = 100.0;
    s.curvature = 0.0;
    s.directionX = 0.0;
    s.directionY = 1.0;
    s.spread = kPi;
    s.bias = 0.5;   // bias' = 0: the denominator is 1
    CHECK(near(glassHighlightFragment(s, 10.0, 0.0, -1.0, 1.0), 1.0, 1e-9));
    CHECK(near(glassHighlightFragment(s, 10.0, 0.0, 1.0, 1.0), 0.0, 1e-9));
    s.spread = 2.0 * kPi;
    CHECK(near(glassHighlightFragment(s, 10.0, 0.0, 1.0, 1.0), 999.0 / 1001.0, 1e-9));
    // bias 1: bias' = 1/1 - 2 = -1, so `out = a / max(1 - (1 - a), eps) = 1`
    // wherever `a > 0`. With the second condition the far side is lit too.
    s.spread = kPi;
    s.bias = 1.0;
    CHECK(near(glassHighlightFragment(s, 10.0, 0.0, 1.0, 1.0), 1.0, 1e-9));
}

// `[BIN]` `0x0000EF60`-`0x0000EF68`: the curvature handed to the shader is zero
// when the inset is negative.
TEST_CASE(a_negative_inset_flattens_the_band) {
    GlassHighlightSettings s;
    s.height = 20.0;
    s.inset = -20.0;
    s.curvature = 1.0;
    s.spread = 2.0 * kPi;
    s.bias = 0.5;
    s.directionX = 0.0;
    s.directionY = 1.0;
    GlassHighlightSettings flat = s;
    flat.curvature = 0.0;
    // Deep in the band, where a curvature of 1 would have dimmed it by half.
    const double sd = -10.0;   // d = sd - inset = 10 = height / 2
    CHECK_EQ(glassHighlightFragment(s, sd, 0.0, -1.0, 1.0),
             glassHighlightFragment(flat, sd, 0.0, -1.0, 1.0));
    CHECK(near(glassHighlightFragment(s, sd, 0.0, -1.0, 1.0), 1.0, 1e-9));
    // And with a non-negative inset the curvature bites: shade = 1 - d/height.
    s.inset = 0.0;
    CHECK(near(glassHighlightFragment(s, 10.0, 0.0, -1.0, 1.0), 0.5, 1e-9));
}

// ---- H2: the glyph selector ------------------------------------------------------

// `[BIN]` `0x000627B4`: `.color` switches on `iconBrightness` (0, 1, anything
// else); every other rendering mode asks one question, whether the effective
// clear mode is nil.
TEST_CASE(the_glyph_selector_tree) {
    using K = HighlightsSetKind;
    for (bool clearNil : {true, false}) {
        CHECK(glyphHighlightsSetFor(true, ChicletAppearance::Default, clearNil) == K::Default);
        CHECK(glyphHighlightsSetFor(true, ChicletAppearance::Bright, clearNil) == K::Bright);
        CHECK(glyphHighlightsSetFor(true, ChicletAppearance::Dim, clearNil) == K::Dim);
    }
    for (ChicletAppearance a :
         {ChicletAppearance::Default, ChicletAppearance::Bright, ChicletAppearance::Dim}) {
        CHECK(glyphHighlightsSetFor(false, a, true) == K::Screened);
        CHECK(glyphHighlightsSetFor(false, a, false) == K::Clear);
    }
}

// ---- H2: the plain composite of generation 26 ------------------------------------

// `[BIN]` `glyphHighlightsUseVCM == false` with a nil clear mode is the branch
// `0x00049570` -> `0x0000E834`: the colour at `coverage x opacity`, under the
// highlight's own blend mode. One texel, mid-band, at 1 px per point and the
// `large` class, where the curvature is zero and the band is full:
//
//   facing away from the light   the key's cone (3 pi / 5) is dark; the rim's
//                                (2 pi -> -1000) is lit (999/1001); the texel
//                                gains `0.05 x 999/1001` by plus-lighter.
//   facing the light             the key adds `0.18` and the rim `0.05`.
TEST_CASE(generation_26_paints_the_key_and_a_full_ring_by_plus_lighter) {
    const RenderingParameters& p = renderingParameters(DesignGeneration::G26);
    SpecularArguments a;
    a.generation = DesignGeneration::G26;
    a.set = HighlightsSetKind::Default;
    a.sizeClass = IconSizeClass::Large;
    a.pixelsPerPoint = 1.0;
    a.lightLongitude = p.highlights.defaultGlyphLightLongitude;
    a.useVCM = p.highlights.glyphHighlightsUseVCM;
    CHECK(!a.useVCM);

    // The light's direction as the shader gets it, `(x, -y)`, for theta = -pi/4.
    const double lx = std::sin(-kPi / 4.0), ly = -std::cos(-kPi / 4.0);
    auto texel = [&](double nx, double ny) {
        FieldImage field;
        field.width = 1;
        field.height = 1;
        // `(d, gx, gy, coverage)`, the distance NEGATIVE inside: 6 px deep, the
        // middle of the 12 px band that starts on the contour.
        field.rgba = {-6.0f, static_cast<float>(nx), static_cast<float>(ny), 1.0f};
        std::vector<float> px = {0.25f, 0.25f, 0.25f, 1.0f};
        const std::size_t moved = drawSpecular(px, field, a);
        CHECK_EQ(moved, std::size_t{1});
        return px;
    };

    const std::vector<float> away = texel(-lx, -ly);
    const float rimOnly = static_cast<float>(0.25 + 0.05 * (999.0 / 1001.0));
    CHECK(std::fabs(away[0] - rimOnly) < 1e-6f);
    CHECK(std::fabs(away[1] - rimOnly) < 1e-6f);
    CHECK(std::fabs(away[2] - rimOnly) < 1e-6f);
    CHECK_EQ(away[3], 1.0f);

    const std::vector<float> facing = texel(lx, ly);
    CHECK(std::fabs(facing[0] - static_cast<float>(0.25 + 0.18 + 0.05)) < 1e-6f);
    CHECK_EQ(facing[3], 1.0f);

    // `glyphsBright` at `small`: the rim's opacity is 0 and only the key draws.
    a.set = HighlightsSetKind::Bright;
    a.sizeClass = IconSizeClass::Small;
    {
        FieldImage field;
        field.width = 1;
        field.height = 1;
        // inset 12, distance 9: mid-band is 16.5 px deep.
        field.rgba = {-16.5f, static_cast<float>(lx), static_cast<float>(ly), 1.0f};
        std::vector<float> px = {0.25f, 0.25f, 0.25f, 1.0f};
        CHECK_EQ(drawSpecular(px, field, a), std::size_t{1});
        CHECK(std::fabs(px[0] - static_cast<float>(0.25 + 0.1)) < 1e-6f);
        // Above the band -- shallower than the inset -- nothing is drawn.
        field.rgba[0] = -6.0f;
        std::vector<float> shallow = {0.25f, 0.25f, 0.25f, 1.0f};
        CHECK_EQ(drawSpecular(shallow, field, a), std::size_t{0});
        CHECK_EQ(shallow[0], 0.25f);
    }
}

// ---- H3: the chiclet ---------------------------------------------------------------

// `[BIN]` `0x00062588`. The first test is the appearance MODE: generation 27
// (`chicletLuminance`) walks the same tree as the glyph, generation 26
// (`systemAppearance`) asks only whether the style's appearance is dark.
TEST_CASE(the_chiclet_selector_tree_and_the_mode_that_bypasses_it) {
    using K = HighlightsSetKind;
    using M = ChicletHighlightsAppearanceMode;
    const ChicletAppearance kClasses[3] = {ChicletAppearance::Default, ChicletAppearance::Bright,
                                           ChicletAppearance::Dim};
    // Mode 1, `.color`: `iconBrightness` picks, whatever the appearance.
    for (bool dark : {false, true}) {
        CHECK(chicletHighlightsSetFor(M::ChicletLuminance, dark, true, ChicletAppearance::Default,
                                      true) == K::Default);
        CHECK(chicletHighlightsSetFor(M::ChicletLuminance, dark, true, ChicletAppearance::Bright,
                                      true) == K::Bright);
        CHECK(chicletHighlightsSetFor(M::ChicletLuminance, dark, true, ChicletAppearance::Dim,
                                      true) == K::Dim);
        // Mode 1, any other rendering mode: the effective clear mode picks.
        for (ChicletAppearance a : kClasses) {
            CHECK(chicletHighlightsSetFor(M::ChicletLuminance, dark, false, a, true) ==
                  K::Screened);
            CHECK(chicletHighlightsSetFor(M::ChicletLuminance, dark, false, a, false) == K::Clear);
        }
    }
    // Mode 0: the appearance alone -- not the rendering mode, not the class,
    // not the clear mode.
    for (bool colour : {false, true}) {
        for (bool clearNil : {false, true}) {
            for (ChicletAppearance a : kClasses) {
                CHECK(chicletHighlightsSetFor(M::SystemAppearance, true, colour, a, clearNil) ==
                      K::Dim);
                CHECK(chicletHighlightsSetFor(M::SystemAppearance, false, colour, a, clearNil) ==
                      K::Default);
            }
        }
    }
}

// `[BIN]` Generation 26's two rewritten chiclet sets (`0x772BC`-`0x7745C`,
// `0x775BC`-`0x776F8`): three highlights in the expander's order -- the key at
// 0, the fill at `+pi`, the rim at 0 -- each with the same four distances, a
// zero inset, the opacity PER SIZE CLASS and `blendModeOverride == .normal`;
// and the chiclet curvature of `[1 x4]` on all three (`0x77244`).
TEST_CASE(generation_26_chiclet_sets_are_three_normal_blended_highlights_per_size) {
    struct Want {
        HighlightsSetKind kind;
        double key[4], fill[4], rim[4];
    };
    const Want wants[2] = {
        {HighlightsSetKind::Default, {0.6, 0.6, 0.5, 0.4}, {0.4, 0.4, 0.3, 0.25},
         {0.08, 0.08, 0.06, 0.04}},
        {HighlightsSetKind::Dim, {0.4, 0.4, 0.3, 0.2}, {0.25, 0.25, 0.2, 0.15},
         {0.05, 0.05, 0.04, 0.03}},
    };
    for (const Want& w : wants) {
        const std::vector<HighlightSlot>& s = chiclet(DesignGeneration::G26, w.kind);
        REQUIRE(s.size() == 3);
        CHECK_EQ(s[0].angleFromKey, 0.0);
        CHECK_EQ(s[1].angleFromKey, kPi);
        CHECK_EQ(s[2].angleFromKey, 0.0);
        CHECK_EQ(s[0].settings.brightness, 1.1);
        CHECK_EQ(s[1].settings.brightness, 1.1);
        CHECK_EQ(s[2].settings.brightness, 1.0);
        for (int k = 0; k < 4; ++k) {
            CHECK_EQ(s[0].settings.opacity.slots[k], w.key[k]);
            CHECK_EQ(s[1].settings.opacity.slots[k], w.fill[k]);
            CHECK_EQ(s[2].settings.opacity.slots[k], w.rim[k]);
            CHECK_EQ(s[0].settings.spread.slots[k], 78.0 * kPi / 180.0);
            CHECK_EQ(s[1].settings.spread.slots[k], 65.0 * kPi / 180.0);
            CHECK_EQ(s[2].settings.spread.slots[k], kPi);
        }
        for (const HighlightSlot& h : s) {
            CHECK(!h.isDarklight);
            CHECK(slotsAre(h.settings.distance, 22.0, 22.0, 30.0, 39.0));
            CHECK(slotsAre(h.settings.inset, 0.0, 0.0, 0.0, 0.0));
            CHECK(slotsAre(h.settings.minDistancePixels, 0.0, 0.0, 0.0, 0.0));
            CHECK(!h.settings.minInsetPixels.present);
            CHECK(!h.settings.outsetOpacity.present);
            CHECK_EQ(h.settings.bias, 0.5);
            CHECK(h.settings.hasBlendModeOverride);
            CHECK(h.settings.blendModeOverride == BlendMode::Normal);
            CHECK(slotsAre(h.curvature, 1.0, 1.0, 1.0, 1.0));
        }
    }

    // Resolved (`0x0004BD90`): the override wins over the brightness rule, the
    // opacity and the distance are the class's, and the light is the CHICLET's.
    const std::vector<HighlightSlot>& def = chiclet(DesignGeneration::G26, HighlightsSetKind::Default);
    SpecularArguments a;
    a.generation = DesignGeneration::G26;
    a.pixelsPerPoint = 1.0;
    a.lightLongitude =
        renderingParameters(DesignGeneration::G26).highlights.defaultChicletLightLongitude;
    a.sizeClass = IconSizeClass::Small;
    GlassHighlightSettings key = resolveChicletHighlight(def[0], a);
    CHECK(key.blendMode == BlendMode::Normal);
    CHECK_EQ(key.opacity, 0.4);
    CHECK_EQ(key.height, 39.0);
    CHECK_EQ(key.inset, 0.0);
    CHECK_EQ(key.curvature, 1.0);
    CHECK_EQ(key.colour[0], 1.1);
    CHECK(near(key.directionX, -std::sqrt(0.5)));
    CHECK(near(key.directionY, std::sqrt(0.5)));
    a.sizeClass = IconSizeClass::Display;
    key = resolveChicletHighlight(def[0], a);
    CHECK_EQ(key.opacity, 0.6);
    CHECK_EQ(key.height, 22.0);
    // The fill points the other way: theta = pi - pi/4.
    const GlassHighlightSettings fill = resolveChicletHighlight(def[1], a);
    CHECK(near(fill.directionX, std::sqrt(0.5)));
    CHECK(near(fill.directionY, -std::sqrt(0.5)));
    CHECK_EQ(fill.opacity, 0.4);

    // Generation 27's plain chiclet sets are untouched by all of this: seven
    // flat members with no override, and the curvature of 0.75.
    const std::vector<HighlightSlot>& g27 = chiclet(DesignGeneration::G27, HighlightsSetKind::Default);
    REQUIRE(g27.size() == 7);
    for (const HighlightSlot& h : g27) CHECK(!h.settings.hasBlendModeOverride);
    CHECK(slotsAre(g27[0].curvature, 0.75, 0.75, 0.75, 0.75));
    CHECK(slotsAre(g27[0].settings.opacity, 0.2, 0.2, 0.2, 0.2));
    // `chicletBright` is not rewritten, so generation 26 keeps its seven
    // members -- under generation 26's curvature.
    const std::vector<HighlightSlot>& bright = chiclet(DesignGeneration::G26, HighlightsSetKind::Bright);
    REQUIRE(bright.size() == 7);
    CHECK(slotsAre(bright[0].settings.distance, 10.0, 10.0, 10.0, 10.0));
    CHECK(slotsAre(bright[0].curvature, 1.0, 1.0, 1.0, 1.0));
    CHECK(slotsAre(bright[4].curvature, 1.0, 1.0, 1.0, 1.0));   // chicletDarklightCurvature
    // `chicletClear`: both fills `matchKey`, the rim nil -- six.
    const std::vector<HighlightSlot>& clear = chiclet(DesignGeneration::G27, HighlightsSetKind::Clear);
    REQUIRE(clear.size() == 6);
    CHECK(slotsAre(clear[0].settings.opacity, 1.25, 1.25, 1.25, 1.25));
    CHECK(slotsAre(clear[2].settings.opacity, 1.25, 1.25, 1.25, 1.25));   // keySharp matched
    CHECK(slotsAre(clear[3].settings.distance, 44.0, 44.0, 44.0, 44.0));  // keyDiffuse matched
    CHECK(slotsAre(clear[4].settings.opacity, 0.25, 0.1, 0.1, 0.0));
    CHECK(slotsAre(clear[4].settings.distance, 9.0, 8.0, 8.0, 9.0));
}

// `[BIN]` The chiclet rasteriser, `0x0000D904`: a cone of exactly pi --
// `|spread / (2 pi) - 0.5| < 1e-6` -- is lit all the way round (`0x0000DC50`),
// there is no `spread > pi` sentinel, and `inset` is never read.
TEST_CASE(the_chiclet_rasteriser_lights_a_cone_of_pi_all_round_and_ignores_the_inset) {
    CHECK(chicletHighlightCone(kPi).alwaysLit);
    // The window is 1e-6 of a turn: 2 pi x 1e-6 = 6.28e-6 radians either side.
    CHECK(chicletHighlightCone(kPi + 5e-6).alwaysLit);
    CHECK(chicletHighlightCone(kPi - 5e-6).alwaysLit);
    CHECK(!chicletHighlightCone(kPi + 1e-5).alwaysLit);
    CHECK(!chicletHighlightCone(kPi - 1e-5).alwaysLit);
    CHECK(!chicletHighlightCone(78.0 * kPi / 180.0).alwaysLit);
    CHECK(near(chicletHighlightCone(78.0 * kPi / 180.0).cone, std::cos(78.0 * kPi / 180.0), 1e-15));
    // Past pi the cone is the plain cosine: the `-1000` of the glyph shader is
    // not in this function.
    CHECK(!chicletHighlightCone(2.0 * kPi).alwaysLit);
    CHECK(near(chicletHighlightCone(2.0 * kPi).cone, 1.0, 1e-15));
    CHECK_EQ(highlightCone(2.0 * kPi, 0.5), -1000.0);

    // The rim of `chicletDefault` in generation 26, `large`, 1 px per point:
    // height 22, curvature 1, bias 0.5 (so the denominator is 1). Mid-band, the
    // fragment is `lit x shade` with `shade = 1 - d/height = 0.5` -- and `lit`
    // is 1 facing the light AND facing away from it.
    const std::vector<HighlightSlot>& def = chiclet(DesignGeneration::G26, HighlightsSetKind::Default);
    REQUIRE(def.size() == 3);
    SpecularArguments a;
    a.generation = DesignGeneration::G26;
    a.pixelsPerPoint = 1.0;
    a.sizeClass = IconSizeClass::Large;
    const GlassHighlightSettings rim = resolveChicletHighlight(def[2], a);
    CHECK_EQ(rim.height, 22.0);
    CHECK_EQ(rim.opacity, 0.08);
    CHECK(near(chicletHighlightFragment(rim, 11.0, 0.0, -1.0), 0.5, 1e-9));
    CHECK(near(chicletHighlightFragment(rim, 11.0, 0.0, 1.0), 0.5, 1e-9));
    CHECK(near(chicletHighlightFragment(rim, 11.0, 1.0, 0.0), 0.5, 1e-9));
    // Through the cone of the glyph shader the same settings light half the
    // way round: `(dot + 1) / 2`, zero facing away.
    CHECK(near(glassHighlightFragment(rim, 11.0, 0.0, 1.0, 1.0), 0.0, 1e-9));

    // The inset: a slot that carried one would have its band moved by the
    // glyph resolver and not by the chiclet one.
    HighlightSlot inset = def[0];
    inset.settings.inset.slots[0] = inset.settings.inset.slots[1] = inset.settings.inset.slots[2] =
        inset.settings.inset.slots[3] = 7.0;
    CHECK_EQ(resolveHighlight(inset, a).inset, 7.0);
    CHECK_EQ(resolveChicletHighlight(inset, a).inset, 0.0);
    CHECK_EQ(resolveChicletHighlight(inset, a).height, resolveHighlight(inset, a).height);
}

// Generation 26 on a pastille, through `drawChicletHighlights`. The light sits
// at -pi/4 (`0x771E0`): up and to the LEFT. So the key (cone 78 degrees) lights
// the top and the left edges, the fill at +pi (cone 65 degrees, lower opacity)
// lights the bottom and the right ones, and the rim -- a cone of pi -- adds to
// all four. Every highlight is brighter than a mid grey and blends `normal`,
// so every edge gets LIGHTER; generation 27 darkens the two sides instead.
TEST_CASE(generation_26_chiclet_highlights_cover_every_edge_and_favour_the_top_left) {
    const std::uint32_t n = 256;
    auto pastille = [&] {
        std::vector<float> px(static_cast<std::size_t>(n) * n * 4, 0.0f);
        for (std::size_t i = 0; i < px.size(); i += 4) {
            px[i] = px[i + 1] = px[i + 2] = 0.5f;
            px[i + 3] = 1.0f;
        }
        clipToChiclet(px, n);
        return px;
    };
    auto lum = [&](const std::vector<float>& px, std::uint32_t x, std::uint32_t y) {
        return px[(static_cast<std::size_t>(y) * n + x) * 4];
    };
    const std::uint32_t mid = n / 2;

    SpecularArguments a;
    a.generation = DesignGeneration::G26;
    a.set = HighlightsSetKind::Default;
    a.sizeClass = IconSizeClass::Large;
    a.pixelsPerPoint = static_cast<double>(n) / 1024.0;
    a.lightLongitude =
        renderingParameters(DesignGeneration::G26).highlights.defaultChicletLightLongitude;
    std::vector<float> g26 = pastille();
    CHECK(drawChicletHighlights(g26, n, a) > 0);

    // The band is 22 x 256/1024 = 5.5 px deep; two pixels in is well inside it.
    const float top = lum(g26, mid, 2), bottom = lum(g26, mid, n - 3);
    const float left = lum(g26, 2, mid), right = lum(g26, n - 3, mid);
    CHECK(top > 0.5f);
    CHECK(bottom > 0.5f);
    CHECK(left > 0.5f);
    CHECK(right > 0.5f);
    CHECK(top > bottom);
    CHECK(left > right);
    // The light is on the diagonal, so the two key edges agree and so do the
    // two fill edges.
    CHECK(std::fabs(top - left) < 1e-3f);
    CHECK(std::fabs(bottom - right) < 1e-3f);
    // The middle is past every band.
    CHECK_EQ(lum(g26, mid, mid), 0.5f);

    // Generation 27 on the same pastille: the mirrored `dark` takes light OFF
    // the two sides, and the key at longitude 0 lights the top alone.
    SpecularArguments b;
    b.pixelsPerPoint = a.pixelsPerPoint;
    std::vector<float> g27 = pastille();
    CHECK(drawChicletHighlights(g27, n, b) > 0);
    CHECK(lum(g27, mid, 2) > 0.5f);
    CHECK(lum(g27, 0, mid) < 0.5f);
    CHECK(lum(g27, n - 1, mid) < 0.5f);
}

// ---- H4: the fills -----------------------------------------------------------------

// `[BIN]` `automatic-gradient` is one function with no generation branch
// (`0x5864`): the luminance picks one of four lightenings, a positive one lerps
// to white and anchors at 0 with the base at `1 - basePosition`, a negative one
// multiplies and anchors at 1 with the base at `basePosition`. The generation
// changes its six constants (`0x770B0`, `0x770BC`), and with them where the
// base stop sits: generation 27's `basePosition` is 0, generation 26's is 0.3.
TEST_CASE(automatic_gradient_under_the_constants_of_each_generation) {
    const AutomaticGradientParameters& p27 =
        renderingParameters(DesignGeneration::G27).automaticGradient;
    const AutomaticGradientParameters& p26 =
        renderingParameters(DesignGeneration::G26).automaticGradient;

    auto grey = [](double v) {
        icf::Color c;
        c.space = icf::ColorSpace::SRGB;
        c.count = 4;
        c.components[0] = c.components[1] = c.components[2] = v;
        c.components[3] = 1.0;
        return c;
    };

    // A grey has no chroma for the saturation boost to push, so the stops are
    // the lightening alone. L = 0.5 is the `midDim` band (`<= 0.50`).
    //   27: +0.08 -> 0.5 + 0.08 x 0.5 = 0.54 at 0; base 0.5 at 1 - 0   = 1.0
    //   26: +0.25 -> 0.5 + 0.25 x 0.5 = 0.625 at 0; base 0.5 at 1 - 0.3 = 0.7
    std::vector<RampStop> s = automaticGradient(grey(0.5), p27);
    REQUIRE(s.size() == 2);
    CHECK(near(s[0].rgba[0], 0.54));
    CHECK_EQ(s[0].location, 0.0);
    CHECK_EQ(s[1].rgba[0], 0.5);
    CHECK_EQ(s[1].location, 1.0);
    s = automaticGradient(grey(0.5), p26);
    REQUIRE(s.size() == 2);
    CHECK(near(s[0].rgba[0], 0.625));
    CHECK_EQ(s[0].location, 0.0);
    CHECK_EQ(s[1].rgba[0], 0.5);
    CHECK_EQ(s[1].location, 0.7);

    // L = 0.9 is the `bright` band, whose lightening is NEGATIVE in both.
    //   27: -0.05 -> 0.9 x 0.95 = 0.855 at 1; base 0.9 at 0
    //   26: -0.10 -> 0.9 x 0.90 = 0.81  at 1; base 0.9 at 0.3
    s = automaticGradient(grey(0.9), p27);
    REQUIRE(s.size() == 2);
    CHECK_EQ(s[0].rgba[0], 0.9);
    CHECK_EQ(s[0].location, 0.0);
    CHECK(near(s[1].rgba[0], 0.855));
    CHECK_EQ(s[1].location, 1.0);
    s = automaticGradient(grey(0.9), p26);
    REQUIRE(s.size() == 2);
    CHECK_EQ(s[0].rgba[0], 0.9);
    CHECK_EQ(s[0].location, 0.3);
    CHECK(near(s[1].rgba[0], 0.81));
    CHECK_EQ(s[1].location, 1.0);

    // The other two bands: `dim` (L <= 0.25) and `midBright` (L <= 0.75).
    //   0.2: 27 +0.04 -> 0.232; 26 +0.2 -> 0.36
    //   0.7: 27 +0.15 -> 0.745; 26 +0.3 -> 0.79
    CHECK(near(automaticGradient(grey(0.2), p27)[0].rgba[0], 0.232));
    CHECK(near(automaticGradient(grey(0.2), p26)[0].rgba[0], 0.36));
    CHECK(near(automaticGradient(grey(0.7), p27)[0].rgba[0], 0.745));
    CHECK(near(automaticGradient(grey(0.7), p26)[0].rgba[0], 0.79));
}

// `[BIN]` `recreateRadar153477135`, the closure at `0x8654`. Under a layer fill
// a shape the art left unpainted -- `fill="none"`, a fill of zero alpha, or an
// opacity of zero -- stays out of the silhouette when the flag is clear
// (generation 27) and is forced in when it is set (generation 26). With no
// override the flag says nothing.
TEST_CASE(a_fill_override_reaches_hidden_shapes_only_under_the_generation_26_flag) {
    using icf::svg::PaintKind;
    icf::svg::Shape painted;
    painted.fill.kind = PaintKind::Color;
    painted.fill.color.a = 1.0;
    icf::svg::Shape unpainted;
    unpainted.fill.kind = PaintKind::None;
    icf::svg::Shape transparent;
    transparent.fill.kind = PaintKind::Color;
    transparent.fill.color.a = 0.0;          // `fill-opacity="0"` lands here
    icf::svg::Shape gradient;
    gradient.fill.kind = PaintKind::Reference;
    icf::svg::Shape invisible = painted;
    invisible.opacity = 0.0;
    icf::svg::Shape faint = painted;
    faint.opacity = 0.25;

    RenderOptions plain;
    RenderOptions g27;
    g27.override.kind = FillOverride::Kind::Solid;
    RenderOptions g26 = g27;
    g26.overrideForcesHiddenPaint = true;

    // No override: the art's own answer, flag or no flag.
    RenderOptions flagOnly;
    flagOnly.overrideForcesHiddenPaint = true;
    for (const RenderOptions& o : {plain, flagOnly}) {
        CHECK(svgFillPaints(painted, o));
        CHECK(!svgFillPaints(unpainted, o));
        CHECK(svgFillPaints(transparent, o));   // painted, at alpha zero
        CHECK(svgFillPaints(gradient, o));
        CHECK_EQ(svgShapeOpacity(invisible, o), 0.0);
        CHECK_EQ(svgShapeOpacity(faint, o), 0.25);
    }

    // The override, flag clear: `CGSVGPaintIsVisible` decides (`0x8710`).
    CHECK(svgFillPaints(painted, g27));
    CHECK(svgFillPaints(gradient, g27));
    CHECK(!svgFillPaints(unpainted, g27));
    CHECK(!svgFillPaints(transparent, g27));
    CHECK_EQ(svgShapeOpacity(invisible, g27), 0.0);   // `b.gt` not taken, `tbz` taken
    CHECK_EQ(svgShapeOpacity(faint, g27), 0.25);

    // The override, flag set: all three forced.
    CHECK(svgFillPaints(painted, g26));
    CHECK(svgFillPaints(unpainted, g26));
    CHECK(svgFillPaints(transparent, g26));
    CHECK_EQ(svgShapeOpacity(invisible, g26), 1.0);
    CHECK_EQ(svgShapeOpacity(faint, g26), 0.25);   // above zero: not the flag's business

    CHECK(!renderingParameters(DesignGeneration::G27).recreateRadar153477135);
    CHECK(renderingParameters(DesignGeneration::G26).recreateRadar153477135);
}

// The same through a whole render, on both paths. Three squares side by side
// under a green layer fill: one painted, one `fill="none"`, one at
// `opacity="0"`. Generation 27 paints the first alone; generation 26 all three.
TEST_CASE(hidden_shapes_under_a_layer_fill_in_a_whole_render_on_both_paths) {
    Device& d = gpuDevice();
    if (!d.valid()) return;
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ic_generation_hidden_fills";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "Assets", ec);
    auto write = [](const fs::path& p, const char* text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    };
    write(dir / "Assets" / "three.svg",
          "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
          "<path d=\"M64 384 L320 384 L320 640 L64 640 Z\" fill=\"#ff0000\"/>"
          "<path d=\"M384 384 L640 384 L640 640 L384 640 Z\" fill=\"none\"/>"
          "<path d=\"M704 384 L960 384 L960 640 L704 640 Z\" fill=\"#0000ff\" opacity=\"0\"/>"
          "</svg>");
    write(dir / "icon.json",
          "{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
          "          \"glass\" : false,\n"
          "          \"image-name\" : \"three.svg\",\n"
          "          \"name\" : \"three\",\n"
          "          \"fill\" : { \"solid\" : \"srgb:0.00000,1.00000,0.00000,1.00000\" }\n"
          "        }\n      ]\n    }\n  ]\n}\n");
    auto bundle = icf::IconBundle::open(dir);
    REQUIRE(bundle.has_value());

    const std::uint32_t n = 64;
    auto alphaAt = [&](const RenderedIcon& icon, std::uint32_t x) {
        return icon.rgba[(static_cast<std::size_t>(n / 2) * n + x) * 4 + 3];
    };
    auto greenAt = [&](const RenderedIcon& icon, std::uint32_t x) {
        return icon.rgba[(static_cast<std::size_t>(n / 2) * n + x) * 4 + 1];
    };
    // The middle of each square, in pixels at 64: 192, 512 and 832 of 1024.
    const std::uint32_t first = 12, second = 32, third = 52;
    for (bool gpu : {false, true}) {
        IconRenderOptions o;
        o.size = n;
        auto g27 = gpu ? renderIconGpu(d, *bundle, o) : renderIcon(d, *bundle, o);
        REQUIRE(g27.has_value());
        CHECK(near(alphaAt(*g27, first), 1.0, 1e-5));
        CHECK(near(greenAt(*g27, first), 1.0, 1e-5));
        CHECK_EQ(alphaAt(*g27, second), 0.0f);
        CHECK_EQ(alphaAt(*g27, third), 0.0f);

        o.generation = DesignGeneration::G26;
        auto g26 = gpu ? renderIconGpu(d, *bundle, o) : renderIcon(d, *bundle, o);
        REQUIRE(g26.has_value());
        for (std::uint32_t x : {first, second, third}) {
            CHECK(near(alphaAt(*g26, x), 1.0, 1e-5));
            CHECK(near(greenAt(*g26, x), 1.0, 1e-5));
        }
    }
    fs::remove_all(dir, ec);
}

// ---- H5: the translucency mask -----------------------------------------------------

// `[BIN]` `glyphTranslucency` as `0x76FC0` leaves it (`0x77F18`-`0x77F40`): a
// border of 25.8 points, the shader instead of the gradient, and a contour pair
// of its own. The four strengths and the body pair are generation 27's.
TEST_CASE(generation_26_translucency_has_a_border_and_a_contour_profile) {
    const TranslucencyEffect& t27 = renderingParameters(DesignGeneration::G27).glyphTranslucency;
    const TranslucencyEffect& t26 = renderingParameters(DesignGeneration::G26).glyphTranslucency;
    CHECK_EQ(t27.borderWidth, 0.0);
    CHECK(t27.useSimpleMask);
    CHECK(t27.replicateBadSmoothing);
    CHECK_EQ(t27.lowerContourOpacity, 0.0);
    CHECK_EQ(t27.upperContourOpacity, 1.0);

    CHECK_EQ(t26.borderWidth, 25.8);
    CHECK(!t26.useSimpleMask);
    CHECK(t26.replicateBadSmoothing);
    CHECK_EQ(t26.lowerContourOpacity, 0.61);
    CHECK_EQ(t26.upperContourOpacity, 0.2);
    CHECK_EQ(t26.lowerOpacity, 0.0);
    CHECK_EQ(t26.upperOpacity, 1.0);
    for (int k = 0; k < 4; ++k) CHECK_EQ(t26.strength.slots[k], 1.0);
}

// `[BIN]` The gate of the mask is `material.translucency > 0` (`0x4AD08`-
// `0x4AD10`, `b.le`), on the document's raw number. In generation 27 that is the
// same question as "is the profile flat"; in generation 26 it is not, because
// `lowerContourOpacity` reaches the shader RAW: at zero translucency the bounds
// are still not all 1.0.
TEST_CASE(the_mask_is_gated_on_the_raw_translucency_and_not_on_a_flat_profile) {
    CHECK(!translucencyDrawsMask(0.0));
    CHECK(!translucencyDrawsMask(-0.25));
    CHECK(translucencyDrawsMask(1e-9));
    CHECK(translucencyDrawsMask(0.5));
    CHECK(translucencyDrawsMask(2.0));
    // `le` after an `fcmp` is taken for unordered operands too.
    CHECK(!translucencyDrawsMask(std::nan("")));

    const TranslucencyEffect& t27 = renderingParameters(DesignGeneration::G27).glyphTranslucency;
    const TranslucencyEffect& t26 = renderingParameters(DesignGeneration::G26).glyphTranslucency;
    CHECK(opacityMaskIsIdentity(opacityMaskArguments(t27, IconSizeClass::Large, 0.0)));
    const OpacityMaskArguments zero = opacityMaskArguments(t26, IconSizeClass::Large, 0.0);
    CHECK(!opacityMaskIsIdentity(zero));
    CHECK_EQ(zero.opacityBounds[0], 1.0f);
    CHECK_EQ(zero.opacityBounds[1], 1.0f);
    CHECK_EQ(zero.contourOpacityBounds[0], 0.61f);   // raw: not scaled by f
    CHECK_EQ(zero.contourOpacityBounds[1], 1.0f);    // 1 - (1 - 0.2) x 0
}

// `[BIN]` `0x00010048`-`0x00010054`: `borderWidth` reaches the shader as
// `float(borderWidth x (double(n) / canvasWidth))` -- texels of the SDF -- and
// the test that makes the contour pair a copy of the body pair (`0x0000FF5C`)
// is on the unconverted double.
TEST_CASE(the_border_width_is_converted_to_texels_in_double_and_narrowed_once) {
    const TranslucencyEffect& t26 = renderingParameters(DesignGeneration::G26).glyphTranslucency;
    // 1024 px: one texel per point.
    CHECK_EQ(opacityMaskArguments(t26, IconSizeClass::Large, 0.5, 1.0).borderWidth, 25.8f);
    // 512 px.
    CHECK_EQ(opacityMaskArguments(t26, IconSizeClass::Large, 0.5, 512.0 / 1024.0).borderWidth,
             12.9f);
    // 412 px: the product in double, then the float.
    CHECK_EQ(opacityMaskArguments(t26, IconSizeClass::Large, 0.5, 412.0 / 1024.0).borderWidth,
             static_cast<float>(25.8 * (412.0 / 1024.0)));

    // f = strength x translucency = 0.5. The asymmetry of the two pairs:
    //   body     (1 - (1 - 0) x 0.5, 1.0 RAW)        = (0.5, 1.0)
    //   contour  (0.61 RAW, 1 - (1 - 0.2) x 0.5)     = (0.61, 0.6)
    const OpacityMaskArguments a = opacityMaskArguments(t26, IconSizeClass::Large, 0.5, 0.5);
    CHECK_EQ(a.opacityBounds[0], 0.5f);
    CHECK_EQ(a.opacityBounds[1], 1.0f);
    CHECK_EQ(a.contourOpacityBounds[0], 0.61f);
    CHECK_EQ(a.contourOpacityBounds[1], 0.6f);

    // Generation 27: a border of zero stays zero at any scale, and the contour
    // pair is the body pair.
    const TranslucencyEffect& t27 = renderingParameters(DesignGeneration::G27).glyphTranslucency;
    const OpacityMaskArguments b = opacityMaskArguments(t27, IconSizeClass::Large, 0.5, 0.5);
    CHECK_EQ(b.borderWidth, 0.0f);
    CHECK_EQ(b.contourOpacityBounds[0], b.opacityBounds[0]);
    CHECK_EQ(b.contourOpacityBounds[1], b.opacityBounds[1]);
    CHECK_EQ(b.opacityBounds[0], 0.5f);

    // `[BIN]` And the BRANCH is the generation's (`useSimpleMask`, `0x0000FE00`):
    // the shader over the SDF in 26, the gradient in 27.
    CHECK(a.kind == OpacityMaskKind::Shader);
    CHECK_EQ(a.rampSegments, 0u);
    CHECK(b.kind == OpacityMaskKind::Gradient);
    CHECK_EQ(b.rampSegments, 16u);
}

// `[BIN]` `simplifiedShapeAwareGradientMask` under generation 26's block, from
// the per-pixel formula (`default_mod4.ll:153-196`, with the arguments of
// `0x0000FD28`), at a translucency of 0.5 and 512 px:
//
//     v       = saturate((y - bounds.y) / bounds.h)     0 top, 0.5 centre, 1 bottom
//     body    = 1.0  + (0.5  - 1.0) v
//     contour = 0.6  + (0.61 - 0.6) v
//     a       = contour + (body - contour) saturate(sd / 12.9)
//     mask    = 1 + (a^2 (3 - 2a) - 1) saturate(sd + 1)
TEST_CASE(the_generation_26_mask_at_the_top_edge_the_centre_and_the_bottom_edge) {
    const TranslucencyEffect& t26 = renderingParameters(DesignGeneration::G26).glyphTranslucency;
    OpacityMaskArguments a = opacityMaskArguments(t26, IconSizeClass::Large, 0.5, 0.5);
    a.bounds[0] = 0.0f;
    a.bounds[1] = 100.0f;
    a.bounds[2] = 300.0f;
    a.bounds[3] = 200.0f;
    const float top = 100.0f, centre = 200.0f, bottom = 300.0f;
    auto S = [](double x) { return x * x * (3.0 - 2.0 * x); };
    auto is = [](float got, double want) { return std::fabs(got - want) < 1e-6; };

    // Deep inside, past the border: the body ramp alone.
    CHECK(is(simplifiedShapeAwareGradientMask(a, 40.0f, top), 1.0));
    CHECK(is(simplifiedShapeAwareGradientMask(a, 40.0f, centre), S(0.75)));      // 0.84375
    CHECK(is(simplifiedShapeAwareGradientMask(a, 40.0f, bottom), 0.5));
    // On the contour (sd = 0): the contour ramp alone, at full coverage.
    CHECK(is(simplifiedShapeAwareGradientMask(a, 0.0f, top), S(0.6)));           // 0.648
    CHECK(is(simplifiedShapeAwareGradientMask(a, 0.0f, centre), S(0.605)));
    CHECK(is(simplifiedShapeAwareGradientMask(a, 0.0f, bottom), S(0.61)));
    // Half way into the border (sd = 6.45): the mean of the two ramps.
    CHECK(is(simplifiedShapeAwareGradientMask(a, 6.45f, top), S(0.8)));          // 0.896
    CHECK(is(simplifiedShapeAwareGradientMask(a, 6.45f, centre), S(0.6775)));
    CHECK(is(simplifiedShapeAwareGradientMask(a, 6.45f, bottom), S(0.555)));
    // Outside: half a pixel out the coverage is 0.5, a pixel out it is zero and
    // the mask is 1 whatever the ramps say.
    CHECK(is(simplifiedShapeAwareGradientMask(a, -0.5f, top), 1.0 + (S(0.6) - 1.0) * 0.5));
    CHECK(is(simplifiedShapeAwareGradientMask(a, -1.0f, top), 1.0));
    CHECK(is(simplifiedShapeAwareGradientMask(a, -5.0f, bottom), 1.0));
    // Above and below the rect the ramp clamps.
    CHECK(is(simplifiedShapeAwareGradientMask(a, 40.0f, 20.0f), 1.0));
    CHECK(is(simplifiedShapeAwareGradientMask(a, 40.0f, 400.0f), 0.5));

    // The rim is LESS opaque than the interior at the top (0.648 against 1) and
    // MORE opaque than it at the bottom (0.662 against 0.5): the contour pair
    // runs the other way round from the body pair.
    CHECK(simplifiedShapeAwareGradientMask(a, 0.0f, top) <
          simplifiedShapeAwareGradientMask(a, 40.0f, top));
    CHECK(simplifiedShapeAwareGradientMask(a, 0.0f, bottom) >
          simplifiedShapeAwareGradientMask(a, 40.0f, bottom));

}

// `[BIN]` Generation 27's mask is the OTHER branch of `0x0000FD28`
// (`0x0000FE04`-`0x0001064C`): an axial gradient on an infinite shape. Seventeen
// stops at `k/16` (`0xD28C`), each `S(upper + (lowerEff - upper) k/16)` with
// `S(a) = (a a)(3 - (a + a))`, interpolated by the monotone cubic of `flags
// 0x400`. At a translucency of 0.5: `upper = 1`, `lowerEff = 0.5`.
TEST_CASE(the_generation_27_mask_is_a_seventeen_stop_gradient_with_no_shape_in_it) {
    const TranslucencyEffect& t27 = renderingParameters(DesignGeneration::G27).glyphTranslucency;
    OpacityMaskArguments a = opacityMaskArguments(t27, IconSizeClass::Large, 0.5, 0.5);
    REQUIRE(a.kind == OpacityMaskKind::Gradient);
    REQUIRE(a.rampSegments == 16u);
    a.bounds[0] = 0.0f;
    a.bounds[1] = 100.0f;
    a.bounds[2] = 300.0f;
    a.bounds[3] = 200.0f;
    auto S = [](double x) { return (x * x) * (3.0 - (x + x)); };
    auto is = [](float got, double want) { return std::fabs(got - want) < 1e-6; };

    // AT A STOP the ramp is the stop. Every one of the seventeen, which sit
    // 12.5 px apart down a rect 200 px tall.
    for (int k = 0; k <= 16; ++k) {
        const float py = 100.0f + 12.5f * static_cast<float>(k);
        CHECK(is(gradientOpacityMask(a, py), S(1.0 - 0.5 * (k / 16.0))));
    }
    // The three the brief names: the top edge, the centre and the bottom edge.
    CHECK(is(gradientOpacityMask(a, 100.0f), 1.0));
    CHECK(is(gradientOpacityMask(a, 200.0f), 0.84375));   // S(0.75)
    CHECK(is(gradientOpacityMask(a, 300.0f), 0.5));       // S(0.5)
    // Held past both ends of the rect.
    CHECK(is(gradientOpacityMask(a, 20.0f), 1.0));
    CHECK(is(gradientOpacityMask(a, 400.0f), 0.5));

    // BETWEEN two stops the cubic is monotone: inside the two stop values. And
    // the stops are samples of `S` over a linear ramp -- a CUBIC -- so a cubic
    // through four of them with central-difference tangents is that same cubic:
    // the fourteen inner segments reproduce `S` exactly. The two END segments
    // do not, because the ramp is padded there and the tangent at the padded
    // end is forced to zero (`smoothColorCoefficients`): at the top that is
    // nearly the curve's own slope (`S'(1) = 0`), at the bottom it is not, and
    // the mask there sits six thousandths under the curve. That kink is what
    // `replicateBadSmoothing` replicates the rest of.
    for (int k = 0; k < 16; ++k) {
        const float py = 100.0f + 12.5f * (static_cast<float>(k) + 0.5f);
        const float m = gradientOpacityMask(a, py);
        const double hi = S(1.0 - 0.5 * (k / 16.0)), lo = S(1.0 - 0.5 * ((k + 1) / 16.0));
        CHECK(m < hi + 1e-6);
        CHECK(m > lo - 1e-6);
        const double curve = S(1.0 - 0.5 * ((k + 0.5) / 16.0));
        if (k >= 1 && k <= 14) {
            CHECK(std::fabs(m - curve) < 1e-6);
        } else if (k == 0) {
            CHECK(std::fabs(m - curve) < 1e-4);
        } else {
            CHECK(m < curve - 4e-3);
            CHECK(m > curve - 8e-3);
        }
    }

    // NO SHAPE. Over a field that calls every texel OUTSIDE, the mask is still
    // the ramp, the same across a row, and its coverage is 1: there is nothing
    // the field could keep opaque.
    FieldImage field;
    field.width = 3;
    field.height = 2;
    field.originY = 199;   // rows 199 and 200: pixel centres 199.5 and 200.5
    field.rgba.assign(3 * 2 * 4, 0.0f);
    for (std::size_t t = 0; t < 6; ++t) field.rgba[t * 4] = 50.0f;   // 50 px outside
    const OpacityMask mask = glassOpacityMask(field, a);
    REQUIRE(mask.a.size() == 6);
    for (std::size_t t = 0; t < 6; ++t) CHECK_EQ(mask.coverage[t], 1.0f);
    CHECK_EQ(mask.a[0], mask.a[1]);
    CHECK_EQ(mask.a[1], mask.a[2]);
    CHECK_EQ(mask.a[0], gradientOpacityMask(a, 199.5f));
    CHECK_EQ(mask.a[3], gradientOpacityMask(a, 200.5f));
    CHECK(mask.a[3] < mask.a[0]);
    // The shader, over the same field, would have answered 1 everywhere.
    OpacityMaskArguments shader = a;
    shader.kind = OpacityMaskKind::Shader;
    CHECK_EQ(glassOpacityMask(field, shader).a[0], 1.0f);

    // No translucency: seventeen stops of `S(1) = 1`, a flat mask.
    const OpacityMaskArguments flat = opacityMaskArguments(t27, IconSizeClass::Large, 0.0, 0.5);
    OpacityMaskArguments flatPlaced = flat;
    flatPlaced.bounds[1] = 100.0f;
    flatPlaced.bounds[3] = 200.0f;
    CHECK_EQ(gradientOpacityMask(flatPlaced, 137.0f), 1.0f);

    // `replicateBadSmoothing` clear (`cbz w24`, `0x0000FE74`): two RAW stops,
    // `upper` at 0 and `lowerEff` at 1. A two-stop cubic under `pad` is
    // smoothstep between them: `1 + (0.5 - 1)(3 v^2 - 2 v^3)`.
    TranslucencyEffect plain = t27;
    plain.replicateBadSmoothing = false;
    OpacityMaskArguments two = opacityMaskArguments(plain, IconSizeClass::Large, 0.5, 0.5);
    REQUIRE(two.kind == OpacityMaskKind::Gradient);
    CHECK_EQ(two.rampSegments, 1u);
    two.bounds[1] = 100.0f;
    two.bounds[3] = 200.0f;
    CHECK(is(gradientOpacityMask(two, 100.0f), 1.0));
    CHECK(is(gradientOpacityMask(two, 150.0f), 0.921875));   // v = 0.25
    CHECK(is(gradientOpacityMask(two, 200.0f), 0.75));       // v = 0.5
    CHECK(is(gradientOpacityMask(two, 300.0f), 0.5));
}

// The gate in a whole render, on both paths: a glass group whose `translucency`
// is switched on at ZERO draws no mask in either generation -- and in generation
// 26 that is the gate's doing, since the profile there is not flat -- while the
// same group at 0.5 draws one in both. (The group spells `specular: false`: a
// missing key is a highlight, and in generation 26 a plus-lighter highlight adds
// to the ALPHA of the rim it lights, which is the rim this case measures.)
TEST_CASE(a_group_at_zero_translucency_gets_no_mask_in_either_generation) {
    Device& d = gpuDevice();
    if (!d.valid()) return;
    namespace fs = std::filesystem;
    auto write = [](const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    };
    auto make = [&](const char* name, const char* value) {
        const fs::path dir = fs::temp_directory_path() / name;
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "Assets", ec);
        write(dir / "Assets" / "square.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1024 1024\">"
              "<path d=\"M256 256 L768 256 L768 768 L256 768 Z\" fill=\"#ffffff\"/></svg>");
        write(dir / "icon.json",
              std::string("{\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n"
                          "          \"image-name\" : \"square.svg\",\n"
                          "          \"name\" : \"square\"\n        }\n      ],\n"
                          "      \"specular\" : false,\n"
                          "      \"translucency\" : { \"enabled\" : true, \"value\" : ") +
                  value + " }\n    }\n  ]\n}\n");
        return dir;
    };
    const fs::path zeroDir = make("ic_generation_mask_zero", "0");
    const fs::path halfDir = make("ic_generation_mask_half", "0.5");
    auto zero = icf::IconBundle::open(zeroDir);
    auto half = icf::IconBundle::open(halfDir);
    REQUIRE(zero.has_value() && half.has_value());

    const std::uint32_t n = 128;
    auto alphaAt = [&](const RenderedIcon& icon, std::uint32_t x, std::uint32_t y) {
        return icon.rgba[(static_cast<std::size_t>(y) * n + x) * 4 + 3];
    };
    for (DesignGeneration g : {DesignGeneration::G27, DesignGeneration::G26}) {
        for (bool gpu : {false, true}) {
            IconRenderOptions o;
            o.size = n;
            o.generation = g;
            auto a = gpu ? renderIconGpu(d, *zero, o) : renderIcon(d, *zero, o);
            REQUIRE(a.has_value());
            CHECK_EQ(a->drawn, std::size_t{1});
            CHECK_EQ(a->glassTranslucent, std::size_t{0});
            // Opaque from the rim to the middle: no mask touched it.
            CHECK(near(alphaAt(*a, 34, 64), 1.0, 1e-5));
            CHECK(near(alphaAt(*a, 64, 64), 1.0, 1e-5));
            CHECK(near(alphaAt(*a, 64, 94), 1.0, 1e-5));

            auto b = gpu ? renderIconGpu(d, *half, o) : renderIcon(d, *half, o);
            REQUIRE(b.has_value());
            CHECK_EQ(b->glassTranslucent, std::size_t{1});
            // The body ramp fades towards the bottom in both generations.
            CHECK(alphaAt(*b, 64, 90) < alphaAt(*b, 64, 40));
            CHECK(alphaAt(*b, 64, 90) < 0.9f);
        }
    }
    // What the generations disagree about at 0.5 is the rim near the TOP, on
    // the vertical centre line of the square (rows 32..95 at this size):
    // generation 27 has no border, so on the second row the mask is the body
    // ramp, close to 1; generation 26 has a border of 25.8 x 128/1024 = 3.2 px
    // whose contour ramp starts at 0.6 there, and 1.5 px in the mask is still
    // well below the body.
    IconRenderOptions o;
    o.size = n;
    auto g27 = renderIcon(d, *half, o);
    o.generation = DesignGeneration::G26;
    auto g26 = renderIcon(d, *half, o);
    REQUIRE(g27.has_value() && g26.has_value());
    CHECK(alphaAt(*g27, 64, 33) > 0.95f);
    CHECK(alphaAt(*g26, 64, 33) < 0.9f);

    std::error_code ec;
    fs::remove_all(zeroDir, ec);
    fs::remove_all(halfDir, ec);
}
