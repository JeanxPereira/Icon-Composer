# Laudo — o VCM é o BT.709, e o grampo barato não era do realce

*2026-09-15. Alvo: o `[OBS]` 1 e o `[OBS]` 3 do laudo da intensidade —
a cor do realce (`glyphHighlightsUseVCM`, a transformada-base de `0xE2960` em
`__common`) e o `shouldClampPlusLBlending` que ninguém tinha lido. O pedido do
usuário continua o mesmo: **"o efeito atual tá duro demais, muito forte, falta
ajuste e polimento baseado em funções reais"**.*

**Este laudo não move um pixel, e diz por quê.** Os dois alvos abriram: o
grampo está lido inteiro — flag, shader e fórmula — e o VCM deixou de ser uma
parede e virou uma função nomeada de ponta a ponta. Mas o grampo **não é do
realce**, e a última perna do VCM é um `flag` que não foi lido. Ligar qualquer
um dos dois hoje seria mover pixel por conta de quem mede, não do alvo.

Endereços de `References/2.0-125/out/slices/IconRendering.arm64` e
`.../RenderBox.arm64` (VA == deslocamento de arquivo), reflexão em
`fieldmd_iconrendering.txt`, IR em `metallib-iconrendering/`.

---

## 1. `shouldClampPlusLBlending` é `true`, e o shader dele tem nome

`[BIN]` **O campo e o valor.** `params+0x220`, campo 21 de 35 da
`ICRRenderingParameters`. O construtor agregado escreve em `0x0005EAE8`:

```
0x0005E8BC  mov  w22, #1                  ; e w22 não é reatribuído até lá
...
0x0005EAE8  strb w22, [x19, #0x220]       ; shouldClampPlusLBlending = true
0x0005EAEC  mov  x8,  #0x70a4             ; 0x4070A3D70A3D70A4
            movk ...
0x0005EAFC  str  x8,  [x19, #0x228]       ; defaultChicletCornerRadius = 266.24
```

> **O deslocamento está ancorado de fora.** O `266.24` do `+0x228` é o número
> que o doc 03 §29.3 já tinha lido, por outro caminho, meses antes; `+0x218`
> guarda o `Int` `2` que é `refractionSupersampling`, o campo anterior; `+0x230`
> é o `thresholds` que a frente da sombra leu. Quatro campos consecutivos na
> ordem de declaração, dois deles lidos por outras frentes em outros dias.

`[BIN]` **O consumidor.** Dois sítios, a mesma forma:

```
0x0004B518  ldrb w24, [x0, #0x31]      ; o blendMode do descritor de desenho
0x0004B530  cmp  w24, #8               ; 8 == BlendMode::PlusLighter
0x0004B534  b.ne <caminho simples>
0x0004B538  ldrb w8,  [x20, #0x288]    ; params+0x220 (params mora em self+0x68)
0x0004B53C  cmp  w8,  #1
0x0004B540  b.ne <caminho simples>
0x0004B57C  bl   _objc_msgSend$setBlendShader:
```

(o outro é `0x00044608`–`0x00044654`, dentro de `0x000435A0`.)

`[BIN]` **O nome do shader não está no `__cstring`** — está soletrado por
`mov`/`movk` no corpo de `swift_once` `0x0000D848`, que monta o global de
`0xCE8D0`:

```
0x0000D88C  mov  x0, #0x6c63 ; movk 0x6d61<<16 ; movk 0x6570<<32 ; movk 0x5064<<48
0x0000D89C  mov  x1, #0x756c ; movk 0x4c73<<16 ;                ; movk 0xec00<<48
0x0000D8BC  bl   _objc_msgSend$initWithLibrary:function:
```

`0x5064_6570_6D61_6C63` + `0xEC00_0000_4C73_756C`, lidos como *small string* de
Swift (12 bytes, discriminador `0xEC`), soletram **`clampedPlusL`**. É a lição
dos imediatos outra vez, e desta vez o que ela escondia era um **nome**.

