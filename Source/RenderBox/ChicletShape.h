#pragma once
// The chiclet: the continuous-corner rounded square an icon is cut to.
//
// WHY THIS FILE EXISTS
// --------------------
// Until this file the background was painted over the whole canvas square and
// `kBackgroundShapeNote` said, truthfully, that the corner geometry had not been
// read. It has been now. `Docs/Laudos/2026-09-15-chiclet-curva.md` closes the
// one thing `Docs/Laudos/2026-09-15-chiclet.md` left open -- WHICH CURVE -- and
// everything below is a transcription of that reading, not a squircle taken
// from folklore.
//
// THE THREE READINGS THIS RESTS ON
// --------------------------------
//  1. `[BIN]` `RenderBox.arm64`, `set_rounded_rect` `0x21780`-`0x217A4`: a
//     continuous corner (`cornerStyle == 1`) is STORED multiplied by 1.275 and
//     tagged shape type 4.
//
//  2. `[BIN]` That multiply is a storage convention and EVERY reader undoes it,
//     guarded on the type byte being 4:
//        `Coverage::Primitive::add_path`   `0x96838` -- x 0.7843137
//        `Coverage::Primitive::encode`     `0x94550` -- x 0.7843137
//        `Coverage::Primitive::decode`     `0x94834` -- x 1.275 on the way back
//        `Coverage::Primitive::set_globals` `0x95A94` -- x 0.7843137
//     `add_path` is the encoder the older laudo asked for: it is what turns the
//     stored shape into `RBPathMakeRoundedRect` (`0x96880`), whose element is
//     `8 | continuous` (`0x118E2C`) -- the `RBPathElement` 9 that
//     `Mapper::apply_callback` (`0x80DCC`) hands to `add_rounded_rect`. It
//     DIVIDES. So the radius that reaches the curve is the radius the caller
//     asked for, and for the chiclet that is 266.24, not 339.456.
//
//  3. `[BIN]` `add_rounded_rect` `0x7F580`-`0x7FE58` then picks the curve by a
//     per-EDGE slack, `0x7F664`-`0x7F690`:
//        t = (|edge| - (r_near + r_far)) / ((r_near + r_far) * 0.528664947)
//     With r = 266.24 on a 1024 edge, t = 1.746 >= 1 -- the CANONICAL
//     continuous corner, not the blended one. Reading (1) alone would have given
//     t = 0.9615 and a visibly different corner; that is the fork this file
//     stands on the far side of.
//
// WHICH OF THE TARGET'S TWO PATHS THIS IS
// ---------------------------------------
// `[BIN]` The target draws an icon in one of two framings, and this file is the
// first one:
//
//  * FULL BLEED -- the chiclet fills the canvas square. This is what
//    `Configuration(icon:style:parametersOverride:)` (`IconRendering 0x19F4C`)
//    sets up, writing `useLegacyInsetting = 0` at `0x1A844`, and it is the ONLY
//    one Icon Composer itself ever asks for: `IconComposerKit` imports that init
//    and not the one that takes the flag.
//
//  * LEGACY INSET -- `0x4202C` shrinks the rect to
//    `side - 2 * floor(side * (relativeIconInset ?? 100/1024))`, the `100/1024`
//    being the immediate `0x3FB9000000000000` at `0x4224C` (the only one in the
//    bundle). That is the framing of the rendition Apple shipped inside
//    `Assets.car`, and of `AppIcon.icns`.
//
// So the 824/1024 of the oracle is NOT missing from this file: it belongs to a
// mode of the target that its own app never turns on, and
// `scripts/png-diff.py --legacy-inset` aligns the two framings for comparison
// instead.
//
// `[OBS]` In that legacy rendition the corner is the SAME curve with a SMALLER
// radius: fitted by `scripts/chiclet-profile.py`, Apple's 412 body wants
// `r = 0.225 * body` (RMS 0.088 px) where `0.26` costs 5.36 px, and our corner
// therefore starts 4.1 px earlier along the diagonal. No constant in the bundle
// produces 0.225 -- platform `main` has no radius override (`0x5EB38`), and the
// one mechanism that can replace the shape is a caller-supplied
// `GlobalConfiguration.iconShape` (`+0x58`, tested by `cbz` at `0x4296C`).
// `Docs/Laudos/2026-09-15-chiclet-geometria.md` has the measurement; the radius
// below stays 0.26 because that is what the reading of THIS path says.
#include <cstdint>
#include <vector>

#include "Source/CoreSVG/Path.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/PixelGrid.h"

