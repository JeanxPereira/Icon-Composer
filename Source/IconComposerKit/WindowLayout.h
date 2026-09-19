#pragma once
// AS LARGURAS DOS DOIS PAINÉIS, E O GRAMPO QUE AS FAZ VALER.
//
// `[BIN]` As seis constantes são do alvo, lidas em 19/09 de
// `WindowLayoutConstants` no slice `IconComposerKit.arm64`
// (`Docs/Laudos/2026-09-19-renditions-e-mirroring.md` §4.2): a sidebar quer
// 330 pt, entre 250 e 480; o inspetor quer 330 pt -- que é também o mínimo
// dele --, até 500.
//
// POR QUE ISTO É UMA FUNÇÃO PURA NO KIT E NÃO UMA LAMBDA NO `defaultLayout`
// -----------------------------------------------------------------------------
// Porque lá ela era INERTE, e ninguém podia ter percebido. O que a T8 instalou
// foi `std::clamp(want, lo, hi)` com `want` SEMPRE igual ao ideal -- e o ideal
// já está dentro de `[lo, hi]` nos dois painéis, então os quatro números de
// mínimo e máximo não podiam mover um pixel. Quatro dos nove `[BIN]` entraram
// no código como decoração, e o comentário ao lado vendia o piso e o teto como
// aplicados. `Source/app` não é linkado por `ic_tests` e `defaultLayout` não
// aparecia em caso nenhum (revisão 19/09, I2 e M4).
//
// Aqui a aritmética é pura, é testada, e o `want` é o que o painel TEM: no
// primeiro layout é o ideal do alvo, e depois é a largura em que o nó do dock
// ficou. É nessa segunda chamada que o piso e o teto existem para alguma coisa
// -- `DockBuilderSplitNode` guarda uma RAZÃO, e um nó de dock redimensiona
// proporcionalmente com a janela.
//
// O QUE O GRAMPO **NÃO** FAZ, dito em vez de suposto: ele não persegue o
// arrasto do divisor. `Source/app/Window.cpp` o aplica quando a JANELA muda de
// tamanho, que é quando a razão empurraria o painel para fora do intervalo
// medido; mover o divisor com o mouse é uma escolha da pessoa e não é desfeita
// no quadro seguinte.
#include <algorithm>

namespace ick {

// `[BIN]` §4.2 do laudo de 19/09.
inline constexpr float kSidebarIdeal = 330.0f, kSidebarMin = 250.0f, kSidebarMax = 480.0f;
inline constexpr float kInspectorIdeal = 330.0f, kInspectorMin = 330.0f, kInspectorMax = 500.0f;

// O teto FINAL, e ele não é do alvo: metade do espaço que existe. Numa janela
// estreita demais para o mínimo medido, 250 pt de sidebar não deixariam canvas
// nenhum, e o alvo supõe um monitor que esta máquina não tem (o
// `Window.minContentSize` de 1284 pt está no laudo e não virou grampo aqui,
// pela mesma razão).
inline constexpr float kPanelShareMax = 0.5f;
// E o piso da RAZÃO, para `DockBuilderSplitNode` nunca receber zero.
inline constexpr float kDockRatioMin = 0.05f;

// A largura que a sidebar pode ter numa janela de `total` pontos, dada a que
// ela tem agora.
inline float clampSidebarWidth(float want, float total) {
    const float span = total > 1.0f ? total : 1.0f;
    return std::min(std::clamp(want, kSidebarMin, kSidebarMax), span * kPanelShareMax);
}

// O mesmo para o inspetor, contra o que SOBROU depois da sidebar -- que é
// contra o que ele é dividido.
inline float clampInspectorWidth(float want, float afterSidebar) {
    const float span = afterSidebar > 1.0f ? afterSidebar : 1.0f;
    return std::min(std::clamp(want, kInspectorMin, kInspectorMax), span * kPanelShareMax);
}

// Pixels em razão, que é o que `DockBuilderSplitNode` pede.
inline float dockRatio(float px, float span) {
    const float s = span > 1.0f ? span : 1.0f;
    return std::clamp(px / s, kDockRatioMin, kPanelShareMax);
}

}  // namespace ick