`[BIN]` **E esse nome é um entry point Metal deste mesmo bundle**,
`metallib-iconrendering/default_mod8.ll:37`, inteiro:

```llvm
%3  = fadd fast <4 x half> %1, %0
%5  = air.fmin.v3f16(splat(half 0xH3C00), %3.rgb)      ; teto = 1.0 literal
%10 = air.saturate.f16(%0.a + %1.a)
%12 = air.fmax.v4f16(%1, (%5, %10))                    ; piso = o FUNDO
```

`[BIN]` **Qual operando é qual não é inferência.** O metadado AIR nomeia os
dois: `!19 = !{i32 0, ..., !"air.arg_name", !"source"}` e `!20` o mesmo para
`!"dest"`. Então `%0` é a fonte e `%1` o fundo, e o `fmax` final ancora no
fundo.

Três coisas seguem, e a do meio é o ponto:

- o teto de rgb é o **`1.0` literal**, não o alfa de saída — grampo diferente do
  de `extendedColor` em `pdf_mode`, que trava em `[0, out.a]`;
- uma pilha de realces claros para em branco em vez de correr para 3 ou 4, que é
  exatamente o que daria de onde descer aos `plusDarker` desenhados depois;
- e o `fmax` contra o fundo faz o grampo **tirar o excesso sem tirar luz**: um
  fundo já acima de um não é puxado para baixo. Um "clamp to 1" escrito só pelo
  nome erraria essa terceira.

`[BIN]` `plusDarker` não recebe nada: o portão é `cmp w24, #8` e mais nada.

### 1.1. E ele **não** é do realce do glifo — o negativo que segurou a mão

`[BIN]` A troca acontece em **quatro** lugares: `0x00044654` e `0x00044908`
(dentro de `0x000435A0`) e `0x0004B57C` e `0x0004B7F4` (dentro de `0x0004B4EC`).

`[BIN]` **O desenho do especular do glifo não chama nenhuma das duas.** Ele é
`0x000491C0`–`0x00049DBC`; o `blendMode` que `resolveHighlight` resolve vai
direto para `-[RBDisplayList drawShape:fill:alpha:blendMode:]` em `0x0000ED00`,
o fim do construtor do `glassHighlight` (`0x0000E834`). Não há `cmp #8` nem
`setBlendShader:` nesse caminho.

`[OBS]` **Quais desenhos o grampo cobre continua por ler.** O `[x0,#0x31]` é o
blendMode de um descritor de desenho; quem monta esse descritor não foi seguido
até o fim. O que se sabe do lado do documento é só metade: `0x00025354` monta o
**nó** da camada com `addLayer:` / `setOpacity:` / `setBlendMode:` (com `w2`
vindo da tabela `CGBlendMode` de 18 entradas em `0x00094AC0`, `0x00025578`, a
mesma que o `BlendMode.h` transcreve) e não tem portão — mas o nó não é o
desenho, e o portão mora no desenho.

> **A tentação recusada.** A primeira versão desta frente ligou o grampo no
> `drawSpecular`, e ele mexeu em **91.276 pixels do Apollo com delta 118** —
> número bonito, visível, do tamanho que o pedido queria. Também mexeu em
> **zero** pixels do ícone do usuário pelo caminho do glifo, e quebrou três
> testes que dependiam do transbordo. Foi o zero que fez olhar de novo, e o que
> apareceu foi `0x000491C0` não chamando `0x0004B4EC`. **O grampo está
> transcrito e DESLIGADO**, com o campo `SpecularArguments::clampPlusLighter`
> guardado para a frente que identificar os desenhos cobertos.

---

## 2. `VCM` é **Video Color Matrix**, e é o BT.709

A frente anterior parou em `0xE2960` por ser `__common` — zerado no arquivo.
O caminho era o corpo do inicializador, e o `0x00049C78` que o laudo anterior
apontou é o **trampolim** de `swift_once`, não o corpo:

```
0x00049C78  adrp x0, 0xce000 ; add x0, x0, #0x668     ; o token
0x00049C80  adrp x1, 0x6000  ; add x1, x1, #0x948     ; O CORPO: 0x00006948
0x00049C88  bl   swift_once
```

