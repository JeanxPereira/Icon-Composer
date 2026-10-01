#include "Source/RenderBox/GpuGlass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Source/RenderBox/BlurKernel.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/RenderingParameters.h"

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
    float far;
    std::uint32_t nx2, ny2;
    float cell2;
    std::uint32_t start2, items2, dist2;
    std::int32_t cx, cy;
    std::uint32_t cw, ch;
    std::int32_t nx0, ny0, nx1, ny1;
    float nearFar;
};
static_assert(sizeof(FieldPush) == 116);

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
    std::uint32_t ow, oh;
    std::int32_t ox, oy;
};
static_assert(sizeof(ShadowPush) == 80);

struct BlurPush {
    std::uint32_t dw, dh;
    std::uint32_t sw, sh;
    std::uint32_t mode;
    std::int32_t halfWidth;
    std::int32_t factor;
};
static_assert(sizeof(BlurPush) == 28);

struct GlassMaskPush {
    std::uint32_t w, h;
    std::int32_t originY;
    std::uint32_t slot;
    float borderWidth;
    float ob0, ob1;
    float cb0, cb1;
    float boundsY, boundsH;
    float alphaFloor;
    std::uint32_t kind;       // `OpacityMaskKind`: 1 e o gradiente da geracao 27
    std::uint32_t segments;   // os segmentos da rampa dele
};
static_assert(sizeof(GlassMaskPush) == 56);

struct DisplacePush {
    float params[4];
    float rot[2];
    std::uint32_t w, h;
    std::uint32_t fw;
    std::int32_t fx, fy;
};
static_assert(sizeof(DisplacePush) == 44);

struct RefractPush {
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t size;
    std::uint32_t variant;
    float scale;
    float inv;
};
static_assert(sizeof(RefractPush) == 32);

struct GlowPush {
    double radius;
    double biasAmount;
    double alpha;
    double maxDistance;
    double edgeWidth;
    double edgeWidthFlat;
    double clip[4];
    std::uint32_t w, h;
    std::int32_t ox, oy;
    std::uint32_t fw;
    std::int32_t fx, fy;
};
static_assert(sizeof(GlowPush) == 112);

