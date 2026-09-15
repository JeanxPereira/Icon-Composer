# Uma definição de SVG vale onde ela está, e não só dentro de `<defs>`

Data: 2026-09-15. Terreno: `Source/CoreSVG/Document.cpp`, `Tests/test_svg_document.cpp`,
`Tests/test_render_svg.cpp`.

O oráculo do `AppIcon-27` (§3 de `2026-09-15-oraculo-appicon.md`) pegou um defeito
do **leitor**, não do renderizador: `collectGradient` só era chamado no ramo
`else if (e.name == "defs")`, e os seis SVG do Illustrator 29.0.0 põem o
`<linearGradient>` **dentro do `<g>`** da forma que o usa. O relatório dizia
`6 of 6 layer(s) drawn` e a arte saía sem cor nenhuma — a variante mais cara do
verde vazio. O contorno foi um script (`car_to_icon.py --hoist-gradients`); este
laudo torna o contorno desnecessário.

---

## 1. O que o parser da Apple faz — e ele não olha o pai

`[BIN]` `SVGReader::parseXMLNode` (**CoreSVG.arm64 0x677C**) despacha pelo **átomo
do próprio elemento**, antes de qualquer pergunta sobre onde ele está. O switch
principal (0x6968–0x6CC0):

| átomo | elemento | destino |
|---|---|---|
| `0x05` | `clipPath` | 0x6AD8 → `parseXMLNodeClipPath` **0x2232C** |
| `0x0E` | `defs` | 0x6B44 (o mesmo despacho sobre os filhos) |
| `0x1D` | `g` | 0x6848, desce nos filhos (0x68A4) |
| `0x21` / `0x30` | `linearGradient` / `radialGradient` | 0x6AC0 → `parseXMLNodeGradient` **0x21C5C** |
| `0x3C` | `style` | 0x6CA4 → `parseXMLNodeStyle` **0x2208C** |
| `0x3D` | `svg` | 0x69E0, desce nos filhos (0x6A38) |
| `0x3E` | `symbol` | 0x6AEC — **construído como um `g`** (`mov w1, #0x1d`), filhos lidos em 0x6B20 |
| `0x4A` | `mask` | 0x6C90 → `parseXMLNodeMask` **0x22424** |
| `0x4D` | `image` | 0x69B4 → `parseXMLNodeImage` **0x22798** |
| `0x4F` | `pattern` | 0x6B30 → `parseXMLNodePattern` **0x2251C** |
| `0x54` | `filter` | 0x6CB4 → `parseXMLNodeFilter` **0x22610** |
| `0x4E21` | `use` | 0x6A64 (guarda o `href`, 0x6DEC) |

`[BIN]` Os nomes dos átomos saem da tabela que `SVGAtom::initializeTable`
(**0x27D8**) monta por `SVGAtom::_mapInit` (0xBA9C): cada entrada é um
`adrp/add x1` para a string e um `mov w2, #N` com o valor. `0x1F` é `id`.

`[BIN]` O ramo do `<defs>` (0x6B44) **é o mesmo despacho**, aplicado aos filhos, e
tem uma coisa a mais que o principal não tem: um filho que não é definição é lido
para um nó de rascunho e cada resultado vai para `addDefinitionNode`
(0x6C38–0x6C78) — então `<defs><g><linearGradient>` também é registrado.

`[BIN]` **Todo coletor termina em `SVGNode::addDefinitionNode` (0x2D648)**, que
arquiva o nó pelo `id` num **único mapa na raiz** (`+0xA0`): gradiente, clipPath,
máscara, pattern e filtro dividem esse mapa.

`[BIN]` **São duas passadas.** O construtor `SVGReader::SVGReader` chama
`parseXMLNode` em **0x5910** e só então `resolveDefinitions` (0x7A64) em
**0x5924**; a resolução em si é `ResolveAttributeDefinition` (0x22C40) →
`SVGNode::findDefinitionNode` (0x2D86C). Uma referência **antes** da definição
resolve como qualquer outra.

**Conclusão:** o parser da Apple coleta definição em qualquer lugar. A mudança
aqui é transcrição `[BIN]`, não interpretação da norma — que diz a mesma coisa.

### 1.1 Mas o alcance dele é mais estreito que o documento

