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
// `DockBuilderSetNodeSize` de fato mova o nó do dock. Isso é ImGui com uma
// janela de verdade, e `Source/app` não é linkado por `ic_tests`. O que este
// arquivo garante é que os números que chegam lá são os certos; que eles
// chegam está em `Source/app/Window.cpp`, `clampDockedWidths`.
//
// E O QUE A MEDIÇÃO MUDOU NESTE ARQUIVO (revisão 19/09, N5/N6). A sonda de
// docking derrubou a premissa de que o nó redimensiona proporcionalmente com a
// janela: o ramo que vale é o 3 de `DockNodeTreeUpdatePosSize`, o de tamanho
// absoluto (a conta está em `WindowLayout.h`). Dois casos daqui afirmavam o
// contrário no nome e testavam, no corpo, um "proporcional" que eles mesmos
// calculavam -- passavam com o comportamento afirmado desligado. Saíram, e no
// lugar deles estão os grampos que existem.
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
// O GRAMPO DO REDIMENSIONAMENTO NÃO DESFAZ O ARRASTO (revisão 19/09, N5/N6).
//
// AQUI ESTAVAM DOIS CASOS QUE NÃO PODIAM REPROVAR. Eles se chamavam
// `..._the_measured_ceiling_bites_when_the_window_grows` e
// `..._the_measured_floor_bites_when_the_window_shrinks`, e os corpos
// calculavam o "proporcional" (`330.0f/1080.0f*grown`) eles mesmos, para então
// conferir que `std::clamp` grampeia um número que o próprio caso inventou.
// Como o nó de dock NÃO redimensiona proporcionalmente -- ramo 3 de
// `DockNodeTreeUpdatePosSize`, medido --, os dois passavam com o comportamento
// que afirmavam desligado. Uma rede que não pode reprovar é pior que rede
// nenhuma, porque ela diz que está tudo bem.
//
// O que passou a ser cobrado é o grampo que existe: `capPanelShare`, o único
// que `clampDockedWidths` aplica. Ele só ENCOLHE, e é essa metade que carrega
// a decisão de comportamento -- um divisor que a pessoa arrastou para 600 pt
// fica em 600 enquanto couber na metade da janela.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_the_resize_cap_only_shrinks_and_never_undoes_a_drag) {
    // 600 pt arrastados numa janela de 1920: cabem na metade, e ficam.
    CHECK(near(ick::capPanelShare(600.0f, 1920.0f), 600.0f));
    // A mesma sidebar numa janela de 1000 passaria de metade, e aí o grampo
    // morde -- mas até a metade, não até o `kSidebarMax` de 480.
    CHECK(near(ick::capPanelShare(600.0f, 1000.0f), 500.0f));
    CHECK(ick::kSidebarMax < 500.0f);   // é a diferença entre os dois grampos
    // E ele NUNCA cresce um painel: uma sidebar estreitada para 200 não é
    // puxada de volta para o ideal por cima do gesto de quem a estreitou.
    CHECK(near(ick::capPanelShare(200.0f, 1920.0f), 200.0f));
    CHECK(near(ick::capPanelShare(ick::kSidebarIdeal, 1080.0f), ick::kSidebarIdeal));
    // Span degenerado não vira divisão por zero nem largura negativa.
    CHECK(near(ick::capPanelShare(330.0f, 0.0f), 0.5f));
    CHECK(near(ick::capPanelShare(330.0f, -10.0f), 0.5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// O QUE SALVA O CANVAS NUMA JANELA ESTREITA É `kPanelShareMax`, E ELE É `[DEC]`.
//
// Medido sem grampo, a 700 pt o canvas fica com 32 px; com o grampo, com
// 165,5. Quem faz isso não é nenhum dos quatro `[BIN]` de mínimo/máximo: é
// metade do espaço, um número deste projeto. Esta é a conta dessa linha da
// medição -- sidebar 329 (absoluta, o nó não a mexe), inspetor contra o que
// sobra.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_the_half_window_cap_is_what_saves_the_canvas) {
    const float narrow = 700.0f;
    const float sidebar = ick::capPanelShare(329.0f, narrow);
    CHECK(near(sidebar, 329.0f));   // 329 < 350: a sidebar cabe na metade
    const float after = narrow - sidebar;
    const float inspector = ick::capPanelShare(329.0f, after);
    CHECK(near(inspector, after * 0.5f));   // 185,5
    CHECK(near(inspector, 185.5f));
    // O canvas fica com o que sobra, e é isso que o grampo compra.
    CHECK(near(narrow - sidebar - inspector, 185.5f));
    // Sem o grampo o inspetor ficaria com os 319 que a sonda mediu, e o canvas
    // com 52 -- a mesma ordem de grandeza dos 32 px medidos na árvore real.
    CHECK(narrow - sidebar - 319.0f < 60.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// E O INTERVALO MEDIDO NÃO TEM COMO MORDER O IDEAL -- dito como caso, porque é
// a afirmação que os dois casos removidos escondiam. Se alguém mexer no ideal
// sem mexer no intervalo, ou vice-versa, é esta linha que reprova.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE(window_layout_the_measured_band_contains_the_ideal_so_it_never_bites) {
    CHECK(ick::kSidebarMin <= ick::kSidebarIdeal);
    CHECK(ick::kSidebarIdeal <= ick::kSidebarMax);
    CHECK(ick::kInspectorMin <= ick::kInspectorIdeal);
    CHECK(ick::kInspectorIdeal <= ick::kInspectorMax);
    // Logo a largura inicial é o ideal, em toda janela larga o bastante --
    // e é a única largura que o nó de dock vai ter, porque ele não a refaz.
    for (const float total : {1080.0f, 1440.0f, 1920.0f, 2560.0f}) {
        CHECK(near(ick::clampSidebarWidth(ick::kSidebarIdeal, total), ick::kSidebarIdeal));
        CHECK(near(ick::clampInspectorWidth(ick::kInspectorIdeal, total - ick::kSidebarIdeal),
                   ick::kInspectorIdeal));
    }
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
