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

bool symbol(std::string_view name, ImVec2 c, float size, ImU32 colour, bool custom) {
    if (!g_symbols) return false;
    const ImTextureID tex = g_symbols->symbol(name, custom);
    if (tex == ImTextureID_Invalid || tex == 0) return false;
    const float h = size * 0.5f;
    ImGui::GetWindowDrawList()->AddImage(tex, ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ImVec2(0, 0),
                                         ImVec2(1, 1), colour);
    return true;
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
    ImGui::SameLine(0, 0);
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
    ImGui::SameLine(0, 0);
    return clicked;
}

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

bool toggle(const char* label, bool* on) {
    const float k = dpi();
    // A LINHA INTEIRA E O CONTROLE, como o `Checkbox` que este substitui era
    // (caixa e rotulo): clicar no nome liga e desliga. O id e o do rotulo CRU,
    // para quem procura o controle pelo id continuar achando.
    const ImVec2 size(38.0f * k, 22.0f * k);
    const ImVec2 row0 = ImGui::GetCursorScreenPos();
    const float rowW = ImGui::GetContentRegionAvail().x;
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
    dl->AddText(ImVec2(row0.x, row0.y + (rowH - th) * 0.5f), theme::u32(tc), label, end);
    // O interruptor encostado a direita.
    const ImVec2 a(row0.x + rowW - size.x, row0.y + (rowH - size.y) * 0.5f);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    ImVec4 track = *on ? theme::kAccent : theme::kTrack;
    if (disabled) track.w *= 0.45f;
    dl->AddRectFilled(a, b, theme::u32(track), size.y * 0.5f);
    const float kx = *on ? b.x - 2.0f * k - 26.0f * k : a.x + 2.0f * k;
    const ImVec2 k0(kx, a.y + 2.0f * k), k1(kx + 26.0f * k, b.y - 2.0f * k);
    dl->AddRectFilled(ImVec2(k0.x, k0.y + 1.0f * k), ImVec2(k1.x, k1.y + 1.0f * k), IM_COL32(0, 0, 0, 64), 9.0f * k);
    dl->AddRectFilled(k0, k1, IM_COL32(255, 255, 255, disabled ? 140 : 255), 9.0f * k);
    return clicked;
}

const char* leftLabel(const char* label, float share) {
    if (!label || (label[0] == '#' && label[1] == '#')) return label;
    static char ring[8][160];
    static int next = 0;
    char* out = ring[next++ & 7];
    const char* hash = std::strstr(label, "##");
    const std::size_t n = hash ? static_cast<std::size_t>(hash - label) : std::strlen(label);
    std::snprintf(out, sizeof ring[0], "##%s", label);
    const float startX = ImGui::GetCursorPosX();
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label, label + n);
    if (share > 0.0f) {
        const float w = avail * share;
        ImGui::SameLine(startX + avail - w);
        ImGui::SetNextItemWidth(w);
    } else {
        ImGui::SameLine();
    }
    return out;
}

bool combo(const char* label, const char* preview) {
    const float k = dpi();
    const char* id = leftLabel(label);
    const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_NoArrowButton);
    // O chevron duplo no fim do campo, onde o ImGui poria a seta.
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const ImVec2 c(b.x - 11.0f * k, (a.y + b.y) * 0.5f);
    if (!symbol("chevron.up.chevron.down", c, 11.0f * k, theme::u32(theme::kText2))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddTriangleFilled(ImVec2(c.x - 3 * k, c.y - 1 * k), ImVec2(c.x + 3 * k, c.y - 1 * k),
                              ImVec2(c.x, c.y - 5 * k), theme::u32(theme::kText2));
        dl->AddTriangleFilled(ImVec2(c.x - 3 * k, c.y + 1 * k), ImVec2(c.x + 3 * k, c.y + 1 * k),
                              ImVec2(c.x, c.y + 5 * k), theme::u32(theme::kText2));
    }
    return open;
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
