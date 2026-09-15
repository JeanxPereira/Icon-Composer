// THE LAYER PANEL'S GESTURES, WITHOUT A WINDOW.
//
// The headless selftest counts `imgui errors` and `textured yes`. It cannot see
// a drag, it cannot see whether the row a person dropped is the row that ends up
// selected, and treating its green as evidence of a usable panel is the mistake
// that produced the panel this file is about.
//
// What CAN be proved is the arithmetic behind the gesture, so the arithmetic is
// where it lives: `planDrop` turns "dropped in the slot between rows 1 and 2"
// into a run of `Session::moveNode` calls, and `visibleRows` is the order the
// arrow keys walk. The cases below assert three things about that run:
//
//   1. it is MINIMAL -- a drop three rows down is three swaps, never four;
//   2. the node LANDS where the slot said, driven through a real Session;
//   3. the whole run is ONE entry on the undo stack, so one Ctrl+Z is one drag.
//
// Like test_kit_idiom.cpp this links `IconComposer::Kit`, so it sits behind
// `if(TARGET IconComposerKit)` in Tests/CMakeLists.txt.
#include "check.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Two groups: the first with four layers named A..D, the second with one.
const char* kTwoGroups = R"({
  "fill" : "automatic",
  "groups" : [
    { "name" : "G0", "layers" : [
        { "name" : "A", "image-name" : "a.svg" },
        { "name" : "B", "image-name" : "b.svg" },
        { "name" : "C", "image-name" : "c.svg" },
        { "name" : "D", "image-name" : "d.svg" } ] },
    { "name" : "G1", "layers" : [
        { "name" : "E", "image-name" : "e.svg" } ] }
  ] })";

fs::path makeBundle(const std::string& name, const std::string& document) {
    const fs::path dir = fs::temp_directory_path() / ("ic-layers-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << document;
    for (const char* f : {"a.svg", "b.svg", "c.svg", "d.svg", "e.svg"})
        std::ofstream(dir / "Assets" / f, std::ios::binary) << "<svg/>";
    return dir;
}

std::string layerName(const ick::Session& s, std::size_t g, std::size_t l) {
    const icf::json::Value* n = icf::nodeAt(s.root(), icf::NodePath{g, l});
    const icf::json::Value* name = n ? n->find("name") : nullptr;
    return name && name->kind() == icf::json::Value::Kind::String ? std::string(name->rawString()) : "?";
}

std::string groupName(const ick::Session& s, std::size_t g) {
    const icf::json::Value* n = icf::nodeAt(s.root(), icf::NodePath{g, std::nullopt});
    const icf::json::Value* name = n ? n->find("name") : nullptr;
    return name && name->kind() == icf::json::Value::Kind::String ? std::string(name->rawString()) : "?";
}

// The order of a group's layers, top to bottom, as one word.
std::string order(const ick::Session& s, std::size_t g) {
    const icf::json::Value* group = icf::nodeAt(s.root(), icf::NodePath{g, std::nullopt});
    const icf::json::Value* layers = group ? group->find("layers") : nullptr;
    const std::size_t n =
        layers && layers->kind() == icf::json::Value::Kind::Array ? layers->elements().size() : 0;
    std::string out;
    for (std::size_t i = 0; i < n; ++i) out += layerName(s, g, i);
    return out;
}

// Exactly what the panel does with a plan: the run of swaps, coalesced into one
// command, with the dragged row carried along. Mirrors PanelLayers' `runMoves`.
icf::NodePath runPlan(ick::Session& s, icf::NodePath path, const ick::DropPlan& plan) {
    for (std::size_t i = 0; i < plan.steps; ++i) {
        if (!s.moveNode(path, plan.delta, /*coalesce=*/true)) break;
        std::size_t& index = path.layer ? *path.layer : *path.group;
        index = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(index) + plan.delta);
    }
    s.endCoalescing();
    return path;
}

}  // namespace

// ---- planDrop: the slot, and the minimum number of swaps it costs -----------

