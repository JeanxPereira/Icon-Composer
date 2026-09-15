# Laudo — a forma da banda do realce: os cinco suspeitos, medidos, e o que sobrou

*2026-09-15. Alvo: o `[OBS]` que `2026-09-15-realce-vcm-fechado.md` §5.1 deixou
— "a quantidade de luz bate (+0,10 de viés), o erro absoluto na banda (23,42)
segue acima do de não desenhar realce (19,78): a luz está no lugar errado".
Esta frente não fecha o realce. Ela **mede onde o lugar certo é** e diz, com
selo, o que NÃO é a causa.*

**Nada de pixel mudou neste commit, e isso é o resultado e não a falta dele.**
Os cinco suspeitos que a frente anterior deixou foram isolados um a um contra o
gabarito da Apple; **nenhum move a banda**, e quatro deles ficaram fechados por
leitura de binário. O que move a banda — deslizar `sd` para dentro — vale
**7,5 ± 0,9 unidades de canvas** medidas em DOIS tamanhos do gabarito, derruba o
erro da banda de **23,42 para 17,60** (abaixo do controle de 19,78, pela
primeira vez) e **não tem leitura `[BIN]` que o sustente**. Aplicá-lo seria
escolher número pelo diff, que é exatamente o que destruiria o oráculo.

Endereços de `References/2.0-125/out/slices/IconRendering.arm64` (VA ==
deslocamento de arquivo), salvo onde estiver escrito `RenderBox`.

---

## 1. O método da medida

Gabarito: `References/27.0-129/out/apple-512.png` (o `CELM` de 8 bits do
`Assets.car` do Icon Composer 27.0-129) e, para o teste de escala,
`References/27.0-129/out/icns/ic13.png` (256 px). Bundle:
`.../out/AppIcon-27-defs.icon`, com as suposições de reconstrução listadas em
`inferences.json` — **elas limitam tudo o que vem abaixo**.

Alinhamento como o laudo do oráculo mandou: render a `--size 412` posto em
`(50,50)` num quadro de 512 (e a 206 em `(25,25)` num de 256), **sem
reamostrar**. Release, `-DIC_BUILD_UI=OFF`, `icrender`.

Três instrumentos novos, todos fora da árvore (scratchpad, não versionados):

1. **a banda**, igual à da frente anterior: os pixels onde o render com realce
   difere do render sem realce — **13.498 px** a 412, 3.723 a 206;
2. **o perfil por PROFUNDIDADE e por SETOR ANGULAR**: um build temporário
   despeja, por pixel, a normal e a distância do campo da última camada que o
   cobre, e as medidas saem em faixas de 1 px de profundidade × oito setores de
   45° da normal. **Uma média global não distingue "a banda está fraca" de "a
   banda está no lugar errado"; este perfil distingue**, e é o que achou o
   deslocamento;
3. **interruptores de experimento** por variável de ambiente no mesmo build
   temporário (classe de tamanho, `pixelsPerPoint`, colocação, semente
   sub-texel, as oito simetrias da normal, e um deslize/escala de `sd` como
   diagnóstico). **Todo esse código foi revertido antes do commit** — o que
   ficou é o laudo e o comentário `[BIN]` em `GlassSpecular.h`.

> **A armadilha da cadeia, evitada:** "confira que sua mudança está no caminho
> que produz a banda". O controle é o mesmo da frente anterior — `none` é o
> build com `drawSpecular` desligado, e a banda é definida por `base ≠ none`, de
> modo que todo número desta tabela é sobre pixels que o especular do glifo
> realmente escreve.

## 2. O que o gabarito mostra, antes de qualquer suspeito

Perfil de luma (nosso − "sem realce") por profundidade, no ápice superior do
chiclet azul, em **unidades de canvas** (1024 por lado; a 412 px, 1 px = 2,49
unidades):

| profundidade (un.) | 0 | 2,5 | 5,0 | 7,5 | 9,9 | 12,4 | 14,9 | 17,4 | 19,9 |
|---|---|---|---|---|---|---|---|---|---|
| **Apple − sem realce** | −7 | −9 | +23 | **+52** | +49 | +41 | +30 | +18 | +6 |
| **nós (hoje)** | +34 | **+56** | +27 | +8 | +6 | +4 | +3 | +2 | +1 |

`[BIN]` (medida no gabarito) **A banda da Apple não é mais fraca nem mais forte
que a nossa: ela está mais FUNDA.** O pico dela cai em ~9 unidades e o nosso em
~2,5; a dela ainda vale +30 onde a nossa já acabou; e os ~5 primeiros unidades —
o aro — são **mais ESCUROS** que o interior na saída da Apple, enquanto na nossa
são o pico do brilho. O mesmo formato aparece nos oito setores angulares e nas
duas bordas medidas à mão (ápice superior e borda esquerda da linha 300).

