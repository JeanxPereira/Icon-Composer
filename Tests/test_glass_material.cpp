// The glass material: the group read, and the denormalisation.
//
// What is pinned here is chosen by one question -- what would a plausible
// TRANSCRIPTION have got wrong? There are four answers and each has a test that
// fails loudly for it:
//
//   1. Clamping all three formulas to [0,1]. It is the obvious uniformity, and
//      it destroys the sign of `refractionStrength` -- which is the sign the two
//      real `refractivity` entries in the entire 145-document corpus carry.
//   2. Sending a negative strength through `pow` directly. `pow` of a negative
//      base with a non-integral exponent is NaN, so the corpus's own values
//      would come out as NaN rather than as a number.
//   3. Reading `shadowStyle == neutral` as "no shadow". The name invites it and
//      `hasShadow = (style != none)` forbids it. 159 of 271 corpus groups are
//      `neutral`, so getting this backwards would darken or un-darken the
//      majority of the corpus.
//   4. Reaching for `find("shadow")` instead of `resolve`. 407 material
//      specialization entries exist in the corpus and 238 of them are
//      appearance-predicated; a `find` reads none of them.
#include "check.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/RenderBox/GlassMaterial.h"
#include "Source/RenderBox/GlassSpecular.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

// A parsed group that owns its tree, so a `icf::Group` view over it stays valid
// for the length of a test.
struct Doc {
    std::optional<icf::json::Value> tree;
    explicit Doc(const std::string& text) : tree(icf::json::parse(text)) {}
    bool ok() const { return tree.has_value(); }
    icf::Group group() const { return icf::Group(*tree); }
};

