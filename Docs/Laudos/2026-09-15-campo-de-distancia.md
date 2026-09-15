# Laudo — o campo de distância

*2026-09-15. Frente: o `generateField` por força bruta era 73 % do render do
Apollo e 99 % do render do ícone do usuário. Entradas:
`Docs/Laudos/2026-09-15-vidro-sobre-raster.md` (que leu de onde o ALVO tira o
campo), `Docs/Laudos/2026-09-15-desfoque-escada.md` (que tirou a sombra do
caminho crítico) e `Docs/Laudos/2026-09-15-sombra-anel.md`.*

Medidas em **Release** (`cmake --preset release -DIC_BUILD_UI=OFF`), máquina
quieta, `--idiom square`. Base: `e676064`.

---

## 0. O resultado, em uma linha

O caminho vetorial passou a **rasterizar o contorno na própria grade do campo** e
a rodar a **mesma** transformada euclidiana exata que a arte raster já rodava. O
render de 1024 px do ícone do usuário cai de **57,2 s para 0,46 s** (124×), e o
do Apollo de **9,84 s para 2,73 s**.

E não é só mais barato: some do campo a **dobra** que a força bruta punha em todo
subcaminho ENTERRADO debaixo de outro. Esse é o único pedaço da diferença de
pixel que **não** converge com a grade, e está medido abaixo.

---

## 1. A medida de antes, confirmada

| ícone | tamanho | antes (`e676064`) | depois | fator |
|---|---|---|---|---|
| `GoWToolkit.icon` (usuário, 1 camada) | 1024 | **57,17 s** | **0,51 / 0,43 / 0,44 s** | **124×** |
| `GoWToolkit.icon` | 512 | **14,58 s** | **0,22 / 0,22 / 0,22 s** | **66×** |
| `Apollo-Reborn__Apollo-Reborn__AppIcon` (10 camadas) | 1024 | **9,84 s** | **2,67 / 2,63 / 2,76 s** | **3,6×** |
| `Apollo…AppIcon` | 512 | **2,70 s** | **0,85 / 0,86 / 0,86 s** | **3,1×** |

Três execuções cada no "depois"; a dispersão é de 0,03 s a 0,13 s. Um ganho de
124× não precisa de dispersão, mas ela está aí.

A meta era **< 1 s a 1024 px no ícone do usuário**. É 0,46 s.

### 1.1. `[ART]` Por que o ícone do usuário era o pior caso

`GOW.svg`, achatado com `subdivisions = 16`, tem **16 788 segmentos** — contra 64
a 724 nas oito artes do Apollo. A força bruta é `pixels × segmentos`: 1 048 576 ×
16 788 = **1,76 × 10^10** testes ponto-segmento para UMA camada. É por isso que
uma camada custava mais que dez.

---

## 2. A CORREÇÃO DE ALVO QUE CHEGOU NO MEIO, e por que ela estava errada

Enquanto esta frente media, veio uma correção dizendo que **no ícone do usuário o
gargalo era a sombra, e não o campo**, com esta tabela a 512 px:

| variante | tempo relatado |
|---|---|
| completo | 15,00 s |
| sem sombra (`"shadow":{"kind":"none"}`) | **0,16 s** |
| sem vidro (`"glass": false`) | 0,18 s |

**Reproduzi os três números e eles estão certos. A leitura deles não estava.**

`[OBS]` `{"kind":"none"}` sozinho **não é um documento sem sombra: é um documento
que este renderizador RECUSA.** O parser do material do grupo exige
`{kind, opacity}`, e o `icrender` imprime exatamente isso e sai com a contagem
zerada:

```
  grupo 0 / GOW: vidro: a chave 'shadow' do grupo nao le -- not {kind, opacity}
  ...z.png: 512 x 512, 0 of 1 layer(s) drawn
```

**`0 of 1 layer(s) drawn`.** Os 0,16 s são o tempo de não desenhar nada. É a
mesma armadilha que o brief chama de "verde vazio" — um filtro que não casa nada
imprime `0 case(s)` e sai 0 — só que do lado do render, e o binário já a
anunciava.

Com um documento **sem sombra E VÁLIDO** (`{"kind":"none","opacity":0.5}`), com a
força bruta ainda no lugar:

| variante (512 px, força bruta) | tempo | desenhou |
|---|---|---|
| completo | **14,32 s** | 1 de 1 |
| `{"kind":"none"}` — recusado | 0,12 s | **0 de 1** |
| `{"kind":"none","opacity":0.5}` — válido | **14,82 s** | 1 de 1 |
| `"glass": false` | **0,17 s** | 1 de 1 |

Desligar a sombra **não muda nada** (14,32 → 14,82 s, dentro da dispersão).
Desligar o vidro inteiro leva a 0,17 s. Confirmado que o vidro estava ligado nas
duas variantes pelas notas que cada render imprime (`kGlassVectorFieldNote`,
`especular desenhado`, `sombra: o anel`) — a variante "sem sombra" imprime as
duas primeiras e não a terceira.

Logo o campo era **~14,6 s dos 14,8 s**, e a resposta à pergunta "(a) ou (b)?" da
correção é **(b), mas sem portão escondido**: o campo não estava sendo construído
naquela variante porque **a camada inteira não estava sendo desenhada**, e o
renderizador dizia isso em voz alta.

O censo do Apollo da frente irmã continua de pé e não conflita: lá são dez
camadas de contorno esparso, aqui é uma de contorno densíssimo.

---

## 3. A decisão: rasterizar, e não acelerar

O brief deu duas saídas — trocar o caminho por rasterização, ou provar com número
que o campo vetorial exato se justifica e então acelerá-lo. Segue o número.

### 3.1. `[BIN]` O argumento de fidelidade, que já estava lido

`Docs/Laudos/2026-09-15-vidro-sobre-raster.md` §1: o gerador do alvo é
`sdfTextureWithBufferAllocator:` (`0x000867F8`), enviado ao objeto castado para o
classref `0x000CC928` = `_OBJC_CLASS_$_CUINamedLayerImage`, que é pedido por
`image` em `0x00028AE0` e **aborta** se for nil (`0x00028AEC cbz x0, #0x291BC`); e
`IconRendering.SDF.SourceLayer` (`0xA3104`) é `{displayList, isOpaque}` — um
desenho, não uma forma. **O alvo rasteriza e depois transforma, nos dois casos.**

Nada disto é novo nesta frente; o que é novo é que o caminho vetorial passou a
obedecer.

### 3.2. Os dois campos na MESMA forma, com o erro

Oráculo ad hoc (`generateField` contra `generateFieldFromContours`, 96×96,
non-zero), `ss` = amostras por pixel por eixo:

| forma | \|Δd\| máx, ss=1 | ss=3 | ss=9 |
|---|---|---|---|
| retângulo (um contorno) | 0,531 px | 0,250 | 0,059 |
| círculo r=30, 256-gon | 0,489 px | 0,164 | 0,055 |
| dois retângulos **DISJUNTOS** | 0,207 px | 0,069 | 0,023 |
| anel (externo + interno invertido) | 0,207 px | 0,069 | 0,023 |
| dois retângulos **SOBREPOSTOS** | **25,907 px** | **25,770** | **25,724** |

As quatro primeiras convergem como `1/ss`, que é o que uma grade tem de fazer. A
quinta **não converge**, e é o achado.

O corte na linha `y = 48` diz por quê (`d`, negativo dentro):

```
   x :  bruta    raster
   38 :  -1.500  -17.500
   40 :  -0.500  -17.513     <- a aresta ENTERRADA de um dos retângulos
   48 :  -8.500  -19.511
   58 :  -1.500  -25.535
   60 :  -0.500  -25.500     <- a outra
```

