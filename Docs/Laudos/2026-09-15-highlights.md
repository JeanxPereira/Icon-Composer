# Laudo — os 16.113 bytes do `Highlights`

*2026-09-15. Alvo: o `[OBS]` que o laudo do especular abriu há poucas horas —
"os nove valores que esse shader consome vêm de `ICRRenderingParameters.Highlights`,
um bloco de **16.113 bytes** que nenhum laudo deste repositório leu … **Nenhum
desses números foi lido. Nem um.**"*

**Lidos.** O bloco abriu, e abriu porque o próprio binário soletra o layout dele
em **imediatos** e não em constantes de *pool* — a lição que a frente do chiclet
deixou hoje de manhã (`mov`+`movk` em vez de `ldr`) valeu exatamente aqui.

O especular **desenha neste commit**. `[ART]` 67 de 145 documentos pediam um
brilho que não saía; num deles, medido, **67.285 pixels de 1.048.576 (6,42 %)**
mudaram.

E o resultado é maior do que a pergunta: **`hasSpecular` não liga um realce, liga
CINCO** — um aro nítido de luz-chave, uma lavagem difusa, um aro de preenchimento
a 180°, e o realce escuro desenhado **duas vezes**, espelhado a ±90°.

Todos os endereços são do slice
`References/2.0-125/out/slices/IconRendering.arm64` (VA == deslocamento de
arquivo). A reflexão é `References/2.0-125/out/fieldmd_iconrendering.txt`.

---

## 1. O layout, e por que ele não é chute

`[BIN]` `ICRRenderingParameters.Highlights` (`params+0x250`, `0x3EF1` = 16.113
bytes) é um preâmbulo de **280 bytes** e depois **dez `HighlightsSet`** de
`0x630` bytes de passo (tamanho `0x629`, o último sem enchimento):

| conjunto | desloc. | conjunto | desloc. |
|---|---|---|---|
| `chicletDefault` | `0x0118` | `glyphsDefault` | `0x2008` |
| `chicletBright` | `0x0748` | `glyphsBright` | `0x2638` |
| `chicletDim` | `0x0D78` | `glyphsDim` | `0x2C68` |
| `chicletClear` | `0x13A8` | `glyphsClear` | `0x3298` |
| `chicletScreened` | `0x19D8` | `glyphsScreened` | `0x38C8` |

```
0x118 + 9*0x630 + 0x629 == 0x3EF1        <- fecha exatamente
```

**E a aritmética não precisa ser acreditada: as dez constantes estão escritas.**
`[BIN]` Os dois seletores materializam os dez deslocamentos como imediatos e
copiam `0x629` bytes:

```
0x000627B4  (glifo)    mov w22, #0x3298   mov w23, #0x38c8
                       mov w8,  #0x2008 / #0x2638 / #0x2c68
                       mov w2,  #0x629 ; bl memcpy          (0x00062978)
0x00062588  (chiclet)  mov w22, #0x13a8   mov w23, #0x19d8
                       add x1, x20, #0x118 / #0x748 / #0xd78
                       mov w2,  #0x629 ; bl memcpy          (0x0006276C)
```

> Dez ponteiros calculados por soma de imediato, um tamanho repetido em dois
> sítios, e um total que bate com a alocação. É o binário concordando consigo
> mesmo por três caminhos.

`[BIN]` Um `HighlightsSet` são **seis `HighlightSettings` de passo `0x108`**,
lidos direto dos seis destinos de `memcpy` de `0x00030EC0`–`0x00030F1C` e dos
seis do construtor (`x19+0x118 … x19+0x640`):

```
keySharp 0x000   keyDiffuse 0x108   fillSharp 0x210
fillDiffuse 0x318   dark 0x420   rim 0x528       (0x528 + 0x101 = 0x629)
```

### 1.1. `HighlightSettings`, dez campos, `0x101` = 257 bytes

`[BIN]` A ordem é a do metadado (`fieldmd 0xA33C4`); os deslocamentos são as
próprias cargas do resolvedor em `0x0004BDCC`–`0x0004BE50`:

