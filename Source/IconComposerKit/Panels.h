#pragma once
// The four panels and the menu, each a function from the Session to what it
// drew. The return values are MEASUREMENTS of the frame -- how many rows, how
// many sections, whether a texture was shown -- so the selftest can assert on a
// frame without a window.
//
// Window titles, and why one carries `##ic`: Onyx registers its own "Inspector"
// panel, and two ImGui windows with the same title are one window. The label a
// person reads stops at `##`.
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ick {

inline constexpr const char* kLayersWindow = "Layers";
inline constexpr const char* kCanvasWindow = "Canvas";
inline constexpr const char* kInspectorWindow = "Inspector##ic";
inline constexpr const char* kDiagnosticsWindow = "Diagnostics";

// UMA LINHA DA ARVORE, como ela foi PARA A TELA. Contagem nao distingue "a
// arvore esta certa" de "a arvore tem o numero certo de linhas erradas": depois
// de mover, renomear ou apagar um no, a pergunta e se o que aparece e o que o
// documento diz, e so o conteudo da linha responde isso. `drawRow` e o unico
// caminho por onde uma linha nasce, entao gravar la cobre todas.
struct RowInfo {
    std::string title;      // o nome que a pessoa le
    bool layer = false;     // camada (true) ou grupo (false)
    bool visible = true;    // o estado do interruptor de visibilidade
    bool glass = false;     // o do vidro; sempre false num grupo, que nao o tem
    bool selected = false;

    // ONDE CADA COISA FOI DESENHADA, em pixels de tela, para quem precisa
    // CLICAR nela.
    //
    // Um teste que chama `setProperty` prova a escrita e nao prova o
    // interruptor: entre o pixel e a escrita ha o retangulo do controle, a
    // ordem em que ele e submetido e o `AllowOverlap` da linha inteira, e ja
    // houve um defeito exatamente ai -- um `Selectable` por cima de um
    // `TreeNodeEx` fazia o clique na parte vazia da linha nao fazer nada.
    // Com o centro de cada item gravado, o teste injeta o evento de mouse do
    // ImGui nesse ponto e a coisa exercida e o controle, nao a funcao embaixo
    // dele. Nada disso toca o sistema: e a fila de eventos do ImGui, no
    // contexto headless.
    //
    // `{0,0}` num grupo para o vidro, que ele nao desenha.
    ImVec2 rowAt{0.0f, 0.0f};
    ImVec2 visibleAt{0.0f, 0.0f};
    ImVec2 glassAt{0.0f, 0.0f};
};

struct LayersStats {
    std::size_t groups = 0, layers = 0;
    bool selectionChanged = false;
    std::size_t rows = 0;       // rows actually on screen: a closed group hides its layers
    std::size_t problems = 0;   // of those, rows drawn with a diagnostic (no art, or art gone)
    std::vector<RowInfo> drawn;   // uma entrada por linha desenhada, de cima para baixo
};
LayersStats drawLayers(Session& s);

// THE TWO PIECES OF THE LAYER PANEL THAT ARE ARITHMETIC, NOT DRAWING
// -----------------------------------------------------------------------------
// The selftest counts `imgui errors` and cannot see a gesture, so the gesture's
// logic is lifted out of the frame and asserted on its own (Tests/test_kit_layers.cpp).

// What a drop between two rows costs in `Session::moveNode` calls.
//
// `moveNode` SWAPS two siblings one position apart (Edit.h), so it is the only
// move the model has, and a drag of five positions has to become five of them --
// but exactly five, and never six. `gap` is the INSERTION SLOT the row was
// dropped at, counted like an iterator: 0 is above the first sibling, `count` is
// below the last, so slot `g` sits between siblings `g-1` and `g`.
struct DropPlan {
    bool valid = false;         // false: the drop changes nothing, or is out of range
    int delta = 0;              // -1 (up) or +1 (down), applied `steps` times
    std::size_t steps = 0;      // how many single-position swaps
    std::size_t to = 0;         // the index the dragged sibling ends at
};
DropPlan planDrop(std::size_t from, std::size_t gap, std::size_t count);

// The tree flattened to what a person can see and therefore walk with the arrow
// keys: every group, and the layers of the OPEN ones, top to bottom. `expanded`
// is indexed by group; a group past its end counts as open, which is how a group
// that was just added arrives.
std::vector<icf::NodePath> visibleRows(const icf::json::Value& root,
                                       const std::vector<unsigned char>& expanded);

// The last render the canvas has to show, and what it did not draw.
struct RenderView {
    // `ImTextureID_Invalid`, never a literal 0: ImGui 1.92 is mid-migration to
    // `ImTextureRef`, and Onyx's own TexturePool already spells it this way.
    ImTextureID texture = ImTextureID_Invalid;
    std::uint32_t width = 0, height = 0;
    // Onde a textura fica: `width` x `height` a partir de (`originX`,
    // `originY`) numa grade de `gridSize` (spec 2026-09-16, "O que o Kit
    // faz"). Zero = a textura e o canvas inteiro em `width`, que e o que ela
    // sempre foi antes desta frente.
    std::uint32_t gridSize = 0;
    std::int32_t originX = 0, originY = 0;
    // False: o ladrilho pedido nao coube (o teto de area ou o do aparelho) e
    // isto e o canvas inteiro na resolucao base, esticado.
    bool refined = true;
    bool pending = false;   // a newer render is on its way
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;

    // HOW LONG THE RENDER TOOK, AND HOW LONG THE ONE IN FLIGHT HAS BEEN GOING.
    //
    // This is the number whose absence made 2026-09-15 possible. Six fronts
    // landed in an afternoon and together took a 1024 px glass render from about
    // a second to eighty-seven; the editor showed an empty canvas and the
    // Diagnostics panel said `pending`, which is the same thing it says when a
    // render takes forty milliseconds. There was no failure to report -- nothing
    // was wrong except that the work does not finish inside a human's patience --
    // so the only place that could have said so is a clock, and there was none.
    //
    // `lastRenderSeconds` is negative until a render has completed, which is not
    // the same as zero and must not print as `0.00 s`.
    // `pendingSeconds` is wall time since the request was made, so it counts the
    // queue as well as the draw: it is what the person is actually waiting for.
    double lastRenderSeconds = -1.0;
    double pendingSeconds = 0.0;
};

// What the menu asked the app to do this frame. The Kit cannot open a file
// dialog or quit; the app reads these after the frame and does it.
//
// AND SINCE 19/09 IT IS NOT ONLY THE MENU THAT ASKS. The Image Asset section
// needs the same door: importing a file means choosing one, choosing one means
// a dialog, and a dialog is Onyx's (Rule 2 -- only `Source/app` links it). The
// channel already existed for `open`/`save`/`saveAs`; it was simply never
// spelled for anything but the bar, and the asset panel said so in a comment
// instead of using it.
struct MenuActions {
    bool newDocument = false, open = false, save = false, saveAs = false, close = false, quit = false;

    // ---- importar um asset por dialogo ------------------------------------
    // `importInto` E O PEDIDO, NAO A SELECAO. `State::act()` roda DEPOIS do
    // frame, e entre o clique no botao e o retorno do dialogo a pessoa pode
    // ter selecionado outra camada (o dialogo e modal para a janela, mas a
    // selecao tambem muda por undo, por apagar um irmao, por um drop). Um
    // pedido que dissesse so "importe" faria o app escrever `image-name` no
    // que estivesse selecionado quando o dialogo fechasse -- outra camada, sem
    // um pixel na tela explicando. Entao o pedido carrega consigo O NO que
    // pediu e O ESCOPO em que ele estava, que sao as duas coordenadas de uma
    // escrita (Edit.h, `setProperty`).
    bool importAsset = false;
    icf::NodePath importInto;
    icf::Context importScope;
};

// UM ITEM DA BARRA, COMO ELE FOI PARA A TELA -- e ONDE, pelo mesmo motivo de
// `RowInfo`: entre o pixel e a operacao existem o retangulo do item, o popup
// que o contem e a ordem em que os dois sao submetidos. Um teste que chama
// `s.duplicateNode()` prova a operacao e nao prova o item de menu.
struct MenuItemInfo {
    std::string label;
    bool enabled = true;
    ImVec2 at{0.0f, 0.0f};   // o centro do item, em pixels de tela
};

struct MenuStats {
    std::size_t menus = 0, items = 0, disabled = 0;
    // Uma entrada por item desenhado NESTE quadro. Um menu fechado nao desenha
    // os itens dele, entao a lista so os tem depois que o popup abriu.
    std::vector<MenuItemInfo> drawn;
    // Onde cada TITULO da barra ficou ("File", "Edit", ...). E nele que se
    // clica para o popup abrir, e so entao os itens existem para serem
    // clicados. Gravado apenas com o menu FECHADO -- ver MenuBar.cpp.
    std::vector<MenuItemInfo> titles;
};
// Draws inside the current window's menu bar: the caller opened the window with
// ImGuiWindowFlags_MenuBar (the canvas does, like sfsymview's "Symbols").
MenuStats drawMenuBar(Session& s, MenuActions& actions);

// ─────────────────────────────────────────────────────────────────────────────
// THE CANVAS'S GEOMETRY, AS FUNCTIONS THAT DRAW NOTHING
//
// Everything the canvas has to get RIGHT -- where Fit puts the icon, where the
// wheel leaves the pixel under the cursor, how far a drag may go, what the
// clip rectangle is -- is arithmetic, and arithmetic can be checked. The
// headless selftest counts `imgui errors 0` and `textured yes` and would have
// reported both while the icon was painting over the toolbar, so it is not the
// thing that keeps these honest; Tests/test_kit_canvas.cpp is.
//
// The convention every one of these shares: a pan is the icon's TOP-LEFT CORNER
// in canvas-local pixels (x right, y down, origin at the canvas's top-left), and
// `sidePx` is the texture's own side in texels, so the icon on screen is
// `sidePx * zoom` wide. Zoom 1.0 therefore means one texel to one screen pixel,
// which is what the `1:1` button asks for.
// ─────────────────────────────────────────────────────────────────────────────
struct CanvasVec { float x = 0.0f, y = 0.0f; };

struct CanvasRect {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    float width() const { return x1 - x0; }
    float height() const { return y1 - y0; }
    bool empty() const { return !(x1 > x0 && y1 > y0); }
    // Tolerant by a quarter pixel: these are floats that have been through a
    // multiply and a lerp, and a rect that pokes out by 1e-6 of a pixel is not
    // the bug this asks about.
    bool contains(const CanvasRect& in, float slack = 0.25f) const {
        return in.empty() || (in.x0 >= x0 - slack && in.y0 >= y0 - slack &&
                              in.x1 <= x1 + slack && in.y1 <= y1 + slack);
    }
};

// The same limits Onyx's ImageViewer uses, so the two viewers in this person's
// day behave alike: eight times out, sixteen times in.
inline constexpr float kCanvasZoomMin = 0.125f;
inline constexpr float kCanvasZoomMax = 16.0f;
// How far past the edge a drag may push the icon. Without it the icon can be
// dragged entirely out of the canvas and there is nothing left on screen to drag
// back; with it there is always this much of it still in view.
inline constexpr float kCanvasPanMargin = 80.0f;

float canvasClampZoom(float zoom);
// The largest zoom at which the whole icon still fits in the canvas.
float canvasFitZoom(float availW, float availH, float sidePx);
// The pan that puts the icon in the middle of the canvas at that zoom.
CanvasVec canvasCentrePan(float availW, float availH, float sidePx, float zoom);
// Zoom about a point: the pan that keeps whatever is under `anchor` (canvas
// local) under `anchor` after the magnification changes. This is the whole of
// "the thing I pointed at did not run away when I scrolled".
CanvasVec canvasZoomAnchored(CanvasVec pan, float fromZoom, float toZoom, CanvasVec anchor);
// Pan bounded by `margin`: an icon smaller than the canvas may be nudged that
// far off centre, a larger one may be dragged until that much of it is left.
CanvasVec canvasClampPan(CanvasVec pan, float availW, float availH, float sidePx, float zoom,
                         float margin = kCanvasPanMargin);
CanvasRect canvasImageRect(CanvasVec pan, float sidePx, float zoom);
CanvasRect canvasIntersect(CanvasRect a, CanvasRect b);

// ─── A SELEÇÃO, QUE É GEOMETRIA DE TELA E NUNCA UM RENDER ────────────────────
//
// Spec 13/09 §6 já decidiu a forma e a marcou `[INF]`: "o retângulo da camada
// selecionada, calculado do `viewBox` do SVG mais `position`, na régua
// `kCanvasPoints` do `IconRenderer`". As duas funções abaixo são essa régua, e
// são as MESMAS duas coisas: o retângulo que o canvas desenha é o alvo que o
// clique acerta, porque os dois saem de `canvasLayerRect`. Duas cópias da
// aritmética seriam duas que param de concordar.
//
// `[INF]` A APROXIMAÇÃO, DITA EM VEZ DE ESCONDIDA: o `viewBox` NÃO é lido. Lê-lo
// custaria abrir e analisar o SVG de cada camada (`icf::svg::SvgDocument::parse`,
// um XML inteiro) -- IO e parse por camada, num documento que chega a 194 delas,
// para responder a um movimento do mouse. Então a caixa da arte é assumida como
// o canvas inteiro (`viewBox 0 0 1024 1024`, o mais comum do corpus: 36 dos 149
// SVGs), e o retângulo é essa caixa colocada exatamente como `rb::placeOnCanvas`
// a coloca -- centrada, escalada por `position.scale`, movida por
// `position.translation`, composta com a `position` do grupo.
//
// O QUE ISSO CUSTA: numa arte cujo `viewBox` é menor ou não é quadrado o
// retângulo é MAIOR que a arte, e o clique pega a camada um pouco antes de o
// cursor tocar um pixel dela. Numa arte vazada -- um anel -- o clique pega a
// camada no buraco. Responder "esta camada tem cobertura neste ponto" com
// precisão exige a cobertura, isto é, um render por camada, que é justamente o
// que a spec §6 proíbe no canvas ("zoom não é render").

// O retângulo de um nó em pixels de tela. `topLeft` é o canto do canvas na tela
// e `side` é o lado dele (`size * zoom`) -- os dois números que o zoom e o pan
// movem, e é por isso que o retângulo os acompanha sem uma segunda cópia da
// transformação. Vazio (`empty()`) quando o nó não existe.
CanvasRect canvasLayerRect(const icf::json::Value& root, icf::NodePath path, icf::Context ctx,
                           CanvasVec topLeft, float side);

// A camada sob um ponto da tela: a MAIS ACIMA cujo retângulo o contenha, ou
// nada quando o ponto está no vazio. O array corre da frente para trás (o
// render o percorre ao contrário, IconRenderer.cpp:776 e :1008), então a
// primeira que acerta é a de cima. Um nó escondido não é candidato: clicar num
// pixel que não existe não pode escolher quem não o pintou.
std::optional<icf::NodePath> canvasLayerAt(const icf::json::Value& root, icf::Context ctx,
                                           CanvasVec topLeft, float side, CanvasVec point);

// ─── O LADRILHO (spec 2026-09-16, "O que o Kit faz") ─────────────────────────
//
// Quanto tempo o pan e o zoom precisam ficar parados antes de um pedido sair.
// Curto de proposito: e o intervalo que separa "a pessoa parou de arrastar" de
// "a pessoa esta arrastando", e nao um atraso que ela deva sentir. Casa com o
// settle do ease (`canvasEase`, ~150 ms), entao um zoom com a roda acaba de
// assentar na tela quando o pedido sai.
inline constexpr float kTileSettleSeconds = 0.15f;
// A resolucao do ladrilho para um zoom: zero (a base) ate 100%, e a base
// ampliada acima disso. Com zoom <= 1 nada muda -- vale o caminho de sempre.
std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom);
// O retangulo pintado (`CanvasStats::painted`, em pixels de tela), levado ao
// espaco de um ladrilho de `canvasTileSize`. `imageTopLeft` e o canto da imagem
// na tela. Vazio (`w == 0`) quando nao ha nada pintado.
TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom);
// The per-frame coefficient of the exponential ease, clamped against both a
// stalled frame and a 500 Hz one so the settle is ~150ms on any machine.
float canvasEase(float deltaSeconds);

