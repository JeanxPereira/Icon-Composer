# Laudo — o consumo de `translucency`

*2026-09-15. Fecha o item 3 da lista "o que um implementador ainda não tem" do
§29.8: `translucency` sai do campo `+0x10` do `Icon.GlassMaterial` e chega ao
pixel. Este documento é laudo, não implementação: nenhuma linha de render foi
escrita.*

Selos: `[BIN]` medido no binário (com endereço), `[ART]` medido no corpus,
`[OBS]` pergunta aberta, `[INF]` inferência marcada como tal.

Alvo: `References/2.0-125/out/slices/IconRendering.arm64` (fatia fina, VA ==
offset de arquivo). Toda a numeração abaixo é desta fatia.

---

## 0. A resposta, em uma frase

`[BIN]` `translucency` **não é multiplicador de alpha da camada nem modo de
mescla**. Ele é o **fator de interpolação de uma máscara de opacidade guiada
pelo campo de distância**: multiplicado por uma força que varia com o tamanho do
ícone, ele interpola cada opacidade nomeada do
`ICRRenderingParameters.glyphTranslucency` **a partir de 1.0 (opaco)**, e o
resultado vira os argumentos `opacityBounds` / `contourOpacityBounds` do shader
`simplifiedShapeAwareGradientMask`.

```
f      = glyphTranslucency.strength[classeDeTamanho] × material.translucency
eff(x) = 1 − (1 − x) · f                    ; ou seja, mix(1.0, x, f)
```

`translucency = 0` dá `eff(x) = 1.0` para todo `x` — opaco, efeito nenhum.
`translucency = 1` com `strength = 1` dá `eff(x) = x` — o perfil de opacidade
que os parâmetros descrevem, inteiro. É uma **torneira**, não uma escala.

> **Por que não havia `translucencyMax`/`translucencyPower`.** A pergunta do
> §29.8 pressupunha a forma de `blurStrength` e `refractionHeight`: um par
> `Max`/`Power` em `ICRRenderingParameters` que desnormaliza um `[0,1]` para
> pontos de tela. Não há esse par porque **o destino já é uma opacidade**, e
> opacidade não precisa de desnormalização. O que o `ICRRenderingParameters`
> tem, no lugar do par, é um sub-struct inteiro — `glyphTranslucency:
> TranslucencyEffect`, oito campos — e `translucency` é o **peso** com que esse
> sub-struct é aplicado. A "outra forma" que o §29.8 suspeitava existir é essa.

---

## 1. Onde é LIDO depois do transporte — endereço

`[BIN]` **`0x000FDE0`**, a instrução `ldr d2, [x1, #0x10]`, dentro da função
`0x0000FD28`–`0x000108A8`.

`[BIN]` O `x1` é um `Icon.GlassMaterial` passado indiretamente, e isso não é
suposição de offset: no sítio de chamada `0x0004AD70` (função `0x0004AC84`) o
buffer é montado imediatamente antes em

```
0x0004ACF4  stp  q1, q0, [sp]          ; +0x00 .. +0x1F
0x0004ACFC  str  q0, [sp, #0x20]       ; +0x20 .. +0x2F
0x0004AD04  strb w8, [sp, #0x30]       ; +0x30, um byte
0x0004AD08  ldr  d0, [sp, #0x10]       ; e o próprio chamador relê +0x10
0x0004AD48  mov  x1, sp
```

`0x31` bytes com **um byte final em `+0x30`** é, campo a campo, o layout que o
§29.2 mediu para o `Icon.GlassMaterial` (`specularPlacement` é o byte de
`+0x30`). O `+0x10` desse buffer é o `translucency` cujo *getter* o §29.2 leu em
`0x38DF0`.

`[BIN]` A função `0x0000FD28` tem três chamadores: `0x00044118`, `0x00045EC8`,
`0x0004AD70`. Os três passam material em `x1` e o contexto de desenho em `x0`.

---

## 2. Sofre aritmética? Sim — e ela tem duas metades

### 2.1. O sub-struct que faltava: `ICRRenderingParameters.glyphTranslucency`

`[BIN]` O §18.3 já tinha visto o nome `TranslucencyEffect` na lista de
sub-estruturas, e ninguém tinha voltado nele. O descritor de campos em
`0xA49FC` dá os oito, e — o que muda tudo — o tipo do quarto:

| # | campo | tipo | como foi lido |
|---|---|---|---|
| 0 | `borderWidth` | `Double` | `Sd` no descritor |
| 1 | `replicateBadSmoothing` | `Bool` | `Sb` |
| 2 | `useSimpleMask` | `Bool` | `Sb` |
| 3 | `strength` | **`IconRendering.SizeBasedValue`** | referência simbólica indireta em `0x9CF6A` → slot `0xC5758` → descritor `0xA0230` |
| 4 | `lowerOpacity` | `Double` | `Sd` |
| 5 | `upperOpacity` | `Double` | `Sd` |
| 6 | `lowerContourOpacity` | `Double` | `Sd` |
| 7 | `upperContourOpacity` | `Double` | `Sd` |

`[BIN]` E `SizeBasedValue` (metadado em `0xA5780`) tem **quatro** campos, nesta
ordem: `display`, `large`, `medium`, `small`. Há, a três entradas dele no mesmo
metadado (`0xA5800`), um enum de quatro casos na ordem **inversa**: `small`,
`medium`, `large`, `display`.

`[INF]` Portanto `strength` é um `Double` por classe de tamanho do ícone, e o
índice do enum caminha ao contrário da ordem de declaração. A prova disso não é
a leitura do metadado — é a tabela de seleção do §2.3, que casa exatamente com
essa inversão.

`[BIN]` **O layout, medido.** `ICRRenderingParameters` está embutido em
`self+0x68` na classe de desenho (§29.4). Com `strength` ocupando 32 bytes, o
sub-struct fica assim, e a função `0x0000FD28` lê os **oito campos, em ordem, em
sete instruções consecutivas** — que é o controle que dispensa aritmética de
offset:

| campo | em `ICRRenderingParameters` | em `self` | instrução que lê | default |
|---|---|---|---|---|
| `borderWidth` | `+0x2B8` | `+0x320` | `0x0000FD74  ldr d0, [x0, #0x320]` | `0.0` |
| `replicateBadSmoothing` | `+0x2C0` | `+0x328` | `0x0000FD80  ldrb w24, [x0, #0x328]` | `true` |
| `useSimpleMask` | `+0x2C1` | `+0x329` | `0x0000FD84  ldrb w8, [x0, #0x329]` | `true` |
| `strength.display` | `+0x2C8` | `+0x330` | via `csel` (§2.3) | `1.0` |
| `strength.large` | `+0x2D0` | `+0x338` | via `csel` | `1.0` |
| `strength.medium` | `+0x2D8` | `+0x340` | via `csel` | `1.0` |
| `strength.small` | `+0x2E0` | `+0x348` | via `csel` | `1.0` |
| `lowerOpacity` | `+0x2E8` | `+0x350` | `0x0000FD8C  ldr d0, [x0, #0x350]` | `0.0` |
| `upperOpacity` | `+0x2F0` | `+0x358` | `0x0000FD90  ldr d1, [x0, #0x358]` | `1.0` |
| `lowerContourOpacity` | `+0x2F8` | `+0x360` | `0x0000FD98  ldr d10, [x0, #0x360]` | `0.0` |
| `upperContourOpacity` | `+0x300` | `+0x368` | `0x0000FDA4  ldr d11, [x0, #0x368]` | `1.0` |

Tamanho do sub-struct: `0x50`.

`[BIN]` **Os defaults** saem do mesmo inicializador agregado do §19 (`0x5E844`,
buffer estático em `0xCDFD0`), nas escritas `0x5ECE0`–`0x5ECF4`:

```
0x0005ECE0  str  xzr, [x19, #0x2b8]        ; borderWidth = 0.0
0x0005ECE4  strh w25, [x19, #0x2c0]        ; w25 = 0x101 (posto em 0x5EB54)
                                           ;   -> replicateBadSmoothing = true
                                           ;   -> useSimpleMask         = true
0x0005ECE8  str  x23, [x19, #0x2c8]        ; x23 = 0x3FF0000000000000 (0x5EAD8) = 1.0
0x0005ECEC  fmov v0.2d, #1.00000000
0x0005ECF0  ldr  q1, [sp, #0x30]           ; spill de 0x5E964, pool 0x971F0 = (1.0, 0.0)
0x0005ECF4  stp  q0, q1, [x19, #0x2d0]     ; 1.0, 1.0, 1.0, 0.0
0x0005ED00  stp  q1, q0, [x19, #0x2f0]     ; 1.0, 0.0, 1.0, (0.15 -> contourGradients)
```

