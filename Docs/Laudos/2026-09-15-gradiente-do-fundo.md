# O gradiente duro do fundo

> *"faz os efeitos do shape em si que ta com **gradiente duro** ainda sem efeito ...
> falta ajuste e polimento baseado em funções reais"*

A rampa estava dura porque **ela não é uma reta no alvo**. É um *smoothstep*, e o
caminho até esse número passa por quatro leituras encadeadas, nenhuma delas
inferida.

Este laudo tem três partes: o que explicava a dureza (§1–§5), as três pistas do
briefing, que todas dão **negativo** e continuam valendo como diagnóstico (§6–§9),
e o que sobrou aberto (§10).

---

## 1. O ponto de entrada: um literal `0x400`

`[BIN]` O caminho de desenho de um fill de documento é **uma** função —
`IconRendering 0x1AACC`–`0x1BFFC`, a mesma que já continha a substituição do
`GradientPlacement.default` (`0x1BAB8`) e a leitura do `boundingRect` (`0x1BC8C`).
Ela faz **uma** chamada de gradiente, em `0x1BC74`:

```
0x0001BC6C  mov  w4, #3         ; colorSpace
0x0001BC70  mov  w6, #0x400     ; flags
0x0001BC74  bl   _objc_msgSend$setAxialGradientStartPoint:endPoint:stopCount:
                                 colors:colorSpace:locations:flags:
```

Os dois são **literais**, sem nenhum caminho que os calcule. O braço sólido da
mesma função passa o mesmo `3` literal (`0x1B168`, `setColor:colorSpace:`).

`0x400` é o que muda a rampa. Os bits 8–11 dele são um código de interpolação, e
`0x400` faz esse código valer **4**.

## 2. O código 4 sobrevive ao construtor

`[BIN]` `RB::Fill::Gradient` guarda os flags num halfword em `+0x34`
(`ctor 0x98FDC`, `strh w26, [x22, #0x34]` em `0x99074`). Quando o chamador passa
um array de `locations` — e `IconRendering` passa — o construtor **limpa** o
nibble em `0x9908C` e, dentro do laço que confere o espaçamento das paradas, o
**reescreve**: código 1 vira midpoint, código 2 vira bézier por bytes, e
**qualquer outro é devolvido intacto** (`0x99154` → `0x991B0`–`0x991B8`,
`w14 = flags & 0xF00`).

Esse mesmo laço é quem liga o bit 15 quando alguma parada foge do espaçamento
uniforme (`0x99114`). Duas paradas em 0 e 1 são uniformes, então o bit 15 fica
**limpo** — e isso importa no passo seguinte.

## 3. Código 4 → rampa tipo 3 + bit 25

`[BIN]` `Gradient::set_fill_state 0x9B8AC`–`0x9BABC` monta o halfword que vai
para `RenderState+2`, ou seja, **os bits 16–31 da palavra 0** (`strh w8, [x19, #2]`
em `0x9BAA0`). Isso confirma de fora a numeração de bits do doc 03 §23: os 4 bits
de geometria×spread caem em 19–22, a dupla de tipo de rampa em 23–24, e os dois
bits soltos em 25 e 26.

O teste de "duas cores" está em `0x9B990`–`0x9B9AC`:

```
tipo != 4  &&  contagem == 2  &&  flags bit15 limpo  &&  (código - 2) & ~2 != 0
```

O último termo pega **2 E 4**. Com código 4 o teste falha e a execução cai em
`0x9B9B8`, que com o bit 15 limpo escreve `0x180` — **tipo de rampa 3**,
`sample_stops_uniform`. E `0x9B9EC` acrescenta:

```
cmp w8, #4 ; csel w11, #0x200, wzr, eq    →  palavra0 bit 25
cmp w8, #1 ; csel w8,  #0x400, wzr, eq    →  palavra0 bit 26 (gama), NÃO é o nosso caso
```

## 4. Bit 25 troca a rampa por um polinômio

`[BIN]` No metallib do RenderBox (`default_mod8.ll`), `sample_stops_uniform` com
o bit 25 ligado (bloco `%51`) para de ler cores e passa a ler um
`GradientCubicColor` — **quatro `half4`, 32 bytes por registro** — e avalia por
Horner:

```
c(f) = c0 + f*(c1 + f*(c2 + f*c3))
```

`[BIN]` E o tamanho bate do lado da CPU: `Gradient::set_gradient_color`
`0x9A8D4`–`0x9A8EC` escolhe **16 halves por registro** exatamente quando o código
é 4 (`mov w10, #0x10 ; csel x20, x8, x10, ne`), contra 4 ou 5 nos outros casos.

