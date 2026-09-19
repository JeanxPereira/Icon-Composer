#pragma once
// The Kit's two ports, made of Onyx and of the RenderBox device.
//
// Spec 13/09 §3: the Kit declares `TextureSink` and `RenderScheduler` and never
// implements them. Here is where they become a real GPU upload and a real
// worker thread -- and this file is on the only side of the wall that is
// allowed to say the word Onyx.
#include "Source/IconComposerKit/Ports.h"
#include "Source/RenderBox/Device.h"

#include <Onyx/App/TexturePool.h>
#include <Onyx/Services/Jobs.h>

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace icapp {

class OnyxTextureSink : public ick::TextureSink {
public:
    explicit OnyxTextureSink(Onyx::Rendering::VkContext& ctx) : pool_(ctx) {}
    ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    bool update(ImTextureID id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    void remove(ImTextureID id) override { pool_.Remove(id); }
    // Once per drawn frame. The pool retires nothing until this has walked
    // `kFramesInFlight` past the Remove() -- a frame still on the GPU may hold
    // a draw command against the id (Onyx/App/TexturePool.h).
    void advanceFrame() { pool_.AdvanceFrame(); }

private:
    Onyx::App::TexturePool pool_;
};

// One lane, one device: the JobQueue serialises the lane, so two renders never
// share the device (spec 13/09 §6). The latest request replaces any earlier one
// still pending.
class JobScheduler : public ick::RenderScheduler {
public:
    JobScheduler(Onyx::Services::JobQueue& jobs, rb::Device& device) : jobs_(jobs), device_(device) {}
    // Waits for a render that is ON a worker right now. The job's Work closure
    // holds `this` and `device_`, and the JobQueue outlives this object (the
    // Window owns the Workspace that owns it), so without the wait a render in
    // flight when the window closes would read a destroyed scheduler.
    ~JobScheduler() override;

    void request(ick::RenderRequest r) override;
    std::optional<ick::RenderResult> poll() override;

private:
    void submitPending();
    Onyx::Services::JobQueue& jobs_;
    rb::Device& device_;
    std::mutex mutex_;
    std::condition_variable idle_;
    std::optional<ick::RenderRequest> pending_;   // waiting for the running job to finish
    bool working_ = false;   // the Work callback is on a worker thread right now
    bool running_ = false;   // submitted and its Done has not run yet
    std::optional<ick::RenderResult> done_;
};

// Renders on the calling thread. The selftest's scheduler.
class SyncScheduler : public ick::RenderScheduler {
public:
    explicit SyncScheduler(rb::Device& device) : device_(device) {}
    void request(ick::RenderRequest r) override;
    std::optional<ick::RenderResult> poll() override;

private:
    rb::Device& device_;
    std::optional<ick::RenderResult> done_;
};

// The one function both schedulers call.
ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r);

// Asks for a `.icon` to open. Empty when the user cancels.
//
// `why` separates the two ways of answering empty. A cancel leaves it empty
// too; a failure before the dialog is even shown writes the reason there. The
// caller cannot tell them apart otherwise, and the difference is the whole
// diagnosis when someone reports that "the menu does nothing" -- measured
// 18/09 against a report of exactly that, where the cause turned out to be a
// DIFFERENT File menu (Onyx draws one above ours) and not this port at all.
// A probe of this function's COM path, everything up to `Show()`, passes both
// with a virgin apartment and with the one GLFW has already entered.
//
// The picker itself is now Onyx's (`SystemOpenFolderDialog`, UIHelpers.h) --
// asking for a DIRECTORY is not an icon problem, it is every host whose
// document is a bundle. What stays here is the part that is about `.icon`:
//
// `startIn` is the folder whose CONTENTS the dialog lists, and it decides
// whether this dialog is usable at all. Pass the PARENT of a bundle, never a
// bundle: a picker opened inside one lists that bundle's own contents, and
// the bundle is then not on screen to be chosen. Measured 19/09 against
// exactly that failure. Empty means "wherever this dialog was last".
//
// And the one-level forgiveness: a pick that is not a bundle but whose PARENT
// is one (`Assets/`, the folder someone lands on after stepping in to look)
// opens the bundle instead of being refused.
//
// `why` separates the two ways of answering empty. A cancel leaves it empty
// too; a failure before the dialog is even shown writes the reason there.
std::filesystem::path SystemOpenBundleDialog(const std::filesystem::path& startIn = {},
                                             std::string* why = nullptr);

}  // namespace icapp
