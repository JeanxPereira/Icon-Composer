# O orçamento de tempo — e o censo de custo da cadeia de vidro

15/09/2026. Tudo aqui é **medido**, nesta máquina, com o binário desta árvore.
Nada vem de leitura de fatia: os selos `[BIN]`/`[ART]` não aparecem porque este
laudo não lê o alvo — ele mede o **nosso** renderizador. O que há é `[INF]`
(medição minha) e `[OBS]` (o que a medição **não** fecha).

---

> **DATADO — 19/09/2026.** Os números deste laudo foram medidos numa árvore
> que ainda percorria **todo segmento por pixel** para construir o campo. Esse
> caminho foi trocado por `generateFieldFromContours` (rasteriza e roda duas
> EDT exatas, O(pixels + segmentos)) depois desta medição, e em 19/09 as
> varreduras de CPU passaram a rodar em várias threads. Medido de novo em
> 19/09, Release, mínimo de três execuções, o MESMO documento a 1024 px:
> **2,42 s** antes das threads e **1,68 s** depois, com o campo em **39 %** e
> não 73 %.
>
> O laudo **não** está errado: ele descreve fielmente a árvore que mediu, e a
> frase dele ("o gargalo não é a sombra, é o campo") continua verdadeira — foi
> ela que orientou as duas trocas. O que não vale mais são as **grandezas
> absolutas**. Quem for otimizar mede de novo antes: uma task foi despachada
> em 19/09 contra os "73 %" deste texto e encontrou 39 %, e a premissa dela
> estava vencida sem que nada na página dissesse isso.

## 0. A frase

**O gargalo do vidro não é a sombra. É o campo de distância.**

A decomposição "desliga um efeito por vez" que originou esta frente mede errado
por construção: **a refração, a translucidez e o especular comem o MESMO campo**
(`IconRenderer.cpp`, o portão `wantsRefraction || wantsTranslucency ||
wantsHighlight`). Desligar um deixa o campo de pé, então cada um deles parece
custar zero — e o custo inteiro dos três aparece grudado em qualquer coisa que
sobre.

Medido no documento mais pesado do corpus (10 camadas de vidro), 1024 px,
Release, mínimo de três execuções intercaladas:

| parte | 1024 px | fatia |
|---|---|---|
| tudo que não é vidro (parse, rasterização, composição, fundo) | 0,76 s | 7 % |
| **o campo de distância (duas EDT exatas por camada)** | **7,50 s** | **73 %** |
| a sombra de vidro | 2,92 s | 28 % |
| a máscara de translucidez, dado o campo | 0,16 s | 2 % |
| os cinco realces especulares, dado o campo | ~0 s | ~0 % |
| `blur-material` + `refractivity` | ~0 s | ~0 % |
| **medido, render completo** | **10,32 s** | |

(A soma das partes dá 11,34 s contra 10,32 s medidos: **resíduo de −10 %**, que é
ruído de máquina, não uma parte esquecida. §3 mostra a dispersão que o produz.)

**A resposta direta à pergunta da frente** — *o que sobra caro depois que a
frente irmã consertar o desfoque da sombra?* — é: **7,8 s dos 10,3 s a 1024 px**,
e nove décimos disso é o campo. Zerar a sombra inteira deixa este documento em
**dez vezes** o custo de não ter vidro.

---

## 1. O que foi medido, e como

- **Release**, sempre: `cmake --preset release -DIC_BUILD_UI=OFF`,
  `cmake --build --preset release --target icrender ic_tests`.
- Documento: `References/corpus/Apollo-Reborn__Apollo-Reborn__AppIcon` — **o mais
  pesado do corpus**, 10 camadas de vidro em 4 grupos, com `shadow`, `specular`,
  `translucency` e `blur-material` todos presentes e ligados.
- Cada variante é o **mesmo documento com uma chave reescrita**, gravado num
  diretório temporário. **Nenhuma linha do renderizador foi tocada para medir** —
  o censo mede o código que é enviado, não uma versão instrumentada dele.
- Relógio: o processo `icrender` inteiro, pelo `Stopwatch` do PowerShell, e
  depois conferido contra a nova linha `... drawn in X.XXX s` do próprio
  `icrender` (§6), que cronometra só `renderIcon`.