`[BIN]` O parser só **desce** em `svg`, `g`, `symbol`, `defs` e no conteúdo de
`clipPath`, `mask` e `pattern` (os três helpers chamam `parseXMLNode` por filho em
0x223D0, 0x224C8 e 0x225BC). Uma primitiva de forma (0x68E8, tabela
`SVGShapeNode::IsValidShapePrimitive` 0x70B0) e **todo o resto** (0x6CE4) viram nó
sem que os filhos sejam lidos. `a` e `switch` **nem são átomos** da tabela de
0x27D8, então caem em 0x6CE4: no alvo, nada dentro de um `<a>` é lido — nem
definição, nem forma.

### 1.2 Id repetido: ganha o ÚLTIMO

`[BIN]` `addDefinitionNode` (0x2D648) lê o atributo `id` (átomo `0x1F`, 0x2D694),
procura no mapa da raiz, e **se já houver um nó com esse id**: solta o antigo
(`release`, 0x2D720) e **apaga a entrada** (0x2D72C) antes de inserir o novo
(0x2D768). A norma diz "o primeiro"; **o alvo diz o último**, e este projeto segue
o alvo.

---

## 2. Quais definições tinham o defeito de alcance — e o censo

`[ART]` Censo dos 149 SVG do corpus (`IC_CORPUS_DIR`), por elemento, contando
ocorrências **dentro** e **fora** de `<defs>`:

| definição | dentro | fora | arquivos com dentro | com fora |
|---|---|---|---|---|
| `linearGradient` | 155 | **0** | 41 | 0 |
| `radialGradient` | 6 | **0** | 4 | 0 |
| `pattern` | 11 | 0 | 3 | 0 |
| `clipPath` | 1 | 0 | 1 | 0 |
| `mask` | 1 | **2** | 1 | 2 |
| `filter` | 26 | 0 | 9 | 0 |
| `style` | 18 | 0 | 18 | 0 |
| `image` | 3 | 0 | 3 | 0 |
| `use` | 11 | 0 | 3 | 0 |
| `symbol`, `marker`, `a`, `switch` | 0 | 0 | 0 | 0 |

`[ART]` **É por isso que 145 documentos nunca expuseram o defeito:** os 161
gradientes do corpus são filhos diretos de `<defs>`, todos eles. Os seis SVG do
`AppIcon-27` são o contrário: **6 gradientes, 6 fora de `<defs>`, em 6 arquivos**.

`[ART]` Ids repetidos: 10 arquivos (todos os `AssetCatalogTinkerer`), e em todos o
id repetido está num `<path>` — **nenhuma definição do corpus repete id**.

E o estado de cada irmão **antes** desta frente:

| definição | como era coletada | tinha o defeito? |
|---|---|---|
| `linearGradient` / `radialGradient` | só filho direto de `<defs>` | **sim, o defeito inteiro** |
| `filter` | passada própria sobre a árvore toda | não |
| `pattern`, `image` | passada própria sobre a árvore toda | não |
| `style` | passada própria sobre a árvore toda | não (ver §5) |
| `clipPath`, `mask` | no `walk`, em qualquer lugar por onde o walk passa | **parcial** (§2.1) |
| `symbol`, `marker` | não coletadas (nomeadas como não desenhadas) | fora de escopo (§5) |

### 2.1 O buraco parcial de `clipPath` e `mask`

O `walk` alcança os dois fora de `<defs>`, mas **não vai** a quatro lugares em que
o alvo vai: (a) debaixo de um grupo cujo filtro colapsa — o `walk` retorna antes
dos filhos; (b) dentro de um filho de `<defs>` que não é definição
(`<defs><g><clipPath>`); (c) dentro de `<symbol>`; (d) dentro do conteúdo de
`<pattern>`. `[ART]` **O corpus não tem nenhum dos quatro** (o único clipPath e as
três máscaras estão onde o walk já passa), então fechar isso não move pixel do
corpus — fecha o mesmo buraco que o gradiente tinha, nos tipos que o dividiam.

---

## 3. O conserto

Em `Source/CoreSVG/Document.cpp`:

1. **`targetDescendsInto`** — a transcrição do §1.1: `svg`, `g`, `symbol`, `defs`,
   `clipPath`, `mask`, `pattern`.
2. **`collectGradients`**, uma passada própria antes do `walk` (junto com
   `collectStyles`, `collectFilters` e `collectPatterns`), sobre esse alcance.
   Passada separada **resolve a referência para frente**: nada exige que a
   definição venha antes de quem a usa, e o alvo também só resolve no fim (0x5924).
