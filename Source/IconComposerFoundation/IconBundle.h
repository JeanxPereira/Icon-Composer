#pragma once
// A `.icon` on disk.
//
// The document is only half of it: a `.icon` is a FOLDER holding `icon.json`
// beside a flat `Assets/`, and the layers name files in there. Measured over 45
// public bundles, all 45 have exactly that shape and NONE has a subdirectory
// under `Assets/` (doc 02).
//
// The tree lives behind a pointer so that moving a bundle does not move the
// storage its views borrow -- an `IconDocument` is a view, and a view into a
// moved-from object is the classic way this shape goes wrong.
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace icf {

class IconBundle {
public:
    // `dir` is the `.icon` folder itself. Fails when it is not a directory, when
    // `icon.json` is absent, unreadable or not a `.icon` document.
    static std::optional<IconBundle> open(const std::filesystem::path& dir);

    const std::filesystem::path& path() const { return dir_; }
    const json::Value& json() const { return *tree_; }
    IconDocument document() const { return *IconDocument::open(*tree_); }

    // The files in `Assets/`, sorted.
    const std::vector<std::string>& assetFiles() const { return assets_; }

    // Every image the icon can actually display: `image-name` resolved through
    // every (appearance, idiom) context, deduped, in the order first reached.
    std::vector<std::string> referencedImageNames() const;

    // Named by the document, absent from `Assets/`.
    std::vector<std::string> missingAssets() const;
    // Present in `Assets/`, named by nothing.
    std::vector<std::string> unusedAssets() const;

    std::filesystem::path assetPath(std::string_view imageName) const;

private:
    IconBundle(std::filesystem::path dir, std::unique_ptr<json::Value> tree,
               std::vector<std::string> assets)
        : dir_(std::move(dir)), tree_(std::move(tree)), assets_(std::move(assets)) {}

    std::filesystem::path dir_;
    std::unique_ptr<json::Value> tree_;
    std::vector<std::string> assets_;
};

}  // namespace icf
