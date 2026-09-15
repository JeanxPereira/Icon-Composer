// Os realces do chiclet: os numeros medidos, e os erros que uma transcricao
// plausivel cometeria com eles.
//
// Cada caso aqui trava UM erro que nao daria excecao, nao daria aviso e sairia
// como "um fundo com luz", so que a luz errada -- que e a forma de falha que
// este projeto persegue desde o §29.8 do doc 03.
#include "check.h"
#include "Source/RenderBox/ChicletHighlights.h"
#include "Source/RenderBox/ChicletShape.h"

#include <cmath>
#include <vector>

using namespace rb;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) < tol; }

RampPoint stop(float r, float g, float b) {
    RampPoint p;
    p.location = 0.0f;
    p.rgba[0] = r;
    p.rgba[1] = g;
    p.rgba[2] = b;
    p.rgba[3] = 1.0f;
    return p;
}

}  // namespace

// ERRO 1: tomar os numeros do glifo. Os dois conjuntos sao DIFERENTES, e uma
// transcricao que reaproveitasse `glyphHighlightSlots` daria um brilho com a
// distancia errada (6 pt em vez de 10), o cone errado (pi/2 em vez de 2pi/3) e
// dois realces a menos.
TEST_CASE(chiclet_slots_are_not_the_glyph_slots) {
    std::size_t chiclet = 0, glyph = 0;
    const HighlightSlot* c = chicletHighlightSlots(chiclet);
    const HighlightSlot* g = glyphHighlightSlots(glyph);
    CHECK_EQ(chiclet, std::size_t(7));  // seis membros + o `dark` espelhado
    CHECK_EQ(glyph, std::size_t(5));    // `fillDiffuse` e `rim` sao nil no glifo

    // `[BIN]` `0x00062BC8`: keySharp do chiclet.
    CHECK(near(c[0].settings.brightness, 1.1));
    CHECK(near(c[0].settings.opacity.slots[0], 0.2));
    CHECK(near(c[0].settings.distance.slots[0], 10.0));
    CHECK(near(c[0].settings.spread.slots[0], 2.0 * kPi / 3.0));
    CHECK(near(c[0].settings.bias, 0.5));
    // ... contra o keySharp do glifo, `0x00064648`.
    CHECK(near(g[0].settings.brightness, 1.0));
    CHECK(near(g[0].settings.distance.slots[1], 6.0));
    CHECK(near(g[0].settings.spread.slots[0], kPi / 2.0));
}

// ERRO 2: dar ao chiclet os quatro slots DIFERENTES que o glifo tem. No chiclet
// os quatro sao iguais em todos os campos, e um leitor que copiasse a tabela do
// glifo trocaria o icone de 1024 px pelo de 16 px sem que nada reclamasse.
TEST_CASE(every_chiclet_size_slot_carries_the_same_number) {
    std::size_t n = 0;
    const HighlightSlot* c = chicletHighlightSlots(n);
    for (std::size_t i = 0; i < n; ++i) {
        const HighlightSettings& s = c[i].settings;
        for (int k = 1; k < 4; ++k) {
            CHECK(near(s.opacity.slots[k], s.opacity.slots[0]));
            CHECK(near(s.distance.slots[k], s.distance.slots[0]));
            CHECK(near(s.spread.slots[k], s.spread.slots[0]));
        }
    }
}

// ERRO 3: descartar o `rim` como se fosse nil, ou faze-lo pintar. No glifo ele
// e `nil` (o byte 19 de `0x00033DE4`); no chiclet ele ESTA PRESENTE e tem
// `opacity == 0`. As duas coisas somem no pixel e sao diferentes no laudo.
TEST_CASE(the_chiclet_rim_exists_and_paints_nothing) {
    std::size_t n = 0;
    const HighlightSlot* c = chicletHighlightSlots(n);
    REQUIRE(n == 7);
    const HighlightSettings& rim = c[6].settings;
    CHECK(near(rim.brightness, 1.0));
    CHECK(near(rim.opacity.slots[0], 0.0));
    CHECK(near(rim.distance.slots[0], 10.0));
    CHECK(near(rim.spread.slots[0], kPi));
    // `[BIN]` `x27` e reatribuido a 1.0 em `0x00062E68`, entre o `dark` e o
    // `rim` -- o §4.3 do laudo dos realces le 0.5 aqui.
    CHECK(near(rim.bias, 1.0));

    SpecularArguments a;
    a.pixelsPerPoint = 1.0;
    CHECK(near(resolveHighlight(c[6], a).opacity, 0.0));
    // e os outros seis NAO sao zero, senao este caso passaria com tudo apagado.
    for (std::size_t i = 0; i < 6; ++i) CHECK(resolveHighlight(c[i], a).opacity > 0.0);
}