| desloc. | campo | tipo | lido em |
|---|---|---|---|
| `+0x00` | `brightness` | `Double` | `0x4BDCC` |
| `+0x08` | `opacity` | `SizeBasedValue<Double>` | `0x4BDCC`–`0x4BDDC` |
| `+0x28` | `outsetOpacity` | `SizeBasedValue<Double>?` | `0x4BDE0`–`0x4BDEC`, **tag `+0x48`** (`0x4BDF0`) |
| `+0x50` | `distance` | `SizeBasedValue<Double>` | `0x4BDF4`–`0x4BE00` |
| `+0x70` | `minDistancePixels` | `SizeBasedValue<Double>` (**não** opcional) | `0x4BE04`–`0x4BE10` |
| `+0x90` | `inset` | `SizeBasedValue<Double>` | `0x4BE14`–`0x4BE20` |
| `+0xB0` | `minInsetPixels` | `SizeBasedValue<Double>?` | `0x4BE24`–`0x4BE30`, **tag `+0xD0`** (`0x4BE34`) |
| `+0xD8` | `spread` | `SizeBasedValue<Angle>` | `0x4BE38`–`0x4BE44` |
| `+0xF8` | `bias` | `Double` | `0x4BE48` |
| `+0x100` | `blendModeOverride` | `Icon.BlendMode?` | `0x4BE50` |

`[BIN]` **A prova de que a régua está certa é o uso, não o tamanho.** Em
`0x0004BEFC`–`0x0004BF04` o campo de `+0x50` e o de `+0x70` entram num `max`
com o de `+0x70` multiplicado por uma escala — que é literalmente
`max(distance, minDistancePixels × pixelUnit)`. Os dois nomes do metadado
descrevem essa conta e nenhuma outra. O mesmo par em `+0x90`/`+0xB0` para
`inset`/`minInsetPixels`, `0x0004BF28`–`0x0004BF30`.

### 1.2. Um byte, três tabelas de tag — e é assim que `nil` é escrito

`[BIN]` O byte `+0x100` é ao mesmo tempo o `blendModeOverride` e o
discriminador dos dois `Optional`/`enum` que envolvem o struct. Três leitores,
um byte, zero armazenamento extra:

| leitor | sentinela | significado |
|---|---|---|
| `0x00035450` | `max(0, byte − 18)` | `byte == 19` → **`HighlightSettings? == nil`** |
| `0x00033F04` | `max(0, byte − 19)` | `byte == 19` → **`FillHighlights.matchKey`** |
| `0x00035470` | `max(0, byte − 20)` | `byte == 20` → **`FillHighlights? == nil`** |

e `byte == 18` (`0x12`) é `blendModeOverride == nil` com o `HighlightSettings`
presente — que é o que **todos** os doze blocos construídos escrevem
(`mov w25, #0x12`, `0x00062BB8`; `mov w24, #0x12`, `0x000646DC`).

> **Esta é a armadilha desta família.** Um leitor que tome o byte como o modo de
> mescla lê `18` e vai à tabela de 18 do §17.3 procurar o índice 18, que não
> existe. Um leitor que o tome como um `Optional<Bool>` perde `matchKey`. É o
> mesmo truque de *extra inhabitants* que a sombra encontrou no `ringWidth`,
> levado a três níveis.

---

## 2. A regra de seleção, medida

`[BIN]` `0x0005E194` instala o *closure* `0x000627B4` em `x1` e cai em
`0x0005E59C`, que copia os **`0x3EF1` bytes inteiros** para a pilha
(`mov w2, #0x3ef1 ; bl memcpy`, `0x0005E684`) — é por isso que a função reserva
`0x41C0` bytes de pilha — e chama o *closure* com `x20` apontando para a cópia.

`[BIN]` `0x000627B4` é o caminho do **glifo**, e escolhe por **duas** coisas,
nenhuma delas a classe de tamanho:

```
if (fill[+0x90] == 1 && (fill[+0x68] | +0x70 | +0x78 | +0x80 | +0x88) == 0) {
    // preenchimento SIMPLES: cinco ponteiros nulos
    switch (fill[+0x5B]) {                       // 0x00062948
        case 0: glyphsDefault (0x2008)
        case 1: glyphsBright  (0x2638)
        default: glyphsDim    (0x2C68)
    }
} else {
    // duas comparações de forma, 0x00040E60 e 0x0006D6B0
    glyphsScreened (0x38C8)  ou  glyphsClear (0x3298)
}
```

