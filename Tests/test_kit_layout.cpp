// AS LARGURAS DOS DOIS PAINÉIS (T8, e a revisão de 19/09 que a corrigiu).
//
// POR QUE ESTE ARQUIVO EXISTE
// -----------------------------------------------------------------------------
// A T8 entregou nove números `[BIN]` e zero casos, contra a restrição global 3
// do plano ("todo trabalho de UI entrega caso E2E"). Quatro desses nove -- os
// mínimos e os máximos -- foram instalados de um jeito que os tornava INERTES:
// `std::clamp(want, lo, hi)` com `want` sempre igual ao ideal, e o ideal já
// dentro do intervalo. O grampo não podia mover um pixel, e o comentário ao
// lado vendia o piso e o teto como aplicados. Um caso sobre esta aritmética
// teria pego isso na hora -- ela é pura.
//
// O que NÃO dá para cobrar aqui, dito em vez de suposto: que
// `DockBuilderSetNodeSize` de fato mova o nó do dock quando a janela cresce.
// Isso é ImGui com uma janela de verdade, e `Source/app` não é linkado por
// `ic_tests`. O que este arquivo garante é que os números que chegam lá são os
// certos; que eles chegam está em `Source/app/Window.cpp`, `clampDockedWidths`.
#include "check.h"
#include "Source/IconComposerKit/WindowLayout.h"

#include <cmath>

namespace {
bool near(float a, float b, float slack = 0.01f) { return std::fabs(a - b) <= slack; }
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// A JANELA EM QUE ESTE EDITOR ABRE. O monitor retrato desta máquina tem 1080 pt
// de largura, que é o número que o próprio comentário de `defaultLayout` cita.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_gives_the_target_widths_on_the_window_that_exists) {
    const float total = 1080.0f;
    const float sidebar = ick::clampSidebarWidth(ick::kSidebarIdeal, total);
    CHECK(near(sidebar, 330.0f));
    CHECK(near(ick::dockRatio(sidebar, total), 330.0f / 1080.0f, 1e-4f));

    // O inspetor é dividido contra o que SOBROU, não contra a janela inteira.
    const float after = total - sidebar;
    const float inspector = ick::clampInspectorWidth(ick::kInspectorIdeal, after);
    CHECK(near(inspector, 330.0f));
    CHECK(near(ick::dockRatio(inspector, after), 330.0f / 750.0f, 1e-4f));
}

// ─────────────────────────────────────────────────────────────────────────────
// O TETO MORDE QUANDO A JANELA CRESCE -- o cenário exato do laudo. Uma razão de
// 330/1080 numa janela maximizada para 1920 dá ~587 pt de sidebar, acima do
// teto medido de 480. Era aqui que os quatro números inertes deveriam estar
// agindo e não estavam.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_the_measured_ceiling_bites_when_the_window_grows) {
    const float grown = 1920.0f;
    const float proportional = 330.0f / 1080.0f * grown;   // ~586,7: o que o nó vira sozinho
    CHECK(proportional > ick::kSidebarMax);
    CHECK(near(ick::clampSidebarWidth(proportional, grown), ick::kSidebarMax));

    const float after = grown - ick::kSidebarMax;
    const float inspectorProportional = 330.0f / 750.0f * after;   // ~633,6
    CHECK(inspectorProportional > ick::kInspectorMax);
    CHECK(near(ick::clampInspectorWidth(inspectorProportional, after), ick::kInspectorMax));
}

// ─────────────────────────────────────────────────────────────────────────────
// E O PISO MORDE QUANDO ELA ENCOLHE. 330/1080 numa janela de 700 dá ~214 pt,
// abaixo do mínimo medido de 250 -- que é o defeito que o comentário de
// `defaultLayout` DESCREVE ("a sidebar passava abaixo dos 250 do alvo sem
// resistencia") e que ele afirmava ter consertado.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_the_measured_floor_bites_when_the_window_shrinks) {
    const float narrow = 700.0f;
    const float proportional = 330.0f / 1080.0f * narrow;   // ~213,9
    CHECK(proportional < ick::kSidebarMin);
    CHECK(near(ick::clampSidebarWidth(proportional, narrow), ick::kSidebarMin));
    // E o piso não passa por cima do teto da janela: 250 numa janela de 700 é
    // menos que a metade dela, então ele vale inteiro.
    CHECK(ick::kSidebarMin < narrow * 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// NUMA JANELA ESTREITA DEMAIS PARA O MÍNIMO DO ALVO, QUEM GANHA É A JANELA.
// O alvo supõe um monitor largo (`Window.minContentSize` é 1284 pt e está no
// laudo justamente como uma medição NÃO aplicada). 250 pt de sidebar numa
// janela de 400 não deixariam canvas nenhum.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_never_lets_a_panel_eat_more_than_half_the_window) {
    CHECK(near(ick::clampSidebarWidth(ick::kSidebarIdeal, 400.0f), 200.0f));
    CHECK(near(ick::clampSidebarWidth(ick::kSidebarMin, 400.0f), 200.0f));
    CHECK(near(ick::clampInspectorWidth(ick::kInspectorIdeal, 300.0f), 150.0f));
    // E a razão nunca sai de [0,05; 0,5], nem com uma largura absurda nem com
    // um span degenerado -- `DockBuilderSplitNode` com zero não divide nada.
    CHECK(near(ick::dockRatio(5000.0f, 1000.0f), 0.5f));
    CHECK(near(ick::dockRatio(1.0f, 1000.0f), 0.05f));
    CHECK(near(ick::dockRatio(330.0f, 0.0f), 0.5f));
    CHECK(near(ick::dockRatio(330.0f, -10.0f), 0.5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// OS NOVE NÚMEROS `[BIN]`, escritos, contra o laudo §4.2. Se alguém "arredondar"
// um deles, esta é a linha que diz.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_constants_are_the_measured_ones) {
    CHECK(near(ick::kSidebarIdeal, 330.0f));
    CHECK(near(ick::kSidebarMin, 250.0f));
    CHECK(near(ick::kSidebarMax, 480.0f));
    CHECK(near(ick::kInspectorIdeal, 330.0f));
    CHECK(near(ick::kInspectorMin, 330.0f));
    CHECK(near(ick::kInspectorMax, 500.0f));
    // E o ideal do inspetor É o mínimo dele, que é a assimetria medida entre os
    // dois painéis e a razão de os dois pares não serem um par só.
    CHECK(near(ick::kInspectorIdeal, ick::kInspectorMin));
}