`[BIN]` E `0x00006948` não calcula nada — ele **copia** do `__const`:

```
0x00006948  ldr q2, [0x938d0]
0x00006950  add x8, x8, #0x960                ; x8 = 0xE2960
0x00006968  stp q0, q1, [x8]                  ; <- 0x938e0, 0x938f0
0x0000697C  str q0, [x8, #0x20]               ; <- 0x93900
0x00006980  stp q1, q2, [x8, #0x30]           ; <- 0x93910, 0x938d0
```

Vinte `float`. Lidos como **`CAColorMatrix`** — quatro linhas de cinco, a última
coluna o viés — eles são, campo a campo:

| | R | G | B | A | viés |
|---|---|---|---|---|---|
| **Y** | `+0.2126` | `+0.7152` | `+0.0722` | 0 | 0 |
| **Cb** | `−0.1146` | `−0.3854` | `+0.5000` | 0 | `+0.5` |
| **Cr** | `+0.5000` | `−0.4542` | `−0.0458` | 0 | `+0.5` |
| **A** | 0 | 0 | 0 | `+1` | 0 |

**É a matriz RGB→YCbCr do BT.709, faixa cheia, com Cb e Cr enviesados em 0.5.**
Os três coeficientes de luma são os do padrão, e não uma escolha de ninguém.

`[BIN]` E a inversa está ao lado: `0xE2910`, corpo de `swift_once` `0x00006988`,
do `__const 0x93920`, lida do mesmo jeito:

| | Y | Cb | Cr | A | viés |
|---|---|---|---|---|---|
| **R** | `+1` | 0 | `+1.5748` | 0 | `−0.78740` |
| **G** | `+1` | `−0.1873` | `−0.4681` | 0 | `+0.32770` |
| **B** | `+1` | `+1.8556` | 0 | 0 | `−0.92780` |
| **A** | 0 | 0 | 0 | `+1` | 0 |

`1.5748`, `−0.1873`, `−0.4681`, `1.8556` são as constantes YCbCr→RGB do BT.709,
e cada viés é exatamente `−0.5 ×` o coeficiente de croma da linha — que é o que
"o croma chega centrado em 0.5" quer dizer. **As duas se invertem**: um ida e
volta sobre `(1,1,1)`, `(1,0,0)`, `(0.2,0.6,0.9)` e `(0,0,0)` volta ao ponto de
partida dentro da precisão de `float`.

> **`VCM` não era uma sigla opaca. É `Video Color Matrix`, literalmente**, e
> `glyphHighlightsUseVCM` quer dizer "pinte este realce pelo canal Y do vídeo".

### 2.1. O que os cinco números do `glyphHighlightVCM` fazem entre as duas

`[BIN]` Entre a ida e a volta o alvo compõe duas matrizes, e `0x00007064` é a
composição (passos de linha de `0x14` = cinco `float` nos dois operandos, o que
confirma o `CAColorMatrix` por um terceiro caminho). A cadeia, em ordem de
execução: `0xE2960` → níveis (`0x00049A64`) → croma (`0x00049B18`) → `0xE2910`
(`0x00049BAC`).

`[BIN]` **A matriz de níveis** (`sp+0x360`, escrita campo a campo em
`0x00049A64`–`0x00049AA8`) tem `Y` na primeira linha e passa o resto:

```
Y  <- (VCM[1] - VCM[0]) * Y + VCM[0]        ; ganho e viés, ambos em claro
Cb <- Cb ;  Cr <- Cr ;  A <- A
```

`[BIN]` **A matriz de croma** (`sp+0x2c0`, `0x00049B18`–`0x00049B68`) é a
surpresa, e o laudo anterior a tinha lido no canal errado. A linha do `Y` é
`[1, 0, 0, 0, 0]` — **`Y` não é tocado**. Quem é multiplicado são as duas
cromas:

```
Cb <- VCM[2] * Cb + (0.5 - 0.5 * VCM[2])
Cr <- VCM[2] * Cr + (0.5 - 0.5 * VCM[2])
```