As chaves usadas para desligar cada parte, todas no **grupo**, e sempre apagando
também a lista `<chave>-specializations` — porque `resolve()` prefere a lista, e
uma variante que só escrevesse a chave nua não desligaria nada em 3 dos 4 grupos
deste documento:

| parte | como se desliga |
|---|---|
| sombra | `"shadow": {"kind":"none","opacity":0}` |
| especular | `"specular": false` |
| translucidez | `"translucency": {"enabled":false,"value":0}` |
| desfoque do material | `"blur-material": null` |
| refração | `"refractivity": {"enabled":false,"strength":0,"depth":0}` |
| vidro inteiro | `"glass": false` em cada **camada** |

### 1.1. Por que a família de variantes tem esta forma

Porque **não dá** para isolar campo, translucidez e especular desligando um por
vez. A família abaixo permite separá-los por inclusão-exclusão:

```
A   (bare)  todo consumidor do campo desligado  -> o campo NUNCA é construído
T           campo + máscara de translucidez
S           campo + cinco realces especulares
TS          campo + máscara + realces
TSh         TS + sombra
full        o documento como está escrito
```

de onde:

```
campo        = T + S − TS − A
translucidez = TS − S
especular    = TS − T
sombra       = TSh − TS
resto        = full − TSh
```

O gerador das variantes foi um script descartável de 70 linhas: ele copia o
`icon.json`, reescreve as chaves da tabela acima em cada grupo, apaga as listas
`-specializations` correspondentes e copia `Assets/` ao lado. Não entrou na
árvore de propósito — o que importa dele são as chaves e a álgebra, que estão
escritas aqui, e não um arquivo que ninguém vai reexecutar e que ficaria
apodrecendo em `scripts/`.

---

## 2. O censo, cru

`[INF]` Mínimo de três execuções intercaladas (intercaladas para que uma deriva
térmica ou um build de vizinho não caia toda numa variante). Segundos.

| variante | 512 px | 1024 px |
|---|---|---|
| `no-glass` (`glass:false` em todas as camadas) | 0,34 | 0,76 |
| `A` vidro ligado, todo efeito desligado | 0,34 | 0,76 |
| `T` campo + translucidez | 2,07 | 8,42 |
| `S` campo + especular | 2,06 | 7,63 |
| `TS` campo + translucidez + especular | 2,02 | 7,79 |
| `TSh` = TS + sombra | 2,36 | 10,71 |
| `full` (+ `blur-material`, `refractivity`) | 2,34 | 10,32 |

Decomposto:

| parte | 512 px | 1024 px | por camada de vidro, 1024 | escala 512→1024 |
|---|---|---|---|---|
| base sem vidro | 0,34 | 0,76 | — | 2,2× |
| **campo de distância** | **1,77** | **7,50** | **0,750** | **4,2×** |
| sombra | 0,34 | 2,92 | 0,292 | **8,6×** |
| translucidez (dado o campo) | ~0 | 0,16 | 0,016 | — |
| especular (dado o campo) | ~0 | ~0 | ~0 | — |
| `blur-material` + `refractivity` | ~0 | ~0 | ~0 | — |

### 2.1. As duas leituras que valem

**O campo escala com o número de pixels, a sombra escala pior.** 4× mais pixels
custam 4,2× de campo e **8,6×** de sombra. Isso é a assinatura de um desfoque
cujo σ cresce com o tamanho: o campo é O(pixels), a sombra é O(pixels × σ) ≈
O(pixels^1,5). É exatamente a curva que a frente irmã está atacando, e é por isso
que o problema do usuário aparece a 1024 e quase não aparece a 512.

**O especular e a translucidez são de graça — DADO o campo.** Os cinco realces
amostram um campo que já existe; a máscara multiplica por um campo que já existe.
`[INF]` Cobrar deles o campo, como a decomposição "um por vez" faz, atribui 100 %
do custo a quem gasta ~2 %.

### 2.2. Onde a minha medição discorda da que abriu a frente

A tabela que abriu a frente diz, a 512 px, no ícone do usuário:

| variante | 512 px |
|---|---|
| sem especular | 14,8 s |
| sem translucidez | 14,4 s |
| **sem sombra** | **0,2 s** |

`[INF]` **Confirmo a forma e não o número.** No documento mais pesado do corpus,
as variantes equivalentes (`T` = sem especular, `S` = sem translucidez, `TS` =
sem sombra; as três também sem `blur-material` e sem `refractivity`, que custam
~0) dão **2,07 / 2,06 / 2,02** contra **2,34** do completo. Isto é: tirar o
especular ou a translucidez **não muda nada** — a diferença cabe no ruído — e
tirar a sombra tira 0,34 s. A forma "desligar um dos três não devolve nada" é
exatamente a mesma que a tabela de abertura mostra; o "0,2 s" não é.

`[INF]` A explicação é o portão do campo, e ela **fecha**: se o documento do
usuário tem `specular` desligado, `translucency` desligada ou zero, e refração
identidade, então `wantsRefraction || wantsTranslucency || wantsHighlight` é
falso e **o campo nunca é construído**. Aí a sombra — que roda por fora desse
portão, e roda até sobre raster — é literalmente todo o custo, e tirá-la leva o
render a 0,2 s. É o caso extremo da mesma máquina: 98 % de sombra em vez de 15 %.

`[OBS]` **O que eu não reproduzi: 15 s a 512 px.** Nenhum documento do corpus
chega perto — o mais pesado é 2,34 s. Não tenho o `.icon` do usuário (nem o
`Docs/_confrontar/examples/ImHex.icon`, que não tem `Assets/`), então não posso
dizer por que a sombra dele custa ~14,6 s enquanto as dez sombras do Apollo
custam 0,34 s juntas. A hipótese que **não** testei é arte muito maior em pontos,
que espalha o alvo do desfoque; registrar isso é melhor que inventar um fator.

---

## 3. A dispersão — e por que ela decide o formato do teto

Esta é a medição que a regra de honestidade pede, e ela foi **acidental**, o que
a torna melhor: eu rodei o mesmo censo duas vezes, uma enquanto um build vizinho
terminava e outra com a máquina quieta.

`[INF]` Mesmo render, `full`, 1024 px:

| quando | s |
|---|---|
| primeira passada, build de outra worktree terminando | **19,26** |
| passada limpa, repetição 1 | 10,59 |
| passada limpa, repetição 2 | 10,51 |
| passada limpa, repetição 3 | 10,85 |

**Em regime, a dispersão é 1,6 %. Com a máquina ocupada, o mesmo render custa
1,8× mais.** E o pior caso que vi no censo inteiro foi a variante `S` a 1024 px:
7,63 s no mínimo, 15,97 s no máximo — **109 % de espalhamento** em três
execuções.

`[INF]` O mesmo padrão aparece dentro da suíte, de novo com quatro execuções da
fixture do corpus em Release: 2,211 / 2,218 / 2,231 / 2,244 s em regime (1,5 %)
contra **2,773 s** numa execução logo depois de um build — **25 %**.

Isso decide tudo sobre o teto:

- Um teto apertado (2×, 3×) **falha sozinho** numa máquina compartilhada. Ele
  seria desligado ou reexecutado até ficar verde dentro de uma semana, e aí não
  mede mais nada — só custa.
- Um teto de **catástrofe** sobrevive: precisa absorver 2× de máquina e ainda
  pegar 90× de regressão. Qualquer fator entre ~5 e ~20 serve. Escolhi **8**.

`[INF]` **Não concluo que o teto é a ferramenta errada.** A variação é grande em
percentual mas é pequena em ordem de grandeza — 1,8×, contra os 90× que o teto
tem de pegar. Há quase **duas ordens de grandeza** de margem entre o ruído e o
defeito, e é exatamente essa folga que torna o instrumento viável. Se a máquina
variasse 10× eu estaria escrevendo o `[OBS]` contrário.

---

## 4. O teto — `Tests/test_time_budget.cpp`

### 4.1. A regra

