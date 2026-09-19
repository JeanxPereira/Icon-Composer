// The canvas and the diagnostics panel.
//
// THE CANVAS SHOWS THE LAST RENDER, AND SAYS WHEN IT IS STALE (spec 13/09 §6)
// ---------------------------------------------------------------------------
// The panel never asks for a render and never touches a device: it is handed a
// `RenderView` the coordinator filled in. While a newer render is on its way it
// keeps the previous texture up -- an editor that blanked the canvas on every
// keystroke would be unusable -- and marks the frame as provisional, in the
// context bar and with a dot on the canvas itself.
//
// That marking is not decoration. `appearance`, `idiom` and preview size live
// on `Session::view`, which is not behind a command and therefore does not move
// `version()`; the coordinator matches a result against the WHOLE key (version,
// context, width), so changing any of the three leaves `pending` true until the
// render that actually answers the new context lands. Those are exactly the
// frames where the pixels on screen belong to a context nobody asked for any
// more, and the only honest thing the canvas can do is say so.
//
// ZOOM IS NOT A RENDER (spec 13/09 §6). It is the same pixels drawn over a
// larger rectangle, so it is applied here and never reaches the render key.
//
// THE CANVAS CLIPS, AND THE CANVAS NAVIGATES (reprovado em uso, 15/09)
// ---------------------------------------------------------------------------
// Two defects that the headless selftest reported green through, because it
// counts `imgui errors` and `textured yes` and neither of those is a person
// trying to work:
//
//   1. Nothing clipped. The body filled a rectangle and then called `AddImage`
//      into the SAME draw list with no `PushClipRect`. A window's draw list is
//      clipped to the window's inner rect, which starts under the menu bar --
//      so the icon was free to paint over everything the body had drawn ABOVE
//      it, which is the row of combos. Past a certain zoom the icon simply ate
//      the appearance / idiom / size / zoom controls. That is the user's "o
//      viewport atravessa os botões", exactly.
//
//   2. The navigation was a sketch: a file-static `pan`, moved only by a middle
//      drag, with no wheel, no anchored zoom, no easing, no bounds and no fit.
//
// The arithmetic below is transcribed from OnyxSDK's ImageViewer
// (D:/CodingProjects/OnyxSDK/Source/Viewers/ImageViewer.cpp) -- anchored zoom
// at :100, the fit/centre pair at :154, the margin clamp at :210, the shared
// exp-decay ease at :230 -- and NOT taken by linking it. Rule 2 of the
// architecture spec: only Source/app links Onyx, and IconComposerKit is what
// `ic_tests` and the headless selftest link (Source/IconComposerKit/
// CMakeLists.txt:22 names Foundation, CoreSVG, RenderBox and imgui_lib, and
// nothing else). `Onyx::Viewers::ImageViewer` also owns a `TexturePool` and a
// `VkContext` (ImageViewer.cpp:52-59) and draws its own toolbar out of
// `Onyx::Theme` and `SFSymbols`, so the class is a viewer of a file on disk,
// not a view transform -- of its 268 lines the part this panel needs is about
// forty, and they are pure. Transcribing them cost nothing and kept the Kit
// buildable with `-DIC_BUILD_UI=OFF`.
//
// E DESDE 19/09 O CANVAS SELECIONA (spec 13/09 §1, rodada 4)
// --------------------------------------------------------------------------
// Ate aqui o caminho era de mao unica: a arvore escolhia uma camada e o canvas
// a enquadrava. Clicar no icone nao escolhia nada, o que num editor de icones e
// a operacao mais obvia que existe.
//
// O clique escreve em `Session::selection`, a MESMA que a arvore le -- nao ha
// uma segunda selecao, entao nao ha o que sincronizar: a linha acende e o
// painel rola ate ela no quadro seguinte sem que este arquivo saiba que aquele
// existe. E o retangulo desenhado e o alvo do clique, os dois saidos de
// `canvasLayerRect` (Panels.h), onde tambem esta escrito o que a regra tem de
// grosseiro e por que responder melhor exigiria o render que a spec proibe
// aqui.
//
// THE DIAGNOSTICS PANEL IS WHAT KEEPS THE PICTURE FROM LYING (spec 13/09 §6)
// --------------------------------------------------------------------------
// `skipped`, `shapeGaps` and `notes` go to the screen for the same reason
// `icrender` prints them: a plausible figure produced without measurement must
// not be silent. A layer the renderer declined to draw appears here by name
// instead of simply not being in the image.
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/ViewModel.h"
// `rb::kCanvasPoints` -- the selection overlay must use the same ruler the
// renderer places art with, or it would frame the layer somewhere the layer is
// not (doc 03 §22).
#include "Source/RenderBox/IconRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace ick {

// ─── The geometry, drawing nothing (declared in Panels.h) ────────────────────

float canvasClampZoom(float zoom) {
    if (!(zoom > 0.0f)) return kCanvasZoomMin;   // also catches NaN
    return std::clamp(zoom, kCanvasZoomMin, kCanvasZoomMax);
}

