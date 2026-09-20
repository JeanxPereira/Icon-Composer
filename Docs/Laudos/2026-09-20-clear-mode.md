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
própria. O que NÃO saiu é de onde vêm as máscaras que esses passes multiplicam,
e sem elas os números não desenham.**

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

## 3. O que este laudo NÃO fez, e é o que agora bloqueia

**Não achou o consumidor.** É o buraco, e ele é maior que o dos valores era:

* `ICRRenderingParameters.clearMode.getter` (`0x5FE74`) tem **0 sítios de
  chamada** (`macho.py xref`). O leitor real é inline e lê `params + 0x120..0x1BD`
  direto.
* `GlobalConfiguration.usesCAFilterForClearMode` e
  `layerUsesCAFilterForClearMode` (`0x2FA90`) também têm **0 sítios**.
* Os símbolos que a trie tem com `ClearMode` no nome são, fora os 16 getters,
  **só maquinário de `Codable` e de reflexão** — nenhum deles desenha.
* `ICRRenderingMode` é uma classe ObjC (`0xCCCE8`) e nenhum seletor dela nomeia o
  Clear; o único seletor com "clear" no nome é `forceClearBackground`
  (`0x8E9C0`), que tem 1 chamador (`0xA560`, dentro da função de render
  `0xA414`–`0xB104`) e é outra coisa — forçar fundo transparente, não o modo.

**Logo as MÁSCARAS não foram lidas.** `DebugMode` nomeia `lighteningMask` e
`darkeningMask`, então elas existem como imagens; de onde saem — que campo, que
limiar, que contorno — não está neste laudo. E é isso que multiplica as forças
acima. `[OBS]`

**Não foram lidos**, também: o `clearIconBlendMode` (`0xa6630` é o nome do campo,
não o valor), o par `lightClearStartBrightness`/`lightClearEndBrightness` e o par
escuro (`0xa22f0`–`0xa2390`), `chicletClear` (`0xa2018`) e `glyphsClear`
(`0xa205a`), e `mitigatedChicletLightClear`/`mitigatedChicletDarkClear`
(`0xa1730`, `0xa1750`). Os nomes estavam no laudo de 19/09; os valores continuam
fora.

**Não tocou em `Source/` nem em `Tests/`.**

---

## 4. O que isto autoriza, e o que não

Autoriza: transcrever `ClearMode` como dados — os 15 valores acima são exatos e
reproduzíveis pelos endereços — e reusar `rb::applyGlyphVCM` para as duas
matrizes que estão ligadas.

**Não autoriza desenhar o Clear inteiro**, pelo mesmo motivo que o laudo de 19/09
não autorizava, só que um degrau adiante: antes faltavam os números, agora falta
a geometria que os números modulam. Uma máscara inventada seria a única peça
inventada num caminho em que todo o resto é medido, e ela decidiria a imagem.
