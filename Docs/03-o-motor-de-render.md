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
| `0.212524` | 1 × 1 | luma R SMPTE (`0.2125`) |
| `0.072083` | 1 × 1 | luma B SMPTE (`0.0721`) |
| `-0.75`, `0.75` | 1 × 1 | |
| `0.25` | 3 × 1 | **a única que discorda** |

Só de A: `0.001`, `0.300049`, `0.333252`. Só de B: `0.0001`.

### A impressão digital, e ela é específica demais para ser coincidência

`[BIN]` Os dois lados carregam **duas trincas de luma, não uma**, e na mesma
proporção:

```
Rec.709   0.2126  0.7152  0.0722   ->  half  0.212646  0.715332  0.072205
SMPTE     0.2125  0.7154  0.0721   ->  half  0.212524  0.715332  0.072083
                                                        ^^^^^^^^
                                        as duas colidem no MESMO half
```

`[BIN]` É por isso que R e B aparecem repartidos **2+1** enquanto G aparece **3**:
o G das duas trincas cai no mesmo valor em meia precisão. A contagem fecha
sozinha — e fecha **igual dos dois lados**.

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
vizinhos, e `min(join[iid], join[iid+2]) < 0` **descarta** o vértice.

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
palavra0 bit 25   um caminho alternativo de amostragem no uniforme
palavra0 bit 26   gama: no caminho de duas cores, `powr(t, arg)`; no de paradas,
                  a entrada passa de 4 para 5 halves e a quinta e o expoente
