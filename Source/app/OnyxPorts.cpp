#include "Source/app/OnyxPorts.h"

#include "Source/RenderBox/IconRenderer.h"

#include <cstdio>
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
    // waiting for by the WHOLE key -- version, context and size (spec 13/09
    // §6, and Ports.h's own note on this field). A result that does not carry
    // back the context it answers can never match: it would be dropped on every
    // frame, forever, and the canvas would stay empty with nothing on screen to
    // say why.
    out.context = r.context;
    // And `size`/`tile` are the rest of that key, so they have to be set even
    // when there are no pixels -- otherwise a render that FAILED is also
    // dropped forever, and the error never reaches the diagnostics panel. This
    // is what was ASKED, not what the job ended up drawing: it stays as asked
    // even when the job below falls back to the base resolution.
    out.size = r.size;
    out.tile = r.tile;

    rb::IconRenderOptions io;
    io.size = r.size;
    io.context = r.context;
    io.viewport = rb::IconViewport{r.tile.x, r.tile.y, r.tile.w, r.tile.h};
    auto icon = rb::renderIcon(device, r.bundle, io);

    // Um ladrilho pode ser impossivel de desenhar de duas formas (spec
    // 2026-09-16, "O teto de area"):
    //  - o teto de AREA: `renderIcon` devolve com sucesso e `viewportRefused`,
    //    porque nada foi desenhado -- nao e um erro, e o convite explicito da
    //    spec para cair na base;
    //  - um teto de APARELHO que o teto de area nao cobre
    //    (`CoveragePass.cpp:290-300`, `maxViewportDimensions` /
    //    `viewportBoundsRange`), que chega como ERRO. So um ladrilho de fato
    //    (`tile.w > 0`) cai aqui: um pedido de canvas inteiro que falha e uma
    //    falha real e tem que aparecer como tal, nunca virar um render de base
    //    silencioso.
    const bool areaCapped = icon.has_value() && icon->viewportRefused;
    const bool tileErrored = !icon.has_value() && r.tile.w > 0;
    std::string tileError;
    if (tileErrored) tileError = icon.error();
    // A nota do teto de area ("viewport acima do teto de area: nada
    // desenhado", `IconRenderer.cpp:636-638`) mora no `icon` RECUSADO, que
    // esta prestes a ser sobrescrito pela tentativa de base -- capturada
    // ANTES, pela mesma razao que `tileError` acima: sem isso a nota
    // desapareceria em silencio toda vez que a base desse certo.
    std::vector<std::string> areaCapNotes;
    if (areaCapped) areaCapNotes = icon->notes;

    if ((areaCapped || tileErrored) && r.fallbackSize > 0) {
        // O teto (spec 2026-09-16): o canvas inteiro na resolucao base, e o
        // painel estica -- o que ele fazia antes desta frente.
        io.size = r.fallbackSize;
        io.viewport = rb::IconViewport{};
        icon = rb::renderIcon(device, r.bundle, io);
        out.refined = false;
        if (icon.has_value()) {
            if (!tileError.empty()) {
                // O motivo da queda nao pode desaparecer so porque a base deu
                // certo: `error` nao-vazio diz "nada mais e valido" (Ports.h), e
                // este resultado E valido, entao o motivo vai para as notas.
                out.notes.push_back("ladrilho recusado, caiu para a base: " + tileError);
            }
            out.notes.insert(out.notes.end(), areaCapNotes.begin(), areaCapNotes.end());
        }
    }

    if (!icon.has_value()) {
        // Falha real: a base tambem nao rendeu, ou nao havia base para cair
        // (`fallbackSize == 0`). O erro do ladrilho, se houver, nao pode
        // desaparecer atras do erro da base.
        out.error = (!tileError.empty() && tileError != icon.error())
                        ? "ladrilho: " + tileError + "; base: " + icon.error()
                        : icon.error();
        return out;
    }
    out.width = icon->width;
    out.height = icon->height;
    out.rgba8 = ick::toRgba8(icon->rgba);
    out.gridSize = icon->size;
    out.originX = icon->originX;
    out.originY = icon->originY;
    out.drawn = icon->drawn;
    out.total = icon->total;
    for (const auto& s : icon->skipped) {
        out.skipped.push_back("grupo " + std::to_string(s.group) + " / " + s.layer + ": " + s.why);
    }
    out.shapeGaps = icon->shapeGaps;
    out.notes.insert(out.notes.end(), icon->notes.begin(), icon->notes.end());
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

std::filesystem::path SystemOpenBundleDialog(std::string* why) {
    if (why) why->clear();
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
    const HRESULT made = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_IFileOpenDialog, reinterpret_cast<void**>(&dialog));
    if (FAILED(made)) {
        // The silent branch that made this port indistinguishable from a
        // cancel. It has never been seen to fire -- a probe of everything up
        // to `Show()` passes on this machine both with a virgin apartment and
        // with GLFW's -- and that is exactly why it has to SAY so if it ever
        // does, instead of answering "" like a person who changed their mind.
        char buf[96];
        std::snprintf(buf, sizeof buf, "the folder picker could not be created (HRESULT 0x%08lX)",
                      static_cast<unsigned long>(made));
        if (why) *why = buf;
    }
    if (SUCCEEDED(made)) {
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
