#include "Source/app/TrafficLights.h"

#include "Source/CoreSVG/Document.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/SvgRenderer.h"
#include "Source/app/NativeWindow.h"
#include "Source/app/Shell.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace icapp {
namespace {

namespace fs = std::filesystem;

fs::path exeDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#endif
    return fs::current_path();
}

std::vector<std::uint8_t> toRgba8(const std::vector<float>& rgba) {
    std::vector<std::uint8_t> out(rgba.size());
    for (std::size_t i = 0; i < rgba.size(); ++i)
        out[i] = static_cast<std::uint8_t>(std::lround(std::clamp(rgba[i], 0.0f, 1.0f) * 255.0f));
    return out;
}

ImTextureID loadBody(TexturePool& pool, const fs::path& file) {
    const icf::DecodedPng png = icf::readPng(file.string());
    if (!png.error.empty() || png.width == 0) return 0;
    const std::vector<std::uint8_t> px = toRgba8(png.rgba);
    return pool.create(png.width, png.height, px.data(), nullptr);
}

// O glifo e um template PRETO (README §3.4): vira uma mascara branca com o
// alfa do desenho, e a cor da tabela §3.3 entra como tinta do `AddImage`.
ImTextureID loadGlyph(TexturePool& pool, rb::Device& device, const fs::path& file, std::uint32_t px) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return 0;
    const std::string raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto doc = icf::svg::SvgDocument::parse(raw);
    if (!doc) return 0;
    rb::RenderOptions o;
    o.width = px;
    o.height = px;
    auto img = rb::renderSvg(device, *doc, o);
    if (!img || img->width == 0) {
        std::fprintf(stderr, "IconComposer: glyph %s: %s\n", file.filename().string().c_str(),
                     img ? "empty" : img.error().c_str());
        return 0;
    }
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(img->width) * img->height * 4);
    for (std::size_t i = 0, n = mask.size() / 4; i < n; ++i) {
        mask[i * 4 + 0] = mask[i * 4 + 1] = mask[i * 4 + 2] = 255;
        mask[i * 4 + 3] = static_cast<std::uint8_t>(
            std::lround(std::clamp(img->rgba[i * 4 + 3], 0.0f, 1.0f) * 255.0f));
    }
    return pool.create(img->width, img->height, mask.data(), nullptr);
}

// A tabela da cor do glifo, aparencia ESCURA (README §3.3, `0x240541e7c`).
constexpr ImVec4 kGlyphClose(0.55f, 0.0f, 0.0f, 1.0f);
constexpr ImVec4 kGlyphMinimize(0.65f, 0.228f, 0.0f, 1.0f);
constexpr ImVec4 kGlyphExpand(0.18f, 0.45f, 0.0f, 1.0f);

}  // namespace

fs::path appleAssetsDir() {
    if (const char* env = std::getenv("IC_APPLE_ASSETS")) {
        std::error_code ec;
        if (fs::is_directory(env, ec)) return env;
    }
    fs::path dir = exeDir();
    for (int i = 0; i < 8 && !dir.empty(); ++i) {
        std::error_code ec;
        const fs::path probe = dir / "ui" / "public" / "apple";
        if (fs::exists(probe / "traffic" / "Close.svg", ec)) return probe;
        const fs::path up = dir.parent_path();
        if (up == dir) break;
        dir = up;
    }
    return {};
}

void TrafficLights::load(TexturePool& pool, rb::Device& device, float dpiScale) {
    const fs::path root = appleAssetsDir();
    if (root.empty()) {
        std::fprintf(stderr, "IconComposer: ui/public/apple not found; the window lights are drawn flat "
                             "(set IC_APPLE_ASSETS to the folder)\n");
        return;
    }
    const fs::path t = root / "traffic";
    // O 2x sempre: o corpo encolhe limpo a 1x, e um monitor de 150% pediria
    // 33 px de um bitmap de 22.
    close_.id = loadBody(pool, t / "Close-dark-2x.png");
    minimize_.id = loadBody(pool, t / "Minimize-dark-2x.png");
    expand_.id = loadBody(pool, t / "Expand-dark-2x.png");
    inactive_.id = loadBody(pool, t / "Inactive-dark-2x.png");
    // A mascara a quatro vezes os 14 pt do PDF (vezes o DPI): o AddImage
    // reduz com bilinear, e o traco fino continua liso.
    const auto gpx = static_cast<std::uint32_t>(std::lround(14.0f * 4.0f * std::max(1.0f, dpiScale)));
    glyphClose_.id = loadGlyph(pool, device, t / "Close.svg", gpx);
    glyphUnsaved_.id = loadGlyph(pool, device, t / "CloseUnsaved.svg", gpx);
    glyphMinimize_.id = loadGlyph(pool, device, t / "Minimize.svg", gpx);
    glyphZoom_.id = loadGlyph(pool, device, t / "Zoom.svg", gpx);
    glyphFullEnter_.id = loadGlyph(pool, device, t / "FullScreenEnter.svg", gpx);
    glyphFullExit_.id = loadGlyph(pool, device, t / "FullScreenExit.svg", gpx);
    loaded_ = close_.id && minimize_.id && expand_.id && inactive_.id;
    if (!loaded_) std::fprintf(stderr, "IconComposer: the window light bodies did not load from %s\n",
                               t.string().c_str());
}

