# A escada de qualidade do desfoque — como um sigma grande é PAGO

15/09/2026. Fatia: `References/2.0-125/out/slices/RenderBox.arm64`. VA == offset
de arquivo. Continuação direta de `Docs/Laudos/2026-09-15-desfoque.md`, que leu o
**kernel**; este lê a **conta** que o alvo faz para não executá-lo inteiro.

---

## 0. A frase

`[BIN]` **O alvo nunca faz 361 taps. Ele desenha menor.**

Desde que `σ = raio` foi corrigido (o laudo anterior, §5), a sombra pedia
`σ` até `32` a 512 px e até `64` a 1024 px. Com `blurKernelHalfWidth = ceil(2.8σ)`
isso é **181** e **361** taps por eixo, e o custo apareceu inteiro no relógio: um
ícone do corpus levava **37,4 s** para renderizar a 1024 px, dos quais **27,6 s**
eram a sombra.

A máquina que o alvo usa no lugar disso já estava meio lida no laudo anterior
(§1.2) e esta frente terminou de ler: **teto de sigma por passada, passadas que
somam variância, e uma redução de resolução que subtrai a variância que ela mesma
introduz.** Depois dela, o mesmo ícone leva **10,3 s** e a sombra leva **1,2 s**.

---

## 1. A escada, endereço a endereço

`[BIN]` Tudo em `RB::Filter::GaussianBlur::render` (`0xFEC34`) e em
`RB::Filter::(anon)::BlurRenderer::render` (`0xFF964`).

### 1.1. O teto de sigma — três degraus, e o default é o do meio

`0xFED34`-`0xFED50`. `ubfx w9, w8, #4, #2` lê dois bits de qualidade das flags e
um `fcsel` de três vias escolhe:

| `(flags>>4)&3` | σmax | taps do kernel cacheado |
|---|---|---|
| `1` | `3.5` | 15 |
| `3` | `7.0` | 31 |
| **qualquer outro** | **`5.25`** | 23 |

`0xFED54` eleva ao quadrado e `0xFED58` guarda `σmax²` em `renderer+0x18`. O
`else` da escada — o degrau **default** — é `5.25`, e é o que este repositório
transcreve.

### 1.2. A contagem de passadas, e o número que a escada olha é o NÃO clampado

`0xFED5C`-`0xFED74`:

```
nRaw = ceil( max(rx², ry²) / σmax²  −  0.001 )        ; fcvtps, float de 0x1619D0
```

`0xFED78`-`0xFED8C` guarda `clamp(nRaw, 1, 32)` em `renderer+0x1c`. **Mas a
decisão de resolução não olha o valor clampado: olha `w25`, que é o `nRaw` cru.**

### 1.3. `[BIN]` O LIMIAR QUE FALTAVA — 7 e 3

