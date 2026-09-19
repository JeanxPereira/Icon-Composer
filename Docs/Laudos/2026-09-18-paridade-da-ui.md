# Laudo — paridade da UI contra o Icon Composer 27.0-129 (18/09/2026)

Laudo de **inventário e paridade**, não de comportamento de render. A pergunta é
a do usuário: o que o editor original faz, o que este editor faz, e onde os dois
não se encontram.

A régua não é memória nem o material sem selo de `_confrontar/`: é a metadata
Swift do slice `IconComposerKit.arm64` da extração 27.0-129, lida aqui.

---

## 1. Como a régua foi lida

`[BIN]` Os nomes de tipo do `IconComposerKit` estão no binário em forma
manglada. O prefixo de módulo é `15IconComposerKit` (o comprimento precede o
nome), e o que segue é uma cadeia de componentes igualmente prefixados por
comprimento. Varrendo todas as ocorrências desse prefixo e lendo a cadeia:

- **180 nomes qualificados**, dos quais **168 tipos de topo** e **31 aninhados**.
- A spec de 13/09 §2.1 diz "306 tipos de view com nome". O número menor aqui não
  contradiz: aquela contagem veio de `swift_meta.py --fields` sobre os
  descriptors de campo, que inclui tipos sem nome mangled alcançável por este
  caminho, e conta views de SwiftUI genéricas instanciadas. **168 é o que este
  método vê; é um piso, não um teto.** `[OBS]`

Os nove nomes que a spec de 13/09 citou estão todos presentes e foram
confirmados por contagem direta de substring, independente do desmanglador:
`LayerSidebar` (35), `ExportSheet` (17), `SpecializablePropertyInspector` (33),
`AssetMirroring` (44), `DocumentSettings` (38), `LocalizationMenu` (56),
`ColorPickerGrid` (25), `ArrangeCommandHandler` (14), `PreviewDimensionsSettings`
(3). `[BIN]`

---

## 2. Os 16 inspetores — onde estamos

O alvo organiza todo inspetor sobre `SpecializablePropertyInspector`. Nós também
(`InspectorSection.h`, `Section::begin`). A tabela é nome do alvo × seção nossa.

| inspetor do alvo | nossa seção | estado |
|---|---|---|
| `IsHiddenInspector` | "Visible" | **tem** |
| `OpacityInspector` | "Opacity" | **tem** |
| `BlendModeInspector` | "Blend Mode" | **tem** |
| `GeometryInspector` | "Geometry" | **tem** |
| `FillInspector` | "Fill" | **tem** (solid, linear-gradient, automatic; sem o painel de cor do alvo) |
| `ShadowInspector` | "Shadow" | **tem** |
| `TranslucencyInspector` | "Translucency" | **tem** |
| `BlurMaterialInspector` | "Blur Material" | **tem** |
| `LayerSpecularInspector` | "Specular" | **tem** |
| `GroupSpecularInspector` | "Specular" | **tem** (uma seção serve os dois níveis) |
| `RefractivityInspector` | "Refractivity" | **tem o valor**, falta o `RefractivityGrid` — o alvo tem um editor 2D (`RefractivityGrid`, `RefractivityGridView`) |
| `ImageAssetInspector` | "Image Asset" | **tem**, com uma falta nomeada em §4 |
| `GroupEffectsInspector` | "Lighting" | **parcial** — cobrimos `lighting`; o alvo tem também `EffectsRenderModePicker` e `EffectsTarget` |
| `DocumentSettingsInspector` | painel Document | **parcial** — temos `supported-platforms`, `color-space-for-untagged-svg-colors` e `features`; o alvo tem ainda `PreviewDimensionsSettings`/`CustomizeSizesSheet` |
| `AssetMirroringInspector` | — | **FALTA** |
| (fundo do documento) `BackgroundChooser` e família | seção "Fill" na raiz | **parcial** — §3 |

