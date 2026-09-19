# Laudo — as seis renditions, `asset-mirroring` e `WindowLayoutConstants` (19/09/2026)

Laudo de **medição no binário**, T1 do plano `Docs/Plans/2026-09-19-paridade.md`.
Nenhuma linha de código de produção ou de teste foi escrita nesta passagem.

Material: os slices arm64 de `References/27.0-129/out/slices/`
(`IconComposerFoundation.arm64`, `IconComposerKit.arm64`, `IconRendering.arm64`,
`RenderBox.arm64`) e os `fieldmd_*.txt` já gerados em `References/27.0-129/out/`.
O corpus de 145 documentos (`References/corpus/*/icon.json`) só entra nas
afirmações marcadas `[ART]`.

**Endereços.** Em todos os slices lidos aqui o `__TEXT` tem `vmaddr == fileoff == 0`,
então *endereço = offset de arquivo*. Todo endereço abaixo é do slice nomeado na
seção.

---

## 0. As três perguntas, e o que cada uma virou

| # | pergunta | veredito |
|---|---|---|
| 1 | O que `Clear` é, e onde entra no pipeline | **respondida** — é um dos três **modos de render**, não uma aparência do documento |
| 2 | Grafia e gramática de `asset-mirroring` / `implicit-asset-mirroring` | **respondida nas duas metades** |
| 3 | `WindowLayoutConstants` | **respondida** — nove constantes, com valor |

---

## 1. Método comum: três ferramentas que faltavam

As três perguntas caíram porque três leituras novas ficaram disponíveis nesta
passagem, e vale registrá-las porque valem para a próxima:

1. **A trie de exportação do dyld** (`LC_DYLD_INFO`/`LC_DYLD_EXPORTS_TRIE`) dá
   **nome mangled → endereço** mesmo quando `LC_SYMTAB` não dá. O
   `IconComposerFoundation.arm64` tem 1.951 símbolos no symtab (quase só helpers)
   e **2.956 exportações na trie**, com os acessores Swift todos lá. O
   `IconComposerKit.arm64` tem 605. É o que transformou "existe um tipo chamado
   `WindowLayoutConstants`" em "o getter está em `0xF4BF4`". `[BIN]`
2. **Varredura de `adrp`+`add` sobre `__text`** para xref de string longa.
3. **Varredura de `movz`/`movk` sobre `__text`** para string **curta**. Isto é o
   que faltava antes: uma `String` de **até 15 bytes** em Swift é *small string* e
   **não vira referência a `__cstring`** — ela é montada em imediatos dentro da
   função. `asset-mirroring` tem exatamente 15 caracteres, e é por isso que
   procurar quem referencia a `__cstring` dele devolve **zero** ocorrências; o uso
   real está em `movz`/`movk`. Uma busca só por xref de cstring teria concluído,
   errado, que a chave não é usada. `[BIN]`

---

## 2. Pergunta 1 — o que `Clear` é

### 2.1. Método

Três leituras independentes que se confirmam:

- o `fieldmd` do Foundation e do IconRendering (já gerados em 15/09 §3.4);
- as tabelas de `__cstring` do Foundation, lidas em bloco e em ordem;
- desmontagem dos acessores de `Rendition` achados pela trie.

### 2.2. Os dois eixos: `Appearance` é do documento, `Rendition` é do render

`[BIN]` `IconComposerFoundation.Appearance` é enum de **quatro** casos, nesta
ordem (`fieldmd_foundation.txt`, descriptor `0x12fd98`):

```
base(0)   light(1)   dark(2)   tinted(3)
```

com as grafias de disco em `__cstring` `0x12a474`–`0x12a484`
(`base`, `light`, `dark`, `tinted`), imediatamente depois de
`"Unknown appearance name: "` (`0x12a440`) e `". Should be one of "` (`0x12a460`).
Isto **bate exatamente** com o nosso `icf::Appearance { Base, Light, Dark, Tinted }`
(`Source/IconComposerFoundation/IconDocument.h:16`) e confirma o doc 01 §6.

