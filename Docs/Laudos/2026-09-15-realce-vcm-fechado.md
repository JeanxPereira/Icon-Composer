# Laudo — o bit 0 é o fundo, e o realce passa a filtrar em vez de somar branco

*2026-09-15. Alvo: o `[OBS]` 1 de `2026-09-15-realce-vcm.md` — se
`beginLayerWithFlags:1` faz do `addColorMatrixFilterWithArray:` um
`BackdropFilterItem<Filter::ColorMatrix>`, isto é, se a matriz lê o fundo. O
pedido do usuário continua: **"o efeito atual tá duro demais, muito forte, falta
ajuste e polimento baseado em funções reais"**.*

**O bit foi lido, e é o que parecia.** O bit 0 de `RB::DisplayList::Layer::Flag`
é o bit de **fundo**. Com ele lido o realce foi pintado como o alvo pinta:
`lerp(fundo, VCM(fundo), cobertura)`. Pela primeira vez há um gabarito da Apple
para medir: **na banda do realce o viés de brilho vai de +34,3 (branco) para
+0,10 (VCM).** O erro médio absoluto nessa banda cai à metade, mas continua
**acima** do de não desenhar realce nenhum. §4 dá os números e diz o que isso
significa.

Endereços de `References/2.0-125/out/slices/RenderBox.arm64` (tem símbolos, VA ==
deslocamento de arquivo) salvo onde está escrito `IconRendering`.

---

## 1. Da mensagem ObjC ao `Layer+0x44`

`[BIN]` `-[RBDisplayList beginLayerWithFlags:]`, `0x0003BCA0`:

```
0x0003BCB4  and  w8, w2, #4
0x0003BCBC  mov  w9, #0x7b
0x0003BCC0  sub  w8, w9, w8            ; 0x7B, ou 0x77 se o bit 2 veio ligado
0x0003BCC4  and  w8, w8, w2
0x0003BCC8  orr  w9, w8, #0x80
0x0003BCCC  mov  w10, #0xa0
0x0003BCD0  tst  w2, w10
0x0003BCD4  csel w2, w8, w9, eq        ; |0x80 só se 0xA0 veio
0x0003BCDC  bl   Builder::begin_layer(State const&, OptionSet<Layer::Flag>)   ; 0x000C9A28
```

Com `w2 = 1`: `0x7B & 1 = 1`, `1 & 0xA0 = 0` → **passa `1` intacto.**

`[BIN]` `Builder::begin_layer` (`0x000C9A28`) chama `make_layer(OptionSet<Flag>)`
(`0x000C9940`, com `mov w1, w21` em `0x000C9A6C`), que aloca `0x58` bytes e salta
para `Layer::Layer(unsigned, OptionSet<Flag>)` (`0x0014DB74`):

```
0x0014DB98  stp  w1, w2, [x0, #0x40]   ; +0x40 = id, +0x44 = FLAGS
```

## 2. Quem lê o bit 0, e o que faz com ele

`[BIN]` Varredura do `__text` por chamadas virtuais `ldr xN, [xM, #0x70]` seguidas
de `blr xN`: oito sítios. Dois são `Builder::apply_filter_` e `lower_color_fn`
(slot `+0x70` da vtable de **`Item`**, que é `append_color_fn`, outra classe). O
que interessa é `Builder::null_style_draw(Item*, Layer&, OptionSet<DrawFlag>)`
(`0x000CDD80`) — onde um item de camada terminado é posto na camada pai:

