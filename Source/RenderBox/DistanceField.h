#pragma once
// TWO DIFFERENT THINGS LIVE HERE, AND THE LINE BETWEEN THEM IS THE POINT.
//
// PART ONE -- `distanceGradient_v1`, a TRANSCRIPTION.
//   `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod95.ll`, the
//   stitchable function `distanceGradient_v1`. Every statement about it below
//   was read from that IR, line by line, and the line numbers of the values it
//   came from are quoted where the arithmetic is not obvious. It is Apple's
//   code, so it is sealed.
//
// PART TWO -- the field GENERATOR, which is THIS PROJECT'S OWN ALGORITHM.
//   Apple's producer builds its field from CASDF elements (spec §7.3); we have
//   an SVG path that has been flattened into contours. Nothing about how we
//   turn one into the other was read from any binary, and none of it carries a
//   seal. It is a design decision, stated below with what it costs and what it
//   gets wrong.
//
// A `[BIN]` seal on part two would be a lie about provenance, which is the one
// failure this repository cannot absorb: every other reader downstream trusts
// the seal to mean "somebody read this out of a binary".
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rb {

// ===========================================================================
// PART ONE -- `distanceGradient_v1`, transcribed from mod95
// ===========================================================================

// `[BIN]` The five `float2` of an `RB::Layer`, in the order the IR's own loop
// stores them (`%11`, five iterations of a two-float copy out of `params + 2`).
// Spec §5.1 names the same five: an affine 2x3 UV transform and a clamp rect.
struct FieldLayer {
    float m0[2]{1, 0};   // the column p.x multiplies
    float m1[2]{0, 1};   // the column p.y multiplies
    float m2[2]{0, 0};   // the translation
    float m3[2]{0, 0};   // clamp low
    float m4[2]{1, 1};   // clamp high
};

// `[BIN]` mod95's 48-byte `params` block, whole. Twelve floats: `params[0]` is
// broadcast as the gradient's SCALE (`%118`), `params[1]` is broadcast as the
// BIAS added to it (`%120`) and is ALSO what the zero-distance branch writes
// into `.yz` (`%52`), and `params[2..11]` are the layer above.
//
// That one float serves as both bias and fallback is a reading, not a guess:
// `%31` is loaded once and feeds `%52` in the early-out block and `%120` in the
// normal one.
struct DistanceGradientParams {
    float scale = 1.0f;
    float fallback = 0.0f;
    FieldLayer layer{};
};

// The half narrowing the target does implicitly, because its texture is
// `half4` and its return type is `half4`. It matters: the central difference
// `%102`/`%105` is an `fsub half`, so the subtraction ROUNDS TO HALF BEFORE it
// is widened to float and normalized. A transcription that differenced in
// float would drift from the target on exactly the small differences a
// distance field is made of. `packHalf2x16` performs the same narrowing on the
// GLSL side, which is what makes the differential meaningful.
float fieldNarrowToHalf(float v);

// The texture mod95 samples at `t0`, and the ONE thing about the sample this
// transcription does not claim to know.
//
// `[OBS]` The sampler is `@__air_sampler_state = i64 -9188470239253755319`,
// i.e. `0x807BFF0000080A49`. That i64 is a packed Metal sampler descriptor and
// its bit layout is not published; the filter and address modes were NOT
// decoded, so the FILTER IS UNKNOWN. Rather than invent a bilinear and gate
// against our own invention, both sides of the differential fetch the NEAREST
// texel with clamp-to-edge and narrow it to half. That keeps the differential
// pointed at what WAS read -- the tap positions, the half-precision central
// difference, the guarded normalize, the zero-distance branch -- instead of at
// a filter nobody measured.
struct FieldTexture {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;   // four floats per texel, row major

    // Nearest with clamp-to-edge, narrowed to half. `field_probe.comp` defines
    // `rbFieldSample` by the same rule and must stay in step with it.
    float sampleX(const float uv[2]) const;
};

// `[BIN]` `uv(p) = clamp(fma(p.xx, m0, fma(p.yy, m1, m2)), m3, m4)`.
//
// The nesting is the IR's (`%44` is the inner fma over `p.yy`, `%45` the outer
// over `p.xx`), and it is `air.fma`, the TRUE fused multiply-add, not the
// contractible `llvm.fmuladd` that appears later at `%121`. Reassociating it
// would change the last bit, and the differential is bit-for-bit here.
void fieldUv(const FieldLayer& layer, float px, float py, float out[2]);

// `[BIN]` The whole of `distanceGradient_v1`, in the order the IR runs it.
//
// `dpdx` and `dpdy` are the target's `dfdx(p.x)` and `dfdy(p.y)`. They are
// PASSED IN rather than computed, for a reason that is not a shortcut: a
// derivative exists only in a fragment shader's quad, and both the CPU oracle
// and the compute probe that gates it live outside one. The transcription
// still owns the `abs` around them (`air.fast_fabs`, `%60`/`%63`), because
// dropping that sign would move two of the four taps to the wrong side.
//
// Returns `half4`: `.x` the centre tap's distance passed straight through,
// `.yz` the scaled gradient, `.w` coverage -- 1 normally, 0 on the
// zero-distance branch. That is what `glassBackground_v1` reads back (§5.2).
void distanceGradient(float px, float py, float dpdx, float dpdy,
                      const DistanceGradientParams& params, const FieldTexture& texture,
                      float out[4]);

// ===========================================================================
// PART TWO -- the field generator. OUR ALGORITHM, NOT A TRANSCRIPTION.
// ===========================================================================
//
// THIS IS NOT READ FROM ANYTHING. It carries no `[BIN]`, no `[ART]`, and it is
// not an inference from a binary either. It is a method this project chose,
// and the choice is argued rather than asserted.
//
// WHAT IS BORROWED, AND FROM WHERE
// --------------------------------
// Only the OUTPUT CONVENTION. `[INF]` from AquaKit's
// `Source/QuartzCore/shaders/Field.frag`, which transcribed
// `QuartzCore.CASDFGenerator`: the field is RGBA16F holding
// `(d, gx, gy, coverage)` with **d NEGATIVE INSIDE**. The sign is not a
// stylistic choice on their side -- `CASDFOutputEffect.minimum = -10000` is a
// floor on a quantity that goes negative, and a field positive inside would
// never approach it. `glassBackground_v1` agrees with that reading from the
// other end: it computes coverage as `saturate(-d/max(fwidth(d),1e-3) + 0.5)`
// (§5.3 block 1), and the minus sign there only makes coverage rise inside the
// shape if `d` is negative inside.
//
// THE METHOD, AND WHAT IT COSTS
// -----------------------------
// BRUTE FORCE: for every sample point, the exact Euclidean distance to every
// segment of every contour, minimised, then signed by a winding test.
//
//   cost:    O(W * H * E). A 512x512 field over a path flattened to 2000
//            segments is 5.2e8 point-segment tests. That is seconds, not
//            milliseconds, and it is the whole of the argument against it.
//   error:   ZERO against the geometry it is handed. The only error is the
//            caller's flattening of curves into segments, which the caller
//            controls and which the gate MEASURES (see `test_rb_field.cpp`:
//            the rectangle, whose contour is exact, comes back exact to float
//            rounding; the circle's error is the polygon's sagitta and nothing
//            more).
//
// WHY NOT SOMETHING FASTER. Jump flooding and 8SSEDT are both far cheaper and
// both approximate: they propagate a nearest-SEED over a grid, so their answer
// is quantised to the grid and JFA additionally admits propagation errors that
// are hard to bound analytically. THIS IS AN ORACLE. A field that is wrong by
// a third of a pixel poisons every glass test that consumes it, and the tests
// downstream would then be measuring our approximation instead of Apple's
// shader. Correctness first; if the cost ever bites, the fast method can be
// added BESIDE this one and gated AGAINST it, which is only possible because
// this one is exact.
//
// WHAT IT STILL GETS WRONG, named rather than hidden:
//   - The gradient is discontinuous on the shape's MEDIAL AXIS. That is a
//     property of the true distance field, not a defect of this code, and it
//     is asserted rather than smoothed over.
//   - Self-intersecting and overlapping contours are resolved by the FILL RULE
//     for the sign, but the DISTANCE is to the nearest edge whether or not
//     that edge is interior to the union. For a shape whose contours cross,
//     the interior edge shows up as a crease in the field. Apple's smooth
//     union (AquaKit's `SmoothUnion`) does not have that crease. We do not
//     model a union at all, because an SVG path is one shape and not a set.
//   - Coverage is analytic, not sampled: `clamp(0.5 - d, 0, 1)` over a field
//     whose slope is 1 per pixel. That is `Field.frag`'s
//     `clamp(-d/max(fwidth(d),1e-4) + 0.5, 0, 1)` with `fwidth(d)` at its true
//     value of 1, so the two agree wherever the field is smooth and differ
//     only across the medial axis, where `fwidth` spikes and theirs narrows the
//     band. Ours does not.

// A closed contour, as x,y pairs in the FIELD'S OWN PIXEL SPACE -- the same
// space `CoveragePass` rasterises into, so a caller that already flattened a
// path for the coverage pass hands the same points here. The closing segment
// from the last point back to the first is implied and must not be repeated.
struct FieldContour {
    std::vector<float> xy;
};

// The two rules `CoreSVG` parses and `PathResolve` implements. The generator
// needs one because the SIGN of the distance is an inside/outside question and
// nothing else about it is.
enum class FieldRule : std::uint32_t { NonZero = 0, EvenOdd = 1 };

struct FieldOptions {
    FieldRule rule = FieldRule::NonZero;
    // The width, in pixels, of the coverage ramp across the boundary. 1 is the
    // one that matches `Field.frag` on a pixel grid; it is a parameter only so
    // a test can prove the band is where it says it is.
    float aaWidth = 1.0f;

    // A origem da grade amostrada, em pixels (antes do supersample). A
    // amostragem e ABSOLUTA -- o centro da linha `y` e `y + originY + 0.5` --
    // e so o indice no resultado e deslocado (spec 2026-09-16, "O invariante
    // que governa o desenho"). Zero deixa a aritmetica de antes intocada.
    std::int32_t originX = 0;
    std::int32_t originY = 0;

    // `[INF]` THE SUB-TEXEL SEED. OURS. THE TARGET WAS NOT READ HERE.
    //
    // Only the grid-borne generator reads this -- `generateFieldFromAlpha`.
    // `generateField`'s brute force already has the true contour and ignores
    // it, and so, since 29/09/2026, does `generateFieldFromContours`, which
    // gives the same exact distance by a faster route.
    //
    // WHAT IT CHANGES. Without it the two Euclidean transforms are seeded from a
    // BINARY mask and the zero of the field is pinned to a texel centre, so the
    // finest thing the field can say is "half a texel". A specular band one
    // pixel wide read off such a field switches on and off a whole texel at a
    // time, and it does that at EVERY resolution, because at 2048 the band is
    // one texel of THAT grid -- the defect does not shrink with more pixels
    // because it is not a sampling defect.
    //
    // With it on, a texel that the surface actually crosses seeds BOTH
    // transforms carrying the signed offset `0.5 - coverage`, and the distance
    // is corrected by the offset of the seed the transform picked. For a
    // STRAIGHT edge at an arbitrary sub-texel offset the result is EXACT, which
    // is the property `test_rb_field.cpp` pins; a curved edge keeps the
    // quantisation of the nearest-centre choice, bounded by half a texel and in
    // practice far under it.
    //
    // `[BIN]` SUPERADO EM 16/09/2026 -- o parágrafo abaixo é de 15/09 e é
    // guardado porque as duas afirmações dele caíram por leitura, não por
    // opinião (`Docs/Laudos/2026-09-16-coreui-gerador-de-sdf.md`). O gerador
    // do alvo NÃO está no CoreUI: `sdfTextureWithBufferAllocator:` (CoreUI
    // 1010, `0x18B4B3044`) só BUSCA uma textura já gravada no catálogo. Quem a
    // faz é o IconRendering, `0x1CB90`, pelo `RB::Filter::Distance` do
    // RenderBox 8.0.84 -- semente edtaa (Sobel + tabela `edgedf`, zero no
    // `alpha = 0.5`), jump flooding e refinamento sub-texel. E os botões de
    // `SDFGeneration` NÃO estão mortos: `0x1D1F8`-`0x1D204` lê os quatro
    // (`box+0x1D8` = `params+0x1C8`). A semente que este código usa é,
    // portanto, a mesma CLASSE de coisa que o alvo faz; o método (EDT exata em
    // vez de JFA) segue sendo nosso.
    //
    // WHY THIS IS `[INF]` AND NOT `[BIN]`. Nobody read the target doing this.
    // The target's own generator is `-[CUINamedLayerImage
    // sdfTextureWithBufferAllocator:]` (`0x000867F8`), which lives in CoreUI --
    // in neither slice this project has -- and the ONE argument that crosses
    // that call (x2, built at `0x000867D8`-`0x000867E8`) is the buffer
    // allocator. The three `ICRRenderingParameters.SDFGeneration` knobs that
    // would have settled it (`clampThreshold`, `precisePixelFormatThreshold`,
    // `maxRelativeSmoothing`, `0xA46E0`) were CHASED and are DEAD in this
    // binary: their names appear only in `__swift5_reflstr`, in the `__cstring`
    // CodingKey literals at `0xA6A00`/`0xA6A20`/`0xA6A40`, and in `__LINKEDIT`.
    // The only two `__text` sites that touch those literals are `0x00061B10`
    // (`CodingKeys.stringValue`) and `0x000744BC`
    // (`CodingKeys.init?(stringValue:)`) -- Codable plumbing and nothing else.
    // `RenderBox.arm64` has no symbol matching `sdf` at all, in 11 996.
    //
    // So this is OUR approximation, declared as one. The convention is stated,
    // not measured, and no reader downstream should take it for the target's.
    bool subpixelSeed = true;
};

// What the field holds at one point.
struct FieldSample {
    float distance = 0.0f;   // NEGATIVE INSIDE
    float gx = 0.0f;         // unit, pointing the way `distance` INCREASES,
    float gy = 0.0f;         // i.e. away from the shape
    float coverage = 0.0f;
};

// The shape with its segments already gathered, so a sweep over a million
// pixels does not rebuild the list a million times.
class FieldShape {
public:
    explicit FieldShape(const std::vector<FieldContour>& contours);

    // Signed distance alone. Negative inside.
    float distanceAt(float x, float y, FieldRule rule) const;

    // Distance, gradient and coverage together.
    //
    // The gradient is EXACT rather than differenced: brute force already knows
    // the closest point on the boundary, and `grad = sign * unit(p - closest)`
    // is the analytic derivative of the distance. It is a unit vector
    // everywhere the field is differentiable, which a central difference is not
    // -- a difference straddling the medial axis returns a vector shorter than
    // 1, and near a corner it rounds the corner off. The one place the exact
    // form has nothing to say is a point EXACTLY on the boundary, where
    // `p - closest` is the zero vector; there, and only there, this falls back
    // to a central difference with e = 0.5, which is the same e `Field.frag`
    // uses.
    FieldSample sampleAt(float x, float y, const FieldOptions& options) const;

    std::size_t segmentCount() const { return segments_.size() / 4; }

private:
    std::vector<double> segments_;   // x0, y0, x1, y1 per segment
};

// The field as a picture: four floats per pixel in AquaKit's `(d, gx, gy,
// coverage)` layout, sampled at PIXEL CENTRES (x + 0.5, y + 0.5), row major.
//
// Pixel centres and not corners, because that is where `gl_FragCoord.xy` lands
// in `Field.frag` and where `CoveragePass` measures coverage. Sampling at
// corners would put the shape half a pixel off from everything else this tower
// draws.
struct FieldImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    // Onde o canto do campo cai na grade de destino: o eco da origem que
    // `FieldOptions` pediu, para que um consumidor saiba em que coordenada o
    // indice `(x, y)` esta.
    std::int32_t originX = 0;
    std::int32_t originY = 0;
    std::vector<float> rgba;

    const float* at(std::uint32_t x, std::uint32_t y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

FieldImage generateField(const FieldShape& shape, std::uint32_t width, std::uint32_t height,
                         FieldOptions options = FieldOptions{});

FieldImage generateField(const std::vector<FieldContour>& contours, std::uint32_t width,
                         std::uint32_t height, FieldOptions options = FieldOptions{});

// ===========================================================================
// PART THREE -- the field from a RASTERISED ALPHA
// ===========================================================================
//
// WHY THIS EXISTS, AND WHY IT IS NOT A SECOND GENERATOR.
//
// `[BIN]` The target does not build its field from geometry. The field is an
// `IconRendering.SDF` (`{texture, maxDistance}`, reflection at `0xA2FF4`), and
// the one call that makes one is `sdfTextureWithBufferAllocator:` at
// `0x000867F8`, inside `0x0008658C`. Its RECEIVER is the `x20` loaded at
// `0x00029334` from the frame of `0x0002881C`, and that frame slot is written
// at `0x00028AF4` with the result of a conditional cast to the class ref at
// `0x000CC928` -- which the chained-fixup import table binds to
// `_OBJC_CLASS_$_CUINamedLayerImage`. Two instructions later the code asks that
// object for `image` (`0x00028AE0`) and BAILS OUT if it is nil
// (`0x00028AEC cbz x0, #0x291BC`).
//
// A `CUINamedLayerImage` carries a bitmap and no path, and the generator
// refuses to run without one. The target's distance field is therefore
// computed from the RASTERISED ALPHA of the layer, after the layer has been
// drawn -- not from its contour. `IconRendering.SDF.SourceLayer` says the same
// from the reflection side: its two fields are `displayList` and `isOpaque`
// (`0xA3104`), a DRAWING and not a shape.
//
// So vector art and raster art are not two cases on the target. They are one,
// and the distinction this renderer used to draw -- "a raster has no contour to
// flatten, so the glass cannot run" -- was an artefact of OUR generator, not of
// the format.
//
// `[BIN]` ATUALIZAÇÃO DE 16/09/2026, e ela derruba o `[OBS]` do parágrafo
// seguinte: a grade e a transformada FORAM lidas. A grade é o `bakedSize` do
// ícone mais uma moldura de um texel (`setSize:(W+2, H+2)` em `0x1D354`,
// `translateByX:1 Y:1` em `0x1D7F4`), e `bakedSize` acompanha o tamanho
// pedido (`0x159C0`). A transformada é `RB::Filter::Distance`; os quatro botões
// valem `clampThreshold = 0.002`, `useAdvancedStacking = true`,
// `precisePixelFormatThreshold = 256` e `maxRelativeSmoothing = 0.005`
// (`0x5EAAC`-`0x5EAC8`). Laudo: `2026-09-16-coreui-gerador-de-sdf.md`.
//
// WHAT IS SEALED AND WHAT IS NOT. That the input is the rasterised alpha is
// `[BIN]`, with the addresses above. The GRID the target rasterises onto, and
// the exact transform it runs over that grid, are NOT read -- they live in
// `TXRTexture`, which is in neither `IconRendering.arm64` nor
// `RenderBox.arm64`. `[OBS]` The three `ICRRenderingParameters.SDFGeneration`
// knobs that would pin them down (`clampThreshold`, `precisePixelFormatThreshold`,
// `maxRelativeSmoothing`, reflection at `0xA46E0`) are named and unread. What
// runs below is this project's transform on the target's input: an EXACT
// Euclidean distance transform over the `alpha >= 0.5` contour, at the target's
// own resolution, which is the same rule and the same primitive
// `shadowRingMask` already uses -- and the shadow front already proved a raster
// needs no contour.

// The 1D squared Euclidean distance transform of Felzenszwalb and
// Huttenlocher: the lower envelope of the parabolas `(q - i)^2 + f[i]`, one
// pass forward and one back. O(n), and EXACT -- no chamfer weights, nothing
// that would show up as a faceted ring on a circle.
//
// `f` is squared distance in; `d` is squared distance out; `arg`, when it is
// not null, receives the index of the parabola that won at each `q`, which is
// what lets the two-dimensional caller recover the NEAREST SEED and from it an
// exact gradient. `v` and `z` are the envelope's vertices and breakpoints,
// passed in so a caller sweeping a million pixels allocates once. `v` must hold
// `n + 1` and `z` must hold `n + 2`.
//
// This is the single copy in the tower: `shadowRingMask` calls it too.
void edtSquared1d(std::vector<double>& f, std::vector<double>& d, std::vector<int>& v,
                  std::vector<double>& z, int n, std::vector<int>* arg = nullptr);

// The COLUMN half of the 2D transform, in place over a row-major `w x h` grid
// of squared distances (`sq` holds `w * h`, and so does `siteY` when given): every column of `sq` goes through `edtSquared1d` and
// comes back where it was. When `siteY` is not null it receives, per texel,
// the row of the parabola that won (`-1` where the column has no seed).
//
// It exists so both callers (`edt2d` and `shadowRingMask`) read a column the
// cheap way. A column of a 1024-wide grid of doubles is one element every
// 8 KB, which is the worst stride a set-associative cache can be handed; this
// gathers a TILE of neighbouring columns with row-order reads, runs the 1D
// transform on each, and scatters them back the same way. Each column is the
// same call over the same inputs as before, so no number can move.
void edtSquaredColumns(double* sq, int w, int h, int* siteY);

// The field of a BITMAP's alpha, in the same `(d, gx, gy, coverage)` layout and
// the same sign convention as `generateField` -- negative inside, unit
// gradient pointing the way `d` increases, coverage from the same
// `-d/aaWidth + 0.5`. A caller can hand the result to `glassDisplacementMap`,
// `glassOpacityMask` or `drawSpecular` without knowing which generator made it.
//
// `rgba` is four floats per texel, row major, ALPHA IN `[3]`, straight
// premultiplied or not -- only the alpha is read. It must hold `width * height`
// texels or the result is empty.
//
// THE CONTOUR IS `alpha >= 0.5`, and the distance is measured from pixel
// CENTRES to that contour: `d = +-(nearestOppositeCentre - 0.5)`. The half
// pixel is what puts the boundary BETWEEN two centres of opposite class rather
// than ON one of them, which is where a coverage of exactly 0.5 belongs and
// what keeps this field in step with `generateField`'s, which samples the true
// contour at centres.
//
// FOUR VIRTUAL OUTSIDE ROWS one step beyond each edge, so art that runs off the
// canvas has a finite depth instead of an infinite one. `[BIN]` That is not a
// default: it is the empty one-texel border of the target's own SDF, the same
// border `0x00011CF8` subtracts (`sdfTexelsW - 2`) and `0x00010F48` puts back.
// `shadowRingMask` already clamps to it; this does too, and agrees with it
// pixel for pixel.
//
// An input whose alpha never reaches 0.5 has no inside, and therefore no
// contour to sign. The result is EMPTY (`width == 0`), so the caller reports a
// gap instead of being handed a field that is everywhere-outside and silently
// draws nothing.
//
// E NAO, ESTA NAO LEVA `superSample`, E A ASSIMETRIA E A GEOMETRIA FALANDO.
//
// `generateFieldFromContours` supersampleia porque tem de onde: o contorno e
// analitico e rasteriza-lo numa grade `ss` vezes mais fina produz informacao que
// nao existia na grade grossa. Um BITMAP nao tem esse de onde. Subir `ss` aqui
// so poderia REPLICAR cada texel em `ss x ss` copias -- a mesma silhueta em
// degrau, agora em degraus menores e em `ss^2` vezes o custo -- ou reamostrar o
// alfa, que inventa uma borda que o arquivo nao contem.
//
// A informacao sub-texel que o raster REALMENTE tem ja esta sendo usada, e por
// outra porta: `coverage[t] = alpha[t]`, a identidade que `CoveragePass` escreve,
// e `options.subpixelSeed` faz as duas transformadas nascerem com o deslocamento
// assinado `0.5 - coverage`. Para uma aresta RETA num deslocamento sub-texel
// qualquer isso e EXATO -- exato de um jeito que nenhum `ss` finito alcanca.
// Somar `superSample` aqui seria simetria de assinatura, nao ganho: veja o
// laudo `2026-09-15-supersample-campo.md`, secao 5.
FieldImage generateFieldFromAlpha(const std::vector<float>& rgba, std::uint32_t width,
                                  std::uint32_t height, FieldOptions options = FieldOptions{});

// The SAME field, for art that arrives as a CONTOUR instead of as a bitmap.
//
// WHY A VECTOR NOW TAKES THE RASTER'S DOOR, AND WHAT IT COST TO SAY SO.
// `generateField` above answers the question exactly: for every pixel it walks
// every segment and keeps the true `argmin`. That is O(pixels x segments), and
// on the art this renderer is actually handed it is the whole render -- `[ART]`
// 10 455 ms of a 1024 px render of `Apollo-Reborn__Apollo-Reborn__AppIcon`,
// 73 % of it, over ten layers; and on the user's own grunge icon it is 56.7 s
// of a 57.2 s render. A million pixels times tens of thousands of segments does
// not have a constant that saves it.
//
// THOSE TWO NUMBERS ARE DATED 15/09 AND DESCRIBE THE TREE THAT STILL WALKED
// EVERY SEGMENT. They are the REASON this door exists, so they stay -- but
// read as "what the exact path cost", not as what a render costs now. Since
// the contours started arriving here, the same document at 1024 px measured
// 2,42 s (19/09, Release, min of three), of which the field was 0,95 s, and
// the CPU sweeps below then went multi-threaded and took it to 1,68 s.
// Anyone reaching for these figures to justify an optimisation should measure
// first: a task was dispatched on 19/09 against the 73 %, and found 39 %.
//
// `[BIN]` And the exactness it buys is not what the target does. The target's
// field comes from `sdfTextureWithBufferAllocator:` (`0x000867F8`) sent to a
// `CUINamedLayerImage` (`0x000CC928`) that BAILS if its `image` is nil
// (`0x00028AE0`, `0x00028AEC cbz x0`); `IconRendering.SDF.SourceLayer`
// (`0xA3104`) is `{displayList, isOpaque}`. The target rasterises and then
// transforms. A field taken off a grid is not a worse answer than the exact
// one -- it is the answer the target gives.
//
// UNTIL 29/09/2026 this rasterised the contours onto the field's own grid and
// ran the two exact Euclidean transforms `generateFieldFromAlpha` runs. That is
// O(pixels + segments), and it was WRONG IN A WAY THAT SHOWED: along a curve the
// distance stepped in texel increments, and the nearest-texel sampling plus the
// four-tap gradient of `distanceGradient_v1` (part one, sealed) turned each step
// into a step of the NORMAL -- hard radial streaks along every curved rim the
// highlights lit. Swapping in `generateField` made them vanish on the Kenzu
// icon, at ten times the cost.
//
// NOW IT RETURNS `generateField`'s NUMBERS -- the same `closestOnSegment`, in
// double, over the same segments, so the distance is bit-identical (`[ART]`
// max |diff| 0 on the chiclet and on every vector-glass layer of
// `Apollo-Reborn__Apollo-Reborn__AppIcon`, `Jellify-Music__App__teal-icon-composer`
// and `DimensionDev__Flare__AppIcon`, at 512 and 1024) -- through a uniform
// grid of segment buckets searched per 8x8 block: an annulus bounded by the
// neighbouring block's distance (1-Lipschitz), then the segments within
// `dP + 2h` of the block centre as each pixel's candidates. Exact EVERYWHERE,
// not in a band, because the translucency's `borderWidth` and the refraction's
// height are document values with no ceiling.
//
// The SIGN still comes from the scanline rasterisation, with the SAME half-open
// crossing rule and fill rule as `FieldShape::distanceAt`, read at the sub-texel
// the pixel centre lands on. The GRADIENT is `generateField`'s exact one,
// `sign * unit(p - foot)`, not the Sobel `fieldFromInsideMask` takes off a
// quantised field. The four virtual outside rows beyond the buffer edge still
// clamp an inside distance, as they do in the alpha path.
//
// WHAT CAME BACK WITH EXACTNESS: the crease. The distance is again to the
// nearest SEGMENT whether or not it is buried inside the union (the caveat
// above) -- on `Apollo`'s `stem.svg` the grid field used to differ from this by
// up to 3.22 px, every one of them under the antenna ellipse, and now it does
// not differ at all.
//
// `superSample` no longer changes the distance -- an exact answer has nothing
// to gain from a finer grid. It still picks the grid the SIGN mask is
// rasterised on (read at the sub-texel whose centre IS the pixel centre, which
// is why it must be ODD; even values are rounded down), so a shape thinner
// than a pixel that only a finer mask catches still counts as present.
FieldImage generateFieldFromContours(const std::vector<FieldContour>& contours,
                                     std::uint32_t width, std::uint32_t height,
                                     FieldOptions options = FieldOptions{},
                                     std::uint32_t superSample = 1);

// O SINAL de `generateFieldFromContours`, sozinho: a mascara dentro/fora que ele
// le (a mesma `rasteriseContours`, a mesma subamostragem), uma entrada por pixel
// do buffer. Devolve a contagem que ele testa -- zero quer dizer "campo vazio"
// -- e deixa `inside` vazio nesse caso. Existe para o caminho residente
// (`GpuGlass.cpp`): a distancia exata vai para a GPU, o sinal fica aqui.
std::size_t fieldInsideMask(const std::vector<FieldContour>& contours, std::uint32_t width,
                            std::uint32_t height, const FieldOptions& options,
                            std::uint32_t superSample, std::vector<char>& inside);

// The COVERAGE of a contour set, one float per pixel in `[0, 1]`: the same
// inside/outside test as `fieldInsideMask` (`rasteriseContours`, the same
// half-open crossing rule and fill rule), run on a grid `samples` times finer on
// each axis and box-averaged back. `samples * samples` sample points per pixel,
// evenly spaced; `samples == 1` is the mask itself as 0 and 1.
//
// OURS, like everything in part two. It exists for the one caller that needs a
// SILHOUETTE rather than a field: a group lit as ONE shape (part four) draws
// its glass elements' silhouettes into one image and takes the field of that
// image's alpha (`generateFieldFromAlpha`), and the alpha of a vector has to
// come from somewhere both render paths compute identically -- which the
// art's own antialiased alpha is not, between the CPU and the GPU. The
// sub-texel seed of `generateFieldFromAlpha` then has a fraction to work with
// along the contour instead of a hard step.
//
// Returns the number of sample points inside, and leaves `coverage` empty when
// that is zero -- a contour set that covers no sample point.
std::size_t fieldCoverageFromContours(const std::vector<FieldContour>& contours,
                                      std::uint32_t width, std::uint32_t height,
                                      const FieldOptions& options, std::uint32_t samples,
                                      std::vector<float>& coverage);

// ===========================================================================
// PART FOUR -- ONE field for a GROUP of glass elements
// ===========================================================================
//
// `[BIN]` The target keeps one SDF per GROUP (`FinalizedIcon.Layer.sdf`), not
// one per element, and builds it in `IconRendering` `0x1CB90`. When the group
// lights its elements one by one (`performsLightingByElement`, the branch at
// `0x1CDD8`) every glass element gets a source list and a `DistanceFilter` of
// its own (`0x1CE98`-`0x1D084`) and the results are STACKED by `0x11480`,
// back to front. Element 0 is drawn as it is (`0x11514`-`0x11528`). For each
// later one (the loop at `0x115F0`-`0x118CC`):
//
//   1. with `SDFGeneration.useAdvancedStacking` -- AND a `RBProjectVersion`
//      gate, `0x11B00`, whose answer was not read -- the element's field is
//      first drawn over everything with blend code `0x3F9` (`0x11664`-`0x11678`),
//      which is RenderBox's mode 51, `sdf_maximum`;
//   2. a clip layer is opened and the element's field is drawn into it through
//      `addStyle:1`, a GradientMap of two stops (`0x1168C`-`0x1182C`), and closed
//      with `clipLayerWithAlpha:1 mode:0` (`0x11874`);
//   3. the element's field is drawn inside that clip, blend 0 (`0x1189C`).
//
// `[BIN]` THE TWO STOPS ARE A STEP. The first sits at
// `0.5 - (maxDistance + 1) / (2 * range)` (`0x11580`-`0x11598`, `range` being
// the encoding range of the SDF texture, `0.2 * min(w, h)`) and the second at
// the NEXT REPRESENTABLE HALF after it (`0x1159C`-`0x115D0`); the colours are
// the two lazily-built globals at `0xCF4A8` and `0xCDE98`, the second of which
// is the opaque black `GlassShadow.h` already reads. The texture encodes
// `u = 0.5 - d / (2 * range)` (inside is above one half), so the step sits at
// `d = maxDistance + 1` and the clip is the element's footprint DILATED by
// `maxDistance + 1` pixels.
//
// `[BIN]` THE TWO FRAGMENTS ARE READ, out of the RenderBox metallib
// (`default_mod66.ll`):
//
//   * GradientMap is the branch of `alpha_effect_fragment_impl` taken when bits
//     9-11 of the state are 2 (`%60`-`%61`): one channel of the texel -- chosen
//     by bits 6-8, or its Rec.709 luma (`%49`-`%57`) -- goes through an affine
//     `t = v * scale + bias` (`%59`) and `t` indexes the gradient
//     (`Gradient::color`, `%96`). `0x117EC`-`0x1181C` hands it the pair
//     `(0.0f, 1.0f)` beside the stops.
//   * `sdf_maximum` is case 51 of `RB::Shader::blend` (`%582`-`%586`):
//     `out = src.r > dst.r ? src : dst`. The WHOLE texel of whichever side is
//     further inside, with no arithmetic -- a union that carries the normal of
//     the winner along with its distance.
//
// `[INF]` Three things join those readings to the function below, and none of
// them was read. That the pair `(0.0f, 1.0f)` leaves the channel as it is
// (which of the two is the scale was not followed into RenderBox's
// `render_(GradientMap...)`); that the first stop's colour is transparent (its
// initialiser, `0x3DB0C`, was not opened -- a step from black to black would
// clip nothing); and that an element's field, drawn with blend 0 at alpha 1,
// REPLACES what is under it, i.e. that the field's layer is opaque over its
// whole rect.
//
// `[OBS]` THE LAST ONE IS THE WEAKEST, and the filter's own fragment does not
// settle it. `filter_distance` (`default_mod74.ll`) resolves the distance as
// `u = saturate(d * scale + bias)` (%86-%95) and writes it in one of two
// shapes: mode 5 puts `u` in ALL FOUR channels, alpha included (%96-%99), and
// mode 6 writes `(u - bias, u, 0, 0)` (%100-%105). Under neither is a plain
// source-over a replacement. Which mode a source layer takes, and whether
// `SDF.SourceLayer.isOpaque` (`0xA3104`) is what makes its layer opaque, was not
// followed. Replacement is kept because it is the only reading under which the
// stack means anything: composited as premultiplied alpha, two encoded
// distances add up to a contour that belongs to neither element.
//
// WHAT THE REPLACEMENT COSTS, said because it shows: in a band `maxDistance + 1`
// wide around every upper element the group's field says "outside" even where
// a lower element paints, so there the translucency mask leaves the lower art
// opaque and the lower element's highlight is cut. `kFieldStackNote`
// (`IconRenderer.h`) says so whenever a group stacks.
//
// SO, PER TEXEL, with `d` this tower's signed distance (negative inside):
//
//     upper.d < maxDistance + 1   ->  the upper element's texel
//     otherwise, step 1 on        ->  whichever of the two has the smaller d
//     otherwise                   ->  the lower texel
//
// Strictly less: at `u` equal to the first stop the gradient still answers
// with the first stop's colour. `[OBS]` Our `d` is a float and the target's `u`
// is a half or an 8-bit texel, so which side of the step a texel within one
// encoding step of it falls on is not reproduced.
//
// `maxDistance` (`0x1C6B8`) is the reach of the widest enabled highlight, in
// pixels, and zero for a group with no specular; the caller resolves it.
//
// STEP 1 CHANGES NO PIXEL OF THIS RENDERER, and that is arithmetic and not a
// measurement: it only speaks where `upper.d >= maxDistance + 1`, and there it
// hands back a texel whose `d` is at least that -- further out than any
// highlight reaches, and more than a pixel outside for the translucency's
// `saturate(sd + 1)`. It is transcribed because it is read, and so that a
// consumer which looks further out (the generation-26 glow) finds it there.
inline bool fieldStackTakesUpper(float upperDistance, float lowerDistance, float edge,
                                 bool advanced) {
    if (upperDistance < edge) return true;
    return advanced && upperDistance < lowerDistance;
}

// The stack of two fields on the same grid. `reach` is `maxDistance`; the edge
// it is compared against is `reach + 1`, computed once, in float, so that the
// resident path (`icon_field_stack.comp`) compares against the same number.
// An EMPTY `lower` answers `upper` and the other way round; two fields on
// different grids answer the empty field, which is a caller's mistake.
FieldImage stackFields(const FieldImage& lower, const FieldImage& upper, float reach,
                       bool advanced);

}  // namespace rb