`[BIN]` `IconComposerFoundation.Rendition` é **outro** enum, de **seis** casos
(descriptors `0x130188` e `0x1301e0`), nesta ordem:

```
lightColor(0)  darkColor(1)  lightTint(2)  darkTint(3)  lightClear(4)  darkClear(5)
```

A ordem dos casos não é suposição por ordem de listagem do `fieldmd`; ela está
provada três vezes por desmontagem, abaixo.

**Serialização** (`Rendition.SerializedRepresentation`, privado): as grafias estão
em `__cstring` `0x12a6e0`–`0x12a718`, encostadas no nome do arquivo-fonte
`IconComposerFoundation/RenderingConfiguration.swift` (`0x12a6a0`) e seguidas de
`"Unknown rendition name: "` (`0x12a730`):

```
light-color   dark-color   light-tint   dark-tint   light-clear   dark-clear
```

### 2.3. As duas tabelas de nome legível, e de onde veio a tabela do adendo §5.1

`[BIN]` `Rendition.fileNameComponent` (getter em `0x421BC`) é um `csel` puro sobre
o tag, com as strings em imediatos. Decodificando os imediatos:

| tag | `fileNameComponent` | `displayName` (getter `0x41F5C`) | serializado |
|---|---|---|---|
| 0 | `Default` | `Default` | `light-color` |
| 1 | `Dark` | `Dark` | `dark-color` |
| 2 | `TintedLight` | `Tinted Light` | `light-tint` |
| 3 | `TintedDark` | `Tinted Dark` | `dark-tint` |
| 4 | `ClearLight` | `Clear Light` | `light-clear` |
| 5 | `ClearDark` | `Clear Dark` | `dark-clear` |

`[BIN]` A "tabela compacta de nomes" do adendo §5.1 do laudo de 18/09
(`Default\0Dark\0\0\0\0TintedLiTintedDaClearLigClearDar`, no Kit em `0x185438`)
é exatamente **esta coluna `fileNameComponent`**, materializada no Kit. O adendo
leu certo; agora sabe-se o que ela é: o **componente de nome de arquivo** de cada
rendition na exportação, e o mesmo texto que o `ictool` aceita em `--rendition`
(exemplo literal no binário, `0x12c650`:
`--rendition TintedDark … --tint-color 0.25 --tint-strength 0.75`).

### 2.4. A ordem dos casos, provada

`[BIN]` Três desmontagens independentes fixam o tag de cada caso:

- `Rendition.displayOrder` (`0x3A594`) é `and x0, x0, #0xff ; ret` — **a ordem de
  exibição é o próprio tag**. Não há reordenação escondida.
- `Rendition.isTintedOrClear` (`0x362F8`) é `and w8,w0,#0xff ; cmp w8,#1 ;
  cset w0,hi` — verdadeiro para tag > 1. Ou seja, 0 e 1 são os casos "color".
- `Rendition.isLight` — o **setter** (`0x420E0`) escreve por tabela
  `0x0000_0405_0203_0001`, isto é `0↔1`, `2↔3`, `4↔5`. Os seis casos são **três
  pares claro/escuro**, e trocar "claro" por "escuro" nunca sai do par.

### 2.5. A resposta: `Clear` é o terceiro **modo de render**

`[BIN]` No `IconRendering.arm64` (`fieldmd_iconrendering.txt`), o estilo que o
render recebe é:

```
IconRendering.ICRIconStyle            (descriptor 0xa4110, 5 campos)
    platform
    appearance        : ICRIconStyle.SystemAppearance   (0xa415c) = { light, dark }
    renderingMode     : RenderingMode                   (0xa4184)
    layoutDirection
    designGeneration  : __C.ICRDesignGeneration

IconRendering.RenderingMode.Contents  (0xa41a0, multipayload) = { tinted(payload), color, clear }
        CodingKeys        (0xa4268) = { color, tinted, clear }
        TintedCodingKeys  (0xa4230) = { _0, saturation }
        ClearCodingKeys   (0xa4220) = vazio
        ColorCodingKeys   (0xa4258) = vazio
```

