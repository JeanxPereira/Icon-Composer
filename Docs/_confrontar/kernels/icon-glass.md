# Icon glass kernels — reconstructed math (IconRendering `default.metallib`)

Reconstructed from LLVM IR (llvmlite-parsed AIR bitcode) of the macOS 27 Icon Composer's
`IconRendering.framework/.../default.metallib` (16 stitched kernels). Math below is **verified
line-by-line against the IR** for the two kernels that form the specular/light-direction chain.

Half constants used: `0xH3C00`=1.0, `0xH1400`=0.25, `0xH4000`=2.0, `0xH3AAA`≈0.8331 (≈5/6).

## Pipeline (how the specular is produced)

```
SDF texture ──▶ distance_gradient ──▶ normal (2D)  ┐
                                                    ├─▶ glassHighlight ─▶ colored specular
light direction (sin,cos) ──────────────────────── ┘
```

The SDF (signed-distance field of the layer shape) is baked to a texture. `distance_gradient`
samples it and returns the **normalized gradient** = the 2D surface normal. `glassHighlight` dots
that normal with the light direction and confines the result to an antialiased edge band.

---

## `SimulatedGlass::distance_gradient`

**Signature:** `(float2 pos, texture2d<half> sdf, float2[5] xform, float2 scale) -> half4`

`xform` is an affine map stored as 5 columns: `m0,m1` (2×2 basis), `t` (translation),
`lo,hi` (uv clamp bounds).

```glsl
// affine sample of the SDF at an arbitrary position
half sampleSDF(float2 p) {
    float2 uv = clamp(p.x*m0 + p.y*m1 + t, lo, hi);
    return sdf.sample(uv).r;              // R channel = signed distance
}

half4 distance_gradient(float2 pos, ...) {
    half d0 = sampleSDF(pos).r;
    if (d0 == 0) discard_fragment();      // outside the shape → nothing

    // central differences, step = |screen-space derivative| of pos
    float dx = abs(dFdx(pos.x));
    float dy = abs(dFdy(pos.y));
    half gx = sampleSDF(pos + float2(dx,0)) - sampleSDF(pos - float2(dx,0));
    half gy = sampleSDF(pos + float2(0,dy)) - sampleSDF(pos - float2(0,dy));

    half2 g = half2(gx, gy);
    half2 n = (gx!=0 || gy!=0) ? g * rsqrt(dot(g,g)) : half2(0);   // normalized normal
    n *= (half2)scale;                    // tail: rescale into lighting space
    return half4(n, ...);                 // .xy = normal (consumed by glassHighlight)
}
```

> **Confidence:** high for the sampling + central-difference gradient + normalize. The tail (past the
> normalize) applies `scale` and packs the output; exact packing of `.zw` not yet nailed (IR cut).

---

## `glassHighlight` — the directional specular (the crown)

**Signature:** `(half d, half2 n, float width, float p3, float p4, float p5, float2 lightDir, half4 color) -> half4`

