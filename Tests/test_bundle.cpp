#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <filesystem>
#include <fstream>
#include <string>

using namespace icf;
namespace fs = std::filesystem;

namespace {

// A throwaway `.icon` on disk. Written, not mocked: the thing under test reads a
// directory, so the test gives it a directory.
fs::path makeBundle(const std::string& name, const std::string& document,
                    const std::vector<std::string>& assets) {
    const fs::path dir = fs::temp_directory_path() / ("ic-test-" + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary) << document;
    for (const auto& a : assets) {
        std::ofstream(dir / "Assets" / a, std::ios::binary) << "<svg/>";
    }
    return dir;
}

const char* kOneLayer = R"({"groups":[{"layers":[{"name":"a","image-name":"a.svg"}]}]})";

}  // namespace

// A `.icon` is a FOLDER: `icon.json` beside a flat `Assets/`. Measured over 45
// bundles, all 45 have exactly that shape (doc 02).
TEST_CASE(bundle_rejects_a_directory_with_no_document) {
    const fs::path dir = fs::temp_directory_path() / "ic-test-empty.icon";
    fs::remove_all(dir);
    fs::create_directories(dir);
    CHECK(!IconBundle::open(dir).has_value());
    CHECK(!IconBundle::open(dir / "does-not-exist").has_value());
}

// An `icon.json` that parses but is not a `.icon` is not a bundle. Opening it
// anyway would hand the caller a document view that answers every question with
// silence -- no groups, no layers, no error.
TEST_CASE(bundle_rejects_a_document_that_is_not_an_icon) {
    const auto dir = makeBundle("notanicon", R"([1,2])", {});
    CHECK(!IconBundle::open(dir).has_value());
    const auto broken = makeBundle("broken", R"({"groups":)", {});
    CHECK(!IconBundle::open(broken).has_value());
}

TEST_CASE(bundle_opens_and_lists_what_it_holds) {
    const auto dir = makeBundle("simple", kOneLayer, {"a.svg", "b.png"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    REQUIRE(b->document().groups().size() == 1);
    REQUIRE(b->assetFiles().size() == 2);
    CHECK_EQ(b->assetFiles()[0], std::string("a.svg"));
    CHECK_EQ(b->assetFiles()[1], std::string("b.png"));
}

// The names come through the RESOLVER, across every context -- so this is the
// set of images the icon can actually display, not merely the strings that
// happen to appear in the file.
TEST_CASE(bundle_collects_the_images_every_context_can_reach) {
    const char* doc = R"({"groups":[{"layers":[
        {"name":"a","image-name":"base.svg",
         "image-name-specializations":[{"appearance":"dark","value":"dark.svg"}]}]}]})";
    const auto dir = makeBundle("contexts", doc, {"base.svg", "dark.svg"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    auto names = b->referencedImageNames();
    REQUIRE(names.size() == 2);
    CHECK_EQ(names[0], std::string("base.svg"));
    CHECK_EQ(names[1], std::string("dark.svg"));
}

TEST_CASE(bundle_reports_an_image_the_document_names_and_the_folder_lacks) {
    const auto dir = makeBundle("missing", kOneLayer, {});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    REQUIRE(b->missingAssets().size() == 1);
    CHECK_EQ(b->missingAssets()[0], std::string("a.svg"));
    CHECK(b->unusedAssets().empty());
}

TEST_CASE(bundle_reports_a_file_no_layer_points_at) {
    const auto dir = makeBundle("unused", kOneLayer, {"a.svg", "stray.png"});
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK(b->missingAssets().empty());
    REQUIRE(b->unusedAssets().size() == 1);
    CHECK_EQ(b->unusedAssets()[0], std::string("stray.png"));
}
