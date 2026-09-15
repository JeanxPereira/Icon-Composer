#include "Source/IconComposerKit/RenderCoordinator.h"

#include <utility>

namespace ick {

RenderCoordinator::~RenderCoordinator() {
    if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
}

void RenderCoordinator::tick(Session& s) {
    const Key now{s.version(), s.view.context, s.view.size};
    if (!everRequested_ || !(now == requested_)) {
        // AGGREGATE initialisation, in declaration order. `RenderRequest r;` does not
        // compile: it holds an `icf::IconBundle`, which has no default constructor
        // (only the private one `open` uses). `RenderRequest` must stay an aggregate.
        RenderRequest r{now.version, s.bundle().clone(), now.context, now.size};
        scheduler_.request(std::move(r));
        requested_ = now;
        everRequested_ = true;
        view_.pending = true;
    }

    while (auto result = scheduler_.poll()) {
        if (result->version < requested_.version) continue;   // stale: the latest wins
        view_.drawn = result->drawn;
        view_.total = result->total;
        view_.skipped = result->skipped;
        view_.shapeGaps = result->shapeGaps;
        view_.notes = result->notes;
        view_.error = result->error;
        if (result->error.empty() && !result->rgba8.empty()) {
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
        if (result->version == requested_.version) view_.pending = false;
    }
}

}  // namespace ick