std::optional<GlassMaterialDocument> read(const Doc& d, icf::Context ctx = {},
                                          GlassMaterialReadError* e = nullptr) {
    return readGlassMaterial(d.group(), ctx, e);
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

std::vector<fs::path> corpusDocuments() {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return {};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(fs::path(dir), ec)) {
        if (!e.is_directory()) continue;
        auto doc = e.path() / "icon.json";
        if (fs::exists(doc)) out.push_back(doc);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The eight fields, and the two derived from one of them.
// ---------------------------------------------------------------------------

// `[BIN]` Only three of the eight defaults were read -- the tail the five-
// argument init fills from the constant at `0x93B30`. This pins those three so
// that a future edit cannot quietly move them, and it does NOT pin the other
// five as if they were read.
TEST_CASE(the_three_read_defaults_are_the_ones_the_five_argument_init_fills) {
    GlassMaterial m;
    CHECK(near(m.refractionHeight, 0.5));
    CHECK(near(m.refractionStrength, 0.0));
    CHECK(m.specularPlacement == SpecularPlacement::Automatic);
}

// THE ONE A TRANSCRIPTION GETS BACKWARDS. `hasShadow = shadowStyle != .none`,
// read at `0x38F58` as `cmp #1, cset ne`. So `neutral` DRAWS a shadow, and so
// does `automatic`. Only `vibrant` infuses the glyph colour (`0x38FB0`,
// `cmp #2, cset eq`) -- and `vibrant` is the target's name for the format's
// `layer-color`.
TEST_CASE(neutral_draws_a_shadow_and_only_vibrant_infuses_the_glyph_colour) {
    struct Case {
        ShadowStyle style;
        bool draws;
        bool infuses;
    };
    const Case cases[] = {
        {ShadowStyle::Automatic, true, false},
        {ShadowStyle::None, false, false},
        {ShadowStyle::Vibrant, true, true},
        {ShadowStyle::Neutral, true, false},   // the trap
    };
    for (const Case& c : cases) {
        GlassMaterial m;
        m.shadowStyle = c.style;
        CHECK_EQ(m.hasShadow(), c.draws);
        CHECK_EQ(m.shadowInfusesGlyphColor(), c.infuses);
    }
}

// `[BIN]` The enums are dense bytes in reflection order. The numbering is not
// decoration: the two derived properties above are compares against the
// literals 1 and 2, so a reordering of these cases silently rewrites them.
TEST_CASE(the_enum_cases_are_dense_bytes_in_the_order_the_metadata_gives) {
    CHECK_EQ(static_cast<int>(ShadowStyle::Automatic), 0);
    CHECK_EQ(static_cast<int>(ShadowStyle::None), 1);
    CHECK_EQ(static_cast<int>(ShadowStyle::Vibrant), 2);
    CHECK_EQ(static_cast<int>(ShadowStyle::Neutral), 3);
    CHECK_EQ(static_cast<int>(SpecularPlacement::Automatic), 0);
    CHECK_EQ(static_cast<int>(SpecularPlacement::Inside), 1);
    CHECK_EQ(static_cast<int>(SpecularPlacement::Outside), 2);
}

// ---------------------------------------------------------------------------
// The arithmetic. Three formulas that clamp three different ways, and the
// differences are the whole finding.
// ---------------------------------------------------------------------------

// `[BIN]` The eight constants, at contiguous addresses `0x985D8`-`0x985F8`.
TEST_CASE(the_normalisation_constants_are_the_ones_read_from_the_binary) {
    GlassRenderingParameters p;
    CHECK(near(p.blurStrengthMax, 64.0));
    CHECK(near(p.refractionHeightMin, 12.8));
    CHECK(near(p.refractionHeightMax, 256.0));
    CHECK(near(p.refractionHeightPower, 1.0));
    CHECK(near(p.refractionStrengthMax, 640.0));
    CHECK(near(p.refractionStrengthPower, 1.0));
    CHECK_EQ(p.refractionSupersampling, 2);
    CHECK(near(p.defaultChicletCornerRadius, 266.24));
    // `[ART]` Five of them are exact fractions of a 1024-point canvas. This is
    // corroboration of `kCanvasPoints`, not proof of it -- but if an edit ever
    // breaks the relation, the corroboration should stop being claimed.
    CHECK(near(p.blurStrengthMax / 1024.0, 0.0625));
    CHECK(near(p.refractionHeightMin / 1024.0, 0.0125));
    CHECK(near(p.refractionHeightMax / 1024.0, 0.25));
    CHECK(near(p.refractionStrengthMax / 1024.0, 0.625));
    CHECK(near(p.defaultChicletCornerRadius / 1024.0, 0.26));
}

// The endpoints are EXACT, not merely close: with the power at 1 the expression
// collapses to `min + (max - min)` and lands on 256.0 bit for bit.
TEST_CASE(the_height_hits_min_and_max_exactly_at_the_ends_of_the_unit_interval) {
    CHECK(denormaliseRefractionHeight(0.0) == 12.8);
    CHECK(denormaliseRefractionHeight(1.0) == 256.0);
}

// Clamped on BOTH sides -- `fminnm` against 1 and then a `>= 0` compare. A
// height outside the unit interval does not extrapolate.
TEST_CASE(the_height_is_clamped_at_both_ends) {
    CHECK(denormaliseRefractionHeight(-0.5) == 12.8);
    CHECK(denormaliseRefractionHeight(-1e9) == 12.8);
    CHECK(denormaliseRefractionHeight(1.5) == 256.0);
    CHECK(denormaliseRefractionHeight(1e9) == 256.0);
}

// `[BIN]` `refractionHeightPower = 1.0`, so the read configuration is linear.
TEST_CASE(the_height_is_linear_because_the_read_power_is_one) {
    GlassRenderingParameters p;
    const double span = p.refractionHeightMax - p.refractionHeightMin;
    for (double h : {0.0, 0.125, 0.25, 0.5, 0.75, 1.0}) {
        CHECK(near(denormaliseRefractionHeight(h), p.refractionHeightMin + span * h));
    }
}

// The clamp comes BEFORE the power, which only shows up when the power is not
// one. A transcription that powered first and clamped after would put an
// out-of-range height somewhere else entirely.
TEST_CASE(the_height_clamps_before_it_powers) {
    GlassRenderingParameters p;
    p.refractionHeightPower = 2.0;
    CHECK(near(denormaliseRefractionHeight(2.0, p), 256.0));   // clamp then pow(1,2)
    CHECK(near(denormaliseRefractionHeight(0.5, p), 12.8 + 243.2 * 0.25));
}

// THE SIGN PATH, and it is the reason this unit exists. `[ART]` These are the
// only two enabled `refractivity` entries in all 145 corpus documents, and both
// strengths are negative. `[BIN]` The target takes the magnitude through the
// power and reapplies the sign around it, so the result is negative with the
// right magnitude -- and, crucially, is a NUMBER: `pow` of a negative base is
// where a direct transcription produces NaN.
TEST_CASE(the_two_real_corpus_strengths_keep_their_sign_and_their_magnitude) {
    // 640 * 0.5269921875 and 640 * 0.3591796875, and the products are exact in
    // double arithmetic -- so these are equalities, not tolerances.
    const double a = denormaliseRefractionStrength(-0.5269921875);
    const double b = denormaliseRefractionStrength(-0.3591796875);
    CHECK(a == -337.275);
    CHECK(b == -229.875);
    CHECK(!std::isnan(a));
    CHECK(!std::isnan(b));
    CHECK(a < 0.0);
    CHECK(b < 0.0);
    // The magnitude is the mirror of the positive input, exactly.
    CHECK(a == -denormaliseRefractionStrength(0.5269921875));
    CHECK(b == -denormaliseRefractionStrength(0.3591796875));
}

// A non-integral power is where the negative base would blow up. `pow(-0.5, 0.5)`
// is NaN; `sign * pow(0.5, 0.5)` is not. This is the test that distinguishes the
// two ways of writing the same formula.
TEST_CASE(a_negative_strength_never_reaches_pow_as_a_negative_base) {
    GlassRenderingParameters p;
    p.refractionStrengthPower = 0.5;
    const double s = denormaliseRefractionStrength(-0.25, p);
    CHECK(!std::isnan(s));
    CHECK(near(s, -640.0 * 0.5));   // -640 * sqrt(0.25)
    // And the naive spelling really would be NaN -- so the test is not vacuous.
    CHECK(std::isnan(std::pow(-0.25, 0.5)));
}

// Only the MAGNITUDE is clamped, and the clamp is at 1.
TEST_CASE(the_strength_clamps_the_magnitude_and_leaves_the_sign_alone) {
    CHECK(denormaliseRefractionStrength(1.0) == 640.0);
    CHECK(denormaliseRefractionStrength(7.0) == 640.0);
    CHECK(denormaliseRefractionStrength(-1.0) == -640.0);
    CHECK(denormaliseRefractionStrength(-7.0) == -640.0);
}

// `[BIN]` The zero test is on the input, so it catches -0.0 too -- `copysign(1,
// -0.0)` is -1, and a formula that trusted it would emit -0.0 where the target
// emits +0.0. NaN goes to zero by the same guard.
TEST_CASE(zero_and_nan_strength_give_zero) {
    CHECK(denormaliseRefractionStrength(0.0) == 0.0);
    CHECK(denormaliseRefractionStrength(-0.0) == 0.0);
    CHECK(!std::signbit(denormaliseRefractionStrength(-0.0)));
    const double n = denormaliseRefractionStrength(std::nan(""));
    CHECK(!std::isnan(n));
    CHECK(n == 0.0);
}

// THE ASYMMETRY. The blur has a ceiling and NO floor: a negative blur strength
// produces a negative radius, and is not clamped up. A transcription that
// clamped all three formulas the same way would return 0 here.
TEST_CASE(the_blur_has_a_ceiling_and_no_floor) {
    CHECK(denormaliseBlurRadius(0.0) == 0.0);
    CHECK(denormaliseBlurRadius(0.5) == 32.0);
    CHECK(denormaliseBlurRadius(1.0) == 64.0);
    CHECK(denormaliseBlurRadius(4.0) == 64.0);   // ceiling
    CHECK(denormaliseBlurRadius(-0.5) == -32.0); // NOT clamped up to zero
    CHECK(denormaliseBlurRadius(-2.0) == -128.0);
}

// `[BIN]` The shader gets ONE argument and it is the strength NEGATED
// (`0x10C14`, and again at `0x805F4`). Kept separate from the strength because
// the negation is a fact about the argument, not about the strength.
TEST_CASE(the_displacement_shader_argument_is_the_negated_strength) {
    GlassMaterial m;
    m.refractionStrength = -0.5269921875;
    const DenormalisedGlass g = denormaliseGlass(m);
    CHECK(g.refractionStrengthPoints == -337.275);
    CHECK(g.displacementShaderArgument == 337.275);
    CHECK_EQ(g.refractionSupersampling, 2);
}

// `[OBS]` The four unconsumed fields cross with no arithmetic. This test exists
// to make that a commitment: if anyone later invents a denormalisation for them,
// it fails, and the failure is the point.
TEST_CASE(the_four_unconsumed_fields_are_transported_and_not_scaled) {
    GlassMaterial m;
    m.translucency = 0.7;
    m.shadowOpacity = 0.5;
    m.hasSpecular = true;
    m.specularPlacement = SpecularPlacement::Inside;
    m.shadowStyle = ShadowStyle::Neutral;
    const DenormalisedGlass g = denormaliseGlass(m);
    CHECK(g.translucency == 0.7);
    CHECK(g.shadowOpacity == 0.5);
    CHECK(g.hasSpecular);
    CHECK(g.specularPlacement == SpecularPlacement::Inside);
    CHECK(g.shadowStyle == ShadowStyle::Neutral);
    CHECK(g.hasShadow);
    CHECK(!g.shadowInfusesGlyphColor);
}

// ---------------------------------------------------------------------------
// The document read.
// ---------------------------------------------------------------------------

TEST_CASE(the_six_keys_read_from_a_bare_group) {
    Doc d(R"({
      "blur-material": 0.3,
      "translucency": { "enabled": true, "value": 0.7 },
      "refractivity": { "enabled": true, "strength": -0.5269921875, "depth": 0.50046875 },
      "shadow": { "kind": "layer-color", "opacity": 0.75 },
      "specular": "inside",
      "lighting": "combined"
    })");
    REQUIRE(d.ok());
    auto m = read(d);
    REQUIRE(m.has_value());

    CHECK(m->blurMaterialKey);
    REQUIRE(m->blurMaterial.has_value());
    CHECK(near(*m->blurMaterial, 0.3));
    REQUIRE(m->translucency.has_value());
    CHECK(m->translucency->enabled);
    CHECK(near(m->translucency->value, 0.7));
    REQUIRE(m->refractivity.has_value());
    CHECK(m->refractivity->enabled);
    CHECK(near(m->refractivity->strength, -0.5269921875));
    CHECK(near(m->refractivity->depth, 0.50046875));
    REQUIRE(m->shadow.has_value());
    CHECK(m->shadow->kind == icf::ShadowKind::LayerColor);
    CHECK(near(m->shadow->opacity, 0.75));
    REQUIRE(m->specular.has_value());
    CHECK(*m->specular == icf::SpecularHighlight::Inside);
    REQUIRE(m->lighting.has_value());
    CHECK(*m->lighting == icf::Lighting::Combined);
}

// THE DEFECT THIS UNIT IS BUILT AGAINST. The same six keys spelled as
// `-specializations` lists must read identically. `[ART]` The corpus carries 407
// such entries across the six keys, and no group carries both spellings of one
// key -- so a reader that only handled the bare form would read nothing at all
// for a third of the material.
TEST_CASE(the_same_six_keys_read_from_specialization_lists) {
    Doc d(R"({
      "blur-material-specializations": [ { "value": 0.3 } ],
      "translucency-specializations": [ { "value": { "enabled": true, "value": 0.7 } } ],
      "shadow-specializations": [ { "value": { "kind": "layer-color", "opacity": 0.75 } } ],
      "specular-specializations": [ { "value": "inside" } ],
      "lighting-specializations": [ { "value": "combined" } ],
      "refractivity-specializations": [
        { "value": { "enabled": true, "strength": -0.3591796875, "depth": 0.5 } }
      ]
    })");
    REQUIRE(d.ok());
    auto m = read(d);
    REQUIRE(m.has_value());
    REQUIRE(m->blurMaterial.has_value());
    CHECK(near(*m->blurMaterial, 0.3));
    REQUIRE(m->translucency.has_value());
    CHECK(near(m->translucency->value, 0.7));
    REQUIRE(m->shadow.has_value());
    CHECK(m->shadow->kind == icf::ShadowKind::LayerColor);
    REQUIRE(m->specular.has_value());
    CHECK(*m->specular == icf::SpecularHighlight::Inside);
    REQUIRE(m->lighting.has_value());
    CHECK(*m->lighting == icf::Lighting::Combined);
    REQUIRE(m->refractivity.has_value());
    CHECK(near(m->refractivity->strength, -0.3591796875));
}

