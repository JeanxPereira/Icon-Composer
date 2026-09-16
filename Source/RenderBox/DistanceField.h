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

    // `[INF]` THE SUB-TEXEL SEED. OURS. THE TARGET WAS NOT READ HERE.
    //
    // Only the grid-borne generators read this -- `generateFieldFromAlpha` and
    // `generateFieldFromContours`. `generateField`'s brute force already has the
    // true contour and ignores it.
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
// `[BIN]` And the exactness it buys is not what the target does. The target's
// field comes from `sdfTextureWithBufferAllocator:` (`0x000867F8`) sent to a
// `CUINamedLayerImage` (`0x000CC928`) that BAILS if its `image` is nil
// (`0x00028AE0`, `0x00028AEC cbz x0`); `IconRendering.SDF.SourceLayer`
// (`0xA3104`) is `{displayList, isOpaque}`. The target rasterises and then
// transforms. A field taken off a grid is not a worse answer than the exact
// one -- it is the answer the target gives.
//
// So this rasterises the contours onto the field's own grid -- one scanline
// sweep, the SAME half-open crossing rule and the SAME fill rule as
// `FieldShape::distanceAt`'s inside test, so a pixel is inside here exactly
// when it was inside there -- and hands the mask to the same two exact
// Euclidean transforms `generateFieldFromAlpha` runs. O(pixels + segments).
//
// AND IT IS NOT ONLY CHEAPER. The `argmin` measured to the nearest SEGMENT
// whether or not that segment is buried inside the union -- the crease the
// method's own caveat above admits to, and the one thing about the old field
// that Apple's smooth union provably does not have. A rasterised shape has no
// buried edges to measure to, so the crease is gone. `[ART]` On
// `Apollo-Reborn__Apollo-Reborn__AppIcon`'s own art the two fields agree to a
// mean of 0.27-0.34 px and a worst of 0.78 px on SEVEN of eight layers; the
// eighth is `stem.svg`, where they differ by up to 3.22 px, and every one of
// those pixels is a crease under the antenna ellipse.
//
// `superSample` rasterises onto an N-times-finer grid and runs the transform
// there, which divides both the distance quantisation and the ANGULAR
// quantisation of the gradient by N; the field is then read at the sub-texel
// that the pixel centre lands on. It must be ODD, because only an odd factor
// has a sub-texel whose centre IS the pixel centre -- an even one would read
// the field half a sub-texel off and put the whole picture out of step with
// `CoveragePass`. Even values are rounded down to the odd below.
FieldImage generateFieldFromContours(const std::vector<FieldContour>& contours,
                                     std::uint32_t width, std::uint32_t height,
                                     FieldOptions options = FieldOptions{},
                                     std::uint32_t superSample = 1);

}  // namespace rb
