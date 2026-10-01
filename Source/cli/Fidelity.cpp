#include "Source/cli/Fidelity.h"

#include <cstdlib>

#include "Source/cli/RenderBundle.h"

namespace iccli {

std::uint8_t fidelityByte(float v) {
    if (!(v > 0.0f)) return 0;
    if (v >= 1.0f) return 255;
    return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
}

FidelityStats compareIcons(const rb::RenderedIcon& a, const rb::RenderedIcon& b) {
    FidelityStats s;
    auto differ = [&](const char* what) {
        if (!s.sameShape) return;
        s.sameShape = false;
        s.shapeWhy = what;
    };
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) {
        differ("dimensoes");
        return s;
    }
    if (a.originX != b.originX || a.originY != b.originY || a.size != b.size) differ("origem");
    if (a.drawn != b.drawn || a.total != b.total) differ("drawn/total");
    if (a.skipped.size() != b.skipped.size()) differ("skipped");
    for (std::size_t i = 0; s.sameShape && i < a.skipped.size(); ++i) {
        if (a.skipped[i].why != b.skipped[i].why || a.skipped[i].layer != b.skipped[i].layer) {
            differ("skipped");
        }
    }
    if (a.shapeGaps != b.shapeGaps) differ("shapeGaps");
    if (a.notes != b.notes) differ("notes");
    if (a.backgroundPainted != b.backgroundPainted || a.backgroundGap != b.backgroundGap) {
        differ("fundo");
    }
    if (a.glassTranslucent != b.glassTranslucent || a.glassRefracted != b.glassRefracted ||
        a.glassSpecular != b.glassSpecular || a.glassShadowed != b.glassShadowed ||
        a.glassShadowOverdrawn != b.glassShadowOverdrawn || a.glassGlowed != b.glassGlowed) {
        differ("contadores de vidro");
    }

    double sum = 0.0;
    const std::size_t n = a.rgba.size();
    for (std::size_t i = 0; i < n; ++i) {
        const int d = std::abs(static_cast<int>(fidelityByte(a.rgba[i])) -
                               static_cast<int>(fidelityByte(b.rgba[i])));
        sum += d;
        if (d > kFidelityMaxCeiling) ++s.over;
        if (d > s.max) {
            s.max = d;
            const std::size_t px = i / 4;
            s.worstX = static_cast<std::uint32_t>(px % a.width);
            s.worstY = static_cast<std::uint32_t>(px / a.width);
            s.worstChannel = static_cast<int>(i % 4);
        }
    }
    s.mean = n ? sum / static_cast<double>(n) : 0.0;
    return s;
}

rb::Result<FidelityStats> fidelityOf(rb::Device& device, const icf::IconBundle& bundle,
                                     std::uint32_t size, icf::Context context,
                                     rb::DesignGeneration generation) {
    auto cpu = renderBundleIcon(device, bundle, size, 16, context, false, nullptr, {}, generation);
    if (!cpu) return std::unexpected("cpu: " + cpu.error());
    auto gpu = renderBundleIcon(device, bundle, size, 16, context, true, nullptr, {}, generation);
    if (!gpu) return std::unexpected("gpu: " + gpu.error());
    return compareIcons(*cpu, *gpu);
}

}  // namespace iccli
