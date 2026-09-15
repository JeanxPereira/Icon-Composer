# Laudo — a sombra desenha

*2026-09-15. Frente: fazer a sombra do `GlassMaterial` chegar ao pixel. Entrada:
`Docs/Laudos/2026-09-15-sombra.md`, que fechou a aritmética da alpha e deixou
duas coisas por fazer — nomear o terceiro fator e transcrever a geometria.*

Antes desta frente, `grep -n "shadow" Source/RenderBox/IconRenderer.cpp` achava
**um comentário** sobre o drop-shadow de um SVG e nenhuma aritmética. O documento
carregava `shadowStyle` e `shadowOpacity`, o inspetor editava os dois, e o pixel
ignorava os dois. `GlassMaterial.h` dizia isso em tantas palavras: *"`shadowOpacity`,
`hasSpecular` and `specularPlacement` have no known consumer."*

Agora desenha, e o `shadowOpacity` saiu daquela lista.

Endereços do slice `References/2.0-125/out/slices/IconRendering.arm64`
(VA == offset de arquivo), salvo onde o texto disser outra coisa.

---

## 1. O terceiro fator TEM nome, e o laudo-mãe podia tê-lo lido

O item 1 do `[OBS]` do laudo-mãe: *"O nome de `[descritor+0x38]`. Multiplica a
alpha da sombra e a do desenho principal; a posição e o uso dizem 'opacidade da
camada', mas não segui quem o escreve."*

`[BIN]` **Ele é `IconRendering.FinalizedIcon.Layer.opacity`, um `Swift.Double`.**
E o "descritor de `0xC0` bytes" tem nome próprio: ele **é** o
`FinalizedIcon.Layer`. O array percorrido com passo `0xC0` em `0x435A0` é
`FinalizedIcon.layers`.

Três leituras independentes, cada uma com endereço:

### 1.1. A reflexão

`[BIN]` `out/fieldmd_iconrendering.txt`, do `__swift5_fieldmd` em **`0xA301C`**:

```
0xa301c  [struct] IconRendering.FinalizedIcon.Layer  (9 fields)
      material                           <indirect-external>
      blendMode                          <indirect-external>
      opacity                            Sd
      knocksOutBorder                    Sb
      image                              <indirect-external>
      contentFrame                       __C.CGRect
      effectsFrame                       <indirect-external>
      sdf                                IconRendering.SDF
      shadowImage                        <indirect-external>
```

Terceiro campo, `opacity`, e `Sd` **é** `Swift.Double`. O arquivo já estava no
disco desde antes do laudo-mãe.

### 1.2. O vetor de deslocamentos EMITIDO — os offsets são lidos, não contados

`[BIN]` `__swift5_types` em `0xAC614` → descritor nominal `0x9EF30` (nome
`Layer`, `FieldOffsetVectorOffset = 2`) → metadado estático em `0xBD3F0`. Os
deslocamentos em `0xBD3F0 + 2×8`:

| desloc. | campo |
|---|---|
| `+0x00` | `material` (`Icon.GlassMaterial`) |
| `+0x31` | `blendMode` |
| **`+0x38`** | **`opacity`** |
| `+0x40` | `knocksOutBorder` |
| `+0x48` | `image` |
| `+0x50` | `contentFrame` (`CGRect`, `0x20`) |
| `+0x70` | `effectsFrame` (`Optional<CGRect>`) |
| `+0x98` | `sdf` |
| `+0xB0` | `shadowImage` |

`[BIN]` A *value-witness table* em `0xBD388` (metadado − 8) dá `size` e `stride`
**`0xC0`/`0xC0`** — o passo do array do laudo-mãe, confirmado pelo TIPO e não
pelo `add x23, x23, #0xc0`.

`[BIN]` E o `+0x31` não é acidente: `Icon.GlassMaterial` tem `size 0x31` com
`stride 0x38` (metadado `0xBED40`), então o `blendMode` empacota no byte `0x31`
que a `size` deixou livre e o `opacity` cai em `0x38`, não em `0x40`.

