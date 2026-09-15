# Laudo — a intensidade do realce, e as três identidades que não eram o problema

*2026-09-15. Alvo: o `[OBS]` que o laudo dos realces abriu há poucas horas —
`ctx[0]`, a latitude `phi` e a pós-passagem `0x12550`, "três identidades
assumidas". O pedido veio de olhar o render: **"o efeito atual tá duro demais,
muito forte, falta ajuste e polimento baseado em funções reais"**.*

**As três eram identidades de verdade.** Nenhuma delas explica "forte demais",
e este laudo prova as três — duas delas como teorema, não como número medido,
o que é mais forte: valem **para qualquer valor** dos parâmetros que ainda não
foram lidos.

E, porque elas não eram o problema, a medição continuou até achar o que é.
**O alvo não compõe a saída branca do shader.** Por realce ele abre uma camada,
desenha o `glassHighlight` dentro dela, fecha essa camada como **máscara** e só
então pinta *através* dela uma cor montada por transformadas de cor compostas.
O portão dessa passagem é `glyphHighlightsUseVCM`, e ele é **`true`** nesta
versão. Este renderizador está no ramo `useVCM == false`, cuja única escala é
`glyphHighlightNonVCMScale` = **1.0** — literalmente força cheia e sem polimento.

Todos os endereços são do slice
`References/2.0-125/out/slices/IconRendering.arm64` (VA == deslocamento de
arquivo). A reflexão é `References/2.0-125/out/fieldmd_iconrendering.txt`.

---

## 1. `ctx[0]` é `GlobalConfiguration.lightIntensity`, e vale `1.0`

O laudo-mãe tinha o leitor (`0x0004C010`) e não o escritor. O escritor apareceu
por um caminho que este repositório já tinha pago caro para aprender: **não
procurar a constante, procurar a base**.

`[BIN]` **A base.** `0x0004266C` monta o contexto num quadro de pilha de
`0x4FE0` bytes. Que a base seja `sp+0x480` não é inferência — está escrito duas
vezes, e a segunda fecha com o tamanho conhecido do `Highlights`:

```
0x00042B28  add  x19, sp, #0x480        ; <- a base do contexto
0x00042B68  add  x0,  x19, #0x5f0
0x00042B70  mov  w2,  #0x3ef1           ; os 16.113 bytes do Highlights
0x00042B74  bl   memmove
```

`[BIN]` **O primeiro `0x68`.** `0x00042878`–`0x000428A0` copia `sp+0x4B90` para
`sp+0x480` inteiro, e `sp+0x4B90` foi carregado de `x4` — o **quinto argumento**
— em `0x000426F0`–`0x00042718`. Os cinco sítios de chamada (`0x00019CFC`,
`0x0002367C`, `0x0004248C`, `0x00042634`, `0x000592B4`) passam o mesmo struct.

`[BIN]` **O struct é `IconRendering.GlobalConfiguration`** (`fieldmd 0xA327C`,
treze campos). O encaixe é campo a campo e fecha em `0x68` bytes exatos:

| desloc. | campo | evidência no código |
|---|---|---|
| `+0x00` | `lightIntensity` `Double` | `ldr d0,[x1]; str d0,[x0]` (`0x0004DD64`) |
| `+0x08`..`+0x18` | `customLightDirection` (3 × `Double`) | `ldur q0,[x1,#8]` + `ldur q1,[x1,#0x11]` — 25 bytes lidos como dois `q` sobrepostos |
| `+0x20` | a **tag** desse `Optional` | `ldrb w8,[x20,#0x20]; cmp w8,#1` (`0x0004BE70`) |
| `+0x21`..`+0x23` | `effectsAreEnabled`, `drawMitigatedVersion`, `forceEnableEnhancedGlass` | três `ldrb`/`strb` seguidos |
| `+0x24`, `+0x25` | os dois `…UsesCAFilterForClearMode` | |
| `+0x26` | `allowHDR` `Bool?` | |
| `+0x28` | `enabledRenderingSteps` `Int` | **`and x8, x10, x8`** em `0x0004284C` — uma máscara de bits, e só ela é mascarada |
| `+0x30` | `_relativeIconInset` `Double?` | |
| `+0x40`, `+0x48` | `canvasSize` | `stp x25, x26, [sp,#0x40]` em `0x00023634`, e o ramo vizinho é `size / escala` (`fdiv`, `0x00023608`) |
| `+0x50` | `chicletDropShadow` `Bool?` | `strb w24,[sp,#0x50]` |
| `+0x51`..`+0x67` | `iconShape` | `ldur q0,[sp,#0xc1]; stur q0,[sp,#0x51]` |

