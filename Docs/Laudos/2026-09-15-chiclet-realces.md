# Laudo — o chiclet ganha os próprios realces

*2026-09-15. Alvo: o `[OBS] 6` que o laudo dos realces deixou aberto há poucas
horas — «**O chiclet** — números lidos, nada os consome; o `hasSpecular` desta
cadeia é o da camada.»*

**Consumidos.** O fundo do ícone — a pastilha — deixa de ser uma rampa chapada.
`[ART]` Nos três ícones medidos, entre **8,7 % e 10,8 % dos pixels visíveis**
mudam, com delta máximo de canal de 168 a 177.

E a resposta é maior do que a pergunta em dois pontos:

1. **A escrita de `fill[+0x5B]` foi achada**, e ela é exatamente o que os dois
   limiares vizinhos prometiam: uma **classe de luminância** do próprio fill,
   medida como **leveza HSL** parada a parada.
2. **E ela não move um pixel nesta versão.** `chicletDefault`, `chicletBright` e
   `chicletDim` são montados dos **mesmos valores**, membro a membro — o que
   corrige, com `[BIN]`, a segunda metade do `[OBS] 4` do laudo dos realces
   («inertes em 2.0-125 do lado do glifo, **não** do lado do chiclet»).

Todos os endereços são do slice
`References/2.0-125/out/slices/IconRendering.arm64` (VA == deslocamento de
arquivo). A reflexão é `References/2.0-125/out/fieldmd_iconrendering.txt`.

---

## 1. Onde a cadeia do chiclet é aplicada, e qual é o portão dela

O glifo passa por `0x000491C0`–`0x00049DBC`, com o portão
`material.hasSpecular` em `0x00049200`. O chiclet **não tem `hasSpecular` de
camada**, e por isso não podia estar naquela função. `[BIN]` Ele tem a sua:

```
0x000475A0-0x00047AE8   (1352 bytes)
  0x000475C8  ldrb w8, [x20, #0x21]
  0x000475CC  cmp  w8, #1
  0x000475D0  b.ne #0x47a5c        ; -> epilogo. FALSO NAO DESENHA NADA.
  0x000475D8  add  x0, x20, #0x408 ; o FILL
  0x000475DC  add  x20, x20, #0x68 ; o contexto
  0x000475E0  bl   #0x5e590        ; o resolvedor do CHICLET
```

`[BIN]` `0x0005E590` é um *thunk* de três instruções — `adrp x1, #0x62000 ;
add x1, x1, #0x588 ; b #0x5e59c` — que põe `x1 = 0x00062588` (o *closure* do
chiclet) e cai no **mesmo corpo `0x0005E59C`** que o glifo usa por
`0x0005E194` (`x1 = 0x000627B4`). Um corpo, dois seletores. É por isso que os
dois lados compartilham o expansor (`0x00030E88`), o resolvedor (`0x0004BD90`) e
o agrupador (`0x0004C314`) — e é por isso que `resolveHighlight`, escrito pela
frente dos realces para o glifo, serve ao chiclet sem uma linha de mudança.

`[BIN]` `0x0005E590` é chamado de **um sítio só**, `0x000475E0`, contra três de
`0x0005E194` (`0x0001C884`, `0x00048548`, `0x000492D8`).

`[OBS]` **O que é o byte `ctx+0x21` não foi lido.** Ele é o `hasSpecular` desta
cadeia e não tem nome no metadado que abriu aqui. Este renderizador desenha os
realces do chiclet sempre que o fundo é pintado — o único estado em que há
pastilha — e diz isso em `out.notes`.

---

## 2. Qual conjunto o caminho normal escolhe

### 2.1. O primeiro teste não é sobre o fill: é sobre o parâmetro

`[BIN]` `0x00062588`–`0x000627B0`:

```
0x000625B4  ldrb w8, [x20, #0x110]   ; chicletHighlightsAppearanceMode
0x000625B8  cmp  w8, #1
0x000625BC  b.ne #0x62698            ; -> ramo systemAppearance
```

`[BIN]` E o metadado diz o que esse enum é. `fieldmd 0xA4FB0`,
`ICRRenderingParameters.Highlights.ChicletHighlightsAppearanceMode`, **dois
casos**:

```
0xa4fb0  [enum] ...ChicletHighlightsAppearanceMode  (2 fields)
      systemAppearance
      chicletLuminance
```

