// O GATE DO CACHE (plano 2026-09-29, R3). Um render com cache e IGUAL, float a
// float, ao render sem cache das mesmas entradas -- inclusive depois de uma
// sequencia de edicoes que passam pelo MESMO cache quente, que e o uso do
// canvas. Cada edicao da sequencia mexe numa entrada de uma das quatro etapas
// guardadas (`RenderCache.h`); se a chave de uma delas esquecer essa entrada, a
// etapa devolve a imagem da edicao anterior e o caso reprova.
//
// A deteccao foi medida, nao suposta: a lista de mutilacoes da chave que este
// arquivo pega esta no laudo de perfil, §6.
#include "check.h"
#include "Source/IconComposerFoundation/Edit.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/RenderCache.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using namespace rb;

namespace {

namespace fs = std::filesystem;

Device& gpu() {
    static Device* d = [] {
        auto made = Device::create();
        if (!made) {
            std::printf("  FAIL no Vulkan device: %s\n", made.error().c_str());
            ++ictest::failures();
            return static_cast<Device*>(nullptr);
        }
        return new Device(std::move(*made));
    }();
    static Device dead;
    return d ? *d : dead;
}

std::optional<icf::IconBundle> corpusBundle(const char* name) {
    const char* dir = std::getenv("IC_CORPUS_DIR");
    if (!dir || !*dir) return std::nullopt;
    return icf::IconBundle::open(fs::path(dir) / name);
}

// Tudo que o render devolve e que a tela ou o painel de diagnostico mostram.
bool sameRender(const RenderedIcon& a, const RenderedIcon& b, std::string& why) {
    if (a.width != b.width || a.height != b.height || a.originX != b.originX ||
        a.originY != b.originY) {
        why = "dimensoes";
        return false;
    }
    if (a.rgba.size() != b.rgba.size() ||
        std::memcmp(a.rgba.data(), b.rgba.data(), a.rgba.size() * sizeof(float)) != 0) {
        std::size_t differing = 0;
        for (std::size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i) {
            if (std::memcmp(&a.rgba[i], &b.rgba[i], sizeof(float)) != 0) ++differing;
        }
        why = "pixels: " + std::to_string(differing) + " floats diferentes";
        return false;
    }
    if (a.drawn != b.drawn || a.notes != b.notes || a.shapeGaps != b.shapeGaps) {
        why = "relatorio";
        return false;
    }
    return true;
}

icf::json::Value parsed(const char* text) {
    auto v = icf::json::parse(text);
    if (!v) {
        std::printf("  FAIL json de teste nao le: %s\n", text);
        ++ictest::failures();
        return icf::json::Value::null();
    }
    return *v;
}

void setOn(icf::IconBundle& b, icf::NodePath path, const char* prop, const char* json) {
    icf::json::Value* node = icf::nodeAt(b.json(), path);
    if (!node) {
        std::printf("  FAIL no do teste nao existe: %s\n", prop);
        ++ictest::failures();
        return;
    }
    icf::setProperty(*node, prop, icf::Context{}, parsed(json));
}

struct Step {
    const char* name;
    std::function<void(icf::IconBundle&, IconRenderOptions&)> apply;
};

// Aplica os passos em sequencia, CUMULATIVOS, sobre um cache so, e compara
// cada render com cache contra o render sem cache das mesmas entradas.
void runSequence(const char* doc, IconRenderOptions base, const std::vector<Step>& steps) {
    Device& d = gpu();
    if (!d.valid()) return;
    auto bundle = corpusBundle(doc);
    if (!bundle) {
        std::printf("  FAIL corpus: %s (IC_CORPUS_DIR)\n", doc);
        ++ictest::failures();
        return;
    }
    RenderCache cache;
    IconRenderOptions io = base;
    icf::IconBundle current = bundle->clone();

    auto compare = [&](const char* label) {
        IconRenderOptions plain = io;
        plain.cache = nullptr;
        IconRenderOptions cached = io;
        cached.cache = &cache;
        auto want = renderIcon(d, current, plain);
        auto got = renderIcon(d, current, cached);
        if (!want || !got) {
            std::printf("  FAIL %s / %s: o render falhou\n", doc, label);
            ++ictest::failures();
            return;
        }
        std::string why;
        if (!sameRender(*want, *got, why)) {
            std::printf("  FAIL %s / %s: com cache difere de sem cache (%s)\n", doc, label,
                        why.c_str());
            ++ictest::failures();
        }
    };

    compare("frio");
    for (const Step& s : steps) {
        s.apply(current, io);
        compare(s.name);
    }
}

}  // namespace

