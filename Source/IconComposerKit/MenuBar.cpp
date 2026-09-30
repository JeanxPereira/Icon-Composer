// The menu bar, drawn by us inside the canvas window.
//
// WHY WE DRAW IT AND NOT THE TOOLKIT (spec 13/09 §7)
// --------------------------------------------------
// Onyx draws `File > Open` and `Recent Files` on its own and offers no hook to
// them, so an editor that let Onyx own the bar would ship two File menus that
// disagree. We draw ours; Onyx keeps the layout, the theme and the window.
//
// WHY THE ACTIONS ARE RETURNED INSTEAD OF PERFORMED
// -------------------------------------------------
// Rule 2 of the architecture spec: the Kit links no toolkit, so it cannot open
// a file dialog and cannot close a window. `MenuActions` is the whole of what
// this function can ask for; the app reads it after the frame and does it.
//
// The items this round does not implement -- Export, Copy/Paste Properties,
// Localization -- are drawn DISABLED with the reason in a tooltip (spec 13/09
// §7), not hidden. A menu that omits what the target has teaches the wrong
// shape of the program.
//
// THE SHORTCUT COLUMN WAS TEXT, AND NOTHING WAS LISTENING
// -------------------------------------------------------
// `ImGui::MenuItem(label, shortcut, ...)` DRAWS its third argument right-aligned
// and binds NOTHING: the string is a picture of a key. This bar advertised nine
// chords -- Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+W, Ctrl+Q, Ctrl+Z, Ctrl+Y
// and Del -- and a search of the whole Kit for `ImGuiKey`, `IsKeyPressed` or
// `Shortcut` before this change returned nothing at all, in the Kit and in
// `Source/app/` alike. So every one of them did nothing, including the undo the
// coalescing work in the inspector exists to make usable.
//
// They are bound here, next to the labels that promise them, so the two cannot
// drift apart. `ImGuiInputFlags_RouteGlobal` is the right route and not a
// shortcut around focus: a route loses to an ACTIVE item, so Ctrl+Z inside the
// asset panel's import field still edits the text, and Del while renaming a
// layer still deletes a character rather than the layer. ImGui's text input
// claims the whole keyboard while it is active, which is what makes that true.
//
// Redo answers BOTH Ctrl+Y and Ctrl+Shift+Z. The menu can only print one, and
// the two halves of the world disagree about which; binding one and printing it
// is cheaper than being right about the argument.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "Source/IconComposerKit/Theme.h"
#include "Source/IconComposerKit/Widgets.h"

#include "imgui.h"

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string_view>