`VCM[2]` é **saturação**, não contraste de luma. (`0x00049AE4` pula esta matriz
inteira quando `VCM[2] == 1.0`, que é o que uma saturação identidade seria.)

Com `glyphHighlightVCM = [0.2, 1.2, 1.25, 0.0, true]` isso dá, em uma frase:

> **levantar a luma em `0.2` (ganho `1.0`) e abrir a croma em `1.25`.**

E com `glyphDarklightVCM = [−0.15, 0.7, 1.25, 0.0]`: `Y ← 0.85·Y − 0.15`, mesma
abertura de croma. **Nem um nem outro soma branco.** É por isso que o realce do
alvo lê como vidro e o daqui lê como cromado: aqui se soma luz branca por cima,
lá se levanta a luz **do que já está embaixo** e se guarda a cor dele.

### 2.2. E o `addStyle:9` não pinta cor nenhuma — ele é pulado

`[BIN]` O laudo anterior leu o `addStyle:9 data:` de `0x00049800` como "pinta
através da máscara uma cor montada por transformadas compostas". Não é isso.
A tabela de salto de `_RBDrawingStateAddStyle` (`RenderBox 0x00040AB0`, tabela
de bytes em `0x0015E440`) manda o índice `9` para `0x00040B68`, que monta um
`RB::Filter::ColorClamp` (`add_filter_style<Filter::ColorClamp>`, `0x00045048`)
a partir de dois RGB e um `RBColorSpace` — e os dois RGB que `0x000497D8`
escreve são `(0,0,0)` e `(h,h,h)` com `h` o *content headroom*.

`[BIN]` **E ele não roda**: `0x000497C0` (`cmp w19, #1; b.eq 0x49804`) pula
tanto o `addContentHeadroom:` quanto o `addStyle:9` quando `VCM[4]` é `1` — e
`glyphHighlightVCM[4]` **é** `true`. O quinto campo do VCM, que a frente
anterior registrou como "o quinto é `Bool`" sem saber o que fazia, é o
interruptor do grampo de headroom.

### 2.3. A sequência inteira, e a única perna que falta

`[BIN]` Por realce, no ramo `useVCM == true`:

```
save                                       0x8F2C0
beginLayer                                 0x8E460
  <o glassHighlight, dentro da camada>     0xE834
clipLayerWithAlpha:1.0 mode:0              0x8E620   -> Builder::clip_layer
[addContentHeadroom: / addStyle:9]         PULADOS, VCM[4] == 1
addColorMatrixFilterWithArray:flags:0      0x8E260   <- a composta de §2.1
beginLayerWithFlags:1                      0x8E480
drawLayerWithAlpha:1.0 blendMode:0         0x8E860   blendMode 0 == normal
restore                                    0x8F2A0
```

A forma do realce vira **recorte**; dentro dele entra a matriz de cor; e então
se abre e desenha uma camada com alfa 1 e blend **normal**. Não há `drawShape:`
nenhum neste ramo — nada é pintado além do `drawLayerWithAlpha:`.

`[OBS]` **A perna que falta é uma só, e é um `flag`.** Uma matriz de cor sem
fonte produziria a cor constante do viés; para a sequência acima fazer sentido,
o `beginLayerWithFlags:1` tem de instanciar a variante de **fundo** do filtro —
e ela existe no RenderBox, `RB::DisplayList::BackdropFilterItem<Filter::ColorMatrix>`
(`0x0007D798`, com `GenericFilter<Filter::ColorMatrix>::make_backdrop_item` em
`0x0007E850`). `-[RBDisplayList beginLayerWithFlags:]` (`0x0003BCA0`) leva o
`1` a `RB::DisplayList::Builder::begin_layer(State, OptionSet<Layer::Flag>)`
(`0x000C9A28`), e **o significado do bit 0 de `Layer::Flag` não foi lido**.

