#include "Source/app/Window.h"

#include "Source/IconComposerKit/Export.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Renditions.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/Theme.h"
#include "Source/IconComposerKit/Widgets.h"
#include "Source/IconComposerKit/WindowLayout.h"
#include "Source/app/AppPorts.h"
#include "Source/app/Dialogs.h"
#include "Source/app/JobQueue.h"
#include "Source/app/NativeWindow.h"
#include "Source/app/Shell.h"
#include "Source/app/Symbols.h"
#include "Source/app/TrafficLights.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace icapp {
namespace {

namespace fs = std::filesystem;

// A `.icon` is a DIRECTORY (spec 13/09 §4), and the dialog that asks for one
// is `SystemOpenBundleDialog` (AppPorts.h), which on Windows hands back the
// folder itself. This stays because the answer is not a bundle on every path
// into it: the fallback dialog off Windows still returns a FILE, and a file
// inside the bundle names the bundle -- picking `Foo.icon/icon.json` opens
// `Foo.icon`. A folder comes back unchanged.
fs::path bundleDirOf(const fs::path& picked) {
    std::error_code ec;
    if (fs::is_directory(picked, ec)) return picked;
    const fs::path parent = picked.parent_path();
    return parent.empty() ? picked : parent;
}

// Everything the panels share, owned by run() so destruction order is stated
// once: the coordinator returns its texture before the sink that owns the pool
// goes, and the sink goes before the Shell's device (the Shell outlives this).
struct State {
    Shell* shell = nullptr;
    std::optional<ick::Session> session;
    // O MESMO dispositivo que o agendador usa. A exportação renderiza na
    // thread principal (ver `drainExport`), e não por um `RenderScheduler`:
    // aquele canal é de UM render de cada vez e o mais novo descarta o
    // anterior (Ports.h), que é exatamente o contrário de uma fila de seis
    // que tem de sair inteira -- e o PNG sai dos FLOATS do render (Export.h),
    // que `RenderResult` não carrega.
    //
    // E PORTANTO A EXCLUSÃO TEM DE SER PEDIDA, NÃO SUPOSTA. Até 19/09 este
    // comentário dizia que "o `JobQueue` serializa a raia, então dois renders
    // nunca dividem o dispositivo -- e aqui nem há dois, porque este roda
    // entre quadros". As duas metades eram falsas: o `JobQueue` serializa a
    // raia DELE e não a thread do frame, e "entre quadros" descreve a posição
    // no código, não exclusão mútua -- o job é assíncrono e atravessa quadros
    // (é por isso que `~JobScheduler` espera em `!working_`). Clicar Export
    // com uma miniatura em voo punha um render de 1024 px nesta thread por
    // cima de um de 128 px num worker, sobre o MESMO `rb::Device`, cujo
    // `VkCommandPool` e cuja `VkQueue` são de sincronização externa.
    //
    // `drainExport` agora arrenda o dispositivo do multiplexador
    // (`SharedScheduler::tryLease`, Renditions.h) antes de tocar nele.
    rb::Device* device = nullptr;
    std::unique_ptr<PoolTextureSink> sink;
    std::unique_ptr<JobScheduler> scheduler;
    // DOIS CONSUMIDORES, UM AGENDADOR (T4). O canal do `RenderScheduler` é de
    // um resultado por vez e `JobScheduler` é UMA raia e UM dispositivo: um
    // segundo `JobScheduler` no mesmo `JobQueue` renderizaria em paralelo no
    // mesmo `rb::Device`, e dois consumidores no mesmo `poll()` roubariam o
    // resultado um do outro em silêncio. O multiplexador (Renditions.h) dá uma
    // `RenderScheduler` virtual para cada um, com a prioridade no canvas.
    std::unique_ptr<ick::SharedScheduler> mux;
    std::unique_ptr<ick::RenderCoordinator> coordinator;
    std::unique_ptr<ick::RenditionThumbnails> thumbs;
    ick::MenuActions actions;
    bool quit = false;
    bool showDiagnostics = false;
    // WHY A DROP IS QUEUED AND NOT ACTED ON. The shell delivers it from inside
    // its message pump (Shell.h, `onDrop`), and `adopt()` destroys the session the
    // panels after it are about to draw -- the same reason `act()` runs LAST,
    // from the diagnostics panel. The callback only records; `act()` opens.
    std::optional<fs::path> dropped;
    // The last thing that went wrong on a path the person took deliberately,
    // shown on the empty canvas because a GUI has no stderr. The folder picker
    // used to answer "" for a cancel and for a COM failure alike, and the two
    // are indistinguishable to whoever is clicking.
    //
    // E DESDE 19/09 ELE E DESENHADO COM DOCUMENTO ABERTO TAMBEM. Ate aqui
    // `trouble` so aparecia no canvas VAZIO -- sumia exatamente quando ha
    // documento, que e o unico momento em que salvar pode falhar (laudo 18/09
    // §4.2). As falhas de escrita abaixo escreviam so em `stderr`, que uma
    // janela nao tem. Agora toda saida de `act()` que nao e um cancelamento
    // passa por `fail()`, e as duas superficies do Kit a mostram: uma linha
    // vermelha elidida na barra do canvas, a frase inteira no Diagnostics.
    std::string trouble;
    // Uma falha: para a tela E para o stderr. O stderr fica porque este
    // binario tambem roda como `--selftest` e da linha de comando, onde nao
    // ha painel para ler.
    void fail(std::string what) {
        std::fprintf(stderr, "iconcomposer: %s\n", what.c_str());
        trouble = std::move(what);
    }
    // A pasta que continha o ultimo bundle aberto -- onde o seletor deve
    // abrir da proxima vez. Ver `act()`.
    fs::path lastParent;

