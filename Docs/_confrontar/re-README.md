> ## ⚠️ MATERIAL HERDADO — NÃO É BASE, É MATERIAL A CONFRONTAR
>
> **Decidido em 2026-08-01.** Estes documentos vieram do `floyd-overlay`, onde a
> receita de liquid glass **nunca fechou**. Eles registram o que já foi
> investigado, mas suas conclusões não são confiáveis: partir delas é herdar os
> erros delas.
>
> **A RE do liquid glass (projeto B) será feita LIMPA, do binário**, sem ler
> estes arquivos primeiro. Só depois de chegar a uma conclusão independente é
> que se compara com o que está escrito aqui. Onde divergir, **o binário ganha**,
> e a divergência é registrada — saber onde alguém errou antes tem valor.
>
> O que aqui é observação direta (schema `.icon` validado contra documentos
> reais, UI copy extraída literalmente do binário) tende a estar certo. O que é
> *interpretação* (valores de material, semântica de parâmetro, receita de
> render) é justamente o que não fechou.
>
> Ver `docs/superpowers/specs/2026-08-01-tauri-wgpu-shell-design.md` §1.5.

# Icon Composer, Reverse-Engineered — the Authoring App

A community reference for how Apple's **Icon Composer** (the macOS authoring tool for Liquid Glass
app icons) is built: the app bundle, the framework stack, the `.icon` document model, and how its
UI controls map onto the `IconRendering` glass shaders.