struct CanvasStats {
    bool textured = false;
    std::size_t contextControls = 0;   // appearance, idiom, size, zoom
    std::size_t zoomControls = 0;      // -, +, 1:1, Fit
    MenuStats menu;

    // THE FRAME'S GEOMETRY, IN SCREEN COORDINATES, so a test can assert on what
    // the canvas actually did instead of re-deriving it. `clip` is the rectangle
    // the canvas pushed and nothing it draws escapes; `image` is where the icon
    // went, unclipped and possibly far larger than the canvas; `painted` is the
    // two intersected -- the pixels that can reach the screen.
    CanvasRect clip, image, painted;
    float zoom = 1.0f;   // the eased magnification this frame, not the target
    // O aviso de que estes pixels sao a base esticada esteve na barra neste
    // frame. Espelha `RenderView::refined`, que ate 18/09 era escrito pelo
    // coordenador e lido por ninguem.
    bool stretchedNotice = false;
    // O RETANGULO DA CAMADA SELECIONADA, COMO ELE FOI PARA A TELA, pelo mesmo
    // motivo de `RowInfo::rowAt`: entre "a Session tem uma selecao" e "a pessoa
    // ve ONDE ela esta" existe a transformacao de vista, e so o retangulo
    // desenhado prova que ela acompanha o zoom e o pan em vez de deslizar. E e
    // ele tambem que o teste de clique usa como alvo -- o que se desenha e o
    // que se acerta.
    //
    // `selectionDrawn` e falso quando nao ha selecao, quando ela e um grupo (o
    // canvas so enquadra camada) ou quando o no sumiu.
    bool selectionDrawn = false;
    CanvasRect selection;
    // A LINHA VERMELHA, EXATAMENTE COMO ELA FOI PARA A TELA -- elidida, que e
    // o que a barra cabe. Vazia quando nao havia nada a dizer. O texto
    // INTEIRO e do painel de Diagnostics (`DiagnosticsStats::trouble`); esta
    // aqui e a que garante que a pessoa OLHANDO o canvas ve que houve falha.
    std::string trouble;
};