    // ---- A EXPORTACAO, COMO FILA E NAO COMO LACO --------------------------
    //
    // POR QUE NAO UM `for` COM SEIS RENDERS DENTRO DE `act()`. Um render de
    // 1024 px com vidro leva DEZENAS de segundos (RenderView::lastRenderSeconds
    // existe por causa do 15/09), e seis deles num laco sao minutos com a
    // janela parada e nada na tela dizendo por que. A fila e drenada UM POR
    // QUADRO, e cada item custa dois quadros de proposito:
    //
    //   1. o quadro do ANUNCIO -- `status` recebe "Exporting 3 of 6: <nome>",
    //      o modal desenha essa frase, e `drainExport` volta sem renderizar;
    //   2. o quadro do TRABALHO -- o render acontece, e a janela congela por
    //      ele, mas congela DEPOIS de ter dito o que esta fazendo.
    //
    // Sem o primeiro quadro a frase seria escrita e o render comecaria antes
    // de qualquer pixel dela chegar a tela: a pessoa veria a frase do item
    // ANTERIOR durante a espera do atual, que e pior do que nao ter frase.
    std::vector<icf::Context> exportQueue;   // o que falta, em ordem
    std::size_t exportDone = 0, exportTotal = 0, exportFailures = 0;
    std::uint32_t exportSize = 512;
    fs::path exportDir;
    bool exportAnnounced = false;

    // Grava os bytes. Devolve o motivo de nao ter conseguido, ou vazio.
    static std::string writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
        std::FILE* f = std::fopen(path.string().c_str(), "wb");
        if (!f) return "could not open " + path.string();
        const std::size_t n = bytes.empty() ? 0 : std::fwrite(bytes.data(), 1, bytes.size(), f);
        const bool bad = std::ferror(f) != 0 || n != bytes.size();
        std::fclose(f);
        if (bad) return "could not write " + path.string();
        return {};
    }

    void drainExport() {
        if (exportQueue.empty() || !session || !device || !mux) return;
        ick::ExportSheetState& sheet = session->exportSheet;
        const std::string stem = ick::exportStem(*session);
        const icf::Context ctx = exportQueue.front();
        const std::string name = ick::exportFileName(stem, ctx, exportSize);
        const std::string doing = "Exporting " + std::to_string(exportDone + 1) + " of " +
                                  std::to_string(exportTotal) + ": " + name;
        if (!exportAnnounced) {
            sheet.status = doing;
            exportAnnounced = true;
            return;
        }

        // O DISPOSITIVO É UM SÓ E O AGENDADOR PODE ESTAR NELE (revisão 19/09,
        // C1). O arrendamento não espera: com um render em voo a exportação
        // fica parada este quadro, DIZENDO que está parada e por quê, e tenta
        // de novo no seguinte. Um mutex aqui congelaria a janela pelos
        // segundos que o job segura o dispositivo.
        //
        // E O ARRENDAMENTO DEVOLVE-SE SOZINHO (revisão 19/09, N3). Entre pegar
        // e devolver corre `renderExportFile`, que a 1024 px aloca o ladrilho
        // inteiro -- o MESMO escape que `AppPorts.cpp` já nomeia no `Work` do
        // job ("`bad_alloc` é o escape que se espera de verdade aqui") e por
        // causa do qual aquele lado tem um `struct Release` e um `try/catch`.
        // Um `throw` aqui, com o par cru, deixava `leased_` de pé para sempre:
        // `pump()` nunca mais submetia e o canvas e as miniaturas paravam de
        // renderizar em silêncio. O `Lease` fecha o primeiro buraco por
        // qualquer saída; o `catch` fecha o segundo, virando a exceção numa
        // linha na tela em vez de num editor mudo.
        ick::ExportFile made;
        {
            ick::SharedScheduler::Lease lease(*mux);
            if (!lease) {
                sheet.status = doing + " — waiting for a render in flight";
                return;
            }
            try {
                made = ick::renderExportFile(*device, session->bundle(), stem, exportSize, ctx);
            } catch (const std::exception& e) {
                made.context = ctx;
                made.name = name;
                made.error = std::string("o render lançou: ") + e.what();
            } catch (...) {
                made.context = ctx;
                made.name = name;
                made.error = "o render lançou algo que não é std::exception";
            }
        }
        sheet.status = doing;
        std::string why = made.error;
        if (why.empty()) why = writeBytes(exportDir / made.name, made.png);
        if (!why.empty()) {
            ++exportFailures;
            fail("export " + made.name + ": " + why);
        }
        // O que foi desenhado sem ter sido medido continua indo para o
        // stderr, como no `icrender`: uma nota nao e uma falha e nao pode
        // pintar a barra de vermelho, mas tambem nao pode sumir.
        for (const auto& n : made.notes) {
            std::fprintf(stderr, "iconcomposer: [OBS] %s: %s\n", made.name.c_str(), n.c_str());
        }

        exportQueue.erase(exportQueue.begin());
        ++exportDone;
        exportAnnounced = false;
        if (!exportQueue.empty()) return;

        sheet.busy = false;
        const std::size_t wrote = exportDone - exportFailures;
        sheet.status = "Wrote " + std::to_string(wrote) + " of " + std::to_string(exportTotal) +
                       " to " + exportDir.string();
        // E `trouble` NAO E APAGADO AQUI (revisao 19/09, M6). A politica "o
        // sucesso apaga a queixa anterior" esta escrita para `save`/`saveAs` e
        // e sobre a MESMA operacao: gravar de novo, e conseguir, e a resposta
        // exata para "isto ja passou". Uma exportacao bem-sucedida nao responde
        // isso sobre um `Ctrl+S` que falhou por disco cheio -- o documento
        // continua nao salvo e o titulo continua com o asterisco, e apagar a
        // linha vermelha diria que nao.
    }

