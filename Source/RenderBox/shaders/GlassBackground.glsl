// `glassBackground_v1`, block by block, in GLSL.
//
// `[BIN]` `References/2.0-125/out/metallib-renderbox/default_mod98.ll`. This is
// the twin of `Source/RenderBox/GlassBackground.h`, and the reading -- what each
// block is, which IR values it came from, where mod98 disagrees with the spec's
// block list, and how the uniform names were bound -- lives THERE, once. This
// side carries only what is about being GLSL.
//
// This is the file the renderer includes AND the file the differential gates, so
// what draws and what is verified cannot drift apart.
//
// WHY `precise` IS ON EVERY INTERMEDIATE AND THERE IS NO `fma()`. The same
// measurement `Glass.glsl` records: glslc turns `precise` into SPIR-V's
// `NoContraction` on `OpFMul` and `OpFAdd` but NOT on `OpExtInst ... Fma`, and
// an undecorated `Fma` may legally be decomposed -- which this driver does. So
// every multiply-add is written unfused on both sides.
//
// WHAT THIS FILE DOES NOT DO, AND MUST NOT START DOING:
//   * it does not sample. Every fetched colour arrives as a parameter.
//   * it does not compute a LOD. `rbBackdropLod` is transcribed in
//     `mip_reduce.comp` with its half floor and half log, and it is gated there.
//     What lives here is the RADIUS a block asks for; turning a radius into a
//     level is the pyramid's job and a second copy would be a second thing to
//     drift.
//   * it does not take a derivative. `fwidth` is the caller's.
#ifndef RB_GLASSBACKGROUND_GLSL
#define RB_GLASSBACKGROUND_GLSL

#include "Glass.glsl"

// =========================================================================
// block 1 -- coverage
// =========================================================================

// `[BIN]` mod98 %81..%86.
float rbBgDecodeDistance(float raw, float scale, float bias) {
    precise float d = raw * scale + bias;
    return d;
}

// `[BIN]` mod98 %158..%164 -- the scale and the bias are SPLATTED over both
// lanes; there is no per-axis pair.
vec2 rbBgDecodeGradient(vec2 raw, float scale, float bias) {
    precise vec2 g = raw * vec2(scale) + vec2(bias);
    return g;
}

// `[BIN]` mod98 %91..%98. The negate comes BEFORE the divide (`%93` then `%94`)
// and the half is added after (`%96`), which is not the same rounding as
// `0.5 - d/w`.
float rbBgCoverage(float d, float fwidthD) {
    precise float w = max(fwidthD, kRbGlassEpsilon);
    precise float q = (-d) / w;
    precise float s = q + 0.5;
    return clamp(s, 0.0, 1.0);
}

// `[BIN]` mod98 %100.
float rbBgMask(float sdfAlpha, float coverage) {
    precise float m = sdfAlpha * coverage;
    return m;
}

// `[BIN]` mod98 %109.
vec2 rbBgShadowSamplePoint(vec2 p, vec2 offset) {
    precise vec2 q = p - offset;
    return q;
}

// `[BIN]` mod98 %139/%140 -- X is a literal zero, not a uniform.
vec2 rbBgRingShadowSamplePoint(vec2 p, float offsetY) {
    precise vec2 q = p - vec2(0.0, offsetY);
    return q;
}

// =========================================================================
// block 3 -- the key-fill highlight  (shaderVariant & 1)
// =========================================================================

