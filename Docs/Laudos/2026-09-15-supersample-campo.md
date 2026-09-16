# O `superSample` do gerador de campo: medido, e DESLIGADO

Fecha o `[OBS]` que o commit `799411c` deixou aberto ao corrigir o normal que o
especular lê.

> "o pior caso no meio da banda segue em ~10 graus e o teto ali não é o estimador
> e sim o resíduo de 0,06 px do próprio `d`, que `superSample=3` nos dois sítios
> de chamada levaria a ≤1 grau (max 7) ao custo de 9× as duas transformadas
> euclidianas por camada — **o tempo disso num render de 1024 não foi medido e
> por isso não foi ligado**."

**Decisão: não ligar, em nenhum dos dois sítios.** A promessa de qualidade
angular do `[OBS]` estava certa — e por baixo. O que ela não prometia, e que é o
que decide, é o pixel: `superSample = 3` **não aproxima o render do gabarito**.
Num dos sítios ele afasta.

Nada mudou no desenho. Os quatro PNG de controle saem com SHA-256 idêntico ao
de `799411c`.

---

## 1. O que foi medido, e com que aparato

Duas perguntas independentes, dois instrumentos.

**A qualidade** saiu do mesmo aparato de forma fechada que a frente anterior
usou, e a checagem de que é o mesmo é que os números de `ss = 1` reproduzem os
do commit dígito a dígito: círculo de r=180 num campo de 512, direção do campo
contra o normal radial analítico, mean/pior em graus por faixa de profundidade.

**O custo e o pixel** saíram de `icrender` em **Release** (`cmake --preset
release -DIC_BUILD_UI=OFF`), **uma execução por ponto**, nos dois documentos
reais: `References/27.0-129/out/AppIcon-27.icon` (6 camadas) e
`D:/CodingProjects/GoWToolkit/dist/macos/GoWToolkit.icon` (1 camada). O relógio
é o do próprio `icrender`, que envolve `renderIcon` e nada mais — o mesmo número
que `Tests/test_time_budget.cpp` fiscaliza.

O erro por canal contra `apple-512.png` é o alinhamento da casa: render em 412
posto em (50,50) num quadro de 512, sem reamostragem (`png-diff.py
--legacy-inset`, cujo `inset` para 512 dá exatamente 50).

### 1.1. `superSample = 2` é `superSample = 1`, bit a bit

Antes dos números: o gerador **arredonda um fator par para o ímpar abaixo**
(`DistanceField.cpp:964`, `if ((ss & 1) == 0) --ss;`), porque só um fator ímpar
tem um sub-texel cujo centro É o centro do pixel. Então `ss = 2` não é meio
caminho — é o baseline.

Medido: as quatro faixas de ângulo saem **idênticas** em `ss = 1` e `ss = 2`.
Isto está agora fixado por caso de teste, porque a primeira coisa que o próximo
leitor vai tentar é `2`, e o resultado pareceria "supersample não faz nada" em
vez de "2 quer dizer 1".

---

## 2. A qualidade angular: o `[OBS]` acertou, e por baixo

Círculo r=180 num campo de 512. Mean / pior em graus, e o resíduo médio de |d|
contra a forma fechada:

| faixa (px) | `ss=1` | `ss=3` | `ss=5` | resíduo \|d\| 1 → 3 → 5 |
|---|---|---|---|---|
| 0,5–1 | 0,96 / 4,13 | **0,54 / 2,02** | 0,33 / 1,08 | 0,069 → 0,027 → 0,015 |
| 2–4 | 2,51 / 15,74 | **0,66 / 4,83** | 0,36 / 3,97 | 0,086 → 0,022 → 0,013 |
| 4–8 | 2,77 / 19,86 | **0,64 / 5,76** | 0,36 / 3,14 | 0,074 → 0,020 → 0,012 |
| 8–16 | 2,38 / 13,11 | **0,62 / 5,15** | 0,32 / 2,78 | 0,061 → 0,019 → 0,012 |

A coluna `ss=1` é, dígito a dígito, a do commit `799411c` (1,0/4,1 · 2,5/15,7 ·
2,8/19,9 · 2,4/13,1) — é o mesmo instrumento.

O `[OBS]` prometia "≤1 grau (max 7)". **Entregue: ≤0,66 grau, max 5,76.** E a
causa que ele nomeou também se confirma: o resíduo do próprio `d` cai de ~0,06–0,09
px para ~0,02 px, e é ele que estava segurando o teto — não o estimador de Sobel.

Quatro vezes menos erro de ângulo. Tudo verdade, e tudo irrelevante — §4.

---

## 3. O custo, em Release, uma execução por ponto

Baseline `ss = 1`, e cada sítio ligado **isoladamente**, para poder decidir um
de cada vez:

