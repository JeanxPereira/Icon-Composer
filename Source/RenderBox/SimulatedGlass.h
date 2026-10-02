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

// A LENTE, PARA QUEM COMPOE NA GPU DA TELA (02/10). O que do vidro simulado nao
// depende do fundo nem de onde o icone esta: o mapa de deslocamento da
// pastilha, a cobertura dela e as constantes do `SimulatedChiclet`. O editor
// sobe isto como textura e faz o resto por quadro num fragment shader
// (Source/app/shaders/stage.frag), em vez de `simulatedGlass` na CPU.
struct SimulatedGlassLens {
    std::uint32_t size = 0;
    // Por texel: (disp.x, disp.y, cobertura da pastilha, mascara da lente).
    // `disp` e o do `displacementMap_v1`: 0.5 e deslocamento zero.
    std::vector<float> rgba;
    // A forca do deslocamento, em pixels da grade de `size` (ja negada).
    float scalePixels = 0.0f;
    // O menor lado da pastilha, em pixels da grade: o `S` dos raios relativos.
    double side = 0.0;
    // `relativeBackdropBlurRadius` e `resultBlurRadius` [BIN]: o desfoque e
    // hypot(relativo * S, absoluto), o primeiro em pixels de `S` e o segundo
    // em pontos da tela.
    double relativeBlur = 0.0, absoluteBlur = 0.0;
    std::uint32_t variant = 0;   // a superamostragem da refracao
};
SimulatedGlassLens simulatedGlassLens(IconPlatform platform, std::uint32_t size = 512);

// As tres matrizes de cor do Mono, [piso, teto, saturacao] de `applyGlyphVCM`:
// a do vidro (clara e escura) e as duas do Clear, com o teto do `inputClamp`.
struct MonoColourMatrices {
    double glassLight[3], glassDark[3], clearLighten[3], clearHighlight[3];
    double clearDarkening;   // `total D`
    double vcmMin, vcmMax;   // o grampo de cada canal depois da matriz
};
MonoColourMatrices monoColourMatrices();

// O fundo como o icone o ve em (u, v) do quadrado (0..1): a copia da tela com o
// vidro por cima. Bilinear nas duas imagens.
void backdropWithGlass(const ClearBackdrop& backdrop, const SimulatedGlass* glass, double squareX,
                       double squareY, double squareSide, double u, double v, double out[3],
                       double& glassCoverage, double raw[3]);

}  // namespace rb
