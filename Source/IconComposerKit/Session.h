#pragma once
// One open `.icon`, and everything the UI does to it.
//
// UNDO IS A SNAPSHOT OF THE NODE, NOT AN INVERSE OPERATION (spec 13/09 §5)
// -------------------------------------------------------------------------
// A command records the path of the node it touched and that node's JSON
// before and after. Undo is assignment. The largest corpus document is under
// 200 KB, so a node copy costs nothing worth an inverse-operation bug. A
// structural command (add, remove, move) snapshots the PARENT.
//
// Coalescing: while a control is being dragged, consecutive edits to the same
// (path, property, scope) fold into one command -- what the target's
// `DocumentCommands` does with its coalesced undo. `endCoalescing` is the drag
// ending; the panels call it on `IsItemDeactivatedAfterEdit`.
//
// TWO INVARIANTS THE REST OF THE EDITOR RESTS ON
// -------------------------------------------------------------------------
// 1. Every mutation of the document goes through a command. A write that goes
//    around them is an undo that lies about what it restores.
// 2. `version()` moves on every one of them, including undo and redo. The
//    canvas re-renders on that number (spec 13/09 §6); a mutation that does not
//    move it is a canvas showing a document that is no longer the one in memory.
// `root()` and `bundle()` hand out mutable references because `IconBundle`'s own
// operations (`importAsset`, `saveAs`) need them. Reading through them is
// ordinary; WRITING to the tree through them is the one way to break both
// invariants at once, and no caller in this editor does it.
#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerKit/Tile.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ick {

// WHAT A PERSON IS LOOKING THROUGH -- INCLUDING WHERE THEY ARE LOOKING.
//
// `zoom` used to be the only thing here and `pan` was a `static ImVec2` inside
// PanelCanvas.cpp. A file-static is not "no state", it is ONE state shared by
// every document the process ever opens: closing a document you had dragged to
// the corner and opening another one put the new one in that same corner, with
// nothing on screen to explain it. Pan is a property of looking, exactly like
// zoom, so it lives exactly where zoom lives -- and a new `Session` therefore
// starts centred and unfitted, which is the whole of the fix.
//
// The pair `x`/`xTarget`: a control (wheel, drag, button, combo) writes the
// TARGET, and the canvas eases the current value toward it once per frame. That
// is what makes the movement smooth instead of teleporting. Both are kept
// because the anchored zoom has to do its arithmetic against the target -- two
// wheel clicks in one ease must compose, not fight.
//
// Plain floats rather than `ImVec2`: Session.h is the Kit's model header and
// nothing else in it needs Dear ImGui.
struct ViewContext {
    icf::Context context;         // appearance and idiom the canvas renders
    std::uint32_t size = 512;     // the preview size, in pixels

    float zoom = 1.0f;            // the magnification ON SCREEN this frame
    float zoomTarget = 1.0f;      // where the ease is heading
    // The icon's top-left corner, in pixels from the canvas's own top-left.
    float panX = 0.0f, panY = 0.0f;
    float panTargetX = 0.0f, panTargetY = 0.0f;

    // False until the canvas has laid the document out to Fit. It is false in a
    // freshly opened Session and nowhere else, so "Fit on open" needs no event.
    bool fitted = false;

    // A control outside the canvas -- the zoom combo, the View>Zoom menu -- asks
    // for a magnification by writing this, and the canvas applies it anchored on
    // the viewport centre and clears it. It cannot write `zoomTarget` itself:
    // the anchored zoom needs the PREVIOUS target to know how to move the pan,
    // and a control that overwrote it would zoom about the canvas's top-left
    // corner, which is not where anybody is looking.
    float zoomRequest = 0.0f;     // > 0 = pending
    bool fitRequest = false;

    // O LADRILHO (spec 2026-09-16, "O que o Kit faz"). O canvas o escreve
    // quando o pan e o zoom ficam parados por `kTileSettleSeconds`, e o
    // coordenador pede o que estiver aqui. Enquanto a pessoa arrasta, estes
    // dois NAO mudam -- e e isso que impede um pedido por quadro, que o
    // agendador so descartaria. `tileSize == 0` e a resolucao base (`size`)
    // com o canvas inteiro, o pedido de sempre.
    std::uint32_t tileSize = 0;
    TileRect tile;
    // O ladrilho que ESTE quadro pediria, guardado para o quadro seguinte
    // comparar. O assentamento nao pode olhar so o zoom e o pan: `tile` sai de
    // `CanvasStats::painted`, que muda quando a janela e redimensionada ou o
    // splitter do dock e arrastado. Sem esta comparacao, esses dois gestos
    // deixavam `settled` VERDADEIRO -- zoom e pan parados -- e o bloco de
    // `v.tile` reescrevia o ladrilho a cada quadro: um pedido por quadro, que
    // e exatamente o que o temporizador existe para impedir (spec 2026-09-16,
    // "O que o Kit faz"), alcancado por outro gesto.
    TileRect lastWanted;
    float settledSeconds = 0.0f;
};

