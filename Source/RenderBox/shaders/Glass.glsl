// The shared glass math -- the primitives `glassBackground_v1` and
// `glassForeground_v1` are both built from.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod98.ll`
// (`glassBackground_v1`) and `default_mod99.ll` (`glassForeground_v1`). The
// provenance of every constant, the divergences from AquaKit's reading of
// QuartzCore, and the measured error of the edge curve against the closed-form
// Gaussian all live in `Source/RenderBox/GlassOracle.h`, which this file is the
// twin of. ONE explanation, in one place; this side carries only what is about
// being GLSL.
//
// This is the file the renderer includes AND the file the differential gates, so
// what draws and what is verified cannot drift apart.
//
// WHY `precise` IS ON EVERY INTERMEDIATE, AND WHY THERE IS NO `fma()` ANYWHERE.
// The oracle and this file are compared bit for bit, so a driver contracting
// `a * b + c` into an fma, or reassociating a sum, is a failed gate rather than
// a rounding footnote. `precise` is what stops that: glslc turns it into SPIR-V's
// `NoContraction`.
//
// `[ART]` But it only turns it into `NoContraction` on `OpFMul` and `OpFAdd`.
// Disassembling this probe's SPIR-V shows `OpExtInst ... Fma` carrying NO
// decoration, and GLSL.std.450 allows an undecorated `Fma` to be decomposed into
// `a * b + c` -- which this driver does. So `fma()` here would be a request the
// GPU declines while the oracle's `std::fma` obeys, and the two would part by an
// ulp. Every multiply-add on both sides is therefore written unfused. The
// oracle's header carries the full reading and the hazard it leaves behind.
#ifndef RB_GLASS_GLSL
#define RB_GLASS_GLSL

const float kRbGlassEpsilon = 0.00100040435791015625;  // 0xH1419

const float kRbEdgeInputScale = 0.25;    // 0xH3400
const float kRbEdgeInputBias = 0.5;      // 0xH3800
const float kRbEdgeDomainScale = 4.0;    // 0xH4400
const float kRbEdgeDomainBias = -2.0;    // 0xHC000

const float kRbEdgeC0 = 0.0029544830322265625;  // 0xH1A0D
const float kRbEdgeC1 = -0.034454345703125;     // 0xHA869
const float kRbEdgeC2 = 0.168212890625;         // 0xH3162
const float kRbEdgeC3 = -0.560546875;           // 0xHB87C
const float kRbEdgeBias = 0.5;                  // 0xH3800

const float kRbInvSqrt2 = 0.70703125;  // 0xH39A8

// Two Rec. 709 triples, one ulp of half apart in red and blue, both present in
// mod98 and both carried. See the oracle's header.
const vec3 kRbLumaCompress = vec3(0.212646484375, 0.71533203125, 0.07220458984375);
const vec3 kRbLumaWeight = vec3(0.2125244140625, 0.71533203125, 0.07208251953125);

const float kRbChromaBoost = 0.300048828125;  // 0xH34CD

const float kRbAberrationStepDown = -0.3333333432674408;
const float kRbAberrationStepUp = 0.3333333432674408;
const float kRbAberrationAlphaScale = 0.14285714924335479736328125;   // float(1/7)
const vec3 kRbAberrationRgbScale = vec3(0.5, 0.333251953125, 0.5);    // green is the HALF 1/3

// ---------------------------------------------------------- the edge curve

// `[BIN]` mod98 %285..%287. Algebraically a clamp of `x` to [-2, 2]; written as
// the target writes it because the two roundings are observable outside the
// clamp's dead zones.
float rbGlassEdgeDomain(float x) {
    precise float s = x * kRbEdgeInputScale + kRbEdgeInputBias;
    precise float c = clamp(s, 0.0, 1.0);
    precise float u = c * kRbEdgeDomainScale + kRbEdgeDomainBias;
    return u;
}

// `[BIN]` mod98 %288..%292, on `u` already in [-2, 2].
float rbGlassEdgePolynomial(float u) {
    precise float u2 = u * u;
    precise float p0 = kRbEdgeC0 * u2 + kRbEdgeC1;
    precise float p1 = p0 * u2 + kRbEdgeC2;
    precise float p2 = p1 * u2 + kRbEdgeC3;
    precise float r = p2 * u + kRbEdgeBias;
    return r;
}

float rbGlassEdgeCoverage(float x) {
    return rbGlassEdgePolynomial(rbGlassEdgeDomain(x));
}

// `[BIN]` mod98 %284 -- the pre-scale is applied before the domain mapping.
float rbGlassEdgeCoverageInSigma(float distanceInSigma) {
    precise float scaled = distanceInSigma * kRbInvSqrt2;
    return rbGlassEdgeCoverage(scaled);
}

// ------------------------------------------------------ the height profile

