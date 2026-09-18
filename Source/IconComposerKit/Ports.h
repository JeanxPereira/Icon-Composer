#pragma once
// The two things a window gives the Kit, as interfaces the Kit never implements.
//
// Rule 2 of the architecture spec: only Source/app links Onyx. So the upload of
// pixels to the GPU the window owns, and the thread the render runs on, are
// abstract here and concrete there -- and null in every test, which is what
// makes the whole UI exercisable on a machine with no device.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerKit/Tile.h"
#include "imgui.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ick {

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
};

struct RenderResult {
    std::uint64_t version = 0;
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

// Straight RGBA floats, as `rb::RenderedIcon::rgba` gives them, to 8 bits:
// clamped to [0,1] and rounded, the same rule `icf::encodePng` applies, so the
// canvas and the PNG show the same byte.
std::vector<std::uint8_t> toRgba8(const std::vector<float>& straightRgba);

}  // namespace ick
