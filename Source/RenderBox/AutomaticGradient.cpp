#include "Source/RenderBox/AutomaticGradient.h"

#include "Source/RenderBox/ColorSpace.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

double clamp01(double v) {
    // `[BIN]` The target does `fminnm` against 1.0 and then a max against 0.0,
    // and in the vector path the max is an `fcmge`+`and` -- which sends a NaN to
    // zero rather than propagating it. Written the same way round here so a NaN
    // component cannot leave this function alive.
    const double capped = v < 1.0 ? v : 1.0;
    return capped > 0.0 ? capped : 0.0;
}

}  // namespace

double luminance709(double r, double g, double b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

std::vector<RampStop> automaticGradient(const icf::Color& base,
                                        const AutomaticGradientParameters& p) {
    // The grey spaces carry (luminance, alpha) and the RGB spaces four
    // components; `Values.h` deliberately does not normalise one into the other,
    // so the widening happens here, where a rendering decision belongs.
    double r = base.components[0];
    double g = base.count >= 3 ? base.components[1] : base.components[0];
    double b = base.count >= 3 ? base.components[2] : base.components[0];
    const double alpha = base.count >= 3 ? base.components[3] : base.components[1];

    const double L = luminance709(r, g, b);

    // `[BIN]` The band is chosen by three constants IN THE CODE, not by any
    // parameter: `fmov #0.25`, `#0.50`, `#0.75`, compared with `ls` (<=).
    double lightening;
    if (L <= 0.25) {
        lightening = p.dimLightening;
    } else if (L <= 0.50) {
        lightening = p.midDimLightening;
    } else if (L <= 0.75) {
        lightening = p.midBrightLightening;
    } else {
        lightening = p.brightLightening;
    }

    // `[BIN]` The saturation boost pushes each channel away from the luminance:
    // `c - sb * (L - c)`.
    const double sb = p.saturationBoost;
    double boosted[3] = {r - sb * (L - r), g - sb * (L - g), b - sb * (L - b)};

    // `[BIN]` The sign of the lightening chooses what it blends toward: positive
    // lerps to white, negative is a plain multiply by (1 + lightening).
    const bool positive = lightening > 0.0;
    double shifted[3];
    for (int i = 0; i < 3; ++i) {
        const double toward = positive ? (1.0 - boosted[i]) : boosted[i];
        shifted[i] = clamp01(boosted[i] + lightening * toward);
    }

    // `[BIN]` Two stops. The SHIFTED colour anchors at 0 when the lightening is
    // positive and at 1 when it is negative, and the ORIGINAL colour takes the
    // other end, moved by `basePosition`. The alpha passes through untouched to
    // both.
    RampStop a;
    a.rgba[0] = shifted[0];
    a.rgba[1] = shifted[1];
    a.rgba[2] = shifted[2];
    a.rgba[3] = alpha;
    a.location = positive ? 0.0 : 1.0;

    RampStop c;
    c.rgba[0] = r;
    c.rgba[1] = g;
    c.rgba[2] = b;
    c.rgba[3] = alpha;
    c.location = positive ? (1.0 - p.basePosition) : (0.0 + p.basePosition);

    // `[BIN]` The target sorts ascending by location, and it has to: which stop
    // comes first flips with the sign of the lightening, so a fixed order would
    // be right for one band group and reversed for the other.
    std::vector<RampStop> stops{a, c};
    std::stable_sort(stops.begin(), stops.end(),
                     [](const RampStop& x, const RampStop& y) {
                         return x.location < y.location;
                     });
    return stops;
}

std::vector<RampStop> automaticGradientInWorkingSpace(const icf::Color& base,
                                                      const AutomaticGradientParameters& p) {
    // What `IconColor` holds: four components, gamma-encoded Display P3. A
    // Display P3 or a grey base is already those numbers, and is handed over as
    // the document's own doubles; only an sRGB base has to be converted, and the
    // conversion is a `float` one. The alpha is never a component of it.
    const bool grey = base.count < 3;
    icf::Color inP3;
    inP3.space = icf::ColorSpace::DisplayP3;
    inP3.count = 4;
    inP3.components[0] = base.components[0];
    inP3.components[1] = grey ? base.components[0] : base.components[1];
    inP3.components[2] = grey ? base.components[0] : base.components[2];
    inP3.components[3] = grey ? base.components[1] : base.components[3];
    if (base.space == icf::ColorSpace::SRGB || base.space == icf::ColorSpace::ExtendedSRGB) {
        float held[4];
        toDisplayP3(base, held);
        for (int k = 0; k < 3; ++k) inP3.components[k] = static_cast<double>(held[k]);
    }

    std::vector<RampStop> stops = automaticGradient(inP3, p);
    for (RampStop& s : stops) {
        float rgba[4];
        for (int k = 0; k < 4; ++k) rgba[k] = static_cast<float>(s.rgba[k]);
        displayP3ToSrgb(rgba);
        for (int k = 0; k < 3; ++k) s.rgba[k] = static_cast<double>(rgba[k]);
    }
    return stops;
}

}  // namespace rb