// `[BIN]` mod98 %173..%260.
float rbBgKeyFillHighlight(float d, float fwidthMax, float sdfAlpha, float maskValue,
                           vec2 grad, float height, float effectOffset, float placement,
                           float edge, float projX, float projY, float spread) {
    if (!(height > 0.0)) return 0.0;                    // %177
    precise float s = effectOffset + d;                 // %181
    precise float ns = -s;                              // %182
    precise float halfW = fwidthMax * 0.5;              // %183
    bool outside = placement > 0.0;                     // %186
    bool maskFull = !(maskValue < 1.0);                 // %187
    precise float sum = halfW + height;                 // %188
    bool tooFar = !(sum > ns);                          // %189, spelled `ule`
    bool skipInner = outside ? maskFull : tooFar;       // %190
    if (!(s < halfW)) return 0.0;                       // %191, spelled `uge`
    if (skipInner) return 0.0;                          // %192
    if (sdfAlpha == 0.0) return 0.0;                    // %193..%195

    precise float inv = 1.0 / fwidthMax;                // %197
    precise float soft;
    if (outside) {
        soft = 1.0;                                     // %196 -> the phi's 0xH3C00
    } else {
        precise float t = clamp(ns / height, 0.0, 1.0); // %199, %200
        precise float t1 = 1.0 - t;                     // %201
        // %203..%205: a HARD zero-or-one indicator, produced by converting the
        // boolean to an integer and that integer to a float.
        precise float nz = (t1 != 0.0) ? 1.0 : 0.0;
        soft = nz + (t1 - nz) * 0.75;                   // %206, `air.mix`, 0xH3A00
    }
    precise float band;
    if (outside) {
        band = 1.0 - maskValue;                         // %210
    } else {
        precise float b = (height + s) * inv + 0.5;     // %212, %213
        band = clamp(b, 0.0, 1.0);                      // %214
    }
    precise float a = sdfAlpha * soft;                  // %217
    precise float b2 = clamp(ns * inv + 0.5, 0.0, 1.0); // %218, %219
    precise float c = a * b2;                           // %220
    precise float amt = band * c;                       // %221

    precise float invEdge = 1.0 / max(1.0 - edge, kRbGlassEpsilon);   // %224..%226
    precise float pr = projX * grad.x + (-projY) * grad.y;            // %235
    // %240 is `<pr, -pr>`, %243 subtracts the edge from both lanes, %246 scales
    // with the reciprocal on the LEFT.
    precise float e0 = clamp(invEdge * (pr - edge), 0.0, 1.0);
    precise float e1 = clamp(invEdge * ((-pr) - edge), 0.0, 1.0);
    precise float v0 = amt * e0;                                      // %248
    precise float v1 = amt * e1;
    precise float d0 = max(spread * (1.0 - v0) + 1.0, kRbGlassEpsilon);  // %254..%256
    precise float d1 = max(spread * (1.0 - v1) + 1.0, kRbGlassEpsilon);
    precise float r0 = v0 / d0;                                       // %257
    precise float r1 = v1 / d1;
    precise float r = r0 + r1;                                        // %260
    return r;
}

// =========================================================================
// block 4 -- the ring shadow
// =========================================================================

// `[BIN]` mod98 %261..%308. Both edge evaluations carry the 1/sqrt(2) pre-scale.
float rbBgRingShadow(float d2, float sdfAlpha2, float maskValue, float opacity,
                     float ringMask, float blurRadius, float strokeWidth) {
    if (!(opacity > 0.0)) return 0.0;                           // %266
    if (!(maskValue > 0.0 || ringMask < 1.0)) return 0.0;       // %268..%272
    precise float inv = 1.0 / max(blurRadius, kRbGlassEpsilon); // %277, %278
    precise float a = d2 * inv;                                 // %279
    precise float b = strokeWidth * inv + a;                    // %283
    precise float c0 = rbGlassEdgeCoverageInSigma(a);           // %284..%292
    precise float c1 = rbGlassEdgeCoverageInSigma(b);           // %293..%301
    precise float band = clamp(c0 - c1, 0.0, 1.0);              // %302, %303
    precise float m = 1.0 + (maskValue - 1.0) * ringMask;       // %304, `air.mix`
    precise float s = opacity * m;                              // %305
    precise float t = band * s;                                 // %306
    precise float r = sdfAlpha2 * t;                            // %308
    return r;
}

// =========================================================================
// block 5 -- the shadow lobe
// =========================================================================

// `[BIN]` mod98 %315..%331.
float rbBgShadowHeight(float d, float amount, float invHeight, float distanceOffset) {
    return rbGlassBandAmount(d, amount, invHeight, distanceOffset);
}

// `[BIN]` mod98 %337..%354. NO sigma pre-scale at this call site.
float rbBgShadowCoverage(float d1, float sdfAlpha1, float shadowRadius,
                         float shadowOpacity) {
    precise float x = shadowRadius * d1;               // %340
    precise float cov = rbGlassEdgeCoverage(x);        // %342..%349
    precise float a = sdfAlpha1 * shadowOpacity;       // %353
    precise float r = cov * a;                         // %354
    return r;
}

// `[BIN]` mod98 %394..%450 / %452..%464. The matrix is applied WITHOUT its bias,
// scaled, and only then biased -- which is why this cannot call
// `rbGlassFaceColorMatrixRaw`.
vec4 rbBgShadowColor(vec3 backdrop, bool sampled, float vibrancy,
                     vec4 cm0, vec4 cm1, vec4 cm2, float matrixAlpha) {
    precise vec3 bias = vec3(cm0.w, cm1.w, cm2.w);
    if (!sampled) return vec4(bias, matrixAlpha);      // %452..%464
    precise float f0 = (backdrop.r * cm0.x + backdrop.g * cm0.y) + backdrop.b * cm0.z;
    precise float f1 = (backdrop.r * cm1.x + backdrop.g * cm1.y) + backdrop.b * cm1.z;
    precise float f2 = (backdrop.r * cm2.x + backdrop.g * cm2.y) + backdrop.b * cm2.z;
    precise vec3 scaled = vec3(vibrancy) * vec3(f0, f1, f2);   // %434
    precise vec3 rgb = scaled + bias;                          // %444
    precise float a = matrixAlpha + (1.0 - matrixAlpha) * vibrancy;  // %449
    return vec4(rgb, a);
}