E o padrão **escala com o tamanho**: no rendition de 256 px o aro escuro tem ~1
px e o pico cai a ~2 px, isto é, as MESMAS ~5 e ~9 unidades de canvas. Não é
artefato de pixel; é geometria.

## 3. A tabela de contribuição: os cinco suspeitos, um a um

Erro médio absoluto de canal e viés (nosso − Apple) **só na banda (13.498 px)**,
412 px contra o gabarito de 512. Referências: `base` = o commit `cc97cc1`;
`none` = sem realce nenhum.

| # | suspeito | como foi isolado | Δ médio | viés | veredito |
|---|---|---|---|---|---|
| — | **base (hoje)** | — | **23,42** | +0,10 | — |
| — | **controle sem realce** | `drawSpecular` desligado | 19,78 | −10,74 | — |
| 1 | **campo: semente sub-texel** | `FieldOptions::subpixelSeed = false` | **25,04** | −1,24 | **piora**; a semente fica ligada |
| 2 | **classe de tamanho** | `small` / `medium` / `display` | 19,78 / **22,91** / 24,35 | −6,75 / +1,43 / −1,88 | `small` cai ao nível do controle (opacidade 0,3 no `keySharp`) sem acertar o viés; nenhuma move a banda |
| 2 | **escala de `height`/`inset`** | `pixelsPerPoint = 0,5` (rect sem o recuo) | 23,25 | +1,47 | ±0,2: a unidade não é o erro |
| 3 | **dobra dentro/fora** | `placement = outside` (os dois escuros para fora) | 24,13 | +1,62 | piora |
| 3 | idem, com `display` | `outside` + classe 3 | 24,86 | −0,24 | piora |
| 4 | **`spatialHighlighting`** | — | — | — | **identidade provada**, §4.4 |
| 5 | **sinal do gradiente** | normal invertida (`−nx, −ny`) | **25,74** | +3,23 | piora: o sinal de hoje é o certo |
| 5 | idem, as outras 6 simetrias | `x↔y`, `−x`, `−y` e combinações | 23,39 … 23,50 | −14,8 … +0,1 | nenhuma melhora |
| — | **diagnóstico: `sd` 3 px mais fundo** | não é conserto, §5 | **17,60** | −7,10 | **única coisa que passa do controle** |

Nenhum dos cinco tira 1,5 do erro. O deslize tira 5,8 e passa o controle.

### 3.1. O perfil diz por que os suspeitos não podiam funcionar

Os cinco mexem em **intensidade, direção ou largura**; o erro é de
**posição**. No perfil por setor, `display` (banda mais fina) e `medium` (banda
igual, escuros com cone π) continuam com todo o brilho em 0–2,5 unidades; as
simetrias da normal só trocam QUAL borda acende, e a Apple acende **todas** as
bordas na mesma profundidade. É por isso que a média global quase não se move
enquanto o desenho continua errado — e é o que o laudo do oráculo previu ao
pedir "média por bloco e viés por quadrante".

## 4. O que ficou FECHADO por leitura, e não por diff

### 4.1. As unidades são as nossas — `ctx+0x46A8` lido

`[BIN]` `0x00042D3C`–`0x00042D50`, dentro do construtor do contexto
(`0x0004266C`):

```
0x00042D2C  fdiv d0, d11, d9        ; CGRectGetHeight(rect) / CGRectGetHeight(canvas)
0x00042D30  fcmp d0, d8             ; contra a razão de LARGURA
0x00042D34  fcsel d0, d0, d8, mi    ; escala = min(razão_w, razão_h)
0x00042D38  str  d0, [sp, #0xa60]   ; ctx+0x5E0
0x00042D40  fdiv d0, d1, d0         ; 1 / escala
0x00042D44  str  d0, [sp, #0x4b20]  ; ctx+0x46A0
0x00042D48  ldr  d1, [sp, #0x10]
0x00042D4C  fdiv d0, d0, d1         ; (1/escala) / contentsScale
0x00042D50  str  d0, [sp, #0x4b28]  ; ctx+0x46A8
```

`[BIN]` `0x8DA28` e `0x8D9D4` são `_CGRectGetWidth` e `_CGRectGetHeight`,
resolvidos **pela tabela de símbolos indiretos** (a lição que quase custou um
conserto falso a uma frente hoje; o mesmo resolvedor devolve `_hypot`, `_sin`,
`_atan2` nos endereços que os laudos anteriores já nomeavam, o que é o teste de
sanidade dele).