**Duas aparências × três modos = seis.** As seis renditions são exatamente o
produto cartesiano, e o nome de cada uma diz as duas metades:

| rendition | `ICRIconStyle.appearance` | `RenderingMode.Contents` |
|---|---|---|
| `Default` | `light` | `color` |
| `Dark` | `dark` | `color` |
| `Tinted Light` | `light` | `tinted` |
| `Tinted Dark` | `dark` | `tinted` |
| `Clear Light` | `light` | `clear` |
| `Clear Dark` | `dark` | `clear` |

Portanto: **`Clear` NÃO é uma aparência do documento que o doc 01 §6 tenha
perdido.** É um **modo de composição do render**, irmão de `color` e `tinted`, e
o eixo de aparência do render tem só **dois** valores (`light`/`dark`), não quatro.
O predicado `appearance` do documento e o `Rendition` da UI são eixos diferentes,
como o adendo de 18/09 já suspeitava — e agora está medido qual é qual.

### 2.6. Qual aparência do documento cada rendition lê

`[BIN]` `Rendition.sourceAppearance` (`0x415FC`) é uma tabela de bytes num registrador:

```
0x000415FC  ubfiz  x8, x0, #3, #8          ; x8 = tag*8
0x00041600  mov    x9, #0x201              ; x9 = 0x0000_0303_0303_0201
0x00041604  movk   x9, #0x303, lsl #16
0x00041608  movk   x9, #0x303, lsl #32
0x0004160C  lsr    x0, x9, x8
0x00041610  ret
```

Bytes do menos significativo ao mais: `01 02 03 03 03 03`. Contra
`Appearance{base=0, light=1, dark=2, tinted=3}`:

| rendition | `sourceAppearance` |
|---|---|
| `Default` | **`light`** |
| `Dark` | **`dark`** |
| `Tinted Light` | **`tinted`** |
| `Tinted Dark` | **`tinted`** |
| `Clear Light` | **`tinted`** |
| `Clear Dark` | **`tinted`** |

`[BIN]` O inverso confirma: `Appearance.renditions` (`0x20DA8`), no ramo de
`tinted`, varre `allCases` (tabela de 6 bytes em `0x157028` = `00 01 02 03 04 05`)
com `cmp w24,#2 ; b.lo (pula)` — isto é, **`tinted` cobre os quatro tags ≥ 2**,
os dois `Tinted` e os dois `Clear`.

**Consequência dura para a UI:** as quatro renditions tingidas leem **a mesma
fatia** do documento (`appearance: tinted`). O documento **não tem** como
distinguir `Tinted Light` de `Clear Dark`; o que distingue é o render. Um combo de
aparência de escrita continua com quatro entradas — o que ganha seis entradas é o
seletor de **rendition** (preview/exportação), que é outra coisa.

### 2.7. Duas surpresas medidas, que mudam a cara da barra

`[BIN]` `Rendition.displayGrouped` (`0x4202C`) monta três arrays literais de dois
elementos, em `__const` `0x1580A8` = `[0,1]`, `0x1580D8` = `[4,5]`, `0x158108` =
`[2,3]`. Ou seja, a ordem visual do alvo é:

```
[ Default, Dark ]   [ Clear Light, Clear Dark ]   [ Tinted Light, Tinted Dark ]
```

**`Clear` vem antes de `Tinted`.** Coerente com isto, `Appearance.defaultRendition`
(`0x20D98`) é a tabela `0x0401_0000`:

| appearance | rendition padrão |
|---|---|
| `base` | `Default` |
| `light` | `Default` |
| `dark` | `Dark` |
| `tinted` | **`Clear Light`** |

