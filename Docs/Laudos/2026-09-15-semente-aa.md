# A semente anti-serrilhada do campo de distância

**Data** 2026-09-15 · **Terreno** `Source/RenderBox/DistanceField.{h,cpp}`
**Alvo da prova** `D:/CodingProjects/GoWToolkit/dist/macos/GoWToolkit.icon`, `--idiom square`

---

## §0 O que este laudo decide

O especular sai picotado na borda porque a semente da transformada de distância é
**binária**: cada texel é dentro ou fora, e o zero do campo fica preso ao centro
do texel. A banda que o especular acende tem ~1 px de largura, e uma banda de 1
px lida num campo quantizado em 1 px acende por texel inteiro.

Este laudo (a) fecha a via medida — os três botões de `SDFGeneration` **não
existem como código** neste binário, e o gerador do alvo está fora do dump —,
(b) troca a semente por uma de **cobertura sub-texel**, `[INF]`, e (c) mede.

---

## §1 A VIA MEDIDA NÃO ABRIU. As provas.

O `[OBS]` do laudo do vidro sobre raster mandava ler
`ICRRenderingParameters.SDFGeneration` antes de escrever algoritmo. Foi lido, e
o resultado é negativo — com endereços, para que ninguém pague a busca de novo.

**A estrutura existe** (`fieldmd`, `0xA46E0`, 4 campos):

| campo | tipo |
|---|---|
| `clampThreshold` | `Sd` (Double) |
| `useAdvancedStacking` | `Sb` (Bool) |
| `precisePixelFormatThreshold` | `Si` (Int) |
| `maxRelativeSmoothing` | `Sd` (Double) |

**E é tudo o que existe.** Os nomes aparecem em exatamente três lugares de
`IconRendering.arm64`:

1. `__swift5_reflstr` (`0xA0920`–`0xA2A96`) — o metadado de reflexão;
2. `__cstring` `0xA6A00` (`useAdvancedStacking`), `0xA6A20`
   (`precisePixelFormatThreshold`), `0xA6A40` (`maxRelativeSmoothing`);
3. `__LINKEDIT` — a tabela de nomes de símbolo.

`clampThreshold` **não tem cópia no `__cstring`**, e a razão confirma a leitura
em vez de a contradizer: tem 14 bytes, cabe numa *small string* de Swift, e é
montada por imediatos — `mov x10, #0x6c63` / `movk #0x6d61, lsl 16` /
`movk #0x5470, lsl 32` / `movk #0x7268, lsl 48` em `0x61B24`–`0x61B30`, que é
`"clampThr…"`. As outras três passam de 15 bytes e por isso precisam de literal.
*(A lição de hoje, outra vez: constante por `mov`+`movk` não está no pool.)*

**Quem toca nessas literais no `__text`: dois sítios, e os dois são Codable.**

| endereço | função | tamanho |
|---|---|---|
| `0x00061B10` | `CodingKeys.stringValue` (getter) | 144 bytes |
| `0x000744BC` | `CodingKeys.init?(stringValue:)` | 368 bytes |

Nenhum terceiro. Nenhum leitor que faça trabalho com o valor.

**E o gerador está fora do dump.** `sdfTextureWithBufferAllocator:` é chamado em
`0x000867F8`, dentro de `0x0008658C`. O **único** argumento que atravessa essa
chamada é `x2`, montado em `0x000867D8`–`0x000867E8`, e é o *alocador de
buffer* — `0x8658C` passa a função inteira a colher
`minimumTextureBufferAlignmentForPixelFormat:` (`0x00086628`) por formato e a
reduzir ao **máximo** (`cmgt`/`bit` em `0x86724`–`0x86774`, semeado com 1).
Nenhum limiar, nenhuma suavização cruza a fronteira. O receptor é um
`CUINamedLayerImage`, classe do CoreUI, que **não está em nenhuma fatia deste
projeto**.

Duas confirmações laterais: `RenderBox.arm64` tem **11 996 símbolos e zero** que
casem `sdf`/`SDF`; e `IconRendering.arm64` não tem seletor nenhum com `smooth`,
`clamp` ou `antialias` (só `addAlphaThresholdFilterWithMinAlpha:maxAlpha:color:colorSpace:`,
que é do anel da sombra e já está lido).

