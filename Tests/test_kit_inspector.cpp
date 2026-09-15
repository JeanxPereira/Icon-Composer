// WHAT AN INSPECTOR CONTROL DOES TO THE UNDO STACK, AND TO THE BYTES.
//
// The user's report was "mexer nos controles [...] péssimo", and the headless
// selftest cannot see any of it: it counts `imgui errors 0` and `textured yes`
// and would say exactly that about a panel whose every slider was broken. So
// this file measures the two things about a control that are NOT a matter of
// taste, and that a person would otherwise have to discover by losing work:
//
//   1. A DRAG IS ONE UNDO. `Session::setProperty` takes a `coalesce` flag and
//      folds consecutive edits to the same (path, property, scope) into one
//      command (Session.h). A slider that forgets to pass it turns one gesture
//      into a couple of hundred commands and makes Ctrl+Z useless -- you press
//      it and the value moves by one pixel's worth. The first four cases below
//      pin the fold, its boundaries, and -- in the second case -- what the
//      absence of the flag actually costs, because a test that only asserts "1"
//      never shows that the other number was 214.
//
//   2. A NUMBER NOBODY TOUCHED KEEPS ITS LEXEME. `[ART]` the corpus spells
//      coordinates like `0.5000000000000001`, and `json::Value` carries the
//      source text of a number precisely so a round trip can be byte-exact
//      (Json.h). The Geometry section used to read the whole `position` through
//      `float` and write it back whole, so dragging the SCALE re-printed both
//      translation coordinates at float precision. The last two cases measure
//      that loss on the `icf::` primitives the section is built from: the float
//      path destroys the document's bytes and the double path does not, which
//      is the whole reason the section was moved to doubles and to per-member
//      writes.
//
// Neither case draws a frame. What a control LOOKS like, whether its step feels
// right under the hand, and whether the typed-entry hint is discoverable are
// human judgements and are named as such in the laudo; these are the parts that
// are arithmetic.
#include "check.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/IconComposerKit/Session.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// One group, one layer, an opacity and a position with a coordinate the corpus
// really writes. `0.5000000000000001` is the shortest text that round-trips that
// double; a float cannot hold it and cannot print it back.
const char* kDocument = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : "automatic",
  "groups" : [ { "name" : "G", "layers" : [ { "name" : "art", "image-name" : "art.svg",
      "opacity" : 1,
      "position" : { "scale" : 0.8, "translation-in-points" : [ 0, 0.5000000000000001 ] } } ] } ] })";

fs::path makeBundle(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / ("ic-inspector-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << kDocument;
    std::ofstream(dir / "Assets" / "art.svg", std::ios::binary) << "<svg/>";
    return dir;
}

const icf::NodePath kLayer{std::size_t{0}, std::size_t{0}};

// How many commands the stack holds, counted the only way the Session exposes:
// by emptying it. Every case that calls this is finished with the session.
std::size_t undoDepth(ick::Session& s) {
    std::size_t n = 0;
    while (s.undo()) ++n;
    return n;
}

// One frame of a slider: what `Section::write` does, with the coalesce flag the
// control passed.
void slide(ick::Session& s, double value, bool coalesce, icf::Context scope = icf::Context{}) {
    s.setProperty(kLayer, "opacity", scope, icf::json::Value::number(value), coalesce);
}

// 214 frames is a real drag: about two seconds of holding the mouse down at 100
// frames a second, or one slow sweep across a 200 px slider.
constexpr int kDragFrames = 214;

// The value this frame of the drag reports. It starts one step BELOW the
// document's opening 1.0 on purpose: `Session::apply` records no command when an
// edit changes no byte, so a first frame that wrote the value already there
// would be a frame the stack never saw -- and the counts below would be short by
// exactly one for a reason that has nothing to do with coalescing.
double dragValue(int frame) { return 1.0 - (frame + 1) * 0.004; }

double opacityUnderBase(const ick::Session& s) {
    const icf::json::Value* node = icf::nodeAt(s.root(), kLayer);
    if (!node) return -1;
    const icf::json::Value* v = icf::resolve(*node, "opacity", icf::Context{});
    if (!v || v->kind() != icf::json::Value::Kind::Number) return -1;
    return std::strtod(v->number().c_str(), nullptr);
}

}  // namespace

// THE FOLD. One gesture, one command, and one Ctrl+Z that takes the whole
// gesture back -- not the last pixel of it.
TEST_CASE(kit_a_coalesced_drag_is_one_undo) {
    auto s = ick::Session::open(makeBundle("coalesce"));
    REQUIRE(s.has_value());

    for (int i = 0; i < kDragFrames; ++i) slide(*s, dragValue(i), true);
    s->endCoalescing();   // what IsItemDeactivatedAfterEdit calls on release

    // The document really did move, so the fold is not the edits being dropped.
    CHECK(opacityUnderBase(*s) < 0.2);
    CHECK(s->canUndo());
    CHECK(s->undo());
    // ONE undo, and the value is back where the drag started.
    CHECK_EQ(opacityUnderBase(*s), 1.0);
    CHECK(!s->canUndo());
}

