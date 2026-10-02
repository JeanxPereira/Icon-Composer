#pragma once
// "Abrir com Icon Composer" no menu de contexto do Explorer, para pastas `.icon`.
//
// POR QUE UM VERBO DE PASTA E NAO UMA ASSOCIACAO. Um `.icon` e um DIRETORIO, e
// o Windows so associa programa a extensao de ARQUIVO: toda pasta, tenha o nome
// que tiver, e da classe `Directory`. O que existe e pendurar um verbo nessa
// classe e filtra-lo pelo nome (`AppliesTo`), e e isso que esta aqui:
//
//   HKCU\Software\Classes\Directory\shell\IconComposer
//       MUIVerb   = @<exe>,-101          o rotulo, da tabela de strings do exe
//       AppliesTo = System.FileName:"*.icon"
//       Icon      = "<exe>",0
//       \command  = "<exe>" "%1"
//
// O ROTULO E DO SISTEMA, NAO DO APP: `MUIVerb` aponta para a string 101 do
// proprio executavel (Resources/IconComposer.rc), que tem uma tabela por
// idioma, e o Explorer escolhe a do idioma de exibicao de quem esta logado.
//
// `HKCU`: so o usuario corrente, sem elevacao, e desfazer e apagar a chave. O
// caminho do exe fica gravado, entao um exe movido deixa de estar registrado
// -- `contextMenuRegistered` compara o comando com o exe que esta rodando.
//
// So Windows. Nos outros `contextMenuSupported` e falso e o app nao oferece.
#include <string>

namespace icapp {

// O id da string do rotulo na tabela do executavel.
inline constexpr int kContextMenuVerbString = 101;

bool contextMenuSupported();
// O verbo existe E aponta para este executavel.
bool contextMenuRegistered();
// Liga ou desliga. Vazio quando deu certo; senao, o que falhou.
std::string setContextMenu(bool enabled);

}  // namespace icapp
