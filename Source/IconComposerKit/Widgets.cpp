#include "Source/IconComposerKit/Widgets.h"

#include "Source/IconComposerKit/Theme.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ick {
namespace {
SymbolSource* g_symbols = nullptr;
ArtSource* g_art = nullptr;
}

void setArtSource(ArtSource* source) { g_art = source; }
ArtSource* artSource() { return g_art; }

void setSymbolSource(SymbolSource* source) { g_symbols = source; }
SymbolSource* symbolSource() { return g_symbols; }

namespace ui {

float dpi() {
    const float k = ImGui::GetStyle().FontScaleDpi;
    return k > 0.0f ? k : 1.0f;
}

namespace {

// O simbolo numa draw list DADA. Quem desenha depois de abrir um popup (a
// linha de um submenu) ja nao esta na janela da linha, e a draw list corrente
// e a do filho.
bool symbolOn(ImDrawList* dl, std::string_view name, ImVec2 c, float size, ImU32 colour, bool custom) {
    if (!g_symbols) return false;
    const ImTextureID tex = g_symbols->symbol(name, custom);
    if (tex == ImTextureID_Invalid || tex == 0) return false;
    const float h = size * 0.5f;
    dl->AddImage(tex, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0), ImVec2(1, 1), colour);
    return true;
}

}  // namespace

bool symbol(std::string_view name, ImVec2 c, float size, ImU32 colour, bool custom) {
    return symbolOn(ImGui::GetWindowDrawList(), name, c, size, colour, custom);
}

namespace {

void fallbackText(ImDrawList* dl, ImVec2 c, ImU32 col, const char* text) {
    if (!text || !*text) return;
    const ImVec2 ts = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, text);
}

}  // namespace

bool plainButton(const char* id, std::string_view sym, ImVec2 box, float symSize, const char* fallback,
                 bool enabled, const char* tooltip) {
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, box);
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    ImVec4 col = hovered ? theme::kText : theme::kText2;
    if (!enabled) col = theme::kText4;
    if (!symbol(sym, c, symSize * dpi(), theme::u32(col)))
        fallbackText(ImGui::GetWindowDrawList(), c, theme::u32(col), fallback);
    if (tooltip && enabled) ImGui::SetItemTooltip("%s", tooltip);
    return clicked && enabled;
}

bool roundButton(const char* id, std::string_view sym, float symSize, const char* tooltip) {
    const float k = dpi();
    const float d = 34.0f * k;
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(d, d));
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 c(a.x + d * 0.5f, a.y + d * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // `--control-shadow`: meio pt de sombra embaixo e o anel fino.
    dl->AddCircleFilled(ImVec2(c.x, c.y + 0.5f * k), d * 0.5f + 0.5f * k, IM_COL32(0, 0, 0, 90), 40);
    dl->AddCircleFilled(c, d * 0.5f, theme::u32(held ? theme::kCapOn : hovered ? theme::kBoxStrong : theme::kControl), 40);
    dl->AddCircle(c, d * 0.5f, IM_COL32(255, 255, 255, 20), 40, 1.0f);
    if (!symbol(sym, c, symSize * k, theme::u32(theme::kText)))
        fallbackText(dl, c, theme::u32(theme::kText), "=");
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked;
}

void beginCapsule(const char* id, float width) {
    const float k = dpi();
    const float h = 34.0f * k;
    ImGui::PushID(id);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + width * k, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(a.x, a.y + 0.5f * k), ImVec2(b.x, b.y + 0.5f * k), IM_COL32(0, 0, 0, 90), h * 0.5f);
    dl->AddRectFilled(a, b, theme::u32(theme::kControl), h * 0.5f);
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 20), h * 0.5f, 0, 1.0f);
    // O primeiro botao entra a 3 pt da borda e centrado na altura (28 de 34).
    ImGui::SetCursorScreenPos(ImVec2(a.x + 3.0f * k, a.y + 3.0f * k));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##capsule-end"), b.x);
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##capsule-top"), a.y);
}