`[BIN]` O padrão é `1` (`strb w22, [x19, #0x110]` com `w22 == 1`, `0x00062B34`),
e `1` é o **segundo** caso: `chicletLuminance`. **O caminho normal é o de
luminância.** A pista que o *briefing* apontou — dois limiares de luminância ao
lado de conjuntos chamados `Dim` e `Bright` — está **confirmada**, e não por
analogia: pelo nome do caso do enum.

Os dois ramos, inteiros:

```
; modo == 1  (chicletLuminance)          0x000625C0-0x000625E8
if (fill[+0x90] == 1 && (fill[+0x68]|+0x70|+0x78|+0x80|+0x88) == 0) {
    switch (fill[+0x5B]) {                            ; 0x00062744
        0 -> chicletDefault (+0x118)                  ; 0x00062760
        1 -> chicletBright  (+0x748)                  ; 0x00062754
        _ -> chicletDim     (+0xD78) }                ; 0x000626A4
} else {
    r = cmp(0x00040E60 de duas formas)                ; 0x00062674 / 0x00062680
    ambas 1            -> chicletScreened (+0x19D8)   ; 0x0006272C
    exatamente uma 1   -> chicletClear    (+0x13A8)   ; 0x00062738
    nenhuma            -> 0x0006D6B0 decide           ; 0x00062724
}

; modo != 1  (systemAppearance)          0x00062698-0x000626AC
fill[+0x61] == 1 -> chicletDim ; senao chicletDefault
```

`[BIN]` E o seletor termina chamando o **mesmo expansor do glifo**, com os dois
campos de curvatura **do chiclet** e um booleano a menos:

```
0x00062770-0x00062794
  sp+0x2C0 <- Highlights+0x50   (chicletHighlightCurvature)
  sp+0x180 <- Highlights+0x70   (chicletDarklightCurvature)
  strb wzr, [sp, #0x1A0]        ; o terceiro argumento e FALSO aqui
  bl #0x30e88
```

`[BIN]` Os dois são `[0.75]×4` (`fmov v0.2d, #0.75`, `0x00062ABC`, gravado
quatro vezes em `0x00062ADC`–`0x00062AE0`) — os mesmos números do glifo. O
booleano falso é onde `glyphHighlightsUseVCM` (`true`, `Highlights+0x90`) **não
vale** para o chiclet. `[OBS]` Como nenhum dos dois `VCM` foi seguido até um
pixel, essa diferença fica nomeada e não desenhada.

### 2.2. `fill[+0x5B]` é a classe de luminância — a escrita, achada

O laudo dos realces registrou: «`[OBS]` O que `fill[+0x5B]` **É** não foi lido …
mas a escrita do byte não foi achada.» Foi achada.

`[BIN]` `0x0001A8C0` e `0x0001A920`–`0x0001A97C`, dentro de
`0x00019F4C`–`0x0001A998`. `x20` é o objeto de parâmetros e o *payload* dele
começa em `+0x10` (`add x0, x20, #0x10`, `0x0001A888`), então `+0xA8`,
`+0xB0` e `+0xB8` **são** `Highlights+0x98`, `+0xA0` e `+0xA8`:

```
0x0001A920  ldp  d10, d11, [x20, #0xa8]  ; maxDim = 0.2 , minBright = 0.99
0x0001A924  ldrb w21,      [x20, #0xb8]  ; iconBrightnessOnlyUsesMax = false
0x0001A944  bl   #0x1f10c                ; -> (d0 = MIN, d1 = MAX)
0x0001A958  fcmp d11, d9                 ; minBright vs MAX
0x0001A95C  b.pl #0x1a968
0x0001A960  mov  w8, #1                  ; BRIGHT
0x0001A968  cmp  w21, #0
0x0001A96C  fcsel d0, d9, d8, ne         ; onlyUsesMax ? MAX : MIN
0x0001A970  fcmp d0, d10                 ; vs maxDim
0x0001A974  b.pl #0x1a8c0                ; -> mov w8, #0  DEFAULT
0x0001A978  mov  w8, #2                  ; DIM
0x0001A8C4  strb w8, [x19, #0x5b]
```

e o **mesmo** teste de "fill simples" do seletor guarda a entrada
(`0x0001A89C`–`0x0001A8B4`: `cmp w27, #1` e o `orr` dos cinco ponteiros contra
zero). Um fill que não é simples nunca chega a classificar e o byte fica `0`.

