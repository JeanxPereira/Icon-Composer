#pragma once
// As luzes da janela do macOS 27, com os assets DECODADOS do alvo, e nao
// circulos desenhados: o braco imagem do `MacWindowControlElement`
// (AquaKit lab/export/traffic-lights/README.md, §1-§4), o mesmo que o
// ui/src/TrafficLights.tsx do Tauri usa.
//
// - Corpo: o bitmap `WindowControlBodies/<Close|Minimize|Expand>` de 22 pt (o
//   disco de 14 no meio e a sombra em volta), ou `Inactive` quando a janela
//   nao esta em foco e o grupo esta ocioso (`isDimmed`, §3.2). Hover e pressed
//   nao mudam o corpo (§2).
// - Glifo: o vetor `WindowControls/*` como mascara, na cor da tabela §3.3.
//   Aparece so com o GRUPO em rollover ou um botao pressionado; o ponto de "nao
//   salvo" (`CloseUnsaved`) fica sempre.
// - Verde: o "+" do zoom com Alt, senao as setas de tela cheia (§2.1). Aqui
//   ele maximiza, que e o que uma janela do Windows tem no lugar da tela cheia.
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
    Tex close_, minimize_, expand_, inactive_;
    Tex glyphClose_, glyphUnsaved_, glyphMinimize_, glyphZoom_, glyphFullEnter_;
    bool loaded_ = false;
    bool pressedAny_ = false;
};

// A pasta dos assets da Apple que o Tauri usa (`ui/public/apple`), procurada
// subindo a partir do executavel; `IC_APPLE_ASSETS` passa por cima. Vazio
// quando nao ha.
std::filesystem::path appleAssetsDir();

}  // namespace icapp
