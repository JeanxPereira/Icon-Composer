#pragma once
// The Kit's two ports, made of the app's own window and of the RenderBox device.
//
// Spec 13/09 §3: the Kit declares `TextureSink` and `RenderScheduler` and never
// implements them. Here is where they become a real GPU upload and a real
// worker thread -- and this file is on the only side of the wall that knows
// there is a window at all.
#include "Source/IconComposerKit/Ports.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/RenderCache.h"

#include "Source/app/Dialogs.h"
#include "Source/app/JobQueue.h"
#include "Source/app/TexturePool.h"

#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>

namespace icapp {

class PoolTextureSink : public ick::TextureSink {
public:
    explicit PoolTextureSink(const Gpu& gpu) : pool_(gpu) {}
    ImTextureID create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    bool update(ImTextureID id, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) override;
    void remove(ImTextureID id) override { pool_.remove(id); }
    // Once per drawn frame. The pool retires nothing until this has walked
    // `kFramesInFlight` past the remove() -- a frame still on the GPU may hold
    // a draw command against the id (TexturePool.h).
    void advanceFrame() { pool_.advanceFrame(); }

private:
    TexturePool pool_;
};

// One lane, one device -- and the lane is NOT the whole of the exclusion
// (revisao 19/09, C1). The JobQueue serialises the renders OF THIS LANE: two
// jobs of this scheduler never sit on the `rb::Device` at once. What it does
// not do is exclude the SECOND consumer of that same device. The export
// (`State::drainExport`, Source/app/Window.cpp) calls `rb::renderIcon` on the
// FRAME thread, over this very `rb::Device`, whose `VkCommandPool` and
// `VkQueue` are externally-synchronised objects; clicking Export with a
// thumbnail in flight reached exactly that. Until 19/09 this header said "the
// JobQueue serialises the lane, so two renders never share the device (spec
// 13/09 §6)", and that sentence had been false since the export existed -- it
// is the same claim the review caught next to `State::device`, left standing
// here, on the class's own declaration.
//
// Today two renders really never share the device, but for the OPPOSITE
// reason to the one the old sentence gave: not the JobQueue, the mux's LEASE.
// It lives in `ick::SharedScheduler` (`Source/IconComposerKit/Renditions.h`):
// `tryLease`/`endLease`, taken through the RAII `SharedScheduler::Lease`, and
// the mux is the only door either consumer goes through -- while the lease is
// up, `pump()` submits nothing to this scheduler at all.
//
// The latest request replaces any earlier one still pending.
class JobScheduler : public ick::RenderScheduler {
public:
    JobScheduler(JobQueue& jobs, rb::Device& device) : jobs_(jobs), device_(device) {}
    // Waits for a render that is ON a worker right now. The job's Work closure
    // holds `this` and `device_`, and the JobQueue outlives this object (the
    // Window owns the Workspace that owns it), so without the wait a render in
    // flight when the window closes would read a destroyed scheduler.
    ~JobScheduler() override;

    void request(ick::RenderRequest r) override;
    std::optional<ick::RenderResult> poll() override;

private:
    void submitPending();
    JobQueue& jobs_;
    rb::Device& device_;
    std::mutex mutex_;
    std::condition_variable idle_;
    std::optional<ick::RenderRequest> pending_;   // waiting for the running job to finish
    bool working_ = false;   // the Work callback is on a worker thread right now
    bool running_ = false;   // submitted and its Done has not run yet
    std::optional<ick::RenderResult> done_;
    // What one render leaves for the next. Only the job in flight touches it,
    // and there is never more than one (`running_`).
    rb::RenderCache cache_;
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
ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r,
                            rb::RenderCache* cache = nullptr);

// Asks for a `.icon` to open. Empty when the user cancels.
//
// `why` separates the two ways of answering empty. A cancel leaves it empty
// too; a failure before the dialog is even shown writes the reason there. The
// caller cannot tell them apart otherwise, and the difference is the whole
// diagnosis when someone reports that "the menu does nothing" -- measured
// 18/09 against a report of exactly that, where the cause turned out to be a
// DIFFERENT File menu (the old toolkit drew one above ours) and not this
// port at all.
// A probe of this function's COM path, everything up to `Show()`, passes both
// with a virgin apartment and with the one GLFW has already entered.
//
// The picker itself is `openFolderDialog` (Dialogs.h) -- asking for a
// DIRECTORY is not an icon problem, it is every host whose document is a
// bundle. What stays here is the part that is about `.icon`:
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
