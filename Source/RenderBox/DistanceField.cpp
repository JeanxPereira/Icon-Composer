// See `DistanceField.h` for the line between the two halves of this file:
// everything above `PART TWO` is transcribed from mod95 and sealed; everything
// below it is this project's own algorithm and is not.
#include "Source/RenderBox/DistanceField.h"

#include "Source/RenderBox/Parallel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <numeric>
#include <utility>

namespace rb {

// ===========================================================================
// PART ONE -- `distanceGradient_v1`, transcribed from mod95
// ===========================================================================

float fieldNarrowToHalf(float v) {
    // Round-to-nearest-even through the 16-bit format via the compiler's own
    // conversion. Hand-rolled bit twiddling here would be a second half
    // implementation to keep in step with the driver's `packHalf2x16`, and the
    // two would drift on the tie.
    const _Float16 h = static_cast<_Float16>(v);
    return static_cast<float>(h);
}

float FieldTexture::sampleX(const float uv[2]) const {
    if (width == 0 || height == 0) return 0.0f;
    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);
    // The same three operations `rbFieldSample` performs in the probe, in the
    // same order: scale by the size, floor, clamp into range. `uv` reaches here
    // already clamped by the layer's own rect, so the multiply cannot run away.
    int cx = static_cast<int>(std::floor(uv[0] * static_cast<float>(w)));
    int cy = static_cast<int>(std::floor(uv[1] * static_cast<float>(h)));
    cx = std::clamp(cx, 0, w - 1);
    cy = std::clamp(cy, 0, h - 1);
    return fieldNarrowToHalf(rgba[(static_cast<std::size_t>(cy) * width + cx) * 4]);
}

void fieldUv(const FieldLayer& layer, float px, float py, float out[2]) {
    // `%44`: the inner fma is over p.yy, and `%45`: the outer over p.xx. The
    // nesting is not commutative in the last bit, so it is transcribed and not
    // rearranged.
    const float inner0 = std::fma(py, layer.m1[0], layer.m2[0]);
    const float inner1 = std::fma(py, layer.m1[1], layer.m2[1]);
    const float outer0 = std::fma(px, layer.m0[0], inner0);
    const float outer1 = std::fma(px, layer.m0[1], inner1);
    // `air.fast_clamp`, `%46`.
    out[0] = std::clamp(outer0, layer.m3[0], layer.m4[0]);
    out[1] = std::clamp(outer1, layer.m3[1], layer.m4[1]);
}

void distanceGradient(float px, float py, float dpdx, float dpdy,
                      const DistanceGradientParams& params, const FieldTexture& texture,
                      float out[4]) {
    float uv[2];
    fieldUv(params.layer, px, py, uv);
    const float sdf = texture.sampleX(uv);

    // `%50`: `fcmp oeq half %49, 0`. The comparison is on the HALF the texture
    // returned, which is why the sample is narrowed before it gets here -- a
    // float that merely rounds to zero in half would take the other branch.
    if (sdf == 0.0f) {
        // `%51`: `(0, u1, u1, 0)`. The zero in `.w` is the load-bearing part --
        // `glassBackground_v1` multiplies its mask by this channel, so a shader
        // that returned 1 here would paint glass over a place the field says
        // has no shape in it.
        const float u1 = fieldNarrowToHalf(params.fallback);
        out[0] = 0.0f;
        out[1] = u1;
        out[2] = u1;
        out[3] = 0.0f;
        return;
    }

    // `%60`/`%63`: the derivatives enter in ABSOLUTE value, so the four taps
    // straddle `p` no matter which way the quad is walked.
    const float hx = std::fabs(dpdx);
    const float hy = std::fabs(dpdy);

    // Five taps, all through the same layer transform. `%65` and `%74` are
    // `p - (hx,0)` and `(hx,0) + p`; `%84` and `%93` the same in y.
    float t[2];
    fieldUv(params.layer, px - hx, py, t);
    const float left = texture.sampleX(t);
    fieldUv(params.layer, hx + px, py, t);
    const float right = texture.sampleX(t);
    fieldUv(params.layer, px, py - hy, t);
    const float down = texture.sampleX(t);
    fieldUv(params.layer, px, hy + py, t);
    const float up = texture.sampleX(t);

    // `%102`/`%105`: an `fsub half`, THEN `fpext` to float. The difference is
    // rounded to half before anything else touches it. There is no division by
    // the step -- the central difference is left unscaled, because the very
    // next thing done to it is a normalize that would divide the scale out
    // again.
    float g[2] = {fieldNarrowToHalf(right - left), fieldNarrowToHalf(up - down)};

    // `%108`/`%109`: `any(delta != 0)`. This is the guard, and it is not
    // decoration: `fast_rsqrt(0)` is an infinity and the multiply that follows
    // turns it into a NaN, which would then be written into the field and
    // propagate through every consumer. A flat neighbourhood -- the inside of a
    // large shape, or a tap step small enough that all four land in one texel --
    // reaches it on ordinary input, not only on a pathological one.
    if (g[0] != 0.0f || g[1] != 0.0f) {
        // `%111`..`%115`: `delta * rsqrt(dot(delta, delta))`.
        const float inv = 1.0f / std::sqrt(g[0] * g[0] + g[1] * g[1]);
        g[0] *= inv;
        g[1] *= inv;
    }

    // `%121`: `llvm.fmuladd(g, scale.xx, fallback.xx)`. That intrinsic is the
    // CONTRACTIBLE one -- whether the target's back end fuses it is `[OBS]`, it
    // is a choice made after this IR. This transcription fuses, and the result
    // is narrowed to half immediately afterwards (`%122`), where a one-ULP
    // float disagreement almost always vanishes; the gate allows one half ULP
    // here for exactly that reason and no more.
    out[0] = sdf;
    out[1] = fieldNarrowToHalf(std::fma(g[0], params.scale, params.fallback));
    out[2] = fieldNarrowToHalf(std::fma(g[1], params.scale, params.fallback));
    // `%125`: `0xH3C00`, the half 1.0.
    out[3] = 1.0f;
}

// ===========================================================================
// PART TWO -- the field generator. OUR ALGORITHM, NOT A TRANSCRIPTION.
// ===========================================================================
//
// Nothing from here down was read from a binary. See the header for the method,
// its cost, and the three things it gets wrong.

namespace {

struct Closest {
    double d2 = 0.0;
    double cx = 0.0;
    double cy = 0.0;
};

// Exact point-to-segment distance, in double. Double and not float because
// this is the oracle the glass tests will be measured against: the cancellation
// in `px - cx` for a point a hundredth of a pixel off a boundary a thousand
// pixels away is where a float would lose the digits the gate is trying to
// read.
Closest closestOnSegment(double px, double py, double ax, double ay, double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double len2 = dx * dx + dy * dy;
    double t = 0.0;
    if (len2 > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / len2;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    }
    Closest c;
    c.cx = ax + t * dx;
    c.cy = ay + t * dy;
    const double ex = px - c.cx;
    const double ey = py - c.cy;
    c.d2 = ex * ex + ey * ey;
    return c;
}

// Which side of the directed edge a->b the point lies on. Positive is left.
double isLeft(double ax, double ay, double bx, double by, double px, double py) {
    return (bx - ax) * (py - ay) - (px - ax) * (by - ay);
}

}  // namespace

FieldShape::FieldShape(const std::vector<FieldContour>& contours) {
    for (const FieldContour& c : contours) {
        const std::size_t n = c.xy.size() / 2;
        if (n < 2) continue;   // a contour of one point encloses nothing
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;   // the closing segment is implied
            segments_.push_back(c.xy[i * 2]);
            segments_.push_back(c.xy[i * 2 + 1]);
            segments_.push_back(c.xy[j * 2]);
            segments_.push_back(c.xy[j * 2 + 1]);
        }
    }
}

float FieldShape::distanceAt(float x, float y, FieldRule rule) const {
    const double px = x;
    const double py = y;
    if (segments_.empty()) return 0.0f;

    double best = -1.0;
    int winding = 0;
    bool parity = false;
    for (std::size_t s = 0; s < segments_.size(); s += 4) {
        const double ax = segments_[s], ay = segments_[s + 1];
        const double bx = segments_[s + 2], by = segments_[s + 3];

        const Closest c = closestOnSegment(px, py, ax, ay, bx, by);
        if (best < 0.0 || c.d2 < best) best = c.d2;

        // The inside test rides along with the distance loop because it walks
        // the same segments, and walking them twice would double the cost of
        // the one thing that is already the expensive part.
        if (rule == FieldRule::NonZero) {
            // The upward/downward crossing count. The half-open comparison
            // (`<=` on one side, `<` on the other) is what stops a vertex
            // exactly at `py` from being counted twice.
            if (ay <= py) {
                if (by > py && isLeft(ax, ay, bx, by, px, py) > 0.0) ++winding;
            } else {
                if (by <= py && isLeft(ax, ay, bx, by, px, py) < 0.0) --winding;
            }
        } else {
            if ((ay > py) != (by > py)) {
                const double t = (py - ay) / (by - ay);
                if (px < ax + t * (bx - ax)) parity = !parity;
            }
        }
    }

    const bool inside = rule == FieldRule::NonZero ? winding != 0 : parity;
    const double d = std::sqrt(best);
    return static_cast<float>(inside ? -d : d);
}

