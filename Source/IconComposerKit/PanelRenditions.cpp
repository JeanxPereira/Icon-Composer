#include "Source/IconComposerKit/Renditions.h"

#include "Source/IconComposerKit/ViewModel.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace ick {

// ─── O VOCABULÁRIO ───────────────────────────────────────────────────────────

const char* renditionDisplayName(rb::Rendition r) {
    // `[BIN]` getter `0x41F5C`, um `csel` puro sobre o tag.
    switch (r) {
        case rb::Rendition::LightColor: return "Default";
        case rb::Rendition::DarkColor:  return "Dark";
        case rb::Rendition::LightTint:  return "Tinted Light";
        case rb::Rendition::DarkTint:   return "Tinted Dark";
        case rb::Rendition::LightClear: return "Clear Light";
        case rb::Rendition::DarkClear:  return "Clear Dark";
    }
    return "?";
}

const char* renditionFileNameComponent(rb::Rendition r) {
    // `[BIN]` getter `0x421BC`, e a mesma coluna da tabela compacta do Kit em
    // `0x185438` -- o texto que o `ictool` aceita em `--rendition`.
    switch (r) {
        case rb::Rendition::LightColor: return "Default";
        case rb::Rendition::DarkColor:  return "Dark";
        case rb::Rendition::LightTint:  return "TintedLight";
        case rb::Rendition::DarkTint:   return "TintedDark";
        case rb::Rendition::LightClear: return "ClearLight";
        case rb::Rendition::DarkClear:  return "ClearDark";
    }
    return "?";
}

const char* renditionSerializedName(rb::Rendition r) {
    // `[BIN]` `__cstring` `0x12a6e0`-`0x12a718`.
    switch (r) {
        case rb::Rendition::LightColor: return "light-color";
        case rb::Rendition::DarkColor:  return "dark-color";
        case rb::Rendition::LightTint:  return "light-tint";
        case rb::Rendition::DarkTint:   return "dark-tint";
        case rb::Rendition::LightClear: return "light-clear";
        case rb::Rendition::DarkClear:  return "dark-clear";
    }
    return "?";
}

bool renditionIsClear(rb::Rendition r) {
    return r == rb::Rendition::LightClear || r == rb::Rendition::DarkClear;
}

bool renditionValidFor(rb::Rendition r, icf::Idiom idiom) {
    // `[BIN]` `Platform.validRenditions` (`0x3A248`): `Default` sempre; as
    // outras cinco só para plataforma <= 1, e watchOS é 2.
    if (idiom == icf::Idiom::WatchOS) return r == rb::Rendition::LightColor;
    return true;
}

rb::Rendition defaultRenditionFor(icf::Appearance a) {
    // `[BIN]` `Appearance.defaultRendition` (`0x20D98`), a tabela de quatro
    // bytes `00 00 01 04`. Transcrita como os quatro casos e não como um
    // índice na constante, para que o `tinted -> Clear Light` -- que é a
    // surpresa -- fique visível.
    switch (a) {
        case icf::Appearance::Base:   return rb::Rendition::LightColor;
        case icf::Appearance::Light:  return rb::Rendition::LightColor;
        case icf::Appearance::Dark:   return rb::Rendition::DarkColor;
        case icf::Appearance::Tinted: return rb::Rendition::LightClear;
    }
    return rb::Rendition::LightColor;
}

icf::Context renditionContext(rb::Rendition r, icf::Idiom idiom) {
    icf::Context c;
    c.appearance = rb::sourceAppearance(r);   // `[BIN]`, medido e testado em test_fill_resolve
    c.idiom = idiom;
    return c;
}

namespace {

// A primeira rendition ANTERIOR na ordem medida que lê a mesma fatia do
// documento -- isto é, que produz exatamente os mesmos pixels neste motor.
std::optional<rb::Rendition> shadowedBy(rb::Rendition r) {
    const icf::Appearance mine = rb::sourceAppearance(r);
    for (rb::Rendition e : kAllRenditions) {
        if (e == r) break;
        if (renditionIsClear(e)) continue;   // essa já não é oferecida por outro motivo
        if (rb::sourceAppearance(e) == mine) return e;
    }
    return std::nullopt;
}

}  // namespace

