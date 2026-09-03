# Spec — o `automatic`, e o `automatic-gradient` que veio junto

**Data:** 2026-09-02 · **Estado:** aprovado, em execução

Fechar o `fill: automatic` de ponta a ponta. Este documento diz o que foi lido, o
que será construído, o que **não** será, e — diferente do spec do vidro — ele
começa dizendo que **a pergunta que o originou estava errada**.

---

## 1. Por que agora

`scripts/slice-reach.py`, hoje, depois do vidro:

| bloqueio | camadas | documentos |
|---|---|---|
| **`fill: automatic`** | 29 (14,9%) | **26 (47,3%)** |
| traço pintado | 31 (16,0%) | 10 (18,2%) |
| `automatic-gradient` | 17 (8,8%) | 14 (25,5%) |

`[ART]` A contrafactual, medida com o `blockers_of` do próprio `slice-reach` e
com o arnês conferido contra o baseline publicado (111 e 15) antes de qualquer
número novo:

| resolvendo | camadas | documentos |
|---|---|---|
| hoje | 111 (57,2%) | 15 (27,3%) |
| **+ `automatic`** | **125 (64,4%)** | **29 (52,7%)** |
| + `automatic` + `automatic-gradient` | 140 (72,2%) | 32 (58,2%) |
| *(controle)* `automatic-gradient` sozinho | 112 | **15 — zero ganho** |
| *(controle)* traço sozinho | 141 | 17 |

**O `automatic` quase dobra o alcance de documento sozinho** — é o maior salto de
documento que qualquer item da fila produz. O traço compra o dobro de *camadas* e
quase nenhum *documento*, porque traço se concentra em documentos já bloqueados de
outras três formas.

### 1.1. E a ressalva que o próprio levantamento levantou

`[ART]` **Dos 14 documentos que destravam com o `automatic` sozinho, 10 são
variantes de cor do `DimensionDev__Flare`, referenciando um SVG byte-idêntico.**
Os outros quatro são `AdguardTeam__AdguardForiOS__ProAppIcon`,
`Jellify-Music__App__teal-icon-composer`, `OpenSource03__harnss__icon` e
`robertying__learnX__AppIcon`.

**Os +14 documentos são 5 desenhos distintos.** O número é real e a régua o conta
certo; a diversidade atrás dele não é 14. Isso fica dito aqui porque um número que
só o autor sabe interpretar não é uma medida, é um argumento.

---

## 2. A pergunta estava errada, e a resposta é melhor

Este spec nasceu de uma hipótese: *"`automatic` é o gradiente de chiclet do
sistema, com a aparência escolhendo o polo"*. Ela estava **metade certa**, e a
metade errada é a interessante.

### 2.1. São DUAS operações diferentes com o mesmo nome

`[BIN]` O `IconComposerKit.arm64` importa **exatamente seis** construtores de
`Icon.Fill` e mais nada, e todos os sítios de chamada deles vivem em **duas**
funções, cujos limites o `LC_FUNCTION_STARTS` dá exatos:

| função | é o quê | prova |
|---|---|---|
| `0x10AD9C`–`0x10B080` | o fill do **fundo / chiclet** | o único chamador dela desce para `Icon(name:chiclet:layers:…)` |
| `0x10B7EC`–`0x10C448` | o fill da **camada** | chama `Layer.fill`, `Layer.isGlass`, `Layer.frame`, e termina em `Icon.Element(contents:bounds:fill:…)` |

**As duas leem o mesmo tipo do documento e dão respostas diferentes.**

### 2.2. A ordem dos casos, lida de uma instrução que os NOMEIA

`[BIN]` Tudo pende da numeração, então ela não foi assumida: o
`Fill.Kind.displayName` em `IconComposerFoundation 0x0A8D40` é uma cadeia de
`csel` sobre strings pequenas de Swift, e decodificar os imediatos dá os nomes:

| tag | caso | nome de exibição |
|---|---|---|
| 0 | `none` | "None" |
| 1 | `automatic` | "Automatic" |
| 2 | `solid` | "Solid" |
| 3 | `automaticGradient` | **"Standard Gradient"** |
| 4 | `linearGradient` | **"Custom Gradient"** |
| 5 | `systemLight` | "System Light" |
| 6 | `systemDark` | "System Dark" |

### 2.3. No FUNDO — a hipótese sobrevive, com um terceiro braço que ninguém previu

`[BIN]` `0x10AEF4`:

```
and  w8, w26, #0xff        ; w26 = rendition.sourceAppearance
cmp  w8, #2
b.lo -> systemLightChicletGradient    ; base(0), light(1)
b.eq -> systemDarkChicletGradient     ; dark(2)
     -> IconColor.clear ; Fill.solid(clear)   ; tinted(3)
```

**Sob `tinted`, o fundo `automatic` é TRANSPARENTE.** Não é um cinza, não é um
gradiente: é `IconColor.clear = (0,0,0,0)`.

`[BIN]` E o `none` no fundo percorre **o mesmo bloco**, byte a byte. No fundo,
`none` não quer dizer "não desenhe".

### 2.4. Na CAMADA — não é cor nenhuma

`[BIN]` O fill da camada é resolvido **duas vezes**, em dois slots de
especialização. O segundo (`0x10BD90`) constrói o slot com a aparência **forçada a
`light`**: `mov w9,#0x100 ; bfxil w9,w0,#0,#8`.

E o `automatic` da camada, em `0x10C0FC`–`0x10C138`, é:

> **"o que o fill DESTA MESMA camada resolve na aparência `light`"** — uma cópia
> verbatim daquele buffer. E sob `light` ele próprio vira **`nil`**.

`[BIN]` Não é `.system`. Não é cor. É um **operador de herança de
especialização**. E `none` na camada é `Element.fill = nil` — sem override, a arte
fica com as cores dela.

> **Isto corrige o doc 03 §24.3**, que atribui "55 fills de camada + 28 de fundo"
> ao `Icon.Fill.Contents.system`. **Só os 28 do fundo viram `.system`.** Os 55 da
> camada nunca chegam lá.

### 2.5. E o corpus previu isso antes de eu contar

`[ART]` A leitura faz duas previsões falsificáveis, e as duas fecham:

| previsão | medido em 145 documentos |
|---|---|
| `none` no fundo seria indistinguível de `automatic`, logo redundante | **0 ocorrências** (contra 49 na camada) |
| `system-light`/`system-dark` são vocabulário de chiclet, logo só de fundo | **0 na camada** (33 no fundo) |

Duas divisões limpas 0/N, as duas do lado que a leitura exige.

`[ART]` E o terceiro fato, o que mais restringe: **toda camada `automatic` nomeia
arte (51/51), toda camada `none` nomeia arte (40/40), e as 159 camadas sem chave
`fill` também.** O `fill` não é o que faz a camada existir — a arte é. Um fill que
substituísse a arte tornaria `none` e "sem chave" a mesma afirmação, e o corpus
mantém as duas, 49 e 159 vezes.

---

## 3. O eixo — o que afundou o `automatic-gradient` está lido

O spec do gradiente (`2026-09-01-gradiente.md` §4.4) deixou o `automatic-gradient`
**não implementado e nomeado**: os seis parâmetros estavam medidos e **o eixo
nunca foi**. Essa razão caiu.

`[BIN]` `Icon.SystemFill` resolve por `Icon.Fill.resolve` em `IconRendering
0x3CE80`, que escolhe entre as duas rampas por `cmp x8, #1` e **zera a região de
placement**, gravando `Optional<GradientPlacement>` = `.none`.

`[BIN]` E `.none` tem significado definido — `GradientPlacement.default`,
`IconRendering 0x38CF4`:

```
start = (0.0, 0.0)
end   = (0.0, 1.0)
```

`[BIN]` O caminho de desenho substitui o nil por esse default (`0x1BAB8`), e o
placement é em **coordenadas unitárias de um retângulo**:
`ponto = rect.origin + unit * (largura, altura)`.

`[BIN]` **Os três sítios conversores passam `placement: nil` para o
`automaticGradient`.** Então ele herda o mesmo eixo vertical padrão. **O
`automatic-gradient` está inteiramente especificado.**

### 3.1. As duas rampas, completas

`[BIN]` O construtor em `IconRendering 0x3E680` escreve exatamente **duas
paradas**, com `r == g == b` e alpha `1.0`:

| | parada 0 @ `0.0` | parada 1 @ `1.0` |
|---|---|---|
| `systemLightGradient` | `1.0` (255) | `0.9607843137254902` (245) |
| `systemDarkGradient` | `0.12156862745098039` (31) | `0.058823529411764705` (15) |

