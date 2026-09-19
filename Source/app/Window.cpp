#include "Source/app/Window.h"

#include "Source/IconComposerKit/Export.h"
#include "Source/IconComposerKit/Panels.h"
#include "Source/IconComposerKit/RenderCoordinator.h"
#include "Source/IconComposerKit/Renditions.h"
#include "Source/IconComposerKit/Session.h"
#include "Source/IconComposerKit/WindowLayout.h"
#include "Source/app/OnyxPorts.h"

#include <Onyx/App/App.h>
#include <Onyx/App/IPanel.h>
#include <Onyx/App/UIHelpers.h>
#include <Onyx/App/Window.h>
#include <Onyx/Services/Threading.h>
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
// is `SystemOpenBundleDialog` (OnyxPorts.h), which on Windows hands back the
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
// goes, and the sink goes before the VkContext (the window outlives this).
struct State {
    // The window handle, kept because `glfwGetCurrentContext()` is an OpenGL
    // call and returns null in a Vulkan app -- Quit through it would silently
    // do nothing.
    GLFWwindow* window = nullptr;
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
    std::unique_ptr<OnyxTextureSink> sink;
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
    Onyx::App::App* app = nullptr;
    bool quit = false;
    // WHY A DROP IS QUEUED AND NOT ACTED ON. GLFW delivers it from inside
    // `glfwPollEvents`, i.e. mid-frame, and `adopt()` destroys the session the
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
        if (!mux->tryLease()) {
            sheet.status = doing + " — waiting for a render in flight";
            return;
        }
        ick::ExportFile made =
            ick::renderExportFile(*device, session->bundle(), stem, exportSize, ctx);
        mux->endLease();
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
            const std::string p = SystemSaveFileDialog("Untitled.icon");
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
            // para ser escolhido (OnyxPorts.h). Com um documento aberto, o
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
        // actually suggests -- and it is the one path into the document that
        // does not depend on finding the right `File` menu (Onyx draws one of
        // its own, above ours, whose Open cannot accept a folder at all).
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
            const std::string p = SystemSaveFileDialog(session->bundle().path().filename().string());
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
            // "All Files" que o proprio Onyx acrescenta -- quem tem um `.SVG`
            // em maiusculas ou um arquivo sem extensao continua podendo
            // escolher, e `importAsset` recusa o que nao for arquivo.
            const std::string picked = SystemOpenFileDialog({{"Artwork (svg, png)", {"svg", "png"}}});
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
                Onyx::App::FolderDialogOptions options;
                options.title = "Export icon as image";
                // Ao lado do bundle: e onde os arquivos de uma pessoa moram.
                options.startIn = session->bundle().path().parent_path();
                // Balde de MRU proprio, pela mesma razao do seletor de bundle:
                // sem chave, um Save As em outro canto decide onde este abre.
                options.mruKey = "icon-composer/export-image";
                std::string why;
                const fs::path dir = Onyx::App::SystemOpenFolderDialog(options, &why);
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
        if (a.close) close();
        if (a.quit) quit = true;
    }

    void title() {
        if (!app) return;
        auto* config = app->getConfig();
        if (!config) return;
        std::string t = "Icon Composer";
        if (session) {
            t += " - " + session->bundle().path().filename().string();
            if (session->isDirty()) t += " *";
        }
        config->windowTitle = t;
    }
};