```
teto = (segundos medidos em Release) × kSlack × kBuildFactor
kSlack        = 8
kBuildFactor  = 1.0   com NDEBUG  (Release)
              = 12.0  sem NDEBUG  (Debug)
```

### 4.2. Por que o teto é declarado **por configuração**, e não medido só em Release

Porque as duas coisas são verdade ao mesmo tempo: **a medição que vale é a de
Release** (é a configuração em que o render do usuário roda), e **a suíte roda em
Debug** (o preset `mingw` fixa `CMAKE_BUILD_TYPE=Debug`, e é dali que sai o
`ic_tests.exe` que todo mundo executa e que o `gate-m1.ps1` reexecuta por
mutação). Um número único mentiria num dos dois lados: alto demais para Release
(nunca morde) ou baixo demais para Debug (falha sozinho).

Então os tetos estão escritos em **segundos de Release** e são multiplicados por
`kBuildFactor` quando `NDEBUG` está ausente. O discriminador é `NDEBUG` e não uma
variável de ambiente **de propósito**: o CMake põe `-DNDEBUG` em `Release` e não
põe em `Debug`, então a constante segue o binário que a contém e não a memória de
quem executa.

### 4.3. O fator Debug, medido — e ele **não** é 5,6×

`[INF]` As mesmas três fixtures, os dois builds, o mesmo corpus, **em execução da
suíte inteira** (não filtrada — §4.5 explica por que isso importa):

| fixture | Release | Debug | razão |
|---|---|---|---|
| sem vidro, 64 camadas, 512 px | 0,263 s | 0,602 s | **2,3×** |
| cadeia completa, 4 camadas, 512 px | 0,229 s | 1,914 s | **8,4×** |
| corpus, 10 camadas de vidro, 512 px | 2,218 s | 16,181 s | **7,3×** |

**A razão não é um número.** Ela vai de 2,3× a 8,4× aqui, e uma execução filtrada
do caso do vidro, mais cedo, deu **11,2×**. O Debug taxa os laços internos — a
EDT exata do campo de distância — muito mais do que taxa o parse e a composição
em volta deles, então a fixture com mais campo por segundo é a de pior razão. O
5,6× com que a frente foi aberta é um ponto dessa faixa, não a faixa.

`kBuildFactor = 12` é o pior valor visto, arredondado para cima, de modo que o
teto de Debug nunca fique, em termos reais, **mais apertado** que o de Release de
que ele deriva. O preço é que o caso barato ganha um teto Debug bem folgado —
22 s contra uma medição de 0,60 s — e essa é a troca certa: aquele caso existe
para dizer *"não foi a cadeia de vidro que quebrou"*, e uma regressão de 90× nele
ainda cai em 54 s, muito acima.

### 4.4. Os três renders com teto

| caso | o que é | medido, Release | teto Release | teto Debug |
|---|---|---|---|---|
| `time_budget_no_glass_512` | 64 camadas, `glass:false`, 512 px | 0,263 s | 1,84 s | 22,1 s |
| `time_budget_full_glass_chain_512` | 4 camadas de vidro com a cadeia inteira (sombra `neutral 0.6`, `specular true`, `translucency 0.2`, `blur-material 0.24`), 512 px | 0,229 s | 2,48 s | 29,8 s |
| `time_budget_corpus_heaviest_512` | o documento do corpus com **mais camadas de vidro**, escolhido medindo o corpus e não nomeando um arquivo — hoje `Apollo-Reborn__Apollo-Reborn__AppIcon`, 10 camadas — 512 px | 2,218 s | 22,4 s | 269 s |

Os tetos vêm da **pior** de seis execuções da suíte inteira em Release (0,230 /
0,310 / 2,800 s) × 8, e não da melhor. A margem efetiva no verde de hoje é de
**7× a 11×**.

Três decisões que valem a pena dizer em voz alta:

1. **512 px, não 1024.** É o tamanho em que o app realmente renderiza
   (`Session::view.size = 512`), então o número com teto é o número que o usuário
   espera. Botar 1024 na suíte custaria 10 s por execução em Release e mais de um
   minuto em Debug para pegar o mesmo defeito.
