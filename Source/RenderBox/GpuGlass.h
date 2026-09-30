#pragma once
// O VIDRO NO CAMINHO RESIDENTE (frente GPU, G2-G4 -- `Docs/Plans/2026-09-29-render-gpu.md`).
//
// As etapas de vidro de `renderIcon` -- o campo de distancia, a sombra, a mascara
// de translucidez, a refracao, o especular e os realces da pastilha -- como
// passos gravados no lote de `Resident`, sobre buffers que nunca descem. Cada
// funcao aqui e a de CPU que ela espelha, com o nome dela no comentario, e o
// gabarito e sempre a de CPU (`icfidelity`).
//
// Interno: so IconRendererGpu.cpp inclui.
#include <cstdint>
#include <vector>

#include "Source/RenderBox/BlendMode.h"
#include "Source/RenderBox/DistanceField.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GpuResident.h"

namespace rb::gpu {

// Um campo `(d, gx, gy, cobertura)` num buffer da GPU, no layout de `FieldImage`.
// `width == 0` e o campo vazio de `generateFieldFromContours`.
struct ResidentField {
    Slab data;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::int32_t originX = 0;
    std::int32_t originY = 0;
};

// `generateFieldFromContours`: o sinal na CPU (`fieldInsideMask`), a distancia
// exata em `icon_field.comp`. Pede `Device::float64()`.
Result<ResidentField> fieldFromContours(Resident& r, const std::vector<FieldContour>& contours,
                                        std::uint32_t width, std::uint32_t height,
                                        const FieldOptions& options, std::uint32_t superSample);

// `shadowImage` sobre a arte reta `art` (W x H), e -- com `overdrawAlpha > 0` --
// `shadowOverdrawImage` dela. O anel, a cor, a escada de `blurLadderPlan`, a
// translacao e o overdraw sao os kernels `icon_ring`, `icon_shadow` e
// `icon_blur`. Pede `Device::float64()`.
struct ResidentShadow {
    Slab image;
    Slab overdraw;   // nulo quando nao ha overdraw
};
Result<ResidentShadow> shadow(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry, double overdrawAlpha);

}  // namespace rb::gpu
