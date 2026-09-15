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

std::vector<float> blurPremultipliedRgba(const std::vector<float>& src, std::uint32_t width,
                                         std::uint32_t height, double sigma) {
    if (sigma <= 0.0 || width == 0 || height == 0) return src;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (src.size() < texels * 4) return src;

    const std::vector<double> kernel = blurKernel(sigma);
    if (kernel.empty()) return src;
    const int halfWidth = static_cast<int>(kernel.size() / 2);

    std::vector<float> pre(texels * 4);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = src[t * 4 + 3];
        for (int c = 0; c < 3; ++c) pre[t * 4 + c] = src[t * 4 + c] * a;
        pre[t * 4 + 3] = a;
    }

    std::vector<float> tmp(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -halfWidth; i <= halfWidth; ++i) {
                const int sx = std::clamp(static_cast<int>(x) + i, 0, static_cast<int>(width) - 1);
                const double k = kernel[static_cast<std::size_t>(i + halfWidth)];
                const float* p = &pre[(static_cast<std::size_t>(y) * width + sx) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &tmp[(static_cast<std::size_t>(y) * width + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c]);
        }
    }

    std::vector<float> out(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -halfWidth; i <= halfWidth; ++i) {
                const int sy = std::clamp(static_cast<int>(y) + i, 0, static_cast<int>(height) - 1);
                const double k = kernel[static_cast<std::size_t>(i + halfWidth)];
                const float* p = &tmp[(static_cast<std::size_t>(sy) * width + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &out[(static_cast<std::size_t>(y) * width + x) * 4];
            const double a = acc[3];
            o[3] = static_cast<float>(a);
            for (int c = 0; c < 3; ++c) o[c] = a > 0.0 ? static_cast<float>(acc[c] / a) : 0.0f;
        }
    }
    return out;
}

}  // namespace rb
