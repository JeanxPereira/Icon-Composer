#include "check.h"
#include "Source/cli/Report.h"

#include <filesystem>
#include <fstream>

using namespace icf;
namespace fs = std::filesystem;

namespace {

fs::path makeBundle(const std::string& name, const std::string& document,
                    const std::vector<std::string>& assets) {
    const fs::path dir = fs::temp_directory_path() / ("ic-cli-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << document;
    for (const auto& a : assets) std::ofstream(dir / "Assets" / a, std::ios::binary) << "<svg/>";
    return dir;
}

// Two groups, three layers, a specialization on the second layer's fill and one
// on the third layer's image. Small enough to assert exactly, wide enough to
// exercise the resolver.
const char* kDoc = R"({
  "supported-platforms" : { "squares" : "shared" },
  "fill" : { "solid" : "srgb:0.00000,0.00000,0.00000,1.00000" },
  "groups" : [
    { "name" : "Back",
      "layers" : [ { "name" : "plate", "image-name" : "plate.svg", "glass" : false } ] },
    { "name" : "Front", "opacity" : 0.8,
      "layers" : [
        { "name" : "mark", "image-name" : "mark.svg", "glass" : true,
          "fill-specializations" : [ { "value" : "none" },
                                     { "appearance" : "dark", "value" : { "solid" : "gray:1.00000,1.00000" } } ] },
        { "name" : "badge",
          "image-name-specializations" : [ { "value" : "badge.svg" },
                                           { "idiom" : "watchOS", "value" : "badge-round.svg" } ] } ] } ] })";

}  // namespace

TEST_CASE(cli_summary_states_what_the_bundle_holds) {
    const auto dir = makeBundle("summary", kDoc,
                                {"plate.svg", "mark.svg", "badge.svg", "badge-round.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string out = cli::summary(*b);
    CHECK(out.find("2 groups, 3 layers") != std::string::npos);
    CHECK(out.find("4 assets") != std::string::npos);
    CHECK(out.find("platforms    squares: shared") != std::string::npos);
}

// The tree is the point of the tool: what the icon composes to, FOR A CONTEXT.
// The same document read as dark and as light must differ, and visibly.
TEST_CASE(cli_tree_resolves_for_the_context_it_is_given) {
    const auto dir = makeBundle("tree", kDoc,
                                {"plate.svg", "mark.svg", "badge.svg", "badge-round.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());

    const std::string light = cli::tree(*b, {Appearance::Light, Idiom::Base});
    const std::string dark = cli::tree(*b, {Appearance::Dark, Idiom::Base});
    const std::string watch = cli::tree(*b, {Appearance::Light, Idiom::WatchOS});

    CHECK(light.find("fill none") != std::string::npos);
    CHECK(dark.find("fill solid gray:1.00000,1.00000") != std::string::npos);
    CHECK(light.find("badge.svg") != std::string::npos);
    CHECK(watch.find("badge-round.svg") != std::string::npos);
    CHECK(light != dark);
}

// A shadow is `{kind, opacity}`. Printing it as JSON puts three lines and a pair
// of braces where two words belong, and the tree stops being readable at exactly
// the moment it gets interesting.
TEST_CASE(cli_tree_renders_a_shadow_as_two_words) {
    const auto dir = makeBundle("shadow", kDoc,
                                {"plate.svg", "mark.svg", "badge.svg", "badge-round.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const char* withShadow = R"({"groups":[{"name":"g","shadow":{"kind":"neutral","opacity":0.5},
                                            "layers":[{"name":"l"}]}]})";
    const auto dir2 = makeBundle("shadow2", withShadow, {});
    auto b2 = IconBundle::open(dir2);
    REQUIRE(b2.has_value());
    const std::string out = cli::tree(*b2, {Appearance::Base, Idiom::Base});
    CHECK(out.find("shadow neutral 0.5") != std::string::npos);
    CHECK(out.find("{") == std::string::npos);
    (void)dir;
}

TEST_CASE(cli_tree_marks_a_layer_that_is_glass) {
    const auto dir = makeBundle("glass", kDoc,
                                {"plate.svg", "mark.svg", "badge.svg", "badge-round.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string out = cli::tree(*b, {Appearance::Light, Idiom::Base});
    CHECK(out.find("mark") != std::string::npos);
    CHECK(out.find("[glass]") != std::string::npos);
}

// A report that stays quiet about a broken reference is worse than no report:
// two of 55 real bundles carry one (doc 02 §3).
TEST_CASE(cli_assets_report_names_a_dangling_reference) {
    const auto dir = makeBundle("dangling", kDoc, {"plate.svg", "mark.svg", "badge.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string out = cli::assets(*b);
    CHECK(out.find("MISSING  badge-round.svg") != std::string::npos);
}

TEST_CASE(cli_assets_report_names_a_file_nothing_uses) {
    const auto dir = makeBundle("unused", kDoc,
                                {"plate.svg", "mark.svg", "badge.svg", "badge-round.svg", "old.png"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string out = cli::assets(*b);
    CHECK(out.find("UNUSED   old.png") != std::string::npos);
}
