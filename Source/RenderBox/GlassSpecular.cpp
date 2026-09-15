#include "Source/RenderBox/GlassSpecular.h"

namespace rb {

SpecularBand specularBand(const SpecularBand& declared, bool inside) {
    if (inside) return declared;
    // `[BIN]` `fsub d0, d2, d1` at `0x000495E0` is `inset - height`, with
    // `d1 == height` (`+0x30`) and `d2 == inset` (`+0x38`) loaded together by
    // the `ldp d1, d2, [x21, #-0x10]` at `0x000495C0`. The subtraction is
    // written in that order, not as `-(height - inset)`, because that is the
    // order it was read and this project's differentials are bit-for-bit.
    SpecularBand out = declared;
    out.inset = declared.inset - declared.height;
    out.curvature = 0.0;
    return out;
}

bool specularPlacementBit(SpecularPlacement placement, bool identityRecolour) {
    switch (placement) {
        case SpecularPlacement::Inside:
            return true;
        case SpecularPlacement::Outside:
            return false;
        case SpecularPlacement::Automatic:
            break;
    }
    // `[BIN]` `0x00049280`-`0x000492C0`. The three tests are ANDed, and the
    // middle one is an OR of five 64-bit loads compared against zero -- so any
    // one of them non-zero takes the automatic case to `outside`.
    return identityRecolour;
}

bool specularDrawsInside(SpecularPlacement placement, bool identityRecolour, bool isDarklight,
                         bool hasOutsetOpacity) {
    // `[BIN]` The `csinc` at `0x000494F8` tests the Optional tag FIRST in
    // effect: tag == 1 (nil) forces the result to 1 regardless of `w10`.
    if (!hasOutsetOpacity) return true;
    // `[BIN]` `eor w10, w12, #1` then `orr w10, w11, w10` at `0x000494E4` /
    // `0x000494EC`: a bright highlight contributes a literal 1 to the OR.
    if (!isDarklight) return true;
    return specularPlacementBit(placement, identityRecolour);
}

bool documentAsksForSpecular(const GlassMaterial& material) { return material.hasSpecular; }

bool documentAsksForSpecular(const DenormalisedGlass& glass) { return glass.hasSpecular; }

const char* specularDoesNotDrawNote() {
    return "este documento pede um especular e ele NAO desenha: `[BIN]` o brilho desemboca no "
           "shader `glassHighlight` do metallib do IconRendering (0xE834, literal em 0xE92C, "
           "drawShape em 0xED00) e `[OBS]` os nove valores que esse shader consome -- height, "
           "inset, curvature, spread, bias, direction, color, opacity, blendMode -- vem de "
           "ICRRenderingParameters.Highlights, 16113 bytes construidos em 0x62A78-0x63C1C e nao "
           "lidos; o documento so aporta o portao e um bit de colocacao, nenhuma magnitude";
}

}  // namespace rb
