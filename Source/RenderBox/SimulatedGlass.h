#pragma once
// O VIDRO SOB O ICONE nas rendicoes Mono: `ICRSimulatedGlassChicletLayer`.
//
// `[BIN]` O canvas do alvo (Kit `CanvasLayer.init`, 0x128D60) poe esta camada
// DEBAIXO do `ICRIconLayer`, e ela so tem area quando a rendicao e tingida ou
// clear (`validatedRendition >= 2`) e o export nao e o mitigado (Kit
// 0x1290FC-0x129140). Ela desenha uma COPIA do fundo do canvas, e sobre ela o
// icone compoe (as passadas do Clear leem o que esta atras delas, que e isto).
// Nada ali e um CABackdropLayer: e o proprio fundo, redesenhado com filtros
// (IconRendering 0x7C870, 0x8036C):
//
//   recorte na pastilha (0x803EC)
//   matriz de cor: VCM claro [0.12, 1.0, 1.2] ou escuro [0.05, 0.3, 0.8]
//       (0x807C4-0x807E0 / 0x80934-0x80960, conferidos)
//   desfoque `resultBlurRadius` = 2.0, absoluto (0x80548)
//   mapa de deslocamento `displacementMap_v1`, forca -0.28 S, faixa 0.11 S
//       (0x80584-0x805F4, o mesmo mapa da refracao das camadas)
//   desfoque `relativeBackdropBlurRadius` = 0.0065 S (0x80684)
//
// com S = o menor lado da pastilha. Valores do `SimulatedChiclet`: doc 03
// §19.3. `[INF]` A ordem dos filtros do RenderBox (ultimo acrescentado primeiro)
// e a comutacao das duas passadas de desfoque com o deslocamento: aqui os dois
// desfoques viram um so (sigma = hipotenusa) ANTES do deslocamento. `[OBS]` A
// matriz de tint do Tinted Light (0x7308) nao foi transcrita.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/ChicletShape.h"

namespace rb {

struct ClearBackdrop;

// O vidro no quadrado do canvas, numa grade `size` x `size`: cor RETA do vidro
// e alfa = cobertura da pastilha. Fora da pastilha o alfa e zero (vale o fundo).
struct SimulatedGlass {
    std::uint32_t size = 0;
    std::vector<float> rgba;
};

// `squareX/Y/Side`: o quadrado do canvas em pixels de `backdrop` (a copia da
// tela, em px CSS -- o `resultBlurRadius` de 2 pontos e 2 px CSS ali).
SimulatedGlass simulatedGlass(const ClearBackdrop& backdrop, double squareX, double squareY,
                              double squareSide, IconPlatform platform, bool dark,
                              std::uint32_t size = 512);

// O fundo como o icone o ve em (u, v) do quadrado (0..1): a copia da tela com o
// vidro por cima. Bilinear nas duas imagens.
void backdropWithGlass(const ClearBackdrop& backdrop, const SimulatedGlass* glass, double squareX,
                       double squareY, double squareSide, double u, double v, double out[3],
                       double& glassCoverage, double raw[3]);

}  // namespace rb
