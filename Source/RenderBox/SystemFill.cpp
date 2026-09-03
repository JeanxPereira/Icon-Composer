#include "Source/RenderBox/SystemFill.h"

namespace rb {
namespace {

// `[BIN]` `IconRendering 0x3E680`, the whole builder: two stops, `r == g == b`,
// alpha 1.0, locations from the `{1.0, 0.0}` constant at `0x971F0` and a count
// of 2 from the array header at `0x938B0`. Written once here because the two
// ramps differ ONLY in the two greys handed to it -- the target has one builder
// and two call sites, and so does this.
std::vector<RampStop> buildSystemRamp(double grey0, double grey1) {
    std::vector<RampStop> stops(2);
    stops[0].rgba[0] = grey0;
    stops[0].rgba[1] = grey0;
    stops[0].rgba[2] = grey0;
    stops[0].rgba[3] = 1.0;
    stops[0].location = 0.0;
    stops[1].rgba[0] = grey1;
    stops[1].rgba[1] = grey1;
    stops[1].rgba[2] = grey1;
    stops[1].rgba[3] = 1.0;
    stops[1].location = 1.0;
    return stops;
}

}  // namespace

std::vector<RampStop> systemLightGradient() {
    // `[BIN]` `0x5E8E8`, into `ICRRenderingParameters+0x80`. 255 and 245.
    return buildSystemRamp(1.0, 0.9607843137254902);
}

std::vector<RampStop> systemDarkGradient() {
    // `[BIN]` `0x5E908`, into `ICRRenderingParameters+0x88`. 31 and 15.
    return buildSystemRamp(0.12156862745098039, 0.058823529411764705);
}

std::vector<RampStop> systemGradient(SystemFill which) {
    // `[BIN]` `0x3CF74`: the raw value is masked to a byte and compared with 1,
    // so the test is "is it dark", not "is it light". Written the same way
    // round, because a two-case enum tested the other way would silently take a
    // third value to the wrong arm.
    return which == SystemFill::Dark ? systemDarkGradient() : systemLightGradient();
}

std::vector<RampStop> rewriteStopOpacity(const std::vector<RampStop>& stops, double opacity) {
    std::vector<RampStop> out;
    out.reserve(stops.size());
    for (const RampStop& in : stops) {
        RampStop s;
        // `[BIN]` `0x3CFF4` loads r, g, b from `+0x00/+0x08/+0x10` and the
        // location from `+0x20`. The source alpha at `+0x18` is never loaded,
        // so the assignment below is the transcription and NOT a shortcut for
        // `in.rgba[3] * opacity`.
        s.rgba[0] = in.rgba[0];
        s.rgba[1] = in.rgba[1];
        s.rgba[2] = in.rgba[2];
        s.rgba[3] = opacity;
        s.location = in.location;
        out.push_back(s);
    }
    return out;
}

GradientPlacement defaultGradientPlacement() {
    // `[BIN]` `IconRendering 0x38CF4`. Three of the four doubles are stored
    // from `xzr` and the fourth from the immediate `0x3FF0000000000000`.
    GradientPlacement p;
    p.start = {0.0, 0.0};
    p.end = {0.0, 1.0};
    return p;
}

PlacementPoint placeUnitPoint(PlacementPoint unit, const PlacementRect& rect) {
    // `[BIN]` `0x1BB60`-`0x1BB74`. Each axis is scaled by its own extent and
    // offset by its own origin -- the x and y arithmetic never meet, which is
    // what makes a transposed implementation a real failure mode rather than a
    // theoretical one.
    PlacementPoint out;
    out.x = rect.x + unit.x * rect.width;
    out.y = rect.y + unit.y * rect.height;
    return out;
}

GradientAxis placeGradient(const std::optional<GradientPlacement>& placement,
                           const PlacementRect& rect) {
    // `[BIN]` `0x1BAB8`-`0x1BAE4`: the discriminator is read first and the
    // default substituted for `.none`, BEFORE the unit-to-rect mapping. The
    // order matters -- the default is in unit coordinates like any other
    // placement, not in rect coordinates, so it goes through the same mapping.
    const GradientPlacement p = placement.has_value() ? *placement : defaultGradientPlacement();
    GradientAxis axis;
    axis.start = placeUnitPoint(p.start, rect);
    axis.end = placeUnitPoint(p.end, rect);
    return axis;
}

std::optional<PlacementRect> systemFillRect(SystemFillRectSource source,
                                            const PlacementRect& boundingRect) {
    if (source == SystemFillRectSource::BoundingRect) return boundingRect;
    // `[OBS]` The chiclet-aligned rect is origin (0,0) with a `CGSize` from the
    // drawing context at `+0x48`, and which size that is was not read. There is
    // no number to return, so none is returned.
    return std::nullopt;
}

ResolvedSystemFill resolveSystemFill(SystemFill which, double opacity) {
    // `[BIN]` `0x3CE80`, in the order the function performs it: pick the ramp
    // (`0x3CF7C`), rewrite the stops (`0x3CF98`), zero the placement region
    // (`0x3CFAC`), tag it (`0x3CFB8`).
    ResolvedSystemFill out;
    out.stops = rewriteStopOpacity(systemGradient(which), opacity);
    out.placement = std::nullopt;
    return out;
}

}  // namespace rb
