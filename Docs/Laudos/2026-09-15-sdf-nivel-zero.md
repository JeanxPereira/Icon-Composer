# Laudo — o nível zero do SDF do CoreUI: o texel foi cercado do lado de cá, e a convenção do zero NÃO cabe nos 7,5 unidades

*2026-09-15. Alvo: o último candidato do `[OBS]` do deslize de 7,5 ± 0,9 unidades
de canvas da banda do realce especular (`2026-09-15-realce-forma.md` §5, item 1),
depois que a frente da refração (`7b710ad`) eliminou `[descriptor+0xA8]`.*

**Nenhum pixel mudou neste commit.** O resultado é uma medida e duas eliminações.
O produtor do campo continua fora do alcance — e isso agora está provado por
carga de biblioteca, não por ausência de busca —, mas o **consumidor inteiro**
foi lido, e com ele a aritmética que diz **quanto** a convenção do zero de um
texel pode valer. Ela não chega perto de 7,5.

Endereços de `References/27.0-129/out/slices/IconRendering.arm64` e, onde estiver
escrito `RenderBox`, de `RenderBox.arm64` (VA == deslocamento de arquivo). Os
módulos `.ll` são os de `References/27.0-129/out/metallib-iconrendering/`.

---

## 1. A parede, medida e não suposta

`[BIN]` `sdfTextureWithBufferAllocator:` tem **um** stub (`0x8F320`) e **um**
sítio de chamada (`0x867F8`, dentro de `0x8658C`). A string mora em
`__TEXT,__objc_methname 0xB2EC3` — tabela de **nomes de seletor**, não de método
implementado — e não existe nenhum `-[<Classe> sdfTextureWithBufferAllocator:]`
em nenhuma method list do slice.

`[BIN]` **A prova positiva de que a implementação é externa é uma carga de
dylib**, não a falta de um símbolo:

```
LC_LOAD_WEAK_DYLIB  /System/Library/PrivateFrameworks/CoreUI.framework/Versions/A/CoreUI
```

presente nas **duas** versões (`2.0-125` e `27.0-129`) do `IconRendering`. É
`WEAK`, e por isso não aparece numa listagem de `LC_LOAD_DYLIB` — foi essa a
razão de a parede parecer "ausência de evidência" até hoje. Todas as classes
`CUI*` (`_OBJC_CLASS_$_CUICatalog`, `_CUINamedLayerImage`,
`_CUINamedIconLayerStack`, `_CUINamedIconLayerGroup`, `_CUINamedColor`,
`_CUINamedGradient`, `_CUIEncapsulationShape`, `_CUIMutable*`) são símbolos
**undefined** — importações.

`[BIN]` E o CoreUI **não está no DMG**. Esgotado:

| onde se procurou | resultado |
|---|---|
| `Contents/MacOS/Icon Composer`, `Executables/ictool`, `Executables/icrtool` | FAT x86_64+arm64; **zero** ocorrência de `sdfTexture`/`CUI`/`CoreUI`; nenhum dos três linka CoreUI |
| os dois appex (QuickLook Preview / Thumbnail) | idem |
| `References/27.0-129/disk.img` (21.315.584 B) via `tree.txt` (109 linhas) | `grep -i "coreui\|dyld\|shared_cache\|privateframework\|cache"` → **0 hits**; `grep -a -c "CoreUI" disk.img` → **0** |
| frameworks embarcados no bundle | CoreSVG, IconComposerFoundation, IconComposerKit, IconRendering, RenderBox. Só isso. |
| `References/` inteiro | nenhum arquivo com `coreui`/`dyld`/`cache`/`shared` no nome; nenhum `.dylib`; `CUIRendition` → 0 hits em qualquer lugar |
| slices de `2.0-125` (4 extras: CoreSVG, IconComposer, ictool, icrtool) | nenhum é CoreUI |

