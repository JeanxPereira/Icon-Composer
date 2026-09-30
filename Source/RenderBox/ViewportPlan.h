#pragma once
// O buffer em que um render de viewport roda (spec 2026-09-16, "A margem").
//
// Tres decisoes, cada uma com o motivo medido:
//
//   1. A MARGEM E POR DOCUMENTO. O pior caso teorico (refracao, 640 pontos)
//      faria todo viewport virar o render cheio. `[ART]` So 2 dos 146
//      documentos do corpus refratam.
//   2. OS ALCANCES SE SOMAM NA CADEIA. A refracao le o backdrop, que contem a
//      sombra de grupos anteriores; um pixel do recorte so e exato com margem
//      >= a banda local MAIS a soma das refracoes.
//   3. O BUFFER E (viewport + margem) ∩ canvas, COM ORIGEM ALINHADA. O
//      desfoque, a reducao e a amostragem da refracao grampeiam na borda do
//      buffer -- que so pode ser a do canvas ou estar alem do alcance -- e a
//      escada do desfoque agrupa em caixas a partir da origem do buffer.
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/GlassTranslucency.h"
#include "Source/RenderBox/IconRenderer.h"
#include "Source/RenderBox/PixelGrid.h"

#include <cstddef>
#include <cstdint>

namespace rb {

// Acima disto o refino nao acontece e o canvas estica a base (spec, "O teto
// de area").
inline constexpr std::size_t kViewportAreaCap = 16'000'000;

// A banda que o especular e a translucidez leem do campo, em pontos. `[OBS]`
// A spec mede ~24 para o especular; 32 e o arredondamento para cima, e o gate
// e quem diz se basta.
inline constexpr double kLocalFieldBandPoints = 32.0;

// A banda que os realces da PASTILHA leem do campo, em pontos. Ela nao sai de
// grupo nenhum: a pastilha e desenhada sempre que o fundo pinta, entao ela
// entra na margem por fora do laco dos grupos. `[BIN]` `chicletMember` de
// `ChicletHighlights.cpp` da `inset = 0` nos sete slots e `distance` no maximo
// 40 pontos (os dois difusos, `0x00062C64` e `0x00062DBC`), e
// `resolveHighlight` faz `height = distance x pixelsPerPoint`, de modo que a
// banda vale 40 pontos em qualquer `size`.
//
// `[ART]` MEDIDO em 18/09: sem esta linha o gate de um documento SO com fundo
// acusa 5356 pixels e `max |d| = 0,198` a exatamente 4 px da borda interna --
// que era a margem inteira que um documento sem grupos recebia.
inline constexpr double kChicletHighlightBandPoints = 40.0;

struct DocumentReach {
    double localPoints = 0.0;         // maior banda local que nao e sombra
    double shadowSigmaPoints = 0.0;   // maior sigma de sombra
    double shadowShiftPoints = 0.0;   // maior deslocamento de sombra (+ anel)
    double chainedPoints = 0.0;       // SOMA dos alcances de refracao
};

DocumentReach documentReach(const icf::IconDocument& doc, const icf::Context& ctx,
                            IconSizeClass sizeClass);

// O produto dos fatores de reducao que `blurLadder` vai usar para este sigma,
// nivel a nivel. A origem do buffer tem que ser multipla dele.
std::uint32_t blurLadderAlignment(double sigmaPixels);

struct ViewportPlan {
    PixelGrid buffer;
    PixelGrid crop;
    // Onde o ACUMULADOR tem de estar certo: o recorte mais o alcance encadeado
    // das refracoes (a unica coisa que le o acumulador fora do proprio pixel),
    // contido no buffer. A margem da sombra e da banda local e do buffer: a
    // arte, o campo e a sombra precisam dela, o acumulador nao. So a GPU usa
    // (IconSurface.h, `begin`); com o canvas inteiro e o buffer.
    PixelGrid narrow;
    std::uint32_t marginPixels = 0;
    std::uint32_t alignment = 1;
    bool overCap = false;
};

// `lattice > 0`: as bordas do buffer de um LADRILHO caem numa grade de
// `lattice` px (para fora, grampeadas no canvas), desde que o buffer continue
// abaixo do teto. Margem a mais so custa area (o invariante vale para qualquer
// margem >= a pedida, com a origem alinhada), e dois ladrilhos vizinhos passam a
// ter o MESMO buffer -- a arte, o campo e a sombra de um servem ao outro pelo
// cache. A GPU pede (IconRendererGpu.cpp); 0 e o plano de sempre.
Result<ViewportPlan> planViewport(const IconViewport& viewport, std::uint32_t size,
                                  const DocumentReach& reach, std::uint32_t lattice = 0);

}  // namespace rb
