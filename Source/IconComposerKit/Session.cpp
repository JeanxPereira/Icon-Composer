#include "Source/IconComposerKit/Session.h"

#include "Source/IconComposerKit/ViewModel.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace ick {
namespace fs = std::filesystem;

std::optional<Session> Session::open(const fs::path& dir) {
    auto b = icf::IconBundle::open(dir);
    if (!b) return std::nullopt;
    Session s(std::move(*b));
    // THE CANVAS OPENS WHERE THE DOCUMENT SAYS IT SHIPS (ViewModel.h).
    //
    // Only the VIEW moves, never the document and never `scope`: the inspector
    // still edits Base by default, so nothing here turns an opened file into an
    // idiom-specialized one. This is which composition a person is looking
    // THROUGH, and `ViewContext` is exactly the place that is not undoable.
    s.view.context.idiom = declaredIdiom(s.root());
    return s;
}

std::optional<Session> Session::create(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / "Assets", ec);
    if (ec) return std::nullopt;
    std::ofstream f(dir / "icon.json", std::ios::binary | std::ios::trunc);
    if (!f) return std::nullopt;
    f << "{\n  \"fill\" : \"automatic\",\n  \"groups\" : [\n\n  ]\n}";
    f.close();
    return open(dir);
}

// A caixa de uma arte, do disco. Nada aqui é um segundo leitor de XML:
// `icf::svg::readViewBox` é a mesma regra que `SvgDocument::parse` usa, e ler
// um `.png` por este caminho simplesmente não encontra um `<svg>` e devolve
// nada, que é o que "não tenho caixa a afirmar" quer dizer.
static std::optional<icf::svg::ViewBox> readBoxOf(const fs::path& art) {
    std::error_code ec;
    if (!fs::is_regular_file(art, ec)) return std::nullopt;
    std::ifstream f(art, std::ios::binary);
    if (!f) return std::nullopt;
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return icf::svg::readViewBox(bytes);
}

const icf::svg::ViewBox* Session::assetViewBox(std::string_view imageName) const {
    if (imageName.empty()) return nullptr;
    // A INVALIDAÇÃO, e é uma comparação de inteiro -- não um `stat` por
    // consulta, que devolveria o custo por quadro que o cache existe para
    // tirar. Ver IconBundle.h, `assetsGeneration`.
    const std::uint64_t gen = bundle_.assetsGeneration();
    if (gen != assetBoxesGeneration_) {
        assetBoxes_.clear();
        assetBoxesGeneration_ = gen;
    }
    auto it = assetBoxes_.find(imageName);
    if (it == assetBoxes_.end()) {
        ++assetBoxReads_;
        it = assetBoxes_.emplace(std::string(imageName),
                                 readBoxOf(bundle_.assetPath(imageName))).first;
    }
    return it->second ? &*it->second : nullptr;
}

template <class F>
bool Session::apply(icf::NodePath target, std::string key, F&& edit) {
    icf::json::Value* node = icf::nodeAt(root(), target);
    if (!node) return false;
    Command c{target, *node, *node, std::move(key)};
    const bool result = edit(*node);
    c.after = *node;
    if (icf::json::write(c.before) == icf::json::write(c.after)) return result;  // a no-op is not a command
    push(std::move(c));
    return result;
}

void Session::push(Command c) {
    redo_.clear();
    // Undone past the last save and then edited: that clean state is now on the
    // dropped redo branch, so no depth of this stack is clean again.
    if (cleanDepth_ > undo_.size()) cleanDepth_ = std::numeric_limits<std::size_t>::max();
    if (!c.key.empty() && c.key == coalesceKey_ && !undo_.empty() && undo_.back().key == c.key) {
        undo_.back().after = std::move(c.after);   // fold into the drag's command
    } else {
        coalesceKey_ = c.key;
        undo_.push_back(std::move(c));
    }
    ++version_;
}

void Session::setProperty(icf::NodePath path, std::string_view prop, icf::Context scope,
                          std::optional<icf::json::Value> value, bool coalesce) {
    std::string key;
    if (coalesce) {
        key = (path.group ? std::to_string(*path.group) : "r") + "/" +
              (path.layer ? std::to_string(*path.layer) : "-") + "/" + std::string(prop) + "/" +
              std::to_string(static_cast<int>(scope.appearance)) + "/" +
              std::to_string(static_cast<int>(scope.idiom));
    } else {
        coalesceKey_.clear();
    }
    apply(path, std::move(key), [&](icf::json::Value& node) {
        icf::setProperty(node, prop, scope, std::move(value));
        return true;
    });
}

std::optional<std::size_t> Session::addGroup(std::string name) {
    coalesceKey_.clear();
    std::optional<std::size_t> index;
    apply(icf::NodePath{}, "", [&](icf::json::Value& rootNode) {
        index = icf::addGroup(rootNode, std::move(name));
        return true;
    });
    return index;
}

std::optional<std::size_t> Session::addLayer(std::size_t group, std::string name, std::string imageName) {
    coalesceKey_.clear();
    std::optional<std::size_t> index;
    apply(icf::NodePath{group, std::nullopt}, "", [&](icf::json::Value& g) {
        index = icf::addLayer(g, std::move(name), std::move(imageName));
        return true;
    });
    return index;
}

