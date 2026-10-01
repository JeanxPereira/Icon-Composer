#pragma once
// `File > Export Icon as Image…`: o modal, a lista de contextos que ele
// oferece, e o motor que transforma um contexto em bytes de PNG.
//
// QUEM FAZ O QUÊ (Regra 2 da spec de arquitetura)
// -----------------------------------------------------------------------------
// O Kit renderiza e CODIFICA; ele não abre diálogo e não escreve arquivo. O
// modal monta um `ExportPlan` (Panels.h), o app pergunta onde gravar e chama
// `renderExportFile` uma vez por contexto, gravando os bytes que voltam. É por
// isso que `ExportFile` carrega `png` e não um caminho: o caminho é do app.
//
// OS BYTES TÊM DE SER OS DO `IconComposerCli`
// -----------------------------------------------------------------------------
// `renderExportFile` é a MESMA sequência que `Source/cli/render_main.cpp` faz
// no caminho de bundle -- `rb::IconRenderOptions` com o tamanho e o contexto,
// `rb::renderIcon`, `icf::encodePng` -- e nada mais. Em particular:
//
//   * canvas INTEIRO, nunca um `IconViewport`. O ladrilho existe para o canvas
//     (spec 2026-09-16) e é uma otimização de tela; um PNG exportado de um
//     ladrilho seria um recorte.
//   * `subdivisions` no padrão, que é o padrão do `IconComposerCli` também.
//   * os floats do render vão DIRETO para `encodePng`. Passar por
//     `toRgba8` (Ports.h) e voltar daria os mesmos bytes -- as duas funções
//     aplicam a mesma regra de clamp e arredondamento --, mas seria uma
//     igualdade que depende de uma coincidência em vez do caminho.
//
// `Tests/test_e2e_export.cpp` cobra essa igualdade byte a byte contra
// `iccli::renderBundleIcon` (`Source/cli/RenderBundle.h`), que é a função que o
// binário `IconComposerCli` CHAMA -- não uma transcrição dela. Divergir é defeito, não
// tolerância.
//
// A DIFERENÇA ENTRE AS DUAS COISAS, porque ela já custou uma promessa falsa: até
// 19/09 o caso comparava contra as opções de `render_main.cpp` copiadas para
// dentro do teste. As três cópias concordavam naquele dia, e nada impedia a
// divergência: um campo novo no `main` mudava o `IconComposerCli`, não mudava a UI, e o
// caso continuava verde. As linhas que decidem os bytes moraram para
// `RenderBundle.h` por isso, e agora um campo novo muda os dois lados de uma vez.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/IconComposerFoundation/Json.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/RenderBox/Device.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ick {

// O título do popup, e portanto a identidade dele para o ImGui. Compartilhado
// com quem precisa perguntar `IsPopupOpen`.
inline constexpr const char* kExportSheetTitle = "Export Icon as Image";

// OS TAMANHOS OFERECIDOS. `[INF]` Nada medido diz o que `ExportSize` do alvo
// enumera. São os dois que este editor JÁ nomeia em outro lugar -- `View >
// Preview Size` (MenuBar.cpp) --, então o modal não inventa vocabulário novo:
// o que dá para ver é o que dá para exportar.
inline constexpr std::uint32_t kExportSizes[] = {512u, 1024u};

