#include "Source/RenderBox/Mono.h"

#include "Source/RenderBox/ChicletShape.h"
#include "Source/RenderBox/SimulatedGlass.h"

#include <optional>

namespace rb {

void prepareMono(IconRenderOptions& io, const MonoLook& mono) {
    if (mono.kind == MonoLook::Kind::TintedDark) {
        io.tint = mono.tint;
        return;
    }
    // O Clear tambem nao e `.color`: a sombra vira `neutral` (0x49F40). Uma
    // recoloracao identidade liga so esse portao.
    io.tint = IconRenderOptions::TintRecolour{};
    io.clearMask = true;
}

std::string finishMono(RenderedIcon& icon, const MonoLook& mono, const ClearBackdrop& backdrop,
                       icf::Idiom idiom, std::uint32_t canvasSize) {
    if (mono.kind == MonoLook::Kind::TintedDark) applyTintedDark(icon, mono.tint);
    const bool square = mono.squareSide > 0.0;
    if (mono.clears() && !square) return "clear sem o quadrado no fundo";
    if (square && backdrop.width == 0) return "clear sem backdrop";
    // O vidro simulado sob o icone, nas quatro (SimulatedGlass.h). A aparencia
    // escura escolhe o VCM.
    std::optional<SimulatedGlass> glass;
    if (square) {
        glass = simulatedGlass(backdrop, mono.squareX, mono.squareY, mono.squareSide, iconPlatformOf(idiom),
                               mono.dark());
    }
    if (mono.clears()) {
        applyClear(icon, backdrop, mono.squareX, mono.squareY, mono.squareSide, canvasSize,
                   glass ? &*glass : nullptr);
    } else if (glass) {
        applyOverGlass(icon, backdrop, mono.squareX, mono.squareY, mono.squareSide, canvasSize, *glass);
    }
    return {};
}

}  // namespace rb