2. **Nenhum caso tem uma camada só.** Uma camada mede 0,010 s / 0,062 s, e um
   teto construído sobre dez milissegundos não é teto, é detector de jitter do
   escalonador. 64 e 4 camadas põem as duas bases perto de 0,23 s, onde 8× quer
   dizer alguma coisa — e onde o controle negativo ainda estoura com folga.
3. **O caso do corpus escolhe o documento medindo**, e imprime o nome quando
   falha. Um teto que diz "estourou" sem dizer em quê obriga quem lê a abrir o
   arquivo de teste para descobrir.

### 4.5. O erro de calibração que eu cometi, e que vale registrar

A primeira calibração saiu de execuções **filtradas** (`ic_tests time_budget_`).
`[INF]` O mesmo caso mediu **0,062 s sozinho e 0,118 s dentro da suíte inteira**,
com o device quente e 626 casos atrás dele — quase o dobro. Um teto calibrado
sozinho e executado junto nasce com metade da folga que o autor acha que deu.

Os números de §4.4 vêm todos de execuções da suíte **inteira**, que é a condição
em que o teto realmente roda.

### 4.6. O que isto custa à suíte, dito em voz alta

`[INF]` Os três casos somam **18,7 s** numa execução Debug (0,60 + 1,91 + 16,18),
e a suíte inteira, com eles, mede **81,4 s / 653 casos / 0 falhas**. Em Release
são 2,7 s de 23,3 s / 627 casos / 0 falhas. Dezesseis daqueles dezenove segundos
de Debug são o caso do corpus.

Vale? Vale, e a mitigação é estrutural: o arquivo entra na lista `isSlow()` de
`Tests/main.cpp` — **não** porque leia o corpus, mas porque é **caro de
propósito**. A varredura de mutação (`gate-m1.ps1`) sai na primeira falha; com o
orçamento rodando por último, esses 21 s são pagos só pela execução limpa e pelas
mutações que sobreviveram a todo o resto. Eu **não** baixei o caso do corpus para
256 px, que o deixaria quatro vezes mais barato, por um motivo: 512 px é o
tamanho em que o app renderiza, e um teto sobre um tamanho que ninguém usa mede
um trabalho que ninguém espera.

### 4.7. A prova de que ele imprime o que mede

O teto imprime **sempre**, passando ou falhando. Suíte inteira, Release:

```
  [budget] no glass, 64 layers, 512 px                                  0.263 s  of   1.840 s
  [budget] full glass chain, 4 layers, 512 px                           0.229 s  of   2.480 s
  [budget] corpus Apollo-Reborn__Apollo-Reborn__AppIcon, 10 glass layer(s), 512 px   2.218 s  of  22.400 s

627 case(s), 0 failure(s)
```

Um orçamento que ninguém consegue ver é um orçamento que ninguém recalibra.

---

## 5. O controle negativo — a prova de que ele reprova

`IC_TIME_BUDGET_CONTROL=1` renderiza **todos** os casos com teto a **4× o lado**
(2048 px em vez de 512, dezesseis vezes os pixels) contra o **mesmo teto de
512 px**. É um render de verdade descendo o pipeline de verdade — não um número
falso entregue à comparação.

`[INF]` Executado, Release, `IC_TIME_BUDGET_CONTROL=1 ic_tests time_budget_`:

```
  [budget] no glass, 64 layers, 512 px                                  2.576 s  of   1.840 s
  FAIL time budget: no glass, 64 layers, 512 px took 2.576 s, ceiling 1.840 s ...
  [budget] full glass chain, 4 layers, 512 px                          11.018 s  of   2.480 s
  FAIL time budget: full glass chain, 4 layers, 512 px took 11.018 s, ceiling 2.480 s ...
  [budget] corpus Apollo-Reborn__Apollo-Reborn__AppIcon, 10 glass layer(s), 512 px  48.950 s  of  22.400 s
  FAIL time budget: corpus ... took 48.950 s, ceiling 22.400 s ...

3 case(s), 3 failure(s)
```

Três de três vermelhos, código de saída 1, e **nenhum outro caso da suíte se
mexe**.