O doc 03 §23 tinha lido o bit 25 como *"um caminho alternativo de amostragem no
uniforme"* e parado aí. Ele não amostra a mesma rampa de outro jeito: **ele troca
o que a rampa é**.

## 5. `smooth_color_coefficients`, e o que ela vira com duas paradas

`[BIN]` Quem preenche os quatro coeficientes é a segunda metade da lambda `$_1` de
`set_gradient_color` (`0x9B7B4`–`0x9B868`): depois de escrever as cores, ela
passa de novo pelos registros, junta as quatro cores vizinhas de cada segmento e
chama **`RB::Fill::(anonymous)::smooth_color_coefficients`**, `0x9DEB0`–`0x9DF64`.

As bordas saem do **spread** (`flags & 7`, lido do fecho em `+0x18`): sob
`reflect` ela alcança o vizinho do outro lado; sob `pad` — o nosso caso, spread 0
— ela **duplica as pontas** (`0x9B81C`, `0x9B838`).

A função, transcrita instrução a instrução:

```
d0 = p1-p0 ;  d1 = p2-p1 ;  d2 = p3-p2
m1 = (d0+d1)/2                                        0x9DECC   (0.5 = 0x3F000000)
m2 = (d1+d2)/2                                        0x9DED8
m1 = 0  onde sinal(d0) != sinal(d1)                   0x9DEE0-0x9DEEC
m1 = clamp(m1, ±3·d0) depois clamp(m1, ±3·d1)         0x9DEF0-0x9DF08
m2 = 0 onde sinal(d1) != sinal(d2), clamps iguais     0x9DF0C-0x9DF28
b0 = p1 + m1/3 ;  b1 = p2 - m2/3                      0x9DF2C-0x9DF40  (1/3 = 0x3EAAAAAB)
c0 = p1
c1 = m1
c2 = 3·(p1 - 2·b0 + b1)
c3 = d1 + 3·(b0 - b1)
```

É um **Hermite cúbico monótono de Fritsch–Carlson**, escrito na base de potências
via os pontos de controle de Bézier.

**E é aqui que ele vira um número.** Com **duas** paradas e spread `pad`, o
chamador duplicou as pontas, então `d0 = 0` e `d2 = 0`. O clamp a `±3·d0` força
`m1 = 0`; o clamp a `±3·d2` força `m2 = 0`. Sobra

```
c0 = p1    c1 = 0    c2 = 3·(p2-p1)    c3 = -2·(p2-p1)

c(f) = p1 + (p2 - p1) · (3f² - 2f³)
```

— **smoothstep**. O gradiente do alvo sai devagar das duas cores de ponta. Uma
mistura reta do lado é a rampa dura.

`[ART]` Não é um caso de laboratório: **48 de 48** `linear-gradient` do corpus têm
exatamente duas paradas (doc 03 §23.4), as duas rampas canônicas do `system-*`
têm duas (`SystemFill.h`) e o `automatic-gradient` devolve duas
(`AutomaticGradient.h`). Toda rampa que este renderizador constrói para um fill de
documento cai no caso degenerado. O `smoothstep` **é** a rampa do ícone.

### 5.1. O que foi implementado

`GradientOracle.h/.cpp` ganha `smoothColorCoefficients` (a transcrição) e
`rampSmoothAtPositions` (saturar → escalar por `n-1` → índice + fração → Horner).
`FillOverride` ganha um bit `smooth`, ligado em `fillPaint` para os quatro casos
— porque os quatro são fills de documento e passam pelo mesmo `0x1BC74` — e lido
nos dois pontos que avaliam a rampa de um override (`paintBackground` e o braço de
override do `SvgRenderer`). **Gradiente próprio de um SVG não muda**: como o
`IconRendering` entrega um gradiente de SVG ao RB não foi lido, e supor que é o
mesmo caminho seria exatamente o chute que este projeto recusa.

---

## 6. Pista 1 — a `orientation` descartada: **a medição continua valendo**

`[BIN]` Reconferido em `FillResolve.h` e no binário: o conversor de fundo
`0x10AD9C`–`0x10B080` lê `primaryColor` (`Fill+0x08`) e `secondaryColor`
(`Fill+0x30`) e **nunca toca** em `orientation` (`Fill+0x58`); os três sítios de
`automaticGradient` passam `placement: nil`. Nada do que mudou hoje encosta nisso.
Honrar o `stop.y = 0.7` do documento dele continua sendo **mais correto que o alvo
e portanto um pixel diferente**, e segue não implementado.

`[ART]` E há um argumento novo, que aperta a decisão em vez de só repeti-la. Dos
**14** fills de fundo `linear-gradient` do corpus que nomeiam uma `orientation`,
**13** nomeiam exatamente `start.y = 0 → stop.y = 0.7` — o mesmo par que o
documento do usuário traz. Um valor que aparece idêntico em 13 documentos de 13
autores diferentes não é uma decisão de design: é o **default que o editor grava**.
Honrá-lo não seria respeitar a intenção do autor, seria elevar uma constante da UI
a geometria.