struct HighlightPush {
    std::uint32_t w, h;
    std::uint32_t nslots;
    std::uint32_t chiclet;
    std::uint32_t useVCM;
    std::uint32_t clampPlus;
    std::uint32_t slot;
    std::uint32_t fw;
    std::int32_t fx, fy;
};
static_assert(sizeof(HighlightPush) == 40);

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
                                        const FieldOptions& options, std::uint32_t superSample,
                                        float farBand, const FieldClamp* clamp,
                                        const FieldNear* nearRect) {
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

    // QUEM ENTRA NA GRADE. Sem banda, todo segmento. Com a banda longe, so os que
    // chegam a `farBand + 4` do campo: um segmento de fora fica a mais que isso de
    // todo pixel, entao um pixel ate a banda tem o pe dentro da grade (o mesmo pe
    // e o mesmo indice -- a lista `segs` continua inteira), e um alem dela sai alem
    // dela de qualquer jeito. No zoom profundo a pastilha tem ~12 800 segmentos e
    // um ladrilho do miolo nao chega a nenhum.
    double x0 = static_cast<double>(options.originX), y0 = static_cast<double>(options.originY);
    double x1 = x0 + width, y1 = y0 + height;
    std::vector<std::uint32_t> gridSegs;
    gridSegs.reserve(nseg);
    {
        // Com um retangulo exato, todo segmento; senao a maior das bandas.
        const bool exact = farBand <= 0.0f || (nearRect && nearRect->band <= 0.0f);
        const double reach =
            static_cast<double>(std::max(farBand, nearRect ? nearRect->band : 0.0f)) + 4.0;
        const double rx0 = x0 - reach, ry0 = y0 - reach, rx1 = x1 + reach, ry1 = y1 + reach;
        for (std::size_t s = 0; s < nseg; ++s) {
            const double ax = segs[s * 4], ay = segs[s * 4 + 1];
            const double bx = segs[s * 4 + 2], by = segs[s * 4 + 3];
            if (!exact && (std::max(ax, bx) < rx0 || std::min(ax, bx) > rx1 ||
                                   std::max(ay, by) < ry0 || std::min(ay, by) > ry1)) {
                continue;
            }
            gridSegs.push_back(static_cast<std::uint32_t>(s));
        }
    }
    for (std::uint32_t s : gridSegs) {
        for (int e = 0; e < 2; ++e) {
            const double px = segs[s * 4 + e * 2], py = segs[s * 4 + e * 2 + 1];
            x0 = std::min(x0, px);
            x1 = std::max(x1, px);
            y0 = std::min(y0, py);
            y1 = std::max(y1, py);
        }
    }
    const double extent = std::max(x1 - x0, y1 - y0);

    // Uma grade de segmentos (partidas relativas a `items`, e os itens) e a
    // distancia de cada centro de celula ao centro ocupado mais proximo.
    struct Level {
        double cell = 0.0;
        int nx = 0, ny = 0;
        std::vector<std::uint32_t> starts;   // cells + 1
        std::vector<std::uint32_t> items;
        std::vector<float> dist;
    };
    auto build = [&](double cell) {
        Level L;
        L.cell = cell;
        L.nx = std::max(1, static_cast<int>(std::floor((x1 - x0) / cell)) + 1);
        L.ny = std::max(1, static_cast<int>(std::floor((y1 - y0) / cell)) + 1);
        const int nx = L.nx, ny = L.ny;
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
        L.starts.assign(cells + 1, 0);
        for (const std::uint32_t s : gridSegs) forCells(s, [&](std::size_t c) { ++L.starts[c + 1]; });
        for (std::size_t c = 0; c < cells; ++c) L.starts[c + 1] += L.starts[c];
        L.items.resize(L.starts[cells]);
        {
            std::vector<std::uint32_t> fill(L.starts.begin(),
                                            L.starts.begin() + static_cast<std::ptrdiff_t>(cells));
            for (const std::uint32_t s : gridSegs) {
                forCells(s, [&](std::size_t c) { L.items[fill[c]++] = static_cast<std::uint32_t>(s); });
            }
        }

        std::vector<double> sq(cells);
        for (std::size_t c = 0; c < cells; ++c) sq[c] = L.starts[c + 1] > L.starts[c] ? 0.0 : 1e20;
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
        L.dist.resize(cells);
        for (std::size_t c = 0; c < cells; ++c) L.dist[c] = static_cast<float>(std::sqrt(sq[c]) * cell);
        return L;
    };
    // A fina, e a grossa (ver icon_field.comp): oito celulas finas por lado, ou
    // no maximo ~64 celulas no lado maior.
    const Level fine = build(std::max(8.0, extent / 512.0));
    const Level coarse = build(std::max(8.0 * fine.cell, extent / 64.0));
    const double cell = fine.cell;
    const int nx = fine.nx, ny = fine.ny;

    // Numa lista so: [partidas finas][itens finos][partidas grossas][itens grossos].
    std::vector<std::uint32_t> grid;
    grid.reserve(fine.starts.size() + fine.items.size() + coarse.starts.size() + coarse.items.size());
    grid.insert(grid.end(), fine.starts.begin(), fine.starts.end());
    const std::size_t itemsBase = grid.size();
    grid.insert(grid.end(), fine.items.begin(), fine.items.end());
    const std::size_t start2 = grid.size();
    grid.insert(grid.end(), coarse.starts.begin(), coarse.starts.end());
    const std::size_t items2 = grid.size();
    grid.insert(grid.end(), coarse.items.begin(), coarse.items.end());
    std::vector<float> cellDist(fine.dist);
    const std::size_t dist2 = cellDist.size();
    cellDist.insert(cellDist.end(), coarse.dist.begin(), coarse.dist.end());

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
    p.far = farBand;
    p.nx2 = static_cast<std::uint32_t>(coarse.nx);
    p.ny2 = static_cast<std::uint32_t>(coarse.ny);
    p.cell2 = static_cast<float>(coarse.cell);
    p.start2 = static_cast<std::uint32_t>(start2);
    p.items2 = static_cast<std::uint32_t>(items2);
    p.dist2 = static_cast<std::uint32_t>(dist2);
    p.cx = clamp ? clamp->x : 0;
    p.cy = clamp ? clamp->y : 0;
    p.cw = clamp ? clamp->w : width;
    p.ch = clamp ? clamp->h : height;
    if (nearRect) {
        p.nx0 = nearRect->x0;
        p.ny0 = nearRect->y0;
        p.nx1 = nearRect->x1;
        p.ny1 = nearRect->y1;
        p.nearFar = nearRect->band;
    }
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

Result<ShadowBlur> shadowBlur(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry,
                              const ShadowParameters& parameters) {
    ShadowBlur out;
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
    out.blurred = sigma > 0.0 && width > 0 && height > 0 && blurKernelHalfWidth(sigma) > 0;

    auto img = r.acquire(bytes);
    if (!img) return std::unexpected(img.error());
    ShadowPush p{};
    p.ringWidth = geometry.ringWidth ? *geometry.ringWidth : 0.0;
    p.brightness = parameters.vibrantBrightness;
    p.w = width;
    p.h = height;
    p.mode = 0;
    p.R = R;
    p.colour =
        !shadowUsesVibrantTable(style) ? 0u : (parameters.vibrantBrightness != 1.0 ? 1u : 2u);
    p.premultiply = out.blurred ? 1u : 0u;
    p.ow = width;
    p.oh = height;
    if (auto ok = r.dispatch(r.shadow, {whole(*img), whole(art), whole(r.dummy()), whole(column)},
                             &p, gx, gy);
        !ok) {
        return std::unexpected(ok.error());
    }
    if (out.blurred) {
        if (auto ok = blurLadderOn(r, *img, width, height, sigma * sigma); !ok) {
            return std::unexpected(ok.error());
        }
    }
    out.image = *img;
    return out;
}

Result<ResidentShadow> shadowPlace(Resident& r, const ShadowBlur& blur, const Slab& art,
                                   std::uint32_t width, std::uint32_t height,
                                   const ShadowGeometry& geometry, double overdrawAlpha,
                                   const ShadowOutput& to) {
    ResidentShadow out;
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(to.width) * to.height * 16;
    const std::uint32_t gx = groups16(to.width), gy = groups16(to.height);
    ShadowPush p{};
    p.w = width;
    p.h = height;
    p.ow = to.width;
    p.oh = to.height;
    p.ox = to.x;
    p.oy = to.y;

    auto made = r.acquire(bytes);
    if (!made) return std::unexpected(made.error());
    p.mode = 1;
    p.dx = geometry.offsetX;
    p.dy = geometry.offsetY;
    p.blurred = blur.blurred ? 1u : 0u;
    if (auto ok = r.dispatch(r.shadow, {whole(*made), whole(blur.image), whole(r.dummy()),
                                        whole(r.dummy())},
                             &p, gx, gy);
        !ok) {
        return std::unexpected(ok.error());
    }
    out.image = *made;

    auto od = shadowOverdraw(r, *made, art, width, height, overdrawAlpha, to);
    if (!od) return std::unexpected(od.error());
    out.overdraw = *od;
    return out;
}

Result<Slab> shadowOverdraw(Resident& r, const Slab& placed, const Slab& content,
                            std::uint32_t width, std::uint32_t height, double overdrawAlpha,
                            const ShadowOutput& to) {
    // `shadowOverdrawImage` devolve vazio quando `!(clipAlpha > 0)`.
    if (!(overdrawAlpha > 0.0)) return Slab{};
    ShadowPush p{};
    p.w = width;
    p.h = height;
    p.ow = to.width;
    p.oh = to.height;
    p.ox = to.x;
    p.oy = to.y;
    p.mode = 2;
    p.k = static_cast<float>(overdrawAlpha);
    auto od = r.acquire(static_cast<VkDeviceSize>(to.width) * to.height * 16);
    if (!od) return std::unexpected(od.error());
    if (auto ok = r.dispatch(r.shadow, {whole(*od), whole(placed), whole(content), whole(r.dummy())},
                             &p, groups16(to.width), groups16(to.height));
        !ok) {
        return std::unexpected(ok.error());
    }
    return *od;
}

Result<ResidentShadow> shadow(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry, double overdrawAlpha,
                              const ShadowParameters& parameters) {
    auto blur = shadowBlur(r, art, width, height, style, geometry, parameters);
    if (!blur) return std::unexpected(blur.error());
    return shadowPlace(r, *blur, art, width, height, geometry, overdrawAlpha,
                       ShadowOutput{width, height, 0, 0});
}


// ---- G4: a mascara, a refracao e os realces --------------------------------------

Result<void> glassMask(Resident& r, const Slab& art, const Slab& field, std::uint32_t width,
                       std::uint32_t height, std::int32_t originY,
                       const OpacityMaskArguments& args, const Slab& counters,
                       std::uint32_t slot) {
    GlassMaskPush p{};
    p.w = width;
    p.h = height;
    p.originY = originY;
    p.slot = slot;
    p.borderWidth = args.borderWidth;
    p.ob0 = args.opacityBounds[0];
    p.ob1 = args.opacityBounds[1];
    p.cb0 = args.contourOpacityBounds[0];
    p.cb1 = args.contourOpacityBounds[1];
    p.boundsY = args.bounds[1];
    p.boundsH = args.bounds[3];
    // O `alphaFloor` padrao de `opacityMaskMissedPixels`.
    p.alphaFloor = 0.5f;
    p.kind = static_cast<std::uint32_t>(args.kind);
    p.segments = args.rampSegments;
    // A rampa do gradiente: os MESMOS floats que `gradientOpacityMask` le na
    // CPU, um `vec4` de coeficientes por segmento. No ramo do shader nada a le.
    Range ramp = whole(r.dummy());
    if (args.kind == OpacityMaskKind::Gradient && args.rampSegments > 0) {
        auto staged = r.stage(args.ramp, sizeof(float) * 4 * args.rampSegments);
        if (!staged) return std::unexpected(staged.error());
        ramp = *staged;
    }
    return r.dispatch(r.glassMask, {whole(art), whole(field), whole(counters), ramp}, &p,
                      groups16(width), groups16(height));
}

Result<void> refract(Resident& r, const Slab& target, const Slab& field, const PixelGrid& grid,
                     const GlassRefraction& g, const SourceView* fieldView) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(grid.texels()) * 16;
    auto map = r.acquire(bytes);
    if (!map) return std::unexpected(map.error());
    DisplacePush d{};
    d.params[0] = g.heightPixels;
    d.params[1] = g.curvature;
    d.params[2] = g.offset;
    d.params[3] = g.maskOffset;
    d.rot[0] = g.angleCos;
    d.rot[1] = g.angleSin;
    d.w = grid.width;
    d.h = grid.height;
    d.fw = fieldView ? fieldView->width : grid.width;
    d.fx = fieldView ? fieldView->x : 0;
    d.fy = fieldView ? fieldView->y : 0;
    if (auto ok = r.dispatch(r.displace, {whole(*map), whole(field)}, &d, groups16(grid.width),
                             groups16(grid.height));
        !ok) {
        return ok;
    }
    // O fundo e uma FOTO do alvo: refratar no lugar realimentaria pixels ja
    // refratados (o comentario de `glassOver`).
    auto backdrop = r.acquire(bytes);
    if (!backdrop) return std::unexpected(backdrop.error());
    r.copy(whole(target), *backdrop);
    RefractPush p{};
    p.w = grid.width;
    p.h = grid.height;
    p.ox = grid.originX;
    p.oy = grid.originY;
    p.size = grid.size;
    p.variant = g.variant;
    p.scale = g.scalePixels;
    p.inv = 1.0f / static_cast<float>(grid.size);
    return r.dispatch(r.refract, {whole(target), whole(*backdrop), whole(*map)}, &p,
                      groups16(grid.width), groups16(grid.height));
}