> `[BIN]` **`Icon.Layer` NÃO é este struct.** Metadado `0xBE8E8`, `size 0x58`,
> campos `elements@0x00, opacity@0x08, blendMode@0x10, material@0x18,
> performsLightingByElement@0x49, appearance@0x50`. São dois tipos com nomes
> parecidos, e confundi-los é o erro que a aritmética de offsets convidava.

### 1.3. O escritor

`[BIN]` Função **`0x16314`–`0x174D8`**, o laço que converte `Icon.Layer` em
`FinalizedIcon.Layer`:

```
0x0001702C   add   x26, x26, #0x58      ; cursor da ORIGEM, passo 0x58 = Icon.Layer
0x00017030   add   x8,  x20, x24
0x00017034   strb  w21, [x8, #0x51]     ; -> elemento +0x31, blendMode
0x00017038   str   d8,  [x8, #0x58]     ; -> elemento +0x38, OPACITY
0x0001703C   add   x24, x24, #0xc0      ; cursor do DESTINO, passo 0xC0
...
0x0001704C   ldur  d8,  [x26, #-0x10]   ; <- a origem de d8
0x00017050   ldurb w21, [x26, #-8]      ; <- a origem do blendMode
0x00017068   ldrb  w23, [x26, #0x30]    ; -> material.specularPlacement
```

O `0x58` do `str` menos o `0x20` de cabeçalho do array Swift dá o `+0x38` do
elemento. `[INF]` `x26` aponta para *(`Icon.Layer` + `0x18`)*, isto é, para o
campo `material` — corroborado três vezes: `[x26+0x30]` alimenta o
`material.specularPlacement` do destino (`GlassMaterial+0x30`), `[x26−0x08]`
alimenta o `blendMode` e `Icon.Layer.blendMode` está em `+0x10 = 0x18 − 0x08`.
Logo `[x26−0x10]` é **`Icon.Layer.opacity`**, em `+0x08`.

**Cópia verbatim: nem grampo nem transformação no sítio da cópia.**

### 1.4. O layout se confirma DENTRO da função da sombra

`[BIN]` A instrução seguinte à que lê o fator é

```
0x00049F10   ldr  d9,  [x0, #0x38]      ; opacity
0x00049F14   ldp  x19, x24, [x0, #0xb0] ; shadowImage
```

A função que desenha a sombra lê, quatro bytes depois do campo que o metadado
chama `opacity`, o campo que o metadado chama `shadowImage`. Duas leituras, uma
tabela, nada entre elas. Isto não vinha do mesmo caminho da reflexão e concorda
com ele.

### 1.5. O contra-indício do laudo-mãe se dissolve

`[BIN]` O laudo-mãe hesitou porque `Shadow.ignoreFillOpacity` é `true` por padrão
e **não é consultado** em `0x49ED4`. Ele não precisa ser: `ignoreFillOpacity` é
campo de `ICRRenderingParameters.Shadow`, um struct de **parâmetros**, e o
`+0x38` é a opacidade de uma **camada**, em outro tipo. São duas grandezas, não
duas leituras de uma. A tensão era entre um campo e um homônimo.

`[OBS]` Continua aberto, e menor do que parecia: **quem lê o
`ignoreFillOpacity`**.

### 1.6. Duas correções ao laudo-mãe, da mesma tabela

`[BIN]` O `§5.3` chama o byte `[descritor+0x31]` de *"um `Bool` do descritor"*.
Ele é `FinalizedIcon.Layer.blendMode`, um `Icon.BlendMode` — enum de 18 casos que
cabe num byte. Lido como byte em `0x45F10`, o que não muda a aritmética de lá,
mas muda o que ele é.

`[BIN]` O `§5.2` chama `[descritor+0xB0]` apenas de *"a imagem"*. Ela é
`shadowImage`: a saída da preparação do `§5.1` realimentada na composição do
`§5.2`. O ciclo fecha.

---

## 2. A geometria — já estava medida, e foi transcrita

O pedido listava a geometria como pendência, apontando um `inputShadowOffset`
`[OBS]` do doc 03. **A pendência não existia:** o `§5.1` do laudo-mãe já a tinha
medido inteira, com endereços. O que faltava era transcrever.

