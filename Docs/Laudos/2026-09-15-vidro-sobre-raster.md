# Laudo — o vidro sobre arte raster

*2026-09-15. Frente: a lacuna de uma coisa só que tirava TRÊS efeitos —
refração, translucidez e especular — de toda camada de vidro com arte `.png`.
Entradas: `Docs/Laudos/2026-09-15-sombra-anel.md` (que nomeou o seletor e
registrou como `[OBS]` o que não leu), `Docs/Laudos/2026-09-15-especular.md` e
`Docs/Laudos/2026-09-15-translucencia.md`.*

Endereços do slice `References/2.0-125/out/slices/IconRendering.arm64`
(VA == offset de arquivo).

---

## 0. O resultado, em uma linha

`[BIN]` **O alvo tira o campo de distância do ALFA RASTERIZADO da camada, não do
contorno vetorial dela.** O gerador é um método de `CUINamedLayerImage` — uma
imagem — e ele **aborta** se a imagem for `nil`.

Logo raster e vetor **não são dois casos**. São um só, e a distinção que este
renderizador desenhava aqui era artefato do **nosso** gerador, não do formato.

A lacuna fechou. Os três efeitos ligaram.

---

## 1. A cadeia, endereço por endereço

O laudo do anel parou em: *"gerado por `sdfTextureWithBufferAllocator:` num
`TXRTexture`, que não está no `IconRendering.arm64` nem no `RenderBox.arm64`"*.
Isso está certo sobre **a implementação**. A pergunta desta frente era outra —
sobre o que ele opera — e essa se responde no lado do chamador, que **está** no
dump.

`[BIN]` O seletor tem **um** stub (`0x0008F320`) e **uma** chamada:

```
0x000867F8  bl  #0x8f320   ; _objc_msgSend$sdfTextureWithBufferAllocator:
```

dentro de `0x0008658C`. O receptor é `x20`, e `x20` é o **segundo argumento** da
função — carregado pelo único chamador, `0x0002933C`:

```
0x00029330  ldr   x0,  [x19, #0x50]     ; o contexto de render
0x00029334  ldr   x20, [x19, #0x88]     ; O RECEPTOR
0x00029338  mov   x21, #0
0x0002933C  bl    #0x8658c
```

`[x19+0x88]` é escrito uma vez, em `0x00028AF4`, com o resultado de um cast
condicional:

```
0x00028AA4  adrp  x1, #0xce000
0x00028AA8  add   x1, x1, #0xbe0
0x00028AAC  adrp  x2, #0xcc000
0x00028AB0  add   x2, x2, #0x928        ; <-- a classe
0x00028AD0  bl    #0x8df38              ; o cast condicional
0x00028AD4  cbz   w0, #0x28f30
0x00028AD8  ldr   x28, [x19, #0xc0]     ; o objeto castado
0x00028ADC  mov   x0, x28
0x00028AE0  bl    #0x8eb60              ; _objc_msgSend$image
0x00028AEC  cbz   x0, #0x291bc          ; SE A IMAGEM E NIL, ABORTA
0x00028AF0  str   x0,  [x19, #8]
0x00028AF4  str   x28, [x19, #0x88]     ; e so agora ele e o receptor
0x00028AFC  bl    #0x8eaa0              ; _objc_msgSend$hasLightingEffects
```

### 1.1. `0xCC928` é `CUINamedLayerImage`

`[BIN]` `IconRendering.arm64` não tem símbolos Swift, mas tem
`LC_DYLD_CHAINED_FIXUPS`. Decodificando os slots de `__objc_classrefs` (seção em
`0xCC890`, 376 bytes) contra a tabela de imports do fixup (675 entradas, formato
1):

| slot | bind |
|---|---|
| `0xCC918` | `_OBJC_CLASS_$_CUICatalog` |
| `0xCC920` | `_OBJC_CLASS_$_CUINamedIconLayerGroup` |
| **`0xCC928`** | **`_OBJC_CLASS_$_CUINamedLayerImage`** |
| `0xCC930` | `_OBJC_CLASS_$_CUIMutableNamedIconLayerStack` |
| `0xCC938` | `_OBJC_CLASS_$_CUIMutableNamedLayerImage` |
| `0xCC940` | `_OBJC_CLASS_$_CUIMutableNamedIconLayerGroup` |
| `0xCC958` | `_OBJC_CLASS_$_TXRTexture` |

Um `CUINamedLayerImage` do CoreUI carrega um **bitmap**. Não tem caminho, não
tem contorno, não tem regra de preenchimento. E `0x00028AEC` diz que sem a
imagem **não há campo nenhum**.

### 1.2. O caminho até ele começa num GRUPO, e o rápido é o de uma camada só