> **E esse encaixe resolve DUAS perguntas de uma vez.** `ctx[0x20]`, que
> `0x0004BE74` compara com `1` para decidir se `phi` é zero, é a **tag do
> `Optional` de `customLightDirection`**. "`phi = 0`" não é um estado
> inventado nem um ramo conveniente: é `customLightDirection == nil`, que é o
> estado de quem não pediu luz custom.

`[BIN]` **O valor é `1.0`**, e vem de uma leitura independente que já estava
neste repositório: `Docs/_confrontar/kernels/default-ramps.md` leu o *outro*
inicializador do mesmo struct — o `lightAngle:` de `0x35DF0` — e registrou
`*param_1 = 0x3ff0000000000000` **assado no deslocamento `0x0` precisamente
porque aquele init não recebe intensidade**. Dois binários, dois inits, o mesmo
campo.

`[BIN]` E ele é lido em **um** lugar no slice inteiro (`0x0004C010`) e escrito
em **um** (`0x00042884`). Um escalar com um leitor só é a melhor forma possível
de erro — e não havia erro.

---

## 2. A pós-passagem `0x12550` é `spatialHighlighting`, tem 288 bytes, e está lida inteira

O laudo-mãe estimou "~1,5 KB". São **288** (`0x00012550`–`0x0001266F`), e ela
cabe num parágrafo. O que a escondia não era tamanho: eram os *stubs*.

`[BIN]` **Os stubs, nomeados pela tabela de símbolos indiretos** — e não pelo
que a forma parecia:

| stub | é | o que eu tinha lido antes de olhar |
|---|---|---|
| `0x8DD28` | **`_hypot`** | "atan2", pelos dois argumentos |
| `0x8DE00` | **`_sin`** | "alguma função de ângulo" |
| `0x8DDF4` | `_pow` | |
| `0x8DCEC` | `_atan2` | |
| `0x8DC68` | `___sincos_stret` | |
| `0x8DCE0` | `_asin` | |

> **Os dois primeiros eram o laudo inteiro.** Com `atan2` a passagem seria uma
> função do *azimute* do realce e atenuaria cada um dos cinco por um fator
> diferente — que é exatamente o desenho de "o realce de cima está forte
> demais". Com `hypot` ela é função da **latitude da luz** e trata os cinco
> igualmente. Uma leitura plausível e errada teria produzido um conserto
> bonito, com endereço, e falso.

`[BIN]` A função, com os seis campos de
`ICRRenderingParameters.SpatialHighlighting` (`params+0x258`, nomes de
`fieldmd 0xA48AC`) no `x0` e o `GlassHighlightSettings` recém-montado no `x20`:

```
t        = max(0, 1 - hypot(dir.x, dir.y) / sin(alignmentRange))   ; 0x12594-0x125B8
opacity *= minIntensity + (1 - minIntensity) * (1 - t^intensityPower)
                                                                   ; 0x125BC-0x125E0
spread  += t^spreadPower * (pi - spread)                           ; 0x125E4-0x12614
height  *= 1 + maxExtraHeight * t^heightPower                      ; 0x12618-0x12638
theta    = atan2(dir.x, dir.y)
dir      = (sin theta, cos theta, 0)                               ; 0x1263C-0x12650
```

