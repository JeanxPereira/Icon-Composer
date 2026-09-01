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

### 8.1. São QUATRO passes, e nenhum fragment shader

`[BIN]` O `shader_path.metal` não define fragment shader nenhum. Define quatro
**vertex shaders**, e os varyings de cada um dizem para que serve:

| passe | emite além de `position` | leitura |
|---|---|---|
| `path_edges_vertex` | nada | estêncil da borda |
| `path_interior_vertex` | nada | estêncil do interior |
| `path_exterior_vertex` | `path_y` (float2), `path_slope` (float) | **antialiasing analítico**: posição relativa à aresta e inclinação dela dão a cobertura exata do pixel |
| `path_distance_vertex` | `path_p` (float2), `path_p1p2` (float2) | o campo de distância |

`[INF]` Interior e borda emitindo **só posição** é a assinatura de passes de
estêncil: a cobertura não sai de um fragment shader, sai da decomposição da
geometria. Quem carrega o antialiasing é o `exterior`, com a inclinação da
aresta.

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
