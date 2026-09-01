#include "check.h"
#include "Source/CoreSVG/Path.h"
#include "Source/CoreSVG/Document.h"
#include "Source/CoreSVG/Xml.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::vector<fs::path> corpusSvgs() {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return {};
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& bundle : fs::directory_iterator(fs::path(dir), ec)) {
        const fs::path assets = bundle.path() / "Assets";
        if (!fs::is_directory(assets, ec)) continue;
        for (const auto& f : fs::directory_iterator(assets, ec)) {
            if (f.path().extension() == ".svg") out.push_back(f.path());
        }
    }
    return out;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// Every `d="…"` in the file. A scan, not a parse: the XML layer is a separate
// question, and this gate is about the path grammar alone.
std::vector<std::string> pathData(const std::string& svg) {
    std::vector<std::string> out;
    size_t i = 0;
    while ((i = svg.find(" d=\"", i)) != std::string::npos) {
        const size_t start = i + 4;
        const size_t end = svg.find('"', start);
        if (end == std::string::npos) break;
        out.push_back(svg.substr(start, end - start));
        i = end;
    }
    return out;
}

}  // namespace

// THE PATH GATE.
//
// 373 `d` attributes across 149 real SVGs, none of them written for this
// project. What fails is counted BY CAUSE: a command the reader declines to
// implement is a scope decision with a number attached (doc 04 §4), and
// malformed data would be a defect.
TEST_CASE(corpus_svg_path_gate) {
    auto files = corpusSvgs();
    REQUIRE(!files.empty());

    int total = 0, ok = 0, malformed = 0;
    long long segments = 0;
    std::map<char, int> declined;
    std::vector<std::string> offenders;

    for (const auto& f : files) {
        for (const auto& d : pathData(readAll(f))) {
            ++total;
            auto p = icf::svg::parsePath(d);
            if (p) {
                ++ok;
                segments += static_cast<long long>(p->segments.size());
                continue;
            }
            if (p.error().unsupportedCommand) {
                ++declined[p.error().unsupportedCommand];
            } else {
                ++malformed;
                if (offenders.size() < 10) {
                    offenders.push_back(f.filename().string() + " @" +
                                        std::to_string(p.error().offset) + ": " +
                                        d.substr(0, 60));
                }
            }
        }
    }

    std::printf("  %zu SVGs, %d path(s): %d parsed into %lld segments, %d malformed\n",
                files.size(), total, ok, segments, malformed);
    for (const auto& [cmd, n] : declined) {
        std::printf("    declined '%c' x%d\n", cmd, n);
    }
    for (const auto& o : offenders) std::printf("    MALFORMED %s\n", o.c_str());

    CHECK(files.size() >= 140);
    CHECK(total >= 350);
    // Malformed is a defect in the reader or a corrupt file; a declined command
    // is a scope decision. The two must never be confused, so only one of them
    // is allowed to be non-zero.
    CHECK_EQ(malformed, 0);
    // Arcs, twice in 149 files (doc 04 §2). More than a handful would mean the
    // scope decision needs revisiting.
    CHECK(declined.size() <= 2);
}

// THE XML GATE.
//
// 149 real SVGs, written by whatever each author's tool emits -- Inkscape,
// Illustrator, Figma, a script. Every one must parse, and the census of what
// they contain is what scopes the layer above (doc 04 §4).
TEST_CASE(corpus_svg_xml_gate) {
    auto files = corpusSvgs();
    REQUIRE(!files.empty());

    int parsed = 0;
    std::map<std::string, int> elements;
    std::vector<std::string> offenders;

    std::function<void(const icf::svg::Element&)> census = [&](const icf::svg::Element& e) {
        ++elements[e.name];
        for (const auto& c : e.children) census(c);
    };

    for (const auto& f : files) {
        auto d = icf::svg::parseXml(readAll(f));
        if (!d) {
            if (offenders.size() < 10) offenders.push_back(f.filename().string());
            continue;
        }
        ++parsed;
        census(d->root);
    }

    std::printf("  %zu SVGs: %d parsed, %zu distinct element names\n",
                files.size(), parsed, elements.size());
    for (const auto& o : offenders) std::printf("    UNPARSED %s\n", o.c_str());

    // The ten most common, so a change in what the corpus holds is visible in
    // the log rather than only in a pass or a fail.
    std::vector<std::pair<int, std::string>> byCount;
    for (const auto& [name, n] : elements) byCount.emplace_back(n, name);
    std::sort(byCount.rbegin(), byCount.rend());
    for (size_t i = 0; i < byCount.size() && i < 10; ++i) {
        std::printf("    %-22s %d\n", byCount[i].second.c_str(), byCount[i].first);
    }

    CHECK_EQ(parsed, static_cast<int>(files.size()));
}

// THE DOCUMENT GATE.
//
// Not "does it crash" -- what does it SEE. Every file is read into geometry, and
// everything it declines is counted by name. The report is the scope of doc 04
// §4 restated as a number, and it is the input to deciding what to write next.
TEST_CASE(corpus_svg_document_gate) {
    auto files = corpusSvgs();
    REQUIRE(!files.empty());

    int read = 0, refused = 0, clean = 0;
    long long shapes = 0, segments = 0;
    std::map<std::string, int> declined;   // token -> files that hit it
    std::vector<std::string> offenders;

    for (const auto& f : files) {
        auto d = icf::svg::SvgDocument::parse(readAll(f));
        if (!d) {
            ++refused;
            if (offenders.size() < 10) offenders.push_back(f.filename().string());
            continue;
        }
        ++read;
        shapes += static_cast<long long>(d->shapes.size());
        for (const auto& s : d->shapes) segments += static_cast<long long>(s.path.segments.size());
        if (d->unsupported().empty()) ++clean;
        for (const auto& u : d->unsupported()) ++declined[u];
    }

    std::printf("  %zu SVGs: %d read, %d refused, %d fully understood\n",
                files.size(), read, refused, clean);
    std::printf("  %lld shapes, %lld segments\n", shapes, segments);
    for (const auto& o : offenders) std::printf("    REFUSED %s\n", o.c_str());

    std::vector<std::pair<int, std::string>> byCount;
    for (const auto& [name, n] : declined) byCount.emplace_back(n, name);
    std::sort(byCount.rbegin(), byCount.rend());
    for (const auto& [n, name] : byCount) {
        std::printf("    declined %-24s in %d file(s)\n", name.c_str(), n);
    }

    // Every corpus file declares a viewBox and is well-formed, so every one must
    // be readable. A refusal is a defect in this reader.
    CHECK_EQ(refused, 0);
    CHECK(shapes > 0);
}