// O PISO E O TETO DAS DUAS LARGURAS, APLICADOS QUANDO A JANELA MUDA DE TAMANHO
// (revisao 19/09, I2).
//
// `DockBuilderSplitNode` guarda uma RAZAO e o no redimensiona junto com a
// janela, entao `defaultLayout` sozinho nao pode ter piso nem teto: ele roda
// uma vez. O que segura os quatro numeros `[BIN]` de minimo e maximo e esta
// funcao. `DockBuilderSetNodeSize` escreve `Size` E `SizeRef` e poe a
// autoridade no no (imgui.cpp:20792), que e exatamente o que faz o split
// recalcular a partir do valor grampeado no quadro seguinte.
//
// SO NA MUDANCA DE TAMANHO DA JANELA, e isto e a diferenca entre um grampo e
// uma briga: arrastar o divisor para 600 pt e uma escolha da pessoa e nao e
// desfeita no quadro seguinte. O que nao pode e a janela crescer e levar a
// sidebar a 587 pt sozinha.
void clampDockedWidths() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    if (!vp) return;
    const float total = vp->WorkSize.x;
    // O tamanho de janela em que o grampo ja foi aplicado. Um por processo,
    // como `g_dropTarget`, e pela mesma razao: ha exatamente uma janela.
    static float appliedAt = -1.0f;
    if (std::fabs(total - appliedAt) < 0.5f) return;
    appliedAt = total;

    auto clampOne = [](const char* window, float want) {
        ImGuiWindow* w = ImGui::FindWindowByName(window);
        if (!w || !w->DockNode) return;
        ImGuiDockNode* node = w->DockNode;
        if (!(node->Size.x > 0.0f) || !(node->Size.y > 0.0f)) return;
        // Meio pixel de folga: `Size.x` passou por uma razao e uma
        // multiplicacao, e reescrever o no por 1e-4 pt seria um `SizeRef` novo
        // por quadro sem nada mudando na tela.
        if (std::fabs(want - node->Size.x) < 1.0f) return;
        ImGui::DockBuilderSetNodeSize(node->ID, ImVec2(want, node->Size.y));
    };

    ImGuiWindow* layers = ImGui::FindWindowByName(ick::kLayersWindow);
    const float sidebarNow =
        (layers && layers->DockNode) ? layers->DockNode->Size.x : ick::kSidebarIdeal;
    const float sidebar = ick::clampSidebarWidth(sidebarNow, total);
    clampOne(ick::kLayersWindow, sidebar);
    // O inspetor e grampeado contra o que SOBRA depois da sidebar grampeada --
    // a mesma conta de `defaultLayout`, e nao contra a janela inteira.
    ImGuiWindow* inspector = ImGui::FindWindowByName(ick::kInspectorWindow);
    const float inspectorNow =
        (inspector && inspector->DockNode) ? inspector->DockNode->Size.x : ick::kInspectorIdeal;
    clampOne(ick::kInspectorWindow, ick::clampInspectorWidth(inspectorNow, total - sidebar));
}

struct LayersPanel : Onyx::App::IPanel {
    explicit LayersPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) {
            ick::drawLayers(*st.session);
        } else {
            ImGui::Begin(ick::kLayersWindow);
            ImGui::TextDisabled("No document");
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kLayersWindow; }
    State& st;
};

// Where a dropped path goes. See the note at the `glfwSetDropCallback` call:
// the GLFW window's user pointer belongs to Onyx, and a GLFW callback carries
// no user data of its own.
State* g_dropTarget = nullptr;

struct CanvasPanel : Onyx::App::IPanel {
    explicit CanvasPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.coordinator && st.session) {
            // The coordinator ticks BEFORE the canvas draws: it may create or
            // replace the texture the canvas is about to show (spec 13/09 §6).
            st.coordinator->tick(*st.session);
            ick::drawCanvas(*st.session, st.coordinator->view(), st.actions, st.trouble);
            // FORA da janela do canvas, de proposito: um popup nasce no stack
            // de IDs de quem o abre, e este modal cobre a janela inteira --
            // ele nao e do canvas, so e pedido pelo menu que o canvas
            // desenha. Chamado aqui porque este painel e o unico que existe
            // com documento aberto e sem o qual o menu tambem nao existiria.
            ick::drawExportSheet(*st.session, st.actions);
        } else {
            // With no document the Kit's menu bar has nothing to hang from, so
            // the empty canvas carries the two items that can still be acted on.
            ImGui::Begin(ick::kCanvasWindow, nullptr, ImGuiWindowFlags_MenuBar);
            if (ImGui::BeginMenuBar()) {
                if (ImGui::BeginMenu("File")) {
                    if (ImGui::MenuItem("New...", "Ctrl+N")) st.actions.newDocument = true;
                    if (ImGui::MenuItem("Open...", "Ctrl+O")) st.actions.open = true;
                    if (ImGui::MenuItem("Quit", "Ctrl+Q")) st.actions.quit = true;
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }
            // A BUTTON AND NOT A LABEL. `TextDisabled` read as a caption for a
            // window that had nothing to press, and the only working Open was
            // in the menu above -- next to Onyx's own `File > Open`, which is
            // drawn higher, looks more like the one you want, and cannot take
            // a folder. The empty canvas now carries the action itself.
            if (ImGui::Button("Open a .icon bundle...")) st.actions.open = true;
            ImGui::SameLine();
            ImGui::TextDisabled("or drag one in");
            if (!st.trouble.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", st.trouble.c_str());
            }
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kCanvasWindow; }
    State& st;
};