Sem esse bit, "a matriz lê o fundo" é a única leitura que fecha, mas é uma
leitura por eliminação — e este projeto não pinta por eliminação. Com ele, o
realce é implementável aqui inteiro: por pixel, `lerp(fundo, VCM(fundo),
cobertura)`, com a cobertura saindo do mesmo `glassHighlightFragment` de hoje.

---

## 3. O que foi entregue em código

- **`Source/RenderBox/BlendFormula.h` / `.cpp`.** `clampedPlusL()`, transcrito
  do IR com os nomes `source`/`dest` do metadado AIR; `BlendOptions::clampPlusLighter`;
  e o comentário que carrega a proveniência da flag, os quatro sítios de troca e
  o **negativo** do §1.1. O `case PlusLighter` de `blend()` desvia para ele
  quando a opção está ligada.
- **`Source/RenderBox/GlassSpecular.h`.** `SpecularArguments::clampPlusLighter`,
  **`false`**, com o §1.1 escrito em cima dele.
- **`Source/RenderBox/GlassSpecular.cpp`.** `drawSpecular` passa a opção (hoje
  desligada) ao `blend`, e a nota de camada do especular foi reescrita: ela não
  diz mais que o `addStyle:` pinta a cor, diz o que o `addStyle:9` é e que ele é
  pulado, e passa a nomear as duas matrizes BT.709 com endereço e a única perna
  que falta.
- **`Tests/test_blend_formula.cpp`.** Um caso novo com quatro asserções: que o
  grampo **morde** (três brancos opacos: `4.0` cru contra `1.0` grampeado, e o
  `plusDarker` seguinte descendo num caso e não no outro); que o teto é o `1.0`
  literal e não o alfa de saída; que o `fmax` é contra o fundo (um fundo em
  `2.5` sobrevive ao grampo); e que `plusDarker` lê igual com a opção dos dois
  jeitos. O caso antigo dos dois `plus` passou a pedir a fórmula crua
  explicitamente, com a razão escrita.

### A prova em pixel

Release (`cmake --preset release -DIC_BUILD_UI=OFF`, alvo `icrender`), 1024 px,
`--idiom square`, só pixels visíveis (alfa > 0 num dos dois):

| ícone | mudados | delta máx. | tempo |
|---|---|---|---|
| `GoWToolkit.icon` (o do usuário) | **0** de 987.176 | **0** | 0,410 s → **0,389 s** |
| `Apollo-Reborn…AppIcon` | **0** de 987.549 | **0** | 2,653 s → **2,460 s** |

**Zero, e o zero é o laudo.** Este commit lê duas coisas e liga nenhuma, porque
das duas uma não é do realce e a outra tem uma perna faltando. O número que o
pedido queria ver existe e foi medido — ligar o grampo no `drawSpecular` dá
**91.276 pixels do Apollo com delta 118** — e é exatamente o número que não se
pode entregar, porque `0x000491C0` não chama `0x0004B4EC`.

Suíte: **639 casos, 0 falhas** (`mingw`, `-DIC_BUILD_UI=OFF`, com
`IC_CORPUS_DIR` apontando para a árvore principal).

---

## 4. O que continua `[OBS]`

1. `[OBS]` **O bit 0 de `RB::DisplayList::Layer::Flag`** — se
   `beginLayerWithFlags:1` faz do `addColorMatrixFilterWithArray:` um
   `BackdropFilterItem`. **É a última perna do VCM**, e com ela o realce sai.
   `-[RBDisplayList beginLayerWithFlags:]` `0x0003BCA0` →
   `Builder::begin_layer` `0x000C9A28`; a variante de fundo é `0x0007D798`.
2. `[OBS]` **Quais desenhos o grampo de `plusLighter` cobre** (§1.1): quem monta
   o descritor cujo `+0x31` chega a `0x000435A0` / `0x0004B4EC`. Sabe-se que o
   especular do glifo **não** chega; o resto não foi seguido.
3. `[OBS]` **A regra exata de `0x00007064`.** A ordem está fixada pela cadeia
   (`0x0` aplicado DEPOIS de `x1`, senão RGB→YCbCr não viria antes dos níveis),
   mas a aritmética SIMD dela não foi transcrita instrução a instrução.