> **O `b.pl` é o detalhe que decide o empate.** `pl` é `N == 0`, isto é `>= 0`,
> então `minBright >= MAX` **não** é `Bright` e `sonda >= maxDim` **não** é
> `Dim`. Os dois empates caem em `Default`.

`[BIN]` `0x0001F10C`–`0x0001F32C` é a faixa de luminância de um fill, e ela é
uma conta só, por **parada** de gradiente, com passo `0x28`:

```
0x0001F1EC-0x0001F208   hi = max(r,g,b) ;  lo = min(r,g,b)
0x0001F220-0x0001F224   L  = (hi + lo) * 0.5          <- LEVEZA HSL
0x0001F278-0x0001F2C0   devolve (min L, max L) sobre todas as paradas
0x0001F2CC-0x0001F2D4   fcmp min, max ; b.ls -- senao brk (um ClosedRange)
0x0001F2F8-0x0001F2FC   sem paradas: (0.0, 1.0)
```

> **Não é `max(r,g,b)` e não é luma.** Um vermelho puro tem `hi = 1` e `lo = 0`,
> logo `L = 0.5`: um leitor que usasse o máximo sozinho o chamaria de `Bright`
> e trocaria o conjunto de todo ícone saturado. O teste desta frente trava isso.

**E este é o único consumidor dos dois limiares.** Varredura do `__text` inteiro
atrás de `ldr d, [x, #0x98]` e `ldr d, [x, #0xA0]`: quatro e treze ocorrências,
e as únicas que leem as **duas** juntas são dois construtores de cópia
(`0x00070F34` e `0x00081290`), que copiam o bloco campo a campo e não decidem
nada.

### 2.3. E a classe não move pixel nenhum em 2.0-125

`[BIN]` O construtor `0x00062A78`–`0x00063C1C` guarda cada constante **uma vez**
em `sp+0x00`..`sp+0xD0` e depois só as recopia. Os trinta `memcpy` de `0x101`
bytes contam a história:

| conjunto | os seis `memcpy` |
|---|---|
| `chicletDefault` | `0x62BC8` `0x62C64` `0x62D08` `0x62DBC` `0x62E58` `0x62ED0` |
| `chicletBright` | `0x62F50` `0x62FC8` `0x63074` `0x63144` `0x631D0` `0x63238` |
| `chicletDim` | `0x632C8` `0x63350` `0x63424` `0x634FC` `0x6358C` `0x63608` |

e os **dezoito** membros desses três conjuntos leem exclusivamente fatias
gravadas antes de `0x00062F00`. **A primeira constante nova do construtor
aparece em `0x00063624`** (`ldr q0, [x8, #0x740]` == `{1.0, 1.25}` e
`fmov v1.2d, #1.25`) — depois do sexto `memcpy` do `Dim` em `0x00063608`, e
portanto já dentro de `chicletClear`.

Conferido também membro a membro nos dois primeiros conjuntos: `Bright.keySharp`
lê `sp+0xA0` / `sp+0xD0` / `sp+0xC0` / `sp+0x90` exatamente como
`Default.keySharp`; `Bright.keyDiffuse` lê `sp+0x50` / `sp+0x60` / `sp+0x80` /
`sp+0x70`; e assim pelos seis.

> **Então a regra de luminância é medida e implementada, e o pixel que ela
> escolhe é o mesmo nos três casos.** Isto corrige o `[OBS] 4` do laudo dos
> realces, que dizia que os índices de aparência eram inertes «do lado do glifo,
> **não** do lado do chiclet». São inertes dos dois lados — por motivos
> diferentes (lá os cinco saem da mesma fábrica `0x00064604`; aqui os três saem
> das mesmas constantes de pilha), e com a mesma consequência.
>
> Este renderizador a implementa mesmo assim, pelo mesmo motivo que
> `highlightSizeValue` implementa a inversão de tamanho que também não se vê
> hoje: o dia em que um arquivo de parâmetros diferenciar os conjuntos, a regra
> já está certa. E a classe medida entra na nota de cada render, porque medir é
> informação mesmo quando não é pixel.

`[OBS]` `chicletClear` (`+0x13A8`) e `chicletScreened` (`+0x19D8`) **não** estão
transcritos. Eles só são alcançados pelo ramo de fill não-simples, e quem escolhe
entre os dois são `0x00040E60` e `0x0006D6B0`, duas comparações de forma que esta
frente não seguiu. Ficam nomeados na nota de cada render.

---

