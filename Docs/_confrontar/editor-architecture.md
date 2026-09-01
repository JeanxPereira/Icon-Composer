# Icon Composer — editor architecture & logic

How the macOS 27 Icon Composer editor is put together: the layers of code, the document model, the
edit→bake→render→export data flow, undo, and selection. Reconstructed from binary symbols
(`strings` over the app + frameworks) and the reconstructed shaders. Symbol-level; where a name is
inferred it is marked *(inferred)*.

## 1. Layering (who depends on whom)

```mermaid
graph TD
    App["Icon Composer.app<br/><i>SwiftUI shell, menus, windows</i>"]
    Kit["IconComposerKit<br/><i>editor: UI, inspectors, canvas, commands</i>"]
    Found["IconComposerFoundation<br/><i>document model, Composition, serialization</i>"]
    CoreSVG["CoreSVG<br/><i>SVG parse/raster</i>"]
    IR["IconRendering<br/><i>layer-stack baker → FinalizedIcon</i>"]
    RB["RenderBox<br/><i>2D GPU engine (stitched shaders)</i>"]
    Metal["Metal"]

    App --> Kit
    Kit --> Found
    Kit --> IR
    Kit --> CoreSVG
    Found --> CoreSVG
    Found --> IR
    IR --> RB
    RB --> Metal

    ICT["ictool<br/><i>.icon → asset catalog</i>"] --> Found
    ICR["icrtool<br/><i>.icon → PNG</i>"] --> IR
    QL["QuickLook / Thumbnail appex"] --> IR
```

- **App shell** is thin (~204 KB): SwiftUI `App`, `WindowGroup`, `Commands`, document type binding.
- **IconComposerKit** (6.5 MB) holds the entire editor: the inspector panels, the layer sidebar, the
  composition canvas, command handlers, the color panel, and the export sheet.
- **IconComposerFoundation** (3.6 MB) is the model: the `Composition` object graph, the
  `SpecializableProperty` mechanism, `.icon` (de)serialization, and the headless `ToolCommand`s that
  `ictool`/`icrtool` reuse.
- **IconRendering** turns a composition into a baked `FinalizedIcon`; **RenderBox** is the GPU engine
  it renders through (see [`kernels/icon-glass.md`](kernels/icon-glass.md)).

## 2. Document model

`.icon` on disk is a package (`NSFileWrapper`). In memory it is an `IconComposerDocument` whose
payload is a `Composition` object graph.

```mermaid
classDiagram
    class IconComposerDocument {
        +Composition composition
        +FileWrapper read/write  (.icon package)
        +NSUndoManager undoManager
    }
    class Composition {
        +Fill fill            // chiclet background
        +Group[] groups
        +AssetStore assets    // imported SVG/PNG by name
        +SupportedPlatforms supportedPlatforms
        +snapshot() Snapshot
    }
    class Group {
        +Layer[] layers
        +Shadow shadow           // kind: neutral, opacity
        +Translucency translucency
        +SpecializableProperty~Position~ position
    }
    class Layer {
        +String name
        +String imageName        // → AssetStore
        +Bool glass              // Liquid Glass toggle
        +SpecializableProperty~Fill~ fill
        +SpecializableProperty~Double~ opacity
        +SpecializableProperty~BlendMode~ blendMode
        +SpecializableProperty~Bool~ hidden
        +SpecializableProperty~Position~ position
        +Specular / Lighting     // written only when customized
    }
    class SpecializableProperty~T~ {
        +T base                          // the "root" appearance value
        +[Specialization~T~] overrides   // per appearance / idiom
    }
    class AssetStore {
        +[String: Asset] byName
        +Snapshot
    }
    IconComposerDocument --> Composition
    Composition --> Group
    Composition --> AssetStore
    Group --> Layer
    Layer --> SpecializableProperty
    Group --> SpecializableProperty
```

### `SpecializableProperty` — the base+override mechanism

Every visual property is a `SpecializableProperty<T>`. It (de)serializes via
`decodeSpecializableProperty(singularKey:pluralKey:)`:

- if only the **singular key** is present (`"fill"`) → a single base value;
- if the **plural key** is present (`"fill-specializations"`) → an array where the entry with **no
  discriminator** is the base and the rest carry `appearance:` or `idiom:` to override.

Appearances form an inheritance tree — a foundation string literally calls the root *"the appearance
all appearances inherit from"*:

```mermaid
graph TD
    root["(root / base = Light)"] --> dark[dark]
    root --> tinted[tinted]
    root --> clear[clear]
    root --> sq[idiom: square]
    root --> ci[idiom: circle · watchOS]
```