```
0x000CDE54  ldr  x22, [x21, #0x38]     ; x22 = Layer* do LayerItem
...
0x000CE028  ldr  w8, [x22, #0x44]      ; Layer::Flag
0x000CE02C  tbz  w8, #0, 0xCE050       ; bit 0 DESLIGADO -> append comum
0x000CE030  ldrb w10, [x19, #0x44]     ; flags da camada PAI
0x000CE034  tbnz w10, #7, 0xCE050
0x000CE038  ldr  s0, [x22, #0x38]
0x000CE03C  fcmp s0, #0.0
0x000CE040  b.ne 0xCE050
0x000CE044  ldrb w10, [x21, #0x40]     ; Layer::Effect do item
0x000CE048  tst  w10, #3
0x000CE04C  b.eq 0xCE160               ; -> ramo de FUNDO
0x000CE160  ldr  x0, [x22, #0x18]      ; a lista de filtros da camada
0x000CE164  cbz  x0, 0xCE190
0x000CE168  ldr  x9, [x0, #8]          ; um filtro só
0x000CE16C  cbnz x9, 0xCE050
0x000CE170  ldr  x8, [x0]
0x000CE174  ldr  x8, [x8, #0x70]       ; slot 14
0x000CE178  mov  x1, x20               ; Builder&
0x000CE17C  blr  x8                    ; make_backdrop_item(Builder&)
...
0x000CE1A4  bl   Heap::alloc<BackdropFilterItem<Filter::ColorMatrix>>  ; sem filtro
0x000CE1C4  adrp x8, 0x18b000 ; add x8, #0xa0 ; str x8, [x0]           ; vtable 0x18B0A0
...
0x000CE20C  bl   Builder::append(Layer&, Item*)   ; x1 = x19, a camada PAI
0x000CE210  str  xzr, [x22, #0x18]     ; o filtro foi consumido
```

`[BIN]` **`+0x18` é a lista de filtros, não chute:** `Builder::apply_filter_`
termina em `bl Layer::append_filter(LayerFilter*)` (`0x0014E0E0`, chamado de
`0x000CD418`).

`[BIN]` **O slot `+0x70` é `make_backdrop_item`, pela vtable.** A vtable de
`GenericFilter<Filter::ColorMatrix>` (`__ZTV…`, `0x0018B658`) lida entrada a
entrada bate com a ordem dos símbolos: entrada 16 = `0x0007E850` =
`GenericFilter<ColorMatrix>::make_backdrop_item`, entrada 15 = `can_render_inline`.
As entradas começam em `+0x10`, então o deslocamento da chamada é
`(16 − 2) × 8 = 0x70`.

`[BIN]` **O item de reserva é o que o laudo anterior nomeou.** A vtable que
`0x000CE1C4` grava (`0x0018B0A0`) começa com `0x0007D798` — o
`BackdropFilterItem<Filter::ColorMatrix>` citado lá.

`[BIN]` E o mesmo bit bloqueia a fusão em linha: `0x000CDEEC ldr w11, [x22, #0x44]`,
`0x000CDEF0 tbnz w11, #0, 0xCDF28` pula o caminho que achataria a camada.

> **Em uma frase:** o bit 0 de `Layer::Flag` transforma o filtro instalado na
> camada num item que filtra **o que já está na camada pai**. A matriz
> `addColorMatrixFilterWithArray:` de `IconRendering 0x00049C48` lê o fundo.

**Stubs:** esta leitura não passa por stub nenhum de biblioteca — todos os `bl`
citados vão a funções com símbolo dentro do próprio `RenderBox.arm64`, e a única
chamada indireta (`blr x8` em `0x000CE17C`) foi resolvida pela vtable, não pelo
nome.

## 3. O que foi pintado

`[BIN]` As duas matrizes, reconferidas byte a byte no `__const` do
`IconRendering.arm64` (`0x938E0`..`0x9391F` e `0x93920`..`0x9395F`, linha alfa
`[0,0,0,1,0]` de `0x938D0`), estão em `GlassSpecular.cpp` como `float`.

`[BIN]` Por realce, no ramo `glyphHighlightsUseVCM == true` (`SpecularArguments::useVCM`):

- a forma do `glassHighlight` vira **recorte** (`clipLayerWithAlpha:1.0 mode:0`,
  `0x000497BC`): a cor e o `blendMode` do desenho da forma não chegam ao pixel,
  só a cobertura `f × opacity` (com `opacity` já incluindo `[descriptor+0x38]`);
