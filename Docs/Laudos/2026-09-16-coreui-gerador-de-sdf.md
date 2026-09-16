# Laudo — o gerador de SDF: a parede era no lugar errado, e o gerador não tem o recuo

*2026-09-16. Frente irmã de `2026-09-15-sdf-nivel-zero.md`, aberta quando o
`CoreUI` foi extraído do `dyld_shared_cache` de **macOS 27.0 beta 6, build
26A5416b** (`D:/CodingProjects/SF-Symbols/References/26A5416b/dsc-images/CoreUI`,
4,6 MB, arm64e). Alvo: os dois valores que a frente irmã declarou faltar —
`maxDistance` e o `tex.r` do contorno `alpha = 0,5` — e o deslize de
7,5 ± 0,9 unidades de canvas da banda do realce.*

**Resultado em uma linha.** O `CoreUI` **não gera** SDF nenhum — ele busca uma
textura já gravada. O gerador está **do lado de cá**, no `IconRendering`
(`0x1CB90`) e no `RenderBox` 8.0.84 (`RB::Filter::Distance`), binários que o
projeto sempre teve. Lido inteiro, ele dá `maxDistance = 0,2·min(W,H)` (que **se
cancela**), zero **exato** no contorno `alpha = 0,5`, e **nenhum recuo**. **O
deslize de 7,5 não fechou, e não está no gerador.** Nenhum pixel mudou.

Selos: `[BIN] CoreUI 1010` para o binário extraído; `[BIN] IR 27` para
`References/27.0-129/out/slices/IconRendering.arm64` (e, onde dito, igual em
`2.0-125`); `[BIN] RB 8.0.84` para `RenderBox.arm64` (idêntico nas duas versões,
`LC_UUID` igual, laudo irmão §1). VA == deslocamento de arquivo nas fatias.

---

## 0. A ressalva de versão, tratada antes de tudo

O `CoreUI` lido é o **1010**; o Icon Composer 27.0-129 linka **1007** e o 2.0-125
linka **1008**. Isto limita só o §1 — e o §1 é negativo por uma razão que não
depende de versão: o `IconRendering` **1007/1008-linkado** contém ele mesmo o
gerador (§2) e o gravador (`setSdfTexture:`, `0x2551C`). As duas metades do
contrato estão num binário que é do alvo, não do sistema. Nenhum valor numérico
deste laudo vem do CoreUI 1010.

`[OBS]` Não foi feito diferencial com `26A5421a` (a extração não foi pedida).

---

## 1. O CoreUI 1010: `sdfTextureWithBufferAllocator:` é uma BUSCA

### 1.1. As ferramentas, e as duas armadilhas da imagem extraída

A imagem do cache tem `__objc_stubs`, `__auth_stubs` e `__objc_methname` com
**tamanho zero**: os `bl` de mensagem saem para uma região compartilhada
(`0x1900xxxxx`) fora do arquivo. Três réguas resolveram (todas no scratchpad):

- **tabela de símbolos indiretos** para `__got`/`__auth_got` (1.103 fendas):
  `0x1E09E9CE0` = `_OBJC_CLASS_$_CUINamedTexture`;
- **ponteiros empacotados** do slide-info (34 bits baixos = deslocamento de
  `0x180000000`) para ler as tabelas de nomes de `__DATA_CONST`;
- **nomeação dos stubs compartilhados por ordem**: os 2.187
  `_objc_msgSend$…` da própria imagem estão em ordem alfabética estrita, passo
  `0x20`; a região compartilhada preserva a ordem local. **A hipótese foi
  PREVISTA e depois conferida por fluxo de argumento, em dois sítios
  independentes**: `0x190025410` recebe `1` só atrás de `_CUIImageIsWideGamut`
  no mesmo byte (`0x18B49C854`–`0x18B49C86C`) → `setThemeDisplayGamut:`; e
  `0x190025500` recebe `x27 = x5`, o argumento `atScale:` de
  `_addImage:withBaseKey:name:atScale:` (`0x18B49C87C`–`0x18B49C890`) →
  `setThemeScale:`. As duas posições foram previstas a partir de
  `setThemeElement:` antes da conferência.

