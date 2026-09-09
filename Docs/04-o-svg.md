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

## 3. O achado: a Apple constrói **seis** primitivas, e derruba o resto

> **Esta seção foi reescrita em 2026-09-09.** A versão anterior lia o suporte a
> filtro por **strings** e concluiu, com selo `[INF]`, que o CoreSVG suportava
> oito primitivas e que os três ausentes eram "descartados em silêncio". A
> tabela do binário diz outra coisa, em duas direções — e o que acontece com a
> cadeia não é um descarte silencioso. O que segue é `[BIN]`, com endereços.

`[BIN]` `SVGFilter::filterPrimitive(SVGAtom::Name)` — **CoreSVG.arm64 0x2A230** —
varre uma tabela de **seis `uint32`** em `__const:0x327F0`:

```
{0x67, 0x5f, 0x5b, 0x72, 0x65, 0x78}
```

Um nome que não casa cai em `SVGAtom::ToString`, `SVGUtilities::log("Filter
primitive: <%s> is currently not supported.")`, e a função retorna **nullptr**:
a primitiva **nunca é construída** e nunca entra na cadeia.

`[BIN]` `SVGFilterPrimitive::selectPrimitive()` — **0x23B48** — despacha os
**mesmos seis** átomos, com `x19` nascendo em 0, e devolve nullptr para
qualquer outro. As duas listas batem exatamente.

| átomo | primitiva | `draw*` |
|---|---|---|
| `0x67` | `feGaussianBlur` | `0x23D20` |
| `0x5f` | `feOffset` | `0x23E94` |
| `0x5b` | `feFlood` | `0x23F7C` |
| `0x72` | `feComposite` | `0x240C8` |
| `0x65` | `feBlend` | `0x24BFC` |
| `0x78` | `feConvolveMatrix` | `0x24F74` |

**As duas correções à leitura antiga:**

- `feImage`, `feMerge` e `feTile` **não desenham**. As strings existem no
  binário — são átomos, e por isso a leitura por strings as pegou — mas
  nenhuma está na tabela nem tem uma função `draw*`.
- `feConvolveMatrix` **desenha**, e nunca tinha sido listada.

`feColorMatrix`, `feMorphology` e `feDropShadow` seguem fora, como a leitura
antiga dizia — mas pela tabela, e não pela ausência da string.

### 3.1. E o buraco colapsa a cadeia inteira

O erro da conclusão antiga não foi a lista: foi supor que uma primitiva
derrubada é só uma etapa a menos. `[BIN]` Ela não é.

| onde | endereço | o que faz |
|---|---|---|
| `inputImage<T>` | `0x2520C` | atributo ausente → resultado anterior; nome presente e **fora do mapa → nullptr** |
| `drawFeBlend` | `0x24BFC` | `x22` nasce 0; `cbz x19` / `cbz x20` → **null entra, null sai** |
| `drawFeComposite` | `0x240C8` | mesma forma (`x25 = 0`, guardas em `0x2412C`/`0x24130`) |
| `SVGFilter::draw` | `0x29A34` | resultado final nulo → `0x29E58`, que é **só cleanup** |
| `PopSVGNodeAttributes` | `0xA85C` | ignora o retorno; **não existe plano B** |

**Logo: um elemento cuja cadeia nomeia uma primitiva que o alvo não constrói
DESENHA NADA.** Não "desenha sem o filtro".

Isso inverte o conselho da versão antiga desta seção. Ela dizia que
implementar `feColorMatrix` nos deixaria *mais certos que o alvo*, o que
continua verdade; mas o que ela não viu é que **desenhar a forma sem o filtro
também** produz pixel que o alvo nunca faz. As duas leituras plausíveis estavam
erradas, e a medida é uma terceira coisa.

`[ART]` No corpus: **17 das 18 cadeias** caem nessa regra, todas por
`feColorMatrix` (14 também por `feMorphology`). São o `PiStats` e três dos
assets do `Delta`, todos exports do Figma — inner shadow e drop shadow. A 18ª,
o borrão do `00_stripes.svg` do Delta, está inteira dentro das seis.

### 3.2. O que o nosso lado faz com isso

`SvgDocument` reproduz a regra: um `<filter>` com primitiva derrubada marca
`Filter::collapses()`, e o elemento que o referencia não vira forma nenhuma — e
**não é reportado em `unsupported()`**, porque reproduzir o alvo não é uma
lacuna nossa.

