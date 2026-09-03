// The fill resolution: seven document cases, two converters, fourteen rows.
//
// What is pinned here is chosen by one question -- what would a plausible
// TRANSCRIPTION have got wrong? This unit has an unusually clear answer,
// because a plausible transcription is exactly the thing that was written
// first: ONE converter for both positions. Six of the fourteen rows agree, and
// the tests below are aimed at the eight that do not:
//
//   1. Sharing the `none` arm. It is `nil` on a layer and the SYSTEM PATH on
//      the background -- the same word, opposite answers. A single converter
//      has to pick one and is then wrong 49 times or 0 times depending on which.
//   2. Reading `automatic` as "the chiclet ramp, appearance picks the pole".
//      That is the hypothesis this spec was born from, and it is half right:
//      under `tinted` the background is `IconColor.clear`, and a two-armed
//      switch draws a light ramp over a rendition that must be transparent.
//   3. Reading the layer's `automatic` as a colour at all. It is an operator on
//      specializations -- the answer this same layer gives at the `light` slot
//      -- and under `light` it is nil. Anything that returns a ramp here paints
//      55 corpus layers that must stay unpainted.
//   4. Giving `automatic-gradient` the document's `orientation`. Three of the
//      four gradient sites pass `placement: nil`, and 8 corpus fills carry an
//      orientation that no site reads.
//   5. Giving the BACKGROUND's `linear-gradient` its orientation. The one site
//      of four that reads `orientation` is the LAYER's. 8 background fills at
//      the document root and 6 more inside specializations carry one that is
//      discarded.
//   6. Reaching for `find("fill")` instead of `resolve`. 35 of the corpus's 69
//      background specialization entries and 243 of its 367 layer entries carry
//      a predicate; a `find` reads none of them.
//   7. Pairing `system-light` with the light APPEARANCE. The document names a
//      ramp, not a condition: `system-light` is the light ramp under `dark`.
//   8. Bounding the layer's inheritance with a depth limit. The terminator is
//      read, not invented -- the light-slot pass maps `automatic` to nil.
#include "check.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/RenderBox/FillResolve.h"

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

bool near(double a, double b) { return std::fabs(a - b) < 1e-12; }

// A parsed tree that owns itself, so a view over it stays valid for the length
// of a test.
struct Doc {
    std::optional<icf::json::Value> tree;
    explicit Doc(const std::string& text) : tree(icf::json::parse(text)) {}
    bool ok() const { return tree.has_value(); }
    const icf::json::Value& root() const { return *tree; }
    icf::Layer layer() const { return icf::Layer(*tree); }
};

// The document `Fill` for a JSON fill value, so a converter test can name its
// input in the format's own words instead of building a struct by hand.
icf::Fill fillOf(const std::string& json) {
    auto v = icf::json::parse(json);
    auto f = icf::fillFrom(*v);
    return *f;
}

const char* kRed = "\"display-p3:1.00000,0.00000,0.00000,1.00000\"";
const char* kBlue = "\"display-p3:0.00000,0.00000,1.00000,1.00000\"";

std::string outcomeName(FillOutcome o) {
    switch (o) {
        case FillOutcome::Resolved: return "resolved";
        case FillOutcome::NoFill: return "no-fill";
        case FillOutcome::Refused: return "refused";
    }
    return "?";
}

std::string contentsName(ResolvedFill::Contents c) {
    switch (c) {
        case ResolvedFill::Contents::Solid: return "solid";
        case ResolvedFill::Contents::AutomaticGradient: return "automatic-gradient";
        case ResolvedFill::Contents::Gradient: return "gradient";
        case ResolvedFill::Contents::System: return "system";
    }
    return "?";
}

std::string rampName(SystemFill r) {
    return r == SystemFill::Dark ? "dark-chiclet" : "light-chiclet";
}

// The four appearances, in the enum's own order.
const icf::Appearance kAppearances[] = {icf::Appearance::Base, icf::Appearance::Light,
                                        icf::Appearance::Dark, icf::Appearance::Tinted};

