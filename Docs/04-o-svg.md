# 04 — O SVG

Cada camada de um `.icon` aponta para um arquivo em `Assets/`, e 149 dos 209
medidos são SVG (doc 02 §2). Este documento diz o que esses arquivos contêm e o
que o leitor da Apple faz com eles.

## As duas fontes

**O binário.** `CoreSVG.framework`, fatia arm64 (448.624 bytes), do bundle do
Icon Composer — é uma cópia literal do framework privado do sistema (doc 00 §2).
Os nomes de elemento e de atributo que ele parseia estão em claro no pool de
strings.

**O corpus.** Os 149 SVGs dos 55 bundles locais. Tamanhos de 164 bytes a 2,38 MB,
mediana 1.290.

## 1. O que o CoreSVG parseia

`[BIN]` Os elementos que ele nomeia:

```
svg  g  defs  symbol  use  switch  title  style
path  rect  circle  ellipse  line  polyline  polygon
linearGradient  radialGradient  stop  pattern
mask  clipPath  image  text
filter  feBlend  feComposite  feFlood  feGaussianBlur  feImage  feMerge
        feOffset  feTile
```

`[BIN]` Os atributos:

```
class  style  transform  viewBox  preserveAspectRatio  opacity
fill  fill-opacity  fill-rule
stroke  stroke-opacity  stroke-width  stroke-linecap  stroke-linejoin
       stroke-dasharray  stroke-dashoffset  stroke-miterlimit
clip-path  clip-rule  mask  filter
points  gradientUnits  gradientTransform  spreadMethod
offset  stop-color  stop-opacity  patternUnits  href  xlink:href
```

## 2. O que os arquivos reais contêm

`[ART]` Contado nos 149. Todos, sem exceção, declaram `viewBox`.

| elemento | n | | elemento | n |
|---|---|---|---|---|
| `stop` | 595 | | `feColorMatrix` | 42 |
| `path` | 373 | | `feBlend` | 39 |
| `g` | 201 | | `filter` | 26 |
| `linearGradient` | 155 | | `feGaussianBlur` | 22 |
| `svg` | 149 | | `feOffset` | 21 |
| `rect` | 103 | | `feComposite` | 21 |
| `defs` | 74 | | `feFlood` | 18 |
| `title` | 37 | | `feMorphology` | 14 |
| `pattern` | 11 | | `feDropShadow` | 8 |
| `use` | 11 | | `circle` | 6 |
| `mask` | 3 | | `radialGradient` | 6 |

Por arquivo: `style` ou `style=` em 71, `transform` em 62, largura com unidade em
59, algum gradiente em 45, algum filtro em 9, `mask` em 3, `use` em 3, imagem
embutida em 3, `clip-path` em 1.

`[ART]` Os comandos de path, por ocorrência:

```
C 1997   L 967   M 561   Z 502   H 492   V 357
c 334    z 75    l 36    s 26    m 17    h 13   v 4
A 1      a 1     S 1
```

Cúbicas e retas dominam. Arcos aparecem **duas vezes em 149 arquivos**.

> **Cuidado de medição.** Uma primeira contagem viu `e` 46 vezes e quase o
> registrou como comando. `e` não é comando de path: é o expoente de um número
> em notação científica. O que isso diz de verdade é que **o parser de número
> precisa aceitar `1e-5`**.

## 3. O achado: três filtros que a Apple não lê

`[BIN]` **`feColorMatrix`, `feMorphology` e `feDropShadow` aparecem ZERO vezes**
no `CoreSVG`, no `IconComposerKit` e no `IconComposerFoundation`. Nenhum dos três
está na lista do §1.

`[ART]` E os três estão nos arquivos reais: 42, 14 e 8 ocorrências.

`[INF]` Logo o Icon Composer **descarta esses filtros em silêncio**, e os ícones
que aquelas pessoas commitaram desenham sem eles. Um leitor nosso que os
implementasse ficaria *mais* certo que o alvo e produziria pixel diferente — o
que, para um projeto cujo objetivo é reproduzir o alvo, é o erro errado de
cometer.

O que fica: o suporte a filtro do CoreSVG é `feBlend`, `feComposite`, `feFlood`,
`feGaussianBlur`, `feImage`, `feMerge`, `feOffset` e `feTile`.

## 4. O escopo desta rodada

