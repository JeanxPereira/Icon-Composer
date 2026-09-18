// O gate do render de viewport (spec 2026-09-16, "O invariante que governa o
// desenho"): um render de viewport e IGUAL ao recorte correspondente de um
// render cheio na mesma `size`, float a float, tolerancia ZERO.
//
// Quando nao e, o relatorio diz DE QUE TIPO e a diferenca: delta grande colado
// numa borda interna do buffer e margem curta; delta na ordem de ULP espalhado
// pelas bordas antialiasadas e aritmetica da GPU. So o primeiro e o desenho
// errado -- e nenhum dos dois se resolve afrouxando a tolerancia.
#include "check.h"
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/Png.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/ViewportPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
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

struct ViewportDiff {
    std::size_t differing = 0;
    float maxAbs = 0.0f;
    std::uint32_t worstX = 0, worstY = 0;   // na grade de `size`
    // Distancia do pior pixel ate a borda do buffer mais proxima que NAO
    // coincide com a borda do canvas. Maximo quando o buffer e o canvas.
    std::uint32_t worstInnerEdge = std::numeric_limits<std::uint32_t>::max();
};

ViewportDiff diffAgainstCrop(const RenderedIcon& full, const RenderedIcon& part) {
    ViewportDiff d;
    const PixelGrid& b = part.buffer;
    for (std::uint32_t y = 0; y < part.height; ++y) {
        for (std::uint32_t x = 0; x < part.width; ++x) {
            const std::uint32_t gx = static_cast<std::uint32_t>(part.originX) + x;
            const std::uint32_t gy = static_cast<std::uint32_t>(part.originY) + y;
            const std::size_t pi = (static_cast<std::size_t>(y) * part.width + x) * 4;
            const std::size_t fi = (static_cast<std::size_t>(gy) * full.width + gx) * 4;
            float worst = 0.0f;
            for (int k = 0; k < 4; ++k) {
                worst = std::max(worst, std::fabs(part.rgba[pi + k] - full.rgba[fi + k]));
            }
            if (worst == 0.0f) continue;
            ++d.differing;
            if (worst <= d.maxAbs) continue;
            d.maxAbs = worst;
            d.worstX = gx;
            d.worstY = gy;
            std::uint32_t edge = std::numeric_limits<std::uint32_t>::max();
            const auto bx0 = static_cast<std::uint32_t>(b.originX);
            const auto by0 = static_cast<std::uint32_t>(b.originY);
            if (bx0 > 0) edge = std::min(edge, gx - bx0);
            if (by0 > 0) edge = std::min(edge, gy - by0);
            if (bx0 + b.width < part.size) edge = std::min(edge, bx0 + b.width - 1 - gx);
            if (by0 + b.height < part.size) edge = std::min(edge, by0 + b.height - 1 - gy);
            d.worstInnerEdge = edge;
        }
    }
    return d;
}

// Renderiza cheio e em cada viewport, compara, e imprime o relatorio de
// cada viewport que diferir. Devolve quantos viewports diferiram.
int compareViewports(const icf::IconBundle& bundle, std::uint32_t size,
                     const std::vector<IconViewport>& views, RenderedIcon* fullOut = nullptr) {
    Device& dev = gpu();
    IconRenderOptions o;
    o.size = size;
    auto full = renderIcon(dev, bundle, o);
    if (!full) {
        std::printf("  FAIL render cheio: %s\n", full.error().c_str());
        ++ictest::failures();
        return 1;
    }
    int bad = 0;
    for (const IconViewport& v : views) {
        IconRenderOptions vo = o;
        vo.viewport = v;
        auto part = renderIcon(dev, bundle, vo);
        if (!part) {
            std::printf("  FAIL viewport (%d,%d %ux%u): %s\n", v.originX, v.originY, v.width,
                        v.height, part.error().c_str());
            ++bad;
            continue;
        }
        const ViewportDiff d = diffAgainstCrop(*full, *part);
        if (d.differing == 0) continue;
        ++bad;
        std::printf("  viewport (%d,%d %ux%u) buffer (%d,%d %ux%u): %zu px diferentes, "
                    "max |d| = %.9g em (%u,%u), a %u px da borda interna\n",
                    v.originX, v.originY, v.width, v.height, part->buffer.originX,
                    part->buffer.originY, part->buffer.width, part->buffer.height, d.differing,
                    static_cast<double>(d.maxAbs), d.worstX, d.worstY, d.worstInnerEdge);
    }
    if (fullOut) *fullOut = std::move(*full);
    return bad;
}

