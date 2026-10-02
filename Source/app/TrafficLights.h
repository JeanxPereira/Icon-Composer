#pragma once
// As luzes da janela do macOS 27, com os assets DECODADOS do alvo, e nao
// circulos desenhados: o braco imagem do `MacWindowControlElement`
// (AquaKit lab/export/traffic-lights/README.md, §1-§4), o mesmo que o
// ui/src/TrafficLights.tsx do Tauri usa.
//
// A LOGICA, PECA POR PECA, como o README de 01/10 a fecha (os dois laudos de
// 30/09 refeitos):
//
// - O ESTADO E POR LUZ (§3.2, `_bezelInteractionState`): `pressed` quando ESTA
//   luz esta apertada com o ponteiro em cima dela (`isHighlighted`); senao
//   `rollover` quando o ponteiro esta no GRUPO -- o hover e do grupo, e as tres
//   entram juntas; senao `idle`. Arrastar para fora com o botao preso devolve
//   a luz ao `idle`.
// - Corpo (§2): o bitmap `WindowControlBodies/<Close|Minimize|Expand>` de 22 pt
//   (o disco de 14 no meio e a sombra em volta), ou `Inactive` quando a luz
//   esta `isDimmed = idle && !janelaAtiva`. So variante e `isDimmed` escolhem
//   o corpo: hover e pressed nao o trocam.
// - Glifo (§3): o vetor `WindowControls/*` como mascara, na cor da tabela §3.3
//   (preto quando `isDimmed`). A opacidade e 0 em `idle` e 1 de `rollover` em
//   diante; o ponto de "nao salvo" (`CloseUnsaved`) e isento e fica sempre. A
//   troca e animada por `spring(response: 0.1, dampingFraction: 1.0)` sobre
//   `interactionState > 1` (§4.1).
// - Verde (§2.1): fora da tela cheia, as setas de entrar, ou o "+" do zoom com
//   Option (aqui, Alt); em tela cheia, as setas de sair. A tela cheia desta
//   janela e a maximizada, que e o que o botao faz no Windows.
// - Flex (§4.1, `Settings.windowControl`): com a luz apertada o controle
//   inteiro sobe para (14 + 2) / 14 do tamanho, por uma mola
//   (`scaleSpringResponse` 0.355, `scaleSpringDamping` 0.4), e volta ao soltar.
//   Os brilhos do Flex sao zero nesta variante, e o puxao pelo arrasto divide
//   por 3825 pt -- nao chega a um pixel, e nao esta aqui.
//
// Os PNG/SVG sao da Apple e nao entram no git (ui/.gitignore); moram em
// `ui/public/apple/traffic/`, procurados a partir do executavel. Sem eles, os
// discos sao desenhados chapados nas cores dos corpos, e o editor avisa uma vez
// no stderr.
#include "Source/RenderBox/Device.h"
#include "Source/app/TexturePool.h"

#include "imgui.h"

#include <filesystem>
#include <string>

namespace icapp {

class Shell;

class TrafficLights {
public:
    // Carrega os assets. Nunca falha: o que faltar cai no desenho chapado.
    void load(TexturePool& pool, rb::Device& device, float dpiScale);
    // Desenha o grupo com o canto de cima-esquerda do disco da primeira luz
    // em `topLeft` (pixels de tela). Devolve a largura do grupo.
    float draw(Shell& shell, ImVec2 topLeft, float scale, bool dirty);

private:
    struct Tex {
        ImTextureID id = 0;
    };
    // Uma mola do SwiftUI (`response`, `dampingFraction`), integrada por quadro.
    struct Spring {
        float value = 0.0f, velocity = 0.0f;
        void step(float target, float response, float damping, float dt);
    };
    Tex close_, minimize_, expand_, inactive_;
    Tex glyphClose_, glyphUnsaved_, glyphMinimize_, glyphZoom_, glyphFullEnter_, glyphFullExit_;
    bool loaded_ = false;
    // Por luz: a opacidade do glifo e a escala do Flex.
    Spring glyph_[3];
    Spring scale_[3] = {{1.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f}};
};

// A pasta dos assets da Apple que o Tauri usa (`ui/public/apple`), procurada
// subindo a partir do executavel; `IC_APPLE_ASSETS` passa por cima. Vazio
// quando nao ha.
std::filesystem::path appleAssetsDir();

}  // namespace icapp
