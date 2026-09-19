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

#include <cstdint>
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

    // ---- editing, since 2026-09-13 (spec 13/09 §4.2) ----
    json::Value& json() { return *tree_; }
    // A deep copy that shares nothing -- what a render job works on while the
    // UI keeps editing the original.
    IconBundle clone() const;
    // Atomic: written to a sibling temporary and renamed over `icon.json`, so a
    // crash mid-write leaves the old document, never half of the new one.
    // Empty on success, otherwise the reason -- this tower reports, it does not throw.
    std::string save() const;
    // Creates `dir/Assets`, copies every asset, writes the document, and this
    // bundle now IS `dir`.
    std::string saveAs(const std::filesystem::path& dir);
    // Copies `file` into `Assets/` under its own name and lists it.
    // No caller yet in this round: the spec (§4.2) mandates the operation here,
    // but the inspector UI that will invoke it lands in a later round.
    std::string importAsset(const std::filesystem::path& file);

    // The files in `Assets/`, sorted.
    const std::vector<std::string>& assetFiles() const { return assets_; }

    // A GERAÇÃO DOS BYTES EM `Assets/`: um inteiro que se move toda vez que um
    // arquivo de asset é escrito. Quem guarda algo LIDO de um asset -- o cache
    // de `viewBox` da `Session`, que o hit-test do canvas consulta -- compara
    // este número e joga fora o que guardou quando ele mexe.
    //
    // POR QUE UM CONTADOR AQUI, e não um aviso à Session nos chamadores de
    // `importAsset`: hoje há dois (`app/Window.cpp` e `PanelInspectorAsset.cpp`)
    // e NENHUM passa pela Session -- ela recebe só o `image-name` depois, como
    // uma edição comum. Consertar os dois deixaria de fora o terceiro que
    // aparecesse depois, e o sintoma seria mudo: um retângulo de seleção
    // calculado sobre a caixa da arte ANTIGA. `IconBundle` continua sem
    // conhecer a Session; é ela que pergunta.
    //
    // `assetFiles()` não responde isto: importar POR CIMA de um nome que já
    // está lá não mexe na lista, e é exatamente o caso que invalida a leitura.
    //
    // `saveAs` NÃO o move, de propósito: copiar os assets para outra pasta não
    // muda um byte de nenhum deles, e o que foi lido continua valendo.
    std::uint64_t assetsGeneration() const { return assetsGeneration_; }

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
    std::uint64_t assetsGeneration_ = 0;
};

}  // namespace icf