4. `[OBS]` **`clipLayerWithAlpha:mode:`** — `Builder::clip_layer(Layer*, State&,
   float, ClipMode)` (`RenderBox 0x000C9EFC`, via `0x0003BB64`), e o que
   `mode:0` faz. O alfa é `1.0` e o `mode` é `0`, mas a semântica da cobertura
   não foi lida.
5. `[OBS]` Os seis valores de `spatialHighlighting` e os itens 4 a 7 do laudo-mãe
   seguem intocados.

---

## 5. Resumo dos selos

| pergunta | resposta | selo |
|---|---|---|
| `shouldClampPlusLBlending` | `params+0x220`, **`true`** (`0x0005EAE8` com `w22=1` de `0x0005E8BC`), ancorado pelo `266.24` de `+0x228` que outra frente já lera | `[BIN]` |
| o que ele liga | `setBlendShader:` com **`clampedPlusL`**, nome soletrado por `mov`/`movk` em `0x0000D88C`/`0x0000D89C`, entry point Metal em `default_mod8.ll:37` | `[BIN]` |
| a fórmula | `max(dest, (min(1, source+dest).rgb, saturate(source.a+dest.a)))`, com `source`/`dest` **nomeados** pelo metadado AIR | `[BIN]` |
| ele cobre o realce do glifo? | **NÃO.** Os quatro sítios vivem em `0x000435A0`/`0x0004B4EC`; `0x000491C0`-`0x00049DBC` não chama nenhuma e desenha por `drawShape:fill:alpha:blendMode:` (`0x0000ED00`) | `[BIN]` |
| o que ele cobre então | não lido | `[OBS]` |
| `VCM` | **Video Color Matrix.** `0xE2960` é o `CAColorMatrix` BT.709 RGB→YCbCr (copiado do `__const 0x938E0` pelo corpo de `swift_once` `0x00006948`), `0xE2910` a inversa (`0x00006988`), e as duas se invertem numericamente | `[BIN]` |
| o que os cinco números fazem | `Y ← (VCM[1]−VCM[0])·Y + VCM[0]`; `Cb,Cr ← VCM[2]·c + (0.5−0.5·VCM[2])`; `VCM[4]` pula o grampo de headroom; `VCM[3]` é esse headroom | `[BIN]` |
| `VCM[2]` | **saturação**, não contraste de luma — a linha do `Y` da matriz de `0x00049B18` é `[1,0,0,0,0]` | `[BIN]` |
| `addStyle:9` | `RB::Filter::ColorClamp` (tabela de salto `0x0015E440`), e **pulado** aqui porque `VCM[4] == 1` | `[BIN]` |
| quem pinta | `addColorMatrixFilterWithArray:flags:0` (`0x00049C48`), depois `beginLayerWithFlags:1` + `drawLayerWithAlpha:1.0 blendMode:0` | `[BIN]` |
| a matriz lê o fundo? | o bit 0 de `Layer::Flag` não foi lido; `BackdropFilterItem<Filter::ColorMatrix>` existe (`0x0007D798`) | `[OBS]` |
| pixel | **0** de 987.176 (GoW) e **0** de 987.549 (Apollo) — nada foi ligado | `[BIN]` |

### Instrumentos

Nenhum MCP de RE (`list_instances` vazio pelo quinto laudo seguido). `dis`/`fn`/
`xref`/`syms` de `scripts/macho.py`, mais duas varreduras que o `macho.py` não
faz e que valeram este laudo: **um scanner de acessos por deslocamento de
struct** (`ldrb`/`str`/`ldr` com imediato escalado — foi assim que
`params+0x220` deu em `0x0005EAE8` numa varredura e não numa busca) e a **leitura
da tabela de seções** para saber de onde `0x938D0` vinha. E o `RenderBox.arm64`,
que **mantém símbolos** e por isso entregou `_RBDrawingStateAddStyle`,
`add_filter_style<Filter::ColorClamp>` e a família `BackdropFilterItem<>` de
graça.
