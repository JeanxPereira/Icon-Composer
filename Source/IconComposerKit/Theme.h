#pragma once
// O tema do editor: UM, escuro, fixo no codigo -- a aparencia escura do
// macOS 27 (build 26A5416b), com os valores decodados no AquaKit.
//
// Cada token diz de onde veio:
//   [BIN] decodado do binario da Apple (o laudo do AquaKit citado ao lado);
//   [KIT] medido do kit Figma oficial da Apple (so layout e algumas cores);
//   [CSS] ainda ABERTO no AquaKit: fica o valor do ui/src/App.css do Tauri,
//         que imita a captura do alvo. Um [CSS] e o primeiro a trocar quando
//         o laudo fechar.
// Onde o alvo tem vidro (sidebar, barra de ferramentas), aqui e cor chapada:
// o blur e uma frente a parte.
//
// Para mudar a cara do editor, mude AQUI. Nao ha outro lugar, nem arquivo de
// configuracao, nem troca em tempo de execucao.
#include "imgui.h"

namespace ick::theme {

constexpr ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
}
inline ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

// ---- as cores do sistema, aparencia escura -----------------------------------
// Rotulos: branco com o alfa do catalogo. [BIN] DarkAqua.car / DarkStandard.car
// (AquaKit docs/re/2026-09-16-system-label-color-alphas-per-appearance.md e
// 2026-09-16-the-button-foreground-reaches-the-designlibrary-catalog.md).
inline constexpr ImVec4 kText       = rgba(255, 255, 255, 216 / 255.0f);   // labelColor [BIN]
inline constexpr ImVec4 kText2      = rgba(255, 255, 255, 140 / 255.0f);   // secondaryLabelColor [BIN]
inline constexpr ImVec4 kText3      = rgba(255, 255, 255, 63 / 255.0f);    // tertiaryLabelColor [BIN]
inline constexpr ImVec4 kText4      = rgba(255, 255, 255, 25 / 255.0f);    // quaternaryLabelColor [BIN]
inline constexpr ImVec4 kText5      = rgba(255, 255, 255, 12 / 255.0f);    // quinaryLabelColor [BIN]
// systemBlue da aparencia escura [BIN] (catalogo DarkStandard; AquaKit
// docs/superpowers/archive/fatia-1-janela.md). O `controlAccentColor` cru e
// (0,122,255); no escuro o azul do sistema e este.
inline constexpr ImVec4 kAccent     = rgba(0, 145, 255);
inline constexpr ImVec4 kAccentText = rgba(255, 255, 255);
// selectedContentBackgroundColor, escuro [BIN] (NSColor.h do AquaKit;
// docs/re/2026-09-17-spotlight-text-and-selection.md).
inline constexpr ImVec4 kSelection  = rgba(0, 89, 209);
// O preenchimento da janela no escuro [KIT] (AquaKit WindowShadow.h, "the fill").
inline constexpr ImVec4 kWindowFill = rgba(30, 30, 30);
// Trilho do switch ligado, escuro [BIN] (docs/re/2026-09-20-the-mac-switch-...).
inline constexpr ImVec4 kSwitchOn   = rgba(48, 209, 88);

// Os preenchimentos de controle e o separador sao ABERTOS no AquaKit
// (separatorColor, controlColor, controlBackgroundColor): ficam os do CSS.
inline constexpr ImVec4 kSep        = rgba(255, 255, 255, 0.09f);    // separatorColor [CSS]
inline constexpr ImVec4 kBox        = rgba(255, 255, 255, 0.05f);    // [CSS]
inline constexpr ImVec4 kBoxStrong  = rgba(255, 255, 255, 0.09f);    // [CSS]
inline constexpr ImVec4 kControl    = rgba(255, 255, 255, 0.12f);    // [CSS]
inline constexpr ImVec4 kCapOn      = rgba(255, 255, 255, 0.14f);    // [CSS]
inline constexpr ImVec4 kTrack      = rgba(255, 255, 255, 0.16f);    // [CSS]
inline constexpr ImVec4 kPopover    = rgba(40, 40, 43, 0.97f);       // [CSS]
inline constexpr ImVec4 kField      = rgba(0x1c, 0x1c, 0x1e);        // [CSS]
inline constexpr ImVec4 kDanger     = rgba(0xff, 0x69, 0x61);        // [CSS]
// O fundo solido do canvas, o da captura do alvo [CSS].
inline constexpr ImVec4 kCanvas     = rgba(0x1e, 0x1e, 0x20);
// Os paineis (sidebar, inspetor, barra): o preenchimento da janela, chapado
// no lugar do vidro.
inline constexpr ImVec4 kPanel      = kWindowFill;
inline constexpr ImVec4 kWindowBg   = kWindowFill;

// ---- a moldura (o rim do compositor) ---------------------------------------
// [BIN] AquaKit SkyLight/WindowShadow.h; docs/re/2026-08-20-window-shadow.md §3:
// rim de fora escuro (densidade 0,6 ativa, 0,55 inativa, raio 1) e rim de
// dentro claro (densidade 0,1, raio 0,5). As CORES sao inferidas das strings
// do dump ("black"/"white"), nao decodadas.
inline constexpr ImVec4 kRimInner         = rgba(255, 255, 255, 0.10f);
inline constexpr ImVec4 kRimOuterActive   = rgba(0, 0, 0, 0.60f);
inline constexpr ImVec4 kRimOuterInactive = rgba(0, 0, 0, 0.55f);

// ---- a geometria -----------------------------------------------------------
inline constexpr float kFontSize      = 13.0f;   // o corpo de texto do macOS
// A barra de titulo com a barra de ferramentas unificada: 52 pt, o da captura
// do alvo (ui/src/App.css `.toolbar`). O titlebar SEM toolbar do kit e 33 [KIT].
inline constexpr float kTitleBarH     = 52.0f;
// Da borda da janela ao disco da primeira luz: 16 pt, o da captura do alvo com
// a sidebar (commit ab45036 do Tauri). O inset do AppKit sem sidebar e 9 [BIN].
inline constexpr float kLightsInset   = 16.0f;
// O canto de controle por tamanho [BIN] (docs/re/2026-08-28-control-border-
// shape-radius.md §2): altura / 4 -- regular 24 pt -> 6, small 20 -> 5.
inline constexpr float kControlRadius = 6.0f;
inline constexpr float kSmallRadius   = 5.0f;
inline constexpr float kPopupRadius   = 10.0f;   // [CSS]
// O canto da janela e 16 pt [BIN] (NSThemeFrame, `_getCachedDefaultWindow
// CornerRadius`). No Windows quem recorta e o DWM, com o canto dele (8 px);
// este numero e o que a moldura desenharia se a janela tivesse alfa proprio.
inline constexpr float kWindowRadius  = 16.0f;
inline constexpr float kDwmRadius     = 8.0f;

// Escreve o tema no `ImGui::GetStyle()` do contexto corrente, escalado pelo
// DPI do monitor. Nao carrega fonte: isso e do app (os testes nao tem disco).
void apply(float dpiScale = 1.0f);

}  // namespace ick::theme
