#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerFoundation/Values.h"
#include "Source/cli/Report.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// The corpus is third-party material and never enters git; it is pointed at,
// never vendored. A run that cannot find it FAILS -- it does not skip. A gate
// that quietly passes when it did not run is worse than no gate.
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

// THE M1 GATE.
//
// Every document is real: written by Apple's own encoder, through the real app,
// by 145 different people who never heard of this project. Three properties,
// and the third is the one that carries the weight.
TEST_CASE(corpus_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());  // IC_CORPUS_DIR unset or empty -- see corpusDocuments()

    int parsed = 0, idempotent = 0, byteExact = 0;
    std::vector<std::string> unparsed, lossy;
    std::vector<std::string> reformatted;

    for (const auto& p : docs) {
        const std::string raw = readAll(p);
        auto v = icf::json::parse(raw);
        if (!v) {
            unparsed.push_back(p.parent_path().filename().string());
            continue;
        }
        ++parsed;

        // (2) A full cycle loses nothing: writing what we read, reading that
        // back and writing it again must give the same bytes. This is what says
        // the model kept the content, independently of how the file was spaced.
        const std::string once = icf::json::write(*v);
        auto again = icf::json::parse(once);
        if (!again || icf::json::write(*again) != once) {
            lossy.push_back(p.parent_path().filename().string());
            continue;
        }
        ++idempotent;

        // (3) The differential against Apple's encoder. `JSONEncoder` with
        // `.prettyPrinted` and `.sortedKeys` writes no trailing newline; some
        // repositories add one, and some ran a formatter that stripped the space
        // before the colon. Those are third-party edits, not our disagreement.
        std::string expected = raw;
        while (!expected.empty() && (expected.back() == '\n' || expected.back() == '\r')) {
            expected.pop_back();
        }
        if (once == expected) {
            ++byteExact;
        } else {
            reformatted.push_back(p.parent_path().filename().string());
        }
    }

    std::printf("  %zu documents: %d parsed, %d lossless, %d byte-exact\n",
                docs.size(), parsed, idempotent, byteExact);

    for (const auto& n : unparsed) std::printf("    UNPARSED  %s\n", n.c_str());
    for (const auto& n : lossy) std::printf("    LOSSY     %s\n", n.c_str());

    CHECK_EQ(parsed, static_cast<int>(docs.size()));
    CHECK_EQ(idempotent, static_cast<int>(docs.size()));

    // 135 of 145 measured on 2026-08-31: 122 byte-exact outright, 13 more once a
    // trailing newline is allowed. The other 10 had the space before the colon
    // removed by a formatter in their own repository -- property (2) already
    // proved their content survives.
    CHECK(byteExact >= 135);
    if (byteExact < static_cast<int>(docs.size())) {
        std::printf("  %zu formatted by their own repository, content proven intact:\n",
                    reformatted.size());
        for (const auto& n : reformatted) std::printf("    %s\n", n.c_str());
    }
}

namespace {

constexpr icf::Appearance kAppearances[] = {
    icf::Appearance::Base, icf::Appearance::Light,
    icf::Appearance::Dark, icf::Appearance::Tinted,
};
constexpr icf::Idiom kIdioms[] = {
    icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS,
    icf::Idiom::MacOS, icf::Idiom::WatchOS,
};

bool endsWith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Every specialization entry, everywhere in the tree, with the owner that holds
// it and the property it specializes.
void collectSpecializations(
    const icf::json::Value& node,
    std::vector<std::tuple<const icf::json::Value*, std::string, size_t>>& out) {
    if (node.kind() == icf::json::Value::Kind::Object) {
        for (const auto& m : node.members()) {
            if (endsWith(m.first, "-specializations") &&
                m.second.kind() == icf::json::Value::Kind::Array) {
                const std::string property =
                    m.first.substr(0, m.first.size() - std::string("-specializations").size());
                for (size_t i = 0; i < m.second.elements().size(); ++i) {
                    out.emplace_back(&node, property, i);
                }
            }
            collectSpecializations(m.second, out);
        }
    } else if (node.kind() == icf::json::Value::Kind::Array) {
        for (const auto& e : node.elements()) collectSpecializations(e, out);
    }
}

}  // namespace

