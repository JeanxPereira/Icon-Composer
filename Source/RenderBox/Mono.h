#pragma once
// O MONO: as quatro rendicoes do alvo que leem a fatia `tinted` do documento
// e diferem no RENDER, nao no documento -- Clear Light, Clear Dark, Tinted
// Light e Tinted Dark (laudo de 30/09 §5; os commits `b48a44f`, `6337a98` e
// `2cbef18`, que as trouxeram ao `icserver`).
//
// Em duas fases, porque o render do meio e de quem chama (a GPU ou a CPU, o
// documento com ou sem efeitos, o cache):
//
//   prepareMono -- antes: o Tinted Dark leva a recoloracao, os outros tres a
//                  mascara do Clear (e a recoloracao identidade, que so liga o
//                  portao da sombra neutra, 0x49F40);
//   finishMono  -- depois: a recoloracao dos pixels, o vidro simulado sob o
//                  icone e o Clear (ou o icone sobre o vidro, no Tinted Dark --
//                  e em toda rendicao da geracao 26, que nao tem modo Clear).
//
// O `icserver` e o editor chamam as mesmas duas, e e isso que garante que o
// canvas do ImGui e o do Tauri desenham o mesmo Mono.
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/IconRenderer.h"

#include <string>

namespace rb {

struct MonoLook {
    enum class Kind { ClearLight, ClearDark, TintedLight, TintedDark };
    Kind kind = Kind::ClearLight;
    // So o Tinted Dark: a cor do espectro e a saturacao (Tint.tsx / doc.ts).
    IconRenderOptions::TintRecolour tint;
    // O quadrado do icone NO FUNDO, em pixels do fundo. Lado zero: sem vidro
    // (so o Tinted Dark aceita isso; o Clear sem o fundo nao tem o que ler).
    double squareX = 0.0, squareY = 0.0, squareSide = 0.0;

    bool dark() const { return kind == Kind::ClearDark || kind == Kind::TintedDark; }
    bool clears() const { return kind != Kind::TintedDark; }
    bool operator==(const MonoLook&) const = default;
};

void prepareMono(IconRenderOptions& io, const MonoLook& mono);

// Vazio no sucesso; senao o motivo (o Clear pedido sem fundo).
// `canvasSize` e a grade do render (`io.size`), a mesma que o quadrado mede.
std::string finishMono(RenderedIcon& icon, const MonoLook& mono, const ClearBackdrop& backdrop,
                       icf::Idiom idiom, std::uint32_t canvasSize);

}  // namespace rb