// Os quatro viewports da spec, numa grade de `s`: cruzando a borda da forma,
// encostado no canvas, com origem que nao e multipla de 4, e abaixo da forma.
std::vector<IconViewport> gateViewports(std::uint32_t s) {
    const std::int32_t q = static_cast<std::int32_t>(s / 4);
    const std::uint32_t side = s / 3;
    return {
        IconViewport{q - 7, q - 7, side, side},                                    // borda da forma
        IconViewport{0, static_cast<std::int32_t>(s - side), side, side},          // canto do canvas
        IconViewport{q + 3, q * 2 + 1, side + 5, side - 3},                        // origem nao alinhada
        IconViewport{q, static_cast<std::int32_t>(s - s / 5), side, s / 5},        // abaixo da forma
    };
}

// Um bundle de mentira, escrito no temporario, para gatear o que a Task 3
// converteu SEM depender de um documento do corpus: fundo em gradiente,
// pastilha, arte vetor (preenchida, TRACADA e MASCARADA com regiao) e arte
// raster, e nenhum vidro -- que e exatamente a fatia que ja anda num buffer
// parcial. O `<filter>` mora num documento a parte, porque ele nao anda: e
// recusado por nome, e o caso que o cobre mede a recusa e nao o pixel.
class TempBundle {
public:
    explicit TempBundle(const std::string& name, const std::string& document) {
        dir_ = fs::temp_directory_path() / ("ic-viewport-" + name);
        std::error_code ec;
        fs::remove_all(dir_, ec);
        fs::create_directories(dir_ / "Assets", ec);
        write(dir_ / "icon.json", document);
        // Um circulo: borda curva para o antialias da GPU, e uma rampa propria
        // que mede o retangulo de colocacao.
        write(dir_ / "Assets" / "disc.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<circle cx=\"256\" cy=\"256\" r=\"200\" fill=\"#3080ff\"/></svg>");
        // O TRACO e a MASCARA COM REGIAO, numa arte propria e sem fill de
        // camada, para que cada um chegue ao gate com a sua propria tinta.
        //
        // O traco corre em diagonal por tres quadrantes, com dois vertices, de
        // modo que todo viewport do gate pegue um pedaco dele; a regiao da
        // mascara (80..380) corta um retangulo que vai de 40 a 460, entao as
        // duas bordas do corte caem DENTRO do canvas e um recorte que medisse
        // a regiao na grade errada as moveria.
        write(dir_ / "Assets" / "marks.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<defs><mask id=\"m\" maskUnits=\"userSpaceOnUse\" x=\"80\" y=\"80\""
              " width=\"300\" height=\"300\">"
              "<rect width=\"512\" height=\"512\" fill=\"white\"/></mask></defs>"
              "<rect x=\"40\" y=\"300\" width=\"420\" height=\"140\" fill=\"#20d0a0\""
              " mask=\"url(#m)\"/>"
              "<path d=\"M60 60 L452 210 L200 452\" fill=\"none\" stroke=\"#ff2080\""
              " stroke-width=\"18\"/></svg>");
        // Uma arte com `<filter>`, para o caso da recusa.
        write(dir_ / "Assets" / "blur.svg",
              "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">"
              "<defs><filter id=\"b\"><feGaussianBlur stdDeviation=\"12\"/></filter></defs>"
              "<rect x=\"120\" y=\"120\" width=\"272\" height=\"272\" fill=\"#ffcc00\""
              " filter=\"url(#b)\"/></svg>");
        // Um raster com borda dura e um degrade em x e em z, ampliado 6x: uma
        // amostragem deslocada meio texel aparece na cor, nao so na borda.
        const std::uint32_t side = 48;
        std::vector<float> px(static_cast<std::size_t>(side) * side * 4, 0.0f);
        for (std::uint32_t y = 0; y < side; ++y) {
            for (std::uint32_t x = 0; x < side; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * side + x) * 4;
                const bool in = (x - 24.0) * (x - 24.0) + (y - 24.0) * (y - 24.0) < 400.0;
                px[i + 0] = static_cast<float>(x) / side;
                px[i + 1] = 0.5f;
                px[i + 2] = static_cast<float>(y) / side;
                px[i + 3] = in ? 1.0f : 0.0f;
            }
        }
        const std::vector<std::uint8_t> png = icf::encodePng(px, side, side);
        std::FILE* f = std::fopen((dir_ / "Assets" / "dot.png").string().c_str(), "wb");
        if (f) {
            std::fwrite(png.data(), 1, png.size(), f);
            std::fclose(f);
        }
    }
    ~TempBundle() {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    const fs::path& path() const { return dir_; }

    TempBundle(const TempBundle&) = delete;
    TempBundle& operator=(const TempBundle&) = delete;

private:
    static void write(const fs::path& p, const std::string& text) {
        std::FILE* f = std::fopen(p.string().c_str(), "wb");
        if (!f) return;
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
    fs::path dir_;
};

// Fundo em gradiente + um disco vetor + um raster ampliado, os dois sem vidro.
// As cores sao a grafia do corpus (`display-p3:r,g,b,a`), e o `fill` do disco
// e uma rampa de CAMADA -- que e o que faz o gate medir a avaliacao absoluta
// do compositor de `SvgRenderer`, e nao so a colocacao.
const char* const kPlainDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.90000,0.20000,0.30000,1.00000",
                                   "display-p3:0.10000,0.30000,0.90000,1.00000" ] },
  "groups" : [
    { "layers" : [
      { "glass" : false, "image-name" : "disc.svg", "name" : "disc",
        "fill" : { "linear-gradient" : [ "display-p3:0.10000,0.90000,0.40000,1.00000",
                                         "display-p3:0.95000,0.85000,0.10000,1.00000" ] } },
      { "glass" : false, "image-name" : "marks.svg", "name" : "marks" },
      { "glass" : false, "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ 120, -90 ] } }
    ] }
  ]
})";

