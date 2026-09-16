#pragma once
// `sdf_glass_displacement` on the CPU -- the twin of `shaders/SdfDisplacement.glsl`.
//
// THIS IS THE GENERATOR OF THE DISPLACEMENT MAP, and it is the one stage of the
// icon's glass whose provenance is WEAKER than the rest of this tower. Read the
// next three sections before trusting anything below them.
//
// WHERE THE GLASS COMES FROM, AND WHY THIS FILE HAD TO GO OUTSIDE RENDERBOX
// ------------------------------------------------------------------------
// `[ART]` `RenderBox` exports five system shaders; `IconRendering` imports
// exactly one, `_RBSystemShaderDisplacementMap`, and the string
// `glassBackground` appears zero times in all seven non-RenderBox slices
// (`Docs/Specs/2026-09-02-vidro.md` §4.4, `Docs/03-o-motor-de-render.md` §29).
// So the icon's glass is not drawn by `glassBackground_v1`. It is:
//
//   1. the layer's shape becomes a signed distance field  -- `DistanceField.h`,
//      and that generator is OURS, declared, not a transcription;
//   2. a DISPLACEMENT MAP is generated from that field    -- THIS FILE;
//   3. `displacementMap_v1` applies it                    -- `DisplacementOracle.h`,
//      transcribed from mod97 with a 0 ULP differential on all four tap modes.
//
// `[BIN]` `IconRendering` writes a 32-byte blob through
// `-[RBDisplayList addStyle:data:]` style 3, which the jump table at `0x15E440`
// dispatches to `RB::DisplayList::State::add_glass_displacement` (`0xF39F0` in
// `RenderBox.arm64`). Its XML serialiser at `0xEDB24` names every field:
//
//     component . gradient . range(float2) . offset . height . curvature .
//     angle . mask-offset
//
// THE TWO NAMES THAT ARE NOT THE EFFECT'S -- `component` AND `range` -- BELONG
// TO THE FIELD, AND THEY WERE READ ON 2026-09-15
// ---------------------------------------------------------------------------
// `Docs/Laudos/2026-09-15-refracao.md` has the whole of it. In short:
//
// `[BIN]` `DistanceGradient` is a five-case dense enum, read from the XML
// serialiser's own string table at `0x18D978` (`0xEDBD0`-`0xEDC20`, with the
// out-of-range fallback at `0x16F735` naming the default the serialiser SKIPS,
// exactly as `component`'s fallback `red` doubles its default case 3):
//
//     0 sampled . 1 stored . 2 stored-inverse . 3 stored-float .
//     4 stored-float-inverse
//
// and `IconRendering` writes **1 = `stored`** (`0x10CC4` `mov w8, #0x100`, a
// halfword store putting `component = 0` and `gradient = 1`).
//
// `[BIN]` `range` is that stored field's DECODE INTERVAL and nothing else:
// `RB::CGContext::apply_glass_displacement` (`0xC1324`) reads it ONLY on the
// `gradient != 0` side of `0xC14A8 cbz` -- the `sampled` side derives the
// gradient by a 15-tap separable convolution instead (`0xC1534`) and never
// touches it. On the stored side it folds to a scale and a bias,
//
//     0xC14B0  ldr   d0, [x21]          ; (range.x, range.y)
//     0xC14B8  dup   v1.2s, v0.s[0]
//     0xC14BC  ld1   {v1.s}[0], [x9]    ; x9 = effect+8 = `offset`
//     0xC14C0  fsub  v0.2s, v1.2s, v0.2s ; (offset - range.x, range.x - range.y)
//     0xC15EC  rev64 v0.2s, v0.2s
//
// paired with a per-case unorm->signed `(scale, bias)` constant chosen by the
// same enum: `stored` (2, -1) `0x15F868`, `stored-inverse` (-2, 1) `0x15F860`,
// `stored-float` (1, 0) `0x15C760`, `stored-float-inverse` (-1, 0) `0x15F168`.
// A float2 that is one endpoint per channel extreme is a decode interval.
//
// `[BIN]` AND THE `v` OF `(v, -v)` HAS A NAME. The blob's `range` is written at
// `0x10CCC` (`stp s0, s1` after `fneg s1, s0`) from the third floating-point
// argument of `0x10C14`; both call sites pass the same thing --
// `0x4A6F8`/`0x4A8E4` load it from `[sp,#0x40]`, stored at `0x4A658`/`0x4A410`
// from `d14`, which `0x4A3F0` moved out of `x24 = [x0, #0xA8]` (`0x4A324`).
// `x0` is the `0xC0`-byte `IconRendering.FinalizedIcon.Layer`, whose reflection
// (`0xA301C`) puts `sdf: IconRendering.SDF` at `+0x98` and `shadowImage` at
// `+0xB0`; `IconRendering.SDF` is the two-field struct at `0xA2FF4`,
// `{texture, maxDistance: Swift.Double}` -- the ONLY `maxDistance` in the whole
// reflection -- so `+0xA8` is its trailing `Double`:
//
//     range = (sdf.maxDistance, -sdf.maxDistance)
//
// `[BIN]` The cross-check is a second, independent consumer of the same field:
// the glyph specular loads `[descriptor+0xA8]` into shader argument 7 beside a
// literal `0.5` at argument 8 (`0x49238`, `0xEAA8`) and decodes
// `sd = (0.5 - tex.r) * (-2 * maxDist)` -- which is `2*M*u - M`, the same
// symmetric interval this `range` spells, with the same magnitude. Two paths,
// one scalar, one name. This closes `[OBS]` 4 of doc 03 §29.8.
//
// `[INF]` Four of those names -- `angle`, `curvature`, `height`, `mask-offset`
// -- match `CASDFGlassDisplacementEffect`'s four properties 4 of 4. That effect
// is QuartzCore's, not RenderBox's: RenderBox's shader library contains no
// displacement GENERATOR at all (all 16 translation units were checked, and
// `displacementMap_v1` RECEIVES the map as a texture). So the arithmetic that
// builds the map had to be read out of QuartzCore.
//
// THE PROVENANCE, STATED PLAINLY BECAUSE IT IS NOT THE TOWER'S USUAL ONE
// ----------------------------------------------------------------------
// `[BIN]` The source transcribed here is `UberShader<T,U>::sdf_glass_displacement`
// from QuartzCore's `default.metallib`, macOS 27.0 build `26A5416b` -- THE SAME
// BUILD as this project's target. But it was not read from that metallib by this
// project. It was read by ANOTHER project, AquaKit, whose 1:1 transcription lives
// at `D:/CodingProjects/AquaKit/Source/QuartzCore/shaders/GlassCommon.glsl`
// (function `SdfGlassDisplacement`, line 7), with a CPU twin at
// `tests/GlassShaderReference.h::ReferenceDisplacement` and the decode written up
// in `docs/shaders-glass-quartzcore.md` §3 and §11.
//
// `[OBS]` **THIS PROJECT DID NOT RE-READ THE IR.** Every other stage of this
// tower is sealed against a metallib under `References/` that a reader here can
// open -- and where RenderBox and QuartzCore both had a copy, the two were held
// against each other. Here there is NO RenderBox-side copy to cross-check
// against: the generator is not in RenderBox's metallib. The authority of this
// transcription is therefore AquaKit's read of QuartzCore and nothing else, and
// that is one link longer than every other `[BIN]` in this directory. What the
// gate in `Tests/test_rb_sdf_displacement.cpp` proves is that our CPU, our GPU
// and AquaKit's independent CPU twin agree -- three implementations of ONE
// reading, not two readings.
//
// The one claim checkable from inside this project is the parameter VOCABULARY:
// the four scalars QuartzCore's function takes are the four fields RenderBox's
// own serialiser names, and that serialiser was read here. It confirms that the
// arithmetic is being fed the right nouns; it does not confirm the arithmetic.
//
// THE DIVERGENCE THIS PORT DECLARES
// ---------------------------------
// `[BIN]` The target computes `w = max(fwidth(t), 1e-4)` from the RASTERISER's
// screen derivative. A CPU has no fwidth and a compute dispatch has no fragment
// quad, so `fwidthT` is an ARGUMENT here and in the GLSL -- the same choice
// `DistanceField.h` made for `dpdx`/`dpdy`, and the same one AquaKit's CPU twin
// makes. A fragment caller takes `fwidth()` of `maskDistance()` and passes the
// result in; nothing downstream moves. The floor of `1e-4` does NOT go to the
// caller, it stays in the function, because it is the target's.
#include <cstdint>