## 7. Pista 2 — os seis números do `automatic-gradient`: **já estava fechada, e não é o caso dele**

O `[OBS]` do doc 03 perto da linha 1847 está **vencido**: o §24 do mesmo documento
lê a regra inteira (`IconRendering 0x5864`), e `AutomaticGradient.h` a transcreve,
inclusive as fronteiras de banda em 0.25/0.50/0.75 como imediatos.

E não se aplica: o fundo do usuário é `linear-gradient`, não `automatic-gradient`.
O que sobrava do §23.5 era `sample_stops_binary` não lido inteiro — e o caminho do
ícone **não passa por ele**, passa por `sample_stops_uniform` (§3 acima). A
geometria do `automatic-gradient` também já não é pergunta: ele herda o eixo
vertical padrão porque passa `placement: nil`, como `SystemFill.h` registra.

## 8. Pista 3 — a lateralidade de y: **continua `[OBS]`**, com um indício

Não fechou. `[ART]` Dos mesmos 14 fundos com `orientation` explícita, **11** põem
a cor mais clara na ponta de menor `y`. É direcional e não é leitura: 11 de 14 não
é 14 de 14, e três autores fizeram o contrário. A `kGradientAxisDirectionNote`
fica como está. O renderizador continua pondo a `primaryColor` no topo, que é o
lado que esses 11 documentos concordam em querer.

## 9. As três hipóteses que dão negativo — e o negativo é o diagnóstico

Todas foram medidas, e todas estão **desligadas** para o ícone. Vale dizer, porque
cada uma é uma suavização que alguém razoável tentaria a olho.

**Oklab.** `[BIN]` `RenderBox` tem a matriz LMS do Oklab em `0x1625D8`–`0x1625FC`
(`0.4122214615, 0.5363325477, 0.05144599453 / 0.2119034976, 0.6806995273,
0.1073969603 / 0.08830246329, 0.2817188501, 0.6299787164`), tem
`Fill::Color::convert_to_oklab 0x12D030` e `convert_from_oklab 0x12D148`, e o
shader `Gradient::color_out` carrega a **inversa** dela aplicada ao **cubo** da
cor (as nove `half` do `default_mod8.ll` batem com `4.0767416621, -3.3077115913,
0.2309699292 / -1.2684380046, 2.6097574011, -0.3413193965 / -0.0041960863,
-0.7034186147, 1.7076147010` em uma ulp de `half`). O gradiente **pode** ser
interpolado em espaço perceptual.

Só que o gatilho são os **bits 6–7 dos flags**: `set_fill_state 0x9BA0C`–`0x9BA38`
liga o bit 18 da palavra 0 só quando esse campo vale 3, e `set_gradient_color
0x9A58C`–`0x9A5B0` liga o `convert_to_oklab` da CPU no mesmo campo. `0x400` tem
os bits 6–7 **zerados**. Os dois lados ficam desligados, e `color_out` devolve a
entrada intacta — a guarda de saída é `(w0 & 0x50000) == 0`, os bits 16 e 18.

**A tabela de transferência.** `[BIN]` O mesmo `color_out` tem uma LUT de **4096
`half`**, indexada pelo padrão de bits do `half` deslocado de 3 com o sinal
preservado, escolhida entre duas por um bit da palavra 2. Ela é o bit 16, que vem
do **mesmo** campo de bits 6–7. Também desligada.

**Dithering.** `[BIN]` Existe um `dither` no motor — mas como *function constant*
do shader de **imagem** (`default_mod2.ll`, metadado `!32`, ao lado de
`has_tone_map` e `has_tint`), e de nenhum shader de gradiente. Então a hipótese
barata do briefing tem resposta: **o alvo não dithera o gradiente**. O banding de
8 bits que sobra é real e é do alvo também.

E duas suavizações que existem e que o ícone não pede: o **midpoint** (código 1 →
`powr(t, log(0.5)/log(midpoint))`, `interpolation_factor 0x9A1CC`–`0x9A204`) e a
**bézier por stop** (código 2, quatro bytes escalados por 3/255, `0x9A208`). Um
easing escolhido no olho teria imitado um desses dois e estaria errado pelo motivo
certo.

## 10. O que sobra aberto

- `[OBS]` **A rampa roda em cor PREMULTIPLICADA no alvo** (`0x9B710` multiplica rgb
  por alpha antes da passagem de coeficientes) e os coeficientes são guardados em
  `half`. Aqui ela roda em cor reta e em `float`. Toda cor de fill medida tem alpha
  1.0, onde os dois coincidem exatamente; abaixo de 1.0 não coincidem, e não há
  leitura que decida.