`[BIN]` Os pools deste inicializador foram relidos e batem com os do §29.3
(`0x985D0` = `0.005, 64.0`; `0x985E0` = `12.8, 256.0`; `0x985F0` = `1.0, 640.0`),
o que valida a régua com que a tabela acima foi lida.

`[INF]` **O default é um no-op deliberado.** `strength = (1,1,1,1)` e
`lowerOpacity = lowerContourOpacity = 0.0`, `upperOpacity = upperContourOpacity
= 1.0` — mas `borderWidth = 0.0`, e sobretudo o `translucency` do documento é
quem abre a torneira. Sem o campo do material, `f = 0` e a máscara sai `1.0` em
todo pixel. É exatamente o comportamento que a régua do projeto registra hoje:
a camada desenha, mas opaca.

### 2.2. A fórmula, transcrita

`[BIN]` De `0x0000FDDC` a `0x0000FDF4`:

```
0x0000FDDC  ldr   d1, [x9]              ; k = strength[classeDeTamanho]   (x9 vem do §2.3)
0x0000FDE0  ldr   d2, [x1, #0x10]       ; t = material.translucency
0x0000FDE4  fmul  d12, d1, d2           ; f = k * t
0x0000FDE8  fmov  d8, #1.00000000
0x0000FDEC  fsub  d0, d8, d0            ; d0 entrou como lowerOpacity
0x0000FDF0  fmul  d0, d0, d12
0x0000FDF4  fsub  d0, d8, d0            ; lowerOpacity_eff = 1 - (1 - lowerOpacity) * f
```

`[BIN]` E a **mesma** fórmula, com o mesmo `f` (`d12`) e o mesmo `1.0` (`d8`),
em `0x0000FF48`–`0x0000FF50`, sobre `upperContourOpacity` (`d11`):

```
0x0000FF48  fsub  d0, d8, d11
0x0000FF4C  fmul  d0, d0, d12
0x0000FF50  fsub  d0, d8, d0           ; upperContourOpacity_eff
```

`[BIN]` `d8`, `d10`, `d11`, `d12` são callee-saved em AArch64 e não são
reescritos no caminho `0xFD28 → 0xFEA8 → 0xFF48` (as reatribuições em
`0xFE04`–`0xFE70` estão no **outro** ramo do `cbz` de `0xFE00`). A fórmula de
`0xFF48` usa, portanto, os valores carregados em `0xFDA4` e `0xFDE4`.

`[BIN]` **Não há `pow`, não há grampo, não há piso nem teto.** Os quatro
chamadores de `_pow` que o §29.4 inventariou (`0x125C8`, `0x125EC`, `0x12620`,
`0x4A728`/`0x4A768`/`0x4A998`/`0x4A9D8`) continuam sendo todos; nenhum deles é
esta função. `translucency` atravessa **cru até a multiplicação** — só é
multiplicado por `strength` e usado como peso de interpolação.

`[INF]` A assimetria vale registrar porque um transcritor a erraria: só
**`lowerOpacity`** e **`upperContourOpacity`** passam por `eff()`.
`upperOpacity` e `lowerContourOpacity` vão **crus** para o shader. Nos defaults
isso é invisível (`eff(1.0) = 1.0` e o par de contorno só entra com
`borderWidth ≠ 0`), mas com parâmetros carregados de um plist não é.

### 2.3. A seleção por classe de tamanho

`[BIN]` `0x0000FD9C`–`0x0000FDC8`:

```
0x0000FD9C  mov  w10, #0x469f
0x0000FDA0  ldrb w10, [x0, x10]          ; byte de classe de tamanho em self+0x469F
0x0000FD88  add  x9,  x0, #0x348
0x0000FDAC  add  x11, x0, #0x338
0x0000FDB0  add  x12, x0, #0x330
0x0000FDA8  cmp  w10, #2
0x0000FDB4  csel x11, x11, x12, eq       ; ==2 -> +0x338 ; senão +0x330
0x0000FDB8  add  x12, x0, #0x340
0x0000FDBC  cmp  w10, #0
0x0000FDC0  csel x9,  x9,  x12, eq       ; ==0 -> +0x348 ; senão +0x340
0x0000FDC4  cmp  w10, #1
0x0000FDC8  csel x9,  x11, x9,  gt       ; >1  -> x11
```

| `w10` | endereço | campo |
|---|---|---|
| `0` | `self+0x348` | `strength.small` |
| `1` | `self+0x340` | `strength.medium` |
| `2` | `self+0x338` | `strength.large` |
| `≥3` | `self+0x330` | `strength.display` |