// The predicate selects, and the specialization outranks the plain key. `[ART]`
// 238 of the corpus's 407 material specialization entries are appearance-
// predicated; reading the bare key for a dark rendering would be wrong for every
// one of them.
TEST_CASE(the_appearance_predicate_selects_and_a_specialization_outranks_the_plain_key) {
    Doc d(R"({
      "translucency": { "enabled": true, "value": 0.5 },
      "translucency-specializations": [
        { "value": { "enabled": true, "value": 0.2 }, "appearance": "dark" },
        { "value": { "enabled": false, "value": 0.9 } }
      ],
      "shadow": { "kind": "neutral", "opacity": 0.5 },
      "shadow-specializations": [
        { "value": { "kind": "none", "opacity": 0.5 }, "appearance": "tinted" }
      ]
    })");
    REQUIRE(d.ok());

    // Dark: the predicated entry is the most specific match.
    auto dark = read(d, icf::Context{icf::Appearance::Dark, icf::Idiom::Base});
    REQUIRE(dark.has_value());
    REQUIRE(dark->translucency.has_value());
    CHECK(near(dark->translucency->value, 0.2));

    // Base: no predicate matches `dark`, so the unpredicated entry wins -- and
    // it still outranks the plain `translucency`, which is 0.5.
    auto base = read(d, icf::Context{icf::Appearance::Base, icf::Idiom::Base});
    REQUIRE(base.has_value());
    REQUIRE(base->translucency.has_value());
    CHECK(near(base->translucency->value, 0.9));
    CHECK(!base->translucency->enabled);

    // `shadow` has no unpredicated entry, so outside `tinted` it falls back to
    // the plain key -- and inside `tinted` it does not.
    REQUIRE(base->shadow.has_value());
    CHECK(base->shadow->kind == icf::ShadowKind::Neutral);
    auto tinted = read(d, icf::Context{icf::Appearance::Tinted, icf::Idiom::Base});
    REQUIRE(tinted.has_value());
    REQUIRE(tinted->shadow.has_value());
    CHECK(tinted->shadow->kind == icf::ShadowKind::None);
}

// `[OBS]` `null` is the collapsed `{enabled, explicitStrength}` pair, and which
// half it collapsed is not determinable. It is therefore ABSENT, not zero -- but
// the key's presence is still recorded, because "the group said null" and "the
// group said nothing" are different facts and the corpus contains both.
TEST_CASE(a_null_blur_material_is_absent_and_is_not_a_zero) {
    Doc withNull(R"({ "blur-material": null })");
    REQUIRE(withNull.ok());
    auto a = read(withNull);
    REQUIRE(a.has_value());
    CHECK(a->blurMaterialKey);
    CHECK(!a->blurMaterial.has_value());

    Doc without(R"({ "translucency": { "enabled": true, "value": 0.5 } })");
    REQUIRE(without.ok());
    auto b = read(without);
    REQUIRE(b.has_value());
    CHECK(!b->blurMaterialKey);
    CHECK(!b->blurMaterial.has_value());

    Doc zero(R"({ "blur-material": 0 })");
    REQUIRE(zero.ok());
    auto c = read(zero);
    REQUIRE(c.has_value());
    CHECK(c->blurMaterialKey);
    REQUIRE(c->blurMaterial.has_value());
    CHECK(*c->blurMaterial == 0.0);
}

// A group carrying none of the six reads successfully with everything absent.
// `[ART]` A real case: 74 of the 271 corpus groups carry no `blur-material`, 168
// carry no `lighting`, and 266 carry no `refractivity`.
TEST_CASE(a_group_with_no_material_keys_reads_as_all_absent) {
    Doc d(R"({ "name": "Front", "layers": [] })");
    REQUIRE(d.ok());
    auto m = read(d);
    REQUIRE(m.has_value());
    CHECK(!m->specular.has_value());
    CHECK(!m->shadow.has_value());
    CHECK(!m->translucency.has_value());
    CHECK(!m->blurMaterialKey);
    CHECK(!m->refractivity.has_value());
    CHECK(!m->lighting.has_value());
}

