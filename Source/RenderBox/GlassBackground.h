#pragma once
// `glassBackground_v1`, block by block, on the CPU.
//
// PRIMARY SOURCE. `[BIN]` `References/2.0-125/out/metallib-renderbox/
// default_mod98.ll` -- 1715 lines, ~65 basic blocks, one `define`. Every
// function below names the IR value numbers it was read from. The block list in
// `Docs/Specs/2026-09-02-vidro.md` §5.3 was used as orientation and is corrected
// where the IR disagrees with it; each correction is named at its site.
//
// WHY THIS IS THIRTY-FOUR FUNCTIONS AND NOT ONE.
// The target is one function of ~1444 lines. Transcribed as one function it
// would be unreviewable and, worse, ungateable: a differential over the whole
// thing proves only that two large transcriptions agree, and says nothing about
// WHICH of sixty-five blocks is wrong when they do not. So the function is cut
// at the seams the IR itself has -- every basic-block group that produces one
// named quantity is one function here, taking that quantity's inputs
// explicitly. `GlassOracle` did the same for the primitives, and this builds on
// it rather than restating it.
//
// WHAT IS NOT HERE, AND WHY IT CANNOT BE.
// mod98 takes fifteen texture samples and one `fwidth`. A derivative and a
// sampler are exactly what an oracle cannot hold: reproducing them would mean
// reproducing the target's filtering, wrap mode and LOD unit, and the
// differential would then be measuring the sampler rather than the block. So
// every sampled value arrives as an ARGUMENT, and the LOD is not computed here
// at all -- `MipPyramid.h`'s `backdropLod` is the transcription of
// `log2/fmax/fsub/fmin`, it is already gated, and duplicating it would create a
// second copy to drift. What this file owns is the RADIUS each sample asks for
// (which is mod98's own arithmetic) and everything done to a sample after it
// lands.
//
// PRECISION. `[BIN]` mod98 does this arithmetic in HALF -- every literal is an
// `0xH....`. This is float32, for the reason `GlassOracle.h` sets out at length:
// that is what the RenderBox tower runs and what Vulkan gives without an
// extension, so bit-for-bit agreement WITH THE TARGET is not on the table. What
// is reproduced is the ARRANGEMENT, and bit-for-bit is demanded, and gated,
// between this file and `shaders/GlassBackground.glsl`.
//
// THREE TOOLCHAIN HAZARDS, ALREADY PAID FOR ELSEWHERE, RESPECTED HERE:
//   * every multiply-add is written `a * b + c` UNFUSED on both sides, because
//     glslc does not decorate `OpExtInst Fma` with `NoContraction` even under
//     `precise` (`GlassOracle.h` has the disassembly);
//   * no `packHalf2x16` anywhere -- this file never narrows to half;
//   * `std::fma` is not called, so its non-correctly-rounded implementation on
//     this toolchain cannot bite.
//
// THE ONE PLACE MOD98 DISAGREES WITH THE SPEC'S BLOCK LIST. Spec §5.3 puts the
// chromatic dispersion behind "`u@208 != 0`" and the shadow lobe behind nothing
// in particular. `[BIN]` The IR gates the dispersion on `%554` (byte 208) AND
// `shaderVariant & 4`, and gates the SHADOW LOBE's backdrop fetch on `%365`
// (byte **152**, `%364 = getelementptr float, %2, i64 38`) and the same variant
// bit. Two different uniforms, both under bit 2. The spec's "outer lobe" and
// "face lobe" are, in the target's own vocabulary, the SHADOW and the INNER
// REFRACTION.
#include <cstddef>
#include <cstdint>