void endCapsule() {
    const float k = dpi();
    const float endX = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##capsule-end"), 0.0f);
    const float top = ImGui::GetStateStorage()->GetFloat(ImGui::GetID("##capsule-top"), 0.0f);
    ImGui::PopStyleVar();
    ImGui::EndGroup();
    ImGui::PopID();
    // A proxima capsula no MESMO topo, 8 pt depois (`gap` do `.toolbar`). O
    // ImGui recusa um cursor movido para alem do que ja foi submetido sem um
    // item depois; um item vazio o leva junto.
    ImGui::SetCursorScreenPos(ImVec2(endX + 8.0f * k, top));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::SetCursorScreenPos(ImVec2(endX + 8.0f * k, top));
}

namespace {

// O proximo botao da capsula, encostado neste e NO MESMO TOPO, posto a mao.
// Um `SameLine` aqui volta para a linha que o ImGui guardou -- e dentro de uma
// barra de menus essa e a linha de base da barra, nao a do botao que a capsula
// posicionou: o primeiro botao caia no lugar e os outros desciam.
void nextInCapsule(ImVec2 a, ImVec2 b) { ImGui::SetCursorScreenPos(ImVec2(b.x, a.y)); }

}  // namespace

bool capButton(const char* id, std::string_view sym, bool on, float symSize, bool custom, const char* tooltip,
               float width, const char* fallback) {
    const float k = dpi();
    const ImVec2 size(width * k, 28.0f * k);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) dl->AddRectFilled(a, b, theme::u32(theme::kCapOn), 14.0f * k);
    else if (hovered) dl->AddRectFilled(a, b, theme::u32(theme::kBox), 14.0f * k);
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    if (sym.empty() || !symbol(sym, c, symSize * k, theme::u32(theme::kText), custom))
        fallbackText(dl, c, theme::u32(theme::kText), fallback);
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    nextInCapsule(a, b);
    return clicked;
}

bool capText(const char* id, const char* label, float width, const char* tooltip) {
    const float k = dpi();
    const ImVec2 size(width * k - 6.0f * k, 28.0f * k);
    const bool clicked = ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(a, b, theme::u32(theme::kBox), 14.0f * k);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float chevron = 9.0f * k;
    const float total = ts.x + 5.0f * k + chevron;
    const float x = (a.x + b.x - total) * 0.5f;
    dl->AddText(ImVec2(x, (a.y + b.y - ts.y) * 0.5f), theme::u32(theme::kText), label);
    const ImVec2 cc(x + ts.x + 5.0f * k + chevron * 0.5f, (a.y + b.y) * 0.5f);
    if (!symbol("chevron.down", cc, chevron, theme::u32(theme::kText2))) {
        dl->AddTriangleFilled(ImVec2(cc.x - 3 * k, cc.y - 1.5f * k), ImVec2(cc.x + 3 * k, cc.y - 1.5f * k),
                              ImVec2(cc.x, cc.y + 2 * k), theme::u32(theme::kText2));
    }
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    nextInCapsule(a, b);
    return clicked;
}