Isto é `[BIN]` — os quatro bytes são `00 00 01 04`. *Por que* a aparência tingida
abre em `Clear Light` e não em `Tinted Light` é `[OBS]`: o agrupamento acima
sugere que no 27 o Clear é a face primária da família tingida, mas isso é leitura,
não medição.

`[BIN]` `Platform.validRenditions` (`0x3A248`) percorre os 6 casos com
`cmp w25,#0 ; ccmp w24,#1,#0,ne ; b.hi (pula)`, onde `w25` é o tag da rendition e
`w24` o da plataforma (`Platform = iOS(0), macOS(1), watchOS(2)`):
**`Default` vale para toda plataforma; as outras cinco só valem para
plataforma ≤ 1.** Isto é, **watchOS oferece só `Default`** — a grade não é 3×6.

### 2.8. Onde `Clear` entra no pipeline de render

`[BIN]` No `IconRendering.arm64`, `Clear` tem bloco de parâmetros **próprio**,
paralelo ao de tint, não um caso do de tint:

- `ICRRenderingParameters.ClearMode` (descriptor `0xa4578`), **15 campos**:
  `totalLighteningStrength`, `totalDarkeningStrength`, `totalHighlightsStrength`,
  `contentLighteningStrength`, `contentDarkeningStrength`, `highlightsVCM`,
  `lighteningUsesVCM`, `lighteningVCM`, `darkeningUsesVCM`, `darkeningVCM`,
  `passOrder`, `applyToLightTintToo`, `leaveChicletDarklightsToSystem`,
  `drawByReference`, `debugMode`.
  - `ClearMode.PassOrder` (`0xa463c`) = `{ lightenFirst, darkenFirst }`
  - `ClearMode.DebugMode` (`0xa4664`) = `{ none, rawImage, lighteningMask, darkeningMask, highlightsMask }`
  - os três `VCM` são a struct de 4 campos `{ black, white, saturation, headroom }`
    (`0xa46b0`), a mesma forma do VCM de realce que o doc 03 já conhece.
- e, nas tabelas de nome de parâmetro do IconRendering, o par claro/escuro do
  Clear existe lado a lado com o de tint: `lightClearStartBrightness` /
  `lightClearEndBrightness` / `darkClearStartBrightness` / `darkClearEndBrightness`
  (`0xa22f0`–`0xa2390`), `mitigatedChicletLightClear` / `mitigatedChicletDarkClear`
  (`0xa1730`, `0xa1750`), `chicletClear` (`0xa2018`), `glyphsClear` (`0xa205a`),
  `clearIconBlendMode` (`0xa6630`), `usesCAFilterForClearMode` /
  `layerUsesCAFilterForClearMode` (`0xa0e30`, `0xa0e50`).

Em uma frase: **`Clear` entra como o terceiro valor de `ICRIconStyle.renderingMode`,
e a partir daí um passo de clarear/escurecer com máscaras próprias (`ClearMode`),
com caminho de CAFilter próprio, ao lado — não dentro — do caminho de tint.**
Os **valores** desses 15 campos não foram lidos (§6).

`[BIN]` No lado do controle, `IconComposerFoundation.RenderingConfiguration`
(descriptor `0x13000c`, 14 campos) carrega
`rendition`, `tintSpectrumPosition: Double`, `tintAlpha: Double` e
`lastTintMode: InspectedTintMode` — e `InspectedTintMode` (`0x1300c4`) tem
**quatro** casos, `{ lightTint, darkTint, lightClear, darkClear }`. Ou seja: a cor
do tint é ajustável pelo usuário e vale para as quatro renditions tingidas,
inclusive as Clear (o `ictool` expõe isso como `--tint-color`/`--tint-strength`).
No Kit, `RenditionBar` tem o campo `_showTintedOptionsPopover` — é o popover
dessas duas alavancas.

---

## 3. Pergunta 2 — `asset-mirroring` e `implicit-asset-mirroring`