std::string appearanceName(icf::Appearance a) {
    switch (a) {
        case icf::Appearance::Base: return "base";
        case icf::Appearance::Light: return "light";
        case icf::Appearance::Dark: return "dark";
        case icf::Appearance::Tinted: return "tinted";
    }
    return "?";
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
// The seven cases, and the numbering everything else hangs off
// ---------------------------------------------------------------------------

// `[BIN]` The tag order was read from `Fill.Kind.displayName`
// (`IconComposerFoundation 0x0A8D40`), an instruction that NAMES the cases.
// This pins the numbering the two converters' switches are indexed by, so that
// a future edit to `icf::FillKind`'s declaration order is a failing test rather
// than a silently shifted table.
TEST_CASE(the_seven_fill_kinds_are_in_the_order_the_displayname_chain_names) {
    CHECK_EQ(static_cast<int>(icf::FillKind::None), 0);
    CHECK_EQ(static_cast<int>(icf::FillKind::Automatic), 1);
    CHECK_EQ(static_cast<int>(icf::FillKind::Solid), 2);
    CHECK_EQ(static_cast<int>(icf::FillKind::AutomaticGradient), 3);
    CHECK_EQ(static_cast<int>(icf::FillKind::LinearGradient), 4);
    CHECK_EQ(static_cast<int>(icf::FillKind::SystemLight), 5);
    CHECK_EQ(static_cast<int>(icf::FillKind::SystemDark), 6);

    // And the format's spelling of each, which is what a document says. The
    // two that matter are 3 and 4: the UI calls them "Standard Gradient" and
    // "Custom Gradient", so matching cases to display names by spelling would
    // have put `automatic-gradient` one case out.
    CHECK(icf::fillKindFromString("none") == icf::FillKind::None);
    CHECK(icf::fillKindFromString("automatic") == icf::FillKind::Automatic);
    CHECK(icf::fillKindFromString("automatic-gradient") == icf::FillKind::AutomaticGradient);
    CHECK(icf::fillKindFromString("linear-gradient") == icf::FillKind::LinearGradient);
    CHECK(icf::fillKindFromString("system-light") == icf::FillKind::SystemLight);
    CHECK(icf::fillKindFromString("system-dark") == icf::FillKind::SystemDark);
}

// `[BIN]` The packed byte table `0x0000'0303'0303'0201` at
// `IconComposerFoundation 0x3D374`. The finding is the collapse: FOUR of the
// six renditions reach `tinted`, which is the arm that makes the automatic
// background transparent. A transcription that mapped only `lightTint` there
// would render three renditions with a chiclet ramp they must not have.
TEST_CASE(rendition_source_appearance_collapses_four_renditions_onto_tinted) {
    CHECK(sourceAppearance(Rendition::LightColor) == icf::Appearance::Light);
    CHECK(sourceAppearance(Rendition::DarkColor) == icf::Appearance::Dark);
    CHECK(sourceAppearance(Rendition::LightTint) == icf::Appearance::Tinted);
    CHECK(sourceAppearance(Rendition::DarkTint) == icf::Appearance::Tinted);
    CHECK(sourceAppearance(Rendition::LightClear) == icf::Appearance::Tinted);
    CHECK(sourceAppearance(Rendition::DarkClear) == icf::Appearance::Tinted);

    // No rendition produces `base`. The background switch has an arm for it
    // anyway, because its compare is unsigned -- see the background tests.
    for (int r = 0; r <= 5; ++r) {
        CHECK(sourceAppearance(static_cast<Rendition>(r)) != icf::Appearance::Base);
    }
}

// `[BIN]` `IconColor.clear`, `IconRendering 0x3DB0C`: four zeroes.
TEST_CASE(icon_color_clear_is_four_zeroes) {
    const icf::Color c = iconColorClear();
    CHECK_EQ(c.count, 4);
    for (int i = 0; i < 4; ++i) CHECK(near(c.components[i], 0.0));
}

// ---------------------------------------------------------------------------
// THE BACKGROUND CONVERTER -- seven rows
// ---------------------------------------------------------------------------

// ROW 1 -- `none`, and it is the row that breaks the shared-converter design.
// `[BIN]` `0x10AEE8` is a `cbz` into `0x10AEF4`, the block `automatic` reaches
// from `0x10AEF0`. The same block, not a similar one -- so the two cases must
// agree under EVERY appearance, which is what this checks rather than checking
// one.
TEST_CASE(background_none_takes_the_very_same_path_as_automatic) {
    for (icf::Appearance a : kAppearances) {
        const FillResolution n = backgroundFillFrom(fillOf("\"none\""), a);
        const FillResolution m = backgroundFillFrom(fillOf("\"automatic\""), a);
        CHECK_EQ(outcomeName(n.outcome), std::string("resolved"));
        CHECK_EQ(outcomeName(m.outcome), std::string("resolved"));
        CHECK(identical(n.fill, m.fill));
    }
}

// ROW 2 -- the appearance switch, all three arms.
//
// `[BIN]` `0x10AEF4`: `cmp w8, #2` / `b.lo` / `b.eq` / fall through. The `b.lo`
// is UNSIGNED and so covers `base` as well as `light`; `base` is in the light
// arm because of the branch's shape, not because anything chose it.
TEST_CASE(background_automatic_under_base_and_light_is_the_light_chiclet_ramp) {
    for (icf::Appearance a : {icf::Appearance::Base, icf::Appearance::Light}) {
        const FillResolution r = backgroundFillFrom(fillOf("\"automatic\""), a);
        CHECK_EQ(outcomeName(r.outcome), std::string("resolved"));
        CHECK_EQ(contentsName(r.fill.contents), std::string("system"));
        CHECK_EQ(rampName(r.fill.ramp), std::string("light-chiclet"));
        // `[BIN]` `.system(_, 1.0, true)` at all four sites.
        CHECK(near(r.fill.opacity, 1.0));
        CHECK(r.fill.thirdArgument);
    }
}

TEST_CASE(background_automatic_under_dark_is_the_dark_chiclet_ramp) {
    const FillResolution r = backgroundFillFrom(fillOf("\"automatic\""), icf::Appearance::Dark);
    CHECK_EQ(contentsName(r.fill.contents), std::string("system"));
    CHECK_EQ(rampName(r.fill.ramp), std::string("dark-chiclet"));
}

// THE ARM NOBODY PREDICTED, and the one a two-armed switch deletes silently.
// Under `tinted` the automatic background is not a grey and not a ramp: it is
// `Fill.solid(IconColor.clear)`, alpha 0. `[BIN]` the fall-through of
// `0x10AEF4`. Four of the six renditions land here.
TEST_CASE(background_automatic_under_tinted_is_a_transparent_solid) {
    const FillResolution r = backgroundFillFrom(fillOf("\"automatic\""), icf::Appearance::Tinted);
    CHECK_EQ(outcomeName(r.outcome), std::string("resolved"));
    CHECK_EQ(contentsName(r.fill.contents), std::string("solid"));
    CHECK_EQ(r.fill.primary.count, 4);
    for (int i = 0; i < 4; ++i) CHECK(near(r.fill.primary.components[i], 0.0));
    // The alpha specifically: a clear that was a black would pass a test that
    // only looked at rgb.
    CHECK(near(r.fill.primary.components[3], 0.0));

    // And `none` goes with it, through the same block.
    const FillResolution n = backgroundFillFrom(fillOf("\"none\""), icf::Appearance::Tinted);
    CHECK(identical(n.fill, r.fill));
}

// ROW 3 -- `solid`. `[BIN]` `0x10AF68`, `primaryColor` at `Fill+0x08`.
TEST_CASE(background_solid_is_the_primary_colour_under_every_appearance) {
    const icf::Fill f = fillOf(std::string("{\"solid\" : ") + kRed + "}");
    for (icf::Appearance a : kAppearances) {
        const FillResolution r = backgroundFillFrom(f, a);
        CHECK_EQ(contentsName(r.fill.contents), std::string("solid"));
        CHECK(near(r.fill.primary.components[0], 1.0));
        CHECK(near(r.fill.primary.components[2], 0.0));
        CHECK(!r.fill.placement.has_value());
    }
}

// ROW 4 -- `automatic-gradient`. `[BIN]` `0x10AE74`, `placement: nil`.
TEST_CASE(background_automatic_gradient_is_one_colour_and_no_placement) {
    // The fixture NAMES an orientation, so that a converter which honoured it
    // fails here rather than passing on a fill that had none to honour.
    // `[ART]` 4 corpus background `automatic-gradient` fills carry one.
    const icf::Fill f =
        fillOf(std::string("{\"automatic-gradient\" : ") + kRed +
               ", \"orientation\" : {\"start\" : {\"x\" : 0.1, \"y\" : 0.2},"
               " \"stop\" : {\"x\" : 0.3, \"y\" : 0.4}}}");
    CHECK(f.orientation.has_value());
    const FillResolution r = backgroundFillFrom(f, icf::Appearance::Light);
    CHECK_EQ(contentsName(r.fill.contents), std::string("automatic-gradient"));
    CHECK(near(r.fill.primary.components[0], 1.0));
    CHECK(!r.fill.placement.has_value());
}

// ROW 5 -- `linear-gradient`, and THE CORRECTION OF §5.2. `[BIN]` `0x10AFAC`
// passes `placement: nil`; the background converter reads `Fill+0x08` and
// `Fill+0x30` and never `Fill+0x58`. So a document that names an orientation
// here has it DISCARDED -- which is 8 corpus fills at the root and 6 in
// specializations, and is the behaviour our own renderer most likely does not
// have yet.
TEST_CASE(background_linear_gradient_discards_the_orientation_the_document_names) {
    const icf::Fill f = fillOf(std::string("{\"linear-gradient\" : [") + kRed + ", " + kBlue +
                               "], \"orientation\" : {\"start\" : {\"x\" : 0.25, \"y\" : 0.5},"
                               " \"stop\" : {\"x\" : 0.75, \"y\" : 1.0}}}");
    // The document really does carry it -- otherwise this test would pass for
    // the wrong reason.
    CHECK(f.orientation.has_value());

    const FillResolution r = backgroundFillFrom(f, icf::Appearance::Light);
    CHECK_EQ(contentsName(r.fill.contents), std::string("gradient"));
    CHECK(near(r.fill.primary.components[0], 1.0));
    CHECK(near(r.fill.secondary.components[2], 1.0));
    CHECK(!r.fill.placement.has_value());
}

// ROWS 6 and 7 -- `system-light` / `system-dark`. `[BIN]` `0x10AF14` and
// `0x10AF10`. The document names a RAMP, not a condition: `system-light` stays
// the light ramp under `dark`, which is what makes these two cases different
// from `automatic`.
TEST_CASE(background_system_cases_name_a_ramp_and_not_an_appearance) {
    for (icf::Appearance a : kAppearances) {
        const FillResolution l = backgroundFillFrom(fillOf("\"system-light\""), a);
        CHECK_EQ(contentsName(l.fill.contents), std::string("system"));
        CHECK_EQ(rampName(l.fill.ramp), std::string("light-chiclet"));

        const FillResolution d = backgroundFillFrom(fillOf("\"system-dark\""), a);
        CHECK_EQ(contentsName(d.fill.contents), std::string("system"));
        CHECK_EQ(rampName(d.fill.ramp), std::string("dark-chiclet"));
    }
}

// ---------------------------------------------------------------------------
// THE LAYER CONVERTER -- seven rows
// ---------------------------------------------------------------------------

// ROW 1 -- `none` is `nil`. `[BIN]` `0x10C0F0` -> `0x10C15C`.
TEST_CASE(layer_none_is_nil_under_every_appearance) {
    for (icf::Appearance a : kAppearances) {
        const FillResolution r = layerFillFrom(fillOf("\"none\""), a, nullptr);
        CHECK_EQ(outcomeName(r.outcome), std::string("no-fill"));
    }
}

// THE ASYMMETRY, pinned in one place. The same document word, the two
// converters, and they do not merely differ -- one of them answers something
// and the other answers nothing.
TEST_CASE(none_means_opposite_things_on_a_layer_and_on_the_background) {
    for (icf::Appearance a : kAppearances) {
        const FillResolution layer = layerFillFrom(fillOf("\"none\""), a, nullptr);
        const FillResolution background = backgroundFillFrom(fillOf("\"none\""), a);
        CHECK_EQ(outcomeName(layer.outcome), std::string("no-fill"));
        CHECK_EQ(outcomeName(background.outcome), std::string("resolved"));
    }
    // And the background's answer is a DRAWN one in three of the four: a
    // chiclet ramp, not an empty. `[ART]` The corpus never exercises this --
    // `none` appears 0 times on a background across 145 documents -- which is
    // itself the prediction the reading made: a `none` that behaves exactly
    // like `automatic` is redundant, and the format's writers never write it.
    CHECK_EQ(contentsName(backgroundFillFrom(fillOf("\"none\""), icf::Appearance::Light)
                              .fill.contents),
             std::string("system"));
}

// ROW 2 -- `automatic`, and it is not a colour.
//
// `[BIN]` `0x10C0FC`: under the `light` slot it is nil. This is both a row of
// the table and the terminator of the inheritance below.
TEST_CASE(layer_automatic_under_light_is_nil) {
    const FillResolution r = layerFillFrom(fillOf("\"automatic\""), icf::Appearance::Light,
                                           nullptr);
    CHECK_EQ(outcomeName(r.outcome), std::string("no-fill"));

    // Even when a light slot exists to inherit from: the `light` arm returns
    // before it is read. Otherwise `automatic` under light would inherit its
    // own answer.
    const icf::Fill red = fillOf(std::string("{\"solid\" : ") + kRed + "}");
    const FillResolution s = layerFillFrom(fillOf("\"automatic\""), icf::Appearance::Light, &red);
    CHECK_EQ(outcomeName(s.outcome), std::string("no-fill"));
}

// `[BIN]` `0x10C138`: a VERBATIM COPY of what this same layer's fill resolves
// at the `light` slot. Checked as a whole-result comparison, because "verbatim"
// is the claim -- a spot check of one field would pass for a converter that
// re-derived the answer under the current appearance instead of copying.
TEST_CASE(layer_automatic_inherits_the_light_slot_verbatim) {
    const icf::Fill lightSlot =
        fillOf(std::string("{\"linear-gradient\" : [") + kRed + ", " + kBlue +
               "], \"orientation\" : {\"start\" : {\"x\" : 0.0, \"y\" : 0.25},"
               " \"stop\" : {\"x\" : 1.0, \"y\" : 0.75}}}");
    const FillResolution want = layerFillFrom(lightSlot, icf::Appearance::Light, nullptr);
    REQUIRE(want.outcome == FillOutcome::Resolved);

    // `[OBS]` The dead `cmp w8, #2` at `0x10C11C` has no branch consuming it,
    // so `base`, `dark` and `tinted` all take the copy. All three are checked
    // and no fourth arm is modelled.
    for (icf::Appearance a : {icf::Appearance::Base, icf::Appearance::Dark,
                              icf::Appearance::Tinted}) {
        const FillResolution got = layerFillFrom(fillOf("\"automatic\""), a, &lightSlot);
        CHECK_EQ(outcomeName(got.outcome), std::string("resolved"));
        CHECK(identical(got.fill, want.fill));
    }
}

// THE TERMINATION, and it is read rather than bounded. `[BIN]` The light-slot
// pass at `0x10BE20` maps `none` AND `automatic` to nil, so an `automatic`
// whose light slot is also `automatic` ends at nil instead of looping.
TEST_CASE(layer_automatic_inheriting_an_automatic_terminates_at_nil) {
    const icf::Fill automatic = fillOf("\"automatic\"");
    for (icf::Appearance a : {icf::Appearance::Base, icf::Appearance::Dark,
                              icf::Appearance::Tinted}) {
        const FillResolution r = layerFillFrom(automatic, a, &automatic);
        CHECK_EQ(outcomeName(r.outcome), std::string("no-fill"));
    }

    // And an `automatic` whose light slot says `none` ends at nil too -- the
    // same block, the other case.
    const icf::Fill none = fillOf("\"none\"");
    const FillResolution n = layerFillFrom(automatic, icf::Appearance::Dark, &none);
    CHECK_EQ(outcomeName(n.outcome), std::string("no-fill"));
}

// ROW 3 -- `solid`. `[BIN]` `0x10C168`.
TEST_CASE(layer_solid_is_the_primary_colour) {
    const icf::Fill f = fillOf(std::string("{\"solid\" : ") + kBlue + "}");
    for (icf::Appearance a : kAppearances) {
        const FillResolution r = layerFillFrom(f, a, nullptr);
        CHECK_EQ(contentsName(r.fill.contents), std::string("solid"));
        CHECK(near(r.fill.primary.components[2], 1.0));
        CHECK(!r.fill.placement.has_value());
    }
}

// ROW 4 -- `automatic-gradient`. `[BIN]` `0x10C088`, `placement: nil` -- ON THE
// LAYER TOO, where the `linear-gradient` beside it does read the orientation.
// That is the pair of rows that makes the discarding a fact about the CASE and
// not about the position.
TEST_CASE(layer_automatic_gradient_discards_the_orientation_the_document_names) {
    const icf::Fill f =
        fillOf(std::string("{\"automatic-gradient\" : ") + kRed +
               ", \"orientation\" : {\"start\" : {\"x\" : 0.0, \"y\" : 0.0},"
               " \"stop\" : {\"x\" : 1.0, \"y\" : 0.0}}}");
    CHECK(f.orientation.has_value());
    const FillResolution r = layerFillFrom(f, icf::Appearance::Dark, nullptr);
    CHECK_EQ(contentsName(r.fill.contents), std::string("automatic-gradient"));
    CHECK(!r.fill.placement.has_value());
}

// ROW 5 -- `linear-gradient`, THE ONE SITE OF FOUR that reads `orientation`.
// `[BIN]` `0x10C1A0`, constructor at `0x10C1DC`, taking `orientation.start` and
// `orientation.stop` in that order. The document's `stop` becomes the
// placement's `end`, and the axis is checked component by component so that a
// transposition cannot pass.
TEST_CASE(layer_linear_gradient_carries_the_orientation_into_the_placement) {
    const icf::Fill f = fillOf(std::string("{\"linear-gradient\" : [") + kRed + ", " + kBlue +
                               "], \"orientation\" : {\"start\" : {\"x\" : 0.25, \"y\" : 0.5},"
                               " \"stop\" : {\"x\" : 0.75, \"y\" : 1.0}}}");
    const FillResolution r = layerFillFrom(f, icf::Appearance::Dark, nullptr);
    CHECK_EQ(contentsName(r.fill.contents), std::string("gradient"));
    CHECK(near(r.fill.primary.components[0], 1.0));
    CHECK(near(r.fill.secondary.components[2], 1.0));
    REQUIRE(r.fill.placement.has_value());
    CHECK(near(r.fill.placement->start.x, 0.25));
    CHECK(near(r.fill.placement->start.y, 0.5));
    CHECK(near(r.fill.placement->end.x, 0.75));
    CHECK(near(r.fill.placement->end.y, 1.0));

    // The same fill, the other converter: same colours, NO placement. This is
    // the one comparison the correction of §5.2 rests on.
    const FillResolution b = backgroundFillFrom(f, icf::Appearance::Dark);
    CHECK(!b.fill.placement.has_value());
    CHECK(!identical(b.fill, r.fill));
}

// `[OBS]` Whether the target's `Fill.orientation` is optional or decoded with a
// default was not read. A document that names none gets no placement here, and
// `[ART]` that is 26 of the corpus's 48 layer `linear-gradient` fills -- the
// common case, which is why it is pinned rather than left to the sweep.
TEST_CASE(layer_linear_gradient_without_an_orientation_has_no_placement) {
    const icf::Fill f =
        fillOf(std::string("{\"linear-gradient\" : [") + kRed + ", " + kBlue + "]}");
    CHECK(!f.orientation.has_value());
    const FillResolution r = layerFillFrom(f, icf::Appearance::Dark, nullptr);
    CHECK_EQ(contentsName(r.fill.contents), std::string("gradient"));
    CHECK(!r.fill.placement.has_value());
}

// ROWS 6 and 7 -- `system-light` / `system-dark` on a LAYER. `[BIN]`
// `0x10C148` and `0x10C260`. `[ART]` No corpus layer names either, across 145
// documents; the cases are in the converter and are transcribed, and the
// corpus's 0/33 split says the vocabulary belongs to the chiclet.
TEST_CASE(layer_system_cases_exist_and_name_the_same_two_ramps) {
    for (icf::Appearance a : kAppearances) {
        const FillResolution l = layerFillFrom(fillOf("\"system-light\""), a, nullptr);
        CHECK_EQ(outcomeName(l.outcome), std::string("resolved"));
        CHECK_EQ(rampName(l.fill.ramp), std::string("light-chiclet"));

        const FillResolution d = layerFillFrom(fillOf("\"system-dark\""), a, nullptr);
        CHECK_EQ(rampName(d.fill.ramp), std::string("dark-chiclet"));
    }
}

// ---------------------------------------------------------------------------
// Both key shapes, and the appearance predicate
// ---------------------------------------------------------------------------

// `[ART]` 34 of the 145 documents specialize their background fill, and 35 of
// those 69 entries carry an `appearance`. A converter that reached for
// `find("fill")` would read the unpredicated entry -- or nothing -- for every
// one of them.
TEST_CASE(the_background_reads_the_bare_key_and_the_specializations_alike) {
    Doc bare(std::string("{\"fill\" : {\"solid\" : ") + kRed + "}}");
    REQUIRE(bare.ok());
    CHECK_EQ(contentsName(resolveBackgroundFill(bare.root(), {}).fill.contents),
             std::string("solid"));

    Doc specialized(
        "{\"fill\" : \"automatic\","
        " \"fill-specializations\" : ["
        "   {\"value\" : \"system-dark\", \"appearance\" : \"dark\"},"
        "   {\"value\" : \"none\", \"appearance\" : \"tinted\"}]}");
    REQUIRE(specialized.ok());

    // Under `dark` the specialization wins and names the dark ramp.
    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    const FillResolution d = resolveBackgroundFill(specialized.root(), dark);
    CHECK_EQ(rampName(d.fill.ramp), std::string("dark-chiclet"));

    // Under `light` no entry matches, so the bare `automatic` is read.
    icf::Context light;
    light.appearance = icf::Appearance::Light;
    const FillResolution l = resolveBackgroundFill(specialized.root(), light);
    CHECK_EQ(rampName(l.fill.ramp), std::string("light-chiclet"));

    // Under `tinted` the specialization says `none` -- which on the BACKGROUND
    // is the automatic path, and under `tinted` that is clear. Two findings in
    // one resolution.
    icf::Context tinted;
    tinted.appearance = icf::Appearance::Tinted;
    const FillResolution t = resolveBackgroundFill(specialized.root(), tinted);
    CHECK_EQ(contentsName(t.fill.contents), std::string("solid"));
    CHECK(near(t.fill.primary.components[3], 0.0));
}

// A document with no `fill` key is not a refusal and not a colour. `[ART]` No
// corpus document is in this state -- all 145 name a background fill -- so the
// answer is `NoFill` and stays visible as such rather than being given a
// default the target was never read to have.
TEST_CASE(a_document_that_names_no_background_fill_is_not_given_one) {
    Doc empty("{\"groups\" : []}");
    REQUIRE(empty.ok());
    CHECK_EQ(outcomeName(resolveBackgroundFill(empty.root(), {}).outcome), std::string("no-fill"));
}

// THE TEST THAT EXERCISES THE INHERITANCE THE WAY A DOCUMENT DOES: the layer's
// `automatic` is not handed a light slot by a caller, it goes and resolves one
// -- through the specialization list, at the same slot with the appearance byte
// replaced.
TEST_CASE(a_layer_automatic_resolves_its_own_light_slot_through_the_specializations) {
    Doc layer(std::string("{\"name\" : \"art\", \"fill-specializations\" : ["
                          "  {\"value\" : \"automatic\", \"appearance\" : \"dark\"},"
                          "  {\"value\" : {\"solid\" : ") +
              kBlue + "}, \"appearance\" : \"light\"}]}");
    REQUIRE(layer.ok());

    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    const FillResolution d = resolveLayerFill(layer.layer(), dark);
    CHECK_EQ(outcomeName(d.outcome), std::string("resolved"));
    CHECK_EQ(contentsName(d.fill.contents), std::string("solid"));
    CHECK(near(d.fill.primary.components[2], 1.0));

    // The slot it inherited from, read directly, answers the same thing.
    icf::Context light;
    light.appearance = icf::Appearance::Light;
    const FillResolution l = resolveLayerFill(layer.layer(), light);
    CHECK(identical(d.fill, l.fill));

    // And the IDIOM crosses the forced appearance unchanged -- `bfxil` replaces
    // one byte of the slot and keeps the other.
    icf::Context darkOnMac;
    darkOnMac.appearance = icf::Appearance::Dark;
    darkOnMac.idiom = icf::Idiom::MacOS;
    CHECK(identical(resolveLayerFill(layer.layer(), darkOnMac).fill, l.fill));
}

// The chain, at the document level: `automatic` under dark inheriting an
// `automatic` at the light slot. Nil, not a loop and not a ramp.
TEST_CASE(a_layer_automatic_inheriting_an_automatic_is_nil_at_the_document_level) {
    Doc layer(
        "{\"name\" : \"art\", \"fill\" : \"automatic\","
        " \"fill-specializations\" : ["
        "   {\"value\" : \"automatic\", \"appearance\" : \"light\"}]}");
    REQUIRE(layer.ok());
    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    CHECK_EQ(outcomeName(resolveLayerFill(layer.layer(), dark).outcome), std::string("no-fill"));
}

// A layer whose light slot names nothing at all. `[INF]` Nothing to copy, and
// every path that reaches the converter with no fill answers nil.
TEST_CASE(a_layer_automatic_with_no_light_slot_is_nil) {
    Doc layer(
        "{\"name\" : \"art\","
        " \"fill-specializations\" : ["
        "   {\"value\" : \"automatic\", \"appearance\" : \"dark\"}]}");
    REQUIRE(layer.ok());
    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    CHECK_EQ(outcomeName(resolveLayerFill(layer.layer(), dark).outcome), std::string("no-fill"));
}

// `[ART]` 159 corpus layers carry no `fill` key and 49 say `none`. Both answer
// nil here, and the corpus keeping both spellings alive is the evidence that
// neither replaces the art.
TEST_CASE(a_layer_that_names_no_fill_is_nil_and_not_a_refusal) {
    Doc layer("{\"name\" : \"art\", \"image-name\" : \"art.svg\"}");
    REQUIRE(layer.ok());
    CHECK_EQ(outcomeName(resolveLayerFill(layer.layer(), {}).outcome), std::string("no-fill"));
}

// ---------------------------------------------------------------------------
// Refusals -- a key that is present and unreadable is an error, not a default
// ---------------------------------------------------------------------------

TEST_CASE(an_unreadable_fill_is_refused_by_both_converters) {
    Doc background("{\"fill\" : \"wibble\"}");
    REQUIRE(background.ok());
    CHECK_EQ(outcomeName(resolveBackgroundFill(background.root(), {}).outcome),
             std::string("refused"));

    Doc layer("{\"name\" : \"art\", \"fill\" : {\"solid\" : \"not-a-colour\"}}");
    REQUIRE(layer.ok());
    const FillResolution r = resolveLayerFill(layer.layer(), {});
    CHECK_EQ(outcomeName(r.outcome), std::string("refused"));
    CHECK(!r.why.empty());
}

// `[BIN]` The converters read two named colour slots, `Fill+0x08` and
// `Fill+0x30`. `[ART]` All 97 corpus ramps carry exactly two colours. `[OBS]`
// How a ramp of another length decodes into those two was not read, so it is
// refused -- taking the first two would be an invented decoding.
TEST_CASE(a_ramp_that_is_not_two_colours_is_refused_and_not_truncated) {
    const icf::Fill three = fillOf(std::string("{\"linear-gradient\" : [") + kRed + ", " + kBlue +
                                   ", " + kRed + "]}");
    CHECK_EQ(outcomeName(backgroundFillFrom(three, icf::Appearance::Light).outcome),
             std::string("refused"));
    CHECK_EQ(outcomeName(layerFillFrom(three, icf::Appearance::Light, nullptr).outcome),
             std::string("refused"));

    const icf::Fill one = fillOf(std::string("{\"linear-gradient\" : [") + kRed + "]}");
    CHECK_EQ(outcomeName(backgroundFillFrom(one, icf::Appearance::Light).outcome),
             std::string("refused"));
}

// A malformed value at the LIGHT slot refuses only when the light slot is
// actually read. A value nobody consumes must not be able to fail a read that
// succeeded.
TEST_CASE(an_unreadable_light_slot_refuses_only_the_case_that_reads_it) {
    Doc inherits(
        "{\"name\" : \"art\", \"fill\" : \"automatic\","
        " \"fill-specializations\" : ["
        "   {\"value\" : \"wibble\", \"appearance\" : \"light\"}]}");
    REQUIRE(inherits.ok());
    icf::Context dark;
    dark.appearance = icf::Appearance::Dark;
    CHECK_EQ(outcomeName(resolveLayerFill(inherits.layer(), dark).outcome), std::string("refused"));

    Doc ignores(std::string("{\"name\" : \"art\", \"fill\" : {\"solid\" : ") + kRed + "},"
                " \"fill-specializations\" : ["
                "   {\"value\" : \"wibble\", \"appearance\" : \"light\"}]}");
    REQUIRE(ignores.ok());
    CHECK_EQ(outcomeName(resolveLayerFill(ignores.layer(), dark).outcome), std::string("resolved"));
}

// ---------------------------------------------------------------------------
// THE CORPUS
// ---------------------------------------------------------------------------

// The two clean 0/N divisions the reading PREDICTED before they were counted,
// pinned as exact numbers. These are raw key occurrences -- every `fill` value
// and every `fill-specializations` entry -- so they are a property of the
// documents and not of any context.
//
// If a future corpus adds a `none` background or a `system-light` layer, this
// test fails, and it SHOULD: those are the two counts the whole two-converter
// reading rests on.
TEST_CASE(corpus_confirms_the_two_zero_over_n_divisions_the_reading_predicted) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());  // IC_CORPUS_DIR unset or empty

    std::map<std::string, int> background, layers;
    int documents = 0, withBackgroundFill = 0;

    auto count = [](std::map<std::string, int>& into, const icf::json::Value& v) {
        auto f = icf::fillFrom(v);
        if (!f) {
            ++into["UNREADABLE"];
            return;
        }
        switch (f->kind) {
            case icf::FillKind::None: ++into["none"]; break;
            case icf::FillKind::Automatic: ++into["automatic"]; break;
            case icf::FillKind::Solid: ++into["solid"]; break;
            case icf::FillKind::AutomaticGradient: ++into["automatic-gradient"]; break;
            case icf::FillKind::LinearGradient: ++into["linear-gradient"]; break;
            case icf::FillKind::SystemLight: ++into["system-light"]; break;
            case icf::FillKind::SystemDark: ++into["system-dark"]; break;
        }
    };
    auto countAll = [&](std::map<std::string, int>& into, const icf::json::Value& owner) {
        bool any = false;
        if (const icf::json::Value* bare = owner.find("fill")) {
            count(into, *bare);
            any = true;
        }
        if (const icf::json::Value* list = owner.find("fill-specializations")) {
            if (list->kind() == icf::json::Value::Kind::Array) {
                for (const auto& e : list->elements()) {
                    if (const icf::json::Value* value = e.find("value")) {
                        count(into, *value);
                        any = true;
                    }
                }
            }
        }
        return any;
    };

    for (const auto& p : docs) {
        auto tree = icf::json::parse(readAll(p));
        if (!tree) continue;
        auto doc = icf::IconDocument::open(*tree);
        if (!doc) continue;
        ++documents;
        if (countAll(background, *tree)) ++withBackgroundFill;
        for (const auto& g : doc->groups()) {
            for (const auto& l : g.layers()) countAll(layers, l.json());
        }
    }

    CHECK_EQ(documents, 145);
    // Every document names a background fill, which is why `NoFill` never
    // occurs on the background in the sweep below.
    CHECK_EQ(withBackgroundFill, 145);

    // PREDICTION 1: `none` on the background is indistinguishable from
    // `automatic`, therefore redundant, therefore never written.
    CHECK_EQ(background["none"], 0);
    CHECK_EQ(layers["none"], 49);

    // PREDICTION 2: `system-light`/`system-dark` are chiclet vocabulary,
    // therefore background-only.
    CHECK_EQ(background["system-light"], 14);
    CHECK_EQ(background["system-dark"], 19);
    CHECK_EQ(layers["system-light"], 0);
    CHECK_EQ(layers["system-dark"], 0);

    // And the case this whole spec exists for.
    CHECK_EQ(background["automatic"], 28);
    CHECK_EQ(layers["automatic"], 55);

    CHECK_EQ(background["UNREADABLE"], 0);
    CHECK_EQ(layers["UNREADABLE"], 0);

    std::printf("  fill keys      background:");
    for (const auto& [name, n] : background) std::printf(" %s=%d", name.c_str(), n);
    std::printf("\n                 layer:");
    for (const auto& [name, n] : layers) std::printf(" %s=%d", name.c_str(), n);
    std::printf("\n");
}