bool menuItem(const char* label, const char* shortcut, bool selected, bool enabled) {
    const float k = dpi();
    const char* hash = std::strstr(label, "##");
    const char* end = hash ? hash : label + std::strlen(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float gutter = 22.0f * k;   // a coluna da marca, a esquerda como no sistema
    const float sw = shortcut ? ImGui::CalcTextSize(shortcut).x + 28.0f * k : 0.0f;
    const float w = std::max({ImGui::GetContentRegionAvail().x, gutter + ts.x + sw + 12.0f * k, 96.0f * k});
    const float h = 24.0f * k;
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + w, a.y + h);

    // O `Selectable` e so o comportamento (o id do rotulo, o clique que fecha
    // o popup): o realce quadrado e o texto dele ficam invisiveis, e a linha e
    // desenhada aqui com o canto do sistema.
    const ImVec4 none(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Header, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
    ImGui::PushStyleColor(ImGuiCol_Text, none);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 1.0f * k));
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(w, h));
    const bool hovered = enabled && ImGui::IsItemHovered();
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(a, b, theme::u32(theme::kAccent), 5.0f * k);
    const ImVec4 col = !enabled ? theme::kText3 : hovered ? theme::kAccentText : theme::kText;
    const float mid = (a.y + b.y) * 0.5f;
    if (selected) {
        const ImVec2 c(a.x + gutter * 0.5f, mid);
        if (!symbol("checkmark", c, 11.0f * k, theme::u32(col))) {
            const ImVec2 pts[3] = {ImVec2(c.x - 4 * k, c.y), ImVec2(c.x - 1 * k, c.y + 3 * k),
                                   ImVec2(c.x + 4 * k, c.y - 4 * k)};
            dl->AddPolyline(pts, 3, theme::u32(col), 0, 1.5f * k);
        }
    }
    dl->AddText(ImVec2(a.x + gutter, mid - ts.y * 0.5f), theme::u32(col), label, end);
    if (shortcut) {
        const ImVec2 ss = ImGui::CalcTextSize(shortcut);
        const ImVec4 sc = !enabled ? theme::kText3 : hovered ? theme::kAccentText : theme::kText2;
        dl->AddText(ImVec2(b.x - 10.0f * k - ss.x, mid - ss.y * 0.5f), theme::u32(sc), shortcut);
    }
    return clicked && enabled;
}

bool beginMenu(const char* label, bool enabled) {
    const float k = dpi();
    const char* hash = std::strstr(label, "##");
    const char* end = hash ? hash : label + std::strlen(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float gutter = 22.0f * k;    // a mesma coluna da marca de `menuItem`
    const float arrow = 28.0f * k;     // a coluna da seta, a do atalho
    const float h = 24.0f * k;
    const float w = std::max({ImGui::GetContentRegionAvail().x, gutter + ts.x + arrow, 96.0f * k});
    const ImGuiStyle& style = ImGui::GetStyle();
    // A draw list e a posicao da LINHA, guardadas antes: com o submenu aberto o
    // `BeginMenu` volta ja dentro da janela do filho.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + w, a.y + h);

    // A largura que a linha pede ao popup. O `BeginMenu` so declara a do rotulo
    // dele, que aqui e vazio; um item sem altura a declara sem descer o cursor.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::Dummy(ImVec2(w, 0.0f));
    ImGui::PopStyleVar();

    // O `BeginMenu` e so o comportamento -- abrir no hover, fechar ao sair, o
    // triangulo ate o filho, o teclado. O realce, o texto e a seta dele ficam
    // invisiveis e a linha e desenhada aqui, como em `menuItem`.
    //
    // A ALTURA: o `Selectable` de dentro tem a altura de uma linha de TEXTO do
    // rotulo. Com o rotulo escondido atras de `##` e a fonte em 24 pt so nesta
    // chamada, essa linha tem os 24 pt de `menuItem`, comeca no cursor e o
    // popup do filho nasce com a primeira linha na altura desta. Esticar com
    // `ItemSpacing` no lugar disso desce a linha e o filho meio espacamento.
    char id[160];
    std::snprintf(id, sizeof id, "##%s", label);
    const ImVec4 none(0, 0, 0, 0);
    ImGui::PushStyleColor(ImGuiCol_Header, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, none);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, none);
    ImGui::PushStyleColor(ImGuiCol_Text, none);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.12f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, 1.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * k, 5.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f * k);
    // O filho de um menu e uma janela-filha: a borda dele vem de
    // `ChildBorderSize`, que o tema zera. A do popup, para ficar igual ao pai.
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, style.PopupBorderSize);
    ImGui::PushFont(nullptr, 24.0f);
    const bool open = ImGui::BeginMenu(id, enabled);
    ImGui::PopFont();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(5);
    // O `BeginMenu` devolve o "ultimo item" a linha, aberto ou nao.
    const bool lit = enabled && (open || ImGui::IsItemHovered());

    if (lit) dl->AddRectFilled(a, b, theme::u32(theme::kAccent), 5.0f * k);
    const ImVec4 col = !enabled ? theme::kText3 : lit ? theme::kAccentText : theme::kText;
    const float mid = (a.y + b.y) * 0.5f;
    dl->AddText(ImVec2(a.x + gutter, mid - ts.y * 0.5f), theme::u32(col), label, end);
    const ImVec2 c(b.x - 12.0f * k, mid);
    if (!symbolOn(dl, "chevron.right", c, 9.0f * k, theme::u32(col), false)) {
        dl->AddTriangleFilled(ImVec2(c.x - 2 * k, c.y - 4 * k), ImVec2(c.x - 2 * k, c.y + 4 * k),
                              ImVec2(c.x + 3 * k, c.y), theme::u32(col));
    }
    return open;
}