// O QUE O MODAL DE EXPORTAÇÃO ESTÁ MOSTRANDO (T2).
//
// Mora aqui, ao lado de `selection` e `scope`, porque é o mesmo tipo de coisa:
// estado de quem está olhando, que precisa sobreviver ao quadro e não pertence
// ao documento. `MenuActions` não serve -- ele é zerado a cada quadro, e um
// modal que esquecesse o que foi marcado assim que a pessoa soltasse o mouse
// não seria um modal.
//
// `chosen` é um BIT POR CONTEXTO OFERECIDO, na ordem de `exportContexts()`, e
// não quatro booleanos com nome: a lista dos contextos cresce sozinha quando o
// documento ganha uma especialização, e cresceria de novo no dia em que alguém
// ler `ICRRenderingParameters.ClearMode` e o `Clear` virar um render de
// verdade. Um campo por aparência teria de ser reescrito nesse dia.
struct ExportSheetState {
    bool open = false;
    std::uint32_t size = 512;
    std::vector<unsigned char> chosen;
    // A última frase do app sobre esta exportação -- o progresso enquanto ela
    // corre, o resumo quando acaba. Vazia antes da primeira.
    std::string status;
    // Há uma exportação em curso. O botão Export fica cinza enquanto isto
    // vale, para a fila do app não receber um segundo lote por cima do
    // primeiro.
    bool busy = false;
};

class Session {
public:
    static std::optional<Session> open(const std::filesystem::path& bundleDir);
    // Writes `{"fill":"automatic","groups":[]}` and an empty Assets/, then opens it.
    static std::optional<Session> create(const std::filesystem::path& bundleDir);

    icf::IconBundle& bundle() { return bundle_; }
    const icf::IconBundle& bundle() const { return bundle_; }
    icf::json::Value& root() { return bundle_.json(); }
    const icf::json::Value& root() const { return bundle_.json(); }
    // Bumps on every edit, undo and redo -- what the render coordinator watches.
    std::uint64_t version() const { return version_; }

    void setProperty(icf::NodePath path, std::string_view prop, icf::Context scope,
                     std::optional<icf::json::Value> value, bool coalesce = false);
    void endCoalescing() { coalesceKey_.clear(); }
    std::optional<std::size_t> addGroup(std::string name);
    std::optional<std::size_t> addLayer(std::size_t group, std::string name, std::string imageName);
    bool removeNode(icf::NodePath path);
    // A copia do no, logo depois dele (Edit.h, `duplicateNode`, e o `[INF]`
    // sobre o nome esta la). Devolve o caminho do no NOVO, para quem quiser
    // seleciona-lo, ou nullopt quando nao ha o que duplicar.
    //
    // UM COMANDO SO, e o instantaneo e do PAI -- como `addGroup` e
    // `removeNode`, e pelo mesmo motivo: a operacao acrescenta um irmao, e o
    // unico no cuja imagem antes/depois descreve isso inteiro e o pai.
    std::optional<icf::NodePath> duplicateNode(icf::NodePath path);
    // `coalesce` is the drag rule of the header note applied to structure: a drop
    // five rows down is five swaps, and five entries on the undo stack for one
    // gesture is five presses of Ctrl+Z to take back one drag. Consecutive moves
    // under the same parent fold into one command while it is set; the panel
    // calls `endCoalescing` when the gesture ends, so two separate drags stay two.
    bool moveNode(icf::NodePath path, int delta, bool coalesce = false);
    bool rename(icf::NodePath path, std::string name);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    // "The stack is not where it was at the last save."
    bool isDirty() const { return undo_.size() != cleanDepth_; }

    std::string save();
    std::string saveAs(const std::filesystem::path& dir);