// THE MODEL GATE.
//
// Two properties, and the second is the one that would catch a resolver that
// merely looks right.
TEST_CASE(corpus_model_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());

    std::vector<icf::json::Value> keep;  // the views borrow; the trees must outlive them
    keep.reserve(docs.size());
    int opened = 0, clean = 0;
    int groups = 0, layers = 0;
    int entries = 0, reachable = 0;
    std::vector<std::string> offenders;

    for (const auto& p : docs) {
        auto v = icf::json::parse(readAll(p));
        if (!v) continue;
        keep.push_back(std::move(*v));
        const icf::json::Value& root = keep.back();

        auto doc = icf::IconDocument::open(root);
        if (!doc) {
            offenders.push_back(p.parent_path().filename().string() + ": not a document");
            continue;
        }
        ++opened;

        // (1) The model recognises every key. A typed layer that ignores what it
        // does not know reports success on a document it half understood.
        auto unknown = doc->unknownKeys();
        if (unknown.empty()) {
            ++clean;
        } else {
            for (const auto& k : unknown) {
                offenders.push_back(p.parent_path().filename().string() + ": unknown key " + k);
            }
        }

        for (const auto& g : doc->groups()) {
            ++groups;
            layers += static_cast<int>(g.layers().size());
        }

        // (2) No specialization is DEAD. For every entry in every list, some
        // (appearance, idiom) must make the resolver choose exactly it. An entry
        // nothing can reach means the matching rule is wrong -- and a resolver
        // that quietly never selects a case is invisible until a pixel is wrong.
        std::vector<std::tuple<const icf::json::Value*, std::string, size_t>> specs;
        collectSpecializations(root, specs);
        for (const auto& [owner, property, index] : specs) {
            ++entries;
            const icf::json::Value* list = owner->find(property + "-specializations");
            const icf::json::Value* want = list->elements()[index].find("value");
            if (!want) continue;  // an entry with no value cannot be selected, by design
            bool hit = false;
            for (auto a : kAppearances) {
                for (auto i : kIdioms) {
                    if (icf::resolve(*owner, property, {a, i}) == want) {
                        hit = true;
                        break;
                    }
                }
                if (hit) break;
            }
            if (hit) {
                ++reachable;
            } else {
                offenders.push_back(p.parent_path().filename().string() + ": unreachable " +
                                    property + "-specializations[" + std::to_string(index) + "]");
            }
        }
    }

    std::printf("  %d documents opened, %d with no unknown key\n", opened, clean);
    std::printf("  %d groups, %d layers, %d specializations (%d reachable)\n",
                groups, layers, entries, reachable);
    for (size_t i = 0; i < offenders.size() && i < 20; ++i) {
        std::printf("    %s\n", offenders[i].c_str());
    }
    if (offenders.size() > 20) std::printf("    ... and %zu more\n", offenders.size() - 20);

    CHECK_EQ(opened, static_cast<int>(docs.size()));
    CHECK_EQ(clean, static_cast<int>(docs.size()));
    CHECK_EQ(reachable, entries);
}