void endMenu() { ImGui::EndMenu(); }

void pushMenuStyle() {
    const float k = dpi();
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f * k);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * k, 5.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * k, 2.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme::kAccent);
    ImGui::PushStyleColor(ImGuiCol_Header, theme::kBoxStrong);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme::kPopover);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.12f));
}

void popMenuStyle() {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
}

// ---- a caixa de secao e as linhas dela --------------------------------------
namespace {

// Uma caixa por vez: as secoes do inspetor nao se aninham.
struct BoxState {
    bool active = false;
    int rows = 0;
    float x0 = 0.0f, x1 = 0.0f, top = 0.0f;
};
BoxState g_box;

constexpr float kBoxInset = 10.0f;     // `.isection-box` padding lateral
// O `.iline` do Tauri tem 44 pt e o `.isection` 22 de margem; aqui o inspetor tem
// uma secao por propriedade (o dobro das do Tauri), e nessa conta a coluna nao
// cabia na janela: 38 pt de linha e 16 entre secoes.
constexpr float kRowPad = 7.0f;        // (38 - 24) / 2
constexpr float kSectionGap = 16.0f;

}  // namespace

bool sectionHead(const char* label, bool enabled) {
    const float k = dpi();
    ImGui::PushFont(nullptr, 11.0f);
    const char* hash = std::strstr(label, "##");
    const char* end = hash ? hash : label + std::strlen(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    ImGui::PushID("##section-head");
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(ts.x + 2.0f * kBoxInset * k, ts.y));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    ImGui::EndDisabled();
    const ImVec4 col = !enabled ? theme::kText3 : hovered ? theme::kText : theme::kText2;
    ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + kBoxInset * k, p.y), theme::u32(col), label, end);
    ImGui::PopFont();
    return clicked && enabled;
}

void sectionStatus(const char* text, const ImVec4& colour) {
    const float k = dpi();
    ImGui::PushFont(nullptr, 11.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ts.x - kBoxInset * k);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool sectionAction(const char* text) {
    const float k = dpi();
    ImGui::PushFont(nullptr, 11.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ts.x - kBoxInset * k);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(text, ts);
    ImVec4 col = theme::kAccent;
    if (!ImGui::IsItemHovered()) col.w = 0.85f;
    ImGui::GetWindowDrawList()->AddText(p, theme::u32(col), text);
    ImGui::PopFont();
    return clicked;
}

void boxBegin() {
    const float k = dpi();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    g_box.active = true;
    g_box.rows = 0;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    g_box.x0 = p.x;
    g_box.x1 = p.x + ImGui::GetContentRegionAvail().x;
    g_box.top = p.y;
    ImGui::Indent(kBoxInset * k);
    // Os campos sobre a caixa: um degrau acima dela, senao somem no fundo.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::kBoxStrong);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme::kControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme::kCapOn);
}

void boxEnd() {
    if (!g_box.active) return;
    const float k = dpi();
    const float sp = ImGui::GetStyle().ItemSpacing.y;
    ImGui::PopStyleColor(3);
    ImGui::Unindent(kBoxInset * k);
    // O cursor ja esta `sp` abaixo do ultimo item; o fundo fecha a 10 pt dele.
    const float bottom = ImGui::GetCursorScreenPos().y + kRowPad * k - sp;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->ChannelsSetCurrent(0);
    dl->AddRectFilled(ImVec2(g_box.x0, g_box.top), ImVec2(g_box.x1, bottom), theme::u32(theme::kBox), 10.0f * k);
    dl->ChannelsMerge();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + kRowPad * k - sp);
    ImGui::Dummy(ImVec2(0.0f, kSectionGap * k - sp));
    g_box = BoxState{};
}