FieldSample FieldShape::sampleAt(float x, float y, const FieldOptions& options) const {
    FieldSample out;
    if (segments_.empty()) return out;

    const double px = x;
    const double py = y;

    double best = -1.0;
    double bcx = 0.0, bcy = 0.0;
    int winding = 0;
    bool parity = false;
    for (std::size_t s = 0; s < segments_.size(); s += 4) {
        const double ax = segments_[s], ay = segments_[s + 1];
        const double bx = segments_[s + 2], by = segments_[s + 3];

        const Closest c = closestOnSegment(px, py, ax, ay, bx, by);
        if (best < 0.0 || c.d2 < best) {
            best = c.d2;
            bcx = c.cx;
            bcy = c.cy;
        }

        if (options.rule == FieldRule::NonZero) {
            if (ay <= py) {
                if (by > py && isLeft(ax, ay, bx, by, px, py) > 0.0) ++winding;
            } else {
                if (by <= py && isLeft(ax, ay, bx, by, px, py) < 0.0) --winding;
            }
        } else {
            if ((ay > py) != (by > py)) {
                const double t = (py - ay) / (by - ay);
                if (px < ax + t * (bx - ax)) parity = !parity;
            }
        }
    }

    const bool inside = options.rule == FieldRule::NonZero ? winding != 0 : parity;
    const double dist = std::sqrt(best);
    const double sign = inside ? -1.0 : 1.0;
    out.distance = static_cast<float>(sign * dist);

    // The exact gradient: `d(p) = sign * |p - closest|`, so `grad d` is
    // `sign * unit(p - closest)`. Unit everywhere the closest point is
    // well defined, which is everywhere except the boundary itself and the
    // medial axis -- and on the medial axis it stays unit while merely picking
    // one of the two competing directions, which is the honest answer for a
    // quantity that has no single one.
    if (dist > 0.0) {
        out.gx = static_cast<float>(sign * (px - bcx) / dist);
        out.gy = static_cast<float>(sign * (py - bcy) / dist);
    } else {
        // Exactly on the boundary. `p - closest` is the zero vector and the
        // exact form has nothing to say, so this is the one place a difference
        // is used -- with e = 0.5, the same step `Field.frag` takes.
        const double e = 0.5;
        const double dxp = distanceAt(static_cast<float>(px + e), static_cast<float>(py), options.rule);
        const double dxm = distanceAt(static_cast<float>(px - e), static_cast<float>(py), options.rule);
        const double dyp = distanceAt(static_cast<float>(px), static_cast<float>(py + e), options.rule);
        const double dym = distanceAt(static_cast<float>(px), static_cast<float>(py - e), options.rule);
        const double gx = dxp - dxm;
        const double gy = dyp - dym;
        const double len = std::sqrt(gx * gx + gy * gy);
        if (len > 0.0) {
            out.gx = static_cast<float>(gx / len);
            out.gy = static_cast<float>(gy / len);
        }
    }

    // `Field.frag`'s coverage with `fwidth(d)` at the value a unit-slope field
    // on a pixel grid actually has. Written with the width as a divisor so a
    // test can widen the band and watch it widen, which is the only way to
    // prove the 0.5 is a half-pixel offset and not a magic number.
    const double w = options.aaWidth > 0.0f ? options.aaWidth : 1.0f;
    const double cov = -out.distance / w + 0.5;
    out.coverage = static_cast<float>(cov < 0.0 ? 0.0 : (cov > 1.0 ? 1.0 : cov));
    return out;
}

FieldImage generateField(const FieldShape& shape, std::uint32_t width, std::uint32_t height,
                         FieldOptions options) {
    FieldImage img;
    img.width = width;
    img.height = height;
    img.rgba.assign(static_cast<std::size_t>(width) * height * 4, 0.0f);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            // Pixel CENTRES: where `gl_FragCoord.xy` lands and where the
            // coverage pass measures. Corners would put the field half a pixel
            // off from everything else this tower draws.
            const FieldSample s = shape.sampleAt(static_cast<float>(x) + 0.5f,
                                                 static_cast<float>(y) + 0.5f, options);
            float* p = img.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = s.distance;
            p[1] = s.gx;
            p[2] = s.gy;
            p[3] = s.coverage;
        }
    }
    return img;
}

FieldImage generateField(const std::vector<FieldContour>& contours, std::uint32_t width,
                         std::uint32_t height, FieldOptions options) {
    return generateField(FieldShape(contours), width, height, options);
}

// ===========================================================================
// PART THREE -- the field from a rasterised alpha. See `DistanceField.h` for
// the `[BIN]` that says this is the input the TARGET uses, and for the line
// between that reading and the transform below, which is ours.
// ===========================================================================

namespace {
constexpr double kEdtInf = 1e20;

// A buffer whose elements are NOT value-initialised on `resize`. The transform
// writes every element before it reads one, and zeroing a million doubles
// first -- on memory the OS then has to fault in -- was a measurable slice of
// the field.
template <typename T>
struct NoInitAllocator : std::allocator<T> {
    using value_type = T;
    template <typename U> struct rebind { using other = NoInitAllocator<U>; };
    NoInitAllocator() = default;
    template <typename U> NoInitAllocator(const NoInitAllocator<U>&) noexcept {}
    template <typename U> void construct(U* p) noexcept { ::new (static_cast<void*>(p)) U; }
    template <typename U, typename... A> void construct(U* p, A&&... a) {
        ::new (static_cast<void*>(p)) U(std::forward<A>(a)...);
    }
};
template <typename T>
using EdtBuffer = std::vector<T, NoInitAllocator<T>>;

// The full two-dimensional transform: seeds are the texels for which
// `seed[t]` is true, and every other texel comes back with the squared
// distance to the nearest seed CENTRE plus, in `site`, that seed's linear
// index. Column pass then row pass, which is what makes the composition exact
// rather than separable-approximate: after the column pass each column holds
// the true 1D answer, and the row pass takes the lower envelope of those.
//
// BOTH PASSES RUN ON SEVERAL THREADS, AND NOT ONE NUMBER MOVES.
//
// The first pass reads column `x` and writes column `x`; the second reads row
// `y` and writes row `y`. No cell is read by one line and written by another,
// there is no accumulator, and `edtSquared1d` is a pure function of the line
// it is handed -- so a column computed on a worker thread is the SAME
// `double`, bit for bit, as the one computed here, because it is the same
// arithmetic in the same order over the same inputs. The only thing the split
// changes is which core runs it.
//
// The scratch the envelope needs (`f`, `d`, `v`, `z`, `arg`) used to be
// allocated once for the whole transform and is now allocated once per WORKER,
// which is what makes it unshared. The cost of that is one allocation per
// thread per pass against a sweep of a million texels, and the alternative --
// one shared buffer -- would be a data race and a wrong answer, not a slower
// one.
void edt2d(const std::vector<char>& seed, int w, int h, EdtBuffer<double>& sq,
           EdtBuffer<int>& site) {
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    const int n = std::max(w, h);

    // Neither buffer is zeroed first: the seed loop writes every `sq`, the
    // column pass writes every `site`, and the row pass rewrites every `site`.
    sq.resize(texels);
    site.resize(texels);

    // The envelope makes two sweeps over a line and the caller copies the line
    // in and out, which is a handful of operations per element either way:
    // eight is the estimate, in the same unit every other call site uses.
    const std::size_t pass = texels * 8;

    parallelRanges(static_cast<std::size_t>(h), texels, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t t = y0 * w; t < y1 * w; ++t) sq[t] = seed[t] ? 0.0 : kEdtInf;
    });

    // `site` first holds what used to be a separate `siteY`: the row of the
    // nearest seed within t's own column, which is all the column pass can
    // know. The row pass turns it into a full index -- reading the row's
    // column answers from a private copy, because it overwrites them in place.
    edtSquaredColumns(sq.data(), w, h, site.data());
    parallelRanges(static_cast<std::size_t>(h), pass, [&](std::size_t y0, std::size_t y1) {
        std::vector<double> f(static_cast<std::size_t>(n));
        std::vector<double> d(static_cast<std::size_t>(n));
        std::vector<int> v(static_cast<std::size_t>(n) + 1);
        std::vector<double> z(static_cast<std::size_t>(n) + 2);
        std::vector<int> arg(static_cast<std::size_t>(n));
        std::vector<int> siteY(static_cast<std::size_t>(w));
        for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y) {
            double* row = &sq[static_cast<std::size_t>(y) * w];
            int* srow = &site[static_cast<std::size_t>(y) * w];
            for (int x = 0; x < w; ++x) f[static_cast<std::size_t>(x)] = row[x];
            std::copy(srow, srow + w, siteY.begin());
            edtSquared1d(f, d, v, z, w, &arg);
            for (int x = 0; x < w; ++x) {
                row[x] = d[static_cast<std::size_t>(x)];
                const int sx = arg[static_cast<std::size_t>(x)];
                const int sy = siteY[static_cast<std::size_t>(sx)];
                srow[x] = sy < 0 ? -1 : sy * w + sx;
            }
        }
    });
}
}  // namespace

