#include "check.h"
#include "Source/IconComposerFoundation/Values.h"

using namespace icf;

// A colour is `"<space>:<components>"`. Measured over the corpus's 576 colours:
// five spaces, and the two grey ones carry TWO components, not four. A parser
// that assumes RGBA is wrong on 163 of them.
TEST_CASE(color_parses_a_display_p3_quadruple) {
    auto c = colorFromString("display-p3:0.87451,0.58447,0.00392,1.00000");
    REQUIRE(c.has_value());
    CHECK(c->space == ColorSpace::DisplayP3);
    REQUIRE(c->count == 4);
    CHECK(c->components[0] > 0.87450 && c->components[0] < 0.87452);
    CHECK(c->components[3] == 1.0);
}

TEST_CASE(color_parses_a_grey_pair) {
    auto c = colorFromString("extended-gray:1.00000,1.00000");
    REQUIRE(c.has_value());
    CHECK(c->space == ColorSpace::ExtendedGray);
    CHECK(c->count == 2);
}

TEST_CASE(color_rejects_an_unknown_space) {
    CHECK(!colorFromString("cmyk:0,0,0,1").has_value());
    CHECK(!colorFromString("0.5,0.5,0.5,1").has_value());
}

// The component count is a property OF THE SPACE. Accepting four components for
// a grey would silently read a colour nobody wrote.
TEST_CASE(color_rejects_the_wrong_component_count_for_its_space) {
    CHECK(!colorFromString("gray:1.0,1.0,1.0,1.0").has_value());
    CHECK(!colorFromString("srgb:1.0,1.0").has_value());
}

TEST_CASE(color_rejects_a_component_that_is_not_a_number) {
    CHECK(!colorFromString("srgb:1.0,1.0,red,1.0").has_value());
    CHECK(!colorFromString("srgb:1.0,,1.0,1.0").has_value());
}

// The case the two above do NOT cover, and the mutation sweep is how that came
// out: `red` and `` fail the conversion outright, so neither notices whether the
// reader requires the WHOLE field to be the number. `1.0x` converts fine and
// stops early -- a reader that accepts a prefix reads it as 1.0 and says nothing.
TEST_CASE(color_rejects_a_component_with_something_after_the_number) {
    CHECK(!colorFromString("srgb:1.0x,1.0,1.0,1.0").has_value());
    CHECK(!colorFromString("srgb:1.0,1.0,1.0,1.0 ").has_value());
}

// ---- the closed vocabularies -------------------------------------------
//
// Ten blend modes, not seventeen (doc 01 §6). The spelling on disk is the
// kebab-case of the Swift case name -- `plusDarker` is written `plus-darker`.
TEST_CASE(blend_mode_parses_all_ten_and_nothing_else) {
    CHECK(blendModeFromString("normal") == BlendMode::Normal);
    CHECK(blendModeFromString("plus-lighter") == BlendMode::PlusLighter);
    CHECK(blendModeFromString("plus-darker") == BlendMode::PlusDarker);
    CHECK(blendModeFromString("overlay") == BlendMode::Overlay);
    CHECK(blendModeFromString("multiply") == BlendMode::Multiply);
    CHECK(blendModeFromString("soft-light") == BlendMode::SoftLight);
    CHECK(blendModeFromString("hard-light") == BlendMode::HardLight);
    CHECK(blendModeFromString("darken") == BlendMode::Darken);
    CHECK(blendModeFromString("lighten") == BlendMode::Lighten);
    CHECK(blendModeFromString("screen") == BlendMode::Screen);
    CHECK(!blendModeFromString("plusDarker").has_value());   // the Swift spelling
    CHECK(!blendModeFromString("color-burn").has_value());   // a mode this format cannot name
}

TEST_CASE(shadow_kind_parses_its_four_cases) {
    CHECK(shadowKindFromString("automatic") == ShadowKind::Automatic);
    CHECK(shadowKindFromString("neutral") == ShadowKind::Neutral);
    CHECK(shadowKindFromString("layer-color") == ShadowKind::LayerColor);
    CHECK(shadowKindFromString("none") == ShadowKind::None);
}

// ---- the value shapes ---------------------------------------------------

namespace {
icf::json::Value node(const char* t) { return *icf::json::parse(t); }
}  // namespace

TEST_CASE(fill_reads_a_bare_string_as_a_kind) {
    auto f = fillFrom(node(R"("automatic")"));
    REQUIRE(f.has_value());
    CHECK(f->kind == FillKind::Automatic);
    CHECK(f->colors.empty());
}

TEST_CASE(fill_reads_a_solid_colour) {
    auto f = fillFrom(node(R"({"solid":"srgb:1.00000,0.50000,0.00000,1.00000"})"));
    REQUIRE(f.has_value());
    CHECK(f->kind == FillKind::Solid);
    REQUIRE(f->colors.size() == 1);
    CHECK(f->colors[0].space == ColorSpace::SRGB);
}

// `automatic-gradient` carries ONE colour; the rest of the ramp is derived by
// the renderer, not written down (doc 01 §10.4).
TEST_CASE(fill_reads_an_automatic_gradient_as_a_single_colour) {
    auto f = fillFrom(node(R"({"automatic-gradient":"display-p3:0.00000,0.50588,1.00000,1.00000"})"));
    REQUIRE(f.has_value());
    CHECK(f->kind == FillKind::AutomaticGradient);
    CHECK(f->colors.size() == 1);
}

TEST_CASE(fill_reads_a_linear_gradient_with_its_orientation) {
    auto f = fillFrom(node(R"({"linear-gradient":["srgb:0,0,0,1","srgb:1,1,1,1"],
                               "orientation":{"start":{"x":0.5,"y":0},"stop":{"x":0.5,"y":0.7}}})"));
    REQUIRE(f.has_value());
    CHECK(f->kind == FillKind::LinearGradient);
    REQUIRE(f->colors.size() == 2);
    REQUIRE(f->orientation.has_value());
    CHECK(f->orientation->stop.y == 0.7);
}

TEST_CASE(fill_rejects_a_colour_it_cannot_read) {
    CHECK(!fillFrom(node(R"({"solid":"cmyk:0,0,0,1"})")).has_value());
}

TEST_CASE(shadow_reads_its_kind_and_opacity) {
    auto s = shadowFrom(node(R"({"kind":"neutral","opacity":0.5})"));
    REQUIRE(s.has_value());
    CHECK(s->kind == ShadowKind::Neutral);
    CHECK(s->opacity == 0.5);
}

TEST_CASE(position_reads_its_scale_and_translation) {
    auto p = positionFrom(node(R"({"scale":1.5,"translation-in-points":[10,-20]})"));
    REQUIRE(p.has_value());
    CHECK(p->scale == 1.5);
    CHECK(p->translation.x == 10.0);
    CHECK(p->translation.y == -20.0);
}

TEST_CASE(translucency_and_refractivity_read_their_fields) {
    auto t = translucencyFrom(node(R"({"enabled":true,"value":0.25})"));
    REQUIRE(t.has_value());
    CHECK(t->enabled == true);
    CHECK(t->value == 0.25);

    auto r = refractivityFrom(node(R"({"enabled":false,"strength":0.4,"depth":8})"));
    REQUIRE(r.has_value());
    CHECK(r->enabled == false);
    CHECK(r->strength == 0.4);
    CHECK(r->depth == 8.0);
}