`[INF]` O mapeamento é a inversão exata entre a ordem de declaração de
`SizeBasedValue` (`display, large, medium, small`) e a ordem do enum de tamanho
(`small, medium, large, display`). Quatro casos, quatro slots, ordem espelhada —
é o que valida os dois metadados um contra o outro.

---

## 3. Quem CONSOME — o destino, nomeado

### 3.1. A bifurcação: `useSimpleMask`

`[BIN]` `0x0000FE00  cbz w8, #0xFEA8`, com `w8` = `useSimpleMask` (lido em
`0xFD84`). São **dois caminhos de máscara**, e o default (`true`) é o que
**não** usa shader:

- **`useSimpleMask == true`** → cai em `0x0000FE04`, e em `0x0000FE98` chama
  `0x0000D28C` com `(d0 = 0.0, d1 = 1.0, d2 = 0.0625, d3 = upperOpacity,
  d4 = lowerOpacity_eff)`. `1 / 0.0625 = 16`: `0xD28C` amostra a rampa em
  **16 passos** e monta uma lista de paradas de gradiente. Nenhum shader é
  instanciado nesse ramo.
- **`useSimpleMask == false`** → cai em `0x0000FEA8` e instancia o shader.

`[BIN]` Dentro do ramo `true` há ainda `0x0000FE74  cbz w24, #0x10434`, com
`w24` = `replicateBadSmoothing`.

### 3.2. O shader, e ele tem nome

`[BIN]` `0x0000FFC8  bl #0x8ed60  ; _objc_msgSend$initWithLibrary:function:`, com
o literal Swift montado em `0x0000FF8C`–`0x0000FFB4`: ponteiro `0xA6190`,
comprimento `0x20` = 32 — **`"simplifiedShapeAwareGradientMask"`**.

`[BIN]` O metallib do `IconRendering`
(`References/2.0-125/out/metallib-iconrendering/`) exporta sete shaders:
`clampToEdges`, `clampedPlusL`, `glassHighlight`, `glow`, `sdfFill`,
`shapeAwareGradientMask`, `simplifiedShapeAwareGradientMask`. Os nomes dos
argumentos estão no metadado `air.visible` de cada módulo:

```
simplifiedShapeAwareGradientMask (default_mod4.ll:153)
  0 float2  position
  1 float   borderWidth
  2 float2  opacityBounds
  3 float2  contourOpacityBounds
  4 float   sdfScale
  5 float   sdfZero
  6 float4  bounds
  7 texture t
```

`borderWidth`, `opacityBounds`, `contourOpacityBounds`: os nomes dos argumentos
do shader são, campo a campo, os nomes do `TranslucencyEffect`. O par
`lower`/`upper` do struct é o `float2` do shader.

### 3.3. A ligação dos argumentos — sete `setArgumentBytes:`

`[BIN]` `0x00010074`–`0x00010204`, todos
`-[RBShader setArgumentBytes:atIndex:type:count:flags:]`:

| índice | `type` | conteúdo escrito | argumento do shader |
|---|---|---|---|
| `0` | `1` (float) | `borderWidth × (n − 2) / self[0x568]` (`0x10048`–`0x10054`) | `borderWidth` |
| `1` | `2` (float2) | `(lowerOpacity_eff, upperOpacity)` (`0x10078`–`0x10088`) | **`opacityBounds`** |
| `2` | `2` (float2) | `(d14, d15)` (`0x100AC`–`0x100B4`) | **`contourOpacityBounds`** |
| `3` | `1` (float) | `sdfScale × −2.0` (`0x100D8`–`0x100E8`) | `sdfScale` |
| `4` | `1` (float) | `0x3F000000` = **`0.5`** (`0x1010C`) | `sdfZero` |
| `5` | `4` (float4) | quatro acessores de retângulo (`0x10140`–`0x101B8`) | `bounds` |
| `6` | `7` (textura) | `0x101E0` | `t` |

`[BIN]` O par do índice `2` sai de um `fcsel` em `borderWidth == 0.0`
(`0x0000FF5C`–`0x0000FF70`):

```
borderWidth == 0 : (d14, d15) = (lowerOpacity_eff,     upperOpacity)          ; = opacityBounds
borderWidth != 0 : (d14, d15) = (lowerContourOpacity,  upperContourOpacity_eff)
```

