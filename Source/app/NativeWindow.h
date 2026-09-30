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
#include <cstdint>

struct GLFWwindow;

namespace icapp::NativeWindow {

void install(GLFWwindow* window);
// O tratamento comum das mensagens da janela sem moldura, para as duas
// cascas: a area cliente, o teste de acerto, o cursor do ImGui e a ativacao.
// Verdadeiro quando tratou, e `result` e o que devolver. So no Windows.
bool handleBorderless(void* hwnd, unsigned msg, std::uintptr_t wp, std::intptr_t lp, std::intptr_t& result);
// Em pixels de tela, contados do topo da area cliente.
void setTitleBarHeight(float px);
// Os retangulos da barra que sao CONTROLES (as luzes, os menus), em pixels da
// area cliente, refeitos a cada quadro. Ali o sistema responde HTCLIENT mesmo
// sem item do ImGui sob o cursor: sem isso o primeiro movimento sobre uma luz
// ja chegava como HTCAPTION, o Windows o mandava como mensagem de moldura, a
// GLFW nao o repassava ao ImGui -- e o botao nunca ficava hovered, porque
// nunca via o cursor. Medido 30/09: os glifos das luzes nao apareciam.
void clearControlRects();
void addControlRect(float x0, float y0, float x1, float y1);
// O nome da janela ImGui que desenha a barra: so ela e "barra" para o sistema.
inline constexpr const char* kHostWindow = "##IconComposerHost";

}  // namespace icapp::NativeWindow