bool renditionSupported(rb::Rendition r) {
    if (renditionIsClear(r)) return false;
    return !shadowedBy(r).has_value();
}

std::string renditionUnsupportedReason(rb::Rendition r) {
    if (renditionIsClear(r)) {
        return std::string(renditionDisplayName(r)) +
               " is not an appearance of the document: `Clear` is the THIRD rendering mode "
               "(`RenderingMode.Contents = {color, tinted, clear}`, measured 19/09 in "
               "IconRendering.arm64), and this renderer has two.\n"
               "The 15 fields of `ICRRenderingParameters.ClearMode` have not been read, so drawing "
               "it here would mean showing the Tinted image under another label. The laudo says so "
               "in as many words: naming it is authorised, implementing it is not.\n"
               "Docs/Laudos/2026-09-19-renditions-e-mirroring.md §2.5, §2.8 and §6.";
    }
    if (auto by = shadowedBy(r)) {
        return std::string(renditionDisplayName(r)) + " would be the same pixels as " +
               renditionDisplayName(*by) +
               ".\nThe target separates the two with `ICRIconStyle.appearance` ({light, dark}), "
               "which is an axis of the RENDER and not of the document (laudo §2.5). This engine "
               "has no such axis: the only appearance that reaches it is the document's, and both "
               "of these read the same `" +
               std::string(icf::appearanceToString(rb::sourceAppearance(r))) +
               "` slice (laudo §2.6).\n"
               "Offering it would be offering the neighbour's image.";
    }
    return {};
}

rb::Rendition renditionForAppearance(icf::Appearance a) {
    const rb::Rendition measured = defaultRenditionFor(a);
    if (renditionSupported(measured)) return measured;
    // A medida não é desenhável aqui (só acontece com `tinted`, cujo padrão
    // medido é `Clear Light`). A substituta é a primeira SUPORTADA que lê a
    // mesma fatia -- derivada, não escolhida: no dia em que `ClearMode` for
    // lido, `renditionSupported(Clear Light)` passa a ser verdadeiro e esta
    // linha deixa de ser alcançada.
    const icf::Appearance slice = rb::sourceAppearance(measured);
    for (rb::Rendition r : kAllRenditions) {
        if (renditionSupported(r) && rb::sourceAppearance(r) == slice) return r;
    }
    return measured;
}

std::vector<RenditionGroup> renditionGroups(icf::Idiom idiom) {
    // `[BIN]` `Rendition.displayGrouped` (`0x4202C`): os três arrays literais
    // em `__const` `0x1580A8` = [0,1], `0x1580D8` = [4,5], `0x158108` = [2,3].
    // **Clear antes de Tinted**, que não é a ordem do enum.
    static constexpr rb::Rendition kPairs[3][2] = {
        {rb::Rendition::LightColor, rb::Rendition::DarkColor},
        {rb::Rendition::LightClear, rb::Rendition::DarkClear},
        {rb::Rendition::LightTint, rb::Rendition::DarkTint},
    };
    std::vector<RenditionGroup> out;
    for (const auto& pair : kPairs) {
        RenditionGroup g;
        for (rb::Rendition r : pair) {
            if (!renditionValidFor(r, idiom)) continue;
            g.items[g.count++] = r;
        }
        // Um grupo vazio não vira um separador solto na tela: em watchOS
        // sobram zero itens em dois dos três pares, e a barra tem UM grupo.
        if (g.count > 0) out.push_back(g);
    }
    return out;
}

// ─── DUAS FILAS NUM AGENDADOR SÓ ─────────────────────────────────────────────