### 3.1. Método

O `fieldmd` do Foundation dá o **tipo**; a `__cstring` dá as grafias candidatas; a
varredura de imediatos e a de `adrp`+`add` dão **quem usa cada grafia**; a trie dá
os acessores para desmontar o default e a regra de herança. As duas metades que o
cabeçalho de `PanelInspectorAsset.cpp` pede — a grafia da chave no disco e a
gramática do valor — **fecharam as duas**.

### 3.2. A grafia da chave — `[BIN]`, nas duas

**`asset-mirroring`** (15 caracteres, *small string*). A `__cstring` em `0x12ab01`
não tem xref, como esperado; o uso real está em imediatos, em **dois sítios
independentes** — os `rawValue` de `CodingKeys` do snapshot de **grupo** e do
snapshot de **camada**:

```
0x000BA35C  mov   x0, #0x7361          ; x0 = 0x696D_2D74_6573_7361  -> "asset-mi"
0x000BA360  movk  x0, #0x6573, lsl #16
0x000BA364  movk  x0, #0x2d74, lsl #32
0x000BA368  movk  x0, #0x696d, lsl #48
            (x1 = 0xEF67_6E69_726F_7272 -> "rroring" + flag de tamanho 0xEF = 15)
0x000BA36C  ret
```

e o mesmo par em `0x000C3474`–`0x000C3480`. Que é tabela de `CodingKeys` e não
outra coisa está provado pela vizinhança: o caso imediatamente seguinte, em
`0x000C3488`, devolve `"position"` (`0x6E6F_6974_6973_6F70`, tamanho 8) — e
`assetMirroring` é seguido de `position` na lista de `CodingKeys` do grupo.

**`asset-mirroring-specializations`** (31 caracteres, longo demais para small
string): `__cstring` `0x12ba90`, referenciada em `0xBA07C` e `0xC34E4` — os mesmos
dois `rawValue`.

**`implicit-asset-mirroring`** (24 caracteres): `__cstring` `0x12a750`,
referenciada em `0x418C4`, `0x41D68`, `0xC8EC4`, `0xC8FC4` — o `CodingKeys` do
documento (`Snapshot.JSONContent.CodingKeys`, descriptor `0x1317cc`, onde o campo
se chama `implicitAssetMirroring`).

`[BIN]` Confirmação posicional, de brinde: o bloco de `__cstring`
`0x12b84c`–`0x12bac0`, que segue o nome de arquivo
`IconComposerFoundation/IconComposition.Group.Snapshot.swift` (`0x12b810`), é a
tabela de `rawValue` do grupo **na ordem exata** dos 30 casos do
`Snapshot.CodingKeys` do `fieldmd` (`0x1313f4`), pulando só os que o linker
deduplicou: `name`, `layers`, `hidden-specializations`, `opacity-specializations`,
`blur-material`, …, `lighting-specializations`, **`asset-mirroring-specializations`**,
`position`, `position-specializations`. A regra camelCase→kebab acertou aqui; e
agora não é previsão, é leitura.

### 3.3. A gramática do valor — `[BIN]`

`[BIN]` Na camada e no grupo, o valor **não é um booleano nu**: é um **objeto**.

```
IconComposerFoundation.IconComposition.AssetMirroring   (struct, descriptor 0x130ae0)
        mirrorable : Bool?          (fieldmd: "SbSg" = Optional<Bool>)
    AssetMirroring.CodingKeys       (0x130afc) = { mirrorable }
```

- a grafia da sub-chave é `mirrorable` — `__cstring` `0x12b35f`, e em imediatos
  dentro do `encode`/`init(from:)` do tipo (`0xA5E24`, `0xA5E5C`, `0xA5E90`,
  `0xA5F34`, `0xA5F54`);
- o codificador usa **container com chave** (`encode(to:)` em `0xA6068`,
  `init(from:)` em `0xA6234`), não `singleValueContainer`;
