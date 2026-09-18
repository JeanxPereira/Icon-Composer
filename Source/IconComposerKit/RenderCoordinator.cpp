#include "Source/IconComposerKit/RenderCoordinator.h"

#include <utility>

namespace ick {

RenderCoordinator::~RenderCoordinator() {
    if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
}

void RenderCoordinator::tick(Session& s) {
    // O LADRILHO (spec 2026-09-16, "O que o Kit faz"). O canvas escreve
    // `tileSize`/`tile` quando o pan e o zoom param; aqui isso e so mais um
    // pedaco da chave. `tileSize == 0` (zoom <= 1, ou nada pintado) e o pedido
    // de sempre: o canvas inteiro na resolucao base.
    const ViewContext& v = s.view;
    const bool tiled = v.tileSize > 0 && v.tile.w > 0;
    const Key now{s.version(), v.context, tiled ? v.tileSize : v.size,
                  tiled ? v.tile : TileRect{}};
    if (!everRequested_ || !(now == requested_)) {
        // AGGREGATE initialisation, in declaration order. `RenderRequest r;` does not
        // compile: it holds an `icf::IconBundle`, which has no default constructor
        // (only the private one `open` uses). `RenderRequest` must stay an aggregate.
        // Every field is named, so a field added to the struct and forgotten here
        // is a warning rather than a silent default.
        //
        // O ultimo, `fallbackSize`, e PARA ONDE CAIR quando o ladrilho nao
        // couber (o teto de area ou o do aparelho): o canvas inteiro na
        // resolucao base, que e o que o painel mostrava antes desta frente.
        // Nunca zero -- `ViewContext::size` e 512 ou 1024 --, e e de proposito:
        // com uma base para onde cair, o job nunca devolve o estado "recusado e
        // sem pixels", em que nada foi desenhado e nem ha erro para mostrar.
        RenderRequest r{now.version, s.bundle().clone(), now.context, now.size, now.tile, v.size};
        scheduler_.request(std::move(r));
        requested_ = now;
        everRequested_ = true;
        view_.pending = true;
        requestedAt_ = std::chrono::steady_clock::now();
        view_.pendingSeconds = 0.0;
    }

    // Ticked every frame while something is in flight, so the panel can say how
    // long the person has been waiting BEFORE the answer arrives. That is the
    // whole point: a render that never finishes produces no result to time, and
    // "pending" alone cannot tell forty milliseconds from eighty-seven seconds.
    if (view_.pending) {
        view_.pendingSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - requestedAt_).count();
    }

    while (auto result = scheduler_.poll()) {
        // A result is the one being waited for only when it answers the WHOLE key.
        // Comparing the version alone accepts a stale frame the moment the view
        // context moves: `size` and `context` live on `Session::view`, which is
        // not behind a command, so changing either leaves the version where it
        // was. Ask for 512, drag the size to 1024, and the 512 that lands carries
        // the same version -- it would be shown as if it were the answer, and
        // `pending` would go false, so nothing would ever correct it. The tile
        // is in the key for the same reason: a pan that stops one tile to the
        // left is a different question, with the same version and the same size.
        //
        // `size` and `tile` here are the ECHO of what was asked (Ports.h), not
        // what the pixels turned out to be: a job that fell back to the base
        // still answers the tile it was asked for, and `width`/`gridSize` are
        // where that shows.
        const Key answered{result->version, result->context, result->size, result->tile};
        if (!(answered == requested_)) continue;   // stale: the latest wins
        view_.drawn = result->drawn;
        view_.total = result->total;
        view_.skipped = result->skipped;
        view_.shapeGaps = result->shapeGaps;
        view_.notes = result->notes;
        view_.error = result->error;
        if (result->error.empty() && !result->rgba8.empty()) {
            // ONDE OS PIXELS FICAM, junto com os pixels e nunca sem eles: o
            // painel poe a textura no retangulo que estes quatro numeros
            // descrevem (spec 2026-09-16, "O que o Kit faz"), entao uma grade
            // adotada sem a textura correspondente colocaria a textura ANTIGA
            // no lugar da nova. Nos dois ramos, porque a textura so e recriada
            // quando a extensao muda e a grade pode mudar sem ela: dois
            // ladrilhos do mesmo tamanho em origens diferentes passam pelo
            // `update`.
            view_.gridSize = result->gridSize;
            view_.originX = result->originX;
            view_.originY = result->originY;
            view_.refined = result->refined;
            if (view_.texture != ImTextureID_Invalid && view_.width == result->width &&
                view_.height == result->height) {
                sink_.update(view_.texture, result->width, result->height, result->rgba8.data());
            } else {
                if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
                view_.texture = sink_.create(result->width, result->height, result->rgba8.data());
                view_.width = result->width;
                view_.height = result->height;
            }
        }
        view_.lastRenderSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - requestedAt_).count();
        view_.pendingSeconds = 0.0;
        view_.pending = false;   // it matched the whole key, so it IS the answer
    }
}

}  // namespace ick