| documento | lado | `ss=1` | só chiclet `ss=3` | só arte `ss=3` |
|---|---|---|---|---|
| AppIcon-27 (6 camadas) | 412 | **0,269 s** | 0,361 s (+34 %) | 0,653 s (**2,43×**) |
| AppIcon-27 | 1024 | **1,669 s** | 2,266 s (+36 %) | 4,401 s (**2,64×**) |
| GoWToolkit (1 camada) | 412 | **0,090 s** | 0,168 s | 0,158 s |
| GoWToolkit | 1024 | **0,478 s** | 1,056 s | 1,074 s |

O 412 do AppIcon-27 bate com os 0,272 s de `799411c`. **O 1024, que é o número
que o `[OBS]` pedia e que ninguém tinha, é 1,669 s.**

Duas leituras que a tabela dá de graça:

- **O chiclet é um custo FIXO por render**: +0,092 s e +0,078 s em 412, +0,597 s
  e +0,578 s em 1024 — praticamente o mesmo nos dois documentos, apesar de um ter
  6 camadas e o outro 1. É o que se espera de um campo gerado **uma vez por
  render** (`IconRenderer.cpp:625`, no estágio do fundo, não no laço de camadas).
- **A arte escala com a contagem de camadas de vidro vetorial**: o documento de 6
  camadas paga 2,4×–2,6×, o de 1 camada paga aproximadamente o mesmo que um campo
  fixo. O 2,6× reencontra, por outro caminho, o "2.6x the render" que a escada já
  registrada ao lado de `kFieldSuperSample` tinha medido contra a força bruta.

---

## 4. O pixel, que é quem decide — e ele diz não

Erro médio por canal contra `apple-512.png`, sobre os 189 993 pixels visíveis.
**Quatro casas decimais, porque em duas os três resultados são o mesmo número:**

| variante | R | G | B | A |
|---|---|---|---|---|
| `ss=1` (base) | **8,8416** | **10,1735** | **9,8461** | **4,9520** |
| `ss=3` só na arte | 8,8411 | 10,1738 | 9,8484 | 4,9520 |
| `ss=3` só no chiclet | 8,8468 | 10,1786 | 9,8513 | 4,9560 |

E o desenho **muda** — não é um no-op que a média esconde:

- `ss=1 → arte ss=3`: **8 488 de 169 744 px movidos (5,00 %)**, pior delta de canal 13.
- `ss=1 → chiclet ss=3`: **4 954 px movidos (2,92 %)**, pior delta de canal 29.

Então a leitura é limpa:

- **O chiclet piora os QUATRO canais.** +0,0052 R, +0,0051 G, +0,0052 B,
  +0,0040 A. Custa +36 % do render em 1024 para andar para trás. Recusa fácil.
- **A arte é empate.** −0,0005 em R (a favor), +0,0003 em G e +0,0023 em B
  (contra), 0,0000 em A. Custa 2,6× o render em 1024. Um empate que custa 2,6× é
  uma derrota.

### Por que quatro vezes menos erro de ângulo não compra um nível de cor

O resto contra a Apple é de **~10 níveis por canal**, e ele não vem daqui. Vem
do `blur-material` desligado, do grampo `plusLighter` desligado e do overdraw da
sombra — cada um anotado como `[OBS]` no seu próprio sítio, e cada um valendo
ordens de grandeza mais do que isto. Um resíduo de **0,06 px** no campo está três
ordens de grandeza abaixo desse piso, e o cone do especular é um cosseno largo
demais para revelar dois graus de normal.

Vale registrar o caso do chiclet em separado, porque ele é o mais instrutivo: ali
o campo mais exato afasta do alvo porque **a Apple não desenha aquele realce a
partir de um campo**. `[INF]` `0x0000D904` monta um **gradiente cônico** numa
camada recortada (`beginLayer` 0xE0F0, `clipLayerWithAlpha` 0xE134,
`setConicGradient` 0xE448, `drawShape` 0xE490). Aproximar-se da forma fechada do
nosso estimador é afastar-se do instrumento que o alvo usa.

**É a regra anti-overfitting funcionando na direção incômoda.** O gabarito
arbitrou entre duas leituras nossas e escolheu a menos exata. Não se ajusta o
número até o pixel fechar — e também não se paga 2,6× por um número mais bonito
que o pixel recusa.

---

## 5. O terceiro sítio: `generateFieldFromAlpha` **não** deve ganhar o parâmetro

`IconRenderer.cpp:1264` chama `generateFieldFromAlpha` para arte raster, e a
assinatura (`DistanceField.h:381`) **não** aceita `superSample` — ela passa `1`
fixo a `fieldFromInsideMask`. Isso está certo, e a assimetria é a geometria
falando, não um esquecimento.

`generateFieldFromContours` supersampleia porque **tem de onde**: o contorno é
analítico, e rasterizá-lo numa grade `ss` vezes mais fina produz informação que
não existia na grade grossa. Um **bitmap não tem esse de onde**. Subir `ss` ali
só poderia replicar cada texel em `ss × ss` cópias — a mesma silhueta em degrau,
agora em degraus menores, por `ss²` do custo — ou reamostrar o alfa, o que
inventa uma borda que o arquivo não contém.