`[BIN]` `0x000289E8` casta o primeiro argumento para `CUINamedIconLayerGroup`
(`0xCC920`), pede `layers` (`0x00028A24`), e se a contagem for exatamente `1`
(`0x00028A54 cmp x20, #1`) casta esse único elemento para `CUINamedLayerImage` e
segue direto. Os dois outros ramos — `0x00028C64` (não é um grupo) e `0x00028DC8`
(mais de uma camada) — passam por uma continuação assíncrona antes de voltar ao
mesmo ponto. Nenhum deles chega ao gerador com outra coisa que não uma imagem.

### 1.3. A reflexão diz o mesmo, por outro lado

`[BIN]` `References/2.0-125/out/fieldmd_iconrendering.txt`:

```
0xa3104  [struct] IconRendering.SDF.SourceLayer  (2 fields)
      displayList   <indirect-external>
      isOpaque      Sb
```

A fonte de um SDF é um **display list** e um bit de opacidade. Um display list é
um **desenho**, não uma forma. Se o campo viesse de geometria, o campo aqui se
chamaria `path`, `shape` ou `contours`.

### 1.4. E o consumidor fecha o círculo

`[BIN]` `0x00025354` monta a árvore do CoreUI que vai ser desenhada. Para cada
camada ele faz, em sequência:

```
0x00025510  bl  #0x8f8c0   ; _objc_msgSend$setImage:
0x0002551C  bl  #0x8fc60   ; _objc_msgSend$setSdfTexture:
0x00025520  cmp x26, #0
0x00025524  cset w2, ne
0x0002552C  bl  #0x8f7e0   ; _objc_msgSend$setHasLightingEffects:
```

`hasLightingEffects` é **literalmente `sdfTexture != nil`**. É o portão: sem
campo de distância, a camada não recebe iluminação — que é exatamente o sintoma
que `specularDoesNotDrawNote()` descrevia. O portão existe; o que não existia era
o motivo de ele fechar para raster.

---

## 2. `[OBS]` O que continua atrás do `TXRTexture`

O laudo do anel estava certo em não afirmar mais do que leu, e esta frente **não
abre** a caixa. O que continua não lido:

1. **A grade.** Em que resolução o alvo rasteriza antes de transformar. A
   `IconRendering.SDF` carrega um `maxDistance` (`0xA2FF4`) e `0x00084304`
   devolve as dimensões em texels, mas quem as escolhe está no `TXRTexture`.
2. **A transformada.** Se é euclidiana exata, jump flooding, 8SSEDT ou outra
   coisa. O `maxDistance` se cancela na conta do anel (§4 daquele laudo), então a
   escala não se vê daqui.
3. **`[OBS]` Os três botões que apontariam para a resposta**, nomeados e não
   lidos — `0xA46E0`:

   ```
   0xa46e0  [struct] IconRendering.ICRRenderingParameters.SDFGeneration  (4 fields)
         clampThreshold               Sd
         useAdvancedStacking          Sb
         precisePixelFormatThreshold  Si
         maxRelativeSmoothing         Sd
   ```

   Vale reparar no que esses nomes **são**: um limiar de corte, um limiar de
   formato de pixel e um alisamento relativo. São botões do domínio **raster**.
   Um gerador que lesse geometria não teria o que fazer com um
   `precisePixelFormatThreshold`. Isso é `[INF]`, e é corroboração, não prova —
   a prova é §1.

O que esta frente afirma com selo `[BIN]` é **a entrada**, não a grade nem a
transformada. Onde o código escolhe a resolução, ele diz que escolheu.

---

## 3. Por que isto NÃO é o caso "`[INF]` defensável mesmo sem a leitura"

O briefing desta frente previa dois desfechos: ou o alfa se confirma e a lacuna
fecha, ou o `TXRTexture` é parede e a derivação do alfa vira uma proposta com
selo `[INF]` e sem desenho.

É o primeiro. A parede existe, mas fica **depois** da pergunta que importava: a
entrada do gerador é legível no chamador, e foi lida. A decisão de desenhar não
se apoia em inferência nenhuma sobre o que há dentro do `TXRTexture` — apoia-se
em `0x00028AB0`, `0x00028AE0` e `0x00028AEC`, que dizem que a coisa em que o
gerador opera é uma imagem e que sem imagem ele não opera.

---

## 4. O que entrou no código

### 4.1. `Source/RenderBox/DistanceField.h` / `.cpp` — PARTE TRÊS

`generateFieldFromAlpha(rgba, w, h, options)`, na **mesma** convenção que
`generateField`: `(d, gx, gy, coverage)`, `d` negativo dentro, gradiente unitário
apontando para onde `d` cresce, cobertura `-d/aaWidth + 0.5` grampeada. Quem
consome — `glassDisplacementMap`, `glassOpacityMask`, `drawSpecular` — não sabe
qual dos dois geradores fez o campo, e não precisa saber.