    // The coordinator is recreated with the session because it caches the last
    // request it made; keeping it across a document would have it waiting for
    // an answer about a document that is gone.
    void adopt(std::optional<ick::Session> s) {
        coordinator.reset();
        // Pela MESMA razão do coordenador: as miniaturas guardam a versão do
        // documento que cada textura responde, e uma barra que sobrevivesse à
        // troca mostraria o ícone anterior ao lado do novo.
        thumbs.reset();
        // A FILA DE EXPORTACAO MORRE COM O DOCUMENTO, pela mesma razao que o
        // coordenador: `drainExport` renderiza `session->bundle()`, e uma
        // fila que sobrevivesse a troca escreveria PNGs do documento novo com
        // os nomes pedidos para o antigo -- sem um pixel na tela explicando.
        //
        // MAS ELA NAO MORRE CALADA (revisao 19/09, M5). Ate aqui a troca
        // limpava os quatro contadores e o `sheet` do documento antigo ia
        // junto: a pasta ficava com 3 de 6 PNGs e nenhum "Wrote 3 of 6" nunca
        // aparecia. O abandono e um fato que a pessoa precisa saber, entao ele
        // passa por `fail()` como qualquer outro -- e `open()`/`newDocument`
        // limpam `trouble` ANTES de chamar `adopt`, entao esta frase sobrevive
        // a troca em vez de ser apagada por ela.
        if (!exportQueue.empty()) {
            fail("export abandoned at " + std::to_string(exportDone) + " of " +
                 std::to_string(exportTotal) + ": the document changed, and the queue renders the "
                 "document it was asked about");
        }
        exportQueue.clear();
        exportTotal = exportDone = exportFailures = 0;
        exportAnnounced = false;
        session = std::move(s);
        if (session) {
            coordinator = std::make_unique<ick::RenderCoordinator>(mux->canvasLane(), *sink);
            thumbs = std::make_unique<ick::RenditionThumbnails>(mux->thumbnailLane(), *sink);
        }
    }
    void open(const fs::path& dir) {
        auto s = ick::Session::open(dir);
        if (!s) {
            fail("not a `.icon` this reader can open: " + dir.string());
            return;
        }
        trouble.clear();
        lastParent = dir.parent_path();
        adopt(std::move(s));
    }
    void close() { adopt(std::nullopt); }