    // ─── A CAIXA DE CADA ARTE, LIDA UMA VEZ POR `image-name` ────────────────
    //
    // O hit-test do canvas precisa do retângulo de TODAS as camadas a cada
    // clique -- um documento do corpus chega a 194 --, e o retângulo de uma
    // camada é a caixa da arte colocada pela `position`. Abrir e analisar o
    // SVG de cada camada para responder a um clique é o caminho ingênuo, e é
    // inaceitável: é IO e parse por camada, dentro do laço de eventos.
    //
    // Então a leitura acontece UMA VEZ por `image-name` e fica aqui. Duas
    // camadas que apontam para a mesma arte -- o caso comum, é o que uma
    // especialização por aparência é -- custam uma leitura só, e o clique
    // seguinte não custa nenhuma. `assetBoxReads()` é esse número, exposto
    // para que um teste possa cobrá-lo em vez de acreditar nele.
    //
    // `nullptr` QUANDO NÃO HÁ CAIXA A AFIRMAR: o nome é vazio, o arquivo não
    // existe (o corpus tem referências penduradas -- 55 bundles, 2 delas), não
    // é um SVG que este leitor abra, ou é um `.png`, que não tem `viewBox`.
    // Quem pergunta cai no que o canvas fazia antes -- a caixa assumida como o
    // canvas inteiro --, que é uma aproximação e não um desaparecimento. O
    // `nullopt` também é GUARDADO: sem isso uma referência pendurada seria uma
    // ida ao disco por camada por clique, que é justamente o custo que este
    // cache existe para não ter.
    const icf::svg::ViewBox* assetViewBox(std::string_view imageName) const;
    // Quantos arquivos foram abertos desde que esta Session existe. Não cresce
    // com o número de cliques -- cresce com o número de artes distintas.
    std::uint64_t assetBoxReads() const { return assetBoxReads_; }

    std::optional<icf::NodePath> selection;
    icf::Context scope;
    ViewContext view;
    ExportSheetState exportSheet;

private:
    struct Command {
        icf::NodePath target;
        icf::json::Value before;
        icf::json::Value after;
        std::string key;   // non-empty while coalescing
    };
    explicit Session(icf::IconBundle b) : bundle_(std::move(b)) {}
    // Snapshots the node at `target`, runs `edit`, snapshots again, and records a
    // command when the two differ. Returns what `edit` returned; nothing is
    // recorded when the node does not exist or when the edit changed no byte.
    // `edit` takes the target node and returns bool; a structural edit ignores
    // that argument and calls the `icf::` function on `root()` instead -- the
    // parent snapshot captures the change either way, and no pointer moves
    // (erasing inside `groups[g].layers` does not move `groups[g]`).
    template <class F>
    bool apply(icf::NodePath target, std::string key, F&& edit);   // edit: bool(json::Value&)
    void push(Command c);
    void dropSelectionIfGone();
    // The selection names a node by INDEX, so a structural edit that renumbers
    // siblings has to renumber it too, or it silently points at another node.
    void reindexSelectionAfterMove(icf::NodePath path, int delta);
    void reindexSelectionAfterRemove(icf::NodePath path);
    // E inserir um irmao empurra para baixo todo irmao que vinha depois dele,
    // exatamente como remover empurra para cima. Sem isto uma selecao abaixo
    // da duplicata passaria a nomear o vizinho de cima em silencio -- o mesmo
    // defeito que `reindexSelectionAfterRemove` existe para impedir.
    void reindexSelectionAfterInsert(icf::NodePath path);

    icf::IconBundle bundle_;
    // `mutable` porque `assetViewBox` é uma PERGUNTA -- o documento não muda
    // ao ser perguntado, e o hit-test roda sobre uma Session const. O
    // `std::less<>` transparente é o que deixa procurar por `string_view` sem
    // construir uma `std::string` a cada consulta.
    mutable std::map<std::string, std::optional<icf::svg::ViewBox>, std::less<>> assetBoxes_;
    // A geração de `Assets/` que `assetBoxes_` descreve. Quando a do bundle
    // passa desta, alguém importou por cima e o que está guardado é a caixa da
    // arte antiga (IconBundle.h, `assetsGeneration`).
    mutable std::uint64_t assetBoxesGeneration_ = 0;
    mutable std::uint64_t assetBoxReads_ = 0;
    std::vector<Command> undo_;
    std::vector<Command> redo_;
    std::size_t cleanDepth_ = 0;
    std::string coalesceKey_;
    std::uint64_t version_ = 1;
};

}  // namespace ick
