#pragma once
#include <filesystem>

#include "Source/RenderBox/DesignGeneration.h"

namespace icapp {
// Opens the window, with `initial` loaded when it names a bundle. Returns the
// process exit code. `generation` is the design generation the initial document
// opens with; View > Design Generation changes it afterwards.
int run(const std::filesystem::path& initial,
        rb::DesignGeneration generation = rb::DesignGeneration::G27);
}  // namespace icapp