    // Runs after the frame: the Kit asked, the app answers with dialogs and files.
    void act() {
        ick::MenuActions a = actions;
        actions = {};
        if (a.newDocument) {
            const std::string p = saveFileDialog("Untitled.icon").string();
            if (!p.empty()) {
                auto s = ick::Session::create(p);
                if (!s) {
                    fail("could not create " + p);
                } else {
                    trouble.clear();
                    adopt(std::move(s));
                }
            }
        }
        if (a.open) {
            std::string why;
            // ONDE O SELETOR ABRE. O PAI do bundle, nunca o bundle: dentro
            // dele a lista mostra o `Assets/` e o `.icon` nao esta na tela
            // para ser escolhido (AppPorts.h). Com um documento aberto, o
            // vizinho dele e o palpite certo -- os `.icon` de uma pessoa
            // moram juntos. Sem documento, o ultimo lugar de onde abrimos; e
            // sem isso, vazio, que deixa o dialogo lembrar sozinho.
            fs::path startIn = lastParent;
            if (session) startIn = session->bundle().path().parent_path();
            const fs::path p = SystemOpenBundleDialog(startIn, &why);
            if (!p.empty()) {
                open(bundleDirOf(p));
            } else if (!why.empty()) {
                // Not a cancel: the picker never got as far as asking.
                fail(why);
            }
        }
        // A `.icon` is a folder, so DRAGGING IT IN is the gesture the format
        // actually suggests.
        if (dropped) {
            const fs::path p = *dropped;
            dropped.reset();
            open(bundleDirOf(p));
        }
        // O SUCESSO APAGA A QUEIXA ANTERIOR, e nada mais a apaga. Uma linha
        // vermelha que ficasse para sempre viraria parte do cenario; uma que
        // sumisse sozinha depois de N segundos sumiria enquanto a pessoa
        // estivesse lendo o porque. Gravar de novo, e conseguir, e a resposta
        // exata para "isto ja passou".
        if (a.save && session) {
            const std::string r = session->save();
            if (r.empty()) trouble.clear();
            else fail("save: " + r);
        }
        if (a.saveAs && session) {
            const std::string p = saveFileDialog(session->bundle().path().filename().string()).string();
            if (!p.empty()) {
                const std::string r = session->saveAs(p);
                if (r.empty()) trouble.clear();
                else fail("save as: " + r);
            }
        }
        // IMPORTAR UM ASSET POR DIALOGO (laudo 18/09 §4.3). O pedido veio do
        // painel do inspetor e trouxe CONSIGO o no e o escopo (Panels.h,
        // `MenuActions::importInto`): este bloco roda depois do frame, e a
        // camada que pediu pode nao ser mais a selecionada.
        //
        // A ordem e a mesma do campo digitado em PanelInspectorAsset.cpp, e
        // deliberadamente: copiar o arquivo para `Assets/` NAO e uma edicao do
        // documento e nao carrega comando; apontar a camada para ele E, e vai
        // pela Session como tudo o mais. Um undo tira a referencia e deixa a
        // copia onde esta, que `unusedAssets()` nomeia no Diagnostics.
        if (a.importAsset && session) {
            // Os filtros sao os dois formatos que o motor le (doc 04), mais o
            // "All Files" que `openFileDialog` acrescenta -- quem tem um `.SVG`
            // em maiusculas ou um arquivo sem extensao continua podendo
            // escolher, e `importAsset` recusa o que nao for arquivo.
            const std::string picked = openFileDialog({{"Artwork (svg, png)", {"svg", "png"}}}).string();
            if (!picked.empty()) {
                const fs::path file(picked);
                const std::string why = session->bundle().importAsset(file);
                if (!why.empty()) {
                    fail("import: " + why);
                } else {
                    trouble.clear();
                    session->setProperty(a.importInto, "image-name", a.importScope,
                                         icf::json::Value::string(file.filename().string()));
                }
            }
        }
        // EXPORTAR A IMAGEM (T2). O modal ja escolheu o tamanho e os
        // contextos; falta o DESTINO, que e um dialogo, e portanto e daqui
        // (Regra 2). O plano veio no pedido e nao e relido do modal: entre o
        // clique e o retorno do dialogo a pessoa pode ter mexido nas caixas.
        if (a.exportImage && session) {
            ick::ExportSheetState& sheet = session->exportSheet;
            if (!exportQueue.empty()) {
                // Ja ha um lote andando. O modal desabilita Export enquanto
                // `busy`, entao chegar aqui e um caminho que nao deveria
                // existir -- e um lote perdido em silencio seria pior.
                sheet.status = "An export is already running.";
            } else if (a.exportPlan.contexts.empty() || a.exportPlan.size == 0) {
                sheet.status = "Nothing to export.";
                sheet.busy = false;
            } else {
                FolderDialogOptions options;
                options.title = "Export icon as image";
                // Ao lado do bundle: e onde os arquivos de uma pessoa moram.
                options.startIn = session->bundle().path().parent_path();
                // Balde de MRU proprio, pela mesma razao do seletor de bundle:
                // sem chave, um Save As em outro canto decide onde este abre.
                options.mruKey = "icon-composer/export-image";
                std::string why;
                const fs::path dir = openFolderDialog(options, &why);
                if (dir.empty()) {
                    sheet.busy = false;
                    if (why.empty()) {
                        // Cancelar nao e falhar: nao pinta a barra de vermelho.
                        sheet.status = "Export cancelled.";
                    } else {
                        fail("export: " + why);
                        sheet.status = "export: " + why;
                    }
                } else {
                    exportDir = dir;
                    exportSize = a.exportPlan.size;
                    exportQueue = a.exportPlan.contexts;
                    exportTotal = exportQueue.size();
                    exportDone = 0;
                    exportFailures = 0;
                    exportAnnounced = false;
                    sheet.busy = true;
                    sheet.status = "Exporting 1 of " + std::to_string(exportTotal) + "…";
                }
            }
        }
        if (a.toggleDiagnostics) showDiagnostics = !showDiagnostics;
        if (a.close) close();
        if (a.quit) quit = true;
    }

    // O titulo da janela do sistema (a barra de tarefas, o Alt+Tab). O que a
    // barra desenhada mostra e `titleBarText`, sem o nome do app.
    std::string title() const {
        std::string t = "Icon Composer";
        if (session) {
            t += " - " + session->bundle().path().filename().string();
            if (session->isDirty()) t += " *";
        }
        return t;
    }
    std::string titleBarText() const {
        if (!session) return "Icon Composer";
        return session->bundle().path().stem().string();
    }
};