Um leitor de SVG completo é um projeto por si. O que o `.icon` precisa, por
ordem de peso no corpus, é **geometria e pintura**:

| entra | fica de fora, e é REPORTADO |
|---|---|
| `svg` com `viewBox`, `g`, `defs` | `filter` e suas primitivas |
| `path`, `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon` | `pattern`, `mask`, `clipPath` |
| `transform` | `use`, `symbol`, `switch` |
| `fill`, `stroke` e as suas propriedades | `text`, `image` |
| `linearGradient`, `radialGradient`, `stop` | |
| `style=` por elemento | `<style>` com CSS de seletor |

**Reportado, nunca descartado em silêncio.** É a mesma regra do
`IconDocument::unknownKeys` (doc 01): um leitor que ignora o que não conhece
afirma sucesso sobre um arquivo que entendeu pela metade. O `SvgDocument` responde
o que viu e não tratou, e o gate conta isso sobre os 149.

## 5. O que NÃO está resolvido

1. **Se o app rasteriza o SVG ou o converte em display list na importação.**
   `IconRendering.SDF.SourceLayer` tem um campo `displayList` (doc 03 §5), o que
   sugere o segundo — mas isso é leitura do nome, não medida.
2. **O que o CoreSVG faz com um elemento que não conhece** — ignora a subárvore
   ou o elemento apenas.
3. **`preserveAspectRatio`**: o CoreSVG o nomeia, e nenhum arquivo do corpus o
   usa. Sem exemplo, sem comportamento a conferir.
4. **As unidades.** 59 arquivos têm `width` com unidade (`px`, `mm`, `%`). Qual
   régua o CoreSVG aplica a cada uma não foi levantado, e o `viewBox` — presente
   em 149 de 149 — pode tornar a pergunta irrelevante.

## 6. O que o leitor faz hoje, medido

`scripts/gate-m1.ps1` roda três gates de SVG sobre os 149 arquivos.

**O caminho de path.** 373 atributos `d`, **372 lidos** em 5.850 segmentos, zero
malformados, e **exatamente um** comando recusado: um `A`. O levantamento do §2
previu dois arcos em dois lugares; implementá-los ganharia um path em 373.

**O XML.** 149 de 149 parseados, 30 nomes de elemento distintos, e o censo bate
elemento a elemento com a sonda independente em Python que produziu a tabela do
§2. Duas implementações concordando.

**O documento.** 149 de 149 lidos em geometria, zero recusados, **477 formas e
6.384 segmentos**.

### E zero de 149 "totalmente compreendidos"

O relatório de cobertura começou dizendo **133 de 149**, e o número era falso. Ele
contava um arquivo como entendido quando toda forma dele virava geometria — mas
este leitor **não lê pintura nenhuma**, e os gradientes e filtros moram dentro de
`<defs>`, que ele pulava sem reportar. Um instrumento que mede a metade fácil e
chama de total é pior do que não medir.

Corrigido, o relatório nomeia cada atributo de pintura e cada tipo de definição.
O número honesto é **zero**, e a lista é o roteiro da próxima rodada, ordenada por
peso real:

| declinado | em N arquivos | | declinado | em N arquivos |
|---|---|---|---|---|
| `paint:fill` | 122 | | `defs:style` | 18 |
| `paint:style` | 67 | | `rect:rounded` | 15 |
| `paint:stroke` | 50 | | `paint:stroke-opacity` | 12 |
| `paint:fill-rule` | 41 | | `paint:fill-opacity` | 9 |
| `defs:linearGradient` | 41 | | `defs:filter` | 9 |
| `paint:stroke-width` | 40 | | `paint:opacity` | 7 |
| `paint:class` | 35 | | `defs:radialGradient` | 4 |

Mais `paint:filter` (5), `paint:mask` (3), `defs:pattern` (3), `defs:image` (3),
`mask` (2), `path:A` (1), `paint:clip-path` (1), `defs:clipPath` (1).

`[INF]` A ordem de trabalho que esses números sugerem é: cor sólida e
`fill-rule` primeiro (122 e 41 arquivos), gradiente linear depois (41), o CSS de
`style=` e `class` em seguida (67 e 35), e cantos arredondados de `rect` (15).
Filtros ficam por último — e três das primitivas mais usadas no corpus a própria
Apple não lê (§3).
