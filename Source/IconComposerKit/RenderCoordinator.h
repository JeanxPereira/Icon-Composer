#pragma once
// From "the document changed" to "the canvas has a texture", once per frame.
//
// THE LATEST WINS (spec 13/09 §6). A request carries the Session version it was
// made from; a result older than the last request is dropped, because the
// canvas must never step backwards. Zoom is not a render: it is the same pixels
// shown larger.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Ports.h"
#include "Source/IconComposerKit/Session.h"

#include <chrono>
#include <cstdint>

namespace ick {

class RenderCoordinator {
public:
    RenderCoordinator(RenderScheduler& scheduler, TextureSink& sink) : scheduler_(scheduler), sink_(sink) {}
    ~RenderCoordinator();
    RenderCoordinator(const RenderCoordinator&) = delete;
    RenderCoordinator& operator=(const RenderCoordinator&) = delete;

    void tick(Session& s);
    const RenderView& view() const { return view_; }

private:
    struct Key {
        std::uint64_t version = 0;
        icf::Context context;
        std::uint32_t size = 0;
        // O ladrilho pedido, na grade de `size`. Vazio (`w == 0`) = a base,
        // o icone inteiro.
        TileRect tile;
        // O Mono (Renditions.h, `lookOf`): duas rendicoes da mesma fatia.
        std::optional<rb::MonoLook> mono;
        // O fundo do palco, so num Mono (Ports.h, `RenderRequest::background`).
        StageBackground background;
        rb::DesignGeneration generation = rb::DesignGeneration::G27;
        bool effects = true;
        bool operator==(const Key&) const = default;
    };
    // O que `tick` quer ver na tela neste quadro, e o que ja esta.
    void apply(const RenderResult& r, bool asTile);

    RenderScheduler& scheduler_;
    TextureSink& sink_;
    RenderView view_;
    // DUAS CAMADAS, UMA RAIA (30/09). A base (o icone inteiro) e o ladrilho
    // (o que se ve, nitido, com zoom alto) saem do mesmo agendador, que e de
    // um pedido por vez e o mais novo substitui o que espera. Entao a ordem e
    // decidida aqui: a base primeiro, sempre -- sem ela nao ha o que mostrar
    // durante um zoom --, e o ladrilho so com a base em dia.
    Key base_, tile_;          // o que esta na tela
    Key inFlight_;             // o ultimo pedido feito
    bool flying_ = false;
    bool haveBase_ = false, haveTile_ = false;
    // When the request now in flight was made. Wall time from HERE, and not from
    // inside the render job, because the queue is part of what the person waits
    // for.
    std::chrono::steady_clock::time_point requestedAt_{};
};

}  // namespace ick
