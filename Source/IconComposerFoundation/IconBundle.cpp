#include "Source/IconComposerFoundation/IconBundle.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace icf {
namespace {

namespace fs = std::filesystem;

// Every context an icon can be asked for. Four appearances by five idioms: the
// grid the resolver is defined over (doc 01 §5).
constexpr Appearance kAppearances[] = {
    Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted,
};
constexpr Idiom kIdioms[] = {
    Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS,
};

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

}  // namespace

std::optional<IconBundle> IconBundle::open(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return std::nullopt;
    const fs::path document = dir / "icon.json";
    if (!fs::is_regular_file(document, ec)) return std::nullopt;

    auto parsed = json::parse(readAll(document));
    if (!parsed) return std::nullopt;
    // A folder with an `icon.json` that is not a `.icon` document is not a
    // bundle. Opening it anyway would hand the caller a view that answers every
    // question with silence.
    if (!IconDocument::open(*parsed)) return std::nullopt;

    std::vector<std::string> assets;
    // `Assets/` is flat in all 45 bundles measured, so this does not recurse. A
    // bundle that nested its assets would come back with the folder missing from
    // the listing rather than with a wrong path.
    for (const auto& e : fs::directory_iterator(dir / "Assets", ec)) {
        if (e.is_regular_file()) assets.push_back(e.path().filename().string());
    }
    std::sort(assets.begin(), assets.end());

    return IconBundle(dir, std::make_unique<json::Value>(std::move(*parsed)), std::move(assets));
}

std::vector<std::string> IconBundle::referencedImageNames() const {
    std::vector<std::string> out;
    auto doc = IconDocument::open(*tree_);
    if (!doc) return out;
    for (const auto& g : doc->groups()) {
        for (const auto& l : g.layers()) {
            for (auto a : kAppearances) {
                for (auto i : kIdioms) {
                    const json::Value* v = l.resolve("image-name", {a, i});
                    if (!v || v->kind() != json::Value::Kind::String) continue;
                    const std::string& name = v->rawString();
                    if (std::find(out.begin(), out.end(), name) == out.end()) out.push_back(name);
                }
            }
        }
    }
    return out;
}

std::vector<std::string> IconBundle::missingAssets() const {
    std::vector<std::string> out;
    for (const auto& name : referencedImageNames()) {
        if (std::find(assets_.begin(), assets_.end(), name) == assets_.end()) out.push_back(name);
    }
    return out;
}

std::vector<std::string> IconBundle::unusedAssets() const {
    const auto referenced = referencedImageNames();
    std::vector<std::string> out;
    for (const auto& file : assets_) {
        if (std::find(referenced.begin(), referenced.end(), file) == referenced.end()) {
            out.push_back(file);
        }
    }
    return out;
}

fs::path IconBundle::assetPath(std::string_view imageName) const {
    return dir_ / "Assets" / fs::path(std::string(imageName));
}

}  // namespace icf