// O TETO DE METADE DA JANELA, APLICADO QUANDO A JANELA MUDA DE TAMANHO
// (revisao 19/09, I2 e N5).
//
// O QUE ESTE COMENTARIO DIZIA E A SONDA DERRUBOU. Ate 19/09 ele afirmava que
// "`DockBuilderSplitNode` guarda uma RAZAO e o no redimensiona junto com a
// janela", e que sem esta funcao a sidebar iria sozinha a ~587 pt numa janela
// de 1920. Medido: ela fica em 329 pt em 1080, em 1920 e em 700. O ramo de
// `DockNodeTreeUpdatePosSize` que vale para esta arvore e o 3
// (`imgui.cpp:20329`), o de tamanho ABSOLUTO, porque o irmao de cada painel
// carrega o no central -- quem absorve a mudanca da janela e o canvas. A conta
// esta no cabecalho de `Source/IconComposerKit/WindowLayout.h`.
//
// ENTAO O QUE SOBRA AQUI E UM GRAMPO SO, E ELE E REAL: metade do espaco. Sem
// ele, a 700 pt medidos o canvas fica com 32 px; com ele, com 165,5. Quem faz
// isso e `kPanelShareMax`, que e um `[DEC]` deste projeto -- nao um dos `[BIN]`
// do alvo.
//
// E O QUE SAIU DAQUI, DE PROPOSITO: o intervalo `[min, max]` medido. Com
// largura absoluta ele nao tinha como morder contra a janela; a UNICA coisa
// que ele alcancava era desfazer o arrasto da pessoa -- arrastar o divisor
// para 600 pt e redimensionar devolvia 480 --, que e justamente o que o
// comentario prometia nao fazer. `capPanelShare` so ENCOLHE, entao um divisor
// arrastado fica onde a pessoa o pos enquanto couber na metade.
//
// `DockBuilderSetNodeSize` escreve `Size` E `SizeRef` e poe a autoridade no no
// (imgui.cpp:20792), que e o que faz o valor sobreviver ao quadro seguinte.
void clampDockedWidths() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return;
    const float total = vp->WorkSize.x;
    // O tamanho de janela em que o grampo ja foi aplicado. Um por processo,
    // e pela mesma razao que o resto do app: ha exatamente uma janela.
    static float appliedAt = -1.0f;
    if (std::fabs(total - appliedAt) < 0.5f) return;

    // E ELE SO E LATCHEADO DEPOIS DE TER SIDO APLICADO (revisao 19/09, N7).
    // Ate aqui `appliedAt = total` era a linha seguinte ao teste, antes de as
    // janelas serem procuradas: num quadro em que os nos de dock ainda nao
    // existem -- `FindWindowByName` devolve null -- o tamanho ficava
    // registrado como grampeado e o grampo DAQUELE tamanho nunca rodava.
    // Benigno enquanto `defaultLayout` entrega numeros dentro do intervalo;
    // deixa de ser no primeiro `imgui.ini` que restaure um painel fora dele.
    ImGuiWindow* layers = ImGui::FindWindowByName(ick::kLayersWindow);
    ImGuiWindow* inspector = ImGui::FindWindowByName(ick::kInspectorWindow);
    if (!layers || !layers->DockNode || !inspector || !inspector->DockNode) return;
    ImGuiDockNode* layersNode = layers->DockNode;
    ImGuiDockNode* inspectorNode = inspector->DockNode;
    if (!(layersNode->Size.x > 0.0f) || !(layersNode->Size.y > 0.0f)) return;
    if (!(inspectorNode->Size.x > 0.0f) || !(inspectorNode->Size.y > 0.0f)) return;
    appliedAt = total;

    auto capOne = [](ImGuiDockNode* node, float want) {
        // Meio pixel de folga: `Size.x` passou por uma razao e uma
        // multiplicacao, e reescrever o no por 1e-4 pt seria um `SizeRef` novo
        // por quadro sem nada mudando na tela.
        if (std::fabs(want - node->Size.x) < 1.0f) return;
        ImGui::DockBuilderSetNodeSize(node->ID, ImVec2(want, node->Size.y));
    };

    const float sidebar = ick::capPanelShare(layersNode->Size.x, total);
    capOne(layersNode, sidebar);
    // O inspetor e medido contra o que SOBRA depois da sidebar grampeada -- a
    // mesma conta de `defaultLayout`, e nao contra a janela inteira.
    capOne(inspectorNode, ick::capPanelShare(inspectorNode->Size.x, total - sidebar));
}

// ---- os paineis, na ordem em que desenham -----------------------------------

void drawLayersPanel(State& st) {
    if (st.session) {
        ick::drawLayers(*st.session);
    } else {
        ImGui::Begin(ick::kLayersWindow);
        ImGui::Dummy(ImVec2(1.0f, ick::theme::kTitleBarH * ImGui::GetStyle().FontScaleDpi));
        ImGui::TextDisabled("No document");
        ImGui::End();
    }
}