// `trouble` E DO APP E CHEGA POR PARAMETRO (Regra 2). `State::trouble` mora em
// `Source/app/Window.cpp` -- e o app que abre dialogo, cria e salva, e portanto
// e o app que sabe o que deu errado. O Kit nao inventa um global para ler isso:
// recebe a frase e a desenha. Vazia e o caso normal, e entao nada e desenhado.
CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions,
                       std::string_view trouble = {});

// O QUE UMA SECAO DO INSPETOR E, para quem precisa conferir de fora.
//
// O painel conta seções desde o inicio, e contagem nao distingue "a seção Fill
// sumiu" de "a seção Shadow apareceu duas vezes". O nome distingue, e e o que
// torna o inventario do inspetor uma coisa que um teste pode afirmar em vez de
// uma lista que envelhece num comentario: `Section::begin` e o unico caminho
// por onde uma seção nasce, entao gravar aqui cobre todas, inclusive as que
// ainda nao existem.
struct SectionInfo {
    std::string label;   // o cabecalho que a pessoa le
    std::string prop;    // a chave do documento que ela edita
    bool own = false;    // o escopo atual tem entrada propria (nao herdada)
    bool open = false;   // o cabecalho estava aberto, entao o corpo desenhou
};

struct InspectorStats {
    std::string title;
    std::size_t sections = 0;    // enabled sections drawn
    std::size_t inherited = 0;   // of those, marked as inherited under the scope
    std::size_t disabled = 0;    // sections drawn greyed, with the reason
    std::vector<SectionInfo> drawn;   // uma entrada por `Section::begin`, em ordem
    // ONDE O BOTAO "Browse..." DA SECAO Image Asset FOI DESENHADO, em pixels
    // de tela, para quem precisa CLICAR nele. `{0,0}` quando o no em foco nao
    // tem essa secao -- o mesmo contrato de `RowInfo::glassAt` num grupo.
    ImVec2 importBrowseAt{0.0f, 0.0f};
};
// `actions` porque o botao Browse da secao Image Asset pede um dialogo, e o
// Kit nao abre dialogo (Regra 2): ele escreve o pedido aqui e o app o le
// depois do frame, como ja fazia com a barra de menu.
InspectorStats drawInspector(Session& s, MenuActions& actions);

struct DiagnosticsStats {
    std::size_t rows = 0;
    // A FRASE INTEIRA da ultima falha do app, como ela foi para a tela --
    // vazia quando nao havia nenhuma. A barra do canvas elide para caber;
    // este painel e o "algum lugar" onde o texto completo tem de estar.
    std::string trouble;
};
DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view,
                                 std::string_view trouble = {});

}  // namespace ick
