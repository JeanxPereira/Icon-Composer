// IconComposer -- the editor.
//
//   IconComposer [bundle.icon] [--generation 26|27]
//   IconComposer --version
//   IconComposer --selftest <bundle.icon> [--frames N]
//
// The selftest is the assertion that "the UI opens" (spec 13/09 §8): every
// panel drawn every frame in a context with no backend, against a REAL device
// through the synchronous scheduler, so the pixels and the round-trip are both
// answered on a machine nobody is looking at.
#include "Source/IconComposerKit/SelfTest.h"
#include "Source/app/AppPorts.h"
#include "Source/app/Window.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

// This is a console-subsystem program, so a launch from a file manager or a
// shortcut gets a console created FOR it: an empty black window beside the
// editor. `GetConsoleProcessList` reporting exactly ONE attached process IS
// that case -- a console inherited from a shell has the shell in the list too,
// so a run from PowerShell keeps its stderr, which is what the selftest and
// every diagnostic line depend on. Hidden, not freed: stderr keeps a place to go.
void hideAConsoleMadeForUs() {
#if defined(_WIN32)
    DWORD attached = 0;
    if (GetConsoleProcessList(&attached, 1) != 1) return;
    if (HWND console = GetConsoleWindow(); console != nullptr) ShowWindow(console, SW_HIDE);
#endif
}

// The selftest asserts that the canvas HAD a texture, not that a GPU accepted
// it: there is no window here, so there is no pool to upload into.
struct NullSink : ick::TextureSink {
    ImTextureID create(std::uint32_t, std::uint32_t, const std::uint8_t*) override {
        return static_cast<ImTextureID>(1);
    }
    bool update(ImTextureID, std::uint32_t, std::uint32_t, const std::uint8_t*) override { return true; }
    void remove(ImTextureID) override {}
};

int selftest(const std::string& bundle, int frames) {
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "IconComposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }
    icapp::SyncScheduler scheduler(*device);
    NullSink sink;
    const ick::SelfTestReport r = ick::runSelfTest(bundle, scheduler, sink, frames);
    std::printf("%s\n", ick::describe(r).c_str());
    const bool ok = r.failure.empty() && r.bytesRoundTripped && r.textured && r.imguiErrors == 0;
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--version") == 0) {
        std::puts("IconComposer " IC_VERSION);
        return 0;
    }
    hideAConsoleMadeForUs();
    if (argc >= 3 && std::strcmp(argv[1], "--selftest") == 0) {
        int frames = 30;
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--frames") == 0) frames = std::atoi(argv[++i]);
        }
        return selftest(argv[2], frames);
    }
    // `IconComposer <bundle.icon> --generation 26`: abre ja na geracao pedida.
    rb::DesignGeneration generation = rb::DesignGeneration::G27;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--generation") == 0 && std::strcmp(argv[i + 1], "26") == 0) {
            generation = rb::DesignGeneration::G26;
        }
    }
    return icapp::run(argc >= 2 ? argv[1] : "", generation);
}