`asset-mirroring` é chave **conhecida** pelo nosso modelo nos dois níveis
(`Coverage.cpp`, tabelas de grupo e de camada) e `implicit-asset-mirroring` no
documento. O que falta é controle, não modelo. A recusa atual está escrita em
`PanelInspectorAsset.cpp` e é honesta: zero ocorrências nos 145 documentos, então
nem a grafia da chave nem a gramática do valor foram observadas. **Um controle
aqui escreveria valor não visto sob chave não lida.** A paridade real desta
linha depende de medição no binário, não de decidir a UI.

---

## 3. As frentes inteiras que o alvo tem e nós não

Agrupadas pelos tipos que as nomeiam. Nenhuma delas é "quase pronta".

**Exportação** — `ExportSheet`, `ExportOptions`, `ExportOptionsView`,
`ExportSize`, `ExportableImage`, `ResolvedExportOptions`, `ExportableLicenseAgreement`.
Nós: um item de menu desabilitado com "Round 5: not built yet". O `icrender`
grava PNG pela linha de comando, então o motor existe; o que falta é a folha de
opções e o caminho pela UI.

**Localização** — `LocalizationMenu`, `AddLanguageSheet`, `RemapLanguageSheet`,
`AssetLocalizationSheet`, `SupportedLanguagesList`, `DisplayableLanguage`,
`Localization`. Nós: item de menu desabilitado. O modelo já conhece
`specializable-language-ids` no documento e `localization`/`language-direction`
como predicados de especialização (`Coverage.cpp`), então o leitor lê o que a
UI não edita.

**Painel de cor** — `ColorPanelCoordinator`, `ColorPanelTarget`, `ColorPickerGrid`,
`ColorSlider`, `ColorChip`, `RecentColorsManager`. Nós: `ColorEdit4` do ImGui e
campos numéricos. Funciona; não é o painel do alvo.

**Fundo do documento** — `BackgroundChooser`, `BackgroundKindPickerButton`,
`BackgroundColorPopoverContent`, `BackgroundImagePopoverContent`,
`BackgroundImageManager`, `Checkerboard`, `CheckerboardShape`. Nós: a seção
"Fill" na raiz cobre cor e gradiente; **imagem de fundo e xadrez de transparência
não existem**.

**Área de transferência de propriedades** — `Pasteboard`, `PasteboardGroup`,
`PasteboardLayer`, `PasteboardMember`, `PasteboardMemberItem`,
`PasteboardMemberItemDragSource`, `PasteboardMembers`, `PasteboardObserver`,
`PasteboardPropertySet`, `PasteboardCommandHandler(s)`,
`PropertiesPasteboardCommandHandler`, `InspectorCopyablePropertyTypeKey`,
`MenuContent.PasteboardSection`. Nós: Copy/Paste Properties desabilitados.

**Canvas editável** — `EditableIconCompositionCanvas`, `ConstrainedDrag`,
`ConstrainedDragGesture`, `ConstraintState`, `LayoutGuide`, `LayoutGuideView`,
`RulerTicks`, `RulerTicksView`, `SelectionIndicatorRectangle`,
`StableAnchorPointView`, `Zoomable`/`ZoomableView`, `CanvasSelectionPresentation`.
Nós: o canvas navega (zoom ancorado, pan, Fit, ladrilho) e **não edita** — não há
clique de seleção, arrasto de camada, guias nem réguas.

**Grade de renditions** — `AppIconGrid`, `Rendition`, `RenditionBar`,
`RenditionTitle`, `RenditionTitleButton`, `PreviewProvider`,
`PreviewProviderRenderer`, `ThumbnailProviderRenderer`. Nós: um contexto por vez,
escolhido em dois combos. **Não há a grade de todas as aparências.**

**Multi-seleção** — `MultiPicker`, `MultiToggle`, `MultiNumericTextField`,
`AllVariantsContextMenu`. Nós: seleção simples, por decisão registrada da spec
de 13/09 §10.