// So a arte com `<filter>`, sem fundo: o caso da recusa nao compara pixel.
const char* const kFilteredDocument = R"({
  "groups" : [
    { "layers" : [
      { "glass" : false, "image-name" : "blur.svg", "name" : "blur" }
    ] }
  ]
})";

// Vidro com translucidez, especular e refracao, e sombra `"none"`: o campo e a
// refracao sao julgados sem a escada do desfoque no caminho, que e a unica
// coisa que ainda nao anda num buffer parcial. A forca -0,53 e a do documento
// do corpus que mais refrata (`CamilleScholtz__swmpc__swmpc`, -0,527), entao a
// margem encadeada da spec e exercitada com o pior caso real.
const char* const kGlassDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.9,0.2,0.3,1", "display-p3:0.1,0.3,0.9,1" ] },
  "groups" : [
    { "layers" : [ { "image-name" : "disc.svg", "name" : "disc" } ],
      "refractivity" : { "depth" : 0.5, "enabled" : true, "strength" : -0.53 },
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } },
    { "layers" : [ { "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ 120, -90 ] } } ],
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } }
  ]
})";

// O MESMO VIDRO, MAS COM A ORIGEM DO BUFFER DE FATO EM JOGO.
//
// `kGlassDocument` acima mede o PIOR caso de margem do corpus, e o preco disso
// e que a margem encadeada (339 pontos) come meio canvas: os quatro buffers do
// gate saem com `originX == 0`, e a metade HORIZONTAL de todo sitio convertido
// (`fo.originX`, `lx = x - originX`, `px`) nunca chega a rodar com um valor
// nao-nulo. Pior: a arte daquele documento nao encosta em dois dos quatro
// viewports, que passam por vacuidade.
//
// Este documento corrige as duas coisas de uma vez:
//
//   - a REFRACAO e modesta -- `strength -0.10` da 64 pontos e `depth 0.25` da
//     73,6 pontos de altura, entao cada grupo contribui `max(64, 73,6) = 73,6`
//     para a cadeia. DOIS grupos refratam, entao a soma e 147,2 pontos, que e
//     exatamente a soma sobre a cadeia que a spec descreve ("A margem, e de
//     onde vem o numero": cada refracao le o resultado da anterior). Em
//     `size = 512` isso da margem 98 px e buffers com origem NAO-NULA em tres
//     dos quatro viewports;
//   - a ARTE cobre os quatro. O disco entra com `scale 2`, o que o poe em
//     `56..456` da grade de 512 (centro 256, raio 200): a borda ESQUERDA dele
//     cruza os quatro recortes, e o interior -- que e onde a translucidez age
//     -- cobre os quatro. O raster desce para a esquerda (`x 72..192,
//     y 342..462`, circulo de raio 60 em (132,402)) e alcanca tres deles, de
//     modo que o caminho do campo por ALFA tambem seja julgado com origem.
//
// `position` LEVA AS DUAS CHAVES DE PROPOSITO. `positionFrom`
// (`Values.cpp:209`) devolve `nullopt` se `translation-in-points` faltar ou
// nao tiver dois elementos -- e um `position` invalido e descartado em
// silencio, com a arte voltando ao `scale 1`. Escrever so `"scale": 2` aqui
// deixou o disco em raio 100 e dois viewports julgando o vazio; foi medido.
const char* const kSpreadGlassDocument = R"({
  "fill" : { "linear-gradient" : [ "display-p3:0.9,0.2,0.3,1", "display-p3:0.1,0.3,0.9,1" ] },
  "groups" : [
    { "layers" : [ { "image-name" : "disc.svg", "name" : "disc",
        "position" : { "scale" : 2, "translation-in-points" : [ 0, 0 ] } } ],
      "refractivity" : { "depth" : 0.25, "enabled" : true, "strength" : -0.10 },
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } },
    { "layers" : [ { "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 6, "translation-in-points" : [ -248, 292 ] } } ],
      "refractivity" : { "depth" : 0.25, "enabled" : true, "strength" : -0.10 },
      "shadow" : { "kind" : "none", "opacity" : 0.5 },
      "specular" : true,
      "translucency" : { "enabled" : true, "value" : 0.5 } }
  ]
})";

