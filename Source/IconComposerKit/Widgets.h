#pragma once
// Os componentes da janela do Tauri (ui/src/*.tsx, ui/src/App.css), em Dear
// ImGui: o que o CSS desenha com `border-radius`, `box-shadow` e mascara de
// SF Symbol, aqui desenhado na draw list com as cores do tema (Theme.h).
//
// Nada aqui guarda estado de documento: sao widgets. O estado de UI que um
// widget precisa (um menu aberto) fica no ImGui, pelo id.
#include "Source/IconComposerKit/Stage.h"

#include "imgui.h"

#include <filesystem>
#include <memory>
#include <string>
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

// O PALCO: a grade do icone (`appicongrid.ios`/`.watchos` de `custom/`, como
// mascara branca num raster grande -- a do `SymbolSource` tem o tamanho de um
// botao) e as imagens de fundo (`backgrounds/`, mais as que a pessoa
// acrescenta). Sem fonte, nos testes, nao ha grade nem imagem: o palco e a cor
// chapada e os controles continuam la.
// UM QUADRO DO MONO, COMPOSTO NA GPU DA TELA. `texture` e o icone de antes do
// vidro (`RenderResult::monoRaw`), no retangulo `quad` da tela, cobrindo `uv`
// do canvas (0..1; um ladrilho cobre so um pedaco). O resto e o que o vidro e
// o Clear leem: onde o quadrado do icone esta, o palco e o fundo dele.
struct MonoStageDraw {
    ImTextureID texture = ImTextureID_Invalid;
    ImVec2 quadMin, quadMax;
    ImVec2 uvMin, uvMax;
    ImVec2 squareMin;
    float squareSide = 0.0f;
    ImVec2 stageMin, stageMax;
    StageBackground background;
    bool clear = false;   // a textura e a mascara do Clear
    bool dark = false;    // a matriz escura do vidro
    bool watch = false;   // a pastilha redonda
};

struct StageSource {
    virtual ~StageSource() = default;
    // O app compoe o Mono na GPU da tela. Falso (os testes, ou um aparelho
    // que nao montou o pipeline): o vidro e o Clear ficam com o job, na CPU.
    virtual bool composes() { return false; }
    // Poe o quadro na draw list, dentro do recorte corrente. Falso: nao deu, e
    // o chamador desenha a textura como ela e.
    virtual bool composeMono(ImDrawList*, const MonoStageDraw&) { return false; }
    virtual ImTextureID grid(bool watch) = 0;
    virtual int backgrounds() = 0;
    // A textura e a proporcao; invalida enquanto nao carregou.
    virtual ArtThumb background(int index) = 0;
    virtual std::string backgroundName(int index) = 0;
    // Os pixels, para o render do Mono (Ports.h, `MonoBackdrop::image`).
    virtual std::shared_ptr<const StagePixels> pixels(int index) = 0;
};
void setStageSource(StageSource* source);
StageSource* stageSource();

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
// O botao estreito da capsula (`.cap-btn.narrow`): 22 pt, so a setinha.
bool capChevron(const char* id, const char* tooltip = nullptr);
// A AMOSTRA DE FUNDO (`.swatch`): 36 x 26, raio 13, a cor ou a imagem (cortada
// em `cover`), com o anel de destaque quando e a escolhida. Dentro de uma
// capsula, como um `capButton`.
bool capSwatch(const char* id, ImU32 colour, const ArtThumb& image, bool on, const char* tooltip = nullptr);
// A mesma amostra solta, num popover: `size` em pt, `rounding` em pt. Sem cor
// nem imagem (`colour == 0`) desenha so o anel e deixa o miolo para o chamador.
bool swatch(const char* id, ImVec2 size, float rounding, ImU32 colour, const ArtThumb& image, bool on,
            const char* tooltip = nullptr);