void edtSquared1d(std::vector<double>& f, std::vector<double>& d, std::vector<int>& v,
                  std::vector<double>& z, int n, std::vector<int>* arg) {
    int k = 0;
    v[0] = 0;
    z[0] = -kEdtInf;
    z[1] = kEdtInf;
    for (int q = 1; q < n; ++q) {
        double s = 0.0;
        while (true) {
            const double vk = v[static_cast<std::size_t>(k)];
            s = ((f[static_cast<std::size_t>(q)] + static_cast<double>(q) * q) -
                 (f[static_cast<std::size_t>(v[static_cast<std::size_t>(k)])] + vk * vk)) /
                (2.0 * q - 2.0 * vk);
            if (k == 0 || s > z[static_cast<std::size_t>(k)]) break;
            --k;
        }
        ++k;
        v[static_cast<std::size_t>(k)] = q;
        z[static_cast<std::size_t>(k)] = s;
        z[static_cast<std::size_t>(k) + 1] = kEdtInf;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[static_cast<std::size_t>(k) + 1] < q) ++k;
        const int vk = v[static_cast<std::size_t>(k)];
        const double dq = static_cast<double>(q) - vk;
        d[static_cast<std::size_t>(q)] = dq * dq + f[static_cast<std::size_t>(vk)];
        if (arg) (*arg)[static_cast<std::size_t>(q)] = vk;
    }
}

void edtSquaredColumns(double* sq, int w, int h, int* siteY) {
    if (w <= 0 || h <= 0) return;
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    // Sixteen columns: two cache lines of doubles per row, and sixteen columns
    // of a 1024-high grid are 128 KB each for `f` and `d`, which stays in L2.
    constexpr int kTile = 16;
    const std::size_t hs = static_cast<std::size_t>(h);
    parallelRanges(static_cast<std::size_t>(w), texels * 8, [&](std::size_t x0, std::size_t x1) {
        std::vector<std::vector<double>> f(kTile, std::vector<double>(hs));
        std::vector<std::vector<double>> d(kTile, std::vector<double>(hs));
        std::vector<std::vector<int>> arg(siteY ? kTile : 0, std::vector<int>(hs));
        std::vector<int> v(hs + 1);
        std::vector<double> z(hs + 2);
        for (std::size_t tx = x0; tx < x1; tx += kTile) {
            const int nt = static_cast<int>(std::min<std::size_t>(kTile, x1 - tx));
            for (std::size_t y = 0; y < hs; ++y) {
                const double* row = &sq[y * w + tx];
                for (int j = 0; j < nt; ++j) f[static_cast<std::size_t>(j)][y] = row[j];
            }
            for (int j = 0; j < nt; ++j) {
                const std::size_t ju = static_cast<std::size_t>(j);
                edtSquared1d(f[ju], d[ju], v, z, h, siteY ? &arg[ju] : nullptr);
            }
            for (std::size_t y = 0; y < hs; ++y) {
                double* row = &sq[y * w + tx];
                for (int j = 0; j < nt; ++j) row[j] = d[static_cast<std::size_t>(j)][y];
                if (siteY) {
                    int* srow = &siteY[y * w + tx];
                    for (int j = 0; j < nt; ++j) {
                        const std::size_t ju = static_cast<std::size_t>(j);
                        srow[j] = d[ju][y] >= kEdtInf ? -1 : arg[ju][y];
                    }
                }
            }
        }
    });
}