// `moveNode` swaps neighbours, so the cost of a drop is the DISTANCE. The number
// this asserts is the whole point of dragging: five rows used to be five trips
// through a context menu, and it has to be five swaps and not one more.
TEST_CASE(layers_drop_costs_exactly_the_distance) {
    // Four siblings, 0..3. Dropping 0 into the last slot is three swaps down.
    const ick::DropPlan down = ick::planDrop(/*from=*/0, /*gap=*/4, /*count=*/4);
    CHECK(down.valid);
    CHECK_EQ(down.delta, +1);
    CHECK_EQ(down.steps, std::size_t{3});
    CHECK_EQ(down.to, std::size_t{3});

    // And back up: the last into the first slot.
    const ick::DropPlan up = ick::planDrop(3, 0, 4);
    CHECK(up.valid);
    CHECK_EQ(up.delta, -1);
    CHECK_EQ(up.steps, std::size_t{3});
    CHECK_EQ(up.to, std::size_t{0});

    // The slot BELOW a row that is above you is that row's index, not one past
    // it: dropping 3 just under 0 lands at 1, one place below where 0 sits.
    const ick::DropPlan under = ick::planDrop(3, 1, 4);
    CHECK(under.valid);
    CHECK_EQ(under.to, std::size_t{1});
    CHECK_EQ(under.steps, std::size_t{2});

    // Dropping 0 just under 2 lands at 2, because removing 0 first shifts the
    // slot down. This is the asymmetry a naive `to = gap` gets wrong by one.
    const ick::DropPlan over = ick::planDrop(0, 3, 4);
    CHECK(over.valid);
    CHECK_EQ(over.to, std::size_t{2});
    CHECK_EQ(over.steps, std::size_t{2});
}

// The two slots that touch a row are the place it already is. A plan for them
// would be an empty command on the undo stack -- a Ctrl+Z that appears to do
// nothing, which is worse than no undo at all.
TEST_CASE(layers_drop_onto_itself_is_not_a_move) {
    for (std::size_t from = 0; from < 4; ++from) {
        CHECK(!ick::planDrop(from, from, 4).valid);
        CHECK(!ick::planDrop(from, from + 1, 4).valid);
    }
    // Out of range on either side is refused, not clamped: a clamp would move a
    // row somewhere nobody pointed at.
    CHECK(!ick::planDrop(4, 0, 4).valid);
    CHECK(!ick::planDrop(0, 5, 4).valid);
    CHECK(!ick::planDrop(0, 0, 0).valid);
}

// ---- the plan driven through a real Session --------------------------------

// The end of the gesture, on the real model: the layers come out in the order
// the slot named, the dragged layer is the one selected, and the whole drag is
// ONE undo.
TEST_CASE(layers_drop_lands_where_the_slot_said_and_is_one_undo) {
    auto s = ick::Session::open(makeBundle("drop", kTwoGroups));
    REQUIRE(s.has_value());
    CHECK_EQ(order(*s, 0), std::string("ABCD"));

    // Drag A (index 0) and drop it between C and D -- slot 3.
    s->selection = icf::NodePath{std::size_t{0}, std::size_t{0}};
    const std::uint64_t before = s->version();
    const ick::DropPlan plan = ick::planDrop(0, 3, 4);
    REQUIRE(plan.valid);
    const icf::NodePath landed = runPlan(*s, icf::NodePath{std::size_t{0}, std::size_t{0}}, plan);

    CHECK_EQ(order(*s, 0), std::string("BCAD"));
    CHECK_EQ(landed.layer.value_or(99), std::size_t{2});
    CHECK_EQ(layerName(*s, 0, *landed.layer), std::string("A"));
    // The selection is reindexed by the Session at every swap, so it followed the
    // node it named rather than staying on a position that now holds another layer.
    REQUIRE(s->selection.has_value());
    CHECK(*s->selection == landed);
    CHECK(s->version() > before);

    // ONE Ctrl+Z, not two. This is what `coalesce` buys, and without it a drag of
    // three rows needs three presses to take back.
    REQUIRE(s->undo());
    CHECK_EQ(order(*s, 0), std::string("ABCD"));
    CHECK(!s->canUndo());
}

// Two drags in a row are two undos: coalescing must not swallow a second gesture
// just because it happens to move layers under the same group.
TEST_CASE(layers_two_drags_are_two_undos) {
    auto s = ick::Session::open(makeBundle("twodrags", kTwoGroups));
    REQUIRE(s.has_value());

    runPlan(*s, icf::NodePath{std::size_t{0}, std::size_t{0}}, ick::planDrop(0, 3, 4));
    CHECK_EQ(order(*s, 0), std::string("BCAD"));
    runPlan(*s, icf::NodePath{std::size_t{0}, std::size_t{3}}, ick::planDrop(3, 0, 4));
    CHECK_EQ(order(*s, 0), std::string("DBCA"));

    REQUIRE(s->undo());
    CHECK_EQ(order(*s, 0), std::string("BCAD"));
    REQUIRE(s->undo());
    CHECK_EQ(order(*s, 0), std::string("ABCD"));
}