// A BARRA DE RENDITIONS (T4). Painel próprio, ancorado acima do canvas em
// `defaultLayout`: o alvo a desenha fora do canvas, e uma janela própria é
// também o que deixa fechá-la num documento pesado, quando as miniaturas não
// valem o render.
struct RenditionsPanel : Onyx::App::IPanel {
    explicit RenditionsPanel(State& s) : st(s) {}
    void Draw() override {
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
    std::string_view getName() const override { return ick::kRenditionsWindow; }
    State& st;
};

struct InspectorPanel : Onyx::App::IPanel {
    explicit InspectorPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session) {
            ick::drawInspector(*st.session, st.actions);
        } else {
            ImGui::Begin(ick::kInspectorWindow);
            ImGui::End();
        }
    }
    std::string_view getName() const override { return ick::kInspectorWindow; }
    State& st;
};

struct DiagnosticsPanel : Onyx::App::IPanel {
    explicit DiagnosticsPanel(State& s) : st(s) {}
    void Draw() override {
        if (st.session && st.coordinator) {
            ick::drawDiagnostics(*st.session, st.coordinator->view(), st.trouble);
        } else {
            ImGui::Begin(ick::kDiagnosticsWindow);
            ImGui::End();
        }
        // End of frame work, here because this panel is registered LAST: `act()`
        // can replace or close the session, and a swap mid-frame would leave the
        // panels after it drawing against state that changed under them.
        // `advanceFrame` likewise belongs after every upload this frame made.
        st.sink->advanceFrame();
        // Depois de todo painel ter desenhado: os nos de dock ja existem e ja
        // foram redimensionados por este quadro.
        clampDockedWidths();
        st.act();
        // DEPOIS de `act()`, que e quem enfileira: assim o primeiro item ja
        // tem o quadro do anuncio neste mesmo frame, e nao no seguinte.
        st.drainExport();
        st.title();
        if (st.quit && st.window) glfwSetWindowShouldClose(st.window, 1);
    }
    std::string_view getName() const override { return ick::kDiagnosticsWindow; }
    State& st;
};