bool Session::removeNode(icf::NodePath path) {
    coalesceKey_.clear();
    if (!path.group) return false;
    const icf::NodePath parent = path.layer ? icf::NodePath{path.group, std::nullopt} : icf::NodePath{};
    // The snapshot is the PARENT's; the edit is `icf::removeNode` on the root, which
    // is the one spelling of this operation. Two spellings are two places to be wrong.
    const bool ok = apply(parent, "", [&](icf::json::Value&) { return icf::removeNode(root(), path); });
    if (ok) {
        reindexSelectionAfterRemove(path);
        dropSelectionIfGone();
    }
    return ok;
}

std::optional<icf::NodePath> Session::duplicateNode(icf::NodePath path) {
    coalesceKey_.clear();
    if (!path.group) return std::nullopt;   // a raiz nao tem irmaos
    const icf::NodePath parent = path.layer ? icf::NodePath{path.group, std::nullopt} : icf::NodePath{};
    // O instantaneo e do PAI e a edicao e `icf::duplicateNode` sobre a raiz --
    // a mesma forma de `removeNode` logo acima, e pela mesma razao: duas
    // grafias da operacao seriam dois lugares para errar.
    const bool ok = apply(parent, "", [&](icf::json::Value&) { return icf::duplicateNode(root(), path); });
    if (!ok) return std::nullopt;
    reindexSelectionAfterInsert(path);
    return path.layer ? icf::NodePath{path.group, *path.layer + 1}
                      : icf::NodePath{*path.group + 1, std::nullopt};
}

bool Session::moveNode(icf::NodePath path, int delta, bool coalesce) {
    if (!path.group) return false;
    const icf::NodePath parent = path.layer ? icf::NodePath{path.group, std::nullopt} : icf::NodePath{};
    // The key names the PARENT, not the node: the node's index is exactly what
    // each swap changes, so keying on it would fold nothing. The parent's
    // snapshot is what the command already holds, and folding a run of swaps
    // under one parent gives a single before/after of that parent -- which is
    // precisely "the drag, undone".
    std::string key;
    if (coalesce) {
        key = "move/" + (parent.group ? std::to_string(*parent.group) : std::string("r"));
    } else {
        coalesceKey_.clear();
    }
    const bool ok =
        apply(parent, std::move(key), [&](icf::json::Value&) { return icf::moveNode(root(), path, delta); });
    if (ok) reindexSelectionAfterMove(path, delta);
    return ok;
}

bool Session::rename(icf::NodePath path, std::string name) {
    coalesceKey_.clear();
    return apply(path, "", [&](icf::json::Value&) { return icf::setName(root(), path, std::move(name)); });
}

bool Session::undo() {
    if (undo_.empty()) return false;
    coalesceKey_.clear();
    Command c = std::move(undo_.back());
    undo_.pop_back();
    if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.before;
    redo_.push_back(std::move(c));
    ++version_;
    dropSelectionIfGone();
    return true;
}

bool Session::redo() {
    if (redo_.empty()) return false;
    coalesceKey_.clear();
    Command c = std::move(redo_.back());
    redo_.pop_back();
    if (icf::json::Value* node = icf::nodeAt(root(), c.target)) *node = c.after;
    undo_.push_back(std::move(c));
    ++version_;
    dropSelectionIfGone();
    return true;
}

std::string Session::save() {
    const std::string r = bundle_.save();
    if (r.empty()) cleanDepth_ = undo_.size();
    return r;
}

std::string Session::saveAs(const fs::path& dir) {
    const std::string r = bundle_.saveAs(dir);
    if (r.empty()) cleanDepth_ = undo_.size();
    return r;
}

void Session::dropSelectionIfGone() {
    if (selection && !icf::nodeAt(root(), *selection)) selection.reset();
}

// `icf::moveNode` SWAPS two siblings one position apart, so the selection follows
// whichever of the two it named -- the node itself, or a layer inside a group that
// moved. Called only when the move succeeded, so both indices are in range.
void Session::reindexSelectionAfterMove(icf::NodePath path, int delta) {
    if (!selection || !path.group) return;
    if (path.layer) {
        if (selection->group != path.group || !selection->layer) return;
        const std::size_t a = *path.layer;
        const std::size_t b = delta < 0 ? a - 1 : a + 1;
        if (*selection->layer == a) selection->layer = b;
        else if (*selection->layer == b) selection->layer = a;
    } else {
        const std::size_t a = *path.group;
        const std::size_t b = delta < 0 ? a - 1 : a + 1;
        if (selection->group == a) selection->group = b;
        else if (selection->group == b) selection->group = a;
    }
}

// Removing a sibling shifts every later one down by one. A selection naming one of
// those has to shift with it, or it silently comes to mean the next node down; a
// selection at or under what was removed is gone.
// O espelho exato de `reindexSelectionAfterRemove`: a duplicata entra em
// `index + 1`, entao todo irmao a partir dali desce um. Uma selecao no
// ORIGINAL nao se move -- ele continua onde estava.
void Session::reindexSelectionAfterInsert(icf::NodePath path) {
    if (!selection || !path.group) return;
    if (path.layer) {
        if (selection->group != path.group || !selection->layer) return;
        if (*selection->layer > *path.layer) selection->layer = *selection->layer + 1;
    } else {
        if (selection->group > path.group) selection->group = *selection->group + 1;
    }
}

void Session::reindexSelectionAfterRemove(icf::NodePath path) {
    if (!selection || !path.group) return;
    if (path.layer) {
        if (selection->group != path.group || !selection->layer) return;
        if (*selection->layer == *path.layer) selection.reset();
        else if (*selection->layer > *path.layer) selection->layer = *selection->layer - 1;
    } else {
        if (selection->group == path.group) selection.reset();   // the group, or a layer of it
        else if (selection->group > path.group) selection->group = *selection->group - 1;
    }
}

}  // namespace ick