void rowStart() {
    if (!g_box.active) return;
    const float k = dpi();
    const float sp = ImGui::GetStyle().ItemSpacing.y;
    float y = ImGui::GetCursorPosY();
    if (g_box.rows > 0) {
        // Fecha a linha de cima (10 pt abaixo do controle dela) com o fio.
        const float line = ImGui::GetCursorScreenPos().y + kRowPad * k - sp;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(g_box.x0 + kBoxInset * k, line),
                                            ImVec2(g_box.x1 - kBoxInset * k, line), theme::u32(theme::kSep), 1.0f);
        y += kRowPad * k - sp;
    }
    ImGui::SetCursorPosY(y + kRowPad * k);
    ++g_box.rows;
}

int segmented(const char* const* labels, const char* const* tips, int count, int current) {
    const float k = dpi();
    rowStart();
    const float h = ImGui::GetFrameHeight();
    const float total = rowAvail();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // O trilho, e cada segmento com a largura proporcional ao rotulo dele.
    dl->AddRectFilled(a, ImVec2(a.x + total, a.y + h), theme::u32(theme::kBoxStrong), 6.0f * k);
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) sum += ImGui::CalcTextSize(labels[i]).x;
    const float pad = std::max(0.0f, (total - sum) / static_cast<float>(count));
    int picked = -1;
    float x = a.x;
    for (int i = 0; i < count; ++i) {
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        const float w = i + 1 == count ? a.x + total - x : ts.x + pad;
        ImGui::SetCursorScreenPos(ImVec2(x, a.y));
        if (ImGui::InvisibleButton(labels[i], ImVec2(w, h))) picked = i;
        const bool hovered = ImGui::IsItemHovered();
        if (tips && tips[i]) ImGui::SetItemTooltip("%s", tips[i]);
        const ImVec2 s0(x + 2.0f * k, a.y + 2.0f * k), s1(x + w - 2.0f * k, a.y + h - 2.0f * k);
        if (i == current) {
            dl->AddRectFilled(ImVec2(s0.x, s0.y + 0.5f * k), ImVec2(s1.x, s1.y + 0.5f * k), IM_COL32(0, 0, 0, 60),
                              4.5f * k);
            dl->AddRectFilled(s0, s1, theme::u32(theme::kTrack), 4.5f * k);
        }
        const ImVec4 col = i == current || hovered ? theme::kText : theme::kText2;
        dl->AddText(ImVec2(x + (w - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), theme::u32(col), labels[i]);
        x += w;
    }
    return picked;
}

