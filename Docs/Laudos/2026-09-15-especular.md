# Laudo — o consumo de `hasSpecular` e `specularPlacement`

*2026-09-15. Alvo: o `[OBS]` mais antigo do `Docs/03-o-motor-de-render.md` §29.7
— `hasSpecular` "trafega literalmente por `ICRIconLayer.hasSpecular`; **nenhuma**
aritmética lida", `specularPlacement` "transporte lido; consumo não lido" — e o
item 3 do §29.8, que a frente da sombra deixou pela metade: "lido em `0x4922C`,
sem aritmética, colapsa num bit consumido uma vez em `0x494E8`; **onde essa alpha
desemboca não foi seguido**."*

**Seguido. E fecha — do lado do destino.** O especular desemboca no shader
`glassHighlight` do metallib do próprio `IconRendering`, por nome literal, e o
`hasSpecular` não é um campo sem aritmética: é o **portão de uma função inteira
de 3.068 bytes**, testado na terceira instrução dela.

**E não fecha — do lado dos números.** Os nove valores que esse shader consome
vêm de `ICRRenderingParameters.Highlights`, um bloco de **16.113 bytes** que
nenhum laudo deste repositório leu. O documento aporta um portão e um bit. Não
aporta uma única magnitude. **Por isso o especular não desenha neste commit, e
por isso o renderizador passa a dizer que não desenha em vez de calar.**

Todos os endereços são do slice
`References/2.0-125/out/slices/IconRendering.arm64` (VA == deslocamento de
arquivo), salvo onde o texto diz `default_mod0.ll`/`default_mod1.ll`, que são
`References/2.0-125/out/metallib-iconrendering/`.

---

## 1. A resposta curta

`[BIN]` `hasSpecular` é lido em **`0x49200`**, e o que ele faz é abrir ou fechar
a função `0x491C0`–`0x49DBC` inteira:

```
0x000491C0   ; entrada (fim em 0x00049DBC, 3068 bytes)
0x00049200   ldrb w8, [x0]         ; *** material.hasSpecular *** , descritor +0x00
0x00049204   cmp  w8, #1
0x00049208   b.ne #0x49cb8         ; -> epílogo. FALSO NÃO DESENHA NADA.
0x00049210   ldrb w8, [x20, #0x21] ; e um segundo portão, do contexto
0x00049214   cmp  w8, #1
0x00049218   b.ne #0x49cb8
0x0004922C   ldrb w20, [x0, #0x30] ; *** material.specularPlacement ***
0x00049230   ldr  d10, [x0, #0x38] ; a opacidade que a camada carrega  [INF]
```

`[BIN]` Essa função chama **`0xE834`** em dois sítios (`0x49590` e `0x497AC`), e
`0xE834` monta um shader **pelo nome**:

```
0x0000E92C   mov  x0, #0x6c67 / movk 0x7361,16 / movk 0x4873,32 / movk 0x6769,48
0x0000E93C   mov  x1, #0x6c68 / movk 0x6769,16 / movk 0x7468,32 / movk 0xee00,48
             ; small string de Swift, contagem 14 (0xEE):  "glassHighlight"
0x0000E960   bl   -[RBShader initWithLibrary:function:]
0x0000E990   ..  0x0000EAF8   dez setArgumentBytes:atIndex:type:count:flags:
0x0000EB94   bl   setCIFilterProvider:
0x0000ECE8   bl   setShader:bounds:flags:
0x0000ED00   bl   -[RBDisplayList drawShape:fill:alpha:blendMode:]
```

`[BIN]` Então: **não é `CAFilter`**, **não é** `addBlurFilterWithRadius:` como o
`blurStrength`, **não é** outro `drawShape:` cru como a sombra, e **não é** o
estilo `glass-highlight` do `RenderBox` — a nota do §29.5 continua valendo, o
`IconRendering` nunca usa o estilo 2 do vizinho. É uma das **sete peças do
metallib do próprio `IconRendering`** (§2 do doc 03), a primeira da lista, e o
nome dela sempre disse o que ela era.

> Isto **fecha o `[OBS] consumo` de `hasSpecular`** no §29.7. O campo não é
> "transporte sem aritmética": ele não sofre aritmética porque é um **portão**, e
> o portão foi encontrado.

---

## 2. A cadeia: do descritor de `0xC0` bytes até o `setArgumentBytes:`