namespace rb {
namespace glassbg {

// ------------------------------------------------------ the variant bits

// `[BIN]` mod98 `%127`..`%169`: the function constant `RB::shaderVariant`
// (`!26`, a `uint`) is loaded once and three bits are tested.
//
//   bit 0 (`%128`) gates the ring-shadow re-sample, the key-fill highlight, the
//                  ring shadow, the blur fill, and the whole ring epilogue;
//   bit 1 (`%166`) selects the INNER REFRACTION path -- when it is CLEAR the
//                  face is one flat backdrop fetch (`%839`) and the refraction,
//                  the dispersion and the outer refraction never run;
//   bit 2 (`%168`) gates the shadow lobe's backdrop fetch, the dispersion and
//                  the specular.
//
// `[BIN]` The polarity of bit 1 is worth stating because it reads backwards:
// `%167 = icmp eq i32 %166, 0` then `br i1 %167, label %839, label %476` -- the
// flat path is taken when the bit is ZERO.
inline constexpr std::uint32_t kVariantRing = 1u;
inline constexpr std::uint32_t kVariantInnerRefraction = 2u;
inline constexpr std::uint32_t kVariantBackdrop = 4u;

// --------------------------------------------------------- the uniform map

// `[BIN]` `RB::Shader::Glass::BackgroundUniforms` -- 256 bytes, 75 LLVM fields,
// confirmed member by member against the TBAA node `!40`, which lists every
// offset. Widened here to 106 floats because nine of the fields are `[4 x half]`
// and thirteen are `[2 x float]`/`[2 x half]`; the byte each float mirrors is in
// the comment and in `kUniformByte`.
//
// THE NAMES ARE `[INF]`, THE OFFSETS ARE `[BIN]`, AND THE DIFFERENCE MATTERS.
// Doc 03 §27.4 read 54 CIFilter-style keys out of `RenderBox.arm64`'s packer and
// bound each to a store offset. Laid against this struct those bindings are
// **shifted by exactly +16**: the doc's "byte" column is the `str s0, [sp, #n]`
// STACK offset, and the struct begins at `sp+16`. That is not a guess dressed as
// a reading -- ten independent structural coincidences fix it, and one of them
// alone would be enough:
//
//   * doc +48 is `inputShadowOffset`, an eight-byte `float2`. At +16 that is
//     struct @64, field #14, the ONLY `[2 x float]` in the struct outside the
//     two decode pairs -- and mod98 subtracts field #14 from `p` as an XY offset
//     (`%109`). At +0 it would straddle fields #10 and #11, which doc §27.3's
//     own positive control says never happens.
//   * doc +144…+150 are `inputBlurOpacity0..3`, four halves, and +152…+158 are
//     `inputBlurDistance0..3`, four more. At +16 those are fields #28..#31 and
//     #32..#35 -- eight consecutive halves, and mod98 reads them as exactly two
//     quadruples feeding one three-segment ramp (`%494`..`%541`). At +0 the
//     first quadruple lands inside `[4 x half] @144`, a colour-matrix row, and
//     the second inside two `float`s.
//   * doc §27.5 puts "the three colour matrices" at +64…+135 and +140…+143 --
//     76 bytes. At +16 that is @80…@151 plus @156…@159, and the three matrices
//     are fields #17..#25 at bytes 80…151. Exact.
//   * doc +188 is `inputFaceColorMatrixMaxLuma`. At +16 that is @204, which is
//     `%875`/`%1123` -- the `complement` argument of `compressMaxLuma`, twice.
//   * doc +166 is `inputFaceOpacity`. At +16 that is @182, which is `%870` --
//     the weight the face colour matrix is mixed back by.
//   * doc +192/+194/+196 are `inputAberrationAmount/Height/Offset`. At +16 they
//     are @208/@210/@212, which are exactly the `(amount, invHeight, offset)`
//     triple of the dispersion's band (`%554`/`%593`/`%590`).
//   * doc +0…+12 are `inputInnerRefractionAmount/Height` and
//     `inputOuterRefractionAmount/Height`. At +16 they are @16/@20/@24/@28, and
//     AquaKit -- which read QuartzCore, not RenderBox -- calls the two blocks
//     mod98 feeds from those pairs "the inner refraction" and "the outer
//     refraction". Two binaries and one name.
//
// `[BIN]` And the shift explains the one thing that looked like a contradiction:
// doc §27.5 lists "+240…+255" as unresolved, "probably written in a block". At
// +16 that is @256…@271 -- past the struct, in the `MultiLevelLayer` the spec
// puts at byte 256. A block write of a layer transform is exactly what it is.
//
// `[OBS]` Slots whose name is still a hole are marked below. They are the ones
// doc §27.5 could not resolve, and nothing in this file depends on their names,
// only on their offsets and their arithmetic.
//
// `[BIN]` AND ONE FIELD IS NEVER READ. The spec says "every byte from 0 to 255
// is read by the IR". Resolving every `getelementptr` in mod98 says 74 of the 75
// fields are read and field #48 (**byte 202**) is not. It is the only hole.
struct Uniforms {
    // fields #0/#1 -- the two decode pairs. `[OBS]` no CIFilter key binds these;
    // they sit BELOW the packer's first key (doc +0 -> struct @16), so the host
    // writes them, not the filter dictionary.
    float fieldScale = 1.0f;        // @0   `[BIN]` %82, d = fma(sdf.x, this, +4)
    float fieldBias = 0.0f;         // @4   `[BIN]` %84
    float gradientScale = 1.0f;     // @8   `[BIN]` %88, g = fma(sdf.yz, this, +12)
    float gradientBias = 0.0f;      // @12  `[BIN]` %90

