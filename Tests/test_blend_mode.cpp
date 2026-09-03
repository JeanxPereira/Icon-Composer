// The blend vocabulary and the two-hop translation.
//
// The load-bearing test here is `the_cg_hop_agrees_with_the_public_numbering`.
// Everything else pins a transcription against itself, which catches a typo and
// nothing more. That one pins it against **CoreGraphics**, a numbering
// published by Apple that nobody in this project chose -- so a table that was
// read out of the wrong offset, or off by a row, cannot pass it.
#include "check.h"

#include "Source/RenderBox/BlendMode.h"

using rb::BlendMode;

TEST_CASE(every_engine_mode_reaches_a_named_shader_case) {
    // Total, and the name is how a wrong case shows up as a wrong WORD instead
    // of a wrong number -- `43` and `44` look alike in a diff; `plus_lighter`
    // and `plus_darker` do not.
    struct Row { BlendMode mode; std::uint32_t rbCase; std::string_view name; };
    const Row rows[] = {
        {BlendMode::Normal,       2,  "source_over"},
        {BlendMode::Darken,       27, "darken"},
        {BlendMode::Multiply,     25, "multiply"},
        {BlendMode::ColorBurn,    30, "color_burn"},
        {BlendMode::PlusDarker,   44, "plus_darker"},
        {BlendMode::Lighten,      28, "lighten"},
        {BlendMode::Screen,       12, "screen"},
        {BlendMode::ColorDodge,   29, "color_dodge"},
        {BlendMode::PlusLighter,  43, "plus_lighter"},
        {BlendMode::Overlay,      26, "overlay"},
        {BlendMode::SoftLight,    31, "soft_light"},
        {BlendMode::HardLight,    32, "hard_light"},
        {BlendMode::Difference,   33, "difference"},
        {BlendMode::Exclusion,    14, "exclusion"},
        {BlendMode::Hue,          36, "hue"},
        {BlendMode::Saturation,   37, "saturation"},
        {BlendMode::Color,        38, "color"},
        {BlendMode::Luminosity,   39, "luminosity"},
    };
    CHECK_EQ(static_cast<int>(sizeof(rows) / sizeof(rows[0])), rb::kBlendModeCount);
    for (const Row& r : rows) {
        CHECK_EQ(rb::renderBoxCaseOf(r.mode), r.rbCase);
        CHECK(rb::renderBoxCaseName(r.rbCase) == r.name);
    }
}

// THE ONE THAT CHECKS FROM OUTSIDE. `CGBlendMode` is public API, and its
// numbering was fixed by Apple long before this project existed. Both tables
// are transcribed from binaries; this asserts their COMPOSITION lands on the
// case whose own name matches what CoreGraphics calls that index.
//
// It covers all 28 entries of `cg_table`, including the twelve no `.icon` can
// reach -- those twelve are the control. A table read one row off would still
// look self-consistent across the sixteen colour modes and would break here.
TEST_CASE(the_cg_hop_agrees_with_the_public_numbering) {
    struct Row { std::uint32_t cg; std::string_view caseName; };
    const Row rows[] = {
        { 0, "source_over"},   // CGBlendModeNormal
        { 1, "multiply"},      { 2, "screen"},        { 3, "overlay"},
        { 4, "darken"},        { 5, "lighten"},       { 6, "color_dodge"},
        { 7, "color_burn"},    { 8, "soft_light"},    { 9, "hard_light"},
        {10, "difference"},    {11, "exclusion"},     {12, "hue"},
        {13, "saturation"},    {14, "color"},         {15, "luminosity"},
        {16, "clear"},         {17, "copy"},          {18, "source_in"},
        {19, "source_out"},    {20, "source_atop"},   {21, "dest_over"},
        {22, "dest_in"},       {23, "dest_out"},      {24, "dest_atop"},
        {25, "exclusive_or"},  // CGBlendModeXOR
        {26, "plus_darker"},   {27, "plus_lighter"},
    };
    for (const Row& r : rows) {
        auto rbCase = rb::renderBoxCaseOfCg(r.cg);
        REQUIRE(rbCase.has_value());
        CHECK(rb::renderBoxCaseName(*rbCase) == r.caseName);
    }
    // The table ends at 27, and reading past it is a gap, not a `normal`.
    CHECK(!rb::renderBoxCaseOfCg(28).has_value());
}