// WHAT THE FLAG IS WORTH. The same gesture with the flag forgotten -- which is
// what a control that omits it does -- and the number is the one that makes the
// case above mean something.
TEST_CASE(kit_a_drag_without_the_flag_is_one_command_per_frame) {
    auto s = ick::Session::open(makeBundle("uncoalesced"));
    REQUIRE(s.has_value());

    for (int i = 0; i < kDragFrames; ++i) slide(*s, dragValue(i), false);
    CHECK_EQ(undoDepth(*s), static_cast<std::size_t>(kDragFrames));
    // And after all of them, the document is back at the opening value: nothing
    // was lost, it just cost 214 presses of Ctrl+Z to get there.
    CHECK_EQ(opacityUnderBase(*s), 1.0);
}

// THE BOUNDARY. `endCoalescing` is the drag ENDING, and two gestures must be two
// commands -- otherwise a person who drags, lets go, and drags again cannot
// undo only the second one.
TEST_CASE(kit_releasing_the_drag_closes_the_command) {
    auto s = ick::Session::open(makeBundle("release"));
    REQUIRE(s.has_value());

    for (int i = 0; i < 100; ++i) slide(*s, 1.0 - (i + 1) * 0.005, true);
    s->endCoalescing();
    const double afterFirst = opacityUnderBase(*s);
    for (int i = 0; i < 100; ++i) slide(*s, afterFirst - i * 0.001, true);
    s->endCoalescing();

    CHECK(s->undo());
    CHECK_EQ(opacityUnderBase(*s), afterFirst);   // only the second gesture went
    CHECK(s->undo());
    CHECK_EQ(opacityUnderBase(*s), 1.0);
    CHECK(!s->canUndo());
}

// THE KEY. The fold is keyed on (path, property, scope), so two controls dragged
// in the same section do not collapse into each other, and the SAME control
// under two scopes does not either -- which is what would let an edit to Dark be
// undone as though it had been an edit to Base.
TEST_CASE(kit_the_fold_is_keyed_on_property_and_scope) {
    auto s = ick::Session::open(makeBundle("key"));
    REQUIRE(s.has_value());

    // Same node, same scope, two properties: two commands.
    s->setProperty(kLayer, "opacity", icf::Context{}, icf::json::Value::number(0.5), true);
    s->setProperty(kLayer, "glass", icf::Context{}, icf::json::Value::boolean(true), true);
    CHECK_EQ(undoDepth(*s), std::size_t{2});

    auto t = ick::Session::open(makeBundle("key-scope"));
    REQUIRE(t.has_value());
    const icf::Context dark{icf::Appearance::Dark, icf::Idiom::Base};
    // Same node, same property, two scopes: two commands, because they are two
    // different members of the document.
    slide(*t, 0.5, true);
    slide(*t, 0.25, true, dark);
    CHECK_EQ(undoDepth(*t), std::size_t{2});
}

// THE LEXEME, AND WHAT A FLOAT DOES TO IT.
//
// This is the defect the Geometry section carried: it read `position` into a
// `Position`, cast every member to `float` for the control, and wrote the whole
// thing back. A coordinate nobody touched went out through a float and came back
// a different number -- silently, and in a document the round-trip gate compares
// byte for byte.
TEST_CASE(kit_a_float_round_trip_rewrites_a_coordinate_nobody_touched) {
    const char* kSpelled = "0.5000000000000001";
    auto parsed = icf::json::parse(kSpelled);
    REQUIRE(parsed.has_value());
    // It arrives, and it survives being written straight back: the lexeme is
    // carried, not re-printed.
    CHECK_EQ(icf::json::write(*parsed), std::string(kSpelled));

    const double exact = std::strtod(kSpelled, nullptr);
    // THE DOUBLE PATH. Re-printing the value through `number(double)` gives the
    // same text back, so a control that keeps doubles may rewrite a member it
    // did touch without damaging what it means.
    CHECK_EQ(icf::json::write(icf::json::Value::number(exact)), std::string(kSpelled));

    // THE FLOAT PATH, which is what the control used to do to every coordinate
    // on every frame of every drag, including the ones the drag was not about.
    const double throughFloat = static_cast<double>(static_cast<float>(exact));
    CHECK(icf::json::write(icf::json::Value::number(throughFloat)) != std::string(kSpelled));
}

// THE SAME THING SAID ABOUT THE WHOLE PROPERTY, through the reader and writer the
// Geometry section actually uses. `positionToJson(positionFrom(v))` is held to
// the bytes (test_values_tojson); putting a float in the middle of it is not.
TEST_CASE(kit_the_position_round_trip_is_exact_in_doubles_and_lossy_in_floats) {
    const char* kPosition = R"({"scale":0.8,"translation-in-points":[0,0.5000000000000001]})";
    auto parsed = icf::json::parse(kPosition);
    REQUIRE(parsed.has_value());
    auto p = icf::positionFrom(*parsed);
    REQUIRE(p.has_value());

    // Doubles all the way: the same bytes come back.
    CHECK_EQ(icf::json::write(icf::positionToJson(*p)), icf::json::write(*parsed));

    // The old control's arithmetic, exactly: every member through a float, and
    // the scale out to a percentage and back.
    icf::Position viaFloat;
    viaFloat.scale = static_cast<double>(static_cast<float>(p->scale * 100.0)) / 100.0;
    viaFloat.translation = {static_cast<double>(static_cast<float>(p->translation.x)),
                            static_cast<double>(static_cast<float>(p->translation.y))};
    CHECK(icf::json::write(icf::positionToJson(viaFloat)) != icf::json::write(*parsed));
}
