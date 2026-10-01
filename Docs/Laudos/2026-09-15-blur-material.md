# Laudo — a superfície do `blur-material`: duas paredes caíram, a terceira era nossa, e o gabarito recusou a única leitura que sobrava

*2026-09-15. Alvo: o `[OBS]` que `Docs/Laudos/2026-09-15-desfoque.md` §6 deixou —
"o kernel fechou, a superfície não", com três coisas travando o pixel. Esta
frente fecha duas por leitura, responde a terceira (que nunca foi leitura do
alvo, e sim arquitetura nossa) e mede a que sobra até o ponto em que o gabarito
a derruba.*

**Zero pixel mudou neste commit, e isso é o resultado.** O ramo que roda está
lido, o recorte virou aritmética, o ponto do laço onde o composite de baixo está
pronto está achado — e a extensão, que é a última peça, foi **desenhada,
renderizada e medida contra o gabarito da Apple, que a recusou**. A
transcrição entra ligada aos testes e **desligada no desenho**, exatamente como
`BlendFormula.h` faz com o grampo de `plusLighter`.

Endereços de `References/2.0-125/out/slices/IconRendering.arm64` (VA ==
deslocamento de arquivo), salvo onde estiver escrito `RenderBox`.

---

> **ERRATA — 2026-10-01. O desfoque NÃO vê a arte do próprio grupo.** Este laudo
> lê `0x4A488` como o desenho do conteúdo do grupo. Relido duas vezes: `0x4A488`
> é `bl 0x49ED4` com `w1 = 0`, o desenho da SOMBRA (carrega `shadowStyle`,
> `shadowOpacity`, `[+0x38]` e `shadowImage` em `0x49F08`–`0x49F14`); o conteúdo
> é `0x4AC84`, chamado em `0x48C08` depois de `0x4A2D4` voltar. O desfoque vê o
> fundo mais a sombra do grupo, e `0x4A594` (`bl 0x1135C`) o recorta ao interior
> do SDF — recorte que a transcrição daqui omitiu. Os comentários do código
> (`BlurKernel.h`, `IconRenderer.cpp`) foram corrigidos; o desfoque continua
> desligado.

---

## 1. A função, inteira

`[BIN]` `0x4A2D4`–`0x4AC84` é **uma** função e os **dois** sítios de desfoque
moram dentro dela. Cinco chamadores (`0x43A84`, `0x44028`, `0x45DDC`, `0x48BD4`,
`0x48F30`) passam `x0` = um descritor de grupo de `0xC0` bytes copiado do array
Swift atrás de `ctx+0x400` — o passo é o literal `add x23, x23, #0xc0`
(`0x439C8`) — e `x20` = o contexto do renderizador.

`[BIN]` Os primeiros `0x38` bytes do descritor **são** o material de vidro de
oito campos do doc 03 §29.2: `+0x18` é `blurStrength`, `+0x20`
`refractionHeight`, `+0x28` `refractionStrength`. Não é encaixe: `0x4A598` usa
`+0x18` como o `b` de `min(b,1) × [x20+0x250]`, que é a conta do desfoque que o
doc 03 §29.4 já tinha lido em `0x4A948`.

## 2. Parede (1) — qual ramo roda: `refractionStrength`, e é sempre o primeiro

`[BIN]` A função carrega os dois escalares em `0x4A30C`–`0x4A310` e ramifica
neles e em mais nada:

```
0x4A404  fcmp d13, #0.0 ; b.le 0x4A5DC      ; blurStrength <= 0 ?
0x4A40C  fcmp d1,  #0.0 ; b.ne 0x4A82C      ; refractionStrength != 0 ?
```

| `blurStrength` | `refractionStrength` | destino | camada |
|---|---|---|---|
| `> 0` | `== 0` | **`0x4A418`** | `beginLayerWithFlags:` **1**, corpo **vazio** |
| `> 0` | `!= 0` | `0x4A82C` | flag **`0x80`** em volta do corpo da refração |
| `== 0` | `!= 0` | `0x4A5DC` | flag 1 em volta do corpo, **sem** desfoque |
| `== 0` | `== 0` | `0x4A34C` | desenho simples, sem camada |

