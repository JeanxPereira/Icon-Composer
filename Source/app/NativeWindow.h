#pragma once
// A janela sem moldura do sistema, com o comportamento de uma que tem: arrastar
// pela barra de titulo, redimensionar pela borda, Aero Snap, a sombra e os
// cantos arredondados do Windows 11.
//
// A barra de titulo e desenhada pelo app em ImGui (as luzes, os menus, o
// titulo); o que o sistema precisa saber e so ONDE ela esta, para responder
// `HTCAPTION` ali -- e so onde nao ha um item do ImGui debaixo do cursor, ou
// um clique num menu arrastaria a janela em vez de abri-lo.
//
// Fora do Windows isto nao faz nada e a janela fica sem moldura mesmo.
struct GLFWwindow;

namespace icapp::NativeWindow {

void install(GLFWwindow* window);
// Em pixels de tela, contados do topo da area cliente.
void setTitleBarHeight(float px);
// O nome da janela ImGui que desenha a barra: so ela e "barra" para o sistema.
inline constexpr const char* kHostWindow = "##IconComposerHost";

}  // namespace icapp::NativeWindow