// The bridge between the format's ten and the engine's eighteen is by NAME, and
// the two orders differ. `plus-lighter` is tag 1 in the format and tag 8 in the
// engine: a cast would compile, run, and silently produce `darken`.
TEST_CASE(the_format_enum_is_bridged_by_name_and_not_by_tag) {
    CHECK_EQ(static_cast<int>(icf::BlendMode::PlusLighter), 1);
    CHECK_EQ(static_cast<int>(rb::blendModeOf(icf::BlendMode::PlusLighter)), 8);
    CHECK_EQ(rb::renderBoxCaseOf(rb::blendModeOf(icf::BlendMode::PlusLighter)),
             std::uint32_t{43});

    // And the whole of the format's vocabulary lands somewhere sane.
    const icf::BlendMode all[] = {
        icf::BlendMode::Normal, icf::BlendMode::PlusLighter,
        icf::BlendMode::PlusDarker, icf::BlendMode::Overlay,
        icf::BlendMode::Multiply, icf::BlendMode::SoftLight,
        icf::BlendMode::HardLight, icf::BlendMode::Darken,
        icf::BlendMode::Lighten, icf::BlendMode::Screen,
    };
    for (icf::BlendMode m : all) {
        const BlendMode e = rb::blendModeOf(m);
        // Round trip through the JSON spelling: the format can name every one
        // of its own ten, and the name has to come back to the same mode.
        const std::string_view key = rb::blendModeKey(e);
        CHECK(!key.empty());
        auto back = rb::blendModeFromKey(key);
        REQUIRE(back.has_value());
        CHECK(*back == e);
    }
}

// The eight the format cannot spell have NO key, and that emptiness is a
// measurement rather than an oversight: no `icon.json` spelling for them was
// found, so none was invented. A kebab-case guess would be indistinguishable
// from a reading, which is the one thing this project will not ship.
TEST_CASE(the_eight_modes_the_format_cannot_name_have_no_key) {
    const BlendMode unnameable[] = {
        BlendMode::ColorBurn, BlendMode::ColorDodge, BlendMode::Difference,
        BlendMode::Exclusion, BlendMode::Hue, BlendMode::Saturation,
        BlendMode::Color, BlendMode::Luminosity,
    };
    for (BlendMode m : unnameable) {
        CHECK(rb::blendModeKey(m).empty());
        // ...but they still translate. Unreachable from a document is not the
        // same as unsupported by the engine, and conflating the two is how a
        // renderer ends up refusing something the target draws.
        CHECK(rb::renderBoxCaseOf(m) != 0u);
    }
    CHECK(!rb::blendModeFromKey("hue").has_value());
    CHECK(!rb::blendModeFromKey("color-burn").has_value());
    CHECK(!rb::blendModeFromKey("").has_value());
    CHECK(!rb::blendModeFromKey("Normal").has_value());  // case matters
}

// `blend_name` is guarded by `cmp w0, #0x37` in the binary, and the shader's
// own switch lists a case 56 the table has no entry for. That discrepancy is
// recorded as unread rather than filled with a plausible label.
TEST_CASE(the_case_name_table_stops_where_the_binary_stops) {
    CHECK(rb::renderBoxCaseName(0) == "copy");
    CHECK(rb::renderBoxCaseName(55) == "custom_complex");
    CHECK(rb::renderBoxCaseName(56).empty());
    CHECK(rb::renderBoxCaseName(9999).empty());
}