## 3. Os seis membros de `chicletDefault`, relidos

`[BIN]` `0x00062AB8`–`0x00062EDC`, campo a campo contra a régua de
`HighlightSettings` (`0x101` bytes, dez campos) que `GlassSpecular.h` já carrega:

| | `keySharp` | `keyDiffuse` | `fillSharp` | `fillDiffuse` | `dark` | `rim` |
|---|---|---|---|---|---|---|
| destino | `+0x000` | `+0x108` | `+0x210` | `+0x318` | `+0x420` | `+0x528` |
| `brightness` | 1.1 | 1.1 | 1.1 | 1.1 | **0.0** | 1.0 |
| `opacity` | 0.2 | 0.5 | 0.2 | 0.25 | 0.2 | **0.0** |
| `distance` | 10 | 40 | 10 | 40 | 10 | 10 |
| `spread` | 2π/3 | π/2 | 2π/3 | π/2 | π/3 | π |
| `bias` | 0.5 | 0.08 | 0.5 | 0.08 | 0.5 | **1.0** |

`outsetOpacity` e `minInsetPixels` são `nil` **nos seis** (byte de tag `1` em
`+0x48` e `+0xD0`), `inset` e `minDistancePixels` são zero nos seis, e
`blendModeOverride` é o sentinela `18` nos seis. Os valores do *pool*:
`0x098710 = {1.1, 0.2}`, `0x098720 = {1.1, 0.5}`, `0x098730 = {1.1, 0.25}`,
`0x0984F0 = {0.0, 0.2}`.

**Duas correções ao §4.3 do laudo dos realces**, as duas `[BIN]`:

1. O `bias` do `rim` é **1.0**, não `0.5`. `x27` é reatribuído a
   `0x3FF0000000000000` em `0x00062E68`, **entre** o `dark` e o `rim`, e é esse
   `x27` que vai para `+0xF8` em `0x00062EC0`. Não move pixel — a opacidade do
   `rim` é zero — mas é o tipo de número que um laudo não pode errar.
2. Os quatro slots de cada `SizeBasedValue` são **iguais** nos seis membros do
   chiclet. No glifo eles diferem (`distance` 4 em `display` e 6 nas outras,
   `opacity` 0.3 só em `small`), e foi exatamente isso que fez a armadilha de
   `slots[3 − sizeClass]` aparecer no pixel lá. Aqui ela continua aplicada e
   continua invisível.

**E todos os seis existem**, que é a diferença que mais muda a figura. No glifo,
`fillDiffuse` e `rim` são `nil` (`0x00033FA4` escreve 20, `0x00033DE4` escreve
19) e sobram **cinco** realces. No chiclet os seis estão presentes, o expansor
faz **sete** posições (o `dark` duas vezes, a ±π/2), e o `rim` — presente, com
`opacity == 0` — é o único que não pinta. Ficam **seis realces vivos**.

> Presente-com-opacidade-zero e ausente somem igual no pixel e são coisas
> diferentes no laudo. O teste desta frente checa as duas, porque um leitor que
> descartasse o `rim` como `nil` acertaria o pixel e erraria a leitura.

---

## 4. O que este renderizador NÃO transcreve: o rasterizador

`[BIN]` O glifo termina no shader Metal `glassHighlight` (`0x0000E834` monta por
nome). **O chiclet não.** `0x000475A0` chama `0x0000D904` (3888 bytes), que
desenha no `RBDisplayList`:

```
0x0000E0F0  beginLayerWithFlags:
0x0000E134  clipLayerWithAlpha:mode:
0x0000E448  setConicGradientCenter:angle:stopCount:colors:colorSpace:locations:flags:
0x0000E490  drawShape:fill:alpha:blendMode:
0x0000E500  drawLayerWithAlpha:blendMode:
```

isto é: uma **faixa recortada**, preenchida por um **gradiente cônico** em torno
do centro. A forma da pastilha é analítica, então o alvo não precisa de campo de
distância: o cônico dá o termo **angular** e a camada recortada dá o **radial**.

`[INF]` **Este renderizador não transcreve esse caminho.** Ele resolve os seis
realces pelo **mesmo `0x0004BD90`** que o alvo usa nos dois lados — e que
`resolveHighlight` já é — e depois os avalia com o corpo de
`glassHighlightFragment` sobre o campo de distância do próprio contínuo do
chiclet. **Os números são `[BIN]`; a máquina que os converte em cobertura é
`[INF]`**, e o que ela pode errar é a forma exata da queda angular perto dos
cantos, onde a normal do contorno e o ângulo polar do centro deixam de coincidir.

