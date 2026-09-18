#include "Source/RenderBox/SvgFilter.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace rb {
namespace {

using icf::svg::SvgDocument;

// `[BIN]` `drawFeGaussianBlur` (0x23D20) clamps each component of
// `stdDeviation` against the double at `0x4059000000000000` before anything
// else. That constant is 100.
constexpr double kStdDeviationCeiling = 100.0;

bool number(const std::string& text, double& out) {
    if (text.empty()) return false;
    try {
        std::size_t used = 0;
        const double v = std::stod(text, &used);
        while (used < text.size() && (text[used] == ' ' || text[used] == '\t')) ++used;
        if (used != text.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

// `stdDeviation` is one number or two, space or comma separated -- SVG's own
// grammar for a `<number-optional-number>`.
bool twoNumbers(const std::string& text, double& x, double& y) {
    std::string a, b;
    std::string* cur = &a;
    for (char c : text) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!a.empty() && cur == &a) cur = &b;
            continue;
        }
        cur->push_back(c);
    }
    if (a.empty()) return false;
    if (!number(a, x)) return false;
    if (b.empty()) {
        y = x;
        return true;
    }
    return number(b, y);
}

// A separable Gaussian. Straight RGBA in, straight RGBA out -- and the blur runs
// on PREMULTIPLIED values, because averaging straight colour pulls the colour of
// transparent pixels into visible ones. That is the same defect the raster
// sampler had to fix, and it shows exactly where an edge meets transparency.
std::vector<float> gaussian(const std::vector<float>& src, std::uint32_t w, std::uint32_t h,
                            double sigma) {
    if (sigma <= 0.0) return src;
    const std::size_t texels = static_cast<std::size_t>(w) * h;
    std::vector<float> pre(texels * 4);
    for (std::size_t t = 0; t < texels; ++t) {
        const float a = src[t * 4 + 3];
        pre[t * 4 + 0] = src[t * 4 + 0] * a;
        pre[t * 4 + 1] = src[t * 4 + 1] * a;
        pre[t * 4 + 2] = src[t * 4 + 2] * a;
        pre[t * 4 + 3] = a;
    }

    // Three sigmas each side holds 99.7% of the weight; past that the taps are
    // below the precision of an 8-bit output anyway.
    const int radius = static_cast<int>(std::ceil(sigma * 3.0));
    std::vector<double> kernel(static_cast<std::size_t>(radius) * 2 + 1);
    double sum = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double v = std::exp(-(static_cast<double>(i) * i) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (double& k : kernel) k /= sum;

    std::vector<float> tmp(texels * 4, 0.0f);
    // Horizontal, then vertical. The edge is CLAMPED rather than wrapped or
    // taken as black: a blur that pulled black in from outside would darken
    // every edge of the canvas.
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int sx = std::clamp(static_cast<int>(x) + i, 0, static_cast<int>(w) - 1);
                const double k = kernel[static_cast<std::size_t>(i + radius)];
                const float* p = &pre[(static_cast<std::size_t>(y) * w + sx) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &tmp[(static_cast<std::size_t>(y) * w + x) * 4];
            for (int c = 0; c < 4; ++c) o[c] = static_cast<float>(acc[c]);
        }
    }
    std::vector<float> out(texels * 4, 0.0f);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (int i = -radius; i <= radius; ++i) {
                const int sy = std::clamp(static_cast<int>(y) + i, 0, static_cast<int>(h) - 1);
                const double k = kernel[static_cast<std::size_t>(i + radius)];
                const float* p = &tmp[(static_cast<std::size_t>(sy) * w + x) * 4];
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * k;
            }
            float* o = &out[(static_cast<std::size_t>(y) * w + x) * 4];
            const double a = acc[3];
            o[3] = static_cast<float>(a);
            // Back to straight. A pixel the blur left fully transparent has no
            // colour to recover, and dividing by its zero alpha would invent one.
            for (int c = 0; c < 3; ++c) {
                o[c] = a > 0.0 ? static_cast<float>(acc[c] / a) : 0.0f;
            }
        }
    }
    return out;
}