namespace rb {

// The three numbers that shape one END of one edge.
//
// `[BIN]` `0x7F694`-`0x7F6F8`. `extent` is how much of the edge the corner eats,
// measured in radii; `control` and `shoulder` place the two control points that
// leave the edge. At t >= 1 they are the canonical triple; below it they are a
// linear blend that reproduces the canonical triple exactly at t == 1 -- which
// is what validates both constant pools at once.
struct ContinuousCornerParams {
    double extent = 1.5286649465560913;    // `0x15EBC0` lane 0
    double control = 1.0884900093078613;   // `0x15EBC0` lane 1
    double shoulder = 0.8684070110321045;  // `0x15EC78`
};

// The slack of one edge, and the triple it selects. `edge` is the edge's length;
// `rNear` and `rFar` are the two corner radii MEASURED ALONG THAT EDGE.
//
// The division is done in float, as the target does it: the comparison against
// 1.0 is a cliff, and doing it in double would move the cliff.
ContinuousCornerParams continuousCornerParams(double edge, double rNear, double rFar);

// The outline of a continuous rounded rect: four lines and twelve cubics, the
// count `add_rounded_rect` emits for `RBPathElement` 9.
//
// The subpath starts at the middle of the right edge, as the target's does
// (`0x7F634`) -- a seam in the middle of an edge rather than inside a corner.
icf::svg::Path continuousRoundedRect(double x, double y, double w, double h,
                                     double rx, double ry);

// `[BIN]` `GlassRenderingParameters::defaultChicletCornerRadius` is 266.24 at
// `+0x228`, against the 1024 canvas of `Platform.extendedCanvasBounds`
// (`IconComposerFoundation 0x385D8`). 266.24 / 1024 is exactly 0.26, so the
// radius scales with the canvas without a second constant.
double chicletCornerRadius(std::uint32_t size);

// ─── A PLATAFORMA TROCA A FORMA, E O WATCHOS É UM CÍRCULO ────────────────────
//
// `[BIN]` `ICRRenderingParameters.platformOverrides` (`params + 0x248`) tem, nos
// defaults de `0x5EB38`-`0x5EBA0`, DUAS entradas de 32 bytes, e o laudo
// `Docs/Laudos/2026-09-15-chiclet-geometria.md` §1.4 as leu:
//
//     chave 1 = watchOS   cornerRadius 512,0   usesHalfPixelInset true
//     chave 2 = tvOS      cornerRadius nil     aspectRatio 5/3
//
// `512,0` num canvas de 1024 é metade do lado: o canto contínuo come a aresta
// inteira e **a pastilha do watchOS é um círculo**. A tabela é consultada em
// `0x42A24` e o que ela não cobre cai no `defaultChicletCornerRadius`, isto é,
// **a plataforma `main` -- onde iOS e macOS vivem -- não tem override nenhum**.
//
// Até 20/09 nada disto estava ligado: `renderIcon` recortava TODO idioma ao
// mesmo chiclet de 0,26, então o ícone de watchOS saía quadrado. Era a metade
// visual do defeito que `icf::resolve` era a outra (IconDocument.h).
//
// `tvOS` não está aqui, e a ausência é a afirmação: ele quer um canvas 5/3 e
// `icf::Idiom` não tem um caso que chegue nele -- um enum com um caso que
// nenhum documento alcança seria forma sem leitor.
enum class IconPlatform { Main, WatchOS };

// `[ART]` A ponte entre o vocabulário do arquivo e o da tabela. `watchOS` é o
// único idioma que a tabela nomeia; os outros quatro são `main` -- e isso é
// leitura, não medição, porque `Base` e `Square` não existem do outro lado (a
// mesma fronteira que `ick::renditionValidFor` já declara).
IconPlatform iconPlatformOf(icf::Idiom idiom);

// O retângulo e o raio que a plataforma resolve, em pixels da grade de `size`.
//
// `[BIN]` `0x422BC`-`0x422DC`: `d15 = 2 × (0,5 / escala)` é subtraído do lado
// -- meio PIXEL por aresta -- e só quando `usesHalfPixelInset` do override está
// ligado, que hoje é só o watchOS (byte `+0x19` da entrada).
struct ChicletGeometry {
    double origin = 0.0;  // o canto superior esquerdo, em pixels
    double side = 0.0;
    double radius = 0.0;

    static ChicletGeometry of(std::uint32_t size, IconPlatform platform = IconPlatform::Main);
};

// O contorno dessa geometria. Uma função só, porque a cobertura que recorta o
// fundo e os contornos que alimentam os realces têm de sair da MESMA poligonal.
//
// `[BIN]` `continuousRoundedRect` já grampeia o raio a meia aresta
// (`RB::clamp_corner_radii`, `0x80DEC`), então o 512 do watchOS contra um lado
// encolhido de meio pixel fecha sozinho, sem um clamp inventado aqui.
icf::svg::Path chicletOutline(const ChicletGeometry& g);

// Quantos segmentos por cúbica esse raio pede. Sai daqui para que a cobertura e
// os realces subdividam igual.
int chicletSubdivisions(double radius);

// Coverage of the chiclet over the BUFFER of `g`, row major, one float per
// pixel in [0, 1]. Antialiased: a pixel the outline crosses gets the fraction
// of itself that is inside.
//
// O CONTORNO E ABSOLUTO -- ele e o retangulo continuo de `g.size` x `g.size`,
// com o raio de `g.size` -- e so a VARREDURA se restringe ao buffer, na
// coordenada absoluta `(x + g.originX) + 0.5` (spec 2026-09-16, "O invariante
// que governa o desenho"). Com `PixelGrid::full(size)` sai exatamente a
// cobertura de antes.
//
// `platform` escolhe a forma (ver `IconPlatform`); o default é `Main`, que é a
// pastilha de sempre, float a float.
std::vector<float> chicletCoverage(const PixelGrid& g, IconPlatform platform = IconPlatform::Main);

// A mesma cobertura sobre o canvas inteiro. Fica porque um chamador que so tem
// a resolucao nao precisa montar uma grade para dizer "tudo".
std::vector<float> chicletCoverage(std::uint32_t size,
                                   IconPlatform platform = IconPlatform::Main);

// Multiplies a PREMULTIPLIED RGBA accumulator by that coverage, in place.
//
// Premultiplied is why all four channels are scaled and not just alpha: in that
// form the colour carries its own alpha and a partial pixel has to dim in step.
void clipToChiclet(std::vector<float>& acc, const PixelGrid& g,
                   IconPlatform platform = IconPlatform::Main);
void clipToChiclet(std::vector<float>& acc, std::uint32_t size,
                   IconPlatform platform = IconPlatform::Main);

}  // namespace rb