namespace {

// The whole of the field, given an inside/outside mask on a grid that may be
// FINER than the field's own. `ss` is how many mask texels span one field
// pixel, and it is odd, so mask texel `x * ss + ss / 2` is the one whose centre
// IS the field pixel's centre. With `ss == 1` this is, arithmetic for
// arithmetic, what `generateFieldFromAlpha` did before it was factored out
// here -- including the ORDER of the four border tests, which decides ties.
FieldImage fieldFromInsideMask(const std::vector<char>& inside,
                               const std::vector<float>& coverage, int mw, int mh, int ss,
                               const FieldOptions& options) {
    FieldImage img;
    const std::size_t texels = static_cast<std::size_t>(mw) * mh;

    std::vector<char> outsideSeed(texels, 0);
    for (std::size_t t = 0; t < texels; ++t) outsideSeed[t] = inside[t] ? 0 : 1;
    std::vector<char> insideSeed(inside);

    // `[INF]` THE SUB-TEXEL SEED. See `FieldOptions::subpixelSeed` for why this
    // is ours and for the addresses that were chased before it was written.
    //
    // `sOff[t]` is the SIGNED distance from texel t's centre to the surface,
    // positive outside, in texels: the inverse of this tower's own coverage
    // convention `cov = -d/aaWidth + 0.5` at `aaWidth == 1`, so the seed and
    // the coverage channel below are two readings of one rule and cannot drift.
    //
    // `band[t]` marks a texel the surface actually CROSSES. Two conditions, and
    // the second is not decoration: a texel must carry partial coverage AND
    // straddle the class boundary. Partial coverage alone would mark every texel
    // of a uniformly TRANSLUCENT fill -- a layer painted at alpha 0.5 has
    // `0.5 - c == 0` everywhere and would collapse the whole field to zero. A
    // translucent plateau has no opposite-class neighbour, so it is not a band.
    const bool sub = options.subpixelSeed && coverage.size() == texels;
    std::vector<float> sOff;
    std::vector<float> bnx, bny;   // the band's outward unit normal
    std::vector<char> band;
    if (sub) {
        // EVERY texel starts at the binary answer -- half a texel, on the side
        // its class puts it. A seed that never becomes a band texel therefore
        // contributes exactly the 0.5 the old code used, which is what makes
        // this reduce to the old arithmetic instead of merely approximating it.
        sOff.assign(texels, 0.0f);
        for (std::size_t t = 0; t < texels; ++t) sOff[t] = inside[t] ? -0.5f : 0.5f;
        band.assign(texels, 0);
        bnx.assign(texels, 0.0f);
        bny.assign(texels, 0.0f);
        // One row per worker: texel t writes only its own `sOff`, `band`,
        // `bnx`, `bny` and seeds, and reads only `inside` and `coverage`,
        // which nothing here writes.
        parallelRanges(static_cast<std::size_t>(mh), texels * 4, [&](std::size_t r0, std::size_t r1) {
        for (int y = static_cast<int>(r0); y < static_cast<int>(r1); ++y) {
            for (int x = 0; x < mw; ++x) {
                const std::size_t t = static_cast<std::size_t>(y) * mw + x;
                const float c = coverage[t];
                if (!(c > 0.0f) || !(c < 1.0f)) continue;   // no sub-texel information
                const bool in = inside[t] != 0;
                bool straddles = false;
                if (x > 0 && ((inside[t - 1] != 0) != in)) straddles = true;
                if (x + 1 < mw && ((inside[t + 1] != 0) != in)) straddles = true;
                if (y > 0 && ((inside[t - mw] != 0) != in)) straddles = true;
                if (y + 1 < mh && ((inside[t + mw] != 0) != in)) straddles = true;
                if (!straddles) continue;

                // THE NORMAL, from a Sobel of the coverage: LEFT minus RIGHT,
                // because `d` rises where coverage falls, so this points
                // OUTWARD. It is what the lattice cannot give -- a vector
                // between two texel centres one apart has only eight directions
                // to choose from, 45 degrees apart, and that quantisation lands
                // on exactly the texels the specular lights.
                const int xm = x > 0 ? x - 1 : x;
                const int xp = x + 1 < mw ? x + 1 : x;
                const int ym = y > 0 ? y - 1 : y;
                const int yp = y + 1 < mh ? y + 1 : y;
                const std::size_t r0 = static_cast<std::size_t>(ym) * mw;
                const std::size_t r1 = static_cast<std::size_t>(y) * mw;
                const std::size_t r2 = static_cast<std::size_t>(yp) * mw;
                const double c00 = coverage[r0 + xm], c01 = coverage[r0 + x], c02 = coverage[r0 + xp];
                const double c10 = coverage[r1 + xm], c12 = coverage[r1 + xp];
                const double c20 = coverage[r2 + xm], c21 = coverage[r2 + x], c22 = coverage[r2 + xp];
                const double gx = (c00 + 2.0 * c10 + c20) - (c02 + 2.0 * c12 + c22);
                const double gy = (c00 + 2.0 * c01 + c02) - (c20 + 2.0 * c21 + c22);
                const double len = std::sqrt(gx * gx + gy * gy);
                if (!(len > 0.0)) continue;   // no normal, so no sub-texel answer

                sOff[t] = 0.5f - c;
                band[t] = 1;
                bnx[t] = static_cast<float>(gx / len);
                bny[t] = static_cast<float>(gy / len);
                // The surface passes THROUGH this texel, so it is a seed for
                // BOTH transforms whichever side its centre fell on. That is
                // what lets a band texel answer for itself at distance zero --
                // and it is what makes a straight edge come back exact, because
                // the near side no longer has to reach across to a centre of the
                // opposite class to find out where the surface is.
                outsideSeed[t] = 1;
                insideSeed[t] = 1;
            }
        }
        });
    }

    // Distance from every texel to the nearest OUTSIDE centre (what an inside
    // texel needs) and to the nearest INSIDE centre (what an outside texel
    // needs). Two transforms and not one, because a signed field wants the
    // depth on each side measured to the other side's seeds.
    EdtBuffer<double> sqOut, sqIn;
    EdtBuffer<int> siteOut, siteIn;
    edt2d(outsideSeed, mw, mh, sqOut, siteOut);
    edt2d(insideSeed, mw, mh, sqIn, siteIn);

    const int W = mw / ss;
    const int H = mh / ss;
    img.width = static_cast<std::uint32_t>(W);
    img.height = static_cast<std::uint32_t>(H);
    img.rgba.assign(static_cast<std::size_t>(W) * H * 4, 0.0f);

    const double aa = options.aaWidth > 0.0f ? static_cast<double>(options.aaWidth) : 1.0;
    const double inv = 1.0 / ss;
    // The half-width of the window swept around the foot point, in mask texels.
    // See the refinement below for why the centre is the foot and not the query.
    constexpr int kFootSearch = 2;
    const int half = ss / 2;

    // ONE ROW PER WORKER, AND NOTHING SHARED. Every iteration below writes the
    // four floats of ITS OWN pixel and reads only `inside`, `band`, `sOff`,
    // `bnx`, `bny`, `sqIn`, `sqOut`, `siteIn` and `siteOut`, all of which are
    // finished and const from here on. Splitting the outer loop therefore
    // cannot change a single float: each pixel is computed by the same
    // expression over the same inputs, only on another core.
    parallelRanges(static_cast<std::size_t>(H), static_cast<std::size_t>(W) * H * 8,
                   [&](std::size_t y0, std::size_t y1) {
    for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y) {
        for (int x = 0; x < W; ++x) {
            const int mx = x * ss + half;
            const int my = y * ss + half;
            const std::size_t t = static_cast<std::size_t>(my) * mw + mx;
            double dist = 0.0;
            double gx = 0.0;
            double gy = 0.0;
            double sign = 0.0;

            const int s = inside[t] ? siteOut[t] : siteIn[t];
            sign = inside[t] ? -1.0 : 1.0;
            double d = 0.0;

            if (sub && s >= 0 && band[static_cast<std::size_t>(s)]) {
                // `[INF]` THE SURFACE POINT, and this is the whole correction.
                //
                // The naive move is to keep the lattice distance and shorten it
                // by the seed's offset. That is WRONG and measurably so: the
                // offset runs along the seed's NORMAL and the lattice distance
                // runs along the line between two centres, and adding one to the
                // other as if they were collinear costs up to half a texel in
                // the worst case -- it made the worst error of a circle go from
                // 0.49 to 1.02 texels before it was caught.
                //
                // What the seed actually knows is a POINT: the surface passes
                // through `seed - sOff * normal`. Measuring the plain Euclidean
                // distance from the query texel to THAT point is right whatever
                // the angle between the two directions, exact for a straight
                // edge at any sub-texel offset, and it hands back a gradient that
                // moves continuously with the coverage instead of snapping to
                // the eight lattice directions.
                const std::size_t su = static_cast<std::size_t>(s);
                const double px = static_cast<double>(s % mw) - bnx[su] * sOff[su];
                const double py = static_cast<double>(s / mw) - bny[su] * sOff[su];
                const double vx = static_cast<double>(mx) - px;
                const double vy = static_cast<double>(my) - py;
                // SQUARED, in mask texels, for the whole of the search below: the
                // window centred on the foot lands on the band almost every time,
                // so its inner statement runs for real rather than falling out on
                // `!band[u]`, and a root per candidate was measured at three
                // quarters of a second on a 412 px render. Monotone, so the
                // comparison is the same one; the root is taken once, after.
                double best2 = vx * vx + vy * vy;
                if (vx != 0.0 || vy != 0.0) {
                    // The gradient is the direction `d` INCREASES, which is
                    // outward on both sides: away from the surface point when
                    // outside, towards it when inside.
                    gx = inside[t] ? -vx : vx;
                    gy = inside[t] ? -vy : vy;
                } else {
                    // The centre sits exactly ON the surface. The vector has
                    // nothing to say and the normal has everything.
                    gx = bnx[su];
                    gy = bny[su];
                }
                // `[INF]` AND THE NEAREST CENTRE IS NOT ALWAYS THE NEAREST
                // SURFACE. The transform picks the seed whose CENTRE is closest,
                // which is not the same question once each seed carries its own
                // surface point half a texel away. Close to the band the two
                // answers can disagree, and the disagreement is the worst error
                // this method has. It is also cheap to settle: look at the band
                // texels in the 3x3 around the query and keep the nearest
                // surface point among them. Eight reads per texel, no second
                // transform, and it only does anything within one texel of the
                // band -- which is exactly where the specular reads.
                //
                // `[INF]` AND THE NEIGHBOURHOOD IS THE FOOT'S, NOT THE QUERY'S.
                // The competitors of a query at depth r are not around the
                // query -- they are around the point on the surface it is
                // measuring to. Searching the query's own 3x3 therefore stops
                // helping one texel from the band, which is where this refinement
                // used to end; centring the same search on the foot of the
                // current best keeps it working at every depth the specular
                // reads. The window stays small because the foot is already
                // within a texel of the true one.
                const int fx = static_cast<int>(std::lround(px));
                const int fy = static_cast<int>(std::lround(py));
                for (int dy = -kFootSearch; dy <= kFootSearch; ++dy) {
                    const int uy = fy + dy;
                    if (uy < 0 || uy >= mh) continue;
                    for (int dx = -kFootSearch; dx <= kFootSearch; ++dx) {
                        const int ux = fx + dx;
                        if (ux < 0 || ux >= mw) continue;
                        const std::size_t u = static_cast<std::size_t>(uy) * mw + ux;
                        if (!band[u]) continue;
                        const double qx = static_cast<double>(ux) - bnx[u] * sOff[u];
                        const double qy = static_cast<double>(uy) - bny[u] * sOff[u];
                        const double wx = static_cast<double>(mx) - qx;
                        const double wy = static_cast<double>(my) - qy;
                        const double cand2 = wx * wx + wy * wy;
                        if (cand2 >= best2) continue;
                        best2 = cand2;
                        if (wx != 0.0 || wy != 0.0) {
                            gx = inside[t] ? -wx : wx;
                            gy = inside[t] ? -wy : wy;
                        } else {
                            gx = bnx[u];
                            gy = bny[u];
                        }
                    }
                }
                d = std::sqrt(best2) * inv;
            } else {
                if (inside[t]) {
                    dist = std::sqrt(sqOut[t]);
                    if (s >= 0) {
                        gx = static_cast<double>(s % mw) - mx;
                        gy = static_cast<double>(s / mw) - my;
                    }
                } else {
                    dist = std::sqrt(sqIn[t]);
                    if (s >= 0) {
                        // Outside, `d` grows as the shape recedes, so the
                        // gradient points AWAY from the nearest inside centre --
                        // the opposite sense from the inside branch, which is why
                        // the two are written out rather than shared.
                        gx = static_cast<double>(mx) - (s % mw);
                        gy = static_cast<double>(my) - (s / mw);
                    }
                }
                // Centres to contour: the boundary sits half a mask texel before
                // the first centre of the opposite class. Then out of mask texels
                // and into field pixels, which is the only thing `ss` changes
                // about the arithmetic.
                d = (dist - 0.5) * inv;
            }

            if (inside[t]) {
                // The four virtual outside rows one step beyond each edge, at
                // the FIELD's resolution and not the mask's -- the empty
                // one-texel border of the target's own SDF is one FIELD texel
                // however finely this rasterised. When one of them is nearer
                // than any real outside texel it wins, and the gradient turns
                // to face it -- axis aligned, because the row is.
                const double px = static_cast<double>(x) + 0.5;
                const double py = static_cast<double>(y) + 0.5;
                const double bx1 = static_cast<double>(W) - px;
                const double by1 = static_cast<double>(H) - py;
                if (px < d) { d = px; gx = -1.0; gy = 0.0; }
                if (py < d) { d = py; gx = 0.0; gy = -1.0; }
                if (bx1 < d) { d = bx1; gx = 1.0; gy = 0.0; }
                if (by1 < d) { d = by1; gx = 0.0; gy = 1.0; }
            }

            const double len = std::sqrt(gx * gx + gy * gy);
            float* p = img.rgba.data() + (static_cast<std::size_t>(y) * W + x) * 4;
            p[0] = static_cast<float>(sign * d);
            if (len > 0.0) {
                p[1] = static_cast<float>(gx / len);
                p[2] = static_cast<float>(gy / len);
            }
            const double cov = -static_cast<double>(p[0]) / aa + 0.5;
            p[3] = static_cast<float>(cov < 0.0 ? 0.0 : (cov > 1.0 ? 1.0 : cov));
        }
    }
    });