3. `walk` ganha um ramo `linearGradient`/`radialGradient` que **retorna sem
   desenhar e sem acusar** — antes eles caíam no ramo final e viravam
   `unsupported: linearGradient`, enquanto a forma perdia a cor.
4. **`collectDefinitionsOf` / `collectDefinitionsIn`** recolhem `clipPath` e `mask`
   nos quatro lugares do §2.1.
5. **Id repetido: o último ganha.** `clipPaths[id]` **somava** os caminhos do
   segundo aos do primeiro (a união, que não é nenhuma das duas definições); agora
   apaga. `masks` e `patterns` também apagam a entrada anterior antes de reler, e
   uma definição posterior **recusada** (unidades que este leitor não inventa)
   também desloca a anterior — no alvo a anterior sumiu de qualquer jeito.
6. **`nameIdsSharedAcrossKinds`**: um id usado por **dois tipos** de definição não
   tem resposta possível nos mapas por tipo deste leitor (o alvo guarda um mapa
   só, e nele o último ganha). Isso é **nomeado** —
   `id repetido entre definicoes de tipos diferentes: <id>` — e não respondido em
   silêncio com o gradiente. `[ART]` Nenhum arquivo do corpus faz isso.

A linha `if (f != filters.end() && f->second.collapses()) return;` **continua
intacta e única**: ela é âncora de duas mutações do `gate-m1.ps1`. Conferi por
script que as 31 âncoras de `svgdoc`/`svgdoch` ainda ocorrem **exatamente uma vez**
cada.

Testes novos (7), em `Tests/test_svg_document.cpp` e `Tests/test_render_svg.cpp`:

- gradiente **fora de `<defs>` e depois** da forma que o nomeia (referência para
  frente) — e o relatório fica vazio, sem `linearGradient`;
- gradiente em `<g><g>`, em `<defs><g>` e em `<symbol>`;
- gradiente dentro de `<a>`, onde o alvo **não** desce: não é coletado;
- id repetido: último ganha, e o clipPath não vira união;
- id dividido entre dois tipos: nomeado;
- clipPath sob filtro que colapsa + máscara em `<defs><g>`: coletados, sem
  geometria fantasma (o número de formas não muda);
- **o pixel**: o mesmo gradiente dentro e fora de `<defs>` dá **0 componente
  diferente** em 32×32, e o canal azul do primeiro stop chega ao pixel — porque
  contar camadas é exatamente o que não pegou este defeito.

---

## 4. As provas

### 4.1 Sem içar × içado — o teste de aceitação, que não supõe nada sobre a Apple

`icrender <bundle> --size 412 --appearance dark`, os dois bundles do
`References/27.0-129/out`:

| | pixels visíveis | diferentes | Δ máximo (R,G,B) |
|---|---|---|---|
| **antes** | 159.964 | **79.214 (49,52 %)** | 167 / 187 / 246 |
| **depois** | 159.964 | **0 (0,00 %)** | 0 / 0 / 0 |

Os dois PNG saem com o **mesmo SHA-256**, e o render do bundle içado é
**byte a byte o mesmo de antes** da mudança — o conserto não mexeu em quem já
funcionava. O `stderr` perdeu as doze linhas
`elemento nao desenhado: linearGradient` / `url(#SVGID_1_) nao resolve para nenhum
gradiente do documento`.

### 4.2 Contra o gabarito da Apple

`apple-512.png` × render de 412 posto em (50,50) num quadro de 512, cópia pixel a
pixel (`scripts/png-diff.py`, alinhamento do §6.2 do laudo do oráculo):

| render | pixels diferentes | Δ médio R | G | B |
|---|---|---|---|---|
| sem içar, **antes** | 159.324 | 20,92 | 47,49 | 60,19 |
| sem içar, **depois** | 160.327 | **8,95** | **10,03** | **9,89** |
| içado (referência) | 160.327 | 8,95 | 10,03 | 9,89 |

`[ART]` O diff do bundle **sem içar** passou a ser **idêntico** ao do içado, que é
o que se pedia. (Os valores absolutos são os do `HEAD` de hoje, `cc97cc1`, e não os
do laudo do oráculo: o realce por VCM mudou a imagem no meio do caminho.) A contagem
de pixels diferentes sobe 1.003 enquanto o erro médio cai por 4× — antes, o que
"acertava" era pixel sem cor nenhuma coincidindo com o fundo.