void SharedScheduler::request(int lane, RenderRequest r) {
    // O contrato de `RenderScheduler` por raia: um pendente, o mais novo
    // descarta o anterior. É o que o `RenderCoordinator` já supõe.
    waiting_[lane] = std::move(r);
    pump();
}

std::optional<RenderResult> SharedScheduler::poll(int lane) {
    pump();
    if (ready_[lane].empty()) return std::nullopt;
    RenderResult out = std::move(ready_[lane].front());
    ready_[lane].erase(ready_[lane].begin());
    return out;
}

bool SharedScheduler::tryLease() {
    // Recolher ANTES de responder: um resultado que só faltava ser colhido não
    // é um render em voo, e recusar por causa dele deixaria a exportação
    // esperando um quadro por nada. `drain` não submete, então o dispositivo
    // não escapa para uma raia entre esta linha e a próxima.
    drain();
    if (inFlight_) return false;
    leased_ = true;
    return true;
}

void SharedScheduler::endLease() {
    leased_ = false;
    pump();
}

void SharedScheduler::drain() {
    while (auto r = real_.poll()) {
        // De quem é. `inFlight_` vazio só acontece com um resultado que
        // sobreviveu a uma troca de documento; ele vai para a raia do canvas,
        // que o descarta pela chave -- nunca some em silêncio de um jeito que
        // deixe a outra raia esperando para sempre.
        const int owner = inFlight_ ? *inFlight_ : 0;
        inFlight_.reset();
        ready_[owner].push_back(std::move(*r));
    }
}

void SharedScheduler::pump() {
    drain();
    // O DISPOSITIVO ESTÁ ARRENDADO À EXPORTAÇÃO: nada é submetido. Os pedidos
    // ficam em `waiting_` -- o contrato da raia já é "um pendente, o mais novo
    // descarta o anterior" -- e saem no `endLease`.
    if (leased_) return;
    if (inFlight_) return;
    // A PRIORIDADE É DO CANVAS: é o que a pessoa está olhando. A miniatura
    // espera, e o relógio dela conta a espera.
    for (int lane = 0; lane < 2; ++lane) {
        if (!waiting_[lane]) continue;
        RenderRequest r = std::move(*waiting_[lane]);
        waiting_[lane].reset();
        inFlight_ = lane;
        real_.request(std::move(r));
        return;
    }
}

// ─── AS MINIATURAS ───────────────────────────────────────────────────────────

RenditionThumbnails::RenditionThumbnails(RenderScheduler& scheduler, TextureSink& sink,
                                         std::uint32_t size)
    : scheduler_(scheduler), sink_(sink), size_(size > 0 ? size : 128) {}

RenditionThumbnails::~RenditionThumbnails() {
    for (RenditionThumb& t : thumbs_) {
        if (t.texture != ImTextureID_Invalid) sink_.remove(t.texture);
    }
}

const RenditionThumb* RenditionThumbnails::find(icf::Context ctx) const {
    for (const RenditionThumb& t : thumbs_) {
        if (t.context == ctx) return &t;
    }
    return nullptr;
}

