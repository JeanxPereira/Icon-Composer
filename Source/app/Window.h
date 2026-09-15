#pragma once
#include <filesystem>

namespace icapp {
// Opens the window, with `initial` loaded when it names a bundle. Returns the
// process exit code.
int run(const std::filesystem::path& initial);
}  // namespace icapp