`[BIN]` `0x00062588` é o caminho do **chiclet** e tem a mesma forma, com os
cinco deslocamentos `chiclet*` e **um portão a mais**: `[Highlights+0x110]`,
que é `chicletHighlightsAppearanceMode` (`0x000625B4`), e um segundo byte do
preenchimento em `+0x61` (`0x00062698`).

`[OBS]` **O que `fill[+0x5B]` É não foi lido.** A forma de três e os dois
limiares do preâmbulo — `maxDimChicletLuminance = 0.2` e
`minBrightChicletLuminance = 0.99` (`Highlights+0x98` e `+0xA0`, do *pool*
`0x986E0`) — dizem *classe de luminância*, mas a escrita do byte não foi achada.

**E isto NÃO MUDA NENHUM PIXEL nesta versão, o que é `[BIN]`:**
`0x00063AEC`–`0x00063BF4` constrói os cinco conjuntos `glyphs*` chamando a
**mesma** fábrica `0x00064604` com os **mesmos três** argumentos. Os cinco são
idênticos byte a byte. Medir o índice com mais força não moveria um pixel.

---

## 3. Um conjunto vira até SETE realces

`[BIN]` `0x00030E88` (1.516 bytes) expande um `HighlightsSet` num array de
`IconRendering.Highlight` — tamanho `0x131`, passo `0x138`:

```
+0x000  settings   HighlightSettings (0x101, preenchido até 0x108)
+0x108  angleFromKey   Double
+0x110  curvature      SizeBasedValue<Double>
+0x130  isDarklight    Bool
```

`[BIN]` Sete posições, escritas em `x19+0x20 + i*0x138`:

| i | ajustes | `angleFromKey` | `curvature` | `isDarklight` | endereço |
|---|---|---|---|---|---|
| 0 | `keySharp` | `0` | `glyphHighlightCurvature` | não | `0x30F70`–`0x30F94` |
| 1 | `keyDiffuse` | `0` | `[1,1,1,1]` | não | `0x30FD4`–`0x30FF8` |
| 2 | `fillSharp` (`matchKey` → `keySharp`) | `+π` | `glyphHighlightCurvature` | não | `0x3108C`–`0x310C0` |
| 3 | `fillDiffuse` (`matchKey` → `keyDiffuse`) | `+π` | `[1,1,1,1]` | não | `0x31154`–`0x31188` |
| 4 | `dark` | `+π/2` | `glyphDarklightCurvature` | **sim** | `0x311F8`–`0x3122C` |
| 5 | `dark` | `−π/2` | `glyphDarklightCurvature` | **sim** | `0x3129C`–`0x312D0` |
| 6 | `rim` | `0` | `glyphHighlightCurvature` | não | `0x31310`–`0x31334` |

e `0x00031338`–`0x000313EC` percorre os sete e **descarta** todo aquele cujos
ajustes eram `nil` (`0x00022A38` lê o byte `+0x130`; `0x00022A54` é quem escreve
o `2` que marca "vazio").

`[BIN]` Com os padrões de glifo desta versão, `fillDiffuse` e `rim` **são `nil`**
(`0x00033FA4` escreve `20`; `0x00033DE4` escreve `19`). **Sobram cinco**, e o
escuro é desenhado duas vezes, espelhado.

> **Este é o achado que muda a figura mental.** O `[OBS]` que esta frente foi
> fechar falava em "os nove valores que esse shader consome", no singular. Não é
> um brilho com nove números: é uma **passagem** que resolve cinco realces
> independentes, cada um com seus nove, e os agrupa (`0x0004C314`) em
> `HighlightsPass` por tudo menos a direção — que é por que o array externo tem
> passo `0x58` e o interno `0x60`, como o laudo do especular já tinha medido sem
> saber o que estava contando.

---

## 4. Os defaults, lidos

### 4.1. O preâmbulo (`Highlights+0x00`..`+0x118`)

`[BIN]` De `0x00062AB8`–`0x00062B34`:

| desloc. | campo | valor |
|---|---|---|
| `+0x00` | `defaultChicletLight.longitude` | **`0.0`** |
| `+0x08` | `defaultGlyphLight.longitude` | **`0.0`** |
| `+0x10` | `glyphHighlightCurvature` | **`[0.75, 0.75, 0.75, 0.75]`** |
| `+0x30` | `glyphDarklightCurvature` | **`[0.75]×4`** |
| `+0x50` | `chicletHighlightCurvature` | **`[0.75]×4`** |
| `+0x70` | `chicletDarklightCurvature` | **`[0.75]×4`** |
| `+0x90` | `glyphHighlightsUseVCM` | **`true`** |
| `+0x98` | `maxDimChicletLuminance` | **`0.2`** |
| `+0xA0` | `minBrightChicletLuminance` | **`0.99`** |
| `+0xA8` | `iconBrightnessOnlyUsesMax` | **`false`** |
| `+0xB0` | `glyphHighlightVCM` | `[0.2, 1.2, 1.25, 0.0]` + byte `1` em `+0xD0` |
| `+0xD8` | `glyphDarklightVCM` | `[−0.15, 0.7, 1.25, 0.0]` + byte `1` em `+0xF8` |
| `+0x100` | `glyphHighlightNonVCMScale` | **`1.0`** |
| `+0x108` | `glyphDarklightNonVCMScale` | **`1.0`** |
| `+0x110` | `chicletHighlightsAppearanceMode` | **`1`** |

`[OBS]` O byte `1` no fim dos dois `VCM` tem a forma de uma tag de `Optional`
(carga de quatro `Double`, tag no quinto qword), mas então a carga estaria
preenchida sob um `nil`, o que é estranho. Os dois `VCM` **não são consumidos
por esta cadeia** e ficam por ler.

### 4.2. O conjunto `glyphs*` — o que o especular de um glifo usa

`[BIN]` `0x00063AEC`–`0x00063B34` passa à fábrica `0x00064604` três
`SizeBasedValue` (ordem de memória `display, large, medium, small`):

```
A = {1.0, 1.0, 1.0, 0.3}        B = {1.0, 1.0, 0.2, 0.2}       C = {0.9, 0.9, 0.2, 0.2}
```

e `0x00064604` monta os seis membros. **Os cinco vivos:**

| | `keySharp` | `keyDiffuse` | `fillSharp` | `dark` (×2) |
|---|---|---|---|---|
| endereço | `0x64648` | `0x646FC` | `0x6478C` | `0x64888` |
| `brightness` | `1.0` | `1.0` | `1.0` | **`0.0`** |
| `opacity` | `A` | `[1,1,1,1]` | `A` | `B` |
| `outsetOpacity` | `nil` | `nil` | `nil` | **`C`** |
| `distance` | `{4, 6, 6, 6}` | `{16, 24, 24, 24}` | `{4, 6, 6, 6}` | `{4, 6, 6, 6}` |
| `minDistancePixels` | `[1]×4` | `[4]×4` | `[1]×4` | `[1]×4` |
| `inset` | `[0]×4` | `[0]×4` | `[0]×4` | `[0]×4` |
| `minInsetPixels` | `nil` | `nil` | `nil` | `nil` |
| `spread` | `[π/2]×4` | `[π/3]×4` | `[π/3]×4` | `{π/2, π/2, π, π}` |
| `bias` | `0.5` | `0.08` | `0.5` | `0.2` |
| `blendModeOverride` | `nil` | `nil` | `nil` | `nil` |

`fillDiffuse` = `nil` (`0x00064858`), `rim` = `nil` (`0x00064914`).

> **O `dark` é o único dos seis com `outsetOpacity`.** Isso fecha, do lado dos
> números, a segunda recusa que o laudo do especular mediu em `0x000494F8`: o
> `outside` só é sequer considerado onde a `outsetOpacity` existe, e ela existe
> exatamente onde o `isDarklight` da primeira recusa também deixa passar. As
> duas condições que pareciam independentes selecionam **o mesmo membro**.

### 4.3. O conjunto `chicletDefault`, de passagem

`[BIN]` `0x00062BC8`–`0x00062EDC`, para quem for atrás do chiclet:

| | `keySharp` | `keyDiffuse` | `fillSharp` | `fillDiffuse` | `dark` | `rim` |
|---|---|---|---|---|---|---|
| `brightness` | `1.1` | `1.1` | `1.1` | `1.1` | `0.0` | `1.0` |
| `opacity` | `[0.2]×4` | `[0.5]×4` | `[0.2]×4` | `[0.25]×4` | `[0.2]×4` | `[0]×4` |
| `distance` | `[10]×4` | `[40]×4` | `[10]×4` | `[40]×4` | `[10]×4` | `[10]×4` |
| `spread` | `[2π/3]×4` | `[π/2]×4` | `[2π/3]×4` | `[π/2]×4` | `[π/3]×4` | `[π]×4` |
| `bias` | `0.5` | `0.08` | `0.5` | `0.08` | `0.5` | `0.5` |