// `[BIN]` mod98 %467..%469.
vec4 rbBgShadowPremultiplied(vec4 color, float coverage) {
    precise vec4 r = vec4(coverage) * color;
    return r;
}

// =========================================================================
// block 6 -- the inner refraction, and the blur radius ramp
// =========================================================================

// `[BIN]` mod98 %483..%490.
float rbBgInnerRefractionHeight(float d, float amount, float invHeight) {
    return rbGlassBandAmount(d, amount, invHeight, 0.0);
}

// `[BIN]` mod98 %492..%546. The sum associates as `p.z + (p.x + p.y)`.
float rbBgBlurRadiusRamp(vec4 distances, vec3 opacities, float opacity0, float scale,
                         float x) {
    precise vec3 lo = vec3(distances.x, distances.y, distances.z);   // %505
    precise vec3 hi = vec3(distances.y, distances.z, distances.w);   // %511
    precise vec3 span = hi - lo;                                     // %515
    precise vec3 inv = vec3(1.0) / span;                             // %516
    precise vec3 base = (-lo) / span;                                // %517, %518
    precise vec3 t = clamp(vec3(x) * inv + base, 0.0, 1.0);          // %519, %520
    precise vec3 prod = t * opacities;                               // %532
    precise float s01 = prod.x + prod.y;                             // %538
    precise float sum = prod.z + s01;                                // %540
    precise float v = opacity0 - sum;                                // %541
    precise float r = scale * v;                                     // %545
    return r;
}

// `[BIN]` mod98 %841..%846 -- operands in the opposite order to %545.
float rbBgFlatBlurRadius(float opacity0, float scale) {
    precise float r = opacity0 * scale;
    return r;
}

// `[BIN]` mod98 %335 / %550 / %807 / %974.
vec2 rbBgRefractPoint(vec2 p, vec2 grad, float height) {
    precise vec2 q = grad * vec2(height) + p;
    return q;
}

// =========================================================================
// block 7 -- the chromatic dispersion
// =========================================================================

// `[BIN]` mod98 %591..%601.
float rbBgAberrationHeight(float d, float amount, float invHeight, float offset) {
    return rbGlassBandAmount(d, amount, invHeight, offset);
}

// `[BIN]` mod98 %573..%607. The two projections are SWAPPED into the result.
vec2 rbBgAberrationStep(vec2 grad, float height, float cosA, float sinA) {
    precise float pr0 = grad.x * cosA + grad.y * (-sinA);   // %583
    precise float pr1 = grad.x * sinA + grad.y * cosA;      // %587
    precise vec2 v = vec2(pr1, pr0) * vec2(height);         // %604, %607
    return v;
}

// =========================================================================
// block 8 -- the blur fill (darken / lighten)
// =========================================================================

// `[BIN]` mod98 %749..%764. DARKEN multiplies the min and LIGHTEN the max --
// see the header for why that is the opposite of the spec's block list.
vec3 rbBgBlurFill(vec3 face, vec3 backdrop, float lighten, float darken, float normal) {
    precise vec3 mn = min(face, backdrop);              // %749
    precise vec3 mx = max(face, backdrop);              // %750
    precise vec3 hi = vec3(lighten) * mx;               // %755
    precise vec3 acc = mn * vec3(darken) + hi;          // %756
    precise float w = (1.0 - darken) - lighten;         // %757, %758
    precise vec3 sum = face * vec3(w) + acc;            // %761
    precise vec3 r = sum + (backdrop - sum) * vec3(normal);  // %764, `air.mix`
    return r;
}

// =========================================================================
// block 9 -- the outer refraction
// =========================================================================

// `[BIN]` mod98 %780..%787.
float rbBgOuterRefractionHeight(float d, float amount, float invHeight) {
    return rbGlassBandAmount(d, amount, invHeight, 0.0);
}