    float innerRefractionAmount = 0.0f;     // @16  `[BIN]` %481
    float innerRefractionInvHeight = 0.0f;  // @20  `[BIN]` %478 (a RECIPROCAL)
    float outerRefractionAmount = 0.0f;     // @24  `[BIN]` %778
    float outerRefractionInvHeight = 0.0f;  // @28  `[BIN]` %775 (a RECIPROCAL)
    float refractionDistance0 = 0.0f;       // @32  `[BIN]` %824
    float refractionDistance1 = 1.0f;       // @36  `[BIN]` %826
    float blurRadius = 1.0f;                // @40  `[BIN]` %543 / %841
    float bleedBlurRadius = 0.0f;           // @44  `[BIN]` %977 (the specular's)
    float bleedAmount = 0.0f;               // @48  `[BIN]` %961
    float bleedInvHeight = 0.0f;            // @52  `[BIN]` %958 (a RECIPROCAL)
    float shadowAmount = 0.0f;              // @56  `[BIN]` %322
    float shadowInvHeight = 0.0f;           // @60  `[BIN]` %319 (a RECIPROCAL)
    float shadowOffsetX = 0.0f;             // @64  `[BIN]` %104
    float shadowOffsetY = 0.0f;             // @68  `[BIN]` %107
    float shadowBlurRadius = 0.0f;          // @72  `[BIN]` %370
    float shadowRadius = 0.0f;              // @76  `[BIN]` %339 (a RECIPROCAL)

    // fields #17..#25 -- the three 3x4 colour matrices, `.rgb` coefficients and
    // `.w` the bias. `[BIN]` face @80..@103 (%897..%941), specular @104..@127
    // (%1001..%1045), shadow @128..@151 (%394..%442).
    float faceMatrix[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};        // @80
    float specularMatrix[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};    // @104
    float shadowMatrix[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};      // @128

    float shadowVibrancy = 0.0f;      // @152 `[BIN]` %365 -- gate AND scale
    float shadowMatrixAlpha = 0.0f;   // @156 `[BIN]` %447/%462

    float blurOpacity0 = 0.0f;   // @160 `[BIN]` %535 / %845
    float blurOpacity1 = 0.0f;   // @162 `[BIN]` %523
    float blurOpacity2 = 0.0f;   // @164 `[BIN]` %527
    float blurOpacity3 = 0.0f;   // @166 `[BIN]` %530
    float blurDistance0 = 0.0f;  // @168 `[BIN]` %494
    float blurDistance1 = 1.0f;  // @170 `[BIN]` %498
    float blurDistance2 = 2.0f;  // @172 `[BIN]` %503
    float blurDistance3 = 3.0f;  // @174 `[BIN]` %509
    float bleedDistance0 = 0.0f; // @176 `[BIN]` %1050 (the specular ramp)
    float bleedDistance1 = 1.0f; // @178 `[BIN]` %1052
    float bleedOpacity = 0.0f;   // @180 `[BIN]` %954 -- gate AND scale
    float faceOpacity = 0.0f;    // @182 `[BIN]` %870 / %1118

    float bleedDarkenScale = 0.0f;  // @184 `[BIN]` %1066 -- the specular weight's
    float bleedDarkenBias = 0.0f;   // @186 `[BIN]` %1068     scale and bias