`[INF]` Com `borderWidth = 0` os dois `float2` ficam **idênticos**, e a mistura
corpo/contorno do shader degenera — é assim que "sem borda" é expresso, sem um
booleano para isso.

`[BIN]` A conversão do índice `0` é **a mesma** que o §29.5 mediu para o campo
`height` do `glass-displacement`: `× (n − 2) / [self+0x568]`, com o mesmo
divisor. Duas funções vizinhas, a mesma conversão de pontos para texels do SDF —
é o que confirma que `x0` aqui é o mesmo `self` do §29.4/§29.5, e portanto que
`ICRRenderingParameters` está mesmo em `self+0x68`.

### 3.4. O que o shader faz com o par — transcrito do AIR

`[BIN]` `metallib-iconrendering/default_mod4.ll`, linhas 153–196:

```
d    = t.sample(position).r
sd   = (sdfZero - d) * sdfScale
u    = saturate(sd / borderWidth)                        ; 0 no contorno, 1 a borderWidth de fundura
v    = saturate((position.y - bounds.y) / bounds.w)      ; 0 no topo, 1 na base
corpo    = mix(opacityBounds.y,        opacityBounds.x,        v)
contorno = mix(contourOpacityBounds.y, contourOpacityBounds.x, v)
a    = mix(contorno, corpo, u)
a    = clamp(a, 0, 1)
a    = a*a*(3 - 2*a)                                     ; o polinômio do smoothstep
cov  = saturate(sd + 1)                                  ; cobertura antisserrilhada na borda
out  = mix(1.0, a, cov)
return half4(out, out, out, out)
```

`[INF]` "lower"/"upper" são **posição vertical**, não faixa numérica: `v` é o
parâmetro vertical dentro de `bounds`, `.y` (upper) vale no topo e `.x` (lower)
na base. Quem lesse `lowerOpacity`/`upperOpacity` como "mínimo e máximo de uma
normalização" inverteria a rampa.

`[BIN]` Para contraste, a variante **não** simplificada
(`default_mod2.ll:153`) troca a rampa vertical por
`saturate(dot(lightDirection.xy, opaqueEnd - position) / length)` e tem um único
`minOpacity` em vez dos dois `float2`. É a mesma máscara com a rampa alinhada à
direção da luz; a "simplificada" é a versão de eixo vertical.

### 3.5. O espelho em CoreImage, e o desenho

`[BIN]` `0x000102A0  _objc_msgSend$setCIFilterProvider:` instala o provedor cujo
corpo é a função `0x000108A8`–`0x00010C14`. Ela constrói um `CIFilter` por nome
e preenche as chaves, todas literais em `__cstring`:

| endereço | literal | chave |
|---|---|---|
| `0x000108DC` | `0xA61C0` `"CUISimplifiedShapeAwareGradientMask"` (35) | `filterWithName:` (`0x10904`) |
| `0x00010968` | `0xA61F0` `"inputBorderWidth"` | `setValue:forKey:` (`0x10990`) |
| `0x000109B4` | `0xA6210` `"inputOpacityBounds"` | `setValue:forKey:` (`0x109D8`) |
| `0x000109FC` | `0xA6230` `"inputContourOpacityBounds"` | `setValue:forKey:` (`0x10A20`) |
| `0x00010A78` | literal imediato | `"inputSDFScaleFactor"` (`0x10AB0`) |
| `0x00010B08` | literal imediato | `"inputSDFZeroValue"` (`0x10B2C`) |
| `0x00010B50` | literal imediato | `"inputBounds"` (`0x10B80`) |
| `0x00010BAC` | literal imediato | `"inputImage"` (`0x10BD8`) |

`[BIN]` O arquivo-fonte está escrito em `0xA616E`:
`"IconRendering/CustomShaders.swift"`.

`[INF]` É a ponte Metal↔CoreImage: o mesmo efeito, com os mesmos argumentos, para
o renderizador que não é o `RenderBox`.

`[BIN]` E o desenho fecha em:

```
0x000103F0  _objc_msgSend$setShader:bounds:flags:
0x000103F4  fmov s0, #1.00000000
0x00010408  _objc_msgSend$drawShape:fill:alpha:blendMode:
```

`[INF]` O `alpha` do `drawShape:` é a constante **`1.0`**. Isto é a prova
negativa que fecha a pergunta 3 do encargo: **`translucency` não vira o alpha
da chamada de desenho**. Ele já está dentro da máscara, pixel a pixel.

---

## 4. O `enabled` do documento