// ERRO 4: o `dark` desenhado uma vez so, ou nos dois lados com o mesmo angulo.
// `0x00030E88` escreve o MESMO ajuste duas vezes, a +pi/2 e a -pi/2, e o
// `brightness == 0` faz os dois cairem em `PlusDarker` sem override nenhum.
TEST_CASE(the_chiclet_dark_is_mirrored_and_subtracts) {
    std::size_t n = 0;
    const HighlightSlot* c = chicletHighlightSlots(n);
    REQUIRE(n == 7);
    CHECK(c[4].isDarklight);
    CHECK(c[5].isDarklight);
    CHECK(near(c[4].angleFromKey, kPi / 2.0));
    CHECK(near(c[5].angleFromKey, -kPi / 2.0));
    CHECK(near(c[4].settings.brightness, 0.0));

    SpecularArguments a;
    a.pixelsPerPoint = 1.0;
    CHECK(resolveHighlight(c[4], a).blendMode == BlendMode::PlusDarker);
    CHECK(resolveHighlight(c[0], a).blendMode == BlendMode::PlusLighter);
    // As duas posicoes DIFUSAS recebem o literal [1,1,1,1] do expansor, nao a
    // curvatura do parametro (`0x00030FD4` / `0x00031154`).
    CHECK(near(c[1].curvature.slots[0], 1.0));
    CHECK(near(c[3].curvature.slots[0], 1.0));
    CHECK(near(c[0].curvature.slots[0], 0.75));
    CHECK(near(c[4].curvature.slots[0], 0.75));
}

// ERRO 5: ler a classe de luminancia ao contrario. `Bright` sai da MAIOR leveza
// contra 0.99 e `Dim` da MENOR contra 0.2, e a sonda do `Dim` so vira a maior
// quando `iconBrightnessOnlyUsesMax` -- que e `false` nesta versao
// (`0x00062AF8`).
TEST_CASE(the_chiclet_luminance_class_reads_the_two_thresholds_the_right_way) {
    // Leveza HSL = (max + min) / 2, por parada. Um par branco/preto tem
    // faixa 0..1 e portanto e Bright pelo topo.
    const std::vector<RampPoint> whiteBlack = {stop(1, 1, 1), stop(0, 0, 0)};
    const ChicletLuminance wb = chicletFillLuminance(whiteBlack);
    CHECK(near(wb.lo, 0.0));
    CHECK(near(wb.hi, 1.0));
    CHECK(classifyChicletAppearance(wb) == ChicletAppearance::Bright);

    // Um cinza escuro so: abaixo de 0.2, portanto Dim.
    const std::vector<RampPoint> dark = {stop(0.1f, 0.1f, 0.1f)};
    CHECK(classifyChicletAppearance(chicletFillLuminance(dark)) == ChicletAppearance::Dim);

    // O mesmo cinza escuro com `onlyUsesMax` continua Dim porque min == max...
    CHECK(classifyChicletAppearance(chicletFillLuminance(dark), true, true) ==
          ChicletAppearance::Dim);
    // ... mas uma rampa que vai do escuro ao medio deixa de ser Dim quando a
    // sonda passa a ser o MAIOR. E este e o unico caso em que o booleano se ve.
    const std::vector<RampPoint> darkToMid = {stop(0.1f, 0.1f, 0.1f), stop(0.5f, 0.5f, 0.5f)};
    const ChicletLuminance dm = chicletFillLuminance(darkToMid);
    CHECK(classifyChicletAppearance(dm, true, false) == ChicletAppearance::Dim);
    CHECK(classifyChicletAppearance(dm, true, true) == ChicletAppearance::Default);

    // O EMPATE VAI PARA DEFAULT NOS DOIS LIMIARES: `b.pl` e `>=`, nao `>`. Os
    // dois casos entram pela faixa e nao por uma cor, porque `0.2f` e `0.99f`
    // nao sao exatos em float e o empate se perderia na conversao -- que e,
    // por sinal, um segundo jeito de errar isto.
    CHECK(classifyChicletAppearance(ChicletLuminance{0.2, 0.2}) == ChicletAppearance::Default);
    CHECK(classifyChicletAppearance(ChicletLuminance{0.99, 0.99}) == ChicletAppearance::Default);
    CHECK(classifyChicletAppearance(ChicletLuminance{0.2, 0.9900001}) ==
          ChicletAppearance::Bright);
    CHECK(classifyChicletAppearance(ChicletLuminance{0.1999999, 0.5}) == ChicletAppearance::Dim);

    // Saturacao nao e leveza: um vermelho puro tem max 1 e min 0, logo 0.5 --
    // um leitor que usasse `max(r,g,b)` sozinho o chamaria de Bright.
    const std::vector<RampPoint> red = {stop(1, 0, 0)};
    CHECK(near(chicletFillLuminance(red).hi, 0.5));
    CHECK(classifyChicletAppearance(chicletFillLuminance(red)) == ChicletAppearance::Default);

    // Um fill que nao e simples nunca classifica: o byte fica 0 (`0x0001A8C0`).
    CHECK(classifyChicletAppearance(wb, false) == ChicletAppearance::Default);

    // Sem paradas, `(0.0, 1.0)` -- `0x0001F2F8`.
    const ChicletLuminance empty = chicletFillLuminance(std::vector<RampPoint>{});
    CHECK(near(empty.lo, 0.0));
    CHECK(near(empty.hi, 1.0));
}

