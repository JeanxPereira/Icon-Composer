#include "Source/RenderBox/BlurKernel.h"

#include <algorithm>
#include <cmath>

namespace rb {

const char* const kBlurMaterialSurfaceNote =
    "blur-material: o raio esta medido (min(b,1) x blurStrengthMax=64, 0x4A948) e o KERNEL que "
    "o addBlurFilterWithRadius: constroi a partir dele tambem esta (sigma = raio, "
    "BlurKernel.h) -- o que falta e a SUPERFICIE. O alvo envolve o filtro numa camada com "
    "needs-background (flag 1, nomeada em 0xE9F48) e a recorta com um retangulo construido em "
    "0x4A4A4-0x4A56C a partir do frame da propria camada, afastado por -[self+0x46A8]; nem o "
    "ramo que roda, nem esse valor, nem o frame estao lidos. Desenhar um desfoque de fundo "
    "sobre uma extensao inventada seria pior do que nao desenhar, entao o grupo sai sem ele.";

int blurKernelHalfWidth(double sigma) {
    if (!(sigma > 0.0)) return 0;
    // `0xC3634`-`0xC3654`: multiply, `frintp` (ceil), then the ceiling at
    // `0x44800000` == 1024.0 before the conversion to an unsigned int. The cap
    // is transcribed rather than assumed unreachable: a 1024-pixel canvas with a
    // blur radius past ~366 would reach it.
    const double taps = std::ceil(sigma * 2.7999999523162842);
    return static_cast<int>(std::fmin(taps, 1024.0));
}

std::vector<double> blurKernel(double sigma) {
    const int halfWidth = blurKernelHalfWidth(sigma);
    if (halfWidth <= 0) return {};

    std::vector<double> kernel(static_cast<std::size_t>(halfWidth) * 2 + 1);
    double sum = 0.0;
    for (int i = -halfWidth; i <= halfWidth; ++i) {
        const double v = std::exp(-(static_cast<double>(i) * i) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(i + halfWidth)] = v;
        sum += v;
    }
    // The binary accumulates the off-centre half and normalises by `2*sum + 1`
    // (`0xC36D8`); summing the whole mirrored row is the same number and is what
    // this repository's other two Gaussians already write.
    for (double& k : kernel) k /= sum;
    return kernel;
}

// `[BIN]` The default rung of the quality ladder, `0xFED3C`-`0xFED50`: the `else`
// of the three-way `fcsel` on `(flags >> 4) & 3`.
namespace {

constexpr double kBlurSigmaMax = 5.25;
// `[BIN]` The float at `0x1619D0`, added before the `fcvtps` at `0xFED74`.
constexpr double kBlurPassEpsilon = 0.001;
// `[BIN]` `0xBEF20000` at `0xFFA24` = `-0.47265625` = `-0.6875^2`.
constexpr double kBlurReduce4Variance = 0.47265625;
// `[BIN]` `0xBF440000` at `0xFFB80` = `-0.765625` = `-0.875^2`. NOT the `2.56`
// the laudo quoted -- that one is the top quality rung's branch at `0xFFB08`.
constexpr double kBlurReduce2Variance = 0.765625;
// `[INF]` The floor on a reduced side. The target's is not read; without one a
// 32-pixel thumbnail with a wide radius would be blurred on a 2x2 grid, where
// the bilinear expand has nothing left to reconstruct.
constexpr std::uint32_t kBlurMinReducedSide = 8;

// One separable pass over PREMULTIPLIED RGBA, in place, with clamped edges.
void blurPass(std::vector<float>& img, std::uint32_t width, std::uint32_t height, double sigma) {
    const std::vector<double> kernel = blurKernel(sigma);
    if (kernel.empty()) return;
    const int halfWidth = static_cast<int>(kernel.size() / 2);
    const std::size_t texels = static_cast<std::size_t>(width) * height;

    std::vector<float> tmp(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -halfWidth; i <= halfWidth; ++i) {
                const int sx = std::clamp(static_cast<int>(x) + i, 0, static_cast<int>(width) - 1);
                const double k = kernel[static_cast<std::size_t>(i + halfWidth)];
                const float* p = &img[(static_cast<std::size_t>(y) * width + sx) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &tmp[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c]);
        }
    }

    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -halfWidth; i <= halfWidth; ++i) {
                const int sy = std::clamp(static_cast<int>(y) + i, 0, static_cast<int>(height) - 1);
                const double k = kernel[static_cast<std::size_t>(i + halfWidth)];
                const float* p = &tmp[(static_cast<std::size_t>(sy) * width + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &img[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c]);
        }
    }
}

