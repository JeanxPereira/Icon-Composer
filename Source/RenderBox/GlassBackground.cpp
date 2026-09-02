#include "Source/RenderBox/GlassBackground.h"

#include <algorithm>
#include <cmath>

#include "Source/RenderBox/GlassOracle.h"

namespace rb::glassbg {
namespace {

// The same shape as `GlassOracle`'s, and deliberately the same file-local
// helper rather than a shared one: `clamp(x, 0, 1)` in GLSL is
// `min(max(x, 0), 1)`, and this reproduces that on signed zeros too.
float saturate(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// `[INF]` The summation order of `air.dot.v3f16` is not in the IR; it is fixed
// here and spelled out identically in `GlassBackground.glsl`, for the reason
// `GlassOracle` gives.
float dot3(const float a[3], const float b[3]) {
    return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2];
}

}  // namespace

// ============================================================== block 1

float decodeDistance(float raw, float scale, float bias) { return raw * scale + bias; }

void decodeGradient(const float raw[2], float scale, float bias, float out[2]) {
    out[0] = raw[0] * scale + bias;
    out[1] = raw[1] * scale + bias;
}

float coverage(float d, float fwidthD) {
    // `[BIN]` mod98 %92: the guarded divisor uses mod98's epsilon, and the
    // negate is applied to `d` BEFORE the divide.
    const float w = std::fmax(fwidthD, glass::kEpsilon);
    const float q = (-d) / w;
    return saturate(q + 0.5f);
}

float mask(float sdfAlpha, float coverageValue) { return sdfAlpha * coverageValue; }

void shadowSamplePoint(const float p[2], float offsetX, float offsetY, float out[2]) {
    out[0] = p[0] - offsetX;
    out[1] = p[1] - offsetY;
}

void ringShadowSamplePoint(const float p[2], float offsetY, float out[2]) {
    out[0] = p[0] - 0.0f;
    out[1] = p[1] - offsetY;
}

// ============================================================== block 3

float keyFillHighlight(float d, float fwidthMax, float sdfAlpha, float maskValue,
                       const float grad[2], float height, float effectOffset,
                       float placement, float edge, float projX, float projY,
                       float spread) {
    if (!(height > 0.0f)) return 0.0f;                 // %177
    const float s = effectOffset + d;                  // %181
    const float ns = -s;                               // %182
    const float halfW = fwidthMax * 0.5f;              // %183
    const bool outside = placement > 0.0f;             // %186
    const bool maskFull = !(maskValue < 1.0f);         // %187, spelled `uge`
    const float sum = halfW + height;                  // %188
    const bool tooFar = !(sum > ns);                   // %189, spelled `ule`
    const bool skipInner = outside ? maskFull : tooFar;  // %190
    if (!(s < halfW)) return 0.0f;                     // %191
    if (skipInner) return 0.0f;                        // %192
    if (sdfAlpha == 0.0f) return 0.0f;                 // %193..%195

    const float inv = 1.0f / fwidthMax;                // %197
    float soft;
    if (outside) {
        soft = 1.0f;                                   // %196 -> the phi's 0xH3C00
    } else {
        const float t = saturate(ns / height);         // %199, %200
        const float t1 = 1.0f - t;                     // %201
        // `[BIN]` %203..%205 -- a HARD zero-or-one, made by converting the
        // boolean to an i32 and that to a half. The mix at %206 pulls the soft
        // value three quarters of the way toward it.
        const float nz = (t1 != 0.0f) ? 1.0f : 0.0f;
        soft = nz + (t1 - nz) * 0.75f;                 // %206, 0xH3A00
    }
    float band;
    if (outside) {
        band = 1.0f - maskValue;                       // %210
    } else {
        band = saturate((height + s) * inv + 0.5f);    // %212..%214
    }
    const float a = sdfAlpha * soft;                   // %217
    const float b2 = saturate(ns * inv + 0.5f);        // %218, %219
    const float c = a * b2;                            // %220
    const float amt = band * c;                        // %221

    const float invEdge = 1.0f / std::fmax(1.0f - edge, glass::kEpsilon);  // %224..%226
    const float pr = projX * grad[0] + (-projY) * grad[1];                 // %235
    const float e0 = saturate(invEdge * (pr - edge));                      // %243..%247
    const float e1 = saturate(invEdge * ((-pr) - edge));
    const float v0 = amt * e0;                                             // %248
    const float v1 = amt * e1;
    const float d0 = std::fmax(spread * (1.0f - v0) + 1.0f, glass::kEpsilon);  // %254..%256
    const float d1 = std::fmax(spread * (1.0f - v1) + 1.0f, glass::kEpsilon);
    return v0 / d0 + v1 / d1;                                              // %257, %260
}

// ============================================================== block 4

float ringShadow(float d2, float sdfAlpha2, float maskValue, float opacity,
                 float ringMask, float blurRadius, float strokeWidth) {
    if (!(opacity > 0.0f)) return 0.0f;                             // %266
    if (!(maskValue > 0.0f || ringMask < 1.0f)) return 0.0f;        // %268..%272
    const float inv = 1.0f / std::fmax(blurRadius, glass::kEpsilon);  // %277, %278
    const float a = d2 * inv;                                       // %279
    const float b = strokeWidth * inv + a;                          // %283
    const float c0 = glass::edgeCoverageInSigma(a);                 // %284..%292
    const float c1 = glass::edgeCoverageInSigma(b);                 // %293..%301
    const float band = saturate(c0 - c1);                           // %302, %303
    const float m = 1.0f + (maskValue - 1.0f) * ringMask;           // %304
    const float s = opacity * m;                                    // %305
    return sdfAlpha2 * (band * s);                                  // %306, %308
}

// ============================================================== block 5

float shadowHeight(float d, float amount, float invHeight, float distanceOffset) {
    return glass::bandAmount(d, amount, invHeight, distanceOffset);
}

float shadowCoverage(float d1, float sdfAlpha1, float shadowRadius, float shadowOpacity) {
    const float x = shadowRadius * d1;                  // %340
    const float cov = glass::edgeCoverage(x);           // %342..%349
    const float a = sdfAlpha1 * shadowOpacity;          // %353
    return cov * a;                                     // %354
}

void shadowColor(const float backdrop[3], bool sampled, float vibrancy,
                 const float matrix[3][4], float matrixAlpha, float out[4]) {
    const float bias[3] = {matrix[0][3], matrix[1][3], matrix[2][3]};
    if (!sampled) {                                     // %452..%464
        out[0] = bias[0];
        out[1] = bias[1];
        out[2] = bias[2];
        out[3] = matrixAlpha;
        return;
    }
    for (int i = 0; i < 3; ++i) {
        const float row[3] = {matrix[i][0], matrix[i][1], matrix[i][2]};
        const float f = dot3(backdrop, row);            // %403, %416, %429
        out[i] = vibrancy * f + bias[i];                // %434 then %444
    }
    out[3] = matrixAlpha + (1.0f - matrixAlpha) * vibrancy;  // %449
}

void shadowPremultiplied(const float color[4], float coverageValue, float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = coverageValue * color[i];  // %467..%469
}

// ============================================================== block 6

float innerRefractionHeight(float d, float amount, float invHeight) {
    return glass::bandAmount(d, amount, invHeight, 0.0f);
}

float blurRadiusRamp(float x, const float distances[4], const float opacities[3],
                     float opacity0, float scale) {
    float prod[3];
    for (int i = 0; i < 3; ++i) {
        const float lo = distances[i];                  // %505
        const float hi = distances[i + 1];              // %511
        const float span = hi - lo;                     // %515
        const float inv = 1.0f / span;                  // %516
        const float base = (-lo) / span;                // %517, %518
        const float t = saturate(x * inv + base);       // %519, %520
        prod[i] = t * opacities[i];                     // %532
    }
    // `[BIN]` %538 then %540 -- `prod.z + (prod.x + prod.y)`, not left to right.
    const float s01 = prod[0] + prod[1];
    const float sum = prod[2] + s01;
    const float v = opacity0 - sum;                     // %541
    return scale * v;                                   // %545
}

float flatBlurRadius(float opacity0, float scale) { return opacity0 * scale; }  // %846

void refractPoint(const float p[2], const float grad[2], float height, float out[2]) {
    out[0] = grad[0] * height + p[0];
    out[1] = grad[1] * height + p[1];
}

// ============================================================== block 7

float aberrationHeight(float d, float amount, float invHeight, float offset) {
    return glass::bandAmount(d, amount, invHeight, offset);
}

void aberrationStep(const float grad[2], float height, float cosA, float sinA,
                    float out[2]) {
    const float pr0 = grad[0] * cosA + grad[1] * (-sinA);   // %583
    const float pr1 = grad[0] * sinA + grad[1] * cosA;      // %587
    out[0] = pr1 * height;                                  // %604, %607
    out[1] = pr0 * height;
}

// ============================================================== block 8

void blurFill(const float face[3], const float backdrop[3], float lighten, float darken,
              float normal, float out[3]) {
    const float w = (1.0f - darken) - lighten;              // %757, %758
    for (int i = 0; i < 3; ++i) {
        const float mn = std::fmin(face[i], backdrop[i]);   // %749
        const float mx = std::fmax(face[i], backdrop[i]);   // %750
        const float hi = lighten * mx;                      // %755
        const float acc = mn * darken + hi;                 // %756
        const float sum = face[i] * w + acc;                // %761
        out[i] = sum + (backdrop[i] - sum) * normal;        // %764
    }
}

// ============================================================== block 9

float outerRefractionHeight(float d, float amount, float invHeight) {
    return glass::bandAmount(d, amount, invHeight, 0.0f);
}

float outerRefractionWeight(float d, float distance0, float distance1, float opacity) {
    const float span = distance1 - distance0;               // %827
    const float inv = 1.0f / span;                          // %829
    const float base = (-distance0) / span;                 // %830, %831
    const float t = saturate(d * inv + base);               // %833, %834
    return opacity * t;                                     // %835
}

void outerRefractionMix(const float face[4], const float outer[4], float weight,
                        float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = face[i] + (outer[i] - face[i]) * weight;  // %838
}

// ============================================================== block 10

void unpremultiply(const float rgba[4], float out[4]) {
    const float a = std::fmax(rgba[3], glass::kEpsilon);    // %862
    for (int i = 0; i < 3; ++i) out[i] = rgba[i] / a;       // %866
    out[3] = 1.0f;                                          // %868
}

void faceColor(const float unpre[3], float faceOpacity, float maxLuma,
               const float matrix[3][4], float out[3]) {
    if (!(faceOpacity > 0.0f)) {                            // %871
        for (int i = 0; i < 3; ++i) out[i] = unpre[i];
        return;
    }
    float c[3] = {unpre[0], unpre[1], unpre[2]};
    if (maxLuma > 0.0f) glass::compressMaxLuma(unpre, maxLuma, c);  // %876..%892

    glass::ColorMatrix m;
    for (int i = 0; i < 3; ++i) {
        m.row[i] = {matrix[i][0], matrix[i][1], matrix[i][2], matrix[i][3]};
    }
    float raw[3];
    glass::faceColorMatrixRaw(c, m, raw);                   // %906..%943
    // `[BIN]` %946 interpolates from `%866`, the UNCOMPRESSED colour.
    for (int i = 0; i < 3; ++i) out[i] = unpre[i] + (raw[i] - unpre[i]) * faceOpacity;
}

// ============================================================== block 11

float specularWeight(float d, const float faceRgb[3], float distance0, float distance1,
                     float opacity, float weightScale, float weightBias) {
    const float span = distance1 - distance0;               // %1053
    const float inv = 1.0f / span;                          // %1054
    const float base = (-distance0) / span;                 // %1055, %1056
    const float t = saturate(d * inv + base);               // %1057, %1058
    const float t2 = t * t;                                 // %1059
    const float w0 = opacity * t2;                          // %1060
    // `[BIN]` %1062 -- `kLumaWeight`, the SECOND Rec. 709 rounding.
    const float y = dot3(faceRgb, glass::kLumaWeight);
    const float ys = saturate(y);                           // %1063
    const float q = weightScale * ys + weightBias;          // %1069
    const float q2 = q * q;                                 // %1070
    const float q4 = q2 * q2;                               // %1071
    return w0 * q4;                                         // %1072
}

float specularHeight(float d, float amount, float invHeight) {
    return glass::bandAmount(d, amount, invHeight, 0.0f);  // %963..%970
}

void specular(const float face[4], const float backdrop[3], float weight,
              const float matrix[3][4], float out[4]) {
    glass::ColorMatrix m;
    for (int i = 0; i < 3; ++i) {
        m.row[i] = {matrix[i][0], matrix[i][1], matrix[i][2], matrix[i][3]};
    }
    float raw[3];
    glass::faceColorMatrixRaw(backdrop, m, raw);
    for (int i = 0; i < 3; ++i) out[i] = face[i] + (raw[i] - face[i]) * weight;  // %1075
    out[3] = face[3];                                                            // %1077
}

// ============================================================== block 12

bool isZero(float maskValue, float shadowCov, float highlight, float ringShadow) {
    return maskValue == 0.0f && shadowCov < glass::kEpsilon &&
           highlight < glass::kEpsilon && ringShadow < glass::kEpsilon;
}

void composite(const float shadowPre[4], const float facePre[4], float maskValue,
               float out[4]) {
    for (int i = 0; i < 4; ++i) {
        out[i] = shadowPre[i] + (facePre[i] - shadowPre[i]) * maskValue;  // %1082
    }
}

void applyRingShadow(const float col[4], float ringShadow, float out[4]) {
    // `[BIN]` %1084's constant vector is `<0, 0, 0, ringShadow>`, so the colour
    // is only scaled and the ring shadow reaches ALPHA alone.
    const float k = 1.0f - ringShadow;                      // %1085
    for (int i = 0; i < 3; ++i) out[i] = col[i] * k + 0.0f;
    out[3] = col[3] * k + ringShadow;                       // %1088
}

void highlightFillColor(const float backdrop[3], float highlight, float faceOpacity,
                        float maxLuma, const float matrix[3][4], float clampMode,
                        float colorBias, float out[3]) {
    if (!(faceOpacity > 0.0f)) {                            // %1119
        for (int i = 0; i < 3; ++i) out[i] = backdrop[i];
        return;
    }
    float c[3] = {backdrop[0], backdrop[1], backdrop[2]};
    if (maxLuma > 0.0f) glass::compressMaxLuma(backdrop, maxLuma, c);

    glass::ColorMatrix m;
    for (int i = 0; i < 3; ++i) {
        m.row[i] = {matrix[i][0], matrix[i][1], matrix[i][2], matrix[i][3]};
    }
    float raw[3];
    glass::faceColorMatrixRaw(c, m, raw);
    if (clampMode > 0.0f) {                                 // %1194
        for (int i = 0; i < 3; ++i) {
            raw[i] = (colorBias < 0.0f) ? std::fmin(raw[i], backdrop[i])   // %1201
                                        : std::fmax(raw[i], backdrop[i]);  // %1203
        }
    }
    const float w = highlight * faceOpacity;                // %1206
    for (int i = 0; i < 3; ++i) out[i] = backdrop[i] + (raw[i] - backdrop[i]) * w;  // %1209
}

void applyKeyFillHighlight(const float col[4], float highlight, const float backdrop[3],
                           float faceOpacity, float maxLuma, const float matrix[3][4],
                           float clampMode, float colorBias, float alphaGain,
                           float out[4]) {
    if (!(highlight > glass::kEpsilon)) {                   // %1089
        for (int i = 0; i < 4; ++i) out[i] = col[i];
        return;
    }
    float rgb[3] = {col[0], col[1], col[2]};                // %1091
    const float a = col[3];                                 // %1092
    float t;
    float src[3];
    if (!(a < 1.0f)) {                                      // %1093 -> %1094
        t = 1.0f - a;                                       // %1095
        src[0] = src[1] = src[2] = 0.0f;                    // %1218's zeroinitializer
    } else {
        float fill[3];
        highlightFillColor(backdrop, highlight, faceOpacity, maxLuma, matrix, clampMode,
                           colorBias, fill);
        t = 1.0f - a;                                       // %1212
        for (int i = 0; i < 3; ++i) rgb[i] = fill[i] * t + rgb[i];  // %1215
        for (int i = 0; i < 3; ++i) src[i] = backdrop[i];   // %1116 -> %1218
    }
    const float k = highlight * colorBias;                  // %1223
    const float gain = highlight * alphaGain;               // %1232
    const float aOut = gain * t + a;                        // %1233
    const float oneMinus = 1.0f - aOut;                     // %1235
    for (int i = 0; i < 3; ++i) {
        const float f = -2.0f * rgb[i] + 3.0f;              // %1226
        const float g = k * f + 1.0f;                       // %1227
        const float h = rgb[i] * g;                         // %1228
        out[i] = (-src[i]) * oneMinus + h;                  // %1238, %1239
    }
    out[3] = aOut;
}

void fade(const float col[4], float d, float headroom, float gradientDistance0,
          float gradientInvSpan, float toneWhite, float out[4]) {
    if (!(headroom > 0.0f)) {                               // %1246
        for (int i = 0; i < 4; ++i) out[i] = col[i];
        return;
    }
    const float x = d - gradientDistance0;                  // %1251
    const float t = saturate(x * gradientInvSpan);          // %1254, %1255
    const float s = 1.0f - t;                               // %1256
    const float a = col[3];
    const float ai = std::fmax(a, glass::kEpsilon);         // %1260
    const float sa = saturate(a);                           // %1268
    const float w = headroom * s;                           // %1274
    float tgt[4];
    for (int i = 0; i < 3; ++i) {
        const float c = col[i] / ai;                        // %1264
        const float c2 = toneWhite * c;                     // %1267
        tgt[i] = sa * c2;                                   // %1271
    }
    tgt[3] = sa;                                            // %1273
    for (int i = 0; i < 4; ++i) out[i] = col[i] + (tgt[i] - col[i]) * w;  // %1277
}

void clampHeadroom(const float col[4], float ceiling, float out[4]) {
    if (!(ceiling > 0.0f)) {                                // %1283
        for (int i = 0; i < 4; ++i) out[i] = col[i];
        return;
    }
    const float a = col[3];
    const float ai = std::fmax(a, glass::kEpsilon);         // %1286
    for (int i = 0; i < 3; ++i) {
        const float c = col[i] / ai;                        // %1290
        const float k = std::fmin(std::fmax(c, -0.75f), ceiling);  // %1293
        // `[BIN]` %1294/%1295 -- the RAW alpha, not the floored one.
        out[i] = a * k;
    }
    out[3] = a;
}

// ============================================== the assembly

// `[BIN]` mod98 `%49`..`%1298`. Every branch below names the IR value that
// decides it, and the order is the IR's.
void evaluate(const Inputs& in, const Uniforms& u, const Field& field,
              const Backdrop& backdrop, float out[4]) {
    // ---- block 1: the field, the distance, the coverage -----------------
    float s0[4];
    field.sample(in.p, s0);                                             // %78
    const float d = decodeDistance(s0[0], u.fieldScale, u.fieldBias);   // %85
    const float cov = coverage(d, in.fwidthD);                          // %91..%98
    const float maskValue = mask(s0[3], cov);                           // %100
    const bool maskBelowOne = maskValue < 1.0f;                         // %101

    float grad[2];
    const float rawGrad[2] = {s0[1], s0[2]};
    decodeGradient(rawGrad, u.gradientScale, u.gradientBias, grad);     // %164
    const float fwidthMax = std::fmax(in.fwidthD, glass::kEpsilon);     // %172

    // ---- block 2: the two offset re-samples -----------------------------
    // `[BIN]` %101 -- the shadow's re-sample happens only where the mask is not
    // already saturated; elsewhere `%126` is a `zeroinitializer`, which makes
    // both `d1` and `a1` zero rather than "unsampled".
    float d1 = 0.0f, a1 = 0.0f;
    if (maskBelowOne) {                                                 // %102
        float pt[2];
        shadowSamplePoint(in.p, u.shadowOffsetX, u.shadowOffsetY, pt);
        float s1[4];
        field.sample(pt, s1);                                           // %115
        d1 = decodeDistance(s1[0], u.fieldScale, u.fieldBias);          // %123
        a1 = s1[3];
    }
    float d2 = 0.0f, a2 = 0.0f;
    if ((in.variant & kVariantRing) != 0 && u.ringShadowOpacity > 0.0f) {  // %129, %134
        float pt[2];
        ringShadowSamplePoint(in.p, u.ringShadowOffsetY, pt);
        float s2[4];
        field.sample(pt, s2);                                           // %146
        d2 = decodeDistance(s2[0], u.fieldScale, u.fieldBias);          // %154
        a2 = s2[3];
    }

    // ---- blocks 3 and 4: the highlight and the ring shadow ---------------
    float highlight = 0.0f;
    float ringShadowValue = 0.0f;
    if ((in.variant & kVariantRing) != 0) {                             // %165
        highlight = keyFillHighlight(d, fwidthMax, s0[3], maskValue, grad,
                                     u.highlightHeight, u.highlightEffectOffset,
                                     u.highlightPlacement, u.highlightEdge,
                                     u.highlightProjX, u.highlightProjY,
                                     u.highlightSpread);                // %173..%260
        ringShadowValue = ringShadow(d2, a2, maskValue, u.ringShadowOpacity,
                                     u.ringShadowMask, u.ringShadowBlurRadius,
                                     u.ringShadowStrokeWidth);          // %261..%308
    }

    // ---- block 5: the shadow lobe ---------------------------------------
    float shadowPre[4] = {0.0f, 0.0f, 0.0f, 0.0f};                      // %473's phi
    if (maskBelowOne) {                                                 // %101 again
        const float scov = shadowCoverage(d1, a1, u.shadowRadius, u.shadowOpacity);
        if (isZero(maskValue, scov, highlight, ringShadowValue)) {      // %361
            out[0] = out[1] = out[2] = out[3] = 0.0f;                   // %470 -> %1298
            return;
        }
        const float h = shadowHeight(d, u.shadowAmount, u.shadowInvHeight,
                                     u.shadowDistanceOffset);
        float unpre[3] = {0.0f, 0.0f, 0.0f};
        // `[BIN]` %366's immediate `0x3F50640000000000` is the half `0xH1419`
        // widened to double by the printer -- the same epsilon as everywhere
        // else, not a second constant.
        const bool sampled = (in.variant & kVariantBackdrop) != 0 &&
                             u.shadowVibrancy > glass::kEpsilon;
        if (sampled) {
            float pt[2];
            refractPoint(in.p, grad, h, pt);                            // %335
            float b[4];
            backdrop.sample(pt, u.shadowBlurRadius, b);                 // %384
            const float ba = std::fmax(b[3], glass::kEpsilon);          // %387
            for (int i = 0; i < 3; ++i) unpre[i] = b[i] / ba;           // %391
        }
        float col[4];
        shadowColor(unpre, sampled, u.shadowVibrancy, u.shadowMatrix,
                    u.shadowMatrixAlpha, col);
        shadowPremultiplied(col, scov, shadowPre);                      // %469
    }

    // ---- blocks 6..11: the face -----------------------------------------
    float facePre[4] = {0.0f, 0.0f, 0.0f, 0.0f};                        // %1079's phi
    if (maskValue > 0.0f) {                                             // %474
        float face[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if ((in.variant & kVariantInnerRefraction) != 0) {              // %167, inverted
            const float inner = innerRefractionHeight(d, u.innerRefractionAmount,
                                                      u.innerRefractionInvHeight);
            const float x = inner + d;                                  // %491
            const float distances[4] = {u.blurDistance0, u.blurDistance1,
                                        u.blurDistance2, u.blurDistance3};
            const float opacities[3] = {u.blurOpacity1, u.blurOpacity2, u.blurOpacity3};
            const float radius =
                blurRadiusRamp(x, distances, opacities, u.blurOpacity0, u.blurRadius);
            float rp[2];
            refractPoint(in.p, grad, inner, rp);                        // %550

            // `[BIN]` %555 is `air.convert.u.i1.f.f16` -- a NON-ZERO test, not a
            // `> 0` one. A negative aberration amount still disperses.
            if ((in.variant & kVariantBackdrop) != 0 && u.aberrationAmount != 0.0f) {
                const float ah = aberrationHeight(d, u.aberrationAmount,
                                                  u.aberrationInvHeight,
                                                  u.aberrationOffset);
                float step[2];
                aberrationStep(grad, ah, u.aberrationCos, u.aberrationSin, step);
                // The taps and the combine are `GlassOracle`'s -- shared with
                // mod99 and gated there. This loop is the caller's contract,
                // spelled out in that header, and nothing of it is re-derived.
                float acc[3] = {0.0f, 0.0f, 0.0f};
                float alphaSum = 0.0f;
                for (int t = 0; t < glass::kAberrationTaps; ++t) {
                    const glass::AberrationTap tap = glass::aberrationTap(t);
                    const float uvp[2] = {rp[0] + step[0] * tap.offset,
                                          rp[1] + step[1] * tap.offset};
                    float sm[4];
                    backdrop.sample(uvp, radius, sm);
                    const float a = std::fmax(sm[3], glass::kEpsilon);  // %633
                    for (int k = 0; k < 3; ++k) acc[k] += (sm[k] / a) * tap.weight[k];
                    alphaSum += sm[3];                                  // the RAW alpha
                }
                glass::aberrationCombine(acc, alphaSum, face);          // %701..%710
            } else {
                backdrop.sample(rp, radius, face);                      // %570
            }

            // `[BIN]` %713..%726 -- three uniforms ORed, under bit 0.
            if ((in.variant & kVariantRing) != 0 &&
                (u.blurFillLighten > 0.0f || u.blurFillDarken > 0.0f ||
                 u.blurFillNormal > 0.0f)) {
                float b[4];
                backdrop.sample(in.p, u.blurFillBlurRadius, b);         // %740
                const float ba = std::fmax(b[3], glass::kEpsilon);      // %743
                const float bu[3] = {b[0] / ba, b[1] / ba, b[2] / ba};
                const float rgb[3] = {face[0], face[1], face[2]};
                float mixed[3];
                blurFill(rgb, bu, u.blurFillLighten, u.blurFillDarken,
                         u.blurFillNormal, mixed);
                for (int i = 0; i < 3; ++i) face[i] = mixed[i];         // %766
            }

            if (u.refractionOpacity > 0.0f) {                           // %772
                const float oh = outerRefractionHeight(d, u.outerRefractionAmount,
                                                       u.outerRefractionInvHeight);
                const float ox = oh + d;                                // %788
                const float orad = blurRadiusRamp(ox, distances, opacities,
                                                  u.blurOpacity0, u.blurRadius);
                float op[2];
                refractPoint(in.p, grad, oh, op);                       // %807
                // `[BIN]` %821 is NOT un-premultiplied -- unlike every other
                // fetch in the function, this one is mixed in as it comes.
                float outer[4];
                backdrop.sample(op, orad, outer);
                const float w = outerRefractionWeight(d, u.refractionDistance0,
                                                     u.refractionDistance1,
                                                     u.refractionOpacity);
                float mixed[4];
                outerRefractionMix(face, outer, w, mixed);              // %838
                for (int i = 0; i < 4; ++i) face[i] = mixed[i];
            }
        } else {
            // `[BIN]` %839..%858 -- no ramp, no refraction, one fetch at `p`.
            const float radius = flatBlurRadius(u.blurOpacity0, u.blurRadius);
            backdrop.sample(in.p, radius, face);
        }

        float unpre[4];
        unpremultiply(face, unpre);                                     // %862..%868
        float fc[3];
        faceColor(unpre, u.faceOpacity, u.faceMatrixMaxLuma, u.faceMatrix, fc);
        facePre[0] = fc[0];
        facePre[1] = fc[1];
        facePre[2] = fc[2];
        facePre[3] = 1.0f;                                              // %868 / %948

        if ((in.variant & kVariantBackdrop) != 0 && u.bleedOpacity > 0.0f) {  // %955
            const float sh = specularHeight(d, u.bleedAmount, u.bleedInvHeight);
            float sp[2];
            refractPoint(in.p, grad, sh, sp);                           // %974
            float b[4];
            backdrop.sample(sp, u.bleedBlurRadius, b);                  // %991
            const float ba = std::fmax(b[3], glass::kEpsilon);          // %994
            const float bu[3] = {b[0] / ba, b[1] / ba, b[2] / ba};      // %998
            const float rgb[3] = {facePre[0], facePre[1], facePre[2]};
            const float w = specularWeight(d, rgb, u.bleedDistance0, u.bleedDistance1,
                                           u.bleedOpacity, u.bleedDarkenScale,
                                           u.bleedDarkenBias);
            float lit[4];
            specular(facePre, bu, w, u.specularMatrix, lit);            // %1075..%1077
            for (int i = 0; i < 4; ++i) facePre[i] = lit[i];
        }
    }

    // ---- block 12: composite, ring shadow, highlight, epilogue -----------
    float col[4];
    composite(shadowPre, facePre, maskValue, col);                      // %1082

    if ((in.variant & kVariantRing) != 0) {                             // %165 again
        float withRing[4];
        applyRingShadow(col, ringShadowValue, withRing);                // %1088
        for (int i = 0; i < 4; ++i) col[i] = withRing[i];
        if (highlight > glass::kEpsilon) {                              // %1089
            // `[BIN]` %1097 is `fmax(0xH1419, 0)` -- the radius is a LITERAL
            // ZERO here, so this fetch is always at the pyramid's base level.
            float b[4];
            backdrop.sample(in.p, 0.0f, b);                             // %1107
            const float ba = std::fmax(b[3], glass::kEpsilon);          // %1110
            const float bu[3] = {b[0] / ba, b[1] / ba, b[2] / ba};      // %1114
            float lit[4];
            applyKeyFillHighlight(col, highlight, bu, u.faceOpacity, u.faceMatrixMaxLuma,
                                  u.faceMatrix, u.faceMatrixClampMode,
                                  u.highlightColorBias, u.highlightAlphaGain, lit);
            for (int i = 0; i < 4; ++i) col[i] = lit[i];
        }
    }

    float faded[4];
    fade(col, d, u.maxHeadroom, u.sdrGradientDistance0, u.sdrGradientInvSpan,
         u.sdrHoldingToneWhite, faded);                                 // %1277
    clampHeadroom(faded, u.clampCeiling, out);                          // %1297
}

}  // namespace rb::glassbg