- `d` — signed distance to the shape edge (px), `n` — surface normal from `distance_gradient`.
- `width` — edge/rim width the highlight lives in.
- `lightDir` — 2D light direction (the UI's `inputAngleSinCos`, i.e. sin/cos of the light longitude).
- `p3` — directional threshold (`n·l` cutoff; higher ⇒ tighter highlight facing the light).
- `p4` — soft-normalization / contrast amount.
- `p5` — edge-proximity falloff strength.
- `color` — highlight color (half4 → can exceed 1.0 for EDR/bloom).

Verified reconstruction:

```glsl
half4 glassHighlight(half d, half2 n, float width,
                     float p3, float p4, float p5,
                     float2 lightDir, half4 color)
{
    // 1. antialiased edge band: 1 inside the [0,width] rim, 0 outside (fwidth AA)
    half aa   = clamp(fwidth(d), 0.25, 2.0) * 0.8331;
    half B1   = saturate((width - d)/aa + 0.5);
    half B2   = saturate( d        /aa + 0.5);
    half band = B1 * B2;

    // 2. edge-proximity falloff, dialed by p5
    //    = mix(1, 1 - saturate(d/width), p5)
    half A = 1.0 - p5 * saturate(d / width);

    // 3. directional (light) term
    half C = saturate( (dot((half2)lightDir, n) - p3) / max(1.0 - p3, 0.25) );
    half D = C * A;

    // 4. combine, softened by p4, tint by color
    half intensity = (D * band) / max(1.0 + p4*(1.0 - D), 0.25);
    return color * intensity;
}
```

### Mapping to the Icon Composer UI

| IR param | `.icon` / IconRendering | UI (`LayerSpecularInspector`) |
|---|---|---|
| `lightDir` | `inputAngleSinCos` (light longitude→sin,cos) | light angle / direction |
| `p3` | highlight-curvature-ish threshold | highlight tightness |
| `p4` | contrast/normalization | highlight strength/softness |
| `p5` | edge falloff | how far the highlight rides the edge |
| `width` | `inputBorderWidth` | rim width |
| `color` | highlight color (EDR-capable) | highlight color / `SolariumHighlightWhite` |

> **Confidence:** high — the arithmetic is a direct 1:1 transcription of the IR (fast-math, so
> associativity is free to reorder but values match). The only naming inference is which of
> `p3/p4/p5` the UI labels map to; the math itself is exact.

---

---

## `SimulatedGlass::glass_background` — the refraction core

**Signature:** `(float2 pos, texture2d content, float2 cXform[5], texture2d heightNormal,
float2 nXform[5], float refractHeight, float refractStrength, float4 cmRow0, cmRow1, cmRow2, cmOffset) -> half4`

- `content` — the texture being refracted (the backdrop/lower layers).
- `heightNormal` — a map with **R = height/coverage**, **GB = encoded 2D normal**.
- `refractHeight` / `refractStrength` — the UI's `refractionHeight*` / `refractionStrength*`.
- `cmRow0..2` + `cmOffset` — a **3×3 color matrix + offset** (the milky tint / chromatic shift;
  same role as the material-recipe `colorMatrix`).

```glsl
half4 glass_background(float2 pos, ...) {
    // 1. sample height + normal
    half4 hn     = sample(heightNormal, affine(pos, nXform));
    half  height = hn.r;
    half2 n      = hn.gb * 2.0 - 1.0;                 // decode normal [-1,1]

    // 2. refraction amount from height — smoothstep falloff (strong at edge, weak flat)
    float t = clamp((height - 0.0700) * 1.282 / refractHeight, 0.0, 1.0);
    half  r = (1.0 - smoothstep01(t)) * refractStrength;

    // 3. refract: sample content displaced along the normal
    float2 disp    = pos - r * (float2)n;
    half4  content = sample(content, affine(disp, cXform));

    // 4. color-matrix tint + coverage premultiply
    half3 tinted = half3(dot(content.rgb, cmRow0.rgb),
                         dot(content.rgb, cmRow1.rgb),
                         dot(content.rgb, cmRow2.rgb)) + cmOffset.rgb;
    return half4(tinted, content.a) * hn.a;
}
```

This is edge-lensing: near the shape edge (low `height`) refraction is strong, bending the sampled
content along the surface normal; the flat interior barely refracts. The magic remap
`(height-0.070)*1.282` normalizes the stored height before the smoothstep. **Confidence:** high
(the two remap constants are `0xBFB1EB85…`≈-0.070 and `0x3FF48348…`≈1.282, read from the IR).

> **Together with `glassHighlight`** this is the full icon material: `glass_background` bends + tints
> the backdrop; `glassHighlight` adds the directional specular on top; `distance_gradient` feeds the
> normal to both.

## Smaller kernels (all verified from IR)

```glsl
// antialiased SDF → fill coverage (smoothstep across the edge, fwidth AA)
half sdfFill(float2 p, tex, float scale, float bias) {
    half v   = tex.sample(p).r * scale + bias;
    half aa  = clamp(fwidth(v), 0.25, 2.0);
    half cov = clamp((v + aa*0.4165) / (aa*0.8331), 0, 1);
    return cov*cov*(3 - 2*cov);                       // smoothstep
}

// gaussian glow/bloom around the shape, edge-masked & normalized
half4 glow(float2 p, tex, float sigma, float intensity, half4 color, float scale, float bias) {
    half  x     = (tex.sample(p).r - bias) * scale / sigma;
    half  g     = exp(-0.5 * x*x);                    // gaussian
    half  denom = max(1 + (1-g)*intensity, 0.25);
    half  aa    = clamp(fwidth(x), 0.25, 2.0) * 0.8331;
    half  cov   = saturate(x/aa + 0.5);
    return color * (cov * g / denom);
}

// Plus-Lighter (additive) blend, rgb clamped to 1, then max with source
half4 clampedPlusL(half4 dest, half4 src) {
    half3 rgb = min(1.0, dest.rgb + src.rgb);
    half  a   = saturate(dest.a + src.a);
    return max(src, half4(rgb, a));
}

// shape-aware gradient: blends a directional linear gradient with the SDF edge falloff
half shapeAwareGradientMask(float2 p, tex, float invW, float base,
                            float2 point, float gradScale, float2 dir,
                            float distScale, float distBias) {
    half d = tex.sample(p).r;
    half w = saturate((distBias - d) * distScale / invW);      // edge proximity
    half g = saturate(dot(dir, point - p) / gradScale);        // linear gradient
    half m = mix(1 - g, g, w);                                 // edge picks direction
    half v = clamp(mix(1.0, base, m), 0, 1);
    return v*v*(3 - 2*v);                                      // smoothstep
}
```

`clampToEdges` is just a clamped-sampler helper (`RB::Layer` frame clamp). `distance_gradient`
(the normal source) is documented above.

> **App vs runtime note:** the iOS 27 *runtime* IconRendering metallib carried a separate **bevel**
> kernel (`height,inset,spread,bias_amount,curvature,light_dir,color`). The macOS 27 Icon Composer
> metallib does **not** — here the directional lighting is folded into `glassHighlight` and the
> `refractionHeight*`/height-map feeding `glass_background`. Reconcile when the runtime bevel is
> written up.

## Full kernel roster (IconRendering `default.metallib`, 16 fns)

| Kernel | Role | Status |
|---|---|---|
| `SimulatedGlass::glass_background` | refraction + tint core | ✅ reconstructed |
| `SimulatedGlass::distance_gradient` | SDF → 2D normal | ✅ reconstructed |
| `glassHighlight` | directional specular | ✅ reconstructed |
| `sdfFill` | antialiased shape fill | ✅ reconstructed |
| `glow` | gaussian glow/bloom | ✅ reconstructed |
| `clampedPlusL` | Plus-Lighter clamp blend | ✅ reconstructed |
| `shapeAwareGradientMask` / `simplified…` | shape-following gradient | ✅ reconstructed |
| `clampToEdges` | clamped sampler helper | ✅ trivial |