// `[BIN]` As larguras dos dois paineis sao as do alvo, lidas em 19/09 de
// `WindowLayoutConstants` (laudo §4.2) e transcritas em
// `Source/IconComposerKit/WindowLayout.h`, que e onde a aritmetica mora e onde
// ela e testada.
//
// Elas eram percentuais herdados do `sfsymview`, 20% e 28%, e a diferenca nao
// e cosmetica: numa janela de 1601 pt aquilo dava 320 / 448, isto e, o
// inspetor saia quase 120 pt largo demais.
//
// E O PISO E O TETO PRECISAM DE DUAS COISAS, nao de uma (revisao 19/09, I2).
// `DockBuilderSplitNode` pede uma RAZAO, e a razao e calculada UMA vez, com a
// `WorkSize` do primeiro layout; o no de dock redimensiona proporcionalmente
// com a janela depois disso. O grampo que ficava aqui -- `clamp(ideal, lo,
// hi)` -- era um no-op, porque o ideal ja esta dentro do intervalo nos dois
// paineis: numa janela que abre com 1080 pt a sidebar saia com 330 (certo), e
// maximizada para 1920 ela virava ~587, acima do teto de 480 que o comentario
// afirmava estar aplicado. Entao:
//
//   1. AQUI, a razao inicial, derivada da largura em pontos -- e o grampo de
//      metade da janela e o unico que pode morder nesta chamada;
//   2. e em `clampDockedWidths`, chamado quando a JANELA muda de tamanho, que
//      e o momento em que a razao empurraria o painel para fora do intervalo
//      medido. Sem (2) as quatro constantes de minimo e maximo continuam
//      decorativas.
//
// O QUE EU NAO APLIQUEI, DE PROPOSITO: `Window.minContentSize` do alvo e
// 1284x704, e este editor roda numa maquina cujo monitor retrato tem 1080 de
// largura. Um minimo de 1284 poria a janela maior que a tela. O alvo e macOS
// e supoe um monitor largo; a medicao fica no laudo e nao vira grampo aqui.
// `Canvas.padding` (96 pt) tambem nao: mexer nele muda o que o `Fit` faz, e
// `canvas_fit_fits_exactly_and_centres` afirma hoje que o Fit encosta EXATO
// no eixo curto. Trocar isso e uma decisao com teste proprio, nao um efeito
// colateral desta linha.
void defaultLayout(ImGuiID dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    const ImVec2 work = ImGui::GetMainViewport()->WorkSize;
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
    // A BARRA DE RENDITIONS, ACIMA DO CANVAS (T4). É onde o alvo a põe, e é a
    // única posição em que ela não disputa largura com a sidebar nem com o
    // inspetor: os seis itens são uma FILA, e uma fila quer o eixo comprido.
    //
    // `[DEC]` A altura. `WindowLayoutConstants` (laudo 19/09 §4.2) tem nove
    // constantes e nenhuma é da barra, então 190 pt é derivado do que ela
    // desenha -- miniatura de 92, botão de título, a linha do relógio e as
    // margens --, não medido. Em pixels e não em razão, pela mesma lição das
    // larguras logo acima: uma razão não tem piso, e numa janela alta a barra
    // engoliria o canvas.
    const float top190 = std::clamp(190.0f / (work.y > 1.0f ? work.y : 1.0f), 0.08f, 0.4f);
    const ImGuiID top = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Up, top190, nullptr, &centre);
    const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.22f, nullptr, &centre);
    ImGui::DockBuilderDockWindow(ick::kLayersWindow, left);
    ImGui::DockBuilderDockWindow(ick::kRenditionsWindow, top);
    ImGui::DockBuilderDockWindow(ick::kCanvasWindow, centre);
    ImGui::DockBuilderDockWindow(ick::kInspectorWindow, right);
    // "Log" is Onyx's and this tree does not create it, but it is worth
    // keeping -- it prints the adapter and the swapchain. Undocked it floats
    // over the Layers tree, so it goes to the bottom as a tab, and
    // Diagnostics is docked LAST so it is the tab that comes up selected.
#ifndef ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY
    // Onyx's "Viewer" is its DocumentWindow's tab host. Our documents are the
    // Session's and never become tabs there, so it can only ever say "No
    // documents open" -- which, next to an open icon, reads as the open
    // having failed. The registrar hides it outright; on an SDK pin that
    // predates `SetVisible` it cannot be hidden, and then docking it here at
    // least keeps it from floating over the Layers tree.
    ImGui::DockBuilderDockWindow("Viewer", bottom);
#endif
    ImGui::DockBuilderDockWindow("Log", bottom);
    ImGui::DockBuilderDockWindow(ick::kDiagnosticsWindow, bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

}  // namespace