Repare no caso do vidro: 0,229 s → **11,018 s**, um fator de **48** para 16× de
pixels. Não é linear porque a sombra não é linear (§2.1) — o controle, sem
querer, remede a curva que o censo mediu. E repare no caso sem vidro: 0,263 s →
2,576 s, fator **9,8**, quase exatamente proporcional aos pixels, que é o que um
rasterizador sem vidro deve fazer. **O controle separa as duas curvas sozinho.**

`[OBS]` **Por que o botão é o tamanho e não o σ.** O σ da sombra mora em
`Source/RenderBox/BlurKernel.*` e `GlassShadow.*`, que uma frente irmã estava
reescrevendo na mesma tarde; entrar ali seria conflito garantido. O que o
controle precisa provar é que **um render que demora demais de verdade deixa esta
suíte vermelha**, e uma tela dezesseis vezes maior prova exatamente isso. O que
ele **não** prova é que uma regressão especificamente de σ seja pega — mas como a
asserção é sobre tempo de parede e nada mais, qualquer render lento a dispara.

---

## 6. A linha de custo onde o usuário vê

### 6.1. `icrender`

`Source/cli/render_main.cpp` imprimia:

```
out.png: 512 x 512, 10 of 10 layer(s) drawn
```

Agora imprime:

```
ok.png: 512 x 512, 7 of 10 layer(s) drawn in 2.291 s
```

O relógio fica em volta de `renderIcon`/`renderSvg` e **nada mais** — não o
parse, não o encode do PNG — porque é o número que o orçamento de `test_time_budget`
cobra, e os dois têm de querer dizer a mesma coisa. O caminho do SVG solto ganhou
a mesma linha.

### 6.2. O painel de Diagnósticos

É onde o buraco doeu: a primeira linha do painel era

```
render   10 of 10 layer(s) drawn
```

e, enquanto o render estava em voo, o painel dizia **nada** e a tela ganhava um
ponto amarelo. Um ponto não distingue quarenta milissegundos de um minuto e
meio — que é exatamente a frase de que o usuário precisava e não teve.

Agora são duas linhas:

```
render   10 of 10 layer(s) drawn in 2.18 s
render   a newer render has been running for 12.40 s      <- só enquanto pendente
```

O relógio fica no **coordenador** (`RenderCoordinator::tick`), do instante do
`request()` até o resultado que casa a chave inteira — **não** dentro do job de
render. Isso é deliberado: a fila faz parte do que a pessoa espera. Um
escalonador ocupado com um render de 87 s não começa o próximo por 87 s, e um
relógio que só cronometrasse o desenho reportaria um render rápido enquanto a
tela continuasse vazia.

`lastRenderSeconds` nasce **negativo** e não zero, porque "nenhum render
terminou ainda" não pode imprimir `0.00 s`.

Pinado por `Tests/test_kit_idiom.cpp :: kit_coordinator_times_the_render_it_is_waiting_for`,
que assere as duas metades separadamente — o contador andando **enquanto**
pendente (a metade que importa: um render que nunca termina não produz resultado
para cronometrar) e o sentinela deixando de ser negativo quando o resultado
chega.

---

## 7. O que este laudo NÃO fecha

- `[OBS]` **Os 15 s a 512 px do ícone do usuário.** §2.2. Confirmei a forma da
  decomposição, não a magnitude, e disse por quê.
- `[OBS]` **O teto não é um perfil.** Ele pega catástrofe (≥8×). Uma regressão de
  3× passa por ele em silêncio, e isso é uma escolha, não um descuido: §3 mostra
  que um teto de 3× falharia sozinho nesta máquina.
- `[OBS]` **O censo é de um documento.** Apollo é o mais pesado do corpus, mas
  um documento com arte de contorno muito mais denso pagaria mais campo por
  camada — a EDT exata cobra por segmento de contorno, e eu não varri o corpus
  inteiro medindo isso. O que está medido é que, **neste** documento, o campo é
  três quartos do custo.
- `[OBS]` **Nada aqui mede a GPU.** `icrender` e a suíte rodam o mesmo caminho, e
  se ele é CPU ou fila de device não foi separado. A conclusão "o campo domina"
  vale para o tempo de parede, que é o que o usuário sente.