// A key that is PRESENT and unreadable is an error, never a default -- the rule
// `Values.h` states for the layer below. And the error names the key, so a
// corpus run reports which one refused.
TEST_CASE(a_present_but_malformed_key_refuses_and_names_itself) {
    struct Case {
        const char* json;
        const char* key;
    };
    const Case cases[] = {
        {R"({ "shadow": { "kind": "wibble", "opacity": 0.5 } })", "shadow"},
        {R"({ "shadow": { "opacity": 0.5 } })", "shadow"},
        {R"({ "shadow": "neutral" })", "shadow"},
        {R"({ "translucency": { "enabled": true } })", "translucency"},
        {R"({ "refractivity": { "enabled": true, "strength": 0 } })", "refractivity"},
        {R"({ "specular": 1 })", "specular"},
        {R"({ "specular": "sideways" })", "specular"},
        {R"({ "blur-material": "0.5" })", "blur-material"},
        {R"({ "lighting": "sideways" })", "lighting"},
        {R"({ "lighting": true })", "lighting"},
    };
    for (const Case& c : cases) {
        Doc d(c.json);
        REQUIRE(d.ok());
        GlassMaterialReadError e;
        auto m = read(d, {}, &e);
        CHECK(!m.has_value());
        CHECK_EQ(e.key, std::string(c.key));
    }
}

// ---------------------------------------------------------------------------
// The mapping onto the target's eight fields.
// ---------------------------------------------------------------------------

// `[INF]` 4 = 1 + 3. `SpecularHighlight`'s four cases become one bool and a
// three-case placement, and the document's two SHAPES are the two axes: the bool
// is off/on and the string is the placement.
TEST_CASE(the_four_specular_forms_collapse_into_two_fields) {
    struct Case {
        const char* json;
        bool hasSpecular;
        SpecularPlacement placement;
    };
    const Case cases[] = {
        {R"({ "specular": false })", false, SpecularPlacement::Automatic},
        {R"({ "specular": true })", true, SpecularPlacement::Automatic},
        {R"({ "specular": "off" })", false, SpecularPlacement::Automatic},
        {R"({ "specular": "automatic" })", true, SpecularPlacement::Automatic},
        {R"({ "specular": "inside" })", true, SpecularPlacement::Inside},
        {R"({ "specular": "outside" })", true, SpecularPlacement::Outside},
    };
    for (const Case& c : cases) {
        Doc d(c.json);
        REQUIRE(d.ok());
        auto doc = read(d);
        REQUIRE(doc.has_value());
        const GlassMaterial m = glassMaterialFrom(*doc);
        CHECK_EQ(m.hasSpecular, c.hasSpecular);
        CHECK(m.specularPlacement == c.placement);
    }
}

// `[INF]` The rename across the boundary: the format's `layer-color` is the
// renderer's `vibrant`. Forced by matching cardinality and three shared names --
// and it is `vibrant` that `shadowInfusesGlyphColor` keys on, so the pairing has
// a visible consequence.
TEST_CASE(layer_color_becomes_vibrant_and_it_is_the_one_that_infuses) {
    struct Case {
        const char* kind;
        ShadowStyle style;
        bool draws;
        bool infuses;
    };
    const Case cases[] = {
        {"automatic", ShadowStyle::Automatic, true, false},
        {"none", ShadowStyle::None, false, false},
        {"layer-color", ShadowStyle::Vibrant, true, true},
        {"neutral", ShadowStyle::Neutral, true, false},
    };
    for (const Case& c : cases) {
        Doc d(std::string(R"({ "shadow": { "kind": ")") + c.kind + R"(", "opacity": 0.6 } })");
        REQUIRE(d.ok());
        auto doc = read(d);
        REQUIRE(doc.has_value());
        const GlassMaterial m = glassMaterialFrom(*doc);
        CHECK(m.shadowStyle == c.style);
        CHECK_EQ(m.hasShadow(), c.draws);
        CHECK_EQ(m.shadowInfusesGlyphColor(), c.infuses);
        CHECK(near(m.shadowOpacity, 0.6));
    }
}

// `[INF]` `refractivity.depth` -> `refractionHeight` and `.strength` ->
// `refractionStrength`, by name. This walks the whole path for the corpus's two
// real entries: document text in, shader argument out.
TEST_CASE(the_two_real_refractivity_entries_walk_the_whole_path) {
    Doc d(R"({
      "refractivity": { "enabled": true, "strength": -0.5269921875, "depth": 0.50046875 }
    })");
    REQUIRE(d.ok());
    auto doc = read(d);
    REQUIRE(doc.has_value());
    const GlassMaterial m = glassMaterialFrom(*doc);
    CHECK(m.refractionStrength == -0.5269921875);
    CHECK(m.refractionHeight == 0.50046875);

    const DenormalisedGlass g = denormaliseGlass(m);
    CHECK(g.refractionStrengthPoints == -337.275);
    CHECK(g.displacementShaderArgument == 337.275);
    CHECK(near(g.refractionHeightPoints, 12.8 + 243.2 * 0.50046875));
    // Inside the read range, and on the low side of the 1024-point canvas.
    CHECK(g.refractionHeightPoints > 12.8);
    CHECK(g.refractionHeightPoints < 256.0);
}

// `[OBS]` The `enabled` bits are NOT applied, because where they collapse was
// not read. This test states that as a commitment rather than leaving it to a
// reader's assumption: a disabled translucency of 0.5 still carries 0.5.
TEST_CASE(the_enabled_bits_stay_visible_and_are_not_applied_to_the_value) {
    Doc d(R"({ "translucency": { "enabled": false, "value": 0.5 } })");
    REQUIRE(d.ok());
    auto doc = read(d);
    REQUIRE(doc.has_value());
    REQUIRE(doc->translucency.has_value());
    CHECK(!doc->translucency->enabled);          // the bit is readable...
    const GlassMaterial m = glassMaterialFrom(*doc);
    CHECK(m.translucency == 0.5);                // ...and it was not applied
}

// An absent key leaves the field at its struct default, and the defaults that
// were actually read survive a document that says nothing.
//
// `[BIN]` `specular` is the one that is NOT the struct's zero. A missing
// `specular` is `.automatic`, read out of `Group.init` at `0x9090C` in
// `IconComposerFoundation.arm64` (`0x909D4  strb w9, [x19, #0xe8]`, w9 == 1,
// and `SpecularHighlight` lists `off, automatic, inside, outside`). So an empty
// group HAS a highlight, and the document has to say `"specular": false` to
// take it away -- which `[ART]` 36 of the 271 corpus groups do, out loud.
TEST_CASE(absent_keys_leave_the_read_defaults_in_place) {
    Doc d(R"({ "name": "Back" })");
    REQUIRE(d.ok());
    auto doc = read(d);
    REQUIRE(doc.has_value());
    CHECK(!doc->specular.has_value());  // the DOCUMENT still says nothing...
    const GlassMaterial m = glassMaterialFrom(*doc);
    CHECK(m.refractionHeight == 0.5);
    CHECK(m.refractionStrength == 0.0);
    CHECK(m.specularPlacement == SpecularPlacement::Automatic);
    CHECK(m.hasSpecular);  // ...and the MATERIAL still lights it
    CHECK(m.shadowStyle == ShadowStyle::Automatic);
}