Resolving a property for a target appearance = start from `base`, then apply the matching override if
one exists (`value: "none"` disables the property for that appearance).

## 3. Edit → render data flow

```mermaid
graph LR
    SVG["SVG / PNG art"] -->|CoreSVG import| Asset["AssetStore"]
    Inspectors["Inspectors / canvas edits"] -->|mutate| Comp["Composition (model)"]
    Asset --> Comp
    Comp -->|snapshot| Snap["Composition.Snapshot"]
    Snap -->|build layer stack| Bake["IconRendering: bake"]
    Bake -->|FinalizedIcon| RBX["RenderBox GPU passes"]
    RBX -->|MTLTexture| Canvas["Composition canvas (live preview)"]
    RBX -->|raster| Export["Export (PNG / .icon / catalog)"]
```

The model is **snapshot-based**: edits mutate `Composition`; a `Snapshot` (immutable) is handed to
`IconRendering`, which bakes the layer stack (SDF fills + glass + specular + tint per appearance) into
a `FinalizedIcon`, rendered through RenderBox to an `MTLTexture` for the canvas or rasterized for export.

## 4. Bake / render sequence

```mermaid
sequenceDiagram
    participant U as User
    participant I as Inspector/Canvas
    participant C as Composition
    participant UM as UndoManager (coalescer)
    participant R as IconRendering
    participant G as RenderBox/Metal

    U->>I: drag slider / edit value
    I->>C: mutate SpecializableProperty
    C->>UM: registerUndo (coalesced in a run-loop group)
    C-->>R: snapshot() for the active appearance(s)
    R->>R: build layer stack, load SDF textures
    R->>G: stitched passes — sdfFill, distance_gradient,<br/>glassHighlight, glass_background, gradientMask, glow, tint
    G->>G: composite (clampedPlusL / blend)
    G-->>I: MTLTexture → live canvas
    Note over R,G: incremental — only re-bakes layers whose inputs changed
```

Undo edits are **coalesced**: a run-loop observer batches rapid mutations (e.g. a slider drag) into a
single undo group (`undoCoalescingTimer`, `closeCoalescingUndoGroups`,
`ensureNextSequenceOfModificationsIsANewUndoEvent`). Rendering is **incremental** (`Incremental Update`,
texture pool reuse) — only changed layers re-bake.

## 5. Selection & commands

```mermaid
graph TD
    Sel["SelectableMember / MultiMember<br/>(selection model)"] --> CH
    subgraph CH["Command handlers (IconComposerKit)"]
      Arrange["ArrangeCommandHandler<br/>(reorder / group / ungroup)"]
      Paste["PasteboardCommandHandler(s)<br/>(cut/copy/paste, PropertiesPasteboard)"]
      DocCmd["DocumentCommands"]
    end
    subgraph Tool["Headless commands (Foundation)"]
      Mutate["IconMutatingCommand"]
      ExpImg["ExportImageCommand / IcontoolExportImageCommand"]
      ExpIR["ExportIntermediateRepresentationCommand"]
      Help["HelpCommand"]
    end
    CH --> Mutate
    ExpImg --> Tool
```

The same `ToolCommand` / `IconMutatingCommand` layer backs both the GUI and the CLIs — the editor and
`ictool`/`icrtool` share one mutation/serialization path. `PropertiesPasteboardCommandHandler` lets you
copy **properties** (a fill, a specular setup) between layers, not just layers themselves.

## 6. Export

```mermaid
graph LR
    Doc[".icon document"] -->|ictool| Cat["asset catalog / packaged icon"]
    Doc -->|icrtool| PNG["rasterized PNGs (per size/appearance)"]
    Doc -->|GUI: Export Icon as Image| IMG["static PNG for web/review"]
    Doc -->|GUI: Export IR| IRR["intermediate representation"]
```

- **Export Icon as Image** — *"Export static versions of your icon for use on websites, advertising,
  or for review."* Excluded-glass images note: *"composite over an existing glass chiclet using the
  screen blend mode."*
- **ictool** compiles `.icon` → asset-catalog form Xcode consumes; **icrtool** renders `.icon` → PNGs;
  both drive the same `IconRendering` path headlessly (no window/GPU-context assumptions beyond Metal).

See also: [`README.md`](README.md) (bundle map, `.icon` schema) · [`editor-ui.md`](editor-ui.md)
(windows, menus, inspectors) · [`kernels/icon-glass.md`](kernels/icon-glass.md) (shader math).
