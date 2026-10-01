#pragma once
// O TETO DE FIDELIDADE DA FRENTE GPU, COMO FUNCAO (restricao 2 de
// `Docs/Plans/2026-09-29-render-gpu.md`).
//
// A GPU calcula em float onde a CPU calcula em double, entao `renderIconGpu` nao
// e byte a byte com `renderIcon`. O que o plano cobra e um TETO, medido por
// documento, em niveis de 8 bits: erro medio <= 0,5 e pior pixel <= 4. Os dois
// lados vao para bytes com a MESMA conversao do PNG (`toByte` de Png.cpp:
// grampo e `v * 255 + 0,5` truncado), e a diferenca e tomada canal a canal nos
// quatro canais.
//
// Mora numa biblioteca pela razao de `RenderBundle.h`: o caso da suite
// (`Tests/test_gpu_fidelity.cpp`) e a ferramenta que varre o corpus inteiro em
// Release (`icfidelity`) medem com a MESMA funcao, e uma copia em cada lugar
// deixaria de concordar.
#include <cstdint>
#include <string>
#include <vector>

#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"

namespace iccli {

inline constexpr double kFidelityMeanCeiling = 0.5;
inline constexpr int kFidelityMaxCeiling = 4;

struct FidelityStats {
    double mean = 0.0;          // media de |d| sobre todos os canais, em niveis
    int max = 0;                // pior |d| de um canal
    std::uint32_t worstX = 0, worstY = 0;
    int worstChannel = 0;
    std::size_t over = 0;       // canais com |d| acima do teto de pior pixel
    bool sameShape = true;      // largura, altura e metadados iguais
    std::string shapeWhy;       // quando `sameShape` e falso, o que diferiu

    bool withinCeiling() const {
        return sameShape && mean <= kFidelityMeanCeiling && max <= kFidelityMaxCeiling;
    }
};

// A conversao do PNG, copiada de `toByte` (Png.cpp) -- a mesma para os dois lados.
std::uint8_t fidelityByte(float v);

// `a` contra `b`: pixels e o relatorio (`drawn`, `skipped`, `shapeGaps`, `notes`
// e os contadores de vidro), que tem de ser IGUAIS -- as decisoes sao as mesmas.
FidelityStats compareIcons(const rb::RenderedIcon& a, const rb::RenderedIcon& b);

// Um documento pelos dois caminhos, com as opcoes do `icrender` a `size` -- e a
// geracao de design dele (`icfidelity --generation`): os dois caminhos tem de
// concordar nas duas.
rb::Result<FidelityStats> fidelityOf(
    rb::Device& device, const icf::IconBundle& bundle, std::uint32_t size,
    icf::Context context = {}, rb::DesignGeneration generation = rb::DesignGeneration::G27);

}  // namespace iccli