// `[BIN]` mod98 %823..%835.
float rbBgOuterRefractionWeight(float d, float distance0, float distance1,
                                float opacity) {
    precise float span = distance1 - distance0;             // %827
    precise float inv = 1.0 / span;                         // %829
    precise float base = (-distance0) / span;               // %830, %831
    precise float t = clamp(d * inv + base, 0.0, 1.0);      // %833, %834
    precise float w = opacity * t;                          // %835
    return w;
}

// `[BIN]` mod98 %838 -- four channels, alpha included.
vec4 rbBgOuterRefractionMix(vec4 face, vec4 outer, float weight) {
    precise vec4 r = face + (outer - face) * vec4(weight);
    return r;
}

// =========================================================================
// block 10 -- desaturate, then the face colour matrix
// =========================================================================

// `[BIN]` mod98 %861..%868 -- the alpha the result carries is a literal one.
vec4 rbBgUnpremultiply(vec4 c) {
    precise float a = max(c.a, kRbGlassEpsilon);
    precise vec3 rgb = c.rgb / vec3(a);
    return vec4(rgb, 1.0);
}

// `[BIN]` mod98 %869..%948. The mix interpolates from the UNCOMPRESSED colour.
vec3 rbBgFaceColor(vec3 unpre, float faceOpacity, float maxLuma,
                   vec4 cm0, vec4 cm1, vec4 cm2) {
    if (!(faceOpacity > 0.0)) return unpre;                      // %871
    precise vec3 c = unpre;
    if (maxLuma > 0.0) c = rbGlassCompressMaxLuma(unpre, maxLuma);  // %876..%892
    precise vec3 m = rbGlassFaceColorMatrixRaw(c, cm0, cm1, cm2);   // %906..%943
    precise vec3 r = unpre + (m - unpre) * vec3(faceOpacity);       // %946
    return r;
}

// =========================================================================
// block 11 -- the specular
// =========================================================================

// `[BIN]` mod98 %1048..%1072. The luma triple is `kRbLumaWeight`, the SECOND
// Rec. 709 rounding -- not the one the compression uses.
float rbBgSpecularWeight(float d, vec3 faceRgb, float distance0, float distance1,
                         float opacity, float weightScale, float weightBias) {
    precise float span = distance1 - distance0;            // %1053
    precise float inv = 1.0 / span;                        // %1054
    precise float base = (-distance0) / span;              // %1055, %1056
    precise float t = clamp(d * inv + base, 0.0, 1.0);     // %1057, %1058
    precise float t2 = t * t;                              // %1059
    precise float w0 = opacity * t2;                       // %1060
    precise float y = (faceRgb.r * kRbLumaWeight.r + faceRgb.g * kRbLumaWeight.g) +
                      faceRgb.b * kRbLumaWeight.b;         // %1062
    precise float ys = clamp(y, 0.0, 1.0);                 // %1063
    precise float q = weightScale * ys + weightBias;       // %1069
    precise float q2 = q * q;                              // %1070
    precise float q4 = q2 * q2;                            // %1071
    precise float r = w0 * q4;                             // %1072
    return r;
}

// `[BIN]` mod98 %963..%970 -- the specular's own band, no offset.
float rbBgSpecularHeight(float d, float amount, float invHeight) {
    return rbGlassBandAmount(d, amount, invHeight, 0.0);
}

// `[BIN]` mod98 %999..%1077 -- alpha carried through untouched.
vec4 rbBgSpecular(vec4 face, vec3 backdrop, float weight, vec4 cm0, vec4 cm1, vec4 cm2) {
    precise vec3 m = rbGlassFaceColorMatrixRaw(backdrop, cm0, cm1, cm2);
    precise vec3 r = face.rgb + (m - face.rgb) * vec3(weight);   // %1075
    return vec4(r, face.a);
}

// =========================================================================
// block 12 -- composite, ring, and the epilogue
// =========================================================================

// `[BIN]` mod98 %355..%361 -- the comparisons are `olt`, transcribed as `<`.
bool rbBgIsZero(float maskValue, float shadowCoverage, float highlight, float ringShadow) {
    return maskValue == 0.0 && shadowCoverage < kRbGlassEpsilon &&
           highlight < kRbGlassEpsilon && ringShadow < kRbGlassEpsilon;
}

// `[BIN]` mod98 %1080..%1082.
vec4 rbBgComposite(vec4 shadowPre, vec4 facePre, float maskValue) {
    precise vec4 r = shadowPre + (facePre - shadowPre) * vec4(maskValue);
    return r;
}

// `[BIN]` mod98 %1084..%1088. The value applied here is %311 -- the RING
// SHADOW's -- and it lands in ALPHA only.
vec4 rbBgApplyRingShadow(vec4 col, float ringShadow) {
    precise vec4 r = col * vec4(1.0 - ringShadow) + vec4(0.0, 0.0, 0.0, ringShadow);
    return r;
}

