#include "Source/app/OnyxPorts.h"

#include "Source/RenderBox/IconRenderer.h"

#include <cstdio>
#include <exception>
#include <string>
#include <utility>

// O seletor de pasta e do Onyx desde 19/09 (`SystemOpenFolderDialog`), entao
// nada de COM mora mais aqui -- este arquivo voltou a ser so a ponte.
#include <Onyx/App/UIHelpers.h>

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

ick::RenderResult renderNow(rb::Device& device, const ick::RenderRequest& r,
                            rb::RenderCache* cache) {
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
    io.cache = cache;
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
            // A LIBERACAO SAI DO CAMINHO FELIZ. `~JobScheduler` espera em
            // `!working_`, e so estas duas linhas o baixam -- qualquer escape
            // entre aqui e elas fecharia a janela em cima de uma espera que
            // nunca termina. Onyx CONTEM o throw (Jobs.cpp:180-185: o job
            // "still completes normally"), entao nada aqui aborta o processo
            // e nada aqui reclama: o destrutor simplesmente nunca voltaria.
            // Num destrutor, por isso, e sem lancar.
            struct Release {
                JobScheduler* s;
                ~Release() {
                    {
                        std::lock_guard<std::mutex> lock(s->mutex_);
                        s->working_ = false;
                    }
                    s->idle_.notify_all();
                }
            } release{this};
            // E o resultado tem que CHEGAR. Sem isto `*result` volta como foi
            // construido -- versao 0, contexto vazio -- e o coordenador
            // descarta uma resposta assim em todo frame para sempre (Ports.h,
            // `failedResult`): tela vazia, `pending` eterno, diagnostico
            // nenhum. `renderNow` aloca o buffer inteiro do ladrilho, entao
            // `bad_alloc` e o escape que se espera de verdade aqui.
            try {
                *result = renderNow(device_, *req, &cache_);
            } catch (const std::exception& e) {
                *result = ick::failedResult(*req, std::string("o render lancou: ") + e.what());
            } catch (...) {
                *result = ick::failedResult(*req, "o render lancou algo que nao e std::exception");
            }
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

std::filesystem::path SystemOpenBundleDialog(const std::filesystem::path& startIn,
                                             std::string* why) {
    if (why) why->clear();
    namespace fs = std::filesystem;

    // ONDE O SELETOR ABRE E O QUE DECIDE SE ELE SERVE. Medido 19/09: o
    // seletor reabria DENTRO do ultimo bundle, entao a lista mostrava so o
    // `Assets/` do proprio bundle e o `.icon` nao estava na tela para ser
    // escolhido -- "nao da pra abrir a porra do icon" dito de novo, por outro
    // motivo. Um pacote e escolhivel quando o PAI dele e o que esta listado.
    Onyx::App::FolderDialogOptions options;
    options.title = "Open a .icon bundle";
    options.startIn = startIn;
    // Um balde de MRU so deste pedido: sem GUID, todo dialogo do processo
    // divide um so, e um Save As em outro canto passa a decidir onde este
    // abre.
    options.mruKey = "icon-composer/open-bundle";

    fs::path picked = Onyx::App::SystemOpenFolderDialog(options, why);
    if (picked.empty()) return picked;   // cancelou, ou `why` ja diz o que houve

    // PERDAO DE UM NIVEL. A pessoa que entra no bundle para "ver se e esse" e
    // aperta Selecionar pasta escolhe `Assets/`, ou o proprio bundle depois de
    // entrar nele. Um editor que responde "isto nao e um .icon" a quem estava
    // com o dedo em cima do documento certo esta tecnicamente correto e
    // praticamente inutil.
    std::error_code ec;
    if (!fs::exists(picked / "icon.json", ec)) {
        const fs::path up = picked.parent_path();
        if (!up.empty() && fs::exists(up / "icon.json", ec)) return up;
    }
    return picked;
}

}  // namespace icapp