bool highlightModeTranscribed(BlendMode m) {
    return m == BlendMode::Normal || m == BlendMode::PlusLighter || m == BlendMode::PlusDarker ||
           m == BlendMode::Screen || m == BlendMode::Multiply;
}

bool resolveHighlights(const HighlightSlot* slots, std::size_t count,
                       const SpecularArguments& args, std::vector<double>& records,
                       bool chiclet) {
    records.clear();
    // `glassHighlightFragment` com `fwidth(sd) == 1`: a largura da banda.
    constexpr double kEps = 0.0009765625;
    constexpr double kBandWidth = 0.8330078125;
    const double band = std::min(std::max(1.0, kEps), 2.0) * kBandWidth;
    const HighlightParameters& hp = renderingParameters(args.generation).highlights;
    for (std::size_t s = 0; s < count; ++s) {
        const GlassHighlightSettings g =
            chiclet ? resolveChicletHighlight(slots[s], args) : resolveHighlight(slots[s], args);
        if (g.opacity <= 0.0 || g.height <= 0.0) continue;
        if (!highlightModeTranscribed(g.blendMode)) return false;
        const GlyphVCM& vcm = slots[s].isDarklight ? hp.glyphDarklightVCM : hp.glyphHighlightVCM;
        double rec[kHighlightStride] = {};
        rec[0] = g.inset;
        rec[1] = g.height;
        // As duas reescritas da fronteira do shader, as de `glassHighlightFragment`:
        // a curvatura zerada com `inset < 0` e o cone (`highlightCone`).
        rec[2] = g.inset < 0.0 ? 0.0 : g.curvature;
        rec[3] = highlightCone(g.spread, g.bias);
        if (chiclet) {
            // O termo angular do rasterizador da pastilha (`chicletHighlightFragment`).
            const ChicletCone c = chicletHighlightCone(g.spread);
            rec[3] = c.cone;
            rec[16] = c.alwaysLit ? 1.0 : 0.0;
        }
        rec[4] = g.directionX;
        rec[5] = g.directionY;
        rec[6] = 1.0 / g.bias - 2.0;
        rec[7] = g.opacity;
        rec[8] = g.colour[0];
        rec[9] = static_cast<double>(static_cast<std::uint32_t>(g.blendMode));
        rec[10] = vcm.lumaFloor;
        rec[11] = vcm.lumaCeiling;
        rec[12] = vcm.saturation;
        rec[13] = band;
        rec[14] = g.colour[1];   // a cor por canal (a mascara do Clear)
        rec[15] = g.colour[2];
        records.insert(records.end(), rec, rec + kHighlightStride);
    }
    return true;
}