void drawCanvasPanel(State& st) {
    if (st.coordinator && st.session) {
        // The coordinator ticks BEFORE the canvas draws: it may create or
        // replace the texture the canvas is about to show (spec 13/09 §6).
        st.coordinator->tick(*st.session);
        ick::drawCanvas(*st.session, st.coordinator->view(), st.actions, st.trouble);
        // FORA da janela do canvas, de proposito: um popup nasce no stack
        // de IDs de quem o abre, e este modal cobre a janela inteira --
        // ele nao e do canvas, so e pedido pelo menu que o canvas desenha.
        ick::drawExportSheet(*st.session, st.actions);
        return;
    }
    // With no document the Kit's menu bar has nothing to hang from, so the
    // empty canvas carries the items that can still be acted on.
    const float k = ImGui::GetStyle().FontScaleDpi;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f * k, (ick::theme::kTitleBarH - ick::theme::kFontSize) * 0.5f * k));
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ick::theme::kPanel);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ick::theme::kCanvas);
    ImGui::Begin(ick::kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar);
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
    if (ImGui::BeginMenuBar()) {
        ick::ui::pushMenuStyle();
        const bool fileOpen = ImGui::BeginMenu("File");
        ick::ui::popMenuStyle();
        if (fileOpen) {
            ick::ui::pushMenuStyle();
            if (ImGui::MenuItem("New...", "Ctrl+N")) st.actions.newDocument = true;
            if (ImGui::MenuItem("Open...", "Ctrl+O")) st.actions.open = true;
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) st.actions.quit = true;
            ick::ui::popMenuStyle();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    // Os atalhos valem sem documento tambem; com documento quem os le e o
    // menu do Kit (MenuBar.cpp).
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) st.actions.newDocument = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) st.actions.open = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Q, ImGuiInputFlags_RouteGlobal)) st.actions.quit = true;
    if (ImGui::Button("Open a .icon bundle...")) st.actions.open = true;
    ImGui::SameLine();
    ImGui::TextDisabled("or drag one in");
    if (!st.trouble.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ick::theme::kDanger);
        ImGui::TextWrapped("%s", st.trouble.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

// A BARRA DE RENDITIONS (T4). Painel próprio, ancorado acima do canvas em
// `defaultLayout`: o alvo a desenha fora do canvas, e uma janela própria é
// também o que deixa fechá-la num documento pesado, quando as miniaturas não
// valem o render.
void drawRenditionsPanel(State& st) {
    if (st.session) {
        // `thumbs` pode faltar num caminho sem sessão; a barra aceita nulo
        // e desenha a forma sem os pixels, dizendo isso na nota.
        ick::drawRenditions(*st.session, st.thumbs.get());
    } else {
        ImGui::Begin(ick::kRenditionsWindow);
        ImGui::TextDisabled("No document");
        ImGui::End();
    }
}

void drawInspectorPanel(State& st) {
    if (st.session) {
        ick::drawInspector(*st.session, st.actions);
    } else {
        ImGui::Begin(ick::kInspectorWindow);
        ImGui::End();
    }
}

void drawDiagnosticsPanel(State& st) {
    if (st.session && st.coordinator) {
        ick::drawDiagnostics(*st.session, st.coordinator->view(), st.trouble);
    } else {
        ImGui::Begin(ick::kDiagnosticsWindow);
        ImGui::End();
    }
}

// `[BIN]` As larguras dos dois paineis sao as do alvo, lidas em 19/09 de
// `WindowLayoutConstants` (laudo §4.2) e transcritas em
// `Source/IconComposerKit/WindowLayout.h`, que e onde a aritmetica mora e onde
// ela e testada.
//
// Elas eram percentuais herdados do `sfsymview`, 20% e 28%, e a diferenca nao
// e cosmetica: numa janela de 1601 pt aquilo dava 320 / 448, isto e, o
// inspetor saia quase 120 pt largo demais.
//
// O QUE ESTA CHAMADA DECIDE, E SO ELA (revisao 19/09, I2 e N5). A razao aqui e
// calculada UMA vez, com a `WorkSize` do primeiro layout -- e, ao contrario do
// que este comentario dizia ate 19/09, o no de dock NAO a persegue depois
// disso. A sonda da re-revisao mediu o ramo 3 de `DockNodeTreeUpdatePosSize`
// (`imgui.cpp:20329`): como o irmao de cada painel carrega o no central, os
// dois ficam com largura ABSOLUTA e quem absorve a janela e o canvas. Sem
// grampo nenhum, a sidebar mede 329 pt em 1080, 329 em 1920 e 329 em 700 -- o
// "maximizada para 1920 ela virava ~587" que estava escrito aqui e falso.
//
// Isso NAO torna estas linhas decorativas, e a diferenca importa: o ganho
// delas e a LARGURA INICIAL. Derivar a razao do ideal medido da 330 pt a 1080,
// contra os 216 que os 20% herdados davam -- e essa largura e a que o painel
// vai carregar pela sessao inteira, justamente porque o no nao a refaz.
//
// O que os quatro numeros de minimo e maximo fazem, dito sem enfeite: eles sao
// o intervalo do alvo escrito em volta do ideal, e com o ideal dentro dele nao
// movem um pixel hoje. Nao ha um segundo momento em que eles mordam -- a
// tentativa de faze-los morder no redimensionamento so conseguia desfazer o
// arrasto do divisor, e saiu. `clampDockedWidths` ficou com o unico grampo que
// age de verdade, o de metade do espaco (`kPanelShareMax`, um `[DEC]` nosso),
// e ele so encolhe.
//
// O QUE EU NAO APLIQUEI, DE PROPOSITO: `Window.minContentSize` do alvo e
// 1284x704, e este editor roda numa maquina cujo monitor retrato tem 1080 de
// largura. Um minimo de 1284 poria a janela maior que a tela. O alvo e macOS
// e supoe um monitor largo; a medicao fica no laudo e nao vira grampo aqui.
// `Canvas.padding` (96 pt) tambem nao: mexer nele muda o que o `Fit` faz, e
// `canvas_fit_fits_exactly_and_centres` afirma hoje que o Fit encosta EXATO
// no eixo curto. Trocar isso e uma decisao com teste proprio, nao um efeito
// colateral desta linha.
void defaultLayout(ImGuiID dockspaceId, ImVec2 work) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, work);

    const float total = work.x > 1.0f ? work.x : 1.0f;
    const float sidebar = ick::clampSidebarWidth(ick::kSidebarIdeal, total);
    // Contra o que sobrou, nao contra a janela inteira.
    const float afterLeft = total - sidebar;
    const float inspector = ick::clampInspectorWidth(ick::kInspectorIdeal, afterLeft);

    ImGuiID centre = dockspaceId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Left, ick::dockRatio(sidebar, total), nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(
        centre, ImGuiDir_Right, ick::dockRatio(inspector, afterLeft), nullptr, &centre);
    // TRES COLUNAS, COMO O TAURI (30/09): sidebar | canvas | inspetor. As
    // renditions deixaram de ser um no do dock -- elas flutuam no rodape do
    // canvas (`.rendition-bar`), e o Diagnostics abre pelo menu View.
    ImGui::DockBuilderDockWindow(ick::kLayersWindow, left);
    ImGui::DockBuilderDockWindow(ick::kCanvasWindow, centre);
    ImGui::DockBuilderDockWindow(ick::kInspectorWindow, right);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

