#include "Source/IconComposerKit/Export.h"

#include "Source/IconComposerFoundation/Png.h"
#include "Source/IconComposerKit/ViewModel.h"
#include "Source/RenderBox/IconRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace ick {
namespace {

// O centro do último item submetido, em pixels de tela -- a mesma função que
// MenuBar.cpp e PanelLayers.cpp têm, pelo mesmo motivo: é o ponto em que um
// teste injeta o clique.
ImVec2 centreOfLastItem() {
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

// OS PREDICADOS QUE O DOCUMENTO ESCREVE, em qualquer profundidade.
//
// Uma especialização é um membro cuja chave termina em `-specializations` e
// cujo valor é um array de objetos (IconDocument.cpp, `resolve`). Eles ficam
// na raiz, nos grupos e nas camadas, então a varredura é recursiva e não uma
// lista de três lugares que envelhece quando o formato ganhar um quarto.
struct Predicates {
    bool appearance[4] = {};   // indexado por icf::Appearance
    bool idiom[5] = {};        // indexado por icf::Idiom
};

bool endsWith(const std::string& s, std::string_view tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

void scanEntry(const icf::json::Value& entry, Predicates& out) {
    if (entry.kind() != icf::json::Value::Kind::Object) return;
    if (const icf::json::Value* a = entry.find("appearance")) {
        if (a->kind() == icf::json::Value::Kind::String) {
            if (auto want = icf::appearanceFromString(a->rawString())) {
                out.appearance[static_cast<int>(*want)] = true;
            }
        }
    }
    if (const icf::json::Value* i = entry.find("idiom")) {
        if (i->kind() == icf::json::Value::Kind::String) {
            if (auto want = icf::idiomFromString(i->rawString())) {
                out.idiom[static_cast<int>(*want)] = true;
            }
        }
    }
}

void scan(const icf::json::Value& node, Predicates& out) {
    if (node.kind() == icf::json::Value::Kind::Array) {
        for (const auto& e : node.elements()) scan(e, out);
        return;
    }
    if (node.kind() != icf::json::Value::Kind::Object) return;
    for (const auto& m : node.members()) {
        if (endsWith(m.first, "-specializations") &&
            m.second.kind() == icf::json::Value::Kind::Array) {
            for (const auto& e : m.second.elements()) scanEntry(e, out);
        }
        scan(m.second, out);
    }
}

Predicates predicatesOf(const icf::json::Value& root) {
    Predicates p;
    scan(root, p);
    return p;
}

}  // namespace

std::vector<icf::Idiom> exportIdioms(const icf::json::Value& root) {
    const Predicates p = predicatesOf(root);
    std::vector<icf::Idiom> out;
    // O DECLARADO PRIMEIRO. É a composição que o documento diz que embarca
    // (ViewModel.h, `declaredIdiom`), e portanto a que uma pessoa que exporta
    // uma vez quer.
    out.push_back(declaredIdiom(root));
    for (auto i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                   icf::Idiom::WatchOS}) {
        if (!p.idiom[static_cast<int>(i)]) continue;
        if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
    }
    return out;
}

std::vector<icf::Appearance> exportAppearances(const icf::json::Value& root) {
    const Predicates p = predicatesOf(root);
    std::vector<icf::Appearance> out{icf::Appearance::Base};
    // `light` só quando o documento o distingue de `base` -- ver o cabeçalho.
    if (p.appearance[static_cast<int>(icf::Appearance::Light)]) {
        out.push_back(icf::Appearance::Light);
    }
    out.push_back(icf::Appearance::Dark);
    out.push_back(icf::Appearance::Tinted);
    return out;
}

std::vector<icf::Context> exportContexts(const icf::json::Value& root) {
    const std::vector<icf::Idiom> idioms = exportIdioms(root);
    const std::vector<icf::Appearance> appearances = exportAppearances(root);
    std::vector<icf::Context> out;
    out.reserve(idioms.size() * appearances.size());
    for (icf::Idiom i : idioms) {
        for (icf::Appearance a : appearances) out.push_back(icf::Context{a, i});
    }
    return out;
}

std::string exportContextLabel(icf::Context ctx) {
    return std::string(appearanceLabel(ctx.appearance)) + " / " + idiomLabel(ctx.idiom);
}

std::string exportFileName(std::string_view stem, icf::Context ctx, std::uint32_t size) {
    std::string out(stem);
    if (out.empty()) out = "icon";
    out += '-';
    out += icf::appearanceToString(ctx.appearance);
    out += '-';
    out += icf::idiomToString(ctx.idiom);
    out += '-';
    out += std::to_string(size);
    out += ".png";
    return out;
}

std::string exportStem(const Session& s) {
    std::filesystem::path name = s.bundle().path().filename();
    if (name.extension() == ".icon") name = name.stem();
    const std::string out = name.string();
    return out.empty() ? std::string("icon") : out;
}

ExportFile renderExportFile(rb::Device& device, const icf::IconBundle& bundle,
                            std::string_view stem, std::uint32_t size, icf::Context ctx) {
    ExportFile out;
    out.context = ctx;
    out.name = exportFileName(stem, ctx, size);
    if (size == 0) {
        out.error = "export: um tamanho de zero pixel nao e uma imagem";
        return out;
    }

    // AS CINCO LINHAS DO `IconComposerCli`, e nada a mais. Ver o cabeçalho de
    // Export.h: o canvas inteiro (sem `viewport`), `subdivisions` no padrão, e
    // os floats direto para o encoder.
    rb::IconRenderOptions io;
    io.size = size;
    io.context = ctx;
    auto icon = rb::renderIcon(device, bundle, io);
    if (!icon) {
        out.error = icon.error();
        return out;
    }
    out.drawn = icon->drawn;
    out.total = icon->total;
    out.notes = icon->notes;
    for (const auto& s : icon->skipped) {
        out.notes.push_back("grupo " + std::to_string(s.group) + " / " + s.layer + ": " + s.why);
    }
    for (const auto& g : icon->shapeGaps) out.notes.push_back(g);
    out.png = icf::encodePng(icon->rgba, icon->width, icon->height);
    if (out.png.empty()) out.error = "export: o encoder nao produziu bytes";
    return out;
}

ExportSheetStats drawExportSheet(Session& s, MenuActions& a) {
    ExportSheetStats st;
    ExportSheetState& sheet = s.exportSheet;
    if (!sheet.open) return st;

    if (!ImGui::IsPopupOpen(kExportSheetTitle)) ImGui::OpenPopup(kExportSheetTitle);
    // No centro do que existe. `Appearing` e não todo quadro: um modal que se
    // recentrasse a cada quadro não poderia ser arrastado para o lado.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
        ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal(kExportSheetTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return st;
    }
    st.open = true;

    const std::string stem = exportStem(s);
    const std::vector<icf::Context> options = exportContexts(s.root());
    st.options = options.size();
    // A LISTA MUDOU DE TAMANHO: recomeça com tudo marcado. Acontece na
    // primeira abertura e quando o documento ganha ou perde uma
    // especialização enquanto o modal está fechado -- e nos dois casos "tudo"
    // é a resposta certa, porque a lista já é só o que o documento distingue.
    if (sheet.chosen.size() != options.size()) {
        sheet.chosen.assign(options.size(), 1);
    }

    ImGui::TextUnformatted(stem.c_str());
    ImGui::Separator();

    // ---- o tamanho ---------------------------------------------------------
    // ENQUANTO UMA EXPORTAÇÃO CORRE, as escolhas ficam cinzas: a fila do app
    // já foi montada com o plano que estava marcado no clique, e uma caixa
    // que continuasse respondendo mostraria uma escolha que não é a que está
    // sendo gravada.
    ImGui::BeginDisabled(sheet.busy);

    ImGui::TextUnformatted("Size");
    for (std::uint32_t size : kExportSizes) {
        char label[32];
        std::snprintf(label, sizeof label, "%u px", size);
        ImGui::SameLine();
        if (ImGui::RadioButton(label, sheet.size == size)) sheet.size = size;
        st.controls.push_back(MenuItemInfo{label, !sheet.busy, centreOfLastItem()});
    }
    st.size = sheet.size;

    ImGui::Separator();
    ImGui::TextUnformatted("Contexts");

    // ---- um contexto por caixa, com o nome que ele vai escrever ------------
    std::size_t chosen = 0;
    for (std::size_t i = 0; i < options.size(); ++i) {
        const std::string label = exportContextLabel(options[i]);
        bool on = sheet.chosen[i] != 0;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Checkbox(label.c_str(), &on)) sheet.chosen[i] = on ? 1 : 0;
        st.controls.push_back(MenuItemInfo{label, !sheet.busy, centreOfLastItem()});
        ImGui::PopID();
        const std::string name = exportFileName(stem, options[i], sheet.size);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", name.c_str());
        if (sheet.chosen[i]) {
            ++chosen;
            st.names.push_back(name);
        }
    }
    st.chosen = chosen;
    ImGui::EndDisabled();

