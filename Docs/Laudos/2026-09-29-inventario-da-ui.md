# Inventário da UI — cada elemento da janela do Icon Composer 27.0-129

*29/09/2026. Base para a casca Tauri (`ui/`). Cada linha junta quatro fontes:
o **texto** que a Apple escreve (strings do binário), o **tipo** Swift do
`IconComposerKit` que desenha o elemento (metadata `__swift5_fieldmd`), o
**ícone** (SF Symbol ou asset do `.car`) e o que o **nosso núcleo** já faz.*

Selos: `[BIN]` lido do binário · `[ART]` visto nas capturas do app real
(`Contacts.icon`, rendições Default/Dark/Mono) · `[INF]` inferido.

Os dumps brutos ficaram no scratchpad da sessão (`ui-inv/`): `ui_strings.tsv`
(828 strings com função e região), `kit_fields.txt` (285 tipos com campos),
os ícones extraídos do `.car` e a lista de SF Symbols cruzada com o catálogo.

**Estado no núcleo:** ✅ faz · 🟡 parcial · ❌ não faz · ❓ não medido.

---

## 0. Três fatos que mudam o desenho

1. **Não há paleta própria.** O `Assets.car` do Kit tem **zero** cores
   nomeadas, e a seleção usa `Color.accentColor` / `controlAccentColor` `[BIN]`.
   A cara do app é a dos controles padrão do macOS, e a UI Tauri imita esses
   controles, não uma paleta.