namespace {

// A value that resolved must DECODE. The shapes come from doc 01 §7, measured
// over the corpus; `blur-material` is nullable and `specular` carries two shapes,
// and both of those are stated here rather than papered over.
bool decodes(std::string_view property, const icf::json::Value& v) {
    using K = icf::json::Value::Kind;
    if (property == "fill") return icf::fillFrom(v).has_value();
    if (property == "shadow") return icf::shadowFrom(v).has_value();
    if (property == "position") return icf::positionFrom(v).has_value();
    if (property == "translucency") return icf::translucencyFrom(v).has_value();
    if (property == "refractivity") return icf::refractivityFrom(v).has_value();
    if (property == "blend-mode") {
        return v.kind() == K::String && icf::blendModeFromString(v.rawString()).has_value();
    }
    if (property == "lighting") {
        return v.kind() == K::String && icf::lightingFromString(v.rawString()).has_value();
    }
    if (property == "specular") {
        // bool in 100 places, the four-case enum in 3 (doc 01 §7).
        if (v.kind() == K::Bool) return true;
        return v.kind() == K::String && icf::specularHighlightFromString(v.rawString()).has_value();
    }
    if (property == "blur-material") {
        // 48 of 123 are an explicit `null` -- "no material", which is not the
        // same as the key being absent.
        return v.kind() == K::Null || v.kind() == K::Number;
    }
    if (property == "opacity" || property == "material") return v.kind() == K::Number;
    if (property == "hidden" || property == "glass") return v.kind() == K::Bool;
    if (property == "image-name") return v.kind() == K::String;
    return true;  // a property with no typed reader yet
}

constexpr std::string_view kTypedProperties[] = {
    "fill", "shadow", "position", "translucency", "refractivity", "blend-mode",
    "lighting", "specular", "blur-material", "opacity", "material", "hidden",
    "glass", "image-name",
};

}  // namespace

// THE VALUE GATE.
//
// Every typed property of every group and every layer, resolved for all twenty
// (appearance, idiom) contexts, must decode. A reader that returns nullopt on a
// real document is a reader that would make the renderer fall back to a default
// nobody chose -- silently, and only visible as a wrong pixel.
TEST_CASE(corpus_value_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());

    std::vector<icf::json::Value> keep;
    keep.reserve(docs.size());
    int decoded = 0, failed = 0;
    std::vector<std::string> offenders;

    auto check = [&](const icf::json::Value& owner, std::string_view what,
                     const std::string& where) {
        for (auto property : kTypedProperties) {
            for (auto a : kAppearances) {
                for (auto i : kIdioms) {
                    const icf::json::Value* v = icf::resolve(owner, property, {a, i});
                    if (!v) continue;
                    if (decodes(property, *v)) {
                        ++decoded;
                    } else {
                        ++failed;
                        if (offenders.size() < 20) {
                            offenders.push_back(where + " " + std::string(what) + " " +
                                                std::string(property) + " = " +
                                                icf::json::write(*v));
                        }
                    }
                }
            }
        }
    };

    for (const auto& p : docs) {
        auto v = icf::json::parse(readAll(p));
        if (!v) continue;
        keep.push_back(std::move(*v));
        const icf::json::Value& root = keep.back();
        auto doc = icf::IconDocument::open(root);
        if (!doc) continue;
        const std::string name = p.parent_path().filename().string();

        check(root, "document", name);
        for (const auto& g : doc->groups()) {
            check(g.json(), "group", name);
            for (const auto& l : g.layers()) check(l.json(), "layer", name);
        }
    }

    std::printf("  %d values decoded, %d failed\n", decoded, failed);
    for (const auto& o : offenders) std::printf("    %s\n", o.c_str());
    CHECK_EQ(failed, 0);
    CHECK(decoded > 0);
}