namespace {

// ---- a hospedeira e o topo das colunas ---------------------------------------
TrafficLights g_lights;

// SEM ABAS. Cada coluna e uma janela so no seu no, e a aba dela seria uma
// segunda barra de titulo embaixo da primeira. `AutoHideTabBar` a esconde
// enquanto o no tem uma janela so, e ela volta (o triangulo no canto) quando
// alguem empilha outra -- o docking continua la, so nao aparece a toa.
void columnClass() {
    static ImGuiWindowClass cls;
    cls.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&cls);
}

// A janela hospedeira e so o dockspace. A barra de titulo nao e dela: e o topo
// de 52 pt de cada coluna, e o sistema arrasta a janela por qualquer ponto
// dessa faixa que nao tenha um controle embaixo (NativeWindow.cpp).
void drawHost(Shell& shell) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float k = ImGui::GetStyle().FontScaleDpi;
    shell.setTitleBarHeight(ick::theme::kTitleBarH * k);

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin(NativeWindow::kHostWindow, nullptr, flags);
    ImGui::PopStyleVar(3);
    // Um id novo para o layout de tres colunas: um `imgui.ini` de antes de 30/09
    // guarda a arvore com as renditions em cima e o Diagnostics embaixo.
    const ImGuiID dockspace = ImGui::GetID("IconComposerColumns");
    if (!ImGui::DockBuilderGetNode(dockspace)) defaultLayout(dockspace, vp->WorkSize);
    ImGui::DockSpace(dockspace, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
}

// As luzes, no topo da coluna da sidebar (`.sidebar-top`): a janela dela e
// reaberta para acrescentar, depois de o Kit ter desenhado a lista.
void drawLights(State& st, Shell& shell) {
    if (!ImGui::Begin(ick::kLayersWindow)) {
        ImGui::End();
        return;
    }
    const float k = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 o = ImGui::GetWindowPos();
    const float d = 14.0f * k;
    const float barH = ick::theme::kTitleBarH * k;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    g_lights.draw(shell, ImVec2(o.x + ick::theme::kLightsInset * k, o.y + (barH - d) * 0.5f), k,
                  st.session && st.session->isDirty());
    ImGui::SetCursorScreenPos(keep);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::End();
}

// As renditions flutuando no rodape do canvas (`.rendition-bar`: 8 pt das
// laterais, 10 do fundo). Uma janela sem decoracao nem fundo, posta todo
// quadro no retangulo do canvas -- e por isso segue o splitter e a janela.
void drawRenditionsOverlay(State& st) {
    ImGuiWindow* canvas = ImGui::FindWindowByName(ick::kCanvasWindow);
    if (!canvas || !st.session) return;
    const float k = ImGui::GetStyle().FontScaleDpi;
    const float h = 86.0f * k;
    ImGui::SetNextWindowPos(ImVec2(canvas->Pos.x + 8.0f * k, canvas->Pos.y + canvas->Size.y - h - 10.0f * k));
    ImGui::SetNextWindowSize(ImVec2(canvas->Size.x - 16.0f * k, h));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ick::drawRenditions(*st.session, st.thumbs.get(),
                        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
}

// O rim de dentro do macOS 27 (Theme.h, `kRimInner`): o realce claro de 0,5 pt
// por cima de tudo, seguindo o canto que o DWM recorta. Maximizada a janela
// nao tem canto nem borda.
void drawRim(Shell& shell) {
    if (shell.maximized()) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float k = ImGui::GetStyle().FontScaleDpi;
    const float w = std::max(1.0f, k);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImVec2 a(vp->Pos.x + w * 0.5f, vp->Pos.y + w * 0.5f);
    const ImVec2 b(vp->Pos.x + vp->Size.x - w * 0.5f, vp->Pos.y + vp->Size.y - w * 0.5f);
    fg->AddRect(a, b, ick::theme::u32(ick::theme::kRimInner), shell.cornerRadius(), 0, w);
}

}  // namespace

