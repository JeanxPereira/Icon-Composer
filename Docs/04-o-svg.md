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

### O relatório de cobertura, e a vez em que ele me pegou

O relatório começou dizendo **133 de 149 totalmente compreendidos**, e o número
era falso. Ele contava um arquivo como entendido quando toda forma dele virava
geometria — mas naquele momento o leitor **não lia pintura nenhuma**, e os
gradientes e filtros moram dentro de `<defs>`, que ele pulava sem reportar. Um
instrumento que mede a metade fácil e chama de total é pior do que não medir.

Corrigido para nomear cada atributo de pintura e cada tipo de definição, o número
honesto caiu para **zero**, e a lista virou o roteiro. Com a pintura implementada
ele está em **98 de 149**.

### A pintura, medida antes de escrita

`[ART]` As formas de valor de `fill`/`stroke` nos 149 arquivos:

| forma | n |
|---|---|
| `url(#id)` | 165 |
| hex de seis dígitos | 162 |
| `none` | 148 |
| `white` | 41 |
| `black` | 29 |
| `color(display-p3 …)` | ~15 |
| hex de três dígitos | 1 |

**Cores nomeadas são duas**, não 147: `white` e `black`. As outras 145 do CSS
ocorrem zero vezes, então a tabela que este leitor carrega tem duas entradas, e
um terceiro nome é uma recusa reportada — não um preto plausível.

`[ART]` Dentro de `style="…"`: `stop-color` 54, `fill` 50, `fill-rule` 34,
`stop-opacity` 34, `mix-blend-mode` 24, `clip-rule` 22, `fill-opacity` 19.
`[ART]` Gradientes: `gradientUnits` em 159, `gradientTransform` em 13, um único
`xlink:href`. Zero `spreadMethod`. `[ART]` Seletores em `<style>`: 16
descendentes e 13 de classe, e nada mais.

> **E uma coisa que a sonda perdeu e o gate achou.** A varredura em Python
> classificou `fill` e `stroke` e concluiu que `rgb()` não ocorre. Ocorre — em
> `stop-color`, e num `fill`. Quem contou foi o relatório de cobertura, porque
> ele imprime **o valor** que não conseguiu ler e não apenas uma contagem:
> `stop-color=rgb(9,111,124)`. Vinte linhas de função depois, o número de
> arquivos totalmente compreendidos subiu de 95 para 98.

## 7. Onde o leitor está

| | |
|---|---|
| lidos | **149 de 149**, zero recusados |
| totalmente compreendidos | **98 de 149** |
| geometria | **477 formas, 6.564 segmentos** |
| paths | 372 de 373, um `A` recusado |

Lê: `svg`/`g`/`defs`, `path`, `rect` (com cantos arredondados), `circle`,
`ellipse`, `line`, `polyline`, `polygon`, `transform` acumulado pela árvore,
`fill`, `stroke`, `fill-rule`, `fill-opacity`, `stroke-opacity`, `stroke-width`,
`style="…"` com precedência sobre o atributo, herança de pintura pela árvore, e
os gradientes lineares e radiais com os seus stops.

O que sobra, e é o roteiro da próxima rodada:

| declinado | em N arquivos | o que falta |
|---|---|---|
| `paint:class` | 35 | o seletor de classe do `<style>` |
| `defs:style` | 18 | a folha de estilo em si |
| `defs:filter` · `paint:filter` | 9 · 5 | filtros |
| `paint:opacity` | 7 | opacidade de grupo, que é composição e não pintura |
| `paint:clip-rule` · `paint:clip-path` · `defs:clipPath` | 4 · 1 · 1 | recorte |
| `paint:mask` · `mask` · `defs:mask` | 3 · 2 · 1 | máscara |
| `defs:pattern` · `defs:image` | 3 · 3 | padrão e imagem embutida |
| `path:A` | 1 | arcos |
| `gradient:href` | 1 | gradiente que herda os stops de outro |

`[INF]` As duas primeiras linhas são o mesmo trabalho — um mini-CSS de seletor de
classe e descendente, 29 regras em 18 arquivos — e sozinhas valem mais que todo o
resto somado.