// ERRO 6: acender uma pastilha que nao esta la. `automatic` sob `tinted` e
// `IconColor.clear`, e um realce somado sobre alfa zero inventaria uma pastilha
// transparente luminosa.
TEST_CASE(the_chiclet_highlights_do_not_light_an_empty_pastille) {
    const std::uint32_t n = 64;
    std::vector<float> empty(static_cast<std::size_t>(n) * n * 4, 0.0f);
    SpecularArguments a;
    a.pixelsPerPoint = static_cast<double>(n) / 1024.0;
    CHECK_EQ(drawChicletHighlights(empty, n, a), std::size_t(0));
    for (float v : empty) CHECK(v == 0.0f);

    // E sobre uma pastilha opaca eles DESENHAM -- senao o caso acima passaria
    // com a funcao inteira desligada.
    std::vector<float> solid(static_cast<std::size_t>(n) * n * 4, 0.0f);
    for (std::size_t i = 0; i < solid.size(); i += 4) {
        solid[i] = solid[i + 1] = solid[i + 2] = 0.5f;
        solid[i + 3] = 1.0f;
    }
    clipToChiclet(solid, n);
    const std::size_t moved = drawChicletHighlights(solid, n, a);
    CHECK(moved > 0);

    // A CHAVE ACENDE O TOPO. `angleFromKey == 0` da direcao (0, 1), o shader
    // recebe (x, -y), e a normal de fora no topo e (0, -1) -- produto +1. Se
    // alguem perdesse o `fneg` de `0x0000EF8C` o brilho iria para o rodape.
    const auto lum = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (static_cast<std::size_t>(y) * n + x) * 4;
        return solid[i];
    };
    const std::uint32_t mid = n / 2;
    CHECK(lum(mid, 1) > 0.5f);       // topo, aceso pela chave
    CHECK(lum(mid, n / 2) == 0.5f);  // miolo, longe de qualquer banda
    // As laterais sao ESCURECIDAS pelo `dark` espelhado a +-pi/2, cuja banda
    // vale `10 * n / 1024` px -- 0,625 px aqui, ou seja a coluna de borda e so
    // ela. Uma leitura que perdesse o espelhamento acenderia um lado so.
    CHECK(lum(0, mid) < 0.5f);
    CHECK(lum(n - 1, mid) < 0.5f);
}