### 4.3 Os portões

| medida | antes | depois |
|---|---|---|
| gate SVG (`corpus_svg_document_gate`) | 149 lidos, 0 recusados, **140** totalmente compreendidos, 445 formas, 6.242 segmentos | **idêntico** |
| gate de caminhos | 149 SVGs, 373 `d`, 372 lidos, 0 malformados | idêntico |
| suíte (`-DIC_BUILD_UI=OFF`) | 639 casos, 0 falhas | **646 casos, 0 falhas** (107,4 s) |

`[OBS]` O `Docs/README.md` diz **139** totalmente compreendidos; a medida de hoje
dá **140** antes e depois desta frente. A diferença é anterior a ela e não foi
investigada aqui.

---

## 5. O que fica aberto — divergências medidas e **não** consertadas

- `[BIN]` **A folha de estilo do alvo só vale para quem vem depois dela.**
  `parseXMLNodeStyle` (0x2208C) cria **um** `SVGStyle` na primeira `<style>`
  (0x220BC/0x220D8) e acumula com `addDefinitionsFromCSS` (0x16F10), mas
  `applyStyleToAttributes` (0x21898) lê `reader+0x10` **no instante em que cada nó
  é lido** (0x218B8): um elemento anterior à `<style>` não recebe regra nenhuma.
  Este leitor coleta as folhas numa passada e aplica a todos. `[ART]` **0 dos 149
  arquivos** põem um usuário de `class` antes da `<style>`, então a divergência não
  move pixel do corpus. Não mexi: é regra de CSS, não de alcance.
- `[BIN]` **`<a>`**: o alvo não lê os filhos (0x6CE4), este leitor desenha-os como
  um grupo. `[ART]` 0 arquivos no corpus. Mudar isso é decisão de **desenho**, fora
  do escopo desta frente.
- `[BIN]` **`<symbol>`** é construído como um `g` pelo parser (0x6AEC) e anexado ao
  pai (0x6D18); aqui ele continua **nomeado e não desenhado**. `[OBS]` Se o
  renderizador do alvo pinta esse `g` no lugar em que ele está, ou se algo mais
  adiante o pula, não foi lido. `[ART]` 0 símbolos no corpus.
- `[OBS]` **A CTM de uma definição.** `collectClipPath`/`collectMask` assam a
  transformada do lugar onde a definição está; em SVG o conteúdo de um `clipPath`
  com `userSpaceOnUse` vive no espaço de quem o **referencia**. As duas respostas
  coincidem quando definição e referenciador compartilham a CTM, que é o caso do
  corpus inteiro (nenhum dos quatro `clipPath`/`mask` carrega `transform`). E há
  uma inconsistência interna anterior a esta frente: o ramo do `walk` inclui o
  `transform` do próprio `clipPath`, o ramo do `<defs>` não. Não mexi.
- `[OBS]` **Mutação**: não acrescentei entradas ao `scripts/gate-m1.ps1` — o
  arquivo é compartilhado com as frentes irmãs. As três que valem, para quem
  integrar: `b.collectGradients(xml->root);` → `void(0);`; `if (!targetDescendsInto(e.name)) return;`
  (em `collectGradients`) → `return;`; e `clipPaths.erase(*id);` → `(void)0;`.
- `scripts/car_to_icon.py --hoist-gradients` **continua existindo** e continua
  funcionando; com esta mudança ele é desnecessário, mas outra frente está usando o
  bundle `AppIcon-27-defs.icon` agora, então a bandeira fica.

---

## 6. Reprodução

```
cmake --preset mingw -DIC_BUILD_UI=OFF
cmake --build --preset mingw --target ic_tests icrender
$env:IC_CORPUS_DIR='...\References\corpus'; build\mingw\Tests\ic_tests.exe

build\mingw\Source\cli\icrender.exe References\27.0-129\out\AppIcon-27.icon      --out raw.png  --size 412 --appearance dark
build\mingw\Source\cli\icrender.exe References\27.0-129\out\AppIcon-27-defs.icon --out defs.png --size 412 --appearance dark
python scripts\png-diff.py raw.png defs.png      # 0 pixels diferentes
```

Os endereços de §1: `python scripts/macho.py dis References/2.0-125/out/slices/CoreSVG.arm64 0x677C 1020`
e `... dis ... 0x2D648 100`.