void RenditionThumbnails::tick(Session& s, const std::vector<icf::Context>& want) {
    const auto now = std::chrono::steady_clock::now();
    auto wanted = [&](icf::Context c) {
        return std::find(want.begin(), want.end(), c) != want.end();
    };
    auto mut = [&](icf::Context c) -> RenditionThumb* {
        for (RenditionThumb& t : thumbs_) {
            if (t.context == c) return &t;
        }
        return nullptr;
    };

    // O que saiu da lista devolve a textura. Trocar de idioma não pode vazar
    // uma textura por troca.
    for (auto it = thumbs_.begin(); it != thumbs_.end();) {
        if (wanted(it->context)) {
            ++it;
            continue;
        }
        if (it->texture != ImTextureID_Invalid) sink_.remove(it->texture);
        it = thumbs_.erase(it);
    }
    for (const icf::Context& c : want) {
        if (!mut(c)) {
            RenditionThumb t;
            t.context = c;
            thumbs_.push_back(std::move(t));
        }
    }

    while (auto res = scheduler_.poll()) {
        // A chave INTEIRA, como o coordenador do canvas: versão, contexto e
        // tamanho. Um resultado que responde outra pergunta não é a resposta.
        if (!inFlight_ || res->version != flightVersion_ || !(res->context == flightContext_) ||
            res->size != size_) {
            continue;
        }
        inFlight_ = false;
        RenditionThumb* t = mut(res->context);
        if (!t) continue;   // o contexto saiu da barra enquanto o render corria
        t->pending = false;
        t->pendingSeconds = 0.0;
        t->lastRenderSeconds = std::chrono::duration<double>(now - flightAt_).count();
        t->error = res->error;
        // A VERSÃO É GRAVADA MESMO NO ERRO, de propósito: sem isso um documento
        // que não renderiza seria repedido a cada quadro para sempre, e a fila
        // do canvas nunca mais sairia. O erro fica na miniatura até o documento
        // mudar, que é quando a pergunta muda.
        t->version = res->version;
        if (res->error.empty() && !res->rgba8.empty()) {
            if (t->texture != ImTextureID_Invalid && t->width == res->width &&
                t->height == res->height) {
                sink_.update(t->texture, res->width, res->height, res->rgba8.data());
            } else {
                if (t->texture != ImTextureID_Invalid) sink_.remove(t->texture);
                t->texture = sink_.create(res->width, res->height, res->rgba8.data());
                t->width = res->width;
                t->height = res->height;
            }
        }
    }

    if (inFlight_) {
        if (RenditionThumb* t = mut(flightContext_)) {
            t->pendingSeconds = std::chrono::duration<double>(now - flightAt_).count();
        }
    }

    stale_ = 0;
    for (const icf::Context& c : want) {
        const RenditionThumb* t = find(c);
        if (!t || t->version != s.version()) ++stale_;
    }

    // UM DE CADA VEZ, na ordem em que a barra as pediu -- a selecionada
    // primeiro. Quatro pedidos de uma vez seriam dois segundos de janela
    // parada, que é a regressão que este laço existe para não cometer.
    if (inFlight_) return;
    for (const icf::Context& c : want) {
        RenditionThumb* t = mut(c);
        if (!t || t->version == s.version()) continue;
        RenderRequest r{s.version(), s.bundle().clone(), c, size_, TileRect{}, size_};
        scheduler_.request(std::move(r));
        inFlight_ = true;
        flightVersion_ = s.version();
        flightContext_ = c;
        flightAt_ = now;
        t->pending = true;
        t->pendingSeconds = 0.0;
        return;
    }
}

// ─── O PAINEL ────────────────────────────────────────────────────────────────