Este é o item que o laudo anterior deixou em aberto ("wider still and the
renderer downsamples first" — sem dizer *quando*). Está em `0xFEDF0` e `0xFEE24`,
e o que ele decide é o **tamanho do alvo de render**:

| condição | tamanho | endereço |
|---|---|---|
| `nRaw >= 7` | `(d + 3) >> 2` — redução **4x** | `0xFEDF0`, `0xFEE04`-`0xFEE08` |
| `nRaw >= 3` | `(d + 1) >> 1` — redução **2x** | `0xFEE24`, `0xFEE3C`-`0xFEE50` |
| senão | tamanho cheio | — |

(No degrau de qualidade 3, `w24 == 0x30`, os dois ramos reduzem **só em X** —
os `fcsel` de `0xFEE1C` e `0xFEE5C`. Não é o degrau default.)

### 1.4. `[BIN]` A redução SUBTRAI a variância que ela introduz — e a constante do degrau default não é a que o laudo anterior citou

`BlurRenderer::render` lê o mesmo `+0x1c`, toma o ramo correspondente e
**reescreve a variância em `+0x10` antes de qualquer kernel ser construído**:

| ramo | conta | endereço | é |
|---|---|---|---|
| 4x, qualidade default | `v/16 − 0.47265625` | `0xFFA18`-`0xFFA30` | `0.6875²` |
| **2x, qualidade default** | **`v/4 − 0.765625`** | `0xFFB78`-`0xFFB8C` | **`0.875²`** |
| 2x, qualidade 3 | `v/4 − 2.56` | `0xFFB08`-`0xFFB20` | `1.6²` |

**A correção:** o laudo anterior transcreveu `v/4 − 1.6²` como se fosse o 2x. Ele
é o 2x do **degrau de cima**: o teste que separa os dois é `ldrb w8, [x20,#9]` em
`0xFFA4C`, e `[x20+9]` é escrito em `0xFECF8`-`0xFED00` como
`(flags & 0x30) == 0x30`, isto é, qualidade **3**. O degrau default subtrai
`0.875²`.

`[BIN]` **E nenhuma das duas está no pool de constantes.** `−0.47265625` é
`mov w8, #-0x410e0000` (`0xFFA24`), que é `0xBEF20000`; `−0.765625` é
`mov w8, #-0x40bc0000` (`0xFFB80`), que é `0xBF440000`. A lição que custou quatro
vezes no dia 15/09 — varrer o `__text` atrás do imediato, não o pool — é o
motivo de estarem aqui.

Variâncias gaussianas **somam**: a imagem encolhida já carrega `0.6875` (resp.
`0.875`) sigmas de borrão na grade dela, e o kernel é pedido para o resto. É a
mesma contabilidade da soma de passadas, aplicada à reamostragem.

---

## 2. O que NÃO está lido, e a escolha que entrou no lugar

`[INF]` **Se a escada RECORRE.** `GaussianBlur::render` dimensiona o alvo do
multipass **uma vez**; `BlurRenderer::render` reduz por conta própria e reescreve
a variância; **quem recomputa `+0x1c` entre os dois não foi seguido** — é código
de agendamento de multipass de GPU, atrás de
`RenderGroup::add_multipass_renderer` (`0x105E3C`), e segui-lo inteiro não cabia
nesta frente.

A leitura que entrou é a única sob a qual **os dois ramos são alcançáveis e a
contagem de passadas fica limitada**: *reduzir, recomputar `nRaw` na grade menor,
e reduzir de novo enquanto ele for 3 ou mais.* É também a regra mais simples que
**preserva a variância total exatamente** — cada nível subtrai precisamente o que
adiciona. Sob ela, um canvas de 1024 px com `σ = 19.2` (o raio default da sombra,
`Tests/test_glass_shadow.cpp`) reduz **uma vez** (4x) e termina com **uma**
passada de `σ = 4.75` numa grade de 256×256; com `σ = 64` reduziria duas vezes e
terminaria com uma passada de `σ = 3.94` em 64×64.

`[INF]` **Os filtros de reamostragem.** A redução é uma **média de caixa** do
bloco `fator × fator` e a expansão é **bilinear**, com o texel reduzido `r`
entendido no centro do bloco (`r·fator + (fator−1)/2`) para que descida e subida
concordem sobre onde uma amostra *está*. Os filtros do alvo não foram lidos.
Os dois contribuem cerca de `0.25²` e `0.29²` de variância em vez dos `0.875²` /
`0.6875²` que estão sendo subtraídos, então o resultado fica **levemente mais
estreito** do que a transcrição pede: `0.09%` da variância total no degrau 4x,
`3.9%` no 2x. A medida disso está no §4.

`[INF]` **O piso de tamanho.** Uma redução é recusada se levasse qualquer lado
abaixo de **8 texels**. O alvo não tem piso lido; sem um, um thumbnail de 32 px
com raio largo seria borrado numa grade de 2×2, onde a expansão bilinear não tem
o que reconstruir.

`[OBS]` **A largura do kernel por passada.** Os kernels cacheados do alvo têm 15,
23 e 31 taps para σmax `3.5` / `5.25` / `7.0`, o que é `2.0σ`, `2.10σ` e `2.14σ`
— mais estreito que os `2.8σ` que `roi` (`0xFEB58`) e o caminho de CPU
(`0xC3634`) usam. Este arquivo trunca em `2.8σ` por passada, que é o número que
`BlurKernel.h` já possuía e guarda mais massa. Por que o caminho de GPU se
permite `2.1σ` não foi lido.

---

## 3. O tempo, em Release, medido

Build: `cmake --preset release -DIC_BUILD_UI=OFF`, alvo `icrender`.
Ícone: `References/corpus/Apollo-Reborn__Apollo-Reborn__AppIcon`, `--idiom square`.
Uma execução por medida.

| | antes | depois |
|---|---|---|
| **512 px** | **3,44 s** | **2,68 s** |
| **1024 px** | **37,37 s** | **10,30 s** |

`[OBS]` O `Docs/_confrontar/examples/ImHex.icon` — o único bundle do usuário
dentro do repositório — **não carrega esta medida**: os SVGs das 11 camadas não
estão no bundle (só `icon.json` está), então `icrender` reporta
"referencia pendurada" para todas e desenha **0 de 11**, em `0,2 s` a 1024 px
antes e depois. A medida é do Apollo.

### 3.1. Onde o tempo estava, e onde está

`[OBS]` Medido desligando **um** efeito por vez num clone do bundle, a 1024 px,
com o binário de antes:

| variante | tempo |
|---|---|
| sem vidro (`"glass": false` nas 10 camadas) | **0,98 s** |
| com vidro, **sem sombra** (`"shadow": {"kind":"none"}`) | **9,77 s** |
| com vidro, sem sombra, **sem especular** | **9,82 s** |
| completo | **37,37 s** |

Duas coisas saem daí de uma vez. A sombra custava `37,37 − 9,77 = **27,6 s**`.
E **o especular não custa nada**: `9,82` contra `9,77` é ruído, e os cinco
realces que amostram o campo de distância não são o problema que se temia.

---

## 4. O pixel — o que a escada mudou, medido

A escada muda o pixel **de propósito**: um gaussiano composto por passadas sobre
uma grade reduzida não é byte-idêntico a um gaussiano de 109 taps na grade cheia,
e nunca poderia ser. O que segue é a medida, `icrender` antes contra
`icrender` depois, no mesmo ícone.

**1024 px** — 43.014 pixels visíveis diferentes de 1.048.576 (**4,10 %**):

| | R | G | B | A |
|---|---|---|---|---|
| delta máximo entre pixels **visíveis** | 2 | 5 | **8** | 1 |

`p50 = 1`, `p90 = 1`, `p99 = 2`, `p100 = 8`.

**512 px** — 15.611 pixels visíveis diferentes de 262.144 (**5,96 %**):

| | R | G | B | A |
|---|---|---|---|---|
| delta máximo entre pixels **visíveis** | 1 | 2 | 2 | 0 |

`p50 = 1`, `p90 = 1`, `p99 = 2`, `p100 = 2`.

**Dentro do portão de ~8/255, e o alpha praticamente não se move** — o canal que
*é* a sombra difere em no máximo **1** a 1024 px e em **0** a 512 px. O que se
move é RGB, por até 8, na borda do gradiente onde a sombra escura encontra o
fundo claro: é o efeito de a variância total ficar `0,09 %` estreita (§2).

### 4.1. A ressalva honesta: os 35 pixels de delta 25

Contando **todos** os pixels, o delta máximo a 1024 px é **25** no azul (e 20 a
512 px), em **35** pixels de 1.048.576 (`0,0033 %`). **Todos os 35 têm
`alpha == 0` nos dois quadros.** São RGB de um pixel totalmente transparente —
lixo de `acc[c] / a` com `a` abaixo de meio nível de 8 bits, invisível em
qualquer composite e presente antes desta mudança pelo mesmo motivo. A tabela
acima é a medida útil; esta é a medida completa, e as duas estão aqui porque
publicar só a primeira seria escolher o número que agrada.

---

## 5. A meta de 1 s a 1024 px NÃO foi alcançada, e o que sobra está medido

**10,30 s**, não `< 1 s`. O desfoque deixou de ser o gargalo e não virou o
gargalo de outra coisa: **ele não é mais mensurável no total.**

`[OBS]` Instrumentação temporária (um cronômetro por estágio, removido antes do
commit), Release, 1024 px, mesmo ícone:

| estágio | chamadas | tempo |
|---|---|---|
| **`generateField` (o campo de distância)** | **10** | **10.455 ms** |
| `shadowImage` — a cadeia inteira da sombra | 10 | 1.179 ms |
| ↳ o **desfoque** | 10 | **465 ms** |
| ↳ a máscara do anel | 10 | 333 ms |
| ↳ a translação | 10 | 302 ms |
| `generateFieldFromAlpha` | **0** | 0 ms |

A 512 px: campo **1.945 ms**, sombra `232 ms`, desfoque **126 ms**, de
`2,69 s` totais.

**O tempo que resta é o campo de distância, e são 73 % do render.**
`DistanceField.cpp:305`, `generateField`, é um laço `para cada pixel → para cada
segmento de contorno`: 1.048.576 pixels vezes os segmentos da arte, dez vezes,
sem estrutura de aceleração. Um microssegundo por pixel por camada. A frente
irmã que mede custo tem aqui um alvo nomeado, com número, e ele **não** é o
especular (§3.1) e **não** é `generateFieldFromAlpha`, que este ícone nunca
chama — a arte dele é vetorial, então o campo sai dos contornos e não do alpha.

`[OBS]` Dentro do `1,18 s` que sobra na sombra, os `465 ms` de desfoque já não
são o kernel: são as passadas de resolução cheia que a escada ainda paga —
premultiplicar, reduzir, expandir, despremultiplicar — sobre 4 M de floats por
sombra. Reduzir isso significaria manter a sombra na grade pequena até o
composite, o que muda `GlassShadow.cpp` e não só o kernel, e não foi feito.

---

## 6. O que ficou `[OBS]`

1. Se a escada recorre no alvo (§2) — a escolha desta frente é `[INF]` declarada.
2. Os filtros de reamostragem do alvo (§2).
3. A largura do kernel por passada no caminho de GPU, `2.1σ` contra `2.8σ` (§2).
4. O custo de `generateField`, medido em §5 e **não consertado**: está fora de
   `BlurKernel.*` / `GlassShadow.*`.
5. Tudo que o laudo anterior deixou aberto continua aberto: a superfície do
   `blur-material`, o `opaque:`, e `render_variable`.

---

## 7. A suíte

`cmake --preset mingw -DIC_BUILD_UI=OFF`, alvo `ic_tests`, com
`IC_CORPUS_DIR` apontada para o corpus: **624 casos, 0 falhas**, em `b459c49`.
