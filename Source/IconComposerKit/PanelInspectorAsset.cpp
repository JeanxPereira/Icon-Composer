// The Asset family of inspector sections: which file the layer draws, whether
// it is mirrored for right-to-left, and the one neighbour the format NAMES but
// never writes.
//
// TWO OF THE THREE ARE LIVE, AND THE REASON THE THIRD IS NOT
// -----------------------------------------------------------------------------
// `image-name` is the property of this family that the corpus actually writes
// (408 layers carry it, plus 29 specialization lists -- doc 01 §4) and that the
// renderer actually reads (`IconRenderer` resolves it under the render context).
// It is also the only thing in the whole editor that says which artwork a layer
// is, so it gets a real section: the current name, the files in `Assets/`, an
// import, and -- loudest of the four -- the case where the document names a file
// that is not there.
//
// TWO CONTROLS WERE GREY FOR ONE REASON, AND HALF OF THAT REASON FELL ON 19/09
// -----------------------------------------------------------------------------
// `asset-mirroring` and the layer's `material` were greyed together, and NOT
// because the renderer was behind: because nobody knew what to write. Both are
// sealed `CodingKey`s that occur ZERO times in the 145 documents, and two
// separate things were unknown about each:
//
//   1. The KEY's spelling on disk. The camelCase-to-kebab rule "prevê, não
//      decide" (doc 01 §1): the corpus already caught it being wrong once, where
//      `assumedSVGColorSpace` is written `color-space-for-untagged-svg-colors`.
//      For a key the corpus never uses, the derived name is a guess (§10.1).
//   2. The VALUE's grammar. §7 counts the observed shape of every key that
//      occurs; these had no row there, because they had no observations.
//
// `asset-mirroring` NOW HAS BOTH, and neither came from the corpus, which is
// still mute (0 of 145 -- laudo 19/09 §3.6). They came out of the binary
// (laudo 19/09 §3, every line `[BIN]`, with an address):
//
//   * the key is spelled `asset-mirroring` -- read off the `CodingKeys`
//     `rawValue` of the group snapshot (`0xBA35C`) and of the layer snapshot
//     (`0xC3474`), as an immediate, not derived from camelCase (§3.2);
//   * the value is an OBJECT, `IconComposition.AssetMirroring` (descriptor
//     `0x130ae0`), one member `mirrorable : Bool?`, encoded through a KEYED
//     container, and the member's default is `nil` -- the default initialiser
//     at `0x1A04` is `mov w0, #2 ; ret`, and 2 is the extra inhabitant of
//     `Optional<Bool>` (§3.3);
//   * it IS specializable at both levels, so the pair `asset-mirroring` +
//     `asset-mirroring-specializations` obeys the same invariant every other
//     specializable property does (§3.3);
//   * the semantics are `efetivo = mirrorable ?? herdado`, disassembled from
//     `effectiveIsMirrorable(inheritedValue:)` at `0xA5DE8`, and the root of the
//     chain is the document's `implicit-asset-mirroring` (§3.4).
//
// So the control below is a THREE-position one, because `Bool?` has three
// inhabitants, and each position writes a shape the binary says the decoder
// accepts.
//
// THE LAYER'S `material` STAYS GREY, AND THIS IS THE HALF THAT DID NOT FALL
// -----------------------------------------------------------------------------
// Nothing in the 19/09 laudo touched it. Its key name on disk is still a guess
// off the camelCase rule and its value has never been seen -- no `fieldmd` type
// was read for it, no `CodingKeys` `rawValue` was disassembled, and the corpus
// has no row for it either (doc 01 §4, §7). A control for it would write an
// unseen value under an unread key and `icon.json` would accept it in silence,
// which is exactly the failure this family is best placed to cause, since the
// key sits beside the two that work. It stays named, greyed, and carrying that
// reason in the tooltip -- a different statement from "not built yet".
#include "Source/IconComposerKit/InspectorSection.h"

#include <algorithm>
#include <filesystem>
#include <optional>
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