- O contorno é `alpha >= 0.5`, e a distância é medida de **centros de pixel** ao
  contorno: `d = ±(centroOpostoMaisProximo − 0.5)`. O meio pixel é o que põe a
  fronteira **entre** dois centros de classe oposta em vez de **sobre** um.
- **Duas** transformadas euclidianas exatas e não uma: os texels de dentro
  precisam da distância aos centros de fora, e os de fora, aos de dentro.
- O gradiente é **exato**, não diferenciado: a transformada de
  Felzenszwalb–Huttenlocher com `argmin` devolve o **seed mais próximo**, e
  `unit(p − seed)` é a derivada analítica. Foi por isso que o `edtSquared1d`
  ganhou o parâmetro `arg`.
- `[BIN]` **Quatro linhas virtuais de fora, um passo além de cada borda** — a
  borda vazia de um texel do SDF do alvo, a mesma que `0x00011CF8` subtrai
  (`sdfTexelsW − 2`) e `0x00010F48` devolve, e a mesma a que `shadowRingMask` já
  se grampeia. Arte que sai do canvas tem profundidade finita.
- **Alfa que nunca chega a 0.5 devolve campo VAZIO.** Não há contorno para
  assinar, e um campo todo-fora deixaria os três efeitos desenharem nada em
  silêncio. Vazio faz o chamador nomear a lacuna.

### 4.2. Uma cópia só da transformada