// Groups reorder by the same arithmetic, and the disclosure state the panel
// keeps beside them is positional -- so the test that matters here is that the
// GROUP the plan names is the group that moved.
TEST_CASE(layers_groups_reorder_by_the_same_plan) {
    auto s = ick::Session::open(makeBundle("groups", kTwoGroups));
    REQUIRE(s.has_value());
    CHECK_EQ(groupName(*s, 0), std::string("G0"));

    const ick::DropPlan plan = ick::planDrop(/*from=*/0, /*gap=*/2, /*count=*/2);
    REQUIRE(plan.valid);
    CHECK_EQ(plan.steps, std::size_t{1});
    const icf::NodePath landed = runPlan(*s, icf::NodePath{std::size_t{0}, std::nullopt}, plan);
    CHECK_EQ(landed.group.value_or(99), std::size_t{1});
    CHECK_EQ(groupName(*s, 0), std::string("G1"));
    CHECK_EQ(groupName(*s, 1), std::string("G0"));
}

// "Send to Back" is the same run with the distance the panel computes, and it is
// still one undo -- the reason the menu item exists at all is that the drag it
// replaces is the long one.
TEST_CASE(layers_send_to_back_is_one_undo) {
    auto s = ick::Session::open(makeBundle("toback", kTwoGroups));
    REQUIRE(s.has_value());
    icf::NodePath p{std::size_t{0}, std::size_t{0}};
    const std::size_t steps = 4 - 1 - 0;
    for (std::size_t i = 0; i < steps; ++i) {
        REQUIRE(s->moveNode(p, +1, true));
        *p.layer += 1;
    }
    s->endCoalescing();
    CHECK_EQ(order(*s, 0), std::string("BCDA"));
    REQUIRE(s->undo());
    CHECK_EQ(order(*s, 0), std::string("ABCD"));
    CHECK(!s->canUndo());
}

// ---- visibleRows: the order the arrow keys walk -----------------------------

// Up and Down move by one row of WHAT IS ON SCREEN, so a closed group has to
// hide its layers from the walk as well as from the eye -- otherwise Down from a
// closed group jumps into a layer nobody can see.
TEST_CASE(layers_visible_rows_skip_a_closed_group) {
    auto s = ick::Session::open(makeBundle("rows", kTwoGroups));
    REQUIRE(s.has_value());

    // Everything open: G0, A, B, C, D, G1, E.
    const auto all = ick::visibleRows(s->root(), {});
    REQUIRE(all.size() == 7);
    CHECK(all[0] == (icf::NodePath{std::size_t{0}, std::nullopt}));
    CHECK(all[1] == (icf::NodePath{std::size_t{0}, std::size_t{0}}));
    CHECK(all[5] == (icf::NodePath{std::size_t{1}, std::nullopt}));
    CHECK(all[6] == (icf::NodePath{std::size_t{1}, std::size_t{0}}));

    // G0 closed: G0, G1, E. Down from G0 is G1, not A.
    const auto closed = ick::visibleRows(s->root(), {0, 1});
    REQUIRE(closed.size() == 3);
    CHECK(closed[0] == (icf::NodePath{std::size_t{0}, std::nullopt}));
    CHECK(closed[1] == (icf::NodePath{std::size_t{1}, std::nullopt}));
    CHECK(closed[2] == (icf::NodePath{std::size_t{1}, std::size_t{0}}));

    // A group past the end of the vector counts as open -- how a group that was
    // just added arrives, and the state the panel starts in.
    // G0 closed and G1 not mentioned at all: G0, G1, E again -- G1 opened
    // because nothing said it was shut.
    const auto partial = ick::visibleRows(s->root(), {0});
    REQUIRE(partial.size() == 3);
    CHECK(partial[1] == (icf::NodePath{std::size_t{1}, std::nullopt}));
    CHECK(partial[2] == (icf::NodePath{std::size_t{1}, std::size_t{0}}));
}

// A document with no groups walks to nothing rather than to a phantom row.
TEST_CASE(layers_visible_rows_of_an_empty_document) {
    auto s = ick::Session::open(makeBundle("empty", R"({"fill":"automatic","groups":[]})"));
    REQUIRE(s.has_value());
    CHECK(ick::visibleRows(s->root(), {}).empty());
}