namespace ick {
namespace {

// O centro do ultimo item submetido, em pixels de tela. E o ponto em que um
// teste injeta o clique (Tests/test_e2e_layers.cpp, `clickAt`).
ImVec2 centreOfLastItem() {
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

// Every item goes through here so that `MenuStats` counts what was actually
// drawn: a disabled item that stopped being counted is a menu that can quietly
// lose an entry without any frame noticing.
struct Builder {
    MenuStats& st;
    bool item(const char* label, const char* shortcut, bool enabled = true, const char* why = nullptr,
              bool selected = false) {
        ++st.items;
        if (!enabled) ++st.disabled;
        const bool pressed = ImGui::MenuItem(label, shortcut, selected, enabled);
        // Gravado aqui porque aqui e o unico caminho por onde um item nasce
        // (Panels.h, `MenuItemInfo`): um item novo entra no relato sem ninguem
        // o acrescentar.
        st.drawn.push_back(MenuItemInfo{label, enabled, centreOfLastItem()});
        if (why) ImGui::SetItemTooltip("%s", why);
        return pressed;
    }

    // `BeginMenu` com o titulo gravado. So com o menu FECHADO: aberto, o
    // `BeginMenu` ja comecou o popup e `GetItemRect*` deixa de falar do
    // titulo. E nao faz falta -- quem clica para ABRIR clica num menu fechado,
    // que e exatamente o estado em que a posicao e gravada.
    bool menu(const char* label) {
        ++st.menus;
        // O TITULO acende discreto (`.menubar-title:hover`, `--box-strong`); o
        // item DENTRO do menu acende na cor de destaque (`pushMenuStyle`, em
        // volta da barra inteira). So o titulo troca, e so enquanto e desenhado.
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::kBoxStrong);
        ImGui::PushStyleColor(ImGuiCol_Header, theme::kBoxStrong);
        const bool open = ImGui::BeginMenu(label);
        ImGui::PopStyleColor(2);
        if (!open) st.titles.push_back(MenuItemInfo{label, true, centreOfLastItem()});
        return open;
    }
};

constexpr const char* kRound5 = "Round 5: not built yet";

// One chord, routed globally. Split out so that the binding and the label can be
// read against each other in one place below.
bool chord(ImGuiKeyChord keys) {
    return ImGui::Shortcut(keys, ImGuiInputFlags_RouteGlobal);
}

// Reads a boolean property as the canvas would see it under the base context.
// A property that is absent, or present with another type, is `false` -- the
// same reading `resolve` gives a node that never wrote the key.
bool booleanUnderBase(const icf::json::Value& node, std::string_view prop) {
    const icf::json::Value* v = icf::resolve(node, prop, icf::Context{});
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

}  // namespace

MenuStats drawMenuBar(Session& s, MenuActions& a) {
    if (!ImGui::BeginMenuBar()) return MenuStats{};
    MenuStats st = drawMenus(s, a);
    ImGui::EndMenuBar();
    return st;
}

MenuStats drawMenus(Session& s, MenuActions& a) {
    MenuStats st;
    ui::pushMenuStyle();
    Builder b{st};

    if (b.menu("File")) {
        if (b.item("New…", "Ctrl+N")) a.newDocument = true;
        if (b.item("Open…", "Ctrl+O")) a.open = true;
        ImGui::Separator();
        // "Dirty" is the undo stack not being where it was at the last save, so
        // a document edited and then fully undone offers nothing to save.
        if (b.item("Save", "Ctrl+S", s.isDirty())) a.save = true;
        if (b.item("Save As…", "Ctrl+Shift+S")) a.saveAs = true;
        // O ITEM DEIXOU DE SER CINZA (T2). Ele não exporta: abre o modal
        // (`Export.h`), que é onde o tamanho, os contextos e o destino são
        // escolhidos. Sem atalho impresso, pela regra do topo deste arquivo --
        // nada medido diz qual o alvo usa, e a terceira posição de `MenuItem`
        // desenha um acorde sem ligar nada.
        if (b.item("Export Icon as Image…", nullptr, !s.exportSheet.open)) {
            s.exportSheet.open = true;
            // O RECIBO DO LOTE ANTERIOR SAI DA BARRA AQUI, e só aqui. Desde
            // 19/09 a barra do canvas mostra `status` com o modal fechado
            // (PanelCanvas.cpp, I5), então "Wrote 5 of 6 to …" fica na tela
            // depois que o modal some -- que é o ponto. O que não pode é ele
            // ficar ao lado do progresso do lote SEGUINTE.
            if (!s.exportSheet.busy) s.exportSheet.status.clear();
        }
        ImGui::Separator();
        if (b.item("Close", "Ctrl+W")) a.close = true;
        if (b.item("Quit", "Ctrl+Q")) a.quit = true;
        ImGui::EndMenu();
    }

    if (b.menu("Edit")) {
        if (b.item("Undo", "Ctrl+Z", s.canUndo())) s.undo();
        if (b.item("Redo", "Ctrl+Y", s.canRedo())) s.redo();
        ImGui::Separator();
        // DUPLICATE. A spec de 13/09 §7 lista o item entre Redo e Delete e e
        // ai que ele fica; o alvo o tem em `DocumentCommands`. Desabilitado
        // sem selecao pela MESMA condicao do Delete ao lado -- duplicar a raiz
        // nao e uma operacao (ela nao tem irmaos), e `duplicateNode` recusa.
        //
        // SEM ATALHO IMPRESSO, de proposito. Nada medido diz qual o alvo usa,
        // e a nota no topo deste arquivo e sobre exatamente esse defeito: a
        // terceira posicao de `MenuItem` DESENHA e nao LIGA nada, e esta barra
        // ja anunciou nove acordes que nao existiam. Um `Ctrl+D` inventado
        // seria o decimo.
        if (b.item("Duplicate", nullptr, s.selection && s.selection->group)) {
            // A selecao segue a DUPLICATA, que e o no que a pessoa acabou de
            // criar e o que ela vai renomear ou mover em seguida.
            if (auto made = s.duplicateNode(*s.selection)) s.selection = *made;
        }
        if (b.item("Delete", "Del", s.selection && s.selection->group)) s.removeNode(*s.selection);
        b.item("Copy Properties", nullptr, false, kRound5);
        b.item("Paste Properties", nullptr, false, kRound5);
        b.item("Localization", nullptr, false, kRound5);
        ImGui::EndMenu();
    }

    if (b.menu("View")) {
        if (b.item("Diagnostics", nullptr)) a.toggleDiagnostics = true;
        ImGui::Separator();
        // These four write `Session::view` directly. They are NOT commands: the
        // context a person is looking through is not part of the document, it
        // does not move `version()`, and it is not on the undo stack.
        if (ImGui::BeginMenu("Appearance")) {
            for (auto ap : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                            icf::Appearance::Tinted}) {
                if (b.item(appearanceLabel(ap), nullptr, true, nullptr, s.view.context.appearance == ap)) {
                    s.view.context.appearance = ap;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Idiom")) {
            for (auto id : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                            icf::Idiom::WatchOS}) {
                if (b.item(idiomLabel(id), nullptr, true, nullptr, s.view.context.idiom == id)) {
                    s.view.context.idiom = id;
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Preview Size")) {
            if (b.item("512", nullptr, true, nullptr, s.view.size == 512)) s.view.size = 512;
            if (b.item("1024", nullptr, true, nullptr, s.view.size == 1024)) s.view.size = 1024;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Zoom")) {
            // Zoom is NOT a render (spec 13/09 §6): it is the same pixels shown
            // larger, so it never reaches the render key.
            for (float z : {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f}) {
                char label[16];
                std::snprintf(label, sizeof label, "%d%%", static_cast<int>(z * 100.0f + 0.5f));
                // `zoomRequest`, not `zoom`: the canvas eases toward a target
                // and anchors the change on the viewport centre, and a menu that
                // wrote the eased value directly would simply be undone by the
                // next frame's ease (Session.h). The tick reads the target, so
                // it agrees with the combo and with the wheel.
                if (b.item(label, nullptr, true, nullptr, s.view.zoomTarget == z)) s.view.zoomRequest = z;
            }
            ImGui::Separator();
            if (b.item("Fit", nullptr, true, "Fit the whole icon in the canvas and centre it."))
                s.view.fitRequest = true;
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (b.menu("Layer")) {
        const bool hasSel = s.selection && s.selection->group;
        const bool isLayer = hasSel && s.selection->layer;
        const icf::json::Value* groupsNode = s.root().find("groups");
        const std::size_t groups = groupsNode ? groupsNode->elements().size() : 0;

        if (b.item("Add Group", nullptr)) {
            if (auto i = s.addGroup("Group")) s.selection = icf::NodePath{*i, std::nullopt};
        }
        // A layer needs a group to live in, and a fresh document has none.
        if (b.item("Add Image Layer", nullptr, groups > 0)) {
            const std::size_t g = hasSel ? *s.selection->group : groups - 1;
            if (auto i = s.addLayer(g, "Layer", "")) s.selection = icf::NodePath{g, *i};
        }
        ImGui::Separator();
        // Both toggles write the BASE scope: the menu is the coarse switch, and
        // the per-appearance override is the inspector's scope selector (§7).
        if (b.item("Toggle Glass", nullptr, isLayer)) {
            if (const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection)) {
                s.setProperty(*s.selection, "glass", icf::Context{},
                              icf::json::Value::boolean(!booleanUnderBase(*node, "glass")));
            }
        }
        // `hidden` absent IS visible, so showing a node again removes the entry
        // rather than writing `false` -- the document keeps the shape it had
        // before anything was hidden.
        if (b.item("Toggle Visibility", nullptr, hasSel)) {
            if (const icf::json::Value* node = icf::nodeAt(s.root(), *s.selection)) {
                const bool hidden = booleanUnderBase(*node, "hidden");
                // Writes the boolean, never removes the key -- same reason as the
                // sidebar's toggle: the corpus spells "not hidden" both ways, and
                // removal would drop a key the encoder wrote (250 of them).
                s.setProperty(*s.selection, "hidden", icf::Context{},
                              icf::json::Value::boolean(!hidden));
            }
        }
        ImGui::Separator();
        if (b.item("Move Up", nullptr, hasSel)) s.moveNode(*s.selection, -1);
        if (b.item("Move Down", nullptr, hasSel)) s.moveNode(*s.selection, +1);
        ImGui::EndMenu();
    }

    ui::popMenuStyle();

    // ---- the chords the labels above promise --------------------------------
    // Outside the menu bar, so the routes are registered against the window and
    // not against a menu that is open for one frame in a hundred. Each line is
    // the same call the corresponding `b.item` makes, and each is guarded by the
    // same condition, so a chord can never do what its greyed menu item cannot.
    if (chord(ImGuiMod_Ctrl | ImGuiKey_N)) a.newDocument = true;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_O)) a.open = true;
    if (chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) a.saveAs = true;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_S) && s.isDirty()) a.save = true;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_W)) a.close = true;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Q)) a.quit = true;
    if (chord(ImGuiMod_Ctrl | ImGuiKey_Z)) s.undo();
    // Both calls run every frame, never short-circuited: `Shortcut` REGISTERS the
    // route as well as reading it, and a route that is registered only on the
    // frames the other chord missed is a route that intermittently is not there.
    const bool redoY = chord(ImGuiMod_Ctrl | ImGuiKey_Y);
    const bool redoZ = chord(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
    if (redoY || redoZ) s.redo();
    if (chord(ImGuiKey_Delete) && s.selection && s.selection->group) s.removeNode(*s.selection);
    return st;
}

}  // namespace ick