`SvgFilter.cpp` transcreve **três** das seis — `feFlood`, `feBlend` (modo
`normal`) e `feGaussianBlur` — que são a cadeia limpa inteira. `feOffset`,
`feComposite` e `feConvolveMatrix` recusam por nome: o alvo as desenha, então
uma cadeia que use qualquer uma **é** lacuna nossa e tem que dizer.

`[OBS]` **A largura do borrão não é afirmada igual à do alvo.** `[BIN]`
`drawFeGaussianBlur` clampa `stdDeviation` em **100** (`fminnm` contra
`0x4059000000000000`), recusa anisotropia por log
(`"Different radii for gaussian blur not supported"`, `0x23D88`) seguindo com o
componente X, e entrega o número ao **`inputRadius` do `CIGaussianBlur`**
(`0x23DF4`). `inputRadius` não é o sigma da SVG, e o kernel que o CoreImage
constrói a partir dele mora num framework que este projeto não leu. O
encanamento medido está transcrito; o kernel é a gaussiana da própria SVG 1.1,
e todo render que roda um borrão o declara.

## 4. O escopo desta rodada

Um leitor de SVG completo é um projeto por si. O que o `.icon` precisa, por
ordem de peso no corpus, é **geometria e pintura**:

| entra | fica de fora, e é REPORTADO |
|---|---|
| `svg` com `viewBox`, `g`, `defs` | `feOffset`, `feComposite`, `feConvolveMatrix` |
| `path`, `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon` | `pattern` |
| `transform`, `mask`, `clipPath`, `opacity` | `use`, `symbol`, `switch` |
| `fill`, `stroke` e as suas propriedades | `text`, `image` |
| `linearGradient`, `radialGradient`, `stop` | |
| `filter` com `feFlood`, `feBlend`, `feGaussianBlur` | |
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
`xlink:href`. Zero `spreadMethod`. `[ART]` Seletores em `<style>`: **29 regras, todas de
classe única** — nenhum id, nenhum elemento, nenhum descendente, nenhuma
pseudo-classe, nenhuma at-rule, nenhum comentário.

> **Correção.** Uma primeira contagem registrou aqui "16 descendentes e 13 de
> classe". Era artefato do regex, que capturava a quebra de linha antes do ponto
> e via um espaço onde não havia seletor nenhum. Todas as 29 são `.nome`.

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
| totalmente compreendidos | **128 de 149** |
| geometria | **477 formas, 6.564 segmentos** |
| paths | 372 de 373, um `A` recusado |

Lê: `svg`/`g`/`defs`, `path`, `rect` (com cantos arredondados), `circle`,
`ellipse`, `line`, `polyline`, `polygon`, `transform` acumulado pela árvore,
`fill`, `stroke`, `fill-rule`, `fill-opacity`, `stroke-opacity`, `stroke-width`,
`style="…"`, a folha de estilo `<style>` com seletor de classe, herança de
pintura pela árvore, e os gradientes lineares e radiais com os seus stops.

### A cascata

`[INF]` A ordem é a do CSS, e não é óbvia:

```
atributo de apresentação   <   regra da folha de estilo   <   atributo style=
```

Um `fill="black"` **perde** para um `.fil0 { fill:#000066 }`. Inverter os dois
primeiros pinta 35 arquivos com a cor da qual eles foram sobrescritos.

### O que sobra

| declinado | em N arquivos |
|---|---|
| `defs:filter` · `paint:filter` | 9 · 5 |
| `paint:opacity` — opacidade de grupo, que é composição e não pintura | 7 |
| `paint:clip-rule` · `paint:clip-path` · `defs:clipPath` | 4 · 1 · 1 |
| `paint:mask` · `mask` · `defs:mask` | 3 · 2 · 1 |
| `defs:pattern` · `defs:image` | 3 · 3 |
| `path:A` — arcos | 1 |
| `gradient:href` — gradiente que herda os stops de outro | 1 |
| `class:st3`…`st6` | 1 |

`[ART]` A última linha não é limitação do leitor. O
`PDF-Archiver/AppIcon-text.svg` usa `class="st6"` e **não tem bloco `<style>`
nenhum** — as classes dele são órfãs no próprio arquivo. O relatório achou uma
anomalia real, que é exatamente o que um relatório de cobertura serve para
achar quando não está mentindo.