O `pi` de `0x125F0`–`0x125FC` é um quarteto `mov`+`movk` soletrando
`0x400921FB54442D18` — a lição que este repositório já pagou seis vezes, e que
vale aqui também: ele **não está no *constant pool***.

E a ordem de campo bate com a ordem de uso, um a um: `alignmentRange` é o único
que passa por `sin`, `intensityPower` e `spreadPower` e `heightPower` são os
três expoentes, `minIntensity` é o **piso** do multiplicador, e `maxExtraHeight`
é o único que não é expoente e multiplica a altura. Seis nomes, seis papéis,
nenhum sobrando.

### 2.1. E por que ela não muda um pixel aqui — sem precisar dos seis números

`[OBS]` **Os seis valores continuam não lidos.** Não importam:

`resolveHighlight` monta `dir = (cos phi · sin theta, cos phi · cos theta,
sin phi)` (`0x0004C014`–`0x0004C030`). Logo `hypot(dir.x, dir.y) = |cos phi|`.
Com `phi = 0` isso é **1**. E `sin` de qualquer ângulo é no máximo 1, portanto
`1/sin(alignmentRange) ≥ 1` e

```
t = max(0, 1 - (algo >= 1)) == 0
```

`pow(0, p) == 0`, então o fator de opacidade colapsa em
`minIntensity + (1 - minIntensity) == 1`, o `spread` não ganha nada, a altura
não ganha nada, e a direção já era plana. **Identidade, para qualquer
`alignmentRange`, `intensityPower`, `minIntensity`, `spreadPower`,
`heightPower` e `maxExtraHeight`.**

O teste desta frente roda a passagem com quatro conjuntos de parâmetros
deliberadamente absurdos e exige que nenhum bit se mova.

---

## 3. A latitude `phi` não pode mover um realce — e isso é da própria passagem

`[BIN]` A última reescrita de `0x12550` é **incondicional e sem parâmetro**:

```
0x0001263C  mov v0, v12 ; mov v1, v13 ; bl atan2      ; theta = atan2(dir.x, dir.y)
0x00012648  bl  __sincos_stret
0x0001264C  stp d0, d1, [x20, #8]                     ; dir.x = sin, dir.y = cos
0x00012650  str xzr,    [x20, #0x18]                  ; dir.z = 0
```

O `atan2` joga fora o comprimento e o `str xzr` zera o `z`. Seja qual for
`phi`, **a direção que chega ao shader é sempre `(sin theta, cos theta, 0)`**.
`phi` só alcança o pixel por dentro de `t` — e `t` é zero aqui, pelo §2.1.

> Isto fecha o item 2 do `[OBS]` do laudo-mãe pelo lado que interessa: não é que
> `phi` "seja zero e por sorte não apareça". É que a passagem que roda depois
> **apaga a latitude por construção**. Um transcritor que guardasse o `cos(phi)`
> nas componentes planares teria uma direção mais curta, um `dot` menor e um
> realce que enfraquece com a luz alta — pelo motivo errado, e sem aviso.

---

## 4. Então onde está o "forte demais": a cor, não a geometria

Medidas as três, o exagero tinha de estar em outro lugar. A varredura por
`Highlights+0x90` — um dos portões que o laudo do especular listou como
`ctx+0x680` sem saber o nome — deu no ponto.

`[BIN]` `0x000494D8` lê `ctx[0x680]`, que é `Highlights+0x90`, que é
`glyphHighlightsUseVCM`. O laudo-mãe já tinha lido o valor: **`true`**
(`§4.1`). E ele parte a função de desenho em duas:

```
0x000495F8  ldr  w8, [sp, #0xb8]        ; useVCM
0x000495FC  cbz  w8, #0x498c4           ; FALSO -> a cauda simples
```

`[BIN]` **O ramo `useVCM == false`** (`0x000498C4` → `0x00049554`) monta um
descritor de três campos e desenha:

```
0x00049554  mov w8, #6 ; strb w8, [sp, #0x118]      ; tag 6 (2 para o darklight)
0x0004955C  ldr d0, [x19, #0x6f0]                   ; glyphHighlightNonVCMScale
0x00049568  str wzr, [sp, #0x110]                   ; 0 (1.0 no ramo do darklight, 0x49A58)
0x0004956C  str s0,  [sp, #0x114]
0x00049590  bl  #0xe834                             ; constrói e desenha o shader
```

`glyphHighlightNonVCMScale` e `glyphDarklightNonVCMScale` valem **`1.0`** os
dois (`Highlights+0x100` e `+0x108`, `§4.1` do laudo-mãe). **É este o ramo que
este renderizador implementa, e a escala dele é a unidade.** Força cheia, sem
polimento — que é, palavra por palavra, o que o usuário descreveu.

`[BIN]` **O ramo `useVCM == true`**, que é o que o alvo toma, não desenha o
shader por cima de nada. Ele desenha **dentro de uma camada** e usa o resultado
como **máscara**:

```
0x0004977C  [x22 save]                              ; x22 = ctx[0x520], o display list
0x00049784  [x22 beginLayer]
0x000497AC  bl #0xe834                              ; o glassHighlight, DENTRO da camada
0x000497BC  [x22 clipLayerWithAlpha:1.0 mode:0]     ; a camada vira recorte
0x000497D4  [x22 addContentHeadroom:<VCM[3]>]       ; pulado: VCM[4] == 1
0x00049800  [x22 addStyle:9 data:…]
```

(Os seletores vêm de `__objc_stubs`: `0x8F2C0` `save`, `0x8E460` `beginLayer`,
`0x8E620` `clipLayerWithAlpha:mode:`, `0x8E2A0` `addContentHeadroom:`,
`0x8E3A0` `addStyle:data:`.)

`[BIN]` E a cor que é pintada através dessa máscara sai de **transformadas de
cor compostas**, montadas dos cinco campos do `VCM` — que são cinco e não
quatro, e o quinto é um `Bool`. `0x00049500`–`0x00049548` escolhe os cinco
deslocamentos de uma vez, pelo bit `isDarklight`:

| | realce | darklight |
|---|---|---|
| `VCM[0]` | `ctx+0x6A0` = `Highlights+0xB0` = **0.2** | `ctx+0x6C8` = **−0.15** |
| `VCM[1]` | `+0x6A8` = **1.2** | `+0x6D0` = **0.7** |
| `VCM[2]` | `+0x6B0` = **1.25** | `+0x6D8` = **1.25** |
| `VCM[3]` | `+0x6B8` = **0.0** | `+0x6E0` = **0.0** |
| `VCM[4]` | `+0x6C0` = **`1`** (`Bool`) | `+0x6E8` |

e `0x00049A64`–`0x00049AB8` monta a matriz com dois deles escritos em claro:

```
0x00049A64  fsub s0, s14, s9 ; str s0, [sp, #0x360]   ; ganho  = VCM[1] - VCM[0] = 1.0
0x00049A74  str  s9,          [sp, #0x370]            ; viés   = VCM[0]         = 0.2
0x00049AB8  bl   #0x7064                              ; compõe com a transformada anterior
```

e `0x00049AE4`–`0x00049B24` acrescenta um terceiro passo de contraste em torno
de `0.5` com inclinação `VCM[2] = 1.25` (`s1 = 0.5 - 0.5·VCM[2]`).

`[OBS]` **O que falta para fechar, e por que não foi inventado aqui.** A
transformada-base que as outras duas compõem está em `0xE2960`, que é
`__common` — **memória zerada no load e preenchida em tempo de execução**, pelo
inicializador que `0x0004983C`–`0x0004984C` chama uma vez via o token de
`0xCE668` (o corpo está em `0x00049C78`). Ler os bytes do arquivo ali dá lixo, e
foi o que deram. Sem ela, sem a regra de composição de `0x7064` e sem a
semântica de `addStyle:9` e de `clipLayerWithAlpha:mode:` do RenderBox, montar a
cor seria escolher um número que ficasse bonito — que é precisamente o que este
pedido proibiu.