`[ART]` **E `refractionStrength` é zero para todo documento que existe.** Não é
chave de `.icon`: o default da constante de oito campos (`0x93B30`, doc 03 §29.2)
põe `refractionStrength = 0.0`, e um `grep -rl refraction` sobre os **145**
documentos do corpus devolve **zero arquivos** — assim como o gabarito `27.0-129`
e o `GoWToolkit.icon` do usuário. **Todo documento toma o primeiro ramo.**

O ramo é o do `0x4A5B4`: `save`, `clipShape:alpha:mode:` (`0x4A580`),
`addBlurFilterWithRadius:opaque:` (`0x4A5B4`), `beginLayerWithFlags:1`
(`0x4A5C0`), `drawLayerWithAlpha:1.0 blendMode:0` **imediato** (`0x4A5D0`),
`restore`. Nada é desenhado dentro da camada.

`[BIN]` A flag 1 é o bit 0, que o serializador XML do RenderBox chama
**`needs-background`** (`RB::XML::DisplayList::begin_layer` `0xE9E78`, atributo
em `0xE9F48`), e que `Docs/Laudos/2026-09-15-realce-vcm-fechado.md` leu por um
segundo caminho independente: `Builder::null_style_draw` (`0xCDD80`, `tbz` em
`0xCE02C`) aloca um `BackdropFilterItem` na camada **pai**. Duas leituras que
não compartilham caminho dizem a mesma coisa. `0x80` é
`ignored-by-needs-background` (`0xE9F08`).

### 2.1. A ordem, que é o formato inteiro do efeito

`[BIN]` O conteúdo do grupo é desenhado **antes** da camada — `bl 0x49ED4` em
`0x4A488` precede o `0x4A48C` que abre o `save`. Logo **o fundo que a camada
precisa INCLUI a arte do próprio grupo**: o grupo é desfocado junto com tudo o
que está embaixo dele, não meramente por cima.

## 3. Parede (2) — o recorte: seis `CGRect` e um pixel

`[BIN]` `0x4A4A4`–`0x4A56C`, com **todos** os stubs resolvidos pela **tabela de
símbolos indiretos** (a lição que quase custou um conserto falso hoje):

| stub | símbolo |
|---|---|
| `0x8DA10` | `_CGRectGetMinX` |
| `0x8DA1C` | `_CGRectGetMinY` |
| `0x8DA28` | `_CGRectGetWidth` |
| `0x8D9D4` | `_CGRectGetHeight` |
| `0x8DA58` | `_CGRectOffset` |
| `0x8DA34` | `_CGRectInset` |

Seis `double` entrando em cada um dos dois últimos é o que faz
`CGRectOffset(r,dx,dy)` e `CGRectInset(r,dx,dy)` baterem com os registradores de
argumento. Transcrito:

```
frame  = CGRect em descritor +0x70 .. +0x88
canvas = CGRect em ctx       +0x558 .. +0x570
r = CGRectMake(MinX(frame)*W(canvas),  MinY(frame)*H(canvas),
               W(frame)*W(canvas),     H(frame)*H(canvas))
r = CGRectOffset(r, MinX(canvas), MinY(canvas))
r = CGRectInset (r, -[ctx+0x46A8], -[ctx+0x46A8])
[shape setRect:r] ; [list clipShape:shape alpha:1 mode:0]
```

Um frame multiplicado pelo TAMANHO do canvas e deslocado pela ORIGEM dele é um
frame em coordenadas **unitárias**. É o que a multiplicação significa, e é por
isso que o retângulo do canvas está na expressão.

### 3.1. O canvas é `(0, 0, 1024, 1024)`

`[BIN]` `0x4291C`–`0x42968`, dentro do **mesmo construtor de contexto** que a
frente do realce leu: a origem é `movi v0.2d, #0` gravado em `ctx+0x558`
(`0x4295C`–`0x42960`) e o tamanho é

