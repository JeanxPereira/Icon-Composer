#include "Source/RenderBox/GpuGlass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Source/RenderBox/BlurKernel.h"

namespace rb::gpu {
namespace {

// ---- os blocos de push constant, espelho dos `layout(push_constant)` -----------

struct FieldPush {
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t nx, ny;
    std::uint32_t itemsBase;
    std::uint32_t nseg;
    float gx0, gy0, cell;
    float aa;
    std::uint32_t rule;
};
static_assert(sizeof(FieldPush) == 52);

struct RingPush {
    std::uint32_t w, h;
    std::uint32_t R;
};
static_assert(sizeof(RingPush) == 12);

struct ShadowPush {
    double ringWidth;
    double brightness;
    double dx, dy;
    std::uint32_t w, h;
    std::uint32_t mode;
    std::uint32_t R;
    std::uint32_t colour;
    std::uint32_t premultiply;
    std::uint32_t blurred;
    float k;
};
static_assert(sizeof(ShadowPush) == 64);

struct BlurPush {
    std::uint32_t dw, dh;
    std::uint32_t sw, sh;
    std::uint32_t mode;
    std::int32_t halfWidth;
    std::int32_t factor;
};
static_assert(sizeof(BlurPush) == 28);

Result<Slab> resident(Resident& r, const void* data, std::size_t bytes) {
    auto s = r.acquire(std::max<std::size_t>(bytes, 16));
    if (!s) return s;
    if (bytes > 0) {
        if (auto ok = r.upload(*s, data, bytes); !ok) return std::unexpected(ok.error());
    }
    return s;
}

}  // namespace

// A GRADE DE SEGMENTOS DA GPU. A mesma ideia de `ExactSegments` (DistanceField.cpp)
// -- cada segmento em toda celula que atravessa, com folga de arredondamento para
// o lado de SOBRAR -- e duas diferencas, as duas so de busca:
//
//   - a grade cobre a uniao da caixa dos segmentos com o BUFFER, para que todo
//     centro de bloco caia numa celula e a transformada abaixo valha para ele;
//   - vem com `cellDist`, a distancia euclidiana exata (`edtSquared1d`) de cada
//     centro de celula ao centro da celula ocupada mais proxima. Ela limita `dP`
//     por baixo e por cima e o shader so visita a coroa entre os dois.
//
// Nenhum numero de pixel sai daqui: os candidatos sao um superconjunto dos da CPU
// e o pixel faz o minimo exato sobre eles (icon_field.comp diz por que o pe e o
// mesmo).
Result<ResidentField> fieldFromContours(Resident& r, const std::vector<FieldContour>& contours,
                                        std::uint32_t width, std::uint32_t height,
                                        const FieldOptions& options, std::uint32_t superSample) {
    ResidentField out;
    std::vector<char> inside;
    if (fieldInsideMask(contours, width, height, options, superSample, inside) == 0) return out;

    // A MESMA lista que `FieldShape` monta, na mesma ordem: o indice e o desempate.
    std::vector<float> segs;
    for (const FieldContour& c : contours) {
        const std::size_t n = c.xy.size() / 2;
        if (n < 2) continue;
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;
            segs.insert(segs.end(), {c.xy[i * 2], c.xy[i * 2 + 1], c.xy[j * 2], c.xy[j * 2 + 1]});
        }
    }
    const std::size_t nseg = segs.size() / 4;
    if (nseg == 0) return out;

    double x0 = static_cast<double>(options.originX), y0 = static_cast<double>(options.originY);
    double x1 = x0 + width, y1 = y0 + height;
    for (std::size_t k = 0; k < segs.size(); k += 2) {
        x0 = std::min(x0, static_cast<double>(segs[k]));
        x1 = std::max(x1, static_cast<double>(segs[k]));
        y0 = std::min(y0, static_cast<double>(segs[k + 1]));
        y1 = std::max(y1, static_cast<double>(segs[k + 1]));
    }
    const double extent = std::max(x1 - x0, y1 - y0);
    const double cell = std::max(8.0, extent / 512.0);
    const int nx = std::max(1, static_cast<int>(std::floor((x1 - x0) / cell)) + 1);
    const int ny = std::max(1, static_cast<int>(std::floor((y1 - y0) / cell)) + 1);
    const std::size_t cells = static_cast<std::size_t>(nx) * ny;

