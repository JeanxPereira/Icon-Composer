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
#include "Source/RenderBox/GlassGlow.h"
#include "Source/RenderBox/GlassLayer.h"
#include "Source/RenderBox/GlassShadow.h"
#include "Source/RenderBox/GlassSpecular.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/PixelGrid.h"
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

// O grampo de dentro de `FieldImage` e a borda do buffer do render. Um campo
// feito numa grade menor (a estreita de IconRendererGpu.cpp) grampeia na borda
// do buffer: (x, y) e onde o pixel (0, 0) do campo cai nele, w x h o buffer.
struct FieldClamp {
    std::int32_t x = 0, y = 0;
    std::uint32_t w = 0, h = 0;
};

// Um retangulo do campo (coordenadas do campo, [x0, x1) x [y0, y1)) com uma banda
// propria: um bloco que o toca usa `band` (0 = exato) no lugar de `farBand`.
struct FieldNear {
    std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float band = 0.0f;
};

// `generateFieldFromContours`: o sinal na CPU (`fieldInsideMask`), a distancia
// exata em `icon_field.comp`. Pede `Device::float64()`.
// `farBand > 0`: alem dessa distancia (em pixels) o campo deixa de ser exato --
// sai `farBand + 1` com o sinal, o grampo da borda de dentro e a normal (1, 0) --,
// para quem so le o campo ate uma banda e satura alem dela (IconRendererGpu.cpp).
Result<ResidentField> fieldFromContours(Resident& r, const std::vector<FieldContour>& contours,
                                        std::uint32_t width, std::uint32_t height,
                                        const FieldOptions& options, std::uint32_t superSample,
                                        float farBand = 0.0f,
                                        const FieldClamp* clamp = nullptr,
                                        const FieldNear* nearRect = nullptr);

// `shadowImage` sobre a arte reta `art` (W x H), e -- com `overdrawAlpha > 0` --
// `shadowOverdrawImage` dela. O anel, a cor, a escada de `blurLadderPlan`, a
// translacao e o overdraw sao os kernels `icon_ring`, `icon_shadow` e
// `icon_blur`. Pede `Device::float64()`.
struct ResidentShadow {
    Slab image;
    Slab overdraw;   // nulo quando nao ha overdraw
};
// `parameters`: o `Shadow` da geracao -- o que o passo le dele e o
// `vibrantBrightness` (e se ele e 1, caso em que o filtro e pulado).
Result<ResidentShadow> shadow(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry, double overdrawAlpha,
                              const ShadowParameters& parameters = kShadow);

// As duas metades de `shadow`. `shadowBlur`: o anel, a cor e a escada sobre a
// grade inteira da arte (a parte cara, e a que o cache guarda). `shadowPlace`: a
// translacao e o overdraw, escritos numa grade de saida (`to`: tamanho, e onde
// o pixel (0, 0) dela cai na grade da arte) -- a composicao so le a estreita.
struct ShadowBlur {
    Slab image;          // pre-multiplicada quando `blurred`
    bool blurred = false;
};
struct ShadowOutput {
    std::uint32_t width = 0, height = 0;
    std::int32_t x = 0, y = 0;
};
Result<ShadowBlur> shadowBlur(Resident& r, const Slab& art, std::uint32_t width,
                              std::uint32_t height, ShadowStyle style,
                              const ShadowGeometry& geometry,
                              const ShadowParameters& parameters = kShadow);
Result<ResidentShadow> shadowPlace(Resident& r, const ShadowBlur& blur, const Slab& art,
                                   std::uint32_t width, std::uint32_t height,
                                   const ShadowGeometry& geometry, double overdrawAlpha,
                                   const ShadowOutput& to);
// So o overdraw de `shadowPlace`: a sombra ja posta (`placed`, na grade de saida)
// com o alfa vezes `content.a * overdrawAlpha`. A parte, porque num grupo o
// conteudo so fica pronto DEPOIS da sombra -- a mascara de translucidez entra
// nele e nao na fonte da sombra. `overdrawAlpha <= 0` devolve nulo.
Result<Slab> shadowOverdraw(Resident& r, const Slab& placed, const Slab& content,
                            std::uint32_t width, std::uint32_t height, double overdrawAlpha,
                            const ShadowOutput& to);

// `glassOpacityMask` + `opacityMaskMissedPixels` + `applyOpacityMask` sobre a
// arte `art`, lendo o campo `field` (`icon_glass_mask`). Soma os pintados em
// `counters[slot]` e os perdidos em `counters[slot + 1]`.
Result<void> glassMask(Resident& r, const Slab& art, const Slab& field, std::uint32_t width,
                       std::uint32_t height, std::int32_t originY,
                       const OpacityMaskArguments& args, const Slab& counters,
                       std::uint32_t slot);

// `glassOver(target, grid, glassDisplacementMap(field, g), g)`: `icon_displace` e
// `icon_refract`, sobre uma copia do alvo.
// Um campo (ou outra fonte) numa grade diferente da do alvo: a largura dele, e
// onde o pixel (0, 0) do alvo cai nele. Nulo: a mesma grade.
struct SourceView {
    std::uint32_t width = 0;
    std::int32_t x = 0, y = 0;
};

Result<void> refract(Resident& r, const Slab& target, const Slab& field, const PixelGrid& grid,
                     const GlassRefraction& g, const SourceView* fieldView = nullptr);

// Os realces resolvidos na CPU (`resolveHighlight`), um registro de
// `kHighlightStride` doubles por realce que pinta, na ordem dos slots. Devolve
// false quando um modo de mescla nao esta transcrito em `icon_highlight`.
// `chiclet`: os realces da pastilha, resolvidos por `resolveChicletHighlight` e
// com o termo angular do rasterizador dela (`chicletHighlightCone`: o cone de
// pi exato vira o ultimo double do registro, "aceso em todo o contorno").
constexpr std::size_t kHighlightStride = 17;
bool resolveHighlights(const HighlightSlot* slots, std::size_t count,
                       const SpecularArguments& args, std::vector<double>& records,
                       bool chiclet = false);

// `drawSpecular` (`chiclet == false`) ou `drawChicletHighlights` sobre `target`,
// lendo o campo; os pixels mudados somam em `counters[slot]`. Pede `float64()`.
Result<void> highlights(Resident& r, const Slab& target, const Slab& field, std::uint32_t width,
                        std::uint32_t height, const std::vector<double>& records, bool chiclet,
                        bool useVCM, bool clampPlusLighter, const Slab& counters,
                        std::uint32_t slot, const SourceView* fieldView = nullptr);

// `drawGlow` sobre `target` (W x H, com a origem `(originX, originY)` na grade
// do canvas), lendo o campo: `icon_glow`. Sem contagem. Pede `float64()`, e
// quem chama ja conferiu `glowDraws`.
Result<void> glow(Resident& r, const Slab& target, const Slab& field, std::uint32_t width,
                  std::uint32_t height, std::int32_t originX, std::int32_t originY,
                  const GlowArguments& args, const SourceView* fieldView = nullptr);

}  // namespace rb::gpu