// And the way to switch it off is to spell it, which is why the corpus spells
// it. This is the companion to the case above: without it, "absent is
// automatic" could be mistaken for "the off switch stopped working".
TEST_CASE(an_explicit_specular_false_still_switches_the_highlight_off) {
    Doc d(R"({ "specular": false })");
    REQUIRE(d.ok());
    auto doc = read(d);
    REQUIRE(doc.has_value());
    REQUIRE(doc->specular.has_value());
    const GlassMaterial m = glassMaterialFrom(*doc);
    CHECK(!m.hasSpecular);
    CHECK(m.specularPlacement == SpecularPlacement::Automatic);
}

// ---------------------------------------------------------------------------
// The corpus.
// ---------------------------------------------------------------------------

// THE GATE. Every group of all 145 documents, in every appearance, read to
// completion -- and the distribution printed, because a silent default on real
// data looks exactly like a correct read until someone counts.
TEST_CASE(glass_material_corpus_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());  // IC_CORPUS_DIR unset or empty

    const icf::Appearance appearances[] = {icf::Appearance::Base, icf::Appearance::Light,
                                           icf::Appearance::Dark, icf::Appearance::Tinted};

    int documents = 0, groups = 0, reads = 0;
    std::vector<std::string> refusals;
    std::map<std::string, int> shadowStyles, specularForms, lightings;
    int blurNull = 0, blurNumber = 0, blurAbsent = 0;
    int translucencyOn = 0, translucencyOff = 0, translucencyAbsent = 0;
    int refractivityOn = 0, refractivityOff = 0, refractivityAbsent = 0;
    int negativeStrengths = 0, shadowsDrawn = 0, infusing = 0;
    double minHeight = 1e300, maxHeight = -1e300, minBlur = 1e300, maxBlur = -1e300;

    auto name = [](ShadowStyle s) -> const char* {
        switch (s) {
            case ShadowStyle::Automatic: return "automatic";
            case ShadowStyle::None: return "none";
            case ShadowStyle::Vibrant: return "vibrant(layer-color)";
            case ShadowStyle::Neutral: return "neutral";
        }
        return "?";
    };

    for (const auto& p : docs) {
        auto tree = icf::json::parse(readAll(p));
        if (!tree) continue;
        auto doc = icf::IconDocument::open(*tree);
        if (!doc) continue;
        ++documents;
        for (const icf::Group& g : doc->groups()) {
            ++groups;
            for (icf::Appearance a : appearances) {
                GlassMaterialReadError e;
                auto read = readGlassMaterial(g, {a, icf::Idiom::Base}, &e);
                if (!read) {
                    refusals.push_back(p.parent_path().filename().string() + ": " + e.key +
                                       " -- " + e.why);
                    continue;
                }
                ++reads;

                const GlassMaterial m = glassMaterialFrom(*read);
                const DenormalisedGlass d = denormaliseGlass(m);

                // Only the base appearance feeds the distribution, so a
                // four-times-over count does not read as four times the corpus.
                if (a != icf::Appearance::Base) continue;

                if (read->shadow) ++shadowStyles[name(m.shadowStyle)];
                if (m.hasShadow()) ++shadowsDrawn;
                if (m.shadowInfusesGlyphColor()) ++infusing;

                if (read->specular) {
                    ++specularForms[std::string(m.hasSpecular ? "on" : "off") + "/" +
                                    (m.specularPlacement == SpecularPlacement::Inside
                                         ? "inside"
                                         : m.specularPlacement == SpecularPlacement::Outside
                                               ? "outside"
                                               : "automatic")];
                }
                if (read->lighting) {
                    ++lightings[*read->lighting == icf::Lighting::Individual ? "individual"
                                                                            : "combined"];
                }

                if (!read->blurMaterialKey) {
                    ++blurAbsent;
                } else if (read->blurMaterial) {
                    ++blurNumber;
                    minBlur = std::fmin(minBlur, d.blurRadiusPoints);
                    maxBlur = std::fmax(maxBlur, d.blurRadiusPoints);
                } else {
                    ++blurNull;
                }

                if (!read->translucency) {
                    ++translucencyAbsent;
                } else if (read->translucency->enabled) {
                    ++translucencyOn;
                } else {
                    ++translucencyOff;
                }

                if (!read->refractivity) {
                    ++refractivityAbsent;
                } else {
                    if (read->refractivity->enabled) ++refractivityOn; else ++refractivityOff;
                    if (d.refractionStrengthPoints < 0.0) ++negativeStrengths;
                    minHeight = std::fmin(minHeight, d.refractionHeightPoints);
                    maxHeight = std::fmax(maxHeight, d.refractionHeightPoints);
                }

                // Nothing real may produce a non-number. This is the assertion
                // the sign path exists to satisfy: `pow` of a negative base
                // would land here as a NaN.
                CHECK(!std::isnan(d.refractionHeightPoints));
                CHECK(!std::isnan(d.refractionStrengthPoints));
                CHECK(!std::isnan(d.blurRadiusPoints));
            }
        }
    }

    std::printf("  %d documents, %d groups, %d reads (4 appearances each)\n",
                documents, groups, reads);
    std::printf("  shadow: ");
    for (const auto& kv : shadowStyles) std::printf("%s x%d  ", kv.first.c_str(), kv.second);
    std::printf("\n  shadows drawn: %d, infusing glyph colour: %d\n", shadowsDrawn, infusing);
    std::printf("  specular: ");
    for (const auto& kv : specularForms) std::printf("%s x%d  ", kv.first.c_str(), kv.second);
    std::printf("\n  lighting: ");
    for (const auto& kv : lightings) std::printf("%s x%d  ", kv.first.c_str(), kv.second);
    std::printf("\n  blur-material: number x%d (radius %.4g..%.4g pt), null x%d, absent x%d\n",
                blurNumber, minBlur, maxBlur, blurNull, blurAbsent);
    std::printf("  translucency: enabled x%d, disabled x%d, absent x%d\n",
                translucencyOn, translucencyOff, translucencyAbsent);
    std::printf("  refractivity: enabled x%d, disabled x%d, absent x%d; "
                "negative strengths x%d; height %.6g..%.6g pt\n",
                refractivityOn, refractivityOff, refractivityAbsent, negativeStrengths,
                minHeight, maxHeight);
    for (size_t i = 0; i < refusals.size() && i < 10; ++i) {
        std::printf("    REFUSED %s\n", refusals[i].c_str());
    }

    CHECK_EQ(documents, 145);
    CHECK_EQ(groups, 271);
    CHECK_EQ(reads, 271 * 4);
    CHECK_EQ(static_cast<int>(refusals.size()), 0);

    // `[ART]` The census, at the BASE appearance. It is not the raw key count of
    // the survey and it should not be: the survey counts written keys, and this
    // counts what `resolve` answers. `shadow` is written in 271 of 271 groups
    // but resolves in 266 at base, because five groups carry only a
    // dark-predicated specialization and no plain key. That gap is a property of
    // the format, and pinning it here is what would catch a reader that started
    // falling back to the plain key when it should not.
    CHECK_EQ(shadowStyles["neutral"], 182);
    CHECK_EQ(shadowStyles["vibrant(layer-color)"], 66);
    CHECK_EQ(shadowStyles["none"], 18);
    CHECK_EQ(shadowStyles["automatic"], 0);
    CHECK_EQ(shadowStyles["neutral"] + shadowStyles["vibrant(layer-color)"] +
                 shadowStyles["none"] + shadowStyles["automatic"],
             266);

    CHECK_EQ(specularForms["off/automatic"], 60);
    CHECK_EQ(specularForms["on/automatic"], 76);
    CHECK_EQ(specularForms["on/inside"], 3);
    CHECK_EQ(specularForms["on/outside"], 0);

    CHECK_EQ(lightings["individual"], 73);
    CHECK_EQ(lightings["combined"], 22);

    // `[ART]` And the null is not only a bare-key phenomenon: 37 of the corpus's
    // `blur-material` specialization entries carry `"value": null`, and a
    // resolved null OUTRANKS the plain key exactly as a resolved number does. A
    // reader that treated a null `value` as "no override" and fell back would
    // report 48 nulls and 132 absents here instead of 62 and 118 -- which is the
    // mistake this line exists to catch, and it is one I made once already while
    // measuring the census.
    CHECK_EQ(blurNumber, 91);
    CHECK_EQ(blurNull, 62);
    CHECK_EQ(blurAbsent, 118);
    CHECK_EQ(translucencyOn, 217);
    CHECK_EQ(translucencyOff, 50);
    CHECK_EQ(translucencyAbsent, 4);
    CHECK_EQ(refractivityOn + refractivityOff, 5);

    // `[ART]` Both enabled `refractivity` entries in the entire corpus are
    // negative, which is the whole reason the sign is preserved. If the sign
    // were dropped this count would be 0 and everything else would still pass.
    CHECK_EQ(refractivityOn, 2);
    CHECK_EQ(negativeStrengths, 2);

    // `[BIN]` `neutral` draws. If it did not, this would be 271 minus the
    // neutrals rather than 271 minus the `none`s -- a difference of 182 groups.
    CHECK_EQ(shadowsDrawn, 271 - shadowStyles["none"]);
    CHECK_EQ(infusing, shadowStyles["vibrant(layer-color)"]);

    // `[BIN]` The blur has a ceiling: no radius may exceed `blurStrengthMax`.
    CHECK(maxBlur <= 64.0);
    // `[ART]` And every real height lands inside the read range.
    CHECK(minHeight >= 12.8);
    CHECK(maxHeight <= 256.0);
}