// Source-over, straight in and straight out. `feBlend mode="normal"` is exactly
// this, and it is the only mode the corpus's one chain uses.
std::vector<float> sourceOver(const std::vector<float>& top, const std::vector<float>& bottom,
                              std::size_t texels) {
    std::vector<float> out(texels * 4, 0.0f);
    for (std::size_t t = 0; t < texels; ++t) {
        const float sa = top[t * 4 + 3], da = bottom[t * 4 + 3];
        const float a = sa + da * (1.0f - sa);
        out[t * 4 + 3] = a;
        for (int c = 0; c < 3; ++c) {
            const float s = top[t * 4 + c] * sa;
            const float d = bottom[t * 4 + c] * da * (1.0f - sa);
            out[t * 4 + c] = a > 0.0f ? (s + d) / a : 0.0f;
        }
    }
    return out;
}

bool parseFloodColour(const std::string& text, float rgb[3]) {
    // `flood-color` is never named in the corpus's chains -- the default is
    // black -- so what has to work is the ABSENT case. A hex value is read
    // because refusing one that is present would be a limit with no reason.
    if (text.empty()) {
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        return true;
    }
    if (text[0] != '#' || (text.size() != 7 && text.size() != 4)) return false;
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (int i = 0; i < 3; ++i) {
        int hi, lo;
        if (text.size() == 7) {
            hi = hex(text[1 + i * 2]);
            lo = hex(text[2 + i * 2]);
        } else {
            hi = lo = hex(text[1 + i]);
        }
        if (hi < 0 || lo < 0) return false;
        rgb[i] = static_cast<float>(hi * 16 + lo) / 255.0f;
    }
    return true;
}

}  // namespace

