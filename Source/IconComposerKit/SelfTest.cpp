#include "Source/IconComposerKit/SelfTest.h"

#include "Source/IconComposerKit/Headless.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Session.h"
#include "imgui.h"

#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

namespace ick {
namespace fs = std::filesystem;
namespace {

std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// The first layer of the first group that has one, or that group itself when the
// document is all empty groups. The script needs a node to edit, and a document
// whose first group is empty is still a document worth exercising.
icf::NodePath firstSelectable(const icf::json::Value& root) {
    const icf::json::Value* groups = root.find("groups");
    if (groups && groups->kind() == icf::json::Value::Kind::Array) {
        const auto& gs = groups->elements();
        for (std::size_t g = 0; g < gs.size(); ++g) {
            const icf::json::Value* layers = gs[g].find("layers");
            if (layers && layers->kind() == icf::json::Value::Kind::Array && !layers->elements().empty()) {
                return icf::NodePath{g, std::size_t{0}};
            }
        }
        if (!gs.empty()) return icf::NodePath{std::size_t{0}, std::nullopt};
    }
    return icf::NodePath{std::nullopt, std::nullopt};
}

struct Frame {
    LayersStats layers;
    InspectorStats inspector;
    CanvasStats canvas;
    DiagnosticsStats diagnostics;
};

// One whole frame of the editor, laid out the way the window lays it out. The
// coordinator ticks BEFORE the frame: it may create or replace the canvas
// texture, and the canvas draws whatever `view()` holds by then (spec 13/09 §6).
Frame frame(HeadlessImGui& gui, Session& s, RenderCoordinator& c) {
    Frame f;
    MenuActions actions;
    c.tick(s);
    gui.newFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(280, 900));
    f.layers = drawLayers(s);
    ImGui::SetNextWindowPos(ImVec2(280, 0));
    ImGui::SetNextWindowSize(ImVec2(840, 700));
    f.canvas = drawCanvas(s, c.view(), actions);
    ImGui::SetNextWindowPos(ImVec2(280, 700));
    ImGui::SetNextWindowSize(ImVec2(840, 200));
    f.diagnostics = drawDiagnostics(s, c.view());
    ImGui::SetNextWindowPos(ImVec2(1120, 0));
    ImGui::SetNextWindowSize(ImVec2(320, 900));
    f.inspector = drawInspector(s);
    gui.render();
    return f;
}

}  // namespace

SelfTestReport runSelfTest(const fs::path& bundleDir, RenderScheduler& scheduler, TextureSink& sink, int frames) {
    SelfTestReport r;
    // A scratch copy: the script saves, and the original is somebody's file.
    // `filename()` is empty on a path that ends in a separator, so normalise
    // first and keep a name that is never empty.
    fs::path name = bundleDir.lexically_normal().filename();
    if (name.empty()) name = "bundle";
    const fs::path work = fs::temp_directory_path() / ("ic-selftest-" + name.string());
    std::error_code ec;
    fs::remove_all(work, ec);
    ec.clear();
    fs::copy(bundleDir, work, fs::copy_options::recursive, ec);
    if (ec) {
        r.failure = "could not copy the bundle: " + ec.message();
        return r;
    }
    const std::string original = slurp(work / "icon.json");

    auto s = Session::open(work);
    if (!s) {
        r.failure = "not a bundle this reader can open";
        return r;
    }
    HeadlessImGui gui;
    RenderCoordinator coord(scheduler, sink);

    Frame f = frame(gui, *s, coord);
    r.frames = 1;
    r.groups = f.layers.groups;
    r.layers = f.layers.layers;

    // Select, and give the tree one frame to have reported the selection.
    const icf::NodePath target = firstSelectable(s->root());
    if (!target.group) {
        r.failure = "no node to select";
        r.imguiErrors = gui.errors();
        return r;
    }
    s->selection = target;
    f = frame(gui, *s, coord);
    ++r.frames;
    r.sections = f.inspector.sections;

    // Edit under a scope that is not Base, so the write goes through the
    // specialization list and the undo has to take it back out (spec 13/09 §4.3).
    s->setProperty(target, "opacity", icf::Context{icf::Appearance::Dark, icf::Idiom::Base},
                   icf::json::Value::number(0.5));
    f = frame(gui, *s, coord);
    ++r.frames;
    if (!s->undo()) {
        r.failure = "undo had nothing to undo";
        r.imguiErrors = gui.errors();
        return r;
    }
    f = frame(gui, *s, coord);
    ++r.frames;
    const std::string saved = s->save();
    if (!saved.empty()) {
        r.failure = "save: " + saved;
        r.imguiErrors = gui.errors();
        return r;
    }
    // The assertion of spec 13/09 §8: on a byte-exact document, open, edit, undo
    // and save gives the same bytes back.
    r.bytesRoundTripped = slurp(work / "icon.json") == original;

    // Settle: the render is answered on a later frame, so the canvas gets a few
    // to show it (spec 13/09 §6 -- the latest wins, and it arrives when it does).
    for (int i = 0; i < frames; ++i) {
        f = frame(gui, *s, coord);
        ++r.frames;
        if (f.canvas.textured) {
            r.textured = true;
            break;
        }
    }
    r.diagnostics = f.diagnostics.rows;
    r.imguiErrors = gui.errors();
    return r;
}

std::string describe(const SelfTestReport& r) {
    std::ostringstream o;
    o << "selftest: " << r.frames << " frame(s), " << r.groups << " group(s), " << r.layers << " layer(s), "
      << r.sections << " inspector section(s), " << r.diagnostics << " diagnostic row(s); "
      << "textured " << (r.textured ? "yes" : "no") << "; bytes round-tripped "
      << (r.bytesRoundTripped ? "yes" : "NO") << "; imgui errors " << r.imguiErrors;
    if (!r.failure.empty()) o << "; FAILED: " << r.failure;
    return o.str();
}

}  // namespace ick
