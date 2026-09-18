#pragma once
// A grade de pixels em que um render roda.
//
// `IconRenderOptions::size` carregava TRES significados ao mesmo tempo: a
// resolucao do canvas (quantos pixels valem os 1024 pontos), a dimensao do
// buffer e a origem implicita em (0,0). Esta struct os separa (spec
// 2026-09-16, "O que muda no contrato"). Uma funcao que recebe uma `PixelGrid`
// e diz que usa extensao ou origem; uma que recebe o escalar `size` usa escala
// e nada mais -- e e o compilador quem enumera os sitios que mudaram.
#include <cstddef>
#include <cstdint>

namespace rb {

struct PixelGrid {
    std::uint32_t size = 0;                  // 1024 pontos de canvas valem `size` pixels
    std::int32_t originX = 0, originY = 0;   // canto do buffer, na grade de `size`
    std::uint32_t width = 0, height = 0;     // extensao do buffer

    static PixelGrid full(std::uint32_t s) { return PixelGrid{s, 0, 0, s, s}; }
    std::size_t texels() const { return static_cast<std::size_t>(width) * height; }
    bool isFull() const {
        return originX == 0 && originY == 0 && width == size && height == size;
    }
};

}  // namespace rb
