#include "Source/app/OnyxPorts.h"

#include "Source/RenderBox/IconRenderer.h"

#include <string>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#else
#include <Onyx/App/UIHelpers.h>
#endif

namespace icapp {

ImTextureID OnyxTextureSink::create(std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba8) {
    std::string err;
    return pool_.Create(w, h, rgba8, err);
}

bool OnyxTextureSink::update(ImTextureID id, std::uint32_t, std::uint32_t, const std::uint8_t* rgba8) {
    // The pool takes no size here: it refuses a resize in place, and the
    // coordinator already removes and recreates when the size moved.
    std::string err;
    return pool_.Update(id, rgba8, err);
}

ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r) {
    ick::RenderResult out;
    out.version = r.version;
    // THE ECHO. The coordinator matches a result against the request it is
    // waiting for by the WHOLE key -- version, context and width (spec 13/09
    // §6, and Ports.h's own note on this field). A result that does not carry
    // back the context it answers can never match: it would be dropped on every
    // frame, forever, and the canvas would stay empty with nothing on screen to
    // say why.
    out.context = r.context;
    // And the width is the size half of that key, so it has to be set even when
    // there are no pixels -- otherwise a render that FAILED is also dropped
    // forever, and the error never reaches the diagnostics panel.
    out.width = r.size;

    rb::IconRenderOptions io;
    io.size = r.size;
    io.context = r.context;
    auto icon = rb::renderIcon(device, r.bundle, io);
    if (!icon) {
        out.error = icon.error();
        return out;
    }
    out.width = icon->width;
    out.height = icon->height;
    out.rgba8 = ick::toRgba8(icon->rgba);
    out.drawn = icon->drawn;
    out.total = icon->total;
    for (const auto& s : icon->skipped) {
        out.skipped.push_back("grupo " + std::to_string(s.group) + " / " + s.layer + ": " + s.why);
    }
    out.shapeGaps = icon->shapeGaps;
    out.notes = icon->notes;
    return out;
}

JobScheduler::~JobScheduler() {
    std::unique_lock<std::mutex> lock(mutex_);
    pending_.reset();   // nothing queued here will be submitted after this
    idle_.wait(lock, [this] { return !working_; });
}

void JobScheduler::request(ick::RenderRequest r) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(r);   // replaces whatever was waiting: the latest wins
    }
    submitPending();
}

void JobScheduler::submitPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_ || !pending_) return;
    running_ = true;
    working_ = true;
    auto req = std::make_shared<ick::RenderRequest>(std::move(*pending_));
    pending_.reset();
    auto result = std::make_shared<ick::RenderResult>();
    jobs_.Submit(
        /*lane*/ 1,
        [this, req, result](Onyx::Services::Progress&) {
            *result = renderNow(device_, *req);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                working_ = false;
            }
            idle_.notify_all();
        },
        [this, result] {
            // Done runs on the main thread, inside Pump().
            {
                std::lock_guard<std::mutex> lock(mutex_);
                done_ = std::move(*result);
                running_ = false;
            }
            submitPending();
        });
}

std::optional<ick::RenderResult> JobScheduler::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<ick::RenderResult> r = std::move(done_);
    done_.reset();
    return r;
}

void SyncScheduler::request(ick::RenderRequest r) { done_ = renderNow(device_, r); }

std::optional<ick::RenderResult> SyncScheduler::poll() {
    std::optional<ick::RenderResult> r = std::move(done_);
    done_.reset();
    return r;
}

std::filesystem::path SystemOpenBundleDialog() {
#ifdef _WIN32
    // GLFW already put the main thread in an apartment (`glfwInit` calls
    // CoInitializeEx), so this returns S_FALSE rather than doing the work --
    // which still has to be balanced. RPC_E_CHANGED_MODE would mean someone
    // chose the other model: COM is up either way, so the dialog goes ahead
    // and only the CoUninitialize is skipped.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool balance = SUCCEEDED(init);

    std::filesystem::path out;
    IFileOpenDialog* dialog = nullptr;
    // The explicit IID rather than IID_PPV_ARGS: `__uuidof` is a compiler
    // extension and this file is built by both g++ and cl.
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog)))) {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            // FORCEFILESYSTEM keeps the answer to something SIGDN_FILESYSPATH
            // can name -- without it the picker also offers This PC and the
            // libraries, which have no path on disk.
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
        }
        dialog->SetTitle(L"Open a .icon bundle");
        // No owner window, as Onyx's own dialogs pass none.
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR wide = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide) {
                    out = std::filesystem::path(wide);
                    CoTaskMemFree(wide);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (balance) CoUninitialize();
    return out;
#else
    // No folder picker here yet: the caller reduces a file to the bundle that
    // holds it, which is what this app did on every platform before.
    return std::filesystem::path(SystemOpenFileDialog({{"Icon Composer document", {"json"}}}));
#endif
}

}  // namespace icapp
