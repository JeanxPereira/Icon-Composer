# Plano — a cadeia do render na GPU

*29/09/2026. A casca Tauri mostra o quadro do `icserver` (núcleo C++). Com o
cache quente o Kenzu sai em ~12 ms e o Apollo em ~60 ms a 512 px, mas o
primeiro render, a troca de aparência e cada ladrilho de zoom são frios (0,05
a 1,2 s). A cadeia é CPU com uma passada de cobertura na GPU. Tempo real pede
a cadeia inteira na GPU, residente, com um readback só no fim.*

## O que já existe

- `glslc` no build (`Source/RenderBox/CMakeLists.txt`) e `ComputePass`
  (`Compute.h`).
- As transcrições GLSL dos shaders da Apple, hoje usadas como oráculo:
  `GlassForeground.glsl` (realces), `GlassBackground.glsl`,
  `DistanceField.glsl`, `SdfDisplacement.glsl`, `Displacement.glsl`,
  `Gradient.glsl`, `Path*.glsl`.
- A cobertura dos caminhos SVG já é desenhada na GPU (`CoveragePass`).

## Restrições globais

1. **O caminho de CPU continua, e é o gabarito.** A exportação e o `icrender`
   seguem nele; o gate de SHA do corpus continua valendo para ele.
2. **Fidelidade medida, não byte a byte.** A GPU calcula em float e a CPU em
   double em vários pontos. Cada task roda o corpus inteiro a 512 px pelos
   dois caminhos e reporta, por documento, o erro médio e o pior pixel (em
   níveis de 8 bits). Teto: **média ≤ 0,5 nível, pior pixel ≤ 4 níveis**, em
   todo documento. Um passo acima do teto não entra; a exceção é dita com o
   documento e o motivo.
3. **Residente.** As camadas vivem como imagens na GPU do começo ao fim. Uma
   etapa ainda não portada roda na CPU com upload/readback, e isso é
   declarado; a meta de cada task é tirar uma dessas idas e voltas.
4. **Medido.** Cada task reporta o tempo do Kenzu e do Apollo a 512 e 1024 px,
   frio e quente, antes e depois.
5. Commits em português, no estilo do `git log`, sem atribuição.

## Tasks

### G1 — O esqueleto residente

`renderIconGpu` (mesma assinatura e mesmo `RenderedIcon` de `renderIcon`),
com as camadas em imagens da GPU: tinta (fill override) aplicada à cobertura,
fundo com a pastilha e os degradês, composição com os modos de mescla
transcritos, readback único no fim. As etapas de vidro (campo, sombra,
translucidez, especular, realces da pastilha) entram pela CPU com
upload/readback. O harness de fidelidade (restrição 2) nasce aqui.

### G2 — O campo na GPU

A distância exata ao contorno (a grade de segmentos de `f05df7d`) em compute,
com o mesmo sinal (a regra de preenchimento da cobertura).

### G3 — A sombra na GPU

Anel, desfoque separável, deslocamento e overdraw.

### G4 — O vidro na GPU

Máscara de translucidez, especular e os realces da pastilha, sobre os
`.glsl` que já existem.

### G5 — O `icserver` no caminho GPU

O servidor passa a usar `renderIconGpu`; o cache se adapta ao que ficar
residente. A meta: ladrilho de zoom e troca de aparência abaixo de 16 ms no
Kenzu a 1024 px.