TEST_CASE(render_cache_lru_keeps_the_recent_and_drops_the_old) {
    RenderCache c(100);
    const CacheKey a{1, 1}, b{2, 2}, x{3, 3};
    c.store(a, std::vector<float>(1), 60);
    c.store(b, std::vector<float>(1), 30);
    CHECK(c.find<std::vector<float>>(a) != nullptr);   // a passa a ser o mais recente
    c.store(x, std::vector<float>(1), 30);             // 120 > 100: sai o mais antigo, b
    CHECK(c.find<std::vector<float>>(a) != nullptr);
    CHECK(c.find<std::vector<float>>(b) == nullptr);
    CHECK(c.find<std::vector<float>>(x) != nullptr);
    CHECK_EQ(c.heldBytes(), std::size_t{90});
    // Um valor maior que o orcamento inteiro volta para quem pediu e nao fica.
    auto big = c.store(CacheKey{4, 4}, std::vector<float>(1), 101);
    CHECK(big != nullptr);
    CHECK(c.find<std::vector<float>>(CacheKey{4, 4}) == nullptr);
    // O tipo faz parte da resposta: a mesma chave nao serve outro tipo.
    CHECK(c.find<int>(a) == nullptr);
}

TEST_CASE(render_cache_hasher_frames_every_call) {
    // "ab" + "c" e "a" + "bc" sao os mesmos bytes em chamadas diferentes.
    KeyHasher h1("d"), h2("d");
    h1.bytes("ab", 2).bytes("c", 1);
    h2.bytes("a", 1).bytes("bc", 2);
    CHECK(!(h1.finish() == h2.finish()));
    KeyHasher h3("d"), h4("e");
    h3.bytes("x", 1);
    h4.bytes("x", 1);
    CHECK(!(h3.finish() == h4.finish()));
    KeyHasher h5("d"), h6("d");
    h5.bytes("x", 1);
    h6.bytes("x", 1);
    CHECK(h5.finish() == h6.finish());
}

TEST_CASE(render_cache_second_render_is_all_hits_and_the_same) {
    Device& d = gpu();
    if (!d.valid()) return;
    auto bundle = corpusBundle("Apollo-Reborn__Apollo-Reborn__AppIcon");
    REQUIRE(bundle.has_value());
    RenderCache cache;
    IconRenderOptions io;
    io.size = 128;
    auto plain = renderIcon(d, *bundle, io);
    io.cache = &cache;
    auto first = renderIcon(d, *bundle, io);
    const RenderCache::Stats afterFirst = cache.stats();
    auto second = renderIcon(d, *bundle, io);
    const RenderCache::Stats afterSecond = cache.stats();
    REQUIRE(plain && first && second);
    std::string why;
    CHECK(sameRender(*plain, *first, why));
    CHECK(sameRender(*plain, *second, why));
    // O primeiro guarda; o segundo so acha.
    CHECK(afterFirst.misses > 0);
    CHECK_EQ(afterSecond.misses, afterFirst.misses);
    CHECK(afterSecond.hits > afterFirst.hits);
}

TEST_CASE(render_cache_follows_every_edit_on_a_vector_glass_document) {
    IconRenderOptions base;
    base.size = 128;
    runSequence(
        "Apollo-Reborn__Apollo-Reborn__AppIcon", base,
        {
            {"posicao de uma camada",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {1, 1}, "position",
                       R"({"scale":1.5,"translation-in-points":[20,90]})");
             }},
            {"cor de uma camada",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {1, 1}, "fill", R"({"solid":"srgb:0.10000,0.80000,0.30000,1.00000"})");
             }},
            {"opacidade de uma camada",
             [](icf::IconBundle& b, IconRenderOptions&) { setOn(b, {1, 0}, "opacity", "0.4"); }},
            {"estilo da sombra do grupo",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {1, std::nullopt}, "shadow", R"({"kind":"layer-color","opacity":0.8})");
             }},
            {"translucidez do grupo",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {1, std::nullopt}, "translucency", R"({"enabled":true,"value":0.7})");
             }},
            {"fundo do documento",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {}, "fill", R"({"solid":"srgb:0.90000,0.20000,0.20000,1.00000"})");
             }},
            {"esconder uma das tres copias do mesmo svg",
             [](icf::IconBundle& b, IconRenderOptions&) { setOn(b, {3, 1}, "hidden", "true"); }},
            {"trocar a arte de uma camada",
             [](icf::IconBundle& b, IconRenderOptions&) {
                 setOn(b, {1, 4}, "image-name", R"("Face.svg")");
             }},
            {"tamanho", [](icf::IconBundle&, IconRenderOptions& io) { io.size = 96; }},
            {"subdivisoes", [](icf::IconBundle&, IconRenderOptions& io) { io.subdivisions = 8; }},
            // A 1024 px e longe da borda, porque a margem que o plano soma ao
            // ladrilho (o alcance da sombra) engolia o canvas inteiro a 256 px:
            // os dois ladrilhos viravam o MESMO buffer, e o caso seguinte nao
            // separava nada (medido pela varredura de mutacao).
            {"ladrilho",
             [](icf::IconBundle&, IconRenderOptions& io) {
                 io.size = 1024;
                 io.viewport = IconViewport{300, 300, 112, 96};
             }},
            // Mesmas dimensoes, outra ORIGEM: o unico caso em que so a origem
            // separa duas chaves -- a do campo (`FieldOptions`) e a da pastilha
            // (a grade). Sem ele as duas podiam perder a origem sem reprovar.
            {"outro ladrilho, mesmas dimensoes",
             [](icf::IconBundle&, IconRenderOptions& io) {
                 io.viewport = IconViewport{500, 560, 112, 96};
             }},
            {"idioma watchOS (a pastilha vira circulo)",
             [](icf::IconBundle&, IconRenderOptions& io) {
                 io.viewport = IconViewport{};
                 io.size = 128;
                 io.context.idiom = icf::Idiom::WatchOS;
             }},
            {"aparencia escura",
             [](icf::IconBundle&, IconRenderOptions& io) {
                 io.context = icf::Context{};
                 io.context.appearance = icf::Appearance::Dark;
             }},
            {"de volta ao comeco das opcoes",
             [](icf::IconBundle&, IconRenderOptions& io) {
                 io.context = icf::Context{};
                 io.subdivisions = 16;
             }},
        });
}

