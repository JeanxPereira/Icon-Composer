// The glass shadow's arithmetic, pinned to the numbers that were measured.
//
// This project is not test-first and these are not a specification: they are a
// clamp on four things the binary says and that a later edit could quietly
// change, each of which would still produce a plausible picture. The picture is
// what makes them worth writing down -- a wrong shadow looks like a shadow.
#include "check.h"

#include <cmath>

#include "Source/RenderBox/GlassShadow.h"

using rb::IconSizeClass;
using rb::ShadowInputs;
using rb::ShadowStyle;

namespace {
bool near(double a, double b, double eps = 1e-12) { return std::fabs(a - b) <= eps; }
}  // namespace

// THE INDEX INVERSION, WHICH IS THE ONE BUG NO PICTURE WOULD SHOW.
//
// `[BIN]` The size enum runs `small 0 .. display 3` and `SizeBasedValue`
// declares `display, large, medium, small`, so the tables are read
// `slots[3 - sizeClass]` (`0x49FA4`-`0x49FD0`). With this version's defaults all
// four slots are equal, so a transcription that indexed `slots[sizeClass]`
// produces a BIT-IDENTICAL render and no corpus test can see it.
//
// Differentiated slots are what make it visible, which is exactly what a
// parameter file that differentiates size classes would do.
TEST_CASE(glass_shadow_reads_the_opacity_table_backwards_on_purpose) {
    rb::ShadowParameters p;
    // slots are [display, large, medium, small].
    p.neutralOpacity = rb::SizeBasedValue{{0.1, 0.2, 0.3, 0.4}};

    ShadowInputs in;
    in.style = ShadowStyle::Neutral;
    in.shadowOpacity = 1.0;
    in.layerOpacity = 1.0;

    in.sizeClass = IconSizeClass::Small;
    CHECK(near(rb::shadowAlpha(in, p), 0.4));
    in.sizeClass = IconSizeClass::Medium;
    CHECK(near(rb::shadowAlpha(in, p), 0.3));
    in.sizeClass = IconSizeClass::Large;
    CHECK(near(rb::shadowAlpha(in, p), 0.2));
    in.sizeClass = IconSizeClass::Display;
    CHECK(near(rb::shadowAlpha(in, p), 0.1));
}

// THE MISSING CLAMP, AND THE CORPUS NUMBER THAT DEPENDS ON IT.
//
// `[BIN]` Two `fmul` and nothing else between `0x49F0C` and `0x4A06C`: no
// `fminnm`, no `pow`, no ceiling. `[ART]` Three of the corpus's 302 `shadow`
// resolutions carry an opacity above 1 -- Apollo-Reborn's `2.4` and `1.6` -- and
// they land on exact tenths against the measured neutral table, which is the
// document side corroborating both the 0.375 and the absence of the clamp.
TEST_CASE(glass_shadow_does_not_clamp_the_document_opacity) {
    ShadowInputs in;
    in.style = ShadowStyle::Neutral;
    in.sizeClass = IconSizeClass::Large;

    in.shadowOpacity = 2.4;   // Apollo-Reborn/AppIcon group 0, and LG-antenna group 0
    CHECK(near(rb::shadowAlpha(in), 0.9));
    in.shadowOpacity = 1.6;   // Apollo-Reborn/AppIcon group 2
    CHECK(near(rb::shadowAlpha(in), 0.6));

    // A clamped transcription would answer 0.375 to both, which is a picture
    // and therefore not something a render test would question.
    CHECK(rb::shadowAlpha(in) > 0.375);
}

