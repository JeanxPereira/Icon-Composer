#pragma once
// The typed reading of a `.icon` property VALUE.
//
// The resolver (IconDocument.h) answers with a JSON node; this turns that node
// into something with a type. Every vocabulary here is closed -- doc 01 §6 read
// the cases out of the binary -- and every shape was measured over the corpus.
//
// Every reader returns `nullopt` rather than a default. A value this layer
// cannot read is a value the format did not intend, and saying so is the point
// of having a typed layer at all.
#include "Source/IconComposerFoundation/Json.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace icf {

enum class ColorSpace { DisplayP3, SRGB, ExtendedSRGB, Gray, ExtendedGray };

// A colour, faithful to what the document says. The grey spaces carry two
// components (luminance, alpha) and the RGB spaces four; the count is a property
// of the space and is NOT normalised here -- turning grey into RGBA is a
// rendering decision, and this layer does not make those.
struct Color {
    ColorSpace space = ColorSpace::SRGB;
    int count = 0;
    double components[4] = {0, 0, 0, 0};
};

std::optional<Color> colorFromString(std::string_view s);

// Ten, not seventeen: the format cannot name the other seven (doc 01 §6).
enum class BlendMode { Normal, PlusLighter, PlusDarker, Overlay, Multiply,
                       SoftLight, HardLight, Darken, Lighten, Screen };
enum class ShadowKind { Automatic, Neutral, LayerColor, None };
enum class SpecularHighlight { Off, Automatic, Inside, Outside };
enum class Lighting { Individual, Combined };
enum class FillKind { None, Automatic, Solid, AutomaticGradient, LinearGradient,
                      SystemLight, SystemDark };

std::optional<BlendMode> blendModeFromString(std::string_view s);
std::optional<ShadowKind> shadowKindFromString(std::string_view s);
std::optional<SpecularHighlight> specularHighlightFromString(std::string_view s);
std::optional<Lighting> lightingFromString(std::string_view s);
std::optional<FillKind> fillKindFromString(std::string_view s);

struct Point {
    double x = 0;
    double y = 0;
};

struct Orientation {
    Point start;
    Point stop;
};

struct Fill {
    FillKind kind = FillKind::None;
    // `solid` and `automatic-gradient` carry one colour; `linear-gradient`
    // carries the ramp. The other kinds carry none.
    std::vector<Color> colors;
    std::optional<Orientation> orientation;
};

struct Shadow {
    ShadowKind kind = ShadowKind::None;
    double opacity = 0;
};

struct Position {
    double scale = 1;
    Point translation;
};

struct Translucency {
    bool enabled = false;
    double value = 0;
};

struct Refractivity {
    bool enabled = false;
    double strength = 0;
    double depth = 0;
};

std::optional<Fill> fillFrom(const json::Value& v);
std::optional<Shadow> shadowFrom(const json::Value& v);
std::optional<Position> positionFrom(const json::Value& v);
std::optional<Translucency> translucencyFrom(const json::Value& v);
std::optional<Refractivity> refractivityFrom(const json::Value& v);

// ---- writing, since 2026-09-13 --------------------------------------------
// The inverse of the readers above, and each one is held to the bytes it reads:
// `write(toJson(fromJson(v)))` must equal `write(v)` over every value the corpus
// resolves (test_values_tojson).
//
// `[ART]` A colour component is "%.5f": 1,978 of 1,978 in the corpus (spec 13/09
// §2.2). Doubles elsewhere go through `json::Value::number(double)`.
std::string colorToString(const Color& c);
std::string_view blendModeToString(BlendMode m);
std::string_view shadowKindToString(ShadowKind k);
std::string_view specularHighlightToString(SpecularHighlight s);
std::string_view lightingToString(Lighting l);
std::string_view fillKindToString(FillKind k);

json::Value fillToJson(const Fill& f);
json::Value shadowToJson(const Shadow& s);
json::Value positionToJson(const Position& p);
json::Value translucencyToJson(const Translucency& t);
json::Value refractivityToJson(const Refractivity& r);

}  // namespace icf