```

`[BIN]` **O layout da parada** sai do `sample_stops_uniform`: um array plano de
`half4` (RGBA), com stride **4** ou **5** conforme o bit 26.

### 23.4. E isto corrobora a medição do formato

`[ART]` O `linear-gradient` do documento tem **sempre exatamente duas paradas** —
48 de 48 no corpus.

`[INF]` Que é precisamente a rampa **tipo 0** do motor, o caminho de duas cores.
As duas medições vêm de lados opostos — uma dos documentos, outra do IR — e
descrevem a mesma coisa.

### 23.5. O que continua fechado

`[OBS]` `sample_stops_binary` foi localizado e **não** foi lido inteiro; o
caminho do bit 25 no uniforme também não. E `Gradient::color_out` (97 linhas,
noutro módulo) segue sem leitura.

`[OBS]` A regra do `automatic-gradient` — o que os seis números de §19.4 fazem
com uma cor — **não está aqui**: estas funções recebem uma rampa pronta. Quem a
deriva é o `IconRendering`, não o `RenderBox`.

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
| 0 | 4 | `inputInnerRefractionAmount` | -150 |  |
| 4 | 4 | `inputInnerRefractionHeight` | 60 | `fdiv fcsel` |
| 8 | 4 | `inputOuterRefractionAmount` | 100 |  |
| 12 | 4 | `inputOuterRefractionHeight` | 50 | `fdiv fcsel` |
| 16 | 4 | `inputRefractionDistance0` | -11 |  |
| 20 | 4 | `inputRefractionDistance1` | -3 |  |
| 24 | 4 | `inputBlurRadius` | 30 | `fmul` |
| 28 | 4 | `inputBleedBlurRadius` | 100 | `fadd` |
| 32 | 4 | `inputBleedAmount` | 400 |  |
| 36 | 4 | `inputBleedHeight` | 500 | `fdiv fcsel` |
| 40 | 4 | `inputShadowAmount` | 200 |  |
| 44 | 4 | `inputShadowHeight` | 250 | `fdiv fcsel` |
| 56 | 4 | `inputShadowBlurRadius` | 25 | `fadd` |
| 60 | 4 | `inputShadowRadius` | 25 | `fdiv fcsel` |
| 136 | 4 | `inputShadowVibrancyContribution` | 1 |  |
| 144 | 2 | `inputBlurOpacity0` | 1 |  |
| 146 | 2 | `inputBlurOpacity1` | 0.1 |  |
| 148 | 2 | `inputBlurOpacity2` | *runtime* |  |
| 150 | 2 | `inputBlurOpacity3` | 0.4 |  |
| 152 | 2 | `inputBlurDistance0` | -450 |  |
| 154 | 2 | `inputBlurDistance1` | -3 |  |
| 156 | 2 | `inputBlurDistance2` | *runtime* |  |
| 158 | 2 | `inputBlurDistance3` | *runtime* |  |
| 160 | 2 | `inputBleedDistance0` | -400 |  |
| 162 | 2 | `inputBleedDistance1` | -42 |  |
| 164 | 2 | `inputBleedOpacity` | 0.2 |  |
| 166 | 2 | `inputFaceOpacity` | 1 |  |
| 168 | 2 | `inputBleedDarkenBlend` | *runtime* | `fcsel` |
| 172 | 2 | `inputShadowDistanceOffset` | -50 |  |
| 174 | 2 | `inputShadowOpacity` | 1 |  |
| 176 | 2 | `inputRefractionOpacity` | 0.75 |  |
| 178 | 2 | `inputMaxHeadroom` | 1.2 | `fadd fdiv fsub fcsel` |
| 180 | 2 | `inputSDRGradientDistance0` | -2.5 |  |
| 182 | 2 | `inputSDRGradientDistance1` | -1.5 | `fsub fdiv` |
| 188 | 2 | `inputFaceColorMatrixMaxLuma` | 1 | `fsub fmadd fcsel fcsel fsub` |
| 190 | 2 | `inputSDRHoldingToneWhite` | 0.97 |  |
| 192 | 2 | `inputAberrationAmount` | 1 |  |
| 194 | 2 | `inputAberrationHeight` | 20 | `fdiv` |
| 196 | 2 | `inputAberrationOffset` | 1 |  |
| 202 | 2 | `inputRingShadowOffset` | *runtime* |  |
| 204 | 2 | `inputRingShadowStrokeWidth` | *runtime* |  |
| 206 | 2 | `inputRingShadowBlurRadius` | *runtime* |  |
| 208 | 2 | `inputRingShadowOpacity` | *runtime* |  |
| 210 | 2 | `inputRingShadowMask` | 1 |  |
| 212 | 2 | `inputKeyFillHighlightHeight` | *runtime* |  |
| 220 | 2 | `inputKeyFillHighlightSpread` | *runtime* | `fdiv fadd` |
| 222 | 2 | `inputKeyFillHighlightEffectOffset` | -2 |  |
| 224 | 2 | `inputKeyFillHighlightColorBias` | *runtime* |  |
| 226 | 2 | `inputBlurFillBlurRadius` | 1 | `fmul` |
| 228 | 2 | `inputBlurFillLightenOpacity` | 4 |  |
| 230 | 2 | `inputBlurFillDarkenOpacity` | *runtime* |  |
| 232 | 2 | `inputBlurFillNormalOpacity` | *runtime* |  |
| 236 | 2 | `inputBleedColorMatrixBlack` | *runtime* | `fcsel` |
| 238 | 2 | `inputBleedColorMatrixBlack` | *runtime* | `fcsel` |

### 27.5. O que NÃO está ligado, e por quê

`[ART]` Os 118 bytes restantes, nomeados em vez de preenchidos:

| faixa | bytes | leitura |
|---|---|---|
| `+64`…`+135`, `+140`…`+143` | 76 | **as três matrizes de cor.** `White`, `Black`, `Saturation` e `FillColor` **não** são gravadas chave a chave: passam pela montagem composta YCC, uma chave alimentando várias entradas. O AquaKit transcreveu essa rotina do lado do QuartzCore (`MakeYCCCompositeMatrix`); ligá-la aqui é trabalho à parte |
| `+48`…`+55` | 8 | `inputShadowOffset`, um `float2` — a ferramenta viu o store numa passada anterior e o perdeu ao endurecer a morte de registradores. `[OBS]` |
| `+170`, `+184`…`+187`, `+198`…`+201`, `+214`…`+219`, `+234`…`+235`, `+240`…`+255` | 34 | `[OBS]` não resolvidos. Alguns são as chaves de matriz; os de `+240` em diante o empacotador provavelmente escreve em bloco |

`[INF]` A cobertura de 138 de 256 **não é uma barra de progresso**: os 76 bytes
das matrizes têm produtor conhecido e transcrito noutro projeto, então o que
falta de verdade são os 42 bytes da última linha.

### 27.6. O que isto fecha

O spec do vidro (`Docs/Specs/2026-09-02-vidro.md` §6) nomeava esta ligação como o
**risco que podia não fechar**, com a regra de que sem ela o vidro ficaria nomeado
em vez de aproximado.

**Fechou.** As chaves deixam de ser hipótese: 54 delas têm byte, largura, valor
padrão e transformação, com um controle que cruza o escritor e o leitor. E o
`…Height` gravado como recíproco é o tipo de detalhe que nenhuma quantidade de
comparação visual teria encontrado.
