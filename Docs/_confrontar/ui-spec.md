# Icon Composer — complete UI control spec (for 1:1 replication)

Compiled from Apple's Icon Composer screenshots + reverse-engineering of `IconRendering` /
`IconComposerKit`. Goal: an exhaustive checklist of every control to build. Enum values marked
**(RE)** are confirmed from the binaries; **(obs)** from screenshots; **(confirm)** = verify in the
app / via Ghidra before finalizing.

## 1. Window / Toolbar (top bar)
| Control | Type | Values / behavior |
|---|---|---|
| Sidebar toggle | icon button | show/hide layer panel |
| Document title | label | `<name>.icon` |
| Preview-generation cluster | segmented | `⊘` (none) · `26` · `27` — OS-version preview style (obs) |
| Appearance preview toggle | pill switch | flips the canvas Light ↔ Dark preview |
| Grid / show-all-appearances | dropdown button | tile all appearances |
| Preview size | dropdown | 512pt / 1024pt, scale 1x/2x/3x (obs) |
| Zoom | dropdown | 50 / 75 / 100 / 150 / 200 % |
| Inspector tabs (top-right) | icon buttons | Attributes · Info/File |

## 2. Layer panel (left)
- Tree: document root → **Group** nodes → **Layer** leaves, each with a thumbnail.
- Per row: disclosure (groups), name, thumbnail, drag-reorder, visibility.
- Footer: `+` (add layer/group) · `−` (remove).
- Selection drives the inspector. Maps to `.icon` `icon.json` groups[].layers[].

## 3. Canvas footer
- Platform pill: `iOS, macOS` (target idioms).
- Appearance bar: `Default` label + swatch row → **Default · Dark · Mono/Tinted · Clear** (obs; Clear = the clear/liquid variants).

## 4. Inspector — the parameter surface

Every section header has a **scope selector** (`Default ▾` / `All ▾`): edits apply to the base or a
per-appearance specialization (Dark / Mono / Clear).

### 4a. Color
| Control | Type | Default | Values | RE param |
|---|---|---|---|---|
| Opacity | slider + % | 100 | 0–100% | layer `opacity` |
| Blend Mode | select | Normal | **(RE, confirmed @0xaa6d6+):** normal, multiply, screen, overlay, darken, lighten, colorDodge, plusLighter, plusDarker, softLight, hardLight, difference, exclusion, hue, saturation, color, luminosity (CoreGraphics family) | layer `blendMode` |

### 4b. Liquid Glass  (per-layer `GlassMaterial`)
| Control | Type | Default | Values | RE param |
|---|---|---|---|---|
| Mode | select | Automatic | **Automatic · Individual · Combined** (obs) | `gathersSpecularByElement` / grouping |
| Specular | select | Inside | **Inside · Outside** (RE: CUIThemeSpecularPlacement) | `specularPlacement` |
| Specular · Dark | select (sub) | Inside | Inside · Outside | per-appearance |
| Specular · Mono | select (sub) | Outside | Inside · Outside | per-appearance |
| Blur | switch + % | off / 50 | 0–100% | `blurStrength` (max 64; recipe blurRadius 45) |
| Refractivity | switch + 2D pad + 2 fields | on / H 23% · S 14% | Height 0–100%, Strength 0–100% | `refractionHeight` (ramp 12.8–256, pow 1) + `refractionStrength` (max 640) |
| Translucency | switch + % | off / 50 | 0–100% | `translucency` (default 0.5) |
| Translucency · Mono | switch + % (sub) | on / 20 | 0–100% | per-appearance |
| Shadow | select + % | Chromatic / Neutral · 5 | **Chromatic · Neutral** | `shadowStyle` + `shadowOpacity` |
| Shadow · Dark | select (sub) | Neutral | Chromatic · Neutral | per-appearance |
| Shadow · Mono | select (sub) | Neutral | Chromatic · Neutral | per-appearance |

### 4c. Composition
| Control | Type | Default | RE param |
|---|---|---|---|
| Visible | switch | on | layer `visible` |
| Layout · X | number (pt) | 0 | transform.tx |
| Layout · Y | number (pt) | 0 | transform.ty |
| Scale | number (%) | 100 | transform.scale |

## 5. Appearances / specializations (the "scope" system)
- Base = **Default**. Overridable per **Dark**, **Mono** (tinted), **Clear** (light/dark).
- Each glass/color/shadow property can carry a per-appearance override (the indented Dark/Mono
  sub-rows). Encoded as `*-specializations` in `icon.json`. **(confirm the exact appearance set: Default,
  Dark, TintedLight/Mono, ClearLight, ClearDark)**

## 6. Document-level (not in the per-layer inspector — confirm)
- Target platforms/idioms (iOS, macOS, watchOS?), background presets (the 6 sine-* backgrounds
  shipped in IconComposerKit), export (`.icon` bundle, PNG render via `ictool --export-image`).

## 7. CONFIRMED serialized `.icon` schema (from real bundles + RE)
From Discord.icon / ImHex.icon `icon.json` and IconComposerKit strings:

**Document root**
- `fill`: `{ "linear-gradient": ["display-p3:r,g,b,a", …], "orientation": {start:{x,y}, stop:{x,y}} }` OR `{ "solid": "srgb:r,g,b,a" }`
- `supported-platforms`: `{ "squares": "shared" | [...], "circles": ["watchOS", …] }`
- `groups`: `[ { "layers": [...], "shadow"?, "translucency"? } ]`

**Layer**
- `name`: string · `image-name`: `"<file>.svg"`
- `glass`: bool (is this layer a Liquid Glass layer)
- `hidden`: bool
- `fill`: solid/gradient (as above)
- `fill-specializations`: `[ { "appearance": "<appearance>", "value": <fill> | "none" } ]` — `"none"` hides the layer in that appearance; an entry with no `appearance` is the base override.

**Group / layer material**
- `shadow`: `{ "kind": "neutral" | "chromatic", "opacity": 0..1 }`
- `translucency`: `{ "enabled": bool, "value": 0..1 }`
- (specular / refraction / blur keys exist per RE but are omitted when default)

**Appearance enum (RE-confirmed, IconComposerKit):**
`default`, `dark`, `tinted-light` (TintedLight), `tinted-dark` (TintedDark),
`clear-light` (ClearLight), `clear-dark` (ClearDark).

**Confirmed enums:** shadow.kind = `neutral | chromatic`; specular = `Inside | Outside`;
mode = `Automatic | Individual | Combined`.

**BlendMode (RE-CONFIRMED, serialized camelCase):** normal, multiply, screen, overlay, darken,
lighten, colorDodge, plusLighter, plusDarker, softLight, hardLight, difference, exclusion, hue,
saturation, color, luminosity. **Glass Mode:** Automatic, Individual, Combined (no "Off").

### Still to pin from RE (nice-to-have, not blocking)
- **SpecularPlacement** raw ints and any 3rd case.
- Serialized key names for specular/refraction/blur (glass sub-object shape).
