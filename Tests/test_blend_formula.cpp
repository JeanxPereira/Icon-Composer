// The blend arithmetic, against an oracle that is not itself.
//
// A transcription tested only against its own constants proves the typing was
// careful. The tests here compare against the **W3C separable blend functions**,
// written out independently below -- an external definition the target is
// supposed to implement. Where the target does NOT implement it, that
// divergence is pinned with a number rather than smoothed over.
#include "check.h"

#include <cmath>

#include "Source/RenderBox/BlendFormula.h"

using rb::BlendColour;
using rb::BlendMode;

namespace {

bool near(double a, double b, double tol = 1e-12) { return std::fabs(a - b) <= tol; }

BlendColour opaque(double r, double g, double b) { return BlendColour{{r, g, b, 1.0}}; }

// Premultiply a straight colour, which is how a caller with a document colour
// would reach this API.
BlendColour premul(double r, double g, double b, double a) {
    return BlendColour{{r * a, g * a, b * a, a}};
}

// The W3C separable blend functions, written from the specification and NOT
// from the shader. This is the whole point of the file: an independent second
// opinion on what each name is supposed to mean.
double w3cMultiply(double s, double d) { return s * d; }
double w3cScreen(double s, double d) { return s + d - s * d; }
double w3cHardLight(double s, double d) {
    return s <= 0.5 ? w3cMultiply(2.0 * s, d) : w3cScreen(2.0 * s - 1.0, d);
}
double w3cOverlay(double s, double d) { return w3cHardLight(d, s); }
double w3cDarken(double s, double d) { return std::fmin(s, d); }
double w3cLighten(double s, double d) { return std::fmax(s, d); }
double w3cSoftLight(double s, double d) {
    const double dd = d <= 0.25 ? ((16.0 * d - 12.0) * d + 4.0) * d : std::sqrt(d);
    return s <= 0.5 ? d - (1.0 - 2.0 * s) * d * (1.0 - d)
                    : d + (2.0 * s - 1.0) * (dd - d);
}

}  // namespace

// With both operands OPAQUE the composition tail collapses to nothing, so the
// premultiplied result IS the blend function. That makes the comparison against
// the W3C definitions direct, with no alpha algebra in between to hide a sign.
TEST_CASE(the_separable_modes_are_the_w3c_functions_when_both_sides_are_opaque) {
    const double vals[] = {0.0, 0.125, 0.25, 0.5, 0.75, 1.0};
    for (double s : vals) {
        for (double d : vals) {
            const BlendColour src = opaque(s, s, s), dst = opaque(d, d, d);
            struct Row { BlendMode m; double expected; };
            const Row rows[] = {
                {BlendMode::Multiply,  w3cMultiply(s, d)},
                {BlendMode::Screen,    w3cScreen(s, d)},
                {BlendMode::Overlay,   w3cOverlay(s, d)},
                {BlendMode::Darken,    w3cDarken(s, d)},
                {BlendMode::Lighten,   w3cLighten(s, d)},
                {BlendMode::HardLight, w3cHardLight(s, d)},
            };
            for (const Row& r : rows) {
                const BlendColour got = rb::blend(r.m, src, dst);
                CHECK(near(got.rgba[0], r.expected));
                CHECK(near(got.rgba[3], 1.0));
            }
        }
    }
}

// AND SOFT-LIGHT IS NOT ONE OF THEM. The target applies the W3C LOW branch
// unconditionally -- there is no `sqrt` and no cubic anywhere in the module,
// which the full definition needs (doc 03 §26.4).
//
// This test asserts BOTH halves: that we compute the target's formula, and that
// the target's formula genuinely differs from W3C. Without the second half a
// future "fix" to the real W3C curve would pass.
TEST_CASE(soft_light_is_the_low_branch_unconditionally_and_that_differs) {
    const double vals[] = {0.0, 0.125, 0.25, 0.5, 0.75, 1.0};
    for (double s : vals) {
        for (double d : vals) {
            const BlendColour got =
                rb::blend(BlendMode::SoftLight, opaque(s, s, s), opaque(d, d, d));
            const double lowBranch = d + (2.0 * s - 1.0) * d * (1.0 - d);
            CHECK(near(got.rgba[0], lowBranch));
        }
    }
    // The divergence, with numbers, at a point where the W3C high branch bites.
    const BlendColour got =
        rb::blend(BlendMode::SoftLight, opaque(0.75, 0.75, 0.75), opaque(0.25, 0.25, 0.25));
    CHECK(near(got.rgba[0], 0.34375));
    CHECK(near(w3cSoftLight(0.75, 0.25), 0.375));
    CHECK(!near(got.rgba[0], w3cSoftLight(0.75, 0.25), 1e-6));
}