TEST_CASE(render_cache_follows_every_edit_on_a_raster_glass_document) {
    // O campo desta camada sai do ALFA da arte (`fieldFromAlphaCached`), nao
    // de contornos: a outra chave de campo.
    IconRenderOptions base;
    base.size = 128;
    runSequence("ARMSX2__ARMSX1__icon", base,
                {
                    {"posicao da camada raster",
                     [](icf::IconBundle& b, IconRenderOptions&) {
                         setOn(b, {0, 0}, "position",
                               R"({"scale":0.25,"translation-in-points":[40,-10]})");
                     }},
                    {"translucidez do grupo",
                     [](icf::IconBundle& b, IconRenderOptions&) {
                         setOn(b, {0, std::nullopt}, "translucency",
                               R"({"enabled":true,"value":0.9})");
                     }},
                    {"tamanho", [](icf::IconBundle&, IconRenderOptions& io) { io.size = 160; }},
                });
}

TEST_CASE(render_cache_keys_the_svg_by_its_text) {
    // Dois arquivos com o MESMO viewBox e conteudo diferente: a posicao, a
    // tinta e as opcoes empatam, e so o texto separa as chaves. Todo svg do
    // corpus tem viewBox proprio, entao a variante e escrita aqui, numa copia
    // do bundle. O que muda e a GEOMETRIA (o ponto inicial do primeiro
    // caminho), nao uma cor: a tinta que o render aplica por cima cobre as
    // cores do arquivo, e trocar a cor dentro dele nao mudou um pixel -- a
    // varredura de mutacao mediu isso duas vezes antes deste texto.
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    const fs::path src = fs::path(dir) / "Apollo-Reborn__Apollo-Reborn__AppIcon";
    const fs::path tmp = fs::temp_directory_path() / "ic_render_cache_svg_text";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::copy(src, tmp, fs::copy_options::recursive, ec);
    REQUIRE(!ec);
    {
        std::string text;
        {
            std::FILE* f = std::fopen((tmp / "Assets" / "Eyes 3.svg").string().c_str(), "rb");
            REQUIRE(f != nullptr);
            char buf[4096];
            std::size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            std::fclose(f);
        }
        const std::string from = "d=\"M2147 2790 ";
        const std::size_t at = text.find(from);
        REQUIRE(at != std::string::npos);
        text.replace(at, from.size(), "d=\"M2187 2750 ");
        std::FILE* f = std::fopen((tmp / "Assets" / "Eyes-movido.svg").string().c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    auto bundle = icf::IconBundle::open(tmp);
    REQUIRE(bundle.has_value());

    RenderCache cache;
    IconRenderOptions io;
    io.size = 128;
    io.cache = &cache;
    REQUIRE(renderIcon(d, *bundle, io).has_value());   // aquece com Eyes 3.svg
    setOn(*bundle, {1, 1}, "image-name", R"("Eyes-movido.svg")");
    auto got = renderIcon(d, *bundle, io);
    io.cache = nullptr;
    auto want = renderIcon(d, *bundle, io);
    REQUIRE(got && want);
    std::string why;
    if (!sameRender(*want, *got, why)) {
        std::printf("  FAIL arte trocada por outra de mesmo viewBox: %s\n", why.c_str());
        ++ictest::failures();
    }
    fs::remove_all(tmp, ec);
}

TEST_CASE(render_cache_with_no_room_draws_the_same) {
    // Orcamento de um byte: nada fica guardado e todo passo e refeito. O que
    // isto prova e que o caminho de "guardar e devolver o que guardou" nao
    // muda o valor, independente de o cache reter alguma coisa.
    Device& d = gpu();
    if (!d.valid()) return;
    auto bundle = corpusBundle("Apollo-Reborn__Apollo-Reborn__AppIcon");
    REQUIRE(bundle.has_value());
    RenderCache tiny(1);
    IconRenderOptions io;
    io.size = 128;
    auto plain = renderIcon(d, *bundle, io);
    io.cache = &tiny;
    auto cached = renderIcon(d, *bundle, io);
    REQUIRE(plain && cached);
    std::string why;
    CHECK(sameRender(*plain, *cached, why));
    CHECK_EQ(tiny.entries(), std::size_t{0});
}