// The specular placement fold. What is pinned here is the SHAPE of the
// decision, which is the part a reader of the enum would get wrong.
//
// A three-case enum that collapses to one bit invites exactly one conclusion:
// that two of the three cases are the same case. They are not. What is true is
// narrower and stranger, and both halves of it are `[BIN]`:
//
//   - the bit can only ever ASK for `outside`; two conditions that belong to
//     the renderer and not to the document can refuse it;
//   - `automatic` is a third answer, not a synonym for either neighbour: it
//     follows the identity recolour state, and it follows it towards `outside`,
//     which is the OPPOSITE direction from the shadow's gate of the same shape.
TEST_CASE(specular_placement_is_a_request_that_two_renderer_conditions_can_refuse) {
    using rb::SpecularPlacement;

    // `[BIN]` `0x000494E4`: a bright highlight ORs a literal 1 into the result,
    // so no document can move it. Only the `multiply` pass can go outside.
    for (auto p : {SpecularPlacement::Automatic, SpecularPlacement::Inside,
                   SpecularPlacement::Outside}) {
        CHECK(rb::specularDrawsInside(p, /*identityRecolour=*/false, /*isDarklight=*/false,
                                      /*hasOutsetOpacity=*/true));
    }

    // `[BIN]` `0x000494F8`: a nil `constraints.outsetOpacity` forces inside too.
    CHECK(rb::specularDrawsInside(SpecularPlacement::Outside, false, true, false));

    // With both refusals out of the way the document is finally heard, and the
    // three cases are three answers.
    CHECK(!rb::specularDrawsInside(SpecularPlacement::Outside, true, true, true));
    CHECK(rb::specularDrawsInside(SpecularPlacement::Inside, false, true, true));
    CHECK(rb::specularDrawsInside(SpecularPlacement::Automatic, true, true, true));
    CHECK(!rb::specularDrawsInside(SpecularPlacement::Automatic, false, true, true));

    // `[BIN]` `0x000495E0`-`0x000495EC`. Same thickness, mirrored about the
    // anchor, and flat on the outside because `curvature` is zeroed there.
    const rb::SpecularBand declared{/*height=*/4.0, /*inset=*/10.0, /*curvature=*/0.75};
    const rb::SpecularBand inside = rb::specularBand(declared, true);
    const rb::SpecularBand outside = rb::specularBand(declared, false);

    // The shader's band is `0 <= distance - inset <= height`, so these are the
    // two intervals in distance, and they meet at the anchor without overlap.
    CHECK_EQ(inside.inset, 10.0);
    CHECK_EQ(inside.inset + inside.height, 14.0);
    CHECK_EQ(outside.inset, 6.0);
    CHECK_EQ(outside.inset + outside.height, 10.0);
    CHECK_EQ(inside.curvature, 0.75);
    CHECK_EQ(outside.curvature, 0.0);
}

