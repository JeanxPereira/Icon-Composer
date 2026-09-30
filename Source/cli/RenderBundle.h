#pragma once
// O CAMINHO DE BUNDLE DO `icrender`, COMO FUNÇÃO EM VEZ DE TRECHO DE `main`.
//
// POR QUE ISTO EXISTE (revisão 19/09, I6)
// -----------------------------------------------------------------------------
// `Source/IconComposerKit/Export.h` promete, no cabeçalho, que
// `Tests/test_e2e_export.cpp` "cobra essa igualdade byte a byte contra a
// sequência do `icrender`". O que o caso fazia era comparar contra uma
// TRANSCRIÇÃO do `render_main.cpp` escrita dentro do próprio teste. As três
// cópias concordavam em 19/09, e nada impedia a divergência de amanhã: se
// `render_main.cpp` ganhasse um campo -- um `sizeClass`, um `viewport`, um
// `subdivisions` diferente --, o `icrender` mudaria, a UI não, e o caso
// continuaria verde comparando a UI com a cópia que o teste tinha do que o
// `icrender` fazia naquele dia.
//
// Um portão que compara duas coisas contra uma terceira não é um portão. Então
// as linhas que DECIDEM OS BYTES saíram do `main` e vieram para cá: o binário
// `icrender` chama esta função, o caso chama esta função, e a promessa do
// cabeçalho passa a ser verdade por construção. Um campo novo aqui muda os dois
// lados de uma vez, e é o caso que reprova.
//
// O QUE FICOU NO `main`, de propósito: o argv, o relógio, o `stdout`/`stderr` e
// a gravação em disco. Nada disso muda um byte do PNG, e trazer o `writePng`
// para cá faria esta função conhecer um caminho de arquivo -- que é a única
// coisa que `renderExportFile` (o lado da UI) nunca pode fazer.
#include "Source/IconComposerFoundation/IconBundle.h"
#include "Source/IconComposerFoundation/IconDocument.h"
#include "Source/RenderBox/Device.h"
#include "Source/RenderBox/IconRenderer.h"

#include <cstdint>

namespace iccli {

// As opções que o `icrender` monta para um bundle, e o render. `subdivisions`
// é o do `--subdivisions` (padrão 16); tudo o mais em `IconRenderOptions` fica
// no padrão -- em particular NÃO há `viewport`: um PNG exportado de um ladrilho
// seria um recorte.
// `gpu` troca `rb::renderIcon` por `rb::renderIconGpu` com AS MESMAS opcoes
// (`icrender --gpu`); `cache` e o `RenderCache` de quem mede frio e quente
// (`icrender --repeat`). Os dois no padrao sao o render que decide os bytes.
rb::Result<rb::RenderedIcon> renderBundleIcon(rb::Device& device, const icf::IconBundle& bundle,
                                              std::uint32_t size, int subdivisions,
                                              icf::Context context, bool gpu = false,
                                              rb::RenderCache* cache = nullptr);

}  // namespace iccli