// ---- asset-mirroring --------------------------------------------------------
// THREE POSITIONS, NAMED BY WHAT THEY DO AND NOT BY WHAT THE TARGET CALLS THEM
//
// The target's own picker has three cases -- the Kit's `AssetMirroring.
// InspectorValue` is an enum of `inherited`, `fixed`, `mirror` (field metadata
// at `0x1D78E0`). Those names are read; the FUNCTION that maps them onto `Bool?`
// is internal to the Kit, not exported, and was not disassembled. The laudo says
// so in as many words: `inherited = nil, fixed = false, mirror = true` is the
// only assignment that closes with §3.3 and §3.4, but it is marked `[INF]`, not
// `[BIN]` (laudo 19/09 §3.5).
//
// That is why the three positions below are labelled by the VALUE and never by
// the target's case names. Swapping two of those names would produce a document
// that is perfectly valid and semantically inverted, and nothing here would
// catch it: `asset-mirroring` occurs 0 times in 145 documents (§3.6), so the
// corpus gate has nothing to compare against. A label that says "does not
// mirror" is true of `{"mirrorable": false}` whatever the target calls that
// case; a label that says "Fixed" is only true if the inference holds.
enum class Mirror { Inherited, Off, On };

Mirror readMirror(const icf::json::Value* v) {
    if (!v || v->kind() != icf::json::Value::Kind::Object) return Mirror::Inherited;
    const icf::json::Value* m = v->find("mirrorable");
    if (!m || m->kind() != icf::json::Value::Kind::Bool) return Mirror::Inherited;
    return m->boolean() ? Mirror::On : Mirror::Off;
}

// `[BIN]` the encoder is a KEYED container over one `Bool?` (§3.3), so `false`
// and "no member" are different documents with different meanings, and the
// control has to be able to write both.
//
// AND "INHERITED" REMOVES THE WHOLE PROPERTY, not just the member. `[INF]`,
// and the reasoning is a rejection of the other reading, so it is written
// down. Swift's synthesised `encodeIfPresent` would emit `{ }` for an
// `AssetMirroring` whose `mirrorable` is `nil` -- true of the ENCODER, and it
// says nothing about whether the target ever puts such an object on a node.
// Nothing decides it from outside either: `asset-mirroring` occurs ZERO times
// in the 145 documents (laudo 19/09 §3.6), so the corpus has no opinion.
//
// What decides it is what the two cost. `{ }` and an absent key resolve to
// the SAME effective value -- `readMirror` answers `Inherited` for both, and
// the fold below reaches past both -- so `{ }` is a member the document did
// not carry, carrying no information. On a node that never had the key,
// picking "Inherited" would ADD bytes that mean nothing, and the byte-exact
// round-trip gate would see it. Under a non-Base scope it is worse: an entry
// in the specialization list that says nothing.
//
// So: `Off`/`On` write the object; `Inherited` writes `nullopt`, which is the
// same door `setProperty` already uses for "this scope has no value of its
// own". If someone later reads a document where the target itself wrote
// `{ }`, `readMirror` already answers it correctly -- this is about what WE
// write, not about what we accept.
std::optional<icf::json::Value> mirrorToJson(Mirror m) {
    if (m == Mirror::Inherited) return std::nullopt;
    std::vector<icf::json::Value::Member> members;
    members.emplace_back("mirrorable", icf::json::Value::boolean(m == Mirror::On));
    return icf::json::Value::object(std::move(members));
}

