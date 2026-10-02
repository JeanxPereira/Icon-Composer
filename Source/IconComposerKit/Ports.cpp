#include "Source/IconComposerKit/Ports.h"

#include <cmath>
#include <utility>

namespace ick {

RenderResult failedResult(const RenderRequest& r, std::string why) {
    RenderResult out;
    // A chave, os quatro campos, e nada mais: sem `gridSize`, sem `origin`,
    // sem pixels -- `error` nao-vazio diz que nada mais e valido.
    out.version = r.version;
    out.context = r.context;
    out.size = r.size;
    out.tile = r.tile;
    out.mono = r.mono;
    out.background = r.background;
    out.monoRaw = r.monoRaw;
    out.generation = r.generation;
    out.effects = r.effects;
    out.error = std::move(why);
    return out;
}

void disableGlassEffects(icf::IconBundle& bundle) {
    icf::json::Value* groups = bundle.json().find("groups");
    if (!groups) return;
    for (icf::json::Value& group : groups->elements()) {
        icf::json::Value* layers = group.find("layers");
        if (!layers) continue;
        for (icf::json::Value& layer : layers->elements()) {
            std::erase_if(layer.members(),
                          [](const auto& kv) { return kv.first == "glass-specializations"; });
            layer.set("glass", icf::json::Value::boolean(false));
        }
    }
}

std::vector<std::uint8_t> toRgba8(const std::vector<float>& in) {
    std::vector<std::uint8_t> out(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        float v = in[i];
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        out[i] = static_cast<std::uint8_t>(std::lround(v * 255.0f));
    }
    return out;
}

}  // namespace ick