// A SOMBRA, na size em que a escada do desfoque reduz de fato.
//
// `documentReach` mede a sombra pela tabela DEFAULT de `radius` (0,3, igual
// nos quatro slots de `SizeBasedValue`), nao pelo que o grupo pede -- entao
// basta que ALGUM grupo desenhe sombra (`kind` diferente de `none`, alpha >
// 0) para que `shadowSigmaPoints` saia do zero fixo em 19,2 pontos de canvas.
// Em `size = 512` (`k = 0,5`) isso da `sigmaPx = 9,6`, `variance = 92,16`, e
// `blurLadderAlignment` reduz por 2 (`ceil(92,16/27,5625 - 0,001) = 4 >= 3`);
// nenhum dos fixtures anteriores exercitava isto porque todos eles escreviam
// `"shadow": {"kind": "none", ...}` de proposito, para julgar campo, especular
// e refracao sem a escada no caminho. Dois grupos aqui, um `neutral` (tabela
// `neutralOpacity`) e um `layer-color` (`shadowUsesVibrantTable`, tabela
// `vibrantOpacity`), para que as duas tabelas de alfa sejam exercitadas e nao
// so uma.
const char* const kShadowDocument = R"({
  "groups" : [
    { "layers" : [ { "image-name" : "disc.svg", "name" : "disc",
        "position" : { "scale" : 0.6, "translation-in-points" : [ -140, -160 ] } } ],
      "shadow" : { "kind" : "neutral", "opacity" : 0.8 } },
    { "layers" : [ { "image-name" : "dot.png", "name" : "dot",
        "position" : { "scale" : 5, "translation-in-points" : [ 150, 120 ] } } ],
      "refractivity" : { "depth" : 0.4, "enabled" : true, "strength" : -0.36 },
      "shadow" : { "kind" : "layer-color", "opacity" : 0.6 } }
  ]
})";