int run(const fs::path& initial) {
    // Validation off: nobody here is checking the driver, and the tower's
    // default (on) costs every frame (spec 13/09 §6). Two VkInstances in one
    // process -- Onyx's and RenderBox's -- is that section's decision too.
    //
    // AND WHAT MAKES THAT SAFE IS NOT THIS LINE. It is that RenderBox reaches
    // every device-level entry point through a table loaded from ITS OWN device
    // (`volkLoadDeviceTable`, Source/RenderBox/VulkanApi.h). Onyx calls
    // `volkLoadDevice` (VkContext.cpp:362), which owns volk's GLOBAL table for
    // the rest of the process; a second device that read that table would post
    // this render to ONYX's device, and post it without crashing. So the
    // condition is exact and it is checkable: this second device is legal only
    // while `IC_RB_DEVICE_FUNCTIONS` names every device-level function the tower
    // calls. Writing a bare `vkFoo(device, ...)` anywhere in RenderBox -- rather
    // than `api().vkFoo(...)` -- compiles in both builds and re-opens the hole in
    // the UI build alone, where nothing would report it.
    //
    // This was challenged on 15/09 by a branch that adopted Onyx's device
    // instead, on the premise that one dispatch table per process leaves no
    // choice. Measured and rejected: laudo 2026-09-15-device-do-onyx.md.
    auto device = rb::Device::create(rb::DeviceOptions{.validation = false});
    if (!device) {
        std::fprintf(stderr, "iconcomposer: no Vulkan device: %s\n", device.error().c_str());
        return 2;
    }

    Onyx::Threading::MarkMainThread();
    Onyx::App::Window::initNative();
    Onyx::App::Window window;

    State state;
    state.window = window.getGLFWwindow();
    // O dispositivo da exportacao e o mesmo do agendador -- ver `State::device`.
    state.device = &*device;
    state.sink = std::make_unique<OnyxTextureSink>(window.vkContext());
    state.scheduler = std::make_unique<JobScheduler>(window.workspace().Jobs(), *device);
    // O multiplexador vive tanto quanto o agendador, e não por documento: ele
    // não guarda nada do documento, só de quem é o render em voo.
    state.mux = std::make_unique<ick::SharedScheduler>(*state.scheduler);

    // DROPPING A `.icon` ON THE WINDOW OPENS IT. Onyx installs an EMPTY drop
    // callback of its own (`Source/App/Window.cpp:191` on the dddce38
    // checkout), so until now every drop was swallowed without a trace. This
    // replaces it: GLFW keeps one callback per window and the last writer
    // wins, so this must run after the `Window` constructor.
    //
    // Only the FIRST path is taken. A multi-selection drop has no meaning for
    // an editor that holds one document, and picking one silently beats
    // opening the last of several.
    // THE USER POINTER IS NOT OURS. Onyx stores its own `Window*` there
    // (`Source/App/Window.cpp:305`) and casts it back in three callbacks
    // (:310, :319, :404); writing ours over it made those read a `State` as a
    // `Window` and the process died on the first resize -- measured, not
    // feared. GLFW passes no user data to a callback, so the one window this
    // process owns is reached through a file-static instead. `run()` is the
    // only writer and there is exactly one `State` per process.
    if (GLFWwindow* w = window.getGLFWwindow()) {
        g_dropTarget = &state;
        glfwSetDropCallback(w, [](GLFWwindow*, int count, const char** paths) {
            if (count < 1 || !paths || !paths[0]) return;
            if (g_dropTarget) g_dropTarget->dropped = fs::path(paths[0]);
        });
    }

    window.app().SetDefaultLayout(&defaultLayout);
    window.app().SetRegistrar([&state, initial](Onyx::App::App& app) {
        state.app = &app;
        if (auto* config = app.getConfig()) config->windowTitle = "Icon Composer";
#ifdef ONYX_HAS_OPEN_FILE_HANDLER
        // ONYX'S `File > Open` IS THE ONE PEOPLE CLICK, so it is the one that
        // has to work. Left to itself it builds its filters from the Workspace
        // modules and probes the path for an owner; this app registers no
        // module, so the filters collapse to "All Files", the dialog is for
        // FILES and a `.icon` is a DIRECTORY, and anything picked is dropped
        // with a warning nobody reads. Claiming it points that item at the
        // same action our own menu raises, and takes Onyx's global Ctrl+O with
        // it so one keystroke stops opening two dialogs.
        //
        // Only the REQUEST is recorded here: this runs mid-frame, and `act()`
        // swaps the session after the panels have drawn.
        //
        // The `#ifdef` is not decoration -- `IC_ONYX_SOURCE_DIR` may be absent
        // and the SHA pinned in CMakeLists.txt predates the hook, so this file
        // has to compile against both.
        app.SetOpenFileHandler([&state] { state.actions.open = true; });
#endif
        // Onyx's generic panels are for game archives; ours replace them.
        app.setPanelVisible("Documents", false);
        app.setPanelVisible("Inspector", false);
#ifdef ONYX_HAS_MENU_ENTRY_FILTER
        // A barra do Onyx fica ACIMA da nossa e tem a cara de menu principal,
        // entao o que esta nela e inerte aqui e pior do que ausente: e um
        // convite. Tres entradas sao dessa especie, medidas 18/09 contra o
        // checkout `dddce38`:
        //
        //   `Export`          -- glTF, DDS e Copy Hash, permanentemente
        //                        cinzas, e o comentario do proprio Onyx diz
        //                        que nunca tiveram corpo. Num editor de icone
        //                        e ruido, e contradiz o nosso `File > Export
        //                        Icon as Image...` logo abaixo.
        //   `File > Close All`-- fecha documentos do Workspace e abas do
        //                        DocumentWindow. Nao registramos nenhum
        //                        documento la e escondemos o DocumentWindow,
        //                        entao o item nao faz nada -- e parece o
        //                        fechar do app, que e o nosso `File > Close`.
        //   `File > Recent Files` -- os recentes do Onyx, rotulados com dica
        //                        de jogo (GOW1/GOW2/GOWR) e abertos pelo
        //                        Workspace. Para nos, lista vazia ou arquivos
        //                        de outro app.
        //
        // `File > Open` fica (nos o reivindicamos acima), `File > Exit` fica
        // (desde esta rodada ele fecha pela porta normal em vez de `exit(0)`),
        // `Options` e `View` ficam, porque funcionam.
        app.SetMenuEntryFilter([](std::string_view menu, std::string_view item) {
            if (menu == "Export") return false;
            if (menu == "File" && (item == "Close All" || item == "Recent Files")) return false;
            return true;
        });
#endif
#ifdef ONYX_HAS_DOCUMENT_WINDOW_VISIBILITY
        // And "Viewer" is not a panel, so `setPanelVisible` never reached it:
        // it is the DocumentWindow's own tab host, drawn straight from
        // `App::frame()`. Our documents belong to the Session, not to Onyx's
        // Workspace (Rule 2 of the architecture spec -- the same reason this
        // app registers no GameModule), so no tab is ever added to it and the
        // window has exactly one thing it can say: "No documents open."
        // Measured 18/09 with `AppIcon-27.icon` open on screen and that line
        // underneath it -- true about that tab host, and read by anyone
        // looking at the screen as the open having silently failed. It is the
        // same trap as the File menu that swallowed the pick, one panel down.
        app.getDocumentWindow().SetVisible(false);
#endif
        app.addPanel(std::make_unique<LayersPanel>(state));
        app.addPanel(std::make_unique<CanvasPanel>(state));
        // DEPOIS do canvas, e isto é a prioridade em ato. O multiplexador
        // (Renditions.h) põe o canvas na frente quando as DUAS filas estão
        // cheias, mas não interrompe um render em voo -- com a raia livre,
        // quem pede primeiro sai primeiro. O coordenador do canvas pede dentro
        // de `CanvasPanel::Draw`, e o tick das miniaturas dentro de
        // `drawRenditions`; nesta ordem o pedido de 512 px sai antes do de 128
        // em todo quadro em que os dois nascem juntos.
        app.addPanel(std::make_unique<RenditionsPanel>(state));
        app.addPanel(std::make_unique<InspectorPanel>(state));
        app.addPanel(std::make_unique<DiagnosticsPanel>(state));
        if (!initial.empty()) state.open(initial);
    });
    window.run();
    // The coordinator returns its texture to the pool before the pool goes, and
    // the pool goes before the VkContext (the window outlives `state`).
    state.close();
    return 0;
}

}  // namespace icapp
