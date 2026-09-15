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

namespace {
std::string writeAtomically(const fs::path& target, const std::string& bytes) {
    const fs::path tmp = target.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "could not open " + tmp.string() + " for writing";
        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!f) return "could not write " + tmp.string();
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return "could not replace " + target.string() + ": " + ec.message();
    }
    return {};
}
}  // namespace

IconBundle IconBundle::clone() const {
    return IconBundle(dir_, std::make_unique<json::Value>(*tree_), assets_);
}

std::string IconBundle::save() const {
    return writeAtomically(dir_ / "icon.json", json::write(*tree_));
}

std::string IconBundle::saveAs(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets", ec);
    if (ec) return "could not create " + (dir / "Assets").string() + ": " + ec.message();
    for (const auto& a : assets_) {
        fs::copy_file(dir_ / "Assets" / a, dir / "Assets" / a, fs::copy_options::overwrite_existing, ec);
        if (ec) return "could not copy " + a + ": " + ec.message();
    }
    const std::string wrote = writeAtomically(dir / "icon.json", json::write(*tree_));
    if (!wrote.empty()) return wrote;
    dir_ = dir;
    return {};
}

std::string IconBundle::importAsset(const fs::path& file) {
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return "not a file: " + file.string();
    const std::string name = file.filename().string();
    fs::create_directories(dir_ / "Assets", ec);
    fs::copy_file(file, dir_ / "Assets" / name, fs::copy_options::overwrite_existing, ec);
    if (ec) return "could not copy " + name + ": " + ec.message();
    if (std::find(assets_.begin(), assets_.end(), name) == assets_.end()) {
        assets_.push_back(name);
        std::sort(assets_.begin(), assets_.end());
    }
    return {};
}

}  // namespace icf