void note(const char* text, const ImVec4& colour) {
    ImGui::PushFont(nullptr, 11.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + rowAvail());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void note(const char* text) { note(text, theme::kText3); }

float rowAvail() {
    if (!g_box.active) return ImGui::GetContentRegionAvail().x;
    return g_box.x1 - kBoxInset * dpi() - ImGui::GetCursorScreenPos().x;
}

bool toggle(const char* label, bool* on) {
    const float k = dpi();
    rowStart();
    // A LINHA INTEIRA E O CONTROLE, como o `Checkbox` que este substitui era
    // (caixa e rotulo): clicar no nome liga e desliga. O id e o do rotulo CRU,
    // para quem procura o controle pelo id continuar achando.
    const ImVec2 size(38.0f * k, 22.0f * k);
    const ImVec2 row0 = ImGui::GetCursorScreenPos();
    const float rowW = rowAvail();
    const float rowH = std::max(size.y, ImGui::GetFrameHeight());
    const bool clicked = ImGui::InvisibleButton(label, ImVec2(rowW, rowH));
    if (clicked) *on = !*on;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
    // O rotulo (ate o `##`) a esquerda, centrado na altura.
    const char* hash = std::strstr(label, "##");
    const char* end = hash ? hash : label + std::strlen(label);
    const float th = ImGui::GetTextLineHeight();
    ImVec4 tc = theme::kText;
    if (disabled) tc = theme::kText3;
    // O interruptor encostado a direita, e o rotulo cortado a 6 pt dele: na
    // coluna estreita um nome comprido nao corre por baixo do trilho.
    const ImVec2 a(row0.x + rowW - size.x, row0.y + (rowH - size.y) * 0.5f);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    dl->PushClipRect(row0, ImVec2(std::max(row0.x, a.x - 6.0f * k), row0.y + rowH), true);
    dl->AddText(ImVec2(row0.x, row0.y + (rowH - th) * 0.5f), theme::u32(tc), label, end);
    dl->PopClipRect();
    ImVec4 track = *on ? theme::kAccent : theme::kTrack;
    if (disabled) track.w *= 0.45f;
    dl->AddRectFilled(a, b, theme::u32(track), size.y * 0.5f);
    const float kx = *on ? b.x - 2.0f * k - 26.0f * k : a.x + 2.0f * k;
    const ImVec2 k0(kx, a.y + 2.0f * k), k1(kx + 26.0f * k, b.y - 2.0f * k);
    dl->AddRectFilled(ImVec2(k0.x, k0.y + 1.0f * k), ImVec2(k1.x, k1.y + 1.0f * k), IM_COL32(0, 0, 0, 64), 9.0f * k);
    dl->AddRectFilled(k0, k1, IM_COL32(255, 255, 255, disabled ? 140 : 255), 9.0f * k);
    return clicked;
}

namespace {

// O rotulo desenhado e o controle posto a direita com `width` px (0: logo
// depois do rotulo, na largura que o ImGui der).
const char* labelled(const char* label, float share, float fixed) {
    if (!label || (label[0] == '#' && label[1] == '#')) return label;
    static char ring[8][160];
    static int next = 0;
    char* out = ring[next++ & 7];
    const char* hash = std::strstr(label, "##");
    const std::size_t n = hash ? static_cast<std::size_t>(hash - label) : std::strlen(label);
    std::snprintf(out, sizeof ring[0], "##%s", label);
    rowStart();
    const float startX = ImGui::GetCursorPosX();
    const float avail = rowAvail();
    const float w = fixed > 0.0f ? std::min(fixed, avail) : avail * share;
    // O rotulo para a 6 pt do controle: na coluna estreita um nome comprido e
    // cortado ali, em vez de correr por baixo do campo.
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushClipRect(p, ImVec2(p.x + std::max(0.0f, avail - w - 6.0f * dpi()), p.y + ImGui::GetFrameHeight()),
                        true);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, label + n);
    ImGui::PopClipRect();
    // Um rotulo cortado diz o nome inteiro a quem para em cima dele.
    if (ImGui::CalcTextSize(label, label + n).x > avail - w - 6.0f * dpi() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        ImGui::SetTooltip("%.*s", static_cast<int>(n), label);
    }
    if (w > 0.0f) {
        ImGui::SameLine(startX + avail - w);
        ImGui::SetNextItemWidth(w);
    } else {
        ImGui::SameLine();
    }
    return out;
}

}  // namespace

const char* leftLabel(const char* label, float share) { return labelled(label, share, 0.0f); }

const char* leftLabelFixed(const char* label, float width) { return labelled(label, 0.0f, width * dpi()); }

