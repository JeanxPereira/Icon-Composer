#pragma once
// One open `.icon`, and everything the UI does to it.
//
// UNDO IS A SNAPSHOT OF THE NODE, NOT AN INVERSE OPERATION (spec 13/09 §5)
// -------------------------------------------------------------------------
// A command records the path of the node it touched and that node's JSON
// before and after. Undo is assignment. The largest corpus document is under
// 200 KB, so a node copy costs nothing worth an inverse-operation bug. A
// structural command (add, remove, move) snapshots the PARENT.
//
// Coalescing: while a control is being dragged, consecutive edits to the same
// (path, property, scope) fold into one command -- what the target's
// `DocumentCommands` does with its coalesced undo. `endCoalescing` is the drag
// ending; the panels call it on `IsItemDeactivatedAfterEdit`.
//
// TWO INVARIANTS THE REST OF THE EDITOR RESTS ON
// -------------------------------------------------------------------------
// 1. Every mutation of the document goes through a command. A write that goes
//    around them is an undo that lies about what it restores.
// 2. `version()` moves on every one of them, including undo and redo. The
//    canvas re-renders on that number (spec 13/09 §6); a mutation that does not
//    move it is a canvas showing a document that is no longer the one in memory.
// `root()` and `bundle()` hand out mutable references because `IconBundle`'s own
// operations (`importAsset`, `saveAs`) need them. Reading through them is
// ordinary; WRITING to the tree through them is the one way to break both
// invariants at once, and no caller in this editor does it.
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ick {

// WHAT A PERSON IS LOOKING THROUGH -- INCLUDING WHERE THEY ARE LOOKING.
//
// `zoom` used to be the only thing here and `pan` was a `static ImVec2` inside
// PanelCanvas.cpp. A file-static is not "no state", it is ONE state shared by
// every document the process ever opens: closing a document you had dragged to
// the corner and opening another one put the new one in that same corner, with
// nothing on screen to explain it. Pan is a property of looking, exactly like
// zoom, so it lives exactly where zoom lives -- and a new `Session` therefore
// starts centred and unfitted, which is the whole of the fix.
//
// The pair `x`/`xTarget`: a control (wheel, drag, button, combo) writes the
// TARGET, and the canvas eases the current value toward it once per frame. That
// is what makes the movement smooth instead of teleporting. Both are kept
// because the anchored zoom has to do its arithmetic against the target -- two
// wheel clicks in one ease must compose, not fight.
//
// Plain floats rather than `ImVec2`: Session.h is the Kit's model header and
// nothing else in it needs Dear ImGui.
struct ViewContext {
    icf::Context context;         // appearance and idiom the canvas renders
    std::uint32_t size = 512;     // the preview size, in pixels

    float zoom = 1.0f;            // the magnification ON SCREEN this frame
    float zoomTarget = 1.0f;      // where the ease is heading
    // The icon's top-left corner, in pixels from the canvas's own top-left.
    float panX = 0.0f, panY = 0.0f;
    float panTargetX = 0.0f, panTargetY = 0.0f;

    // False until the canvas has laid the document out to Fit. It is false in a
    // freshly opened Session and nowhere else, so "Fit on open" needs no event.
    bool fitted = false;

    // A control outside the canvas -- the zoom combo, the View>Zoom menu -- asks
    // for a magnification by writing this, and the canvas applies it anchored on
    // the viewport centre and clears it. It cannot write `zoomTarget` itself:
    // the anchored zoom needs the PREVIOUS target to know how to move the pan,
    // and a control that overwrote it would zoom about the canvas's top-left
    // corner, which is not where anybody is looking.
    float zoomRequest = 0.0f;     // > 0 = pending
    bool fitRequest = false;
};

class Session {
public:
    static std::optional<Session> open(const std::filesystem::path& bundleDir);
    // Writes `{"fill":"automatic","groups":[]}` and an empty Assets/, then opens it.
    static std::optional<Session> create(const std::filesystem::path& bundleDir);

    icf::IconBundle& bundle() { return bundle_; }
    const icf::IconBundle& bundle() const { return bundle_; }
    icf::json::Value& root() { return bundle_.json(); }
    const icf::json::Value& root() const { return bundle_.json(); }
    // Bumps on every edit, undo and redo -- what the render coordinator watches.
    std::uint64_t version() const { return version_; }

    void setProperty(icf::NodePath path, std::string_view prop, icf::Context scope,
                     std::optional<icf::json::Value> value, bool coalesce = false);
    void endCoalescing() { coalesceKey_.clear(); }
    std::optional<std::size_t> addGroup(std::string name);
    std::optional<std::size_t> addLayer(std::size_t group, std::string name, std::string imageName);
    bool removeNode(icf::NodePath path);
    bool moveNode(icf::NodePath path, int delta);
    bool rename(icf::NodePath path, std::string name);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    // "The stack is not where it was at the last save."
    bool isDirty() const { return undo_.size() != cleanDepth_; }

    std::string save();
    std::string saveAs(const std::filesystem::path& dir);

    std::optional<icf::NodePath> selection;
    icf::Context scope;
    ViewContext view;

private:
    struct Command {
        icf::NodePath target;
        icf::json::Value before;
        icf::json::Value after;
        std::string key;   // non-empty while coalescing
    };
    explicit Session(icf::IconBundle b) : bundle_(std::move(b)) {}
    // Snapshots the node at `target`, runs `edit`, snapshots again, and records a
    // command when the two differ. Returns what `edit` returned; nothing is
    // recorded when the node does not exist or when the edit changed no byte.
    // `edit` takes the target node and returns bool; a structural edit ignores
    // that argument and calls the `icf::` function on `root()` instead -- the
    // parent snapshot captures the change either way, and no pointer moves
    // (erasing inside `groups[g].layers` does not move `groups[g]`).
    template <class F>
    bool apply(icf::NodePath target, std::string key, F&& edit);   // edit: bool(json::Value&)
    void push(Command c);
    void dropSelectionIfGone();
    // The selection names a node by INDEX, so a structural edit that renumbers
    // siblings has to renumber it too, or it silently points at another node.
    void reindexSelectionAfterMove(icf::NodePath path, int delta);
    void reindexSelectionAfterRemove(icf::NodePath path);

    icf::IconBundle bundle_;
    std::vector<Command> undo_;
    std::vector<Command> redo_;
    std::size_t cleanDepth_ = 0;
    std::string coalesceKey_;
    std::uint64_t version_ = 1;
};

}  // namespace ick