- `[OBS]` **Paradas não uniformes.** A rampa tipo 3 não guarda posição: o índice é
  `int(t·escala)`. Uma rampa com espaçamento irregular liga o bit 15 e cai em
  outro tipo, que não foi lido. `rampSmoothAtPositions` volta para a leitura
  linear nesse caso, com gate próprio, em vez de fingir que o cúbico se aplica.
- `[OBS]` **O gradiente próprio de um SVG.** Não mudou, e a razão é que não foi
  lido como o `IconRendering` o entrega ao RB.
- `[OBS]` **`RB::ColorSpace`.** `rb_color_space(3)` (`0x8B704`, tabela de bytes em
  `0x16CC89`) devolve `0x12` — um byte com dois nibbles que parecem *gamut* e
  *transferência*. O que `0x12` **nomeia** não foi lido. O que ficou apertado é
  outra coisa, e é útil: o `3` é **literal nos dois braços** do desenho, sólido e
  gradiente, então **o espaço declarado no documento não chega a essa chamada**.
  `IconRendering` não tem nenhuma das strings `srgb` / `display-p3` — elas vivem no
  `IconComposerFoundation` (`0x127204`, na lista `named, extended-srgb, srgb,
  display-p3, gray, extended-gray`), que importa
  `CGColorCreateCopyByMatchingToColorSpace`. Se a conversão acontece, acontece lá,
  e é lá que a próxima frente deve procurar. `kBackgroundP3Note` continua viva e a
  matriz **não foi inventada**.

---

## 11. A prova

Release, `icrender <bundle> --out <png> --size 1024 --idiom square`, delta máximo
de canal **entre pixels visíveis**, antes contra depois:

| caso | camadas | mudados (dos visíveis) | delta máx | média sobre os mudados | tempo antes → depois |
|---|---|---|---|---|---|
| `GoWToolkit.icon` (o dele) | 1 de 1 | **91,56 %** | 11 | 6,06 | 0,266 s → 0,289 s |
| `alienator88__Viz__Viz` | 0 de 1 | **96,38 %** | 8 | 5,32 | 0,029 s → 0,044 s |
| `CamilleScholtz__swmpc__swmpc` | 1 de 1 | **53,45 %** | 4 | 2,46 | 0,370 s → 0,410 s |
| `Apollo-Reborn__…__AppIcon` | 10 de 10 | **54,73 %** | 8 | 4,58 | 2,397 s → 2,503 s |

O `Viz` desenha **0 de 1** camadas — a arte dele é `.heic` e este renderizador não
a lê. Está na tabela de propósito: é o fundo **sozinho**, a medição mais limpa do
gradiente que existe no corpus, e dizer que ele desenhou uma camada seria o verde
vazio contra o qual o briefing avisa.

O delta máximo de 11 níveis não é pouco disfarçado de pouco: é exatamente o que a
curva prevê. `max |smoothstep(t) − t| = 0,0962` em `t = 0,2113`, e o canal verde
do fundo dele percorre 172 → 66, ou seja `0,0962 × 106 = 10,2` níveis. O que muda
não é a amplitude, é a **forma** — e ela muda em 92 % dos pixels visíveis.

**Verificação independente da curva.** Numa coluna do `Viz`, o render de depois
contra a forma fechada `p0 + (p1−p0)(3t²−2t³)` avaliada nos extremos do próprio
PNG:

```
   y       t      antes    depois   previsto
   0    0,0005      255      255      255
 108    0,1060      246      252      252
 216    0,2114      238      246      246
 324    0,3169      230      236      236
 512    0,5005      215      215      215      ← smoothstep e a reta concordam
 700    0,6841      200      194      194
 808    0,7896      192      184      184
 916    0,8950      183      177      177
1023    0,9995      175      175      175
```

Nove de nove. As pontas e o meio ficam presos, que é a assinatura do `smoothstep`
e não de um easing qualquer.

**Tempo.** O ícone dele a 1024 px vai de **0,266 s a 0,289 s** — dois `fmuladd` e
duas comparações por pixel de fundo. O Apollo vai de 2,397 s a 2,503 s. Os tetos
de `Tests/test_time_budget.cpp` passam.

**Suíte.** `630 case(s), 0 failure(s)` em 59,6 s (Debug, `IC_CORPUS_DIR` apontado
para a árvore principal). Três casos novos em `Tests/test_rb_gradient.cpp`, e os
três foram vistos correndo pelo nome — o `smoothstep` contra a forma fechada, o
clamp monótono contra um pico que um Catmull-Rom sem guarda estouraria, e o
fallback linear da rampa não uniforme.
