#include "Source/RenderBox/FillResolve.h"

namespace rb {
namespace {

FillResolution refuse(const char* why) {
    FillResolution r;
    r.outcome = FillOutcome::Refused;
    r.why = why;
    return r;
}

FillResolution noFill() {
    FillResolution r;
    r.outcome = FillOutcome::NoFill;
    return r;
}

FillResolution resolved(const ResolvedFill& fill) {
    FillResolution r;
    r.outcome = FillOutcome::Resolved;
    r.fill = fill;
    return r;
}

// `.solid(color)`.
FillResolution solidOf(const icf::Color& color) {
    ResolvedFill f;
    f.contents = ResolvedFill::Contents::Solid;
    f.primary = color;
    return resolved(f);
}

// `.system(ramp, 1.0, true)` -- the two literals are the same at all four
// sites, so they live here once rather than at each call.
FillResolution systemOf(SystemFill ramp) {
    ResolvedFill f;
    f.contents = ResolvedFill::Contents::System;
    f.ramp = ramp;
    f.opacity = 1.0;
    f.thirdArgument = true;
    return resolved(f);
}

// The colour-carrying cases, shared by both converters because both read the
// same two offsets for them: `Fill+0x08` and `Fill+0x30`.
//
// The PLACEMENT is not shared and is not decided here -- it is the one thing
// the two converters disagree about for a `linear-gradient`, so it is the
// caller's argument.
FillResolution colouredCase(const icf::Fill& fill,
                            const std::optional<GradientPlacement>& gradientPlacement) {
    switch (fill.kind) {
        case icf::FillKind::Solid:
            // A `solid` with no colour cannot be read. `[OBS]` The target
            // decodes into a field, so it has no such state and there is no
            // branch to transcribe; refusing is this reader's answer to a
            // document shape the format does not produce, and `Values.h` would
            // already have refused it upstream.
            if (fill.colors.empty()) return refuse("solid names no colour");
            return solidOf(fill.colors[0]);

        case icf::FillKind::AutomaticGradient: {
            if (fill.colors.empty()) return refuse("automatic-gradient names no colour");
            ResolvedFill f;
            f.contents = ResolvedFill::Contents::AutomaticGradient;
            f.primary = fill.colors[0];
            // `[BIN]` `placement: nil` at BOTH sites -- `0x10AE74` and
            // `0x10C088`. The document's `orientation` is not read here even on
            // the layer, where the `linear-gradient` beside it is read.
            // `[ART]` 8 corpus `automatic-gradient` fills name an orientation
            // that no converter of the four reads.
            f.placement = std::nullopt;
            return resolved(f);
        }

        case icf::FillKind::LinearGradient: {
            // `[BIN]` Two colours, from two named slots: `primaryColor` at
            // `Fill+0x08` and `secondaryColor` at `Fill+0x30`. `[ART]` All 97
            // `linear-gradient` fills in the corpus carry exactly two.
            //
            // `[OBS]` How a ramp of any other length would decode into those
            // two slots was not read, and the corpus has none. So a ramp that
            // is not two colours is refused rather than truncated: taking the
            // first two would be an invented decoding wearing a reading's
            // clothes.
            if (fill.colors.size() != 2) {
                return refuse("linear-gradient is not a two-colour ramp");
            }
            ResolvedFill f;
            f.contents = ResolvedFill::Contents::Gradient;
            f.primary = fill.colors[0];
            f.secondary = fill.colors[1];
            f.placement = gradientPlacement;
            return resolved(f);
        }

        default:
            // Unreachable: both converters handle the other four cases before
            // calling this. Refusing rather than asserting keeps a future
            // eighth case a visible refusal instead of a crash.
            return refuse("not a colour-carrying fill kind");
    }
}

}  // namespace

icf::Appearance sourceAppearance(Rendition rendition) {
    // `[BIN]` The packed table `0x0000'0303'0303'0201`, byte-indexed by the
    // rendition. Written as the six cases it has rather than as a shift of the
    // constant, so that the four that collapse onto `tinted` are visible as
    // four.
    switch (rendition) {
        case Rendition::LightColor: return icf::Appearance::Light;
        case Rendition::DarkColor:  return icf::Appearance::Dark;
        case Rendition::LightTint:  return icf::Appearance::Tinted;
        case Rendition::DarkTint:   return icf::Appearance::Tinted;
        case Rendition::LightClear: return icf::Appearance::Tinted;
        case Rendition::DarkClear:  return icf::Appearance::Tinted;
    }
    // `[OBS]` The table's upper two bytes are zero, so a seventh rendition
    // would index `base`. There is no seventh case and this is not modelled as
    // one -- `Light` here is the C++ language's need for a return, not a read.
    return icf::Appearance::Light;
}

icf::Color iconColorClear() {
    icf::Color c;
    c.space = icf::ColorSpace::SRGB;  // `[OBS]` see the header: not read.
    c.count = 4;
    c.components[0] = 0.0;
    c.components[1] = 0.0;
    c.components[2] = 0.0;
    c.components[3] = 0.0;
    return c;
}

bool identical(const ResolvedFill& a, const ResolvedFill& b) {
    auto sameColor = [](const icf::Color& x, const icf::Color& y) {
        if (x.space != y.space || x.count != y.count) return false;
        for (int i = 0; i < 4; ++i) {
            if (x.components[i] != y.components[i]) return false;
        }
        return true;
    };
    auto samePlacement = [](const std::optional<GradientPlacement>& x,
                            const std::optional<GradientPlacement>& y) {
        if (x.has_value() != y.has_value()) return false;
        if (!x) return true;
        return x->start.x == y->start.x && x->start.y == y->start.y &&
               x->end.x == y->end.x && x->end.y == y->end.y;
    };
    return a.contents == b.contents && sameColor(a.primary, b.primary) &&
           sameColor(a.secondary, b.secondary) && samePlacement(a.placement, b.placement) &&
           a.ramp == b.ramp && a.opacity == b.opacity && a.thirdArgument == b.thirdArgument;
}

FillResolution backgroundFillFrom(const icf::Fill& fill, icf::Appearance appearance) {
    switch (fill.kind) {
        // `[BIN]` `none` at `0x10AEE8` is a `cbz` into `0x10AEF4` -- the same
        // block `automatic` reaches from `0x10AEF0`. Not a similar block: the
        // same one. So the two cases fall through to each other here, and the
        // absence of a separate `none` arm is the transcription.
        case icf::FillKind::None:
        case icf::FillKind::Automatic:
            // `[BIN]` `0x10AEF4`: `cmp w8, #2` with an UNSIGNED `b.lo`, so
            // `base` and `light` share an arm because the compare covers both,
            // and `tinted` is whatever the two branches did not take.
            switch (appearance) {
                case icf::Appearance::Base:
                case icf::Appearance::Light:
                    return systemOf(SystemFill::Light);
                case icf::Appearance::Dark:
                    return systemOf(SystemFill::Dark);
                case icf::Appearance::Tinted:
                    // The arm nobody predicted. Not a grey, not a ramp: a solid
                    // of `IconColor.clear`. A background that is entirely
                    // transparent, produced by the same case that produces the
                    // chiclet ramp two lines up.
                    return solidOf(iconColorClear());
            }
            return refuse("unknown appearance");

        // `[BIN]` `0x10AF14` and `0x10AF10`. Note the pairing is NOT by the
        // appearance being rendered: `system-light` names the light ramp under
        // every appearance, including `dark`. The document is choosing a ramp,
        // not describing a condition.
        case icf::FillKind::SystemLight: return systemOf(SystemFill::Light);
        case icf::FillKind::SystemDark:  return systemOf(SystemFill::Dark);

        case icf::FillKind::Solid:
        case icf::FillKind::AutomaticGradient:
        case icf::FillKind::LinearGradient:
            // `[BIN]` The background converter never touches `orientation`
            // (`Fill+0x58`) -- for `linear-gradient` at `0x10AFAC` as much as
            // for `automatic-gradient` at `0x10AE74`. The nil is passed here as
            // a literal, not derived from `fill.orientation` being absent,
            // because it is passed even when the document names one.
            return colouredCase(fill, std::nullopt);
    }
    return refuse("unknown fill kind");
}

FillResolution resolveBackgroundFill(const icf::json::Value& root, icf::Context ctx) {
    // Through `icf::resolve`, never through `find`. That is what makes the bare
    // `fill` and the `fill-specializations` list one code path and what makes
    // the appearance predicate apply.
    const icf::json::Value* node = icf::resolve(root, "fill", ctx);
    if (!node) return noFill();
    auto parsed = icf::fillFrom(*node);
    if (!parsed) return refuse("unreadable fill");
    return backgroundFillFrom(*parsed, ctx.appearance);
}

FillResolution layerFillFrom(const icf::Fill& fill, icf::Appearance appearance,
                             const icf::Fill* lightSlotFill) {
    switch (fill.kind) {
        // `[BIN]` `0x10C0F0` jumps to `0x10C15C`, which is the nil. Here is the
        // whole asymmetry of this file in two adjacent functions: the same
        // document word takes the system path on the background and answers
        // nothing on a layer.
        case icf::FillKind::None:
            return noFill();

        case icf::FillKind::Automatic:
            // `[BIN]` `0x10C0FC`: under the `light` slot, `automatic` is nil.
            // This is also the terminator -- the light-slot pass at `0x10BE20`
            // maps `none` and `automatic` to nil, so the inheritance below can
            // recurse exactly once and stop.
            if (appearance == icf::Appearance::Light) return noFill();

            // `[OBS]` A dead `cmp w8, #2` sits at `0x10C11C` with nothing
            // consuming it. `base`, `dark` and `tinted` all reach the copy at
            // `0x10C138`; the tail merge left the compare behind. There is no
            // fourth arm and none is written.
            //
            // `[INF]` A layer whose light slot names no fill has nothing to
            // copy. The target's second resolution would hand the converter a
            // nil `Layer.fill`, and every path that reaches this converter with
            // no fill answers nil, so nil is the answer here too.
            if (!lightSlotFill) return noFill();

            // `[BIN]` "A verbatim copy of that buffer" -- the light slot's
            // ANSWER, not its document value re-interpreted under the current
            // appearance. So the recursion passes `light`, and passes no light
            // slot of its own because `automatic` under `light` returns before
            // reading one.
            return layerFillFrom(*lightSlotFill, icf::Appearance::Light, nullptr);

        // `[BIN]` `0x10C148` and `0x10C260`. `[ART]` Unreached by the corpus:
        // across 145 documents no layer names `system-light` or `system-dark`,
        // while 33 backgrounds do. The cases exist in the converter and are
        // transcribed; the corpus says the vocabulary is the chiclet's.
        case icf::FillKind::SystemLight: return systemOf(SystemFill::Light);
        case icf::FillKind::SystemDark:  return systemOf(SystemFill::Dark);

        case icf::FillKind::Solid:
        case icf::FillKind::AutomaticGradient:
            return colouredCase(fill, std::nullopt);

        case icf::FillKind::LinearGradient: {
            // `[BIN]` THE ONE SITE OF FOUR that reads `orientation`: `0x10C1A0`
            // builds `GradientPlacement` through the constructor at `0x10C1DC`
            // from `orientation.start` and `orientation.stop`, in that order.
            std::optional<GradientPlacement> placement;
            if (fill.orientation) {
                GradientPlacement p;
                p.start = {fill.orientation->start.x, fill.orientation->start.y};
                p.end = {fill.orientation->stop.x, fill.orientation->stop.y};
                placement = p;
            }
            return colouredCase(fill, placement);
        }
    }
    return refuse("unknown fill kind");
}

FillResolution resolveLayerFill(const icf::Layer& layer, icf::Context ctx) {
    const icf::json::Value* node = layer.resolve("fill", ctx);
    if (!node) return noFill();
    auto parsed = icf::fillFrom(*node);
    if (!parsed) return refuse("unreadable fill");

    // `[BIN]` The second specialization slot, `0x10BDC0`: `mov w9, #0x100 ;
    // bfxil w9, w0, #0, #8`. The appearance byte is REPLACED with `light` and
    // the low byte of the slot is kept, so the idiom crosses unchanged.
    //
    // The target BUILDS that slot unconditionally, before the kind is known,
    // and only the `automatic` case reads what it resolves to. The second
    // resolution is done conditionally here for a reason the target does not
    // have: resolving is free there and PARSING is this reader's own step, and
    // a value that is never read must not be able to refuse a read that
    // succeeded. So the condition below is the set of inputs that consume the
    // slot, and nothing wider.
    if (parsed->kind != icf::FillKind::Automatic ||
        ctx.appearance == icf::Appearance::Light) {
        return layerFillFrom(*parsed, ctx.appearance, nullptr);
    }

    icf::Context lightCtx = ctx;
    lightCtx.appearance = icf::Appearance::Light;

    const icf::json::Value* lightNode = layer.resolve("fill", lightCtx);
    if (!lightNode) return layerFillFrom(*parsed, ctx.appearance, nullptr);

    auto lightFill = icf::fillFrom(*lightNode);
    if (!lightFill) return refuse("unreadable fill at the light slot");
    return layerFillFrom(*parsed, ctx.appearance, &*lightFill);
}

}  // namespace rb