// `[BIN]` The reduced size is `(d + factor - 1) / factor` -- `0xFEE04`-`0xFEE08`
// for the 4x (`add #3`, `sshr #2`) and `0xFEE3C`-`0xFEE50` for the 2x
// (`add #1`, `asr #1`). `[INF]` The filter is a box average of the block; the
// target's is not read.
std::vector<float> reduceBox(const std::vector<float>& img, std::uint32_t width,
                             std::uint32_t height, int factor, std::uint32_t& outWidth,
                             std::uint32_t& outHeight) {
    const std::uint32_t f = static_cast<std::uint32_t>(factor);
    outWidth = (width + f - 1) / f;
    outHeight = (height + f - 1) / f;
    std::vector<float> out(static_cast<std::size_t>(outWidth) * outHeight * 4, 0.0f);
    const double norm = 1.0 / (static_cast<double>(factor) * factor);
    for (std::uint32_t ry = 0; ry < outHeight; ++ry) {
        for (std::uint32_t rx = 0; rx < outWidth; ++rx) {
            double acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < factor; ++j) {
                const int sy = std::clamp(static_cast<int>(ry * f) + j, 0,
                                          static_cast<int>(height) - 1);
                for (int i = 0; i < factor; ++i) {
                    const int sx = std::clamp(static_cast<int>(rx * f) + i, 0,
                                              static_cast<int>(width) - 1);
                    const float* p = &img[(static_cast<std::size_t>(sy) * width + sx) * 4];
                    for (int c = 0; c < 4; ++c) acc[c] += p[c];
                }
            }
            float* o = &out[(static_cast<std::size_t>(ry) * outWidth + rx) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c] * norm);
        }
    }
    return out;
}

// `[INF]` Bilinear, with the reduced texel `r` understood to sit at source
// coordinate `r * factor + (factor - 1) / 2` -- the centre of the block
// `reduceBox` averaged, so down and up agree about where a sample IS. Edges
// clamp, for the reason the blur's edges clamp.
std::vector<float> expandBilinear(const std::vector<float>& img, std::uint32_t width,
                                  std::uint32_t height, std::uint32_t outWidth,
                                  std::uint32_t outHeight, int factor) {
    std::vector<float> out(static_cast<std::size_t>(outWidth) * outHeight * 4, 0.0f);
    const double f = factor;
    const double centre = (f - 1.0) * 0.5;
    const int lastX = static_cast<int>(width) - 1;
    const int lastY = static_cast<int>(height) - 1;
    for (std::uint32_t y = 0; y < outHeight; ++y) {
        const double v = (static_cast<double>(y) - centre) / f;
        const double fy = std::floor(v);
        const double ty = v - fy;
        const int y0 = std::clamp(static_cast<int>(fy), 0, lastY);
        const int y1 = std::clamp(static_cast<int>(fy) + 1, 0, lastY);
        for (std::uint32_t x = 0; x < outWidth; ++x) {
            const double u = (static_cast<double>(x) - centre) / f;
            const double fx = std::floor(u);
            const double tx = u - fx;
            const int x0 = std::clamp(static_cast<int>(fx), 0, lastX);
            const int x1 = std::clamp(static_cast<int>(fx) + 1, 0, lastX);
            const float* p00 = &img[(static_cast<std::size_t>(y0) * width + x0) * 4];
            const float* p10 = &img[(static_cast<std::size_t>(y0) * width + x1) * 4];
            const float* p01 = &img[(static_cast<std::size_t>(y1) * width + x0) * 4];
            const float* p11 = &img[(static_cast<std::size_t>(y1) * width + x1) * 4];
            float* o = &out[(static_cast<std::size_t>(y) * outWidth + x) * 4];
            for (int c = 0; c < 4; ++c) {
                const double top = p00[c] + (p10[c] - p00[c]) * tx;
                const double bottom = p01[c] + (p11[c] - p01[c]) * tx;
                o[c] = static_cast<float>(top + (bottom - top) * ty);
            }
        }
    }
    return out;
}

