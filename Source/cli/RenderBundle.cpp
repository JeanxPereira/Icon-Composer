#include "Source/cli/RenderBundle.h"

namespace iccli {

rb::Result<rb::RenderedIcon> renderBundleIcon(rb::Device& device, const icf::IconBundle& bundle,
                                              std::uint32_t size, int subdivisions,
                                              icf::Context context, bool gpu,
                                              rb::RenderCache* cache, rb::IconViewport viewport,
                                              rb::DesignGeneration generation) {
    rb::IconRenderOptions io;
    io.size = size;
    io.subdivisions = subdivisions;
    io.context = context;
    io.cache = cache;
    io.viewport = viewport;
    io.generation = generation;
    return gpu ? rb::renderIconGpu(device, bundle, io) : rb::renderIcon(device, bundle, io);
}

std::optional<rb::DesignGeneration> designGenerationFromString(std::string_view text) {
    if (text == "26") return rb::DesignGeneration::G26;
    if (text == "27") return rb::DesignGeneration::G27;
    return std::nullopt;
}

}  // namespace iccli