Então `ctx+0x46A8` — o multiplicador de `minDistancePixels` em `0x0004BF00` — é
**unidade de canvas por pixel**, e `height`/`inset` vivem em unidades de canvas.
É exatamente o `pixelsPerPoint = size/1024` deste renderizador, invertido. O
`[INF]` que `GlassSpecular.h` carregava ("o par `pixelUnit`/escala cancela nessa
razão") virou leitura.

### 4.2. A classe de tamanho do gabarito é `large` — os três limiares, lidos

`[BIN]` `0x00042D54`–`0x00042DE4` compara `min(rect.w, rect.h)` com
`[ctx+0x4E28]`, `[ctx+0x4E30]`, `[ctx+0x4E38]` e escreve o byte em `ctx+0x469F`;
`0x00018D70`–`0x00018DCC` faz a mesma escada sobre `min(w,h)/escala` lendo
`[params+0x240/0x248/0x250]` (o mesmo campo com o cabeçalho de 0x10 do objeto).

`[BIN]` E os três números estão no *pool*, escritos por `0x0005EB00`–`0x0005EB10`
(`ldr q0, [0x98600]` e `mov x8, #0x4070000000000000`):

| campo | valor | origem |
|---|---|---|
| `minMediumSize` | **25,0** | `0x98600` |
| `minLargeSize` | **60,0** | `0x98608` |
| `minDisplaySize` | **256,0** | imediato em `0x0005EB0C` |

O gabarito é um ícone de 256 pt a 2× cujo chiclet mede 206 pt: `d0 < 256` →
**`large` (2)**, que é a classe que este renderizador já usa. O suspeito 2 está
fechado do lado do binário e medido do lado do pixel (±1 de erro em qualquer das
quatro classes).

### 4.3. `inset` é zero, relido da fábrica

`[BIN]` `0x00064604` (a fábrica dos seis membros do conjunto `glyphs*`), campo a
campo: `keySharp` escreve zero em `+0x90`/`+0xA0` e a tag `1` (`nil`) em `+0xD0`
(`0x00064698`–`0x000646B0`); `keyDiffuse`, o mesmo (`0x00064730`–`0x00064738`),
com `distance` `{16,24,24,24}` de `0x987C0` + `fmov v1.2d, #24.0` e
`minDistancePixels` `[4]×4`. E nada acrescenta um termo depois: `0x0004BD90` só
faz os dois `max`, `0x0000ED94` só **multiplica** `height` e `inset` pela escala
`(sdfTexels−2)/rect.width` (`0x0000EF74`), e o agrupamento em `HighlightsPass`
(`0x0004C314`) só **compara** — varrido, não tem uma `fadd`/`fmul` sobre os
campos, só `fcmp`.

### 4.4. `spatialHighlighting` não pode morder — agora com o corpus junto

`[BIN]` (frente anterior) a pós-passagem `0x00012550` é a identidade quando
`customLightDirection == nil` (tag `ctx+0x20`, lida em `0x0004BE74`).
`[ART]` **E nenhum documento pode deixar de ser `nil`:** `customLightDirection` é
campo de `GlobalConfiguration`, não do `.icon`. Nos 145 documentos do corpus, 82
trazem a chave `lighting` e os únicos valores dela são `individual` (64) e
`combined` (18) — não existe chave de direção de luz em documento nenhum. O
suspeito 4 está fechado.

### 4.5. O sinal do gradiente: o gabarito desempatou, e a favor do que estava lá

`[BIN]` O laudo dos highlights marcou o sinal como **argumentado**: "normal para
fora, porque é a que faz `angleFromKey = 0` acender o topo". As oito simetrias
de `(nx, ny)` foram renderizadas contra o gabarito:

| simetria | identidade | `−y` | `−x,−y` | `x↔y` | as outras quatro |
|---|---|---|---|---|---|
| Δ médio na banda | **23,42** | 25,74 | 23,50 | 23,39 | 23,39–23,50 |

A identidade é a melhor das oito e a inversão pura é a pior. **Este é o uso que
o gabarito tem direito de ter**: escolher entre duas leituras possíveis, sem
mexer em número nenhum. O `[OBS]` do sinal pode ser rebaixado a "argumentado e
medido".

## 5. O que sobra, com número: 7,5 unidades de profundidade

O único parâmetro que move a banda para onde o gabarito a mostra é `sd`. Deslize
diagnóstico (`sd ← sd − s`), com a banda medida nos dois tamanhos do gabarito:

| `s` (px a 412) | 0 | 2,0 | 2,4 | 3,0 | 3,5 | 4,0 |
|---|---|---|---|---|---|---|
| Δ médio na banda | 23,42 | 19,18 | 17,93 | **17,60** | 17,60 | 18,05 |

| `s` (px a 206) | 0 | 0,8 | 1,2 | **1,5** | 1,75 | 2,0 | 3,0 |
|---|---|---|---|---|---|---|---|
| Δ médio na banda | 26,21 | 21,93 | 18,69 | **17,65** | 17,90 | 18,12 | 20,10 |

`3,0–3,5 px` a 412 e `1,5 px` a 206 são **7,5 ± 0,9 unidades de canvas nos dois
tamanhos**. Escalar `sd` em vez de deslocá-lo (a leitura rival: o `maxDistance`
do shader não ser o da textura) **piora** — `sd×0,5` dá 23,63 e `sd×0,4` dá
23,33 —, então o que o gabarito pede é um **deslocamento**, não um ganho.

**E não foi aplicado.** Os dois lugares que poderiam carregar esse deslocamento
são os dois que ninguém leu:

1. `[OBS]` **o nível zero da textura de SDF**. `sd = (0,5 − tex.r) × (−2·maxDist)`
   (`0x0000EAA8` grava o `0,5` no argumento 8; `0x00049238` carrega
   `[descriptor+0xA8]` para o argumento 7). O gerador é
   `-[CUINamedLayerImage sdfTextureWithBufferAllocator:]` (`0x000867F8`), que
   vive no CoreUI e **não está em nenhuma das duas fatias**. Um zero 7,5
   unidades para dentro do contorno `alpha ≥ 0,5` explicaria a medida inteira,
   inclusive o aro escuro (que passaria a ser a região onde o alvo não tem
   banda nenhuma e nós temos o `keySharp` todo);
2. `[OBS]` **`[descriptor+0xA8]`**, o `maxDistance` — lido como escala, nunca
   como origem.

Aplicar 7,5 sem uma dessas duas leituras seria ajustar parâmetro até o diff
fechar. **No ícone do usuário** (`GoWToolkit.icon`, 1024 px, `--idiom square`) o
mesmo deslize move **91.301 px (9,25 % dos visíveis, Δ máx. 101)** e tira o
realce de todo traço mais fino que 7,5 unidades — a arte de grunge dele é cheia
deles. Um conserto que precisa disso precisa antes de uma leitura.

## 6. O que este commit entrega

- **`Source/RenderBox/GlassSpecular.h`**: as leituras de §4 (unidades,
  limiares de classe, `inset` zero, o corpus da luz) e a medida de §5 como
  `[OBS]` numerado, no arquivo que a próxima frente vai abrir. **Nenhuma linha
  de código mudou** — `icrender` produz o mesmo pixel de `cc97cc1`.
- Este laudo.

Suíte: **639 casos, 0 falhas** (`mingw`, `-DIC_BUILD_UI=OFF`, `IC_CORPUS_DIR`
posta). Tempo do `icrender` em Release: `AppIcon-27-defs` a 412 px **0,27 s**,
`GoWToolkit` a 1024 px **0,51 s** — iguais aos de `cc97cc1`, como tem de ser
quando nada mudou.

## 7. Resumo dos selos

| pergunta | resposta | selo |
|---|---|---|
| a semente sub-texel é a causa? | não: desligá-la piora (23,42 → 25,04) | `[BIN]` (pixel) |
| a classe/escala de `height` é a causa? | não: as quatro classes ficam em 19,8–24,4 e nenhuma move a banda; e a classe do gabarito **é** `large`, por `min(rect)` contra 25/60/256 (`0x42D54`, pool `0x98600`) | `[BIN]` |
| as unidades de `height`/`inset` | unidade de canvas; `ctx+0x46A8 = (1/escala)/contentsScale` (`0x42D3C`–`0x42D50`, com `_CGRectGetWidth/Height` resolvidos pela tabela de indiretos) | `[BIN]` |
| a dobra dentro/fora é a causa? | não: `outside` piora (24,13) | `[BIN]` (pixel) |
| `spatialHighlighting` pode morder? | não: `customLightDirection` é de `GlobalConfiguration` e nenhum documento tem chave de direção de luz (145 do corpus, 82 com `lighting` ∈ {individual, combined}) | `[BIN]`+`[ART]` |
| o sinal do gradiente está trocado? | não: das oito simetrias, a identidade é a melhor (23,42) e a inversão a pior (25,74) | `[BIN]` (pixel) |
| então onde está o erro? | a banda do alvo está **7,5 ± 0,9 unidades de canvas mais funda**, medido em 512 e em 256; deslizar `sd` leva o erro a 17,60, abaixo do controle de 19,78 | `[OBS]` |
| de onde viria o deslocamento? | do zero do SDF (CoreUI, fora das fatias) ou de `[descriptor+0xA8]`; **não** de `inset` (zero, relido em `0x64604`), nem de `0x4BD90`/`0xED94`/`0x4C314` | `[OBS]` |