    // `[INF]` THE NORMAL COMES OFF THE FIELD, NOT OFF THE LATTICE. OURS, AND
    // MEASURED RATHER THAN ARGUED.
    //
    // Everything above builds the direction out of the vector from the query
    // texel to ONE seed, and a seed is an integer lattice point. The LENGTH of
    // that vector is fine -- it is the distance, and the sub-texel surface point
    // already corrects it -- but its DIRECTION is not: which seed wins changes
    // discretely as the query slides along an edge, so the direction jumps from
    // texel to texel instead of turning with the curve.
    //
    // MEASURED on a circle of r = 180 in a 512 field, against the closed-form
    // radial normal, mean/worst in degrees, by depth in pixels:
    //
    //                          0.5-1      2-4        4-8       8-16
    //   vector to the seed   15.4/41.5  11.2/38.8  7.4/33.9  5.0/20.5
    //   Sobel of `d`          1.0/ 4.1   2.5/15.7  2.8/19.9  2.4/13.1
    //
    // `drawSpecular` hands `p[1]`,`p[2]` to `dot(direction, normal)` inside a
    // cosine cone, so those tens of degrees are not a rounding difference: they
    // are the comb visible along every curved edge this renderer draws.
    //
    // WHY SOBEL AND NOT A TWO-TAP CENTRAL DIFFERENCE. The same sweep says
    // 3.4/20.7 and 3.3/23.5 in the two middle bands, against Sobel's 2.5 and
    // 2.8. The field's own residue against the closed form is 0.06 px mean, and
    // a 3x3 averages across it where two taps cannot.
    //
    // WHAT STAYS ON THE SEED VECTOR, and it is the one place differencing is
    // WRONG rather than merely coarser: the MEDIAL AXIS. There the true field
    // has a crease, the difference across it is short in both axes, and
    // normalising a short vector would invent a direction where the geometry
    // has an ambiguity -- the failure
    // `the_medial_axis_is_where_the_gradient_stops_being_differenceable` states
    // for the exact field. A field of unit slope differences to
    // `kSobelUnitSlope`; below half of that the crease has the vote and the
    // seed vector is kept.
    //
    // The 1-texel frame keeps the seed vector too: a clamped Sobel reads a
    // duplicated column, and the magnitude it answers with cannot be told from
    // a crease.
    {
        const double kSobelUnitSlope = 8.0;   // |Sobel| over a ramp of slope 1
        const double kMedialFloor = 0.5;
        const float* base = img.rgba.data();
        const auto D = [&](int xx, int yy) {
            return static_cast<double>(base[(static_cast<std::size_t>(yy) * W + xx) * 4]);
        };
        // Also one row per worker, and it is unshared for a reason worth
        // saying out loud: this pass READS channel 0 of a 3x3 and WRITES
        // channels 1 and 2 of the centre. Reads and writes never touch the
        // same float, so no row can see a neighbour half-written -- which is
        // what makes it safe to split and what would stop being true the day
        // somebody made it write `p[0]`.
        const int rows = H > 2 ? H - 2 : 0;
        parallelRanges(static_cast<std::size_t>(rows), static_cast<std::size_t>(W) * H * 6,
                       [&](std::size_t r0, std::size_t r1) {
        for (int y = static_cast<int>(r0) + 1; y < static_cast<int>(r1) + 1; ++y) {
            for (int x = 1; x + 1 < W; ++x) {
                const double d00 = D(x - 1, y - 1), d01 = D(x, y - 1), d02 = D(x + 1, y - 1);
                const double d10 = D(x - 1, y), d12 = D(x + 1, y);
                const double d20 = D(x - 1, y + 1), d21 = D(x, y + 1), d22 = D(x + 1, y + 1);
                const double gx = (d02 + 2.0 * d12 + d22) - (d00 + 2.0 * d10 + d20);
                const double gy = (d20 + 2.0 * d21 + d22) - (d00 + 2.0 * d01 + d02);
                const double len = std::sqrt(gx * gx + gy * gy);
                if (!(len >= kMedialFloor * kSobelUnitSlope)) continue;
                float* p = img.rgba.data() + (static_cast<std::size_t>(y) * W + x) * 4;
                p[1] = static_cast<float>(gx / len);
                p[2] = static_cast<float>(gy / len);
            }
        }
        });
    }
    return img;
}

// One crossing of a segment with a scanline's CENTRE line: where it crosses and
// which way the edge runs there.
struct Crossing {
    double x;
    int dir;
};

// The contours, rasterised to an inside mask at pixel centres.
//
// The crossing rule is `FieldShape::distanceAt`'s, turned inside out: from "per
// pixel, walk every segment" to "per segment, emit the rows it crosses". A
// segment counts for the rows whose centre lies in
// `[min(ay, by), max(ay, by))` -- the same half-open interval the brute force's
// `ay <= py && by > py` pair describes, which is what stops a vertex sitting
// exactly on a centre line from being counted twice. Within a row, a crossing
// at exactly the pixel's own centre COUNTS, because the brute force's test is
// the strict `px < xIntersect` over the crossings to the RIGHT and the sweep
// below accumulates the ones to the LEFT. A closed contour's directions sum to
// zero, so the left-hand winding is minus the right-hand one and "non-zero"
// means the same thing measured from either side.
//
// `ox` e `oy` sao a origem do buffer JA MULTIPLICADA pelo supersample: a
// geometria nao anda, as LINHAS amostradas e que sao as absolutas
// `y + oy + 0.5`, e so o indice na mascara e deslocado (spec 2026-09-16, "O
// invariante que governa o desenho"). Com origem zero a aritmetica e a de
// antes, termo a termo.
void rasteriseContours(const std::vector<FieldContour>& contours, int ox, int oy, int w, int h,
                       FieldRule rule, double scale, std::vector<char>& inside,
                       std::size_t& insideCount) {
    inside.assign(static_cast<std::size_t>(w) * h, 0);
    insideCount = 0;

    std::vector<std::vector<Crossing>> rows(static_cast<std::size_t>(h));
    for (const FieldContour& c : contours) {
        const std::size_t n = c.xy.size() / 2;
        if (n < 2) continue;   // a contour of one point encloses nothing
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t j = (i + 1) % n;   // the closing segment is implied
            const double ax = static_cast<double>(c.xy[i * 2]) * scale;
            const double ay = static_cast<double>(c.xy[i * 2 + 1]) * scale;
            const double bx = static_cast<double>(c.xy[j * 2]) * scale;
            const double by = static_cast<double>(c.xy[j * 2 + 1]) * scale;
            if (ay == by) continue;   // a horizontal edge crosses no centre line

            const int dir = ay < by ? 1 : -1;
            const double lo = ay < by ? ay : by;
            const double hi = ay < by ? by : ay;
            int y0 = static_cast<int>(std::ceil(lo - 0.5)) - oy;
            int y1 = static_cast<int>(std::ceil(hi - 0.5)) - oy;   // exclusive
            if (y0 < 0) y0 = 0;
            if (y1 > h) y1 = h;
            const double invDy = 1.0 / (by - ay);
            for (int y = y0; y < y1; ++y) {
                const double cy = static_cast<double>(y + oy) + 0.5;
                const double t = (cy - ay) * invDy;
                rows[static_cast<std::size_t>(y)].push_back({ax + t * (bx - ax), dir});
            }
        }
    }

    // The buckets above are filled serially, so each row's crossings arrive in
    // the order they always did; from here a row only sorts and scans its own,
    // and one row per worker changes nothing but the core. The count is an
    // integer, added up per range.
    std::atomic<std::size_t> counted{0};
    parallelRanges(static_cast<std::size_t>(h), static_cast<std::size_t>(w) * h * 2,
                   [&](std::size_t r0, std::size_t r1) {
    std::size_t local = 0;
    for (int y = static_cast<int>(r0); y < static_cast<int>(r1); ++y) {
        std::vector<Crossing>& r = rows[static_cast<std::size_t>(y)];
        if (r.empty()) continue;
        std::sort(r.begin(), r.end(),
                  [](const Crossing& a, const Crossing& b) { return a.x < b.x; });
        char* row = &inside[static_cast<std::size_t>(y) * w];
        std::size_t k = 0;
        int acc = 0;
        // O `while` comeca em `k = 0` em toda linha, entao os cruzamentos a
        // ESQUERDA do buffer entram na soma antes da primeira coluna -- e a
        // paridade/winding da coluna e a mesma do render cheio.
        for (int x = 0; x < w; ++x) {
            const double px = static_cast<double>(x + ox) + 0.5;
            while (k < r.size() && r[k].x <= px) {
                acc += rule == FieldRule::NonZero ? r[k].dir : 1;
                ++k;
            }
            const bool in = rule == FieldRule::NonZero ? acc != 0 : (acc & 1) != 0;
            if (in) {
                row[x] = 1;
                ++local;
            } else if (k >= r.size()) {
                break;   // every crossing is behind us and we are out: so is the rest
            }
        }
    }
    counted += local;
    });
    insideCount = counted.load();
}

