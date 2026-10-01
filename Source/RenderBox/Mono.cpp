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
    // `[BIN]` Na geracao 26 os realces do chiclet ficam FORA da camada tingida
    // (IconRendering 0x43338), e o render ja recoloriu o conteudo por baixo
    // deles: `tintApplied`.
    if (mono.kind == MonoLook::Kind::TintedDark && !icon.tintApplied) {
        applyTintedDark(icon, mono.tint);
    }
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
    // `[BIN]` O Clear so existe onde a geracao tem um modo Clear: na 26 ele e
    // nil (`0x77064`) e o render nao e a mascara (`RenderedIcon::clearMask`,
    // `kClearModeNilNote`) -- o icone vai sobre o vidro como no Tinted Dark.
    if (mono.clears() && icon.clearMask) {
        applyClear(icon, backdrop, mono.squareX, mono.squareY, mono.squareSide, canvasSize,
                   glass ? &*glass : nullptr);
    } else if (glass) {
        applyOverGlass(icon, backdrop, mono.squareX, mono.squareY, mono.squareSide, canvasSize, *glass);
    }
    return {};
}

}  // namespace rb