`[BIN]` Com `s = min(largura, altura) / 1024` (o literal `2^-10` em `0x20B88`),
a imagem da sombra é o desenho do próprio elemento com:

| passo | operação | endereço | com os padrões, num alvo de 1024 |
|---|---|---|---|
| 1 | cor: `colorMultiply(v,v,v,1)`, `v = vibrantBrightness` | `0x20BB4`, pulado se `v == 1` (`0x20BC0`) | `×0.75` no ramo vibrante |
| 1' | cor: `alphaMultiply(RBColorBlack)` | `0x20BAC`, GOT `0xC5818` | silhueta preta no ramo neutro |
| 2 | translação `(s·offsetX, s·offsetY)` | `0x20BDC`–`0x20BEC` | `(0, 32)` px |
| 3 | desfoque `raio = s · blurStrengthMax · clamp(radius[3−c], 0, 1)` | `0x20C38`, grampo `0x20C14`–`0x20C28` | `19.2` px |
| 4 | anel: recorte com `−(s · ringWidth[3−c])` | `0x20C8C`, `0x20CE8` | `16` px — **não transcrito** |
| 5 | `drawDisplayList:` | `0x20CF4` | |

e a composição do `§5.2`: um `setRect:` pintado com essa imagem sob um
`tintColor:` (branco `0xCDE70` / preto `0xCDE98`), na alpha das três
multiplicações, sob `Shadow.blendMode` — `2 = multiply`.

**Os passos 2 e 3 comutam** — uma gaussiana é invariante a translação — então a
ordem entre eles não é uma escolha e a implementação desfoca primeiro, na grade
da própria arte.

### 2.1. O grampo do desfoque é o TERCEIRO formato desta família

`[BIN]` Três fórmulas da mesma matéria grampeiam de três maneiras, e a diferença
é a informação:

| quantidade | grampo | endereço |
|---|---|---|
| `refractionHeight` | dos **dois** lados, antes do `pow` | `0x4A708` |
| `blurStrength` do material | **só teto**, sem piso | `0x4A948` |
| `Shadow.radius` | dos **dois** lados | `0x20C14`–`0x20C28` |
| `shadowOpacity` | **nenhum** | — |

---

## 3. O que o corpus diz `[ART]`

Varredura das 145 documentos, 271 grupos, 302 resoluções de `shadow`
(incluindo `shadow-specializations`):

| medida | valor |
|---|---|
| grupos que carregam `shadow` | **271 de 271** |
| resoluções `neutral` | 190 |
| resoluções `layer-color` (= `vibrant`) | 87 |
| resoluções `none` | 25 |
| resoluções `automatic` | **0** |
| grupos que contêm vidro | 113 |
| resoluções em grupo com vidro que desenhariam | 108 |
| opacidade mais comum | `0.5`, em 226 das 302 |

### 3.1. O achado do corpus: TRÊS opacidades acima de 1

`[ART]` Três resoluções passam de `1.0`, todas `neutral`, todas em grupo com
vidro:

| documento | grupo | opacidade |
|---|---|---|
| `Apollo-Reborn/AppIcon` | 0 | **2.4** |
| `Apollo-Reborn/AppIcon` | 2 | **1.6** |
| `Apollo-Reborn/LG-antenna` | 0 | **2.4** |

E elas caem em décimos exatos contra a tabela medida:

```
2.4 × neutralOpacity 0.375 = 0.9        1.6 × 0.375 = 0.6
```

**Dois valores autorados, dois décimos exatos.** É o lado do documento
corroborando ao mesmo tempo o `0.375` do construtor (`0x5EC88`) e a ausência do
grampo: uma transcrição que grampeasse `shadowOpacity` em `[0,1]` desenharia a
sombra do Apollo em `0.375` no lugar de `0.9`, e não teria como perceber. O
autor estava afinando contra a tabela.

Está travado em `Tests/test_glass_shadow.cpp`.

---

## 4. O que foi implementado