> **Conclusão.** Os três botões são **configuração morta neste binário**: nomes
> em metadado e em `CodingKeys`, decodificados e nunca lidos por código que
> desenhe. Quem os consome é o CoreUI, por um caminho que este dump não tem. A
> via medida está fechada, e o que segue é **`[INF]`** — aproximação nossa,
> declarada como tal.

---

## §2 O conserto, `[INF]`

Cinco peças. Nenhuma foi lida de binário nenhum.

**1. A semente carrega a cobertura.** `sOff[t] = 0.5 − cobertura[t]` é a
distância **com sinal** do centro do texel até a superfície, positiva fora. Não
é convenção inventada aqui: é o inverso exato da convenção de cobertura que esta
torre já escreve (`cov = −d/aaWidth + 0.5` com `aaWidth = 1`), então semente e
canal de cobertura são duas leituras de uma regra só e não podem divergir.

**2. A banda.** Um texel é banda quando tem cobertura parcial **e** um vizinho-4
de classe oposta. A segunda condição não é enfeite: cobertura parcial sozinha
marcaria todo texel de um preenchimento uniformemente **translúcido** (alpha
0.5 em toda parte ⇒ `0.5 − c = 0` em toda parte) e colapsaria o campo a zero. Um
platô translúcido não tem vizinho de classe oposta.

**3. O ponto de superfície — e é aqui que estava o erro.** O movimento ingénuo é
manter a distância da rede e encurtá-la pelo `sOff` da semente. **Está errado**,
e mensuravelmente: o `sOff` corre ao longo da **normal** e a distância da rede
corre ao longo da linha entre dois centros, e somar uma à outra como se fossem
colineares custa até meio texel. Foi medido: o pior erro de um círculo foi de
0,49 para **1,02** texel antes de isto ser apanhado.

O que a semente sabe de facto é um **ponto**: a superfície passa por
`semente − sOff · normal`. A distância euclidiana simples do texel a **esse
ponto** está certa seja qual for o ângulo, é **exata** para uma aresta reta em
qualquer deslocamento sub-texel, e devolve de brinde um gradiente que se move
continuamente com a cobertura.

**4. A normal, por Sobel da cobertura.** O vetor entre dois centros a um texel de
distância só tem **oito direções**, 45° de quantização angular — exatamente nos
texels que o especular acende. A cobertura não tem esse piso.

**5. O mínimo local 3×3.** A transformada escolhe a semente cujo **centro** está
mais perto, que deixou de ser a mesma pergunta assim que cada semente passou a
ter um ponto de superfície meio texel ao lado. Perto da banda as duas respostas
discordam, e isso era o pior erro do método. Resolve-se com oito leituras por
texel: o mínimo sobre os pontos de superfície da vizinhança 3×3. Sem segunda
transformada.

**A cobertura do contorno** (`coverageFromContours`) é **exata em x** — os
intervalos internos de uma scanline são exatos em x e a sobreposição com uma
coluna é uma subtração — e quantizada em y por `1/16`. `rasteriseContours` ficou
**intacto**, empates e tudo: a **classe** (e portanto a silhueta e o sinal) não
se mexe; a cobertura só diz *onde dentro do texel* a superfície corre.

Binário na entrada ⇒ toda semente volta a `±0.5` e a aritmética é a de antes.
O interruptor é `FieldOptions::subpixelSeed` (padrão `true`).

### Por que 16 sub-linhas e não 4

Não por gosto — porque 4 e 8 deixavam uma **regressão de pior caso**:

| sub-linhas | rms na banda (×) | pior erro | vs. binária (0,497) |
|---|---|---|---|
| 4 | ×3,61 | 0,703 | **pior** |
| 8 | ×4,49 | 0,623 | **pior** |
| **16** | **×4,71** | **0,266** | melhor |

E não custa: a 1024 px, 4/8/16 mediram 0,423 / 0,427 / 0,436 s — as varreduras
de scanline não são o gargalo, a transformada é. 16 é o menor que remove a
regressão.

---

## §3 O campo, contra a verdade analítica

Círculo de raio `0,32·n`, erro contra o campo exato. **Na banda `|d| ≤ 1,5 px`**
— que é o que o especular lê:

| n | rms binária | rms sub-texel | ganho | pior binária | pior sub | grad binária | grad sub | ganho |
|---|---|---|---|---|---|---|---|---|
| 128 | 0,2477 | **0,0517** | ×4,79 | 0,4905 | 0,1576 | 28,96° | **11,25°** | ×2,57 |
| 256 | 0,2443 | **0,0524** | ×4,67 | 0,4976 | 0,1577 | 29,20° | **12,05°** | ×2,42 |
| 512 | 0,2497 | **0,0520** | ×4,80 | 0,4971 | 0,2154 | 29,22° | **12,42°** | ×2,35 |
| 1024 | 0,2583 | **0,0549** | ×4,71 | 0,4974 | 0,2659 | 29,40° | **12,60°** | ×2,33 |

**O diagnóstico confirma-se, e é a linha mais importante da tabela:** o erro da
semente binária vai de **0,2477 a 128 px para 0,2583 a 1024 px**. Oito vezes mais
pixels e o erro **não desce** — é um quarto de texel em qualquer resolução. Não é
amostragem; é estrutura. O pior caso da binária é **0,49 em todas as quatro
resoluções**: meio texel, exatamente o que uma semente presa ao centro do texel
tem de custar.

O gradiente sai de **29°** de erro rms para **12°**. Os 29° são o piso da rede:
a um texel de distância só há oito direções.

`[OBS]` **Fora da banda o pior caso piorou.** Sobre `|d| ≤ 6 px` o pior erro
sub-texel cresce com a resolução — 0,409 / 0,569 / 0,655 / **0,751** — e a 1024
ultrapassa o 0,497 da binária. É a discordância entre "centro mais próximo" e
"superfície mais próxima", e o refino 3×3 só alcança um texel. O rms nessa faixa
continua 2,6× melhor (0,263 → 0,103). Quem ler o campo longe da borda deve saber
disto.

---

## §4 Os pixels, sobre o ícone do usuário

`GoWToolkit.icon`, `--idiom square`, `1 of 1 layer(s) drawn` nas quatro
renderizações (não é verde vazio). Visíveis = alpha > 0 numa das duas.

| | 512 | 1024 |
|---|---|---|
| pixels visíveis | 247 188 (94,3 %) | 987 176 (94,1 %) |
| **mudaram** | **11,47 %** | **10,17 %** |
| delta máximo | 255 | 255 |
| delta > 64 | 0,89 % | 0,73 % |
| delta > 128 | 0,40 % | 0,26 % |

O delta de 255 é real e não é susto: são pixels da própria banda especular, onde
o realce branco encosta no preenchimento. Deslocar a banda por uma fração de
pixel troca `[255,255,255]` por `[224,160,84]` num texel. **Menos de 0,4 % dos
visíveis** passam de 128; a esmagadora maioria da mudança é pequena.

## §5 O serrilhado — a medida que distingue "ficou liso" de "ficou diferente"

Duas métricas, sobre a **banda** (os pixels que a mudança toca, dilatados de 2,
região definida **igual para as duas imagens**, e normalizada por 1000 px de
banda para 512 e 1024 serem comparáveis):

**512 px** (banda: 48 278 px)

| | antes | depois | |
|---|---|---|---|
| saltos > 8 níveis / 1000 | 531,1 | 497,6 | **−6,3 %** |
| saltos > 16 / 1000 | 416,9 | 372,5 | **−10,6 %** |
| saltos > 32 / 1000 | 307,7 | 257,0 | **−16,5 %** |
| \|laplaciano\| médio | 61,70 | 45,17 | **−26,8 %** |

**1024 px** (banda: 164 869 px)

| | antes | depois | |
|---|---|---|---|
| saltos > 8 níveis / 1000 | 447,1 | 425,8 | **−4,8 %** |
| saltos > 16 / 1000 | 351,6 | 321,6 | **−8,5 %** |
| saltos > 32 / 1000 | 231,9 | 206,9 | **−10,8 %** |
| \|laplaciano\| médio | 46,32 | 35,00 | **−24,4 %** |

O `|laplaciano|` médio é a métrica que mede *rugosidade* e não *diferença*: uma
rampa lisa dá ~0, uma escada não. Cai um quarto nas duas resoluções.

### Sobre "melhorar mais a 1024": o que a medida diz de facto

O briefing esperava que o ganho **relativo** fosse maior a 1024. **Não é** —
é −26,8 % a 512 contra −24,4 % a 1024, e não vou torcer a métrica até dar o
contrário. A razão é que a serrilha **absoluta** a 1024 já era menor (46,3 contra
61,7): há menos para tirar.

A afirmação de escala que **se prova** é outra, e é melhor:

> **depois@512 (45,17) ≈ antes@1024 (46,32).**
> O conserto entrega a 512 px a lisura que antes só se conseguia a 1024 —
> a um quarto dos pixels.

E o §3 prova o resto: mais pixels **não** consertavam (0,2477 → 0,2583 de 128 a
1024), e a semente conserta em **todas** as resoluções por igual (×4,79 / ×4,67 /
×4,80 / ×4,71). O defeito era estrutural e o conserto é estrutural.

---

## §6 O anel da sombra: INTOCADO, e o briefing tinha um engano

O briefing avisava que `generateFieldFromAlpha` alimenta a cadeia de vidro **e**
`shadowRingMask`. **Não alimenta.** `shadowRingMask`
(`Source/RenderBox/GlassShadow.cpp:164`) tem a **própria cópia** da transformada:
semeia `sq[t] = art[t*4+3] >= 0.5f ? inf : 0`, chama `edtSquared1d` duas vezes
por conta própria e faz o seu `dist − 0.5` em `:211`. Nunca chama
`generateFieldFromAlpha`.

Verificado, e não deduzido:

- `git diff` não toca `edtSquared1d` nem `rasteriseContours`;
- `GlassShadow.cpp` **não está no diff**;
- `shadowImage` recebe `artRgba` (`IconRenderer.cpp:1340`) — a arte rasterizada,
  nunca o campo;
- os casos de `test_glass_shadow.cpp`, que fixam a máscara numericamente, estão
  verdes.

**O anel é bit-a-bit idêntico. Não há regressão.**

`[OBS]` **E é uma dívida, não uma vitória.** O anel continua com a semente
binária e com o mesmo meio texel de quantização que este laudo acabou de tirar do
vidro. O mesmo remédio aplica-se lá — e **não foi aplicado**, porque o terreno
desta frente era `DistanceField.*`. Fica nomeado para não virar folclore.

---

## §7 O tempo, em Release

`cmake --preset release -DIC_BUILD_UI=OFF`, 5 execuções, relógio em volta de
`renderIcon`.

| | antes (mín / mediana) | depois (mín / mediana) | |
|---|---|---|---|
| 512 px | 0,112 / 0,122 s | 0,122 / 0,125 s | +2 % na mediana |
| 1024 px | 0,391 / 0,403 s | 0,455 / 0,456 s | **+13 %** |

**Continua `O(pixels)`.** De 512 para 1024 são 4× os pixels: antes o tempo fez
×3,5 (0,112 → 0,391), depois faz ×3,7 (0,122 → 0,455). O expoente não mudou —
o que se pagou foi **constante**, não uma ordem. As peças novas são todas por
pixel: a cobertura (16 varreduras de scanline, exatas em x), a marcação da banda
com o Sobel, e o mínimo 3×3. **Nenhuma transformada euclidiana a mais** — que é
o ponto, já que o supersample de 3×3 pagaria a transformada **nove** vezes.

`Tests/test_time_budget.cpp` está verde.

---

## §8 A suíte

`cmake --preset mingw -DIC_BUILD_UI=OFF`, alvo `ic_tests`:

```
638 case(s), 0 failure(s)
```

---

## §9 O que fica em aberto

1. `[OBS]` **O pior caso fora da banda** (§3): 0,751 texel a 1024 contra 0,497 da
   binária. O refino 3×3 só alcança um texel; um refino 5×5 fechado por `|d|`
   provavelmente o resolve, e não foi medido. E o refino está **fechado** por o
   sítio escolhido pela transformada ser ele próprio um texel de banda: um texel
   cujo vizinho é banda mas cujo sítio não é passa ao lado dele. Abri-lo é de
   duas linhas e não foi medido — os números deste laudo são todos com o portão
   fechado.
2. `[OBS]` **O anel da sombra** (§6) continua com a semente binária.
3. `[OBS]` **Arte uniformemente translúcida**: a regra da banda protege o
   *interior* de um platô a alpha 0,5, mas na silhueta desse platô a cobertura
   vem escalada pela opacidade da camada e o deslocamento sai enviesado. O viés é
   limitado por meio texel — o mesmo que a binária já tinha — mas é um viés e
   não uma quantização.
4. `[OBS]` **A grade do alvo** continua não lida, e agora sabe-se *porquê*: está
   no CoreUI (§1), não nas fatias deste projeto. Os três botões de `SDFGeneration`
   podem sair da lista de coisas por ler — não há o que ler neste binário.