`[BIN]` **Diferencial entre versões, de quebra:** `RenderBox.arm64` é
**efetivamente idêntico** nas duas versões — mesmo tamanho (2.772.176), **mesmo
`LC_UUID` `237dec3cbe7c3143877eebd12cfbb196`**, e os únicos 413 bytes diferentes
caem todos dentro do blob de `LC_CODE_SIGNATURE` (`0x29B1B0`–`0x2A4CD0`). Não
vale re-analisar RenderBox 27. `IconRendering.arm64` é um rebuild de verdade
(UUIDs distintos, 162.758 bytes divergentes espalhados de `0x180` a `0x10E880`),
mas o trecho que interessa aqui é idêntico ao que `2.0-125` já dizia.

`[BIN]` **Confirmação independente da frente `semente-aa`:** os três botões de
`ICRRenderingParameters.SDFGeneration` (`0xA46E0` — `clampThreshold`,
`precisePixelFormatThreshold`, `maxRelativeSmoothing`) continuam sendo
configuração morta neste binário. Esta frente bateu na mesma parede por um
caminho diferente (do consumidor para trás, não do parâmetro para a frente) e
chegou ao mesmo lugar. Duas frentes, dois caminhos, uma parede.

**A partir daqui, tudo é do lado de cá.**

---

## 2. O FORMATO DO TEXEL — `[OBS]` FECHADO. São quatro, e três são `unorm`

Esta é a primeira pergunta da frente e ela tem resposta exata, porque o
**consumidor recusa** um formato que não conheça.

`[BIN]` `0x8658C` — a função que envolve a chamada ao CoreUI — carrega uma
tabela global (`0xE2B80`, montada uma vez por `swift_once` cujo corpo é
`0x85638` → `0x85654`) e **casa o `pixelFormat` da textura que o CoreUI
devolveu** contra cada entrada:

```
0x00086A6C  mov  x0, x23              ; x23 = retorno de sdfTextureWithBufferAllocator:
0x00086A70  bl   _objc_msgSend$pixelFormat
0x00086A74  cmp  x22, x0              ; x22 = campo +0x00 da entrada da tabela
0x00086A78  b.eq #0x86ADC             ; casou -> segue
```

e, esgotada a tabela sem casar, monta um erro com o código `4`
(`0x86AB0 mov w8,#4` / `0x86AB8 strb w8,[x1,#8]`) e **aborta a carga do SDF**.
O `cmp` contra o retorno de `pixelFormat` é o que prova que o campo `+0x00` da
entrada É um `MTLPixelFormat`, sem depender de nome nenhum.

`[BIN]` A tabela tem **quatro entradas de `0x30` bytes** (elementos a partir de
`array+0x20`; o leitor as percorre em `0x86A58`–`0x86A68`, o construtor as
escreve em `0x856A4`–`0x85748`):

| # | `+0x00` | `MTLPixelFormat` | `+0x18` | constante | endereço |
|---|---|---|---|---|---|
| 0 | 80 | `BGRA8Unorm` | **4** | `0x9C060` / `0x856B0` | |
| 1 | 90 | `RGB10A2Unorm` | **4** | `0x9C070` / `0x856D8` | |
| 2 | 115 | `RGBA16Float` | **8** | `0x9C080` / `0x856FC` | |
| 3 | 10 | `R8Unorm` | **1** | `0x9C090` / `0x85738` | |

`[BIN]` **O segundo caminho que confirma a identificação** (armadilha 3): o campo
`+0x18` vale `4, 4, 8, 1` — que são exatamente os **bytes por pixel** de
`BGRA8Unorm`, `RGB10A2Unorm`, `RGBA16Float` e `R8Unorm`. Dois campos
independentes da mesma linha concordam sobre qual formato ela nomeia. (Os
campos `+0x10`, `+0x20` e `+0x28` — `1,1,1,0`, os pares `(2,3)/(3,0)/(5,0)/(2,0)`
em `0x9C038`/`0x97890`/`0x9C040`/`0x9C048`, e `0,0,0,1` — não foram nomeados e
não são usados aqui.)

