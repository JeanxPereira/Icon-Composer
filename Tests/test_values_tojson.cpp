#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Values.h"

#include <cstdlib>
#include <filesystem>

using namespace icf;

TEST_CASE(values_color_to_string_uses_five_fixed_decimals) {
    // spec §2.2: 1,978 of 1,978 colour components in the corpus are "%.5f".
    Color c;
    c.space = ColorSpace::DisplayP3;
    c.count = 4;
    c.components[0] = 0.5; c.components[1] = 0; c.components[2] = 1; c.components[3] = 0.00392;
    CHECK_EQ(colorToString(c), std::string("display-p3:0.50000,0.00000,1.00000,0.00392"));
    Color g;
    g.space = ColorSpace::Gray;
    g.count = 2;
    g.components[0] = 1; g.components[1] = 1;
    CHECK_EQ(colorToString(g), std::string("gray:1.00000,1.00000"));
}

TEST_CASE(values_enum_to_string_inverts_from_string) {
    for (auto m : {BlendMode::Normal, BlendMode::PlusLighter, BlendMode::PlusDarker, BlendMode::Overlay,
                   BlendMode::Multiply, BlendMode::SoftLight, BlendMode::HardLight, BlendMode::Darken,
                   BlendMode::Lighten, BlendMode::Screen}) {
        auto back = blendModeFromString(blendModeToString(m));
        REQUIRE(back.has_value());
        CHECK(*back == m);
    }
    for (auto k : {ShadowKind::Automatic, ShadowKind::Neutral, ShadowKind::LayerColor, ShadowKind::None}) {
        CHECK(*shadowKindFromString(shadowKindToString(k)) == k);
    }
    for (auto s : {SpecularHighlight::Off, SpecularHighlight::Automatic, SpecularHighlight::Inside,
                   SpecularHighlight::Outside}) {
        CHECK(*specularHighlightFromString(specularHighlightToString(s)) == s);
    }
    CHECK(*lightingFromString(lightingToString(Lighting::Combined)) == Lighting::Combined);
    for (auto f : {FillKind::None, FillKind::Automatic, FillKind::Solid, FillKind::AutomaticGradient,
                   FillKind::LinearGradient, FillKind::SystemLight, FillKind::SystemDark}) {
        CHECK(*fillKindFromString(fillKindToString(f)) == f);
    }
}

TEST_CASE(values_to_json_writes_the_shape_from_json_reads) {
    auto solid = json::parse(R"({"solid" : "srgb:0.00000,0.50000,1.00000,1.00000"})");
    REQUIRE(solid.has_value());
    auto f = fillFrom(*solid);
    REQUIRE(f.has_value());
    CHECK_EQ(json::write(fillToJson(*f)), json::write(*solid));

    auto plain = json::parse(R"("automatic")");
    CHECK_EQ(json::write(fillToJson(*fillFrom(*plain))), std::string("\"automatic\""));

    auto pos = json::parse(R"({"scale" : 0.5, "translation-in-points" : [ 10, -2.5 ]})");
    auto p = positionFrom(*pos);
    REQUIRE(p.has_value());
    CHECK_EQ(json::write(positionToJson(*p)), json::write(*pos));

    auto sh = json::parse(R"({"kind" : "neutral", "opacity" : 0.5})");
    CHECK_EQ(json::write(shadowToJson(*shadowFrom(*sh))), json::write(*sh));
    auto tr = json::parse(R"({"enabled" : true, "value" : 0.2})");
    CHECK_EQ(json::write(translucencyToJson(*translucencyFrom(*tr))), json::write(*tr));
    auto rf = json::parse(R"({"depth" : 0.14, "enabled" : true, "strength" : 0.23})");
    CHECK_EQ(json::write(refractivityToJson(*refractivityFrom(*rf))), json::write(*rf));
}

namespace {
constexpr Appearance kA[] = {Appearance::Base, Appearance::Light, Appearance::Dark, Appearance::Tinted};
constexpr Idiom kI[] = {Idiom::Base, Idiom::Square, Idiom::IOS, Idiom::MacOS, Idiom::WatchOS};

// Every typed value the corpus resolves, re-encoded, must give the bytes it was read from.
void roundTrip(const json::Value& owner, std::size_t& seen, std::size_t& same, std::string& miss) {
    auto check = [&](const json::Value* v, const std::optional<json::Value>& back) {
        if (!v || !back) return;
        ++seen;
        if (json::write(*back) == json::write(*v)) ++same;
        else if (miss.empty()) miss = json::write(*v);
    };
    for (auto a : kA) {
        for (auto i : kI) {
            const Context ctx{a, i};
            if (const json::Value* v = resolve(owner, "fill", ctx)) {
                if (auto f = fillFrom(*v)) check(v, fillToJson(*f));
            }
            if (const json::Value* v = resolve(owner, "shadow", ctx)) {
                if (auto s = shadowFrom(*v)) check(v, shadowToJson(*s));
            }
            if (const json::Value* v = resolve(owner, "position", ctx)) {
                if (auto p = positionFrom(*v)) check(v, positionToJson(*p));
            }
            if (const json::Value* v = resolve(owner, "translucency", ctx)) {
                if (auto t = translucencyFrom(*v)) check(v, translucencyToJson(*t));
            }
            if (const json::Value* v = resolve(owner, "refractivity", ctx)) {
                if (auto r = refractivityFrom(*v)) check(v, refractivityToJson(*r));
            }
        }
    }
}
}  // namespace

TEST_CASE(values_to_json_round_trips_every_corpus_value) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir != nullptr);
    std::size_t seen = 0, same = 0;
    std::string miss;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        auto b = IconBundle::open(e.path());
        if (!b) continue;
        auto doc = b->document();
        roundTrip(doc.json(), seen, same, miss);
        for (const auto& g : doc.groups()) {
            roundTrip(g.json(), seen, same, miss);
            for (const auto& l : g.layers()) roundTrip(l.json(), seen, same, miss);
        }
    }
    std::printf("  %zu typed values re-encoded, %zu identical%s%s\n", seen, same,
                miss.empty() ? "" : "; first miss: ", miss.c_str());
    CHECK(seen >= 1000);
    CHECK_EQ(same, seen);
}