// Screen sits in the CHEAP band and skips the composition tail entirely. That
// is invisible when everything is opaque and visible the moment it is not, so
// this pins the difference against the tail form rather than trusting the
// comment.
TEST_CASE(screen_skips_the_composition_tail) {
    const BlendColour src = premul(1.0, 0.0, 0.0, 0.5);
    const BlendColour dst = premul(0.0, 0.0, 1.0, 0.5);
    const BlendColour got = rb::blend(BlendMode::Screen, src, dst);

    // Four channels of `s + d*(1-s)`, alpha included -- so alpha is
    // 0.5 + 0.5*0.5 = 0.75, NOT the `as + ab - as*ab` the tail would give
    // (which is also 0.75 here) ... and the RED channel is what separates them:
    // premultiplied red is 0.5 and 0.0, so screen gives 0.5, while the tail
    // form would give 0.5*(1-0.5) + 0 + B.
    CHECK(near(got.rgba[0], 0.5 + 0.0 * (1.0 - 0.5)));
    CHECK(near(got.rgba[2], 0.0 + 0.5 * (1.0 - 0.0)));
    CHECK(near(got.rgba[3], 0.5 + 0.5 * (1.0 - 0.5)));

    const BlendColour tailForm = rb::blend(BlendMode::Multiply, src, dst);
    CHECK(!near(got.rgba[0], tailForm.rgba[0], 1e-6));
}

// The two plus modes share one shader block and differ ONLY by the negative
// slack that saturating the alpha leaves behind. Where the alphas do not
// overshoot, they are the same function -- which is exactly why no corpus case
// can tell them apart and why the tables had to.
TEST_CASE(the_two_plus_modes_differ_only_once_the_alphas_overshoot) {
    // The RAW formula, which is what the default options ask for -- it is what
    // a DOCUMENT `plus-lighter` gets (`0x00025354` composites those through
    // `setBlendMode:`, with no clamp gate). The next test is the other one.
    rb::BlendOptions raw;

    const BlendColour a = premul(1.0, 1.0, 1.0, 0.25);
    const BlendColour b = premul(1.0, 1.0, 1.0, 0.25);
    const BlendColour lighter = rb::blend(BlendMode::PlusLighter, a, b, raw);
    const BlendColour darker = rb::blend(BlendMode::PlusDarker, a, b, raw);
    for (int k = 0; k < 4; ++k) CHECK(near(lighter.rgba[k], darker.rgba[k]));
    CHECK(near(lighter.rgba[3], 0.5));

    const BlendColour c = premul(1.0, 1.0, 1.0, 0.75);
    const BlendColour hot = rb::blend(BlendMode::PlusLighter, c, c, raw);
    const BlendColour cold = rb::blend(BlendMode::PlusDarker, c, c, raw);
    CHECK(near(hot.rgba[3], 1.0));
    CHECK(near(cold.rgba[3], 1.0));
    // slack = saturate(1.5) - 1.5 = -0.5
    CHECK(near(hot.rgba[0], 1.5));
    CHECK(near(cold.rgba[0], 1.0));
}