// A DISTANCIA EXATA AO CONTORNO, SEM O PRECO DA FORCA BRUTA. NOSSO.
//
// O campo que as duas transformadas acima fazem sobre uma mascara e quantizado
// na grade: ao longo de uma curva a distancia anda em degraus de texel, e a
// amostragem NEAREST mais o gradiente de quatro toques de `distanceGradient_v1`
// (PARTE UM, selada) viram esses degraus em degraus da NORMAL -- as linhas
// duras radiais que os realces mostravam nas bordas curvas. Trocar o campo pelo
// `generateField` exato as apagava (medido no Kenzu), a 3,5 s em vez de 0,35 s
// em 1024 px. O que esta aqui da o MESMO numero que `generateField` -- a mesma
// `closestOnSegment`, em double, sobre os mesmos segmentos -- sem olhar todo
// segmento em todo pixel.
//
// COMO. Os segmentos vao para uma grade uniforme de celulas, cada um em toda
// celula que atravessa. O buffer e varrido em BLOCOS de `kExactBlock` pixels de
// lado. Para o centro `P` de um bloco, de meia-diagonal `h`:
//
//   1. `dP`, a distancia exata de `P` ao contorno. O primeiro bloco da linha a
//      acha por aneis de celulas; os seguintes usam que a distancia e
//      1-Lipschitz -- `dP` esta em `[dAnt - s, dAnt + s]`, `s` o passo entre os
//      centros -- e so visitam as celulas dessa COROA.
//   2. Os candidatos: todo segmento a no maximo `dP + 2h` de `P`. Nenhum outro
//      pode ser o mais proximo de um pixel `q` do bloco: `d(q) <= dP + h`, e um
//      segmento a essa distancia de `q` esta a no maximo `dP + 2h` de `P`.
//   3. Cada pixel toma o minimo exato sobre os candidatos.
//
// A coroa e o que mantem o fundo da forma barato: um bloco a 500 px da borda
// nao visita o disco de 500 px, so o anel em que o contorno pode estar.
//
// A BANDA E O CAMPO INTEIRO. A translucidez le `sd / borderWidth` e a refracao
// le a altura do vidro; as duas sao valores do DOCUMENTO, sem teto. Com a coroa
// o fundo custa pouco, entao nao ha banda a escolher e nenhum consumidor a
// adivinhar.
//
// O SINAL NAO SAI DAQUI. Sai de `rasteriseContours`, a mesma regra de
// cruzamento semiaberto e a mesma regra de preenchimento de antes -- nao existe
// uma segunda implementacao de dentro/fora para discordar da primeira.

// Pixels por lado de um bloco: oito deixam `2h` perto de 10 px, poucos
// candidatos por pixel e uma busca de grade para cada 64 pixels.
constexpr int kExactBlock = 8;

struct ExactSegments {
    std::vector<double> xy;   // x0, y0, x1, y1 por segmento, na ordem de `FieldShape`
    double gx0 = 0.0, gy0 = 0.0, cell = 8.0;
    int nx = 1, ny = 1;
    std::vector<std::uint32_t> start;   // nx * ny + 1
    std::vector<std::uint32_t> items;

    std::size_t count() const { return xy.size() / 4; }

    // As celulas que o segmento `s` atravessa, faixa de linha a faixa de linha,
    // com folga de arredondamento para o lado de SOBRAR: uma celula a mais custa
    // um teste, uma a menos custa a exatidao.
    template <typename F>
    void forCells(std::size_t s, F&& f) const {
        const double ax = xy[s * 4], ay = xy[s * 4 + 1];
        const double bx = xy[s * 4 + 2], by = xy[s * 4 + 3];
        const double inv = 1.0 / cell;
        constexpr double eps = 1e-7;
        const double ylo = std::min(ay, by), yhi = std::max(ay, by);
        int j0 = static_cast<int>(std::floor((ylo - gy0) * inv - eps));
        int j1 = static_cast<int>(std::floor((yhi - gy0) * inv + eps));
        j0 = std::max(j0, 0);
        j1 = std::min(j1, ny - 1);
        for (int j = j0; j <= j1; ++j) {
            double xlo, xhi;
            if (ay == by) {
                xlo = std::min(ax, bx);
                xhi = std::max(ax, bx);
            } else {
                const double b0 = std::max(ylo, gy0 + j * cell);
                const double b1 = std::min(yhi, gy0 + (j + 1) * cell);
                double t0 = (b0 - ay) / (by - ay);
                double t1 = (b1 - ay) / (by - ay);
                t0 = t0 < 0.0 ? 0.0 : (t0 > 1.0 ? 1.0 : t0);
                t1 = t1 < 0.0 ? 0.0 : (t1 > 1.0 ? 1.0 : t1);
                const double x0 = ax + t0 * (bx - ax);
                const double x1 = ax + t1 * (bx - ax);
                xlo = std::min(x0, x1);
                xhi = std::max(x0, x1);
            }
            int i0 = static_cast<int>(std::floor((xlo - gx0) * inv - eps));
            int i1 = static_cast<int>(std::floor((xhi - gx0) * inv + eps));
            i0 = std::max(i0, 0);
            i1 = std::min(i1, nx - 1);
            for (int i = i0; i <= i1; ++i) f(static_cast<std::size_t>(j) * nx + i);
        }
    }

    explicit ExactSegments(const std::vector<FieldContour>& contours) {
        // A MESMA lista que `FieldShape` monta, na mesma ordem.
        for (const FieldContour& c : contours) {
            const std::size_t n = c.xy.size() / 2;
            if (n < 2) continue;
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t j = (i + 1) % n;
                xy.push_back(c.xy[i * 2]);
                xy.push_back(c.xy[i * 2 + 1]);
                xy.push_back(c.xy[j * 2]);
                xy.push_back(c.xy[j * 2 + 1]);
            }
        }
        const std::size_t n = count();
        if (n == 0) return;
        double x0 = xy[0], y0 = xy[1], x1 = xy[0], y1 = xy[1];
        for (std::size_t k = 0; k < xy.size(); k += 2) {
            x0 = std::min(x0, xy[k]);
            x1 = std::max(x1, xy[k]);
            y0 = std::min(y0, xy[k + 1]);
            y1 = std::max(y1, xy[k + 1]);
        }
        // Oito pixels por celula, e no maximo ~512 celulas por lado: o zoom
        // entrega contornos num canvas de 16k px, e uma grade de 2000 x 2000
        // custaria mais para montar do que economiza.
        const double extent = std::max(x1 - x0, y1 - y0);
        cell = std::max(8.0, extent / 512.0);
        gx0 = x0;
        gy0 = y0;
        nx = std::max(1, static_cast<int>(std::floor((x1 - x0) / cell)) + 1);
        ny = std::max(1, static_cast<int>(std::floor((y1 - y0) / cell)) + 1);
        const std::size_t cells = static_cast<std::size_t>(nx) * ny;
        start.assign(cells + 1, 0);
        for (std::size_t s = 0; s < n; ++s) forCells(s, [&](std::size_t c) { ++start[c + 1]; });
        for (std::size_t c = 0; c < cells; ++c) start[c + 1] += start[c];
        items.resize(start[cells]);
        std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
        for (std::size_t s = 0; s < n; ++s) {
            forCells(s, [&](std::size_t c) { items[fill[c]++] = static_cast<std::uint32_t>(s); });
        }
    }

    Closest closest(std::size_t s, double px, double py) const {
        return closestOnSegment(px, py, xy[s * 4], xy[s * 4 + 1], xy[s * 4 + 2], xy[s * 4 + 3]);
    }

    // Toda celula que pode ter um ponto a distancia em `[rIn, rOut]` de `P`: as
    // que tocam o disco de `rOut`, menos as que cabem INTEIRAS no disco de
    // `rIn`. Linha de celulas a linha, com os dois intervalos em x fechados.
    template <typename F>
    void forAnnulus(double px, double py, double rIn, double rOut, F&& f) const {
        const double inv = 1.0 / cell;
        int j0 = static_cast<int>(std::floor((py - rOut - gy0) * inv));
        int j1 = static_cast<int>(std::floor((py + rOut - gy0) * inv));
        j0 = std::max(j0, 0);
        j1 = std::min(j1, ny - 1);
        for (int j = j0; j <= j1; ++j) {
            const double ya = gy0 + j * cell;
            const double yb = ya + cell;
            const double dyMin = py < ya ? ya - py : (py > yb ? py - yb : 0.0);
            if (dyMin > rOut) continue;
            const double so = std::sqrt(rOut * rOut - dyMin * dyMin);
            int i0 = static_cast<int>(std::floor((px - so - gx0) * inv));
            int i1 = static_cast<int>(std::floor((px + so - gx0) * inv));
            i0 = std::max(i0, 0);
            i1 = std::min(i1, nx - 1);
            int k0 = 1, k1 = 0;   // o buraco; vazio quando `rIn` nao alcanca a linha
            const double dyMax = std::max(std::fabs(py - ya), std::fabs(py - yb));
            if (rIn > dyMax) {
                const double si = std::sqrt(rIn * rIn - dyMax * dyMax);
                k0 = static_cast<int>(std::ceil((px - si - gx0) * inv));
                k1 = static_cast<int>(std::floor((px + si - gx0) * inv)) - 1;
            }
            const std::size_t row = static_cast<std::size_t>(j) * nx;
            for (int i = i0; i <= i1; ++i) {
                if (i >= k0 && i <= k1) {
                    i = k1;
                    continue;
                }
                f(row + static_cast<std::size_t>(i));
            }
        }
    }

    // A distancia exata de `P` por aneis de celulas: o anel `r` so e visitado
    // enquanto o quadrado ja coberto nao garante que o melhor achado ganha de
    // tudo o que esta fora dele.
    double nearest(double px, double py) const {
        const double inv = 1.0 / cell;
        const int ci = std::clamp(static_cast<int>(std::floor((px - gx0) * inv)), 0, nx - 1);
        const int cj = std::clamp(static_cast<int>(std::floor((py - gy0) * inv)), 0, ny - 1);
        double best = -1.0;
        const int rMax = std::max({ci, cj, nx - 1 - ci, ny - 1 - cj});
        for (int r = 0; r <= rMax; ++r) {
            if (best >= 0.0 && r >= 1) {
                // Tudo fora do quadrado de `r - 1` celulas em volta esta pelo
                // menos a isto de `P` (negativo quando `P` cai fora dele).
                const double left = gx0 + (ci - (r - 1)) * cell;
                const double right = gx0 + (ci + r) * cell;
                const double top = gy0 + (cj - (r - 1)) * cell;
                const double bottom = gy0 + (cj + r) * cell;
                const double lb = std::min({px - left, right - px, py - top, bottom - py});
                if (lb > 0.0 && lb * lb > best) break;
            }
            for (int j = cj - r; j <= cj + r; ++j) {
                if (j < 0 || j >= ny) continue;
                const int step = (r == 0 || j == cj - r || j == cj + r) ? 1 : 2 * r;
                for (int i = ci - r; i <= ci + r; i += step) {
                    if (i < 0 || i >= nx) continue;
                    const std::size_t c = static_cast<std::size_t>(j) * nx + i;
                    for (std::uint32_t k = start[c]; k < start[c + 1]; ++k) {
                        const double d2 = closest(items[k], px, py).d2;
                        if (best < 0.0 || d2 < best) best = d2;
                    }
                }
            }
        }
        return best < 0.0 ? 0.0 : std::sqrt(best);
    }
};