// A recusa do filtro, procurada por nome no relatorio.
bool namesTheFilterRefusal(const std::vector<std::string>& gaps) {
    for (const std::string& g : gaps) {
        if (g.find("filtro SVG em render de viewport") != std::string::npos) return true;
    }
    return false;
}

}  // namespace

TEST_CASE(viewport_default_is_the_whole_canvas) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    CHECK_EQ(compareViewports(*bundle, 256, {IconViewport{}}), 0);
}

TEST_CASE(viewport_echoes_where_its_pixels_sit) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    auto bundle = icf::IconBundle::open(fs::path(dir) / "Jellify-Music__App__teal-icon-composer");
    REQUIRE(bundle.has_value());
    // O eco e o que o Kit usa para por a textura no lugar; sem ele o gate nao
    // sabe onde recortar o render cheio.
    IconRenderOptions o;
    o.size = 256;
    o.viewport = IconViewport{96, 96, 64, 64};
    auto part = renderIcon(d, *bundle, o);
    REQUIRE(part.has_value());
    CHECK_EQ(part->width, 64u);
    CHECK_EQ(part->originX, 96);
}

// O INVARIANTE SOBRE PIXEL DE VERDADE, no que a Task 3 converteu: o fundo em
// rampa, o recorte da pastilha com os realces, a arte vetor pela GPU e a arte
// raster amostrada na CPU. Tolerancia ZERO nos quatro viewports da spec.
TEST_CASE(viewport_background_chiclet_and_art_match_the_full_render) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("plain", kPlainDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.backgroundPainted);
    CHECK_EQ(full.drawn, 3u);
    // A rampa da CAMADA tem que estar de fato pintando, ou o caso mediria a
    // colocacao e nao a avaliacao. O SVG se pinta de `#3080ff`, cujo azul e o
    // canal mais forte; a rampa do documento vai de verde a amarelo, e o
    // centro do disco sai `0,527 / 0,875 / 0,249`.
    const std::size_t c = (static_cast<std::size_t>(256) * full.width + 256) * 4;
    CHECK(full.rgba[c + 1] > full.rgba[c + 2]);
}

// A RECUSA DO FILTRO, e nao um pixel.
//
// O desfoque de `feGaussianBlur` le vizinhos e grampeia na borda do buffer, e a
// margem dele sairia do conteudo do SVG, que a conta da margem ainda nao le.
// Entao num buffer parcial ele e DITO (spec 2026-09-16, "O invariante que
// governa o desenho") -- e este caso mede exatamente isso: o render cheio
// desenha e nao recusa, o de viewport recusa com o motivo por escrito.
TEST_CASE(viewport_refuses_an_svg_filter_by_name_instead_of_clamping_it) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("filtered", kFilteredDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());

    IconRenderOptions o;
    o.size = 512;
    auto full = renderIcon(d, *bundle, o);
    REQUIRE(full.has_value());
    CHECK_EQ(full->drawn, 1u);
    CHECK(!namesTheFilterRefusal(full->shapeGaps));

    o.viewport = IconViewport{128, 128, 128, 128};
    auto part = renderIcon(d, *bundle, o);
    REQUIRE(part.has_value());
    CHECK(namesTheFilterRefusal(part->shapeGaps));
}