`[ART]` No corpus (`References/corpus`, 145 documentos), `translucency` aparece
**203 vezes e sempre como o par `{enabled, value}`** — nunca como escalar.
E `enabled: false` convive com valores diferentes de zero:

| `(enabled, value)` | ocorrências |
|---|---|
| `(true, 0.5)` | 116 |
| `(false, 0.5)` | 33 |
| `(true, 0.2)` | 15 |
| `(true, 0.25)` | 6 |
| `(true, 0.7)` | 4 |
| `(false, 0.2)` / `(false, 0.4)` | 2 cada |
| `(true, 0.0)` | 3 |

`[INF]` O `enabled` é, no documento, um interruptor que **preserva** o valor —
um documento desligado guarda `0.5` para quando for religado.

`[BIN]` O modelo do editor tem o par: `IconComposerFoundation.Translucency`
(`fieldmd_foundation.txt`, `0x12d070`) = `enabled: Bool`, `value: Double`, com
`CodingKeys` iguais. O `IconComposerKit` tem um `TranslucencyInspector` cujo
corpo é um `MultiToggle` (o interruptor) mais um `NumericTextField` (o valor),
com `WritableKeyPath<Translucency, Bool>` e
`WritableKeyPath<Translucency, CGFloat>` — duas propriedades independentes.

`[BIN]` **E o par morre na fronteira do render.** No modelo de render não existe
bit nenhum:

- `Icon.GlassMaterial` tem **um só** campo, `translucency: Double` (`+0x10`), e
  nada mais (descritor `0xA38FC`, oito campos, conferidos um a um).
- Varredura pelas oito fatias: `"setTranslucency:"` só existe no
  `IconRendering.arm64` (`0xB3876`, `0xF3D38`), e **não há nenhuma** ocorrência
  de `"translucencyEnabled"` nem de `"isTranslucent"` em fatia alguma.
- O `ICRRenderingParameters.glyphTranslucency` também não tem campo de
  habilitação: os oito campos do `TranslucencyEffect` estão listados no §2.1 e
  os dois `Bool` que há são `replicateBadSmoothing` e `useSimpleMask`.

`[OBS]` **O sítio exato da dobra não foi lido.** O `IconComposerKit` importa os
tipos do `IconRendering` só por referência simbólica (`0x1B974C`,
`0x1B975C`, `0x1B9764`), e a função que constrói o `Icon.GlassMaterial` a partir
do `Composition.Group` não foi localizada.

`[INF]` Mas a resposta à pergunta do encargo já está fechada pelo que **foi**
medido: **`enabled` não vira um bit à parte** — não existe onde ele caberia. E
`enabled = false` tem que virar `value = 0.0`, porque `f = strength × 0 = 0` faz
`eff(x) = 1 − (1 − x)·0 = 1.0` para todo `x`, que é exatamente "sem
translucidez". Qualquer outra dobra (mandar o `value` cru, por exemplo)
desenharia o efeito numa camada que o documento desligou. Um implementador deve
escrever `translucency = enabled ? value : 0.0` — e o `[OBS]` acima é sobre
*onde* a Apple faz isso, não sobre *o quê*.

---

## 5. `useSystemGlass` e `useOS26Compositing`

### 5.1. Os dois defaults, medidos

`[BIN]` No mesmo inicializador de `0x5E844`:

- `simulatedChiclet.useSystemGlass` — `ICRRenderingParameters+0x00` —
  `0x0005E880  strb wzr, [x19]` → **`false`** (já registrado no §19.3).
- As três últimas `Bool` do agregado são escritas em
  `0x0005EDB0  strh w22, [x19, #0x360]` e `0x0005EDB4  strb wzr, [x19, #0x362]`,
  com `w22 = 1` (o mesmo `w22` que o §19.3 leu como `true` em `+0x48` e
  `+0x220`):

| `ICRRenderingParameters` | campo | default |
|---|---|---|
| `+0x360` | `supportsChicletAlignmentForSystemFills` | `true` |
| `+0x361` | `recreateRadar153477135` | **`false`** |
| `+0x362` | `useOS26Compositing` | **`false`** |

### 5.2. `useOS26Compositing` liga um *color clamp* e um *headroom* — `[BIN]`

`[BIN]` Em `self` o campo fica em `+0x362 + 0x68 = +0x3CA`. O leitor decisivo é
`0x00043140` (função `0x00042FBC`):

