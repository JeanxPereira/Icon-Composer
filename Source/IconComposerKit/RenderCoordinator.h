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
        // O ladrilho pedido, na grade de `size` (spec 2026-09-16, "O que o Kit
        // faz"). Parte da chave como o resto: dois ladrilhos da MESMA grade
        // sao dois pedidos diferentes, e um resultado que responde o vizinho
        // nao responde este.
        TileRect tile;
        bool operator==(const Key&) const = default;
    };
    RenderScheduler& scheduler_;
    TextureSink& sink_;
    RenderView view_;
    Key requested_;
    bool everRequested_ = false;
    // When the request now in flight was made. Wall time from HERE, and not from
    // inside the render job, because the queue is part of what the person waits
    // for: a scheduler that is already busy with an 87 second render will not
    // start this one for 87 seconds, and a clock that only timed the draw would
    // report a fast render while the canvas stayed empty.
    std::chrono::steady_clock::time_point requestedAt_{};
};

}  // namespace ick
