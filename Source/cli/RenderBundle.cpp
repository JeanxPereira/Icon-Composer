#include "Source/cli/RenderBundle.h"

namespace iccli {

rb::Result<rb::RenderedIcon> renderBundleIcon(rb::Device& device, const icf::IconBundle& bundle,
                                              std::uint32_t size, int subdivisions,
                                              icf::Context context, bool gpu,
                                              rb::RenderCache* cache, rb::IconViewport viewport) {
    rb::IconRenderOptions io;
    io.size = size;
    io.subdivisions = subdivisions;
    io.context = context;
    io.cache = cache;
    io.viewport = viewport;
    return gpu ? rb::renderIconGpu(device, bundle, io) : rb::renderIcon(device, bundle, io);
}

}  // namespace iccli