    // `ExactSegments::forCells`, com a grade acima.
    auto forCells = [&](std::size_t s, auto&& f) {
        const double ax = segs[s * 4], ay = segs[s * 4 + 1];
        const double bx = segs[s * 4 + 2], by = segs[s * 4 + 3];
        const double inv = 1.0 / cell;
        constexpr double eps = 1e-7;
        const double ylo = std::min(ay, by), yhi = std::max(ay, by);
        int j0 = static_cast<int>(std::floor((ylo - y0) * inv - eps));
        int j1 = static_cast<int>(std::floor((yhi - y0) * inv + eps));
        j0 = std::max(j0, 0);
        j1 = std::min(j1, ny - 1);
        for (int j = j0; j <= j1; ++j) {
            double xlo, xhi;
            if (ay == by) {
                xlo = std::min(ax, bx);
                xhi = std::max(ax, bx);
            } else {
                const double b0 = std::max(ylo, y0 + j * cell);
                const double b1 = std::min(yhi, y0 + (j + 1) * cell);
                double t0 = (b0 - ay) / (by - ay);
                double t1 = (b1 - ay) / (by - ay);
                t0 = std::clamp(t0, 0.0, 1.0);
                t1 = std::clamp(t1, 0.0, 1.0);
                const double xa = ax + t0 * (bx - ax);
                const double xb = ax + t1 * (bx - ax);
                xlo = std::min(xa, xb);
                xhi = std::max(xa, xb);
            }
            int i0 = static_cast<int>(std::floor((xlo - x0) * inv - eps));
            int i1 = static_cast<int>(std::floor((xhi - x0) * inv + eps));
            i0 = std::max(i0, 0);
            i1 = std::min(i1, nx - 1);
            for (int i = i0; i <= i1; ++i) f(static_cast<std::size_t>(j) * nx + i);
        }
    };
    std::vector<std::uint32_t> grid(cells + 1, 0);
    for (std::size_t s = 0; s < nseg; ++s) forCells(s, [&](std::size_t c) { ++grid[c + 1]; });
    for (std::size_t c = 0; c < cells; ++c) grid[c + 1] += grid[c];
    const std::size_t itemsBase = grid.size();
    grid.resize(itemsBase + grid[cells]);
    {
        std::vector<std::uint32_t> fill(grid.begin(), grid.begin() + static_cast<std::ptrdiff_t>(cells));
        for (std::size_t s = 0; s < nseg; ++s) {
            forCells(s, [&](std::size_t c) {
                grid[itemsBase + fill[c]++] = static_cast<std::uint32_t>(s);
            });
        }
    }

    // A distancia de cada centro de celula ao centro ocupado mais proximo.
    std::vector<double> sq(cells);
    for (std::size_t c = 0; c < cells; ++c) sq[c] = grid[c + 1] > grid[c] ? 0.0 : 1e20;
    edtSquaredColumns(sq.data(), nx, ny, nullptr);
    {
        std::vector<double> f(static_cast<std::size_t>(nx)), d(static_cast<std::size_t>(nx));
        std::vector<int> v(static_cast<std::size_t>(nx) + 1);
        std::vector<double> z(static_cast<std::size_t>(nx) + 2);
        for (int j = 0; j < ny; ++j) {
            double* row = &sq[static_cast<std::size_t>(j) * nx];
            std::copy(row, row + nx, f.begin());
            edtSquared1d(f, d, v, z, nx);
            std::copy(d.begin(), d.end(), row);
        }
    }
    std::vector<float> cellDist(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        cellDist[c] = static_cast<float>(std::sqrt(sq[c]) * cell);
    }

    const std::size_t texels = static_cast<std::size_t>(width) * height;
    std::vector<std::uint32_t> packed((texels + 3) / 4, 0);
    std::memcpy(packed.data(), inside.data(), texels);

