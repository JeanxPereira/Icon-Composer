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
        bool operator==(const Key&) const = default;
    };
    RenderScheduler& scheduler_;
    TextureSink& sink_;
    RenderView view_;
    Key requested_;
    bool everRequested_ = false;
};

}  // namespace ick
