#pragma once
// The two things a window gives the Kit, as interfaces the Kit never implements.
//
// Rule 2 of the architecture spec: only Source/app links Onyx. So the upload of
// pixels to the GPU the window owns, and the thread the render runs on, are
// abstract here and concrete there -- and null in every test, which is what
// makes the whole UI exercisable on a machine with no device.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerKit/Stage.h"
#include "Source/IconComposerKit/Tile.h"
#include "Source/RenderBox/DesignGeneration.h"
#include "Source/RenderBox/Mono.h"
#include "imgui.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ick {

// O FUNDO ATRAS DO ICONE, para o Mono (o vidro simulado e o Clear leem o que
// esta atras): uma cor chapada do tamanho do palco, em `pixelsPerPoint` --
// o canvas do editor e solido (Theme.h, `kCanvas`), e o app o monta assim.
//
// DESDE 02/10 O PALCO PODE SER UMA IMAGEM (Stage.h): com `image` presente o
// app a estende sobre o palco como o canvas a desenha (`stageCover`), e a cor
// so vale sem ela. O ponteiro e compartilhado e imutavel, entao o job o le na
// thread de trabalho sem que a lista de fundos do app precise de tranca.
struct MonoBackdrop {
    std::uint32_t width = 0, height = 0;
    double pixelsPerPoint = 0.5;
    float r = 0.0f, g = 0.0f, b = 0.0f;
    std::shared_ptr<const StagePixels> image;
    bool operator==(const MonoBackdrop&) const = default;
};

// O QUE UM RENDER DESENHA: a fatia do documento e, nas quatro rendicoes Mono
// (Clear Light/Dark, Tinted Light/Dark), o que o render faz com ela. Duas
// rendicoes do Mono leem a MESMA fatia (`tinted`) e so o `mono` as separa.
struct RenderLook {
    icf::Context context;
    std::optional<rb::MonoLook> mono;
    // A geracao de design e os efeitos ligados ou nao (`EffectsRenderMode`):
    // parte da chave, como o contexto.
    rb::DesignGeneration generation = rb::DesignGeneration::G27;
    bool effects = true;
    bool operator==(const RenderLook&) const = default;
};

struct RenderRequest {
    std::uint64_t version = 0;   // Session::version() this was made from
    icf::IconBundle bundle;      // a clone: the job reads it while the UI keeps editing
    icf::Context context;
    std::uint32_t size = 512;
    // O ladrilho, na grade de `size`. `w == 0`: o canvas inteiro.
    TileRect tile;
    // Se o buffer do ladrilho passar do teto, o job renderiza o canvas inteiro
    // nesta resolucao. Zero: nao ha para onde cair.
    std::uint32_t fallbackSize = 0;
    // O Mono, quando a rendicao e uma das quatro (Renditions.h, `lookOf`).
    std::optional<rb::MonoLook> mono;
    MonoBackdrop backdrop;
    // QUAL fundo `backdrop` e, para o eco: so preenchido num pedido Mono, que e
    // o unico render que le o fundo. Nos outros fica o padrao, e trocar o fundo
    // nao refaz um render que nao depende dele.
    StageBackground background;
    rb::DesignGeneration generation = rb::DesignGeneration::G27;
    bool effects = true;
    // O MONO SEM O VIDRO (02/10): o job devolve o icone como o render o deixa
    // -- a mascara do Clear, ou o icone ja tingido -- e NAO roda o vidro
    // simulado nem o Clear. Quem os faz e a `StageSource` do app, na GPU da
    // tela e por quadro (Widgets.h, `composeMono`); `backdrop` e o quadrado do
    // `mono` nao sao lidos.
    bool monoRaw = false;
};

struct RenderResult {
    std::uint64_t version = 0;
    // O eco do Mono pedido: parte da chave, como o contexto.
    std::optional<rb::MonoLook> mono;
    // E o do fundo do palco que o Mono leu.
    StageBackground background;
    // Os pixels sao o Mono SEM o vidro (`RenderRequest::monoRaw`), e se eles
    // sao a mascara do Clear (as passadas L/D/H nos canais) ou o icone pronto
    // para ir sobre o vidro.
    bool monoRaw = false, monoClear = false;
    // E o da geracao de design e dos efeitos.
    rb::DesignGeneration generation = rb::DesignGeneration::G27;
    bool effects = true;
    // What this result ANSWERS, echoed back from the request that produced it.
    // The implementor of RenderScheduler must copy it across: the coordinator
    // decides "is this still the frame I am waiting for?" by comparing version,
    // context and size against what it last asked for, and a result that does
    // not carry its context can never be matched -- it would be dropped forever
    // and the canvas would stay empty.
    icf::Context context;
    // O ECO do pedido, que e o que o coordenador compara: `size` e `tile`
    // como foram PEDIDOS, mesmo quando o job caiu para a base.
    std::uint32_t size = 0;
    TileRect tile;
    // O que os pixels SAO: `width` x `height` a partir de (`originX`,
    // `originY`) numa grade de `gridSize`.
    std::uint32_t gridSize = 0;
    std::int32_t originX = 0, originY = 0;
    bool refined = true;   // false: caiu para a base por causa do teto

    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba8;   // straight alpha, R8G8B8A8, row major
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> skipped, shapeGaps, notes;
    std::string error;                 // non-empty: nothing else is valid
};

// "Render this"; the answer arrives later, on the main thread, through poll().
// Only the LATEST request matters: a scheduler may drop any earlier one that has
// not started, and a result older than the last request may be dropped too.
struct RenderScheduler {
    virtual ~RenderScheduler() = default;
    virtual void request(RenderRequest r) = 0;
    virtual std::optional<RenderResult> poll() = 0;
};

// Pixels in, an ImGui texture id out. `update` keeps the id when the size is the
// same; the caller removes and creates when it is not.
struct TextureSink {
    virtual ~TextureSink() = default;
    virtual ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
    virtual bool update(ImTextureID id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) = 0;
    virtual void remove(ImTextureID id) = 0;
};

// UM RENDER QUE MORREU, dito de um jeito que o coordenador consegue OUVIR.
//
// O eco (`version`, `context`, `size`, `tile`) e a razao inteira desta funcao
// existir. Um fracasso que perde a chave nao e um fracasso visivel: ele nao
// casa com `requested_` (RenderCoordinator.cpp), e descartado em todo frame
// para sempre, `pending` nunca cai, e o canvas fica vazio sem nada na tela
// dizendo por que. Quem constroi o resultado na mao acerta a chave quando o
// render deu certo e esquece dela justamente no caminho de erro, que e o
// caminho que ninguem exercita.
//
// `why` vai para `error`, que por contrato quer dizer "nada mais aqui e
// valido" -- entao nao ha pixels, e o coordenador preserva a textura anterior.
RenderResult failedResult(const RenderRequest& r, std::string why);

// "Liquid Glass Effects Disabled": `glass` falso em toda camada de `bundle` (a
// lista de especializacao de `glass` sai junto, senao ela ganharia da chave
// simples). E para uma COPIA: o documento aberto nao muda.
void disableGlassEffects(icf::IconBundle& bundle);

// Straight RGBA floats, as `rb::RenderedIcon::rgba` gives them, to 8 bits:
// clamped to [0,1] and rounded, the same rule `icf::encodePng` applies, so the
// canvas and the PNG show the same byte.
std::vector<std::uint8_t> toRgba8(const std::vector<float>& straightRgba);

}  // namespace ick