// `clampedPlusL`, and the three things about it that a "clamp to 1" written
// from the name alone would get wrong.
TEST_CASE(clamped_plus_l_caps_at_one_floors_at_the_backdrop_and_spares_plus_darker) {
    // 1. IT BITES. Three opaque white highlights over white run the raw formula
    //    to 4; the shader stops at 1, which is the whole reason the two
    //    `plusDarker` highlights drawn afterwards have anywhere to descend from.
    rb::BlendOptions raw;
    rb::BlendOptions on;
    on.clampPlusLighter = true;
    const BlendColour white = premul(1.0, 1.0, 1.0, 1.0);
    BlendColour rawAcc = white, clampedAcc = white;
    for (int i = 0; i < 3; ++i) {
        rawAcc = rb::blend(BlendMode::PlusLighter, white, rawAcc, raw);
        clampedAcc = rb::blend(BlendMode::PlusLighter, white, clampedAcc, on);
    }
    CHECK(near(rawAcc.rgba[0], 4.0));
    CHECK(near(clampedAcc.rgba[0], 1.0));
    CHECK(near(clampedAcc.rgba[3], 1.0));

    // A `plusDarker` on top now has room. Against the unclamped stack it does
    // not: 4 - 1 is still far above white.
    const BlendColour dark = BlendColour{{0.0, 0.0, 0.0, 0.5}};
    CHECK(rb::blend(BlendMode::PlusDarker, dark, clampedAcc, on).rgba[0] < 1.0);
    CHECK(rb::blend(BlendMode::PlusDarker, dark, rawAcc, raw).rgba[0] > 3.0);

    // 2. THE CAP IS THE LITERAL 1.0, NOT THE OUTPUT ALPHA. A half-covered
    //    highlight over a half-covered backdrop sums rgb to 1.0 with alpha 1.0;
    //    push the source rgb over one and rgb still stops at 1 while the
    //    `extendedColor` clamp of `pdf_mode` would have stopped it at `out.a`.
    const BlendColour halfA = premul(1.0, 1.0, 1.0, 0.4);
    const BlendColour halfB = premul(1.0, 1.0, 1.0, 0.3);
    const BlendColour mid = rb::blend(BlendMode::PlusLighter, halfA, halfB, on);
    CHECK(near(mid.rgba[0], 0.7));
    CHECK(near(mid.rgba[3], 0.7));

    // 3. THE `fmax` IS AGAINST THE BACKDROP, so a backdrop already above one --
    //    which is exactly the state an unclamped stack leaves behind -- is never
    //    pulled DOWN by the cap. Clamping without the `fmax` would darken it.
    const BlendColour over = BlendColour{{2.5, 2.5, 2.5, 1.0}};
    const BlendColour kept = rb::blend(BlendMode::PlusLighter, white, over, on);
    for (int k = 0; k < 3; ++k) CHECK(near(kept.rgba[k], 2.5));

    // And the direct entry point agrees with the dispatch.
    const BlendColour direct = rb::clampedPlusL(halfA, halfB);
    for (int k = 0; k < 4; ++k) CHECK(near(direct.rgba[k], mid.rgba[k]));

    // 4. `plusDarker` IS NOT TOUCHED: the gate in the target is `cmp w24, #8`
    //    and nothing else, so mode 4 reads the same with the option either way.
    const BlendColour d1 = rb::blend(BlendMode::PlusDarker, halfA, halfB, on);
    const BlendColour d2 = rb::blend(BlendMode::PlusDarker, halfA, halfB, raw);
    for (int k = 0; k < 4; ++k) CHECK(near(d1.rgba[k], d2.rgba[k]));
}

// A transparent operand is the identity on the other one, for every mode this
// file claims. It is the cheapest check that the tail was not written with a
// sign or an operand swapped, and it covers all nine at once.
TEST_CASE(a_transparent_operand_leaves_the_other_alone) {
    const BlendColour clear{};
    const BlendColour colour = premul(0.2, 0.6, 0.9, 0.7);
    const BlendMode modes[] = {
        BlendMode::Normal, BlendMode::Screen, BlendMode::Multiply,
        BlendMode::Overlay, BlendMode::Darken, BlendMode::Lighten,
        BlendMode::SoftLight, BlendMode::HardLight,
        BlendMode::PlusLighter, BlendMode::PlusDarker,
    };
    for (BlendMode m : modes) {
        const BlendColour onClear = rb::blend(m, colour, clear);
        for (int k = 0; k < 4; ++k) CHECK(near(onClear.rgba[k], colour.rgba[k]));
        const BlendColour clearOn = rb::blend(m, clear, colour);
        for (int k = 0; k < 4; ++k) CHECK(near(clearOn.rgba[k], colour.rgba[k]));
    }
}

// DARKEN AND LIGHTEN PAIR EACH OPERAND WITH THE **OTHER** SIDE'S ALPHA, and
// that pairing is invisible whenever the two alphas are equal -- which every
// other test in this file happens to arrange. Written after noticing that a
// mutation swapping the two would have survived the rest of the file.
//
// `[BIN]` Block %191: `fmin(as*dst, ab*src)`. In straight terms that is
// `as*ab*min(cs,cb)`; the swap gives `as*as*cs` against `ab*ab*cb`, which is
// not a blend of anything.
TEST_CASE(darken_and_lighten_pair_each_side_with_the_others_alpha) {
    const BlendColour src = premul(0.8, 0.8, 0.8, 0.5);   // s = 0.4
    const BlendColour dst = premul(0.2, 0.2, 0.2, 1.0);   // d = 0.2
    const double as = 0.5, ab = 1.0, s = 0.4, d = 0.2;

    const BlendColour dark = rb::blend(BlendMode::Darken, src, dst);
    const BlendColour light = rb::blend(BlendMode::Lighten, src, dst);
    const double tail = s * (1.0 - ab) + d * (1.0 - as);

    CHECK(near(dark.rgba[0], tail + std::fmin(as * d, ab * s)));
    CHECK(near(light.rgba[0], tail + std::fmax(as * d, ab * s)));
    // And the two really are different here, or the check above is vacuous.
    CHECK(!near(dark.rgba[0], light.rgba[0], 1e-6));
    // The swapped pairing would give 0.2 where the correct one gives 0.1.
    CHECK(near(std::fmin(as * d, ab * s), 0.1));
    CHECK(near(std::fmin(ab * d, as * s), 0.2));
}