Os seis presentes (byte `+0x100` = `18` em todos), `outsetOpacity` e
`minInsetPixels` `nil` em todos, `inset` e `minDistancePixels` zerados em todos.
**O chiclet não está implementado neste commit** — o `hasSpecular` que esta
frente segue é o da camada, e ele cai no caminho do glifo.

---

## 5. A resolução, linha por linha

`[BIN]` `0x0004BD90` (1.412 bytes) transforma um `Highlight` no
`GlassHighlightSettings` de `0x60` bytes que o construtor do shader recebe:

```
k         = ctx[0x469F]                              ; a classe de tamanho, 0..3
v[k]      = SizeBasedValue.slots[3 - k]              ; 0x4BEB0-0x4BEF4
height    = max(distance[k], minDistancePixels[k] * ctx[0x46A8])     ; 0x4BF00
inset     = max(inset[k],    minInsetPixels[k]    * ctx[0x46A8])     ; 0x4BF2C
            minInsetPixels == nil alimenta -INFINITO ali             ; 0x4BF20
theta     = angleFromKey + lightLongitude                            ; 0x4BEF8
direction = (cos(phi)*sin(theta), cos(phi)*cos(theta), sin(phi))     ; 0x4C014-0x4C030
opacity   = ctx[0] * opacity[k]                                      ; 0x4C0A8
color     = (brightness, brightness, brightness, 1.0)                ; 0x4C0AC-0x4C0B0
blendMode = blendModeOverride ?? (brightness < 0.5 ? 4 : 8)          ; 0x4C0A0-0x4C0A4
```

### 5.1. A inversão de tamanho, confirmada de um segundo sítio

`[BIN]` `0x0004BEB0`–`0x0004BEF4` é uma escada de quatro vias
(`cmp #1`/`b.gt`, `cbz`, `cmp #2`/`b.eq`) que põe `k == 0` no **último** slot e
`k == 3` no primeiro:

```
k = 0 (small)   -> slots[3]        k = 2 (large)   -> slots[1]
k = 1 (medium)  -> slots[2]        k = 3 (display) -> slots[0]
```

que é o `slots[3 − sizeClass]` que a frente da sombra mediu em `0x00049FA4` e a
da translucidez no `TranslucencyEffect.strength` — aqui lido numa **terceira**
função, sobre um **terceiro** struct.

> **E aqui a armadilha FINALMENTE aparece no pixel.** Nos cinco campos de quatro
> do `Shadow` os quatro números são iguais, e a sombra registrou que o erro
> "sairia idêntico e não apareceria em teste nenhum". No `Highlights` eles
> **diferem**: `distance` vale `4` em `display` e `6` nas outras três, e
> `opacity` cai para `0.3` só em `small`. Ler a tabela ao contrário aqui troca o
> ícone de 1024 px com o de 16 px, e isso **é** visível. O teste desta frente
> checa os dois extremos por isso.

### 5.2. E os números de mescla caem no enum que este repositório já tinha

`[BIN]` `csel w8, w9, w8, mi` com `w8 = 8` e `w9 = 4` produz **4** quando
`brightness < 0.5` e **8** caso contrário. Em `Source/RenderBox/BlendMode.h`,
escrito meses antes a partir do RenderBox e não do `IconRendering`:

```
PlusDarker = 4,   PlusLighter = 8
```

**Um realce escuro subtrai e um claro soma, e ninguém teve de escolher isso.**
Duas leituras independentes, de dois binários, sobre a mesma numeração de
CoreGraphics.

### 5.3. As duas transformações de fronteira

`[BIN]` **`bias' = 1/bias − 2`** (`0x0000E9DC`–`0x0000E9EC`, como `float`,
argumento 3). `0.5` é o valor neutro: leva `bias'` a `0` e o denominador a `1`.