```
0x4291C  d0 = (double)w ; d1 = (double)h ; d2 = (double)s
0x42928  d0 = w/s ; d1 = h/s ; d2 = min(d1, d0)
0x42940  mov x8, #0x4090000000000000        ; 1024.0, IMEDIATO, fora do pool
0x4294C  ctx+0x568 = 1024*(w/s)/min
0x42954  ctx+0x570 = 1024*(h/s)/min
```

O `s` se cancela: é `1024 · w / min(w,h)`. **Para um ícone quadrado, `1024 ×
1024`.**

### 3.2. `[ctx+0x46A8]` é o MESMO que a frente do realce leu — e é UM pixel

`[BIN]` `Docs/Laudos/2026-09-15-realce-forma.md` §4.1 leu `0x42D3C`–`0x42D50`
como `ctx+0x46A0 = 1/escala` e `ctx+0x46A8 = (1/escala)/contentsScale`, com
`escala = min(rectW/canvasW, rectH/canvasH)`. **É o mesmo `self`**, por três
verificações que não são o encaixe:

1. o construtor toma o endereço do próprio quadro em **`x20`** (`0x42F40`,
   `add x20, sp, #0x480`) e grava `sp+0x9A0`, `sp+0x9E8` e `sp+0x9F0` — que são
   `ctx+0x520`, `+0x568` e `+0x570`, **exatamente** a display list e o tamanho do
   canvas que este sítio de desfoque carrega de `x20`;
2. uma varredura de **todo** `ldr`/`str` de offset sem sinal `#0x46A8` em
   `__text` acha **onze**, dez na mesma convenção de registrador e um
   (`0x4EC38`–`0x4EC3C`) uma cópia campo-a-campo entre dois objetos do mesmo
   tipo;
3. `0x4BEAC` é o uso da própria passagem de realces, quatro instruções do
   `0x4BF00` que aquele laudo nomeia.

Então `[ctx+0x46A8]` é **unidade de canvas por pixel**, e `CGRectInset` pelo
**negativo** dele é um **AFASTAMENTO de exatamente um pixel de dispositivo** em
cada lado. É guarda de sangramento, não corte. **A parede (2) caiu**: o recorte
é aritmética, e nas unidades deste renderizador é `frame × size` crescido de um
pixel.

### 3.3. O que sobra da parede (2): o FRAME

`[OBS]` O retângulo em descritor `+0x70` continua sem leitura. Ele é unitário
pela aritmética que o consome, é copiado em bloco pela passagem que funde grupos
(`0x48724`), e o array em que vive pendura em `ctx+0x400` — **quem escreve
aqueles quatro `double` não foi seguido**.

## 4. Parede (3) — onde o composite de baixo está pronto: era nossa, e está respondida

Nunca foi leitura do alvo. `IconRenderer.cpp` compõe grupos de trás para frente
num único acumulador pré-multiplicado `acc`, então **no fim do laço de camadas
de um grupo** `acc` vale precisamente "tudo que está embaixo deste grupo, mais
este grupo" — que é, pela ordem de §2.1, precisamente o fundo que a camada do
alvo precisa. A chamada vai nessa linha, logo antes do `blendPremulOver`.

O único caso em que isso **não** vale é um grupo com `blend-mode` não-normal:
ele desenha num alvo próprio e o acumulador não está embaixo dele. Desfocar um
alvo de grupo vazio seria o desenhar-silenciosamente-errado que esta torre
recusa, então esse caso é **recusado por nome**
(`kBlurMaterialBlendedGroupNote`) — a mesma forma de recusa que
`groupWouldRefract` já usa.

## 5. O que foi desenhado, medido e depois desligado

Com §2, §3 e §4 sobra **uma** incógnita: o frame. A única leitura que esta frente
tinha para ele era o **retângulo unitário** — o canvas inteiro. Ela foi
implementada, renderizada e medida.

`AppIcon-27` a `--size 412`, posto no recuo legado dentro do quadro de 512
(`scripts/png-diff.py --legacy-inset`), contra
`References/27.0-129/out/apple-512.png`:

| | R | G | B | A | pixels diferentes |
|---|---|---|---|---|---|
| **sem desenhar o desfoque (controle)** | **8,95** | **10,03** | **9,89** | **4,95** | 160.327 |
| desfoque, frame unitário | 15,58 | 20,09 | 22,49 | 7,09 | 189.731 |
| diagnóstico: só a cor, alpha preservado | 14,35 | 18,90 | 21,33 | 4,95 | 187.732 |

E os **quatro cantos do squircle** são os piores blocos 8×8 do quadro
(`(88,56)` 183,8; `(416,56)` 182,3; `(56,88)` 181,6; `(448,88)` 180,2).

### 5.1. O perfil de luma, que diz a mesma coisa em forma

Coluna central do quadro de 512, do topo do ícone para baixo:

| linha | 50 | 52 | 54 | 56 | 58 | 60 | 65 | 70 | 80 | 90 |
|---|---|---|---|---|---|---|---|---|---|---|
| **Apple** | 157 | 133 | 98 | 76 | 53 | **49** | 49 | 49 | 49 | 49 |
| **nós, sem desfoque** | 202 | 124 | 75 | 65 | 59 | 55 | **49** | 49 | 49 | 49 |
| **nós, com desfoque** | 84 | 84 | 83 | 81 | 80 | 78 | 74 | 69 | 62 | 62 |

O gabarito cai `157 → 49` em nove linhas e então **segura um 49 chapado** — o
topo escuro do gradiente de fundo. O nosso sem desfoque cai `202 → 49` em quinze
e segura o mesmo 49. **O nosso com desfoque é chapado em 84 e nunca chega a 49.**
Um desfoque de fundo do tamanho do canvas com `σ = 0,56 × 64 = 35,84` unidades de
canvas (14,4 px a 412) **apaga estrutura que o alvo guarda**.

### 5.2. Portanto: a medida entra, a correção não

O gabarito tem direito de desempatar entre duas **leituras**, e desempatou: o
frame **não** é o retângulo unitário. Qual é, esta frente não sabe. Aplicar
qualquer outro seria escolher extensão pelo diff em 46 documentos — o
plausível-e-errado que o próprio laudo do desfoque já recusou uma vez.

`blurMaterialSurface` e `drawBlurMaterial` entram em `BlurKernel.h/.cpp` como
**transcrição fixada por testes e DESLIGADA no desenho**, que é o que
`BlendFormula.h` já faz com `shouldClampPlusLBlending`. `IconRenderer.cpp` calcula
a superfície e emite a nota; não chama o desenho.

## 6. O `opaque:`, que é o único candidato que sobrou e não fechou

`[OBS]` O argumento `opaque:` é `mov w2, #1` nos **dois** sítios (`0x4A5B0`,
`0x4A95C`). Ele entra nas flags do filtro em `0x3E8D4`, fica no bit 0 de
`GaussianBlur+0x18` (`RenderBox 0xFE5C0`), é copiado para `renderer+8` em
`0xFECE8`–`0xFECEC`, e `BlurRenderer::render` o lê **uma única vez**, em
`0xFFF98`: se vale `1` **e** esta é a **última** passada (`[x20+0x1c]` acabou de
ser decrementado a zero, `0xFFF8C`–`0xFFFB0`), põe `w23 = 0x10`, que
`0xFFFF8`–`0x100004` dobra no estado de render empacotado como **bit 20**.

O que o bit 20 de um `RB::RenderState` faz não foi seguido. **É a única coisa em
toda a cadeia que poderia impedir um desfoque de amolecer uma silhueta**, e é por
isso que fica escrito aqui com endereço em vez de virar palpite.

## 7. O censo, recontado pela terceira vez

`[ART]` Direto de `IC_CORPUS_DIR`, 145 documentos:

* **123** chaves `blur-material`, em **123 grupos** sobre **73 documentos**;
* **75** números, **todos positivos**, de `0,05` a `1,0` (= `3,2` a `64`
  unidades de canvas de raio), em **46 documentos**;
* **48** `null` explícitos.

