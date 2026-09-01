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
