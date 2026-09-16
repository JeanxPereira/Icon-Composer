# Laudo — a passagem de overdraw da sombra: o que ela desenha, em que ordem, e o que ela NÃO explica

*2026-09-15. Frente: o item 3 do `[OBS]` de `2026-09-15-sombra-anel.md` §10 — "a
passagem de overdraw da §5.3 do laudo-mãe continua transcrita e não desenhada
(`kShadowOverdrawNote`)". Entrada adicional, e a razão de a frente existir hoje:
a pista que `2026-09-15-realce-forma.md` deixou — o gabarito da Apple tem um
**aro escuro de 0–5 unidades de canvas no ápice superior** que não pode vir de
nenhum dos cinco realces, e a passagem de overdraw era um dos três candidatos.*

**A passagem está desenhada, e ela NÃO é o aro.** A aritmética da alpha já
estava lida; o que faltava era a geometria e a ordem, e as duas estão agora
lidas com endereço. O perfil por profundidade, medido antes e depois no mesmo
lugar em que o laudo da forma mediu o dele, diz que esta passagem escurece
**0,11 luma a 0–2 unidades** e **4,06 luma a 60 unidades**: ela é
aproximadamente zero exatamente onde o aro está e cresce para dentro. É o
formato oposto ao do aro. Os outros dois candidatos — refração e
`blur-material` — seguem de pé.

Endereços de `References/2.0-125/out/slices/IconRendering.arm64` (VA ==
deslocamento de arquivo), salvo onde estiver escrito `RenderBox`.

---

## 1. O que a passagem desenha: a MESMA sombra, outra vez

`[BIN]` A passagem de overdraw **não é um segundo efeito**. É uma segunda
chamada à própria função que compõe a sombra principal — `0x49ED4` —, em
`0x46034` e `0x4AEB4`, com **o mesmo descritor**. Dentro dela, portanto, tudo é
o mesmo:

| o quê | endereço | vale nas duas chamadas |
|---|---|---|
| a imagem | `[descritor+0xB0]` → `0x85ED8` em `0x4A18C` | sim |
| o retângulo | `setRect:` em `0x4A1AC` | sim |
| a tinta | `setRBImage:…tintColor:…` em `0x4A1F0` | sim |
| a alpha | `shadowOpacity × Opacity[3−c] × layerOpacity`, `0x4A06C`/`0x4A070` | sim |

`[BIN]` **A única coisa que `w1` muda lá dentro é o byte de mescla.** Varrendo a
função inteira (`0x49ED4`–`0x4A21C`), `w1` aparece em exatamente dois lugares, e
os dois são o mesmo `csel`:

```
0x00049F94  tst   w1, #1
0x00049F98  csel  w8, w10, w11, ne      ; ramo NEUTRO:   w10 = Shadow.overdrawBlendMode (ctx+0x45A2)
                                        ;                w11 = Shadow.blendMode         (ctx+0x45A0)
...
0x0004A004  tst   w1, #1
0x0004A008  csel  w8, w10, w8, ne       ; ramo VIBRANTE: idem, sobre o resultado do
                                        ;                blendModeForVibrantOnDim (ctx+0x45A1)
```

`Shadow` mora em `ctx+0x4508`, então `+0x45A0`/`+0x45A1`/`+0x45A2` são
`Shadow+0x98`/`+0x99`/`+0x9A` — os três bytes de mescla do laudo-mãe §3. Com os
padrões: `multiply` nos dois casos.

**Então o que faz dela um "overdraw" está FORA da função.** É o recorte.

## 2. Onde ela desenha: recortada pela cobertura da PRÓPRIA ARTE

`[BIN]` Em volta da segunda chamada, o alvo faz três coisas, nesta ordem
(números da cópia grande em `0x435A0`; a cópia limpa em `0x4AC84` é idêntica e
está entre parênteses):

```
0x00045FA8 (0x4AE90)   beginLayer
0x00045FE4 (0x4AE98)   bl 0x4B4EC                     <- o conteúdo do recorte
0x00045FF4 (0x4AEA8)   clipLayerWithAlpha: <alpha> mode: 0
0x00046034 (0x4AEB4)   bl 0x49ED4  com w1 = 1         <- a segunda sombra
```