E a informação sub-texel que o raster **realmente** tem já está sendo usada, por
outra porta: `coverage[t] = alpha[t]`, a mesma identidade que `CoveragePass`
escreve, com `options.subpixelSeed` fazendo as duas transformadas nascerem com o
deslocamento assinado `0.5 − coverage`. Para uma aresta **reta** num deslocamento
sub-texel qualquer isso é **exato** — exato de um jeito que nenhum `ss` finito
alcança.

Somar `superSample` ali seria simetria de assinatura, não ganho.

---

## 6. Controles

- **Suíte inteira, Release, `IC_BUILD_UI=OFF`: 653 casos, 0 falhas.** O baseline
  **na mesma configuração** é 652 — medido por A/B controlado, revertendo apenas
  o arquivo de teste e reconstruindo. O caso que esta frente acrescenta é um. A
  contagem **subiu** um; não caiu.
- **Sobre os 678 do enunciado**, que é exatamente a armadilha 4a: a árvore declara
  **678** `TEST_CASE`. Com `IC_BUILD_UI=OFF`, **26 não são compilados** — todos
  `kit_*`, `canvas_*` e `layers_*`, a suíte da UI que o flag remove junto com o
  OnyxSDK. 678 − 26 = 652. A lista dos 26 foi conferida nome a nome contra os 652
  que rodaram; nenhum deles toca o campo de distância. O 678 do enunciado é uma
  corrida com a UI ligada.
- **Orçamento de tempo** (`Tests/test_time_budget.cpp`), nesta corrida:
  `0,290 s` de `1,920 s`; `0,162 s` de `1,040 s`; `0,589 s` de `4,000 s`. Nenhum
  teto foi tocado, porque nada foi ligado. **Se a arte tivesse sido ligada**, o
  caso do corpus (10 camadas de vidro) pagaria os mesmos ~2,6× e iria a ~1,5 s
  contra um teto de 4,0 s: caberia — e teria cabido por sorte, não por mérito, o
  que é mais uma razão para a decisão não se apoiar no orçamento.
- **SHA-256, antes e depois, quatro renders.** Idênticos, como têm de ser quando
  nada é ligado:

  | | 412 | 1024 |
  |---|---|---|
  | AppIcon-27 | `978F76B332EFADE0…` | `F18284DC7F612FAC…` |
  | GoWToolkit | `68C119F10F9BD0A1…` | `9836D308A21D3CBD…` |

  Camadas desenhadas conferidas em todos: `6 of 6` e `1 of 1` (armadilha 4b).

---

## 7. O que fica `[OBS]`

- **A GRADE do alvo continua não lida.** `ICRRenderingParameters.SDFGeneration`
  (`0xA46E0`) tem três botões que ninguém seguiu, e um deles pode ser justamente
  este fator. Uma amostra por pixel segue sendo **escolha medida deste projeto**,
  não leitura do binário — e agora é uma escolha medida contra o gabarito, não só
  contra o tempo.
- **O campo do chiclet é recalculado a cada render de uma forma FIXA.** Não existe
  cache hoje, e o custo dele em `ss = 1` não foi isolado. Cachear por `size` é a
  economia óbvia e é **independente** desta decisão: valeria mesmo com `ss = 1`,
  e era o que tornaria `ss = 3` grátis ali — só que `ss = 3` ali piora o pixel,
  então o cache deixou de ser a pergunta interessante.
- **`ss = 5` foi medido e não investigado.** Ele corta o erro angular pela metade
  de novo (0,32–0,36 grau de média), e por 25× as transformadas. Como `ss = 3` já
  não move o gabarito, `ss = 5` não foi levado a render nenhum.

---

## 8. O que mudou no código

Nenhuma mudança de comportamento. Só medição registrada onde ela é lida:

- `Source/RenderBox/IconRenderer.cpp` — `kFieldSuperSample` continua `1`, agora
  com as três tabelas acima ao lado dele e o `[OBS]` do `799411c` marcado como
  fechado.
- `Source/RenderBox/ChicletHighlights.cpp` — a chamada continua em `ss = 1`, com
  a medição que recusou o `ss = 3` **neste sítio especificamente**, e o `[OBS]`
  do cache.
- `Source/RenderBox/DistanceField.h` — por que `generateFieldFromAlpha` não leva
  o parâmetro (§5).
- `Tests/test_rb_field.cpp` — um caso novo, a 128 px, que fixa as duas afirmações
  em que a decisão se apoia: que `ss = 3` de fato afia o normal, e que `ss = 2`
  é `ss = 1`. Um botão que ninguém gira é um botão que apodrece; nenhum render
  passa outra coisa que não `1`, então nada mais na suíte perceberia se uma
  reescrita da transformada parasse de honrar `superSample`.