**Fatias de especialização** — `SpecializationSlice`,
`EnumeratedSpecializationSlice`, `SliceContext`, `SliceContextMenu`, `SliceDrop`,
`SliceDropDestinationModifier`. Nós: o seletor de escopo no cabeçalho de cada
seção faz o essencial (ler, escrever, remover override) e não tem menu de
contexto nem arrastar-e-soltar entre fatias.

**Tamanhos de preview customizados** — `CustomizeSizesSheet`,
`PreviewDimensionsSettings`. Esta é a **feature nova do 27.0-129** que o laudo de
15/09 §3.4 achou no `fieldmd` do Foundation (`PreviewSizeCustomization`,
`AllPreviewSizeCustomizations`). Nós: dois tamanhos fixos, 512 e 1024.

**Fora de escopo por natureza** — `EULAScene`, `LicenseAgreement*`,
`WhatsNewView`, `AppDelegate`, `ResourceBundleClass`, os três
`*Workaround*` (contornos de bug do AppKit), `ThumbnailProviderRenderer`
(extensão do Finder).

---

## 4. Defeitos medidos nesta passagem, não inferidos

Estes não são "falta feature": são coisas que **estão na tela e não fazem o que
a tela promete**. Todos no caminho que o usuário descreveu como "a parte de
layers e até opções".

### 4.1. A barra do Onyx oferece três itens que não servem a este app

A barra nativa do Onyx fica **acima** da nossa e tem a cara de menu principal.
Medido em `OnyxSDK/Source/App/App.cpp` no checkout `dddce38`:

- **`File > Close All`** (`:397`) fecha documentos do Workspace do Onyx e chama
  `m_documentWindow.CloseAll()`. Nós não registramos nenhum documento no
  Workspace e acabamos de esconder o `DocumentWindow`. **O item não faz nada**, e
  parece o "fechar" do app.
- **`File > Recent Files`** (`:411`) lista os recentes do Onyx, rotulados com
  dica de jogo (`GOW1`, `GOW2`, `GOWR`), e `openRecentFile` entra pelo Workspace.
  Para nós é uma lista vazia ou, pior, arquivos de outro app.
- **`File > Exit`** (`:451`) chama `exit(0)` direto. **Isso pula toda a nossa
  descida**: não passa por `State::close()`, não espera o `JobScheduler`, não dá
  `vkDeviceWaitIdle`. Um `exit(0)` com um render em voo numa thread de trabalho
  é exatamente o desligamento que o `~JobScheduler` existe para impedir.
- **`Export`** (`:456`) tem três itens permanentemente desabilitados — glTF, DDS
  e Copy Hash — e o comentário do próprio Onyx diz que nunca tiveram corpo. Num
  editor de ícone, um "Export glTF..." cinza é ruído que contradiz o nosso
  `File > Export Icon as Image…`.

`Options > Settings...` e `View` (alternar painéis, Reset Layout) **funcionam**.
O comentário do Onyx em `:488` afirma que nenhum chamador define
`m_defaultLayout`; nós definimos (`Window.cpp`, `SetDefaultLayout`), então o
Reset Layout reconstrói o nosso layout. O comentário está desatualizado, o
código está certo.

### 4.2. Erro de escrita não chega na tela

`State::act()` manda o resultado de `save`, `saveAs` e da criação de documento
para `stderr`. Uma janela não tem stderr. `State::trouble` existe e só é
desenhado no canvas **vazio** — isto é, some no instante em que há documento, que
é o único instante em que salvar pode falhar.

### 4.3. Importar asset só aceita caminho digitado

`PanelInspectorAsset.cpp` tem um campo de texto para o caminho e nenhum botão de
procurar, porque o Kit não pode abrir diálogo (Regra 2) e `MenuActions` não tem
um canal para pedir um. A recusa está escrita e é honesta — mas o canal é barato:
`open`, `save` e `saveAs` já são exatamente isso.

### 4.4. Uma camada nova nasce sem arte e sem caminho óbvio até ela