FilteredGroup applySvgFilter(const SvgDocument::Filter& filter,
                             const std::vector<float>& source,
                             std::uint32_t width, std::uint32_t height,
                             const PathGlobals& placement, std::int32_t originX,
                             std::int32_t originY) {
    FilteredGroup out;
    const std::size_t texels = static_cast<std::size_t>(width) * height;

    // `[BIN]` `inputImage` (0x2520C): an absent `in` takes the PREVIOUS result,
    // a named one is looked up, and a name nothing produced yields null. A null
    // input makes its primitive return null (`drawFeBlend` 0x24BFC,
    // `drawFeComposite` 0x240C8), and a null final result reaches
    // `SVGFilter::draw`'s cleanup path (0x29A34 -> 0x29E58), which draws
    // nothing. `hasValue` is that null, carried explicitly.
    struct Slot {
        std::vector<float> rgba;
        bool hasValue = false;
    };
    std::map<std::string, Slot> named;

    Slot sourceGraphic;
    sourceGraphic.rgba = source;
    sourceGraphic.hasValue = true;
    named["SourceGraphic"] = sourceGraphic;

    // `SourceAlpha` is the source with its colour thrown away -- black at the
    // source's own alpha.
    {
        Slot a;
        a.rgba.assign(texels * 4, 0.0f);
        for (std::size_t t = 0; t < texels; ++t) a.rgba[t * 4 + 3] = source[t * 4 + 3];
        a.hasValue = true;
        named["SourceAlpha"] = a;
    }
    Slot previous = sourceGraphic;

    auto inputFor = [&](const SvgDocument::FilterPrimitive& p, const char* key) -> Slot {
        const std::string name = p.attribute(key);
        if (name.empty()) return previous;
        auto it = named.find(name);
        if (it == named.end()) return Slot{};  // null, exactly as the target
        return it->second;
    };

    bool ranBlur = false;
    for (const auto& p : filter.primitives) {
        Slot result;
        if (p.name == "feFlood") {
            float rgb[3];
            if (!parseFloodColour(p.attribute("flood-color"), rgb)) {
                out.why = "feFlood com flood-color que este leitor nao le: " +
                          p.attribute("flood-color");
                return out;
            }
            double opacity = 1.0;
            const std::string o = p.attribute("flood-opacity");
            if (!o.empty() && !number(o, opacity)) {
                out.why = "feFlood com flood-opacity que nao e um numero: " + o;
                return out;
            }
            result.rgba.assign(texels * 4, 0.0f);
            for (std::size_t t = 0; t < texels; ++t) {
                result.rgba[t * 4 + 0] = rgb[0];
                result.rgba[t * 4 + 1] = rgb[1];
                result.rgba[t * 4 + 2] = rgb[2];
                result.rgba[t * 4 + 3] = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
            }
            result.hasValue = true;
        } else if (p.name == "feBlend") {
            const std::string mode = p.attribute("mode");
            if (!mode.empty() && mode != "normal") {
                // The target blends all of them; we do not, so it is a gap and
                // says so rather than drawing `normal` and calling it done.
                out.why = "feBlend mode=" + mode + " nao transcrito";
                return out;
            }
            const Slot a = inputFor(p, "in");
            const Slot b = inputFor(p, "in2");
            if (!a.hasValue || !b.hasValue) {
                result.hasValue = false;  // null in, null out
            } else {
                result.rgba = sourceOver(a.rgba, b.rgba, texels);
                result.hasValue = true;
            }
        } else if (p.name == "feGaussianBlur") {
            const Slot a = inputFor(p, "in");
            if (!a.hasValue) {
                result.hasValue = false;
            } else {
                double sx = 0.0, sy = 0.0;
                const std::string sd = p.attribute("stdDeviation");
                if (!sd.empty() && !twoNumbers(sd, sx, sy)) {
                    out.why = "feGaussianBlur com stdDeviation que nao e um numero: " + sd;
                    return out;
                }
                // The clamp, then the isotropy check, in the target's order.
                sx = std::min(sx, kStdDeviationCeiling);
                sy = std::min(sy, kStdDeviationCeiling);
                if (sx != sy) {
                    // `[BIN]` The target LOGS "Different radii for gaussian blur
                    // not supported" and carries on with the X component. It
                    // does not refuse -- so neither does this -- but a render
                    // that took the first number and dropped the second has to
                    // say so.
                    out.notes.push_back(
                        "feGaussianBlur com stdDeviation anisotropico: o alvo usa so o "
                        "primeiro valor e registra o descarte (CoreSVG 0x23D88)");
                }
                // User units into pixels. The placement's two scales are equal
                // for a uniform fit, and the blur has ONE radius, so the X one
                // is the one that can be used without inventing an anisotropy
                // the target does not have.
                const double sigma = sx * std::abs(placement.m0[0]);
                result.rgba = gaussian(a.rgba, width, height, sigma);
                result.hasValue = true;
                if (sigma > 0.0) ranBlur = true;
            }
        } else {
            // In the target's six and not transcribed here. A gap of ours.
            out.why = "primitiva " + p.name + " nao transcrita";
            return out;
        }
        const std::string name = p.attribute("result");
        if (!name.empty()) named[name] = result;
        previous = result;
    }

    if (!previous.hasValue) {
        // The chain nulled out. The target draws nothing, so an empty canvas IS
        // the answer -- not a refusal.
        out.rgba.assign(texels * 4, 0.0f);
        out.ok = true;
        return out;
    }
    out.rgba = std::move(previous.rgba);
    out.ok = true;

    // THE FILTER REGION CUTS WHAT FALLS OUTSIDE IT. Only `userSpaceOnUse` is
    // read: SVG's default is `objectBoundingBox`, which re-scales the region by
    // each referencing element's own box, and no corpus filter uses it.
    if (filter.hasRegion && filter.userSpace) {
        const double sx = placement.m0[0], sy = placement.m1[1];
        const double ox = placement.m2[0], oy = placement.m2[1];
        const double x0 = filter.x * sx + ox, y0 = filter.y * sy + oy;
        const double x1 = (filter.x + filter.width) * sx + ox;
        const double y1 = (filter.y + filter.height) * sy + oy;
        for (std::uint32_t py = 0; py < height; ++py) {
            for (std::uint32_t px = 0; px < width; ++px) {
                const double cx =
                    static_cast<double>(static_cast<std::int64_t>(px) + originX) + 0.5;
                const double cy =
                    static_cast<double>(static_cast<std::int64_t>(py) + originY) + 0.5;
                if (cx < x0 || cx > x1 || cy < y0 || cy > y1) {
                    out.rgba[(static_cast<std::size_t>(py) * width + px) * 4 + 3] = 0.0f;
                }
            }
        }
    }

    if (ranBlur) {
        // `[OBS]`, and it announces itself on every render that provokes it: the
        // routing is measured, the kernel is not.
        out.notes.push_back(
            "feGaussianBlur: o alvo passa stdDeviation ao inputRadius do CIGaussianBlur "
            "(CoreSVG 0x23DF4) e o kernel que o CoreImage constroi dai nao foi lido -- "
            "a largura do borrao nao e afirmada igual a do alvo");
    }
    return out;
}

}  // namespace rb
