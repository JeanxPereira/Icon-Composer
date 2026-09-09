# Spec — o `<pattern>`, o `<use>` e o raster embutido, e por que são **um**

*2026-09-09. Escrito depois de a régua ter isolado o que sobrou e de a leitura do
binário confirmar que o alvo desenha isto de verdade — ao contrário do filtro,
onde a mesma pergunta virou uma recusa.*

---

## 1. Três linhas da régua, um construto

Depois das duas frentes de filtro de hoje, o que bloqueia é:

| bloqueio | camadas | documentos |
|---|---|---|
| `use` | 3 | 1 |
| `pattern` | 3 | 1 |
| `raster embutido` | 3 | 1 |
| vidro sobre raster | 1 | 1 |

**As três primeiras são o mesmo documento e o mesmo construto.** O Figma exporta
"esta forma é preenchida por uma imagem" assim:

```xml
<path d="..." fill="url(#pattern0_2033_33)"/>
...
<defs>
  <pattern id="pattern0_2033_33" patternContentUnits="objectBoundingBox"
           width="57.7728" height="51.2125">
    <use xlink:href="#image0_2033_33" transform="scale(0.00285714)"/>
  </pattern>
  <image id="image0_2033_33" width="1024" height="1024"
         preserveAspectRatio="none" xlink:href="data:image/png;base64,iVBOR..."/>
</defs>
```

Contar isso como três bloqueios distintos é contar um problema três vezes.
**Uma frente fecha as três**, e com elas o Delta — o último documento do corpus
fora da fatia que não seja o vidro sobre raster.

---

## 2. O que o corpus realmente pede — medido, não suposto

Censo sobre os 149 SVGs: **3 arquivos**, todos do Delta, **11 definições** de
`<pattern>` e **11 referências**.

| atributo | ocorrências | valores |
|---|---|---|
| `patternContentUnits` | 11 de 11 | `objectBoundingBox`, sempre |
| `patternUnits` | **0** | ausente → o default da SVG, `objectBoundingBox` |
| `width` / `height` | 11 de 11 | 2,93 a 82,34 |
| `patternTransform` | **0** | — |
| `x` / `y` | **0** | ausentes → 0 |

O `<use>` dentro do pattern carrega **só** `xlink:href` e `transform`, e o
transform é sempre um `scale` uniforme. O `<image>` é sempre 1024×1024,
`preserveAspectRatio="none"`, com um `data:image/png;base64`.

`[ART]` **A largura do ladrilho é MAIOR que a caixa da forma** — `width` em
unidades de `objectBoundingBox` vale 57,77 caixas. Então **o ladrilhamento nunca
acontece neste corpus**: um único ladrilho cobre a forma inteira. O
ladrilhamento será construído mesmo assim, porque recusar o segundo ladrilho
seria um limite sem razão — mas o que o corpus *exercita* é a colocação única, e
isso fica dito.

`[ART]` E `1024 × 0,00285714 = 2,9257`, que é exatamente o `width` do pattern do
`texture.svg`. O `scale` do `<use>` existe para levar os 1024 px da imagem ao
tamanho do ladrilho em unidades de caixa.

O PNG embutido: **colour-type 6 (RGBA), 8 bits, não entrelaçado**, 226 chunks
`IDAT`. É o caso que `Png.h` já lê — Adam7, a única lacuna conhecida do
decodificador, não aparece aqui.

---

## 3. O alvo desenha isto, e aqui a string **não** engana

A lição de hoje foi que a presença de uma string no binário não prova que o
elemento é desenhado: `feImage`, `feMerge` e `feTile` existem como átomos e não
estão na tabela de seis (doc 04 §3). Por isso esta seção foi medida antes de uma
linha ser escrita.

`[BIN]` `SVGPattern` é uma **classe inteira**, não um átomo solto:

| símbolo | endereço |
|---|---|
| `SVGPattern::draw(CGContext*)` | `0x12184` |
| `SVGPattern::drawCells(CGContext*, CGRect, double)` | `0x11D50` |
| `SVGPattern::inheritParentPattern()` | `0x11C08` |
| `SVGPattern::attributeIsUserSpace(SVGAtom::Name, bool)` | `0x11FA8` |
| `SVGReader::parseXMLNodePattern(_xmlNode*, SVGNode*)` | `0x2251C` |
| `ResolvePatternHrefDefinition(SVGNode*, SVGAtom::Name)` | `0x23154` |
| `ConvertUseElementCoordinates(SVGAttributeMap*, CGRect&)` | `0x22AC` |