`[OBS]` As paradas do cônico (`0x0000E1AC`–`0x0000E330`, passo `0x28`, quatro
`Double` de cor mais a posição) **não foram lidas**, nem `0x000126EC` (1172
bytes), que monta a forma recortada.

E um detalhe do recorte que **é** decisão deste projeto: o realce é multiplicado
pelo **alfa que o fundo já tem**, e não pela cobertura do contorno. O alfa já
carrega a cobertura (`clipToChiclet` a multiplicou nele), e uma pastilha
transparente — o que `automatic` sob `tinted` produz, que é `IconColor.clear` —
não tem superfície para acender. Sem isso, o realce poria luz sobre o nada, e o
caso `a_tinted_automatic_background_is_transparent_and_not_a_grey` é
exatamente quem cobra isso.

---

## 5. O que foi entregue em código

- **`Source/RenderBox/ChicletHighlights.h` / `.cpp`** (arquivo novo). O laudo
  inteiro em cima; depois `ChicletAppearance`, `chicletFillLuminance`
  (`0x0001F10C`), `classifyChicletAppearance` (`0x0001A920`),
  `chicletHighlightSlots` (as sete posições do §3) e `drawChicletHighlights`.
- **`Source/RenderBox/IconRenderer.cpp`**, **um ponto só**: logo depois de
  `clipToChiclet`, a classe de aparência sai da luminância do próprio `paint` e
  os realces entram sobre o fundo já recortado.
- **`Tests/test_chiclet_highlights.cpp`** (arquivo novo), seis casos que travam
  seis erros que não dariam exceção: tomar os números do glifo; dar ao chiclet os
  quatro slots diferentes do glifo; descartar o `rim` como `nil` ou fazê-lo
  pintar; desenhar o `dark` uma vez só; ler a classe de luminância ao contrário
  (com o empate nos dois limiares e o vermelho puro); e acender uma pastilha
  transparente.
- **`Tests/test_automatic_fill.cpp`**, três amostras de rampa movidas quatro
  linhas para dentro, com o porquê escrito: as linhas de fora agora estão acesas
  de propósito, e o que aqueles casos fixam é **qual rampa**, não o que a luz da
  pastilha faz por cima dela.

### A prova em pixel, e o tempo

Release (`cmake --preset release -DIC_BUILD_UI=OFF`, `--target icrender`),
1024 px, `--idiom square`, antes e depois. «Mudados» conta pixels distintos;
o delta máximo é medido **só entre pixels visíveis** (alfa > 0 num dos dois).

| documento | mudados | % dos visíveis | delta máx. | antes | depois |
|---|---|---|---|---|---|
| `GoWToolkit.icon` (do usuário) | 106 912 | **10,83 %** | 168 | 0,291 s | **0,390 s** |
| `Apollo-Reborn__Apollo-Reborn__AppIcon` | 86 013 | **8,71 %** | 171 | 2,410 s | **2,603 s** |
| `Aeastr__GlowGetter__icon` | 96 445 | **9,72 %** | 177 | 0,729 s | **0,836 s** |

O custo é **um campo de distância de 1024² sobre o contorno do chiclet** (um
único `generateFieldFromContours`, a mesma porta que a arte vetorial já usa
desde o campo de distância desta manhã) mais seis varreduras com saída precoce
fora da banda. Entre +0,10 s e +0,19 s por render, e nenhum teto de
`Tests/test_time_budget.cpp` foi tocado.

Visualmente: a pastilha ganha um aro claro no topo, uma lavagem difusa que desce
dele, um aro claro no rodapé (o `fillSharp`/`fillDiffuse` a π) e as duas laterais
escurecidas pelo `dark` espelhado. É a pastilha lendo como vidro, que é o que o
usuário apontou que faltava.

**Suíte:** 633 casos, 0 falhas (`IC_CORPUS_DIR` posta, `IC_BUILD_UI=OFF`;
627 antes desta frente, mais os seis casos novos), 58,6 s.

---

## 6. O que continua `[OBS]`

1. `[OBS]` **O byte `ctx+0x21`** (`0x000475C8`), o portão real desta cadeia. Sem
   nome no metadado. Aqui os realces saem sempre que há pastilha.