namespace rb {
namespace sdfdisp {

// `[BIN]` The function's three inputs, in QuartzCore's own packing (AquaKit
// `docs/shaders-glass-quartzcore.md` §3):
//
//     field  = (d, gx, gy, coverage)     -- the SDF generator's output
//     params = (height, curvature, offset, mask-offset)
//     rot    = (cos, sin)                -- the angle arrives already decomposed
//
// `[INF]` `params.w` is `mask-offset` from RenderBox's serialiser; AquaKit's
// write-up calls the same slot `cutoff`, from its position in the arithmetic
// (`t = s - params.w`, and `t` is what the mask ramps on). One float, two names,
// and the RenderBox name is the one this project can point at a binary for.
struct Field {
    float d = 0.0f;
    float gx = 0.0f;
    float gy = 0.0f;
    float coverage = 1.0f;
};

struct Params {
    float height = 0.0f;      // params.x -- REACH, measured inward from the edge
    float curvature = 0.0f;   // params.y -- blends the two profiles, [0,1]
    float offset = 0.0f;      // params.z
    float maskOffset = 0.0f;  // params.w
};

// `[BIN]` `rot` is a float2, and the shader builds a 2x2 out of it with rows
// `(rot.x, -rot.y)` and `(rot.y, rot.x)`. See `direction()` for why those rows
// are what they are, and for the scar that names them.
struct Rot {
    float cos = 1.0f;
    float sin = 0.0f;
};

// The return: `(disp.x, disp.y, alpha, 1.0)`. The `.w` is a LITERAL 1.0 in the
// target rather than a computed value, so it is carried as a field instead of
// being folded away -- a consumer that reads `.w` as a coverage is reading a
// constant.
struct Result {
    float x = 0.0f;
    float y = 0.0f;
    float alpha = 0.0f;
    float one = 1.0f;
};

// ---- the constants, and the one that must not be "corrected" --------------

// `[BIN]` `0.2929`, EXACT. It is not `1 - sqrt(2)/2 = 0.29289321881...`: the two
// part at the fifth digit, ~6.8e-6 absolute, which is tens of float ULP. AquaKit
// seals it as a constant written by hand in Apple's source and rounded there, not
// derived at compile time (`docs/shaders-glass-quartzcore.md` §3). Replacing it
// with the prettier closed form is a plausible wrong pixel, and
// `the_flat_profile_is_not_one_minus_root_two_over_two` stands in the way of it.
inline constexpr float kFlatDrop = 0.2929f;

// `[BIN]` The band's hard cutoff, in the FIELD's distance units. AquaKit's
// transcription carries a warning that applies here too: `-5.0` is a LENGTH
// compared against the field, so it is only correct while the field's ruler is
// one pixel per point. `[OBS]` Our field's ruler is not established against the
// target, so a scaled path would need this multiplied the way AquaKit's
// `SdfKeyFillHighlight` multiplies its own copy by `pointsToPixels`.
inline constexpr float kBandCutoff = -5.0f;

// `[BIN]` The floor under the screen derivative -- Apple's canonical
// antialiasing pattern, the same literal `sdf_glass_highlight` and
// `sdf_key_fill_highlight` carry.
inline constexpr float kMinFilterWidth = 1e-4f;

// `[BIN]` `+[CASDFGlassDisplacementEffect defaultValues]` @ `0x18b316684`; the
// block at `0x18b3166cc` returns a dictionary already frozen in rodata at
// `0x1e8970978`, COUNT 3. Read by AquaKit out of QuartzCore, same build.
inline constexpr float kDefaultAngle = 0.0f;
inline constexpr float kDefaultCurvature = 1.0f;   // clamped to [0,1] by the setter
inline constexpr float kDefaultHeight = 20.0f;     // an NSNumber int in the dictionary

// `[OBS]` `maskOffset` HAS NO DEFAULT. The frozen dictionary holds three keys and
// the class has four properties; the fourth exists and is settable and simply is
// not in the dictionary, so it stays whatever the ivar is born with. The gap is
// carried rather than filled: a `kDefaultMaskOffset = 0.0f` next to the other
// three would read as a fourth measurement, and there are only three.

// ---- the stages, each of them separately probeable ------------------------

// `[BIN]` `s = -(field.x + params.z)`: the ORIENTED depth, positive inward.
float depth(const Field& field, const Params& params);

// `[BIN]` `t = s - params.w`. This is what the mask ramps on and what the `-5.0`
// cutoff is compared against, and it is the value a fragment caller has to take
// `fwidth()` of.
float maskDistance(const Field& field, const Params& params);

// `[BIN]` `w = max(fwidth(t), 1e-4)`.
float filterWidth(float fwidthT);

// `[BIN]` `saturate(t/w + 0.5)`, the mask's antialiasing ramp. Split out of
// `alpha()` so the gate can read it on its own instead of the probe carrying a
// second copy of the expression.
float maskRamp(float t, float w);

// `[BIN]` `alpha = saturate(t/w + 0.5) * field.w`, then `if (t < -5.0) alpha = 0`.
//
// The cutoff zeroes ONLY the alpha. `disp.xy` is untouched by it, and that is not
// an oversight to tidy up: below the cutoff the caller is expected to be masked
// out by the alpha, so a transcription that zeroed the displacement too would
// agree everywhere the mask is consumed and disagree everywhere it is not.
float alpha(float t, float w, float coverage);

// `[BIN]` `x = saturate(s / params.x)` -- 0 at the edge, 1 at the end of the
// band. `[OBS]` There is NO guard on `height == 0` in the target: the division is
// unconditional, so a zero height leaves `x` at an infinity (saturating to 1, or
// to 0 for a negative `s`) and, at exactly `s == 0`, at a NaN. This transcription
// reproduces that instead of inserting a guard the target does not have.
float bandCoordinate(float s, float height);

// `[BIN]` `flat = 1.0 - 0.2929 * (x < 1.0 ? 1.0 : 0.0)`. Inside the band this is
// `0.7071`, the normal of a flat 45-degree bevel, constant across the band.
// Outside it the profile is 1, which makes the displacement exactly zero.
float flatProfile(float x);

// `[BIN]` `circ = sqrt(1.0 - (1.0 - x) * (1.0 - x))` -- a quarter circle: maximum
// displacement at the edge, falling smoothly to zero. Written in the target's
// algebraic form and NOT as the equivalent `sqrt(x * (2 - x))`, because the two
// round differently and this stage is gated against the GPU.
float circularProfile(float x);

// `[BIN]` `mag = mix(flat, circ, curvature)`, and `mix` is the target's
// `air.mix`, i.e. `flat + (circ - flat) * curvature`. GLSL's `mix` is free to
// expand as `x*(1-a) + y*a`, which rounds differently, so both sides spell the
// interpolation out.
float magnitude(float flat, float circ, float curvature);

// `[BIN]` `dir = (dot(g, (rot.x, -rot.y)), dot(g, (rot.y, rot.x)))`.
//
// THE ROTATION TAKES THE GRADIENT. AquaKit carries a scar exactly here
// (`docs/re/2026-08-28-aberracao-a-matriz-nao-e-o-displacement.md`): an earlier
// version of its aberration block fed the DISPLACEMENT into this matrix instead
// of the gradient, so the second row became `(alpha, 1.0)` -- a coverage and a
// literal -- and inside the shape the dispersion picked up a floor that did not
// vary with the geometry at all. That laudo's conclusion is that the matrix is a
// TRANSFORM from field space into UV, supplied by the CPU and texel-scaled, and
// that what it multiplies is `field.yz`.
//
// `[OBS]` Whether the caller folds that texel scale into `rot`'s MAGNITUDE is not
// read on this side. With a unit `(cos, sin)` these rows are a pure rotation and
// `|dir| == |g|`; with a longer `rot` they are a similarity and the length scales
// with it. The function is agnostic either way, and the gate pins both facts
// rather than assuming the unit case.
void direction(const Field& field, const Rot& rot, float (&dir)[2]);

// `[BIN]` `saturate((params.x - s) / w + 0.5)` -- the ramp that turns the
// displacement off past the end of the band. It divides by the SAME `w` the mask
// used, which is `fwidth(t)` and not `fwidth(s)`; the two agree only because `t`
// and `s` differ by a constant.
float fade(float s, float height, float w);

// The whole function, in the order the target runs it.
Result glassDisplacement(const Field& field, const Params& params, const Rot& rot, float fwidthT);

}  // namespace sdfdisp
}  // namespace rb
