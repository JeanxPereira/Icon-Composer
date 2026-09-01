# Icon Composer inspector — full control inventory ↔ RE parameter map

Source: Apple's Icon Composer screenshot (`photos.icon`, macOS build, inspector open).
Each UI control cross-referenced to the reverse-engineered `IconRendering` parameter and
its value mapping. This is the authoritative UI↔param bridge for the open-source tool.

## Toolbar (top)
| Control | Meaning |
|---|---|
| Sidebar toggle | show/hide layer panel |
| `photos.icon` | document title |
| ⊘ circle + `26` + `27` badges | preview-generation toggles (render style: neutral / iOS-macOS 26 / 27) |
| light–dark pill switch (glass glyph) | appearance preview toggle (Default ↔ Dark) |
| grid ▾ | show-all-appearances grid |
| `1024pt 1x ▾` | preview size + scale factor |
| `100% ▾` | zoom |
| eyedropper / document icons (top-right) | inspector tabs (attributes / file info) |

## Layer panel (left)
- Tree: root `photos` → nested `Group`s → named layers (`orange`, `blue`, `red`, `mint`,
  `yellow`, `purple`, `pink`, `green`), each with a thumbnail. Maps to `.icon` `icon.json`
  `groups[].layers[]`. Disclosure per group. `+ / −` footer = add/remove layer.

## Canvas footer
- `iOS, macOS` pill → target platforms/idioms.
- `Default` pill + 3 swatches → appearance variants row (Default / Dark / Mono, plus Clear).

## Inspector (right) — the parameter panel

Section headers carry an appearance scope selector (`Default ▾` / `All ▾`) — controls whether
edits apply to the base or a per-appearance specialization.

### Color
| Control | Type | Shown | RE parameter | Mapping |
|---|---|---|---|---|
| Opacity | slider + % | 100% | layer `opacity` | 0–100% → 0–1 |
| Blend Mode | dropdown | Multiply | layer `blendMode` | enum (Normal, Multiply, …) |

### Liquid Glass  (per-layer `GlassMaterial`)
| Control | Type | Shown | RE parameter | Mapping / default |
|---|---|---|---|---|
| Mode | dropdown | Individual | `gathersSpecularByElement` (per-element vs combined) | Individual / Combined |
| Specular | dropdown | Inside | `specularPlacement` = **CUIThemeSpecularPlacement** | Inside / Outside (the enum we hunted) |
| Specular · Dark | dropdown (sub) | Inside | per-appearance `specularPlacement` (dark) | specialization |
| Specular · Mono | dropdown (sub) | Outside | per-appearance `specularPlacement` (mono/tinted) | specialization |
| Blur | toggle + % | 50% | `blurStrength` | 0–100% → 0–`blurStrengthMax` (64); material recipe `blurRadius` 45 is the fully-on ref |
| Refractivity | toggle + 2D pad + 2 fields | on; ↻ 23%, ↕ 14% | `refractionHeight` (↻) + `refractionStrength` (↕) | Height% → ramp min 12.8…max 256 (power 1); Strength% → 0…strengthMax 640. Pad = the height→displacement ramp curve |
| Translucency | toggle + % | 50% | `translucency` | 0–100% → 0–1. **Default 50% == RE default 0.5 ✓** |
| Translucency · Mono | toggle + % | on | per-appearance `translucency` (mono) | specialization |
| Shadow | dropdown + % | Chromatic 5% | `shadowStyle` + `shadowOpacity` | Chromatic / Neutral; % → opacity |
| Shadow · Dark | dropdown + % | Neutral | per-appearance shadow (dark) | specialization |
| Shadow · Mono | dropdown + % | Neutral | per-appearance shadow (mono) | specialization |

### Composition
| Control | Type | Shown | RE parameter | Mapping |
|---|---|---|---|---|
| Visible | toggle | on | layer `visible` | bool |
| Layout | x / y / scale | 0pt, 0pt, 100% | layer `transform` (translation + scale) | pt offsets + scale% |

## Confirmations this screenshot gives the RE
- **Specular placement enum values = {Inside, Outside}** — resolves Q6 (`CUIThemeSpecularPlacement`)
  at the UI level (raw int values still to enumerate, but the semantic set is Inside/Outside).
- **Translucency default 50% ⇒ `translucency = 0.5`** — matches the recovered GlassMaterial default.
- **Refractivity is two knobs** — Height (↻) and Strength (↕) — matching `refractionHeight` +
  `refractionStrength`; the 2D pad visualises the ramp (our `min 12.8 / max 256 / power 1`).
- **Per-appearance specializations** for Specular / Translucency / Shadow appear as indented
  Dark / Mono sub-rows — matches the `*-specializations` in `icon.json`.
- **Default per-layer blend = Multiply** (at least for this icon's layers).