`[BIN]` **`spread' = cos(spread)`**, com a sentinela **`−1000.0f`** quando
`spread > π` (`0x0000EE3C`–`0x0000EE54` compara com π e faz `cset w26, gt`;
`0x0000EF50`–`0x0000EF5C` escolhe entre `−1000` e o cosseno).

> **Sem esta segunda transformação o efeito inteiro some em silêncio.** O corpo
> do shader é
> `lit = saturate((dot(direction, n) − spread') / max(1 − spread', 2^-10))`, e
> `dot` vive em `[−1, 1]`. Com um **radiano** em `spread'` — `π/2 ≈ 1.571` —
> `dot − spread'` é negativo em todo pixel e `lit` é **identicamente zero**. Com
> o **cosseno**, `π/2 → 0` é um hemisfério, `π/3 → 0.5` é um cone de 60°, e
> `π → −1000` é "sempre aceso". Um transcritor que perdesse o `cos` teria um
> especular que compila, roda, não emite aviso nenhum e não desenha nada — a
> falha que se parece exatamente com "não implementado".

`[BIN]` E mais duas, menores: `curvature` é zerada quando `inset < 0`
(`0x0000EF60`–`0x0000EF68`), e a direção chega ao shader como o `float2`
**`(x, −y)`** (`fneg s6`, `0x0000EF8C`) — é por isso que o `+y` da luz é o `−y`
da imagem, e é por isso que a chave (`angleFromKey = 0`) acende o **topo**.

---

## 6. O que foi entregue em código

- **`Source/RenderBox/GlassSpecular.h` / `.cpp`.** O laudo inteiro em cima,
  depois `HighlightSettings` (dez campos), `HighlightSlot`, os cinco
  `glyphHighlightSlots()` com os números de `§4.2`, `highlightSizeValue()` com a
  inversão, `resolveHighlight()` com o `§5`, `glassHighlightFragment()` com o
  corpo do shader de `default_mod1.ll:60-116`, e `drawSpecular()`.
- **`Source/RenderBox/IconRenderer.cpp`.** O bloco que emitia a nota agora
  desenha: o realce entra na mesma porta do campo de distância que a refração e
  a translucidez já usavam, e é composto **depois** de a sombra ser lançada
  (senão a silhueta da sombra incharia com o aro). `out.glassSpecular` conta as
  camadas que receberam pixel.
- **`Source/RenderBox/IconRenderer.h`.** O contador, com o motivo.
- **`Tests/test_glass_material.cpp`.** Um caso novo que trava os cinco erros que
  uma transcrição plausível cometeria: a tabela de tamanho lida para a frente, o
  `spread` em radianos, o `bias` cru, o conjunto expandido em um só realce, e
  `rim`/`fillDiffuse` tomados como presentes.

Suíte: **615 casos, 0 falhas** (`IC_CORPUS_DIR` posta).

### A prova em pixel

`icrender Apollo-Reborn__Apollo-Reborn__AppIcon --size 1024`, antes e depois:

```
1.048.576 pixels, 67.285 mudados (6,42 %), delta máximo de canal 255
```

Visualmente: o capacete ganha um aro claro no topo e sombreamento nas laterais,
o anel do visor deixa de ser uma faixa chapada e lê como um toro de vidro, e as
três antenas ganham os gomos que a arte desenha e a composição não mostrava.

---

## 7. O que continua `[OBS]`, nomeado onde o pixel pode ser duvidado

1. `[OBS]` **`ctx[0]`** (`0x0004C010`), o escalar que multiplica **toda**
   opacidade resolvida (`fmul d12, d13, d12`, `0x0004C0A8`). Não lido. Entra
   aqui como `1.0`. Se ele não for `1`, **todo** o brilho está errado por um
   fator comum — o que é a melhor forma de erro possível: um só número, e
   visível como "forte demais" ou "fraco demais" e não como forma errada.
2. `[OBS]` **A latitude `phi`** da luz (`ctx[0x08]`..`ctx[0x18]`, via
   `0x0004BE8C`–`0x0004BEA4`). Aqui `phi = 0`, que é o ramo que `0x0004BE74`
   toma quando `ctx[0x20] == 1` — um estado que o alvo tem, não um inventado.
   `phi ≠ 0` encolheria as componentes `x`/`y` da direção por `cos(phi)`.
