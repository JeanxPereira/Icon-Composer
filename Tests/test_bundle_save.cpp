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
    // The source lives outside the bundle, under the name it keeps: `importAsset`
    // copies a file into `Assets/` by its own filename, and that filename is what a
    // layer's `image-name` will have to spell.
    const fs::path srcDir = fs::temp_directory_path() / "ic-import-src";
    fs::remove_all(srcDir);
    fs::create_directories(srcDir);
    const fs::path src = srcDir / "b.svg";
    std::ofstream(src, std::ios::binary) << "<svg id='b'/>";
    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(b->importAsset(src), std::string(""));
    CHECK(fs::exists(dir / "Assets" / "b.svg"));
    CHECK_EQ(b->assetFiles().size(), std::size_t(2));
    CHECK(b->assetFiles()[1] == "b.svg");
    CHECK(!b->importAsset(srcDir / "does-not-exist.svg").empty());
}

// REIMPORTAR POR CIMA DE UM NOME QUE JÁ ESTÁ EM `Assets/`.
//
// É o gesto normal de quem desenha o ícone: reexportar o SVG do editor de arte
// e importá-lo de novo, com o mesmo nome, para a mesma camada. Falhava --
// "could not copy a.svg: File exists" -- e não trocava byte nenhum, porque
// `copy_options::overwrite_existing` não sobrescreve nesta toolchain (a
// medição está em IconBundle.cpp). O caso acima não o alcançava: importa um
// nome NOVO, onde a flag nunca é exercida.
//
// Este caso cobra as duas coisas que o defeito quebrava ao mesmo tempo -- o
// relato de erro E os bytes -- e mais a geração, que é o que avisa quem tenha
// LIDO o arquivo antigo (a `Session` cacheia a `viewBox` de cada asset).
TEST_CASE(bundle_import_asset_replaces_an_asset_that_is_already_there) {
    const fs::path dir = scratch("reimport");
    const fs::path srcDir = fs::temp_directory_path() / "ic-reimport-src";
    fs::remove_all(srcDir);
    fs::create_directories(srcDir);
    const fs::path src = srcDir / "a.svg";      // o MESMO nome que `scratch` põe
    std::ofstream(src, std::ios::binary) << "<svg id='novo'/>";

    auto b = IconBundle::open(dir);
    REQUIRE(b.has_value());
    CHECK_EQ(slurp(dir / "Assets" / "a.svg"), std::string("<svg/>"));
    const std::uint64_t gen = b->assetsGeneration();

    CHECK_EQ(b->importAsset(src), std::string(""));
    // OS BYTES, e não só a ausência de erro: um import que reportasse sucesso
    // e deixasse a arte velha no lugar é exatamente o defeito que havia.
    CHECK_EQ(slurp(dir / "Assets" / "a.svg"), std::string("<svg id='novo'/>"));
    // A lista não muda -- o nome já estava lá --, e é por isso que ela não
    // serve como aviso de que a arte mudou. A geração serve.
    CHECK_EQ(b->assetFiles().size(), std::size_t(1));
    CHECK(b->assetsGeneration() != gen);
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
        // The SAME tolerance `corpus_gate` applies, and for the same reason: Apple's
        // `JSONEncoder` writes no trailing newline, and some repositories add one.
        // The project's 135 is 122 identical outright plus 13 that differ only there
        // (Tests/test_corpus.cpp); the other 10 had the space before the colon
        // stripped by a formatter of their own, which is a third-party edit.
        std::string expected = slurp(e.path() / "icon.json");
        while (!expected.empty() && (expected.back() == '\n' || expected.back() == '\r')) {
            expected.pop_back();
        }
        if (json::write(b->json()) != expected) continue;  // one of the 10 reformatted
        ++docs;
        json::Value* g0 = nodeAt(b->json(), NodePath{0, std::nullopt});
        REQUIRE(g0 != nullptr);
        const json::Value snapshot = *g0;
        setProperty(*g0, "opacity", Context{Appearance::Dark, Idiom::Base}, json::Value::number(0.5));
        *g0 = snapshot;
        const fs::path out = fs::temp_directory_path() / ("ic-exact-" + e.path().filename().string());
        fs::remove_all(out);
        REQUIRE(b->saveAs(out).empty());
        if (slurp(out / "icon.json") == expected) ++exact;
        fs::remove_all(out);
    }
    std::printf("  %zu byte-exact documents saved, %zu still byte-exact\n", docs, exact);
    CHECK(docs >= 135);
    CHECK_EQ(exact, docs);
}