namespace {

// O lado da miniatura em pontos. `WindowLayoutConstants` não nomeia a barra
// (laudo §4.2 tem nove constantes e nenhuma é dela), então isto é `[DEC]`:
// grande o bastante para a arte ser reconhecível, pequeno o bastante para os
// seis caberem lado a lado numa janela de 1280.
constexpr float kTile = 92.0f;
constexpr float kInGroupGap = 8.0f;
constexpr float kBetweenGroupsGap = 26.0f;

std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

ImVec2 centreOfLastItem() {
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

}  // namespace

RenditionStats drawRenditions(Session& s, RenditionThumbnails* thumbs) {
    RenditionStats st;
    const icf::Idiom idiom = s.view.context.idiom;
    const std::vector<RenditionGroup> groups = renditionGroups(idiom);
    const rb::Rendition current = renditionForAppearance(s.view.context.appearance);

    // OS CONTEXTOS QUE ESTA BARRA VAI MOSTRAR, o selecionado primeiro: é a
    // ordem em que as miniaturas são pedidas, e é por isso que a que a pessoa
    // acabou de escolher é a primeira a aparecer.
    std::vector<icf::Context> want;
    auto pushContext = [&](rb::Rendition r) {
        const icf::Context c = renditionContext(r, idiom);
        if (std::find(want.begin(), want.end(), c) == want.end()) want.push_back(c);
    };
    if (renditionValidFor(current, idiom) && renditionSupported(current)) pushContext(current);
    for (const RenditionGroup& g : groups) {
        for (std::size_t i = 0; i < g.count; ++i) {
            if (renditionSupported(g.items[i])) pushContext(g.items[i]);
        }
    }
    // O TICK VEM ANTES DO DESENHO, exatamente como o coordenador do canvas
    // (Window.cpp): ele pode criar ou trocar a textura que a linha abaixo vai
    // mostrar.
    if (thumbs) thumbs->tick(s, want);

    if (!ImGui::Begin(kRenditionsWindow, nullptr, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::End();
        return st;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool firstGroup = true;
    for (const RenditionGroup& g : groups) {
        if (!firstGroup) ImGui::SameLine(0.0f, kBetweenGroupsGap);
        firstGroup = false;
        ++st.groups;
        st.groupSizes.push_back(g.count);

        for (std::size_t i = 0; i < g.count; ++i) {
            if (i > 0) ImGui::SameLine(0.0f, kInGroupGap);
            const rb::Rendition r = g.items[i];
            const bool enabled = renditionSupported(r);
            const bool selected = (r == current);
            const icf::Context ctx = renditionContext(r, idiom);
            // SÓ A SUPORTADA GANHA A ARTE, e `&& enabled` é a linha inteira do
            // motivo: a miniatura é chaveada por CONTEXTO, e `Clear Dark` cai
            // no mesmo `tinted` de `Tinted Light`. Sem esta condição os três
            // itens cinzas mostrariam a imagem da vizinha -- exatamente a
            // mentira que o cinza existe para não contar. Medido: sem ela o
            // caso `thumbnails_are_rendered_one_at_a_time` conta 6 miniaturas
            // desenhadas onde só 3 foram renderizadas.
            const RenditionThumb* thumb = (thumbs && enabled) ? thumbs->find(ctx) : nullptr;

            RenditionInfo info;
            info.rendition = r;
            info.label = renditionDisplayName(r);
            info.enabled = enabled;
            info.selected = selected;
            info.pending = thumb && thumb->pending;
            info.textured = thumb && thumb->texture != ImTextureID_Invalid;

            ImGui::PushID(static_cast<int>(r));
            ImGui::BeginGroup();
            ImGui::BeginDisabled(!enabled);

            bool clicked = false;
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            if (ImGui::Button("##thumb", ImVec2(kTile, kTile))) clicked = true;
            info.imageAt = centreOfLastItem();
            const bool hoveredImage = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            // A ARTE POR CIMA DO BOTÃO, e não um `ImageButton`: o botão já dá o
            // retângulo, o hover e o clique, e desenhar a textura na drawlist
            // deixa o item existir do mesmo jeito quando não há textura
            // nenhuma -- que é o caso da suíte, e é o caso do primeiro quadro
            // de todo documento.
            if (info.textured) {
                const ImVec2 a(origin.x + 4.0f, origin.y + 4.0f);
                const ImVec2 b(origin.x + kTile - 4.0f, origin.y + kTile - 4.0f);
                dl->AddImage(thumb->texture, a, b);
            } else {
                const char* what = !thumbs         ? "no device"
                                   : (thumb && thumb->pending) ? "rendering…"
                                   : (thumb && !thumb->error.empty()) ? "failed"
                                   : !enabled      ? "not drawn"
                                                   : "…";
                const ImVec2 sz = ImGui::CalcTextSize(what);
                dl->AddText(ImVec2(origin.x + (kTile - sz.x) * 0.5f,
                                   origin.y + (kTile - sz.y) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), what);
            }
            if (selected) {
                // O ENQUADRAMENTO DA ESCOLHIDA. Dois pixels e a cor de destaque
                // do tema: é o mesmo sinal que a camada selecionada recebe no
                // canvas, e é o que liga esta barra ao que está na tela grande.
                dl->AddRect(ImVec2(origin.x - 2.0f, origin.y - 2.0f),
                            ImVec2(origin.x + kTile + 2.0f, origin.y + kTile + 2.0f),
                            ImGui::GetColorU32(ImGuiCol_ButtonActive), 3.0f, 0, 2.0f);
            }

            // O `RenditionTitleButton` do alvo: o NOME é um botão, e clicar
            // nele leva o canvas para aquele contexto.
            if (ImGui::Button(info.label.c_str(), ImVec2(kTile, 0.0f))) clicked = true;
            info.at = centreOfLastItem();
            const bool hoveredTitle = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);

            ImGui::EndDisabled();
            ImGui::EndGroup();

            if (hoveredImage || hoveredTitle) {
                std::string tip;
                if (!enabled) {
                    tip = renditionUnsupportedReason(r);
                } else {
                    tip = std::string(renditionDisplayName(r)) + " -- `" +
                          renditionSerializedName(r) + "` on disk, `" +
                          renditionFileNameComponent(r) + "` to `ictool --rendition`.\n"
                          "It reads the document's `" +
                          std::string(icf::appearanceToString(rb::sourceAppearance(r))) +
                          "` slice, so clicking it puts the canvas -- and the Appearance "
                          "combo -- there.";
                    if (thumb && !thumb->error.empty()) tip += "\nLast render failed: " + thumb->error;
                    else if (thumb && thumb->lastRenderSeconds >= 0.0)
                        tip += "\nThumbnail rendered in " + secondsText(thumb->lastRenderSeconds) + ".";
                }
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopID();

            if (!enabled) ++st.disabled;
            // UM CONTROLE SÓ ESCREVE O MESMO ESTADO QUE O OUTRO LÊ. O clique
            // não guarda "qual rendition" em lugar nenhum: ele move a aparência
            // do `ViewContext`, que é a MESMA coisa que os combos do canvas e o
            // menu View>Appearance movem. Dois controles que discordam do mesmo
            // estado seriam defeito, e a única maneira de não discordarem é não
            // haver um segundo estado.
            if (clicked && enabled) s.view.context.appearance = rb::sourceAppearance(r);
            st.drawn.push_back(std::move(info));
        }
    }

    // A LINHA QUE DIZ O QUE ESTÁ ACONTECENDO. Nunca vazia: um painel que
    // renderiza quatro vezes meio segundo e não diz nada é exatamente a
    // regressão de 15/09 que `RenderView::lastRenderSeconds` existe por causa.
    if (!thumbs) {
        st.note = "No render device: this bar shows the shape, not the pixels.";
    } else {
        const RenditionThumb* busy = nullptr;
        for (const icf::Context& c : want) {
            const RenditionThumb* t = thumbs->find(c);
            if (t && t->pending) busy = t;
        }
        if (busy) {
            st.note = "rendering " + std::string(appearanceLabel(busy->context.appearance)) +
                      " at " + std::to_string(thumbs->size()) + " px — " +
                      secondsText(busy->pendingSeconds);
            if (thumbs->stale() > 1) {
                st.note += ", " + std::to_string(thumbs->stale() - 1) + " more to go";
            }
        } else if (thumbs->stale() > 0) {
            st.note = std::to_string(thumbs->stale()) + " thumbnail(s) queued behind the canvas";
        } else {
            const RenditionThumb* t = thumbs->find(renditionContext(current, idiom));
            st.note = std::to_string(want.size()) + " rendition(s) at " +
                      std::to_string(thumbs->size()) + " px";
            if (t && t->lastRenderSeconds >= 0.0) {
                st.note += ", last " + secondsText(t->lastRenderSeconds);
            }
        }
    }
    ImGui::TextDisabled("%s", st.note.c_str());
    if (st.disabled > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("· %zu greyed (hover for why)", st.disabled);
    }

    ImGui::End();
    return st;
}

}  // namespace ick