e ela chama `CGPatternCreate`, `CGContextSetFillPattern` e
`CGColorSpaceCreatePattern`. `drawCells` é literalmente o ladrilhamento.

`[BIN]` `attributeIsUserSpace` (`0x11FA8`) distingue as duas unidades **pelo
comprimento da string** — `cmp w8, #0xe` para `userSpaceOnUse` (14 caracteres) e
`cmp w8, #0x11` para `objectBoundingBox` (17) — e devolve o `bool` que o chamador
passou quando o atributo está ausente. Ou seja: o default é do chamador, e cada
um dos dois atributos tem o seu, como a SVG manda.

**Nada exótico.** Ao contrário do filtro, não há aqui uma regra do alvo que
contrarie a especificação, e nada foi encontrado que peça uma recusa.

---

## 4. O que entra

Quatro peças, e nenhuma delas é um renderizador novo.

### 4.1. O leitor: `<pattern>`, `<use>` e `<image>`

`SvgDocument` ganha:

```cpp
struct Pattern {
    double x = 0, y = 0, width = 0, height = 0;
    bool unitsUserSpace = false;        // patternUnits, default objectBoundingBox
    bool contentUserSpace = true;       // patternContentUnits, default userSpaceOnUse
    Transform transform;                // patternTransform
    std::string imageId;                // o que o <use> nomeia
    Transform useTransform;             // o transform do <use>
};
std::map<std::string, Pattern> patterns;

struct EmbeddedImage {
    std::uint32_t width = 0, height = 0;
    std::vector<float> rgba;            // straight, decodado uma vez
};
std::map<std::string, EmbeddedImage> images;
```

Coletados numa passagem própria, como os filtros — pela mesma razão: o Figma põe
o `<defs>` no fim do arquivo.

**O que é recusado por nome**, porque o corpus não exercita e adivinhar seria
inventar: um `<pattern>` cujo conteúdo não seja exatamente um `<use>` apontando
para um `<image>`; um `href` que não resolva; um `data:` que não seja
`image/png;base64`; `patternUnits="userSpaceOnUse"` (nenhum dos 11 usa, e a
geometria é outra).

### 4.2. O `data:` URI e o base64

Um decodificador de base64 e o `Png.h` que já existe. `[OBS]` O `sRGB` chunk é
lido e **ignorado** — a conversão de espaço de cor não foi medida do alvo, e é a
mesma nota que o `display-p3` já carrega.

### 4.3. O mapa do pixel ao ladrilho

Para uma forma de caixa `(bx0,by0)-(bx1,by1)`:

- `objectBoundingBox` (o único caso do corpus): o ladrilho tem
  `w = width * (bx1-bx0)` e `h = height * (by1-by0)`, ancorado em
  `(bx0 + x*(bx1-bx0), by0 + y*(by1-by0))`.
- o conteúdo, sob `patternContentUnits="objectBoundingBox"`, é escalado pela
  caixa antes do `transform` do `<use>`.
- o pixel cai no ladrilho por `fmod` nas duas direções — o ladrilhamento, que o
  corpus não exercita e que existe para não ser um limite inventado.

A amostragem é **premultiplicada**, pela razão que a gaussiana de hoje já
registra e que o amostrador de raster já pagou uma vez: média de cor direta puxa
a cor invisível dos pixels transparentes.

### 4.4. O renderizador

`resolveGradient` hoje devolve *"url(#X) nao resolve para nenhum gradiente do
documento"* para uma referência de pattern — **uma mensagem falsa**, que manda
quem lê o relatório procurar um `<linearGradient>` que nunca existiu. A
resolução de pintura passa a distinguir os três casos: gradiente, pattern, e
nada.

---

## 5. O que isto vale, e o que fica

Se a frente fechar, a régua vai a **191 de 194 camadas** e **52 de 55
documentos** — 52 sobre o teto real de 53 (doc 02 §3: `chromium` e `loupe` pedem
arte que o autor renomeou antes de publicar).

Fica **uma** camada: `vidro sobre raster`, onde `generateField` come polilinhas e
um raster não tem caminho para achatar.

`[OBS]` E fica dito que **um ladrilho é o que o corpus prova**. O segundo
ladrilho será desenhado e não terá testemunha no corpus — como os 7 caps e os 3
joins do traço, dos quais o corpus exercita um par (doc 03 §31).