> **Status:** work in progress. Bundle + framework map = done (static, from `strings`). `.icon`
> document model = **validated against real documents** (`examples/`). Shader IR = **pending**
> (llvmlite disasm of the two `default.metallib`s — see [Next steps](#next-steps)).

## Documents in this reference

- [`editor-architecture.md`](editor-architecture.md) — framework layering, document model, edit→bake→
  render data flow, undo/selection, export — **6 mermaid diagrams** ✅
- [`editor-ui.md`](editor-ui.md) — window layout, toolbar, layer sidebar, the full inspector catalog,
  color panel, export sheet, context menus, and the menu bar ✅
- [`kernels/icon-glass.md`](kernels/icon-glass.md) — reconstructed shader math for all 16 IconRendering
  kernels (`glass_background` refraction, `glassHighlight` specular, `distance_gradient`, fills, glow,
  blend) ✅
- [`renderbox-engine.md`](renderbox-engine.md) — the RenderBox 2D GPU engine roster (100+ kernels) +
  the general `glassBackground_v1`/`glassForeground_v1` uniform layouts (diff target vs the QuartzCore
  glass ref) ✅
- [`appearance-recipes.md`](appearance-recipes.md) — decoded `.visualstyleset` numeric values
  (shadow-platter vibrancy) ✅
- [`dynamic-menu-recipe.md`](dynamic-menu-recipe.md) — Frida/lldb/AX recipe to dump the live menu bar
  + keyboard shortcuts (run on the hackintosh) — the one thing static RE can't recover ⏳
- [`examples/`](examples/) — real `.icon` documents (Discord, ImHex) used to validate the schema ✅
- This file — bundle map, framework roles, `.icon` document model, fidelity map vs `floyd-overlay`.

## Method & provenance

- **Subject:** `Icon Composer.app`, **version 27.0 (build 27A5209)**, SDK `macos 27.0`, built with
  Xcode `2700` (`23A344017`), min system `26.4`. Bundle id `com.apple.IconComposer`. Document type
  UTI `com.apple.iconcomposer.icon` — a **package** (`com.apple.package`), extension `.icon`.
- **Extraction:** the full bundle survived a DMG→Windows copy intact (payload under each framework's
  `Versions/A/`; only the HFS `Versions/Current` symlinks were lost). Analyzed on Windows with
  `strings` (mingw) + `llvmlite 0.47.0` for the AIR/bitcode. No macOS required for the static pass.
- **Clean-room posture:** same as [`../liquid-glass`](../liquid-glass/README.md). We document the
  architecture, symbol-level design, and reconstructed math in our own words. We do **not**
  redistribute Apple's binaries, bitcode, or resources. Reimplement, don't paste.

## Bundle map

All binaries are universal Mach-O (x86_64 + arm64e).

| Path (under `Contents/`) | Size | Role |
|---|---:|---|
| `MacOS/Icon Composer` | 204 KB | app shell (thin — logic is in the frameworks) |
| `Frameworks/IconComposerKit` | 6.5 MB | **document model + editor UI** (inspectors, layer sidebar, panels) |
| `Frameworks/RenderBox` (+ `default.metallib` 1.95 MB) | 5.5 MB | **2D GPU engine** (primitive/gradient/blur/distance/blend/stroke/mesh/mask/composite) |
| `Frameworks/IconComposerFoundation` | 3.6 MB | shared foundation types / serialization |
| `Frameworks/IconRendering` (+ `default.metallib` 114 KB) | 2.2 MB | **the glass renderer** (stitched icon kernels; same family as the on-device one) |
| `Frameworks/CoreSVG` | 0.9 MB | SVG import (vector layer artwork) |
| `Executables/ictool` | 177 KB | **compile** `.icon` → asset catalog / packaged output |
| `Executables/icrtool` | 120 KB | **render** `.icon` → rasterized images |
| `PlugIns/…QuickLook Preview.appex` | 197 KB | Finder Quick Look of `.icon` |
| `PlugIns/…Thumbnail.appex` | 196 KB | Finder thumbnails of `.icon` |
| `Frameworks/IconComposerKit/…/Resources/Backgrounds/*.jpeg` | ~10 MB | the 6 preview wallpapers (`sine-*`) behind the icon canvas |
| `…/Assets.car` (app + kit) | — | UI assets |

## Framework stack (who does what)

```
Icon Composer.app  (SwiftUI shell)
  └─ IconComposerKit         editor: inspectors, layer tree, .icon (de)serialization, preview canvas
       ├─ IconComposerFoundation   shared model/value types
       ├─ CoreSVG                  vector artwork import
       └─ IconRendering            builds the layer stack → bakes a FinalizedIcon
            └─ RenderBox           the actual GPU engine; icon glass kernels are STITCHED onto it
  Executables/ictool, icrtool      headless compile/render (same IconRendering+RenderBox path)
```

**Key architectural finding:** the icon glass is **not** a monolithic shader. `RenderBox` is a
general 2D vector/layer GPU engine, and the icon-specific glass is a set of **function-stitched**
fragments (`RB::stitch::*`) plugged into it. The stitch connectors name their signatures — e.g.
`RB::stitch::Cfloat2_half4` matches `glassHighlight(…, float2 dir, half4 color)`. So:

- **RenderBox `default.metallib`** = the engine kernels: `primitive`, `gradient` (linear/cubic/mesh),
  `Filter::Blur`, `Filter::Distance` (SDF), `blend`/`composite` (with `saturation`, `pdf_mode`
  blend math), `stroke`, `image`, `mask`. Blend/composite lives in `RB::Shader::composite`.
- **IconRendering `default.metallib`** = the glass pieces stitched on top:
  `SimulatedGlass::glass_background`, `SimulatedGlass::distance_gradient`, `glassHighlight`,
  the **bevel/emboss** kernel (`height, inset, spread, bias_amount, curvature, light_dir, color`),
  `shapeAwareGradientMask` / `simplifiedShapeAwareGradientMask`, `sdfFill`, `glow`, `clampedPlusL`
  (Plus-Lighter clamp), `clampToEdges`, and a tint-matrix pass (`tintMatrixRow0..3`).

(The IconRendering kernel roster + uniform signatures are documented in
[`../icons/`](../icons/) from the iOS 27 pull; the macOS 27 app ships the same family.)

## The `.icon` document model

A `.icon` is a **package** (directory): `icon.json` manifest + an `Assets/` folder of layer artwork
(SVG, sometimes PNG). Schema below is **validated against real documents** (`examples/Discord.icon`,
`examples/ImHex.icon`), cross-checked with the `IconComposerKit` symbol names.

### Top level

```jsonc
{
  "fill": { … },                    // chiclet background fill (see Fill)
  "groups": [ Group, … ],
  "supported-platforms": {          // idiom → platform routing
    "squares": "shared",            // "shared" or a platform list
    "circles": ["watchOS"]
  }
}
```

### Group

```jsonc
{
  "layers": [ Layer, … ],
  "shadow":       { "kind": "neutral", "opacity": 0.5 },
  "translucency": { "enabled": true,  "value": 0.5 },
  "position-specializations": [ { "idiom": "square",
      "value": { "scale": 10, "translation-in-points": [0,0] } } ]
}
```

### Layer

```jsonc
{
  "name": "5",
  "image-name": "5.svg",            // → Assets/5.svg
  "glass": true,                    // ← per-layer Liquid Glass toggle (THE key flag)
  "hidden": false,
  "fill": { … } | "fill-specializations": [ Spec, … ],
  "opacity-specializations":     [ Spec, … ],   // value 0..1
  "blend-mode-specializations":  [ Spec, … ],   // "normal" | "screen" | …
  "hidden-specializations":      [ Spec, … ],
  "position-specializations":    [ Spec, … ],
  "image-name-specializations":  [ Spec, … ]    // swap the artwork per appearance/idiom
}
```

Confirmed specialization keys (from 16 real Xcode-26 `.icon` bundles): `fill-`, `opacity-`,
`blend-mode-`, `hidden-`, `position-`, `image-name-specializations`.

### Fill types & colors

```jsonc
"fill": { "solid": "srgb:1,1,1,1" }
"fill": { "linear-gradient": [ "display-p3:0.19,0.31,0.69,1", "display-p3:0.07,0.12,0.29,1" ],
          "orientation": { "start": {"x":0.5,"y":0}, "stop": {"x":0.5,"y":0.7} } }
"fill": { "automatic-gradient": "srgb:…" }   // auto light→dark ramp from one seed color
"fill": "none"
```

Colors are **colorspace-prefixed float tuples**, confirmed across 16 real bundles:
`srgb:r,g,b,a`, `display-p3:r,g,b,a`, `extended-srgb:…`, `gray:v,a`, `extended-gray:…`.
Wide-gamut **P3 + extended-range is native** to the format — an open re-implementation must carry a
wide-gamut/EDR-aware pipeline, not plain sRGB.

### Specializations — the appearance/idiom override mechanism (the crown)

Every visual property is encoded as a **variant array**. The entry with **no discriminator** is the
base (= Light, default idiom); entries carry a discriminator to override:

- `"appearance": "dark" | "tinted"` — Light is the implicit base; **Clear** derives from the glass
  path. `"value": "none"` disables the property for that appearance (e.g. hide a fill in Dark).
- `"idiom": "square" | "circle"` — shape family (circle = watch). Overrides position/hidden per shape.

```jsonc
"fill-specializations": [
  { "value": { "solid": "gray:0.79438,1" } },                 // base (Light)
  { "appearance": "dark",   "value": "none" },                // Dark: no fill
  { "appearance": "tinted", "value": { "solid": "gray:0.8,1" } }
]
```

This base+override array is the **core of the format** and the first thing to model.

### Not present until customized

`specular` / `lighting` / `light-direction` / `blur-material` appear in **none** of the 16 real
Xcode-26 `.icon` bundles (a full disk search confirmed their absence). The renderer applies defaults
keyed off `glass:true`; those fields are written to `icon.json` only when the user edits the
`LayerSpecularInspector` / `GroupSpecularInspector` / `BlurMaterialInspector`. Capturing their exact
field names + ranges needs a document authored **with** customized specular (none exists on disk yet)
— or the code-side defaults via Ghidra (see next steps).

See [`examples/`](examples/) for the two annotated reference documents.

## Light / Specular controls → shader uniforms (the fidelity target)

This is the "maximum fidelity to specular / light direction" goal, mapped end-to-end:

| UI (Icon Composer) | `.icon` field | IconRendering param | Shader uniform |
|---|---|---|---|
| Specular highlight | `SpecularHighlight` / `HighlightStyle` | `CUIGlassHighlight`, `totalHighlightsStrength`, `CUIThemeSpecularPlacement` | `glassHighlight(… float2 dir, half4 color)` |
| Light direction | `Lighting` / `Light` (angle) | `inputAngleSinCos`, `lightDirection` (longitude **+** latitude) | `light_dir`, `lightDirection` |
| Depth / bevel | (group/layer effect) | `inputHeight`, `inputCurvature`, `inputSpread`, `inputBiasAmount`, `refractionHeight{Min,Max,Power}`, `refractionStrength{Max,Power}` | bevel kernel `height,inset,spread,bias_amount,curvature` |
| Highlight vs darklight | — | `chiclet/glyph HighlightCurvature`, `chiclet/glyph DarklightCurvature` | (per-layer highlight + opposite-side shade) |
| Appearance tint | `blurMaterialSpecializations`, tint | `tintMatrixRow0..3`, `*TintStart/EndBrightness` | tint-matrix pass |

## Fidelity / reuse map vs `floyd-overlay`

| Piece | floyd status |
|---|---|
| SDF fill / distance | ✅ have (`distance_gradient` ≈ our SDF) |
| Glass refraction core | ⚠️ `SimulatedGlass::glass_background` is a **different** impl than the QuartzCore one we RE'd — related, re-verify |
| Directional `glassHighlight` | ⚠️ have a highlight, not this exact rig |
| **Bevel/height/light_dir** | ❌ **new** — the heart of the icon's 3D specular |
| `shapeAwareGradientMask`, `clampedPlusL`, tint-matrix | ❌ new (icon-specific compositing) |
| Blur / gradient / blend engine | ✅ RenderBox concepts map to our passes |
| Layer-stack baker + appearance specializations | ❌ **new orchestration layer** — the bulk of an "icon composer" that isn't material math |

**Verdict:** floyd's engine is a ~50–60 % material head-start. The genuinely new work an
open-source Icon Composer needs — the layer-stack baker, the bevel/light_dir depth kernel, the
appearance-specialization document model, and the `ictool`/`icrtool`-equivalent export — is now
fully mapped at the symbol level and buildable.

## Next steps

1. **Metallib → IR (llvmlite).** ✅ IconRendering metallib (16 kernels) all extracted to `.ll`.
   `glassHighlight` + `distance_gradient` reconstructed & verified — see
   [`kernels/icon-glass.md`](kernels/icon-glass.md). Remaining: `SimulatedGlass::glass_background`
   (refraction core), the gradient-mask/glow/compositing kernels, and the RenderBox metallib.
2. **Decode the appearance recipes.** `../icons/…/visualstyleset/platterVibrantShadow{Light,Dark}.visualstyleset`
   (binary plist) for exact numeric defaults.
3. **Dump the `ictool` / `icrtool` arg parser** to spec the export pipeline (`.icon` → catalog / PNGs).
4. ✅ **`.icon` JSON schema** — done, validated against `examples/Discord.icon` + `examples/ImHex.icon`.
   Remaining: capture a document with **customized specular/lighting** to pin down those field
   names + ranges (they are absent from default docs).

## Files

Static string dumps used for this doc live in the analysis scratchpad (not committed). Re-generate
with `strings -n 5` over each binary + `llvmlite` over the metallibs.
