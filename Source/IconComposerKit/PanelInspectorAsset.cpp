// The Asset family of inspector sections: which file the layer draws, and the
// two neighbours the format NAMES but never writes.
//
// ONE OF THE THREE IS LIVE, AND THE REASON THE OTHER TWO ARE NOT
// -----------------------------------------------------------------------------
// `image-name` is the property of this family that the corpus actually writes
// (408 layers carry it, plus 29 specialization lists -- doc 01 §4) and that the
// renderer actually reads (`IconRenderer` resolves it under the render context).
// It is also the only thing in the whole editor that says which artwork a layer
// is, so it gets a real section: the current name, the files in `Assets/`, an
// import, and -- loudest of the four -- the case where the document names a file
// that is not there.
//
// `material` on the layer and `asset-mirroring` stay greyed, and NOT for the
// usual reason. It is not that the renderer is behind: it is that nobody knows
// what to write. Both are sealed `CodingKey`s that occur ZERO times in the 145
// documents (verified against the corpus, and doc 01 §3, §4, §9). Two separate
// things are therefore unknown about each of them:
//
//   1. The KEY's spelling on disk. The camelCase-to-kebab rule "prevê, não
//      decide" (doc 01 §1): the corpus already caught it being wrong once, where
//      `assumedSVGColorSpace` is written `color-space-for-untagged-svg-colors`.
//      For a key the corpus never uses, the derived name is a guess (§10.1).
//   2. The VALUE's grammar. §7 counts the observed shape of every key that
//      occurs; these two have no row there, because they have no observations.
//
// A control for either would write an unseen value under an unread key, and the
// document would take it without a word -- the exact failure this family is best
// placed to cause, since both keys sit beside the one property that works. So
// they are named, greyed, and carry that reason in the tooltip, which is a
// different statement from "not built yet".
#include "Source/IconComposerKit/InspectorSection.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ick {
namespace {

// The path typed into the import field. It outlives the frame that started it
// and the Session stores no UI state, so it lives here -- the same place and for
// the same reason as PanelLayers' rename buffer. One editor window edits one
// document.
char g_importPath[512] = {};
std::string g_importError;

// A warm red for the two things this panel has to say out loud: a reference with
// no file behind it, and an import that did not happen.
const ImVec4 kAlarm(0.95f, 0.45f, 0.35f, 1.0f);

std::string_view stringOr(const icf::json::Value* v, std::string_view fallback) {
    if (!v || v->kind() != icf::json::Value::Kind::String) return fallback;
    return v->rawString();
}

void imageAsset(Section& x) {
    PropertyView v;
    if (x.begin("Image Asset", "image-name", v)) {
        // The list is `Assets/` as it is on disk, which is also what
        // `importAsset` keeps sorted -- so an import shows up in the combo on the
        // very next frame without anyone rescanning the folder.
        const std::vector<std::string>& files = x.s.bundle().assetFiles();
        const std::string name(stringOr(v.value, ""));
        const bool present =
            !name.empty() && std::find(files.begin(), files.end(), name) != files.end();

        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::BeginCombo("File", name.empty() ? "(none)" : name.c_str())) {
            if (files.empty()) ImGui::TextDisabled("Assets/ is empty");
            for (const std::string& f : files) {
                if (ImGui::Selectable(f.c_str(), f == name)) {
                    // A file name is written as the raw bytes between the quotes,
                    // which is what `addLayer` already does with the name it is
                    // handed (Edit.cpp): no name that came out of a directory
                    // listing can carry a quote or a backslash.
                    x.write("image-name", icf::json::Value::string(f), false);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("The file in Assets/ this layer draws, under the scope above");

        // Three silences, and they are three different facts:
        //   empty        -- a layer just added, naming no file yet (Edit.cpp
        //                   gives a fresh layer an empty `image-name`);
        //   named, there -- the ordinary case, and it says nothing;
        //   named, gone  -- the document points at a file `Assets/` does not
        //                   have. THIS one the editor must not swallow. The
        //                   corpus carries two of them, and a layer that quietly
        //                   renders nothing teaches the author that the layer is
        //                   wrong when it is the reference that is.
        if (name.empty()) {
            ImGui::TextDisabled("no image: this layer draws nothing");
        } else if (!present) {
            ImGui::TextColored(kAlarm, "missing from Assets/: %s", name.c_str());
            ImGui::SetItemTooltip(
                "The document names this file and Assets/ does not have it, so the layer renders "
                "nothing. Import the file below, or pick another from the list.");
        }

        ImGui::Separator();
        // DUAS PORTAS, E ANTES DE 19/09 HAVIA UMA SO.
        //
        // O Kit continua sem poder abrir dialogo: `Ports.h` lhe da duas portas
        // para o app, um sorvedouro de textura e um agendador de render, e
        // nenhuma das duas e um dialogo (Regra 2 -- so `Source/app` linka
        // Onyx). O que mudou nao foi a regra, foi o canal: `MenuActions` ja
        // levava `open`, `save` e `saveAs` do Kit ate o app, e o comentario
        // que estava aqui dizia que esse canal "era do menu". Nao e -- e do
        // Kit. O botao abaixo escreve um pedido nele e o app abre o dialogo
        // depois do frame, exatamente como faz com o `File > Open`.
        //
        // E o CAMPO DIGITADO FICA. Ele funciona, colar um caminho e mais
        // rapido do que navegar ate ele, e um caminho digitado e a unica porta
        // que sobra se o dialogo nativo falhar (o laudo de 18/09 tem um caso
        // desses, com o COM do seletor de pasta).
        ImGui::TextDisabled("Import into Assets/");
        ImGui::SetNextItemWidth(220.0f);
        // Typing clears the last failure. The buffer and the error are file
        // statics -- one editor window, one document -- so without this the
        // message from a path typed against ANOTHER layer stays on screen under
        // the next one, red and wrong, until an import happens to succeed.
        if (ImGui::InputTextWithHint("##import-path", "full path to an .svg or .png file",
                                     g_importPath, sizeof g_importPath)) {
            g_importError.clear();
        }
        ImGui::SetItemTooltip(
            "A full path to an .svg or .png. Pasting one is often faster than walking to it, and "
            "this field is also the way through if the native dialog ever refuses to open.");
        ImGui::SameLine();
        ImGui::BeginDisabled(g_importPath[0] == '\0');
        if (ImGui::Button("Import")) {
            const std::filesystem::path file(g_importPath);
            // Copying a file into `Assets/` is not an edit of the DOCUMENT, so it
            // carries no command; naming it IS one, and that goes through the
            // Session like everything else. Undo therefore takes back the
            // reference and leaves the copy where it is, which is not a leak:
            // `unusedAssets()` names it in the Diagnostics panel.
            g_importError = x.s.bundle().importAsset(file);
            if (g_importError.empty()) {
                x.write("image-name", icf::json::Value::string(file.filename().string()), false);
                g_importPath[0] = '\0';
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Copies the file into Assets/ and points this layer at it");

        // O BOTAO QUE PEDE O DIALOGO. Ele nao importa nada: escreve o pedido
        // em `MenuActions` e o app o executa depois do frame (Panels.h). O
        // pedido leva o NO e o ESCOPO consigo -- ver a nota em `MenuActions`:
        // `act()` roda depois, e "a camada selecionada" pode ja ser outra.
        ImGui::SameLine();
        const bool browse = ImGui::Button("Browse…");
        // Onde o botao ficou, para quem precisa clicar nele de fora
        // (Panels.h, `InspectorStats::importBrowseAt`).
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        x.st.importBrowseAt = ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        ImGui::SetItemTooltip(
            "Opens a file chooser. The Kit cannot open one itself (it links no toolkit), so the "
            "request goes to the app, which opens the dialog after this frame and then copies the "
            "file into Assets/ and points THIS layer at it.");
        if (browse) {
            x.menu.importAsset = true;
            x.menu.importInto = x.path;
            x.menu.importScope = x.s.scope;
            // Um pedido novo apaga a queixa do anterior: a mensagem vermelha
            // abaixo e do caminho DIGITADO, e deixa-la sob um dialogo que
            // acabou de ser pedido e a mesma mentira que digitar ja apagava.
            g_importError.clear();
        }

        if (!g_importError.empty()) ImGui::TextColored(kAlarm, "%s", g_importError.c_str());
    }
    x.end();
}

}  // namespace

void drawLayerAssetSections(Section& x) {
    imageAsset(x);

    // The tooltips are ASCII: the default ImGui font has no section sign, and a
    // reason that renders as a box is not a reason. Section numbers are spelled
    // out ("doc 01 sec. 4") on screen; the comments above keep the mark.
    x.disabled("Asset Mirroring",
               "Nothing to write yet, and the gap is the FORMAT's, not the renderer's.\n"
               "'asset-mirroring' is a sealed CodingKey that occurs 0 times in the 145 documents "
               "(doc 01 sec. 3, sec. 4), so both its spelling on disk and the shape of its value "
               "are unobserved -- the derived key name is a guess (doc 01 sec. 10.1), and the "
               "value grammar in sec. 7 has no row for it. Nothing consumes it either. A control "
               "here would write an invented value under an invented key, and icon.json would "
               "accept it in silence.");

    x.disabled("Material",
               "The layer's 'material' occurs 0 times in the 145 documents (doc 01 sec. 4): its "
               "key name on disk is a guess and its value has never been seen, exactly as for "
               "Asset Mirroring above.\n"
               "The material the renderer DOES consume is the group's 'blur-material' (123 "
               "documents, read by GlassMaterial) -- a different key on a different node, so it "
               "belongs to the group's inspector, not this one. How the two relate is still open "
               "(doc 01 sec. 10.3), and guessing it here would write the wrong key on the wrong "
               "node.");
}

}  // namespace ick