`[BIN]` E o `resolve` **reescreve o alpha de cada parada** com a opacidade do
fill, em vez de multiplicar: o helper em `0x3CFF4` lê `r,g,b` de `+0x00/+0x08/+0x10`
e a localização de `+0x20`, **pulando o alpha da origem em `+0x18`**.

---

## 4. O que será construído

Cada peça segue o padrão da torre. O `AutomaticGradient.h/.cpp` já existe e é o
modelo mais próximo: uma regra lida do binário, gatada na CPU sem GPU.

| # | tarefa | prova |
|---|---|---|
| 1 | `Fill.Kind` com as 7 tags na ordem lida do `displayName` | tabela inteira testada, não três pontos |
| 2 | o conversor de **fundo**: os 7 casos, incluindo `tinted → clear` e `none → mesmo caminho` | os três braços da aparência exercitados |
| 3 | o conversor de **camada**: a herança do slot `light`, e o colapso para `nil` sob `light` | inclusive a cadeia `automatic → automatic → nil` |
| 4 | as duas rampas do `SystemFill` + a reescrita de alpha | valores exatos; alpha **substituído**, não multiplicado |
| 5 | `GradientPlacement.default` e a substituição do nil | unidade → retângulo, nos dois eixos |
| 6 | ligar o `automatic-gradient`, que agora tem eixo | o §4.4 do spec do gradiente deixa de valer |
| 7 | a correção do `orientation` no fundo (§5.2) | 8 + 6 ocorrências reais do corpus |
| 8 | mutações no gate | cada guarda morde |
| 9 | re-medir o alcance | número novo, medido |

---

## 5. O que este spec NÃO cobre, e duas correções que ele carrega

### 5.1. As lacunas nomeadas

| | por quê |
|---|---|
| **o retângulo do alinhamento ao chiclet** | `[BIN]` `supportsChicletAlignmentForSystemFills` é **`true`** por default, e quando ligado o rect é origem `(0,0)` com um `CGSize` do contexto de desenho. `[OBS]` **Se esse tamanho é o canvas, o chiclet ou o quadro full-bleed não foi lido.** A primeira implementação desenha sobre o `boundingRect` da própria forma e **nomeia essa divergência**, em vez de chutar o rect |
| **a direção do eixo `(0,0)→(0,1)`** | `[OBS]` a lateralidade de y do display list do RB não foi estabelecida. Na rampa clara (255→245) é quase invisível; **na escura (31→15) não é** |
| o `Bool` do `.system(_, Double, Bool)` | `[OBS]` os três construtores gravam `1`; nenhum escritor de `0` foi achado |
| o espaço de cor das rampas | `[OBS]` `IconColor` são quatro `Double` sem tag de espaço |
| `ResolvedFill` e `promoteNoneFillsToEachAppearance` | `[OBS]` localizados, não lidos. São do **editor**, não do caminho de render |

### 5.2. E uma correção a um comportamento que já desenhamos

`[BIN]` O `orientation` do documento é **descartado em três dos quatro sítios**.
Só o caminho de `linearGradient` **de camada** constrói um `GradientPlacement` a
partir dele; o conversor do chiclet lê `primaryColor` e `secondaryColor` e **nunca
toca** no `orientation`.

`[ART]` Um `linear-gradient` de **fundo** com `orientation` desenha no eixo
vertical padrão, e o corpus tem 8 ocorrências na raiz mais 6 em especializações.
**Isto é alcançável e o nosso renderizador provavelmente honra o `orientation`
onde o alvo o ignora** — a tarefa 7 existe por isso.

---

## 6. Como saber que acabou

- O gate passa com a varredura completa, e as mutações novas mordem.
- `slice-reach` mostra **125 camadas e 29 documentos** com o `automatic`, e
  **140 e 32** com o `automatic-gradient` junto — medido, não estimado.
- O `automatic-gradient` sai da lista de armas não implementadas, e o §4.4 do
  spec do gradiente ganha a nota de que a razão dele caiu.
- Cada lacuna do §5.1 está **nomeada** no relatório do `icrender`, com o motivo.

---

## 7. O resultado, medido

*2026-09-03. As nove tarefas rodaram; isto é o que elas entregaram.*

`[ART]` A régua, medida por mim contra a régua anterior tirada do próprio git
(`4ef51b8`) e rodada sobre o mesmo corpus:

| régua | antes | depois | o §6 prometia |
|---|---|---|---|
| camadas | 111 (57,2%) | **145 (74,7%)** | 145 |
| documentos (camadas) | 15 (27,3%) | **33 (60,0%)** | 33 |
| documentos **completos** (fundo *e* camadas) | **1** (1,8%) | **33** (60,0%) | — |
| suíte | 417 casos | **478 casos, 0 falhas** | — |
| mutações | 209 | **231** | — |

A linha dos documentos completos é a que mede o que realmente mudou: até ontem o
`fill` da raiz não era lido, e as camadas eram compostas **sobre nada**. Um só
documento do corpus fechava inteiro. Agora fecham 33.

`[ART]` **E o gate fechou: `gate-m1 passed`, 231 de 231, numa execução única.**
Zero sobreviventes, zero restaurações e zero divergência de hash no log. As 22
mutações que este trabalho acrescentou foram todas pegas — inclusive a que
troca a *reescrita* do alpha por uma multiplicação, que **nenhum caso do corpus
consegue distinguir** e que existe só para provar que o teste dedicado do §30.6
não é decoração.

As duas tentativas anteriores de rodar a lista inteira tinham sido mortas. A
diferença não foi o gate: foi rodá-lo num worktree dedicado
(`scripts/gate-worktree.ps1`), com a árvore de trabalho livre ao lado.

### 7.1. E o número dos documentos vende mais do que vale

`[ART]` Os **+18 documentos** não são 18 desenhos. **Dez deles são o
`DimensionDev__Flare`** — `AppIcon` e mais nove variantes de cor (`_black`,
`_blue`, `_cyan`, `_light_blue`, `_orange`, `_red`, `_teal`, `_white`,
`_yellow`) — e os dez apontam para **os mesmos dois SVGs, byte a byte**
(`Group.svg` e `Group-2.svg`, conferidos por SHA-256). O que muda entre eles é o
`fill`, que é exatamente a peça que este spec ligou.

Os outros oito são distintos, inclusive os três do `CodeEdit`
(`CodeEditAlphaIcon`, `CodeEditBetaIcon`, `CodeEditDevIcon`), que **não** são
variantes de cor: os conjuntos de assets deles têm hashes diferentes.

**São 9 desenhos distintos, não 18.** Dito aqui porque "60% dos documentos" é
verdadeiro e induz a erro sozinho, e porque um corpus público tem clusters — a
régua conta documentos, e documentos não são amostras independentes.

### 7.2. A ressalva da régua ficou MAIOR, e é para ficar

`[ART]` **Das 145 camadas desenháveis, 51 desenham SEM o material que o documento
pede** — eram 32 quando o vidro ligou. O número subiu porque mais camadas de
vidro passaram a ser alcançáveis, não porque algo regrediu: `translucency`,
sombra e especular do grupo seguem sem consumidor lido no binário (doc 03 §29),
então nenhum é aplicado.

`scripts/slice-reach.py` imprime as duas linhas, e a segunda diz o essencial:
**desenhável não é o mesmo que igual ao alvo.**

### 7.3. O que o §6 prometia e o que veio a mais

O §6 previa 125 camadas e 29 documentos com o `automatic` sozinho, e 140 e 32
com o `automatic-gradient` junto. Vieram **145 e 33**, e a diferença não é sorte:
o mesmo `placement: nil` que deu eixo ao `automatic-gradient` fechou o último
buraco do `linear-gradient` de camada — uma rampa que não nomeia `orientation`
era recusada e agora desenha no eixo vertical padrão. `[ART]` São **26 dos 48**
`linear-gradient` de camada do corpus.

O `scripts/slice-reach.py` registra os três estágios separados (125/29 → 140/32
→ 145/33) para que a promessa do spec continue conferível depois do fato.

### 7.4. As correções que este trabalho carrega

1. **O doc 03 §24.3 estava errado.** Ele atribuía "55 fills de camada + 28 de
   fundo" ao `Icon.Fill.Contents.system`. **Só os 28 do fundo viram `.system`**;
   os 55 da camada nunca chegam lá, porque na camada o `automatic` é herança de
   especialização e não cor. Corrigido no doc 03 §30.4.
2. **O §4.4 do spec do gradiente perdeu a razão de ser.** Ele recusava o
   `automatic-gradient` porque o eixo nunca tinha sido lido. Foi lido.
3. **O `orientation` do fundo agora é descartado de propósito e nomeado.** O
   alvo o ignora em três dos quatro sítios; honrá-lo seria um pixel diferente do
   dele. O descarte vai para `RenderedIcon::notes`.
