# Appearance & material recipes — exact numeric values

Decoded from the macOS/iOS 27 `CoreMaterial.framework` + `MaterialKit.framework` recipe files
(binary plists, `plistlib`). These are the **exact numbers** the material system applies. Two
families (as in [`../liquid-glass/material-recipes.md`](../liquid-glass/material-recipes.md)):

- **Glass family** — `blurRadius` + a 4×5 `colorMatrix` only (the Liquid Glass look).
- **Vibrancy family** — `luminanceAmount`/`luminanceValues` + `saturation` + `brightness` +
  `backdropScale` + `blurAtEnd` (classic adaptive vibrancy).

## The Liquid Glass material — `platformContentGlass`

```jsonc
// platformContentGlass.materialrecipe  (materialSettingsVersion 2)
"blurRadius": 45,
"colorMatrix": {           // 4×5, row-major (RGBA + offset column)
  "m11": 0.921, "m12": -0.265, "m13": -0.027, "m14": 0, "m15": 0.235,
  "m21": -0.079,"m22": 0.735,  "m23": -0.027, "m24": 0, "m25": 0.235,
  "m31": -0.079,"m32": -0.265, "m33": 0.973,  "m34": 0, "m35": 0.235,
  "m41": 0,     "m42": 0,      "m43": 0,      "m44": 1, "m45": 0
}
```

**`Darker`, `Lighter`, `UltraDarker` have an identical `baseMaterial`** — the light/dark/tint
variation is *not* in the recipe; it's applied by IconRendering's brightness ramps / tint matrix on
top. So the glass tint itself is one fixed matrix.

