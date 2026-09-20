# Laudo — os valores do `ClearMode`

*2026-09-20. O laudo de 19/09 (`2026-09-19-renditions-e-mirroring.md` §6) fechou
dizendo: os 15 campos de `ICRRenderingParameters.ClearMode` estão nomeados e
tipados, os **valores** exigem desmontar o inicializador de parâmetros, e sem
eles "este laudo **não autoriza implementar o modo Clear no render**". Este
documento desmonta esse inicializador. Os 15 valores estão abaixo, todos `[BIN]`.
E ele também diz, com a mesma clareza, **o que continua faltando** — que não são
os valores.*

Selos: `[BIN]` medido no binário (com endereço), `[ART]` medido no corpus,
`[OBS]` pergunta aberta, `[INF]` inferência marcada como tal.

Alvo: `References/27.0-129/out/slices/IconRendering.arm64` (fatia fina,
VA == offset de arquivo).

---

## 0. A resposta, em uma frase

**Os 15 valores saíram, e eles descrevem uma gradação — dois passes de
clarear/escurecer mais um de realce, cada um com força própria e matriz de cor
própria. E as máscaras que esses passes multiplicam não estão neste binário:
a caçada da §3 mostra que o `IconRendering` só COMPARA e SERIALIZA o
`ClearMode`, nunca desenha com ele. Quem desenha é o QuartzCore, por CAFilter.**

---

## 1. Onde os valores moram

`[BIN]` `GlassMaterial.h` já nomeava o construtor agregado de
`ICRRenderingParameters`: **`0x5E838`**, "escrevendo pools em
`0x985D0`/`0x985E0`/`0x985F0`". É o mesmo construtor, e o bloco do `ClearMode`
está em `0x5E990`–`0x5EA3C`.

`[BIN]` O campo `clearMode` fica em **`params + 0x120`**. Duas leituras
independentes concordam:

* o getter exportado `ICRRenderingParameters.clearMode.getter` (`0x5FE74`, achado
  pela trie de exportação — o `LC_SYMTAB` desta fatia não nomeia função Swift
  nenhuma, mas a trie sim) copia `[x20, #0x120]` até `[x20, #0x1a0]+16` e o
  `ldur q0, [x9, #0x8e]` da cauda;
* o construtor faz `add x21, x19, #0x120` em `0x5E990` e, em `0x5EA3C`,
  `stp q0, q1, [x19, #0x120]`.

`[BIN]` A struct é montada **na pilha**, em `sp+0x50`, e copiada de lá. Logo
`sp + 0x50 + K` é o campo de deslocamento `K` do `ClearMode`, e é essa
correspondência que dá cada valor abaixo.

### 1.1. O layout, campo a campo

`[BIN]` Não é layout suposto: cada deslocamento é o que o **getter** daquele
campo carrega. Dezesseis getters lidos, um por linha.

| campo | `+` | getter | tipo |
|---|---|---|---|
| `totalLighteningStrength` | `0x00` | `0x2F5DC` | `Double` |
| `totalDarkeningStrength` | `0x08` | `0x37F1C` | `Double` |
| `totalHighlightsStrength` | `0x10` | `0x38DF0` | `Double` |
| `contentLighteningStrength` | `0x18` | `0x38E00` | `Double` |
| `contentDarkeningStrength` | `0x20` | `0x38E10` | `Double` |
| `highlightsVCM` | `0x28` | `0x5FEF4` | `VCM` |
| `lighteningUsesVCM` | `0x49` | `0x37F84` | `Bool` |
| `lighteningVCM` | `0x50` | `0x5FF3C` | `VCM` |
| `darkeningUsesVCM` | `0x71` | `0x5FF84` | `Bool` |
| `darkeningVCM` | `0x78` | `0x5FFA4` | `VCM` |
| `passOrder` | `0x99` | `0x5FFEC` | `PassOrder` |
| `applyToLightTintToo` | `0x9a` | `0x60014` | `Bool` |
| `leaveChicletDarklightsToSystem` | `0x9b` | `0x60034` | `Bool` |
| `drawByReference` | `0x9c` | `0x60054` | `Bool` |
| `debugMode` | `0x9d` | `0x60074` | `DebugMode` |

O `VCM` (descriptor `0xa46b0`) é `{ black: Double, white: Double,
saturation: Double, headroom: Double? }` — 33 bytes, o `Double?` sendo 8 de valor
mais 1 de etiqueta. Daí os buracos de alinhamento em `0x4a`–`0x4f` e
`0x72`–`0x77`.