`Source/RenderBox/GlassShadow.h` / `.cpp` — arquivos **novos**, no molde de
`GlassTranslucency.h/.cpp`. O toque em `IconRenderer.cpp` é um `#include`, um
bloco antes do desenho da arte e duas chamadas.

- `ShadowParameters` — os 15 campos de `ICRRenderingParameters.Shadow` com os
  defaults medidos. Dez deles são carregados e não consumidos, pela mesma razão
  que o `TranslucencyEffect` carrega seus dois booleanos não lidos.
- `shadowAlpha` — as três multiplicações, **na ordem do binário** (`d8×d10`
  primeiro, `d9×` depois), sem grampo.
- `sizeBasedValue` — reusado de `GlassTranslucency.h`. **Uma inversão, um lugar.**
- `shadowGeometry` — deslocamento, raio e largura do anel em pixels do alvo.
- `shadowImage` — cor, desfoque, translação.
- `shadowEffectiveStyle`, `shadowBlendMode` — o portão do `§4(e)` e o byte de
  mescla do `§4(d)`, escritos como funções para que a leitura viva no código e
  não só num comentário. Nenhum dos dois pode disparar neste renderizador, que
  desenha o documento como autorado — o estado identidade.
- `shadowOverdrawAlpha` — a aritmética do `§5.3`, transcrita **e não desenhada**:
  ela existe para que a nota consiga dizer se o documento em mãos abriria aquela
  passagem.

### 4.1. A porta é o bit `glass` da camada

`[INF]` Mesma decisão, e mesmo raciocínio, da frente da translucidez: a sombra é
campo de `Icon.GlassMaterial` e `[ART]` o bit `glass` do documento é
`Icon.Element.participatesInGlass`. `[ART]` Os 271 grupos carregam `shadow` e só
113 contêm vidro — honrar a chave em todos poria sombra sob 158 grupos cujo autor
nunca pediu vidro.

### 4.2. O `opacity` da camada entra DUAS vezes, e é transcrição

`[BIN]` O terceiro fator multiplica a alpha da sombra (`0x4A070`) **e** a do
desenho do elemento (`0x495F0`). O laço de `IconRenderer.cpp` já passava
`opacity` ao `blendOver` da arte antes desta frente; passá-lo também à sombra é o
que faz os dois concordarem. Dividi-lo em qualquer um dos dois seria compensar
uma aritmética que o binário não faz.

### 4.3. Um raster PODE ter sombra

A sombra sai da alpha da própria arte e não precisa de contorno nenhum, ao
contrário da máscara de translucidez, que precisa de um campo de distância.
`[ART]` 45 das 171 camadas de vidro do corpus nomeiam `.png`, e todas elas
estavam perdendo os dois efeitos pela razão de um só.

---

## 5. A prova em pixel

`icrender <bundle> --out <png> --size 1024`, antes e depois, contando texels
cujo `max|Δ|` sobre os quatro canais é diferente de zero:

| documento | estilo | pixels mudados | % | Δ máx |
|---|---|---|---|---|
| `Apollo-Reborn/AppIcon` | `neutral` (2.4, 1.6, 0.6) | **388.659** de 1.048.576 | 37,07 % | 190 |
| `CodeEditApp/CodeEditAlphaIcon` | `layer-color` (vibrante) | **166.003** de 1.048.576 | 15,83 % | 133 |
| `Aeastr/GlowGetter` | `neutral` (0.5) | **0** | 0 % | 0 |

O terceiro é o **controle negativo** e não um furo: os três grupos do GlowGetter
têm `glass: false` em todas as cinco camadas, então a porta do `§4.1` recusa —
como deve. Os dois ramos de cor desenham; a porta segura nos dois sentidos.

---

## 6. O que fica `[OBS]`

1. **O anel.** `Shadow.ringWidth` vem preenchido nesta versão
   (`[16,16,16,16]`, `0x5EC58`), então **o padrão é ter anel** e toda sombra que
   este renderizador desenha está a um passo de distância do alvo. A largura está
   lida; a **geometria** da forma de recorte (construída em `0x11C40`, aplicada
   por `clipLayerWithAlpha:mode:` em `0x20CE8`) não foi transcrita. Item 5 do
   `[OBS]` do laudo-mãe, ainda aberto. `kShadowRingNote` diz isso em toda
   renderização.