E `0x4B4EC` **não é um construtor de máscara: é o desenho do conteúdo.** A prova
é um `xref`: `0x4B4EC` tem quatro chamadores, e um deles é `0x4B3EC`, que está
**dentro de `0x4AF20`** — a função que a passagem de conteúdo chama uma
instrução antes (`0x4ADA4`). `0x4AF20` termina assim:

```
0x0004B3E4  bl   #0x8e260   ; _objc_msgSend$addColorMatrixFilterWithArray:flags:
0x0004B3E8  mov  x0, x22    ; x22 = x1 = o descritor (0x4AF4C)
0x0004B3EC  bl   #0x4b4ec
```

e o `cmp w0,#1 / b.eq 0x4b3e8` de `0x4AF78` pula **direto** para essas duas
últimas instruções quando o predicado de `0x40E60` responde, de modo que no caso
comum `0x4AF20` **é** `0x4B4EC`. A camada de recorte recebe o mesmo desenho, do
mesmo descritor, que a passagem de conteúdo acabou de pôr na tela.

> Isto é o nome do campo virando geometria: `drawOverContent`. A sombra
> principal desenha **embaixo** da arte, sem recorte; esta desenha **a mesma
> imagem por cima** dela, recortada a ela.

`[BIN]` Os dois portões, lidos em `0x45F04`–`0x45F14` (e `0x4ADBC`–`0x4ADC8`):

```
if (ctx+0x45B1 != 1)  -> não abre        ; Shadow.drawOverContent (Shadow+0xA9), padrão true
if (desc+0x31 != 0)   -> não abre        ; ver §6, o [OBS] que sobra
```

## 3. Em que ordem: depois do conteúdo, ANTES dos realces

`[BIN]` O driver por elemento, `0x48B74`, chama três coisas e nesta ordem:

```
0x00048BD4  bl 0x4A2D4    ; a passagem do VIDRO -- onde a sombra principal (0x49ED4, w1=0) vive
0x00048C08  bl 0x4AC84    ; o CONTEÚDO (0x4ADA4 -> 0x4AF20) e, logo depois, o OVERDRAW
0x00048DB4  bl 0x491C0    ; os REALCES
```

e `0x48DB4` está no ponto de junção `0x48DAC` para onde **todos** os `b.eq`
intermediários saltam, isto é, os realces correm em qualquer ramo. O laço por
elemento de `0x48E20` roda os mesmos três na mesma ordem dentro do corpo
(`0x48F30`, `0x48F5C`, `0x48EAC`), e a cópia grande de `0x435A0` também
(`0x45DDC`, `0x45DF0`–`0x46038`, `0x45D10`/`0x46688`).

**A resposta à pergunta que motivou a frente é, portanto, "antes dos realces".**
Se a passagem escurecesse o ápice, o realce ainda passaria por cima dela — o que
é exatamente a configuração em que um brilho pode nascer *sobre* um aro escuro.
A configuração está certa; o que não está é a quantidade, §5.

## 4. `clipLayerWithAlpha:mode:` — o `mode 0` está lido, e as duas leituras anteriores estavam erradas

Esta é a pergunta 3 do pedido, e a resposta **derruba** o que o repositório
carregava em dois lugares.

`[BIN]` `RenderBox.arm64`, `_RBDrawingStateClipLayer` (`0x3BB64`) passa o
argumento `mode:` pela função de `0x8B624` antes de entregá-lo ao quarto
parâmetro de
`RB::DisplayList::Builder::clip_layer(Layer*, State&, float, ClipMode)`
(`0xC9EFC`):

```
0x0003BBC0  mov  x0, x19          ; x19 = o argumento `mode:`
0x0003BBC4  bl   #0x8b624
0x0003BBC8  mov  x3, x0           ; -> ClipMode
0x0003BBDC  bl   #0xc9efc
```

`[BIN]` **E `0x8B624` carrega DOIS nomes manglados** — `RB::aliasing_mode(RB::RenderingMode)`
e `rb_clip_mode(RBClipMode)` — porque o *identical code folding* fundiu os dois:
o corpo tem três instruções, `cmp w0,#1 / cset w0, eq / ret`. O tipo do
parâmetro **no sítio da chamada** é `ClipMode`, então o nome que vale ali é o
segundo.

`[BIN]` E `ClipMode` tem **dois** valores, com nome. `RB::XML::Value::ClipMode::to_string`
(`0x130BB0`) limita o enum em `cmp w8,#1 / b.hi <ret>` e indexa a tabela de
ponteiros de `0x18F8A8`, que tem exatamente duas entradas:

| valor | nome |
|---|---|
| `0` | **`normal`** |
| `1` | **`inverse`** |

Portanto `mode: 0` significa **"recorte normal, não invertido"**.

- O `[INF]` do laudo do VCM — *"`mode 0` é recorte por alfa"* — está **meio
  derrubado**: ele acerta que não há inversão, mas o `0` não escolhe *por
  alfa*; a alpha é o `float`, um argumento separado.
- O `[INF]` de `2026-09-15-sombra-anel.md` §6 — *"é o modo de antialiasing;
  máscara direta"* — está **derrubado**. Ele leu o nome fundido pelo ICF. O
  pixel que ele desenhou continua certo; a razão que ele deu não é a do binário,
  e `kShadowRingNote` herdava essa razão.

`[INF]` **O que o `float` faz continua inferência**, e vale dizer de onde vem: o
seletor se chama `clipLayerWithAlpha:`; `clip_layer` **destrói a camada** quando
o `float` é `0` e a camada não é trivial (`0xC9F44`–`0xC9F50`); e todas as
sobrecargas de `make_clip` do `RenderBox.arm64` têm a assinatura
`(Builder&, float, ClipMode, vector<Clip*>&)`, isto é, o `float` viaja junto da
**cobertura** e não do preenchimento. Três indícios, nenhuma leitura direta do
rasterizador.

## 5. As duas tabelas, relidas — e as duas são PLANAS

Pergunta 4 do pedido. `[BIN]` Do mesmo construtor embutido, `0x5ECA8`–`0x5ECD8`:

```
0x0005ECA8  adrp x9, #0x98000
0x0005ECAC  ldr  q0, [x9, #0x630]      ; (0.3, 0.2)
0x0005ECB0  mov  x9, #-0x6666666666666667
0x0005ECB4  movk x9, #0x999a
0x0005ECB8  movk x9, #0x3fc9, lsl #48  ; 0x3FC999999999999A = 0.2  <- mov+movk, NÃO está no pool
0x0005ECBC  dup  v1.2d, x9
0x0005ECC0  stp  q0, q1, [x0, #0xc0]   ; campos +0xB0..+0xCF
0x0005ECC8  ldr  q1, [x9, #0x640]      ; (0.2, 0.5)
0x0005ECCC  ldr  q0, [sp, #0x20]       ; (0.5, 0.5), do fmov v3.2d,#0.5 de 0x5E928 (str em 0x5E938)
0x0005ECD0  stp  q1, q0, [x0, #0xe0]   ; campos +0xD0..+0xEF
0x0005ECD4  mov  x9, #0x3fe0000000000000
0x0005ECD8  str  x9, [x0, #0x100]      ; campo +0xF0
```

(o objeto tem cabeçalho de `0x10`, daí `[x0, #0xc0]` ser o campo `+0xB0`).
Resultado:

| campo | display | large | medium | small |
|---|---|---|---|---|
| `maxNeutralOverdrawOpacity` | 0,2 | 0,2 | 0,2 | 0,2 |
| `maxVibrantOverdrawOpacity` | 0,5 | 0,5 | 0,5 | 0,5 |

e `translucencyForMaxOverdraw = 0,3`.

**As duas são planas**, que é exatamente por que a armadilha de índice é
silenciosa aqui: `valor[c]` e `valor[3−c]` dão o mesmo pixel. Por isso ela foi
escrita pelo helper `sizeBasedValue` que já existe — por princípio, não por
evidência. O seletor do binário (`0x4ADE0`–`0x4AE74`) foi conferido assim mesmo,
e é a escada de sempre: `x9..x12` apontam para os quatro escaninhos **vibrantes**
(`ctx+0x45E0`, `+0x45E8`, `+0x45F0`, `+0x45F8`), o `cmp w13,#2` troca por
neutros (`+0x45C0`, `+0x45C8`, `+0x45D0`, `+0x45D8`), e os três `csel` de
`0x4AE60`–`0x4AE74` resolvem `c=3→+0x00`, `c=2→+0x08`, `c=1→+0x10`, `c=0→+0x18`.

`[BIN]` A conta completa, `0x4AE44`–`0x4AE7C` (idêntica em `0x45F60`–`0x45F94`):