2. **O vocabulário de aparência da UI é Default / Dark / Mono** `[ART]`, e o
   binário confirma "Mono" ("The background layer cannot be edited in Mono
   mode", `[BIN]`). As seis rendições internas (`lightColor, darkColor,
   lightTint, darkTint, lightClear, darkClear`, `[BIN]`) aparecem na barra como
   três miniaturas, e o render Mono é o vidro claro (o caminho Clear, ainda ❌
   no nosso render).
3. **O escopo do inspetor segue a rendição selecionada** `[ART]`. Na mesma
   camada, a opacidade é 65 % em Default e 100 % em Dark, e o Fill é Automatic
   em Default e Solid em Dark/Mono. O seletor no cabeçalho de "Color" mostra a
   rendição corrente. "Liquid Glass" e "Composition" ficam em "All".

---

## 1. Janela e barra de título

| elemento | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| janela principal | — | `IconDocumentView` (24 campos: `_navigationSplitViewVisibility`, `_showInspector`, `_inspectorPane`, `_showGrid`, `_gridStyle`, `__zoomLevel`, `_showingExportSheet`, `_showingCustomizeSizesSheet`) `[BIN]` | — | — |
| mostrar/ocultar a barra lateral | "Show Sidebar" `[BIN]` | `_navigationSplitViewVisibility` `[BIN]` | `sidebar.left` `[BIN]` | — |
| larguras das colunas | — | `WindowLayoutConstants.{Window,Canvas,Sidebar,Inspector}` `[BIN]` (valores no laudo de 19/09) | — | ✅ medido |
| título do documento | nome do arquivo (runtime) | `_documentDisplayName` `[BIN]` | — | ✅ |

## 2. Barra de ferramentas

| elemento | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| ⊘ / 26 / 27 | "Choose design generation or disable Liquid Glass effects", "Design Generation 26", "Design Generation 27", "Liquid Glass Effects Disabled" `[BIN]` | `EffectsRenderModePicker` sobre `EffectsRenderMode {disabled, designGeneration26, designGeneration27}` `[BIN]`; aviso `DesignGenerationChangeToast` ("Re-enabled effects for …") | `slash.circle`, `26.circle`, `27.circle` `[BIN]` | ❓ (o render hoje é uma geração só; qual, não medido) |
| par de amostras claro/escuro | "Choose background", "Solid Color Background", "Image Background", "Add Background…" `[BIN]` | `BackgroundChooser` + 2× `BackgroundKindPickerButton{isSelected, roundedEdge, popoverContent}`; `BackgroundColorPopoverContent`, `BackgroundImagePopoverContent`, `BackgroundImageManager{Scope: user, system}` `[BIN]` | círculos desenhados, não símbolo `[INF]` | ❌ (fundo do canvas não é escolhível) |
| fundos embutidos | "1 - sine-purple-orange" … "6 - sine-gray" `[BIN]` | 6 JPEG 8192² em `Resources/Backgrounds` `[BIN]` | — | ❌ |
| grade + seta | "Show or hide grid", "Grid", "Grid Style", "Light", "Dark" `[BIN]` | `AppIconGridToolbarItemView{_showGrid, _gridStyle}`, `AppIconGridStyle {light, dark}` `[BIN]` | `toolbar-grid-on/off` (asset), `chevron.down`; grade `appicongrid.square/.circle` `[BIN]` | ❌ |
| "Full size" | "Full size", "Full size (%g%@ pt @1x)", "Select preview size", "Customize…" `[BIN]` | `IconPreviewDimensionsSettings`, `RasterSize{size, scale}`, `PreviewSizeCustomization{customSizes, deletedBuiltinSizes}` `[BIN]` | `chevron.down` | 🟡 (512/1024 fixos) |
| "100%" | "Change zoom level", "Zoom In", "Zoom Out" `[BIN]`; "%" literal | `ZoomableView{_zoomLevel, zoomLevelRange: ClosedRange<Double>}` — contínuo, não lista `[BIN]` | `plus/minus.magnifyingglass` (menu) `[BIN]` | 🟡 (zoom existe; não vetorial em tempo real) |
| abas do inspetor | "Show or hide style options", "Show or hide document options", "Style Inspector ⌘1", "Document Settings Inspector ⌘2" `[BIN]` | `InspectorPane {content, document}` `[BIN]` | `paintbrush`, `document` `[BIN]` (não é martelo) | ✅ (dois painéis) |

## 3. Barra lateral (camadas)

| elemento | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| lista | — | `LayerSidebar` → `LayerList` → `LayerOutlineView` (`NSOutlineView`) → `Coordinator.MemberCellView` `[BIN]` | — | ✅ |
| linha do documento | nome (runtime) | `IconRow{documentDisplayName}` `[BIN]` | `square` `[BIN]` | ✅ |
| grupo | "Group" (nome padrão) `[BIN]` | `GroupRow{_group}` `[BIN]` | `folder` + disclosure | ✅ |
| camada | "Layer" (nome padrão) `[BIN]` | `LayerRow{_layer}`; `Row{name, editableName, hiddenBinding, hiddenOrHasHiddenAncestor, icon, _isHovering}` `[BIN]` | miniatura sobre xadrez (`Checkerboard` `[INF]`) | ✅ (sem miniatura) |
| olho de oculto | "Toggles layer visibility", "Show Layer", "Hidden" `[BIN]` | `hiddenBinding` | `eye.slash` `[BIN]` | ✅ |
| seleção | — | `IconMemberSelection`, `SelectableIconMember {group, layer, icon}`, `HighlightStyle {selected, layerInSelectedGroup, hovered}` `[BIN]` | accent do sistema | ✅ (simples; sem multi) |
| + / − | "Opens menu to add new group or image layer", "Removes selected layers from the icon", "Add Layer", "New Image…", "Delete Layer" `[BIN]` | `LargeHitAreaButtonStyle` `[BIN]` | `plus`, `minus` | ✅ |

## 4. Canvas

| elemento | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| rolagem e zoom | "Icon Canvas" `[BIN]` | `ZoomableView` (`NSScrollView`), `ClipView.shouldUnconditionallyCenterIcon` `[BIN]` | — | 🟡 |
| edição | "Snap to Guides" `[BIN]` | `EditableIconCompositionCanvas` (`_snapToGuides`, `__dragState`, `gridColor`, `selectedMemberColor`), `DragState.Operation {selection, move}`, `LayoutGuide`, `RulerTicksView`, `ConstrainedDragGesture` `[BIN]` | — | 🟡 (clique seleciona; sem arrastar/guias) |
| camadas de desenho | — | `CanvasLayer` (`CALayer`) com `chicletLayer: ICRIconLayer` e `simulatedGlassLayer` `[BIN]` | — | ✅ (render próprio) |
| gradiente no canvas | "Flip Gradient Colors" `[BIN]` | `GradientPlacementView` + `Knob` `[BIN]` | — | ❌ |

## 5. Barra de rendições (rodapé do canvas)

| elemento | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| barra | "Previews icon for …" `[BIN]` | `RenditionBar{preferredPreviewSize, _renditionHoverTarget, _platformHoverTarget, iconSize, iconPadding, _recentRenditions, _showTintedOptionsPopover}`, `RenditionTitleButton`, `RenditionIconButton` `[BIN]` | — | ✅ |
| grupo esquerdo "iOS, macOS" | "iOS", "macOS", "No Platform Selected" `[BIN]` | `SupportedPlatforms.Squares {unique, shared}` `[BIN]` | miniaturas | ✅ |
| grupo direito Default/Dark/Mono | "Default", "Dark", "Tinted", "Clear Light", "Clear Dark", "Tinted Light", "Tinted Dark" `[BIN]`; legenda "Mono" `[ART]` | `Rendition {lightColor, darkColor, lightTint, darkTint, lightClear, darkClear}` `[BIN]` | miniaturas | 🟡 (Mono/Clear não desenha) |
| popover de tinta | "Options…", "Opens tint options", "Tint color", "Tint intensity", "Enable or disable tinted appearance" `[BIN]` | `tintSpectrumPosition`, `tintAlpha`, `InspectedTintMode {lightTint, darkTint, lightClear, darkClear}` `[BIN]` | — | ❌ |

## 6. Inspetor — aba de estilo (camada selecionada)

Container `IconCompositionInspector` com `_appearanceFocusSetting`,
`_liquidGlassFocusSetting`, `_compositionFocusSetting`,
`_localizationFocusSetting` `[BIN]`. Cada seção é `InspectorSection{title,
primarySlotComponent}`; o seletor do cabeçalho é a máquina de variação
(`SpecializablePropertyInspector`, `SpecializationSlice`,
`EnumeratedSpecializationSlice`; "Opens menu to choose variation type",
"Remove Variation", "Vary for", "Multiple values", "Reset value", "Not
Applicable") `[BIN]`. Arrastar número: `Scrubbable` `[BIN]`.

| seção / linha | texto | tipo | ícone | núcleo |
|---|---|---|---|---|
| **Color** (escopo: rendição) | "Color" `[BIN]` | — | — | ✅ (escrita por aparência) |
| Opacity | "Opacity" `[BIN]` | `OpacityInspector` | `Opacity.pdf` (asset) | ✅ |
| Blend Mode | "Blend Mode", "Normal", "Multiply", "Overlay", "Plus Lighter"… `[BIN]` | `BlendModeInspector{context: {layers, groups}, __previewBlendMode}`; `BlendMode` com 10 casos `[BIN]` | `Blendmode.pdf` (asset) | 🟡 (modos transcritos: parte) |
| Fill | "Fill", "None", "Automatic", "Solid", "Gradient", "System Light", "System Dark", "Use Automatic Color", "Color Options", "Standard", "Recent" `[BIN]` | `FillInspector{supportsGradientOrientation, supportsOpacity}`, `IconColorPicker` (15 campos), `RecentColorsManager`, `ColorPickerGrid`; "Opens system color picker", "Opens color preset menu" `[BIN]` | `custom.image.fill` | 🟡 (valores sim; painel de cor não) |
| **Liquid Glass** (escopo: All) | "Liquid Glass" `[BIN]` | — | — | ✅ |
| Effects | "Effects", "Enable or disable glass effects on this layer" `[BIN]` | `CopyablePropertyType.isGlass` `[BIN]` | `custom.fx.circle` | ✅ |
| **Composition** (escopo: All) | "Composition" `[BIN]` | — | — | ✅ |
| Visible | "Visible", "Toggle visibility" `[BIN]` | `IsHiddenInspector` `[BIN]` | `eye` | ✅ |
| Image | "Image", "Replace…", "Copy Image", "Localize…" `[BIN]` | `ImageAssetInspector`, `SliceContent`, `AssetView` `[BIN]` | `photo` | ✅ (sem Localize) |
| Layout | "Layout", "x", "y", "pt" `[BIN]` | `GeometryInspector` sobre `Position{translationInPoints, scale}`, `MultiNumericTextField`, `InspectorTextFieldStyle{inlineLabel, unit}` `[BIN]` | `arrow.up.left.and.down.right.and.arrow.up.right.and.down.left`; escala `arrow.up.left.and.arrow.down.right` `[BIN]` | ✅ |

**Com grupo selecionado ou vidro ligado** (não visível nas capturas) `[BIN]`:

| linha | tipo | núcleo |
|---|---|---|
| Mode (Combined / Individual) | `GroupEffectsInspector` | 🟡 (só `lighting`) |
| Specular (Off / Automatic / Inside / Outside) | `GroupSpecularInspector`, `LayerSpecularInspector` | ✅ |
| Blur | `BlurMaterialInspector` | ✅ |
| Translucency | `TranslucencyInspector` | ✅ |
| Shadow (Neutral / Chromatic) | `ShadowInspector`, `Shadow.Kind {automatic, neutral, layerColor, none}` | ✅ |
| Refractivity (Strength, Height) | `RefractivityInspector` + `RefractivityGridView` (grade 2D) | 🟡 (valor sim; grade não) |
| espelhamento | `AssetMirroringInspector`, `InspectorValue {inherited, fixed, mirror}` | ✅ |
| seleção mista | `MultiPicker`, `MultiToggle` ("Currently some are on and some are off") | ❌ |

## 7. Inspetor — aba do documento

| linha | texto | tipo | núcleo |
|---|---|---|---|
| fundo | "The background layer cannot be edited in Mono mode" `[BIN]` | `IconBackgroundInspector` | ✅ |
| plataformas | "Platforms", "iOS Only", "macOS Only", "Shared", "Unique" `[BIN]` | `DocumentSettingsInspector`, `InspectorChoices {unified, independent, iOSOnly, macOSOnly, off}` | ✅ |
| cores de SVG | "SVG Colors", "Use Display P3 if untagged" `[BIN]` | `AssumedSVGColorSpace` | ✅ |
| espelhar | "Mirror Assets in Right to Left", "Automatically mirror assets for right-to-left languages" `[BIN]` | `IconAssetMirroringInspector` | ✅ |
| idiomas | "Languages", "Add Language", "Filter Languages" `[BIN]` | `SupportedLanguagesList`, `AddLanguageSheet`, `RemapLanguageSheet`, `AssetLocalizationSheet`, `LocalizationMenu` | ❌ |

## 8. Menus `[BIN]`

- **File:** Export…, Copy Icon as Image.
- **Edit:** Redo, Cut / Copy / Paste / Duplicate / Delete, Copy / Paste Style, Select All / Deselect All, Select Parent.
- **View:** Show Sidebar, Inspectors (Style ⌘1, Document ⌘2), Zoom In / Out, Snap to Guides, Hide / Show Grid, Customize Toolbar….
- **Arrange:** Bring Forward / to Front, Send Backward / to Back, Align (6), Distribute (4) (`ArrangeCommandHandler`).
- **Help / app:** About Icon Composer, Copy Build Info, Icon Composer Help, What's New.

## 9. Folhas `[BIN]`

- **Export Icon as Image:** Platform / Appearance / Localization, macOS pre-Tahoe, Use legacy metrics, Glass Chiclet, Export… / Export HDR…, Cancel (`ExportSheet`, `ExportOptions`, `PlatformSelection {one, all, preTahoe}`, `ExportSize {fixed, hero}`). Núcleo: 🟡.
- **Customize Point Sizes:** Reset to Defaults, Done (`CustomizeSizesSheet`). Núcleo: ❌.
- **Add Language / Move Localization.** Núcleo: ❌.
- **What's New:** Refraction, New Specular Highlights, Extended Preview, Continue. Fora de escopo.

## 10. O que ficou sem resposta

- ~500 literais curtos não decodificados (~18 %); alguns rótulos saíram
  truncados (Refraction; Left-to-Right / Right-to-Left).
- As larguras das colunas saem de `navigationSplitViewColumnWidth` em código;
  os valores estão no laudo de 19/09, não neste.
- Qual geração de design (26 ou 27) o nosso render reproduz: não medido.
- O fundo bege das capturas não é nenhum dos 6 embutidos: é imagem do usuário.
