#include "Source/RenderBox/ChicletHighlights.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Source/RenderBox/BlendFormula.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/DistanceField.h"

namespace rb {
namespace {

constexpr double kPi = 3.14159265358979323846;

HighlightSizeValue flat(double all, bool present = true) {
    HighlightSizeValue v;
    v.slots[0] = v.slots[1] = v.slots[2] = v.slots[3] = all;
    v.present = present;
    return v;
}

// `[BIN]` Um membro de `chicletDefault`, montado do jeito que
// `0x00062AB8`-`0x00062EDC` monta: os quatro slots iguais, `outsetOpacity` e
// `minInsetPixels` `nil`, `inset` e `minDistancePixels` zerados,
// `blendModeOverride` ausente (o byte 18).
HighlightSettings chicletMember(double brightness, double opacity, double distance, double spread,
                                double bias) {
    HighlightSettings s;
    s.brightness = brightness;
    s.opacity = flat(opacity);
    s.outsetOpacity = flat(0.0, false);
    s.distance = flat(distance);
    s.minDistancePixels = flat(0.0);
    s.inset = flat(0.0);
    s.minInsetPixels = flat(0.0, false);
    s.spread = flat(spread);
    s.bias = bias;
    s.hasBlendModeOverride = false;
    return s;
}

const HighlightSlot* buildChicletSlots(std::size_t& count) {
    static HighlightSlot slots[7];
    static bool built = false;
    if (!built) {
        // `[BIN]` `Highlights+0x50` e `+0x70`, ambos `[0.75]x4`
        // (`fmov v0.2d, #0.75` em `0x00062ABC`, gravado quatro vezes em
        // `0x00062ADC`-`0x00062AE0`). O seletor do chiclet passa estes dois ao
        // expansor em `0x00062788`-`0x0006278C`.
        const HighlightSizeValue chicletCurvature = flat(0.75);
        // `[BIN]` As duas posicoes DIFUSAS do expansor nao recebem a curvatura
        // do parametro e sim o literal `[1,1,1,1]` -- `0x00030FD4` e
        // `0x00031154`.
        const HighlightSizeValue one = flat(1.0);

        // `[BIN]` `0x00062BC8` -> chicletDefault+0x000
        const HighlightSettings keySharp = chicletMember(1.1, 0.2, 10.0, 2.0 * kPi / 3.0, 0.5);
        // `[BIN]` `0x00062C64` -> +0x108
        const HighlightSettings keyDiffuse = chicletMember(1.1, 0.5, 40.0, kPi / 2.0, 0.08);
        // `[BIN]` `0x00062D08` -> +0x210. Identico ao `keySharp`, inclusive no
        // cone -- ao contrario do glifo, onde `fillSharp` estreita para pi/3.
        const HighlightSettings fillSharp = chicletMember(1.1, 0.2, 10.0, 2.0 * kPi / 3.0, 0.5);
        // `[BIN]` `0x00062DBC` -> +0x318. PRESENTE no chiclet; `nil` no glifo.
        const HighlightSettings fillDiffuse = chicletMember(1.1, 0.25, 40.0, kPi / 2.0, 0.08);
        // `[BIN]` `0x00062E58` -> +0x420. `brightness == 0` faz o modo de
        // mescla cair em `PlusDarker` por `0x0004C0A0`, sem override.
        const HighlightSettings dark = chicletMember(0.0, 0.2, 10.0, kPi / 3.0, 0.5);
        // `[BIN]` `0x00062ED0` -> +0x528. PRESENTE, com `opacity == 0`: ele
        // existe e nao pinta. `bias == 1.0` (`x27` reatribuido em `0x00062E68`).
        const HighlightSettings rim = chicletMember(1.0, 0.0, 10.0, kPi, 1.0);

        // `[BIN]` As sete posicoes de `0x00030E88`, na ordem em que ele as
        // escreve (`x19+0x20 + i*0x138`).
        slots[0] = HighlightSlot{keySharp, 0.0, chicletCurvature, false};
        slots[1] = HighlightSlot{keyDiffuse, 0.0, one, false};
        slots[2] = HighlightSlot{fillSharp, kPi, chicletCurvature, false};
        slots[3] = HighlightSlot{fillDiffuse, kPi, one, false};
        slots[4] = HighlightSlot{dark, kPi / 2.0, chicletCurvature, true};
        slots[5] = HighlightSlot{dark, -kPi / 2.0, chicletCurvature, true};
        slots[6] = HighlightSlot{rim, 0.0, chicletCurvature, false};
        built = true;
    }
    count = 7;
    return slots;
}

// O contorno da pastilha como um poligono fechado, na grade do campo.
//
// A mesma subdivisao por cubica que `chicletCoverage` usa, para que a cobertura
// que recorta o fundo e a que recorta os realces venham da MESMA poligonal.
std::vector<FieldContour> chicletContours(std::uint32_t size) {
    using icf::svg::Path;
    using icf::svg::Point;
    using icf::svg::Segment;
    using icf::svg::SegmentKind;

    const double r = chicletCornerRadius(size);
    const Path outline = continuousRoundedRect(0.0, 0.0, size, size, r, r);
    const int perCubic = std::clamp(static_cast<int>(std::ceil(r * 0.5)), 8, 96);

    FieldContour c;
    Point cur{0.0, 0.0};
    bool first = true;
    auto push = [&](const Point& p) {
        c.xy.push_back(static_cast<float>(p.x));
        c.xy.push_back(static_cast<float>(p.y));
    };
    for (const Segment& s : outline.segments) {
        switch (s.kind) {
            case SegmentKind::Move:
                cur = s.p[0];
                if (first) {
                    push(cur);
                    first = false;
                }
                break;
            case SegmentKind::Line:
                push(s.p[0]);
                cur = s.p[0];
                break;
            case SegmentKind::Cubic: {
                for (int i = 1; i <= perCubic; ++i) {
                    const double t = static_cast<double>(i) / perCubic;
                    const double u = 1.0 - t;
                    const double a = u * u * u, b = 3 * u * u * t, cc = 3 * u * t * t,
                                 d = t * t * t;
                    push(Point{a * cur.x + b * s.p[0].x + cc * s.p[1].x + d * s.p[2].x,
                               a * cur.y + b * s.p[0].y + cc * s.p[1].y + d * s.p[2].y});
                }
                cur = s.p[2];
                break;
            }
            case SegmentKind::Close:
                break;
        }
    }
    // O segmento de fechamento e implicito; uma repeticao do primeiro ponto no
    // fim seria um segmento de comprimento zero.
    if (c.xy.size() >= 4) {
        const std::size_t n = c.xy.size();
        if (c.xy[n - 2] == c.xy[0] && c.xy[n - 1] == c.xy[1]) {
            c.xy.resize(n - 2);
        }
    }
    std::vector<FieldContour> out;
    if (c.xy.size() >= 6) out.push_back(std::move(c));
    return out;
}

}  // namespace

ChicletLuminance chicletFillLuminance(const std::vector<RampPoint>& stops) {
    ChicletLuminance out;
    if (stops.empty()) {
        // `[BIN]` `0x0001F2F8`-`0x0001F2FC`: um fill sem paradas devolve
        // `(0.0, 1.0)`, que e a faixa mais larga possivel e portanto nem
        // `Dim` nem `Bright` sozinha -- `Bright` porque `0.99 < 1.0`, na
        // verdade. O alvo escreve exatamente isso.
        return out;
    }
    bool first = true;
    for (const RampPoint& s : stops) {
        const double r = s.rgba[0], g = s.rgba[1], b = s.rgba[2];
        const double hi = std::max(std::max(r, g), b);
        const double lo = std::min(std::min(r, g), b);
        const double L = (hi + lo) * 0.5;
        if (first) {
            out.lo = out.hi = L;
            first = false;
        } else {
            out.lo = std::min(out.lo, L);
            out.hi = std::max(out.hi, L);
        }
    }
    return out;
}

ChicletLuminance chicletFillLuminance(const float rgb[3]) {
    const double r = rgb[0], g = rgb[1], b = rgb[2];
    const double hi = std::max(std::max(r, g), b);
    const double lo = std::min(std::min(r, g), b);
    const double L = (hi + lo) * 0.5;
    ChicletLuminance out;
    out.lo = out.hi = L;
    return out;
}

ChicletAppearance classifyChicletAppearance(const ChicletLuminance& l, bool simpleFill,
                                            bool onlyUsesMax, double maxDimLuminance,
                                            double minBrightLuminance) {
    // `[BIN]` `0x0001A89C`-`0x0001A8B4`: o teste de fill simples e o portao da
    // classificacao inteira; o ramo de fora escreve `0` em `0x0001A8C0`.
    if (!simpleFill) return ChicletAppearance::Default;
    // `[BIN]` `0x0001A958`: `fcmp minBright, MAX` e `b.pl` -- cai em `Bright`
    // quando `minBright < MAX`, ou seja quando a maior leveza PASSA do limiar.
    if (minBrightLuminance < l.hi) return ChicletAppearance::Bright;
    // `[BIN]` `0x0001A96C`: `fcsel ... ne` com `iconBrightnessOnlyUsesMax`.
    const double dimProbe = onlyUsesMax ? l.hi : l.lo;
    // `[BIN]` `0x0001A970`-`0x0001A978`: `Dim` quando a sonda fica ABAIXO do
    // limiar; empate vai para `Default`, porque `b.pl` cobre o `>=`.
    if (dimProbe < maxDimLuminance) return ChicletAppearance::Dim;
    return ChicletAppearance::Default;
}

const HighlightSlot* chicletHighlightSlots(std::size_t& count) { return buildChicletSlots(count); }

std::size_t drawChicletHighlights(std::vector<float>& rgba, std::uint32_t size,
                                  const SpecularArguments& args) {
    const std::size_t n = static_cast<std::size_t>(size) * size;
    if (size == 0 || rgba.size() < n * 4) return 0;

    const std::vector<FieldContour> contours = chicletContours(size);
    if (contours.empty()) return 0;
    // UMA AMOSTRA POR PIXEL, e aqui a escolha foi medida contra o gabarito e nao
    // herdada. Este campo e o unico do render cujo custo NAO cresce com o
    // documento -- a pastilha e a mesma forma em todo render, e ele e gerado uma
    // vez por render, nao uma por camada. Isso o tornava o candidato obvio a
    // `superSample = 3`: o custo e fixo e cacheavel.
    //
    // Medido mesmo assim, porque candidato obvio nao e numero. Com `ss = 3` o
    // erro medio por canal contra `apple-512.png` PIORA nos quatro canais --
    // R 8,8416 -> 8,8468, G 10,1735 -> 10,1786, B 9,8461 -> 9,8513,
    // A 4,9520 -> 4,9560 -- movendo 2,92 % dos pixels com pior delta de canal
    // 29, e cobra +0,09 s em 412 e +0,59 s em 1024 (Release, os mesmos valores
    // no AppIcon-27 e no GoWToolkit, que e o que se espera de um campo por
    // render). Um realce mais bem orientado sobre uma pastilha que o alvo
    // desenha com um GRADIENTE CONICO numa camada recortada (`[INF]` 0x0000D904,
    // ver a nota do realce) nao se aproxima do alvo por ficar mais exato.
    //
    // `[OBS]` Que este campo seja recalculado a cada render de uma forma FIXA
    // continua sendo desperdicio mesmo em `ss = 1`; nao existe cache hoje e o
    // custo do campo em `ss = 1` nao foi isolado. Cachear por `size` e a
    // economia obvia, e e independente desta decisao.
    const FieldImage field = generateFieldFromContours(contours, size, size);
    if (field.width == 0) return 0;

    std::size_t count = 0;
    const HighlightSlot* slots = chicletHighlightSlots(count);
    std::vector<char> hit(n, 0);

    for (std::size_t s = 0; s < count; ++s) {
        const GlassHighlightSettings g = resolveHighlight(slots[s], args);
        // `rim` sai aqui: `opacity == 0` e o unico dos seis que nao pinta.
        if (g.opacity <= 0.0 || g.height <= 0.0) continue;

        for (std::uint32_t y = 0; y < size; ++y) {
            for (std::uint32_t x = 0; x < size; ++x) {
                const float* p = field.at(x, y);
                // O campo e NEGATIVO DENTRO e o `sd` do shader e positivo
                // dentro -- o mesmo giro que `drawSpecular` faz.
                const double sd = -static_cast<double>(p[0]);
                if (sd < -2.0) continue;  // fora da pastilha, alem da banda de AA
                const double nx = p[1];
                const double ny = p[2];
                if (nx == 0.0 && ny == 0.0) continue;
                // `[BIN]` `0x0000E134` `clipLayerWithAlpha:mode:` -- o realce do
                // chiclet vive dentro de uma camada RECORTADA, e `0x00047948`
                // (`fcmp s0, #0.0 ; b.le`) pula o desenho quando a opacidade
                // resolvida nao passa de zero.
                //
                // `[INF]` O recorte aqui e o ALFA QUE O FUNDO JA TEM, e nao a
                // cobertura do contorno, por dois motivos que sao o mesmo: o
                // alfa do fundo JA e a cobertura (`clipToChiclet` a multiplicou
                // nele), e uma pastilha transparente -- o que `automatic` sob
                // `tinted` produz, que e `IconColor.clear` -- nao tem superficie
                // para acender. Ler a cobertura do campo em vez do alfa poria
                // luz sobre o nada nesse caso.
                const double clip = rgba[(static_cast<std::size_t>(y) * size + x) * 4 + 3];
                if (clip <= 0.0) continue;

                const double f = glassHighlightFragment(g, sd, nx, ny, 1.0);
                if (f <= 0.0) continue;

                const double alpha = f * g.opacity * clip;
                BlendColour src;
                src.rgba[0] = g.colour[0] * alpha;
                src.rgba[1] = g.colour[1] * alpha;
                src.rgba[2] = g.colour[2] * alpha;
                src.rgba[3] = alpha;

                const std::size_t px = static_cast<std::size_t>(y) * size + x;
                const std::size_t i = px * 4;
                BlendColour dst;
                for (int c = 0; c < 4; ++c) dst.rgba[c] = rgba[i + c];
                const BlendColour outc = blend(g.blendMode, src, dst);
                for (int c = 0; c < 4; ++c) {
                    const double v = outc.rgba[c] < 0.0 ? 0.0 : outc.rgba[c];
                    if (static_cast<float>(v) != rgba[i + c]) hit[px] = 1;
                    rgba[i + c] = static_cast<float>(v);
                }
            }
        }
    }
    std::size_t touched = 0;
    for (std::size_t i = 0; i < n; ++i) touched += static_cast<std::size_t>(hit[i]);
    return touched;
}

std::string chicletHighlightsNote(ChicletAppearance appearance, const ChicletLuminance& l) {
    const char* name = "chicletDefault";
    if (appearance == ChicletAppearance::Bright) name = "chicletBright";
    if (appearance == ChicletAppearance::Dim) name = "chicletDim";

    char lum[96];
    std::snprintf(lum, sizeof(lum), "%.4f..%.4f", l.lo, l.hi);

    std::string out;
    out += "realces do chiclet desenhados: `[BIN]` SEIS realces vivos do conjunto `";
    out += name;
    out += "` de ICRRenderingParameters.Highlights -- keySharp e fillSharp (brightness 1.1, "
           "opacity 0.2, distance 10 pt, cone 2pi/3, bias 0.5, a pi de distancia angular), "
           "keyDiffuse e fillDiffuse (40 pt, pi/2, 0.08, opacity 0.5 e 0.25) e o `dark` duas "
           "vezes a +-pi/2 (brightness 0 e portanto plusDarker); o setimo, `rim`, existe com "
           "opacity 0 e por isso nao pinta. A classe de luminancia foi MEDIDA "
           "(0x1A920-0x1A97C sobre 0x1F10C: leveza HSL por parada, faixa ";
    out += lum;
    out += " contra maxDim 0.2 / minBright 0.99) e deu `";
    out += name;
    out += "`; `[BIN]` ela nao move pixel nesta versao porque 0x62A78-0x63608 monta "
           "chicletDefault, chicletBright e chicletDim dos MESMOS valores (a primeira constante "
           "nova do construtor so aparece em 0x63624, ja dentro de chicletClear). `[OBS]` o "
           "portao real e o byte ctx+0x21 (0x475C8), sem nome no metadado -- aqui os realces "
           "saem sempre que ha pastilha; `[OBS]` chicletClear/chicletScreened (0x13A8/0x19D8) "
           "nao estao transcritos e quem escolhe entre eles (0x40E60/0x6D6B0) nao foi seguido; "
           "`[INF]` o alvo rasteriza isto com um gradiente conico numa camada recortada "
           "(0x0000D904: beginLayer 0xE0F0, clipLayerWithAlpha 0xE134, setConicGradient 0xE448, "
           "drawShape 0xE490) e nao com o shader glassHighlight -- aqui os mesmos ajustes "
           "resolvidos por 0x4BD90 sao avaliados sobre o campo de distancia do contorno "
           "continuo, o que pode divergir do alvo na queda ANGULAR perto dos cantos";
    return out;
}

}  // namespace rb