// `[BIN]` `0x4A248`: `automatic` falls into the same `w8 = 0` as `vibrant`, and
// only `neutral` takes the other table. `none` returns before the arithmetic.
TEST_CASE(glass_shadow_groups_automatic_with_vibrant_and_none_draws_nothing) {
    ShadowInputs in;
    in.shadowOpacity = 1.0;
    in.sizeClass = IconSizeClass::Large;

    in.style = ShadowStyle::Automatic;
    CHECK(near(rb::shadowAlpha(in), 0.75));
    in.style = ShadowStyle::Vibrant;
    CHECK(near(rb::shadowAlpha(in), 0.75));
    in.style = ShadowStyle::Neutral;
    CHECK(near(rb::shadowAlpha(in), 0.375));

    in.style = ShadowStyle::None;
    CHECK(near(rb::shadowAlpha(in), 0.0));
    CHECK(!rb::shadowDraws(in));

    // A style that draws but an opacity of zero is NOT a gap: the document said
    // so. `shadowDraws` has to separate the two.
    in.style = ShadowStyle::Neutral;
    in.shadowOpacity = 0.0;
    CHECK(!rb::shadowDraws(in));
    in.shadowOpacity = 0.5;
    CHECK(rb::shadowDraws(in));
}

// THE GEOMETRY, IN TARGET PIXELS. `[BIN]` `s = min(w, h) / 1024` (`0x20B88`),
// offset `(0, 32)`, radius `s * blurStrengthMax * clamp(radius[3-c], 0, 1)` --
// so a 1024 target puts the shadow 32 px down under a 19.2 px radius, and a 512
// target halves both. The two are locked together because the SAME scale drives
// them; a ruler that drifted would drift on one of them first.
TEST_CASE(glass_shadow_geometry_scales_with_the_target) {
    const rb::ShadowGeometry big = rb::shadowGeometry(1024, IconSizeClass::Large);
    CHECK(near(big.offsetX, 0.0));
    CHECK(near(big.offsetY, 32.0));
    CHECK(near(big.blurRadius, 19.2, 1e-9));
    REQUIRE(big.ringWidth.has_value());
    CHECK(near(*big.ringWidth, 16.0));

    const rb::ShadowGeometry half = rb::shadowGeometry(512, IconSizeClass::Large);
    CHECK(near(half.offsetY, 16.0));
    CHECK(near(half.blurRadius, 9.6, 1e-9));

    // The blur's fraction is clamped on BOTH sides (`0x20C14`-`0x20C28`), which
    // is the third clamping shape in this material and the reason it is written
    // out rather than reused from the refraction's.
    rb::ShadowParameters p;
    p.radius = rb::SizeBasedValue{{4.0, 4.0, 4.0, 4.0}};
    CHECK(near(rb::shadowGeometry(1024, IconSizeClass::Large, p).blurRadius, 64.0));
    p.radius = rb::SizeBasedValue{{-4.0, -4.0, -4.0, -4.0}};
    CHECK(near(rb::shadowGeometry(1024, IconSizeClass::Large, p).blurRadius, 0.0));
}

// THE TWO COLOUR BRANCHES, WHICH ARE NOT TWO SHADES OF ONE THING. `[BIN]` The
// neutral branch multiplies the ALPHA by black (`0x20BAC`) -- a black silhouette
// that keeps its alpha -- and the vibrant one multiplies the COLOUR by
// `vibrantBrightness` (`0x20BB4`), keeping the glyph's own hue. That difference
// IS `GlassMaterial::shadowInfusesGlyphColor`.
//
// Run with no blur and no offset so the colour is the only thing under test.
TEST_CASE(glass_shadow_neutral_blackens_and_vibrant_dims_the_glyph_colour) {
    rb::ShadowParameters p;
    p.offsetX = p.offsetY = 0.0;
    p.radius = rb::SizeBasedValue{{0.0, 0.0, 0.0, 0.0}};
    const rb::ShadowGeometry g = rb::shadowGeometry(2, IconSizeClass::Large, p);
    CHECK(near(g.blurRadius, 0.0));

    // One opaque red texel and three transparent ones.
    std::vector<float> art(2 * 2 * 4, 0.0f);
    art[0] = 1.0f; art[1] = 0.0f; art[2] = 0.0f; art[3] = 1.0f;

    const std::vector<float> neutral =
        rb::shadowImage(art, 2, 2, ShadowStyle::Neutral, g, p);
    REQUIRE(neutral.size() == art.size());
    CHECK(near(neutral[0], 0.0, 1e-6));
    CHECK(near(neutral[1], 0.0, 1e-6));
    CHECK(near(neutral[2], 0.0, 1e-6));
    CHECK(near(neutral[3], 1.0, 1e-6));   // the alpha is the silhouette and survives

    const std::vector<float> vibrant =
        rb::shadowImage(art, 2, 2, ShadowStyle::Vibrant, g, p);
    CHECK(near(vibrant[0], 0.75, 1e-6));  // red x vibrantBrightness
    CHECK(near(vibrant[3], 1.0, 1e-6));
}