`[BIN]` Contexto da mesma função, de carona: o alinhamento do alocador de buffer
que atravessa a chamada é o **máximo** de
`minimumTextureBufferAlignmentForPixelFormat:` (`0x86628`) sobre os quatro
formatos, reduzido pelo `cmgt`/`bit` de `0x86724`–`0x86774`. Nenhum limiar,
nenhuma suavização e **nenhuma dimensão** cruzam a fronteira — o que a frente
`semente-aa` já tinha dito e esta releitura confirma linha a linha.

> **Resposta à pergunta 1.** O texel do SDF do alvo é `BGRA8Unorm`,
> `RGB10A2Unorm`, `RGBA16Float` ou `R8Unorm`. Em três dos quatro o canal é
> **`unorm`**, e é aí que uma convenção de zero pode existir. No quarto é
> half-float, onde `0,5` é exato e convenção nenhuma existe.

---

## 3. O CONSUMIDOR, INTEIRO — e ele só tem UM termo de origem

### 3.1. A decodificação, lida no shader

`[BIN]` `metallib-iconrendering/default_mod0.ll` (`@glassHighlight`) e
`default_mod1.ll` (`@glassHighlight_v1`) fazem a mesma coisa, e o `_v1` — que lê
os argumentos de um buffer, uma casa por vez — dá o **mapa de índices** de graça:

```llvm
%42 = air.sample_texture_2d.v4f16(tex, __air_sampler_state, %0, ...)
%44 = extractelement %43, i64 0        ; tex.r
%45 = fpext half %44 to float
%46 = fsub float %41, %45              ; slot10 - tex.r
%47 = fmul float %46, %39              ; * slot9
%48 = fsub float %47, %10              ; - slot1      <<<<<<<<
%49 = fptrunc float %48 to half        ; -> `sd`
%50 = shufflevector %43, <i32 1, i32 2>
%51 = fmul <2 x half> %50, splat (half 2.0)
%52 = fsub <2 x half> splat (half 1.0), %51   ; normal = 1 - 2*tex.gb
```

`[BIN]` E o casamento das casas com os índices de
`setArgumentBytes:atIndex:type:count:flags:` em `0x0000E834` fecha **exato** —
dez índices, onze casas de `float`, porque o índice 5 é `count 2` e o 6 é a cor
(`type 5`, duas casas):

| índice | casa | escrito em | o que é |
|---|---|---|---|
| 0 | 0 | `0xE970` (`ldr s0,[sp,#0x10]`) | `height` |
| **1** | **1** | **`0xE994` (`ldr s0,[sp,#0x14]`)** | **`inset` — o único termo de origem** |
| 2 | 2 | `0xE9B8` | `spread'` |
| 3 | 3 | `0xE9DC` (`1/x − 2`) | `bias'` |
| 4 | 4 | `0xEA10` | `curvature` |
| 5 | 5,6 | `0xEA34` | direção `(x, −y)` |
| 6 | 7,8 | `0xEA58` | cor |
| **7** | **9** | **`0xEA7C`–`0xEA84` (`fmul d9, #-2.0`)** | **escala = `−2·maxDistance`** |
| **8** | **10** | **`0xEAA8` (`mov w8,#0x3f000000`)** | **nível zero = `0,5f`** |
| 9 | — | `0xEAD4` (`type 7`) | a textura |

O nome do shader é soletrado por `mov`/`movk` em `0xE92C`–`0xE948` (armadilha 1:
não está no pool) — `glassHighlight`, 14 bytes, *small string*.

Portanto, com `u = tex.r`:

> `[BIN]` **`sd = (2u − 1)·maxDistance − inset`**

### 3.2. Dois consumidores independentes escrevem a MESMA forma

