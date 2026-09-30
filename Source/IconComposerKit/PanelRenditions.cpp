#include "Source/IconComposerKit/Theme.h"
#include "Source/IconComposerKit/Widgets.h"
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

icf::Appearance canvasSliceOf(icf::Appearance a) {
    // `[BIN]` `0x10AEF4`: `cmp w8, #2` com `b.lo` sem sinal põe `base` e
    // `light` no mesmo braço. Uma chave só, então.
    return a == icf::Appearance::Base ? icf::Appearance::Light : a;
}

rb::Rendition renditionForAppearance(icf::Appearance a, icf::Idiom idiom) {
    // Desenhável POR ESTE MOTOR e válida NESTA PLATAFORMA. As duas condições,
    // porque a barra só pode marcar um item que ela desenhou.
    auto offered = [&](rb::Rendition r) {
        return renditionSupported(r) && renditionValidFor(r, idiom);
    };
    const rb::Rendition measured = defaultRenditionFor(a);
    if (offered(measured)) return measured;
    // A medida não serve. A substituta é a primeira OFERECIDA que lê a mesma
    // fatia -- derivada, não escolhida: no dia em que `ClearMode` for lido,
    // `renditionSupported(Clear Light)` passa a ser verdadeiro e o caso
    // `tinted` deixa de cair aqui.
    const icf::Appearance slice = rb::sourceAppearance(measured);
    for (rb::Rendition r : kAllRenditions) {
        if (offered(r) && rb::sourceAppearance(r) == slice) return r;
    }
    // NENHUMA RENDITION DESTA PLATAFORMA LÊ ESSA FATIA. Hoje isto é exatamente
    // watchOS com `dark` ou `tinted`: `[BIN]` `Platform.validRenditions` diz
    // que a plataforma tem uma só, e o combo de aparência do canvas continua
    // podendo pôr o documento em qualquer fatia. A barra marca a única que a
    // plataforma tem -- marcar nada é a resposta errada, porque deixa a pessoa
    // sem saber o que está na tela grande -- e `drawRenditions` põe a
    // divergência na nota.
    for (rb::Rendition r : kAllRenditions) {
        if (offered(r)) return r;
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

RenditionStats drawRenditions(Session& s, RenditionThumbnails* thumbs, ImGuiWindowFlags extraFlags) {
    RenditionStats st;
    const icf::Idiom idiom = s.view.context.idiom;
    const std::vector<RenditionGroup> groups = renditionGroups(idiom);
    const rb::Rendition current = renditionForAppearance(s.view.context.appearance, idiom);
    // A FATIA QUE O CANVAS ESTÁ MOSTRANDO, que é a pergunta que a miniatura
    // marcada tem de responder.
    const icf::Context canvasContext{canvasSliceOf(s.view.context.appearance), idiom};
    // E se ela É a fatia que a rendition marcada nomeia. Fora do watchOS isto é
    // sempre verdade (`renditionForAppearance` devolve a rendition cuja
    // `sourceAppearance` é essa fatia); em watchOS com `dark` ou `tinted` não
    // é, porque a plataforma tem uma rendition só.
    const bool sliceHasARendition =
        rb::sourceAppearance(current) == canvasContext.appearance;

    // O CONTEXTO DE CADA ITEM. A MARCADA MOSTRA O QUE ESTÁ NA TELA GRANDE, e
    // não o que o nome dela diz: em watchOS/`dark` a única rendition é
    // `Default`, cuja fatia é `light`, e desenhar o render `light` embaixo de
    // um canvas escuro é mostrar a imagem da vizinha -- exatamente a mentira
    // que os itens cinzas existem para não contar. O nome continua sendo o
    // medido; a nota diz que a fatia é outra.
    auto contextOf = [&](rb::Rendition r, bool selected) {
        return selected ? canvasContext : renditionContext(r, idiom);
    };

    // OS CONTEXTOS QUE ESTA BARRA VAI MOSTRAR, o selecionado primeiro: é a
    // ordem em que as miniaturas são pedidas, e é por isso que a que a pessoa
    // acabou de escolher é a primeira a aparecer.
    std::vector<icf::Context> want;
    auto pushContext = [&](icf::Context c) {
        if (std::find(want.begin(), want.end(), c) == want.end()) want.push_back(c);
    };
    if (renditionValidFor(current, idiom) && renditionSupported(current)) pushContext(canvasContext);
    for (const RenditionGroup& g : groups) {
        for (std::size_t i = 0; i < g.count; ++i) {
            if (!renditionSupported(g.items[i])) continue;
            pushContext(contextOf(g.items[i], g.items[i] == current));
        }
    }
    // O TICK VEM ANTES DO DESENHO, exatamente como o coordenador do canvas
    // (Window.cpp): ele pode criar ou trocar a textura que a linha abaixo vai
    // mostrar.
    if (thumbs) thumbs->tick(s, want);

    if (!ImGui::Begin(kRenditionsWindow, nullptr, ImGuiWindowFlags_HorizontalScrollbar | extraFlags)) {
        ImGui::End();
        return st;
    }

    // A FAIXA DO TAURI (`.rendition-bar`, `.rthumb`): miniaturas de 40 pt com
    // a imagem de 36, raio 9, 8 pt entre elas e mais entre os grupos; a marcada
    // sobre o chip. Tudo centrado na largura, e a legenda -- o nome da que esta
    // sob o mouse, ou da marcada -- num chip logo acima.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float k = ui::dpi();
    const float tile = 40.0f * k, inner = 36.0f * k, gap = 8.0f * k, groupGap = 22.0f * k;
    float total = 0.0f;
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        if (gi) total += groupGap;
        total += groups[gi].count * tile + (groups[gi].count ? (groups[gi].count - 1) * gap : 0.0f);
    }
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const float y = wp.y + ws.y - tile;
    float x = wp.x + std::max(0.0f, (ws.x - total) * 0.5f);
    std::string caption;
    std::string hoveredCaption;

    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const RenditionGroup& g = groups[gi];
        if (gi) x += groupGap;
        ++st.groups;
        st.groupSizes.push_back(g.count);

        for (std::size_t i = 0; i < g.count; ++i) {
            if (i > 0) x += gap;
            const rb::Rendition r = g.items[i];
            const bool enabled = renditionSupported(r);
            const bool selected = (r == current);
            const icf::Context ctx = contextOf(r, selected);
            const RenditionThumb* thumb = (thumbs && enabled) ? thumbs->find(ctx) : nullptr;

            RenditionInfo info;
            info.rendition = r;
            info.label = renditionDisplayName(r);
            info.enabled = enabled;
            info.selected = selected;
            info.pending = thumb && thumb->pending;
            info.textured = thumb && thumb->texture != ImTextureID_Invalid;

            ImGui::PushID(static_cast<int>(r));
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::InvisibleButton("##thumb", ImVec2(tile, tile));
            ImGui::EndDisabled();
            const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            info.imageAt = centreOfLastItem();
            info.at = info.imageAt;
            const ImVec2 a0(x, y), a1(x + tile, y + tile);
            if (selected) {
                dl->AddRectFilled(a0, a1, theme::u32(ImVec4(0.12f, 0.12f, 0.13f, 0.72f)), 9.0f * k);
                dl->AddRect(a0, a1, IM_COL32(255, 255, 255, 30), 9.0f * k, 0, 1.0f);
            } else if (hovered && enabled) {
                dl->AddRectFilled(a0, a1, theme::u32(ImVec4(0.12f, 0.12f, 0.13f, 0.40f)), 9.0f * k);
            }
            const float pad = (tile - inner) * 0.5f;
            if (info.textured) {
                dl->AddImage(thumb->texture, ImVec2(x + pad, y + pad), ImVec2(x + pad + inner, y + pad + inner),
                             ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, enabled ? 255 : 90));
            } else {
                // Sem pixels: o contorno da pastilha, apagado -- "ainda nao",
                // "nao da" ou "falhou" vao para o tooltip, nao para a faixa.
                dl->AddRect(ImVec2(x + pad + 2 * k, y + pad + 2 * k), ImVec2(x + pad + inner - 2 * k, y + pad + inner - 2 * k),
                            theme::u32(enabled ? theme::kText4 : theme::kText5), 8.0f * k, 0, 1.0f);
                if (info.pending) {
                    const ImVec2 c(x + tile * 0.5f, y + tile * 0.5f);
                    dl->AddCircleFilled(c, 2.0f * k, theme::u32(theme::kText3));
                }
            }
            if (selected) caption = info.label;
            if (hovered) hoveredCaption = info.label;

            if (hovered) {
                std::string tip;
                if (!enabled) {
                    tip = renditionUnsupportedReason(r);
                } else {
                    tip = std::string(renditionDisplayName(r)) + " -- `" +
                          renditionSerializedName(r) + "` on disk, `" +
                          renditionFileNameComponent(r) + "` to `ictool --rendition`.\n"
                          "It reads the document's `" +
                          std::string(icf::appearanceToString(rb::sourceAppearance(r))) +
                          "` slice, so clicking it puts the canvas there.";
                    if (selected && !sliceHasARendition) {
                        tip += "\nThe canvas is on the `" +
                               std::string(icf::appearanceToString(canvasContext.appearance)) +
                               "` slice, which " + std::string(idiomLabel(idiom)) +
                               " has no rendition for. The thumbnail is that slice -- "
                               "what is on the big screen -- and not this rendition's own.";
                    }
                    if (thumb && !thumb->error.empty()) tip += "\nLast render failed: " + thumb->error;
                    else if (thumb && thumb->lastRenderSeconds >= 0.0)
                        tip += "\nThumbnail rendered in " + secondsText(thumb->lastRenderSeconds) + ".";
                    else if (!thumbs) tip += "\nNo render device.";
                }
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopID();

            if (!enabled) ++st.disabled;
            if (clicked && enabled) s.view.context.appearance = rb::sourceAppearance(r);
            st.drawn.push_back(std::move(info));
            x += tile;
        }
    }

    // A legenda (`.rcaption`): 11 pt, secundario, sobre o chip, centrada.
    {
        const std::string text = hoveredCaption.empty() ? caption : hoveredCaption;
        if (!text.empty()) {
            ImGui::PushFont(nullptr, 11.0f);
            const ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            const float cx = wp.x + ws.x * 0.5f;
            const ImVec2 c0(cx - ts.x * 0.5f - 9.0f * k, y - 6.0f * k - ts.y - 4.0f * k);
            const ImVec2 c1(cx + ts.x * 0.5f + 9.0f * k, y - 6.0f * k);
            dl->AddRectFilled(c0, c1, theme::u32(ImVec4(0.12f, 0.12f, 0.13f, 0.72f)), 9.0f * k);
            dl->AddText(ImVec2(cx - ts.x * 0.5f, c0.y + 2.0f * k), theme::u32(theme::kText2), text.c_str());
            ImGui::PopFont();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(wp.x, y + tile));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

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
            const RenditionThumb* t = thumbs->find(canvasContext);
            st.note = std::to_string(want.size()) + " rendition(s) at " +
                      std::to_string(thumbs->size()) + " px";
            if (t && t->lastRenderSeconds >= 0.0) {
                st.note += ", last " + secondsText(t->lastRenderSeconds);
            }
        }
    }
    // A FATIA DO CANVAS NÃO TEM RENDITION NESTA PLATAFORMA, dito em vez de
    // escondido. Hoje só acontece em watchOS: `[BIN]` `Platform.validRenditions`
    // dá a ela uma rendition só, e o combo de aparência do canvas continua
    // podendo pôr o documento em `dark` ou `tinted`. Sem esta frase a barra
    // marcaria `Default` mostrando uma imagem escura e não diria por quê.
    if (!sliceHasARendition) {
        st.note += " · " + std::string(idiomLabel(idiom)) + " has no rendition for the `" +
                   std::string(icf::appearanceToString(canvasContext.appearance)) +
                   "` slice the canvas is on; the marked thumbnail is that slice";
    }
    // A nota (quantas, em quanto tempo, o que falta) fica nas estatisticas e no
    // Diagnostics; a faixa e so as miniaturas, como a do alvo.

    ImGui::End();
    return st;
}

}  // namespace ick