// THE SWEEP. Every document's background fill and every layer's fill, in all
// four appearances, through the converters that will feed the renderer. Zero
// refusals, and the distribution reported so that a change in it is visible in
// the log rather than only in a pass or a fail.
TEST_CASE(corpus_resolves_every_fill_in_every_appearance_without_a_refusal) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());  // IC_CORPUS_DIR unset or empty

    int documents = 0, backgroundResolutions = 0, layerResolutions = 0;
    std::vector<std::string> refusals;
    std::map<std::string, int> backgroundContents, layerContents;
    int layerNil = 0, backgroundNoFill = 0;
    int automaticLayers = 0, automaticLayersInheriting = 0, automaticLayersNil = 0;
    int automaticBackgrounds = 0, automaticBackgroundsClear = 0;
    int layerPlacements = 0, backgroundPlacements = 0;
    int backgroundOrientationsDiscarded = 0;

    // The document's own word at this resolution, so the sweep can check the
    // ARM that was taken and not only that some arm was.
    auto documentKind = [](const icf::json::Value* node) -> std::optional<icf::FillKind> {
        if (!node) return std::nullopt;
        auto f = icf::fillFrom(*node);
        if (!f) return std::nullopt;
        return f->kind;
    };

    for (const auto& p : docs) {
        const std::string name = p.parent_path().filename().string();
        auto tree = icf::json::parse(readAll(p));
        if (!tree) continue;
        auto doc = icf::IconDocument::open(*tree);
        if (!doc) continue;
        ++documents;

        for (icf::Appearance a : kAppearances) {
            icf::Context ctx;
            ctx.appearance = a;

            const icf::json::Value* bgNode = icf::resolve(*tree, "fill", ctx);
            const auto bgKind = documentKind(bgNode);
            const FillResolution bg = resolveBackgroundFill(*tree, ctx);
            ++backgroundResolutions;
            if (bg.outcome == FillOutcome::Refused) {
                refusals.push_back(name + " background/" + appearanceName(a) + ": " + bg.why);
            } else if (bg.outcome == FillOutcome::NoFill) {
                ++backgroundNoFill;
            } else {
                ++backgroundContents[contentsName(bg.fill.contents) +
                                     (bg.fill.contents == ResolvedFill::Contents::System
                                          ? "/" + rampName(bg.fill.ramp)
                                          : "")];
                if (bg.fill.placement) ++backgroundPlacements;

                // THE THREE ARMS, ON REAL DOCUMENTS. Counting "a transparent
                // solid appeared somewhere" would also be satisfied by a
                // document that simply names a clear colour; this asserts that
                // the arm each `automatic` background takes is the arm the
                // switch at `0x10AEF4` says it takes.
                if (bgKind == icf::FillKind::Automatic || bgKind == icf::FillKind::None) {
                    ++automaticBackgrounds;
                    if (a == icf::Appearance::Tinted) {
                        ++automaticBackgroundsClear;
                        CHECK(bg.fill.contents == ResolvedFill::Contents::Solid);
                        CHECK(bg.fill.primary.components[3] == 0.0);
                    } else {
                        CHECK(bg.fill.contents == ResolvedFill::Contents::System);
                        CHECK(bg.fill.ramp == (a == icf::Appearance::Dark ? SystemFill::Dark
                                                                          : SystemFill::Light));
                    }
                }

                // The correction of §5.2: a background fill whose document
                // named an axis the converter threw away.
                if (bgNode) {
                    auto parsed = icf::fillFrom(*bgNode);
                    if (parsed && parsed->orientation) ++backgroundOrientationsDiscarded;
                }
            }

            icf::Context lightCtx = ctx;
            lightCtx.appearance = icf::Appearance::Light;

            for (const auto& g : doc->groups()) {
                for (const auto& l : g.layers()) {
                    const auto layerKind = documentKind(l.resolve("fill", ctx));
                    const FillResolution lf = resolveLayerFill(l, ctx);
                    ++layerResolutions;
                    if (lf.outcome == FillOutcome::Refused) {
                        refusals.push_back(name + " layer '" + std::string(l.name()) + "'/" +
                                           appearanceName(a) + ": " + lf.why);
                        continue;
                    }
                    if (lf.outcome == FillOutcome::NoFill) {
                        ++layerNil;
                    } else {
                        ++layerContents[contentsName(lf.fill.contents)];
                        if (lf.fill.placement) ++layerPlacements;
                    }

                    // THE INHERITANCE, ON REAL DOCUMENTS. Every `automatic`
                    // layer must answer exactly what this same layer answers at
                    // the `light` slot -- and under `light` it must answer nil.
                    // A converter that re-derived a colour instead of copying
                    // would pass a count and fail this.
                    if (layerKind == icf::FillKind::Automatic) {
                        ++automaticLayers;
                        const FillResolution atLight = resolveLayerFill(l, lightCtx);
                        if (a == icf::Appearance::Light) {
                            CHECK(lf.outcome == FillOutcome::NoFill);
                        } else {
                            CHECK(lf.outcome == atLight.outcome);
                            if (lf.outcome == FillOutcome::Resolved) {
                                CHECK(identical(lf.fill, atLight.fill));
                            }
                        }
                        if (lf.outcome == FillOutcome::NoFill) ++automaticLayersNil;
                        else ++automaticLayersInheriting;
                    }
                }
            }
        }
    }

    CHECK_EQ(documents, 145);
    CHECK_EQ(static_cast<int>(refusals.size()), 0);
    for (size_t i = 0; i < refusals.size() && i < 10; ++i) {
        std::printf("  REFUSED %s\n", refusals[i].c_str());
    }

    // Every document names a background fill and no converter path answers nil
    // on the background, so this is the reading's claim and not an accident of
    // the sweep.
    CHECK_EQ(backgroundNoFill, 0);

    // `[ART]` 81 of the 580 background resolutions take the `automatic`/`none`
    // block, and each was checked above against the arm the switch says it
    // takes. 20 of them fall under `tinted` and every one is transparent, so
    // deleting the clear arm fails 20 assertions rather than none.
    //
    // 81 is not a multiple of 4, and that is the interesting part: 18 documents
    // name an `automatic` background under all four appearances and 5
    // specialize it away under some -- three of them name `automatic` ONLY
    // under `dark`. So the background's appearance switch is exercised by
    // documents that disagree about which appearance reaches it, not by 20
    // copies of one shape.
    CHECK_EQ(automaticBackgrounds, 81);
    CHECK_EQ(automaticBackgroundsClear, 20);

    // `[ART]` And the layer's `automatic` fires 156 times, of which 152
    // TERMINATE AT NIL and only 4 inherit a colour. That ratio is the reading's
    // own claim in numbers: `automatic` on a layer is an inheritance operator
    // that usually inherits nothing, not a colour by another name. A
    // transcription that answered a chiclet ramp here would paint 156.
    CHECK_EQ(automaticLayers, 156);
    CHECK_EQ(automaticLayersNil, 152);
    CHECK_EQ(automaticLayersInheriting, 4);

    // §5.2 over the corpus: no background resolution carries a placement,
    // however many orientations the documents named.
    CHECK_EQ(backgroundPlacements, 0);
    CHECK(backgroundOrientationsDiscarded > 0);
    CHECK(layerPlacements > 0);

    std::printf("  fill sweep     %d documents, %d background + %d layer resolutions,"
                " %d refusals\n",
                documents, backgroundResolutions, layerResolutions,
                static_cast<int>(refusals.size()));
    std::printf("  background     ");
    for (const auto& [what, n] : backgroundContents) std::printf("%s=%d ", what.c_str(), n);
    std::printf("(no-fill %d, automatic %d of which clear %d, orientations discarded %d)\n",
                backgroundNoFill, automaticBackgrounds, automaticBackgroundsClear,
                backgroundOrientationsDiscarded);
    std::printf("  layer          ");
    for (const auto& [what, n] : layerContents) std::printf("%s=%d ", what.c_str(), n);
    std::printf("(nil %d, automatic %d -> nil %d / inherited %d, placements %d)\n", layerNil,
                automaticLayers, automaticLayersNil, automaticLayersInheriting, layerPlacements);
}