`[BIN]` **O gêmeo de CPU, e ele está num binário COM símbolos.**
`RB::CGContext::apply_glass_highlight(const RB::GlassHighlightEffect&)` vive em
`RenderBox.arm64 0xC0E64`, e passa pelo helper compartilhado
`RB::(anonymous)::apply_distance_effect<…>` (os *thunks* de
`dispatch_apply_tiles` em `0xC5034`, `0xC50E4`, `0xC52C4`, `0xC5450`, `0xC56B0`,
`0xC577C` carregam a assinatura inteira no nome manglado). É o mesmo helper que
`apply_glass_displacement` (`0xC1324`) usa, e a frente da refração já transcreveu
o par `(escala, viés)` de lá: `escala = range.x − range.y = 2M`,
`viés = offset − range.x = offset − M`, aplicado ao `u` cru. Isto é
`u·2M + offset − M = (2u − 1)·M + offset` — **a mesma expressão**, escrita por
outro caminho, noutro binário, para outro efeito.

`[BIN]` E a terceira leitura é a das duas cargas vizinhas do descritor:

```
0x00049234  ldr x28, [x0, #0x98]     ; Layer.sdf.texture
0x00049238  ldr d8, [x0, #0xa8]      ; Layer.sdf.maxDistance
```

`x28` vai para `0x84304` (via `0x49580 mov x0,x28`), que faz
`[[x28+0x50] width]` (`_objc_msgSend$width`, `0x843A8`) e
`[[x28+0x50] height]` (`0x84414`); `d8` vira `v9` e depois o `−2·d9` do índice 7.
**Os dois campos que a frente da refração leu por reflexão são usados quatro
bytes um do outro, na mesma função, cada um no papel que ela lhes deu.**

### 3.3. `inset` é zero, e é o único lugar onde caberia um deslocamento

`[BIN]` `0xED94` monta os argumentos e a origem sai dele assim:

```
0x0000EE0C  ldr   q1, [x20, #0x30]        ; (height, inset), em unidades de canvas
0x0000EF6C  ldur  q2, [x29, #-0x90]       ; a razão d0
0x0000EF74  fmul  v2.2d, v3.2d, v2.d[0]   ; (height·r, inset·r)
0x0000EF94  fcvtn v2.2s, v2.2d
0x0000EF98  str   d2, [x19]               ; -> índices 0 e 1
```

com a razão montada logo antes, em `0xE8CC`–`0xE8F4`:

> `[BIN]` **`r = (texture.width − 2) / rect.width`**, texels de SDF por unidade
> de canvas — `texture.width` pelo `_objc_msgSend$width` de `0x843A8`,
> `rect.width` em `[ctx+0x568]` (`0xE8C0`), o canvas `(0,0,1024,1024)` que a
> frente do blur-material montou em `0x4291C`–`0x42968`.

O `−2` é uma **moldura de um texel de cada lado**: o conteúdo do retângulo ocupa
`width − 2` texels, não `width`.

`[BIN]` E `inset` **é zero**, relido pela frente da forma do realce campo a campo
na fábrica `0x64604` (`keySharp` grava zeros em `+0x90`/`+0xA0` e a tag `nil` `1`
em `+0xD0`, `0x64698`–`0x646B0`; `keyDiffuse` idem, `0x64730`–`0x64738`), com
`0x4BD90`, `0xED94` e `0x4C314` não acrescentando termo nenhum.

> **Resposta às perguntas 2 e 3.** O consumidor **desfaz** a convenção com
> exatamente duas constantes — o `0,5` de `0xEAA8` e o `−2·maxDistance` de
> `0xEA7C` — e acrescenta exatamente **um** termo de origem, o `inset`, que é
> zero. Não há um segundo `0,5`, não há meio texel, não há `fmsub` escondido.
> **Todo o deslize de 7,5, se vier daqui, tem de estar dentro do `u` que o
> CoreUI gravou.** Essa é a boa notícia: a pergunta ficou de UMA variável.

---

## 4. A ARITMÉTICA — quanto vale a convenção do zero, em unidades de canvas