Bate com a recontagem do laudo do desfoque, dígito por dígito. `[ART]` E o ícone
do usuário (`GoWToolkit.icon`) tem **zero** ocorrências — controle negativo de
graça, e ele não se move.

## 8. O que este commit entrega

* `Source/RenderBox/BlurKernel.h` — a transcrição inteira de §1–§6, incluindo o
  `[OBS]` do frame com a medida que o derrubou e o `[OBS]` do `opaque:` com
  endereço.
* `Source/RenderBox/BlurKernel.cpp` — `blurMaterialSurface` (o recorte) e
  `drawBlurMaterial` (a composição `source-over` sobre pré-multiplicado), mais
  duas notas: a do frame e a da recusa do grupo mesclado.
* `Source/RenderBox/IconRenderer.cpp` — **um** toque, num ponto só: a superfície
  é calculada no fim do laço do grupo e a nota é emitida; o desenho **não** é
  chamado, com o porquê escrito na linha.
* `Tests/test_blur_material.cpp` — três casos sobre a aritmética desligada.

**Pixel:** `AppIcon-27` a 412 e `GoWToolkit` a 1024 saem **byte a byte iguais** a
`390d2e9` — o SHA-256 do `AppIcon-27` é
`0DAB331C145D9FF96CAE7560D1B477A0E9131EBF22C45BCC1E0F04F963683A23` e bate com o
render de controle.

**Tempo, Release, `-DIC_BUILD_UI=OFF`:** `AppIcon-27` a 412 px **0,262 s**,
`GoWToolkit` a 1024 px **0,448 s** — iguais aos de `390d2e9`, como tem de ser
quando nada desenha. `[ART]` E, para quem for ligar isto: com o desfoque
DESENHADO em canvas inteiro nos três grupos do `AppIcon-27`, o mesmo render custou
**0,310 s** — `+36 ms` para três desfoques de `σ = 14,4 px` sobre 412², ou ~12 ms
cada. A escada de qualidade de `Docs/Laudos/2026-09-15-desfoque-escada.md` é o que
torna isso pagável; sem ela seriam 81 taps por eixo na grade cheia.

**Suíte:** `mingw`, `-DIC_BUILD_UI=OFF`, `IC_CORPUS_DIR` posta —
**649 casos, 0 falhas** (os 646 de `390d2e9` mais os três novos).

## 9. Resumo dos selos

| pergunta | resposta | selo |
|---|---|---|
| `[self+0x46A8]` é o mesmo da frente do realce? | **sim**, e é unidade de canvas por pixel; o construtor põe o próprio quadro em `x20` (`0x42F40`) e grava `ctx+0x520/0x568/0x570`, que é o que este sítio lê | `[BIN]` |
| qual dos dois ramos roda? | **sempre o de `0x4A5B4`**, flag 1 `needs-background`, corpo vazio — porque a escolha é `refractionStrength` e nenhum dos 145 documentos, nem o gabarito, nem o ícone do usuário tem chave de refração | `[BIN]`+`[ART]` |
| o que é desfocado? | o composite de baixo **mais a arte do próprio grupo**: `0x4A488` desenha antes de `0x4A48C` abrir a camada | `[BIN]` |
| o recorte | `frame × canvas(0,0,1024,1024)`, afastado de **um pixel**; seis stubs de `CGRect` resolvidos pela tabela de indiretos | `[BIN]` |
| onde no laço? | fim do laço de camadas do grupo, antes do `blendPremulOver`; grupo mesclado é recusado por nome | arquitetura nossa |
| desenhou? | **não.** O frame (`descritor+0x70`) não está lido e o gabarito recusou o retângulo unitário: 8,95/10,03/9,89 → 15,58/20,09/22,49, com os quatro cantos nos piores blocos e o perfil chapado em 84 onde o alvo segura 49 | `[OBS]` |
| o que poderia explicar? | `opaque:1`, que acende o **bit 20** do estado de render da última passada (`0xFFF98` → `0x100004`) e cujo efeito não foi seguido | `[OBS]` |
| quanto custaria? | **+36 ms** em três grupos a 412 px, pela escada de qualidade | `[ART]` |