    ImGui::Separator();
    // A FRASE DO APP. É aqui que a exportação deixa de ser muda: o app escreve
    // "Exporting 2 of 4: ..." antes de cada render e o resumo no fim, e este é
    // o único lugar em que isso está na tela enquanto o modal cobre o resto.
    if (!sheet.status.empty()) {
        ImGui::TextUnformatted(sheet.status.c_str());
        st.status = sheet.status;
        ImGui::Separator();
    }

    // ---- os dois botões ----------------------------------------------------
    const bool canExport = chosen > 0 && !sheet.busy;
    ImGui::BeginDisabled(!canExport);
    const bool pressed = ImGui::Button("Export");
    ImGui::EndDisabled();
    st.exportAt = centreOfLastItem();
    st.controls.push_back(MenuItemInfo{"Export", canExport, st.exportAt});
    if (pressed && canExport) {
        a.exportImage = true;
        a.exportPlan.size = sheet.size;
        a.exportPlan.contexts.clear();
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (sheet.chosen[i]) a.exportPlan.contexts.push_back(options[i]);
        }
        // O app sobrescreve assim que sabe o que aconteceu com o diálogo de
        // pasta; até lá a frase diz que o pedido saiu, e não nada.
        sheet.busy = true;
        sheet.status = "Choosing a folder…";
    }

    ImGui::SameLine();
    // FECHAR NÃO CANCELA, e o rótulo não promete que cancela. A fila é do app
    // e já está andando; um botão "Cancel" que deixasse os PNGs restantes
    // aparecendo na pasta depois seria pior do que este. E o que a exportação
    // está fazendo continua na tela depois daqui: com o modal fechado, a barra
    // do canvas desenha `sheet.status` (PanelCanvas.cpp).
    bool close = ImGui::Button("Close");
    st.closeAt = centreOfLastItem();
    st.controls.push_back(MenuItemInfo{"Close", true, st.closeAt});

    // ESCAPE FECHA TAMBÉM, e o motivo pelo qual não fechava não é o que a
    // revisão de 19/09 supôs (M2).
    //
    // O laudo atribuiu isso ao `OpenPopup` por quadro da linha 181 -- "qualquer
    // fechamento que não passe pelo botão Close é desfeito no quadro seguinte".
    // Medido no imgui desta árvore, não é: `NavUpdateCancelRequest`
    // (imgui.cpp:15039) fecha popup e menu e EXCLUI explicitamente
    // `ImGuiWindowFlags_Modal`, e nem chegaria lá sem
    // `ImGuiConfigFlags_NavEnableKeyboard`, que este processo não liga. ImGui
    // nunca fechou este modal; não havia fechamento a ser desfeito.
    //
    // Então o Escape é nosso, e é aqui: um modal cuja única saída é acertar um
    // botão de 50 px é uma armadilha para quem abriu por engano. `IsKeyPressed`
    // com repeat desligado, e só com o modal em foco -- um modal tem o foco
    // exclusivo, mas a condição diz isso em vez de supor.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        close = true;
    }
    if (close) {
        sheet.open = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
    return st;
}

}  // namespace ick