2. `[OBS]` **`chicletClear` e `chicletScreened`** (`+0x13A8`, `+0x19D8`) não
   transcritos, e as duas comparações de forma que escolhem entre eles
   (`0x00040E60`, `0x0006D6B0`) não seguidas. Um fill não-simples cai em
   `Default` aqui, que é o que `0x0001A8C0` escreve, mas o alvo iria para um dos
   outros dois conjuntos.
3. `[OBS]` **O rasterizador do §4**: `0x0000D904` (gradiente cônico + camada
   recortada) e `0x000126EC` (a forma da faixa). O que este renderizador põe no
   lugar é `[INF]`.
4. `[OBS]` **O terceiro argumento falso** de `0x00062788` — o `useVCM` que o
   chiclet não tem e o glifo tem. Os dois `VCM` do preâmbulo continuam sem
   consumidor lido, como o `[OBS] 7` do laudo dos realces já dizia.
5. `[OBS]` Tudo que o realce do glifo já carrega e que vale igual aqui, porque é
   o mesmo resolvedor: **`ctx[0]`** (`0x0004C010`), a **latitude `phi`**, e a
   pós-passagem espacial **`0x00012550`**.
6. `[OBS]` **Quem escreve `fill[+0x61]`**, o índice do ramo `systemAppearance`.
   `0x00019DF4` o copia de `[x22+?]`; a origem não foi seguida. Inalcançável
   nesta versão de qualquer forma, já que o modo padrão é `chicletLuminance`.

---

## 7. Resumo dos selos

| pergunta do pedido | resposta | selo |
|---|---|---|
| 1. qual conjunto o caminho normal escolhe | `chicletHighlightsAppearanceMode` é `1` por padrão (`0x00062B34`) e o enum de dois casos (`fieldmd 0xA4FB0`) diz que `1` é **`chicletLuminance`**; com fill simples, `fill[+0x5B]` escolhe `Default(0)/Bright(1)/Dim(≥2)` em `0x00062744` | `[BIN]` |
| — | `fill[+0x5B]` **é** a classe de luminância, escrita em `0x0001A8C4`: `MAX > 0.99 → Bright`; senão `(onlyUsesMax ? MAX : MIN) < 0.2 → Dim`; senão `Default`; empate vai para `Default` (`b.pl`) | `[BIN]` |
| — | a luminância é **leveza HSL** `(max+min)/2` por parada, faixa `(min, max)` sobre as paradas, `(0,1)` sem paradas — `0x0001F10C` | `[BIN]` |
| — | os dois limiares não são lidos em nenhum outro sítio do slice (varredura completa do `__text`) | `[BIN]` |
| — | **e a escolha não move pixel**: `Default`, `Bright` e `Dim` saem das mesmas constantes; a primeira nova só aparece em `0x00063624`, já em `chicletClear` | `[BIN]` |
| 2. onde a cadeia é aplicada | `0x000475A0`, portão `ctx+0x21 == 1` em `0x000475C8`; `0x0005E590` é o *thunk* que instala o *closure* `0x00062588` e reusa o corpo `0x0005E59C` do glifo | `[BIN]` |
| — | o portão em si | não lido | `[OBS]` |
| 3. desenhou? | **sim.** Seis realces vivos de `chicletDefault`; 8,71 % a 10,83 % dos pixels visíveis em três ícones, delta máx. 168–177 | `[BIN]`/`[ART]` |
| — | o rasterizador do alvo é um gradiente cônico numa camada recortada, e não o shader `glassHighlight`; o que entra no lugar | `[INF]` |
| — | falta: `ctx+0x21`, `Clear`/`Screened`, `0x0000D904`, o `useVCM`, `ctx[0]`, `phi`, `0x12550` | `[OBS]` |

### Instrumentos

**Nenhum MCP de RE foi usado — nem estava disponível** (`list_instances` vazio),
pelo quarto laudo seguido. Tudo saiu de `scripts/macho.py` (`fn`, `dis`,
`xref`), de uma varredura própria do `__text` atrás de pares `ADRP`+`ADD`
(que é como os dois *closures* foram localizados — eles não têm `BL` nenhum) e
de outra atrás dos imediatos de `STRB`/`LDR` em deslocamentos específicos, de
`References/2.0-125/out/fieldmd_iconrendering.txt`, e de leituras diretas do
*constant pool* no arquivo do slice (VA == deslocamento): `0x0984F0`, `0x0986E0`,
`0x098710`–`0x098740`.