Escreva `D` = `maxDistance` **em unidades de canvas**
(`D = maxDistance_texels · rect.width/(texture.width − 2)`, pela razão de §3.3).
Com `inset = 0`:

```
sd_canvas = (2u − 1) · D
```

Se o gravador põe `sd = 0` em `u = 0,5 + Δu` em vez de `u = 0,5`, o leitor erra
`sd` por `2·D·Δu`. Para explicar os **7,5 unidades medidos**:

> **`|Δu| = 3,75 / D`**

Agora o teto: **de quanto pode ser uma convenção de zero?** De meia unidade do
menor passo do formato, porque isso é o que "qual código significa zero" custa —
e os formatos são os quatro de §2:

| formato | passo | `0,5` é representável? | `|Δu|` máximo | `D` exigido para dar 7,5 |
|---|---|---|---|---|
| `BGRA8Unorm` / `R8Unorm` | 1/255 | não (127/255 = 0,498039; 128/255 = 0,501961) | **1/510 = 0,0019608** | **1912,5** |
| `RGB10A2Unorm` | 1/1023 | não (511/1023; 512/1023) | 1/2046 = 0,00048876 | 7672,5 |
| `RGBA16Float` | — | **sim, exato** | 0 | ∞ (impossível) |

Mesmo admitindo o dobro — um erro de **um LSB inteiro**, e não de meio, que já
não é convenção mas engano — o formato mais grosseiro dos quatro exige
`D ≥ 956,25` unidades de canvas.

`[BIN]` **O canvas tem 1024 unidades de lado**, e o retângulo do chiclet do
gabarito é o canvas inteiro. A diagonal dele é `1024·√2 = 1448,2`.

> **VEREDITO.** Para a **convenção do nível zero do texel** carregar os 7,5
> unidades, `maxDistance` teria de valer **entre 93 % (um LSB inteiro) e 187 %
> (meio LSB) do LADO do canvas** — no segundo caso, 1,32× a própria **diagonal**
> da imagem em que o campo vive. **O candidato não cabe.**

`[INF]` E a consequência que torna isso implausível também pelo outro lado: com
`D = 1912`, a banda do realce — que o gabarito mostra entre ~5 e ~30 unidades de
profundidade (`realce-forma.md` §5) — caberia em `30/(2·1912)·255 = 2,0` códigos
do byte. **Dois degraus para a banda inteira.** E `keyDiffuse` do conjunto
`glyphs*` tem `distance` de 24 pt = 96 unidades de canvas, o que daria 6,4
códigos. Um SDF assim não desenha as bordas que o gabarito tem.

### 4.1. A rota do MEIO TEXEL também está fechada — e quem a fecha é a medida

A pergunta 3 da frente (centro do texel × canto) tem uma resposta que **não
precisa do CoreUI**, e ela sai da própria medida de `realce-forma.md` §5:

| tamanho do render | deslize medido | em unidades de canvas |
|---|---|---|
| 412 px | 3,0–3,5 px | 7,5 ± 0,9 |
| 206 px | 1,5 px | 7,5 ± 0,9 |

O deslize **é constante em unidades de canvas e cai pela metade em pixels quando
o render cai pela metade.** Um termo de grade — meio texel, um texel, a moldura
de `−2` de §3.3 — é constante em **TEXELS**, e a grade do SDF é a da própria
imagem da camada, que acompanha o render (`0xE8CC`–`0xE8F4` relê
`texture.width` a cada desenho, justamente porque ela muda). Logo um meio texel
apareceria como **o mesmo número de pixels nos dois tamanhos**, e não como o
mesmo número de unidades de canvas. **Não é o que o gabarito mostra.**

`[INF]` A saída que restaria — uma textura de SDF de tamanho **FIXO** — exigiria
`0,5·1024/(W−2) = 7,5`, isto é `W ≈ 70`: um campo de 70 texels para um ícone de
1024 unidades, com a silhueta do glifo quantizada em degraus de 15 unidades. A
mesma tabela de §2, que aceita quatro formatos e nenhum tamanho, e o `0x84304`
que pergunta a largura à textura em vez de a saber, dizem o contrário.