int run(const fs::path& initial, rb::DesignGeneration generation) {
    // Validation off: nobody here is checking the driver, and the tower's
    // default (on) costs every frame (spec 13/09 §6).
    //
    // DOIS DISPOSITIVOS NO PROCESSO: o da tela (Shell) e este, o do render. A
    // exportacao e os ladrilhos do canvas ficam longe da fila da swapchain, e
    // os pixels passam de um para o outro pela CPU (TexturePool). Sem volk
    // desde 30/09, cada um chama o loader com o proprio `VkDevice`; a tabela
    // por dispositivo da RenderBox (Source/RenderBox/VulkanApi.h) continua
    // valendo, so deixou de ser a unica coisa entre os dois.
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }

    std::string why;
    std::unique_ptr<Shell> shell = Shell::create("Icon Composer", &why);
    if (!shell) {
        std::fprintf(stderr, "iconcomposer: no window: %s\n", why.c_str());
        return 2;
    }

    // A ordem de destruicao e a inversa desta: o estado (e com ele o
    // coordenador, que devolve a textura) antes do pool, o pool antes da fila
    // de trabalho, e tudo antes do Shell, que e dono do dispositivo da tela.
    // As luzes carregam ANTES da fila de trabalho existir: rasterizar os
    // glifos usa o `rb::Device`, e o aquecimento abaixo tambem -- na thread de
    // trabalho, ao mesmo tempo, seriam duas threads num `VkQueue`.
    auto lightsSink = std::make_unique<PoolTextureSink>(shell->gpu());
    g_lights.load(lightsSink->pool(), *device, ImGui::GetStyle().FontScaleDpi);

    JobQueue jobs;
    // A primeira coisa na fila, antes de qualquer render: o canvas pede o dele
    // no primeiro quadro e ele espera este, em vez de pagar o aquecimento
    // dentro do proprio tempo.
    jobs.submit([&device] { warmUp(*device); }, {});
    // Os SF Symbols dos widgets (Widgets.h), rasterizados na fila, depois do
    // aquecimento e antes do primeiro render do canvas.
    AppSymbols symbols;
    symbols.schedule(jobs, *device, lightsSink->pool(), appleAssetsDir(),
                     static_cast<std::uint32_t>(std::lround(40.0f * std::max(1.0f, ImGui::GetStyle().FontScaleDpi))));
    ick::setSymbolSource(&symbols);
    // A arte das camadas na sidebar, pela mesma fila.
    AppArt art(jobs, *device, lightsSink->pool());
    ick::setArtSource(&art);
    State state;
    state.shell = shell.get();
    // O dispositivo da exportacao e o mesmo do agendador -- ver `State::device`.
    state.device = &*device;
    state.sink = std::move(lightsSink);
    state.scheduler = std::make_unique<JobScheduler>(jobs, *device);
    // O multiplexador vive tanto quanto o agendador, e não por documento: ele
    // não guarda nada do documento, só de quem é o render em voo.
    state.mux = std::make_unique<ick::SharedScheduler>(*state.scheduler);

    // DROPPING A `.icon` ON THE WINDOW OPENS IT. Only the FIRST path is taken:
    // a multi-selection drop has no meaning for an editor that holds one
    // document, and picking one silently beats opening the last of several.
    // O callback so ANOTA; quem abre e `act()`, depois do quadro.
    shell->onDrop([&state](const fs::path& p) { state.dropped = p; });

    if (!initial.empty()) state.open(initial);
    if (state.session) state.session->view.generation = generation;

    shell->run([&] {
        // Os `done` dos renders rodam aqui, na thread principal, antes de
        // qualquer painel perguntar pelo resultado.
        jobs.pump();
        drawHost(*shell);
        columnClass();
        drawLayersPanel(state);
        drawLights(state, *shell);
        columnClass();
        drawCanvasPanel(state);
        // DEPOIS do canvas, e isto é a prioridade em ato. O multiplexador
        // (Renditions.h) põe o canvas na frente quando as DUAS filas estão
        // cheias, mas não interrompe um render em voo -- com a raia livre,
        // quem pede primeiro sai primeiro. Nesta ordem o pedido de 512 px sai
        // antes do de 128 em todo quadro em que os dois nascem juntos.
        drawRenditionsOverlay(state);
        columnClass();
        drawInspectorPanel(state);
        if (state.showDiagnostics) {
            const float k = ImGui::GetStyle().FontScaleDpi;
            ImGui::SetNextWindowSize(ImVec2(760.0f * k, 320.0f * k), ImGuiCond_FirstUseEver);
            drawDiagnosticsPanel(state);
        }
        drawRim(*shell);

        // End of frame work: `act()` can replace or close the session, and a
        // swap mid-frame would leave the panels after it drawing against state
        // that changed under them. `advanceFrame` likewise belongs after every
        // upload this frame made.
        state.sink->advanceFrame();
        // Depois de todo painel ter desenhado: os nos de dock ja existem e ja
        // foram redimensionados por este quadro.
        clampDockedWidths();
        state.act();
        // DEPOIS de `act()`, que e quem enfileira: assim o primeiro item ja
        // tem o quadro do anuncio neste mesmo frame, e nao no seguinte.
        state.drainExport();
        shell->setTitle(state.title());
        if (state.quit) shell->close();
    });

    // The coordinator returns its texture to the pool before the pool goes.
    state.close();
    ick::setSymbolSource(nullptr);
    ick::setArtSource(nullptr);
    return 0;
}

}  // namespace icapp