// `[BIN]` mod98 %1117..%1209.
vec3 rbBgHighlightFillColor(vec3 backdrop, float highlight, float faceOpacity,
                            float maxLuma, vec4 cm0, vec4 cm1, vec4 cm2, float clampMode,
                            float colorBias) {
    if (!(faceOpacity > 0.0)) return backdrop;                      // %1119
    precise vec3 c = backdrop;
    if (maxLuma > 0.0) c = rbGlassCompressMaxLuma(backdrop, maxLuma);
    precise vec3 m = rbGlassFaceColorMatrixRaw(c, cm0, cm1, cm2);
    if (clampMode > 0.0) {                                          // %1194
        if (colorBias < 0.0) m = min(m, backdrop);                  // %1199, %1201
        else                 m = max(m, backdrop);                  // %1203
    }
    precise float w = highlight * faceOpacity;                      // %1206
    precise vec3 r = backdrop + (m - backdrop) * vec3(w);           // %1209
    return r;
}

// `[BIN]` mod98 %1089..%1241.
vec4 rbBgApplyKeyFillHighlight(vec4 col, float highlight, vec3 backdrop, float faceOpacity,
                               float maxLuma, vec4 cm0, vec4 cm1, vec4 cm2, float clampMode,
                               float colorBias, float alphaGain) {
    if (!(highlight > kRbGlassEpsilon)) return col;         // %1089
    precise vec3 rgb = col.rgb;                             // %1091
    precise float a = col.a;                                // %1092
    precise float t;
    precise vec3 src;
    if (!(a < 1.0)) {                                       // %1093 -> %1094
        t = 1.0 - a;                                        // %1095
        src = vec3(0.0);                                    // %1218's zeroinitializer
    } else {
        precise vec3 fill = rbBgHighlightFillColor(backdrop, highlight, faceOpacity,
                                                   maxLuma, cm0, cm1, cm2, clampMode,
                                                   colorBias);
        t = 1.0 - a;                                        // %1212
        rgb = fill * vec3(t) + rgb;                         // %1215
        src = backdrop;                                     // %1116 -> %1218
    }
    precise float k = highlight * colorBias;                // %1223
    precise vec3 f = vec3(-2.0) * rgb + vec3(3.0);          // %1226
    precise vec3 g = vec3(k) * f + vec3(1.0);               // %1227
    precise vec3 h = rgb * g;                               // %1228
    precise float gain = highlight * alphaGain;             // %1232
    precise float aOut = gain * t + a;                      // %1233
    precise vec3 outRgb = (-src) * vec3(1.0 - aOut) + h;    // %1235..%1239
    return vec4(outRgb, aOut);
}

// `[BIN]` mod98 %1244..%1277.
vec4 rbBgFade(vec4 col, float d, float headroom, float gradientDistance0,
              float gradientInvSpan, float toneWhite) {
    if (!(headroom > 0.0)) return col;                      // %1246
    precise float x = d - gradientDistance0;                // %1251
    precise float t = clamp(x * gradientInvSpan, 0.0, 1.0); // %1254, %1255
    precise float s = 1.0 - t;                              // %1256
    precise float a = col.a;
    precise float ai = max(a, kRbGlassEpsilon);             // %1260
    precise vec3 c = col.rgb / vec3(ai);                    // %1264
    precise vec3 c2 = vec3(toneWhite) * c;                  // %1267
    precise float sa = clamp(a, 0.0, 1.0);                  // %1268
    precise vec3 tgtRgb = vec3(sa) * c2;                    // %1271
    precise vec4 tgt = vec4(tgtRgb, sa);                    // %1273
    precise float w = headroom * s;                         // %1274
    precise vec4 r = col + (tgt - col) * vec4(w);           // %1277
    return r;
}

// `[BIN]` mod98 %1280..%1297. `-0.75` is `0xHBA00`; the re-premultiply uses the
// RAW alpha, not the epsilon-floored one.
vec4 rbBgClampHeadroom(vec4 col, float ceiling) {
    if (!(ceiling > 0.0)) return col;                       // %1283
    precise float a = col.a;
    precise float ai = max(a, kRbGlassEpsilon);             // %1286
    precise vec3 c = col.rgb / vec3(ai);                    // %1290
    precise vec3 k = clamp(c, vec3(-0.75), vec3(ceiling));  // %1293
    precise vec3 r = vec3(a) * k;                           // %1295
    return vec4(r, a);
}

#endif