`[BIN]` A moldura de um texel (`−2`) é, de todo modo, uma **escala**, não uma
origem: a `412` ela vale `412/410 = 1,0049`. Pela mesma razão que derrubou
`[descriptor+0xA8]`, ela não carrega deslocamento. E para este renderizador ela é
inócua por um segundo motivo: **aqui não se amostra textura de SDF nenhuma** — o
campo é avaliado no pixel, e não há moldura para respeitar.

---

## 5. O que sobra, e qual byte exatamente falta

Os três candidatos que o `[OBS]` de `realce-forma.md` §5 listava estão agora
assim:

| candidato | estado | quem fechou |
|---|---|---|
| `[descriptor+0xA8]` (`maxDistance`) como origem | **eliminado**: `(v,−v)` é simétrico por `fneg` literal | `refracao.md`, `7b710ad` |
| o `inset` do shader | **eliminado**: é o único termo de origem da cadeia e vale **zero** | `realce-forma.md` §4.3, reconfirmado aqui em `0xED94`/`0xE994` |
| **a convenção do nível zero do texel** | **eliminado**: exige `maxDistance ≥ 956` unidades de canvas, 93 % do lado do canvas | **este laudo, §4** |
| **a grade / meio texel** | **eliminado**: seria constante em PIXELS, e a medida é constante em unidades de CANVAS | **este laudo, §4.1** |

**O `[OBS]` continua aberto, e com quatro candidatos a menos ele ficou afiado.**
O que resta não é uma convenção de codificação nem um artefato de grade: é um
**termo geométrico de ~7,5 unidades de canvas dentro do gerador**, isto é, o
contorno sobre o qual o CoreUI centra o campo não é o `alpha ≥ 0,5` da arte da
camada, mas um contorno recuado. Um limiar de alfa (`clampThreshold`) **não** faz
isso — a rampa de alfa de uma borda antialiada tem ~1 pixel de largura, o que
seria constante em pixels e cairia pela mesma régua de §4.1.

> **`[OBS]` O BYTE QUE FALTA, e onde ele mora.** São **dois valores**, ambos
> escritos por `-[CUINamedLayerImage sdfTextureWithBufferAllocator:]`:
>
> 1. **`maxDistance`** — o `Double` que chega a `[Layer+0xA8]` (`0x49238`) e vira
>    a escala `−2·M` do índice 7. Sem ele, `Δu` não se converte em unidades de
>    canvas e o §4 é uma desigualdade em vez de uma igualdade.
> 2. **o valor de `tex.r` no contorno `alpha = 0,5` da arte da camada** — o
>    `u₀` tal que `(2u₀ − 1)·M` é a profundidade em que o alvo põe o zero.
>
> Os dois vivem em
> `/System/Library/PrivateFrameworks/CoreUI.framework/Versions/A/CoreUI`,
> alcançado por `LC_LOAD_WEAK_DYLIB`, que **não é distribuído no DMG** (§1) e
> mora no `dyld_shared_cache` do macOS. Para chegar lá seria preciso
> exatamente um de: (a) `CoreUI` extraído de um `dyld_shared_cache_arm64e` de
> macOS 26/27 (`dsc_extractor` / `ipsw dyld extract CoreUI`); (b) o
> `dyld_shared_cache_arm64e` completo de uma imagem/IPSW; (c) um
> `CoreUI.framework/CoreUI` standalone de um dump de `PrivateFrameworks`.
> **Nada disso está em `References/`, e nada disso foi inventado aqui.**

**E nenhum número foi aplicado.** Aplicar 7,5 continua sendo ajustar parâmetro
até o diff fechar, e o mesmo deslize continua movendo 91.301 px do ícone do
usuário (`realce-forma.md` §5). A frente entrega a medição.

---

## 6. O pixel, a suíte e o tempo