**This matrix IS the `glass_background` kernel's color matrix** (see
[`kernels/icon-glass.md`](kernels/icon-glass.md#simulatedglassglass_background)):
```
cmRow0 = (0.921, -0.265, -0.027)
cmRow1 = (-0.079, 0.735, -0.027)
cmRow2 = (-0.079, -0.265, 0.973)
cmOffset = (0.235, 0.235, 0.235)     // the milky-white lift
```
Diagonal ≈ 0.92/0.735/0.973, small negative cross-desaturation, +0.235 white offset = the milky glass.

Other **Glass-family** members (same structure, `blur 45` + a colorMatrix): `platformDisabled`,
`platformPinched`, `platformSelected`; and `blur 15` + colorMatrix: `knowledgePlatters{,Dark}`,
`modules` (blur 0).

## Vibrancy-family recipes (full table)

`bs`=backdropScale, `bae`=blurAtEnd, `lum`=luminanceAmount, `lumV`=luminanceValues, `sat`=saturation,
`br`=brightness.

| recipe | blur | lum | lumValues | sat | br | other |
|---|---:|---:|---|---:|---:|---|
| `platformContentLight` | 30 | 0.75 | [0.9, 0.83, 0.925, 0.815] | 1.5 | 0.1 | bs .25, bae |
| `platformContentDark` | 30 | 0.75 | [0.16, 0.26, 0.1, 0.1] | 1.5 | 0 | bs .25, bae |
| `platformContentThinLight` | 30 | 0.6 | [0.725, 0.825, 0.76, 0.73] | 1.35 | 0.12 | |
| `platformContentThinDark` | 30 | 0.6 | [0.2, 0.21, 0.1, 0.15] | 1.35 | 0 | |
| `platformContentThickLight` | 45 | 0.88 | [0.99, 0.95, 0.98, 0.905] | 1.5 | 0.045 | |
| `platformContentThickDark` | 45 | 0.88 | [0.14, 0.16, 0.1, 0.03] | 1.5 | 0 | |
| `platformContentUltraThinLight` | 22.5 | 0.5 | [0.45, 0.55, 0.65, 0.68] | 1.1 | 0.12 | |
| `platformContentUltraThinDark` | 22.5 | 0.5 | [0.24, 0.24, 0.3, 0.39] | 1.1 | 0 | |
| `platformChromeLight` | 22.5 | 0.75 | [0.8, 0.9, 1.1, 0.825] | 1.1 | 0.1 | |
| `platformChromeDark` | 22.5 | 0.75 | [0.23, 0.52, 0.27, 0.255] | 2.0 | −0.1 | |
| `dockLight` | 30 | 0.5 | [0.3, 0.5, 1.0, 0.77] | 1.8 | 0.08 | bs .25, bae |
| `dockDark` | 30 | 0.5 | [0.29, −0.2, 0.375, 0.65] | 1.6 | 0 | |
| `platters` | 30 | 0.6 | [0.775, 0.85, 1.05, 0.94] | 2.4 | 0 | |
| `plattersDark` | 30 | 0.4 | [0.41, −0.4, 0.3, 0] | 1.4 | −0.03 | |
| `tintablePlatters` | 30 | — | — | 1.8 | — | bs .25, bae |
| `knowledgeBackground` | 29.5 | 0.47 | [0, 0.13, 0.13, 0] | 1.35 | — | bs .45 |
| `knowledgeBackgroundDark` | 29.5 | 0.57 | [−0.1, 0.03, 0.03, −0.1] | 1.45 | — | bs .45 |
| `knowledgePlattersSheer` | 30 | 0.6 | [0.775, 0.85, 1.05, 0.94] | 2.4 | — | |
| `modulesBackground` | 25 | 0.4 | [0.38, 0, 1, 0.76] | 1.6 | — | zoom 0.04 |
| `toolbarButtonBackground` | 15 | 0.5 | [0.24, 0.24, 0.3, 0.39] | 1.1 | — | |
| `previewBackground` | 11.2 | — | — | — | — | zoom 0.024 |
| `ambientCompact` | 40 | — | curvesValues RGB [0,0.7,0.88,0.88] | — | — | bs .25, bae |
| `carPlayPlatters{,Dark}` | 15 | — | — | — | — | |

`*ReduceTransparency` variants drop blur and set `averageColorEnabled: true` (accessibility).

## `vibrantColorMatrix` visualstylesets (fills / strokes)

These `.visualstyleset` files carry a `vibrantColorMatrix` `inputColorMatrix` (used for platter/fill
tinting; some set `inputBackdropAware: true`):

| styleset | notable |
|---|---|
| `platformFillLight` | strong matrix, m15/m25/m35 offset **0.0625** |
| `platformFillDark` | offset **0.298** |
| `platterFillLight` | near-identity, offset **−0.08** |
| `platterFillDark` | near-identity, offset **+0.06** |
| `moduleFill` | `inputBackdropAware: true`, offset **0.97** |
| `platformColorsLight` | dark tint matrix, m45 offset 0.1 |
| `platterVibrantShadow{Light,Dark}` (MaterialKit) | the drop-shadow vibrancy (below) |

## `platterVibrantShadow{Light,Dark}` — the chiclet drop shadow

`shadow: { kind: "neutral" }` in `.icon` resolves here (`vibrantColorMatrix`). Dark inherits Light
(`secondary`) and overrides:

| `filterProperties` | Light | Dark |
|---|---:|---:|
| `darkenAll` | 0.15 | 0.25 |
| `saturationAdjustment` | 0.25 | 0.25 |
| `darkenShadowHighlights` | 0.0 | −0.5 |
| `blueYellowContrast` | 0 | −0.5 |
| `redGreenContrast` | 0 | −0.25 |

## New recipe types (iOS/macOS 27)

- **`.descendantrecipe`** / **`.descendantstyleset`** — recipes that **inherit from a parent** and
  override deltas (e.g. `platformChromeFill{Light,Dark}`, `platformStrokeThin*`, `modulesSheer`).
  Same base+override philosophy as the `.icon` specializations. *(structure decoded; per-file deltas
  TODO)*

## Still code-side (needs Ghidra on IconRendering)

The per-appearance **brightness ramps** (`*TintStart/EndBrightness`, `*ClearStart/End`,
`fakeGlass*`), `refractionHeight{Min,Max,Power}`, `refractionStrength{Max,Power}`, and the specular
curvatures are **not** in these resource files — they're constants in the `IconRendering` binary.
See [`README.md` § Next steps](README.md#next-steps).
