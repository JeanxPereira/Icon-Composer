#include "Source/RenderBox/SimulatedGlass.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/Parallel.h"
#include "Source/RenderBox/PixelGrid.h"

namespace rb {

namespace {

// `[BIN]` `SimulatedChiclet` (doc 03 §19.3).
constexpr double kRelativeBackdropBlurRadius = 0.0065;
constexpr double kResultBlurRadius = 2.0;
constexpr double kRelativeRefractionStrength = 0.28;
constexpr double kRelativeRefractionHeight = 0.11;
constexpr std::uint32_t kRefractionSupersampling = 2;

// A pastilha na grade: o campo (d, gx, gy, cobertura), o lado dela e o mapa de
// deslocamento da lente. So dependem do tamanho e da plataforma, entao ficam
// guardados entre renders -- o mapa tambem (02/10): a refracao e so funcao do
// campo e de `chiclet.side`, e era refeita a cada passo de um pan.
struct Chiclet {
    FieldImage field;
    double side = 0.0;
    GlassRefraction refraction;
    DisplacementImage displacement;
};

const Chiclet& chicletFor(std::uint32_t size, IconPlatform platform) {
    static std::mutex m;
    static std::map<std::pair<std::uint32_t, int>, std::unique_ptr<Chiclet>> cache;
    std::lock_guard lock(m);
    auto& slot = cache[{size, static_cast<int>(platform)}];
    if (!slot) {
        slot = std::make_unique<Chiclet>();
        const std::vector<FieldContour> contours = chicletFieldContours(size, platform);
        double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
        for (const FieldContour& c : contours) {
            for (std::size_t i = 0; i + 1 < c.xy.size(); i += 2) {
                x0 = std::min<double>(x0, c.xy[i]);
                x1 = std::max<double>(x1, c.xy[i]);
                y0 = std::min<double>(y0, c.xy[i + 1]);
                y1 = std::max<double>(y1, c.xy[i + 1]);
            }
        }
        slot->side = std::max(0.0, std::min(x1 - x0, y1 - y0));
        slot->field = generateFieldFromContours(contours, size, size);
        GlassRefraction& r = slot->refraction;
        r.heightPixels = static_cast<float>(kRelativeRefractionHeight * slot->side);
        // `displacementMap_v1`'s argumento e a forca NEGADA (0x805F4).
        r.scalePixels = static_cast<float>(-kRelativeRefractionStrength * slot->side);
        r.curvature = 1.0f;
        r.variant = kRefractionSupersampling;
        slot->displacement = glassDisplacementMap(slot->field, r);
    }
    return *slot;
}

// Gaussiana separavel sobre RGB (3 floats por pixel), bordas presas.
void gaussianBlur(std::vector<float>& rgb, std::uint32_t w, std::uint32_t h, double sigma) {
    if (sigma < 0.3) return;
    const int r = static_cast<int>(std::ceil(3.0 * sigma));
    std::vector<float> k(2 * r + 1);
    double sum = 0.0;
    for (int i = -r; i <= r; ++i) sum += k[i + r] = static_cast<float>(std::exp(-0.5 * i * i / (sigma * sigma)));
    for (float& v : k) v = static_cast<float>(v / sum);
    std::vector<float> tmp(rgb.size());
    const int W = static_cast<int>(w), H = static_cast<int>(h);
    parallelRanges(h, static_cast<std::size_t>(w) * h * (2 * r + 1) * 3, [&](std::size_t ya, std::size_t yb) {
    for (int y = static_cast<int>(ya); y < static_cast<int>(yb); ++y) {
        for (int x = 0; x < W; ++x) {
            float a[3] = {0, 0, 0};
            for (int i = -r; i <= r; ++i) {
                const int xx = std::clamp(x + i, 0, W - 1);
                const float* s = &rgb[(static_cast<std::size_t>(y) * w + xx) * 3];
                for (int c = 0; c < 3; ++c) a[c] += k[i + r] * s[c];
            }
            float* d = &tmp[(static_cast<std::size_t>(y) * w + x) * 3];
            for (int c = 0; c < 3; ++c) d[c] = a[c];
        }
    }
    });
    parallelRanges(h, static_cast<std::size_t>(w) * h * (2 * r + 1) * 3, [&](std::size_t ya, std::size_t yb) {
    for (int y = static_cast<int>(ya); y < static_cast<int>(yb); ++y) {
        for (int x = 0; x < W; ++x) {
            float a[3] = {0, 0, 0};
            for (int i = -r; i <= r; ++i) {
                const int yy = std::clamp(y + i, 0, H - 1);
                const float* s = &tmp[(static_cast<std::size_t>(yy) * w + x) * 3];
                for (int c = 0; c < 3; ++c) a[c] += k[i + r] * s[c];
            }
            float* d = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
            for (int c = 0; c < 3; ++c) d[c] = a[c];
        }
    }
    });
}

void sampleBackdrop(const ClearBackdrop& b, double bx, double by, double out[3]) {
    bx -= 0.5;
    by -= 0.5;
    const int x0 = static_cast<int>(std::floor(bx)), y0 = static_cast<int>(std::floor(by));
    const double fx = bx - x0, fy = by - y0;
    auto t = [&](int x, int y, int c) {
        x = std::clamp(x, 0, static_cast<int>(b.width) - 1);
        y = std::clamp(y, 0, static_cast<int>(b.height) - 1);
        return b.rgba[(static_cast<std::size_t>(y) * b.width + x) * 4 + c] / 255.0;
    };
    for (int c = 0; c < 3; ++c) {
        out[c] = (t(x0, y0, c) * (1 - fx) + t(x0 + 1, y0, c) * fx) * (1 - fy) +
                 (t(x0, y0 + 1, c) * (1 - fx) + t(x0 + 1, y0 + 1, c) * fx) * fy;
    }
}

}  // namespace

SimulatedGlass simulatedGlass(const ClearBackdrop& backdrop, double squareX, double squareY,
                              double squareSide, IconPlatform platform, bool dark,
                              std::uint32_t size) {
    SimulatedGlass out;
    if (backdrop.width == 0 || squareSide <= 0.0 || size == 0) return out;
    const Chiclet& chiclet = chicletFor(size, platform);
    const double gridPerCss = size / squareSide;          // px da grade por px CSS
    const double sideCss = chiclet.side / gridPerCss;     // S, em px CSS

    // Os dois desfoques como um so, em px da grade (ver o cabecalho, `[INF]`).
    const double sigmaCss = std::hypot(kRelativeBackdropBlurRadius * sideCss,
                                       kResultBlurRadius * backdrop.pixelsPerPoint);
    const double sigma = sigmaCss * gridPerCss;
    // Margem para o desfoque nao trazer a borda da grade para dentro.
    const std::uint32_t margin = static_cast<std::uint32_t>(std::ceil(3.0 * sigma)) + 2;
    const std::uint32_t W = size + 2 * margin;
    std::vector<float> rgb(static_cast<std::size_t>(W) * W * 3);
    parallelRanges(W, static_cast<std::size_t>(W) * W * 40, [&](std::size_t ya, std::size_t yb) {
    for (std::uint32_t y = static_cast<std::uint32_t>(ya); y < yb; ++y) {
        for (std::uint32_t x = 0; x < W; ++x) {
            double c[3];
            sampleBackdrop(backdrop, squareX + (x - static_cast<double>(margin) + 0.5) / gridPerCss,
                           squareY + (y - static_cast<double>(margin) + 0.5) / gridPerCss, c);
            float* d = &rgb[(static_cast<std::size_t>(y) * W + x) * 3];
            for (int k = 0; k < 3; ++k) d[k] = static_cast<float>(c[k]);
        }
    }
    });
    gaussianBlur(rgb, W, W, sigma);

    // O fundo desfocado como acumulador pre-multiplicado opaco, e a lente.
    std::vector<float> acc(static_cast<std::size_t>(size) * size * 4);
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float* s = &rgb[(static_cast<std::size_t>(y + margin) * W + x + margin) * 3];
            float* d = &acc[(static_cast<std::size_t>(y) * size + x) * 4];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = 1.0f;
        }
    }
    glassOver(acc, PixelGrid::full(size), chiclet.displacement, chiclet.refraction);

    // A matriz de cor e o recorte (a cobertura do campo).
    const MonoColourMatrices m = monoColourMatrices();
    const double* g = dark ? m.glassDark : m.glassLight;
    GlyphVCM vcm;
    vcm.lumaFloor = g[0];
    vcm.lumaCeiling = g[1];
    vcm.saturation = g[2];
    out.size = size;
    out.rgba.resize(acc.size());
    const std::size_t texels = static_cast<std::size_t>(size) * size;
    parallelRanges(texels, texels * 30, [&](std::size_t ia, std::size_t ib) {
    for (std::size_t i = ia; i < ib; ++i) {
        double c[3] = {acc[i * 4], acc[i * 4 + 1], acc[i * 4 + 2]};
        applyGlyphVCM(vcm, c);
        for (int k = 0; k < 3; ++k) out.rgba[i * 4 + k] = static_cast<float>(std::clamp(c[k], 0.0, 1.0));
        out.rgba[i * 4 + 3] = std::clamp(chiclet.field.rgba[i * 4 + 3], 0.0f, 1.0f);
    }
    });
    return out;
}