`Add Image Layer` (menu `Layer` e botão `+`) cria a camada com `image-name`
vazio. A seção Image Asset diz "no image: this layer draws nothing", que é
verdade, e a saída é ou escolher da combo (se `Assets/` já tiver o arquivo) ou
digitar um caminho absoluto. Com `Assets/` vazio, as duas portas estão fechadas
para quem não sabe do campo de texto.

---

## 5. O que está inteiro e vale dizer

Para não ler a lista acima como "nada funciona":

- **Árvore de camadas**: grupos e camadas, disclosure, renomear por duplo clique
  e por F2/Enter, visibilidade, vidro, adicionar, remover, mover por menu de
  contexto, arrastar-e-soltar com o custo exato em passos, navegação inteira por
  teclado (setas, Ctrl+setas para mover, Delete/Backspace), rolar até a seleção
  que veio de fora, e estado vazio que diz o que fazer.
- **Escrita sob escopo**: a operação central da spec de 13/09 §4.3, com os quatro
  casos, e o invariante "chave simples e lista nunca coexistem" varrido sobre o
  corpus.
- **Undo/redo** por comando com coalescência, e `isDirty` no título.
- **Round-trip byte-exato** em 135 documentos do corpus.
- **Canvas**: zoom ancorado no cursor, pan com limite, Fit, ladrilho em
  resolução de tela com invariante de tolerância zero.
- **Diagnóstico**: `skipped`, `shapeGaps`, `notes`, assets faltando, chaves
  desconhecidas, relógio do render.

---

## 5.1. Adendo de 19/09 — a exportação nomeia SEIS renditions, e nós temos quatro aparências

`[BIN]` Junto dos tipos de exportação, o binário carrega uma tabela compacta
de nomes (blocos de 8 bytes, o resto em continuação logo abaixo — a forma
que o Swift usa para nomes de caso de enum):

```
ExportableImage  … iOS   macOS   watchOS   iOS 
… Default  Dark  TintedLi TintedDa ClearLig ClearDar … ght  rk  ht  k 
```

Remontado: as plataformas são **iOS, macOS, watchOS** (com `iOS` repetido
logo depois, provavelmente o padrão), e as renditions são **seis**:

| rendition | nosso `icf::Appearance` |
|---|---|
| `Default` | `Base` / `Light` |
| `Dark` | `Dark` |
| `TintedLight` | `Tinted`, metade clara |
| `TintedDark` | `Tinted`, metade escura |
| `ClearLight` | **não existe no nosso modelo** |
| `ClearDark` | **não existe no nosso modelo** |

Isto não contradiz o doc 01 §6, que mediu **quatro** valores para o predicado
`appearance` no documento. São dois eixos diferentes: o documento especializa
por quatro aparências; a UI **previsualiza e exporta** seis renditions, e as
duas últimas nomeiam um modo — "Clear" — que o nosso modelo não tem nome para.
`Tinted` também se divide em claro e escuro na saída, o que o predicado do
documento não faz.

**O que isso custa em paridade:** o combo de aparência deste editor oferece
quatro entradas e o alvo oferece seis, então há dois estados do ícone que
este editor não consegue mostrar de jeito nenhum. Antes de acrescentar as
entradas, falta medir o que `Clear` faz no render — é pergunta para o
binário, não para a UI, e é `[OBS]` até lá.

---

## 6. O que este laudo NÃO fez

- Não desmontou nenhum dos 168 tipos: o inventário é de **nome**, e nome dá
  existência, não comportamento. Tudo que este laudo afirma sobre o que uma tela
  do alvo *faz* vem do nome dela e está marcado como tal. `[OBS]`
- Não mediu `WindowLayoutConstants` (segue `[OBS]` desde 13/09 §2.1).
- Não abriu `AssetMirroringInspector` para descobrir a grafia da chave e a
  gramática do valor — que é exatamente o que destravaria §2.
- Não contou quantos dos 168 tipos são views de fato contra tipos de apoio.