// The 16113 bytes, and what a plausible transcription would get wrong in them.
// Five answers, five checks:
//
//   1. Reading the `SizeBasedValue` forwards. `slots[k]` instead of
//      `slots[3 - k]` swaps `small` with `display`, and UNLIKE the shadow -- all
//      of whose defaults are four equal numbers -- the highlights DIFFER
//      between classes (`distance` is 4 at display and 6 everywhere else), so
//      the inversion is visible in a pixel here and must be visible in a test.
//   2. Taking `spread` to the shader as a radian. `lit` would be zero
//      everywhere and the whole effect would silently vanish; the cosine at
//      `0x0000EE20` is what makes it a cone.
//   3. Taking `bias` straight. `0.5` is the NEUTRAL value and `1/bias - 2` is
//      what makes it so; a reader who passes `0.5` gets a denominator of `1.5`.
//   4. Expanding the set into one highlight. It expands into seven candidates
//      and five survivors, and two of the five are the same `dark` settings at
//      opposite angles.
//   5. Calling `rim` and `fillDiffuse` present. They are `nil`, spelled in the
//      SAME byte as `blendModeOverride` (19 and 20 against the mode's own 18).
TEST_CASE(highlights_defaults_are_the_read_numbers_and_the_size_table_runs_backwards) {
    std::size_t count = 0;
    const rb::HighlightSlot* slots = rb::glyphHighlightSlots(count);

    // `[BIN]` `0x00030E88` writes seven slots; `0x00033FA4` and `0x00033DE4`
    // make two of them nil; `0x00031384` drops those two.
    CHECK_EQ(count, static_cast<std::size_t>(5));

    // `[BIN]` `0x00064648`: keySharp, and its `distance` is the one place the
    // size classes disagree. `display` is the FIRST slot in memory and the LAST
    // enum value.
    const rb::HighlightSettings& keySharp = slots[0].settings;
    CHECK_EQ(keySharp.brightness, 1.0);
    CHECK_EQ(keySharp.bias, 0.5);
    CHECK_EQ(rb::highlightSizeValue(keySharp.distance, rb::IconSizeClass::Display), 4.0);
    CHECK_EQ(rb::highlightSizeValue(keySharp.distance, rb::IconSizeClass::Small), 6.0);
    // `[BIN]` `A = {1.0, 1.0, 1.0, 0.3}`: opacity drops only in the SMALL class.
    CHECK_EQ(rb::highlightSizeValue(keySharp.opacity, rb::IconSizeClass::Display), 1.0);
    CHECK_EQ(rb::highlightSizeValue(keySharp.opacity, rb::IconSizeClass::Small), 0.3);

    // `[BIN]` `0x000646FC`: keyDiffuse is the thick, low-bias wash.
    CHECK_EQ(rb::highlightSizeValue(slots[1].settings.distance, rb::IconSizeClass::Display), 16.0);
    CHECK_EQ(rb::highlightSizeValue(slots[1].settings.distance, rb::IconSizeClass::Small), 24.0);
    CHECK_EQ(slots[1].settings.bias, 0.08);

    // `[BIN]` `0x00064888`: the dark one, twice, at opposite angles, and the
    // ONLY one of the six with an `outsetOpacity` -- which is exactly the
    // condition `0x000494F8` needs before `outside` is even considered.
    CHECK_EQ(slots[3].settings.brightness, 0.0);
    CHECK(slots[3].isDarklight);
    CHECK(slots[4].isDarklight);
    CHECK(slots[3].settings.outsetOpacity.present);
    CHECK(!slots[0].settings.outsetOpacity.present);
    CHECK(std::abs(slots[3].angleFromKey + slots[4].angleFromKey) < 1e-12);
    CHECK(slots[3].angleFromKey > 0.0);

    // `[BIN]` `0x0004C0A0`: the mode falls out of `brightness` alone, and the
    // two numbers it produces are this project's own enum values.
    rb::SpecularArguments args;
    args.sizeClass = rb::IconSizeClass::Display;
    args.pixelsPerPoint = 1.0;
    const rb::GlassHighlightSettings key = rb::resolveHighlight(slots[0], args);
    const rb::GlassHighlightSettings dark = rb::resolveHighlight(slots[3], args);
    CHECK(key.blendMode == rb::BlendMode::PlusLighter);
    CHECK(dark.blendMode == rb::BlendMode::PlusDarker);

    // `[BIN]` `0x0004BEFC`-`0x0004BF04`: `max(distance * scale, minDistancePixels)`.
    // At 1 px per point the floor of 1 px loses to the 4 pt band.
    CHECK_EQ(key.height, 4.0);
    // At a sixteenth of the canvas the floor wins, and that is the whole point
    // of the field having a name with `Pixels` in it.
    args.pixelsPerPoint = 1.0 / 16.0;
    CHECK_EQ(rb::resolveHighlight(slots[0], args).height, 1.0);
    args.pixelsPerPoint = 1.0;

    // `[BIN]` `0x0004C014`: theta = angleFromKey + longitude, direction on the
    // unit circle. The key light points at +y and the fill at -y.
    CHECK(std::abs(key.directionX) < 1e-12);
    CHECK(std::abs(key.directionY - 1.0) < 1e-12);
    const rb::GlassHighlightSettings fill = rb::resolveHighlight(slots[2], args);
    CHECK(std::abs(fill.directionY + 1.0) < 1e-12);

    // THE CONE. `[BIN]` `spread' = cos(spread)` with `-1000` past pi. A reader
    // who forgets the cosine gets a highlight that is identically zero, which is
    // the failure that looks exactly like "not implemented".
    //
    // `n` is the outward normal; at the top of a shape it is `(0, -1)` in image
    // space, and the key direction reaches the shader as `(x, -y) == (0, -1)`,
    // so the two agree there and disagree at the bottom.
    const double lit = rb::glassHighlightFragment(key, /*sd=*/2.0, /*nx=*/0.0, /*ny=*/-1.0, 1.0);
    const double unlit = rb::glassHighlightFragment(key, /*sd=*/2.0, /*nx=*/0.0, /*ny=*/1.0, 1.0);
    CHECK(lit > 0.5);
    CHECK_EQ(unlit, 0.0);

    // And the band is a band: nothing outside `[inset, inset + height]`.
    CHECK_EQ(rb::glassHighlightFragment(key, /*sd=*/-2.0, 0.0, -1.0, 1.0), 0.0);
    CHECK_EQ(rb::glassHighlightFragment(key, /*sd=*/40.0, 0.0, -1.0, 1.0), 0.0);

    // `[BIN]` `bias' = 1/bias - 2`. At the read `0.5` the denominator is 1, so
    // a fully lit fragment comes through untouched; a reader who passed `0.5`
    // straight in would divide by `1.5` and lose a third of the highlight.
    rb::GlassHighlightSettings flat = key;
    flat.curvature = 0.0;
    flat.spread = 3.2;  // > pi: the `-1000` sentinel
    // `[BIN]` The sentinel is `-1000` and NOT an infinity, so `lit` reaches 1
    // only where the normal faces the light exactly: `(1 + 1000) / 1001`. One
    // step off it is `0.998`, and that 0.2% is the sentinel showing through.
    CHECK(std::abs(rb::glassHighlightFragment(flat, 2.0, 0.0, -1.0, 1.0) - 1.0) < 1e-9);
    CHECK(std::abs(rb::glassHighlightFragment(flat, 2.0, 0.0, 1.0, 1.0) - 999.0 / 1001.0) < 1e-9);
}