float canvasFitZoom(float availW, float availH, float sidePx) {
    if (!(sidePx > 0.0f) || !(availW > 0.0f) || !(availH > 0.0f)) return 1.0f;
    return canvasClampZoom(std::min(availW / sidePx, availH / sidePx));
}

CanvasVec canvasCentrePan(float availW, float availH, float sidePx, float zoom) {
    const float side = sidePx * zoom;
    return CanvasVec{(availW - side) * 0.5f, (availH - side) * 0.5f};
}

CanvasVec canvasZoomAnchored(CanvasVec pan, float fromZoom, float toZoom, CanvasVec anchor) {
    if (!(fromZoom > 0.0f)) return pan;
    // The anchor's position INSIDE the icon, in screen pixels, scales with the
    // magnification; keeping the anchor still means moving the corner by the
    // difference. Written against the target zoom, so two wheel clicks inside
    // one ease compose instead of fighting.
    const float scale = canvasClampZoom(toZoom) / fromZoom;
    return CanvasVec{anchor.x - (anchor.x - pan.x) * scale,
                     anchor.y - (anchor.y - pan.y) * scale};
}

CanvasVec canvasClampPan(CanvasVec pan, float availW, float availH, float sidePx, float zoom,
                         float margin) {
    const float side = sidePx * zoom;
    auto axis = [&](float v, float avail) {
        if (side <= avail) {
            // Smaller than the canvas: it lives centred, and may be nudged off
            // centre by the margin so a drag still feels like it does something.
            const float centre = (avail - side) * 0.5f;
            return std::clamp(v, centre - margin, centre + margin);
        }
        // Larger: the far edge may not come further in than the margin, which is
        // what stops the icon being dragged out of the canvas entirely.
        return std::clamp(v, avail - side - margin, margin);
    };
    return CanvasVec{axis(pan.x, availW), axis(pan.y, availH)};
}

CanvasRect canvasImageRect(CanvasVec pan, float sidePx, float zoom) {
    const float side = sidePx * zoom;
    return CanvasRect{pan.x, pan.y, pan.x + side, pan.y + side};
}

CanvasRect canvasIntersect(CanvasRect a, CanvasRect b) {
    CanvasRect r{std::max(a.x0, b.x0), std::max(a.y0, b.y0),
                 std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    if (r.empty()) return CanvasRect{r.x0, r.y0, r.x0, r.y0};
    return r;
}

namespace {

// A `position` de um no sob o contexto que o CANVAS renderiza -- nao o escopo
// do inspetor: o retangulo tem de enquadrar a arte que esta na tela.
icf::Position positionOf(const icf::json::Value* node, icf::Context ctx) {
    icf::Position p;
    if (!node) return p;
    if (const icf::json::Value* pv = icf::resolve(*node, "position", ctx)) {
        if (auto read = icf::positionFrom(*pv)) p = *read;
    }
    return p;
}

bool hiddenUnder(const icf::json::Value& node, icf::Context ctx) {
    const icf::json::Value* v = icf::resolve(node, "hidden", ctx);
    return v && v->kind() == icf::json::Value::Kind::Bool && v->boolean();
}

const std::vector<icf::json::Value>* arrayAt(const icf::json::Value& owner, const char* key) {
    const icf::json::Value* v = owner.find(key);
    if (!v || v->kind() != icf::json::Value::Kind::Array) return nullptr;
    return &v->elements();
}

}  // namespace

CanvasRect canvasLayerRect(const icf::json::Value& root, icf::NodePath path, icf::Context ctx,
                           CanvasVec topLeft, float side) {
    const icf::json::Value* node = icf::nodeAt(root, path);
    if (!node || !path.group || !(side > 0.0f)) return CanvasRect{};

    // COMPOSTA COM A DO GRUPO, que e o que o render faz (`compose`,
    // IconRenderer.cpp:571). Um retangulo que ignorasse o grupo cairia fora da
    // arte em todo documento cujo grupo carrega uma `position`.
    icf::Position p = positionOf(node, ctx);
    if (path.layer) {
        const icf::Position g =
            positionOf(icf::nodeAt(root, icf::NodePath{path.group, std::nullopt}), ctx);
        p.translation.x = g.scale * p.translation.x + g.translation.x;
        p.translation.y = g.scale * p.translation.y + g.translation.y;
        p.scale = g.scale * p.scale;
    }

    // `rb::placeOnCanvas` com `w == h == kCanvasPoints` (ver a nota no header):
    // a caixa centrada, escalada, e entao movida -- so que em pixels de tela,
    // que e a mesma coisa vezes `side / kCanvasPoints`.
    const float pointsToPixels = side / static_cast<float>(rb::kCanvasPoints);
    const float half = side * static_cast<float>(p.scale) * 0.5f;
    const float cx = topLeft.x + side * 0.5f + static_cast<float>(p.translation.x) * pointsToPixels;
    const float cy = topLeft.y + side * 0.5f + static_cast<float>(p.translation.y) * pointsToPixels;
    return CanvasRect{cx - half, cy - half, cx + half, cy + half};
}

std::optional<icf::NodePath> canvasLayerAt(const icf::json::Value& root, icf::Context ctx,
                                           CanvasVec topLeft, float side, CanvasVec point) {
    const std::vector<icf::json::Value>* groups = arrayAt(root, "groups");
    if (!groups) return std::nullopt;
    for (std::size_t g = 0; g < groups->size(); ++g) {
        const icf::json::Value& group = (*groups)[g];
        if (hiddenUnder(group, ctx)) continue;
        const std::vector<icf::json::Value>* layers = arrayAt(group, "layers");
        if (!layers) continue;
        for (std::size_t l = 0; l < layers->size(); ++l) {
            if (hiddenUnder((*layers)[l], ctx)) continue;
            const icf::NodePath path{g, l};
            const CanvasRect r = canvasLayerRect(root, path, ctx, topLeft, side);
            if (r.empty()) continue;
            if (point.x >= r.x0 && point.x <= r.x1 && point.y >= r.y0 && point.y <= r.y1) {
                return path;
            }
        }
    }
    return std::nullopt;
}

std::uint32_t canvasTileSize(std::uint32_t baseSize, float zoom) {
    if (!(zoom > 1.0f) || baseSize == 0) return 0;   // `!(>)` tambem pega NaN
    return static_cast<std::uint32_t>(std::lround(static_cast<double>(baseSize) * zoom));
}

TileRect canvasTileFor(CanvasRect painted, CanvasVec imageTopLeft, std::uint32_t baseSize,
                       float zoom) {
    const std::uint32_t t = canvasTileSize(baseSize, zoom);
    if (t == 0 || painted.empty()) return TileRect{};
    // Tela -> ladrilho. O lado na tela e `base * zoom` e o ladrilho tem `t`
    // pixels, entao a razao e ~1 e so absorve o arredondamento de `t`.
    const double k = static_cast<double>(t) / (static_cast<double>(baseSize) * zoom);
    const std::int64_t tMax = static_cast<std::int64_t>(t);
    // Para FORA nos dois lados -- `floor` no canto de cima e `ceil` no de
    // baixo -- para que o ladrilho cubra todo pixel de tela que o recorte
    // toca; arredondar para dentro deixaria uma fatia de fundo do painel na
    // borda. O grampo em [0, t] e o que mantem o pedido dentro da grade.
    auto lo = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor((v - o) * k)), 0, tMax);
    };
    auto hi = [&](float v, float o) {
        return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::ceil((v - o) * k)), 0, tMax);
    };
    const std::int64_t x0 = lo(painted.x0, imageTopLeft.x), x1 = hi(painted.x1, imageTopLeft.x);
    const std::int64_t y0 = lo(painted.y0, imageTopLeft.y), y1 = hi(painted.y1, imageTopLeft.y);
    if (x1 <= x0 || y1 <= y0) return TileRect{};
    return TileRect{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                    static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0)};
}

