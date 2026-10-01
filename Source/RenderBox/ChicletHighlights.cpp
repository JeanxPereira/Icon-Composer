#include "Source/RenderBox/ChicletHighlights.h"

#include "Source/RenderBox/Parallel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Source/RenderBox/BlendFormula.h"
#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/DistanceField.h"

namespace rb {
namespace {

constexpr double kPi = 3.14159265358979323846;

// O contorno da pastilha como um poligono fechado, na grade do campo.
//
// A mesma subdivisao por cubica que `chicletCoverage` usa, para que a cobertura
// que recorta o fundo e a que recorta os realces venham da MESMA poligonal.
std::vector<FieldContour> chicletContours(std::uint32_t size, IconPlatform platform) {
    using icf::svg::Path;
    using icf::svg::Point;
    using icf::svg::Segment;
    using icf::svg::SegmentKind;

    const ChicletGeometry geom = ChicletGeometry::of(size, platform);
    const Path outline = chicletOutline(geom);
    const int perCubic = chicletSubdivisions(geom.radius);

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

std::vector<FieldContour> chicletFieldContours(std::uint32_t size, IconPlatform platform) {
    return chicletContours(size, platform);
}

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

const HighlightSlot* chicletHighlightSlots(std::size_t& count) {
    // A tabela que este arquivo montava a mao (`chicletMember`, os quatro slots
    // sempre iguais e nenhum override) saiu: a lista e a expansao do
    // `chicletDefault` da geracao 27, montada uma vez em
    // `RenderingParameters.cpp` -- os mesmos sete, na mesma ordem.
    const std::vector<HighlightSlot>& slots = expandedHighlights(
        DesignGeneration::G27, HighlightFamily::Chiclet, HighlightsSetKind::Default);
    count = slots.size();
    return slots.data();
}

HighlightsSetKind chicletHighlightsSetFor(ChicletHighlightsAppearanceMode mode,
                                          bool appearanceIsDark, bool colourMode,
                                          ChicletAppearance iconBrightness,
                                          bool effectiveClearModeIsNil) {
    // `[BIN]` `0x000625B4`-`0x000625BC`: `cmp w8, #1; b.ne` -- tudo que nao e
    // `chicletLuminance` cai no ramo da aparencia.
    if (mode != ChicletHighlightsAppearanceMode::ChicletLuminance) {
        // `[BIN]` `0x00062698`-`0x000626AC` e `0x00062760`: `+0x61 == 1` e
        // `chicletDim` (`+0xD78`), senao `chicletDefault` (`+0x118`).
        return appearanceIsDark ? HighlightsSetKind::Dim : HighlightsSetKind::Default;
    }
    if (colourMode) {
        // `[BIN]` `0x00062744`-`0x00062764`.
        if (iconBrightness == ChicletAppearance::Default) return HighlightsSetKind::Default;
        if (iconBrightness == ChicletAppearance::Bright) return HighlightsSetKind::Bright;
        return HighlightsSetKind::Dim;
    }
    // `[BIN]` `0x00062684`-`0x00062740`: `x23` e `0x19D8` (`chicletScreened`) e
    // `x22` e `0x13A8` (`chicletClear`).
    return effectiveClearModeIsNil ? HighlightsSetKind::Screened : HighlightsSetKind::Clear;
}

ChicletCone chicletHighlightCone(double spread) {
    ChicletCone out;
    // `[BIN]` `0x0000DBA0`-`0x0000DBBC`, na ordem das instrucoes: metade, pela
    // constante pi, menos meio, em modulo.
    const double offPi = std::fabs(spread * 0.5 / kPi + -0.5);
    out.alwaysLit = offPi < 1e-6;
    // `[BIN]` `0x0000DB80`: o cosseno do cone, sem a sentinela do shader do
    // glifo.
    out.cone = std::cos(spread);
    return out;
}

GlassHighlightSettings resolveChicletHighlight(const HighlightSlot& slot,
                                               const SpecularArguments& args) {
    GlassHighlightSettings g = resolveHighlight(slot, args);
    // `[BIN]` `0x0000D904` nunca le `+0x38`. Zero nos dez conjuntos do chiclet
    // das duas geracoes; escrito para que um conjunto que o tivesse nao
    // deslocasse a banda.
    g.inset = 0.0;
    return g;
}

double chicletHighlightFragment(const GlassHighlightSettings& s, double sd, double nx,
                                double ny) {
    const ChicletCone c = chicletHighlightCone(s.spread);
    // `fwidth(sd)` de um campo de inclinacao um por pixel, como em
    // `drawSpecular`.
    return highlightFragment(s, c.cone, c.alwaysLit, sd, nx, ny, 1.0);
}

std::size_t drawChicletHighlights(std::vector<float>& rgba, const PixelGrid& grid,
                                  const SpecularArguments& args, IconPlatform platform) {
    const std::size_t n = grid.texels();
    if (grid.size == 0 || grid.width == 0 || grid.height == 0 || rgba.size() < n * 4) return 0;

    // O contorno e o do canvas inteiro; o campo e que nasce recortado, com a
    // origem do buffer, e amostra as linhas absolutas.
    const std::vector<FieldContour> contours = chicletContours(grid.size, platform);
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
    FieldOptions fo;
    fo.originX = grid.originX;
    fo.originY = grid.originY;
    const FieldImage field = generateFieldFromContours(contours, grid.width, grid.height, fo);
    if (field.width == 0) return 0;

    // A lista da geracao e do conjunto que quem chama escolheu.
    const std::vector<HighlightSlot>& list =
        expandedHighlights(args.generation, HighlightFamily::Chiclet, args.set);
    const std::size_t count = list.size();
    const HighlightSlot* slots = list.data();
    std::vector<char> hit(n, 0);

    for (std::size_t s = 0; s < count; ++s) {
        const GlassHighlightSettings g = resolveChicletHighlight(slots[s], args);
        // O `rim` da geracao 27 sai aqui: `opacity == 0`, o unico dos sete que
        // nao pinta.
        if (g.opacity <= 0.0 || g.height <= 0.0) continue;

        // Uma linha por worker, com junta antes do proximo realce: cada pixel
        // le e escreve so os proprios floats, e ve os realces na mesma ordem.
        parallelRanges(grid.height, n * 24, [&](std::size_t y0, std::size_t y1) {
        for (std::uint32_t y = static_cast<std::uint32_t>(y0); y < static_cast<std::uint32_t>(y1); ++y) {
            for (std::uint32_t x = 0; x < grid.width; ++x) {
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
                // `[INF]` O recorte aqui e o ALFA QUE A IMAGEM JA TEM, e nao a
                // cobertura do contorno, por dois motivos que sao o mesmo: o
                // alfa do fundo JA e a cobertura (`clipToChiclet` a multiplicou
                // nele), e uma pastilha transparente -- o que `automatic` sob
                // `tinted` produz, que e `IconColor.clear` -- nao tem superficie
                // para acender. Ler a cobertura do campo em vez do alfa poria
                // luz sobre o nada nesse caso. (Desde 01/10 a passada vem
                // DEPOIS dos grupos, a ordem do alvo -- `IconRenderer.cpp` --,
                // entao o alfa lido e o do fundo com o que os grupos puseram
                // por cima; sobre um fundo opaco e o mesmo numero.)
                const double clip = rgba[(static_cast<std::size_t>(y) * grid.width + x) * 4 + 3];
                if (clip <= 0.0) continue;

                const double f = chicletHighlightFragment(g, sd, nx, ny);
                if (f <= 0.0) continue;

                const double alpha = f * g.opacity * clip;
                BlendColour src;
                src.rgba[0] = g.colour[0] * alpha;
                src.rgba[1] = g.colour[1] * alpha;
                src.rgba[2] = g.colour[2] * alpha;
                src.rgba[3] = alpha;

                const std::size_t px = static_cast<std::size_t>(y) * grid.width + x;
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
        });
    }
    std::size_t touched = 0;
    for (std::size_t i = 0; i < n; ++i) touched += static_cast<std::size_t>(hit[i]);
    return touched;
}

std::size_t drawChicletHighlights(std::vector<float>& rgba, std::uint32_t size,
                                  const SpecularArguments& args, IconPlatform platform) {
    return drawChicletHighlights(rgba, PixelGrid::full(size), args, platform);
}

const char* const kChicletRasteriserNote =
    "`[INF]` o alvo rasteriza os realces do chiclet com um gradiente conico numa camada recortada "
    "(0x0000D904: beginLayer 0xE0F0, clipLayerWithAlpha 0xE134, setConicGradient 0xE448, "
    "drawShape 0xE490) e nao com o shader glassHighlight -- aqui os mesmos ajustes resolvidos "
    "por 0x4BD90 sao avaliados sobre o campo de distancia do contorno continuo. Do rasterizador "
    "lido entram duas coisas: o cone de pi exato acende o contorno inteiro (0xDBA0-0xDC54) e o "
    "inset nao e lido. `[OBS]` O resto fica como estava: o perfil radial (o modo de renderizacao "
    "2 do RBShape e o clipLayerWithAlpha mode:1 nao foram lidos; aqui e o shade do shader do "
    "glifo), o angulo do conico (aqui a normal do contorno, que diverge do angulo polar do "
    "centro perto dos cantos), a uniao dos realces de uma passada numa rampa so (aqui um por "
    "vez) e os pisos 1e-6 (aqui o 2^-10 do shader)";

std::string chicletHighlightsNote(DesignGeneration generation, HighlightsSetKind set,
                                  ChicletAppearance appearance, const ChicletLuminance& l) {
    const char* const kSetNames[5] = {"chicletDefault", "chicletBright", "chicletDim",
                                      "chicletClear", "chicletScreened"};
    const int kind = static_cast<int>(set);
    const char* name = kSetNames[kind < 0 ? 0 : (kind > 4 ? 4 : kind)];
    const char* klass = "Default";
    if (appearance == ChicletAppearance::Bright) klass = "Bright";
    if (appearance == ChicletAppearance::Dim) klass = "Dim";

    char lum[96];
    std::snprintf(lum, sizeof(lum), "%.4f..%.4f", l.lo, l.hi);

    std::string out;
    if (generation == DesignGeneration::G26) {
        out += "realces do chiclet desenhados (geracao 26): `[BIN]` o conjunto `";
        out += name;
        out += "` que 0x76FC0 reescreve em ICRRenderingParameters.Highlights -- keySharp "
               "(brightness 1.1, cone de 78 graus) e fillSharp (1.1, 65 graus, a pi de distancia "
               "angular) e o rim (1.0, cone pi: o contorno inteiro), os tres com distance "
               "22/22/30/39 pt e a opacidade por classe de tamanho, bias 0.5, curvatura 1 e "
               "blendModeOverride == .normal: o realce COBRE em vez de somar. keyDiffuse e dark sao "
               "nil. `[BIN]` O seletor 0x62588 esta no ramo systemAppearance "
               "(chicletHighlightsAppearanceMode == 0, 0x77298): aparencia escura escolhe "
               "chicletDim, senao chicletDefault, sem olhar o modo de renderizacao nem a classe de "
               "luminancia (medida em ";
        out += lum;
        out += ", `";
        out += klass;
        out += "`, com iconBrightnessOnlyUsesMax ligado -- ela escolhe o conjunto do GLIFO). A luz "
               "vem de defaultChicletLight.longitude = -pi/4 (0x771E0). `[OBS]` a aparencia escura "
               "aqui e o contexto `dark` ou a rendicao Tinted Dark; o Clear Dark nao chega ao "
               "render como aparencia; `[OBS]` o portao real e o byte ctx+0x21 (0x475C8), sem nome "
               "no metadado -- aqui os realces saem sempre que ha pastilha; ";
        out += kChicletRasteriserNote;
        return out;
    }
    if (set == HighlightsSetKind::Clear || set == HighlightsSetKind::Screened) {
        out += "realces do chiclet desenhados: `[BIN]` o conjunto `";
        out += name;
        out += "` de ICRRenderingParameters.Highlights, que o seletor 0x62588 escolhe FORA de "
               ".color (0x62684-0x62740: chicletScreened quando o modo efetivo do Clear e nil -- o "
               "Tinted Dark --, chicletClear quando nao e): keySharp (brightness 1.0, distance "
               "10 pt, cone de 100 graus, bias 0.5) e keyDiffuse (44 pt, 45 graus), os dois fills "
               "FillHighlights.matchKey -- os mesmos dois a pi de distancia angular --, o `dark` "
               "duas vezes a +-pi/2 (cone de 65 graus, distance 9/8/8/9 pt) e o rim nil "
               "(0x63624-0x63AEC); as opacidades sao 1.25 / 0.5 e [0.25, 0.1, 0.1, 0] no Clear e "
               "[0.6, 0.7, 0.7, 0.7] / [0.25, 0.3, 0.3, 0.3] e [0.25, 0, 0, 0] no Screened. `[BIN]` "
               "Sob a mascara do Clear os claros saem na cor (0.7, 0, 0) em plusLighter e os "
               "escuros ficam para o sistema (0x478B4, 0x47874). `[OBS]` o portao real e o byte "
               "ctx+0x21 (0x475C8), sem nome no metadado -- aqui os realces saem sempre que ha "
               "pastilha; ";
        out += kChicletRasteriserNote;
        return out;
    }
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
    out += klass;
    out += "`; `[BIN]` ela nao move pixel nesta geracao porque 0x62A78-0x63608 monta "
           "chicletDefault, chicletBright e chicletDim dos MESMOS valores (a primeira constante "
           "nova do construtor so aparece em 0x63624, ja dentro de chicletClear). `[OBS]` o "
           "portao real e o byte ctx+0x21 (0x475C8), sem nome no metadado -- aqui os realces "
           "saem sempre que ha pastilha; ";
    out += kChicletRasteriserNote;
    return out;
}

}  // namespace rb
