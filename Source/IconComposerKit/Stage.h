#pragma once
// O FUNDO DO PALCO -- o que fica atras do icone no canvas (`Background` do
// Canvas.tsx): uma cor chapada ou uma das imagens de `ui/public/apple/
// backgrounds` (as seis `sine-*` do alvo, mais as que a pessoa acrescenta).
//
// E estado de vista, como o zoom: nao esta no documento, nao e um comando e
// nao entra no undo. Mas entra na CHAVE do render do Mono, porque o vidro
// simulado e o Clear leem o que esta atras do icone.
//
// Sem Dear ImGui aqui, pela mesma razao de Session.h.
#include <cstdint>
#include <vector>

namespace ick {

struct StageBackground {
    enum class Kind { Solid, Image };
    Kind kind = Kind::Solid;
    // A cor chapada. O padrao e o `kCanvas` do tema (Theme.h), `#1e1e20` -- a
    // sexta amostra do `SOLID_COLORS` do Tauri, e o "auto" dele no escuro.
    float r = 0x1e / 255.0f, g = 0x1e / 255.0f, b = 0x20 / 255.0f;
    // O indice da imagem na `StageSource` (Widgets.h).
    int image = 0;
    bool operator==(const StageBackground&) const = default;
};

// Os pixels de uma imagem de fundo, para o render do Mono: R8G8B8A8, por linha.
struct StagePixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

// O `background-size: cover; background-position: center` do CSS: o retangulo
// da imagem (em UV, 0..1) que cobre um palco de `stageW` x `stageH`.
struct StageCover {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
};
inline StageCover stageCover(float stageW, float stageH, float imageW, float imageH) {
    StageCover c;
    if (!(stageW > 0.0f) || !(stageH > 0.0f) || !(imageW > 0.0f) || !(imageH > 0.0f)) return c;
    const float stage = stageW / stageH, image = imageW / imageH;
    if (image > stage) {
        // A imagem e mais larga que o palco: sobra dos lados.
        const float keep = stage / image;
        c.u0 = (1.0f - keep) * 0.5f;
        c.u1 = c.u0 + keep;
    } else {
        const float keep = image / stage;
        c.v0 = (1.0f - keep) * 0.5f;
        c.v1 = c.v0 + keep;
    }
    return c;
}

}  // namespace ick
