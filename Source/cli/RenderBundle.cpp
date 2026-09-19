#include "Source/cli/RenderBundle.h"

namespace iccli {

rb::Result<rb::RenderedIcon> renderBundleIcon(rb::Device& device, const icf::IconBundle& bundle,
                                              std::uint32_t size, int subdivisions,
                                              icf::Context context) {
    rb::IconRenderOptions io;
    io.size = size;
    io.subdivisions = subdivisions;
    io.context = context;
    return rb::renderIcon(device, bundle, io);
}

}  // namespace iccli