### 1.2. O que o método faz

`[BIN] CoreUI 1010` `-[CUINamedLayerImage(CUINamedIconLayerStack)
sdfTextureWithBufferAllocator:]` (`0x18B4B3044`, 432 bytes, **nenhuma operação
de ponto flutuante**):

```
cached = objc_getAssociatedObject(self, "com.apple.coreui.iconstack.sdf")   ; 0x18B5A23ED
if cached == nil:
    id  = CUIRenditionKeyValueForAttribute(key(self), 17)       ; 0x18B4B30A4, 17 = Identifier
    k   = copy(baseKey)
    k.element = 41; k.part = 0; k.identifier = id               ; 0x18B4B30BC..0x18B4B30D8
    k.displayGamut = 1; k.dimension1 = 0; k.dimension2 = 0      ; P3 primeiro
    if !store.canGetRendition(k): k.displayGamut = 0            ; 0x18B4B3120, cai para sRGB
    if !store.canGetRendition(k): log "unable to locate sdf '%@' key:%@"   ; 0x18B5A240C
    cached = [CUINamedTexture …name:… key:k theme:…]            ; 0x18B4B3170
    objc_setAssociatedObject(self, key, cached, 0x301)          ; RETAIN
return [cached textureWithBufferAllocator:allocator]            ; cauda 0x18B4B31F0
```

- **atributo 17 = Identifier**: cada `-[CUIRenditionKey setTheme…:]` carrega o
  próprio número (`setThemeIdentifier:` `0x18B511D60 mov w1,#0x11`; a tabela
  inteira 1..27 foi tirada assim).
- **elemento 41 = `kCoreThemeTextureID` "Texture Group"** (tabela de
  elementos, `0x1E0927468`, entre `Icons=38` e `SearchField=44`).
- a cauda `0x190025A30` tem **três** chamadores, todos repassando uma
  `CUINamedTexture` guardada e o alocador: este, `edgeDefinitionTexture…` e
  `gradientTexture…` de `CUINamedSolidLayerImage`.
- `-[CUINamedTexture textureWithBufferAllocator:]` (`0x18B55A1F8`) exige
  rendition do tipo `0x3EF` (1007), embrulha num `_CUTextureLink` e entrega a
  um carregador externo. `_cacheRenditionProperties` (`0x18B55A0E4`) guarda só
  escala, orientação e dois bits. **Nenhuma distância.**
- a versão mutável (`0x18B4B0368`) **recusa** com
  "Can't use sdfTextureWithBufferAllocator: on a CUIMutableNamedIconLayerStack"
  e devolve `nil`; o que ela tem é `setSdfTexture:` — **o cliente fornece o
  campo.**

### 1.3. De carona: o `CUINamedSolidLayerImage`

`[BIN] CoreUI 1010` `0x18B52C0F8` monta **duas** chaves elemento 41 com
`Dimension1 = 1` e `= 2`, guardadas em `_edgeTexture` e `_gradientTexture` —
exatamente `kCoreThemeTextureSDFEdge = 1` e `kCoreThemeTextureSDFGradient = 2`
da tabela `0x1E09264E0` (vizinha das faces de cubo `+X..-Z = 0..5`). O
`1.0` escrito ali é `_opacity`, não distância.

> **Varredura negativa.** Em todo o CoreUI 1010 a palavra `sdf` (qualquer caixa)
> aparece só nos quatro métodos, nas duas mensagens de log, na chave associada
> e nos dois nomes da tabela de texturas. **Não há gerador no CoreUI.**

---

## 2. O gerador, no `IconRendering`

### 2.1. `maxDistance` — pergunta 1

`[BIN] IR 27` (endereços **iguais** em 2.0-125), varredura de imediatos
materializados por `mov`/`movk`/`orr` + `fmov` (armadilha 1): `0.2`
(`0x3FC999999999999A`) aparece em **três** sítios, e dois são as metades do
contrato:

| papel | endereço | o que faz |
|---|---|---|
| **escritor** | `0x1D228`–`0x1D294` | `M = 0,2·min(W,H)`; `str d0,[x19,#0xA8]` |
| **leitor** | `0x294B0`–`0x294D8` | `M = 0,2·min(x4,x5)`, com `(x4,x5)` = `0x14D14` = `(Int(size.w·scale), Int(size.h·scale))` da pilha |
| (terceiro) | `0x3018` | outra função, não usada aqui |

> **`maxDistance = 0,2 × min(W, H)`, em pixels do `bakedSize`.** Constante
> relativa, não absoluta. O leitor **não a lê do arquivo** — ele a recalcula; é
> uma convenção entre as duas metades.

E ela **se cancela**: o escritor grava `u = 0,5 + d/(2M)` (§3.2) e o leitor
(laudo irmão §3.1) decodifica `sd = (2u − 1)·M`. Logo **`sd = d`**, a distância
verdadeira em texels, para `|d| < M`. `M` só age como **grampo**.

### 2.2. A chamada ao RenderBox

`[BIN] IR 27` `0x1D9D4`–`0x1D9FC`: `addStyle:data:` com **tipo 10** e o bloco
`{M, −M, 1.0, radius, flags}`. No RenderBox o tipo 10 é
`add_style(RBDrawingState*, RBDisplayListDistanceFilter const*)` (`0x3F27C`), e
os invólucros públicos dão nome aos campos:

```
-[RBDisplayList addDistanceFilterWithZeroDistance:oneDistance:scale:flags:]  0x3F390 -> {z, o, s, -1, f}
-[RBDisplayList addDistanceFilterWithMaxDistance:scale:flags:]               0x3F354 -> {m, -m, s, -1, f}
```

O IconRendering usa a **forma de `maxDistance`**: `zero = +M`, `one = −M`,
`scale = 1`.

Resto do gerador, lido:

| item | valor | endereço |
|---|---|---|
| textura | `(W+2) × (H+2)`, moldura de 1 texel; conteúdo em `translateByX:1 Y:1` | `0x1D210`–`0x1D224`, `0x1D354`, `0x1D7F4` |
| formato | `precisePixelFormatThreshold < min(W,H)` ? `RGBA16Float` (115, 8 bpp) : `RGB10A2Unorm` (90, 4 bpp) | `0x1D248`–`0x1D26C` |
| raio | `min(maxRelativeSmoothing·min(W,H), 3,5·√2)` (`0x4013CC8A99AF5453`) | `0x1D880`–`0x1D898` |
| `RBProjectVersion(8,0,80)` | se falso, raio ao quadrado; o RB embarcado é **8.0.84** (`@(#)PROGRAM:RenderBox PROJECT:RenderBox-8.0.84`), então vale o raio | `0x1D9A0`–`0x1D9C0` |
| flags | `0x262` ou `0x662` → `format = 1`, `edges = 1` ou `3` | `0x1D9C8`–`0x1D9D0` |
| filtro depois | shader **`clampToEdges`** (small string `0x1DA4C`–`0x1DA64`), retângulo = canvas recuado por `clampThreshold·(W,H)` | `0x1DA08`–`0x1DB9C` |

Os stubs foram nomeados pela tabela de símbolos indiretos (armadilha 2):
`0x8DC44` = `_RBProjectVersion`, `0x8DA34` = `_CGRectInset`; no RB,
`0x154680` = `_frexpf`, `0x1546BC` = `_ldexpf`.

### 2.3. Os quatro botões de `SDFGeneration` — NÃO estão mortos

`[BIN] IR 27` `0x1D1F8`–`0x1D204` lê `[box+0x1D8/0x1E0/0x1E8/0x1F0]` depois de
`swift_beginAccess(box+0x10)`. `box+0x1D8` = `params+0x1C8`, confirmado por três
fontes: a ordem da reflexão (`sdfGeneration` imediatamente antes de
`blurStrengthMax`), o `blurStrengthMax` de doc 03 §29.3 em `params+0x1E8`
(lido como `[box+0x1F8]`), e o getter `0x61AA4` do mesmo §29.3. Os padrões, do
construtor agregado `0x5E838`:

| campo | padrão | escrita |
|---|---|---|
| `clampThreshold` | **0,002** | `0x5EA9C`–`0x5EAAC`, imediato `0x3F60624DD2F1A9FC` (não está no pool) |
| `useAdvancedStacking` | **true** | `0x5EAB0`, `w22 = 1` de `0x5E8BC` |
| `precisePixelFormatThreshold` | **256** | `0x5EAB4`, `w20 = 0x100` de `0x5E9C4` |
| `maxRelativeSmoothing` | **0,005** | `0x5EAC8`, pool `0x98620` |

`[OBS]` O uso de `useAdvancedStacking` (guardado em `[x19+0x44]`/`[x19+0x1F0]`)
não foi seguido.

### 2.4. Em que grade

`[BIN] IR 27` A grade é o **`bakedSize`**: o laço por camada (`0x15D10`) recebe
a cópia de `[x19+0x78]`, que é o que vai para `FinalizedIcon+0x18`
(`0x15674`–`0x15684`), enquanto `requestedSize` vai para `+0x00`. E
`bakedSize = (round(w·scale), round(h·scale))` sobre o retângulo de `0x4202C`
(`0x159C0`) — **acompanha o tamanho pedido**. Isto transforma em `[BIN]` o que
o laudo irmão §4.1 tinha como `[INF]`: um termo de grade é constante em
**pixels**, e continua eliminado.

---

## 3. A transformada, no `RenderBox` 8.0.84

### 3.1. O algoritmo — pergunta 4

`[BIN] RB 8.0.84` `RB::Filter::(anon)::DistanceRenderer::render`
(`0x9342C`), máquina de estados em `[this+0x38]`, com o shader
`RB::Shader::filter_distance` (`metallib-renderbox/default_mod74.ll`, modo em
`shader_state >> 16 & 0xF`):

| estado | modo | o que faz |
|---|---|---|
| 2 | 2 | **semente edtaa** (Gustavson & Strand): gradiente de **Sobel** (pesos 1-2-1, `0xH4000`) do alfa, normalizado, e uma tabela 2D `edgedf` amostrada em `(alfa, min(|gx|,|gy|)/max(|gx|,|gy|))` — `RB::Device::distance_texture()` (`0xD5EB8`, ligada em `0x93858`) |
| 3 … | 3 | **jump flooding**, 8 vizinhos a `±passo`, fica o menor `|v|²`; passo inicial `2^(n−1)` com `n = ceil(log2 max(|zero|,|one|))` grampeado em `[2,12]` (`frexpf`/`ldexpf`, `0x92FCC`–`0x9301C`), e cai à metade enquanto > 0,75 |
| último 3 | 4 + 6 | passo 1 com **refinamento sub-texel** ao longo da tangente (`|v|² < 4`), e em seguida a **codificação** |
| 7/8 | blur | `NarrowBlurKernel::get(min(r², 12,25))`, separável, repetido enquanto sobra variância > 0,01 |
| 9/10 | 9/10 | normal = diferença central de 4 taps (passo 1) sobre o campo **desfocado**, codificada por `format` |

O gêmeo de CPU (`RB::CGContext::apply_distance_filter`, `0xC1F64`) desfoca com
`gaussian_kernel_` (`0xC35F4`): `w(x) = exp(−0,5·x²/r²)`, meia-largura
`ceil(2,8·r)` (`0x15ECF0`) — **sigma = raio**, a mesma variância que o GPU
reparte em pedaços de `12,25 = 3,5²`.

### 3.2. Onde cai o zero, e o teste de dentro/fora — perguntas 2 e 3

`[BIN] RB 8.0.84` **lido por dois caminhos independentes**:

```
GPU  DistanceRenderer::render  0x937A4-0x937E4:  s = 1/(zero - one);  b = s * zero
CPU  apply_distance_filter     0xC25F8-0xC2630:  s = 1/(zero - one);  b = s * zero
     (zero == one -> (s, b) = (1.0, 0.5), 0x15F170)
shader (modos 5/6): d = |v|,  d = -d se alpha < 0.5,  u = saturate(d*s + b)
```

Com `(zero, one) = (+M, −M)`: **`s = 1/(2M)`, `b = 0,5`**. O zero do campo é
`u = 0,5` exato, e ele cai onde `d = 0` — no contorno `alfa = 0,5`, que é onde a
semente o põe (modo 0: `alfa + (−0,5)`, `0xHB800`; modo 2: `edgedf`, nula em
`alfa = 0,5` por construção).

> **Pergunta 3.** O teste é **`alpha < 0,5` → fora**, isto é, `alpha >= 0,5` →
> dentro (`fcmp olt half %88, 0xH3800`). **O nosso `alpha >= 0.5` deixa de ser
> `[OBS]` e passa a `[BIN] RB 8.0.84`** — o `[OBS]` de `vidro-sobre-raster` §7.3
> e de `campo-de-distancia` §7.4 está **fechado**. O que difere é o método (EDT
> exata aqui, JFA + edtaa lá), com a mesma classe de semente sub-texel.
>
> **Pergunta 2.** **Não há contorno recuado.** A hipótese da frente irmã é
> refutada pelo binário: o único deslocamento possível estaria em `b`, e `b` é
> `0,5` por aritmética lida duas vezes. No formato do alvo a 1024
> (`RGBA16Float`, `256 < 1024`) `0,5` é exato; em `RGB10A2Unorm` (bake ≤ 256) o
> arredondamento custa `2M·(1/2046) ≤ 0,05` texel.

---

## 4. A régua dimensional, aplicada ao gerador inteiro

| termo do gerador | valor | unidade | passa na régua? | move a banda? |
|---|---|---|---|---|
| `b` (zero da codificação) | `0,5` exato | — | — | **não**: é zero |
| `maxDistance` | `0,2·min(W,H)` | canvas (relativo) | sim | **não**: se cancela; só grampeia a `±204,8` un. |
| semente edtaa / JFA | ≤ ~0,5 texel | pixel | **não** | — |
| moldura de 1 texel | escala `W/(W+2)` | pixel | não | — |
| `clampToEdges` | `0,002·W` da borda | canvas | sim | não: só a 2 un. da borda do canvas |
| **desfoque do campo** | sigma = `min(0,005·min(W,H), 3,5√2)` px = **5,12 un.** até 990 px | **canvas** | **sim** | medido abaixo |

O desfoque é o **único** termo do gerador que passa na régua. Ele não move uma
aresta reta (uma rampa linear convolvida é a mesma rampa); em curva move
`σ²κ/2`. Como a leitura decide o número, ele foi **aplicado e medido**.

### 4.1. A medida

Instrumento de `realce-forma.md` §1 (banda = pixels onde o render com especular
difere do sem especular; erro médio RGB contra o gabarito nela), refeito no
scratchpad com duas chaves de ambiente temporárias, **revertidas**. O
pós-processo aplicado: grampo em `±M`, gaussiana separável `σ = raio`
(`clamp_to_edge`), normal refeita por diferença central.
`AppIcon-27-defs.icon`, Release:

| tamanho | banda (px) | sem realce | base | **com desfoque** |
|---|---|---|---|---|
| 412 (em 512) | 13 532 | 20,29 | 22,62 | **22,61** |
| 206 (em 256) | 3 754 | 20,18 | 24,64 | **24,69** |

Global (`png-diff.py --legacy-inset`, `AppIcon-27.icon` a 412): **8,85 / 10,18 /
9,85**, alfa 4,95 — igual ao baseline em duas casas; peso da banda de silhueta
inalterado (2.524 px).

> **Neutro nos dois tamanhos.** O desfoque é `[BIN]` e não move a banda. Por ser
> neutro e mover pixel em toda camada de vidro, **não foi ligado**: fica
> transcrito neste laudo e no comentário de `GlassSpecular.h`.