```
0x0004313C  bl   #0x8e640      ; -[... clipShape:alpha:mode:]
0x00043140  ldrb w8, [x20, #0x3ca]     ; useOS26Compositing
0x00043148  cmp  w8, #1
0x0004314C  b.ne #0x431b0              ; false -> só beginLayerWithFlags:

; ramo true:
0x00043154  bl   #0x8e2a0      ; -[RBDisplayList addContentHeadroom:]
0x00043158  str  xzr, [sp, #0x30]      ; monta o blob do estilo
0x0004315C  str  wzr, [sp, #0x38]
0x00043160  stp  s8, s8, [sp, #0x3c]
0x00043164  str  s8, [sp, #0x44]
0x0004316C  ldr  d0, [x8, #0x7c8]      ; pool 0x977C8 = (uint32 4, uint32 0)
0x00043170  str  d0, [sp, #0x48]
0x00043180  bl   #0x8e3a0      ; -[RBDisplayList addStyle:data:] com w2 = 9
0x0004318C  bl   #0x8e480      ; -[... beginLayerWithFlags:] 0

; ramo false:
0x000431B4  bl   #0x8e480      ; -[... beginLayerWithFlags:] 0
```

`[BIN]` O estilo **9** foi nomeado pelo `RenderBox.arm64`, pela mesma tabela de
saltos que o §29.5 usou para o estilo 3. `RBDrawingStateAddStyle` (`0x40AB0`)
indexa a tabela de bytes em `0x15E440`; a entrada `[9]` vale `0x1A`, e o alvo é
`0x40B00 + 0x1A·4 = 0x40B68`. Esse bloco termina em:

```
0x00040BC0  bl #0x45048   ; RB::DisplayList::State::add_filter_style<RB::Filter::ColorClamp>
0x00040BD4  bl #0xeeb74   ; RB::XML::DisplayList::add_color_clamp_filter(..., RBDisplayListColorClamp const&)
```

`[BIN]` Portanto **`useOS26Compositing == true` acrescenta, antes de abrir a
camada, um `contentHeadroom` e um filtro `ColorClamp`** (`RBDisplayListColorClamp`,
espaço de cor `4`, flags `0`, os limites vindos de `s8`). `false` abre a camada
crua. É o caminho HDR/EDR: declarar o *headroom* do conteúdo e grampear a cor ao
compor.

### 5.3. `useSystemGlass` — não fechado, mas estreitado

`[OBS]` **Qual caminho `useSystemGlass` liga não foi lido.** O campo está em
`ICRRenderingParameters+0x00`, ou seja `self+0x68`, e `0x68` é um deslocamento
comum demais para que uma varredura de `ldrb` o isole; os candidatos não foram
separados.

`[BIN]` O que **foi** eliminado: `useSystemGlass = true` **não** pode alcançar o
`glassBackground_v1` a partir destes binários. O §29.1 já tinha medido que dos
cinco *system shaders* que o `RenderBox` exporta o `IconRendering` importa
exatamente um, `_RBSystemShaderDisplacementMap`, e que a string
`"glassBackground"` aparece 0 vez fora do `RenderBox`. Seja qual for o caminho
que `useSystemGlass` troca, ele não é o dos 75 uniforms.

---

## 6. O que foi eliminado, e por quê

Isto é entrega tanto quanto a fórmula: são as hipóteses que o §29.8 deixou
abertas e que agora estão fechadas pela negativa.

1. `[BIN]` **`translucency` não é um dos 56 modos de mescla** (§15/§26). Ele
   entra numa multiplicação de ponto flutuante em `0xFDE4` e termina como
   endpoint de `air.mix` dentro do shader. O `blendMode` do
   `drawShape:fill:alpha:blendMode:` de `0x10408` vem de outro registrador.
2. `[BIN]` **`translucency` não é seletor de caminho de composição.** A direção
   (a) do encargo — `useSystemGlass` / `useOS26Compositing` serem a resposta —
   está eliminada: os dois campos são lidos em funções diferentes
   (`0x43140` para o `useOS26Compositing`) e nenhum deles aparece na função
   `0xFD28`, que é onde `translucency` é consumido. Quem troca de caminho dentro
   do efeito de translucidez é `useSimpleMask` (`0xFE00`), que é um parâmetro de
   render e não vem do documento.
3. `[BIN]` **`translucency` não é nome em shader nenhum.** A busca por
   `translucen` (sem distinção de caixa) nos três dumps de metallib
   (`metallib/`, `metallib-iconrendering/`, `metallib-renderbox/`) devolve
   **zero** arquivos. O nome morre no lado ObjC/Swift; o que chega ao shader é
   `opacityBounds`.
