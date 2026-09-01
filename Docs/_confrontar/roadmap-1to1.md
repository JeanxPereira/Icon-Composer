# Open Icon Composer — 1:1 implementation roadmap

Goal: a cross-platform (web/WebGPU) Icon Composer that reproduces Apple's tool 1:1 — the full
editor UI, the `.icon` format, and the Liquid Glass rendering. Fidelity target = perceptual
match against iPhone 16 Pro (iOS 27) device screenshots.

Reference specs: `docs/icon-composer-ui-spec.md` (control surface + serialized schema),
`floyd-overlay/docs/re/icon-composer/**` (RE: kernels, default ramps, recipes),
`floyd-overlay/shaders/glass_core.hlsl` (the tuned glass this port is based on).

Status legend: ✅ done · 🚧 in progress · ⬜ todo

---

## ✅ Slice 1 — engine skeleton (DONE)
WebGPU engine mounts in the Next app, renders a glass shape over a synthetic backdrop, driven
by `InspectorState`. Rebased on floyd-overlay's glass_core (analytic SDF, frost refraction,
faceOpacity tint, rim specular). Official shadcn UI components wired.

---

## M1 — Editor core (the app actually works)
Make every control real and stateful; the layer panel a true tree; the toolbar functional.
- ⬜ **M1.1 Inspector: wire every control** to `InspectorState` + engine — Opacity, Blend Mode,
  Mode, Specular (+ Dark/Mono sub-rows), Blur, Refractivity (2D pad + Height/Strength),
  Translucency (+ Mono), Shadow (kind + opacity, + Dark/Mono), Composition Layout (x/y/scale).
- ⬜ **M1.2 Refractivity 2D pad** — the draggable graph control (Height×Strength) as a real widget.
- ⬜ **M1.3 Layer panel CRUD** — add/remove layer & group, rename, drag-reorder, toggle visibility,
  selection drives inspector; per-layer state model (not one global InspectorState).
- ⬜ **M1.4 Per-layer state** — refactor `InspectorState` → a layer tree where each layer/group holds
  its own material; inspector edits the selected node.
- ⬜ **M1.5 Toolbar** — size selector, zoom, appearance preview toggle, appearance grid — all live.
- ⬜ **M1.6 macOS Tahoe polish pass** — spacing, typography, control styling to match screenshots.

## M2 — Content pipeline (real layers, real files)
- ⬜ **M2.1 `.icon` load** — parse `icon.json` + `Assets/*.svg` → app state (groups, layers, fills,
  specializations, shadow, translucency, supported-platforms).
- ⬜ **M2.2 SVG layer content** — rasterize each SVG layer to a texture (P3), not the test shape.
- ⬜ **M2.3 Multi-layer compositor** — composite the layer stack (fills + SVG content + blend modes)
  into the content plane the glass envelope refracts.
- ⬜ **M2.4 `.icon` save/export** — serialize state → `icon.json` + bundle the SVG assets.
- ⬜ **M2.5 SVG import** — add a layer from a dropped/opened SVG.

## M3 — Glass fidelity (match the device)
- ⬜ **M3.1 Calibrate** the floyd-based glass vs iPhone 16 Pro screenshots — refraction reach/amount,
  frost blur, tint, rim specular. (Needs device captures.)
- ⬜ **M3.2 Blur pyramid** — depth-driven mip LOD frost (replace single separable blur), per glass_core.
- ⬜ **M3.3 Supercircle SDF** — port `sdf_common.hlsli` (exact Apple squircle corners + per-corner
  radius/squareness) for the envelope; per-platform shape (iOS squircle, watchOS circle).
- ⬜ **M3.4 Per-layer glass vs flat** — honor `glass:true|false`; flat layers composite without the
  material; glass layers get the full treatment.
- ⬜ **M3.5 glass_foreground lens** — the 7-tap chromatic aberration rim lens (glass_core layer 2).

## M4 — Appearances (specializations)
- ⬜ **M4.1 Appearance model** — default / dark / tinted-light / tinted-dark / clear-light / clear-dark
  in state + the inspector scope selectors (`Default`/`All` + per-row Dark/Mono).
- ⬜ **M4.2 Per-appearance render** — colorMatrix + material params per appearance (the RE recipes:
  platformContentGlass + Darker/Lighter/UltraDarker; tinted/clear ramps; ClearMode when known).
- ⬜ **M4.3 `fill-specializations`** — per-appearance fill overrides incl. `"none"` (hide in appearance).
- ⬜ **M4.4 Appearance preview** — toolbar toggle + the appearance-grid view render each variant.

## M5 — Shadow & interaction polish
- ⬜ **M5.1 Drop shadow** — neutral/chromatic, opacity, the glass_core shadow pass.
- ⬜ **M5.2 Canvas interaction** — pan/zoom, select layer on canvas, drag to set Layout transform.
- ⬜ **M5.3 Background** — the shipped sine-* backgrounds + custom; the glass refracts it.

## M6 — Export & validation
- ⬜ **M6.1 PNG export** — render at 16…1024 × appearance × idiom (iOS/macOS), offscreen readback.
- ⬜ **M6.2 Device pixel-diff harness** — iPhone 16 Pro golden screenshots → SSIM/ΔE regression on
  the glass output; wire into CI-ish checks.
- ⬜ **M6.3 Fidelity pass** — close the diff per appearance/size until under the perceptual threshold.

## Cross-cutting / backlog
- ⬜ Confirm remaining RE enums (full BlendMode set, SpecularPlacement ints, glass sub-object keys).
- ⬜ iOS-27-vs-macOS kernel/constant diff (the ios27/* binaries are already in Ghidra).
- ⬜ WebGPU fallback messaging + perf (large canvases, live editing).

---

### Immediate next (this milestone = M1)
M1.4 (per-layer state) unblocks M1.1/M1.3. Suggested order: M1.4 → M1.1 → M1.3 → M1.2 → M1.5 → M1.6.
Then M2 (real content) so the glass refracts actual SVG layers instead of the test backdrop.