2. **O kernel do desfoque.** O **raio** é medido; o que o
   `addBlurFilterWithRadius:` constrói a partir dele mora num binário que este
   projeto não decodificou — a mesma lacuna que `GlassMaterial.h` já registra
   para o desfoque do material. Aqui o raio é tomado como o suporte de três
   sigmas (`sigma = raio/3`), que é o **inverso** da regra que o gaussiano deste
   repositório já usa (`ceil(3σ)`); `sigma = raio` daria uma sombra cerca de três
   vezes mais larga e é igualmente não lida. É a única escolha deste arquivo que
   é convenção e não medida, e ela está isolada numa constante nomeada.
3. **A passagem de overdraw** (`§5.3`). Transcrita como aritmética, não
   desenhada. `kShadowOverdrawNote` avisa quando o documento em mãos a abriria.
4. **O default de `Layer.opacity`.** A cópia é verbatim e o inicializador de
   `Icon.Layer` não se materializa neste slice: uma varredura dos 117 sítios de
   `fmov dN, #1.0` do `__text` não acha nenhum `str` num `Layer`, e não existe
   `Icon.Layer.<anon>.CodingKeys` para carregar um `?? 1.0`. `1.0` é a identidade
   multiplicativa e é o que este renderizador usa quando o documento cala, mas
   isso é a regra do **documento**, não um valor lido do binário.
5. **Quem lê o `ignoreFillOpacity`** (§1.5).
6. **Os seis campos do portão** (`ctx+0x4E8..0x510`) e **a escrita de
   `ctx+0x469F`** — os itens 2 e 3 do `[OBS]` do laudo-mãe, intocados por esta
   frente.
7. **A ordem entre a máscara de translucidez e a sombra.** Este renderizador tira
   a sombra da arte já mascarada; qual imagem o alvo alimenta ao `shadowImage` não
   foi rastreado. Com máscara identidade — todo grupo cuja translucidez esteja
   ausente ou desligada — as duas leituras dão o mesmo pixel.

---

## 7. Instrumentos

**Nenhum MCP de RE foi usado — nem estava disponível.** O Ghidra responde e
`list_instances` devolve lista vazia; não há servidor de IDA nesta sessão. Tudo
saiu de `scripts/macho.py` (`syms`/`fn`/`xref`/`dis`), de
`out/fieldmd_iconrendering.txt`, e de leitores de `__swift5_types`, de vetores de
deslocamento de campo e de *value-witness table* escritos no *scratchpad*. Nada
foi escrito dentro de `References/`.

O ponto de controle das três leituras da §1 é que elas vêm de lugares diferentes
do arquivo — seção de reflexão, seção de tipos, e `__text` — e produzem a mesma
tabela.

## 8. Placar

| pergunta | resposta | selo |
|---|---|---|
| nome de `[descritor+0x38]` | `IconRendering.FinalizedIcon.Layer.opacity`, `Swift.Double` | `[BIN]` |
| nome do descritor de `0xC0` | `IconRendering.FinalizedIcon.Layer` | `[BIN]` |
| quem escreve `+0x38` | `0x17038`, de `Icon.Layer.opacity`, verbatim | `[BIN]` |
| `[descritor+0x31]` | `blendMode`, não um `Bool` — correção ao laudo-mãe | `[BIN]` |
| `[descritor+0xB0]` | `shadowImage` — correção ao laudo-mãe | `[BIN]` |
| o contra-indício do `ignoreFillOpacity` | dissolvido: outro struct, outra grandeza | `[BIN]` |
| a geometria | já medida no `§5.1`; transcrita, menos o anel | `[BIN]` · `[OBS]` o anel |
| opacidades acima de 1 no corpus | 3 de 302, em décimos exatos contra `0.375` | `[ART]` |
| o kernel do desfoque | raio medido, kernel não lido | `[OBS]` |
| a passagem de overdraw | aritmética transcrita, não desenhada | `[OBS]` |
