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

}  // namespace icapp