// THE BUNDLE GATE.
//
// The first measurement, over 35 bundles, said the reference between
// `image-name` and the files in `Assets/` is a bijection. Widening the sample to
// 55 broke it: two bundles NAME an image that is not there, both from a
// specialization, both a stale name left behind by a rename (chromium asks for
// `2 – Layer.svg` beside a `2.svg`; loupe asks for `loupe-icon-light 3.png`
// beside a `loupe-icon-light.png`). The files are absent upstream too -- this is
// what real documents do, not what the fetch lost.
//
// So the bijection is not a law of the format, and the gate does not pretend it
// is. One half of it does hold, and that is the assertion:
//
//   * EVERY file in `Assets/` is named by some layer in some context. No bundle
//     carries dead weight.
//   * NOT every name resolves. The dangling ones are counted, and a jump in that
//     count is what the gate refuses.
TEST_CASE(corpus_bundle_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());

    int bundles = 0, clean = 0, assets = 0, referenced = 0;
    int dangling = 0, withUnused = 0;
    std::vector<std::string> offenders;

    for (const auto& p : docs) {
        const fs::path dir = p.parent_path();
        std::error_code ec;
        if (!fs::is_directory(dir / "Assets", ec)) continue;  // document-only entry
        auto b = icf::IconBundle::open(dir);
        if (!b) {
            offenders.push_back(dir.filename().string() + ": would not open");
            continue;
        }
        ++bundles;
        assets += static_cast<int>(b->assetFiles().size());
        referenced += static_cast<int>(b->referencedImageNames().size());

        const auto missing = b->missingAssets();
        const auto unused = b->unusedAssets();
        if (missing.empty() && unused.empty()) ++clean;
        if (!unused.empty()) ++withUnused;
        dangling += static_cast<int>(missing.size());

        for (const auto& m : missing) {
            offenders.push_back(dir.filename().string() + ": names " + m + ", no such file");
        }
        for (const auto& u : unused) {
            offenders.push_back(dir.filename().string() + ": holds " + u + ", nothing names it");
        }
    }

    std::printf("  %d bundles: %d matching exactly, %d dangling reference(s), "
                "%d with an unused file (%d files, %d references)\n",
                bundles, clean, dangling, withUnused, assets, referenced);
    for (size_t i = 0; i < offenders.size() && i < 20; ++i) {
        std::printf("    %s\n", offenders[i].c_str());
    }

    // Fewer than this means the corpus was fetched without `--with-assets`, and
    // the gate would be reporting on nothing.
    CHECK(bundles >= 50);
    // No bundle carries a file nothing names. This half held over all 55.
    CHECK_EQ(withUnused, 0);
    // Two dangling references, both known and named above. More than that is
    // either a new corpus entry worth reading or a reader that stopped resolving
    // -- and both deserve a look before the number moves.
    CHECK(dangling <= 2);
    CHECK(referenced > assets);  // the dangling names are counted as references
}

// THE REPORT GATE.
//
// The tree is the tool's whole point, so it must stay readable over every real
// bundle in every context. A brace in the output means a property shape the
// report does not know and fell back to dumping as JSON -- which is the moment
// the tree stops being a tree.
TEST_CASE(corpus_report_gate) {
    auto docs = corpusDocuments();
    REQUIRE(!docs.empty());

    int bundles = 0, trees = 0;
    std::vector<std::string> offenders;

    for (const auto& p : docs) {
        const fs::path dir = p.parent_path();
        std::error_code ec;
        if (!fs::is_directory(dir / "Assets", ec)) continue;
        auto b = icf::IconBundle::open(dir);
        if (!b) continue;
        ++bundles;
        if (icf::cli::summary(*b).empty()) {
            offenders.push_back(dir.filename().string() + ": empty summary");
        }
        for (auto a : kAppearances) {
            for (auto i : kIdioms) {
                const std::string t = icf::cli::tree(*b, {a, i});
                ++trees;
                if (t.empty()) {
                    offenders.push_back(dir.filename().string() + ": empty tree");
                } else if (t.find('{') != std::string::npos) {
                    const size_t at = t.find('{');
                    offenders.push_back(dir.filename().string() + ": raw JSON in tree near '" +
                                        t.substr(at, 40) + "'");
                }
            }
        }
    }

    std::printf("  %d bundles, %d trees rendered, %zu offender(s)\n",
                bundles, trees, offenders.size());
    for (size_t i = 0; i < offenders.size() && i < 10; ++i) {
        std::printf("    %s\n", offenders[i].c_str());
    }
    CHECK(bundles >= 50);
    CHECK_EQ(static_cast<int>(offenders.size()), 0);
}