    auto segBuf = resident(r, segs.data(), segs.size() * sizeof(float));
    if (!segBuf) return std::unexpected(segBuf.error());
    auto gridBuf = resident(r, grid.data(), grid.size() * sizeof(std::uint32_t));
    if (!gridBuf) return std::unexpected(gridBuf.error());
    auto distBuf = resident(r, cellDist.data(), cellDist.size() * sizeof(float));
    if (!distBuf) return std::unexpected(distBuf.error());
    auto insideBuf = r.stage(packed.data(), packed.size() * sizeof(std::uint32_t));
    if (!insideBuf) return std::unexpected(insideBuf.error());
    auto field = r.acquire(static_cast<VkDeviceSize>(texels) * 16);
    if (!field) return std::unexpected(field.error());

    FieldPush p{};
    p.w = width;
    p.h = height;
    p.ox = options.originX;
    p.oy = options.originY;
    p.nx = static_cast<std::uint32_t>(nx);
    p.ny = static_cast<std::uint32_t>(ny);
    p.itemsBase = static_cast<std::uint32_t>(itemsBase);
    p.nseg = static_cast<std::uint32_t>(nseg);
    p.gx0 = static_cast<float>(x0);
    p.gy0 = static_cast<float>(y0);
    p.cell = static_cast<float>(cell);
    p.aa = options.aaWidth;
    p.rule = static_cast<std::uint32_t>(options.rule);
    if (auto ok = r.dispatch(r.field, {whole(*field), whole(*segBuf), whole(*gridBuf),
                                       whole(*distBuf), *insideBuf},
                             &p, (width + 7) / 8, (height + 7) / 8);
        !ok) {
        return std::unexpected(ok.error());
    }
    out.data = *field;
    out.width = width;
    out.height = height;
    out.originX = options.originX;
    out.originY = options.originY;
    return out;
}

// `blurLadder` sobre `img` (pre-multiplicado, W x H), no lugar: as reducoes na
// ordem do plano, as passadas no fundo, as expansoes de volta.
Result<void> blurLadderOn(Resident& r, const Slab& img, std::uint32_t width, std::uint32_t height,
                          double variance) {
    const BlurLadderPlan plan = blurLadderPlan(width, height, variance);
    if (plan.levels.empty() || plan.passes == 0) return {};
    std::vector<Slab> levels{img};
    for (std::size_t i = 1; i < plan.levels.size(); ++i) {
        const BlurLadderLevel& L = plan.levels[i];
        const BlurLadderLevel& up = plan.levels[i - 1];
        auto s = r.acquire(static_cast<VkDeviceSize>(L.width) * L.height * 16);
        if (!s) return std::unexpected(s.error());
        BlurPush p{L.width, L.height, up.width, up.height, 2u, 0, L.factor};
        if (auto ok = r.dispatch(r.blur, {whole(*s), whole(levels.back()), whole(r.dummy())}, &p,
                                 groups16(L.width), groups16(L.height));
            !ok) {
            return ok;
        }
        levels.push_back(*s);
    }
    const BlurLadderLevel& bottom = plan.levels.back();
    // `blurPass` sai sem tocar em nada quando o kernel e vazio.
    const std::vector<double> kernel = blurKernel(plan.sigmaPerPass);
    if (!kernel.empty()) {
        auto k = resident(r, kernel.data(), kernel.size() * sizeof(double));
        if (!k) return std::unexpected(k.error());
        auto tmp = r.acquire(static_cast<VkDeviceSize>(bottom.width) * bottom.height * 16);
        if (!tmp) return std::unexpected(tmp.error());
        const std::int32_t hw = static_cast<std::int32_t>(kernel.size() / 2);
        for (int i = 0; i < plan.passes; ++i) {
            BlurPush h{bottom.width, bottom.height, bottom.width, bottom.height, 0u, hw, 1};
            if (auto ok = r.dispatch(r.blur, {whole(*tmp), whole(levels.back()), whole(*k)}, &h,
                                     groups16(bottom.width), groups16(bottom.height));
                !ok) {
                return ok;
            }
            BlurPush v{bottom.width, bottom.height, bottom.width, bottom.height, 1u, hw, 1};
            if (auto ok = r.dispatch(r.blur, {whole(levels.back()), whole(*tmp), whole(*k)}, &v,
                                     groups16(bottom.width), groups16(bottom.height));
                !ok) {
                return ok;
            }
        }
    }
    for (std::size_t i = plan.levels.size() - 1; i >= 1; --i) {
        const BlurLadderLevel& L = plan.levels[i];
        const BlurLadderLevel& up = plan.levels[i - 1];
        BlurPush p{up.width, up.height, L.width, L.height, 3u, 0, L.factor};
        if (auto ok = r.dispatch(r.blur, {whole(levels[i - 1]), whole(levels[i]), whole(r.dummy())},
                                 &p, groups16(up.width), groups16(up.height));
            !ok) {
            return ok;
        }
    }
    return {};
}