Result<void> highlights(Resident& r, const Slab& target, const Slab& field, std::uint32_t width,
                        std::uint32_t height, const std::vector<double>& records, bool chiclet,
                        bool useVCM, bool clampPlusLighter, const Slab& counters,
                        std::uint32_t slot, const SourceView* fieldView) {
    const std::uint32_t n = static_cast<std::uint32_t>(records.size() / kHighlightStride);
    if (n == 0) return {};
    auto staged = resident(r, records.data(), records.size() * sizeof(double));
    if (!staged) return std::unexpected(staged.error());
    HighlightPush p{width, height, n, chiclet ? 1u : 0u, useVCM ? 1u : 0u,
                    clampPlusLighter ? 1u : 0u, slot, fieldView ? fieldView->width : width,
                    fieldView ? fieldView->x : 0, fieldView ? fieldView->y : 0};
    return r.dispatch(r.highlight, {whole(target), whole(field), whole(*staged), whole(counters)},
                      &p, groups16(width), groups16(height));
}

Result<void> glow(Resident& r, const Slab& target, const Slab& field, std::uint32_t width,
                  std::uint32_t height, std::int32_t originX, std::int32_t originY,
                  const GlowArguments& args, const SourceView* fieldView) {
    static_assert(sizeof(GlowArguments) == 80, "a GlowArguments field is missing from the push");
    GlowPush p{};
    p.radius = args.radius;
    p.biasAmount = args.biasAmount;
    p.alpha = args.alpha;
    p.maxDistance = args.maxDistance;
    p.edgeWidth = args.edgeWidth;
    p.edgeWidthFlat = args.edgeWidthFlat;
    for (int k = 0; k < 4; ++k) p.clip[k] = args.clip[k];
    p.w = width;
    p.h = height;
    p.ox = originX;
    p.oy = originY;
    p.fw = fieldView ? fieldView->width : width;
    p.fx = fieldView ? fieldView->x : 0;
    p.fy = fieldView ? fieldView->y : 0;
    return r.dispatch(r.glow, {whole(target), whole(field)}, &p, groups16(width),
                      groups16(height));
}

}  // namespace rb::gpu