---

## 2. Os 15 valores

`[BIN]` Cada um rastreado do pool ao deslocamento.

```
totalLighteningStrength          1.0        0x98590 lane 0
totalDarkeningStrength           0.3        0x98590 lane 1
totalHighlightsStrength          1.0        0x985A0 lane 0
contentLighteningStrength        0.85       0x985A0 lane 1
contentDarkeningStrength         0.0        0x98540 lane 0

highlightsVCM  black             0.2        0x98540 lane 1
               white             1.35       0x985B0 lane 0
               saturation        1.4        0x985B0 lane 1
               headroom          .some(1.2) imediato 0x3FF3333333333333,
                                            etiqueta 0x00 (strh 0x0100 @ sp+0x98)

lighteningUsesVCM                true       o byte alto do mesmo strh
lighteningVCM  black             0.9        0x985C0 lane 0
               white             2.5        0x985C0 lane 1
               saturation        2.0        imediato 0x4000000000000000
               headroom          .some(1.2) imediato 0x3FF3333333333333,
                                            etiqueta 0x00 (strh wzr @ sp+0xC0)

darkeningUsesVCM                 false      o byte alto do mesmo strh wzr
darkeningVCM   black            -0.5        0x985D0 lane 0
               white             0.5        0x985D0 lane 1
               saturation        1.5        imediato 0x3FF8000000000000
               headroom          nil        valor 0.0 (xzr) e etiqueta 0x01

passOrder                        lightenFirst   0x00  } os quatro bytes
applyToLightTintToo              true           0x01  } de 0x98860,
leaveChicletDarklightsToSystem   true           0x01  } `01 00 01 01`,
                                                      } escritos por
                                                      } `str s0, [sp,#0xE8]`
drawByReference                  true       w22 == 1 (0x5E8BC)
debugMode                        none       o byte alto do mesmo strh
```

### 2.1. Três leituras que os números pagam sozinhos

**O `darkeningVCM` é lixo vivo.** `darkeningUsesVCM` é `false`, e é o único dos
três que tem `headroom == nil` e um valor de headroom `0.0` gravado com `xzr`. Os
outros dois carregam `1.2` com etiqueta `.some`. Um campo desligado com o valor
por preencher é o padrão de um parâmetro que existe para ser ligado em
depuração — e é o que impede alguém de ler `black = -0.5` como se desenhasse.

**O escurecimento é fraco e não toca o conteúdo.**
`totalDarkeningStrength = 0.3` contra `1.0` do clarear, e
`contentDarkeningStrength = 0.0` contra `0.85`. Isto é, o passe de escurecer age
sobre **tudo menos o conteúdo**, e com menos de um terço da força. Somado a
`leaveChicletDarklightsToSystem = true`, a leitura é que a sombra escura do Clear
é majoritariamente responsabilidade do sistema, não do render.

**O `black` do realce é o mesmo 0,2 do glifo.** `[BIN]` `GlassSpecular.h` já tem
`glyphHighlightVCM = [0.2, 1.2, 1.25, 0.0, true]`. O `highlightsVCM` do Clear é
`[0.2, 1.35, 1.4, .some(1.2)]`: mesmo piso de luma, teto e saturação mais altos.
É a mesma família de matriz com outra afinação — e a cadeia que a aplica **já
está transcrita neste repo**, em `rb::applyGlyphVCM` (BT.709 RGB→YCbCr, níveis em
Y, ganho de croma em torno de 0,5, volta).

`[INF]` O `VCM` de 4 campos dos parâmetros e o `GlyphVCM` de 5 campos do runtime
são o mesmo objeto em duas formas: o `headroom: Double?` colapsa o `headroom` e o
`skipHeadroomClamp` do runtime — `nil` é o clamp pulado, `.some(h)` é
`addContentHeadroom:` com `h`. 4 = 3 + 1 e a nulidade é exatamente o booleano;
não desmontei a conversão.

### 2.2. O `clearMode` padrão não é nulo

`[INF]` O campo é `ClearMode?` e não há byte de etiqueta separado: o construtor
escreve até `+0x9d` e o campo seguinte do pai começa em `+0x1c0`. `debugMode` é
um enum de 5 casos num byte, com 251 habitantes extras, e o construtor escreve
`0` ali — que é `DebugMode.none`, não o nulo. Logo o padrão é `.some`, com
depuração desligada. Não desmontei a testemunha de valor do `Optional`.

---

## 3. A caçada ao consumidor — e ele não existe neste binário

Este laudo nasceu, na manhã de 20/09, dizendo que o consumidor era o buraco. A
caçada foi feita no mesmo dia, e a resposta é mais forte que "não achei": **em
`IconRendering` 27.0-129 nada renderiza com o `ClearMode`.**

### 3.1. A prova, e ela é independente de base

Os cinco últimos campos são bytes em `+0x99`–`+0x9d`: `passOrder` seguido de
quatro consecutivos. Um renderizador precisa de pelo menos `passOrder` (qual
passe primeiro) e `debugMode`. Então a assinatura é **`LDRB` em quatro
deslocamentos CONSECUTIVOS a partir do mesmo registrador-base, mais o byte em
`x-1`** — que não depende de ONDE a struct está, e portanto pega também uma
cópia na pilha, que é como o inlining do getter a deixaria.

`[BIN]` Varrendo o `__text` inteiro (`0x1890` + 570.776 bytes, funções
delimitadas por `LC_FUNCTION_STARTS`), o padrão `+0x99` mais `+0x9a`–`+0x9d`
ocorre em **duas** funções, e em nenhuma outra:

| função | o que é | como sei |
|---|---|---|
| `0x6D6B0` | `ClearMode.==` derivado | compara campo a campo dois operandos, 504 bytes, e a assinatura aparece **duas vezes** nela — uma por operando (`x8` e `x1`) |
| `0x60588` | `ClearMode.encode(to:)` | 1 chamador (`0x60FC8`); as únicas chamadas que faz são `__swift_instantiateConcreteTypeFromMangledNameV2` e `__swift_project_boxed_opaque_existential_1`, e o existencial encaixotado é o `Encoder` |

Uma varredura complementar por `LDR`/`LDRB` nos deslocamentos dos 15 campos, com
base `0` e base `0x120`, concorda: **ninguém lê `params + 0x169`
(`= 0x120 + 0x49`) nem `params + 0x1B9`**, isto é, também não há leitor inline
pela base dos parâmetros.

`[OBS]` O que a varredura não cobre: um leitor que só tocasse os `Double` — via
`LDP`, que ela não decodifica — e nunca um dos bytes. Mas um renderizador que
ignorasse `passOrder` e `lighteningUsesVCM` não saberia o que desenhar, então o
risco residual é pequeno, e fica dito em vez de escondido.

### 3.2. E não há shader de Clear

`[BIN]` Nenhum kernel de `metallib-iconrendering/` nem de `metallib-renderbox/`
(os `.ll` extraídos) tem "clear" no nome. O modo não tem shader próprio.

### 3.3. Quem desenha é o QuartzCore, por CAFilter

Os próprios campos já diziam, e fecham com o que o binário importa:

* `drawByReference = true` ↔ o seletor
  `drawLayerByReference:alpha:blendMode:flags:` (`0x8E840`);
* `leaveChicletDarklightsToSystem = true` — a parte escura é **do sistema**,
  dito no nome do campo;
* `usesCAFilterForClearMode` e `layerUsesCAFilterForClearMode` — o modo tem um
  caminho de CAFilter **no nome da bandeira**.

`[BIN]` E `IconRendering` importa sete constantes do CAFilter. Mapeei os slots do
`__got` pelos fixups encadeados (`LC_DYLD_CHAINED_FIXUPS`, 675 imports) e depois
quem os carrega:

```
0xc5d08  _kCAFilterColorMatrix                 carregado em 0x4042C
0xc5d10  _kCAFilterInputColorMatrix            0x40480, 0x40878
0xc5d18  _kCAFilterInputPremultipliedValues    0x404AC
0xc5d20  _kCAFilterPlusD                       0x40290
0xc5d28  _kCAFilterPlusL                       0x40100
0xc5d30  _kCAFilterScreenBlendMode             0x3FC6C
0xc5d38  _kCAFilterVibrantColorMatrix          0x4081C
```

Os sete usos caem em **três** funções vizinhas: `0x3FAE4`–`0x40404` (a fábrica,
que escolhe entre `PlusL`, `PlusD` e `Screen`), `0x40404`–`0x4062C` (monta um
`colorMatrix`) e `0x406E8`–`0x40AD0` (monta o `vibrantColorMatrix`). A fábrica
tem 3 chamadores e abre com `ldrb w8, [x0, #0x22] ; tbz w8, #0`.

`[INF]` Esse `+0x22` é `GlobalConfiguration.drawMitigatedVersion`. A struct
(descriptor `0xa32cc`) tem `layerUsesCAFilterForClearMode` medido em `+0x24`
pelo getter `0x2FA90`, e os campos em ordem são `effectsAreEnabled`,
`drawMitigatedVersion`, `forceEnableEnhancedGlass`, `layerUsesCAFilter…`,
`usesCAFilter…` — quatro `Bool` de um byte, logo `+0x21`, `+0x22`, `+0x23`,
`+0x24`, `+0x25`. **Esta fábrica é portanto o caminho MITIGADO** — é dela que
saem `mitigatedChicletLightClear` e `mitigatedChicletDarkClear` — e não o
principal.

`[BIN]` De quebra isso fecha um `[OBS]` que `ChicletHighlights.h` deixou aberto:
o "byte `ctx+0x21`, sem nome no metadado" que ela cita como o portão real dos
realces é **`effectsAreEnabled`**.

### 3.4. A conclusão da caçada

**A máscara nunca esteve no `IconRendering` para ser achada.** O `ClearMode` é um
bloco de parâmetros que este binário compara, serializa e **entrega** — e quem
clareia, escurece e mistura é o compositor do sistema, o QuartzCore, com
`colorMatrix` mais `PlusL`/`PlusD`. `lighteningMask` e `darkeningMask` do
`DebugMode` não são imagens que o `IconRendering` gera: são os intermediários
desse pipeline, do outro lado da fronteira.

---

## 4. O que continua faltando

`[OBS]` A aritmética do `CA::OGL` que aplica esses filtros — agora sabidamente
fora deste binário.

**Não foram lidos**, ainda: o `clearIconBlendMode` (`0xa6630` é o nome do campo,
não o valor), o par `lightClearStartBrightness`/`lightClearEndBrightness` e o par
`chicletClear` (`0xa2018`) e `glyphsClear` (`0xa205a`), e
`mitigatedChicletLightClear`/`mitigatedChicletDarkClear` (`0xa1730`, `0xa1750`).
Os nomes estavam no laudo de 19/09; os valores continuam fora.

Os pares `*ClearStartBrightness`/`*ClearEndBrightness`, que aquele laudo listava
como não lidos, **saíram** — estão na §4.1 abaixo.

**Não tocou em `Source/` nem em `Tests/`.**

### 4.1. `ContourGradients`, de brinde e medido

`[BIN]` Achado no caminho: `contourGradients` fica em `params + 0x308` (getter
`0x69198`), tem 10 `Double` em sequência, e o construtor a preenche em
`0x5ECFC`–`0x5ED2C` dos pools `0x986A0`–`0x986E0` mais o imediato
`0x3FE8000000000000`:

| | start | end |
|---|---|---|
| `lightTint` | 0,15 | 0,0 |
| `darkTint` | 0,15 | 0,0 |
| `lightClear` | **0,10** | **0,0** |
| `darkClear` | **0,12** | **0,0** |
| `fakeGlass` | 0,88 | 0,75 |

Todo `end` é zero fora do `fakeGlass` — é uma rampa de brilho ao longo do
contorno que morre. E o Clear tem o par dele, mais fraco que o do tint.

---

## 5. O que isto autoriza, e o que não

Autoriza: transcrever `ClearMode` e `ContourGradients` como dados — os valores
são exatos e reproduzíveis pelos endereços — e reusar `rb::applyGlyphVCM` para
as duas matrizes que estão ligadas.

**E autoriza procurar a aritmética no QuartzCore em vez de no `IconRendering`.**
Isto não é uma substituição de fonte por conveniência: é o que a §3 mediu. O
AquaKit decodificou exatamente esse binário, e o `_kCAFilterVibrantColorMatrix`
que ele sela em `QuartzCore/YccMatrix.h` é **uma das sete constantes** que a
§3.3 encontrou importadas aqui. Os dois lados nomeiam o mesmo símbolo.

**Não autoriza** dizer que o AquaKit já tem o Clear. Ele tem o `YccMatrix`
(`_kCAFilterVibrantColorMatrix`) e o material de vidro; não tem o encadeamento
`colorMatrix` + `PlusL`/`PlusD` que o Clear pede, nem o `clearIconBlendMode`. O
que mudou é a fronteira: a peça que falta está do outro lado dela, e do outro
lado existe um repo que já mede aquele lado.