// O campo exato sobre o buffer `W x H` de origem `ox, oy` (em pixels do campo),
// assinado pela mascara `inside`, uma entrada por pixel do buffer.
FieldImage exactFieldFromContours(const std::vector<FieldContour>& contours,
                                  const std::vector<char>& inside, int W, int H, int ox,
                                  int oy, const FieldOptions& options) {
    FieldImage img;
    const ExactSegments g(contours);
    if (g.count() == 0) return img;
    // So para o pixel cujo centro cai EXATAMENTE sobre o contorno, onde
    // `p - pe` e o vetor nulo: la `generateField` faz uma diferenca central de
    // passo 0,5, e aqui se faz a mesma, pela mesma funcao.
    const FieldShape shape(contours);

    img.width = static_cast<std::uint32_t>(W);
    img.height = static_cast<std::uint32_t>(H);
    img.rgba.assign(static_cast<std::size_t>(W) * H * 4, 0.0f);
    const double aa = options.aaWidth > 0.0f ? static_cast<double>(options.aaWidth) : 1.0;

    const int bw = (W + kExactBlock - 1) / kExactBlock;
    const int bh = (H + kExactBlock - 1) / kExactBlock;
    const std::size_t nseg = g.count();

    // Uma LINHA DE BLOCOS por item. Cada pixel escreve so os proprios quatro
    // floats e le a grade e a mascara, que ninguem mais escreve, entao a divisao
    // entre nucleos nao move um bit. As linhas vao INTERCALADAS
    // (`i -> i * passo mod bh`, passo primo com `bh`) porque as do meio da forma
    // custam mais que as da borda, e fatias contiguas deixariam um nucleo com
    // todas as caras.
    int stride = std::max(1, bh / static_cast<int>(parallelThreadCount()));
    while (std::gcd(stride, bh) != 1) ++stride;
    parallelRanges(static_cast<std::size_t>(bh), static_cast<std::size_t>(W) * H * 64,
                   [&](std::size_t r0, std::size_t r1) {
    std::vector<std::uint32_t> stamp(nseg, 0);
    std::uint32_t tick = 0;
    std::vector<std::uint32_t> cand;
    std::vector<std::pair<double, std::uint32_t>> ring;
    for (std::size_t r = r0; r < r1; ++r) {
        const int by = static_cast<int>((r * static_cast<std::size_t>(stride)) %
                                        static_cast<std::size_t>(bh));
        const int y0 = by * kExactBlock;
        const int y1 = std::min(H, y0 + kExactBlock);
        double prevD = -1.0, prevX = 0.0, prevY = 0.0;
        for (int bx = 0; bx < bw; ++bx) {
            const int x0 = bx * kExactBlock;
            const int x1 = std::min(W, x0 + kExactBlock);
            // Centros ABSOLUTOS, `(x + origem) + 0,5` (spec 2026-09-16).
            const double cx0 = static_cast<double>(x0 + ox) + 0.5;
            const double cx1 = static_cast<double>(x1 - 1 + ox) + 0.5;
            const double cy0 = static_cast<double>(y0 + oy) + 0.5;
            const double cy1 = static_cast<double>(y1 - 1 + oy) + 0.5;
            const double px = 0.5 * (cx0 + cx1);
            const double py = 0.5 * (cy0 + cy1);
            const double h = 0.5 * std::hypot(cx1 - cx0, cy1 - cy0);
            // Folga de arredondamento, sempre para o lado de visitar MAIS.
            const double slack = 1e-6 * (1.0 + std::fabs(px) + std::fabs(py));

            double best = -1.0;
            const auto collect = [&](double rIn, double rOut) {
                if (++tick == 0) {
                    std::fill(stamp.begin(), stamp.end(), 0u);
                    tick = 1;
                }
                ring.clear();
                best = -1.0;
                g.forAnnulus(px, py, std::max(0.0, rIn - slack), rOut + slack,
                             [&](std::size_t c) {
                    for (std::uint32_t k = g.start[c]; k < g.start[c + 1]; ++k) {
                        const std::uint32_t s = g.items[k];
                        if (stamp[s] == tick) continue;
                        stamp[s] = tick;
                        const double d2 = g.closest(s, px, py).d2;
                        ring.emplace_back(d2, s);
                        if (best < 0.0 || d2 < best) best = d2;
                    }
                });
            };
            if (prevD >= 0.0) {
                const double step = std::hypot(px - prevX, py - prevY);
                collect(prevD - step, prevD + step + 2.0 * h);
            }
            if (best < 0.0) {
                // O primeiro bloco da linha -- e, por garantia, qualquer coroa
                // que tenha voltado vazia, o que a conta acima nao permite.
                const double dN = g.nearest(px, py);
                collect(dN, dN + 2.0 * h);
            }
            const double dP = std::sqrt(best < 0.0 ? 0.0 : best);
            prevD = dP;
            prevX = px;
            prevY = py;
            const double lim = dP + 2.0 * h + slack;
            const double lim2 = lim * lim;
            cand.clear();
            for (const auto& e : ring) {
                if (e.first <= lim2) cand.push_back(e.second);
            }
            // A ordem dos segmentos e a da forca bruta, para que um empate
            // escolha o mesmo pe que ela.
            std::sort(cand.begin(), cand.end());

            for (int y = y0; y < y1; ++y) {
                const double qy = static_cast<double>(y + oy) + 0.5;
                for (int x = x0; x < x1; ++x) {
                    const double qx = static_cast<double>(x + ox) + 0.5;
                    double b2 = -1.0, fx = 0.0, fy = 0.0;
                    for (const std::uint32_t s : cand) {
                        const Closest c = g.closest(s, qx, qy);
                        if (b2 < 0.0 || c.d2 < b2) {
                            b2 = c.d2;
                            fx = c.cx;
                            fy = c.cy;
                        }
                    }
                    const std::size_t t = static_cast<std::size_t>(y) * W + x;
                    const bool in = inside[t] != 0;
                    const double sign = in ? -1.0 : 1.0;
                    double d = std::sqrt(b2 < 0.0 ? 0.0 : b2);
                    double gx = 0.0, gy = 0.0;
                    if (d > 0.0) {
                        // `grad d = sign * unit(p - pe)`, a derivada analitica:
                        // unitaria em todo lugar, e no eixo medial so escolhe um
                        // dos dois pes.
                        gx = sign * (qx - fx) / d;
                        gy = sign * (qy - fy) / d;
                    } else {
                        const float fqx = static_cast<float>(qx), fqy = static_cast<float>(qy);
                        const double e = 0.5;
                        gx = static_cast<double>(shape.distanceAt(static_cast<float>(qx + e), fqy, options.rule)) -
                             shape.distanceAt(static_cast<float>(qx - e), fqy, options.rule);
                        gy = static_cast<double>(shape.distanceAt(fqx, static_cast<float>(qy + e), options.rule)) -
                             shape.distanceAt(fqx, static_cast<float>(qy - e), options.rule);
                    }
                    if (in) {
                        // As quatro linhas virtuais de fora, uma alem de cada
                        // borda do buffer: a mesma regra e as mesmas contas de
                        // `fieldFromInsideMask`.
                        const double bx0 = static_cast<double>(x) + 0.5;
                        const double by0 = static_cast<double>(y) + 0.5;
                        const double bx1 = static_cast<double>(W) - bx0;
                        const double by1 = static_cast<double>(H) - by0;
                        if (bx0 < d) { d = bx0; gx = -1.0; gy = 0.0; }
                        if (by0 < d) { d = by0; gx = 0.0; gy = -1.0; }
                        if (bx1 < d) { d = bx1; gx = 1.0; gy = 0.0; }
                        if (by1 < d) { d = by1; gx = 0.0; gy = 1.0; }
                    }
                    float* p = img.rgba.data() + t * 4;
                    p[0] = static_cast<float>(sign * d);
                    const double len = std::sqrt(gx * gx + gy * gy);
                    if (len > 0.0) {
                        p[1] = static_cast<float>(gx / len);
                        p[2] = static_cast<float>(gy / len);
                    }
                    const double cov = -static_cast<double>(p[0]) / aa + 0.5;
                    p[3] = static_cast<float>(cov < 0.0 ? 0.0 : (cov > 1.0 ? 1.0 : cov));
                }
            }
        }
    }
    });
    return img;
}

}  // namespace

