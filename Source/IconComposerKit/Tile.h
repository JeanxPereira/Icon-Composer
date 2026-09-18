#pragma once
// O pedaco da grade de `size` que o canvas quer ver (spec 2026-09-16, "O que o
// Kit faz"). `w == 0` e o canvas inteiro -- o pedido de sempre.
//
// Um header proprio porque `Session.h` (o modelo, sem ImGui) e `Ports.h` (a
// fronteira com a janela, com ImGui) precisam do mesmo tipo.
#include <cstdint>

namespace ick {

struct TileRect {
    std::int32_t x = 0, y = 0;
    std::uint32_t w = 0, h = 0;
    bool operator==(const TileRect&) const = default;
};

}  // namespace ick