```
t     = clamp( translucency / translucencyForMaxOverdraw , 0 , 1 )
quad  = (shadowStyle == neutral) ? maxNeutral : maxVibrant
alpha = t × quad[3 − c]
if (alpha <= 0) não desenha          (0x4AE84 / 0x45F9C)
```

`[ART]` **Quantos documentos abrem a passagem:** dos 271 grupos dos 145
documentos do corpus, **149 abrem** (em 114 documentos) — os que têm sombra
diferente de `none` e `translucency` habilitada com valor positivo. E o `t`
satura quase sempre: a alpha do recorte é `0,2` em 105 grupos e `0,5` em 18,
isto é, 123 dos 149 já estão no teto de `translucency ≥ 0,3`. Os outros 26
ficam entre `0,033` e `0,467`.

## 6. O que a medida diz: ela NÃO é o aro do ápice

Este é o ponto da frente, e a regra do oráculo mandou medir o **perfil**, não a
média.

Método: `apple-512.png` contra o nosso render a 412 px posto com
`png-diff.py --legacy-inset` (recuo `100/1024`, sem reamostrar), de modo que **1
px do quadro de 512 = 2,0 unidades de canvas** (o corpo vale 824 unidades em 412
px). O ápice superior do chiclet azul é achado por coluna como a linha do maior
salto de luma entre `y=100` e `y=140`, e o perfil é a média de 21 colunas,
`x = 250..270`. A borda cai em `y = 111,14` no nosso render e `y = 111,19` no da
Apple — as duas no mesmo lugar, o que é o que torna o perfil comparável.

| profundidade (un.) | Apple | antes | depois | **depois − antes** | Apple − antes |
|---|---|---|---|---|---|
| 0,0 | 139,44 | 222,20 | 222,09 | **−0,11** | −82,76 |
| 2,0 | 151,03 | 204,67 | 204,55 | **−0,12** | −53,64 |
| 4,0 | 195,17 | 177,80 | 177,73 | **−0,07** | +17,37 |
| 6,0 | 213,19 | 166,22 | 165,97 | −0,25 | +46,96 |
| 8,0 | 203,45 | 163,76 | 163,65 | −0,11 | +39,69 |
| 12,0 | 184,98 | 160,79 | 160,21 | −0,57 | +24,19 |
| 16,0 | 165,84 | 158,45 | 157,64 | −0,81 | +7,39 |
| 24,0 | 166,31 | 156,51 | 155,11 | −1,39 | +9,80 |
| 36,0 | 162,92 | 154,11 | 151,42 | −2,70 | +8,81 |
| 52,0 | 156,84 | 149,88 | 145,99 | −3,89 | +6,96 |
| **60,0** | 153,96 | 147,77 | 143,71 | **−4,06** | +6,19 |
| 84,0 | 146,20 | 141,78 | 137,87 | −3,91 | +4,42 |
| 116,0 | 133,49 | 133,04 | 129,61 | −3,43 | +0,45 |

`[BIN]` (medida) **A passagem é monótona crescente na profundidade e vale
praticamente zero no aro.** O escurecimento é de 0,1 luma a 0–4 unidades, pica
em ~4 luma perto de 60 unidades e desce devagar. O aro do gabarito é o oposto:
local, entre 0 e 5 unidades, com o brilho voltando logo abaixo dele.

E o motivo é geométrico e previsível da própria transcrição: a imagem que ela
compõe é a sombra, isto é, a silhueta com o **anel** de `ringWidth` (que zera a
sombra sobre o contorno e a leva a 1 dezesseis unidades para dentro), **borrada**
com sigma 19,2 unidades e **deslocada 32 unidades**. Dentro da arte, perto da
borda de cima, essa imagem é quase nula por construção. Recortá-la à arte não
pode produzir um aro no contorno; produz o contrário de um aro.

### 6.1. E o sinal do deslocamento não salva a hipótese — diagnóstico, não conserto

O projeto carrega um `[OBS]` antigo sobre a lateralidade de `y` do display list
do RB (o laudo do gradiente do fundo o nomeia). Se `offsetY = 32` fosse **para
cima**, a sombra cairia sobre o topo da arte e o aro seria plausível. Foi
medido, com `out.offsetY` negado num build temporário **revertido antes do
commit**:

| profundidade (un.) | 0,0 | 4,0 | 8,0 | 16,0 | 24,0 | 28,0 |
|---|---|---|---|---|---|---|
| depois − antes, `offsetY` negado | −6,67 | −5,06 | −5,54 | −5,78 | −5,59 | −5,31 |

`[BIN]` (medida) **Nem assim há aro.** Com o sinal trocado o escurecimento fica
quase CONSTANTE em ~5,5 luma em toda a profundidade: é um escurecimento geral,
não uma banda. Nenhum dos dois sinais produz uma feição local em 0–5 unidades.
O `[OBS]` da lateralidade continua aberto e continua **não sendo** a explicação
do aro — o que é uma parede a menos para a próxima frente.

### 6.2. Contra a média global, ela não melhora

`png-diff.py apple-512.png <nosso> --legacy-inset`, 189.993 pixels visíveis:

| | R | G | B | A |
|---|---|---|---|---|
| antes | 8,95 | 10,03 | 9,89 | 4,95 |
| depois | **8,93** | **10,26** | 9,89 | 4,95 |

R melhora 0,02, G piora 0,23, B não move. **E isso não é motivo para não
desenhá-la.** A tabela da §6 mostra por quê: onde a passagem age (20–116
unidades de profundidade) o gabarito já estava **mais claro** que nós
(`Apple − antes` positivo e caindo de +10 a +0,45), e escurecer piora esse
trecho. Mas a aritmética, a geometria e a ordem estão lidas com endereço, e o
critério deste repositório é a leitura e não o diff. Desligá-la porque o Δ médio
subiu 0,23 seria exatamente o overfitting que a regra proíbe, só que com o sinal
invertido.

O que a medida **acrescenta** é um `[OBS]` novo e útil: se a nossa composição
tem um excesso de luz no miolo das formas de 5 a 10 luma que a passagem de
overdraw não explica e nem corrige, ele é de outro lugar — e os dois candidatos
que sobraram da pista original (refração e `blur-material`) continuam por
desenhar.

## 7. O pixel

| documento | texels mudados | % dos visíveis | Δ máx. por canal | Δ médio (R,G,B) |
|---|---|---|---|---|
| `AppIcon-27` a 412 px | **62.308** | 38,95 % de 159.964 | 3 / 5 / 7 / 0 | 0,51 · 0,91 · 1,28 |
| `GoWToolkit` a 1024 px, `--idiom square` | **104.527** | 10,59 % de 987.176 | 2 / 1 / 1 / 0 | 0,08 · 0,05 · 0,02 |

`[ART]` O ícone do usuário abre a passagem — grupo único, `shadow: neutral
0.5`, `translucency: {enabled, 0.5}` — e com `0,5 / 0,3 > 1` o `t` satura, então
a alpha do recorte é `0,2` e a da sombra `0,5 × 0,375 = 0,1875`. O produto dos
dois é `0,0375`, o que é exatamente por que a mudança lá é de **2 níveis no pior
pixel**: é um escurecimento de menos de 4 % aplicado ao que a sombra já cobria
dentro do traço. Nada parecido com os 91.301 px / Δ 101 que o deslize de `sd`
teria causado no mesmo ícone.

## 8. O tempo

Release, `-DIC_BUILD_UI=OFF`, alvo `icrender`, tempo interno impresso pelo CLI:

| documento | antes | depois |
|---|---|---|
| `AppIcon-27` a 412 px | 0,279 s | **0,268 s** |
| `GoWToolkit` a 1024 px | 0,749 s (relógio de parede) | 0,716 s (relógio), **0,514 s** interno |

Iguais dentro do ruído, e de propósito: **a imagem não é reconstruída**. O borrão
é a coisa mais cara do laço, então `IconRenderer.cpp` guarda a imagem que a
sombra principal já compôs e a passagem de overdraw só multiplica uma máscara
sobre a alpha dela. O custo da passagem é uma varredura de texels e um
`blendOver`.

Suíte em Debug (`mingw`, `-DIC_BUILD_UI=OFF`, `IC_CORPUS_DIR` posta):
**647 casos, 0 falhas** (646 antes desta frente, mais um caso novo), 69,4 s.

## 9. O que entrou no código