// ─── QUAIS CONTEXTOS EXISTEM PARA ESTE DOCUMENTO ─────────────────────────────
//
// A lista NÃO é o produto cartesiano de `icf::Appearance` por `icf::Idiom`.
// Vinte caixas seriam vinte, mas a maioria delas produziria o mesmo PNG que
// outra, e uma opção que entrega a imagem da vizinha é uma mentira na tela --
// a mesma razão pela qual `ClearLight`/`ClearDark` do alvo não aparecem aqui
// (o laudo 2026-09-19-renditions-e-mirroring.md §2 mostra que `Clear` não é
// sequer uma aparência, e os 15 campos de `ICRRenderingParameters.ClearMode`
// não foram lidos, então este renderizador não sabe fazer Clear).
//
// O que se pode SABER sem renderizar:
//
//   IDIOMA. `idiom` chega ao render por um caminho só: `icf::resolve`. Uma
//   varredura de `Source/RenderBox/` não acha uma única leitura de
//   `ctx.idiom` -- nenhuma. Então um documento que não escreve `idiom` em
//   especialização nenhuma desenha IGUAL nos cinco idiomas, e oferecer cinco
//   caixas seria oferecer a mesma imagem cinco vezes. A lista é: o idioma que
//   o documento DECLARA (`declaredIdiom`, ViewModel.h) mais todo idioma que
//   ele nomeia numa especialização.
//
//   APARÊNCIA. Aqui é diferente: `appearance` chega ao render também por fora
//   do `resolve`, em `FillResolve.cpp`. E lá está a única igualdade que o
//   próprio conversor declara -- `[BIN]` `0x10AEF4`, um `cmp w8, #2` com
//   `b.lo` sem sinal, faz `base` e `light` caírem no MESMO braço. Então
//   `light` só é oferecido quando o documento escreve `appearance: "light"`
//   em alguma especialização; sem isso ele é `base` com outro nome. `dark` e
//   `tinted` têm braço próprio e são sempre oferecidos.
//
// O QUE ISSO NÃO PROVA, dito em vez de escondido: que todo par que sobra
// difere. Um documento cujo `fill` é um sólido sem especialização nenhuma
// desenha o mesmo fundo em `base` e em `dark`, e saber isso exigiria os dois
// renders -- que é exatamente o que a pessoa está pedindo. O que a lista
// remove é o que se sabe idêntico ANTES de renderizar.
std::vector<icf::Idiom> exportIdioms(const icf::json::Value& root);
std::vector<icf::Appearance> exportAppearances(const icf::json::Value& root);
// O produto das duas listas, idioma por fora, na ordem em que o modal desenha.
std::vector<icf::Context> exportContexts(const icf::json::Value& root);

// "Dark / macOS" -- o que a pessoa lê na caixa.
std::string exportContextLabel(icf::Context ctx);

// O NOME DO ARQUIVO. `[INF]` Nada medido diz a convenção do alvo, então esta
// é escolhida por ser a única que não inventa palavra nenhuma: o nome do
// bundle sem o `.icon`, as duas palavras do contexto NA GRAFIA DO PRÓPRIO
// DOCUMENTO (`icf::appearanceToString` / `icf::idiomToString`, que são as que
// o formato escreve em disco), e o tamanho. `AppIcon-27-dark-macOS-1024.png`.
// Nenhum selo, nenhum "exported by".
std::string exportFileName(std::string_view stem, icf::Context ctx, std::uint32_t size);
// O `stem`: o nome do bundle sem a extensão `.icon`.
std::string exportStem(const Session& s);

// UM PNG PRONTO, MENOS A GRAVAÇÃO.
struct ExportFile {
    icf::Context context;
    std::string name;                 // só o nome; a pasta é do app
    std::vector<std::uint8_t> png;    // vazio quando `error` não está
    std::string error;                // não-vazio: nada mais aqui é válido
    std::size_t drawn = 0, total = 0;
    std::vector<std::string> notes;   // o que foi desenhado sem ter sido medido
};

ExportFile renderExportFile(rb::Device& device, const icf::IconBundle& bundle,
                            std::string_view stem, std::uint32_t size, icf::Context ctx);

// ─── O MODAL ─────────────────────────────────────────────────────────────────
//
// O QUE ELE GRAVA, e por quê: o mesmo contrato de `RowInfo::rowAt` e
// `MenuItemInfo::at`. Entre o pixel e o pedido existem o retângulo do
// controle, o popup que o contém e a ordem em que os dois são submetidos, e um
// teste que escrevesse `actions.exportImage = true` na mão provaria o app e
// passaria com o botão desligado.
struct ExportSheetStats {
    bool open = false;               // o popup desenhou neste quadro
    std::size_t options = 0;         // contextos oferecidos
    std::size_t chosen = 0;          // deles, marcados
    std::uint32_t size = 0;          // o tamanho selecionado
    // Uma entrada por controle desenhado: os botões de tamanho, uma caixa por
    // contexto (rotulada como `exportContextLabel`), Export e Close.
    std::vector<MenuItemInfo> controls;
    ImVec2 exportAt{0.0f, 0.0f}, closeAt{0.0f, 0.0f};
    std::string status;              // a frase do app, como ela foi para a tela
    // Os nomes que esta exportação escreveria, na ordem, só os marcados. É o
    // que o modal mostra ao lado de cada caixa -- o nome é uma decisão `[INF]`
    // e esconder uma decisão dessas atrás de um botão seria pior do que
    // tomá-la.
    std::vector<std::string> names;
};

// Desenhado FORA de qualquer janela (escopo de raiz), depois do canvas: um
// popup nasce no stack de IDs de quem o abre, e o modal não é do canvas -- ele
// cobre a janela inteira. Não faz nada com `exportSheet.open` falso.
ExportSheetStats drawExportSheet(Session& s, MenuActions& actions);

}  // namespace ick