// `[BIN]` Five sites in mod98, two in mod99. `invHeight` arrives as a
// RECIPROCAL -- the packer already divided, with zero guarded to zero (doc 03
// §27.2) -- so there is no division here and there must not be one.
float rbGlassBandAmount(float d, float amount, float invHeight, float offset) {
    precise float raw = (-d - offset) * invHeight;
    precise float x = clamp(raw, 0.0, 1.0);
    precise float inner = x * (2.0 - x);
    precise float circ = clamp(sqrt(inner), 0.0, 1.0);
    precise float outAmount = -circ * amount + amount;
    return outAmount;
}

// --------------------------------------------------- luminance compression

// `[BIN]` mod98 %876..%892. The dot is spelled out rather than delegated to
// `dot()`, whose reduction order GLSL does not fix.
vec3 rbGlassCompressMaxLuma(vec3 c, float complement) {
    if (!(complement > 0.0)) return c;
    precise float y = (kRbLumaCompress.r * c.r + kRbLumaCompress.g * c.g) +
                      kRbLumaCompress.b * c.b;
    precise float ty = -y * complement + 1.0;
    precise float t = clamp(ty, 0.0, 1.0);
    precise float k = (1.0 - t) * kRbChromaBoost + 1.0;
    precise float grey = y * t;
    precise vec3 scaled = c * t;
    // `air.mix(x, y, a)` = `x + (y - x) * a`, and `k >= 1` here: the mix
    // extrapolates away from grey on purpose.
    precise vec3 r = vec3(grey) + (scaled - vec3(grey)) * k;
    return r;
}

// ------------------------------------------------------- the colour matrix

// `[BIN]` mod98 %906/%919/%932 then %943. Rows are `(coefficients, bias)`.
vec3 rbGlassFaceColorMatrixRaw(vec3 c, vec4 cm0, vec4 cm1, vec4 cm2) {
    precise float f0 = (c.r * cm0.x + c.g * cm0.y) + c.b * cm0.z;
    precise float f1 = (c.r * cm1.x + c.g * cm1.y) + c.b * cm1.z;
    precise float f2 = (c.r * cm2.x + c.g * cm2.y) + c.b * cm2.z;
    precise vec3 r = vec3(f0, f1, f2) + vec3(cm0.w, cm1.w, cm2.w);
    return r;
}

// `[BIN]` mod98 %946 -- mixed back over the input by `face_opacity`.
vec3 rbGlassApplyFaceColorMatrix(vec3 c, vec4 cm0, vec4 cm1, vec4 cm2, float faceOpacity) {
    precise vec3 f = rbGlassFaceColorMatrixRaw(c, cm0, cm1, cm2);
    precise vec3 r = c + (f - c) * faceOpacity;
    return r;
}

// -------------------------------------------------- the chromatic dispersion
//
// The sampling is the CALLER's, and the oracle's header says why: a texture is
// what an oracle cannot hold, so what is transcribed here is where each tap
// lands and what weight it carries, plus the combine. The caller's loop:
//
//     acc = 0; alphaSum = 0;
//     for (int i = 0; i < 7; ++i) {
//         vec4 t = rbGlassAberrationTap(i);
//         vec4 s = texture(tex, uv + t.x * step);
//         acc      += (s.rgb / max(s.a, kRbGlassEpsilon)) * t.yzw;
//         alphaSum += s.a;                  // the RAW alpha, not the floored one
//     }
//     result = rbGlassAberrationCombine(acc, alphaSum);

const int kRbAberrationTaps = 7;

// Returns `(offset, weightR, weightG, weightB)`. `[BIN]` mod98 %656/%697: the
// weight is a RUNNING SUM, and the loop below reproduces the accumulation rather
// than indexing it, because the two differ by a float at the third tap.
vec4 rbGlassAberrationTap(int index) {
    if (index < 3) {
        precise float w = 1.0;
        for (int i = 0; i < index; ++i) w = w + kRbAberrationStepDown;
        precise float g = 1.0 - w;
        return vec4(w, w, g, 0.0);
    }
    precise float w = 0.0;
    for (int i = 0; i < index - 3; ++i) w = w + kRbAberrationStepUp;
    precise float g = 1.0 - w;
    return vec4(-w, 0.0, g, w);
}

// `[BIN]` mod98 %701..%710. The alpha scale and the rgb scale are two separate
// multiplies in the IR and stay two here; the second one RE-PREMULTIPLIES, which
// is where RenderBox parts company with AquaKit.
vec4 rbGlassAberrationCombine(vec3 acc, float alphaSum) {
    precise float alpha = alphaSum * kRbAberrationAlphaScale;
    precise vec3 scaled = acc * kRbAberrationRgbScale;
    precise vec3 rgb = scaled * alpha;
    return vec4(rgb, alpha);
}

#endif