> **Este é o achado.** O realce deste renderizador não está forte porque algum
> escalar foi tomado como 1 por engano: **os três escalares que estavam em
> aberto eram 1 mesmo.** Ele está forte porque a *geometria* está certa e a
> *cor* está crua — branco puro somado em `PlusLighter`, onde o alvo pinta uma
> cor casada com o conteúdo através da mesma máscara.

---

## 5. O que foi entregue em código

- **`Source/RenderBox/GlassSpecular.h`.** `SpecularArguments::lightIntensity`
  (com a tabela de `GlobalConfiguration` e a proveniência do `1.0`),
  `SpecularArguments::lightLatitude`, e `SpatialHighlighting` — os seis campos
  com nome e endereço, mais o bit `read` que diz que os **valores** não foram
  lidos. `spatialHighlight()` declarada com o teorema do §2.1 em cima.
- **`Source/RenderBox/GlassSpecular.cpp`.** A direção passa a ser a fórmula
  completa com `cos phi`/`sin phi`; `spatialHighlight()` implementa as quatro
  reescritas; a opacidade multiplica por `lightIntensity`; e `resolveHighlight`
  aplica a reescrita de direção nos **dois** ramos, porque ela é a parte de
  `0x12550` que não depende de parâmetro nenhum.
- **`kBandWidth`.** O único número da transcrição que estava errado:
  `0.83349` (a decimal de onde a constante veio) virou **`0.8330078125`**, que
  é a `half` `0xH3AAA` decodificada — expoente `0xE`, mantissa `0x2AA`,
  `2^-1 · (1 + 682/1024)`. A multiplicação do shader é em `half`
  (`%15 = fmul fast half %14, 0xH3AAA`), então é este o valor pelo qual a borda
  da banda é dividida.
- **`out.notes`.** A frase do especular não fala mais em "duas identidades
  tomadas": ela diz que as três foram medidas, diz o que cada uma era, e nomeia
  com endereço a passagem de cor que **continua** por ler, para o usuário ver
  onde acaba o medido.
- **`Tests/test_glass_material.cpp`.** Dois casos. O primeiro trava as três
  identidades — e trava a terceira rodando a pós-passagem com quatro conjuntos
  de parâmetros absurdos, mais um caso com `phi` fora do plano que exige que ela
  **morda** nos três sentidos (senão "identidade" viraria "função vazia"). O
  segundo põe o joelho da banda em `w/2` e mostra que a decimal antiga o punha
  depois.

### A prova em pixel

Release (`cmake --preset release -DIC_BUILD_UI=OFF`, alvo `icrender`), 1024 px,
`--idiom square`, contando só pixels **visíveis** (alfa > 0 num dos dois):

| ícone | antes | depois | mudados | delta máx. | tempo |
|---|---|---|---|---|---|
| `GoWToolkit.icon` (o do usuário) | — | — | **10** de 987.176 visíveis (0,0010 %) | **1** | 0,324 s → **0,291 s** |
| `Apollo-Reborn…AppIcon` | — | — | **38** de 987.549 visíveis (0,0038 %) | **1** | 2,660 s → **2,479 s** |

**E esse número é o resultado, não a decepção.** Dez pixels e delta 1 é
exatamente o que "as três eram identidades" tem de parecer no pixel: se
`lightIntensity` não fosse 1, ou se `phi` não fosse 0, ou se `0x12550` mordesse,
o diferencial teria sido de dezenas de milhares. Os 10 e os 38 são só o joelho
da banda andando `5·10⁻⁴` — borda fina, forma intacta, que é a assinatura que
este pedido pediu para conferir.

---

## 6. O que continua `[OBS]`

1. `[OBS]` **A cor do VCM** — a transformada-base de `0xE2960`
   (`__common`, inicializada em `0x00049C78`), a regra de composição de
   `0x7064`, e a semântica de `addStyle:9` / `clipLayerWithAlpha:mode:` no
   RenderBox. **É aqui que "forte demais" mora**, e é a próxima frente.