- o **default** de `mirrorable` é **`nil`**: o inicializador de valor padrão
  (`…AssetMirroringV10mirrorableSbSgvpfi`, `0x1A04`) é `mov w0, #2 ; ret`, e `2` é
  o *extra inhabitant* de `Optional<Bool>`, isto é `nil`.

Forma no disco, então:

```json
"asset-mirroring" : { "mirrorable" : true }
```

`[BIN]` E é **especializável**: a propriedade é
`SpecializableProperty<IconComposition.AssetMirroring>` nos dois níveis — os
símbolos importados pelo Kit dizem o tipo inteiro
(`…IconCompositionC5GroupC14assetMirroringAA21SpecializablePropertyVyAC05AssetG0VG…`
e o mesmo com `5LayerC`; os nomes estão na tabela de nomes de símbolo do Kit,
`__LINKEDIT`, offsets `0x2195F8` e `0x219320`). Daí o par
`asset-mirroring` + `asset-mirroring-specializations`, exatamente o invariante
"chave simples e lista nunca coexistem" que o nosso escritor já respeita.

`[BIN]` No documento é diferente e mais simples:
`IconComposition.implicitAssetMirroring` é **`Bool` não-opcional** (`fieldmd`:
`Sb`, descriptors `0x13070c` e `0x1317cc`), **não é especializável**, e o default é
**`false`** — o inicializador padrão (`0x1A48`) é `mov w0, #0 ; ret`.

```json
"implicit-asset-mirroring" : false
```

### 3.4. A semântica, que é o que faz o controle ser tri-estado

`[BIN]` `AssetMirroring.effectiveIsMirrorable(inheritedValue:)` (`0xA5DE8`):

```
and   w8, w1, #0xff
cmp   w8, #2            ; 2 == nil
csel  w8, w0, w1, eq    ; nil -> valor herdado ; senão -> o próprio
and   w0, w8, #1
ret
```

isto é, **`efetivo = mirrorable ?? herdado`**. Os dois consumidores são
`Group.effectiveIsMirrorable(for: SpecializationSlot)` (`0x936D0`) e
`Layer.effectiveIsMirrorable(for:)` (`0x9C06C`); a raiz da cadeia de herança é o
`implicitAssetMirroring` do documento. Também nomeado, e não lido a fundo aqui:
`IconComposition.SpecializationResults.mirrorableAssetNames`, e o
`AssetFile.ResolutionContext(assetName:assumedSVGColorSpace:mirrorBaseAsset:languageID:)`
— o `mirrorBaseAsset: Bool` é por onde o efetivo chega na resolução do asset.

### 3.5. Como o alvo desenha isso

`[BIN]` São **dois** inspetores, não um (metadata de campo do Kit):

- `IconComposerKit.IconAssetMirroringInspector` (`0x1D7884`) — nível documento,
  campos `iconDocument`, `_renderingConfiguration`;
- `IconComposerKit.AssetMirroringInspector` (`0x1D78AC`) — nível grupo/camada,
  campos `iconDocument`, `values`, `_renderingConfiguration`.

`[BIN]` O de grupo/camada é um
`SpecializablePropertyInspector<MultiPicker<Binding<AssetMirroring.InspectorValue>>, ForEach<…>, Text, Divider, …, SliceDrop>`
(nome simbólico na tabela de nomes de símbolo do Kit, `__LINKEDIT`, offset
`0x2AD680`) — ou seja, **picker**, não toggle, com
multi-seleção e alvo de arrastar-e-soltar de fatia.

`[BIN]` `AssetMirroring.InspectorValue` é uma extensão do Kit, enum de **três**
casos (metadata de campo do Kit, `0x1D78E0`):

```
inherited     fixed     mirror
```