FieldImage generateFieldFromAlpha(const std::vector<float>& rgba, std::uint32_t width,
                                  std::uint32_t height, FieldOptions options) {
    FieldImage img;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || rgba.size() < texels * 4) return img;

    std::vector<char> inside(texels, 0);
    std::size_t insideCount = 0;
    // `[INF]` The alpha is ALSO the coverage, and that is not an assumption
    // invented here: it is the same identity `CoveragePass` writes, where the
    // fragment's output IS the signed area a pixel is covered by. The CLASS
    // still comes from the raw `alpha >= 0.5` rule and nothing about the
    // silhouette moves; the coverage only says WHERE inside the texel the
    // surface runs.
    std::vector<float> coverage(texels, 0.0f);
    std::atomic<std::size_t> counted{0};
    parallelRanges(height, texels * 2, [&](std::size_t y0, std::size_t y1) {
        std::size_t local = 0;
        for (std::size_t t = y0 * width; t < y1 * width; ++t) {
            const float a = rgba[t * 4 + 3];
            coverage[t] = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
            if (a >= 0.5f) {
                inside[t] = 1;
                ++local;
            }
        }
        counted += local;
    });
    insideCount = counted.load();
    // No inside means no contour, and a field with no contour is not a field
    // whose every pixel is outside -- it is no answer at all. Say so by being
    // empty, so the caller names a gap instead of drawing nothing quietly.
    if (insideCount == 0) return img;

    // No caminho do alpha o raster JA chega em coordenada do buffer -- quem o
    // colocou foi `placeRaster`, que amostrou absoluto --, entao aqui so a
    // origem e copiada para o eco.
    FieldImage made = fieldFromInsideMask(inside, coverage, static_cast<int>(width),
                                          static_cast<int>(height), 1, options);
    made.originX = options.originX;
    made.originY = options.originY;
    return made;
}

std::size_t fieldInsideMask(const std::vector<FieldContour>& contours, std::uint32_t width,
                            std::uint32_t height, const FieldOptions& options,
                            std::uint32_t superSample, std::vector<char>& inside) {
    inside.clear();
    if (width == 0 || height == 0) return 0;
    // As mesmas linhas da primeira metade de `generateFieldFromContours`, abaixo.
    int ss = superSample < 1 ? 1 : static_cast<int>(superSample);
    if ((ss & 1) == 0) --ss;
    if (ss < 1) ss = 1;
    const int W = static_cast<int>(width);
    const int H = static_cast<int>(height);
    const int mw = W * ss;
    const int mh = H * ss;
    std::vector<char> fine;
    std::size_t insideCount = 0;
    rasteriseContours(contours, options.originX * ss, options.originY * ss, mw, mh, options.rule,
                      static_cast<double>(ss), fine, insideCount);
    if (insideCount == 0) return 0;
    if (ss == 1) {
        inside = std::move(fine);
        return insideCount;
    }
    inside.assign(static_cast<std::size_t>(W) * H, 0);
    const int half = ss / 2;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            inside[static_cast<std::size_t>(y) * W + x] =
                fine[static_cast<std::size_t>(y * ss + half) * mw + (x * ss + half)];
        }
    }
    // A contagem que `generateFieldFromContours` testa e a da grade FINA.
    return insideCount;
}

FieldImage generateFieldFromContours(const std::vector<FieldContour>& contours,
                                     std::uint32_t width, std::uint32_t height,
                                     FieldOptions options, std::uint32_t superSample) {
    FieldImage img;
    if (width == 0 || height == 0) return img;
    // Odd only: see the header. An even factor has no sub-texel centred on the
    // pixel centre.
    int ss = superSample < 1 ? 1 : static_cast<int>(superSample);
    if ((ss & 1) == 0) --ss;
    if (ss < 1) ss = 1;

    const int W = static_cast<int>(width);
    const int H = static_cast<int>(height);
    const int mw = W * ss;
    const int mh = H * ss;

    // O SINAL e a mascara de sempre: a mesma rasterizacao, na grade `ss` vezes
    // mais fina, lida no sub-texel cujo centro E o centro do pixel. A origem
    // entra ja multiplicada, porque a mascara e a grade fina.
    std::vector<char> fine;
    std::size_t insideCount = 0;
    rasteriseContours(contours, options.originX * ss, options.originY * ss, mw, mh, options.rule,
                      static_cast<double>(ss), fine, insideCount);
    // A contour set that covers no sample point has no inside to sign: an empty
    // field, so the caller names the gap rather than drawing nothing in silence.
    if (insideCount == 0) return img;

    std::vector<char> inside;
    if (ss == 1) {
        inside = std::move(fine);
    } else {
        inside.assign(static_cast<std::size_t>(W) * H, 0);
        const int half = ss / 2;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                inside[static_cast<std::size_t>(y) * W + x] =
                    fine[static_cast<std::size_t>(y * ss + half) * mw + (x * ss + half)];
            }
        }
    }

    FieldImage made =
        exactFieldFromContours(contours, inside, W, H, options.originX, options.originY, options);
    made.originX = options.originX;
    made.originY = options.originY;
    return made;
}

std::size_t fieldCoverageFromContours(const std::vector<FieldContour>& contours,
                                      std::uint32_t width, std::uint32_t height,
                                      const FieldOptions& options, std::uint32_t samples,
                                      std::vector<float>& coverage) {
    coverage.clear();
    if (width == 0 || height == 0) return 0;
    const int ss = samples < 1 ? 1 : static_cast<int>(samples);
    const int W = static_cast<int>(width);
    const int H = static_cast<int>(height);
    const int mw = W * ss;
    const int mh = H * ss;
    // The same call `fieldInsideMask` makes: the origin enters already
    // multiplied, because the mask IS the fine grid, and the sampled lines stay
    // absolute -- a viewport's coverage is the full render's, pixel for pixel.
    std::vector<char> fine;
    std::size_t insideCount = 0;
    rasteriseContours(contours, options.originX * ss, options.originY * ss, mw, mh, options.rule,
                      static_cast<double>(ss), fine, insideCount);
    if (insideCount == 0) return 0;

    coverage.assign(static_cast<std::size_t>(W) * H, 0.0f);
    const float each = 1.0f / static_cast<float>(ss * ss);
    parallelRanges(static_cast<std::size_t>(H), static_cast<std::size_t>(mw) * mh,
                   [&](std::size_t y0, std::size_t y1) {
        for (int y = static_cast<int>(y0); y < static_cast<int>(y1); ++y) {
            for (int x = 0; x < W; ++x) {
                int inside = 0;
                for (int j = 0; j < ss; ++j) {
                    const char* row = &fine[static_cast<std::size_t>(y * ss + j) * mw +
                                            static_cast<std::size_t>(x) * ss];
                    for (int i = 0; i < ss; ++i) inside += row[i];
                }
                // An integer count times one constant: the same float on every
                // machine, and exactly 1 when every sample is inside.
                coverage[static_cast<std::size_t>(y) * W + x] =
                    inside == ss * ss ? 1.0f : static_cast<float>(inside) * each;
            }
        }
    });
    return insideCount;
}

FieldImage stackFields(const FieldImage& lower, const FieldImage& upper, float reach,
                       bool advanced) {
    if (lower.width == 0) return upper;
    if (upper.width == 0) return lower;
    if (lower.width != upper.width || lower.height != upper.height ||
        lower.originX != upper.originX || lower.originY != upper.originY) {
        return FieldImage{};
    }
    FieldImage out = lower;
    const float edge = reach + 1.0f;
    const std::size_t texels = static_cast<std::size_t>(out.width) * out.height;
    // A select per texel and nothing else: no float is computed, so the result
    // is one of the two inputs bit for bit.
    parallelRanges(out.height, texels * 4, [&](std::size_t y0, std::size_t y1) {
        for (std::size_t t = y0 * out.width; t < y1 * out.width; ++t) {
            const float* u = &upper.rgba[t * 4];
            float* o = &out.rgba[t * 4];
            if (!fieldStackTakesUpper(u[0], o[0], edge, advanced)) continue;
            for (int k = 0; k < 4; ++k) o[k] = u[k];
        }
    });
    return out;
}

}  // namespace rb