// A mola do SwiftUI: `response` e o periodo natural (omega = 2 pi / response) e
// `dampingFraction` o amortecimento relativo. Euler semi-implicito em passos de
// no maximo 1/240 s: a de resposta 0.1 tem omega ~ 63 e nao e estavel num passo
// de quadro inteiro.
void TrafficLights::Spring::step(float target, float response, float damping, float dt) {
    if (!(dt > 0.0f) || !(response > 0.0f)) {
        value = target;
        velocity = 0.0f;
        return;
    }
    const float omega = 6.2831853f / response;
    const int steps = std::clamp(static_cast<int>(std::ceil(dt * 240.0f)), 1, 32);
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        velocity += (-omega * omega * (value - target) - 2.0f * damping * omega * velocity) * h;
        value += velocity * h;
    }
    // Assentada: sem isto a mola rasteja para sempre a um milesimo do alvo.
    if (std::fabs(value - target) < 0.0005f && std::fabs(velocity) < 0.005f) {
        value = target;
        velocity = 0.0f;
    }
}

float TrafficLights::draw(Shell& shell, ImVec2 topLeft, float scale, bool dirty) {
    // Geometria (README §4): disco de 14 pt, 9 pt entre eles.
    const float d = 14.0f * scale, gap = 9.0f * scale;
    const float groupW = 3 * d + 2 * gap;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 groupMax(topLeft.x + groupW, topLeft.y + d);
    // O hover e do GRUPO (§3.2, `_mouseInGroup:`): o ponteiro sobre o grupo da
    // `rollover` as tres luzes.
    const bool inGroup = ImGui::IsMouseHoveringRect(topLeft, groupMax, false);
    // O grupo e controle, nao barra: o sistema nao pode arrastar a janela por
    // ele (NativeWindow.h). Em coordenadas da area cliente, que sao as do
    // viewport principal.
    {
        const ImVec2 vp = ImGui::GetMainViewport()->Pos;
        const float m = 4.0f * scale;
        NativeWindow::addControlRect(topLeft.x - vp.x - m, topLeft.y - vp.y - m, groupMax.x - vp.x + m,
                                     groupMax.y - vp.y + m);
    }
    const bool windowActive = shell.focused();
    const bool option = ImGui::GetIO().KeyAlt;
    const float dt = ImGui::GetIO().DeltaTime;

    // O VERDE (§2.1): em tela cheia, sair; fora dela, entrar -- ou o "+" do
    // zoom com Option. A tela cheia daqui e a janela maximizada.
    const Tex* green = shell.maximized() ? &glyphFullExit_ : option ? &glyphZoom_ : &glyphFullEnter_;

    struct Light {
        const char* id;
        const Tex* body;
        const Tex* glyph;
        ImVec4 glyphColour;
        ImU32 flat;
    };
    const Light lights[3] = {
        {"##tl-close", &close_, dirty ? &glyphUnsaved_ : &glyphClose_, kGlyphClose, IM_COL32(0xff, 0x5f, 0x57, 255)},
        {"##tl-minimize", &minimize_, &glyphMinimize_, kGlyphMinimize, IM_COL32(0xfe, 0xbc, 0x2e, 255)},
        {"##tl-zoom", &expand_, green, kGlyphExpand, IM_COL32(0x28, 0xc8, 0x40, 255)},
    };
    // `Settings.windowControl` (§4.1): a dimensao associada e os pontos do lift.
    constexpr float kAssociatedDimension = 14.0f, kLiftScalePoints = 2.0f;
    constexpr float kLift = (kAssociatedDimension + kLiftScalePoints) / kAssociatedDimension;

    for (int i = 0; i < 3; ++i) {
        const ImVec2 a(topLeft.x + i * (d + gap), topLeft.y);
        const ImVec2 b(a.x + d, a.y + d);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(lights[i].id, ImVec2(d, d));
        const bool held = ImGui::IsItemActive();
        // `isHighlighted`: apertada E com o ponteiro em cima.
        const bool pressed = held && ImGui::IsMouseHoveringRect(a, b, false);
        // `InteractionState` (§3.2): pressed 3, rollover 2, idle 0.
        const bool idle = !pressed && !inGroup;
        // `isDimmed = disabled || (idle && !windowAppearsActive)`; nada aqui
        // fica disabled.
        const bool dimmed = idle && !windowActive;

        // O Flex: o gesto e um toque longo que comeca dentro do controle, e a
        // escala sobe enquanto ele esta ativo.
        scale_[i].step(held ? kLift : 1.0f, 0.355f, 0.4f, dt);
        const float k = scale_[i].value;
        const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const float half = d * 0.5f * k;

        // O corpo: 22 pt, o disco de 14 no meio, entao 4 pt de sombra em volta.
        const Tex* body = dimmed ? &inactive_ : lights[i].body;
        if (loaded_ && body->id) {
            const float bodyHalf = (d * 0.5f + 4.0f * scale) * k;
            dl->AddImage(body->id, ImVec2(c.x - bodyHalf, c.y - bodyHalf), ImVec2(c.x + bodyHalf, c.y + bodyHalf));
        } else {
            dl->AddCircleFilled(c, half, dimmed ? IM_COL32(0x4a, 0x4a, 0x4e, 255) : lights[i].flat, 32);
        }

        // O glifo: opacidade 0 em idle, `revealAmount` (1) de rollover em
        // diante; o ponto de nao salvo e isento. A mola e sobre
        // `interactionState > 1`.
        const bool unsaved = i == 0 && dirty;
        glyph_[i].step(!idle || unsaved ? 1.0f : 0.0f, 0.1f, 1.0f, dt);
        const float opacity = std::clamp(glyph_[i].value, 0.0f, 1.0f);
        if (opacity > 0.0f && lights[i].glyph->id) {
            ImVec4 colour = dimmed ? ImVec4(0, 0, 0, 1) : lights[i].glyphColour;
            colour.w = opacity;
            dl->AddImage(lights[i].glyph->id, ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half),
                         ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(colour));
        }

        if (clicked) {
            if (i == 0) shell.close();
            else if (i == 1) shell.minimize();
            else shell.toggleMaximize();
        }
    }
    return groupW;
}

}  // namespace icapp