- o fundo, **desmultiplicado**, passa por BT.709 RGB→YCbCr, `Y ← (V1−V0)·Y + V0`,
  `Cb,Cr ← V2·c + (0.5 − 0.5·V2)`, YCbCr→RGB;
- `glyphHighlightVCM = [0.2, 1.2, 1.25, 0.0, true]` nos três claros e
  `glyphDarklightVCM = [−0.15, 0.7, 1.25, 0.0, true]` nos dois escuros (por
  `HighlightSlot::isDarklight`);
- **sem grampo**: `VCM[4] == 1` pula o `ColorClamp` (`0x000497C0`);
- resultado `lerp(fundo, VCM(fundo), cobertura)`, alfa do fundo intacto.

`[BIN]` **E o "fundo" deixou de ser o buffer da camada.** Antes o especular era
composto *dentro* de `drew->rgba` (buffer da camada, **straight** — `blendOver`
multiplica por `a`) e só então a camada ia ao `target`. Com a matriz lendo a camada
pai, o especular vai **depois** do `blendOver`, sobre `target` (premultiplicado),
com `layerOpacity = opacity`. É a mudança em `IconRenderer.cpp`, a mesma nos dois
ramos que desenham especular (vetor e raster) — dois sítios gêmeos, não dois
pontos de desenho diferentes.

`[INF]` `mode:0` é recorte por **alfa**: um recorte por luminância apagaria os
dois escuros, cuja cor é preta. As strings `alpha`/`clip-mode` são vizinhas em
`0x16F0E8`/`0x16F0EE`, mas a tabela não foi lida.

`[INF]` O `lerp` com alfa preservado é exato onde o fundo é opaco (é o
`drawLayerWithAlpha:1.0 blendMode:0` da cópia filtrada através do recorte). Na
franja com alfa < 1 fica aberto se o `BackdropFilterItem` substitui ou compõe
por cima (§5).

`[OBS]` **O caminho mexido é o que desenha o que se mede?** Sim, e há controle:
um build temporário com `drawSpecular` retornando cedo produziu `none`. As
diferenças medidas abaixo são `after − none` e `before − none` sobre as mesmas
camadas, e a região de fundo cinza do oráculo fica **idêntica** nos três
(6,144 de delta médio), então nada vazou para fora do glifo.

## 4. A prova

Release (`cmake --preset release -DIC_BUILD_UI=OFF`, `icrender`), só pixels
visíveis (alfa > 0 num dos dois).

### 4.1 Antes × depois

| ícone | visíveis | mudados | Δ ≥ 16 | Δ máx. |
|---|---|---|---|---|
| `GoWToolkit.icon` 1024 `--idiom square` | 987.176 | **59.790 (6,06 %)** | 32.549 | **203** |
| `Apollo-Reborn…AppIcon` 1024 `--idiom square` | 987.549 | **143.454 (14,53 %)** | 79.912 | **206** |
| `AppIcon-27-defs.icon` 412 `--appearance dark` | 159.964 | 14.553 (9,10 %) | 8.300 | 194 |

Tempo interno do `icrender` (`drawn in`), Release: GoW **0,482 s → 0,438 / 0,483 /
0,466 s**, Apollo **2,803 s → 2,608 / 2,956 s**, AppIcon-27 **0,356 → 0,261 /
0,289 s**. Ruído da máquina; o passo continua O(pixels × 5) e não aloca nada novo.

### 4.2 Contra a Apple (`References/27.0-129/out/apple-512.png`)

Método do laudo do oráculo: render a 412, posto em `(50,50)` num quadro de 512
sem reamostrar; "pastilhas" = pixels cromáticos na saída da Apple, "fundo" =
`R==G==B`.

| região | antes (branco) | **depois (VCM)** | controle: sem realce |
|---|---|---|---|
| todos, Δ médio RGB | 11,240 | **9,626** | 9,367 |
| fundo | 6,144 | 6,144 | 6,144 |
| pastilhas, Δ médio | 20,763 | **16,133** | 15,391 |
| pastilhas, Δ máx. | 226 | **128** | 124 |

E só **na banda do realce** (pixels onde `after ≠ none`, 13.498 px):

