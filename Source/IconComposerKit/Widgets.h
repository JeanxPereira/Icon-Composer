#pragma once
// Os componentes da janela do Tauri (ui/src/*.tsx, ui/src/App.css), em Dear
// ImGui: o que o CSS desenha com `border-radius`, `box-shadow` e mascara de
// SF Symbol, aqui desenhado na draw list com as cores do tema (Theme.h).
//
// Nada aqui guarda estado de documento: sao widgets. O estado de UI que um
// widget precisa (um menu aberto) fica no ImGui, pelo id.
#include "imgui.h"

#include <filesystem>
#include <string_view>

namespace ick {

// OS SIMBOLOS. Os SF Symbols e os assets proprios que o Tauri usa
// (`ui/public/apple/symbols` e `.../custom`), entregues como MASCARA BRANCA: a
// cor entra como tinta, e o mesmo arquivo serve de texto, de secundario e de
// selecionado -- o `mask-image` com `currentColor` do Sym.tsx.
//
// E um port como `TextureSink`: o Kit pede, o app entrega. Nos testes nao ha
// fonte, `symbol` devolve invalido, e os widgets desenham o fallback (uma
// letra ou nada) -- a interacao e a geometria nao mudam.
struct SymbolSource {
    virtual ~SymbolSource() = default;
    // `custom`: da pasta `custom/` (os assets do .car), senao `symbols/`.
    virtual ImTextureID symbol(std::string_view name, bool custom) = 0;
};
void setSymbolSource(SymbolSource* source);
SymbolSource* symbolSource();

// A ARTE DE UMA CAMADA, para a miniatura da sidebar (`Thumb` do Sidebar.tsx):
// o SVG ou o PNG de `Assets/`, rasterizado pelo app. Invalido enquanto nao
// chegou (ou nos testes, que nao tem fonte) -- a linha mostra o xadrez so.
struct ArtThumb {
    ImTextureID texture = ImTextureID_Invalid;
    float width = 0.0f, height = 0.0f;   // a proporcao da arte
};
struct ArtSource {
    virtual ~ArtSource() = default;
    virtual ArtThumb art(const std::filesystem::path& file) = 0;
};
void setArtSource(ArtSource* source);
ArtSource* artSource();

namespace ui {

// Desenha o simbolo centrado em `centre`, com lado `size` (pt, ja com o DPI).
// Falso quando nao ha textura -- o chamador decide o fallback.
bool symbol(std::string_view name, ImVec2 centre, float size, ImU32 colour, bool custom = false);

// Um botao que e so um simbolo, sem fundo (o `.plain-btn`): o hover clareia a
// cor. `fallback` e o texto desenhado sem a textura ("+", "-").
bool plainButton(const char* id, std::string_view sym, ImVec2 box, float symSize, const char* fallback,
                 bool enabled = true, const char* tooltip = nullptr);

// O botao redondo de 34 pt (`.round-btn`): o controle claro com a sombra.
bool roundButton(const char* id, std::string_view sym, float symSize, const char* tooltip = nullptr);

// A CAPSULA (`.capsule`): 34 pt de altura, raio 17, o preenchimento de
// controle. `beginCapsule` desenha o fundo na largura dada e posiciona o
// cursor no primeiro botao; os `capButton` andam dentro dela.
void beginCapsule(const char* id, float width);
void endCapsule();
// Um botao da capsula (`.cap-btn`): 32 x 28, raio 14, `on` com o fundo claro.
bool capButton(const char* id, std::string_view sym, bool on, float symSize = 17.0f, bool custom = false,
               const char* tooltip = nullptr, float width = 32.0f, const char* fallback = "?");
// Um botao de texto da capsula (`.text-cap`): o rotulo e uma setinha.
bool capText(const char* id, const char* label, float width, const char* tooltip = nullptr);

// O ESTILO DE MENU DO SISTEMA (`.menu`): o popover arredondado, o item em
// hover na cor de destaque. Empilhar em volta de quem abre menus.
void pushMenuStyle();
void popMenuStyle();

// O INTERRUPTOR DO SISTEMA (`.toggle` do Tauri): trilho de 38 x 22, botao de
// 26 x 18, a cor de destaque ligado. O rotulo fica a esquerda e o
// interruptor encostado a direita, como as linhas do inspetor (`.iline`).
// Devolve verdade quando mudou, como o `Checkbox` que ele substitui.
bool toggle(const char* label, bool* on);

// O ROTULO A ESQUERDA (`.iline-label`). Desenha a parte visivel de `label`
// (ate o `##`) alinhada a altura do controle, poe o cursor onde o controle
// deve comecar -- encostado a direita, `share` da largura -- e devolve um id
// escondido para o controle usar. Um rotulo que ja comeca com `##` passa sem
// mudar nada.
const char* leftLabel(const char* label, float share = 0.58f);

// O SELETOR DO SISTEMA (`Select` do Inspector.tsx): o rotulo a esquerda, o
// campo a direita com o valor e o `chevron.up.chevron.down` no fim, sem o
// quadrado com triangulo do ImGui. Mesma semantica de `ImGui::BeginCombo`:
// verdadeiro com o popup aberto, e entao `ImGui::EndCombo()`.
bool combo(const char* label, const char* preview);

// O xadrez de 8 pt das miniaturas (`.thumb`).
void checkerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell, float rounding);

// O fator de escala do DPI em vigor (o `FontScaleDpi` do estilo).
float dpi();

}  // namespace ui
}  // namespace ick