`[BIN]` Os textos: `"Mirror Assets in Right to Left"` (`0x1DBD80`),
`"Automatically mirror assets for right-to-left languages"` (`0x1DBDA0`),
`"In Right to Left"` (`0x1DBE30`), e o símbolo
`arrow.trianglehead.left.and.right.righttriangle.left.righttriangle.right`
(`0x1DBDE0`).

`[INF]` A correspondência entre os três casos e `Bool?` é a única que fecha com
§3.3 e §3.4 — `inherited` = `nil`, `fixed` = `false`, `mirror` = `true` — mas a
função que faz essa conversão é interna ao Kit, não exportada, e **não foi
desmontada**. É inferência, não leitura.

### 3.6. O corpus — `[ART]`, medido nesta passagem

Sobre os 145 `icon.json` de `References/corpus/`:

| chave | documentos que a escrevem |
|---|---|
| `asset-mirroring` | **0 / 145** |
| `implicit-asset-mirroring` | **0 / 145** |
| `mirrorable` | **0 / 145** |

O corpus continua mudo. O que mudou é que ele deixou de ser a única testemunha: a
grafia e a gramática agora vêm do binário.

---

## 4. Pergunta 3 — `WindowLayoutConstants`

### 4.1. Método

`WindowLayoutConstants` é um enum-namespace do Kit com quatro enums aninhados. Os
nomes estão em `__const` `0x18FA20`
(`WindowLayoutConstants\0Window\0Canvas\0Sidebar\0Inspector\0`), mas atribuir os
números por vizinhança seria palpite. A atribuição real veio da **trie de
exportação do Kit**, que nomeia tanto os endereços das variáveis (`…vpZ`) quanto
os getters (`…vgZ`) e os address-ors (`…vau`). Os oito `Double` em `__const`
`0x18F9E0`–`0x18FA10` casam um a um com os símbolos `…vpZ`, e os getters
confirmam por imediato.

### 4.2. Os valores — `[BIN]`

Todos em pontos (`CGFloat`), slice `IconComposerKit.arm64`:

| constante | valor | var (`…vpZ`) | getter (`…vgZ`) |
|---|---|---|---|
| `WindowLayoutConstants.Canvas.padding` | **96.0** | `0x18F9E0` | `0xF4BE8` |
| `WindowLayoutConstants.Sidebar.minWidth` | **250.0** | `0x18F9E8` | `0xF4BF4` |
| `WindowLayoutConstants.Sidebar.idealWidth` | **330.0** | `0x18F9F0` | `0xF4C04` |
| `WindowLayoutConstants.Sidebar.maxWidth` | **480.0** | `0x18F9F8` | `0xF4C14` |
| `WindowLayoutConstants.Inspector.minWidth` | **330.0** | `0x18FA00` | `0xF4C04` |
| `WindowLayoutConstants.Inspector.idealWidth` | **330.0** | `0x18FA08` | `0xF4C04` |
| `WindowLayoutConstants.Inspector.maxWidth` | **500.0** | `0x18FA10` | `0xF4C20` |
| `WindowLayoutConstants.Window.minContentSize` | **CGSize(1284.0, 704.0)** | — | `0xF4BAC` |
| `WindowLayoutConstants.Window.idealSize` | **CGSize(1601.25, 960.0)** | — | `0xF4BC4` |

(Os getters de `Sidebar.idealWidth`, `Inspector.minWidth` e `Inspector.idealWidth`
são **a mesma função**, `0xF4C04` — o linker dobrou três getters idênticos porque
os três devolvem 330.0. Isso é evidência a mais de que os três valores são iguais,
não um erro de atribuição.)

Os dois `CGSize` saem em `d0`/`d1` como imediatos:
`0x4094100000000000 = 1284.0`, `0x4086000000000000 = 704.0`,
`0x4099050000000000 = 1601.25`, `0x408E000000000000 = 960.0`.