`[BIN]` A função `0x491C0` recebe o **mesmo descritor** de `0xC0` bytes que a
frente da sombra identificou em `0x4A30C` — o que começa no próprio
`GlassMaterial`. Ela lê dele, no prólogo:

| desloc. | campo | endereço |
|---|---|---|
| `+0x00` | `hasSpecular` | `0x49200` |
| `+0x30` | `specularPlacement` | `0x4922C` |
| `+0x38` | o multiplicador comum de opacidade | `0x49230` |
| `+0x98` | o `RBDisplayList` alvo | `0x49234` |
| `+0xA0` | um valor com tag `0xFF` = ausente | `0x4921C`–`0x49228` |
| `+0xA8` | a escala do campo de distância | `0x49238` |

`[BIN]` O `+0x38` é **exatamente** o `[descritor+0x38]` que o laudo da sombra
mediu multiplicando a alpha da sombra em `0x4A070`: aqui ele multiplica a alpha
do especular em `0x495F0` (`fmul d0, d10, d0`). Duas frentes, o mesmo byte, o
mesmo papel. Continua `[INF]` que ele seja `Icon.Layer.opacity`.

`[BIN]` O `+0xA8` vira o argumento 7 do shader **pré-multiplicado por −2**
(`fmov d0, #-2.0 ; fmul d0, d9, d0`, `0xEA7C`–`0xEA84`), e o argumento 8 é a
constante `0.5` (`0xEAA8`). Esse par — `sdfScale` negado e `sdfZero = 0.5` — é
**o mesmo par que a frente da translucidez leu** em `0x100D8`/`0x1010C` para o
`simplifiedShapeAwareGradientMask`. Controle positivo cruzado entre dois shaders
diferentes lidos por dois caminhos diferentes.

### 2.1. Os dois arrays, e o que são

`[BIN]` A função percorre **dois** laços encaixados:

```
0x49498  mov w9, #0x58 ; madd x8, x10, x9, x8   ; passo 0x58 no array externo
0x4954C  x21 = [x8 + 0x50] + 0x60         ; ponteiro para o array interno
0x49594  x21 += 0x60                      ; passo 0x60, contagem em [ptr+0x10]
```

`[BIN]` O externo é **`IconRendering.IconRenderer.HighlightsPass`**, que o
metadado de campos descreve em duas partes (`fieldmd_iconrendering.txt`
`0xA3F3C` e `0xA3F98`): `constraints: Constraints` e `highlights: Array`. As
nove `Constraints` na ordem declarada dão o layout que os `ldrb`/`ldr` pedem,
sem folga:

| desloc. | campo | leitura |
|---|---|---|
| `+0x00` | `height` | — |
| `+0x08` | `curvature` | — |
| `+0x10` | `blendMode` | — |
| `+0x11` | **`isDarklight`** | `0x494A0` (`ldrb w11, [x8, #0x11]`) |
| `+0x18` | `bias` | — |
| `+0x20` | `color` (`RBColor`, 4 `float`) | — |
| `+0x30` | `opacity` | — |
| `+0x38` | **`outsetOpacity`** (carga do `Double?`) | `0x494A4` (`ldr d15, [x8, #0x38]`) |
| `+0x40` | **`outsetOpacity`** (tag do `Optional`) | `0x494A8` (`ldrb w9, [x8, #0x40]`) |
| `+0x48` | `inset` | — |
| `+0x50` | `highlights` (ponteiro do array) | `0x494AC` |

`0x50` de `Constraints` + o ponteiro = **`0x58`**, que é o passo medido.

`[BIN]` O interno é um array de **`IconRendering.GlassHighlightSettings`**
(`fieldmd 0xA2E20`, nove campos). O passo de `0x60` só fecha por causa de um
detalhe que é fácil errar: **`__C.ICRUnitCartesianCoordinates` tem TRÊS campos**,
não dois (`fieldmd 0xA2AD8`: `x`, `y`, `z`). Com `direction` valendo três
`Double`:

| desloc. | campo | o que a função faz com ele |
|---|---|---|
| `+0x00` | `opacity` | × `[descritor+0x38]` → `alpha:` do `drawShape:` |
| `+0x08` | `direction.x` | → `float2.x` do argumento 5 |
| `+0x10` | `direction.y` | → `float2.y` do argumento 5, **negado** (`fneg s6`, `0xEF8C`) |
| `+0x18` | `direction.z` | testado contra `0.0` (`0xEE58`, `0xEE64`) |
| `+0x20` | `spread` | testado contra **π** (`0xEE3C`–`0xEE50`) |
| `+0x28` | `bias` | → argumento 3, como `1/bias − 2` (`0xE9DC`–`0xE9EC`) |
| `+0x30` | **`height`** | → argumento 0, × escala |
| `+0x38` | **`inset`** | → argumento 1, × escala |
| `+0x40` | **`curvature`** | → argumento 4, se `inset >= 0` |
| `+0x48` | `color` (`RBColor`) | → argumento 6 |
| `+0x58` | `blendMode` | → `0x41F90` → `blendMode:` do `drawShape:` |

Tamanho `0x59`, passo `0x60`. ✔

> **A prova de que a régua está certa não é o tamanho, são os usos.** `spread`
> é o único comparado com π — que é o que se compara com um **cone angular** e
> não com uma opacidade. `direction.z` é o único testado contra zero — que é o
> que se testa numa direção de luz achatada em 2D. `height` é o divisor do
> gradiente e `inset` é o subtraendo da distância, e o shader chama os dois
> exatamente dessas duas coisas. Quatro nomes, quatro papéis, nenhuma sobra.

---

## 3. O shader, transcrito do AIR

`[BIN]` `default_mod1.ll:9-58` (`glassHighlight_v1`) desempacota o *argument
buffer* e é isto que amarra cada `setArgumentBytes:atIndex:` a um parâmetro:

```
idx 0  -> %2 do miolo         (height escalado)
idx 1  -> subtraído da distância ANTES do miolo   (inset escalado)
idx 2  -> %3                  (o cone de spread)
idx 3  -> %4                  (1/bias - 2)
idx 4  -> %5                  (curvature)
idx 5,6 -> %6  float2         (direction.x, -direction.y)
idx 7,8 -> %7  half4          (color)
idx 9  -> sdfScale            ( = -2 * [descritor+0xA8] )
idx 10 -> sdfZero             ( = 0.5 )
img    -> a textura do campo de distância
```

```
sd = (sdfZero - tex.r) * sdfScale - inset      ; %46..%48, POSITIVO POR DENTRO
n  = 1 - 2*tex.gb                              ; %50..%52, a normal
```

`[BIN]` E `_glassHighlight` (`default_mod1.ll:60-116`):

```
w      = clamp(fwidth(sd), 2^-10, 2.0) * 0.83349
band   = saturate(sd/w + 0.5) * saturate((height - sd)/w + 0.5)
k      = clamp((height - 1) * 0.5, 0, 1)
shade  = mix(1.0, 1 - saturate(sd/height), k*k * curvature * (3 - 2*k))
lit    = saturate((dot(direction, n) - spread) / max(1 - spread, 2^-10))
a      = lit * shade
out    = (band * a / max(1 + (1 - a) * bias', 2^-10)) * color
```

**`band` é a geometria inteira.** Como o `sd` que chega já vem deslocado de
`inset`, o brilho vive exatamente onde

```
inset  <=  distância  <=  inset + height
```

com antisserrilhado nas duas bordas. **`inset` é a âncora, `height` é a
espessura.** Nada mais decide *onde*.

---

## 4. Os três valores de `specularPlacement`, medidos

`[BIN]` O enum vira um bit em `0x4926C`–`0x492CC`:

```
0x4926C  cbz  w20, 0x49280     ; automatic (0) -> depende do contexto
0x49270  cmp  w20, #1
0x49274  b.ne 0x492CC          ; outside (2)   -> bit = 0
0x49278  mov  w8, #1           ; inside  (1)   -> bit = 1
0x49280  ; automatic: bit = (ctx+0x463 == 1) && (OR de ctx+0x470..0x490 == 0)
0x492BC  ;                     && (ctx+0x498 == 1)
```

`[BIN]` E o bit é gasto num lugar só, `0x495D8`–`0x495F0`, sobre a cópia do
elemento que vai virar o *argument buffer*:

```
0x495D4  stp  d1, d2, [sp, #0x150]   ; d1 = height (+0x30), d2 = inset (+0x38)
0x495D8  ldr  w8, [sp, #0xb0]
0x495DC  tbnz w8, #0, #0x495f0       ; bit=1 -> sai por cima, nada muda
0x495E0  fsub d0, d2, d1             ; inset - height
0x495E4  str  d0, [sp, #0x158]       ;   -> inset
0x495E8  str  xzr, [sp, #0x160]      ;   -> curvature = 0
0x495EC  mov  v0.16b, v15.16b        ;   -> alpha vem de constraints.outsetOpacity
0x495F0  fmul d0, d10, d0
```