| | Δ médio | viés (nosso − Apple) |
|---|---|---|
| antes (branco em `plusLighter`) | 46,19 | **+34,32** |
| **depois (VCM)** | **23,42** | **+0,10** |
| sem realce | 19,78 | −10,74 |

`[BIN]` **O "forte demais" era real e está medido:** o branco deixava a banda
34 níveis mais clara que a Apple, em média. O VCM leva o viés a **+0,10**, e o
controle mostra que a Apple **tem** realce ali (sem ele a banda fica 10,7 níveis
escura demais). A **quantidade** de luz agora bate.

`[OBS]` **A distribuição ainda não.** O erro absoluto na banda (23,4) segue
acima do controle sem realce (19,8): a luz está na conta certa mas no lugar
errado — ou de mais nuns pixels e de menos noutros. Este laudo não separa as
causas; candidatos em §5. **Não é um conserto que fecha o realce**: é a cor
certa, com a forma ainda por conferir.

Suíte (`mingw`, `-DIC_BUILD_UI=OFF`, `IC_CORPUS_DIR` na árvore principal):
**639 casos, 0 falhas.**

## 5. O que continua `[OBS]`

1. **A forma da banda contra a Apple.** Viés zero com erro absoluto acima do
   controle quer dizer luz redistribuída. Suspeitos, nenhum medido: o campo de
   distância (a semente nova entrou hoje), `height`/`inset` em pixels a 412, a
   dobra dentro/fora dos escuros, e os seis `spatialHighlighting` que seguem sem
   valor (idênticos só em `phi == 0`).
2. **Franja com alfa < 1:** se `BackdropFilterItem<ColorMatrix>::render`
   (`0x0007D7xx`; `Item::render_backdrop_filter` `0x0010FA68`) substitui o fundo
   ou compõe a cópia por cima.
3. **`ClipMode` 0** é alfa por `[INF]`; a tabela de nomes perto de `0x16F0E8`
   não foi lida.
4. **Grupos com blend próprio:** dentro deles `target` é o `groupAcc`, e o fundo
   que o realce filtra é só o do grupo — no alvo o pai do item de fundo é a
   camada que `null_style_draw` recebe, e qual é ela num grupo não foi seguido.
5. As condições extras do ramo de fundo (`pai+0x44` bit 7, `Layer+0x38 == 0`,
   `Effect & 3 == 0`) foram lidas, mas não se provou que o alvo as satisfaz
   neste desenho — só que, se não satisfizesse, a matriz sem fonte daria a cor
   constante do viés, o que nenhum ícone mostra.
6. Herdados: quais desenhos o `clampedPlusL` cobre; a aritmética de `0x00007064`.

## 6. Resumo dos selos

| pergunta | resposta | selo |
|---|---|---|
| `beginLayerWithFlags:1` chega como? | `Layer::Flag = 1` intacto (`0x3BCA0` → `0xC9A28` → `0xC9940` → `Layer+0x44` em `0x14DB98`) | `[BIN]` |
| o que é o bit 0 | **fundo**: `null_style_draw` testa em `0xCE02C` e chama `make_backdrop_item` (`0xCE174`, vtable `+0x70` = entrada 16 de `0x18B658` = `0x7E850`) ou aloca `BackdropFilterItem<ColorMatrix>` (`0xCE1A4`, vtable `0x18B0A0` → `0x7D798`), anexado à camada PAI | `[BIN]` |
| a matriz lê o fundo? | **sim** | `[BIN]` |
| pintou? | sim: `lerp(fundo, VCM(fundo), cobertura)` sobre o composto | `[BIN]` + `[INF]` na franja |
| pixel | GoW 59.790 (6,06 %) Δmáx 203; Apollo 143.454 (14,53 %) Δmáx 206 | `[BIN]` |
| contra a Apple, banda | viés +34,32 → **+0,10**; Δ médio 46,19 → 23,42 (sem realce: 19,78) | `[BIN]` |
| forma da banda | ainda erra mais que não desenhar | `[OBS]` |
