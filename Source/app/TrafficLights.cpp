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
    loaded_ = close_.id && minimize_.id && expand_.id && inactive_.id;
    if (!loaded_) std::fprintf(stderr, "IconComposer: the window light bodies did not load from %s\n",
                               t.string().c_str());
}

float TrafficLights::draw(Shell& shell, ImVec2 topLeft, float scale, bool dirty) {
    // Geometria (README §4, o kit): disco de 14 pt, 9 pt entre eles.
    const float d = 14.0f * scale, gap = 9.0f * scale;
    const float groupW = 3 * d + 2 * gap;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 groupMax(topLeft.x + groupW, topLeft.y + d);
    // O hover e do GRUPO (§3.2): as tres luzes entram em rollover juntas.
    const bool hover = ImGui::IsMouseHoveringRect(topLeft, groupMax, false);
    // O grupo e controle, nao barra: o sistema nao pode arrastar a janela por
    // ele (NativeWindow.h). Em coordenadas da area cliente, que sao as do
    // viewport principal.
    {
        const ImVec2 vp = ImGui::GetMainViewport()->Pos;
        const float m = 4.0f * scale;
        NativeWindow::addControlRect(topLeft.x - vp.x - m, topLeft.y - vp.y - m, groupMax.x - vp.x + m,
                                     groupMax.y - vp.y + m);
    }
    // `isDimmed = disabled || (idle && !windowAppearsActive)`.
    const bool dimmed = !shell.focused() && !hover && !pressedAny_;
    const bool alt = ImGui::GetIO().KeyAlt;

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
        {"##tl-zoom", &expand_, alt ? &glyphZoom_ : &glyphFullEnter_, kGlyphExpand, IM_COL32(0x28, 0xc8, 0x40, 255)},
    };
    bool anyHeld = false;
    for (int i = 0; i < 3; ++i) {
        const ImVec2 a(topLeft.x + i * (d + gap), topLeft.y);
        const ImVec2 b(a.x + d, a.y + d);
        ImGui::SetCursorScreenPos(a);
        const bool clicked = ImGui::InvisibleButton(lights[i].id, ImVec2(d, d));
        anyHeld |= ImGui::IsItemActive();

        // O corpo: 22 pt, o disco de 14 no meio, entao 4 pt de sombra em volta.
        const Tex* body = dimmed ? &inactive_ : lights[i].body;
        if (loaded_ && body->id) {
            const float pad = 4.0f * scale;
            dl->AddImage(body->id, ImVec2(a.x - pad, a.y - pad), ImVec2(b.x + pad, b.y + pad));
        } else {
            dl->AddCircleFilled(ImVec2(a.x + d * 0.5f, a.y + d * 0.5f), d * 0.5f,
                                dimmed ? IM_COL32(0x4a, 0x4a, 0x4e, 255) : lights[i].flat, 32);
        }

        // O glifo: rollover ou pressed; o ponto de nao salvo, sempre.
        const bool unsaved = i == 0 && dirty;
        const bool shown = hover || pressedAny_ || unsaved;
        if (shown && lights[i].glyph->id) {
            const ImVec4 c = dimmed ? ImVec4(0, 0, 0, 1) : lights[i].glyphColour;
            dl->AddImage(lights[i].glyph->id, a, b, ImVec2(0, 0), ImVec2(1, 1), ImGui::ColorConvertFloat4ToU32(c));
        }

        if (clicked) {
            if (i == 0) shell.close();
            else if (i == 1) shell.minimize();
            else shell.toggleMaximize();
        }
    }
    pressedAny_ = anyHeld;
    return groupW;
}

}  // namespace icapp