- **`Source/RenderBox/GlassShadow.h`/`.cpp`**: `shadowOverdrawImage(shadow,
  content, w, h, clipAlpha)` — a sombra já composta, com a alpha multiplicada
  pela cobertura da arte e pela alpha do recorte. Devolve vazio quando
  `clipAlpha <= 0`, que é a saída `alpha <= 0` do binário. O cabeçalho ganha a
  leitura inteira da §1 à §5, inclusive a derrubada do `mode`.
- **`kShadowOverdrawNote`** deixa de dizer que a passagem não é desenhada.
  Passa a dizer a única coisa debaixo dela que continua não lida — o portão de
  `[descritor+0x31]`, §10 — com a mesma forma que `kShadowRingNote` já tem. É a
  regra que o próprio arquivo escreveu: uma nota só ganha lugar enquanto nomear
  uma lacuna aberta.
- **`Source/RenderBox/IconRenderer.cpp`**: um ponto só, mínimo. A lambda
  `castShadow` passa a guardar a imagem; uma lambda nova, `castShadowOverdraw`,
  a compõe com `overdrawBlendMode` depois do `blendOver` do conteúdo e antes do
  `drawSpecular`, nos dois ramos (vetor e raster).
- **`Source/RenderBox/IconRenderer.h`**: `RenderedIcon::glassShadowOverdrawn`,
  contado à parte de `glassShadowed` porque os dois portões são diferentes.
- **`Tests/test_glass_shadow.cpp`**: um caso novo, sobre as duas transcrições
  erradas que continuariam parecendo certas numa miniatura — recortar pela
  cobertura da SOMBRA em vez da do conteúdo, e deixar a COR do conteúdo passar.

## 10. O que continua `[OBS]`

1. **O portão de `[descritor+0x31]`.** `0x45F10` (`ldrb w8, [x24, #0xf1]`, com a
   base do descritor em `x24+0xC0`) exige que esse byte seja **zero** para a
   passagem abrir, e `0x4B518` (`cmp w24, #8`) despacha o mesmo byte dentro de
   `0x4B4EC`. `2026-09-15-sombra-desenho.md` o nomeia
   `FinalizedIcon.Layer.blendMode`, um `Icon.BlendMode` de 18 casos; `0` é
   `normal`. Este renderizador não tem como computá-lo a partir de um `.icon` —
   nenhuma chave de documento escolhe o `blendMode` de uma CAMADA — então a
   passagem abre sempre que a aritmética a abre. Se a leitura do byte estiver
   certa, isso significa que uma camada com mescla não-normal **não** ganharia
   overdraw no alvo, e aqui ganharia.
2. `[INF]` **O que o `float` de `clipLayerWithAlpha:` multiplica** (§4): a
   cobertura, por três indícios e nenhuma leitura do rasterizador.
3. `[OBS]` **A ordem interna do recorte contra o filtro de matriz de cor.**
   `0x4AF20` instala um `addColorMatrixFilterWithArray:` antes de chamar
   `0x4B4EC` no caminho longo, e a camada de recorte do overdraw chama `0x4B4EC`
   **sem** ele (`0x45FE4`/`0x4AE98`). Aqui a cobertura do recorte é a alpha da
   arte, o que é o mesmo pixel enquanto a matriz não mexer no alfa — e se ela
   mexe, não foi lido.
4. `[OBS]` **A luz a mais no miolo**, §6.2: `Apple − nós` é positivo e cai de
   +10 a +0,45 entre 20 e 116 unidades de profundidade, e nada desta frente a
   explica.
5. `[OBS]` **O aro escuro do ápice continua sem dono.** Dos três candidatos que
   a frente da forma nomeou, este está agora **eliminado com medida** (§6 e
   §6.1). Sobram a **refração** e o **`blur-material`**.

### Instrumentos

`scripts/macho.py` (`syms`/`fn`/`xref`/`dis`) sobre as duas fatias, e
`scripts/png-diff.py --legacy-inset`. O perfil da §6 saiu de um script de
*scratchpad* de 30 linhas que reaproveita `read_png`/`place_inset` do próprio
`png-diff.py` — nada dele foi versionado. **Nenhum MCP de RE foi usado; não
havia.** O `[BIN]` da §4 é o único que precisou de mais que o `macho.py`: a
tabela de nomes de `0x18F8A8` foi lida com `struct.unpack_from` direto no
arquivo, e os dois ponteiros são *chained fixups* cujos 32 bits baixos já são o
deslocamento da string.
