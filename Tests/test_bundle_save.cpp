#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace icf;
namespace fs = std::filesystem;

namespace {
std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
fs::path scratch(const char* name) {
    const fs::path dir = fs::temp_directory_path() / (std::string("ic-save-") + name + ".icon");
    fs::remove_all(dir);
    fs::create_directories(dir / "Assets");
    std::ofstream(dir / "icon.json", std::ios::binary)
        << "{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n    {\n      \"layers\" : [\n        {\n          \"image-name\" : \"a.svg\",\n          \"name\" : \"a\"\n        }\n      ],\n      \"name\" : \"g\"\n    }\n  ]\n}";
    std::ofstream(dir / "Assets" / "a.svg", std::ios::binary) << "<svg/>";
    return dir;
}
}  // namespace

TEST_CASE(bundle_save_writes_the_tree_back_and_leaves_no_temp_file) {
    const fs::path dir = scratch("basic");
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    const std::string before = slurp(dir / "icon.json");
    CHECK_EQ(b->save(), std::string(""));
    CHECK_EQ(slurp(dir / "icon.json"), before);
    json::Value* layer = nodeAt(b->json(), NodePath{0, 0});
    REQUIRE(layer != nullptr);
    setProperty(*layer, "glass", Context{}, json::Value::boolean(true));
    CHECK_EQ(b->save(), std::string(""));
    CHECK(slurp(dir / "icon.json").find("\"glass\" : true") != std::string::npos);
    std::size_t files = 0;
    for (const auto& e : fs::directory_iterator(dir)) { (void)e; ++files; }
    CHECK_EQ(files, std::size_t(2));  // icon.json and Assets/, no leftover temp
}

TEST_CASE(bundle_save_as_copies_assets_and_retargets) {
    const fs::path dir = scratch("as-src");
    const fs::path dst = fs::temp_directory_path() / "ic-save-as-dst.icon";
    fs::remove_all(dst);
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(b->saveAs(dst), std::string(""));
    CHECK(b->path() == dst);
    CHECK(fs::exists(dst / "Assets" / "a.svg"));
    CHECK_EQ(slurp(dst / "icon.json"), slurp(dir / "icon.json"));
}

TEST_CASE(bundle_clone_is_deep) {
    const fs::path dir = scratch("clone");
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    IconBundle c = b->clone();
    nodeAt(c.json(), NodePath{0, 0})->set("name", json::Value::string("changed"));
    CHECK(nodeAt(b->json(), NodePath{0, 0})->find("name")->rawString() == "a");
    CHECK(c.path() == b->path());
    CHECK_EQ(c.assetFiles().size(), b->assetFiles().size());
}

TEST_CASE(bundle_import_asset_copies_into_assets_and_lists_it) {
    const fs::path dir = scratch("import");
    const fs::path src = fs::temp_directory_path() / "ic-import-b.svg";
    std::ofstream(src, std::ios::binary) << "<svg id='b'/>";
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(b->importAsset(src), std::string(""));
    CHECK(fs::exists(dir / "Assets" / "b.svg"));
    CHECK_EQ(b->assetFiles().size(), std::size_t(2));
    CHECK(b->assetFiles()[1] == "b.svg");
    CHECK(!b->importAsset(fs::temp_directory_path() / "ic-does-not-exist.svg").empty());
}

TEST_CASE(bundle_save_keeps_every_byte_exact_corpus_document_byte_exact) {
    // Open, edit, put the edit back by hand, save to a scratch copy: the bytes must
    // be the corpus's own for every document the writer already reproduces.
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t docs = 0, exact = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        const std::string original = slurp(e.path() / "icon.json");
        if (json::write(b->json()) != original) continue;  // one of the 10 reformatted
        ++docs;
        json::Value* g0 = nodeAt(b->json(), NodePath{0, std::nullopt});
        REQUIRE(g0 != nullptr);
        const json::Value snapshot = *g0;
        setProperty(*g0, "opacity", Context{Appearance::Dark, Idiom::Base}, json::Value::number(0.5));
        *g0 = snapshot;
        const fs::path out = fs::temp_directory_path() / ("ic-exact-" + e.path().filename().string());
        fs::remove_all(out);
        REQUIRE(b->saveAs(out).empty());
        if (slurp(out / "icon.json") == original) ++exact;
        fs::remove_all(out);
    }
    std::printf("  %zu byte-exact documents saved, %zu still byte-exact\n", docs, exact);
    CHECK(docs >= 135);
    CHECK_EQ(exact, docs);
}