`[BIN]` Casando com a `band` do §3:

| | intervalo de distância | curvatura | opacidade |
|---|---|---|---|
| bit 1 — **`inside`** | `[inset, inset + height]` | `curvature` | `settings.opacity` |
| bit 0 — **`outside`** | `[inset − height, inset]` | **`0`** | `constraints.outsetOpacity` |

**Mesma espessura, mesma âncora, ESPELHADOS em torno dela.** Os dois intervalos
encostam em `inset` e não se sobrepõem. E como `curvature = 0` faz
`shade == 1.0` identicamente, a faixa de fora é **chapada** e a de dentro
**decai com a profundidade**.

`[ART]` Controle independente, e ele vem do texto que a Apple mostra ao autor —
`GroupSpecularInspector` (`Docs/_confrontar/editor-ui.md`):

> *"Choose how highlights **align with each layer**, either **inside** or
> **outside**, or let Icon Composer decide **automatically**."*

É a mesma frase que a aritmética escreve.

### 4.1. O bit é METADE da decisão — a outra metade não é do documento

A suspeita do pedido ("um enum denso de 3 que colapsa num bit é suspeito: ou
dois casos são o mesmo, ou o bit é só metade da decisão") estava certa na
segunda alternativa. `[BIN]` `0x494E0`–`0x494FC`:

```
0x494E0  ldr   w12, [sp, #0xa4]   ; constraints.isDarklight
0x494E4  eor   w10, w12, #1
0x494E8  ldr   w11, [sp, #0x14]   ; o bit de specularPlacement
0x494EC  orr   w10, w11, w10
0x494F0  and   w9,  w9, #0xff     ; tag do Optional constraints.outsetOpacity
0x494F4  cmp   w9, #1
0x494F8  csinc w9, w10, wzr, ne   ; tag == 1 (nil) -> resultado = 1
0x494FC  str   w9, [sp, #0xb0]
```

Duas condições, **as duas forçando `inside`**, e nenhuma das duas é do
documento:

1. `[BIN]` **`isDarklight == false` força `inside`.** O `eor …, #1` põe um `1`
   literal no `orr`. Só o realce **escuro** pode sair para fora. Que
   `[constraints+0x11]` seja `isDarklight` não é só o metadado: o ramo que lê
   esse mesmo byte em `0x49A3C` escolhe `blendMode = 2` (**`multiply`**) quando
   ele é não-zero e `blendMode = 6` (**`screen`**) quando é zero (`0x49A44` e
   `0x49554`, numeração do §17.3). Um realce que multiplica é escuro; um que
   faz *screen* é claro. Nome, offset e mescla concordam.
2. `[BIN]` **`outsetOpacity == nil` força `inside`.** `Double?` com carga em
   `+0x38` e tag em `+0x40`; tag `1` é `none`. E faz sentido para trás: a
   opacidade de *outset* é **para** o caso de fora — sem ela não há com que
   desenhar fora.

> **Então os três casos NÃO são dois.** `inside` e `outside` são estados
> distintos e mensuráveis, e `automatic` é um terceiro: ele segue o estado de
> recoloração identidade — e segue-o **para `outside`**, que é a direção
> **oposta** à do portão de forma idêntica na sombra, onde sair da identidade
> força `neutral`. Duas frentes, o mesmo `ctx+0x470..0x498` contra
> `ctx+0x4E8..0x510`, sinais contrários.

`[OBS]` E continua valendo o que a sombra já registrou: **sei o que esses cinco
`Double` e o byte de portão FAZEM, não sei o que eles SÃO.** Não achei o
escritor.

---

## 5. Onde trava, com endereço

Esta é a parte que impede a implementação, e ela é grande e localizada.

`[BIN]` Todos os nove valores do §2.1 — `opacity`, `direction`, `spread`,
`bias`, `height`, `inset`, `curvature`, `color`, `blendMode` — descendem de
**`ICRRenderingParameters.Highlights`**, o campo `_highlights` em
`params+0x250`. O construtor padrão o monta assim:

```
0x0005EBA8   bl   #0x62a78             ; constrói o bloco em sp+0xf8
0x0005EBBC   bl   ___swift_instantiateConcreteTypeFromMangledNameV2
0x0005EBC0   mov  w1, #0x3f01          ; 0x10 de cabeçalho + 0x3EF1 de dados
0x0005EBC8   bl   swift_allocObject
0x0005EBD8   bl   memcpy(x2 = 0x3ef1)
0x0005EBE0   str  x20, [x19, #0x250]   ; -> params + 0x250
```

`[BIN]` **`0x3EF1` = 16.113 bytes**, produzidos por `0x62A78`–`0x63C1C`
(4.516 bytes de código). O metadado diz por que é tanto: `Highlights` guarda
**dez `HighlightsSet`** (`chicletDefault/Bright/Dim/Clear/Screened` e os cinco
`glyphs*`), cada um com **seis `HighlightSettings`** (`keySharp`, `keyDiffuse`,
`fillSharp`, `fillDiffuse`, `dark`, `rim`), cada um com **dez campos**. E de lá
até o `GlassHighlightSettings` que o shader recebe há ainda duas resoluções:
`0x5E194` → `0x5E59C` (556 bytes) e `0x4C314` (1.128 bytes).

**Nenhum desses números foi lido.** Nem um.

`[OBS]` Também não foram lidos:

1. Os dois portões de contexto que decidem se a função sequer roda: bits 5 e 6
   de `ctx+0x28` (`0x49254`, `0x49264`) contra os bits 0 e 1 do segundo
   argumento (`0x49404`–`0x49428`).
2. `ctx+0x680` (`0x494D8`), que escolhe entre o caminho **espacial** — o que
   passa por `beginLayer` / `addContentHeadroom:` / `addStyle:data:` com estilo
   `9` / `addColorMatrixFilterWithArray:flags:` / `drawLayerWithAlpha:`
   (`0x49750`–`0x49C6C`) — e o caminho **liso** (`0x498C4`).
3. `ctx+0x6F0` e `ctx+0x6F8` (`0x4955C`, `0x49A4C`), que viram o canal alpha de
   uma cor forçada `(1, 0, 0, α)` / `(1, 0, 1, α)` no caminho liso quando as
   duas formas comparadas em `0x40E60`/`0x6D6B0` divergem.
4. A codificação da textura do campo de distância — o `.r` já era `[OBS]` pelo
   laudo da translucidez, e aqui entram também o `.g` e o `.b`, que o shader lê
   como `1 − 2·gb` para montar a normal.

> **Por que isto é um resultado e não uma desculpa.** A hipótese que abriu esta
> frente era que o especular pudesse desembocar no `glassBackground_v1`, cujos
> 75 slots exigem o QuartzCore, que não está em `References/`. **Não é esse o
> caso**, e essa era a notícia boa possível: o shader é do `IconRendering`, está
> no `References/`, e o corpo dele está transcrito no §3 acima. A parede é
> outra, está do lado de cá, e tem tamanho e endereço. Uma transcrição que
> tivesse inventado nove números plausíveis para alimentar esse shader teria
> produzido um brilho bonito e errado em toda parte, e teria fechado o `[OBS]`
> mais antigo do documento com uma mentira.

---

## 6. O corpus decide se vale a pena, e qual caso é o comum

`[ART]` Sobre os 145 documentos de `References/corpus`:

| medida | valor |
|---|---|
| documentos com chave `specular` em algum grupo | **67** de 145 |
| valores de `specular` ao todo (todos em `groups[]`) | **103** |
| `true` | **64** |
| `false` | **36** |
| `"inside"` | **3** |
| `"outside"` | **0** |
| `specular-specializations` (listas por aparência) | **51** |

`[ART]` Então **67 documentos pedem um brilho que este renderizador não
desenha** — quase metade do corpus — e o caso comum é de longe `automatic`
(64 de 67 grupos ligados). O `outside`, que é o único dos três que muda a
geometria de forma visível por escolha do autor, **não aparece nenhuma vez**: o
único caminho até ele, hoje, é o `automatic` fora do estado identidade.

Isso refina a prioridade: fechar `Highlights` vale por 67 documentos; fechar a
distinção `inside`/`outside` a partir do documento vale, hoje, por três.

---

## 7. O que foi entregue em código

Deliberadamente pouco, e nada que desenhe.

- `Source/RenderBox/GlassSpecular.h` / `.cpp` (novos). Carregam o laudo onde um
  implementador vai tropeçar nele, e três coisas que **são** `[BIN]`: o portão
  (`documentAsksForSpecular`), o *fold* de `specularPlacement` com as duas
  recusas do §4.1 (`specularDrawsInside`), e a transformação da faixa
  (`specularBand`).
- `Source/RenderBox/IconRenderer.cpp`, num ponto só, logo depois do bloco da
  translucidez: quando um grupo pede especular, uma nota entra em `out.notes`
  nomeando o shader, o endereço do `drawShape:` e o bloco de 16.113 bytes que
  falta. `[ART]` Ela dispara nos 67 documentos do §6.
- `Tests/test_glass_material.cpp`: um caso que trava a forma da decisão — que
  as duas recusas forçam `inside`, que os três valores são três respostas, e
  que as duas faixas encostam em `inset` sem se sobrepor. Suíte: **601 casos,
  0 falhas**.

### A armadilha de índice, e por que ela NÃO aparece aqui

O pedido nomeou a armadilha do `SizeBasedValue` — enum `small 0 … display 3`
contra struct declarado `display, large, medium, small`, logo `valor[3 − classe]`
— e ela é real. **Nenhuma tabela `SizeBasedValue` aparece nesta cadeia.** Os
`0x60` bytes do `GlassHighlightSettings` são nove `Double` escalares, uma
`RBColor` e um byte; a classe de tamanho não indexa nada aqui. Fica escrito para
que a ausência seja uma leitura e não um esquecimento — e se `Highlights` for
lido um dia, é lá dentro (`HighlightSettings` tem seis campos
`<indirect-external>` do mesmo feitio) que ela deve ser procurada.

---

## 8. Resumo dos selos

| pergunta do pedido | resposta | selo |
|---|---|---|
| 1. onde o especular desemboca | shader **`glassHighlight`** do metallib do `IconRendering`; montado em `0xE834` (literal em `0xE92C`, `initWithLibrary:function:` em `0xE960`), desenhado por `-[RBDisplayList drawShape:fill:alpha:blendMode:]` em **`0xED00`**. Não é `CAFilter`, não é `addBlurFilterWithRadius:`, não é o estilo 2 do RenderBox | `[BIN]` |
| — | `hasSpecular` é o **portão** de `0x491C0`–`0x49DBC`, lido em `0x49200` — fecha o `[OBS] consumo` do §29.7 | `[BIN]` |
| 2. o que os três valores significam | `inset` é a âncora e `height` a espessura da faixa `[inset, inset+height]`; `inside` põe a faixa **para dentro** da âncora, `outside` **para fora** (`inset −= height`), com `curvature = 0` e `constraints.outsetOpacity` no lugar de `settings.opacity`. `automatic` é um terceiro caso: segue o estado identidade, **para `outside`** | `[BIN]` |
| — | o bit é metade: `isDarklight == false` e `outsetOpacity == nil` forçam `inside`, e nenhuma das duas é do documento | `[BIN]` |
| — | o layout de `GlassHighlightSettings` (passo `0x60`) só fecha porque `ICRUnitCartesianCoordinates` tem **três** campos | `[BIN]` |
| — | o corpo de `_glassHighlight`, transcrito de `default_mod1.ll:60-116`; `sdfZero = 0.5` e `sdfScale = −2·[desc+0xA8]`, o mesmo par da translucidez | `[BIN]` |
| 3. desenhou? | **não**, e por isto: `ICRRenderingParameters.Highlights`, `params+0x250`, **16.113 bytes** (`0x3EF1`, alocado em `0x5EBC8`, copiado em `0x5EBD8`), construídos por `0x62A78`–`0x63C1C`, mais as resoluções `0x5E59C` e `0x4C314`. Nenhum número lido | `[OBS]` |
| — | 67 de 145 documentos pedem especular; 64 `true`, 3 `"inside"`, **0 `"outside"`** | `[ART]` |

### Instrumentos

**Nenhum MCP de RE foi usado — nem estava disponível**, exatamente como o laudo
da sombra registrou: o Ghidra responde e `list_instances` devolve lista vazia.
Tudo aqui saiu de `scripts/macho.py` (`fn`, `dis`, `xref`, `syms`), de
`References/2.0-125/out/fieldmd_iconrendering.txt`, do IR do metallib em
`References/2.0-125/out/metallib-iconrendering/default_mod0.ll` e
`default_mod1.ll`, de uma leitura direta de constantes no arquivo do slice
(VA == deslocamento), e de uma varredura estrutural dos 145 `icon.json` do
corpus.