`[INF]` `1284 = 250 + 330 + 704`: com a sidebar e o inspetor nos mínimos, sobram
**704** para o canvas — que é exatamente a altura mínima da janela. O mínimo do
alvo é "um canvas quadrado de 704 pt, mais os dois painéis encolhidos". Já os
`1601.25 × 960` do ideal **não** se decompõem de um jeito óbvio (sobram 941.25
entre as duas idealWidth de 330), e não vou inventar uma decomposição: `[OBS]`.

### 4.3. O que isso diz da nossa janela

`[ART]` Hoje o nosso layout é percentual e vem do `sfsymview`:
`Source/app/Window.cpp:263-264` divide `0.20f` à esquerda e `0.28f` à direita.
Numa janela de 1601 pt isso dá **320 / 448** — a sidebar cai perto do ideal do
alvo (330) por acidente, e o inspetor sai **quase 120 pt largo demais** (o alvo
quer 330 ideal, teto 500). E, por ser percentual, os dois painéis **não têm piso
nem teto**: a janela encolhendo, a sidebar passa abaixo dos 250 do alvo sem
resistência. A `[OBS]` de 13/09 §2.1 pode ser fechada com estes nove números.

---

## 5. O que estas respostas destravam, em uma linha cada

- **Pergunta 1 → a barra de renditions (T4).** A forma é `[[Default, Dark],
  [Clear Light, Clear Dark], [Tinted Light, Tinted Dark]]`, filtrada por
  `Platform.validRenditions` (watchOS = só `Default`). O combo de **aparência**
  do documento continua com quatro entradas; quem ganha seis é o seletor de
  **rendition**, e ele é de preview/exportação, não de escrita.
- **Pergunta 1 → o combo não ganha `Clear`.** Escrever `appearance: "clear"` num
  documento seria lixo: a grafia não existe (`base|light|dark|tinted`) e as
  renditions Clear leem a fatia `tinted`.
- **Pergunta 2 → o inspetor de mirroring pode existir**, com a Regra 2 do plano
  satisfeita: a chave é `asset-mirroring`, o valor é `{"mirrorable": bool}`, a
  ausência é `nil` = herda, e o documento tem `implicit-asset-mirroring: bool`
  (default `false`) como raiz da herança.

---

## 6. O que este laudo NÃO fez

- **Não leu um único valor numérico de `ICRRenderingParameters.ClearMode`.** Os 15
  campos estão nomeados e tipados; os *valores* padrão exigem desmontar o
  inicializador de parâmetros, que é outro trabalho. Sem eles, este laudo **não
  autoriza implementar o modo Clear no render** — só nomear a existência dele.
  `[OBS]`
- **Não mediu por que `Appearance.tinted.defaultRendition` é `Clear Light`.** O
  byte está lido; a razão é leitura minha, não medição. `[OBS]`
- **Não decompôs `Window.idealSize` (1601.25 × 960)** em contribuições de painel.
  `[OBS]`
- **Não desmontou a conversão `AssetMirroring.InspectorValue` ↔ `Bool?`** (é
  interna ao Kit, fora da trie). O mapeamento em §3.5 é `[INF]`.
- **Não verificou se `encode` usa `encodeIfPresent`** — isto é, se `mirrorable ==
  nil` omite a sub-chave ou escreve `null`. Para nós dá no mesmo enquanto o
  controle só escrever `fixed`/`mirror` e **remover a chave** para `inherited`,
  que é o comportamento que o nosso escritor sob escopo já sabe fazer; mas se
  alguém quiser escrever `nil` explicitamente, isso é `[OBS]`.
- **Não leu `SpecializationSlot.exportableVariants`** (`0xD5D7C`, `[(Platform,
  Rendition)]`), que é a matriz que a exportação de T2 vai querer.
- **Não abriu `ExportOptions`/`ExportSize`/`ResolvedExportOptions`.** O escopo aqui
  era `Clear`, não a folha de exportação.
- **Não tocou em `Source/` nem em `Tests/`.** Nenhuma linha de produção ou de
  teste foi escrita, como a T1 manda.
