// See `DistanceField.h` for the line between the two halves of this file:
// everything above `PART TWO` is transcribed from mod95 and sealed; everything
// below it is this project's own algorithm and is not.
#include "Source/RenderBox/DistanceField.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

// The full two-dimensional transform: seeds are the texels for which
// `seed[t]` is true, and every other texel comes back with the squared
// distance to the nearest seed CENTRE plus, in `site`, that seed's linear
// index. Column pass then row pass, which is what makes the composition exact
// rather than separable-approximate: after the column pass each column holds
// the true 1D answer, and the row pass takes the lower envelope of those.
void edt2d(const std::vector<char>& seed, int w, int h, std::vector<double>& sq,
           std::vector<int>& site) {
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    const int n = std::max(w, h);
    std::vector<double> f(static_cast<std::size_t>(n));
    std::vector<double> d(static_cast<std::size_t>(n));
    std::vector<int> v(static_cast<std::size_t>(n) + 1);
    std::vector<double> z(static_cast<std::size_t>(n) + 2);
    std::vector<int> arg(static_cast<std::size_t>(n));

    sq.assign(texels, 0.0);
    site.assign(texels, -1);
    // `siteY[t]` is the row of the nearest seed within t's own column, which is
    // all the column pass can know. The row pass turns it into a full index.
    std::vector<int> siteY(texels, -1);

    for (std::size_t t = 0; t < texels; ++t) sq[t] = seed[t] ? 0.0 : kEdtInf;

    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[static_cast<std::size_t>(y)] = sq[static_cast<std::size_t>(y) * w + x];
        edtSquared1d(f, d, v, z, h, &arg);
        for (int y = 0; y < h; ++y) {
            const std::size_t t = static_cast<std::size_t>(y) * w + x;
            sq[t] = d[static_cast<std::size_t>(y)];
            siteY[t] = d[static_cast<std::size_t>(y)] >= kEdtInf ? -1 : arg[static_cast<std::size_t>(y)];
        }
    }
    for (int y = 0; y < h; ++y) {
        double* row = &sq[static_cast<std::size_t>(y) * w];
        for (int x = 0; x < w; ++x) f[static_cast<std::size_t>(x)] = row[x];
        edtSquared1d(f, d, v, z, w, &arg);
        for (int x = 0; x < w; ++x) {
            const std::size_t t = static_cast<std::size_t>(y) * w + x;
            row[x] = d[static_cast<std::size_t>(x)];
            const int sx = arg[static_cast<std::size_t>(x)];
            const int sy = siteY[static_cast<std::size_t>(y) * w + sx];
            site[t] = sy < 0 ? -1 : sy * w + sx;
        }
    }
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

FieldImage generateFieldFromAlpha(const std::vector<float>& rgba, std::uint32_t width,
                                  std::uint32_t height, FieldOptions options) {
    FieldImage img;
    const std::size_t texels = static_cast<std::size_t>(width) * height;
    if (width == 0 || height == 0 || rgba.size() < texels * 4) return img;

    const int w = static_cast<int>(width);
    const int h = static_cast<int>(height);

    std::vector<char> inside(texels, 0);
    std::size_t insideCount = 0;
    for (std::size_t t = 0; t < texels; ++t) {
        if (rgba[t * 4 + 3] >= 0.5f) {
            inside[t] = 1;
            ++insideCount;
        }
    }
    // No inside means no contour, and a field with no contour is not a field
    // whose every pixel is outside -- it is no answer at all. Say so by being
    // empty, so the caller names a gap instead of drawing nothing quietly.
    if (insideCount == 0) return img;

    std::vector<char> outsideSeed(texels, 0);
    for (std::size_t t = 0; t < texels; ++t) outsideSeed[t] = inside[t] ? 0 : 1;

    // Distance from every texel to the nearest OUTSIDE centre (what an inside
    // texel needs) and to the nearest INSIDE centre (what an outside texel
    // needs). Two transforms and not one, because a signed field wants the
    // depth on each side measured to the other side's seeds.
    std::vector<double> sqOut, sqIn;
    std::vector<int> siteOut, siteIn;
    edt2d(outsideSeed, w, h, sqOut, siteOut);
    edt2d(inside, w, h, sqIn, siteIn);

    img.width = width;
    img.height = height;
    img.rgba.assign(texels * 4, 0.0f);

    const double aa = options.aaWidth > 0.0f ? static_cast<double>(options.aaWidth) : 1.0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t t = static_cast<std::size_t>(y) * w + x;
            double dist = 0.0;
            double gx = 0.0;
            double gy = 0.0;
            double sign = 0.0;

            if (inside[t]) {
                sign = -1.0;
                dist = std::sqrt(sqOut[t]);
                const int s = siteOut[t];
                if (s >= 0) {
                    gx = static_cast<double>(s % w) - x;
                    gy = static_cast<double>(s / w) - y;
                }
                // The four virtual outside rows one step beyond each edge. When
                // one of them is nearer than any real outside texel it wins,
                // and the gradient turns to face it -- axis aligned, because
                // the row is.
                const double bx0 = static_cast<double>(x) + 1.0;
                const double by0 = static_cast<double>(y) + 1.0;
                const double bx1 = static_cast<double>(w - x);
                const double by1 = static_cast<double>(h - y);
                if (bx0 < dist) { dist = bx0; gx = -1.0; gy = 0.0; }
                if (by0 < dist) { dist = by0; gx = 0.0; gy = -1.0; }
                if (bx1 < dist) { dist = bx1; gx = 1.0; gy = 0.0; }
                if (by1 < dist) { dist = by1; gx = 0.0; gy = 1.0; }
            } else {
                sign = 1.0;
                dist = std::sqrt(sqIn[t]);
                const int s = siteIn[t];
                if (s >= 0) {
                    // Outside, `d` grows as the shape recedes, so the gradient
                    // points AWAY from the nearest inside centre -- the
                    // opposite sense from the inside branch, which is why the
                    // two are written out rather than shared.
                    gx = static_cast<double>(x) - (s % w);
                    gy = static_cast<double>(y) - (s / w);
                }
            }

            const double len = std::sqrt(gx * gx + gy * gy);
            float* p = img.rgba.data() + t * 4;
            // Centres to contour: the boundary sits half a pixel before the
            // first centre of the opposite class.
            p[0] = static_cast<float>(sign * (dist - 0.5));
            if (len > 0.0) {
                p[1] = static_cast<float>(gx / len);
                p[2] = static_cast<float>(gy / len);
            }
            const double cov = -static_cast<double>(p[0]) / aa + 0.5;
            p[3] = static_cast<float>(cov < 0.0 ? 0.0 : (cov > 1.0 ? 1.0 : cov));
        }
    }
    return img;
}

}  // namespace rb