// As tres identidades que o laudo dos realces deixou em aberto, agora medidas.
// O caso trava as tres, e trava duas delas como TEOREMA e nao como numero de
// conforto: valem para qualquer valor dos parametros que ainda nao foram lidos.
TEST_CASE(the_three_assumed_identities_are_identities_and_one_of_them_for_any_parameters) {
    std::size_t count = 0;
    const rb::HighlightSlot* slots = rb::glyphHighlightSlots(count);
    CHECK_EQ(count, static_cast<std::size_t>(5));

    rb::SpecularArguments args;
    args.sizeClass = rb::IconSizeClass::Display;
    args.pixelsPerPoint = 1.0;

    // 1. `[BIN]` `ctx[0]` e `GlobalConfiguration.lightIntensity`, e o valor que
    // o alvo assa e `1.0` -- mas ele agora e um CAMPO, e um campo tem de
    // multiplicar. Se alguem trocar o default por 0.5 a opacidade tem de cair
    // pela metade; se `lightIntensity` estiver desligado do resultado, este
    // CHECK e o que percebe.
    const double full = rb::resolveHighlight(slots[0], args).opacity;
    CHECK_EQ(args.lightIntensity, 1.0);
    rb::SpecularArguments dim = args;
    dim.lightIntensity = 0.5;
    CHECK(std::abs(rb::resolveHighlight(slots[0], dim).opacity - full * 0.5) < 1e-12);

    // 2. `[BIN]` `0x00012550` apaga a latitude. Seja qual for `phi`, a direcao
    // que chega ao shader e `(sin theta, cos theta, 0)` -- `z` zerado por
    // `str xzr, [x20, #0x18]` e o comprimento jogado fora pelo `atan2`. Um
    // transcritor que guardasse `cos(phi)` nas componentes planares teria uma
    // direcao mais curta, um `dot` menor, e o realce inteiro enfraqueceria com
    // a luz alta pelo motivo errado.
    rb::SpecularArguments tilted = args;
    tilted.lightLatitude = 0.7;
    const rb::GlassHighlightSettings up = rb::resolveHighlight(slots[0], tilted);
    const rb::GlassHighlightSettings flatLight = rb::resolveHighlight(slots[0], args);
    CHECK(std::abs(up.directionX - flatLight.directionX) < 1e-12);
    CHECK(std::abs(up.directionY - flatLight.directionY) < 1e-12);
    CHECK_EQ(up.directionZ, 0.0);
    CHECK(std::abs(up.directionX * up.directionX + up.directionY * up.directionY - 1.0) < 1e-12);

    // 3. `[BIN]` A pos-passagem espacial e a IDENTIDADE em `phi == 0`, e a
    // prova nao depende dos seis numeros: `hypot(dir.xy) == |cos phi| == 1`,
    // `sin` de qualquer angulo e no maximo 1, logo `t = max(0, 1 - 1/sin) == 0`
    // e `pow(0, p) == 0` colapsa as quatro reescritas. O laco abaixo passa
    // parametros diferentes e absurdos de proposito: se algum deles mover um
    // bit, a leitura de `0x12550` esta errada.
    const double ranges[4] = {0.3, 1.0, 1.5707963267948966, 3.0};
    for (int i = 0; i < 4; ++i) {
        rb::SpatialHighlighting p;
        p.read = true;
        p.alignmentRange = ranges[i];
        p.intensityPower = 0.5 + i;
        p.minIntensity = 0.1 * i;
        p.spreadPower = 1.0 + i;
        p.heightPower = 2.0;
        p.maxExtraHeight = 3.0 + i;

        const rb::GlassHighlightSettings before = rb::resolveHighlight(slots[0], args);
        rb::GlassHighlightSettings after = before;
        rb::spatialHighlight(after, p);
        CHECK(std::abs(after.opacity - before.opacity) < 1e-12);
        CHECK(std::abs(after.spread - before.spread) < 1e-12);
        CHECK(std::abs(after.height - before.height) < 1e-12);
        CHECK(std::abs(after.directionX - before.directionX) < 1e-12);
        CHECK(std::abs(after.directionY - before.directionY) < 1e-12);
    }

    // E a passagem NAO e a funcao vazia: com a luz saindo do plano ela morde,
    // e morde nos tres sentidos que `0x125BC`-`0x12638` escrevem.
    rb::SpatialHighlighting live;
    live.read = true;
    live.alignmentRange = 1.5707963267948966;  // sin == 1
    live.intensityPower = 1.0;
    live.minIntensity = 0.25;
    live.spreadPower = 1.0;
    live.heightPower = 1.0;
    live.maxExtraHeight = 2.0;
    rb::GlassHighlightSettings bitten = rb::resolveHighlight(slots[0], args);
    bitten.directionX = 0.0;
    bitten.directionY = 0.5;  // hypot == 0.5 -> t == 0.5
    bitten.directionZ = 0.8660254037844387;
    const double beforeSpread = bitten.spread;
    const double beforeHeight = bitten.height;
    const double beforeOpacity = bitten.opacity;
    rb::spatialHighlight(bitten, live);
    // `opacity *= min + (1-min)*(1 - t^p)`, com `t = 0.5`, `p = 1`, `min = 0.25`
    CHECK(std::abs(bitten.opacity - beforeOpacity * (0.25 + 0.75 * 0.5)) < 1e-12);
    // `spread += t^p * (pi - spread)`
    CHECK(std::abs(bitten.spread - (beforeSpread + 0.5 * (3.14159265358979323846 - beforeSpread)))
          < 1e-12);
    // `height *= 1 + maxExtraHeight * t^p`
    CHECK(std::abs(bitten.height - beforeHeight * (1.0 + 2.0 * 0.5)) < 1e-12);
    // e a direcao volta normalizada e plana, como nos dois ramos
    CHECK_EQ(bitten.directionZ, 0.0);
    CHECK(std::abs(bitten.directionY - 1.0) < 1e-12);
}

// `[BIN]` A largura da banda e a HALF `0xH3AAA` e nao a decimal de onde ela
// veio. Cinco partes em dez mil so aparecem na borda fina -- que e exatamente
// onde este realce vive.
TEST_CASE(the_band_width_constant_is_the_half_the_shader_multiplies_by) {
    rb::GlassHighlightSettings s;
    s.height = 100.0;
    s.spread = 3.2;  // sentinela: `lit == 1` na direcao certa
    s.bias = 0.5;    // `bias' == 0`, denominador 1
    s.curvature = 0.0;
    s.directionX = 0.0;
    s.directionY = 1.0;
    // A borda de dentro e `saturate(sd/w + 0.5)`, e o joelho dela esta em
    // `sd == w/2`. E so ali que as duas leituras da constante se separam.
    const double w = 0.8330078125;
    CHECK(rb::glassHighlightFragment(s, w * 0.5 + 1e-9, 0.0, -1.0, 1.0) >= 1.0 - 1e-9);
    CHECK(rb::glassHighlightFragment(s, w * 0.5 - 1e-6, 0.0, -1.0, 1.0) < 1.0);
    // a decimal antiga `0.83349` poria o joelho depois deste ponto, e portanto
    // ainda daria banda cheia onde a half ja nao da
    const double oldKnee = 0.83349 * 0.5;
    CHECK(rb::glassHighlightFragment(s, oldKnee - 1e-9, 0.0, -1.0, 1.0) >= 1.0 - 1e-9);
}
