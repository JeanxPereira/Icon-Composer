# 03 — O motor de render

O `.icon` diz o que compor; nada nele diz como desenhar. O desenho é
`IconRendering.framework` sobre `RenderBox.framework`, com os shaders em dois
`default.metallib`. Este documento é o **mapa** deles — não a decodificação.

**Alvos.** `IconRendering` (2.252.160 bytes gordo, 1.121.664 na fatia arm64) e
`RenderBox` (5.639.376 / 2.772.176), ambos de `Icon Composer 2.0 (125)`, e os
`default.metallib` de cada um (94.060 e 2.004.420 bytes).

**Instrumentos.** `metallib_extract.py` e `swift_meta.py`, os dois do AquaKit.

```powershell
$AQ = "D:\CodingProjects\AquaKit\References"
& $AQ\tools\pyre\Scripts\python.exe $AQ\scripts\metallib_extract.py `
    "…\RenderBox.framework\Versions\A\Resources\default.metallib" -o out\metallib-renderbox
python $AQ\scripts\swift_meta.py out\slices\IconRendering.arm64 --fields
```

## 1. Os dois `.metallib`

`[BIN]` Os dois carregam o **mesmo** target triple: `air64_v26-apple-macosx14.0.0`.

| | módulos | vertex | fragment | visible |
|---|---|---|---|---|
| `RenderBox` | 109 | 24 | 58 | 61 |
| `IconRendering` | 14 | 0 | 0 | 14 |

`IconRendering` **não tem um único shader completo** — só funções `!air.visible`,
e os nomes mangled em volta trazem `_stitching_traits_impl`. É uma biblioteca de
peças para o **function stitching** do Metal, montada em tempo de execução. O
`RenderBox` é que tem os pipelines de verdade.

## 2. As sete peças do `IconRendering`

`[BIN]` Cada uma aparece duas vezes — a função e a sua variante `_v1`:

| função | o que o nome diz |
|---|---|
| `glassHighlight` | o brilho do vidro |
| `sdfFill` | preenchimento a partir de um campo de distância |
| `glow` | o brilho difuso |
| `shapeAwareGradientMask` | máscara de gradiente que conhece a forma |
| `simplifiedShapeAwareGradientMask` | a mesma, barata |
| `clampToEdges` | amostragem presa às bordas |
| `clampedPlusL` | `[INF]` mescla *plus-lighter* com clamp — o `plus-lighter` do doc 01 §6 |

Uma assinatura sobreviveu sem mangling perdido:
`_glassHighlight(half, half2, half, float, float, float, float, float2, half4)`.

## 3. As dezesseis unidades do `RenderBox`

`[BIN]` Os nomes dos arquivos-fonte sobrevivem nos inicializadores estáticos
(`_GLOBAL__sub_I_<arquivo>.metal`), e são o índice do motor:

```
shader_accumulator   shader_alpha_effect   shader_blend         shader_custom_effect
shader_filter_blur   shader_filter_color   shader_filter_distance
shader_mask          shader_path           shader_primitive     shader_resolve
shader_spill         shader_stroke         shader_system_fns    shader_system_glass
shader_uber
```

`[BIN]` 82 entry points de pipeline, em famílias regulares — `primitive_*`,
`accumulator_*`, `mask_*`, `path_*`, `stroke_*`, `plane_*`, `filter_*`,
`resolve_*`, `spill_*`, `uber_*` — quase todas com um par
`_fragment` / `_fragment_depth`.

`[BIN]` E **cinco** funções stitchable, que é onde mora o vidro:

```
glassBackground_v1   glassForeground_v1   displacementMap_v1
distanceGradient_v1  ovalizeGradient_v1
```

## 4. O achado: o vocabulário é o mesmo do AquaKit

`[BIN]` **Quatro dos cinco nomes acima já existem no AquaKit.** Ele transcreveu
`GlassBackground.frag`, `GlassForeground.frag`, `KeyFillHighlight.frag`,
`SdfShadow.frag` e `Field.frag` do metallib do **QuartzCore** — outro framework,
outro binário — e `glassBackground`/`glassForeground`/`displacementMap`/
`distanceGradient` aparecem em oito arquivos da árvore dele.

`[BIN]` A correspondência não para nos shaders. `IconRendering.HighlightsSet`
tem os campos **`keySharp`, `keyDiffuse`, `fillSharp`, `fillDiffuse`, `dark`,
`rim`** — que é, campo a campo, o vocabulário do `KeyFillHighlight` que o AquaKit
decodificou pelo lado do controle.

**O que isso significa, e o que não significa.** Significa que o vidro do ÍCONE e
o vidro do CONTROLE falam a mesma língua, e que a decodificação que o AquaKit já
fez do QuartzCore é candidata a valer aqui — o que **corrige por cima** o que o
spec de arquitetura afirmou em 31/08: eles não são primos distantes, são muito
mais próximos do que eu disse. Não significa que sejam o mesmo código: nome igual
não é IR igual.

**O instrumento que fecharia isso** é um diferencial IR contra IR. Ele rodou.

## 4.1. O diferencial — RODOU, 2026-09-01

`[BIN]` `RenderBox::glassBackground_v1` × `QuartzCore::glass_background_base<1,1,1>`,
ambos do build **`26A5416b`** — o mesmo macOS dos dois lados, o que faz a
comparação valer.

| | A — RenderBox | B — QuartzCore |
|---|---|---|
| função | `glassBackground_v1` | `glass_background_base<1,1,1>` (`_all_lph`) |
| onde | `RenderBox.framework/…/default.metallib`, mod98, `shader_system_glass.metal` | `QuartzCore.framework/default.metallib`, mod204 |
| forma | função *stitchable* (`!air.visible`), uniforms em `RB::Shader::Glass::BackgroundUniforms` | fragment shader, uniforms em `GlassBackgroundUniforms` |
| tamanho | 1.445 linhas de IR | 1.089 linhas de IR |

### O que foi medido, e por que essa medida decide

Uma constante escrita no fonte do shader **sobrevive a toda otimização como
literal**. Duas funções que computam a mesma coisa carregam os mesmos números — e
a parte que discrimina de verdade: carregam cada número **o mesmo número de
vezes**. Duas implementações independentes do mesmo efeito não concordam em
multiplicidade, porque ninguém arredonda igual duas vezes.

`[BIN]` **Doze constantes não-triviais em ambos os lados. Onze delas com a
contagem de ocorrências idêntica.**

| constante | A × B | |
|---|---|---|
| `-0.560547` | 3 × 3 | |
| `-0.034454` | 3 × 3 | |
| `0.002954` | 3 × 3 | |
| `0.168213` | 3 × 3 | |
| `0.212646` | 2 × 2 | luma R Rec.709 (`0.2126`) |
| `0.715332` | 3 × 3 | luma G |
| `0.072205` | 2 × 2 | luma B Rec.709 (`0.0722`) |
| `0.212524` | 1 × 1 | luma R, segundo arredondamento (`0.2125`) — **não SMPTE, ver §28.8** |
| `0.072083` | 1 × 1 | luma B, segundo arredondamento (`0.0721`) — **não SMPTE, ver §28.8** |
| `-0.75`, `0.75` | 1 × 1 | |
| `0.25` | 3 × 1 | **a única que discorda** |

Só de A: `0.001`, `0.300049`, `0.333252`. Só de B: `0.0001`.

### A impressão digital, e ela é específica demais para ser coincidência

`[BIN]` Os dois lados carregam **duas trincas de luma, não uma**, e na mesma
proporção:

```
Rec.709 a  0.2126  0.7152  0.0722  ->  half  0.212646  0.715332  0.072205
Rec.709 b  0.2125  0.7154  0.0721  ->  half  0.212524  0.715332  0.072083
                                                        ^^^^^^^^
                                        as duas colidem no MESMO half
```

`[BIN]` É por isso que R e B aparecem repartidos **2+1** enquanto G aparece **3**:
o G das duas trincas cai no mesmo valor em meia precisão. A contagem fecha
sozinha — e fecha **igual dos dois lados**.

> **Correção (§28.8).** A segunda trinca foi chamada aqui de **SMPTE**, e não é:
> a SMPTE 240M é `0.212 / 0.701 / 0.087` e cai em halfs completamente outros. As
> duas trincas são a **Rec.709 em dois arredondamentos de quatro casas** — e as
> duas somam exatamente 1, que é o que torna isso leitura e não semelhança. O
> argumento desta seção não muda de força: continuam sendo duas trincas, na mesma
> proporção, com a mesma colisão. O que muda é o nome da segunda.

`[INF]` Dois times implementando vidro independentemente não escrevem ambos as
duas trincas de luma, na mesma proporção, com a mesma colisão de arredondamento.
**É o mesmo fonte.**

### O que NÃO é igual, e isso também importa

`[BIN]` A embalagem difere, e de forma explicável:

| só no QuartzCore | leitura |
|---|---|
| `aberrate_texture` (216 linhas, função à parte) | a aberração cromática é fatorada fora; o RenderBox é achatado |
| `air.discard_fragment` | é um fragment shader; o `v1` é função que retorna |
| `air.get_width/height_texture_2d` | mede a textura; o RenderBox recebe isso em uniform |

| só no RenderBox | leitura |
|---|---|
| `air.fwidth.f16` | derivada de tela — antialiasing próprio |
| variantes de largura (`v2f32`, `v2f16`, `v4f16`) | o mesmo op em outro vetor |

`[INF]` E 7 dos "13 intrinsics só de A" na primeira passada eram
`llvm.fmuladd.*` contra `air.fma.*` — **a mesma operação sob flag de compilação
diferente**. Contar isso como divergência teria fabricado uma diferença que não
existe; o instrumento normaliza os dois, e aí ficam **28 intrinsics em comum**.

### O veredito, e o que ele custa

**Mesmo fonte, embalagem diferente.** A matemática do vidro do ícone é a
matemática do vidro do controle; o que muda é que o QuartzCore compila oito
variantes por template em tempo de compilação e o RenderBox expõe uma função
stitchable com uniforms em tempo de execução.

Para este projeto: **a transcrição que o AquaKit já fez dos coeficientes vale
aqui**, e o `IconRendering` deixa de ser RE nova para virar porte + releitura do
controle de fluxo. Era isso que o item 4 da tabela de pendências perguntava.

> **Reprodução.** `References/scripts/ir_diff.py`, na caixa do AquaKit:
> ```
> python ir_diff.py --a <renderbox_mod98.ll> --a-fn "@glassBackground_v1" \
>                   --b-fn "glass_background_baseILb1ELb1ELb1EE"
> ```
> Sem `--b` ele procura o módulo sozinho no dump do build TARGET — e **recusa**
> se mais de um definir a função. Recusou na primeira execução: quatro módulos
> definem esse template (`half`/`float` × `tex`/`sdf`), e escolher o primeiro em
> silêncio teria comparado contra uma variante que ninguém pediu.

## 5. A API do `IconRendering`

`[BIN]` 197 descritores de reflexão. Os que descrevem o desenho:

**`GlobalConfiguration`** — 13 campos: `lightIntensity`, `customLightDirection`,
`effectsAreEnabled`, `drawMitigatedVersion`, `forceEnableEnhancedGlass`,
`layerUsesCAFilterForClearMode`, `usesCAFilterForClearMode`, `allowHDR`,
`enabledRenderingSteps`, `_relativeIconInset`, `canvasSize`, `chicletDropShadow`,
`iconShape`.

**`GlassHighlightSettings`** — `opacity`, `direction`
(`ICRUnitCartesianCoordinates`), `spread`, `bias`, `height`, `inset`,
`curvature`, `color` (`RBColor`), `blendMode`.

**`FinalizedIcon.Layer`** — `material`, `blendMode`, `opacity`,
`knocksOutBorder`, `image`, `contentFrame`, `effectsFrame`, `sdf`,
`shadowImage`. É a camada do doc 01 depois de resolvida, com o SDF junto.

**`FinalizedIcon.Configuration`** — `name`, `chiclet`, `layers`, `canvasSize`,
`chicletIsVisible`, `useLegacyInsetting`, `allowCaching`, `iconBrightness`,
`style`, `_parametersOverride`, `_parameters`.

**`ExportFormat`** — `png`, `tiff`, `heic`. Três, e só três.

`[INF]` `usesCAFilterForClearMode` nomeia `CAFilter`, que é QuartzCore: o
`IconRendering` não é um motor isolado, ele chama o compositor do sistema em pelo
menos um caminho.

## 6. O que NÃO está decodado

**Tudo.** Este documento é um índice; nenhuma constante foi extraída, nenhuma
função foi lida, nenhum termo foi transcrito. Em ordem de valor:

1. **O diferencial contra o QuartzCore** (§4). É o que diz se o AquaKit já tem
   metade deste motor decodado.
2. **`glassBackground_v1` e `glassForeground_v1`** — as duas passadas do vidro.
3. **`sdfFill` e o `SDF` do `FinalizedIcon.Layer`** — como a camada vira campo de
   distância. O AquaKit tem `Field.frag` do lado do controle.
4. **`enabledRenderingSteps`**, um `OptionSet` (`rawValue: Int`): quais passos
   existem e em que ordem. É a espinha do pipeline, num inteiro.
5. **`EffectsRenderMode` do doc 01** — `designGeneration26` contra
   `designGeneration27`. Os dois metallib têm o mesmo triple `air64_v26`, então a
   geração **não** está no target; está em código, e provavelmente perto de
   `forceEnableEnhancedGlass` e `drawMitigatedVersion`.


## 7. O buffer de path — o alvo desenha na GPU, e o formato está medido

`[BIN]` `shader_path.metal` (mod58) declara:

```
path_edges_vertex(ShaderState, PathEdgesVertex, PathGlobals,
                  device const Path::CubicSegment*, ushort vid, uint iid)
```

Um **vertex shader** que lê segmentos cúbicos direto de um buffer de device.
`shader_stroke.metal` (6 módulos) faz o mesmo com `StrokeLinePoint`. **O path
nunca é achatado na CPU, em lugar nenhum.**

Isso decide a arquitetura da nossa torre: um rasterizador scanline de CPU não
seria degrau para trocar depois, seria **outro algoritmo**, com outro
antialiasing e outro pixel. É a mesma classe de erro do doc 04 §3 — ficar mais
certo que o alvo é o erro errado num projeto de reprodução.

### 7.1. Os dois layouts, do `air.struct_type_info`

`[BIN]` Offset, tamanho, tipo e nome, campo a campo:

```
RB::Shader::Path::CubicSegment          32 bytes, align 4
   0  int            count
   4  float          recip_n
   8  packed_float2  p1
  16  packed_float2  p2
  24  packed_float2  p3

RB::Shader::PathGlobals                 52 bytes, align 4
   0  packed_float2  path_matrix[3]     afim 2×3
  24  packed_float2  two_over_size
  32  packed_float2  origin
  40  float          depth
  44  float          urx
  48  float          arg
```

### 7.2. As quatro convenções, lidas do corpo da função

Nenhuma delas está no layout — todas saíram do IR do `path_edges_vertex`.

**1. A entrada 0 é um CABEÇALHO, não um segmento.** O shader lê
`segments[0].count` como o número de segmentos a percorrer, e
`segments[0].recip_n` — **carregado como `i32`, pela casa do float** — como o
total de vértices:

```llvm
%51 = getelementptr … %6, i64 0, i32 1   ; recip_n
%53 = load i32, ptr addrspace(1) %52     ; lido como INTEIRO
%54 = icmp sgt i32 %53, %16              ; comparado com o índice do vértice
```

**2. O `count` de cada segmento é uma SOMA DE PREFIXO EXCLUSIVA** — o índice do
**primeiro** vértice dele, não a contagem própria e não a soma inclusiva.

> **Correção (2026-09-01).** A primeira leitura desta seção disse só "soma de
> prefixo", e a implementação saiu **inclusiva**. Errado, e quem pegou foi a
> medição, não um teste: o IR calcula `local = vertexIndex − count` e alimenta
> `t = recip_n × local`. Com soma inclusiva o `local` fica **negativo** e todo
> `t` sai errado. A palavra que faltava valia o bug inteiro.

É o que permite a **busca binária** — o IR parte o intervalo ao meio e compara
`count` contra o índice do vértice:

```llvm
%70 = lshr i32 %69, 1                    ; metade
%72 = getelementptr … %68, i64 %71
%74 = load i32 …                         ; count do meio
%75 = icmp sgt i32 %74, %16
%77 = select i1 %75, i32 %70, i32 %76    ; desce para a metade certa
```

**3. O `p0` é o `p3` da entrada ANTERIOR.** O IR indexa `segments[i-1].p3`:

```llvm
%97 = getelementptr … %61, i64 -1, i32 4, i64 0
```

Logo o `p3` do próprio cabeçalho é onde o path começa, e o segmento 1 resolve
sem caso especial.

**4. Uma quebra de subpath é um `p1.x` NÃO-FINITO.** O shader faz bitcast de
`p1.x` para inteiro, mascara `0x7F800000` e desvia quando o expoente é todo um —
Inf ou NaN:

```llvm
%64 = bitcast float %63 to i32
%65 = and i32 %64, 2139095040            ; 0x7F800000
%66 = icmp eq i32 %65, 2139095040
```

É assim que vários subpaths moram num buffer plano só.

### 7.3. O que NÃO foi medido

**Quantos vértices um segmento recebe.** O alvo carrega o número *no buffer*,
o que significa que a regra que o escolhe mora na CPU e não estava no shader
para ler. O nosso `BuildOptions::subdivisions` é fixo e está marcado `[INF]` no
cabeçalho — produz o buffer com a forma certa, não com a subdivisão do alvo.
Pergunta aberta.

`urx` e `arg` do `PathGlobals` também não foram lidos.


## 8. O estágio de vértice, transcrito e conferido contra a GPU

### 8.1. São quatro passes de vértice — e três de fragment

> **Correção (2026-09-01).** Esta seção afirmava que o `shader_path.metal` **não
> define fragment shader nenhum**. Errado, e o erro foi de método: eu listei os
> módulos 58 a 61 e concluí sobre o arquivo inteiro. Os módulos **62, 63 e 64**
> são exatamente os fragment shaders que eu disse não existirem, e o §12 os
> transcreve. Uma amostra não é um censo.

`[BIN]` Os quatro **vertex shaders**, e os varyings de cada um dizem para que
serve:

| passe | emite além de `position` | leitura |
|---|---|---|
| `path_edges_vertex` | nada | estêncil da borda |
| `path_interior_vertex` | nada | estêncil do interior |
| `path_exterior_vertex` | `path_y` (float2), `path_slope` (float) | **antialiasing analítico**: posição relativa à aresta e inclinação dela dão a cobertura exata do pixel |
| `path_distance_vertex` | `path_p` (float2), `path_p1p2` (float2) | o campo de distância |

`[INF]` Interior e borda emitindo **só posição** é a assinatura de passes de
estêncil. Quem carrega o antialiasing é o `exterior`, com a inclinação da
aresta — e o §12 mostra o que o fragment faz com ela.

### 8.2. A indexação, e por que ela exige a busca binária

`[BIN]` `%13 = shl iid, 5` e `%16 = %13 + (vid >> 1)`:

```
vertexIndex = iid × 32 + (vid >> 1)
side        = vid & 1
```

**Trinta e dois vértices por instância**, e o par `(vid>>1, vid&1)` são as duas
pontas de uma aresta — 64 `vid` por instância. Uma instância portanto
**atravessa vários segmentos**, e é exatamente por isso que o segmento de um
vértice tem de ser *procurado* em vez de indexado.

### 8.3. O transform

`[BIN]`

```
world = m0·p.x + m1·p.y + m2                    (path_matrix, afim 2×3)
ndc.x = world.x ·  two_over_size.x − 1.0
ndc.y = world.y · −two_over_size.y + 1.0        ← Y invertido aqui
ndc.z = depth · 2^-32                            (0x3DF0000000000000)
```

`[BIN]` `origin` está no `PathGlobals` no offset 32 e **este estágio não o lê**.
`urx` e `arg` também não.

`[BIN]` E o vértice que não deve ser desenhado vai para **`(-2, -2, 0, 1)`** —
fora do volume de clip. A primitiva inteira é descartada pelo clipper, sem
`discard` e sem branch no fragment.

### 8.4. A avaliação, e a ordem importa de verdade

`[BIN]` O alvo fatora a cúbica assim:

```
B(t) = u³·p0 + (3tu)·(u·p1 + t·p2) + t³·p3        u = 1 − t
```

e **não** como `u³·p0 + 3u²t·p1 + 3ut²·p2 + t³·p3`. É a mesma curva e é outro
float.

> **A varredura provou isso, depois de me reprovar.** A mutação que troca o
> fatoramento pelo do livro **SOBREVIVEU** na primeira execução: com coordenadas
> como 0, 10, 50 e 90 as duas ordens caem no mesmo float toda vez. Medido em
> 620.000 quádruplas aleatórias nos mesmos `t`, elas divergem em **21,7%** dos
> casos — a afirmação era verdadeira e os meus paths é que eram gentis demais.
> Entrou um teste com coordenadas que divergem em `t = 0,375` e `t = 0,625`, e
> aí a mutação é pega. As coordenadas feias daquele teste são o teste.

### 8.5. O gate, sem oráculo de pixel

Não existe oráculo de pixel (spec de arquitetura, 2026-09-01), e o Vulkan de
núcleo não tem transform feedback para capturar saída de vértice. Então:

- a aritmética mora **uma vez**, em `Source/RenderBox/shaders/PathVertex.glsl`;
- um **compute shader** a executa para uma grade de `(vid, iid)` e escreve as
  posições num buffer que dá para ler de volta;
- `PathVertexOracle.cpp` faz a mesma conta na CPU, na mesma ordem, em `float`;
- o teste exige que as duas concordem **bit a bit**.

Todo resultado comparado é `precise` no GLSL — sem isso o driver pode fundir
multiplicação e soma e as duas pontas divergem no último bit por razão nenhuma,
e o gate teria de aceitar tolerância, que é como um erro pequeno se esconde.

E as mutações cortam **para os dois lados**: mutar o GLSL obriga o oráculo de
CPU a perceber; mutar o oráculo obriga a GPU. Um diferencial que só pega de um
lado é um diferencial com metade funcionando.


## 9. O interior e o exterior — onde a cobertura mora

### 9.1. Três empacotamentos, e eles não são iguais

`[BIN]` Cada passe indexa o buffer de um jeito próprio:

| passe | índice | vértices por índice | por instância |
|---|---|---|---|
| `edges` | `iid·32 + (vid>>1)` | 2 (`vid&1`) | 64 |
| `interior` | `iid·21 + (vid>>2)` | 4 (`vid&3`) | 84 |
| `exterior` | `iid·13 + (vid>>2)` | 4 (`vid&3`) | 52 |

`[OBS]` Por que 32, 21 e 13 — não medido.

### 9.2. O interior é um leque a partir do `origin`

`[BIN]` No `interior`, o canto 0 sai direto do `origin` do `PathGlobals`, **antes
de qualquer segmento ser consultado** — ele existe esteja o índice dentro do
alcance ou não. Os cantos 1, 2 e 3 dão `A`, `B`, `B`:

```
canto 0 → origin        (o ápice)
canto 1 → cúbica(t)
canto 2 → cúbica(t+1)
canto 3 → cúbica(t+1)   ← repetido: o quarto vértice é degenerado, e é ele
                          que costura um strip de triângulos no seguinte
```

`[INF]` Isso é o estêncil clássico: um triângulo `(origin, A, B)` por aresta, e o
número de voltas se acumula. É também onde o `origin` serve para alguma coisa —
o `edges` não o lê.

### 9.3. O exterior é cobertura analítica, e os cinco varyings dizem como

`[BIN]` O `PathExteriorVertex` tem cinco campos, não três:

```
position        float4
path_y          float2     as duas pontas em y, MENOR primeiro
path_slope      float      dx/dy   ← o recíproco do usual
path_intercept  float      x onde y = 0
path_value      half       +1 ou −1
```

`[BIN]` A reta é carregada como **`x = slope·y + intercept`** — invertida de
propósito, porque a integral de cobertura corre ao longo de `y`.

`[BIN]` E o quad:

```
x:  cantos 0 e 3  →  min(a.x, b.x) − 0.5
    cantos 1 e 2  →  urx + 0.5
y:  cantos 0 e 1  →  path_y.x − 0.5
    cantos 2 e 3  →  path_y.y + 0.5
```

`[INF]` Ou seja: **cada aresta cobre tudo o que está à direita dela**, até o
limite `urx`, e o fragment acumula a área com sinal. É acumulação de scanline
analítica — e é por isso que um limite direito precisa existir.

`[BIN]` `path_value = (Δy > 0) ? +1 : −1` é a direção do winding, e é o que faz
duas arestas opostas se cancelarem.

`[BIN]` Aresta quase horizontal é **descartada**, não dividida: o limiar é
`float(1e-4)`, escrito `0x3F1A36E2E0000000` no IR. Sem ele o `dx/dy` estouraria.

### 9.4. Onde a comparação bit a bit para, e por quê

Os três passes concordam entre GPU e CPU **exatamente** — menos dois campos.

`path_slope` é uma **divisão**, e `path_intercept` desce dela. O Vulkan **não
exige divisão de 32 bits corretamente arredondada**: a especificação permite
**2,5 ULP** no `OpFDiv`, e os drivers gastam esse orçamento num recíproco
aproximado. Medido aqui numa Radeon RX 6750 XT: o `slope` voltou a **1 ULP** do
da CPU, com todo o resto idêntico bit a bit.

O `precise` proíbe **contração**; ele não transforma divisão aproximada em
exata, e não existe controle de SPIR-V que o faça.

Então a comparação é exata em tudo que multiplicação e soma alcançam, e limitada
a **4 ULP** nos dois campos que descem da divisão. Isso não é tolerância
escolhida para um teste passar — é o limite documentado da plataforma, e é duas
ordens de grandeza mais apertado que qualquer mutação da varredura: um `slope`
errado erra em **por cento**, não em um bit. A mutação que afrouxa essa própria
comparação para `return true` está na varredura, e é pega.

### 9.5. Duas lições que custaram uma máquina

**Em 2026-09-01 a varredura reiniciou o PC**, e a causa era uma mutação da
própria varredura.

`[OBS]` A mutação *"o total de vértices do cabeçalho é escrito como float"* faz o
`headerVertexCount` ler os bits de `8.0f` como inteiro: **1.090.519.040**. Daí o
`invocationCount` pede 2.181.038.144 invocações × 48 bytes = **104 GB**, no host
e na GPU. A mutação *seria* pega — o teste só tentou alocar antes de reparar.

**Lição 1: nunca dimensionar alocação a partir de um número em que não se
confia.** O `headerAgreesWithSegments()` confere o total declarado contra os
segmentos que o produziram — para o último segmento desenhável, `total` tem de
ser exatamente `count + round(1/recip_n)`. Buffer que falha é **recusado**, e
nada é dimensionado a partir dele. Com a guarda, a mesma mutação é pega por 31
asserções e a memória fica plana.

**Lição 2, e é a pior: o backup do gate vivia só na memória.** Há um
`try/finally` que restaura — e ele não roda quando a máquina desliga. O
`PathBuffer.cpp` ficou **mutado no disco**, com o texto pristino perdido junto
com o processo.

O perigo real não é o arquivo torto; é o que viria depois. A execução seguinte
leria a mutação **como se fosse o código pristino**, restauraria para o defeito,
e passaria verde. Um gate que enterra o próprio defeito.

Agora o pristino vai para o disco antes de qualquer coisa, com um marcador
`SWEEP-IN-PROGRESS`. Uma execução que acha o marcador sabe que a anterior morreu
e **restaura a árvore antes de ler qualquer coisa**. É a mesma família do bug do
`LastWriteTime`: o backup existia, mas não sobrevivia ao que precisava sobreviver.

### 9.6. O pré-voo de âncoras

A proteção de âncora obsoleto já disparou **seis vezes** — sempre legítima,
sempre porque o código andou e a mutação apontava para a forma antiga. E sempre
no meio de uma varredura de vinte minutos, depois de dezenas de builds pagos.

A conferência custa milissegundos e passou para a frente de tudo: as 66 âncoras
são resolvidas **antes do primeiro build**, e o gate reporta **todas** as
obsoletas de uma vez. Um refactor que moveu três linhas vira uma rodada de
conserto, não três.

```
gate-m1: 66 mutations
anchors: 66 of 66 resolve
```

## 10. O traço — levantado, não transcrito

`[BIN]` O `shader_stroke.metal` são **seis** módulos, e é onde aparecem os
**primeiros fragment shaders** do pipeline de path: três pares vértice/fragment.

| par | vértice emite além de `position` | struct |
|---|---|---|
| `stroke_lines` | `stroke_position` f2, `stroke_radius` f2, `stroke_length` f, `stroke_alpha` h2, `stroke_caps` h2 | `StrokeLinePoint` |
| `stroke_joins` | `stroke_line0/1/2` f4, `stroke_position` f2, `stroke_alpha` h, `stroke_join` h | `StrokeLinePoint` |
| `stroke_particles` | `stroke_position` f2, `stroke_alpha` h | `StrokeParticle` |

### 10.1. Os três layouts

`[BIN]` Do `air.struct_type_info`:

```
RB::Shader::StrokeGlobals       44 bytes, align 4
   0  packed_float2  view_matrix[3]      afim 2×3
  24  packed_float2  image_size
  32  float          depth
  36  float          image_width
  40  float          recip_scale

RB::Shader::StrokeLinePoint     16 bytes, align 4
   0  packed_float2  p
   8  float          radius
  12  half           alpha
  14  short          join

RB::Shader::StrokeParticle      20 bytes, align 4
   0  packed_float2  p
   8  packed_half2   n
  12  packed_half4   param
```

`[INF]` **`radius` e `alpha` são POR PONTO.** O traço do alvo tem largura e
opacidade variáveis ao longo do caminho — é um pincel, não o `stroke-width`
uniforme do SVG. Isso é uma diferença de modelo, não de implementação, e o nosso
`CoreSVG` hoje só carrega um `stroke-width` escalar.

`[BIN]` O `stroke_lines_vertex` lê do `StrokeGlobals` apenas `view_matrix`,
`depth` e `recip_scale`. `image_size` e `image_width` não.

### 10.2. A indexação, e ela é muito mais simples que a do path

`[BIN]` Sem cabeçalho, sem soma de prefixo, sem busca binária. Uma instância por
segmento, quatro vértices, e uma **janela de quatro pontos**:

```
o segmento desenhado é   points[iid+1] → points[iid+2]
points[iid] e points[iid+3] são os VIZINHOS, para a direção das junções

vid 0 e 3 → um lado do quad      vid ≤ 1 → a ponta A
vid 1 e 2 → o outro lado         vid > 1 → a ponta B
```

`[BIN]` A meia-largura do quad:

```
halfWidth = max(radius[iid+1], radius[iid+2]) + recip_scale × 0.5
```

— o meio pixel de folga que o antialiasing precisa.

`[BIN]` A direção é `normalize(P[iid+2] − P[iid+1])`, **com fallback**: quando o
segmento tem comprimento zero, usa `P[iid+1] − P[iid]`. Um segmento degenerado
não vira NaN, herda a direção do anterior.

`[BIN]` O campo `join` carrega sentinelas: `join == -3` é testado nos dois
vizinhos, e `min(join[iid+1], join[iid+2]) < 0` **descarta** o vértice.

> **Correção (2026-09-03).** Esta linha dizia `min(join[iid], join[iid+2])`, e o
> índice estava errado por um. `[BIN]` Em `default_mod68.ll` o `air.min.s.i16`
> recebe `%42` e `%54`, e os dois `getelementptr` que os produzem indexam
> `%21 + 1` e `%21 + 2` — **os dois extremos do segmento desenhado**. O
> `join[iid]` é lido (em `%30`), mas só para o teste `== -3`.
>
> A diferença não é cosmética: com o índice errado, **o primeiro segmento de
> todo subpath seria descartado**, porque o ponto de índice `iid` é o fantasma
> que carrega o `-3`. O erro foi achado quando o layout que a CPU emite foi lido
> (§31) e não fechou com o que esta linha afirmava.

### 10.3. Onde eu parei, e por quê

**A transcrição para aqui.** Depois da direção, a geometria do quad ramifica em
bits do `RenderState` que não estão decodados:

```
and i32 %9, 448     →  bits 6–8    (o braço testado é  == 128)
and i32 %9, 1536    →  bits 9–10
```

`[OBS]` O que esses bits selecionam não foi medido. Um deles decide se as pontas
recebem o tratamento de `cap` ligado ao sentinela `-3`; o outro não foi
investigado.

Transcrever além disso seria **inventar** comportamento — o mesmo erro que este
projeto recusou nos filtros de SVG (doc 04 §3) e na regra de subdivisão (§7.3).
O passo que destrava é decodar o `RenderState` do `RB::Shader`, que também
governa o modo polilinha × cúbicas do §7 (bit 6 ali) e portanto paga duas vezes.

`[BIN]` E do lado fragment, o `stroke_particles_fragment` amostra uma textura e
usa `fwidth` — é partícula texturizada com antialiasing por derivada de tela.
Os outros dois não foram lidos.

## 11. O `RenderState` — o layout medido, a semântica não

O §10 parou porque a geometria do traço ramifica em bits do `RenderState`. Este
é o levantamento desses bits.

`[BIN]` O estado é a constante de função `RB::Shader::Constant::shader_state`.
Nada nomeia os campos dele, então o layout tem de sair das **máscaras**: máscara
de um bit é flag, máscara de bits adjacentes é enum, e os valores comparados são
os casos desse enum.

> **Correção (2026-09-01, no mesmo dia).** Esta seção dizia que o estado é *"um
> `i32`"*. **Não é: é um `uint4`.** O inicializador estático o carrega como
> `<4 x i32>` e guarda as quatro palavras em `shader_state.0` … `.3`. Só a
> palavra 0 é embrulhada numa struct — `RenderState0`, e o número no nome era a
> pista que eu não li — e as outras três são lidas direto dos globais.
>
> A tabela abaixo cobria **só a palavra 0**, e estava certa sobre ela. O que
> faltava não parecia faltar: metade não mapeada de um vetor tem exatamente a
> mesma aparência da metade mapeada.

### 11.1. O que responde às nossas duas perguntas

| máscara | bits | valores testados | quem testa |
|---|---|---|---|
| `0x40` | 6 | `0` | **`shader_path.metal`** |
| `0x1C0` | 6–8 | `0`, `128` | **`shader_stroke.metal`**, `shader_blend`, mod100 |
| `0x600` | 9–10 | `0`, `512` | **`shader_stroke.metal`**, `shader_blend`, mod100 |

`[BIN]` Então **bits 6–8 são um campo de três bits**, e o path testa só o bit
mais baixo dele enquanto o traço compara o campo inteiro contra `128` — o bit 7
sozinho. E **bits 9–10 são um campo de dois**, com o caso `512` (bit 9).

`[INF]` É o mesmo campo que o §7 encontrou: no estágio de path o bit 6 escolhe
entre ler o buffer como **polilinha já achatada** (`float2[]`) ou como
**segmentos cúbicos**. A nossa implementação só cobre o braço das cúbicas, e
agora sabe-se que o braço é um caso de um enum de três bits, não um booleano.

### 11.2. O resto do mapa

`[BIN]` Trinta e quatro máscaras distintas, cobrindo os bits 0–30:

- **flags de um bit**, todas testadas contra zero: 0, 1, 8, 10, 11, 12, 13, 14,
  15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30
- **campos de vários bits**: 6–7 (`==128`), 6–8 (`0`/`128`), 9–10 (`0`/`512`),
  9–11 (`0`/`1024`/`1536`), 10–11 (`==1024`), 16–18, 19–20 (`==524288`),
  23–25, 16–29 (`==1507328`)

### 11.2.1. As outras três palavras

`[BIN]` Com o instrumento corrigido: **42 máscaras ao todo**, e as palavras 1, 2
e 3 quase não são mascaradas *dentro* das funções — uma cada. Elas são consumidas
em outro lugar.

> **Correção (2026-09-01, mesmo dia).** A tabela abaixo já disse "palavra 0" para
> máscaras que ela **não podia atribuir**. O RenderBox embrulha as **quatro**
> palavras no **mesmo tipo** — `RenderState0` — e as passa como quatro argumentos
> separados, então o sufixo do nome do tipo não diz qual palavra um `extractvalue`
> produziu.
>
> Medido no `Shader::blend`: o seletor de modo dele sai do **segundo argumento**,
> a **palavra 1**, e o tipo continua se chamando `RenderState0`.
>
> O instrumento agora reporta essas máscaras como palavra **`?`** em vez de zero.
> Uma tabela que diz "não sei" numa coluna é melhor que uma que erra numa fração
> desconhecida das linhas.

### 11.2.2. Onde os bits ganham NOME

`[BIN]` Nos **inicializadores estáticos**. O `air.static_init` decodifica o
`uint4` em `RB::Shader::Constant::<nome>` um bit por vez, e o nome está no
próprio `store`:

| constante | palavra | bit |
|---|---|---|
| `extended_color` | 3 | 2 |
| `floating_point_color` | 3 | 3 |
| `reads_dest` | 3 | 5 |
| `reads_coverage` | 3 | 6 |

`[OBS]` Quatro das quinze constantes nomeadas resolvem para **um bit único**. As
outras — `has_coverage`, `spill_reads_color`, `spill_reads_layer`,
`has_function_table`, `filter_blur_reads_dest` e as variantes `_and_fb_read` —
são expressões compostas, e não foram decodadas.

### 11.2.3. E o que trava o traço continua sem nome — mas quatro campos não

`[OBS]` Os bits batizados por inicializador estático estão todos na **palavra 3**.

> **Atualização (2026-09-01, mesmo dia).** Quatro campos da **palavra 0** ganharam
> nome depois, por outro caminho: não por um inicializador que os batize, mas
> pelo que o `accumulator_shape` **faz** com eles. O §14 tem a leitura.
>
> | campo | significado |
> |---|---|
> | bits 6–7 | o modo da forma no resolve |
> | bits 8–9 | um sub-modo do modo 2 |
> | **bit 10** | **par-ímpar quando ligado, não-zero quando desligado** |
> | bit 11 | liga a curva quadrática do `shape` |
>
> `[OBS]` Isso é a semântica **no contexto do resolve**. O `shader_path` mascara
> os bits 6–8 e o `shader_stroke` compara 6–8 contra 128 — mesmo registrador,
> possivelmente o mesmo campo, e **isso não foi provado**. O §10 segue bloqueado
> até que seja.

### 11.3. Onde a semântica mora, e é outro instrumento

`[BIN]` O binário do host carrega `RB::RenderState` e
`RB::DisplayList::RenderState` — `RenderPass::draw`, `draw_primitives`,
`set_blend_state` recebem um `RenderState` por valor. A CPU é quem compõe o
`i32`, e é lá que os bits ganham nome.

Isso é Mach-O, não metallib: outro instrumento, e o AquaKit já tem a caixa
(`symbols.py`, `cfg.py`, `emu.py`). É o próximo passo do traço, e ele paga duas
vezes — destrava o §10 e fecha o braço que falta no §7.

### 11.4. Uma nota sobre o instrumento que produziu esta tabela

`[OBS]` A primeira versão do `render_state.py` casava nomes SSA no arquivo
inteiro. **A numeração SSA reinicia a cada função**, então ele juntou valores de
funções diferentes e reportou `state & 2048` sendo comparado contra `2`, `4`,
`5` e `9`.

Isso é **aritmeticamente impossível** — um valor mascarado só pode ser `0` ou um
subconjunto da máscara — e foi essa impossibilidade que denunciou o defeito. Sem
ela a tabela teria parecido plausível e estaria errada.

A versão que produziu o que está acima parseia função a função e **recusa**
qualquer par cujo valor não seja subconjunto da máscara, reportando-o em vez de
imprimi-lo como se fosse evidência.

## 12. A cobertura — os três fragment shaders

`[BIN]` Módulos 62, 63 e 64 do `shader_path.metal`. Quarenta e três, cento e
cinquenta e nove, e setenta e cinco linhas de IR: **277 no total**, e é a regra
de cobertura inteira.

`[BIN]` Os três escrevem no **mesmo alvo de render** — `coverage`, um `half2` na
localização 1 — e os três deixam o `.x` em zero. Os varyings chegam
`air.no_perspective` e `air.center`.

### 12.1. O interior é o número de voltas, em quatro instruções

```glsl
half2(0.0, front_facing ? +1.0 : -1.0)
```

`[INF]` É a regra de winding inteira. O leque que o passe `interior` desenha a
partir do `origin` produz triângulos cuja **orientação** carrega o sinal: de
frente soma um, de costas subtrai. Não há contagem, não há estêncil separado —
a orientação da primitiva já é o número.

### 12.2. O exterior é a área EXATA do pixel

`[BIN]` `exterior_shape`, e nada nele é amostrado ou aproximado — o trapézio que
a aresta corta do pixel é integrado em forma fechada:

```
(px, py) = trunc(position.xy)              o pixel, por truncamento para int16
y0 = max(py,     path_y.x)                 recorte inferior
y1 = min(py + 1, path_y.y)                 recorte superior
x0 = y0·slope + (intercept − px)           a reta, em coordenadas do pixel
x1 = y1·slope + (intercept − px)
se slope < 0: troca x0 ↔ x1                põe as pontas em ordem
área = saturate(y1 − y0) · (1 − saturate(x1))              o retângulo
se x1 ≠ x0:  área += (saturate(x1) − saturate(x0)) · k     o triângulo
             onde k = saturate(y1−y0)·(saturate(x0)/2 + saturate(x1)/2 − x0) / (x1−x0)
coverage = path_value × área
```

`[INF]` O `x1 ≠ x0` é a aresta **vertical**: nela o retângulo já é a resposta, e
dividir por zero não seria.

### 12.3. A distância

`[BIN]`

```
t    = saturate(dot(p, d) · scale)
v    = p − d·t                              a perpendicular ao segmento
dist = saturate(length(v))
result = half(1 − max(half(dist), 0xH1626))
```

`[BIN]` O `0xH1626` = **0,0015010833740234375**. O piso existe para que um ponto
exatamente sobre o segmento não volte como cobertura cheia.

`[BIN]` E há **duas** conversões para half: uma antes do `max` e outra no
`fsub half` do fim. O alvo estreita e permanece estreito.

### 12.4. Onde as três comparações param, e cada uma para em lugar diferente

| estágio | tolerância | por quê |
|---|---|---|
| interior | **bit a bit** | é um `select` entre duas constantes; nada pode arredondar |
| exterior | **4 ULP de float** | a correção de trapézio **divide**, e o Vulkan permite 2,5 ULP no `OpFDiv` |
| distância | **2⁻¹¹ absolutos** | ver abaixo |

> **A tolerância da distância foi errada por mim duas vezes antes de acertar, e
> as duas tentativas eram unidades erradas.**
>
> O Vulkan permite 3 ULP no `sqrt`. O estágio estreita para half, então esse
> desacordo sub-ULP em float é **quantizado** em um ULP de half do `dist`. E o
> `dist` é saturado em [0,1], cuja grade de half mais grossa é **2⁻¹¹** — logo um
> ULP de half do `dist` vale no máximo 2⁻¹¹ **absolutos**, e isso atravessa o
> `1 − dist` sem mudar de tamanho.
>
> Nem ULP de float nem ULP de half servem, e os dois foram tentados contra a
> varredura: depois que o resultado é estreitado, o **mesmo intervalo absoluto**
> lê como **um** ULP perto de 0,998 e **dezesseis** perto de 0,048, porque a
> grade de half engrossa com a magnitude. O erro é absoluto, então a medida é.

### 12.5. Duas coisas que a varredura achou nos meus TESTES

`[OBS]` **Um ponto cego sistemático.** A mutação que removia o recorte inferior
(`max(py, path_y.x)`) **sobreviveu**: em todos os casos escritos à mão *e nos mil
da varredura de arestas*, o `path_y.x` era igual ao `py` do pixel — e aí o `max`
devolve o mesmo com ou sem ele. Eu testava só a ponta de cima do recorte. O
`yLow` agora varia na varredura, e há um caso com a aresta começando **dentro**
do pixel.

`[OBS]` **E a tolerância não mordia** — `return true` no lugar dela deixava a
suíte verde, porque desligar uma verificação que hoje passa não quebra nada.
Exatamente a mesma lição do oráculo de vértice (§8.5), repetida no mesmo dia.

## 13. O primeiro pixel

Com o estágio de vértice e os três de fragment transcritos e gatados, a P5 é a
tubulação entre eles: um alvo de render `half2`, mistura **aditiva**, e a leitura
de volta.

### 13.1. O oráculo que existe, e ele não é da Apple

Este projeto não tem oráculo de pixel — está registrado no spec de arquitetura
desde 01/09. Mas **para uma forma cuja cobertura tem forma fechada, não precisa
de um**: a sobreposição de um pixel com um retângulo alinhado aos eixos é
calculável exatamente, sem referência a shader nenhum. Um render que discorda
dela está errado por mais plausível que pareça.

O gate da P5 é isso: retângulo alinhado ao grid, retângulo fracionário nos quatro
lados, retângulo mais estreito que um pixel, forma inteiramente fora da imagem, e
o buraco por winding oposto. **A geometria é o oráculo.**

`[BIN]` A mistura é **aditiva** e tem de ser: cada aresta contribui uma área com
sinal, e a soma sobre as arestas que cruzam um pixel **é** a cobertura. Arestas
opostas se cancelam porque o `path_value` delas difere de sinal.

### 13.2. A cobertura é ASSINADA, e isso não é defeito

`[BIN]` O interior de um retângulo volta **−1** ou **+1** conforme o sentido em
que ele foi percorrido. Quem transforma isso em alpha é um estágio de **resolve**
aplicando a regra de preenchimento — e esse estágio **não está decodado**.

Então a verificação compara **magnitude**, e há um teste separado provando que o
sinal existe e inverte com o sentido. Sem ele, largar o `path_value` inteiro
passaria em todas as verificações de magnitude.

### 13.3. Dois defeitos, e nenhum deles foi achado deduzindo

**`[INF]` O Y do Metal não é o Y do Vulkan.** O `rbToClip` transcreve
`y · (−two_over_size.y) + 1`, que é **correto para Metal**: o NDC dele aponta
para CIMA, então o flip converte um mundo y-para-baixo, e a origem no topo do
fragment devolve a concordância com o `path_y` que o fragment compara.

**O NDC do Vulkan já aponta para baixo.** O mesmo flip inverte. Medido: mundo
y 8..20 caiu nas linhas de tela 24..12 enquanto o fragment recortava contra
`path_y` 8..20.

A transcrição fica com a aritmética do alvo, gatada bit a bit contra o oráculo de
CPU; **a adaptação de API mora no shader de render, onde a API está.**

**`[OBS]` E metade do quad nunca era rasterizada.** Os quatro cantos do alvo estão
em ordem de **anel**, então uma lista de triângulos divide na diagonal 0–2:
`(0,1,2)` e `(0,2,3)`. Eu usei `(0,1,2)` e `(1,2,3)` — a convenção de **strip**.
As duas metades se sobrepõem e deixam a outra sem cobrir.

Achado por medição em três passos, não por dedução: dumpei os varyings (corretos),
depois a posição interpolada (correta), e a região desenhada era exatamente a
metade inferior-direita do quad.

### 13.4. Um padrão meu que a varredura achou TRÊS vezes no mesmo dia

`[OBS]` Três guardas escritas, três guardas que nada exercitava:

| guarda | como a varredura a matou |
|---|---|
| a comparação de ULP do oráculo de vértice (§8.5) | `return true`, suíte verde |
| a tolerância da cobertura (§12.5) | `return true`, suíte verde |
| a guarda do cabeçalho no `draw` (§9.5) | `if (false)`, suíte verde |

Sempre a mesma forma: **desligar uma verificação que hoje passa não quebra nada.**
Escrever a guarda e escrever o teste que prova que ela morde são dois trabalhos, e
eu fiz um só, três vezes. A varredura é o que torna essa diferença visível.

## 14. O resolve — a cobertura vira alpha, e a regra é um bit

O §13 registrou que a cobertura volta **assinada** e que o estágio que a
transforma em opacidade não estava decodado. Está.

`[BIN]` `RB::Shader::accumulator_shape(ShaderState, half, half4)`, módulo 4 do
`shader_accumulator.metal`. Aparece no `accumulator_coverage` **e** no
`accumulator_color`.

### 14.1. A leitura

```
modo = (state >> 6) & 3

modo 1:                            sobre a MAGNITUDE
    a = |cobertura|
    bit 10 ligado  → PAR-ÍMPAR:  f = fract(a);  inteiro par ? f : 1 − f
    bit 10 apagado → NÃO-ZERO:   saturate(a)

modo 2:                            sobre a cobertura COM SINAL
    piso = floor(cobertura);  f = cobertura − piso
    bit 10 ligado  → dentro = |piso| ímpar
    bit 10 apagado → dentro = piso ≠ 0
    sub = (state >> 8) & 3
      0 → dentro ? 1 − f      : 0
      1 → dentro ? 1          : f
      2 → dentro ? 1 − f·0,5  : f·0,5
      3 → dentro

modo 0 e 3: a cobertura passa intacta

depois, se bit 11 e resultado ≥ 0xH1419 e resultado ≤ shape.x:
    resultado = saturate((shape.x·r + shape.y)·r + shape.z)
```

`[BIN]` `0xH1419` = **0,0010004043579101562**.

### 14.2. A regra de preenchimento é UM BIT

`[INF]` E esse é o achado. Todo o resto das duas regras — o valor absoluto, o
`fract`, o teste de paridade — é **compartilhado**; o bit 10 só escolhe qual dos
dois finais roda.

Onde elas discordam de verdade é em **duas voltas**: o não-zero preenche `1,0` e
o par-ímpar abre buraco, `0,0`. Há um teste cujo único trabalho é essa
discordância — uma suíte que nunca alcança `|cobertura| ≥ 2` não consegue dizer
se o bit chegou a ser lido.

### 14.3. Um valor com dois trabalhos

`[BIN]` O `shape.x` é ao mesmo tempo o **limite superior** acima do qual a curva
não se aplica **e o coeficiente principal** da quadrática. Isso é do alvo, não
uma simplificação nossa, e está escrito no código para ninguém "arrumar".

### 14.4. Onde eu escorreguei

`[OBS]` A curva divergiu entre GPU e CPU por **um ULP de float** — 0,192968756
contra 0,192968741. Causa: eu não marquei aquela expressão como `precise`, então
o driver pôde fundir a multiplicação com a soma e o host não pôde.

É o mesmo cuidado aplicado em toda a torre desde o §8.5. Onde eu não apliquei,
apareceu — e apareceu como um número, não como uma opinião.

## 15. A mescla — 56 modos, e onde o seletor mora

`[BIN]` `RB::Shader::blend(ShaderState, half4 src, half4 dst)`, no
`shader_accumulator.metal` e no `shader_blend.metal`.

### 15.1. O seletor

```llvm
%7 = extractvalue RenderState0 %1, 0     ; o SEGUNDO argumento = palavra 1
%8 = lshr i32 %7, 16
%9 = and i32 %8, 16383                   ; bits 16-29
switch i32 %9, ...
```

`[BIN]` **`modo = (palavra1 >> 16) & 16383`**, e o `switch` tem **56 casos**,
numerados de 1 a 56, mais o default.

### 15.2. Isso fecha a pergunta aberta do doc 01 §10.5

O doc 01 registrava: *"dez modos de mescla contra os dezessete implementados
antes — decisão pendente, e o caminho para fechá-la é o `IconRendering` e o
`default.metallib`"*.

`[BIN]` O caminho era esse e a resposta é: o `RenderBox` implementa **56**.

> **Correção (2026-09-01, mesmo dia).** Esta seção dizia *"os dez que o `.icon`
> nomeia"*. Errado: **dez é o que o CORPUS usa**, não o que o formato tem. O
> `IconRendering.Icon.BlendMode` declara **dezoito** casos (§17.3), e o doc 01 §6
> mediu dez porque mediu 145 documentos reais, não o enum.
>
> A contagem certa tem três números, não dois:
>
> | | |
> |---|---|
> | o enum do formato | **18** |
> | o que 145 documentos usam | **10** |
> | o que o `RenderBox` implementa | **56**, incluindo Porter-Duff |
>
> E os "dezessete implementados antes" que o doc 01 §10.5 registrava não eram
> arbitrários: eram **dezessete de dezoito**.

### 15.3. Os 56 não são todos "modos de mescla"

`[BIN]` Boa parte é **composição Porter-Duff**, não mistura separável:

| caso | matemática | leitura |
|---|---|---|
| 3 | `dst.a · src` | *src in dst* |
| 4 | `(1 − dst.a) · src` | *src out dst* |
| 6 | `src·(1 − dst.a) + dst` | *dst over src* |
| 13 | `rgb = src + dst`, `a = src.a + dst.a·(1−src.a)` | **plusLighter** |
| 15 | `max(src, dst)` | **lighten** |
| 16 | `min(src, dst)` | **darken** |

`[OBS]` **Seis de 56.** Os outros cinquenta não foram decodados, e a
correspondência entre os dez nomes do formato e a numeração do `RenderBox` está
ancorada em três pontos apenas (13, 15, 16).

> **Correção (§26).** Os 56 casos foram lidos INTEIROS depois disto, e a
> aritmética destes três está certa — mas a **atribuição de nome** de 15 e 16 à
> `darken`/`lighten` do formato estava prematura. Havia duas famílias de
> min/max e só uma era conhecida quando esta tabela foi escrita. Ver §26.2.

Marcado como aberto em vez de
completado por analogia com o `CGBlendMode`, cuja numeração **não** bate com esta.

### 15.4. E o `extended_color` aparece aqui

`[BIN]` Vários casos leem `Constant::extended_color` — palavra 3, bit 2, batizado
no §11.2.2 — e tomam caminhos diferentes conforme ele. A mescla depende do espaço
de cor, e o bit que a governa já tinha nome antes de eu saber para quê.

## 16. A cor — o `composite`, e o caminho que dá para transcrever

`[BIN]` `RB::Shader::composite(ShaderState, half4 cor, half forma, float depth,
half4 dst, half2 cobertura, constant float*)`, no `shader_accumulator.metal`.

### 16.1. Dois caminhos, e só um é transcrito

`[BIN]` **Palavra 3, bit 0** escolhe:

| | |
|---|---|
| **apagado** | premultiplica a cor pela forma e escreve. É o preenchimento chapado — **transcrito e gatado** |
| **ligado** | roda a mescla, que comuta nos 56 casos do §15 |

`[OBS]` Seis dos 56 estão decodados. Transcrever o segundo caminho seria
**cinquenta stubs vestindo nome de função**, e por isso ele não está aqui.

### 16.2. Quatro bits que este trecho nomeia

`[BIN]`

```
palavra 3 bit 0    mesclar em vez de premultiplicar
palavra 3 bit 1    inverter o alpha
palavra 2 bit 19   espalhar o alpha em todos os canais
palavra 1 bit 30   usar `custom_blend`
```

`[BIN]` O último é achado estrutural: `custom_blend.MTL_VISIBLE_FN_REF` é uma
função **stitchable** — o `RenderBox` aceita mescla fornecida por quem chama,
pelo mesmo mecanismo que expõe o `glassBackground_v1` (§4.1).

### 16.3. O detalhe que uma transcrição descuidada perde

`[BIN]` A inversão alcança **só o canal de cobertura**. A cor mantém o alpha
**não** invertido: são dois números, não um.

`[BIN]` E a profundidade é empurrada um passo atrás quando o alpha **reportado**
— já invertido ou não — fica abaixo de `0xH1419`, para que um fragmento que não
pinta nada não ganhe o teste de profundidade de um que pinta. Testa o
**reportado**, não o cru, então o bit de inversão muda *quais* fragmentos são
empurrados. Há uma mutação para exatamente essa troca.

`[OBS]` Eu tinha transcrito a profundidade **antes** de ler de onde ela vinha, e
voltei para ler a cauda da função antes de escrever o teste. A ordem certa é a
inversa, e ela custou uma releitura.

## 17. `IconRendering` — a ponte entre o documento e o motor

Até aqui o repositório tem duas metades gatadas e **nada as ligando**: um modelo
de documento `.icon` de um lado, um rasterizador de outro. O `IconRendering` é a
camada que as liga, e o metadado Swift dele lê inteiro.

`[BIN]` `IconRendering.framework`, fatia **arm64** do binário universal, **197
descritores** de reflexão.

### 17.1. Os dois modelos

`[BIN]` Há um modelo **de autoria** e um modelo **assado**, e são tipos
diferentes:

```
IconRendering.Icon
    name              String?
    chicletIsVisible  Bool
    chiclet           Icon.Fill
    layers            [Icon.Layer]
    canvasSize        CGSize
    appearance        ICRAppearance

IconRendering.Icon.Layer
    elements                   [Icon.Element]
    opacity                    Double
    blendMode                  Icon.BlendMode
    material                   Icon.GlassMaterial
    performsLightingByElement  Bool
    appearance                 ICRAppearance

IconRendering.Icon.Element
    opacity, blendMode, contents, participatesInGlass,
    appearance, bounds, fill, layoutDirection
```

```
IconRendering.FinalizedIcon
    requestedSize, bakedSize, layers, config, device, retainedObjects

IconRendering.FinalizedIcon.Layer
    material, blendMode, opacity, knocksOutBorder, image,
    contentFrame, effectsFrame, sdf, shadowImage
```

`[INF]` O `Icon` é o documento em memória; o `FinalizedIcon` é o que já passou
pelo *bake* — cada camada dele carrega uma **imagem**, um **SDF** e uma **imagem
de sombra**, que é exatamente a forma que o `RenderBox` consome.

### 17.2. Um elemento tem três tipos de conteúdo

`[BIN]` `Icon.Element.ContentsStorage` é um enum de três cargas:

```
vector   CGPath
raster
svg
```

`[INF]` O nosso `CoreSVG` cobre **um** dos três. O `vector` chega como `CGPath`
já construído e o `raster` como imagem — nenhum dos dois passa por parser de SVG.

### 17.3. Os dezoito modos de mescla, em ordem

`[BIN]` `Icon.BlendMode.CodingKeys`, na ordem de declaração — que para um enum
Swift **é** a ordem dos casos:

```
 0 normal        6 screen        12 difference
 1 darken        7 colorDodge    13 exclusion
 2 multiply      8 plusLighter   14 hue
 3 colorBurn     9 overlay       15 saturation
 4 plusDarker   10 softLight     16 color
 5 lighten      11 hardLight     17 luminosity
```

`[OBS]` Essa é a numeração **do formato**. A do `RenderBox` (§15) tem 56 casos e
**não é a mesma** — os três pontos que ancorei lá (13 plusLighter, 15 lighten,
16 darken) não batem com estes índices, então a tradução entre as duas é uma
tabela que ainda não existe.

### 17.4. `Icon.Fill` responde a pergunta 4 do doc 01

`[BIN]` `Icon.Fill.Contents` é um enum de quatro cargas:

```
solid
automaticGradient
gradient
system            -> Icon.SystemFill
```

O doc 01 §10.4 perguntava *"o que `automatic-gradient` faz com uma cor só — a
regra que o deriva está no render, não no documento"*. `[BIN]` A pergunta estava
certa sobre **onde**: é um caso próprio do enum de preenchimento do
`IconRendering`, ao lado de `gradient` e distinto dele. `[OBS]` A **regra** que
deriva os stops ainda não foi lida.

### 17.5. A configuração global

`[BIN]` `GlobalConfiguration`, treze campos, e vários nomeiam decisões que o doc
03 tratava como desconhecidas:

```
lightIntensity              customLightDirection
effectsAreEnabled           drawMitigatedVersion
forceEnableEnhancedGlass    layerUsesCAFilterForClearMode
usesCAFilterForClearMode    allowHDR
enabledRenderingSteps       _relativeIconInset
canvasSize                  chicletDropShadow
iconShape
```

`[INF]` `drawMitigatedVersion` e `forceEnableEnhancedGlass` são as duas gerações
do efeito que o doc 03 §1 chamou de `EffectsRenderMode`, vistas do lado do host.
`enabledRenderingSteps` é o que liga e desliga passos do pipeline — e é o
candidato natural a alimentar o `RenderState` do §11.

### 17.6. O que isto NÃO é

`[OBS]` Isto é o **mapa dos tipos**, lido do metadado de reflexão. Nenhuma
função do `IconRendering` foi decodada: como um `Icon` vira um `FinalizedIcon`,
como o `sdf` de cada camada é gerado, e como as chamadas ao `RenderBox` são
emitidas seguem sem leitura.

## 18. `Icon` → `FinalizedIcon` — o pipeline, pelos seletores

O §17 mapeou os **tipos** e disse que nenhuma função tinha sido lida. O caminho
mais barato até as funções não foi desmontar: o `IconRendering` é híbrido
Swift/ObjC, e **seletor de Objective-C sempre sobrevive ao strip**.

`[BIN]` O binário está despido de símbolos Swift — **seis** nomes manglados no
arquivo inteiro, e só quatro nomes de arquivo-fonte. Mas os seletores estão
todos lá.

### 18.1. As duas etapas, nomeadas

`[BIN]`

```
finalizedIconWithDescriptor:error:
finalizedIconWithSize:scale:deviceClass:appearance:renderingMode:
finalizedIconWithSize:scale:deviceClass:appearance:renderingMode:la…

renderedIconWithConfiguration:
renderedFullBleedIconWithConfiguration:
renderedFullBleedIconWithConfiguration:excludeChicletSpecularHighl…
renderedLegacyCompatibleIconWithConfiguration:forDeviceClass:
renderedLegacyCompatibleIconWithConfiguration:forDeviceClass:maskT…
renderedSystemGlassCompatibleIconWithConfiguration:
```

`[INF]` São **duas** etapas com fronteira explícita, e é a mesma fronteira que os
dois modelos do §17.1 anunciam:

```
Icon  --finalizedIconWithDescriptor:error:-->  FinalizedIcon  --renderedIcon…-->  imagem
```

`[BIN]` E há **três famílias de saída**, não uma: a normal, a *full bleed* e a
*legacy compatible* — esta última com `forDeviceClass:`, o que diz que o legado
depende do dispositivo enquanto a normal não.

### 18.2. E o lado que fala com o `RenderBox`

`[BIN]`

```
drawShape:fill:alpha:blendMode:
drawLayerByReference:alpha:blendMode:flags:
drawLayerWithAlpha:blendMode:
drawLayer:inContext:   drawInContext:   drawInState:
drawDisplayList:       drawInDisplayList:   renderDisplayList:flags:
drawPlaceholder:
```

`[INF]` `drawShape:fill:alpha:blendMode:` é, campo a campo, o que o §16
transcreveu: uma forma, um preenchimento, um alpha e um modo de mescla. A
assinatura da chamada e a assinatura do `composite` são a mesma coisa vista dos
dois lados.

### 18.3. `ICRRenderingParameters` — os números do render

`[BIN]` Um agregado de sub-estruturas, cada uma um grupo de parâmetros:

| | |
|---|---|
| `SimulatedChiclet` | 8 campos de vidro: `useSystemGlass`, `dimmingStrength`, `relativeBackdropBlurRadius`, `resultBlurRadius`, `relativeRefractionStrength`, `relativeRefractionHeight`, `refractionSupersampling`, `gradientSamplesExp` |
| `Fills` | `automaticGradient`, `systemLightGradient`, `systemDarkGradient` |
| `SDFGeneration` | `clampThreshold`, `useAdvancedStacking`, `precisePixelFormatThreshold`, `maxRelativeSmoothing` |
| `SpatialHighlighting`, `TranslucencyEffect`, `ContourGradients`, `ClearMode` (15 campos) | |

`[BIN]` Um dos 35 campos do agregado chama-se `recreateRadar153477135`. É um
número de radar virado nome de campo: o parâmetro existe para reproduzir um bug
específico, e o alvo carrega o identificador dele no tipo.

### 18.4. E isto responde a pergunta 4 do doc 01

`[BIN]` `ICRRenderingParameters.Fills.AutomaticGradient`:

```
basePosition          Double
saturationBoost       Double
dimLightening         Double
midDimLightening      Double
midBrightLightening   Double
brightLightening      Double
```

`[INF]` O doc 01 §10.4 perguntava *"o que `automatic-gradient` faz com uma cor
só"*. A resposta estrutural é: **uma rampa paramétrica de quatro faixas de
brilho** — escuro, meio-escuro, meio-claro, claro — aplicada sobre a cor base,
com um impulso de saturação e uma âncora de posição. Não é regra arbitrária nem
tabela de stops: são **seis números**.

Os **valores** estão medidos no §19.

### 18.5. O que continua fechado

`[OBS]` **Nenhum corpo de função foi desmontado.** Isto é a superfície: quem
chama quem, com que argumentos, e que parâmetros existem. Como o `sdf` de cada
camada é gerado, e o que exatamente o `finalizedIconWithDescriptor:` faz entre
receber um `Icon` e devolver camadas com imagem e SDF, seguem sem leitura.

## 19. Os defaults do render, lidos do binário

O §18.4 nomeou os seis campos do `automaticGradient` e deixou os **valores** por
ler. Esta seção os lê — e o caminho até eles é o achado de método.

### 19.1. A pergunta que não tinha resposta, e a que tinha

`[BIN]` O Swift emite uma *variable initialization expression* por propriedade
com default: para um `Double` constante, a função inteira é `carrega a
constante; ret`. Varrer o binário atrás delas devolve **zero**, sobre um
`__text` decodificado a **100%**.

`[INF]` Como a cobertura é integral, isto é **ausência**, não varredura
truncada: a otimização de módulo inteiro dissolveu essas funções. É exatamente o
cenário para o qual o `litref.py` do AquaKit foi escrito, e a saída é a dele —
**inverter a pergunta**. Os números existem em algum lugar, e alguém os lê.

### 19.2. O que os leitores apontaram

`[BIN]` Um bloco de constantes em `__TEXT.__const`, a partir de `0x984c8`, com
`1.0` repetido três vezes e `0.0` várias. **Um pool deduplicado não repete
valor** — isto é um layout ordenado, não um pool.

`[BIN]` E há um leitor que o percorre com passo de **16 bytes** (cargas de `q`,
dois `double` por vez), a partir de `0x5e87c`. A entrada da função é `0x5e838`:

```
0x5e838  adrp x8, #0xcd000
0x5e83c  add  x8, x8, #0xfd0     ; x8 -> 0xcdfd0, em __DATA.__data
0x5e840  b    #0x5e844           ; cai no corpo
```

`[INF]` Um prólogo que aponta o registrador de retorno indireto para um buffer
**estático** e cai no corpo genérico é a forma de um inicializador de valor
global. O `ICRRenderingParameters` padrão é montado em `0xcdfd0`.

### 19.3. A cadeia de âncoras — dez campos antes do alvo

`[BIN]` As escritas, na ordem em que a função as faz, contra a lista de campos
que o metadado de reflexão declara (§18.3). O tipo de cada valor não vem do
código — vem da lista:

| offset | campo | valor |
|---|---|---|
| `+0x00` | `simulatedChiclet.useSystemGlass` | `false` |
| `+0x08` | `.dimmingStrength` | `0.22` |
| `+0x10` | `.relativeBackdropBlurRadius` | `0.0065` |
| `+0x18` | `.resultBlurRadius` | `2.0` |
| `+0x20` | `.relativeRefractionStrength` | `0.28` |
| `+0x28` | `.relativeRefractionHeight` | `0.11` |
| `+0x30` | `.refractionSupersampling` | `2` |
| `+0x38` | `.gradientSamplesExp` | `4` |
| `+0x40` | `darkTintDuotoneShadowBlendFactor` | `0.0` |
| `+0x48` | `darkTintHighlightsBlendWithContent` | `true` |

`[INF]` Dez campos, e o padrão de tipos casa exatamente: `Bool`, cinco
`Double`, dois `Int`, `Double`, `Bool`. É o `SimulatedChiclet` inteiro seguido
dos dois campos que o metadado declara depois dele.

### 19.4. Os seis valores

`[BIN]` `fills` começa em `+0x50`, e `Fills.automaticGradient` é o campo zero
dele:

| offset | campo | valor |
|---|---|---|
| `+0x50` | `basePosition` | `0.0` |
| `+0x58` | `saturationBoost` | `0.2` |
| `+0x60` | `dimLightening` | `0.04` |
| `+0x68` | `midDimLightening` | `0.08` |
| `+0x70` | `midBrightLightening` | `0.15` |
| `+0x78` | `brightLightening` | **`-0.05`** |

`[INF]` O clareamento **cresce** do escuro ao meio-claro — `0.04`, `0.08`,
`0.15` — e vira **negativo** na faixa mais clara. A faixa mais brilhante da cor
base não é clareada: é **escurecida**. A leitura é que a rampa faz o meio-tom
florescer e puxa o realce de volta antes que ele estoure; o que está **medido** é
o sinal, não a intenção.

### 19.5. E a prova de que é este agregado, não outro

Layout que casa não basta: uma struct com prefixo parecido produziria a mesma
tabela. O que fecha a identificação é o que vem **logo depois** do alvo.

`[BIN]` `IconColor.LinearGradient` tem **um** campo, `stops: [Stop]` — um array
Swift, **8 bytes**. E em `+0x80` e `+0x88` a função chama o **mesmo**
construtor, duas vezes, com dois `double` cada:

| destino | argumentos | em bytes sRGB |
|---|---|---|
| `+0x80` `systemLightGradient` | `1.0`, `0.9607843137254902` | **255 → 245** |
| `+0x88` `systemDarkGradient` | `0.12156862745098039`, `0.058823529411764705` | **31 → 15** |

`[INF]` Duas rampas de cinza de dois pontos: uma quase-branca, outra
quase-preta, na ordem light/dark que os nomes exigem. Os quatro números são
valores **exatos** de byte sRGB (`245/255`, `31/255`, `15/255`), o que doubles
arbitrários não são.

`[BIN]` E a conta fecha sem folga: `Fills` = 48 + 8 + 8 = **64 bytes**,
`[0x50, 0x90)`, e a escrita seguinte é em `+0x90`. Não sobra um byte para
padding nem falta um para um campo não visto.

### 19.6. O que esta leitura NÃO entrega

`[BIN]` A função faz **126 escritas** no buffer, das quais o instrumento resolve
98 e reporta **28** como `?`.

`[OBS]` Deste total, **16 estão atribuídas a campos com nome** — as das tabelas
acima. As outras **110 têm valor medido e não têm nome ganho**: atribuí-las
exigiria o layout completo do agregado, com os tamanhos dos tipos externos que o
metadado marca como `<indirect-external>`, e isso não foi medido. Os defaults do
`ClearMode`, do `SDFGeneration` e dos demais grupos estão neste mesmo corpo,
esperando essa conta — não uma nova técnica.

### 19.7. O instrumento, e por que ele existe

`scripts/thinlit.py` e `scripts/ctormap.py`.

O primeiro é o método do `litref.py` sobre um Mach-O **fino** em vez do shared
cache — o `IconRendering` não está no cache, vem solto dentro do bundle do app.
Ele carrega os dois avisos do original: cobertura sempre impressa (abaixo de
100%, "não achei" não é ausência), e retorno autenticado tratado como fronteira
de função.

`[BIN]` O **controle positivo** teve de ser outro: o pool de sombra do
`SystemBannerUI` não existe aqui. O que existe em qualquer Mach-O, e é
independente deste decodificador, é a tabela de strings — `__TEXT.__cstring` é
uma sequência de strings terminadas em `NUL` cujos **inícios vêm do arquivo**,
não da minha aritmética. Todo ponteiro que a ferramenta resolve para dentro dela
tem que cair num início. **319 resolvidos, 319 em início, cobertura 100%.**

O segundo lê o corpo do construtor. Ele existe porque a alternativa era
decodificar cadeias `movk` **à mão** — que foi o que eu fiz primeiro, e é
precisamente o risco que o `litref.py` documenta: *"um decodificador escrito às
pressas não falha, ele mente baixinho."* O que o modelo não resolve sai como
`?`, nunca como palpite, e ele imprime as **duas** leituras dos mesmos bits
(`Double` e `Int`) porque o fluxo de instruções não diz qual é o tipo do campo —
quem diz é a lista de campos, que é outra fonte.

## 20. O conversor — as peças compostas, e o alcance medido antes

Até aqui cada estágio passava um gate **isolado** e nada os ligava. Esta seção
liga, e a primeira coisa que ela fez foi medir o quanto isso vale.

### 20.1. A medição que veio antes do código

`scripts/slice-reach.py`, sobre o corpus:

```
POR CAMADA      22 de 194 desenháveis    11,3%
POR DOCUMENTO    0 de  55                 0,0%
```

`[ART]` **Zero documentos.** O enquadramento anterior — "o primeiro ícone ponta
a ponta" — estava errado, e a medição é que disse. A fatia chapada entrega a
primeira **camada**, não o primeiro ícone.

| o que bloqueia | camadas | |
|---|---|---|
| camada de vidro | 87 | 44,8% |
| raster `.png` | 54 | 27,8% |
| gradiente no SVG | 48 | 24,7% |
| traço pintado | 31 | 16,0% |
| `fill` automático | 29 | 14,9% |

`[INF]` Isso reordena a fila: o **raster** vem antes do gradiente, e não precisa
de engenharia reversa nenhuma — é um decodificador de PNG e um desenho de
imagem.

> **Três defeitos meus nesta medição, e o terceiro é o instrutivo.** A primeira
> versão contou os 90 bundles que vieram **sem arte** como bloqueados — buraco
> do corpus reportado como buraco do renderizador. A segunda leu a forma string
> de `fill` caractere a caractere e reportou **letras** como tipos de
> preenchimento. A terceira procurava a chave `image-name` e não a chave irmã
> `image-name-specializations`: num documento que especializa tudo ela via
> **asset nenhum**, não achava bloqueio, e reportava o documento como
> **totalmente desenhável**. Os dois "dentro da fatia" que ela anunciou não eram
> chapados — eram **não lidos**.
>
> O que expôs o terceiro foi um **desacordo entre duas medições minhas**: esta
> reportava zero referências penduradas onde o doc 02 §3 tinha medido duas. Um
> leitor que pula uma chave não falha; ele reporta um mundo menor e chama de
> limpo. Depois do conserto, três checagens cruzadas fecham contra medições
> independentes: **2** referências penduradas, **149** SVGs, **10** modos de
> mescla.

### 20.2. O que o conversor faz

`Source/RenderBox/SvgRenderer.cpp`. Um `SvgDocument` entra, pixels saem:

```
viewBox -> pixels     ajuste uniforme, centrado, sem flip
                      (o y do SVG e o do raster apontam ambos para baixo)
por forma:            buildPathBuffer -> CoveragePass -> readBack
                      -> accumulatorShape (regra de preenchimento)
                      -> compositeFlat (o alpha reportado)
                      -> `over`, acumulado
```

`[INF]` **A cobertura roda na GPU; a regra de preenchimento e o composite rodam
na CPU.** Não é concessão de fidelidade: os dois oráculos são gatados bit a bit
contra `PathResolve.glsl` e `PathComposite.glsl`, então CPU e GPU produzem os
mesmos números **por medição**. É escolha de escopo — transformar esses dois
shaders em passes próprios não é necessário para sair uma imagem, e o
diferencial que guardaria isso já existe.

### 20.3. O que empilhar formas NÃO é

`[BIN]` `composite` premultiplica a cor pela **cobertura**, e reporta o alpha
total (`cor.a × cobertura`) num canal separado.

`[INF]` Empilhar uma forma sobre a outra é uma operação **diferente**, e ela não
está decodada: o alvo faz isso com a mescla de framebuffer, cuja configuração
vive em bits do `RenderState` sem semântica (§11). O `over` do conversor é o
premultiplicado padrão, e ele é **escolha deste renderizador**, não
transcrição — está escrito assim no código.

> Isto foi um erro meu, e o teste pegou. Eu usei a cor de saída do `composite`
> direto no acumulador. Ela é premultiplicada pela cobertura e **não** pelo
> alpha da tinta: é o valor certo para o anexo do alvo e o errado para um
> acumulador. O alpha passava e os três canais de cor saíam deslocados.

### 20.4. A mutação que sobreviveu, e o que ela disse

A varredura reprovou a primeira execução: **102 de 103**, e a sobrevivente foi
*"o ajuste do viewBox esquece de centralizar"*. O defeito não estava no código —
estava no meu teste.

Eu escrevi três casos para o ajuste: um quadrado, um largo, e um com origem
deslocada. Nos **três**, o eixo x é o que preenche o alvo, então
`(largura − s·w) · 0,5` vale zero em todos. Um ajuste que jogasse fora a
centralização **horizontal** inteira passava a suíte limpa. A vertical estava
conferida; a horizontal nunca foi exercida.

`[INF]` É o mesmo ponto cego de §8: lá, `path_y.x` era igual ao `py` do pixel em
todo caso escrito à mão **e** na varredura de mil arestas, e o `max` que corta
por baixo nunca rodou. Casos escolhidos por variedade que, sem querer, concordam
justamente no termo sob teste. O reparo é um viewBox **alto**, onde o letterbox
é horizontal — e o RED foi verificado com a mutação aplicada à mão antes de
aceitar o teste.

### 20.5. O espaço de cor não é convertido, e isso é reportado

`[ART]` `color(display-p3 …)` aparece cerca de uma dúzia de vezes no corpus.
`[OBS]` A matriz que converte para sRGB não foi medida do alvo, e desenhar os
componentes como se fossem sRGB deslocaria todos em silêncio. A forma **é**
desenhada — descartá-la seria pior — e o índice dela sai em `unconvertedP3`.

### 20.6. O PNG, e por que ele não comprime

`Source/IconComposerFoundation/Png.cpp`. Este repositório não linka terceiros, e
uma imagem que ninguém abre não é entrega. O payload do PNG é um fluxo zlib, e
zlib permite blocos **stored** — sem compressão, com o comprimento e o
complemento dele. O arquivo é um PNG válido que qualquer visualizador abre;
apenas maior.

`[INF]` É a troca certa aqui: compressão compraria espaço em disco em troca de
um codificador de Huffman que precisaria de gate próprio, e o que este
repositório verifica é o **render**, não o codificador.

### 20.7. `icrender`, e por que é um binário separado

O `ictool` lê documentos e não precisa de nada além do sistema de arquivos:
roda numa máquina sem GPU, sem driver e sem loader Vulkan. Linkar o
renderizador nele faria todo `ictool --tree` depender de um dispositivo. `[BIN]`
O alvo traça a mesma linha — ele publica `ictool` **e** `icrtool` como dois
programas (doc 01 §11) — então isto segue a forma dele.

### 20.8. Onde os ícones do sistema estão, e não é aqui

Uma pergunta natural é usar os ícones nativos do macOS (Fotos, App Store) como
material. `[BIN]` Eles não são `.icon`:

| | |
|---|---|
| `Icon Composer.app` publica | `AppIcon.icns` e `Assets.car` — **nenhum `.icon`** |
| `IconRendering` declara | `CUILayer`, `CUILayerElement` (ambos `CUINamedLookup`), `CoreUIIconLoadingError` |

`[INF]` **`.icon` é o formato de AUTORIA.** Um app publicado compila o ícone
para dentro do `Assets.car`, e o `IconRendering` o lê de volta **via CoreUI**.
Há **duas portas de entrada** no motor, e este repositório decodificou uma.

`[OBS]` Chegar aos ícones do sistema exige extrair o sistema de arquivos do
IPSW e decodificar os registros `CUILayer` do `Assets.car` — trabalho de uma
porta que não foi aberta, não uma variação da que foi.

## 21. O raster — o que a medição pôs em primeiro lugar

O §20.1 mediu o que a fatia chapada alcança e ordenou o que falta. O primeiro da
fila não é engenharia reversa: **raster, 27,8% das camadas**, e todas elas PNG.

### 21.1. O que o corpus realmente é

`[ART]` Medido nos 60 PNGs:

| | |
|---|---|
| profundidade de bit | **8**, em todos os 60 |
| tipo de cor | **RGBA em 45, RGB em 15** — nenhuma paleta, nenhum cinza |
| filtros de linha | **os cinco ocorrem**; Paeth 7.436 vezes |
| `IDAT` | **667 chunks** para 60 arquivos — têm de ser concatenados |
| entrelaçamento Adam7 | **2 de 60** |

`[INF]` Daí o escopo: 8 bits, RGB e RGBA, todos os filtros, muitos `IDAT`, e
chunks desconhecidos (`eXIf`, `pHYs`, `sRGB`, `gAMA`, `iTXt`, `iCCP`) pulados
pelo comprimento.

### 21.2. Adam7 é uma LACUNA, não uma decisão de escopo

E a diferença importa. Os três filtros de SVG que o doc 04 §3 recusa são
recusados porque **o alvo não os lê** — implementá-los faria este renderizador
divergir do que ele reproduz. Entrelaçamento não é assim: o decodificador da
Apple lê, então esses dois arquivos são **buraco nosso**.

`[OBS]` Recusado **por nome**, com a contagem escrita — e o gate de corpus
verifica que continuam sendo **dois**. Se esse número mudar, a nota de escopo em
`Png.h` está desatualizada e o gate diz isso, em vez de deixar a documentação
virar mentira em silêncio.

### 21.3. O oráculo é o codificador de outra pessoa

Um decodificador conferido só contra o `encodePng` deste repositório provaria
que o **par concorda consigo mesmo** — que não é a mesma coisa que provar que
algum dos dois está certo.

`[ART]` Então as fixtures dos testes são fluxos produzidos pelo **zlib do
Python**, um deflate separado e muito exercitado, em três níveis de compressão —
o que entre eles alcança blocos *stored*, Huffman fixo e Huffman dinâmico. Os
pixels esperados são conhecidos porque o gerador os escolheu.

E o gate de corpus roda sobre os 60 reais, onde a conferência **não é
comparação**: é a aritmética do próprio `IHDR`. O fluxo descomprimido tem de ter
exatamente `altura × (stride + 1)` bytes, número que vem de **outro chunk** que
não os dados. Um inflate que erre por um byte falha ali.

`[ART]` **60 PNGs: 58 decodados, 2 entrelaçados, 55.273.766 pixels.**

### 21.4. As cinco que a varredura cobrou

A primeira execução com as 16 mutações do raster devolveu **114 de 119**. As
cinco sobreviventes eram guardas escritas sem o teste que as fizesse morder — a
quarta vez que esse par se separa neste repositório.

| sobrevivente | por que nada a exercia |
|---|---|
| complemento do bloco *stored* | nenhuma fixture tinha o `NLEN` quebrado |
| tabela Huffman sobre-inscrita | nenhum fluxo malformado entrava |
| empate do Paeth | só difere quando `pb == pc`, e nenhuma imagem natural chega lá |
| comprimento contra o `IHDR` | nenhum PNG com o cabeçalho mentindo |
| `IEND` ausente | **o teste existia e media outra coisa** |

`[INF]` A última é a que ensina. O teste truncava o arquivo em 40 bytes — o que
levava o `IDAT` junto, então o erro vinha do zlib vazio e **não** da guarda do
`IEND`. E como ele só checava "algum erro", passava verde provando nada. Um
teste que não nomeia o motivo não distingue a guarda que ele acha que exerce da
que de fato respondeu.

`[ART]` Duas fixtures tiveram de ser construídas de propósito. O **empate do
Paeth**: resolvendo `pb == pc` com `pa > pb` chega-se a `a=10, b=40, c=20`, onde
o correto escolhe `b` e um desempate errado escolhe `c` — vinte de diferença num
byte. E a **tabela sobre-inscrita**: quatro códigos de um bit onde só existem
dois, e o **zlib do Python também a recusa** (*"invalid code lengths set"*), o
que a torna malformada segundo um juiz independente do decodificador sob teste.

`[ART]` Com os cinco testes escritos: **119 de 119**.

### 21.5. O que isto ainda NÃO faz

`[OBS]` Decodificar não é desenhar. As 54 camadas raster continuam sem sair na
tela: falta o compositor que caminha o documento — grupos, camadas, a
especialização resolvida para um contexto, a arte de cada camada, e a posição e
opacidade dela. O `renderSvg` do §20 desenha **um** SVG; o que falta é o nível
acima dele, e é onde o raster entra.

## 23. O gradiente — o campo de 4 bits que é uma tabela de 4 × 4

`[BIN]` `shader_gradient.metal`. A superfície é maior do que o nome sugere:

```
Gradient::value(ShaderState, float2, half&, GradientGlobals::Geometry)  ponto -> t
Gradient::fold_value(ShaderState, half)                                 o spread
Gradient::color(ShaderState, half, GradientGlobals::Color, ...)         t -> cor
sample_stops_uniform / sample_stops_binary                              as paradas
Gradient::color_out(ShaderState, half4, Tables*)
```

### 23.1. Duas funções, o mesmo seletor, e um quadro que fecha

`[BIN]` `Gradient::value` e `Gradient::fold_value` comutam **ambas** em
`(palavra0 >> 19) & 15`, e agrupam os 16 casos de maneiras diferentes. Cruzando
os dois agrupamentos, o campo se decompõe:

| caso | geometria | spread |
|---|---|---|
| 0 / 1 / 2 / 7 | **linear** | pad / repeat / reflect / nenhum |
| 3 / 4 / 5 / 8 | **radial** | pad / repeat / reflect / nenhum |
| 9 / 10 / 11 / 12 | **focal** | pad / repeat / reflect / nenhum |
| 13 / 14 / 15 | **vinda do vértice** | pad / repeat / reflect |
| 6 | **cônica** | nenhum |

`[INF]` Quatro geometrias por quatro spreads, num campo de quatro bits. O que
torna isso uma medição e não uma leitura conveniente é que **as duas funções são
independentes** e os agrupamentos delas se encaixam sem sobra: `value` separa por
geometria, `fold_value` separa por spread, e cada caso cai numa célula só.

### 23.2. As geometrias, medidas

`[BIN]`

| | |
|---|---|
| **linear** | `t = p.x` — a transformação já pôs o ponto no espaço do gradiente |
| **radial** | `t = √(p·p) · a + b` |
| **cônica** | um minimax de `atan` sobre `p`, dividido por `2π` (`0x3FC45F3060` = `1/2π`) e saturado |
| **focal** | ramifica em `1 − a` e `b > 1`, e **devolve zero** quando o ponto cai fora do cone |
| **do vértice** | lê o valor de um parâmetro de saída e escreve `1.0` nele |

`[BIN]` Os spreads: `pad` é `saturate`, `repeat` é `fract`, e `reflect` é
`fract(t · ½) · 2` dobrado em 1 — a onda triangular.

### 23.3. A rampa — mais dois campos

`[BIN]` `Gradient::color` aplica o `fold_value` **primeiro**, e então comuta em
`(palavra0 >> 23) & 3`:

| valor | rampa |
|---|---|
| **0** | **duas cores**: `mix(a, b, t)` |
| 1, 2 | `sample_stops_binary` — tabela arbitrária, busca binária |
| 3 | `sample_stops_uniform` — paradas equiespaçadas |

`[BIN]` E dois bits soltos:

```
palavra0 bit 25   troca O QUE A RAMPA E: GradientCubicColor em vez de cor plana
palavra0 bit 26   gama: no caminho de duas cores, `powr(t, arg)`; no de paradas,
                  a entrada passa de 4 para 5 halves e a quinta e o expoente
```

`[BIN]` **O layout da parada** sai do `sample_stops_uniform`: um array plano de
`half4` (RGBA), com stride **4** ou **5** conforme o bit 26 — **ou de quatro
`half4` por registro**, 32 bytes, quando o bit 25 está aceso.

> **O bit 25 foi lido em 15/09/2026, e a leitura anterior estava fraca demais**
> (`Docs/Laudos/2026-09-15-gradiente-do-fundo.md` §4). "Um caminho alternativo de
> amostragem" sugere a mesma rampa amostrada de outro jeito. Não é isso: ele
> **troca o que a rampa é**. Com o bit aceso a entrada deixa de ser uma cor por
> parada e passa a ser um `GradientCubicColor` — quatro `half4`, 32 bytes por
> registro —, avaliado por Horner:
> `c(f) = c0 + f·(c1 + f·(c2 + f·c3))`. Os coeficientes saem de uma função que o
> binário **nomeia**, `smooth_color_coefficients` (`0x9DEB0`), derivando-os das
> paradas vizinhas: o resultado é um *smoothstep*, não uma reta. A leitura se
> confere pelos dois lados — o IR do shader e o caminho de CPU, onde `0x9A8D4`
> escolhe **16 halves por registro** exatamente quando o código é 4.

### 23.4. E isto NÃO corrobora a medição do formato — a simetria era falsa

`[ART]` O `linear-gradient` do documento tem **sempre exatamente duas paradas** —
48 de 48 no corpus.

> **A `[INF]` que estava escrita aqui foi REFUTADA em 15/09/2026**
> (`Docs/Laudos/2026-09-15-gradiente-do-fundo.md` §3). Ela dizia: *"que é
> precisamente a rampa tipo 0 do motor, o caminho de duas cores; as duas medições
> vêm de lados opostos e descrevem a mesma coisa"*. Sedutora, simétrica, e
> **errada**.
>
> `[BIN]` O teste de "duas cores" está em `0x9B990`–`0x9B9AC`, e ele tem quatro
> termos, não um:
>
> ```
> tipo != 4  &&  contagem == 2  &&  flags bit15 limpo  &&  (codigo - 2) & ~2 != 0
> ```
>
> O último termo pega **2 E 4**. O fundo do ícone entra com **código 4**, o teste
> **falha**, e a execução cai em `0x9B9B8`, que escreve `0x180` — **rampa tipo 3**
> (`sample_stops_uniform`) **mais o bit 25**. Duas paradas no documento não
> implicam o caminho de duas cores no motor.
>
> **Por que isto é a lição pedagógica mais útil do dia.** A `[INF]` não foi um
> chute: ela juntou duas medições verdadeiras, uma de cada lado, e a coincidência
> de forma fez o resto. O que faltava era ler o **teste**, que tem um termo a mais
> do que a hipótese previa. O "gradiente duro" que se via no render era essa
> `[INF]`, desenhada.

### 23.5. O que continua fechado

`[OBS]` `sample_stops_binary` foi localizado e **não** foi lido inteiro. E
`Gradient::color_out` (97 linhas, noutro módulo) segue sem leitura. ~~o caminho do
bit 25 no uniforme também não~~ — **FECHADO em 15/09/2026**, §23.3 acima e §41.

~~`[OBS]` A regra do `automatic-gradient` … não está aqui.~~ **VENCIDO, e há
tempo.** O §24 deste mesmo documento lê a regra inteira (`IconRendering
0x5864`). Este `[OBS]` foi respondido por outra seção do próprio documento e
nunca apagado — o que é exatamente o modo de falha que a rodada de integração de
16/09/2026 foi feita para corrigir: um `[OBS]` que sobrevive à própria resposta
custa uma frente inteira a quem lê de cima para baixo.

## 24. `automatic-gradient` — a regra, lida

O §19.4 leu os **seis parâmetros** e deixou a REGRA por ler, e o spec do
gradiente registrou esse passo como um que **podia não fechar**. Fechou.

`[BIN]` A derivação é uma função só, em `IconRendering` (`0x5864`), com dois
chamadores e nenhum outro. Ela recebe os seis parâmetros e uma cor, e devolve
**duas paradas**.

### 24.1. A regra

`[BIN]`

```
L  = 0.2126·r + 0.7152·g + 0.0722·b            Rec.709, soma exatamente 1
Λ  = dim        se L ≤ 0.25
     midDim     se L ≤ 0.50
     midBright  se L ≤ 0.75
     bright     caso contrário
c' = c − sb·(L − c)                            por canal
c" = clamp01( c' + Λ·(Λ < 0 ? c' : 1 − c') )
```

E as duas paradas:

```
A = (c", alpha)  em  Λ > 0 ? 0 : 1
B = (c,  alpha)  em  Λ > 0 ? 1 − basePosition : basePosition
```

`[BIN]` ordenadas **crescentes por posição** — o alvo chama um sort com
comparador que compara o campo de posição.

### 24.2. O que uma invenção plausível teria errado

Este é o ponto da seção. Seis parâmetros com nome de faixa de brilho convidam a
uma regra em que **os parâmetros descrevem as faixas**. Não descrevem:

`[BIN]` **As fronteiras estão fixas no código** — `fmov d2, #0.25`,
`fmov d5, #0.50`, `fmov d6, #0.75`, imediatos, comparados com `ls` (≤). Nenhuma
delas vem do bloco de parâmetros.

`[INF]` Uma rampa que derivasse as fronteiras do `basePosition` — o único
parâmetro com cara de posição — teria produzido pixel diferente com aparência de
acerto. E `basePosition` no default `0.0` **não faz nada**, então ler os
defaults sem a regra não poderia ter dito o que ele é.

Três outros detalhes que a leitura entrega e a dedução não:

| | |
|---|---|
| o boost de saturação | empurra o canal **para longe da luminância**, então um cinza não se move |
| o sinal de Λ | troca **em que ponta** a cor derivada fica, e é por isso que o alvo ordena |
| o alpha | atravessa **intocado** para as duas paradas — não é clareado nem clampeado |

`[OBS]` A função não faz **nenhuma** conversão de espaço de cor: ela aplica a
luminância aos componentes como estão. Se eles são linear-light ou codificados
em sRGB é decidido antes, e isso não foi medido — o gradiente herda a pergunta
de espaço do §20.4.

### 24.3. E o `automatic` é outra coisa

`[BIN]` `Icon.Fill.Contents.system` (55 fills de camada + 28 de fundo) **não é
derivado da cor**. Ele lê um byte de payload, compara com 1, e escolhe entre
`ICRRenderingParameters.Fills.systemLightGradient` e `systemDarkGradient` — as
duas listas de paradas enlatadas que o §19.5 mediu como rampas de cinza
`255→245` e `31→15`.

`[INF]` Sem luminância, sem boost, sem clareamento. Os seis parâmetros do
`automaticGradient` não participam.

### 24.4. O que segue fechado

`[OBS]` A **geometria** do `automatic-gradient` não sai desta função: ela devolve
paradas, não posições de eixo. De onde vem o `start`/`end` desse gradiente não
foi rastreado.

## 25. O gradiente ligado, e o que a arte de uma camada realmente dá

### 25.1. O alcance, de novo

`[ART]` Depois de o `url(#id)` do SVG passar a ser pintado:

| | antes do raster | depois do raster | depois do gradiente |
|---|---|---|---|
| camadas | 22 | 62 | **79 de 194 — 40,7%** |
| documentos inteiros | 0 | 9 | **12 de 55** |

`[INF]` Os três números **não são comparáveis entre si** como medida do corpus: o
que mudou foi a régua, não os documentos. Está escrito no cabeçalho do
`slice-reach.py`, porque um número subindo sem essa nota parece descoberta.

### 25.2. A geometria, em termos do alvo

`[BIN]` Para a geometria linear o `Gradient::value` do alvo é literalmente
`p.x` — o que significa que **toda** a informação do eixo tem de estar na
transformação que leva o ponto ao espaço do gradiente. É isso que o renderizador
monta:

```
usuário --(gradientUnits, gradientTransform)--> espaço declarado --> parâmetro
```

`[ART]` `userSpaceOnUse` em **159** dos 161 gradientes do corpus, e o
`objectBoundingBox` — o *default* do SVG — nos outros 2. Os dois entram, porque
2 não é zero. `gradientTransform` em 13, componível.

`[ART]` **`spreadMethod` não entra, e o número é zero**: nenhuma ocorrência em
161 gradientes. O comportamento é sempre `pad`.

### 25.3. A arte dá a forma; o `fill` da camada dá a cor

Esta é a semântica que faltava para o compositor, e ela foi **medida**, não
suposta.

`[ART]` Das camadas do corpus que carregam ao mesmo tempo um `fill` sólido e
arte SVG: **17** nomeiam arte monocromática e **7** nomeiam arte policrômica. E
os dois exemplos que decidem:

| bundle | arte | `fill` da camada |
|---|---|---|
| `Apollo-Reborn/AppIcon` — `Eyes 3.svg` | `#000000` | `display-p3:0.695, 0.153, 0.477` — rosa |
| `Aeastr/GlowGetter` — `LeftHalf.svg` | `#D9D9D9` | branco |

`[INF]` Ninguém desenha um olho rosa como arte preta a menos que o `fill`
retinja. E `#D9D9D9` é o cinza de espaço reservado que uma ferramenta de desenho
deixa para trás. A regra é: **a arte é a forma, o `fill` é a cor.**

O render confirma: com a regra aplicada, o Apollo deixa de ser uma silhueta preta
e vira um capacete branco com anel violeta e o rosa dos olhos aparecendo.

`[OBS]` As **7 camadas de arte policrômica** achatam sob esta regra. Se o alvo
também as achata não foi medido.

### 25.4. O `automatic-gradient` NÃO é desenhado, e o motivo é preciso

As paradas são deriváveis — o §24 leu a regra. **O eixo não.** A função devolve
paradas, não colocação (§24.4).

`[INF]` Derivar as cores certas e inventar onde elas vão põe **cor certa em
lugar errado** — pior que não desenhar, porque parece pronto. A camada é
**nomeada**, com essa frase, e o `icrender` a imprime.

O mesmo vale para `automatic`/`system`: ele escolhe entre duas rampas enlatadas
dos parâmetros de render (§24.3), e os **valores** dessas rampas não foram lidos
além dos dois pares de cinza do §19.5.

## 26. Os 56 modos de mescla, lidos inteiros

`[BIN]` `RB::Shader::(anônimo)::blend(ShaderState, half4, half4)`, em
`shader_blend.metal`. **56 casos, valores 1 a 56**, mais o default. A tabela
`caso → destino` é **idêntica em cinco módulos diferentes**, então não é
específica de módulo.

`[BIN]` E o primeiro argumento é a **origem**: o caso 13 calcula
`a = src.a + dst.a·(1−src.a)`, que só fecha com essa atribuição.

### 26.1. As bandas

`[BIN]` A numeração é **em faixas**, e elas são limpas:

| faixa | o que é |
|---|---|
| **default** | `src` — cópia |
| **1–10** | **Porter-Duff**, na ordem clear, over, in, out, atop, dst-over, dst-in, dst-out, dst-atop, xor |
| **11–18** | aritmética barata que dispensa a cauda de composição: plus, screen, plusLighter, exclusion, max, min, `d−s`, `s−d` |
| **19–23** | **máscara/cobertura** — o `composite` intercepta 19–22 **antes** de chamar o `blend`, escreve cor zero e a cobertura no anexo de duas lanes |
| **24** | uma segunda entrada de source-over, dividindo o bloco do caso 2 |
| **25–42** | a família completa separável e não-separável **com** a cauda de composição |
| **43–53** | operações internas do motor |
| **54–56** | sentinela, não implementados neste shader |

`[INF]` Screen (12) e exclusion (14) caem na faixa barata **legitimamente**: as
formas compostas premultiplicadas delas colapsam numa expressão só.

### 26.2. A correção — havia DUAS famílias de min/max

Esta é a parte que corrige o §15.3.

`[BIN]`

| caso | aritmética |
|---|---|
| **15 / 16** | `fmax` / `fmin` sobre o vetor de **quatro lanes, alpha incluído** |
| **27 / 28** | `min(as·d, s·ab)` / `max(as·d, s·ab)` **em três lanes**, mais a cauda de composição e `a = as+ab−as·ab` |

`[INF]` 15/16 são a operação de **função fixa da GPU** (`MTLBlendOperationMin`/
`Max`): sobre o alpha dão `min(as,ab)`, que não é saída Porter-Duff válida e não
reduz a source-over quando o outro operando está vazio. 27/28 são
**literalmente** a `darken`/`lighten` separável do W3C.

`[OBS]` Qual das duas o formato chama de `darken`/`lighten` **o shader não
decide**. Quem decide é o código do `IconRendering` que empacota o
`palavra1 >> 16`, e ele não foi lido. Estão registradas as duas candidaturas.

> **O que eu errei, e como.** Quando só 13, 15 e 16 estavam decodados, casar
> `min`/`max` com `darken`/`lighten` era o único casamento disponível — e eu o
> registrei como se fosse leitura. Não era: era o **único candidato de um
> conjunto de um**. Com 27/28 na mesa o casamento deixa de selecionar 15/16, e
> a lição é que "o único que serve" não é a mesma afirmação que "o que é",
> mesmo quando as duas coincidem.

E o mesmo cuidado vale para o `plusLighter`: **quatro** casos são aditivos — 11
(`s+d` incluindo alpha, sem clamp), 13 (`s+d`, alpha de source-over), 43 (`s+d`,
`a = saturate(as+ab)`) e 44. A aritmética sozinha não escolhe um.

### 26.3. A tradução, 16 de 18 com candidato único

`[INF]` As correspondências, com o que ficou em aberto marcado:

| formato | nome | caso do `RenderBox` |
|---|---|---|
| 0 | normal | **2** (também em 24) |
| 1 | darken | **27** ou 16 — *contestado, §26.2* |
| 2 | multiply | **25** |
| 3 | colorBurn | **30** |
| 4 | plusDarker | 44 ou 40 — **dois candidatos** |
| 5 | lighten | **28** ou 15 — *contestado* |
| 6 | screen | **12** |
| 7 | colorDodge | **29** |
| 8 | plusLighter | 13, 43 ou 11 — **três candidatos** |
| 9 | overlay | **26** |
| 10 | softLight | **31** |
| 11 | hardLight | **32** |
| 12 | difference | **33** |
| 13 | exclusion | **14** |
| 14 | hue | **36** |
| 15 | saturation | **37** |
| 16 | color | **38** |
| 17 | luminosity | **39** |

### 26.4. Dois achados que a leitura completa entrega

`[BIN]` **O `softLight` do alvo NÃO é a fórmula do W3C.** O caso 31 calcula
`d + (2s − 1)·d·(1 − d)` — o ramo baixo do W3C aplicado incondicionalmente, uma
aproximação quadrática. **Não há `sqrt` nem cúbica em lugar nenhum do módulo**,
que é o que a fórmula completa exigiria.

`[BIN]` **As constantes de luminância se dividem por família.** Os casos 36–39
(hue, saturation, color, luminosity) usam `0,300 / 0,590 / 0,110` — o `Lum()` do
PDF. Os casos 45 e 46 usam **Rec.709** `0,2126 / 0,7152 / 0,0722`.

`[INF]` Constantes diferentes querem dizer subsistemas diferentes: 45 e 46 não
são modos de mescla de cor. Eles são um source-over **chaveado pela luminância
do fundo**, com o peso elevado à quarta potência.

`[BIN]` E o `extended_color` controla **duas** coisas distintas: na faixa
Porter-Duff decide se `1−a` passa por `saturate`; no `pdf_mode` decide se o rgb
mesclado é limitado a `[0, alpha]` — e o sentido é o **inverso** do intuitivo,
porque o limite roda quando o bit está **ligado**.

### 26.5. A contagem

`[ART]` **56 de 56 blocos lidos.** **39 casados** com fórmula nomeada, **17
reportados sem casamento** — 19 a 23, 45 a 53 e 54 a 56. Dezesseis dos 18 nomes
do formato têm candidato único; dois não; e dois dos dezesseis contradizem o que
esta documentação afirmava antes.

## 27. A ponte dos uniforms do vidro — quem escreve qual byte

`[BIN]` O `RB::Shader::Glass::BackgroundUniforms` tem **256 bytes e 75 campos**, e
o IR dá o offset, a largura e a aritmética de cada um — e **nenhum nome**. Estas
são funções `!air.visible`, não *entry points*: não há nó `!air.buffer`, e os
únicos nomes que o AIR guarda são os sete argumentos da função.

`[BIN]` E `RenderBox.arm64` carrega **83 chaves no estilo `CIFilter`** numa corrida
contígua em `0x16E7D8`–`0x16EF66`, embrulhadas em 83 CFStrings de índices
**contíguos e na mesma ordem**.

Nome parecido não prova slot igual — é a armadilha do §15.3, onde "o único
candidato de um conjunto de um" foi registrado como leitura. **Quem decide é o
código que preenche a struct.**

### 27.1. Os dois empacotadores, achados por contagem

`[BIN]` Varrendo as 345.723 instruções do `__text` e resolvendo todo par
`adrp`+`add` que cai no bloco de CFStrings: **83 de 83 chaves são referenciadas**,
e elas se concentram em duas funções.

| função | chaves | struct |
|---|---|---|
| `0x0E6D40` | **75** | `BackgroundUniforms` (256 B) |
| `0x0E7D48` | 12 | `ForegroundUniforms` (72 B) |
| `0x0E5E28` | 1 | — |

`[INF]` 75 chaves numa função e 75 campos na struct não é coincidência: é o
empacotador.

### 27.2. A forma, e ela é a mesma em todas as 75

`[BIN]` Cada campo é três instruções e uma chamada:

```
adrp/add x1, <a CFString da chave>
fmov     d0, <o DEFAULT, imediato ou do pool>
mov      x0, x19                       ; o objeto
bl       0xE80AC                       ; (objeto, chave, default) -> double
fcvt     s0, d0                        ; para float,  ou fcvt h0 para half
str      s0, [sp, #<offset>]           ; ESTE store É a ligação
```

`[BIN]` E entre o `fcvt` e o `str` mora a **transformação**, que é informação
tanto quanto o offset. O caso mais frequente é o recíproco guardado:

```
fdiv  s1, 1.0, s0
fcmp  s0, #0.0
fcsel s0, s1, s11, gt        ; s11 = 0 --> altura zero vira ZERO, não infinito
```

`[INF]` Toda chave `…Height` é gravada como **1/altura**, com guarda em zero. O
shader não divide: ele já recebe o inverso. Transcrever a divisão no shader daria
o mesmo pixel na maioria dos casos e o pixel errado exatamente em altura zero.

### 27.3. O controle positivo — dois artefatos independentes concordando

O instrumento é `scripts/glassbridge.py`, e o que o torna leitura e não palpite é
o controle:

> Todo store que a ferramenta resolve tem que cair **dentro de um único campo que
> o IR declara**. Um store de 4 bytes começando no offset de um `half`, ou um de 8
> bytes atravessando a borda de um `half4`, denuncia aritmética de endereço errada.

`[ART]` **54 de 54 stores dentro da struct caem dentro de um campo declarado.**
E os dois lados foram produzidos por gente diferente para fins diferentes: o
empacotador ARM64 **escreve**, o `default_mod98.ll` **lê**. Eles concordam sobre o
layout.

`[ART]` O `ForegroundUniforms` passa o mesmo controle, 5 de 5.

> **O que a primeira versão errou.** Ela emparelhava a chave com o próximo store
> numa pilha, e o compilador intercala: **oito offsets em colisão e seis chaves
> repetidas**. Colisão é assinatura de emparelhamento errado, não de um binário
> que sobrescreve o próprio campo. A correção foi seguir o **valor** — do `d0` da
> chamada, pelos `fcvt` e pela aritmética, até o `str` — e **matar `v0`–`v7` em
> toda chamada**, porque são caller-saved. Sem essa morte um valor sobrevive a uma
> chamada dentro de um registrador que o callee possui e reaparece ligado a um
> slot que nunca tocou. As colisões caíram de oito para zero.

### 27.4. A tabela

`[BIN]` **54 chaves ligadas, 138 dos 256 bytes.** O `default` é o valor que o
alvo usa quando o documento não diz nada; *runtime* quer dizer que o default vem
de um registrador que este instrumento não constantifica.

| byte | B | chave | default | transformação |
|---|---|---|---|---|
| 16 | 4 | `inputInnerRefractionAmount` | -150 |  |
| 20 | 4 | `inputInnerRefractionHeight` | 60 | `fdiv fcsel` |
| 24 | 4 | `inputOuterRefractionAmount` | 100 |  |
| 28 | 4 | `inputOuterRefractionHeight` | 50 | `fdiv fcsel` |
| 32 | 4 | `inputRefractionDistance0` | -11 |  |
| 36 | 4 | `inputRefractionDistance1` | -3 |  |
| 40 | 4 | `inputBlurRadius` | 30 | `fmul` |
| 44 | 4 | `inputBleedBlurRadius` | 100 | `fadd` |
| 48 | 4 | `inputBleedAmount` | 400 |  |
| 52 | 4 | `inputBleedHeight` | 500 | `fdiv fcsel` |
| 56 | 4 | `inputShadowAmount` | 200 |  |
| 60 | 4 | `inputShadowHeight` | 250 | `fdiv fcsel` |
| 72 | 4 | `inputShadowBlurRadius` | 25 | `fadd` |
| 76 | 4 | `inputShadowRadius` | 25 | `fdiv fcsel` |
| 152 | 4 | `inputShadowVibrancyContribution` | 1 |  |
| 160 | 2 | `inputBlurOpacity0` | 1 |  |
| 162 | 2 | `inputBlurOpacity1` | 0.1 |  |
| 164 | 2 | `inputBlurOpacity2` | *runtime* |  |
| 166 | 2 | `inputBlurOpacity3` | 0.4 |  |
| 168 | 2 | `inputBlurDistance0` | -450 |  |
| 170 | 2 | `inputBlurDistance1` | -3 |  |
| 172 | 2 | `inputBlurDistance2` | *runtime* |  |
| 174 | 2 | `inputBlurDistance3` | *runtime* |  |
| 176 | 2 | `inputBleedDistance0` | -400 |  |
| 178 | 2 | `inputBleedDistance1` | -42 |  |
| 180 | 2 | `inputBleedOpacity` | 0.2 |  |
| 182 | 2 | `inputFaceOpacity` | 1 |  |
| 184 | 2 | `inputBleedDarkenBlend` | *runtime* | `fcsel` |
| 188 | 2 | `inputShadowDistanceOffset` | -50 |  |
| 190 | 2 | `inputShadowOpacity` | 1 |  |
| 192 | 2 | `inputRefractionOpacity` | 0.75 |  |
| 194 | 2 | `inputMaxHeadroom` | 1.2 | `fadd fdiv fsub fcsel` |
| 196 | 2 | `inputSDRGradientDistance0` | -2.5 |  |
| 198 | 2 | `inputSDRGradientDistance1` | -1.5 | `fsub fdiv` |
| 204 | 2 | `inputFaceColorMatrixMaxLuma` | 1 | `fsub fmadd fcsel fcsel fsub` |
| 206 | 2 | `inputSDRHoldingToneWhite` | 0.97 |  |
| 208 | 2 | `inputAberrationAmount` | 1 |  |
| 210 | 2 | `inputAberrationHeight` | 20 | `fdiv` |
| 212 | 2 | `inputAberrationOffset` | 1 |  |
| 218 | 2 | `inputRingShadowOffset` | *runtime* |  |
| 220 | 2 | `inputRingShadowStrokeWidth` | *runtime* |  |
| 222 | 2 | `inputRingShadowBlurRadius` | *runtime* |  |
| 224 | 2 | `inputRingShadowOpacity` | *runtime* |  |
| 226 | 2 | `inputRingShadowMask` | 1 |  |
| 228 | 2 | `inputKeyFillHighlightHeight` | *runtime* |  |
| 236 | 2 | `inputKeyFillHighlightSpread` | *runtime* | `fdiv fadd` |
| 238 | 2 | `inputKeyFillHighlightEffectOffset` | -2 |  |
| 240 | 2 | `inputKeyFillHighlightColorBias` | *runtime* |  |
| 242 | 2 | `inputBlurFillBlurRadius` | 1 | `fmul` |
| 244 | 2 | `inputBlurFillLightenOpacity` | 4 |  |
| 246 | 2 | `inputBlurFillDarkenOpacity` | *runtime* |  |
| 248 | 2 | `inputBlurFillNormalOpacity` | *runtime* |  |
| 252 | 2 | `inputBleedColorMatrixBlack` | *runtime* | `fcsel` |
| 254 | 2 | `inputBleedColorMatrixBlack` | *runtime* | `fcsel` |

### 27.5. A base estava errada em 16 bytes, e o controle passou assim mesmo

`[BIN]` A primeira publicação desta tabela usava base `0xC0`, porque é ali que a
primeira chave grava. **Tomar isso como o byte 0 pressupõe que a primeira chave é
o primeiro campo**, e nos dois empacotadores isso é falso.

`[BIN]` A base verdadeira é `0xB0`, e ela é **nomeada por instruções**: os oito
`stp q0, q0, [sp, #n]` que zeram a struct, com `n` = `0xB0, 0xD0 … 0x190`,
cobrindo `0xB0..0x1AF` — **exatamente 256 bytes, exatamente o `sizeof` da
struct**. Toda a coluna de bytes acima subiu 16.

`[BIN]` E a confirmação de que agora está certo, e não só consistente: o buraco
das matrizes cai em **`+80..+151`, exatamente o bloco dos nove `half4`** que o IR
declara. Com a base errada ele caía em `+64..+135`, atravessando fronteira de
campo sem significar nada. Além disso `inputShadowOffset`, que é um `float2`,
pousa em `+64` — o único `[2 x float]` no meio da struct.

`[ART]` Os 118 bytes não ligados, nomeados em vez de preenchidos:

| faixa | bytes | leitura |
|---|---|---|
| `+0`…`+15` | 16 | **os dois pares de decodificação** (distância e gradiente). Nenhuma chave os escreve, nos DOIS empacotadores — é por isso que os dois erros de base foram de exatamente 16 |
| `+80`…`+151`, `+156`…`+159` | 76 | **as três matrizes de cor.** `White`, `Black`, `Saturation` e `FillColor` não são gravadas chave a chave: passam pela montagem composta YCC, uma chave alimentando várias entradas. O AquaKit transcreveu essa rotina do lado do QuartzCore (`MakeYCCCompositeMatrix`) |
| `+64`…`+71` | 8 | `inputShadowOffset`, um `float2` — a ferramenta o viu numa passada anterior e o perdeu ao endurecer a morte de registradores. `[OBS]` |
| `+186`, `+200`…`+203`, `+214`…`+217`, `+230`…`+235`, `+250` | 18 | `[OBS]` não resolvidos. E um deles tem explicação: `[BIN]` resolvendo todo `getelementptr` do `mod98`, **74 dos 75 campos são lidos — o byte 202 nunca é** |

> **O erro, e por que ele passou duas vezes.** A mesma escolha errada foi feita
> nos dois empacotadores: deslocar a base até a primeira chave cair no byte 0. O
> controle positivo do §27.3 disse `PASS` nas duas vezes, porque ele detecta store
> que **atravessa** fronteira e não tabela inteira **transladada** por múltiplo da
> granularidade.
>
> A lição não é sobre aritmética, é sobre origem: a base agora sai da instrução
> que **zera a struct**, que a nomeia diretamente. Um número que encaixa não é o
> mesmo que um número que foi lido — e quando o encaixe é a única evidência,
> qualquer deslocamento que preserve o alinhamento encaixa igual.

### 27.6. O empacotador do foreground, corrigido — e a base era um palpite

`[BIN]` A tabela do `ForegroundUniforms` publicada acima saiu com a base errada, e
o erro é instrutivo. A base foi escolhida deslocando até a primeira chave cair no
byte 0 — o que **pressupõe que a primeira chave é o primeiro campo**. O `mod99`
diz que não é: os bytes 0..15 guardam a decodificação do campo de distância, e
**nenhuma chave os escreve**.

`[BIN]` A base verdadeira é `0x58`, e ela é **nomeada por uma instrução**:
`stp xzr, xzr, [sp, #0x58]` em `0x0E7D84`, o store que zera a struct. Dezesseis
bytes de diferença.

`[BIN]` Os 18 floats, com a leitura do `mod99` junto:

| byte | conteúdo |
|---|---|
| 0, 4 | escala e viés da distância — **não ligados a chave**, vêm de uma chamada em `0x0E8CE4` |
| 8, 12 | escala e viés do gradiente — mesma origem |
| 16, 20, 24 | `inputRefractionAmount` · `1/inputRefractionHeight` · `inputRefractionOffset` |
| 28, 32, 36 | `inputAberrationAmount` · `1/inputAberrationHeight` · `inputAberrationOffset` |
| 40, 44 | `cos` e `sin` de `inputAberrationAngle` |
| 48, 52, 56, 60 | `inputEdgeStart` · `End` · `OpacityStart` · `OpacityEnd` |
| 64, 68 | `cos` e `sin` de `inputRefractionAngle` |

`[BIN]` E os dois `…Height`, **e só eles**, caem exatamente nos dois floats pelos
quais o `mod99` multiplica a distância — que é a confirmação de que o
deslocamento agora está certo, e não só consistente.

`[ART]` Os bytes 16..71 batem **campo a campo** com o bloco que o AquaKit leu do
QuartzCore. Dois times, dois binários, um layout.

> **A fraqueza do controle, que este erro expôs.** O controle positivo do §27.3
> pega store que **atravessa** fronteira de campo. Ele **não** pega uma tabela
> inteira **transladada** por um múltiplo da granularidade: desloque tudo em 16 e
> cada offset continua dentro de algum campo declarado. Ele disse `PASS` o tempo
> todo com a base errada.
>
> Um controle que não distingue os dois casos não é inútil — é um controle com
> alcance declarado, e o alcance dele agora está escrito. A base passou a sair da
> instrução que a nomeia.

### 27.7. O que isto fecha

O spec do vidro (`Docs/Specs/2026-09-02-vidro.md` §6) nomeava esta ligação como o
**risco que podia não fechar**, com a regra de que sem ela o vidro ficaria nomeado
em vez de aproximado.

**Fechou.** As chaves deixam de ser hipótese: 54 delas têm byte, largura, valor
padrão e transformação, com um controle que cruza o escritor e o leitor. E o
`…Height` gravado como recíproco é o tipo de detalhe que nenhuma quantidade de
comparação visual teria encontrado.

## 28. O inventário de constantes das cinco funções do vidro

`[BIN]` As cinco funções *stitchable* do vidro vivem uma por módulo em
`References/2.0-125/out/metallib-renderbox/`. Esta seção conta **todo literal de
ponto flutuante** que elas carregam, decodifica os bits, e confere o resultado
contra o `GlassShader.h` do AquaKit — que leu os mesmos números de **outro
binário** (QuartzCore), por outro esforço, sem falar com este.

O que a seção **não** faz: ligar constante a nome de uniform. Isso é o §27, e é
outro instrumento.

### 28.1. O método, e o erro que a primeira versão cometeu

O censo é um script, não um olho. Ele varre o `.ll` linha a linha, ignora
metadata (`!…`) e comentário (`;…`), e para cada token de literal atribui o tipo
da **palavra-chave de tipo mais próxima à esquerda na mesma linha**. `half`
decodifica de `0xH____` como IEEE 754 binary16; `float` e `double` vêm no formato
hexadecimal de 16 dígitos que o LLVM usa (os bits do `double` equivalente).

> **O que a primeira versão errou, e por que importa.** Ela casava `tipo` +
> literal **adjacentes** — `half 0xH1419`. Mas o LLVM textual escreve o segundo
> operando depois de uma vírgula: `fmul half %279, 0xH39A8`. A primeira contagem
> perdeu **todo literal em posição de segundo operando**, e com ele o `0xH39A8`
> inteiro — a constante `RingDomainFactor`, que sumiu do censo em vez de aparecer
> com contagem errada. Sumir em silêncio é a pior forma de errar uma contagem,
> porque não deixa rastro. A correção foi trocar adjacência por
> **palavra-chave-mais-próxima**, e o controle é o rodapé de cada varredura: todo
> token com forma de ponto flutuante que **não** ficou sob uma palavra-chave de
> tipo é impresso. Nos cinco módulos ele imprime só três coisas, todas inertes: o
> `14.0` do `target triple`, e os sufixos de versão dos nomes de struct
> (`struct.RB::Layer.90.483`).

`[ART]` Dois controles a mais, os dois passam:

- **Nenhuma linha de metadata dos cinco módulos contém token de ponto
  flutuante.** Excluí-las não esconde nada.
- **Nenhum dos cinco módulos declara constante global de ponto flutuante.** Os
  únicos `@` globais são a `RB::shaderVariant`, o seu inicializador de constante
  de função, e dois `__air_sampler_state` (`i64`). Isto **confirma pelo censo** o
  que o spec do vidro §5.2 afirmava: não existe LUT de gradiente em nenhuma das
  cinco.

**Trivial**, aqui, quer dizer `0.0`, `±1.0`, `±0.5` e `±2.0` — os valores que
qualquer expressão produz por acidente estrutural. Eles são contados, mas ficam
numa tabela à parte, e só os que têm papel identificado aparecem.

### 28.2. A contagem, por função

`[ART]` A varredura completa:

| módulo | função | linhas úteis | ocorrências | distintos | **não-triviais distintos** | ocorr. não-triviais |
|---|---|---|---|---|---|---|
| `mod95` | `distanceGradient_v1` | 159 | 16 | 3 | **0** | 0 |
| `mod96` | `ovalizeGradient_v1` | 66 | 2 | 2 | **0** | 0 |
| `mod97` | `displacementMap_v1` | 468 | 92 | 19 | **16** | 30 |
| `mod98` | `glassBackground_v1` | 1443 | 171 | 30 | **22** | 65 |
| `mod99` | `glassForeground_v1` | 343 | 25 | 12 | **6** | 8 |

`[BIN]` **`distanceGradient_v1` e `ovalizeGradient_v1` não têm constante
nenhuma.** Os 16 e os 2 literais das duas são `0.0` e `1.0`. As duas funções são
inteiramente dirigidas por uniform — nada de aritmética embutida a transcrever, e
nada a conferir contra o AquaKit. Para a transcrição isso é notícia boa: o
`distanceGradient_v1` é diferença central e normalização, e não esconde número.

### 28.3. `displacementMap_v1` — e são os TRÊS padrões MSAA, não só o de oito

`[BIN]` As 16 constantes do `mod97` são duas famílias, e nada mais.

**A família dos jitters.** `%100 = dfdx(p)` e `%101 = dfdy(p)`; cada tap é
`p + a·dfdx + b·dfdy`, com `a` e `b` imediatos. Catorze valores distintos, cada um
com exatamente **2 ocorrências** — uma como `a`, uma como `b`:

| valor | hex `float` | ocorrências |
|---|---|---|
| ±0.0625 | `0x3D800000` / `0xBD800000` | 2 + 2 |
| ±0.125 | `0x3E000000` / `0xBE000000` | 2 + 2 |
| ±0.1875 | `0x3E400000` / `0xBE400000` | 2 + 2 |
| ±0.25 | `0x3E800000` / `0xBE800000` | 2 + 2 |
| ±0.3125 | `0x3EA00000` / `0xBEA00000` | 2 + 2 |
| ±0.375 | `0x3EC00000` / `0xBEC00000` | 2 + 2 |
| ±0.4375 | `0x3EE00000` / `0xBEE00000` | 2 + 2 |

`[BIN]` Reagrupados por tap e escritos em **dezesseis avos de pixel**, os três
ramos do `switch (shaderVariant & 7)` dão:

| `v&7` | taps | padrão, em 1/16 de pixel |
|---|---|---|
| 1 | 2 | `(4,4) (−4,−4)` |
| 2 | 4 | `(−2,−6) (6,−2) (−6,2) (2,6)` |
| 3–7 | 8 | `(1,−3) (−1,3) (5,1) (−3,−5) (−5,5) (−7,−1) (3,7) (7,−7)` |

`[INF]` **Os três são os padrões MSAA padrão do D3D/Metal, valor a valor e na
mesma ordem** — não só o de oito, que é o que o spec do vidro (§5.4) tinha
nomeado. O de 2× e o de 4× também. Isso é a peça mais fácil de gatar do vidro
inteiro: o oráculo é uma tabela de dezesseis inteiros.

**A família dos normalizadores**, um por ramo, e o único lugar onde o `mod97` usa
`half`:

| hex | valor | ocorr. | papel |
|---|---|---|---|
| `0xH3800` | 0.5 | 1 | divisor do ramo de 2 taps |
| `0xH3400` | 0.25 | 1 | divisor do ramo de 4 taps |
| `0xH3000` | 0.125 | 1 | divisor do ramo de 8 taps |

`[BIN]` O ramo de 1 tap (`v&7 == 0`) não multiplica por nada e não chama
derivada. O `2.0` (`0x40000000`, ×1) é o `disp·2 − 1` do §5.4; os 60 zeros são
acumuladores e o `lod` fixo das amostras.

### 28.4. `glassBackground_v1` — as 22 não-triviais

`[BIN]` `default_mod98.ll`. Papel lido do contexto da instrução; onde o contexto
não decide, `[OBS]`.

| hex | valor | ocorr. | papel | selo |
|---|---|---|---|---|
| `0xH1419` | 0.0010004043579101562 | **26** | o epsilon universal — piso de `fwidth`, piso de alpha antes do `fdiv` que des-premultiplica, piso do raio antes do `log2` do LOD, piso do raio do anel, e o comparando dos curtos-circuitos | `[BIN]` |
| `0xH1A0D` | 0.0029544830322265625 | 3 | `EdgeCoverage` A — `fma(A, u, B)` | `[BIN]` |
| `0xHA869` | −0.034454345703125 | 3 | `EdgeCoverage` B | `[BIN]` |
| `0xH3162` | 0.168212890625 | 3 | `EdgeCoverage` C | `[BIN]` |
| `0xHB87C` | −0.560546875 | 3 | `EdgeCoverage` D | `[BIN]` |
| `0xH3400` | 0.25 | 3 | `EdgeInputScale` — `fmuladd(t, 0.25, 0.5)` antes do `saturate` | `[BIN]` |
| `0xH4400` | 4.0 | 3 | `EdgeDomainScale` — `fmuladd(sat, 4.0, −2.0)` | `[BIN]` |
| `0xH39B9` | 0.71533203125 | 3 | luma **G** — o mesmo `half` nas DUAS trincas (§28.8) | `[BIN]` |
| `0xH32CE` | 0.212646484375 | 2 | luma **R**, trinca A | `[BIN]` |
| `0xH2C9F` | 0.07220458984375 | 2 | luma **B**, trinca A | `[BIN]` |
| `0xH34CD` | 0.300048828125 | 2 | `MaxLumaChromaBoost` — `fmuladd(1−t, 0.3, 1)`, o `k ≥ 1` que extrapola de propósito | `[BIN]` |
| `0xH39A8` | 0.70703125 | 2 | `RingDomainFactor` = 1/√2, multiplicando `d` antes do domínio | `[BIN]` |
| `0xH32CD` | 0.2125244140625 | 1 | luma **R**, trinca B | `[BIN]` |
| `0xH2C9D` | 0.07208251953125 | 1 | luma **B**, trinca B | `[BIN]` |
| `0xHBA00` | −0.75 | 1 | `ClampNegativeFloor` — `clamp(rgb/α, −0.75, u)` no valor des-premultiplicado, re-multiplicado logo depois | `[BIN]` |
| `0xH4200` | 3.0 | 1 | o `3 − 2c` de Hermite: `fma(−2, c, 3)`, depois `fma(w, isso, 1)` e `c ·` o resultado | `[BIN]` |
| `0xH3A00` | 0.75 | 1 | `mix(float(bool(x)), x, 0.75)` — mistura um degrau duro com a rampa. A que serve, `[OBS]` | `[BIN]` valor |
| `0xH3555` | 0.333251953125 | 1 | a lane do meio do normalizador por canal `(0.5, ⅓, 0.5)` da dispersão | `[BIN]` |
| `0x3F50640000000000` | 0.0010004043579101562 | 1 | o **mesmo** epsilon, promovido a `float`, num `fcmp ogt` de curto-circuito | `[BIN]` |
| `0xBFD5555560000000` | −0.3333333432674408 | 1 | passo do laço de **3** iterações da dispersão | `[BIN]` |
| `0x3FD5555560000000` | +0.3333333432674408 | 1 | passo do laço de **4** iterações da dispersão | `[BIN]` |
| `0x3FC24924A0000000` | 0.1428571492433548 | 1 | 1/7 — normalizador do alpha somado sobre os sete taps | `[BIN]` |

`[BIN]` E três das triviais têm papel identificado, o que as tira do acidente:

| hex | valor | ocorr. | papel |
|---|---|---|---|
| `0xH3800` | 0.5 | 11 | **3** como `EdgeInputBias`, **3** como o `+0.5` final do polinômio (`EdgeCoverageBias`), **3** como o `fma(±d, 1/w, 0.5)` da banda, **2** nas lanes externas do normalizador `(0.5, ⅓, 0.5)`. Onze de onze, nenhuma sobra |
| `0xHC000` | −2.0 | 4 | **3** como `EdgeDomainBias`; a quarta é o `−2` do Hermite `fma(−2, c, 3)` |
| `0xH4000` | 2.0 | 5 | **cinco** vezes o `2 − k` do perfil de altura `fma(−saturate(sqrt(k(2−k))), amp, amp)` |

`[BIN]` **Os cinco `2.0` são cinco perfis de altura.** O §5.3 do spec nomeava
dois lóbulos (o externo e o de face); a contagem diz que a mesma expressão de
altura roda **cinco** vezes no módulo. `[OBS]` A que lóbulo cada uma pertence não
foi determinado; a contagem é o fato.

`[BIN]` **A `EdgeCoverage` é avaliada exatamente três vezes** (`%289`, `%298`,
`%346`), e a contagem 3 de A/B/C/D é isso e nada mais. Duas delas — as de `%285`
e `%294` — são precedidas do fator 1/√2, e a diferença das duas é
`saturate(Φ(d₁) − Φ(d₂))`: é **literalmente** o `RingShadowBand` do AquaKit,
instrução a instrução. A terceira (`%342`) **não** leva o 1/√2 e é avaliada
sozinha.

### 28.5. `glassForeground_v1` — 6, e cinco delas são o `mod98` de novo

`[BIN]` `default_mod99.ll`:

| hex | valor | ocorr. | papel | selo |
|---|---|---|---|---|
| `0xH1419` | 0.0010004043579101562 | 3 | epsilon em `half`: um `fcmp olt` de saída antecipada, dois `fmax` de piso | `[BIN]` |
| `0x3F1A36E2E0000000` | 9.999999747378752e-05 | 1 | `fast_fmax(fwidth(d), 1e-4)` — o piso da derivada, **em `float`** | `[BIN]` |
| `0xBFD5555560000000` | −0.3333333432674408 | 1 | passo do laço de 3 da dispersão | `[BIN]` |
| `0x3FD5555560000000` | +0.3333333432674408 | 1 | passo do laço de 4 da dispersão | `[BIN]` |
| `0x3FC24924A0000000` | 0.1428571492433548 | 1 | 1/7, normalizador do alpha | `[BIN]` |
| `0xH3555` | 0.333251953125 | 1 | lane do meio de `(0.5, ⅓, 0.5)` | `[BIN]` |

`[INF]` **O bloco de dispersão cromática é o mesmo nos dois módulos, constante por
constante.** Quatro literais (`±⅓` em `float`, `1/7`, `⅓` em `half`) com a mesma
contagem e na mesma forma — dois laços de 3 e 4, sete taps, normalizador do alpha
`1/7` e normalizador por canal `(0.5, ⅓, 0.5)`. Transcrever `AberrateTexture` uma
vez serve às duas funções.

`[BIN]` E o `(0.5, ⅓, 0.5)` corrige o §5.3 do spec, que dizia "pesos `1/3` e
normalizador `1/7`": os pesos por canal são **três valores diferentes**, `0.5`
nas lanes de fora e `⅓` na do meio.

### 28.6. A conferência contra o AquaKit — 15 de 15, bit a bit

`[ART]` O `GlassShader.h` do AquaKit declara as constantes em `float` decimal,
com o `half` de origem no comentário. O teste é o mais duro disponível: arredondar
o decimal do AquaKit para `binary16` e comparar **os bits** com o que o `mod98`
carrega.

| constante (AquaKit) | valor AquaKit | hex no `mod98` | valor `mod98` | ocorr. | bate? |
|---|---|---|---|---|---|
| `EdgeCoverageA` | `0.002954483` | `0xH1A0D` | 0.0029544830322265625 | 3 | **sim** |
| `EdgeCoverageB` | `-0.034454346` | `0xHA869` | −0.034454345703125 | 3 | **sim** |
| `EdgeCoverageC` | `0.16821289` | `0xH3162` | 0.168212890625 | 3 | **sim** |
| `EdgeCoverageD` | `-0.56054688` | `0xHB87C` | −0.560546875 | 3 | **sim** |
| `EdgeCoverageBias` | `0.5` | `0xH3800` | 0.5 | 3 neste papel | **sim** |
| `EdgeInputScale` | `0.25` | `0xH3400` | 0.25 | 3 | **sim** |
| `EdgeInputBias` | `0.5` | `0xH3800` | 0.5 | 3 neste papel | **sim** |
| `EdgeDomainScale` | `4.0` | `0xH4400` | 4.0 | 3 | **sim** |
| `EdgeDomainBias` | `-2.0` | `0xHC000` | −2.0 | 3 neste papel | **sim** |
| `RingDomainFactor` | `0.70710677` | `0xH39A8` | 0.70703125 | 2 | **sim** |
| `LumaRed` | `0.21264648` | `0xH32CE` | 0.212646484375 | 2 | **sim** |
| `LumaGreen` | `0.71533203` | `0xH39B9` | 0.71533203125 | 3 | **sim** |
| `LumaBlue` | `0.07220459` | `0xH2C9F` | 0.07220458984375 | 2 | **sim** |
| `MaxLumaChromaBoost` | `0.3` | `0xH34CD` | 0.300048828125 | 2 | **sim** |
| `ClampNegativeFloor` | `-0.75` | `0xHBA00` | −0.75 | 1 | **sim** |

**Quinze de quinze.** E não é só o valor que bate: o **papel** bate em cada uma.
O `ClampNegativeFloor` do AquaKit é descrito como o piso por componente
`clamp(rgb, −0.75, limite)` **sobre o valor des-premultiplicado**; o `mod98`
divide por `max(α, ε)` em `%1290`, faz o `clamp` em `%1293` e re-multiplica por α
em `%1295`. O `MaxLumaChromaBoost` do AquaKit está no `k = 1 + 0.3·(1−t)` de
`CompressMaxLuma`; o `mod98` escreve `fmuladd(1−t, 0.3, 1)` nas duas ocorrências,
as duas coladas na trinca de luma A.

`[INF]` Quatro coeficientes de minimax de um polinômio de Φ não coincidem por
acaso, e um 1/√2 e um −0.75 acompanhando-os no mesmo papel também não. Esta é a
segunda confirmação independente do veredito **mesmo fonte** do §4.1, e é a mais
barata de auditar: são bits.

`[OBS]` Uma constante do AquaKit **não tem contraparte** neste censo:
`BlurFillSampleOffsetScale = 0.25` (QuartzCore `mod61 %246`). O `mod98` tem
exatamente três `0xH3400`, e as três são `EdgeInputScale`. Não é divergência de
valor: é **ausência de literal** — no RenderBox esse fator ou vem de uniform ou
não existe. Não foi determinado qual.

### 28.7. As divergências, e são reais

Uma divergência não é defeito. É informação sobre onde os dois binários deixam de
ser o mesmo binário.

**1. O epsilon `half`.** `[BIN]` O AquaKit lê `0xH068E` = `0.00010001659` no
`glass_background_base` do QuartzCore. O `mod98` do RenderBox usa `0xH1419` =
`0.0010004043579101562`, **26 vezes em `half` mais uma em `float`** — 27 sítios,
o literal mais frequente do módulo inteiro. **Uma década de diferença**, os dois
em `half`, os dois no mesmo papel. Confirmado.

**2. O epsilon `float`.** `[BIN]` O AquaKit lê `1e-6` (`0x3EB0C6F7A0000000`) na
variante `_lpf` do QuartzCore. O `mod99` do RenderBox usa **`1e-4`**
(`0x3F1A36E2E0000000`), uma vez, em `fast_fmax(fwidth(d), 1e-4)`. **Duas décadas
de diferença.** Também confirmado.

`[BIN]` E o censo explica **por que existem dois epsilons do lado do RenderBox**,
o que o spec do vidro §2.1 não tinha: o `mod98` calcula a cobertura em `half`
(`air.fwidth.f16`, em `%91` e `%171`) e o `mod99` a calcula em `float`
(`air.fwidth.f32`, em `%86`). Tipo diferente, piso diferente. `[OBS]` Por que o
RenderBox escolheu 1e-3 e 1e-4 onde o QuartzCore escolheu 1e-4 e 1e-6 não é
determinável daqui.

**Para a transcrição vale o epsilon do RenderBox**, porque o alvo deste projeto é
o RenderBox. Mas a diferença é grande o bastante para ser observável: `1e-3`
contra `1e-4` num piso de `fwidth` muda a cobertura de qualquer aresta cuja
derivada caia abaixo de um milésimo. Não é ruído de ULP.

**3. Uma contagem que bate sem o papel bater.** `[BIN]` A tabela do spec §2
registra `EdgeDomainScale`/`Bias` como `3 / 4`. O censo confirma os números — mas
**só três dos quatro `−2.0` são `EdgeDomainBias`**. O quarto é o `−2` do Hermite
`fma(−2, c, 3)` em `%1226`, outro subsistema. A contagem estava certa; a leitura
"quatro vezes o bias do domínio" estaria errada. É a mesma armadilha do §26.2 com
outra roupa: o número coincidir não é o número significar.

### 28.8. As duas trincas de luma — e a segunda **não** é a SMPTE

`[BIN]` O `mod98` carrega duas trincas de luma, e elas aparecem em três sítios:

| sítio | trinca | forma |
|---|---|---|
| `%878` | A = `0xH32CE / 0xH39B9 / 0xH2C9F` | `dot(const, rgb)` — constante no **primeiro** operando |
| `%1126` | A = `0xH32CE / 0xH39B9 / 0xH2C9F` | `dot(const, rgb)` — constante no **primeiro** operando |
| `%1062` | B = `0xH32CD / 0xH39B9 / 0xH2C9D` | `dot(rgb, const)` — constante no **segundo** operando |

`[ART]` A aritmética das contagens fecha exata: R vale 2 + 1, B vale 2 + 1, e **G
vale 3 porque as duas trincas colidem no mesmo `half`**. `0xH39B9` é literalmente
o mesmo token nas duas — confirmado.

`[BIN]` **Quem usa qual.** A trinca A alimenta as duas instâncias de
`CompressMaxLuma`: em `%878` e `%1126` o produto escalar é seguido de
`t = saturate(fma(−y, complemento, 1))`, do `k = fma(1−t, 0.3, 1)` e do
`mix(t·y, t·rgb, k)` — a transcrição do AquaKit, sem uma instrução sobrando. A
trinca B alimenta **o especular**: em `%1062` o produto escalar é saturado, entra
num `fma(u, l, b)`, é **elevado à quarta potência** por dois `fmul` sucessivos e
vira o peso de um `mix`. São dois subsistemas, e é exatamente o padrão do §26.4 —
constantes de luminância diferentes querem dizer famílias diferentes.

`[ART]` **A distância entre as duas é 1 ULP em R e 2 ULP em B**, não 1 e 1:
`0x32CE − 0x32CD = 1`, `0x2C9F − 0x2C9D = 2`.

`[BIN]` **E a trinca B não é a Rec.709 arredondada.** O intervalo de reais que
arredonda para `0xH32CD` é `[0.21246338, 0.21258545)` e o que arredonda para
`0xH2C9D` é `[0.07205200, 0.07211304)`. Os coeficientes da Rec.709 — `0.2126` e
`0.0722`, ou os exatos `0.2126729` e `0.0721750` — caem **fora dos dois**. A
trinca A cai dentro nos três canais; a B não cai em nenhum dos dois que diferem.

`[BIN]` Também não é a SMPTE 240M (`0.212 / 0.701 / 0.087` → `0xH32C9 / 0xH399C /
0xH2D91`), nem a Rec.601, nem o `Lum()` do PDF, nem a P3-D65. **Nenhuma delas
colidiria no G**, que é a impressão digital que o §4.1 usou.

`[BIN]` **E a identidade da trinca B fecha, uma linha depois de onde a análise
acima parou.** Ela mesma diz que a fonte tem R em `≈0.2125` e B em `≈0.0721`;
essa trinca existe e é publicada:

```
A   0.2126  0.7152  0.0722   ->  0xH32CE  0xH39B9  0xH2C9F   soma 1.0000
B   0.2125  0.7154  0.0721   ->  0xH32CD  0xH39B9  0xH2C9D   soma 1.0000
```

`[BIN]` Os três canais de B batem, e **as duas somam exatamente 1** — que é o que
separa isto de um palpite. Dois arredondamentos arbitrários não somam 1 os dois.

`[INF]` **As duas são Rec.709, em dois arredondamentos de quatro casas
diferentes** — o segundo é o que circula em Poynton e em boa parte da literatura
de vídeo. Não são dois padrões: é o mesmo padrão copiado de duas referências.

> **O que cada lado errou aqui.** O §4.1 chamou a trinca B de **SMPTE**, e está
> errado: a SMPTE 240M é `0.212 / 0.701 / 0.087` e cai em `0xH32C9 / 0xH399C /
> 0xH2D91`, longe nos três canais. **A correção é real e o §4.1 foi corrigido.**
>
> Mas a análise que achou o erro concluiu `[OBS]` — identidade indeterminada — e
> isso passou do ponto na direção oposta. O teste que refutou a SMPTE foi aplicado
> só contra `0.2126`/`0.0722`, e nunca contra `0.2125`/`0.0721`, que é a trinca
> que a própria conclusão descreve. **Descartar a hipótese certa por não tê-la
> testado é o mesmo tipo de erro que promover a errada**, só que mais barato de
> desfazer.

### 28.9. O que isto fecha, e o que continua aberto

`[ART]` **44 constantes não-triviais distintas** nas cinco funções — 0 + 0 + 16 +
22 + 6 —, cada uma com bits, valor decimal e contagem medida. **15 conferidas
contra o AquaKit, 15 batendo bit a bit**, com o papel batendo junto. **Duas
divergências reais**, as duas no epsilon, as duas confirmadas dos dois lados.

O que a tarefa 2 do spec do vidro pede — `GlassCommon.glsl` + `GlassOracle` com
prova dupla — tem agora o segundo braço da prova pronto: as constantes não são
hipótese, são duas leituras independentes do mesmo número, e a lista de quais
divergem é finita e nomeada.

O que continua `[OBS]`, dito como tal:

| | |
|---|---|
| a fonte decimal da trinca de luma B | intervalo conhecido, identidade não |
| a que serve o `0xH3A00` = 0.75 em `%206` | a aritmética está lida, o papel não |
| a qual lóbulo pertence cada um dos cinco perfis de altura | a contagem é 5, a atribuição não foi feita |
| se o `BlurFillSampleOffsetScale` do AquaKit vira uniform no RenderBox ou desaparece | não há literal para comparar |
| por que o RenderBox escolheu 1e-3/1e-4 onde o QuartzCore escolheu 1e-4/1e-6 | a diferença de tipo explica *haver* dois, não *quais* |

## 29. Do documento ao uniform — a ponte que NÃO fecha, e por quê

O §27 leu quem escreve cada byte dos 256 do `BackgroundUniforms` a partir de
**83 chaves no estilo `CIFilter`**. O que faltava era o outro lado: os oito
campos que o documento `.icon` carrega (`IconRendering.Icon.GlassMaterial`)
chegam a essas chaves **como**?

**Resposta curta: não chegam.** Nesta versão, dentro deste corpus, o
`glassBackground_v1` **não é alcançável** a partir do `IconRendering`. O que o
`IconRendering` faz com o `GlassMaterial` foi lido, e é outra coisa — e a regra
do §6.2 do spec do vidro se aplica na direção que ela previa: o vidro do
`glassBackground` continua **nomeado e não desenhado**.

Esta seção registra as peças que foram lidas, e a fronteira exata.

### 29.1. Quem seleciona o empacotador dos 75 — e é um `CAFilter`

`[BIN]` O `RenderBox.arm64` **tem tabela de símbolos** (11.996 entradas), e ela
nomeia diretamente as funções que o §27 tinha achado por contagem:

| endereço | símbolo |
|---|---|
| `0x0E6D44` | `(anonymous)::CAFilterContext::glass_background_filter()` |
| `0x0E7D60` | `(anonymous)::CAFilterContext::glass_foreground_filter()` |
| `0x0E80AC` | `(anonymous)::CAFilterContext::float_value(NSString*, double)` |
| `0x0E5E38` | `RBDrawingStateAddCAFilter` |

`[BIN]` O ajudante `(objeto, chave, default) -> double` do §27.2 é
`CAFilterContext::float_value(NSString*, double)`. O "objeto" é o **`CAFilter`**;
as 83 chaves são **chaves de `CAFilter`**, não de shader.

`[BIN]` E o seletor está escrito em texto. Em `RBDrawingStateAddCAFilter` o tipo
do filtro é comparado com `isEqualToString:` contra CFStrings:

| endereço | CFString | destino |
|---|---|---|
| `0x0E655C` → `0x0E6570` | `"glassBackground"` (CFString em `0x191020`) | `glass_background_filter()` |
| `0x0E65C4` → `0x0E65D8` | `"glassForeground"` (CFString em `0x191040`) | `glass_foreground_filter()` |

`[INF]` Os 75 uniforms são preenchidos quando **um `CAFilter` cujo `type` é
`"glassBackground"`** é anexado a uma camada que o RenderBox desenha. Quem
constrói esse `CAFilter` — e portanto quem escolhe os 75 valores — é quem cria o
filtro, não o RenderBox.

`[ART]` E **ninguém neste corpus o cria**. A string `glassBackground` aparece
**3 vezes no `RenderBox.arm64` e 0 vez** em `CoreSVG`, `IconComposer`,
`IconComposerFoundation`, `IconComposerKit`, `IconRendering`, `icrtool` e
`ictool`. Dos cinco *system shaders* que o RenderBox exporta —
`RBSystemShaderDistanceGradient`, `OvalizeGradient`, `DisplacementMap`,
`GlassBackground` (`0x19DD40`), `GlassForeground` (`0x19DD48`) — o
`IconRendering` importa **exatamente um**: `_RBSystemShaderDisplacementMap`.

> **Isto é a leitura, não a desistência.** A pergunta "como os 8 campos viram os
> 75 slots" pressupunha que o Icon Composer desenha `glassBackground_v1`. Ele não
> desenha. O caminho dos 75 é o vidro do **sistema** (um `CAFilter` montado fora
> destes binários, no QuartzCore/CoreMaterial); o caminho do ícone é outro, e
> esse foi lido abaixo.

### 29.2. `Icon.GlassMaterial` — o layout, lido dos acessores exportados

`[BIN]` O `IconRendering.arm64` exporta *getter*, *setter* e *modify* por
propriedade, e o corpo do getter é uma instrução. Isso dá o offset sem
inferência:

| byte | B | campo | acessor |
|---|---|---|---|
| `+0x00` | 1 | `hasSpecular: Bool` | `0x38DB8` |
| `+0x01` | 1 | `shadowStyle: ShadowStyle` | `0x38DC8` |
| `+0x08` | 8 | `shadowOpacity: Double` | `0x37F1C` |
| `+0x10` | 8 | `translucency: Double` | `0x38DF0` |
| `+0x18` | 8 | `blurStrength: Double` | `0x38E00` |
| `+0x20` | 8 | `refractionHeight: Double` | `0x38E10` |
| `+0x28` | 8 | `refractionStrength: Double` | `0x38E20` |
| `+0x30` | 1 | `specularPlacement: SpecularPlacement` | `0x38E30` |

`[BIN]` Os dois enums são bytes densos, na ordem do metadado de reflexão:
`ShadowStyle` = `automatic 0`, `none 1`, `vibrant 2`, `neutral 3`;
`SpecularPlacement` = `automatic 0`, `inside 1`, `outside 2`.

`[BIN]` E há **duas propriedades derivadas** que o metadado de campos não lista,
porque são computadas — as duas saem só do `shadowStyle`:

```
hasShadow                (0x38F58)  =  shadowStyle != .none      ; cmp #1, cset ne
shadowInfusesGlyphColor  (0x38FB0)  =  shadowStyle == .vibrant   ; cmp #2, cset eq
```

`[INF]` `neutral` e `automatic`, portanto, **desenham sombra e não infundem
cor**; só `vibrant` infunde. Um transcritor que tratasse `neutral` como "sem
sombra" erraria o pixel, e o nome não avisaria.

`[BIN]` Há ainda um init de conveniência de 5 argumentos em `0x38E58`
(`hasSpecular`, `shadowStyle`, `shadowOpacity`, `translucency`, `blurStrength`)
que preenche o resto de uma constante em `0x93B30`:
**`refractionHeight = 0.5`, `refractionStrength = 0.0`,
`specularPlacement = automatic`.** A mesma constante é usada em `0x0BC98` quando
o `ICRIconLayer` **não responde** a `refractionHeight` — o *fallback* de
compatibilidade.

`[BIN]` A travessia Swift↔ObjC existe e é direta: `0x0BBC0` lê os oito de um
`ICRIconLayer` por `objc_msgSend` (`hasSpecular`, `shadowStyle`,
`shadowOpacity`, `translucency`, `blurStrength`, e — atrás de um
`respondsToSelector:` — `refractionHeight`, `refractionStrength`,
`specularPlacement`) e monta o struct; `0x25588`–`0x255F8` faz o caminho inverso
com os oito `set…:`. **Nenhuma aritmética nos dois sentidos** — é transporte,
não conversão.

### 29.3. Os sete números da normalização, com endereço e valor

`[BIN]` Os campos de `ICRRenderingParameters` cujos nomes têm forma de
mapeamento têm offset lido do getter exportado, não deduzido de layout:

| offset | campo | getter |
|---|---|---|
| `+0x1E8` | `blurStrengthMax` | `0x61F68` |
| `+0x1F0` | `refractionHeightMin` | `0x61F88` |
| `+0x1F8` | `refractionHeightMax` | `0x61FA8` |
| `+0x200` | `refractionHeightPower` | `0x61FC8` |
| `+0x208` | `refractionStrengthMax` | `0x61FE8` |
| `+0x210` | `refractionStrengthPower` | `0x62008` |
| `+0x218` | `refractionSupersampling: Int` | `0x62018` |
| `+0x220` | `shouldClampPlusLBlending: Bool` | `0x62038` |
| `+0x228` | `defaultChicletCornerRadius` | `0x62058` |

`[BIN]` E o construtor agregado do §19 (`0x5E838`) escreve exatamente nesses
offsets, com os valores vindos de `__TEXT.__const`:

| endereço | instrução | offsets | valores |
|---|---|---|---|
| `0x5EAC8` | `stp q0, q1, [x19, #0x1E0]` | `+0x1E0`…`+0x1F8` | `0.005`, **`64.0`**, **`12.8`**, **`256.0`** |
| `0x5EAD4` | `str q0, [x19, #0x200]` | `+0x200`, `+0x208` | **`1.0`**, **`640.0`** |
| `0x5EADC` | `str x23, [x19, #0x210]` | `+0x210` | **`1.0`** |
| `0x5EAE4` | `str x24, [x19, #0x218]` | `+0x218` | **`2`** |
| `0x5EAE8` | `strb w22, [x19, #0x220]` | `+0x220` | **`true`** |
| `0x5EAFC` | `str x8, [x19, #0x228]` | `+0x228` | **`266.24`** |

Os pools: `0x985D0` = (`0.005`, `64.0`), `0x985E0` = (`12.8`, `256.0`),
`0x985F0` = (`1.0`, `640.0`).

`[INF]` A escala é a tela de **1024**: `266.24 = 1024 × 0.26`, `12.8 = 1024/80`,
`256 = 1024/4`, `640 = 1024 × 0.625`. Os números são pontos num ícone de 1024,
não frações.

> **O controle desta tabela, e ele é duplo.** Primeiro: o getter de
> `sdfGeneration` (`0x61AA4`) lê `+0x1C8`, `+0x1D0`, `+0x1D8`, `+0x1E0` — quatro
> campos que **terminam exatamente em `+0x1E8`**, que é onde o metadado de
> reflexão diz que `blurStrengthMax` começa. Duas fontes independentes, mesmo
> byte. Segundo: os seis `Double` seguintes ao alvo, em `+0x258`…`+0x280`, são o
> `SpatialHighlighting` (`alignmentRange`, `intensityPower`, `minIntensity`,
> `spreadPower`, `heightPower`, `maxExtraHeight`), e o `ctormap` lê ali
> `0.3141592653589793`, `2.0`, `0.5`, `2.0`, `2.0`, `1.0` — e há um consumidor,
> em `0x12550`, que usa **esses seis, nessa ordem, nesses papéis**, com π
> literal em `0x125F0` como alvo da interpolação de `spread`. Um grupo de seis
> cujo nome, offset, valor e uso concordam é o que valida a régua com que os
> sete acima foram lidos.

### 29.4. A aritmética da desnormalização — lida, em `0x4A708`

`[BIN]` Existe **um único lugar** em todo o `IconRendering.arm64` que chama
`_pow` com esses parâmetros (`__text` decodificado a 100%; `_pow` é o stub
`0x8DDF4`, e os sete chamadores são `0x125C8`, `0x125EC`, `0x12620` — o
`SpatialHighlighting` acima — e `0x4A728`, `0x4A768`, `0x4A998`, `0x4A9D8`). Os
quatro últimos são o mesmo par de expressões em dois ramos do mesmo corpo.
Transcritas de `0x4A708`:

```
; altura  —  params +0x1F0 / +0x1F8 / +0x200
h = fminnm(x, 1.0)                 ; teto em 1
h = (h >= 0) ? h : 0               ; piso em 0
h = pow(h, refractionHeightPower)
out_height = refractionHeightMin + (refractionHeightMax - refractionHeightMin) * h

; forca   —  params +0x208 / +0x210
m = fminnm(|s|, 1.0)
g = copysign(1.0, s)               ; bsl v2, v4(1.0), v3(s)
g = (s == 0 || isnan(s)) ? 0 : g
out_strength = refractionStrengthMax * g * pow(m, refractionStrengthPower)
```

`[BIN]` E o desfoque, em `0x4A948`, sem `pow` e **sem piso**:

```
radius = min(b, 1.0) * blurStrengthMax     ; -> addBlurFilterWithRadius:opaque:
```

`[INF]` A assimetria é informação: a **altura** é grampeada nos dois lados; a
**força** preserva o sinal (uma força negativa inverte a refração) e grampeia só
o módulo; o **desfoque** só tem teto — um `blurStrength` negativo produziria raio
negativo. Quem transcrever com `clamp(0,1)` nos três erra o caso negativo da
força, que é justamente o que o sinal preservado existe para permitir.

`[BIN]` **A base.** As seis cargas são `[x20+0x250]`, `[+0x258]`, `[+0x260]`,
`[+0x268]`, `[+0x270]`, `[+0x278]`, com `x20` = `swiftself`. O deslocamento
contra os offsets lidos dos getters é constante, `0x68` — o
`ICRRenderingParameters` está embutido em `self+0x68`. `[BIN]` A confirmação
independente disso não é o encaixe: é `0x10CAC`, `ldr x23, [x20, #0x280]`, cujo
valor vai — depois de dois testes de faixa — para `-[RBShader setVariant:]`.
`0x280 − 0x68 = 0x218`, e `+0x218` é `refractionSupersampling: Int`. Um inteiro
usado como variante de shader é exatamente o que aquele nome promete, e é o
sétimo campo consecutivo do bloco.

### 29.5. Para onde os desnormalizados vão — e é `displacementMap`, não `glassBackground`

`[BIN]` A altura e a força saem de `0x4A708` direto para `0x10C14`, e essa
função faz duas coisas, as duas legíveis:

**(a) um estilo de display-list.** `-[RBDisplayList addStyle:data:]` com
`w2 = 3`. O `RBDrawingStateAddStyle` do RenderBox (`0x40AB0`) despacha por tabela
de saltos em `0x15E440`, e o caso 3 chama
`RB::DisplayList::State::add_glass_displacement(...)` (`0xF39F0`) e o
serializador XML `RB::XML::DisplayList::add_glass_displacement` (`0xEDB24`). Esse
serializador **nomeia cada campo do blob**, e o despachante confirma cada offset:

| byte | B | nome no XML | o que o `IconRendering` escreve |
|---|---|---|---|
| `+0x00` | 1 | `component` (canal do campo de distância) | `0` |
| `+0x01` | 1 | `gradient` (`sampled`, …) | `1` |
| `+0x04` | 8 | `range` (`float2`) | `(v, −v)` |
| `+0x0C` | 4 | `offset` | `0` |
| `+0x10` | 4 | `height` | altura em **texels do SDF** |
| `+0x14` | 4 | `curvature` | `1.0` (const `0x93988`) |
| `+0x18` | 4 | `angle` | `0.0` (const `0x93988`) |
| `+0x1C` | 4 | `mask-offset` | `0` |

`[BIN]` A conversão de pontos para texels está em `0x10C8C`:
`height = out_height × (n − 2) / [self+0x568]`, com `n` vindo de uma chamada que
devolve a dimensão do campo de distância.

**(b) o shader.** `-[RBShader initWithSystemShader:]` com
`_RBSystemShaderDisplacementMap` (GOT `0xC5880`), **um** argumento —
`setArgumentBytes:atIndex:0 type:1 count:1` com o `float` **`−out_strength`** —,
`setVariant:` com `refractionSupersampling`, e
`addFilterLayerWithShader:border:layerBorder:bounds:flags:`.

`[ART]` O mesmo par de chamadas aparece uma segunda vez, em `0x805B8`/`0x805F4`,
com o mesmo estilo 3, a mesma constante `(1.0, 0.0)` de `curvature`/`angle` e o
mesmo `−strength`. São dois sítios, um padrão.

`[BIN]` E o irmão do estilo 3 existe: o caso **2** da mesma tabela é
`add_glass_highlight` (`0xF3550`), cujo blob o XML (`0xED778`) nomeia inteiro —
`component`, `gradient`, `color` (`float4`), `color-space`, `headroom`, `range`,
`offset`, `height`, `angle`, `spread`, `bias`, `curvature`, em `0x3C` bytes.
`[ART]` O `IconRendering` usa os estilos `0`, `1`, `3`, `9` e `10`, e **nunca o
2** — o especular do ícone não passa pelo `glass-highlight` do RenderBox.

`[INF]` Portanto o vidro que o Icon Composer realmente desenha é
`displacementMap_v1` (a refração) mais um desfoque de CoreAnimation, e não
`glassBackground_v1`. O §28.3 já tinha o inventário de constantes dessa função —
é essa, e não a de 75 uniforms, que o alvo precisa transcrever primeiro.

### 29.6. O elo que NÃO foi lido, dito como tal — e que FECHOU em 15/09/2026

> **Esta seção descreve um estado que acabou.** O `[OBS]` abaixo foi fechado no
> mesmo dia pelo laudo da sombra (`Docs/Laudos/2026-09-15-sombra.md`), de carona:
> o descritor de `0xC0` bytes começa **no próprio `GlassMaterial`**, e `0x4A30C`
> lê `[x0+0x18]`/`[x0+0x20]` como os operandos de `0x4A948`/`0x4A708`. O `x` **é**
> o `refractionHeight`. O §29.8 item 2 já estava riscado como fechado; o corpo
> desta seção continuou afirmando `[INF]` por um dia inteiro, e essa contradição
> interna é o motivo de o texto ficar aqui em vez de sumir: **ele mostra o custo
> de fechar num lugar e não no outro.** O que segue é a leitura de 05/09,
> preservada, e não o estado de hoje.

~~`[OBS]`~~ **O dado que entra em `0x4A708` não foi rastreado até
`GlassMaterial.refractionHeight`.** No ramo que se consegue seguir sem
suposição, o valor normalizado vem de um global protegido por `swift_once`
(*token* `0xCDE68`, corpo `0xCDE70`) cujo inicializador, `0x3DAA0`, escreve
**quatro `1.0`** (`fmov v0.2d, #1.0; stp q0, q0, [x8]`). O `IconRendering` foi
compilado com WMO: a função recebe um descritor de desenho de `0xC0` bytes
copiado inteiro de um elemento de array (`0x439D4`–`0x43A84`), e os campos do
material já estão dissolvidos dentro dele.

Então o que está lido é: **os nomes, os offsets, os valores, a aritmética e o
consumidor**. O que **não** está lido é a atribuição do argumento — que
`x = material.refractionHeight`. Que seja ele é `[INF]` a partir de três coisas
que não são o encaixe: o nome da constante (`refractionHeightMin/Max/Power`
contra o único `refractionHeight` do sistema de tipos), o grampo em `[0,1]` antes
do `pow` (que só faz sentido para um parâmetro normalizado), e a saída em pontos
de uma tela de 1024. É `[INF]`, e fica marcado assim.

### 29.7. Os oito campos, um a um

> **Esta tabela foi reescrita em 16/09/2026, e o motivo é o pior tipo de dívida
> que um documento pode ter.** Até então ela dizia `[OBS] consumo` em **quatro**
> linhas cujo consumo o §29.8, logo abaixo, já lia inteiro — uma delas
> (`hasSpecular`) com o próprio §29.8 escrevendo a frase *"o que fecha também o
> `[OBS] consumo` dele no §29.7"* e a linha nunca sendo editada. Um documento
> desatualizado faz alguém perder tempo; um documento que **mente sobre o próprio
> estado** faz alguém gastar uma frente inteira numa pergunta já respondida. A
> tabela é agora derivada do §29.8, e não o contrário.

| campo | o que está estabelecido | selo |
|---|---|---|
| `hasSpecular` | offset `+0x00`; trafega literalmente por `ICRIconLayer.hasSpecular`. **Não é "campo sem aritmética": é o portão de uma função inteira**, `0x491C0`–`0x49DBC`, testado na terceira instrução, e o destino é o shader `glassHighlight` do metallib do próprio `IconRendering` — **não** o `glass-highlight` do RenderBox. E ele acende **cinco** realces, não um (§37) | `[BIN]` transporte e consumo |
| `shadowStyle` | offset `+0x01`, enum denso 0–3; `hasShadow = (≠ none)`, `shadowInfusesGlyphColor = (== vibrant)`, lidos de `0x38F58`/`0x38FB0`. O estilo troca o **consumidor**, não um bit (§35) | `[BIN]` |
| `shadowOpacity` | offset `+0x08`; atravessa **cru** até o `alpha:` do `drawShape:`: `alpha = shadowOpacity × Shadow.<vibrant\|neutral>Opacity[3−classe] × FinalizedIcon.Layer.opacity`. Não há desnormalização (§35.1) | `[BIN]` transporte e consumo |
| `translucency` | offset `+0x10`; **não há** `translucencyMax`/`Power` em `ICRRenderingParameters` porque não há desnormalização. `f = glyphTranslucency.strength[classe] × translucency`, `eff(x) = 1 − (1 − x)·f`, consumida pelo shader `simplifiedShapeAwareGradientMask` | `[BIN]` transporte e consumo |
| `blurStrength` | offset `+0x18`; `radius = min(b,1) × blurStrengthMax`, `blurStrengthMax = 64.0`; destino `addBlurFilterWithRadius:opaque:`. O kernel é **gaussiana separável truncada** com **`σ = raio` exatamente**, e o alvo nunca paga os taps que isso implicaria — ele desce a resolução subtraindo a variância que a descida introduz (§36). O que ele desfoca é o **fundo** (`needs-background`), atrás de um portão que só agora tem nome: a tag de `effectsFrame` (§36.3) | `[BIN]` aritmética, kernel e superfície · `[INF]` que o `b` seja este campo |
| `refractionHeight` | offset `+0x20`; `min + (max−min)·pow(clamp01(h), p)` com `12.8 / 256.0 / 1.0`; vira `height` do `glass-displacement`, convertido a texels. A entrada deixou de ser `[INF]` em 15/09/2026: `0x4A30C` lê `[x0+0x18]`/`[x0+0x20]` como os operandos de `0x4A948`/`0x4A708` (§29.6) | `[BIN]` aritmética e entrada |
| `refractionStrength` | offset `+0x28`; `max · sign(s) · pow(min(\|s\|,1), p)` com `640.0 / 1.0`; vira o **único** argumento do `displacementMap_v1`, **negado**. `[ART]` 2 de 271 grupos do corpus refratam, **os dois com força negativa** (§40) | `[BIN]` aritmética · `[BIN]` a entrada, pelo mesmo descritor |
| `specularPlacement` | offset `+0x30`, enum denso 0–2, e os três valores estão medidos: `inside` e `outside` são **a mesma espessura, a mesma âncora, espelhados**; `automatic` segue o estado identidade **para `outside`**. E o bit é metade da decisão — `0x494E0`–`0x494FC` dá `(outsetOpacity_tag==1) ? 1 : (bit \| ¬isDarklight)` (§37.1) | `[BIN]` transporte e consumo |

### 29.8. O que um implementador ainda não tem

1. `[OBS]` **Qualquer** valor para os 75 slots do `glassBackground_v1` a partir
   de um documento. Eles vêm de um `CAFilter` de tipo `"glassBackground"` que
   nenhum binário deste corpus constrói. Fechar isso exige o **QuartzCore**, que
   não está em `References/`.
2. ~~`[OBS]` A ligação do argumento: que o `x` de `0x4A708` seja
   `material.refractionHeight`.~~ **FECHADO em 15/09/2026** pelo laudo da sombra
   (`Docs/Laudos/2026-09-15-sombra.md`), de carona: o descritor de `0xC0` bytes
   começa **no próprio `GlassMaterial`**, e `0x4A30C` lê `[x0+0x18]`/`[x0+0x20]`
   como os operandos de `0x4A948`/`0x4A708`. O `x` **é** o `refractionHeight`, e
   isso deixa de ser `[INF]`.
3. **Dois dos três FECHADOS em 15/09/2026**, e a suposição embutida nesta linha
   era o que travava: não havia par `Max`/`Power` porque **não há desnormalização**.
   No lugar do par existe um sub-struct por campo, com tabela **por classe de
   tamanho** (`SizeBasedValue`, quatro `Double`).
   - `translucency` — `[BIN]` `f = glyphTranslucency.strength[classe] × translucency`,
     `eff(x) = 1 − (1 − x)·f`, consumida pelo shader `simplifiedShapeAwareGradientMask`
     (`Docs/Laudos/2026-09-15-translucencia.md`).
   - `shadowOpacity` — `[BIN]` atravessa **cru**: `alpha = shadowOpacity ×
     Shadow.<vibrant|neutral>Opacity[3−classe] × [descritor+0x38]`, direto no `alpha:`
     do `drawShape:` (`Docs/Laudos/2026-09-15-sombra.md`). **FECHADO em 15/09/2026**
     (`Docs/Laudos/2026-09-15-sombra-desenho.md`): o terceiro fator é
     `[BIN]` **`IconRendering.FinalizedIcon.Layer.opacity`**, e o próprio "descritor
     de `0xC0` bytes" **é** o `FinalizedIcon.Layer` — três leituras independentes
     concordam: a reflexão (`__swift5_fieldmd` `0xA301C`, `opacity` tipado `Sd`), o
     vetor de deslocamentos emitido (`0xBD3F0`, `opacity@0x38`, VWT size/stride
     `0xC0`) e o **escritor** (`0x17038 str d8,[x8,#0x58]`, com `d8` vindo de
     `Icon.Layer.opacity` — cópia verbatim). De carona, duas correções: `+0x31`
     **não** é um `Bool`, é `blendMode`, e `+0xB0` é `shadowImage`. **A sombra
     desenha** — `Apollo-Reborn/AppIcon` muda 388.659 de 1.048.576 pixels.
     `[ART]` das 302 resoluções de `shadow` do corpus, **três passam de 1.0** e caem
     em décimos exatos contra a tabela medida (`2.4 × 0.375 = 0.9`), o que corrobora
     o construtor **e** a inexistência do grampo pelo documento.
   - `specularPlacement` — **FECHADO em 15/09/2026**
     (`Docs/Laudos/2026-09-15-especular.md`). `[BIN]` O brilho desemboca no shader
     **`glassHighlight`** do metallib do próprio `IconRendering` (montado por nome em
     `0xE834`, literal *small string* em `0xE92C`–`0xE948`, dez
     `setArgumentBytes:atIndex:`, `drawShape:` em `0xED00`) — **não** é `CAFilter`,
     **não** é o `glass-highlight` do RenderBox. `hasSpecular` deixa de ser "campo
     sem aritmética": é o **portão de uma função inteira** (`0x491C0`–`0x49DBC`),
     o que fecha também o `[OBS] consumo` dele no §29.7. Os três valores do enum:
     `inside` e `outside` são **a mesma espessura, a mesma âncora, espelhados**
     (`[inset, inset+height]` contra `[inset−height, inset]`), com o de fora chapado
     (curvatura 0) e opacidade própria; `automatic` segue o estado identidade **para
     `outside`**, direção oposta à do portão de forma idêntica na sombra. E o bit era
     **metade** da decisão: `0x494E0`–`0x494FC` dá
     `(outsetOpacity_tag==1) ? 1 : (bit | ¬isDarklight)`.
     `[OBS]` **Não desenha**, e a parede tem tamanho: os nove valores do shader vêm
     de `ICRRenderingParameters.Highlights` (`params+0x250`), **16.113 bytes**
     construídos em `0x62A78`–`0x63C1C`, nenhum número lido. A parede **não** é o
     QuartzCore: o shader é do `IconRendering` e está em `References/`.
     `[ART]` **67 dos 145 documentos** pedem especular (103 valores: 64 `true`,
     36 `false`, 3 `"inside"`, **0 `"outside"`**).
     **E os 16.113 bytes abriram no mesmo dia** (`Docs/Laudos/2026-09-15-highlights.md`):
     `Highlights` é preâmbulo de `0x118` + dez `HighlightsSet` de passo `0x630`, e
     `0x118 + 9×0x630 + 0x629 = 0x3EF1` **fecha na unidade** — com os dez
     deslocamentos lidos do `__text` como **imediatos** (`0x62588`, `0x627B4`),
     espaçados exatamente `0x630`. Cada conjunto tem seis `HighlightSettings` de
     passo `0x108`, e um único byte (`+0x100`) acumula três papéis: `blendModeOverride`
     e os discriminadores de dois Optionals.
     **O achado que muda a figura: `hasSpecular` acende CINCO realces, não um.**
     `0x30E88` expande um conjunto em sete candidatos (`keySharp`, `keyDiffuse`,
     `fillSharp`, `fillDiffuse`, `dark` espelhado duas vezes, `rim`) e `0x31384`
     derruba os `nil` — com os padrões de glifo sobram cinco. Isso explica **de
     dentro** o passo `0x58`/`0x60` que o laudo do especular tinha medido sem saber
     o que contava. `dark` é o **único** dos seis com `outsetOpacity`, o que fecha
     pelos números a segunda recusa de `0x494F8`: `isDarklight` e
     `outsetOpacity != nil` selecionam **o mesmo membro**, não duas condições.
     Resolução (`0x4BD90`): `height = max(distance[k], minDistancePixels[k]×escala)`,
     `direction = (cosφ·sinθ, cosφ·cosθ, sinφ)`, `color = (b,b,b,1)`,
     `blendMode = override ?? (brightness < 0.5 ? 4 : 8)` — e `4`/`8` **são**
     `PlusDarker`/`PlusLighter` como `BlendMode.h` já os numerava a partir do
     RenderBox, meses antes: controle cruzado que ninguém escolheu.
     **A armadilha silenciosa desta família:** `spread' = cos(spread)` com sentinela
     `−1000` acima de π (`0xEE3C`–`0xEF5C`). Transcrever `spread` em radianos deixa
     `lit = saturate((dot − spread')/…)` identicamente zero — compila, roda, **não
     avisa e não desenha**. E aqui a inversão `slots[3−k]`, lida de um **terceiro**
     sítio (`0x4BEB0`), finalmente **aparece no pixel**: ao contrário da sombra, os
     quatro números de `distance` diferem (4 em display, 6 nas outras).
     **O especular desenha.** ~~`[OBS]` o que resta é escalar, não forma: `ctx[0]`
     (`0x4C010`), tomado como `1.0`, erraria tudo por um fator só; e a arte raster
     segue sem brilho, porque não tem contorno e portanto não tem campo de
     distância.~~ **As duas metades fecharam no mesmo dia**, e nenhuma delas do jeito
     que esta linha esperava. `ctx[0]` é
     `[BIN]` `GlobalConfiguration.lightIntensity`, campo `+0x00` do quinto argumento
     de `0x4266C`, copiado para a base do contexto em `0x42884`, lido num **único**
     sítio (`0x4C010`), e o valor **é** `1.0`, assado como `0x3ff0000000000000` no
     init `lightAngle:` (`0x35DF0`) — a tomada estava certa e agora é `[BIN]`
     (`Docs/Laudos/2026-09-15-realce-intensidade.md`, §37.3). E a lacuna do raster
     não era lacuna: `[BIN]` o alvo tira o campo de distância do **alfa
     rasterizado** da camada, não do contorno vetorial dela, de modo que raster e
     vetor **não são dois casos**
     (`Docs/Laudos/2026-09-15-vidro-sobre-raster.md`, §39.1).
   - **A armadilha que isso abre:** o enum de tamanho é `small 0 … display 3` e os
     structs declaram `display, large, medium, small`, então é `valor[3 − classe]`.
     Com os defaults desta versão **os quatro valores são iguais em todas as cinco
     tabelas**: transcrever `valor[classe]` dá pixel idêntico e não avermelha teste
     nenhum — só acorda num documento que diferencie as classes.
4. ~~`[OBS]` O `range` do `RBDisplayListGlassDisplacement`: o valor `v` que o
   `IconRendering` escreve como `(v, −v)` não foi atribuído a nenhuma grandeza
   nomeada.~~ **FECHADO em 15/09/2026** (`Docs/Laudos/2026-09-15-refracao.md` §2).
   `[BIN]` `v` é **`FinalizedIcon.Layer.sdf.maxDistance`** (`layer+0xA8`), passado
   de `0x4A6F8`/`0x4A8E4` a `0x10C14` — e é **o mesmo escalar** do argumento 7 do
   especular (`0x49238`). O achado que vale mais que o valor: **o `range` é
   vocabulário do CAMPO, não do efeito.** Ele é compartilhado com
   `add_glass_highlight`, e por isso um `range` "da refração" nunca existiu como
   grandeza separada. Ver §40.
5. `[OBS]` `useSystemGlass` (`false` por padrão, §19.3) e `useOS26Compositing`:
   os dois existem e os dois trocam de caminho de composição. **Metade fechou em
   15/09/2026** (`Docs/Laudos/2026-09-15-translucencia.md` §5.2): `[BIN]`
   `useOS26Compositing` (`+0x362`, default `false`) acrescenta, **antes** de abrir
   a camada, um `addContentHeadroom:` e um filtro `ColorClamp` de estilo 9
   (`0x00043140`–`0x00043180`) — é o caminho **HDR/EDR**, e não uma segunda
   geração do vidro. `useSystemGlass` continua `[OBS]`, mas estreitado: `[BIN]`
   ele **não pode** alcançar o `glassBackground_v1` a partir destes binários, o
   que o desacopla do item 1 desta mesma lista.

> **Por que isto é um resultado e não uma falha.** A hipótese que abriu a
> investigação — "o `refractionHeight` do documento é desnormalizado por
> `min + (max−min)·pow(h,power)`" — estava **certa na forma e certa nos
> números**, e teria sido fácil parar aí e declarar a ponte fechada. O que
> impediu foi perguntar *onde os desnormalizados desembocam*: e desembocam no
> `displacementMap_v1`, com um argumento e um estilo de oito campos nomeados —
> não nos 75 slots. Uma transcrição que tivesse alimentado o `glassBackground_v1`
> com esses números produziria uma imagem plausível e **errada em toda parte**,
> porque o alvo estaria desenhando o vidro do sistema onde o Icon Composer
> desenha o dele.

---

## 30. O `fill` do documento — e são DOIS conversores com o mesmo nome

*2026-09-03. A ponta que faltava entre o documento e o motor: até aqui o `fill`
da raiz não era lido, e as camadas eram compostas sobre nada.*

A investigação começou de uma hipótese — *"`automatic` é o gradiente de chiclet
do sistema, com a aparência escolhendo o polo"* — e ela estava **metade certa**.
A metade errada é a que mudou o desenho.

### 30.1. As sete tags, lidas de uma instrução que as NOMEIA

Tudo pende da numeração dos casos, então ela não foi assumida a partir da ordem
em que o JSON os escreve. `[BIN]` `Fill.Kind.displayName`, em
`IconComposerFoundation 0x0A8D40`, é uma cadeia de `csel` sobre *small strings*
de Swift; decodificar os imediatos devolve os nomes que a UI mostra:

| tag | caso | `displayName` |
|---|---|---|
| 0 | `none` | "None" |
| 1 | `automatic` | "Automatic" |
| 2 | `solid` | "Solid" |
| 3 | `automaticGradient` | **"Standard Gradient"** |
| 4 | `linearGradient` | **"Custom Gradient"** |
| 5 | `systemLight` | "System Light" |
| 6 | `systemDark` | "System Dark" |

Os dois nomes em negrito não aparecem em lugar nenhum do formato em disco — são
rótulo de UI, e servem aqui como **confirmação independente** de que a tag 3 é o
gradiente do sistema e a 4 é o do autor, e não o contrário.

### 30.2. Os dois conversores, com limites exatos

`[BIN]` O `IconComposerKit.arm64` importa **exatamente seis** construtores de
`Icon.Fill` e mais nada, e todos os sítios de chamada vivem em **duas** funções,
cujos limites o `LC_FUNCTION_STARTS` dá sem ambiguidade:

| função | é o quê | como se sabe |
|---|---|---|
| `0x10AD9C`–`0x10B080` | o fill do **fundo / chiclet** | o único chamador desce para `Icon(name:chiclet:layers:…)` |
| `0x10B7EC`–`0x10C448` | o fill da **camada** | chama `Layer.fill`, `Layer.isGlass`, `Layer.frame`, e termina em `Icon.Element(contents:bounds:fill:…)` |

**As duas leem o mesmo tipo do documento e dão respostas diferentes.** É por isso
que "o que `automatic` significa" não tem uma resposta só.

### 30.3. No FUNDO — a hipótese sobrevive, com um braço que ninguém previu

`[BIN]` `0x10AEF4`:

```
and  w8, w26, #0xff        ; w26 = rendition.sourceAppearance
cmp  w8, #2
b.lo -> systemLightChicletGradient    ; base(0), light(1)
b.eq -> systemDarkChicletGradient     ; dark(2)
     -> IconColor.clear ; Fill.solid(clear)   ; tinted(3)
```

**Sob `tinted`, o fundo `automatic` é TRANSPARENTE.** Não é um cinza neutro e não
é um gradiente: é `IconColor.clear = (0,0,0,0)`. Um conjunto de testes que só
exercitasse `light` e `dark` — o reflexo natural, já que a rampa tem dois polos —
passaria verde sem nunca tocar este braço.

`[BIN]` E o `none` do fundo percorre **o mesmo bloco, byte a byte**. No fundo,
`none` não quer dizer "não desenhe".

### 30.4. Na CAMADA — não é cor nenhuma

`[BIN]` O fill da camada é resolvido **duas vezes**, em dois slots de
especialização. O segundo, em `0x10BD90`, monta o slot com a aparência **forçada
a `light`**: `mov w9,#0x100 ; bfxil w9,w0,#0,#8`.

E o `automatic` da camada, em `0x10C0FC`–`0x10C138`, é:

> **"o que o fill DESTA MESMA camada resolve na aparência `light`"** — uma cópia
> verbatim daquele buffer. E sob `light` ele próprio vira **`nil`**.

Não é `.system`, não é cor: é um **operador de herança de especialização**, e o
terminador que o impede de recursar para sempre é a própria aparência `light`.
`none` na camada é `Element.fill = nil` — sem override, a arte fica com as cores
que ela já tem.

> **Isto corrige o §24.3 desta documentação**, que atribuía "55 fills de camada +
> 28 de fundo" ao `Icon.Fill.Contents.system`. **Só os 28 do fundo viram
> `.system`.** Os 55 da camada nunca chegam lá.

### 30.5. E o corpus previu isso antes de eu contar

`[ART]` A leitura faz duas previsões falsificáveis sobre 145 documentos, e as
duas fecham em divisões limpas `0/N`, sempre do lado que a leitura exige:

| previsão | medido |
|---|---|
| `none` no fundo seria indistinguível de `automatic`, logo redundante | **0 ocorrências** (contra 49 na camada) |
| `system-light`/`system-dark` são vocabulário de chiclet, logo só de fundo | **0 na camada** (33 no fundo) |

`[ART]` E o fato que mais restringe: **toda camada `automatic` nomeia arte
(51/51), toda camada `none` nomeia arte (40/40), e as 159 camadas sem chave
`fill` também.** O `fill` não é o que faz a camada existir — a arte é. Um fill
que substituísse a arte tornaria `none` e "sem chave" a mesma afirmação, e o
corpus mantém as duas, 49 e 159 vezes.

### 30.6. As duas rampas, e o alpha que é REESCRITO

`[BIN]` O construtor em `IconRendering 0x3E680` escreve exatamente **duas
paradas**, com `r == g == b` e alpha `1.0`:

| | parada 0 @ `0.0` | parada 1 @ `1.0` |
|---|---|---|
| `systemLightGradient` | `1.0` (255) | `0.9607843137254902` (245) |
| `systemDarkGradient` | `0.12156862745098039` (31) | `0.058823529411764705` (15) |

`[BIN]` E o `resolve` **reescreve o alpha de cada parada** com a opacidade do
fill em vez de multiplicar: o helper em `0x3CFF4` lê `r,g,b` de
`+0x00/+0x08/+0x10` e a localização de `+0x20`, **pulando o alpha da origem em
`+0x18`**.

> **Por que essa distinção precisou de um teste próprio.** As duas rampas
> carregam alpha `1.0`, onde substituir e multiplicar dão o mesmo número.
> **Nenhum caso do corpus consegue separar as duas regras** — só um teste
> dedicado. A mutação que troca uma pela outra existe no gate exatamente para
> provar que esse teste não é decoração.

### 30.7. O eixo — o que afundou o `automatic-gradient` está lido

O spec do gradiente (`2026-09-01-gradiente.md` §4.4) deixou o `automatic-gradient`
**não implementado e nomeado**: os seis parâmetros estavam medidos e **o eixo
nunca tinha sido**. Essa razão caiu.

`[BIN]` `Icon.Fill.resolve`, em `IconRendering 0x3CE80`, escolhe entre as duas
rampas por `cmp x8, #1` e **zera a região de placement**, gravando
`Optional<GradientPlacement> = .none`. E `.none` tem significado definido —
`GradientPlacement.default`, em `IconRendering 0x38CF4`:

```
start = (0.0, 0.0)
end   = (0.0, 1.0)
```

`[BIN]` O caminho de desenho substitui o nil por esse default (`0x1BAB8`), em
coordenadas **unitárias de um retângulo**: `ponto = rect.origin + unit *
(largura, altura)`. E os **três sítios conversores passam `placement: nil`** para
o `automaticGradient`, então ele herda o eixo vertical padrão.

**O `automatic-gradient` está inteiramente especificado**, e o mesmo nil fechou o
último buraco do `linear-gradient`: uma rampa de camada que não nomeia
`orientation` era recusada, e agora desenha nesse eixo. `[ART]` São **26 dos 48**
`linear-gradient` de camada do corpus — o caso comum, não a borda.

### 30.8. O `orientation` é descartado em três dos quatro sítios

`[BIN]` Só o caminho de `linearGradient` **de camada** constrói um
`GradientPlacement` a partir do `orientation` do documento. O conversor do
chiclet lê `primaryColor` e `secondaryColor` e **nunca toca** no `orientation`.

`[ART]` Um `linear-gradient` de **fundo** com `orientation` desenha no eixo
vertical padrão de qualquer maneira, e o corpus tem 8 ocorrências na raiz mais 6
em especializações. Aqui **o nosso renderizador seria mais correto que o alvo**,
e ser mais correto é a divergência: o `IconRenderer` descarta o `orientation` do
fundo e **nomeia o descarte** em `RenderedIcon::notes`, para que a diferença
apareça em vez de ser silenciosamente boa.

### 30.9. O que segue sem leitura

| | por quê |
|---|---|
| **o retângulo do alinhamento ao chiclet** | `[CONTESTADO em 15/09/2026]` A frase anterior dizia que `supportsChicletAlignmentForSystemFills` é `true` por default **e que quando ligado o rect é origem `(0,0)` com um `CGSize` do contexto**. A segunda metade **não foi reproduzida**: o laudo do chiclet mediu que o campo (Swift #32, `+0x360`) **não dirige ramo nenhum** nesta fatia — toda carga dele é cópia, `==` ou *value witness*, com `str` na instrução seguinte. Enquanto ninguém der o endereço de onde a afirmação saiu, ela não vale. O `[OBS]` do tamanho perde a urgência: em iOS/macOS canvas, chiclet e full-bleed são o mesmo `(0,0,1024,1024)`, porque `1088 = 1024 + 2 × chicletOutset(32)`. A implementação segue desenhando sobre o `boundingRect` da própria forma |
| a lateralidade de y do display list | `[OBS]` na rampa clara (255→245) é quase invisível; **na escura (31→15) não é** |
| o `Bool` do `.system(_, Double, Bool)` | ~~`[OBS]` os três construtores gravam `1`~~ **FECHADO em 15/09/2026** (`Docs/Laudos/2026-09-15-chiclet.md` §5.3): `[BIN]` é o **`alignsToChiclet`**, typeref `0x9D482`. Que os três construtores gravem `1` deixa de ser um mistério e vira a leitura certa — o alinhamento ao chiclet é ligado por construção, e nenhum escritor de `0` existe porque nenhum caminho o desliga. **Meia-integração é pior que nenhuma:** esta tabela carregava o `[CONTESTADO]` do mesmo laudo duas linhas acima e este `[OBS]`, que o mesmo laudo fechou, por um dia inteiro |
| o espaço de cor das rampas | `[OBS]` `IconColor` são quatro `Double` sem tag de espaço |
| `ResolvedFill`, `promoteNoneFillsToEachAppearance` | `[OBS]` localizados, não lidos. São do **editor**, não do caminho de render |

---

## 31. O fluxo de pontos do traço — a regra da CPU, e o `miterlimit` não é da GPU

*2026-09-03. O §10 parou por causa dos bits do `RenderState`; o §33 os
nomeou. Sobrava o outro lado: **quem alimenta o buffer**. Transcrever a
geometria da GPU e enchê-la com um buffer chutado seria pior do que não
transcrever, porque produziria imagem plausível.*

### 31.1. O achado que decide onde o `miterlimit` mora

`[BIN]` `RB::(anônimo)::apply_miter_limit(float2 d_in, float2 d_out, float m)`,
em **`0x11DE6C`–`0x11DF00`**, desmontada inteira com cobertura 100%:

```
s0 = dot(normalize(d_in), normalize(d_out))    ; o cosseno da virada
s1 = (1 + s0) * s2                             ; s2 = m
fcmp  s1, #2.0
csel  w8, #2, wzr, mi                          ; s1 < 2  ->  2 (bevel), senao 0 (miter)
ldr   s1, [x9, #0xe40]                         ; 0x161E40 = 0.99f
fcmp  s0, s1
csinc w0, w8, wzr, le                          ; s0 > 0.99  ->  1 (round)
```

`[BIN]` E `m` é **`miterlimit²`**: `flatten_points` guarda o quadrado em
`Flattener+0x24` (`fmul s9, s3, s3` em `0x11CE9C`). Então
`(1 + cos)·ml² < 2` ⇔ `ml² < 2/(1+cos)`, que é **a regra do SVG e do
CoreGraphics, exata**.

> **Consequência para qualquer transcrição.** O `miterlimit` **não chega à
> GPU**. O campo `join` de cada ponto já vem degenerado: a CPU roda esta regra
> por ponto e escreve `0`, `1` ou `2`. Um renderizador que guardasse o
> `miterlimit` para o shader estaria implementando um alvo diferente.

`[BIN]` A decisão por ponto, de `Stroke::Flattener<Point>::lineto` (`0x11DD70`)
e `flush_lineto` (`0x11DE2C`):

| situação | `join` escrito |
|---|---|
| `raio ≤ Flattener[0x2c]` | **1** (round), sempre. `[0x2c] = bezier_flatness()/escala`, e `RB::bezier_flatness()` (`0x110CA0`) devolve **0,25** — traço mais fino que meio pixel não ganha junção |
| `LineJoin` global = `bevel` | **2**, sem teste de limite |
| `LineJoin` global = `round` | **1**, sem teste — e `1` **não emite primitiva de junção nenhuma** |
| `LineJoin` global = `miter` | `apply_miter_limit` por ponto |

### 31.2. Os sentinelas negativos, e são três coisas diferentes

`[BIN]`

| valor | onde | o que é |
|---|---|---|
| **−3** | `lineto` `0xA9D58`, `finish_subpath` `0xAA29C` | o **ponto-fantasma espelhado** fora de uma ponta ABERTA, nas duas pontas: `2·P₀ − P₁` e `2·P_{k−1} − P_{k−2}`, com raio e alpha copiados do vizinho real |
| **−1** | `finish_subpath` `0xAA2C0`/`0xAA2D4` | a **duplicata de wrap-around** de um subpath FECHADO |
| **−1** | `lineto` `0xA9EF0` | **separador de corte por clip**, posição `(0,0)`, raio `0` |
| **−2** | `lineto` `0xA9E7C`, `finish_subpath` `0xAA228`/`0xAA260` | **ponta `butt` OBLÍQUA**, em par ordenado `[−2][2]` no início e `[2][−2]` no fim |

`[BIN]` **Não existe sentinela de separação entre subpaths**, e não é preciso:
dois `−3` adjacentes fazem o teste `min(join[iid+1], join[iid+2]) < 0` do §10.2
descartar sozinho toda janela que atravessaria de um subpath para o outro.

### 31.3. O layout do buffer, e a folga é de um ponto em cada ponta

`[BIN]` Subpath **aberto** de `k` pontos reais → `N_sub = k + 2`:

```
idx 0        2·P₀ − P₁                  join = −3
idx 1        P₀                         join =  1
idx 2..k−1   P₁ … P_{k−2}               join = por ponto (§31.1)
idx k        P_{k−1}                    join =  1
idx k+1      2·P_{k−1} − P_{k−2}        join = −3
```

`[BIN]` Subpath **fechado** → `N_sub = k + 3`: o slot fantasma da frente é
**sobrescrito** com o penúltimo ponto real (`join = −1`), o ponto inicial recebe
o join da costura, e uma cópia do segundo ponto real é **anexada** com
`join = −1`.

`[BIN]` `add_point` (`0xAA54C`) **descarta em silêncio** um ponto cuja posição
seja idêntica à do anterior — o buffer **não é uma transcrição 1:1** da
polilinha. E `new_buffer` (`0xAA6A8`) usa buffers de `0x800` bytes = 128 pontos,
copiando os **dois últimos** do buffer velho ao trocar.

`[BIN]` As contagens de instância, de `draw_buffer` (`0xAA3E0`):

| passada | `FunctionType` | instâncias | janela |
|---|---|---|---|
| linhas | **14** | `N − 3` | `P[iid..iid+3]`, segmento `P[iid+1]→P[iid+2]` |
| junções | **15** | `N − 2` | `P[iid..iid+2]`, junção em `P[iid+1]` |

`[BIN]` A passada de junções só é emitida quando algum `join ∈ {−2, 0, 2}` —
`add_point` (`0xAA62C`) liga a flag com `(1 << (join+2)) & 0x15`, que coincide
bit a bit com o `switch` do `stroke_joins_vertex`.

### 31.4. `StrokeInfo`, e a largura NÃO está nele

`[BIN]` De `Coverage::Stroke<RBStrokeRef>::get_info` (`0x47C9C`) cruzado com
`draw_stroke` (`0xA90F0`):

| off | conteúdo |
|---|---|
| `+0x00` | kind: `0` linhas, `1` partículas |
| `+0x04` | `RB::LineCap` → palavra 0, bits 6–8 |
| `+0x05` | `RB::LineJoin` → palavra 0, bits 9–10 |
| `+0x08` | payload de 14 bits → **palavra 1**, bits 16–29. `[OBS]` semântica não lida |
| `+0x0C` | `== 1` → palavra 0, bit 11 |
| `+0x10` | **miterlimit** |

`[BIN]` **Negativo nomeado: a largura não está no `StrokeInfo`.** Ela vive em
`StrokeablePath+0x18` e chega à GPU **só** como o campo `radius` por ponto, com
`flatten_points` (`0x11D020`) guardando `{ width * 0.5f, 1.0f }` como o
`Stroke::Point` constante. **`radius = largura/2`, `alpha = 1.0`** num traço
uniforme.

### 31.5. O que continua sem leitura

| | por quê |
|---|---|
| o gatilho prático do par `−2`/`2` | `[BIN]` o mecanismo está lido inteiro. `[OBS]` O que não se mediu é qual produtor faz a tangente registrada divergir da corda. No caminho `Flattener` puro ela **nunca** diverge, então sem dash o par pode ser omitido e o buffer continua correto |
| `StrokeInfo+0x08` | `[OBS]` os 14 bits que vão para a palavra 1 |
| como uma junção `round` é pintada | `[OBS]` `join == 1` não emite primitiva de junção. Não afeta o corpus (`miter` nos 35), mas é buraco para cobrir os três joins |
| o braço de partículas | `[OBS]` `kind == 1`, e um terceiro braço em `0xA9670` |
| **a junção de costura desenhada duas vezes** | `[OBS]` num subpath fechado, `idx 1` e `idx k+1` são o mesmo ponto com o mesmo join e os mesmos vizinhos, e **os dois caem no intervalo da passada de junções**. Inofensivo sob cobertura por união; não sob mescla aditiva. Lido, e **não se sabe se é intencional** |

---

## 32. A mescla, do formato ao caso do shader — e são DOIS saltos

*2026-09-04. O §26 leu os 56 blocos e casou 39 por fórmula, deixando dois nomes
com mais de um candidato. Esta seção fecha isso pelo outro lado: quem ESCREVE o
campo.*

### 32.1. A ponte fala CoreGraphics, não RenderBox

`[BIN]` Uma tabela direta `formato → caso do RenderBox` **não existe** em nenhuma
das sete slices. Busca literal exaustiva pela sequência composta
`[2,27,25,30,44,28,12,29,43,26,31,32,33,14,36,37,38,39]`, larguras 1/2/4/8 LE,
sobre os arquivos inteiros: zero ocorrências. Método puramente literal, sem
decodificador no caminho — o negativo é real e não uma varredura truncada.

`[BIN]` `IconRendering 0x94AC0` (cópia idêntica em `0x978F4`) guarda **18 ×
uint32**, lida por `ldr w2, [x8, x23, lsl #2]` em `0x25578` e entregue direto ao
`setBlendMode:`:

```
0, 4, 1, 7, 26, 5, 2, 6, 27, 3, 8, 9, 10, 11, 12, 13, 14, 15
```

**São as constantes públicas do `CGBlendMode`** — `normal` 0, `multiply` 1,
`screen` 2, `overlay` 3, `darken` 4, `lighten` 5, `colorDodge` 6, `colorBurn` 7,
`softLight` 8, `hardLight` 9, `difference` 10, `exclusion` 11, `hue` 12,
`saturation` 13, `color` 14, `luminosity` 15, `plusDarker` 26, `plusLighter` 27.

> **Dezoito posições concordando com um cabeçalho publicado pela Apple** é uma
> checagem de fora deste binário, contra uma numeração que ninguém aqui
> escolheu. É evidência de tipo diferente da de casar fórmula com bloco de
> shader, e é por isso que os dois nomes ambíguos deixaram de ser ambíguos.

### 32.2. O segundo salto, e ele também se confere sozinho

`[BIN]` `RenderBox 0x15ED18` (`cg_table`, 28 × uint32), indexada por
`rb_blend_mode` em `0x8B4D4`. E `RB::RenderPass::set_blend_state` em `0x11A3D4`
faz `bfi w9, w8, #0x10, #0xe ; str w9, [x19,#4]` — **catorze bits a partir do bit
16 da palavra 1**, que é literalmente o `(palavra1 >> 16) & 16383` do §15.

`[BIN]` `RB::blend_name` em `0x110E2C`, sobre a tabela de 56 ponteiros em
`0x18E718`, **dá nome aos 56 casos**:

```
 0 copy            14 exclusion       28 lighten        42 pin_light
 1 clear           15 maximum         29 color_dodge    43 plus_lighter
 2 source_over     16 minimum         30 color_burn     44 plus_darker
 3 source_in       17 subtract_s      31 soft_light     45 darken_source
 4 source_out      18 subtract_d      32 hard_light     46 lighten_source
 5 source_atop     19 clip_copy       33 difference     47 minimum_inverse
 6 dest_over       20 clip_intersect  34 subtract       48 plus_lighter_ignore_alpha
 7 dest_in         21 clip_copy_inv   35 divide         49 plus_darker_ignore_alpha
 8 dest_out        22 clip_int_inv    36 hue            50 subtract_s_ignore_alpha
 9 dest_atop       23 accum_copy      37 saturation     51 sdf_maximum
10 exclusive_or    24 pass_through    38 color          52 sdf_minimum
11 additive        25 multiply        39 luminosity     53 sdf_minimum_inverse
12 screen          26 overlay         40 linear_burn    54 custom_normal
13 linear_dodge    27 darken          41 linear_light   55 custom_complex
```

`[ART]` **As 28 entradas da `cg_table` concordam com a ordem do CoreGraphics,
nome por nome** — CG 16 `clear` cai no caso 1 `clear`, CG 17 `copy` no caso 0
`copy`, CG 25 `xor` no caso 10 `exclusive_or`. Duas tabelas achadas
independentemente, validadas por uma terceira fonte que não é binária.

**Isto aposenta os 17 blocos que o §26.5 registrava sem casamento**: eles têm
rótulo agora, mesmo onde a fórmula não foi transcrita. E confirma a inferência do
§26.4 — os casos 45 e 46, que usam Rec.709, são `darken_source` e
`lighten_source`, e de fato não são mescla de cor.

### 32.3. A tabela de 18, fechada

| formato | nome | CG | **caso** | nome do caso |
|---|---|---|---|---|
| 0 | normal | 0 | **2** | `source_over` |
| 1 | darken | 4 | **27** | `darken` |
| 2 | multiply | 1 | **25** | `multiply` |
| 3 | colorBurn | 7 | **30** | `color_burn` |
| 4 | **plusDarker** | 26 | **44** | `plus_darker` |
| 5 | lighten | 5 | **28** | `lighten` |
| 6 | screen | 2 | **12** | `screen` |
| 7 | colorDodge | 6 | **29** | `color_dodge` |
| 8 | **plusLighter** | 27 | **43** | `plus_lighter` |
| 9 | overlay | 3 | **26** | `overlay` |
| 10 | softLight | 8 | **31** | `soft_light` |
| 11 | hardLight | 9 | **32** | `hard_light` |
| 12 | difference | 10 | **33** | `difference` |
| 13 | exclusion | 11 | **14** | `exclusion` |
| 14 | hue | 12 | **36** | `hue` |
| 15 | saturation | 13 | **37** | `saturation` |
| 16 | color | 14 | **38** | `color` |
| 17 | luminosity | 15 | **39** | `luminosity` |

`[BIN]` `plusLighter` = **43** e `plusDarker` = **44**, com três testemunhos: a
`cg_table`, o `blend_name`, e o próprio shader — o bloco compartilhado de 43/44
testa `icmp eq i32 %9, 44` e o ramo verdadeiro subtrai o excesso de alpha.

`[BIN]` `darken` = **27** e `lighten` = **28**: 15 e 16 são `maximum` e `minimum`
e **não têm equivalente CoreGraphics** — `cg_blend_mode`, o inverso em
`0x161AE8`, mapeia os dois para `CG 0`.

### 32.4. Qual argumento é a origem, e por que isso quase passou errado

`[BIN]` A assinatura é `blend(ShaderState, half4, half4)` com o estado passado
como **quatro palavras separadas**, então os dois operandos de cor são `%4` e
`%5`, e a declaração os nomeia `src` e depois `dst`.

**Essa ordem é fácil de inverter e difícil de pegar**, porque `screen`,
`multiply`, `darken` e `lighten` são **todos simétricos nos dois operandos** —
leem idêntico sob qualquer atribuição. O par assimétrico decide: `[BIN]` o caso
26 ramifica em `dst.rgb > 0.5*dst.a` e o 32 em `src.rgb > 0.5*src.a`, e o
`blend_name` chama 26 de `overlay` e 32 de `hard_light`. Overlay ramificando no
fundo e hard-light na origem é a definição do W3C; sob a atribuição invertida os
nomes trocariam.

### 32.5. A cauda de composição, e o modo que a pula

`[BIN]` Todo modo separável termina nas mesmas três linhas (em `pdf_mode` e
inline nos casos 25–32):

```
out.rgb = src*(1 - dst.a) + dst*(1 - src.a) + B
out.a   = src.a + dst.a - src.a*dst.a
```

com `B` já escalado por `src.a * dst.a`. `[BIN]` E `extended_color` faz o
`pdf_mode` **limitar** o rgb a `[0, out.a]` — o limite roda quando o bit está
**ligado**, que é o inverso do intuitivo.

`[BIN]` **O `screen` não usa essa cauda.** O caso 12 está na banda barata 11–18
do §26.1 e calcula `src + dst*(1 - src)` nos quatro canais, sem cauda nenhuma.

`[BIN]` E o `soft_light` **não é a fórmula do W3C**: o caso 31 é o ramo baixo
aplicado incondicionalmente, `B = 2·d·s − (2s − as)·d²/max(ab, 0.005)`. Não há
`sqrt` nem cúbica em lugar nenhum do módulo, que é o que a definição completa
exigiria. `[BIN]` O piso `0.005` é a constante `0xH1D1F`, o `half` mais próximo
desse decimal.

---

## 33. Os bits que o traço lê — `LineCap`, `LineJoin`, e a inferência que caiu

*2026-09-04. O §10.3 parou em dois campos do `RenderState` sem semântica. Eles
têm nome.*

### 33.1. O nome está no inicializador estático

`[BIN]` `default_mod69.ll` (`stroke_joins_vertex`), no `air.static_init`:

```llvm
%2 = extractelement <4 x i32> %1, i64 0
%3 = and i32 %2, 1536 ; %4 = icmp ne i32 %3, 512
%5 = and i32 %2, 448  ; %6 = icmp eq i32 %5, 128
%7 = or i1 %4, %6
!24 = !{..., !"RB::Shader::Constant::per_vertex_joins"}
```

**`per_vertex_joins = (w0 & 1536) != 512 || (w0 & 448) == 128`**, e a palavra é a
**0** sem ambiguidade: o `extractelement ... i64 0` está na mesma função, e os
seis módulos do traço carregam só o global `.0`.

### 33.2. Bits 6–8 são `RB::LineCap`, sete casos

`[BIN]` `RenderBox 0x18F920`, via `XML::Value::LineCap::to_string`: `0 round`,
`1 square`, **`2 butt`**, `3 outwards-triangle`, `4 inwards-triangle`,
`5 forwards-triangle`, `6 backwards-triangle`.

`[BIN]` `cg_line_cap` em `0x15EEF8` = `[1, 2, 0, 1, 1, 1, 1]`: os três primeiros
vão para `CGLineCap {1, 2, 0}`, exatamente `butt=0, round=1, square=2` do
CoreGraphics, e os quatro triângulos — que o CG não tem — caem em `round`.

`[BIN]` O `stroke_lines_fragment` lê o campo **inteiro**, com um `switch` de 7
casos, e cada caso é uma função de distância na região além da ponta:

| caso | distância |
|---|---|
| `round` | `sqrt(ov² + d²)` |
| `square` | `max(ov, d)` |
| **`butt`** | `max(r + ov − s, d)` |
| `outwards-triangle` | `d + ov` |
| `inwards-triangle` | `max(r + ov − d, d)` |
| `forwards-triangle` | `max((u>0 ? d : r−d) + ov, d)` |
| `backwards-triangle` | `max((u>0 ? r−d : d) + ov, d)` |

**O braço `== 128` é `cap == butt`**, porque `2 << 6 = 128`.

`[BIN]` E o `butt` tem **duas metades**: o estágio de vértice já **encurtou o
segmento em um `recip_scale`** naquela ponta (com guarda `len > s`), e o
`r + ov − s` do fragment devolve o pixel. Transcrever só uma delas termina o
traço um pixel e meio comprido.

### 33.3. Bits 9–10 são `RB::LineJoin`, três casos

`[BIN]` `0x18F958`: `0 miter`, `1 round`, `2 bevel` — e `rb_line_join`
(`0x8B6B0`) é a identidade, mesma numeração do `CGLineJoin`. O braço `== 512` é
`join == round`.

### 33.4. A CPU monta os mesmos bits

`[BIN]` `RB::(anônimo)::draw_stroke`, `0xA9324`–`0xA9380`:

```
ldrb w8, [x25,#4]      ; StrokeInfo.cap
and  w8, w8, #7        ; 3 bits
ldrb w9, [x25,#5]      ; StrokeInfo.join
bfi  w8, w9, #3, #2    ; 2 bits logo acima
lsl  w27, w8, #6       ; o payload entra no bit 6
orr  w8, w27, #0xe     ; FunctionType = 14
```

### 33.5. E a inferência do §11.1 está REFUTADA

O §11.1 inferia que 6–8 fosse *o mesmo campo* que o estágio de path usa para
escolher entre polilinha e cúbicas, e que decodá-lo pagaria duas vezes.

`[BIN]` O `shader_path` testa o **mesmo bit 6 da mesma palavra 0**, mas
`(estado & 64) != 0` faz ele ler o buffer como `<2 x float>` (polilinha) e `== 0`
como `Path::CubicSegment`. Se fosse o mesmo campo, "polilinha" seria
`cap ∈ {square, outwards-triangle, forwards-triangle}`. É absurdo.

`[BIN]` O que explica a coincidência: **os bits 6–15 da palavra 0 são um payload
de 10 bits POR FAMÍLIA de shader**, não um mapa global. Os onze sítios
`bfi ..., #6, #0xa` do `RenderBox` inserem ali um `PrimitiveCoverageState` ou um
`AccumulatorCoverageState` — tipos nomeados e distintos —, o `draw_stroke`
escreve o seu próprio layout, e o `custom_effect` lê um terceiro. **Os bits 0–5
são o `RB::FunctionType`, e é ele que diz qual leitura vale.**

> **O §10 não paga o §7.** São duas medições separadas, e a do §7 continua de pé
> por conta própria. A inferência estava marcada `[INF]` e caiu quando medida,
> que é para isso que o selo existe.

`[BIN]` **E uma correção de instrumento**: o §11.1 credita essas máscaras a
`shader_blend` e ao `mod100`. **Não há `shader_blend` entre os consumidores** —
são os quatro módulos do traço mais `uber_vertex` e `uber_fragment`, e os dois
uber só as têm porque **inlinam** o traço.

## 34. O vidro dentro de um grupo que mescla — a pergunta era um falso dilema

O §8.1 do spec `2026-09-03-mescla-e-traco.md` deixou em aberto a única coisa que
separa as 169 camadas de hoje das 178 prometidas, e a deixou como um dilema:

> `[OBS]` Um grupo que mescla e carrega vidro não é desenhado. O vidro refrata o
> seu fundo: num alvo próprio esse fundo está vazio, e na tela a mescla
> misturaria o fundo duas vezes. Qual das duas o alvo faz não foi lido.

`[BIN]` **Nenhuma das duas.** O vidro não amostra o destino: ele é um filtro
sobre **o item que ele veste**, e a pergunta "de onde vem o fundo" não tem esse
fundo para vir.

### 34.1. O RenderBox tem DUAS formas de item para cada filtro

`[BIN]` Cada filtro do `RenderBox` aparece no display list de duas maneiras
distintas, e os símbolos as nomeiam:

| forma | símbolo | o que consome |
|---|---|---|
| própria | `DisplayList::GenericFilter<F>::render(...)` | o conteúdo do próprio item |
| do fundo | `DisplayList::BackdropFilterItem<F>` | o que já está atrás |

`[BIN]` **Onze** filtros declaram `GenericFilter<F>::make_backdrop_item(Builder&)`
— e só **nove** têm um `BackdropFilterItem<F>` instanciado no binário. Os dois
que declaram e não têm são `Filter::GaussianBlur` e `Filter::Distance`.

`[BIN]` Os dois efeitos de vidro — `GlassDisplacementEffect` e
`GlassHighlightEffect` — estão entre os nove. **A rota do fundo existe para o
vidro.** O que este parágrafo mede é que ela existe, não que ela seja tomada.

### 34.2. O caminho que o `IconRendering` realmente monta

`[BIN]` O `_RBSystemShaderDisplacementMap` é o único *system shader* que o
`IconRendering` importa (§29.1), e ele chega ao display list por **dois** sítios,
os dois com a mesma forma:

```
beginLayer                                            ← abre uma camada, sem flags
addStyle:data:
[[RBShader alloc] initWithSystemShader: …]            ← o mapa de deslocamento
setArgumentBytes:atIndex:type:count:flags:
setVariant:
addFilterLayerWithShader:border:layerBorder:bounds:flags:
```

| sítio | função | flags |
|---|---|---|
| `0x00010DC4` | `0x00010C14` | `w4 = #0` |
| `0x0008066C` | `0x0008036C` | `w4 = #0` |

`[BIN]` E `-[RBDisplayList addFilterLayerWithShader:border:layerBorder:bounds:flags:]`
(`0x00040A18`) **fecha a camada antes de filtrar**:

```
Builder::end_layer(const State&)          0x000C9B4C
Builder::restore(bool)                    0x000C9670
add_shader_filter_layer(…, Layer*, …)     0x000408D4   ← tail call
```

`[BIN]` `add_shader_filter_layer` termina em
`DisplayList::State::add_custom_effect(Builder&, CustomShader::Closure&,
Layer*, float2, float2, const Rect&, OptionSet<CustomEffect::Flag>)`
(`0x000F4CC0`), com os flags traduzidos por `shader_effect_flags(unsigned)`
(`0x000407B0`) — e `shader_effect_flags(0)` é **0**.

> **A leitura, em uma frase.** O vidro é um `CustomEffect` sobre uma `Layer*`, e
> essa `Layer*` **não é a fonte da refração** — o §34.3 mostra o que é.

### 34.3. A camada vai VAZIA, e o que refrata é outra coisa

`[BIN]` Entre o `beginLayer` e o `addFilterLayerWithShader:` **não há uma única
chamada de desenho**, nos dois sítios. Tudo que acontece ali é `addStyle:data:`,
a construção do `RBShader` e os seus argumentos. Nada é desenhado dentro da
camada que o filtro recebe.

> **Uma correção.** A primeira redação desta seção dizia que o chamador "abriu,
> preencheu e fechou" a camada, e que a refração vinha dela. **Está errado** — a
> camada vai vazia, e a palavra "preencheu" era minha, não do binário. O que
> segue foi lido depois, no caminho de desenho, e é o que decide a pergunta.

`[BIN]` `State::add_custom_effect` não cria um item: ele monta um
`DisplayList::CustomEffectStyle{Closure&, Layer*, …}` e chama
`State::add_style(Builder&, Style*)` (`0x000ABC70`). O vidro é um **estilo**, e o
binário nomeia a subclasse: `DisplayList::GlassDisplacementStyle`
(vtable em `0x0018E000`).

`[BIN]` E `GlassDisplacementStyle::draw(Builder&, Layer&, Item*,
OptionSet<DrawFlag>)` (`0x000F3B38`) diz sobre o quê o efeito roda:

```
GlassDisplacementEffect::can_render_inline()          0x0008ABF8
alpha_effect_applies_as_filter(const Item*)           0x000F2E3C
  Heap::emplace<GenericFilter<GlassDisplacementEffect>>(…)   0x000F8A04
  Builder::apply_filter_(Item*, LayerFilter*, …)             0x000CD1F0
senao:
  GlassDisplacementEffect::can_discard_color(bool*)    0x0008ABD4
  Builder::ensure_layer(Item*, OptionSet<EnsureLayerFlag>, float)  0x000CCD5C
```

`[BIN]` Os dois ramos são a forma de **conteúdo próprio**: `GenericFilter<F>`
aplicado ao `Item*` que está sendo desenhado, ou `ensure_layer` forçando esse
mesmo item a ter camada para então filtrá-la. **Nenhum dos dois chama
`make_backdrop_item`**, e o §34.1 mediu que a rota do fundo existe — ela
simplesmente não é a que este caminho toma.

> ~~**A resposta ao §8.1.** O vidro refrata **o que ele veste**, não o que está
> atrás. O dilema do spec pressupunha uma captura do destino que não acontece.~~

> ### A frase acima é FALSA, e foi corrigida em 15/09/2026
>
> Não o parágrafo `[BIN]` que a precede — esse continua certo, instrução a
> instrução. **A frase que generaliza a partir dele é que não se sustenta**
> (`Docs/Laudos/2026-09-15-refracao.md` §3.1).
>
> `[BIN]` `GlassDisplacementStyle` é o **gerador do mapa**. O trabalho dele é
> transformar o campo da forma em vetores de deslocamento, então ele filtra o
> item que veste — **é o que ele tem de fazer**, e ler isso como "o vidro não
> olha para trás" é ler o particular como geral. **O deslocamento em si é outro
> objeto**, instalado logo depois, e a camada dele carrega o **bit de fundo**:
>
> ```
> 0x10C14   fecha a camada do mapa com addFilterLayerWithShader:
>           (RenderBox 0x40A18 = end_layer + restore + State::add_custom_effect)
> 0x4A794   beginLayerWithFlags: 1        <- bit 0, o BIT DE FUNDO
> 0x4AA00   idem, o segundo sitio
> 0x3BCA0   traduz o argumento publico e passa o bit 0 INTACTO (mascara 0x7B)
>           -> Builder::begin_layer -> Layer+0x44
> 0xF4030   CustomEffectStyle::draw
> 0xCDAA0   Builder::draw  -- tail-call em 0xCDAF4 para null_style_draw
> 0xCE02C   o teste do bit, que pendura um BackdropFilterItem na camada PAI
> ```
>
> **Então o vidro da Apple lê o destino? Sim.** E o que isso muda neste
> repositório é menos do que parece, o que é a parte instrutiva: **a premissa
> corrigida vira a premissa ORIGINAL**. O dilema do §8.1 volta a ser um dilema
> de verdade, e a razão pela qual o nosso `blendTheGroup` continua valendo passou
> a ser outra — medida, e não lida (§40).
>
> A mesma premissa velha viveu em três lugares. O comentário de
> `Source/RenderBox/IconRenderer.cpp` foi corrigido pelo próprio laudo; o
> docstring de `scripts/slice-reach.py`, que é **a régua que imprime o número de
> alcance do `Docs/README.md`**, só foi corrigido em 16/09/2026, nesta rodada de
> integração. Um número e a razão dele moravam no mesmo arquivo, e a razão
> envelheceu primeiro.

~~`[INF]` Para o nosso renderizador: dar ao grupo um alvo próprio **não deixa o
vidro sem fonte**, porque a fonte do vidro é o item dentro do grupo, e o item vai
junto.~~ A conclusão — que `blendTheGroup` pode valer também quando o grupo tem
vidro, destravando as 9 camadas e os 7 documentos — **sobreviveu à correção, por
outro fundamento**: não porque o vidro do alvo ignore o fundo, mas porque o nosso
`glassOver` desloca o buffer de acumulação no lugar, o acoplamento só morde
quando a refração **move** alguma coisa, e `[ART]` nenhum documento bloqueado
tinha vidro que refrata — são **2 de 271 grupos** no corpus inteiro, os dois com
força **negativa**. É diferença no nosso modelo, não pergunta não lida.

### 34.4. O que continua NÃO lido

`[OBS]` **O significado dos bits de `flags:` públicos.** O
`RBDrawingStateBeginLayer` (`0x0003B9FC`) traduz o argumento público para
`OptionSet<DisplayList::Layer::Flag>` por
`w8 = w19 & (0x7B − (w19 & 4))`, mais `| 0x80` quando `w19 & 0xA0`. Dos 21
sítios de `beginLayerWithFlags:` no `IconRendering`, catorze passam `0`, seis
passam `1` e um passa `0x80`.

**Dos três valores que ocorrem, dois ganharam nome em 15/09/2026** — os que
ocorrem, e não os 21 bits do espaço. A tradução acima continua sem leitura; o
que fechou é o significado do `1` e do `0x80`, logo abaixo.

~~`[OBS]` **E uma pista que NÃO fecha, registrada como pista.** O único sítio de
`0x80` (`0x0004A96C`) vem logo depois de um `addBlurFilterWithRadius:opaque:`
(`0x0004A960`), o que sugeriria "camada que lê o fundo". Mas o §34.1 mediu que
**não existe `BackdropFilterItem<GaussianBlur>` neste binário** … As duas
medições não se conciliam.~~

> **FECHADO em 15/09/2026, e a irreconciliação era um nome não lido**
> (`Docs/Laudos/2026-09-15-desfoque.md` §6). Os dois flags têm nome, e o nome sai
> do **serializador XML do próprio RenderBox**, que é onde este documento devia
> ter olhado primeiro:
>
> | flag | sítio | o que `RB::XML::DisplayList::begin_layer` (`0xE9E78`) emite |
> |---|---|---|
> | `1` | `0x4A5B4` (e `0x4A794`, `0x4AA00`) | **`needs-background`** (`0xE9F48`) — camada que precisa do fundo |
> | `0x80` | `0x0004A96C` | **`ignored-by-needs-background`** (`0xE9F08`) |
>
> `0x80` não é "lê o fundo": é **"ignorado por quem lê o fundo"**, que é o
> **oposto**. A pista apontava para o lado contrário do que se supôs, e foi por
> isso que ela não conciliava com o §34.1 — que continua certo: `[BIN]`
> `GenericFilter<GaussianBlur>::make_backdrop_item` (`0x1CBF4`) é literalmente
> `mov x0, #0 ; ret`, e **não pode** ser o mecanismo de fundo deste desfoque. O
> mecanismo é o outro, o da flag 1, e `blur-material.md` §2 completa: **o ramo que
> roda é sempre o de flag 1**, porque a escolha é `refractionStrength` e `[ART]`
> nenhum dos 145 documentos, nem o gabarito, nem o ícone do usuário tem chave de
> refração.
>
> **A lição, e ela vale para além deste bit:** um flag numérico sem nome convida
> a inferir pelo contexto — "vem depois de um blur, logo lê o fundo". O binário
> tinha o nome escrito, num serializador de depuração que ninguém pensou em ler
> porque não é código de desenho. Ver §36.3 e o §45, o bloco de método.

### 34.5. Os instrumentos

Quatro, em `scripts/macho.py`, cada um com um comando:

| comando | o que responde |
|---|---|
| `syms` | a `LC_SYMTAB` de uma fatia fina, filtrada por substring |
| `fn` | a função que contém um endereço, por `LC_FUNCTION_STARTS` |
| `xref` | quem faz `BL`/`B` para um endereço |
| `dis` | uma janela desmontada, com os alvos de chamada nomeados |

O `IconRendering` **não tem nome de símbolo Swift** na `LC_SYMTAB` — 1.793
entradas, nenhuma `$s` com endereço —, e é por isso que o `fn` existe: sem ele
não há como dizer onde uma função começa neste binário.

---

## 35. As 37 frentes de 15–16/09/2026 — o índice, e o que cada classe quer dizer

As seções §36 a §44 integram os **37 laudos** de `Docs/Laudos/` de 15 e 16 de
setembro. Até 16/09/2026, **seis** deles estavam linkados neste documento e
**nove** apareciam nomeados na prosa do `Docs/README.md`; os outros vinte e sete
não existiam em lugar nenhum fora do próprio laudo e da mensagem de commit. Isto
aqui é a dívida sendo paga.

O índice é por **o que a frente produziu** — não por assunto —, porque o volume
esconde a diferença que importa.

| classe | n | o que quer dizer |
|---|---|---|
| **FECHOU** | 14 | leitura nova que chegou ao pixel |
| **FECHOU E DECLAROU** | 6 | leitura fechada, transcrita, fixada por teste e **deliberadamente desligada no desenho**; o produto da frente é uma **nota honesta no render**, não um pixel |
| **ELIMINOU** | 6 | um suspeito morto com medida |
| **INFRA** | 6 | instrumento, UI, orçamento |
| *híbridos* | 4 | contados em duas classes |

### 35.1. `FECHOU E DECLAROU` é um resultado, e precisou de nome próprio

Seis laudos de um mesmo dia terminam com o pixel intacto: `chiclet` (a curva não
fechou), `especular` (as magnitudes não estavam lidas), `realce-vcm` (o grampo
não é do realce), `chiclet-geometria` (o `0,2250` mede o gabarito, não o
binário), `realce-forma` (os 7,5 unidades movem o pixel e não têm `[BIN]`),
`blur-material` (a extensão foi medida e o gabarito a recusou). Lidos de fora,
parecem seis omissões. **São uma família, e dão a mesma razão com palavras
diferentes.** `realce-forma.md` a escreve melhor:

> Aplicá-lo seria **escolher número pelo diff, que é exatamente o que destruiria
> o oráculo**.

O nome existe porque estes são os casos mais fáceis de confundir com fracasso, e
porque o que eles entregam é verificável: uma transcrição no código, um caso de
teste que a fixa, e uma nota no render dizendo o que não está sendo desenhado e
por quê. `Source/RenderBox/BlendFormula.h` é o exemplar que seis laudos citam
pelo nome.

### 35.2. Onde cada laudo foi parar

| laudo | classe | seção |
|---|---|---|
| `sombra`, `sombra-desenho`, `sombra-anel`, `sombra-overdraw` | FECHOU ×3, FECHOU+ELIMINOU | **§36** |
| `desfoque`, `desfoque-escada`, `blur-material`, `opaque-bit20` | FECHOU ×2, FECHOU E DECLAROU, ELIMINOU | **§37** |
| `especular`, `highlights`, `realce-intensidade`, `realce-vcm`, `realce-vcm-fechado`, `realce-forma` | mista | **§38** |
| `chiclet`, `chiclet-curva`, `chiclet-geometria`, `chiclet-realces`, `canto-do-chiclet` | mista | **§39** |
| `vidro-sobre-raster`, `semente-aa`, `campo-de-distancia`, `supersample-campo`, `sdf-nivel-zero` | mista | **§40** |
| `refracao` | FECHOU + ELIMINOU | **§41** |
| `gradiente-do-fundo` | FECHOU + ELIMINOU | **§42** (e §23) |
| `oraculo-appicon`, `icon-composer-27` | INFRA, ELIMINOU | **§43** |
| `orcamento-de-tempo` | INFRA + ELIMINOU | **§44** |
| `svg-definicoes` | FECHOU | `Docs/04-o-svg.md` |
| `portao-glass` | FECHOU | `Docs/01-o-formato-icon.md` |
| `idiom-do-canvas`, `canvas-navegacao`, `painel-camadas`, `inspetor-e-auditoria`, `device-do-onyx` | INFRA | `Docs/Specs/2026-09-13-casca-e-modelo-editavel.md` |

E o **§45** é um bloco de método: quatro armadilhas que custaram uma leitura
errada cada, nestes dois dias, e que não estavam escritas em lugar nenhum.

---

## 36. A sombra, do documento ao pixel

`[BIN]` **A sombra do `GlassMaterial` nunca passa pelo `ShadowStyle` do
RenderBox.** Ela é um `drawShape:` comum, com a opacidade viajando **crua** no
argumento `alpha:`, recortada por uma rampa linear que o nome `ringWidth`
disfarça de anel, e desenhada **duas vezes** — a segunda por cima da arte e
recortada pela cobertura dela.

Quatro frentes, e a ordem entre elas é a da descoberta: `sombra.md` leu a
aritmética, `sombra-desenho.md` nomeou o fator que faltava e a fez desenhar,
`sombra-anel.md` derrubou a própria hipótese que o abriu, e `sombra-overdraw.md`
desenhou a segunda passagem e **eliminou** o primeiro dos três suspeitos do aro
escuro do ápice.

### 36.1. A aritmética da alpha — duas multiplicações e nenhum grampo

`[BIN]` `shadowOpacity` é lido **uma vez** em todo o binário, em `0x49F0C`,
dentro da função `0x49ED4`–`0x4A2D4`:

```
0x00049F08   ldrb w21, [x0, #0x01]   ; shadowStyle
0x00049F0C   ldr  d8,  [x0, #0x08]   ; *** shadowOpacity ***
0x00049F10   ldr  d9,  [x0, #0x38]   ; FinalizedIcon.Layer.opacity
0x00049FD4   ldr  d10, [x9]          ; Shadow.neutralOpacity[k]   (ramo neutro)
0x0004A044   ldr  d10, [x9]          ; Shadow.vibrantOpacity[k]   (ramo vibrante)
0x0004A06C   fmul d0, d8, d10
0x0004A070   fmul d0, d9, d0
0x0004A1F8   fcvt s0, d0
0x0004A20C   bl   #0x8e8c0           ; -[RBDisplayList drawShape:fill:alpha:blendMode:]
```

```
alpha = shadowOpacity × Shadow.<vibrant|neutral>Opacity[3 − classe] × Layer.opacity
```

**Sem grampo, sem `pow`, sem teto.** `shadowOpacity = 2.0` produz `alpha > 1` sem
uma reclamação, e negativo passa negativo. Varridos os **sete** chamadores de
`_pow` (`0x8DDF4`) catalogados no §29.4 — `0x125C8`, `0x125EC`, `0x12620`,
`0x4A728`, `0x4A768`, `0x4A998`, `0x4A9D8` —, **nenhum** está nesta cadeia, e não
há `fminnm`/`fcsel` de grampo entre `0x49F0C` e `0x4A06C`.

> **A frase do §29.8 estava certa e a conclusão implícita estava errada.** Não há
> par `Max`/`Power` para `shadowOpacity` porque **não há normalização nenhuma** —
> a ausência era o sintoma, não a lacuna. `[BIN]` A assimetria é o achado: a
> Apple normaliza o que vai virar **comprimento em pontos** (o
> `refractionHeight`, grampeado dos dois lados antes do `pow`) e não normaliza o
> que **já é uma fração**.

`[ART]` E o corpus corrobora a ausência do grampo por um caminho que ninguém
escolheu. Das **302** resoluções de `shadow` dos 145 documentos, **três passam de
1.0** — `Apollo-Reborn/AppIcon` grupos 0 e 2 com `2.4` e `1.6`, e
`Apollo-Reborn/LG-antenna` com `2.4` — e elas caem em **décimos exatos** contra a
tabela medida: `2,4 × 0,375 = 0,9` e `1,6 × 0,375 = 0,6`. **O autor estava
afinando contra a tabela.** Uma transcrição que grampeasse `shadowOpacity` em
`[0,1]` desenharia a sombra do Apollo em `0,375` no lugar de `0,9` e não teria
como perceber.

`[ART]` O resto do censo: **271 de 271** grupos carregam `shadow`; 190 resoluções
`neutral`, 87 `layer-color` (= `vibrant`), 25 `none` e **zero** `automatic`; a
opacidade mais comum é `0.5`, em **226 das 302**.

### 36.2. A escala que faltava é `ICRRenderingParameters.Shadow`

`[BIN]` O papel de "normalização" é feito por um sub-struct que o §29.3 não tinha
lido: `Shadow`, **15 campos**, em `params + 0x2B0`, **`0xF8` bytes**. Duas fontes
independentes concordam campo a campo — a igualdade `Shadow == Shadow` em
`0x6DFE4` e a cópia de `0x4EABC`–`0x4EBB4` —, e a ordem bate exatamente com as 15
`CodingKeys` do metadado em `0xA5064`: não sobra nem falta campo.

| desloc. | campo | default | onde o default é assado |
|---|---|---|---|
| `+0x00` | `offsetX` | `0.0` | `0x5EC54` |
| `+0x08` | `offsetY` | `32.0` | `0x5EC54` |
| `+0x10` | `ringWidth` | `[16,16,16,16]`, tag presente | `0x5EC58` |
| `+0x38` | `radius` | `[0.3,0.3,0.3,0.3]` | `0x5EC68` |
| `+0x58` | `vibrantOpacity` | `[0.75,…]` | `0x5EC7C` |
| `+0x78` | `neutralOpacity` | `[0.375,…]` | `0x5EC88` |
| `+0x98` | `blendMode` | `2 = multiply` | `0x5EC94` |
| `+0x99` | `blendModeForVibrantOnDim` | `0 = normal` | `0x5EC94` |
| `+0x9A` | `overdrawBlendMode` | `2 = multiply` | `0x5EC98` |
| `+0xA0` | `vibrantBrightness` | `0.75` | `0x5EC9C` |
| `+0xA8` | `ignoreFillOpacity` | `true` | `0x5ECA4` |
| `+0xA9` | `drawOverContent` | `true` | `0x5ECA4` |
| `+0xB0` | `translucencyForMaxOverdraw` | `0.3` | `0x5ECAC` |
| `+0xB8` | `maxNeutralOverdrawOpacity` | `[0.2,…]` | `0x5ECAC` |
| `+0xD8` | `maxVibrantOverdrawOpacity` | `[0.5,…]` | `0x5ECC0` |

Os tipos não são inferidos: os acessores trazem a referência simbólica —
`ringWidthAA14SizeBasedValueVySdGSgv` (`0xE32B7`), com o `Sg` do `Optional` sendo
exatamente o byte de tag medido em `+0x30`, e `overdrawBlendModeAA0A0V0gH0Ov`
(`0xE2F53`) confirmando que `+0x98`…`+0x9A` indexam a tabela de **18** do §17.3 e
não a de **56** do RenderBox.

**O índice é classe de TAMANHO, não de aparência.** `[BIN]` `0x18D70`–`0x18DCC`
compara `min(largura, altura)/escala` contra três limiares em `params+0x230`,
`+0x238` e `+0x240`, que o metadado de `0xA5840` nomeia **`minMediumSize`,
`minLargeSize`, `minDisplaySize`**. Três limiares, quatro classes, e o enum de
`0xA5800` é `small 0, medium 1, large 2, display 3`.

> ### A armadilha silenciosa desta família inteira
>
> `[BIN]` **A tabela é lida ao contrário.** `SizeBasedValue` declara os quatro
> campos em `0xA5780` na ordem **`display, large, medium, small`**, que é a ordem
> da memória. Por isso o `csel` triplo de `0x49FA4`–`0x49FD0` resolve
> `k=3 → +0x00` e `k=0 → +0x18`, isto é **`valor[3 − k]`**.
>
> Quem transcrever `valor[k]` troca `small` com `display` e `medium` com `large`.
> **Com os defaults desta versão os quatro valores são iguais em todas as cinco
> tabelas do `Shadow`: o pixel sai idêntico e o erro não avermelha teste nenhum.**
> Ele só acorda num documento que diferencie as classes. A frente irmã da
> `translucency` tropeçou no mesmo `csel` de quatro vias no
> `TranslucencyEffect.strength` — é um padrão do `IconRendering`, não um acidente
> de um campo, e é por isso que a inversão vive num helper só (`sizeBasedValue`,
> em `GlassTranslucency.h`).
>
> **E ela tem fronteira, que é o que impede a lição de virar superstição:**
> `[BIN]` **não há `SizeBasedValue` no caminho do raio do chiclet** — entre a
> raiz do raio e o `setRoundedRect:` não existe um único índice por classe de
> tamanho, e o que parecia tabela de quatro é um `Optional<Double>` num
> dicionário de *stride* 32 (§39.1). A inversão existe, e existe **em um lugar
> só**.

### 36.3. `shadowStyle` troca o CONSUMIDOR, não um bit

`[BIN]` O enum denso é `automatic 0, none 1, vibrant 2, neutral 3`, e ele decide
**quatro** coisas mais um portão:

**(a) se desenha.** `0x49F74`: `cmp w21,#1 / b.eq 0x4A21C` — `none` retorna.

**(b) qual tabela.** O ramo de `0x4A248` manda `vibrant` **e `automatic`** para
`vibrantOpacity` (`ctx+0x4560`) e `neutral` para `neutralOpacity` (`ctx+0x4580`).
**`automatic` agrupa com `vibrant`.**

**(c) qual cor.** Cada ramo escolhe um global `swift_once` diferente:

| ramo | token | inicializador | valor |
|---|---|---|---|
| vibrante | `0xCDE68` | `0x3DAA0` | **`(1,1,1,1)` — branco opaco** |
| neutro | `0xCDE90` | `0x3DAD4` | **`(0,0,0,1)` — preto opaco** |

Os quatro `Double` viram `float` em `0x4A15C`–`0x4A16C` e entram como
`tintColor:` do `setRBImage:…` de `0x4A1F0`. **Tinta branca é identidade**: a
sombra vibrante mantém as cores do glifo, a neutra tinge tudo de preto. É isto
que o `shadowInfusesGlyphColor` do §29.2 significa em pixels.

> **Correção ao §29.6 deste documento.** Aquela seção lê o global protegido pelo
> token `0xCDE68`, inicializador `0x3DAA0` escrevendo "quatro `1.0`", e o trata
> como o **valor normalizado de entrada da refração**. `[BIN]` **Ele não é isso.**
> É a **cor da sombra vibrante**, e o irmão `0xCDE98` (preto) é a da neutra — o
> mesmo par que a sombra do chiclet usa em `0x434E8`–`0x43514`.

**(d) qual byte de mescla**, e **(e) um portão acima de tudo.** `[BIN]`
`0x49F40`–`0x49F70`: se `[ctx+0x510] != 1`, **ou** se o OR de cinco `Double` em
`ctx+0x4E8..0x508` não for zero, a sombra é **sempre a neutra** e o `vibrant` do
documento não chega ao pixel. `[OBS]` Esses cinco `Double` e o byte têm a **mesma
forma exata** do portão do `specularPlacement` em `0x49280`–`0x492C0`, são
estados de recoloração do ícone, e **não ganharam nome**. Sei o que eles fazem e
não sei o que eles são.

### 36.4. O terceiro fator, e o descritor que finalmente tem nome

Este é o achado que fez a sombra desenhar, e ele veio por uma porta que nenhuma
frente tinha usado: **o metadado de reflexão Swift**, que estava no disco desde
antes do laudo-mãe.

`[BIN]` O descritor de `0xC0` bytes **é** `IconRendering.FinalizedIcon.Layer`, e
o terceiro fator da multiplicação é o `opacity` dele. Três leituras independentes
produzem a mesma tabela, vindas de três lugares diferentes do arquivo:

1. **A reflexão** — `__swift5_fieldmd` em `0xA301C`, nove campos, `opacity`
   tipado `Sd`.
2. **O vetor de deslocamentos emitido** — `__swift5_types` em `0xAC614` leva ao
   descritor nominal `0x9EF30`, e o metadado estático de `0xBD3F0` dá
   `material@0x00`, `blendMode@0x31`, **`opacity@0x38`**, `knocksOutBorder@0x40`,
   `image@0x48`, `contentFrame@0x50`, `effectsFrame@0x70`, `sdf@0x98`,
   `shadowImage@0xB0`. A *value-witness table* de `0xBD388` dá `size`/`stride` =
   **`0xC0`** — o passo do array confirmado **pelo tipo**, não pelo
   `add x23,x23,#0xc0`.
3. **O escritor** — `0x17038`, `str d8,[x8,#0x58]`, com `d8` vindo de
   `Icon.Layer.opacity`: **cópia verbatim**, nem grampo nem transformação.

`[BIN]` O `+0x31` não é acidente: `Icon.GlassMaterial` tem `size 0x31` com
`stride 0x38` (metadado `0xBED40`), então `blendMode` empacota no byte que a
`size` deixou livre e `opacity` cai em `0x38` e não em `0x40`.

> `[BIN]` **`Icon.Layer` NÃO é este struct.** Metadado `0xBE8E8`, `size 0x58`,
> campos `elements@0x00, opacity@0x08, blendMode@0x10, material@0x18,
> performsLightingByElement@0x49, appearance@0x50`. São dois tipos com nomes
> parecidos, e confundi-los é o erro que a aritmética de offsets convidava.

Duas correções de carona ao laudo-mãe, das que só aparecem quando o tipo tem
nome: `[descritor+0x31]` **não é um `Bool`**, é o `blendMode`; e
`[descritor+0xB0]` não é "a imagem", é o **`shadowImage`** — a saída da
preparação realimentada na composição. O ciclo fecha.

E um contra-indício se dissolveu em vez de ser respondido: o laudo-mãe hesitou
porque `Shadow.ignoreFillOpacity` é `true` por padrão e não é consultado em
`0x49ED4`. `[BIN]` **Ele não precisa ser** — `ignoreFillOpacity` é campo de
`ICRRenderingParameters.Shadow`, um struct de **parâmetros**, e o `+0x38` é a
opacidade de uma **camada**, noutro tipo. A tensão era entre um campo e um
homônimo. `[OBS]` Quem lê o `ignoreFillOpacity` continua sem resposta, menor do
que parecia.

### 36.5. A geometria: cinco passos, e a pendência que não existia

O pedido da frente listava a geometria como pendência, apontando um `[OBS]` de
`inputShadowOffset`. **Ela já estava medida inteira**; o que faltava era
transcrever. Com `s = min(largura, altura)/1024` (o literal `2^-10` de `0x20B88`):

| passo | operação | endereço | com os defaults, alvo 1024 |
|---|---|---|---|
| 1 | cor: `colorMultiply(v,v,v,1)`, `v = vibrantBrightness` | `0x20BB4`, **pulado se `v == 1`** (`0x20BC0`) | `×0,75` no ramo vibrante |
| 1' | cor: `alphaMultiply(RBColorBlack)` | `0x20BAC`, GOT `0xC5818` | silhueta preta no ramo neutro |
| 2 | translação `(s·offsetX, s·offsetY)` | `0x20BDC`–`0x20BEC` | `(0, 32)` px |
| 3 | desfoque `raio = s · blurStrengthMax · clamp(radius[3−k],0,1)` | `0x20C38`, grampo `0x20C14`–`0x20C28` | `0,3 × 64 = 19,2` px |
| 4 | anel: recorte com `−(s · ringWidth[3−k])` | `0x20C8C`, `0x20CE8` | 16 px |
| 5 | `drawDisplayList:` | `0x20CF4` | |

O `blurStrengthMax` do passo 3 é lido em `0x20C08` como `[box+0x1F8]` =
`params+0x1E8`: **o mesmo campo `blurStrengthMax = 64.0` do §29.3**, reusado.

**Os passos 2 e 3 comutam** — uma gaussiana é invariante a translação —, então a
ordem entre eles não é uma escolha.

`[BIN]` E o grampo do passo 3 é o **terceiro formato** desta família, o que é
informação e não ruído: `refractionHeight` grampeia dos **dois** lados antes do
`pow` (`0x4A708`), o `blurStrength` do material só tem **teto** (`0x4A948`), o
`Shadow.radius` grampeia dos **dois** lados (`0x20C14`) e o `shadowOpacity` **não
grampeia**. Quatro quantidades da mesma matéria, quatro decisões diferentes.

### 36.6. O anel que é rampa — e a hipótese que o abriu morreu no meio

`[BIN]` **O "anel" não é um anel.** É uma rampa linear de máscara, monótona na
profundidade, que vale `0` sobre o contorno da camada e sobe até `1` a
`ringWidth` pontos **para dentro** dele:

```
mask(p) = clamp( profundidadeDentro(p) / (ringWidth[3−k] × s), 0, 1 )
```

A hipótese de entrada era razoável — *"largura negativa alimentando um recorte
tem cara de inset; o anel seria a coroa entre o contorno e o contorno
encolhido"* — e ela morreu **pela metade**, que é o resultado mais útil que uma
hipótese pode ter:

- **Confirmada no lado do *inset*.** A banda afetada fica de fato dentro do
  contorno, e é a negação de `0x20C8C` que a põe lá.
- **Derrubada no lado da *coroa*.** `[BIN]` `0x11C40` (1.604 bytes, lido inteiro)
  não constrói forma nenhuma: repassa os quatorze argumentos do helper genérico
  `0x10E1C` mudando três, e instala entre `save` e `restore` **exatamente dois
  filtros** — um `addAlphaThresholdFilterWithMinAlpha:maxAlpha:` (`0x11D64`) e um
  `addColorMatrixFilterWithArray:` (`0x12138`) cujos vinte `Float` são dezenove
  zeros e um `1.0` no índice 15, isto é `out.rgb = 0, out.a = in.r`. E a
  implementação do primeiro está no RenderBox, que **mantém os símbolos**:
  `RB::_GLOBAL__N_1::render_(AlphaThresholdEffect…)` (`0x893A4`) calcula
  `scale = 1/(maxAlpha − minAlpha)` e `bias = −minAlpha × scale`, e escreve os
  dois em `+0x44`/`+0x48` dos globais de shader. **Uma escala e um viés é uma
  função monótona.** Nenhuma escolha de `minAlpha`/`maxAlpha` faz aquilo ser
  não-nulo numa faixa e nulo dos dois lados dela.

> **Essa distinção não é acadêmica.** Desenhar a coroa plausível apagaria a
> sombra inteira no miolo do glifo e deixaria só um contorno — **o oposto** do
> que o binário faz. E o caso de teste que a fixa é o do meio de
> `Tests/test_glass_shadow.cpp`: uma barra horizontal num campo 41×41 com anel
> de 8, checando que a máscara sobe e **não volta a descer** até o miolo. Uma
> coroa falharia nele.

`[BIN]` **E o `maxDistance` se cancela.** A banda que `0x11CF8`–`0x11D3C` monta é
`minAlpha = 0.5`, `maxAlpha = 0.5 + ringWidthEmTexels/(2·maxDistance)`; o sinal
vem de `-[RBDisplayList addDistanceFilterWithMaxDistance:scale:flags:]`
(`RenderBox 0x3F354`), que faz `fneg d2,d0` e grava
`zeroDistance = +maxDistance`, `oneDistance = −maxDistance` — logo `alpha > 0.5`
é **interior**. Substituindo, `t = profundidade / ringWidth`: **a máscara não
depende da resolução nem da faixa do campo de distância, só da largura em
pontos.**

`[BIN]` E há **duas** guardas, não uma. `0x20C3C`–`0x20C50` pula o anel quando o
byte baixo do segundo word do `SDF` é `0xFF` — o **caso vazio** dele — e quando
`ringWidth` é `nil`. **Camada sem campo de distância não ganha anel.** O
laudo-mãe tinha chamado a primeira de "um flag do chamador"; é mais específica.

E um `[OBS]` vizinho fechou por varredura, com uma lição de método de brinde:
`[BIN]` `vibrantBrightness` tem **um** consumidor em todo o binário (`0x20BB4`),
provado varrendo as 142.694 instruções do `__text` atrás de cada
`ldr dN,[xM,#0xa0]` (dez sítios) e cada `ldr dN,[xM,#0x350]` (seis — o campo tem
duas grafias porque `Shadow` mora em `params+0x2B0`). **O `0,75` nem chega ao
*constant pool*:** o ARM64 o materializa com `fmov`, e há **zero** ocorrências do
padrão `0x3FE8000000000000` no arquivo inteiro. Ver §45.2.

### 36.7. A passagem de overdraw — está desenhada, e NÃO é o aro

`[BIN]` Não é um segundo efeito: é **a mesma função** `0x49ED4`, chamada de novo
com `w1 = 1`, com o mesmo descritor, a mesma imagem, o mesmo retângulo, a mesma
tinta e a mesma alpha. Varrendo `0x49ED4`–`0x4A21C`, o `w1` aparece em exatamente
dois lugares e os dois são o mesmo `csel`: **a única coisa que ele muda lá dentro
é o byte de mescla** (`overdrawBlendMode` em vez de `blendMode`).

**Então o que faz dela um "overdraw" está FORA da função. É o recorte.**

`[BIN]` `0x45FA8` abre a camada, `0x45FE4` chama `0x4B4EC` e `0x45FF4` recorta
com `clipLayerWithAlpha:mode: 0`. E `0x4B4EC` **não é construtor de máscara: é o
desenho do conteúdo** — prova por `xref`, porque um dos quatro chamadores dele é
`0x4B3EC`, dentro da própria `0x4AF20` que a passagem de conteúdo invoca uma
instrução antes. **A camada de recorte recebe o mesmo desenho, do mesmo
descritor, que a passagem de conteúdo acabou de pôr na tela.** Isto é o nome do
campo virando geometria: `drawOverContent`.

`[BIN]` A ordem, lida do driver por elemento `0x48B74`: vidro (`0x48BD4`) →
conteúdo e overdraw (`0x48C08`) → **realces** (`0x48DB4`), sendo que `0x48DB4`
está no ponto de junção para onde todos os `b.eq` intermediários saltam, de modo
que os realces correm em qualquer ramo.

`[ART]` Dos 271 grupos, **149 abrem a passagem**, em 114 documentos. E o `t`
satura quase sempre: a alpha do recorte é `0,2` em 105 grupos e `0,5` em 18 —
**123 dos 149 já estão no teto**.

> ### A medida que importa, e ela é negativa
>
> A frente existiu porque o gabarito da Apple tem um **aro escuro de 0–5 unidades
> de canvas no ápice superior** que nenhum dos cinco realces explica, e o overdraw
> era **um dos três candidatos**. Medido com o perfil de luma por profundidade, no
> mesmo lugar em que `realce-forma.md` mediu o dele:
>
> | profundidade (un.) | 0,0 | 2,0 | 4,0 | 16,0 | 36,0 | **60,0** | 116,0 |
> |---|---|---|---|---|---|---|---|
> | `depois − antes` | **−0,11** | −0,12 | −0,07 | −0,81 | −2,70 | **−4,06** | −3,43 |
> | `Apple − antes` | −82,76 | −53,64 | +17,37 | +7,39 | +8,81 | +6,19 | +0,45 |
>
> `[BIN]` (medida) **A passagem é monótona crescente na profundidade e vale
> praticamente zero no aro.** O aro do gabarito é o oposto: local, entre 0 e 5
> unidades, com o brilho voltando logo abaixo. E o motivo é previsível da própria
> transcrição — a imagem que ela compõe é a silhueta com o anel, borrada com sigma
> 19,2 e deslocada 32 unidades; perto da borda de cima ela é quase nula **por
> construção**. Recortá-la à arte não pode produzir um aro no contorno; produz o
> contrário de um aro.
>
> **E o sinal do deslocamento não salva a hipótese.** Num build temporário
> revertido antes do commit, com `offsetY` negado, o escurecimento fica quase
> **constante em ~5,5 luma** em toda a profundidade — escurecimento geral, não
> banda. Isso também tira o `[OBS]` antigo da lateralidade de `y` da lista de
> explicações do aro, sem fechá-lo: **uma parede a menos para a próxima frente.**

`[BIN]` Contra a média global a passagem **piora** um canal: R vai de 8,95 para
8,93, G de 10,03 para **10,26**, B não move. **E isso não é motivo para não
desenhá-la.** A aritmética, a geometria e a ordem estão lidas com endereço, e o
critério deste repositório é a leitura e não o diff. Desligá-la porque o Δ médio
subiu 0,23 seria exatamente o *overfitting* que a regra proíbe, com o sinal
invertido.

### 36.8. O que a sombra deixou aberto

| # | `[OBS]` | onde |
|---|---|---|
| 1 | O portão de `[descritor+0x31]` = `blendMode`: `0x45F10` exige o byte **zero** para a passagem abrir. Nenhuma chave de documento escolhe o `blendMode` de uma **camada**, então aqui a passagem abre sempre que a aritmética a abre — **uma camada com mescla não-normal não ganharia overdraw no alvo, e ganha aqui** | `sombra-overdraw` §10.1 |
| 2 | `[INF]` O que o `float` de `clipLayerWithAlpha:` multiplica: a cobertura, por três indícios e **nenhuma** leitura do rasterizador | `sombra-overdraw` §10.2 |
| 3 | A ordem interna do recorte contra o filtro de matriz de cor — `0x4AF20` instala um `addColorMatrixFilterWithArray:` antes de `0x4B4EC` no caminho longo, e a camada de recorte do overdraw chama `0x4B4EC` **sem** ele | `sombra-overdraw` §10.3 |
| 4 | **A luz a mais no miolo.** `Apple − nós` é positivo e cai de +10 a +0,45 entre 20 e 116 unidades de profundidade, e **nada** desta família a explica | `sombra-overdraw` §10.4 |
| 5 | `Shadow.ignoreFillOpacity` sem consumidor conhecido; as seis palavras do portão `0x49F40`–`0x49F70` sem nome; a escrita de `ctx+0x469F` | `sombra-anel` §10.4/§10.5 |
| 6 | A ordem entre a máscara de translucidez e a sombra — qual imagem o alvo alimenta ao `shadowImage`. Com máscara identidade as duas leituras dão o mesmo pixel | `sombra-desenho` §6.7 |
| 7 | O default de `Icon.Layer.opacity`. A cópia é verbatim e o inicializador **não se materializa** neste slice: a varredura dos 117 sítios de `fmov dN,#1.0` do `__text` não acha um `str` num `Layer`. O `1.0` é identidade multiplicativa — regra do documento, não valor lido | `sombra-desenho` §6.4 |

E o pixel, para quem quiser refazer: a sombra passou a desenhar movendo **388.659
de 1.048.576** no `Apollo-Reborn/AppIcon` (37,07 %, Δ máx 190) e **166.003** no
`CodeEditApp/CodeEditAlphaIcon`; o `Aeastr/GlowGetter` moveu **zero**, e é o
**controle negativo** — os três grupos dele têm `glass: false` em todas as cinco
camadas, então a porta recusa, como deve. **A porta segura nos dois sentidos.**

---

## 37. O desfoque: o kernel, a escada, e a superfície que o gabarito recusou

`[BIN]` **O raio é o sigma.** E o alvo **nunca paga** os taps que isso
implicaria: ele desce a resolução **subtraindo a variância que a descida
introduz**. O que ele desfoca é o **fundo**, sobre uma extensão de quadro que só
o `effectsFrame` do descritor descreveria — e cuja única leitura disponível foi
desenhada, medida contra o gabarito da Apple e **recusada por ele**.

Quatro frentes, e a sequência é uma cadeia: `desfoque.md` leu o kernel e corrigiu
um erro de fator 3 nascido horas antes; `desfoque-escada.md` leu a conta que
evita executá-lo inteiro **e corrigiu uma transcrição do seu antecessor**;
`blur-material.md` derrubou duas das três paredes da superfície e apanhou do
gabarito na terceira; `opaque-bit20.md` foi atrás do único candidato que sobrava
e voltou com **não**.

### 37.1. `σ = raio`, por três leituras que não compartilham caminho

O erro que isto corrigiu tinha nascido no mesmo dia: `GlassShadow.h` usava
`kShadowBlurSigmaPerRadius = 1/3`, e o próprio arquivo confessava ser *"a única
escolha deste arquivo que é convenção e não medida"*, listando `sigma = raio`
como *"igualmente não lida"*. **A convenção estava errada por um fator de três,
na direção estreita, e a alternativa recusada era a certa.**

**Leitura 1 — a cadeia de GPU, da entrada aos taps.** `[BIN]`

| endereço | símbolo | o que faz com o raio |
|---|---|---|
| `0x3E8D4` | `-[RBDisplayList addBlurFilterWithRadius:opaque:]` | põe `opaque` nas flags e cai (*tail call*) no próximo |
| `0x3E69C` | `_RBDrawingStateAddBlurFilter` | `fcvt s0,d8` (`0x3E728`): estreita para `float` e **passa intacto** |
| `0xFE598` | `GaussianBlur::GaussianBlur(float, …)` | `dup v0.2s` + `str d0,[x0]` — vira par por eixo. **Nenhuma aritmética** |
| `0xFEC34` | `GaussianBlur::render` | **`0xFED04 fmul v0.2s, v10.2s, v10.2s`** — o raio **ao quadrado** vira o campo de **variância** |
| `0xFF964` | `BlurRenderer::render` | `0xFFEF0` entrega **variância / nº de passadas** ao kernel |
| `0xFE378` | `NarrowBlurKernel::construct(float v)` | `1/(2v)`, e o laço `0xFE3CC`–`0xFE3F0` calcula `exp(−x²/(2v))` |

`w(x) = exp(−x²/(2v))` com `v = raio²` **é** `exp(−x²/(2σ²))` com `σ = raio`.

E a **tabela assada** fecha a conta sem código nenhum: `RB::(anon)::narrow_blur_15`
(`0x15F9D0`) é um kernel de 15 taps literal em `__const`, e
`0,015928393 / 0,11769579 = 0,13533528` contra `exp(−7²/(2·12,25)) = exp(−2) =
0,13533528` **em todos os dígitos impressos**. O `12,25` não é chute: é o imediato
`0x41440000` = `3,5²` que `NarrowBlurKernel::get` compara antes de devolver essa
tabela. **Argumento, tabela e limiar concordam que o número que circula é uma
variância.**

**Leitura 2 — o caminho de CPU.** `[BIN]` `RB::CGContext::apply_blur` (`0xBFDC0`)
passa o `float` **direto** a `gaussian_kernel_` (`0xC35F4`) — nenhuma instrução
entre os dois toca `v0` —, e essa função é
`halfWidth = min(ceil(σ·2,8), 1024)` com `w[i] = exp(−i²/(2σ²))`. Gaussiana de
livro-texto, sigma = o argumento. Quem a alimenta (`0xFF0D0`) faz
`σ = 0,5 · escalaCTM · (rx + ry)`. **Um raio que precisasse ser dividido por três
seria dividido aqui, e não é.**

**Leitura 3 — os bounds.** `[BIN]` `GaussianBlur::roi` (`0xFEB58`) cresce a ROI em
`max(ceil(raio · 2,8), 0)` por eixo, e o `2,8` **não está no pool**: é
`mov w8,#0x3333` + `movk w8,#0x4033,lsl #16` em `0xFEBA4`, o mesmo padrão de bits
que o kernel de CPU carrega de `0x15ECF0`. **Um filtro cujos bounds crescem
`2,8·r` é um filtro cujos taps morrem em `2,8·r`** — sob `σ = raio/3` o alvo
estaria reservando **8,4 sigmas** de margem para um kernel que trunca em três.

O repositório já tinha a curva certa e não sabia: o `gaussian` de `SvgFilter.cpp`
calcula `exp(−i²/(2σ²))` normalizado, truncado em `ceil(3σ)`. **É a mesma curva**,
e a única diferença — `3,0` contra o medido `2,8` — move cada peso em cerca de
`0,24 %`, porque uma gaussiana guarda `0,99730` dentro de 3σ e `0,99489` dentro de
2,8σ. Era metade da resposta, e a metade que já estava certa.

Custo em pixel de consertar o fator 3: **455.043 de 1.048.576 (43,40 %)** no
Apollo a 1024 px, Δ máx 93 — a sombra daquele ícone passou de `σ = 6,4` para
`σ = 19,2`. E **duas notas de `RenderedIcon::notes` saíram**, pela regra que o
próprio `GlassShadow.h` escreve: *uma entrada de `notes` ganha o seu lugar
nomeando uma lacuna que ainda está aberta.*

### 37.2. A escada de qualidade — o alvo nunca faz 361 taps, ele desenha menor

Corrigir o sigma deixou o render de 1024 px em **37,37 s**. Com
`halfWidth = ceil(2,8σ)` e σ até 64, são **361 taps por eixo**. `[BIN]` O alvo não
os executa, e a máquina substituta tem três peças:

**(a) Teto de sigma por passada.** `0xFED34`–`0xFED50` lê dois bits de qualidade
(`ubfx w9, w8, #4, #2`) e um `fcsel` de três vias escolhe:

| `(flags>>4)&3` | σmax | taps cacheados |
|---|---|---|
| `1` | `3,5` | 15 |
| `3` | `7,0` | 31 |
| **qualquer outro (default)** | **`5,25`** | 23 |

**(b) Passadas que somam variância.**
`nRaw = ceil(max(rx²,ry²)/σmax² − 0,001)` (`0xFED5C`–`0xFED74`), grampeado a
`[1,32]` em `renderer+0x1c`. Variâncias gaussianas **somam**: `n` passadas de
variância `v/n` compõem `v`, e `0xFFEF0` entrega exatamente `v/n` ao kernel.

**(c) Redução de resolução que subtrai a variância que ela introduz.** `[BIN]`
`0xFEDF0` e `0xFEE24`:

| condição | alvo de render |
|---|---|
| `nRaw >= 7` | `(d + 3) >> 2` — redução **4×** |
| `nRaw >= 3` | `(d + 1) >> 1` — redução **2×** |
| senão | tamanho cheio |

E `BlurRenderer::render` **reescreve a variância antes de qualquer kernel ser
construído**: `v/16 − 0,47265625` (`= 0,6875²`) no 4× e `v/4 − 0,765625`
(`= 0,875²`) no 2×. **A imagem encolhida já carrega o seu próprio borrão na grade
dela, e o kernel é pedido para o resto** — a mesma contabilidade da soma de
passadas, aplicada à reamostragem.

> **Duas armadilhas de leitura nesta seção, e as duas são a mesma armadilha.**
>
> `[BIN]` **A decisão de resolução olha o `nRaw` CRU, não o grampeado.** O valor
> que vai para `renderer+0x1c` é `clamp(nRaw,1,32)`; o que `0xFEDF0` compara é
> `w25`, o bruto. Ler o grampeado daria a escada certa só até 32 passadas.
>
> `[BIN]` **E uma transcrição do laudo anterior estava errada.** `desfoque.md`
> §1.2 escreveu `v/4 − 2,56` (`= 1,6²`) como a conta do 2×. Ela é o 2× do
> **degrau de cima**: o discriminante é `ldrb w8,[x20,#9]` em `0xFFA4C`, e
> `[x20+9]` é escrito em `0xFECF8`–`0xFED00` como `(flags & 0x30) == 0x30`, isto
> é qualidade **3**. O 2× default subtrai `0,875²`. **Nenhuma das duas constantes
> está no pool** — `−0,47265625` é `mov w8,#-0x410e0000` e `−0,765625` é
> `mov w8,#-0x40bc0000` —, e é exatamente por varrer o imediato que elas
> apareceram (§45.2).

**O tempo, em Release, uma execução por medida** (`Apollo-Reborn/AppIcon`,
`--idiom square`): 1024 px **37,37 s → 10,30 s**; 512 px **3,44 s → 2,68 s**; a
sombra sozinha **27,6 s → 1,2 s**.

**A meta de 1 s a 1024 px NÃO foi alcançada**, e isso está escrito com a culpa
realocada por medida em vez de estimada — ver §44.

`[INF]` Três escolhas desta transcrição são declaradas e não lidas: **se a escada
recorre** no alvo (fica atrás de `RenderGroup::add_multipass_renderer`,
`0x105E3C`); **os filtros de reamostragem** (aqui, caixa na descida e bilinear na
subida, o que deixa o resultado `0,09 %` estreito no 4× e `3,9 %` no 2×); e um
**piso de 8 texels** para a redução, que o alvo não tem lido. `[OBS]` E a largura
do kernel por passada no caminho de GPU é `2,0σ`–`2,14σ`, mais estreita que os
`2,8σ` do `roi` e do caminho de CPU; **por que a GPU se permite isso não foi
lido**.

`[BIN]` O pixel que a escada mudou, de propósito: **43.014 de 1.048.576 (4,10 %)**
a 1024 px, com Δ máx entre pixels **visíveis** de 2/5/8/1 e `p99 = 2`. E a
ressalva que o laudo publicou sem ser obrigado: contando **todos** os pixels o Δ
máx é **25** no azul, em **35** pixels — **todos com `alpha == 0` nos dois
quadros**, lixo de `acc[c]/a` invisível em qualquer composite. *"As duas estão
aqui porque publicar só a primeira seria escolher o número que agrada."*

### 37.3. A superfície: duas paredes caíram, a terceira era nossa, e a quarta doeu

`[BIN]` `0x4A2D4`–`0x4AC84` é **uma** função, e os dois sítios de desfoque moram
dentro dela. Ela ramifica em dois escalares e em mais nada:

```
0x4A404  fcmp d13, #0.0 ; b.le 0x4A5DC      ; blurStrength <= 0 ?
0x4A40C  fcmp d1,  #0.0 ; b.ne 0x4A82C      ; refractionStrength != 0 ?
```

| `blurStrength` | `refractionStrength` | destino | camada |
|---|---|---|---|
| `> 0` | `== 0` | **`0x4A418`** | `beginLayerWithFlags:` **1**, corpo **vazio** |
| `> 0` | `!= 0` | `0x4A82C` | flag **`0x80`** em volta do corpo da refração |
| `== 0` | `!= 0` | `0x4A5DC` | flag 1, sem desfoque |
| `== 0` | `== 0` | `0x4A34C` | desenho simples, sem camada |

**Parede (1) — qual ramo roda. CAIU, e a resposta veio do corpus.** `[ART]`
`refractionStrength` **não é chave de `.icon`**: o default da constante de oito
campos (`0x93B30`) é `0.0`, e um `grep -rl refraction` sobre os 145 documentos
devolve **zero arquivos** — idem o gabarito 27.0-129 e o ícone do usuário. **Todo
documento toma o primeiro ramo**, `0x4A5B4`, flag 1 `needs-background`, corpo
vazio. E de carona cai a ordem, que é o formato inteiro do efeito: `[BIN]` o
conteúdo do grupo é desenhado **antes** da camada (`bl 0x49ED4` em `0x4A488`
precede o `0x4A48C` que abre o `save`), logo **o fundo que a camada precisa
inclui a arte do próprio grupo** — o grupo é desfocado junto com tudo o que está
embaixo dele, não meramente por cima.

**Parede (2) — o recorte. CAIU, e virou aritmética.** `[BIN]` Com os seis stubs
de `CGRect` resolvidos pela **tabela de símbolos indiretos** (§45.3):

```
frame  = CGRect em descritor +0x70 .. +0x88
canvas = CGRect em ctx       +0x558 .. +0x570
r = CGRectMake(MinX(frame)*W(canvas), MinY(frame)*H(canvas),
               W(frame)*W(canvas),    H(frame)*H(canvas))
r = CGRectOffset(r, MinX(canvas), MinY(canvas))
r = CGRectInset (r, -[ctx+0x46A8], -[ctx+0x46A8])
```

**Um frame multiplicado pelo TAMANHO do canvas e deslocado pela ORIGEM dele é um
frame em coordenadas unitárias** — é o que a multiplicação significa. O canvas é
`(0,0,1024,1024)` para um ícone quadrado (`0x4291C`–`0x42968`, com o `1024.0`
materializado por imediato em `0x42940`), e `[ctx+0x46A8]` é **unidade de canvas
por pixel** — o mesmo campo que `realce-forma.md` §4.1 leu, confirmado por três
verificações independentes. Um `CGRectInset` pelo **negativo** dele é um
**afastamento de exatamente um pixel de dispositivo em cada lado**: guarda de
sangramento, não corte.

**Parede (3) — onde o composite de baixo está pronto. Não era do alvo.** Era
arquitetura nossa, e a resposta é o fim do laço de camadas do grupo, logo antes
do `blendPremulOver`. Um grupo com `blend-mode` não-normal desenha num alvo
próprio e por isso é **recusado por nome** (`kBlurMaterialBlendedGroupNote`).

**Parede (4) — o frame. Foi desenhada, medida, e o gabarito a recusou.** Sobrava
**uma** incógnita e **uma** leitura disponível: o frame é o retângulo unitário, o
canvas inteiro. Ela foi implementada e renderizada:

| | R | G | B | A | pixels diferentes |
|---|---|---|---|---|---|
| **controle (não desenha)** | **8,95** | **10,03** | **9,89** | **4,95** | 160.327 |
| desfoque, frame unitário | 15,58 | 20,09 | 22,49 | 7,09 | 189.731 |
| diagnóstico: só a cor, alpha preservado | 14,35 | 18,90 | 21,33 | 4,95 | 187.732 |

**Piorou em todos os canais**, e a terceira linha mostra que **não é o alpha**. O
perfil de luma diz a mesma coisa em forma: o gabarito cai `157 → 49` em nove
linhas e **segura um 49 chapado**; nós sem desfoque caímos `202 → 49` em quinze e
seguramos o mesmo 49; **nós com desfoque somos chapados em 84 e nunca chegamos a
49**. Um desfoque de fundo do tamanho do canvas com `σ = 35,84` unidades **apaga
estrutura que o alvo guarda**.

> **O gabarito tem direito de desempatar entre duas leituras, e desempatou:** o
> frame **não** é o retângulo unitário. Qual é, a frente não sabe, e aplicar
> qualquer outro seria escolher extensão pelo diff em 46 documentos. A
> transcrição entra **ligada aos testes e desligada no desenho**, exatamente como
> `BlendFormula.h` faz com o grampo de `plusLighter`. Zero pixel mudou nesse
> commit, com SHA-256 idêntico e tempo idêntico — **como tem de ser quando nada
> desenha.**

`[ART]` O censo, recontado três vezes e batendo dígito por dígito: **123** chaves
`blur-material` em **123 grupos** sobre **73 documentos**; **75** são números,
todos positivos, de `0,05` a `1,0` (isto é, `3,2` a `64` unidades de raio), em
**46 documentos**; **48** são `null` explícito. A nota é fechada em raio
**positivo**, porque *"o autor deixou desligado" tem de continuar distinguível de
"não está desenhado"*.

### 37.4. O `opaque:` e o bit 20 — a pergunta barata foi feita, e a resposta é NÃO

O laudo da superfície fechou apontando **um** candidato com endereço: o
`opaque:1` dos dois sítios acende o **bit 20** do estado de render da última
passada, e *"é a única coisa em toda a cadeia que poderia impedir um desfoque de
amolecer uma silhueta"*. Foi seguido, e **a hipótese está refutada**.

`[BIN]` **O bit 20 é o bit 4 de `fill_state`, e os campos têm nome no próprio
binário.** `RB::FormattedRenderState::description()` (`RenderBox 0x13365C`) monta
um dicionário cujas chaves `CFString` nomeiam os campos: `function` nos bits 0..5
(`0x1336E8`), `coverage_state` em 6..15 (`0x1337DC`) e `fill_state` em 16..31
(`0x13380C`). E `RenderState::name()` (`0x1328B0`) indexa a tabela de 39 nomes de
`0x1900D8` com os bits 0..5, o que faz da constante `0x03C0001E` de `0x100004` a
função `0x1E` = **`filter_blur`**.

`[BIN]` **E ninguém lê esse bit na CPU, para esta função.** Auditados todos os
acessores: `dest_write_mask` (`0x132DA8`) lê 22..25; `reads_destination` e
`reads_coverage` (`0x132AD0`/`0x132B80`) testam `fill_state & 0xF == 9`;
`reads_noise` (`0x132BBC`) lê o bit 21 pelo caso 15 de `0x16266C`; `reads_tables`
é constante 0 no caso 27; `uses_shader_blending` retorna antes de olhar bit
algum. E a varredura do `__text` inteiro **não acha um único `tbz`/`tbnz #20`** —
o único `ubfx #20,#1` é o `reads_noise` do caso 7, que é
`filter_color`/`filter_custom`. **O destino do bit é a GPU:**
`make_render_pipeline_descriptor` (`0xD8560`) empacota os 64 bits mais a palavra
derivada e chama `setConstantValue:type:atIndex:` com `MTLDataTypeUInt4` no
índice 0 (`0xD86E8`–`0xD870C`), **selecionando uma variante compilada do shader**.
`[OBS]` Esse metallib não está em corte nenhum do dump.

`[BIN]` **Mas o `opaque:` tem um SEGUNDO caminho, e esse é legível — e decide.**
O mesmo bit vira o `OptionSet<Filter::Flag>` final de
`RenderGroup::add_multipass_renderer` (`0xFEE60`), é gravado em
`MultipassInfo+0x85` (`0x105EE0`, **o único escritor daquele deslocamento em todo
o `__text`**) e lido por `resolve_unary_subgroup` em exatamente **duas** formas,
as duas sobre **alfa**: pula `resolve_srgb_alpha()` (`0x107974`) e passa
`!opaque` ao `bool` final de `color_convert(…)` (`0x10756C`). E **não encosta em
geometria em lugar nenhum**: `adjust_roi` (`0xFEB04`), `roi`/`dod` (`0xFEB58`) e
`layer_scale` (`0xFE6E4`) não leem o bit 0 de `+0x18`.

> `[BIN]` **`opaque:1` quer dizer "este conteúdo não tem alfa que valha
> resolver".** Ele não restringe a região amostrada, não deixa o compositor pular
> a leitura do que está atrás, e **não pode** ser o que faz um desfoque de fundo
> conviver com uma silhueta nítida. A hipótese que a missão mandou testar — e
> explicitamente **não** confirmar — está refutada.

**E o frame caiu junto, por uma porta que ninguém tinha usado.** A frente da
superfície tentou seguir o `descritor+0x70` pelo **código**; o caminho que
funciona é o **metadado de reflexão**, o mesmo que nomeou o descritor no §36.4.
`[BIN]` **`descritor+0x70` é `FinalizedIcon.Layer.effectsFrame`, um `CGRect?`** —
e a tag dele em `+0x90` é **o portão de todo o efeito**:

```
0x4A31C  ldrb w25, [x0, #0x90]      ; a TAG de effectsFrame
0x4A338  tbz  w21, #9,  0x4A34C     ; flag limpa -> desenho simples
0x4A344  ccmp w25, #1, #4, ne
0x4A348  b.ne 0x4A3F0               ; efeitos SO se tag != 1
```

e `0x4A3F0` é onde os quatro `double` do retângulo entram em `d12/d11/d10/d9` —
**os mesmos registradores** que a aritmética de recorte da parede (2) entrega a
`CGRectGetMinX/MinY/Width/Height`. A ramificação em
`blurStrength`/`refractionStrength` mora **abaixo** desse desvio.

> `[BIN]` **Com `effectsFrame == nil` o alvo cai em `0x4A3AC`, desenha o conteúdo
> do grupo e retorna SEM ABRIR CAMADA NENHUMA.** O `blur-material` inteiro está
> atrás desse portão.
>
> **O que isso muda é o default honesto, e é a entrega desta leitura.** A frente
> da superfície supôs que o frame ausente valia "o canvas inteiro" e desenhou. O
> alvo, sem frame, **não desenha**. **O retângulo unitário não era a leitura
> conservadora do frame — era a leitura errada de um ramo que nem roda.** O
> gabarito tinha recusado o número certo pela razão errada.

`[OBS]` Quem calcula o `effectsFrame` continua sem leitura, e **não é chave de
documento**: `[ART]` uma varredura dos **146** bundles acha **zero** ocorrências
de `effects-frame`/`effectsFrame` e exatamente **uma** chave `"frame"` em tudo.
Ele é produzido pelo finalizador, em Swift, sem símbolo.

### 37.5. E o `blur-material` morre como suspeito do aro, pelo mesmo instrumento

Os outros dois candidatos do aro escuro do ápice tinham sido eliminados pelo
**perfil de luma por profundidade**; o `blur-material` tinha sido eliminado pelo
**erro médio de canal na imagem inteira**. Instrumentos diferentes, e o segundo é
o que responde a pergunta do aro. Ele foi rodado — com o efeito atrás de uma
variável de ambiente, revertida antes do commit.

Aferição primeiro: com o render de controle o instrumento devolve, para
`Apple − nós`, `−63,99 / −47,06 / +0,29 / +47,16 / +43,66 / +35,52 / +27,54 /
+15,85` contra os `−60,5 / −62,0 / −7,3 / +46,0 / +44,4 / +35,8 / +28,0 / +16,9`
do laudo da refração. **De 5 unidades em diante as duas séries batem dentro de 1
luma**; as duas primeiras divergem porque ali o perfil é dominado pelo degrau da
borda e o render mudou desde então. **O instrumento está reproduzido.**

| profundidade (un.) | 0,0 | 2,5 | 5,0 | 7,5 | 9,9 | 12,4 | 14,9 | 17,4 |
|---|---|---|---|---|---|---|---|---|
| **o que o gabarito pede** | **−63,99** | **−47,06** | +0,29 | **+47,16** | +43,66 | +35,52 | +27,54 | +15,85 |
| `blur-material` desenhado | −86,97 | −90,23 | −68,98 | −52,19 | −47,17 | −43,08 | −39,44 | −35,97 |

`[BIN]` (medida) **Morre com sobra, por três razões independentes, cada uma
bastando sozinha:**

1. **A forma está errada.** O gabarito pede uma feição **local** — escuro nas duas
   primeiras profundidades, cruzando o zero em ~5 unidades, e então **claro** e
   decaindo. O desfoque escurece em **todas** as profundidades, monotonicamente
   em módulo, e **nunca vira**. Logo é *blanket*, não aro.
2. **O sinal está errado onde mais importa.** De 5 unidades em diante o gabarito
   nos quer mais claros (+47 a +16) e o desfoque nos deixa mais escuros (−52 a
   −36): errado por **83 a 52 luma em mais da metade do perfil**.
3. **A amplitude está errada mesmo onde o sinal acerta.** No aro ele dá `−87,0` e
   `−90,2` onde se pede `−64,0` e `−47,1`.

E **os dois extremos do espaço de frames fecham também**, sem inventar número: um
frame que **exclui** o ápice não move o perfil ali (delta zero, nenhum aro); um
frame que **inclui** o ápice reproduz a forma da tabela, porque um recorte só
restringe **onde** o passa-baixa age, não o que ele faz onde age. `[OBS]` Frames
intermediários, cuja borda cai dentro da faixa de 0 a 17 unidades, não foram
medidos — mas são justamente os que `effectsFrame` **não pode ser** sem que a
borda do recorte apareça como artefato.

> **Dos três suspeitos do aro, os três estão agora mortos pelo mesmo instrumento,
> e o aro segue SEM DONO.** Nenhuma destas frentes inventou um quarto candidato,
> e essa contenção é deliberada: um quarto nome sem medida transformaria uma
> pergunta aberta num palpite documentado.

### 37.6. O que o desfoque deixou aberto

| `[OBS]` | onde |
|---|---|
| **Quem escreve o `effectsFrame`.** Não é chave de documento (`[ART]` zero nos 146 bundles); o finalizador o calcula em Swift, sem símbolo | `opaque-bit20` §2.4 |
| **O metallib do `IconRendering` não está em corte nenhum do dump**, o que impede ver a variante de shader que o bit 20 seleciona | `opaque-bit20` §5 |
| Os **dois bits de qualidade** de `GaussianBlur` (`flags >> 4 & 3`): o que cada valor faz está lido (σmax `3,5`/`5,25`/`7,0`, default `5,25`); **quem os escreve** não foi seguido | `desfoque` §7.2 |
| Se a escada **recorre** no alvo; os filtros de reamostragem; `2,1σ` contra `2,8σ` por passada no caminho de GPU | `desfoque-escada` §6.1–§6.3 |
| `render_variable` (`0xFEEB0`) e `addVariableBlurFilterWithRadius:mask:` (`0x3EB74`) não lidos — **nenhum caminho do `IconRendering` deste corpus os alcança** | `desfoque` §7.4 |
| Os `465 ms` que sobram no desfoque já **não são o kernel**: são as passadas de resolução cheia que a escada ainda paga, sobre 4 M de floats por sombra | `desfoque-escada` §5 |

---

## 38. Os realces: um bit acende cinco, e o alvo não soma branco — ele filtra

Seis frentes num dia, e elas formam uma cadeia única: **portão → dados →
escalares → cor → composição → forma**. Cada uma fecha um `[OBS]` da anterior e
abre os seus.

O arco vale ser dito de uma vez, porque ele é a melhor coisa que este documento
tem para ensinar: achou-se o portão; depois os dados; depois **provou-se que os
três escalares suspeitos eram todos identidade** — dois como teorema, não como
medida —, o que não resolveu absolutamente nada; descobriu-se então que o
problema nunca foi intensidade e sim **cor**; ligou-se a cor e a quantidade de
luz passou a bater com a Apple (viés de `+34,32` para `+0,10`); e aí a medida
revelou que **a forma ainda está errada**, `7,5 ± 0,9` unidades fora de lugar, e
a frente terminou **sem aplicar o conserto que funcionava**, porque ele não tinha
leitura.

### 38.1. O portão, e o destino

`[BIN]` `hasSpecular` **não é "transporte sem aritmética"**. Ele não sofre
aritmética porque é um **portão**, e o portão foi encontrado: a função
`0x491C0`–`0x49DBC`, **3.068 bytes**, testa-o na terceira instrução e retorna sem
desenhar nada se ele for falso.

```
0x00049200   ldrb w8, [x0]         ; material.hasSpecular, descritor +0x00
0x00049204   cmp  w8, #1
0x00049208   b.ne #0x49cb8         ; FALSO NAO DESENHA NADA
0x00049210   ldrb w8, [x20, #0x21] ; e um SEGUNDO portao, do contexto
0x0004922C   ldrb w20, [x0, #0x30] ; material.specularPlacement
```

`[BIN]` E o destino é o shader **`glassHighlight`** do metallib do próprio
`IconRendering`, montado **por nome literal**: `0xE92C`–`0xE948` soletra a *small
string* de Swift `"glassHighlight"` por `mov`+`movk`, `0xE960` chama
`-[RBShader initWithLibrary:function:]`, dez `setArgumentBytes:atIndex:` carregam
os argumentos e `0xED00` faz o `drawShape:`. **Não é `CAFilter`, não é
`addBlurFilterWithRadius:`, não é um `drawShape:` cru como a sombra, e não é o
estilo `glass-highlight` do RenderBox** — a nota do §29.5 continua valendo. É a
**primeira das sete peças** do §2 deste documento, *"e o nome dela sempre disse o
que ela era"*.

A hipótese que abriu a frente era a notícia ruim possível — que o especular
desembocasse no `glassBackground_v1`, cujos 75 slots exigem o QuartzCore, que não
está em `References/`. **Não é o caso, e essa era a notícia boa:** o shader é do
`IconRendering`, está no `References/`, e a parede real está do lado de cá, com
tamanho e endereço.

**Os três valores de `specularPlacement`, medidos.** `[BIN]` O enum colapsa num
bit (`0x4926C`–`0x492CC`), e o bit é gasto em **um lugar só**
(`0x495D8`–`0x495F0`):

| | intervalo de distância | curvatura | opacidade |
|---|---|---|---|
| bit 1 — **`inside`** | `[inset, inset + height]` | `curvature` | `settings.opacity` |
| bit 0 — **`outside`** | `[inset − height, inset]` | **`0`** | `constraints.outsetOpacity` |

**Mesma espessura, mesma âncora, espelhados em torno dela.** Os dois intervalos
encostam em `inset` e não se sobrepõem; `curvature = 0` faz `shade == 1.0`
identicamente, então a faixa de fora é **chapada** e a de dentro **decai com a
profundidade**. `[ART]` E a UI da Apple diz a mesma coisa em inglês: *"Choose how
highlights align with each layer, either inside or outside, or let Icon Composer
decide automatically."*

> **E o bit é METADE da decisão.** `[BIN]` `0x494E0`–`0x494FC` é literalmente
> `resultado = (outsetOpacity_tag == 1) ? 1 : (bit | ¬isDarklight)`. Duas
> condições, as duas **forçando `inside`**, e **nenhuma das duas vem do
> documento**:
>
> 1. **`isDarklight == false` força `inside`.** Só o realce **escuro** pode sair
>    para fora — corroborado por um segundo caminho: o ramo que lê o mesmo byte em
>    `0x49A3C` escolhe `blendMode = 2` (`multiply`) quando ele é não-zero e
>    `blendMode = 6` (`screen`) quando é zero. **Um realce que multiplica é
>    escuro; um que faz *screen* é claro.** Nome, offset e mescla concordam.
> 2. **`outsetOpacity == nil` força `inside`** — a opacidade de *outset* é
>    **para** o caso de fora; sem ela não há com que desenhar fora.
>
> A hipótese de entrada era *"um enum denso de 3 que colapsa num bit é suspeito:
> ou dois casos são o mesmo, ou o bit é só metade da decisão"*. Caiu na **segunda**
> alternativa. **Os três casos NÃO são dois.** E `automatic` segue o estado de
> recoloração identidade **para `outside`** — direção **oposta** à do portão de
> forma idêntica na sombra (§36.3), onde sair da identidade força `neutral`. Duas
> frentes, o mesmo formato de portão, **sinais contrários**.

`[ART]` O corpus: **67 de 145** documentos pedem esse brilho, em 103 valores — 64
`true`, 36 `false`, **3 `"inside"` e ZERO `"outside"`**. Fechar `Highlights` vale
por 67 documentos; fechar a distinção `inside`/`outside` a partir do documento
vale, hoje, **por três**.

### 38.2. Os 16.113 bytes, e por que eles abriram

`ICRRenderingParameters.Highlights` (`params+0x250`) é um bloco de `0x3EF1` =
**16.113 bytes**, construído por 4.516 bytes de código em `0x62A78`–`0x63C1C`. A
frente do especular parou aí e **disse que parou**. A frente seguinte abriu — e
abriu porque **o próprio binário soletra o layout em IMEDIATOS e não em constantes
de *pool***.

`[BIN]` Preâmbulo de `0x118` mais **dez `HighlightsSet`** de passo `0x630`
(tamanho `0x629`, o último sem enchimento):

```
0x118 + 9*0x630 + 0x629 == 0x3EF1        <- fecha exatamente
```

E a aritmética não precisa ser acreditada, porque **as dez constantes estão
escritas**: `0x627B4` (glifo) e `0x62588` (chiclet) calculam os dez ponteiros por
soma de imediato — `mov w22,#0x3298`, `mov w23,#0x38c8`, `add x1,x20,#0x118` — com
`mov w2,#0x629` repetido nos dois `memcpy`. **Dez ponteiros por imediato, um
tamanho repetido em dois sítios, e um total que bate com a alocação: é o binário
concordando consigo mesmo por três caminhos.**

`[BIN]` Um `HighlightsSet` são seis `HighlightSettings` de passo `0x108`
(`keySharp`, `keyDiffuse`, `fillSharp`, `fillDiffuse`, `dark`, `rim`), e cada
`HighlightSettings` são dez campos em `0x101` bytes: `brightness`, `opacity`,
`outsetOpacity?`, `distance`, `minDistancePixels`, `inset`, `minInsetPixels?`,
`spread`, `bias`, `blendModeOverride?`.

> ### Um byte, três tabelas de tag
>
> `[BIN]` O byte `+0x100` é **ao mesmo tempo** o `blendModeOverride` e o
> discriminador de dois `Optional` externos. Três leitores, um byte, zero
> armazenamento extra:
>
> | leitor | sentinela | significado |
> |---|---|---|
> | `0x35450` | `max(0, byte − 18)` | `byte == 19` → `HighlightSettings? == nil` |
> | `0x33F04` | `max(0, byte − 19)` | `byte == 19` → `FillHighlights.matchKey` |
> | `0x35470` | `max(0, byte − 20)` | `byte == 20` → `FillHighlights? == nil` |
>
> `byte == 18` é `blendModeOverride == nil` com o settings presente, e é o que
> **todos** os doze blocos escrevem. **Um leitor que tome o byte como o modo de
> mescla lê `18` e vai à tabela de 18 do §17.3 procurar o índice 18, que não
> existe.** É o mesmo truque de *extra inhabitants* que a sombra encontrou no
> `ringWidth`, levado a três níveis.

### 38.3. `hasSpecular` acende CINCO realces, não um

Este é o achado que muda a figura mental, e ele derruba a formulação da própria
pergunta: o `[OBS]` que a frente foi fechar falava em *"os nove valores que esse
shader consome"*, no singular.

`[BIN]` `0x30E88` (1.516 bytes) expande um `HighlightsSet` em **sete** candidatos,
passo `0x138`:

| i | ajustes | `angleFromKey` | `isDarklight` |
|---|---|---|---|
| 0 | `keySharp` | `0` | não |
| 1 | `keyDiffuse` | `0` | não |
| 2 | `fillSharp` (`matchKey` → `keySharp`) | `+π` | não |
| 3 | `fillDiffuse` (`matchKey` → `keyDiffuse`) | `+π` | não |
| 4 | `dark` | `+π/2` | **sim** |
| 5 | `dark` | `−π/2` | **sim** |
| 6 | `rim` | `0` | não |

e `0x31338`–`0x313EC` **descarta** todo aquele cujos ajustes eram `nil`. Com os
padrões de glifo desta versão, `fillDiffuse` e `rim` são `nil`. **Sobram cinco:**
o aro nítido da luz-chave, a lavagem difusa, o aro de preenchimento a 180°, e o
**escuro desenhado duas vezes, espelhado a ±90°**.

> Isso explica **de dentro** o que a frente anterior tinha medido sem saber o que
> contava: o array externo de passo `0x58` e o interno de passo `0x60` não eram
> dois níveis de configuração — são a **passagem** (`HighlightsPass`) e os
> **realces** que ela resolve, agrupados em `0x4C314` por tudo menos a direção.

`[BIN]` E a **resolução** (`0x4BD90`, 1.412 bytes) é onde os dez campos viram os
nove argumentos do shader:

```
k         = ctx[0x469F]                                    ; classe de tamanho, 0..3
v[k]      = SizeBasedValue.slots[3 - k]                    ; 0x4BEB0-0x4BEF4
height    = max(distance[k], minDistancePixels[k] * ctx[0x46A8])
inset     = max(inset[k],    minInsetPixels[k]    * ctx[0x46A8])
            minInsetPixels == nil alimenta -INFINITO ali   ; 0x4BF20
theta     = angleFromKey + lightLongitude
direction = (cos(phi)*sin(theta), cos(phi)*cos(theta), sin(phi))
opacity   = ctx[0] * opacity[k]
color     = (brightness, brightness, brightness, 1.0)
blendMode = blendModeOverride ?? (brightness < 0.5 ? 4 : 8)
```

`[BIN]` O `csel` do `blendMode` dá **4** se `brightness < 0.5` e **8** senão. Em
`Source/RenderBox/BlendMode.h`, escrito meses antes a partir do RenderBox:
`PlusDarker = 4, PlusLighter = 8`. **Um realce escuro subtrai e um claro soma, e
ninguém teve de escolher isso** — controle cruzado que ninguém armou.

> **E aqui a armadilha da inversão de índice FINALMENTE aparece no pixel.** Nos
> cinco campos de quatro do `Shadow` os quatro números são iguais e ler a tabela
> ao contrário não move um pixel (§36.2). No `Highlights` eles **diferem**:
> `distance` vale `4` em `display` e `6` nas outras três, e `opacity` cai para
> `0,3` só em `small`. **Ler a tabela ao contrário aqui troca o ícone de 1024 px
> com o de 16 px, e isso é visível.** O terceiro sítio da mesma escada de quatro
> vias está em `0x4BEB0`–`0x4BEF4` — a sombra mediu em `0x49FA4`, a translucidez
> no `TranslucencyEffect.strength`.

**E a armadilha silenciosa desta família**, que merece o destaque porque a falha
dela se parece exatamente com "não implementado": `[BIN]` **`spread' =
cos(spread)`**, com sentinela **`−1000.0f`** quando `spread > π`
(`0xEE3C`–`0xEE54` compara, `0xEF50`–`0xEF5C` escolhe). O corpo do shader é
`lit = saturate((dot(direction, n) − spread') / max(1 − spread', 2⁻¹⁰))`, e `dot`
vive em `[−1, 1]`. **Com um radiano em `spread'` — `π/2 ≈ 1,571` — `dot − spread'`
é negativo em todo pixel e `lit` é identicamente zero.** Com o cosseno, `π/2 → 0`
é um hemisfério, `π/3 → 0,5` é um cone de 60°, e `π → −1000` é "sempre aceso".
Um transcritor que perdesse o `cos` teria um especular que **compila, roda, não
emite aviso nenhum e não desenha nada**.

`[BIN]` Mais duas menores: `bias' = 1/bias − 2` (`0,5` é o neutro), e a direção
chega ao shader como o `float2` **`(x, −y)`** — é por isso que o `+y` da luz é o
`−y` da imagem, e é por isso que a chave (`angleFromKey = 0`) acende o **topo**.

**O especular passou a desenhar:** 67.285 de 1.048.576 pixels no Apollo (6,42 %),
Δ máx 255. O capacete ganhou aro claro no topo e sombreamento nas laterais, o
anel do visor deixou de ser faixa chapada e lê como toro de vidro, e as três
antenas ganharam os gomos que a arte desenha.

### 38.4. As três identidades — provadas como teorema, e não resolveram nada

O pedido do usuário era o mesmo nos três laudos seguintes: *"o efeito atual tá
duro demais, muito forte, falta ajuste e polimento baseado em funções reais"*. A
hipótese natural é que algum escalar tivesse sido tomado como `1` por engano.
**Ela caiu, e é a queda mais importante da cadeia.**

**(1) `ctx[0]` é `GlobalConfiguration.lightIntensity`, e vale `1.0`.** `[BIN]` O
método foi **não procurar a constante, procurar a base**: `0x42B28` põe a base do
contexto em `sp+0x480`, e o primeiro `0x68` dela é copiado do **quinto argumento**
de `0x4266C`, que o metadado (`fieldmd 0xA327C`, treze campos) identifica como
`GlobalConfiguration` — encaixe campo a campo fechando em `0x68` bytes exatos. É
lido num **único** sítio no slice inteiro (`0x4C010`) e escrito num só
(`0x42884`), e o valor `0x3ff0000000000000` está assado no init `lightAngle:`
(`0x35DF0`), lido por um segundo binário meses antes. *"Um escalar com um leitor
só é a melhor forma possível de erro — e não havia erro."*

E o encaixe resolve **duas** perguntas de uma vez: `ctx[0x20]`, que decide se
`phi` é zero, é a **tag do `Optional` de `customLightDirection`**. "`phi = 0`" não
é um estado inventado nem um ramo conveniente: **é `customLightDirection == nil`,
que é o estado de quem não pediu luz custom.** `[ART]` E nenhum documento pode
deixar de ser `nil` — `customLightDirection` é campo de `GlobalConfiguration`, não
do `.icon`; dos 145 documentos, 82 trazem a chave `lighting` e os únicos valores
são `individual` (64) e `combined` (18).

**(2) `0x12550` é a identidade para QUALQUER valor dos seis parâmetros.** `[BIN]`
Isso é teorema, não medida: `resolveHighlight` monta
`dir = (cos φ·sin θ, cos φ·cos θ, sin φ)`, logo `hypot(dir.x, dir.y) = |cos φ|`;
com `φ = 0` isso é `1`; e `sin` de qualquer ângulo é no máximo `1`, portanto
`1/sin(alignmentRange) ≥ 1` e `t = max(0, 1 − (algo ≥ 1)) == 0`. Tudo o mais
colapsa.

**(3) `phi` não pode mover um realce**, e isso é da própria passagem: `[BIN]` a
última reescrita de `0x12550` é **incondicional e sem parâmetro** — `atan2` joga
fora o comprimento e `str xzr,[x20,#0x18]` zera o `z`. **Não é que `phi` "seja
zero e por sorte não apareça": a passagem que roda depois apaga a latitude por
construção.** Um transcritor que guardasse o `cos(phi)` nas componentes planares
teria um realce que enfraquece com a luz alta, pelo motivo errado e sem aviso.

> **E o pixel dessas três é o resultado, não a decepção:** **10** pixels mudados
> de 987.176 no ícone do usuário (Δ máx 1) e **38** de 987.549 no Apollo. É
> exatamente o que "as três eram identidades" tem de parecer no pixel — se
> qualquer uma delas mordesse, o diferencial teria sido de dezenas de milhares.
> Os 10 e os 38 são só o joelho da banda andando `5·10⁻⁴`.

**Um único número da transcrição estava errado**, e saiu daí: `kBandWidth` era
`0,83349` — a decimal de onde a constante veio — e é **`0,8330078125`**, que é a
`half 0xH3AAA` decodificada, porque a multiplicação do shader é em `half`.

### 38.5. A resposta era a COR: o VCM é o BT.709, e o bit 0 é o fundo

`[BIN]` `0x494D8` lê `Highlights+0x90` = **`glyphHighlightsUseVCM`**, que já se
sabia ser **`true`**, e ele parte a função de desenho em duas. **Este renderizador
estava no ramo `false`**, cuja única escala é `glyphHighlightNonVCMScale = 1.0`.
**Força cheia, sem polimento — que é, palavra por palavra, o que o usuário
descreveu.**

O ramo que o alvo toma **não desenha o shader por cima de nada**:

```
save                                       0x8F2C0
beginLayer                                 0x8E460
  <o glassHighlight, DENTRO da camada>     0xE834
clipLayerWithAlpha:1.0 mode:0              0x8E620   -> a forma vira RECORTE
[addContentHeadroom: / addStyle:9]         PULADOS, VCM[4] == 1
addColorMatrixFilterWithArray:flags:0      0x8E260
beginLayerWithFlags:1                      0x8E480
drawLayerWithAlpha:1.0 blendMode:0         0x8E860
restore                                    0x8F2A0
```

**Não há `drawShape:` nenhum neste ramo.** A cor e o `blendMode` do desenho da
forma **não chegam ao pixel** — só a cobertura.

**`VCM` não era uma sigla opaca. É `Video Color Matrix`, literalmente.** `[BIN]`
`0xE2960` é o **BT.709 RGB→YCbCr** de faixa cheia, com Cb/Cr enviesados em `0,5`;
a inversa está ao lado, em `0xE2910`, com os `1,5748 / −0,1873 / −0,4681 / 1,8556`
que qualquer tabela de BT.709 traz e cada viés valendo exatamente `−0,5 ×` o
coeficiente de croma da linha. **As duas se invertem numericamente**, conferido
sobre quatro cores. A parede anterior era `__common` — zerado no arquivo —, e o
que a derrubou foi notar que `0x49C78` é o **trampolim** de `swift_once` e não o
corpo: o corpo (`0x6948`) não calcula nada, **copia do `__const`**.

`[BIN]` Entre a ida e a volta ficam duas matrizes, e a segunda é a surpresa:

- **níveis** (`0x49A64`): `Y ← (VCM[1] − VCM[0])·Y + VCM[0]`; croma e alfa
  intactos.
- **croma** (`0x49B18`): a linha do `Y` é `[1,0,0,0,0]` — **`Y` não é tocado**.
  Quem é multiplicado são as cromas: `Cb,Cr ← VCM[2]·c + (0,5 − 0,5·VCM[2])`.
  **`VCM[2]` é SATURAÇÃO, não contraste de luma**, e isso **corrigiu a leitura do
  laudo anterior**.

Com `glyphHighlightVCM = [0,2, 1,2, 1,25, 0,0, true]`: *levantar a luma em `0,2`
com ganho `1,0` e abrir a croma em `1,25`*. Com
`glyphDarklightVCM = [−0,15, 0,7, 1,25, 0,0]`: `Y ← 0,85·Y − 0,15`, mesma
abertura.

> **Nem um nem outro soma branco.** É por isso que o realce do alvo lê como vidro
> e o daqui lia como cromado: **aqui se somava luz branca por cima, lá se levanta
> a luz do que já está embaixo e se guarda a cor dele.**

**E a última perna era um bit.** Uma matriz de cor sem fonte produziria a cor
constante do viés; para a sequência fazer sentido, o `beginLayerWithFlags:1` tem
de instanciar a variante de **fundo** do filtro. `[BIN]` O bit foi lido:
`-[RBDisplayList beginLayerWithFlags:]` (`0x3BCA0`) passa o `1` intacto pela
máscara `0x7B`; `Builder::begin_layer` (`0xC9A28`) o leva a `Layer::Layer`
(`0x14DB74`), que grava as flags em **`Layer+0x44`**; e
`Builder::null_style_draw` (`0xCDD80`) testa o bit 0 em `0xCE02C`, pega o filtro
de `Layer+0x18`, chama o slot `+0x70` da vtable — **`make_backdrop_item`**,
confirmado entrada a entrada — e pendura um
`BackdropFilterItem<Filter::ColorMatrix>` **na camada PAI**. O mesmo bit também
**bloqueia a fusão em linha** (`0xCDEF0`).

> **Em uma frase:** o bit 0 de `Layer::Flag` transforma o filtro instalado na
> camada num item que filtra **o que já está na camada pai**. A matriz de cor do
> realce **lê o fundo**.

Com isso o realce passou a ser pintado como o alvo pinta —
**`lerp(fundo, VCM(fundo), cobertura)`** —, e pela primeira vez houve gabarito
para medir. Só na banda do realce (13.498 px):

| | Δ médio | viés (nosso − Apple) |
|---|---|---|
| antes (branco em `plusLighter`) | 46,19 | **+34,32** |
| **depois (VCM)** | **23,42** | **+0,10** |
| controle, sem realce | 19,78 | −10,74 |

`[BIN]` **O "forte demais" era real e está medido:** o branco deixava a banda 34
níveis mais clara que a Apple. O VCM leva o viés a `+0,10`, e o controle mostra
que a Apple **tem** realce ali — sem ele a banda fica 10,7 níveis escura demais.
**A quantidade de luz agora bate.** `[OBS]` **A distribuição ainda não**: o erro
absoluto (23,4) segue acima do controle (19,8). A luz está na conta certa e no
lugar errado.

### 38.6. O grampo `clampedPlusL` — transcrito com fórmula, e desligado

A frente do VCM também abriu o `shouldClampPlusLBlending`, e **este é o exemplar
do padrão `FECHOU E DECLAROU`**.

`[BIN]` O campo é `params+0x220` e vale **`true`** (`0x5EAE8` com `w22 = 1`),
ancorado externamente: o campo vizinho `+0x228` é o `266,24` que este documento já
lia meses antes. O nome do shader **não está no `__cstring`** — está soletrado por
`mov`/`movk` em `0xD88C`/`0xD89C` como *small string* de Swift: **`clampedPlusL`**.
É a lição dos imediatos outra vez, e desta vez o que ela escondia era um **nome**.

`[BIN]` E é um entry point Metal do mesmo bundle (`default_mod8.ll:37`), com os
operandos **nomeados pelo metadado AIR** (`!"source"`, `!"dest"`):

```
max(dest, (min(1, source+dest).rgb, saturate(source.a+dest.a)))
```

Três consequências, e a terceira é a que um "clamp to 1" escrito pelo nome
erraria: o teto de RGB é o **`1.0` literal** e não o alfa de saída; uma pilha de
realces claros para em **branco** em vez de correr para 3 ou 4; e o `fmax` contra
o fundo faz o grampo **tirar o excesso sem tirar luz** — um fundo já acima de 1
não é puxado para baixo.

> ### A tentação recusada, e o zero que a denunciou
>
> A primeira versão daquela frente **ligou** o grampo no `drawSpecular`. Ele mexeu
> em **91.276 pixels do Apollo com delta 118** — número bonito, visível, do
> tamanho que o pedido queria. Também mexeu em **zero** pixels do ícone do usuário
> pelo caminho do glifo, e quebrou três testes que dependiam do transbordo.
>
> **Foi o zero que fez olhar de novo.** `[BIN]` A troca de shader acontece em
> quatro lugares — `0x44654`, `0x44908`, `0x4B57C`, `0x4B7F4` —, e **o desenho do
> especular do glifo não chama nenhum deles**: `0x491C0`–`0x49DBC` entrega o
> `blendMode` direto ao `drawShape:` de `0xED00`, sem um `cmp #8` nem um
> `setBlendShader:` no caminho.
>
> **O grampo está transcrito, fixado por quatro asserções, e DESLIGADO**
> (`SpecularArguments::clampPlusLighter = false`), com a razão escrita na linha e
> o campo guardado para a frente que identificar os desenhos cobertos. `[BIN]` O
> `plusDarker` não recebe nada: o portão é `cmp w24, #8` e mais nada.

### 38.7. A forma: `7,5 ± 0,9` unidades, e o número que NÃO foi aplicado

Com a cor certa, a medida revelou o que a média global escondia. O instrumento
que achou foi novo e é o motivo de a frente existir: **um perfil por
PROFUNDIDADE e por SETOR ANGULAR** — *"uma média global não distingue 'a banda
está fraca' de 'a banda está no lugar errado'; este perfil distingue"*.

| profundidade (un.) | 0 | 2,5 | 5,0 | 7,5 | 9,9 | 12,4 | 14,9 | 17,4 | 19,9 |
|---|---|---|---|---|---|---|---|---|---|
| **Apple − sem realce** | −7 | −9 | +23 | **+52** | +49 | +41 | +30 | +18 | +6 |
| **nós** | +34 | **+56** | +27 | +8 | +6 | +4 | +3 | +2 | +1 |

`[BIN]` (medida) **A banda da Apple não é mais fraca nem mais forte que a nossa:
ela está mais FUNDA.** O pico dela está em ~9 unidades e o nosso em ~2,5; a dela
ainda vale +30 onde a nossa já acabou; e os ~5 primeiros unidades — **o aro** —
são mais **escuros** que o interior na saída da Apple, enquanto na nossa são o
pico do brilho. O mesmo formato nos oito setores, nas duas bordas medidas à mão,
**e no rendition de 256 px**: o aro tem ~1 px e o pico cai a ~2 px, que são as
mesmas ~5 e ~9 **unidades de canvas**. **Não é artefato de pixel; é geometria.**

**Os cinco suspeitos, isolados um a um contra o gabarito** (Δ médio na banda de
13.498 px; `base` = 23,42; controle sem realce = 19,78):

| suspeito | como foi isolado | Δ médio | veredito |
|---|---|---|---|
| semente sub-texel do campo | `subpixelSeed = false` | **25,04** | **piora**; fica ligada |
| classe de tamanho | `small`/`medium`/`display` | 19,78 / 22,91 / 24,35 | nenhuma move a banda |
| escala de `height`/`inset` | `pixelsPerPoint = 0,5` | 23,25 | ±0,2: a unidade não é o erro |
| dobra dentro/fora | `placement = outside` | 24,13 | piora |
| `spatialHighlighting` | — | — | **identidade provada** (§38.4) |
| sinal do gradiente | normal invertida | **25,74** | piora: o sinal de hoje é o certo |
| idem, as outras 6 simetrias | `x↔y`, `−x`, `−y`… | 23,39 … 23,50 | nenhuma melhora |
| **diagnóstico: `sd` 3 px mais fundo** | não é conserto | **17,60** | **única coisa que passa do controle** |

**Por que os cinco não podiam funcionar:** eles mexem em **intensidade, direção
ou largura**, e o erro é de **posição**. As simetrias da normal só trocam QUAL
borda acende, e **a Apple acende todas as bordas na mesma profundidade**.

Quatro dos cinco ficaram fechados **por leitura de binário e não por diff**: a
unidade (`ctx+0x46A8` lido em `0x42D3C`–`0x42D50` como
`(1/escala)/contentsScale`), a classe de tamanho (os três limiares são `25,0`,
`60,0` e `256,0`, e o gabarito é `large`), o `inset` (zero, relido da fábrica
`0x64604`, e nada acrescenta um termo depois — `0x4BD90` só faz os dois `max` e
`0xED94` só multiplica pela escala) e o `spatialHighlighting` (teorema, mais o
`[ART]` do corpus). O quinto, o sinal do gradiente, **o gabarito desempatou — e a
favor do que já estava lá**: a identidade é a melhor das oito simetrias e a
inversão pura é a pior. *"Este é o uso que o gabarito tem direito de ter: escolher
entre duas leituras possíveis, sem mexer em número nenhum."*

**O que sobra**, medido nos dois tamanhos do gabarito: deslizar `sd` para dentro
de `3,0`–`3,5 px` a 412 e de `1,5 px` a 206 — **`7,5 ± 0,9` unidades de canvas
nos dois** —, o que derruba o erro de `23,42` para `17,60`, **abaixo do controle
pela primeira vez**. E a leitura rival morreu numericamente: **escalar** `sd` em
vez de deslocá-lo piora (`×0,5` dá 23,63, `×0,4` dá 23,33). **O gabarito pede um
deslocamento, não um ganho.**

> **E não foi aplicado.** `[OBS]` Os dois lugares que poderiam carregá-lo são os
> dois que ninguém leu, e nenhum deles está nestes binários. Aplicar 7,5 seria
> **ajustar parâmetro até o diff fechar** — e no ícone do usuário o mesmo deslize
> move **91.301 px (9,25 %, Δ máx 101)** e **tira o realce de todo traço mais fino
> que 7,5 unidades**, que a arte de grunge dele tem aos montes.
>
> > Aplicá-lo seria **escolher número pelo diff, que é exatamente o que
> > destruiria o oráculo**.

### 38.8. O que os realces deixaram aberto

| `[OBS]` | onde |
|---|---|
| **A forma da banda**, `7,5 ± 0,9` unidades mais funda, medida em dois tamanhos e **sem `[BIN]`**. O candidato mais forte já foi eliminado — ver §40.5 | `realce-forma` §5 |
| A codificação do texel do SDF em `.g` e `.b`, que o shader lê como `1 − 2·gb` | `highlights` §7.5 |
| `ctx+0x21` (`0x475C8`), o **portão real** da cadeia do chiclet, sem nome no metadado | `chiclet-realces` §6.1 |
| `chicletClear`/`chicletScreened` (`+0x13A8`, `+0x19D8`) não transcritos, e as duas comparações de forma que escolhem entre eles | `chiclet-realces` §6.2 |
| A transformada-base do VCM (`0xE2960`, `__common`, init `0x49C78`) e a aritmética SIMD de `0x7064` | `realce-vcm` §4.3 |
| **Quais desenhos o grampo `clampedPlusL` cobre** — sabe-se que o especular do glifo **não** | `realce-vcm` §4.2 |
| `[INF]` `mode:0` como recorte por **alfa**: um recorte por luminância apagaria os dois escuros, cuja cor é preta — mas a tabela não foi lida. Ver a correção no §45.1, que derruba a leitura vizinha | `realce-vcm-fechado` §5.3 |
| Na franja com alfa < 1, se o `BackdropFilterItem` **substitui ou compõe** por cima | `realce-vcm-fechado` |

---

## 39. O chiclet: a forma, a curva, o raio que não multiplica, e o recuo que ninguém liga

`[BIN]` A forma que recorta o fundo é um **squircle** — retângulo de canto
**contínuo**, três cúbicas por canto —, o raio é `266,24` num espaço de desenho de
1024, o fator `×1,275` que parecia ameaçar a curva é **convenção de
armazenamento**, e o recuo de 824/1024 é um **modo** que o Icon Composer nunca
liga.

Cinco frentes. A primeira **recusou implementar** e disse por quê; a segunda
fechou a dúvida e ligou o recorte; a terceira mediu o canto contra o gabarito e
**não mexeu em número nenhum**; a quarta deu realces à pastilha; a quinta
respondeu quatro perguntas de uma vez e **corrigiu duas leituras deste próprio
documento**.

### 39.1. A família e o raio, por três testemunhos

`[BIN]` O `cornerStyle` que o `IconRendering` passa ao RenderBox é o **imediato
`1`**, no **único** sítio de `setRoundedRect:cornerRadius:cornerStyle:` do binário
inteiro (`0x7E010`). E `1` é o canto contínuo: `set_rounded_rect`
(`RenderBox 0x216F8`) tem um enum **fechado em `{0, 1}`** — qualquer outro valor
cai num `.cold` que aborta —, e o ramo do `1` é o único que multiplica por
`1,275` e marca **tipo de forma 4**. Depois, `Mapper::add_rounded_rect`
(`0x7F580`–`0x7FE58`) despacha pelo `RBPathElement`:

| ramo | por canto | total |
|---|---|---|
| **contínuo** (`0x7F664`–`0x7FC50`) | `lineto` + **3 `cubeto`** | 4 linhas + **12 cúbicas** |
| circular (`0x7FCB4`–`0x7FE54`) | `lineto` + **1 `cubeto`** | 4 linhas + 4 cúbicas |

`[BIN]` E os **dois** modelos convivem no mesmo binário: na mesma poça de
constantes, `0x15EBA0` guarda `0,5522847498` — o *kappa* do arco de círculo,
`4/3·(√2−1)` —, lido **fora** do ramo contínuo. O `cornerStyle` escolhe.

`[BIN]` **Confirmação independente, de outro binário:** o
`IconComposerFoundation` constrói a mesma forma pela SPI do AppKit, com o
argumento **no nome do seletor** — `_bezierPathWithRoundedRect:radius:
continuousCorners:` com `mov w2, #1`. Dois binários, duas implementações, a mesma
decisão.

**O raio, e a distinção que custou uma releitura.** Há **dois** caminhos, e eles
dizem coisas diferentes sobre a mesma constante:

- `[BIN]` No `IconComposerFoundation`, `Platform.cornerRadiusPercentage`
  (`0x386CC`) devolve `0,26` para as plataformas `{0,1}` e `0,5` para as demais, e
  `chicletBoundingPath` (`0x386F0`) **multiplica**: `1024 × 0,26 = 266,24` no
  squircle, ou uma **elipse** em `1088²` para o watchOS. O controle é limpo: a
  plataforma cuja percentagem é `0,5` é exatamente a cuja forma é elipse — e o
  watchOS é mesmo o ícone circular da Apple. Os três casos do enum saem do
  metadado nesta ordem: `iOS = 0, macOS = 1, watchOS = 2`.
- `[BIN]` **No `IconRendering` — que é o caminho que desenha — o `0,26` não
  multiplica nada.** O que existe é `266,24`, **absoluto**, nascido por
  `movz`+3×`movk` em `0x5EAFC` e consumido cru pelo `setRoundedRect:` de
  `0x7E01C`. Entre os dois **não há um único `fmul`/`fdiv`**, e a varredura de
  `adrp #0xCF000` + `#0x700` (o *field-offset* de
  `DefaultIconShape.cornerRadius`) acha exatamente dois sítios: **um escritor**
  (`0x42AA0`) e **um leitor** (`0x4FA00`).

> **E isso tem uma consequência que a primeira leitura deste documento não
> tinha.** Se o rect de desenho encolhe para 824, **o raio não encolhe junto**: o
> *witness* de `path(in:inset:)` (`0x4F9E8`) faz `CGRectInset` e passa
> `raio = cornerRadius − inset`, uma **subtração linear**. Rect 1024 com
> `inset = 100` dá raio `166,24` — e **não** `266,24 × 824/1024 = 214,24`.
>
> `[BIN]` **O `1024.0` escala o CANVAS, não o raio.** A materialização de
> `1024.0` no `__text` inteiro é **um sítio**, `0x42940`, dentro do bloco
> `0x4291C`–`0x42954` que renormaliza o tamanho do canvas. E
> `Source/RenderBox/ChicletShape.cpp:167` já faz `266,24 × size/1024`, que é
> exatamente a CTM certa: espaço de 1024, raio absoluto, ida ao raster por escala
> uniforme.
>
> `[BIN]` **Também não há `SizeBasedValue` no caminho do raio.** O elemento do
> dicionário de `parameters+0x248` tem *stride* 32, mas o que se lê dele é
> `payload@+0` e `tag@+8` com `cmp #1`: é um **`Optional<Double>`**, não os quatro
> slots de uma tabela por classe de tamanho. **A armadilha do `valor[3 − classe]`
> não se aplica ao raio** — ela vive em `0x4BEB0`, e é realce (§38.3). Uma lição
> boa vira superstição quando não tem fronteira; esta tem.

`[ART]` **E o raio nunca vem do documento.** Nos 145 documentos há **zero** chaves
de raio de canto — as duas ocorrências de `"corner"` são **nome de camada** do
autor, e as seis de `"squircle"` são **nome de arquivo SVG** de um único
documento. O que o formato escolhe é a **família**, e de forma binária:
`supported-platforms` separa `squares` (145 de 145) de `circles` (97, sempre
`["watchOS"]`). **O formato escolhe qual família; o binário fixa a geometria de
cada família.**

### 39.2. A curva, e a recusa que valeu mais que um canto

A primeira frente fechou a família e o raio e **parou**, porque a curva não
fechou. A razão era um número concreto e não uma hesitação: se o `×1,275` de
`set_rounded_rect` alcançasse o avaliador, o teste de folga daria

```
r' = 266,24 × 1,275 = 339,456
t  = (1024 − 678,912) / (678,912 × 0,5286649) = 0,9615  < 1
```

e o chiclet cairia no **regime de mistura** — visivelmente diferente de qualquer
squircle padrão. Se não alcançasse, `t > 1` e a curva é a canônica.

> **A recusa, literal:** *"Implementar agora seria escolher entre `t = 0,9615` e
> `t = 1,746` no chute, gravar a escolha em `Source/RenderBox/`, e produzir um
> canto **plausível** — que é exatamente o modo de falha que este repositório
> recusa, e o mesmo erro do filtro SVG: achar os átomos e presumir o desenho. O
> `[OBS]` acima vale mais que o canto errado."*
>
> E a frente **nomeou o experimento** em vez de deixar a pergunta vaga: *"ler o
> codificador que transforma o `RBShape` no `RBPathElement` 9 que `0x80DCC` lê.
> **Uma função, não uma campanha.**"*

**A frente seguinte executou o experimento, e a resposta veio com uma lição de
método junto.** `[BIN]` A contagem anterior — *"o recíproco é lido uma única
vez"* — **valia só para o *constant pool***. Varrendo o `__text` atrás do
**imediato `0x3F48C8C9`** (`0,7843137f`, materializado por `mov`+`movk`)
aparecem **mais três leitores**, e um deles é o codificador:

| função | endereço | o que faz |
|---|---|---|
| `Coverage::Primitive::set_globals` | `0x95A94` | **÷1,275** |
| `Coverage::Primitive::make_shadow` | `0x94EA4` | **×1,275** |
| `Coverage::Primitive::encode` | `0x94550` | **÷1,275** |
| `Coverage::Primitive::decode` | `0x94838` | **×1,275** |
| **`Coverage::Primitive::add_path`** | **`0x9682C`** | **÷1,275** |

**Todo leitor desfaz; todo escritor refaz.** Não sobra consumidor que veja o valor
multiplicado — que era exatamente a forma da dúvida.

`[BIN]` E a prova limpa é o `bsl` de `0x96840`, dentro de `add_path`, porque ela é
uma **assimetria no mesmo ramo**:

```
0x96820  cmeq v4.4s, v5.4s, v4.4s  ; (tipo == 3) ?
0x96824  mvn  v4.16b, v4.16b       ; -> mascara (tipo == 4)
0x96838  fmul v5.4s, v2.4s, v5.4s  ; raios / 1.275
0x96840  bsl  v16.16b, v5.16b, v2.16b  ; tipo 4 -> divididos; tipo 3 -> CRUS
```

**O tipo 3 (circular) passa o raio verbatim; o tipo 4 (contínuo) divide.** A
assimetria é a prova de que o `×1,275` pertence ao tipo 4 e a mais nada.

Logo `t = 1,746 ≥ 1` e a curva é o **contínuo canônico**, com `extent =
1,5286649465560913`, `control = 1,0884900093078613` e `shoulder =
0,8684070110321045`. **E as duas poças de constantes se validam uma à outra:** a
mistura reproduz o trio canônico em `t = 1` exatamente
(`1 + 0,528664947`, `0,96 + 0,128490031`, `0,82 + 0,0484070182`).

**Achado de brinde, que só aparece quando se lê a função inteira:** `[BIN]` a
folga `t` é **por aresta**, e `0x7F7BC`–`0x7F824` a **recalcula entre a segunda e
a terceira cúbica do mesmo canto**. Cada canto usa a folga da aresta de
**entrada** nas duas primeiras cúbicas e a da aresta de **saída** na terceira.
Num quadrado de raios iguais não muda nada; num retângulo estreito, muda.

Com isso o recorte entrou: **6,0921 % do canvas**, todo nos quatro cantos
(`984.696,005 px²` de chiclet contra `1.048.576` de canvas). O canto come
`1,5286649 × 266,24 = 406,992 px` de cada aresta, sobrando `210,016 px` de aresta
reta entre dois cantos. E a ordem importa: `clipToChiclet` vem **depois** do
`paintBackground`, porque pintar o quadrado inteiro e então cortar mantém o
parâmetro da rampa mapeado ao canvas.

### 39.3. O recuo de 824/1024 é um MODO, e o app nunca o liga

`[BIN]` `0x4202C`–`0x4236C` é o método de `FinalizedIcon.Configuration` que
devolve o retângulo em que o ícone é desenhado, e ele despacha por
`useLegacyInsetting` (`Configuration+0x59`):

```
lado' = lado − 2 × piso(lado × (relativeIconInset ?? 100/1024))
```

com o `100/1024` sendo o imediato `0x3FB9000000000000` de `0x4224C` — e uma
varredura de todas as seções de dados de **onze** binários das duas versões acha
**uma ocorrência**. **Não há `0,8046875`, nem `824`, nem `185,4` em lugar
nenhum.**

`[BIN]` E o modo tem **três** escritores em todo o `__text`, o que basta para
saber quem o liga:

| escritor | o que grava |
|---|---|
| `0x14BC0`, no init `Configuration(icon:style:useLegacyInsetting:…)` | o argumento |
| `0x1A844`, no init `Configuration(icon:style:parametersOverride:)` | **`wzr` — zero** |
| `0x27EA4`, em `FinalizedIcon(serialized:device:)` | o que veio serializado |

O init com a bandeira **não tem chamador dentro do `IconRendering`**, e o
`IconComposerKit` importa **só o sem bandeira**. Nas outras fatias do bundle a
string `useLegacyInsetting` **nem aparece**.

> **O recuo não é regra do desenho: é um modo que só um cliente EXTERNO do
> `IconRendering` liga** — `[INF]` o compilador de assets que produziu as
> renditions legadas, que está fora do bundle.

E ele fecha um `[OBS]` vizinho de graça: o `rectB` cujo nome semântico tinha
ficado aberto na primeira frente **é este retângulo recuado**, o que faz do
default de `chicletDropShadow` (`min(ΔL,ΔA)/2 ≥ 1`) literalmente *"há margem para
a sombra"* — com o modo ligado há `100/1024` de margem, sem ele há zero.

`[BIN]` A fórmula bate **exatamente** na saída de 512 do gabarito — corpo de
`412,000` com bordas em `x = 50,000`, sem pixel parcial. `[OBS]` **As três do
`.icns` não batem**: bordas sub-pixel e larguras que a fórmula não produz com
nenhuma arredondação. A afirmação "824/1024 nos quatro tamanhos" do laudo do
oráculo deve ser lida como **"dentro de ±1 px"**, não como a fórmula.

**E o `chicletDropShadow` liga DUAS coisas, não uma.** `[BIN]`
`hasChicletShadow = (chicletDropShadow ?? folga) && drawMitigatedVersion`
(`0x42F00`–`0x42F2C`), com exatamente dois leitores: um **liga a passada de
sombra do chiclet** (`0x430D4` → `0x433D0`) e o outro **suprime o contorno**
(`0x47BC0`: `desenhaContorno = !hasChicletShadow || allowDespiteShadow`). O nome
`allowDespiteShadow` confirma que a exclusão é deliberada. **E por causa do
`&& drawMitigatedVersion`, essa sombra só pode existir no render mitigado** — um
teste que só exercitasse o caminho normal nunca a veria, e concluiria, errado,
que o campo não faz nada.

### 39.4. O canto medido contra o gabarito — e o número que NÃO foi aplicado

`[BIN]` Ajustada a curva contínua transcrita contra o perfil por linha do
`apple-512.png` (o instrumento é `scripts/chiclet-profile.py`, que mede **área
por linha** e por isso não depende do filtro de antialiasing de nenhum dos dois
lados):

| `r/N` | RMS | leitura |
|---|---|---|
| `0,2018` | **3,456 px** | `cornerRadius − inset` do *witness* — canto **pequeno demais** |
| **`0,2250`** | **0,088 px** | **o que o gabarito tem** |
| `0,2600` | **5,355 px** | `0,26 × corpo` — o nosso, canto **grande demais** |
| `0,3231` | 16,054 px | `0,26 × quadro` — refutado antes |

> **É a mesma FAMÍLIA de curva com outro raio.** Com o raio livre, o canto
> contínuo transcrito reproduz o perfil da Apple com **0,088 px** de RMS; a
> família rival testada — o canto **circular**, o tipo 3 — **não passa de 1,77 px**
> com o melhor raio dela. **Não é a curva que muda: é o raio.**
>
> E **as duas leituras que o binário oferece erram com sinais opostos**, com o
> gabarito no meio. Isso é novo e importa: a hipótese do quadro (`0,3231`) errava
> para o **mesmo lado** que a nossa, então refutá-la não tinha estreitado nada.

**A tabela de hipóteses refutadas**, que é o que impede a próxima frente de
repetir o trabalho:

| hipótese | veredito | por quê |
|---|---|---|
| o raio é `0,26` do **quadro** de 1024, não do corpo | **refutada** | daria `0,3231`, um canto **maior** — a diferença medida tem o **sinal contrário** |
| o `×1,275` não é desfeito neste caminho | **refutada** | `r/N = 0,3315` dá RMS de **17,6 px** |
| é o nosso rasterizador (`kSubRows = 4`) | **não é ele** | nosso render cruza a diagonal em `31,175 px` e o modelo contínuo em `31,228`: **0,05 px** de diferença contra os **4,1 px** que faltam |
| a folga por aresta recalculada no meio do canto | **sem efeito aqui** | o chiclet é quadrado de quatro raios iguais: as duas arestas dão o mesmo `t` |
| **não adianta procurar o `inset` certo** | **refutada, e esta é a mais forte** | o *witness* é de **um parâmetro só**: o mesmo `inset` encolhe o rect **e** subtrai do raio. O gabarito o amarra duas vezes e as duas contas não fecham juntas — `inset = 50` dá o corpo certo e raio `83,12`; `inset = 40,42` dá o raio certo e corpo `431,2`. **Logo o `DefaultIconShape` não desenhou este canto, seja qual for o `inset`** |

> `[BIN]` **E o `0,225` comprovadamente NÃO EXISTE em binário nenhum.** Isso mudou
> de status em 16/09/2026 e é diferente de "ainda não achamos": foram **onze**
> binários (as quatro fatias de cada versão mais os três executáveis do app) em
> **quatro passadas independentes** — bytes crus de **todas** as seções em todos
> os alinhamentos, **imediatos de 64 bits reconstruídos de `MOVZ`/`MOVK`/`MOVN`**
> testando o acumulador a cada `movk`, `FMOV` escalar, e strings decimais —, com
> **controle positivo passando**: o varredor acha o `100/1024` e o `266,24` sem
> esforço, nas duas versões. Também não achou `185,4`, `230,4`, `166,24`,
> `0,2018`, `824,0` nem `0,8046875`. O vizinho mais próximo em todo o conjunto é
> um `0,22` numa tabela de material (`0x98520`) e um `0,223989873661194` no
> RenderBox — nenhum dos dois no caminho do canto.
>
> **Logo o raio não muda.** Trocar `0,26` por `0,2250` fecharia o diff e não teria
> uma única leitura de binário atrás. *O gabarito desempata leitura; ele não
> escolhe número.*

**Três coisas que pareciam explicar o canto e não explicam**, todas fechadas
negativamente em 16/09:

`[BIN]` **Não é idiom, e não poderia ser.** O `KEYFORMAT` do `Assets.car` é
`[7,1,2,17,9,10,14,12,24,19,18]` e **o token de `Idiom` (15) não está nele** — o
catálogo não tem eixo de idiom. E do nosso lado o `--idiom` é um no-op **bit a
bit**: cinco valores, o mesmo SHA-256. A razão está lida: aqui `idiom` é **chave
de especialização do documento** e nunca toca a forma; lá a forma vem de
**`style.platform`** (`Configuration+0x60`, lido em `0x42098`), que indexa
`platformOverrides` (`0x5EB38`) e dá o `cornerRadius = 512` ao watchOS. **São
eixos diferentes com o mesmo nome coloquial**, e macOS mora em `main`, que não
tem override nenhum. `[OBS]` E o nosso CLI **não expõe `style.platform`**: o
círculo do watchOS é inalcançável pelo `icrender`.

`[BIN]` **Não é aparência.** O bitmap do gabarito está em `NSAppearanceNameSystem`
(id 0) e as três aparências do documento compilado são `DarkAqua`, `Aqua` e
`Tintable` — o gabarito é de uma aparência que o documento não tem, e as duas que
ele tem já eram byte a byte idênticas. `--appearance` também é no-op aqui: a
escolha do laudo do oráculo foi **inócua, não sortuda**.

`[BIN]` **E não é espaço de cor.** As duas renditions de 512 diferem só no token
`DisplayGamut` e **são a mesma imagem**: 3.724 dos 189.993 pixels visíveis
diferem, **nenhum por mais de 1 nível de 255**. A hipótese de que a média de ~10
níveis viesse de comparar P3 contra sRGB está **refutada**.

> ### E os quatro cantos são o maior PICO, não o maior erro
>
> Este documento e o `README` diziam, por herança, que *"o maior erro de pixel do
> projeto são os quatro cantos"*, porque os doze piores blocos 8×8 são eles.
> `[BIN]` **"Pior bloco" e "maior erro" não são a mesma coisa, e a diferença foi
> medida.**
>
> A banda de silhueta (`|Δα| > 128`) são **2.524 px**, **1,33 % dos visíveis** —
> cerca de 631 por canto, uma faixa de ~4 px ao longo de um arco de ~157 px, que é
> exatamente o que uma diferença de raio de 14 px produz. Dentro dela os quatro
> canais **saturam de uma vez** (a Apple opaca, nós com `α = 0`), e é isso que põe
> os piores blocos todos nos cantos. **Fora dela o canto não é pior que o resto:**
> nos quatro quadrados de 110×110 que os contêm — 26,4 % dos pixels — mora
> **24,97 %** da soma do erro. A fatia de área e nada mais.
>
> | | R | G | B | A |
> |---|---|---|---|---|
> | média sobre os visíveis | 8,85 | 10,18 | 9,85 | 4,95 |
> | **média sem a banda de silhueta** | **7,68** | **9,05** | **8,64** | **1,82** |
>
> **Um canto perfeito valeria 1,2 nível de RGB** — e **3,1 dos 4,95 do alfa**,
> onde ele é quase tudo. Os ~8 níveis restantes são os de sempre: `blur-material`
> desligado, grampo `plusLighter` desligado, overdraw da sombra. O
> `scripts/png-diff.py` passou a **imprimir esse peso**, pela mesma razão que o
> docstring do `slice-reach.py` foi consertado: o número e a leitura dele saem do
> mesmo arquivo, e **consertar o instrumento é parte da integração**.

### 39.5. Os realces do chiclet, e a classe que não move pixel

`[BIN]` O chiclet **não tem `hasSpecular` de camada**, e por isso não podia estar
na função do glifo. Ele tem a sua: `0x475A0`–`0x47AE8`, com o portão em
`ctx+0x21`. E o resolvedor é **o mesmo corpo**: `0x5E590` é um thunk de três
instruções que põe `x1 = 0x62588` (o closure do chiclet) e cai em `0x5E59C`, para
onde o glifo também cai com `x1 = 0x627B4`. **Um corpo, dois seletores** — e é
por isso que `resolveHighlight`, escrito para o glifo, serve ao chiclet **sem uma
linha de mudança**.

`[BIN]` **`fill[+0x5B]` é uma classe de luminância do próprio fill**, e a escrita
foi achada (`0x1A920`–`0x1A97C`): `0x1F10C` devolve a faixa de **leveza HSL** —
`L = (max(r,g,b) + min(r,g,b))/2` — **por parada de gradiente**, e a classe sai de
dois limiares, `maxDimChicletLuminance = 0,2` e `minBrightChicletLuminance =
0,99`.

> **Não é `max(r,g,b)` e não é luma.** Um vermelho puro tem `hi = 1` e `lo = 0`,
> logo `L = 0,5`; um leitor que usasse o máximo sozinho o chamaria de `Bright` e
> trocaria o conjunto de **todo ícone saturado**. E o `b.pl` decide os dois
> empates: `minBright >= MAX` **não** é `Bright`, `sonda >= maxDim` **não** é
> `Dim` — **os dois empates caem em `Default`.**

`[BIN]` **E a escolha não move um pixel nesta versão.** Os trinta `memcpy` de
`0x101` bytes do construtor contam a história: os dezoito membros de
`chicletDefault`, `chicletBright` e `chicletDim` leem **exclusivamente** fatias de
pilha gravadas antes de `0x62F00`, e **a primeira constante nova aparece em
`0x63624`** — já dentro de `chicletClear`. Conferido membro a membro:
`Bright.keySharp` lê os mesmos quatro slots que `Default.keySharp`.

> Isso **corrige com `[BIN]`** a segunda metade de um `[OBS]` do laudo dos
> realces, que dizia que os índices de aparência eram inertes *"do lado do glifo,
> não do lado do chiclet"*. **São inertes dos dois lados**, por motivos diferentes
> — lá os cinco conjuntos saem da mesma fábrica `0x64604`, aqui os três saem das
> mesmas constantes de pilha — e com a mesma consequência.
>
> **E a regra foi implementada mesmo assim**, pelo mesmo motivo que a inversão de
> índice é implementada onde não se vê: o dia em que um arquivo de parâmetros
> diferenciar os conjuntos, a regra já está certa. **Medir é informação mesmo
> quando não é pixel**, e a classe medida entra na nota de cada render.

`[BIN]` No chiclet os **seis** membros existem (no glifo, `fillDiffuse` e `rim`
são `nil`), o expansor faz sete posições com o `dark` duas vezes, e o `rim` —
presente, com `opacity == 0` — é o único que não pinta: ficam **seis realces
vivos**. *Presente-com-opacidade-zero e ausente somem igual no pixel e são coisas
diferentes no laudo.*

`[ART]` A pastilha deixou de ser rampa chapada: **8,71 % a 10,83 %** dos pixels
visíveis mudam nos três ícones medidos, com Δ máx de canal de **168 a 177**, ao
custo de `+0,10` a `+0,19 s` por render.

> `[INF]` **Mas o rasterizador NÃO é o do alvo, e isso está dito.** O glifo
> termina no shader `glassHighlight`; **o chiclet não.** `[BIN]` `0xD904` (3.888
> bytes) desenha no `RBDisplayList` com `beginLayerWithFlags:`,
> `clipLayerWithAlpha:`, **`setConicGradient…`** (`0xE448`) e `drawShape:` — isto
> é, **uma faixa recortada preenchida por um gradiente cônico em torno do
> centro**. A forma da pastilha é analítica, então o alvo **não precisa de campo
> de distância**: o cônico dá o termo angular e a camada recortada dá o radial.
>
> Este renderizador resolve os seis realces pelo mesmo `0x4BD90` e depois os
> avalia sobre o **campo de distância** do contorno. **Os números são `[BIN]`; a
> máquina que os converte em cobertura é `[INF]`** — e o que ela pode errar é a
> queda angular perto dos cantos, onde a normal do contorno e o ângulo polar do
> centro deixam de coincidir. Isto volta a importar no §40.4, onde um campo **mais
> exato** afasta o render do gabarito exatamente aqui.

### 39.6. O que o chiclet deixou aberto

| `[OBS]` | onde |
|---|---|
| **Quem desenha o canto de `0,2250`.** O mecanismo está lido (`GlobalConfiguration.iconShape`, `0x4296C`, com o *witness* de `CGPath` de `0x4FA40`); o valor e o cliente estão **fora do bundle**. E a porta ficou mais estreita em 16/09 — ver §45.4 | `chiclet-geometria` §5.1, `canto-do-chiclet` §7.1 |
| `relativeIconInset` é escrito pelo `IconComposerKit` e só é lido no ramo legado: `[BIN]` para `0x4202C`, `[OBS]` para o resto do binário | `chiclet-geometria` §5.2 |
| Os três tamanhos do `.icns` **não obedecem** à fórmula de `0x4224C` e têm bordas sub-pixel | `chiclet-geometria` §5.3 |
| A **sombra externa** (até 448/512, +2 px em y) vive na passada `0x433D0`, que só roda com `drawMitigatedVersion`. Sem transcrição | `chiclet-geometria` §5.4 |
| O **SDF do shader** (`set_globals`, `0x95434`) nunca foi comparado com as doze cúbicas — mas agora há um perfil de linha contra o qual comparar | `chiclet-curva` §6, `chiclet-geometria` §5.5 |
| **O recorte alcança o FUNDO e só ele** — se o alvo corta também a arte das camadas ao mesmo contorno não foi lido. A nota diz isso em toda rendição | `chiclet-curva` §6 |
| A ordem de índice dos quatro raios (`0x7F5B4`/`0x7F5B8`) — irrelevante para raios iguais, decisiva para `setRoundedRect:cornerRadii:` | `chiclet-curva` §6 |
| `chicletOutset` de 32 não entrou no código; `fill[+0x61]` (o índice do ramo `systemAppearance`) sem origem seguida; `ctx+0x21`, o portão real, **sem nome no metadado**; `chicletClear`/`chicletScreened` não transcritos | `chiclet-curva` §6, `chiclet-realces` §6 |
| `chicletIsVisible` **sem consumidor**, com cinco negativas medidas (zero chamadores do getter e do setter; o renderizador nunca lê o byte; nunca é copiado para a `Configuration`; nenhuma outra fatia o toca; o formato não tem chave). Nasce `true`. **Comporta-se como superfície de API** — e a negativa é forte, **não selada** | `chiclet` §4.2 |
| A chave do dicionário de `parameters+0x248` foi lida como 1 byte com entrada `Optional<Double>`, mas o enum não foi identificado; se alguma plataforma real trouxer override ≠ `nil`, `266,24` deixa de ser universal | `canto-do-chiclet` §7.3 |
| `style.platform` **não tem botão no `icrender`**: o círculo do watchOS, que está `[BIN]` em `0x5EB38`, é inalcançável pela linha de comando | `canto-do-chiclet` §7.4 |

---

## 40. O campo de distância: a entrada é o alfa, e o gerador está no CoreUI

`[BIN]` **O alvo tira o campo de distância do ALFA RASTERIZADO da camada, não do
contorno vetorial dela.** Isso derruba uma distinção inteira que este
renderizador desenhava — "vidro sobre raster" nunca foi caso do formato, era
artefato do **nosso** gerador —, e faz do campo o gargalo de tempo do projeto
(§44) em vez de um detalhe do vidro.

Cinco frentes, e três delas convergem no mesmo buraco: **a grade e a transformada
do alvo não estão nestes binários.** A boa notícia é que isso está **provado por
carga de biblioteca** e não por ausência de busca.

### 40.1. A entrada, lida no chamador

`[BIN]` O seletor `sdfTextureWithBufferAllocator:` tem **um** stub e **uma**
chamada (`0x867F8`, dentro de `0x8658C`), e o receptor vem do único chamador. A
cadeia que importa é curta:

```
0x00028AB0  add x2, x2, #0x928        ; o classref 0xCC928
0x00028AD0  bl  #0x8df38              ; cast condicional
0x00028AE0  bl  #0x8eb60              ; _objc_msgSend$image
0x00028AEC  cbz x0, #0x291bc          ; IMAGEM NIL -> ABORTA
```

`[BIN]` E `0xCC928` tem **nome**, decodificando os slots de `__objc_classrefs`
contra a tabela de imports do `LC_DYLD_CHAINED_FIXUPS` (675 entradas): é
**`_OBJC_CLASS_$_CUINamedLayerImage`**. Um `CUINamedLayerImage` do CoreUI carrega
um **bitmap** — não tem caminho, não tem contorno, não tem regra de
preenchimento. E `0x28AEC` diz que sem a imagem **não há campo nenhum**.

`[BIN]` A reflexão diz o mesmo por outro caminho:
`IconRendering.SDF.SourceLayer` (`0xA3104`) tem dois campos, `displayList` e
`isOpaque`. **A fonte de um SDF é um desenho e um bit de opacidade, não uma
forma.** Se o campo viesse de geometria, o campo aqui se chamaria `path`, `shape`
ou `contours`.

`[BIN]` E o consumidor fecha o círculo: `0x25510`–`0x2552C` faz `setImage:`,
`setSdfTexture:` e então `setHasLightingEffects:` com `cset w2, ne` sobre o
ponteiro do SDF. **`hasLightingEffects` é literalmente `sdfTexture != nil`.** O
portão existia e estava certo; o que não existia era o motivo de ele fechar para
raster.

> **A lacuna fechou, e era de uma coisa só.** Translucidez, especular e refração
> saíam de toda camada de vidro `.png` pela **mesma** razão, e a razão era nossa.
> `[ART]` O efeito: **2.591.717 texels em 9 documentos** — e os nove são
> **exatamente** os nove que a varredura de manifesto previu (camada `.png` de
> vidro com efeito vivo **E** arquivo de arte em disco). A correspondência é um a
> um, e é ela que torna o **zero** dos outros 22 uma **medida** e não um alívio.
>
> O caso que vale sozinho: `CamilleScholtz__swmpc__swmpc` é o único documento do
> corpus com refração `.png` viva, e a única camada que ele tem **era pulada** —
> o `icrender` dizia `0 of 1 layer(s) drawn`. Agora diz `1 of 1`. **Não é um
> efeito a mais numa camada que já desenhava: é o documento inteiro saindo do
> vazio.**

`[ART]` De carona, o censo do `glass` foi recontado e a **régua foi declarada**,
que é o que faltava: o `glass` de uma camada pode vir como booleano ou como
lista, e as duas leituras dão números diferentes sem que nenhuma seja obviamente
a certa — **171 camadas de vidro (45 `.png`)** contando qualquer aparência, ou
**146 (39)** contando só a entrada base. `[OBS]` Qual delas o alvo aplica não foi
lido. Total do corpus: 145 documentos, 271 grupos, 437 camadas, e **`glass` nunca
é herdado** (zero ocorrências no nível do documento e do grupo).

### 40.2. O caminho vetorial passou a obedecer: 57,2 s → 0,46 s

O `generateField` por força bruta era **73 % do render do Apollo e 99 % do render
do ícone do usuário**. A frente tinha duas saídas — trocar por rasterização, ou
provar com número que o campo vetorial exato se justifica e acelerá-lo. **O
argumento de fidelidade já estava lido e apontava para a primeira:** o alvo
rasteriza e **depois** transforma, nos dois casos.

| ícone | tamanho | antes | depois | fator |
|---|---|---|---|---|
| `GoWToolkit.icon` (1 camada) | 1024 | **57,17 s** | **0,46 s** | **124×** |
| idem | 512 | 14,58 s | 0,22 s | 66× |
| `Apollo-Reborn/AppIcon` (10 camadas) | 1024 | 9,84 s | 2,69 s | 3,6× |

`[ART]` **Por que uma camada custava mais que dez:** `GOW.svg`, achatado com
`subdivisions = 16`, tem **16.788 segmentos** contra 64 a 724 nas oito artes do
Apollo. A força bruta é `pixels × segmentos`: **1,76 × 10¹⁰** testes
ponto-segmento para **uma** camada.

> ### E a troca não foi só mais barata — ela removeu um ERRO
>
> O oráculo *ad hoc* que comparou os dois geradores na mesma forma achou quatro
> casos convergindo como `1/ss`, que é o que uma grade tem de fazer, e **um que
> não converge**:
>
> | forma | \|Δd\| máx, `ss=1` | `ss=3` | `ss=9` |
> |---|---|---|---|
> | retângulo, círculo, disjuntos, anel | 0,21 – 0,53 px | 0,07 – 0,25 | 0,02 – 0,06 |
> | **dois retângulos SOBREPOSTOS** | **25,907 px** | **25,770** | **25,724** |
>
> `[BIN]` A força bruta mede até o segmento mais próximo, **esteja ele enterrado
> dentro da união ou não**. Onde um subcaminho pintado passa por baixo de outro, o
> campo **mergulha para −0,5 no meio da forma** e **o especular desenha um aro
> ali**. Uma forma rasterizada não tem aresta enterrada para medir, e `[BIN]` **o
> alvo, que rasteriza, também não pode ter.**
>
> A ressalva **já estava escrita no `DistanceField.h` desde sempre** — *"the
> interior edge shows up as a crease in the field. Apple's smooth union does not
> have that crease"*. Esta frente a **mediu** e a **removeu**. Na arte real, sete
> das oito camadas do Apollo concordam com o `argmin` exato dentro da grade; a
> oitava é o `stem.svg`, com `|Δd|` máx de **3,223 px** e **36 px de dobra** — e o
> pior bloco 8×8 do render inteiro a 512 px está exatamente ali, na base da
> antena, com 121,9 níveis.
>
> `[OBS]` **E ninguém contou em quantos documentos isso mudou o desenho.** A dobra
> sumiu; a varredura que diria onde ela existia não foi feita.

`[BIN]` **A escada de supersample foi medida e o número é UM.** Refinar a grade
não alcança o piso de ~10,9 % nem o bloco de 121,9, porque **eles não são a grade
— são a dobra**. `ss=3` compra 1,5 nível de delta médio por **2,6× o render** e
não compra o piso a preço nenhum. E `ss = 1` é também a grade em que
`generateFieldFromAlpha` já roda: **uma convenção na torre, não duas.**

> **A régua de 8/255 foi desarmada por medida, e isso é método.** O delta máximo
> visível é 255 e o contrato manda parar e explicar. `[BIN]` **Dois campos
> rasterizados que diferem entre si por no máximo ~0,05 px (`ss=9` contra `ss=15`)
> já mudam 10,14 % dos pixels visíveis, com máximo 255.** Nenhuma mudança de campo
> neste ícone cabe em 8/255, **inclusive uma que ninguém chamaria de mudança** — o
> especular é uma banda de ~1 px de largura em `d`, então 0,3 px de erro move a
> banda por um terço da largura dela. É por isso que as colunas de **bloco 8×8**
> existem: elas separam *"o aro pontilhou"* de *"o aro mudou de lugar"*.

**E de carona a frente derrubou uma "correção de alvo".** Uma medida anterior
dizia que um documento simples renderizava em 0,16 s, o que parecia contradizer o
custo do campo. `[BIN]` O documento era `{"kind":"none"}` **sem `opacity`** e
estava sendo **RECUSADO**: `0 of 1 layer(s) drawn`. **Os 0,16 s eram o tempo de
não desenhar nada.** É a armadilha 4(b) do projeto — conferir a contagem de
camadas antes de acreditar num tempo — pegando um caso real.

### 40.3. A semente sub-texel: uma via medida que NÃO abriu, e o que entrou no lugar

O especular saía picotado na borda, e a causa é estrutural: a semente da
transformada é **binária**, cada texel é dentro ou fora, e o zero do campo fica
**preso ao centro do texel**. Uma banda de ~1 px lida num campo quantizado em
1 px **acende por texel inteiro**.

**A via medida foi procurada primeiro, e ela não abriu.** `[BIN]` A estrutura
`ICRRenderingParameters.SDFGeneration` **existe** no metadado (`0xA46E0`) com
quatro campos — `clampThreshold`, `useAdvancedStacking`,
`precisePixelFormatThreshold`, `maxRelativeSmoothing` —, e os nomes aparecem em
**exatamente três lugares**: `__swift5_reflstr`, `__cstring` e `__LINKEDIT`. Os
únicos dois sítios de código que os tocam são o *getter* de
`CodingKeys.stringValue` (`0x61B10`) e o `CodingKeys.init?(stringValue:)`
(`0x744BC`) — isto é, **`Codable` e nada mais**. E o `RenderBox.arm64`, que tem
**11.996 símbolos**, não tem **um** que case `sdf`/`SDF`.

> **Os três botões de `SDFGeneration` podem sair da lista de coisas por ler:
> não há o que ler neste binário.** Isso é um resultado negativo caro e é
> exatamente o tipo de coisa que economiza a próxima frente — sem ele, alguém
> gastaria um dia procurando o consumidor de um campo que só existe para
> desserializar.
>
> Detalhe de método que veio junto: `clampThreshold` **não tem cópia no
> `__cstring`**. Tem 14 bytes, cabe em *small string* de Swift, e é montado por
> imediatos em `0x61B24`–`0x61B30`. Os outros três passam de 15 bytes e por isso
> precisam de literal. **Constante por `mov`+`movk` não está no pool** — de novo
> (§45.2).

`[INF]` **O que entrou no lugar é declarado como inferência e foi medido.** A
semente passa a carregar a cobertura: `sOff[t] = 0,5 − cobertura[t]`, que não é
convenção inventada — é o **inverso exato** da convenção de cobertura que a torre
já escreve (`cov = −d/aaWidth + 0,5`). **Semente e canal de cobertura são duas
leituras de uma regra só e não podem divergir.**

E o erro que a frente cometeu primeiro está no laudo, porque ele é instrutivo: o
movimento ingênuo é manter a distância da rede e **encurtá-la** pelo `sOff`. Está
errado, e mensuravelmente — o `sOff` corre ao longo da **normal** e a distância da
rede corre ao longo da **linha entre dois centros**; somá-las como se fossem
colineares custa até meio texel. **Medido: o pior erro de um círculo foi de 0,49
para 1,02 texel antes de isto ser apanhado.** O que a semente sabe de fato é um
**ponto**, e a distância euclidiana a esse ponto está certa em qualquer ângulo.

`[BIN]` A tabela que prova que o defeito era **estrutural**, na banda
`|d| ≤ 1,5 px` que o especular lê:

| n | rms binária | rms sub-texel | pior binária | gradiente binária | gradiente sub |
|---|---|---|---|---|---|
| 128 | 0,2477 | **0,0517** | 0,4905 | 28,96° | **11,25°** |
| 512 | 0,2497 | **0,0520** | 0,4971 | 29,22° | **12,42°** |
| 1024 | 0,2583 | **0,0549** | 0,4974 | 29,40° | **12,60°** |

**O erro da semente binária vai de 0,2477 a 128 px para 0,2583 a 1024 px: oito
vezes mais pixels e o erro não desce.** É um quarto de texel em qualquer
resolução, e o pior caso é `0,49` nas quatro — meio texel, exatamente o que uma
semente presa ao centro do texel tem de custar. Os 29° do gradiente são **o piso
da rede**: a um texel de distância só há oito direções.

E a honestidade sobre escala está no laudo porque ela **contraria a expectativa
do pedido**: o ganho relativo de lisura é `−26,8 %` a 512 e `−24,4 %` a 1024, não
o contrário — *"e não vou torcer a métrica até dar o contrário"*. A afirmação que
**se prova** é melhor: **`depois@512 ≈ antes@1024`** — o conserto entrega a 512 px
a lisura que antes só se conseguia a 1024, a um quarto dos pixels.

`[BIN]` **E o anel da sombra ficou intocado**, verificado e não deduzido:
`shadowRingMask` tem a própria cópia da transformada e **nunca chama**
`generateFieldFromAlpha`. Bit a bit idêntico.

### 40.4. O `superSample`: a qualidade que o `[OBS]` prometia, e o gabarito recusando

O `[OBS]` que sobrou da frente do especular dizia que `superSample = 3` levaria o
pior caso angular de ~10° para *"≤1 grau (max 7)"*, ao custo de nove vezes as
transformadas, e que **o tempo disso não tinha sido medido**. Foi.

`[BIN]` **A promessa de qualidade estava certa — e por baixo:**

| | mean/pior, `ss=1` | mean/pior, `ss=3` |
|---|---|---|
| quatro faixas de profundidade | 0,96/4,13 · 2,51/15,74 · 2,77/19,86 · 2,38/13,11 | **0,54/2,02 · 0,66/4,83 · 0,64/5,76 · 0,62/5,15** |

**E o gabarito recusa os dois sítios.** Quatro casas decimais, porque em duas os
três resultados são o mesmo número:

| variante | R | G | B | A | pixels movidos |
|---|---|---|---|---|---|
| `ss=1` (base) | **8,8416** | **10,1735** | **9,8461** | **4,9520** | — |
| `ss=3` só na arte | 8,8411 | 10,1738 | 9,8484 | 4,9520 | 8.488 (5,00 %), Δ máx 13 |
| `ss=3` só no chiclet | 8,8468 | 10,1786 | 9,8513 | 4,9560 | 4.954 (2,92 %), Δ máx 29 |

O chiclet **piora os quatro canais** por 2,43×–2,64× de custo: recusa fácil. A
arte **empata** — um canal a favor por 0,0005, dois contra por até 0,0023 — e um
empate que custa 2,6× é uma derrota.

> **E o chiclet piorar é informativo, não ruído.** `[INF]` `0xD904` desenha aquele
> realce com um **gradiente cônico** numa camada recortada (`setConicGradient`
> `0xE448`), **não a partir de um campo** (§39.5). **Aproximar o nosso estimador
> da forma fechada é afastá-lo do instrumento que o alvo usa.**
>
> É a regra anti-*overfitting* funcionando na direção incômoda: o gabarito
> arbitrou entre duas leituras nossas e **escolheu a menos exata**. Não se ajusta
> o número até o pixel fechar — e também não se paga 2,6× por um número mais
> bonito que o pixel recusa.

Dois achados de método vieram junto. `[BIN]` **`superSample = 2` é `superSample =
1` bit a bit**, porque o gerador arredonda par para o ímpar de baixo
(`DistanceField.cpp:964`) — só um fator ímpar tem um sub-texel cujo centro **é** o
centro do pixel, e um par leria o campo meio sub-texel fora, pondo a figura fora
de passo com o `CoveragePass`. Isso virou caso de teste **porque é a primeira
coisa que o próximo leitor tenta**. E `generateFieldFromAlpha` **não deve ganhar
o parâmetro**: rasterizar contorno analítico mais fino **produz** informação,
enquanto reamostrar um alfa só inventaria borda — e a informação sub-texel que o
raster realmente tem já entra exata por `coverage[t] = alpha[t]`. *"Somar
`superSample` ali seria simetria de assinatura, não ganho."*

`[OBS]` `ss = 5` corta o erro angular pela metade de novo (0,32–0,36° de média)
por 25× as transformadas, e **não foi levado a render nenhum** — como `ss = 3` já
não move o gabarito, não havia o que medir.

### 40.5. O nível zero do texel não cabe nos 7,5 unidades, e a prova dispensa o CoreUI

O `[OBS]` da forma da banda (§38.7) tinha três candidatos; dois já tinham caído.
O último era **a convenção do nível zero do texel do SDF** — e ele morre por uma
eliminação **dimensional**, sem que o gerador seja lido.

`[BIN]` **O consumidor inteiro foi lido, e ele tem UM termo de origem:**

```
sd = (2·tex.r − 1) · maxDistance − inset
```

por **três caminhos independentes**: os shaders `default_mod0.ll`/`default_mod1.ll`;
a escada de `setArgumentBytes:atIndex:` de `0xE834` (índice 8 = `0,5f` em
`0xEAA8`, índice 7 = `−2·maxDistance` em `0xEA7C`, índice 1 = `inset` em
`0xE994`); e o gêmeo de CPU `RB::CGContext::apply_glass_highlight`
(`RenderBox 0xC0E64`), **que tem símbolos**. E esse único termo de origem, o
`inset`, **é zero** por releitura da fábrica `0x64604`: **não há um segundo `0,5`,
não há meio texel, não há `fmsub` escondido.**

`[BIN]` **O formato do texel deixou de ser pergunta porque o consumidor RECUSA o
que não conhece.** `0x86A70` compara `[texture pixelFormat]` contra a tabela de
quatro entradas que `0x85654` monta em `0xE2B80`, e `0x86AB0` levanta o código 4
se nenhuma casar: `BGRA8Unorm(80)`, `RGB10A2Unorm(90)`, `RGBA16Float(115)` e
`R8Unorm(10)` — confirmadas por um segundo caminho independente, o campo de
bytes-por-pixel `4/4/8/1` da mesma linha.

**A eliminação é dimensional e é dupla:**

1. `[BIN]` Um erro `Δu` no nível zero desloca `sd` de `2·D·Δu`, com `D` em
   unidades de canvas. Os 7,5 exigem `Δu = 3,75/D`. **Meio LSB do mais grosseiro
   dos quatro formatos é `1/510`** e pediria `D ≥ 1912` — **1,32× a DIAGONAL** de
   um canvas de 1024. Um LSB inteiro, que já não é convenção e sim engano, ainda
   pede `D ≥ 956`, 93 % do lado. E em `RGBA16Float` o `0,5` é **exato** e
   `Δu = 0`.
2. `[BIN]` **Qualquer termo de grade é constante em PIXELS**, porque `0xE8CC`
   relê `texture.width` a cada desenho; **e o deslize medido é constante em
   unidades de CANVAS** — `3,0`–`3,5 px` a 412 **e** `1,5 px` a 206. **A mesma
   régua mata o meio texel e o `clampThreshold`**, porque a rampa de alfa de uma
   borda antialiada tem ~1 pixel de largura.

> **A parede do CoreUI virou prova positiva em vez de ausência de busca.** `[BIN]`
> Ele entra por **`LC_LOAD_WEAK_DYLIB`** nas duas versões — **WEAK, e por isso
> invisível numa listagem de `LC_LOAD_DYLIB`**, que é exatamente por que a parede
> parecia menor —, com **zero** ocorrências de `CoreUI` nos três executáveis do
> app, nos dois appex, no `disk.img` e no `tree.txt`.
>
> E de brinde: `[BIN]` **`RenderBox.arm64` é o MESMO binário nas duas versões** —
> `LC_UUID 237dec3cbe7c3143877eebd12cfbb196` idêntico, **413 bytes divergentes,
> todos dentro do `LC_CODE_SIGNATURE`**. Ninguém precisa reanalisar o RenderBox
> do 27.0-129.

`[OBS]` **O deslize segue aberto, com quatro candidatos a menos e afiado:** o que
resta não é convenção de codificação nem artefato de grade, é um **termo
geométrico de ~7,5 unidades de canvas dentro do gerador** — isto é, o contorno
sobre o qual o CoreUI centra o campo **não é** o `alpha ≥ 0,5` da arte, e sim um
contorno **recuado**. E faltam exatamente **dois valores**, os dois escritos por
`-[CUINamedLayerImage sdfTextureWithBufferAllocator:]`: o `maxDistance` que chega
a `[Layer+0xA8]`, e o valor de `tex.r` no contorno `alpha = 0,5`. Os dois vivem no
`dyld_shared_cache`, e **nada disso foi inventado aqui**.

### 40.6. O que o campo deixou aberto

| `[OBS]` | onde |
|---|---|
| **A grade e a transformada do alvo estão no CoreUI, fora das fatias deste projeto** — e os três botões de `SDFGeneration` (`0xA46E0`) **podem sair da lista**: não há o que ler | `semente-aa` §1/§9.4 |
| O limiar `alpha >= 0,5` **não veio do binário** nem aqui nem no `shadowRingMask` — duas frentes herdaram a mesma convenção da mesma falta, e ele agora governa também a arte vetorial | `vidro-sobre-raster` §7.3, `campo-de-distancia` §7.4 |
| A **dobra** que a força bruta punha em todo subcaminho enterrado sumiu, e **nenhuma varredura contou em quantos documentos isso mudou o desenho** | `campo-de-distancia` §7.5 |
| A regra do `glass` especializado: **171 ou 146 camadas** conforme a leitura, e nada resolve qual o alvo aplica | `vidro-sobre-raster` §7.5 |
| O campo do chiclet é recalculado a cada render de forma **fixa**, sem cache; o custo dele em `ss = 1` não foi isolado | `supersample-campo` §7 |
| `ss = 5` medido e **não investigado** | `supersample-campo` §7 |
| Os **dois bytes** que faltam ao deslize de 7,5: `maxDistance` e o `tex.r` do contorno `alpha = 0,5`, os dois no `CoreUI.framework` | `sdf-nivel-zero` §5 |

---

## 41. A refração: o `range` é do campo, e o vidro LÊ o fundo

`[BIN]` **O `range` do `RBDisplayListGlassDisplacement` é vocabulário do CAMPO DE
DISTÂNCIA, não do efeito**, e o `v` que o `IconRendering` escreve como `(v, −v)`
é o `sdf.maxDistance` da camada. E o `[BIN]` que este documento carregava no
§34.3 — *"o vidro da Apple não amostra o destino"* — era **meia-leitura**:
verdadeira sobre o gerador do mapa, falsa sobre a composição.

**Zero pixel mudou nesse commit**, e os três resultados são medidas.

### 41.1. O corpus fala antes do binário

`[ART]` A frente abre pelo censo **de propósito**, antes de qualquer instrução:
dos **271 grupos**, `shadow` aparece 240 vezes, `translucency` 203,
`blur-material` 123, `specular` 103 — e **`refractivity` cinco**. Dessas cinco,
três estão `enabled: false`. **Dois grupos em 271 refratam, e os dois com força
NEGATIVA** (`−0,527` e `−0,359`), o que corrobora do lado do artefato a
assimetria que o binário já dizia: a força preserva o sinal, a altura é grampeada
dos dois lados.

`[ART]` **E o gabarito não pede refração.** Isso é dito cedo porque **muda o que a
frente pode provar**: se a refração está desligada contra o gabarito, ela não pode
responder por nenhum dos `8,95 / 10,03 / 9,89` de erro médio. O único esconderijo
possível era o campo **sem nome** do TLV `0x3fe` do compilado, e ele morre por uma
estatística do corpus: *"das sete chaves numéricas que um grupo pode carregar,
**nenhuma assume o valor 0,02 uma única vez nos 271 grupos**; e
`refractivity.strength` só assume `{0, −0,527, −0,359}`: nunca positivo."* O mapa
**rival** do TLV `0x3fd` também não nomeia refração — **sob as duas leituras o
gabarito não refrata**.

### 41.2. O `range` tem nome, e o nome é do campo

O `[OBS]` 4 do §29.8 dizia: *"os campos estão nomeados e os offsets lidos, mas o
valor `v` que o `IconRendering` escreve como `(v, −v)` não foi atribuído a nenhuma
grandeza nomeada"*.

`[BIN]` **O primeiro passo é a assinatura demangled, e ela já entrega metade:**

```
State::add_glass_displacement(Builder&, unsigned char, DistanceGradient,
                              float2, float,float,float,float,float)
                              ^component      ^gradient  ^range
```

e o irmão **`add_glass_highlight`** (`0xF3550`) tem **o mesmo `float2` no mesmo
lugar, logo depois do mesmo enum**. **`range` é vocabulário do campo,
compartilhado pelos dois efeitos** — não é geometria do deslocamento, e por isso
um "`range` da refração" nunca existiu como grandeza separada.

`[BIN]` O enum `DistanceGradient` tem cinco casos, lidos da tabela de strings do
próprio serializador XML (`0x18D978`): `0 sampled`, `1 stored`,
`2 stored-inverse`, `3 stored-float`, `4 stored-float-inverse`. O `IconRendering`
escreve **`1 = stored`**, num único `strh` que põe os dois bytes de uma vez
(`0x10CC4`).

`[BIN]` **E o consumidor prova que é decodificação.**
`RB::CGContext::apply_glass_displacement` (`0xC1324`) só olha para o `range` no
lado `gradient != 0` do `cbz` de `0xC14A8` — o lado `sampled` **deriva** o
gradiente por uma convolução separável de 15 taps e **nunca lê o `range`**. Do
lado `stored` ele é dobrado em `(escala, viés)` e pareado com uma constante
unorm→assinado escolhida pelo mesmo enum: `(2,−1)` para `stored`, `(−2,1)` para
`stored-inverse`, `(1,0)` e `(−1,0)` para os `float`. **Um `float2` cujo uso
inteiro é "um extremo por extremo de canal" é um intervalo de decodificação.**

`[BIN]` O `v` foi então perseguido por *dataflow*: `0x10CCC` (o `stp` depois de um
`fneg` literal) ← terceiro argumento de ponto flutuante de `0x10C14` ← os **dois,
e são só dois**, sítios de chamada (`0x4A784`, `0x4A9F4`) ← `[sp,#0x40]` ← `d14` ←
**`[x0, #0xA8]`**. E `x0` é o descritor de `0xC0` bytes que o §36.4 nomeou: na
reflexão, `sdf` fica em `+0x98` e `shadowImage` em `+0xB0`, 24 bytes de espaço, e
`IconRendering.SDF` (`0xA2FF4`) é `{texture, maxDistance: Double}` — **o único
`maxDistance` de toda a reflexão do slice**. Um `Double` final num struct de 24
bytes cai em `+0x10`. Logo:

```
range = (sdf.maxDistance, −sdf.maxDistance)
```

`[BIN]` **Controle cruzado, por um segundo consumidor independente:** o especular
do glifo carrega o mesmo `[descritor+0xA8]` no **argumento 7** do shader dele
(`0x49238`), ao lado do literal `0,5` no argumento 8 — e a decodificação
transcrita de lá dá **o mesmo intervalo simétrico, com a mesma magnitude**, por
outro caminho. *Dois consumidores, um escalar, um nome.*

> **E isso elimina um candidato do `[OBS]` da forma da banda (§38.7), o que é
> metade do valor da leitura.** O `range` é **simétrico em torno de zero por
> construção** — o `fneg` é literal —, logo transporta **ESCALA** e não consegue
> carregar deslocamento nenhum. `[descriptor+0xA8]` sai da lista, e o nível zero
> da textura do CoreUI fica sozinho nela (até o §40.5 matá-lo também).

### 41.3. A correção do §34.3: são DOIS objetos

A cadeia inteira, elo a elo — e ela é o que resolve a contradição:

```
0x10CB4  beginLayer                              <- camada A: o MAPA
0x10CF4    addStyle:data: estilo 3                  component 0, gradient 1 (stored),
                                                    range (M, -M), height em texels
0x10D74    setArgumentBytes: 0 = -strength
0x10DC4    addFilterLayerWithShader:...           <- FECHA A e a registra como entrada
0x4A794  beginLayerWithFlags: 1                   <- camada B, COM O BIT DE FUNDO
0x4AA00  idem, o segundo sitio
0x4A7A4  drawLayerWithAlpha:1.0 blendMode:0
```

`[BIN]` `-[RBDisplayList addFilterLayerWithShader:…]` (`RenderBox 0x40A18`) **é**
`end_layer` + `restore` + `add_shader_filter_layer`, que termina em
`State::add_custom_effect` levando a camada recém-fechada como argumento. **O
`glass-displacement` gera o MAPA; quem desloca é um `CustomEffectStyle` sobre
`_RBSystemShaderDisplacementMap`.** E a camada que veste esse estilo abre com
**flag 1**, o bit de fundo, que `0x3BCA0` passa intacto pela máscara `0x7B` até
`Layer+0x44`, onde `null_style_draw` o testa (`0xCE02C`) e pendura um
`BackdropFilterItem` **na camada PAI**. O caminho do estilo **chega lá**:
`CustomEffectStyle::draw` (`0xF4030`) → `Builder::draw` (`0xCDAA0`) → tail-call
para `null_style_draw` em `0xCDAF4` — **o único chamador dele no slice**.

> **A leitura antiga estava certa e era sobre outro desenho.**
> `GlassDisplacementStyle` é o **gerador do mapa**: o trabalho dele é transformar
> o campo da forma em vetores de deslocamento, então ele filtra o item que veste —
> **é o que ele tem de fazer**. Ler isso como "o vidro não olha para trás" é ler o
> particular como geral. **Os dois `[BIN]` são sobre dois desenhos diferentes e
> nenhum precisa ser retirado.**
>
> `[ART]` Corroboração de que a família **tem** a forma de fundo: o slice
> instancia `GenericFilter<GlassDisplacementEffect>::make_backdrop_item`
> (`0x7ECAC`), `BackdropFilterItem<GlassDisplacementEffect>` (`0x7E12C`) e até o
> `Decoder::emplace` dele. **`CustomEffect`, por contraste, não tem nenhum dos
> três** — consistente com ele chegar ao fundo pelo ramo de **lista vazia**
> (`0xCE190`) e não pelo virtual.

**E o que isso muda neste repositório é menos do que parece, o que é a parte
instrutiva:** *"a premissa corrigida vira a premissa ORIGINAL."* O nosso
`glassOver`, que desloca o buffer de acumulação **no lugar**, tem a forma certa; e
a recusa de um grupo que mescla **e** refrata continua válida **pela primeira
razão e não pela segunda** — um grupo desenhado num alvo próprio refrataria um
fundo vazio **para a Apple também**.

`[INF]` Um elo fica marcado: entre o bit ser posto e o bit ser testado está
`Builder::ensure_layer` (`0xCCD5C`), e só a máscara `0xC` de `0xCCE4C` foi
percorrida. *"A camada B chega inteira ao `null_style_draw`"* é `[BIN]` quanto à
máscara e `[INF]` quanto a nenhuma outra guarda disparar.

### 41.4. A refração NÃO é o aro escuro — duas vezes

O §41.1 responde a primeira vez pelo documento. A segunda é pelo pixel, e é o que
faz dela uma eliminação e não um argumento.

**O instrumento** — o mesmo que depois mataria os outros dois suspeitos (§36.7,
§37.5): um **interruptor temporário por variável de ambiente**
(`IC_EXP_REFRACT="depth,strength"`), que força `refractivity` em todo grupo de
vidro, **revertido antes do commit** (`git diff` vazio, `icrender` reconstruído
com o mesmo SHA-256). O perfil é de luma por profundidade no ápice superior **da
arte de vidro**, com a borda achada na nossa própria saída para que as três
imagens sejam amostradas nas **mesmas linhas**.

| profundidade (un.) | 0,0 | 2,5 | 5,0 | 7,5 | 9,9 | 12,4 | 14,9 | 17,4 |
|---|---|---|---|---|---|---|---|---|
| **Apple − nós** | **−60,5** | **−62,0** | −7,3 | **+46,0** | +44,4 | +35,8 | +28,0 | +16,9 |
| refração `s = −1,0` (o máximo legal) | +4,8 | +0,4 | +2,7 | +4,0 | +4,3 | +4,4 | +4,6 | +4,8 |
| refração `s = −0,527` (a do corpus) | +6,1 | +0,9 | +1,9 | −0,7 | −1,8 | −3,1 | −5,0 | −6,6 |
| refração `s = +1,0` | +4,1 | −2,6 | +0,3 | +3,1 | +3,6 | +3,2 | +1,7 | +0,9 |

`[BIN]` (medida) **A refração não chega perto.** O aro pede `−60`; nos **quatro
cantos do espaço que o formato permite** (`|s| ≤ 1` pelo grampo de `0x4A74C`,
`d ∈ [0,1]` pelo de `0x4A718`) ela move as primeiras 5 unidades entre `−4,9` e
`+6,1` — **uma ordem de grandeza curta**. E **o sinal é o errado justamente onde o
corpus vive**: as duas forças reais são negativas, e negativo **clareia** o aro.
Nenhuma variante põe uma banda clara em 7,5 unidades, que é a outra metade do que
o gabarito mostra.

`[BIN]` No agregado, forçar refração **piora monotonicamente com `|s|`**: de
`8,95/10,03/9,89` para `9,05/10,33/10,21` em `s = ±0,1` e `9,61/14,18/13,93` em
`s = −1,0`. **Não há valor que ajude.**

> **E o controle é o que faz a medida valer.** No ápice da borda **externa** do
> ícone — a pastilha, não a arte de vidro — a refração forçada move **exatamente
> zero** em toda profundidade, que é o certo, porque ali não há camada de vidro. O
> efeito aparece **só dentro da silhueta da arte**, numa banda de ~23 px a 412 px.
> É assim que se sabe que **o interruptor estava mesmo ligado no caminho que
> desenha** — a armadilha que uma medida "sem efeito" esconde.

---

## 42. O gradiente do fundo: a rampa não é uma reta

A frente nasceu de uma queixa visual — *"faz os efeitos do shape em si que ta com
**gradiente duro** ainda sem efeito"* — e a resposta não era um *easing* faltando.
`[BIN]` **A rampa do alvo é um `smoothstep`**, e chegar lá são **quatro leituras
encadeadas, nenhuma inferida**.

### 42.1. Do literal `0x400` ao polinômio

`[BIN]` **(1) O ponto de entrada é um literal.** O caminho de desenho de um fill
de documento é **uma** função (`0x1AACC`–`0x1BFFC`), e ela faz **uma** chamada de
gradiente:

```
0x0001BC6C  mov  w4, #3         ; colorSpace
0x0001BC70  mov  w6, #0x400     ; flags
0x0001BC74  bl   setAxialGradientStartPoint:...:flags:
```

Os dois são literais, sem caminho que os calcule, e o braço **sólido** da mesma
função passa o mesmo `3`. Os **bits 8–11** do `0x400` são um **código de
interpolação**, e ele vale **4**.

`[BIN]` **(2) O código 4 sobrevive ao construtor.** `RB::Fill::Gradient` guarda os
flags num halfword em `+0x34`; quando o chamador passa `locations` — e o
`IconRendering` passa — o construtor limpa o nibble e o reescreve no laço que
confere o espaçamento: código 1 vira *midpoint*, código 2 vira *bézier*, **e
qualquer outro é devolvido intacto**. O mesmo laço liga o **bit 15** quando alguma
parada foge do espaçamento uniforme; duas paradas em 0 e 1 são uniformes, então
**o bit 15 fica limpo** — e isso importa no passo seguinte.

`[BIN]` **(3) O teste de "duas cores" tem quatro termos, e o último pega 2 E 4.**
Em `0x9B990`–`0x9B9AC`:

```
tipo != 4  &&  contagem == 2  &&  flags bit15 limpo  &&  (código − 2) & ~2 != 0
```

**Com código 4 o teste FALHA**, a execução cai em `0x9B9B8`, que escreve `0x180`
— **rampa tipo 3, `sample_stops_uniform`** —, e `0x9B9EC` acrescenta o **bit 25**.
É isto que refuta a `[INF]` do §23.4 deste documento: **o `linear-gradient` do
fundo não cai no caminho de duas cores.**

`[BIN]` **(4) O bit 25 troca O QUE A RAMPA É.** No `default_mod8.ll`, o
`sample_stops_uniform` com o bit ligado **para de ler cores** e passa a ler um
`GradientCubicColor` — quatro `half4`, 32 bytes por registro — avaliado por
Horner: `c(f) = c0 + f·(c1 + f·(c2 + f·c3))`. **Confirmado do outro lado**, na
CPU: `set_gradient_color` (`0x9A8D4`) escolhe **16 halves por registro exatamente
quando o código é 4**, contra 4 ou 5 nos outros casos.

`[BIN]` **E os coeficientes têm uma função nomeada.**
`RB::Fill::(anonymous)::smooth_color_coefficients` (`0x9DEB0`–`0x9DF64`),
transcrita instrução a instrução, é um **Hermite cúbico monótono de
Fritsch–Carlson** escrito na base de potências via os pontos de controle de
Bézier — com as bordas saindo do **`spread`**: sob `reflect` ela alcança o vizinho
do outro lado, sob `pad` — o nosso caso — ela **duplica as pontas**.

> ### A degeneração, e por que ela não é caso de laboratório
>
> Com duas paradas e spread `pad`, as pontas duplicadas dão `d0 = 0` e `d2 = 0`;
> os clamps a `±3·d0` e `±3·d2` forçam `m1 = m2 = 0`, e sobra
>
> ```
> c(f) = p1 + (p2 − p1)·(3f² − 2f³)
> ```
>
> — **smoothstep**. *"O gradiente do alvo sai devagar das duas cores de ponta. Uma
> mistura reta do lado é a rampa dura."*
>
> `[ART]` E **toda** rampa que este renderizador constrói para um fill de
> documento cai nesse caso: **48 de 48** `linear-gradient` do corpus têm duas
> paradas, as duas rampas canônicas do `system-*` têm duas, e o
> `automatic-gradient` devolve duas. **O `smoothstep` É a rampa do ícone.**

### 42.2. A verificação, que é o que separa transcrição de palpite

`[BIN]` O delta máximo medido é **11 níveis**, e ele **não é pouco disfarçado de
pouco: é exatamente o que a curva prevê.** `max|smoothstep(t) − t| = 0,0962` em
`t = 0,2113`, e o canal verde do fundo percorre `172 → 66`, ou seja
`0,0962 × 106 = 10,2` níveis.

E a forma foi conferida contra a forma fechada, numa coluna do documento que
**desenha o fundo sozinho**:

| `y` | 0 | 108 | 216 | 324 | **512** | 700 | 808 | 916 | 1023 |
|---|---|---|---|---|---|---|---|---|---|
| antes | 255 | 246 | 238 | 230 | **215** | 200 | 192 | 183 | 175 |
| depois | 255 | 252 | 246 | 236 | **215** | 194 | 184 | 177 | 175 |
| previsto | 255 | 252 | 246 | 236 | **215** | 194 | 184 | 177 | 175 |

**Nove de nove** — e *"as pontas e o meio ficam presos, que é a assinatura do
`smoothstep` e não de um easing qualquer"*.

> **Nota sobre o documento usado na medida.** Ele desenha **0 de 1 camadas**,
> porque a arte é `.heic` e este renderizador não a lê. **Está na tabela de
> propósito**: é o fundo sozinho, a medição mais limpa do gradiente que existe no
> corpus. **Dizer que ele desenhou uma camada seria o "verde vazio"** contra o
> qual este projeto se guarda — e é por isso que a contagem de camadas aparece na
> linha.

`[ART]` O efeito: **91,56 %** dos pixels visíveis do ícone do usuário mudam, e
**54,73 %** no Apollo, ao custo de dois `fmuladd` e duas comparações por pixel de
fundo. **O que muda não é a amplitude, é a FORMA.**

### 42.3. Três hipóteses plausíveis, todas NEGATIVAS — e o negativo é o diagnóstico

*"Vale dizer, porque cada uma é uma suavização que alguém razoável tentaria a
olho."*

**(a) Oklab. Existe, e está desligado.** `[BIN]` O RenderBox **tem** a matriz LMS
do Oklab (`0x1625D8`), tem `convert_to_oklab` (`0x12D030`) e
`convert_from_oklab`, e o shader `Gradient::color_out` carrega a **inversa**
aplicada ao **cubo** da cor — as nove `half` batem **dentro de uma ulp**. **O
gradiente PODE ser interpolado em espaço perceptual.** Mas o gatilho são os **bits
6–7** dos flags: `set_fill_state` liga o bit 18 da palavra 0 **só quando esse
campo vale 3**, e o caminho de CPU usa o mesmo campo. **`0x400` tem os bits 6–7
zerados.** Os dois lados ficam desligados e `color_out` devolve a entrada intacta.

**(b) A LUT de transferência. Existe, e tem o mesmo gatilho.** `[BIN]` O mesmo
`color_out` tem uma **LUT de 4096 `half`**, indexada pelo padrão de bits do `half`
deslocado de 3 com o sinal preservado, escolhida entre duas pelo **bit 16** — que
vem **do mesmo campo de bits 6–7**. Também desligada.

**(c) Dithering. Não existe para gradiente.** `[BIN]` Há um `dither` no motor, mas
como *function constant* do shader de **imagem** (`default_mod2.ll`), ao lado de
`has_tone_map` e `has_tint`, e **de nenhum shader de gradiente**. **O alvo não
dithera o gradiente**, e o *banding* de 8 bits que sobra é real e é do alvo
também.

> **Bônus: duas suavizações que existem e que o ícone NÃO pede** — *midpoint*
> (código 1, `powr(t, log(0.5)/log(midpoint))`) e *bézier por stop* (código 2,
> quatro bytes escalados por 3/255). *"Um easing escolhido no olho teria imitado
> um desses dois e estaria errado pelo motivo certo."*

### 42.4. O corpus como argumento de decisão, não de pixel

Duas vezes, e as duas vale registrar porque são o tipo de leitura que muda o que
**não** se implementa:

`[ART]` **As 13 `orientation` idênticas.** Dos 14 fundos `linear-gradient` do
corpus que **nomeiam** uma `orientation`, **13 nomeiam exatamente
`start.y = 0 → stop.y = 0,7`** — o mesmo par do documento do usuário. *"Um valor
que aparece idêntico em 13 documentos de 13 autores diferentes não é uma decisão
de design: é o **default que o editor grava**. Honrá-lo não seria respeitar a
intenção do autor, seria **elevar uma constante da UI a geometria**."*

`[ART]` **A lateralidade de `y` continua `[OBS]`, com indício insuficiente.** Dos
mesmos 14, **11** põem a cor mais clara na ponta de menor `y`. *"É direcional e
não é leitura: **11 de 14 não é 14 de 14**, e três autores fizeram o contrário."*
A nota fica como está.

### 42.5. O que o gradiente deixou aberto

| `[OBS]` | conteúdo |
|---|---|
| **Cor premultiplicada contra reta** | `0x9B710` multiplica rgb por alpha **antes** da passagem de coeficientes, e o alvo guarda os coeficientes em `half`; aqui roda em cor reta e `float`. **Coincidem exatamente com alpha 1,0** — que é o caso de toda cor de fill medida — e divergem abaixo, sem leitura que decida |
| **Paradas não uniformes** | a rampa tipo 3 **não guarda posição** (o índice é `int(t·escala)`); espaçamento irregular liga o bit 15 e cai em **outro tipo, não lido**. A transcrição faz *fallback* linear com gate próprio em vez de fingir que o cúbico se aplica |
| **O gradiente próprio de um SVG** | não mudou, e a razão é que **como o `IconRendering` o entrega ao RB não foi lido**. Supor que é o mesmo caminho seria o chute que este projeto recusa |
| **`RB::ColorSpace`** | `rb_color_space(3)` (`0x8B704`) devolve `0x12`, dois nibbles que parecem *gamut* e *transferência*, sem nome lido. **O achado útil é outro:** o `3` é **literal nos dois braços** do desenho, logo **o espaço declarado no documento não chega a essa chamada**. O `IconRendering` não tem nenhuma das strings `srgb`/`display-p3` — elas vivem no `IconComposerFoundation` (`0x127204`), que importa `CGColorCreateCopyByMatchingToColorSpace`. **É lá que a próxima frente deve procurar**, e a matriz não foi inventada |

---

## 43. O oráculo: os pixels da Apple contra os nossos

Até 15/09/2026 este projeto tinha **145 documentos de corpus e ZERO imagens de
gabarito**. Toda afirmação de pixel era *"o binário diz que a fórmula é esta, e
nós a transcrevemos"* — **nunca** "a saída da Apple e a nossa foram comparadas".

`[BIN]` O `Assets.car` do Icon Composer 27.0 (129) guarda **o documento `.icon`
compilado E o bitmap que a própria Apple rasterizou dele**. Os dois foram
extraídos, e é isso que torna possível quase todo o resto deste capítulo — as
eliminações do aro (§36.7, §37.5, §41.4), a recusa do `blur-material` (§37.3), a
medida do canto (§39.4) e a da banda do realce (§38.7) **todas dependem disto
existir**.

### 43.1. Onde o documento estava escondido: a TLV, e não o payload

`[BIN]` As renditions `AppIcon.iconstack` (layout 1019) e `IconGroup` (1020) têm
payload `RAWD` de **comprimento zero** — doze bytes e nada mais. **O grafo de
camadas inteiro mora na seção TLV do `csiheader`**, entre os 184 bytes do
cabeçalho e o payload. É o tipo de coisa que um extrator escrito pelo nome da
rendition não acha nunca.

`[BIN]` As tags medidas: `0x3f4` são os **filhos** (`count × 48`, com o
`float[7]` sendo a **opacidade** e doze bytes de chave de rendition), `0x3fc` é o
**tipo de nó**, `0x3fd` e `0x3fe` são dois blocos de **efeito**, e a chave de 12
bytes de cada filho são **três pares `u16`** que casam com um `FACETKEY` do mesmo
catálogo menos o par constante `(0,3)` — é assim que o grafo se resolve por nome.

`[BIN]` **E `CELM` é uma CADEIA de `KCBC`, não um stream.** O corpo do bitmap são
registros de `'KCBC' + flags + zero + nLinhas + nComprimido`, em **faixas de 170
linhas, quatro registros para 512 linhas**. Tratar o corpo como um stream LZFSE
único devolve **a primeira faixa** — **um terço da imagem, e sem erro nenhum**.
Falha silenciosa, do tipo que produz uma medida confiante e errada.

### 43.2. O que foi SUPOSTO, e isto limita tudo que vem depois

Esta subseção existe porque **o oráculo só vale enquanto não for usado para
escolher as próprias suposições que o alimentam**. As inferências estão repetidas
em `inferences.json` ao lado do bundle reconstruído:

| `[INF]` | o que foi suposto | apoio |
|---|---|---|
| **ordem** | o array compilado é trás→frente e foi invertido | as opacidades do recorte `Tintable` caem `1,0 / 0,9 / 0,7` na direção do fundo, e a numeração dos assets sobe junto |
| **mapa de `0x3fd`** | `[specular, shadow.opacity, shadow.kind, translucency.value, translucency.enabled]` | **adjacência** — `(opacity,kind)` juntos e `(value,enabled)` juntos. **NÃO foi escolhido por diferença de pixel** |
| **mapa de `0x3fe`** | `[blur-material, SEM NOME, lighting]` | o segundo float fica **sem nome** |
| **`shadow.kind`** | o compilado diz `3`; adotado `neutral` | a tabela poria `none` em 3, e **um ícone com sombra visível não diz `none`** |
| **`glass`** | `true` em toda folha | **nenhum bit de vidro foi achado no compilado**; a saída da Apple tem realce e translucidez |
| **espaço de cor** | `display-p3` para SVG sem tag | `[ART]` **20 dos 145** documentos declaram a chave, **e os 20 dizem `display-p3`** |

> `[OBS]` **A leitura rival do `0x3fd`** — `[specular, blur-material, shadow.kind,
> shadow.opacity, translucency.enabled]` — **não foi descartada, e não se decide
> com um catálogo só**: seria preciso um segundo `.car` cujo `icon.json` fonte
> também se conheça. **Decidir entre as duas pelo diff seria fabricar o oráculo, e
> por isso não foi feito.**

### 43.3. O primeiro defeito que o oráculo pegou não era do renderizador

`[BIN]` Os seis SVG saem do Illustrator com o `<linearGradient>` **dentro do `<g>`
da camada**, logo antes do `<path>` — o que **pelo SVG é legal**, porque um
gradiente é referenciado por id esteja onde estiver. Mas o nosso `collectGradient`
só era chamado no ramo `else if (e.name == "defs")`.

O resultado com os assets como a Apple os gravou: **`6 of 6 layer(s) drawn`** e,
no stderr, seis vezes `url(#SVGID_1_) não resolve`. **A arte desenhava sem cor
nenhuma, e o relatório dizia que tudo tinha desenhado.**

> **É a variante mais cara do "verde vazio"**, e é exatamente o que um gabarito
> serve para pegar. O contorno foi `car_to_icon.py --hoist-gradients`, uma
> reescrita **neutra** que move o elemento para um `<defs>` no topo sem tocar em
> atributo nenhum — para que a medida fosse **sobre o renderizador e não sobre o
> leitor**. O conserto de verdade veio depois, e está no `Docs/04-o-svg.md`.

### 43.4. O diff, e a causa está no alfa

`[BIN]` **Direto, 512 contra 512:** 96,42 % dos pixels visíveis diferem, com
delta médio de alfa de **85,9**. E a causa é geométrica, não de cor:

| | corpo (`α>127`) | com sombra (`α>0`) |
|---|---|---|
| **Apple, 512** | `412×412` em `(50,50)` | `448×448` |
| **nosso, 512** | `512×512` | `512×512` |

**`412/512 = 0,8047 = 824/1024`.** A saída da Apple põe o chiclet no retângulo
interno de 824 pontos e usa os 100 que sobram para uma **sombra externa,
deslocada +2 px em y**. Nós desenhamos de borda a borda e não desenhamos sombra
externa — e `--idiom macOS` **não muda nada**, medido. (O dono desse recuo foi
achado no dia seguinte: é o `useLegacyInsetting` do §39.3.)

`[BIN]` **Alinhado, para medir o conteúdo** — render a 412 posto em `(50,50)`,
**cópia pixel a pixel, sem reamostrar**, para que nenhum erro de filtro entre na
conta. E aqui a separação entre fundo e arte sai **sem arbitragem**, porque o
fundo do ícone é cinza (`R==G==B`) e toda a arte é cromática, de modo que **a
própria saída da Apple separa os dois**:

| região | px | diferentes | delta médio |
|---|---|---|---|
| **fundo** | 93.325 | 68,3 % | **5,10** |
| **pastilhas** | 65.920 | **99,98 %** | **29,56** |

E a rampa do fundo, lida numa coluna fora das pastilhas, **bate em ±1 nível de
255 ao longo de toda a extensão**. **O gradiente de duas paradas com a
interpolação Hermite/smoothstep do §42 está certo — confirmado agora contra a
saída da Apple e não só contra o binário.** O erro é o vidro.

`[BIN]` O mapa por bloco diz onde: os dez piores blocos são **os quatro cantos do
chiclet**, e *"quatro cantos acesos simetricamente não é translação — é forma de
canto"*. Fora deles, o vermelho desenha **o contorno de cada pastilha**, que é o
realce de aresta. E o fundo mostra **listras horizontais alternando igual e
quase-igual**: a rampa acerta o valor e erra a posição da banda de quantização,
que é o que um **dither ausente** produz — e o §42.3 depois leu que o alvo, de
fato, **não dithera o gradiente**.

> **Nota de 16/09, e ela corrige a leitura mais fácil de tirar daqui:** que os
> piores blocos sejam os cantos **não faz dos cantos o maior erro**. Ver §39.4 — a
> banda de silhueta é 1,33 % dos visíveis, e um canto perfeito valeria **1,2 nível
> de RGB**.

### 43.5. A classe de tamanho, medida — e ela não morde

`[BIN]` O `.icns` dá três tamanhos e o `.car` dá o quarto, e **824/1024 vale nos
quatro** (com 128 e 32 dentro do arredondamento de um pixel). A rampa do fundo,
lida em **fração da caixa** para ser comparável, é **idêntica numa faixa de 16× de
tamanho**, dentro de ±1 no menor.

**Consequência para a armadilha `valor[3 − classe]`** (§36.2): **o fundo deste
ícone não a exercita em nenhum dos quatro tamanhos.** O projeto **supunha** isso
porque os quatro valores da tabela são iguais nesta versão; **agora é medida, não
suposição** — e `[OBS]` continua sem exercitar, porque um ícone cujos quatro
valores diferissem morderia, e ele não está neste catálogo.

### 43.6. A proveniência do 27.0-129, e por que ele NÃO é stable

O laudo que extraiu o build fez a pergunta que ninguém tinha feito, e a resposta
mudou o cabeçalho do `Docs/README.md`.

`[BIN]` **`DTSDKName` é `macosx27.0.internal` nas DUAS versões — idêntico.** Um
SDK `.internal` é assinatura de build interno, e ele **não virou `macosx27.0`
puro**. Os sinais secundários concordam sem provar sozinhos: os dois `DTXcodeBuild`
são builds de seed interna do Xcode 27, e nenhum tem a forma de um GM público.
**Se existe uma stable 27 em algum lugar, este arquivo não é ela.**

**E o vidro não mudou um byte**, por quatro provas independentes:

1. **Integridade da extração:** CRC-32 de cada `blkx` **6/6**; contadores do
   volume **64 arquivos / 45 pastas** declarados e caminhados; e
   `_CodeSignature/CodeResources` com **52 de 52 hashes conferindo, zero ausente,
   zero divergência** — com o mesmo verificador rodado contra a extração já
   validada de 2.0-125 devolvendo **exatamente os mesmos números**, o que torna o
   método auto-consistente com um baseline conhecido-bom.
2. **Os dois `default.metallib` são sha256 IDÊNTICOS**, e a lista de nomes de
   função extraída dos módulos desmontados dá **369 nomes no RenderBox e 63 no
   IconRendering, com `diff` vazio nos dois**. Nenhum shader novo, sumido ou
   renomeado.
3. **`RenderBox.arm64` é byte-idêntico em código:** 413 bytes divergem no arquivo
   inteiro, em 13 blocos contíguos, **todos no LINKEDIT** — zero bytes de
   `__text`, `__data`, `__const`, `__objc_*` ou `__got`.
4. **`IconRendering.arm64` recompilou com um deslocamento uniforme de `+0x50`**:
   toda descriptor Swift do arquivo novo está exatamente 80 bytes à frente da
   mesma descriptor no velho, o que empurra os ADRP de toda função que referencia
   string ou símbolo Swift e produz exatamente o padrão observado — milhares de
   diffs de 1–2 bytes sem tocar a opcode. Conferido em **nove endereços `[BIN]`**
   citados por laudos recentes: **sete janelas de 32 bytes byte-idênticas**, e as
   duas exceções desmontam para a **mesma instrução** com o offset deslocado de
   exatamente `0x50`.

> **Recomendação, não decisão: não trocar o corpus por este build.** Re-selar 145
> documentos por causa de um build que, medido, não mudou o vidro, é custo sem
> ganho agora. E os laudos posteriores obedeceram — todos os endereços `[BIN]`
> continuam saindo de `2.0-125`. **Que o gabarito de pixel venha do 27.0-129 não é
> desobediência: são dois papéis**, e o `Docs/README.md` os escreve em duas
> linhas.

### 43.7. O que o oráculo deixou aberto

| `[OBS]` | conteúdo |
|---|---|
| O mapa de campos de `0x3fd` tem **duas leituras vivas**, e não se decide com um catálogo só | §8.3 |
| O **segundo float de `0x3fe`** (`0,0` no grupo de trás, `0,02` nos outros dois) **não tem nome** — e o §41.1 depois provou que ele **não pode ser refração** | §8.4 |
| **Nenhum oráculo de aparência.** O `.car` tem o documento em três aparências mas **um** bitmap. E `Aqua` e `DarkAqua` são idênticos no compilado, então mesmo dois não separariam nada | §8.5 |
| A rendition `AppIcon` (layout 1010, payload `SISM`) e os `flags` do `0x3fc` não decodificados — **FECHADOS negativamente em 16/09** (§39.4): o `SISM` são 24 bytes de *multisize image set* de uma entrada 256×256, **sem geometria**, e os `flags` são **zero nos 30 registros**, nas três aparências | §8.7 |
| `CoreSVG`, `Icon Composer`, `icrtool`, `ictool`, os dois `Assets.car` e os dois appex: **tamanho idêntico, hash diferente, não desmontados** | `icon-composer-27` §6 |
| **`IconComposerKit` cresceu 88 KB e `IconComposerFoundation` 35.792 bytes, nenhum auditado.** Isso **trava reselar os 145 documentos**, porque o byte-idêntico do RenderBox não se herda para eles | `icon-composer-27` §6 |
| O **valor** do default de `_isGlass`/`_specular` — só a **existência** do campo foi reconferida | `icon-composer-27` §6 |
| CoreUI-**1007** aqui contra CoreUI-**1008** na 2.0-125, uma casa **abaixo**, causa não investigada | `icon-composer-27` §2 |

---

## 44. O orçamento de tempo, e o que uma decomposição errada custa

Esta seção não tem selo `[BIN]` nem `[ART]`, e isso é de propósito: **ela não lê o
alvo, ela mede o nosso renderizador.** O que há é `[INF]` (medição própria) e
`[OBS]`.

> **O gargalo do vidro não é a sombra. É o campo de distância.**

### 44.1. A decomposição "desliga um efeito por vez" mede errado POR CONSTRUÇÃO

**Refração, translucidez e especular comem o MESMO campo** — o portão em
`IconRenderer.cpp` é `wantsRefraction || wantsTranslucency || wantsHighlight`.
**Desligar um deixa o campo de pé, então cada um deles parece custar zero, e o
custo inteiro dos três aparece grudado em qualquer coisa que sobre.**

A saída é **inclusão-exclusão**, com uma família de variantes desenhada para isso:

```
A    todo consumidor do campo desligado -> o campo NUNCA e construido
T    campo + mascara de translucidez        S   campo + cinco realces
TS   campo + mascara + realces              TSh TS + sombra

campo = T + S - TS - A       translucidez = TS - S
especular = TS - T           sombra = TSh - TS
```

`[INF]` No documento mais pesado do corpus (10 camadas de vidro), 1024 px,
Release, mínimo de três execuções **intercaladas**:

| parte | 1024 px | fatia | escala 512→1024 |
|---|---|---|---|
| tudo que não é vidro | 0,76 s | 7 % | 2,2× |
| **o campo de distância** | **7,50 s** | **73 %** | **4,2×** |
| a sombra de vidro | 2,92 s | 28 % | **8,6×** |
| a máscara de translucidez, **dado o campo** | 0,16 s | 2 % | — |
| os cinco realces, **dado o campo** | ~0 s | ~0 % | — |
| **render completo, medido** | **10,32 s** | | |

A soma das partes dá 11,34 s contra 10,32 medidos: **resíduo de −10 %, que é ruído
de máquina e está dito como tal, não uma parte esquecida.**

`[INF]` **Duas leituras valem sozinhas.** O campo escala com os **pixels** e a
sombra escala **pior**: 4× mais pixels custam 4,2× de campo e **8,6×** de sombra —
a assinatura de um desfoque cujo σ cresce com o tamanho, `O(pixels^1,5)`. É por
isso que o problema aparece a 1024 e quase não aparece a 512. E **o especular e a
translucidez são de graça DADO o campo**: cobrar deles o campo, como a
decomposição ingênua faz, **atribui 100 % do custo a quem gasta ~2 %**.

`[INF]` **E a medida que abriu a frente foi confirmada na FORMA e não no NÚMERO.**
A tabela original dizia `sem especular 14,8 s / sem translucidez 14,4 s / sem
sombra 0,2 s`. No documento mais pesado do corpus as variantes equivalentes dão
`2,07 / 2,06 / 2,02` contra 2,34 do completo — **a mesma forma** ("desligar um dos
três não devolve nada"), outro número. **E a explicação fecha:** se um documento
tem `specular` desligado, `translucency` zero e refração identidade, o portão do
campo é falso e **o campo nunca é construído** — então a sombra, que roda **por
fora** desse portão, é literalmente todo o custo. `[OBS]` Os 15 s a 512 px **não
foram reproduzidos**: nenhum documento do corpus chega perto, e sem o `.icon` do
usuário não dá para dizer por quê. *"Registrar isso é melhor que inventar um
fator."*

### 44.2. A dispersão decide o formato do teto

A medição mais útil foi **acidental**: o censo rodou duas vezes, uma com um build
vizinho terminando e outra com a máquina quieta.

| quando | `full` a 1024 px |
|---|---|
| primeira passada, build de outra worktree terminando | **19,26 s** |
| passada limpa, três repetições | 10,59 / 10,51 / 10,85 s |

`[INF]` **Em regime a dispersão é 1,6 %; com a máquina ocupada o mesmo render
custa 1,8× mais.** O pior caso do censo inteiro espalhou **109 % em três
execuções**.

> **Isso decide tudo sobre o teto.** Um teto apertado (2×, 3×) **falha sozinho**
> numa máquina compartilhada, e seria desligado ou reexecutado até ficar verde
> dentro de uma semana — *"e aí não mede mais nada, só custa"*. Um teto de
> **catástrofe** sobrevive: precisa absorver 2× de máquina e ainda pegar 90× de
> regressão. Qualquer fator entre ~5 e ~20 serve; **escolhido 8**.
>
> `[INF]` E a conclusão contrária está escrita, o que é o que a torna confiável:
> *"não concluo que o teto é a ferramenta errada. A variação é grande em
> percentual mas pequena em ordem de grandeza — 1,8×, contra os 90× que o teto tem
> de pegar. Há quase DUAS ORDENS DE GRANDEZA de margem entre o ruído e o defeito,
> e é exatamente essa folga que torna o instrumento viável. **Se a máquina variasse
> 10× eu estaria escrevendo o `[OBS]` contrário.**"*

### 44.3. O teto, e os dois erros de calibração que ele documenta

A regra é `teto = segundos de Release × 8 × kBuildFactor`, com `kBuildFactor = 12`
sem `NDEBUG`. **O discriminador é `NDEBUG` e não uma variável de ambiente, de
propósito:** o CMake põe `-DNDEBUG` em Release e não põe em Debug, então **a
constante segue o binário que a contém e não a memória de quem executa**.

`[INF]` **O fator Debug NÃO é um número.** Medido nas mesmas três fixtures, vai de
**2,3× a 8,4×** — e uma execução **filtrada** deu **11,2×**. O Debug taxa os laços
internos (a EDT exata do campo) muito mais do que taxa o parse e a composição em
volta, então a fixture com mais campo por segundo é a de pior razão. **O `5,6×`
com que a frente foi aberta é UM PONTO dessa faixa, não a faixa.**

> **E o erro de calibração está registrado porque ele se repete.** `[INF]` A
> primeira calibração saiu de execuções **filtradas**: o mesmo caso mediu
> **0,062 s sozinho e 0,118 s dentro da suíte inteira**, com o device quente e 626
> casos atrás dele — **quase o dobro**. **Um teto calibrado sozinho e executado
> junto nasce com metade da folga que o autor acha que deu.** Os tetos definitivos
> vêm todos de execuções da suíte inteira, que é a condição em que o teto roda.

Três decisões ditas em voz alta: **512 px e não 1024**, porque é o tamanho em que
o app realmente renderiza; **nenhum caso com uma camada só**, porque um teto
construído sobre dez milissegundos *"não é teto, é detector de jitter do
escalonador"*; e **o caso do corpus escolhe o documento MEDINDO o corpus** em vez
de nomear um arquivo, **e imprime o nome quando falha**.

`[INF]` **E o teto imprime SEMPRE, passando ou falhando** — *"um orçamento que
ninguém consegue ver é um orçamento que ninguém recalibra"*.

**O controle negativo é um render de verdade, não um número falso.**
`IC_TIME_BUDGET_CONTROL=1` renderiza os casos com teto a **2048 px em vez de 512
— dezesseis vezes os pixels** — contra o **mesmo** teto. Três de três vermelhos,
código de saída 1, e **nenhum outro caso da suíte se mexe**. E ele **remede, sem
querer, a curva que o censo mediu**: o caso sem vidro vai a `0,263 → 2,576 s`
(fator 9,8, quase proporcional aos pixels, que é o que um rasterizador deve
fazer), e o caso do vidro vai a `0,229 → 11,018 s` (**fator 48**, porque a sombra
não é linear). **O controle separa as duas curvas sozinho.**

`[OBS]` O botão é o **tamanho** e não o **σ**, e a razão está escrita: o σ mora em
arquivos que uma frente irmã estava reescrevendo na mesma tarde. **O que o
controle prova é que um render lento de verdade deixa esta suíte vermelha; o que
ele não prova é que uma regressão especificamente de σ seja pega.**

---

## 45. Quatro armadilhas, medidas — o bloco de método

As quatro custaram **uma leitura errada cada**, em 15 e 16 de setembro de 2026, e
nenhuma delas estava escrita em lugar nenhum da documentação de topo. Elas ficam
aqui, fora da numeração por assunto, ao lado das duas que este documento já
registra — §28.1 ("o erro que a primeira versão cometeu") e §33.5 ("a inferência
do §11.1 está REFUTADA") —, porque são do mesmo gênero.

### 45.1. *Identical code folding*: um endereço pode carregar DOIS nomes

**O que vale é o tipo do parâmetro no sítio da chamada.**

`[BIN]` `RenderBox 0x8B624` carrega dois nomes manglados —
`RB::aliasing_mode(RB::RenderingMode)` **e** `rb_clip_mode(RBClipMode)` — porque o
*identical code folding* fundiu os dois: o corpo tem **três instruções**,
`cmp w0,#1 / cset w0,eq / ret`. No sítio que importa (`_RBDrawingStateClipLayer`,
`0x3BB64`), o argumento vai para o **quarto parâmetro** de
`Builder::clip_layer(Layer*, State&, float, ClipMode)`, então **o nome que vale ali
é o segundo**.

`[BIN]` E `ClipMode` tem exatamente **dois** valores, lidos da tabela de ponteiros
de `0x18F8A8` via `RB::XML::Value::ClipMode::to_string` (`0x130BB0`): **`0 =
normal`, `1 = inverse`**. Portanto `mode: 0` significa **"recorte normal, não
invertido"** — e nada mais.

**Duas `[INF]` morreram nisso no mesmo dia:**

| leitura derrubada | onde estava | o veredito |
|---|---|---|
| *"`mode 0` é recorte por alfa"* | `realce-vcm` §4.4, `realce-vcm-fechado` §5.3 | **meio derrubada** — acerta que não há inversão, mas o `0` **não escolhe por alfa**; a alpha é o `float`, um argumento separado |
| *"é o modo de antialiasing; máscara direta"* | `sombra-anel` §6 | **derrubada. Ele leu o nome fundido pelo ICF.** O pixel que ele desenhou continua certo; **a razão que ele deu não é a do binário**, e `kShadowRingNote` herdava essa razão |

### 45.2. Imediato contra *constant pool*: `mov`+`movk` não aparece numa varredura do pool

Esta apareceu **seis vezes em dois dias**, e cada vez custou alguma coisa.

| caso | o que o pool escondia |
|---|---|
| o recíproco `0,7843137` do chiclet (`0x3F48C8C9`) | a contagem "um leitor só" valia **só para o pool**; havia **cinco** sítios, e um deles era o codificador que decidia a curva (§39.2) |
| os dez deslocamentos do `Highlights` (`0x627B4`, `0x62588`) | o layout de **16.113 bytes** estava **escrito em imediatos** — foi assim que ele abriu (§38.2) |
| o `2,8` do `roi` do desfoque (`0xFEBA4`) | a margem de suporte da gaussiana (§37.1) |
| as constantes de reduce `−0,47265625` e `−0,765625` | a contabilidade de variância da escada (§37.2) |
| o `vibrantBrightness = 0,75` | **zero** ocorrências do padrão `0x3FE8000000000000` no arquivo; o valor nasce de um `fmov` (§36.6) |
| o **nome** `clampedPlusL` (`0xD88C`/`0xD89C`) | *small string* de Swift soletrada por `mov`/`movk` — **desta vez o que a lição escondia era um nome** (§38.6) |
| o `π` de `spatialHighlighting` (`0x125F0`), o `1024,0` do canvas (`0x42940`), o `266,24` (`0x5EAFC`), o `100/1024` (`0x4224C`), o `clampThreshold` em *small string* | todos por imediato |

**A regra prática:** varrer o `__text` reconstruindo o acumulador a cada `MOVZ`/
`MOVK`/`MOVN`, e não só olhar o `__const`. O varredor de §39.4 faz isso, e o
**controle positivo dele** é achar o `100/1024` e o `266,24` — que é o que
permitiu afirmar que o `0,225` **não está lá** em vez de "não achamos".

### 45.3. A tabela de símbolos indiretos: o `macho.py` não a lê

`[BIN]` Endereços como `0x8DA28` e `0x8D9D4` **só** se resolvem como
`_CGRectGetWidth` e `_CGRectGetHeight` pela tabela de símbolos indiretos do
`LC_DYSYMTAB`, e `scripts/macho.py` **não a lê**. Isso **quase custou um conserto
falso**: `0x8DD28` tinha sido lido como `atan2`, pelo formato de dois argumentos,
e **é `_hypot`**.

> **A diferença entre os dois é o laudo inteiro.** Com `atan2`, a pós-passagem
> `0x12550` seria função do **azimute** do realce e atenuaria cada um dos cinco por
> um fator diferente — **que é exatamente o desenho de "o realce de cima está forte
> demais"**. Com `hypot` ela é função da **latitude da luz** e trata os cinco
> igualmente. *"Uma leitura plausível e errada teria produzido um conserto bonito,
> com endereço, e falso."*

**O teste de sanidade do resolvedor** é ele devolver `_pow`, `_sin`, `_atan2` e
`___sincos_stret` nos endereços que laudos anteriores já nomeavam por outro
caminho.

E há um vizinho da mesma família: `[BIN]` `macho.py syms` é **cego** no
`IconComposerFoundation.arm64`, porque os nomes Swift vivem no
`LC_DYLD_EXPORTS_TRIE` e não no `LC_SYMTAB` — `strings` enxerga o nome e o `syms`
não devolve nada. E `xref` **só segue `BL`/`B`**: referência por `ADRP`+`ADD`/`LDR`
— como **toda** constante e **toda** string são alcançadas — passa despercebida.

### 45.4. Um nome que descreve bem demais o que você procura

`[BIN]` `renderedLegacyCompatibleIconWithConfiguration:forDeviceClass:
maskToIconShape:` parecia — e foi apontada por dois laudos — **a porta por onde a
forma do canto entraria de fora**. **Ela não é.** O tipo ObjC do seletor
(`__objc_methtype 0xA8CB0`) é `^{CGImage=}36@0:8@16q24B32`: o quinto argumento é
**`B`, um `BOOL`**, não `@`. **O chamador manda um sim/não, não uma forma** — e o
*overload* de dois argumentos é um *thunk* que faz `mov w4,#0`, isto é,
`maskToIconShape = NO` por omissão.

> **É a armadilha mais barata de cair de todas as quatro**, porque o nome contém
> literalmente as duas palavras que se está procurando. A única porta que carrega
> forma continua sendo `GlobalConfiguration.iconShape` (`0x4296C`) com o *witness*
> de `CGPath` de `0x4FA40`. **Ler o tipo do seletor é mais barato que ler o corpo,
> e teria resolvido isso na primeira vez.**