Release, `-DIC_BUILD_UI=OFF`, alvos `ic_tests` e `icrender`.

**Nenhuma linha executável mudou neste commit** — o diff é este laudo e um bloco
de comentário em `Source/RenderBox/GlassSpecular.h`, que é onde o `[OBS]` mora e
onde a próxima frente vai abrir. O controle, **medido antes e depois da mudança,
com o `icrender` religado nos dois lados** e a contagem de camadas conferida
(armadilha 4b):

| render | camadas | tempo | SHA-256 antes × depois |
|---|---|---|---|
| `AppIcon-27.icon` @ 412 | **6 of 6 layer(s) drawn** | 0,386 → **0,276 s** | `978F76B3…F83FDC` = `978F76B3…F83FDC` |
| `GoWToolkit.icon` @ 1024 `--idiom square` | **1 of 1 layer(s) drawn** | 0,638 → **0,457 s** | `D75C348E…AF5B37` = `D75C348E…AF5B37` |

**Zero pixel mudado nos dois ícones.** (A diferença de tempo é ruído de cache de
primeira execução, não trabalho: o binário desenha o mesmo byte.)

`[BIN]` Suíte: **652 casos, 0 falhas**, com `IC_CORPUS_DIR` apontado para
`References/corpus`. **652 é a baseline correta desta configuração**, e não uma
regressão dos 678: `Tests/CMakeLists.txt:64` compila os casos de UI dentro de um
`if(TARGET IconComposerKit)`, e com `IC_BUILD_UI=OFF` os 26 casos `kit_*` /
`canvas_*` / `layers_*` **não existem**. Nenhum deles encosta no campo de
distância nem no especular.

---

## 7. Resumo, com selo

| pergunta | resposta | selo |
|---|---|---|
| quem chama `sdfTextureWithBufferAllocator:` | um sítio só, `0x867F8` em `0x8658C`; o argumento que cruza é o alocador de buffer e nada mais | `[BIN]` |
| a implementação está alcançável? | **não**: `LC_LOAD_WEAK_DYLIB /System/Library/PrivateFrameworks/CoreUI.framework/…`, ausente do DMG (`tree.txt`: 0 hits) e dos três executáveis | `[BIN]` |
| qual o formato do texel | um de `BGRA8Unorm(80)`, `RGB10A2Unorm(90)`, `RGBA16Float(115)`, `R8Unorm(10)`; fora deles a carga **aborta** com o código 4 (`0x86AB0`) | `[BIN]` |
| o consumidor desfaz a convenção? | sim, com `0,5` (`0xEAA8`) e `−2·maxDistance` (`0xEA7C`); `sd = (2u−1)·M − inset` | `[BIN]` |
| quantos termos de origem existem na cadeia | **um**, o `inset` do índice 1 (`0xE994`), e vale **zero** (`0x64604`) | `[BIN]` |
| as unidades de `sd` | texels de SDF; `r = (texture.width − 2)/rect.width` (`0x843A8`, `[ctx+0x568]`), com moldura de um texel por lado | `[BIN]` |
| o amostrador está no centro ou no canto do texel? | **a pergunta não decide nada**: qualquer termo de grade é constante em PIXELS e a medida é constante em unidades de CANVAS | `[BIN]` + medida |
| a convenção do zero explica os 7,5? | **não**: exigiria `maxDistance ≥ 956` unidades de canvas (93 % do lado) com um LSB inteiro, ou 1912 (1,32× a diagonal) com meio LSB | `[BIN]` + aritmética |
| então onde está? | um termo **geométrico** de ~7,5 unidades de canvas dentro do gerador do CoreUI | `[OBS]` |
| qual byte falta | `maxDistance` e o `tex.r` do contorno `alpha = 0,5`, os dois em `CoreUI.framework` | `[OBS]` |
| o pixel mudou? | **não**: nenhuma linha executável mudou; `6 of 6` e `1 of 1` camadas, 652 casos verdes | `[BIN]` |