A força bruta mede até o **segmento mais próximo**, esteja ele enterrado dentro
da união ou não. Onde um subcaminho pintado passa por baixo de outro, o campo
mergulha para `−0,5` **no meio da forma** e o especular desenha um aro ali. A
ressalva já estava escrita no `DistanceField.h` desde sempre ("the interior edge
shows up as a crease in the field. Apple's smooth union does not have that
crease"); esta frente a mediu e a removeu. Uma forma rasterizada **não tem
aresta enterrada para medir**, e `[BIN]` o alvo, que rasteriza, também não pode
ter.

### 3.3. `[ART]` Os dois campos na arte real do Apollo

512 px, `ss=1`, placement identidade, `subdivisions=16`:

| camada | segmentos | \|Δd\| máx | \|Δd\| médio | px de dobra | bruta | raster |
|---|---|---|---|---|---|---|
| `Antenna.svg` | 460 | 0,781 px | 0,273 | 0 | 364 ms | 14 ms |
| `Apollo Helmet Space.svg` | 64 | 0,499 | 0,342 | 0 | 62 ms | 14 ms |
| `apollo_antennaball.svg` | 64 | 0,487 | 0,337 | 0 | 55 ms | 13 ms |
| `apollo_ring_large.svg` | 128 | 0,500 | 0,338 | 0 | 109 ms | 14 ms |
| `Eyes 3.svg` | 344 | 0,702 | 0,261 | 0 | 266 ms | 13 ms |
| `Face.svg` | 256 | 0,500 | 0,343 | 0 | 198 ms | 13 ms |
| `Helmet 2.svg` | 724 | 0,500 | 0,317 | 0 | 604 ms | 15 ms |
| **`stem.svg`** | 131 | **3,223** | 0,175 | **36** | 106 ms | 14 ms |

Sete das oito concordam com o `argmin` exato dentro da grade. A oitava é o
**stem**, e é a única com dobra. O tempo da bruta acompanha os segmentos; o da
raster é **plano** — é o que `O(pixels)` quer dizer.

### 3.4. E a dobra é EXATAMENTE onde a figura mais muda

O pior bloco 8×8 do render inteiro do Apollo a 512 px está em **(256,144)**, com
**121,9 níveis** de diferença na média do bloco. É a base da antena, onde o
`stem` entra na elipse. Recorte lado a lado: o render antigo tem um aro claro
**dentro** da elipse que o novo não tem — o aro que o especular desenhava sobre a
aresta enterrada do stem.

### 3.5. A escada de `ss`, e por que o número é UM

Apollo 512 px, contra a força bruta, **só pixels visíveis** (alpha > 0 em algum
dos dois quadros):

| ss | mudados | \|Δ\| médio | p90 | p99 | máx | bloco 8×8 médio | bloco 8×8 máx | render 1024 |
|---|---|---|---|---|---|---|---|---|
| 1 | 14,21 % | 4,73 | 3 | 130 | 255 | 2,78 | **121,6** | **2,49 s** |
| 3 | 12,97 % | 3,22 | 2 | 98 | 255 | 1,73 | **121,9** | 6,49 s |
| 9 | 11,66 % | 2,25 | 1 | 60 | 255 | 1,37 | **121,9** | ~20 s |
| 15 | 10,94 % | 1,92 | 1 | 45 | 255 | 1,24 | **121,9** | ~52 s |

O piso de ~10,9 % e o bloco de 121,9 **não se movem**. Refinar a grade não os
alcança porque não são a grade — são a dobra. Que os campos rasterizados
convergem entre si está medido também: `ss=9` contra `ss=15` dá bloco 8×8 máximo
de **10,33**, contra os 121,9 de qualquer um deles contra a bruta.

Então `ss=3` compra 1,5 nível de delta médio por **2,6× o render** (Apollo 1024:
2,49 → 6,49 s) e não compra o piso a preço nenhum. **`ss = 1`**, que é também a
grade em que `generateFieldFromAlpha` já roda para arte raster — uma convenção na
torre, e não duas.

---

## 4. O que mudou no pixel

Só pixels visíveis. Os invisíveis são lixo de `acc/a` e não entram em nenhuma
conta aqui.

| ícone | tamanho | visíveis | mudados | \|Δ\| médio | p50 | p90 | p99 | máx | bloco 8×8 médio | bloco 8×8 máx |
|---|---|---|---|---|---|---|---|---|---|---|
| usuário | 1024 | 987 176 | 45 223 (**4,58 %**) | 1,19 | 0 | **0** | 34 | **255** | 0,54 | 50,3 |
| usuário | 512 | 247 188 | 13 945 (5,64 %) | 1,76 | 0 | 0 | 54 | 255 | 0,74 | 31,9 |
| Apollo | 1024 | 987 549 | 124 723 (12,63 %) | 3,84 | 0 | 1 | 120 | 255 | 2,37 | 191,6 |
| Apollo | 512 | 247 167 | 35 121 (14,21 %) | 4,73 | 0 | 3 | 130 | 255 | 2,78 | 121,6 |

### 4.1. O delta máximo visível é 255 e o brief manda parar e explicar. Aqui está.

**Passa de 8/255, e passa muito. Três coisas, em ordem de tamanho.**

1. **A dobra que sumiu (§3.2–3.4).** Não é regressão: é a correção de um erro que
   o `DistanceField.h` já admitia e que o alvo `[BIN]` não pode cometer. É o
   único componente que **não** encolhe com a grade, e é ele que põe o bloco de
   191,6 no Apollo 1024.

2. **A quantização da grade, 0,17 a 0,34 px de média (§3.3).** O especular é
   uma banda de ~1 px de largura em `d` (`glassHighlight_v1`: `sat(d/w + 0.5) *
   sat((height − d)/w + 0.5)` com `w ≈ 0,83`), então 0,3 px de erro em `d` move a
   banda por um terço da largura dela e o pixel do aro muda muito. O aro é fino e
   **muito longo** — o Apollo é feito de anéis —, o que é por que a CONTAGEM de
   pixels mudados é grande e a MEDIANA é zero.

3. **A sensibilidade do render a qualquer campo.** Medida, e é o número que
   desarma a régua de 8/255 para este ícone: **dois campos rasterizados que
   diferem entre si por no máximo ~0,05 px (`ss=9` contra `ss=15`) já mudam
   10,14 % dos pixels visíveis, com máximo 255.** Nenhuma mudança de campo neste
   ícone cabe em 8/255, inclusive uma que ninguém chamaria de mudança.

   Por isso as colunas de **bloco 8×8** existem nesta tabela: elas separam "o aro
   pontilhou" de "o aro mudou de lugar". No ícone do usuário o bloco 8×8 médio é
   **0,54 de 255** e o p90 por pixel é **0**.

`[OBS]` O que esta frente **não** afirma: que o campo novo é mais próximo do
alvo *no pixel*. Ela afirma que a ENTRADA do gerador é a mesma que a do alvo
(`[BIN]`, §3.1) e que o campo perdeu uma dobra que o alvo não tem. A GRADE do
alvo e os três botões de `ICRRenderingParameters.SDFGeneration` (`0xA46E0`)
continuam não lidos, como o `kGlassVectorFieldNote` diz em cada render.

---

## 5. O que entrou no código

### 5.1. `Source/RenderBox/DistanceField.h` / `.cpp`

- **`fieldFromInsideMask`**, o miolo que já existia dentro de
  `generateFieldFromAlpha`, extraído. Com `ss = 1` faz aritmética por aritmética
  o que fazia antes, **inclusive a ordem dos quatro testes de borda**, que decide
  empates. `generateFieldFromAlpha` passou a chamá-lo e não mudou de resultado.
- **`rasteriseContours`** — uma varredura de scanlines. A regra de cruzamento é a
  do `FieldShape::distanceAt`, virada do avesso: de "por pixel, ande em todo
  segmento" para "por segmento, emita as linhas que ele cruza". Um segmento conta
  para as linhas cujo centro cai em `[min(ay,by), max(ay,by))` — o mesmo
  meio-aberto que o par `ay <= py && by > py` da bruta descreve, que é o que
  impede um vértice exatamente sobre uma linha de centro de contar duas vezes.
  Dentro da linha, um cruzamento exatamente no centro do pixel **conta**, porque
  o teste da bruta é o `px < xIntersect` estrito sobre os cruzamentos à DIREITA e
  a varredura acumula os da ESQUERDA (a soma das direções de um contorno fechado
  é zero, então "non-zero" diz a mesma coisa dos dois lados).
- **`generateFieldFromContours(contours, w, h, options, superSample)`**. O
  `superSample` tem de ser ÍMPAR: só um fator ímpar tem um sub-texel cujo centro
  É o centro do pixel; um par leria o campo meio sub-texel fora e poria a figura
  fora de passo com o `CoveragePass`. Pares caem para o ímpar de baixo.
- `generateField` (força bruta) **fica**. É o oráculo dos testes e é o que
  `test_rb_field.cpp` mede.

### 5.2. `Source/RenderBox/IconRenderer.cpp` / `.h`

- O ramo vetorial do bloco de vidro chama `generateFieldFromContours` com
  `kFieldSuperSample = 1`, cuja escada de medida está escrita ao lado da
  constante. Toque mínimo: nada mais no arquivo foi remexido.
- Um contorno que fecha mas não cobre ponto de amostra nenhum devolve campo
  **vazio** e vira lacuna nomeada, pela mesma regra do alfa que não chega a 0,5.
- **`kGlassVectorFieldNote`**, irmã da `kGlassRasterFieldNote`: diz que o campo
  veio de uma rasterização, com que erro medido, o que se ganha (a dobra), e que
  a grade do alvo continua não lida.

### 5.3. `Tests/test_glass_layer.cpp`

Dois casos afirmavam a lista de notas por tamanho (`notes.empty()` e
`notes.size() == 1`). A nota nova entrou, e eles passaram a afirmar o que de fato
lhes interessa: que nada **além** do campo é declarado a 1024, e que só uma nota
carrega `[INF]` a 128. Nenhum outro teste mudou.

**Suíte: 624 casos, 0 falhas**, com `IC_CORPUS_DIR` apontado ao corpus.

---

## 6. O que sobra caro, com número

Bench direto das funções, 1024 px, uma camada:

| estágio | `GOW.svg` (16 788 seg.) | `Helmet 2.svg` (724 seg.) |
|---|---|---|
| **`shadowImage`** | **77,7 ms** | **75,2 ms** |
| `generateFieldFromContours` | 67,6 ms | 54,1 ms |
| `drawSpecular` (5 realces) | 36,3 ms | 17,7 ms |
| `glassOpacityMask` | 4,5 ms | 4,2 ms |

E dentro do campo, a 1024², `ss=1`:

| parte | tempo |
|---|---|
| rasterizar os contornos | **1,2 ms** |
| **UMA** `edt2d` | **22,7 ms** |
| o campo inteiro (duas `edt2d` + montagem) | **58,0 ms** |

1. **`shadowImage` é agora o estágio de vidro mais caro**, ~76 ms por camada por
   render de 1024 px. No Apollo isso é **~0,75 s dos 2,73 s**. A escada do
   desfoque já tirou o desfoque de lá; o que sobra é a máscara do anel e a
   translação, que a frente irmã cronometrou em 333 ms e 302 ms para dez camadas.
2. **A `edt2d` é 100 % do custo do campo** (1,2 ms de rasterização contra 45 ms
   de duas transformadas). O passo de COLUNA percorre a memória com passo `w`,
   uma falha de cache por elemento; uma transposta em blocos é a próxima
   economia óbvia e ninguém a mediu.
3. **Decomposição do ícone do usuário a 1024 px, depois** (todas com as notas
   conferidas para garantir que a variante desligou o que diz desligar):
   completo **0,46 s**; sem sombra **0,37 s**; sem vidro nenhum **0,25 s**. Ou
   seja: sombra ~0,10 s, campo+especular+translucidez ~0,11 s, e **0,25 s que não
   é vidro nenhum** — rasterização do SVG, fundo, chiclet e composite. Esse 0,25 s
   é o novo piso daquele ícone e ninguém o abriu.
4. **Apollo a 1024 px continua em 2,73 s** e o campo é só ~0,58 s disso (10 ×
   58 ms). O resto é sombra (~0,75 s) e ~1,2 s de rasterização de dez SVGs e
   composite, que esta frente não tocou.

---

## 7. O que continua aberto

1. `[OBS]` **A GRADE do alvo.** `TXRTexture` não está em nenhum dos dois slices, e
   `clampThreshold`, `precisePixelFormatThreshold` e `maxRelativeSmoothing`
   (`0xA46E0`) continuam nomeados e não lidos. Uma amostra por pixel é escolha
   deste projeto, medida e não lida.
2. `[OBS]` **A distância sub-texel sem pagar `ss²`.** Semear a transformada com a
   COBERTURA anti-serrilhada de cada texel de borda em vez de com uma máscara
   binária (a EDT anti-serrilhada) tira os 0,3 px de erro por `O(pixels)`, sem os
   nove passes do `ss=3`. Não foi feito e não foi medido.
3. `[OBS]` **A transposta em bloco na `edt2d`** (§6.2). O passo de coluna é
   estriado e é metade do custo do campo.
4. `[OBS]` **O limiar `alpha >= 0.5`** herdado do `shadowRingMask` continua sem
   origem no binário, e agora ele governa também a arte vetorial pela via da
   rasterização — a ressalva vale para as duas.
5. `[ART]` **A dobra que sumiu mudou o desenho de documentos do corpus** e nenhuma
   varredura contou em quantos. Só o Apollo e o ícone do usuário foram medidos no
   pixel.