bool combo(const char* label, const char* preview) {
    const float k = dpi();
    const char* id = leftLabel(label);
    // A lista aberta e um menu do sistema: 5 pt de margem, e as linhas de
    // `menuItem` dentro dela.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f * k, 5.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f * k);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.12f));
    // O valor e desenhado AQUI e nao pelo `BeginCombo`: sem a seta do ImGui o
    // texto dele vai ate a borda do campo, e na coluna estreita um nome comprido
    // ("Automatic Gradient") corria por baixo do chevron.
    const bool open = ImGui::BeginCombo(id, "", ImGuiComboFlags_NoArrowButton);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    // O chevron duplo no fim do campo, onde o ImGui poria a seta.
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c(b.x - 11.0f * k, (a.y + b.y) * 0.5f);
    if (preview && *preview) {
        const ImGuiStyle& style = ImGui::GetStyle();
        const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
        const float textEnd = std::max(a.x, c.x - 9.0f * k);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(a, ImVec2(textEnd, b.y), true);
        dl->AddText(ImVec2(a.x + style.FramePadding.x, a.y + style.FramePadding.y),
                    theme::u32(disabled ? theme::kText3 : theme::kText), preview);
        dl->PopClipRect();
        if (a.x + style.FramePadding.x + ImGui::CalcTextSize(preview).x > textEnd &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
            ImGui::SetTooltip("%s", preview);
        }
    }
    if (!symbol("chevron.up.chevron.down", c, 11.0f * k, theme::u32(theme::kText2))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddTriangleFilled(ImVec2(c.x - 3 * k, c.y - 1 * k), ImVec2(c.x + 3 * k, c.y - 1 * k),
                              ImVec2(c.x, c.y - 5 * k), theme::u32(theme::kText2));
        dl->AddTriangleFilled(ImVec2(c.x - 3 * k, c.y + 1 * k), ImVec2(c.x + 3 * k, c.y + 1 * k),
                              ImVec2(c.x, c.y + 5 * k), theme::u32(theme::kText2));
    }
    return open;
}

bool colorWell(const char* id, float rgba[4], bool* released, const char* tooltip) {
    const float k = dpi();
    const ImVec2 size(ImGui::CalcItemWidth(), ImGui::GetFrameHeight());
    ImGui::PushID(id);
    const bool clicked = ImGui::InvisibleButton("##well", size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (tooltip) ImGui::SetItemTooltip("%s", tooltip);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = theme::kControlRadius * k;
    // O xadrez so aparece atras de uma cor que deixa ver atraves.
    if (rgba[3] < 1.0f) checkerboard(dl, a, b, 6.0f * k, r);
    dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], rgba[3])), r);
    dl->AddRect(a, b, IM_COL32(255, 255, 255, hovered ? 70 : 36), r, 0, 1.0f);

    // O seletor, num popover no desenho dos menus. Sem posicao imposta: o ImGui
    // o poe junto do ponteiro e DENTRO da janela, e o poco fica na borda direita
    // do inspetor -- um popover alinhado a ele sairia da tela.
    if (clicked) ImGui::OpenPopup("##picker");
    bool changed = false;
    if (released) *released = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * k, 10.0f * k));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f * k);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.12f));
    const bool open = ImGui::BeginPopup("##picker");
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (open) {
        // `Float`: os componentes vao e voltam como fracoes, sem passar por
        // inteiros de 8 bits. Os campos RGB e o hex ficam, que e por onde se
        // digita um valor exato.
        ImGui::SetNextItemWidth(220.0f * k);
        changed = ImGui::ColorPicker4("##colour", rgba,
                                      ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar |
                                          ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview |
                                          ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_DisplayRGB |
                                          ImGuiColorEditFlags_DisplayHex);
        if (released) *released = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

void checkerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell, float rounding) {
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255), rounding);
    dl->PushClipRect(a, b, true);
    for (float y = a.y; y < b.y; y += cell) {
        const int row = static_cast<int>((y - a.y) / cell);
        for (float x = a.x + ((row & 1) ? cell : 0.0f); x < b.x; x += cell * 2.0f)
            dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)),
                              IM_COL32(0xe2, 0xe2, 0xe2, 255));
    }
    dl->PopClipRect();
}

}  // namespace ui
}  // namespace ick