Result<ResidentShadow> shadow(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry, double overdrawAlpha) {
    ResidentShadow out;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 16;
    const std::uint32_t gx = groups16(width), gy = groups16(height);

    // O anel: so com largura positiva (`!(ringWidth > 0)` e a mascara identidade).
    std::uint32_t R = 0;
    Slab column = r.dummy();
    if (geometry.ringWidth && *geometry.ringWidth > 0.0) {
        R = static_cast<std::uint32_t>(std::ceil(*geometry.ringWidth + 0.5)) + 1u;
        auto c = r.acquire(static_cast<VkDeviceSize>(width) * height * 4);
        if (!c) return std::unexpected(c.error());
        column = *c;
        RingPush rp{width, height, R};
        if (auto ok = r.dispatch(r.ring, {whole(column), whole(art)}, &rp, gx, gy); !ok) {
            return std::unexpected(ok.error());
        }
    }

    // `gaussian` -> `blurPremultipliedRgbaInPlace`, que nao faz NADA (nem a
    // pre-multiplicacao) quando o kernel sai vazio.
    const double sigma = geometry.blurRadius * kShadowBlurSigmaPerRadius;
    const bool blurred = sigma > 0.0 && width > 0 && height > 0 && blurKernelHalfWidth(sigma) > 0;

    auto img = r.acquire(bytes);
    if (!img) return std::unexpected(img.error());
    ShadowPush p{};
    p.ringWidth = geometry.ringWidth ? *geometry.ringWidth : 0.0;
    p.brightness = kShadow.vibrantBrightness;
    p.w = width;
    p.h = height;
    p.mode = 0;
    p.R = R;
    p.colour = !shadowUsesVibrantTable(style) ? 0u : (kShadow.vibrantBrightness != 1.0 ? 1u : 2u);
    p.premultiply = blurred ? 1u : 0u;
    if (auto ok = r.dispatch(r.shadow, {whole(*img), whole(art), whole(r.dummy()), whole(column)},
                             &p, gx, gy);
        !ok) {
        return std::unexpected(ok.error());
    }
    if (blurred) {
        if (auto ok = blurLadderOn(r, *img, width, height, sigma * sigma); !ok) {
            return std::unexpected(ok.error());
        }
    }

    auto made = r.acquire(bytes);
    if (!made) return std::unexpected(made.error());
    p.mode = 1;
    p.dx = geometry.offsetX;
    p.dy = geometry.offsetY;
    p.blurred = blurred ? 1u : 0u;
    if (auto ok = r.dispatch(r.shadow, {whole(*made), whole(*img), whole(r.dummy()),
                                        whole(r.dummy())},
                             &p, gx, gy);
        !ok) {
        return std::unexpected(ok.error());
    }
    out.image = *made;

    // `shadowOverdrawImage` devolve vazio quando `!(clipAlpha > 0)`.
    if (overdrawAlpha > 0.0) {
        auto od = r.acquire(bytes);
        if (!od) return std::unexpected(od.error());
        p.mode = 2;
        p.k = static_cast<float>(overdrawAlpha);
        if (auto ok = r.dispatch(r.shadow, {whole(*od), whole(*made), whole(art), whole(r.dummy())},
                                 &p, gx, gy);
            !ok) {
            return std::unexpected(ok.error());
        }
        out.overdraw = *od;
    }
    return out;
}

}  // namespace rb::gpu