float canvasEase(float deltaSeconds) {
    // Clamped both ways: a first frame with dt 0 would freeze the ease at zero
    // and a hitch of half a second would teleport. ~150ms settle either way.
    const float dt = std::clamp(deltaSeconds, 1.0f / 240.0f, 1.0f / 30.0f);
    return 1.0f - std::exp(-18.0f * dt);
}

namespace {

// Seconds, with enough digits at both ends of the range this pipeline actually
// spans: a no-glass 512 is a few hundredths and a glass 1024 has been eighty-
// seven. Two decimals, so "0.04 s" and "87.31 s" read the same way. The unit is
// in the string because a bare number in a table of gap sentences reads as an
// index, not a duration.
std::string secondsText(double s) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f s", s);
    return buf;
}

// The four controls the target keeps on the canvas toolbar and footer (spec
// 13/09 §7). Each writes `Session::view` directly: what a person is looking
// THROUGH is not part of the document, so none of these is a command and none
// of them belongs on the undo stack.
std::size_t contextBar(Session& s) {
    std::size_t n = 0;

    ImGui::SetNextItemWidth(90);
    if (ImGui::BeginCombo("##appearance", appearanceLabel(s.view.context.appearance))) {
        for (auto a : {icf::Appearance::Base, icf::Appearance::Light, icf::Appearance::Dark,
                       icf::Appearance::Tinted}) {
            if (ImGui::Selectable(appearanceLabel(a), a == s.view.context.appearance)) {
                s.view.context.appearance = a;
            }
        }
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    // Three unlabelled 90px combos in a row is three ways to pick the wrong one,
    // and this is the one that changes the PROPORTIONS -- a `position`
    // specialization moves the art's scale and offset. The tooltip goes on the
    // combo button, which is the last item straight after `BeginCombo`, not
    // after `EndCombo` (which closes the popup).
    const bool idiomOpen = ImGui::BeginCombo("##idiom", idiomLabel(s.view.context.idiom));
    {
        const icf::Idiom declared = declaredIdiom(s.root());
        std::string tip = "Idiom -- which platform's composition the canvas draws. "
                          "`position`, `hidden` and `image-name` can each be specialized per idiom, "
                          "so this is what makes the art change size or move.\nThis document declares "
                          "supported-platforms " + declaredPlatformsText(s.root()) + ", so it opens on " +
                          idiomLabel(declared) + ".";
        if (s.view.context.idiom != declared) {
            tip += "\nYou are looking at ";
            tip += idiomLabel(s.view.context.idiom);
            tip += ", which is not what the document declares.";
        }
        ImGui::SetItemTooltip("%s", tip.c_str());
    }
    if (idiomOpen) {
        for (auto i : {icf::Idiom::Base, icf::Idiom::Square, icf::Idiom::IOS, icf::Idiom::MacOS,
                       icf::Idiom::WatchOS}) {
            if (ImGui::Selectable(idiomLabel(i), i == s.view.context.idiom)) s.view.context.idiom = i;
        }
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char sizeLabel[16];
    std::snprintf(sizeLabel, sizeof sizeLabel, "%u px", s.view.size);
    if (ImGui::BeginCombo("##size", sizeLabel)) {
        if (ImGui::Selectable("512 px", s.view.size == 512)) s.view.size = 512;
        if (ImGui::Selectable("1024 px", s.view.size == 1024)) s.view.size = 1024;
        ImGui::EndCombo();
    }
    ++n;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    // TWO CONTROLS THAT DISAGREE ARE WORSE THAN ONE. The combo reads and writes
    // the SAME number the wheel does, so a wheel zoom to 137% shows "137%" here
    // rather than leaving a stale "100%" next to an icon that is plainly not at
    // 100%. It shows the TARGET, not the eased value: mid-ease the number would
    // otherwise flicker through every intermediate percentage.
    char zoomLabel[16];
    std::snprintf(zoomLabel, sizeof zoomLabel, "%d%%",
                  static_cast<int>(s.view.zoomTarget * 100.0f + 0.5f));
    if (ImGui::BeginCombo("##zoom", zoomLabel)) {
        for (float z : {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f}) {
            char l[16];
            std::snprintf(l, sizeof l, "%d%%", static_cast<int>(z * 100.0f + 0.5f));
            // The request, not the target: the canvas is the only place that
            // knows where the viewport centre is, and a zoom that is not
            // anchored anywhere throws the icon off screen (Session.h).
            if (ImGui::Selectable(l, s.view.zoomTarget == z)) s.view.zoomRequest = z;
        }
        ImGui::EndCombo();
    }
    ++n;

    return n;
}

// Zoom out / zoom in / 1:1 / Fit -- the four buttons Onyx's viewer carries
// (ImageViewer.cpp:130-136). They write the same two request fields the combo
// writes, so there is exactly one path into the view transform.
std::size_t zoomBar(Session& s) {
    ImGui::SameLine();
    if (ImGui::SmallButton("-")) s.view.zoomRequest = s.view.zoomTarget / 1.5f;
    ImGui::SetItemTooltip("Zoom out. The mouse wheel over the canvas does the same, "
                          "anchored on the pointer.");
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) s.view.zoomRequest = s.view.zoomTarget * 1.5f;
    ImGui::SetItemTooltip("Zoom in.");
    ImGui::SameLine();
    if (ImGui::SmallButton("1:1")) s.view.zoomRequest = 1.0f;
    ImGui::SetItemTooltip("One texel of the preview to one pixel on screen.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) s.view.fitRequest = true;
    ImGui::SetItemTooltip("Fit the whole icon in the canvas and centre it. "
                          "This is where the canvas opens.");
    return 4;
}

// A FRASE ELIDIDA QUE CABE NUMA BARRA. O motivo de uma falha de escrita e uma
// linha do sistema de arquivos -- caminho inteiro mais a mensagem do
// `error_code` -- e vai facil a duzentos caracteres. Inteira na barra ela
// empurra o canvas para baixo e some na borda da janela; o lugar dela inteira
// e o painel de Diagnostics, que quebra linha numa tabela feita para isso.
// Aqui fica o comeco, que e onde estao a operacao e o arquivo.
//
// Elide em BYTES e nao em caracteres: o corte pode cair no meio de uma
// sequencia UTF-8 e a fonte do ImGui desenharia o pedaco como um losango. Por
// isso o corte recua ate o inicio de um caractere -- os bytes de continuacao
// sao `10xxxxxx`.
std::string elide(std::string_view text, std::size_t budget) {
    if (text.size() <= budget) return std::string(text);
    std::size_t cut = budget;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return std::string(text.substr(0, cut)) + "...";
}

}  // namespace

CanvasStats drawCanvas(Session& s, const RenderView& view, MenuActions& actions,
                       std::string_view trouble) {
    CanvasStats st;
    // The canvas is the window that carries the menu bar: Onyx owns the frame,
    // and one of our windows has to hold the bar we draw ourselves (spec §7).
    if (!ImGui::Begin(kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return st;
    }
    st.menu = drawMenuBar(s, actions);
    st.contextControls = contextBar(s);
    st.zoomControls = zoomBar(s);
    if (view.pending) {
        ImGui::SameLine();
        ImGui::TextDisabled("rendering…");
    }
    // O QUE ESTA NA TELA NAO E O QUE FOI PEDIDO. `refined` era escrito pelo
    // coordenador e lido por ninguem: quando o ladrilho nao cabe, o canvas
    // mostra a resolucao base esticada, que a esta altura do zoom e um borrao
    // -- e o unico sinal disso era uma nota tecnica dentro do painel de
    // Diagnostics, que ninguem abre para entender por que a imagem piorou.
    // Aqui, ao lado do zoom, e onde a pessoa ja esta olhando quando piora.
    if (!view.refined) {
        st.stretchedNotice = true;
        ImGui::SameLine();
        ImGui::TextDisabled("stretched");
        ImGui::SetItemTooltip("The tile this zoom asks for does not fit, so this is the base "
                              "resolution stretched. Diagnostics says which ceiling refused it.");
    }

    // A FALHA DE ESCRITA, ONDE A PESSOA ESTA OLHANDO.
    //
    // Medido 18/09 (laudo §4.2): `State::act()` mandava o motivo de `save`, de
    // `saveAs` e da criacao de documento para `stderr`, e uma janela nao tem
    // stderr. `State::trouble` existia e so era desenhado no canvas VAZIO --
    // some no instante em que ha documento, que e o unico instante em que
    // salvar pode falhar. O resultado media exatamente isto: Ctrl+S num disco
    // cheio, nenhum pixel muda, o titulo continua com o asterisco e nada na
    // tela diz por que.
    //
    // Por que AQUI e nao um modal: um modal interrompe, e a falha ja
    // aconteceu -- nao ha decisao a tomar, ha um fato a saber. Esta barra e
    // onde "rendering..." e "stretched" ja moram, esta a dois centimetros do
    // Ctrl+S que acabou de ser apertado, e e vermelha, que e a unica cor que
    // este editor usa para dizer que algo esta errado (a mesma `kAlarm` do
    // painel de asset). O texto INTEIRO esta no Diagnostics, e o tooltip o
    // repete aqui para quem nao quer abrir o painel.
    //
    // E fica em LINHA PROPRIA, nao em `SameLine`: a frase e longa e, ao lado
    // dos combos, ou empurra o zoom para fora da janela ou e cortada
    // justamente onde esta a informacao.
    if (!trouble.empty()) {
        st.trouble = elide(trouble, 96);
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s", st.trouble.c_str());
        ImGui::SetItemTooltip("%.*s\n\nThe whole line is in the Diagnostics panel, under `write`.",
                              static_cast<int>(trouble.size()), trouble.data());
    }

    ViewContext& v = s.view;
    const ImVec2 availRaw = ImGui::GetContentRegionAvail();
    const float availW = availRaw.x > 1.0f ? availRaw.x : 1.0f;
    const float availH = availRaw.y > 1.0f ? availRaw.y : 1.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 corner(origin.x + availW, origin.y + availH);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // The ruler for every number below is the PREVIEW SIZE, not the texture:
    // since the viewport render (spec 2026-09-16) the texture may be a tile of
    // a larger grid, and measuring the canvas by it would move the icon every
    // time a tile landed. It was already the stand-in before the first render
    // lands -- it is what that render will be, so the fit computed from it is
    // the fit the picture arrives into -- and now it is the ruler always.
    const float sidePx = static_cast<float>(v.size);

    // ── Fit on open ─────────────────────────────────────────────────────────
    // `fitted` is false in a freshly opened Session and nowhere else, so this is
    // "when a document opens" without an event to plumb. No ease: there is
    // nothing on screen yet to ease away from.
    if (!v.fitted) {
        v.zoomTarget = canvasFitZoom(availW, availH, sidePx);
        const CanvasVec c = canvasCentrePan(availW, availH, sidePx, v.zoomTarget);
        v.panTargetX = c.x;
        v.panTargetY = c.y;
        v.zoom = v.zoomTarget;
        v.panX = c.x;
        v.panY = c.y;
        v.fitted = true;
    }

    // Flat colour behind the icon (spec 13/09 §6). Flat and KNOWN: the six
    // `sine-*` backdrops of the target are round 5, and a backdrop taken from
    // the theme could be fully transparent, which would put the icon's own
    // alpha over the window and make transparency unreadable.
    dl->AddRectFilled(origin, corner, IM_COL32(40, 40, 44, 255));

    // Left OR middle drag pans, which is what Onyx's viewer accepts
    // (ImageViewer.cpp:199-200). Left as well as middle because a middle button
    // is the one control a trackpad does not have.
    ImGui::InvisibleButton("##canvas", ImVec2(availW, availH),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    // ── UM CLIQUE, QUE NAO E UM ARRASTO ─────────────────────────────────────
    // O botao esquerdo faz as duas coisas, e o que as separa e "soltou onde
    // apertou". A folga e de dois pixels e nao de zero: o pan comeca com
    // QUALQUER movimento (`IsMouseDragging(.., 0.0f)` logo abaixo), entao
    // exigir imobilidade perfeita faria o tremor de uma mao perder a selecao,
    // e dois pixels de pan a mais ninguem ve. `MouseDragMaxDistanceSqr` zera
    // no aperto (imgui.cpp:10908) e acumula enquanto o botao esta em baixo,
    // entao no quadro do soltar ele e o afastamento MAXIMO do gesto inteiro --
    // um arrasto de ida e volta nao passa por clique.
    //
    // A ESCOLHA fica para depois do ease: ela precisa do canto e do lado que o
    // pan e o zoom deste quadro produzem, e eles so existem umas linhas abaixo.
    constexpr float kClickSlackSqr = 4.0f;
    const bool clicked = ImGui::IsItemDeactivated() &&
                         ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                         io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] <= kClickSlackSqr;

    // One path into the view transform, used by the wheel, the combo and the
    // three zoom buttons alike.
    auto zoomTo = [&](float wanted, CanvasVec anchor) {
        const float target = canvasClampZoom(wanted);
        if (target == v.zoomTarget) return;
        const CanvasVec p =
            canvasZoomAnchored(CanvasVec{v.panTargetX, v.panTargetY}, v.zoomTarget, target, anchor);
        v.panTargetX = p.x;
        v.panTargetY = p.y;
        v.zoomTarget = target;
    };
    const CanvasVec centre{availW * 0.5f, availH * 0.5f};

    if (v.fitRequest) {
        v.fitRequest = false;
        v.zoomTarget = canvasFitZoom(availW, availH, sidePx);
        const CanvasVec c = canvasCentrePan(availW, availH, sidePx, v.zoomTarget);
        v.panTargetX = c.x;
        v.panTargetY = c.y;
    }
    // A control with no pointer of its own zooms about the middle of the canvas,
    // which is where the icon is when nobody has dragged it.
    if (v.zoomRequest > 0.0f) {
        zoomTo(v.zoomRequest, centre);
        v.zoomRequest = 0.0f;
    }

    // THE WHEEL, ANCHORED ON THE POINTER. 1.15 per notch is Onyx's rate
    // (ImageViewer.cpp:191) and the anchoring is the whole point: the texel
    // under the cursor is the one thing that must not move while the rest grows
    // around it.
    if (hovered && io.MouseWheel != 0.0f) {
        zoomTo(v.zoomTarget * std::pow(1.15f, io.MouseWheel),
               CanvasVec{io.MousePos.x - origin.x, io.MousePos.y - origin.y});
    }

    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
                   ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        v.panTargetX += io.MouseDelta.x;
        v.panTargetY += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Bounded against the TARGET, so the target stays somewhere the ease can
    // actually arrive at; clamping the eased value instead would let the two
    // disagree forever and the icon would creep.
    const CanvasVec bounded =
        canvasClampPan(CanvasVec{v.panTargetX, v.panTargetY}, availW, availH, sidePx, v.zoomTarget);
    v.panTargetX = bounded.x;
    v.panTargetY = bounded.y;

    // ── The ease: the "pan suave" that was missing ──────────────────────────
    // One coefficient for zoom and both pan axes, because the anchored zoom's
    // arithmetic only holds while they move together -- ease them at different
    // rates and the anchored point drifts during the settle.
    const float k = canvasEase(io.DeltaTime);
    v.zoom += (v.zoomTarget - v.zoom) * k;
    v.panX += (v.panTargetX - v.panX) * k;
    v.panY += (v.panTargetY - v.panY) * k;
    // Snap inside a sub-pixel, or the lerp never ends and the canvas re-draws a
    // still picture forever.
    if (std::fabs(v.zoomTarget - v.zoom) < 0.0005f) v.zoom = v.zoomTarget;
    if (std::fabs(v.panTargetX - v.panX) < 0.25f) v.panX = v.panTargetX;
    if (std::fabs(v.panTargetY - v.panY) < 0.25f) v.panY = v.panTargetY;

    const ImVec2 tl(origin.x + v.panX, origin.y + v.panY);
    st.clip = CanvasRect{origin.x, origin.y, corner.x, corner.y};
    st.image = canvasImageRect(CanvasVec{tl.x, tl.y}, sidePx, v.zoom);
    st.painted = canvasIntersect(st.image, st.clip);
    st.zoom = v.zoom;

    // O lado do CANVAS INTEIRO na tela. E nele que a arte e colocada -- um
    // ladrilho e um pedaco DESSA praca, e nao muda onde a camada esta -- e
    // portanto e a regua da selecao tambem.
    const float fullSide = sidePx * v.zoom;

    // ── O CLIQUE ESCOLHE A CAMADA ───────────────────────────────────────────
    // Escrito em `Session::selection`, que e a MESMA que a arvore le: nao ha
    // uma "selecao do canvas". A arvore acende a linha e rola ate ela no
    // quadro seguinte (`PanelLayers.cpp`, `scrollToSelection`), sem que nada
    // aqui saiba que ela existe. Vazio limpa, porque `canvasLayerAt` devolve
    // nada e essa nada e a atribuicao.
    if (clicked) {
        s.selection = canvasLayerAt(s.root(), s.view.context, CanvasVec{tl.x, tl.y}, fullSide,
                                    CanvasVec{io.MousePos.x, io.MousePos.y});
    }

    // ── O LADRILHO, SO COM O PAN E O ZOOM PARADOS ───────────────────────────
    // Spec 2026-09-16, "O que o Kit faz": nao ha pedido por quadro. Durante um
    // arrasto a chave mudaria a cada quadro e o agendador so descartaria
    // trabalho, e o intervalo sem imagem nitida viraria o arrasto inteiro. O
    // coordenador pede o que estiver em `v.tile`, e enquanto a pessoa arrasta
    // isto nao muda -- o ladrilho velho e o que continua na tela.
    //
    // A comparacao e de igualdade exata porque o ease acima GRAMPEIA o valor
    // no alvo dentro de meio pixel (as tres linhas de snap); sem elas o pan
    // nunca chegaria ao alvo e nada aqui jamais assentaria.
    //
    // E O RETANGULO PINTADO ENTRA NA CONDICAO, nao so o zoom e o pan. O
    // ladrilho sai de `st.painted`, que depende do espaco disponivel do
    // painel: redimensionar a janela ou arrastar o splitter do dock em
    // zoom > 1 mexe nele com o zoom e o pan PARADOS. So com os dois na
    // condicao, `settled` continuava verdadeiro durante o gesto inteiro, o
    // bloco abaixo reescrevia `v.tile` a cada quadro e o coordenador disparava
    // um pedido por quadro -- cada um com um `bundle().clone()` e um render de
    // ladrilho reiniciado a cada conclusao. O mesmo caso que este temporizador
    // existe para evitar, alcancado por outro gesto.
    //
    // A comparacao e do LADRILHO que sairia, e nao de `painted` cru: e ele que
    // vira pedido, ele ja tem `operator==` e e inteiro (entao um pixel de
    // jitter em `painted` que nao muda ladrilho nenhum nao reinicia a espera).
    // Com zoom <= 1 ele e sempre `TileRect{}`, entao esta condicao nao muda
    // nada la -- vale o caminho de sempre.
    const TileRect want = canvasTileFor(st.painted, CanvasVec{tl.x, tl.y}, v.size, v.zoom);
    const bool settled = v.zoom == v.zoomTarget && v.panX == v.panTargetX &&
                         v.panY == v.panTargetY && want == v.lastWanted;
    v.lastWanted = want;
    v.settledSeconds = settled ? v.settledSeconds + io.DeltaTime : 0.0f;
    if (v.settledSeconds >= kTileSettleSeconds) {
        v.tileSize = want.w ? canvasTileSize(v.size, v.zoom) : 0;
        v.tile = want;
    }

    // ── THE RECORTE ─────────────────────────────────────────────────────────
    // Everything from here to PopClipRect is the canvas's own rectangle and
    // nothing else. The window's draw list is otherwise clipped to the window's
    // INNER rect, which begins under the menu bar and therefore includes the row
    // of combos this function drew a few lines ago -- an icon larger than the
    // canvas painted straight over them. `true` intersects with the current clip
    // rather than replacing it, so the window's own bounds still apply.
    dl->PushClipRect(origin, corner, true);

    // A TEXTURA VAI NO RETANGULO QUE ELA COBRE, e nao mais no canvas inteiro
    // (spec 2026-09-16, "O que o Kit faz"). `fullSide` e o canvas inteiro na
    // tela; `perTexel` leva um pixel da grade do render a um pixel de tela, e
    // a textura ocupa `width` x `height` a partir de (`originX`, `originY`)
    // DESSA grade. Com a grade igual ao canvas e origem zero -- todo render
    // que nao e ladrilho -- isto e exatamente o `AddImage` de antes.
    //
    // E e por aqui que a espera funciona: a grade vem do render que esta na
    // tela, nao do que foi pedido, entao o ladrilho velho fica ancorado na
    // imagem e acompanha o pan; um zoom o estica ate o novo chegar.
    if (view.texture != ImTextureID_Invalid && view.width > 0) {
        const float grid = view.gridSize > 0 ? static_cast<float>(view.gridSize)
                                             : static_cast<float>(view.width);
        const float perTexel = fullSide / grid;
        const ImVec2 a(tl.x + static_cast<float>(view.originX) * perTexel,
                       tl.y + static_cast<float>(view.originY) * perTexel);
        const ImVec2 b(a.x + static_cast<float>(view.width) * perTexel,
                       a.y + static_cast<float>(view.height) * perTexel);
        dl->AddImage(view.texture, a, b);
        st.textured = true;
    }

    // ── O RETANGULO DA CAMADA SELECIONADA ───────────────────────────────────
    // FORA do bloco da textura: a selecao e geometria, nao pixels. Enquanto o
    // primeiro render esta a caminho a camada ja tem um lugar, e esconder a
    // moldura ate a textura chegar seria esconder a unica coisa que o canvas
    // sabe responder sem um render.
    //
    // E DESENHADO DO MESMO `canvasLayerRect` QUE O CLIQUE ACERTA, de proposito:
    // o que a pessoa ve e o alvo, sempre. Dentro do recorte, como tudo aqui.
    if (s.selection && s.selection->layer) {
        const CanvasRect r =
            canvasLayerRect(s.root(), *s.selection, s.view.context, CanvasVec{tl.x, tl.y}, fullSide);
        if (!r.empty()) {
            dl->AddRect(ImVec2(r.x0, r.y0), ImVec2(r.x1, r.y1), IM_COL32(80, 160, 255, 255), 0.0f, 0,
                        2.0f);
            st.selectionDrawn = true;
            st.selection = r;
        }
    }

    // The provisional mark: these pixels are not the answer to what the context
    // bar now says. Small and in the corner, because it is on screen during
    // every edit and must not compete with the icon. Inside the clip, like
    // everything else the canvas draws.
    if (view.pending) {
        dl->AddCircleFilled(ImVec2(corner.x - 12.0f, origin.y + 12.0f), 4.0f, IM_COL32(230, 180, 60, 255));
    }

    dl->PopClipRect();

    ImGui::End();
    return st;
}

DiagnosticsStats drawDiagnostics(const Session& s, const RenderView& view,
                                 std::string_view trouble) {
    DiagnosticsStats st;
    if (!ImGui::Begin(kDiagnosticsWindow)) {
        ImGui::End();
        return st;
    }

    auto row = [&](const char* origin, const std::string& text) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(origin);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", text.c_str());
        ++st.rows;
    };

    if (ImGui::BeginTable("diag", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("origin", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("text");

        // PRIMEIRA LINHA, E INTEIRA. A barra do canvas elide para caber; aqui
        // e o "algum lugar" onde o texto completo tem de estar, e a celula
        // quebra linha (`TextWrapped`) porque uma mensagem do sistema de
        // arquivos traz o caminho inteiro. Primeira porque e a unica linha
        // desta tabela sobre algo que FALHOU -- as outras descrevem o que o
        // render fez.
        if (!trouble.empty()) {
            st.trouble = std::string(trouble);
            row("write", st.trouble);
        }

        // Always present, even when nothing is wrong: "191 of 194" is the line
        // that makes a missing layer visible at a glance.
        //
        // AND HOW LONG IT TOOK, since 2026-09-15. This row used to end at the
        // layer count, and on the day six fronts turned a 1024 px glass render
        // into eighty-seven seconds the panel had nothing to say: no layer was
        // missing, no gap was reported, the arithmetic was right, and the canvas
        // was empty. `RenderView::lastRenderSeconds` is negative until a render
        // has finished, which is not zero and must not print as `0.00 s`.
        std::string what =
            std::to_string(view.drawn) + " of " + std::to_string(view.total) + " layer(s) drawn";
        if (view.lastRenderSeconds >= 0.0) what += " in " + secondsText(view.lastRenderSeconds);
        row("render", what);
        // THE ONE IN FLIGHT, counted while it is still running. The canvas
        // already shows a yellow dot for `pending`, and a dot cannot tell forty
        // milliseconds from a minute and a half -- which is exactly the sentence
        // the user needed and did not get.
        if (view.pending) {
            row("render", "a newer render has been running for " +
                              secondsText(view.pendingSeconds));
        }
        if (!view.error.empty()) row("render", view.error);

        // WHICH COMPOSITION IS ON SCREEN, ALWAYS, AND WHETHER IT IS THE ONE THE
        // DOCUMENT DECLARES. A `position` specialization changes the art's scale
        // and offset, so "the proportions look wrong" and "I am on the wrong
        // idiom" are the same sentence -- and until this row existed there was
        // nothing on screen that said which idiom was active. The combo alone
        // could not: it is 90px of unlabelled text among two identical
        // neighbours.
        const icf::Idiom declared = declaredIdiom(s.root());
        std::string where = std::string("idiom ") + idiomLabel(s.view.context.idiom) + ", appearance " +
                            appearanceLabel(s.view.context.appearance) + "; the document declares " +
                            declaredPlatformsText(s.root());
        if (s.view.context.idiom != declared) {
            where += " -- which is ";
            where += idiomLabel(declared);
            where += ", not what the canvas is showing";
        }
        row("view", where);
        for (const auto& x : view.skipped) row("skipped", x);
        for (const auto& x : view.shapeGaps) row("shape", x);
        // `[OBS]` -- drawn, but on a ruler or a shape that was guessed rather
        // than read. The mark is the same one the docs use.
        for (const auto& x : view.notes) row("[OBS]", x);

        // These two are the DOCUMENT's gaps, not the render's: they are true
        // whether or not a render has ever run.
        const std::vector<std::string> missing = s.bundle().missingAssets();
        for (const auto& x : missing) row("asset", "missing: " + x);
        const std::vector<std::string> unknown = s.bundle().document().unknownKeys();
        for (const auto& x : unknown) row("key", "unknown: " + x);

        ImGui::EndTable();
    }

    ImGui::End();
    return st;
}

}  // namespace ick