SimulatedGlassLens simulatedGlassLens(IconPlatform platform, std::uint32_t size) {
    SimulatedGlassLens out;
    if (size == 0) return out;
    const Chiclet& chiclet = chicletFor(size, platform);
    out.size = size;
    out.scalePixels = chiclet.refraction.scalePixels;
    out.side = chiclet.side;
    out.relativeBlur = kRelativeBackdropBlurRadius;
    out.absoluteBlur = kResultBlurRadius;
    out.variant = chiclet.refraction.variant;
    const std::size_t texels = static_cast<std::size_t>(size) * size;
    out.rgba.resize(texels * 4);
    for (std::size_t i = 0; i < texels; ++i) {
        out.rgba[i * 4 + 0] = chiclet.displacement.rgba[i * 4 + 0];
        out.rgba[i * 4 + 1] = chiclet.displacement.rgba[i * 4 + 1];
        out.rgba[i * 4 + 2] = std::clamp(chiclet.field.rgba[i * 4 + 3], 0.0f, 1.0f);
        out.rgba[i * 4 + 3] = chiclet.displacement.rgba[i * 4 + 3];
    }
    return out;
}

void backdropWithGlass(const ClearBackdrop& backdrop, const SimulatedGlass* glass, double squareX,
                       double squareY, double squareSide, double u, double v, double out[3],
                       double& glassCoverage, double raw[3]) {
    sampleBackdrop(backdrop, squareX + u * squareSide, squareY + v * squareSide, raw);
    for (int c = 0; c < 3; ++c) out[c] = raw[c];
    glassCoverage = 0.0;
    if (!glass || glass->size == 0) return;
    const double gx = u * glass->size - 0.5, gy = v * glass->size - 0.5;
    const int x0 = static_cast<int>(std::floor(gx)), y0 = static_cast<int>(std::floor(gy));
    const double fx = gx - x0, fy = gy - y0;
    const int n = static_cast<int>(glass->size);
    auto t = [&](int x, int y, int c) {
        x = std::clamp(x, 0, n - 1);
        y = std::clamp(y, 0, n - 1);
        return static_cast<double>(glass->rgba[(static_cast<std::size_t>(y) * n + x) * 4 + c]);
    };
    double g[4];
    for (int c = 0; c < 4; ++c) {
        g[c] = (t(x0, y0, c) * (1 - fx) + t(x0 + 1, y0, c) * fx) * (1 - fy) +
               (t(x0, y0 + 1, c) * (1 - fx) + t(x0 + 1, y0 + 1, c) * fx) * fy;
    }
    glassCoverage = std::clamp(g[3], 0.0, 1.0);
    for (int c = 0; c < 3; ++c) out[c] = raw[c] + (g[c] - raw[c]) * glassCoverage;
}

}  // namespace rb
