#pragma once
// AS LARGURAS DOS DOIS PAINÉIS, E O GRAMPO QUE AS FAZ VALER.
//
// `[BIN]` As seis constantes são do alvo, lidas em 19/09 de
// `WindowLayoutConstants` no slice `IconComposerKit.arm64`
// (`Docs/Laudos/2026-09-19-renditions-e-mirroring.md` §4.2): a sidebar quer
// 330 pt, entre 250 e 480; o inspetor quer 330 pt -- que é também o mínimo
// dele --, até 500.
//
// O QUE O NÓ DE DOCK FAZ DE VERDADE QUANDO A JANELA MUDA DE TAMANHO -- MEDIDO
// -----------------------------------------------------------------------------
// Ele NÃO redimensiona proporcionalmente. Até 19/09 este arquivo, o comentário
// de `clampDockedWidths` e o de `defaultLayout` afirmavam os três que
// `DockBuilderSplitNode` guarda uma razão e que o nó a segue com a janela, e a
// sonda de docking da re-revisão (o imgui desta árvore compilado à parte, com
// o mesmo layout de quatro splits) mede o contrário. Em
// `DockNodeTreeUpdatePosSize` o ramo que vale para esta árvore é o **3**
// (`imgui.cpp:20329`):
//
//     else if (child_0->SizeRef[axis] != 0.0f && child_1->HasCentralNodeChild)
//         child_0_size[axis] = ImMin(size_avail - size_min_each, child_0->SizeRef[axis]);
//
// O ramo 4, o proporcional, só é alcançado quando NENHUM dos dois filhos
// carrega o nó central; `defaultLayout` cria o dockspace com
// `ImGuiDockNodeFlags_DockSpace`, o que põe o nó central na raiz, e
// `DockNodeTreeSplit` passa a flag ao herdeiro, que é sempre o `centre`. Logo o
// irmão da sidebar e o irmão do inspetor SEMPRE têm `HasCentralNodeChild`, os
// dois painéis ficam com largura ABSOLUTA, e quem absorve a mudança da janela é
// o canvas. Medido, sem grampo nenhum:
//
//     abre 1080    layers=329.0   maximiza 1920  layers=329.0   estreita 700  layers=329.0
//
// TRÊS CONSEQUÊNCIAS, ditas em vez de escondidas:
//
//   1. A razão derivada do ideal MELHORA a largura inicial, e isso é real: a
//      1080 pt ela dá 330, contra os 216 que os 20% herdados do `sfsymview`
//      davam. É este o ganho dos `[BIN]`, e é só na primeira montagem.
//   2. `kSidebarMin` e `kInspectorMin` NÃO MORDEM. Com largura absoluta um
//      painel só desce abaixo do mínimo medido quando `size_avail -
//      size_min_each` cai abaixo dele, e aí `kPanelShareMax` -- metade da
//      janela -- já mordeu antes. A 700 pt medidos: o inspetor cai a 319,
//      `clamp(319, 330, 500)` o sobe a 330, e `min(330; 185,5)` joga os 330
//      fora. Os dois pisos são a forma escrita do intervalo do alvo, não um
//      grampo que age.
//   3. `kSidebarMax`/`kInspectorMax` também não mordem contra a janela -- a
//      única coisa que eles alcançavam era DESFAZER O ARRASTO DA PESSOA:
//      arrastar o divisor para 600 pt e redimensionar a janela devolvia 480 no
//      quadro seguinte. Era exatamente o que este comentário prometia não
//      fazer. Um grampo que desfaz o gesto de quem usa é defeito, não recurso,
//      e por isso ele saiu do caminho do redimensionamento.
//
// A DIVISÃO QUE FICOU, então, tem dois grampos com dois papéis distintos:
//
//   * `clampSidebarWidth` / `clampInspectorWidth` -- a LARGURA INICIAL, uma
//     vez, em `defaultLayout`. É onde o intervalo `[min, max]` do alvo é o
//     contrato, mesmo que o ideal já caiba nele.
//   * `capPanelShare` -- o REDIMENSIONAMENTO, em `clampDockedWidths`. Só o
//     teto de metade do espaço, e só para BAIXO: é ele, e mais ninguém, que
//     salva o canvas numa janela estreita (a 700 pt medidos, 32 px de canvas
//     sem ele contra 165,5 com ele). E, por só encolher, uma sidebar que a
//     pessoa arrastou para 600 continua com 600 enquanto couber.
//
// `kPanelShareMax` é `[DEC]` deste projeto, não `[BIN]` do alvo -- o número que
// de fato decide no redimensionamento é nosso, e é honesto dizer isso.
//
// POR QUE A ARITMÉTICA MORA NO KIT E NÃO NUMA LAMBDA EM `Source/app`: porque lá
// ela não é linkada por `ic_tests` e nenhum caso a alcançava, que é como os
// quatro números conseguiram passar por inertes sem ninguém ver (revisão 19/09,
// I2 e M4). Aqui ela é pura e é cobrada em `Tests/test_kit_layout.cpp`.
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

// A largura INICIAL da sidebar numa janela de `total` pontos. Uma vez, em
// `defaultLayout`. `want` é o ideal do alvo, e o intervalo medido é o contrato
// escrito em volta dele -- com o ideal dentro de `[250; 480]` ele não morde
// hoje, e é para morder se alguém mexer no ideal sem mexer no intervalo.
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

// O GRAMPO DO REDIMENSIONAMENTO, e ele é só o teto de metade do espaço.
//
// Só encolhe: `want` é a largura em que o nó do dock ESTÁ, e ela chegou ali ou
// pela montagem inicial ou pelo divisor que a pessoa arrastou. Devolver um
// número MAIOR que `want` seria puxar o painel de volta para o ideal por cima
// do gesto dela; devolver um número menor só acontece quando o painel passaria
// de metade do que existe, que é o caso em que o canvas fica sem nada. O
// intervalo `[min, max]` do alvo não entra aqui de propósito -- ver o cabeçalho
// deste arquivo, ponto 3.
inline float capPanelShare(float want, float span) {
    const float s = span > 1.0f ? span : 1.0f;
    return std::min(want, s * kPanelShareMax);
}

// Pixels em razão, que é o que `DockBuilderSplitNode` pede.
inline float dockRatio(float px, float span) {
    const float s = span > 1.0f ? span : 1.0f;
    return std::clamp(px / s, kDockRatioMin, kPanelShareMax);
}

}  // namespace ick