`edt1d` vivia no namespace anônimo do `GlassShadow.cpp`. Virou `edtSquared1d` no
`DistanceField.h`, e o `shadowRingMask` passou a chamá-la. **Uma** transformada
exata no repositório, dois consumidores. Era essa a instrução ("use o
`DistanceField.cpp` em vez de escrever um segundo") e ela valia nos dois
sentidos.

### 4.3. `Source/RenderBox/IconRenderer.cpp`

- O `.png` é **decodificado e posicionado antes** do bloco de vidro, não onde é
  desenhado — porque o campo sai do alfa dele. `[BIN]` É a ordem do alvo também:
  `image` primeiro (`0x00028AE0`), gerador depois (`0x000867F8`). É decodificado
  **uma vez** e reusado no desenho.
- O ramo raster passou a rodar máscara, sombra, realce e composite **na mesma
  ordem** do ramo vetorial.
- `rasterPlacementRect` — o retângulo que `placeRaster` ocupa, ao lado de
  `placeRaster` e derivado da aritmética dele, para a `bounds` da translucidez.
  Um vetor dá esse retângulo pelo `viewBox`; um raster, pelos próprios pixels.
- `kTranslucencyRasterNote` (que dizia *"um raster não tem contorno para achatar,
  então a camada desenha OPACA"*) **saiu**. No lugar entrou
  `kGlassRasterFieldNote`, que diz de onde o campo veio, com os endereços, e o
  que ainda não foi lido debaixo dele.
- `specularDoesNotDrawNote()` **parou de culpar o raster**. A frase agora nomeia
  os três jeitos que ainda restam de não haver campo: regras de preenchimento
  misturadas, nenhum contorno pintado, ou alfa que não chega ao limiar.

### 4.4. `Tests/test_rb_field.cpp` — PARTE TRÊS

Cinco casos: o meio pixel (`d = −0.5` um passo dentro da borda, `+0.5` um passo
fora, `−3.5` no centro de um quadrado 8×8 — **o mesmo número** que o gerador de
contorno dá para o mesmo retângulo, que é onde as duas convenções se encontram);
o gradiente unitário e contínuo através da fronteira; o erro contra a fórmula
fechada do retângulo; o campo vazio; e a borda virtual.

**Medido, e impresso a cada execução:** campo do alfa contra o retângulo exato,
**max |erro| = 0,207 px**. Um campo de bitmap não pode vencer a própria grade —
o contorno que ele conhece é uma escada — e 0,207 fica bem abaixo da diagonal de
um texel.

---

## 5. `[ART]` A recontagem, feita de novo e por outro caminho

A contagem `45/171` veio de outra frente e o briefing pediu confirmação
independente. Confirmada, **com a ressalva de qual regra ela usa**.

O `glass` de uma camada pode vir como booleano (`glass`) ou como lista
(`glass-specializations`, 55 camadas). As duas leituras dão números diferentes, e
nenhuma é obviamente a certa:

| regra | camadas de vidro | `.png` | `.svg` | outro | documentos com `.png` |
|---|---|---|---|---|---|
| `glass` verdadeiro em **qualquer** aparência | **171** | **45** | 125 | 1 (`.heic`) | **31** |
| só a entrada **base** da lista conta | 146 | 39 | 107 | 0 | 29 |

Total do corpus: **145 documentos, 271 grupos, 437 camadas**. Glass nunca é
herdado — `glass` aparece **zero** vezes no nível do documento e **zero** no do
grupo — e a árvore tem exatamente dois níveis, então não há recursão a errar.

Então `45/171` **está certo**, sob a leitura de qualquer-aparência. A outra
leitura dá `39/146`. Uma varredura própria desta frente, contando só
`image-name` resolvido na base, achou **44** camadas `.png` de vidro sob a regra
de qualquer-aparência contra as 45 da outra frente — uma camada de diferença, nos
três layers de `rileytestut__Delta__MicrochipIcon` que oferecem `.png` **e**
`.svg` por idioma e cuja base depende de qual chave se resolve primeiro. Os
**31 documentos** as duas varreduras acham igual.

### 5.1. E quantas dessas camadas o repositório consegue de fato desenhar

`[ART]` **Doze.** O corpus é em boa parte uma colheita **só de manifesto**: das
44 camadas `.png` de vidro, **32 não têm o arquivo de arte em disco** (o bundle
do `sparkle-project__Sparkle__AppIcon`, por exemplo, é um `icon.json` sozinho).
Das 12 restantes, sob a leitura de qualquer-aparência:

| efeito que passou a rodar | camadas |
|---|---|
| translucidez | **10** |
| especular | **5** |
| refração | **1** |

espalhadas por exatamente **9 documentos**.

---

## 6. O que mudou no pixel

Renderizando os **31** documentos com camada `.png` de vidro em `--size 1024`,
com o `icrender` de `bb6b10d` e o desta frente, e contando texels RGBA
diferentes:

| documento | texels mudados | % de 1024² | delta máx. por canal |
|---|---|---|---|
| `Avi0n__MeshCoreOne__AppIcon` | **677.864** | 64,65 % | 218 |
| `CamilleScholtz__swmpc__swmpc` | **607.484** | 57,93 % | 255 |
| `rileytestut__Delta__MicrochipIcon` | **453.829** | 43,28 % | 230 |
| `OpenSource03__harnss__icon` | **387.192** | 36,93 % | 77 |
| `CodeEditApp__CodeEdit__CodeEditPreIcon` | **120.487** | 11,49 % | 32 |
| `CodeEditApp__CodeEdit__CodeEditDevIcon` | **119.615** | 11,41 % | 32 |
| `CodeEditApp__CodeEdit__CodeEditBetaIcon` | **110.186** | 10,51 % | 32 |
| `CodeEditApp__CodeEdit__CodeEditAlphaIcon` | **82.726** | 7,89 % | 32 |
| `LeoNatan__LNPopupController__LNPopupController` | **32.334** | 3,08 % | 164 |
| os outros 22 | 0 | 0 % | 0 |

**Total: 2.591.717 texels, em 9 documentos.**

E os nove são **exatamente** os nove da §5.1 — os documentos onde uma camada
`.png` de vidro tem um efeito vivo **e** o arquivo de arte existe. A
correspondência é um a um, e é ela que torna o zero dos outros 22 uma medida e
não um alívio: eles não mudaram porque não havia o que mudar, e a varredura do
manifesto diz por quê antes de a imagem dizer.

O `CamilleScholtz__swmpc__swmpc` merece nota: é o único documento do corpus com
uma refração `.png` viva, e a única camada que ele tem **era pulada**. O
`icrender` dizia `0 of 1 layer(s) drawn` e nomeava a razão — *"um raster não tem
contorno para achatar e este projeto não tem gerador de campo a partir do
alfa"*. Agora diz `1 of 1`. Não é um efeito a mais numa camada que já desenhava:
é o documento inteiro saindo do vazio.

Suíte: **623 casos, 0 falhas**, com `IC_CORPUS_DIR` apontado ao corpus.

---

## 7. O que continua aberto

1. `[OBS]` **A grade e a transformada do alvo** (§2). O campo aqui sai na
   resolução do alvo e por uma euclidiana exata; as duas escolhas estão ditas em
   `kGlassRasterFieldNote` e em `DistanceField.h`.
2. `[OBS]` **Os três botões de `SDFGeneration`** (`0xA46E0`) continuam nomeados e
   não lidos. O `clampThreshold` é o candidato natural a ser o limiar que aqui é
   `0.5`, e nada o mediu.
3. `[OBS]` **O limiar `alpha >= 0.5`** é o mesmo que o `shadowRingMask` já usava,
   e ele **não** veio do binário nem lá nem aqui. As duas frentes herdaram a
   mesma convenção da mesma falta.
4. `[ART]` **O `.heic`** continua sem leitor. É uma camada em
   `alienator88__Viz__Viz` e ela segue caindo no ramo "extensão que este leitor
   não lê".
5. `[OBS]` **A regra do `glass` especializado** (§5): 171 ou 146 camadas conforme
   a leitura, e nada resolve qual o alvo aplica.
