#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Renditions.h"
#include "Source/IconComposerKit/Widgets.h"

#include <utility>

namespace ick {

RenderCoordinator::~RenderCoordinator() {
    if (view_.texture != ImTextureID_Invalid) sink_.remove(view_.texture);
    if (view_.tileTexture != ImTextureID_Invalid) sink_.remove(view_.tileTexture);
}

namespace {

// Uma textura: atualizada no lugar quando o tamanho e o mesmo, recriada senao.
void upload(TextureSink& sink, ImTextureID& tex, std::uint32_t& w, std::uint32_t& h,
            const RenderResult& r) {
    if (tex != ImTextureID_Invalid && w == r.width && h == r.height) {
        sink.update(tex, r.width, r.height, r.rgba8.data());
        return;
    }
    if (tex != ImTextureID_Invalid) sink.remove(tex);
    tex = sink.create(r.width, r.height, r.rgba8.data());
    w = r.width;
    h = r.height;
}

}  // namespace

void RenderCoordinator::apply(const RenderResult& r, bool asTile) {
    view_.drawn = r.drawn;
    view_.total = r.total;
    view_.skipped = r.skipped;
    view_.shapeGaps = r.shapeGaps;
    view_.notes = r.notes;
    view_.error = r.error;
    // UM FRACASSO TAMBEM E A RESPOSTA. Sem marcar a chave como respondida, o
    // `tick` seguinte a veria de novo em falta e pediria outra vez -- um pedido
    // por quadro, para sempre, para um documento que nao rende. Fica marcado,
    // sem textura, e so uma chave NOVA (uma edicao, outro zoom) tenta de novo.
    if (!r.error.empty() || r.rgba8.empty()) {
        const Key k{r.version, r.context, r.size, r.tile, r.mono, r.background, r.generation, r.effects};
        if (asTile) {
            tile_ = k;
            haveTile_ = true;
        } else {
            base_ = k;
            haveBase_ = true;
        }
        return;
    }
    // Um ladrilho que caiu para a base (o teto de area) E uma base: o icone
    // inteiro na resolucao de reserva. Vai para a camada de baixo.
    if (asTile && r.refined) {
        upload(sink_, view_.tileTexture, view_.tileWidth, view_.tileHeight, r);
        view_.tileMonoRaw = r.monoRaw;
        view_.tileMonoClear = r.monoClear;
        view_.tileMonoDark = r.mono && r.mono->dark();
        view_.tileGrid = r.gridSize;
        view_.tileX = r.originX;
        view_.tileY = r.originY;
        view_.tileVersion = r.version;
        view_.refined = true;
        tile_ = Key{r.version, r.context, r.size, r.tile, r.mono, r.background, r.generation, r.effects};
        haveTile_ = true;
        return;
    }
    upload(sink_, view_.texture, view_.width, view_.height, r);
    view_.monoRaw = r.monoRaw;
    view_.monoClear = r.monoClear;
    view_.monoDark = r.mono && r.mono->dark();
    view_.gridSize = r.gridSize ? r.gridSize : r.width;
    view_.originX = 0;
    view_.originY = 0;
    view_.version = r.version;
    view_.refined = !asTile;
    base_ = Key{r.version, r.context, asTile ? r.gridSize : r.size, TileRect{}, r.mono,
                r.background, r.generation, r.effects};
    haveBase_ = true;
}

void RenderCoordinator::tick(Session& s) {
    const ViewContext& v = s.view;
    const std::uint32_t baseSize = v.size;
    // O Mono do canvas: o da rendicao escolhida, com o quadrado do icone no palco.
    // O PALCO COMPOSTO NA TELA (02/10). Com uma `StageSource` que compoe, o
    // vidro e o Clear nao sao mais do job: o pedido sai sem o quadrado e sem o
    // fundo, e por isso nem o pan, nem o zoom, nem trocar o fundo mexem na
    // chave -- nenhum deles pede render. Sem ela (os testes) vale o caminho de
    // antes, com o vidro na CPU e o quadrado na chave.
    StageSource* stage = stageSource();
    const bool composed = stage && stage->composes();
    RenderLook look = lookOf(s, canvasRendition(s), v.context.idiom, true);
    if (composed && look.mono) look.mono->squareX = look.mono->squareY = look.mono->squareSide = 0.0;
    const std::optional<rb::MonoLook> mono = look.mono;
    const MonoBackdrop backdrop = composed ? MonoBackdrop{} : backdropOf(s, look, true);
    // O fundo so entra na chave de quem o le: fora do Mono fica o padrao, e
    // trocar o fundo do palco nao refaz um render que nao depende dele.
    StageBackground background = mono && !composed ? v.background : StageBackground{};
    // Uma imagem que ainda nao carregou foi desenhada como a cor: a chave diz
    // isso (-1), e quando os pixels chegam ela muda e o Mono se refaz.
    if (background.kind == StageBackground::Kind::Image && !backdrop.image) background.image = -1;
    const Key wantBase{s.version(), v.context, baseSize, TileRect{}, mono, background,
                       look.generation, look.effects};
    const bool tiled = v.tileSize > 0 && v.tile.w > 0;
    const Key wantTile{s.version(), v.context, v.tileSize, v.tile, mono, background,
                       look.generation, look.effects};

    // Primeiro colhe. Uma base de uma versao ANTERIOR ainda serve: num arraste
    // o documento muda a cada passo, e descartar o que estava em voo deixava o
    // canvas parado ate soltar (o mesmo achado do App.tsx do Tauri). O que nao
    // pode e andar para tras, entao a versao so sobe.
    while (auto result = scheduler_.poll()) {
        const Key answered{result->version, result->context, result->size, result->tile, result->mono,
                           result->background, result->generation, result->effects};
        const bool isTile = answered.tile.w > 0;
        if (flying_ && answered == inFlight_) flying_ = false;
        // O MESMO MONO, A MENOS DO QUADRADO. Durante um pan ou um zoom o
        // quadrado do icone no palco muda a cada quadro, e toda resposta chega
        // para um quadrado que ja passou: exigir o de agora descartava todas e
        // o vidro so se mexia quando o gesto parava. A resposta de um quadrado
        // anterior vai para a tela -- e o que ha de mais novo --, e a chave
        // dela, que nao e a pedida, faz o `tick` pedir a seguinte.
        const bool sameMono = [&] {
            if (!result->mono || !mono) return !result->mono && !mono;
            rb::MonoLook a = *result->mono;
            a.squareX = mono->squareX;
            a.squareY = mono->squareY;
            a.squareSide = mono->squareSide;
            return a == *mono;
        }();
        if (result->context != v.context || !sameMono ||
            !(result->background == background) || result->generation != look.generation || result->effects != look.effects) {
            continue;
        }
        if (isTile) {
            if (!result->refined) {
                if (!haveBase_ || result->version >= base_.version) apply(*result, true);
                // E o ladrilho fica RESPONDIDO: recusado, mas respondido. Sem
                // isto o `tick` seguinte o pediria de novo, e o teto o recusaria
                // de novo, um pedido por quadro para sempre.
                if (answered == wantTile) {
                    tile_ = answered;
                    haveTile_ = true;
                }
            } else if (answered == wantTile) {
                apply(*result, true);
            }
        } else if (!haveBase_ || result->version >= base_.version) {
            apply(*result, false);
        }
        view_.lastRenderSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - requestedAt_).count();
    }

    // Depois decide o que falta. A base antes do ladrilho, sempre.
    const Key* next = nullptr;
    if (!haveBase_ || !(base_ == wantBase)) next = &wantBase;
    else if (tiled && (!haveTile_ || !(tile_ == wantTile))) next = &wantTile;
    if (next && !(flying_ && inFlight_ == *next)) {
        // A base para onde cair vai em todo pedido, e nunca e zero: com ela o
        // job nunca devolve "recusado, sem pixels e sem erro".
        RenderRequest r{next->version, s.bundle().clone(), next->context, next->size, next->tile,
                        baseSize, next->mono, backdrop, next->background, next->generation,
                        next->effects, composed && next->mono.has_value()};
        scheduler_.request(std::move(r));
        inFlight_ = *next;
        flying_ = true;
        requestedAt_ = std::chrono::steady_clock::now();
    }
    // Sem ladrilho a pedir, o que esta na tela e o que foi pedido: a base.
    if (!tiled) view_.refined = true;
    view_.pending = flying_;
    view_.pendingSeconds =
        flying_ ? std::chrono::duration<double>(std::chrono::steady_clock::now() - requestedAt_).count()
                : 0.0;
}

}  // namespace ick