3. `[OBS]` **`0x00012550`**, uma pós-passagem de ~1,5 KB sobre os ajustes já
   resolvidos, alimentada por uma cópia de `0x363` bytes do contexto
   (`0x0004C0B4`). É quase certamente o `spatialHighlighting`
   (`params+0x258`, seis campos: `alignmentRange`, `intensityPower`,
   `minIntensity`, `spreadPower`, `heightPower`, …). Não seguida.
4. `[OBS]` **`fill[+0x5B]`** e `fill[+0x61]`, os índices de aparência do §2.
   Inertes em 2.0-125 do lado do glifo, **não** do lado do chiclet.
5. `[OBS]` **A codificação do texel do SDF**, que a translucidez já carregava
   para o `.r`; aqui entram o `.g` e o `.b`, que o shader lê como `1 − 2·gb`.
   Este renderizador alimenta o gradiente do próprio campo, normalizado, e
   argumenta o sinal (`n` é a normal **para fora**, porque é a que faz a chave
   a `angleFromKey = 0` acender o topo); o que o alvo escreve naqueles dois
   canais não foi lido.
6. `[OBS]` **O chiclet.** Os números de `§4.3` estão lidos e nada os consome:
   o `hasSpecular` desta cadeia é o da camada.
7. `[OBS]` **Os dois `VCM`** do preâmbulo, e os dois portões de contexto que o
   laudo do especular já listou (`ctx+0x28` bits 5/6, `ctx+0x680`, `ctx+0x6F0`).

---

## 8. Resumo dos selos

| pergunta do pedido | resposta | selo |
|---|---|---|
| 1. os defaults, lidos | `glyphs*` (idênticos nos cinco), `§4.2`: `keySharp` `distance {4,6,6,6}` `spread π/2` `bias 0.5` `opacity {1,1,1,0.3}`; `keyDiffuse` `{16,24,24,24}` `π/3` `0.08`; `fillSharp` = `keySharp` com cone `π/3`; `dark` `brightness 0` `spread {π/2,π/2,π,π}` `bias 0.2` e a **única** `outsetOpacity` | `[BIN]` |
| — | o layout: `0x3EF1` = preâmbulo `0x118` + 10 × `0x630`; conjunto = 6 × `0x108`; ajustes = `0x101`, dez campos com deslocamento | `[BIN]` |
| — | os dez deslocamentos estão no binário como **imediatos** (`0x2008`, `0x2638`, `0x2C68`, `0x3298`, `0x38C8` em `0x627B4`; `0x118`, `0x748`, `0xD78`, `0x13A8`, `0x19D8` em `0x62588`) e o tamanho `0x629` como `mov w2` | `[BIN]` |
| 2. a regra de seleção | `fill[+0x90] == 1` e cinco ponteiros nulos → `fill[+0x5B]` escolhe `Default/Bright/Dim`; senão duas comparações de forma escolhem `Clear/Screened`. **Não é a classe de tamanho**, e os cinco conjuntos de glifo são idênticos nesta versão | `[BIN]` |
| — | a classe de tamanho indexa `slots[3 − k]`, lido de um terceiro sítio (`0x4BEB0`) — e aqui, ao contrário da sombra, **os quatro números diferem** | `[BIN]` |
| — | `spread' = cos(spread)`, sentinela `−1000` acima de π; `bias' = 1/bias − 2` | `[BIN]` |
| — | `blendMode = brightness < 0.5 ? 4 : 8`, que é `PlusDarker`/`PlusLighter` na numeração que `BlendMode.h` já tinha | `[BIN]` |
| 3. desenhou? | **sim.** 5 realces por camada com especular; `Apollo-Reborn`: 67.285 de 1.048.576 pixels (6,42 %) | `[BIN]` |
| — | falta: `ctx[0]`, `phi`, `0x12550`, o chiclet, o texel do SDF | `[OBS]` |

### Instrumentos

**Nenhum MCP de RE foi usado — nem estava disponível** (Ghidra responde,
`list_instances` vazio), pelo terceiro laudo seguido. Tudo saiu de
`scripts/macho.py` (`fn`, `dis`, `xref`), de
`References/2.0-125/out/fieldmd_iconrendering.txt`, e de leituras diretas do
*constant pool* no arquivo do slice (VA == deslocamento):
`0x984F0`, `0x98540`, `0x986E0`–`0x98720`, `0x987B0`, `0x987C0`.