// `extended_color` clamps to `[0, out.a]`, and the sense is the opposite of the
// intuitive one: the clamp runs when the bit is ON. The default here is OFF
// because what the icon path sets it to was never read -- that is `[OBS]`, and
// a default of "clamp" would be a guess wearing the clothes of a safe choice.
TEST_CASE(the_extended_colour_clamp_runs_when_the_bit_is_on) {
    const BlendColour src = premul(1.0, 1.0, 1.0, 0.5);
    const BlendColour dst = premul(1.0, 1.0, 1.0, 0.5);
    rb::BlendOptions on;
    on.extendedColor = true;

    const BlendColour loose = rb::blend(BlendMode::SoftLight, src, dst);
    const BlendColour tight = rb::blend(BlendMode::SoftLight, src, dst, on);
    for (int k = 0; k < 3; ++k) {
        CHECK(tight.rgba[k] >= 0.0);
        CHECK(tight.rgba[k] <= tight.rgba[3] + 1e-12);
    }
    CHECK(near(loose.rgba[3], tight.rgba[3]));
}

// The eight modes with no transcription must be ASKABLE. A caller that cannot
// tell "I blended it" from "I fell back to source-over" will report a fidelity
// it does not have -- which is the exact defect the group-blend fix removed
// from the renderer on the same day this file was written.
TEST_CASE(an_untranscribed_mode_says_so_instead_of_pretending) {
    CHECK(rb::blendIsTranscribed(BlendMode::Screen));
    CHECK(rb::blendIsTranscribed(BlendMode::PlusDarker));
    CHECK(!rb::blendIsTranscribed(BlendMode::Hue));
    CHECK(!rb::blendIsTranscribed(BlendMode::ColorBurn));
    CHECK(!rb::blendIsTranscribed(BlendMode::Difference));

    const BlendColour src = premul(0.9, 0.1, 0.1, 0.6);
    const BlendColour dst = premul(0.1, 0.1, 0.9, 0.8);
    const BlendColour fell = rb::blend(BlendMode::Hue, src, dst);
    const BlendColour over = rb::blend(BlendMode::Normal, src, dst);
    for (int k = 0; k < 4; ++k) CHECK(near(fell.rgba[k], over.rgba[k]));
}

// OVER NOTHING, EVERY TRANSCRIBED MODE IS THE SOURCE. The renderer leans on it:
// `[BIN]` a group's elements are rasterised alone into the group's image
// (`IconRendering` `0x1A9D0`, `0x13590`), so the FIRST element of a group blends
// against transparent whatever its `blend-mode` says, and `IconRenderer.cpp`
// composites it as `normal`. That is only the same picture if blending over
// transparent leaves the source alone, and for a mode where it did not, the
// group of one element -- drawn straight from its own art -- would be wrong.
//
// It was `[INF]` until this case: the tail `s*(1-ab) + d*(1-as)` is `s` at
// `d = 0`, every `B` term carries a factor of `ab`, `screen` is `s + d*(1-s)`,
// and the plus pair saturates a sum that is already in range.
TEST_CASE(every_transcribed_mode_over_transparent_is_the_source) {
    const BlendMode modes[] = {
        BlendMode::Normal,    BlendMode::Darken,     BlendMode::Multiply,
        BlendMode::ColorBurn, BlendMode::PlusDarker, BlendMode::Lighten,
        BlendMode::Screen,    BlendMode::ColorDodge, BlendMode::PlusLighter,
        BlendMode::Overlay,   BlendMode::SoftLight,  BlendMode::HardLight,
        BlendMode::Difference, BlendMode::Exclusion, BlendMode::Hue,
        BlendMode::Saturation, BlendMode::Color,     BlendMode::Luminosity,
    };
    const BlendColour nothing{{0.0, 0.0, 0.0, 0.0}};
    const BlendColour sources[] = {
        premul(0.9, 0.1, 0.4, 1.0), premul(0.9, 0.1, 0.4, 0.6), premul(1.0, 1.0, 1.0, 0.25),
        premul(0.0, 0.0, 0.0, 0.5), premul(0.3, 0.7, 0.2, 0.0),
    };
    std::size_t transcribed = 0;
    for (BlendMode m : modes) {
        if (!rb::blendIsTranscribed(m)) continue;
        ++transcribed;
        for (const BlendColour& s : sources) {
            const BlendColour got = rb::blend(m, s, nothing);
            for (int k = 0; k < 4; ++k) CHECK(near(got.rgba[k], s.rgba[k]));
        }
    }
    // The nine `IconRenderer.cpp` counts, at least: a mode that stopped being
    // transcribed would otherwise leave this case passing over fewer of them.
    CHECK(transcribed >= 9);
}