(Os números de banda diferem dos de `realce-forma` — 23,42/19,78 sobre
13.498 px — porque a árvore mudou desde então; a ordem base > sem-realce é a
mesma.)

---

## 5. Veredito sobre o deslize

**Não fechou.** O número que o binário dá para o recuo do gerador é **zero**. Os
dois bytes que a frente irmã pediu estão lidos (`M = 0,2·min(W,H)`,
`tex.r(alfa=0,5) = 0,5`) e nenhum dos dois carrega os 7,5 unidades.

`[OBS]` **O deslize de 7,5 ± 0,9 unidades mora fora do gerador.** Com o gerador,
o zero, o formato, a escala e a grade fechados, e o consumidor já lido pela
frente irmã (um só termo de origem, `inset = 0`), o que resta é o **uso** de
`sd` pelo shader do realce (`height`, `spread'`, `bias'`, `curvature`, a
direção) ou a geometria da **arte** que entra no gerador — o `displayList` de
`SDF.SourceLayer`, desenhado em `0x1D1B0` com `drawLayerWithAlpha:blendMode:`
depois de `scaleByX:Y:` (`0x1CE5C`), e cuja relação com o que nós rasterizamos
não foi conferida. Nenhum número foi aplicado.

---

## 6. `[OBS]` fechados de carona

| `[OBS]` | onde | agora |
|---|---|---|
| o teste `alpha >= 0,5` nunca veio do binário | `vidro-sobre-raster` §7.3, `campo-de-distancia` §7.4 | **`[BIN] RB 8.0.84`** (`default_mod74.ll`, modos 0/2/5/6) |
| os três botões de `SDFGeneration` não existem como código | `semente-aa`, `sdf-nivel-zero` §1, `DistanceField.h` | **REFUTADO**: lidos em `0x1D1F8`; padrões em §2.3 |
| a grade e a transformada estão atrás do `TXRTexture` | `vidro-sobre-raster` §2, doc 03 §40 | **lidas**: `bakedSize + 2`, edtaa + JFA (§2.4, §3.1) |
| o gerador mora no CoreUI | `sdf-nivel-zero` §5 | **REFUTADO**: CoreUI só busca (§1) |
| o meio texel só cai se a grade acompanhar o render | `sdf-nivel-zero` §4.1 (`[INF]`) | **`[BIN]`**: `bakedSize` acompanha `requestedSize` (`0x159C0`) |
| o `maxDistance` precisaria ser ≥ 1912 para a convenção caber | `sdf-nivel-zero` §4 | **moot**: `M` se cancela e `b = 0,5` exato |

Comentários corrigidos no lugar (sem mudar código executável):
`Source/RenderBox/DistanceField.h` (os dois blocos que diziam "botões mortos" e
"grade não lida") e `Source/RenderBox/GlassSpecular.h` (o `[OBS]` dos dois
bytes). `[OBS]` A string de nota em `IconRenderer.cpp:373` ainda diz que os
botões "estao nomeados e nao lidos"; ela é saída do renderizador e não foi
tocada aqui para não mudar o stdout de que testes possam depender.

---

## 7. O pixel, a suíte e o tempo

Release, `-DIC_BUILD_UI=OFF`, `ic_tests` + `icrender`, com
`IC_CORPUS_DIR=References/corpus`. O diff do commit é este laudo e comentários.

| render | camadas | tempo | SHA-256 |
|---|---|---|---|
| `AppIcon-27.icon` @ 412 | **6 of 6** | 0,733 → 0,323 s (cache de primeira execução) | `9776C1F6…6BF89C9D` = `9776C1F6…6BF89C9D` |
| `GoWToolkit.icon` @ 1024 `--idiom square` | **1 of 1** | 0,567 → 0,527 s | `B8B79371…2938B736` = `B8B79371…2938B736` |

Suíte: **653 casos, 0 falhas** (baseline de UI OFF), 31,6 s.