// The ladder itself, over PREMULTIPLIED RGBA, in place. `variance` is in the
// texels of `img`, which is what makes the recursion legal: every level converts
// the number into its own grid and subtracts what its own resampling added.
void blurLadder(std::vector<float>& img, std::uint32_t width, std::uint32_t height,
                double variance) {
    if (!(variance > 0.0) || width == 0 || height == 0) return;

    const int factor = blurReduceFactorForVariance(variance);
    if (factor > 1) {
        const std::uint32_t f = static_cast<std::uint32_t>(factor);
        const double residual =
            variance / (f * f) - (factor == 4 ? kBlurReduce4Variance : kBlurReduce2Variance);
        if ((width + f - 1) / f >= kBlurMinReducedSide &&
            (height + f - 1) / f >= kBlurMinReducedSide && residual > 0.0) {
            std::uint32_t rw = 0, rh = 0;
            std::vector<float> small = reduceBox(img, width, height, factor, rw, rh);
            blurLadder(small, rw, rh, residual);
            img = expandBilinear(small, rw, rh, width, height, factor);
            return;
        }
    }

    // `[BIN]` `clamp(nRaw, 1, 32)` (`0xFED78`-`0xFED8C`), each pass carrying
    // `variance / passes` (`0xFFEF0`). Gaussian variances add, so the composition
    // is exact.
    const int passes = std::clamp(blurPassCountForVariance(variance), 1, 32);
    const double sigmaPerPass = std::sqrt(variance / passes);
    for (int i = 0; i < passes; ++i) blurPass(img, width, height, sigmaPerPass);
}

}  // namespace

int blurPassCountForVariance(double variance) {
    if (!(variance > 0.0)) return 0;
    const double sigmaMaxSquared = kBlurSigmaMax * kBlurSigmaMax;
    return static_cast<int>(std::ceil(variance / sigmaMaxSquared - kBlurPassEpsilon));
}

int blurReduceFactorForVariance(double variance) {
    const int n = blurPassCountForVariance(variance);
    if (n >= 7) return 4;
    if (n >= 3) return 2;
    return 1;
}

std::vector<float> blurPremultipliedRgba(const std::vector<float>& src, std::uint32_t width,
                                         std::uint32_t height, double sigma) {
    if (sigma <= 0.0 || width == 0 || height == 0) return src;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (src.size() < texels * 4) return src;
    if (blurKernelHalfWidth(sigma) <= 0) return src;

    std::vector<float> img(texels * 4);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = src[t * 4 + 3];
        for (int c = 0; c < 3; ++c) img[t * 4 + c] = src[t * 4 + c] * a;
        img[t * 4 + 3] = a;
    }

    blurLadder(img, width, height, sigma * sigma);

    std::vector<float> out(texels * 4, 0.0f);
    for (std::size_t t = 0; t < texels; ++t) {
        const double a = img[t * 4 + 3];
        out[t * 4 + 3] = static_cast<float>(a);
        for (int c = 0; c < 3; ++c) {
            out[t * 4 + c] = a > 0.0 ? static_cast<float>(img[t * 4 + c] / a) : 0.0f;
        }
    }
    return out;
}

}  // namespace rb
