#include "Source/RenderBox/GlassOracle.h"

#include <cmath>

namespace rb::glass {
namespace {

float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// `[INF]` The order the three products of an `air.dot.v3f16` are summed in is
// not in the IR. It is fixed here, once, and `Glass.glsl` spells out the same
// sum instead of calling `dot` -- a differential that is bit-for-bit cannot rest
// on an order neither side specifies.
float dot3(const float a[3], const float b[3]) {
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}

}  // namespace

// ------------------------------------------------------------ the edge curve

float edgeDomain(float x) {
    const float s = x * kEdgeInputScale + kEdgeInputBias;
    const float c = saturate(s);
    return c * kEdgeDomainScale + kEdgeDomainBias;
}

float edgePolynomial(float u) {
    // `[BIN]` mod98 %288..%292 -- one square then four `air.fma`s, in this order.
    // Unfused here and in the GLSL twin; the header says why, and says it was
    // measured rather than preferred.
    const float u2 = u * u;
    float p = kEdgeC0 * u2 + kEdgeC1;
    p = p * u2 + kEdgeC2;
    p = p * u2 + kEdgeC3;
    return p * u + kEdgeBias;
}

float edgeCoverage(float x) { return edgePolynomial(edgeDomain(x)); }

float edgeCoverageInSigma(float distanceInSigma) {
    // `[BIN]` mod98 %284: the scale is applied BEFORE the domain mapping, so the
    // clamp at +-2 bites in sigma, not in the raw distance.
    return edgeCoverage(distanceInSigma * kInvSqrt2);
}

// -------------------------------------------------------- the height profile

float bandAmount(float d, float amount, float invHeight, float offset) {
    // `[BIN]` mod98 %317/%324 -- the negation and the offset are one subtraction.
    const float x = saturate((-d - offset) * invHeight);
    // `[BIN]` mod98 %325..%329: `2 - x`, then the product, then the root, then a
    // SECOND saturate. The second one is not redundant on a GPU: `x * (2 - x)`
    // can round above 1 near x = 1, and `sqrt` of that is above 1 too.
    const float circ = saturate(std::sqrt(x * (2.0f - x)));
    // `[BIN]` mod98 %330/%331 -- `fma(-circ, amount, amount)`, which is
    // `amount * (1 - circ)` written the way the target writes it. Simplifying it
    // to the subtraction would round differently when `circ` is near 1.
    return -circ * amount + amount;
}

float reciprocalHeight(float height) {
    // `[BIN]` doc 03 §27.2. The compare is `> 0`, not `!= 0`: a negative height
    // takes the zero branch too.
    return height > 0.0f ? 1.0f / height : 0.0f;
}

// --------------------------------------------------- luminance compression

void compressMaxLuma(const float rgb[3], float complement, float out[3]) {
    // `[BIN]` mod98 %876 -- the guard is `ogt`, so a NaN complement falls
    // through untouched, and so does zero.
    if (!(complement > 0.0f)) {
        out[0] = rgb[0];
        out[1] = rgb[1];
        out[2] = rgb[2];
        return;
    }
    const float y = dot3(kLumaCompress, rgb);
    const float t = saturate(-y * complement + 1.0f);
    const float k = (1.0f - t) * kChromaBoost + 1.0f;
    const float grey = y * t;
    for (int i = 0; i < 3; ++i) {
        // `[BIN]` mod98 %892 -- `air.mix(grey, c * t, k)`, expanded as the
        // target's mix expands. `k >= 1` here, so this extrapolates.
        const float scaled = rgb[i] * t;
        out[i] = grey + (scaled - grey) * k;
    }
}

// ------------------------------------------------------- the colour matrix

void faceColorMatrixRaw(const float rgb[3], const ColorMatrix& m, float out[3]) {
    for (int i = 0; i < 3; ++i) {
        const float row[3] = {m.row[i].r, m.row[i].g, m.row[i].b};
        // `[BIN]` mod98 %933/%942/%943 -- the three dots are gathered into a
        // vector and the three biases into another, and the add is one vector
        // add. Same arithmetic per lane.
        out[i] = dot3(rgb, row) + m.row[i].bias;
    }
}

void applyFaceColorMatrix(const float rgb[3], const ColorMatrix& m, float faceOpacity,
                          float out[3]) {
    float f[3];
    faceColorMatrixRaw(rgb, m, f);
    for (int i = 0; i < 3; ++i) out[i] = rgb[i] + (f[i] - rgb[i]) * faceOpacity;
}

// ------------------------------------------------------ the YCC composite

ColorMatrix makeYccCompositeMatrix(float white, float black, float saturation,
                                   const float fillPremultiplied[4]) {
    const float gain = white - black;
    // `(1 - saturation) * 0.5` -- the recentring that makes `saturation` a scale
    // around grey rather than around zero, because the chroma comes out of the
    // YCC already carrying +0.5.
    const float chromaRecentre = (1.0f - saturation) * 0.5f;
    // Written in the binary's form, `sat * bias + recentre`, not in the
    // simplified one it is equal to -- reading it against the assembly is then
    // direct, and the simplification (the chroma bias is invariant in
    // saturation) stays a remark instead of becoming the code.
    const float chromaBias = saturation * kYccBias[1] + chromaRecentre;

    // Source-over of a PREMULTIPLIED fill: what survives of the matrix is
    // `1 - fill.a` of it, and the fill's own colour is added on top.
    const float k = 1.0f - fillPremultiplied[3];

    ColorMatrix m;
    for (int r = 0; r < 3; ++r) {
        const float ky = kYccInverse[r][0];
        const float kb = kYccInverse[r][1] * saturation;
        const float kr = kYccInverse[r][2] * saturation;

        float coef[3];
        for (int c = 0; c < 3; ++c) {
            coef[c] = ky * gain * kYcc[0][c] + kb * kYcc[1][c] + kr * kYcc[2][c];
        }
        const float bias = ky * (gain * kYccBias[0] + black) +
                           (kYccInverse[r][1] + kYccInverse[r][2]) * chromaBias +
                           kYccInverseBias[r];

        m.row[r].r = coef[0] * k;
        m.row[r].g = coef[1] * k;
        m.row[r].b = coef[2] * k;
        m.row[r].bias = bias * k + fillPremultiplied[r];
    }
    return m;
}

// -------------------------------------------------- the chromatic dispersion

AberrationTap aberrationTap(int index) {
    AberrationTap t;
    if (index < 0 || index >= kAberrationTaps) return t;
    if (index < 3) {
        // `[BIN]` mod98 %620/%656: the loop variable starts at 1 and each
        // iteration ADDS -1/3. Reproduced as the same running sum -- see the
        // header on why `1 - i/3` is a different number at the third tap.
        float w = 1.0f;
        for (int i = 0; i < index; ++i) w = w + kAberrationStepDown;
        t.offset = w;                 // %624: uv + step * w
        t.weight[0] = w;              // %642: red takes w
        t.weight[1] = 1.0f - w;       // %647/%650: green takes 1 - w
        t.weight[2] = 0.0f;
        return t;
    }
    // `[BIN]` mod98 %661/%697 and %617: the second loop runs FOUR times, from 0
    // up by 1/3, and the offset is the NEGATED step.
    float w = 0.0f;
    for (int i = 0; i < index - 3; ++i) w = w + kAberrationStepUp;
    t.offset = -w;                    // %665: uv - step * w
    t.weight[0] = 0.0f;
    t.weight[1] = 1.0f - w;           // %681/%684
    t.weight[2] = w;                  // %691
    return t;
}

void aberrationCombine(const float acc[3], float alphaSum, float out[4]) {
    const float alpha = alphaSum * kAberrationAlphaScale;
    for (int i = 0; i < 3; ++i) out[i] = (acc[i] * kAberrationRgbScale[i]) * alpha;
    out[3] = alpha;
}

}  // namespace rb::glass