2. `[OBS]` **Os seis valores de `spatialHighlighting`** (`params+0x258`). A
   função está lida; os números não. Provado que não podem importar enquanto
   `customLightDirection` for `nil`.
3. `[OBS]` **`shouldClampPlusLBlending`** (`ICRRenderingParameters`,
   `params+0x220` — logo antes de `defaultChicletCornerRadius` em `+0x228` e do
   `thresholds` que a frente da sombra leu em `+0x230`). O nome descreve
   exatamente a pergunta que esta composição levanta: `drawSpecular` soma em
   `PlusLighter` sem grampo em RGB, e três realces de opacidade 1 estouram o
   branco a ponto de os dois `dark` (`PlusDarker`, que vêm depois) não terem de
   onde descer. **O valor não foi lido e o consumidor não foi seguido**, então
   nenhum grampo foi posto.
4. `[OBS]` **`fill[+0x5B]`/`fill[+0x61]`**, os dois `VCM` do preâmbulo como
   dado (o §4 leu o *uso*, não o significado dos quatro números), o chiclet, e a
   codificação do texel do SDF — os itens 4 a 7 do laudo-mãe, intocados.

---

## 7. Resumo dos selos

| pergunta | resposta | selo |
|---|---|---|
| 1. `ctx[0]` | `GlobalConfiguration.lightIntensity`, campo `+0x00` do quinto argumento de `0x4266C`, copiado para a base do contexto em `0x00042884`; lido em **um** sítio no slice (`0x0004C010`) | `[BIN]` |
| — | o valor é `1.0` — assado como `0x3ff0000000000000` no init `lightAngle:` (`0x35DF0`), lido por `Docs/_confrontar/kernels/default-ramps.md` | `[BIN]` |
| — | e o mesmo encaixe dá que `ctx[0x20]` é a tag de `customLightDirection`: `phi = 0` é `nil`, não um ramo escolhido | `[BIN]` |
| 2. `0x12550` | `ICRRenderingParameters.spatialHighlighting`, **288 bytes**, quatro reescritas, seis parâmetros, todos os seis usados na ordem do metadado | `[BIN]` |
| — | é a **identidade** em `phi = 0` **para qualquer valor dos seis**, porque `hypot(dir.xy) = 1` e `sin ≤ 1` forçam `t = 0` | `[BIN]` |
| 3. `phi` | não pode mover realce nenhum: `0x1263C`–`0x12650` reescreve a direção como `(sin theta, cos theta, 0)` e zera `z` incondicionalmente | `[BIN]` |
| onde está o "forte demais" | na **cor**: `glyphHighlightsUseVCM` (`Highlights+0x90`) é `true` e leva a `beginLayer`/`clipLayerWithAlpha:`/`addStyle:` com `glyphHighlightVCM` `[0.2, 1.2, 1.25, 0.0, true]`; este renderizador está no ramo `false`, cuja escala é `glyphHighlightNonVCMScale = 1.0` | `[BIN]` |
| — | a transformada-base do VCM (`0xE2960`, `__common`, init em `0x49C78`) e `0x7064` | `[OBS]` |
| pixel | 10 de 987.176 (GoW) e 38 de 987.549 (Apollo) pixels visíveis, delta máximo 1 — só o joelho da banda, pela `half` `0xH3AAA` | `[BIN]` |

### Instrumentos

Nenhum MCP de RE — `list_instances` vazio pelo quarto laudo seguido. `dis`/`fn`/
`xref` de `scripts/macho.py`, `fieldmd_iconrendering.txt`,
`metallib-iconrendering/default_mod1.ll`, e duas leituras que o `macho.py` ainda
não fazia e que valeram o laudo: a **tabela de símbolos indiretos** (para dar
nome aos stubs de `__stubs` e `__objc_stubs` — foi assim que `_hypot` deixou de
ser "atan2") e a **tabela de seções** (para descobrir que `0xE2960` é `__common`
e que os bytes lidos dali eram lixo, e não a constante).