// O CAMPO, A TRANSLUCIDEZ, O ESPECULAR E A REFRACAO, sobre pixel de verdade.
//
// O campo e o unico passo cujo alcance nao e uma constante -- a distancia de um
// pixel de dentro pode medir ate a borda mais distante do canvas (spec
// 2026-09-16, a `[OBS]` de "A margem, e de onde vem o numero"). O que o salva e
// que nenhum consumidor le alem da propria banda. Este caso e o que prova isso:
// se a hipotese cair, a diferenca aparece LONGE da borda interna, dentro da
// forma, e nao colada nela.
TEST_CASE(viewport_field_translucency_specular_and_refraction_match) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("glass", kGlassDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    // Os tres efeitos tem de estar DESENHANDO, ou o caso mediria o silencio.
    CHECK(full.glassRefracted > 0);
    CHECK(full.glassTranslucent > 0);
    CHECK(full.glassSpecular > 0);
    CHECK_EQ(full.skipped.size(), 0u);
}

// O MESMO INVARIANTE COM A ORIGEM DO BUFFER DE FATO EM JOGO.
//
// O caso acima e o pior caso de margem do corpus, e por isso mesmo ele nao
// exercita origem: os quatro buffers saem em `originX == 0` e dois dos quatro
// viewports nao encostam na arte. Este caso e escrito para o contrario, e a
// cobertura e CONFERIDA em vez de afirmada -- ele imprime a origem de cada
// buffer e exige que a maioria delas seja nao-nula nos dois eixos, e que o
// especular tenha de fato movido pixel em cada um. Sem isso a metade
// horizontal de `fo.originX`, de `lx = x - originX` e de `px` atravessaria a
// task inteira sem nunca rodar com um valor diferente de zero.
TEST_CASE(viewport_glass_matches_with_a_non_zero_buffer_origin) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("glass-spread", kSpreadGlassDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());

    IconRenderOptions o;
    o.size = 512;
    auto full = renderIcon(d, *bundle, o);
    REQUIRE(full.has_value());
    CHECK(full->glassRefracted > 0);
    CHECK(full->glassTranslucent > 0);
    CHECK(full->glassSpecular > 0);
    CHECK_EQ(full->skipped.size(), 0u);

    int nonZeroX = 0, nonZeroY = 0, bad = 0;
    for (const IconViewport& v : gateViewports(512)) {
        IconRenderOptions vo = o;
        vo.viewport = v;
        auto part = renderIcon(d, *bundle, vo);
        if (!part) {
            std::printf("  FAIL viewport (%d,%d %ux%u): %s\n", v.originX, v.originY, v.width,
                        v.height, part.error().c_str());
            ++bad;
            continue;
        }
        const PixelGrid& b = part->buffer;
        if (b.originX != 0) ++nonZeroX;
        if (b.originY != 0) ++nonZeroY;
        // O especular so conta pixel que ele de fato moveu, entao isto e o que
        // separa "o vidro passou por aqui" de "o viewport julgou o vazio".
        CHECK(part->glassSpecular > 0);
        const ViewportDiff diff = diffAgainstCrop(*full, *part);
        std::printf("  viewport (%d,%d %ux%u) buffer (%d,%d %ux%u): %zu px diferentes"
                    " [refr %zu trans %zu spec %zu]\n",
                    v.originX, v.originY, v.width, v.height, b.originX, b.originY, b.width,
                    b.height, diff.differing, part->glassRefracted, part->glassTranslucent,
                    part->glassSpecular);
        if (diff.differing == 0) continue;
        ++bad;
        std::printf("    max |d| = %.9g em (%u,%u), a %u px da borda interna\n",
                    static_cast<double>(diff.maxAbs), diff.worstX, diff.worstY,
                    diff.worstInnerEdge);
    }
    CHECK_EQ(bad, 0);
    // Tres dos quatro: o segundo viewport encosta na borda ESQUERDA do canvas
    // de proposito, e a interseccao com o canvas manda a origem dele para zero
    // em x -- isso e o desenho certo, nao um furo.
    CHECK(nonZeroX >= 3);
    CHECK(nonZeroY >= 3);
}

