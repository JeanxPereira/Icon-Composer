// iconcomposer -- the editor.
//
//   iconcomposer [bundle.icon]
//   iconcomposer --selftest <bundle.icon> [--frames N]
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

namespace {

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
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
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
    if (argc >= 3 && std::strcmp(argv[1], "--selftest") == 0) {
        int frames = 30;
        for (int i = 3; i + 1 < argc; ++i) {
            if (std::strcmp(argv[i], "--frames") == 0) frames = std::atoi(argv[++i]);
        }
        return selftest(argv[2], frames);
    }
    return icapp::run(argc >= 2 ? argv[1] : "");
}