// `efetivo = mirrorable ?? herdado`, with the document's `implicit-asset-mirroring`
// at the root of the chain (§3.4, `0xA5DE8`). `[BIN]` covers the fold and the
// root; `[INF]` is only the middle link -- that a LAYER's inherited value is its
// GROUP's rather than the document's directly. `Layer.effectiveIsMirrorable(for:)`
// and `Group.effectiveIsMirrorable(for:)` were both named but neither was
// disassembled far enough to show who hands the layer its `inheritedValue`.
bool effectiveMirroring(const Session& s, icf::NodePath path, icf::Context scope) {
    auto own = [&](icf::NodePath p) -> Mirror {
        const icf::json::Value* node = icf::nodeAt(s.root(), p);
        if (!node) return Mirror::Inherited;
        return readMirror(icf::resolve(*node, "asset-mirroring", scope));
    };
    // The root of the chain: a plain, non-specializable bool on the document.
    bool value = booleanOr(s.root().find("implicit-asset-mirroring"), false);
    if (path.group) {
        const Mirror g = own(icf::NodePath{path.group, std::nullopt});
        if (g != Mirror::Inherited) value = (g == Mirror::On);
    }
    if (path.layer) {
        const Mirror l = own(path);
        if (l != Mirror::Inherited) value = (l == Mirror::On);
    }
    return value;
}

void assetMirroring(Section& x) {
    PropertyView v;
    if (x.begin("Asset Mirroring", "asset-mirroring", v)) {
        const Mirror now = readMirror(v.value);
        Mirror next = now;

        // The labels say what the value DOES. See the note above for why they do
        // not say `inherited` / `fixed` / `mirror`.
        if (ImGui::RadioButton("Inherited", now == Mirror::Inherited)) next = Mirror::Inherited;
        ImGui::SetItemTooltip(
            "No decision of its own: the value in force comes from further up, with the "
            "document's Implicit Asset Mirroring at the root of the chain. Picking this REMOVES "
            "this node's entry; it does not write false, and it does not leave an empty object "
            "behind.");
        if (ImGui::RadioButton("Does not mirror", now == Mirror::Off)) next = Mirror::Off;
        ImGui::SetItemTooltip(
            "Writes {\"mirrorable\": false}: this node keeps its artwork as authored in a "
            "right-to-left language, whatever the document asks for.");
        if (ImGui::RadioButton("Mirrors", now == Mirror::On)) next = Mirror::On;
        ImGui::SetItemTooltip(
            "Writes {\"mirrorable\": true}: this node's asset is flipped for right-to-left "
            "languages.");

        if (next != now) {
            if (std::optional<icf::json::Value> value = mirrorToJson(next)) {
                x.write("asset-mirroring", std::move(*value), false);
            } else {
                x.erase("asset-mirroring");
            }
        }

        // The arithmetic of the chain, shown because the tri-state is the only
        // control in this panel whose displayed position does NOT tell you what
        // the icon does -- "Inherited" is an answer of "look somewhere else".
        ImGui::TextDisabled("in force here: %s",
                            effectiveMirroring(x.s, x.path, x.s.scope) ? "mirrors" : "does not mirror");
        ImGui::SetItemTooltip(
            "The fold is 'own value, or else the inherited one' (laudo 19/09 sec. 3.4), and the "
            "root of the chain is the document's Implicit Asset Mirroring. That a layer inherits "
            "from its GROUP rather than from the document directly is inference, not a reading.");

        // AND THE PART THAT WOULD OTHERWISE BE A LIE BY SILENCE. Every other
        // live control in this inspector moves a pixel; this one does not,
        // because nothing in the renderer reads mirroring -- grep
        // `Source/RenderBox` for it and there is no hit. The document it
        // writes is correct and the canvas is unchanged, and a person who
        // toggles a control and sees nothing is entitled to be told which of
        // the two it is.
        ImGui::TextDisabled("the canvas does not show this yet");
        ImGui::SetItemTooltip(
            "The document is written correctly and the renderer does not read mirroring at all "
            "yet, so nothing on the canvas changes. This says so rather than letting the control "
            "look broken.");
    }
    x.end();
}

}  // namespace

void drawAssetMirroringSection(Section& x) { assetMirroring(x); }

void drawLayerAssetSections(Section& x) {
    imageAsset(x);
    assetMirroring(x);

    // The tooltip is ASCII: the default ImGui font has no section sign, and a
    // reason that renders as a box is not a reason. Section numbers are spelled
    // out ("doc 01 sec. 4") on screen; the comments above keep the mark.
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
