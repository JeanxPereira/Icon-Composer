#pragma once
// O tema do editor: UM, escuro, fixo no codigo.
//
// Os valores sao os tokens do tema escuro de `ui/src/App.css` (o bloco
// `:root[data-theme="dark"]`), que por sua vez imitam os controles padrao do
// macOS com a cor de destaque -- o alvo nao tem paleta propria (inventario
// §0). Onde o CSS tem vidro (`--panel`, com blur), aqui e cor chapada: o blur
// e uma frente a parte.
//
// Para mudar a cara do editor, mude AQUI. Nao ha outro lugar, nem arquivo de
// configuracao, nem troca em tempo de execucao.
#include "imgui.h"

namespace ick::theme {

constexpr ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// ---- os tokens (App.css, tema escuro) --------------------------------------
inline constexpr ImVec4 kText       = rgba(0xf5, 0xf5, 0xf7);        // --text
inline constexpr ImVec4 kText2      = rgba(0xa1, 0xa1, 0xa6);        // --text-2
inline constexpr ImVec4 kText3      = rgba(0x6e, 0x6e, 0x73);        // --text-3
inline constexpr ImVec4 kAccent     = rgba(0x0a, 0x84, 0xff);        // --accent
inline constexpr ImVec4 kAccentText = rgba(0xff, 0xff, 0xff);        // --accent-text
inline constexpr ImVec4 kSep        = rgba(255, 255, 255, 0.09f);    // --sep
inline constexpr ImVec4 kBox        = rgba(255, 255, 255, 0.05f);    // --box
inline constexpr ImVec4 kBoxStrong  = rgba(255, 255, 255, 0.09f);    // --box-strong
inline constexpr ImVec4 kControl    = rgba(255, 255, 255, 0.12f);    // --control
inline constexpr ImVec4 kCapOn      = rgba(255, 255, 255, 0.14f);    // --cap-on
inline constexpr ImVec4 kTrack      = rgba(255, 255, 255, 0.16f);    // --track
inline constexpr ImVec4 kPopover    = rgba(40, 40, 43, 0.97f);       // --popover
inline constexpr ImVec4 kField      = rgba(0x1c, 0x1c, 0x1e);        // --field
inline constexpr ImVec4 kDanger     = rgba(0xff, 0x69, 0x61);        // --danger
inline constexpr ImVec4 kCanvas     = rgba(0x1e, 0x1e, 0x20);        // --canvas-solid
// `--panel` e rgba(30,30,32,.58) com blur sobre o canvas. Chapado sobre o
// proprio canvas ele some, entao o painel solido sobe um degrau.
inline constexpr ImVec4 kPanel      = rgba(0x26, 0x26, 0x29);
inline constexpr ImVec4 kWindowBg   = rgba(0x2a, 0x2a, 0x2e);        // html, body

// ---- a geometria -----------------------------------------------------------
inline constexpr float kFontSize      = 13.0f;   // font-size do :root
inline constexpr float kTitleBarH     = 38.0f;   // a barra de titulo com as luzes
inline constexpr float kControlRadius = 6.0f;
inline constexpr float kPopupRadius   = 10.0f;

// Escreve o tema no `ImGui::GetStyle()` do contexto corrente, escalado pelo
// DPI do monitor. Nao carrega fonte: isso e do app (os testes nao tem disco).
void apply(float dpiScale = 1.0f);

}  // namespace ick::theme