// A SOMBRA, ULTIMO BLOQUEIO. O gate fecha em zero com a escada do desfoque de
// fato reduzindo -- o passo 4 mediu que ela retornava 1 (nenhuma reducao) em
// todo fixture ate aqui, entao esta e a primeira vez que a origem alinhada
// prova alguma coisa: sem ela, as caixas de `reduceBox` comecam num multiplo
// errado e o resultado muda em todo pixel do buffer, nao so na borda.
TEST_CASE(viewport_shadow_matches_including_the_blur_ladder) {
    Device& d = gpu();
    if (!d.valid()) return;
    TempBundle tb("shadow", kShadowDocument);
    auto bundle = icf::IconBundle::open(tb.path());
    REQUIRE(bundle.has_value());
    RenderedIcon full;
    CHECK_EQ(compareViewports(*bundle, 512, gateViewports(512), &full), 0);
    CHECK(full.glassShadowed >= 2);
    CHECK(full.glassRefracted > 0);   // refracao sobre a sombra do grupo de tras: a cadeia

    // A PROVA ISOLADA DA FUNCAO, num sigma que NENHUM documento alcanca --
    // `radius` satura em 0,3 (ver `kShadowDocument` acima), entao 32,0 so
    // acontece aqui, escrito a mao, para exercitar os DOIS degraus da escada
    // (`blurLadderAlignment(32.0) == 8`: reduz por 4 e depois por 2). Isto
    // prova que a funcao sabe encadear dois niveis; nao prova nada sobre esta
    // fixture.
    CHECK(blurLadderAlignment(32.0) > 1);

    // A PROVA DA FIXTURE, pelo mesmo caminho que `renderIcon` usa
    // (`IconRenderer.cpp`: `documentReach` -> `planViewport` -> `p.alignment`).
    // `shadowSigmaPoints` sai da tabela DEFAULT de `radius` (0,3), fixa e
    // independente do que os dois grupos pedem, contanto que ALGUM deles
    // desenhe sombra -- MEDIDO em 19,2 pontos de canvas. Em `size = 512`,
    // `k = size / kCanvasPoints = 0,5`, isso da `sigmaPx = 9,6` e
    // `blurLadderAlignment(9,6) == 2` (um degrau, nao dois -- a fixture nao
    // alcanca o regime que o CHECK acima exercita). Esta e a assercao que
    // notaria uma regressao: se `radius` mudar, se `size` encolher, ou se
    // `blurPassCountForVariance` mudar de constante, e o sigma real desta
    // fixture cair de volta a 1, e AQUI que o gate acusa -- o CHECK acima,
    // preso no literal 32,0, continuaria verde sem saber.
    const DocumentReach reach = documentReach(bundle->document(), icf::Context{}, IconSizeClass::Large);
    CHECK(blurLadderAlignment(reach.shadowSigmaPoints * (512.0 / kCanvasPoints)) > 1);

    // A COBERTURA, CONFERIDA E NAO AFIRMADA (a licao do passo 4: um "zero" so
    // prova algo se o buffer que o produziu de fato continha o efeito). A
    // refracao encadeada desta fixture pede margem grande -- 173 px em
    // `size = 512`, MEDIDO -- e nenhum dos quatro viewports do gate tem
    // origem X acima disso, entao `originX` sai 0 nos quatro por CLAMPING no
    // canvas, nao por bug: `alignDown` de um valor ja negativo satura em 0
    // antes de a rodada de alinhamento fazer diferenca. O eixo Y e o que
    // sobra para provar o mecanismo, e prova: tres das quatro origens saem
    // NAO-nulas, e o passo 3 (acima) mediu a mesma formula, aplicada ao MESMO
    // `p.alignment`, falhar longe da borda quando desligada -- `alignDown` e
    // uma unica funcao chamada identica para X e Y, entao a prova numa origem
    // cobre a outra.
    int nonZeroY = 0, bad = 0;
    for (const IconViewport& v : gateViewports(512)) {
        IconRenderOptions vo;
        vo.size = 512;
        vo.viewport = v;
        auto part = renderIcon(d, *bundle, vo);
        if (!part) {
            std::printf("  FAIL viewport (%d,%d %ux%u): %s\n", v.originX, v.originY, v.width,
                        v.height, part.error().c_str());
            ++bad;
            continue;
        }
        const PixelGrid& b = part->buffer;
        if (b.originY != 0) ++nonZeroY;
        std::printf("  viewport (%d,%d %ux%u) buffer (%d,%d %ux%u) glassShadowed=%zu\n",
                    v.originX, v.originY, v.width, v.height, b.originX, b.originY, b.width,
                    b.height, part->glassShadowed);
        // `glassShadowed` conta o PASSO rodando no buffer (`++out.glassShadowed`
        // em `IconRenderer.cpp`, uma vez por grupo cujo `castShadow` executa),
        // nao pixels que a sombra de fato moveu -- mais fraco que o
        // `glassSpecular` do caso de vidro acima, que so conta o que o efeito
        // alterou. Ainda assim prova o que este caso precisa: que a escada
        // (`blurLadder`, dentro de `castShadow`) rodou neste buffer e nao foi
        // pulada por vacuidade.
        CHECK(part->glassShadowed > 0);
    }
    CHECK_EQ(bad, 0);
    CHECK(nonZeroY >= 3);
}

