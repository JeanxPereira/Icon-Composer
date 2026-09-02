#include "Source/RenderBox/GlassMaterial.h"

#include <charconv>
#include <cmath>
#include <string>

namespace rb {
namespace {

// A failure that names the key, so a corpus run says WHICH group key refused
// rather than only that the group did.
std::optional<GlassMaterialDocument> refuse(GlassMaterialReadError* error, const char* key,
                                            const char* why) {
    if (error) {
        error->key = key;
        error->why = why;
    }
    return std::nullopt;
}

// A JSON number's lexeme as a double, requiring that the WHOLE lexeme is the
// number. The tree keeps the source text (Json.h), so a number becomes a value
// here, once, at the edge -- the same rule `Values.h` applies one layer down.
bool wholeDouble(const std::string& lexeme, double& out) {
    const char* first = lexeme.data();
    const char* last = first + lexeme.size();
    const auto r = std::from_chars(first, last, out);
    return r.ec == std::errc() && r.ptr == last;
}

}  // namespace

std::optional<GlassMaterialDocument> readGlassMaterial(const icf::Group& group,
                                                       icf::Context ctx,
                                                       GlassMaterialReadError* error) {
    GlassMaterialDocument d;

    // Every lookup goes through `resolve`, never through `find`. That is what
    // makes the bare key and the `-specializations` list the same code path, and
    // it is what makes the appearance predicate apply. `[ART]` 238 of the
    // corpus's 407 material specialization entries carry an `appearance`, so a
    // reader that ignored the list would read the wrong value for the light or
    // dark rendering of a third of the documents that have one.
    if (const icf::json::Value* v = group.resolve("specular", ctx)) {
        // `[ART]` Two shapes, and the vocabulary is one: bool x197, "inside" x3.
        // `[INF]` The bool is the off/on axis of `SpecularHighlight` and the
        // string is the placement axis; `true` is the case that names no
        // placement, which is `automatic`.
        if (v->kind() == icf::json::Value::Kind::Bool) {
            d.specular = v->boolean() ? icf::SpecularHighlight::Automatic
                                      : icf::SpecularHighlight::Off;
        } else if (v->kind() == icf::json::Value::Kind::String) {
            d.specular = icf::specularHighlightFromString(v->rawString());
            if (!d.specular) return refuse(error, "specular", "unknown highlight name");
        } else {
            return refuse(error, "specular", "neither bool nor string");
        }
    }

    if (const icf::json::Value* v = group.resolve("shadow", ctx)) {
        d.shadow = icf::shadowFrom(*v);
        if (!d.shadow) return refuse(error, "shadow", "not {kind, opacity}");
    }

    if (const icf::json::Value* v = group.resolve("translucency", ctx)) {
        d.translucency = icf::translucencyFrom(*v);
        if (!d.translucency) {
            return refuse(error, "translucency", "not {enabled, value}");
        }
    }

    if (const icf::json::Value* v = group.resolve("blur-material", ctx)) {
        d.blurMaterialKey = true;
        // A null is the collapsed `{enabled, explicitStrength}` pair and stays
        // ABSENT rather than becoming a number -- see the header. It is not an
        // error: `[ART]` it is the single most common value the key takes, 85 of
        // 159.
        if (v->kind() == icf::json::Value::Kind::Number) {
            double b = 0;
            if (!wholeDouble(v->number(), b)) {
                return refuse(error, "blur-material", "unreadable number");
            }
            d.blurMaterial = b;
        } else if (v->kind() != icf::json::Value::Kind::Null) {
            return refuse(error, "blur-material", "neither number nor null");
        }
    }

    if (const icf::json::Value* v = group.resolve("refractivity", ctx)) {
        d.refractivity = icf::refractivityFrom(*v);
        if (!d.refractivity) {
            return refuse(error, "refractivity", "not {enabled, strength, depth}");
        }
    }

    if (const icf::json::Value* v = group.resolve("lighting", ctx)) {
        if (v->kind() != icf::json::Value::Kind::String) {
            return refuse(error, "lighting", "not a string");
        }
        d.lighting = icf::lightingFromString(v->rawString());
        if (!d.lighting) return refuse(error, "lighting", "unknown lighting name");
    }

    return d;
}

GlassMaterial glassMaterialFrom(const GlassMaterialDocument& doc) {
    GlassMaterial m;

    if (doc.specular) {
        // `[INF]` 4 = 1 + 3. `hasSpecular` is the off/rest bit and
        // `specularPlacement` carries the three remaining cases. `off` has no
        // placement of its own, so it takes the placement default rather than
        // inventing a fourth.
        m.hasSpecular = *doc.specular != icf::SpecularHighlight::Off;
        switch (*doc.specular) {
            case icf::SpecularHighlight::Inside:
                m.specularPlacement = SpecularPlacement::Inside;
                break;
            case icf::SpecularHighlight::Outside:
                m.specularPlacement = SpecularPlacement::Outside;
                break;
            case icf::SpecularHighlight::Off:
            case icf::SpecularHighlight::Automatic:
                m.specularPlacement = SpecularPlacement::Automatic;
                break;
        }
    }

    if (doc.shadow) {
        // `[INF]` The rename across the boundary. Three names are shared and the
        // fourth is not, and the cardinalities are both four -- so the pairing
        // of `layer-color` with `vibrant` is forced rather than chosen.
        switch (doc.shadow->kind) {
            case icf::ShadowKind::Automatic: m.shadowStyle = ShadowStyle::Automatic; break;
            case icf::ShadowKind::None:      m.shadowStyle = ShadowStyle::None; break;
            case icf::ShadowKind::LayerColor: m.shadowStyle = ShadowStyle::Vibrant; break;
            case icf::ShadowKind::Neutral:   m.shadowStyle = ShadowStyle::Neutral; break;
        }
        m.shadowOpacity = doc.shadow->opacity;
    }

    // `[OBS]` The `enabled` bits do not appear on either side of the assignment.
    // Where they collapse was not read, and the corpus cannot settle it: 42
    // groups are disabled with a non-zero value, so "zero it" and "ignore the
    // bit" are distinguishable behaviours and nothing says which one happens.
    if (doc.translucency) m.translucency = doc.translucency->value;
    if (doc.blurMaterial) m.blurStrength = *doc.blurMaterial;
    if (doc.refractivity) {
        m.refractionStrength = doc.refractivity->strength;
        m.refractionHeight = doc.refractivity->depth;
    }

    return m;
}

double denormaliseRefractionHeight(double normalisedHeight, const GlassRenderingParameters& p) {
    // `std::fmin` is IEEE `minNum`, which is what `fminnm` computes: a NaN
    // operand loses to the number. So a NaN height ends up at the ceiling of 1
    // and then at `max`, and that is the target's behaviour, not a rounding of
    // it. The `>= 0` floor is written as the binary writes it -- a compare, not
    // a `max` -- so it agrees on the NaN that the ceiling already removed.
    const double capped = std::fmin(normalisedHeight, 1.0);
    const double clamped = capped >= 0.0 ? capped : 0.0;
    return p.refractionHeightMin +
           (p.refractionHeightMax - p.refractionHeightMin) *
               std::pow(clamped, p.refractionHeightPower);
}

double denormaliseRefractionStrength(double normalisedStrength,
                                     const GlassRenderingParameters& p) {
    // The magnitude goes through the power and the sign goes around it. Feeding
    // a negative base to `pow` would give NaN, so this is not a stylistic
    // rearrangement of `pow(s, power)` -- it is the only way the corpus's two
    // negative strengths can produce a number at all.
    const double magnitude = std::fmin(std::fabs(normalisedStrength), 1.0);
    double sign = std::copysign(1.0, normalisedStrength);
    // `s == 0` catches -0.0 as well, which is why the zero test is on the input
    // and not on the `copysign` result: `copysign(1, -0.0)` is -1.
    if (normalisedStrength == 0.0 || std::isnan(normalisedStrength)) sign = 0.0;
    return p.refractionStrengthMax * sign * std::pow(magnitude, p.refractionStrengthPower);
}

double denormaliseBlurRadius(double normalisedBlur, const GlassRenderingParameters& p) {
    // Ceiling only. There is no floor in the binary and there is none here.
    return std::fmin(normalisedBlur, 1.0) * p.blurStrengthMax;
}

DenormalisedGlass denormaliseGlass(const GlassMaterial& material,
                                   const GlassRenderingParameters& p) {
    DenormalisedGlass out;
    out.refractionHeightPoints = denormaliseRefractionHeight(material.refractionHeight, p);
    out.refractionStrengthPoints = denormaliseRefractionStrength(material.refractionStrength, p);
    out.displacementShaderArgument = -out.refractionStrengthPoints;
    out.blurRadiusPoints = denormaliseBlurRadius(material.blurStrength, p);
    out.refractionSupersampling = p.refractionSupersampling;

    out.translucency = material.translucency;
    out.shadowOpacity = material.shadowOpacity;
    out.hasSpecular = material.hasSpecular;
    out.specularPlacement = material.specularPlacement;
    out.shadowStyle = material.shadowStyle;
    out.hasShadow = material.hasShadow();
    out.shadowInfusesGlyphColor = material.shadowInfusesGlyphColor();
    return out;
}

}  // namespace rb