// O ESTILO DE MENU DO SISTEMA (`.menu`): o popover arredondado, o item em
// hover na cor de destaque. Empilhar em volta de quem abre menus.
void pushMenuStyle();
void popMenuStyle();
// UMA LINHA DE MENU (`.menu-item`): 24 pt, o realce na cor de destaque com o
// canto de 5 pt, a marca de selecionado numa coluna a esquerda e o atalho a
// direita. Vale para os menus e para as listas dos seletores; a mesma
// assinatura e o mesmo id de `ImGui::MenuItem`.
bool menuItem(const char* label, const char* shortcut = nullptr, bool selected = false, bool enabled = true);
// UM SUBMENU: a mesma linha de `menuItem` com a seta a direita no lugar do
// atalho, acesa enquanto o filho esta aberto, e o popup do filho com a margem,
// o canto e a borda do pai. Mesma semantica de `ImGui::BeginMenu`: verdadeiro
// com o submenu aberto, e entao `endMenu()`.
bool beginMenu(const char* label, bool enabled = true);
void endMenu();

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
// O mesmo, com o controle numa largura fixa em pt (a `.numbox` do Tauri: o
// numero nao estica com a coluna).
const char* leftLabelFixed(const char* label, float width);

// A SECAO DO INSPETOR (`.isection`): o cabecalho de 11 pt secundario, e a
// caixa arredondada com as linhas de 44 pt separadas por um fio (`.iline`).
//
// `sectionHead` desenha o cabecalho e devolve verdade quando foi clicado;
// `sectionStatus` poe uma nota no fim da MESMA linha, alinhada a direita.
// Entre `boxBegin` e `boxEnd`, cada `leftLabel`, `combo` e `toggle` abre uma
// linha sozinho; um controle desenhado a mao chama `rowStart` antes, e
// `rowAvail` diz ate onde a linha vai (a caixa tem 10 pt de margem dos lados).
bool sectionHead(const char* label, bool enabled = true);
void sectionStatus(const char* text, const ImVec4& colour);
// A nota como botao, na cor de destaque (o `.scope` clicavel do Tauri).
bool sectionAction(const char* text);
void boxBegin();
void boxEnd();
void rowStart();
float rowAvail();
// O CONTROLE SEGMENTADO: as opcoes lado a lado numa linha da caixa, a
// escolhida em relevo. Cada segmento e um item com o id do rotulo dele.
// Devolve o indice clicado neste frame, ou -1.
int segmented(const char* const* labels, const char* const* tips, int count, int current);
// Uma nota dentro da caixa: 11 pt, terciaria (ou a cor dada), com quebra de
// linha na largura da caixa. E um item comum: aceita `SetItemTooltip` depois.
void note(const char* text);
void note(const char* text, const ImVec4& colour);

// O SELETOR DO SISTEMA (`Select` do Inspector.tsx): o rotulo a esquerda, o
// campo a direita com o valor e o `chevron.up.chevron.down` no fim, sem o
// quadrado com triangulo do ImGui. Mesma semantica de `ImGui::BeginCombo`:
// verdadeiro com o popup aberto, e entao `ImGui::EndCombo()`.
bool combo(const char* label, const char* preview);

// O POCO DE COR (`.well` do Inspector.tsx): a amostra arredondada, na altura
// de um campo e na largura do proximo item (`SetNextItemWidth`, que e o que
// `leftLabelFixed` deixa armado); clicar abre o seletor num popover. Devolve
// verdade quando a cor mudou. `released` fica verdadeiro no quadro em que uma
// edicao do seletor termina -- o fim do arrasto, para quem junta os passos
// num comando so.
bool colorWell(const char* id, float rgba[4], bool* released = nullptr, const char* tooltip = nullptr);

// O xadrez de 8 pt das miniaturas (`.thumb`).
void checkerboard(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell, float rounding);

// O fator de escala do DPI em vigor (o `FontScaleDpi` do estilo).
float dpi();

}  // namespace ui
}  // namespace ick