// O CORPUS, em `size = 2048` -- 400% sobre 512, o zoom que motivou a spec.
//
// Os dois documentos da spec: `swmpc` (refracao 337 pontos, raster,
// translucidez, sombra, especular, fundo) e `Jellify-Music` (vetor, o mesmo
// sem refracao). Uma execucao so: o render cheio de 2048 roda duas vezes por
// documento (spec 2026-09-16, "A margem", medido em 18/09).
TEST_CASE(viewport_corpus_documents_match_at_2048) {
    Device& d = gpu();
    if (!d.valid()) return;
    const char* dir = std::getenv("IC_CORPUS_DIR");
    REQUIRE(dir && *dir);
    for (const char* name : {"CamilleScholtz__swmpc__swmpc",
                             "Jellify-Music__App__teal-icon-composer"}) {
        auto bundle = icf::IconBundle::open(fs::path(dir) / name);
        REQUIRE(bundle.has_value());
        RenderedIcon full;
        const int bad = compareViewports(*bundle, 2048, gateViewports(2048), &full);
        if (bad) std::printf("  %s: %d viewports diferentes\n", name, bad);
        CHECK_EQ(bad, 0);
        CHECK(full.glassShadowed > 0);
        CHECK(full.backgroundPainted);
        if (std::string(name).find("swmpc") != std::string::npos) {
            CHECK(full.glassRefracted > 0);   // o documento que refrata tem que refratar
        }
        const auto reach = documentReach(bundle->document(), IconRenderOptions{}.context,
                                         IconSizeClass::Large);
        auto plan = planViewport(gateViewports(2048)[0], 2048, reach);
        REQUIRE(plan.has_value());
        std::printf("  %s: margem %u px em 2048 (alinhamento %u), buffer %ux%u\n", name,
                    plan->marginPixels, plan->alignment, plan->buffer.width, plan->buffer.height);
    }
}