    float shadowDistanceOffset = 0.0f;  // @188 `[BIN]` %315
    float shadowOpacity = 0.0f;         // @190 `[BIN]` %352
    float refractionOpacity = 0.0f;     // @192 `[BIN]` %771 -- gate AND scale
    float maxHeadroom = 0.0f;           // @194 `[BIN]` %1245 -- gate AND scale
    float sdrGradientDistance0 = 0.0f;  // @196 `[BIN]` %1250
    float sdrGradientInvSpan = 0.0f;    // @198 `[BIN]` %1253
    float clampCeiling = 0.0f;          // @200 `[BIN]` %1282 -- gate AND ceiling
    float unreadAt202 = 0.0f;           // @202 `[BIN]` field #48: NEVER READ
    float faceMatrixMaxLuma = 0.0f;     // @204 `[BIN]` %875 / %1123
    float sdrHoldingToneWhite = 0.0f;   // @206 `[BIN]` %1258
    float aberrationAmount = 0.0f;      // @208 `[BIN]` %554 -- gate AND amount
    float aberrationInvHeight = 0.0f;   // @210 `[BIN]` %593 (a RECIPROCAL)
    float aberrationOffset = 0.0f;      // @212 `[BIN]` %590
    float aberrationCos = 1.0f;         // @214 `[OBS]` name; `[BIN]` %574
    float aberrationSin = 0.0f;         // @216 `[OBS]` name; `[BIN]` %579
    float ringShadowOffsetY = 0.0f;     // @218 `[BIN]` %137
    float ringShadowStrokeWidth = 0.0f; // @220 `[BIN]` %282
    float ringShadowBlurRadius = 0.0f;  // @222 `[BIN]` %276
    float ringShadowOpacity = 0.0f;     // @224 `[BIN]` %133 / %265 -- gate AND scale
    float ringShadowMask = 0.0f;        // @226 `[BIN]` %270
    float highlightHeight = 0.0f;       // @228 `[BIN]` %176 -- the gate
    float highlightProjX = 0.0f;        // @230 `[OBS]` name; `[BIN]` %228
    float highlightProjY = 0.0f;        // @232 `[OBS]` name; `[BIN]` %232
    float highlightEdge = 0.0f;         // @234 `[OBS]` name; `[BIN]` %223
    float highlightSpread = 0.0f;       // @236 `[BIN]` %251
    float highlightEffectOffset = 0.0f; // @238 `[BIN]` %180
    float highlightColorBias = 0.0f;    // @240 `[BIN]` %1198 / %1222
    float blurFillBlurRadius = 0.0f;    // @242 `[BIN]` %729
    float blurFillLighten = 0.0f;       // @244 `[BIN]` %716
    float blurFillDarken = 0.0f;        // @246 `[BIN]` %719
    float blurFillNormal = 0.0f;        // @248 `[BIN]` %724
    float highlightPlacement = 0.0f;    // @250 `[OBS]` name; `[BIN]` %185
    float highlightAlphaGain = 0.0f;         // @252 `[OBS]` name; `[BIN]` %1231
    float faceMatrixClampMode = 0.0f;   // @254 `[OBS]` name; `[BIN]` %1193
};

inline constexpr std::size_t kUniformSlots = 106;

// The byte each of the 106 floats mirrors, in struct order. `[BIN]` from the
// TBAA node `!40` of mod98. Gated against `offsetof` so the two cannot drift.
inline constexpr std::uint16_t kUniformByte[kUniformSlots] = {
    0,   4,   8,   12,
    16,  20,  24,  28,  32,  36,  40,  44,  48,  52,  56,  60,  64,  68,  72,  76,
    80,  82,  84,  86,  88,  90,  92,  94,  96,  98,  100, 102,
    104, 106, 108, 110, 112, 114, 116, 118, 120, 122, 124, 126,
    128, 130, 132, 134, 136, 138, 140, 142, 144, 146, 148, 150,
    152, 156,
    160, 162, 164, 166, 168, 170, 172, 174, 176, 178, 180, 182,
    184, 186,
    188, 190, 192, 194, 196, 198, 200, 202, 204, 206, 208, 210, 212, 214, 216,
    218, 220, 222, 224, 226, 228, 230, 232, 234, 236, 238, 240, 242, 244, 246,
    248, 250, 252, 254,
};

static_assert(sizeof(Uniforms) == kUniformSlots * sizeof(float),
              "the mirror must be 106 packed floats -- the probe memcpys it");

// =========================================================================
// block 1 -- coverage
// =========================================================================

// `[BIN]` mod98 `%81`..`%86`: `fpext` the red channel, `llvm.fmuladd.f32`
// against fields #0[0] and #0[1], `fptrunc` back.
//
// The two RE-SAMPLES decode the same way but in HALF (`%123` and `%154` are
// `llvm.fmuladd.f16` on `fptrunc`ed copies of the same two floats). In float32
// the two spellings collapse, so one function serves all three sites; the
// divergence is recorded rather than reproduced, because reproducing it would
// mean carrying a half rounding this port has already declared out of scope.
float decodeDistance(float raw, float scale, float bias);

// `[BIN]` mod98 `%158`..`%164`: the SDF's `.yz` widened to float2, then
// `llvm.fmuladd.v2f32` against fields #1[0] and #1[1] SPLATTED. Note that mod98
// keeps this gradient in FLOAT for the dispersion (`%583`/`%587` consume `%164`
// directly) and narrows it to half (`%170`) for everything else.
void decodeGradient(const float raw[2], float scale, float bias, float out[2]);

// `[BIN]` mod98 `%91`..`%98`. `fwidth` is the caller's -- a derivative is not
// something an oracle can hold -- and the guarded divisor uses mod98's own
// epsilon (`0xH1419`, see `GlassOracle::kEpsilon`), not AquaKit's `1e-4`.
float coverage(float d, float fwidthD);

// `[BIN]` mod98 `%100`: the SDF's alpha times the coverage. One multiply, named
// because everything downstream branches on it and the spec calls it `mask`.
float mask(float sdfAlpha, float coverage);

// `[BIN]` mod98 `%109`: `p - (field #14[0], field #14[1])`, taken only when
// `mask < 1` (`%101`). This is where the shadow lobe reads its distance.
void shadowSamplePoint(const float p[2], float offsetX, float offsetY, float out[2]);

// `[BIN]` mod98 `%139`/`%140`: `p - (0, field #56)`. X is a literal zero in the
// `insertelement`, not a uniform -- the ring shadow only ever offsets in Y.
void ringShadowSamplePoint(const float p[2], float offsetY, float out[2]);

// =========================================================================
// block 3 -- the key-fill highlight  (`shaderVariant & 1`)
// =========================================================================

// `[BIN]` mod98 `%173`..`%260`. Returns zero on any of its four early-outs.
//
// `highlightPlacement` (@250) is a two-way switch, not a scale: when it is
// positive the block takes `%209`/`%196` -- coverage falls out of `1 - mask` and
// the softening factor is a literal 1 -- and when it is not it takes `%211`/
// `%198`, where both come from the distance. `[OBS]` The key name is unread;
// the shape is a placement selector and AquaKit's `SdfKeyFillHighlight` has the
// same fork.
//
// `[BIN]` The `0xH3A00` at `%206` is 0.75, and it mixes the softening factor
// toward a HARD indicator (`%205`, an integer-converted zero-or-one) rather than
// toward a constant -- a detail an approximation would flatten.
float keyFillHighlight(float d, float fwidthMax, float sdfAlpha, float maskValue,
                       const float grad[2], float height, float effectOffset,
                       float placement, float edge, float projX, float projY,
                       float spread);

// =========================================================================
// block 4 -- the ring shadow
// =========================================================================

// `[BIN]` mod98 `%261`..`%308`. Two evaluations of the edge curve on distances
// one stroke width apart, subtracted -- which is the "TWO evaluations of the Phi
// polynomial, subtracted" the spec lists as block 4.
//
// `[BIN]` Both evaluations go through `kInvSqrt2` (`%284`, `%293`), so this is
// `edgeCoverageInSigma`. The SHADOW lobe's single evaluation (`%342`) does NOT,
// which is why `GlassOracle` carries the pre-scale as a separate entry point.
float ringShadow(float d2, float sdfAlpha2, float maskValue, float opacity,
                 float ringMask, float blurRadius, float strokeWidth);

// =========================================================================
// block 5 -- the shadow lobe
// =========================================================================

// `[BIN]` mod98 `%315`..`%331` -- `bandAmount` with the shadow's distance
// offset, the only one of mod98's five band sites that reaches its offset by
// `-(offset + d)` rather than `-d - offset`. Identical in IEEE, and delegated to
// `GlassOracle::bandAmount` rather than re-spelled.
float shadowHeight(float d, float amount, float invHeight, float distanceOffset);

// `[BIN]` mod98 `%337`..`%354`. `shadowRadius` (@76) arrives as a RECIPROCAL
// (doc 03 §27.2: the packer divides), so this multiplies. The edge curve is
// called WITHOUT the sigma pre-scale here.
float shadowCoverage(float d1, float sdfAlpha1, float shadowRadius, float shadowOpacity);

// `[BIN]` mod98 `%394`..`%450` when the backdrop was fetched, `%452`..`%464`
// when it was not. `sampled` is `(shaderVariant & 4) && vibrancy > kEpsilon`
// (`%366`, whose immediate `0x3F50640000000000` is the double widening of the
// half `0xH1419` -- the same epsilon, spelled as a double by the printer).
//
// `[BIN]` The matrix here is applied WITHOUT its bias, the result is scaled by
// `vibrancy`, and only THEN the bias is added (`%434` then `%444`). That is not
// `applyFaceColorMatrix`'s arrangement and it is not delegated to it: scaling
// after a bias add and scaling before it are different functions.
void shadowColor(const float backdropUnpremultiplied[3], bool sampled, float vibrancy,
                 const float matrix[3][4], float matrixAlpha, float out[4]);

// `[BIN]` mod98 `%467`..`%469`: the colour splat-multiplied by the coverage.
void shadowPremultiplied(const float color[4], float coverage, float out[4]);

// =========================================================================
// block 6 -- the inner refraction, and the blur radius ramp
// =========================================================================

// `[BIN]` mod98 `%483`..`%490` -- `bandAmount` with NO offset.
float innerRefractionHeight(float d, float amount, float invHeight);

// `[BIN]` mod98 `%492`..`%546`: three linear segments over four distances, each
// contributing an opacity, subtracted from a base and scaled.
//
//     lo    = (d0, d1, d2);  hi = (d1, d2, d3)
//     t     = saturate((x - lo) / (hi - lo))        // as fma(x, 1/span, -lo/span)
//     v     = opacity0 - ((t.x*o1 + t.y*o2) + t.z*o3)
//     radius= blurRadius * v
//
// `[BIN]` The reciprocal and the bias are computed ONCE (`%516`/`%518`) and
// reused by the outer refraction's second evaluation at `%792` -- the compiler
// hoisted them, which is how we know the two sites are the same expression.
//
// `[BIN]` The sum's association is `prod.z + (prod.x + prod.y)` (`%538` then
// `%540 = fadd %539, %538`), not left to right. Transcribed, because a float sum
// is not associative and the differential is bit for bit.
float blurRadiusRamp(float x, const float distances[4], const float opacities[3],
                     float opacity0, float scale);

// `[BIN]` mod98 `%841`..`%846`: when `shaderVariant & 2` is CLEAR there is no
// ramp at all -- the radius is `opacity0 * blurRadius`, with the operands in the
// opposite order to `%545`. Commutative, and transcribed as written.
float flatBlurRadius(float opacity0, float scale);

// `[BIN]` mod98 `%335`, `%550`, `%807`, `%974` -- four sites, one shape:
// `fma(grad, height, p)`. The point the backdrop is fetched at.
void refractPoint(const float p[2], const float grad[2], float height, float out[2]);

// =========================================================================
// block 7 -- the chromatic dispersion
// =========================================================================

// `[BIN]` mod98 `%591`..`%601` -- `bandAmount` with the aberration's offset.
float aberrationHeight(float d, float amount, float invHeight, float offset);

// `[BIN]` mod98 `%573`..`%607`. Two dots of the FLOAT gradient against a
// rotation's two rows, then the pair SWAPPED (`%604` is `<%587, %583>`) and
// scaled by the band. The taps and the combine are `GlassOracle`'s -- they are
// shared with mod99 and already gated there.
//
// `[BIN]` Loop one adds `+step` (`%624`), loop two adds `-step` (`%617`).
void aberrationStep(const float grad[2], float height, float cosA, float sinA,
                    float out[2]);

// =========================================================================
// block 8 -- the blur fill (darken / lighten)
// =========================================================================

// `[BIN]` mod98 `%749`..`%764`.
//
//     acc = min(a,b) * darken + lighten * max(a,b)
//     out = mix(a * (1 - darken - lighten) + acc, b, normal)
//
// DIVERGENCE FROM THE SPEC'S BLOCK LIST, which writes
// `fma(min(a,b), kL, max(a,b)*kD)` -- the IR puts DARKEN on the min (`%756`
// multiplies `%749`, the `fmin`, by `%719` = byte 246) and LIGHTEN on the max
// (`%755` multiplies `%750`, the `fmax`, by `%716` = byte 244). Doc 03 §27.4's
// key names agree with the IR: 246 is `inputBlurFillDarkenOpacity`. Both the
// spec's names and the IR's positions are recorded; the IR wins.
//
// `[BIN]` And the complement is `(1 - darken) - lighten` in that association
// (`%757` then `%758`), not `1 - (darken + lighten)`.
void blurFill(const float face[3], const float backdrop[3], float lighten,
              float darken, float normal, float out[3]);

// =========================================================================
// block 9 -- the outer refraction
// =========================================================================

// `[BIN]` mod98 `%780`..`%787` -- `bandAmount` with NO offset, its own pair.
float outerRefractionHeight(float d, float amount, float invHeight);

// `[BIN]` mod98 `%823`..`%835`: a linear ramp between two refraction distances,
// saturated, scaled by the refraction opacity.
//
// `[BIN]` The two halves of this ramp are computed at DIFFERENT precisions in
// the target -- `%829` divides in half, `%831` divides in float and narrows --
// which in float32 collapses to one expression. Recorded; not reproducible here
// for the reason the header gives.
float outerRefractionWeight(float d, float distance0, float distance1, float opacity);

// `[BIN]` mod98 `%838`: `air.mix` over all FOUR channels, alpha included.
void outerRefractionMix(const float face[4], const float outer[4], float weight,
                        float out[4]);

// =========================================================================
// block 10 -- desaturate, then the face colour matrix
// =========================================================================

// `[BIN]` mod98 `%861`..`%868`: `rgb / max(a, kEpsilon)`, and the alpha the
// result carries is a literal ONE (`%868`), not the alpha it divided by.
void unpremultiply(const float rgba[4], float out[4]);

// `[BIN]` mod98 `%869`..`%948`.
//
// THE DETAIL A PLAUSIBLE TRANSCRIPTION LOSES: the luminance compression feeds
// the MATRIX, but the final mix interpolates from the UNCOMPRESSED colour --
// `%946` is `air.mix(%866, %943, ...)` and `%866` is the raw un-premultiplied
// input, while `%894` (the compressed one) reaches only `%906`/`%919`/`%932`.
// Mixing from the compressed colour would be one instruction shorter and a
// different picture wherever `maxLuma` bites.
void faceColor(const float unpremultiplied[3], float faceOpacity, float maxLuma,
               const float matrix[3][4], float out[3]);

// =========================================================================
// block 11 -- the specular
// =========================================================================

// `[BIN]` mod98 `%1048`..`%1072`. A squared ramp times a weight raised to the
// FOURTH power by two squarings (`%1070`, `%1071`).
//
// `[BIN]` The luma triple here is `<0xH32CD, 0xH39B9, 0xH2C9D>` -- the SECOND
// Rec. 709 rounding, one half-ULP below the one the compression uses in red and
// in blue. `GlassOracle::kLumaWeight` is that triple and `kLumaCompress` is the
// other; which goes where is `[BIN]`, not a choice.
float specularWeight(float d, const float faceRgb[3], float distance0, float distance1,
                     float opacity, float weightScale, float weightBias);

// `[BIN]` mod98 `%999`..`%1077`: the third colour matrix over the specular's own
// backdrop fetch, mixed into the face by the weight. Alpha is carried through
// untouched (`%1077` shuffles lane 7).
void specular(const float face[4], const float backdropUnpremultiplied[3], float weight,
              const float matrix[3][4], float out[4]);

// `[BIN]` mod98 `%963`..`%970` -- the specular's own `bandAmount`, with NO
// offset and its own `(amount, invHeight)` pair. The third of mod98's three
// offset-free band sites; `GlassOracle`'s header counts five in all.
float specularHeight(float d, float amount, float invHeight);

// =========================================================================
// block 12 -- composite, ring shadow, key-fill highlight, epilogue
// =========================================================================

// WHICH VALUE IS WHICH, BECAUSE THE PHI NODES SWAP THEM AND THE SPEC'S BLOCK
// LIST DOES NOT SAY. `[BIN]` `%310` is the phi fed by the `%173` block group --
// the KEY-FILL HIGHLIGHT, whose uniforms are @228/@234..@240 -- and it is what
// drives the epilogue at `%1090`. `%311` is fed by `%273` -- the RING SHADOW,
// whose uniforms are @218..@226 -- and it is what lands in alpha at `%1084`. A
// transcription that read the two the other way round would be self-consistent
// and would put the shadow where the highlight belongs.

// `[BIN]` mod98 `%355`..`%361`: `mask == 0 && shadowCoverage < eps &&
// highlight < eps && ringShadow < eps` short-circuits the whole function to
// `zeroinitializer` (`%470` -> `%1298`). The comparisons are `olt`, so a NaN
// would NOT short-circuit -- `nnan` is set on the function, so that is moot, and
// it is transcribed as written rather than as `<=`.
bool isZero(float maskValue, float shadowCoverage, float highlight, float ringShadow);

// `[BIN]` mod98 `%1080`..`%1082`: `air.mix(shadow, face, mask)` over four
// channels. The spec's block 11.
void composite(const float shadowPre[4], const float facePre[4], float maskValue,
               float out[4]);

// `[BIN]` mod98 `%1084`..`%1088`: the colour scaled by `1 - ringShadow`, with the
// ring shadow added into ALPHA ONLY -- the constant vector is
// `<0, 0, 0, ringShadow>`.
void applyRingShadow(const float col[4], float ringShadow, float out[4]);

// `[BIN]` mod98 `%1117`..`%1209`: the face matrix run a second time, on the
// highlight's own backdrop fetch, with an extra clamp whose DIRECTION is chosen
// by `highlightColorBias`'s sign (`%1199`) and whose presence is gated by
// `faceMatrixClampMode` (`%1194`). `[OBS]` Neither key is bound by doc 03 §27.4.
void highlightFillColor(const float backdropUnpremultiplied[3], float highlight,
                        float faceOpacity, float maxLuma, const float matrix[3][4],
                        float clampMode, float colorBias, float out[3]);

// `[BIN]` mod98 `%1089`..`%1241`. Early-out when `highlight <= kEpsilon` (`%1089`
// is `ogt`, so equality does NOT pass). The `alpha < 1` fork decides whether the
// highlight's backdrop is composited under the colour at all (`%1093`); when
// alpha is already one the source is `zeroinitializer` and only the shaping runs.
//
// `[BIN]` `highlightColorBias` (@240) does DOUBLE DUTY -- it picks the clamp
// direction inside `highlightFillColor` and it is the shaping gain here
// (`%1223`). One uniform, two jobs, and no reading suggests they are separable.
void applyKeyFillHighlight(const float col[4], float highlight,
                           const float backdropUnpremultiplied[3], float faceOpacity,
                           float maxLuma, const float matrix[3][4], float clampMode,
                           float colorBias, float alphaGain, float out[4]);

// `[BIN]` mod98 `%1244`..`%1277` -- the fade. Un-premultiplies, scales by the
// holding-tone white, RE-premultiplies by a SATURATED alpha, and mixes toward
// that by `maxHeadroom * (1 - ramp)`.
void fade(const float col[4], float d, float headroom, float gradientDistance0,
          float gradientInvSpan, float toneWhite, float out[4]);

// `[BIN]` mod98 `%1280`..`%1297` -- the HDR ceiling. `-0.75` is `0xHBA00`, the
// spec's `ClampNegativeFloor`, and it is a FLOOR on the un-premultiplied colour,
// so the clamp is asymmetric on purpose.
//
// `[BIN]` The re-premultiply uses the RAW alpha (`%1294` splats lane 3 of the
// input), not the epsilon-floored one it divided by. At alpha below the epsilon
// those differ, and the target keeps them different.
void clampHeadroom(const float col[4], float ceiling, float out[4]);

// =========================================================================
// the assembly -- the control flow, and the only place the variant lives
// =========================================================================
//
// WHY THIS EXISTS AND WHY IT HAS NO GLSL TWIN.
// The blocks above are where the arithmetic is, and every one of them is gated
// against the GPU bit for bit. But mod98's ~65 basic blocks also contain a
// CONTROL FLOW: three variant bits, fourteen uniform-is-zero early-outs, and one
// short-circuit to `zeroinitializer`. None of that is arithmetic and none of it
// is visible from a single block, so it needs somewhere to live and something to
// bite it. This is that place.
//
// It is CPU-only, and the reason is the sampler: assembling the function means
// taking fifteen texture fetches, and a GPU twin would need a real image, a real
// mip chain and the target's filtering before it could be compared to anything.
// What the caller supplies instead is two interfaces. The differential that
// matters -- the arithmetic -- already ran, block by block, on the GPU.
//
// `[OBS]` NOT REPRODUCED HERE: the sampler, the mip chain, the layer UV
// transform, and the LOD. `Backdrop::sample` is handed a point in the effect's
// own space and a RADIUS, and it owns everything between that and a colour --
// `RB::Layer`'s affine clamp (`DistanceField.h`) and `backdropLod`
// (`MipPyramid.h`) are both already transcribed and gated, and this must not
// grow a second copy of either.

// The SDF, `t1`. Returns the RAW texel: `.x` the encoded distance, `.yz` the
// encoded gradient, `.w` the coverage. `[BIN]` mod98 samples it three times.
struct Field {
    virtual ~Field() = default;
    virtual void sample(const float p[2], float out[4]) const = 0;
};

// The backdrop pyramid, `t0`. Returns the PREMULTIPLIED colour at `p` for a
// blur `radius`; turning the radius into a level is the implementation's job.
// `[BIN]` mod98 samples it up to twelve times.
struct Backdrop {
    virtual ~Backdrop() = default;
    virtual void sample(const float p[2], float radius, float out[4]) const = 0;
};

struct Inputs {
    float p[2] = {0.0f, 0.0f};
    // `[BIN]` mod98 `%91`/`%171` -- `air.fwidth` of the DECODED distance, taken
    // twice and hoisted by the compiler into one value. A derivative belongs to
    // the rasteriser, so it is an input here.
    float fwidthD = 0.0f;
    // `[BIN]` the function constant `RB::shaderVariant`.
    std::uint32_t variant = 0;
};

// `[BIN]` mod98 `%49`..`%1298`, in the IR's own order.
void evaluate(const Inputs& in, const Uniforms& u, const Field& field,
              const Backdrop& backdrop, float out[4]);

}  // namespace glassbg
}  // namespace rb