// `[BIN]` §5.3, the overdraw pass this file transcribes and does NOT draw:
// `t = clamp01(translucency / translucencyForMaxOverdraw)` and
// `alpha = t * max<...>OverdrawOpacity[3-c]`. Pinned because the note that
// reports the gap asks this function whether the gap is real for the document in
// hand, so a wrong answer here would make the renderer silent about it.
TEST_CASE(glass_shadow_overdraw_alpha_saturates_at_the_translucency_ceiling) {
    // 0.3 is `translucencyForMaxOverdraw`; at and above it, `t` is 1.
    CHECK(near(rb::shadowOverdrawAlpha(0.3, ShadowStyle::Neutral, IconSizeClass::Large), 0.2));
    CHECK(near(rb::shadowOverdrawAlpha(0.9, ShadowStyle::Neutral, IconSizeClass::Large), 0.2));
    CHECK(near(rb::shadowOverdrawAlpha(0.15, ShadowStyle::Neutral, IconSizeClass::Large), 0.1));
    // `automatic` and `vibrant` take the other quad, the same way the main pass does.
    CHECK(near(rb::shadowOverdrawAlpha(0.3, ShadowStyle::Vibrant, IconSizeClass::Large), 0.5));
    CHECK(near(rb::shadowOverdrawAlpha(0.3, ShadowStyle::Automatic, IconSizeClass::Large), 0.5));
    CHECK(near(rb::shadowOverdrawAlpha(0.0, ShadowStyle::Neutral, IconSizeClass::Large), 0.0));
    CHECK(near(rb::shadowOverdrawAlpha(0.3, ShadowStyle::None, IconSizeClass::Large), 0.0));
}

// `[BIN]` §4(d) and §4(e), the two context-driven overrides. Neither can fire in
// this renderer -- it draws a document as authored, with no recolouring state --
// so they are pinned as FUNCTIONS to keep the reading out of a comment: outside
// the identity state the shadow is neutral whatever the document said, and the
// blend byte is `multiply` unless the dim branch takes it.
TEST_CASE(glass_shadow_recolouring_forces_neutral_and_picks_the_blend_byte) {
    CHECK(rb::shadowEffectiveStyle(ShadowStyle::Vibrant, false) == ShadowStyle::Vibrant);
    CHECK(rb::shadowEffectiveStyle(ShadowStyle::Vibrant, true) == ShadowStyle::Neutral);
    CHECK(rb::shadowEffectiveStyle(ShadowStyle::Automatic, true) == ShadowStyle::Neutral);
    // `none` exits at `0x49F74`, BEFORE the gate, so it is not turned into a
    // neutral shadow that draws.
    CHECK(rb::shadowEffectiveStyle(ShadowStyle::None, true) == ShadowStyle::None);

    CHECK(rb::shadowBlendMode(ShadowStyle::Neutral) == rb::BlendMode::Multiply);
    CHECK(rb::shadowBlendMode(ShadowStyle::Vibrant) == rb::BlendMode::Multiply);
    CHECK(rb::shadowBlendMode(ShadowStyle::Vibrant, true) == rb::BlendMode::Normal);
    // The dim branch is the VIBRANT one only.
    CHECK(rb::shadowBlendMode(ShadowStyle::Neutral, true) == rb::BlendMode::Multiply);
}