4. `[BIN]` **Não há `translucencyMax`/`translucencyPower` porque não há
   desnormalização.** Os 35 campos de `ICRRenderingParameters` foram relidos no
   `fieldmd_iconrendering.txt`; o que existe no lugar do par é o sub-struct
   `glyphTranslucency`. E não há `pow` no caminho: os sete chamadores de `_pow`
   do §29.4 continuam sendo os mesmos sete.
5. `[BIN]` **`translucency` não é multiplicador de alpha da camada.** O `alpha`
   do `drawShape:` é a constante `1.0` (`0x103F4`).

---

## 7. O que continua `[OBS]`

1. `[OBS]` **O sítio da dobra `{enabled, value} → Double`.** Está entre o
   `IconComposerFoundation.Translucency` e o `Icon.GlassMaterial`, e não foi
   localizado (§4). O *quê* está fechado por eliminação; o *onde*, não.
2. `[OBS]` **Quem escreve o byte de classe de tamanho em `self+0x469F`.** O
   consumidor está lido (`0xFDA0`) e o mapeamento dos quatro casos também; a
   origem do byte, não.
3. `[OBS]` **De onde vêm os valores não-default de `glyphTranslucency`.** O
   `TranslucencyEffect` é `Codable` (nomes das `CodingKeys` em `0xA7250` e
   adiante), então há um plist/JSON de parâmetros fora destes binários que pode
   sobrescrever os defaults do §2.1. Esse arquivo não está em `References/`.
   Com os defaults, `borderWidth = 0` e o par de contorno nunca é usado — quem
   transcrever só os defaults desenha a rampa do corpo e mais nada.
4. `[OBS]` **Se `shapeAwareGradientMask` (a variante não simplificada) chega a
   ser instanciada.** O shader existe no metallib com os nomes de argumento
   lidos (§3.4), mas o único nome dessa família que aparece como literal no
   `IconRendering.arm64` é o `simplifiedShapeAwareGradientMask` (`0xA6190`).
   Isto **não** prova ausência: `sdfFill`, `glassHighlight`, `clampToEdges` e
   `clampedPlusL` também não aparecem como literais e certamente desenham — logo
   há outra rota de nomeação que não foi lida.
5. `[OBS]` **`useSystemGlass`** (§5.3).

---

## 8. Nota de método — o que foi tentado e não deu

`[BIN]` **Os dois MCPs de RE estavam indisponíveis.** O de Ghidra respondeu, mas
`list_instances` devolveu `{"instances": []}` — nenhuma instância aberta para
conectar. De IDA não há servidor configurado nesta sessão (a lista de
ferramentas só traz `mcp__ghidra__*` e `mcp__pcsx2__*`). Todo o laudo foi feito
com `capstone` e com o `scripts/macho.py` que o próprio repo já tinha, mais três
varredores escritos no *scratchpad* (xref de `adrp`/`add`, varredura de `ldr` por
offset imediato, e um leitor de `FieldDescriptor`/referência simbólica do Swift).

**O caminho errado que custou tempo, e a correção.** A primeira leitura do
layout do `TranslucencyEffect` supôs `strength: Double` — 8 bytes — e produziu
um sub-struct de `0x38` bytes. Com essa suposição a função `0xFD28` ficava
incoerente: o `csel` de quatro vias escolhia entre `{strength, lowerOpacity,
upperOpacity, lowerContourOpacity}`, o que não quer dizer nada. **A tentação era
declarar vitória assim mesmo** — a fórmula `1 − (1−x)·f` já estava lida e correta,
e o `ldr d2, [x1, #0x10]` já estava identificado. O que salvou foi a lição
registrada no §29.8: *fechar a forma não é fechar o destino*. Perguntar "por que
quatro slots?" levou à referência simbólica do tipo de `strength`, ao
`SizeBasedValue` de quatro campos, e ao enum de tamanho na ordem inversa — e aí
os oito campos passaram a ser lidos em sete instruções consecutivas, em ordem.
Uma transcrição feita com o layout de `0x38` teria escrito `opacityBounds` com
os campos errados e desenhado uma rampa plausível e errada em toda parte.

**O que não foi tentado.** Não se rodou o executável do projeto nem se
comparou pixel com o alvo: este laudo é leitura de binário e de corpus, e a
validação diferencial fica para quem implementar.
